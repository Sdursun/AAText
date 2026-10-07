/*
 * AAText - TrueType faces, font detection, glyph rendering and cache.
 *
 * FreeType is only ever called on AAText's own stack (StackSwap), since
 * Text() callers such as input.device have only ~3 KB of stack left, and
 * only while aa_GlyphSem is held. That one semaphore also protects the
 * glyph cache, the shared render stack and additions to the font and
 * name tables. While it is held, no graphics/layers/intuition function
 * is called, so it can never take part in a deadlock with layer locks
 * held by our callers.
 *
 * Fonts are found in two ways: explicit mappings from the prefs file,
 * and automatic detection: an outline font installed with a font manager
 * has a .otag file next to its .font file, naming the TrueType file and
 * the code page. Reading it needs DOS, so Text() only queues the font
 * name and AAText's own process (the helper) resolves it.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_ADVANCES_H
#include FT_SIZES_H
#include FT_DRIVER_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

/* src/ft/aa_ftsystem.c (internal FreeType API, not in public headers) */
FT_Memory FT_New_Memory(void);
void FT_Done_Memory(FT_Memory memory);

#include "glyphs.h"
#include "otag.h"
#include "debug.h"

#define AA_STACK_SIZE   (32 * 1024)
#define AA_HASH_SIZE    256
#define AA_MAX_FONTFILE (8 * 1024 * 1024)
#define AA_MAX_OTAG     (64 * 1024)

struct AAFace
{
    char    path[AA_PATH_LEN];
    APTR    data;
    ULONG   size;
    FT_Face face;
    LONG    facenum;
    BOOL    hascodepage;
    BOOL    envpage;            /* .otag without one: ENV:ftcodepage */
    UWORD   codepage[256];      /* from the .otag: character -> Unicode */
};

enum { NAME_PENDING = 1, NAME_READY, NAME_NONE };

/* A font name checked (or queued) for automatic detection. */
struct AAName
{
    char           name[AA_NAME_LEN];
    char           otag[AA_PATH_LEN];
    volatile UBYTE state;
    struct AAFace *face;
};

/* stub.s */
extern LONG aa_CallOnStack(struct StackSwapStruct *sss,
                           LONG (*func)(APTR), APTR arg);

UBYTE aa_GammaLUT[256];

static struct SignalSemaphore aa_GlyphSem;
static APTR aa_Stack;
static FT_Memory aa_FTMemory;
static FT_Library aa_FTLib;

/*
 * Tables only grow while AAText runs. An entry is filled completely
 * before the count is raised, so readers can scan them without a lock.
 * Faces are only added by AAText's own process.
 */
static struct AAFace aa_Faces[AA_MAX_FACES];
static volatile LONG aa_NumFaces;
static struct AAFont aa_Fonts[AA_MAX_FONTS];
static volatile LONG aa_NumFonts;
static struct AAName aa_Names[AA_MAX_NAMES];
static volatile LONG aa_NumNames;

static UBYTE aa_Charset;
static BOOL aa_AutoDetect;
static BOOL aa_AutoReal;
static BOOL aa_Kerning = TRUE;        /* fixed while AAText runs, like real */
static UBYTE aa_Hinting;

/*
 * FT_Load_Glyph() flags for every glyph load - rendering, real metrics
 * tables and size selection alike, so measured and drawn widths agree.
 * Set from the "hinting" preference in aa_GlyphsInit().
 */
static FT_Int32 aa_LoadFlags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;
/*
 * ENV:ftcodepage: 256 big-endian UWORDs, character code -> Unicode.
 * freetype2.library uses it for fonts whose .otag has no code page;
 * otherwise it maps 1:1 (ISO-8859-1), as AAText's charset default does.
 */
static UWORD aa_EnvCodePage[256];
static BOOL aa_HaveEnvCodePage;

static struct Task *aa_HelperTask;
static ULONG aa_HelperSig;

static struct AAGlyph *aa_Hash[AA_HASH_SIZE];
static struct MinList aa_LRU;           /* most recently used first */
static ULONG aa_CacheBytes;
static ULONG aa_CacheCount;
static ULONG aa_CacheLimit = AA_DEFAULT_CACHE_KB * 1024;

#ifdef DEBUG
static ULONG stat_hits, stat_misses, stat_evictions, stat_missing;
#endif

/*
 * Character code -> Unicode. Detected fonts carry the code page of
 * their .otag file; mapped fonts use the "charset" preference
 * (ISO-8859-9 differs from ISO-8859-1 in six positions).
 */
