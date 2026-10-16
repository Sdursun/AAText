/*
 * AATextManager - the window (ReAction).
 *
 * Above the tabs: character set (the code page written into the .otag)
 * and engine, used by both tabs.
 *   Install  font files picked with the file requester (a .ttc gives one
 *            line per face), sizes, "replace", Install
 *   Repair   Check fonts lists the .otag files in FONTS: that can be
 *            repaired; Repair asks once, then repairs them all
 *   Diagnostics  every .otag in FONTS: with its font file and status,
 *            Fix .otag for a moved font file, a plain text report
 *   Download fonts from the catalog (fonts/catalog.txt on GitHub): list,
 *            search, preview, download to T: and install
 *   Settings the directories font files are copied to (by kind) and the
 *            character set the window starts with (settings.c)
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <graphics/text.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
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
#include <stdlib.h>

#include "manager.h"
#include "strings.h"
#include "preview.h"
#include "update.h"
#include "settings.h"
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
    GID_FILES, GID_PREVIEW, GID_PVAA, GID_ADD, GID_REMOVE, GID_SIZES, GID_OVERWRITE, GID_INSTALL,
    GID_CHECK, GID_REPAIRLIST, GID_CHANGEENGINE, GID_REPAIR,
    GID_DSCAN, GID_DSCANSUM, GID_DFONTLIST, GID_DDETAIL1, GID_DDETAIL2,
    GID_DDETAIL3, GID_DDETAIL4, GID_DADVICE, GID_DFIX, GID_DREPORT,
    GID_DLFILTER, GID_DLLOAD, GID_DLLIST, GID_DLPREVIEWBOX, GID_DLAA,
    GID_DLINSTALL,
    GID_TTFDIR, GID_TTFPICK, GID_OTFDIR, GID_OTFPICK, GID_TTCDIR,
    GID_TTCPICK, GID_DEFCHARSET, GID_SAVESET,
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
static struct List tablist, charsetlist, defcharsetlist, enginelist, filelb, repairlb,
                   diaglb, dllb;
static struct FileEntry *files;
static LONG numfiles;
static struct AADiagEntry *diag;
static LONG numdiag;
/* the Diagnostics tab has its own scan: the Repair list points into diag */
#define MAX_DIAG 300
static struct AADiagEntry *dscan;       /* MAX_DIAG entries */
static LONG numdscan = -1;              /* -1: not scanned yet */
static char statustext[200];
static struct MgrSettings settings;
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
    if (gid >= GID_DSCAN && gid <= GID_DREPORT)
        return 2;
    if (gid >= GID_DLFILTER && gid <= GID_DLINSTALL)
        return 3;
    if (gid >= GID_TTFDIR && gid <= GID_SAVESET)
        return 4;
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

/* the Settings tab's directories, as they are typed */
static const ULONG dirgad[MGR_NUM_DIRS] = { GID_TTFDIR, GID_OTFDIR, GID_TTCDIR };

