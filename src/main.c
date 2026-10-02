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

#include "patch.h"
#include "render.h"
#include "prefs.h"
#include "glyphs.h"
#include "debug.h"

#define AA_PORTNAME "AAText"

static const char version[] __attribute__((used)) =
    "$VER: AAText 0.4 (2.10.2026)";

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

    if (!aa_ReadPrefs(&prefs, have_prefspath ? prefspath : NULL, from_shell) &&
        mode == AA_MODE_TEXT)
    {
        Msg("AAText: no preferences file (ENV:AAText.prefs or "
            "ENVARC:AAText.prefs).\n");
        Cleanup();
        return RETURN_FAIL;
    }

    if (!aa_RenderInit())
    {
        Msg("AAText: out of memory.\n");
        Cleanup();
        return RETURN_FAIL;
    }

    numfonts = aa_GlyphsInit(&prefs, from_shell);
    if (numfonts == 0 && mode == AA_MODE_TEXT)
    {
        Msg("AAText: no usable font mappings, nothing to do.\n");
        Cleanup();
        return RETURN_FAIL;
    }
    aa_Mode = mode;

    port = CreateMsgPort();
    if (!port)
    {
        Cleanup();
        return RETURN_FAIL;
    }
    port->mp_Node.ln_Name = (char *)AA_PORTNAME;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);

    if (!aa_Install(GfxBase))
    {
        Msg("AAText: could not install the patch.\n");
        RemPort(port);
        DeleteMsgPort(port);
        Cleanup();
        return RETURN_FAIL;
    }

    {
        LONG info[3];

        info[0] = numfonts;
        info[1] = prefs.gamma100 / 100;
        info[2] = prefs.gamma100 % 100;
        MsgFmt("AAText installed: %ld font mapping(s), gamma %ld.%02ld. "
               "Ctrl-C or \"AAText QUIT\" removes it.\n", info);
    }

    for (;;)
    {
        Wait(SIGBREAKF_CTRL_C);
        if (aa_Remove())
            break;
        Msg("AAText: another program has patched Text() after AAText.\n"
            "Cannot remove safely; continuing in pass-through mode.\n"
            "Remove the other program first, then try again.\n");
    }

    RemPort(port);
    DeleteMsgPort(port);
    Cleanup();

    Msg("AAText removed.\n");
    return RETURN_OK;
}
