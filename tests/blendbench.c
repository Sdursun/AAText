/*
 * blendbench - the ClearType blend loop of src/render.c (BlendLCD())
 * with a 3 byte (RGB) and a 4 byte (RGBA) destination buffer, on the
 * same data, to see what the pixel size costs by itself.
 *
 *   blendbench [SECS <n>]
 *
 * Build: m68k-amigaos-gcc -m68020 -O2 -fomit-frame-pointer -noixemul -o blendbench tests/blendbench.c -lm
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <stdio.h>

#define LEVELS 32
#define GW     12               /* glyph width, rows: a 16 px letter */
#define GH     16
#define BW     400              /* buffer: a text line */
#define BH     20

struct Device *TimerBase;
static UBYTE lv[256];
static UBYTE tab[3][LEVELS + 1][256];
static UBYTE glyph[GW * GH * 3];

static double Now(ULONG freq)
{
    struct EClockVal e;

    ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + e.ev_lo) / freq;
}

/* the loop as in BlendLCD(), bpp bytes per buffer pixel */
static void __attribute__((noinline)) Blend(UBYTE *buf, LONG bpp, LONG gx)
{
    const UBYTE (*tr)[256] = tab[0];
    const UBYTE (*tg)[256] = tab[1];
    const UBYTE (*tb)[256] = tab[2];
    LONG stride = BW * bpp, gstride = GW * 3, y;
    const UBYTE *srow = glyph;
    UBYTE *drow = buf + 2 * stride;

    for (y = 0; y < GH; y++, srow += gstride, drow += stride)
    {
        LONG n = GW;
        const UBYTE *src = srow;
        UBYTE *dst = drow + gx * bpp;

        for (; n > 0; n--, src += 3, dst += bpp)
        {
            LONG l;

            if (!(src[0] | src[1] | src[2]))
                continue;
            if ((l = lv[src[0]]))
                dst[0] = tr[l][dst[0]];
            if ((l = lv[src[1]]))
                dst[1] = tg[l][dst[1]];
            if ((l = lv[src[2]]))
                dst[2] = tb[l][dst[2]];
        }
    }
}

int main(void)
{
    static const char template[] = "SECS/K/N";
    LONG args[1] = { 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    struct timerequest tr;
    ULONG freq, secs = 2, i, c, v, l, bpp;
    UBYTE *buf;

    if (rda && args[0])
        secs = *(LONG *)args[0];
    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&tr, 0))
        return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;
    {
        struct EClockVal e;

        freq = ReadEClock(&e);
    }
    for (i = 0; i < 256; i++)
        lv[i] = (i * LEVELS + 127) / 255;
    for (c = 0; c < 3; c++)
        for (l = 0; l <= LEVELS; l++)
            for (v = 0; v < 256; v++)
                tab[c][l][v] = v + (((LONG)(20 * c) - (LONG)v) * (LONG)l) / LEVELS;
    for (i = 0; i < sizeof(glyph); i++)     /* a letter: ink in the middle */
        glyph[i] = ((i / 3) % GW > 2 && (i / 3) % GW < 9) ? (i * 37) & 255 : 0;
    buf = AllocVec(BW * BH * 4, MEMF_ANY | MEMF_CLEAR);
    if (!buf)
        return RETURN_FAIL;
    for (i = 0; i < BW * BH * 4; i++)
        buf[i] = 170;

    for (bpp = 3; bpp <= 4; bpp++)
    {
        double t0 = Now(freq), t1 = t0;
        ULONG n = 0;

        while (t1 - t0 < secs)
        {
            for (i = 0; i < 32; i++)        /* a line of 32 letters */
                Blend(buf, bpp, i * GW);
            n++;
            t1 = Now(freq);
        }
        printf("%lu bytes a pixel: %.1f us a line of 32 letters\n",
               (unsigned long)bpp, (t1 - t0) * 1e6 / n);
    }
    FreeVec(buf);
    CloseDevice((struct IORequest *)&tr);
    if (rda)
        FreeArgs(rda);
    return RETURN_OK;
}
