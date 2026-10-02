#include "debug.h"

#ifdef DEBUG

#include <stdarg.h>
#include <exec/types.h>
#include <proto/exec.h>

/* stub.s: putchar callback for RawDoFmt, forwards D0 to RawPutChar() */
extern void aa_RawPutChProc(void);

void kprintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    /* On m68k va_list is a plain pointer to the 32-bit argument array,
       which is exactly what RawDoFmt expects for %ld style formats. */
    RawDoFmt((CONST_STRPTR)fmt, (APTR)ap, (void (*)())aa_RawPutChProc, NULL);
    va_end(ap);
}

#endif
