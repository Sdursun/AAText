/*
 * AATextPrefs - preferences program for AAText (ReAction).
 *
 * Reads ENV:AAText.prefs (or ENVARC:), shows the settings on three tabs
 * and talks to a running AAText through its message port (aamsg.h):
 *
 *   change  gamma and hinting are APPLYed at once (live preview)
 *   Use     write ENV:AAText.prefs, then RELOAD
 *   Save    write ENVARC: and ENV:AAText.prefs, then RELOAD
 *   Cancel  if anything was applied, APPLY the settings AAText had before
 *
 * Shell options: LANGUAGE=<name> (e.g. türkçe) overrides the locale,
 * PUBSCREEN=<name> opens the window on that public screen.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <graphics/text.h>
#include <utility/hooks.h>
#include <libraries/locale.h>
#include <libraries/gadtools.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/clicktab.h>
#include <proto/chooser.h>
#include <proto/slider.h>
#include <proto/checkbox.h>
#include <proto/label.h>
#include <proto/button.h>
#include <proto/space.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/asl.h>
#include <libraries/asl.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/clicktab.h>
#include <gadgets/chooser.h>
#include <gadgets/slider.h>
#include <gadgets/checkbox.h>
#include <gadgets/button.h>
#include <gadgets/space.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <gadgets/integer.h>
#include <images/label.h>
#include <reaction/reaction_macros.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "../prefs.h"
#include "../prefswrite.h"
#include "../aaclient.h"
#include "strings.h"

static const char version[] __attribute__((used)) =
    "$VER: AATextPrefs 0.12 (7.10.2026)";

#define ENV_PREFS     "ENV:AAText.prefs"
#define ENVARC_PREFS  "ENVARC:AAText.prefs"
#define REPLY_TICKS   250

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase, *DiskfontBase;
struct Library *WindowBase, *LayoutBase, *ClickTabBase, *ChooserBase,
               *SliderBase, *CheckBoxBase, *LabelBase, *ButtonBase,
               *SpaceBase, *ListBrowserBase, *StringBase, *IntegerBase,
               *AslBase;

enum
{
    GID_TABS = 1, GID_GAMMA, GID_GAMMAVAL, GID_HINTING, GID_REAL,
    GID_PREVIEW, GID_FONTINFO, GID_STATUS, GID_SAVE, GID_USE, GID_CANCEL,
    GID_BLACKLIST, GID_PROGNAME, GID_ADD, GID_REMOVE, GID_RUNNING,
    GID_AUTO, GID_OFFSCREEN, GID_CACHE, GID_CACHEUSED, GID_CHARSET,
    GID_FTCODEPAGE, GID_PICKFILE, GID_KERNING,
    GID_COUNT
};

/* chooser order -> AA_HINT_... value */
static const UBYTE hint_order[4] =
{
    AA_HINT_NORMAL, AA_HINT_LIGHT, AA_HINT_NONE, AA_HINT_FULL
};

static struct Gadget *gads[GID_COUNT];
static Object *winobj, *pages;
static struct Window *win;
static struct List tablist, hintlist, blacklb, runlist, charsetlist;

static struct AAPrefs cur;      /* what the window shows */
static struct AAPrefs before;   /* AAText's settings when we started */
static struct AAPrefs orig;     /* the settings read at start (Restore) */
static BOOL running;            /* AAText answered STATUS */
static BOOL tested;             /* new settings were APPLYed to AAText */
static char statustext[160];
static char gammatext[8];
static char fontinfo[80];
static char cacheused[64];

#define MAX_RUNNING 64
static char runnames[MAX_RUNNING][AA_NAME_LEN];
static LONG numrunning;

static struct TextFont *previewfont;
static struct Hook previewhook;

/* ------------------------------------------------------------------ */

static BOOL OpenLibs(void)
{
    static const struct { struct Library **base; const char *name; } libs[] =
    {
        { (struct Library **)&IntuitionBase, "intuition.library" },
        { (struct Library **)&GfxBase,       "graphics.library" },
        { &UtilityBase,  "utility.library" },
        { &DiskfontBase, "diskfont.library" },
        { &WindowBase,   "window.class" },
        { &LayoutBase,   "gadgets/layout.gadget" },
        { &ClickTabBase, "gadgets/clicktab.gadget" },
        { &ChooserBase,  "gadgets/chooser.gadget" },
        { &SliderBase,   "gadgets/slider.gadget" },
        { &CheckBoxBase, "gadgets/checkbox.gadget" },
        { &LabelBase,    "images/label.image" },
        { &ButtonBase,   "gadgets/button.gadget" },
        { &SpaceBase,    "gadgets/space.gadget" },
        { &ListBrowserBase, "gadgets/listbrowser.gadget" },
        { &StringBase,   "gadgets/string.gadget" },
        { &IntegerBase,  "gadgets/integer.gadget" },
        { &AslBase,      "asl.library" },
    };
    ULONG i;

    for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
    {
        *libs[i].base = OpenLibrary((CONST_STRPTR)libs[i].name, 40);
        if (!*libs[i].base)
        {
            Printf((CONST_STRPTR)"AATextPrefs: cannot open %s\n",
                   (ULONG)libs[i].name);
            return FALSE;
        }
    }
    return TRUE;
}

static void CloseLibs(void)
{
    struct Library **bases[] =
    {
        &AslBase, &IntegerBase, &StringBase, &ListBrowserBase, &SpaceBase,
        &ButtonBase, &LabelBase, &CheckBoxBase, &SliderBase,
        &ChooserBase, &ClickTabBase, &LayoutBase, &WindowBase, &DiskfontBase,
        &UtilityBase, (struct Library **)&GfxBase,
        (struct Library **)&IntuitionBase
    };
    ULONG i;

    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++)
    {
        if (*bases[i])
            CloseLibrary(*bases[i]);
        *bases[i] = NULL;
    }
}

