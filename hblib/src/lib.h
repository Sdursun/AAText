/*
 * harfbuzz.library - shared declarations.
 */
#ifndef HBLIB_LIB_H
#define HBLIB_LIB_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>

#define LIB_NAME     "harfbuzz.library"
#define LIB_VERSION  15         /* 15.1: HarfBuzz 15.1.0 */
#define LIB_REVISION 1
#define LIB_DATE     "09.11.2026"
#define LIB_HBVER    "HarfBuzz 15.1.0"

struct HBBase
{
    struct Library          lib;
    BPTR                    seglist;
    struct SignalSemaphore  lock;   /* around every call: HB_NO_MT */
};

extern struct ExecBase *SysBase;
extern struct HBBase *HBBase;

/*
 * Runs func(arg) with the library's semaphore held (HarfBuzz is built
 * without atomics or mutexes, HB_NO_MT), on a stack of at least
 * HB_STACK bytes: the caller's when enough is left, otherwise a
 * temporary one. The semaphore nests, so a callback (a blob's destroy
 * function, say) may call the library again.
 */
#define HB_STACK       (32 * 1024)
#define HB_STACK_MIN   (12 * 1024)
ULONG hb_Call(ULONG (*func)(APTR), APTR arg);

#ifdef DEBUG
void kprintf(const char *fmt, ...);
#define D(x) kprintf x
#else
#define D(x)
#endif

#endif
