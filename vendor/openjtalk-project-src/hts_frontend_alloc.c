#include <stddef.h>

#include "hts_walloc.h"

void *_HTS_calloc(const size_t num, const size_t size, const char *filename, int linenumber)
{
   if (size != 0u && num > (size_t) -1 / size)
      return NULL;

   return safe_wcalloc(num * size
#ifdef MPL_DEBUG
                       , filename, linenumber
#endif
   );
}

void _HTS_free(void *p, const char *filename, int linenumber)
{
   wfree(p
#ifdef MPL_DEBUG
         , filename, linenumber
#endif
   );
}

char *_HTS_strdup(const char *string, const char *filename, int linenumber)
{
   return wstrdup(string
#ifdef MPL_DEBUG
                  , filename, linenumber
#endif
   );
}
