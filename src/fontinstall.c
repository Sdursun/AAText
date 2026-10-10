/*
 * AATextManager - installing an outline font (.font + .otag + font file).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "fontinstall.h"
#include "otagfile.h"
#include "charsets.h"

#define COPY_BUF  (32 * 1024)

void aa_InstallDefaults(struct AAInstall *in)
{
    static const UWORD def[] = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 24 };
    LONG i;

    memset(in, 0, sizeof(*in));
    in->fonts = "FONTS:";
    in->engine = "aatext";
    in->charset = -1;
    for (i = 0; i < (LONG)(sizeof(def) / sizeof(def[0])); i++)
        in->sizes[i] = def[i];
    in->numsizes = i;
}

/* dir + name into buf ("FONTS:" + "x" or "Work:Fonts/" + "x") */
static void Join(char *buf, LONG max, const char *dir, const char *name)
{
    strncpy(buf, dir, max - 1);
    buf[max - 1] = 0;
    AddPart((STRPTR)buf, (CONST_STRPTR)name, max);
}

static BOOL Exists(const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, ACCESS_READ);

    if (l)
        UnLock(l);
    return l != 0;
}

/* Size of a file, -1 if missing */
static LONG FileSize(const char *path)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR l = Lock((CONST_STRPTR)path, ACCESS_READ);
    LONG size = -1;

    if (l && fib && Examine(l, fib))
        size = fib->fib_Size;
    if (l)
        UnLock(l);
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    return size;
}

/* path -> path.bak, an older .bak is replaced */
static BOOL Backup(const char *path)
{
    char bak[300];

    if (!Exists(path))
        return TRUE;
    strncpy(bak, path, sizeof(bak) - 5);
    bak[sizeof(bak) - 5] = 0;
    strcat(bak, ".bak");
    DeleteFile((CONST_STRPTR)bak);
    return Rename((CONST_STRPTR)path, (CONST_STRPTR)bak) != 0;
}

static BOOL CopyFile(const char *from, const char *to)
{
    UBYTE *buf = AllocVec(COPY_BUF, MEMF_ANY);
    BPTR in = 0, out = 0;
    LONG n;
    BOOL ok = FALSE;

    if (buf && (in = Open((CONST_STRPTR)from, MODE_OLDFILE)) &&
        (out = Open((CONST_STRPTR)to, MODE_NEWFILE)))
    {
        ok = TRUE;
        while ((n = Read(in, buf, COPY_BUF)) > 0)
        {
            if (Write(out, buf, n) != n)
            {
                ok = FALSE;
                break;
            }
        }
        if (n < 0)
            ok = FALSE;
    }
    if (out && !Close(out))
        ok = FALSE;
    if (in)
        Close(in);
    if (buf)
        FreeVec(buf);
    if (out && !ok)
        DeleteFile((CONST_STRPTR)to);
    return ok;
}