static void ReadDirs(void)
{
    LONG i;
    const char *t;

    for (i = 0; i < MGR_NUM_DIRS; i++)
        if ((t = (const char *)GetGad(dirgad[i], STRINGA_TextVal)) && t[0])
            snprintf(settings.dir[i], sizeof(settings.dir[i]), "%s", t);
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
    ReadDirs();

    Busy(TRUE);
    for (i = 0; i < numfiles; i++)
    {
        in.source = files[i].path;
        in.face = files[i].face;
        in.fontdir = settings.dir[mgr_DirKind(in.source)];
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
    r->what = AA_REPAIR_PATH | AA_REPAIR_CODEPAGE | AA_REPAIR_HEIGHT |
              AA_REPAIR_WIDTH;
    r->charset = Charset();
    r->engine = Engine();
    if (GetGad(GID_CHANGEENGINE, GA_Selected))
        r->what |= AA_REPAIR_ENGINE;
}

/* "font file moved, no code page" */
static void Problems(ULONG what, char *buf, LONG len)
{
    static const LONG msg[5] =
        { MSG_P_MOVED, MSG_P_CODEPAGE, MSG_P_ENGINE, MSG_P_HEIGHT,
          MSG_P_WIDTH };
    LONG i;

    buf[0] = 0;
    for (i = 0; i < 5; i++)
        if (what & (1 << i))
            snprintf(buf + strlen(buf), len - strlen(buf), "%s%s",
                     buf[0] ? ", " : "", GetString(msg[i]));
}

/* "Verdana Bold" from the .otag, or its file name ("verdanabold") */
static const char *FontLabel(const struct AADiagEntry *e)
{
    return e->display[0] ? e->display : e->name;
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
    FreeListBrowserList(&diaglb);
    FreeListBrowserList(&dllb);
    FreeVec(dscan);
    dscan = NULL;
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
            LBNA_Column, 0, LBNCA_Text, (ULONG)FontLabel(&diag[i]),
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
    while ((n = RemHead(&defcharsetlist)))
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

/* "Antialiasing" for the previews (both tabs show the same setting) */
static BOOL pvaa = TRUE;

static struct Gadget *AACheckBox(ULONG id)
{
    return (struct Gadget *)CheckBoxObject,
        GA_ID, id,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)GetString(MSG_PV_AA),
        GA_Selected, pvaa,
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
            LAYOUT_AddChild, gads[GID_PVAA] = AACheckBox(GID_PVAA),
            CHILD_WeightedHeight, 0,
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

/* ------------------------------------------------------------------ */
/* Diagnostics tab (until 0.23 in AATextPrefs)                         */
/* ------------------------------------------------------------------ */

static char scansum[96];
/* the read-only fields under the font list */
static char detail1[AA_FONTFILE_LEN];   /* .otag file */
static char detail2[AA_FONTFILE_LEN];   /* font file named in the .otag */
static char detail3[AA_FONTFILE_LEN];   /* where it was found */
static char detail4[80];                /* OT_Engine and its library */
static char advice[128];

static const LONG diag_msg[AA_DIAG_NUM] =
{
    MSG_DIAG_OK, MSG_DIAG_MOVED, MSG_DIAG_MISSING, MSG_DIAG_BADFILE,
    MSG_DIAG_BADOTAG, MSG_DIAG_OTHER
};

static const LONG diag_advice[AA_DIAG_NUM] =
{
    MSG_DIAG_ADV_OK, MSG_DIAG_ADV_MOVED, MSG_DIAG_ADV_MISSING,
    MSG_DIAG_ADV_BADFILE, MSG_DIAG_ADV_BADOTAG, MSG_DIAG_ADV_OTHER
};

/* English status names for the report, which goes into bug reports */
static const char *const diag_name[AA_DIAG_NUM] =
{
    "ok", "moved", "missing", "not a font", "bad .otag", "other engine"
};

static struct ColumnInfo diagcols[] =
{
    { 32, NULL, 0 },
    { 18, NULL, 0 },
    { 50, NULL, 0 },
    { -1, NULL, 0 }
};

/*
 * Show the selected font in the read-only fields under the list: its
 * .otag, the font file named there, where it really is, the OT_Engine
 * with its library, and what to do about it.
 */
static void DiagDetail(void)
{
    ULONG sel = ~0UL;
    struct AADiagEntry *e;
    const char *arg;

    detail1[0] = detail2[0] = detail3[0] = detail4[0] = advice[0] = 0;
    if (gads[GID_DFONTLIST])
        GetAttr(LISTBROWSER_Selected, (Object *)gads[GID_DFONTLIST], &sel);
    if ((LONG)sel >= 0 && (LONG)sel < numdscan)
    {
        e = &dscan[sel];
        snprintf(detail1, sizeof(detail1), "%s", e->otag);
        snprintf(detail2, sizeof(detail2), "%s", e->want);
        snprintf(detail3, sizeof(detail3), "%s", e->found);
        if (e->engine[0] && e->enginever[0])
            snprintf(detail4, sizeof(detail4), GetString(MSG_DIAG_ENGINEFMT),
                     e->engine, e->engine, e->enginever);
        else if (e->engine[0])
            snprintf(detail4, sizeof(detail4),
                     GetString(MSG_DIAG_ENGINENONE), e->engine, e->engine);

        arg = e->status == AA_DIAG_OTHER ? e->engine : "";
        snprintf(advice, sizeof(advice), GetString(diag_advice[e->status]),
                 arg);
        /* without the engine's library diskfont cannot make the font
           for any program, even when the file is fine */
        if (e->engine[0] && !e->enginever[0])
            snprintf(advice, sizeof(advice), GetString(MSG_DIAG_NOENGINE),
                     e->engine);
    }
    SetGad(GID_DDETAIL1, STRINGA_TextVal, (ULONG)detail1);
    SetGad(GID_DDETAIL2, STRINGA_TextVal, (ULONG)detail2);
    SetGad(GID_DDETAIL3, STRINGA_TextVal, (ULONG)detail3);
    SetGad(GID_DDETAIL4, STRINGA_TextVal, (ULONG)detail4);
    SetGad(GID_DADVICE, GA_Text, (ULONG)advice);
    SetGad(GID_DFIX, GA_Disabled, (LONG)sel < 0 || (LONG)sel >= numdscan ||
           dscan[sel].status != AA_DIAG_MOVED);
}

/* Show the scan results; sel is the entry to select, or -1. */
static void DiagShow(LONG sel)
{
    LONG i, ok = 0;

    SetGad(GID_DFONTLIST, LISTBROWSER_Labels, ~0UL);
    FreeListBrowserList(&diaglb);
    for (i = 0; i < numdscan; i++)
    {
        struct AADiagEntry *e = &dscan[i];
        struct Node *n = AllocListBrowserNode(3,
            LBNA_Column, 0, LBNCA_Text, (ULONG)FontLabel(e),
            LBNA_Column, 1, LBNCA_Text, (ULONG)GetString(diag_msg[e->status]),
            LBNA_Column, 2, LBNCA_Text,
                (ULONG)(e->found[0] ? e->found : e->want),
            TAG_DONE);

        if (n)
            AddTail(&diaglb, n);
        if (e->status == AA_DIAG_OK || e->status == AA_DIAG_OTHER)
            ok++;
    }
    SetGad2(GID_DFONTLIST, LISTBROWSER_Labels, (ULONG)&diaglb,
            LISTBROWSER_Selected, (ULONG)sel);

    if (numdscan < 0)
        snprintf(scansum, sizeof(scansum), "%s", GetString(MSG_DIAG_NOFONTS));
    else if (numdscan == MAX_DIAG)
        snprintf(scansum, sizeof(scansum), GetString(MSG_DIAG_LISTFULL),
                 (long)MAX_DIAG);
    else
        snprintf(scansum, sizeof(scansum), GetString(MSG_DIAG_SUMMARY),
                 (long)numdscan, (long)ok, (long)(numdscan - ok));
    SetGad(GID_DSCANSUM, GA_Text, (ULONG)scansum);
    DiagDetail();
}

static void DiagScan(void)
{
    if (!dscan)
        dscan = AllocVec(MAX_DIAG * sizeof(*dscan), MEMF_ANY);
    if (!dscan)
        return;

    Busy(TRUE);
    numdscan = aa_ScanFonts(dscan, MAX_DIAG);
    Busy(FALSE);
    DiagShow(-1);
}

/*
 * Point the selected moved font's .otag at the file where it was
 * found, after asking; the original is kept as .otag.bak.
 */
static void DiagFix(void)
{
    static char backup[AA_FONTFILE_LEN + 4], msg[sizeof(statustext)];
    struct EasyStruct es;
    struct AADiagEntry *e;
    ULONG sel = ~0UL, args[4];
    LONG r;

    GetAttr(LISTBROWSER_Selected, (Object *)gads[GID_DFONTLIST], &sel);
    if ((LONG)sel < 0 || (LONG)sel >= numdscan ||
        dscan[sel].status != AA_DIAG_MOVED)
    {
        SetStatus(GetString(MSG_DIAG_FIX_NOTMOVED));
        return;
    }
    e = &dscan[sel];

    snprintf(backup, sizeof(backup), "%s.bak", e->otag);
    args[0] = (ULONG)e->otag;
    args[1] = (ULONG)e->want;
    args[2] = (ULONG)e->found;
    args[3] = (ULONG)backup;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)GetString(MSG_TITLE);
    es.es_TextFormat = (UBYTE *)GetString(MSG_DIAG_FIX_ASK);
    es.es_GadgetFormat = (UBYTE *)GetString(MSG_DIAG_FIX_GADS);
    if (EasyRequestArgs(win, &es, NULL, args) != 1)
        return;

    r = aa_FixOTag(e, backup);
    switch (r)
    {
        case AA_FIX_OK:
            snprintf(msg, sizeof(msg), GetString(MSG_DIAG_FIXED), backup);
            break;
        case AA_FIX_BACKUP:
            snprintf(msg, sizeof(msg), GetString(MSG_DIAG_FIX_BACKUP), backup);
            break;
        case AA_FIX_WRITE:
            snprintf(msg, sizeof(msg), GetString(MSG_DIAG_FIX_WRITE),
                     e->otag, backup);
            break;
        default:
            snprintf(msg, sizeof(msg), GetString(MSG_DIAG_FIX_READ), e->otag);
            break;
    }
    DiagShow(sel);
    SetStatus(msg);
}

/* Plain text, English: meant to be attached to bug reports. */
static BOOL DiagWrite(const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    LONG i;
    BOOL ok = TRUE;

    if (!fh)
        return FALSE;
    FPrintf(fh, (CONST_STRPTR)"AAText font report (%s)\n",
            (ULONG)"AATextManager " AAMGR_VERSION);
    FPrintf(fh, (CONST_STRPTR)"%ld font(s) in FONTS:\n", (ULONG)numdscan);
    for (i = 0; i < numdscan && ok; i++)
    {
        struct AADiagEntry *e = &dscan[i];

        FPrintf(fh, (CONST_STRPTR)"\n%s: %s\n  .otag:    %s\n",
                (ULONG)e->name, (ULONG)diag_name[e->status], (ULONG)e->otag);
        if (e->display[0])
            FPrintf(fh, (CONST_STRPTR)"  family:   %s\n", (ULONG)e->display);
        if (e->want[0])
            FPrintf(fh, (CONST_STRPTR)"  in .otag: %s\n", (ULONG)e->want);
        if (e->found[0] && strcmp(e->found, e->want))
            FPrintf(fh, (CONST_STRPTR)"  found:    %s\n", (ULONG)e->found);
        ok = FPrintf(fh, (CONST_STRPTR)"  engine:   %s (%s.library %s), "
                     "face %ld, code page %s\n",
                     (ULONG)(e->engine[0] ? e->engine : "?"),
                     (ULONG)(e->engine[0] ? e->engine : "?"),
                     (ULONG)(e->enginever[0] ? e->enginever : "not installed"),
                     (ULONG)e->facenum,
                     (ULONG)(e->codepage ? "in .otag" : "none")) >= 0;
    }
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

static void DiagReport(void)
{
    static char path[AA_PATH_LEN];
    struct FileRequester *fr;
    char msg[AA_PATH_LEN + 40];

    if (numdscan < 0)
        DiagScan();
    if (numdscan < 0)
        return;
    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)win,
                             ASLFR_TitleText,
                                 (ULONG)GetString(MSG_DIAG_SAVE_TITLE),
                             ASLFR_InitialDrawer, (ULONG)"RAM:",
                             ASLFR_InitialFile, (ULONG)"AAText-fonts.txt",
                             ASLFR_DoSaveMode, TRUE,
                             ASLFR_RejectIcons, TRUE,
                             ASLFR_SleepWindow, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
    {
        snprintf(path, sizeof(path), "%s", (char *)fr->fr_Drawer);
        AddPart((STRPTR)path, fr->fr_File, sizeof(path));
        snprintf(msg, sizeof(msg), GetString(DiagWrite(path) ?
                 MSG_DIAG_SAVED : MSG_DIAG_WRITEERR), path);
        SetStatus(msg);
    }
    FreeAslRequest(fr);
}

/* A read-only text field for the font details: long paths can be
   scrolled in it. */
static struct Gadget *DetailField(ULONG id, char *text)
{
    return (struct Gadget *)StringObject,
        GA_ID, id,
        GA_ReadOnly, TRUE,
        STRINGA_MaxChars, AA_FONTFILE_LEN,
        STRINGA_TextVal, (ULONG)text,
    End;
}

static Object *DiagPage(void)
{
    diagcols[0].ci_Title = (STRPTR)GetString(MSG_DIAG_COL_FONT);
    diagcols[1].ci_Title = (STRPTR)GetString(MSG_DIAG_COL_STATUS);
    diagcols[2].ci_Title = (STRPTR)GetString(MSG_DIAG_COL_FILE);

    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,

        LAYOUT_AddImage, LabelObject,
            LABEL_Text, (ULONG)GetString(MSG_DIAG_INFO),
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_DSCAN] = (struct Gadget *)ButtonObject,
                GA_ID, GID_DSCAN, GA_RelVerify, TRUE,
                GA_Text, (ULONG)GetString(MSG_DIAG_SCAN),
            End,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, gads[GID_DSCANSUM] = (struct Gadget *)ButtonObject,
                GA_ID, GID_DSCANSUM,
                GA_ReadOnly, TRUE,
                GA_Underscore, 0,
                GA_Text, (ULONG)scansum,
                BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Justification, BCJ_LEFT,
            End,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, gads[GID_DFONTLIST] =
                         (struct Gadget *)ListBrowserObject,
            GA_ID, GID_DFONTLIST,
            GA_RelVerify, TRUE,
            LISTBROWSER_Labels, (ULONG)&diaglb,
            LISTBROWSER_ColumnInfo, (ULONG)diagcols,
            LISTBROWSER_ColumnTitles, TRUE,
            LISTBROWSER_ShowSelected, TRUE,
            LISTBROWSER_HorizontalProp, TRUE,
        End,
        CHILD_MinHeight, 100,

        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_AddChild, gads[GID_DDETAIL1] = DetailField(GID_DDETAIL1,
                                                             detail1),
            Label(GetString(MSG_DIAG_L_OTAG)),
            LAYOUT_AddChild, gads[GID_DDETAIL2] = DetailField(GID_DDETAIL2,
                                                             detail2),
            Label(GetString(MSG_DIAG_L_WANT)),
            LAYOUT_AddChild, gads[GID_DDETAIL3] = DetailField(GID_DDETAIL3,
                                                             detail3),
            Label(GetString(MSG_DIAG_L_FOUND)),
            LAYOUT_AddChild, gads[GID_DDETAIL4] = DetailField(GID_DDETAIL4,
                                                             detail4),
            /* "OT_Engine": the "_" is part of the name, not a key */
            CHILD_Label, LabelObject,
                LABEL_Text, (ULONG)GetString(MSG_DIAG_L_ENGINE),
                LABEL_Underscore, 0,
            End,
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, gads[GID_DADVICE] = (struct Gadget *)ButtonObject,
            GA_ID, GID_DADVICE,
            GA_ReadOnly, TRUE,
            GA_Underscore, 0,
            GA_Text, (ULONG)advice,
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Justification, BCJ_LEFT,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_DFIX] = (struct Gadget *)ButtonObject,
                GA_ID, GID_DFIX, GA_RelVerify, TRUE,
                GA_Disabled, TRUE,
                GA_Text, (ULONG)GetString(MSG_DIAG_FIX),
            End,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, gads[GID_DREPORT] = (struct Gadget *)ButtonObject,
                GA_ID, GID_DREPORT, GA_RelVerify, TRUE,
                GA_Text, (ULONG)GetString(MSG_DIAG_REPORT),
            End,
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
    End;
}

