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

    /*
     * Everything AAText reads goes into the message's own memory (the
     * path right after it): after a timeout AAText may still read it,
     * when this program could be gone.
     */
    for (i = 0; path && path[i]; i++)
        ;
    m = AllocVec(sizeof(*m) + (path ? i + 1 : 0), MEMF_PUBLIC | MEMF_CLEAR);
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
    if (path)
    {
        CopyMem((APTR)path, m + 1, i + 1);
        m->path = (CONST_STRPTR)(m + 1);
    }
    if (prefs)
    {
        CopyMem((APTR)prefs, &m->active, sizeof(m->active));
        m->prefs = &m->active;
    }

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
    {
        /*
         * m and reply leak on purpose: AAText may still reply. The port
         * must not signal this task then (it may be gone), so it just
         * keeps the message.
         */
        Forbid();
        if (!GetMsg(reply))
        {
            reply->mp_Flags = (reply->mp_Flags & ~PF_ACTION) | PA_IGNORE;
            Permit();
            return AACLIENT_TIMEOUT;
        }
        Permit();                   /* the reply came just in time */
    }

    DeleteMsgPort(reply);
    return m;
}

void aa_FreeReply(struct AAMessage *m)
{
    if (m != AACLIENT_NOTRUNNING && m != AACLIENT_TIMEOUT)
        FreeVec(m);
}
