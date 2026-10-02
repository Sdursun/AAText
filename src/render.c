/*
 * AAText - decides whether a Text() call is ours and draws it.
 *
 * Drawing uses read-modify-write: the background under the text cell is
 * read with ReadPixelArray(), glyph coverage is blended in with the pen
 * colour, and the result is written back with WritePixelArray(). Both
 * functions clip against layers (verified on P96 in stage 2).
 *
 * Safe metrics mode: every character keeps the cell of the original
 * bitmap font, so layouts never change and cp_x advances by TextLength().
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
#include "glyphs.h"
#include "cgx.h"
#include "debug.h"

/* Upper limit for one temporary pixel buffer (bytes). */
#define AA_MAX_BUFFER   (512 * 1024)

/* Preallocated buffers, so short strings need no AllocVec(). */
#define AA_NUM_BUFFERS  4
#define AA_BUFFER_SIZE  (32 * 1024)

extern struct IntuitionBase *IntuitionBase;

struct Library *CyberGfxBase;
UBYTE aa_Mode = AA_MODE_TEXT;

static UBYTE *aa_Buffers[AA_NUM_BUFFERS];
static volatile BOOL aa_BufferBusy[AA_NUM_BUFFERS];

#ifdef DEBUG
enum
{
    R_DRAWN, R_DRAWMODE, R_STYLE, R_NOBITMAP, R_DEPTH, R_NOTCGX, R_LUT8,
    R_NOMAPPING, R_NOVIEWPORT, R_COORDS, R_NOMEM, R_FONTFAIL, R_COUNT
};
static const char *const reason_names[R_COUNT] =
{
    "drawn by AAText", "COMPLEMENT/INVERSVID", "bold/italic/underl.",
    "no bitmap/font", "depth < 15", "not a CGX bitmap", "LUT8 bitmap",
    "font not mapped", "no viewport", "coords/size", "out of memory",
    "font setup failed"
};
static ULONG reason_counts[R_COUNT];
#define FAIL(r) do { reason_counts[r]++; return FALSE; } while (0)
#define COUNT(r) (reason_counts[r]++)
#else
#define FAIL(r) return FALSE
#define COUNT(r) ((void)0)
#endif

BOOL aa_RenderInit(void)
{
    LONG i;

    for (i = 0; i < AA_NUM_BUFFERS; i++)
    {
        aa_Buffers[i] = AllocVec(AA_BUFFER_SIZE, MEMF_ANY);
        if (!aa_Buffers[i])
            return FALSE;
    }
    return TRUE;
}

void aa_RenderCleanup(void)
{
    LONG i;

    for (i = 0; i < AA_NUM_BUFFERS; i++)
    {
        if (aa_Buffers[i])
            FreeVec(aa_Buffers[i]);
        aa_Buffers[i] = NULL;
    }
}

/*
 * Get a pixel buffer. Never waits: if all preallocated buffers are in
 * use, falls back to AllocVec(). Waiting here could deadlock, since our
 * caller may hold layer locks.
 */
static UBYTE *GetBuffer(ULONG size)
{
    LONG i;

    if (size <= AA_BUFFER_SIZE)
    {
        Forbid();
        for (i = 0; i < AA_NUM_BUFFERS; i++)
        {
            if (!aa_BufferBusy[i])
            {
                aa_BufferBusy[i] = TRUE;
                Permit();
                return aa_Buffers[i];
            }
        }
        Permit();
    }
    return AllocVec(size, MEMF_ANY);
}

static void FreeBuffer(UBYTE *buf)
{
    LONG i;

    for (i = 0; i < AA_NUM_BUFFERS; i++)
    {
        if (buf == aa_Buffers[i])
        {
            aa_BufferBusy[i] = FALSE;
            return;
        }
    }
    FreeVec(buf);
}

