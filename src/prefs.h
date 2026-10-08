#ifndef AATEXT_PREFS_H
#define AATEXT_PREFS_H

#include <exec/types.h>

#define AA_MAX_MAPPINGS   16
#define AA_MAX_BLACKLIST  32
#define AA_NAME_LEN       48
#define AA_PATH_LEN       256

#define AA_DEFAULT_CACHE_KB  256

enum
{
    /* same order as aa_CharsetNames in charsets.c */
    AA_CHARSET_LATIN1 = 0,      /* ISO-8859-1, AmigaOS default */
    AA_CHARSET_LATIN2,
    AA_CHARSET_LATIN3,
    AA_CHARSET_LATIN4,
    AA_CHARSET_LATIN5,          /* ISO-8859-9, Turkish */
    AA_CHARSET_LATIN9,
    AA_CHARSET_LATIN10,
    AA_CHARSET_CP1250,
    AA_CHARSET_CYRILLIC,        /* ISO-8859-5 */
    AA_CHARSET_KOI8R
};

enum
{
    AA_HINT_NORMAL = 0,         /* font's own TrueType hints, v40 (default) */
    AA_HINT_NONE,               /* no hinting: smoothest, true to design */
    AA_HINT_LIGHT,              /* FreeType autohinter, vertical only */
    AA_HINT_FULL                /* classic TrueType hinting, v35: crisp */
};

/* One "bitmapfont size -> ttf [pixelsize]" line. */
struct AAMapping
{
    char  fontname[AA_NAME_LEN];    /* without ".font" */
    UWORD ysize;
    char  ttfpath[AA_PATH_LEN];
    UWORD pixelsize;                /* 0 = automatic */
    BOOL  real;                     /* real TrueType metrics */
};

struct AAPrefs
{
    struct AAMapping map[AA_MAX_MAPPINGS];
    UWORD nummaps;
    UWORD gamma100;                 /* gamma * 100, default 80 */
    UBYTE charset;
    UBYTE hinting;                  /* AA_HINT_... */
    BOOL  offscreen;                /* use Workbench colours for bitmaps
                                       that belong to no screen */
    BOOL  autodetect;               /* find TTFs of outline fonts via
                                       their .otag files (default on) */
    BOOL  autoreal;                 /* real metrics for detected fonts */
    BOOL  kerning;                  /* pair kerning in real metrics mode */
    ULONG cachekb;                  /* glyph cache size in KB */
    char  blacklist[AA_MAX_BLACKLIST][AA_NAME_LEN];
    UWORD numblack;
};

/*
 * Read the preferences file. If path is NULL, ENV:AAText.prefs is tried
 * first, then ENVARC:AAText.prefs. Errors are reported with line numbers
 * to the Shell when report is TRUE. Returns FALSE if no file was found.
 */
BOOL aa_ReadPrefs(struct AAPrefs *prefs, const char *path, BOOL report);

/* The settings AAText uses without a preferences file. */
void aa_DefaultPrefs(struct AAPrefs *prefs);

#endif
