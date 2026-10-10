/*
 * AATextManager - installs TrueType/OpenType fonts for diskfont.
 *
 * Started from Workbench, or from the Shell without FILES or REPAIR,
 * it opens its window (gui.c). Shell use:
 *   AATextManager FILES/M,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,
 *                 OVERWRITE/S,REPAIR/S,APPLY/S,LANGUAGE/K
 *
 * FreeType comes from aatext.library.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <graphics/text.h>
#include <libraries/diskfont.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <proto/aatext.h>

#include "../fontinstall.h"
#include "../charsets.h"
#include "../fontscan.h"
#include "manager.h"

#define MAX_FONTS 1024

struct Library *AATextBase;
struct Library *DiskfontBase;

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static const char version[] __attribute__((used)) = "$VER: AATextManager 0.15 (10.10.2026)";

#define TEMPLATE "FILES/M,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,OVERWRITE/S,REPAIR/S,APPLY/S,LANGUAGE/K"
enum { ARG_FILES, ARG_FACE, ARG_CHARSET, ARG_ENGINE, ARG_SIZES, ARG_TO,
       ARG_OVERWRITE, ARG_REPAIR, ARG_APPLY, ARG_LANGUAGE, ARG_COUNT };

static LONG Stricmp_(CONST_STRPTR a, CONST_STRPTR b)
{
    for (; ToLower(*a) == ToLower(*b); a++, b++)
        if (!*a)
            return 0;
    return ToLower(*a) - ToLower(*b);
}

static LONG Charset(const char *name)
{
    LONG i;

    if (Stricmp_((CONST_STRPTR)name, (CONST_STRPTR)"none") == 0)
        return -1;
    for (i = 0; i < AA_NUM_CHARSETS; i++)
        if (Stricmp_((CONST_STRPTR)name, (CONST_STRPTR)aa_CharsetNames[i]) == 0)
            return i;
    return -2;
}

/* "8-16,18,20,24" -> sizes; FALSE if malformed */
BOOL mgr_ParseSizes(const char *s, struct AAInstall *in)
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
BOOL mgr_CheckFont(const struct AAInstall *in)
{
    struct TextAttr ta;
    struct TextFont *tf;
    char name[40];

    if (!DiskfontBase)
        return FALSE;
    strcpy(name, in->name);
    strcat(name, ".font");
    ta.ta_Name = (STRPTR)name;
    ta.ta_YSize = in->sizes[0];
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    if (!(tf = OpenDiskFont(&ta)))
        return FALSE;
    CloseFont(tf);
    return TRUE;
}

/*
 * REPAIR: check the .otag files in FONTS: and list what would be
 * repaired (moved font file, code page of CHARSET, engine freetype2 ->
 * ENGINE); with APPLY, do it (backups as <otag>.bak). FILES, if given,
 * are the font names to look at.
 */
static LONG Repair(STRPTR *names, LONG charset, const char *engine,
                   BOOL apply)
{
    struct AADiagEntry *e;
    struct AARepair r;
    char backup[AA_FONTFILE_LEN];
    LONG n, i, done = 0, todo = 0, rc = RETURN_OK;

    r.what = AA_REPAIR_PATH;
    r.charset = charset;
    r.engine = engine;
    if (charset >= 0)
        r.what |= AA_REPAIR_CODEPAGE;
    if (engine)
        r.what |= AA_REPAIR_ENGINE;

    if (!(e = AllocVec(sizeof(*e) * MAX_FONTS, MEMF_ANY | MEMF_CLEAR)))
        return RETURN_FAIL;
    n = aa_ScanFonts(e, MAX_FONTS);
    if (n < 0)
    {
        Printf("AATextManager: cannot read FONTS:\n");
        FreeVec(e);
        return RETURN_ERROR;
    }
    for (i = 0; i < n; i++)
    {
        ULONG what;
        STRPTR *p;

        if (names && *names)
        {
            for (p = names; *p; p++)
                if (!Stricmp_(*p, (CONST_STRPTR)e[i].name))
                    break;
            if (!*p)
                continue;
        }
        what = aa_RepairNeeded(&e[i], &r);
        if (!what)
            continue;
        todo++;
        Printf("%s:%s%s%s", (LONG)e[i].name,
               (LONG)((what & AA_REPAIR_PATH) ? " font file moved;" : ""),
               (LONG)((what & AA_REPAIR_CODEPAGE) ? " no code page;" : ""),
               (LONG)((what & AA_REPAIR_ENGINE) ?
                      " engine freetype2 -> " : ""));
        if (what & AA_REPAIR_ENGINE)
            Printf("%s\n", (LONG)engine);
        else
            Printf("\n");
        if (!apply)
            continue;
        switch (aa_RepairOTag(&e[i], &r, backup))
        {
        case AA_FIX_OK:
            Printf("  repaired (original: %s)\n", (LONG)backup);
            done++;
            break;
        case AA_FIX_BACKUP:
            Printf("  not repaired: cannot write %s\n", (LONG)backup);
            rc = RETURN_ERROR;
            break;
        case AA_FIX_WRITE:
            Printf("  cannot write the .otag; original in %s\n",
                   (LONG)backup);
            rc = RETURN_ERROR;
            break;
        default:
            Printf("  cannot read or rebuild the .otag\n");
            rc = RETURN_ERROR;
            break;
        }
    }
    if (!todo)
        Printf("Nothing to repair.\n");
    else if (!apply)
        Printf("%ld fonts to repair; APPLY repairs them (backups as "
               ".otag.bak).\n", todo);
    else
        Printf("%ld of %ld fonts repaired.\n", done, todo);
    FreeVec(e);
    return rc;
}

