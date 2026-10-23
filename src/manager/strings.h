#ifndef AATEXTMANAGER_STRINGS_H
#define AATEXTMANAGER_STRINGS_H

/*
 * User interface strings of AATextManager. English is built in; other
 * languages come from aatextmanager.catalog (locale.library). The IDs
 * are part of the catalog format: never renumber, only append. A "_"
 * marks a button's keyboard shortcut. Retired IDs, never to be reused:
 * 87-94 (Fix .otag on the Diagnostics tab, removed in 0.32: the Repair
 * tab does it).
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
    S(MSG_INFO_INSTALL,     33, "Font files are copied to the directories on the Settings tab, .font and .otag are written to FONTS:.") \
    S(MSG_INFO_REPAIR,      34, "Checks the .otag files in FONTS:. Code page: the character set above.") \
    S(MSG_NOTOPEN,          35, "Installed, but diskfont cannot open %s.") \
    S(MSG_NOFONTS,          36, "Add font files first.") \
    S(MSG_FONTSERR,         37, "Cannot read FONTS:.") \
    S(MSG_PREVIEW,          38, "Preview") \
    S(MSG_PV_NOTRTG,        39, "The preview needs an RTG screen (15 bit or more).") \
    S(MSG_CHARSET_LATIN1,   40, "ISO-8859-1 (Western European)") \
    S(MSG_CHARSET_LATIN2,   41, "ISO-8859-2 (Central European)") \
    S(MSG_CHARSET_LATIN3,   42, "ISO-8859-3 (South European)") \
    S(MSG_CHARSET_LATIN4,   43, "ISO-8859-4 (North European)") \
    S(MSG_CHARSET_LATIN5,   44, "ISO-8859-9 (Turkish)") \
    S(MSG_CHARSET_LATIN9,   45, "ISO-8859-15 (Western European, euro)") \
    S(MSG_CHARSET_LATIN10,  46, "ISO-8859-16 (South-Eastern European)") \
    S(MSG_CHARSET_CP1250,   47, "Windows-1250 (Central European)") \
    S(MSG_CHARSET_CYRILLIC, 48, "ISO-8859-5 (Cyrillic)") \
    S(MSG_CHARSET_KOI8R,    49, "KOI8-R (Russian)") \
    S(MSG_PV_ALPHABET,      50, "AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz") \
    S(MSG_PV_SENTENCE,      51, "The quick brown fox jumps over the lazy dog. 0123456789") \
    S(MSG_P_HEIGHT,         52, "height from the bounding box (too small)") \
    S(MSG_MENU_PROJECT,     53, "Project") \
    S(MSG_MENU_UPDATE,      54, "Check for updates...") \
    S(MSG_MENU_QUIT,        55, "Quit") \
    S(MSG_UPD_CHECKING,     56, "Asking GitHub for the latest release...") \
    S(MSG_UPD_NEWER,        57, "AAText %s is available (this is %s).\n\nThe package was downloaded and saved as\n\n%s\n\nUnpack it with LhA and install it as described\nin AAText_EN.txt or AAText_TR.txt.") \
    S(MSG_UPD_CURRENT,      58, "AAText %s is the latest release.") \
    S(MSG_UPD_NONET,        59, "No network: bsdsocket.library is not available\n(is the TCP/IP stack running?).") \
    S(MSG_UPD_NOSSL,        60, "The update check needs AmiSSL 5\n(amisslmaster.library, on Aminet: util/libs/AmiSSL-5).") \
    S(MSG_UPD_FAILED,       61, "The update check failed:\n%s") \
    S(MSG_UPD_OK,           62, "OK") \
    S(MSG_TAB_DIAG,        63, "Diagnostics") \
    S(MSG_DIAG_INFO,       64, "Checks the .otag files in FONTS: and the font files they name. Scanning changes nothing.") \
    S(MSG_DIAG_SCAN,       65, "Scan fonts") \
    S(MSG_DIAG_REPORT,     66, "Save report...") \
    S(MSG_DIAG_SUMMARY,    67, "%ld fonts: %ld OK, %ld with problems") \
    S(MSG_DIAG_NOFONTS,    68, "Cannot read FONTS:.") \
    S(MSG_DIAG_COL_FONT,   69, "Font") \
    S(MSG_DIAG_COL_STATUS, 70, "Status") \
    S(MSG_DIAG_COL_FILE,   71, "Font file") \
    S(MSG_DIAG_OK,         72, "OK") \
    S(MSG_DIAG_MOVED,      73, "Moved") \
    S(MSG_DIAG_MISSING,    74, "Missing") \
    S(MSG_DIAG_BADFILE,    75, "Not a font") \
    S(MSG_DIAG_BADOTAG,    76, "Bad .otag") \
    S(MSG_DIAG_OTHER,      77, "Other engine") \
    S(MSG_DIAG_ADV_OK,     78, "The font file is where the .otag says.") \
    S(MSG_DIAG_ADV_MOVED,  79, "AAText finds it there, other programs may not: repair it on the Repair tab.") \
    S(MSG_DIAG_ADV_MISSING, 80, "The font file is not there. Install the font again, or delete its .font and .otag.") \
    S(MSG_DIAG_ADV_BADFILE, 81, "This file is not a TrueType or OpenType font.") \
    S(MSG_DIAG_ADV_BADOTAG, 82, "The .otag cannot be read or names no font file.") \
    S(MSG_DIAG_ADV_OTHER,  83, "An outline font for another font engine (%s); AAText leaves it alone.") \
    S(MSG_DIAG_SAVE_TITLE, 84, "Save font report") \
    S(MSG_DIAG_SAVED,      85, "Report saved: %s") \
    S(MSG_DIAG_LISTFULL,   86, "Only the first %ld fonts are listed.") \
    S(MSG_DIAG_NOENGINE,   95, "%s.library is not installed: no program can open this font.") \
    S(MSG_DIAG_L_OTAG,     96, ".otag file") \
    S(MSG_DIAG_L_WANT,     97, "Font file in .otag") \
    S(MSG_DIAG_L_FOUND,    98, "Found as") \
    S(MSG_DIAG_L_ENGINE,   99, "OT_Engine") \
    S(MSG_DIAG_ENGINEFMT,  100, "%s (%s.library %s)") \
    S(MSG_DIAG_ENGINENONE, 101, "%s (%s.library not installed)") \
    S(MSG_DIAG_WRITEERR,   102, "Cannot write %s.") \
    S(MSG_TAB_SETTINGS,    103, "Settings") \
    S(MSG_SET_INFO,        104, "Where installed font files are copied, and the character set AATextManager starts with.") \
    S(MSG_SET_TTFDIR,      105, "TTF directory") \
    S(MSG_SET_OTFDIR,      106, "OTF directory") \
    S(MSG_SET_TTCDIR,      107, "TTC/OTC directory") \
    S(MSG_SET_PICK,        108, "Choose...") \
    S(MSG_SET_PICKTITLE,   109, "Directory for font files") \
    S(MSG_SET_CHARSET,     110, "Default character set") \
    S(MSG_SET_SAVE,        111, "Sa_ve settings") \
    S(MSG_SET_SAVED,       112, "Settings saved (ENVARC:AATextManager.prefs).") \
    S(MSG_SET_SAVEERR,     113, "Cannot write ENV:/ENVARC:AATextManager.prefs.") \
    S(MSG_SET_NOTE,        114, "Directories in FONTS: are written as FONTS:..., others with their volume name.") \
    S(MSG_TAB_DOWNLOAD,    115, "Download") \
    S(MSG_DL_INFO,         116, "Free fonts (Google Fonts, github.com/google/fonts). Selected files are downloaded to T: and installed.") \
    S(MSG_DL_FILTER,       117, "Search") \
    S(MSG_DL_LOAD,         118, "_Load list") \
    S(MSG_DL_PREVIEW,      119, "Pre_view") \
    S(MSG_DL_INSTALL,      120, "_Download and install") \
    S(MSG_DL_COL_LIC,      121, "Licence") \
    S(MSG_DL_COL_SIZE,     122, "Size") \
    S(MSG_DL_LOADING,      123, "Loading the font list...") \
    S(MSG_DL_LOADED,       124, "%ld fonts in the list; select one or more (Shift-click).") \
    S(MSG_DL_LOADFAIL,     125, "Cannot load the font list: %s") \
    S(MSG_DL_GETTING,      126, "Downloading %ld/%ld: %s") \
    S(MSG_DL_GETFAIL,      127, "Cannot download %s: %s") \
    S(MSG_DL_NONE,         128, "Select fonts in the list first.") \
    S(MSG_DL_NONET,        129, "No network: bsdsocket.library is not available (is TCP/IP running?).") \
    S(MSG_DL_NOSSL,        130, "Downloading needs AmiSSL 5 (Aminet: util/libs/AmiSSL-5).") \
    S(MSG_PV_AA,           131, "Antialiasing") \
    S(MSG_P_WIDTH,         132, "fixed width, cells too wide") \
    S(MSG_PV_SP,           133, "Subpixel") \
    S(MSG_PV_SP_OFF,       134, "Off") \
    S(MSG_PV_SP_RGB,       135, "RGB") \
    S(MSG_PV_SP_BGR,       136, "BGR") \
    S(MSG_DEL,             137, "Delete...") \
    S(MSG_DEL_ASK,         138, "Delete the font \"%s\"?\n\n%s and its .font file and sizes are deleted.\nFont file: %s") \
    S(MSG_DEL_GADS3,       139, "Only .otag/.font|With the font file|Cancel") \
    S(MSG_DEL_GADS2,       140, "Delete|Cancel") \
    S(MSG_DEL_DONE,        141, "\"%s\" deleted; the font file is kept.") \
    S(MSG_DEL_DONEFILE,    142, "\"%s\" and its font file deleted.") \
    S(MSG_DEL_FAILED,      143, "Cannot delete %s (in use or write protected?).") \
    S(MSG_DEL_SHARED,      144, "Deleted; the font file is kept: \"%s\" uses it too.") \
    S(MSG_DIAG_L_CHARSET,  145, "Character set") \
    S(MSG_DIAG_CS_OTAG,    146, "%s (code page in the .otag)") \
    S(MSG_DIAG_CS_NONE,    147, "none in the .otag: AAText uses %s") \
    S(MSG_DIAG_CS_OWN,     148, "own table (nearest: %s, %ld characters differ)") \
    S(MSG_P_CHARSET,       149, "other character set") \
    S(MSG_CHANGE_CHARSET,  150, "Change other character sets to the chosen one") \
    S(MSG_MENU_ABOUT,      151, "About...") \
    S(MSG_MENU_ABOUTMUI,   152, "About MUI...") \
    S(MSG_MENU_SETTINGS,   153, "Settings") \
    S(MSG_MENU_MUISETTINGS, 154, "MUI...") \
    S(MSG_MENU_HELP,       155, "Help...") \
    S(MSG_ABOUT_TEXT,      156, "%s\n\nAntialiased TrueType text for AmigaOS 3.2\n(AmigaOS 3.1, 3.5 and 3.9: not tested)\n\nCopyright (C) 2026 Serkan Dursun\nMIT License\nhttps://github.com/Sdursun/AAText\n\nFreeType (C) The FreeType Project (FTL)\nReal metrics code derived from AROS (APL)") \
    S(MSG_HELP_NOFILE,     157, "The documentation is not installed:\n%s\n\nInstall it with Install-AAText (\"Documents\").")

#define S(id, n, s) id = n,
enum { AA_STRINGS AA_NUM_STRINGS };
#undef S

/* Open the catalog; language NULL = the user's preferred languages. */
void InitStrings(const char *language);
void FreeStrings(void);
/* The catalog's language ("türkçe"), NULL for the built-in English */
const char *StringsLanguage(void);
const char *GetString(LONG id);

#endif