/* Tab page of a gadget (0-2), -1 for the gadgets outside the pages. */
static LONG PageOf(LONG gid)
{
    if ((gid >= GID_GAMMA && gid <= GID_FONTINFO) || gid == GID_KERNING)
        return 0;
    if ((gid >= GID_BLACKLIST && gid <= GID_RUNNING) || gid == GID_PICKFILE)
        return 1;
    if (gid >= GID_AUTO && gid <= GID_FTCODEPAGE)
        return 2;
    return -1;
}

static ULONG CurrentPage(void)
{
    ULONG page = 0;

    if (gads[GID_TABS])
        GetAttr(CLICKTAB_Current, (Object *)gads[GID_TABS], &page);
    return page;
}

/*
 * Change a gadget's attributes. Gadgets on a hidden tab page go through
 * the page object, so that they do not draw over the shown page; the
 * others are set directly and draw themselves.
 * No varargs: a tag list taken from the address of a stack argument
 * gave wrong values (garbage text in a button).
 */
static void SetGad2(LONG gid, Tag tag1, ULONG data1, Tag tag2, ULONG data2)
{
    struct TagItem tags[3];

    tags[0].ti_Tag = tag1;
    tags[0].ti_Data = data1;
    tags[1].ti_Tag = tag2;
    tags[1].ti_Data = data2;
    tags[2].ti_Tag = TAG_DONE;
    if (!win || !gads[gid])
        return;
    if (PageOf(gid) >= 0 && PageOf(gid) != (LONG)CurrentPage())
    {
        SetPageGadgetAttrsA(gads[gid], pages, win, NULL, tags);
        return;
    }
    SetGadgetAttrsA(gads[gid], win, NULL, tags);
    if (gid == GID_REAL || gid == GID_KERNING || gid == GID_AUTO ||
        gid == GID_OFFSCREEN)
    {
        /* checkbox.gadget does not redraw itself on GA_Selected; erase
           first, or its (antialiased) label is drawn over itself */
        struct Gadget *g = gads[gid];

        EraseRect(win->RPort, g->LeftEdge, g->TopEdge,
                  g->LeftEdge + g->Width - 1, g->TopEdge + g->Height - 1);
        RefreshGList(g, win, NULL, 1);
    }
}

static void SetGad(LONG gid, Tag tag, ULONG data)
{
    SetGad2(gid, tag, data, TAG_IGNORE, 0);
}

/* ------------------------------------------------------------------ */
/* Talking to AAText                                                   */
/* ------------------------------------------------------------------ */

static void SetStatus(const char *text)
{
    if (text != statustext)
        snprintf(statustext, sizeof(statustext), "%s", text);
    SetGad(GID_STATUS, GA_Text, (ULONG)statustext);
}

static void QueryAAText(void)
{
    struct AAMessage *m = aa_SendCommand(AACMD_STATUS, NULL, NULL,
                                         REPLY_TICKS);

    running = FALSE;
    if (m == AACLIENT_TIMEOUT)
        SetStatus(GetString(MSG_STATUS_OLD));
    else if (m == AACLIENT_NOTRUNNING)
        SetStatus(GetString(MSG_STATUS_STOPPED));
    else if (m->result != AARES_OK)
    {
        /* another protocol version: AAText answered without the status */
        SetStatus(GetString(MSG_STATUS_OLD));
        aa_FreeReply(m);
    }
    else
    {
        running = TRUE;
        CopyMem(&m->active, &before, sizeof(before));
        snprintf(statustext, sizeof(statustext),
                 GetString(MSG_STATUS_RUNNING), m->versionstr);
        SetStatus(statustext);
        aa_FreeReply(m);
    }
}

static void ShowRestartNote(void)
{
    struct EasyStruct es;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)GetString(MSG_RESTART_TITLE);
    es.es_TextFormat = (UBYTE *)GetString(MSG_RESTART_TEXT);
    es.es_GadgetFormat = (UBYTE *)GetString(MSG_OK);
    EasyRequestArgs(win, &es, NULL, NULL);
}

/* APPLY or RELOAD; shows the restart note if AAText asks for it. */
static void Send(UWORD cmd, const struct AAPrefs *p)
{
    struct AAMessage *m;

    if (!running)
        return;
    m = aa_SendCommand(cmd, NULL, p, REPLY_TICKS);
    if (m == AACLIENT_NOTRUNNING || m == AACLIENT_TIMEOUT)
    {
        running = FALSE;
        return;
    }
    if (m->result == AARES_RESTART && cmd != AACMD_APPLY)
        ShowRestartNote();
    aa_FreeReply(m);
}

