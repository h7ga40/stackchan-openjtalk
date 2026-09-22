#ifndef EST_WALLOC_H
#define EST_WALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *safe_wcalloc(size_t size, const char *filename, int linenumber);
void wfree(void *mem, const char *filename, int linenumber);
char *wstrdup(const char *string, const char *filename, int linenumber);

#ifdef __cplusplus
}
#endif

#endif

