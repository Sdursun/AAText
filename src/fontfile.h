#ifndef AATEXT_FONTFILE_H
#define AATEXT_FONTFILE_H

#include <exec/types.h>
#include <dos/dos.h>

#define AA_FONTFILE_LEN 256

/*
 * Lock the font file a .otag names. Font installers write the full path
 * with the volume name of the time ("System:Fonts/_TrueType/x.ttf"),
 * which breaks when the boot volume has another name, so when the path
 * does not exist the part after "Fonts/" is tried in FONTS:, then the
 * path without its volume in SYS:; a relative name is tried in FONTS:.
 * The path found (or the last one tried) is left in path, which has
 * room for AA_FONTFILE_LEN bytes. Returns 0 if none exists.
 */
BPTR aa_LockFontFile(const char *file, char *path);

#endif
