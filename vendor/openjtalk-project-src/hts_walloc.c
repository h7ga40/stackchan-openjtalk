#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define Inline static
#include "queue.h"
#include "hts_walloc.h"

#ifdef MPL_DEBUG
#include <windows.h>
#endif

#ifdef _MSC_VER
#define HTS_ALIGNOF(type) __alignof(type)
#else
#define HTS_ALIGNOF(type) _Alignof(type)
#endif

typedef struct hts_memory_control_block {
   QUEUE queue;
   uint32_t magic;
   bool used;
   size_t size;
#ifdef MPL_DEBUG
   const char *filename;
   int linenumber;
   const char *free_filename;
   int free_linenumber;
#endif
} HTSMEMB;

typedef union hts_alignment_probe {
   long l;
#ifdef _MSC_VER
   __int64 i64;
#endif
#if !defined(_MSC_VER) || defined(__cplusplus)
   long long ll;
#endif
} HTS_ALIGNMENT_PROBE;

#define HTSMEM_MAGIC 0xCAFEBABEu
#define HTSMEM_DELTA 40u
#define HTSMEM_SIZE (16u * 1024u * 1024u)

static uint8_t htsmem[HTSMEM_SIZE];
static HTSMEMB *p_htsmemb;
static size_t htsmem_current_used_bytes;
static size_t htsmem_current_used_blocks;
static size_t htsmem_peak_used_bytes;
static size_t htsmem_peak_used_blocks;

static size_t align_block_size(size_t size)
{
   const size_t align = HTS_ALIGNOF(HTS_ALIGNMENT_PROBE);
   return (size + sizeof(HTSMEMB) + align - 1u) & ~(align - 1u);
}

static bool htsmem_setup(void *buf, size_t bufsz, HTSMEMB **out_htsmemb)
{
   HTSMEMB *top;
   HTSMEMB *htsmemb;

   if (bufsz < sizeof(HTSMEMB) * 2u ||
       (size_t) buf % HTS_ALIGNOF(HTS_ALIGNMENT_PROBE) != 0u) {
      return false;
   }

   top = (HTSMEMB *) buf;
   top->magic = 0u;
   top->used = true;
   top->size = 0u;
#ifdef MPL_DEBUG
   top->filename = __FILE__;
   top->linenumber = __LINE__;
   top->free_filename = NULL;
   top->free_linenumber = 0;
#endif
   queue_initialize(&top->queue);

   htsmemb = top + 1;
   htsmemb->magic = HTSMEM_MAGIC;
   htsmemb->used = false;
   htsmemb->size = bufsz - sizeof(HTSMEMB);
#ifdef MPL_DEBUG
   htsmemb->filename = NULL;
   htsmemb->linenumber = 0;
   htsmemb->free_filename = NULL;
   htsmemb->free_linenumber = 0;
#endif
   queue_insert_prev((QUEUE *) top, (QUEUE *) htsmemb);

   *out_htsmemb = top;
   return true;
}

static bool htsmem_allocate(HTSMEMB *htsmemb, size_t size, void **out_block
#ifdef MPL_DEBUG
                            , const char *filename, int linenumber
#endif
)
{
   HTSMEMB *ptr;

   size = align_block_size(size);

   for (ptr = (HTSMEMB *) htsmemb->queue.p_next; ptr != htsmemb;
        ptr = (HTSMEMB *) ptr->queue.p_next) {
      if (ptr->magic != HTSMEM_MAGIC)
         return false;

      if (!ptr->used && ptr->size >= size) {
         if (ptr->size - size > HTSMEM_DELTA) {
            HTSMEMB *split = (HTSMEMB *) ((uint8_t *) ptr + size);
            split->size = ptr->size - size;
            split->magic = HTSMEM_MAGIC;
            split->used = false;
#ifdef MPL_DEBUG
            split->filename = NULL;
            split->linenumber = 0;
            split->free_filename = NULL;
            split->free_linenumber = 0;
#endif

            ptr->size = size;
            ptr->used = true;
#ifdef MPL_DEBUG
            ptr->filename = filename;
            ptr->linenumber = linenumber;
#endif
            queue_insert_prev((QUEUE *) ptr, (QUEUE *) split);
         } else {
            ptr->used = true;
#ifdef MPL_DEBUG
            ptr->filename = filename;
            ptr->linenumber = linenumber;
#endif
         }

         *out_block = (void *) (ptr + 1);
         return true;
      }
   }

   return false;
}

