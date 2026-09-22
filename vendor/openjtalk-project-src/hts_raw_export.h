#ifndef HTS_RAW_EXPORT_H
#define HTS_RAW_EXPORT_H

#include <stddef.h>

int hts_export_voice_to_raw(const char *voice_path, const char *output_path,
                            size_t sample_rate, size_t frame_period,
                            int show_memory_stats);

#endif
