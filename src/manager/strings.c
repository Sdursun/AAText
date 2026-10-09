/*
 * AATextManager - localized strings. See strings.h.
 */

#include <exec/types.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/locale.h>

#include "strings.h"

struct LocaleBase *LocaleBase;
static struct Catalog *catalog;

#define S(id, n, s) [n] = s,     /* retired IDs leave gaps (NULL) */
static const char *const builtin[] = { AA_STRINGS };
#undef S

void InitStrings(const char *language)
{
    LocaleBase = (struct LocaleBase *)OpenLibrary((CONST_STRPTR)"locale.library", 38);
    if (!LocaleBase)
        return;
    catalog = OpenCatalog(NULL, (CONST_STRPTR)"aatextmanager.catalog",
                          OC_BuiltInLanguage, (ULONG)"english",
                          language ? OC_Language : TAG_IGNORE, (ULONG)language,
                          OC_Version, 1,
                          TAG_DONE);
}

void FreeStrings(void)
{
    if (LocaleBase)
    {
        CloseCatalog(catalog);  /* NULL is fine */
        CloseLibrary((struct Library *)LocaleBase);
    }
    catalog = NULL;
    LocaleBase = NULL;
}

const char *GetString(LONG id)
{
    const char *s = (id >= 0 && id < AA_NUM_STRINGS && builtin[id]) ?
                    builtin[id] : "";

    if (catalog)
        s = (const char *)GetCatalogStr(catalog, id, (CONST_STRPTR)s);
    return s;
}
