/*
 * AAText - antialiased Text() patch for AmigaOS 3.2
 *
 * Usage (Shell):  Run >NIL: AAText     install the patch
 *                 AAText QUIT          remove it again
 * Starting AAText while it is already running also removes it,
 * which makes it usable as a toggle from Workbench / WBStartup.
 *
 * Stage 2 test option:  TEST=BOX|ALPHA|RPA|OFF  (default BOX)
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
#include "cgx.h"
#include "debug.h"

#define AA_PORTNAME "AAText"

static const char version[] __attribute__((used)) =
    "$VER: AAText 0.2 (2.10.2026)";

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

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

static const char *ModeName(UBYTE mode)
{
    switch (mode)
    {
        case AA_TEST_BOX:   return "BOX";
        case AA_TEST_ALPHA: return "ALPHA";
        case AA_TEST_RPA:   return "RPA";
        default:            return "OFF";
    }
}

static void CloseLibs(void)
{
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
    UBYTE mode = AA_TEST_BOX;

    from_shell = (argc != 0);

    if (!(SysBase->AttnFlags & AFF_68020))
    {
        Msg("AAText requires a 68020 or better.\n");
        return RETURN_FAIL;
    }

    if (from_shell)
    {
        LONG args[2] = { 0, 0 };
        struct RDArgs *rda = ReadArgs((CONST_STRPTR)"TEST/K,QUIT/S", args, NULL);

        if (!rda)
        {
            PrintFault(IoErr(), (CONST_STRPTR)"AAText");
            return RETURN_FAIL;
        }
        if (args[0])
        {
            const char *t = (const char *)args[0];

            if (ArgIs(t, "BOX"))
                mode = AA_TEST_BOX;
            else if (ArgIs(t, "ALPHA"))
                mode = AA_TEST_ALPHA;
            else if (ArgIs(t, "RPA"))
                mode = AA_TEST_RPA;
            else if (ArgIs(t, "OFF"))
                mode = AA_TEST_OFF;
            else
            {
                Msg("AAText: TEST must be BOX, ALPHA, RPA or OFF.\n");
                FreeArgs(rda);
                return RETURN_ERROR;
            }
        }
        quit = args[1];
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
        CloseLibs();
        return RETURN_FAIL;
    }

    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (CyberGfxBase)
    {
        LONG info[4];

        info[0] = CyberGfxBase->lib_Version;
        info[1] = CyberGfxBase->lib_Revision;
        info[2] = (LONG)(aa_HasBltTemplateAlpha() ? "yes" : "no");
        info[3] = (LONG)(CGX_HAS_LVO(CyberGfxBase, LVO_WritePixelArrayAlpha)
                         ? "yes" : "no");
        MsgFmt("AAText: cybergraphics.library %ld.%ld, "
               "BltTemplateAlpha: %s, WritePixelArrayAlpha: %s\n", info);
        D(("AAText: cybergraphics.library %ld.%ld NegSize=%ld\n",
           (LONG)CyberGfxBase->lib_Version, (LONG)CyberGfxBase->lib_Revision,
           (LONG)CyberGfxBase->lib_NegSize));
    }
    else
    {
        Msg("AAText: cybergraphics.library not found, pass-through only.\n");
        mode = AA_TEST_OFF;
    }

    if (mode == AA_TEST_ALPHA && !aa_HasBltTemplateAlpha())
    {
        Msg("AAText: BltTemplateAlpha() not available, using TEST=BOX.\n");
        mode = AA_TEST_BOX;
    }
    aa_TestMode = mode;

    port = CreateMsgPort();
    if (!port)
    {
        CloseLibs();
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
        CloseLibs();
        return RETURN_FAIL;
    }

    {
        LONG info[1];

        info[0] = (LONG)ModeName(mode);
        MsgFmt("AAText installed (TEST=%s). "
               "Press Ctrl-C or run \"AAText QUIT\" to remove.\n", info);
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
    CloseLibs();

    Msg("AAText removed.\n");
    return RETURN_OK;
}
