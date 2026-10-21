/*
 * AATextManager - fonts to install, repair, diagnostics and download,
 * without a window. See core.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core.h"
#include "manager.h"
#include "../aamsg.h"
#include "../aaclient.h"
#include "../charsets.h"

#define MAX_CATTEXT  (256 * 1024)
#define DL_DIR       "T:AATextFonts"

struct ManagerCore mcore;

BOOL mc_Init(void)
{
    mcore.numdscan = -1;
    mcore.files = AllocVec(MC_MAX_FILES * sizeof(*mcore.files),
                           MEMF_ANY | MEMF_CLEAR);
    mcore.diag = AllocVec(MC_MAX_FONTS * sizeof(*mcore.diag),
                          MEMF_ANY | MEMF_CLEAR);
    mcore.need = AllocVec(MC_MAX_FONTS * sizeof(*mcore.need),
                          MEMF_ANY | MEMF_CLEAR);
    mcore.cat = AllocVec(MC_MAX_CAT * sizeof(*mcore.cat),
                         MEMF_ANY | MEMF_CLEAR);
    mcore.shown = AllocVec(MC_MAX_CAT * sizeof(*mcore.shown),
                           MEMF_ANY | MEMF_CLEAR);
    /* the character set saved on the Settings tab, else ISO-8859-1 */
    mgr_LoadSettings(&mcore.settings);
    if (mcore.settings.charset < 0 ||
        mcore.settings.charset >= AA_NUM_CHARSETS)
        mcore.settings.charset = AA_CHARSET_LATIN1;
    return mcore.files && mcore.diag && mcore.need && mcore.cat &&
           mcore.shown;
}

void mc_Cleanup(void)
{
    APTR *p[] =
    {
        (APTR *)&mcore.files, (APTR *)&mcore.diag, (APTR *)&mcore.need,
        (APTR *)&mcore.dscan, (APTR *)&mcore.cat, (APTR *)&mcore.shown
    };
    ULONG i;

    for (i = 0; i < sizeof(p) / sizeof(p[0]); i++)
    {
        if (*p[i])
            FreeVec(*p[i]);
        *p[i] = NULL;
    }
}

void mc_TellAAText(void)
{
    struct AAMessage *m = aa_SendCommand(AACMD_RELOAD, NULL, NULL, 100);

    if (m != AACLIENT_NOTRUNNING && m != AACLIENT_TIMEOUT)
        aa_FreeReply(m);
}

