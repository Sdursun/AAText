/*
 * gposkern.h - pair kerning from the GPOS table: the 'kern' feature as
 * HarfBuzz applies it to two neighbouring letters of Latin text.
 *
 * Shared by AAText (src/glyphs.c, kerning rows for a whole font) and
 * freetype2.library (aatextlib/src/bullet.c, one pair at a time for
 * OT_TextKernPair). Header only: every function is static inline, so
 * each user compiles what it calls.
 *
 * The lookups of the 'kern' features of script 'latn' (else 'DFLT';
 * else of all 'kern' features), in lookup list order, their values added
 * up. Within a lookup the first PairPos subtable that applies to a pair
 * wins: format 1 when the pair is listed, format 2 when the left glyph is
 * covered. Only the left glyph's XAdvance is used. Extension lookups
 * (type 9) are followed. Every offset is checked against the table.
 * Checked against HarfBuzz (uharfbuzz 0.57) with 9 Google fonts: all
 * pairs equal except with the soft hyphen (0xAD), which HarfBuzz treats
 * as invisible but Amiga fonts draw.
 */

#ifndef GPOSKERN_H
#define GPOSKERN_H

#include <exec/types.h>
#include <string.h>

#define GP_MAX_LOOKUPS  64
#define GP_MAX_SUBS     128

#define GP16(p) ((UWORD)(((p)[0] << 8) | (p)[1]))

struct GPSubtable
{
    ULONG off;                  /* PairPos subtable in the GPOS table */
    UWORD lookup;               /* position in the lookup order */
    UWORD format;               /* 1 listed pairs, 2 class pairs */
};

static inline BOOL gp_In(ULONG len, ULONG off, ULONG n)
{
    return off <= len && n <= len - off;
}

/* Coverage index of glyph g, -1 if not covered. */
static inline LONG gp_Coverage(const UBYTE *t, ULONG len, ULONG off, UWORD g)
{
    LONG n, a, b;

    if (!gp_In(len, off, 4))
        return -1;
    n = GP16(t + off + 2);
    a = 0;
    b = n - 1;
    if (GP16(t + off) == 1)
    {
        const UBYTE *gl = t + off + 4;

        if (!gp_In(len, off + 4, n * 2))
            return -1;
        while (a <= b)
        {
            LONG m = (a + b) >> 1;
            UWORD v = GP16(gl + m * 2);

            if (v == g)
                return m;
            if (v < g)
                a = m + 1;
            else
                b = m - 1;
        }
    }
    else if (GP16(t + off) == 2)
    {
        const UBYTE *rg = t + off + 4;

        if (!gp_In(len, off + 4, n * 6))
            return -1;
        while (a <= b)
        {
            LONG m = (a + b) >> 1;
            const UBYTE *e = rg + m * 6;

            if (g < GP16(e))
                b = m - 1;
            else if (g > GP16(e + 2))
                a = m + 1;
            else
                return GP16(e + 4) + g - GP16(e);
        }
    }
    return -1;
}

/* Class of glyph g in the ClassDef table at off (0 if not listed). */
static inline UWORD gp_Class(const UBYTE *t, ULONG len, ULONG off, UWORD g)
{
    LONG n, a, b;

    if (!gp_In(len, off, 6))
        return 0;
    if (GP16(t + off) == 1)
    {
        UWORD start = GP16(t + off + 2);

        n = GP16(t + off + 4);
        if (g < start || g - start >= n ||
            !gp_In(len, off + 6, (g - start + 1) * 2))
            return 0;
        return GP16(t + off + 6 + (g - start) * 2);
    }
    if (GP16(t + off) != 2)
        return 0;
    n = GP16(t + off + 2);
    if (!gp_In(len, off + 4, n * 6))
        return 0;
    a = 0;
    b = n - 1;
    while (a <= b)
    {
        LONG m = (a + b) >> 1;
        const UBYTE *e = t + off + 4 + m * 6;

        if (g < GP16(e))
            b = m - 1;
        else if (g > GP16(e + 2))
            a = m + 1;
        else
            return GP16(e + 4);
    }
    return 0;
}

static inline LONG gp_ValueSize(UWORD fmt)
{
    LONG n = 0;

    for (fmt &= 0xFF; fmt; fmt &= fmt - 1)
        n++;
    return n * 2;
}

/* XAdvance of the value record at off, 0 if the format has none. */
static inline LONG gp_XAdvance(const UBYTE *t, ULONG len, ULONG off,
                               UWORD fmt)
{
    if (!(fmt & 4))
        return 0;
    off += gp_ValueSize(fmt & 3);
    return gp_In(len, off, 2) ? (WORD)GP16(t + off) : 0;
}

/* The lookups of the 'kern' feature into lk[GP_MAX_LOOKUPS], sorted;
   returns how many (0: no GPOS 'kern' feature). */
