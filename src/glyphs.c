/*
 * AAText - TrueType faces, glyph rendering and glyph cache.
 *
 * FreeType is only ever called on AAText's own stack (StackSwap), since
 * Text() callers such as input.device have only ~3 KB of stack left, and
 * only while aa_GlyphSem is held. That one semaphore also protects the
 * glyph cache and the shared render stack. While it is held, no
 * graphics/layers/intuition function is called, so it can never take
 * part in a deadlock with layer locks held by our callers.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_ADVANCES_H
#include FT_SIZES_H

/* src/ft/aa_ftsystem.c (internal FreeType API, not in public headers) */
FT_Memory FT_New_Memory(void);
void FT_Done_Memory(FT_Memory memory);

#include "glyphs.h"
#include "debug.h"

#define AA_STACK_SIZE   (32 * 1024)
#define AA_HASH_SIZE    256
#define AA_MAX_FONTFILE (8 * 1024 * 1024)

struct AAFace
{
    char    path[AA_PATH_LEN];
    APTR    data;
    ULONG   size;
    FT_Face face;
};

/* stub.s */
extern LONG aa_CallOnStack(struct StackSwapStruct *sss,
                           LONG (*func)(APTR), APTR arg);

UBYTE aa_GammaLUT[256];

static struct SignalSemaphore aa_GlyphSem;
static APTR aa_Stack;
static FT_Memory aa_FTMemory;
static FT_Library aa_FTLib;

static struct AAFace aa_Faces[AA_MAX_MAPPINGS];
static LONG aa_NumFaces;
static struct AAFont aa_Fonts[AA_MAX_MAPPINGS];
static LONG aa_NumFonts;
static UBYTE aa_Charset;

static struct AAGlyph *aa_Hash[AA_HASH_SIZE];
static struct MinList aa_LRU;           /* most recently used first */
static ULONG aa_CacheBytes;
static ULONG aa_CacheCount;
static ULONG aa_CacheLimit = AA_DEFAULT_CACHE_KB * 1024;

#ifdef DEBUG
static ULONG stat_hits, stat_misses, stat_evictions, stat_missing;
#endif

/* ISO-8859-9 differs from ISO-8859-1 in six positions. */
static ULONG ToUnicode(UBYTE c)
{
    if (aa_Charset == AA_CHARSET_LATIN5)
    {
        switch (c)
        {
            case 0xD0: return 0x011E;   /* G breve */
            case 0xDD: return 0x0130;   /* I dot */
            case 0xDE: return 0x015E;   /* S cedilla */
            case 0xF0: return 0x011F;   /* g breve */
            case 0xFD: return 0x0131;   /* dotless i */
            case 0xFE: return 0x015F;   /* s cedilla */
        }
    }
    return c;
}

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* "Arial.font" matches mapping name "arial". */
static BOOL FontNameMatches(const char *fontname, const char *mapname)
{
    while (*mapname && ToLower(*fontname) == ToLower(*mapname))
    {
        fontname++;
        mapname++;
    }
    if (*mapname)
        return FALSE;
    return *fontname == 0 || (fontname[0] == '.' &&
                              ToLower(fontname[1]) == 'f' &&
                              ToLower(fontname[2]) == 'o' &&
                              ToLower(fontname[3]) == 'n' &&
                              ToLower(fontname[4]) == 't' &&
                              fontname[5] == 0);
}

