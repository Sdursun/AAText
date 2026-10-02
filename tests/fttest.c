/*
 * Host-side smoke test for the glyph code, run under vamos (amitools):
 * reads a prefs file, opens the first mapping and prints glyphs as ASCII.
 *
 *   fttest <prefsfile> <text>
 */

#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <graphics/text.h>

#include "prefs.h"
#include "glyphs.h"

/* stub.s references these; the patch itself is not linked here. */
volatile LONG aa_UseCount;
void aa_TextHook(void) { }

static struct AAPrefs prefs;

int main(int argc, char **argv)
{
    static const char shades[] = " .:-=+*#%@";
    struct TextFont tf;
    struct AAFont *font;
    const char *s;
    LONG n;

    if (argc < 3)
    {
        printf("usage: fttest <prefsfile> <text>\n");
        return 10;
    }
    if (!aa_ReadPrefs(&prefs, argv[1], TRUE))
    {
        printf("cannot read %s\n", argv[1]);
        return 10;
    }
    n = aa_GlyphsInit(&prefs, TRUE);
    printf("%ld font mapping(s), gamma %ld\n", (long)n, (long)prefs.gamma100);
    if (n == 0)
        return 10;

    memset(&tf, 0, sizeof(tf));
    tf.tf_Message.mn_Node.ln_Name = (char *)"Test.font";
    strcpy(prefs.map[0].fontname, "Test");
    tf.tf_YSize = prefs.map[0].ysize;
    tf.tf_XSize = prefs.map[0].ysize / 2;
    tf.tf_Baseline = prefs.map[0].ysize * 3 / 4;
    tf.tf_LoChar = 32;
    tf.tf_HiChar = 255;

    font = aa_FindFont(&tf);
    if (!font)
    {
        printf("aa_FindFont failed\n");
        return 10;
    }

    aa_LockGlyphs();
    if (!aa_PrepareFont(font, &tf))
    {
        printf("aa_PrepareFont failed\n");
        aa_UnlockGlyphs();
        return 10;
    }
    /* "@fdde" = hex character codes */
    if (argv[2][0] == '@')
    {
        static char codes[64];
        const char *h = argv[2] + 1;
        int i = 0;

        while (h[0] && h[1] && i < 63)
        {
            unsigned v;

            sscanf(h, "%2x", &v);
            codes[i++] = (char)v;
            h += 2;
        }
        codes[i] = 0;
        argv[2] = codes;
    }
    for (s = argv[2]; *s; s++)
    {
        struct AAGlyph *g = aa_GetGlyph(font, (UBYTE)*s);
        int x, y;

        if (!g)
        {
            printf("'%c': aa_GetGlyph failed\n", *s);
            continue;
        }
        printf("'%c': %dx%d left=%d top=%d advance=%d\n", *s, g->width,
               g->rows, g->left, g->top, g->advance);
        for (y = 0; y < g->rows; y++)
        {
            putchar('|');
            for (x = 0; x < g->width; x++)
                putchar(shades[g->data[y * g->width + x] * 9 / 255]);
            printf("|\n");
        }
    }
    /* second lookup must hit the cache */
    printf("cache: %s\n", aa_GetGlyph(font, (UBYTE)argv[2][0]) ? "ok" : "FAIL");
    aa_UnlockGlyphs();

    aa_GlyphsCleanup();
    printf("done\n");
    return 0;
}
