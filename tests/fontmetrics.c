/*
 * fontmetrics - open fonts with diskfont and print what the bitmap font
 * diskfont made looks like (for checking .otag files written by
 * AATextManager):
 *
 *   fontmetrics <name.font> <size> [<name.font> <size> ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <exec/types.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

struct Library *DiskfontBase;

int main(int argc, char **argv)
{
    int i;

    if (argc < 3)
    {
        printf("usage: fontmetrics <name.font> <size> ...\n");
        return 10;
    }
    if (!(DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36)))
        return 20;
    for (i = 1; i + 1 < argc; i += 2)
    {
        struct TextAttr ta = { (STRPTR)argv[i], atoi(argv[i + 1]), 0, 0 };
        struct TextFont *tf = OpenDiskFont(&ta);
        WORD *space, *kern;
        int c;

        if (!tf)
        {
            printf("%s %s: cannot open\n", argv[i], argv[i + 1]);
            continue;
        }
        printf("%s %d: XSize %d YSize %d Baseline %d Flags $%02x "
               "%s, chars %d-%d\n", argv[i], tf->tf_YSize, tf->tf_XSize,
               tf->tf_YSize, tf->tf_Baseline, tf->tf_Flags,
               (tf->tf_Flags & FPF_PROPORTIONAL) ? "proportional" : "fixed",
               tf->tf_LoChar, tf->tf_HiChar);
        space = (WORD *)tf->tf_CharSpace;
        kern = (WORD *)tf->tf_CharKern;
        for (c = 'A'; c <= 'D'; c++)
        {
            int idx = c - tf->tf_LoChar;

            printf("  '%c': space %d kern %d\n", c,
                   space ? space[idx] : tf->tf_XSize, kern ? kern[idx] : 0);
        }
        CloseFont(tf);
    }
    CloseLibrary(DiskfontBase);
    return 0;
}
