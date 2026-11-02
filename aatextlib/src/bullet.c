/*
 * aatext.library - the bullet.library API: an outline engine for
 * diskfont.library (OT_Engine "aatext").
 *
 * The behaviour follows freetype2.library 1.x (AROS, from E. Lesueur's
 * MorphOS ft2.library and R. Griffith's ttf.library) so that existing .otag
 * files give the same font sizes: the em height comes from the font metric
 * chosen with OT_Spec4_Metric, glyph widths are fractions of that em.
 *
 * New: monochrome hinting for 1-bit glyph maps, 8-bit antialiased glyph
 * maps (OT_GlyphMap8Bit), 32-bit glyph codes, OT_EmboldenX/Y, OT_SetFactor,
 * symbol fonts, bitmap fonts (PCF, BDF, FNT) and a stack that is always
 * big enough for FreeType.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <utility/tagitem.h>
#include <diskfont/diskfonttag.h>
#include <diskfont/glyph.h>
#include <diskfont/oterrors.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <string.h>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftoutln.h>
#include <freetype/tttables.h>
#include <freetype/ftglyph.h>

#include "lib.h"
#include <libraries/aatext.h>

#define REG(r, decl) register decl __asm(#r)

#ifndef OT_GlyphMap8Bit
#define OT_GlyphMap8Bit     (OT_Level0 | OT_Indirect | 0x1a)
#endif
#ifndef OT_WidthList32
#define OT_WidthList32      (OT_Level0 | OT_Indirect | 0x1b)
#endif
#ifndef OT_GlyphCode_32
#define OT_GlyphCode_32     (OT_Level0 | 0x18)
#define OT_GlyphCode2_32    (OT_Level0 | 0x19)
#endif
#ifndef OT_NumGlyphs
#define OT_NumGlyphs        (OT_Level0 | OT_Indirect | 0x1d)
#endif

#define PATHMAX 256

struct FT2Engine
{
    struct GlyphEngine      ge;         /* gle_Library, gle_Name */
    struct SignalSemaphore  lock;
    FT_Library              lib;
    FT_Face                 face;
    LONG                    error;      /* OTERR_... of the last request */

    /* requested font */
    char                    fontfile[PATHMAX];
    char                    afmfile[PATHMAX];
    LONG                    facenum;
    /* opened font */
    char                    openfile[PATHMAX];
    LONG                    openfacenum;
    BOOL                    symbolmap;  /* MS Symbol charmap: U+F0xx */

    UWORD                   codepage[256];
    ULONG                   code, code2;

    /* instance */
    LONG                    pointsize;  /* whole points */
    ULONG                   dpi;        /* x << 16 | y, only remembered */
    FIXED                   setfactor;
    LONG                    metric;
    LONG                    custombbox;
    LONG                    hinter;
    FIXED                   emboldenx, emboldeny;
    FIXED                   hold_sin;
    BOOL                    do_shear, do_rotate;
    FT_Matrix               shear, rotate;
    BOOL                    dirty;      /* size must be set again */
    LONG                    upem;       /* em height in font units */
    LONG                    xppem, yppem;
};

static const char EngineName[] = "aatext";

static LONG seterr(struct FT2Engine *e, LONG err)
{
    e->error = err;
    return err;
}

/* ---- code page -------------------------------------------------------- */

static void default_codepage(struct FT2Engine *e)
{
    LONG i;

    for (i = 0; i < 256; i++)
        e->codepage[i] = i;

    /* ENV:ftcodepage: 256 big-endian UWORDs, character code -> Unicode */
    if (FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS)
    {
        UWORD buf[256];

        if (GetVar((STRPTR)"ftcodepage", (STRPTR)buf, sizeof(buf),
                   GVF_BINARY_VAR | GVF_DONT_NULL_TERM) == sizeof(buf))
            CopyMem(buf, e->codepage, sizeof(buf));
    }
}

static FT_UInt glyph_index(struct FT2Engine *e, ULONG code)
{
    ULONG uni = code < 256 ? e->codepage[code] : code;
    FT_UInt gi;

    if (!e->face)
        return 0;
    gi = FT_Get_Char_Index(e->face, uni);
    /* symbol fonts put their glyphs at U+F020..U+F0FF */
    if (!gi && e->symbolmap && code < 256)
        gi = FT_Get_Char_Index(e->face, 0xF000 | code);
    return gi;
}