#ifdef DEBUG
/* Log each unmapped font once, so the user can see the exact names. */
static void LogUnmappedFont(struct TextFont *tf)
{
    static struct TextFont *seen[32];
    LONG i;

    Forbid();
    for (i = 0; i < 32 && seen[i]; i++)
    {
        if (seen[i] == tf)
        {
            Permit();
            return;
        }
    }
    if (i < 32)
        seen[i] = tf;
    Permit();
    if (i == 32)
        return;

    kprintf("AAText: unmapped font \"%s\" size %ld\n",
            (ULONG)(tf->tf_Message.mn_Node.ln_Name ?
                    tf->tf_Message.mn_Node.ln_Name : "?"),
            (LONG)tf->tf_YSize);
}

/* Log the first few rastports we could not map to a screen. */
static void LogNoViewPort(struct RastPort *rp)
{
    static ULONG logged;
    struct Task *me = FindTask(NULL);
    struct Layer *layer = rp->Layer;

    if (logged >= 3)
        return;
    logged++;

    kprintf("AAText: no viewport: task=\"%s\" rp=%lx bm=%lx layer=%lx\n",
            (ULONG)(me->tc_Node.ln_Name ? me->tc_Node.ln_Name : "?"),
            (ULONG)rp, (ULONG)rp->BitMap, (ULONG)layer);
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

static void PenToRGB(struct ViewPort *vp, ULONG pen, UBYTE *rgb)
{
    ULONG c[3];

    GetRGB32(vp->ColorMap, pen, 1, c);
    rgb[0] = c[0] >> 24;
    rgb[1] = c[1] >> 24;
    rgb[2] = c[2] >> 24;
}

static void FillBuffer(UBYTE *buf, LONG pixels, const UBYTE *rgb)
{
    UBYTE r = rgb[0], g = rgb[1], b = rgb[2];

    while (pixels--)
    {
        *buf++ = r;
        *buf++ = g;
        *buf++ = b;
    }
}

/*
 * Blend one glyph into the RGB buffer (w x h). gx/gy is the top left of
 * the glyph bitmap relative to the buffer; pixels outside are clipped.
 */
static void BlendGlyph(UBYTE *buf, LONG w, LONG h, const struct AAGlyph *g,
                       LONG gx, LONG gy, const UBYTE *fg)
{
    LONG x0 = 0, y0 = 0, x1 = g->width, y1 = g->rows;
    LONG x, y;
    LONG fr = fg[0], fgc = fg[1], fb = fg[2];

    if (gx < 0)
        x0 = -gx;
    if (gy < 0)
        y0 = -gy;
    if (gx + x1 > w)
        x1 = w - gx;
    if (gy + y1 > h)
        y1 = h - gy;

    for (y = y0; y < y1; y++)
    {
        const UBYTE *src = g->data + y * g->width + x0;
        UBYTE *dst = buf + ((gy + y) * w + gx + x0) * 3;

        for (x = x0; x < x1; x++, dst += 3)
        {
            ULONG a = *src++;

            if (a == 0)
                continue;
            if (a == 255)
            {
                dst[0] = fr;
                dst[1] = fgc;
                dst[2] = fb;
                continue;
            }
            a += a >> 7;            /* 0..255 -> 0..256 */
            dst[0] += ((fr  - dst[0]) * (LONG)a) >> 8;
            dst[1] += ((fgc - dst[1]) * (LONG)a) >> 8;
            dst[2] += ((fb  - dst[2]) * (LONG)a) >> 8;
        }
    }
}

/*
 * Draw the string into the buffer, character by character, keeping the
 * cells of the bitmap font. Called with the glyph lock held; calls no
 * graphics functions.
 */
static void DrawString(UBYTE *buf, LONG w, LONG h, struct RastPort *rp,
                       struct AAFont *font, CONST_STRPTR s, LONG count,
                       const UBYTE *fg)
{
    struct TextFont *tf = rp->Font;
    WORD *kern = (WORD *)tf->tf_CharKern;
    WORD *space = (WORD *)tf->tf_CharSpace;
    LONG defidx = tf->tf_HiChar - tf->tf_LoChar + 1;  /* "not in font" glyph */
    LONG pen = 0;
    LONG baseline = tf->tf_Baseline;

    while (count--)
    {
        UBYTE c = *s++;
        LONG idx = (c < tf->tf_LoChar || c > tf->tf_HiChar) ?
                   defidx : c - tf->tf_LoChar;
        LONG k = kern ? kern[idx] : 0;
        LONG cell = k + (space ? space[idx] : tf->tf_XSize);
        struct AAGlyph *g = aa_GetGlyph(font, c);

        if (g && g->width)
        {
            /* centre the TrueType advance in the original cell */
            LONG gx = pen + g->left + (cell - g->advance) / 2;
            LONG gy = baseline - g->top;

            BlendGlyph(buf, w, h, g, gx, gy, fg);
        }
        pen += cell + rp->TxSpacing;
    }
}

/* Debug: gradient from transparent to the pen colour. */
static void DrawGradient(UBYTE *buf, LONG w, LONG h, const UBYTE *fg)
{
    LONG i, j;

    for (j = 0; j < h; j++)
    {
        UBYTE *p = buf + j * w * 3;

        for (i = 0; i < w; i++, p += 3)
        {
            LONG a = (i + 1) * 256 / w;

            p[0] += ((fg[0] - p[0]) * a) >> 8;
            p[1] += ((fg[1] - p[1]) * a) >> 8;
            p[2] += ((fg[2] - p[2]) * a) >> 8;
        }
    }
}

BOOL aa_RenderText(struct RastPort *rp, CONST_STRPTR string, WORD count)
{
    struct TextFont *tf = rp->Font;
    struct BitMap *bm = rp->BitMap;
    struct AAFont *font = NULL;
    struct ViewPort *vp;
    UBYTE mode = aa_Mode;
    UBYTE fg[3], bg[3];
    UBYTE *buf;
    LONG x, y, w, h;

    if (mode == AA_MODE_OFF || count <= 0 || !CyberGfxBase)
        return FALSE;

    if (rp->DrawMode & (COMPLEMENT | INVERSVID))
        FAIL(R_DRAWMODE);
    if (rp->AlgoStyle & (FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED))
        FAIL(R_STYLE);
    if (!bm || !tf)
        FAIL(R_NOBITMAP);
    if (mode == AA_MODE_TEXT && !(font = aa_FindFont(tf)))
    {
#ifdef DEBUG
        LogUnmappedFont(tf);
#endif
        FAIL(R_NOMAPPING);
    }
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
    if (w <= 0 || h <= 0 || w * h * 3 > AA_MAX_BUFFER ||
        !CoordsOk(rp, bm, x, y, w, h))
        FAIL(R_COORDS);

    if (mode == AA_MODE_BOX)
    {
        if (rp->DrawMode & JAM2)
            FillRect(rp, (UBYTE)rp->BgPen, x, y, w, h);
        /* 1 pixel gap on the right so separate Text() calls are visible */
        FillRect(rp, GetAPen(rp), x, y, w > 1 ? w - 1 : w, h);
        rp->cp_x += w;
        COUNT(R_DRAWN);
        return TRUE;
    }

    if (font)
    {
        BOOL ok;

        aa_LockGlyphs();
        ok = aa_PrepareFont(font, tf);
        aa_UnlockGlyphs();
        if (!ok)
            FAIL(R_FONTFAIL);
    }

    buf = GetBuffer(w * h * 3);
    if (!buf)
        FAIL(R_NOMEM);

    PenToRGB(vp, GetAPen(rp), fg);

    /* JAM2: background is the BgPen colour, no need to read it. */
    if (rp->DrawMode & JAM2)
    {
        PenToRGB(vp, GetBPen(rp), bg);
        FillBuffer(buf, w * h, bg);
    }
    else
        cgx_ReadPixelArray(CyberGfxBase, buf, 0, 0, w * 3, rp, x, y, w, h,
                           RECTFMT_RGB);

    if (font)
    {
        aa_LockGlyphs();
        DrawString(buf, w, h, rp, font, string, count, fg);
        aa_UnlockGlyphs();
    }
    else
        DrawGradient(buf, w, h, fg);

    cgx_WritePixelArray(CyberGfxBase, buf, 0, 0, w * 3, rp, x, y, w, h,
                        RECTFMT_RGB);
    FreeBuffer(buf);

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
    aa_PrintGlyphStats();
}
#endif
