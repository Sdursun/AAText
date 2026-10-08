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
 *           RELOAD                     running AAText re-reads its prefs
 *           STATUS                     shows the running AAText's state
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
#include "aamsg.h"
#include "aaclient.h"
#include "debug.h"

#include <string.h>

static const char version[] __attribute__((used)) =
    "$VER: AAText 0.12 (7.10.2026)";

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

static struct AAPrefs prefs;            /* the settings in effect */
static struct AAPrefs newprefs;         /* RELOAD/APPLY scratch */
static BOOL from_shell;
static char prefspath[AA_PATH_LEN];
static BOOL have_prefspath;

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

/* ------------------------------------------------------------------ */
/* Controller side: "AAText RELOAD" / "AAText STATUS"                   */
/* ------------------------------------------------------------------ */

/* Explain a failed aa_SendCommand(); TRUE if m is a reply. */
static BOOL ReplyOk(struct AAMessage *m)
{
    if (m == AACLIENT_NOTRUNNING)
        Msg("AAText is not running.\n");
    else if (m == AACLIENT_TIMEOUT)
        Msg("AAText: the running AAText does not answer "
            "(an older version?).\n");
    else
        return TRUE;
    return FALSE;
}

static int Controller(BOOL reload, BOOL status)
{
    struct AAMessage *m;

    if (reload)
    {
        m = aa_SendCommand(AACMD_RELOAD, have_prefspath ? prefspath : NULL,
                           NULL, 250);
        if (!ReplyOk(m))
            return RETURN_WARN;
        if (m->result == AARES_NOFILE)
            Msg("AAText: cannot read the prefs file.\n");
        else if (m->result == AARES_RESTART)
            Msg("AAText: settings applied; real metrics, kerning and font "
                "mappings change when AAText is restarted.\n");
        else
            Msg("AAText: settings applied.\n");
        aa_FreeReply(m);
    }
    if (status)
    {
        LONG args[13];

        m = aa_SendCommand(AACMD_STATUS, NULL, NULL, 250);
        if (!ReplyOk(m))
            return RETURN_WARN;
        args[0] = (LONG)m->versionstr;
        args[1] = (LONG)((m->flags & AASTAT_PASSTHROUGH) ? " (inactive)" : "");
        args[2] = m->numfaces;
        args[3] = m->numfonts;
        args[4] = (LONG)((m->flags & AASTAT_AUTODETECT) ? "on" : "off");
        args[5] = m->active.gamma100 / 100;
        args[6] = m->active.gamma100 % 100;
        args[7] = (LONG)(m->active.hinting == AA_HINT_NONE ? "none" :
                         m->active.hinting == AA_HINT_LIGHT ? "light" :
                         m->active.hinting == AA_HINT_FULL ? "full" : "normal");
        args[8] = (LONG)((m->flags & AASTAT_MEASURING) ? "on" : "off");
        args[9] = (LONG)(m->active.kerning ? "on" : "off");
        args[10] = m->cacheglyphs;
        args[11] = m->cachebytes / 1024;
        args[12] = m->active.cachekb;
        MsgFmt("%s%s\n"
               "  fonts: %ld file(s), %ld font size(s), auto detection %s\n"
               "  gamma %ld.%02ld, hinting %s, real metrics %s, kerning %s\n"
               "  glyph cache: %ld glyphs, %ld of %ld KB\n", args);
        aa_FreeReply(m);
    }
    return RETURN_OK;
}

/* ------------------------------------------------------------------ */
/* Server side: messages to the running AAText                         */
/* ------------------------------------------------------------------ */

static BOOL MappingsDiffer(const struct AAPrefs *a, const struct AAPrefs *b)
{
    LONG i;

    if (a->nummaps != b->nummaps)
        return TRUE;
    for (i = 0; i < a->nummaps; i++)
    {
        const struct AAMapping *x = &a->map[i], *y = &b->map[i];

        if (x->ysize != y->ysize || x->pixelsize != y->pixelsize ||
            x->real != y->real || strcmp(x->fontname, y->fontname) ||
            strcmp(x->ttfpath, y->ttfpath))
            return TRUE;
    }
    return FALSE;
}

