/*
 * ft2emoji - a window full of colour emoji, drawn with freetype2.library.
 *
 *   ft2emoji [FONT <file>] [SIZE <pixels>] [LIB <name>]
 *
 * Shows every colour glyph of a COLR font (e.g. Twemoji Mozilla) page
 * by page on an RTG screen (15 bit or more). Click an emoji to see its
 * Unicode value. Keys: cursor left/right or Backspace/Space turn the
 * pages, Esc quits. Without FONT it looks for Twemoji.Mozilla.ttf next
 * to the program, in ../Fonts (as in the archive) and in FONTS:.
 *
 * Build (gcc, from the Developer drawer of the archive):
 *   m68k-amigaos-gcc -m68020 -O2 -noixemul -Iinclude -o ft2emoji Examples/ft2emoji.c
 * No CyberGraphX headers are needed: the two cybergraphics.library calls
 * go through the small assembler wrappers below.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/freetype2.h>

#include <stdio.h>
#include <string.h>

/* FreeType needs more stack than a Shell gives by default */
unsigned long __stack = 65536;

/* cybergraphics.library: the two calls this program uses */
#define RECTFMT_RGBA 1UL

ULONG cgx_ReadPixelArray(struct Library *base, APTR dst, ULONG dstx,
                         ULONG dsty, ULONG dstmod, struct RastPort *rp,
                         ULONG srcx, ULONG srcy, ULONG width, ULONG height,
                         ULONG fmt);
ULONG cgx_WritePixelArray(struct Library *base, APTR src, ULONG srcx,
                          ULONG srcy, ULONG srcmod, struct RastPort *rp,
                          ULONG dstx, ULONG dsty, ULONG width, ULONG height,
                          ULONG fmt);

/* stack arguments (C order, base first) -> A0 D0 D1 D2 A1 D3 D4 D5 D6 D7;
   ReadPixelArray is at LVO -120, WritePixelArray at -126 */
__asm__(
    "        .text\n"
    "        .even\n"
    "        .globl  _cgx_ReadPixelArray\n"
    "_cgx_ReadPixelArray:\n"
    "        movem.l %d2-%d7/%a6,-(%sp)\n"
    "        move.l  32(%sp),%a6\n"
    "        move.l  36(%sp),%a0\n"
    "        movem.l 40(%sp),%d0-%d2\n"
    "        move.l  52(%sp),%a1\n"
    "        movem.l 56(%sp),%d3-%d7\n"
    "        jsr     -120(%a6)\n"
    "        movem.l (%sp)+,%d2-%d7/%a6\n"
    "        rts\n"
    "        .globl  _cgx_WritePixelArray\n"
    "_cgx_WritePixelArray:\n"
    "        movem.l %d2-%d7/%a6,-(%sp)\n"
    "        move.l  32(%sp),%a6\n"
    "        move.l  36(%sp),%a0\n"
    "        movem.l 40(%sp),%d0-%d2\n"
    "        move.l  52(%sp),%a1\n"
    "        movem.l 56(%sp),%d3-%d7\n"
    "        jsr     -126(%a6)\n"
    "        movem.l (%sp)+,%d2-%d7/%a6\n"
    "        rts\n");

struct Library *FreeType2Base;
struct Library *CyberGfxBase;
struct Library *GadToolsBase;

static const char Template[] = "FONT/K,SIZE/K/N,LIB/K";
enum { A_FONT, A_SIZE, A_LIB, A_COUNT };

static const char *const FontPaths[] =
{
    "PROGDIR:Twemoji.Mozilla.ttf",
    "PROGDIR:/Fonts/Twemoji.Mozilla.ttf",
    "FONTS:Twemoji.Mozilla.ttf",
    "FONTS:_ttf/Twemoji.Mozilla.ttf",
    NULL
};

static STRPTR SizeLabels[] =
{
    (STRPTR)"24 px", (STRPTR)"32 px", (STRPTR)"48 px", (STRPTR)"64 px", NULL
};
static const LONG Sizes[] = { 24, 32, 48, 64 };

enum { GID_PREV = 1, GID_NEXT, GID_SIZE, GID_STATUS };

struct EmojiView
{
    struct Window *win;
    FT_Face face;
    ULONG *codes;           /* the font's colour emoji, Unicode values */
    UWORD *glyphs;          /* and their glyph indexes */
    LONG count;             /* number of them */
    LONG size;              /* pixel size */
    LONG first;             /* index of the first emoji on the page */
    LONG top;               /* first pixel row below the gadgets */
    LONG cols, rows, cell;  /* grid of the page */
    BOOL colour;            /* the font has colour glyphs */
    BOOL rtg;               /* the screen is RTG, 15 bit or more */
    struct Gadget *status;
    char text[96];
};

