/*
 * AATextManager - the window (MUI 3.8 or newer).
 *
 * The same program as src/manager/gui.c (ReAction): main.c, the work
 * (src/manager/core.c), the texts and the preview are shared; only the
 * window is MUI's. Tabs: Install, Repair, Diagnostics, Download,
 * Settings; above them the character set and engine both use.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>
#include <libraries/iffparse.h>
#include <workbench/startup.h>
#include <utility/hooks.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/asl.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "../manager/manager.h"
#include "../manager/strings.h"
#include "../manager/preview.h"
#include "../manager/update.h"
#include "../manager/settings.h"
#include "../manager/core.h"
#include "../guihelp.h"
#include "../fontscan.h"
#include "../aamsg.h"
#include "../aaclient.h"
#include "../charsets.h"
#include "../prefs.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase, *AslBase, *MUIMasterBase;

/* return IDs of the objects; menu items from MENU_BASE */
enum
{
    GID_TABS = 1, GID_CHARSET, GID_ENGINE,
    GID_FILES, GID_PVAA, GID_PVSP, GID_ADD, GID_REMOVE, GID_SIZES,
    GID_OVERWRITE, GID_INSTALL,
    GID_CHECK, GID_REPAIRLIST, GID_CHANGEENGINE, GID_CHANGECHARSET,
    GID_REPAIR,
    GID_DSCAN, GID_DFONTLIST, GID_DDELETE, GID_DREPORT,
    GID_DLFILTER, GID_DLLOAD, GID_DLLIST, GID_DLAA, GID_DLSP, GID_DLINSTALL,
    GID_TTFPICK, GID_OTFPICK, GID_TTCPICK, GID_DEFCHARSET, GID_SAVESET,
    GID_COUNT
};

enum
{
    MENU_BASE = 100, MENU_UPDATE, MENU_QUIT, MENU_ABOUT,
    MENU_MUISETTINGS, MENU_HELP
};

static const LONG charset_msg[AA_NUM_CHARSETS] =
{
    MSG_CHARSET_LATIN1, MSG_CHARSET_LATIN2, MSG_CHARSET_LATIN3,
    MSG_CHARSET_LATIN4, MSG_CHARSET_LATIN5, MSG_CHARSET_LATIN9,
    MSG_CHARSET_LATIN10, MSG_CHARSET_CP1250, MSG_CHARSET_CYRILLIC,
    MSG_CHARSET_KOI8R
};

static const char *const engines[2] = { "aatext", "freetype2" };

static Object *app, *win, *obj[GID_COUNT];
static Object *status, *filelist, *repairlist, *dfontlist, *dllist,
              *pvbox, *dlbox, *gauge, *scansum, *advice, *detail[5],
              *dirfield[MGR_NUM_DIRS];
static struct MUI_CustomClass *boxclass;

static char sizestext[80] = "8-16,18,20,24";
/* the problems text of each listed font, kept while the list shows */
static char (*problems)[64];

/* ------------------------------------------------------------------ */

static BOOL OpenLibs(void)
{
    static const struct { struct Library **base; const char *name;
                          ULONG ver; } libs[] =
    {
        { (struct Library **)&IntuitionBase, "intuition.library", 40 },
        { (struct Library **)&GfxBase,       "graphics.library", 40 },
        { &UtilityBase,   "utility.library", 40 },
        { &AslBase,       "asl.library", 40 },
        { &MUIMasterBase, MUIMASTER_NAME, 19 },     /* MUI 3.8 */
    };
    ULONG i;

    for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
    {
        *libs[i].base = OpenLibrary((CONST_STRPTR)libs[i].name, libs[i].ver);
        if (!*libs[i].base)
        {
            Printf((CONST_STRPTR)"AATextManager: cannot open %s %ld\n",
                   (ULONG)libs[i].name, libs[i].ver);
            return FALSE;
        }
    }
    return TRUE;
}

