#ifndef AATEXT_OTAG_H
#define AATEXT_OTAG_H

/*
 * Reader for diskfont .otag files (outline font descriptions written by
 * font managers next to the .font file). A .otag file is a TagItem array;
 * tags with OT_Indirect set store their data at a file offset.
 * Pure function, no DOS calls, so it can be tested on the host.
 */

#include <exec/types.h>
#include <utility/tagitem.h>

#define OT_Level1           (TAG_USER | 0x1000)
#define OT_Indirect         0x8000
#define OT_FileIdent        (OT_Level1 | 0x01)
#define OT_Engine           (OT_Level1 | OT_Indirect | 0x02)
#define OT_Spec1_FontFile   (OT_Level1 | OT_Indirect | 0x101)
#define OT_Spec2_CodePage   (OT_Level1 | OT_Indirect | 0x102)
#define OT_Spec6_FaceNum    (OT_Level1 | 0x106)

struct AAOTagInfo
{
    const char *fontfile;       /* points into the buffer, or NULL */
    const char *engine;         /* points into the buffer, or NULL */
    LONG  facenum;
    BOOL  hascodepage;
    UWORD codepage[256];        /* character code -> Unicode */
};

/*
 * Parse a .otag file held in buf (len bytes, must stay valid while the
 * returned strings are used). The font file is taken from
 * OT_Spec1_FontFile; if that is missing, any indirect string ending in
 * .ttf/.ttc/.otf is used. Returns TRUE if a font file was found.
 */
BOOL aa_ParseOTag(const UBYTE *buf, ULONG len, struct AAOTagInfo *info);

/*
 * Build a copy of the .otag in buf with another font file path in out
 * (room for outmax bytes). Everything else is kept: the new path is
 * appended, the tag that named the old one points to it, and
 * OT_FileIdent (the file size) is updated. Returns the new length, or
 * 0 if buf names no font file or out is too small.
 */
ULONG aa_OTagSetFontFile(const UBYTE *buf, ULONG len, const char *fontfile,
                         UBYTE *out, ULONG outmax);

#endif