static BOOL StrEq(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static void StrCopy(char *dst, const char *src, int size)
{
    int i;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* Must hold aa_GlyphSem: there is only one render stack. */
static LONG RunOnRenderStack(LONG (*func)(APTR), APTR arg)
{
    struct StackSwapStruct sss;

    sss.stk_Lower = aa_Stack;
    sss.stk_Upper = (ULONG)aa_Stack + AA_STACK_SIZE;
    sss.stk_Pointer = (APTR)sss.stk_Upper;
    return aa_CallOnStack(&sss, func, arg);
}

/* ------------------------------------------------------------------ */
/* Initialisation (main process)                                       */
/* ------------------------------------------------------------------ */

static void ReportError(BOOL report, const char *fmt, LONG a, LONG b)
{
    LONG args[2];

    D((fmt, a, b));
    if (!report || !Output())
        return;
    args[0] = a;
    args[1] = b;
    VPrintf((CONST_STRPTR)fmt, args);
}

static BOOL LoadFile(struct AAFace *f, BOOL report)
{
    BPTR fh;
    struct FileInfoBlock *fib;
    LONG size = -1;

    fh = Open((CONST_STRPTR)f->path, MODE_OLDFILE);
    if (!fh)
    {
        ReportError(report, "AAText: cannot open %s\n", (LONG)f->path, 0);
        return FALSE;
    }
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib)
    {
        if (ExamineFH(fh, fib))
            size = fib->fib_Size;
        FreeDosObject(DOS_FIB, fib);
    }
    if (size <= 0 || size > AA_MAX_FONTFILE)
    {
        ReportError(report, "AAText: bad file size for %s\n", (LONG)f->path, 0);
        Close(fh);
        return FALSE;
    }
    f->data = AllocVec(size, MEMF_ANY);
    if (!f->data)
    {
        ReportError(report, "AAText: no memory for %s\n", (LONG)f->path, 0);
        Close(fh);
        return FALSE;
    }
    if (Read(fh, f->data, size) != size)
    {
        ReportError(report, "AAText: read error on %s\n", (LONG)f->path, 0);
        Close(fh);
        FreeVec(f->data);
        f->data = NULL;
        return FALSE;
    }
    Close(fh);
    f->size = size;
    return TRUE;
}

static LONG InitLibraryOnStack(APTR arg)
{
    FT_Error err = FT_New_Library(aa_FTMemory, &aa_FTLib);

    if (!err)
        FT_Add_Default_Modules(aa_FTLib);
    return err;
}

static LONG OpenFaceOnStack(APTR arg)
{
    struct AAFace *f = arg;

    return FT_New_Memory_Face(aa_FTLib, f->data, f->size, 0, &f->face);
}

static LONG DoneLibraryOnStack(APTR arg)
{
    FT_Done_Library(aa_FTLib);
    return 0;
}

static struct AAFace *GetFace(const char *path, BOOL report)
{
    struct AAFace *f;
    LONG i, err;

    for (i = 0; i < aa_NumFaces; i++)
        if (StrEq(aa_Faces[i].path, path))
            return aa_Faces[i].face ? &aa_Faces[i] : NULL;

    f = &aa_Faces[aa_NumFaces++];
    StrCopy(f->path, path, AA_PATH_LEN);
    if (!LoadFile(f, report))
        return NULL;

    err = RunOnRenderStack(OpenFaceOnStack, f);
    if (err)
    {
        ReportError(report, "AAText: FreeType error %ld opening %s\n",
                    err, (LONG)path);
        FreeVec(f->data);
        f->data = NULL;
        f->face = NULL;
        return NULL;
    }
    D(("AAText: loaded %s: \"%s\" %s, %ld glyphs, %ld units/em\n",
       (ULONG)path, (ULONG)f->face->family_name,
       (ULONG)(f->face->style_name ? f->face->style_name : ""),
       f->face->num_glyphs, (LONG)f->face->units_per_EM));
    return f;
}

LONG aa_GlyphsInit(const struct AAPrefs *prefs, BOOL report)
{
    double gamma = prefs->gamma100 / 100.0;
    LONG i;

    InitSemaphore(&aa_GlyphSem);
    aa_LRU.mlh_Head = (struct MinNode *)&aa_LRU.mlh_Tail;
    aa_LRU.mlh_Tail = NULL;
    aa_LRU.mlh_TailPred = (struct MinNode *)&aa_LRU.mlh_Head;
    aa_CacheLimit = prefs->cachekb * 1024;
    aa_Charset = prefs->charset;

    for (i = 0; i < 256; i++)
        aa_GammaLUT[i] = (UBYTE)(pow(i / 255.0, 1.0 / gamma) * 255.0 + 0.5);

    aa_Stack = AllocVec(AA_STACK_SIZE, MEMF_ANY);
    aa_FTMemory = FT_New_Memory();
    if (!aa_Stack || !aa_FTMemory)
    {
        ReportError(report, "AAText: out of memory\n", 0, 0);
        return 0;
    }

    ObtainSemaphore(&aa_GlyphSem);

    if (RunOnRenderStack(InitLibraryOnStack, NULL))
    {
        aa_FTLib = NULL;
        ReleaseSemaphore(&aa_GlyphSem);
        ReportError(report, "AAText: FreeType initialisation failed\n", 0, 0);
        return 0;
    }

    for (i = 0; i < prefs->nummaps; i++)
    {
        const struct AAMapping *m = &prefs->map[i];
        struct AAFace *face = GetFace(m->ttfpath, report);
        struct AAFont *font;

        if (!face)
            continue;
        font = &aa_Fonts[aa_NumFonts++];
        StrCopy(font->name, m->fontname, AA_NAME_LEN);
        font->ysize = m->ysize;
        font->pixelsize = m->pixelsize;
        font->real = m->real;
        font->face = face;
    }

    ReleaseSemaphore(&aa_GlyphSem);
    return aa_NumFonts;
}

static void FlushCache(void)
{
    LONG i;

    if (!aa_LRU.mlh_Head)       /* aa_GlyphsInit() never ran */
        return;
    while (aa_LRU.mlh_TailPred != (struct MinNode *)&aa_LRU)
    {
        struct AAGlyph *g = (struct AAGlyph *)aa_LRU.mlh_Head;

        Remove((struct Node *)&g->lru);
        FreeMem(g, g->allocsize);
    }
    for (i = 0; i < AA_HASH_SIZE; i++)
        aa_Hash[i] = NULL;
    aa_CacheBytes = 0;
    aa_CacheCount = 0;
}

void aa_GlyphsCleanup(void)
{
    LONG i;

    FlushCache();

    if (aa_FTLib)
    {
        ObtainSemaphore(&aa_GlyphSem);
        RunOnRenderStack(DoneLibraryOnStack, NULL);   /* frees faces/sizes */
        ReleaseSemaphore(&aa_GlyphSem);
        aa_FTLib = NULL;
    }
    for (i = 0; i < aa_NumFaces; i++)
    {
        if (aa_Faces[i].data)
            FreeVec(aa_Faces[i].data);
        aa_Faces[i].data = NULL;
        aa_Faces[i].face = NULL;
    }
    aa_NumFaces = aa_NumFonts = 0;

    if (aa_FTMemory)
        FT_Done_Memory(aa_FTMemory);
    aa_FTMemory = NULL;
    if (aa_Stack)
        FreeVec(aa_Stack);
    aa_Stack = NULL;
}

/* ------------------------------------------------------------------ */
/* Text() path                                                         */
/* ------------------------------------------------------------------ */

struct AAFont *aa_FindFont(struct TextFont *tf)
{
    const char *name = tf->tf_Message.mn_Node.ln_Name;
    LONG i;

    if (!name)
        return NULL;
    for (i = 0; i < aa_NumFonts; i++)
    {
        struct AAFont *f = &aa_Fonts[i];

        if (f->ysize == tf->tf_YSize && !f->failed &&
            FontNameMatches(name, f->name))
            return f;
    }
    return NULL;
}

LONG aa_FontCount(void)
{
    return aa_NumFonts;
}

struct AAFont *aa_FontAt(LONG i)
{
    return (i >= 0 && i < aa_NumFonts) ? &aa_Fonts[i] : NULL;
}

void aa_LockGlyphs(void)
{
    ObtainSemaphore(&aa_GlyphSem);
}

void aa_UnlockGlyphs(void)
{
    ReleaseSemaphore(&aa_GlyphSem);
}

struct PrepareReq
{
    struct AAFont   *font;
    struct TextFont *tf;
};

/* Width of a character cell in the bitmap font, as Text() advances. */
static LONG CellWidth(struct TextFont *tf, UBYTE c)
{
    LONG idx;

    if (c < tf->tf_LoChar || c > tf->tf_HiChar)
        return -1;
    idx = c - tf->tf_LoChar;
    if (tf->tf_CharSpace)
        return ((WORD *)tf->tf_CharSpace)[idx] +
               (tf->tf_CharKern ? ((WORD *)tf->tf_CharKern)[idx] : 0);
    return tf->tf_XSize;
}

/*
 * Pick the pixel size whose advance widths best match the bitmap font,
 * so glyphs fill the original cells. Measured on letters and digits.
 */
static LONG AutoPixelSize(FT_Face face, struct TextFont *tf)
{
    static const char sample[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    LONG sum_bm = 0, sum_ft = 0, px;
    const char *s;

    for (s = sample; *s; s++)
    {
        LONG w = CellWidth(tf, (UBYTE)*s);
        FT_UInt gi = FT_Get_Char_Index(face, (UBYTE)*s);
        FT_Fixed adv;

        if (w <= 0 || !gi)
            continue;
        if (FT_Get_Advance(face, gi, FT_LOAD_NO_SCALE, &adv))
            continue;
        sum_bm += w;
        sum_ft += adv;
    }
    if (sum_ft <= 0)
        return tf->tf_YSize;

    px = (sum_bm * face->units_per_EM + sum_ft / 2) / sum_ft;
    if (px < 4)
        px = 4;
    if (px > 255)
        px = 255;
    return px;
}

static LONG PrepareOnStack(APTR arg)
{
    struct PrepareReq *r = arg;
    struct AAFont *font = r->font;
    FT_Face face = font->face->face;
    FT_Size size;
    LONG px;

    if (FT_New_Size(face, &size))
        return FALSE;
    FT_Activate_Size(size);

    px = font->pixelsize ? font->pixelsize : AutoPixelSize(face, r->tf);
    if (FT_Set_Pixel_Sizes(face, 0, px))
    {
        FT_Done_Size(size);
        return FALSE;
    }
    font->ftsize = size;

    /*
     * Real metrics: advance and ink extent of every character code, with
     * the same load flags as RenderOnStack() so widths and drawing agree.
     */
    if (font->real)
    {
        LONG c;

        for (c = 0; c < 256; c++)
        {
            FT_UInt gi = FT_Get_Char_Index(face, ToUnicode(c));
            FT_Glyph_Metrics *gm = &face->glyph->metrics;

            if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP))
            {
                font->adv[c] = font->inkl[c] = font->inkr[c] = 0;
                continue;
            }
            font->adv[c] = (face->glyph->advance.x + 32) >> 6;
            font->inkl[c] = gm->horiBearingX >> 6;
            font->inkr[c] = (gm->horiBearingX + gm->width + 63) >> 6;
        }
    }

    D(("AAText: font %s/%ld: baseline=%ld xsize=%ld -> %s %ldpx "
       "(asc=%ld desc=%ld)%s\n",
       (ULONG)font->name, (LONG)font->ysize, (LONG)r->tf->tf_Baseline,
       (LONG)r->tf->tf_XSize, (ULONG)face->family_name, px,
       (LONG)(size->metrics.ascender >> 6),
       (LONG)(size->metrics.descender >> 6),
       (ULONG)(font->real ? " real metrics" : "")));
    return TRUE;
}

