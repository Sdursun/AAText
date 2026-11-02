/*
 * ft2bullet - drives the bullet.library API of aatext.library the way
 * diskfont.library does, and prints what comes back.
 *
 *   ft2bullet <file.otag> | FONT <fontfile> [SIZE <points>] [TEXT <string>]
 *             [AA] [ART] [WIDTHS] [BOLD] [LIB <name>]
 *
 * For each character: the glyph map fields (and with ART the bitmap).
 * AA asks for 8-bit antialiased maps (OT_GlyphMap8Bit). WIDTHS prints the
 * width list for the text's characters and kerning of neighbours.
 * The output is plain text, so two library versions can be compared.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <diskfont/diskfonttag.h>
#include <diskfont/glyph.h>
#include <diskfont/oterrors.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/aatext.h>

#include <stdio.h>
#include <string.h>

#ifndef OT_GlyphMap8Bit
#define OT_GlyphMap8Bit (OT_Level0 | OT_Indirect | 0x1a)
#endif

struct Library *AATextBase;

static const char Template[] = "OTAG,FONT/K,SIZE/K/N,TEXT/K,AA/S,ART/S,WIDTHS/S,BOLD/S,LIB/K";
enum { A_OTAG, A_FONT, A_SIZE, A_TEXT, A_AA, A_ART, A_WIDTHS, A_BOLD, A_LIB, A_COUNT };

static const char Shades[] = " .:-=+*#%@";

/* Loads a .otag file and makes its indirect tag data absolute pointers,
   like diskfont.library does. */
static struct TagItem *LoadOTag(const char *name, ULONG *size)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    struct TagItem *tags, *ti;
    LONG len;

    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    tags = len > 8 ? AllocVec(len, MEMF_ANY) : NULL;
    if (tags && Read(fh, tags, len) != len)
    {
        FreeVec(tags);
        tags = NULL;
    }
    Close(fh);
    if (!tags)
        return NULL;
    for (ti = tags; (UBYTE *)ti < (UBYTE *)tags + len && ti->ti_Tag != TAG_DONE; ti++)
        if (ti->ti_Tag & OT_Indirect)
            ti->ti_Data += (ULONG)tags;
    *size = len;
    return tags;
}

static void ShowMap(struct GlyphMap *gm, BOOL aa)
{
    UWORD x, y;

    for (y = 0; y < gm->glm_BMRows; y++)
    {
        UBYTE *row = gm->glm_BitMap + y * gm->glm_BMModulo;

        printf("    |");
        for (x = 0; x < gm->glm_BlackLeft + gm->glm_BlackWidth && x < 70; x++)
        {
            if (aa)
                putchar(Shades[row[x] * 9 / 255]);
            else
                putchar(row[x >> 3] & (0x80 >> (x & 7)) ? '#' : '.');
        }
        printf("|\n");
    }
}

