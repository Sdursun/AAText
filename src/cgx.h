#ifndef AATEXT_CGX_H
#define AATEXT_CGX_H

/*
 * Minimal cybergraphics.library interface.
 * The toolchain image has no CyberGraphX headers, so the few constants
 * and functions we need are defined here. Values and LVOs were checked
 * against the AROS sources (workbench/libs/cgfx, cybergraphx/cybergraphics.h).
 *
 * Calls go through small assembly wrappers in cgx.s (stack arguments,
 * all 32 bit), library base passed explicitly.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>

#define CYBRMATTR_XMOD        0x80000001UL
#define CYBRMATTR_BPPIX       0x80000002UL
#define CYBRMATTR_PIXFMT      0x80000004UL
#define CYBRMATTR_WIDTH       0x80000005UL
#define CYBRMATTR_HEIGHT      0x80000006UL
#define CYBRMATTR_DEPTH       0x80000007UL
#define CYBRMATTR_ISCYBERGFX  0x80000008UL

#define PIXFMT_LUT8           0UL

#define RECTFMT_RGB           0UL
#define RECTFMT_RGBA          1UL
#define RECTFMT_ARGB          2UL
#define RECTFMT_GREY8         4UL

/* Library vector offsets */
#define LVO_GetCyberMapAttr       (-96)
#define LVO_ReadPixelArray        (-120)
#define LVO_WritePixelArray       (-126)

ULONG cgx_GetCyberMapAttr(struct Library *base, struct BitMap *bm, ULONG attr);

ULONG cgx_ReadPixelArray(struct Library *base, APTR dst, ULONG dstx, ULONG dsty,
                         ULONG dstmod, struct RastPort *rp, ULONG srcx,
                         ULONG srcy, ULONG width, ULONG height, ULONG dstfmt);

ULONG cgx_WritePixelArray(struct Library *base, APTR src, ULONG srcx, ULONG srcy,
                          ULONG srcmod, struct RastPort *rp, ULONG dstx,
                          ULONG dsty, ULONG width, ULONG height, ULONG srcfmt);


#endif
