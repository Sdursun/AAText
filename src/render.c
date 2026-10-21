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
#include "metrics.h"
#include "debug.h"

#include <string.h>

/* Upper limit for one temporary pixel buffer (bytes). */
#define AA_MAX_BUFFER   (512 * 1024)

/*
 * Preallocated buffers, so strings need no AllocVec(): two for short
 * strings, two for long lines (a Shell line across a 1920 pixel screen
 * is about 100 KB). Smallest first, GetBuffer() takes the first that fits.
 */
#define AA_NUM_BUFFERS  4
static const ULONG aa_BufferSize[AA_NUM_BUFFERS] =
{
    32 * 1024, 32 * 1024, 128 * 1024, 128 * 1024
};

/* Preallocated chip RAM templates for the 1 bit real metrics path. */
#define AA_NUM_CHIP     2
#define AA_CHIP_SIZE    4096

static UBYTE *aa_ChipBuffers[AA_NUM_CHIP];
static volatile BOOL aa_ChipBusy[AA_NUM_CHIP];

extern struct IntuitionBase *IntuitionBase;

struct Library *CyberGfxBase;
UBYTE aa_Mode = AA_MODE_TEXT;

static UBYTE *aa_Buffers[AA_NUM_BUFFERS];
static volatile BOOL aa_BufferBusy[AA_NUM_BUFFERS];

/* Copied from the prefs at init; read-only afterwards. */
static char aa_Blacklist[AA_MAX_BLACKLIST][AA_NAME_LEN];
static LONG aa_NumBlack;
static BOOL aa_Offscreen;
static BOOL aa_Aga;

#ifdef DEBUG
enum
{
    R_DRAWN, R_REAL_AA, R_REAL_MONO, R_BLACKLIST, R_DRAWMODE, R_STYLE,
    R_NOBITMAP, R_DEPTH, R_NOTCGX, R_LUT8, R_NOMAPPING, R_NOVIEWPORT,
    R_COORDS, R_NOMEM, R_FONTFAIL, R_PALETTE, R_PAL_REAL, R_COUNT
};
static const char *const reason_names[R_COUNT] =
{
    "drawn by AAText", "drawn, real, AA", "drawn, real, 1 bit",
    "blacklisted task", "COMPLEMENT/INVERSVID",
    "bold/italic/underl.", "no bitmap/font", "depth < 15",
    "not a CGX bitmap", "LUT8 bitmap", "font not mapped", "no viewport",
    "coords/size", "out of memory", "font setup failed",
    "drawn, palette screen", "drawn, real, palette"
};
static ULONG reason_counts[R_COUNT];
#define FAIL(r) do { reason_counts[r]++; return FALSE; } while (0)
#define COUNT(r) (reason_counts[r]++)

/* Timing of drawn strings, measured with the E-clock. */
struct Device *TimerBase;
static struct timerequest aa_TimerReq;
static BOOL aa_TimerOpen;
static ULONG aa_EFreq;
static ULONG stat_ticks, stat_maxticks, stat_chars, stat_offscreen, stat_menu;
static ULONG stat_widthmismatch;
static ULONG stat_phase[4];          /* setup, read, draw, write */
static ULONG stat_declined, stat_declinedticks;
/* E-clock low word for phase timing (differences only, never wraps) */
#define TSTAMP(v) \
    do { if (aa_TimerOpen) { struct EClockVal e_; ReadEClock(&e_); \
         (v) = e_.ev_lo; } } while (0)
#else
#define FAIL(r) return FALSE
#define COUNT(r) ((void)0)
#define TSTAMP(v) ((void)0)
#endif

/*
 * Blending without multiplications (they are slow on a 68020): for one
 * text colour, t[c][l][v] is background value v of channel c covered
 * to level l of AA_LEVELS by the colour. Coverage is quantised to
 * AA_LEVELS steps (aa_Level[]). The last AA_BLEND_SLOTS colours are
 * kept; a new colour costs one table (built with additions only).
 * Used with the glyph lock held.
 */
#define AA_LEVELS       32
#define AA_BLEND_SLOTS  4

struct AABlend
{
    ULONG key;                              /* 0x00RRGGBB, ~0 = unused */
    UBYTE t[3][AA_LEVELS + 1][256];
};

static struct AABlend *aa_Blend[AA_BLEND_SLOTS];
static LONG aa_BlendNext;
static UBYTE aa_Level[256];

static void InitLevels(void)
{
    LONG a;

    for (a = 0; a < 256; a++)
        aa_Level[a] = (a * AA_LEVELS + 127) / 255;
}

/*
 * Blacklist, offscreen and aga options. Text() reads them without a lock, so
 * they are replaced inside Forbid(): no task can run halfway through.
 */
void aa_RenderReconfigure(const struct AAPrefs *prefs)
{
    LONG i;

    Forbid();
    for (i = 0; i < prefs->numblack; i++)
        CopyMem((APTR)prefs->blacklist[i], aa_Blacklist[i], AA_NAME_LEN);
    aa_NumBlack = prefs->numblack;
    aa_Offscreen = prefs->offscreen;
    aa_Aga = prefs->aga;
    Permit();

    for (i = 0; i < aa_NumBlack; i++)
        D(("AAText: blacklisted \"%s\"\n", (ULONG)aa_Blacklist[i]));
}

