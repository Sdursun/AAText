/*
 * ft2info - checks the FreeType API of aatext.library.
 *
 *   ft2info [FONT <file>] [SIZE <pixels>] [TEXT <string>] [LIB <name>]
 *
 * Prints the library and FreeType versions; with a font, its names and
 * format, and the text rendered antialiased as ASCII art.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/aatext.h>

#include <stdio.h>
#include <string.h>

struct Library *AATextBase;

static const char Template[] = "FONT/K,SIZE/K/N,TEXT/K,LIB/K";
enum { A_FONT, A_SIZE, A_TEXT, A_LIB, A_COUNT };

static const char Shades[] = " .:-=+*#%@";

/* Latin-1 / ISO-8859-9 text: the Amiga character codes as Unicode,
   except for the Turkish letters of ISO-8859-9 */
static FT_ULong ToUnicode(UBYTE c)
{
    switch (c)
    {
        case 0xD0: return 0x011E;   /* G breve */
        case 0xDD: return 0x0130;   /* I dot */
        case 0xDE: return 0x015E;   /* S cedilla */
        case 0xF0: return 0x011F;
        case 0xFD: return 0x0131;   /* dotless i */
        case 0xFE: return 0x015F;
    }
    return c;
}

static void ShowText(FT_Face face, const char *text, int size)
{
    enum { W = 78, H = 40 };
    static UBYTE canvas[H][W];
    int pen = 0, baseline, len = strlen(text), i, x, y;

    if (size > H - 4)
        size = H - 4;
    if (FT_Set_Pixel_Sizes(face, 0, size))
    {
        printf("FT_Set_Pixel_Sizes failed\n");
        return;
    }
    memset(canvas, 0, sizeof(canvas));
    baseline = (face->size->metrics.ascender + 63) >> 6;
    if (baseline >= H)
        baseline = H - 1;

    for (i = 0; i < len; i++)
    {
        FT_GlyphSlot slot;
        FT_Bitmap *bm;
        int gx, gy;

        if (FT_Load_Char(face, ToUnicode((UBYTE)text[i]), FT_LOAD_RENDER))
            continue;
        slot = face->glyph;
        bm = &slot->bitmap;
        for (y = 0; y < (int)bm->rows; y++)
            for (x = 0; x < (int)bm->width; x++)
            {
                gx = pen + slot->bitmap_left + x;
                gy = baseline - slot->bitmap_top + y;
                if (gx >= 0 && gx < W && gy >= 0 && gy < H)
                    canvas[gy][gx] = bm->buffer[y * bm->pitch + x];
            }
        pen += slot->advance.x >> 6;
    }

    for (y = 0; y < H; y++)
    {
        char line[W + 1];
        int used = 0;

        for (x = 0; x < W; x++)
        {
            line[x] = Shades[canvas[y][x] * 9 / 255];
            if (canvas[y][x])
                used = 1;
        }
        line[W] = 0;
        if (used)
            printf("|%s\n", line);
    }
}

int main(void)
{
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    const char *libname = "aatext.library";
    FT_Library lib;
    FT_Int major, minor, patch;
    int rc = RETURN_OK;

    rda = ReadArgs((STRPTR)Template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"ft2info");
        return RETURN_FAIL;
    }
    if (args[A_LIB])
        libname = (const char *)args[A_LIB];

    AATextBase = OpenLibrary((STRPTR)libname, 0);
    if (!AATextBase)
    {
        printf("cannot open %s\n", libname);
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    printf("library:  %s %d.%d\n", AATextBase->lib_Node.ln_Name,
           AATextBase->lib_Version, AATextBase->lib_Revision);
    printf("id:       %s", (char *)AATextBase->lib_IdString);

    if (FT_Init_FreeType(&lib))
    {
        printf("FT_Init_FreeType failed\n");
        rc = RETURN_ERROR;
    }
    else
    {
        FT_Library_Version(lib, &major, &minor, &patch);
        printf("FreeType: %d.%d.%d\n", major, minor, patch);

        if (args[A_FONT])
        {
            FT_Face face;
            FT_Error err = FT_New_Face(lib, (const char *)args[A_FONT], 0, &face);

            if (err)
            {
                printf("FT_New_Face: error %d (%s)\n", err,
                       FT_Error_String(err) ? FT_Error_String(err) : "?");
                rc = RETURN_ERROR;
            }
            else
            {
                printf("family:   %s\n", face->family_name ? face->family_name : "?");
                printf("style:    %s\n", face->style_name ? face->style_name : "?");
                printf("format:   %s\n", FT_Get_Font_Format(face));
                printf("glyphs:   %ld, units/em %d, faces %ld%s%s\n",
                       face->num_glyphs, face->units_per_EM, face->num_faces,
                       FT_HAS_KERNING(face) ? ", kerning" : "",
                       FT_HAS_MULTIPLE_MASTERS(face) ? ", variable" : "");
                ShowText(face, args[A_TEXT] ? (const char *)args[A_TEXT] : "AaGg\xD0\xF0\xDE\xFE\xFD",
                         args[A_SIZE] ? *(LONG *)args[A_SIZE] : 24);
                FT_Done_Face(face);
            }
        }
        FT_Done_FreeType(lib);
    }

    CloseLibrary(AATextBase);
    FreeArgs(rda);
    return rc;
}