static bool htsmem_release(HTSMEMB *htsmemb, void *buf
#ifdef MPL_DEBUG
                           , const char *filename, int linenumber
#endif
)
{
   HTSMEMB *hdr;
   HTSMEMB *neighbor;

   if ((HTSMEMB *) buf < htsmemb) {
#ifdef MPL_DEBUG
      DebugBreak();
#endif
      return false;
   }

   hdr = (HTSMEMB *) buf - 1;
   if (hdr->magic != HTSMEM_MAGIC) {
#ifdef MPL_DEBUG
      DebugBreak();
#endif
      return false;
   }

   hdr->used = false;
#ifdef MPL_DEBUG
   hdr->free_filename = filename;
   hdr->free_linenumber = linenumber;
#endif

   neighbor = (HTSMEMB *) hdr->queue.p_prev;
   if (!neighbor->used) {
      hdr->size += neighbor->size;
      neighbor->magic = 0u;
      neighbor->queue.p_next->p_prev = neighbor->queue.p_prev;
      neighbor->queue.p_prev->p_next = neighbor->queue.p_next;
   }

   neighbor = (HTSMEMB *) hdr->queue.p_next;
   if (!neighbor->used) {
      neighbor->size += hdr->size;
      hdr->magic = 0u;
      hdr->queue.p_next->p_prev = hdr->queue.p_prev;
      hdr->queue.p_prev->p_next = hdr->queue.p_next;
   }

   return true;
}

void wmem_init(void)
{
   htsmem_setup(htsmem, sizeof(htsmem), &p_htsmemb);
   htsmem_current_used_bytes = 0u;
   htsmem_current_used_blocks = 0u;
   htsmem_peak_used_bytes = 0u;
   htsmem_peak_used_blocks = 0u;
}

int hts_walloc_get_stats(HTS_WallocStats *stats)
{
   QUEUE *entry = NULL;

   if (stats == NULL)
      return 0;

   memset(stats, 0, sizeof(*stats));
   stats->total_bytes = sizeof(htsmem);
   stats->block_header_bytes = sizeof(HTSMEMB);

   if (p_htsmemb == NULL)
      return 1;

   while ((entry = queue_enumerate((QUEUE *) p_htsmemb, entry)) != NULL) {
      HTSMEMB *block = (HTSMEMB *) entry;

      if (block->magic != HTSMEM_MAGIC)
         return 0;

      stats->managed_bytes += block->size;
      if (block->used) {
         stats->used_bytes += block->size;
         stats->used_blocks++;
      } else {
         stats->free_bytes += block->size;
         stats->free_blocks++;
         if (stats->max_free_block_bytes < block->size)
            stats->max_free_block_bytes = block->size;
      }
   }

   stats->used_block_header_bytes = stats->used_blocks * stats->block_header_bytes;
   stats->peak_used_bytes = htsmem_peak_used_bytes;
   stats->peak_used_blocks = htsmem_peak_used_blocks;
   return 1;
}

void *safe_wcalloc(size_t size
#ifdef MPL_DEBUG
                   , const char *filename, int linenumber
#endif
)
{
   void *result = NULL;

   if (p_htsmemb == NULL)
      wmem_init();

   if (!htsmem_allocate(p_htsmemb, size, &result
#ifdef MPL_DEBUG
                        , filename, linenumber
#endif
   )) {
      return NULL;
   }

   memset(result, 0, size);
   {
      HTSMEMB *hdr = (HTSMEMB *) result - 1;
      htsmem_current_used_bytes += hdr->size;
      htsmem_current_used_blocks++;
      if (htsmem_peak_used_bytes < htsmem_current_used_bytes)
         htsmem_peak_used_bytes = htsmem_current_used_bytes;
      if (htsmem_peak_used_blocks < htsmem_current_used_blocks)
         htsmem_peak_used_blocks = htsmem_current_used_blocks;
   }
   return result;
}

void wfree(void *mem
#ifdef MPL_DEBUG
           , const char *filename, int linenumber
#endif
)
{
   if (mem == NULL)
      return;

   {
      HTSMEMB *hdr = (HTSMEMB *) mem - 1;
      if (hdr->magic == HTSMEM_MAGIC && hdr->used) {
         htsmem_current_used_bytes -= hdr->size;
         htsmem_current_used_blocks--;
      }
   }

   htsmem_release(p_htsmemb, mem
#ifdef MPL_DEBUG
                  , filename, linenumber
#endif
   );
}

char *wstrdup(const char *string
#ifdef MPL_DEBUG
              , const char *filename, int linenumber
#endif
)
{
   char *buffer;
   size_t size;

   if (string == NULL)
      return NULL;

   size = strlen(string) + 1u;
   buffer = (char *) safe_wcalloc(size
#ifdef MPL_DEBUG
                                  , filename, linenumber
#endif
   );
   if (buffer == NULL)
      return NULL;

   memcpy(buffer, string, size);
   return buffer;
}
