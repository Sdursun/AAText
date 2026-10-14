/*
 * AATextManager - preview of a font file before it is installed.
 * See preview.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <proto/aatext.h>

#include "preview.h"
#include "../cgx.h"
#include "../charsets.h"
#include "../prefs.h"

static struct Library *CyberGfxBase;
static UBYTE *cov;              /* coverage 0-255, PV_WIDTH x PV_HEIGHT */
static UBYTE *rgb;              /* RTG: 3 bytes a pixel (with CGX only) */
static UBYTE *pens;             /* palette screens: a pen per pixel */
static struct BitMap *tbm;      /* WritePixelLine8()'s one line bitmap */
static enum { PV_NONE, PV_RGB, PV_PAL } mode;

/* coverage steps on palette screens, as AAText's "aga on" */
#define PAL_STEPS 3

/* sample lines: pixel size and which text (0 alphabet, 1 sentence) */
static const struct { UBYTE size, sentence; } lines[] =
{
    { 14, 0 }, { 10, 1 }, { 14, 1 }, { 20, 1 }, { 28, 1 }
};
#define NUM_LINES (sizeof(lines) / sizeof(lines[0]))
#define GAP 3

BOOL pv_Init(void)
{
    /* cybergraphics.library only for RTG screens: palette screens need
       nothing but graphics.library */
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (CyberGfxBase)
        rgb = AllocVec(PV_WIDTH * PV_HEIGHT * 3, MEMF_ANY);
    cov = AllocVec(PV_WIDTH * PV_HEIGHT, MEMF_ANY);
    pens = AllocVec(PV_WIDTH * PV_HEIGHT, MEMF_ANY);
    return cov && pens;
}

void pv_Cleanup(void)
{
    if (tbm)
        FreeBitMap(tbm);
    if (pens)
        FreeVec(pens);
    if (cov)
        FreeVec(cov);
    if (rgb)
        FreeVec(rgb);
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    tbm = NULL;
    pens = cov = rgb = NULL;
    CyberGfxBase = NULL;
}

LONG pv_NeededHeight(void)
{
    LONG i, h = GAP;

    for (i = 0; i < (LONG)NUM_LINES; i++)
        h += lines[i].size * 3 / 2 + GAP;   /* line height of most fonts */
    return h < PV_HEIGHT ? h : PV_HEIGHT;
}

/*
 * Next character of s as Unicode: the texts come from the catalog,
 * which tools/mkcatalog.pl writes in ISO-8859-9 (English is ASCII)
 */
static ULONG NextChar(const UBYTE **s)
{
    return aa_CharsetToUnicode(AA_CHARSET_LATIN5, *(*s)++);
}

static ULONG PenRGB(struct Screen *scr, UBYTE pen)
{
    ULONG c[3];

    GetRGB32(scr->ViewPort.ColorMap, pen, 1, c);
    return ((c[0] >> 24) << 16) | ((c[1] >> 24) << 8) | (c[2] >> 24);
}

/* one glyph's coverage added at pen position x, baseline y (neighbouring
   glyphs may overlap) */
static void AddGlyph(FT_GlyphSlot g, LONG x, LONG y)
{
    FT_Bitmap *b = &g->bitmap;
    LONG r, c, px, py;

    for (r = 0; r < (LONG)b->rows; r++)
    {
        py = y - g->bitmap_top + r;
        if (py < 0 || py >= PV_HEIGHT)
            continue;
        for (c = 0; c < (LONG)b->width; c++)
        {
            LONG a = b->buffer[r * b->pitch + c], v;
            UBYTE *d;

            px = x + g->bitmap_left + c;
            if (!a || px < 0 || px >= PV_WIDTH)
                continue;
            d = cov + py * PV_WIDTH + px;
            v = *d + a;
            *d = v > 255 ? 255 : v;
        }
    }
}

/*
 * The pen for bg + (fg - bg) * step / PAL_STEPS: the nearest palette
 * colour if it is within a quarter of the bg-fg distance of the mix,
 * else no mix (as AAText's MixPen() in render.c).
 */
