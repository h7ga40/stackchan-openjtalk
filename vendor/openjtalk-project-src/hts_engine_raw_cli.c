#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HTS_engine.h"
#include "HTS_hidden.h"
#include "hts_raw_export.h"
#include "hts_walloc.h"

typedef enum HtsEngineRawCliCommand {
   HTS_ENGINE_RAW_CLI_COMMAND_EXPORT_VOICE,
   HTS_ENGINE_RAW_CLI_COMMAND_CHECK_SSTREAM,
   HTS_ENGINE_RAW_CLI_COMMAND_CHECK_SSTREAM_VIEW,
   HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM,
   HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM_VIEW,
   HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM,
   HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW,
   HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW,
   HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_STREAM_VIEW
} HtsEngineRawCliCommand;

typedef enum HtsEngineRawCliProfile {
   HTS_ENGINE_RAW_CLI_PROFILE_RAW,
   HTS_ENGINE_RAW_CLI_PROFILE_48KHZ,
   HTS_ENGINE_RAW_CLI_PROFILE_16KHZ
} HtsEngineRawCliProfile;

typedef struct HtsEngineRawCliOptions {
   HtsEngineRawCliCommand command;
   HtsEngineRawCliProfile profile;
   const char *voice_path;
   const char *output_path;
   const char *raw_path;
   const char *labels_path;
   const char *user_dictionary_path;
   const char *wav_path;
   size_t wav_sample_rate;
   size_t audio_buffer_size;
   size_t pstream_block_frames;
   float alpha;
   float beta;
   float speed;
   float half_tone;
   float msd_threshold;
   float gv_spectrum_weight;
   float gv_lf0_weight;
   float volume_db;
   int alpha_specified;
   int use_stdin;
   int show_memory_stats;
   int show_help;
} HtsEngineRawCliOptions;

typedef struct LabelBlockRange {
   size_t start;
   size_t count;
   size_t frame_count;
} LabelBlockRange;

static void stream_protocol_print(const char *format, ...);

static void print_usage(FILE *stream)
{
   fprintf(stream, "Usage:\n");
   fprintf(stream, "  hts_engine_raw_cli.exe export-voice --voice VOICE.htsvoice --output VOICE.raw [--tts-48khz|--tts-16khz] [--memory-stats]\n");
   fprintf(stream, "  hts_engine_raw_cli.exe check-sstream --raw VOICE.raw --labels LABELS.txt [--memory-stats]\n");
   fprintf(stream, "  hts_engine_raw_cli.exe check-sstream-view --raw VOICE.raw --labels LABELS.txt [--memory-stats]\n");
   fprintf(stream, "  hts_engine_raw_cli.exe check-pstream --raw VOICE.raw --labels LABELS.txt [--memory-stats]\n");
   fprintf(stream, "  hts_engine_raw_cli.exe check-pstream-view --raw VOICE.raw --labels LABELS.txt [--memory-stats]\n");
   fprintf(stream, "  hts_engine_raw_cli.exe check-gstream --raw VOICE.raw --labels LABELS.txt [--wav OUTPUT.wav] [--memory-stats]\n");
   fprintf(stream, "  type LABELS.txt | hts_engine_raw_cli.exe check-gstream-view --raw VOICE.raw --stdin --wav OUTPUT.wav [--memory-stats]\n");
   fprintf(stream, "  type LABELS.txt | hts_engine_raw_cli.exe synthesize-view --raw VOICE.raw --stdin --wav OUTPUT.wav [--memory-stats]\n");
   fprintf(stream, "  type CHUNKS.txt | hts_engine_raw_cli.exe synthesize-stream-view --raw VOICE.raw --stdin --wav OUTPUT.wav [--memory-stats] [--pstream-block-frames N]\n");
   fprintf(stream, "\nOptions:\n");
   fprintf(stream, "  --voice PATH     HTS voice file to export.\n");
   fprintf(stream, "  --output PATH    Raw HTS voice export destination.\n");
   fprintf(stream, "  --raw PATH       Raw HTSRAW2 voice image.\n");
   fprintf(stream, "  --labels PATH    Full-context label text file.\n");
   fprintf(stream, "  --user-dic PATH  Unsupported here; raw engine consumes labels, not text.\n");
   fprintf(stream, "  --stdin          Read full-context labels from standard input.\n");
   fprintf(stream, "  --wav PATH       Optional WAV output for check-gstream commands.\n");
   fprintf(stream, "  --wav-sample-rate N\n");
   fprintf(stream, "                   Optional WAV output sample rate. Source rate must be an integer multiple.\n");
   fprintf(stream, "  --audio-buffer N\n");
   fprintf(stream, "                   Play synthesized audio on Windows using an N-sample buffer.\n");
   fprintf(stream, "  --tts-48khz      Use sample-rate=48000, frame-period=240; synthesis default alpha=0.55.\n");
   fprintf(stream, "  --tts-16khz      Use sample-rate=16000, frame-period=80; synthesis default alpha=0.42.\n");
   fprintf(stream, "  --alpha F        Override the synthesis all-pass constant.\n");
   fprintf(stream, "  --beta F         Set the postfiltering coefficient (0.0 to 1.0).\n");
   fprintf(stream, "  --speed F        Set the speech speed rate.\n");
   fprintf(stream, "  --half-tone F    Shift log F0 by F semitones.\n");
   fprintf(stream, "  --msd-threshold F\n");
   fprintf(stream, "                   Set the log F0 voiced/unvoiced threshold (0.0 to 1.0).\n");
   fprintf(stream, "  --gv-spectrum-weight F\n");
   fprintf(stream, "                   Set the spectrum GV weight.\n");
   fprintf(stream, "  --gv-lf0-weight F\n");
   fprintf(stream, "                   Set the log F0 GV weight.\n");
   fprintf(stream, "  --volume-db F    Set the output volume in dB.\n");
   fprintf(stream, "  --memory-stats   Print HTS allocator memory statistics to stderr.\n");
   fprintf(stream, "  --pstream-block-frames N\n");
   fprintf(stream, "                   Experimental: split stream chunks into label blocks with about N frames before PStream generation.\n");
   fprintf(stream, "  --help           Show this help text.\n");
}

static int option_requires_value(int argc, int index, const char *option)
{
   if (index + 1 < argc)
      return 0;
   fprintf(stderr, "error: %s requires a value\n", option);
   return 1;
}

