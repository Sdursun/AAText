/*
 * aabench - text drawing benchmark for AAText. Draws in a window on the
 * Workbench screen and reports characters, lines and scrolls per second.
 *
 *   aabench [FONT <name.font>] [SIZE <n>] [SECS <n>] [OLD <program>]
 *           [NEW <program>] [NOAA] [OUT <file>]
 *
 * Without OLD/NEW the tests run once with whatever runs now. With OLD
 * and NEW the running AAText is stopped, the tests run with OLD, then
 * with NEW (and with NOAA also without AAText), and the results are
 * shown side by side; at the end OLD is started again. OUT appends the
 * table to a file as well. Default font: the screen font.
 *
 * Tests (each runs SECS seconds, default 3, after a warm-up pass):
 *   short     8 character strings, as in gadgets and menus
 *   line      80 character lines, JAM1 (background read and blended)
 *   jam2      80 character lines, JAM2 (background filled)
 *   bold      bold and italic lines (algorithmic styles)
 *   turkish   lines of Turkish letters
 *   measure   TextLength() + TextExtent() of a line (no drawing)
 *   scroll    scroll the window up one line, draw a new bottom line
 *             (a Shell or editor while text comes in)
 *   page      clear the window and draw all its lines (a viewer page)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>
#include <proto/timer.h>

#define AA_PORTNAME "AAText"           /* src/aamsg.h */
#define MAX_RUNS    3
#define NUM_TESTS   8
#define WIN_W       640
#define WIN_H       400

struct Library *DiskfontBase;
struct Device *TimerBase;

static const char *const test_name[NUM_TESTS] =
{
    "short", "line", "jam2", "bold", "turkish", "measure", "scroll", "page"
};
/* what one iteration counts as in the table */
static const char *const test_unit[NUM_TESTS] =
{
    "chars/s", "chars/s", "chars/s", "chars/s", "chars/s", "calls/s",
    "scrolls/s", "pages/s"
};

static const char *const words[] =
{
    "Open... ", "Save as ", "Cancel  ", "Quit    ", "Settings", "Window  ",
    "Iconify ", "Snapshot"
};
static const char *const lines[] =
{
    "The quick brown fox jumps over the lazy dog. 0123456789 (illustrate) WAVE",
    "int main(int argc, char **argv) { return printf(\"%s\\n\", argv[0]); }  ",
    "Amiga Workbench 3.2 - AAText antialiased TrueType text, RTG screens  ",
    "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ !\"#$%&/()=?*+-"
};
/* ISO-8859-9 */
static const char turkish[] =
    "Pijamal\xFD hasta ya\xF0\xFDz \xFE"
    "of\xF6re \xE7" "abucak g\xFC" "vendi. \xC7\xD0\xDD\xD6\xDE\xDC \xE7\xF0\xFD\xF6\xFE\xFC";

struct Bench
{
    struct Window *win;
    struct RastPort *rp;
    struct TextFont *tf;
    struct timerequest tr;
    ULONG freq;
    ULONG secs;
};

static double Now(struct Bench *b)
{
    struct EClockVal e;

    ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) /
           (double)b->freq;
}

static void Clear(struct Bench *b)
{
    struct Window *w = b->win;

    SetAPen(b->rp, 0);
    RectFill(b->rp, w->BorderLeft, w->BorderTop,
             w->Width - w->BorderRight - 1, w->Height - w->BorderBottom - 1);
    SetAPen(b->rp, 1);
}

static LONG Rows(struct Bench *b)
{
    struct Window *w = b->win;

    return (w->Height - w->BorderTop - w->BorderBottom) /
           b->tf->tf_YSize;
}

/* draw string s at row r (wrapping around the window) */
static void Line(struct Bench *b, LONG r, const char *s, LONG len)
{
    struct Window *w = b->win;

    Move(b->rp, w->BorderLeft + 2,
         w->BorderTop + (r % Rows(b)) * b->tf->tf_YSize + b->tf->tf_Baseline);
    Text(b->rp, (STRPTR)s, len);
}

/*
 * One iteration of test t; returns what it counts (characters, calls,
 * scrolls or pages). i is the iteration number.
 */
static ULONG Step(struct Bench *b, LONG t, ULONG i)
{
    struct RastPort *rp = b->rp;
    struct Window *w = b->win;
    const char *s;
    LONG len, r, rows = Rows(b);
    struct TextExtent te;

    switch (t)
    {
        case 0:                         /* short */
            s = words[i & 7];
            Move(rp, w->BorderLeft + 2 + (i & 7) * 70,
                 w->BorderTop + ((i >> 3) % rows) * b->tf->tf_YSize +
                 b->tf->tf_Baseline);
            Text(rp, (STRPTR)s, 8);
            return 8;
        case 1:                         /* line */
        case 2:                         /* jam2 */
        case 3:                         /* bold */
            s = lines[i & 3];
            len = strlen(s);
            Line(b, i, s, len);
            return len;
        case 4:                         /* turkish */
            len = strlen(turkish);
            Line(b, i, turkish, len);
            return len;
        case 5:                         /* measure */
            s = lines[i & 3];
            len = strlen(s);
            TextLength(rp, (STRPTR)s, len);
            TextExtent(rp, (STRPTR)s, len, &te);
            return 1;
        case 6:                         /* scroll */
            ScrollRaster(rp, 0, b->tf->tf_YSize, w->BorderLeft,
                         w->BorderTop, w->Width - w->BorderRight - 1,
                         w->BorderTop + rows * b->tf->tf_YSize - 1);
            s = lines[i & 3];
            Line(b, rows - 1, s, strlen(s));
            return 1;
        case 7:                         /* page */
            Clear(b);
            for (r = 0; r < rows; r++)
            {
                s = lines[(i + r) & 3];
                Line(b, r, s, strlen(s));
            }
            return 1;
    }
    return 0;
}

