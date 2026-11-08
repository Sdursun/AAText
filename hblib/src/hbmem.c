/*
 * hbmem.c - HarfBuzz's allocator (hb_malloc_impl and friends, see
 * hb.hh) on an exec memory pool, fast RAM preferred. Each block keeps its
 * size in front of it, like AllocVec(). Pools are not safe for several
 * tasks at once: the caller serialises (harfbuzz.library: one semaphore
 * around every call).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>
#include <stddef.h>

#include "hbmem.h"

#define HB_PUDDLE  (32 * 1024)

static APTR hb_pool;

BOOL hbmem_Init(void)
{
    if (!hb_pool)
        hb_pool = CreatePool(MEMF_ANY, HB_PUDDLE, HB_PUDDLE / 2);
    return hb_pool != NULL;
}

void hbmem_Cleanup(void)
{
    if (hb_pool)
        DeletePool(hb_pool);
    hb_pool = NULL;
}

void *hb_malloc_impl(size_t size)
{
    ULONG *p;

    if (!hb_pool || size > 0x7FFFFFF0UL)
        return NULL;
    p = AllocPooled(hb_pool, size + sizeof(ULONG));
    if (!p)
        return NULL;
    *p = size + sizeof(ULONG);
    return p + 1;
}

void *hb_calloc_impl(size_t n, size_t size)
{
    void *p;

    if (size && n > 0x7FFFFFF0UL / size)
        return NULL;
    p = hb_malloc_impl(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}

void hb_free_impl(void *block)
{
    ULONG *p;

    if (!block)
        return;
    p = (ULONG *)block - 1;
    FreePooled(hb_pool, p, *p);
}

void *hb_realloc_impl(void *block, size_t size)
{
    ULONG cur;
    void *nb;

    if (!block)
        return hb_malloc_impl(size);
    if (!size)
    {
        hb_free_impl(block);
        return NULL;
    }
    cur = ((ULONG *)block)[-1] - sizeof(ULONG);
    if (size <= cur && size + 64 > cur)
        return block;               /* small shrink: keep the block */
    nb = hb_malloc_impl(size);
    if (nb)
    {
        memcpy(nb, block, cur < size ? cur : size);
        hb_free_impl(block);
    }
    return nb;
}
