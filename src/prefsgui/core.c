/*
 * AATextPrefs - the settings and everything around them, without a
 * window. See core.h.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <stdio.h>
#include <string.h>

#include "core.h"
#include "../prefswrite.h"
#include "../aaclient.h"
#include "../aamsg.h"
#include "../charsets.h"

#define REPLY_TICKS 250

struct PrefsCore pc;

void pc_Init(void)
{
    aa_ReadPrefs(&pc.cur, NULL, FALSE);     /* defaults if there is no file */
    CopyMem(&pc.cur, &pc.before, sizeof(pc.before));
    CopyMem(&pc.cur, &pc.orig, sizeof(pc.orig));
}

/* ------------------------------------------------------------------ */
/* Talking to AAText                                                   */
/* ------------------------------------------------------------------ */

LONG pc_Query(void)
{
    struct AAMessage *m = aa_SendCommand(AACMD_STATUS, NULL, NULL,
                                         REPLY_TICKS);

    pc.running = FALSE;
    if (m == AACLIENT_TIMEOUT)
        return PC_OLD;
    if (m == AACLIENT_NOTRUNNING)
        return PC_STOPPED;
    if (m->result != AARES_OK)
    {
        /* another protocol version: AAText answered without the status */
        aa_FreeReply(m);
        return PC_OLD;
    }
    pc.running = TRUE;
    CopyMem(&m->active, &pc.before, sizeof(pc.before));
    snprintf(pc.version, sizeof(pc.version), "%s", m->versionstr);
    aa_FreeReply(m);
    return PC_RUNNING;
}

LONG pc_Send(UWORD cmd, const struct AAPrefs *p)
{
    struct AAMessage *m;
    LONG r;

    if (!pc.running)
        return PC_GONE;
    m = aa_SendCommand(cmd, NULL, p, REPLY_TICKS);
    if (m == AACLIENT_NOTRUNNING || m == AACLIENT_TIMEOUT)
    {
        pc.running = FALSE;
        return PC_GONE;
    }
    r = m->result == AARES_RESTART ? PC_RESTART : PC_SENT;
    aa_FreeReply(m);
    return r;
}

BOOL pc_LiveApply(void)
{
    static struct AAPrefs p;    /* ~6.5 KB: not on the stack */

    if (!pc.running)
        return FALSE;
    CopyMem(&pc.cur, &p, sizeof(p));
    p.autoreal = pc.before.autoreal;
    p.kerning = pc.before.kerning;
    pc_Send(AACMD_APPLY, &p);
    if (!pc.running)
        return FALSE;
    pc.tested = TRUE;
    return TRUE;
}

BOOL pc_Usage(ULONG *cachebytes, ULONG *glyphs, ULONG *facebytes,
              LONG *files)
{
    struct AAMessage *m;

    if (!pc.running)
        return FALSE;
    m = aa_SendCommand(AACMD_STATUS, NULL, NULL, REPLY_TICKS);
    if (m == AACLIENT_NOTRUNNING || m == AACLIENT_TIMEOUT)
        return FALSE;
    *cachebytes = m->cachebytes;
    *glyphs = m->cacheglyphs;
    *facebytes = m->facebytes;
    *files = m->numfaces;
    aa_FreeReply(m);
    return TRUE;
}

BOOL pc_Write(const char *path, const char *template_path)
{
    return aa_WritePrefs(&pc.cur, path, template_path);
}

/* ------------------------------------------------------------------ */
/* Programs                                                            */
/* ------------------------------------------------------------------ */

LONG pc_AddProgram(const char *name)
{
    char buf[AA_NAME_LEN];
    LONG i, len;

    /* trim blanks; names are compared without case, as AAText does */
    while (*name == ' ' || *name == '\t')
        name++;
    snprintf(buf, sizeof(buf), "%s", name);
    for (len = strlen(buf); len > 0 && (buf[len - 1] == ' ' ||
                                        buf[len - 1] == '\t'); len--)
        buf[len - 1] = 0;
    if (!buf[0])
        return PC_EMPTY;
    for (i = 0; i < pc.cur.numblack; i++)
        if (!Stricmp((CONST_STRPTR)pc.cur.blacklist[i], (CONST_STRPTR)buf))
            return PC_DOUBLE;
    if (pc.cur.numblack == AA_MAX_BLACKLIST)
        return PC_FULL;
    strcpy(pc.cur.blacklist[pc.cur.numblack++], buf);
    return PC_ADDED;
}