int main(void)
{
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    const char *libname = "aatext.library";
    const UBYTE *text;
    struct TagItem *otag;
    struct GlyphEngine *ge;
    ULONG otagsize, points, i, len;
    struct TagItem t[6];
    int rc = RETURN_OK;

    rda = ReadArgs((STRPTR)Template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"ft2bullet");
        return RETURN_FAIL;
    }
    if (args[A_LIB])
        libname = (const char *)args[A_LIB];
    points = args[A_SIZE] ? *(LONG *)args[A_SIZE] : 13;
    text = args[A_TEXT] ? (const UBYTE *)args[A_TEXT]
                        : (const UBYTE *)"AVWag.\xD0\xF0\xDD\xFD\xDE\xFE";
    len = strlen((const char *)text);

    if (args[A_OTAG])
        otag = LoadOTag((const char *)args[A_OTAG], &otagsize);
    else if (args[A_FONT])
    {
        /* a minimal .otag: just the font file */
        otag = AllocVec(2 * sizeof(struct TagItem), MEMF_ANY | MEMF_CLEAR);
        if (otag)
        {
            otag[0].ti_Tag = OT_Spec1_FontFile;
            otag[0].ti_Data = args[A_FONT];
            otag[1].ti_Tag = TAG_DONE;
        }
    }
    else
        otag = NULL;
    if (!otag)
    {
        printf("need a .otag file or FONT\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }

    AATextBase = OpenLibrary((STRPTR)libname, 0);
    if (!AATextBase)
    {
        printf("cannot open %s\n", libname);
        FreeVec(otag);
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    printf("%s %d.%d\n", AATextBase->lib_Node.ln_Name,
           AATextBase->lib_Version, AATextBase->lib_Revision);

    ge = OpenEngine();
    if (!ge)
    {
        printf("OpenEngine failed\n");
        rc = RETURN_ERROR;
        goto done;
    }
    printf("engine %s\n", ge->gle_Name);

    t[0].ti_Tag = args[A_OTAG] ? OT_OTagPath : TAG_IGNORE;
    t[0].ti_Data = args[A_OTAG];
    t[1].ti_Tag = OT_OTagList;   t[1].ti_Data = (ULONG)otag;
    t[2].ti_Tag = TAG_DONE;
    printf("SetInfo otag: %ld\n", SetInfoA(ge, t));

    t[0].ti_Tag = OT_DeviceDPI;   t[0].ti_Data = (72 << 16) | 72;
    t[1].ti_Tag = OT_PointHeight; t[1].ti_Data = points << 16;
    t[2].ti_Tag = TAG_DONE;
    printf("SetInfo size %ld: %ld\n", points, SetInfoA(ge, t));

    if (args[A_BOLD])
    {
        /* what diskfont.library sets for FSF_BOLD */
        t[0].ti_Tag = OT_EmboldenX; t[0].ti_Data = 0xE75;
        t[1].ti_Tag = OT_EmboldenY; t[1].ti_Data = 0x99E;
        t[2].ti_Tag = TAG_DONE;
        printf("SetInfo bold: %ld\n", SetInfoA(ge, t));
    }

    for (i = 0; i < len; i++)
    {
        struct GlyphMap *gm = NULL;
        ULONG err;
        Tag tag = args[A_AA] ? OT_GlyphMap8Bit : OT_GlyphMap;

        t[0].ti_Tag = OT_GlyphCode; t[0].ti_Data = text[i];
        t[1].ti_Tag = TAG_DONE;
        SetInfoA(ge, t);

        t[0].ti_Tag = tag; t[0].ti_Data = (ULONG)&gm;
        t[1].ti_Tag = TAG_DONE;
        err = ObtainInfoA(ge, t);
        if (err || !gm)
        {
            printf("'%c' $%02lx: error %ld\n", text[i], (ULONG)text[i], err);
            continue;
        }
        printf("'%c' $%02lx: mod %u rows %u black %u,%u %ux%u org %08lx,%08lx "
               "x0y0 %d,%d x1y1 %d,%d width %08lx\n",
               text[i], (ULONG)text[i], gm->glm_BMModulo, gm->glm_BMRows,
               gm->glm_BlackLeft, gm->glm_BlackTop, gm->glm_BlackWidth,
               gm->glm_BlackHeight, gm->glm_XOrigin, gm->glm_YOrigin,
               gm->glm_X0, gm->glm_Y0, gm->glm_X1, gm->glm_Y1, gm->glm_Width);
        if (args[A_ART])
            ShowMap(gm, args[A_AA] != 0);

        t[0].ti_Tag = tag; t[0].ti_Data = (ULONG)gm;
        t[1].ti_Tag = TAG_DONE;
        ReleaseInfoA(ge, t);
    }

    if (args[A_WIDTHS])
    {
        struct MinList *wl = NULL;
        struct GlyphWidthEntry *we;
        ULONG err;

        t[0].ti_Tag = OT_GlyphCode;  t[0].ti_Data = 32;
        t[1].ti_Tag = OT_GlyphCode2; t[1].ti_Data = 255;
        t[2].ti_Tag = TAG_DONE;
        SetInfoA(ge, t);
        t[0].ti_Tag = OT_WidthList; t[0].ti_Data = (ULONG)&wl;
        t[1].ti_Tag = TAG_DONE;
        err = ObtainInfoA(ge, t);
        printf("width list: error %ld\n", err);
        if (!err && wl)
        {
            ULONG n = 0;

            for (we = (struct GlyphWidthEntry *)wl->mlh_Head;
                 we->gwe_Node.mln_Succ;
                 we = (struct GlyphWidthEntry *)we->gwe_Node.mln_Succ)
            {
                for (i = 0; i < len; i++)
                    if (we->gwe_Code == text[i])
                    {
                        printf("  width $%02x %08lx\n", we->gwe_Code, we->gwe_Width);
                        break;
                    }
                n++;
            }
            printf("  %ld entries\n", n);
            t[0].ti_Tag = OT_WidthList; t[0].ti_Data = (ULONG)wl;
            ReleaseInfoA(ge, t);
        }

        for (i = 0; i + 1 < len; i++)
        {
            FIXED kern = 0;

            t[0].ti_Tag = OT_GlyphCode;  t[0].ti_Data = text[i];
            t[1].ti_Tag = OT_GlyphCode2; t[1].ti_Data = text[i + 1];
            t[2].ti_Tag = TAG_DONE;
            SetInfoA(ge, t);
            t[0].ti_Tag = OT_TextKernPair; t[0].ti_Data = (ULONG)&kern;
            t[1].ti_Tag = TAG_DONE;
            err = ObtainInfoA(ge, t);
            printf("  kern '%c%c' %08lx (error %ld)\n", text[i], text[i + 1], kern, err);
        }
    }

    CloseEngine(ge);
done:
    CloseLibrary(AATextBase);
    FreeVec(otag);
    FreeArgs(rda);
    return rc;
}
