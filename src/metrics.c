/*
 * AAText - real metrics mode measuring functions. See metrics.h.
 *
 * This file is derived from AROS rom/graphics/textextent.c and
 * rom/graphics/textfit.c (TextExtent() and TextFit()):
 *
 *   Copyright (C) 1995-2026, The AROS Development Team.
 *   All rights reserved.
 *
 * Unlike the rest of AAText (MIT License), this file is distributed
 * under the AROS Public License:
 *
 *   The contents of this file are subject to the AROS Public License
 *   Version 1.1 (the "License"); you may not use this file except in
 *   compliance with the License. You may obtain a copy of the License at
 *   http://www.aros.org/license.html (also in the file LICENSE.APL).
 *
 *   Software distributed under the License is distributed on an "AS IS"
 *   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 *   the License for the specific language governing rights and
 *   limitations under the License.
 *
 *   The Original Code is AROS rom/graphics/textextent.c and textfit.c.
 *   The Initial Developer of the Original Code is The AROS Development
 *   Team. Portions created by The AROS Development Team are Copyright
 *   (C) 1995-2026 The AROS Development Team. All Rights Reserved.
 *
 *   Contributor(s): Serkan Dursun.
 *
 * Changes made to the Original Code (APL section 3.3):
 *
 *   2026-10-03  Serkan Dursun
 *     - Character widths and ink extents come from the TrueType advance
 *       tables of an AAFont instead of tf_CharSpace/tf_CharKern/
 *       tf_CharLoc; the RastPort is replaced by struct AAMetricsCtx.
 *     - Turned into plain C functions without library calls:
 *       aa_MLength(), aa_MExtent(), aa_MFit(); TextFit() measures each
 *       character with aa_MExtent() instead of calling TextExtent().
 *     - Ink extent only counted for characters with ink.
 *
 *   2026-10-07  Serkan Dursun
 *     - Pair kerning (aa_KernPair()) between neighbouring characters in
 *       all three functions, so they agree with what Text() draws.
 *
 *   2026-11-14  Serkan Dursun
 *     - Widths, ink extents and kerning may be in thirds of a pixel
 *       (AAFont.sub); all three functions sum in those units and round
 *       to pixels once (ExtentSub()); TextFit() keeps its running width
 *       in those units too.
 */

#include <exec/types.h>
#include <graphics/text.h>

#include "metrics.h"
#include "glyphs.h"

/* Units per pixel of the font's tables (see AAFont.sub). */
static inline LONG Sub(const struct AAFont *f)
{
    return f->sub ? f->sub : 1;
}

/*
 * Pen advance and horizontal extent of a string in units of 1/Sub()
 * pixel: *minx, *maxx bound the ink and the pen positions (*maxx
 * exclusive, as the ink tables are). Only the callers round to pixels,
 * once, so a string's width is the rounded sum of its letters, as Text()
 * draws it, not the sum of rounded letters.
 */
static LONG ExtentSub(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count,
                      LONG *minxp, LONG *maxxp)
{
    const struct AAFont *f = m->font;
    LONG x = 0, minx = 0, maxx = 0, n = count, sp = m->txspacing * Sub(f);
    UBYTE prev = 0;

#define CHECK_MINMAX(v) \
    do { if ((v) < minx) minx = (v); if ((v) > maxx) maxx = (v); } while (0)

    while (count-- > 0)
    {
        UBYTE c = *s++;

        if (count < n - 1)          /* not the first character */
            x += aa_KernPair(f, prev, c);
        prev = c;

        /* ink only counts if the glyph has any */
        if (f->inkr[c] > f->inkl[c])
        {
            CHECK_MINMAX(x + f->inkl[c]);
            CHECK_MINMAX(x + f->inkr[c]);
        }
        x += f->adv[c];
        CHECK_MINMAX(x);
        x += sp;
        CHECK_MINMAX(x);
    }
#undef CHECK_MINMAX
    *minxp = minx;
    *maxxp = maxx;
    return x;
}

LONG aa_MLength(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count)
{
    const struct AAFont *f = m->font;
    LONG S = Sub(f), w = 0, i, sp = m->txspacing * S;

    /* as ExtentSub() sums the pen, without the ink extent */
    for (i = 0; i < count; i++)
    {
        if (i > 0)
            w += aa_KernPair(f, s[i - 1], s[i]);
        w += f->adv[s[i]] + sp;
    }
    return aa_SubRound(w, S);
}

void aa_MExtent(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count,
                struct TextExtent *te)
{
    LONG S = Sub(m->font), minx, maxx, x;

    x = ExtentSub(m, s, count, &minx, &maxx);
    minx = aa_SubFloor(minx, S);
    maxx = aa_SubCeil(maxx, S);

    if (count > 0)
        maxx--;                 /* MaxX is inclusive */

    if (m->algostyle & FSF_BOLD)
        maxx += m->boldsmear;
    if (m->algostyle & FSF_ITALIC)
    {
        maxx += m->baseline / 2;
        minx -= (m->ysize - m->baseline) / 2;
    }

    te->te_Width = aa_SubRound(x, S);
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
    UBYTE prev = 0;
    LONG S = Sub(m->font), wsub = 0;    /* width so far, 1/S pixel */

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
            LONG newwidth, minx, maxx, newminx, newmaxx, pos, cw, cmin, cmax;
            UBYTE c = *s;

            /* the character alone, in 1/S pixel (cmax exclusive) */
            cw = ExtentSub(m, s, 1, &cmin, &cmax);
            s += direction;

            /*
             * Kerning with the character measured before: it stands to
             * the left of c going forward, to the right going backward.
             */
            pos = wsub;
            if (fit > 0)
                pos += direction > 0 ? aa_KernPair(m->font, prev, c)
                                     : aa_KernPair(m->font, c, prev);
            prev = c;

            /* to pixels, with the styles as aa_MExtent() adds them */
            newwidth = aa_SubRound(pos + cw, S);
            minx = aa_SubFloor(pos + cmin, S);
            maxx = aa_SubCeil(pos + cmax, S) - 1;
            if (m->algostyle & FSF_BOLD)
                maxx += m->boldsmear;
            if (m->algostyle & FSF_ITALIC)
            {
                maxx += m->baseline / 2;
                minx -= (m->ysize - m->baseline) / 2;
            }
            newminx = minx < te->te_Extent.MinX ? minx : te->te_Extent.MinX;
            newmaxx = maxx > te->te_Extent.MaxX ? maxx : te->te_Extent.MaxX;

            if (newmaxx - newminx + 1 > bitwidth)
                break;
            if (cte && (cte->te_Extent.MinX > newminx ||
                        cte->te_Extent.MaxX < newmaxx ||
                        cte->te_Width < newwidth))
                break;

            wsub = pos + cw;
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
