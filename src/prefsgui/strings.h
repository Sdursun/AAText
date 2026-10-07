#ifndef AATEXTPREFS_STRINGS_H
#define AATEXTPREFS_STRINGS_H

/*
 * User interface strings of AATextPrefs. English is built in; other
 * languages come from aatextprefs.catalog (locale.library). The IDs are
 * part of the catalog format: never renumber, only append. A "_" marks
 * a button's keyboard shortcut. MSG_TEST and MSG_TAB_FONTS are no longer
 * used (font mappings are edited in the prefs file only).
 * Translations: catalogs/<language>.ct, compiled by tools/mkcatalog.pl.
 */

#include <exec/types.h>

#define AA_STRINGS \
    S(MSG_WINDOW_TITLE,     0,  "AAText Preferences") \
    S(MSG_TAB_APPEARANCE,   1,  "Appearance") \
    S(MSG_TAB_FONTS,        2,  "Fonts") \
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
    S(MSG_TEST,             18, "Test") \
    S(MSG_CANCEL,           19, "_Cancel") \
    S(MSG_STATUS_RUNNING,   20, "%s is running.") \
    S(MSG_STATUS_STOPPED,   21, "AAText is not running: settings are only saved.") \
    S(MSG_NOT_YET,          22, "This page comes in a later version.") \
    S(MSG_RESTART_TITLE,    23, "AAText Preferences") \
    S(MSG_RESTART_TEXT,     24, "Real character widths change when AAText\nis restarted (e.g. at the next reboot).") \
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
    S(MSG_CHARSET_NOTE,     44, "For fonts without a code page in .otag or ENV:ftcodepage.") \
    S(MSG_FTCODEPAGE,       45, "Write Turkish ENV:ftcodepage") \
    S(MSG_FTCODEPAGE_ASK,   46, "ENV:ftcodepage exists already.\nReplace it with the Turkish code page?\n(freetype2.library uses it too.)") \
    S(MSG_REPLACE_CANCEL,   47, "Replace|Cancel") \
    S(MSG_FTCODEPAGE_DONE,  48, "ENV:ftcodepage written (ENVARC: too).") \
    S(MSG_FTCODEPAGE_FAIL,  49, "Cannot write ENV:ftcodepage.")

#define S(id, n, s) id = n,
enum { AA_STRINGS AA_NUM_STRINGS };
#undef S

/* Open the catalog; language NULL = the user's preferred languages. */
void InitStrings(const char *language);
void FreeStrings(void);
const char *GetString(LONG id);

#endif
