/*
 * lcdtest - stage 0 of subpixel (LCD) antialiasing: draws the same line
 * with FreeType in a window on an RTG screen, as
 *
 *   gray   grayscale coverage (what AAText does now)
 *   RGB    LCD coverage, subpixels red-green-blue (FreeType "Harmony")
 *   BGR    the same with red and blue swapped (BGR panels)
 *   RGB f, BGR f   the same through FreeType's default LCD filter
 *          (FIR 0x08 0x4D 0x56 0x4D 0x08 over the subpixels), which
 *          our FreeType build leaves out, applied here
 *
 * on three backgrounds (the window's, white, dark), so both can be
 * compared on the screen they are meant for.
 *
 *   lcdtest [FONT <file.ttf>] [SIZE <pixels>] [SECONDS <n>] [GAMMA <n>]
 *
 * FONT: a TrueType/OpenType file; without it the first of DejaVu Sans,
 * Verdana, Tahoma, Arial in FONTS:_ttf/ is used. SIZE: pixel size
 * (default 13). SECONDS: close after that many seconds (default 0:
 * only with the close gadget or Ctrl-C). GAMMA: gamma * 100 as in
 * AAText.prefs (default 80). Needs an RTG screen (15 bit or more).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <dos/rdargs.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "cgx.h"

struct Library *CyberGfxBase;

#define W      760
#define LINEH  (px * 3 / 2)

static const char *const sample[] =
{
    "The quick brown fox jumps over the lazy dog 0123456789",
    "Workbench  Pijamal\xC4\xB1 hasta ya\xC4\x9F\xC4\xB1z \xC5\x9F" "of\xC3\xB6re "
        "\xC3\xA7" "abucak g\xC3\xBCvendi  WAVE illustrate mmm",
};

static UBYTE lut[256];

/* the next UTF-8 character of s (the sample texts are UTF-8) */
static ULONG NextChar(const UBYTE **s)
{
    const UBYTE *p = *s;
    ULONG c = *p++;

    if (c >= 0xE0)
    {
        c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
    }
    else if (c >= 0xC0)
    {
        c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
        p++;
    }
    *s = p;
    return c;
}

/* Blend coverage (0..255, already gamma corrected) of one channel */
static UBYTE Mix(UBYTE bg, UBYTE fg, UBYTE cov)
{
    return bg + (((LONG)fg - bg) * cov + 127) / 255;
}

/*
 * FreeType's default LCD filter over one row of subpixels: src has n
 * values, dst gets n + 4 (two more on each side, where the filter
 * spreads them).
 */
static void Filter(const UBYTE *src, LONG n, UBYTE *dst)
{
    static const UBYTE wt[5] = { 0x08, 0x4D, 0x56, 0x4D, 0x08 };
    LONG i, k;

    for (i = 0; i < n + 4; i++)
    {
        ULONG v = 0;

        for (k = 0; k < 5; k++)
        {
            LONG j = i - 4 + k;

            if (j >= 0 && j < n)
                v += src[j] * wt[k];
        }
        v >>= 8;
        dst[i] = v > 255 ? 255 : v;
    }
}

/*
 * One line into buf (w x h RGB, filled with bg): mode 0 gray, 1 RGB,
 * 2 BGR, 3 RGB filtered, 4 BGR filtered.
 */