static void CloseLibs(void)
{
    struct Library **bases[] =
    {
        &MUIMasterBase, &AslBase, &UtilityBase,
        (struct Library **)&GfxBase, (struct Library **)&IntuitionBase
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
/* Small object makers (MUI_MakeObject() without varargs)              */
/* ------------------------------------------------------------------ */

static Object *Make(LONG type, ULONG a, ULONG b, ULONG c)
{
    ULONG params[3];

    params[0] = a;
    params[1] = b;
    params[2] = c;
    return MUI_MakeObjectA(type, params);
}

static Object *MkButton(LONG msg, LONG id)
{
    obj[id] = Make(MUIO_Button, (ULONG)GetString(msg), 0, 0);
    if (obj[id])
        set(obj[id], MUIA_HorizWeight, 0);
    return obj[id];
}

static Object *MkLabel(LONG msg, ULONG flags)
{
    return Make(MUIO_Label, (ULONG)GetString(msg), flags, 0);
}

/* a checkmark with its text on the right */
static Object *MkCheck(LONG msg, LONG id, BOOL on)
{
    obj[id] = Make(MUIO_Checkmark, (ULONG)GetString(msg), 0, 0);
    if (obj[id])
        set(obj[id], MUIA_Selected, on);
    return HGroup,
        Child, obj[id],
        Child, MkLabel(msg, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame),
        Child, HSpace(0),
    End;
}

static Object *MkCycle(LONG msg, LONG id, const char **entries, LONG active)
{
    obj[id] = Make(MUIO_Cycle, (ULONG)GetString(msg), (ULONG)entries, 0);
    if (obj[id])
        set(obj[id], MUIA_Cycle_Active, active);
    return obj[id];
}

/* plain text; "FONTS:_ttf/" keeps its "_" */
static Object *InfoText(const char *text)
{
    return TextObject,
        MUIA_Text_Contents, (ULONG)text,
        MUIA_Text_PreParse, (ULONG)"",
    End;
}

/* a framed text field for the font details */
static Object *DetailText(void)
{
    return TextObject,
        TextFrame,
        MUIA_Background, MUII_TextBack,
        MUIA_Text_Contents, (ULONG)"",
        MUIA_Text_PreParse, (ULONG)"",
    End;
}

/* a list in its listview; display draws the entries */
static Object *MkList(Object **list, struct Hook *display, const char *format,
                      BOOL multi)
{
    return ListviewObject,
        MUIA_CycleChain, TRUE,
        multi ? MUIA_Listview_MultiSelect : TAG_IGNORE,
            MUIV_Listview_MultiSelect_Shifted,
        MUIA_Listview_List, *list = ListObject,
            InputListFrame,
            MUIA_List_Format, (ULONG)format,
            MUIA_List_Title, TRUE,
            MUIA_List_DisplayHook, (ULONG)display,
        End,
    End;
}

static ULONG Get(Object *o, ULONG attr)
{
    ULONG v = 0;

    if (o)
        get(o, attr, &v);
    return v;
}

static struct Window *IWin(void)
{
    return (struct Window *)Get(win, MUIA_Window_Window);
}

static void SetStatus(const char *text)
{
    if (status)
        set(status, MUIA_Text_Contents, (ULONG)text);
}

static void Busy(BOOL on)
{
    set(app, MUIA_Application_Sleep, on);
}

/* A requester with gadgets "A|B|C"; 1, 2, ... for them, 0 the last. */
static LONG Ask(LONG title, LONG text, LONG gadgets, APTR args)
{
    return MUI_RequestA(app, win, 0, (char *)GetString(title),
                        (char *)GetString(gadgets), (char *)GetString(text),
                        args);
}

static LONG Charset(void)
{
    return (LONG)Get(obj[GID_CHARSET], MUIA_Cycle_Active);
}

static const char *Engine(void)
{
    return engines[Get(obj[GID_ENGINE], MUIA_Cycle_Active) ? 1 : 0];
}

static LONG CurrentPage(void)
{
    return (LONG)Get(obj[GID_TABS], MUIA_Group_ActivePage);
}

/* ------------------------------------------------------------------ */
/* Preview boxes                                                        */
/* ------------------------------------------------------------------ */

/* The Install and Download tabs share one preview buffer: the page
   whose font it holds (0 or 3), -1 for none */
static LONG pvpage = -1;

#define MUIA_AABox_Page (TAG_USER | 0x4142)

struct BoxData
{
    LONG page;
};

static ULONG BoxNew(struct IClass *cl, Object *o, struct opSet *msg)
{
    struct BoxData *d;

    if (!(o = (Object *)DoSuperMethodA(cl, o, (Msg)msg)))
        return 0;
    d = INST_DATA(cl, o);
    d->page = GetTagData(MUIA_AABox_Page, 0, msg->ops_AttrList);
    return (ULONG)o;
}

static ULONG BoxAskMinMax(struct IClass *cl, Object *o,
                          struct MUIP_AskMinMax *msg)
{
    LONG h = pv_NeededHeight();

    DoSuperMethodA(cl, o, (Msg)msg);
    msg->MinMaxInfo->MinWidth += 300;
    msg->MinMaxInfo->DefWidth += 500;
    msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    msg->MinMaxInfo->MinHeight += h;
    msg->MinMaxInfo->DefHeight += h;
    msg->MinMaxInfo->MaxHeight += h;
    return 0;
}

static ULONG BoxDraw(struct IClass *cl, Object *o, struct MUIP_Draw *msg)
{
    struct BoxData *d = INST_DATA(cl, o);

    DoSuperMethodA(cl, o, (Msg)msg);
    /* the buffer holds the other tab's font: an empty box until this
       tab's font is rendered again */
    if (!(msg->flags & MADF_DRAWOBJECT) || pvpage != d->page)
        return 0;
    pv_Draw(_rp(o), _mleft(o), _mtop(o), _mwidth(o), _mheight(o),
            _dri(o)->dri_Pens[BACKGROUNDPEN], _dri(o)->dri_Pens[TEXTPEN],
            GetString(MSG_PV_NOTRTG));
    return 0;
}

static ULONG BoxDispatcher(struct IClass *cl, Object *o, Msg msg)
{
    switch (msg->MethodID)
    {
        case OM_NEW:
            return BoxNew(cl, o, (struct opSet *)msg);
        case MUIM_AskMinMax:
            return BoxAskMinMax(cl, o, (struct MUIP_AskMinMax *)msg);
        case MUIM_Draw:
            return BoxDraw(cl, o, (struct MUIP_Draw *)msg);
    }
    return DoSuperMethodA(cl, o, msg);
}

/* MUI calls dispatchers with the class in a0, object a2, message a1 */
static ULONG BoxEntry(void)
{
    register struct IClass *cl __asm("a0");
    register Object *o __asm("a2");
    register Msg msg __asm("a1");

    return BoxDispatcher(cl, o, msg);
}

static Object *PreviewBox(LONG page)
{
    return NewObject(boxclass->mcc_Class, NULL,
                     MUIA_AABox_Page, page,
                     MUIA_Background, MUII_GroupBack,
                     TAG_DONE);
}

/* Render font file path (NULL: nothing) for page and show it. */
static void Render(LONG page, const char *path, LONG face)
{
    struct Window *w = IWin();

    if (!w)
        return;
    Busy(TRUE);
    if (path)
        pv_Render(w->WScreen, path, face, GetString(MSG_PV_ALPHABET),
                  GetString(MSG_PV_SENTENCE));
    else
        pv_Render(w->WScreen, NULL, 0, NULL, NULL);
    pvpage = page;
    Busy(FALSE);
    if (CurrentPage() == page)
        MUI_Redraw(page == 0 ? pvbox : dlbox, MADF_DRAWOBJECT);
}

/* ------------------------------------------------------------------ */
/* Install tab                                                          */
/* ------------------------------------------------------------------ */

static ULONG FileDisplay(char **array, const struct FileEntry *f)
{
    if (!f)
    {
        array[0] = (char *)GetString(MSG_COL_FAMILY);
        array[1] = (char *)GetString(MSG_COL_STYLE);
        array[2] = (char *)GetString(MSG_COL_FACE);
        array[3] = (char *)GetString(MSG_COL_FILE);
        return 0;
    }
    array[0] = (char *)f->info.family;
    array[1] = (char *)f->info.style;
    array[2] = (char *)f->facetext;
    array[3] = (char *)FilePart((CONST_STRPTR)f->path);
    return 0;
}

static ULONG FileDisplayEntry(void)
{
    register char **array __asm("a2");
    register const struct FileEntry *f __asm("a1");

    return FileDisplay(array, f);
}

static void ShowPreview(void)
{
    LONG sel = (LONG)Get(filelist, MUIA_List_Active);

    if (sel >= 0 && sel < mcore.numfiles)
        Render(0, mcore.files[sel].path, mcore.files[sel].face);
    else
        Render(0, NULL, 0);
}

static void ShowFiles(void)
{
    LONG i;

    set(filelist, MUIA_List_Quiet, TRUE);
    DoMethod(filelist, MUIM_List_Clear);
    for (i = 0; i < mcore.numfiles; i++)
        DoMethod(filelist, MUIM_List_InsertSingle, (ULONG)&mcore.files[i],
                 MUIV_List_Insert_Bottom);
    set(filelist, MUIA_List_Quiet, FALSE);
    ShowPreview();
}

static void AddFiles(void)
{
    struct FileRequester *fr;
    char path[256], msg[200];
    LONG i;

    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)IWin(),
                             ASLFR_TitleText, (ULONG)GetString(MSG_ADD_TITLE),
                             ASLFR_DoMultiSelect, TRUE,
                             ASLFR_RejectIcons, TRUE,
                             ASLFR_DoPatterns, TRUE,
                             ASLFR_InitialPattern,
                                 (ULONG)"#?.(ttf|otf|ttc|otc)",
                             TAG_DONE);
    if (!fr)
        return;
    Busy(TRUE);
    if (AslRequest(fr, NULL))
    {
        msg[0] = 0;
        for (i = 0; i < fr->fr_NumArgs; i++)
        {
            struct WBArg *a = &fr->fr_ArgList[i];

            if (!NameFromLock(a->wa_Lock, (STRPTR)path, sizeof(path)) ||
                !AddPart((STRPTR)path, a->wa_Name, sizeof(path)))
                continue;
            if (!mc_AddFile(path))
                snprintf(msg, sizeof(msg), GetString(MSG_NOTFONT),
                         (const char *)a->wa_Name);
        }
        Busy(FALSE);
        ShowFiles();
        SetStatus(msg);
    }
    else
        Busy(FALSE);
    FreeAslRequest(fr);
}

