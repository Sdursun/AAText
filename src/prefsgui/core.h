#ifndef AATEXTPREFS_CORE_H
#define AATEXTPREFS_CORE_H

/*
 * AATextPrefs without its window: the settings being edited, talking to
 * the running AAText, the prefs and code page files, the program and
 * mapping lists. Shared by the ReAction and the MUI GUI; nothing here
 * opens a window or shows a text, the GUI turns the results into
 * messages.
 */

#include <exec/types.h>

#include "../prefs.h"

#define PC_MAX_RUNNING 64

struct PrefsCore
{
    struct AAPrefs cur;         /* what the window shows */
    struct AAPrefs before;      /* AAText's settings when we started */
    struct AAPrefs orig;        /* the settings read at start (Restore) */
    BOOL running;               /* AAText answered STATUS */
    BOOL tested;                /* new settings were APPLYed to AAText */
    char version[40];           /* "AAText 0.40 (21.10.2026)" */
    /* programs running now, names AAText's blacklist can match, sorted */
    char runnames[PC_MAX_RUNNING][AA_NAME_LEN];
    LONG numrunning;
};

extern struct PrefsCore pc;

/* Read the settings (defaults without a file); before and orig too. */
void pc_Init(void);

/* Ask AAText for its state: running, before and version are set. */
enum { PC_RUNNING, PC_STOPPED, PC_OLD };
LONG pc_Query(void);

/*
 * APPLY or RELOAD to AAText (nothing if it is not running). PC_RESTART:
 * applied, some changes need a restart; PC_GONE: AAText went away.
 */
enum { PC_SENT, PC_RESTART, PC_GONE };
LONG pc_Send(UWORD cmd, const struct AAPrefs *p);

/*
 * cur to the running AAText for a live preview, with the settings that
 * need a restart (real widths, kerning) kept as AAText has them. FALSE
 * if AAText is not running (any more).
 */
BOOL pc_LiveApply(void);

/* Glyph cache and font file use of the running AAText; FALSE if none. */
BOOL pc_Usage(ULONG *cachebytes, ULONG *glyphs, ULONG *facebytes,
              LONG *files);

/* cur to path, keeping the other lines of template_path. */
BOOL pc_Write(const char *path, const char *template_path);

/* Blacklist: add (blanks trimmed, no doubles) and remove. */
enum { PC_ADDED, PC_EMPTY, PC_DOUBLE, PC_FULL };
LONG pc_AddProgram(const char *name);
void pc_RemoveProgram(LONG i);

/* Fill runnames/numrunning with the programs running now. */
void pc_FillRunning(void);

/*
 * ENV:ftcodepage to match cur.charset (ENVARC: too with archive).
 * pc_CodePageForeign(): ENV holds a table of no set we know, which is
 * replaced only if the user agrees (the GUI asks before writing).
 */
enum { PC_CP_OK, PC_CP_ENVFAIL, PC_CP_ARCFAIL };
BOOL pc_CodePageForeign(void);
LONG pc_WriteCodePage(BOOL archive);

/* Font mappings: add (index, or -1 if the list is full), replace,
   remove, pixel size (TRUE if it changed). */
LONG pc_AddMapping(const struct AAMapping *m);
void pc_SetMapping(LONG i, const struct AAMapping *m);
void pc_RemoveMapping(LONG i);
BOOL pc_SetMapPixel(LONG i, UWORD px);

#endif
