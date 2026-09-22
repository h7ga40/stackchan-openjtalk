#include "openjtalk_frontend_api.h"

#include <stdlib.h>
#include <string.h>

#include "mecab.h"
#include "njd.h"
#include "jpcommon.h"
#include "text2mecab.h"
#include "mecab2njd.h"
#include "njd_set_pronunciation.h"
#include "njd_set_digit.h"
#include "njd_set_accent_phrase.h"
#include "njd_set_accent_type.h"
#include "njd_set_unvoiced_vowel.h"
#include "njd_set_long_vowel.h"
#include "njd2jpcommon.h"

#define OPENJTALK_FRONTEND_MAXBUFLEN 1024

struct OpenJTalkFrontend {
   Mecab mecab;
   NJD njd;
   JPCommon jpcommon;
   int initialized;
};

static void OpenJTalkFrontend_initialize(OpenJTalkFrontend *frontend)
{
   Mecab_initialize(&frontend->mecab);
   NJD_initialize(&frontend->njd);
   JPCommon_initialize(&frontend->jpcommon);
   frontend->initialized = 1;
}

OpenJTalkFrontend *OpenJTalkFrontend_create(void)
{
   OpenJTalkFrontend *frontend = (OpenJTalkFrontend *) calloc(1, sizeof(OpenJTalkFrontend));

   if (frontend == NULL)
      return NULL;
   OpenJTalkFrontend_initialize(frontend);
   return frontend;
}

void OpenJTalkFrontend_clear(OpenJTalkFrontend *frontend)
{
   if (frontend == NULL || frontend->initialized == 0)
      return;

   Mecab_clear(&frontend->mecab);
   NJD_clear(&frontend->njd);
   JPCommon_clear(&frontend->jpcommon);
   frontend->initialized = 0;
}

void OpenJTalkFrontend_destroy(OpenJTalkFrontend *frontend)
{
   if (frontend == NULL)
      return;

   OpenJTalkFrontend_clear(frontend);
   free(frontend);
}

int OpenJTalkFrontend_load(OpenJTalkFrontend *frontend, const char *dictionary_path)
{
   return OpenJTalkFrontend_load_with_user_dic(frontend, dictionary_path, NULL);
}

int OpenJTalkFrontend_load_with_user_dic(OpenJTalkFrontend *frontend, const char *dictionary_path,
                                         const char *user_dictionary_path)
{
   if (frontend == NULL || dictionary_path == NULL)
      return 0;
   if (frontend->initialized == 0)
      OpenJTalkFrontend_initialize(frontend);
   if (Mecab_load_with_userdic(&frontend->mecab, dictionary_path, user_dictionary_path) != TRUE) {
      OpenJTalkFrontend_clear(frontend);
      return 0;
   }
   return 1;
}

void OpenJTalkFrontend_refresh(OpenJTalkFrontend *frontend)
{
   if (frontend == NULL || frontend->initialized == 0)
      return;

   JPCommon_refresh(&frontend->jpcommon);
   NJD_refresh(&frontend->njd);
   Mecab_refresh(&frontend->mecab);
}

int OpenJTalkFrontend_make_labels(OpenJTalkFrontend *frontend, const char *text)
{
   char buff[OPENJTALK_FRONTEND_MAXBUFLEN];

   if (frontend == NULL || frontend->initialized == 0 || text == NULL)
      return 0;

   text2mecab(buff, text);
   Mecab_analysis(&frontend->mecab, buff);
   mecab2njd(&frontend->njd, Mecab_get_feature(&frontend->mecab),
             Mecab_get_size(&frontend->mecab));
   njd_set_pronunciation(&frontend->njd);
   njd_set_digit(&frontend->njd);
   njd_set_accent_phrase(&frontend->njd);
   njd_set_accent_type(&frontend->njd);
   njd_set_unvoiced_vowel(&frontend->njd);
   njd_set_long_vowel(&frontend->njd);
   njd2jpcommon(&frontend->jpcommon, &frontend->njd);
   JPCommon_make_label(&frontend->jpcommon);

   return JPCommon_get_label_size(&frontend->jpcommon) > 2 ? 1 : 0;
}

int OpenJTalkFrontend_get_label_size(OpenJTalkFrontend *frontend)
{
   if (frontend == NULL || frontend->initialized == 0)
      return 0;
   return JPCommon_get_label_size(&frontend->jpcommon);
}

char **OpenJTalkFrontend_get_label_feature(OpenJTalkFrontend *frontend)
{
   if (frontend == NULL || frontend->initialized == 0)
      return NULL;
   return JPCommon_get_label_feature(&frontend->jpcommon);
}

