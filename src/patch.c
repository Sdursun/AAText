/*
 * AAText - patch installation, removal and the C side of the hook.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <graphics/rastport.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "patch.h"
#include "render.h"
#include "metrics.h"
#include "debug.h"

#define AA_MAX_TASKS 32

/* stub.s */
extern void aa_TextStub(void);
extern void aa_TextLengthStub(void);
extern void aa_TextExtentStub(void);
extern void aa_TextFitStub(void);
extern LONG aa_CallTextLength(APTR fn, struct RastPort *rp, CONST_STRPTR s,
                              LONG count, struct GfxBase *gfx);
extern LONG aa_CallTextExtent(APTR fn, struct RastPort *rp, CONST_STRPTR s,
                              LONG count, struct TextExtent *te,
                              struct GfxBase *gfx);
extern ULONG aa_CallTextFit(APTR fn, struct RastPort *rp, CONST_STRPTR s,
                            LONG len, struct TextExtent *te,
                            const struct TextExtent *cte, LONG dir,
                            LONG bitwidth, LONG bitheight,
                            struct GfxBase *gfx);

volatile LONG aa_UseCount;

static struct GfxBase *aa_GfxBase;
static volatile BOOL aa_Passthrough;

/* Tasks currently inside the hook, for reentrancy protection. */
static struct Task *aa_BusyTasks[AA_MAX_TASKS];

#ifdef DEBUG
/*
 * Per-task statistics: which tasks call Text(), whether they are DOS
 * processes and how much stack they have left. Printed on removal.
 */
#define AA_MAX_STATS 64

struct TaskStat
{
    struct Task *task;
    char         name[24];
    UBYTE        type;          /* NT_TASK or NT_PROCESS */
    ULONG        calls;
    ULONG        stacksize;
    LONG         minfree;       /* lowest free stack seen, -1 = unknown */
};

static struct TaskStat aa_Stats[AA_MAX_STATS];
static ULONG aa_CallCount;
static ULONG aa_StatsOverflow;

