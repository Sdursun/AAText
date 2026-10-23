/*
 * The Help menu: the documentation in the window's language. See
 * guihelp.h.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <stdio.h>

#include "guihelp.h"

/* ISO-8859-1/9 bytes, as the catalog and the installer name them */
#define TURKISH  "t\xFC" "rk\xE7" "e"

const char *gh_HelpFile(const char *language)
{
    if (language && !Stricmp((CONST_STRPTR)language, (CONST_STRPTR)TURKISH))
        return "LOCALE:Help/" TURKISH "/AAText_TR.txt";
    return "LOCALE:Help/english/AAText_EN.txt";
}

BOOL gh_ShowFile(const char *path)
{
    static const char viewer[] = "SYS:Utilities/MultiView";
    char cmd[160];
    BPTR lock, in, out;

    /* an asynchronous SystemTags() succeeds even when the program is
       missing, so look first */
    if (!(lock = Lock((CONST_STRPTR)path, ACCESS_READ)))
        return FALSE;
    UnLock(lock);
    if (!(lock = Lock((CONST_STRPTR)viewer, ACCESS_READ)))
        return FALSE;
    UnLock(lock);

    snprintf(cmd, sizeof(cmd), "%s \"%s\"", viewer, path);
    in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    if (in && out &&
        SystemTags((CONST_STRPTR)cmd, SYS_Input, in, SYS_Output, out,
                   SYS_Asynch, TRUE, NP_StackSize, 16384, TAG_DONE) == 0)
        return TRUE;        /* the new process closes in and out */
    if (in)
        Close(in);
    if (out)
        Close(out);
    return FALSE;
}
