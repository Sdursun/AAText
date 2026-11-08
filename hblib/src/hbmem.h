/*
 * hbmem.h - HarfBuzz's allocator on an exec memory pool (hbmem.c).
 * HarfBuzz is built with hb_malloc_impl etc. defined to these names.
 */
#ifndef HBMEM_H
#define HBMEM_H

#include <stddef.h>
#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL hbmem_Init(void);
void hbmem_Cleanup(void);

void *hb_malloc_impl(size_t size);
void *hb_calloc_impl(size_t n, size_t size);
void *hb_realloc_impl(void *block, size_t size);
void  hb_free_impl(void *block);

#ifdef __cplusplus
}
#endif

#endif
