/*
 * pxbench - how long cybergraphics.library takes to read and write a
 * text-line sized rectangle in each pixel array format AAText could use.
 * AAText reads the background with ReadPixelArray(), blends, and writes
 * it back with WritePixelArray(); this shows what the format costs.
 *
 *   pxbench [W <n>] [H <n>] [SECS <n>]
 *
 * Opens a window on the Workbench screen (run AAText or not, it does not
 * matter: no text is drawn) and prints microseconds per call.
 *
 * Build: m68k-amigaos-gcc -m68020 -O2 -noixemul -o pxbench tests/pxbench.c src/cgx.s -lm
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

#include <stdio.h>

#include "../src/cgx.h"

struct Library *CyberGfxBase;
struct Device *TimerBase;

static double Now(ULONG freq)
{
    struct EClockVal e;

    ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + e.ev_lo) / freq;
}

int main(void)
{
    static const char template[] = "W/K/N,H/K/N,SECS/K/N";
    static const struct { const char *name; ULONG fmt, bpp; } fmts[] =
    {
        { "RGB",  RECTFMT_RGB,  3 },
        { "ARGB", RECTFMT_ARGB, 4 },
        { "RGBA", RECTFMT_RGBA, 4 }
    };
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rda;
    struct timerequest tr;
    struct Window *win;
    ULONG freq, w, h, secs, f;
    UBYTE *buf;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (!rda)
        return RETURN_FAIL;
    w = args[0] ? *(LONG *)args[0] : 400;
    h = args[1] ? *(LONG *)args[1] : 20;
    secs = args[2] ? *(LONG *)args[2] : 1;
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (!CyberGfxBase ||
        OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&tr, 0))
    {
        printf("pxbench: needs cybergraphics.library and timer.device\n");
        return RETURN_FAIL;
    }
    TimerBase = tr.tr_node.io_Device;
    {
        struct EClockVal e;

        freq = ReadEClock(&e);
    }
    buf = AllocVec(w * h * 4, MEMF_ANY | MEMF_CLEAR);
    win = OpenWindowTags(NULL, WA_Left, 40, WA_Top, 40, WA_InnerWidth, w + 20,
                         WA_InnerHeight, h + 20, WA_Title, (ULONG)"pxbench",
                         WA_Activate, FALSE, TAG_DONE);
    if (!buf || !win)
        goto out;
    SetAPen(win->RPort, 2);
    RectFill(win->RPort, win->BorderLeft, win->BorderTop,
             win->BorderLeft + w + 19, win->BorderTop + h + 19);
    printf("%lu x %lu pixels, %lu s per test, screen depth %lu\n",
           (unsigned long)w, (unsigned long)h, (unsigned long)secs,
           (unsigned long)GetBitMapAttr(win->RPort->BitMap, BMA_DEPTH));
    printf("format   read us   write us\n");
    for (f = 0; f < sizeof(fmts) / sizeof(fmts[0]); f++)
    {
        ULONG n, mod = w * fmts[f].bpp;
        LONG x = win->BorderLeft + 10, y = win->BorderTop + 10;
        double t0, t1, rd, wr;

        for (n = 0, t0 = t1 = Now(freq); t1 - t0 < secs; n++)
        {
            cgx_ReadPixelArray(CyberGfxBase, buf, 0, 0, mod, win->RPort, x,
                               y, w, h, fmts[f].fmt);
            if ((n & 15) == 15)
                t1 = Now(freq);
        }
        rd = (Now(freq) - t0) * 1e6 / n;
        for (n = 0, t0 = t1 = Now(freq); t1 - t0 < secs; n++)
        {
            cgx_WritePixelArray(CyberGfxBase, buf, 0, 0, mod, win->RPort, x,
                                y, w, h, fmts[f].fmt);
            if ((n & 15) == 15)
                t1 = Now(freq);
        }
        wr = (Now(freq) - t0) * 1e6 / n;
        printf("%-6s %9.1f %10.1f\n", fmts[f].name, rd, wr);
    }
out:
    if (win)
        CloseWindow(win);
    if (buf)
        FreeVec(buf);
    CloseDevice((struct IORequest *)&tr);
    CloseLibrary(CyberGfxBase);
    FreeArgs(rda);
    return RETURN_OK;
}