static BOOL WritePrefsFile(const char *path, const char *template_path)
{
    char msg[160];

    if (aa_WritePrefs(&cur, path, template_path))
        return TRUE;
    snprintf(msg, sizeof(msg), GetString(MSG_WRITE_ERROR), path);
    SetStatus(msg);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */

/* Draw sample text with the screen font; AAText renders it. */
static ULONG PreviewRender(struct Hook *hook, Object *obj,
                           struct gpRender *gpr)
{
    struct RastPort *rp = gpr->gpr_RPort;
    struct IBox *box = NULL;
    struct DrawInfo *dri = gpr->gpr_GInfo ? gpr->gpr_GInfo->gi_DrInfo : NULL;
    LONG y;

    GetAttr(SPACE_AreaBox, obj, (ULONG *)&box);
    if (!rp || !box)
        return 0;

    SetAPen(rp, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
    RectFill(rp, box->Left, box->Top, box->Left + box->Width - 1,
             box->Top + box->Height - 1);
    if (previewfont)
        SetFont(rp, previewfont);
    SetAPen(rp, dri ? dri->dri_Pens[TEXTPEN] : 1);
    SetDrMd(rp, JAM1);

    y = box->Top + 4 + rp->TxBaseline;
    {
        const char *line1 = GetString(MSG_PREVIEW_LINE1);
        const char *line2 = GetString(MSG_PREVIEW_LINE2);

        Move(rp, box->Left + 8, y);
        Text(rp, (CONST_STRPTR)line1, strlen(line1));
        Move(rp, box->Left + 8, y + rp->TxHeight + 4);
        Text(rp, (CONST_STRPTR)line2, strlen(line2));
    }
    return 0;
}

static void RefreshPreview(void)
{
    if (win)
        RefreshPageGadget(gads[GID_PREVIEW], pages, win, NULL);
}

/*
 * The gamma slider moves in steps of 0.05 (level = gamma100 / 5). A value
 * from the file that is not on a step is kept until the slider is moved.
 */
#define GAMMA_STEP 5

static LONG GammaLevel(UWORD gamma100)
{
    return (gamma100 + GAMMA_STEP / 2) / GAMMA_STEP;
}

/*
 * Read the settings back from the gadgets. The code value that comes
 * with WMHI_GADGETUP is not reliable for every event, so the gadgets'
 * current state is always used.
 */
static void ReadGadgets(void)
{
    ULONG v = 0;

    if (GetAttr(SLIDER_Level, (Object *)gads[GID_GAMMA], &v) &&
        (LONG)v != GammaLevel(cur.gamma100))
        cur.gamma100 = v * GAMMA_STEP;
    if (GetAttr(CHOOSER_Selected, (Object *)gads[GID_HINTING], &v))
        cur.hinting = hint_order[v & 3];
    if (GetAttr(GA_Selected, (Object *)gads[GID_REAL], &v))
        cur.autoreal = v != 0;
    if (GetAttr(GA_Selected, (Object *)gads[GID_KERNING], &v))
        cur.kerning = v != 0;
    if (GetAttr(GA_Selected, (Object *)gads[GID_AUTO], &v))
        cur.autodetect = v != 0;
    if (GetAttr(GA_Selected, (Object *)gads[GID_OFFSCREEN], &v))
        cur.offscreen = v != 0;
    if (GetAttr(INTEGER_Number, (Object *)gads[GID_CACHE], &v))
        cur.cachekb = v;
    if (GetAttr(CHOOSER_Selected, (Object *)gads[GID_CHARSET], &v))
        cur.charset = v ? AA_CHARSET_LATIN5 : AA_CHARSET_LATIN1;
}

/* "In use: 5 KB, 52 characters" from AAText's STATUS */
static void UpdateCacheUsed(void)
{
    struct AAMessage *m = NULL;

    cacheused[0] = 0;
    if (running)
        m = aa_SendCommand(AACMD_STATUS, NULL, NULL, REPLY_TICKS);
    if (m != AACLIENT_NOTRUNNING && m != AACLIENT_TIMEOUT)
    {
        snprintf(cacheused, sizeof(cacheused), GetString(MSG_CACHE_USED),
                 (long)((m->cachebytes + 1023) / 1024), (long)m->cacheglyphs);
        aa_FreeReply(m);
    }
    if (win)
        SetGad(GID_CACHEUSED, GA_Text, (ULONG)cacheused);
}

static void UpdateGammaText(void)
{
    snprintf(gammatext, sizeof(gammatext), "%ld.%02ld",
             (long)(cur.gamma100 / 100), (long)(cur.gamma100 % 100));
    if (win)
        SetGad(GID_GAMMAVAL, GA_Text, (ULONG)gammatext);
}

/*
 * A setting changed: let AAText use it at once and redraw the preview.
 * Real widths need a restart, so AAText keeps its own value.
 */
static void LiveApply(void)
{
    struct AAPrefs p;

    ReadGadgets();
    UpdateGammaText();
    if (!running)
        return;
    CopyMem(&cur, &p, sizeof(p));
    p.autoreal = before.autoreal;
    p.kerning = before.kerning;
    Send(AACMD_APPLY, &p);
    if (!running)
    {
        SetStatus(GetString(MSG_STATUS_STOPPED));
        return;
    }
    tested = TRUE;
    SetStatus(GetString(MSG_STATUS_TESTED));
    RefreshPreview();
    UpdateCacheUsed();
}

/* ------------------------------------------------------------------ */
/* Programs tab                                                        */
/* ------------------------------------------------------------------ */

/* Show cur.blacklist in the list browser. */
static void ShowBlacklist(void)
{
    LONG i;

    if (win)
        SetGad(GID_BLACKLIST, LISTBROWSER_Labels, ~0UL);
    FreeListBrowserList(&blacklb);
    for (i = 0; i < cur.numblack; i++)
    {
        struct Node *n = AllocListBrowserNode(1,
                             LBNCA_Text, (ULONG)cur.blacklist[i], TAG_DONE);
        if (n)
            AddTail(&blacklb, n);
    }
    if (win)
        SetGad2(GID_BLACKLIST, LISTBROWSER_Labels, (ULONG)&blacklb,
                LISTBROWSER_Selected, (ULONG)-1);
}

static void AddProgram(const char *name)
{
    char buf[AA_NAME_LEN];
    LONG i, len;

    /* trim blanks; names are compared without case, as AAText does */
    while (*name == ' ' || *name == '\t')
        name++;
    snprintf(buf, sizeof(buf), "%s", name);
    for (len = strlen(buf); len > 0 && (buf[len - 1] == ' ' ||
                                        buf[len - 1] == '\t'); len--)
        buf[len - 1] = 0;
    if (!buf[0])
        return;
    for (i = 0; i < cur.numblack; i++)
        if (!Stricmp((CONST_STRPTR)cur.blacklist[i], (CONST_STRPTR)buf))
            return;
    if (cur.numblack == AA_MAX_BLACKLIST)
    {
        char msg[80];

        snprintf(msg, sizeof(msg), GetString(MSG_LIST_FULL),
                 (long)AA_MAX_BLACKLIST);
        SetStatus(msg);
        return;
    }
    strcpy(cur.blacklist[cur.numblack++], buf);
    ShowBlacklist();
    if (win)
        SetGad(GID_PROGNAME, STRINGA_TextVal, (ULONG)"");
    LiveApply();
}

/*
 * Choose a program file. Only its name is kept: AAText matches the
 * process name (Workbench start) or the command name (Shell) without
 * a path, so the program need not be running now.
 */
static void PickProgram(void)
{
    struct FileRequester *fr;

    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)win,
                             ASLFR_TitleText, (ULONG)GetString(MSG_PICK_TITLE),
                             ASLFR_InitialDrawer, (ULONG)"SYS:",
                             ASLFR_RejectIcons, TRUE,
                             ASLFR_SleepWindow, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
        AddProgram((const char *)fr->fr_File);
    FreeAslRequest(fr);
}

static void RemoveProgram(void)
{
    ULONG sel = ~0UL;
    LONG i;

    GetAttr(LISTBROWSER_Selected, (Object *)gads[GID_BLACKLIST], &sel);
    if ((LONG)sel < 0 || (LONG)sel >= cur.numblack)
        return;
    for (i = sel; i < cur.numblack - 1; i++)
        strcpy(cur.blacklist[i], cur.blacklist[i + 1]);
    cur.numblack--;
    ShowBlacklist();
    LiveApply();
}

/* Remember name (len chars) once, sorted, for the running list. */
static void AddRunning(const char *name, LONG len)
{
    char buf[AA_NAME_LEN];
    LONG i, j;

    if (len <= 0 || numrunning == MAX_RUNNING)
        return;
    if (len >= AA_NAME_LEN)
        len = AA_NAME_LEN - 1;
    CopyMem((APTR)name, buf, len);
    buf[len] = 0;
    for (i = 0; i < numrunning; i++)
    {
        LONG c = Stricmp((CONST_STRPTR)buf, (CONST_STRPTR)runnames[i]);

        if (c == 0)
            return;
        if (c < 0)
            break;
    }
    for (j = numrunning; j > i; j--)
        strcpy(runnames[j], runnames[j - 1]);
    strcpy(runnames[i], buf);
    numrunning++;
}

/*
 * The names AAText's blacklist can match: task names and, for Shell
 * processes, the running command (without its path).
 */
static void AddTaskNames(struct Task *t)
{
    const char *name = t->tc_Node.ln_Name;

    if (name)
        AddRunning(name, strlen(name));
    if (t->tc_Node.ln_Type == NT_PROCESS)
    {
        struct CommandLineInterface *cli =
            BADDR(((struct Process *)t)->pr_CLI);

        if (cli && cli->cli_Module && cli->cli_CommandName)
        {
            const UBYTE *b = BADDR(cli->cli_CommandName);
            const char *cmd = (const char *)b + 1;
            LONG len = b[0], i;

            for (i = len - 1; i >= 0; i--)
                if (cmd[i] == '/' || cmd[i] == ':')
                {
                    cmd += i + 1;
                    len -= i + 1;
                    break;
                }
            AddRunning(cmd, len);
        }
    }
}

/*
 * Leave out what nobody blacklists: file systems and handlers (DF0,
 * RAM, CON), library/device tasks and Shell process names.
 */
static BOOL IsProgramName(const char *name)
{
    static const char *const skip[] =
    {
        ".device", ".library", ".resource", ".gadget", ".class", ".image",
        NULL
    };
    static const char *const shells[] =
    {
        "Background CLI", "Shell Process", "Initial CLI", "New CLI",
        "AAText", "AATextPrefs", NULL
    };
    struct DosList *dl;
    LONG len = strlen(name), i;
    BOOL handler;

    for (i = 0; skip[i]; i++)
    {
        LONG n = strlen(skip[i]);

        if (len > n && !Stricmp((CONST_STRPTR)name + len - n,
                                (CONST_STRPTR)skip[i]))
            return FALSE;
    }
    for (i = 0; shells[i]; i++)
        if (!Stricmp((CONST_STRPTR)name, (CONST_STRPTR)shells[i]))
            return FALSE;

    dl = LockDosList(LDF_DEVICES | LDF_READ);
    handler = FindDosEntry(dl, (CONST_STRPTR)name, LDF_DEVICES) != NULL;
    UnLockDosList(LDF_DEVICES | LDF_READ);
    return !handler;
}

static void FillRunning(void)
{
    struct Node *n;
    LONG i, j;

    if (win)
        SetGad(GID_RUNNING, CHOOSER_Labels, ~0UL);
    while ((n = RemHead(&runlist)))
        FreeChooserNode(n);

    numrunning = 0;
    Forbid();
    AddTaskNames(FindTask(NULL));
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
        AddTaskNames((struct Task *)n);
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ; n = n->ln_Succ)
        AddTaskNames((struct Task *)n);
    Permit();

    /* DOS calls only now, after Permit() */
    for (i = j = 0; i < numrunning; i++)
        if (IsProgramName(runnames[i]))
        {
            if (i != j)
                strcpy(runnames[j], runnames[i]);
            j++;
        }
    numrunning = j;

    for (i = 0; i < numrunning; i++)
        if ((n = AllocChooserNode(CNA_Text, (ULONG)runnames[i], TAG_DONE)))
            AddTail(&runlist, n);
    if (win)
        SetGad(GID_RUNNING, CHOOSER_Labels, (ULONG)&runlist);
}

