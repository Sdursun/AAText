/*
 * AAText - decides whether a Text() call is ours and draws it.
 *
 * Stage 2: no glyphs yet. Strings on RTG rastports are replaced by test
 * boxes of exactly the size the original Text() would cover, so that
 * detection, clipping and the colour pipeline can be checked visually.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/clip.h>
#include <graphics/view.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "render.h"
#include "cgx.h"
#include "debug.h"

/* Upper limit for one temporary pixel buffer (bytes). */
#define AA_MAX_BUFFER (512 * 1024)

extern struct IntuitionBase *IntuitionBase;

struct Library *CyberGfxBase;
UBYTE aa_TestMode = AA_TEST_BOX;

#ifdef DEBUG
enum
{
    R_DRAWN, R_DRAWMODE, R_NOBITMAP, R_DEPTH, R_NOTCGX, R_LUT8,
    R_NOVIEWPORT, R_COORDS, R_NOMEM, R_COUNT
};
static const char *const reason_names[R_COUNT] =
{
    "drawn by AAText", "COMPLEMENT/INVERSVID", "no bitmap/font",
    "depth < 15", "not a CGX bitmap", "LUT8 bitmap", "no viewport",
    "coords/size", "out of memory"
};
static ULONG reason_counts[R_COUNT];
#define FAIL(r) do { reason_counts[r]++; return FALSE; } while (0)
#define COUNT(r) (reason_counts[r]++)
#else
#define FAIL(r) return FALSE
#define COUNT(r) ((void)0)
#endif

BOOL aa_HasBltTemplateAlpha(void)
{
    return CyberGfxBase && CGX_HAS_LVO(CyberGfxBase, LVO_BltTemplateAlpha);
}

BOOL aa_HasWritePixelArrayAlpha(void)
{
    return CyberGfxBase && CGX_HAS_LVO(CyberGfxBase, LVO_WritePixelArrayAlpha);
}

#ifdef DEBUG
/* Log the first few rastports we could not map to a screen. */
static void LogNoViewPort(struct RastPort *rp)
{
    static ULONG logged;
    struct Task *me = FindTask(NULL);
    struct Layer *layer = rp->Layer;
    struct Screen *scr;

    if (logged >= 3)
        return;
    logged++;

    kprintf("AAText: no viewport: task=\"%s\" rp=%lx bm=%lx layer=%lx "
            "window=%lx\n",
            (ULONG)(me->tc_Node.ln_Name ? me->tc_Node.ln_Name : "?"),
            (ULONG)rp, (ULONG)rp->BitMap, (ULONG)layer,
            (ULONG)(layer ? layer->Window : NULL));
    if (layer)
        kprintf("AAText:   layer flags=%lx bounds=%ld,%ld-%ld,%ld "
                "layerinfo=%lx\n",
                (ULONG)layer->Flags, (LONG)layer->bounds.MinX,
                (LONG)layer->bounds.MinY, (LONG)layer->bounds.MaxX,
                (LONG)layer->bounds.MaxY, (ULONG)layer->LayerInfo);
    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen)
        kprintf("AAText:   screen \"%s\" bm=%lx rp.bm=%lx layerinfo=%lx "
                "barlayer=%lx\n",
                (ULONG)(scr->Title ? (char *)scr->Title : "?"),
                (ULONG)scr->RastPort.BitMap, (ULONG)&scr->BitMap,
                (ULONG)&scr->LayerInfo, (ULONG)scr->BarLayer);
}
#endif

/*
 * Find the ViewPort (for pen -> RGB) of the screen the rastport draws on.
 * Window rastports know their window; others (e.g. the screen title bar)
 * are matched by bitmap. The screen list is walked under Forbid() rather
 * than LockIBase(): our caller may hold layer locks, and taking the
 * Intuition lock here could deadlock against input.device.
 */
static struct ViewPort *FindViewPort(struct RastPort *rp)
{
    struct Layer *layer = rp->Layer;
    struct Screen *scr;
    struct ViewPort *vp = NULL;

    if (layer && layer->Window)
        return &((struct Window *)layer->Window)->WScreen->ViewPort;

    Forbid();
    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen)
    {
        if (scr->RastPort.BitMap == rp->BitMap)
        {
            vp = &scr->ViewPort;
            break;
        }
    }
    Permit();

    return vp;
}