/* ------------------------------------------------------------------ */
/* Download tab: fonts from the catalog (fonts/catalog.txt)             */
/* ------------------------------------------------------------------ */

#define MAX_CAT      1500
#define MAX_CATTEXT  (256 * 1024)
#define DL_DIR       "T:AATextFonts"

struct CatEntry
{
    char family[40];
    char style[28];
    char licence[8];
    char size[12];          /* "331 KB" */
    LONG bytes;
    char path[120];         /* relative to catbase, or an https:// url */
};

static struct CatEntry *cat;
static LONG numcat;
static LONG *shown;         /* list row -> cat index */
static LONG numshown;
static char catbase[128];

static struct ColumnInfo catcols[] =
{
    { 34, NULL, 0 }, { 30, NULL, 0 }, { 16, NULL, 0 }, { 20, NULL, 0 },
    { -1, NULL, 0 }
};

/* Copy one tab-separated field; returns the start of the next one. */
static char *Field(char *p, char *dst, LONG len)
{
    LONG n = 0;

    while (*p && *p != '\t')
    {
        if (n < len - 1)
            dst[n++] = *p;
        p++;
    }
    dst[n] = 0;
    return *p ? p + 1 : p;
}

static void ParseCatalog(char *text)
{
    char *line, *nl, num[16];
    struct CatEntry *e;

    numcat = 0;
    catbase[0] = 0;
    for (line = text; line && numcat < MAX_CAT; line = nl)
    {
        if ((nl = strchr(line, '\n')))
            *nl++ = 0;
        if (strchr(line, '\r'))
            *strchr(line, '\r') = 0;
        if (!line[0] || line[0] == '#')
            continue;
        if (!strncmp(line, "base ", 5))
        {
            snprintf(catbase, sizeof(catbase), "%s", line + 5);
            continue;
        }
        e = &cat[numcat];
        line = Field(line, e->family, sizeof(e->family));
        line = Field(line, e->style, sizeof(e->style));
        line = Field(line, e->licence, sizeof(e->licence));
        line = Field(line, num, sizeof(num));
        Field(line, e->path, sizeof(e->path));
        if (!e->family[0] || !e->path[0])
            continue;
        e->bytes = atol(num);
        snprintf(e->size, sizeof(e->size), "%ld KB",
                 (long)((e->bytes + 1023) / 1024));
        numcat++;
    }
}