static int utf8_decode_one(const unsigned char *input, unsigned int *codepoint, size_t *length)
{
   if (input[0] < 0x80) {
      *codepoint = input[0];
      *length = 1;
      return 1;
   }
   if ((input[0] & 0xe0) == 0xc0 && input[1] != '\0' && (input[1] & 0xc0) == 0x80) {
      *codepoint = ((unsigned int) (input[0] & 0x1f) << 6) | (unsigned int) (input[1] & 0x3f);
      *length = 2;
      return 1;
   }
   if ((input[0] & 0xf0) == 0xe0 && input[1] != '\0' && input[2] != '\0' && (input[1] & 0xc0) == 0x80 && (input[2] & 0xc0) == 0x80) {
      *codepoint = ((unsigned int) (input[0] & 0x0f) << 12) |
         ((unsigned int) (input[1] & 0x3f) << 6) | (unsigned int) (input[2] & 0x3f);
      *length = 3;
      return 1;
   }
   if ((input[0] & 0xf8) == 0xf0 && input[1] != '\0' && input[2] != '\0' && input[3] != '\0' &&
       (input[1] & 0xc0) == 0x80 && (input[2] & 0xc0) == 0x80 && (input[3] & 0xc0) == 0x80) {
      *codepoint = ((unsigned int) (input[0] & 0x07) << 18) |
         ((unsigned int) (input[1] & 0x3f) << 12) |
         ((unsigned int) (input[2] & 0x3f) << 6) | (unsigned int) (input[3] & 0x3f);
      *length = 4;
      return 1;
   }
   return 0;
}

static size_t utf8_encode_one(unsigned int codepoint, char *output)
{
   if (codepoint < 0x80) {
      output[0] = (char) codepoint;
      return 1;
   }
   if (codepoint < 0x800) {
      output[0] = (char) (0xc0 | (codepoint >> 6));
      output[1] = (char) (0x80 | (codepoint & 0x3f));
      return 2;
   }
   if (codepoint < 0x10000) {
      output[0] = (char) (0xe0 | (codepoint >> 12));
      output[1] = (char) (0x80 | ((codepoint >> 6) & 0x3f));
      output[2] = (char) (0x80 | (codepoint & 0x3f));
      return 3;
   }
   output[0] = (char) (0xf0 | (codepoint >> 18));
   output[1] = (char) (0x80 | ((codepoint >> 12) & 0x3f));
   output[2] = (char) (0x80 | ((codepoint >> 6) & 0x3f));
   output[3] = (char) (0x80 | (codepoint & 0x3f));
   return 4;
}

static int append_text(char **buffer, size_t *length, size_t *capacity, const char *text, int hiragana)
{
   const unsigned char *p = (const unsigned char *) text;

   if (text == NULL)
      return 1;

   while (*p != '\0') {
      unsigned int codepoint;
      size_t input_length;
      char encoded[4];
      size_t encoded_length;

      if (!utf8_decode_one(p, &codepoint, &input_length)) {
         codepoint = *p;
         input_length = 1;
      }
      if (hiragana && codepoint >= 0x30a1 && codepoint <= 0x30f6)
         codepoint -= 0x60;
      encoded_length = utf8_encode_one(codepoint, encoded);

      if (*length + encoded_length + 1 > *capacity) {
         size_t new_capacity = *capacity * 2;
         char *new_buffer;

         while (*length + encoded_length + 1 > new_capacity)
            new_capacity *= 2;
         new_buffer = (char *) realloc(*buffer, new_capacity);
         if (new_buffer == NULL)
            return 0;
         *buffer = new_buffer;
         *capacity = new_capacity;
      }
      memcpy(*buffer + *length, encoded, encoded_length);
      *length += encoded_length;
      (*buffer)[*length] = '\0';
      p += input_length;
   }
   return 1;
}

char *OpenJTalkFrontend_get_kana(OpenJTalkFrontend *frontend, int hiragana)
{
   size_t capacity = 256;
   size_t length = 0;
   char *buffer;
   NJDNode *node;

   if (frontend == NULL || frontend->initialized == 0)
      return NULL;

   buffer = (char *) malloc(capacity);
   if (buffer == NULL)
      return NULL;
   buffer[0] = '\0';

   for (node = frontend->njd.head; node != NULL; node = node->next) {
      const char *read = NJDNode_get_read(node);
      const char *text = read;

      if (text == NULL || strcmp(text, "*") == 0)
         text = NJDNode_get_string(node);
      if (text != NULL && strcmp(text, "*") != 0 && !append_text(&buffer, &length, &capacity, text, hiragana)) {
         free(buffer);
         return NULL;
      }
   }

   return buffer;
}

void OpenJTalkFrontend_fprint_text_analysis(OpenJTalkFrontend *frontend, FILE *fp)
{
   if (frontend == NULL || frontend->initialized == 0 || fp == NULL)
      return;
   NJD_fprint(&frontend->njd, fp);
}
