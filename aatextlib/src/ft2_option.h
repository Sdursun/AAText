/*
 * freetype2.library FreeType configuration: the stock ftoption.h with the
 * options a shared AmigaOS 3 library cannot or need not have switched off.
 */
#ifndef AATEXTLIB_FT2_OPTION_H
#define AATEXTLIB_FT2_OPTION_H

#include <freetype/config/ftoption.h>

/* no external libraries on AmigaOS 3; zlib is FreeType's own copy */
#undef FT_CONFIG_OPTION_SYSTEM_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_BROTLI

/* OT-SVG glyphs need an SVG renderer hook nobody provides here */
#undef FT_CONFIG_OPTION_SVG

/* no Mac resource forks on Amiga file systems */
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK

/* error strings for FT_Error_String() */
#define FT_CONFIG_OPTION_ERROR_STRINGS

/* AmigaOS 3 code pages are Latin, Greek and Cyrillic: the autohinter's
   CJK and Indic modules only cost space */
#undef AF_CONFIG_OPTION_CJK
#undef AF_CONFIG_OPTION_INDIC

/* FreeType 2.15: variable composite glyphs ('VARC'), made for CJK
   variable fonts; nothing in AmigaOS 3 code pages needs them */
#undef TT_CONFIG_OPTION_VARC

#endif
