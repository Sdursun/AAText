/*
 * AAText - antialiased Text() patch for AmigaOS 3.2
 *
 * Usage (Shell):  Run >NIL: AAText     install the patch
 *                 AAText QUIT          remove it again
 * Starting AAText while it is already running also removes it,
 * which makes it usable as a toggle from Workbench / WBStartup.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "patch.h"
#include "debug.h"

#define AA_PORTNAME "AAText"

static const char version[] __attribute__((used)) =
    "$VER: AAText 0.1 (2.10.2026)";

struct GfxBase *GfxBase;

static BOOL from_shell;

/* Print to the Shell; silently does nothing when started from Workbench. */
static void Msg(const char *text)
{
    if (from_shell && Output())
        PutStr((CONST_STRPTR)text);
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

int main(int argc, char **argv)
{
    struct MsgPort *port;
    LONG quit = FALSE;

    from_shell = (argc != 0);

    if (!(SysBase->AttnFlags & AFF_68020))
    {
        Msg("AAText requires a 68020 or better.\n");
        return RETURN_FAIL;
    }

    if (from_shell)
    {
        LONG args[1] = { 0 };
        struct RDArgs *rda = ReadArgs((CONST_STRPTR)"QUIT/S", args, NULL);

        if (!rda)
        {
            PrintFault(IoErr(), (CONST_STRPTR)"AAText");
            return RETURN_FAIL;
        }
        quit = args[0];
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
    if (!GfxBase)
    {
        Msg("AAText requires graphics.library V39 or better.\n");
        return RETURN_FAIL;
    }

    port = CreateMsgPort();
    if (!port)
    {
        CloseLibrary((struct Library *)GfxBase);
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
        CloseLibrary((struct Library *)GfxBase);
        return RETURN_FAIL;
    }

    Msg("AAText installed. Press Ctrl-C or run \"AAText QUIT\" to remove.\n");

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
    CloseLibrary((struct Library *)GfxBase);

    Msg("AAText removed.\n");
    return RETURN_OK;
}