/* ---- face and instance ------------------------------------------------- */

static void set_transform(struct FT2Engine *e)
{
    FT_Matrix m;

    if (!e->face)
        return;
    if (e->do_shear && e->do_rotate)
    {
        m = e->shear;
        FT_Matrix_Multiply(&e->rotate, &m);
        FT_Set_Transform(e->face, &m, NULL);
    }
    else if (e->do_shear)
        FT_Set_Transform(e->face, &e->shear, NULL);
    else if (e->do_rotate)
        FT_Set_Transform(e->face, &e->rotate, NULL);
    else
        FT_Set_Transform(e->face, NULL, NULL);
}

static void close_face(struct FT2Engine *e)
{
    if (e->face)
        FT_Done_Face(e->face);
    e->face = NULL;
    e->openfile[0] = 0;
}

/* Opens the requested font file unless it is open already. */
static LONG open_face(struct FT2Engine *e)
{
    FT_Error err;

    if (e->face && e->openfacenum == e->facenum &&
        !Stricmp((STRPTR)e->openfile, (STRPTR)e->fontfile))
        return seterr(e, OTERR_Success);

    close_face(e);
    e->dirty = TRUE;
    if (!e->fontfile[0])
        return seterr(e, OTERR_NoFace);

    err = FT_New_Face(e->lib, e->fontfile, e->facenum, &e->face);
    if (err)
    {
        D(("ft2: FT_New_Face %s: %ld\n", (ULONG)e->fontfile, err));
        e->face = NULL;
        return seterr(e, OTERR_BadFace);
    }
    if (e->afmfile[0])
        FT_Attach_File(e->face, e->afmfile);    /* metrics are optional */

    /* Unicode charmap if there is one, else a symbol one */
    e->symbolmap = FALSE;
    if (FT_Select_Charmap(e->face, FT_ENCODING_UNICODE))
    {
        if (!FT_Select_Charmap(e->face, FT_ENCODING_MS_SYMBOL))
            e->symbolmap = TRUE;
    }

    strcpy(e->openfile, e->fontfile);
    e->openfacenum = e->facenum;
    set_transform(e);
    return seterr(e, OTERR_Success);
}

/* Bitmap-only fonts: choose the strike nearest to the requested height. */
static FT_Error select_strike(FT_Face face, LONG pixels)
{
    LONG i, best = 0, bestdiff = 0x7fffffff;

    if (face->num_fixed_sizes <= 0)
        return FT_Err_Invalid_Pixel_Size;
    for (i = 0; i < face->num_fixed_sizes; i++)
    {
        LONG d = face->available_sizes[i].height - pixels;

        if (d < 0)
            d = -d;
        if (d < bestdiff)
        {
            bestdiff = d;
            best = i;
        }
    }
    return FT_Select_Size(face, best);
}

