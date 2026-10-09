#ifndef AATEXTPREFS_STRINGS_H
#define AATEXTPREFS_STRINGS_H

/*
 * User interface strings of AATextPrefs. English is built in; other
 * languages come from aatextprefs.catalog (locale.library). The IDs are
 * part of the catalog format: never renumber, only append. A "_" marks
 * a button's keyboard shortcut. Retired IDs, never to be reused:
 * 2 (Fonts tab), 18 (Test button), 22 (placeholder page).
 * Translations: catalogs/<language>.ct, compiled by tools/mkcatalog.pl.
 */

#include <exec/types.h>

#define AA_STRINGS \
    S(MSG_WINDOW_TITLE,     0,  "AAText Preferences") \
    S(MSG_TAB_APPEARANCE,   1,  "Appearance") \
    S(MSG_TAB_PROGRAMS,     3,  "Programs") \
    S(MSG_TAB_ADVANCED,     4,  "Advanced") \
    S(MSG_GAMMA,            5,  "Gamma") \
    S(MSG_HINTING,          6,  "Hinting") \
    S(MSG_HINT_NORMAL,      7,  "Normal (font's own hints)") \
    S(MSG_HINT_NONE,        8,  "None (smoothest)") \
    S(MSG_HINT_LIGHT,       9,  "Light (soft but clean)") \
    S(MSG_HINT_FULL,        10, "Full (crispest)") \
    S(MSG_REAL,             11, "Real character widths") \
    S(MSG_REAL_NOTE,        12, "Changes layouts; takes effect after AAText is restarted.") \
    S(MSG_PREVIEW,          13, "Preview") \
    S(MSG_PREVIEW_LINE1,    14, "The quick brown fox jumps over the lazy dog") \
    S(MSG_PREVIEW_LINE2,    15, "0123456789 (illustrate) WAVE mmm") \
    S(MSG_SAVE,             16, "_Save") \
    S(MSG_USE,              17, "_Use") \
    S(MSG_CANCEL,           19, "_Cancel") \
    S(MSG_STATUS_RUNNING,   20, "%s is running.") \
    S(MSG_STATUS_STOPPED,   21, "AAText is not running: settings are only saved.") \
    S(MSG_RESTART_TITLE,    23, "AAText Preferences") \
    S(MSG_RESTART_TEXT,     24, "Real character widths and kerning change when\nAAText is restarted (e.g. at the next reboot).") \
    S(MSG_OK,               25, "OK") \
    S(MSG_WRITE_ERROR,      26, "Cannot write %s.") \
    S(MSG_STATUS_TESTED,    27, "Changes are shown live - Save or Use keeps them.") \
    S(MSG_STATUS_OLD,       28, "The running AAText is too old for live changes.") \
    S(MSG_PREVIEW_FONT,     29, "Screen font: %s %ld") \
    S(MSG_PREVIEW_PLAIN,    30, "Screen font: %s %ld (AAText off: plain text)") \
    S(MSG_PROGRAMS_INFO,    31, "AAText leaves the text of these programs alone:") \
    S(MSG_PROGRAM,          32, "Program") \
    S(MSG_ADD,              33, "Add") \
    S(MSG_REMOVE,           34, "Remove") \
    S(MSG_RUNNING,          35, "Add a running program...") \
    S(MSG_LIST_FULL,        36, "The list is full (%ld programs).") \
    S(MSG_AUTODETECT,       37, "Find TrueType fonts automatically (.otag)") \
    S(MSG_CACHE,            38, "Glyph cache (KB)") \
    S(MSG_CACHE_USED,       39, "In use: %ld KB, %ld characters") \
    S(MSG_OFFSCREEN,        40, "Workbench colours for off-screen bitmaps") \
    S(MSG_CHARSET,          41, "Character set") \
    S(MSG_CHARSET_LATIN1,   42, "ISO-8859-1 (Western European)") \
    S(MSG_CHARSET_LATIN5,   43, "ISO-8859-9 (Turkish)") \
    S(MSG_CHARSET_NOTE,     44, "For fonts without a code page in .otag. Save and Use set ENV:ftcodepage too.") \
    S(MSG_FTCODEPAGE_ASK,   46, "ENV:ftcodepage holds a code page that is not in the list.\nReplace it with %s?\n(freetype2.library uses it too.)") \
    S(MSG_REPLACE_KEEP,     47, "Replace|Keep") \
    S(MSG_FTCODEPAGE_FAIL,  49, "Cannot write ENV:ftcodepage.") \
    S(MSG_PICK,             50, "Choose...") \
    S(MSG_PICK_TITLE,       51, "Program AAText should leave alone") \
    S(MSG_MENU_PROJECT,     52, "Project") \
    S(MSG_MENU_OPEN,        53, "Open...") \
    S(MSG_MENU_SAVEAS,      54, "Save As...") \
    S(MSG_MENU_QUIT,        55, "Quit") \
    S(MSG_MENU_EDIT,        56, "Edit") \
    S(MSG_MENU_DEFAULTS,    57, "Reset To Defaults") \
    S(MSG_MENU_LASTSAVED,   58, "Last Saved") \
    S(MSG_MENU_RESTORE,     59, "Restore") \
    S(MSG_OPEN_TITLE,       60, "Open AAText preferences") \
    S(MSG_SAVEAS_TITLE,     61, "Save AAText preferences as") \
    S(MSG_READ_ERROR,       62, "Cannot read %s.") \
    S(MSG_KERNING,          63, "Kerning (with real widths)") \
    S(MSG_CHARSET_LATIN2,   64, "ISO-8859-2 (Central European)") \
    S(MSG_CHARSET_LATIN3,   65, "ISO-8859-3 (South European)") \
    S(MSG_CHARSET_LATIN4,   66, "ISO-8859-4 (North European)") \
    S(MSG_CHARSET_LATIN9,   67, "ISO-8859-15 (Western European, euro)") \
    S(MSG_CHARSET_LATIN10,  68, "ISO-8859-16 (South-Eastern European)") \
    S(MSG_CHARSET_CP1250,   69, "Windows-1250 (Central European)") \
    S(MSG_CHARSET_CYRILLIC, 70, "ISO-8859-5 (Cyrillic)") \
    S(MSG_CHARSET_KOI8R,    71, "KOI8-R (Russian)") \
    S(MSG_TAB_DIAG,         72, "Diagnostics") \
    S(MSG_DIAG_INFO,        73, "Checks the .otag files in FONTS: and the font files they name. Scanning changes nothing.") \
    S(MSG_DIAG_SCAN,        74, "Scan fonts") \
    S(MSG_DIAG_REPORT,      75, "Save report...") \
    S(MSG_DIAG_SUMMARY,     76, "%ld fonts: %ld OK, %ld with problems") \
    S(MSG_DIAG_NOFONTS,     77, "Cannot read FONTS:.") \
    S(MSG_DIAG_COL_FONT,    78, "Font") \
    S(MSG_DIAG_COL_STATUS,  79, "Status") \
    S(MSG_DIAG_COL_FILE,    80, "Font file") \
    S(MSG_DIAG_OK,          81, "OK") \
    S(MSG_DIAG_MOVED,       82, "Moved") \
    S(MSG_DIAG_MISSING,     83, "Missing") \
    S(MSG_DIAG_BADFILE,     84, "Not a font") \
    S(MSG_DIAG_BADOTAG,     85, "Bad .otag") \
    S(MSG_DIAG_OTHER,       86, "Other engine") \
    S(MSG_DIAG_ADV_OK,      88, "The font file is where the .otag says.") \
    S(MSG_DIAG_ADV_MOVED,   89, "AAText finds it there, other programs may not: use Fix .otag.") \
    S(MSG_DIAG_ADV_MISSING, 90, "The font file is not there. Install the font again, or delete its .font and .otag.") \
    S(MSG_DIAG_ADV_BADFILE, 91, "This file is not a TrueType or OpenType font.") \
    S(MSG_DIAG_ADV_BADOTAG, 92, "The .otag cannot be read or names no font file.") \
    S(MSG_DIAG_ADV_OTHER,   93, "An outline font for another font engine (%s); AAText leaves it alone.") \
    S(MSG_DIAG_SAVE_TITLE,  94, "Save font report") \
    S(MSG_DIAG_SAVED,       95, "Report saved: %s") \
    S(MSG_DIAG_LISTFULL,    96, "Only the first %ld fonts are listed.") \
    S(MSG_DIAG_FIX,         98, "Fix .otag...") \
    S(MSG_DIAG_FIX_ASK,     99, "Change %s?\n\nFont file now: %s\nbecomes: %s\n\nThe original is kept as %s.") \
    S(MSG_DIAG_FIX_GADS,   100, "Change|Cancel") \
    S(MSG_DIAG_FIXED,      101, "Fixed. The original is in %s.") \
    S(MSG_DIAG_FIX_NOTMOVED, 102, "Only fonts marked Moved can be fixed.") \
    S(MSG_DIAG_FIX_BACKUP, 103, "Cannot write the backup %s; nothing was changed.") \
    S(MSG_DIAG_FIX_READ,   104, "Cannot read or rebuild %s; nothing was changed.") \
    S(MSG_DIAG_FIX_WRITE,  105, "Cannot write %s. The original is in %s.") \
    S(MSG_DIAG_LIB,        106, "; %s %s") \
    S(MSG_DIAG_LIB_NONE,   107, "; no %s") \
    S(MSG_DIAG_NOENGINE,   109, "%s.library is not installed: no program can open this font.") \
    S(MSG_DIAG_L_OTAG,     110, ".otag file") \
    S(MSG_DIAG_L_WANT,     111, "Font file in .otag") \
    S(MSG_DIAG_L_FOUND,    112, "Found as") \
    S(MSG_DIAG_L_ENGINE,   113, "OT_Engine") \
    S(MSG_DIAG_ENGINEFMT,  114, "%s (%s.library %s)") \
    S(MSG_DIAG_ENGINENONE, 115, "%s (%s.library not installed)")


#define S(id, n, s) id = n,
enum { AA_STRINGS AA_NUM_STRINGS };
#undef S

/* Open the catalog; language NULL = the user's preferred languages. */
void InitStrings(const char *language);
void FreeStrings(void);
const char *GetString(LONG id);

#endif