static void DrawLine(FT_Face face, UBYTE *buf, LONG w, LONG h, LONG base,
                     const char *text, int mode, const UBYTE *fg)
{
    const UBYTE *s = (const UBYTE *)text;
    LONG pen = 4;

    while (*s)
    {
        ULONG c = NextChar(&s);
        FT_GlyphSlot g = face->glyph;
        FT_Bitmap *bm;
        LONG x, y;

        if (FT_Load_Char(face, c, FT_LOAD_TARGET_LIGHT) ||
            FT_Render_Glyph(g, mode ? FT_RENDER_MODE_LCD :
                                      FT_RENDER_MODE_NORMAL))
            continue;
        bm = &g->bitmap;
        for (y = 0; y < (LONG)bm->rows; y++)
        {
            LONG py = base - g->bitmap_top + y;
            const UBYTE *src = bm->buffer + y * bm->pitch;

            if (py < 0 || py >= h)
                continue;
            if (mode == 0)
            {
                for (x = 0; x < (LONG)bm->width; x++)
                {
                    LONG xx = pen + g->bitmap_left + x;
                    UBYTE a = lut[src[x]], *d;

                    if (xx < 0 || xx >= w || !a)
                        continue;
                    d = buf + (py * w + xx) * 3;
                    d[0] = Mix(d[0], fg[0], a);
                    d[1] = Mix(d[1], fg[1], a);
                    d[2] = Mix(d[2], fg[2], a);
                }
            }
            else
            {
                static UBYTE row[3 * 512 + 8];
                LONG n = bm->width, off = 0;

                if (mode >= 3 && n < 3 * 512)
                {
                    /* 2 subpixels more on each side: start 1 pixel
                       left, ending 1 pixel right */
                    row[0] = 0;
                    Filter(src, n, row + 1);
                    row[n + 5] = 0;
                    src = row;
                    n += 6;
                    off = -1;
                }
                for (x = 0; x < n / 3; x++)
                {
                    LONG xx = pen + g->bitmap_left + off + x;
                    UBYTE r = lut[src[x * 3]], gg = lut[src[x * 3 + 1]],
                          b = lut[src[x * 3 + 2]], *d;

                    if (xx < 0 || xx >= w || !(r | gg | b))
                        continue;
                    if (mode == 2 || mode == 4)     /* BGR panel */
                    {
                        UBYTE t = r;

                        r = b;
                        b = t;
                    }
                    d = buf + (py * w + xx) * 3;
                    d[0] = Mix(d[0], fg[0], r);
                    d[1] = Mix(d[1], fg[1], gg);
                    d[2] = Mix(d[2], fg[2], b);
                }
            }
        }
        pen += (g->advance.x + 32) >> 6;
    }
}

static const char version[] __attribute__((used)) =
    "$VER: lcdtest 1.0 (14.10.2026)";

/* fonts tried without FONT */
static const char *const fonts[] =
{
    "FONTS:_ttf/DejaVuSans.ttf", "FONTS:_ttf/verdana.ttf",
    "FONTS:_ttf/tahoma.ttf", "FONTS:_ttf/arial.ttf", NULL
};

