/*
 * hbinfo - shapes test strings with HarfBuzz and prints the glyphs, for
 * comparing the Amiga build with HarfBuzz on a PC (tests/hbinfo.py).
 *
 *   hbinfo FONT <file> [TIMES <n>]
 *
 * The font is shaped at its units per em, so the positions are integers
 * in font units and must equal HarfBuzz's anywhere. Lines starting with
 * '#' (timing, memory, stack) are not compared. TIMES: shapes per string
 * for the timing (default 200).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <stdio.h>
#include <string.h>

#include <hb.h>
#include <hb-ot.h>

#include "../src/hbmem.h"

struct Device *TimerBase;

/* UTF-8 */
static const char *const tests[] =
{
    "AVATAR Wave To. Yo, P.T.",                         /* kerning */
    "office fine flow affix",                           /* ligatures */
    "Pijamal\xC4\xB1 hasta ya\xC4\x9F\xC4\xB1z \xC5\x9F" "of\xC3\xB6re "
    "\xC3\xA7" "abucak g\xC3\xBC" "vendi. \xC4\xB0\xC4\x9E\xC5\x9E\xC3\x87"
    "\xC3\x96\xC3\x9C",                                 /* Turkish */
    "e\xCC\x81 a\xCC\x88 o\xCC\x83 n\xCC\xA7",          /* combining marks */
    "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "
    "\xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85" /* Arabic */
};
#define NUM_TESTS (sizeof(tests) / sizeof(tests[0]))

/* fixed, not the locale's: the output must not depend on the system */
static hb_language_t Language(ULONG i)
{
    return hb_language_from_string(i == NUM_TESTS - 1 ? "ar" : "en", -1);
}

static UBYTE *ReadAll(const char *name, ULONG *size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG len;

    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len > 0 && (buf = AllocVec(len, MEMF_ANY)))
    {
        if (Read(fh, buf, len) != len)
        {
            FreeVec(buf);
            buf = NULL;
        }
    }
    Close(fh);
    *size = len;
    return buf;
}

/* bytes of the stack below the current SP that were never written */
static ULONG StackUnused(struct Task *me)
{
    UBYTE *p = me->tc_SPLower;

    while (p < (UBYTE *)me->tc_SPUpper && *p == 0xA5)
        p++;
    return p - (UBYTE *)me->tc_SPLower;
}

static void StackFill(struct Task *me)
{
    UBYTE here;
    UBYTE *p = me->tc_SPLower;
    ULONG end = (ULONG)&here - 256;     /* our own frame stays */

    while ((ULONG)p < end)
        *p++ = 0xA5;
}

int main(void)
{
    static const char template[] = "FONT/A,TIMES/K/N";
    LONG args[2] = { 0, 0 };
    struct RDArgs *rda;
    struct timerequest tr;
    struct Task *me = FindTask(NULL);
    UBYTE *data;
    ULONG size, freq, i, times;
    hb_blob_t *blob;
    hb_face_t *face;
    hb_font_t *font;
    unsigned upem;
    int rc = RETURN_OK;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"hbinfo");
        return RETURN_FAIL;
    }
    times = args[1] ? *(LONG *)args[1] : 200;
    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&tr, 0))
        return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;
    {
        struct EClockVal e;

        freq = ReadEClock(&e);
    }
    if (!hbmem_Init())
    {
        rc = RETURN_FAIL;
        goto out;
    }
    data = ReadAll((const char *)args[0], &size);
    if (!data)
    {
        printf("hbinfo: cannot read %s\n", (const char *)args[0]);
        rc = RETURN_ERROR;
        goto out;
    }

    printf("# HarfBuzz %s\n", hb_version_string());
    blob = hb_blob_create((const char *)data, size, HB_MEMORY_MODE_READONLY,
                          NULL, NULL);
    face = hb_face_create(blob, 0);
    upem = hb_face_get_upem(face);
    font = hb_font_create(face);
    hb_font_set_scale(font, upem, upem);
    printf("font: %lu glyphs, %u units per em\n",
           (unsigned long)hb_face_get_glyph_count(face), upem);

    for (i = 0; i < NUM_TESTS; i++)
    {
        hb_buffer_t *buf = hb_buffer_create();
        hb_glyph_info_t *info;
        hb_glyph_position_t *pos;
        unsigned n, k;
        struct EClockVal t0, t1;
        ULONG unused;
        char props[64];

        StackFill(me);
        hb_buffer_add_utf8(buf, tests[i], -1, 0, -1);
        hb_buffer_set_language(buf, Language(i));
        hb_buffer_guess_segment_properties(buf);
        hb_shape(font, buf, NULL, 0);
        unused = StackUnused(me);

        info = hb_buffer_get_glyph_infos(buf, &n);
        pos = hb_buffer_get_glyph_positions(buf, &n);
        snprintf(props, sizeof(props), "%s %s",
                 hb_direction_to_string(hb_buffer_get_direction(buf)),
                 hb_language_to_string(hb_buffer_get_language(buf)) ?
                 hb_language_to_string(hb_buffer_get_language(buf)) : "-");
        printf("text %lu: %u glyphs, %s\n", (unsigned long)i + 1, n, props);
        for (k = 0; k < n; k++)
            printf("  g=%lu c=%lu ax=%ld ay=%ld dx=%ld dy=%ld\n",
                   (unsigned long)info[k].codepoint,
                   (unsigned long)info[k].cluster,
                   (long)pos[k].x_advance, (long)pos[k].y_advance,
                   (long)pos[k].x_offset, (long)pos[k].y_offset);
        printf("# stack used by hb_shape: about %lu bytes\n",
               (unsigned long)((UBYTE *)&props - (UBYTE *)me->tc_SPLower -
                               unused));

        ReadEClock(&t0);
        for (k = 0; k < times; k++)
        {
            hb_buffer_clear_contents(buf);
            hb_buffer_add_utf8(buf, tests[i], -1, 0, -1);
            hb_buffer_set_language(buf, Language(i));
            hb_buffer_guess_segment_properties(buf);
            hb_shape(font, buf, NULL, 0);
        }
        ReadEClock(&t1);
        {
            double s = (double)(t1.ev_lo - t0.ev_lo) / freq;
            unsigned len = strlen(tests[i]);

            if (s > 0)
                printf("# %lu shapes: %.0f us each, %.0f bytes/s\n",
                       (unsigned long)times, s * 1e6 / times,
                       len * times / s);
        }
        hb_buffer_destroy(buf);
    }

    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    FreeVec(data);
out:
    hbmem_Cleanup();
    CloseDevice((struct IORequest *)&tr);
    FreeArgs(rda);
    return rc;
}