/*
 * The CGX pixel array functions take unsigned 16 bit coordinates, and
 * without a layer nothing clips against the bitmap. Reject anything that
 * could write outside the bitmap.
 */
static BOOL CoordsOk(struct RastPort *rp, struct BitMap *bm,
                     LONG x, LONG y, LONG w, LONG h)
{
    if (x < 0 || y < 0 || x + w > 0x7FFF || y + h > 0x7FFF)
        return FALSE;

    if (!rp->Layer)
    {
        if (x + w > (LONG)GetBitMapAttr(bm, BMA_WIDTH) ||
            y + h > (LONG)GetBitMapAttr(bm, BMA_HEIGHT))
            return FALSE;
    }
    return TRUE;
}

/* RectFill() in the given pen without disturbing the rastport. */
static void FillRect(struct RastPort *rp, ULONG pen,
                     LONG x, LONG y, LONG w, LONG h)
{
    ULONG oldpen;
    UWORD *oldptrn;

    if (w <= 0 || h <= 0)
        return;

    oldpen = GetAPen(rp);
    oldptrn = rp->AreaPtrn;
    rp->AreaPtrn = NULL;        /* Text() never uses area patterns */
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
    SetAPen(rp, oldpen);
    rp->AreaPtrn = oldptrn;
}

/* Alpha of the test gradient at column i of w: transparent -> opaque. */
static inline ULONG GradientAlpha(LONG i, LONG w)
{
    return (ULONG)((i + 1) * 255 / w);
}

static BOOL DrawAlphaTemplate(struct RastPort *rp, LONG x, LONG y,
                              LONG w, LONG h)
{
    UBYTE *buf;
    LONG i;

    if (w * h > AA_MAX_BUFFER)
        return FALSE;
    buf = AllocVec(w * h, MEMF_ANY);
    if (!buf)
        return FALSE;

    for (i = 0; i < w; i++)
        buf[i] = GradientAlpha(i, w);
    for (i = 1; i < h; i++)
        CopyMem(buf, buf + i * w, w);

    cgx_BltTemplateAlpha(CyberGfxBase, buf, 0, w, rp, x, y, w, h);

    FreeVec(buf);
    return TRUE;
}

static BOOL DrawReadModifyWrite(struct RastPort *rp, struct ViewPort *vp,
                                LONG x, LONG y, LONG w, LONG h)
{
    ULONG rgb[3];
    LONG fr, fg, fb;
    UBYTE *buf, *p;
    LONG i, j;

    if (w * h * 3 > AA_MAX_BUFFER)
        return FALSE;
    buf = AllocVec(w * h * 3, MEMF_ANY);
    if (!buf)
        return FALSE;

    GetRGB32(vp->ColorMap, GetAPen(rp), 1, rgb);
    fr = rgb[0] >> 24;
    fg = rgb[1] >> 24;
    fb = rgb[2] >> 24;

    cgx_ReadPixelArray(CyberGfxBase, buf, 0, 0, w * 3, rp, x, y, w, h,
                       RECTFMT_RGB);

    p = buf;
    for (j = 0; j < h; j++)
    {
        for (i = 0; i < w; i++)
        {
            LONG a = GradientAlpha(i, w);

            p[0] += ((fr - p[0]) * a) / 255;
            p[1] += ((fg - p[1]) * a) / 255;
            p[2] += ((fb - p[2]) * a) / 255;
            p += 3;
        }
    }

    cgx_WritePixelArray(CyberGfxBase, buf, 0, 0, w * 3, rp, x, y, w, h,
                        RECTFMT_RGB);

    FreeVec(buf);
    return TRUE;
}

