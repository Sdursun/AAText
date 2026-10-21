/*
 * AAText - preferences file writer. See prefswrite.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "prefswrite.h"
#include "charsets.h"

#define MAX_TEMPLATE (64 * 1024)

static const char *const managed[] =
{
    "gamma", "hinting", "real", "kerning", "cache", "fontmem", "offscreen", "aga", "antialias", "subpixel", "auto",
    "charset", "blacklist", NULL
};

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/*
 * A font mapping ("name size -> file ..."): a "->" token outside quotes
 * before any comment. AATextPrefs edits mappings, so it writes them.
 */
static BOOL IsMapping(const char *line, LONG len)
{
    BOOL quoted = FALSE;
    LONG i;

    for (i = 0; i < len; i++)
    {
        char c = line[i];

        if (c == '"')
            quoted = !quoted;
        else if (!quoted && c == '#')
            return FALSE;
        else if (!quoted && c == '-' && i + 1 < len && line[i + 1] == '>' &&
                 i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t') &&
                 (i + 2 == len || line[i + 2] == ' ' || line[i + 2] == '\t'))
            return TRUE;
    }
    return FALSE;
}

/* Does the line (len bytes) start with a managed keyword? */
static BOOL IsManaged(const char *line, LONG len)
{
    LONG i = 0, n, k;

    if (IsMapping(line, len))
        return TRUE;

    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    for (k = 0; managed[k]; k++)
    {
        const char *w = managed[k];

        for (n = 0; w[n] && i + n < len && ToLower(line[i + n]) == w[n]; n++)
            ;
        if (!w[n] && (i + n == len || line[i + n] == ' ' ||
                      line[i + n] == '\t'))
            return TRUE;
    }
    return FALSE;
}

static BOOL Put(BPTR fh, const char *s)
{
    LONG len = strlen(s);

    return Write(fh, (APTR)s, len) == len;
}

/*
 * Read the template completely first: it is often the very file that is
 * about to be replaced. Returns NULL (len 0) if there is none.
 */
static char *LoadTemplate(const char *template_path, LONG *lenp)
{
    BPTR in;
    char *buf;
    LONG len;

    *lenp = 0;
    if (!template_path || !(in = Open((CONST_STRPTR)template_path,
                                      MODE_OLDFILE)))
        return NULL;
    buf = AllocVec(MAX_TEMPLATE, MEMF_ANY);
    if (!buf)
    {
        Close(in);
        return NULL;
    }
    len = Read(in, buf, MAX_TEMPLATE);
    Close(in);
    *lenp = len > 0 ? len : 0;
    return buf;
}

/* Copy the unmanaged lines of the template; TRUE if anything was kept. */
static BOOL CopyUnmanaged(BPTR out, const char *buf, LONG len, BOOL *ok)
{
    LONG start, i;
    BOOL kept = FALSE;

    for (start = 0, i = 0; len > 0 && i <= len; i++)
    {
        if (i == len || buf[i] == '\n')
        {
            LONG n = i - start;

            /* drop a trailing CR of files edited on Windows */
            if (n > 0 && buf[start + n - 1] == '\r')
                n--;
            if (!IsManaged(buf + start, n) && !(i == len && n == 0))
            {
                if (Write(out, buf + start, n) != n || !Put(out, "\n"))
                    *ok = FALSE;
                kept = TRUE;
            }
            start = i + 1;
        }
    }
    return kept;
}

BOOL aa_WritePrefs(const struct AAPrefs *prefs, const char *path,
                   const char *template_path)
{
    static const char *const hint[] = { "normal", "none", "light", "full" };
    /* every name with " \"...\"" around it: 3 more bytes each */
    static char line[(AA_NAME_LEN + 3) * AA_MAX_BLACKLIST + AA_PATH_LEN +
                     64];
    BPTR out;
    BOOL ok = TRUE;
    LONG i, tlen;
    char *tbuf = LoadTemplate(template_path, &tlen);

    out = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!out)
    {
        if (tbuf)
            FreeVec(tbuf);
        return FALSE;
    }

    if (!CopyUnmanaged(out, tbuf, tlen, &ok))
        ok &= Put(out, "# AAText preferences, written by AATextPrefs\n");
    if (tbuf)
        FreeVec(tbuf);

    /*
     * Numbers as %ld with long arguments only: programs linked with
     * amiga.lib get its RawDoFmt() based sprintf(), where %d is 16 bit.
     */
    sprintf(line, "gamma %ld.%02ld\n", (long)(prefs->gamma100 / 100),
            (long)(prefs->gamma100 % 100));
    ok &= Put(out, line);
    sprintf(line, "hinting %s\n", hint[prefs->hinting < 4 ? prefs->hinting : 0]);
    ok &= Put(out, line);
    sprintf(line, "real %s\n", prefs->autoreal ? "on" : "off");
    ok &= Put(out, line);
    sprintf(line, "kerning %s\n", prefs->kerning ? "on" : "off");
    ok &= Put(out, line);
    sprintf(line, "cache %ld\n", (long)prefs->cachekb);
    ok &= Put(out, line);
    sprintf(line, "fontmem %ld\n", (long)prefs->fontmemkb);
    ok &= Put(out, line);
    sprintf(line, "offscreen %s\n", prefs->offscreen ? "on" : "off");
    ok &= Put(out, line);
    sprintf(line, "aga %s\n", prefs->aga ? "on" : "off");
    ok &= Put(out, line);
    sprintf(line, "antialias %s\n", prefs->antialias ? "on" : "off");
    ok &= Put(out, line);
    {
        static const char *const sp[] = { "off", "rgb", "bgr" };

        sprintf(line, "subpixel %s\n", sp[prefs->subpixel <= 2 ?
                                            prefs->subpixel : 0]);
        ok &= Put(out, line);
    }
    sprintf(line, "auto %s\n", prefs->autodetect ? "on" : "off");
    ok &= Put(out, line);
    sprintf(line, "charset %s\n", aa_CharsetNames[prefs->charset <
            AA_NUM_CHARSETS ? prefs->charset : AA_CHARSET_LATIN1]);
    ok &= Put(out, line);

    if (prefs->numblack)
    {
        strcpy(line, "blacklist");
        for (i = 0; i < prefs->numblack; i++)
        {
            const char *name = prefs->blacklist[i];
            BOOL quote = strchr(name, ' ') != NULL;

            strcat(line, quote ? " \"" : " ");
            strcat(line, name);
            if (quote)
                strcat(line, "\"");
        }
        strcat(line, "\n");
        ok &= Put(out, line);
    }

    /* font mappings; names and paths with blanks in quotes */
    for (i = 0; i < prefs->nummaps; i++)
    {
        const struct AAMapping *m = &prefs->map[i];
        BOOL qn = strchr(m->fontname, ' ') != NULL;
        BOOL qp = strchr(m->ttfpath, ' ') != NULL;

        sprintf(line, "%s%s%s %ld -> %s%s%s", qn ? "\"" : "", m->fontname,
                qn ? "\"" : "", (long)m->ysize, qp ? "\"" : "", m->ttfpath,
                qp ? "\"" : "");
        if (m->pixelsize)
            sprintf(line + strlen(line), " %ld", (long)m->pixelsize);
        if (m->real)
            strcat(line, " real");
        strcat(line, "\n");
        ok &= Put(out, line);
    }

    if (!Close(out))
        ok = FALSE;
    return ok;
}