static int parse_options(int argc, char **argv, HtsEngineRawCliOptions *options)
{
   int i;

   memset(options, 0, sizeof(*options));
   options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_SSTREAM;
   options->profile = HTS_ENGINE_RAW_CLI_PROFILE_RAW;
   options->speed = 1.0f;
   options->msd_threshold = 0.5f;
   options->gv_spectrum_weight = 1.0f;
   options->gv_lf0_weight = 1.0f;

   if (argc >= 2 && strcmp(argv[1], "export-voice") != 0 &&
       strcmp(argv[1], "check-sstream") != 0 &&
       strcmp(argv[1], "check-sstream-view") != 0 &&
       strcmp(argv[1], "check-pstream") != 0 &&
       strcmp(argv[1], "check-pstream-view") != 0 &&
       strcmp(argv[1], "check-gstream") != 0 &&
       strcmp(argv[1], "check-gstream-view") != 0 &&
       strcmp(argv[1], "synthesize-view") != 0 &&
       strcmp(argv[1], "synthesize-stream-view") != 0 &&
       strcmp(argv[1], "--help") != 0 && strcmp(argv[1], "-h") != 0) {
      fprintf(stderr, "error: unknown command: %s\n", argv[1]);
      return 0;
   }

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "check-sstream") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_SSTREAM;
      } else if (strcmp(argv[i], "export-voice") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_EXPORT_VOICE;
      } else if (strcmp(argv[i], "check-sstream-view") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_SSTREAM_VIEW;
      } else if (strcmp(argv[i], "check-pstream") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM;
      } else if (strcmp(argv[i], "check-pstream-view") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM_VIEW;
      } else if (strcmp(argv[i], "check-gstream") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM;
      } else if (strcmp(argv[i], "check-gstream-view") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW;
      } else if (strcmp(argv[i], "synthesize-view") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW;
      } else if (strcmp(argv[i], "synthesize-stream-view") == 0) {
         options->command = HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_STREAM_VIEW;
      } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
         options->show_help = 1;
      } else if (strcmp(argv[i], "--raw") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->raw_path = argv[++i];
      } else if (strcmp(argv[i], "--voice") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->voice_path = argv[++i];
      } else if (strcmp(argv[i], "--output") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->output_path = argv[++i];
      } else if (strcmp(argv[i], "--labels") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->labels_path = argv[++i];
      } else if (strcmp(argv[i], "--user-dic") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->user_dictionary_path = argv[++i];
         fprintf(stderr, "error: --user-dic is unsupported by hts_engine_raw_cli.exe because it consumes full-context labels, not text. Use OpenJTalk.exe, openjtalk_label_cli.exe, or openjtalk_kana_cli.exe.\n");
         return 0;
      } else if (strcmp(argv[i], "--stdin") == 0) {
         options->use_stdin = 1;
      } else if (strcmp(argv[i], "--wav") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->wav_path = argv[++i];
      } else if (strcmp(argv[i], "--wav-sample-rate") == 0) {
         char *end;
         unsigned long parsed;

         if (option_requires_value(argc, i, argv[i]))
            return 0;
         parsed = strtoul(argv[++i], &end, 10);
         if (end == argv[i] || *end != '\0' || parsed == 0) {
            fprintf(stderr, "error: --wav-sample-rate requires a positive integer\n");
            return 0;
         }
         options->wav_sample_rate = (size_t) parsed;
      } else if (strcmp(argv[i], "--audio-buffer") == 0) {
         char *end;
         unsigned long parsed;

         if (option_requires_value(argc, i, argv[i]))
            return 0;
         parsed = strtoul(argv[++i], &end, 10);
         if (end == argv[i] || *end != '\0' || parsed == 0) {
            fprintf(stderr, "error: --audio-buffer requires a positive integer\n");
            return 0;
         }
         options->audio_buffer_size = (size_t) parsed;
      } else if (strcmp(argv[i], "--tts-48khz") == 0) {
         if (options->profile != HTS_ENGINE_RAW_CLI_PROFILE_RAW) {
            fprintf(stderr, "error: TTS profile options are mutually exclusive\n");
            return 0;
         }
         options->profile = HTS_ENGINE_RAW_CLI_PROFILE_48KHZ;
      } else if (strcmp(argv[i], "--tts-16khz") == 0) {
         if (options->profile != HTS_ENGINE_RAW_CLI_PROFILE_RAW) {
            fprintf(stderr, "error: TTS profile options are mutually exclusive\n");
            return 0;
         }
         options->profile = HTS_ENGINE_RAW_CLI_PROFILE_16KHZ;
      } else if (strcmp(argv[i], "--alpha") == 0) {
         char *end;
         double parsed;

         if (option_requires_value(argc, i, argv[i]))
            return 0;
         parsed = strtod(argv[++i], &end);
         if (end == argv[i] || *end != '\0' || parsed < 0.0 || parsed > 1.0) {
            fprintf(stderr, "error: --alpha requires a value from 0.0 to 1.0\n");
            return 0;
         }
         options->alpha = (float) parsed;
         options->alpha_specified = 1;
      } else if (strcmp(argv[i], "--beta") == 0 || strcmp(argv[i], "--msd-threshold") == 0) {
         char *end;
         double parsed;
         int is_beta = strcmp(argv[i], "--beta") == 0;

         if (option_requires_value(argc, i, argv[i]))
            return 0;
         parsed = strtod(argv[++i], &end);
         if (end == argv[i] || *end != '\0' || parsed < 0.0 || parsed > 1.0) {
            fprintf(stderr, "error: %s requires a value from 0.0 to 1.0\n", is_beta ? "--beta" : "--msd-threshold");
            return 0;
         }
         if (is_beta)
            options->beta = (float) parsed;
         else
            options->msd_threshold = (float) parsed;
      } else if (strcmp(argv[i], "--speed") == 0 || strcmp(argv[i], "--gv-spectrum-weight") == 0 ||
                 strcmp(argv[i], "--gv-lf0-weight") == 0 || strcmp(argv[i], "--volume-db") == 0 ||
                 strcmp(argv[i], "--half-tone") == 0) {
         char *end;
         double parsed;

         if (option_requires_value(argc, i, argv[i]))
            return 0;
         parsed = strtod(argv[++i], &end);
         if (end == argv[i] || *end != '\0' ||
             ((strcmp(argv[i - 1], "--speed") == 0 || strcmp(argv[i - 1], "--gv-spectrum-weight") == 0 ||
               strcmp(argv[i - 1], "--gv-lf0-weight") == 0) && parsed < 0.0)) {
            fprintf(stderr, "error: %s requires a valid numeric value\n", argv[i - 1]);
            return 0;
         }
         if (strcmp(argv[i - 1], "--speed") == 0 && parsed < 1.0E-06)
            parsed = 1.0E-06;
         if (strcmp(argv[i - 1], "--speed") == 0)
            options->speed = (float) parsed;
         else if (strcmp(argv[i - 1], "--gv-spectrum-weight") == 0)
            options->gv_spectrum_weight = (float) parsed;
         else if (strcmp(argv[i - 1], "--gv-lf0-weight") == 0)
            options->gv_lf0_weight = (float) parsed;
         else if (strcmp(argv[i - 1], "--volume-db") == 0)
            options->volume_db = (float) parsed;
         else
            options->half_tone = (float) parsed;
      } else if (strcmp(argv[i], "--memory-stats") == 0) {
         options->show_memory_stats = 1;
      } else if (strcmp(argv[i], "--pstream-block-frames") == 0) {
         char *end;
         unsigned long parsed;

         if (option_requires_value(argc, i, argv[i]))
            return 0;
         parsed = strtoul(argv[++i], &end, 10);
         if (end == argv[i] || *end != '\0' || parsed == 0) {
            fprintf(stderr, "error: --pstream-block-frames requires a positive integer\n");
            return 0;
         }
         options->pstream_block_frames = (size_t) parsed;
      } else {
         fprintf(stderr, "error: unknown option: %s\n", argv[i]);
         return 0;
      }
   }

   return 1;
}

static int file_exists(const char *path)
{
   FILE *fp = fopen(path, "rb");

   if (fp == NULL)
      return 0;
   fclose(fp);
   return 1;
}

static void resolve_synthesis_profile(const HtsEngineRawCliOptions *options, size_t raw_sampling_rate, size_t raw_fperiod,
                                      size_t *sampling_rate, size_t *fperiod, float *alpha)
{
   *sampling_rate = raw_sampling_rate;
   *fperiod = raw_fperiod;

   if (options->profile == HTS_ENGINE_RAW_CLI_PROFILE_48KHZ) {
      *sampling_rate = 48000;
      *fperiod = 240;
   } else if (options->profile == HTS_ENGINE_RAW_CLI_PROFILE_16KHZ) {
      *sampling_rate = 16000;
      *fperiod = 80;
   }

   if (options->alpha_specified) {
      *alpha = options->alpha;
   } else if (*sampling_rate == 16000 && *fperiod == 80) {
      *alpha = 0.42f;
   } else {
      *alpha = 0.55f;
   }
}

static void resolve_export_profile(const HtsEngineRawCliOptions *options, size_t *sampling_rate, size_t *fperiod)
{
   *sampling_rate = 0;
   *fperiod = 0;

   if (options->profile == HTS_ENGINE_RAW_CLI_PROFILE_48KHZ) {
      *sampling_rate = 48000;
      *fperiod = 240;
   } else if (options->profile == HTS_ENGINE_RAW_CLI_PROFILE_16KHZ) {
      *sampling_rate = 16000;
      *fperiod = 80;
   }
}

static void free_file_data(unsigned char *data)
{
   free(data);
}

static int load_file_data(const char *path, unsigned char **out_data, size_t *out_size)
{
   FILE *fp;
   long file_size_long;
   size_t file_size;
   unsigned char *data;

   *out_data = NULL;
   *out_size = 0;

   fp = fopen(path, "rb");
   if (fp == NULL) {
      fprintf(stderr, "error: failed to open file '%s': errno=%d\n", path, errno);
      return 0;
   }
   if (fseek(fp, 0, SEEK_END) != 0) {
      fclose(fp);
      return 0;
   }
   file_size_long = ftell(fp);
   if (file_size_long <= 0) {
      fclose(fp);
      return 0;
   }
   if (fseek(fp, 0, SEEK_SET) != 0) {
      fclose(fp);
      return 0;
   }

   file_size = (size_t) file_size_long;
   data = (unsigned char *) malloc(file_size);
   if (data == NULL) {
      fclose(fp);
      fprintf(stderr, "error: failed to allocate %zu bytes for '%s'\n", file_size, path);
      return 0;
   }
   if (fread(data, 1, file_size, fp) != file_size) {
      fclose(fp);
      free(data);
      fprintf(stderr, "error: failed to read file '%s'\n", path);
      return 0;
   }
   fclose(fp);

   *out_data = data;
   *out_size = file_size;
   return 1;
}

static void print_memory_stats(const char *stage)
{
   HTS_WallocStats stats;

   if (!hts_walloc_get_stats(&stats))
      return;

   fprintf(stderr,
           "memory_stats: stage=%s total_bytes=%zu managed_bytes=%zu used_bytes=%zu free_bytes=%zu max_free_block_bytes=%zu used_blocks=%zu free_blocks=%zu block_header_bytes=%zu used_block_header_bytes=%zu peak_used_bytes=%zu peak_used_blocks=%zu\n",
           stage,
           stats.total_bytes,
           stats.managed_bytes,
           stats.used_bytes,
           stats.free_bytes,
           stats.max_free_block_bytes,
           stats.used_blocks,
           stats.free_blocks,
           stats.block_header_bytes,
           stats.used_block_header_bytes,
           stats.peak_used_bytes,
           stats.peak_used_blocks);
}

