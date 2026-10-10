/*
 * AATextManager - the window (ReAction).
 *
 * Above the tabs: character set (the code page written into the .otag)
 * and engine, used by both tabs.
 *   Install  font files picked with the file requester (a .ttc gives one
 *            line per face), sizes, "replace", Install
 *   Repair   Check fonts lists the .otag files in FONTS: that can be
 *            repaired; Repair asks once, then repairs them all
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <graphics/text.h>
#include <libraries/asl.h>
#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/clicktab.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/label.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
#include <proto/string.h>
#include <proto/asl.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/clicktab.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/space.h>
#include <utility/hooks.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <reaction/reaction_macros.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "manager.h"
#include "strings.h"
#include "preview.h"
#include "../fontscan.h"
#include "../charsets.h"
#include "../prefs.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase;
struct Library *WindowBase, *LayoutBase, *ClickTabBase, *ChooserBase,
               *CheckBoxBase, *LabelBase, *ButtonBase, *ListBrowserBase,
               *StringBase, *AslBase, *SpaceBase;

enum
{
    GID_TABS = 1, GID_CHARSET, GID_ENGINE,
    GID_FILES, GID_PREVIEW, GID_ADD, GID_REMOVE, GID_SIZES, GID_OVERWRITE, GID_INSTALL,
    GID_CHECK, GID_REPAIRLIST, GID_CHANGEENGINE, GID_REPAIR,
    GID_STATUS,
    GID_COUNT
};

#define MAX_FILES  64
#define MAX_FONTS  1024

struct FileEntry
{
    char  path[256];
    LONG  face;
    char  facetext[8];
    struct AAFontInfo info;
};

static struct Gadget *gads[GID_COUNT];
static Object *winobj, *pages;
static struct Window *win;
static struct List tablist, charsetlist, enginelist, filelb, repairlb;
static struct FileEntry *files;
static LONG numfiles;
static struct AADiagEntry *diag;
static LONG numdiag;
static char statustext[200];
static char sizestext[80] = "8-16,18,20,24";

static const char *const engines[2] = { "aatext", "freetype2" };

static const LONG charset_msg[AA_NUM_CHARSETS] =
{
    MSG_CHARSET_LATIN1, MSG_CHARSET_LATIN2, MSG_CHARSET_LATIN3,
    MSG_CHARSET_LATIN4, MSG_CHARSET_LATIN5, MSG_CHARSET_LATIN9,
    MSG_CHARSET_LATIN10, MSG_CHARSET_CP1250, MSG_CHARSET_CYRILLIC,
    MSG_CHARSET_KOI8R
};

static struct ColumnInfo filecols[] =
{
    { 30, NULL, 0 }, { 20, NULL, 0 }, { 12, NULL, 0 }, { 38, NULL, 0 },
    { -1, NULL, 0 }
};

static struct ColumnInfo repaircols[] =
{
    { 40, NULL, 0 }, { 60, NULL, 0 }, { -1, NULL, 0 }
};

/* ------------------------------------------------------------------ */