const char *mc_FontLabel(const struct AADiagEntry *e)
{
    return e->display[0] ? e->display : e->name;
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

BOOL mc_AddFile(const char *path)
{
    LONG face = 0, faces = 1, i;

    /* listed already (added twice): nothing new */
    for (i = 0; i < mcore.numfiles; i++)
        if (!Stricmp((CONST_STRPTR)mcore.files[i].path, (CONST_STRPTR)path))
            return TRUE;
    while (face < faces && mcore.numfiles < MC_MAX_FILES)
    {
        struct FileEntry *f = &mcore.files[mcore.numfiles];

        if (!aa_GetFontInfo(path, face, &f->info))
            return face > 0;
        snprintf(f->path, sizeof(f->path), "%s", path);
        f->face = face;
        snprintf(f->facetext, sizeof(f->facetext), "%ld", (long)face);
        faces = f->info.numfaces;
        mcore.numfiles++;
        face++;
    }
    return TRUE;
}

void mc_RemoveFile(LONG i)
{
    if (i < 0 || i >= mcore.numfiles)
        return;
    memmove(&mcore.files[i], &mcore.files[i + 1],
            (mcore.numfiles - i - 1) * sizeof(mcore.files[0]));
    mcore.numfiles--;
}

void mc_InstallOne(struct AAInstall *in, const char *path, LONG face,
                   struct MCResult *r)
{
    in->source = path;
    in->face = face;
    in->fontdir = mcore.settings.dir[mgr_DirKind(path)];
    switch (aa_InstallFont(in))
    {
        case AA_INSTALL_OK:
            r->ok++;
            if (!mgr_CheckFont(in))
                snprintf(r->notopen, sizeof(r->notopen), "%s", in->name);
            break;
        case AA_INSTALL_EXISTS:
            r->skipped++;
            break;
        default:
            r->failed++;
            break;
    }
}

void mc_InstallAll(struct AAInstall *in, struct MCResult *r)
{
    LONG i;

    for (i = 0; i < mcore.numfiles; i++)
        mc_InstallOne(in, mcore.files[i].path, mcore.files[i].face, r);
    if (r->ok)
        mc_TellAAText();
    if (!r->skipped && !r->failed)
        mcore.numfiles = 0;
}

/* ------------------------------------------------------------------ */
/* Repair                                                              */
/* ------------------------------------------------------------------ */

LONG mc_Check(const struct AARepair *r)
{
    LONG i, todo = 0;

    /* the Diagnostics tab scans again after a repair */
    if (mcore.dscan)
        FreeVec(mcore.dscan);
    mcore.dscan = NULL;
    mcore.numdscan = -1;
    mcore.numdiag = aa_ScanFonts(mcore.diag, MC_MAX_FONTS);
    if (mcore.numdiag < 0)
        return -1;
    for (i = 0; i < mcore.numdiag; i++)
        if ((mcore.need[i] = aa_RepairNeeded(&mcore.diag[i], r)))
            todo++;
    return todo;
}

LONG mc_RepairAll(const struct AARepair *r)
{
    static char backup[AA_FONTFILE_LEN];
    LONG i, done = 0;

    for (i = 0; i < mcore.numdiag; i++)
        if (aa_RepairNeeded(&mcore.diag[i], r) &&
            aa_RepairOTag(&mcore.diag[i], r, backup) == AA_FIX_OK)
            done++;
    return done;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/* English status names for the report, which goes into bug reports */
static const char *const diag_name[AA_DIAG_NUM] =
{
    "ok", "moved", "missing", "not a font", "bad .otag", "other engine"
};

LONG mc_Scan(void)
{
    if (!mcore.dscan)
        mcore.dscan = AllocVec(MC_MAX_DIAG * sizeof(*mcore.dscan), MEMF_ANY);
    if (!mcore.dscan)
        return mcore.numdscan = -1;
    return mcore.numdscan = aa_ScanFonts(mcore.dscan, MC_MAX_DIAG);
}

const char *mc_FontFile(const struct AADiagEntry *e)
{
    if (e->status == AA_DIAG_OK || e->status == AA_DIAG_MOVED)
        return e->found[0] ? e->found : e->want;
    if (e->status == AA_DIAG_BADFILE)
        return e->want;
    return "";
}

const char *mc_DeletableFile(const struct AADiagEntry *e)
{
    const char *file = mc_FontFile(e);
    BPTR lock;

    if (!file[0] || !(lock = Lock((CONST_STRPTR)file, ACCESS_READ)))
        return "";
    UnLock(lock);
    return file;
}

const struct AADiagEntry *mc_SharedBy(const struct AADiagEntry *e,
                                      const char *file)
{
    LONG i;

    for (i = 0; i < mcore.numdscan; i++)
        if (&mcore.dscan[i] != e &&
            !Stricmp((CONST_STRPTR)mc_FontFile(&mcore.dscan[i]),
                     (CONST_STRPTR)file))
            return &mcore.dscan[i];
    return NULL;
}

/* delete the files in dir and dir itself (a font's size directory:
   only files in it); FALSE if something stays */
static BOOL DeleteSizeDir(const char *dir)
{
    static char path[AA_FONTFILE_LEN + 40];
    struct FileInfoBlock *fib;
    BPTR lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
    BOOL ok = TRUE;

    if (!lock)
        return TRUE;            /* no size directory */
    if (!(fib = AllocDosObject(DOS_FIB, NULL)))
    {
        UnLock(lock);
        return FALSE;
    }
    if (Examine(lock, fib) && fib->fib_DirEntryType > 0)
    {
        /* collect first, delete after: deleting while ExNext() walks
           the directory is not safe on every file system */
        char (*names)[108] = AllocVec(64 * 108, MEMF_ANY);
        LONG n = 0, i;

        if (!names)
            ok = FALSE;
        else
        {
            while (ExNext(lock, fib))
            {
                if (fib->fib_DirEntryType > 0 || n >= 64)
                {
                    ok = FALSE;     /* not a plain size directory */
                    continue;
                }
                snprintf(names[n++], 108, "%s", (char *)fib->fib_FileName);
            }
        }
        UnLock(lock);
        lock = 0;
        for (i = 0; ok && i < n; i++)
        {
            snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
            if (!DeleteFile((CONST_STRPTR)path))
                ok = FALSE;
        }
        if (names)
            FreeVec(names);
        if (ok && !DeleteFile((CONST_STRPTR)dir))
            ok = FALSE;
    }
    else
        ok = FALSE;             /* a file of that name: leave it */
    if (lock)
        UnLock(lock);
    FreeDosObject(DOS_FIB, fib);
    return ok;
}

/* delete path if it exists; FALSE if it exists and stays */
static BOOL DeleteIfThere(const char *path)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);

    if (!lock)
        return TRUE;
    UnLock(lock);
    return DeleteFile((CONST_STRPTR)path) != 0;
}

