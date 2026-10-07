/*
 * AATextPrefs - preferences program for AAText (ReAction).
 *
 * Reads ENV:AAText.prefs (or ENVARC:), shows the settings on four tabs
 * and talks to a running AAText through its message port (aamsg.h):
 *
 *   Test    APPLY the shown settings without writing a file
 *   Use     write ENV:AAText.prefs, then RELOAD
 *   Save    write ENVARC: and ENV:AAText.prefs, then RELOAD
 *   Cancel  if Test was used, APPLY the settings AAText had before
 *
 * Shell options: LANGUAGE=<name> (e.g. türkçe) overrides the locale.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <graphics/text.h>
#include <utility/hooks.h>
#include <libraries/locale.h>

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

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/clicktab.h>
#include <gadgets/chooser.h>
#include <gadgets/slider.h>
#include <gadgets/checkbox.h>
#include <gadgets/button.h>
#include <gadgets/space.h>
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
    "$VER: AATextPrefs 0.1 (8.10.2026)";

#define ENV_PREFS     "ENV:AAText.prefs"
#define ENVARC_PREFS  "ENVARC:AAText.prefs"
#define REPLY_TICKS   250

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase, *DiskfontBase;
struct Library *WindowBase, *LayoutBase, *ClickTabBase, *ChooserBase,
               *SliderBase, *CheckBoxBase, *LabelBase, *ButtonBase,
               *SpaceBase;

enum
{
    GID_TABS = 1, GID_GAMMA, GID_GAMMAVAL, GID_HINTING, GID_REAL,
    GID_PREVIEW, GID_STATUS, GID_SAVE, GID_USE, GID_TEST, GID_CANCEL,
    GID_COUNT
};

/* chooser order -> AA_HINT_... value */
static const UBYTE hint_order[4] =
{
    AA_HINT_NORMAL, AA_HINT_LIGHT, AA_HINT_NONE, AA_HINT_FULL
};

static struct Gadget *gads[GID_COUNT];
static Object *winobj;
static struct Window *win;
static struct List tablist, hintlist;

static struct AAPrefs cur;      /* what the window shows */
static struct AAPrefs before;   /* AAText's settings when we started */
static BOOL running;            /* AAText answered STATUS */
static BOOL tested;             /* Test sent new settings to AAText */
static char statustext[160];
static char gammatext[8];

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
        &SpaceBase, &ButtonBase, &LabelBase, &CheckBoxBase, &SliderBase,
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

/* ------------------------------------------------------------------ */
/* Talking to AAText                                                   */
/* ------------------------------------------------------------------ */

static void SetStatus(const char *text)
{
    if (text != statustext)
        snprintf(statustext, sizeof(statustext), "%s", text);
    if (win)
        SetGadgetAttrs(gads[GID_STATUS], win, NULL,
                       GA_Text, (ULONG)statustext, TAG_DONE);
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
        RefreshGList(gads[GID_PREVIEW], win, NULL, 1);
}

/*
 * Read the settings back from the gadgets. The code value that comes
 * with WMHI_GADGETUP is not reliable for every event, so Test, Use and
 * Save always take the gadgets' current state.
 */
static void ReadGadgets(void)
{
    ULONG v = 0;

    if (GetAttr(SLIDER_Level, (Object *)gads[GID_GAMMA], &v))
        cur.gamma100 = v;
    if (GetAttr(CHOOSER_Selected, (Object *)gads[GID_HINTING], &v))
        cur.hinting = hint_order[v & 3];
    if (GetAttr(GA_Selected, (Object *)gads[GID_REAL], &v))
        cur.autoreal = v != 0;
}

static void UpdateGammaText(void)
{
    snprintf(gammatext, sizeof(gammatext), "%d.%02d",
             cur.gamma100 / 100, cur.gamma100 % 100);
    if (win)
        SetGadgetAttrs(gads[GID_GAMMAVAL], win, NULL,
                       GA_Text, (ULONG)gammatext, TAG_DONE);
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
}

static Object *PlaceholderPage(void)
{
    return VLayoutObject,
               LAYOUT_SpaceOuter, TRUE,
               LAYOUT_AddImage, LabelObject,
                   LABEL_Text, (ULONG)GetString(MSG_NOT_YET),
               End,
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
                SLIDER_Min, 50,
                SLIDER_Max, 400,
                SLIDER_Level, cur.gamma100,
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
        End,
    End;
}