/* Case-insensitive "needle in hay" (ASCII is enough for family names) */
static BOOL Contains(const char *hay, const char *needle)
{
    LONG n = strlen(needle);

    for (; *hay; hay++)
        if (!Strnicmp((CONST_STRPTR)hay, (CONST_STRPTR)needle, n))
            return TRUE;
    return !n;
}

/* The catalog rows whose family matches the search text */
static void ShowCatalog(void)
{
    const char *filter = (const char *)GetGad(GID_DLFILTER, STRINGA_TextVal);
    LONG i;

    SetGad(GID_DLLIST, LISTBROWSER_Labels, ~0UL);
    FreeListBrowserList(&dllb);
    numshown = 0;
    for (i = 0; i < numcat; i++)
    {
        struct CatEntry *e = &cat[i];
        struct Node *n;

        if (filter && !Contains(e->family, filter))
            continue;
        n = AllocListBrowserNode(4,
            LBNA_Column, 0, LBNCA_Text, (ULONG)e->family,
            LBNA_Column, 1, LBNCA_Text, (ULONG)e->style,
            LBNA_Column, 2, LBNCA_Text, (ULONG)e->licence,
            LBNA_Column, 3, LBNCA_Text, (ULONG)e->size,
            TAG_DONE);
        if (!n)
            break;
        AddTail(&dllb, n);
        shown[numshown++] = i;
    }
    SetGad(GID_DLLIST, LISTBROWSER_Labels, (ULONG)&dllb);
}