BOOL aa_PrepareFont(struct AAFont *font, struct TextFont *tf)
{
    struct PrepareReq r;

    if (font->prepared)
        return TRUE;
    if (font->failed)
        return FALSE;

    r.font = font;
    r.tf = tf;
    if (RunOnRenderStack(PrepareOnStack, &r))
        font->prepared = TRUE;
    else
        font->failed = TRUE;
    return font->prepared;
}

struct GlyphReq
{
    struct AAFont  *font;
    UBYTE           code;
    struct AAGlyph *glyph;
};

static LONG RenderOnStack(APTR arg)
{
    struct GlyphReq *r = arg;
    FT_Face face = r->font->face->face;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bm;
    struct AAGlyph *g;
    FT_UInt gi;
    ULONG allocsize;
    LONG x, y, w, rows;

    FT_Activate_Size((FT_Size)r->font->ftsize);

    gi = FT_Get_Char_Index(face, ToUnicode(r->code));
#ifdef DEBUG
    if (!gi && r->code > ' ')
        stat_missing++;
#endif
    if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) ||
        FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL))
        return FALSE;

    bm = &slot->bitmap;
    w = bm->width;
    rows = bm->rows;
    if (bm->pixel_mode != FT_PIXEL_MODE_GRAY)
        w = rows = 0;

    allocsize = sizeof(struct AAGlyph) + w * rows;
    g = AllocMem(allocsize, MEMF_ANY);
    if (!g)
        return FALSE;

    g->font = r->font;
    g->code = r->code;
    g->left = slot->bitmap_left;
    g->top = slot->bitmap_top;
    g->width = w;
    g->rows = rows;
    g->advance = (slot->advance.x + 32) >> 6;
    g->allocsize = allocsize;

    for (y = 0; y < rows; y++)
    {
        const UBYTE *src = bm->buffer + y * bm->pitch;
        UBYTE *dst = g->data + y * w;

        for (x = 0; x < w; x++)
            dst[x] = aa_GammaLUT[src[x]];
    }

    r->glyph = g;
    return TRUE;
}

