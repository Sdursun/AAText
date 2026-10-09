/*
 * AAText - font diagnostics: check the .otag files in FONTS:.
 * See fontscan.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <string.h>

#include "fontscan.h"
#include "otag.h"

#define MAX_OTAG (64 * 1024)

static void Copy(char *dst, const char *src, LONG size)
{
    LONG i;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static BOOL EndsWith(const char *s, const char *suffix)
{
    LONG ls = strlen(s), lx = strlen(suffix);

    return ls > lx && !Stricmp((CONST_STRPTR)s + ls - lx,
                               (CONST_STRPTR)suffix);
}

/* TrueType, OpenType (CFF) or collection? Looks at the first 4 bytes. */
static BOOL IsFontFile(const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    UBYTE h[4];
    BOOL ok = FALSE;

    if (!fh)
        return FALSE;
    if (Read(fh, h, 4) == 4)
        ok = (h[0] == 0 && h[1] == 1 && h[2] == 0 && h[3] == 0) ||
             !memcmp(h, "true", 4) || !memcmp(h, "OTTO", 4) ||
             !memcmp(h, "ttcf", 4);
    Close(fh);
    return ok;
}

/* Engines AAText can use: TrueType/OpenType through FreeType. */
static BOOL KnownEngine(const char *engine)
{
    return !engine || !Stricmp((CONST_STRPTR)engine, (CONST_STRPTR)"ttf") ||
           !Stricmp((CONST_STRPTR)engine, (CONST_STRPTR)"ttengine") ||
           !Strnicmp((CONST_STRPTR)engine, (CONST_STRPTR)"freetype", 8);
}

static void Check(struct AADiagEntry *e, UBYTE *buf)
{
    static struct AAOTagInfo ot;
    BPTR fh, lock;
    LONG len = 0;

    fh = Open((CONST_STRPTR)e->otag, MODE_OLDFILE);
    if (fh)
    {
        len = Read(fh, buf, MAX_OTAG);
        Close(fh);
    }
    if (len <= 0 || !aa_ParseOTag(buf, len, &ot))
    {
        /* other engines (bullet) name their files differently */
        e->status = len > 0 && ot.engine && !KnownEngine(ot.engine)
                    ? AA_DIAG_OTHER : AA_DIAG_BADOTAG;
        if (len > 0 && ot.engine)
            Copy(e->engine, ot.engine, sizeof(e->engine));
        return;
    }
    if (ot.engine)
        Copy(e->engine, ot.engine, sizeof(e->engine));
    e->facenum = ot.facenum;
    e->codepage = ot.hascodepage;
    Copy(e->want, ot.fontfile, sizeof(e->want));

    lock = aa_LockFontFile(ot.fontfile, e->found);
    if (!lock)
    {
        e->found[0] = 0;
        e->status = KnownEngine(ot.engine) ? AA_DIAG_MISSING : AA_DIAG_OTHER;
        return;
    }
    UnLock(lock);
    if (!KnownEngine(ot.engine))
        e->status = AA_DIAG_OTHER;
    else if (!IsFontFile(e->found))
        e->status = AA_DIAG_BADFILE;
    else
        e->status = strcmp(e->found, e->want) ? AA_DIAG_MOVED : AA_DIAG_OK;
}

/* Add the .otag files of one directory; FALSE when out is full. */
static BOOL ScanDir(BPTR dir, struct AADiagEntry *out, LONG max, LONG *n,
                    struct FileInfoBlock *fib, UBYTE *buf)
{
    char dirname[AA_FONTFILE_LEN];

    if (!NameFromLock(dir, (STRPTR)dirname, sizeof(dirname)) ||
        !Examine(dir, fib))
        return TRUE;
    while (ExNext(dir, fib))
    {
        const char *fn = (const char *)fib->fib_FileName;
        struct AADiagEntry *e;
        LONG i, nl;

        if (fib->fib_DirEntryType > 0 || !EndsWith(fn, ".otag"))
            continue;
        nl = strlen(fn) - 5;
        if (nl >= AA_NAME_LEN)
            nl = AA_NAME_LEN - 1;

        /* the first directory of the assign wins, as in diskfont */
        for (i = 0; i < *n; i++)
            if (!Strnicmp((CONST_STRPTR)out[i].name, (CONST_STRPTR)fn, nl) &&
                !out[i].name[nl])
                break;
        if (i < *n)
            continue;
        if (*n == max)
            return FALSE;

        e = &out[(*n)++];
        memset(e, 0, sizeof(*e));
        Copy(e->name, fn, nl + 1);
        Copy(e->otag, dirname, sizeof(e->otag));
        AddPart((STRPTR)e->otag, (CONST_STRPTR)fn, sizeof(e->otag));
        Check(e, buf);
    }
    return TRUE;
}