/* Explain why the network could not be opened */
static void NetFailed(LONG r, struct AANet *n)
{
    char msg[sizeof(statustext)];

    if (r == UPD_NONET)
        SetStatus(GetString(MSG_DL_NONET));
    else if (r == UPD_NOSSL)
        SetStatus(GetString(MSG_DL_NOSSL));
    else
    {
        snprintf(msg, sizeof(msg), GetString(MSG_DL_LOADFAIL), n->error);
        SetStatus(msg);
    }
}

static void LoadCatalog(void)
{
    static struct AANet net;
    char msg[sizeof(statustext)];
    char *text;
    LONG len = -1, r;
    BPTR fh;

    if (!(text = AllocVec(MAX_CATTEXT, MEMF_ANY)))
        return;
    SetStatus(GetString(MSG_DL_LOADING));
    Busy(TRUE);
    if (strncmp(settings.catalog, "https://", 8))
    {
        /* a file (for testing a catalog before it is published) */
        if ((fh = Open((CONST_STRPTR)settings.catalog, MODE_OLDFILE)))
        {
            len = Read(fh, text, MAX_CATTEXT - 1);
            Close(fh);
        }
        strcpy(net.error, "cannot read the file");
        if (len >= 0)
            text[len] = 0;
    }
    else if ((r = net_Open(&net, AAMGR_VERSION)))
    {
        Busy(FALSE);
        NetFailed(r, &net);
        FreeVec(text);
        return;
    }
    else
    {
        len = net_GetMem(&net, settings.catalog, text, MAX_CATTEXT);
        net_Close(&net);
    }
    Busy(FALSE);
    if (len < 0)
    {
        snprintf(msg, sizeof(msg), GetString(MSG_DL_LOADFAIL), net.error);
        SetStatus(msg);
    }
    else
    {
        ParseCatalog(text);
        ShowCatalog();
        snprintf(msg, sizeof(msg), GetString(MSG_DL_LOADED), (long)numcat);
        SetStatus(msg);
    }
    FreeVec(text);
}

/* T:AATextFonts/<file name> of a catalog entry */
static void LocalPath(const struct CatEntry *e, char *buf, LONG len)
{
    snprintf(buf, len, "%s", DL_DIR);
    AddPart((STRPTR)buf, FilePart((CONST_STRPTR)e->path), len);
}

static LONG SizeOf(const char *path)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR l = Lock((CONST_STRPTR)path, ACCESS_READ);
    LONG size = -1;

    if (l && fib && Examine(l, fib))
        size = fib->fib_Size;
    if (l)
        UnLock(l);
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    return size;
}

/*
 * Make sure the entry is in T:AATextFonts (downloads it unless it is
 * there with the catalog's size); FALSE with the reason in the status.
 */
static BOOL Fetch(struct AANet *net, const struct CatEntry *e, char *path,
                  LONG len)
{
    static char url[300];
    char msg[sizeof(statustext)];
    BPTR l;

    LocalPath(e, path, len);
    if (e->bytes > 0 && SizeOf(path) == e->bytes)
        return TRUE;
    if (!(l = Lock((CONST_STRPTR)DL_DIR, ACCESS_READ)))
        l = CreateDir((CONST_STRPTR)DL_DIR);
    if (l)
        UnLock(l);
    if (!strncmp(e->path, "https://", 8))
        snprintf(url, sizeof(url), "%s", e->path);
    else
        snprintf(url, sizeof(url), "%s%s", catbase, e->path);
    if (net_GetFile(net, url, path))
    {
        LONG size = SizeOf(path);

        /* the size the catalog gives: a short file is half a font */
        if (e->bytes <= 0 || size == e->bytes)
            return TRUE;
        DeleteFile((CONST_STRPTR)path);
        snprintf(net->error, sizeof(net->error),
                 "incomplete: %ld of %ld bytes", (long)size, (long)e->bytes);
    }
    snprintf(msg, sizeof(msg), GetString(MSG_DL_GETFAIL),
             FilePart((CONST_STRPTR)e->path), net->error);
    SetStatus(msg);
    return FALSE;
}

/* The catalog index of the only selected row; -1 for none or several */
static LONG OnlySelected(void)
{
    struct Node *n;
    LONG row, found = -1;
    ULONG sel;

    for (n = dllb.lh_Head, row = 0; n->ln_Succ; n = n->ln_Succ, row++)
    {
        sel = 0;
        GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel)
            continue;
        if (found >= 0)
            return -1;
        found = row;
    }
    return found >= 0 && found < numshown ? shown[found] : -1;
}

/*
 * A click in the list: one font selected is downloaded (unless it is in
 * T:AATextFonts already) and shown; with several, or none, the preview
 * is cleared.
 */
