#ifndef AATEXT_AAMSG_H
#define AATEXT_AAMSG_H

/*
 * Message protocol between a running AAText and its controllers (the
 * preferences program, "AAText RELOAD" / "AAText STATUS").
 *
 * AAText owns the public port AA_PORTNAME. A controller fills an
 * AAMessage, sends it there with PutMsg() (FindPort() and PutMsg()
 * inside Forbid()) and waits for the reply on its own reply port.
 * AAText checks magic, version and length and ignores anything else.
 */

#include <exec/types.h>
#include <exec/ports.h>

#include "prefs.h"

#define AA_PORTNAME     "AAText"
#define AAMSG_MAGIC     0x41415458UL        /* 'AATX' */
#define AAMSG_VERSION   2       /* 2: AAPrefs.kerning (0.12) */

enum
{
    AACMD_RELOAD = 1,   /* read the prefs file again and apply it */
    AACMD_APPLY,        /* apply the AAPrefs in the message (preview, Use) */
    AACMD_STATUS        /* fill in the status fields */
};

/* result values (AACMD_RELOAD / AACMD_APPLY) */
#define AARES_OK        0
#define AARES_RESTART   1   /* applied, but some changes (real metrics,
                               font mappings) only take effect after
                               AAText is restarted */
#define AARES_NOFILE    -1  /* RELOAD: prefs file could not be read */
#define AARES_BADMSG    -2  /* unknown command or protocol version */

/* status flags */
#define AASTAT_AUTODETECT   0x01
#define AASTAT_MEASURING    0x02    /* TextLength() etc. are patched */
#define AASTAT_PASSTHROUGH  0x04    /* could not be removed, inactive */

struct AAMessage
{
    struct Message  msg;
    ULONG           magic;          /* AAMSG_MAGIC */
    UWORD           version;        /* AAMSG_VERSION */
    UWORD           cmd;            /* AACMD_... */
    LONG            result;         /* filled in by AAText */

    /* AACMD_RELOAD: prefs file, NULL = the one AAText started with */
    CONST_STRPTR    path;
    /* AACMD_APPLY: settings to apply (only valid until the reply) */
    const struct AAPrefs *prefs;

    /* AACMD_STATUS reply */
    char            versionstr[40]; /* e.g. "AAText 0.12 (8.10.2026)" */
    ULONG           flags;          /* AASTAT_... */
    LONG            numfonts;       /* bitmap font/size entries */
    LONG            numfaces;       /* loaded TrueType/OpenType files */
    ULONG           cachebytes;     /* glyph cache in use */
    ULONG           cacheglyphs;
    struct AAPrefs  active;         /* the settings in effect */
};

#endif