/* Blends a FreeType bitmap at x, y (window coordinates), clipped to the
   grid area. BGRA is premultiplied: out = src + dst * (255 - a) / 255;
   gray glyphs are drawn black. */
static void Blend(struct EmojiView *v, FT_Bitmap *bm, LONG x, LONG y)
{
    struct Window *win = v->win;
    LONG w = bm->width, h = bm->rows, i, j, sx = 0, sy = 0;
    LONG minx = win->BorderLeft, miny = v->top;
    LONG maxx = win->Width - win->BorderRight;
    LONG maxy = win->Height - win->BorderBottom;
    UBYTE *buf;

    if (x < minx)
        sx = minx - x;
    if (y < miny)
        sy = miny - y;
    if (x + w > maxx)
        w = maxx - x;
    if (y + h > maxy)
        h = maxy - y;
    if (w - sx <= 0 || h - sy <= 0)
        return;
    buf = AllocVec(w * h * 4, MEMF_ANY);
    if (!buf)
        return;
    cgx_ReadPixelArray(CyberGfxBase, buf, sx, sy, w * 4, win->RPort,
                       x + sx, y + sy, w - sx, h - sy, RECTFMT_RGBA);
    for (j = sy; j < h; j++)
    {
        const UBYTE *s = bm->buffer + j * bm->pitch;
        UBYTE *d = buf + (j * w + sx) * 4;

        for (i = sx; i < w; i++, d += 4)
        {
            ULONG a, r, g, b;

            if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                b = s[i * 4];
                g = s[i * 4 + 1];
                r = s[i * 4 + 2];
                a = s[i * 4 + 3];
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                a = s[i];
                r = g = b = 0;
            }
            else
                continue;
            d[0] = r + d[0] * (255 - a) / 255;
            d[1] = g + d[1] * (255 - a) / 255;
            d[2] = b + d[2] * (255 - a) / 255;
        }
    }
    cgx_WritePixelArray(CyberGfxBase, buf, sx, sy, w * 4, win->RPort,
                        x + sx, y + sy, w - sx, h - sy, RECTFMT_RGBA);
    FreeVec(buf);
}

static void SetStatus(struct EmojiView *v, const char *text)
{
    strncpy(v->text, text, sizeof(v->text) - 1);
    v->text[sizeof(v->text) - 1] = 0;
    GT_SetGadgetAttrs(v->status, v->win, NULL, GTTX_Text, (ULONG)v->text,
                      TAG_DONE);
}

static LONG PerPage(const struct EmojiView *v)
{
    return v->cols * v->rows;
}

static void PageStatus(struct EmojiView *v)
{
    char s[96];
    LONG pp = PerPage(v), pages = pp ? (v->count + pp - 1) / pp : 0;

    if (!v->rtg)
        strcpy(s, "Needs an RTG screen (15 bit or more)");
    else if (!v->count)
        strcpy(s, "No colour glyphs in this font");
    else
        sprintf(s, "Page %ld of %ld, %ld emoji - click one", (long)(pp ?
                v->first / pp + 1 : 1), (long)pages, (long)v->count);
    SetStatus(v, s);
}

/* the grid for the window's size; keeps the first emoji on the page */
static void Layout(struct EmojiView *v)
{
    struct Window *win = v->win;
    LONG w = win->Width - win->BorderLeft - win->BorderRight;
    LONG h = win->Height - win->BorderBottom - v->top;

    v->cell = v->size + v->size / 3;
    v->cols = w / v->cell;
    v->rows = h / v->cell;
    if (v->cols < 1)
        v->cols = 1;
    if (v->rows < 1)
        v->rows = 1;
    v->first -= v->first % PerPage(v);
}

static void DrawPage(struct EmojiView *v)
{
    struct Window *win = v->win;
    struct RastPort *rp = win->RPort;
    LONG i, n = PerPage(v);
    LONG x0 = win->BorderLeft, y0 = v->top;

    SetAPen(rp, 0);
    RectFill(rp, x0, y0, win->Width - win->BorderRight - 1,
             win->Height - win->BorderBottom - 1);
    PageStatus(v);
    if (!v->rtg)
        return;
    for (i = 0; i < n && v->first + i < v->count; i++)
    {
        LONG cx = x0 + (i % v->cols) * v->cell;
        LONG cy = y0 + (i / v->cols) * v->cell;
        FT_GlyphSlot slot = v->face->glyph;

        if (FT_Load_Glyph(v->face, v->glyphs[v->first + i],
                          FT_LOAD_COLOR | FT_LOAD_RENDER))
            continue;
        /* centre the glyph's box in the cell */
        Blend(v, &slot->bitmap,
              cx + (v->cell - (LONG)slot->bitmap.width) / 2,
              cy + (v->cell - (LONG)slot->bitmap.rows) / 2);
    }
}