/* Run test t for b->secs seconds; returns units per second. */
static double RunTest(struct Bench *b, LONG t)
{
    double t0, t1, n = 0;
    ULONG i;

    Clear(b);
    SetDrMd(b->rp, t == 2 ? JAM2 : JAM1);
    SetBPen(b->rp, 0);
    SetSoftStyle(b->rp, t == 3 ? FSF_BOLD | FSF_ITALIC : 0,
                 FSF_BOLD | FSF_ITALIC);

    /* warm-up: glyph caches, font loading */
    for (i = 0; i < 64; i++)
        Step(b, t, i);
    Clear(b);
    WaitBlit();

    t0 = Now(b);
    t1 = t0;
    for (i = 0; t1 - t0 < (double)b->secs; i++)
    {
        n += Step(b, t, i);
        if ((i & 15) == 15)
        {
            WaitBlit();
            t1 = Now(b);
            if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
                break;
        }
    }
    WaitBlit();
    t1 = Now(b);

    SetSoftStyle(b->rp, 0, FSF_BOLD | FSF_ITALIC);
    SetDrMd(b->rp, JAM1);
    return t1 > t0 ? n / (t1 - t0) : 0;
}

static void RunAll(struct Bench *b, double *res, const char *label)
{
    LONG t;

    SetWindowTitles(b->win, (STRPTR)label, (STRPTR)-1);
    for (t = 0; t < NUM_TESTS; t++)
    {
        res[t] = RunTest(b, t);
        printf("  %-8s %-8s %10.0f %s\n", label, test_name[t], res[t],
               test_unit[t]);
        fflush(stdout);
    }
}

/* ---- starting and stopping AAText ----------------------------------- */

static BOOL Running(void)
{
    struct MsgPort *p;

    Forbid();
    p = FindPort((CONST_STRPTR)AA_PORTNAME);
    Permit();
    return p != NULL;
}

