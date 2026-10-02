#ifndef AATEXT_PREFS_H
#define AATEXT_PREFS_H

#include <exec/types.h>

#define AA_MAX_MAPPINGS   16
#define AA_NAME_LEN       32
#define AA_PATH_LEN       256

enum
{
    AA_CHARSET_LATIN1 = 0,      /* ISO-8859-1, AmigaOS default */
    AA_CHARSET_LATIN5           /* ISO-8859-9, Turkish */
};

/* One "bitmapfont size -> ttf [pixelsize]" line. */
struct AAMapping
{
    char  fontname[AA_NAME_LEN];    /* without ".font" */
    UWORD ysize;
    char  ttfpath[AA_PATH_LEN];
    UWORD pixelsize;                /* 0 = automatic */
};

struct AAPrefs
{
    struct AAMapping map[AA_MAX_MAPPINGS];
    UWORD nummaps;
    UWORD gamma100;                 /* gamma * 100, e.g. 180 */
    UBYTE charset;
};

/*
 * Read the preferences file. If path is NULL, ENV:AAText.prefs is tried
 * first, then ENVARC:AAText.prefs. Errors are reported with line numbers
 * to the Shell when report is TRUE. Returns FALSE if no file was found.
 */
BOOL aa_ReadPrefs(struct AAPrefs *prefs, const char *path, BOOL report);

#endif
