/*
 * texttest - opens a window on the Workbench screen and draws sample
 * lines with Text() in the given font, for testing AAText with any font
 * without changing the system's font preferences.
 *
 *   texttest <name.font> <size> [seconds] [style] [text]
 *
 * style: optional letters b (bold), i (italic), u (underlined), applied
 * to the last line with SetSoftStyle(). The window closes after the
 * given number of seconds (default 10) or with its close gadget. "text"
 * replaces the first line. After each line, cp_x is checked against
 * TextLength() and marked with a short line (pen 3).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>

struct Library *DiskfontBase;

/* ISO-8859-9 text (this file is UTF-8; the bytes are written as escapes) */
static const char *const lines[] =
{
    "The quick brown fox jumps over the lazy dog",
    "Pijamal\xFD hasta ya\xF0\xFDz \xFE" "of\xF6re \xE7" "abucak g\xFCvendi.",
    "\xC7\xD0\xDD\xD6\xDE\xDC \xE7\xF0\xFD\xF6\xFE\xFC 0123456789 (illustrate) WAVE mmm",
};

int main(int argc, char **argv)
{
    struct TextAttr ta;
    struct TextFont *tf;
    struct Window *win;
    struct RastPort *rp;
    ULONG style = 0;
    int secs, i, y, w = 0;

    if (argc < 3)
    {
        printf("usage: texttest <name.font> <size> [seconds] [style]\n");
        return 10;
    }
    secs = argc > 3 ? atoi(argv[3]) : 10;
    if (argc > 4)
    {
        if (strchr(argv[4], 'b')) style |= FSF_BOLD;
        if (strchr(argv[4], 'i')) style |= FSF_ITALIC;
        if (strchr(argv[4], 'u')) style |= FSF_UNDERLINED;
    }

    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);
    if (!DiskfontBase)
        return 20;
    ta.ta_Name = (STRPTR)argv[1];
    ta.ta_YSize = atoi(argv[2]);
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    tf = OpenDiskFont(&ta);
    if (!tf)
    {
        printf("cannot open %s %s\n", argv[1], argv[2]);
        CloseLibrary(DiskfontBase);
        return 10;
    }

    win = OpenWindowTags(NULL,
                         WA_Left, 40, WA_Top, 200,
                         WA_InnerWidth, 640,
                         WA_InnerHeight, (tf->tf_YSize + 6) * 4 + 8,
                         WA_Title, (ULONG)argv[1],
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                         WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW,
                         TAG_DONE);
    if (!win)
    {
        CloseFont(tf);
        CloseLibrary(DiskfontBase);
        return 20;
    }
    rp = win->RPort;
    SetFont(rp, tf);
    SetAPen(rp, 1);
    SetDrMd(rp, JAM1);

    y = win->BorderTop + 4 + tf->tf_Baseline;
    for (i = 0; i < 3; i++)
    {
        const char *s = (i == 0 && argc > 5) ? argv[5] : lines[i];
        LONG x0 = win->BorderLeft + 8, tl;

        if (i == 2 && style)
            SetSoftStyle(rp, style, AskSoftStyle(rp));
        Move(rp, x0, y);
        Text(rp, (CONST_STRPTR)s, strlen(s));
        tl = TextLength(rp, (CONST_STRPTR)s, strlen(s));
        if (rp->cp_x != x0 + tl)
            printf("line %d: cp_x moved %d, TextLength %ld\n", i + 1,
                   rp->cp_x - (int)x0, (long)tl);
        SetAPen(rp, 3);
        Move(rp, rp->cp_x, y + 2);
        Draw(rp, rp->cp_x, y + 4);
        SetAPen(rp, 1);
        if (tl > w)
            w = tl;
        y += tf->tf_YSize + 6;
    }
    printf("%s %d: drawn, widest line %d pixels\n", argv[1], tf->tf_YSize, w);

    for (i = 0; i < secs * 10; i++)
    {
        struct IntuiMessage *m = (struct IntuiMessage *)GetMsg(win->UserPort);

        if (m)
        {
            ReplyMsg((struct Message *)m);
            break;
        }
        Delay(5);
    }

    CloseWindow(win);
    CloseFont(tf);
    CloseLibrary(DiskfontBase);
    return 0;
}