/* ------------------------------------------------------------------ */
/* Advanced tab                                                        */
/* ------------------------------------------------------------------ */

/* ISO-8859-9 -> Unicode, the format freetype2.library reads. */
static void MakeLatin5Page(UWORD *page)
{
    LONG i;

    for (i = 0; i < 256; i++)
        page[i] = i;
    page[0xD0] = 0x011E;
    page[0xDD] = 0x0130;
    page[0xDE] = 0x015E;
    page[0xF0] = 0x011F;
    page[0xFD] = 0x0131;
    page[0xFE] = 0x015F;
}

static BOOL WriteFile(const char *path, const void *data, LONG len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    BOOL ok;

    if (!fh)
        return FALSE;
    ok = Write(fh, (APTR)data, len) == len;
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

/* Write ENV: and ENVARC:ftcodepage, asking before replacing another one. */
static void WriteFtCodePage(void)
{
    UWORD page[256], old[256];
    LONG len;

    MakeLatin5Page(page);
    len = GetVar((CONST_STRPTR)"ftcodepage", (STRPTR)old, sizeof(old),
                 GVF_GLOBAL_ONLY | GVF_BINARY_VAR | GVF_DONT_NULL_TERM);
    if (len >= 0 && (len != (LONG)sizeof(old) ||
                     memcmp(old, page, sizeof(page))))
    {
        struct EasyStruct es;

        es.es_StructSize = sizeof(es);
        es.es_Flags = 0;
        es.es_Title = (UBYTE *)GetString(MSG_WINDOW_TITLE);
        es.es_TextFormat = (UBYTE *)GetString(MSG_FTCODEPAGE_ASK);
        es.es_GadgetFormat = (UBYTE *)GetString(MSG_REPLACE_CANCEL);
        if (EasyRequestArgs(win, &es, NULL, NULL) != 1)
            return;
    }

    if (!SetVar((CONST_STRPTR)"ftcodepage", (STRPTR)page, sizeof(page),
                GVF_GLOBAL_ONLY | GVF_BINARY_VAR))
    {
        SetStatus(GetString(MSG_FTCODEPAGE_FAIL));
        return;
    }
    /* AAText reads ENV:ftcodepage again when settings are applied */
    LiveApply();
    /* written by hand: SetVar(GVF_SAVE_VAR) reports success even when
       ENVARC: cannot be written (seen with ENVARC: not assigned) */
    if (WriteFile("ENVARC:ftcodepage", page, sizeof(page)))
        SetStatus(GetString(MSG_FTCODEPAGE_DONE));
    else
    {
        char msg[80];

        snprintf(msg, sizeof(msg), GetString(MSG_WRITE_ERROR),
                 "ENVARC:ftcodepage");
        SetStatus(msg);
    }
}

/* "Screen font: XEN 8", without the ".font" */
static void MakeFontInfo(const struct TextAttr *ta)
{
    char name[AA_NAME_LEN];
    char *dot;

    snprintf(name, sizeof(name), "%s", ta->ta_Name ? (char *)ta->ta_Name : "");
    if ((dot = strstr(name, ".font")))
        *dot = 0;
    snprintf(fontinfo, sizeof(fontinfo),
             GetString(running ? MSG_PREVIEW_FONT : MSG_PREVIEW_PLAIN),
             name, (long)ta->ta_YSize);
}

/* The key after '_' in a label, lower case; 0 if there is none. */
static ULONG ShortcutKey(LONG msg)
{
    const char *s = strchr(GetString(msg), '_');

    return s && s[1] ? ToLower((UBYTE)s[1]) : 0;
}

static void AddTab(struct List *list, LONG msg, ULONG number)
{
    struct Node *n = AllocClickTabNode(TNA_Text, (ULONG)GetString(msg),
                                       TNA_Number, number, TAG_DONE);
    if (n)
        AddTail(list, n);
}

static void AddChoice(struct List *list, LONG msg)
{
    struct Node *n = AllocChooserNode(CNA_Text, (ULONG)GetString(msg),
                                      TAG_DONE);
    if (n)
        AddTail(list, n);
}

static void FreeLists(void)
{
    struct Node *n;

    while ((n = RemHead(&tablist)))
        FreeClickTabNode(n);
    while ((n = RemHead(&hintlist)))
        FreeChooserNode(n);
    while ((n = RemHead(&runlist)))
        FreeChooserNode(n);
    while ((n = RemHead(&charsetlist)))
        FreeChooserNode(n);
    FreeListBrowserList(&blacklb);
}

static Object *ProgramsPage(void)
{
    LONG lineh = previewfont ? previewfont->tf_YSize : 8;

    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,

        LAYOUT_AddImage, LabelObject,
            LABEL_Text, (ULONG)GetString(MSG_PROGRAMS_INFO),
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, gads[GID_BLACKLIST] =
                         (struct Gadget *)ListBrowserObject,
            GA_ID, GID_BLACKLIST,
            GA_RelVerify, TRUE,
            LISTBROWSER_Labels, (ULONG)&blacklb,
            LISTBROWSER_ShowSelected, TRUE,
            LISTBROWSER_AutoFit, TRUE,
        End,
        CHILD_MinHeight, lineh * 6 + 8,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_PROGNAME] = (struct Gadget *)StringObject,
                GA_ID, GID_PROGNAME,
                GA_RelVerify, TRUE,
                GA_TabCycle, TRUE,
                STRINGA_MaxChars, AA_NAME_LEN - 1,
                STRINGA_TextVal, (ULONG)"",
            End,
            Label(GetString(MSG_PROGRAM)),
            /* even size: a short label like "Add" is not left too
               tight for fonts that measure narrower than they draw */
            LAYOUT_AddChild, HLayoutObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, gads[GID_ADD] = (struct Gadget *)ButtonObject,
                    GA_ID, GID_ADD, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)GetString(MSG_ADD),
                End,
                LAYOUT_AddChild, gads[GID_REMOVE] =
                                 (struct Gadget *)ButtonObject,
                    GA_ID, GID_REMOVE, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)GetString(MSG_REMOVE),
                End,
            End,
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_RUNNING] = (struct Gadget *)ChooserObject,
                GA_ID, GID_RUNNING,
                GA_RelVerify, TRUE,
                CHOOSER_DropDown, TRUE,
                CHOOSER_Title, (ULONG)GetString(MSG_RUNNING),
                CHOOSER_Labels, (ULONG)&runlist,
                CHOOSER_MaxLabels, MAX_RUNNING,
            End,
            LAYOUT_AddChild, gads[GID_PICKFILE] = (struct Gadget *)ButtonObject,
                GA_ID, GID_PICKFILE, GA_RelVerify, TRUE,
                GA_Text, (ULONG)GetString(MSG_PICK),
            End,
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
    End;
}

