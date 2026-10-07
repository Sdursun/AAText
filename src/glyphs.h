#ifndef AATEXT_GLYPHS_H
#define AATEXT_GLYPHS_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/tasks.h>
#include <graphics/text.h>

#include "prefs.h"

#define AA_MAX_FONTS    64      /* bitmap font name + size entries */
#define AA_MAX_FACES    32      /* loaded TrueType files */
#define AA_MAX_NAMES    96      /* font names checked for a .otag */

/* A rendered glyph: 8 bit coverage (gamma corrected), pitch == width. */
struct AAGlyph
{
    struct MinNode  lru;        /* must be first: LRU list node */
    struct AAGlyph *next;       /* hash chain */
    struct AAFont  *font;
    UWORD  code;                /* Amiga character code */
    WORD   left;                /* bitmap offset from pen position */
    /*
     * FreeType's bitmap_top: rows above the baseline *line*, so the
     * glyph's last row just above it is row top-1. On the Amiga,
     * tf_Baseline is the pixel row letters stand *on* (their bottom
     * row), so a glyph's first row goes to tf_Baseline + 1 - top.
     */
    WORD   top;
    UWORD  width;
    UWORD  rows;
    WORD   advance;             /* pixels */
    ULONG  allocsize;
    UBYTE  data[];
};

/*
 * A bitmap font (name + size) drawn with a TrueType face: either mapped
 * in the prefs, or detected automatically from the font's .otag file.
 */
struct AAFont
{
    char   name[AA_NAME_LEN];   /* without directory and ".font" */
    UWORD  ysize;
    UWORD  pixelsize;           /* from prefs; 0 = automatic */
    UWORD  pxused;              /* pixel size chosen by aa_PrepareFont() */
    struct AAFace *face;
    APTR   ftsize;              /* FT_Size, created on first use */
    BOOL   prepared;
    BOOL   failed;
    BOOL   real;                /* real TrueType metrics */
    /*
     * Rows to add to tf_Baseline to reach the row the bitmap font's
     * letters actually sit on (measured from its glyph images; some
     * outline engines report a baseline that differs from where they
     * put the glyphs). Set by aa_PrepareFont().
     */
    WORD   yoffset;
    /*
     * Real metrics mode, allocated and filled by aa_PrepareFont() and
     * read-only after that (so measuring needs no lock): advance width
     * and horizontal ink extent [inkl, inkr) relative to the pen, per
     * character code.
     */
    WORD  *adv;
    WORD  *inkl;
    WORD  *inkr;
};

/*
 * Does this font use real metrics? Only proportional fonts do: programs
 * like the Shell (console.device) position fixed-width text by
 * tf_XSize cells, so fixed-width fonts always keep their cells, even
 * with "real on". Decided by the TextFont alone, so the measuring hooks,
 * Text() and font preparation always agree.
 */
static inline BOOL aa_UseReal(const struct AAFont *font,
                              const struct TextFont *tf)
{
    return font->real && (tf->tf_Flags & FPF_PROPORTIONAL);
}

/* Gamma corrected coverage values, filled by aa_GlyphsInit(). */
extern UBYTE aa_GammaLUT[256];

/*
 * Load all TTF files named in the prefs into memory and open them with
 * FreeType. Must be called from a DOS process before the patch is
 * installed. Returns the number of usable font mappings.
 */
LONG aa_GlyphsInit(const struct AAPrefs *prefs, BOOL report);
void aa_GlyphsCleanup(void);

/*
 * Apply the settings that can change while AAText runs: gamma, hinting,
 * charset, cache size, automatic detection. Flushes the glyph cache and
 * sets fonts up again on next use. From AAText's own process only.
 */
void aa_GlyphsReconfigure(const struct AAPrefs *prefs);

/* Counts for the STATUS message. */
void aa_GlyphsStatus(LONG *numfonts, LONG *numfaces, ULONG *bytes,
                     ULONG *glyphs);

/*
 * Mapping for a TextFont, or NULL. Lock-free in the common case. With
 * automatic detection, an unknown font name is queued for the helper
 * process (see aa_SetHelper) and NULL is returned until it is loaded;
 * a detected font gets an entry for each size on first use.
 */
struct AAFont *aa_FindFont(struct TextFont *tf);

/*
 * Automatic detection runs file I/O in AAText's own process: Text() may
 * be called by tasks that cannot use DOS (input.device). aa_FindFont()
 * signals the helper; the helper calls aa_ResolvePending().
 */
void aa_SetHelper(struct Task *task, ULONG sigmask);
LONG aa_ResolvePending(BOOL report);

/* Queue a font name (as in tf_Message.mn_Node.ln_Name) for detection. */
void aa_RequestFont(const char *fontname);

/* All font entries, for preparing them at startup. */
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