static UBYTE MixPen(struct ColorMap *cm, const ULONG *pal, LONG ncol,
                    UBYTE bgpen, UBYTE fgpen, LONG step)
{
    ULONG c[3];
    LONG mix[3], span = 0, dist = 0, i;
    UBYTE pen;

    for (i = 0; i < 3; i++)
    {
        LONG b = pal[bgpen * 3 + i] >> 24;
        LONG f = pal[fgpen * 3 + i] >> 24;

        mix[i] = b + (f - b) * step / PAL_STEPS;
        c[i] = (ULONG)mix[i] * 0x01010101;
        span += (f - b) * (f - b);
    }
    pen = (UBYTE)FindColor(cm, c[0], c[1], c[2], ncol - 1);
    for (i = 0; i < 3; i++)
    {
        LONG d = (LONG)(pal[pen * 3 + i] >> 24) - mix[i];

        dist += d * d;
    }
    if (dist * 16 > span)
        return step * 2 >= PAL_STEPS ? fgpen : bgpen;
    return pen;
}

/* coverage -> pens for a palette screen of depth bits; FALSE: no memory */
static BOOL ToPens(struct Screen *scr, LONG depth, UBYTE bgpen, UBYTE fgpen)
{
    struct ColorMap *cm = scr->ViewPort.ColorMap;
    LONG ncol = 1L << depth, i;
    UBYTE step[PAL_STEPS + 1];
    ULONG *pal;

    if (ncol > cm->Count)
        ncol = cm->Count;
    if (bgpen >= ncol || fgpen >= ncol ||
        !(pal = AllocVec(ncol * 3 * sizeof(ULONG), MEMF_ANY)))
        return FALSE;
    GetRGB32(cm, 0, ncol, pal);
    step[0] = bgpen;
    for (i = 1; i < PAL_STEPS; i++)
        step[i] = MixPen(cm, pal, ncol, bgpen, fgpen, i);
    step[PAL_STEPS] = fgpen;
    FreeVec(pal);

    for (i = 0; i < PV_WIDTH * PV_HEIGHT; i++)
        pens[i] = step[(cov[i] * PAL_STEPS + 127) / 255];

    /* one line bitmap of the screen's depth for WritePixelLine8() */
    if (tbm && GetBitMapAttr(tbm, BMA_DEPTH) != (ULONG)depth)
    {
        FreeBitMap(tbm);
        tbm = NULL;
    }
    if (!tbm)
        tbm = AllocBitMap(PV_WIDTH, 1, depth, 0, NULL);
    return tbm != NULL;
}

/* coverage -> RGB between bg and fg (0xRRGGBB) */
static void ToRGB(ULONG fg, ULONG bg)
{
    LONG i, k;

    for (i = 0; i < PV_WIDTH * PV_HEIGHT; i++)
        for (k = 0; k < 3; k++)
        {
            LONG f = (fg >> (16 - 8 * k)) & 0xFF;
            LONG b = (bg >> (16 - 8 * k)) & 0xFF;

            rgb[i * 3 + k] = b + (f - b) * cov[i] / 255;
        }
}

/* the whole file in memory (aatext.library has streams, but the
   buffer keeps this the same as fontinfo.c) */
static UBYTE *LoadFile(const char *path, LONG *len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG n;

    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n > 0 && (buf = AllocVec(n, MEMF_ANY)) && Read(fh, buf, n) != n)
    {
        FreeVec(buf);
        buf = NULL;
    }
    Close(fh);
    *len = n;
    return buf;
}