static Object *AdvancedPage(void)
{
    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,

        LAYOUT_AddChild, gads[GID_AUTO] = (struct Gadget *)CheckBoxObject,
            GA_ID, GID_AUTO,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)GetString(MSG_AUTODETECT),
            GA_Selected, cur.autodetect,
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_OFFSCREEN] = (struct Gadget *)CheckBoxObject,
            GA_ID, GID_OFFSCREEN,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)GetString(MSG_OFFSCREEN),
            GA_Selected, cur.offscreen,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_CACHE] = (struct Gadget *)IntegerObject,
                GA_ID, GID_CACHE,
                GA_RelVerify, TRUE,
                GA_TabCycle, TRUE,
                INTEGER_Minimum, 32,
                INTEGER_Maximum, 16384,
                INTEGER_Number, cur.cachekb,
                INTEGER_Arrows, TRUE,
            End,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth, 100,
            LAYOUT_AddChild, gads[GID_CACHEUSED] = (struct Gadget *)ButtonObject,
                GA_ID, GID_CACHEUSED,
                GA_ReadOnly, TRUE,
                GA_Text, (ULONG)cacheused,
                BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Justification, BCJ_LEFT,
            End,
        End,
        CHILD_WeightedHeight, 0,
        Label(GetString(MSG_CACHE)),

        LAYOUT_AddChild, gads[GID_CHARSET] = (struct Gadget *)ChooserObject,
            GA_ID, GID_CHARSET,
            GA_RelVerify, TRUE,
            CHOOSER_PopUp, TRUE,
            CHOOSER_Labels, (ULONG)&charsetlist,
            CHOOSER_Selected, cur.charset == AA_CHARSET_LATIN5 ? 1 : 0,
        End,
        CHILD_WeightedHeight, 0,
        Label(GetString(MSG_CHARSET)),
        LAYOUT_AddImage, LabelObject,
            LABEL_Text, (ULONG)GetString(MSG_CHARSET_NOTE),
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_FTCODEPAGE] =
                             (struct Gadget *)ButtonObject,
                GA_ID, GID_FTCODEPAGE, GA_RelVerify, TRUE,
                GA_Text, (ULONG)GetString(MSG_FTCODEPAGE),
            End,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, SpaceObject, End,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, SpaceObject, End,     /* takes the spare height */
    End;
}