static void print_stream_chunk_memory_stats(size_t chunk_id, const HTS_WallocStats *before)
{
   HTS_WallocStats after;
   size_t peak_delta_bytes = 0;
   size_t peak_delta_blocks = 0;

   if (before == NULL)
      return;
   if (!hts_walloc_get_stats(&after))
      return;
   if (after.peak_used_bytes >= before->peak_used_bytes)
      peak_delta_bytes = after.peak_used_bytes - before->peak_used_bytes;
   if (after.peak_used_blocks >= before->peak_used_blocks)
      peak_delta_blocks = after.peak_used_blocks - before->peak_used_blocks;

   fprintf(stderr,
           "memory_stats: stage=after_stream_chunk chunk=%zu used_bytes=%zu used_blocks=%zu peak_used_bytes=%zu peak_used_blocks=%zu chunk_peak_delta_bytes=%zu chunk_peak_delta_blocks=%zu free_bytes=%zu max_free_block_bytes=%zu\n",
           chunk_id,
           after.used_bytes,
           after.used_blocks,
           after.peak_used_bytes,
           after.peak_used_blocks,
           peak_delta_bytes,
           peak_delta_blocks,
           after.free_bytes,
           after.max_free_block_bytes);
   stream_protocol_print("STREAM_MEMORY chunk_id=%zu used_bytes=%zu used_blocks=%zu peak_used_bytes=%zu peak_used_blocks=%zu chunk_peak_delta_bytes=%zu chunk_peak_delta_blocks=%zu",
                         chunk_id,
                         after.used_bytes,
                         after.used_blocks,
                         after.peak_used_bytes,
                         after.peak_used_blocks,
                         peak_delta_bytes,
                         peak_delta_blocks);
}

static void print_pstream_storage_stats(size_t chunk_id, const HTS_PStreamSet *pss)
{
   size_t i;

   if (pss == NULL || pss->pstream == NULL)
      return;

   for (i = 0; i < pss->nstream; i++) {
      const HTS_PStream *pst = &pss->pstream[i];
      size_t par_pointer_bytes = pst->length * sizeof(float *);
      size_t par_data_bytes = pst->length * pst->vector_length * sizeof(float);
      size_t msd_flag_bytes = pst->msd_flag != NULL ? pss->total_frame * sizeof(HTS_Boolean) : 0;
      size_t gv_switch_bytes = pst->gv_switch != NULL ? pst->length * sizeof(HTS_Boolean) : 0;
      size_t gv_bytes = 0;
      size_t window_bytes = 0;
      size_t total_estimated_bytes;

      if (pst->gv_mean != NULL)
         gv_bytes += pst->vector_length * sizeof(float);
      if (pst->gv_vari != NULL)
         gv_bytes += pst->vector_length * sizeof(float);
      if (pst->win_l_width != NULL)
         window_bytes += pst->win_size * sizeof(int);
      if (pst->win_r_width != NULL)
         window_bytes += pst->win_size * sizeof(int);
      if (pst->win_coefficient != NULL)
         window_bytes += pst->win_size * sizeof(float *);

      total_estimated_bytes = par_pointer_bytes + par_data_bytes + msd_flag_bytes + gv_switch_bytes + gv_bytes + window_bytes;
      fprintf(stderr,
              "pstream_storage: chunk=%zu stream=%zu length=%zu vector_length=%zu par_data_bytes=%zu par_pointer_bytes=%zu msd_flag_bytes=%zu gv_switch_bytes=%zu gv_bytes=%zu window_bytes=%zu estimated_bytes=%zu\n",
              chunk_id,
              i,
              pst->length,
              pst->vector_length,
              par_data_bytes,
              par_pointer_bytes,
              msd_flag_bytes,
              gv_switch_bytes,
              gv_bytes,
              window_bytes,
              total_estimated_bytes);
      stream_protocol_print("STREAM_PSTREAM_MEMORY chunk_id=%zu stream=%zu length=%zu vector_length=%zu par_data_bytes=%zu par_pointer_bytes=%zu msd_flag_bytes=%zu gv_switch_bytes=%zu estimated_bytes=%zu",
                            chunk_id,
                            i,
                            pst->length,
                            pst->vector_length,
                            par_data_bytes,
                            par_pointer_bytes,
                            msd_flag_bytes,
                            gv_switch_bytes,
                            total_estimated_bytes);
   }
}

static void free_interpolation_weights(float *duration_iw, float **parameter_iw, float **gv_iw, size_t num_voices)
{
   size_t i;

   if (parameter_iw != NULL) {
      for (i = 0; i < num_voices; i++)
         free(parameter_iw[i]);
      free(parameter_iw);
   }
   if (gv_iw != NULL) {
      for (i = 0; i < num_voices; i++)
         free(gv_iw[i]);
      free(gv_iw);
   }
   free(duration_iw);
}

static int allocate_interpolation_weights(size_t num_voices, size_t num_streams, float **duration_iw, float ***parameter_iw, float ***gv_iw)
{
   size_t i, j;
   float weight;

   *duration_iw = NULL;
   *parameter_iw = NULL;
   *gv_iw = NULL;

   if (num_voices == 0 || num_streams == 0)
      return 0;

   *duration_iw = (float *) calloc(num_voices, sizeof(float));
   *parameter_iw = (float **) calloc(num_voices, sizeof(float *));
   *gv_iw = (float **) calloc(num_voices, sizeof(float *));
   if (*duration_iw == NULL || *parameter_iw == NULL || *gv_iw == NULL)
      goto error;

   weight = 1.0f / (float) num_voices;
   for (i = 0; i < num_voices; i++) {
      (*duration_iw)[i] = weight;
      (*parameter_iw)[i] = (float *) calloc(num_streams, sizeof(float));
      (*gv_iw)[i] = (float *) calloc(num_streams, sizeof(float));
      if ((*parameter_iw)[i] == NULL || (*gv_iw)[i] == NULL)
         goto error;
      for (j = 0; j < num_streams; j++) {
         (*parameter_iw)[i][j] = weight;
         (*gv_iw)[i][j] = weight;
      }
   }

   return 1;

error:
   free_interpolation_weights(*duration_iw, *parameter_iw, *gv_iw, num_voices);
   *duration_iw = NULL;
   *parameter_iw = NULL;
   *gv_iw = NULL;
   return 0;
}

static int allocate_pstream_options(size_t num_streams, float **msd_threshold, float **gv_weight)
{
   size_t i;

   *msd_threshold = NULL;
   *gv_weight = NULL;

   if (num_streams == 0)
      return 0;

   *msd_threshold = (float *) calloc(num_streams, sizeof(float));
   *gv_weight = (float *) calloc(num_streams, sizeof(float));
   if (*msd_threshold == NULL || *gv_weight == NULL) {
      free(*msd_threshold);
      free(*gv_weight);
      *msd_threshold = NULL;
      *gv_weight = NULL;
      return 0;
   }

   for (i = 0; i < num_streams; i++) {
      (*msd_threshold)[i] = 0.5f;
      (*gv_weight)[i] = 1.0f;
   }

   return 1;
}

static void apply_sstream_options(const HtsEngineRawCliOptions *options, HTS_SStreamSet *sss)
{
   size_t state_index;

   if (options->half_tone == 0.0f || HTS_SStreamSet_get_nstream(sss) < 2)
      return;
   for (state_index = 0; state_index < HTS_SStreamSet_get_total_state(sss); state_index++) {
      float lf0 = HTS_SStreamSet_get_mean(sss, 1, state_index, 0) + options->half_tone * HALF_TONE;

      if (lf0 < MIN_LF0)
         lf0 = MIN_LF0;
      else if (lf0 > MAX_LF0)
         lf0 = MAX_LF0;
      HTS_SStreamSet_set_mean(sss, 1, state_index, 0, lf0);
   }
}

static void apply_pstream_options(const HtsEngineRawCliOptions *options, size_t num_streams,
                                  float *msd_threshold, float *gv_weight)
{
   if (num_streams > 0)
      gv_weight[0] = options->gv_spectrum_weight;
   if (num_streams > 1) {
      msd_threshold[1] = options->msd_threshold;
      gv_weight[1] = options->gv_lf0_weight;
   }
}

static short clip_float_to_pcm16(float x)
{
   if (x > 32767.0f)
      return 32767;
   if (x < -32768.0f)
      return -32768;
   return (short) x;
}

static int write_u16_le(FILE *fp, unsigned short value)
{
   unsigned char bytes[2];

   bytes[0] = (unsigned char) (value & 0xffu);
   bytes[1] = (unsigned char) ((value >> 8) & 0xffu);
   return fwrite(bytes, 1, sizeof(bytes), fp) == sizeof(bytes);
}

static int write_exact(FILE *fp, const void *data, size_t size)
{
   return size == 0 || fwrite(data, 1, size, fp) == size;
}

static int write_u32_le(FILE *fp, unsigned int value)
{
   unsigned char bytes[4];

   bytes[0] = (unsigned char) (value & 0xffu);
   bytes[1] = (unsigned char) ((value >> 8) & 0xffu);
   bytes[2] = (unsigned char) ((value >> 16) & 0xffu);
   bytes[3] = (unsigned char) ((value >> 24) & 0xffu);
   return fwrite(bytes, 1, sizeof(bytes), fp) == sizeof(bytes);
}

static int get_wav_decimation_ratio(size_t source_rate, size_t output_rate, size_t *ratio)
{
   if (output_rate == 0)
      output_rate = source_rate;
   if (source_rate == 0 || output_rate == 0 || output_rate > source_rate || source_rate % output_rate != 0) {
      fprintf(stderr, "error: --wav-sample-rate must evenly divide source sample rate: source=%zu output=%zu\n",
              source_rate, output_rate);
      return 0;
   }
   *ratio = source_rate / output_rate;
   return 1;
}

