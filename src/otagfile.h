#ifndef AATEXT_OTAGFILE_H
#define AATEXT_OTAGFILE_H

/*
 * .otag files as an editable tag list, for AATextManager and the
 * diagnostics: load a .otag, change, add or remove tags, write it again.
 * (AAText itself only reads them, with the small parser in otag.c.)
 *
 * A .otag is a TagItem array ended by TAG_DONE; a tag with OT_Indirect
 * set has its data elsewhere in the file, ti_Data being the offset, and
 * OT_FileIdent holds the size of the file. The length of indirect data
 * is not stored: it runs to the next indirect data (or the end of the
 * file), so unknown tags are kept byte for byte.
 */

#include <exec/types.h>

#include "otag.h"

/* tags of diskfont/diskfonttag.h and the FTManager .otag */
#define OT_Level2            (TAG_USER | 0x2000)
#define OT_Family            (OT_Level1 | OT_Indirect | 0x03)
#define OT_YSizeFactor       (OT_Level1 | 0x11)
#define OT_SpaceWidth        (OT_Level2 | 0x12)
#define OT_IsFixed           (OT_Level2 | 0x13)
#define OT_SerifFlag         (OT_Level1 | 0x14)
#define OT_StemWeight        (OT_Level1 | 0x15)
#define OT_SlantStyle        (OT_Level1 | 0x16)
#define OT_HorizStyle        (OT_Level1 | 0x17)
#define OT_SpaceFactor       (OT_Level2 | 0x18)
#define OT_InhibitAlgoStyle  (OT_Level2 | 0x19)
#define OT_AvailSizes        (OT_Level1 | OT_Indirect | 0x20)
#define OT_SpecCount         (OT_Level1 | 0x100)
#define OT_Spec3_AFMFile     (OT_Level1 | OT_Indirect | 0x103)
#define OT_Spec4_MetricsFont (OT_Level1 | 0x104)

#define AA_OTAG_MAXTAGS  64
#define AA_OTAG_POOL     8192   /* indirect data of all tags */

struct AAOTagItem
{
    ULONG  tag;
    ULONG  data;            /* direct value; for indirect tags unused */
    UBYTE *ind;             /* indirect data in the pool, or NULL */
    ULONG  indlen;
};

struct AAOTagFile
{
    LONG   count;
    struct AAOTagItem items[AA_OTAG_MAXTAGS];
    ULONG  poolused;
    UBYTE  pool[AA_OTAG_POOL];
};

/* Empty list holding only OT_FileIdent (filled in when writing). */
void aa_OTagInit(struct AAOTagFile *f);

/* Parse a .otag held in buf. FALSE if it is not one or too big. */
BOOL aa_OTagLoad(struct AAOTagFile *f, const UBYTE *buf, ULONG len);

/*
 * Write the list into out (room for max bytes): tags, TAG_DONE, then the
 * indirect data, each at an even offset; OT_FileIdent gets the size.
 * Returns the size, 0 if out is too small.
 */
ULONG aa_OTagSave(const struct AAOTagFile *f, UBYTE *out, ULONG max);

/* The item of tag, or NULL. */
struct AAOTagItem *aa_OTagFind(struct AAOTagFile *f, ULONG tag);

/* Set a direct tag (added at the end if missing). FALSE if full. */
BOOL aa_OTagSet(struct AAOTagFile *f, ULONG tag, ULONG data);

/* Set an indirect tag's data (copied). FALSE if full. */
BOOL aa_OTagSetData(struct AAOTagFile *f, ULONG tag, const void *data,
                    ULONG len);

/* Same for a NUL-terminated string. */
BOOL aa_OTagSetString(struct AAOTagFile *f, ULONG tag, const char *s);

/* Remove a tag if present. */
void aa_OTagRemove(struct AAOTagFile *f, ULONG tag);

/* Read or write a .otag file (DOS). */
BOOL aa_OTagReadFile(struct AAOTagFile *f, const char *path);
BOOL aa_OTagWriteFile(const struct AAOTagFile *f, const char *path);

#endif
