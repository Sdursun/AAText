/*
 * AAText - real metrics mode measuring functions. See metrics.h.
 */

#include <exec/types.h>
#include <graphics/text.h>

#include "metrics.h"
#include "glyphs.h"

LONG aa_MLength(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count)
{
    const struct AAFont *f = m->font;
    LONG w = 0;

    while (count-- > 0)
        w += f->adv[*s++] + m->txspacing;
    return w;
}

void aa_MExtent(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count,
                struct TextExtent *te)
{
    const struct AAFont *f = m->font;
    LONG x = 0, minx = 0, maxx = 0, n = count;

#define CHECK_MINMAX(v) \
    do { if ((v) < minx) minx = (v); if ((v) > maxx) maxx = (v); } while (0)

    while (count-- > 0)
    {
        UBYTE c = *s++;

        /* ink only counts if the glyph has any */
        if (f->inkr[c] > f->inkl[c])
        {
            CHECK_MINMAX(x + f->inkl[c]);
            CHECK_MINMAX(x + f->inkr[c]);
        }
        x += f->adv[c];
        CHECK_MINMAX(x);
        x += m->txspacing;
        CHECK_MINMAX(x);
    }
#undef CHECK_MINMAX

    if (n > 0)
        maxx--;                 /* MaxX is inclusive */

    if (m->algostyle & FSF_BOLD)
        maxx += m->boldsmear;
    if (m->algostyle & FSF_ITALIC)
    {
        maxx += m->baseline / 2;
        minx -= (m->ysize - m->baseline) / 2;
    }

    te->te_Width = x;
    te->te_Height = m->ysize;
    te->te_Extent.MinX = minx;
    te->te_Extent.MaxX = maxx;
    te->te_Extent.MinY = -m->baseline;
    te->te_Extent.MaxY = m->ysize - 1 - m->baseline;
}

ULONG aa_MFit(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG len,
              struct TextExtent *te, const struct TextExtent *cte,
              LONG direction, LONG bitwidth, LONG bitheight)
{
    ULONG fit = 0;

    if (len > 0 && bitheight >= m->ysize)
    {
        BOOL ok = TRUE;

        te->te_Extent.MinX = 0;
        te->te_Extent.MinY = -m->baseline;
        te->te_Extent.MaxX = 0;
        te->te_Extent.MaxY = m->ysize - m->baseline - 1;
        te->te_Width = 0;
        te->te_Height = m->ysize;

        if (cte && (cte->te_Extent.MinY > te->te_Extent.MinY ||
                    cte->te_Extent.MaxY < te->te_Extent.MaxY ||
                    cte->te_Height < te->te_Height))
            ok = FALSE;

        while (ok && len-- > 0)
        {
            struct TextExtent ce;
            LONG newwidth, minx, maxx, newminx, newmaxx;

            aa_MExtent(m, s, 1, &ce);
            s += direction;

            newwidth = te->te_Width + ce.te_Width;
            minx = te->te_Width + ce.te_Extent.MinX;
            maxx = te->te_Width + ce.te_Extent.MaxX;
            newminx = minx < te->te_Extent.MinX ? minx : te->te_Extent.MinX;
            newmaxx = maxx > te->te_Extent.MaxX ? maxx : te->te_Extent.MaxX;

            if (newmaxx - newminx + 1 > bitwidth)
                break;
            if (cte && (cte->te_Extent.MinX > newminx ||
                        cte->te_Extent.MaxX < newmaxx ||
                        cte->te_Width < newwidth))
                break;

            te->te_Width = newwidth;
            te->te_Extent.MinX = newminx;
            te->te_Extent.MaxX = newmaxx;
            fit++;
        }
    }

    if (fit == 0)
    {
        te->te_Width = 0;
        te->te_Height = 0;
        te->te_Extent.MinX = 0;
        te->te_Extent.MinY = 0;
        te->te_Extent.MaxX = 0;
        te->te_Extent.MaxY = 0;
    }
    return fit;
}
