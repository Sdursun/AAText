#ifndef AATEXTMANAGER_SETTINGS_H
#define AATEXTMANAGER_SETTINGS_H

/*
 * AATextManager's own settings: where installed font files go (one
 * directory for each kind of font file) and the character set the
 * window starts with. Kept in ENV:/ENVARC:AATextManager.prefs, plain
 * text:
 *
 *   ttfdir  FONTS:_ttf/
 *   otfdir  FONTS:_otf/
 *   ttcdir  FONTS:_ttc/
 *   charset latin1
 *   catalog https://...           (not in the window; for tests a file)
 */

#include <exec/types.h>

#define MGR_PREFS_ENV     "ENV:AATextManager.prefs"
#define MGR_PREFS_ENVARC  "ENVARC:AATextManager.prefs"
#define MGR_DIR_DEFAULT   "FONTS:_ttf/"     /* .ttf */
#define MGR_DIR_OTF_DEFAULT "FONTS:_otf/"   /* .otf */
#define MGR_DIR_TTC_DEFAULT "FONTS:_ttc/"   /* .ttc, .otc */
#define MGR_DIR_LEN       256
#define MGR_CATALOG_URL       "https://raw.githubusercontent.com/Sdursun/AAText/main/fonts/catalog.txt"

enum { MGR_DIR_TTF, MGR_DIR_OTF, MGR_DIR_TTC, MGR_NUM_DIRS };

struct MgrSettings
{
    char dir[MGR_NUM_DIRS][MGR_DIR_LEN];
    LONG charset;                   /* AA_CHARSET_* */
    char catalog[MGR_DIR_LEN];      /* the Download tab's font list */
};

/* Defaults: FONTS:_ttf/, _otf/ and _ttc/, ISO-8859-1. */
void mgr_DefaultSettings(struct MgrSettings *s);

/* ENV: (then ENVARC:) over the defaults; FALSE if neither was there. */
BOOL mgr_LoadSettings(struct MgrSettings *s);

/* To ENV: and ENVARC:; FALSE if one could not be written. */
BOOL mgr_SaveSettings(const struct MgrSettings *s);

/* MGR_DIR_* for a font file name: .otf, .ttc/.otc, otherwise .ttf. */
LONG mgr_DirKind(const char *file);

/*
 * A directory inside FONTS: as "FONTS:<rest>" (so the .otag still works
 * after the boot volume is renamed); others are left as they are.
 */
void mgr_FontsRelative(char *path, LONG len);

#endif