static LONG HintIndex(UBYTE hinting)
{
    LONG i;

    for (i = 0; i < 4; i++)
        if (hint_order[i] == hinting)
            return i;
    return 0;
}

static Object *AppearancePage(void)
{
    LONG previewh = (previewfont ? previewfont->tf_YSize : 8) * 2 + 16;

    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_GAMMA] = (struct Gadget *)SliderObject,
                GA_ID, GID_GAMMA,
                GA_RelVerify, TRUE,
                SLIDER_Min, 50 / GAMMA_STEP,
                SLIDER_Max, 400 / GAMMA_STEP,
                SLIDER_Level, GammaLevel(cur.gamma100),
                SLIDER_Orientation, SLIDER_HORIZONTAL,
            End,
            LAYOUT_AddChild, gads[GID_GAMMAVAL] = (struct Gadget *)ButtonObject,
                GA_ID, GID_GAMMAVAL,
                GA_ReadOnly, TRUE,
                GA_Text, (ULONG)gammatext,
                BUTTON_BevelStyle, BVS_NONE,
            End,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth, 48,
        End,
        Label(GetString(MSG_GAMMA)),

        LAYOUT_AddChild, gads[GID_HINTING] = (struct Gadget *)ChooserObject,
            GA_ID, GID_HINTING,
            GA_RelVerify, TRUE,
            CHOOSER_PopUp, TRUE,
            CHOOSER_Labels, (ULONG)&hintlist,
            CHOOSER_Selected, HintIndex(cur.hinting),
        End,
        Label(GetString(MSG_HINTING)),

        LAYOUT_AddChild, gads[GID_REAL] = (struct Gadget *)CheckBoxObject,
            GA_ID, GID_REAL,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)GetString(MSG_REAL),
            GA_Selected, cur.autoreal,
        End,
        LAYOUT_AddChild, gads[GID_KERNING] = (struct Gadget *)CheckBoxObject,
            GA_ID, GID_KERNING,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)GetString(MSG_KERNING),
            GA_Selected, cur.kerning,
        End,
        LAYOUT_AddImage, LabelObject,
            LABEL_Text, (ULONG)GetString(MSG_REAL_NOTE),
        End,

        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, (ULONG)GetString(MSG_PREVIEW),
            LAYOUT_AddChild, gads[GID_PREVIEW] = (struct Gadget *)SpaceObject,
                GA_ID, GID_PREVIEW,
                SPACE_MinHeight, previewh,
                SPACE_MinWidth, 360,
                SPACE_RenderHook, (ULONG)&previewhook,
            End,
            LAYOUT_AddChild, gads[GID_FONTINFO] = (struct Gadget *)ButtonObject,
                GA_ID, GID_FONTINFO,
                GA_ReadOnly, TRUE,
                GA_Text, (ULONG)fontinfo,
                BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Justification, BCJ_LEFT,
            End,
            CHILD_WeightedHeight, 0,
        End,
    End;
}

enum
{
    MENU_OPEN = 1, MENU_SAVEAS, MENU_QUIT, MENU_DEFAULTS, MENU_LASTSAVED,
    MENU_RESTORE
};

/* The standard Prefs menus; labels are filled in by OpenWin(). */
static struct NewMenu menu[] =
{
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "O",  0, 0, (APTR)MENU_OPEN },
    { NM_ITEM,  NULL, "A",  0, 0, (APTR)MENU_SAVEAS },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "Q",  0, 0, (APTR)MENU_QUIT },
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "D",  0, 0, (APTR)MENU_DEFAULTS },
    { NM_ITEM,  NULL, "L",  0, 0, (APTR)MENU_LASTSAVED },
    { NM_ITEM,  NULL, "R",  0, 0, (APTR)MENU_RESTORE },
    { NM_END,   NULL, NULL, 0, 0, NULL }
};

