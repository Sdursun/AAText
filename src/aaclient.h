#ifndef AATEXT_AACLIENT_H
#define AATEXT_AACLIENT_H

/*
 * Sending commands to a running AAText (see aamsg.h). Used by the
 * "AAText RELOAD/STATUS" Shell commands and the preferences program.
 */

#include "aamsg.h"

#define AACLIENT_NOTRUNNING ((struct AAMessage *)0)
#define AACLIENT_TIMEOUT    ((struct AAMessage *)-1)

/*
 * Send a command and wait up to timeout_ticks (1/50 s) for the reply.
 * Returns the replied message (free it with aa_FreeReply()),
 * AACLIENT_NOTRUNNING if no AAText runs, or AACLIENT_TIMEOUT if it did
 * not answer. On a timeout the message is deliberately not freed: it
 * may still be queued at a running AAText too old to understand it.
 */
struct AAMessage *aa_SendCommand(UWORD cmd, const char *path,
                                 const struct AAPrefs *prefs,
                                 LONG timeout_ticks);

void aa_FreeReply(struct AAMessage *m);

#endif