static void CopyName(char *dst, const char *src, int size)
{
    int i;

    if (!src)
        src = "?";
    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static void RecordCall(struct Task *me)
{
    struct TaskStat *st = NULL;
    UBYTE *sp = (UBYTE *)&st;   /* approximate current stack pointer */
    LONG spfree;
    int i;

    if (sp >= (UBYTE *)me->tc_SPLower && sp <= (UBYTE *)me->tc_SPUpper)
        spfree = sp - (UBYTE *)me->tc_SPLower;
    else
        spfree = -1;

    aa_CallCount++;

    Forbid();
    for (i = 0; i < AA_MAX_STATS; i++)
    {
        if (aa_Stats[i].task == me)
        {
            st = &aa_Stats[i];
            break;
        }
        if (!aa_Stats[i].task)
        {
            st = &aa_Stats[i];
            st->task = me;
            CopyName(st->name, me->tc_Node.ln_Name, sizeof(st->name));
            st->type = me->tc_Node.ln_Type;
            st->stacksize = (UBYTE *)me->tc_SPUpper - (UBYTE *)me->tc_SPLower;
            st->minfree = spfree;
            Permit();
            kprintf("AAText: new caller \"%s\" %s stack=%ld free=%ld\n",
                    st->name, st->type == NT_PROCESS ? "process" : "TASK",
                    st->stacksize, spfree);
            Forbid();
            break;
        }
    }
    if (st)
    {
        st->calls++;
        if (spfree >= 0 && (st->minfree < 0 || spfree < st->minfree))
            st->minfree = spfree;
    }
    else
        aa_StatsOverflow++;
    Permit();
}

static void PrintStats(void)
{
    int i;

    kprintf("AAText: --- Text() callers: %ld calls total ---\n", aa_CallCount);
    kprintf("AAText: %-24s %-7s %8s %8s %8s\n",
            (ULONG)"task", (ULONG)"type", (ULONG)"calls",
            (ULONG)"stack", (ULONG)"minfree");
    for (i = 0; i < AA_MAX_STATS && aa_Stats[i].task; i++)
    {
        struct TaskStat *st = &aa_Stats[i];

        kprintf("AAText: %-24s %-7s %8ld %8ld %8ld\n",
                (ULONG)st->name,
                (ULONG)(st->type == NT_PROCESS ? "process" : "TASK"),
                st->calls, st->stacksize, st->minfree);
    }
    if (aa_StatsOverflow)
        kprintf("AAText: (%ld calls from tasks not in table)\n",
                aa_StatsOverflow);
}
#endif

/*
 * Patched graphics.library functions. Text() is always patched; the
 * three measuring functions only when a font uses real metrics.
 */
enum { P_TEXT, P_TEXTLENGTH, P_TEXTEXTENT, P_TEXTFIT, P_COUNT };

static const WORD patch_lvo[P_COUNT] = { -60, -54, -690, -696 };
#ifdef DEBUG
static const char *const patch_name[P_COUNT] =
{
    "Text", "TextLength", "TextExtent", "TextFit"
};
#endif
static void (*const patch_stub[P_COUNT])(void) =
{
    aa_TextStub, aa_TextLengthStub, aa_TextExtentStub, aa_TextFitStub
};
static APTR aa_Orig[P_COUNT];
static LONG aa_NumPatches;

/*
 * Call the original Text() (or whatever was in the vector before us)
 * with its register arguments.
 */
static inline void CallOrigText(struct RastPort *rp, CONST_STRPTR string,
                                LONG count, struct GfxBase *gfx)
{
    register struct RastPort *_a1 __asm("a1") = rp;
    register CONST_STRPTR     _a0 __asm("a0") = string;
    register LONG             _d0 __asm("d0") = count;
    register struct GfxBase  *_a6 __asm("a6") = gfx;

    __asm volatile ("jsr (%4)"
                    : "+r" (_d0), "+r" (_a0), "+r" (_a1)
                    : "r" (_a6), "a" (aa_Orig[P_TEXT])
                    : "d1", "cc", "memory");
}

/*
 * Mark the task as being inside the hook. Returns FALSE if it already
 * is (we were re-entered through a function we called ourselves) or if
 * the table is full; the caller must then pass straight through.
 */
static BOOL EnterTask(struct Task *me)
{
    struct Task **freeslot = NULL;
    int i;

    Forbid();
    for (i = 0; i < AA_MAX_TASKS; i++)
    {
        if (aa_BusyTasks[i] == me)
        {
            Permit();
            return FALSE;
        }
        if (!aa_BusyTasks[i] && !freeslot)
            freeslot = &aa_BusyTasks[i];
    }
    if (freeslot)
        *freeslot = me;
    Permit();

    return freeslot != NULL;
}

static void LeaveTask(struct Task *me)
{
    int i;

    /* A single aligned long write is atomic, no Forbid() needed. */
    for (i = 0; i < AA_MAX_TASKS; i++)
    {
        if (aa_BusyTasks[i] == me)
        {
            aa_BusyTasks[i] = NULL;
            break;
        }
    }
}

/* Called from aa_TextStub with the original register arguments. */
void aa_TextHook(struct RastPort *rp, CONST_STRPTR string, LONG count,
                 struct GfxBase *gfx)
{
    struct Task *me;

    if (aa_Passthrough)
    {
        CallOrigText(rp, string, count, gfx);
        return;
    }

    me = FindTask(NULL);
    if (!EnterTask(me))
    {
        CallOrigText(rp, string, count, gfx);
        return;
    }

#ifdef DEBUG
    RecordCall(me);
#endif

    if (!aa_RenderText(rp, string, (UWORD)count, me))
        CallOrigText(rp, string, count, gfx);

    LeaveTask(me);
}

/*
 * Measuring hooks. They decide by font only (see aa_RealMetrics()), call
 * no patched function and never wait on graphics locks, so they need no
 * reentrancy tracking.
 */
LONG aa_TextLengthHook(struct RastPort *rp, CONST_STRPTR string, LONG count,
                       struct GfxBase *gfx)
{
    struct AAMetricsCtx m;

    if (!aa_Passthrough && aa_RealMetrics(rp, FindTask(NULL), &m))
        return aa_MLength(&m, string, (UWORD)count);
    return aa_CallTextLength(aa_Orig[P_TEXTLENGTH], rp, string, count, gfx);
}

LONG aa_TextExtentHook(struct RastPort *rp, CONST_STRPTR string, LONG count,
                       struct TextExtent *te, struct GfxBase *gfx)
{
    struct AAMetricsCtx m;

    if (!aa_Passthrough && aa_RealMetrics(rp, FindTask(NULL), &m))
    {
        aa_MExtent(&m, string, (UWORD)count, te);
        return te->te_Width;
    }
    return aa_CallTextExtent(aa_Orig[P_TEXTEXTENT], rp, string, count, te,
                             gfx);
}

ULONG aa_TextFitHook(struct RastPort *rp, CONST_STRPTR string, LONG len,
                     struct TextExtent *te, const struct TextExtent *cte,
                     LONG dir, LONG bitwidth, LONG bitheight,
                     struct GfxBase *gfx)
{
    struct AAMetricsCtx m;

    if (!aa_Passthrough && aa_RealMetrics(rp, FindTask(NULL), &m))
        return aa_MFit(&m, string, (UWORD)len, te, cte, (WORD)dir,
                       (UWORD)bitwidth, (UWORD)bitheight);
    return aa_CallTextFit(aa_Orig[P_TEXTFIT], rp, string, len, te, cte, dir,
                          bitwidth, bitheight, gfx);
}

BOOL aa_Install(struct GfxBase *gfx, BOOL measuring)
{
    LONG i;

    aa_GfxBase = gfx;
    aa_Passthrough = FALSE;
    aa_NumPatches = measuring ? P_COUNT : 1;

    /* All vectors at once, so no caller sees a half-installed set. */
    Forbid();
    for (i = 0; i < aa_NumPatches; i++)
        aa_Orig[i] = SetFunction((struct Library *)gfx, patch_lvo[i],
                                 (APTR)patch_stub[i]);
    CacheClearU();
    Permit();

    for (i = 0; i < aa_NumPatches; i++)
        D(("AAText: patched %s(), original at %lx\n",
           (ULONG)patch_name[i], (ULONG)aa_Orig[i]));
    return aa_Orig[P_TEXT] != NULL;
}

BOOL aa_Remove(void)
{
    LONG i;

    Forbid();
    /*
     * Jump table entry: JMP abs.l (0x4EF9) followed by the address.
     * Only restore if every vector still points to us; otherwise
     * another program patched on top and we must stay (pass-through).
     */
    for (i = 0; i < aa_NumPatches; i++)
    {
        APTR current = *(APTR *)((UBYTE *)aa_GfxBase + patch_lvo[i] + 2);

        if (current != (APTR)patch_stub[i])
        {
            aa_Passthrough = TRUE;
            Permit();
            D(("AAText: %s() vector is %lx (not ours), staying in "
               "pass-through\n", (ULONG)patch_name[i], (ULONG)current));
            return FALSE;
        }
    }
    for (i = 0; i < aa_NumPatches; i++)
        SetFunction((struct Library *)aa_GfxBase, patch_lvo[i], aa_Orig[i]);
    CacheClearU();
    Permit();

    /* Wait until no task is executing inside our code. */
    while (aa_UseCount)
        Delay(2);
    /* A caller may still be between "subq" and "rts" in a stub. */
    Delay(10);

#ifdef DEBUG
    PrintStats();
    aa_PrintRenderStats();
#endif
    D(("AAText: removed\n"));
    return TRUE;
}