static ULONG ToUnicode(const struct AAFace *face, UBYTE c)
{
    if (face->hascodepage)
        return face->codepage[c] ? face->codepage[c] : c;

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

static BOOL StrEq(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static BOOL StrIEq(const char *a, const char *b)
{
    while (*a && ToLower(*a) == ToLower(*b))
    {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void StrCopy(char *dst, const char *src, int size)
{
    int i;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static LONG StrLen(const char *s)
{
    LONG n = 0;

    while (s[n])
        n++;
    return n;
}

/* Length of s without a trailing ".font" (any case). */
static LONG StemLen(const char *s)
{
    LONG len = StrLen(s);

    if (len > 5 && s[len - 5] == '.' && StrIEq(&s[len - 4], "font"))
        len -= 5;
    return len;
}

/* "Work:Fonts/Arial.font" -> "Arial" */
static void BaseName(const char *src, char *dst)
{
    const char *p = src;
    LONG len, i;

    for (; *src; src++)
        if (*src == ':' || *src == '/')
            p = src + 1;
    len = StemLen(p);
    if (len > AA_NAME_LEN - 1)
        len = AA_NAME_LEN - 1;
    for (i = 0; i < len; i++)
        dst[i] = p[i];
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
/* Loading (AAText's own process only)                                 */
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

/* Read a whole file into AllocVec() memory; NULL on error. */
static APTR ReadFile(const char *path, ULONG maxsize, ULONG *sizep)
{
    BPTR fh;
    struct FileInfoBlock *fib;
    LONG size = -1;
    APTR data;

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh)
        return NULL;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib)
    {
        if (ExamineFH(fh, fib))
            size = fib->fib_Size;
        FreeDosObject(DOS_FIB, fib);
    }
    if (size <= 0 || (ULONG)size > maxsize ||
        !(data = AllocVec(size, MEMF_ANY)))
    {
        Close(fh);
        return NULL;
    }
    if (Read(fh, data, size) != size)
    {
        Close(fh);
        FreeVec(data);
        return NULL;
    }
    Close(fh);
    *sizep = size;
    return data;
}

/*
 * Apply aa_Hinting: load flags for every glyph load, and the TrueType
 * interpreter version (a library-wide property, so it is set back to
 * v40 for the other modes). On the render stack, glyph lock held.
 */
static LONG SetHintingOnStack(APTR arg)
{
    FT_UInt version = aa_Hinting == AA_HINT_FULL ? TT_INTERPRETER_VERSION_35
                                                 : TT_INTERPRETER_VERSION_40;

    FT_Property_Set(aa_FTLib, "truetype", "interpreter-version", &version);

    switch (aa_Hinting)
    {
        case AA_HINT_NONE:
            aa_LoadFlags = FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
            break;

        case AA_HINT_LIGHT:
            /* autofit module: vertical-only hinting, any font */
            aa_LoadFlags = FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT |
                           FT_LOAD_NO_BITMAP;
            break;

        default:    /* normal (v40) and full (v35): the font's own hints */
            aa_LoadFlags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;
            break;
    }
    return 0;
}

static LONG InitLibraryOnStack(APTR arg)
{
    FT_Error err = FT_New_Library(aa_FTMemory, &aa_FTLib);

    if (err)
        return err;
    FT_Add_Default_Modules(aa_FTLib);
    return SetHintingOnStack(NULL);
}

static LONG OpenFaceOnStack(APTR arg)
{
    struct AAFace *f = arg;

    return FT_New_Memory_Face(aa_FTLib, f->data, f->size, f->facenum,
                              &f->face);
}

static LONG DoneLibraryOnStack(APTR arg)
{
    FT_Done_Library(aa_FTLib);
    return 0;
}

/*
 * Find or load a TrueType face. ot (may be NULL) supplies the code page.
 * Takes the glyph lock only around FreeType, never around file I/O.
 */
/*
 * Like freetype2.library: a detected font whose .otag has no code page
 * uses ENV:ftcodepage when it is set. Called with aa_GlyphSem held.
 */
static void SetEnvCodePage(struct AAFace *f)
{
    if (!f->envpage)
        return;
    f->hascodepage = aa_HaveEnvCodePage;
    if (aa_HaveEnvCodePage)
        CopyMem(aa_EnvCodePage, f->codepage, sizeof(f->codepage));
}

/* Read ENV:ftcodepage into buf; TRUE if it is set and complete. */
static BOOL ReadEnvCodePage(UWORD *buf)
{
    return GetVar((CONST_STRPTR)"ftcodepage", (STRPTR)buf,
                  256 * sizeof(UWORD),
                  LV_VAR | GVF_BINARY_VAR | GVF_DONT_NULL_TERM) ==
           (LONG)(256 * sizeof(UWORD));
}

static struct AAFace *GetFace(const char *path, LONG facenum,
                              const struct AAOTagInfo *ot, BOOL report)
{
    struct AAFace *f;
    LONG i, err;

    for (i = 0; i < aa_NumFaces; i++)
        if (aa_Faces[i].facenum == facenum && StrEq(aa_Faces[i].path, path))
            return aa_Faces[i].face ? &aa_Faces[i] : NULL;

    if (aa_NumFaces == AA_MAX_FACES)
    {
        ReportError(report, "AAText: too many TrueType files (%ld)\n",
                    AA_MAX_FACES, 0);
        return NULL;
    }

    f = &aa_Faces[aa_NumFaces];
    StrCopy(f->path, path, AA_PATH_LEN);
    f->facenum = facenum;
    f->face = NULL;
    /* the semaphore: aa_GlyphsReconfigure() may replace the ENV table */
    ObtainSemaphore(&aa_GlyphSem);
    f->hascodepage = ot && ot->hascodepage;
    f->envpage = ot && !ot->hascodepage;
    if (f->hascodepage)
        CopyMem((APTR)ot->codepage, f->codepage, sizeof(f->codepage));
    else
        SetEnvCodePage(f);
    ReleaseSemaphore(&aa_GlyphSem);
    f->data = ReadFile(path, AA_MAX_FONTFILE, &f->size);
    aa_NumFaces++;          /* failed loads are remembered too */

    if (!f->data)
    {
        ReportError(report, "AAText: cannot load %s\n", (LONG)path, 0);
        return NULL;
    }

    ObtainSemaphore(&aa_GlyphSem);
    err = RunOnRenderStack(OpenFaceOnStack, f);
    ReleaseSemaphore(&aa_GlyphSem);
    if (err)
    {
        ReportError(report, "AAText: FreeType error %ld opening %s\n",
                    err, (LONG)path);
        FreeVec(f->data);
        f->data = NULL;
        f->face = NULL;
        return NULL;
    }
    D(("AAText: loaded %s: \"%s\" %s, %ld glyphs, %ld units/em, "
       "code page: %s\n",
       (ULONG)path, (ULONG)f->face->family_name,
       (ULONG)(f->face->style_name ? f->face->style_name : ""),
       f->face->num_glyphs, (LONG)f->face->units_per_EM,
       (ULONG)(f->hascodepage ? (ot && ot->hascodepage ? ".otag" : "ENV:ftcodepage") : "charset pref")));
    return f;
}

/* Append a font entry. Caller holds aa_GlyphSem or is still in init. */
static struct AAFont *AddFont(const char *name, UWORD ysize, UWORD px,
                              BOOL real, struct AAFace *face)
{
    struct AAFont *f;

    if (aa_NumFonts == AA_MAX_FONTS)
        return NULL;
    f = &aa_Fonts[aa_NumFonts];
    StrCopy(f->name, name, AA_NAME_LEN);
    f->ysize = ysize;
    f->pixelsize = px;
    f->real = real;
    f->face = face;
    f->ftsize = NULL;
    f->prepared = FALSE;
    f->failed = FALSE;
    f->adv = f->inkl = f->inkr = NULL;
    f->kern = NULL;
    f->yoffset = 0;
    aa_NumFonts++;          /* publish after the entry is complete */
    return f;
}

/* Coverage -> gamma corrected coverage. Uses floating point: call it
   from AAText's own process, never from Text(). */
static void MakeGammaLUT(UWORD gamma100, UBYTE *lut)
{
    double gamma = gamma100 / 100.0;
    LONG i;

    for (i = 0; i < 256; i++)
        lut[i] = (UBYTE)(pow(i / 255.0, 1.0 / gamma) * 255.0 + 0.5);
}

LONG aa_GlyphsInit(const struct AAPrefs *prefs, BOOL report)
{
    LONG i, err;

    InitSemaphore(&aa_GlyphSem);
    aa_LRU.mlh_Head = (struct MinNode *)&aa_LRU.mlh_Tail;
    aa_LRU.mlh_Tail = NULL;
    aa_LRU.mlh_TailPred = (struct MinNode *)&aa_LRU.mlh_Head;
    aa_CacheLimit = prefs->cachekb * 1024;
    aa_Charset = prefs->charset;
    aa_AutoDetect = prefs->autodetect;
    aa_AutoReal = prefs->autoreal;
    aa_Kerning = prefs->kerning;
    aa_Hinting = prefs->hinting;

    aa_HaveEnvCodePage = ReadEnvCodePage(aa_EnvCodePage);
    D(("AAText: ENV:ftcodepage %s\n",
       (ULONG)(aa_HaveEnvCodePage ? "found" : "not set")));

    MakeGammaLUT(prefs->gamma100, aa_GammaLUT);

    aa_Stack = AllocVec(AA_STACK_SIZE, MEMF_ANY);
    aa_FTMemory = FT_New_Memory();
    if (!aa_Stack || !aa_FTMemory)
    {
        ReportError(report, "AAText: out of memory\n", 0, 0);
        return 0;
    }

    ObtainSemaphore(&aa_GlyphSem);
    err = RunOnRenderStack(InitLibraryOnStack, NULL);
    ReleaseSemaphore(&aa_GlyphSem);
    if (err)
    {
        aa_FTLib = NULL;
        ReportError(report, "AAText: FreeType initialisation failed\n", 0, 0);
        return 0;
    }

    for (i = 0; i < prefs->nummaps; i++)
    {
        const struct AAMapping *m = &prefs->map[i];
        struct AAFace *face = GetFace(m->ttfpath, 0, NULL, report);

        if (face)
            AddFont(m->fontname, m->ysize, m->pixelsize, m->real, face);
    }
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

/*
 * Forget every prepared size, so fonts are set up again on next use with
 * the current hinting and code page (real metrics tables included).
 * On the render stack, glyph lock held.
 */
static LONG ResetFontsOnStack(APTR arg)
{
    LONG i;

    for (i = 0; i < aa_NumFonts; i++)
    {
        struct AAFont *f = &aa_Fonts[i];

        f->prepared = FALSE;    /* first: measuring hooks re-prepare */
        if (f->ftsize)
            FT_Done_Size((FT_Size)f->ftsize);
        f->ftsize = NULL;
        f->failed = FALSE;
    }
    return 0;
}

void aa_GlyphsReconfigure(const struct AAPrefs *prefs)
{
    UBYTE lut[256];
    UWORD *page = AllocVec(256 * sizeof(UWORD), MEMF_ANY);
    BOOL havepage = page && ReadEnvCodePage(page);
    LONG i;

    MakeGammaLUT(prefs->gamma100, lut);

    ObtainSemaphore(&aa_GlyphSem);
    CopyMem(lut, aa_GammaLUT, sizeof(lut));
    /* ENV:ftcodepage may have been written since (AATextPrefs) */
    if (page)
    {
        aa_HaveEnvCodePage = havepage;
        if (havepage)
            CopyMem(page, aa_EnvCodePage, sizeof(aa_EnvCodePage));
        for (i = 0; i < aa_NumFaces; i++)
            SetEnvCodePage(&aa_Faces[i]);
    }
    aa_Charset = prefs->charset;
    aa_AutoDetect = prefs->autodetect;
    aa_CacheLimit = prefs->cachekb * 1024;
    aa_Hinting = prefs->hinting;
    if (aa_FTLib)
    {
        RunOnRenderStack(SetHintingOnStack, NULL);
        FlushCache();
        RunOnRenderStack(ResetFontsOnStack, NULL);
    }
    ReleaseSemaphore(&aa_GlyphSem);
    if (page)
        FreeVec(page);
    D(("AAText: reconfigured: gamma %ld, hinting %ld, cache %ld KB\n",
       (LONG)prefs->gamma100, (LONG)prefs->hinting, prefs->cachekb));
}

void aa_GlyphsStatus(LONG *numfonts, LONG *numfaces, ULONG *bytes,
                     ULONG *glyphs)
{
    LONG i, n = 0;

    ObtainSemaphore(&aa_GlyphSem);
    for (i = 0; i < aa_NumFaces; i++)
        if (aa_Faces[i].face)
            n++;
    *numfonts = aa_NumFonts;
    *numfaces = n;
    *bytes = aa_CacheBytes;
    *glyphs = aa_CacheCount;
    ReleaseSemaphore(&aa_GlyphSem);
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
    for (i = 0; i < aa_NumFonts; i++)
    {
        if (aa_Fonts[i].adv)
            FreeVec(aa_Fonts[i].adv);       /* inkl/inkr share it */
        aa_Fonts[i].adv = NULL;
        if (aa_Fonts[i].kern)
            FreeVec(aa_Fonts[i].kern);
        aa_Fonts[i].kern = NULL;
    }
    aa_NumFaces = aa_NumFonts = aa_NumNames = 0;

    if (aa_FTMemory)
        FT_Done_Memory(aa_FTMemory);
    aa_FTMemory = NULL;
    if (aa_Stack)
        FreeVec(aa_Stack);
    aa_Stack = NULL;
}

/* ------------------------------------------------------------------ */
/* Automatic detection                                                 */
/* ------------------------------------------------------------------ */

void aa_SetHelper(struct Task *task, ULONG sigmask)
{
    aa_HelperSig = sigmask;
    aa_HelperTask = task;
}

static LONG FindFontName(const char *base)
{
    LONG i, n = aa_NumNames;

    for (i = 0; i < n; i++)
        if (StrIEq(aa_Names[i].name, base))
            return i;
    return -1;
}

/*
 * .otag path for a font name: next to the .font file if the name has a
 * directory, otherwise in FONTS:.
 */
static void OTagPath(const char *fontname, const char *base, char *dst)
{
    const char *p;
    LONG len = 0, i;
    BOOL hasdir = FALSE;

    for (p = fontname; *p; p++)
        if (*p == ':' || *p == '/')
            hasdir = TRUE;

    if (hasdir)
    {
        len = StemLen(fontname);
        if (len > AA_PATH_LEN - 6)
            len = AA_PATH_LEN - 6;
        for (i = 0; i < len; i++)
            dst[i] = fontname[i];
    }
    else
    {
        CopyMem("FONTS:", dst, 6);
        len = 6;
        for (i = 0; base[i] && len < AA_PATH_LEN - 6; i++)
            dst[len++] = base[i];
    }
    CopyMem(".otag", dst + len, 6);
}

void aa_RequestFont(const char *fontname)
{
    char base[AA_NAME_LEN];
    BOOL added = FALSE;

    if (!fontname || !aa_AutoDetect)
        return;
    BaseName(fontname, base);
    if (!base[0] || FindFontName(base) >= 0)
        return;

    ObtainSemaphore(&aa_GlyphSem);
    if (FindFontName(base) < 0 && aa_NumNames < AA_MAX_NAMES)
    {
        struct AAName *n = &aa_Names[aa_NumNames];

        StrCopy(n->name, base, AA_NAME_LEN);
        OTagPath(fontname, base, n->otag);
        n->face = NULL;
        n->state = NAME_PENDING;
        aa_NumNames++;
        added = TRUE;
    }
    ReleaseSemaphore(&aa_GlyphSem);

    if (added && aa_HelperTask)
        Signal(aa_HelperTask, aa_HelperSig);
}

/* Read a .otag file and load the TrueType file it names. */
static struct AAFace *ResolveName(struct AAName *n, BOOL report)
{
    struct AAOTagInfo *ot;
    struct AAFace *face = NULL;
    UBYTE *buf;
    ULONG size;

    buf = ReadFile(n->otag, AA_MAX_OTAG, &size);
    if (!buf)
    {
        D(("AAText: auto: %s: no .otag, bitmap font\n", (ULONG)n->name));
        return NULL;
    }
    ot = AllocVec(sizeof(*ot), MEMF_ANY);
    if (ot && aa_ParseOTag(buf, size, ot))
    {
        char path[AA_PATH_LEN];
        const char *file = ot->fontfile;
        BPTR lock;

        /* absolute in practice; relative paths are tried in FONTS: */
        StrCopy(path, file, AA_PATH_LEN);
        lock = Lock((CONST_STRPTR)path, ACCESS_READ);
        if (!lock)
        {
            CopyMem("FONTS:", path, 6);
            StrCopy(path + 6, file, AA_PATH_LEN - 6);
            lock = Lock((CONST_STRPTR)path, ACCESS_READ);
        }
        if (lock)
        {
            UnLock(lock);
            D(("AAText: auto: %s -> %s (engine \"%s\")\n", (ULONG)n->name,
               (ULONG)path, (ULONG)(ot->engine ? ot->engine : "?")));
            face = GetFace(path, ot->facenum, ot, report);
        }
        else
            ReportError(report, "AAText: %s: font file %s not found\n",
                        (LONG)n->otag, (LONG)file);
    }
    else
        D(("AAText: auto: %s: no TrueType file in .otag\n", (ULONG)n->name));

    if (ot)
        FreeVec(ot);
    FreeVec(buf);
    return face;
}

LONG aa_ResolvePending(BOOL report)
{
    LONG i, done = 0;

    for (i = 0; i < aa_NumNames; i++)
    {
        struct AAName *n = &aa_Names[i];

        if (n->state != NAME_PENDING)
            continue;
        n->face = ResolveName(n, report);
        n->state = n->face ? NAME_READY : NAME_NONE;
        done++;
    }
    return done;
}

/* ------------------------------------------------------------------ */
/* Text() path                                                         */
/* ------------------------------------------------------------------ */

struct AAFont *aa_FindFont(struct TextFont *tf)
{
    const char *fontname = tf->tf_Message.mn_Node.ln_Name;
    char base[AA_NAME_LEN];
    struct AAFont *font = NULL;
    LONG i, n;

    if (!fontname)
        return NULL;
    BaseName(fontname, base);

    n = aa_NumFonts;
    for (i = 0; i < n; i++)
    {
        struct AAFont *f = &aa_Fonts[i];

        if (f->ysize == tf->tf_YSize && StrIEq(f->name, base))
            return f->failed ? NULL : f;
    }

    if (!aa_AutoDetect)
        return NULL;

    i = FindFontName(base);
    if (i < 0)
    {
        aa_RequestFont(fontname);
        return NULL;
    }
    if (aa_Names[i].state != NAME_READY)
        return NULL;

    /* detected face, first time in this size: add an entry */
    ObtainSemaphore(&aa_GlyphSem);
    for (n = 0; n < aa_NumFonts; n++)
    {
        if (aa_Fonts[n].ysize == tf->tf_YSize &&
            StrIEq(aa_Fonts[n].name, base))
        {
            font = &aa_Fonts[n];
            break;
        }
    }
    if (!font)
        font = AddFont(base, tf->tf_YSize, 0, aa_AutoReal, aa_Names[i].face);
    ReleaseSemaphore(&aa_GlyphSem);

    return (font && !font->failed) ? font : NULL;
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

/*
 * Top and bottom ink rows of a character in the bitmap font's glyph
 * image (tf_CharData), or FALSE if it has none.
 */
static BOOL InkRows(struct TextFont *tf, UBYTE c, LONG *top, LONG *bottom)
{
    const UBYTE *data = tf->tf_CharData;
    ULONG loc;
    LONG idx, bit0, w, x, y;

    if (!data || !tf->tf_CharLoc || (tf->tf_Style & FSF_COLORFONT) ||
        c < tf->tf_LoChar || c > tf->tf_HiChar)
        return FALSE;
    idx = c - tf->tf_LoChar;
    loc = ((ULONG *)tf->tf_CharLoc)[idx];
    bit0 = loc >> 16;
    w = loc & 0xFFFF;

    *top = -1;
    for (y = 0; y < tf->tf_YSize; y++)
    {
        const UBYTE *row = data + y * tf->tf_Modulo;

        for (x = 0; x < w; x++)
        {
            LONG b = bit0 + x;

            if (row[b >> 3] & (0x80 >> (b & 7)))
            {
                if (*top < 0)
                    *top = y;
                *bottom = y;
                break;
            }
        }
    }
    return *top >= 0;
}

/*
 * The row the bitmap font's letters really sit on: the lowest ink row of
 * letters without descenders. Returns tf_Baseline if it cannot be
 * measured (e.g. colour fonts).
 */
static LONG MeasureBaseline(struct TextFont *tf, LONG *capheight)
{
    static const char flat[] = "HEIxzn0";
    const char *s;
    LONG base = -1, top, bottom;

    *capheight = 0;
    for (s = flat; *s; s++)
    {
        if (!InkRows(tf, (UBYTE)*s, &top, &bottom))
            continue;
        if (bottom > base)
            base = bottom;
        if (*s == 'H')
            *capheight = bottom - top + 1;
    }
    return base >= 0 ? base : tf->tf_Baseline;
}

/*
 * Rows of a rendered glyph that look solid (some pixel at least 50%
 * coverage): what the eye reads as the letter's height. Faint
 * antialiasing rows at the edges do not count.
 */
static LONG SolidRows(const FT_Bitmap *bm)
{
    LONG rows = 0, x, y;

    for (y = 0; y < (LONG)bm->rows; y++)
    {
        const UBYTE *row = bm->buffer + y * bm->pitch;

        for (x = 0; x < (LONG)bm->width; x++)
        {
            if (row[x] >= 128)
            {
                rows++;
                break;
            }
        }
    }
    return rows;
}

/*
 * Pixel size at which the TrueType 'H' is capheight pixels tall, or 0.
 * Hinting may round the result by a pixel; the neighbours are checked.
 */
static LONG CapPixelSize(FT_Face face, LONG capheight)
{
    FT_UInt gi = FT_Get_Char_Index(face, 'H');
    static const BYTE tries[3] = { 0, -1, 1 };  /* exact estimate first */
    LONG units, px, best = 0, besterr = 1000, i;

    if (!gi || FT_Load_Glyph(face, gi, FT_LOAD_NO_SCALE))
        return 0;
    units = face->glyph->metrics.height;
    if (units <= 0)
        return 0;
    px = (capheight * face->units_per_EM + units / 2) / units;

    for (i = 0; i < 3; i++)
    {
        LONG p = px + tries[i], h, err;

        if (p < 4 || FT_Set_Pixel_Sizes(face, 0, p) ||
            FT_Load_Glyph(face, gi, aa_LoadFlags) ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
            continue;
        h = SolidRows(&face->glyph->bitmap);
        err = h > capheight ? h - capheight : capheight - h;
        if (err < besterr)
        {
            besterr = err;
            best = p;
        }
    }
    return best;
}

#define KU16(p) ((UWORD)(((p)[0] << 8) | (p)[1]))

/* The codes whose glyph is gi: [*lo, *hi) in the (glyph, code) list. */
static void CodesOfGlyph(const ULONG *map, LONG n, UWORD gi,
                         LONG *lo, LONG *hi)
{
    LONG a = 0, b = n;

    while (a < b)               /* first entry with glyph >= gi */
    {
        LONG m = (a + b) >> 1;

        if ((map[m] >> 8) < gi)
            a = m + 1;
        else
            b = m;
    }
    *lo = a;
    while (a < n && (map[a] >> 8) == gi)
        a++;
    *hi = a;
}

/*
 * One pass over the format 0 subtables of the 'kern' table: count
 * (fill == FALSE) or store (fill == TRUE) every pair between our
 * character codes whose kerning is not 0 pixels at this size.
 */
static void KernPass(struct AAKern *k, const UBYTE *t, ULONG len,
                     const ULONG *map, LONG nmap, FT_Fixed scale,
                     UWORD *count, BOOL fill)
{
    ULONG off = 4;
    LONG nt = len >= 4 ? KU16(t + 2) : 0;

    while (nt-- > 0 && off + 14 <= len)
    {
        const UBYTE *st = t + off;
        ULONG stlen = KU16(st + 2);
        UWORD cov = KU16(st + 4);
        LONG npairs, i;

        if (stlen < 14 || off + stlen > len)
            break;
        off += stlen;
        /* format 0, horizontal, no minimum or cross-stream values */
        if ((cov >> 8) != 0 || (cov & 7) != 1)
            continue;
        npairs = KU16(st + 6);
        if ((ULONG)(14 + npairs * 6) > stlen)
            npairs = (stlen - 14) / 6;

        for (i = 0; i < npairs; i++)
        {
            const UBYTE *p = st + 14 + i * 6;
            LONG v = (WORD)KU16(p + 4);
            LONG px = (FT_MulFix(v, scale) + 32) >> 6;
            LONG l0, l1, r0, r1, l, r;

            if (px == 0)
                continue;
            if (px < -128)
                px = -128;
            if (px > 127)
                px = 127;
            CodesOfGlyph(map, nmap, KU16(p), &l0, &l1);
            if (l0 == l1)
                continue;
            CodesOfGlyph(map, nmap, KU16(p + 2), &r0, &r1);
            for (l = l0; l < l1; l++)
            {
                UBYTE lc = map[l] & 0xFF;

                for (r = r0; r < r1; r++)
                {
                    if (!fill)
                        count[lc]++;
                    else if (count[lc] < k->first[lc + 1])
                    {
                        k->pair[count[lc]].right = map[r] & 0xFF;
                        k->pair[count[lc]].px = px;
                        count[lc]++;
                    }
                }
            }
        }
    }
}

/*
 * Pair kerning for real metrics mode from the 'kern' table (GPOS
 * kerning would need a shaping engine). gidx: glyph of each code.
 * Measuring may read font->kern meanwhile, so the table is filled in
 * place and every index stays within AA_MAX_KERN. Render stack.
 */
static void BuildKerning(struct AAFont *font, FT_Face face,
                         const UWORD *gidx)
{
    struct AAKern *k = font->kern;
    UWORD count[256];
    ULONG map[256];
    FT_ULong len = 0;
    UBYTE *t;
    LONG n = 0, i, j, total;

    if (FT_Load_Sfnt_Table(face, TTAG_kern, 0, NULL, &len) || len < 4)
        return;
    t = AllocVec(len, MEMF_ANY);
    if (!t)
        return;
    if (FT_Load_Sfnt_Table(face, TTAG_kern, 0, t, &len) || KU16(t) != 0)
        goto out;               /* only the version 0 (Microsoft) table */
    if (!k && !(k = AllocVec(sizeof(*k), MEMF_ANY | MEMF_CLEAR)))
        goto out;

    /* (glyph << 8 | code), sorted by glyph, for the glyph -> codes lookup */
    for (i = 0; i < 256; i++)
    {
        ULONG e;

        if (!gidx[i])
            continue;
        e = ((ULONG)gidx[i] << 8) | i;
        for (j = n; j > 0 && map[j - 1] > e; j--)
            map[j] = map[j - 1];
        map[j] = e;
        n++;
    }

    /* count the pairs per left code, then lay out the table */
    memset(count, 0, sizeof(count));
    KernPass(k, t, len, map, n, face->size->metrics.x_scale, count, FALSE);
    for (i = 0, total = 0; i < 256; i++)
    {
        LONG c = count[i];

        if (total + c > AA_MAX_KERN)
            c = AA_MAX_KERN - total;        /* the rest is dropped */
        k->first[i] = total;
        count[i] = total;                   /* fill position */
        total += c;
    }
    k->first[256] = total;
    KernPass(k, t, len, map, n, face->size->metrics.x_scale, count, TRUE);

    /* sort each left code's pairs by right code for aa_KernPair() */
    for (i = 0; i < 256; i++)
    {
        LONG a, b;

        for (a = k->first[i] + 1; a < k->first[i + 1]; a++)
        {
            UBYTE r = k->pair[a].right;
            BYTE px = k->pair[a].px;

            for (b = a; b > k->first[i] && k->pair[b - 1].right > r; b--)
                k->pair[b] = k->pair[b - 1];
            k->pair[b].right = r;
            k->pair[b].px = px;
        }
    }
    font->kern = k;
    D(("AAText: %s %ld: %ld kerning pairs\n", (ULONG)font->name,
       (LONG)font->ysize, total));
out:
    FreeVec(t);
}

static LONG PrepareOnStack(APTR arg)
{
    struct PrepareReq *r = arg;
    struct AAFont *font = r->font;
    FT_Face face = font->face->face;
    FT_Size size;
    LONG px;

    LONG capheight, measured = MeasureBaseline(r->tf, &capheight);

    if (FT_New_Size(face, &size))
        return FALSE;
    FT_Activate_Size(size);

    /*
     * Safe metrics: match the bitmap font's widths, letters must fit its
     * cells. Real metrics: widths come from the TrueType font anyway, so
     * match the capital height and the text keeps the size of the
     * original font.
     */
    if (font->pixelsize)
        px = font->pixelsize;
    else if (aa_UseReal(font, r->tf) && capheight > 0)
        px = CapPixelSize(face, capheight);
    else
        px = 0;
    if (px <= 0)
        px = AutoPixelSize(face, r->tf);
    if (FT_Set_Pixel_Sizes(face, 0, px))
    {
        FT_Done_Size(size);
        return FALSE;
    }
    font->ftsize = size;
    font->pxused = px;

    /*
     * Real metrics: advance and ink extent of every character code, with
     * the same load flags as RenderOnStack() so widths and drawing agree.
     */
    if (aa_UseReal(font, r->tf))
    {
        LONG c;

        if (!font->adv)
        {
            font->adv = AllocVec(3 * 256 * sizeof(WORD), MEMF_ANY);
            if (!font->adv)
            {
                font->ftsize = NULL;
                FT_Done_Size(size);
                return FALSE;
            }
            font->inkl = font->adv + 256;
            font->inkr = font->adv + 512;
        }
        UWORD gidx[256];

        for (c = 0; c < 256; c++)
        {
            FT_UInt gi = FT_Get_Char_Index(face, ToUnicode(font->face, c));
            FT_Glyph_Metrics *gm = &face->glyph->metrics;

            gidx[c] = gi;
            if (FT_Load_Glyph(face, gi, aa_LoadFlags))
            {
                font->adv[c] = font->inkl[c] = font->inkr[c] = 0;
                continue;
            }
            font->adv[c] = (face->glyph->advance.x + 32) >> 6;
            font->inkl[c] = gm->horiBearingX >> 6;
            font->inkr[c] = (gm->horiBearingX + gm->width + 63) >> 6;
        }
        if (aa_Kerning && FT_HAS_KERNING(face))
            BuildKerning(font, face, gidx);
    }

    /*
     * Vertical position: put our baseline where the bitmap font's
     * letters actually stand, so text stays centred in title bars etc.
     */
    {
        struct TextFont *tf = r->tf;
        LONG off = measured - tf->tf_Baseline;
        LONG limit = tf->tf_YSize / 2;

        if (off > limit)
            off = limit;
        if (off < -limit)
            off = -limit;
        font->yoffset = off;

#ifdef DEBUG
        {
            FT_UInt gi = FT_Get_Char_Index(face, 'H');
            LONG ftcap = 0;

            if (gi && !FT_Load_Glyph(face, gi, aa_LoadFlags))
                ftcap = (face->glyph->metrics.horiBearingY + 32) >> 6;
            kprintf("AAText: font %s/%ld: baseline=%ld measured=%ld "
                    "(offset %ld), cap height bitmap=%ld TrueType=%ld\n",
                    (ULONG)font->name, (LONG)font->ysize,
                    (LONG)tf->tf_Baseline, measured, off, capheight, ftcap);
        }
#endif
    }

    D(("AAText: font %s/%ld: baseline=%ld xsize=%ld -> %s %ldpx "
       "(asc=%ld desc=%ld)%s\n",
       (ULONG)font->name, (LONG)font->ysize, (LONG)r->tf->tf_Baseline,
       (LONG)r->tf->tf_XSize, (ULONG)face->family_name, px,
       (LONG)(size->metrics.ascender >> 6),
       (LONG)(size->metrics.descender >> 6),
       (ULONG)(aa_UseReal(font, r->tf) ? " real metrics" : "")));
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

    gi = FT_Get_Char_Index(face, ToUnicode(r->font->face, r->code));
#ifdef DEBUG
    if (!gi && r->code > ' ')
        stat_missing++;
#endif
    if (FT_Load_Glyph(face, gi, aa_LoadFlags) ||
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
