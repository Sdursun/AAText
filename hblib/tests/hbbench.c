/*
 * hbbench - shaping speed of harfbuzz.library: lines and characters
 * shaped per second, for a few kinds of text.
 *
 *   hbbench FONT <file> [SECS <n>] [LIB <path>]
 *
 * Each test shapes the same line again and again for SECS seconds
 * (default 3) after a warm-up, in one buffer that is cleared each time,
 * as a program shaping the lines of a document would.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <stdio.h>
#include <string.h>

#include <proto/harfbuzz.h>

struct Library *HarfBuzzBase;
struct Device *TimerBase;

static const struct
{
    const char *name;
    const char *lang;
    const char *text;           /* UTF-8 */
} tests[] =
{
    { "latin", "en", "The quick brown fox jumps over the lazy dog. "
                     "AVATAR Wave To. office affluent 0123456789" },
    { "turkish", "tr", "Pijamal\xC4\xB1 hasta ya\xC4\x9F\xC4\xB1z "
                       "\xC5\x9F" "of\xC3\xB6re \xC3\xA7" "abucak g\xC3\xBC"
                       "vendi. \xC4\xB0\xC4\x9E\xC5\x9E\xC3\x87\xC3\x96"
                       "\xC3\x9C \xC3\xA7\xC4\x9F\xC4\xB1\xC3\xB6\xC5\x9F"
                       "\xC3\xBC" },
    { "marks", "en", "e\xCC\x81 a\xCC\x88 o\xCC\x83 n\xCC\xA7 "
                     "u\xCC\x88\xCC\x81 c\xCC\xA7 i\xCC\x87 "
                     "e\xCC\x81 a\xCC\x88 o\xCC\x83 n\xCC\xA7" },
    { "arabic", "ar", "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "
                      "\xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84"
                      "\xD9\x85 \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "
                      "\xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84"
                      "\xD9\x85" }
};
#define NUM_TESTS (sizeof(tests) / sizeof(tests[0]))

static UBYTE *ReadAll(const char *name, ULONG *size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG len;

    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len > 0 && (buf = AllocVec(len, MEMF_ANY)) &&
        Read(fh, buf, len) != len)
    {
        FreeVec(buf);
        buf = NULL;
    }
    Close(fh);
    *size = len;
    return buf;
}

static double Seconds(ULONG freq)
{
    struct EClockVal e;

    ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + e.ev_lo) / freq;
}

static void ShapeLine(hb_font_t *font, hb_buffer_t *buf, ULONG t)
{
    hb_buffer_clear_contents(buf);
    hb_buffer_add_utf8(buf, tests[t].text, -1, 0, -1);
    hb_buffer_set_language(buf, hb_language_from_string(tests[t].lang, -1));
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font, buf, NULL, 0);
}

int main(void)
{
    static const char template[] = "FONT/A,SECS/K/N,LIB/K";
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rda;
    struct timerequest tr;
    ULONG freq, size, t, secs;
    UBYTE *data = NULL;
    int rc = RETURN_OK;

    rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"hbbench");
        return RETURN_FAIL;
    }
    secs = args[1] ? *(LONG *)args[1] : 3;
    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&tr, 0))
        return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;
    {
        struct EClockVal e;

        freq = ReadEClock(&e);
    }
    HarfBuzzBase = OpenLibrary((CONST_STRPTR)(args[2] ? (const char *)args[2]
                                              : "harfbuzz.library"), 0);
    if (!HarfBuzzBase || !(data = ReadAll((const char *)args[0], &size)))
    {
        printf("hbbench: cannot open harfbuzz.library or the font\n");
        rc = RETURN_FAIL;
        goto out;
    }
    printf("%.*s, %s, %lu s per test\n",
           (int)strcspn((const char *)HarfBuzzBase->lib_IdString, "\r\n"),
           (const char *)HarfBuzzBase->lib_IdString,
           (const char *)args[0], (unsigned long)secs);
    printf("%-9s %10s %12s %10s\n", "text", "lines/s", "chars/s", "us/line");
    {
        hb_blob_t *blob = hb_blob_create((const char *)data, size,
                                         HB_MEMORY_MODE_READONLY, NULL, NULL);
        hb_face_t *face = hb_face_create(blob, 0);
        hb_font_t *font = hb_font_create(face);
        hb_buffer_t *buf = hb_buffer_create();

        hb_font_set_scale(font, 16 * 64, 16 * 64);
        for (t = 0; t < NUM_TESTS; t++)
        {
            ULONG i, chars = hb_buffer_get_length(buf);
            double t0, t1;

            for (i = 0; i < 20; i++)        /* warm-up: shape plans */
                ShapeLine(font, buf, t);
            chars = hb_buffer_get_length(buf);
            t0 = t1 = Seconds(freq);
            for (i = 0; t1 - t0 < secs; i++)
            {
                ShapeLine(font, buf, t);
                if ((i & 7) == 7)
                    t1 = Seconds(freq);
            }
            t1 = Seconds(freq);
            printf("%-9s %10.0f %12.0f %10.1f\n", tests[t].name,
                   i / (t1 - t0), i * (double)chars / (t1 - t0),
                   (t1 - t0) * 1e6 / i);
        }
        hb_buffer_destroy(buf);
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
out:
    if (data)
        FreeVec(data);
    if (HarfBuzzBase)
        CloseLibrary(HarfBuzzBase);
    CloseDevice((struct IORequest *)&tr);
    FreeArgs(rda);
    return rc;
}
