/*
 * AAText - the AAText line in S:Startup-Sequence. See startup.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "startup.h"

#define MAX_SCRIPT  (64 * 1024)

static int Lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static BOOL Blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/*
 * Does the line (len chars, without '\n') run the command name (lower
 * case)? "name", "C:name", "Run >NIL: name args", "run <>NIL: dir/name".
 * Comment lines (';' first) never do.
 */
static BOOL RunsCommand(const char *l, LONG len, const char *name)
{
    LONG i = 0, start, end, j, n = strlen(name);
    BOOL afterrun = FALSE;

    for (;;)
    {
        while (i < len && Blank(l[i]))
            i++;
        if (i >= len || l[i] == ';')
            return FALSE;
        start = i;
        while (i < len && !Blank(l[i]) && l[i] != ';')
            i++;
        end = i;

        /* "Run" and its redirections come before the command */
        if (!afterrun && end - start == 3 && Lower(l[start]) == 'r' &&
            Lower(l[start + 1]) == 'u' && Lower(l[start + 2]) == 'n')
        {
            afterrun = TRUE;
            continue;
        }
        if (l[start] == '>' || l[start] == '<')
            continue;
        break;
    }

    /* the file part of the command */
    for (j = end - 1; j >= start; j--)
        if (l[j] == ':' || l[j] == '/')
            break;
    start = j + 1;
    if (end - start != n)
        return FALSE;
    for (j = 0; j < n; j++)
        if (Lower(l[start + j]) != name[j])
            return FALSE;
    return TRUE;
}

/* Append len bytes to out; FALSE if it does not fit. */
static BOOL Put(char *out, LONG max, LONG *pos, const char *s, LONG len)
{
    if (*pos + len > max)
        return FALSE;
    memcpy(out + *pos, s, len);
    *pos += len;
    return TRUE;
}

LONG aa_StartupEdit(const char *in, LONG len, BOOL add, char *out, LONG max,
                    LONG *outlen)
{
    static const char line[] = AA_STARTUP_LINE "\n";
    LONG i, pos = 0, iprefs = -1, loadwb = -1, found = 0;

    /* find the lines (start offsets) first */
    for (i = 0; i < len; )
    {
        LONG e = i;

        while (e < len && in[e] != '\n')
            e++;
        if (RunsCommand(in + i, e - i, "aatext"))
            found++;
        else if (iprefs < 0 && RunsCommand(in + i, e - i, "iprefs"))
            iprefs = e < len ? e + 1 : len;     /* after this line */
        else if (loadwb < 0 && RunsCommand(in + i, e - i, "loadwb"))
            loadwb = i;                         /* before this line */
        i = e + 1;
    }

    *outlen = 0;
    if (add)
    {
        LONG at;

        if (found)
            return AA_SS_PRESENT;
        at = iprefs >= 0 ? iprefs : loadwb;
        if (at < 0)
            return AA_SS_NOPLACE;
        if (!Put(out, max, &pos, in, at))
            return AA_SS_READ;
        /* IPrefs as the last line without a line feed */
        if (at > 0 && in[at - 1] != '\n' && !Put(out, max, &pos, "\n", 1))
            return AA_SS_READ;
        if (!Put(out, max, &pos, line, sizeof(line) - 1) ||
            !Put(out, max, &pos, in + at, len - at))
            return AA_SS_READ;
        *outlen = pos;
        return AA_SS_ADDED;
    }

    if (!found)
        return AA_SS_ABSENT;
    for (i = 0; i < len; )
    {
        LONG e = i;

        while (e < len && in[e] != '\n')
            e++;
        if (!RunsCommand(in + i, e - i, "aatext") &&
            !Put(out, max, &pos, in + i, (e < len ? e + 1 : len) - i))
            return AA_SS_READ;
        i = e + 1;
    }
    *outlen = pos;
    return AA_SS_REMOVED;
}

/* Write len bytes to a new file path. */
static BOOL WriteAll(const char *path, const char *buf, LONG len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    BOOL ok;

    if (!fh)
        return FALSE;
    ok = Write(fh, (APTR)buf, len) == len;
    if (!Close(fh))
        ok = FALSE;
    if (!ok)
        DeleteFile((CONST_STRPTR)path);
    return ok;
}

LONG aa_StartupFile(const char *path, BOOL add)
{
    static char bak[256], tmp[256];
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    char *in = AllocVec(MAX_SCRIPT, MEMF_ANY);
    char *out = AllocVec(MAX_SCRIPT + 64, MEMF_ANY);
    LONG len = -1, outlen, r = AA_SS_READ;
    ULONG prot = 0;
    BPTR fh, l;

    if (!fib || !in || !out || strlen(path) > sizeof(bak) - 12)
        goto out;
    if ((l = Lock((CONST_STRPTR)path, ACCESS_READ)))
    {
        if (Examine(l, fib))
            prot = fib->fib_Protection;
        UnLock(l);
    }
    if ((fh = Open((CONST_STRPTR)path, MODE_OLDFILE)))
    {
        len = Read(fh, in, MAX_SCRIPT);
        Close(fh);
    }
    if (len < 0 || len == MAX_SCRIPT)
        goto out;

    r = aa_StartupEdit(in, len, add, out, MAX_SCRIPT + 64, &outlen);
    if (r != AA_SS_ADDED && r != AA_SS_REMOVED)
        goto out;

    /* the oldest original is kept */
    strcpy(bak, path);
    strcat(bak, ".AAText-bak");
    if ((l = Lock((CONST_STRPTR)bak, ACCESS_READ)))
        UnLock(l);
    else if (!WriteAll(bak, in, len))
    {
        r = AA_SS_BACKUP;
        goto out;
    }

    /* never leave a half written script: new file first, then swap */
    strcpy(tmp, path);
    strcat(tmp, ".AAText-new");
    if (!WriteAll(tmp, out, outlen))
    {
        r = AA_SS_WRITE;
        goto out;
    }
    if (!DeleteFile((CONST_STRPTR)path) ||
        !Rename((CONST_STRPTR)tmp, (CONST_STRPTR)path))
    {
        /* put the original back if it is gone */
        if (!(l = Lock((CONST_STRPTR)path, ACCESS_READ)))
            WriteAll(path, in, len);
        else
            UnLock(l);
        DeleteFile((CONST_STRPTR)tmp);
        r = AA_SS_WRITE;
        goto out;
    }
    SetProtection((CONST_STRPTR)path, prot);

out:
    if (out)
        FreeVec(out);
    if (in)
        FreeVec(in);
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    return r;
}
