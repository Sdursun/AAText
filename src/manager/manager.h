#ifndef AATEXTMANAGER_MANAGER_H
#define AATEXTMANAGER_MANAGER_H

#include <exec/types.h>

#include "../fontinstall.h"

/* the version of the AAText package (also in main.c's $VER) */
#ifndef AAMGR_VERSION              /* a test build may pretend to be older */
#define AAMGR_VERSION "0.32"
#endif

/* main.c */
BOOL mgr_ParseSizes(const char *s, struct AAInstall *in);
BOOL mgr_CheckFont(const struct AAInstall *in);    /* diskfont opens it */

/* gui.c: the window (language NULL: the user's); returns a DOS return code */
int mgr_RunGUI(const char *language);

#endif