static int write_wav_samples_pcm16(FILE *fp, const char *path, const float *samples, size_t sample_count, size_t ratio)
{
   size_t i;

   if (ratio == 0 || sample_count % ratio != 0) {
      fprintf(stderr, "error: WAV sample count is not divisible by downsample ratio: samples=%zu ratio=%zu\n",
              sample_count, ratio);
      return 0;
   }

   for (i = 0; i < sample_count; i += ratio) {
      size_t j;
      float sum = 0.0f;
      short sample;

      for (j = 0; j < ratio; j++)
         sum += samples[i + j];
      sample = clip_float_to_pcm16(sum / (float) ratio);
      if (!write_u16_le(fp, (unsigned short) sample)) {
         fprintf(stderr, "error: failed to write WAV samples '%s'\n", path);
         return 0;
      }
   }
   return 1;
}

static int write_wav_pcm16(const char *path, const float *samples, size_t sample_count, size_t source_rate, size_t output_rate)
{
   FILE *fp;
   size_t ratio;
   size_t output_sample_count;
   unsigned int data_bytes;

   if (output_rate == 0)
      output_rate = source_rate;
   if (!get_wav_decimation_ratio(source_rate, output_rate, &ratio))
      return 0;
   if (sample_count % ratio != 0) {
      fprintf(stderr, "error: WAV sample count is not divisible by downsample ratio: samples=%zu ratio=%zu\n",
              sample_count, ratio);
      return 0;
   }
   output_sample_count = sample_count / ratio;
   if (output_sample_count > (0xffffffffu - 36u) / sizeof(short) || output_rate > 0xffffffffu / sizeof(short)) {
      fprintf(stderr, "error: WAV output is too large\n");
      return 0;
   }

   data_bytes = (unsigned int) (output_sample_count * sizeof(short));
   fp = fopen(path, "wb");
   if (fp == NULL) {
      fprintf(stderr, "error: failed to open WAV output '%s': errno=%d\n", path, errno);
      return 0;
   }

   if (!write_exact(fp, "RIFF", 4) ||
       !write_u32_le(fp, data_bytes + 36u) ||
       !write_exact(fp, "WAVE", 4) ||
       !write_exact(fp, "fmt ", 4) ||
       !write_u32_le(fp, 16u) ||
       !write_u16_le(fp, 1u) ||
       !write_u16_le(fp, 1u) ||
       !write_u32_le(fp, (unsigned int) output_rate) ||
       !write_u32_le(fp, (unsigned int) output_rate * sizeof(short)) ||
       !write_u16_le(fp, sizeof(short)) ||
       !write_u16_le(fp, sizeof(short) * 8u) ||
       !write_exact(fp, "data", 4) ||
       !write_u32_le(fp, data_bytes)) {
      fclose(fp);
      fprintf(stderr, "error: failed to write WAV header '%s'\n", path);
      return 0;
   }

   if (!write_wav_samples_pcm16(fp, path, samples, sample_count, ratio)) {
      fclose(fp);
      return 0;
   }

   if (fclose(fp) != 0) {
      fprintf(stderr, "error: failed to close WAV output '%s': errno=%d\n", path, errno);
      return 0;
   }

   return 1;
}

typedef struct WavPcm16Writer {
   FILE *fp;
   const char *path;
   size_t source_sampling_rate;
   size_t sampling_rate;
   size_t decimation_ratio;
   size_t sample_count;
} WavPcm16Writer;

static int wav_writer_open(WavPcm16Writer *writer, const char *path, size_t source_sampling_rate, size_t output_sampling_rate)
{
   memset(writer, 0, sizeof(*writer));
   if (output_sampling_rate == 0)
      output_sampling_rate = source_sampling_rate;
   if (!get_wav_decimation_ratio(source_sampling_rate, output_sampling_rate, &writer->decimation_ratio))
      return 0;
   writer->path = path;
   writer->source_sampling_rate = source_sampling_rate;
   writer->sampling_rate = output_sampling_rate;
   if (path == NULL)
      return 1;
   writer->fp = fopen(path, "wb");
   if (writer->fp == NULL) {
      fprintf(stderr, "error: failed to open WAV output '%s': errno=%d\n", path, errno);
      return 0;
   }

   if (!write_exact(writer->fp, "RIFF", 4) ||
       !write_u32_le(writer->fp, 36u) ||
       !write_exact(writer->fp, "WAVE", 4) ||
       !write_exact(writer->fp, "fmt ", 4) ||
       !write_u32_le(writer->fp, 16u) ||
       !write_u16_le(writer->fp, 1u) ||
       !write_u16_le(writer->fp, 1u) ||
       !write_u32_le(writer->fp, (unsigned int) output_sampling_rate) ||
       !write_u32_le(writer->fp, (unsigned int) output_sampling_rate * sizeof(short)) ||
       !write_u16_le(writer->fp, sizeof(short)) ||
       !write_u16_le(writer->fp, sizeof(short) * 8u) ||
       !write_exact(writer->fp, "data", 4) ||
       !write_u32_le(writer->fp, 0u)) {
      fclose(writer->fp);
      writer->fp = NULL;
      fprintf(stderr, "error: failed to write WAV header '%s'\n", path);
      return 0;
   }

   return 1;
}

static int wav_writer_append(WavPcm16Writer *writer, const float *samples, size_t sample_count)
{
   size_t output_sample_count;

   if (writer->decimation_ratio == 0 || sample_count % writer->decimation_ratio != 0) {
      fprintf(stderr, "error: streamed WAV sample count is not divisible by downsample ratio: samples=%zu ratio=%zu\n",
              sample_count, writer->decimation_ratio);
      return 0;
   }
   output_sample_count = sample_count / writer->decimation_ratio;
   if (writer->sample_count > ((size_t) 0xffffffffu / sizeof(short)) - output_sample_count) {
      fprintf(stderr, "error: streamed WAV output is too large\n");
      return 0;
   }
   if (writer->fp != NULL &&
       !write_wav_samples_pcm16(writer->fp, writer->path, samples, sample_count, writer->decimation_ratio))
      return 0;
   writer->sample_count += output_sample_count;
   return 1;
}

static int wav_writer_close(WavPcm16Writer *writer)
{
   unsigned int data_bytes;

   if (writer->fp == NULL)
      return 1;

   data_bytes = (unsigned int) (writer->sample_count * sizeof(short));
   if (fseek(writer->fp, 4, SEEK_SET) != 0 ||
       !write_u32_le(writer->fp, data_bytes + 36u) ||
       fseek(writer->fp, 40, SEEK_SET) != 0 ||
       !write_u32_le(writer->fp, data_bytes)) {
      fclose(writer->fp);
      writer->fp = NULL;
      fprintf(stderr, "error: failed to update WAV header '%s'\n", writer->path);
      return 0;
   }

   if (fclose(writer->fp) != 0) {
      writer->fp = NULL;
      fprintf(stderr, "error: failed to close WAV output '%s': errno=%d\n", writer->path, errno);
      return 0;
   }
   writer->fp = NULL;
   return 1;
}

static void free_label_lines(char **labels, size_t label_count)
{
   size_t i;

   if (labels == NULL)
      return;
   for (i = 0; i < label_count; i++)
      free(labels[i]);
   free(labels);
}

static int append_label_line(char ***labels, size_t *label_count, size_t *capacity, const char *line, size_t length)
{
   char *copy;

   if (*label_count == *capacity) {
      size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
      char **new_labels = (char **) realloc(*labels, new_capacity * sizeof(char *));
      if (new_labels == NULL)
         return 0;
      *labels = new_labels;
      *capacity = new_capacity;
   }

   copy = (char *) malloc(length + 1);
   if (copy == NULL)
      return 0;
   memcpy(copy, line, length);
   copy[length] = '\0';
   (*labels)[(*label_count)++] = copy;
   return 1;
}

static int parse_stream_metadata_size_t(const char *line, const char *key, size_t *value)
{
   const char *p = strstr(line, key);
   char *end;
   unsigned long parsed;

   if (p == NULL)
      return 0;
   p += strlen(key);
   if (*p != '=')
      return 0;
   p++;
   if (*p < '0' || *p > '9')
      return 0;
   parsed = strtoul(p, &end, 10);
   if (end == p)
      return 0;
   *value = (size_t) parsed;
   return 1;
}

static int parse_stream_metadata_int(const char *line, const char *key, int *value)
{
   size_t parsed;

   if (!parse_stream_metadata_size_t(line, key, &parsed))
      return 0;
   if (parsed > 2147483647u)
      return 0;
   *value = (int) parsed;
   return 1;
}

static void stream_protocol_print(const char *format, ...)
{
   va_list args;

   va_start(args, format);
   vfprintf(stdout, format, args);
   va_end(args);
   fputc('\n', stdout);
   fflush(stdout);
}

static void stream_protocol_error(const char *code)
{
   stream_protocol_print("STREAM_ERROR code=%s", code);
}

static int load_label_lines_from_stream(FILE *fp, const char *source_name, char ***out_labels, size_t *out_label_count)
{
   char line[8192];
   char **labels = NULL;
   size_t label_count = 0;
   size_t capacity = 0;

   *out_labels = NULL;
   *out_label_count = 0;

   while (fgets(line, sizeof(line), fp) != NULL) {
      char *start = line;
      char *end;
      char *copy;
      size_t length;

      while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
         start++;
      if (*start == '\0')
         continue;

      end = start + strlen(start);
      while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
         end--;
      length = (size_t) (end - start);

      if (label_count == capacity) {
         size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
         char **new_labels = (char **) realloc(labels, new_capacity * sizeof(char *));
         if (new_labels == NULL) {
            free_label_lines(labels, label_count);
            return 0;
         }
         labels = new_labels;
         capacity = new_capacity;
      }

      copy = (char *) malloc(length + 1);
      if (copy == NULL) {
         free_label_lines(labels, label_count);
         return 0;
      }
      memcpy(copy, start, length);
      copy[length] = '\0';
      labels[label_count++] = copy;
   }

   if (ferror(fp)) {
      fprintf(stderr, "error: failed to read labels from '%s'\n", source_name);
      free_label_lines(labels, label_count);
      return 0;
   }

   *out_labels = labels;
   *out_label_count = label_count;
   return 1;
}

