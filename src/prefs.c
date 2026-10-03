/*
 * AAText - preferences file parser.
 *
 *   # comment
 *   Arial          22  ->  FONTS:_TrueType/arial.ttf     [pixelsize] [real]
 *   "Some Font"    16  ->  "FONTS:My Fonts/x.ttf"
 *   gamma   1.8
 *   charset latin1 | latin5
 *   cache 256                (glyph cache size in KB)
 *   blacklist FinalWriter TypeSmith
 *   offscreen on | off
 *   auto on | off            (detect TrueType fonts via .otag, default on)
 *   real on | off            (real metrics for detected fonts, default off)
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "prefs.h"
#include "debug.h"

#define MAX_PREFS_SIZE  (64 * 1024)
#define MAX_TOKENS      8

static BOOL report_errors;

static void PrefsError(LONG line, const char *what)
{
    LONG args[2];

    D(("AAText: prefs line %ld: %s\n", line, (ULONG)what));
    if (!report_errors || !Output())
        return;
    args[0] = line;
    args[1] = (LONG)what;
    VPrintf((CONST_STRPTR)"AAText: prefs line %ld: %s\n", args);
}

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static BOOL StrIEq(const char *a, const char *b)
{
    while (*a && ToLower(*a) == ToLower(*b))
    {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void StrCopy(char *dst, const char *src, int size)
{
    int i;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* Parses an unsigned decimal; returns -1 if not a number. */
static LONG ParseUInt(const char *s)
{
    LONG v = 0;

    if (!*s)
        return -1;
    for (; *s; s++)
    {
        if (*s < '0' || *s > '9' || v > 100000)
            return -1;
        v = v * 10 + (*s - '0');
    }
    return v;
}

/* Parses "1.8" style numbers into hundredths; -1 on error. */
static LONG ParseFixed100(const char *s)
{
    LONG ip = 0, fp = 0, fdiv = 0;

    if (!*s)
        return -1;
    for (; *s && *s != '.'; s++)
    {
        if (*s < '0' || *s > '9' || ip > 100)
            return -1;
        ip = ip * 10 + (*s - '0');
    }
    if (*s == '.')
    {
        for (s++; *s; s++)
        {
            if (*s < '0' || *s > '9')
                return -1;
            if (fdiv < 2)
            {
                fp = fp * 10 + (*s - '0');
                fdiv++;
            }
        }
    }
    while (fdiv++ < 2)
        fp *= 10;
    return ip * 100 + fp;
}

/*
 * Splits a line in place into whitespace separated tokens. Double quotes
 * group words containing spaces. Stops at '#' outside quotes.
 */
static int Tokenize(char *line, char **tok)
{
    int n = 0;
    char *p = line;

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#')
            break;
        if (n == MAX_TOKENS)
            return -1;
        if (*p == '"')
        {
            tok[n++] = ++p;
            while (*p && *p != '"')
                p++;
            if (*p != '"')
                return -1;
            *p++ = 0;
        }
        else
        {
            tok[n++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = 0;
        }
    }
    return n;
}

