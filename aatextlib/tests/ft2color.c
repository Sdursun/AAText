/*
 * ft2color - draws colour glyphs of a COLR font (e.g. an emoji font)
 * through freetype2.library, to see whether colour fonts work and what
 * a glyph costs.
 *
 *   ft2color FONT <file> [SIZE <pixels>] [CODES <hex,hex,...>] [WAIT <s>]
 *            [LIB <name>]
 *
 * Opens a window on the Workbench screen (RTG, 15 bit or more), loads
 * each code with FT_LOAD_COLOR | FT_LOAD_RENDER and blends the result
 * into the window: half of it over a light, half over a dark background.
 * Prints the pixel mode FreeType returned (BGRA = colour) and the time
 * each glyph took. CODES are Unicode values in hex, because the Shell
 * cannot pass emoji as text.
 *
 * Build: make test (needs ../src/cgx.s of AAText for ReadPixelArray).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/aatext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/cgx.h"

struct Library *AATextBase;
struct Library *CyberGfxBase;
struct Device *TimerBase;

static const char Template[] = "FONT/A,SIZE/K/N,CODES/K,WAIT/K/N,LIB/K";
enum { A_FONT, A_SIZE, A_CODES, A_WAIT, A_LIB, A_COUNT };

/* grinning face, party popper, red heart, thumbs up, rocket, cat face,
   pile of poo, flag of Turkey (two codes: no shaping here, so two
   regional indicator letters), fire, check mark */
static const char DefaultCodes[] =
    "1F600,1F389,2764,1F44D,1F680,1F431,1F4A9,1F1F9,1F1F7,1F525,2705";

static ULONG Micros(ULONG freq)
{
    struct EClockVal e;

    ReadEClock(&e);
    return (ULONG)(((unsigned long long)e.ev_hi << 32 | e.ev_lo) * 1000000ULL
                   / freq);
}

static const char *ModeName(int mode)
{
    switch (mode)
    {
    case FT_PIXEL_MODE_MONO: return "mono";
    case FT_PIXEL_MODE_GRAY: return "gray";
    case FT_PIXEL_MODE_BGRA: return "BGRA (colour)";
    default:                 return "other";
    }
}

/* Blends a FreeType bitmap into the window at x, y (window coordinates).
   BGRA is premultiplied: out = src + dst * (255 - a) / 255. Gray and mono
   are drawn black. */
static void Blend(struct Window *win, FT_Bitmap *bm, LONG x, LONG y)
{
    LONG w = bm->width, h = bm->rows, i, j;
    LONG minx = win->BorderLeft, miny = win->BorderTop;
    LONG maxx = win->Width - win->BorderRight;
    LONG maxy = win->Height - win->BorderBottom;
    UBYTE *buf;

    if (w <= 0 || h <= 0 || x < minx || y < miny || x + w > maxx ||
        y + h > maxy)
        return;
    buf = AllocVec(w * h * 4, MEMF_ANY);
    if (!buf)
        return;
    cgx_ReadPixelArray(CyberGfxBase, buf, 0, 0, w * 4, win->RPort, x, y, w, h,
                       RECTFMT_RGBA);
    for (j = 0; j < h; j++)
    {
        const UBYTE *s = bm->buffer + j * bm->pitch;
        UBYTE *d = buf + j * w * 4;

        for (i = 0; i < w; i++, d += 4)
        {
            ULONG a, r, g, b;

            if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                b = s[i * 4];
                g = s[i * 4 + 1];
                r = s[i * 4 + 2];
                a = s[i * 4 + 3];
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                a = s[i];
                r = g = b = 0;
            }
            else
            {
                a = s[i >> 3] & (0x80 >> (i & 7)) ? 255 : 0;
                r = g = b = 0;
            }
            d[0] = r + d[0] * (255 - a) / 255;
            d[1] = g + d[1] * (255 - a) / 255;
            d[2] = b + d[2] * (255 - a) / 255;
        }
    }
    cgx_WritePixelArray(CyberGfxBase, buf, 0, 0, w * 4, win->RPort, x, y, w, h,
                        RECTFMT_RGBA);
    FreeVec(buf);
}