/* Sets the pixel size from the point size and the chosen metric. */
static LONG set_instance(struct FT2Engine *e)
{
    FT_Face face = e->face;
    LONG ymin, ymax, pixels, xpixels;

    if (!face)
        return seterr(e, OTERR_NoFace);
    if (!e->dirty)
        return seterr(e, OTERR_Success);

    if (!FT_IS_SCALABLE(face))
    {
        if (select_strike(face, e->pointsize))
            return seterr(e, OTERR_TooSmall);
        e->upem = 0;
        e->xppem = face->size->metrics.x_ppem;
        e->yppem = face->size->metrics.y_ppem;
        e->dirty = FALSE;
        return seterr(e, OTERR_Success);
    }

    if (e->metric == METRIC_RAW_EM)
    {
        e->upem = face->units_per_EM;
        pixels = e->pointsize - 1;
    }
    else
    {
        TT_OS2 *os2 = NULL;
        LONG metric = e->metric;

        if (metric == METRIC_TYPOASCEND || metric == METRIC_USWINASCEND)
        {
            os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
            if (!os2 || os2->version == 0xFFFF)
                metric = METRIC_GLOBALBBOX;
        }

        switch (metric)
        {
            case METRIC_ASCEND:
                ymax = face->ascender;
                ymin = face->descender;
                break;
            case METRIC_TYPOASCEND:
                ymax = os2->sTypoAscender;
                ymin = os2->sTypoDescender;
                break;
            case METRIC_USWINASCEND:
                ymax = os2->usWinAscent;
                ymin = os2->usWinDescent;
                break;
            case METRIC_CUSTOMBBOX:
                ymin = (WORD)e->custombbox;
                ymax = e->custombbox >> 16;
                break;
            case METRIC_BMSIZE:
                ymin = 0;
                ymax = face->height;
                break;
            default:
                ymin = face->bbox.yMin;
                ymax = face->bbox.yMax;
                break;
        }

        /* bad or missing values: the bounding box */
        if (ymax == 0)
        {
            ymin = face->bbox.yMin;
            ymax = face->bbox.yMax;
        }
        if (ymin > 0)
            ymin = -ymin;
        if (ymax - ymin <= 0)
        {
            ymin = -(LONG)face->units_per_EM / 4;
            ymax = face->units_per_EM;
        }

        e->upem = ymax - ymin;

        /* the em in pixels such that ascent + descent = point size,
           split into whole pixels on each side of the baseline */
        pixels = face->units_per_EM * e->pointsize / e->upem;
        pixels = pixels * ymax / e->upem + pixels * -ymin / e->upem;
    }

    if (pixels < 1)
        pixels = 1;
    xpixels = (pixels * e->setfactor + 0x8000) >> 16;
    if (xpixels < 1)
        xpixels = 1;

    if (FT_Set_Pixel_Sizes(face, xpixels, pixels))
        return seterr(e, OTERR_TooSmall);

    e->xppem = face->size->metrics.x_ppem;
    e->yppem = face->size->metrics.y_ppem;
    set_transform(e);
    e->dirty = FALSE;
    return seterr(e, OTERR_Success);
}

/* glyph advance (26.6 pixels) as a fraction of the em width */
static FIXED em_fraction(struct FT2Engine *e, LONG advance)
{
    LONG em;

    if (e->upem && e->face->units_per_EM)
        em = e->xppem * e->upem / e->face->units_per_EM;
    else
        em = e->xppem;
    if (em <= 0)
        return 0;
    return (advance << 10) / em;
}

static FT_Int32 load_flags(struct FT2Engine *e, BOOL eightbit)
{
    FT_Int32 flags = FT_IS_SCALABLE(e->face) ? FT_LOAD_NO_BITMAP : FT_LOAD_DEFAULT;

    switch (e->hinter)
    {
        case HINTER_FORCEAUTO:
            flags |= FT_LOAD_FORCE_AUTOHINT |
                     (eightbit ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO);
            break;
        case HINTER_NONE:
            flags |= FT_LOAD_NO_HINTING;
            break;
        case HINTER_LIGHT:
            flags |= FT_LOAD_TARGET_LIGHT;
            break;
        default:
            /* 1-bit glyphs get hinting made for 1-bit output */
            flags |= eightbit ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO;
            break;
    }
    return flags;
}

/* emboldening strength in 26.6 pixels from a FIXED fraction of the em */
static FT_Pos embolden_pixels(LONG ppem, FIXED amount)
{
    return (FT_Pos)((amount * ppem) >> 10);
}

/* ---- glyph maps ------------------------------------------------------- */

#define FLOOR64(x)  ((x) & ~63)
#define CEIL64(x)   (((x) + 63) & ~63)

static void free_glyphmap(struct GlyphMap *gm)
{
    if (!gm)
        return;
    if (gm->glm_BitMap)
        FreeVec(gm->glm_BitMap);
    FreeVec(gm);
}