static int load_label_lines(const char *path, char ***out_labels, size_t *out_label_count)
{
   FILE *fp;
   int ok;

   fp = fopen(path, "rb");
   if (fp == NULL) {
      fprintf(stderr, "error: failed to open labels file '%s': errno=%d\n", path, errno);
      return 0;
   }

   ok = load_label_lines_from_stream(fp, path, out_labels, out_label_count);
   fclose(fp);
   return ok;
}

static int load_label_lines_from_stdin(char ***out_labels, size_t *out_label_count)
{
   return load_label_lines_from_stream(stdin, "stdin", out_labels, out_label_count);
}

static int check_streams(const HtsEngineRawCliOptions *options, char **labels, size_t label_count, const char *label_source, const char *status_name, int use_data_view, int create_pstream, int create_gstream)
{
   HTS_ModelSet ms;
   HTS_Label label;
   HTS_SStreamSet sss;
   HTS_PStreamSet pss;
   HTS_GStreamSet gss;
   HTS_Boolean stop = FALSE;
   unsigned char *raw_data = NULL;
   size_t raw_data_size = 0;
   float *duration_iw = NULL;
   float **parameter_iw = NULL;
   float **gv_iw = NULL;
   float *msd_threshold = NULL;
   float *gv_weight = NULL;
   float *speech = NULL;
   size_t num_voices = 0;
   size_t num_streams = 0;
   size_t sampling_rate = 0;
   size_t fperiod = 0;
   size_t pstream_total_frame = 0;
   size_t pstream0_vector_length = 0;
   size_t gstream_sample_count = 0;
   float pstream0_parameter = 0.0f;
   float alpha = 0.55f;
   int ok = 0;

   wmem_init();
   if (options->show_memory_stats)
      print_memory_stats("after_wmem_init");

   HTS_ModelSet_initialize(&ms);
   HTS_Label_initialize(&label);
   HTS_SStreamSet_initialize(&sss);
   HTS_PStreamSet_initialize(&pss);
   HTS_GStreamSet_initialize(&gss);

   if (use_data_view) {
      if (!load_file_data(options->raw_path, &raw_data, &raw_data_size)) {
         fprintf(stderr, "error: failed to load raw voice data '%s'\n", options->raw_path);
         goto cleanup;
      }
      if (HTS_ModelSet_load_raw_from_data(&ms, raw_data, raw_data_size) != TRUE) {
         fprintf(stderr, "error: failed to view raw voice '%s'\n", options->raw_path);
         goto cleanup;
      }
   } else {
      if (HTS_ModelSet_load_raw(&ms, options->raw_path) != TRUE) {
         fprintf(stderr, "error: failed to load raw voice '%s'\n", options->raw_path);
         goto cleanup;
      }
   }
   if (options->show_memory_stats)
      print_memory_stats("after_raw_load");
   resolve_synthesis_profile(options, HTS_ModelSet_get_sampling_frequency(&ms), HTS_ModelSet_get_fperiod(&ms),
                             &sampling_rate, &fperiod, &alpha);

   HTS_Label_load_from_strings(&label, sampling_rate, fperiod, labels, label_count);
   if (HTS_Label_get_size(&label) == 0) {
      fprintf(stderr, "error: no full-context label lines were loaded: %s\n", label_source);
      goto cleanup;
   }

   num_voices = HTS_ModelSet_get_nvoices(&ms);
   num_streams = HTS_ModelSet_get_nstream(&ms);
   if (!allocate_interpolation_weights(num_voices, num_streams, &duration_iw, &parameter_iw, &gv_iw)) {
      fprintf(stderr, "error: failed to allocate interpolation weights\n");
      goto cleanup;
   }

   if (HTS_SStreamSet_create(&sss, &ms, &label, FALSE, options->speed, duration_iw, parameter_iw, gv_iw) != TRUE) {
      fprintf(stderr, "error: failed to create state stream set from raw voice\n");
      goto cleanup;
   }
   apply_sstream_options(options, &sss);
   if (options->show_memory_stats)
      print_memory_stats("after_sstream_create");

   if (create_pstream) {
      if (!allocate_pstream_options(num_streams, &msd_threshold, &gv_weight)) {
         fprintf(stderr, "error: failed to allocate parameter stream options\n");
         goto cleanup;
      }
      apply_pstream_options(options, num_streams, msd_threshold, gv_weight);
      if (HTS_PStreamSet_create(&pss, &sss, msd_threshold, gv_weight) != TRUE) {
         fprintf(stderr, "error: failed to create parameter stream set from raw voice\n");
         goto cleanup;
      }
      if (options->show_memory_stats)
         print_memory_stats("after_pstream_create");
      pstream_total_frame = HTS_PStreamSet_get_total_frame(&pss);
      pstream0_vector_length = HTS_PStreamSet_get_vector_length(&pss, 0);
      pstream0_parameter = HTS_PStreamSet_get_parameter(&pss, 0, 0, 0);
   }

   if (create_gstream) {
      if (options->wav_path != NULL) {
         if (fperiod != 0 && HTS_PStreamSet_get_total_frame(&pss) > ((size_t) -1) / fperiod) {
            fprintf(stderr, "error: WAV sample count overflow\n");
            goto cleanup;
         }
         gstream_sample_count = HTS_PStreamSet_get_total_frame(&pss) * fperiod;
         speech = (float *) calloc(gstream_sample_count, sizeof(float));
         if (speech == NULL) {
            fprintf(stderr, "error: failed to allocate WAV sample buffer\n");
            goto cleanup;
         }
      }
      if (HTS_GStreamSet_create_with_speech(&gss, &pss, 0, FALSE, sampling_rate,
                                            fperiod, alpha, 0.0f, &stop, 1.0f, speech, NULL) != TRUE) {
         fprintf(stderr, "error: failed to create generated stream set from raw voice\n");
         goto cleanup;
      }
      if (options->show_memory_stats)
         print_memory_stats("after_gstream_create");
      if (options->wav_path != NULL && !write_wav_pcm16(options->wav_path, speech, gstream_sample_count, sampling_rate, options->wav_sample_rate))
         goto cleanup;
      HTS_PStreamSet_clear(&pss);
      if (options->show_memory_stats)
         print_memory_stats("after_pstream_clear");
   }

   fprintf(stdout,
           "%s: mode=%s raw=%s labels=%s label_count=%zu voices=%zu states=%zu streams=%zu total_state=%zu total_frame=%zu stream0_vector_length=%zu duration0=%zu mean0=%.6f vari0=%.6f",
           status_name,
           use_data_view ? "view" : "copy",
           options->raw_path,
           label_source,
           HTS_Label_get_size(&label),
           num_voices,
           HTS_ModelSet_get_nstate(&ms),
           num_streams,
           HTS_SStreamSet_get_total_state(&sss),
           HTS_SStreamSet_get_total_frame(&sss),
           HTS_SStreamSet_get_vector_length(&sss, 0),
           HTS_SStreamSet_get_duration(&sss, 0),
           HTS_SStreamSet_get_mean(&sss, 0, 0, 0),
           HTS_SStreamSet_get_vari(&sss, 0, 0, 0));
   if (create_pstream)
      fprintf(stdout,
              " pstream0_length=%zu pstream0_vector_length=%zu par0=%.6f",
              pstream_total_frame,
              pstream0_vector_length,
              pstream0_parameter);
   if (create_gstream)
      fprintf(stdout,
              " gstream_frames=%zu gstream_samples=%zu gstream0_vector_length=%zu synth_sample_rate=%zu synth_frame_period=%zu alpha=%.5f",
              HTS_GStreamSet_get_total_frame(&gss),
              HTS_GStreamSet_get_total_nsamples(&gss),
              HTS_GStreamSet_get_vector_length(&gss, 0),
              sampling_rate,
              fperiod,
              alpha);
   if (options->wav_path != NULL)
      fprintf(stdout, " wav=%s", options->wav_path);
   if (options->wav_path != NULL)
      fprintf(stdout, " wav_sample_rate=%zu", options->wav_sample_rate != 0 ? options->wav_sample_rate : sampling_rate);
   fprintf(stdout, " status=ok\n");
   ok = 1;

cleanup:
   HTS_GStreamSet_clear(&gss);
   HTS_PStreamSet_clear(&pss);
   HTS_SStreamSet_clear(&sss);
   HTS_Label_clear(&label);
   HTS_ModelSet_clear(&ms);
   free_interpolation_weights(duration_iw, parameter_iw, gv_iw, num_voices);
   free(msd_threshold);
   free(gv_weight);
   free(speech);
   free_file_data(raw_data);
   if (options->show_memory_stats)
      print_memory_stats("after_clear");
   return ok;
}

static void free_stream_parameters(float **parameters, size_t nstream)
{
   size_t i;

   if (parameters == NULL)
      return;
   for (i = 0; i < nstream; i++)
      free(parameters[i]);
   free(parameters);
}