static BOOL OpenWin(struct Screen *scr)
{
    AddTab(&tablist, MSG_TAB_APPEARANCE, 0);
    AddTab(&tablist, MSG_TAB_FONTS, 1);
    AddTab(&tablist, MSG_TAB_PROGRAMS, 2);
    AddTab(&tablist, MSG_TAB_ADVANCED, 3);
    AddChoice(&hintlist, MSG_HINT_NORMAL);
    AddChoice(&hintlist, MSG_HINT_LIGHT);
    AddChoice(&hintlist, MSG_HINT_NONE);
    AddChoice(&hintlist, MSG_HINT_FULL);

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
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VLayoutObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,

            LAYOUT_AddChild, gads[GID_TABS] = (struct Gadget *)ClickTabObject,
                GA_ID, GID_TABS,
                GA_RelVerify, TRUE,
                CLICKTAB_Labels, (ULONG)&tablist,
                CLICKTAB_Current, 0,
                CLICKTAB_PageGroup, PageObject,
                    PAGE_Add, AppearancePage(),
                    PAGE_Add, PlaceholderPage(),
                    PAGE_Add, PlaceholderPage(),
                    PAGE_Add, PlaceholderPage(),
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
                LAYOUT_AddChild, gads[GID_TEST] = (struct Gadget *)ButtonObject,
                    GA_ID, GID_TEST, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)GetString(MSG_TEST),
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

int main(int argc, char **argv)
{
    char language[AA_NAME_LEN] = "";
    struct Screen *scr = NULL;
    ULONG sigmask = 0;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    NewList(&tablist);      /* FreeLists() must work on every path */
    NewList(&hintlist);

    if (argc)
    {
        LONG args[1] = { 0 };
        struct RDArgs *rda = ReadArgs((CONST_STRPTR)"LANGUAGE/K", args, NULL);

        if (rda)
        {
            if (args[0])
                strncpy(language, (const char *)args[0], sizeof(language) - 1);
            FreeArgs(rda);
        }
    }

    if (!OpenLibs())
        goto out;
    InitStrings(language[0] ? language : NULL);

    aa_ReadPrefs(&cur, NULL, FALSE);   /* defaults if there is no file */
    CopyMem(&cur, &before, sizeof(before));
    UpdateGammaText();
    QueryAAText();

    scr = LockPubScreen(NULL);
    if (!scr)
        goto out;
    previewfont = OpenDiskFont(scr->Font);

    if (!OpenWin(scr))
        goto out;
    UnlockPubScreen(NULL, scr);
    scr = NULL;

    GetAttr(WINDOW_SigMask, winobj, &sigmask);
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
            switch (result & WMHI_CLASSMASK)
            {
                case WMHI_CLOSEWINDOW:
                    if (tested)
                        Send(AACMD_APPLY, &before);
                    done = TRUE;
                    break;

                case WMHI_GADGETUP:
                    switch (result & WMHI_GADGETMASK)
                    {
                        case GID_GAMMA:
                        case GID_HINTING:
                        case GID_REAL:
                            ReadGadgets();
                            UpdateGammaText();
                            break;

                        case GID_TEST:
                            ReadGadgets();
                            if (running)
                            {
                                Send(AACMD_APPLY, &cur);
                                tested = TRUE;
                                SetStatus(GetString(MSG_STATUS_TESTED));
                                RefreshPreview();
                            }
                            break;

                        case GID_USE:
                            ReadGadgets();
                            if (WritePrefsFile(ENV_PREFS, ENV_PREFS))
                            {
                                Send(AACMD_RELOAD, NULL);
                                done = TRUE;
                            }
                            break;

                        case GID_SAVE:
                            ReadGadgets();
                            /* ENVARC: keeps its own comments and mappings */
                            if (WritePrefsFile(ENVARC_PREFS, ENVARC_PREFS) &&
                                WritePrefsFile(ENV_PREFS, ENVARC_PREFS))
                            {
                                Send(AACMD_RELOAD, NULL);
                                done = TRUE;
                            }
                            break;

                        case GID_CANCEL:
                            if (tested)
                                Send(AACMD_APPLY, &before);
                            done = TRUE;
                            break;
                    }
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
