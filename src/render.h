#ifndef AATEXT_RENDER_H
#define AATEXT_RENDER_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/rastport.h>
#include <exec/tasks.h>

struct AAPrefs;

enum
{
    AA_MODE_OFF = 0,    /* never draw, always pass through */
    AA_MODE_TEXT,       /* antialiased TrueType text (default) */
    AA_MODE_BOX,        /* debug: solid box in FgPen colour */
    AA_MODE_RPA         /* debug: gradient box via Read/WritePixelArray */
};

extern struct Library *CyberGfxBase;
extern UBYTE aa_Mode;

BOOL aa_RenderInit(const struct AAPrefs *prefs);
void aa_RenderCleanup(void);

/*
 * Try to draw the string ourselves. Returns FALSE if the rastport is not
 * one we handle; the caller must then call the original Text().
 * On success rp->cp_x has been advanced exactly like Text() would.
 */
BOOL aa_RenderText(struct RastPort *rp, CONST_STRPTR string, WORD count,
                   struct Task *me);

#ifdef DEBUG
void aa_PrintRenderStats(void);
#endif

#endif
