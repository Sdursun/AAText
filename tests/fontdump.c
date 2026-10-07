/*
 * fontdump - prints characters of an Amiga bitmap font as ASCII art.
 * Used to see what an outline engine generated for a code point, e.g.
 * whether 0xFD became dotless i (ISO-8859-9) or y acute (ISO-8859-1).
 *
 *   fontdump <name.font> <size> <hex codes...>
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
    struct TextAttr ta;
    struct TextFont *tf;
    int i;

    if (argc < 4)
    {
        printf("usage: fontdump <name.font> <size> <hex codes...>\n");
        return 10;
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
    printf("%s: ysize %d baseline %d chars %d-%d flags %02x\n",
           tf->tf_Message.mn_Node.ln_Name, tf->tf_YSize, tf->tf_Baseline,
           tf->tf_LoChar, tf->tf_HiChar, tf->tf_Flags);

    for (i = 3; i < argc; i++)
    {
        int c = (int)strtol(argv[i], NULL, 16);
        ULONG loc;
        int idx, bit0, w, x, y;

        if (c < tf->tf_LoChar || c > tf->tf_HiChar)
        {
            printf("%02X: not in font\n", c);
            continue;
        }
        idx = c - tf->tf_LoChar;
        loc = ((ULONG *)tf->tf_CharLoc)[idx];
        bit0 = loc >> 16;
        w = loc & 0xFFFF;
        printf("%02X: width %d\n", c, w);
        for (y = 0; y < tf->tf_YSize; y++)
        {
            const UBYTE *row = (const UBYTE *)tf->tf_CharData + y * tf->tf_Modulo;

            putchar('|');
            for (x = 0; x < w; x++)
            {
                int b = bit0 + x;

                putchar(row[b >> 3] & (0x80 >> (b & 7)) ? '#' : ' ');
            }
            printf("|%s\n", y == tf->tf_Baseline ? " <- baseline" : "");
        }
    }
    CloseFont(tf);
    CloseLibrary(DiskfontBase);
    return 0;
}
