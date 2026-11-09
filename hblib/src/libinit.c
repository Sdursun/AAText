/*
 * harfbuzz.library - library header: RomTag, init, open, close, expunge,
 * the function table, and hb_Call() around every function.
 *
 * AmigaOS 3.1+ (exec V40) on a 68020 or better. Of exec it needs memory
 * pools (V39) and StackSwap (V37); the IEEE math libraries are V37.
 */

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdarg.h>

#include "lib.h"
#include "hbmem.h"

struct ExecBase *SysBase;
struct HBBase *HBBase;

/*
 * libnix's floating point (soft float and libm) calls the IEEE math
 * libraries through these bases (libstubs); in a program its startup
 * code opens them, here LibInit() does. All exist since OS 2.0.
 */
struct Library *MathIeeeDoubBasBase, *MathIeeeDoubTransBase;
struct Library *MathIeeeSingBasBase, *MathIeeeSingTransBase;

static struct
{
    struct Library **base;
    const char *name;
} const MathLibs[] =
{
    { &MathIeeeDoubBasBase,   "mathieeedoubbas.library" },
    { &MathIeeeDoubTransBase, "mathieeedoubtrans.library" },
    { &MathIeeeSingBasBase,   "mathieeesingbas.library" },
    { &MathIeeeSingTransBase, "mathieeesingtrans.library" }
};
#define NUM_MATHLIBS (sizeof(MathLibs) / sizeof(MathLibs[0]))

static void CloseLibs(void)
{
    ULONG i;

    for (i = 0; i < NUM_MATHLIBS; i++)
    {
        if (*MathLibs[i].base)
            CloseLibrary(*MathLibs[i].base);
        *MathLibs[i].base = NULL;
    }
}

static BOOL OpenLibs(void)
{
    ULONG i;

    for (i = 0; i < NUM_MATHLIBS; i++)
        if (!(*MathLibs[i].base = OpenLibrary((CONST_STRPTR)MathLibs[i].name,
                                              37)))
            return FALSE;
    return TRUE;
}

static const char LibName[] = LIB_NAME;
static const char LibId[] =
    "$VER: " LIB_NAME " 15.1 (" LIB_DATE ") " LIB_HBVER "\r\n";

#define REG(r, decl) register decl __asm(#r)

static struct HBBase *LibInit(REG(d0, struct HBBase *base),
                              REG(a0, BPTR seglist),
                              REG(a6, struct ExecBase *sysbase));
static struct HBBase *LibOpen(REG(a6, struct HBBase *base));
static BPTR LibClose(REG(a6, struct HBBase *base));
static BPTR LibExpunge(REG(a6, struct HBBase *base));
static ULONG LibNull(void);

/* the wrappers (gen/wrappers.c); no prototypes needed for the table */
#define LIBFUNC(f)  extern char f[];
#include "gen/functable.inc"
#undef LIBFUNC
#define LIBFUNC(f)  (APTR)f,

static const APTR FuncTable[] =
{
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibNull,
#include "gen/functable.inc"
    (APTR)-1
};

struct InitTable
{
    ULONG        it_DataSize;
    const APTR  *it_FuncTable;
    APTR         it_DataInit;
    APTR         it_InitRoutine;
};

static const struct InitTable InitTab =
{
    sizeof(struct HBBase),
    FuncTable,
    NULL,
    (APTR)LibInit
};

const struct Resident RomTag __attribute__((used)) =
{
    RTC_MATCHWORD,
    (struct Resident *)&RomTag,
    (APTR)(&RomTag + 1),
    RTF_AUTOINIT,
    LIB_VERSION,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)LibId + 6,
    (APTR)&InitTab
};

/* ---- library management ----------------------------------------------- */

