#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hts_walloc.h"
#include "openjtalk_frontend_api.h"

typedef enum OpenJTalkKanaCliFormat {
   OPENJTALK_KANA_FORMAT_HIRA,
   OPENJTALK_KANA_FORMAT_KATA
} OpenJTalkKanaCliFormat;

typedef struct OpenJTalkKanaCliOptions {
   const char *dictionary_path;
   const char *user_dictionary_path;
   const char *input_path;
   const char *output_path;
   OpenJTalkKanaCliFormat format;
   int show_help;
} OpenJTalkKanaCliOptions;

static void print_usage(FILE *stream)
{
   fprintf(stream, "Usage:\n");
   fprintf(stream, "  openjtalk_kana_cli.exe --dic DICTIONARY_DIR [--user-dic USER.dic] --input INPUT.txt --output OUTPUT.txt [--format hira|kata]\n");
   fprintf(stream, "\nOptions:\n");
   fprintf(stream, "  --dic PATH          OpenJTalk dictionary directory.\n");
   fprintf(stream, "  --user-dic PATH     MeCab user dictionary file.\n");
   fprintf(stream, "  --input PATH        UTF-8 text input. Empty lines and lines starting with # are ignored.\n");
   fprintf(stream, "  --output PATH       UTF-8 kana output text file without BOM.\n");
   fprintf(stream, "  --format hira|kata  Output hiragana or katakana. Default: hira.\n");
   fprintf(stream, "  --help              Show this help text.\n");
}

static int option_requires_value(int argc, int index, const char *option)
{
   if (index + 1 < argc)
      return 0;
   fprintf(stderr, "error: %s requires a value\n", option);
   return 1;
}

static int parse_options(int argc, char **argv, OpenJTalkKanaCliOptions *options)
{
   int i;

   memset(options, 0, sizeof(*options));
   options->format = OPENJTALK_KANA_FORMAT_HIRA;
   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
         options->show_help = 1;
      } else if (strcmp(argv[i], "--dic") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->dictionary_path = argv[++i];
      } else if (strcmp(argv[i], "--user-dic") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->user_dictionary_path = argv[++i];
      } else if (strcmp(argv[i], "--input") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->input_path = argv[++i];
      } else if (strcmp(argv[i], "--output") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         options->output_path = argv[++i];
      } else if (strcmp(argv[i], "--format") == 0) {
         if (option_requires_value(argc, i, argv[i]))
            return 0;
         i++;
         if (strcmp(argv[i], "hira") == 0) {
            options->format = OPENJTALK_KANA_FORMAT_HIRA;
         } else if (strcmp(argv[i], "kata") == 0) {
            options->format = OPENJTALK_KANA_FORMAT_KATA;
         } else {
            fprintf(stderr, "error: unknown --format value: %s\n", argv[i]);
            return 0;
         }
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

static char *trim_line(char *line)
{
   char *start = line;
   char *end;

   if ((unsigned char) start[0] == 0xef && (unsigned char) start[1] == 0xbb && (unsigned char) start[2] == 0xbf)
      start += 3;
   while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
      start++;
   end = start + strlen(start);
   while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
      end--;
   *end = '\0';
   return start;
}

static int convert_file(OpenJTalkFrontend *frontend, const OpenJTalkKanaCliOptions *options)
{
   FILE *input;
   FILE *output;
   char line[8192];
   int line_number = 0;
   int wrote_any = 0;

   input = fopen(options->input_path, "rb");
   if (input == NULL) {
      fprintf(stderr, "error: failed to open input '%s': errno=%d\n", options->input_path, errno);
      return 0;
   }
   output = fopen(options->output_path, "wb");
   if (output == NULL) {
      fprintf(stderr, "error: failed to open output '%s': errno=%d\n", options->output_path, errno);
      fclose(input);
      return 0;
   }

   while (fgets(line, sizeof(line), input) != NULL) {
      char *text;
      char *kana;

      line_number++;
      if (strchr(line, '\n') == NULL && !feof(input)) {
         fprintf(stderr, "error: input line %d is too long\n", line_number);
         fclose(output);
         fclose(input);
         return 0;
      }
      text = trim_line(line);
      if (*text == '\0' || *text == '#')
         continue;

      if (wrote_any)
         OpenJTalkFrontend_refresh(frontend);
      if (OpenJTalkFrontend_make_labels(frontend, text) != 1) {
         fprintf(stderr, "error: failed to analyze input line %d\n", line_number);
         fclose(output);
         fclose(input);
         return 0;
      }

      kana = OpenJTalkFrontend_get_kana(frontend, options->format == OPENJTALK_KANA_FORMAT_HIRA);
      if (kana == NULL) {
         fprintf(stderr, "error: failed to build kana for input line %d\n", line_number);
         fclose(output);
         fclose(input);
         return 0;
      }
      fprintf(output, "%s\n", kana);
      free(kana);
      wrote_any = 1;
   }

   if (ferror(input)) {
      fprintf(stderr, "error: failed to read input '%s'\n", options->input_path);
      fclose(output);
      fclose(input);
      return 0;
   }

   fclose(output);
   fclose(input);
   return 1;
}

int main(int argc, char **argv)
{
   OpenJTalkKanaCliOptions options;
   OpenJTalkFrontend *frontend = NULL;
   int exit_code = 0;

   if (!parse_options(argc, argv, &options)) {
      print_usage(stderr);
      return 2;
   }
   if (options.show_help) {
      print_usage(stdout);
      return 0;
   }
   if (options.dictionary_path == NULL || options.input_path == NULL || options.output_path == NULL) {
      fprintf(stderr, "error: --dic, --input, and --output are required\n");
      print_usage(stderr);
      return 2;
   }
   if (!file_exists(options.input_path)) {
      fprintf(stderr, "error: input file does not exist: %s\n", options.input_path);
      return 2;
   }

   wmem_init();
   frontend = OpenJTalkFrontend_create();
   if (frontend == NULL) {
      fprintf(stderr, "error: failed to create OpenJTalk frontend\n");
      return 3;
   }
   if (OpenJTalkFrontend_load_with_user_dic(frontend, options.dictionary_path, options.user_dictionary_path) != 1) {
      if (options.user_dictionary_path != NULL)
         fprintf(stderr, "error: failed to load dictionary: %s with user dictionary: %s\n", options.dictionary_path, options.user_dictionary_path);
      else
         fprintf(stderr, "error: failed to load dictionary: %s\n", options.dictionary_path);
      exit_code = 3;
      goto cleanup;
   }
   if (!convert_file(frontend, &options))
      exit_code = 3;

cleanup:
   OpenJTalkFrontend_destroy(frontend);
   return exit_code;
}
