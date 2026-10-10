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
static UBYTE *rgb;              /* PV_WIDTH x PV_HEIGHT, 3 bytes a pixel */
static BOOL drawn;              /* rgb holds a font */

/* sample lines: pixel size and which text (0 alphabet, 1 sentence) */
static const struct { UBYTE size, sentence; } lines[] =
{
    { 14, 0 }, { 10, 1 }, { 14, 1 }, { 20, 1 }, { 28, 1 }
};
#define NUM_LINES (sizeof(lines) / sizeof(lines[0]))
#define GAP 3

BOOL pv_Init(void)
{
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (CyberGfxBase)
        rgb = AllocVec(PV_WIDTH * PV_HEIGHT * 3, MEMF_ANY);
    return CyberGfxBase && rgb;
}

void pv_Cleanup(void)
{
    if (rgb)
        FreeVec(rgb);
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    rgb = NULL;
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

/* one glyph bitmap blended into the buffer at pen position x, baseline y */
static void Blend(FT_GlyphSlot g, LONG x, LONG y, ULONG fg, ULONG bg)
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
            UBYTE *d;
            ULONG a = b->buffer[r * b->pitch + c];
            LONG i;

            px = x + g->bitmap_left + c;
            if (!a || px < 0 || px >= PV_WIDTH)
                continue;
            d = rgb + (py * PV_WIDTH + px) * 3;
            for (i = 0; i < 3; i++)
            {
                LONG f = (fg >> (16 - 8 * i)) & 0xFF;
                LONG k = (bg >> (16 - 8 * i)) & 0xFF;

                /* add to what is there: neighbouring glyphs may overlap */
                LONG v = d[i] + (f - k) * (LONG)a / 255;

                d[i] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
        }
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
    ULONG fg, bg;
    UBYTE *file;
    LONG len, i, y = GAP;

    drawn = FALSE;
    if (!rgb || !path)
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

        fg = PenRGB(scr, dri ? dri->dri_Pens[TEXTPEN] : 1);
        bg = PenRGB(scr, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
        if (dri)
            FreeScreenDrawInfo(scr, dri);
    }
    for (i = 0; i < PV_WIDTH * PV_HEIGHT; i++)
    {
        rgb[i * 3] = bg >> 16;
        rgb[i * 3 + 1] = bg >> 8;
        rgb[i * 3 + 2] = bg;
    }

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
                Blend(face->glyph, x, y, fg, bg);
                x += (face->glyph->advance.x + 32) >> 6;
            }
        }
        y += ((-face->size->metrics.descender + 63) >> 6) + GAP;
    }
    drawn = TRUE;

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    FreeVec(file);
    return TRUE;
}

void pv_Draw(struct RastPort *rp, LONG left, LONG top, LONG width,
             LONG height, UBYTE bgpen, UBYTE textpen, const char *notrtg)
{
    BOOL rtg = CyberGfxBase && rp->BitMap &&
               cgx_GetCyberMapAttr(CyberGfxBase, rp->BitMap,
                                   CYBRMATTR_ISCYBERGFX) &&
               cgx_GetCyberMapAttr(CyberGfxBase, rp->BitMap,
                                   CYBRMATTR_DEPTH) >= 15;
    LONG w = width < PV_WIDTH ? width : PV_WIDTH;
    LONG h = height < PV_HEIGHT ? height : PV_HEIGHT;

    if (width <= 0 || height <= 0)
        return;
    SetAPen(rp, bgpen);
    RectFill(rp, left, top, left + width - 1, top + height - 1);
    if (!rtg)
    {
        SetAPen(rp, textpen);
        SetDrMd(rp, JAM1);
        Move(rp, left + 4, top + 4 + rp->TxBaseline);
        Text(rp, (CONST_STRPTR)notrtg, strlen(notrtg));
        return;
    }
    if (drawn)
        cgx_WritePixelArray(CyberGfxBase, rgb, 0, 0, PV_WIDTH * 3, rp,
                            left, top, w, h, RECTFMT_RGB);
}
