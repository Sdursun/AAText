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

#include "manager.h"
#include "strings.h"
#include "preview.h"
#include "update.h"
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
    GID_DSCAN, GID_DSCANSUM, GID_DFONTLIST, GID_DDETAIL1, GID_DDETAIL2,
    GID_DDETAIL3, GID_DDETAIL4, GID_DADVICE, GID_DFIX, GID_DREPORT,
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
static struct List tablist, charsetlist, enginelist, filelb, repairlb,
                   diaglb;
static struct FileEntry *files;
static LONG numfiles;
static struct AADiagEntry *diag;
static LONG numdiag;
/* the Diagnostics tab has its own scan: the Repair list points into diag */
#define MAX_DIAG 300
static struct AADiagEntry *dscan;       /* MAX_DIAG entries */
static LONG numdscan = -1;              /* -1: not scanned yet */
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
    if (gid >= GID_DSCAN && gid <= GID_DREPORT)
        return 2;
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
    FreeListBrowserList(&diaglb);
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
            LBNA_Column, 0, LBNCA_Text, (ULONG)e->name,
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
    NewList(&diaglb);

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
    if (diag)
        FreeVec(diag);
    if (files)
        FreeVec(files);
    return rc;
}
