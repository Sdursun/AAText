/*
 * AAText - finding the font file of a .otag. See fontfile.h.
 * Shared by AAText and AATextPrefs (Diagnostics).
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "fontfile.h"

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Lock prefix + rest, built in path; 0 if it does not exist. */
static BPTR LockJoined(const char *prefix, const char *rest, char *path)
{
    LONG n = 0;

    while (prefix[n] && n < AA_FONTFILE_LEN - 1)
    {
        path[n] = prefix[n];
        n++;
    }
    while (*rest && n < AA_FONTFILE_LEN - 1)
        path[n++] = *rest++;
    path[n] = 0;
    return Lock((CONST_STRPTR)path, ACCESS_READ);
}

BPTR aa_LockFontFile(const char *file, char *path)
{
    const char *rest = file, *p;
    BPTR lock;

    if ((lock = LockJoined("", file, path)))
        return lock;

    for (p = file; *p; p++)
        if (*p == ':')
            rest = p + 1;
    if (rest == file)
        return LockJoined("FONTS:", file, path);

    for (p = rest; *p; p++)
        if ((p == rest || p[-1] == '/') && ToLower(p[0]) == 'f' &&
            ToLower(p[1]) == 'o' && ToLower(p[2]) == 'n' &&
            ToLower(p[3]) == 't' && ToLower(p[4]) == 's' && p[5] == '/')
        {
            if ((lock = LockJoined("FONTS:", p + 6, path)))
                return lock;
            break;
        }
    return LockJoined("SYS:", rest, path);
}