static BOOL OpenLibs(void)
{
    static const struct { struct Library **base; const char *name; } libs[] =
    {
        { (struct Library **)&IntuitionBase, "intuition.library" },
        { (struct Library **)&GfxBase,       "graphics.library" },
        { &UtilityBase,  "utility.library" },
        { &WindowBase,   "window.class" },
        { &LayoutBase,   "gadgets/layout.gadget" },
        { &ClickTabBase, "gadgets/clicktab.gadget" },
        { &ChooserBase,  "gadgets/chooser.gadget" },
        { &CheckBoxBase, "gadgets/checkbox.gadget" },
        { &LabelBase,    "images/label.image" },
        { &ButtonBase,   "gadgets/button.gadget" },
        { &ListBrowserBase, "gadgets/listbrowser.gadget" },
        { &StringBase,   "gadgets/string.gadget" },
        { &AslBase,      "asl.library" },
        { &SpaceBase,    "gadgets/space.gadget" },
    };
    ULONG i;

    for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
    {
        *libs[i].base = OpenLibrary((CONST_STRPTR)libs[i].name, 40);
        if (!*libs[i].base)
        {
            Printf((CONST_STRPTR)"AATextManager: cannot open %s\n",
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
        &SpaceBase, &AslBase, &StringBase, &ListBrowserBase, &ButtonBase, &LabelBase,
        &CheckBoxBase, &ChooserBase, &ClickTabBase, &LayoutBase,
        &WindowBase, &UtilityBase, (struct Library **)&GfxBase,
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

/* Tab page of a gadget, -1 for those outside the pages */
static LONG PageOf(LONG gid)
{
    if (gid >= GID_FILES && gid <= GID_INSTALL)
        return 0;
    if (gid >= GID_CHECK && gid <= GID_REPAIR)
        return 1;
    return -1;
}

/* As in AATextPrefs: hidden pages are set through the page object. */
static void SetGad2(LONG gid, Tag tag1, ULONG data1, Tag tag2, ULONG data2)
{
    struct TagItem tags[3];
    ULONG page = 0;

    tags[0].ti_Tag = tag1;
    tags[0].ti_Data = data1;
    tags[1].ti_Tag = tag2;
    tags[1].ti_Data = data2;
    tags[2].ti_Tag = TAG_DONE;
    if (!win || !gads[gid])
        return;
    GetAttr(CLICKTAB_Current, (Object *)gads[GID_TABS], &page);
    if (PageOf(gid) >= 0 && PageOf(gid) != (LONG)page)
        SetPageGadgetAttrsA(gads[gid], pages, win, NULL, tags);
    else
        SetGadgetAttrsA(gads[gid], win, NULL, tags);
}

static void SetGad(LONG gid, Tag tag, ULONG data)
{
    SetGad2(gid, tag, data, TAG_IGNORE, 0);
}

static ULONG GetGad(LONG gid, Tag tag)
{
    ULONG v = 0;

    if (gads[gid])
        GetAttr(tag, (Object *)gads[gid], &v);
    return v;
}

static void SetStatus(const char *text)
{
    if (text != statustext)
        snprintf(statustext, sizeof(statustext), "%s", text);
    SetGad(GID_STATUS, GA_Text, (ULONG)statustext);
}

static void Busy(BOOL on)
{
    SetAttrs(winobj, WA_BusyPointer, on, TAG_DONE);
}

static LONG Charset(void)
{
    return (LONG)GetGad(GID_CHARSET, CHOOSER_Selected);
}

static const char *Engine(void)
{
    return engines[GetGad(GID_ENGINE, CHOOSER_Selected) ? 1 : 0];
}

static struct Hook previewhook;

/* The preview box: copy the rendered sample lines (render hook) */
static ULONG PreviewRender(struct Hook *hook, Object *obj,
                           struct gpRender *gpr)
{
    struct DrawInfo *dri = gpr->gpr_GInfo ? gpr->gpr_GInfo->gi_DrInfo : NULL;
    struct IBox *box = NULL;

    GetAttr(SPACE_AreaBox, obj, (ULONG *)&box);
    if (gpr->gpr_RPort && box)
        pv_Draw(gpr->gpr_RPort, box->Left, box->Top, box->Width,
                box->Height, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0,
                dri ? dri->dri_Pens[TEXTPEN] : 1, GetString(MSG_PV_NOTRTG));
    return 0;
}

/* Render the selected font (or nothing) and show it */
static void ShowPreview(void)
{
    LONG sel = (LONG)GetGad(GID_FILES, LISTBROWSER_Selected);
    ULONG page = GetGad(GID_TABS, CLICKTAB_Current);

    if (!win)
        return;
    Busy(TRUE);
    if (sel >= 0 && sel < numfiles)
        pv_Render(win->WScreen, files[sel].path, files[sel].face,
                  GetString(MSG_PV_ALPHABET), GetString(MSG_PV_SENTENCE));
    else
        pv_Render(win->WScreen, NULL, 0, NULL, NULL);
    Busy(FALSE);
    if (page == 0)
        RefreshGList(gads[GID_PREVIEW], win, NULL, 1);
}

/* ------------------------------------------------------------------ */
/* Install tab                                                          */
/* ------------------------------------------------------------------ */

static void ShowFiles(void)
{
    LONG i;

    SetGad(GID_FILES, LISTBROWSER_Labels, ~0UL);
    FreeListBrowserList(&filelb);
    for (i = 0; i < numfiles; i++)
    {
        struct FileEntry *f = &files[i];
        struct Node *n;

        snprintf(f->facetext, sizeof(f->facetext), "%ld", (long)f->face);
        n = AllocListBrowserNode(4,
            LBNA_Column, 0, LBNCA_Text, (ULONG)f->info.family,
            LBNA_Column, 1, LBNCA_Text, (ULONG)f->info.style,
            LBNA_Column, 2, LBNCA_Text, (ULONG)f->facetext,
            LBNA_Column, 3, LBNCA_Text,
                (ULONG)FilePart((CONST_STRPTR)f->path),
            TAG_DONE);
        if (n)
            AddTail(&filelb, n);
    }
    SetGad(GID_FILES, LISTBROWSER_Labels, (ULONG)&filelb);
    ShowPreview();
}

/* Add a file: one entry per face; FALSE if FreeType cannot read it */
static BOOL AddFile(const char *path)
{
    LONG face = 0, faces = 1;

    while (face < faces && numfiles < MAX_FILES)
    {
        struct FileEntry *f = &files[numfiles];

        if (!aa_GetFontInfo(path, face, &f->info))
            return face > 0;
        snprintf(f->path, sizeof(f->path), "%s", path);
        f->face = face;
        faces = f->info.numfaces;
        numfiles++;
        face++;
    }
    return TRUE;
}

static void AddFiles(void)
{
    struct FileRequester *fr;
    char path[256], msg[sizeof(statustext)];
    LONG i;

    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)win,
                             ASLFR_TitleText, (ULONG)GetString(MSG_ADD_TITLE),
                             ASLFR_DoMultiSelect, TRUE,
                             ASLFR_RejectIcons, TRUE,
                             ASLFR_DoPatterns, TRUE,
                             ASLFR_InitialPattern,
                                 (ULONG)"#?.(ttf|otf|ttc|otc)",
                             ASLFR_SleepWindow, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    if (AslRequest(fr, NULL))
    {
        msg[0] = 0;
        Busy(TRUE);
        for (i = 0; i < fr->fr_NumArgs; i++)
        {
            struct WBArg *a = &fr->fr_ArgList[i];

            if (!NameFromLock(a->wa_Lock, (STRPTR)path, sizeof(path)) ||
                !AddPart((STRPTR)path, a->wa_Name, sizeof(path)))
                continue;
            if (!AddFile(path))
                snprintf(msg, sizeof(msg), GetString(MSG_NOTFONT),
                         (const char *)a->wa_Name);
        }
        Busy(FALSE);
        ShowFiles();
        SetStatus(msg);
    }
    FreeAslRequest(fr);
}

static void RemoveFile(void)
{
    LONG sel = (LONG)GetGad(GID_FILES, LISTBROWSER_Selected);

    if (sel < 0 || sel >= numfiles)
        return;
    memmove(&files[sel], &files[sel + 1],
            (numfiles - sel - 1) * sizeof(files[0]));
    numfiles--;
    ShowFiles();
}

static void Install(void)
{
    static struct AAInstall in;
    char msg[sizeof(statustext)], notopen[100] = "";
    LONG i, ok = 0, skipped = 0, failed = 0;

    if (!numfiles)
    {
        SetStatus(GetString(MSG_NOFONTS));
        return;
    }
    aa_InstallDefaults(&in);
    if (!mgr_ParseSizes((const char *)GetGad(GID_SIZES, STRINGA_TextVal),
                        &in))
    {
        SetStatus(GetString(MSG_BADSIZES));
        return;
    }
    in.charset = Charset();
    in.engine = Engine();
    in.overwrite = GetGad(GID_OVERWRITE, GA_Selected) != 0;

    Busy(TRUE);
    for (i = 0; i < numfiles; i++)
    {
        in.source = files[i].path;
        in.face = files[i].face;
        switch (aa_InstallFont(&in))
        {
            case AA_INSTALL_OK:
                ok++;
                if (!mgr_CheckFont(&in))
                    snprintf(notopen, sizeof(notopen), GetString(MSG_NOTOPEN),
                             in.name);
                break;
            case AA_INSTALL_EXISTS:
                skipped++;
                break;
            default:
                failed++;
                break;
        }
    }
    Busy(FALSE);
    snprintf(msg, sizeof(msg), GetString(MSG_INSTALLED), (long)ok,
             (long)skipped, (long)failed);
    if (notopen[0])
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), " %s",
                 notopen);
    SetStatus(msg);
    if (!skipped && !failed)
    {
        numfiles = 0;
        ShowFiles();
    }
}

/* ------------------------------------------------------------------ */
/* Repair tab                                                           */
/* ------------------------------------------------------------------ */

static void Repair_Settings(struct AARepair *r)
{
    r->what = AA_REPAIR_PATH | AA_REPAIR_CODEPAGE | AA_REPAIR_HEIGHT;
    r->charset = Charset();
    r->engine = Engine();
    if (GetGad(GID_CHANGEENGINE, GA_Selected))
        r->what |= AA_REPAIR_ENGINE;
}

/* "font file moved, no code page" */
static void Problems(ULONG what, char *buf, LONG len)
{
    static const LONG msg[4] =
        { MSG_P_MOVED, MSG_P_CODEPAGE, MSG_P_ENGINE, MSG_P_HEIGHT };
    LONG i;

    buf[0] = 0;
    for (i = 0; i < 4; i++)
        if (what & (1 << i))
            snprintf(buf + strlen(buf), len - strlen(buf), "%s%s",
                     buf[0] ? ", " : "", GetString(msg[i]));
}

/* the problems text of each listed font, kept while the list shows */
static char (*problems)[64];

static LONG CheckFonts(void)
{
    struct AARepair r;
    char msg[sizeof(statustext)];
    LONG i, todo = 0;

    SetGad(GID_REPAIRLIST, LISTBROWSER_Labels, ~0UL);
    FreeListBrowserList(&repairlb);
    Repair_Settings(&r);
    Busy(TRUE);
    numdiag = aa_ScanFonts(diag, MAX_FONTS);
    Busy(FALSE);
    for (i = 0; i < numdiag; i++)
    {
        ULONG what = aa_RepairNeeded(&diag[i], &r);
        struct Node *n;

        if (!what)
            continue;
        Problems(what, problems[i], sizeof(problems[i]));
        n = AllocListBrowserNode(2,
            LBNA_Column, 0, LBNCA_Text, (ULONG)diag[i].name,
            LBNA_Column, 1, LBNCA_Text, (ULONG)problems[i],
            TAG_DONE);
        if (n)
            AddTail(&repairlb, n);
        todo++;
    }
    SetGad(GID_REPAIRLIST, LISTBROWSER_Labels, (ULONG)&repairlb);
    if (numdiag < 0)
        SetStatus(GetString(MSG_FONTSERR));
    else if (!todo)
        SetStatus(GetString(MSG_NOTHING));
    else
    {
        snprintf(msg, sizeof(msg), GetString(MSG_TOREPAIR), (long)todo);
        SetStatus(msg);
    }
    SetGad(GID_REPAIR, GA_Disabled, todo == 0);
    return todo;
}

static void RepairAll(void)
{
    static char backup[AA_FONTFILE_LEN];
    struct EasyStruct es;
    struct AARepair r;
    char msg[sizeof(statustext)];
    ULONG args[1];
    LONG i, todo, done = 0;

    /* again, with the settings as they are now */
    if (!(todo = CheckFonts()))
        return;
    args[0] = todo;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)GetString(MSG_TITLE);
    es.es_TextFormat = (UBYTE *)GetString(MSG_REPAIR_ASK);
    es.es_GadgetFormat = (UBYTE *)GetString(MSG_REPAIR_GADS);
    if (EasyRequestArgs(win, &es, NULL, args) != 1)
        return;

    Repair_Settings(&r);
    Busy(TRUE);
    for (i = 0; i < numdiag; i++)
        if (aa_RepairNeeded(&diag[i], &r) &&
            aa_RepairOTag(&diag[i], &r, backup) == AA_FIX_OK)
            done++;
    Busy(FALSE);
    CheckFonts();
    snprintf(msg, sizeof(msg), GetString(MSG_REPAIRED), (long)done,
             (long)todo);
    SetStatus(msg);
}

