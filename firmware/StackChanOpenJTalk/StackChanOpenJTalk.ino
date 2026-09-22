#include <Arduino.h>
#include <FS.h>
#include <M5StackChan.h>
#include <OpenJTalkFrontendK151.h>
#include <OpenJTalkRawK151.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>

SET_LOOP_TASK_STACK_SIZE(32768);

namespace {

constexpr gpio_num_t kSdCsPin = GPIO_NUM_4;
constexpr char kVoicePath[] = "/openjtalk/mei_normal_16.raw";
constexpr char kLabelsPath[] = "/openjtalk/test.lab";
constexpr char kDictionaryPath[] = "/sd/openjtalk/dic";
constexpr size_t kAudioBlockSamples = 1024;
constexpr size_t kAudioBufferCount = 3;
constexpr size_t kMaxLabels = 128;
constexpr size_t kMaxLabelLength = 1024;
constexpr uint32_t kSerialLabelTimeoutMs = 15000;
constexpr uint8_t kSpeakerChannel = 0;
constexpr uint8_t kSpeakerVolume = 160;
constexpr float kDefaultHalfTone = 3.0f;

int16_t *audioBuffers[kAudioBufferCount] = {};
size_t nextAudioBuffer = 0;
uint8_t *rawVoice = nullptr;
size_t rawVoiceSize = 0;
OpenJTalkFrontend *frontend = nullptr;
bool frontendReady = false;

struct LabelFile {
    char *text = nullptr;
    char **lines = nullptr;
    size_t count = 0;
    bool ownsIndividualLines = false;
};

void showStatus(const char *line1, const char *line2 = nullptr, uint32_t color = TFT_WHITE)
{
    auto& display = M5StackChan.Display();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(color, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(8, 18);
    display.println(line1);
    if (line2 != nullptr) {
        display.setTextSize(1);
        display.println(line2);
    }
}

bool allocateAudioBuffers()
{
    for (size_t i = 0; i < kAudioBufferCount; ++i) {
        audioBuffers[i] = static_cast<int16_t *>(heap_caps_malloc(
            kAudioBlockSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (audioBuffers[i] == nullptr) return false;
    }
    return true;
}

bool speakerSink(const int16_t *samples, size_t sampleCount,
                 uint32_t sampleRate, bool finalBlock)
{
    if (finalBlock) {
        while (M5.Speaker.isPlaying(kSpeakerChannel)) delay(5);
        return true;
    }
    if (samples == nullptr || sampleCount == 0 || sampleCount > kAudioBlockSamples) return false;

    int16_t *buffer = audioBuffers[nextAudioBuffer];
    memcpy(buffer, samples, sampleCount * sizeof(int16_t));
    while (!M5.Speaker.playRaw(buffer, sampleCount, sampleRate, false, 1,
                               kSpeakerChannel, false)) {
        delay(1);
    }
    nextAudioBuffer = (nextAudioBuffer + 1) % kAudioBufferCount;
    return true;
}

uint8_t *loadBinaryToPsram(const char *path, size_t *size)
{
    *size = 0;
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return nullptr;
    const size_t length = file.size();
    uint8_t *data = static_cast<uint8_t *>(heap_caps_malloc(
        length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
        file.close();
        return nullptr;
    }
    size_t offset = 0;
    while (offset < length) {
        const size_t received = file.read(data + offset, length - offset);
        if (received == 0) break;
        offset += received;
    }
    file.close();
    if (offset != length) {
        heap_caps_free(data);
        return nullptr;
    }
    *size = length;
    return data;
}

bool loadLabels(const char *path, LabelFile *labels)
{
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    const size_t length = file.size();
    labels->text = static_cast<char *>(heap_caps_malloc(
        length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    labels->lines = static_cast<char **>(heap_caps_calloc(
        kMaxLabels, sizeof(char *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (labels->text == nullptr || labels->lines == nullptr) {
        file.close();
        return false;
    }
    const size_t received = file.readBytes(labels->text, length);
    file.close();
    if (received != length) return false;
    labels->text[length] = '\0';

    char *cursor = labels->text;
    while (*cursor != '\0') {
        while (*cursor == '\r' || *cursor == '\n') ++cursor;
        if (*cursor == '\0') break;
        if (labels->count >= kMaxLabels) return false;
        labels->lines[labels->count++] = cursor;
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') ++cursor;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    return labels->count > 0;
}

void releaseLabels(LabelFile *labels)
{
    if (labels->ownsIndividualLines && labels->lines != nullptr) {
        for (size_t i = 0; i < labels->count; ++i) heap_caps_free(labels->lines[i]);
    }
    heap_caps_free(labels->lines);
    heap_caps_free(labels->text);
    *labels = {};
}

bool waitForSerialData(uint32_t timeoutMs)
{
    const uint32_t started = millis();
    while (!Serial.available()) {
        M5StackChan.update();
        if (millis() - started >= timeoutMs) return false;
        delay(2);
    }
    return true;
}

bool receiveSerialLabels(size_t count, LabelFile *labels)
{
    if (count == 0 || count > kMaxLabels) return false;
    labels->lines = static_cast<char **>(heap_caps_calloc(
        count, sizeof(char *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (labels->lines == nullptr) return false;
    labels->ownsIndividualLines = true;

    for (size_t i = 0; i < count; ++i) {
        if (!waitForSerialData(kSerialLabelTimeoutMs)) return false;
        String line = Serial.readStringUntil('\n');
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        if (line.length() == 0 || line.length() > kMaxLabelLength) return false;
        labels->lines[i] = static_cast<char *>(heap_caps_malloc(
            line.length() + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (labels->lines[i] == nullptr) return false;
        memcpy(labels->lines[i], line.c_str(), line.length() + 1);
        labels->count++;
        Serial.printf("STACKCHAN_OPENJTALK: LABEL_OK index=%u\n",
                      static_cast<unsigned>(labels->count));
    }
    return true;
}

bool synthesizeLabels(LabelFile *labels, const char *source, float halfTone)
{
    const OpenJTalkRawConfig config = {
        kAudioBlockSamples,
        0.42f,
        0.0f,
        1.0f,
        0.0f,
        halfTone,
    };
    OpenJTalkRawStats stats = {};
    Serial.printf("STACKCHAN_OPENJTALK: START source=%s voice_bytes=%u labels=%u half_tone=%.2f psram_free=%u\n",
                  source, static_cast<unsigned>(rawVoiceSize),
                  static_cast<unsigned>(labels->count), halfTone,
                  static_cast<unsigned>(ESP.getFreePsram()));
    showStatus("OpenJTalk K151-R", "Synthesizing...");
    const uint32_t startedMs = millis();
    const bool ok = openjtalk_raw_synthesize(rawVoice, rawVoiceSize,
                                             labels->lines, labels->count,
                                             &config, &stats);
    const uint32_t elapsedMs = millis() - startedMs;

    Serial.printf(
        "STACKCHAN_OPENJTALK: RESULT=%s source=%s rate=%u frame_period=%u frames=%u samples=%u "
        "elapsed_ms=%u peak_hts_bytes=%u psram_free=%u\n",
        ok ? "PASS" : "FAIL", source, static_cast<unsigned>(stats.sample_rate),
        static_cast<unsigned>(stats.frame_period), static_cast<unsigned>(stats.total_frames),
        static_cast<unsigned>(stats.total_samples), static_cast<unsigned>(elapsedMs),
        static_cast<unsigned>(stats.peak_allocated_bytes),
        static_cast<unsigned>(ESP.getFreePsram()));
    return ok;
}

void fail(const char *reason);

void finishRequest(bool ok)
{
    if (ok) {
        M5StackChan.showRgbColor(0, 20, 0);
        showStatus("OpenJTalk READY", "Waiting for serial text", TFT_GREEN);
    } else {
        fail("SERIAL_SYNTHESIS");
    }
    Serial.println("STACKCHAN_OPENJTALK: IDLE command=OPENJTALK_TEXT|OPENJTALK_LABELS");
}

void handleTextCommand(float halfTone)
{
    if (!frontendReady) {
        Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=FRONTEND_NOT_READY");
        return;
    }
    Serial.println("STACKCHAN_OPENJTALK: TEXT_READY encoding=UTF-8");
    if (!waitForSerialData(kSerialLabelTimeoutMs)) {
        Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=TEXT_TIMEOUT");
        return;
    }
    String text = Serial.readStringUntil('\n');
    if (text.endsWith("\r")) text.remove(text.length() - 1);
    if (text.length() == 0 || text.length() >= 1024) {
        Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=TEXT_LENGTH");
        return;
    }

    showStatus("OpenJTalk K151-R", "Analyzing Japanese...");
    const uint32_t startedMs = millis();
    const size_t psramBefore = ESP.getFreePsram();
    if (OpenJTalkFrontend_make_labels(frontend, text.c_str()) != 1) {
        OpenJTalkFrontend_refresh(frontend);
        Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=LABEL_GENERATION");
        return;
    }
    const int labelCount = OpenJTalkFrontend_get_label_size(frontend);
    char **labelLines = OpenJTalkFrontend_get_label_feature(frontend);
    if (labelLines == nullptr || labelCount <= 0 || labelCount > static_cast<int>(kMaxLabels)) {
        OpenJTalkFrontend_refresh(frontend);
        Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=LABEL_COUNT");
        return;
    }
    Serial.printf("STACKCHAN_OPENJTALK: LABELS source=DEVICE count=%u elapsed_ms=%u psram_used=%u\n",
                  static_cast<unsigned>(labelCount),
                  static_cast<unsigned>(millis() - startedMs),
                  static_cast<unsigned>(psramBefore - ESP.getFreePsram()));
    LabelFile labels;
    labels.lines = labelLines;
    labels.count = static_cast<size_t>(labelCount);
    const bool ok = synthesizeLabels(&labels, "DEVICE", halfTone);
    OpenJTalkFrontend_refresh(frontend);
    finishRequest(ok);
}

void fail(const char *reason)
{
    Serial.printf("STACKCHAN_OPENJTALK: ERROR reason=%s\n", reason);
    showStatus("OpenJTalk ERROR", reason, TFT_RED);
    M5StackChan.showRgbColor(24, 0, 0);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("STACKCHAN_OPENJTALK: BOOT");

    M5StackChan.begin();
    M5StackChan.Motion.setAutoTorqueReleaseEnabled(false);
    M5StackChan.setServoPowerEnabled(false);
    M5StackChan.showRgbColor(0, 0, 16);
    showStatus("OpenJTalk K151-R", "Loading 16 kHz HTSRAW2...");

    if (ESP.getPsramSize() == 0) {
        fail("PSRAM_NOT_FOUND");
        return;
    }
    if (!allocateAudioBuffers()) {
        fail("AUDIO_BUFFER_ALLOC");
        return;
    }
    if (!SD.begin(kSdCsPin, SPI, 25000000)) {
        fail("SD_MOUNT");
        return;
    }

    rawVoice = loadBinaryToPsram(kVoicePath, &rawVoiceSize);
    if (rawVoice == nullptr) {
        fail("VOICE_LOAD");
        return;
    }

    frontend = OpenJTalkFrontend_create();
    if (frontend != nullptr && OpenJTalkFrontend_load(frontend, kDictionaryPath) == 1) {
        frontendReady = true;
        Serial.printf("STACKCHAN_OPENJTALK: FRONTEND=READY dic=%s psram_free=%u\n",
                      kDictionaryPath, static_cast<unsigned>(ESP.getFreePsram()));
    } else {
        if (frontend != nullptr) {
            OpenJTalkFrontend_destroy(frontend);
            frontend = nullptr;
        }
        Serial.printf("STACKCHAN_OPENJTALK: FRONTEND=DISABLED dic=%s\n", kDictionaryPath);
    }
    LabelFile labels;
    if (!loadLabels(kLabelsPath, &labels)) {
        heap_caps_free(rawVoice);
        fail("LABEL_LOAD");
        return;
    }

    M5.Speaker.setVolume(kSpeakerVolume);
    openjtalk_raw_set_audio_sink(speakerSink);
    const bool ok = synthesizeLabels(&labels, "SD", kDefaultHalfTone);

    releaseLabels(&labels);
    if (ok) {
        M5StackChan.showRgbColor(0, 20, 0);
        showStatus("OpenJTalk READY", "Waiting for serial text", TFT_GREEN);
        Serial.println("STACKCHAN_OPENJTALK: IDLE command=OPENJTALK_TEXT|OPENJTALK_LABELS");
    } else {
        fail("SYNTHESIS");
    }
}

void loop()
{
    M5StackChan.update();
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        float textHalfTone = kDefaultHalfTone;
        if (sscanf(command.c_str(), "OPENJTALK_TEXT %f", &textHalfTone) == 1) {
            if (textHalfTone < -12.0f || textHalfTone > 12.0f) {
                Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=PITCH_RANGE");
            } else {
                handleTextCommand(textHalfTone);
            }
            delay(10);
            return;
        }
        unsigned int requestedCount = 0;
        float requestedHalfTone = kDefaultHalfTone;
        const int commandFields = sscanf(command.c_str(), "OPENJTALK_LABELS %u %f",
                                         &requestedCount, &requestedHalfTone);
        if (commandFields >= 1 &&
            requestedCount > 0 && requestedCount <= kMaxLabels) {
            if (requestedHalfTone < -12.0f || requestedHalfTone > 12.0f) {
                Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=PITCH_RANGE");
                delay(10);
                return;
            }
            while (Serial.available()) Serial.read();
            Serial.printf("STACKCHAN_OPENJTALK: READY labels=%u\n", requestedCount);
            LabelFile labels;
            if (!receiveSerialLabels(requestedCount, &labels)) {
                releaseLabels(&labels);
                Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=LABEL_RECEIVE");
                fail("SERIAL_LABELS");
            } else {
                Serial.printf("STACKCHAN_OPENJTALK: RECEIVED labels=%u\n",
                              static_cast<unsigned>(labels.count));
                const bool ok = synthesizeLabels(&labels, "SERIAL", requestedHalfTone);
                releaseLabels(&labels);
                finishRequest(ok);
            }
        } else if (command.length() > 0) {
            Serial.println("STACKCHAN_OPENJTALK: SERIAL_ERROR reason=UNKNOWN_COMMAND");
        }
    }
    delay(10);
}
