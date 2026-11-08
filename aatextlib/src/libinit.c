/*
 * freetype2.library - library header: RomTag, init, open, close, expunge,
 * and the function table.
 */

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include "lib.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Library *UtilityBase;
struct FT2Base *FT2Base;

static const char LibName[] = LIB_NAME;
static const char LibId[] =
    "$VER: " LIB_NAME " 2.0 (" LIB_DATE ") " LIB_FTVER "\r\n";

/* ---- functions in registers ------------------------------------------- */

#define REG(r, decl) register decl __asm(#r)

static struct FT2Base *LibInit(REG(d0, struct FT2Base *base),
                               REG(a0, BPTR seglist),
                               REG(a6, struct ExecBase *sysbase));
static struct FT2Base *LibOpen(REG(a6, struct FT2Base *base));
static BPTR LibClose(REG(a6, struct FT2Base *base));
static BPTR LibExpunge(REG(a6, struct FT2Base *base));
static ULONG LibNull(void);

/* bullet API (bullet.c) */
struct GlyphEngine;
struct TagItem;
struct GlyphEngine *LIB_OpenEngine(REG(a6, struct FT2Base *base));
void LIB_CloseEngine(REG(a0, struct GlyphEngine *ge));
ULONG LIB_SetInfoA(REG(a0, struct GlyphEngine *ge), REG(a1, struct TagItem *tags));
ULONG LIB_ObtainInfoA(REG(a0, struct GlyphEngine *ge), REG(a1, struct TagItem *tags));
ULONG LIB_ReleaseInfoA(REG(a0, struct GlyphEngine *ge), REG(a1, struct TagItem *tags));

/* reserved slots */
static LONG LibUnimplemented(void)
{
    return 0x07;    /* FT_Err_Unimplemented_Feature */
}

/* the FreeType API is called directly: arguments on the stack */
#include <ft2build.h>
#include "ftapi.h"

#define LIBFUNC(f)  (APTR)f,
#define LIBSKIP()   (APTR)LibUnimplemented,

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
    sizeof(struct FT2Base),
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

static void CloseLibs(void)
{
    if (UtilityBase)
        CloseLibrary(UtilityBase);
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    UtilityBase = NULL;
    DOSBase = NULL;
}

static struct FT2Base *LibInit(REG(d0, struct FT2Base *base),
                               REG(a0, BPTR seglist),
                               REG(a6, struct ExecBase *sysbase))
{
    SysBase = sysbase;
    FT2Base = base;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)LibName;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (char *)LibId + 6;

    /* exec V39 for memory pools, 68020 for the compiled code */
    if (sysbase->LibNode.lib_Version < 39 ||
        !(sysbase->AttnFlags & AFF_68020))
        goto fail;

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
    UtilityBase = OpenLibrary("utility.library", 39);
    if (!DOSBase || !UtilityBase)
        goto fail;

    D(("freetype2.library: init %08lx\n", (ULONG)base));
    return base;

fail:
    CloseLibs();
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return NULL;
}

static struct FT2Base *LibOpen(REG(a6, struct FT2Base *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return base;
}

static BPTR LibExpunge(REG(a6, struct FT2Base *base))
{
    BPTR seglist;

    if (base->lib.lib_OpenCnt)
    {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }

    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    CloseLibs();
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static BPTR LibClose(REG(a6, struct FT2Base *base))
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

/* ---- big stack ---------------------------------------------------------- */

ULONG ft2_CallSwapped(struct StackSwapStruct *sss, ULONG (*func)(APTR), APTR arg);

#ifdef DEBUG
/*
 * Debug builds always switch, to a pattern-filled stack, and report the
 * deepest use seen so far: how much stack FreeType really needs.
 */
#define FT2_MEASURE_STACK (64 * 1024)
#define FT2_FILL          0xA5A5A5A5UL

static ULONG ft2_MaxUsed;

static ULONG MeasuredCall(ULONG (*func)(APTR), APTR arg)
{
    struct StackSwapStruct sss;
    ULONG *p, *end, used, result;

    sss.stk_Lower = AllocMem(FT2_MEASURE_STACK, MEMF_ANY);
    if (!sss.stk_Lower)
        return func(arg);
    end = (ULONG *)((UBYTE *)sss.stk_Lower + FT2_MEASURE_STACK);
    for (p = sss.stk_Lower; p < end; p++)
        *p = FT2_FILL;
    sss.stk_Upper = (ULONG)end;
    sss.stk_Pointer = (APTR)sss.stk_Upper;
    result = ft2_CallSwapped(&sss, func, arg);
    /* the first longs may be written by exec, not by FreeType */
    for (p = (ULONG *)sss.stk_Lower + 16; p < end && *p == FT2_FILL; p++)
        ;
    used = (UBYTE *)end - (UBYTE *)p;
    if (used > ft2_MaxUsed)
    {
        ULONG *low = sss.stk_Lower;

        ft2_MaxUsed = used;
        D(("freetype2.library: stack used %ld bytes (new maximum) by \"%s\", "
           "lowest longs %08lx %08lx %08lx %08lx\n",
           used, (ULONG)FindTask(NULL)->tc_Node.ln_Name,
           low[0], low[1], low[2], low[3]));
    }
    FreeMem(sss.stk_Lower, FT2_MEASURE_STACK);
    return result;
}
#endif

ULONG ft2_BigStack(ULONG (*func)(APTR), APTR arg)
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
        sp - (UBYTE *)me->tc_SPLower >= FT2_STACK_MIN)
        return func(arg);

    sss.stk_Lower = AllocMem(FT2_STACK, MEMF_ANY);
    if (!sss.stk_Lower)
        return func(arg);   /* better than failing outright */
    sss.stk_Upper = (ULONG)sss.stk_Lower + FT2_STACK;
    sss.stk_Pointer = (APTR)sss.stk_Upper;
    result = ft2_CallSwapped(&sss, func, arg);
    FreeMem(sss.stk_Lower, FT2_STACK);
    return result;
}
