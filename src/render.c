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
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#ifdef DEBUG
#include <devices/timer.h>
#include <proto/timer.h>
#endif

#include "render.h"
#include "prefs.h"
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

/* Copied from the prefs at init; read-only afterwards. */
static char aa_Blacklist[AA_MAX_BLACKLIST][AA_NAME_LEN];
static LONG aa_NumBlack;
static BOOL aa_Offscreen;

#ifdef DEBUG
enum
{
    R_DRAWN, R_BLACKLIST, R_DRAWMODE, R_STYLE, R_NOBITMAP, R_DEPTH,
    R_NOTCGX, R_LUT8, R_NOMAPPING, R_NOVIEWPORT, R_COORDS, R_NOMEM,
    R_FONTFAIL, R_COUNT
};
static const char *const reason_names[R_COUNT] =
{
    "drawn by AAText", "blacklisted task", "COMPLEMENT/INVERSVID",
    "bold/italic/underl.", "no bitmap/font", "depth < 15",
    "not a CGX bitmap", "LUT8 bitmap", "font not mapped", "no viewport",
    "coords/size", "out of memory", "font setup failed"
};
static ULONG reason_counts[R_COUNT];
#define FAIL(r) do { reason_counts[r]++; return FALSE; } while (0)
#define COUNT(r) (reason_counts[r]++)

/* Timing of drawn strings, measured with the E-clock. */
struct Device *TimerBase;
static struct timerequest aa_TimerReq;
static BOOL aa_TimerOpen;
static ULONG aa_EFreq;
static ULONG stat_ticks, stat_maxticks, stat_chars, stat_offscreen;
static ULONG stat_widthmismatch;
#else
#define FAIL(r) return FALSE
#define COUNT(r) ((void)0)
#endif

BOOL aa_RenderInit(const struct AAPrefs *prefs)
{
    LONG i;

    for (i = 0; i < prefs->numblack; i++)
    {
        CopyMem((APTR)prefs->blacklist[i], aa_Blacklist[i], AA_NAME_LEN);
        D(("AAText: blacklisted \"%s\"\n", (ULONG)aa_Blacklist[i]));
    }
    aa_NumBlack = prefs->numblack;
    aa_Offscreen = prefs->offscreen;

#ifdef DEBUG
    if (!OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                    (struct IORequest *)&aa_TimerReq, 0))
    {
        struct EClockVal ev;

        aa_TimerOpen = TRUE;
        TimerBase = aa_TimerReq.tr_node.io_Device;
        aa_EFreq = ReadEClock(&ev);
    }
#endif

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
#ifdef DEBUG
    if (aa_TimerOpen)
        CloseDevice((struct IORequest *)&aa_TimerReq);
    aa_TimerOpen = FALSE;
#endif
}

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Is the name (len chars, not necessarily terminated) in the blacklist? */
static BOOL InBlacklist(const char *name, LONG len)
{
    LONG i, j;

    for (i = 0; i < aa_NumBlack; i++)
    {
        const char *b = aa_Blacklist[i];

        for (j = 0; j < len && b[j] && ToLower(b[j]) == ToLower(name[j]); j++)
            ;
        if (j == len && !b[j])
            return TRUE;
    }
    return FALSE;
}

/*
 * Programs started from Workbench run as a process named after the
 * program. Shell commands run in the Shell's process ("Background CLI",
 * "Shell Process"), so the running command name is checked as well.
 * Only memory is read here, no DOS calls.
 */
static BOOL IsBlacklisted(struct Task *me)
{
    const char *name = me->tc_Node.ln_Name;

    if (name)
    {
        LONG len = 0;

        while (name[len])
            len++;
        if (InBlacklist(name, len))
            return TRUE;
    }

    if (me->tc_Node.ln_Type == NT_PROCESS)
    {
        struct CommandLineInterface *cli =
            BADDR(((struct Process *)me)->pr_CLI);

        if (cli && cli->cli_Module && cli->cli_CommandName)
        {
            const UBYTE *bstr = BADDR(cli->cli_CommandName);
            const char *cmd = (const char *)bstr + 1;
            LONG len = bstr[0], i;

            /* FilePart(): skip "dir/" and "volume:" */
            for (i = len - 1; i >= 0; i--)
            {
                if (cmd[i] == '/' || cmd[i] == ':')
                {
                    cmd += i + 1;
                    len -= i + 1;
                    break;
                }
            }
            if (len > 0 && InBlacklist(cmd, len))
                return TRUE;
        }
    }
    return FALSE;
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
    struct ViewPort *vp = NULL, *wbvp = NULL;

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
        if ((scr->Flags & SCREENTYPE) == WBENCHSCREEN)
            wbvp = &scr->ViewPort;
    }
    Permit();

    /* "offscreen on": bitmaps of no screen use the Workbench colours */
    if (!vp && aa_Offscreen && wbvp)
    {
#ifdef DEBUG
        stat_offscreen++;
#endif
        vp = wbvp;
    }
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

