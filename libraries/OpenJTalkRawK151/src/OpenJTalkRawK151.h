#ifndef OPENJTALK_RAW_K151_H
#define OPENJTALK_RAW_K151_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*OpenJTalkRawAudioSink)(const int16_t *samples,
                                     size_t sample_count,
                                     uint32_t sample_rate,
                                     bool final_block);

typedef struct OpenJTalkRawConfig {
    size_t audio_buffer_samples;
    float alpha;
    float beta;
    float speed;
    float volume_db;
    float half_tone;
} OpenJTalkRawConfig;

typedef struct OpenJTalkRawStats {
    size_t sample_rate;
    size_t frame_period;
    size_t label_count;
    size_t total_frames;
    size_t total_samples;
    size_t current_allocated_bytes;
    size_t peak_allocated_bytes;
    size_t allocation_count;
    bool used_psram;
} OpenJTalkRawStats;

void openjtalk_raw_set_audio_sink(OpenJTalkRawAudioSink sink);

bool openjtalk_raw_synthesize(const uint8_t *raw_voice,
                              size_t raw_voice_size,
                              char **labels,
                              size_t label_count,
                              const OpenJTalkRawConfig *config,
                              OpenJTalkRawStats *stats);

#ifdef __cplusplus
}
#endif

#endif
