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
#include "charsets.h"

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
    /* "charsets": the charset keyword and a few known mappings per set */
    if (strcmp(argv[2], "charsets") == 0)
    {
        static const struct { UBYTE cs, c; UWORD u; } t[] =
        {
            { AA_CHARSET_LATIN1,   0xE9, 0x00E9 },  /* e acute */
            { AA_CHARSET_LATIN2,   0xA3, 0x0141 },  /* L stroke */
            { AA_CHARSET_LATIN3,   0xA5, 0x0000 },  /* undefined -> code */
            { AA_CHARSET_LATIN4,   0xA1, 0x0104 },  /* A ogonek */
            { AA_CHARSET_LATIN5,   0xFD, 0x0131 },  /* dotless i */
            { AA_CHARSET_LATIN9,   0xA4, 0x20AC },  /* euro */
            { AA_CHARSET_LATIN10,  0xAA, 0x0218 },  /* S comma */
            { AA_CHARSET_CP1250,   0x8A, 0x0160 },  /* S caron */
            { AA_CHARSET_CYRILLIC, 0xB0, 0x0410 },  /* Cyrillic A */
            { AA_CHARSET_KOI8R,    0xC1, 0x0430 },  /* Cyrillic a */
            { AA_CHARSET_KOI8R,    0x41, 0x0041 },  /* ASCII */
        };
        int bad = prefs.charset != AA_CHARSET_KOI8R;

        printf("charset from prefs: %s\n", aa_CharsetNames[prefs.charset]);
        for (i = 0; i < (int)(sizeof(t) / sizeof(t[0])); i++)
        {
            ULONG want = t[i].u ? t[i].u : t[i].c;
            ULONG got = aa_CharsetToUnicode(t[i].cs, t[i].c);

            printf("%-8s %02X -> %04lX %s\n", aa_CharsetNames[t[i].cs],
                   t[i].c, (unsigned long)got, got == want ? "ok" : "FAIL");
            bad |= got != want;
        }
        return bad ? 10 : 0;
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
    tf.tf_Flags = FPF_PROPORTIONAL;
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
    /* "capsize": like the user's Arial/14 (baseline 9, 'H' rows 3..9):
       a real metrics font must get a 7 pixel 'H' standing on row 9 */
    if (strcmp(argv[2], "capsize") == 0)
    {
        static UBYTE data[16];
        static ULONG loc[256];
        struct TextFont bt;
        struct AAFont *bf = aa_FontAt(0);
        struct AAGlyph *g;
        int y, gy, bottom;

        aa_UnlockGlyphs();
        for (y = 3; y <= 9; y++)
            data[y] = 0x42;
        for (y = 0; y < 256; y++)
            loc[y] = 8;
        memset(&bt, 0, sizeof(bt));
        bt.tf_YSize = bf->ysize;
        bt.tf_XSize = 8;
        bt.tf_Flags = FPF_PROPORTIONAL;     /* real metrics need this */
        bt.tf_Baseline = 9;
        bt.tf_LoChar = 0;
        bt.tf_HiChar = 255;
        bt.tf_CharData = data;
        bt.tf_Modulo = 1;
        bt.tf_CharLoc = loc;

        /* main() already prepared it with a font without glyph data */
        bf->prepared = FALSE;
        bf->ftsize = NULL;
        aa_LockGlyphs();
        aa_PrepareFont(bf, &bt);
        g = aa_GetGlyph(bf, 'H');
        aa_UnlockGlyphs();
        if (!g)
            return 10;
        /* the placement used by render.c */
        gy = bt.tf_Baseline + bf->yoffset + 1 - g->top;
        bottom = gy + g->rows - 1;
        printf("capsize: %dpx ", bf->pxused);
        printf("real=%d 'H' %d rows, top %d -> rows %d..%d, "
               "baseline 9 -> %s\n", (int)bf->real, g->rows, g->top, gy,
               bottom, (g->rows == 7 && bottom == 9) ? "OK" : "FAIL");
        /* a fixed-width font (Shell) must never use real metrics */
        bt.tf_Flags = 0;
        printf("fixed-width font with \"real\": real metrics %s -> %s\n",
               aa_UseReal(bf, &bt) ? "on" : "off",
               aa_UseReal(bf, &bt) ? "FAIL" : "OK");
        aa_GlyphsCleanup();
        return (g->rows == 7 && bottom == 9 && !aa_UseReal(bf, &bt)) ? 0 : 10;
    }

    /* "baseline": bitmap font whose letters stand 2 rows below
       tf_Baseline; the measured offset must be +2 */
    if (strcmp(argv[2], "baseline") == 0)
    {
        static UBYTE data[16];
        static ULONG loc[256];
        struct TextFont bt;
        struct AAFont *bf;
        int y;

        aa_UnlockGlyphs();
        for (y = 3; y <= 12; y++)
            data[y] = 0x42;                 /* 'H'-like, ink rows 3..12 */
        for (y = 0; y < 256; y++)
            loc[y] = 8;                     /* bit offset 0, width 8 */

        memset(&bt, 0, sizeof(bt));
        bt.tf_Message.mn_Node.ln_Name = (char *)"Base.font";
        strcpy(prefs.map[0].fontname, "Base");
        bt.tf_YSize = prefs.map[0].ysize;   /* 16 in tests/test.prefs */
        bt.tf_XSize = 8;
        bt.tf_Baseline = 10;
        bt.tf_LoChar = 0;
        bt.tf_HiChar = 255;
        bt.tf_CharData = data;
        bt.tf_Modulo = 1;
        bt.tf_CharLoc = loc;

        /* a fresh entry: aa_FindFont() on a new name/size */
        bf = aa_FontAt(0);
        bf->prepared = FALSE;
        bf->ftsize = NULL;
        strcpy(bf->name, "Base");
        aa_LockGlyphs();
        aa_PrepareFont(bf, &bt);
        aa_UnlockGlyphs();
        printf("baseline: tf_Baseline 10, ink bottom 12 -> yoffset %d -> %s\n",
               bf->yoffset, bf->yoffset == 2 ? "OK" : "FAIL");
        aa_GlyphsCleanup();
        return bf->yoffset == 2 ? 0 : 10;
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
            sum += font->adv[text[k]] +
                   (k > 0 ? aa_KernPair(font, text[k - 1], text[k]) : 0);
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

        /*
         * Kerning: pairs that kern in most fonts. Length, extent and both
         * fit directions must all include it; with "kerning off" there
         * are no pairs at all.
         */
        {
            static const char *const pairs[] = { "AV", "To", "Ye", "Wa", "LT" };
            LONG kerned = 0, total = font->kern ? font->kern->first[256] : 0;
            int i;

            printf("kerning: %ld pairs, setting %s\n", (long)total,
                   prefs.kerning ? "on" : "off");
            if (!prefs.kerning && total)
            {
                printf("kerning off but pairs loaded -> FAIL\n");
                fails++;
            }
            for (i = 0; i < 5; i++)
            {
                const UBYTE *p = (const UBYTE *)pairs[i];
                LONG kp = aa_KernPair(font, p[0], p[1]);
                LONG want = font->adv[p[0]] + font->adv[p[1]] + kp;
                LONG l = aa_MLength(&m, p, 2);
                ULONG nf, nb;
                struct TextExtent pe, ff, bf;

                aa_MExtent(&m, p, 2, &pe);
                nf = aa_MFit(&m, p, 2, &ff, NULL, 1, 10000, m.ysize);
                nb = aa_MFit(&m, p + 1, 2, &bf, NULL, -1, 10000, m.ysize);
                kerned += kp != 0;
                printf("  %s: kern %ld, length %ld, extent w %d, fit %d/%d "
                       "-> %s\n", pairs[i], (long)kp, (long)l, pe.te_Width,
                       ff.te_Width, bf.te_Width,
                       (l == want && pe.te_Width == want && nf == 2 &&
                        nb == 2 && ff.te_Width == want &&
                        bf.te_Width == want) ? "OK" : "FAIL");
                fails += !(l == want && pe.te_Width == want && nf == 2 &&
                           nb == 2 && ff.te_Width == want &&
                           bf.te_Width == want);
            }
            if (prefs.kerning && !kerned)
            {
                printf("kerning on but none of the pairs kerns -> FAIL\n");
                fails++;
            }
        }

        /* whole text, both directions: fit width == length */
        {
            ULONG nf = aa_MFit(&m, text, len, &fe, NULL, 1, 10000, m.ysize);
            LONG fw = fe.te_Width;
            ULONG nb = aa_MFit(&m, text + len - 1, len, &fe, NULL, -1,
                               10000, m.ysize);

            printf("fit both ways: %ld / %d, length %ld -> %s\n", (long)fw,
                   fe.te_Width, (long)sum,
                   (nf == (ULONG)len && nb == (ULONG)len && fw == sum &&
                    fe.te_Width == sum) ? "OK" : "FAIL");
            fails += !(nf == (ULONG)len && nb == (ULONG)len && fw == sum &&
                       fe.te_Width == sum);
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