BOOL mc_DeleteFont(LONG i, BOOL withfile, char *failed, LONG len)
{
    static char base[AA_FONTFILE_LEN], path[AA_FONTFILE_LEN + 16];
    static const char *const ext[] =
    {
        /* Repair's backups too: they would only bring the font back half */
        ".otag", ".font", ".otag.bak", ".font.bak", NULL
    };
    const struct AADiagEntry *e;
    const char *file;
    LONG n, k;

    failed[0] = 0;
    if (i < 0 || i >= mcore.numdscan)
        return FALSE;
    e = &mcore.dscan[i];
    file = mc_DeletableFile(e);

    /* FONTS:x.otag -> FONTS:x */
    snprintf(base, sizeof(base), "%s", e->otag);
    n = strlen(base);
    if (n > 5 && !Stricmp((CONST_STRPTR)base + n - 5, (CONST_STRPTR)".otag"))
        base[n - 5] = 0;

    for (k = 0; ext[k]; k++)
    {
        snprintf(path, sizeof(path), "%s%s", base, ext[k]);
        if (!DeleteIfThere(path))
        {
            snprintf(failed, len, "%s", path);
            return FALSE;
        }
    }
    if (!DeleteSizeDir(base))
    {
        snprintf(failed, len, "%s/", base);
        return FALSE;
    }
    if (withfile && file[0] && !DeleteIfThere(file))
    {
        snprintf(failed, len, "%s", file);
        return FALSE;
    }
    return TRUE;
}