int main(void)
{
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    struct timerequest tr;
    struct Window *win = NULL;
    FT_Library lib = NULL;
    FT_Face face = NULL;
    const char *libname = "freetype2.library", *p;
    ULONG size, wait, freq, total = 0, n = 0;
    LONG penx, baseline, winw, winh;
    int rc = RETURN_OK;

    rda = ReadArgs((STRPTR)Template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"ft2color");
        return RETURN_FAIL;
    }
    if (args[A_LIB])
        libname = (const char *)args[A_LIB];
    size = args[A_SIZE] ? *(LONG *)args[A_SIZE] : 48;
    wait = args[A_WAIT] ? *(LONG *)args[A_WAIT] : 10;
    p = args[A_CODES] ? (const char *)args[A_CODES] : DefaultCodes;

    AATextBase = OpenLibrary((STRPTR)libname, 0);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    if (!AATextBase || !CyberGfxBase ||
        OpenDevice((STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&tr, 0))
    {
        printf("ft2color: needs %s, cybergraphics.library, timer.device\n",
               libname);
        rc = RETURN_FAIL;
        goto out_libs;
    }
    TimerBase = tr.tr_node.io_Device;
    {
        struct EClockVal e;

        freq = ReadEClock(&e);
    }
    printf("%s %d.%d\n", AATextBase->lib_Node.ln_Name,
           AATextBase->lib_Version, AATextBase->lib_Revision);

    if (FT_Init_FreeType(&lib))
    {
        printf("FT_Init_FreeType failed\n");
        rc = RETURN_FAIL;
        goto out;
    }
    {
        ULONG t0 = Micros(freq);
        FT_Error err = FT_New_Face(lib, (const char *)args[A_FONT], 0, &face);

        if (err)
        {
            printf("FT_New_Face: error %d (%s)\n", err,
                   FT_Error_String(err) ? FT_Error_String(err) : "?");
            rc = RETURN_ERROR;
            goto out;
        }
        printf("font: %s %s, %ld glyphs, opened in %lu ms\n",
               face->family_name ? face->family_name : "?",
               face->style_name ? face->style_name : "",
               (long)face->num_glyphs, (Micros(freq) - t0) / 1000);
    }
    {
        FT_Palette_Data pd;

        printf("colour: %s", FT_HAS_COLOR(face) ? "yes" : "no");
        if (!FT_Palette_Data_Get(face, &pd))
            printf(", %u palette(s) of %u colours", pd.num_palettes,
                   pd.num_palette_entries);
        printf("\n");
    }
    if (FT_Set_Pixel_Sizes(face, 0, size))
    {
        printf("FT_Set_Pixel_Sizes(%lu) failed\n", (unsigned long)size);
        rc = RETURN_ERROR;
        goto out;
    }

    winw = 40 + 12 * (size + size / 4);
    winh = 3 * size;
    win = OpenWindowTags(NULL, WA_Left, 60, WA_Top, 60, WA_InnerWidth, winw,
                         WA_InnerHeight, winh, WA_Title, (ULONG)"ft2color",
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                         WA_Activate, FALSE, TAG_DONE);
    if (!win)
    {
        printf("cannot open a window\n");
        rc = RETURN_ERROR;
        goto out;
    }
    if (GetBitMapAttr(win->RPort->BitMap, BMA_DEPTH) < 15)
    {
        printf("needs an RTG screen with 15 bit or more\n");
        rc = RETURN_ERROR;
        goto out;
    }
    /* light top half, dark bottom half: shows the alpha edges */
    SetAPen(win->RPort, 2);
    RectFill(win->RPort, win->BorderLeft, win->BorderTop,
             win->BorderLeft + winw - 1, win->BorderTop + winh / 2 - 1);
    SetAPen(win->RPort, 1);
    RectFill(win->RPort, win->BorderLeft, win->BorderTop + winh / 2,
             win->BorderLeft + winw - 1, win->BorderTop + winh - 1);

    penx = win->BorderLeft + 20;
    baseline = win->BorderTop + winh / 2 + size / 3;
    printf("code     glyph  mode            size     render us\n");
    while (*p)
    {
        char *end;
        ULONG code = strtoul(p, &end, 16), t0, t1;
        FT_UInt gi;
        FT_Error err;

        if (end == p)
            break;
        p = *end ? end + 1 : end;
        gi = FT_Get_Char_Index(face, code);
        t0 = Micros(freq);
        err = FT_Load_Glyph(face, gi, FT_LOAD_COLOR | FT_LOAD_RENDER);
        t1 = Micros(freq);
        if (err)
        {
            printf("U+%04lX %5u  error %d (%s)\n", (unsigned long)code, gi,
                   err, FT_Error_String(err) ? FT_Error_String(err) : "?");
            continue;
        }
        printf("U+%04lX %5u  %-14s %3ux%-3u %9lu\n", (unsigned long)code, gi,
               ModeName(face->glyph->bitmap.pixel_mode),
               face->glyph->bitmap.width, face->glyph->bitmap.rows,
               (unsigned long)(t1 - t0));
        total += t1 - t0;
        n++;
        Blend(win, &face->glyph->bitmap, penx + face->glyph->bitmap_left,
              baseline - face->glyph->bitmap_top);
        penx += (face->glyph->advance.x >> 6) + size / 4;
    }
    if (n)
        printf("average %lu us per glyph (%lu glyphs, %lu px)\n",
               (unsigned long)(total / n), (unsigned long)n,
               (unsigned long)size);
    Delay(wait * 50);

out:
    if (win)
        CloseWindow(win);
    if (face)
        FT_Done_Face(face);
    if (lib)
        FT_Done_FreeType(lib);
    CloseDevice((struct IORequest *)&tr);
out_libs:
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    if (AATextBase)
        CloseLibrary(AATextBase);
    FreeArgs(rda);
    return rc;
}
