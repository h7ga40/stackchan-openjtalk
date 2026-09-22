#define FESTIVAL 1
#define MPL_DEBUG 1
#define HTS_EMBEDDED 1

#include "OpenJTalkRawK151.h"
#include <HTS_hidden.h>

static OpenJTalkRawAudioSink audio_sink;
static bool audio_failed;

void openjtalk_raw_set_audio_sink(OpenJTalkRawAudioSink sink)
{
    audio_sink = sink;
}

void openjtalk_raw_audio_reset_error(void)
{
    audio_failed = false;
}

bool openjtalk_raw_audio_failed(void)
{
    return audio_failed;
}

void HTS_Audio_initialize(HTS_Audio *audio)
{
    if (audio == NULL) return;
    audio->sampling_frequency = 0;
    audio->max_buff_size = 0;
    audio->buff = NULL;
    audio->buff_size = 0;
    audio->audio_interface = NULL;
}

void HTS_Audio_set_parameter(HTS_Audio *audio,
                             size_t sampling_frequency,
                             size_t max_buff_size)
{
    if (audio == NULL) return;
    HTS_Audio_clear(audio);
    if (sampling_frequency == 0 || max_buff_size == 0) return;
    audio->sampling_frequency = sampling_frequency;
    audio->max_buff_size = max_buff_size;
    audio->buff = (short *) HTS_calloc(max_buff_size, sizeof(short));
    audio->audio_interface = audio_sink != NULL ? (void *) audio_sink : NULL;
}

static void submit_audio(HTS_Audio *audio)
{
    if (audio == NULL || audio->buff_size == 0) return;
    if (audio_sink == NULL ||
        !audio_sink(audio->buff, audio->buff_size,
                    (uint32_t) audio->sampling_frequency, false)) {
        audio_failed = true;
    }
    audio->buff_size = 0;
}

void HTS_Audio_write(HTS_Audio *audio, short data)
{
    if (audio == NULL || audio->buff == NULL || audio_failed) return;
    audio->buff[audio->buff_size++] = data;
    if (audio->buff_size >= audio->max_buff_size) submit_audio(audio);
}

void HTS_Audio_flush(HTS_Audio *audio)
{
    if (audio == NULL) return;
    submit_audio(audio);
    if (audio_sink != NULL && !audio_sink(NULL, 0,
                                          (uint32_t) audio->sampling_frequency,
                                          true)) {
        audio_failed = true;
    }
}

void HTS_Audio_clear(HTS_Audio *audio)
{
    if (audio == NULL) return;
    if (audio->buff != NULL) {
        HTS_Audio_flush(audio);
        HTS_free(audio->buff);
    }
    HTS_Audio_initialize(audio);
}

