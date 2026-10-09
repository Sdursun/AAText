/*
 * AAText - .otag files as an editable tag list. See otagfile.h.
 * Used by AATextManager and AATextPrefs (diagnostics), not by AAText.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "otagfile.h"

#define MAX_FILE (64 * 1024)

static ULONG GetLong(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static void PutLong(UBYTE *p, ULONG v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

void aa_OTagInit(struct AAOTagFile *f)
{
    f->count = 1;
    f->poolused = 0;
    f->items[0].tag = OT_FileIdent;
    f->items[0].data = 0;
    f->items[0].ind = NULL;
    f->items[0].indlen = 0;
}

/* Copy len bytes into the pool; NULL if it is full. */
static UBYTE *PoolCopy(struct AAOTagFile *f, const void *data, ULONG len)
{
    UBYTE *p;

    if (f->poolused + len > AA_OTAG_POOL)
        return NULL;
    p = f->pool + f->poolused;
    memcpy(p, data, len);
    f->poolused += len;
    return p;
}

BOOL aa_OTagLoad(struct AAOTagFile *f, const UBYTE *buf, ULONG len)
{
    ULONG pos, tabend, i, j;

    aa_OTagInit(f);
    f->count = 0;
    if (len < 8 || GetLong(buf) != OT_FileIdent)
        return FALSE;

    /* the tag table */
    for (pos = 0; pos + 4 <= len; pos += 8)
    {
        struct AAOTagItem *it;
        ULONG tag = GetLong(buf + pos);

        if (tag == TAG_DONE)
            break;
        if (pos + 8 > len || f->count == AA_OTAG_MAXTAGS)
            return FALSE;
        it = &f->items[f->count++];
        it->tag = tag;
        it->data = GetLong(buf + pos + 4);
        it->ind = NULL;
        it->indlen = 0;
    }
    /* FTManager ends the table with a 4-byte TAG_DONE */
    tabend = pos + 4 <= len ? pos + 4 : len;

    /* indirect data: from its offset up to the next one or the end */
    for (i = 0; i < (ULONG)f->count; i++)
    {
        struct AAOTagItem *it = &f->items[i];
        ULONG end = len;

        if (!(it->tag & OT_Indirect) || !(it->tag & TAG_USER))
            continue;
        if (it->data < tabend || it->data > len)
            return FALSE;
        for (j = 0; j < (ULONG)f->count; j++)
        {
            const struct AAOTagItem *o = &f->items[j];

            if ((o->tag & OT_Indirect) && (o->tag & TAG_USER) &&
                o->data > it->data && o->data < end)
                end = o->data;
        }
        it->indlen = end - it->data;
        it->ind = PoolCopy(f, buf + it->data, it->indlen);
        if (!it->ind)
            return FALSE;
    }
    return f->count > 0;
}

ULONG aa_OTagSave(const struct AAOTagFile *f, UBYTE *out, ULONG max)
{
    ULONG size = (f->count + 1) * 8, off, i;

    for (i = 0; i < (ULONG)f->count; i++)
        if (f->items[i].ind)
            size = ((size + 1) & ~1UL) + f->items[i].indlen;
    if (size > max)
        return 0;

    memset(out, 0, size);
    off = (f->count + 1) * 8;
    for (i = 0; i < (ULONG)f->count; i++)
    {
        const struct AAOTagItem *it = &f->items[i];

        PutLong(out + i * 8, it->tag);
        if (it->ind)
        {
            off = (off + 1) & ~1UL;     /* word data must be aligned */
            memcpy(out + off, it->ind, it->indlen);
            PutLong(out + i * 8 + 4, off);
            off += it->indlen;
        }
        else
            PutLong(out + i * 8 + 4,
                    it->tag == OT_FileIdent ? size : it->data);
    }
    PutLong(out + f->count * 8, TAG_DONE);
    return size;
}

struct AAOTagItem *aa_OTagFind(struct AAOTagFile *f, ULONG tag)
{
    LONG i;

    for (i = 0; i < f->count; i++)
        if (f->items[i].tag == tag)
            return &f->items[i];
    return NULL;
}

static struct AAOTagItem *FindOrAdd(struct AAOTagFile *f, ULONG tag)
{
    struct AAOTagItem *it = aa_OTagFind(f, tag);

    if (it || f->count == AA_OTAG_MAXTAGS)
        return it;
    it = &f->items[f->count++];
    it->tag = tag;
    it->data = 0;
    it->ind = NULL;
    it->indlen = 0;
    return it;
}

BOOL aa_OTagSet(struct AAOTagFile *f, ULONG tag, ULONG data)
{
    struct AAOTagItem *it = FindOrAdd(f, tag);

    if (!it)
        return FALSE;
    it->data = data;
    return TRUE;
}

/*
 * The old data stays in the pool unused; a .otag is small and the pool
 * is only filled again when the file is loaded anew.
 */
BOOL aa_OTagSetData(struct AAOTagFile *f, ULONG tag, const void *data,
                    ULONG len)
{
    struct AAOTagItem *it = FindOrAdd(f, tag);
    UBYTE *p;

    if (!it || !(p = PoolCopy(f, data, len)))
        return FALSE;
    it->ind = p;
    it->indlen = len;
    return TRUE;
}

BOOL aa_OTagSetString(struct AAOTagFile *f, ULONG tag, const char *s)
{
    return aa_OTagSetData(f, tag, s, strlen(s) + 1);
}

void aa_OTagRemove(struct AAOTagFile *f, ULONG tag)
{
    LONG i;

    for (i = 0; i < f->count; i++)
        if (f->items[i].tag == tag)
        {
            memmove(&f->items[i], &f->items[i + 1],
                    (f->count - i - 1) * sizeof(f->items[0]));
            f->count--;
            return;
        }
}

BOOL aa_OTagReadFile(struct AAOTagFile *f, const char *path)
{
    UBYTE *buf = AllocVec(MAX_FILE, MEMF_ANY);
    BPTR fh;
    LONG len = -1;
    BOOL ok;

    if (!buf)
        return FALSE;
    if ((fh = Open((CONST_STRPTR)path, MODE_OLDFILE)))
    {
        len = Read(fh, buf, MAX_FILE);
        Close(fh);
    }
    ok = len > 0 && len < MAX_FILE && aa_OTagLoad(f, buf, len);
    FreeVec(buf);
    return ok;
}

BOOL aa_OTagWriteFile(const struct AAOTagFile *f, const char *path)
{
    UBYTE *buf = AllocVec(MAX_FILE, MEMF_ANY);
    ULONG len;
    BPTR fh;
    BOOL ok = FALSE;

    if (!buf)
        return FALSE;
    len = aa_OTagSave(f, buf, MAX_FILE);
    if (len && (fh = Open((CONST_STRPTR)path, MODE_NEWFILE)))
    {
        ok = Write(fh, buf, len) == (LONG)len;
        if (!Close(fh))
            ok = FALSE;
    }
    FreeVec(buf);
    return ok;
}