/*
 * Apply new settings. Real metrics, kerning and font mappings are fixed
 * while AAText runs (the measuring patches, text widths and font tables
 * depend on them); they keep their values and AARES_RESTART tells the
 * caller.
 */
static LONG ApplyPrefs(const struct AAPrefs *np)
{
    LONG result = (np->autoreal != prefs.autoreal ||
                   np->kerning != prefs.kerning ||
                   MappingsDiffer(np, &prefs)) ? AARES_RESTART : AARES_OK;
    UWORD nummaps = prefs.nummaps;
    BOOL autoreal = prefs.autoreal, kerning = prefs.kerning;

    if (np != &newprefs)
        CopyMem((APTR)np, &newprefs, sizeof(newprefs));
    CopyMem(prefs.map, newprefs.map, sizeof(prefs.map));
    newprefs.nummaps = nummaps;
    newprefs.autoreal = autoreal;
    newprefs.kerning = kerning;
    CopyMem(&newprefs, &prefs, sizeof(prefs));

    aa_GlyphsReconfigure(&prefs);
    aa_RenderReconfigure(&prefs);
    return result;
}

static void HandleMessage(struct AAMessage *m)
{
    /* foreign or newer messages: answer without touching them */
    if (m->msg.mn_Length < sizeof(*m) || m->magic != AAMSG_MAGIC ||
        m->version != AAMSG_VERSION)
    {
        if (m->msg.mn_Length >= sizeof(*m) && m->magic == AAMSG_MAGIC)
            m->result = AARES_BADMSG;
        ReplyMsg(&m->msg);
        return;
    }

    switch (m->cmd)
    {
        case AACMD_RELOAD:
        {
            const char *path = m->path ? (const char *)m->path :
                               have_prefspath ? prefspath : NULL;

            /* without a file the defaults apply, as at startup */
            if (!aa_ReadPrefs(&newprefs, path, FALSE) && path)
                m->result = AARES_NOFILE;
            else
                m->result = ApplyPrefs(&newprefs);
            break;
        }

        case AACMD_APPLY:
            m->result = m->prefs ? ApplyPrefs(m->prefs) : AARES_BADMSG;
            break;

        case AACMD_STATUS:
            strncpy(m->versionstr, version + 6, sizeof(m->versionstr) - 1);
            m->flags = (prefs.autodetect ? AASTAT_AUTODETECT : 0) |
                       (aa_MeasuringPatched() ? AASTAT_MEASURING : 0) |
                       (aa_IsPassthrough() ? AASTAT_PASSTHROUGH : 0);
            aa_GlyphsStatus(&m->numfonts, &m->numfaces, &m->cachebytes,
                            &m->cacheglyphs);
            CopyMem(&prefs, &m->active, sizeof(prefs));
            m->result = AARES_OK;
            break;

        default:
            m->result = AARES_BADMSG;
            break;
    }
    ReplyMsg(&m->msg);
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
    LONG quit = FALSE, reload = FALSE, status = FALSE;
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
        LONG args[5] = { 0, 0, 0, 0, 0 };
        struct RDArgs *rda =
            ReadArgs((CONST_STRPTR)"PREFS/K,TEST/K,QUIT/S,RELOAD/S,STATUS/S",
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
        reload = args[3];
        status = args[4];
        FreeArgs(rda);
    }

    if (reload || status)
        return Controller(reload, status);

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
#ifdef DEBUG
    aa_TraceInfo();
#endif

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
        ULONG portsig = 1UL << port->mp_SigBit;
        ULONG sigs = Wait(SIGBREAKF_CTRL_C | (1UL << helpersig) | portsig);
        struct AAMessage *m;

        /* fonts seen by Text() for the first time: load them now */
        if (sigs & (1UL << helpersig))
            aa_ResolvePending(FALSE);

        /* commands from the prefs program, "AAText RELOAD" etc. */
        if (sigs & portsig)
            while ((m = (struct AAMessage *)GetMsg(port)))
                HandleMessage(m);

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
    {
        struct AAMessage *m;

        /* nobody may be left waiting for a reply */
        while ((m = (struct AAMessage *)GetMsg(port)))
            HandleMessage(m);
    }
    DeleteMsgPort(port);
    Cleanup();

    Msg("AAText removed.\n");
    return RETURN_OK;
}
