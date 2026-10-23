#ifndef AATEXT_GUIHELP_H
#define AATEXT_GUIHELP_H

/*
 * The Help menu of AATextPrefs and AATextManager (ReAction and MUI):
 * the documentation in the language the window uses, shown with
 * MultiView.
 */

#include <exec/types.h>

/*
 * The document for language (the catalog's language, NULL: English):
 * LOCALE:Help/türkçe/AAText_TR.txt or LOCALE:Help/english/AAText_EN.txt.
 */
const char *gh_HelpFile(const char *language);

/* Show path with SYS:Utilities/MultiView, on its own; FALSE if the
   file or MultiView is missing. */
BOOL gh_ShowFile(const char *path);

#endif
