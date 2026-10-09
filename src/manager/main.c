/*
 * AATextManager - installs TrueType/OpenType fonts for diskfont.
 *
 * Shell use (the window comes later):
 *   AATextManager FILES/M/A,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,
 *                 OVERWRITE/S
 *
 * FreeType comes from aatext.library.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <graphics/text.h>
#include <libraries/diskfont.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <proto/aatext.h>

#include "../fontinstall.h"
#include "../charsets.h"

struct Library *AATextBase;
struct Library *DiskfontBase;

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static const char version[] = "$VER: AATextManager 0.1 (9.10.2026)";

#define TEMPLATE "FILES/M/A,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,OVERWRITE/S"
enum { ARG_FILES, ARG_FACE, ARG_CHARSET, ARG_ENGINE, ARG_SIZES, ARG_TO,
       ARG_OVERWRITE, ARG_COUNT };

static LONG Stricmp(CONST_STRPTR a, CONST_STRPTR b)
{
    for (; ToLower(*a) == ToLower(*b); a++, b++)
        if (!*a)
            return 0;
    return ToLower(*a) - ToLower(*b);
}

static LONG Charset(const char *name)
{
    LONG i;

    if (Stricmp((CONST_STRPTR)name, (CONST_STRPTR)"none") == 0)
        return -1;
    for (i = 0; i < AA_NUM_CHARSETS; i++)
        if (Stricmp((CONST_STRPTR)name, (CONST_STRPTR)aa_CharsetNames[i]) == 0)
            return i;
    return -2;
}

/* "8-16,18,20,24" -> sizes; FALSE if malformed */
static BOOL ParseSizes(const char *s, struct AAInstall *in)
{
    LONG a, b, n;

    in->numsizes = 0;
    while (*s)
    {
        n = StrToLong((CONST_STRPTR)s, &a);
        if (n <= 0 || a < 2 || a > 999)
            return FALSE;
        s += n;
        b = a;
        if (*s == '-')
        {
            n = StrToLong((CONST_STRPTR)++s, &b);
            if (n <= 0 || b < a || b > 999)
                return FALSE;
            s += n;
        }
        for (; a <= b; a++)
        {
            if (in->numsizes >= AA_INSTALL_MAXSIZES)
                return FALSE;
            in->sizes[in->numsizes++] = a;
        }
        if (*s == ',')
            s++;
        else if (*s)
            return FALSE;
    }
    return in->numsizes > 0;
}

/* Can diskfont open what was installed? (It looks in FONTS:.) */
static void Check(const struct AAInstall *in)
{
    struct TextAttr ta;
    struct TextFont *tf;
    char name[40];

    if (!DiskfontBase)
        return;
    strcpy(name, in->name);
    strcat(name, ".font");
    ta.ta_Name = (STRPTR)name;
    ta.ta_YSize = in->sizes[0];
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    if ((tf = OpenDiskFont(&ta)))
    {
        CloseFont(tf);
        Printf("  checked: diskfont opens %s %ld\n", (LONG)name,
               (LONG)in->sizes[0]);
    }
    else
        Printf("  WARNING: diskfont cannot open %s (is %s.library "
               "installed, is the directory in FONTS:?)\n", (LONG)name,
               (LONG)in->engine);
}

int main(void)
{
    LONG args[ARG_COUNT] = { 0 };
    struct RDArgs *rda;
    static struct AAInstall in;
    STRPTR *files;
    LONG rc = RETURN_OK, warn = RETURN_OK;
    LONG cs = 0;                    /* latin1, the Amiga default */

    (void)version;
    if (!(rda = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL)))
    {
        PrintFault(IoErr(), (CONST_STRPTR)"AATextManager");
        return RETURN_FAIL;
    }
    AATextBase = OpenLibrary((CONST_STRPTR)AATEXTLIBNAME, AATEXTLIBVERSION);
    if (!AATextBase)
    {
        Printf("AATextManager: needs %s %ld or newer (LIBS:)\n",
               (LONG)AATEXTLIBNAME, (LONG)AATEXTLIBVERSION);
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);

    aa_InstallDefaults(&in);
    if (args[ARG_CHARSET])
        cs = Charset((const char *)args[ARG_CHARSET]);
    if (cs == -2)
    {
        Printf("AATextManager: unknown character set %s\n", args[ARG_CHARSET]);
        rc = RETURN_ERROR;
    }
    in.charset = cs;
    if (args[ARG_ENGINE])
        in.engine = (const char *)args[ARG_ENGINE];
    if (args[ARG_TO])
        in.fonts = (const char *)args[ARG_TO];
    if (args[ARG_FACE])
        in.face = *(LONG *)args[ARG_FACE];
    in.overwrite = args[ARG_OVERWRITE] != 0;
    if (args[ARG_SIZES] && !ParseSizes((const char *)args[ARG_SIZES], &in))
    {
        Printf("AATextManager: bad SIZES (example: 8-16,18,20,24)\n");
        rc = RETURN_ERROR;
    }

    for (files = (STRPTR *)args[ARG_FILES]; rc == RETURN_OK && *files; files++)
    {
        LONG r;

        in.source = (const char *)*files;
        r = aa_InstallFont(&in);
        switch (r)
        {
        case AA_INSTALL_OK:
            Printf("%s: installed as %s (%s %s)%s\n", (LONG)in.source,
                   (LONG)in.name, (LONG)in.info.family, (LONG)in.info.style,
                   (LONG)(in.replaced ? ", old files kept as .bak" : ""));
            if (in.info.numfaces > 1)
                Printf("  file has %ld faces; this was FACE %ld\n",
                       in.info.numfaces, in.face);
            Check(&in);
            break;
        case AA_INSTALL_NOTFONT:
            Printf("%s: not a font FreeType can read\n", (LONG)in.source);
            warn = RETURN_WARN;
            break;
        case AA_INSTALL_EXISTS:
            Printf("%s: %s already exists, skipped (OVERWRITE replaces "
                   "it)\n", (LONG)in.source, (LONG)in.name);
            warn = RETURN_WARN;
            break;
        default:
            Printf("%s: %s\n", (LONG)in.source,
                   (LONG)(r == AA_INSTALL_COPYFAIL ?
                          "cannot copy the font file" :
                          "cannot write .font/.otag"));
            rc = RETURN_ERROR;
            break;
        }
    }

    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
    CloseLibrary(AATextBase);
    FreeArgs(rda);
    return rc != RETURN_OK ? rc : warn;
}