static void free_label_block_ranges(LabelBlockRange *ranges)
{
   free(ranges);
}

static int append_label_block_range(LabelBlockRange **ranges, size_t *range_count, size_t *capacity, size_t start, size_t count, size_t frame_count)
{
   LabelBlockRange *new_ranges;

   if (count == 0)
      return 1;
   if (*range_count == *capacity) {
      size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;
      new_ranges = (LabelBlockRange *) realloc(*ranges, new_capacity * sizeof(LabelBlockRange));
      if (new_ranges == NULL)
         return 0;
      *ranges = new_ranges;
      *capacity = new_capacity;
   }

   (*ranges)[*range_count].start = start;
   (*ranges)[*range_count].count = count;
   (*ranges)[*range_count].frame_count = frame_count;
   (*range_count)++;
   return 1;
}

static int split_labels_by_sstream_frames(const unsigned char *raw_data, size_t raw_data_size, char **labels, size_t label_count, size_t block_frame_limit, LabelBlockRange **out_ranges, size_t *out_range_count)
{
   HTS_ModelSet ms;
   float *duration_iw = NULL;
   float **parameter_iw = NULL;
   float **gv_iw = NULL;
   LabelBlockRange *ranges = NULL;
   size_t range_count = 0;
   size_t range_capacity = 0;
   size_t num_voices = 0;
   size_t num_streams = 0;
   size_t sampling_rate = 0;
   size_t fperiod = 0;
   size_t current_start = 0;
   size_t current_count = 0;
   size_t current_frames = 0;
   size_t i;
   int ok = 0;

   *out_ranges = NULL;
   *out_range_count = 0;

   if (block_frame_limit == 0 || label_count == 0)
      return 0;

   HTS_ModelSet_initialize(&ms);

   if (HTS_ModelSet_load_raw_from_data(&ms, raw_data, raw_data_size) != TRUE)
      goto cleanup;
   num_voices = HTS_ModelSet_get_nvoices(&ms);
   num_streams = HTS_ModelSet_get_nstream(&ms);
   sampling_rate = HTS_ModelSet_get_sampling_frequency(&ms);
   fperiod = HTS_ModelSet_get_fperiod(&ms);

   if (!allocate_interpolation_weights(num_voices, num_streams, &duration_iw, &parameter_iw, &gv_iw))
      goto cleanup;

   for (i = 0; i < label_count; i++) {
      HTS_Label label;
      HTS_SStreamSet sss;
      size_t label_frames = 0;

      HTS_Label_initialize(&label);
      HTS_SStreamSet_initialize(&sss);
      HTS_Label_load_from_strings(&label, sampling_rate, fperiod, labels + i, 1);
      if (HTS_SStreamSet_create(&sss, &ms, &label, FALSE, 1.0f, duration_iw, parameter_iw, gv_iw) != TRUE) {
         HTS_SStreamSet_clear(&sss);
         HTS_Label_clear(&label);
         goto cleanup;
      }
      label_frames = HTS_SStreamSet_get_total_frame(&sss);
      HTS_SStreamSet_clear(&sss);
      HTS_Label_clear(&label);

      if (current_count > 0 && current_frames + label_frames > block_frame_limit) {
         if (!append_label_block_range(&ranges, &range_count, &range_capacity, current_start, current_count, current_frames))
            goto cleanup;
         current_start = i;
         current_count = 0;
         current_frames = 0;
      }
      current_count++;
      current_frames += label_frames;
   }
   if (!append_label_block_range(&ranges, &range_count, &range_capacity, current_start, current_count, current_frames))
      goto cleanup;

   *out_ranges = ranges;
   *out_range_count = range_count;
   ranges = NULL;
   ok = 1;

cleanup:
   free_label_block_ranges(ranges);
   HTS_ModelSet_clear(&ms);
   free_interpolation_weights(duration_iw, parameter_iw, gv_iw, num_voices);
   return ok;
}

static int synthesize_pstream_to_wav(HTS_PStreamSet *pss, WavPcm16Writer *writer, HTS_Audio *audio,
                                     size_t sampling_rate, size_t fperiod, float alpha, float beta, float volume_db)
{
   HTS_Vocoder vocoder;
   HTS_Boolean stop = FALSE;
   float **parameters = NULL;
   float *frame_speech = NULL;
   size_t nstream = HTS_PStreamSet_get_nstream(pss);
   size_t total_frame = HTS_PStreamSet_get_total_frame(pss);
   size_t msd_frame = 0;
   size_t nlpf = 0;
   size_t i, k, frame;
   int ok = 0;

   if (nstream != 2 && nstream != 3) {
      fprintf(stderr, "error: stream chunk requires 2 or 3 parameter streams\n");
      return 0;
   }
   if (HTS_PStreamSet_get_vector_length(pss, 1) != 1) {
      fprintf(stderr, "error: stream chunk lf0 vector length must be 1\n");
      return 0;
   }
   if (nstream >= 3 && HTS_PStreamSet_get_vector_length(pss, 2) % 2 == 0) {
      fprintf(stderr, "error: stream chunk low-pass filter coefficient count must be odd\n");
      return 0;
   }

   parameters = (float **) calloc(nstream, sizeof(float *));
   if (parameters == NULL)
      goto cleanup;
   for (i = 0; i < nstream; i++) {
      parameters[i] = (float *) calloc(HTS_PStreamSet_get_vector_length(pss, i), sizeof(float));
      if (parameters[i] == NULL)
         goto cleanup;
   }
   frame_speech = (float *) calloc(fperiod, sizeof(float));
   if (frame_speech == NULL)
      goto cleanup;

   HTS_Vocoder_initialize(&vocoder, HTS_PStreamSet_get_vector_length(pss, 0) - 1, 0, FALSE, sampling_rate, fperiod);
   if (nstream >= 3)
      nlpf = HTS_PStreamSet_get_vector_length(pss, 2);

   for (frame = 0; frame < total_frame; frame++) {
      float *lpf = NULL;

      memset(frame_speech, 0, fperiod * sizeof(float));
      for (i = 0; i < nstream; i++) {
         if (HTS_PStreamSet_is_msd(pss, i)) {
            if (HTS_PStreamSet_get_msd_flag(pss, i, frame) == TRUE) {
               for (k = 0; k < HTS_PStreamSet_get_vector_length(pss, i); k++)
                  parameters[i][k] = HTS_PStreamSet_get_parameter(pss, i, msd_frame, k);
               msd_frame++;
            } else {
               for (k = 0; k < HTS_PStreamSet_get_vector_length(pss, i); k++)
                  parameters[i][k] = HTS_NODATA;
            }
         } else {
            for (k = 0; k < HTS_PStreamSet_get_vector_length(pss, i); k++)
               parameters[i][k] = HTS_PStreamSet_get_parameter(pss, i, frame, k);
         }
      }

      if (stop == FALSE) {
         if (nstream >= 3)
            lpf = parameters[2];
         HTS_Vocoder_synthesize(&vocoder, HTS_PStreamSet_get_vector_length(pss, 0) - 1,
                                parameters[1][0], parameters[0], nlpf, lpf, alpha, beta,
                                expf(volume_db * DB), frame_speech, audio);
      }
      if (!wav_writer_append(writer, frame_speech, fperiod))
         goto cleanup_vocoder;
   }

   ok = 1;

cleanup_vocoder:
   HTS_Vocoder_clear(&vocoder);

cleanup:
   free_stream_parameters(parameters, nstream);
   free(frame_speech);
   return ok;
}