/* ------------------------------------------------------------------ */

static void AddTab(struct List *list, LONG msg, ULONG number)
{
    struct Node *n = AllocClickTabNode(TNA_Text, (ULONG)GetString(msg),
                                       TNA_Number, number, TAG_DONE);
    if (n)
        AddTail(list, n);
}

static void AddChoice(struct List *list, const char *text)
{
    struct Node *n = AllocChooserNode(CNA_Text, (ULONG)text, TAG_DONE);

    if (n)
        AddTail(list, n);
}

static void FreeLists(void)
{
    struct Node *n;

    while ((n = RemHead(&tablist)))
        FreeClickTabNode(n);
    while ((n = RemHead(&charsetlist)))
        FreeChooserNode(n);
    while ((n = RemHead(&enginelist)))
        FreeChooserNode(n);
    FreeListBrowserList(&filelb);
    FreeListBrowserList(&repairlb);
}

static struct Gadget *MakeButton(ULONG id, LONG msg)
{
    return (struct Gadget *)ButtonObject,
        GA_ID, id, GA_RelVerify, TRUE,
        GA_Text, (ULONG)GetString(msg),
    End;
}

static Object *InfoLine(LONG msg)
{
    /* "FONTS:_ttf/": the "_" is part of the name, not a key */
    return LabelObject,
        LABEL_Text, (ULONG)GetString(msg),
        LABEL_Underscore, 0,
    End;
}

