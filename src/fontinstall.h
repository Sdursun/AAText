#ifndef AATEXT_FONTINSTALL_H
#define AATEXT_FONTINSTALL_H

/*
 * AATextManager: install an outline font for diskfont, as FTManager
 * does. The font file is copied to <fonts>_ttf/, and <name>.font (4 bytes,
 * no bitmap sizes) and <name>.otag are written to <fonts>, the .otag
 * naming the font file as "<fonts>_ttf/<file>" (no volume name) and
 * carrying the code page of the chosen character set.
 */

#include <exec/types.h>

#include "fontinfo.h"

#define AA_INSTALL_MAXSIZES  32

enum
{
    AA_INSTALL_OK = 0,
    AA_INSTALL_NOTFONT,     /* FreeType cannot read the file */
    AA_INSTALL_EXISTS,      /* name taken and overwrite not set */
    AA_INSTALL_COPYFAIL,    /* font file could not be copied */
    AA_INSTALL_WRITEFAIL    /* .font or .otag could not be written */
};

struct AAInstall
{
    /* in */
    const char *source;     /* font file */
    LONG  face;             /* face in a .ttc */
    const char *fonts;      /* "FONTS:" or a directory ending in ':'/'/' */
    const char *engine;     /* "aatext" or "freetype2" */
    LONG  charset;          /* AA_CHARSET_*, or -1 for no code page */
    UWORD sizes[AA_INSTALL_MAXSIZES];
    LONG  numsizes;
    BOOL  overwrite;        /* replace a font of the same name (backups) */

    /* out */
    struct AAFontInfo info;
    char  name[32];         /* "verdanaregular" */
    char  fontfile[256];    /* as written into the .otag */
    BOOL  replaced;         /* old .font/.otag kept as .bak */
    BOOL  copied;           /* FALSE: the font file was already there */
};

/* Default sizes for the font requester: 8-16, 18, 20, 24. */
void aa_InstallDefaults(struct AAInstall *in);

/* Install; returns AA_INSTALL_*. */
LONG aa_InstallFont(struct AAInstall *in);

/*
 * Build the .otag for an installed font (used by aa_InstallFont, and by
 * the tests to compare with FTManager's output).
 */
struct AAOTagFile;
BOOL aa_BuildOTag(const struct AAInstall *in, struct AAOTagFile *f);

#endif
