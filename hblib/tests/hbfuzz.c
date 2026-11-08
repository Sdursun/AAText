/*
 * hbfuzz - feeds malformed and unusual fonts to harfbuzz.library: for
 * each font in a pack file (tests/mkpack.py), create face and font,
 * shape strings in several scripts (also with variations and features),
 * query the OpenType layout tables, destroy everything.
 *
 *   hbfuzz PACK <file> [FROM <n>] [LIB <path>]
 *
 * Every font's number and name are printed (and flushed) before it is
 * used, so after a crash the log shows which font did it. FROM skips
 * the first n fonts. The memory pool of the library is checked before
 * and after (AvailMem) to see leaks.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include <proto/harfbuzz.h>

struct Library *HarfBuzzBase;

/* UTF-8 */
static const char *const texts[] =
{
    "AVATAR Wave fi ffl To. \xC4\xB0\xC4\x9F\xC5\x9F",              /* Latin */
    "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD9\x84\xD8\xA7",    /* Arabic */
    "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",                              /* Hebrew */
    "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87", /* Devanagari */
    "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5", /* Thai */
    "\xED\x95\x9C\xEA\xB8\x80",                                      /* Hangul */
    "e\xCC\x81\xCC\xA7 a\xCC\x88\xCC\x83"                            /* marks */
};
#define NUM_TEXTS (sizeof(texts) / sizeof(texts[0]))

static ULONG Get32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | (p[2] << 8) | p[3];
}

static void Shape(hb_font_t *font, const char *text, int feat)
{
    hb_buffer_t *buf = hb_buffer_create();
    hb_feature_t f[2];
    unsigned n = 0;

    hb_buffer_add_utf8(buf, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    if (feat)
    {
        hb_feature_from_string("-kern", -1, &f[0]);
        hb_feature_from_string("smcp", -1, &f[1]);
    }
    hb_shape(font, buf, feat ? f : NULL, feat ? 2 : 0);
    hb_buffer_get_glyph_positions(buf, &n);
    hb_buffer_normalize_glyphs(buf);
    hb_buffer_destroy(buf);
}

static void OneFont(const UBYTE *data, ULONG size)
{
    hb_blob_t *blob = hb_blob_create((const char *)data, size,
                                     HB_MEMORY_MODE_READONLY, NULL, NULL);
    unsigned faces = hb_face_count(blob), fi;

    if (faces == 0)
        faces = 1;
    if (faces > 4)
        faces = 4;
    for (fi = 0; fi < faces; fi++)
    {
        hb_face_t *face = hb_face_create(blob, fi);
        hb_font_t *font = hb_font_create(face);
        hb_variation_t var;
        hb_tag_t tags[32];
        unsigned count, i;
        hb_glyph_extents_t ext;
        hb_font_extents_t fext;
        char name[64];

        hb_font_set_scale(font, 2048, 2048);
        for (i = 0; i < NUM_TEXTS; i++)
            Shape(font, texts[i], i & 1);

        hb_variation_from_string("wght=700", -1, &var);
        hb_font_set_variations(font, &var, 1);
        hb_font_set_synthetic_slant(font, 0.2f);
        for (i = 0; i < NUM_TEXTS; i++)
            Shape(font, texts[i], 0);

        count = 32;
        hb_ot_layout_table_get_script_tags(face, HB_OT_TAG_GSUB, 0, &count,
                                           tags);
        count = 32;
        hb_ot_layout_table_get_feature_tags(face, HB_OT_TAG_GPOS, 0, &count,
                                            tags);
        hb_ot_layout_has_glyph_classes(face);
        hb_font_get_h_extents(font, &fext);
        for (i = 0; i < 8; i++)
        {
            hb_font_get_glyph_extents(font, i, &ext);
            hb_font_get_glyph_name(font, i, name, sizeof(name));
        }
        hb_font_destroy(font);
        hb_face_destroy(face);
    }
    hb_blob_destroy(blob);
}

int main(void)
{
    static const char template[] = "PACK/A,FROM/K/N,LIB/K";
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rda;
    BPTR fh;
    UBYTE head[8], *data = NULL;
    ULONG n, i, from, before, after;
    int rc = RETURN_OK;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"hbfuzz");
        return RETURN_FAIL;
    }
    from = args[1] ? *(LONG *)args[1] : 0;
    HarfBuzzBase = OpenLibrary((CONST_STRPTR)(args[2] ? (const char *)args[2]
                                              : "harfbuzz.library"), 0);
    if (!HarfBuzzBase)
    {
        printf("hbfuzz: cannot open harfbuzz.library\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    fh = Open((CONST_STRPTR)args[0], MODE_OLDFILE);
    if (!fh || Read(fh, head, 8) != 8 || memcmp(head, "HBFZ", 4))
    {
        printf("hbfuzz: not a pack file\n");
        rc = RETURN_ERROR;
        goto out;
    }
    n = Get32(head + 4);
    printf("%.*s, %lu fonts\n",
           (int)strcspn((const char *)HarfBuzzBase->lib_IdString, "\r\n"),
           (const char *)HarfBuzzBase->lib_IdString, (unsigned long)n);
    before = AvailMem(MEMF_ANY);

    for (i = 0; i < n; i++)
    {
        UBYTE rec[8];
        char name[256];
        ULONG nlen, size;

        if (Read(fh, rec, 8) != 8)
            break;
        nlen = Get32(rec);
        size = Get32(rec + 4);
        if (nlen >= sizeof(name) || Read(fh, name, nlen) != (LONG)nlen)
            break;
        name[nlen] = 0;
        if (i < from)
        {
            Seek(fh, size, OFFSET_CURRENT);
            continue;
        }
        if (!(data = AllocVec(size ? size : 1, MEMF_ANY)) ||
            Read(fh, data, size) != (LONG)size)
        {
            printf("%lu %s: cannot read\n", (unsigned long)i, name);
            rc = RETURN_ERROR;
            break;
        }
        printf("%lu %s (%lu bytes)\n", (unsigned long)i, name,
               (unsigned long)size);
        fflush(stdout);
        OneFont(data, size);
        FreeVec(data);
        data = NULL;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
    }
    after = AvailMem(MEMF_ANY);
    printf("done: %lu of %lu fonts; free memory before %lu, after %lu\n",
           (unsigned long)i, (unsigned long)n, (unsigned long)before,
           (unsigned long)after);
out:
    if (data)
        FreeVec(data);
    if (fh)
        Close(fh);
    CloseLibrary(HarfBuzzBase);
    FreeArgs(rda);
    return rc;
}
