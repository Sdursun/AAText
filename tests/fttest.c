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
#include "metrics.h"
#include "otag.h"

/* stub.s references these; the patch itself is not linked here. */
volatile LONG aa_UseCount;
void aa_TextHook(void) { }
void aa_TextLengthHook(void) { }
void aa_TextExtentHook(void) { }
void aa_TextFitHook(void) { }

static struct AAPrefs prefs;

int main(int argc, char **argv)
{
    static const char shades[] = " .:-=+*#%@";
    struct TextFont tf;
    struct AAFont *font;
    const char *s;
    LONG n;
    int i;

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
    /* "otag": parse a .otag file given as third argument */
    if (strcmp(argv[2], "otag") == 0 && argc > 3)
    {
        static UBYTE buf[65536];
        static struct AAOTagInfo info;
        FILE *f = fopen(argv[3], "rb");
        size_t len = f ? fread(buf, 1, sizeof(buf), f) : 0;
        BOOL ok;

        if (f)
            fclose(f);
        ok = aa_ParseOTag(buf, len, &info);
        printf("otag %s: %lu bytes, ok=%d\n  font file: %s\n  engine: %s\n"
               "  face %ld, code page: %s\n", argv[3], (unsigned long)len,
               (int)ok, info.fontfile ? info.fontfile : "(none)",
               info.engine ? info.engine : "(none)", (long)info.facenum,
               info.hascodepage ? "yes" : "no");
        if (info.hascodepage)
            printf("  0xD0->%04X 0xDD->%04X 0xDE->%04X 0xF0->%04X 0xFD->%04X "
                   "0xFE->%04X 0x41->%04X\n", info.codepage[0xD0],
                   info.codepage[0xDD], info.codepage[0xDE],
                   info.codepage[0xF0], info.codepage[0xFD],
                   info.codepage[0xFE], info.codepage[0x41]);
        return ok ? 0 : 10;
    }

    n = aa_GlyphsInit(&prefs, TRUE);

    /* "auto": detect a font via FONTS:<name>.otag like Text() would */
    if (strcmp(argv[2], "auto") == 0 && argc > 3)
    {
        struct AAFont *af;
        LONG done;

        memset(&tf, 0, sizeof(tf));
        tf.tf_Message.mn_Node.ln_Name = argv[3];
        tf.tf_YSize = 16;
        tf.tf_XSize = 8;
        tf.tf_Baseline = 12;
        tf.tf_LoChar = 32;
        tf.tf_HiChar = 255;

        af = aa_FindFont(&tf);
        printf("first lookup: %s (expected: none, queued)\n",
               af ? "found" : "none");
        done = aa_ResolvePending(TRUE);
        printf("resolved %ld pending name(s)\n", (long)done);
        af = aa_FindFont(&tf);
        printf("second lookup: %s\n", af ? af->name : "none");
        if (!af)
            return 10;
        aa_LockGlyphs();
        if (aa_PrepareFont(af, &tf))
        {
            static const UBYTE turkish[] = { 0xF0, 0xFD, 0xDE, 0 };
            const UBYTE *t;

            for (t = turkish; *t; t++)
            {
                struct AAGlyph *g = aa_GetGlyph(af, *t);
                int x, y;

                printf("code %02X: %dx%d\n", *t, g ? g->width : -1,
                       g ? g->rows : -1);
                for (y = 0; g && y < g->rows; y++)
                {
                    putchar('|');
                    for (x = 0; x < g->width; x++)
                        putchar(shades[g->data[y * g->width + x] * 9 / 255]);
                    printf("|\n");
                }
            }
        }
        aa_UnlockGlyphs();
        /* same name, other size: entry created without new I/O */
        tf.tf_YSize = 24;
        af = aa_FindFont(&tf);
        printf("other size: %s, %ld font entries\n", af ? "found" : "none",
               (long)aa_FontCount());
        /* a bitmap font without .otag */
        tf.tf_Message.mn_Node.ln_Name = (char *)"topaz.font";
        aa_FindFont(&tf);
        aa_ResolvePending(TRUE);
        printf("topaz: %s (expected: none)\n",
               aa_FindFont(&tf) ? "found" : "none");
        aa_GlyphsCleanup();
        return 0;
    }
    printf("%ld font mapping(s), gamma %ld, cache %ld KB, offscreen %d\n",
           (long)n, (long)prefs.gamma100, (long)prefs.cachekb,
           (int)prefs.offscreen);
    for (i = 0; i < prefs.numblack; i++)
        printf("blacklist: \"%s\"\n", prefs.blacklist[i]);
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
    /* "metrics": real metrics consistency checks */
    if (strcmp(argv[2], "metrics") == 0)
    {
        static const UBYTE text[] = "Hello, World! fij Wq";
        LONG len = sizeof(text) - 1, sum = 0, k;
        struct AAMetricsCtx m;
        struct TextExtent te, fe;
        int c, bad = 0, fails = 0;

        if (!font->real)
        {
            printf("metrics: mapping is not \"real\"\n");
            return 10;
        }
        /* drawing advance (glyph cache) == measuring advance (table) */
        for (c = 32; c < 256; c++)
        {
            struct AAGlyph *g = aa_GetGlyph(font, (UBYTE)c);

            if (!g || g->advance != font->adv[c])
                bad++;
        }
        printf("advance table vs rendered glyphs: %d mismatches -> %s\n",
               bad, bad ? "FAIL" : "OK");
        fails += bad != 0;

        m.font = font;
        m.ysize = tf.tf_YSize;
        m.baseline = tf.tf_Baseline;
        m.boldsmear = 1;
        m.txspacing = 0;
        m.algostyle = 0;

        for (k = 0; k < len; k++)
            sum += font->adv[text[k]];
        aa_MExtent(&m, text, len, &te);
        printf("length %ld, sum %ld, extent w=%d h=%d x %d..%d y %d..%d -> %s\n",
               (long)aa_MLength(&m, text, len), (long)sum, te.te_Width,
               te.te_Height, te.te_Extent.MinX, te.te_Extent.MaxX,
               te.te_Extent.MinY, te.te_Extent.MaxY,
               (aa_MLength(&m, text, len) == sum && te.te_Width == sum &&
                te.te_Extent.MaxX >= sum - 1) ? "OK" : "FAIL");
        fails += !(aa_MLength(&m, text, len) == sum && te.te_Width == sum);

        /* TextFit forwards: a box exactly as wide as the first k chars'
           extent must fit k chars, and not k+1 when that is wider */
        for (k = 1; k < len; k++)
        {
            struct TextExtent pe;
            ULONG n;

            aa_MExtent(&m, text, k, &pe);
            n = aa_MFit(&m, text, len, &fe, NULL, 1,
                        pe.te_Extent.MaxX - pe.te_Extent.MinX + 1, m.ysize);
            if (n < (ULONG)k)
            {
                printf("fit: %ld chars expected >= %ld, got %lu\n",
                       (long)k, (long)k, (unsigned long)n);
                fails++;
            }
        }
        /* backwards from the last character */
        {
            ULONG n = aa_MFit(&m, text + len - 1, len, &fe, NULL, -1,
                              10000, m.ysize);
            printf("fit backwards, unlimited: %lu of %ld -> %s\n",
                   (unsigned long)n, (long)len, n == (ULONG)len ? "OK" : "FAIL");
            fails += n != (ULONG)len;
            n = aa_MFit(&m, text, len, &fe, NULL, 1, 10000, m.ysize - 1);
            printf("fit, too low: %lu -> %s\n", (unsigned long)n,
                   n == 0 ? "OK" : "FAIL");
            fails += n != 0;
        }

        m.algostyle = FSF_BOLD | FSF_ITALIC;
        aa_MExtent(&m, text, len, &fe);
        printf("bold+italic extent x %d..%d (plain %d..%d) -> %s\n",
               fe.te_Extent.MinX, fe.te_Extent.MaxX, te.te_Extent.MinX,
               te.te_Extent.MaxX,
               (fe.te_Width == te.te_Width &&
                fe.te_Extent.MaxX > te.te_Extent.MaxX &&
                fe.te_Extent.MinX < te.te_Extent.MinX) ? "OK" : "FAIL");
        printf("metrics: %s\n", fails ? "FAILED" : "all OK");
        aa_UnlockGlyphs();
        aa_GlyphsCleanup();
        return fails ? 10 : 0;
    }

    /* "stress": fill the cache past its limit several times (LRU test) */
    if (strcmp(argv[2], "stress") == 0)
    {
        ULONG limit = prefs.cachekb * 1024, bytes, count, maxbytes = 0;
        int round, c, fails = 0;

        for (round = 0; round < 3; round++)
        {
            for (c = 32; c < 256; c++)
            {
                if (!aa_GetGlyph(font, (UBYTE)c))
                    fails++;
                aa_GetCacheStats(&bytes, &count);
                if (bytes > maxbytes)
                    maxbytes = bytes;
            }
        }
        /* recently used glyphs must survive: 'A' twice in a row */
        aa_GetGlyph(font, 'A');
        aa_GetCacheStats(&bytes, &count);
        printf("stress: %d failures, %lu glyphs, %lu bytes now, "
               "%lu max, limit %lu -> %s\n", fails, (unsigned long)count,
               (unsigned long)bytes, (unsigned long)maxbytes,
               (unsigned long)limit,
               (!fails && maxbytes <= limit + 4096) ? "OK" : "FAIL");
        aa_UnlockGlyphs();
        aa_GlyphsCleanup();
        return fails ? 10 : 0;
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