static void RemoveFile(void)
{
    LONG sel = (LONG)Get(filelist, MUIA_List_Active);

    if (sel < 0 || sel >= mcore.numfiles)
        return;
    mc_RemoveFile(sel);
    ShowFiles();
}

/* the Settings tab's directories, as they are typed */
static void ReadDirs(void)
{
    const char *t;
    LONG i;

    for (i = 0; i < MGR_NUM_DIRS; i++)
        if ((t = (const char *)Get(dirfield[i], MUIA_String_Contents)) &&
            t[0])
            snprintf(mcore.settings.dir[i], sizeof(mcore.settings.dir[i]),
                     "%s", t);
}

/* "3 installed, 1 skipped..." and the font diskfont cannot open */
static void ShowResult(const struct MCResult *r)
{
    char msg[200];

    snprintf(msg, sizeof(msg), GetString(MSG_INSTALLED), (long)r->ok,
             (long)r->skipped, (long)r->failed);
    if (r->notopen[0])
    {
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), " ");
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg),
                 GetString(MSG_NOTOPEN), r->notopen);
    }
    SetStatus(msg);
}

/* The install settings of the window into in; FALSE: sizes wrong */
static BOOL InstallSettings(struct AAInstall *in)
{
    aa_InstallDefaults(in);
    if (!mgr_ParseSizes((const char *)Get(obj[GID_SIZES],
                                          MUIA_String_Contents), in))
    {
        SetStatus(GetString(MSG_BADSIZES));
        return FALSE;
    }
    in->charset = Charset();
    in->engine = Engine();
    in->overwrite = Get(obj[GID_OVERWRITE], MUIA_Selected) != 0;
    ReadDirs();
    return TRUE;
}

static void Install(void)
{
    static struct AAInstall in;
    struct MCResult r;

    if (!mcore.numfiles)
    {
        SetStatus(GetString(MSG_NOFONTS));
        return;
    }
    if (!InstallSettings(&in))
        return;
    memset(&r, 0, sizeof(r));
    Busy(TRUE);
    mc_InstallAll(&in, &r);
    Busy(FALSE);
    ShowResult(&r);
    if (!mcore.numfiles)
        ShowFiles();
}

/* "Antialiasing" and "Subpixel" of the previews, shared by both tabs */
static BOOL pvaa = TRUE;
static UBYTE pvsp = PV_SUBPIXEL_OFF;
static const char *spnames[4];

static Object *AAOptions(LONG aaid, LONG spid)
{
    return HGroup,
        Child, MkCheck(MSG_PV_AA, aaid, pvaa),
        Child, MkLabel(MSG_PV_SP, MUIO_Label_DoubleFrame),
        Child, MkCycle(MSG_PV_SP, spid, spnames, pvsp),
    End;
}

static Object *InstallPage(void)
{
    static struct Hook display;

    display.h_Entry = (HOOKFUNC)FileDisplayEntry;

    return VGroup,
        Child, InfoText(GetString(MSG_INFO_INSTALL)),
        Child, MkList(&filelist, &display, "BAR,BAR,BAR,", FALSE),
        Child, HGroup,
            Child, MkButton(MSG_ADD, GID_ADD),
            Child, HSpace(0),
            Child, MkButton(MSG_REMOVE, GID_REMOVE),
        End,
        Child, VGroup,
            GroupFrameT(GetString(MSG_PREVIEW)),
            Child, pvbox = PreviewBox(0),
            Child, AAOptions(GID_PVAA, GID_PVSP),
        End,
        Child, HGroup,
            Child, MkLabel(MSG_SIZES, MUIO_Label_DoubleFrame),
            Child, obj[GID_SIZES] = StringObject,
                StringFrame,
                MUIA_String_MaxLen, sizeof(sizestext),
                MUIA_String_Contents, (ULONG)sizestext,
                MUIA_CycleChain, TRUE,
            End,
        End,
        Child, MkCheck(MSG_OVERWRITE, GID_OVERWRITE, FALSE),
        Child, HGroup,
            Child, MkButton(MSG_INSTALL, GID_INSTALL),
            Child, HSpace(0),
        End,
    End;
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
    if (Get(obj[GID_CHANGEENGINE], MUIA_Selected))
        r->what |= AA_REPAIR_ENGINE;
    if (Get(obj[GID_CHANGECHARSET], MUIA_Selected))
        r->what |= AA_REPAIR_CHARSET;
}

/* "font file moved, no code page" */
static void Problems(ULONG what, char *buf, LONG len)
{
    static const LONG msg[6] =
        { MSG_P_MOVED, MSG_P_CODEPAGE, MSG_P_ENGINE, MSG_P_HEIGHT,
          MSG_P_WIDTH, MSG_P_CHARSET };
    LONG i;

    buf[0] = 0;
    for (i = 0; i < 6; i++)
        if (what & (1 << i))
            snprintf(buf + strlen(buf), len - strlen(buf), "%s%s",
                     buf[0] ? ", " : "", GetString(msg[i]));
}

/* entries: the fonts in mcore.diag */
static ULONG RepairDisplay(char **array, const struct AADiagEntry *e)
{
    if (!e)
    {
        array[0] = (char *)GetString(MSG_COL_FONT);
        array[1] = (char *)GetString(MSG_COL_PROBLEM);
        return 0;
    }
    array[0] = (char *)mc_FontLabel(e);
    array[1] = problems[e - mcore.diag];
    return 0;
}

static ULONG RepairDisplayEntry(void)
{
    register char **array __asm("a2");
    register const struct AADiagEntry *e __asm("a1");

    return RepairDisplay(array, e);
}

static LONG CheckFonts(void)
{
    struct AARepair r;
    char msg[200];
    LONG i, todo;

    DoMethod(repairlist, MUIM_List_Clear);
    Repair_Settings(&r);
    Busy(TRUE);
    todo = mc_Check(&r);
    Busy(FALSE);
    /* mc_Check() drops the Diagnostics scan */
    DoMethod(dfontlist, MUIM_List_Clear);
    set(repairlist, MUIA_List_Quiet, TRUE);
    for (i = 0; i < mcore.numdiag; i++)
    {
        if (!mcore.need[i])
            continue;
        Problems(mcore.need[i], problems[i], sizeof(problems[i]));
        DoMethod(repairlist, MUIM_List_InsertSingle, (ULONG)&mcore.diag[i],
                 MUIV_List_Insert_Bottom);
    }
    set(repairlist, MUIA_List_Quiet, FALSE);
    if (todo < 0)
        SetStatus(GetString(MSG_FONTSERR));
    else if (!todo)
        SetStatus(GetString(MSG_NOTHING));
    else
    {
        snprintf(msg, sizeof(msg), GetString(MSG_TOREPAIR), (long)todo);
        SetStatus(msg);
    }
    set(obj[GID_REPAIR], MUIA_Disabled, todo <= 0);
    return todo > 0 ? todo : 0;
}