static Object *InstallPage(void)
{
    filecols[0].ci_Title = (STRPTR)GetString(MSG_COL_FAMILY);
    filecols[1].ci_Title = (STRPTR)GetString(MSG_COL_STYLE);
    filecols[2].ci_Title = (STRPTR)GetString(MSG_COL_FACE);
    filecols[3].ci_Title = (STRPTR)GetString(MSG_COL_FILE);

    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddImage, InfoLine(MSG_INFO_INSTALL),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_FILES] = (struct Gadget *)ListBrowserObject,
            GA_ID, GID_FILES,
            GA_RelVerify, TRUE,
            LISTBROWSER_Labels, (ULONG)&filelb,
            LISTBROWSER_ColumnInfo, (ULONG)filecols,
            LISTBROWSER_ColumnTitles, TRUE,
            LISTBROWSER_ShowSelected, TRUE,
        End,
        CHILD_MinHeight, 100,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_ADD] = MakeButton(GID_ADD, MSG_ADD),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, gads[GID_REMOVE] = MakeButton(GID_REMOVE, MSG_REMOVE),
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, (ULONG)GetString(MSG_PREVIEW),
            LAYOUT_AddChild, gads[GID_PREVIEW] = (struct Gadget *)SpaceObject,
                GA_ID, GID_PREVIEW,
                SPACE_MinHeight, pv_NeededHeight(),
                SPACE_MinWidth, 300,
                SPACE_RenderHook, (ULONG)&previewhook,
            End,
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_SIZES] = (struct Gadget *)StringObject,
            GA_ID, GID_SIZES,
            STRINGA_MaxChars, sizeof(sizestext) - 1,
            STRINGA_TextVal, (ULONG)sizestext,
        End,
        Label(GetString(MSG_SIZES)),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_OVERWRITE] =
                         (struct Gadget *)CheckBoxObject,
            GA_ID, GID_OVERWRITE,
            GA_Text, (ULONG)GetString(MSG_OVERWRITE),
            GA_Selected, FALSE,
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_INSTALL] = MakeButton(GID_INSTALL,
                                                        MSG_INSTALL),
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
    End;
}