BOOL aa_RenderInit(const struct AAPrefs *prefs)
{
    LONG i;

    aa_RenderReconfigure(prefs);
    InitLevels();

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

    /* the small ones are needed; the large ones save AllocVec() calls */
    for (i = 0; i < AA_NUM_BUFFERS; i++)
    {
        aa_Buffers[i] = AllocVec(aa_BufferSize[i], MEMF_ANY);
        if (!aa_Buffers[i] && aa_BufferSize[i] <= 32 * 1024)
            return FALSE;
    }
    /* optional: GetChipBuffer() falls back to AllocVec() */
    for (i = 0; i < AA_NUM_CHIP; i++)
        aa_ChipBuffers[i] = AllocVec(AA_CHIP_SIZE, MEMF_CHIP);
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
    for (i = 0; i < AA_NUM_CHIP; i++)
    {
        if (aa_ChipBuffers[i])
            FreeVec(aa_ChipBuffers[i]);
        aa_ChipBuffers[i] = NULL;
    }
    for (i = 0; i < AA_BLEND_SLOTS; i++)
    {
        if (aa_Blend[i])
            FreeVec(aa_Blend[i]);
        aa_Blend[i] = NULL;
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

    if (size <= aa_BufferSize[AA_NUM_BUFFERS - 1])
    {
        Forbid();
        for (i = 0; i < AA_NUM_BUFFERS; i++)
        {
            if (aa_Buffers[i] && !aa_BufferBusy[i] &&
                size <= aa_BufferSize[i])
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

    /*
     * Menus: without a menu tool (MagicMenu), Intuition draws the menu
     * items on input.device into a buffer that belongs to no screen and
     * has no layer. They use the pens of the menu's screen, that of the
     * active window (else the front screen).
     */
    if (!vp && !layer)
    {
        static struct Task *inputtask;
        struct Window *aw = IntuitionBase->ActiveWindow;

        if (!inputtask)
            inputtask = FindTask((CONST_STRPTR)"input.device");
        if (inputtask && FindTask(NULL) == inputtask)
        {
            scr = aw ? aw->WScreen : IntuitionBase->FirstScreen;
            if (scr)
                vp = &scr->ViewPort;
#ifdef DEBUG
            stat_menu++;
#endif
        }
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
 *
 * With a layer, a cell starting left of or above the layer (negative x
 * or y: an icon label scrolled half out of a window) is cropped: that
 * part is never visible. *sx, *sy get the columns and rows to skip.
 * Rejecting it instead left such a label to the 1 bit or bitmap
 * fallback on every scroll, next to its scrolled, antialiased part.
 */
static BOOL CoordsOk(struct RastPort *rp, struct BitMap *bm,
                     LONG x, LONG y, LONG w, LONG h, LONG *sx, LONG *sy)
{
    *sx = *sy = 0;
    if (x + w > 0x7FFF || y + h > 0x7FFF)
        return FALSE;
    if (x < 0 || y < 0)
    {
        if (!rp->Layer)
            return FALSE;
        *sx = x < 0 ? -x : 0;
        *sy = y < 0 ? -y : 0;
        if (*sx >= w || *sy >= h)
            return FALSE;
    }

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
 * Algorithmic italic as in graphics.library: rows above the baseline move
 * right, rows below move left, one pixel per two rows.
 */
static inline LONG ItalicShift(LONG row, LONG italicbase)
{
    return italicbase < 0 ? 0 : (italicbase - row) >> 1;
}

/*
 * Blend one glyph into the RGB buffer (w x h). gx/gy is the top left of
 * the glyph bitmap relative to the buffer; pixels outside are clipped.
 * italicbase is the baseline row for italic shearing, or -1.
 */
/* Blend table for colour fg, or NULL (no memory: blend directly). */
static const struct AABlend *BlendFor(const UBYTE *fg)
{
    ULONG key = ((ULONG)fg[0] << 16) | ((ULONG)fg[1] << 8) | fg[2];
    struct AABlend *b;
    LONG i, c, v, l;

    for (i = 0; i < AA_BLEND_SLOTS; i++)
        if (aa_Blend[i] && aa_Blend[i]->key == key)
            return aa_Blend[i];

    i = aa_BlendNext;
    aa_BlendNext = (aa_BlendNext + 1) % AA_BLEND_SLOTS;
    if (!aa_Blend[i])
        aa_Blend[i] = AllocVec(sizeof(struct AABlend), MEMF_ANY);
    b = aa_Blend[i];
    if (!b)
        return NULL;

    for (c = 0; c < 3; c++)
    {
        for (v = 0; v < 256; v++)
        {
            LONG diff = fg[c] - v, acc = 0;

            /* v + diff * l / AA_LEVELS, as a running sum */
            for (l = 0; l <= AA_LEVELS; l++, acc += diff)
                b->t[c][l][v] = v + (acc >> 5);
        }
    }
    b->key = key;
    return b;
}

/*
 * Palette screens and the coverage maps behind them want one coverage
 * value a pixel: set while they draw, LCD glyphs are blended with the
 * mean of their three values. Glyph lock held.
 */
static BOOL aa_GrayCoverage;

/*
 * LCD glyph (subpixel): each channel mixed with its own coverage, so the
 * subpixels of the screen get the third-of-a-pixel positions.
 */
static void BlendLCD(UBYTE *buf, LONG w, LONG h, const struct AAGlyph *g,
                     LONG gx, LONG gy, const UBYTE *fg,
                     const struct AABlend *bt, LONG italicbase)
{
    LONG y0 = 0, y1 = g->rows, x, y, gw = g->width;

    if (gy < 0)
        y0 = -gy;
    if (gy + y1 > h)
        y1 = h - gy;
    for (y = y0; y < y1; y++)
    {
        LONG rx = gx + ItalicShift(gy + y, italicbase);
        LONG x0 = rx < 0 ? -rx : 0;
        LONG x1 = rx + gw > w ? w - rx : gw;
        const UBYTE *src = g->data + (y * gw + x0) * 3;
        UBYTE *dst = buf + ((gy + y) * w + rx + x0) * 3;

        for (x = x0; x < x1; x++, src += 3, dst += 3)
        {
            LONG c;

            if (!(src[0] | src[1] | src[2]))
                continue;
            for (c = 0; c < 3; c++)
            {
                if (bt)
                {
                    LONG l = aa_Level[src[c]];

                    if (l)
                        dst[c] = bt->t[c][l][dst[c]];
                }
                else if (src[c])
                {
                    ULONG a = src[c];

                    a += a >> 7;        /* 0..255 -> 0..256 */
                    dst[c] += ((fg[c] - dst[c]) * (LONG)a) >> 8;
                }
            }
        }
    }
}

/* Gray coverage of an LCD glyph, for the gray blending below */
static struct AAGlyph *GrayCopy(const struct AAGlyph *g)
{
    static UBYTE mem[sizeof(struct AAGlyph) + 128 * 128];
    struct AAGlyph *c = (struct AAGlyph *)mem;
    LONG x, y;

    if ((ULONG)g->width * g->rows > 128 * 128)
        return NULL;
    *c = *g;
    c->lcd = FALSE;
    for (y = 0; y < g->rows; y++)
        for (x = 0; x < g->width; x++)
            c->data[y * g->width + x] = aa_GlyphCov(g, x, y);
    return c;
}

static void BlendGlyph(UBYTE *buf, LONG w, LONG h, const struct AAGlyph *g,
                       LONG gx, LONG gy, const UBYTE *fg,
                       const struct AABlend *bt, LONG italicbase)
{
    LONG y0 = 0, y1 = g->rows;
    LONG x, y;
    LONG fr = fg[0], fgc = fg[1], fb = fg[2];

    if (g->lcd)
    {
        if (!aa_GrayCoverage)
        {
            BlendLCD(buf, w, h, g, gx, gy, fg, bt, italicbase);
            return;
        }
        if (!(g = GrayCopy(g)))
            return;
        y1 = g->rows;
    }

    if (bt)
    {
        const UBYTE (*tr)[256] = bt->t[0];
        const UBYTE (*tg)[256] = bt->t[1];
        const UBYTE (*tb)[256] = bt->t[2];
        LONG stride = w * 3, gw = g->width;
        const UBYTE *srow;
        UBYTE *drow;

        if (gy < 0)
            y0 = -gy;
        if (gy + y1 > h)
            y1 = h - gy;
        if (y0 >= y1)
            return;
        /* rows advance by additions: multiplications are slow on 68020 */
        srow = g->data + y0 * gw;
        drow = buf + (gy + y0) * stride;
        for (y = y0; y < y1; y++, srow += gw, drow += stride)
        {
            LONG rx = gx + ItalicShift(gy + y, italicbase);
            LONG x0 = rx < 0 ? -rx : 0;
            LONG x1 = rx + gw > w ? w - rx : gw;
            const UBYTE *src = srow + x0;
            UBYTE *dst = drow + (rx + x0) * 3;

            for (x = x0; x < x1; x++, dst += 3)
            {
                LONG l = aa_Level[*src++];

                if (l == 0)
                    continue;
                dst[0] = tr[l][dst[0]];
                dst[1] = tg[l][dst[1]];
                dst[2] = tb[l][dst[2]];
            }
        }
        return;
    }

    if (gy < 0)
        y0 = -gy;
    if (gy + y1 > h)
        y1 = h - gy;

    for (y = y0; y < y1; y++)
    {
        LONG rx = gx + ItalicShift(gy + y, italicbase);
        LONG x0 = rx < 0 ? -rx : 0;
        LONG x1 = rx + g->width > w ? w - rx : g->width;
        const UBYTE *src = g->data + y * g->width + x0;
        UBYTE *dst = buf + ((gy + y) * w + rx + x0) * 3;

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
                       const UBYTE *fg, const struct AABlend *bt)
{
    struct TextFont *tf = rp->Font;
    WORD *kern = (WORD *)tf->tf_CharKern;
    WORD *space = (WORD *)tf->tf_CharSpace;
    LONG defidx = tf->tf_HiChar - tf->tf_LoChar + 1;  /* "not in font" glyph */
    LONG pen = 0;
    LONG baseline = tf->tf_Baseline + font->yoffset;   /* measured, see glyphs.h */

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
            LONG gy = baseline + 1 - g->top;  /* see AAGlyph.top */

            BlendGlyph(buf, w, h, g, gx, gy, fg, bt, -1);
        }
        pen += cell + rp->TxSpacing;
    }
}

/*
 * 1 bit version for BltTemplate(): set template bits where the glyph
 * coverage is at least 50%. bpr is the template's bytes per row.
 */
static void SetGlyphBits(UBYTE *tmpl, LONG bpr, LONG w, LONG h,
                         const struct AAGlyph *g, LONG gx, LONG gy,
                         LONG italicbase)
{
    LONG y0 = 0, y1 = g->rows;
    LONG x, y;

    if (gy < 0)
        y0 = -gy;
    if (gy + y1 > h)
        y1 = h - gy;

    for (y = y0; y < y1; y++)
    {
        LONG rx = gx + ItalicShift(gy + y, italicbase);
        LONG x0 = rx < 0 ? -rx : 0;
        LONG x1 = rx + g->width > w ? w - rx : g->width;
        UBYTE *row = tmpl + (gy + y) * bpr;

        for (x = x0; x < x1; x++)
        {
            if (aa_GlyphCov(g, x, y) >= 128)
            {
                LONG px = rx + x;

                row[px >> 3] |= 0x80 >> (px & 7);
            }
        }
    }
}

/* Underline (or any horizontal line) from x0 to x1-1 in buffer row y. */
static void LineRGB(UBYTE *buf, LONG w, LONG y, LONG x0, LONG x1,
                    const UBYTE *fg)
{
    UBYTE *p;

    if (x0 < 0)
        x0 = 0;
    if (x1 > w)
        x1 = w;
    for (p = buf + (y * w + x0) * 3; x0 < x1; x0++, p += 3)
    {
        p[0] = fg[0];
        p[1] = fg[1];
        p[2] = fg[2];
    }
}

static void LineBits(UBYTE *tmpl, LONG bpr, LONG w, LONG y, LONG x0, LONG x1)
{
    UBYTE *row = tmpl + y * bpr;

    if (x0 < 0)
        x0 = 0;
    if (x1 > w)
        x1 = w;
    for (; x0 < x1; x0++)
        row[x0 >> 3] |= 0x80 >> (x0 & 7);
}

/*
 * Real metrics mode: draw the string with TrueType advances into either
 * an RGB buffer (rgb != NULL) or a 1 bit template. ox is the pen origin
 * inside the box. Algorithmic styles are applied like graphics.library
 * does: bold smears by tf_BoldSmear, italic shears, underline one row
 * below the baseline. Glyph lock held; no graphics calls.
 */
static void DrawReal(UBYTE *rgb, UBYTE *tmpl, LONG bpr, LONG w, LONG h,
                     struct RastPort *rp, struct AAFont *font,
                     CONST_STRPTR s, LONG count, LONG ox, const UBYTE *fg,
                     const struct AABlend *bt)
{
    struct TextFont *tf = rp->Font;
    UBYTE style = rp->AlgoStyle;
    LONG baseline = tf->tf_Baseline + font->yoffset;   /* measured, see glyphs.h */
    LONG italicbase = (style & FSF_ITALIC) ? baseline : -1;
    LONG smear = (style & FSF_BOLD) ? (tf->tf_BoldSmear ? tf->tf_BoldSmear : 1)
                                    : 0;
    LONG pen = ox, i;

    for (i = 0; i < count; i++)
    {
        UBYTE c = s[i];
        struct AAGlyph *g;

        if (i > 0)                  /* as aa_MExtent() measures it */
            pen += aa_KernPair(font, s[i - 1], c);
        g = aa_GetGlyph(font, c);

        if (g && g->width)
        {
            LONG gx = pen + g->left;
            LONG gy = baseline + 1 - g->top;  /* see AAGlyph.top */

            if (rgb)
            {
                BlendGlyph(rgb, w, h, g, gx, gy, fg, bt, italicbase);
                if (smear)
                    BlendGlyph(rgb, w, h, g, gx + smear, gy, fg, bt,
                               italicbase);
            }
            else
            {
                SetGlyphBits(tmpl, bpr, w, h, g, gx, gy, italicbase);
                if (smear)
                    SetGlyphBits(tmpl, bpr, w, h, g, gx + smear, gy,
                                 italicbase);
            }
        }
        pen += font->adv[c] + rp->TxSpacing;
    }

    if (style & FSF_UNDERLINED)
    {
        LONG uy = baseline + 1 < h ? baseline + 1 : baseline;

        if (rgb)
            LineRGB(rgb, w, uy, ox, pen, fg);
        else
            LineBits(tmpl, bpr, w, uy, ox, pen);
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

static void FillMetricsCtx(struct AAMetricsCtx *m, struct RastPort *rp,
                           struct AAFont *font)
{
    struct TextFont *tf = rp->Font;

    m->font = font;
    m->ysize = tf->tf_YSize;
    m->baseline = tf->tf_Baseline;
    m->boldsmear = tf->tf_BoldSmear;
    m->txspacing = rp->TxSpacing;
    m->algostyle = rp->AlgoStyle;
}

/*
 * Does the rastport's font use real metrics for this task? Decided by
 * font and task only - never by the destination - because programs often
 * measure with a different rastport than they draw with. Text() and the
 * three measuring hooks all use this, so they always agree.
 */
BOOL aa_RealMetrics(struct RastPort *rp, struct Task *me,
                    struct AAMetricsCtx *m)
{
    struct TextFont *tf = rp->Font;
    struct AAFont *font;

    if (aa_Mode != AA_MODE_TEXT || !tf)
        return FALSE;
    font = aa_FindFont(tf);
    if (!font || !aa_UseReal(font, tf))
        return FALSE;
    if (aa_NumBlack && IsBlacklisted(me))
        return FALSE;
    if (!font->prepared)
    {
        BOOL ok;

        aa_LockGlyphs();
        ok = aa_PrepareFont(font, tf);
        aa_UnlockGlyphs();
        if (!ok)
            return FALSE;
    }
    FillMetricsCtx(m, rp, font);
    return TRUE;
}

/*
 * Chip RAM templates for BltTemplate(): on planar screens the blitter
 * reads the template, so it must be in chip RAM (the only chip RAM
 * AAText uses). Never waits, like GetBuffer().
 */
static UBYTE *GetChipBuffer(ULONG size)
{
    LONG i;

    if (size <= AA_CHIP_SIZE)
    {
        Forbid();
        for (i = 0; i < AA_NUM_CHIP; i++)
        {
            if (aa_ChipBuffers[i] && !aa_ChipBusy[i])
            {
                aa_ChipBusy[i] = TRUE;
                Permit();
                return aa_ChipBuffers[i];
            }
        }
        Permit();
    }
    return AllocVec(size, MEMF_CHIP);
}

static void FreeChipBuffer(UBYTE *buf)
{
    LONG i;

    for (i = 0; i < AA_NUM_CHIP; i++)
    {
        if (buf == aa_ChipBuffers[i])
        {
            aa_ChipBusy[i] = FALSE;
            return;
        }
    }
    FreeVec(buf);
}

/* Can this rastport take the antialiased read-modify-write path? */
/* ------------------------------------------------------------------ */
/* Palette (AGA) screens                                                */
/* ------------------------------------------------------------------ */

/* Coverage steps: 0 (untouched), 1/3, 2/3 (palette mixes), 1 (text pen) */
#define AA_PAL_STEPS 3

/* Bitmaps AAText draws on with palette pens ("aga on"). */
static BOOL IsPaletteBitMap(struct BitMap *bm)
{
    return aa_Aga && GetBitMapAttr(bm, BMA_DEPTH) <= 8;
}

/*
 * The pen for bg + (fg - bg) * step / AA_PAL_STEPS: the nearest palette
 * colour if it is close to that mix (within a quarter of the distance
 * between bg and fg), else no mix at all: the text pen from half
 * coverage up, the background below. A 16 colour palette without greys
 * would otherwise give brown or blue edges to black text on grey.
 */
static UBYTE MixPen(struct ColorMap *cm, const ULONG *rgb, LONG ncol,
                    UBYTE bgpen, UBYTE fgpen, LONG step)
{
    ULONG c[3];
    LONG mix[3], span = 0, dist = 0, i;
    UBYTE pen;

    for (i = 0; i < 3; i++)
    {
        LONG b = rgb[bgpen * 3 + i] >> 24;
        LONG f = rgb[fgpen * 3 + i] >> 24;

        mix[i] = b + (f - b) * step / AA_PAL_STEPS;
        c[i] = (ULONG)mix[i] * 0x01010101;
        span += (f - b) * (f - b);
    }
    pen = (UBYTE)FindColor(cm, c[0], c[1], c[2], ncol - 1);
    for (i = 0; i < 3; i++)
    {
        LONG d = (LONG)(rgb[pen * 3 + i] >> 24) - mix[i];

        dist += d * d;
    }
    if (dist * 16 > span)       /* farther than a quarter of the span */
        return step * 2 >= AA_PAL_STEPS ? fgpen : bgpen;
    return pen;
}

/*
 * Draw a coverage map (the first byte of each RGB pixel of cov: white
 * text blended on black, covmod bytes a row) into the w x h cell at x,
 * y of a palette
 * rastport. The pens under the cell are read (JAM1) or are the BgPen
 * (JAM2); covered pixels become the text pen or the palette colour
 * nearest to the mix, then the pens are written back.
 *
 * Everything is allocated per call: Text() runs on any task, often with
 * layers locked, so no lock of ours may be held around these graphics
 * calls. No pens are allocated.
 */
static BOOL PaletteCompose(struct RastPort *rp, struct ViewPort *vp,
                           const UBYTE *cov, LONG covmod, LONG x, LONG y,
                           LONG w, LONG h)
{
    struct ColorMap *cm = vp->ColorMap;
    LONG depth = GetBitMapAttr(rp->BitMap, BMA_DEPTH);
    LONG ncol = 1L << depth;
    LONG mod = (w + 15) & ~15;      /* row length ReadPixelArray8() uses */
    struct RastPort temprp;
    struct BitMap *tbm;
    ULONG *rgb;
    UBYTE *pens;
    UWORD *mix;
    UBYTE fgpen = GetAPen(rp);
    LONG r, c, i;

    if (ncol > cm->Count)
        ncol = cm->Count;
    if (fgpen >= ncol)
        return FALSE;
    /* palette (32 bit guns), pens of the cell, mix cache per step and pen */
    rgb = AllocVec(ncol * 3 * sizeof(ULONG) + mod * h +
                   (AA_PAL_STEPS - 1) * 256 * sizeof(UWORD), MEMF_ANY);
    if (!rgb)
        return FALSE;
    pens = (UBYTE *)(rgb + ncol * 3);
    mix = (UWORD *)(pens + mod * h);        /* mod is even */
    tbm = AllocBitMap(mod, 1, depth, 0, NULL);
    if (!tbm)
    {
        FreeVec(rgb);
        return FALSE;
    }
    InitRastPort(&temprp);
    temprp.BitMap = tbm;

    GetRGB32(cm, 0, ncol, rgb);
    for (i = 0; i < (AA_PAL_STEPS - 1) * 256; i++)
        mix[i] = 0xFFFF;

    if (rp->DrawMode & JAM2)
        memset(pens, GetBPen(rp), mod * h);
    else
        ReadPixelArray8(rp, x, y, x + w - 1, y + h - 1, pens, &temprp);

    for (r = 0; r < h; r++)
    {
        const UBYTE *cv = cov + r * covmod;
        UBYTE *p = pens + r * mod;

        for (c = 0; c < w; c++, cv += 3, p++)
        {
            LONG step = (*cv * AA_PAL_STEPS + 127) / 255;
            UWORD *m;

            if (!step)
                continue;
            if (step >= AA_PAL_STEPS || *p >= ncol)
            {
                *p = fgpen;
                continue;
            }
            m = &mix[(step - 1) * 256 + *p];
            if (*m == 0xFFFF)
                *m = MixPen(cm, rgb, ncol, *p, fgpen, step);
            *p = (UBYTE)*m;
        }
    }

    WritePixelArray8(rp, x, y, x + w - 1, y + h - 1, pens, &temprp);
    FreeBitMap(tbm);
    FreeVec(rgb);
    return TRUE;
}

static struct ViewPort *CanBlend(struct RastPort *rp, LONG x, LONG y,
                                 LONG w, LONG h, LONG *sx, LONG *sy)
{
    struct BitMap *bm = rp->BitMap;

    if (rp->DrawMode & (COMPLEMENT | INVERSVID))
        return NULL;
    if (GetBitMapAttr(bm, BMA_DEPTH) < 15 || !CyberGfxBase ||
        !cgx_GetCyberMapAttr(CyberGfxBase, bm, CYBRMATTR_ISCYBERGFX) ||
        cgx_GetCyberMapAttr(CyberGfxBase, bm, CYBRMATTR_PIXFMT) == PIXFMT_LUT8)
        return NULL;
    if (w * h * 3 > AA_MAX_BUFFER || !CoordsOk(rp, bm, x, y, w, h, sx, sy))
        return NULL;
    return FindViewPort(rp);
}

/*
 * Real metrics mode Text(). Must draw every string of a real font itself
 * (the measuring hooks already reported TrueType widths): antialiased
 * where possible, otherwise as a 1 bit template through BltTemplate(),
 * which handles all draw modes and planar screens like Text() does.
 */
static BOOL RenderReal(struct RastPort *rp, CONST_STRPTR string, WORD count,
                       struct AAFont *font)
{
    struct TextFont *tf = rp->Font;
    struct AAMetricsCtx m;
    struct TextExtent te;
    struct ViewPort *vp;
    LONG x, y, w, h, ox, sx = 0, sy = 0;
    BOOL ok;

    if (!rp->BitMap)
        FAIL(R_NOBITMAP);

    aa_LockGlyphs();
    ok = aa_PrepareFont(font, tf);
    aa_UnlockGlyphs();
    if (!ok)
        FAIL(R_FONTFAIL);

    /* Draw box = text extent, exactly what TextExtent() reports. */
    FillMetricsCtx(&m, rp, font);
    aa_MExtent(&m, string, count, &te);
    ox = -te.te_Extent.MinX;
    x = rp->cp_x + te.te_Extent.MinX;
    y = rp->cp_y - tf->tf_Baseline;
    w = te.te_Extent.MaxX - te.te_Extent.MinX + 1;
    h = tf->tf_YSize;
    if (w <= 0 || h <= 0)
    {
        rp->cp_x += te.te_Width;
        COUNT(R_REAL_AA);
        return TRUE;
    }

    vp = CanBlend(rp, x, y, w, h, &sx, &sy);
    if (vp && vp->ColorMap)
    {
        UBYTE fg[3], bg[3];
        UBYTE *buf = GetBuffer(w * h * 3);

        if (buf)
        {
            PenToRGB(vp, GetAPen(rp), fg);
            if (rp->DrawMode & JAM2)
            {
                PenToRGB(vp, GetBPen(rp), bg);
                FillBuffer(buf, w * h, bg);
            }
            else
                cgx_ReadPixelArray(CyberGfxBase, buf, sx, sy, w * 3, rp,
                                   x + sx, y + sy, w - sx, h - sy,
                                   RECTFMT_RGB);

            aa_LockGlyphs();
            DrawReal(buf, NULL, 0, w, h, rp, font, string, count, ox, fg,
                     BlendFor(fg));
            aa_UnlockGlyphs();

            cgx_WritePixelArray(CyberGfxBase, buf, sx, sy, w * 3, rp,
                                x + sx, y + sy, w - sx, h - sy,
                                RECTFMT_RGB);
            FreeBuffer(buf);
            rp->cp_x += te.te_Width;
            COUNT(R_REAL_AA);
            return TRUE;
        }
    }

    /* "aga on": palette screens with mixed pens */
    if (!vp && IsPaletteBitMap(rp->BitMap) &&
        !(rp->DrawMode & (COMPLEMENT | INVERSVID)) &&
        w * h * 3 <= AA_MAX_BUFFER &&
        CoordsOk(rp, rp->BitMap, x, y, w, h, &sx, &sy) &&
        (vp = FindViewPort(rp)) && vp->ColorMap)
    {
        static const UBYTE white[3] = { 255, 255, 255 };
        UBYTE *buf = GetBuffer(w * h * 3);

        if (buf)
        {
            BOOL done;

            memset(buf, 0, w * h * 3);
            aa_LockGlyphs();
            aa_GrayCoverage = TRUE;
            DrawReal(buf, NULL, 0, w, h, rp, font, string, count, ox, white,
                     BlendFor(white));
            aa_GrayCoverage = FALSE;
            aa_UnlockGlyphs();
            done = PaletteCompose(rp, vp, buf + (sy * w + sx) * 3, w * 3,
                                  x + sx, y + sy, w - sx, h - sy);
            FreeBuffer(buf);
            if (done)
            {
                rp->cp_x += te.te_Width;
                COUNT(R_PAL_REAL);
                return TRUE;
            }
        }
    }

    /* 1 bit fallback: any screen type and draw mode. */
    {
        LONG bpr = ((w + 15) >> 4) << 1;
        UBYTE *tmpl = GetChipBuffer(bpr * h);

        if (!tmpl)
            FAIL(R_NOMEM);
        memset(tmpl, 0, bpr * h);

        aa_LockGlyphs();
        DrawReal(NULL, tmpl, bpr, w, h, rp, font, string, count, ox, NULL,
                 NULL);
        aa_UnlockGlyphs();

        BltTemplate((PLANEPTR)tmpl, 0, bpr, rp, x, y, w, h);
        FreeChipBuffer(tmpl);
    }
    rp->cp_x += te.te_Width;
    COUNT(R_REAL_MONO);
    return TRUE;
}

static BOOL RenderString(struct RastPort *rp, CONST_STRPTR string, WORD count)
{
    struct TextFont *tf = rp->Font;
    struct BitMap *bm = rp->BitMap;
    struct AAFont *font = NULL;
    struct ViewPort *vp;
    UBYTE mode = aa_Mode;
    UBYTE fg[3], bg[3];
    UBYTE *buf;
    BOOL palette;
    LONG sx = 0, sy = 0;           /* cropped columns and rows */
    LONG x, y, w, h;
#ifdef DEBUG
    ULONG ts0 = 0, ts1 = 0, ts2 = 0, ts3 = 0, ts4 = 0;
#endif

    TSTAMP(ts0);
    if (!tf)
        FAIL(R_NOBITMAP);
    if (mode == AA_MODE_TEXT)
    {
        font = aa_FindFont(tf);
        if (font && aa_UseReal(font, tf))
            return RenderReal(rp, string, count, font);
    }

    if (rp->DrawMode & (COMPLEMENT | INVERSVID))
        FAIL(R_DRAWMODE);
    if (rp->AlgoStyle & (FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED))
        FAIL(R_STYLE);
    if (!bm)
        FAIL(R_NOBITMAP);
    if (mode == AA_MODE_TEXT && !font)
    {
#ifdef DEBUG
        LogUnmappedFont(tf);
#endif
        FAIL(R_NOMAPPING);
    }
    palette = IsPaletteBitMap(bm);
    if (!palette)
    {
        if (GetBitMapAttr(bm, BMA_DEPTH) < 15)
            FAIL(R_DEPTH);
        if (!CyberGfxBase ||
            !cgx_GetCyberMapAttr(CyberGfxBase, bm, CYBRMATTR_ISCYBERGFX))
            FAIL(R_NOTCGX);
        if (cgx_GetCyberMapAttr(CyberGfxBase, bm,
                                CYBRMATTR_PIXFMT) == PIXFMT_LUT8)
            FAIL(R_LUT8);
    }
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
        !CoordsOk(rp, bm, x, y, w, h, &sx, &sy))
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

    /* "aga on": coverage as white on black, then mixed palette pens */
    if (palette)
    {
        static const UBYTE white[3] = { 255, 255, 255 };
        BOOL done;

        memset(buf, 0, w * h * 3);
        if (font)
        {
            aa_LockGlyphs();
            aa_GrayCoverage = TRUE;
            DrawString(buf, w, h, rp, font, string, count, white,
                       BlendFor(white));
            aa_GrayCoverage = FALSE;
            aa_UnlockGlyphs();
        }
        else
            DrawGradient(buf, w, h, white);
        done = PaletteCompose(rp, vp, buf + (sy * w + sx) * 3, w * 3,
                              x + sx, y + sy, w - sx, h - sy);
        FreeBuffer(buf);
        if (!done)
            FAIL(R_NOMEM);
        rp->cp_x += w;
        COUNT(R_PALETTE);
        return TRUE;
    }

    PenToRGB(vp, GetAPen(rp), fg);

    /* JAM2: background is the BgPen colour, no need to read it. */
    TSTAMP(ts1);
    if (rp->DrawMode & JAM2)
    {
        PenToRGB(vp, GetBPen(rp), bg);
        FillBuffer(buf, w * h, bg);
    }
    else
        cgx_ReadPixelArray(CyberGfxBase, buf, sx, sy, w * 3, rp, x + sx,
                           y + sy, w - sx, h - sy, RECTFMT_RGB);
    TSTAMP(ts2);

    if (font)
    {
        aa_LockGlyphs();
        DrawString(buf, w, h, rp, font, string, count, fg, BlendFor(fg));
        aa_UnlockGlyphs();
    }
    else
        DrawGradient(buf, w, h, fg);

    TSTAMP(ts3);
    cgx_WritePixelArray(CyberGfxBase, buf, sx, sy, w * 3, rp, x + sx, y + sy,
                        w - sx, h - sy, RECTFMT_RGB);
    FreeBuffer(buf);
    TSTAMP(ts4);
#ifdef DEBUG
    if (aa_TimerOpen)
    {
        stat_phase[0] += ts1 - ts0;
        stat_phase[1] += ts2 - ts1;
        stat_phase[2] += ts3 - ts2;
        stat_phase[3] += ts4 - ts3;
    }
#endif

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

    /* without cybergraphics.library (no RTG) only palette screens */
    if (aa_Mode == AA_MODE_OFF || count <= 0 || (!CyberGfxBase && !aa_Aga))
        return FALSE;
    if (aa_NumBlack && IsBlacklisted(me))
        FAIL(R_BLACKLIST);

#ifdef DEBUG
    if (aa_TimerOpen)
        ReadEClock(&t0);
#endif
    done = RenderString(rp, string, count);
#ifdef DEBUG
    if (aa_TimerOpen)
    {
        ULONG d;

        ReadEClock(&t1);
        d = t1.ev_lo - t0.ev_lo;    /* calls are far shorter than a wrap */
        if (done)
        {
            stat_ticks += d;
            if (d > stat_maxticks)
                stat_maxticks = d;
            stat_chars += count;
        }
        else
        {
            /* what AAText costs for text it leaves to the system */
            stat_declined++;
            stat_declinedticks += d;
        }
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
    if (stat_menu)
        kprintf("AAText: menu items drawn with the menu screen's colours: %ld\n",
                stat_menu);
    if (stat_widthmismatch)
        kprintf("AAText: width mismatches vs TextLength(): %ld\n",
                stat_widthmismatch);
    if (drawn && stat_chars && aa_EFreq)
        kprintf("AAText: timing: %ld us per Text() call, %ld us per char, "
                "max %ld us (%ld calls, %ld chars, E-clock %ld Hz)\n",
                TicksToMicros(stat_ticks) / drawn,
                TicksToMicros(stat_ticks) / stat_chars,
                TicksToMicros(stat_maxticks), drawn, stat_chars, aa_EFreq);
    if (drawn && aa_EFreq)
        kprintf("AAText: per drawn call (safe metrics): setup %ld us, "
                "read %ld us, draw %ld us, write %ld us\n",
                TicksToMicros(stat_phase[0]) / drawn,
                TicksToMicros(stat_phase[1]) / drawn,
                TicksToMicros(stat_phase[2]) / drawn,
                TicksToMicros(stat_phase[3]) / drawn);
    if (stat_declined && aa_EFreq)
        kprintf("AAText: declined calls: %ld, %ld us each\n", stat_declined,
                TicksToMicros(stat_declinedticks) / stat_declined);
    aa_PrintGlyphStats();
}
#endif