static void LocalizeMenu(void)
{
    static const LONG labels[] =
    {
        MSG_MENU_PROJECT, MSG_MENU_OPEN, MSG_MENU_SAVEAS, 0, MSG_MENU_QUIT,
        MSG_MENU_EDIT, MSG_MENU_DEFAULTS, MSG_MENU_LASTSAVED,
        MSG_MENU_RESTORE
    };
    ULONG i;

    for (i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
        if (labels[i])
            menu[i].nm_Label = (CONST_STRPTR)GetString(labels[i]);
}

static BOOL OpenWin(struct Screen *scr)
{
    LocalizeMenu();
    AddTab(&tablist, MSG_TAB_APPEARANCE, 0);
    AddTab(&tablist, MSG_TAB_PROGRAMS, 1);
    AddTab(&tablist, MSG_TAB_ADVANCED, 2);
    AddChoice(&hintlist, MSG_HINT_NORMAL);
    AddChoice(&hintlist, MSG_HINT_LIGHT);
    AddChoice(&hintlist, MSG_HINT_NONE);
    AddChoice(&hintlist, MSG_HINT_FULL);
    AddChoice(&charsetlist, MSG_CHARSET_LATIN1);
    AddChoice(&charsetlist, MSG_CHARSET_LATIN5);
    ShowBlacklist();
    FillRunning();

    previewhook.h_Entry = (HOOKFUNC)(APTR)HookEntry;
    previewhook.h_SubEntry = (HOOKFUNC)(APTR)PreviewRender;

    winobj = WindowObject,
        WA_Title, (ULONG)GetString(MSG_WINDOW_TITLE),
        WA_ScreenTitle, (ULONG)GetString(MSG_WINDOW_TITLE),
        WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY |
                  IDCMP_MENUPICK,
        WINDOW_NewMenu, (ULONG)menu,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VLayoutObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,

            LAYOUT_AddChild, gads[GID_TABS] = (struct Gadget *)ClickTabObject,
                GA_ID, GID_TABS,
                GA_RelVerify, TRUE,
                CLICKTAB_Labels, (ULONG)&tablist,
                CLICKTAB_Current, 0,
                CLICKTAB_PageGroup, pages = PageObject,
                    PAGE_Add, AppearancePage(),
                    PAGE_Add, ProgramsPage(),
                    PAGE_Add, AdvancedPage(),
                End,
            End,

            LAYOUT_AddChild, gads[GID_STATUS] = (struct Gadget *)ButtonObject,
                GA_ID, GID_STATUS,
                GA_ReadOnly, TRUE,
                GA_Text, (ULONG)statustext,
                BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Justification, BCJ_LEFT,
            End,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HLayoutObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, gads[GID_SAVE] = (struct Gadget *)ButtonObject,
                    GA_ID, GID_SAVE, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)GetString(MSG_SAVE),
                End,
                LAYOUT_AddChild, gads[GID_USE] = (struct Gadget *)ButtonObject,
                    GA_ID, GID_USE, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)GetString(MSG_USE),
                End,
                LAYOUT_AddChild, gads[GID_CANCEL] = (struct Gadget *)ButtonObject,
                    GA_ID, GID_CANCEL, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)GetString(MSG_CANCEL),
                End,
            End,
            CHILD_WeightedHeight, 0,
        End,
    End;

    if (!winobj)
        return FALSE;
    win = (struct Window *)DoMethod(winobj, WM_OPEN, NULL);
    return win != NULL;
}

/* ------------------------------------------------------------------ */

/* A button or its key; TRUE when the window is to close. */
static BOOL Action(ULONG id)
{
    switch (id)
    {
        case GID_GAMMA:
        case GID_HINTING:
            LiveApply();
            break;

        case GID_REAL:
        case GID_KERNING:
            ReadGadgets();
            break;

        case GID_AUTO:
        case GID_OFFSCREEN:
        case GID_CACHE:
        case GID_CHARSET:
            LiveApply();
            break;

        case GID_TABS:
        {
            ULONG page = 0;

            GetAttr(CLICKTAB_Current, (Object *)gads[GID_TABS], &page);
            if (page == 1)
                FillRunning();
            else if (page == 2)
                UpdateCacheUsed();
            break;
        }

        case GID_ADD:
        {
            STRPTR text = NULL;

            GetAttr(STRINGA_TextVal, (Object *)gads[GID_PROGNAME],
                    (ULONG *)&text);
            if (text)
                AddProgram((const char *)text);
            break;
        }

        case GID_PICKFILE:
            PickProgram();
            break;

        case GID_REMOVE:
            RemoveProgram();
            break;

        case GID_RUNNING:
        {
            ULONG sel = ~0UL;

            GetAttr(CHOOSER_Selected, (Object *)gads[GID_RUNNING], &sel);
            if ((LONG)sel >= 0 && (LONG)sel < numrunning)
                AddProgram(runnames[sel]);
            break;
        }

        case GID_FTCODEPAGE:
            WriteFtCodePage();
            break;

        case GID_USE:
            ReadGadgets();
            if (WritePrefsFile(ENV_PREFS, ENV_PREFS))
            {
                Send(AACMD_RELOAD, NULL);
                return TRUE;
            }
            break;

        case GID_SAVE:
            ReadGadgets();
            /* ENVARC: keeps its own comments and mappings */
            if (WritePrefsFile(ENVARC_PREFS, ENVARC_PREFS) &&
                WritePrefsFile(ENV_PREFS, ENVARC_PREFS))
            {
                Send(AACMD_RELOAD, NULL);
                return TRUE;
            }
            break;

        case GID_CANCEL:
            if (tested)
                Send(AACMD_APPLY, &before);
            return TRUE;
    }
    return FALSE;
}

/* Show cur in every gadget and let AAText use it. */
static void ShowPrefs(void)
{
    SetGad(GID_GAMMA, SLIDER_Level, GammaLevel(cur.gamma100));
    SetGad(GID_HINTING, CHOOSER_Selected, HintIndex(cur.hinting));
    SetGad(GID_REAL, GA_Selected, cur.autoreal);
    SetGad(GID_KERNING, GA_Selected, cur.kerning);
    SetGad(GID_AUTO, GA_Selected, cur.autodetect);
    SetGad(GID_OFFSCREEN, GA_Selected, cur.offscreen);
    SetGad(GID_CACHE, INTEGER_Number, cur.cachekb);
    SetGad(GID_CHARSET, CHOOSER_Selected,
           cur.charset == AA_CHARSET_LATIN5 ? 1 : 0);
    ShowBlacklist();
    LiveApply();
}

/* Read a prefs file into the window. */
static void LoadPrefs(const char *path)
{
    struct AAPrefs p;

    if (aa_ReadPrefs(&p, path, FALSE))
    {
        CopyMem(&p, &cur, sizeof(cur));
        ShowPrefs();
    }
    else
    {
        char msg[160];

        snprintf(msg, sizeof(msg), GetString(MSG_READ_ERROR), path);
        SetStatus(msg);
    }
}

