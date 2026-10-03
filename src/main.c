/*
 * AAText - antialiased Text() patch for AmigaOS 3.2
 *
 * Usage (Shell):  Run >NIL: AAText     install the patch
 *                 AAText QUIT          remove it again
 * Starting AAText while it is already running also removes it,
 * which makes it usable as a toggle from Workbench / WBStartup.
 *
 * Options:  PREFS=<file>               default ENV:AAText.prefs,
 *                                      then ENVARC:AAText.prefs
 *           TEST=TEXT|BOX|RPA|OFF      debug drawing modes (default TEXT)
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

#include "patch.h"
#include "render.h"
#include "prefs.h"
#include "glyphs.h"
#include "debug.h"

#define AA_PORTNAME "AAText"

static const char version[] __attribute__((used)) =
    "$VER: AAText 0.9 (3.10.2026)";

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

static struct AAPrefs prefs;
static BOOL from_shell;

/* Print to the Shell; silently does nothing when started from Workbench. */
static void Msg(const char *text)
{
    if (from_shell && Output())
        PutStr((CONST_STRPTR)text);
}

static void MsgFmt(const char *fmt, LONG *args)
{
    if (from_shell && Output())
        VPrintf((CONST_STRPTR)fmt, args);
}

/*
 * If an instance is running, send it Ctrl-C and return TRUE.
 * The signal is sent inside Forbid() so the task cannot go away
 * between FindPort() and Signal().
 */
static BOOL SignalRunningInstance(void)
{
    struct MsgPort *port;

    Forbid();
    port = FindPort((CONST_STRPTR)AA_PORTNAME);
    if (port && port->mp_SigTask)
        Signal(port->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();

    return port != NULL;
}

/* Case-insensitive compare of a TEST= value. */
static BOOL ArgIs(const char *arg, const char *name)
{
    while (*arg && *name)
    {
        char a = *arg++, n = *name++;

        if (a >= 'a' && a <= 'z')
            a -= 'a' - 'A';
        if (a != n)
            return FALSE;
    }
    return *arg == 0 && *name == 0;
}

/* A font (name as in the TextFont, size) to set up at startup. */
struct FontRef
{
    char  name[AA_PATH_LEN];
    UWORD ysize;
};

static struct FontRef refs[AA_MAX_FONTS];
static LONG numrefs;

static void AddRef(const char *name, UWORD ysize)
{
    LONG i, n;

    for (i = 0; i < numrefs; i++)
    {
        if (refs[i].ysize != ysize)
            continue;
        for (n = 0; name[n] && name[n] == refs[i].name[n]; n++)
            ;
        if (name[n] == refs[i].name[n])
            return;
    }
    if (numrefs == AA_MAX_FONTS)
        return;
    for (n = 0; n < AA_PATH_LEN - 1 && name[n]; n++)
        refs[numrefs].name[n] = name[n];
    refs[numrefs].name[n] = 0;
    refs[numrefs].ysize = ysize;
    numrefs++;
}

/*
 * Set up the fonts that matter right now, in our own process, so the
 * first Text() call (possibly from input.device) finds them ready:
 * every font currently open in the system (Workbench, screen, menu and
 * window fonts) plus the fonts mapped in the prefs. Detected fonts are
 * resolved via their .otag files; then each one is opened once and its
 * TrueType size prepared (real metrics fonts measure all 256 characters).
 */
static void SetupFonts(void)
{
    struct Library *DiskfontBase;
    struct Node *node;
    LONG i;

    /* fonts open in the system; copy names, the list may change later */
    Forbid();
    for (node = GfxBase->TextFonts.lh_Head; node->ln_Succ; node = node->ln_Succ)
    {
        struct TextFont *tf = (struct TextFont *)node;

        if (node->ln_Name)
            AddRef(node->ln_Name, tf->tf_YSize);
    }
    Permit();

    for (i = 0; i < aa_FontCount(); i++)
    {
        struct AAFont *font = aa_FontAt(i);
        char name[AA_NAME_LEN + 6];
        int n;

        for (n = 0; font->name[n]; n++)
            name[n] = font->name[n];
        CopyMem(".font", name + n, 6);
        AddRef(name, font->ysize);
    }

    for (i = 0; i < numrefs; i++)
        aa_RequestFont(refs[i].name);
    aa_ResolvePending(from_shell);

    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);
    if (!DiskfontBase)
        return;
    for (i = 0; i < numrefs; i++)
    {
        struct TextAttr ta;
        struct TextFont *tf;

        ta.ta_Name = (STRPTR)refs[i].name;
        ta.ta_YSize = refs[i].ysize;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        tf = OpenDiskFont(&ta);
        if (tf)
        {
            struct AAFont *font;

            if (tf->tf_YSize == refs[i].ysize && (font = aa_FindFont(tf)))
            {
                aa_LockGlyphs();
                aa_PrepareFont(font, tf);
                aa_UnlockGlyphs();
            }
            CloseFont(tf);
        }
    }
    CloseLibrary(DiskfontBase);
}

static void Cleanup(void)
{
    aa_GlyphsCleanup();
    aa_RenderCleanup();
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
}

