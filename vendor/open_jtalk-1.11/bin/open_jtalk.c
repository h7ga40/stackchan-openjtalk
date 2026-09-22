/* ----------------------------------------------------------------- */
/*           The Japanese TTS System "Open JTalk"                    */
/*           developed by HTS Working Group                          */
/*           http://open-jtalk.sourceforge.net/                      */
/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2008-2018  Nagoya Institute of Technology          */
/*                           Department of Computer Science          */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the HTS working group nor the names of its  */
/*   contributors may be used to endorse or promote products derived */
/*   from this software without specific prior written permission.   */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

#ifndef OPEN_JTALK_C
#define OPEN_JTALK_C

#ifdef __cplusplus
#define OPEN_JTALK_C_START extern "C" {
#define OPEN_JTALK_C_END   }
#else
#define OPEN_JTALK_C_START
#define OPEN_JTALK_C_END
#endif                          /* __CPLUSPLUS */

OPEN_JTALK_C_START;

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "../../src/openjtalk_frontend_api.h"

#define MAXBUFLEN 1024
#define RAW_COMMAND_BUFLEN 8192
#define RAW_PATH_BUFLEN 4096

typedef struct _Open_JTalk {
   OpenJTalkFrontend *frontend;
} Open_JTalk;

static void Open_JTalk_initialize(Open_JTalk * open_jtalk)
{
   open_jtalk->frontend = OpenJTalkFrontend_create();
}

static void Open_JTalk_clear(Open_JTalk * open_jtalk)
{
   OpenJTalkFrontend_destroy(open_jtalk->frontend);
   open_jtalk->frontend = NULL;
}

static int Open_JTalk_load_frontend(Open_JTalk * open_jtalk, char *dn_mecab, char *fn_userdic)
{
   if (OpenJTalkFrontend_load_with_user_dic(open_jtalk->frontend, dn_mecab, fn_userdic) != 1) {
      Open_JTalk_clear(open_jtalk);
      return 0;
   }
   return 1;
}

#ifdef _WIN32
static int path_exists_readable(const char *path)
{
   FILE *fp;

   if (path == NULL)
      return 0;
   fp = fopen(path, "rb");
   if (fp == NULL)
      return 0;
   fclose(fp);
   return 1;
}

static int copy_text(char *buffer, size_t buffer_size, const char *text)
{
   size_t len;

   if (buffer == NULL || text == NULL || buffer_size == 0)
      return 0;
   len = strlen(text);
   if (len + 1 > buffer_size)
      return 0;
   memcpy(buffer, text, len + 1);
   return 1;
}

static int replace_extension_with_raw(const char *source, char *target, size_t target_size)
{
   char *last_slash;
   char *last_backslash;
   char *name_start;
   char *dot;

   if (!copy_text(target, target_size, source))
      return 0;

   last_slash = strrchr(target, '/');
   last_backslash = strrchr(target, '\\');
   name_start = target;
   if (last_slash != NULL && last_slash + 1 > name_start)
      name_start = last_slash + 1;
   if (last_backslash != NULL && last_backslash + 1 > name_start)
      name_start = last_backslash + 1;

   dot = strrchr(name_start, '.');
   if (dot == NULL)
      dot = target + strlen(target);
   if ((size_t) (dot - target) + 5 > target_size)
      return 0;
   memcpy(dot, ".raw", 5);
   return 1;
}

static int append_raw_command_text(char *command, size_t command_size, const char *text)
{
   size_t command_len = strlen(command);
   size_t text_len = strlen(text);

   if (command_len + text_len + 1 >= command_size)
      return 0;
   memcpy(command + command_len, text, text_len + 1);
   return 1;
}

static int append_raw_command_arg(char *command, size_t command_size, const char *arg)
{
   const char *p;
   size_t command_len;

   if (append_raw_command_text(command, command_size, "\"") != 1)
      return 0;
   for (p = arg; *p != '\0'; p++) {
      command_len = strlen(command);
      if (*p == '"') {
         if (command_len + 2 >= command_size)
            return 0;
         command[command_len] = '\\';
         command[command_len + 1] = *p;
         command[command_len + 2] = '\0';
      } else {
         if (command_len + 1 >= command_size)
            return 0;
         command[command_len] = *p;
         command[command_len + 1] = '\0';
      }
   }
   return append_raw_command_text(command, command_size, "\"");
}

