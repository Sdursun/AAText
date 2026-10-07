/*
 * textbench - measures Text() speed: draws one line of text many times
 * in a window on the Workbench screen and reports the time per call and
 * per character. Run it with and without AAText to see what AAText
 * costs.
 *
 *   textbench <name.font> <size> [calls] [jam2]
 *
 * calls: number of Text() calls (default 2000). "jam2" draws with JAM2
 * (background filled) instead of JAM1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>
#include <proto/timer.h>

struct Library *DiskfontBase;
struct Device *TimerBase;

static const char line[] = "The quick brown fox jumps over the lazy dog";

int main(int argc, char **argv)
{
    struct TextAttr ta;
    struct TextFont *tf;
    struct Window *win;
    struct RastPort *rp;
    struct timerequest tr;
    struct EClockVal t0, t1;
    ULONG freq, calls, i, rows, len = strlen(line);
    BOOL jam2;
    double us;

    if (argc < 3)
    {
        printf("usage: textbench <name.font> <size> [calls] [jam2]\n");
        return 10;
    }
    calls = argc > 3 ? atol(argv[3]) : 2000;
    jam2 = argc > 4 && !strcmp(argv[4], "jam2");

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&tr, 0))
        return 20;
    TimerBase = tr.tr_node.io_Device;

    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);
    ta.ta_Name = (STRPTR)argv[1];
    ta.ta_YSize = atoi(argv[2]);
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    tf = DiskfontBase ? OpenDiskFont(&ta) : NULL;
    if (!tf)
    {
        printf("cannot open %s %s\n", argv[1], argv[2]);
        goto out;
    }

    win = OpenWindowTags(NULL,
                         WA_Left, 40, WA_Top, 120,
                         WA_InnerWidth, 640, WA_InnerHeight, 300,
                         WA_Title, (ULONG)"textbench",
                         WA_DragBar, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE,
                         TAG_DONE);
    if (!win)
        goto out;
    rp = win->RPort;
    SetFont(rp, tf);
    SetAPen(rp, 1);
    SetBPen(rp, 0);
    SetDrMd(rp, jam2 ? JAM2 : JAM1);
    rows = 280 / tf->tf_YSize;

    /* once outside the measurement: the font is loaded on first use */
    Move(rp, win->BorderLeft + 4, win->BorderTop + 4 + tf->tf_Baseline);
    Text(rp, (CONST_STRPTR)line, len);
    Delay(50);

    freq = ReadEClock(&t0);
    for (i = 0; i < calls; i++)
    {
        Move(rp, win->BorderLeft + 4,
             win->BorderTop + 4 + tf->tf_Baseline + (i % rows) * tf->tf_YSize);
        Text(rp, (CONST_STRPTR)line, len);
    }
    ReadEClock(&t1);

    us = ((double)(t1.ev_hi - t0.ev_hi) * 4294967296.0 +
          (double)t1.ev_lo - (double)t0.ev_lo) * 1e6 / freq;
    printf("%s %ld, %s: %ld calls of %ld chars in %ld ms: "
           "%ld us/call, %ld.%01ld us/char, %ld chars/s\n",
           argv[1], (long)ta.ta_YSize, jam2 ? "JAM2" : "JAM1",
           (long)calls, (long)len, (long)(us / 1000),
           (long)(us / calls), (long)(us / (calls * len)),
           (long)(us * 10 / (calls * len)) % 10,
           (long)(calls * len * 1e6 / us));

    CloseWindow(win);
out:
    if (tf)
        CloseFont(tf);
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
    CloseDevice((struct IORequest *)&tr);
    return 0;
}
