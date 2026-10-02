#ifndef AATEXT_PATCH_H
#define AATEXT_PATCH_H

#include <exec/types.h>
#include <graphics/gfxbase.h>

/* Number of tasks currently executing inside our patch code. */
extern volatile LONG aa_UseCount;

/*
 * Install the Text() patch, and with measuring also TextLength(),
 * TextExtent() and TextFit() (real metrics mode). TRUE on success.
 */
BOOL aa_Install(struct GfxBase *gfx, BOOL measuring);

/*
 * Try to remove the patch. Returns TRUE if the original vector was
 * restored and no task is executing our code any more; the program may
 * then exit. Returns FALSE if another program has patched Text() on top
 * of us: the patch then stays in memory in pass-through mode.
 */
BOOL aa_Remove(void);

#endif