static int synthesize_chunk_to_wav(const HtsEngineRawCliOptions *options, const unsigned char *raw_data, size_t raw_data_size,
                                   char **labels, size_t label_count, size_t chunk_id, size_t block_frame_limit,
                                   WavPcm16Writer *writer, HTS_Audio *audio, size_t *out_frame_count, size_t *out_sample_count)
{
   HTS_ModelSet ms;
   HTS_Label label;
   HTS_SStreamSet sss;
   HTS_PStreamSet pss;
   float *duration_iw = NULL;
   float **parameter_iw = NULL;
   float **gv_iw = NULL;
   float *msd_threshold = NULL;
   float *gv_weight = NULL;
   size_t num_voices = 0;
   size_t num_streams = 0;
   size_t sampling_rate;
   size_t fperiod;
   size_t sample_count;
   float alpha;
   int ok = 0;

   *out_frame_count = 0;
   *out_sample_count = 0;

   if (block_frame_limit > 0 && label_count > 1) {
      LabelBlockRange *ranges = NULL;
      size_t range_count = 0;
      size_t i;
      int ok = 0;

      if (!split_labels_by_sstream_frames(raw_data, raw_data_size, labels, label_count, block_frame_limit, &ranges, &range_count)) {
         fprintf(stderr, "error: failed to split stream chunk into PStream blocks\n");
         goto block_cleanup;
      }
      if (range_count <= 1) {
         ok = synthesize_chunk_to_wav(options, raw_data, raw_data_size, labels, label_count, chunk_id, 0,
                                      writer, audio, out_frame_count, out_sample_count);
         goto block_cleanup;
      }

      stream_protocol_print("STREAM_PSTREAM_BLOCKS chunk_id=%zu block_frames=%zu blocks=%zu", chunk_id, block_frame_limit, range_count);
      for (i = 0; i < range_count; i++) {
         size_t block_frames = 0;
         size_t block_samples = 0;

         stream_protocol_print("STREAM_PSTREAM_BLOCK chunk_id=%zu block=%zu labels=%zu estimated_frames=%zu",
                               chunk_id, i + 1, ranges[i].count, ranges[i].frame_count);
         if (!synthesize_chunk_to_wav(options, raw_data, raw_data_size, labels + ranges[i].start, ranges[i].count,
                                      chunk_id, 0, writer, audio, &block_frames, &block_samples))
            goto block_cleanup;
         *out_frame_count += block_frames;
         *out_sample_count += block_samples;
      }
      ok = 1;

block_cleanup:
      free_label_block_ranges(ranges);
      return ok;
   }

   HTS_ModelSet_initialize(&ms);
   HTS_Label_initialize(&label);
   HTS_SStreamSet_initialize(&sss);
   HTS_PStreamSet_initialize(&pss);

   if (HTS_ModelSet_load_raw_from_data(&ms, raw_data, raw_data_size) != TRUE) {
      fprintf(stderr, "error: failed to load raw voice view for stream chunk\n");
      goto cleanup;
   }
   num_voices = HTS_ModelSet_get_nvoices(&ms);
   num_streams = HTS_ModelSet_get_nstream(&ms);
   resolve_synthesis_profile(options, HTS_ModelSet_get_sampling_frequency(&ms), HTS_ModelSet_get_fperiod(&ms),
                             &sampling_rate, &fperiod, &alpha);
   HTS_Label_load_from_strings(&label, sampling_rate, fperiod, labels, label_count);

   if (writer->source_sampling_rate != sampling_rate) {
      fprintf(stderr, "error: stream chunk sampling rate changed: %zu != %zu\n", sampling_rate, writer->source_sampling_rate);
      goto cleanup;
   }
   if (!allocate_interpolation_weights(num_voices, num_streams, &duration_iw, &parameter_iw, &gv_iw) ||
       !allocate_pstream_options(num_streams, &msd_threshold, &gv_weight))
      goto cleanup;
   apply_pstream_options(options, num_streams, msd_threshold, gv_weight);

   if (HTS_SStreamSet_create(&sss, &ms, &label, FALSE, options->speed, duration_iw, parameter_iw, gv_iw) != TRUE) {
      fprintf(stderr, "error: failed to create state stream set for stream chunk\n");
      goto cleanup;
   }
   apply_sstream_options(options, &sss);
   HTS_Label_clear(&label);
   HTS_ModelSet_clear(&ms);
   if (HTS_PStreamSet_create_consuming_sstream(&pss, &sss, msd_threshold, gv_weight) != TRUE) {
      fprintf(stderr, "error: failed to create parameter stream set for stream chunk\n");
      goto cleanup;
   }
   HTS_SStreamSet_clear(&sss);
   if (options->show_memory_stats)
      print_pstream_storage_stats(chunk_id, &pss);
   if (fperiod != 0 && HTS_PStreamSet_get_total_frame(&pss) > ((size_t) -1) / fperiod) {
      fprintf(stderr, "error: stream chunk WAV sample count overflow\n");
      goto cleanup;
   }

   sample_count = HTS_PStreamSet_get_total_frame(&pss) * fperiod;
   if (!synthesize_pstream_to_wav(&pss, writer, audio, sampling_rate, fperiod, alpha,
                                  options->beta, options->volume_db)) {
      fprintf(stderr, "error: failed to synthesize stream chunk to WAV sink\n");
      goto cleanup;
   }

   *out_frame_count = HTS_PStreamSet_get_total_frame(&pss);
   *out_sample_count = sample_count;
   ok = 1;

cleanup:
   HTS_PStreamSet_clear(&pss);
   HTS_SStreamSet_clear(&sss);
   HTS_Label_clear(&label);
   HTS_ModelSet_clear(&ms);
   free_interpolation_weights(duration_iw, parameter_iw, gv_iw, num_voices);
   free(msd_threshold);
   free(gv_weight);
   return ok;
}

static int synthesize_stream_from_stdin(const HtsEngineRawCliOptions *options)
{
   unsigned char *raw_data = NULL;
   size_t raw_data_size = 0;
   size_t sampling_rate;
   size_t fperiod;
   float alpha;
   HTS_ModelSet ms;
   HTS_Audio audio;
   WavPcm16Writer writer;
   char line[8192];
   char **chunk_labels = NULL;
   size_t chunk_label_count = 0;
   size_t chunk_capacity = 0;
   size_t chunk_count = 0;
   size_t expected_chunk_id = 1;
   size_t current_chunk_id = 0;
   size_t expected_label_count = 0;
   size_t total_frames = 0;
   size_t total_samples = 0;
   int in_chunk = 0;
   int current_final = 0;
   int final_seen = 0;
   int eos_seen = 0;
   int audio_initialized = 0;
   int ok = 0;

   memset(&writer, 0, sizeof(writer));
   HTS_ModelSet_initialize(&ms);

   if (!load_file_data(options->raw_path, &raw_data, &raw_data_size)) {
      stream_protocol_error("RAW_LOAD_FAILED");
      goto cleanup;
   }
   if (HTS_ModelSet_load_raw_from_data(&ms, raw_data, raw_data_size) != TRUE) {
      stream_protocol_error("RAW_VIEW_LOAD_FAILED");
      fprintf(stderr, "error: failed to load raw voice view for stream\n");
      goto cleanup;
   }
   resolve_synthesis_profile(options, HTS_ModelSet_get_sampling_frequency(&ms), HTS_ModelSet_get_fperiod(&ms),
                             &sampling_rate, &fperiod, &alpha);
   HTS_ModelSet_clear(&ms);

   if (options->audio_buffer_size > 0) {
      HTS_Audio_initialize(&audio);
      HTS_Audio_set_parameter(&audio, sampling_rate, options->audio_buffer_size);
      audio_initialized = 1;
   }

   if (!wav_writer_open(&writer, options->wav_path, sampling_rate, options->wav_sample_rate)) {
      stream_protocol_error("WAV_OPEN_FAILED");
      goto cleanup;
   }
   stream_protocol_print("STREAM_READY protocol=chunked-v1 sample_rate=%zu frame_period=%zu alpha=%.5f wav_sample_rate=%zu raw_bytes=%zu",
                         sampling_rate, fperiod, alpha, writer.sampling_rate, raw_data_size);

   while (fgets(line, sizeof(line), stdin) != NULL) {
      char *start = line;
      char *end;
      size_t length;

      while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
         start++;
      end = start + strlen(start);
      while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
         end--;
      length = (size_t) (end - start);

      if (length == 0)
         continue;
      if (strncmp(start, "EOS", 3) == 0) {
         if (in_chunk) {
            stream_protocol_error("EOS_INSIDE_CHUNK");
            fprintf(stderr, "error: EOS inside stream label chunk\n");
            goto cleanup;
         }
         eos_seen = 1;
         break;
      }
      if (strncmp(start, "BEGIN", 5) == 0) {
         if (in_chunk) {
            stream_protocol_error("NESTED_BEGIN");
            fprintf(stderr, "error: nested BEGIN in stream labels\n");
            goto cleanup;
         }
         if (final_seen) {
            stream_protocol_error("BEGIN_AFTER_FINAL");
            fprintf(stderr, "error: BEGIN after final stream chunk\n");
            goto cleanup;
         }
         if (!parse_stream_metadata_size_t(start, "chunk_id", &current_chunk_id) ||
             !parse_stream_metadata_size_t(start, "labels", &expected_label_count) ||
             !parse_stream_metadata_int(start, "final", &current_final)) {
            stream_protocol_error("BEGIN_METADATA_REQUIRED");
            fprintf(stderr, "error: BEGIN requires chunk_id, labels, and final metadata\n");
            goto cleanup;
         }
         if (current_chunk_id != expected_chunk_id) {
            stream_protocol_print("STREAM_ERROR code=UNEXPECTED_CHUNK_ID got=%zu expected=%zu", current_chunk_id, expected_chunk_id);
            fprintf(stderr, "error: unexpected chunk_id: got=%zu expected=%zu\n", current_chunk_id, expected_chunk_id);
            goto cleanup;
         }
         if (expected_label_count == 0) {
            stream_protocol_error("EMPTY_LABEL_COUNT_METADATA");
            fprintf(stderr, "error: stream chunk labels metadata must be greater than zero\n");
            goto cleanup;
         }
         if (current_final != 0 && current_final != 1) {
            stream_protocol_print("STREAM_ERROR code=INVALID_FINAL_METADATA value=%d", current_final);
            fprintf(stderr, "error: stream chunk final metadata must be 0 or 1\n");
            goto cleanup;
         }
         free_label_lines(chunk_labels, chunk_label_count);
         chunk_labels = NULL;
         chunk_label_count = 0;
         chunk_capacity = 0;
         in_chunk = 1;
         continue;
      }
      if (strncmp(start, "END", 3) == 0) {
         size_t chunk_frames;
         size_t chunk_samples;
         size_t end_chunk_id;
         HTS_WallocStats chunk_memory_before;
         int have_chunk_memory_before = 0;

         if (!in_chunk) {
            stream_protocol_error("END_WITHOUT_BEGIN");
            fprintf(stderr, "error: END without BEGIN in stream labels\n");
            goto cleanup;
         }
         if (!parse_stream_metadata_size_t(start, "chunk_id", &end_chunk_id)) {
            stream_protocol_error("END_METADATA_REQUIRED");
            fprintf(stderr, "error: END requires chunk_id metadata\n");
            goto cleanup;
         }
         if (end_chunk_id != current_chunk_id) {
            stream_protocol_print("STREAM_ERROR code=END_CHUNK_ID_MISMATCH got=%zu expected=%zu", end_chunk_id, current_chunk_id);
            fprintf(stderr, "error: END chunk_id mismatch: got=%zu expected=%zu\n", end_chunk_id, current_chunk_id);
            goto cleanup;
         }
         if (chunk_label_count == 0) {
            stream_protocol_error("EMPTY_CHUNK");
            fprintf(stderr, "error: empty stream label chunk\n");
            goto cleanup;
         }
         if (chunk_label_count != expected_label_count) {
            stream_protocol_print("STREAM_ERROR code=LABEL_COUNT_MISMATCH got=%zu expected=%zu", chunk_label_count, expected_label_count);
            fprintf(stderr, "error: stream chunk label count mismatch: got=%zu expected=%zu\n",
                    chunk_label_count, expected_label_count);
            goto cleanup;
         }
         if (options->show_memory_stats)
            have_chunk_memory_before = hts_walloc_get_stats(&chunk_memory_before);
         if (!synthesize_chunk_to_wav(options, raw_data, raw_data_size, chunk_labels, chunk_label_count,
                                      current_chunk_id, options->pstream_block_frames, &writer,
                                      audio_initialized ? &audio : NULL, &chunk_frames, &chunk_samples)) {
            stream_protocol_print("STREAM_ERROR code=CHUNK_SYNTHESIS_FAILED chunk_id=%zu", current_chunk_id);
            goto cleanup;
         }
         chunk_count++;
         total_frames += chunk_frames;
         total_samples += chunk_samples;
         stream_protocol_print("STREAM_ACK chunk_id=%zu labels=%zu final=%d frames=%zu samples=%zu",
                               current_chunk_id, chunk_label_count, current_final, chunk_frames, chunk_samples);
         if (options->show_memory_stats && have_chunk_memory_before)
            print_stream_chunk_memory_stats(current_chunk_id, &chunk_memory_before);
         fprintf(stderr, "synthesize_stream_chunk: chunk=%zu labels=%zu final=%d frames=%zu samples=%zu status=ok\n",
                 current_chunk_id, chunk_label_count, current_final, chunk_frames, chunk_samples);
         free_label_lines(chunk_labels, chunk_label_count);
         chunk_labels = NULL;
         chunk_label_count = 0;
         chunk_capacity = 0;
         in_chunk = 0;
         if (current_final)
            final_seen = 1;
         expected_chunk_id++;
         continue;
      }
      if (!in_chunk) {
         stream_protocol_error("LABEL_OUTSIDE_CHUNK");
         fprintf(stderr, "error: label outside BEGIN/END stream chunk\n");
         goto cleanup;
      }
      if (!append_label_line(&chunk_labels, &chunk_label_count, &chunk_capacity, start, length)) {
         stream_protocol_error("LABEL_APPEND_FAILED");
         fprintf(stderr, "error: failed to append stream chunk label\n");
         goto cleanup;
      }
   }

   if (ferror(stdin)) {
      stream_protocol_error("STDIN_READ_FAILED");
      fprintf(stderr, "error: failed to read stream chunks from stdin\n");
      goto cleanup;
   }
   if (in_chunk) {
      stream_protocol_error("UNTERMINATED_CHUNK");
      fprintf(stderr, "error: unterminated stream label chunk\n");
      goto cleanup;
   }
   if (!eos_seen) {
      stream_protocol_error("EOS_REQUIRED");
      fprintf(stderr, "error: stream EOS was not received\n");
      goto cleanup;
   }
   if (!final_seen) {
      stream_protocol_error("FINAL_CHUNK_REQUIRED");
      fprintf(stderr, "error: final stream chunk was not received\n");
      goto cleanup;
   }
   if (chunk_count == 0) {
      stream_protocol_error("NO_CHUNKS");
      fprintf(stderr, "error: no stream label chunks received\n");
      goto cleanup;
   }
   if (!wav_writer_close(&writer)) {
      stream_protocol_error("WAV_CLOSE_FAILED");
      goto cleanup;
   }

   if (options->show_memory_stats)
      print_memory_stats("after_stream_synthesis");
   stream_protocol_print("STREAM_DONE chunks=%zu total_frames=%zu total_samples=%zu wav_samples=%zu wav=%s",
                         chunk_count, total_frames, total_samples, writer.sample_count,
                         options->wav_path != NULL ? options->wav_path : "N/A");
   fprintf(stdout,
           "synthesize_stream_raw: mode=view raw=%s chunks=%zu total_frames=%zu total_samples=%zu synth_sample_rate=%zu synth_frame_period=%zu alpha=%.5f wav_sample_rate=%zu wav_samples=%zu wav=%s status=ok\n",
           options->raw_path, chunk_count, total_frames, total_samples, sampling_rate, fperiod, alpha,
           writer.sampling_rate, writer.sample_count, options->wav_path != NULL ? options->wav_path : "N/A");
   ok = 1;

cleanup:
   if (writer.fp != NULL)
      fclose(writer.fp);
   if (audio_initialized)
      HTS_Audio_clear(&audio);
   HTS_ModelSet_clear(&ms);
   free_label_lines(chunk_labels, chunk_label_count);
   free_file_data(raw_data);
   return ok;
}

