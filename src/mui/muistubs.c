/*
 * Varargs version of MUI_NewObject() for the MUI GUIs.
 * The inline header made by sfdc turns it into a macro, which the MUI
 * object macros (ApplicationObject ... End) cannot use; with
 * NO_INLINE_STDARG it is this function, calling the tag list
 * version with the arguments on the stack.
 */

#include <exec/types.h>
#include <utility/tagitem.h>
#include <libraries/mui.h>
#include <proto/muimaster.h>

Object *MUI_NewObject(char *classname, Tag tag1, ...)
{
    return MUI_NewObjectA(classname, (struct TagItem *)&tag1);
}
