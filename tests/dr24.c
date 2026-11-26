/*
 * dr24 - JAM1 text into an off-screen bitmap of a given pixel format,
 * for testing AAText's "directread" on formats no screen mode offers
 * (WinUAE has no 24 bit RTG modes). Needs "offscreen on" in the AAText
 * preferences: a bitmap of no screen is drawn with Workbench colours.
 *
 *   dr24 [FMT <pixfmt>] [SECS <n>]
 *
 * FMT is a CGX PIXFMT_ number, default 9 (RGB24); 10 is BGR24. The
 * bitmap gets a colour gradient, then text lines; a checksum of the
 * result is printed. Run it with "directread off" and "directread on":
 * the two checksums must be equal. The bitmap is also shown in a window
 * on the Workbench screen for SECS seconds (default 3).
 *
 * Build: m68k-amigaos-gcc -m68020 -O2 -noixemul -Isrc -o dr24
 *        tests/dr24.c src/cgx.s
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

#include "../src/cgx.h"

#define BM_W 640
#define BM_H 240

#ifndef BMF_SPECIALFMT
#define BMF_SPECIALFMT 0x80
#endif
#define SHIFT_PIXFMT(fmt) ((ULONG)(fmt) << 24)

struct Library *CyberGfxBase;

int main(void)
{
    static const char template[] = "FMT/K/N,SECS/K/N";
    static UBYTE rgb[BM_W * BM_H * 3];
    LONG args[2] = { 0, 0 };
    LONG want = PIXFMT_RGB24, secs = 3, i, x, y;
    struct RDArgs *rda;
    struct BitMap *bm;
    struct RastPort rp;
    struct Screen *wb;
    struct Window *win = NULL;
    struct TextFont *tf = NULL;
    ULONG fmt, sum;
    int rc = RETURN_OK;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (rda && args[0])
        want = *(LONG *)args[0];
    if (rda && args[1])
        secs = *(LONG *)args[1];
    if (rda)
        FreeArgs(rda);

    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (!CyberGfxBase)
        return RETURN_FAIL;

    bm = AllocBitMap(BM_W, BM_H, 24, BMF_CLEAR | BMF_SPECIALFMT |
                     SHIFT_PIXFMT(want), NULL);
    if (!bm)
    {
        printf("dr24: no bitmap of pixel format %ld\n", (long)want);
        CloseLibrary(CyberGfxBase);
        return RETURN_FAIL;
    }
    fmt = cgx_GetCyberMapAttr(CyberGfxBase, bm, CYBRMATTR_PIXFMT);
    printf("dr24: bitmap %ldx%ld, pixel format %ld (asked %ld)\n",
           (long)BM_W, (long)BM_H, (long)fmt, (long)want);

    /* the window first: then AAText finds the Workbench colours */
    wb = LockPubScreen(NULL);
    if (wb)
    {
        win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 40,
                             WA_Top, 60, WA_InnerWidth, BM_W,
                             WA_InnerHeight, BM_H, WA_Title, (ULONG)"dr24",
                             WA_DragBar, TRUE, WA_SmartRefresh, TRUE,
                             TAG_DONE);
        if (wb->Font)
            tf = OpenFont(wb->Font);
        UnlockPubScreen(NULL, wb);
    }

    InitRastPort(&rp);
    rp.BitMap = bm;
    if (tf)
        SetFont(&rp, tf);

    /* a gradient: every channel differs, so swapped channels show */
    for (y = 0; y < BM_H; y++)
        for (x = 0; x < BM_W; x++)
        {
            UBYTE *p = rgb + (y * BM_W + x) * 3;

            p[0] = (UBYTE)(x * 255 / (BM_W - 1));
            p[1] = (UBYTE)(y * 255 / (BM_H - 1));
            p[2] = (UBYTE)(255 - x * 255 / (BM_W - 1));
        }
    cgx_WritePixelArray(CyberGfxBase, rgb, 0, 0, BM_W * 3, &rp, 0, 0, BM_W,
                        BM_H, RECTFMT_RGB);

    SetDrMd(&rp, JAM1);
    for (i = 0; i < 10; i++)
    {
        static const char line[] =
            "24 bit direct read: Aa Bb Cc Gg Ss \xc7\xe7 \xd0\xf0 \xdd\xfd "
            "\xd6\xf6 \xde\xfe \xdc\xfc 0123456789";

        SetAPen(&rp, i & 1 ? 2 : 1);
        Move(&rp, 8, 20 + i * 22);
        Text(&rp, (CONST_STRPTR)line, strlen(line));
    }

    /* checksum of the result, read back as RGB */
    memset(rgb, 0, sizeof(rgb));
    cgx_ReadPixelArray(CyberGfxBase, rgb, 0, 0, BM_W * 3, &rp, 0, 0, BM_W,
                       BM_H, RECTFMT_RGB);
    for (sum = 0, i = 0; i < BM_W * BM_H * 3; i++)
        sum = (sum << 1 | sum >> 31) ^ rgb[i];
    printf("dr24: checksum %08lx\n", (unsigned long)sum);

    if (win)
    {
        BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft,
                          win->BorderTop, BM_W, BM_H, 0xC0);
        Delay(secs * 50);
        CloseWindow(win);
    }
    if (tf)
        CloseFont(tf);
    WaitBlit();
    FreeBitMap(bm);
    CloseLibrary(CyberGfxBase);
    return rc;
}
