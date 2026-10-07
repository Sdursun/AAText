/*
 * AAText - controller side of the message protocol. See aaclient.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "aaclient.h"

struct AAMessage *aa_SendCommand(UWORD cmd, const char *path,
                                 const struct AAPrefs *prefs,
                                 LONG timeout_ticks)
{
    struct AAMessage *m;
    struct MsgPort *reply, *port;
    LONG i;

    m = AllocVec(sizeof(*m), MEMF_PUBLIC | MEMF_CLEAR);
    reply = CreateMsgPort();
    if (!m || !reply)
    {
        if (m)
            FreeVec(m);
        if (reply)
            DeleteMsgPort(reply);
        return AACLIENT_NOTRUNNING;
    }
    m->msg.mn_Node.ln_Type = NT_MESSAGE;
    m->msg.mn_ReplyPort = reply;
    m->msg.mn_Length = sizeof(*m);
    m->magic = AAMSG_MAGIC;
    m->version = AAMSG_VERSION;
    m->cmd = cmd;
    m->path = (CONST_STRPTR)path;
    m->prefs = prefs;

    Forbid();
    port = FindPort((CONST_STRPTR)AA_PORTNAME);
    if (port)
        PutMsg(port, &m->msg);
    Permit();
    if (!port)
    {
        DeleteMsgPort(reply);
        FreeVec(m);
        return AACLIENT_NOTRUNNING;
    }

    for (i = 0; i < timeout_ticks && !GetMsg(reply); i++)
        Delay(1);
    if (i == timeout_ticks)
        return AACLIENT_TIMEOUT;    /* m and reply leak on purpose */

    DeleteMsgPort(reply);
    return m;
}

void aa_FreeReply(struct AAMessage *m)
{
    if (m != AACLIENT_NOTRUNNING && m != AACLIENT_TIMEOUT)
        FreeVec(m);
}