BOOL mc_WriteReport(const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    LONG i;
    BOOL ok = TRUE;

    if (!fh)
        return FALSE;
    FPrintf(fh, (CONST_STRPTR)"AAText font report (%s)\n",
            (ULONG)"AATextManager " AAMGR_VERSION);
    FPrintf(fh, (CONST_STRPTR)"%ld font(s) in FONTS:\n",
            (ULONG)mcore.numdscan);
    for (i = 0; i < mcore.numdscan && ok; i++)
    {
        struct AADiagEntry *e = &mcore.dscan[i];

        FPrintf(fh, (CONST_STRPTR)"\n%s: %s\n  .otag:    %s\n",
                (ULONG)e->name, (ULONG)diag_name[e->status], (ULONG)e->otag);
        if (e->display[0])
            FPrintf(fh, (CONST_STRPTR)"  family:   %s\n", (ULONG)e->display);
        if (e->want[0])
            FPrintf(fh, (CONST_STRPTR)"  in .otag: %s\n", (ULONG)e->want);
        if (e->found[0] && strcmp(e->found, e->want))
            FPrintf(fh, (CONST_STRPTR)"  found:    %s\n", (ULONG)e->found);
        ok = FPrintf(fh, (CONST_STRPTR)"  engine:   %s (%s.library %s), "
                     "face %ld, code page %s",
                     (ULONG)(e->engine[0] ? e->engine : "?"),
                     (ULONG)(e->engine[0] ? e->engine : "?"),
                     (ULONG)(e->enginever[0] ? e->enginever : "not installed"),
                     (ULONG)e->facenum,
                     (ULONG)(e->charset >= 0 ? aa_CharsetNames[e->charset] :
                             e->charset == AA_CS_OWN ? "own table" :
                             e->charset == AA_CS_NONE ? "none" : "?")) >= 0;
        if (ok && e->charset == AA_CS_OWN)
            FPrintf(fh, (CONST_STRPTR)" (nearest %s, %ld differ)",
                    (ULONG)aa_CharsetNames[e->nearcharset],
                    (ULONG)e->neardiff);
        if (ok)
            ok = FPrintf(fh, (CONST_STRPTR)"\n") >= 0;
    }
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

/* ------------------------------------------------------------------ */
/* Download                                                            */
/* ------------------------------------------------------------------ */

/* Copy one tab-separated field; returns the start of the next one. */
static char *Field(char *p, char *dst, LONG len)
{
    LONG n = 0;

    while (*p && *p != '\t')
    {
        if (n < len - 1)
            dst[n++] = *p;
        p++;
    }
    dst[n] = 0;
    return *p ? p + 1 : p;
}

static void ParseCatalog(char *text)
{
    char *line, *nl, num[16];
    struct CatEntry *e;

    mcore.numcat = 0;
    mcore.catbase[0] = 0;
    for (line = text; line && mcore.numcat < MC_MAX_CAT; line = nl)
    {
        if ((nl = strchr(line, '\n')))
            *nl++ = 0;
        if (strchr(line, '\r'))
            *strchr(line, '\r') = 0;
        if (!line[0] || line[0] == '#')
            continue;
        if (!strncmp(line, "base ", 5))
        {
            snprintf(mcore.catbase, sizeof(mcore.catbase), "%s", line + 5);
            continue;
        }
        e = &mcore.cat[mcore.numcat];
        line = Field(line, e->family, sizeof(e->family));
        line = Field(line, e->style, sizeof(e->style));
        line = Field(line, e->licence, sizeof(e->licence));
        line = Field(line, num, sizeof(num));
        Field(line, e->path, sizeof(e->path));
        if (!e->family[0] || !e->path[0])
            continue;
        e->bytes = atol(num);
        snprintf(e->size, sizeof(e->size), "%ld KB",
                 (long)((e->bytes + 1023) / 1024));
        mcore.numcat++;
    }
}

LONG mc_LoadCatalog(char *error, LONG len)
{
    static struct AANet net;
    char *text;
    LONG got = -1, r = 0;
    BPTR fh;

    error[0] = 0;
    if (!(text = AllocVec(MAX_CATTEXT, MEMF_ANY)))
        return UPD_FAILED;
    if (strncmp(mcore.settings.catalog, "https://", 8))
    {
        /* a file (for testing a catalog before it is published) */
        if ((fh = Open((CONST_STRPTR)mcore.settings.catalog, MODE_OLDFILE)))
        {
            got = Read(fh, text, MAX_CATTEXT - 1);
            Close(fh);
        }
        if (got < 0)
            snprintf(error, len, "cannot read the file");
    }
    else if ((r = net_Open(&net, AAMGR_VERSION)))
    {
        snprintf(error, len, "%s", net.error);
        FreeVec(text);
        return r;
    }
    else
    {
        got = net_GetMem(&net, mcore.settings.catalog, text, MAX_CATTEXT);
        if (got < 0)
            snprintf(error, len, "%s", net.error);
        net_Close(&net);
    }
    if (got >= 0)
    {
        text[got] = 0;
        ParseCatalog(text);
    }
    FreeVec(text);
    return got >= 0 ? 0 : UPD_FAILED;
}

/* Case-insensitive "needle in hay" (ASCII is enough for family names) */
static BOOL Contains(const char *hay, const char *needle)
{
    LONG n = strlen(needle);

    for (; *hay; hay++)
        if (!Strnicmp((CONST_STRPTR)hay, (CONST_STRPTR)needle, n))
            return TRUE;
    return !n;
}

void mc_Filter(const char *filter)
{
    LONG i;

    mcore.numshown = 0;
    for (i = 0; i < mcore.numcat; i++)
        if (!filter || Contains(mcore.cat[i].family, filter))
            mcore.shown[mcore.numshown++] = i;
}

static LONG SizeOf(const char *path)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR l = Lock((CONST_STRPTR)path, ACCESS_READ);
    LONG size = -1;

    if (l && fib && Examine(l, fib))
        size = fib->fib_Size;
    if (l)
        UnLock(l);
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    return size;
}

BOOL mc_LocalCopy(const struct CatEntry *e, char *path, LONG len)
{
    snprintf(path, len, "%s", DL_DIR);
    AddPart((STRPTR)path, FilePart((CONST_STRPTR)e->path), len);
    return e->bytes > 0 && SizeOf(path) == e->bytes;
}

BOOL mc_Fetch(struct AANet *net, const struct CatEntry *e, char *path,
              LONG len)
{
    static char url[300];
    BPTR l;

    if (mc_LocalCopy(e, path, len))
        return TRUE;
    if (!(l = Lock((CONST_STRPTR)DL_DIR, ACCESS_READ)))
        l = CreateDir((CONST_STRPTR)DL_DIR);
    if (l)
        UnLock(l);
    if (!strncmp(e->path, "https://", 8))
        snprintf(url, sizeof(url), "%s", e->path);
    else
        snprintf(url, sizeof(url), "%s%s", mcore.catbase, e->path);
    if (net_GetFile(net, url, path))
    {
        LONG size = SizeOf(path);

        /* the size the catalog gives: a short file is half a font */
        if (e->bytes <= 0 || size == e->bytes)
            return TRUE;
        DeleteFile((CONST_STRPTR)path);
        snprintf(net->error, sizeof(net->error),
                 "incomplete: %ld of %ld bytes", (long)size, (long)e->bytes);
    }
    return FALSE;
}
