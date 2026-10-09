/*
 * AATextManager - font information for the .otag, read with FreeType.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_TRUETYPE_TABLES_H

#ifdef AA_USE_AATEXTLIB
/* FreeType from aatext.library; the program opens AATextBase */
#include <proto/aatext.h>
#endif

#include "fontinfo.h"

/*
 * OT_SpaceWidth is the space advance in units of 8820 per em: that is
 * what FTManager wrote for Verdana (720/2048 em -> 3100) and Source
 * Sans 3 (200/1000 em -> 1764), rounded down.
 * (Where 8820 comes from is not known; not verified for other fonts.)
 */
#define AA_SPACE_UNITS  8820

/* diskfont/diskfonttag.h OTS_* values */
static ULONG StemWeight(UWORD w)
{
    if (w <= 150) return 40;        /* OTS_Thin */
    if (w <= 250) return 56;        /* OTS_ExtraLight */
    if (w <= 350) return 72;        /* OTS_Light */
    if (w <= 450) return 120;       /* OTS_Book (FTManager: regular) */
    if (w <= 550) return 136;       /* OTS_Medium */
    if (w <= 650) return 152;       /* OTS_SemiBold */
    if (w <= 750) return 184;       /* OTS_Bold */
    if (w <= 850) return 200;       /* OTS_ExtraBold */
    return 216;                     /* OTS_Black */
}

/* diskfont/diskfonttag.h OTH_* values, usWidthClass 1..9 */
static ULONG HorizStyle(UWORD w)
{
    static const UBYTE h[9] = { 16, 48, 80, 112, 144, 176, 208, 240, 240 };

    return (w >= 1 && w <= 9) ? h[w - 1] : 144;     /* OTH_Normal */
}

/* The whole file in memory (FreeType streams are off in AAText's build). */
static UBYTE *LoadFile(const char *path, LONG *len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG n;

    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n > 0 && (buf = AllocVec(n, MEMF_ANY)) && Read(fh, buf, n) != n)
    {
        FreeVec(buf);
        buf = NULL;
    }
    Close(fh);
    *len = n;
    return buf;
}

static void CopyName(char *dst, const char *src, LONG max)
{
    strncpy(dst, src ? src : "", max - 1);
    dst[max - 1] = 0;
}

BOOL aa_GetFontInfo(const char *path, LONG facenum, struct AAFontInfo *info)
{
    FT_Library lib;
    FT_Face face;
    FT_Fixed adv = 0;
    FT_UInt gi;
    TT_OS2 *os2;
    UBYTE *buf;
    LONG len;
    BOOL ok = FALSE;

    memset(info, 0, sizeof(*info));
    if (!(buf = LoadFile(path, &len)))
        return FALSE;
    if (FT_Init_FreeType(&lib))
    {
        FreeVec(buf);
        return FALSE;
    }
    if (!FT_New_Memory_Face(lib, buf, len, facenum, &face))
    {
        CopyName(info->family, face->family_name, sizeof(info->family));
        CopyName(info->style, face->style_name, sizeof(info->style));
        info->numfaces = face->num_faces;
        info->bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
        info->italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
        info->fixed = FT_IS_FIXED_WIDTH(face) != 0;
        info->unitsperem = face->units_per_EM;
        info->weight = info->bold ? 700 : 400;
        info->width = 5;

        os2 = (TT_OS2 *)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
        if (os2 && os2->version != 0xFFFF)
        {
            if (os2->usWeightClass >= 1 && os2->usWeightClass <= 1000)
                info->weight = os2->usWeightClass;
            if (os2->usWidthClass >= 1 && os2->usWidthClass <= 9)
                info->width = os2->usWidthClass;
            /* PANOSE latin text: serif styles 2..10, sans 11..13 */
            info->serif = os2->panose[0] == 2 &&
                          os2->panose[1] >= 2 && os2->panose[1] <= 10;
        }

        gi = FT_Get_Char_Index(face, ' ');
        if (gi && !FT_Get_Advance(face, gi, FT_LOAD_NO_SCALE, &adv))
            info->spaceadvance = adv;
        else
            info->spaceadvance = info->unitsperem / 4;

        info->stemweight = StemWeight(info->weight);
        info->horizstyle = HorizStyle(info->width);
        info->slantstyle = info->italic ? 1 : 0;    /* OTS_Italic */
        if (info->unitsperem)
            info->spacewidth = info->spaceadvance * AA_SPACE_UNITS /
                              info->unitsperem;
        ok = info->family[0] != 0;
        FT_Done_Face(face);
    }
    FT_Done_FreeType(lib);
    FreeVec(buf);
    return ok;
}

void aa_FontBaseName(const struct AAFontInfo *info, char *buf, LONG max)
{
    const char *s[2];
    LONG n = 0, i;

    s[0] = info->family;
    s[1] = info->style;
    for (i = 0; i < 2; i++)
    {
        const char *p;

        for (p = s[i]; *p && n < max - 1; p++)
        {
            char c = *p;

            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                buf[n++] = c;
        }
    }
    buf[n] = 0;
}
