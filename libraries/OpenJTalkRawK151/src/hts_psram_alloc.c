#include "EST_walloc.h"

#include <esp_heap_caps.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef union AllocationAlignment {
    long long integer;
    double real;
    void *pointer;
} AllocationAlignment;

typedef struct AllocationHeader {
    size_t size;
    uint32_t magic;
    bool psram;
    AllocationAlignment alignment;
} AllocationHeader;

static const uint32_t kAllocationMagic = 0x48545332u;
static size_t current_bytes;
static size_t peak_bytes;
static size_t allocation_count;
static bool any_psram;

void openjtalk_raw_allocator_reset_stats(void)
{
    current_bytes = 0;
    peak_bytes = 0;
    allocation_count = 0;
    any_psram = false;
}

void openjtalk_raw_allocator_get_stats(size_t *current,
                                       size_t *peak,
                                       size_t *count,
                                       bool *used_psram)
{
    if (current != NULL) *current = current_bytes;
    if (peak != NULL) *peak = peak_bytes;
    if (count != NULL) *count = allocation_count;
    if (used_psram != NULL) *used_psram = any_psram;
}

void *safe_wcalloc(size_t size, const char *filename, int linenumber)
{
    (void) filename;
    (void) linenumber;
    if (size == 0 || size > SIZE_MAX - sizeof(AllocationHeader)) return NULL;

    const size_t total = sizeof(AllocationHeader) + size;
    AllocationHeader *header = (AllocationHeader *) heap_caps_calloc(
        1, total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool in_psram = true;
    if (header == NULL) {
        header = (AllocationHeader *) heap_caps_calloc(1, total, MALLOC_CAP_8BIT);
        in_psram = false;
    }
    if (header == NULL) return NULL;

    header->size = size;
    header->magic = kAllocationMagic;
    header->psram = in_psram;
    current_bytes += size;
    if (current_bytes > peak_bytes) peak_bytes = current_bytes;
    allocation_count++;
    any_psram = any_psram || in_psram;
    return header + 1;
}

void wfree(void *mem, const char *filename, int linenumber)
{
    (void) filename;
    (void) linenumber;
    if (mem == NULL) return;
    AllocationHeader *header = ((AllocationHeader *) mem) - 1;
    if (header->magic != kAllocationMagic) return;
    header->magic = 0;
    if (current_bytes >= header->size) current_bytes -= header->size;
    heap_caps_free(header);
}

char *wstrdup(const char *string, const char *filename, int linenumber)
{
    if (string == NULL) return NULL;
    const size_t size = strlen(string) + 1;
    char *copy = (char *) safe_wcalloc(size, filename, linenumber);
    if (copy != NULL) memcpy(copy, string, size);
    return copy;
}