int main(int argc, char **argv)
{
    struct MsgPort *port;
    LONG quit = FALSE;
    char prefspath[AA_PATH_LEN];
    BOOL have_prefspath = FALSE;
    UBYTE mode = AA_MODE_TEXT;
    LONG numfonts;
    BOOL anyreal;
    BYTE helpersig;
    LONG i;

    from_shell = (argc != 0);

    if (!(SysBase->AttnFlags & AFF_68020))
    {
        Msg("AAText requires a 68020 or better.\n");
        return RETURN_FAIL;
    }

    if (from_shell)
    {
        LONG args[3] = { 0, 0, 0 };
        struct RDArgs *rda = ReadArgs((CONST_STRPTR)"PREFS/K,TEST/K,QUIT/S",
                                      args, NULL);

        if (!rda)
        {
            PrintFault(IoErr(), (CONST_STRPTR)"AAText");
            return RETURN_FAIL;
        }
        if (args[0])
        {
            const char *p = (const char *)args[0];
            int i;

            for (i = 0; i < AA_PATH_LEN - 1 && p[i]; i++)
                prefspath[i] = p[i];
            prefspath[i] = 0;
            have_prefspath = TRUE;
        }
        if (args[1])
        {
            const char *t = (const char *)args[1];

            if (ArgIs(t, "TEXT"))
                mode = AA_MODE_TEXT;
            else if (ArgIs(t, "BOX"))
                mode = AA_MODE_BOX;
            else if (ArgIs(t, "RPA"))
                mode = AA_MODE_RPA;
            else if (ArgIs(t, "OFF"))
                mode = AA_MODE_OFF;
            else
            {
                Msg("AAText: TEST must be TEXT, BOX, RPA or OFF.\n");
                FreeArgs(rda);
                return RETURN_ERROR;
            }
        }
        quit = args[2];
        FreeArgs(rda);
    }

    if (SignalRunningInstance())
    {
        Msg("AAText: removal requested from the running instance.\n");
        return RETURN_OK;
    }
    if (quit)
    {
        Msg("AAText is not running.\n");
        return RETURN_WARN;
    }

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!GfxBase || !IntuitionBase)
    {
        Msg("AAText requires AmigaOS 3.0 (V39) or better.\n");
        Cleanup();
        return RETURN_FAIL;
    }

    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (!CyberGfxBase)
    {
        Msg("AAText: cybergraphics.library not found (P96 or CGX needed).\n");
        Cleanup();
        return RETURN_FAIL;
    }
    D(("AAText: cybergraphics.library %ld.%ld\n",
       (LONG)CyberGfxBase->lib_Version, (LONG)CyberGfxBase->lib_Revision));

    /* The prefs file is optional; without it the defaults apply. */
    if (!aa_ReadPrefs(&prefs, have_prefspath ? prefspath : NULL, from_shell) &&
        have_prefspath)
    {
        Msg("AAText: cannot read the PREFS file.\n");
        Cleanup();
        return RETURN_FAIL;
    }

    if (!aa_RenderInit(&prefs))
    {
        Msg("AAText: out of memory.\n");
        Cleanup();
        return RETURN_FAIL;
    }

    numfonts = aa_GlyphsInit(&prefs, from_shell);
    if (numfonts == 0 && !prefs.autodetect && mode == AA_MODE_TEXT)
    {
        Msg("AAText: no usable font mappings and \"auto off\", "
            "nothing to do.\n");
        Cleanup();
        return RETURN_FAIL;
    }
    aa_Mode = mode;

    /* helper signal: Text() asks us to load newly seen fonts */
    helpersig = AllocSignal(-1);
    if (helpersig < 0)
    {
        Cleanup();
        return RETURN_FAIL;
    }
    aa_SetHelper(FindTask(NULL), 1UL << helpersig);

    SetupFonts();

    /* the measuring functions are needed if any font may use real metrics */
    anyreal = prefs.autoreal;
    for (i = 0; i < prefs.nummaps; i++)
        if (prefs.map[i].real)
            anyreal = TRUE;
    anyreal = anyreal && mode == AA_MODE_TEXT;

    port = CreateMsgPort();
    if (!port)
    {
        Cleanup();
        return RETURN_FAIL;
    }
    port->mp_Node.ln_Name = (char *)AA_PORTNAME;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);

    if (!aa_Install(GfxBase, anyreal))
    {
        Msg("AAText: could not install the patch.\n");
        RemPort(port);
        DeleteMsgPort(port);
        Cleanup();
        return RETURN_FAIL;
    }

    {
        LONG info[6];

        info[0] = aa_FontCount();
        info[1] = (LONG)(prefs.autodetect ? "on" : "off");
        info[2] = prefs.gamma100 / 100;
        info[3] = prefs.gamma100 % 100;
        info[4] = prefs.cachekb;
        info[5] = prefs.numblack;
        MsgFmt("AAText installed: %ld font(s) ready, auto detection %s, "
               "gamma %ld.%02ld, cache %ld KB, %ld blacklisted.\n"
               "AAText: Ctrl-C or \"AAText QUIT\" removes it.\n", info);
    }

    for (;;)
    {
        ULONG sigs = Wait(SIGBREAKF_CTRL_C | (1UL << helpersig));

        /* fonts seen by Text() for the first time: load them now */
        if (sigs & (1UL << helpersig))
            aa_ResolvePending(FALSE);

        if (!(sigs & SIGBREAKF_CTRL_C))
            continue;
        if (aa_Remove())
            break;
        Msg("AAText: another program has patched Text() after AAText.\n"
            "Cannot remove safely; continuing in pass-through mode.\n"
            "Remove the other program first, then try again.\n");
    }

    aa_SetHelper(NULL, 0);
    FreeSignal(helpersig);
    RemPort(port);
    DeleteMsgPort(port);
    Cleanup();

    Msg("AAText removed.\n");
    return RETURN_OK;
}
