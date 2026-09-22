#ifndef HTS_WALLOC_H
#define HTS_WALLOC_H

#include <stddef.h>

typedef struct HTS_WallocStats {
   size_t total_bytes;
   size_t managed_bytes;
   size_t used_bytes;
   size_t free_bytes;
   size_t max_free_block_bytes;
   size_t used_blocks;
   size_t free_blocks;
   size_t block_header_bytes;
   size_t used_block_header_bytes;
   size_t peak_used_bytes;
   size_t peak_used_blocks;
} HTS_WallocStats;

void wmem_init(void);
int hts_walloc_get_stats(HTS_WallocStats *stats);
void *safe_wcalloc(size_t size
#ifdef MPL_DEBUG
                   , const char *filename, int linenumber
#endif
);
void wfree(void *mem
#ifdef MPL_DEBUG
           , const char *filename, int linenumber
#endif
);
char *wstrdup(const char *string
#ifdef MPL_DEBUG
              , const char *filename, int linenumber
#endif
);

#endif
