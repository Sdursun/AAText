#ifndef AATEXT_STARTUP_H
#define AATEXT_STARTUP_H

/*
 * "AAText STARTUP=ADD|REMOVE": the line that starts AAText at boot, in
 * S:Startup-Sequence right after IPrefs (used by the installer).
 *
 * ADD does nothing if a line that is not a comment already runs AAText
 * (e.g. "run >NIL: c:aatext"); otherwise "Run >NIL: C:AAText" goes
 * right after the IPrefs line, or before LoadWB if there is no IPrefs.
 * REMOVE deletes every line (not a comment) that runs AAText. All other
 * lines stay as they are.
 */

#include <exec/types.h>

#define AA_STARTUP_LINE  "Run >NIL: C:AAText"

enum
{
    AA_SS_ADDED = 0,        /* line added */
    AA_SS_PRESENT,          /* ADD: AAText is started already */
    AA_SS_REMOVED,          /* line(s) removed */
    AA_SS_ABSENT,           /* REMOVE: no such line */
    AA_SS_NOPLACE,          /* ADD: neither IPrefs nor LoadWB found */
    AA_SS_READ,             /* the file cannot be read (or is too big) */
    AA_SS_BACKUP,           /* the backup cannot be written */
    AA_SS_WRITE             /* the new file cannot be written */
};

/*
 * Edit the text in (len bytes) into out (room for max bytes); *outlen
 * gets the new length. Returns AA_SS_ADDED, _PRESENT, _REMOVED, _ABSENT
 * or _NOPLACE (out is only valid for _ADDED and _REMOVED). No library
 * calls, for the tests.
 */
LONG aa_StartupEdit(const char *in, LONG len, BOOL add, char *out, LONG max,
                    LONG *outlen);

/*
 * Edit the file path. Before the first change the original is copied to
 * <path>.AAText-bak (an existing backup is kept); the new text is
 * written to <path>.AAText-new and then put in place of the file, with
 * the file's protection bits. Returns AA_SS_...
 */
LONG aa_StartupFile(const char *path, BOOL add);

#endif
