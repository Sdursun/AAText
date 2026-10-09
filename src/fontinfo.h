#ifndef AATEXT_FONTINFO_H
#define AATEXT_FONTINFO_H

/*
 * What AATextManager needs to know about a font file to write its .otag:
 * names, style, weight and width class, spacing. Read with FreeType: in
 * AATextManager from aatext.library, in the host tests from the static
 * FreeType (same source, see Makefile).
 */

#include <exec/types.h>

struct AAFontInfo
{
    char  family[64];
    char  style[64];
    LONG  numfaces;         /* faces in the file (.ttc: more than 1) */
    BOOL  bold, italic, fixed, serif;
    UWORD weight;           /* OS/2 usWeightClass, 400 if unknown */
    UWORD width;            /* OS/2 usWidthClass 1..9, 5 if unknown */
    UWORD unitsperem;
    ULONG spaceadvance;     /* advance of the space, font units */

    /* the same as .otag values */
    ULONG stemweight;       /* OT_StemWeight */
    ULONG horizstyle;       /* OT_HorizStyle */
    ULONG slantstyle;       /* OT_SlantStyle */
    ULONG spacewidth;       /* OT_SpaceWidth */
};

/*
 * Read face number face of the font file at path. FALSE if FreeType
 * cannot open it (not a font, missing, or FreeType not available).
 */
BOOL aa_GetFontInfo(const char *path, LONG face, struct AAFontInfo *info);

/*
 * The FTManager style file name for a face: family and style, lower
 * case, letters and digits only ("verdanaregular"); at most max-1
 * characters.
 */
void aa_FontBaseName(const struct AAFontInfo *info, char *buf, LONG max);

#endif
