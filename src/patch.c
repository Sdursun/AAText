/*
 * AAText - Text() patch installation, removal and the C side of the hook.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <graphics/rastport.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "patch.h"
#include "debug.h"

#define LVO_Text    (-60)

#define AA_MAX_TASKS 32

/* stub.s */
extern void aa_TextStub(void);

volatile LONG aa_UseCount;

static struct GfxBase *aa_GfxBase;
static APTR aa_OrigText;
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
                    : "r" (_a6), "a" (aa_OrigText)
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

    /* Stage 1: everything goes to the original function. */
    CallOrigText(rp, string, count, gfx);

    LeaveTask(me);
}

BOOL aa_Install(struct GfxBase *gfx)
{
    aa_GfxBase = gfx;
    aa_Passthrough = FALSE;

    Forbid();
    aa_OrigText = SetFunction((struct Library *)gfx, LVO_Text,
                              (APTR)aa_TextStub);
    CacheClearU();
    Permit();

    D(("AAText: installed, original Text() at %lx\n", (ULONG)aa_OrigText));
    return aa_OrigText != NULL;
}

BOOL aa_Remove(void)
{
    APTR current;

    Forbid();
    /* Jump table entry: JMP abs.l (0x4EF9) followed by the address. */
    current = *(APTR *)((UBYTE *)aa_GfxBase + LVO_Text + 2);
    if (current != (APTR)aa_TextStub)
    {
        aa_Passthrough = TRUE;
        Permit();
        D(("AAText: vector is %lx (not ours), staying in pass-through\n",
           (ULONG)current));
        return FALSE;
    }
    SetFunction((struct Library *)aa_GfxBase, LVO_Text, aa_OrigText);
    CacheClearU();
    Permit();

    /* Wait until no task is executing inside our code. */
    while (aa_UseCount)
        Delay(2);
    /* A caller may still be between "subq" and "rts" in the stub. */
    Delay(10);

#ifdef DEBUG
    PrintStats();
#endif
    D(("AAText: removed\n"));
    return TRUE;
}