static inline LONG gp_KernLookups(const UBYTE *t, ULONG len, UWORD *lk)
{
    ULONG sl, fl, so = 0, ls = 0;
    LONG ns, nf, i, j, n = 0, nidx = -1;

    if (!gp_In(len, 0, 10) || GP16(t) != 1)
        return 0;
    sl = GP16(t + 4);
    fl = GP16(t + 6);
    if (!gp_In(len, sl, 2) || !gp_In(len, fl, 2))
        return 0;

    /* script 'latn', else 'DFLT': its default language system */
    ns = GP16(t + sl);
    if (gp_In(len, sl + 2, ns * 6))
    {
        ULONG latn = 0, dflt = 0;

        for (i = 0; i < ns; i++)
        {
            const UBYTE *e = t + sl + 2 + i * 6;

            if (!memcmp(e, "latn", 4))
                latn = sl + GP16(e + 4);
            else if (!memcmp(e, "DFLT", 4))
                dflt = sl + GP16(e + 4);
        }
        so = latn ? latn : dflt;
    }
    if (so && gp_In(len, so, 2) && GP16(t + so))
    {
        ls = so + GP16(t + so);
        if (gp_In(len, ls, 6) && gp_In(len, ls + 6, GP16(t + ls + 4) * 2))
            nidx = GP16(t + ls + 4);
    }

    nf = GP16(t + fl);
    if (!gp_In(len, fl + 2, nf * 6))
        return 0;
    for (i = 0; i < nf; i++)
    {
        const UBYTE *e = t + fl + 2 + i * 6;
        ULONG fo = fl + GP16(e + 4);
        LONG k, nl;
        BOOL used = nidx < 0;

        if (memcmp(e, "kern", 4))
            continue;
        for (k = 0; k < nidx && !used; k++)
            used = GP16(t + ls + 6 + k * 2) == i;
        if (!used || !gp_In(len, fo, 4))
            continue;
        nl = GP16(t + fo + 2);
        if (!gp_In(len, fo + 4, nl * 2))
            continue;
        for (k = 0; k < nl && n < GP_MAX_LOOKUPS; k++)
        {
            UWORD l = GP16(t + fo + 4 + k * 2);

            for (j = 0; j < n && lk[j] < l; j++)
                ;
            if (j < n && lk[j] == l)
                continue;               /* already there */
            memmove(lk + j + 1, lk + j, (n - j) * sizeof(*lk));
            lk[j] = l;
            n++;
        }
    }
    return n;
}

/* The PairPos subtables of the nl lookups lk into sub[GP_MAX_SUBS], in
   order (Extension lookups resolved); returns how many. */
static inline LONG gp_Subtables(const UBYTE *t, ULONG len, const UWORD *lk,
                                LONG nl, struct GPSubtable *sub)
{
    ULONG ll;
    LONG ns = 0, i, j;

    if (!gp_In(len, 0, 10))
        return 0;
    ll = GP16(t + 8);
    if (!gp_In(len, ll, 2))
        return 0;
    for (i = 0; i < nl; i++)
    {
        ULONG lo;
        UWORD type;
        LONG nsub;

        if (lk[i] >= GP16(t + ll) || !gp_In(len, ll + 2 + lk[i] * 2, 2))
            continue;
        lo = ll + GP16(t + ll + 2 + lk[i] * 2);
        if (!gp_In(len, lo, 6))
            continue;
        type = GP16(t + lo);
        nsub = GP16(t + lo + 4);
        if (!gp_In(len, lo + 6, nsub * 2))
            continue;
        for (j = 0; j < nsub && ns < GP_MAX_SUBS; j++)
        {
            ULONG st = lo + GP16(t + lo + 6 + j * 2);
            UWORD ty = type;

            if (ty == 9)                /* Extension */
            {
                if (!gp_In(len, st, 8))
                    continue;
                ty = GP16(t + st + 2);
                st += ((ULONG)GP16(t + st + 4) << 16) | GP16(t + st + 6);
            }
            if (ty != 2 || !gp_In(len, st, 16) ||
                (GP16(t + st) != 1 && GP16(t + st) != 2))
                continue;
            sub[ns].off = st;
            sub[ns].lookup = i;
            sub[ns].format = GP16(t + st);
            ns++;
        }
    }
    return ns;
}

/* Kerning of the glyph pair (l, r) in font units: the XAdvance of l. */
static inline LONG gp_PairKern(const UBYTE *t, ULONG len,
                               const struct GPSubtable *sub, LONG ns,
                               UWORD l, UWORD r)
{
    LONG s, sum = 0, done = -1;     /* lookup that has applied */

    for (s = 0; s < ns; s++)
    {
        ULONG st = sub[s].off;
        UWORD vf1, vf2;
        LONG vsz, ci;

        if (sub[s].lookup == done)
            continue;
        vf1 = GP16(t + st + 4);
        vf2 = GP16(t + st + 6);
        vsz = gp_ValueSize(vf1) + gp_ValueSize(vf2);
        ci = gp_Coverage(t, len, st + GP16(t + st + 2), l);
        if (ci < 0)
            continue;
        if (sub[s].format == 1)         /* listed pairs, by second glyph */
        {
            ULONG ps;
            LONG rec = 2 + vsz, a, b;

            if (ci >= GP16(t + st + 8) || !gp_In(len, st + 10 + ci * 2, 2))
                continue;
            ps = st + GP16(t + st + 10 + ci * 2);
            if (!gp_In(len, ps, 2) || !gp_In(len, ps + 2, GP16(t + ps) * rec))
                continue;
            a = 0;
            b = GP16(t + ps) - 1;
            while (a <= b)
            {
                LONG m = (a + b) >> 1;
                UWORD g = GP16(t + ps + 2 + m * rec);

                if (g == r)
                {
                    sum += gp_XAdvance(t, len, ps + 2 + m * rec + 2, vf1);
                    done = sub[s].lookup;
                    break;
                }
                if (g < r)
                    a = m + 1;
                else
                    b = m - 1;
            }
        }
        else                            /* class pairs */
        {
            UWORD n1 = GP16(t + st + 12), n2 = GP16(t + st + 14);
            UWORD k1 = gp_Class(t, len, st + GP16(t + st + 8), l);
            UWORD k2 = gp_Class(t, len, st + GP16(t + st + 10), r);

            if (k1 >= n1 || k2 >= n2)
                continue;
            sum += gp_XAdvance(t, len,
                               st + 16 + ((ULONG)k1 * n2 + k2) * vsz, vf1);
            done = sub[s].lookup;
        }
    }
    return sum;
}

#endif /* GPOSKERN_H */