/* ASL requester for Open / Save As; TRUE with the full path in buf. */
static BOOL AskFile(LONG title, BOOL save, char *buf, LONG len)
{
    struct FileRequester *fr;
    const char *drawer = "SYS:Prefs/Presets";
    BPTR lock = Lock((CONST_STRPTR)drawer, ACCESS_READ);
    BOOL ok = FALSE;

    if (lock)
        UnLock(lock);
    else
        drawer = "SYS:";
    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)win,
                             ASLFR_TitleText, (ULONG)GetString(title),
                             ASLFR_InitialDrawer, (ULONG)drawer,
                             ASLFR_InitialFile, (ULONG)"AAText.prefs",
                             ASLFR_DoSaveMode, save,
                             ASLFR_RejectIcons, TRUE,
                             ASLFR_SleepWindow, TRUE,
                             TAG_DONE);
    if (!fr)
        return FALSE;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
    {
        snprintf(buf, len, "%s", fr->fr_Drawer ? (char *)fr->fr_Drawer : "");
        ok = AddPart((STRPTR)buf, fr->fr_File, len);
    }
    FreeAslRequest(fr);
    return ok;
}

/* A menu item; TRUE when the window is to close. */
static BOOL MenuAction(ULONG id)
{
    char path[AA_PATH_LEN];

    switch (id)
    {
        case MENU_OPEN:
            if (AskFile(MSG_OPEN_TITLE, FALSE, path, sizeof(path)))
                LoadPrefs(path);
            break;

        case MENU_SAVEAS:
            ReadGadgets();
            /* comments and font mappings come from the file in use */
            if (AskFile(MSG_SAVEAS_TITLE, TRUE, path, sizeof(path)))
                WritePrefsFile(path, ENV_PREFS);
            break;

        case MENU_QUIT:
            return Action(GID_CANCEL);

        case MENU_DEFAULTS:
            aa_DefaultPrefs(&cur);
            ShowPrefs();
            break;

        case MENU_LASTSAVED:
            LoadPrefs(ENVARC_PREFS);
            break;

        case MENU_RESTORE:
            CopyMem(&orig, &cur, sizeof(cur));
            ShowPrefs();
            break;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    char language[AA_NAME_LEN] = "", pubname[MAXPUBSCREENNAME + 1] = "";
    struct Screen *scr = NULL;
    ULONG sigmask = 0, keys[3];
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    NewList(&tablist);      /* FreeLists() must work on every path */
    NewList(&hintlist);
    NewList(&blacklb);
    NewList(&runlist);
    NewList(&charsetlist);

    if (argc)
    {
        LONG args[2] = { 0, 0 };
        struct RDArgs *rda = ReadArgs((CONST_STRPTR)"LANGUAGE/K,PUBSCREEN/K",
                                      args, NULL);

        if (rda)
        {
            if (args[0])
                strncpy(language, (const char *)args[0], sizeof(language) - 1);
            if (args[1])
                strncpy(pubname, (const char *)args[1], sizeof(pubname) - 1);
            FreeArgs(rda);
        }
    }

    if (!OpenLibs())
        goto out;
    InitStrings(language[0] ? language : NULL);

    aa_ReadPrefs(&cur, NULL, FALSE);   /* defaults if there is no file */
    CopyMem(&cur, &before, sizeof(before));
    CopyMem(&cur, &orig, sizeof(orig));
    UpdateGammaText();
    QueryAAText();
    UpdateCacheUsed();

    /* the named screen, else the default one (as Prefs programs do) */
    if (pubname[0])
        scr = LockPubScreen((CONST_STRPTR)pubname);
    if (!scr)
        scr = LockPubScreen(NULL);
    if (!scr)
        goto out;
    previewfont = OpenDiskFont(scr->Font);
    MakeFontInfo(scr->Font);

    if (!OpenWin(scr))
        goto out;
    UnlockPubScreen(NULL, scr);
    scr = NULL;

    GetAttr(WINDOW_SigMask, winobj, &sigmask);
    keys[0] = ShortcutKey(MSG_SAVE);
    keys[1] = ShortcutKey(MSG_USE);
    keys[2] = ShortcutKey(MSG_CANCEL);
    while (!done)
    {
        ULONG sigs = Wait(sigmask | SIGBREAKF_CTRL_C);
        ULONG result;
        UWORD code;

        if (sigs & SIGBREAKF_CTRL_C)
            break;
        while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) !=
               WMHI_LASTMSG)
        {
            ULONG key;

            switch (result & WMHI_CLASSMASK)
            {
                case WMHI_CLOSEWINDOW:
                    done |= Action(GID_CANCEL);
                    break;

                case WMHI_GADGETUP:
                    done |= Action(result & WMHI_GADGETMASK);
                    break;

                case WMHI_MENUPICK:
                {
                    UWORD mc = result & WMHI_MENUMASK;

                    while (mc != MENUNULL && !done)
                    {
                        struct MenuItem *item = ItemAddress(win->MenuStrip,
                                                            mc);
                        if (!item)
                            break;
                        done |= MenuAction((ULONG)GTMENUITEM_USERDATA(item));
                        mc = item->NextSelect;
                    }
                    break;
                }

                case WMHI_VANILLAKEY:
                    key = ToLower(result & WMHI_KEYMASK);
                    if (key == 0x1b)        /* Esc */
                        done |= Action(GID_CANCEL);
                    else if (key && key == keys[0])
                        done |= Action(GID_SAVE);
                    else if (key && key == keys[1])
                        done |= Action(GID_USE);
                    else if (key && key == keys[2])
                        done |= Action(GID_CANCEL);
                    break;
            }
        }
    }
    rc = RETURN_OK;

out:
    if (winobj)
        DisposeObject(winobj);
    FreeLists();
    if (previewfont)
        CloseFont(previewfont);
    if (scr)
        UnlockPubScreen(NULL, scr);
    FreeStrings();
    CloseLibs();
    return rc;
}
