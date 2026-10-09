#ifndef LIBRARIES_AATEXT_H
#define LIBRARIES_AATEXT_H

/*
 * aatext.library 1.0 - definitions for programs and font installers.
 *
 * aatext.library is AAText's library: FreeType 2.14.3 as a shared library
 * (see <proto/aatext.h>) and a bullet.library-compatible outline engine for
 * diskfont.library (OT_Engine "aatext" in a .otag file). It does not
 * replace freetype2.library; both can be installed.
 *
 * The .otag tags are those of freetype2.library (FTManager), so a .otag
 * file made for freetype2.library works with aatext.library when its
 * OT_Engine is changed to "aatext".
 */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef DISKFONT_DISKFONTTAG_H
#include <diskfont/diskfonttag.h>
#endif

#define AATEXTLIBNAME     "aatext.library"
#define AATEXTLIBVERSION  1     /* FreeType 2.14 API */

/* OT_Engine value */
#define OTE_AAText        "aatext"

/* ---- engine specific .otag tags (as written by FTManager) ------------ */

/* font file (.ttf, .ttc, .otf, .pfb, .pfa, .cff, .pcf, .bdf, .fnt, ...) */
#define OT_Spec1_FontFile     (OT_Level1 | OT_Indirect | 0x101)
/* code page: 256 UWORDs, Amiga character code -> Unicode; without it
   ENV:ftcodepage is used, and without that ISO-8859-1 */
#define OT_Spec2_CodePage     (OT_Level1 | OT_Indirect | 0x102)
#define OT_Spec2_DefCodePage  (OT_Level1 | 0x102)
/* AFM or PFM metrics file for Type 1 fonts */
#define OT_Spec3_AFMFile      (OT_Level1 | OT_Indirect | 0x103)
/* which font metrics give the em-square height, METRIC_... */
#define OT_Spec4_Metric       (OT_Level1 | 0x104)
/* custom metrics for METRIC_CUSTOMBBOX: yMax << 16 | (UWORD)yMin */
#define OT_Spec5_BBox         (OT_Level1 | 0x105)
/* face index in a .ttc/.otc collection */
#define OT_Spec6_FaceNum      (OT_Level1 | 0x106)
/* embedded bitmap size (0 = scalable) - accepted, not used */
#define OT_Spec7_BMSize       (OT_Level1 | 0x107)
/* old tag for OT_GlyphMap8Bit (freetype2.library 1.x) */
#define OT_GlyphMap8Bit_Old   (OT_Level1 | 0x108)
/* hinting, HINTER_... */
#define OT_Spec9_Hinter       (OT_Level1 | 0x109)

/* OT_Spec4_Metric values */
#define METRIC_GLOBALBBOX     0     /* default: font bounding box */
#define METRIC_RAW_EM         1
#define METRIC_ASCEND         2     /* hhea ascender/descender */
#define METRIC_TYPOASCEND     3     /* OS/2 sTypoAscender/Descender */
#define METRIC_USWINASCEND    4     /* OS/2 usWinAscent/Descent */
#define METRIC_CUSTOMBBOX     5     /* OT_Spec5_BBox */
#define METRIC_BMSIZE         6

/* OT_Spec9_Hinter values */
#define HINTER_DEFAULT        0     /* the font's own hinting */
#define HINTER_FORCEAUTO      1     /* FreeType's autohinter */
#define HINTER_NONE           2     /* no hinting */
#define HINTER_LIGHT          3     /* aatext.library: light autohinting,
                                       vertical only, keeps letter shapes */

#endif