static void ParseLine(struct AAPrefs *prefs, char *line, LONG lineno)
{
    char *tok[MAX_TOKENS];
    int n = Tokenize(line, tok);

    if (n < 0)
    {
        PrefsError(lineno, "syntax error");
        return;
    }
    if (n == 0)
        return;

    if (StrIEq(tok[0], "gamma"))
    {
        LONG g = n == 2 ? ParseFixed100(tok[1]) : -1;

        if (g < 50 || g > 400)
            PrefsError(lineno, "gamma must be between 0.5 and 4.0");
        else
            prefs->gamma100 = g;
        return;
    }
    if (StrIEq(tok[0], "charset"))
    {
        if (n == 2 && StrIEq(tok[1], "latin1"))
            prefs->charset = AA_CHARSET_LATIN1;
        else if (n == 2 && StrIEq(tok[1], "latin5"))
            prefs->charset = AA_CHARSET_LATIN5;
        else
            PrefsError(lineno, "charset must be latin1 or latin5");
        return;
    }
    if (StrIEq(tok[0], "cache"))
    {
        LONG kb = n == 2 ? ParseUInt(tok[1]) : -1;

        if (kb < 32 || kb > 16384)
            PrefsError(lineno, "cache must be between 32 and 16384 (KB)");
        else
            prefs->cachekb = kb;
        return;
    }
    if (StrIEq(tok[0], "auto") || StrIEq(tok[0], "real"))
    {
        BOOL *flag = StrIEq(tok[0], "auto") ? &prefs->autodetect
                                            : &prefs->autoreal;

        if (n == 2 && StrIEq(tok[1], "on"))
            *flag = TRUE;
        else if (n == 2 && StrIEq(tok[1], "off"))
            *flag = FALSE;
        else
            PrefsError(lineno, "expected on or off");
        return;
    }
    if (StrIEq(tok[0], "offscreen"))
    {
        if (n == 2 && StrIEq(tok[1], "on"))
            prefs->offscreen = TRUE;
        else if (n == 2 && StrIEq(tok[1], "off"))
            prefs->offscreen = FALSE;
        else
            PrefsError(lineno, "offscreen must be on or off");
        return;
    }
    if (StrIEq(tok[0], "blacklist"))
    {
        int i;

        if (n < 2)
            PrefsError(lineno, "blacklist needs at least one program name");
        for (i = 1; i < n; i++)
        {
            if (prefs->numblack == AA_MAX_BLACKLIST)
            {
                PrefsError(lineno, "too many blacklist entries");
                break;
            }
            StrCopy(prefs->blacklist[prefs->numblack++], tok[i], AA_NAME_LEN);
        }
        return;
    }

    /* font mapping: name size -> path [pixelsize] [real] */
    if (n >= 4 && n <= 6 && tok[2][0] == '-' && tok[2][1] == '>' && !tok[2][2])
    {
        struct AAMapping *m;
        LONG size = ParseUInt(tok[1]);
        LONG px = 0;
        BOOL real = FALSE;
        int len, i;

        if (size <= 0 || size > 255)
        {
            PrefsError(lineno, "invalid font size");
            return;
        }
        for (i = 4; i < n; i++)
        {
            LONG v = ParseUInt(tok[i]);

            if (StrIEq(tok[i], "real") && !real)
                real = TRUE;
            else if (!px && v > 0 && v <= 255)
                px = v;
            else
            {
                PrefsError(lineno, "expected pixel size and/or \"real\"");
                return;
            }
        }
        if (prefs->nummaps == AA_MAX_MAPPINGS)
        {
            PrefsError(lineno, "too many font mappings");
            return;
        }
        m = &prefs->map[prefs->nummaps++];
        StrCopy(m->fontname, tok[0], AA_NAME_LEN);
        /* accept "name.font" as well */
        len = 0;
        while (m->fontname[len])
            len++;
        if (len > 5 && StrIEq(&m->fontname[len - 5], ".font"))
            m->fontname[len - 5] = 0;
        m->ysize = size;
        StrCopy(m->ttfpath, tok[3], AA_PATH_LEN);
        m->pixelsize = px;
        m->real = real;
        return;
    }

    PrefsError(lineno, "unknown line");
}

static BPTR OpenPrefsFile(const char *path)
{
    BPTR fh;

    if (path)
        return Open((CONST_STRPTR)path, MODE_OLDFILE);
    fh = Open((CONST_STRPTR)"ENV:AAText.prefs", MODE_OLDFILE);
    if (!fh)
        fh = Open((CONST_STRPTR)"ENVARC:AAText.prefs", MODE_OLDFILE);
    return fh;
}

BOOL aa_ReadPrefs(struct AAPrefs *prefs, const char *path, BOOL report)
{
    BPTR fh;
    char *buf, *line, *p;
    LONG len, lineno = 1;

    report_errors = report;
    prefs->nummaps = 0;
    prefs->gamma100 = 180;
    prefs->charset = AA_CHARSET_LATIN1;
    prefs->offscreen = FALSE;
    prefs->autodetect = TRUE;
    prefs->autoreal = FALSE;
    prefs->cachekb = AA_DEFAULT_CACHE_KB;
    prefs->numblack = 0;

    fh = OpenPrefsFile(path);
    if (!fh)
        return FALSE;

    buf = AllocVec(MAX_PREFS_SIZE + 1, MEMF_ANY);
    if (!buf)
    {
        Close(fh);
        return FALSE;
    }
    len = Read(fh, buf, MAX_PREFS_SIZE);
    Close(fh);
    if (len < 0)
        len = 0;
    buf[len] = 0;

    for (line = p = buf; ; p++)
    {
        if (*p == '\r')         /* tolerate files edited on Windows */
            *p = ' ';
        if (*p == '\n' || *p == 0)
        {
            BOOL end = (*p == 0);

            *p = 0;
            ParseLine(prefs, line, lineno);
            if (end)
                break;
            lineno++;
            line = p + 1;
        }
    }

    FreeVec(buf);
    return TRUE;
}
