/*
 * AATextManager - its own settings (see settings.h).
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <string.h>

#include "settings.h"
#include "../charsets.h"
#include "../prefs.h"

void mgr_DefaultSettings(struct MgrSettings *s)
{
    strcpy(s->dir[MGR_DIR_TTF], MGR_DIR_DEFAULT);
    strcpy(s->dir[MGR_DIR_OTF], MGR_DIR_OTF_DEFAULT);
    strcpy(s->dir[MGR_DIR_TTC], MGR_DIR_TTC_DEFAULT);
    s->charset = AA_CHARSET_LATIN1;
    strcpy(s->catalog, MGR_CATALOG_URL);
}

static const char *const dirkeys[MGR_NUM_DIRS] = { "ttfdir", "otfdir", "ttcdir" };

static BOOL IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static void ParseLine(struct MgrSettings *s, char *line)
{
    char *key = line, *val;
    LONG i, n;

    while (IsSpace(*key))
        key++;
    if (!*key || *key == '#' || *key == ';')
        return;
    for (val = key; *val && !IsSpace(*val); val++)
        ;
    if (*val)
        *val++ = 0;
    while (IsSpace(*val))
        val++;
    n = strlen(val);
    while (n && IsSpace(val[n - 1]))
        val[--n] = 0;
    if (!n)
        return;

    for (i = 0; i < MGR_NUM_DIRS; i++)
        if (Stricmp((CONST_STRPTR)key, (CONST_STRPTR)dirkeys[i]) == 0 &&
            n < MGR_DIR_LEN)
            strcpy(s->dir[i], val);
    if (Stricmp((CONST_STRPTR)key, (CONST_STRPTR)"catalog") == 0 &&
        n < MGR_DIR_LEN)
        strcpy(s->catalog, val);
    if (Stricmp((CONST_STRPTR)key, (CONST_STRPTR)"charset") == 0)
        for (i = 0; i < AA_NUM_CHARSETS; i++)
            if (Stricmp((CONST_STRPTR)val,
                        (CONST_STRPTR)aa_CharsetNames[i]) == 0)
                s->charset = i;
}

static BOOL LoadFrom(struct MgrSettings *s, const char *path)
{
    static char buf[2048];
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG len;
    char *line, *nl;

    if (!fh)
        return FALSE;
    len = Read(fh, buf, sizeof(buf) - 1);
    Close(fh);
    if (len < 0)
        return FALSE;
    buf[len] = 0;
    for (line = buf; line; line = nl)
    {
        if ((nl = strchr(line, '\n')))
            *nl++ = 0;
        ParseLine(s, line);
    }
    return TRUE;
}

BOOL mgr_LoadSettings(struct MgrSettings *s)
{
    mgr_DefaultSettings(s);
    return LoadFrom(s, MGR_PREFS_ENV) || LoadFrom(s, MGR_PREFS_ENVARC);
}

static BOOL SaveTo(const struct MgrSettings *s, const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    BOOL ok = TRUE;
    LONG i;

    if (!fh)
        return FALSE;
    FPuts(fh, (CONST_STRPTR)"# AATextManager settings\n");
    for (i = 0; i < MGR_NUM_DIRS; i++)
        if (FPrintf(fh, (CONST_STRPTR)"%s %s\n", (ULONG)dirkeys[i],
                    (ULONG)s->dir[i]) < 0)
            ok = FALSE;
    if (strcmp(s->catalog, MGR_CATALOG_URL) &&
        FPrintf(fh, (CONST_STRPTR)"catalog %s\n", (ULONG)s->catalog) < 0)
        ok = FALSE;
    if (FPrintf(fh, (CONST_STRPTR)"charset %s\n",
                (ULONG)aa_CharsetNames[s->charset < AA_NUM_CHARSETS ?
                                       s->charset : 0]) < 0)
        ok = FALSE;
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

BOOL mgr_SaveSettings(const struct MgrSettings *s)
{
    BOOL env = SaveTo(s, MGR_PREFS_ENV);

    return SaveTo(s, MGR_PREFS_ENVARC) && env;
}

static BOOL EndsWith(const char *name, const char *ext)
{
    LONG n = strlen(name), e = strlen(ext);

    return n > e && Stricmp((CONST_STRPTR)(name + n - e),
                            (CONST_STRPTR)ext) == 0;
}

LONG mgr_DirKind(const char *file)
{
    if (EndsWith(file, ".otf"))
        return MGR_DIR_OTF;
    if (EndsWith(file, ".ttc") || EndsWith(file, ".otc"))
        return MGR_DIR_TTC;
    return MGR_DIR_TTF;
}

void mgr_FontsRelative(char *path, LONG len)
{
    static char full[MGR_DIR_LEN], fonts[MGR_DIR_LEN];
    struct DevProc *dp = NULL;
    BPTR lock;
    LONG n;

    if (Strnicmp((CONST_STRPTR)path, (CONST_STRPTR)"FONTS:", 6) == 0)
        return;
    if (!(lock = Lock((CONST_STRPTR)path, ACCESS_READ)))
        return;                 /* not there (yet): leave it */
    n = NameFromLock(lock, (STRPTR)full, sizeof(full));
    UnLock(lock);
    if (!n)
        return;

    /* every directory of the FONTS: assign */
    while ((dp = GetDeviceProc((CONST_STRPTR)"FONTS:", dp)))
    {
        if (!dp->dvp_Lock ||
            !NameFromLock(dp->dvp_Lock, (STRPTR)fonts, sizeof(fonts)))
            continue;
        n = strlen(fonts);
        if (Strnicmp((CONST_STRPTR)full, (CONST_STRPTR)fonts, n) == 0 &&
            (full[n] == 0 || full[n] == '/' || fonts[n - 1] == ':'))
        {
            const char *rest = full + n;

            if (*rest == '/')
                rest++;
            if (6 + strlen(rest) + 2 <= (ULONG)len)
            {
                strcpy(path, "FONTS:");
                strcat(path, rest);
                if (*rest)
                    strcat(path, "/");
            }
            break;
        }
        if (!(dp->dvp_Flags & DVPF_ASSIGN))
            break;
    }
    FreeDeviceProc(dp);
}