void pc_RemoveProgram(LONG i)
{
    if (i < 0 || i >= pc.cur.numblack)
        return;
    for (; i < pc.cur.numblack - 1; i++)
        strcpy(pc.cur.blacklist[i], pc.cur.blacklist[i + 1]);
    pc.cur.numblack--;
}

/* Remember name (len chars) once, sorted, for the running list. */
static void AddRunning(const char *name, LONG len)
{
    char buf[AA_NAME_LEN];
    LONG i, j;

    if (len <= 0 || pc.numrunning == PC_MAX_RUNNING)
        return;
    if (len >= AA_NAME_LEN)
        len = AA_NAME_LEN - 1;
    CopyMem((APTR)name, buf, len);
    buf[len] = 0;
    for (i = 0; i < pc.numrunning; i++)
    {
        LONG c = Stricmp((CONST_STRPTR)buf, (CONST_STRPTR)pc.runnames[i]);

        if (c == 0)
            return;
        if (c < 0)
            break;
    }
    for (j = pc.numrunning; j > i; j--)
        strcpy(pc.runnames[j], pc.runnames[j - 1]);
    strcpy(pc.runnames[i], buf);
    pc.numrunning++;
}

/*
 * The names AAText's blacklist can match: task names and, for Shell
 * processes, the running command (without its path).
 */
static void AddTaskNames(struct Task *t)
{
    const char *name = t->tc_Node.ln_Name;

    if (name)
        AddRunning(name, strlen(name));
    if (t->tc_Node.ln_Type == NT_PROCESS)
    {
        struct CommandLineInterface *cli =
            BADDR(((struct Process *)t)->pr_CLI);

        if (cli && cli->cli_Module && cli->cli_CommandName)
        {
            const UBYTE *b = BADDR(cli->cli_CommandName);
            const char *cmd = (const char *)b + 1;
            LONG len = b[0], i;

            for (i = len - 1; i >= 0; i--)
                if (cmd[i] == '/' || cmd[i] == ':')
                {
                    cmd += i + 1;
                    len -= i + 1;
                    break;
                }
            AddRunning(cmd, len);
        }
    }
}

/*
 * Leave out what nobody blacklists: file systems and handlers (DF0,
 * RAM, CON), library/device tasks and Shell process names.
 */
static BOOL IsProgramName(const char *name)
{
    static const char *const skip[] =
    {
        ".device", ".library", ".resource", ".gadget", ".class", ".image",
        NULL
    };
    static const char *const shells[] =
    {
        "Background CLI", "Shell Process", "Initial CLI", "New CLI",
        "AAText", "AATextPrefs", NULL
    };
    struct DosList *dl;
    LONG len = strlen(name), i;
    BOOL handler;

    for (i = 0; skip[i]; i++)
    {
        LONG n = strlen(skip[i]);

        if (len > n && !Stricmp((CONST_STRPTR)name + len - n,
                                (CONST_STRPTR)skip[i]))
            return FALSE;
    }
    for (i = 0; shells[i]; i++)
        if (!Stricmp((CONST_STRPTR)name, (CONST_STRPTR)shells[i]))
            return FALSE;

    dl = LockDosList(LDF_DEVICES | LDF_READ);
    handler = FindDosEntry(dl, (CONST_STRPTR)name, LDF_DEVICES) != NULL;
    UnLockDosList(LDF_DEVICES | LDF_READ);
    return !handler;
}

void pc_FillRunning(void)
{
    struct Node *n;
    LONG i, j;

    pc.numrunning = 0;
    Forbid();
    AddTaskNames(FindTask(NULL));
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
        AddTaskNames((struct Task *)n);
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ; n = n->ln_Succ)
        AddTaskNames((struct Task *)n);
    Permit();

    /* DOS calls only now, after Permit() */
    for (i = j = 0; i < pc.numrunning; i++)
        if (IsProgramName(pc.runnames[i]))
        {
            if (i != j)
                strcpy(pc.runnames[j], pc.runnames[i]);
            j++;
        }
    pc.numrunning = j;
}

