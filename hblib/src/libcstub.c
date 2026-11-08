/*
 * libcstub.c - the parts of the C library HarfBuzz needs that libnix
 * cannot give a library.
 *
 * libnix's stdio (and its snprintf, which writes through putc) needs a
 * program's startup code: __initstdio calls exit(), which a library does
 * not have. So:
 *   - snprintf/vsnprintf here, for the formats HarfBuzz uses: %d %i %u
 *     %x %X %c %s %% with flags '-' '0', a width, the length modifiers l
 *     and h (all integers are 32 bits here), and %g for variation values;
 *   - HarfBuzz's debug output to stderr (vfprintf, fputs, fputc) writes
 *     nothing; with HB_DEBUG off it is never called, but it is linked.
 *
 * <stdio.h> is not included: __sF is only an address here.
 */

#include <stdarg.h>
#include <stddef.h>

long __sF[64];              /* stdin, stdout, stderr: never used */

int vfprintf(void *stream, const char *format, va_list args)
{
    (void)stream;
    (void)format;
    (void)args;
    return 0;
}

int fputs(const char *s, void *stream)
{
    (void)s;
    (void)stream;
    return 0;
}

int fputc(int c, void *stream)
{
    (void)stream;
    return c & 0xFF;
}

/* ---- snprintf ------------------------------------------------------------ */

struct Out
{
    char  *buf;
    size_t size;
    size_t len;             /* characters produced, written or not */
};

static void Put(struct Out *o, char c)
{
    if (o->len + 1 < o->size)
        o->buf[o->len] = c;
    o->len++;
}

/* digits of v in base, padded to width */
static void PutNumber(struct Out *o, unsigned long v, int neg, unsigned base,
                      int upper, int width, int left, int zero)
{
    char tmp[16];
    int n = 0, pad;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do
    {
        tmp[n++] = digits[v % base];
        v /= base;
    }
    while (v);
    pad = width - n - neg;
    if (!left && !zero)
        for (; pad > 0; pad--)
            Put(o, ' ');
    if (neg)
        Put(o, '-');
    if (!left && zero)
        for (; pad > 0; pad--)
            Put(o, '0');
    while (n)
        Put(o, tmp[--n]);
    if (left)
        for (; pad > 0; pad--)
            Put(o, ' ');
}

/*
 * %g with the default precision of 6 significant digits, without an
 * exponent for the values fonts use (axis values, about -32768..32768);
 * other values are written the same way, which is still readable.
 */
static void PutDouble(struct Out *o, double v)
{
    unsigned long ip, frac;
    int digits, i, decimals;
    double f;

    if (v != v)
    {
        Put(o, 'n'); Put(o, 'a'); Put(o, 'n');
        return;
    }
    if (v < 0)
    {
        Put(o, '-');
        v = -v;
    }
    if (v >= 4294967295.0)
    {
        Put(o, 'i'); Put(o, 'n'); Put(o, 'f');
        return;
    }
    ip = (unsigned long)v;
    for (digits = 1, frac = ip; frac >= 10; frac /= 10)
        digits++;
    decimals = ip ? 6 - digits : 6;
    if (decimals < 0)
        decimals = 0;
    f = v - (double)ip;
    for (i = 0, frac = 0; i < decimals; i++)
        f *= 10.0;
    frac = (unsigned long)(f + 0.5);
    for (i = 0, f = 1; i < decimals; i++)
        f *= 10.0;
    if ((double)frac >= f)          /* rounded up into the integer part */
    {
        ip++;
        frac = 0;
    }
    PutNumber(o, ip, 0, 10, 0, 0, 0, 0);
    /* drop trailing zeros, as %g does */
    while (decimals > 0 && frac % 10 == 0)
    {
        frac /= 10;
        decimals--;
    }
    if (decimals > 0)
    {
        Put(o, '.');
        PutNumber(o, frac, 0, 10, 0, decimals, 0, 1);
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct Out o;

    o.buf = buf;
    o.size = size;
    o.len = 0;
    for (; *fmt; fmt++)
    {
        int left = 0, zero = 0, width = 0;
        long v;

        if (*fmt != '%')
        {
            Put(&o, *fmt);
            continue;
        }
        fmt++;
        for (;; fmt++)
        {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + *fmt++ - '0';
        while (*fmt == 'l' || *fmt == 'h')
            fmt++;
        switch (*fmt)
        {
            case 'd':
            case 'i':
                v = va_arg(ap, long);
                PutNumber(&o, v < 0 ? -(unsigned long)v : (unsigned long)v,
                          v < 0, 10, 0, width, left, zero);
                break;
            case 'u':
                PutNumber(&o, va_arg(ap, unsigned long), 0, 10, 0, width,
                          left, zero);
                break;
            case 'x':
            case 'X':
                PutNumber(&o, va_arg(ap, unsigned long), 0, 16, *fmt == 'X',
                          width, left, zero);
                break;
            case 'c':
                Put(&o, (char)va_arg(ap, int));
                break;
            case 's':
            {
                const char *s = va_arg(ap, const char *);
                int n = 0;

                if (!s)
                    s = "(null)";
                while (s[n])
                    n++;
                if (!left)
                    for (; width > n; width--)
                        Put(&o, ' ');
                while (*s)
                    Put(&o, *s++);
                if (left)
                    for (; width > n; width--)
                        Put(&o, ' ');
                break;
            }
            case 'g':
            case 'f':
                PutDouble(&o, va_arg(ap, double));
                break;
            case '%':
                Put(&o, '%');
                break;
            case 0:
                fmt--;
                break;
            default:
                Put(&o, '%');
                Put(&o, *fmt);
                break;
        }
    }
    if (size)
        buf[o.len < size ? o.len : size - 1] = 0;
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
