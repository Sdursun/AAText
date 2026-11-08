/*
 * freetype2.library - shared declarations.
 */
#ifndef AATEXTLIB_LIB_H
#define AATEXTLIB_LIB_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>

#define LIB_NAME     "freetype2.library"
#define LIB_VERSION  2
#define LIB_REVISION 0
#define LIB_DATE     "08.11.2026"
#define LIB_FTVER    "FreeType 2.15.0"

struct FT2Base
{
    struct Library  lib;
    BPTR            seglist;
};

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct Library *UtilityBase;
extern struct FT2Base *FT2Base;

/* Runs func(arg) on a stack of at least FT2_STACK bytes: on the caller's
   stack when there is enough left, otherwise on a temporary one. FreeType's
   autohinter and CFF/Type 1 code need more stack than many tasks have.
   Measured (debug build): up to 18.8 KB opening TTF and OTF fonts; 12 KB
   was not enough and overwrote the Process below a 16 KB stack. */
#define FT2_STACK       (48 * 1024)
#define FT2_STACK_MIN   (32 * 1024)
ULONG ft2_BigStack(ULONG (*func)(APTR), APTR arg);

#ifdef DEBUG
void kprintf(const char *fmt, ...);
#define D(x) kprintf x
#else
#define D(x)
#endif

#endif