BOOL pv_Render(struct Screen *scr, const char *path, LONG facenum,
               const char *alphabet, const char *sentence)
{
    FT_Library lib;
    FT_Face face;
    UBYTE *file, fgpen = 1, bgpen = 0;
    LONG len, i, y = GAP, depth;

    mode = PV_NONE;
    if (!cov || !path)
        return FALSE;
    if (!(file = LoadFile(path, &len)))
        return FALSE;
    if (FT_Init_FreeType(&lib))
    {
        FreeVec(file);
        return FALSE;
    }
    if (FT_New_Memory_Face(lib, file, len, facenum, &face))
    {
        FT_Done_FreeType(lib);
        FreeVec(file);
        return FALSE;
    }

    {
        struct DrawInfo *dri = GetScreenDrawInfo(scr);

        if (dri)
        {
            fgpen = dri->dri_Pens[TEXTPEN];
            bgpen = dri->dri_Pens[BACKGROUNDPEN];
            FreeScreenDrawInfo(scr, dri);
        }
    }
    memset(cov, 0, PV_WIDTH * PV_HEIGHT);

    for (i = 0; i < (LONG)NUM_LINES && y < PV_HEIGHT; i++)
    {
        const UBYTE *s;
        char label[8];
        LONG x = 4, pass;

        if (FT_Set_Pixel_Sizes(face, 0, lines[i].size))
            break;
        y += (face->size->metrics.ascender + 63) >> 6;
        label[0] = '0' + lines[i].size / 10;
        label[1] = '0' + lines[i].size % 10;
        label[2] = ' ';
        label[3] = ' ';
        label[4] = 0;
        for (pass = 0; pass < 2; pass++)
        {
            s = (const UBYTE *)(!pass ? label :
                                lines[i].sentence ? sentence : alphabet);
            while (*s && x < PV_WIDTH)
            {
                ULONG c = NextChar(&s);

                if (FT_Load_Char(face, c, FT_LOAD_RENDER |
                                 FT_LOAD_TARGET_LIGHT))
                    continue;
                AddGlyph(face->glyph, x, y);
                x += (face->glyph->advance.x + 32) >> 6;
            }
        }
        y += ((-face->size->metrics.descender + 63) >> 6) + GAP;
    }
    /* palette screens: pens; RTG: RGB */
    depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    if (depth <= 8)
    {
        if (ToPens(scr, depth, bgpen, fgpen))
            mode = PV_PAL;
    }
    else if (rgb && depth >= 15)
    {
        ToRGB(PenRGB(scr, fgpen), PenRGB(scr, bgpen));
        mode = PV_RGB;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    FreeVec(file);
    return TRUE;
}

void pv_Draw(struct RastPort *rp, LONG left, LONG top, LONG width,
             LONG height, UBYTE bgpen, UBYTE textpen, const char *notrtg)
{
    LONG depth = rp->BitMap ? GetBitMapAttr(rp->BitMap, BMA_DEPTH) : 0;
    BOOL rtg = CyberGfxBase && rgb && depth >= 15 &&
               cgx_GetCyberMapAttr(CyberGfxBase, rp->BitMap,
                                   CYBRMATTR_ISCYBERGFX);
    LONG w = width < PV_WIDTH ? width : PV_WIDTH;
    LONG h = height < PV_HEIGHT ? height : PV_HEIGHT;
    LONG r;

    if (width <= 0 || height <= 0)
        return;
    SetAPen(rp, bgpen);
    RectFill(rp, left, top, left + width - 1, top + height - 1);
    if (depth > 8 && !rtg)
    {
        /* 9-14 bit, or true colour without cybergraphics.library */
        SetAPen(rp, textpen);
        SetDrMd(rp, JAM1);
        Move(rp, left + 4, top + 4 + rp->TxBaseline);
        Text(rp, (CONST_STRPTR)notrtg, strlen(notrtg));
        return;
    }
    if (mode == PV_RGB && rtg)
        cgx_WritePixelArray(CyberGfxBase, rgb, 0, 0, PV_WIDTH * 3, rp,
                            left, top, w, h, RECTFMT_RGB);
    else if (mode == PV_PAL && depth <= 8 &&
             GetBitMapAttr(tbm, BMA_DEPTH) == (ULONG)depth)
    {
        struct RastPort temprp;

        InitRastPort(&temprp);
        temprp.BitMap = tbm;
        for (r = 0; r < h; r++)
            WritePixelLine8(rp, left, top + r, w, pens + r * PV_WIDTH,
                            &temprp);
    }
}
