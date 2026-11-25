/*
 * AAText - reads the background under a string before blending.
 *
 * ReadPixelArray() works everywhere. Reading the screen memory directly
 * is about twice as fast on PiStorm/Emu68 RTG (330 against 152 us for
 * a 400 x 20 line), but seven times slower on WinUAE uaegfx (14 against
 * 106 us). "directread auto" (the default) therefore measures both once
 * per pixel format, on a real string, and reads directly only if that is
 * at least 10% faster (ReadPixelArray() is the safe way); it also
 * compares the pixels both give and stays with ReadPixelArray() if they
 * differ. "directread on" / "off" force one way.
 *
 * Direct read: layers.library/DoHookClipRects() calls our hook for each
 * part of the layer inside the box. Parts on the screen are read from
 * the bitmap locked with LockBitMapTagList(): only plain copies while it
 * is locked, no graphics calls. When a part is in a backing store
 * (obscured smart refresh layer), the whole string is read with
 * ReadPixelArray() instead; super bitmap layers always are. Parts of
 * simple refresh layers hidden by other windows are not visited, the
 * same as ReadPixelArray() leaves them, and WritePixelArray() does not
 * write there either.
 *
 * 15, 16 and 32 bit pixel formats; 24 bit screens use ReadPixelArray().
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/layers.h>
#include <proto/timer.h>

#include "directread.h"
#include "render.h"
#include "cgx.h"
#include "debug.h"

struct Library *LayersBase;
struct Device *TimerBase;

static struct timerequest dr_TimerReq;
static BOOL dr_TimerOpen;
static UBYTE dr_Mode;

/* per pixel format (PIXFMT_...): what measuring found */
enum { FMT_UNKNOWN = 0, FMT_DIRECT, FMT_RPA };
#define NUM_PIXFMT        14
static volatile UBYTE dr_State[NUM_PIXFMT];
static volatile UBYTE dr_Mismatch[NUM_PIXFMT];

#define CALIB_MIN_PIXELS  1500  /* smaller strings measure too noisily */
#define CALIB_RUNS        3     /* the fastest of three counts */
#define CALIB_MAX_DIFFER  3     /* pixel differences before giving up */

#ifdef DEBUG
static ULONG stat_direct, stat_rpa, stat_fallback, stat_offset;
#define STAT(v) ((v)++)
#else
#define STAT(v) ((void)0)
#endif

/* Message of DoHookClipRects() hooks (not in the NDK headers). */
struct AAClipMsg
{
    struct Layer    *layer;
    struct Rectangle bounds;    /* in the bitmap of the hook's rastport */
    LONG             offsetx;   /* layer coordinates of bounds.MinX/MinY */
    LONG             offsety;
};

struct ReadHook
{
    struct Hook    hook;
    struct BitMap *bm;          /* the string's rastport bitmap */
    ULONG          fmt;         /* its pixel format */
    UBYTE         *buf;
    LONG           mod;         /* bytes per buffer row */
    LONG           bx, by;      /* layer coordinates of buffer pixel 0, 0 */
    LONG           x0, y0;      /* layer coordinates to read, inclusive */
    LONG           x1, y1;
    LONG           lx, ly;      /* bitmap coordinates of layer 0, 0 */
    ULONG          pixels;      /* pixels copied */
    BOOL           fallback;    /* use ReadPixelArray() for this string */
    const ULONG   *thi, *tlo;   /* 15/16 bit: see Tables16() */
};

static BOOL Is32(ULONG fmt)
{
    return fmt == PIXFMT_ARGB32 || fmt == PIXFMT_BGRA32 ||
           fmt == PIXFMT_RGBA32;
}

/* the 15 and 16 bit formats, PIXFMT_RGB15 (1) to PIXFMT_BGR16PC (8) */
static BOOL Is16(ULONG fmt)
{
    return fmt >= PIXFMT_RGB15 && fmt <= PIXFMT_BGR16PC;
}

/* "PC" formats: the low byte of a pixel comes first in memory */
static BOOL IsPC(ULONG fmt)
{
    return fmt == PIXFMT_RGB15PC || fmt == PIXFMT_BGR15PC ||
           fmt == PIXFMT_RGB16PC || fmt == PIXFMT_BGR16PC;
}

/*
 * 15/16 bit pixel value v (as a big endian word) to R, G, B, x bytes.
 * 5 and 6 bit values are scaled to 8 bits by repeating their top bits,
 * so white stays 255.
 */
static ULONG Expand16(ULONG fmt, ULONG v)
{
    ULONG r, g, b, t;

    if (fmt <= PIXFMT_BGR15PC)
    {
        r = (v >> 10) & 31;
        g = (v >> 5) & 31;
        b = v & 31;
        g = (g << 3) | (g >> 2);
    }
    else
    {
        r = (v >> 11) & 31;
        g = (v >> 5) & 63;
        b = v & 31;
        g = (g << 2) | (g >> 4);
    }
    r = (r << 3) | (r >> 2);
    b = (b << 3) | (b >> 2);
    if (fmt == PIXFMT_BGR15 || fmt == PIXFMT_BGR15PC ||
        fmt == PIXFMT_BGR16 || fmt == PIXFMT_BGR16PC)
    {
        t = r;
        r = b;
        b = t;
    }
    return (r << 24) | (g << 16) | (b << 8);
}

/*
 * Two tables per 15/16 bit format, 256 entries each: the converted
 * pixel is thi[high byte] | tlo[low byte]. That works because every
 * output bit comes from only one of the two bytes; checked over all
 * 65536 values when the tables are built (a format that failed would
 * be read with ReadPixelArray()). Built on first use, freed when AAText
 * quits.
 */
static ULONG *dr_Tables[PIXFMT_BGR16PC + 1];
static UBYTE dr_TablesBad[PIXFMT_BGR16PC + 1];

static const ULONG *Tables16(ULONG fmt)
{
    ULONG *t, v;

    if (dr_Tables[fmt] || dr_TablesBad[fmt])
        return dr_Tables[fmt];
    t = AllocVec(2 * 256 * sizeof(ULONG), MEMF_ANY);
    if (!t)
        return NULL;
    for (v = 0; v < 256; v++)
    {
        t[v] = Expand16(fmt, v << 8);
        t[256 + v] = Expand16(fmt, v);
    }
    for (v = 0; v < 65536; v++)
        if ((t[v >> 8] | t[256 + (v & 255)]) != Expand16(fmt, v))
        {
            FreeVec(t);
            dr_TablesBad[fmt] = TRUE;
            D(("AAText: directread: no tables for pixel format %ld\n", fmt));
            return NULL;
        }
    Forbid();
    if (!dr_Tables[fmt])
    {
        dr_Tables[fmt] = t;
        t = NULL;
    }
    Permit();
    if (t)
        FreeVec(t);             /* another task was quicker */
    return dr_Tables[fmt];
}

/* Can the format be read directly? Sets up its tables. */
static BOOL Supported(ULONG fmt)
{
    return Is32(fmt) || (Is16(fmt) && Tables16(fmt));
}

/*
 * Bits of R, G, B that ReadPixelArray() and direct read must agree on.
 * For 15/16 bit screens only the bits the screen has: how the system
 * scales 5 and 6 bit values to 8 bits may differ from Expand16().
 */
static ULONG SumMask(ULONG fmt)
{
    if (fmt <= PIXFMT_BGR15PC)
        return 0xF8F8F800UL;
    if (fmt <= PIXFMT_BGR16PC)
        return 0xF8FCF800UL;    /* green has 6 bits */
    return 0xFFFFFF00UL;
}

/*
 * Copy bitmap rectangle minx..maxx, miny..maxy (inclusive, bitmap
 * coordinates) into the buffer as R, G, B, x bytes.
 */
static BOOL ReadRect(struct ReadHook *d, LONG minx, LONG miny, LONG maxx,
                     LONG maxy)
{
    struct TagItem tags[7];
    ULONG base = 0, bpr = 0, fmt = ~0UL, bpp = 0, bw = 0, bh = 0;
    APTR lock;
    LONG n, y;

    tags[0].ti_Tag = LBMI_BASEADDRESS;  tags[0].ti_Data = (ULONG)&base;
    tags[1].ti_Tag = LBMI_BYTESPERROW;  tags[1].ti_Data = (ULONG)&bpr;
    tags[2].ti_Tag = LBMI_PIXFMT;       tags[2].ti_Data = (ULONG)&fmt;
    tags[3].ti_Tag = LBMI_BYTESPERPIX;  tags[3].ti_Data = (ULONG)&bpp;
    tags[4].ti_Tag = LBMI_WIDTH;        tags[4].ti_Data = (ULONG)&bw;
    tags[5].ti_Tag = LBMI_HEIGHT;       tags[5].ti_Data = (ULONG)&bh;
    tags[6].ti_Tag = TAG_DONE;          tags[6].ti_Data = 0;

    lock = cgx_LockBitMapTagList(CyberGfxBase, d->bm, tags);
    if (!lock)
        return FALSE;
    if (!base || fmt != d->fmt || bpp != (Is16(fmt) ? 2UL : 4UL))
    {
        cgx_UnLockBitMap(CyberGfxBase, lock);
        return FALSE;
    }
    if (minx < 0)
        minx = 0;
    if (miny < 0)
        miny = 0;
    if (maxx >= (LONG)bw)
        maxx = (LONG)bw - 1;
    if (maxy >= (LONG)bh)
        maxy = (LONG)bh - 1;
    n = maxx - minx + 1;

    for (y = miny; y <= maxy && n > 0 && Is16(fmt); y++)
    {
        const UBYTE *s = (const UBYTE *)(base + (ULONG)y * bpr +
                                         (ULONG)minx * 2);
        ULONG *t = (ULONG *)(d->buf + (y - d->ly - d->by) * d->mod +
                             (minx - d->lx - d->bx) * 4);
        const ULONG *thi = d->thi, *tlo = d->tlo;
        LONG i;

        if (IsPC(fmt))
            for (i = 0; i < n; i++, s += 2)
                t[i] = thi[s[1]] | tlo[s[0]];
        else
            for (i = 0; i < n; i++, s += 2)
                t[i] = thi[s[0]] | tlo[s[1]];
        d->pixels += n;
    }
    for (y = miny; y <= maxy && n > 0 && Is32(fmt); y++)
    {
        const ULONG *s =(const ULONG *)(base + (ULONG)y * bpr +
                                         (ULONG)minx * 4);
        ULONG *t = (ULONG *)(d->buf + (y - d->ly - d->by) * d->mod +
                             (minx - d->lx - d->bx) * 4);
        LONG i;

        switch (fmt)
        {
        case PIXFMT_RGBA32:
            for (i = 0; i < n; i++)
                t[i] = s[i];
            break;
        case PIXFMT_ARGB32:
            for (i = 0; i < n; i++)
                t[i] = s[i] << 8;
            break;
        default:                /* BGRA32 */
            for (i = 0; i < n; i++)
            {
                ULONG v = s[i];

                t[i] = ((v & 0xFF00) << 16) | (v & 0xFF0000) |
                       ((v >> 16) & 0xFF00);
            }
            break;
        }
        d->pixels += n;
    }
    cgx_UnLockBitMap(CyberGfxBase, lock);
    return TRUE;
}

/* Called by DoHookClipRects() for each part of the layer in the box. */
static ULONG ReadHookFunc(struct Hook *h, struct RastPort *rp,
                          struct AAClipMsg *msg)
{
    struct ReadHook *d = (struct ReadHook *)h;
    struct Layer *l = msg->layer;
    LONG minx, miny, maxx, maxy;

    if (d->fallback)
        return 0;
    /* a backing store (obscured smart refresh part): not here */
    if (!l || !rp || rp->BitMap != d->bm)
    {
        d->fallback = TRUE;
        return 0;
    }
    d->lx = l->bounds.MinX;
    d->ly = l->bounds.MinY;
#ifdef DEBUG
    /* the offsets should agree with the layer position (unverified) */
    if (msg->offsetx != msg->bounds.MinX - d->lx ||
        msg->offsety != msg->bounds.MinY - d->ly)
    {
        if (stat_offset++ < 5)
            kprintf("AAText: directread: hook offsets %ld,%ld, bounds "
                    "%ld,%ld, layer %ld,%ld\n", msg->offsetx, msg->offsety,
                    (LONG)msg->bounds.MinX, (LONG)msg->bounds.MinY,
                    d->lx, d->ly);
    }
#endif

    minx = msg->bounds.MinX;
    miny = msg->bounds.MinY;
    maxx = msg->bounds.MaxX;
    maxy = msg->bounds.MaxY;
    if (minx < d->x0 + d->lx)
        minx = d->x0 + d->lx;
    if (miny < d->y0 + d->ly)
        miny = d->y0 + d->ly;
    if (maxx > d->x1 + d->lx)
        maxx = d->x1 + d->lx;
    if (maxy > d->y1 + d->ly)
        maxy = d->y1 + d->ly;
    if (minx > maxx || miny > maxy)
        return 0;
    if (!ReadRect(d, minx, miny, maxx, maxy))
        d->fallback = TRUE;
    return 0;
}

/* TRUE: everything visible was read directly; *pixels: how many. */
static BOOL ReadDirect(UBYTE *buf, struct RastPort *rp, ULONG fmt, LONG x,
                       LONG y, LONG w, LONG h, LONG sx, LONG sy,
                       ULONG *pixels)
{
    struct ReadHook d;

    d.bm = rp->BitMap;
    d.fmt = fmt;
    d.buf = buf;
    d.mod = w * 4;
    d.bx = x;
    d.by = y;
    d.x0 = x + sx;
    d.y0 = y + sy;
    d.x1 = x + w - 1;
    d.y1 = y + h - 1;
    d.lx = d.ly = 0;
    d.pixels = 0;
    d.fallback = FALSE;
    d.thi = Is16(fmt) ? dr_Tables[fmt] : NULL;
    d.tlo = d.thi ? d.thi + 256 : NULL;

    if (rp->Layer)
    {
        struct Rectangle r;

        r.MinX = d.x0;
        r.MinY = d.y0;
        r.MaxX = d.x1;
        r.MaxY = d.y1;
        d.hook.h_MinNode.mln_Succ = NULL;
        d.hook.h_MinNode.mln_Pred = NULL;
        d.hook.h_Entry = (HOOKFUNC)aa_HookEntry;
        d.hook.h_SubEntry = (HOOKFUNC)(APTR)ReadHookFunc;
        d.hook.h_Data = NULL;
        DoHookClipRects(&d.hook, rp, &r);
    }
    else if (!ReadRect(&d, d.x0, d.y0, d.x1, d.y1))
        d.fallback = TRUE;

    *pixels = d.pixels;
    return !d.fallback;
}

static void ReadRPA(UBYTE *buf, struct RastPort *rp, LONG x, LONG y, LONG w,
                    LONG h, LONG sx, LONG sy)
{
    cgx_ReadPixelArray(CyberGfxBase, buf, sx, sy, w * 4, rp, x + sx, y + sy,
                       w - sx, h - sy, RECTFMT_RGBA);
}

static ULONG Now(void)
{
    struct EClockVal e;

    ReadEClock(&e);
    return e.ev_lo;
}

/* Checksum of the read part of the buffer: the bits of R, G, B in mask. */
static ULONG Sum(const UBYTE *buf, LONG w, LONG h, LONG sx, LONG sy,
                 ULONG mask)
{
    ULONG sum = 0;
    LONG x, y;

    for (y = sy; y < h; y++)
    {
        const ULONG *p = (const ULONG *)(buf + y * w * 4);

        for (x = sx; x < w; x++)
            sum = ((sum << 1) | (sum >> 31)) + (p[x] & mask);
    }
    return sum;
}

/*
 * "directread auto": read the string both ways; direct if 10% faster. FALSE:
 * nothing decided, buf not (completely) read.
 */
static BOOL Measure(UBYTE *buf, struct RastPort *rp, ULONG fmt, LONG x,
                    LONG y, LONG w, LONG h, LONG sx, LONG sy)
{
    ULONG area = (ULONG)(w - sx) * (ULONG)(h - sy);
    ULONG tdirect = ~0UL, trpa = ~0UL, t, pixels, sumdirect;
    LONG i;
    BOOL direct;

    for (i = 0; i < CALIB_RUNS; i++)
    {
        BOOL ok;

        t = Now();
        ok = ReadDirect(buf, rp, fmt, x, y, w, h, sx, sy, &pixels);
        t = Now() - t;
        /* partly hidden or in a backing store: another string */
        if (!ok || pixels != area)
            return FALSE;
        if (t < tdirect)
            tdirect = t;
    }
    sumdirect = Sum(buf, w, h, sx, sy, SumMask(fmt));

    for (i = 0; i < CALIB_RUNS; i++)
    {
        t = Now();
        ReadRPA(buf, rp, x, y, w, h, sx, sy);
        t = Now() - t;
        if (t < trpa)
            trpa = t;
    }

    if (Sum(buf, w, h, sx, sy, SumMask(fmt)) != sumdirect)
    {
        /* drawn over meanwhile, or the formats do not agree */
        D(("AAText: directread: pixel format %ld: pixels differ\n", fmt));
        if (++dr_Mismatch[fmt] >= CALIB_MAX_DIFFER)
            dr_State[fmt] = FMT_RPA;
        return TRUE;
    }
    /* direct only when clearly faster: ReadPixelArray() is the safe way */
    direct = tdirect * 10 < trpa * 9;     /* a tie (0 and 0) is no reason */
    dr_State[fmt] = direct ? FMT_DIRECT : FMT_RPA;
    D(("AAText: directread: pixel format %ld, %ld x %ld: direct %ld, "
       "ReadPixelArray %ld E-clock ticks -> %s\n", fmt, w - sx, h - sy,
       tdirect, trpa, (ULONG)(direct ? "direct" : "ReadPixelArray")));
    return TRUE;
}

