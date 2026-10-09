#ifndef AATEXTMANAGER_STRINGS_H
#define AATEXTMANAGER_STRINGS_H

/*
 * User interface strings of AATextManager. English is built in; other
 * languages come from aatextmanager.catalog (locale.library). The IDs
 * are part of the catalog format: never renumber, only append. A "_"
 * marks a button's keyboard shortcut.
 * Translations: catalogs/manager-<language>.ct, compiled by
 * tools/mkcatalog.pl.
 */

#include <exec/types.h>

#define AA_STRINGS \
    S(MSG_TITLE,            0,  "AATextManager") \
    S(MSG_TAB_INSTALL,      1,  "Install") \
    S(MSG_TAB_REPAIR,       2,  "Repair") \
    S(MSG_CHARSET,          3,  "Character set") \
    S(MSG_ENGINE,           4,  "Engine") \
    S(MSG_ENGINE_AATEXT,    5,  "aatext (aatext.library)") \
    S(MSG_ENGINE_FT2,       6,  "freetype2 (freetype2.library)") \
    S(MSG_ADD,              7,  "_Add fonts...") \
    S(MSG_REMOVE,           8,  "_Remove") \
    S(MSG_COL_FAMILY,       9,  "Family") \
    S(MSG_COL_STYLE,        10, "Style") \
    S(MSG_COL_FACE,         11, "Face") \
    S(MSG_COL_FILE,         12, "File") \
    S(MSG_SIZES,            13, "Sizes") \
    S(MSG_OVERWRITE,        14, "Replace fonts of the same name") \
    S(MSG_INSTALL,          15, "_Install") \
    S(MSG_ADD_TITLE,        16, "Choose TrueType/OpenType fonts") \
    S(MSG_NOTFONT,          17, "%s is not a font FreeType can read.") \
    S(MSG_BADSIZES,         18, "Sizes: for example 8-16,18,20,24") \
    S(MSG_INSTALLED,        19, "%ld installed, %ld skipped (name taken), %ld failed.") \
    S(MSG_CHECK,            20, "_Check fonts") \
    S(MSG_REPAIR,           21, "Re_pair...") \
    S(MSG_COL_FONT,         22, "Font") \
    S(MSG_COL_PROBLEM,      23, "To repair") \
    S(MSG_P_MOVED,          24, "font file moved") \
    S(MSG_P_CODEPAGE,       25, "no code page") \
    S(MSG_P_ENGINE,         26, "engine freetype2") \
    S(MSG_NOTHING,          27, "Nothing to repair.") \
    S(MSG_TOREPAIR,         28, "%ld fonts to repair.") \
    S(MSG_REPAIR_ASK,       29, "Repair %ld fonts?\n\nThe original .otag files are kept as .otag.bak.") \
    S(MSG_REPAIR_GADS,      30, "Repair|Cancel") \
    S(MSG_REPAIRED,         31, "%ld of %ld fonts repaired.") \
    S(MSG_CHANGE_ENGINE,    32, "Change engine freetype2 to the chosen one") \
    S(MSG_INFO_INSTALL,     33, "Font files are copied to FONTS:_ttf/, .font and .otag are written to FONTS:.") \
    S(MSG_INFO_REPAIR,      34, "Checks the .otag files in FONTS:. Code page: the character set above.") \
    S(MSG_NOTOPEN,          35, "Installed, but diskfont cannot open %s.") \
    S(MSG_NOFONTS,          36, "Add font files first.") \
    S(MSG_FONTSERR,         37, "Cannot read FONTS:.") \
    S(MSG_CHARSET_LATIN1,   40, "ISO-8859-1 (Western European)") \
    S(MSG_CHARSET_LATIN2,   41, "ISO-8859-2 (Central European)") \
    S(MSG_CHARSET_LATIN3,   42, "ISO-8859-3 (South European)") \
    S(MSG_CHARSET_LATIN4,   43, "ISO-8859-4 (North European)") \
    S(MSG_CHARSET_LATIN5,   44, "ISO-8859-9 (Turkish)") \
    S(MSG_CHARSET_LATIN9,   45, "ISO-8859-15 (Western European, euro)") \
    S(MSG_CHARSET_LATIN10,  46, "ISO-8859-16 (South-Eastern European)") \
    S(MSG_CHARSET_CP1250,   47, "Windows-1250 (Central European)") \
    S(MSG_CHARSET_CYRILLIC, 48, "ISO-8859-5 (Cyrillic)") \
    S(MSG_CHARSET_KOI8R,    49, "KOI8-R (Russian)")

#define S(id, n, s) id = n,
enum { AA_STRINGS AA_NUM_STRINGS };
#undef S

/* Open the catalog; language NULL = the user's preferred languages. */
void InitStrings(const char *language);
void FreeStrings(void);
const char *GetString(LONG id);

#endif
