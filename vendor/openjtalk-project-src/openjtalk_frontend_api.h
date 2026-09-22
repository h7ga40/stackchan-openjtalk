#ifndef OPENJTALK_FRONTEND_API_H
#define OPENJTALK_FRONTEND_API_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenJTalkFrontend OpenJTalkFrontend;

OpenJTalkFrontend *OpenJTalkFrontend_create(void);
void OpenJTalkFrontend_destroy(OpenJTalkFrontend *frontend);

int OpenJTalkFrontend_load(OpenJTalkFrontend *frontend, const char *dictionary_path);
int OpenJTalkFrontend_load_with_user_dic(OpenJTalkFrontend *frontend, const char *dictionary_path,
                                         const char *user_dictionary_path);
void OpenJTalkFrontend_clear(OpenJTalkFrontend *frontend);
void OpenJTalkFrontend_refresh(OpenJTalkFrontend *frontend);

int OpenJTalkFrontend_make_labels(OpenJTalkFrontend *frontend, const char *text);
int OpenJTalkFrontend_get_label_size(OpenJTalkFrontend *frontend);
char **OpenJTalkFrontend_get_label_feature(OpenJTalkFrontend *frontend);
char *OpenJTalkFrontend_get_kana(OpenJTalkFrontend *frontend, int hiragana);
void OpenJTalkFrontend_fprint_text_analysis(OpenJTalkFrontend *frontend, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif
