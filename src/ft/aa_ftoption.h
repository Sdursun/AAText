/*
 * AAText FreeType configuration: the stock ftoption.h with everything
 * we do not need switched off. Fonts are only ever opened from memory.
 */
#ifndef AA_FTOPTION_H
#define AA_FTOPTION_H

#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES   /* no getenv() */
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB
/* FT_ and TT_CONFIG_OPTION_POSTSCRIPT_NAMES stay on: the CFF driver needs the
   psnames service for (non-CID) CFF fonts. */
#undef FT_CONFIG_OPTION_ADOBE_GLYPH_LIST
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_INCREMENTAL
#undef FT_CONFIG_OPTION_SVG

#define FT_CONFIG_OPTION_DISABLE_STREAM_SUPPORT     /* memory fonts only */

#undef TT_CONFIG_OPTION_EMBEDDED_BITMAPS          /* always use outlines */
#undef TT_CONFIG_OPTION_COLOR_LAYERS
#undef TT_CONFIG_OPTION_GX_VAR_SUPPORT
#undef TT_CONFIG_OPTION_BDF

/* CFF only, no Type 1 fonts: no AFM metrics files */
#define T1_CONFIG_OPTION_NO_AFM

/* autohinter: Amiga code pages are Latin/Greek/Cyrillic only */
#undef AF_CONFIG_OPTION_CJK
#undef AF_CONFIG_OPTION_INDIC

#endif