/* ------------------------------------------------------------------ */
/* ENV:ftcodepage                                                      */
/* ------------------------------------------------------------------ */

static BOOL WriteFile(const char *path, const void *data, LONG len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    BOOL ok;

    if (!fh)
        return FALSE;
    ok = Write(fh, (APTR)data, len) == len;
    if (!Close(fh))
        ok = FALSE;
    return ok;
}

/* TRUE if page is the table of one of the sets in the chooser. */
static BOOL KnownCodePage(const UWORD *page)
{
    static UWORD known[256];
    LONG i;

    for (i = 0; i < AA_NUM_CHARSETS; i++)
    {
        aa_CharsetPage(i, known);
        if (!memcmp(known, page, sizeof(known)))
            return TRUE;
    }
    return FALSE;
}

/* ENV:ftcodepage into old; its length, or -1 if it is not set */
static LONG ReadCodePage(UWORD *old)
{
    return GetVar((CONST_STRPTR)"ftcodepage", (STRPTR)old,
                  256 * sizeof(UWORD),
                  GVF_GLOBAL_ONLY | GVF_BINARY_VAR | GVF_DONT_NULL_TERM);
}

BOOL pc_CodePageForeign(void)
{
    static UWORD old[256];
    LONG len = ReadCodePage(old);

    return len >= 0 && (len != (LONG)sizeof(old) || !KnownCodePage(old));
}

/*
 * Make ENV:ftcodepage (and with archive ENVARC:ftcodepage) match the
 * chosen character set, so freetype2.library and AAText agree. Latin-1
 * is what both use without the variable, so it is deleted then.
 */
LONG pc_WriteCodePage(BOOL archive)
{
    static UWORD page[256], old[256];
    BOOL latin1 = pc.cur.charset == AA_CHARSET_LATIN1;
    LONG len;

    aa_CharsetPage(pc.cur.charset, page);
    len = ReadCodePage(old);
    if (latin1)
    {
        if (len >= 0)
            DeleteVar((CONST_STRPTR)"ftcodepage", GVF_GLOBAL_ONLY);
    }
    else if ((len != (LONG)sizeof(old) || memcmp(old, page, sizeof(page))) &&
             !SetVar((CONST_STRPTR)"ftcodepage", (STRPTR)page, sizeof(page),
                     GVF_GLOBAL_ONLY | GVF_BINARY_VAR))
        return PC_CP_ENVFAIL;
    if (!archive)
        return PC_CP_OK;

    /* written by hand: SetVar(GVF_SAVE_VAR) reports success even when
       ENVARC: cannot be written (seen with ENVARC: not assigned) */
    if (latin1 ? DeleteFile((CONST_STRPTR)"ENVARC:ftcodepage") ||
                 IoErr() == ERROR_OBJECT_NOT_FOUND
               : WriteFile("ENVARC:ftcodepage", page, sizeof(page)))
        return PC_CP_OK;
    return PC_CP_ARCFAIL;
}

/* ------------------------------------------------------------------ */
/* Font mappings                                                       */
/* ------------------------------------------------------------------ */

LONG pc_AddMapping(const struct AAMapping *m)
{
    if (pc.cur.nummaps == AA_MAX_MAPPINGS)
        return -1;
    pc.cur.map[pc.cur.nummaps] = *m;
    return pc.cur.nummaps++;
}

void pc_SetMapping(LONG i, const struct AAMapping *m)
{
    if (i >= 0 && i < pc.cur.nummaps)
        pc.cur.map[i] = *m;
}

void pc_RemoveMapping(LONG i)
{
    if (i < 0 || i >= pc.cur.nummaps)
        return;
    for (; i < pc.cur.nummaps - 1; i++)
        pc.cur.map[i] = pc.cur.map[i + 1];
    pc.cur.nummaps--;
}

BOOL pc_SetMapPixel(LONG i, UWORD px)
{
    if (i < 0 || i >= pc.cur.nummaps || pc.cur.map[i].pixelsize == px)
        return FALSE;
    pc.cur.map[i].pixelsize = px;
    return TRUE;
}