static int get_default_raw_engine_path(char *path, size_t path_size)
{
   DWORD len;
   char *slash;

   len = GetModuleFileNameA(NULL, path, (DWORD) path_size);
   if (len == 0 || len >= path_size)
      return 0;
   slash = strrchr(path, '\\');
   if (slash == NULL)
      slash = strrchr(path, '/');
   if (slash == NULL)
      return 0;
   slash[1] = '\0';
   return append_raw_command_text(path, path_size, "hts_engine_raw_cli.exe");
}

static int export_raw_voice_from_htsvoice(const char *raw_engine_path, const char *voice_path, const char *raw_path)
{
   STARTUPINFOA si;
   PROCESS_INFORMATION pi;
   DWORD exit_code = 1;
   char command[RAW_COMMAND_BUFLEN];
   char default_raw_engine_path[MAX_PATH];

   if (raw_engine_path == NULL) {
      if (get_default_raw_engine_path(default_raw_engine_path, sizeof(default_raw_engine_path)) != 1) {
         fprintf(stderr, "Error: default raw engine path cannot be resolved.\n");
         return 0;
      }
      raw_engine_path = default_raw_engine_path;
   }

   command[0] = '\0';
   if (append_raw_command_arg(command, sizeof(command), raw_engine_path) != 1 ||
       append_raw_command_text(command, sizeof(command), " export-voice --voice ") != 1 ||
       append_raw_command_arg(command, sizeof(command), voice_path) != 1 ||
       append_raw_command_text(command, sizeof(command), " --output ") != 1 ||
       append_raw_command_arg(command, sizeof(command), raw_path) != 1) {
      fprintf(stderr, "Error: raw export command line is too long.\n");
      return 0;
   }

   fprintf(stderr, "Info: matching raw voice file was not found; generating: %s\n", raw_path);

   memset(&si, 0, sizeof(si));
   memset(&pi, 0, sizeof(pi));
   si.cb = sizeof(si);
   si.dwFlags = STARTF_USESTDHANDLES;
   si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
   si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
   si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

   if (CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) == 0) {
      fprintf(stderr, "Error: raw export process cannot be started: %s\n", command);
      return 0;
   }

   WaitForSingleObject(pi.hProcess, INFINITE);
   if (GetExitCodeProcess(pi.hProcess, &exit_code) == 0)
      exit_code = 1;
   CloseHandle(pi.hThread);
   CloseHandle(pi.hProcess);

   if (exit_code != 0) {
      fprintf(stderr, "Error: raw export process failed with exit code %lu.\n", (unsigned long) exit_code);
      return 0;
   }

   return path_exists_readable(raw_path);
}

static int resolve_or_create_raw_voice_from_htsvoice(const char *voice_path, const char *raw_engine_path,
                                                     char *raw_path, size_t raw_path_size)
{
   char same_dir_candidate[RAW_PATH_BUFLEN];
   char cwd_candidate[RAW_PATH_BUFLEN];
   const char *last_slash;
   const char *last_backslash;
   const char *base_name;
   int have_same_dir_candidate;
   int have_cwd_candidate;

   if (voice_path == NULL)
      return 0;

   have_same_dir_candidate = replace_extension_with_raw(voice_path, same_dir_candidate, sizeof(same_dir_candidate));
   if (have_same_dir_candidate && path_exists_readable(same_dir_candidate))
      return copy_text(raw_path, raw_path_size, same_dir_candidate);

   last_slash = strrchr(voice_path, '/');
   last_backslash = strrchr(voice_path, '\\');
   base_name = voice_path;
   if (last_slash != NULL && last_slash + 1 > base_name)
      base_name = last_slash + 1;
   if (last_backslash != NULL && last_backslash + 1 > base_name)
      base_name = last_backslash + 1;

   have_cwd_candidate = replace_extension_with_raw(base_name, cwd_candidate, sizeof(cwd_candidate));
   if (have_cwd_candidate && path_exists_readable(cwd_candidate))
      return copy_text(raw_path, raw_path_size, cwd_candidate);

   if (have_same_dir_candidate &&
       export_raw_voice_from_htsvoice(raw_engine_path, voice_path, same_dir_candidate) &&
       path_exists_readable(same_dir_candidate))
      return copy_text(raw_path, raw_path_size, same_dir_candidate);

   if (have_cwd_candidate &&
       (!have_same_dir_candidate || strcmp(same_dir_candidate, cwd_candidate) != 0) &&
       export_raw_voice_from_htsvoice(raw_engine_path, voice_path, cwd_candidate) &&
       path_exists_readable(cwd_candidate))
      return copy_text(raw_path, raw_path_size, cwd_candidate);

   return 0;
}

