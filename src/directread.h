#ifndef AATEXT_DIRECTREAD_H
#define AATEXT_DIRECTREAD_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* AA_DIRECT_... ("directread") are in prefs.h */
#include "prefs.h"

/* Opens layers.library and timer.device. Never fails. */
void aa_DirectInit(UBYTE mode);
void aa_DirectCleanup(void);

/* New "directread" setting; a changed one measures again. */
void aa_DirectReconfigure(UBYTE mode);

/*
 * Read the background of a string box into buf, as RGBA (4 bytes a
 * pixel, byte 3 undefined). The box is x, y, w, h in rastport
 * coordinates; buf has w pixels a row; the first sx columns and sy rows
 * are not read (outside the layer). Parts hidden by other layers are
 * left as they are, exactly like ReadPixelArray() does.
 */
void aa_ReadBackground(UBYTE *buf, struct RastPort *rp, LONG x, LONG y,
                       LONG w, LONG h, LONG sx, LONG sy);

#ifdef DEBUG
void aa_PrintDirectStats(void);
#endif

#endif
