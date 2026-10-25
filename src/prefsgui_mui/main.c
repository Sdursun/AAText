/*
 * AATextPrefs - preferences program for AAText (MUI 3.8 or newer).
 *
 * The same program as src/prefsgui/main.c (ReAction): the settings and
 * the talking to AAText are in src/prefsgui/core.c, the texts in the
 * same catalog. Only the window is MUI's.
 *
 * Shell options: LANGUAGE=<name> (e.g. türkçe) overrides the locale,
 * PUBSCREEN=<name> opens the window on that public screen.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <graphics/text.h>
#include <utility/hooks.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <libraries/mui.h>
#include <libraries/iffparse.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/utility.h>
#include <proto/asl.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "../prefs.h"
#include "../prefswrite.h"
#include "../aaclient.h"
#include "../charsets.h"
#include "../manager/preview.h"
#include "../guihelp.h"
#include "../prefsgui/core.h"
#include "../prefsgui/strings.h"

static const char version[] __attribute__((used)) =
    "$VER: AATextPrefs 0.44 (26.10.2026) MUI";
static const char stackcookie[] __attribute__((used)) = "$STACK:16384";

#define ENV_PREFS     "ENV:AAText.prefs"
#define ENVARC_PREFS  "ENVARC:AAText.prefs"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase, *DiskfontBase, *AslBase, *MUIMasterBase;
/* FreeType for the mapping preview (optional: no preview without it) */
struct Library *AATextBase;

/* return IDs of the objects; menu items from MENU_BASE */
enum
{
    GID_TABS = 1, GID_GAMMA, GID_HINTING, GID_REAL, GID_KERNING,
    GID_ANTIALIAS, GID_SUBPIXEL, GID_PROGNAME, GID_ADD, GID_REMOVE,
    GID_PICKFILE, GID_AUTO, GID_OFFSCREEN, GID_AGA, GID_CACHE, GID_FONTMEM,
    GID_CHARSET, GID_MANAGER, GID_MAPLIST, GID_MAPADD, GID_MAPCHANGE,
    GID_MAPREMOVE, GID_MAPPX, GID_SAVE, GID_USE, GID_CANCEL,
    GID_COUNT
};

enum
{
    MENU_BASE = 100, MENU_OPEN, MENU_SAVEAS, MENU_QUIT, MENU_DEFAULTS,
    MENU_LASTSAVED, MENU_RESTORE, MENU_ABOUT,
    MENU_MUISETTINGS, MENU_HELP
};

/* cycle order -> AA_HINT_... value */
static const UBYTE hint_order[4] =
{
    AA_HINT_NORMAL, AA_HINT_LIGHT, AA_HINT_NONE, AA_HINT_FULL
};

/* Labels of the character set cycle, in AA_CHARSET_* order */
static const LONG charset_msg[AA_NUM_CHARSETS] =
{
    MSG_CHARSET_LATIN1, MSG_CHARSET_LATIN2, MSG_CHARSET_LATIN3,
    MSG_CHARSET_LATIN4, MSG_CHARSET_LATIN5, MSG_CHARSET_LATIN9,
    MSG_CHARSET_LATIN10, MSG_CHARSET_CP1250, MSG_CHARSET_CYRILLIC,
    MSG_CHARSET_KOI8R, MSG_CHARSET_AMIGAE2
};

static Object *app, *win, *obj[GID_COUNT];
static Object *preview, *fontinfotext, *status, *cacheused,
              *fontmemused, *blacklist, *runlist, *runlv, *popobj, *maplist,
              *mappreview;
static struct MUI_CustomClass *boxclass, *gammaclass;

static char fontinfo[80];
static struct TextFont *previewfont;
static BOOL pvready;            /* pv_Init() done */

/* ------------------------------------------------------------------ */