static void Click(struct EmojiView *v, LONG mx, LONG my)
{
    LONG col, row, i;
    char s[96];

    if (!v->count || mx < v->win->BorderLeft || my < v->top)
        return;
    col = (mx - v->win->BorderLeft) / v->cell;
    row = (my - v->top) / v->cell;
    if (col >= v->cols || row >= v->rows)
        return;
    i = v->first + row * v->cols + col;
    if (i >= v->count)
        return;
    sprintf(s, "U+%04lX  glyph %u  (%ld of %ld)", (unsigned long)v->codes[i],
            v->glyphs[i], (long)i + 1, (long)v->count);
    SetStatus(v, s);
}

static void Turn(struct EmojiView *v, LONG pages)
{
    LONG first = v->first + pages * PerPage(v);

    if (first < 0 || first >= v->count)
        return;
    v->first = first;
    DrawPage(v);
}

/* every character of the font with colour layers (COLR); if there are
   none, every character from U+2000 on */
static BOOL CollectCodes(struct EmojiView *v)
{
    FT_ULong code;
    FT_UInt gi;
    LONG n = 0, pass;

    for (pass = 0; pass < 2; pass++)
    {
        n = 0;
        for (code = FT_Get_First_Char(v->face, &gi); gi;
             code = FT_Get_Next_Char(v->face, code, &gi))
        {
            BOOL take;

            if (v->colour)
            {
                FT_LayerIterator it;
                FT_UInt lg, ci;

                it.p = NULL;
                take = FT_Get_Color_Glyph_Layer(v->face, gi, &lg, &ci, &it);
            }
            else
                take = code >= 0x2000;
            if (!take)
                continue;
            if (pass)
            {
                v->codes[n] = code;
                v->glyphs[n] = gi;
            }
            n++;
        }
        if (!pass)
        {
            v->codes = AllocVec(n * sizeof(ULONG) + 4, MEMF_ANY);
            v->glyphs = AllocVec(n * sizeof(UWORD) + 2, MEMF_ANY);
            if (!v->codes || !v->glyphs)
                return FALSE;
        }
    }
    v->count = n;
    return TRUE;
}

static struct Gadget *MakeGadgets(struct Screen *scr, APTR vi,
                                  struct Gadget **glist, struct EmojiView *v)
{
    struct NewGadget ng;
    struct Gadget *g;
    LONG fh = scr->Font->ta_YSize, bh = fh + 6, i;
    LONG size = 0;

    for (i = 0; i < 4; i++)
        if (Sizes[i] == v->size)
            size = i;
    g = CreateContext(glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_TopEdge = scr->WBorTop + fh + 1 + 4;
    ng.ng_LeftEdge = scr->WBorLeft + 4;
    ng.ng_Height = bh;

    ng.ng_Width = 4 * fh;
    ng.ng_GadgetText = (UBYTE *)"<";
    ng.ng_GadgetID = GID_PREV;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 4;
    ng.ng_GadgetText = (UBYTE *)">";
    ng.ng_GadgetID = GID_NEXT;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 4;
    ng.ng_Width = 7 * fh;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = GID_SIZE;
    g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)SizeLabels,
                     GTCY_Active, size, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 4;
    ng.ng_Width = 32 * fh;
    ng.ng_GadgetID = GID_STATUS;
    g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Border, TRUE,
                     GTTX_Text, (ULONG)"", TAG_DONE);
    v->status = g;
    v->top = ng.ng_TopEdge + bh + 4;
    return g;
}