/* Copies a FreeType bitmap (mono or gray) into a glyph map buffer. */
static void copy_bitmap(const FT_Bitmap *src, UBYTE *dst, LONG pitch,
                        BOOL eightbit)
{
    ULONG x, y;

    for (y = 0; y < src->rows; y++)
    {
        const UBYTE *s = src->buffer + (LONG)y * src->pitch;
        UBYTE *d = dst + y * pitch;

        for (x = 0; x < src->width; x++)
        {
            UBYTE v;

            if (src->pixel_mode == FT_PIXEL_MODE_MONO)
                v = (s[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
            else
                v = s[x];
            if (eightbit)
                d[x] = v;
            else if (v >= 128)
                d[x >> 3] |= 0x80 >> (x & 7);
        }
    }
}

static struct GlyphMap *render_glyph(struct FT2Engine *e, BOOL eightbit)
{
    FT_Face face;
    FT_GlyphSlot slot;
    FT_UInt gi;
    struct GlyphMap *gm;
    FT_Pos xmin, ymin, xmax, ymax, rawxmin, rawymax, advance;
    LONG width, rows, pitch;
    UBYTE *buf;

    if (set_instance(e) != OTERR_Success)
        return NULL;
    face = e->face;

    gi = glyph_index(e, e->code);
    if (!gi)
    {
        seterr(e, OTERR_UnknownGlyph);
        return NULL;
    }
    if (FT_Load_Glyph(face, gi, load_flags(e, eightbit)))
    {
        seterr(e, OTERR_BadGlyph);
        return NULL;
    }
    slot = face->glyph;
    advance = slot->metrics.horiAdvance;

    if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        FT_BBox cbox;

        if (e->emboldenx || e->emboldeny)
        {
            FT_Pos sx = embolden_pixels(e->xppem, e->emboldenx);
            FT_Pos sy = embolden_pixels(e->yppem, e->emboldeny);

            FT_Outline_EmboldenXY(&slot->outline, sx, sy);
            advance += sx;
        }
        FT_Outline_Get_CBox(&slot->outline, &cbox);
        rawxmin = cbox.xMin;
        rawymax = cbox.yMax;
        xmin = FLOOR64(cbox.xMin);
        ymin = FLOOR64(cbox.yMin);
        xmax = CEIL64(cbox.xMax);
        ymax = CEIL64(cbox.yMax);
    }
    else if (slot->format == FT_GLYPH_FORMAT_BITMAP)
    {
        xmin = rawxmin = (FT_Pos)slot->bitmap_left * 64;
        ymax = rawymax = (FT_Pos)slot->bitmap_top * 64;
        xmax = xmin + (FT_Pos)slot->bitmap.width * 64;
        ymin = ymax - (FT_Pos)slot->bitmap.rows * 64;
    }
    else
    {
        seterr(e, OTERR_BadGlyph);
        return NULL;
    }

    width = (xmax - xmin) >> 6;
    rows = (ymax - ymin) >> 6;
    if (eightbit)
        pitch = (width + 3) & ~3;
    else
        pitch = ((width + 31) & ~31) >> 3;

    gm = AllocVec(sizeof(*gm), MEMF_PUBLIC | MEMF_CLEAR);
    /* one byte more: no AllocVec(0) for empty glyphs such as the space */
    buf = AllocVec(pitch * rows + 1, MEMF_PUBLIC | MEMF_CLEAR);
    if (!gm || !buf)
    {
        if (buf)
            FreeVec(buf);
        if (gm)
            FreeVec(gm);
        seterr(e, OTERR_NoMemory);
        return NULL;
    }

    if (width > 0 && rows > 0)
    {
        if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
        {
            FT_Bitmap bm;
            FT_Raster_Params params;

            memset(&bm, 0, sizeof(bm));
            memset(&params, 0, sizeof(params));
            bm.width = width;
            bm.rows = rows;
            bm.pitch = pitch;
            bm.buffer = buf;
            if (eightbit)
            {
                bm.pixel_mode = FT_PIXEL_MODE_GRAY;
                bm.num_grays = 256;
                params.flags = FT_RASTER_FLAG_AA;
            }
            else
            {
                bm.pixel_mode = FT_PIXEL_MODE_MONO;
                bm.num_grays = 2;
            }
            params.target = &bm;
            params.source = &slot->outline;
            FT_Outline_Translate(&slot->outline, -xmin, -ymin);
            if (FT_Outline_Render(e->lib, &slot->outline, &params))
            {
                FreeVec(buf);
                FreeVec(gm);
                seterr(e, OTERR_BadGlyph);
                return NULL;
            }
        }
        else
            copy_bitmap(&slot->bitmap, buf, pitch, eightbit);
    }

    gm->glm_BMModulo = pitch;
    gm->glm_BMRows = rows;
    gm->glm_BlackLeft = 0;
    gm->glm_BlackTop = 0;
    gm->glm_BlackWidth = width;
    gm->glm_BlackHeight = rows;
    gm->glm_XOrigin = -rawxmin << 10;
    gm->glm_YOrigin = rawymax << 10;
    gm->glm_X0 = -xmin >> 6;
    gm->glm_Y0 = ymax >> 6;
    gm->glm_X1 = CEIL64(slot->advance.x - rawxmin) >> 6;
    gm->glm_Y1 = CEIL64(rawymax - slot->advance.y) >> 6;
    gm->glm_Width = em_fraction(e, advance);
    gm->glm_BitMap = buf;

    /* like bullet.library: a glyph without advance is no glyph */
    if (!gm->glm_Width)
    {
        free_glyphmap(gm);
        seterr(e, OTERR_UnknownGlyph);
        return NULL;
    }
    return gm;
}

/* ---- width lists ------------------------------------------------------ */

static void free_widthlist(struct MinList *list, BOOL wide)
{
    struct MinNode *n, *next;
    ULONG size = wide ? sizeof(struct GlyphWidthEntry32)
                      : sizeof(struct GlyphWidthEntry);

    if (!list)
        return;
    for (n = list->mlh_Head; (next = n->mln_Succ); n = next)
        FreeMem(n, size);
    FreeMem(list, sizeof(struct MinList));
}

static struct MinList *width_list(struct FT2Engine *e, BOOL wide)
{
    struct MinList *list;
    ULONG c, first = e->code, last = e->code2;
    FT_Int32 flags;

    if (set_instance(e) != OTERR_Success)
        return NULL;
    if (first > last)
    {
        first = e->code2;
        last = e->code;
    }
    if (!wide && last > 0xFFFF)
        last = 0xFFFF;

    list = AllocMem(sizeof(*list), MEMF_PUBLIC | MEMF_CLEAR);
    if (!list)
    {
        seterr(e, OTERR_NoMemory);
        return NULL;
    }
    list->mlh_Head = (struct MinNode *)&list->mlh_Tail;
    list->mlh_Tail = NULL;
    list->mlh_TailPred = (struct MinNode *)&list->mlh_Head;
    flags = load_flags(e, FALSE);

    for (c = first; c <= last; c++)
    {
        FT_UInt gi = glyph_index(e, c);
        FIXED w = 0;

        if (!gi)
            continue;
        if (!FT_Load_Glyph(e->face, gi, flags))
        {
            FT_Pos adv = e->face->glyph->metrics.horiAdvance;

            if (e->emboldenx)
                adv += embolden_pixels(e->xppem, e->emboldenx);
            w = em_fraction(e, adv);
        }

        if (wide)
        {
            struct GlyphWidthEntry32 *n = AllocMem(sizeof(*n), MEMF_PUBLIC | MEMF_CLEAR);

            if (!n)
                goto nomem;
            n->gwe32_Code = c;
            n->gwe32_Width = w;
            AddTail((struct List *)list, (struct Node *)n);
        }
        else
        {
            struct GlyphWidthEntry *n = AllocMem(sizeof(*n), MEMF_PUBLIC | MEMF_CLEAR);

            if (!n)
                goto nomem;
            n->gwe_Code = c;
            n->gwe_Width = w;
            AddTail((struct List *)list, (struct Node *)n);
        }
        if (c == 0xFFFFFFFF)
            break;
    }
    return list;

nomem:
    free_widthlist(list, wide);
    seterr(e, OTERR_NoMemory);
    return NULL;
}

/* kerning of code/code2 as a fraction of the em; positive moves apart */
static FIXED kerning(struct FT2Engine *e)
{
    FT_UInt l, r;
    FT_Vector k;

    if (set_instance(e) != OTERR_Success || !e->upem)
        return 0;
    l = glyph_index(e, e->code);
    r = glyph_index(e, e->code2);
    if (!l || !r || !FT_HAS_KERNING(e->face))
        return 0;
    if (FT_Get_Kerning(e->face, l, r, FT_KERNING_UNSCALED, &k))
        return 0;
    return (-k.x << 16) / e->upem;
}

/* ---- SetInfoA --------------------------------------------------------- */

static void copy_path(char *dst, const char *src)
{
    if (!src)
        src = "";
    strncpy(dst, src, PATHMAX - 1);
    dst[PATHMAX - 1] = 0;
}

static void scan_tags(struct FT2Engine *e, struct TagItem *tags, LONG depth)
{
    struct TagItem *tstate = tags, *ti;

    while ((ti = NextTagItem(&tstate)))
    {
        ULONG data = ti->ti_Data;

        switch (ti->ti_Tag)
        {
            case OT_Spec1_FontFile:
                copy_path(e->fontfile, (const char *)data);
                e->afmfile[0] = 0;
                break;

            case OT_Spec2_DefCodePage:
                default_codepage(e);
                break;

            case OT_Spec2_CodePage:
                if (data)
                    CopyMem((APTR)data, e->codepage, sizeof(e->codepage));
                break;

            case OT_Spec3_AFMFile:
                copy_path(e->afmfile, (const char *)data);
                break;

            case OT_Spec4_Metric:
                if (e->metric != (LONG)data)
                {
                    e->metric = data;
                    e->dirty = TRUE;
                }
                break;

            case OT_Spec5_BBox:
                if (e->custombbox != (LONG)data)
                {
                    e->custombbox = data;
                    e->dirty = TRUE;
                }
                break;

            case OT_Spec6_FaceNum:
                e->facenum = data;
                break;

            case OT_Spec9_Hinter:
                e->hinter = data;
                break;

            case OT_PointHeight:
                if (e->pointsize != (LONG)(data >> 16))
                {
                    e->pointsize = data >> 16;
                    e->dirty = TRUE;
                }
                break;

            case OT_PointSize:      /* old: points * 16 */
                if (e->pointsize != (LONG)(data >> 4))
                {
                    e->pointsize = data >> 4;
                    e->dirty = TRUE;
                }
                break;

            case OT_DeviceDPI:
                e->dpi = data;
                break;

            case OT_SetFactor:
                if (!data)
                    data = 0x10000;
                if (e->setfactor != (FIXED)data)
                {
                    e->setfactor = data;
                    e->dirty = TRUE;
                }
                break;

            case OT_RotateSin:
            case OT_ShearSin:
                e->hold_sin = data;
                break;

            case OT_RotateCos:
                /* counter-clockwise baseline rotation */
                if (e->hold_sin == 0 && (FIXED)data == 0x10000)
                    e->do_rotate = FALSE;
                else
                {
                    e->do_rotate = TRUE;
                    e->rotate.xx = data;
                    e->rotate.xy = -e->hold_sin;
                    e->rotate.yx = e->hold_sin;
                    e->rotate.yy = data;
                }
                set_transform(e);
                break;

            case OT_ShearCos:
                /* clockwise slant of vertical strokes */
                if (e->hold_sin == 0 && (FIXED)data == 0x10000)
                    e->do_shear = FALSE;
                else
                {
                    e->do_shear = TRUE;
                    e->shear.xx = 0x10000;
                    e->shear.xy = data ? FT_DivFix(e->hold_sin, data) : 0;
                    e->shear.yx = 0;
                    e->shear.yy = 0x10000;
                }
                set_transform(e);
                break;

            case OT_EmboldenX:
                e->emboldenx = data;
                break;

            case OT_EmboldenY:
                e->emboldeny = data;
                break;

            case OT_GlyphCode:
            case OT_GlyphCode_32:
                e->code = data;
                break;

            case OT_GlyphCode2:
            case OT_GlyphCode2_32:
                e->code2 = data;
                break;

            case OT_OTagList:
                if (data && depth < 4)
                    scan_tags(e, (struct TagItem *)data, depth + 1);
                break;

            default:
                break;
        }
    }
}

/* ---- ObtainInfoA / ReleaseInfoA --------------------------------------- */

static ULONG obtain(struct FT2Engine *e, struct TagItem *tags)
{
    struct TagItem *tstate = tags, *ti;
    ULONG rc = OTERR_Success;

    if (open_face(e) != OTERR_Success)
        return e->error;

    while ((ti = NextTagItem(&tstate)))
    {
        APTR data = (APTR)ti->ti_Data;

        switch (ti->ti_Tag)
        {
            case OT_GlyphMap:
                if (!(*(struct GlyphMap **)data = render_glyph(e, FALSE)))
                    rc = e->error;
                break;

            case OT_GlyphMap8Bit:
            case OT_GlyphMap8Bit_Old:
                if (!(*(struct GlyphMap **)data = render_glyph(e, TRUE)))
                    rc = e->error;
                break;

            case OT_WidthList:
                if (!(*(struct MinList **)data = width_list(e, FALSE)))
                    rc = e->error;
                break;

            case OT_WidthList32:
                if (!(*(struct MinList **)data = width_list(e, TRUE)))
                    rc = e->error;
                break;

            case OT_TextKernPair:
            case OT_DesignKernPair:
                *(FIXED *)data = kerning(e);
                break;

            case OT_NumGlyphs:
                *(ULONG *)data = e->face->num_glyphs;
                break;

            default:
                rc = OTERR_UnknownTag;
                break;
        }
    }
    return rc;
}

static void release(struct TagItem *tags)
{
    struct TagItem *tstate = tags, *ti;

    while ((ti = NextTagItem(&tstate)))
    {
        switch (ti->ti_Tag)
        {
            case OT_GlyphMap:
            case OT_GlyphMap8Bit:
            case OT_GlyphMap8Bit_Old:
                free_glyphmap((struct GlyphMap *)ti->ti_Data);
                break;
            case OT_WidthList:
                free_widthlist((struct MinList *)ti->ti_Data, FALSE);
                break;
            case OT_WidthList32:
                free_widthlist((struct MinList *)ti->ti_Data, TRUE);
                break;
        }
    }
}

/* ---- library entry points --------------------------------------------- */

struct Call
{
    struct FT2Engine   *e;
    struct TagItem     *tags;
};

static ULONG do_open(APTR arg)
{
    struct FT2Engine *e = AllocVec(sizeof(*e), MEMF_PUBLIC | MEMF_CLEAR);

    (void)arg;
    if (!e)
        return 0;
    if (FT_Init_FreeType(&e->lib))
    {
        FreeVec(e);
        return 0;
    }
    InitSemaphore(&e->lock);
    e->ge.gle_Library = &FT2Base->lib;
    e->ge.gle_Name = (STRPTR)EngineName;
    e->setfactor = 0x10000;
    e->shear.xx = e->shear.yy = 0x10000;
    e->rotate = e->shear;
    e->dirty = TRUE;
    default_codepage(e);
    return (ULONG)e;
}

static ULONG do_close(APTR arg)
{
    struct FT2Engine *e = arg;

    close_face(e);
    FT_Done_FreeType(e->lib);
    FreeVec(e);
    return 0;
}

static ULONG do_setinfo(APTR arg)
{
    struct Call *c = arg;

    seterr(c->e, OTERR_Success);
    scan_tags(c->e, c->tags, 0);
    return c->e->error;
}

static ULONG do_obtain(APTR arg)
{
    struct Call *c = arg;

    return obtain(c->e, c->tags);
}

struct GlyphEngine *LIB_OpenEngine(REG(a6, struct FT2Base *base))
{
    (void)base;
    return (struct GlyphEngine *)ft2_BigStack(do_open, NULL);
}

void LIB_CloseEngine(REG(a0, struct GlyphEngine *ge))
{
    struct FT2Engine *e = (struct FT2Engine *)ge;

    if (!e)
        return;
    ObtainSemaphore(&e->lock);
    ReleaseSemaphore(&e->lock);
    ft2_BigStack(do_close, e);
}

ULONG LIB_SetInfoA(REG(a0, struct GlyphEngine *ge), REG(a1, struct TagItem *tags))
{
    struct Call c;
    ULONG rc;

    if (!ge)
        return OTERR_Failure;
    c.e = (struct FT2Engine *)ge;
    c.tags = tags;
    ObtainSemaphore(&c.e->lock);
    rc = ft2_BigStack(do_setinfo, &c);
    ReleaseSemaphore(&c.e->lock);
    return rc;
}

ULONG LIB_ObtainInfoA(REG(a0, struct GlyphEngine *ge), REG(a1, struct TagItem *tags))
{
    struct Call c;
    ULONG rc;

    if (!ge)
        return OTERR_Failure;
    c.e = (struct FT2Engine *)ge;
    c.tags = tags;
    ObtainSemaphore(&c.e->lock);
    rc = ft2_BigStack(do_obtain, &c);
    ReleaseSemaphore(&c.e->lock);
    return rc;
}

ULONG LIB_ReleaseInfoA(REG(a0, struct GlyphEngine *ge), REG(a1, struct TagItem *tags))
{
    (void)ge;
    release(tags);
    return OTERR_Success;
}