/* Hash bucket of a glyph. */
static inline ULONG HashOf(struct AAFont *font, UBYTE code)
{
    return (code ^ ((ULONG)font >> 4)) & (AA_HASH_SIZE - 1);
}

/* Remove the least recently used glyph from cache, hash and memory. */
static void EvictOldest(void)
{
    struct AAGlyph *g = (struct AAGlyph *)aa_LRU.mlh_TailPred;
    struct AAGlyph **pp;

    if (!g->lru.mln_Pred)       /* list empty */
        return;

    for (pp = &aa_Hash[HashOf(g->font, g->code)]; *pp; pp = &(*pp)->next)
    {
        if (*pp == g)
        {
            *pp = g->next;
            break;
        }
    }
    Remove((struct Node *)&g->lru);
    aa_CacheBytes -= g->allocsize;
    aa_CacheCount--;
    FreeMem(g, g->allocsize);
#ifdef DEBUG
    stat_evictions++;
#endif
}

struct AAGlyph *aa_GetGlyph(struct AAFont *font, UBYTE code)
{
    ULONG h = HashOf(font, code);
    struct AAGlyph *g;
    struct GlyphReq r;

    for (g = aa_Hash[h]; g; g = g->next)
    {
        if (g->font == font && g->code == code)
        {
#ifdef DEBUG
            stat_hits++;
#endif
            /* most recently used goes to the front */
            if ((struct MinNode *)g != aa_LRU.mlh_Head)
            {
                Remove((struct Node *)&g->lru);
                AddHead((struct List *)&aa_LRU, (struct Node *)&g->lru);
            }
            return g;
        }
    }

#ifdef DEBUG
    stat_misses++;
#endif
    r.font = font;
    r.code = code;
    r.glyph = NULL;
    if (!RunOnRenderStack(RenderOnStack, &r))
        return NULL;

    g = r.glyph;
    g->next = aa_Hash[h];
    aa_Hash[h] = g;
    AddHead((struct List *)&aa_LRU, (struct Node *)&g->lru);
    aa_CacheBytes += g->allocsize;
    aa_CacheCount++;

    /*
     * Evict from the tail, never the glyph just added. Safe: every user
     * of glyph pointers holds aa_GlyphSem, and callers use each glyph
     * before asking for the next one.
     */
    while (aa_CacheBytes > aa_CacheLimit &&
           aa_LRU.mlh_TailPred != (struct MinNode *)g)
        EvictOldest();

    return g;
}

void aa_GetCacheStats(ULONG *bytes, ULONG *count)
{
    *bytes = aa_CacheBytes;
    *count = aa_CacheCount;
}

#ifdef DEBUG
void aa_PrintGlyphStats(void)
{
    kprintf("AAText: glyph cache: %ld hits, %ld misses, %ld evictions, "
            "%ld glyphs, %ld of %ld bytes, %ld chars without glyph\n",
            stat_hits, stat_misses, stat_evictions, aa_CacheCount,
            aa_CacheBytes, aa_CacheLimit, stat_missing);
}
#endif