static BOOL WriteFile(const char *path, const void *data, LONG len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    BOOL ok;

    if (!fh)
        return FALSE;
    ok = Write(fh, (APTR)data, len) == len;
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

/*
 * Tags in FTManager's order and with its values; added: the code page,
 * and the em height from the hhea ascender/descender (OT_Spec4_Metric)
 * instead of FTManager's bounding box: in fonts with a few very tall or
 * deep glyphs (Calibri) the bounding box made the letters small and put
 * the baseline high in the cell.
 * Not verified: what FTManager writes for OT_InhibitAlgoStyle of bold
 * and italic faces (its regular face has 3: underline and bold).
 */
BOOL aa_BuildOTag(const struct AAInstall *in, struct AAOTagFile *f)
{
    const struct AAFontInfo *fi = &in->info;
    UWORD sizes[AA_INSTALL_MAXSIZES + 1];
    UWORD page[256];
    LONG i;
    BOOL ok;

    sizes[0] = in->numsizes;
    for (i = 0; i < in->numsizes; i++)
        sizes[i + 1] = in->sizes[i];
    aa_OTagInit(f);
    ok = aa_OTagSetString(f, OT_Engine, in->engine) &&
         aa_OTagSetString(f, OT_Family, fi->family) &&
         aa_OTagSet(f, OT_YSizeFactor, 0x00010001) &&
         aa_OTagSet(f, OT_SpaceWidth, fi->spacewidth) &&
         aa_OTagSet(f, OT_IsFixed, fi->fixed) &&
         aa_OTagSet(f, OT_SerifFlag, fi->serif) &&
         aa_OTagSet(f, OT_StemWeight, fi->stemweight) &&
         aa_OTagSet(f, OT_SlantStyle, fi->slantstyle) &&
         aa_OTagSet(f, OT_HorizStyle, fi->horizstyle) &&
         aa_OTagSet(f, OT_SpaceFactor, 0x10000) &&
         aa_OTagSet(f, OT_InhibitAlgoStyle,
                    FSF_UNDERLINED | FSF_BOLD | (fi->italic ? FSF_ITALIC : 0)) &&
         aa_OTagSet(f, OT_SpecCount, in->charset >= 0 ? 5 : 4) &&
         aa_OTagSetString(f, OT_Spec1_FontFile, in->fontfile);
    if (ok && in->charset >= 0 && in->charset < AA_NUM_CHARSETS)
    {
        aa_CharsetPage(in->charset, page);
        ok = aa_OTagSetData(f, OT_Spec2_CodePage, page, sizeof(page));
    }
    return ok &&
           aa_OTagSetString(f, OT_Spec3_AFMFile, "") &&
           aa_OTagSet(f, OT_Spec4_Metric, OT_METRIC_ASCEND) &&
           aa_OTagSet(f, OT_Spec6_FaceNum, in->face) &&
           aa_OTagSetData(f, OT_AvailSizes, sizes,
                          (in->numsizes + 1) * sizeof(UWORD));
}

LONG aa_InstallFont(struct AAInstall *in)
{
    static struct AAOTagFile otag;
    static const UBYTE fontcontents[4] = { 0x0F, 0x03, 0x00, 0x00 };
    char ttfdir[256], dest[300], fontpath[300], otagpath[300], leaf[40];
    const char *file = (const char *)FilePart((CONST_STRPTR)in->source);
    BPTR a, b;
    LONG size;
    BOOL same = FALSE;

    in->replaced = in->copied = FALSE;
    if (!aa_GetFontInfo(in->source, in->face, &in->info))
        return AA_INSTALL_NOTFONT;
    aa_FontBaseName(&in->info, in->name, 26);   /* 30 - ".otag" */
    if (!in->name[0])
        strcpy(in->name, "font");

    strcpy(leaf, in->name);
    strcat(leaf, ".font");
    Join(fontpath, sizeof(fontpath), in->fonts, leaf);
    strcpy(leaf, in->name);
    strcat(leaf, ".otag");
    Join(otagpath, sizeof(otagpath), in->fonts, leaf);
    if ((Exists(fontpath) || Exists(otagpath)) && !in->overwrite)
        return AA_INSTALL_EXISTS;

    /* the font file: <fonts>_ttf/<file> */
    Join(ttfdir, sizeof(ttfdir), in->fonts, "_ttf");
    if (!Exists(ttfdir))
    {
        BPTR l = CreateDir((CONST_STRPTR)ttfdir);

        if (!l)
            return AA_INSTALL_COPYFAIL;
        UnLock(l);
    }
    Join(dest, sizeof(dest), ttfdir, file);
    Join(in->fontfile, sizeof(in->fontfile), ttfdir, file);

    a = Lock((CONST_STRPTR)in->source, ACCESS_READ);
    b = Lock((CONST_STRPTR)dest, ACCESS_READ);
    if (a && b)
        same = SameLock(a, b) == LOCK_SAME;
    if (a)
        UnLock(a);
    if (b)
        UnLock(b);
    if (!same)
    {
        size = FileSize(dest);
        if (size >= 0 && size == FileSize(in->source))
            same = TRUE;            /* taken as the same file */
        else if (size >= 0 && !in->overwrite)
            return AA_INSTALL_EXISTS;
    }
    if (!same)
    {
        if (!Backup(dest) || !CopyFile(in->source, dest))
            return AA_INSTALL_COPYFAIL;
        in->copied = TRUE;
    }

    if (!aa_BuildOTag(in, &otag))
        return AA_INSTALL_WRITEFAIL;
    if (Exists(fontpath) || Exists(otagpath))
    {
        if (!Backup(fontpath) || !Backup(otagpath))
            return AA_INSTALL_WRITEFAIL;
        in->replaced = TRUE;
    }
    if (!aa_OTagWriteFile(&otag, otagpath) ||
        !WriteFile(fontpath, fontcontents, sizeof(fontcontents)))
        return AA_INSTALL_WRITEFAIL;
    return AA_INSTALL_OK;
}