/* Without aatext.library: a requester when started from Workbench */
static void NoLibrary(BOOL wb)
{
    struct EasyStruct es;
    ULONG args[2];

    args[0] = (ULONG)AATEXTLIBNAME;
    args[1] = AATEXTLIBVERSION;
    if (!wb)
    {
        VPrintf((CONST_STRPTR)"AATextManager: needs %s %ld or newer "
                "(LIBS:)\n", (APTR)args);
        return;
    }
    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase)
        return;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"AATextManager";
    es.es_TextFormat = (UBYTE *)"AATextManager needs %s %ld or newer\nin LIBS:.";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequestArgs(NULL, &es, NULL, args);
    CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
}

int main(int argc, char **argv)
{
    LONG args[ARG_COUNT] = { 0 };
    struct RDArgs *rda = NULL;
    static struct AAInstall in;
    STRPTR *files;
    LONG rc = RETURN_OK, warn = RETURN_OK;
    LONG cs = 0;                    /* latin1, the Amiga default */

    (void)version;
    (void)argv;
    if (argc && !(rda = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL)))
    {
        PrintFault(IoErr(), (CONST_STRPTR)"AATextManager");
        return RETURN_FAIL;
    }
    AATextBase = OpenLibrary((CONST_STRPTR)AATEXTLIBNAME, AATEXTLIBVERSION);
    if (!AATextBase)
    {
        NoLibrary(!argc);
        if (rda)
            FreeArgs(rda);
        return RETURN_FAIL;
    }
    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);

    /* Workbench, or neither files nor REPAIR: the window */
    if (!argc || (!args[ARG_FILES] && !args[ARG_REPAIR]))
    {
        rc = mgr_RunGUI((const char *)args[ARG_LANGUAGE]);
        goto out;
    }

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
    if (args[ARG_SIZES] && !mgr_ParseSizes((const char *)args[ARG_SIZES], &in))
    {
        Printf("AATextManager: bad SIZES (example: 8-16,18,20,24)\n");
        rc = RETURN_ERROR;
    }

    if (rc == RETURN_OK && args[ARG_REPAIR])
        rc = Repair((STRPTR *)args[ARG_FILES], args[ARG_CHARSET] ? cs : -1,
                    (const char *)args[ARG_ENGINE], args[ARG_APPLY] != 0);
    else
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
            if (mgr_CheckFont(&in))
                Printf("  checked: diskfont opens %s.font %ld\n",
                       (LONG)in.name, (LONG)in.sizes[0]);
            else
                Printf("  WARNING: diskfont cannot open %s.font (is "
                       "%s.library installed, is the directory in "
                       "FONTS:?)\n", (LONG)in.name, (LONG)in.engine);
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

out:
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
    CloseLibrary(AATextBase);
    if (rda)
        FreeArgs(rda);
    return rc != RETURN_OK ? rc : warn;
}