static Object *RepairPage(void)
{
    repaircols[0].ci_Title = (STRPTR)GetString(MSG_COL_FONT);
    repaircols[1].ci_Title = (STRPTR)GetString(MSG_COL_PROBLEM);

    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddImage, InfoLine(MSG_INFO_REPAIR),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_CHANGEENGINE] =
                         (struct Gadget *)CheckBoxObject,
            GA_ID, GID_CHANGEENGINE,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)GetString(MSG_CHANGE_ENGINE),
            GA_Selected, TRUE,
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_REPAIRLIST] =
                         (struct Gadget *)ListBrowserObject,
            GA_ID, GID_REPAIRLIST,
            GA_ReadOnly, TRUE,
            LISTBROWSER_Labels, (ULONG)&repairlb,
            LISTBROWSER_ColumnInfo, (ULONG)repaircols,
            LISTBROWSER_ColumnTitles, TRUE,
        End,
        CHILD_MinHeight, 100,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_CHECK] = MakeButton(GID_CHECK, MSG_CHECK),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, gads[GID_REPAIR] = (struct Gadget *)ButtonObject,
                GA_ID, GID_REPAIR, GA_RelVerify, TRUE,
                GA_Text, (ULONG)GetString(MSG_REPAIR),
                GA_Disabled, TRUE,
            End,
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
    End;
}