static void RepairAll(void)
{
    struct AARepair r;
    char msg[200];
    ULONG args[1];
    LONG todo, done;

    /* again, with the settings as they are now */
    if (!(todo = CheckFonts()))
        return;
    args[0] = todo;
    if (Ask(MSG_TITLE, MSG_REPAIR_ASK, MSG_REPAIR_GADS, args) != 1)
        return;
    Repair_Settings(&r);
    Busy(TRUE);
    done = mc_RepairAll(&r);
    Busy(FALSE);
    CheckFonts();
    snprintf(msg, sizeof(msg), GetString(MSG_REPAIRED), (long)done,
             (long)todo);
    SetStatus(msg);
}

static Object *RepairPage(void)
{
    static struct Hook display;

    display.h_Entry = (HOOKFUNC)RepairDisplayEntry;

    return VGroup,
        Child, InfoText(GetString(MSG_INFO_REPAIR)),
        Child, MkCheck(MSG_CHANGE_ENGINE, GID_CHANGEENGINE, TRUE),
        /* off by default: a Cyrillic or Greek font may be meant so */
        Child, MkCheck(MSG_CHANGE_CHARSET, GID_CHANGECHARSET, FALSE),
        Child, MkList(&repairlist, &display, "BAR,", FALSE),
        Child, HGroup,
            Child, MkButton(MSG_CHECK, GID_CHECK),
            Child, HSpace(0),
            Child, MkButton(MSG_REPAIR, GID_REPAIR),
        End,
    End;
}

/* ------------------------------------------------------------------ */
/* Diagnostics tab                                                      */
/* ------------------------------------------------------------------ */

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

static ULONG DiagDisplay(char **array, const struct AADiagEntry *e)
{
    if (!e)
    {
        array[0] = (char *)GetString(MSG_DIAG_COL_FONT);
        array[1] = (char *)GetString(MSG_DIAG_COL_STATUS);
        array[2] = (char *)GetString(MSG_DIAG_COL_FILE);
        return 0;
    }
    array[0] = (char *)mc_FontLabel(e);
    array[1] = (char *)GetString(diag_msg[e->status]);
    array[2] = (char *)(e->found[0] ? e->found : e->want);
    return 0;
}

static ULONG DiagDisplayEntry(void)
{
    register char **array __asm("a2");
    register const struct AADiagEntry *e __asm("a1");

    return DiagDisplay(array, e);
}

/*
 * Show the selected font under the list: its .otag, the font file
 * named there, where it really is, the OT_Engine with its library, the
 * character set, and what to do about it.
 */
static void DiagDetail(void)
{
    static char text[5][AA_FONTFILE_LEN], adv[128];
    LONG sel = (LONG)Get(dfontlist, MUIA_List_Active), i;
    const struct AADiagEntry *e;

    for (i = 0; i < 5; i++)
        text[i][0] = 0;
    adv[0] = 0;
    if (sel >= 0 && sel < mcore.numdscan)
    {
        e = &mcore.dscan[sel];
        snprintf(text[0], sizeof(text[0]), "%s", e->otag);
        snprintf(text[1], sizeof(text[1]), "%s", e->want);
        snprintf(text[2], sizeof(text[2]), "%s", e->found);
        if (e->engine[0] && e->enginever[0])
            snprintf(text[3], sizeof(text[3]),
                     GetString(MSG_DIAG_ENGINEFMT), e->engine, e->engine,
                     e->enginever);
        else if (e->engine[0])
            snprintf(text[3], sizeof(text[3]),
                     GetString(MSG_DIAG_ENGINENONE), e->engine, e->engine);
        if (e->charset >= 0)
            snprintf(text[4], sizeof(text[4]), GetString(MSG_DIAG_CS_OTAG),
                     GetString(charset_msg[e->charset]));
        else if (e->charset == AA_CS_OWN)
            snprintf(text[4], sizeof(text[4]), GetString(MSG_DIAG_CS_OWN),
                     GetString(charset_msg[e->nearcharset]),
                     (long)e->neardiff);
        else if (e->charset == AA_CS_NONE)
        {
            /* AAText then uses its own "charset" setting */
            static struct AAPrefs p;

            aa_ReadPrefs(&p, NULL, FALSE);
            snprintf(text[4], sizeof(text[4]), GetString(MSG_DIAG_CS_NONE),
                     GetString(charset_msg[p.charset < AA_NUM_CHARSETS ?
                                           p.charset : 0]));
        }
        snprintf(adv, sizeof(adv), GetString(diag_advice[e->status]),
                 e->status == AA_DIAG_OTHER ? e->engine : "");
        /* without the engine's library diskfont cannot make the font
           for any program, even when the file is fine */
        if (e->engine[0] && !e->enginever[0])
            snprintf(adv, sizeof(adv), GetString(MSG_DIAG_NOENGINE),
                     e->engine);
    }
    for (i = 0; i < 5; i++)
        set(detail[i], MUIA_Text_Contents, (ULONG)text[i]);
    set(advice, MUIA_Text_Contents, (ULONG)adv);
    set(obj[GID_DDELETE], MUIA_Disabled, sel < 0 || sel >= mcore.numdscan);
}

static void DiagShow(void)
{
    char sum[96];
    LONG i, ok = 0;

    set(dfontlist, MUIA_List_Quiet, TRUE);
    DoMethod(dfontlist, MUIM_List_Clear);
    for (i = 0; i < mcore.numdscan; i++)
    {
        const struct AADiagEntry *e = &mcore.dscan[i];

        DoMethod(dfontlist, MUIM_List_InsertSingle, (ULONG)e,
                 MUIV_List_Insert_Bottom);
        if (e->status == AA_DIAG_OK || e->status == AA_DIAG_OTHER)
            ok++;
    }
    set(dfontlist, MUIA_List_Quiet, FALSE);
    if (mcore.numdscan < 0)
        snprintf(sum, sizeof(sum), "%s", GetString(MSG_DIAG_NOFONTS));
    else if (mcore.numdscan == MC_MAX_DIAG)
        snprintf(sum, sizeof(sum), GetString(MSG_DIAG_LISTFULL),
                 (long)MC_MAX_DIAG);
    else
        snprintf(sum, sizeof(sum), GetString(MSG_DIAG_SUMMARY),
                 (long)mcore.numdscan, (long)ok,
                 (long)(mcore.numdscan - ok));
    set(scansum, MUIA_Text_Contents, (ULONG)sum);
    DiagDetail();
}

static void DiagScan(void)
{
    Busy(TRUE);
    mc_Scan();
    Busy(FALSE);
    DiagShow();
}

/*
 * Delete the selected font after asking: its .otag, .font and size
 * directory, and if wanted the font file too (not when another font
 * uses it).
 */