static BOOL DrawWritePixelArrayAlpha(struct RastPort *rp, struct ViewPort *vp,
                                     LONG x, LONG y, LONG w, LONG h)
{
    ULONG rgb[3];
    ULONG color;
    ULONG *buf;
    LONG i;

    if (w * h * 4 > AA_MAX_BUFFER)
        return FALSE;
    buf = AllocVec(w * h * 4, MEMF_ANY);
    if (!buf)
        return FALSE;

    GetRGB32(vp->ColorMap, GetAPen(rp), 1, rgb);
    color = ((rgb[0] >> 8) & 0xFF0000) | ((rgb[1] >> 16) & 0xFF00) |
            (rgb[2] >> 24);

    for (i = 0; i < w; i++)
        buf[i] = (GradientAlpha(i, w) << 24) | color;
    for (i = 1; i < h; i++)
        CopyMem(buf, buf + i * w, w * 4);

    cgx_WritePixelArrayAlpha(CyberGfxBase, buf, 0, 0, w * 4, rp, x, y, w, h,
                             0xFFFFFFFF);

    FreeVec(buf);
    return TRUE;
}

BOOL aa_RenderText(struct RastPort *rp, CONST_STRPTR string, WORD count)
{
    struct TextFont *tf = rp->Font;
    struct BitMap *bm = rp->BitMap;
    struct ViewPort *vp;
    UBYTE mode = aa_TestMode;
    LONG x, y, w, h;

    if (mode == AA_TEST_OFF || count <= 0 || !CyberGfxBase)
        return FALSE;

    if (rp->DrawMode & (COMPLEMENT | INVERSVID))
        FAIL(R_DRAWMODE);
    if (!bm || !tf)
        FAIL(R_NOBITMAP);
    if (GetBitMapAttr(bm, BMA_DEPTH) < 15)
        FAIL(R_DEPTH);
    if (!cgx_GetCyberMapAttr(CyberGfxBase, bm, CYBRMATTR_ISCYBERGFX))
        FAIL(R_NOTCGX);
    if (cgx_GetCyberMapAttr(CyberGfxBase, bm, CYBRMATTR_PIXFMT) == PIXFMT_LUT8)
        FAIL(R_LUT8);
    vp = FindViewPort(rp);
    if (!vp || !vp->ColorMap)
    {
#ifdef DEBUG
        LogNoViewPort(rp);
#endif
        FAIL(R_NOVIEWPORT);
    }

    /* Same cell the original Text() covers; cp_x advance == TextLength(). */
    w = TextLength(rp, string, count);
    h = tf->tf_YSize;
    x = rp->cp_x;
    y = rp->cp_y - tf->tf_Baseline;
    if (w <= 0 || h <= 0 || !CoordsOk(rp, bm, x, y, w, h))
        FAIL(R_COORDS);

    switch (mode)
    {
        case AA_TEST_BOX:
            if (rp->DrawMode & JAM2)
                FillRect(rp, (UBYTE)rp->BgPen, x, y, w, h);
            /* 1 pixel gap on the right so separate Text() calls are visible */
            FillRect(rp, GetAPen(rp), x, y, w > 1 ? w - 1 : w, h);
            break;

        case AA_TEST_ALPHA:
            if (w * h > AA_MAX_BUFFER)
                FAIL(R_COORDS);
            if (rp->DrawMode & JAM2)
                FillRect(rp, (UBYTE)rp->BgPen, x, y, w, h);
            if (!DrawAlphaTemplate(rp, x, y, w, h))
                FAIL(R_NOMEM);
            break;

        case AA_TEST_RPA:
            if (w * h * 3 > AA_MAX_BUFFER)
                FAIL(R_COORDS);
            if (rp->DrawMode & JAM2)
                FillRect(rp, (UBYTE)rp->BgPen, x, y, w, h);
            if (!DrawReadModifyWrite(rp, vp, x, y, w, h))
                FAIL(R_NOMEM);
            break;

        case AA_TEST_WPAA:
            if (w * h * 4 > AA_MAX_BUFFER)
                FAIL(R_COORDS);
            if (rp->DrawMode & JAM2)
                FillRect(rp, (UBYTE)rp->BgPen, x, y, w, h);
            if (!DrawWritePixelArrayAlpha(rp, vp, x, y, w, h))
                FAIL(R_NOMEM);
            break;
    }

    rp->cp_x += w;
    COUNT(R_DRAWN);
    return TRUE;
}

#ifdef DEBUG
void aa_PrintRenderStats(void)
{
    int i;

    kprintf("AAText: --- Text() decisions ---\n");
    for (i = 0; i < R_COUNT; i++)
        kprintf("AAText: %-22s %8ld\n", (ULONG)reason_names[i],
                reason_counts[i]);
}
#endif
