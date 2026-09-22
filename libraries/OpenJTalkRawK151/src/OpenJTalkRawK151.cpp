#define FESTIVAL 1
#define MPL_DEBUG 1
#define HTS_EMBEDDED 1

#include "OpenJTalkRawK151.h"

#include <HTS_hidden.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern "C" void openjtalk_raw_allocator_reset_stats(void);
extern "C" void openjtalk_raw_allocator_get_stats(size_t *, size_t *, size_t *, bool *);
extern "C" void openjtalk_raw_audio_reset_error(void);
extern "C" bool openjtalk_raw_audio_failed(void);

namespace {

void free_weights(float *duration_iw, float **parameter_iw,
                  float **gv_iw, size_t voices)
{
    if (parameter_iw != nullptr) {
        for (size_t i = 0; i < voices; ++i) HTS_free(parameter_iw[i]);
        HTS_free(parameter_iw);
    }
    if (gv_iw != nullptr) {
        for (size_t i = 0; i < voices; ++i) HTS_free(gv_iw[i]);
        HTS_free(gv_iw);
    }
    HTS_free(duration_iw);
}

bool allocate_weights(size_t voices, size_t streams,
                      float **duration_iw, float ***parameter_iw,
                      float ***gv_iw)
{
    *duration_iw = (float *) HTS_calloc(voices, sizeof(float));
    *parameter_iw = (float **) HTS_calloc(voices, sizeof(float *));
    *gv_iw = (float **) HTS_calloc(voices, sizeof(float *));
    const float weight = voices == 0 ? 0.0f : 1.0f / (float) voices;
    for (size_t i = 0; i < voices; ++i) {
        (*duration_iw)[i] = weight;
        (*parameter_iw)[i] = (float *) HTS_calloc(streams, sizeof(float));
        (*gv_iw)[i] = (float *) HTS_calloc(streams, sizeof(float));
        for (size_t j = 0; j < streams; ++j) {
            (*parameter_iw)[i][j] = weight;
            (*gv_iw)[i][j] = weight;
        }
    }
    return true;
}

void apply_half_tone(HTS_SStreamSet *sss, float half_tone)
{
    if (half_tone == 0.0f || HTS_SStreamSet_get_nstream(sss) < 2) return;
    const size_t states = HTS_SStreamSet_get_total_state(sss);
    for (size_t i = 0; i < states; ++i) {
        float lf0 = HTS_SStreamSet_get_mean(sss, 1, i, 0) + half_tone * HALF_TONE;
        if (lf0 < MIN_LF0) lf0 = MIN_LF0;
        if (lf0 > MAX_LF0) lf0 = MAX_LF0;
        HTS_SStreamSet_set_mean(sss, 1, i, 0, lf0);
    }
}

}  // namespace

extern "C" bool openjtalk_raw_synthesize(const uint8_t *raw_voice,
                                           size_t raw_voice_size,
                                           char **labels,
                                           size_t label_count,
                                           const OpenJTalkRawConfig *config,
                                           OpenJTalkRawStats *stats)
{
    if (raw_voice == nullptr || raw_voice_size == 0 || labels == nullptr ||
        label_count == 0 || config == nullptr || stats == nullptr ||
        config->audio_buffer_samples == 0) {
        return false;
    }

    HTS_ModelSet ms;
    HTS_Label label;
    HTS_SStreamSet sss;
    HTS_PStreamSet pss;
    HTS_GStreamSet gss;
    HTS_Audio audio;
    float *duration_iw = nullptr;
    float **parameter_iw = nullptr;
    float **gv_iw = nullptr;
    float *msd_threshold = nullptr;
    float *gv_weight = nullptr;
    size_t voices = 0;
    size_t streams = 0;
    float alpha = 0.0f;
    float volume = 1.0f;
    HTS_Boolean stop = FALSE;
    bool ok = false;

    memset(stats, 0, sizeof(*stats));
    openjtalk_raw_allocator_reset_stats();
    openjtalk_raw_audio_reset_error();
    HTS_ModelSet_initialize(&ms);
    HTS_Label_initialize(&label);
    HTS_SStreamSet_initialize(&sss);
    HTS_PStreamSet_initialize(&pss);
    HTS_GStreamSet_initialize(&gss);
    HTS_Audio_initialize(&audio);

    if (HTS_ModelSet_load_raw_from_data(&ms, raw_voice, raw_voice_size) != TRUE) goto cleanup;
    stats->sample_rate = HTS_ModelSet_get_sampling_frequency(&ms);
    stats->frame_period = HTS_ModelSet_get_fperiod(&ms);
    stats->label_count = label_count;
    voices = HTS_ModelSet_get_nvoices(&ms);
    streams = HTS_ModelSet_get_nstream(&ms);
    if (voices == 0 || streams == 0) goto cleanup;

    HTS_Label_load_from_strings(&label, stats->sample_rate,
                                stats->frame_period, labels, label_count);
    if (HTS_Label_get_size(&label) == 0) goto cleanup;
    allocate_weights(voices, streams, &duration_iw, &parameter_iw, &gv_iw);
    msd_threshold = (float *) HTS_calloc(streams, sizeof(float));
    gv_weight = (float *) HTS_calloc(streams, sizeof(float));
    for (size_t i = 0; i < streams; ++i) {
        msd_threshold[i] = 0.5f;
        gv_weight[i] = 1.0f;
    }

    if (HTS_SStreamSet_create(&sss, &ms, &label, FALSE,
                              config->speed > 0.0f ? config->speed : 1.0f,
                              duration_iw, parameter_iw, gv_iw) != TRUE) goto cleanup;
    apply_half_tone(&sss, config->half_tone);

    HTS_Label_clear(&label);
    HTS_ModelSet_clear(&ms);
    if (HTS_PStreamSet_create_consuming_sstream(&pss, &sss,
                                                 msd_threshold, gv_weight) != TRUE) goto cleanup;
    HTS_SStreamSet_clear(&sss);

    HTS_Audio_set_parameter(&audio, stats->sample_rate,
                            config->audio_buffer_samples);
    alpha = config->alpha > 0.0f
        ? config->alpha : (stats->sample_rate == 16000 ? 0.42f : 0.55f);
    volume = expf(config->volume_db * 0.11512925464970229f);
    if (HTS_GStreamSet_create(&gss, &pss, 0, FALSE, stats->sample_rate,
                              stats->frame_period, alpha, config->beta,
                              &stop, volume, &audio) != TRUE) goto cleanup;
    stats->total_frames = HTS_GStreamSet_get_total_frame(&gss);
    stats->total_samples = HTS_GStreamSet_get_total_nsamples(&gss);
    ok = !openjtalk_raw_audio_failed();

cleanup:
    HTS_Audio_clear(&audio);
    HTS_GStreamSet_clear(&gss);
    HTS_PStreamSet_clear(&pss);
    HTS_SStreamSet_clear(&sss);
    HTS_Label_clear(&label);
    HTS_ModelSet_clear(&ms);
    HTS_free(msd_threshold);
    HTS_free(gv_weight);
    free_weights(duration_iw, parameter_iw, gv_iw, voices);
    openjtalk_raw_allocator_get_stats(&stats->current_allocated_bytes,
                                      &stats->peak_allocated_bytes,
                                      &stats->allocation_count,
                                      &stats->used_psram);
    return ok;
}
