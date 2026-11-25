/*
 * drscreen - opens its own RTG screen of a given depth and draws JAM1
 * text in a window there, for testing AAText's "directread" on screen
 * formats the Workbench does not use. The Workbench settings are not
 * touched.
 *
 *   drscreen [DEPTH <15|16|24|32>] [SECS <n>]
 *
 * The screen uses the Workbench screen font, so AAText draws the text
 * like on the Workbench. Shows the screen's pixel format, then waits
 * SECS seconds (look at it, or take a screenshot) and closes.
 *
 * Build: m68k-amigaos-gcc -m68020 -O2 -noixemul -o drscreen tests/drscreen.c src/cgx.s
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

#include "../src/cgx.h"

struct Library *CyberGfxBase;

int main(void)
{
    static const char template[] = "DEPTH/K/N,SECS/K/N";
    LONG args[2] = { 0, 0 };
    LONG depth = 16, secs = 5, i;
    struct RDArgs *rda;
    struct Screen *scr;
    struct Window *win;
    ULONG modeid, fmt = ~0UL;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (rda && args[0])
        depth = *(LONG *)args[0];
    if (rda && args[1])
        secs = *(LONG *)args[1];
    if (rda)
        FreeArgs(rda);

    modeid = BestModeID(BIDTAG_NominalWidth, 800, BIDTAG_NominalHeight, 600,
                        BIDTAG_Depth, depth, TAG_DONE);
    if (modeid == INVALID_ID)
    {
        printf("drscreen: no %ld bit screen mode\n", depth);
        return RETURN_FAIL;
    }
    scr = OpenScreenTags(NULL, SA_DisplayID, modeid, SA_Depth, depth,
                         SA_Width, 800, SA_Height, 600,
                         SA_Title, (ULONG)"drscreen", SA_SysFont, 1,
                         SA_Pens, (ULONG)"\xff\xff", SA_ShowTitle, TRUE,
                         TAG_DONE);
    if (!scr)
    {
        printf("drscreen: cannot open the screen\n");
        return RETURN_FAIL;
    }
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (CyberGfxBase)
    {
        fmt = cgx_GetCyberMapAttr(CyberGfxBase, scr->RastPort.BitMap,
                                  CYBRMATTR_PIXFMT);
        CloseLibrary(CyberGfxBase);
    }
    printf("drscreen: %ld bit screen, mode $%08lx, pixel format %ld\n",
           (long)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH),
           (unsigned long)modeid, (long)fmt);

    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 20,
                         WA_Top, 30, WA_Width, 700, WA_Height, 400,
                         WA_Title, (ULONG)"drscreen", WA_SimpleRefresh, TRUE,
                         WA_DragBar, TRUE, TAG_DONE);
    if (win)
    {
        struct RastPort *rp = win->RPort;

        SetDrMd(rp, JAM1);
        for (i = 0; i < 14; i++)
        {
            static const char line[] =
                "JAM1 on a deep screen: Aa Bb Cc Gg Ss \xc7\xe7 \xd0\xf0 \xdd\xfd \xd6\xf6 \xde\xfe \xdc\xfc";

            /* lines on alternating pen backgrounds */
            SetAPen(rp, i & 1 ? 3 : 2);
            RectFill(rp, win->BorderLeft, win->BorderTop + i * 24,
                     win->Width - win->BorderRight - 1,
                     win->BorderTop + i * 24 + 23);
            SetAPen(rp, i & 1 ? 2 : 1);
            Move(rp, win->BorderLeft + 6, win->BorderTop + i * 24 + 16);
            Text(rp, (CONST_STRPTR)line, strlen(line));
        }
        Delay(secs * 50);
        CloseWindow(win);
    }
    CloseScreen(scr);
    return RETURN_OK;
}
