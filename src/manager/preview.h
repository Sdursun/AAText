#ifndef AATEXTMANAGER_PREVIEW_H
#define AATEXTMANAGER_PREVIEW_H

/*
 * Preview of a font that is not installed yet: sample lines in a few
 * sizes, rendered with FreeType (aatext.library) into an RGB buffer in
 * the program's own process. The gadget's render hook only copies that
 * buffer to the window (WritePixelArray), so FreeType never runs on
 * Intuition's small stacks. RTG screens only.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>

#define PV_WIDTH   720          /* the buffer; the box shows what fits */
#define PV_HEIGHT  160

/* Open cybergraphics.library; FALSE if missing (no preview). */
BOOL pv_Init(void);
void pv_Cleanup(void);

/*
 * Render font file path (face) for screen scr in its text and
 * background colours; path NULL clears it. FALSE if the font cannot be
 * read (the box then stays empty).
 */
BOOL pv_Render(struct Screen *scr, const char *path, LONG face);

/* Draw the buffer into box of rp (from the render hook). */
void pv_Draw(struct RastPort *rp, LONG left, LONG top, LONG width,
             LONG height, UBYTE bgpen, UBYTE textpen,
             const char *notrtg);

/* Height of the box that shows every line. */
LONG pv_NeededHeight(void);

#endif