static BOOL OpenWin(struct Screen *scr, LONG charset)
{
    LONG i;

    AddTab(&tablist, MSG_TAB_INSTALL, 0);
    AddTab(&tablist, MSG_TAB_REPAIR, 1);
    for (i = 0; i < AA_NUM_CHARSETS; i++)
        AddChoice(&charsetlist, GetString(charset_msg[i]));
    AddChoice(&enginelist, GetString(MSG_ENGINE_AATEXT));
    AddChoice(&enginelist, GetString(MSG_ENGINE_FT2));

    winobj = WindowObject,
        WA_Title, (ULONG)GetString(MSG_TITLE),
        WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VLayoutObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,

            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_AddChild, gads[GID_CHARSET] =
                                 (struct Gadget *)ChooserObject,
                    GA_ID, GID_CHARSET, GA_RelVerify, TRUE,
                    CHOOSER_PopUp, TRUE,
                    CHOOSER_Labels, (ULONG)&charsetlist,
                    CHOOSER_Selected, charset,
                End,
                Label(GetString(MSG_CHARSET)),
                LAYOUT_AddChild, gads[GID_ENGINE] =
                                 (struct Gadget *)ChooserObject,
                    GA_ID, GID_ENGINE, GA_RelVerify, TRUE,
                    CHOOSER_PopUp, TRUE,
                    CHOOSER_Labels, (ULONG)&enginelist,
                    CHOOSER_Selected, 0,
                End,
                Label(GetString(MSG_ENGINE)),
            End,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, gads[GID_TABS] = (struct Gadget *)ClickTabObject,
                GA_ID, GID_TABS,
                GA_RelVerify, TRUE,
                CLICKTAB_Labels, (ULONG)&tablist,
                CLICKTAB_Current, 0,
                CLICKTAB_PageGroup, pages = PageObject,
                    PAGE_Add, InstallPage(),
                    PAGE_Add, RepairPage(),
                End,
            End,

            LAYOUT_AddChild, gads[GID_STATUS] = (struct Gadget *)ButtonObject,
                GA_ID, GID_STATUS,
                GA_ReadOnly, TRUE,
                GA_Underscore, 0,
                GA_Text, (ULONG)statustext,
                BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Justification, BCJ_LEFT,
            End,
            CHILD_WeightedHeight, 0,
        End,
    End;

    if (!winobj)
        return FALSE;
    win = (struct Window *)DoMethod(winobj, WM_OPEN, NULL);
    return win != NULL;
}