static struct HBBase *LibInit(REG(d0, struct HBBase *base),
                              REG(a0, BPTR seglist),
                              REG(a6, struct ExecBase *sysbase))
{
    SysBase = sysbase;
    HBBase = base;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)LibName;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (char *)LibId + 6;
    InitSemaphore(&base->lock);

    /* AmigaOS 3.1 (exec V40), 68020 for the compiled code */
    if (sysbase->LibNode.lib_Version < 40 ||
        !(sysbase->AttnFlags & AFF_68020) || !OpenLibs() || !hbmem_Init())
    {
        CloseLibs();
        FreeMem((UBYTE *)base - base->lib.lib_NegSize,
                base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    D(("harfbuzz.library: init %08lx\n", (ULONG)base));
    return base;
}

static struct HBBase *LibOpen(REG(a6, struct HBBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return base;
}

static BPTR LibExpunge(REG(a6, struct HBBase *base))
{
    BPTR seglist;

    if (base->lib.lib_OpenCnt)
    {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }

    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    /* everything programs left behind goes with the pool */
    hbmem_Cleanup();
    CloseLibs();
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static BPTR LibClose(REG(a6, struct HBBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (!base->lib.lib_OpenCnt && (base->lib.lib_Flags & LIBF_DELEXP))
        return LibExpunge(base);
    return 0;
}

static ULONG LibNull(void)
{
    return 0;
}

#ifdef DEBUG
/*
 * kprintf through exec: RawDoFmt() formats, RawPutChar() (LVO -516)
 * writes to the serial port. Not libdebug's, which crashed in this
 * library before printing anything. RawDoFmt() takes WORD-sized %d/%x
 * unless 'l' is given: use %ld/%lx.
 */
static void SerPutChar(REG(d0, UBYTE c), REG(a3, APTR data))
{
    register UBYTE ch __asm("d0") = c;
    register struct ExecBase *a6 __asm("a6") = SysBase;

    (void)data;
    if (!ch)
        return;
    __asm volatile ("jsr -516(%%a6)" : "+d" (ch) : "a" (a6)
                    : "d1", "a0", "a1", "cc", "memory");
}

void kprintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);          /* on the 68k: a pointer to the arguments */
    RawDoFmt((CONST_STRPTR)fmt, (APTR)ap, (void (*)())SerPutChar, NULL);
    va_end(ap);
}
#endif

/* ---- every call ----------------------------------------------------------- */

ULONG hb_CallSwapped(struct StackSwapStruct *sss, ULONG (*func)(APTR),
                     APTR arg);

#ifdef DEBUG
/*
 * Debug builds always switch, to a pattern-filled stack, and report the
 * deepest use seen so far: how much stack HarfBuzz really needs.
 */
#define HB_MEASURE_STACK (64 * 1024)
#define HB_FILL          0xA5A5A5A5UL

static ULONG hb_MaxUsed;

static ULONG MeasuredCall(ULONG (*func)(APTR), APTR arg)
{
    struct StackSwapStruct sss;
    ULONG *p, *end, used, result;

    sss.stk_Lower = AllocMem(HB_MEASURE_STACK, MEMF_ANY);
    if (!sss.stk_Lower)
        return func(arg);
    end = (ULONG *)((UBYTE *)sss.stk_Lower + HB_MEASURE_STACK);
    for (p = sss.stk_Lower; p < end; p++)
        *p = HB_FILL;
    sss.stk_Upper = (ULONG)end;
    sss.stk_Pointer = (APTR)sss.stk_Upper;
    result = hb_CallSwapped(&sss, func, arg);
    for (p = (ULONG *)sss.stk_Lower + 16; p < end && *p == HB_FILL; p++)
        ;
    used = (UBYTE *)end - (UBYTE *)p;
    if (used > hb_MaxUsed)
    {
        hb_MaxUsed = used;
        D(("harfbuzz.library: stack used %ld bytes (new maximum) by \"%s\"\n",
           used, (ULONG)FindTask(NULL)->tc_Node.ln_Name));
    }
    FreeMem(sss.stk_Lower, HB_MEASURE_STACK);
    return result;
}
#endif

static ULONG OnBigStack(ULONG (*func)(APTR), APTR arg)
{
    struct Task *me = FindTask(NULL);
    UBYTE *sp = (UBYTE *)&me;
    struct StackSwapStruct sss;
    ULONG result;

#ifdef DEBUG
    return MeasuredCall(func, arg);
#endif
    /* enough left on the caller's stack? */
    if (sp > (UBYTE *)me->tc_SPLower &&
        sp - (UBYTE *)me->tc_SPLower >= HB_STACK_MIN)
        return func(arg);

    sss.stk_Lower = AllocMem(HB_STACK, MEMF_ANY);
    if (!sss.stk_Lower)
        return func(arg);   /* better than failing outright */
    sss.stk_Upper = (ULONG)sss.stk_Lower + HB_STACK;
    sss.stk_Pointer = (APTR)sss.stk_Upper;
    result = hb_CallSwapped(&sss, func, arg);
    FreeMem(sss.stk_Lower, HB_STACK);
    return result;
}

ULONG hb_Call(ULONG (*func)(APTR), APTR arg)
{
    ULONG result;

    ObtainSemaphore(&HBBase->lock);
    result = OnBigStack(func, arg);
    ReleaseSemaphore(&HBBase->lock);
    return result;
}