static void DiagDelete(void)
{
    static char label[64], failed[AA_FONTFILE_LEN + 16];
    char msg[200];
    const struct AADiagEntry *other = NULL;
    struct AADiagEntry *e;
    const char *file;
    LONG sel = (LONG)Get(dfontlist, MUIA_List_Active), r;
    ULONG args[3];
    BOOL withfile;

    if (sel < 0 || sel >= mcore.numdscan)
        return;
    e = &mcore.dscan[sel];
    file = mc_DeletableFile(e);
    if (file[0])
        other = mc_SharedBy(e, file);

    snprintf(label, sizeof(label), "%s", mc_FontLabel(e));
    args[0] = (ULONG)label;
    args[1] = (ULONG)e->otag;
    args[2] = (ULONG)(file[0] ? file : "-");
    /* the font file only when there is one that no other font uses */
    r = Ask(MSG_TITLE, MSG_DEL_ASK,
            file[0] && !other ? MSG_DEL_GADS3 : MSG_DEL_GADS2, args);
    if (r == 0)
        return;
    withfile = r == 2;

    if (!mc_DeleteFont(sel, withfile, failed, sizeof(failed)))
        snprintf(msg, sizeof(msg), GetString(MSG_DEL_FAILED), failed);
    else if (other)
        snprintf(msg, sizeof(msg), GetString(MSG_DEL_SHARED),
                 mc_FontLabel(other));
    else
        snprintf(msg, sizeof(msg), GetString(withfile ? MSG_DEL_DONEFILE :
                                                        MSG_DEL_DONE), label);
    DiagScan();
    SetStatus(msg);
}

static void DiagReport(void)
{
    static char path[AA_PATH_LEN];
    struct FileRequester *fr;
    char msg[AA_PATH_LEN + 40];

    if (mcore.numdscan < 0)
        DiagScan();
    if (mcore.numdscan < 0)
        return;
    fr = AllocAslRequestTags(ASL_FileRequest,
                             ASLFR_Window, (ULONG)IWin(),
                             ASLFR_TitleText,
                                 (ULONG)GetString(MSG_DIAG_SAVE_TITLE),
                             ASLFR_InitialDrawer, (ULONG)"RAM:",
                             ASLFR_InitialFile, (ULONG)"AAText-fonts.txt",
                             ASLFR_DoSaveMode, TRUE,
                             ASLFR_RejectIcons, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    Busy(TRUE);
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
    {
        snprintf(path, sizeof(path), "%s", (char *)fr->fr_Drawer);
        AddPart((STRPTR)path, fr->fr_File, sizeof(path));
        snprintf(msg, sizeof(msg), GetString(mc_WriteReport(path) ?
                 MSG_DIAG_SAVED : MSG_DIAG_WRITEERR), path);
        SetStatus(msg);
    }
    Busy(FALSE);
    FreeAslRequest(fr);
}

static Object *DiagPage(void)
{
    static struct Hook display;
    static const LONG labels[5] =
    {
        MSG_DIAG_L_OTAG, MSG_DIAG_L_WANT, MSG_DIAG_L_FOUND,
        MSG_DIAG_L_ENGINE, MSG_DIAG_L_CHARSET
    };
    Object *fields;
    LONG i;

    display.h_Entry = (HOOKFUNC)DiagDisplayEntry;

    fields = ColGroup(2), End;
    for (i = 0; fields && i < 5; i++)
    {
        /* text, not a label: "OT_Engine" keeps its "_" */
        DoMethod(fields, OM_ADDMEMBER, (ULONG)TextObject,
                     MUIA_Text_Contents, (ULONG)GetString(labels[i]),
                     MUIA_Text_PreParse, (ULONG)"r",
                     MUIA_HorizWeight, 0,
                 End);
        DoMethod(fields, OM_ADDMEMBER, (ULONG)(detail[i] = DetailText()));
    }

    return VGroup,
        Child, InfoText(GetString(MSG_DIAG_INFO)),
        Child, HGroup,
            Child, MkButton(MSG_DIAG_SCAN, GID_DSCAN),
            Child, scansum = InfoText(""),
        End,
        Child, MkList(&dfontlist, &display, "BAR,BAR,", FALSE),
        Child, fields,
        Child, advice = InfoText(""),
        Child, HGroup,
            Child, MkButton(MSG_DEL, GID_DDELETE),
            Child, HSpace(0),
            Child, MkButton(MSG_DIAG_REPORT, GID_DREPORT),
        End,
    End;
}

/* ------------------------------------------------------------------ */
/* Download tab                                                         */
/* ------------------------------------------------------------------ */

static ULONG CatDisplay(char **array, const struct CatEntry *e)
{
    if (!e)
    {
        array[0] = (char *)GetString(MSG_COL_FAMILY);
        array[1] = (char *)GetString(MSG_COL_STYLE);
        array[2] = (char *)GetString(MSG_DL_COL_LIC);
        array[3] = (char *)GetString(MSG_DL_COL_SIZE);
        return 0;
    }
    array[0] = (char *)e->family;
    array[1] = (char *)e->style;
    array[2] = (char *)e->licence;
    array[3] = (char *)e->size;
    return 0;
}

static ULONG CatDisplayEntry(void)
{
    register char **array __asm("a2");
    register const struct CatEntry *e __asm("a1");

    return CatDisplay(array, e);
}

/* The catalog rows whose family matches the search text */
static void ShowCatalog(void)
{
    LONG i;

    mc_Filter((const char *)Get(obj[GID_DLFILTER], MUIA_String_Contents));
    set(dllist, MUIA_List_Quiet, TRUE);
    DoMethod(dllist, MUIM_List_Clear);
    for (i = 0; i < mcore.numshown; i++)
        DoMethod(dllist, MUIM_List_InsertSingle,
                 (ULONG)&mcore.cat[mcore.shown[i]], MUIV_List_Insert_Bottom);
    set(dllist, MUIA_List_Quiet, FALSE);
}

/* Explain why the network could not be opened (or the load failed) */
static void NetFailed(LONG r, const char *error)
{
    char msg[200];

    if (r == UPD_NONET)
        SetStatus(GetString(MSG_DL_NONET));
    else if (r == UPD_NOSSL)
        SetStatus(GetString(MSG_DL_NOSSL));
    else
    {
        snprintf(msg, sizeof(msg), GetString(MSG_DL_LOADFAIL), error);
        SetStatus(msg);
    }
}

static void LoadCatalog(void)
{
    char msg[200], error[160];
    LONG r;

    SetStatus(GetString(MSG_DL_LOADING));
    Busy(TRUE);
    r = mc_LoadCatalog(error, sizeof(error));
    Busy(FALSE);
    if (r)
    {
        NetFailed(r, error);
        return;
    }
    ShowCatalog();
    snprintf(msg, sizeof(msg), GetString(MSG_DL_LOADED), (long)mcore.numcat);
    SetStatus(msg);
}

/* mc_Fetch(), the reason in the status line when it fails */
static BOOL Fetch(struct AANet *net, const struct CatEntry *e, char *path,
                  LONG len)
{
    char msg[200];

    if (mc_Fetch(net, e, path, len))
        return TRUE;
    snprintf(msg, sizeof(msg), GetString(MSG_DL_GETFAIL),
             FilePart((CONST_STRPTR)e->path), net->error);
    SetStatus(msg);
    return FALSE;
}

/* The selected rows (shift-click selects several, a click one) */
static LONG NextSelected(LONG pos)
{
    DoMethod(dllist, MUIM_List_NextSelected, (ULONG)&pos);
    return pos;
}

static LONG CountSelected(void)
{
    LONG pos, n = 0;

    for (pos = NextSelected(MUIV_List_NextSelected_Start);
         pos != MUIV_List_NextSelected_End; pos = NextSelected(pos))
        n++;
    return n;
}

/* The catalog index of the only selected row; -1 for none or several */
static LONG OnlySelected(void)
{
    LONG pos = NextSelected(MUIV_List_NextSelected_Start);

    if (pos == MUIV_List_NextSelected_End || pos < 0 ||
        pos >= mcore.numshown ||
        NextSelected(pos) != MUIV_List_NextSelected_End)
        return -1;
    return mcore.shown[pos];
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
        Render(3, NULL, 0);
        return;
    }
    if (mc_LocalCopy(&mcore.cat[i], path, sizeof(path)))
    {
        /* there already: no network needed */
        Render(3, path, 0);
        return;
    }
    if ((r = net_Open(&net, AAMGR_VERSION)))
    {
        NetFailed(r, net.error);
        return;
    }
    Busy(TRUE);
    ok = Fetch(&net, &mcore.cat[i], path, sizeof(path));
    net_Close(&net);
    Busy(FALSE);
    if (ok)
    {
        SetStatus("");
        Render(3, path, 0);
    }
    else
        Render(3, NULL, 0);
}

