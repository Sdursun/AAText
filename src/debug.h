#ifndef AATEXT_DEBUG_H
#define AATEXT_DEBUG_H

/*
 * Debug output to the serial port via exec/RawPutChar().
 * Safe to call from any task context (not from interrupts while
 * other tasks may be printing - lines may interleave, that's fine).
 *
 * Format string uses exec/RawDoFmt() syntax: always use %ld / %lx / %s,
 * every argument is passed as a 32-bit value.
 */

#ifdef DEBUG
void kprintf(const char *fmt, ...);
#define D(x) kprintf x
#else
#define D(x) ((void)0)
#endif

#endif
