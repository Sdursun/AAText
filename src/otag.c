/*
 * AAText - .otag file reader. See otag.h.
 */

#include <exec/types.h>
#include <utility/tagitem.h>

#include "otag.h"

/* Big-endian reads; .otag data is not necessarily aligned. */
static ULONG GetLong(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static UWORD GetWord(const UBYTE *p)
{
    return (UWORD)((p[0] << 8) | p[1]);
}

/* NUL-terminated string at offset, fully inside the buffer? */
static const char *StringAt(const UBYTE *buf, ULONG len, ULONG off)
{
    ULONG i;

    if (off >= len)
        return NULL;
    for (i = off; i < len; i++)
        if (buf[i] == 0)
            return i > off ? (const char *)buf + off : NULL;
    return NULL;
}

static int ToLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static BOOL HasFontSuffix(const char *s)
{
    static const char *const suffixes[] = { ".ttf", ".ttc", ".otf" };
    LONG len = 0, i, j;

    while (s[len])
        len++;
    if (len < 5)
        return FALSE;
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 4 && ToLower(s[len - 4 + j]) == suffixes[i][j]; j++)
            ;
        if (j == 4)
            return TRUE;
    }
    return FALSE;
}

BOOL aa_ParseOTag(const UBYTE *buf, ULONG len, struct AAOTagInfo *info)
{
    const char *fallback = NULL;
    ULONG pos;

    info->fontfile = NULL;
    info->engine = NULL;
    info->facenum = 0;
    info->hascodepage = FALSE;

    if (len < 8 || GetLong(buf) != OT_FileIdent)
        return FALSE;

    for (pos = 0; pos + 8 <= len; pos += 8)
    {
        ULONG tag = GetLong(buf + pos);
        ULONG data = GetLong(buf + pos + 4);

        if (tag == TAG_DONE)
            break;

        switch (tag)
        {
            case OT_Spec1_FontFile:
                info->fontfile = StringAt(buf, len, data);
                break;

            case OT_Engine:
                info->engine = StringAt(buf, len, data);
                break;

            case OT_Spec6_FaceNum:
                info->facenum = (LONG)data;
                break;

            case OT_Spec2_CodePage:
                if (data + 512 <= len)
                {
                    LONG i;

                    for (i = 0; i < 256; i++)
                        info->codepage[i] = GetWord(buf + data + i * 2);
                    info->hascodepage = TRUE;
                }
                break;

            default:
                /* other engines: any indirect string naming a font file */
                if ((tag & TAG_USER) && (tag & OT_Indirect) && !fallback)
                {
                    const char *s = StringAt(buf, len, data);

                    if (s && HasFontSuffix(s))
                        fallback = s;
                }
                break;
        }
    }

    if (!info->fontfile)
        info->fontfile = fallback;
    return info->fontfile != NULL;
}

static void PutLong(UBYTE *p, ULONG v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

ULONG aa_OTagSetFontFile(const UBYTE *buf, ULONG len, const char *fontfile,
                         UBYTE *out, ULONG outmax)
{
    static struct AAOTagInfo info;
    ULONG pos, off, newoff, newlen, n, i;

    if (!aa_ParseOTag(buf, len, &info))
        return 0;
    off = (const UBYTE *)info.fontfile - buf;

    /* the tag whose indirect data is the old path */
    for (pos = 0; pos + 8 <= len; pos += 8)
    {
        ULONG tag = GetLong(buf + pos);

        if (tag == TAG_DONE)
            return 0;
        if ((tag & TAG_USER) && (tag & OT_Indirect) &&
            GetLong(buf + pos + 4) == off)
            break;
    }
    if (pos + 8 > len)
        return 0;

    for (n = 0; fontfile[n]; n++)
        ;
    newoff = (len + 3) & ~3UL;          /* keep indirect data aligned */
    newlen = newoff + n + 1;
    if (newlen > outmax)
        return 0;

    for (i = 0; i < len; i++)
        out[i] = buf[i];
    for (; i < newoff; i++)
        out[i] = 0;
    for (i = 0; i <= n; i++)
        out[newoff + i] = fontfile[i];
    PutLong(out + pos + 4, newoff);
    PutLong(out + 4, newlen);           /* OT_FileIdent: size of the file */
    return newlen;
}