static void Action(ULONG id)
{
    switch (id)
    {
        case GID_FILES:     ShowPreview();  break;
        case GID_ADD:       AddFiles();     break;
        case GID_REMOVE:    RemoveFile();   break;
        case GID_INSTALL:   Install();      break;
        case GID_CHECK:     CheckFonts();   break;
        case GID_REPAIR:    RepairAll();    break;
        case GID_CHARSET:
        case GID_ENGINE:
        case GID_CHANGEENGINE:
            /* the repair list depends on these */
            if (numdiag > 0)
                CheckFonts();
            break;
    }
}

/* The key of a "_X" label, lower case, or 0 */
static ULONG ShortcutKey(LONG msg)
{
    const char *u = strchr(GetString(msg), '_');

    return u && u[1] ? ToLower((UBYTE)u[1]) : 0;
}

int mgr_RunGUI(const char *language)
{
    static const struct { LONG msg; ULONG gid; } keys[] =
    {
        { MSG_ADD, GID_ADD }, { MSG_REMOVE, GID_REMOVE },
        { MSG_INSTALL, GID_INSTALL }, { MSG_CHECK, GID_CHECK },
        { MSG_REPAIR, GID_REPAIR },
    };
    static struct AAPrefs prefs;
    struct Screen *scr = NULL;
    ULONG sigmask = 0;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    NewList(&tablist);
    NewList(&charsetlist);
    NewList(&enginelist);
    NewList(&filelb);
    NewList(&repairlb);

    files = AllocVec(MAX_FILES * sizeof(*files), MEMF_ANY | MEMF_CLEAR);
    diag = AllocVec(MAX_FONTS * sizeof(*diag), MEMF_ANY | MEMF_CLEAR);
    problems = AllocVec(MAX_FONTS * sizeof(*problems), MEMF_ANY | MEMF_CLEAR);
    if (!files || !diag || !problems || !OpenLibs())
        goto out;
    InitStrings(language);
    pv_Init();              /* without it the box stays empty */
    previewhook.h_Entry = (HOOKFUNC)(APTR)HookEntry;
    previewhook.h_SubEntry = (HOOKFUNC)(APTR)PreviewRender;

    /* the character set chosen in AATextPrefs */
    aa_ReadPrefs(&prefs, NULL, FALSE);
    if (!(scr = LockPubScreen(NULL)))
        goto out;
    if (!OpenWin(scr, prefs.charset < AA_NUM_CHARSETS ? prefs.charset : 0))
        goto out;
    UnlockPubScreen(NULL, scr);
    scr = NULL;

    GetAttr(WINDOW_SigMask, winobj, &sigmask);
    while (!done)
    {
        ULONG sigs = Wait(sigmask | SIGBREAKF_CTRL_C);
        ULONG result, key, i;
        UWORD code;

        if (sigs & SIGBREAKF_CTRL_C)
            break;
        while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) !=
               WMHI_LASTMSG)
        {
            switch (result & WMHI_CLASSMASK)
            {
                case WMHI_CLOSEWINDOW:
                    done = TRUE;
                    break;
                case WMHI_GADGETUP:
                    Action(result & WMHI_GADGETMASK);
                    break;
                case WMHI_VANILLAKEY:
                    key = ToLower(result & WMHI_KEYMASK);
                    if (key == 0x1b)
                        done = TRUE;
                    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
                        if (key && key == ShortcutKey(keys[i].msg) &&
                            PageOf(keys[i].gid) ==
                                (LONG)GetGad(GID_TABS, CLICKTAB_Current) &&
                            !GetGad(keys[i].gid, GA_Disabled))
                            Action(keys[i].gid);
                    break;
            }
        }
    }
    rc = RETURN_OK;

out:
    if (winobj)
        DisposeObject(winobj);
    if (scr)
        UnlockPubScreen(NULL, scr);
    FreeLists();
    FreeStrings();
    pv_Cleanup();
    CloseLibs();
    if (problems)
        FreeVec(problems);
    if (diag)
        FreeVec(diag);
    if (files)
        FreeVec(files);
    return rc;
}
