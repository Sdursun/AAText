#ifndef AATEXT_FONTSCAN_H
#define AATEXT_FONTSCAN_H

/*
 * Font diagnostics for AATextPrefs: checks every .otag file in FONTS:
 * (all directories of the assign) and the font file it names, the way
 * AAText's automatic detection finds them. Only reads; changes nothing.
 */

#include <exec/types.h>

#include "prefs.h"
#include "fontfile.h"

enum
{
    AA_DIAG_OK = 0,         /* font file where the .otag says */
    AA_DIAG_MOVED,          /* found only in FONTS:/SYS: (volume renamed) */
    AA_DIAG_MISSING,        /* font file not found anywhere */
    AA_DIAG_BADFILE,        /* exists, but not a TrueType/OpenType file */
    AA_DIAG_BADOTAG,        /* .otag unreadable or names no font file */
    AA_DIAG_OTHER,          /* outline font of another engine (bullet...) */
    AA_DIAG_NUM
};

struct AADiagEntry
{
    char  name[AA_NAME_LEN];            /* font name, without .otag */
    char  otag[AA_FONTFILE_LEN];        /* the .otag file */
    char  want[AA_FONTFILE_LEN];        /* font file named in the .otag */
    char  found[AA_FONTFILE_LEN];       /* where it is (MOVED, OK) */
    char  engine[16];
    LONG  facenum;
    UBYTE status;                       /* AA_DIAG_... */
    UBYTE codepage;                     /* the .otag has a code page */
};

/*
 * Scan FONTS: into out (room for max entries), fonts with problems
 * first, then by name. A font name found in more than one directory of
 * the assign is listed once, as diskfont.library uses the first. Returns the number of entries,
 * or -1 if FONTS: cannot be read.
 */
LONG aa_ScanFonts(struct AADiagEntry *out, LONG max);

#endif