static int write_child_stdin_text(HANDLE child_stdin_write, const char *text)
{
   DWORD text_len = (DWORD) strlen(text);
   DWORD written;

   return WriteFile(child_stdin_write, text, text_len, &written, NULL) != 0 && written == text_len;
}

static int write_child_stdin_label(HANDLE child_stdin_write, const char *label)
{
   return write_child_stdin_text(child_stdin_write, label) &&
          write_child_stdin_text(child_stdin_write, "\n");
}

static int is_stream_chunk_boundary_label(const char *label)
{
   return strstr(label, "-pau+") != NULL;
}

static int Open_JTalk_synthesis_raw_cli(char **labels, int label_count,
                                        const char *raw_engine_path,
                                        const char *raw_voice_path,
                                        const char *wavfn,
                                        size_t requested_sample_rate,
                                        int alpha_specified,
                                        float alpha,
                                        size_t raw_pstream_block_frames,
                                        size_t audio_buffer_size, float beta, float speed,
                                        float half_tone, float msd_threshold,
                                        float gv_spectrum_weight, float gv_lf0_weight,
                                        float volume_db)
{
   SECURITY_ATTRIBUTES sa;
   STARTUPINFOA si;
   PROCESS_INFORMATION pi;
   HANDLE child_stdin_read = NULL;
   HANDLE child_stdin_write = NULL;
   DWORD exit_code = 1;
   int result = 0;
   int i;
   int write_ok = 1;
   int chunk_id = 1;
   char command[RAW_COMMAND_BUFLEN];
   char chunk_header[64];
   char chunk_footer[64];
   char block_frames_arg[64];
   char sample_rate_arg[64];
   char alpha_arg[64];
   char audio_buffer_arg[64];
   char synthesis_options_arg[384];
   char default_raw_engine_path[MAX_PATH];

   if (raw_engine_path == NULL) {
      if (get_default_raw_engine_path(default_raw_engine_path, sizeof(default_raw_engine_path)) != 1) {
         fprintf(stderr, "Error: default raw engine path cannot be resolved.\n");
         return 0;
      }
      raw_engine_path = default_raw_engine_path;
   }

   command[0] = '\0';
   if (append_raw_command_arg(command, sizeof(command), raw_engine_path) != 1 ||
       append_raw_command_text(command, sizeof(command), " synthesize-stream-view --raw ") != 1 ||
       append_raw_command_arg(command, sizeof(command), raw_voice_path) != 1 ||
       append_raw_command_text(command, sizeof(command), " --stdin") != 1) {
      fprintf(stderr, "Error: raw engine command line is too long.\n");
      return 0;
   }
   if (requested_sample_rate == 16000) {
      if (append_raw_command_text(command, sizeof(command), " --tts-16khz") != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   } else if (requested_sample_rate == 48000) {
      if (append_raw_command_text(command, sizeof(command), " --tts-48khz") != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   } else if (requested_sample_rate > 0) {
      sprintf(sample_rate_arg, " --wav-sample-rate %lu", (unsigned long) requested_sample_rate);
      if (append_raw_command_text(command, sizeof(command), sample_rate_arg) != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   }
   if (alpha_specified) {
      sprintf(alpha_arg, " --alpha %.9g", alpha);
      if (append_raw_command_text(command, sizeof(command), alpha_arg) != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   }
   if (wavfn != NULL) {
      if (append_raw_command_text(command, sizeof(command), " --wav ") != 1 ||
          append_raw_command_arg(command, sizeof(command), wavfn) != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   }
   if (raw_pstream_block_frames > 0) {
      sprintf(block_frames_arg, " --pstream-block-frames %lu", (unsigned long) raw_pstream_block_frames);
      if (append_raw_command_text(command, sizeof(command), block_frames_arg) != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   }
   if (audio_buffer_size > 0) {
      sprintf(audio_buffer_arg, " --audio-buffer %lu", (unsigned long) audio_buffer_size);
      if (append_raw_command_text(command, sizeof(command), audio_buffer_arg) != 1) {
         fprintf(stderr, "Error: raw engine command line is too long.\n");
         return 0;
      }
   }
   sprintf(synthesis_options_arg,
           " --beta %.9g --speed %.9g --half-tone %.9g --msd-threshold %.9g"
           " --gv-spectrum-weight %.9g --gv-lf0-weight %.9g --volume-db %.9g",
           beta, speed, half_tone, msd_threshold, gv_spectrum_weight, gv_lf0_weight, volume_db);
   if (append_raw_command_text(command, sizeof(command), synthesis_options_arg) != 1) {
      fprintf(stderr, "Error: raw engine command line is too long.\n");
      return 0;
   }

   memset(&sa, 0, sizeof(sa));
   sa.nLength = sizeof(sa);
   sa.bInheritHandle = TRUE;
   if (CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0) == 0) {
      fprintf(stderr, "Error: raw engine stdin pipe cannot be created.\n");
      return 0;
   }
   SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);

   memset(&si, 0, sizeof(si));
   memset(&pi, 0, sizeof(pi));
   si.cb = sizeof(si);
   si.dwFlags = STARTF_USESTDHANDLES;
   si.hStdInput = child_stdin_read;
   si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
   si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

   if (CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) == 0) {
      fprintf(stderr, "Error: raw engine process cannot be started: %s\n", command);
      CloseHandle(child_stdin_read);
      CloseHandle(child_stdin_write);
      return 0;
   }
   CloseHandle(child_stdin_read);

   for (i = 0; i < label_count && write_ok; chunk_id++) {
      int chunk_start = i;
      int chunk_end = i;
      int chunk_label_count;
      int final_chunk;
      int j;

      while (chunk_end + 1 < label_count && !is_stream_chunk_boundary_label(labels[chunk_end]))
         chunk_end++;
      chunk_label_count = chunk_end - chunk_start + 1;
      final_chunk = chunk_end + 1 >= label_count;

      sprintf(chunk_header, "BEGIN chunk_id=%d labels=%d final=%d\n", chunk_id, chunk_label_count, final_chunk ? 1 : 0);
      if (!write_child_stdin_text(child_stdin_write, chunk_header)) {
         fprintf(stderr, "Error: stream chunk header cannot be written to raw engine stdin.\n");
         write_ok = 0;
         break;
      }

      for (j = chunk_start; j <= chunk_end; j++) {
         if (!write_child_stdin_label(child_stdin_write, labels[j])) {
            fprintf(stderr, "Error: full-context label cannot be written to raw engine stdin.\n");
            write_ok = 0;
            break;
         }
      }
      if (!write_ok)
         break;

      sprintf(chunk_footer, "END chunk_id=%d\n", chunk_id);
      if (!write_child_stdin_text(child_stdin_write, chunk_footer)) {
         fprintf(stderr, "Error: stream chunk footer cannot be written to raw engine stdin.\n");
         write_ok = 0;
         break;
      }
      i = chunk_end + 1;
   }
   if (write_ok && !write_child_stdin_text(child_stdin_write, "EOS\n")) {
      fprintf(stderr, "Error: stream EOS cannot be written to raw engine stdin.\n");
      write_ok = 0;
   }
   CloseHandle(child_stdin_write);

   WaitForSingleObject(pi.hProcess, INFINITE);
   if (GetExitCodeProcess(pi.hProcess, &exit_code) != 0 && exit_code == 0 && write_ok && i == label_count)
      result = 1;
   if (result != 1)
      fprintf(stderr, "Error: raw engine process failed with exit code %lu.\n", (unsigned long) exit_code);

   CloseHandle(pi.hThread);
   CloseHandle(pi.hProcess);
   return result;
}
#endif

static int Open_JTalk_synthesis(Open_JTalk * open_jtalk, const char *txt,
                                FILE * logfp, const char *raw_engine_path,
                                const char *raw_voice_path, const char *wavfn,
                                size_t requested_sample_rate, int alpha_specified,
                                float alpha, size_t raw_pstream_block_frames,
                                size_t audio_buffer_size, float beta, float speed,
                                float half_tone, float msd_threshold,
                                float gv_spectrum_weight, float gv_lf0_weight,
                                float volume_db)
{
   int result = 0;
   int label_size;
   char **label_feature;

   if (OpenJTalkFrontend_make_labels(open_jtalk->frontend, txt) != 1)
      return 0;

   label_size = OpenJTalkFrontend_get_label_size(open_jtalk->frontend);
   label_feature = OpenJTalkFrontend_get_label_feature(open_jtalk->frontend);

   if (label_size > 2) {
#ifdef _WIN32
      if (raw_voice_path != NULL) {
         result = Open_JTalk_synthesis_raw_cli(label_feature, label_size,
                                               raw_engine_path, raw_voice_path, wavfn,
                                               requested_sample_rate, alpha_specified, alpha,
                                               raw_pstream_block_frames, audio_buffer_size, beta, speed,
                                               half_tone, msd_threshold, gv_spectrum_weight,
                                               gv_lf0_weight, volume_db);
      } else {
         fprintf(stderr, "Error: raw HTS voice must be specified or resolvable.\n");
         result = 0;
      }
#else
      if (raw_voice_path != NULL) {
         fprintf(stderr, "Error: raw engine child process mode is only implemented on Windows.\n");
         result = 0;
      } else {
         fprintf(stderr, "Error: raw HTS voice must be specified or resolvable.\n");
         result = 0;
      }
#endif
      if (logfp != NULL) {
         fprintf(logfp, "[Text analysis result]\n");
         OpenJTalkFrontend_fprint_text_analysis(open_jtalk->frontend, logfp);
         fprintf(logfp, "\n[Output label]\n");
         {
            int label_index;

            for (label_index = 0; label_index < label_size; label_index++)
               fprintf(logfp, "%s\n", label_feature[label_index]);
         }
         fprintf(logfp, "\n");
      }
   }
   OpenJTalkFrontend_refresh(open_jtalk->frontend);

   return result;
}

static void usage()
{
   fprintf(stderr, "The Japanese TTS System \"Open JTalk\"\n");
   fprintf(stderr, "Version 1.10 (http://open-jtalk.sourceforge.net/)\n");
   fprintf(stderr, "Copyright (C) 2008-2016 Nagoya Institute of Technology\n");
   fprintf(stderr, "All rights reserved.\n");
   fprintf(stderr, "\n");
   fprintf(stderr, "Yet Another Part-of-Speech and Morphological Analyzer \"Mecab\"\n");
   fprintf(stderr, "Version 0.996 (http://mecab.sourceforge.net/)\n");
   fprintf(stderr, "Copyright (C) 2001-2008 Taku Kudo\n");
   fprintf(stderr, "              2004-2008 Nippon Telegraph and Telephone Corporation\n");
   fprintf(stderr, "All rights reserved.\n");
   fprintf(stderr, "\n");
   fprintf(stderr, "NAIST Japanese Dictionary\n");
   fprintf(stderr, "Version 0.6.1-20090630 (http://naist-jdic.sourceforge.jp/)\n");
   fprintf(stderr, "Copyright (C) 2009 Nara Institute of Science and Technology\n");
   fprintf(stderr, "All rights reserved.\n");
   fprintf(stderr, "\n");
   fprintf(stderr, "UniDic\n");
   fprintf(stderr, "Version 2.2.0 (https://unidic.ninjal.ac.jp/)\n");
   fprintf(stderr, "Copyright (C) 2011-2017 The UniDic Consortium\n");
   fprintf(stderr, "All rights reserved.\n");
   fprintf(stderr, "\n");
   fprintf(stderr, "open_jtalk - The Japanese TTS system \"Open JTalk\"\n");
   fprintf(stderr, "\n");
   fprintf(stderr, "  usage:\n");
   fprintf(stderr, "       open_jtalk [ options ] [ infile ] \n");
   fprintf(stderr,
           "  options:                                                                   [  def][ min-- max]\n");
   fprintf(stderr,
           "    -x  dir        : dictionary directory                                    [  N/A]\n");
   fprintf(stderr,
           "    --user-dic file : MeCab user dictionary file                             [  N/A]\n");
   fprintf(stderr,
           "    -m  htsvoice   : HTS voice file; matching .raw is used for synthesis    [  N/A]\n");
   fprintf(stderr,
           "    -raw file      : raw HTS voice file for child-process synthesis override [ auto]\n");
   fprintf(stderr,
           "    -raw-engine exe: raw HTS engine CLI executable                           [auto]\n");
   fprintf(stderr,
           "    -raw-block-frames i: raw CLI PStream block size (0 disables)             [    0][   0--    ]\n");
   fprintf(stderr,
           "    -z  i          : audio buffer size (if i==0, turn off)                   [    0][   0--    ]\n");
   fprintf(stderr,
           "    -ow s          : filename of output wav audio (generated speech)         [  N/A]\n");
   fprintf(stderr,
           "    -ot s          : filename of output trace information                    [  N/A]\n");
   fprintf(stderr,
           "    -s  i          : sampling frequency                                      [ auto][   1--    ]\n");
   fprintf(stderr,
           "    -p  i          : unsupported in raw-only mode                           [  N/A]\n");
   fprintf(stderr,
           "    -a  f          : all-pass constant                                       [ auto][ 0.0-- 1.0]\n");
   fprintf(stderr,
           "    -b  f          : postfiltering coefficient                               [  0.0][ 0.0-- 1.0]\n");
   fprintf(stderr,
           "    -r  f          : speech speed rate                                       [  1.0][ 0.0--    ]\n");
   fprintf(stderr,
           "    -fm f          : additional half-tone                                    [  0.0][    --    ]\n");
   fprintf(stderr,
           "    -u  f          : voiced/unvoiced threshold                               [  0.5][ 0.0-- 1.0]\n");
   fprintf(stderr,
           "    -jm f          : weight of GV for spectrum                               [  1.0][ 0.0--    ]\n");
   fprintf(stderr,
           "    -jf f          : weight of GV for log F0                                 [  1.0][ 0.0--    ]\n");
   fprintf(stderr,
           "    -g  f          : volume (dB)                                             [  0.0][    --    ]\n");
   fprintf(stderr, "\n");
   fprintf(stderr, "  stable interface aliases:\n");
   fprintf(stderr, "    --dic dir [--user-dic file] --voice file --sample-rate hz --input file --output file\n");
   fprintf(stderr, "  infile:\n");
   fprintf(stderr,
           "    text file                                                                [stdin]\n");
   fprintf(stderr, "\n");

   exit(0);
}

void wmem_init();

int main(int argc, char **argv)
{
   size_t i;

   /* text */
   char buff[MAXBUFLEN];

   /* Open JTalk */
   Open_JTalk open_jtalk;

   /* dictionary directory */
   char *dn_dict = NULL;
   char *fn_userdic = NULL;

   /* HTS voice */
   char *fn_voice = NULL;
   char *fn_raw_voice = NULL;
   char *fn_raw_engine = NULL;
   char auto_raw_voice[RAW_PATH_BUFLEN];
   size_t raw_pstream_block_frames = 0;

   /* input text file name */
   FILE *txtfp = stdin;
   char *txtfn = NULL;

   /* output file pointers */
   FILE *logfp = NULL;
   char *wavfn = NULL;
   size_t requested_sample_rate = 0;
   size_t audio_buffer_size = 0;
   float beta = 0.0f;
   float speed = 1.0f;
   float half_tone = 0.0f;
   float msd_threshold = 0.5f;
   float gv_spectrum_weight = 1.0f;
   float gv_lf0_weight = 1.0f;
   float volume_db = 0.0f;
   int alpha_specified = 0;
   float alpha = 0.0f;

   auto_raw_voice[0] = '\0';

   wmem_init();

   /* output usage */
   if (argc == 1)
      usage();

   /* initialize Open JTalk */
   Open_JTalk_initialize(&open_jtalk);

   /* get dictionary directory */
   for (i = 0; i < argc; i++) {
      if (strcmp(argv[i], "--dic") == 0 && i + 1 < (size_t) argc)
         dn_dict = argv[++i];
      else if (strcmp(argv[i], "--user-dic") == 0 && i + 1 < (size_t) argc)
         fn_userdic = argv[++i];
      else if (strcmp(argv[i], "--voice") == 0 && i + 1 < (size_t) argc)
         fn_voice = argv[++i];
      else if (strcmp(argv[i], "--input") == 0 && i + 1 < (size_t) argc)
         txtfn = argv[++i];
      else if (strcmp(argv[i], "--output") == 0 && i + 1 < (size_t) argc)
         wavfn = argv[++i];
      if (argv[i][0] == '-' && argv[i][1] == 'x')
         dn_dict = argv[++i];
      if (argv[i][0] == '-' && argv[i][1] == 'h')
         usage();
      if (strcmp(argv[i], "-raw") == 0)
         fn_raw_voice = argv[++i];
      if (strcmp(argv[i], "-raw-engine") == 0)
         fn_raw_engine = argv[++i];
      if (strcmp(argv[i], "-raw-block-frames") == 0)
         raw_pstream_block_frames = (size_t) atoi(argv[++i]);
   }
   if (dn_dict == NULL) {
      fprintf(stderr, "Error: Dictionary must be specified.\n");
      exit(1);
   }

   /* get HTS voice file name */
   for (i = 0; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'm')
         fn_voice = argv[++i];
   }
   if (fn_voice == NULL && fn_raw_voice == NULL) {
      fprintf(stderr, "Error: HTS voice must be specified.\n");
      exit(1);
   }

   if (fn_raw_voice == NULL && fn_voice != NULL) {
#ifdef _WIN32
      if (resolve_or_create_raw_voice_from_htsvoice(fn_voice, fn_raw_engine, auto_raw_voice, sizeof(auto_raw_voice)) != 1) {
         fprintf(stderr, "Error: matching raw HTS voice file cannot be found or generated for: %s\n", fn_voice);
         fprintf(stderr, "       Specify -raw VOICE.raw or make the .htsvoice directory/current directory writable.\n");
         Open_JTalk_clear(&open_jtalk);
         exit(1);
      }
      fn_raw_voice = auto_raw_voice;
#endif
   }

   if (fn_raw_voice == NULL) {
      fprintf(stderr, "Error: raw HTS voice must be specified or resolvable.\n");
      Open_JTalk_clear(&open_jtalk);
      exit(1);
   }

   if (Open_JTalk_load_frontend(&open_jtalk, dn_dict, fn_userdic) != TRUE) {
      if (fn_userdic != NULL)
         fprintf(stderr, "Error: Dictionary cannot be loaded with user dictionary: %s\n", fn_userdic);
      else
         fprintf(stderr, "Error: Dictionary cannot be loaded.\n");
      Open_JTalk_clear(&open_jtalk);
      exit(1);
   }

   /* get options */
   while (--argc) {
      if (**++argv == '-') {
         if (strcmp(*argv, "-raw") == 0) {
            fn_raw_voice = *++argv;
            --argc;
            continue;
         }
         if (strcmp(*argv, "-raw-engine") == 0) {
            fn_raw_engine = *++argv;
            --argc;
            continue;
         }
         if (strcmp(*argv, "-raw-block-frames") == 0) {
            raw_pstream_block_frames = (size_t) atoi(*++argv);
            --argc;
            continue;
         }
         if (strcmp(*argv, "--dic") == 0 || strcmp(*argv, "--user-dic") == 0 || strcmp(*argv, "--voice") == 0 ||
             strcmp(*argv, "--input") == 0 || strcmp(*argv, "--output") == 0) {
            argv++;
            --argc;
            continue;
         }
         if (strcmp(*argv, "--sample-rate") == 0) {
            requested_sample_rate = (size_t) atoi(*++argv);
            --argc;
            continue;
         }
         switch (*(*argv + 1)) {
         case 'o':
            switch (*(*argv + 2)) {
            case 'w':
               wavfn = *++argv;
               break;
            case 't':
               logfp = fopen(*++argv, "wt");
               break;
            default:
               fprintf(stderr, "Error: Invalid option '-o%c'.\n", *(*argv + 2));
               exit(1);
            }
            --argc;
            break;
         case 'h':
            usage();
            break;
         case 'x':
            argv++;             /* dictionary was already loaded */
            --argc;
            break;
         case 'm':
            argv++;             /* HTS voice was already loaded */
            --argc;
            break;
         case 's':
            requested_sample_rate = (size_t) atoi(*++argv);
            --argc;
            break;
         case 'p':
            fprintf(stderr, "Error: -p is unsupported in raw-only mode.\n");
            exit(1);
         case 'a':
            alpha = (float)atof(*++argv);
            alpha_specified = 1;
            --argc;
            break;
         case 'b':
            beta = (float)atof(*++argv);
            --argc;
            break;
         case 'r':
            speed = (float)atof(*++argv);
            --argc;
            break;
         case 'f':
            switch (*(*argv + 2)) {
            case 'm':
               half_tone = (float)atof(*++argv);
               break;
            default:
               fprintf(stderr, "Error: Invalid option '-f%c'.\n", *(*argv + 2));
               exit(1);
            }
            --argc;
            break;
         case 'u':
            msd_threshold = (float)atof(*++argv);
            --argc;
            break;
         case 'j':
            switch (*(*argv + 2)) {
            case 'm':
               gv_spectrum_weight = (float)atof(*++argv);
               break;
            case 'f':
            case 'p':
               gv_lf0_weight = (float)atof(*++argv);
               break;
            default:
               fprintf(stderr, "Error: Invalid option '-j%c'.\n", *(*argv + 2));
               exit(1);
            }
            --argc;
            break;
         case 'g':
            volume_db = (float)atof(*++argv);
            --argc;
            break;
         case 'z':
            audio_buffer_size = (size_t) atoi(*++argv);
            --argc;
            break;
         default:
            fprintf(stderr, "Error: Invalid option '-%c'.\n", *(*argv + 1));
            exit(1);
         }
      } else {
         txtfn = *argv;
         txtfp = fopen(txtfn, "rt");
      }
   }

   if (txtfn != NULL) {
      txtfp = fopen(txtfn, "rt");
      if (txtfp == NULL) {
         fprintf(stderr, "Error: Input text file cannot be opened: %s\n", txtfn);
         Open_JTalk_clear(&open_jtalk);
         exit(1);
      }
   }

   /* synthesize */
   if (fgets(buff, MAXBUFLEN - 1, txtfp) == NULL) {
      fprintf(stderr, "Error: Input text is empty or cannot be read.\n");
      Open_JTalk_clear(&open_jtalk);
      exit(1);
   }
   if (Open_JTalk_synthesis(&open_jtalk, buff, logfp, fn_raw_engine, fn_raw_voice, wavfn,
                            requested_sample_rate, alpha_specified, alpha,
                            raw_pstream_block_frames, audio_buffer_size, beta, speed,
                            half_tone, msd_threshold, gv_spectrum_weight,
                            gv_lf0_weight, volume_db) != TRUE) {
      fprintf(stderr, "Error: waveform cannot be synthesized.\n");
      Open_JTalk_clear(&open_jtalk);
      exit(1);
   }

   /* free memory */
   Open_JTalk_clear(&open_jtalk);

   /* close files */
   if (txtfn != NULL)
      fclose(txtfp);
   if (logfp != NULL)
      fclose(logfp);

   return 0;
}

OPEN_JTALK_C_END;
#endif                          /* !OPEN_JTALK_C */
