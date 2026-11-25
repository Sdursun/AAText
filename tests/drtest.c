/*
 * drtest - text in a partly covered SMART_REFRESH window, for AAText's
 * "directread": the covered part lives in a backing store bitmap.
 *
 *   drtest [SECS <n>]
 *
 * Opens window "Back" (smart refresh) and window "Cover" over its middle,
 * draws JAM1 text lines across both in "Back", waits SECS seconds (look
 * at the screen), then moves "Cover" away: the uncovered text must look
 * like the rest. Closes both after another SECS seconds.
 *
 * Build: m68k-amigaos-gcc -m68020 -O2 -noixemul -o drtest tests/drtest.c
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char template[] = "SECS/K/N";
    LONG args[1] = { 0 };
    LONG secs = 4, i;
    struct RDArgs *rda;
    struct Window *back, *cover;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (rda && args[0])
        secs = *(LONG *)args[0];
    if (rda)
        FreeArgs(rda);

    back = OpenWindowTags(NULL, WA_Left, 40, WA_Top, 60, WA_Width, 600,
                          WA_Height, 300, WA_Title, (ULONG)"Back",
                          WA_SmartRefresh, TRUE, WA_DragBar, TRUE,
                          WA_DepthGadget, TRUE, TAG_DONE);
    if (!back)
    {
        printf("drtest: cannot open window\n");
        return RETURN_FAIL;
    }
    cover = OpenWindowTags(NULL, WA_Left, 200, WA_Top, 120, WA_Width, 240,
                           WA_Height, 140, WA_Title, (ULONG)"Cover",
                           WA_SmartRefresh, TRUE, WA_DragBar, TRUE, TAG_DONE);
    if (!cover)
    {
        CloseWindow(back);
        printf("drtest: cannot open window\n");
        return RETURN_FAIL;
    }

    SetDrMd(back->RPort, JAM1);
    SetAPen(back->RPort, 1);
    for (i = 0; i < 12; i++)
    {
        static const char line[] =
            "Smart refresh: Aa Bb Cc Dd Ee Ff Gg Hh Ii Jj Kk Ll Mm Nn";

        Move(back->RPort, back->BorderLeft + 4,
             back->BorderTop + 4 + (i + 1) * (back->RPort->TxHeight + 4));
        Text(back->RPort, (CONST_STRPTR)line, strlen(line));
    }
    Delay(secs * 50);
    MoveWindow(cover, 500, 300);
    Delay(secs * 50);

    CloseWindow(cover);
    CloseWindow(back);
    return RETURN_OK;
}
