/*
 * ft2dfont - opens an outline font through diskfont.library (so through
 * the font's engine, e.g. freetype2.library) and draws sample text in a
 * window on the Workbench screen.
 *
 *   ft2dfont [FONT <name.font>] [SIZES <n> <n> ...] [TEXT <string>]
 *            [BOLD] [ITALIC] [WAIT <seconds>]
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>

#include <stdio.h>
#include <string.h>

struct Library *DiskfontBase;

static const char Template[] = "FONT/K,SIZES/K/N/M,TEXT/K,BOLD/S,ITALIC/S,WAIT/K/N";
enum { A_FONT, A_SIZES, A_TEXT, A_BOLD, A_ITALIC, A_WAIT, A_COUNT };

/* Draws text from tf_CharData with BltTemplate(), at top-left x, y. */
static void DrawRaw(struct RastPort *rp, struct TextFont *tf, const char *s,
                    LONG x, LONG y)
{
    UWORD *loc = tf->tf_CharLoc;
    WORD *space = tf->tf_CharSpace;
    WORD *kern = tf->tf_CharKern;
    BOOL prop = (tf->tf_Flags & FPF_PROPORTIONAL) && space;

    for (; *s; s++)
    {
        UBYTE c = *s;
        LONG idx = (c >= tf->tf_LoChar && c <= tf->tf_HiChar)
                   ? c - tf->tf_LoChar : tf->tf_HiChar - tf->tf_LoChar + 1;
        UWORD off = loc[idx * 2], w = loc[idx * 2 + 1];
        LONG k = kern ? kern[idx] : 0;

        if (w)
            BltTemplate((PLANEPTR)((UBYTE *)tf->tf_CharData + (off >> 4) * 2),
                        off & 15, tf->tf_Modulo, rp, x + k, y, w, tf->tf_YSize);
        x += prop ? space[idx] + k : tf->tf_XSize;
    }
}

int main(void)
{
    static LONG defsizes[] = { 11, 13, 16, 24 };
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    struct Window *win;
    const char *font = "verdanaregular.font";
    const char *text = "The quick brown fox - \xD0\xF0\xDD\xFD\xDE\xFE \xD6\xF6\xDC\xFC\xC7\xE7";
    LONG *sizes[8], nsizes = 0, i, y, wait = 10;
    UBYTE style = FS_NORMAL;

    rda = ReadArgs((STRPTR)Template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"ft2dfont");
        return RETURN_FAIL;
    }
    if (args[A_FONT])
        font = (const char *)args[A_FONT];
    if (args[A_TEXT])
        text = (const char *)args[A_TEXT];
    if (args[A_WAIT])
        wait = *(LONG *)args[A_WAIT];
    if (args[A_BOLD])
        style |= FSF_BOLD;
    if (args[A_ITALIC])
        style |= FSF_ITALIC;
    if (args[A_SIZES])
        for (; nsizes < 8 && ((LONG **)args[A_SIZES])[nsizes]; nsizes++)
            sizes[nsizes] = ((LONG **)args[A_SIZES])[nsizes];
    else
        for (; nsizes < 4; nsizes++)
            sizes[nsizes] = &defsizes[nsizes];

    DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
    win = OpenWindowTags(NULL,
                         WA_Title, (ULONG)"ft2dfont",
                         WA_Left, 20, WA_Top, 30,
                         WA_InnerWidth, 620, WA_InnerHeight, 200,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                         WA_Activate, FALSE,
                         TAG_DONE);
    if (!DiskfontBase || !win)
    {
        printf("no diskfont.library or window\n");
        goto done;
    }

    SetAPen(win->RPort, 1);
    SetDrMd(win->RPort, JAM1);
    y = win->BorderTop + 4;
    for (i = 0; i < nsizes; i++)
    {
        struct TextAttr ta = { (STRPTR)font, (UWORD)*sizes[i], style, 0 };
        struct TextFont *tf = OpenDiskFont(&ta);
        char line[200];

        if (!tf)
        {
            printf("%s %ld: OpenDiskFont failed\n", font, *sizes[i]);
            continue;
        }
        SetFont(win->RPort, tf);
        SetSoftStyle(win->RPort, style, AskSoftStyle(win->RPort));
        printf("%s %ld: ysize %u xsize %u baseline %u flags $%02x style $%02x, "
               "text width %ld\n",
               font, *sizes[i], tf->tf_YSize, tf->tf_XSize, tf->tf_Baseline,
               tf->tf_Flags, tf->tf_Style,
               (LONG)TextLength(win->RPort, (STRPTR)text, strlen(text)));
        sprintf(line, "%ld: %s", *sizes[i], text);
        Move(win->RPort, win->BorderLeft + 4, y + tf->tf_Baseline);
        Text(win->RPort, (STRPTR)line, strlen(line));
        y += tf->tf_YSize + 2;
        /* the same line from the font's own glyph bitmaps: what the engine
           made, even when a Text() patch draws the text differently */
        DrawRaw(win->RPort, tf, line, win->BorderLeft + 4, y);
        y += tf->tf_YSize + 4;
        CloseFont(tf);
    }

    Delay(wait * 50);

done:
    if (win)
        CloseWindow(win);
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
    FreeArgs(rda);
    return RETURN_OK;
}
