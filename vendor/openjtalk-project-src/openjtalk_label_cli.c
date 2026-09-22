#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hts_walloc.h"
#include "openjtalk_frontend_api.h"

typedef struct OpenJTalkLabelCliOptions {
   const char *dictionary_path;
   const char *user_dictionary_path;
   const char *input_path;
   const char *output_path;
   int show_help;
} OpenJTalkLabelCliOptions;

static void print_usage(FILE *stream)
{
   fprintf(stream, "Usage:\n");
   fprintf(stream, "  openjtalk_label_cli.exe --dic DICTIONARY_DIR [--user-dic USER.dic] --input TEXT.txt --output LABELS.txt\n");
   fprintf(stream, "\nOptions:\n");
   fprintf(stream, "  --dic PATH       OpenJTalk dictionary directory.\n");
   fprintf(stream, "  --user-dic PATH  MeCab user dictionary file.\n");
   fprintf(stream, "  --input PATH     UTF-8 text input. First non-empty, non-comment line is used.\n");
   fprintf(stream, "  --output PATH    Full-context label output text file.\n");
   fprintf(stream, "  --help           Show this help text.\n");
}

static int option_requires_value(int argc, int index, const char *option)
{
   if (index + 1 < argc)
      return 0;
   fprintf(stderr, "error: %s requires a value\n", option);
   return 1;
}

static int parse_options(int argc, char **argv, OpenJTalkLabelCliOptions *options)
{
   int i;

   memset(options, 0, sizeof(*options));
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

static int read_first_phrase(const char *path, char *buffer, size_t buffer_size)
{
   FILE *fp;
   char line[8192];

   fp = fopen(path, "rb");
   if (fp == NULL) {
      fprintf(stderr, "error: failed to open input '%s': errno=%d\n", path, errno);
      return 0;
   }

   while (fgets(line, sizeof(line), fp) != NULL) {
      char *phrase = trim_line(line);
      size_t length;

      if (*phrase == '\0' || *phrase == '#')
         continue;
      length = strlen(phrase);
      if (length + 1 > buffer_size) {
         fprintf(stderr, "error: input phrase is too long\n");
         fclose(fp);
         return 0;
      }
      memcpy(buffer, phrase, length + 1);
      fclose(fp);
      return 1;
   }

   if (ferror(fp)) {
      fprintf(stderr, "error: failed to read input '%s'\n", path);
      fclose(fp);
      return 0;
   }

   fprintf(stderr, "error: input has no phrase: %s\n", path);
   fclose(fp);
   return 0;
}

static int write_labels(const char *path, char **labels, int label_count)
{
   FILE *fp;
   int i;

   fp = fopen(path, "wb");
   if (fp == NULL) {
      fprintf(stderr, "error: failed to open output '%s': errno=%d\n", path, errno);
      return 0;
   }
   for (i = 0; i < label_count; i++)
      fprintf(fp, "%s\n", labels[i]);
   fclose(fp);
   return 1;
}

int main(int argc, char **argv)
{
   OpenJTalkLabelCliOptions options;
   OpenJTalkFrontend *frontend = NULL;
   char phrase[8192];
   char **labels;
   int label_count;
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
   if (!read_first_phrase(options.input_path, phrase, sizeof(phrase)))
      return 2;

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
   if (OpenJTalkFrontend_make_labels(frontend, phrase) != 1) {
      fprintf(stderr, "error: failed to make labels\n");
      exit_code = 3;
      goto cleanup;
   }
   label_count = OpenJTalkFrontend_get_label_size(frontend);
   labels = OpenJTalkFrontend_get_label_feature(frontend);
   if (labels == NULL || label_count <= 0 || !write_labels(options.output_path, labels, label_count))
      exit_code = 3;

cleanup:
   OpenJTalkFrontend_destroy(frontend);
   return exit_code;
}