/* Back on the Download tab: its selected font again if it is downloaded
   already (no network for a tab switch), else nothing */
static void DlShowAgain(void)
{
    static char path[256];
    LONG i = OnlySelected();

    if (i >= 0 && mc_LocalCopy(&mcore.cat[i], path, sizeof(path)))
        Render(3, path, 0);
    else
        Render(3, NULL, 0);
}

static void DlInstall(void)
{
    static struct AAInstall in;
    static struct AANet net;
    static char path[256];
    char msg[200];
    struct MCResult res;
    LONG pos, todo = CountSelected(), done = 0, r;

    if (!todo)
    {
        SetStatus(GetString(MSG_DL_NONE));
        return;
    }
    if (!InstallSettings(&in))
        return;
    if ((r = net_Open(&net, AAMGR_VERSION)))
    {
        NetFailed(r, net.error);
        return;
    }

    memset(&res, 0, sizeof(res));
    Busy(TRUE);
    set(gauge, MUIA_Gauge_Max, todo);
    set(gauge, MUIA_Gauge_Current, 0);
    for (pos = NextSelected(MUIV_List_NextSelected_Start);
         pos != MUIV_List_NextSelected_End; pos = NextSelected(pos))
    {
        const struct CatEntry *e;

        if (pos < 0 || pos >= mcore.numshown)
            continue;
        e = &mcore.cat[mcore.shown[pos]];
        snprintf(msg, sizeof(msg), GetString(MSG_DL_GETTING), (long)++done,
                 (long)todo, FilePart((CONST_STRPTR)e->path));
        SetStatus(msg);
        /* let the window show the status and the gauge */
        DoMethod(app, MUIM_Application_InputBuffered);
        if (!Fetch(&net, e, path, sizeof(path)))
            res.failed++;
        else
        {
            mc_InstallOne(&in, path, 0, &res);
            DeleteFile((CONST_STRPTR)path);    /* a copy is in fontdir */
        }
        set(gauge, MUIA_Gauge_Current, done);
    }
    net_Close(&net);
    Busy(FALSE);
    if (res.ok)
        mc_TellAAText();
    ShowResult(&res);
}

static Object *DownloadPage(void)
{
    static struct Hook display;

    display.h_Entry = (HOOKFUNC)CatDisplayEntry;

    return VGroup,
        Child, InfoText(GetString(MSG_DL_INFO)),
        Child, HGroup,
            Child, MkLabel(MSG_DL_FILTER, MUIO_Label_DoubleFrame),
            Child, obj[GID_DLFILTER] = StringObject,
                StringFrame,
                MUIA_String_MaxLen, 40,
                MUIA_CycleChain, TRUE,
            End,
            Child, MkButton(MSG_DL_LOAD, GID_DLLOAD),
        End,
        Child, MkList(&dllist, &display, "BAR,BAR,BAR,", TRUE),
        Child, VGroup,
            GroupFrameT(GetString(MSG_PREVIEW)),
            Child, dlbox = PreviewBox(3),
            Child, AAOptions(GID_DLAA, GID_DLSP),
        End,
        Child, HGroup,
            Child, gauge = GaugeObject,
                GaugeFrame,
                MUIA_Gauge_Horiz, TRUE,
                MUIA_Gauge_Max, 1,
                MUIA_FixHeightTxt, (ULONG)"X",
            End,
            Child, MkButton(MSG_DL_INSTALL, GID_DLINSTALL),
        End,
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
                             ASLFR_Window, (ULONG)IWin(),
                             ASLFR_TitleText,
                                 (ULONG)GetString(MSG_SET_PICKTITLE),
                             ASLFR_InitialDrawer,
                                 (ULONG)mcore.settings.dir[kind],
                             ASLFR_DrawersOnly, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    Busy(TRUE);
    if (AslRequest(fr, NULL) && fr->fr_Drawer && fr->fr_Drawer[0])
    {
        snprintf(path, sizeof(path), "%s", (char *)fr->fr_Drawer);
        mgr_FontsRelative(path, sizeof(path));
        n = strlen(path);
        if (n && path[n - 1] != ':' && path[n - 1] != '/' &&
            n < (LONG)sizeof(path) - 1)
            strcat(path, "/");
        snprintf(mcore.settings.dir[kind], sizeof(mcore.settings.dir[kind]),
                 "%s", path);
        set(dirfield[kind], MUIA_String_Contents,
            (ULONG)mcore.settings.dir[kind]);
    }
    Busy(FALSE);
    FreeAslRequest(fr);
}

static void SaveSettings(void)
{
    LONG i;

    ReadDirs();
    for (i = 0; i < MGR_NUM_DIRS; i++)
    {
        mgr_FontsRelative(mcore.settings.dir[i],
                          sizeof(mcore.settings.dir[i]));
        set(dirfield[i], MUIA_String_Contents,
            (ULONG)mcore.settings.dir[i]);
    }
    mcore.settings.charset = (LONG)Get(obj[GID_DEFCHARSET],
                                       MUIA_Cycle_Active);
    SetStatus(GetString(mgr_SaveSettings(&mcore.settings) ? MSG_SET_SAVED :
                                                            MSG_SET_SAVEERR));
}

