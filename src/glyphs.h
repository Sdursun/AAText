#ifndef AATEXT_GLYPHS_H
#define AATEXT_GLYPHS_H

#include <exec/types.h>
#include <exec/lists.h>
#include <graphics/text.h>

#include "prefs.h"

/* A rendered glyph: 8 bit coverage (gamma corrected), pitch == width. */
struct AAGlyph
{
    struct MinNode  lru;        /* must be first: LRU list node */
    struct AAGlyph *next;       /* hash chain */
    struct AAFont  *font;
    UWORD  code;                /* Amiga character code */
    WORD   left;                /* bitmap offset from pen position */
    WORD   top;                 /* rows above the baseline */
    UWORD  width;
    UWORD  rows;
    WORD   advance;             /* pixels */
    ULONG  allocsize;
    UBYTE  data[];
};

/* A bitmap font (name + size) mapped to a TrueType face. */
struct AAFont
{
    char   name[AA_NAME_LEN];
    UWORD  ysize;
    UWORD  pixelsize;           /* from prefs; 0 = automatic */
    struct AAFace *face;
    APTR   ftsize;              /* FT_Size, created on first use */
    BOOL   prepared;
    BOOL   failed;
    BOOL   real;                /* real TrueType metrics */
    /*
     * Real metrics mode, filled by aa_PrepareFont() and read-only after
     * that (so measuring needs no lock): advance width and horizontal
     * ink extent [inkl, inkr) relative to the pen, per character code.
     */
    WORD   adv[256];
    WORD   inkl[256];
    WORD   inkr[256];
};

/* Gamma corrected coverage values, filled by aa_GlyphsInit(). */
extern UBYTE aa_GammaLUT[256];

/*
 * Load all TTF files named in the prefs into memory and open them with
 * FreeType. Must be called from a DOS process before the patch is
 * installed. Returns the number of usable font mappings.
 */
LONG aa_GlyphsInit(const struct AAPrefs *prefs, BOOL report);
void aa_GlyphsCleanup(void);

/* Mapping for a TextFont, or NULL. Lock-free (table is read-only). */
struct AAFont *aa_FindFont(struct TextFont *tf);

/* All usable mappings, for preparing them at startup. */
LONG aa_FontCount(void);
struct AAFont *aa_FontAt(LONG i);

/*
 * Glyph access. All of the following must be called between
 * aa_LockGlyphs() and aa_UnlockGlyphs(), and nothing that may wait for
 * a layer or Intuition lock may be called while the lock is held.
 * Glyph pointers stay valid only until aa_UnlockGlyphs().
 */
void aa_LockGlyphs(void);
void aa_UnlockGlyphs(void);
BOOL aa_PrepareFont(struct AAFont *font, struct TextFont *tf);
struct AAGlyph *aa_GetGlyph(struct AAFont *font, UBYTE code);

/* Current cache size, for tests and statistics (lock held). */
void aa_GetCacheStats(ULONG *bytes, ULONG *count);

#ifdef DEBUG
void aa_PrintGlyphStats(void);
#endif

#endif
