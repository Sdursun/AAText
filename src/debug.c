#include "debug.h"

#ifdef DEBUG

#include <stdarg.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/var.h>

/* stub.s: putchar callback for RawDoFmt, forwards D0 to RawPutChar() */
extern void aa_RawPutChProc(void);

/*
 * Every line also goes into a ring buffer in RAM. After a crash that
 * halts the machine, the serial output's last lines may be lost; the
 * ring can still be read from memory (WinUAE bridge, amiga_peek).
 * Layout: "AATRACE!", ULONG write position, then the text.
 */
#define AA_RING_SIZE 16384

static struct
{
    char  magic[8] __attribute__((nonstring));
    ULONG pos;
    char  text[AA_RING_SIZE];
} aa_Ring = { "AATRACE!", 0, { 0 } };

void kprintf(const char *fmt, ...)
{
    va_list ap;
    char line[256];
    LONG i;

    va_start(ap, fmt);
    /* On m68k va_list is a plain pointer to the 32-bit argument array,
       which is exactly what RawDoFmt expects for %ld style formats. */
    RawDoFmt((CONST_STRPTR)fmt, (APTR)ap, (void (*)())aa_RawPutChProc, NULL);
    /* PutChProc NULL (V45+): store into the buffer */
    RawDoFmt((CONST_STRPTR)fmt, (APTR)ap, NULL, line);
    va_end(ap);

    Disable();
    for (i = 0; line[i] && i < (LONG)sizeof(line) - 1; i++)
    {
        aa_Ring.text[aa_Ring.pos] = line[i];
        aa_Ring.pos = (aa_Ring.pos + 1) % AA_RING_SIZE;
    }
    Enable();
}

/* Also in ENV:AATextTrace, when the serial output is not at hand. */
void aa_TraceInfo(void)
{
    char buf[32];
    ULONG args[1];

    args[0] = (ULONG)&aa_Ring;
    RawDoFmt((CONST_STRPTR)"%lx", args, NULL, buf);
    SetVar((CONST_STRPTR)"AATextTrace", (CONST_STRPTR)buf, -1, GVF_GLOBAL_ONLY);
    kprintf("AAText: trace ring at %lx (%ld bytes)\n", (ULONG)&aa_Ring,
            (LONG)sizeof(aa_Ring));
}

#endif
