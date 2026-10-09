#ifndef AATEXTMANAGER_MANAGER_H
#define AATEXTMANAGER_MANAGER_H

#include <exec/types.h>

#include "../fontinstall.h"

/* main.c */
BOOL mgr_ParseSizes(const char *s, struct AAInstall *in);
BOOL mgr_CheckFont(const struct AAInstall *in);    /* diskfont opens it */

/* gui.c: the window; returns a DOS return code */
int mgr_RunGUI(void);

#endif
