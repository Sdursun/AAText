#ifndef AATEXT_RENDER_H
#define AATEXT_RENDER_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/rastport.h>

/* Stage 2 test modes */
enum
{
    AA_TEST_OFF = 0,    /* never draw, always pass through */
    AA_TEST_BOX,        /* solid box in FgPen colour (RectFill) */
    AA_TEST_ALPHA,      /* gradient box via cybergraphics BltTemplateAlpha() */
    AA_TEST_RPA,        /* gradient box via Read/WritePixelArray + own blending */
    AA_TEST_WPAA        /* gradient box via cybergraphics WritePixelArrayAlpha() */
};

/* TRUE if cybergraphics.library provides WritePixelArrayAlpha(). */
BOOL aa_HasWritePixelArrayAlpha(void);

extern struct Library *CyberGfxBase;
extern UBYTE aa_TestMode;

/* TRUE if cybergraphics.library provides BltTemplateAlpha(). */
BOOL aa_HasBltTemplateAlpha(void);

/*
 * Try to draw the string ourselves. Returns FALSE if the rastport is not
 * one we handle; the caller must then call the original Text().
 * On success rp->cp_x has been advanced exactly like Text() would.
 */
BOOL aa_RenderText(struct RastPort *rp, CONST_STRPTR string, WORD count);

#ifdef DEBUG
void aa_PrintRenderStats(void);
#endif

#endif