void aa_ReadBackground(UBYTE *buf, struct RastPort *rp, LONG x, LONG y,
                       LONG w, LONG h, LONG sx, LONG sy)
{
    if (dr_Mode != AA_DIRECT_OFF && LayersBase && CyberGfxBase &&
        !(rp->Layer && (rp->Layer->Flags & LAYERSUPER)))
    {
        ULONG fmt = cgx_GetCyberMapAttr(CyberGfxBase, rp->BitMap,
                                        CYBRMATTR_PIXFMT);
        ULONG pixels;

        if (Supported(fmt))
        {
            if (dr_Mode == AA_DIRECT_ON || dr_State[fmt] == FMT_DIRECT)
            {
                if (ReadDirect(buf, rp, fmt, x, y, w, h, sx, sy, &pixels))
                {
                    STAT(stat_direct);
                    return;
                }
                STAT(stat_fallback);
            }
            else if (dr_State[fmt] == FMT_UNKNOWN && dr_TimerOpen &&
                     (ULONG)(w - sx) * (ULONG)(h - sy) >= CALIB_MIN_PIXELS &&
                     Measure(buf, rp, fmt, x, y, w, h, sx, sy))
                return;
        }
    }
    ReadRPA(buf, rp, x, y, w, h, sx, sy);
    STAT(stat_rpa);
}

void aa_DirectReconfigure(UBYTE mode)
{
    LONG i;

    if (mode == dr_Mode)
        return;
    for (i = 0; i < NUM_PIXFMT; i++)
    {
        dr_State[i] = FMT_UNKNOWN;
        dr_Mismatch[i] = 0;
    }
    dr_Mode = mode;
}

void aa_DirectInit(UBYTE mode)
{
    dr_Mode = mode;
    LayersBase = OpenLibrary((CONST_STRPTR)"layers.library", 39);
    if (!OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                    (struct IORequest *)&dr_TimerReq, 0))
    {
        dr_TimerOpen = TRUE;
        TimerBase = dr_TimerReq.tr_node.io_Device;
    }
}

void aa_DirectCleanup(void)
{
    LONG i;

    for (i = 0; i <= (LONG)PIXFMT_BGR16PC; i++)
    {
        if (dr_Tables[i])
            FreeVec(dr_Tables[i]);
        dr_Tables[i] = NULL;
        dr_TablesBad[i] = FALSE;
    }
    if (dr_TimerOpen)
        CloseDevice((struct IORequest *)&dr_TimerReq);
    dr_TimerOpen = FALSE;
    if (LayersBase)
        CloseLibrary(LayersBase);
    LayersBase = NULL;
}

#ifdef DEBUG
void aa_PrintDirectStats(void)
{
    static const char *const mode[] = { "auto", "on", "off" };
    static const char *const state[] = { "?", "direct", "RPA" };
    LONG i;

    kprintf("AAText: directread %s: %ld direct, %ld ReadPixelArray, "
            "%ld fell back, %ld odd hook offsets\n",
            (ULONG)mode[dr_Mode <= 2 ? dr_Mode : 0], stat_direct, stat_rpa,
            stat_fallback, stat_offset);
    for (i = 0; i < NUM_PIXFMT; i++)
        if (dr_State[i] != FMT_UNKNOWN || dr_Mismatch[i])
            kprintf("AAText: directread pixel format %ld: %s "
                    "(%ld differences)\n", i, (ULONG)state[dr_State[i]],
                    (ULONG)dr_Mismatch[i]);
}
#endif