static void DlPreview(void)
{
    static struct AANet net;
    static char path[256];
    LONG i = OnlySelected(), r;
    BOOL ok;

    if (i < 0)
    {
        pv_Render(win->WScreen, NULL, 0, NULL, NULL);
        RefreshGList(gads[GID_DLPREVIEWBOX], win, NULL, 1);
        return;
    }
    LocalPath(&cat[i], path, sizeof(path));
    if (cat[i].bytes > 0 && SizeOf(path) == cat[i].bytes)
    {
        /* there already: no network needed */
        Busy(TRUE);
        pv_Render(win->WScreen, path, 0, GetString(MSG_PV_ALPHABET),
                  GetString(MSG_PV_SENTENCE));
        Busy(FALSE);
        RefreshGList(gads[GID_DLPREVIEWBOX], win, NULL, 1);
        return;
    }
    if ((r = net_Open(&net, AAMGR_VERSION)))
    {
        NetFailed(r, &net);
        return;
    }
    Busy(TRUE);
    ok = Fetch(&net, &cat[i], path, sizeof(path));
    net_Close(&net);
    if (ok)
    {
        pv_Render(win->WScreen, path, 0, GetString(MSG_PV_ALPHABET),
                  GetString(MSG_PV_SENTENCE));
        SetStatus("");
    }
    Busy(FALSE);
    RefreshGList(gads[GID_DLPREVIEWBOX], win, NULL, 1);
}

static void DlInstall(void)
{
    static struct AAInstall in;
    static struct AANet net;
    static char path[256];
    char msg[sizeof(statustext)], notopen[100] = "";
    struct Node *n;
    LONG row, todo = 0, done = 0, ok = 0, skipped = 0, failed = 0, r;
    ULONG sel;

    for (n = dllb.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        sel = 0;
        GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        todo += sel != 0;
    }
    if (!todo)
    {
        SetStatus(GetString(MSG_DL_NONE));
        return;
    }
    aa_InstallDefaults(&in);
    if (!mgr_ParseSizes((const char *)GetGad(GID_SIZES, STRINGA_TextVal), &in))
    {
        SetStatus(GetString(MSG_BADSIZES));
        return;
    }
    in.charset = Charset();
    in.engine = Engine();
    in.overwrite = GetGad(GID_OVERWRITE, GA_Selected) != 0;
    ReadDirs();
    if ((r = net_Open(&net, AAMGR_VERSION)))
    {
        NetFailed(r, &net);
        return;
    }

    Busy(TRUE);
    for (n = dllb.lh_Head, row = 0; n->ln_Succ; n = n->ln_Succ, row++)
    {
        struct CatEntry *e = &cat[shown[row]];

        sel = 0;
        GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel)
            continue;
        snprintf(msg, sizeof(msg), GetString(MSG_DL_GETTING), (long)++done,
                 (long)todo, FilePart((CONST_STRPTR)e->path));
        SetStatus(msg);
        if (!Fetch(&net, e, path, sizeof(path)))
        {
            failed++;
            continue;
        }
        in.source = path;
        in.face = 0;
        in.fontdir = settings.dir[mgr_DirKind(path)];
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
        DeleteFile((CONST_STRPTR)path);     /* installed: a copy is in fontdir */
    }
    net_Close(&net);
    Busy(FALSE);
    snprintf(msg, sizeof(msg), GetString(MSG_INSTALLED), (long)ok,
             (long)skipped, (long)failed);
    if (notopen[0])
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), " %s",
                 notopen);
    SetStatus(msg);
}

static Object *DownloadPage(void)
{
    catcols[0].ci_Title = (STRPTR)GetString(MSG_COL_FAMILY);
    catcols[1].ci_Title = (STRPTR)GetString(MSG_COL_STYLE);
    catcols[2].ci_Title = (STRPTR)GetString(MSG_DL_COL_LIC);
    catcols[3].ci_Title = (STRPTR)GetString(MSG_DL_COL_SIZE);

    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddImage, InfoLine(MSG_DL_INFO),
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, gads[GID_DLFILTER] = (struct Gadget *)StringObject,
                GA_ID, GID_DLFILTER, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
                STRINGA_MaxChars, 39,
            End,
            CHILD_Label, LabelObject,
                LABEL_Text, (ULONG)GetString(MSG_DL_FILTER),
            End,
            LAYOUT_AddChild, gads[GID_DLLOAD] = MakeButton(GID_DLLOAD,
                                                           MSG_DL_LOAD),
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, gads[GID_DLLIST] = (struct Gadget *)ListBrowserObject,
            GA_ID, GID_DLLIST,
            GA_RelVerify, TRUE,
            LISTBROWSER_Labels, (ULONG)&dllb,
            LISTBROWSER_ColumnInfo, (ULONG)catcols,
            LISTBROWSER_ColumnTitles, TRUE,
            LISTBROWSER_MultiSelect, TRUE,
            LISTBROWSER_ShowSelected, TRUE,
        End,
        CHILD_MinHeight, 90,

        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, (ULONG)GetString(MSG_PREVIEW),
            LAYOUT_AddChild, gads[GID_DLPREVIEWBOX] =
                             (struct Gadget *)SpaceObject,
                GA_ID, GID_DLPREVIEWBOX,
                SPACE_MinHeight, pv_NeededHeight(),
                SPACE_RenderHook, (ULONG)&previewhook,
            End,
            LAYOUT_AddChild, gads[GID_DLAA] = AACheckBox(GID_DLAA),
            CHILD_WeightedHeight, 0,
        End,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, SpaceObject, End,
            LAYOUT_AddChild, gads[GID_DLINSTALL] = MakeButton(GID_DLINSTALL,
                                                              MSG_DL_INSTALL),
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
    End;
}

/* ------------------------------------------------------------------ */
/* Settings tab                                                         */
/* ------------------------------------------------------------------ */