int main(int argc, char **argv)
{
    static const char *const modename[] = { "gray", "RGB", "BGR", "RGB f",
                                            "BGR f" };
    FT_Library lib;
    FT_Face face;
    struct Window *win;
    struct Screen *scr;
    UBYTE *buf, bgs[3][3], fgs[3][3];
    ULONG c[3];
    LONG px, secs, gamma100, i, k, m, y, h;
    UBYTE *fontdata = NULL;
    long fontlen = 0;
    LONG args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rda;
    const char *fontpath = NULL;

    (void)argc;
    (void)argv;
    if (!(rda = ReadArgs((CONST_STRPTR)"FONT,SIZE/N,SECONDS/N,GAMMA/N", args,
                         NULL)))
    {
        PrintFault(IoErr(), (CONST_STRPTR)"lcdtest");
        return 10;
    }
    px = args[1] ? *(LONG *)args[1] : 13;
    secs = args[2] ? *(LONG *)args[2] : 0;
    gamma100 = args[3] ? *(LONG *)args[3] : 80;
    if (px < 6 || px > 40)
        px = 13;
    if (gamma100 < 50 || gamma100 > 400)
        gamma100 = 80;
    for (i = 0; i < 256; i++)
        lut[i] = (UBYTE)(pow(i / 255.0, 100.0 / gamma100) * 255.0 + 0.5);

    if (!(CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40)))
    {
        printf("needs cybergraphics.library (an RTG screen)\n");
        return 10;
    }
    /* the whole file in memory: FreeType's file streams are not built */
    {
        FILE *f = NULL;

        if (args[0])
            f = fopen(fontpath = (const char *)args[0], "rb");
        else
            for (i = 0; fonts[i] && !f; i++)
                f = fopen(fontpath = fonts[i], "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            fontlen = ftell(f);
            fseek(f, 0, SEEK_SET);
            if ((fontdata = malloc(fontlen)))
                fread(fontdata, 1, fontlen, f);
            fclose(f);
        }
    }
    if (!fontdata || FT_Init_FreeType(&lib) ||
        FT_New_Memory_Face(lib, fontdata, fontlen, 0, &face) ||
        FT_Set_Pixel_Sizes(face, 0, px))
    {
        printf("cannot open the font %s\n",
               fontpath ? fontpath : "(none of the default ones found)");
        CloseLibrary(CyberGfxBase);
        FreeArgs(rda);
        return 10;
    }

    h = 3 * (5 * 2 * LINEH + 8);
    win = OpenWindowTags(NULL, WA_Left, 10, WA_Top, 30, WA_InnerWidth, W + 70,
                         WA_InnerHeight, h + 4, WA_Title, (ULONG)fontpath,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                         WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW,
                         TAG_DONE);
    if (!win)
    {
        printf("cannot open the window (too high for the screen? try a "
               "smaller SIZE)\n");
        CloseLibrary(CyberGfxBase);
        FreeArgs(rda);
        return 20;
    }
    scr = win->WScreen;

    /* backgrounds: the window's pens, black on white, white on dark */
    GetRGB32(scr->ViewPort.ColorMap, 0, 1, c);
    for (i = 0; i < 3; i++)
        bgs[0][i] = c[i] >> 24;
    GetRGB32(scr->ViewPort.ColorMap, 1, 1, c);
    for (i = 0; i < 3; i++)
        fgs[0][i] = c[i] >> 24;
    memset(bgs[1], 255, 3);
    memset(fgs[1], 0, 3);
    bgs[2][0] = 0x20; bgs[2][1] = 0x28; bgs[2][2] = 0x38;
    memset(fgs[2], 0xF0, 3);

    buf = AllocVec(W * 2 * LINEH * 3, MEMF_ANY);
    SetAPen(win->RPort, 1);
    y = win->BorderTop + 2;
    for (k = 0; k < 3; k++)
    {
        for (m = 0; m < 5; m++)
        {
            LONG p;

            for (p = 0; p < W * 2 * LINEH; p++)
                memcpy(buf + p * 3, bgs[k], 3);
            DrawLine(face, buf, W, 2 * LINEH, px + px / 3, sample[0], m,
                     fgs[k]);
            DrawLine(face, buf, W, 2 * LINEH, LINEH + px + px / 3,
                     sample[1], m, fgs[k]);
            cgx_WritePixelArray(CyberGfxBase, buf, 0, 0, W * 3, win->RPort,
                                win->BorderLeft + 66, y, W, 2 * LINEH,
                                RECTFMT_RGB);
            Move(win->RPort, win->BorderLeft + 4, y + LINEH);
            Text(win->RPort, (CONST_STRPTR)modename[m],
                 strlen(modename[m]));
            y += 2 * LINEH;
        }
        y += 8;
    }

    {
        ULONG sigs, mask = (1UL << win->UserPort->mp_SigBit) |
                           SIGBREAKF_CTRL_C;
        LONG t;

        for (t = 0; !secs || t < secs * 10; t++)
        {
            struct IntuiMessage *im;
            BOOL done = FALSE;

            Delay(5);
            sigs = SetSignal(0, mask);
            while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)))
            {
                done = im->Class == IDCMP_CLOSEWINDOW;
                ReplyMsg((struct Message *)im);
            }
            if (done || (sigs & SIGBREAKF_CTRL_C))
                break;
        }
    }

    FreeVec(buf);
    CloseWindow(win);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    CloseLibrary(CyberGfxBase);
    FreeArgs(rda);
    return 0;
}