static BOOL Problem(const struct AADiagEntry *e)
{
    return e->status != AA_DIAG_OK && e->status != AA_DIAG_OTHER;
}

/* Sort order: fonts with problems first, then by name. */
static BOOL Before(const struct AADiagEntry *a, const struct AADiagEntry *b)
{
    if (Problem(a) != Problem(b))
        return Problem(a);
    return Stricmp((CONST_STRPTR)a->name, (CONST_STRPTR)b->name) < 0;
}

LONG aa_ScanFonts(struct AADiagEntry *out, LONG max)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    UBYTE *buf = AllocVec(MAX_OTAG, MEMF_ANY);
    struct DevProc *dp = NULL;
    LONG n = 0, dirs = 0, i, j;

    if (!fib || !buf)
    {
        if (fib)
            FreeDosObject(DOS_FIB, fib);
        FreeVec(buf);
        return -1;
    }

    /* every directory of a multi-directory assign */
    while ((dp = GetDeviceProc((CONST_STRPTR)"FONTS:", dp)))
    {
        if (dp->dvp_Lock)
        {
            dirs++;
            if (!ScanDir(dp->dvp_Lock, out, max, &n, fib, buf))
                break;
        }
        if (!(dp->dvp_Flags & DVPF_ASSIGN))
            break;
    }
    FreeDeviceProc(dp);

    /* not an assign (or a handler without a lock): the plain way */
    if (!dirs)
    {
        BPTR lock = Lock((CONST_STRPTR)"FONTS:", ACCESS_READ);

        if (lock)
        {
            dirs++;
            ScanDir(lock, out, max, &n, fib, buf);
            UnLock(lock);
        }
    }

    FreeVec(buf);
    FreeDosObject(DOS_FIB, fib);
    if (!dirs)
        return -1;

    /* problems first, then by name (a few hundred entries at most) */
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && Before(&out[j], &out[j - 1]); j--)
        {
            static struct AADiagEntry t;

            t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

static BOOL WriteAll(const char *path, const UBYTE *data, LONG len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    BOOL ok;

    if (!fh)
        return FALSE;
    ok = Write(fh, (APTR)data, len) == len;
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

LONG aa_FixOTag(struct AADiagEntry *e, char *backup)
{
    UBYTE *buf, *out;
    LONG len = 0, newlen, result = AA_FIX_OK;
    BPTR fh, lock;

    backup[0] = 0;
    if (e->status != AA_DIAG_MOVED)
        return AA_FIX_NOTMOVED;
    buf = AllocVec(MAX_OTAG * 2, MEMF_ANY);
    if (!buf)
        return AA_FIX_READ;
    out = buf + MAX_OTAG;

    if ((fh = Open((CONST_STRPTR)e->otag, MODE_OLDFILE)))
    {
        len = Read(fh, buf, MAX_OTAG);
        Close(fh);
    }
    newlen = len > 0 && len < MAX_OTAG
             ? aa_OTagSetFontFile(buf, len, e->found, out, MAX_OTAG) : 0;
    if (!newlen)
        result = AA_FIX_READ;
    else
    {
        Copy(backup, e->otag, AA_FONTFILE_LEN - 4);
        strcat(backup, ".bak");
        lock = Lock((CONST_STRPTR)backup, ACCESS_READ);
        if (lock)
            UnLock(lock);       /* keep the oldest original */
        else if (!WriteAll(backup, buf, len))
            result = AA_FIX_BACKUP;
        if (result == AA_FIX_OK && !WriteAll(e->otag, out, newlen))
            result = AA_FIX_WRITE;
    }
    if (result != AA_FIX_BACKUP && result != AA_FIX_READ)
    {
        /* check again what is on disk now */
        e->want[0] = e->found[0] = e->engine[0] = 0;
        Check(e, buf);
    }
    FreeVec(buf);
    return result;
}
