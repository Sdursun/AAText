#ifndef AATEXT_METRICS_H
#define AATEXT_METRICS_H

/*
 * Real metrics mode: TextLength(), TextExtent() and TextFit() for fonts
 * mapped with "real", based on the TrueType advance/ink tables of an
 * AAFont. Pure functions (no graphics calls), so they can be tested on
 * the host. Semantics follow graphics.library (AROS rom/graphics used as
 * reference): cp_x advance == TextLength(), algorithmic bold and italic
 * widen the extent but not the advance.
 */

#include <exec/types.h>
#include <graphics/text.h>

struct AAFont;

/* What the measuring functions need from the RastPort. */
struct AAMetricsCtx
{
    const struct AAFont *font;
    WORD  ysize;
    WORD  baseline;
    WORD  boldsmear;
    WORD  txspacing;
    UBYTE algostyle;
};

LONG aa_MLength(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count);

void aa_MExtent(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG count,
                struct TextExtent *te);

ULONG aa_MFit(const struct AAMetricsCtx *m, CONST_STRPTR s, LONG len,
              struct TextExtent *te, const struct TextExtent *cte,
              LONG direction, LONG bitwidth, LONG bitheight);

#endif
