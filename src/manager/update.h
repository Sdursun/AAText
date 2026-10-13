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

#endif
