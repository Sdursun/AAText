#ifndef AATEXT_PREFSWRITE_H
#define AATEXT_PREFSWRITE_H

#include <exec/types.h>

#include "prefs.h"

/*
 * Write the settings the preferences program manages (gamma, hinting,
 * real, cache, offscreen, auto, charset, blacklist) to path.
 *
 * Every other line of the file at template_path (usually the file being
 * replaced) is kept as it is: comments, font mappings, settings this
 * version does not know. Lines with managed keywords are dropped and the
 * current values are appended. template_path may be NULL or missing.
 * Returns FALSE if the file cannot be written.
 */
BOOL aa_WritePrefs(const struct AAPrefs *prefs, const char *path,
                   const char *template_path);

#endif