/* ASL's drawer requester; the drawer goes into the field. */
static void PickDir(LONG kind)
{
    static char path[MGR_DIR_LEN];
    struct FileRequester *fr;
    LONG n;

    ReadDirs();
    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)win,
                             ASLFR_TitleText,
                                 (ULONG)GetString(MSG_SET_PICKTITLE),
                             ASLFR_InitialDrawer, (ULONG)settings.dir[kind],
                             ASLFR_DrawersOnly, TRUE,
                             ASLFR_SleepWindow, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    if (AslRequest(fr, NULL) && fr->fr_Drawer && fr->fr_Drawer[0])
    {
        snprintf(path, sizeof(path), "%s", (char *)fr->fr_Drawer);
        mgr_FontsRelative(path, sizeof(path));
        n = strlen(path);
        if (n && path[n - 1] != ':' && path[n - 1] != '/' &&
            n < (LONG)sizeof(path) - 1)
            strcat(path, "/");
        snprintf(settings.dir[kind], sizeof(settings.dir[kind]), "%s", path);
        SetGad(dirgad[kind], STRINGA_TextVal, (ULONG)settings.dir[kind]);
    }
    FreeAslRequest(fr);
}

static void SaveSettings(void)
{
    LONG i;

    ReadDirs();
    for (i = 0; i < MGR_NUM_DIRS; i++)
    {
        mgr_FontsRelative(settings.dir[i], sizeof(settings.dir[i]));
        SetGad(dirgad[i], STRINGA_TextVal, (ULONG)settings.dir[i]);
    }
    settings.charset = (LONG)GetGad(GID_DEFCHARSET, CHOOSER_Selected);
    SetStatus(GetString(mgr_SaveSettings(&settings) ? MSG_SET_SAVED :
                                                       MSG_SET_SAVEERR));
}

static struct Gadget *DirField(ULONG id, LONG kind)
{
    return (struct Gadget *)StringObject,
        GA_ID, id,
        GA_TabCycle, TRUE,
        STRINGA_MaxChars, MGR_DIR_LEN - 1,
        STRINGA_TextVal, (ULONG)settings.dir[kind],
    End;
}

static Object *DirRow(ULONG field, ULONG pick, LONG kind)
{
    return HLayoutObject,
        LAYOUT_AddChild, gads[field] = DirField(field, kind),
        LAYOUT_AddChild, gads[pick] = (struct Gadget *)ButtonObject,
            GA_ID, pick, GA_RelVerify, TRUE,
            GA_Text, (ULONG)GetString(MSG_SET_PICK),
        End,
        CHILD_WeightedWidth, 0,
    End;
}

static Object *SettingsPage(void)
{
    return VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddImage, InfoLine(MSG_SET_INFO),
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_AddChild, DirRow(GID_TTFDIR, GID_TTFPICK, MGR_DIR_TTF),
            CHILD_Label, LabelObject,
                LABEL_Text, (ULONG)GetString(MSG_SET_TTFDIR),
            End,
            LAYOUT_AddChild, DirRow(GID_OTFDIR, GID_OTFPICK, MGR_DIR_OTF),
            CHILD_Label, LabelObject,
                LABEL_Text, (ULONG)GetString(MSG_SET_OTFDIR),
            End,
            LAYOUT_AddChild, DirRow(GID_TTCDIR, GID_TTCPICK, MGR_DIR_TTC),
            CHILD_Label, LabelObject,
                LABEL_Text, (ULONG)GetString(MSG_SET_TTCDIR),
            End,
            LAYOUT_AddChild, gads[GID_DEFCHARSET] =
                             (struct Gadget *)ChooserObject,
                GA_ID, GID_DEFCHARSET, GA_RelVerify, TRUE,
                CHOOSER_PopUp, TRUE,
                CHOOSER_Labels, (ULONG)&defcharsetlist,
                CHOOSER_Selected, settings.charset,
            End,
            Label(GetString(MSG_SET_CHARSET)),
        End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddImage, InfoLine(MSG_SET_NOTE),
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, SpaceObject, End,     /* takes the spare height */

        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, SpaceObject, End,
            LAYOUT_AddChild, gads[GID_SAVESET] = MakeButton(GID_SAVESET,
                                                            MSG_SET_SAVE),
            CHILD_WeightedWidth, 0,
        End,
        CHILD_WeightedHeight, 0,
    End;
}

/* ------------------------------------------------------------------ */
/* Menu: update check                                                   */
/* ------------------------------------------------------------------ */

enum { MENU_UPDATE = 1, MENU_QUIT };

static struct NewMenu menu[] =
{
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, NULL, 0, 0, (APTR)MENU_UPDATE },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, (STRPTR)"Q", 0, 0, (APTR)MENU_QUIT },
    { NM_END,   NULL, NULL, 0, 0, NULL }
};

static void LocalizeMenu(void)
{
    menu[0].nm_Label = (STRPTR)GetString(MSG_MENU_PROJECT);
    menu[1].nm_Label = (STRPTR)GetString(MSG_MENU_UPDATE);
    menu[3].nm_Label = (STRPTR)GetString(MSG_MENU_QUIT);
}

static void Requester(LONG msg, ULONG *args)
{
    struct EasyStruct es;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)GetString(MSG_MENU_UPDATE);
    es.es_TextFormat = (UBYTE *)GetString(msg);
    es.es_GadgetFormat = (UBYTE *)GetString(MSG_UPD_OK);
    EasyRequestArgs(win, &es, NULL, args);
}

/*
 * Ask GitHub for the latest release; a newer package is downloaded to
 * RAM: and its full path shown. Nothing is installed.
 */