static Object *DirRow(LONG kind, LONG pick)
{
    return HGroup,
        Child, dirfield[kind] = StringObject,
            StringFrame,
            MUIA_String_MaxLen, MGR_DIR_LEN,
            MUIA_String_Contents, (ULONG)mcore.settings.dir[kind],
            MUIA_CycleChain, TRUE,
        End,
        Child, MkButton(MSG_SET_PICK, pick),
    End;
}

static const char *charsets[AA_NUM_CHARSETS + 1], *enginenames[3],
                  *tabs[6];

static Object *SettingsPage(void)
{
    return VGroup,
        Child, InfoText(GetString(MSG_SET_INFO)),
        Child, ColGroup(2),
            Child, MkLabel(MSG_SET_TTFDIR, MUIO_Label_DoubleFrame),
            Child, DirRow(MGR_DIR_TTF, GID_TTFPICK),
            Child, MkLabel(MSG_SET_OTFDIR, MUIO_Label_DoubleFrame),
            Child, DirRow(MGR_DIR_OTF, GID_OTFPICK),
            Child, MkLabel(MSG_SET_TTCDIR, MUIO_Label_DoubleFrame),
            Child, DirRow(MGR_DIR_TTC, GID_TTCPICK),
            Child, MkLabel(MSG_SET_CHARSET, MUIO_Label_DoubleFrame),
            Child, MkCycle(MSG_SET_CHARSET, GID_DEFCHARSET, charsets,
                           mcore.settings.charset),
        End,
        Child, InfoText(GetString(MSG_SET_NOTE)),
        Child, VSpace(0),
        Child, HGroup,
            Child, HSpace(0),
            Child, MkButton(MSG_SET_SAVE, GID_SAVESET),
        End,
    End;
}

/* ------------------------------------------------------------------ */
/* Menu: update check                                                   */
/* ------------------------------------------------------------------ */

static struct NewMenu menu[] =
{
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, (STRPTR)"?", 0, 0, (APTR)MENU_ABOUT },
    { NM_ITEM,  NULL, NULL, 0, 0, (APTR)MENU_UPDATE },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, (STRPTR)"Q", 0, 0, (APTR)MENU_QUIT },
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, NULL, 0, 0, (APTR)MENU_MUISETTINGS },
    { NM_ITEM,  NULL, (STRPTR)"H", 0, 0, (APTR)MENU_HELP },
    { NM_END,   NULL, NULL, 0, 0, NULL }
};

static void LocalizeMenu(void)
{
    menu[0].nm_Label = (STRPTR)GetString(MSG_MENU_PROJECT);
    menu[1].nm_Label = (STRPTR)GetString(MSG_MENU_ABOUT);
    menu[2].nm_Label = (STRPTR)GetString(MSG_MENU_UPDATE);
    menu[4].nm_Label = (STRPTR)GetString(MSG_MENU_QUIT);
    menu[5].nm_Label = (STRPTR)GetString(MSG_MENU_SETTINGS);
    menu[6].nm_Label = (STRPTR)GetString(MSG_MENU_MUISETTINGS);
    menu[7].nm_Label = (STRPTR)GetString(MSG_MENU_HELP);
}

/* Program, version, author, licences */
static void ShowAbout(void)
{
    ULONG arg = (ULONG)("AATextManager " AAMGR_VERSION " (MUI)");

    Ask(MSG_TITLE, MSG_ABOUT_TEXT, MSG_UPD_OK, &arg);
}

/* The documentation in the window's language */
static void ShowHelp(void)
{
    const char *path = gh_HelpFile(StringsLanguage());
    ULONG arg = (ULONG)path;

    if (!gh_ShowFile(path))
        Ask(MSG_TITLE, MSG_HELP_NOFILE, MSG_UPD_OK, &arg);
}

/*
 * Ask GitHub for the latest release; a newer package is downloaded to
 * RAM: and its full path shown. Nothing is installed.
 */
static void CheckUpdate(void)
{
    static struct AAUpdate u;
    ULONG args[3];
    LONG r, msg;

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
            msg = MSG_UPD_NEWER;
            break;
        case UPD_CURRENT:
            args[0] = (ULONG)AAMGR_VERSION;
            msg = MSG_UPD_CURRENT;
            break;
        case UPD_NONET:
            msg = MSG_UPD_NONET;
            break;
        case UPD_NOSSL:
            msg = MSG_UPD_NOSSL;
            break;
        default:
            args[0] = (ULONG)u.error;
            msg = MSG_UPD_FAILED;
            break;
    }
    Ask(MSG_MENU_UPDATE, msg, MSG_UPD_OK, args);
}

/* ------------------------------------------------------------------ */

/* obj sends id back to the main loop when attr changes */
static void Notify(Object *o, ULONG attr, ULONG value, LONG id)
{
    if (o)
        DoMethod(o, MUIM_Notify, attr, value, (ULONG)app, 2,
                 MUIM_Application_ReturnID, id);
}