int main(int argc, char **argv)
{
   HtsEngineRawCliOptions options;
   char **labels = NULL;
   size_t label_count = 0;
   const char *label_source = NULL;
   int exit_code = 0;

   if (!parse_options(argc, argv, &options)) {
      print_usage(stderr);
      return 2;
   }

   if (options.show_help) {
      print_usage(stdout);
      return 0;
   }

   if (options.command == HTS_ENGINE_RAW_CLI_COMMAND_EXPORT_VOICE) {
      size_t export_sample_rate;
      size_t export_frame_period;

      if (options.voice_path == NULL) {
         fprintf(stderr, "error: --voice is required for export-voice\n");
         print_usage(stderr);
         return 2;
      }
      if (options.output_path == NULL) {
         fprintf(stderr, "error: --output is required for export-voice\n");
         print_usage(stderr);
         return 2;
      }
      if (!file_exists(options.voice_path)) {
         fprintf(stderr, "error: voice file does not exist: %s\n", options.voice_path);
         return 2;
      }
      resolve_export_profile(&options, &export_sample_rate, &export_frame_period);
      return hts_export_voice_to_raw(options.voice_path, options.output_path,
                                     export_sample_rate, export_frame_period,
                                     options.show_memory_stats) ? 0 : 3;
   }

   if (options.raw_path == NULL) {
      fprintf(stderr, "error: --raw is required\n");
      print_usage(stderr);
      return 2;
   }
   if (!file_exists(options.raw_path)) {
      fprintf(stderr, "error: raw file does not exist: %s\n", options.raw_path);
      return 2;
   }
   if (options.command == HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_STREAM_VIEW) {
      if (!options.use_stdin || options.labels_path != NULL) {
      fprintf(stderr, "error: synthesize-stream-view requires --stdin and does not accept --labels\n");
         print_usage(stderr);
         return 2;
      }
      if (options.wav_path == NULL && options.audio_buffer_size == 0) {
         fprintf(stderr, "error: synthesize-stream-view requires --wav or --audio-buffer\n");
         print_usage(stderr);
         return 2;
      }
      return synthesize_stream_from_stdin(&options) ? 0 : 3;
   }
   if (options.wav_sample_rate != 0 && options.wav_path == NULL) {
      fprintf(stderr, "error: --wav-sample-rate requires --wav\n");
      print_usage(stderr);
      return 2;
   }
   if (options.labels_path == NULL && !options.use_stdin) {
      fprintf(stderr, "error: either --labels or --stdin is required\n");
      print_usage(stderr);
      return 2;
   }
   if (options.labels_path != NULL && options.use_stdin) {
      fprintf(stderr, "error: --labels and --stdin are mutually exclusive\n");
      print_usage(stderr);
      return 2;
   }
   if (options.labels_path != NULL && !file_exists(options.labels_path)) {
      fprintf(stderr, "error: labels file does not exist: %s\n", options.labels_path);
      return 2;
   }

   if (options.use_stdin) {
      label_source = "stdin";
      if (!load_label_lines_from_stdin(&labels, &label_count))
         return 3;
   } else {
      label_source = options.labels_path;
      if (!load_label_lines(options.labels_path, &labels, &label_count))
         return 3;
   }

   if (options.wav_path != NULL &&
       options.command != HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM &&
       options.command != HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW &&
       options.command != HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW) {
      fprintf(stderr, "error: --wav is only supported by check-gstream and synthesize commands\n");
      exit_code = 2;
      goto cleanup;
   }

   if (!check_streams(&options, labels, label_count, label_source,
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW
                         ? "synthesize_raw"
                         : (options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM ||
                            options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW
                               ? "check_gstream_raw"
                               : (options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM ||
                                  options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM_VIEW
                                     ? "check_pstream_raw"
                                     : "check_sstream_raw")),
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_SSTREAM_VIEW ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM_VIEW ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW,
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_PSTREAM_VIEW ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW,
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_CHECK_GSTREAM_VIEW ||
                      options.command == HTS_ENGINE_RAW_CLI_COMMAND_SYNTHESIZE_VIEW))
      exit_code = 3;

cleanup:
   free_label_lines(labels, label_count);
   return exit_code;
}
