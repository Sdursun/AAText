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

enum
{
    AA_FIX_OK = 0,
    AA_FIX_NOTMOVED,        /* only AA_DIAG_MOVED entries are fixed */
    AA_FIX_READ,            /* the .otag cannot be read or rebuilt */
    AA_FIX_BACKUP,          /* the backup cannot be written */
    AA_FIX_WRITE            /* the .otag cannot be written; backup kept */
};

/*
 * Point a moved font's .otag at the file where it was found (e->found,
 * e.g. FONTS:_ttf/x.ttf). The original is first copied to <otag>.bak,
 * unless that exists already (the oldest original is kept); its name is
 * left in backup (AA_FONTFILE_LEN bytes). e is checked again afterwards.
 * Returns AA_FIX_...
 */
LONG aa_FixOTag(struct AADiagEntry *e, char *backup);

#endif