static const char *FindFont(const char *arg)
{
    LONG i;

    if (arg)
        return arg;
    for (i = 0; FontPaths[i]; i++)
    {
        BPTR lock = Lock((STRPTR)FontPaths[i], ACCESS_READ);

        if (lock)
        {
            UnLock(lock);
            return FontPaths[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda = NULL;
    struct EmojiView view;
    struct Screen *scr = NULL;
    struct Gadget *glist = NULL;
    APTR vi = NULL;
    FT_Library lib = NULL;
    const char *libname = "freetype2.library", *font;
    int rc = RETURN_OK;
    BOOL done = FALSE;

    memset(&view, 0, sizeof(view));
    view.size = 48;
    if (argc)
    {
        rda = ReadArgs((STRPTR)Template, args, NULL);
        if (!rda)
        {
            PrintFault(IoErr(), (STRPTR)"ft2emoji");
            return RETURN_FAIL;
        }
        if (args[A_LIB])
            libname = (const char *)args[A_LIB];
        if (args[A_SIZE])
            view.size = *(LONG *)args[A_SIZE];
        if (view.size < 8 || view.size > 256)
            view.size = 48;
    }

    FreeType2Base = OpenLibrary((STRPTR)libname, 2);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37);
    if (!FreeType2Base || !GadToolsBase)
    {
        if (argc)
            printf("ft2emoji: needs %s 2 and gadtools.library\n", libname);
        rc = RETURN_FAIL;
        goto out;
    }
    font = FindFont(args[A_FONT] ? (const char *)args[A_FONT] : NULL);
    if (!font || FT_Init_FreeType(&lib) ||
        FT_New_Face(lib, font, 0, &view.face))
    {
        if (argc)
            printf("ft2emoji: cannot open the font %s\n",
                   font ? font : "Twemoji.Mozilla.ttf");
        rc = RETURN_ERROR;
        goto out;
    }
    view.colour = FT_HAS_COLOR(view.face) != 0;
    if (FT_Set_Pixel_Sizes(view.face, 0, view.size) || !CollectCodes(&view))
    {
        rc = RETURN_ERROR;
        goto out;
    }

    scr = LockPubScreen(NULL);
    if (!scr || !(vi = GetVisualInfo(scr, TAG_DONE)) ||
        !MakeGadgets(scr, vi, &glist, &view))
    {
        rc = RETURN_ERROR;
        goto out;
    }
    view.rtg = CyberGfxBase && GetBitMapAttr(scr->RastPort.BitMap,
                                             BMA_DEPTH) >= 15;
    view.win = OpenWindowTags(NULL,
        WA_Left, 40, WA_Top, 40,
        WA_Width, scr->Width * 2 / 3 < 640 ? scr->Width : scr->Width * 2 / 3,
        WA_Height, scr->Height * 2 / 3 < 400 ? scr->Height : scr->Height * 2 / 3,
        WA_MinWidth, 200, WA_MinHeight, view.top + 80,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_Title, (ULONG)"ft2emoji",
        WA_ScreenTitle, (ULONG)"ft2emoji - colour emoji with freetype2.library",
        WA_PubScreen, (ULONG)scr,
        WA_Gadgets, (ULONG)glist,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS |
                  IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW |
                  BUTTONIDCMP | CYCLEIDCMP,
        TAG_DONE);
    UnlockPubScreen(NULL, scr);
    scr = NULL;
    if (!view.win)
    {
        rc = RETURN_ERROR;
        goto out;
    }
    GT_RefreshWindow(view.win, NULL);
    Layout(&view);
    DrawPage(&view);

    while (!done)
    {
        struct IntuiMessage *msg;

        WaitPort(view.win->UserPort);
        while ((msg = GT_GetIMsg(view.win->UserPort)))
        {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            WORD mx = msg->MouseX, my = msg->MouseY;
            struct Gadget *gad = (struct Gadget *)msg->IAddress;

            GT_ReplyIMsg(msg);
            switch (cls)
            {
            case IDCMP_CLOSEWINDOW:
                done = TRUE;
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(view.win);
                GT_EndRefresh(view.win, TRUE);
                break;
            case IDCMP_NEWSIZE:
                Layout(&view);
                DrawPage(&view);
                break;
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTDOWN)
                    Click(&view, mx, my);
                break;
            case IDCMP_RAWKEY:
                if (code == 0x4F)           /* cursor left */
                    Turn(&view, -1);
                else if (code == 0x4E)      /* cursor right */
                    Turn(&view, 1);
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27)
                    done = TRUE;
                else if (code == ' ')
                    Turn(&view, 1);
                else if (code == 8)
                    Turn(&view, -1);
                break;
            case IDCMP_GADGETUP:
                if (gad->GadgetID == GID_PREV)
                    Turn(&view, -1);
                else if (gad->GadgetID == GID_NEXT)
                    Turn(&view, 1);
                else if (gad->GadgetID == GID_SIZE && code < 4)
                {
                    view.size = Sizes[code];
                    FT_Set_Pixel_Sizes(view.face, 0, view.size);
                    Layout(&view);
                    DrawPage(&view);
                }
                break;
            }
        }
    }

out:
    if (view.win)
        CloseWindow(view.win);
    if (glist)
        FreeGadgets(glist);
    if (vi)
        FreeVisualInfo(vi);
    if (scr)
        UnlockPubScreen(NULL, scr);
    if (view.face)
        FT_Done_Face(view.face);
    if (lib)
        FT_Done_FreeType(lib);
    if (view.codes)
        FreeVec(view.codes);
    if (view.glyphs)
        FreeVec(view.glyphs);
    if (GadToolsBase)
        CloseLibrary(GadToolsBase);
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    if (FreeType2Base)
        CloseLibrary(FreeType2Base);
    if (rda)
        FreeArgs(rda);
    return rc;
}
