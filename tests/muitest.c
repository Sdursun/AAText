/*
 * muitest - checks that the MUI 3.8 classes the MUI GUIs of AATextPrefs
 * and AATextManager will use build with our compiler and run: a Register
 * group, a multi-column List, a Gauge and a custom Area subclass that
 * draws into its box (as the font previews will).
 *
 *   muitest [seconds]
 */

#include <exec/types.h>
#include <libraries/mui.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>

struct Library *MUIMasterBase;

/* ---- the custom class: an Area that draws a few text lines ---------- */

struct BoxData
{
    LONG lines;
};

static ULONG BoxAskMinMax(struct IClass *cl, Object *obj,
                          struct MUIP_AskMinMax *msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);
    msg->MinMaxInfo->MinWidth += 200;
    msg->MinMaxInfo->DefWidth += 300;
    msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    msg->MinMaxInfo->MinHeight += 60;
    msg->MinMaxInfo->DefHeight += 60;
    msg->MinMaxInfo->MaxHeight += 60;
    return 0;
}

static ULONG BoxDraw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    struct RastPort *rp;
    LONG y;

    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & MADF_DRAWOBJECT))
        return 0;
    rp = _rp(obj);
    SetAPen(rp, _dri(obj)->dri_Pens[TEXTPEN]);
    SetDrMd(rp, JAM1);
    for (y = 0; y < 3; y++)
    {
        Move(rp, _mleft(obj) + 4,
             _mtop(obj) + 4 + rp->TxBaseline + y * (rp->TxHeight + 2));
        Text(rp, (STRPTR)"Pijamal\xFD hasta ya\xF0\xFDz \xFE" "of\xF6re", 27);
    }
    return 0;
}

static ULONG BoxDispatcher(struct IClass *cl, Object *obj, Msg msg)
{
    switch (msg->MethodID)
    {
        case MUIM_AskMinMax:
            return BoxAskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
        case MUIM_Draw:
            return BoxDraw(cl, obj, (struct MUIP_Draw *)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}

/* stack arguments from MUI's dispatcher call: registers a0/a2/a1 */
static ULONG BoxEntry(void)
{
    register struct IClass *cl __asm("a0");
    register Object *obj __asm("a2");
    register Msg msg __asm("a1");

    return BoxDispatcher(cl, obj, msg);
}

/* ---- the window ------------------------------------------------------ */

static const char *pages[] = { "Fonts", "Download", NULL };
static const char *rows[][3] =
{
    { "Ubuntu", "Bold", "324 KB" },
    { "Ubuntu", "Italic", "376 KB" },
    { "Verdana", "Regular", "171 KB" },
};

static LONG ListDisplay(void)
{
    register char **array __asm("a2");
    register const char **entry __asm("a1");

    if (entry)
    {
        array[0] = (char *)entry[0];
        array[1] = (char *)entry[1];
        array[2] = (char *)entry[2];
    }
    else
    {
        array[0] = "\033bFamily";
        array[1] = "\033bStyle";
        array[2] = "\033bSize";
    }
    return 0;
}

int main(int argc, char **argv)
{
    static struct Hook display;
    struct MUI_CustomClass *box;
    Object *app, *win, *list, *gauge;
    LONG secs = argc > 1 ? atoi(argv[1]) : 10, i;
    ULONG sigs = 0;
    int rc = 20;

    if (!(MUIMasterBase = OpenLibrary((CONST_STRPTR)MUIMASTER_NAME, 19)))
    {
        printf("muitest: muimaster.library 19 (MUI 3.8) or newer needed\n");
        return 20;
    }
    printf("muimaster.library %d.%d\n", MUIMasterBase->lib_Version,
           MUIMasterBase->lib_Revision);
    box = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL,
                                sizeof(struct BoxData), (APTR)BoxEntry);
    if (!box)
    {
        printf("muitest: MUI_CreateCustomClass failed\n");
        CloseLibrary(MUIMasterBase);
        return 20;
    }
    display.h_Entry = (HOOKFUNC)ListDisplay;

    app = ApplicationObject,
        MUIA_Application_Title, (ULONG)"muitest",
        MUIA_Application_Base, (ULONG)"MUITEST",
        SubWindow, win = WindowObject,
            MUIA_Window_Title, (ULONG)"AAText MUI 3.8 test",
            MUIA_Window_ID, MAKE_ID('M','T','S','T'),
            WindowContents, VGroup,
                Child, RegisterGroup(pages),
                    Child, VGroup,
                        Child, ListviewObject,
                            MUIA_Listview_List, list = ListObject,
                                InputListFrame,
                                MUIA_List_Format, (ULONG)"BAR,BAR,",
                                MUIA_List_Title, TRUE,
                                MUIA_List_DisplayHook, (ULONG)&display,
                            End,
                        End,
                        Child, NewObject(box->mcc_Class, NULL,
                                         TextFrame, TAG_DONE),
                    End,
                    Child, VGroup,
                        Child, gauge = GaugeObject,
                            GaugeFrame,
                            MUIA_Gauge_Horiz, TRUE,
                            MUIA_Gauge_Max, 100,
                            MUIA_Gauge_InfoText, (ULONG)"%ld %%",
                        End,
                    End,
                End,
            End,
        End,
    End;

    if (!app)
        printf("muitest: application not created\n");
    else
    {
        for (i = 0; i < 3; i++)
            DoMethod(list, MUIM_List_InsertSingle, (ULONG)rows[i],
                     MUIV_List_Insert_Bottom);
        set(gauge, MUIA_Gauge_Current, 60);
        DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
                 (ULONG)app, 2, MUIM_Application_ReturnID,
                 MUIV_Application_ReturnID_Quit);
        set(win, MUIA_Window_Open, TRUE);
        printf("window open\n");
        for (i = 0; i < secs * 50; i++)
        {
            if (DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs) ==
                (ULONG)MUIV_Application_ReturnID_Quit)
                break;
            if (sigs & SIGBREAKF_CTRL_C)
                break;
            Delay(1);
        }
        MUI_DisposeObject(app);
        rc = 0;
    }
    MUI_DeleteCustomClass(box);
    CloseLibrary(MUIMasterBase);
    return rc;
}
