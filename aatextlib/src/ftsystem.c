/*
 * aatext.library - FreeType system interface for AmigaOS: memory, file
 * streams and getenv().
 *
 * Memory: every FT_Memory (one per FT_Library, see FT_Init_FreeType) has
 * its own exec memory pool. Pools are not task safe; FreeType's rule that
 * an FT_Library and its objects are used by one task at a time covers it.
 *
 * Streams: AmigaDOS file handles with a read buffer, so that FreeType's
 * many small reads do not each become a DOS packet.
 */

#include <ft2build.h>
#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>
#include <freetype/fttypes.h>

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "lib.h"

/* ---- memory ----------------------------------------------------------- */

#define FT2_PUDDLE  (16 * 1024)

/* Each block stores its size in front of it, like AllocVec(). */
static void *ft_alloc(FT_Memory memory, long size)
{
    ULONG *p = AllocPooled(memory->user, size + sizeof(ULONG));

    if (!p)
        return NULL;
    *p = size + sizeof(ULONG);
    return p + 1;
}

static void ft_free(FT_Memory memory, void *block)
{
    ULONG *p;

    if (!block)
        return;
    p = (ULONG *)block - 1;
    FreePooled(memory->user, p, *p);
}

static void *ft_realloc(FT_Memory memory, long cur_size, long new_size,
                        void *block)
{
    void *nb;

    if (!block)
        return ft_alloc(memory, new_size);
    if (new_size <= cur_size && new_size + 64 > cur_size)
        return block;   /* small shrink: keep the block */
    nb = ft_alloc(memory, new_size);
    if (nb)
    {
        CopyMem(block, nb, cur_size < new_size ? cur_size : new_size);
        ft_free(memory, block);
    }
    return nb;
}

FT_BASE_DEF(FT_Memory) FT_New_Memory(void)
{
    FT_Memory memory = AllocVec(sizeof(*memory), MEMF_ANY | MEMF_CLEAR);

    if (!memory)
        return NULL;
    memory->user = CreatePool(MEMF_ANY, FT2_PUDDLE, FT2_PUDDLE / 2);
    if (!memory->user)
    {
        FreeVec(memory);
        return NULL;
    }
    memory->alloc = ft_alloc;
    memory->realloc = ft_realloc;
    memory->free = ft_free;
    return memory;
}

FT_BASE_DEF(void) FT_Done_Memory(FT_Memory memory)
{
    if (!memory)
        return;
    DeletePool(memory->user);
    FreeVec(memory);
}

/* ---- streams ---------------------------------------------------------- */

#define IOBUF_SIZE  1024

struct SysFile
{
    BPTR  file;
    ULONG filepos;      /* position of the DOS file handle */
    ULONG bufstart;     /* file offset of buf[0] */
    ULONG buflen;       /* valid bytes in buf */
    UBYTE buf[IOBUF_SIZE];
};

#define STREAM_FILE(stream)  ((struct SysFile *)(stream)->descriptor.pointer)

static void ft_amiga_stream_close(FT_Stream stream)
{
    struct SysFile *sf = STREAM_FILE(stream);

    if (sf)
    {
        Close(sf->file);
        FreeVec(sf);
    }
    stream->descriptor.pointer = NULL;
    stream->size = 0;
    stream->base = NULL;
}

/* Reads count bytes at offset straight from the file; returns bytes read. */
static ULONG sf_read(struct SysFile *sf, ULONG offset, UBYTE *dst, ULONG count)
{
    LONG n;

    if (sf->filepos != offset)
    {
        if (Seek(sf->file, offset, OFFSET_BEGINNING) < 0)
        {
            sf->filepos = (ULONG)-1;
            return 0;
        }
        sf->filepos = offset;
    }
    n = Read(sf->file, dst, count);
    if (n <= 0)
    {
        sf->filepos = (ULONG)-1;    /* unknown, seek next time */
        return 0;
    }
    sf->filepos += n;
    return (ULONG)n;
}

static unsigned long ft_amiga_stream_io(FT_Stream stream, unsigned long offset,
                                        unsigned char *buffer,
                                        unsigned long count)
{
    struct SysFile *sf = STREAM_FILE(stream);
    ULONG done = 0;

    /* count == 0 is a seek; FreeType only checks the offset */
    if (count == 0)
        return offset <= stream->size ? 0 : 1;

    while (done < count)
    {
        ULONG pos = offset + done;
        ULONG want = count - done;

        if (pos >= sf->bufstart && pos < sf->bufstart + sf->buflen)
        {
            ULONG avail = sf->bufstart + sf->buflen - pos;
            ULONG n = want < avail ? want : avail;

            CopyMem(sf->buf + (pos - sf->bufstart), buffer + done, n);
            done += n;
        }
        else if (want >= IOBUF_SIZE)
        {
            /* big read: straight into the caller's buffer */
            ULONG n = sf_read(sf, pos, buffer + done, want);

            if (!n)
                break;
            done += n;
        }
        else
        {
            sf->buflen = sf_read(sf, pos, sf->buf, IOBUF_SIZE);
            sf->bufstart = pos;
            if (!sf->buflen)
                break;
        }
    }
    return done;
}

FT_BASE_DEF(FT_Error) FT_Stream_Open(FT_Stream stream, const char *filepathname)
{
    struct SysFile *sf;
    LONG size;

    if (!stream)
        return FT_THROW(Invalid_Stream_Handle);

    sf = AllocVec(sizeof(*sf), MEMF_ANY);
    if (!sf)
        return FT_THROW(Out_Of_Memory);

    sf->file = Open((STRPTR)filepathname, MODE_OLDFILE);
    if (!sf->file)
    {
        FreeVec(sf);
        FT_ERROR(("FT_Stream_Open: could not open `%s'\n", filepathname));
        return FT_THROW(Cannot_Open_Resource);
    }

    /* the size: Seek() returns the old position */
    Seek(sf->file, 0, OFFSET_END);
    size = Seek(sf->file, 0, OFFSET_BEGINNING);
    if (size <= 0)
    {
        Close(sf->file);
        FreeVec(sf);
        FT_ERROR(("FT_Stream_Open: `%s' is empty or unreadable\n", filepathname));
        return FT_THROW(Cannot_Open_Stream);
    }

    sf->filepos = 0;
    sf->bufstart = 0;
    sf->buflen = 0;

    stream->size = (unsigned long)size;
    stream->pos = 0;
    stream->base = NULL;
    stream->descriptor.pointer = sf;
    stream->pathname.pointer = (char *)filepathname;
    stream->read = ft_amiga_stream_io;
    stream->close = ft_amiga_stream_close;

    FT_TRACE1(("FT_Stream_Open: opened `%s' (%ld bytes)\n",
               filepathname, stream->size));
    return FT_Err_Ok;
}

/* ---- getenv ----------------------------------------------------------- */

/* FreeType reads FREETYPE_PROPERTIES in FT_Init_FreeType() and parses it at
   once. The value is the same for every caller, so a shared buffer will do. */
static char envbuf[256];

char *getenv(const char *name)
{
    /* DOS calls need a process; a plain task gets no environment */
    if (FindTask(NULL)->tc_Node.ln_Type != NT_PROCESS)
        return NULL;
    if (GetVar((STRPTR)name, (STRPTR)envbuf, sizeof(envbuf), 0) < 0)
        return NULL;
    return envbuf;
}