static BOOL OpenWin(LONG charset)
{
    static const LONG buttons[] =
    {
        GID_ADD, GID_REMOVE, GID_INSTALL, GID_CHECK, GID_REPAIR, GID_DSCAN,
        GID_DDELETE, GID_DREPORT, GID_DLLOAD, GID_DLINSTALL, GID_TTFPICK,
        GID_OTFPICK, GID_TTCPICK, GID_SAVESET
    };
    LONG i;

    LocalizeMenu();
    tabs[0] = GetString(MSG_TAB_INSTALL);
    tabs[1] = GetString(MSG_TAB_REPAIR);
    tabs[2] = GetString(MSG_TAB_DIAG);
    tabs[3] = GetString(MSG_TAB_DOWNLOAD);
    tabs[4] = GetString(MSG_TAB_SETTINGS);
    for (i = 0; i < AA_NUM_CHARSETS; i++)
        charsets[i] = GetString(charset_msg[i]);
    enginenames[0] = GetString(MSG_ENGINE_AATEXT);
    enginenames[1] = GetString(MSG_ENGINE_FT2);
    for (i = 0; i < 3; i++)
        spnames[i] = GetString(MSG_PV_SP_OFF + i);

    app = ApplicationObject,
        MUIA_Application_Title, (ULONG)"AATextManager",
        MUIA_Application_Version,
            (ULONG)"$VER: AATextManager " AAMGR_VERSION " MUI",
        MUIA_Application_Description, (ULONG)GetString(MSG_TITLE),
        MUIA_Application_Base, (ULONG)"AATEXTMANAGER",
        MUIA_Application_Menustrip, Make(MUIO_MenustripNM, (ULONG)menu, 0, 0),
        SubWindow, win = WindowObject,
            MUIA_Window_Title, (ULONG)GetString(MSG_TITLE),
            MUIA_Window_ID, MAKE_ID('A','A','T','M'),
            WindowContents, VGroup,
                Child, ColGroup(2),
                    Child, MkLabel(MSG_CHARSET, MUIO_Label_DoubleFrame),
                    Child, MkCycle(MSG_CHARSET, GID_CHARSET, charsets,
                                   charset),
                    Child, MkLabel(MSG_ENGINE, MUIO_Label_DoubleFrame),
                    Child, MkCycle(MSG_ENGINE, GID_ENGINE, enginenames, 0),
                End,
                Child, obj[GID_TABS] = RegisterGroup(tabs),
                    MUIA_CycleChain, TRUE,
                    Child, InstallPage(),
                    Child, RepairPage(),
                    Child, DiagPage(),
                    Child, DownloadPage(),
                    Child, SettingsPage(),
                End,
                Child, status = TextObject,
                    MUIA_Text_PreParse, (ULONG)"",
                    MUIA_Text_Contents, (ULONG)"",
                End,
            End,
        End,
    End;
    if (!app)
        return FALSE;

    set(obj[GID_REPAIR], MUIA_Disabled, TRUE);
    set(obj[GID_DDELETE], MUIA_Disabled, TRUE);
    for (i = 0; i < (LONG)(sizeof(buttons) / sizeof(buttons[0])); i++)
    {
        set(obj[buttons[i]], MUIA_CycleChain, TRUE);
        Notify(obj[buttons[i]], MUIA_Pressed, FALSE, buttons[i]);
    }
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app,
             2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    Notify(obj[GID_TABS], MUIA_Group_ActivePage, MUIV_EveryTime, GID_TABS);
    Notify(obj[GID_CHARSET], MUIA_Cycle_Active, MUIV_EveryTime, GID_CHARSET);
    Notify(obj[GID_ENGINE], MUIA_Cycle_Active, MUIV_EveryTime, GID_ENGINE);
    Notify(obj[GID_CHANGEENGINE], MUIA_Selected, MUIV_EveryTime,
           GID_CHANGEENGINE);
    Notify(obj[GID_CHANGECHARSET], MUIA_Selected, MUIV_EveryTime,
           GID_CHANGECHARSET);
    Notify(obj[GID_PVAA], MUIA_Selected, MUIV_EveryTime, GID_PVAA);
    Notify(obj[GID_DLAA], MUIA_Selected, MUIV_EveryTime, GID_DLAA);
    Notify(obj[GID_PVSP], MUIA_Cycle_Active, MUIV_EveryTime, GID_PVSP);
    Notify(obj[GID_DLSP], MUIA_Cycle_Active, MUIV_EveryTime, GID_DLSP);
    Notify(filelist, MUIA_List_Active, MUIV_EveryTime, GID_FILES);
    Notify(dfontlist, MUIA_List_Active, MUIV_EveryTime, GID_DFONTLIST);
    Notify(dllist, MUIA_List_Active, MUIV_EveryTime, GID_DLLIST);
    Notify(obj[GID_DLFILTER], MUIA_String_Acknowledge, MUIV_EveryTime,
           GID_DLFILTER);

    set(win, MUIA_Window_Open, TRUE);
    return Get(win, MUIA_Window_Open) != 0;
}

static void Action(ULONG id)
{
    switch (id)
    {
        case GID_FILES:     ShowPreview();  break;
        case GID_TABS:
            /* the shared preview buffer: this tab's font again */
            if (CurrentPage() == 0 && pvpage != 0)
                ShowPreview();
            else if (CurrentPage() == 3 && pvpage != 3)
                DlShowAgain();
            break;
        case GID_ADD:       AddFiles();     break;
        case GID_REMOVE:    RemoveFile();   break;
        case GID_INSTALL:   Install();      break;
        case GID_CHECK:     CheckFonts();   break;
        case GID_REPAIR:    RepairAll();    break;
        case GID_DSCAN:     DiagScan();     break;
        case GID_DFONTLIST: DiagDetail();   break;
        case GID_DDELETE:   DiagDelete();   break;
        case GID_DREPORT:   DiagReport();   break;
        case GID_TTFPICK:   PickDir(MGR_DIR_TTF); break;
        case GID_OTFPICK:   PickDir(MGR_DIR_OTF); break;
        case GID_TTCPICK:   PickDir(MGR_DIR_TTC); break;
        case GID_SAVESET:   SaveSettings(); break;
        case GID_DLLOAD:    LoadCatalog();  break;
        case GID_DLFILTER:  ShowCatalog();  break;
        case GID_DLLIST:    DlPreview();    break;
        case GID_DLINSTALL: DlInstall();    break;
        case GID_PVAA:
        case GID_DLAA:
        {
            BOOL on = Get(obj[id], MUIA_Selected) != 0;

            if (on == pvaa)
                break;      /* the other tab's box, set from here */
            pvaa = on;
            pv_SetAntialias(pvaa);
            nnset(obj[id == GID_PVAA ? GID_DLAA : GID_PVAA], MUIA_Selected,
                  pvaa);
            set(obj[GID_PVSP], MUIA_Disabled, !pvaa);
            set(obj[GID_DLSP], MUIA_Disabled, !pvaa);
            if (id == GID_PVAA)
                ShowPreview();
            else
                DlShowAgain();
            break;
        }
        case GID_PVSP:
        case GID_DLSP:
        {
            UBYTE sp = (UBYTE)Get(obj[id], MUIA_Cycle_Active);

            if (sp == pvsp)
                break;
            pvsp = sp;
            pv_SetSubpixel(pvsp);
            nnset(obj[id == GID_PVSP ? GID_DLSP : GID_PVSP],
                  MUIA_Cycle_Active, pvsp);
            if (id == GID_PVSP)
                ShowPreview();
            else
                DlShowAgain();
            break;
        }
        case GID_CHARSET:
        case GID_ENGINE:
        case GID_CHANGEENGINE:
        case GID_CHANGECHARSET:
            /* the repair list depends on these */
            if (mcore.numdiag > 0)
                CheckFonts();
            break;
    }
}

int mgr_RunGUI(const char *language)
{
    ULONG sigs = 0;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    problems = AllocVec(MC_MAX_FONTS * sizeof(*problems),
                        MEMF_ANY | MEMF_CLEAR);
    if (!problems || !OpenLibs() || !mc_Init())
        goto out;
    InitStrings(language);
    pv_Init();              /* without it the boxes stay empty */

    boxclass = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL,
                                     sizeof(struct BoxData), (APTR)BoxEntry);
    if (!boxclass || !OpenWin(mcore.settings.charset))
    {
        Printf((CONST_STRPTR)"AATextManager: cannot open the window\n");
        goto out;
    }

    while (!done)
    {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);

        if (id == (ULONG)MUIV_Application_ReturnID_Quit || id == MENU_QUIT)
            done = TRUE;
        else if (id == MENU_UPDATE)
            CheckUpdate();
        else if (id == MENU_ABOUT)
            ShowAbout();
        else if (id == MENU_MUISETTINGS)
            DoMethod(app, MUIM_Application_OpenConfigWindow, 0);
        else if (id == MENU_HELP)
            ShowHelp();
        else if (id)
            Action(id);
        if (!done && sigs)
        {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }
    rc = RETURN_OK;

out:
    if (app)
        MUI_DisposeObject(app);
    if (boxclass)
        MUI_DeleteCustomClass(boxclass);
    FreeStrings();
    pv_Cleanup();
    CloseLibs();
    if (problems)
        FreeVec(problems);
    mc_Cleanup();
    return rc;
}