/* Ctrl-C to the running AAText, as "AAText QUIT" does; wait for it */
static BOOL StopAAText(void)
{
    struct MsgPort *p;
    LONG i;

    Forbid();
    p = FindPort((CONST_STRPTR)AA_PORTNAME);
    if (p && p->mp_SigTask)
        Signal(p->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();
    for (i = 0; i < 300 && Running(); i++)     /* up to 6 s */
        Delay(1);
    if (Running())
        return FALSE;
    Delay(50);                                 /* "removed" and exit */
    return TRUE;
}

static BOOL StartAAText(const char *prog)
{
    char cmd[300];
    LONG i;

    snprintf(cmd, sizeof(cmd), "Run >NIL: \"%s\"", prog);
    if (SystemTags((STRPTR)cmd, SYS_Input, Open((STRPTR)"NIL:", MODE_OLDFILE),
                   SYS_Output, Open((STRPTR)"NIL:", MODE_NEWFILE),
                   TAG_DONE) != 0)
        return FALSE;
    for (i = 0; i < 500 && !Running(); i++)    /* up to 10 s */
        Delay(1);
    if (!Running())
        return FALSE;
    Delay(50);                                 /* patch installed */
    return TRUE;
}

/* ---- output --------------------------------------------------------- */

static void Table(FILE *f, const char *font, LONG size, ULONG secs,
                  const char *const *label, double res[][NUM_TESTS],
                  LONG nruns)
{
    LONG t, r;

    fprintf(f, "\naabench: %s %ld, %lu s per test\n", font, (long)size,
            (unsigned long)secs);
    fprintf(f, "%-8s %-9s", "test", "unit");
    for (r = 0; r < nruns; r++)
        fprintf(f, " %12s", label[r]);
    if (nruns >= 2)
        fprintf(f, " %9s", "new/old");
    fprintf(f, "\n");
    for (t = 0; t < NUM_TESTS; t++)
    {
        fprintf(f, "%-8s %-9s", test_name[t], test_unit[t]);
        for (r = 0; r < nruns; r++)
            fprintf(f, " %12.0f", res[r][t]);
        if (nruns >= 2 && res[0][t] > 0)
            fprintf(f, " %+8.1f%%", (res[1][t] / res[0][t] - 1.0) * 100.0);
        fprintf(f, "\n");
    }
}

int main(void)
{
    static const char template[] =
        "FONT/K,SIZE/K/N,SECS/K/N,OLD/K,NEW/K,NOAA/S,OUT/K";
    enum { A_FONT, A_SIZE, A_SECS, A_OLD, A_NEW, A_NOAA, A_OUT, A_COUNT };
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    struct Bench b;
    struct TextAttr ta;
    struct Screen *scr;
    char fontname[64];
    const char *label[MAX_RUNS];
    static double res[MAX_RUNS][NUM_TESTS];
    LONG nruns = 0, rc = RETURN_OK;
    BOOL compare;

    memset(&b, 0, sizeof(b));
    rda = ReadArgs((STRPTR)template, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"aabench");
        return RETURN_FAIL;
    }
    compare = args[A_OLD] && args[A_NEW];
    if ((args[A_OLD] || args[A_NEW]) && !compare)
    {
        printf("aabench: OLD and NEW go together\n");
        FreeArgs(rda);
        return RETURN_ERROR;
    }
    b.secs = args[A_SECS] ? *(LONG *)args[A_SECS] : 3;
    if (b.secs < 1)
        b.secs = 1;

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&b.tr, 0))
    {
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    TimerBase = b.tr.tr_node.io_Device;
    {
        struct EClockVal e;

        b.freq = ReadEClock(&e);
    }
    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);

    /* the font: given, else the Workbench screen's */
    scr = LockPubScreen(NULL);
    if (args[A_FONT])
        snprintf(fontname, sizeof(fontname), "%s", (char *)args[A_FONT]);
    else if (scr && scr->Font)
        snprintf(fontname, sizeof(fontname), "%s", scr->Font->ta_Name);
    else
        strcpy(fontname, "topaz.font");
    ta.ta_Name = (STRPTR)fontname;
    ta.ta_YSize = args[A_SIZE] ? *(LONG *)args[A_SIZE] :
                  scr && scr->Font ? scr->Font->ta_YSize : 8;
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    if (scr)
        UnlockPubScreen(NULL, scr);

    if (compare && !StopAAText())
    {
        printf("aabench: the running AAText does not stop\n");
        rc = RETURN_FAIL;
        goto out;
    }

    /* the font after stopping: no AAText state left from the old run */
    b.tf = DiskfontBase ? OpenDiskFont(&ta) : NULL;
    if (!b.tf)
    {
        printf("aabench: cannot open %s %ld\n", fontname,
               (long)ta.ta_YSize);
        rc = RETURN_FAIL;
        goto restart;
    }
    b.win = OpenWindowTags(NULL,
                           WA_Left, 20, WA_Top, 40,
                           WA_InnerWidth, WIN_W, WA_InnerHeight, WIN_H,
                           WA_Title, (ULONG)"aabench",
                           WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                           WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                           TAG_DONE);
    if (!b.win)
    {
        rc = RETURN_FAIL;
        goto restart;
    }
    b.rp = b.win->RPort;
    SetFont(b.rp, b.tf);
    printf("aabench: %s %ld, %lu s per test, window %ldx%ld, %ld rows\n",
           fontname, (long)ta.ta_YSize, (unsigned long)b.secs,
           (long)WIN_W, (long)WIN_H, (long)Rows(&b));

    if (!compare)
    {
        label[0] = Running() ? "AAText" : "system";
        RunAll(&b, res[0], label[0]);
        nruns = 1;
    }
    else
    {
        label[0] = "old";
        if (!StartAAText((char *)args[A_OLD]))
        {
            printf("aabench: cannot start %s\n", (char *)args[A_OLD]);
            rc = RETURN_FAIL;
            goto restart;
        }
        RunAll(&b, res[0], label[0]);
        StopAAText();
        nruns = 1;

        label[1] = "new";
        if (!StartAAText((char *)args[A_NEW]))
        {
            printf("aabench: cannot start %s\n", (char *)args[A_NEW]);
            rc = RETURN_FAIL;
            goto restart;
        }
        RunAll(&b, res[1], label[1]);
        StopAAText();
        nruns = 2;

        if (args[A_NOAA])
        {
            label[2] = "system";
            RunAll(&b, res[2], label[2]);
            nruns = 3;
        }
    }

    Table(stdout, fontname, ta.ta_YSize, b.secs, label, res, nruns);
    if (args[A_OUT])
    {
        FILE *f = fopen((char *)args[A_OUT], "a");

        if (f)
        {
            Table(f, fontname, ta.ta_YSize, b.secs, label, res, nruns);
            fclose(f);
        }
        else
            printf("aabench: cannot write %s\n", (char *)args[A_OUT]);
    }

restart:
    if (b.win)
        CloseWindow(b.win);
    b.win = NULL;
    if (compare && !Running() && !StartAAText((char *)args[A_OLD]))
        printf("aabench: could not start %s again\n", (char *)args[A_OLD]);

out:
    if (b.win)
        CloseWindow(b.win);
    if (b.tf)
        CloseFont(b.tf);
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
    CloseDevice((struct IORequest *)&b.tr);
    FreeArgs(rda);
    return rc;
}