/*
 * Width of the string as Text() advances cp_x, i.e. TextLength(), using
 * the same per-character cells as DrawString().
 */
static LONG CalcWidth(struct RastPort *rp, CONST_STRPTR s, LONG count)
{
    struct TextFont *tf = rp->Font;
    WORD *kern = (WORD *)tf->tf_CharKern;
    WORD *space = (WORD *)tf->tf_CharSpace;
    LONG defidx = tf->tf_HiChar - tf->tf_LoChar + 1;
    LONG w = 0;

    if (!kern && !space)
        return count * (tf->tf_XSize + rp->TxSpacing);

    while (count--)
    {
        UBYTE c = *s++;
        LONG idx = (c < tf->tf_LoChar || c > tf->tf_HiChar) ?
                   defidx : c - tf->tf_LoChar;

        w += (kern ? kern[idx] : 0) + (space ? space[idx] : tf->tf_XSize) +
             rp->TxSpacing;
    }
    return w;
}

#ifdef DEBUG
/*
 * Verify CalcWidth() against the system's TextLength(). On mismatch the
 * system value is used (so the layout stays right) and logged.
 */
static void CheckWidth(struct RastPort *rp, CONST_STRPTR s, LONG count,
                       LONG *w)
{
    LONG tl = TextLength(rp, s, count);

    if (tl == *w)
        return;
    if (stat_widthmismatch++ < 5)
        kprintf("AAText: width mismatch font \"%s\" %ld: ours=%ld "
                "TextLength=%ld count=%ld\n",
                (ULONG)rp->Font->tf_Message.mn_Node.ln_Name,
                (LONG)rp->Font->tf_YSize, *w, tl, count);
    *w = tl;
}
#endif

static BOOL RenderString(struct RastPort *rp, CONST_STRPTR string, WORD count)
{
    struct TextFont *tf = rp->Font;
    struct BitMap *bm = rp->BitMap;
    struct AAFont *font = NULL;
    struct ViewPort *vp;
    UBYTE mode = aa_Mode;
    UBYTE fg[3], bg[3];
    UBYTE *buf;
    LONG x, y, w, h;

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
    w = CalcWidth(rp, string, count);
#ifdef DEBUG
    CheckWidth(rp, string, count, &w);
#endif
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

BOOL aa_RenderText(struct RastPort *rp, CONST_STRPTR string, WORD count,
                   struct Task *me)
{
    BOOL done;
#ifdef DEBUG
    struct EClockVal t0, t1;
#endif

    if (aa_Mode == AA_MODE_OFF || count <= 0 || !CyberGfxBase)
        return FALSE;
    if (aa_NumBlack && IsBlacklisted(me))
        FAIL(R_BLACKLIST);

#ifdef DEBUG
    if (aa_TimerOpen)
        ReadEClock(&t0);
#endif
    done = RenderString(rp, string, count);
#ifdef DEBUG
    if (done && aa_TimerOpen)
    {
        ULONG d;

        ReadEClock(&t1);
        d = t1.ev_lo - t0.ev_lo;    /* calls are far shorter than a wrap */
        stat_ticks += d;
        if (d > stat_maxticks)
            stat_maxticks = d;
        stat_chars += count;
    }
#endif
    return done;
}

#ifdef DEBUG
static ULONG TicksToMicros(unsigned long long ticks)
{
    return aa_EFreq ? (ULONG)(ticks * 1000000ULL / aa_EFreq) : 0;
}

void aa_PrintRenderStats(void)
{
    ULONG drawn = reason_counts[R_DRAWN];
    int i;

    kprintf("AAText: --- Text() decisions ---\n");
    for (i = 0; i < R_COUNT; i++)
        kprintf("AAText: %-22s %8ld\n", (ULONG)reason_names[i],
                reason_counts[i]);
    if (stat_offscreen)
        kprintf("AAText: offscreen bitmaps drawn with Workbench colours: %ld\n",
                stat_offscreen);
    if (stat_widthmismatch)
        kprintf("AAText: width mismatches vs TextLength(): %ld\n",
                stat_widthmismatch);
    if (drawn && stat_chars && aa_EFreq)
        kprintf("AAText: timing: %ld us per Text() call, %ld us per char, "
                "max %ld us (%ld calls, %ld chars, E-clock %ld Hz)\n",
                TicksToMicros(stat_ticks) / drawn,
                TicksToMicros(stat_ticks) / stat_chars,
                TicksToMicros(stat_maxticks), drawn, stat_chars, aa_EFreq);
    aa_PrintGlyphStats();
}
#endif
