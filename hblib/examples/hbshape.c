/*
 * hbshape - the smallest useful harfbuzz.library program: shapes a line
 * of text with a TrueType/OpenType font and prints the glyphs, where
 * they go, and the width of the line.
 *
 *   hbshape FONT/A TEXT/A [SIZE/N] [LANG/K] [FEATURES/K] [LATIN5/S]
 *
 *   hbshape FONTS:_ttf/DejaVuSans.ttf "AVATAR office" SIZE 16
 *   hbshape FONTS:_ttf/DejaVuSans.ttf "AVATAR office" FEATURES "-kern,-liga"
 *   hbshape FONTS:_ttf/DejaVuSans.ttf "<Turkish text>" LANG tr LATIN5
 *
 * TEXT comes from the Shell in the Amiga character set: ISO-8859-1, or
 * ISO-8859-9 (Turkish) with LATIN5. HarfBuzz itself wants Unicode, so
 * the bytes are turned into code points first.
 *
 * Build (gcc, from the Developer directory of the archive):
 *   m68k-amigaos-gcc -m68020 -O2 -noixemul -Iinclude -o hbshape examples/hbshape.c
 *
 * This file is public domain: use it as the start of your own program.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include <proto/harfbuzz.h>     /* also includes <hb.h> and <hb-ot.h> */

struct Library *HarfBuzzBase;

static const char Template[] =
    "FONT/A,TEXT/A,SIZE/N,LANG/K,FEATURES/K,LATIN5/S";
enum { A_FONT, A_TEXT, A_SIZE, A_LANG, A_FEATURES, A_LATIN5, A_COUNT };

/* The font file in memory; HarfBuzz reads the font from a blob. */
static UBYTE *ReadFile(const char *name, ULONG *size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG len;

    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len > 0 && (buf = AllocVec(len, MEMF_ANY)) && Read(fh, buf, len) != len)
    {
        FreeVec(buf);
        buf = NULL;
    }
    Close(fh);
    *size = len;
    return buf;
}

/* The blob calls this when the last face using the font is gone. */
static void FreeFontData(void *data)
{
    FreeVec(data);
}

/* Amiga characters -> Unicode: ISO-8859-1, or ISO-8859-9 with latin5 */
static hb_codepoint_t ToUnicode(UBYTE c, BOOL latin5)
{
    if (latin5)
        switch (c)
        {
            case 0xD0: return 0x011E;   /* G breve */
            case 0xDD: return 0x0130;   /* I dot */
            case 0xDE: return 0x015E;   /* S cedilla */
            case 0xF0: return 0x011F;   /* g breve */
            case 0xFD: return 0x0131;   /* dotless i */
            case 0xFE: return 0x015F;   /* s cedilla */
        }
    return c;
}

/* v in 1/64 pixel as "-12.34" (no floating point needed) */
static const char *Px(hb_position_t v, char *buf)
{
    unsigned long a = v < 0 ? -v : v;

    sprintf(buf, "%s%lu.%02lu", v < 0 ? "-" : "", a >> 6,
            ((a & 63) * 100 + 32) >> 6);
    return buf;
}

/* "kern,-liga,smcp": a comma separated list of OpenType features */
static unsigned ParseFeatures(const char *s, hb_feature_t *f, unsigned max)
{
    unsigned n = 0;

    while (s && *s && n < max)
    {
        const char *e = strchr(s, ',');
        int len = e ? (int)(e - s) : -1;

        if (hb_feature_from_string(s, len, &f[n]))
            n++;
        s = e ? e + 1 : NULL;
    }
    return n;
}

int main(void)
{
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    UBYTE *data;
    ULONG size, i, len;
    LONG ppem;
    hb_codepoint_t cp[256];
    hb_feature_t features[16];
    unsigned nfeatures, nglyphs;
    hb_blob_t *blob;
    hb_face_t *face;
    hb_font_t *font;
    hb_buffer_t *buf;
    hb_glyph_info_t *info;
    hb_glyph_position_t *pos;
    hb_position_t width = 0;
    const UBYTE *text;
    int rc = RETURN_OK;

    rda = ReadArgs((CONST_STRPTR)Template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"hbshape");
        return RETURN_FAIL;
    }
    ppem = args[A_SIZE] ? *(LONG *)args[A_SIZE] : 16;

    HarfBuzzBase = OpenLibrary((CONST_STRPTR)"harfbuzz.library", 0);
    if (!HarfBuzzBase)
    {
        printf("hbshape: cannot open harfbuzz.library\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    data = ReadFile((const char *)args[A_FONT], &size);
    if (!data)
    {
        printf("hbshape: cannot read %s\n", (const char *)args[A_FONT]);
        CloseLibrary(HarfBuzzBase);
        FreeArgs(rda);
        return RETURN_ERROR;
    }

    /* blob -> face -> font. The blob owns the file data from now on. */
    blob = hb_blob_create((const char *)data, size, HB_MEMORY_MODE_READONLY,
                          data, FreeFontData);
    face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);              /* the face keeps a reference */
    font = hb_font_create(face);
    /* positions in 1/64 pixel at ppem pixels per em */
    hb_font_set_scale(font, ppem * 64, ppem * 64);

    /* the text, as Unicode code points */
    text = (const UBYTE *)args[A_TEXT];
    len = strlen((const char *)text);
    if (len > 256)
        len = 256;
    for (i = 0; i < len; i++)
        cp[i] = ToUnicode(text[i], args[A_LATIN5] != 0);

    buf = hb_buffer_create();
    hb_buffer_add_codepoints(buf, cp, len, 0, len);
    if (args[A_LANG])
        hb_buffer_set_language(buf,
            hb_language_from_string((const char *)args[A_LANG], -1));
    /* script and direction from the text (the language too, if not set:
       give LANG, there is no locale to take it from) */
    hb_buffer_guess_segment_properties(buf);

    nfeatures = ParseFeatures((const char *)args[A_FEATURES], features, 16);
    hb_shape(font, buf, features, nfeatures);

    info = hb_buffer_get_glyph_infos(buf, &nglyphs);
    pos = hb_buffer_get_glyph_positions(buf, &nglyphs);
    printf("%u glyphs, %s, %ld px per em\n", nglyphs,
           hb_direction_to_string(hb_buffer_get_direction(buf)), (long)ppem);
    printf("glyph            id  char  advance  x off  y off   (pixels)\n");
    for (i = 0; i < nglyphs; i++)
    {
        char name[32], a[16], x[16], y[16];

        if (!hb_font_get_glyph_name(font, info[i].codepoint, name,
                                    sizeof(name)))
            strcpy(name, "-");
        printf("%-14s %5lu  %4lu  %7s %6s %6s\n", name,
               (unsigned long)info[i].codepoint,
               (unsigned long)info[i].cluster,     /* first character */
               Px(pos[i].x_advance, a), Px(pos[i].x_offset, x),
               Px(pos[i].y_offset, y));
        width += pos[i].x_advance;
    }
    {
        char w[16];

        printf("width %s pixels\n", Px(width, w));
    }

    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    hb_face_destroy(face);              /* frees the font data */
    CloseLibrary(HarfBuzzBase);
    FreeArgs(rda);
    return rc;
}