static BOOL OpenLibs(void)
{
    static const struct { struct Library **base; const char *name;
                          ULONG ver; } libs[] =
    {
        { (struct Library **)&IntuitionBase, "intuition.library", 40 },
        { (struct Library **)&GfxBase,       "graphics.library", 40 },
        { &UtilityBase,   "utility.library", 40 },
        { &DiskfontBase,  "diskfont.library", 39 },     /* OS 3.1 has 39 */
        { &AslBase,       "asl.library", 40 },
        { &MUIMasterBase, MUIMASTER_NAME, 19 },     /* MUI 3.8 */
    };
    ULONG i;

    for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
    {
        *libs[i].base = OpenLibrary((CONST_STRPTR)libs[i].name, libs[i].ver);
        if (!*libs[i].base)
        {
            Printf((CONST_STRPTR)"AATextPrefs: cannot open %s %ld\n",
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
        &MUIMasterBase, &AslBase, &DiskfontBase, &UtilityBase,
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

/* a button whose "_" marks its key */
static Object *MkButton(LONG msg, LONG id)
{
    Object *o = Make(MUIO_Button, (ULONG)GetString(msg), 0, 0);

    obj[id] = o;
    return o;
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

/* a number field (only digits) */
static Object *MkInteger(LONG id, LONG value, LONG maxlen)
{
    return obj[id] = StringObject,
        StringFrame,
        MUIA_String_Accept, (ULONG)"0123456789",
        MUIA_String_MaxLen, maxlen,
        MUIA_String_Integer, value,
        MUIA_CycleChain, TRUE,
    End;
}

static Object *InfoText(const char *text)
{
    return TextObject,
        MUIA_Text_Contents, (ULONG)text,
        MUIA_Text_PreParse, (ULONG)"",
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

/* ------------------------------------------------------------------ */
/* Custom classes                                                      */
/* ------------------------------------------------------------------ */

/*
 * The preview boxes: BOX_SCREEN draws two lines with the screen font
 * (AAText renders them), BOX_MAPPING the selected mapping's TrueType
 * font through preview.c.
 */
#define MUIA_AABox_Kind (TAG_USER | 0x4141)
enum { BOX_SCREEN, BOX_MAPPING };

struct BoxData
{
    LONG kind;
};

static ULONG BoxNew(struct IClass *cl, Object *o, struct opSet *msg)
{
    struct BoxData *d;

    if (!(o = (Object *)DoSuperMethodA(cl, o, (Msg)msg)))
        return 0;
    d = INST_DATA(cl, o);
    d->kind = GetTagData(MUIA_AABox_Kind, BOX_SCREEN, msg->ops_AttrList);
    return (ULONG)o;
}

static ULONG BoxAskMinMax(struct IClass *cl, Object *o,
                          struct MUIP_AskMinMax *msg)
{
    struct BoxData *d = INST_DATA(cl, o);
    LONG lineh = previewfont ? previewfont->tf_YSize : 8, h;

    DoSuperMethodA(cl, o, (Msg)msg);
    if (d->kind == BOX_SCREEN)
        h = lineh * 2 + 16;
    else
        h = AATextBase ? pv_NeededHeight() : lineh;
    msg->MinMaxInfo->MinWidth += 200;
    msg->MinMaxInfo->DefWidth += 360;
    msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    msg->MinMaxInfo->MinHeight += h;
    msg->MinMaxInfo->DefHeight += h;
    msg->MinMaxInfo->MaxHeight += h;
    return 0;
}

static void DrawScreenFont(Object *o)
{
    struct RastPort *rp = _rp(o);
    struct TextFont *old = rp->Font;
    const char *line1 = GetString(MSG_PREVIEW_LINE1);
    const char *line2 = GetString(MSG_PREVIEW_LINE2);
    APTR clip;
    LONG y;

    clip = MUI_AddClipping(muiRenderInfo(o), _mleft(o), _mtop(o),
                           _mwidth(o), _mheight(o));
    if (previewfont)
        SetFont(rp, previewfont);
    SetAPen(rp, _dri(o)->dri_Pens[TEXTPEN]);
    SetDrMd(rp, JAM1);
    y = _mtop(o) + 4 + rp->TxBaseline;
    Move(rp, _mleft(o) + 8, y);
    Text(rp, (CONST_STRPTR)line1, strlen(line1));
    Move(rp, _mleft(o) + 8, y + rp->TxHeight + 4);
    Text(rp, (CONST_STRPTR)line2, strlen(line2));
    SetFont(rp, old);
    MUI_RemoveClipping(muiRenderInfo(o), clip);
}

static ULONG BoxDraw(struct IClass *cl, Object *o, struct MUIP_Draw *msg)
{
    struct BoxData *d = INST_DATA(cl, o);

    DoSuperMethodA(cl, o, (Msg)msg);
    if (!(msg->flags & MADF_DRAWOBJECT))
        return 0;
    if (d->kind == BOX_SCREEN)
        DrawScreenFont(o);
    else if (pvready)
        pv_Draw(_rp(o), _mleft(o), _mtop(o), _mwidth(o), _mheight(o),
                _dri(o)->dri_Pens[BACKGROUNDPEN], _dri(o)->dri_Pens[TEXTPEN],
                "");
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

/*
 * The gamma slider moves in steps of 0.05 (level = gamma100 / 5) and
 * shows "1.80" instead of the level. A value from the file that is not
 * on a step is kept until the slider is moved.
 */
#define GAMMA_STEP 5

static LONG GammaLevel(UWORD gamma100)
{
    return (gamma100 + GAMMA_STEP / 2) / GAMMA_STEP;
}

static ULONG GammaDispatcher(struct IClass *cl, Object *o, Msg msg)
{
    static char buf[16];

    if (msg->MethodID == MUIM_Numeric_Stringify)
    {
        LONG g = ((struct MUIP_Numeric_Stringify *)msg)->value * GAMMA_STEP;

        snprintf(buf, sizeof(buf), "%ld.%02ld", (long)(g / 100),
                 (long)(g % 100));
        return (ULONG)buf;
    }
    return DoSuperMethodA(cl, o, msg);
}

static ULONG GammaEntry(void)
{
    register struct IClass *cl __asm("a0");
    register Object *o __asm("a2");
    register Msg msg __asm("a1");

    return GammaDispatcher(cl, o, msg);
}

/* ------------------------------------------------------------------ */
/* Talking to AAText                                                   */
/* ------------------------------------------------------------------ */

static void SetStatus(const char *text)
{
    if (status)
        set(status, MUIA_Text_Contents, (ULONG)text);
}

static void QueryAAText(void)
{
    char msg[160];

    switch (pc_Query())
    {
        case PC_RUNNING:
            snprintf(msg, sizeof(msg), GetString(MSG_STATUS_RUNNING),
                     pc.version);
            SetStatus(msg);
            break;
        case PC_STOPPED:
            SetStatus(GetString(MSG_STATUS_STOPPED));
            break;
        default:
            SetStatus(GetString(MSG_STATUS_OLD));
            break;
    }
}

/* A requester with gadgets "A|B|C"; 1, 2, ... for them, 0 the last. */
static LONG Ask(LONG title, LONG text, LONG gadgets, APTR args)
{
    return MUI_RequestA(app, win, 0, (char *)GetString(title),
                        (char *)GetString(gadgets), (char *)GetString(text),
                        args);
}

/* APPLY or RELOAD; shows the restart note if AAText asks for it. */
static void Send(UWORD cmd, const struct AAPrefs *p)
{
    if (pc_Send(cmd, p) == PC_RESTART && cmd != AACMD_APPLY)
        Ask(MSG_RESTART_TITLE, MSG_RESTART_TEXT, MSG_OK, NULL);
}

static BOOL WritePrefsFile(const char *path, const char *template_path)
{
    char msg[160];

    if (pc_Write(path, template_path))
        return TRUE;
    snprintf(msg, sizeof(msg), GetString(MSG_WRITE_ERROR), path);
    SetStatus(msg);
    return FALSE;
}

/* Read the settings back from the objects. */
static void ReadGadgets(void)
{
    ULONG v;

    v = Get(obj[GID_GAMMA], MUIA_Numeric_Value);
    if ((LONG)v != GammaLevel(pc.cur.gamma100))
        pc.cur.gamma100 = v * GAMMA_STEP;
    pc.cur.hinting = hint_order[Get(obj[GID_HINTING], MUIA_Cycle_Active) & 3];
    pc.cur.autoreal = Get(obj[GID_REAL], MUIA_Selected) != 0;
    pc.cur.kerning = Get(obj[GID_KERNING], MUIA_Selected) != 0;
    pc.cur.autodetect = Get(obj[GID_AUTO], MUIA_Selected) != 0;
    pc.cur.offscreen = Get(obj[GID_OFFSCREEN], MUIA_Selected) != 0;
    pc.cur.aga = Get(obj[GID_AGA], MUIA_Selected) != 0;
    pc.cur.antialias = Get(obj[GID_ANTIALIAS], MUIA_Selected) != 0;
    v = Get(obj[GID_SUBPIXEL], MUIA_Cycle_Active);
    pc.cur.subpixel = v <= AA_SUBPIXEL_BGR ? v : AA_SUBPIXEL_OFF;
    /* the same limits as the prefs file */
    v = Get(obj[GID_CACHE], MUIA_String_Integer);
    pc.cur.cachekb = v < 32 ? 32 : v > 16384 ? 16384 : v;
    /* 0: no limit; otherwise at least 1 MB */
    v = Get(obj[GID_FONTMEM], MUIA_String_Integer);
    pc.cur.fontmemkb = v && v < 1024 ? 1024 : v > 262144 ? 262144 : v;
    v = Get(obj[GID_CHARSET], MUIA_Cycle_Active);
    pc.cur.charset = v < AA_NUM_CHARSETS ? v : AA_CHARSET_LATIN1;
}

/* "In use: 5 KB, 52 characters" from AAText's STATUS */
static void UpdateCacheUsed(void)
{
    static char ctext[64], ftext[64];
    ULONG cbytes, glyphs, fbytes;
    LONG files;

    ctext[0] = ftext[0] = 0;
    if (pc_Usage(&cbytes, &glyphs, &fbytes, &files))
    {
        snprintf(ctext, sizeof(ctext), GetString(MSG_CACHE_USED),
                 (long)((cbytes + 1023) / 1024), (long)glyphs);
        snprintf(ftext, sizeof(ftext), GetString(MSG_FONTMEM_USED),
                 (long)((fbytes + 1023) / 1024), (long)files);
    }
    if (cacheused)
        set(cacheused, MUIA_Text_Contents, (ULONG)ctext);
    if (fontmemused)
        set(fontmemused, MUIA_Text_Contents, (ULONG)ftext);
}

/* A setting changed: let AAText use it at once and redraw the preview. */
static void LiveApply(void)
{
    BOOL wasrunning = pc.running;

    ReadGadgets();
    if (!wasrunning)
        return;
    if (!pc_LiveApply())
    {
        SetStatus(GetString(MSG_STATUS_STOPPED));
        return;
    }
    SetStatus(GetString(MSG_STATUS_TESTED));
    if (preview)
        MUI_Redraw(preview, MADF_DRAWOBJECT);
    UpdateCacheUsed();
}

/* ------------------------------------------------------------------ */
/* Programs tab                                                        */
/* ------------------------------------------------------------------ */

static void ShowBlacklist(void)
{
    LONG i;

    set(blacklist, MUIA_List_Quiet, TRUE);
    DoMethod(blacklist, MUIM_List_Clear);
    for (i = 0; i < pc.cur.numblack; i++)
        DoMethod(blacklist, MUIM_List_InsertSingle,
                 (ULONG)pc.cur.blacklist[i], MUIV_List_Insert_Bottom);
    set(blacklist, MUIA_List_Quiet, FALSE);
}

static void AddProgram(const char *name)
{
    char msg[80];

    switch (pc_AddProgram(name))
    {
        case PC_ADDED:
            ShowBlacklist();
            set(obj[GID_PROGNAME], MUIA_String_Contents, (ULONG)"");
            LiveApply();
            break;
        case PC_FULL:
            snprintf(msg, sizeof(msg), GetString(MSG_LIST_FULL),
                     (long)AA_MAX_BLACKLIST);
            SetStatus(msg);
            break;
    }
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
                             ASLFR_Window, (ULONG)IWin(),
                             ASLFR_TitleText, (ULONG)GetString(MSG_PICK_TITLE),
                             ASLFR_InitialDrawer, (ULONG)"SYS:",
                             ASLFR_RejectIcons, TRUE,
                             TAG_DONE);
    if (!fr)
        return;
    set(app, MUIA_Application_Sleep, TRUE);
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
        AddProgram((const char *)fr->fr_File);
    set(app, MUIA_Application_Sleep, FALSE);
    FreeAslRequest(fr);
}

static void RemoveProgram(void)
{
    LONG sel = (LONG)Get(blacklist, MUIA_List_Active);

    if (sel < 0 || sel >= pc.cur.numblack)
        return;
    pc_RemoveProgram(sel);
    ShowBlacklist();
    LiveApply();
}

/* The running programs into the popup list before it opens. */
static ULONG RunStrObj(void)
{
    LONG i;

    set(runlist, MUIA_List_Quiet, TRUE);
    DoMethod(runlist, MUIM_List_Clear);
    pc_FillRunning();
    for (i = 0; i < pc.numrunning; i++)
        DoMethod(runlist, MUIM_List_InsertSingle, (ULONG)pc.runnames[i],
                 MUIV_List_Insert_Bottom);
    set(runlist, MUIA_List_Quiet, FALSE);
    return TRUE;
}

/* A running program chosen in the popup: on the list at once. */
static void RunObjStr(void)
{
    char *name = NULL;

    DoMethod(runlist, MUIM_List_GetEntry, MUIV_List_GetEntry_Active,
             (ULONG)&name);
    if (name)
        AddProgram(name);
}

static ULONG RunStrObjEntry(void)
{
    return RunStrObj();
}

static ULONG RunObjStrEntry(void)
{
    RunObjStr();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Advanced tab                                                        */
/* ------------------------------------------------------------------ */

/*
 * ENV:ftcodepage (ENVARC: too with archive) to the chosen character set.
 * A code page that is not one of ours is replaced only when the user
 * agrees. FALSE if a file could not be written.
 */
static BOOL WriteCodePage(BOOL archive)
{
    char msg[80];

    if (pc_CodePageForeign())
    {
        ULONG arg = (ULONG)GetString(charset_msg[pc.cur.charset]);

        if (Ask(MSG_WINDOW_TITLE, MSG_FTCODEPAGE_ASK, MSG_REPLACE_KEEP,
                &arg) != 1)
            return TRUE;
    }
    switch (pc_WriteCodePage(archive))
    {
        case PC_CP_ENVFAIL:
            SetStatus(GetString(MSG_FTCODEPAGE_FAIL));
            return FALSE;
        case PC_CP_ARCFAIL:
            snprintf(msg, sizeof(msg), GetString(MSG_WRITE_ERROR),
                     "ENVARC:ftcodepage");
            SetStatus(msg);
            return FALSE;
    }
    return TRUE;
}

/* The font installer, SYS:System/AATextManager, on its own. */
static void StartManager(void)
{
    static const char path[] = "SYS:System/AATextManager";
    char msg[160];
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    BPTR in = 0, out = 0;

    /* an asynchronous SystemTags() succeeds even when the program is
       missing, so look first */
    if (lock)
    {
        UnLock(lock);
        in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
        out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    }
    if (in && out &&
        SystemTags((CONST_STRPTR)path, SYS_Input, in, SYS_Output, out,
                   SYS_Asynch, TRUE, NP_StackSize, 16384, TAG_DONE) == 0)
        return;             /* the new process closes in and out */
    if (in)
        Close(in);
    if (out)
        Close(out);
    snprintf(msg, sizeof(msg), GetString(MSG_DIAG_NOMANAGER), path);
    SetStatus(msg);
}

/* ------------------------------------------------------------------ */
/* Font mappings tab                                                   */
/* ------------------------------------------------------------------ */

static LONG MapSelected(void)
{
    LONG sel = (LONG)Get(maplist, MUIA_List_Active);

    return sel >= 0 && sel < pc.cur.nummaps ? sel : -1;
}

static LONG CurrentPage(void)
{
    return (LONG)Get(obj[GID_TABS], MUIA_Group_ActivePage);
}

/* the selected mapping's TrueType file in the preview, or nothing */
static void MapPreview(void)
{
    struct Window *w = IWin();
    LONG sel = MapSelected();

    if (!w || !AATextBase)
        return;
    if (!pvready && !(pvready = pv_Init()))
        return;
    if (sel >= 0)
        pv_Render(w->WScreen, pc.cur.map[sel].ttfpath, 0,
                  GetString(MSG_PREVIEW_LINE1), GetString(MSG_PREVIEW_LINE2));
    else
        pv_Render(w->WScreen, NULL, 0, NULL, NULL);
    if (CurrentPage() == 3)
        MUI_Redraw(mappreview, MADF_DRAWOBJECT);
}

/* the selection's pixel size in its field, Change/Remove usable */
static void MapSelectionChanged(void)
{
    LONG sel = MapSelected();

    nnset(obj[GID_MAPPX], MUIA_String_Integer,
          sel >= 0 ? pc.cur.map[sel].pixelsize : 0);
    set(obj[GID_MAPPX], MUIA_Disabled, sel < 0);
    set(obj[GID_MAPCHANGE], MUIA_Disabled, sel < 0);
    set(obj[GID_MAPREMOVE], MUIA_Disabled, sel < 0);
    MapPreview();
}

/* the columns of a mapping: entry is a struct AAMapping in pc.cur */
static ULONG MapDisplay(char **array, const struct AAMapping *mp)
{
    static char font[AA_NAME_LEN + 8], px[12];

    if (!mp)
    {
        array[0] = (char *)GetString(MSG_MAP_COL_FONT);
        array[1] = (char *)GetString(MSG_MAP_COL_FILE);
        array[2] = (char *)GetString(MSG_MAP_COL_PX);
        return 0;
    }
    snprintf(font, sizeof(font), "%s %ld", mp->fontname, (long)mp->ysize);
    if (mp->pixelsize)
        snprintf(px, sizeof(px), "%ld", (long)mp->pixelsize);
    else
        snprintf(px, sizeof(px), "%s", GetString(MSG_MAP_AUTO));
    array[0] = font;
    array[1] = (char *)mp->ttfpath;
    array[2] = px;
    return 0;
}

static ULONG MapDisplayEntry(void)
{
    register char **array __asm("a2");
    register const struct AAMapping *mp __asm("a1");

    return MapDisplay(array, mp);
}

/* Show cur.map in the list; sel is the entry to select or -1. */
static void ShowMappings(LONG sel)
{
    LONG i;

    set(maplist, MUIA_List_Quiet, TRUE);
    DoMethod(maplist, MUIM_List_Clear);
    for (i = 0; i < pc.cur.nummaps; i++)
        DoMethod(maplist, MUIM_List_InsertSingle, (ULONG)&pc.cur.map[i],
                 MUIV_List_Insert_Bottom);
    set(maplist, MUIA_List_Quiet, FALSE);
    nnset(maplist, MUIA_List_Active,
          sel >= 0 ? sel : MUIV_List_Active_Off);
    MapSelectionChanged();
}

/*
 * Add a mapping (replace < 0) or change entry replace: the bitmap font
 * from the font requester, then its TrueType file.
 */
static void EditMapping(LONG replace)
{
    struct FontRequester *fo;
    struct FileRequester *fr = NULL;
    struct AAMapping m;
    char name[AA_NAME_LEN + 8], path[AA_PATH_LEN];
    LONG len;
    BOOL ok = FALSE;

    if (replace < 0 && pc.cur.nummaps == AA_MAX_MAPPINGS)
    {
        char msg[80];

        snprintf(msg, sizeof(msg), GetString(MSG_MAP_FULL),
                 (long)AA_MAX_MAPPINGS);
        SetStatus(msg);
        return;
    }
    if (replace >= 0)
        m = pc.cur.map[replace];
    else
        memset(&m, 0, sizeof(m));

    set(app, MUIA_Application_Sleep, TRUE);
    /* the bitmap font and its size */
    snprintf(name, sizeof(name), "%s.font", replace >= 0 ? m.fontname
                                                         : "topaz");
    fo = AllocAslRequestTags(ASL_FontRequest,
                             ASLFO_Window, (ULONG)IWin(),
                             ASLFO_TitleText,
                                 (ULONG)GetString(MSG_MAP_FONTTITLE),
                             ASLFO_InitialName, (ULONG)name,
                             ASLFO_InitialSize, replace >= 0 ? m.ysize : 8,
                             TAG_DONE);
    if (fo && AslRequest(fo, NULL) && fo->fo_Attr.ta_Name)
    {
        snprintf(m.fontname, sizeof(m.fontname), "%s",
                 (const char *)fo->fo_Attr.ta_Name);
        len = strlen(m.fontname);
        if (len > 5 && !Stricmp((CONST_STRPTR)m.fontname + len - 5,
                                (CONST_STRPTR)".font"))
            m.fontname[len - 5] = 0;
        m.ysize = fo->fo_Attr.ta_YSize;
        ok = TRUE;
    }
    if (fo)
        FreeAslRequest(fo);

    /* its TrueType file */
    if (ok)
    {
        ok = FALSE;
        fr = AllocAslRequestTags(ASL_FileRequest,
                                 ASLFR_Window, (ULONG)IWin(),
                                 ASLFR_TitleText,
                                     (ULONG)GetString(MSG_MAP_FILETITLE),
                                 ASLFR_InitialDrawer, (ULONG)"FONTS:_ttf",
                                 ASLFR_InitialPattern,
                                     (ULONG)"#?.(ttf|otf|ttc|otc)",
                                 ASLFR_DoPatterns, TRUE,
                                 ASLFR_RejectIcons, TRUE,
                                 TAG_DONE);
    }
    if (fr && AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
    {
        snprintf(path, sizeof(path), "%s", (const char *)fr->fr_Drawer);
        if (AddPart((STRPTR)path, fr->fr_File, sizeof(path)))
        {
            snprintf(m.ttfpath, sizeof(m.ttfpath), "%s", path);
            ok = TRUE;
        }
    }
    if (fr)
        FreeAslRequest(fr);
    set(app, MUIA_Application_Sleep, FALSE);
    if (!ok)
        return;

    if (replace < 0)
        replace = pc_AddMapping(&m);
    else
        pc_SetMapping(replace, &m);
    ShowMappings(replace);
    LiveApply();
}

static void RemoveMapping(void)
{
    LONG sel = MapSelected();

    if (sel < 0)
        return;
    pc_RemoveMapping(sel);
    ShowMappings(-1);
    LiveApply();
}

static void MapPixelSize(void)
{
    LONG sel = MapSelected();
    ULONG v = Get(obj[GID_MAPPX], MUIA_String_Integer);

    if (sel < 0)
        return;
    if (!pc_SetMapPixel(sel, v > 255 ? 255 : v))
        return;
    ShowMappings(sel);
    LiveApply();
}

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */

/* "Screen font: XEN 8", without the ".font" */
static void MakeFontInfo(const struct TextAttr *ta)
{
    char name[AA_NAME_LEN];
    char *dot;

    snprintf(name, sizeof(name), "%s", ta->ta_Name ? (char *)ta->ta_Name : "");
    if ((dot = strstr(name, ".font")))
        *dot = 0;
    snprintf(fontinfo, sizeof(fontinfo),
             GetString(pc.running ? MSG_PREVIEW_FONT : MSG_PREVIEW_PLAIN),
             name, (long)ta->ta_YSize);
}

static LONG HintIndex(UBYTE hinting)
{
    LONG i;

    for (i = 0; i < 4; i++)
        if (hint_order[i] == hinting)
            return i;
    return 0;
}

static const char *tabs[5], *subpix[4], *hints[5],
                  *charsets[AA_NUM_CHARSETS + 1];

static Object *AppearancePage(void)
{
    obj[GID_GAMMA] = NewObject(gammaclass->mcc_Class, NULL,
        MUIA_Slider_Horiz, TRUE,
        MUIA_Numeric_Min, 50 / GAMMA_STEP,
        MUIA_Numeric_Max, 400 / GAMMA_STEP,
        MUIA_Numeric_Value, GammaLevel(pc.cur.gamma100),
        MUIA_CycleChain, TRUE,
        TAG_DONE);

    return VGroup,
        Child, ColGroup(2),
            Child, MkLabel(MSG_GAMMA, MUIO_Label_SingleFrame),
            Child, obj[GID_GAMMA],
            Child, MkLabel(MSG_SUBPIXEL, MUIO_Label_DoubleFrame),
            Child, MkCycle(MSG_SUBPIXEL, GID_SUBPIXEL, subpix,
                         pc.cur.subpixel),
            Child, MkLabel(MSG_HINTING, MUIO_Label_DoubleFrame),
            Child, MkCycle(MSG_HINTING, GID_HINTING, hints,
                         HintIndex(pc.cur.hinting)),
        End,
        Child, MkCheck(MSG_ANTIALIAS, GID_ANTIALIAS, pc.cur.antialias),
        Child, MkCheck(MSG_REAL, GID_REAL, pc.cur.autoreal),
        Child, MkCheck(MSG_KERNING, GID_KERNING, pc.cur.kerning),
        Child, InfoText(GetString(MSG_REAL_NOTE)),
        Child, VGroup,
            GroupFrameT(GetString(MSG_PREVIEW)),
            Child, preview = NewObject(boxclass->mcc_Class, NULL,
                                       MUIA_AABox_Kind, BOX_SCREEN,
                                       TAG_DONE),
            Child, fontinfotext = InfoText(fontinfo),
        End,
        Child, VSpace(0),
    End;
}

static Object *ProgramsPage(void)
{
    static struct Hook strobj, objstr;
    Object *page, *popbutton;

    strobj.h_Entry = (HOOKFUNC)RunStrObjEntry;
    objstr.h_Entry = (HOOKFUNC)RunObjStrEntry;

    page = VGroup,
        Child, InfoText(GetString(MSG_PROGRAMS_INFO)),
        Child, ListviewObject,
            MUIA_CycleChain, TRUE,
            MUIA_Listview_List, blacklist = ListObject,
                InputListFrame,
            End,
        End,
        Child, HGroup,
            Child, MkLabel(MSG_PROGRAM, MUIO_Label_DoubleFrame),
            /* the field, its popup lists the programs running now */
            Child, popobj = PopobjectObject,
                MUIA_Popstring_String, obj[GID_PROGNAME] = StringObject,
                    StringFrame,
                    MUIA_String_MaxLen, AA_NAME_LEN,
                    MUIA_CycleChain, TRUE,
                End,
                MUIA_Popstring_Button, popbutton = Make(MUIO_PopButton,
                                                        (ULONG)MUII_PopUp,
                                                        0, 0),
                MUIA_Popobject_StrObjHook, (ULONG)&strobj,
                MUIA_Popobject_ObjStrHook, (ULONG)&objstr,
                MUIA_Popobject_Object, runlv = ListviewObject,
                    MUIA_Listview_List, runlist = ListObject,
                        InputListFrame,
                    End,
                End,
            End,
            Child, MkButton(MSG_PICK, GID_PICKFILE),
            Child, MkButton(MSG_ADD, GID_ADD),
            Child, MkButton(MSG_REMOVE, GID_REMOVE),
        End,
    End;
    if (page)
    {
        set(popbutton, MUIA_ShortHelp, (ULONG)GetString(MSG_RUNNING));
        /* the field takes the width, the buttons only what they need */
        set(popobj, MUIA_HorizWeight, 300);
    }
    return page;
}

static Object *AdvancedPage(void)
{
    return VGroup,
        Child, MkCheck(MSG_AUTODETECT, GID_AUTO, pc.cur.autodetect),
        Child, MkCheck(MSG_OFFSCREEN, GID_OFFSCREEN, pc.cur.offscreen),
        Child, MkCheck(MSG_AGA, GID_AGA, pc.cur.aga),
        Child, ColGroup(3),
            Child, MkLabel(MSG_CACHE, MUIO_Label_DoubleFrame),
            Child, MkInteger(GID_CACHE, pc.cur.cachekb, 6),
            Child, cacheused = InfoText(""),
            Child, MkLabel(MSG_FONTMEM, MUIO_Label_DoubleFrame),
            Child, MkInteger(GID_FONTMEM, pc.cur.fontmemkb, 7),
            Child, fontmemused = InfoText(""),
            Child, MkLabel(MSG_CHARSET, MUIO_Label_DoubleFrame),
            Child, MkCycle(MSG_CHARSET, GID_CHARSET, charsets,
                         pc.cur.charset),
            Child, HSpace(0),
        End,
        Child, InfoText(GetString(MSG_CHARSET_NOTE)),
        Child, VSpace(0),
        Child, HGroup,
            Child, InfoText(GetString(MSG_MANAGER_NOTE)),
            Child, HSpace(0),
            Child, MkButton(MSG_DIAG_MANAGER, GID_MANAGER),
        End,
    End;
}

static Object *MappingsPage(void)
{
    static struct Hook display;

    display.h_Entry = (HOOKFUNC)MapDisplayEntry;

    return VGroup,
        Child, InfoText(GetString(MSG_MAP_INFO)),
        Child, ListviewObject,
            MUIA_CycleChain, TRUE,
            MUIA_Listview_List, maplist = ListObject,
                InputListFrame,
                MUIA_List_Format, (ULONG)"BAR,BAR,",
                MUIA_List_Title, TRUE,
                MUIA_List_DisplayHook, (ULONG)&display,
            End,
        End,
        Child, HGroup,
            Child, MkButton(MSG_MAP_ADD, GID_MAPADD),
            Child, MkButton(MSG_MAP_CHANGE, GID_MAPCHANGE),
            Child, MkButton(MSG_REMOVE, GID_MAPREMOVE),
            Child, HSpace(0),
            Child, MkLabel(MSG_MAP_PX, MUIO_Label_DoubleFrame),
            Child, MkInteger(GID_MAPPX, 0, 4),
        End,
        Child, VGroup,
            GroupFrameT(GetString(MSG_PREVIEW)),
            Child, mappreview = NewObject(boxclass->mcc_Class, NULL,
                                          MUIA_AABox_Kind, BOX_MAPPING,
                                          TAG_DONE),
        End,
    End;
}

/* The standard Prefs menus; labels are filled in by OpenWin(). */
static struct NewMenu menu[] =
{
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "O",  0, 0, (APTR)MENU_OPEN },
    { NM_ITEM,  NULL, "A",  0, 0, (APTR)MENU_SAVEAS },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "?",  0, 0, (APTR)MENU_ABOUT },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "Q",  0, 0, (APTR)MENU_QUIT },
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, "D",  0, 0, (APTR)MENU_DEFAULTS },
    { NM_ITEM,  NULL, "L",  0, 0, (APTR)MENU_LASTSAVED },
    { NM_ITEM,  NULL, "R",  0, 0, (APTR)MENU_RESTORE },
    { NM_TITLE, NULL, NULL, 0, 0, NULL },
    { NM_ITEM,  NULL, NULL, 0, 0, (APTR)MENU_MUISETTINGS },
    { NM_ITEM,  NULL, "H",  0, 0, (APTR)MENU_HELP },
    { NM_END,   NULL, NULL, 0, 0, NULL }
};

static void LocalizeMenu(void)
{
    static const LONG labels[] =
    {
        MSG_MENU_PROJECT, MSG_MENU_OPEN, MSG_MENU_SAVEAS, 0,
        MSG_MENU_ABOUT, 0, MSG_MENU_QUIT,
        MSG_MENU_EDIT, MSG_MENU_DEFAULTS, MSG_MENU_LASTSAVED,
        MSG_MENU_RESTORE,
        MSG_MENU_SETTINGS, MSG_MENU_MUISETTINGS, MSG_MENU_HELP
    };
    ULONG i;

    for (i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
        if (labels[i])
            menu[i].nm_Label = (CONST_STRPTR)GetString(labels[i]);
}

/* obj sends id back to the main loop when attr changes */
static void Notify(Object *o, ULONG attr, ULONG value, LONG id)
{
    if (o)
        DoMethod(o, MUIM_Notify, attr, value, (ULONG)app, 2,
                 MUIM_Application_ReturnID, id);
}

static BOOL OpenWin(const char *pubname)
{
    LONG i;

    LocalizeMenu();
    tabs[0] = GetString(MSG_TAB_APPEARANCE);
    tabs[1] = GetString(MSG_TAB_PROGRAMS);
    tabs[2] = GetString(MSG_TAB_ADVANCED);
    tabs[3] = GetString(MSG_TAB_MAPPINGS);
    subpix[0] = GetString(MSG_SP_OFF);
    subpix[1] = GetString(MSG_SP_RGB);
    subpix[2] = GetString(MSG_SP_BGR);
    hints[0] = GetString(MSG_HINT_NORMAL);
    hints[1] = GetString(MSG_HINT_LIGHT);
    hints[2] = GetString(MSG_HINT_NONE);
    hints[3] = GetString(MSG_HINT_FULL);
    for (i = 0; i < AA_NUM_CHARSETS; i++)
        charsets[i] = GetString(charset_msg[i]);

    app = ApplicationObject,
        MUIA_Application_Title, (ULONG)"AATextPrefs",
        MUIA_Application_Version, (ULONG)version,
        MUIA_Application_Description, (ULONG)GetString(MSG_WINDOW_TITLE),
        MUIA_Application_Base, (ULONG)"AATEXTPREFS",
        MUIA_Application_Menustrip, Make(MUIO_MenustripNM, (ULONG)menu, 0, 0),
        SubWindow, win = WindowObject,
            MUIA_Window_Title, (ULONG)GetString(MSG_WINDOW_TITLE),
            MUIA_Window_ScreenTitle, (ULONG)GetString(MSG_WINDOW_TITLE),
            MUIA_Window_ID, MAKE_ID('A','A','T','P'),
            pubname[0] ? MUIA_Window_PublicScreen : TAG_IGNORE,
                (ULONG)pubname,
            WindowContents, VGroup,
                Child, obj[GID_TABS] = RegisterGroup(tabs),
                    MUIA_CycleChain, TRUE,
                    Child, AppearancePage(),
                    Child, ProgramsPage(),
                    Child, AdvancedPage(),
                    Child, MappingsPage(),
                End,
                Child, status = TextObject,
                    MUIA_Text_PreParse, (ULONG)"",
                    MUIA_Text_Contents, (ULONG)"",
                End,
                Child, HGroup,
                    MUIA_Group_SameSize, TRUE,
                    Child, MkButton(MSG_SAVE, GID_SAVE),
                    Child, HSpace(0),
                    Child, MkButton(MSG_USE, GID_USE),
                    Child, HSpace(0),
                    Child, MkButton(MSG_CANCEL, GID_CANCEL),
                End,
            End,
        End,
    End;
    if (!app)
        return FALSE;

    for (i = GID_ADD; i < GID_COUNT; i++)
        if (obj[i])
            set(obj[i], MUIA_CycleChain, TRUE);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app,
             2, MUIM_Application_ReturnID, GID_CANCEL);
    Notify(obj[GID_TABS], MUIA_Group_ActivePage, MUIV_EveryTime, GID_TABS);
    Notify(obj[GID_GAMMA], MUIA_Numeric_Value, MUIV_EveryTime, GID_GAMMA);
    Notify(obj[GID_SUBPIXEL], MUIA_Cycle_Active, MUIV_EveryTime,
           GID_SUBPIXEL);
    Notify(obj[GID_HINTING], MUIA_Cycle_Active, MUIV_EveryTime, GID_HINTING);
    Notify(obj[GID_CHARSET], MUIA_Cycle_Active, MUIV_EveryTime, GID_CHARSET);
    for (i = GID_REAL; i <= GID_ANTIALIAS; i++)
        Notify(obj[i], MUIA_Selected, MUIV_EveryTime, i);
    for (i = GID_AUTO; i <= GID_AGA; i++)
        Notify(obj[i], MUIA_Selected, MUIV_EveryTime, i);
    Notify(obj[GID_PROGNAME], MUIA_String_Acknowledge, MUIV_EveryTime,
           GID_ADD);
    Notify(obj[GID_CACHE], MUIA_String_Acknowledge, MUIV_EveryTime,
           GID_CACHE);
    Notify(obj[GID_FONTMEM], MUIA_String_Acknowledge, MUIV_EveryTime,
           GID_FONTMEM);
    Notify(obj[GID_MAPPX], MUIA_String_Acknowledge, MUIV_EveryTime,
           GID_MAPPX);
    Notify(maplist, MUIA_List_Active, MUIV_EveryTime, GID_MAPLIST);
    for (i = GID_ADD; i <= GID_PICKFILE; i++)
        Notify(obj[i], MUIA_Pressed, FALSE, i);
    Notify(obj[GID_MANAGER], MUIA_Pressed, FALSE, GID_MANAGER);
    for (i = GID_MAPADD; i <= GID_MAPREMOVE; i++)
        Notify(obj[i], MUIA_Pressed, FALSE, i);
    for (i = GID_SAVE; i <= GID_CANCEL; i++)
        Notify(obj[i], MUIA_Pressed, FALSE, i);
    /* a double click in the running programs takes that one */
    DoMethod(runlv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
             (ULONG)popobj, 2, MUIM_Popstring_Close, TRUE);

    ShowBlacklist();
    ShowMappings(-1);
    UpdateCacheUsed();
    QueryAAText();
    set(win, MUIA_Window_Open, TRUE);
    return Get(win, MUIA_Window_Open) != 0;
}

/* ------------------------------------------------------------------ */

/* An action of the main loop; TRUE when the window is to close. */
static BOOL Action(ULONG id)
{
    switch (id)
    {
        case GID_GAMMA:
        case GID_HINTING:
        case GID_AUTO:
        case GID_OFFSCREEN:
        case GID_AGA:
        case GID_ANTIALIAS:
        case GID_SUBPIXEL:
        case GID_CACHE:
        case GID_FONTMEM:
        case GID_CHARSET:
            LiveApply();
            break;

        case GID_REAL:
        case GID_KERNING:
            ReadGadgets();
            break;

        case GID_TABS:
            switch (CurrentPage())
            {
                case 2:
                    UpdateCacheUsed();
                    break;
                case 3:
                    MapPreview();
                    break;
            }
            break;

        case GID_MAPLIST:
            MapSelectionChanged();
            break;
        case GID_MAPADD:
            EditMapping(-1);
            break;
        case GID_MAPCHANGE:
            if (MapSelected() >= 0)
                EditMapping(MapSelected());
            break;
        case GID_MAPREMOVE:
            RemoveMapping();
            break;
        case GID_MAPPX:
            MapPixelSize();
            break;

        case GID_ADD:
        {
            char *text = (char *)Get(obj[GID_PROGNAME], MUIA_String_Contents);

            if (text)
                AddProgram(text);
            break;
        }
        case GID_REMOVE:
            RemoveProgram();
            break;
        case GID_PICKFILE:
            PickProgram();
            break;
        case GID_MANAGER:
            StartManager();
            break;

        case GID_USE:
            ReadGadgets();
            if (WritePrefsFile(ENV_PREFS, ENV_PREFS) && WriteCodePage(FALSE))
            {
                Send(AACMD_RELOAD, NULL);
                return TRUE;
            }
            break;

        case GID_SAVE:
            ReadGadgets();
            /* ENVARC: keeps its own comments and mappings */
            if (WritePrefsFile(ENVARC_PREFS, ENVARC_PREFS) &&
                WritePrefsFile(ENV_PREFS, ENVARC_PREFS) &&
                WriteCodePage(TRUE))
            {
                Send(AACMD_RELOAD, NULL);
                return TRUE;
            }
            break;

        case GID_CANCEL:
            if (pc.tested)
                Send(AACMD_APPLY, &pc.before);
            return TRUE;
    }
    return FALSE;
}

/* Show cur in every object and let AAText use it. */
static void ShowPrefs(void)
{
    nnset(obj[GID_GAMMA], MUIA_Numeric_Value, GammaLevel(pc.cur.gamma100));
    nnset(obj[GID_HINTING], MUIA_Cycle_Active, HintIndex(pc.cur.hinting));
    nnset(obj[GID_REAL], MUIA_Selected, pc.cur.autoreal);
    nnset(obj[GID_KERNING], MUIA_Selected, pc.cur.kerning);
    nnset(obj[GID_AUTO], MUIA_Selected, pc.cur.autodetect);
    nnset(obj[GID_OFFSCREEN], MUIA_Selected, pc.cur.offscreen);
    nnset(obj[GID_AGA], MUIA_Selected, pc.cur.aga);
    nnset(obj[GID_ANTIALIAS], MUIA_Selected, pc.cur.antialias);
    nnset(obj[GID_SUBPIXEL], MUIA_Cycle_Active, pc.cur.subpixel);
    nnset(obj[GID_CACHE], MUIA_String_Integer, pc.cur.cachekb);
    nnset(obj[GID_FONTMEM], MUIA_String_Integer, pc.cur.fontmemkb);
    nnset(obj[GID_CHARSET], MUIA_Cycle_Active, pc.cur.charset);
    ShowBlacklist();
    ShowMappings(-1);
    LiveApply();
}

/* Read a prefs file into the window. */
static void LoadPrefs(const char *path)
{
    static struct AAPrefs p;    /* ~6.5 KB: not on the stack */

    if (aa_ReadPrefs(&p, path, FALSE))
    {
        CopyMem(&p, &pc.cur, sizeof(pc.cur));
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
                             ASLFR_Window, (ULONG)IWin(),
                             ASLFR_TitleText, (ULONG)GetString(title),
                             ASLFR_InitialDrawer, (ULONG)drawer,
                             ASLFR_InitialFile, (ULONG)"AAText.prefs",
                             ASLFR_DoSaveMode, save,
                             ASLFR_RejectIcons, TRUE,
                             TAG_DONE);
    if (!fr)
        return FALSE;
    set(app, MUIA_Application_Sleep, TRUE);
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0])
    {
        snprintf(buf, len, "%s", fr->fr_Drawer ? (char *)fr->fr_Drawer : "");
        ok = AddPart((STRPTR)buf, fr->fr_File, len);
    }
    set(app, MUIA_Application_Sleep, FALSE);
    FreeAslRequest(fr);
    return ok;
}

/* Program, version, author, licences */
static void ShowAbout(void)
{
    ULONG arg = (ULONG)(version + 6);       /* after "$VER: " */

    Ask(MSG_WINDOW_TITLE, MSG_ABOUT_TEXT, MSG_OK, &arg);
}

/* The documentation in the window's language */
static void ShowHelp(void)
{
    const char *path = gh_HelpFile(StringsLanguage());
    ULONG arg = (ULONG)path;

    if (!gh_ShowFile(path))
        Ask(MSG_WINDOW_TITLE, MSG_HELP_NOFILE, MSG_OK, &arg);
}

/* A menu item; TRUE when the window is to close. */
static BOOL MenuAction(ULONG id)
{
    static char path[AA_PATH_LEN];

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
            aa_DefaultPrefs(&pc.cur);
            ShowPrefs();
            break;
        case MENU_LASTSAVED:
            LoadPrefs(ENVARC_PREFS);
            break;
        case MENU_RESTORE:
            CopyMem(&pc.orig, &pc.cur, sizeof(pc.cur));
            ShowPrefs();
            break;
        case MENU_ABOUT:
            ShowAbout();
            break;
        case MENU_MUISETTINGS:
            DoMethod(app, MUIM_Application_OpenConfigWindow, 0);
            break;
        case MENU_HELP:
            ShowHelp();
            break;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    char language[AA_NAME_LEN] = "", pubname[MAXPUBSCREENNAME + 1] = "";
    struct Screen *scr = NULL;
    ULONG sigs = 0;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

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
    AATextBase = OpenLibrary((CONST_STRPTR)"aatext.library", 1);
    InitStrings(language[0] ? language : NULL);
    pc_Init();
    pc_Query();

    /* the screen font for the preview, from the screen the window uses */
    if (pubname[0])
        scr = LockPubScreen((CONST_STRPTR)pubname);
    if (!scr)
    {
        scr = LockPubScreen(NULL);
        pubname[0] = 0;
    }
    if (!scr)
        goto out;
    previewfont = OpenDiskFont(scr->Font);
    MakeFontInfo(scr->Font);
    UnlockPubScreen(NULL, scr);

    boxclass = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL,
                                     sizeof(struct BoxData), (APTR)BoxEntry);
    gammaclass = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Slider, NULL, 0,
                                       (APTR)GammaEntry);
    if (!boxclass || !gammaclass || !OpenWin(pubname))
    {
        Printf((CONST_STRPTR)"AATextPrefs: cannot open the window\n");
        goto out;
    }

    while (!done)
    {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);

        if (id == (ULONG)MUIV_Application_ReturnID_Quit)
            done = Action(GID_CANCEL);
        else if (id > MENU_BASE)
            done = MenuAction(id);
        else if (id)
            done = Action(id);
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
    if (gammaclass)
        MUI_DeleteCustomClass(gammaclass);
    if (boxclass)
        MUI_DeleteCustomClass(boxclass);
    if (pvready)
        pv_Cleanup();
    if (AATextBase)
        CloseLibrary(AATextBase);
    AATextBase = NULL;
    if (previewfont)
        CloseFont(previewfont);
    FreeStrings();
    CloseLibs();
    return rc;
}
