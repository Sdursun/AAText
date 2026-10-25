/*
 * sysinfotest - what DOpus 5's clock task does with SysInfo.library
 * (for its CPU usage display): OpenLibrary, InitSysInfo(), wait,
 * FreeSysInfo(), CloseLibrary(). Used to show that the exec patches of
 * sysmon.library stay behind without DOpus.
 *
 *   sysinfotest [seconds]
 */

#include <stdio.h>
#include <stdlib.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>

static APTR InitSysInfo(struct Library *base)
{
    register APTR result __asm("d0");
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr -30(a6)" : "=r" (result) : "r" (a6)
                    : "d1", "a0", "a1", "cc", "memory");
    return result;
}

static void FreeSysInfo(struct Library *base, APTR si)
{
    register struct Library *a6 __asm("a6") = base;
    register APTR a0 __asm("a0") = si;

    __asm volatile ("jsr -36(a6)" : "+r" (a0) : "r" (a6)
                    : "d0", "d1", "a1", "cc", "memory");
}

int main(int argc, char **argv)
{
    struct Library *base = OpenLibrary((CONST_STRPTR)"SysInfo.library", 2);
    APTR si;

    if (!base)
    {
        printf("cannot open SysInfo.library 2\n");
        return 10;
    }
    si = InitSysInfo(base);
    printf("InitSysInfo: %08lx\n", (unsigned long)si);
    Delay(50 * (argc > 1 ? atoi(argv[1]) : 2));
    if (si)
        FreeSysInfo(base, si);
    CloseLibrary(base);
    printf("freed and closed\n");
    return 0;
}
