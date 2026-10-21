#ifndef AATEXTMANAGER_CORE_H
#define AATEXTMANAGER_CORE_H

/*
 * AATextManager without its window: the fonts to install, the repair
 * and diagnostics scans, deleting fonts, the download catalog. Shared by
 * the ReAction and the MUI GUI; nothing here opens a window or shows a
 * text, the GUI turns the results into messages.
 */

#include <exec/types.h>

#include "settings.h"
#include "update.h"
#include "../fontscan.h"
#include "../fontinfo.h"
#include "../fontinstall.h"

#define MC_MAX_FILES  64
#define MC_MAX_FONTS  1024      /* repair scan */
#define MC_MAX_DIAG   300       /* diagnostics scan */
#define MC_MAX_CAT    1500

/* a font file (one face of it) to install */
struct FileEntry
{
    char  path[256];
    LONG  face;
    char  facetext[8];
    struct AAFontInfo info;
};

/* a font of the download catalog */
struct CatEntry
{
    char family[40];
    char style[28];
    char licence[8];
    char size[12];          /* "331 KB" */
    LONG bytes;
    char path[120];         /* relative to catbase, or an https:// url */
};

struct ManagerCore
{
    struct FileEntry *files;        /* Install tab */
    LONG numfiles;
    struct AADiagEntry *diag;       /* Repair tab's scan */
    LONG numdiag;
    ULONG *need;                    /* its repairs (AA_REPAIR_*) per font */
    struct AADiagEntry *dscan;      /* Diagnostics tab's scan */
    LONG numdscan;                  /* -1: not scanned yet */
    struct CatEntry *cat;           /* Download tab */
    LONG numcat;
    LONG *shown;                    /* list row -> cat index */
    LONG numshown;
    char catbase[128];
    struct MgrSettings settings;
};

extern struct ManagerCore mcore;

/* Allocate the tables and read the settings (libraries open); FALSE
   without memory. */
BOOL mc_Init(void);
void mc_Cleanup(void);

/* A running AAText looks for new fonts (RELOAD); no answer is fine. */
void mc_TellAAText(void);

/* "Verdana Bold" from the .otag, or its file name ("verdanabold") */
const char *mc_FontLabel(const struct AADiagEntry *e);

/* ---- Install ---- */

/* Add a file, one entry per face (twice: nothing new); FALSE if
   FreeType cannot read it. */
BOOL mc_AddFile(const char *path);
void mc_RemoveFile(LONG i);

struct MCResult
{
    LONG ok, skipped, failed;
    char notopen[32];       /* installed, but diskfont cannot open it */
};

/* Install the font file path (face) with in's settings; counts into r.
   in->fontdir is chosen from the settings by the file's type. */
void mc_InstallOne(struct AAInstall *in, const char *path, LONG face,
                   struct MCResult *r);

/* Install every listed file; the list is emptied if all went in.
   AAText is told about new fonts. */
void mc_InstallAll(struct AAInstall *in, struct MCResult *r);

/* ---- Repair ---- */

/* Scan FONTS: and note the repairs r would make; the number of fonts
   to repair, -1 if FONTS: cannot be read. Drops the Diagnostics scan. */
LONG mc_Check(const struct AARepair *r);

/* Repair what mc_Check() found (with r); the number repaired. */
LONG mc_RepairAll(const struct AARepair *r);

/* ---- Diagnostics ---- */

/* Scan FONTS: into dscan; numdscan, -1 if FONTS: cannot be read. */
LONG mc_Scan(void);

/* e's font file, if it has one ("" if not) */
const char *mc_FontFile(const struct AADiagEntry *e);

/* e's font file if it exists, else "" */
const char *mc_DeletableFile(const struct AADiagEntry *e);

/* another scanned font that uses file (a .ttc face, the same file
   installed twice), or NULL */
const struct AADiagEntry *mc_SharedBy(const struct AADiagEntry *e,
                                      const char *file);

/*
 * Delete dscan entry i: its .otag, .font, their backups and size
 * directory, with withfile its font file too. FALSE with the path that
 * stayed in failed. Scan again afterwards.
 */
BOOL mc_DeleteFont(LONG i, BOOL withfile, char *failed, LONG len);

/* The scan as a plain English text file, for bug reports. */
BOOL mc_WriteReport(const char *path);

/* ---- Download ---- */

/*
 * Load the catalog (settings.catalog, a file or https://). 0 when done;
 * else UPD_NONET, UPD_NOSSL, or UPD_FAILED with the reason in error.
 */
LONG mc_LoadCatalog(char *error, LONG len);

/* shown/numshown: the entries whose family contains filter (any case) */
void mc_Filter(const char *filter);

/* T:AATextFonts/<file name> of an entry; TRUE if it is there complete */
BOOL mc_LocalCopy(const struct CatEntry *e, char *path, LONG len);

/*
 * Make sure e is in T:AATextFonts (downloads it unless it is there
 * complete); FALSE with the reason in net->error.
 */
BOOL mc_Fetch(struct AANet *net, const struct CatEntry *e, char *path,
              LONG len);

#endif
