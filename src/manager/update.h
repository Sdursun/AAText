#ifndef AATEXTMANAGER_UPDATE_H
#define AATEXTMANAGER_UPDATE_H

/*
 * Update check: asks GitHub for the latest AAText release over HTTPS
 * (bsdsocket.library and AmiSSL 5) and, if it is newer than this
 * program, downloads the package (AAText.lha) to RAM:. Nothing is
 * installed. Runs only when the user asks for it.
 */

#include <exec/types.h>

enum
{
    UPD_NEWER = 0,          /* newer release, saved to u->saved */
    UPD_CURRENT,            /* this is the latest release */
    UPD_NONET,              /* no bsdsocket.library (TCP/IP not running) */
    UPD_NOSSL,              /* no AmiSSL 5 */
    UPD_FAILED              /* network, TLS or HTTP error: u->error */
};

struct AAUpdate
{
    char tag[32];           /* "v0.21" */
    char saved[64];         /* "RAM:AAText-v0.21.lha" */
    char error[160];
};

/*
 * myversion: this program's version ("0.20"). Downloads the package of
 * a newer release. Blocking; the caller shows a busy pointer.
 */
LONG upd_Check(const char *myversion, struct AAUpdate *u);

/*
 * Downloads over HTTPS for the font catalog (same libraries and
 * certificate check as the update check). net_Open() returns 0, or
 * UPD_NONET, UPD_NOSSL, UPD_FAILED (n->error). Blocking.
 */
struct AANet
{
    void *ctx;              /* SSL_CTX */
    BOOL opened;            /* AmiSSL opened */
    const char *version;    /* for the User-Agent */
    char error[160];        /* why the last call failed */
};

LONG net_Open(struct AANet *n, const char *myversion);
void net_Close(struct AANet *n);

/* url into mem (max bytes, NUL-terminated); bytes read or -1 (n->error) */
LONG net_GetMem(struct AANet *n, const char *url, char *mem, LONG max);

/* url into the file path (deleted again on failure) */
BOOL net_GetFile(struct AANet *n, const char *url, const char *path);

#endif
