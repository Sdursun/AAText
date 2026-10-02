/*
 * AAText FreeType system interface.
 *
 * Memory comes from an exec memory pool (fast RAM preferred). Pools are
 * not task safe, but AAText only calls FreeType while holding its render
 * semaphore, so no extra locking is done here.
 * Stream support is disabled: fonts are loaded into memory by AAText and
 * opened with FT_New_Memory_Face().
 */

#include <ft2build.h>
#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

#define AA_FT_PUDDLE  (32 * 1024)

/* Each block stores its size in front, like AllocVec(). */
static FT_Pointer ft_alloc(FT_Memory memory, long size)
{
    ULONG *p = AllocPooled(memory->user, size + sizeof(ULONG));

    if (!p)
        return NULL;
    *p = size + sizeof(ULONG);
    return p + 1;
}

static void ft_free(FT_Memory memory, FT_Pointer block)
{
    ULONG *p;

    if (!block)
        return;
    p = (ULONG *)block - 1;
    FreePooled(memory->user, p, *p);
}

static FT_Pointer ft_realloc(FT_Memory memory, long cur_size, long new_size,
                             FT_Pointer block)
{
    FT_Pointer nb = ft_alloc(memory, new_size);

    if (nb && block)
    {
        CopyMem(block, nb, cur_size < new_size ? cur_size : new_size);
        ft_free(memory, block);
    }
    return nb;
}

FT_BASE_DEF(FT_Memory) FT_New_Memory(void)
{
    FT_Memory memory = AllocVec(sizeof(*memory), MEMF_ANY | MEMF_CLEAR);

    if (!memory)
        return NULL;
    memory->user = CreatePool(MEMF_ANY, AA_FT_PUDDLE, AA_FT_PUDDLE / 2);
    if (!memory->user)
    {
        FreeVec(memory);
        return NULL;
    }
    memory->alloc = ft_alloc;
    memory->realloc = ft_realloc;
    memory->free = ft_free;
    return memory;
}

FT_BASE_DEF(void) FT_Done_Memory(FT_Memory memory)
{
    if (!memory)
        return;
    DeletePool(memory->user);
    FreeVec(memory);
}