static void CheckUpdate(void)
{
    static struct AAUpdate u;
    ULONG args[3];
    LONG r;

    SetStatus(GetString(MSG_UPD_CHECKING));
    Busy(TRUE);
    r = upd_Check(AAMGR_VERSION, &u);
    Busy(FALSE);
    SetStatus("");
    switch (r)
    {
        case UPD_NEWER:
            args[0] = (ULONG)(u.tag[0] == 'v' ? u.tag + 1 : u.tag);
            args[1] = (ULONG)AAMGR_VERSION;
            args[2] = (ULONG)u.saved;
            Requester(MSG_UPD_NEWER, args);
            break;
        case UPD_CURRENT:
            args[0] = (ULONG)AAMGR_VERSION;
            Requester(MSG_UPD_CURRENT, args);
            break;
        case UPD_NONET:
            Requester(MSG_UPD_NONET, NULL);
            break;
        case UPD_NOSSL:
            Requester(MSG_UPD_NOSSL, NULL);
            break;
        default:
            args[0] = (ULONG)u.error;
            Requester(MSG_UPD_FAILED, args);
            break;
    }
}

static BOOL OpenWin(struct Screen *scr, LONG charset)
{
    LONG i;

    LocalizeMenu();
    AddTab(&tablist, MSG_TAB_INSTALL, 0);
    AddTab(&tablist, MSG_TAB_REPAIR, 1);
    AddTab(&tablist, MSG_TAB_DIAG, 2);
    AddTab(&tablist, MSG_TAB_DOWNLOAD, 3);
    AddTab(&tablist, MSG_TAB_SETTINGS, 4);
    for (i = 0; i < AA_NUM_CHARSETS; i++)
    {
        AddChoice(&charsetlist, GetString(charset_msg[i]));
        AddChoice(&defcharsetlist, GetString(charset_msg[i]));
    }
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
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY |
                  IDCMP_MENUPICK,
        WINDOW_NewMenu, (ULONG)menu,
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
                    PAGE_Add, DiagPage(),
                    PAGE_Add, DownloadPage(),
                    PAGE_Add, SettingsPage(),
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
        case GID_DSCAN:     DiagScan();     break;
        case GID_DFONTLIST: DiagDetail();   break;
        case GID_DFIX:      DiagFix();      break;
        case GID_DREPORT:   DiagReport();   break;
        case GID_TTFPICK:   PickDir(MGR_DIR_TTF); break;
        case GID_OTFPICK:   PickDir(MGR_DIR_OTF); break;
        case GID_TTCPICK:   PickDir(MGR_DIR_TTC); break;
        case GID_SAVESET:   SaveSettings(); break;
        case GID_DLLOAD:    LoadCatalog();  break;
        case GID_DLFILTER:  ShowCatalog();  break;
        case GID_DLLIST:    DlPreview();    break;
        case GID_PVAA:
        case GID_DLAA:
            pvaa = GetGad(id, GA_Selected) != 0;
            pv_SetAntialias(pvaa);
            SetGad(id == GID_PVAA ? GID_DLAA : GID_PVAA, GA_Selected, pvaa);
            if (id == GID_PVAA)
                ShowPreview();
            else
                DlPreview();
            break;
        case GID_DLINSTALL: DlInstall();    break;
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
        { MSG_REPAIR, GID_REPAIR }, { MSG_SET_SAVE, GID_SAVESET },
        { MSG_DL_LOAD, GID_DLLOAD },
        { MSG_DL_INSTALL, GID_DLINSTALL },
    };
    struct Screen *scr = NULL;
    ULONG sigmask = 0;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    NewList(&tablist);
    NewList(&charsetlist);
    NewList(&defcharsetlist);
    NewList(&enginelist);
    NewList(&filelb);
    NewList(&repairlb);
    NewList(&diaglb);
    NewList(&dllb);

    files = AllocVec(MAX_FILES * sizeof(*files), MEMF_ANY | MEMF_CLEAR);
    diag = AllocVec(MAX_FONTS * sizeof(*diag), MEMF_ANY | MEMF_CLEAR);
    problems = AllocVec(MAX_FONTS * sizeof(*problems), MEMF_ANY | MEMF_CLEAR);
    cat = AllocVec(MAX_CAT * sizeof(*cat), MEMF_ANY | MEMF_CLEAR);
    shown = AllocVec(MAX_CAT * sizeof(*shown), MEMF_ANY | MEMF_CLEAR);
    if (!files || !diag || !problems || !cat || !shown || !OpenLibs())
        goto out;
    InitStrings(language);
    pv_Init();              /* without it the box stays empty */
    previewhook.h_Entry = (HOOKFUNC)(APTR)HookEntry;
    previewhook.h_SubEntry = (HOOKFUNC)(APTR)PreviewRender;

    /* the character set saved on the Settings tab, else ISO-8859-1 */
    mgr_LoadSettings(&settings);
    if (settings.charset < 0 || settings.charset >= AA_NUM_CHARSETS)
        settings.charset = AA_CHARSET_LATIN1;
    if (!(scr = LockPubScreen(NULL)))
        goto out;
    if (!OpenWin(scr, settings.charset))
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
                case WMHI_MENUPICK:
                {
                    UWORD mc = result & WMHI_MENUMASK;

                    while (mc != MENUNULL && !done)
                    {
                        struct MenuItem *item = ItemAddress(win->MenuStrip,
                                                            mc);

                        if (!item)
                            break;
                        if (GTMENUITEM_USERDATA(item) == (APTR)MENU_UPDATE)
                            CheckUpdate();
                        else if (GTMENUITEM_USERDATA(item) == (APTR)MENU_QUIT)
                            done = TRUE;
                        mc = item->NextSelect;
                    }
                    break;
                }
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
    FreeVec(cat);
    FreeVec(shown);
    if (diag)
        FreeVec(diag);
    if (files)
        FreeVec(files);
    return rc;
}
