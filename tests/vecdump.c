/*
 * vecdump - prints the function vectors (jump table targets) of system
 * libraries, to find patches a program leaves behind: run it before
 * and after the program and compare the two outputs.
 *
 *   vecdump [library ...]     (default: the usual system libraries)
 */

#include <stdio.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <proto/exec.h>

static const char *const deflibs[] =
{
    "exec.library", "dos.library", "graphics.library", "intuition.library",
    "layers.library", "diskfont.library", "utility.library",
    "locale.library", "icon.library", "workbench.library",
    "gadtools.library", "asl.library", "keymap.library", NULL
};

static void Dump(const char *name)
{
    struct Library *lib = OpenLibrary((CONST_STRPTR)name, 0);
    LONG lvo;

    if (!lib)
    {
        printf("%s: cannot open\n", name);
        return;
    }
    for (lvo = 6; lvo <= lib->lib_NegSize - 6; lvo += 6)
    {
        UBYTE *jmp = (UBYTE *)lib - lvo;

        /* JMP absolute.l = 0x4EF9 */
        if (jmp[0] == 0x4E && jmp[1] == 0xF9)
            printf("%-20s -%-4ld %08lx\n", name, (long)lvo,
                   (unsigned long)*(ULONG *)(jmp + 2));
        else
            printf("%-20s -%-4ld op %02x%02x\n", name, (long)lvo, jmp[0],
                   jmp[1]);
    }
    CloseLibrary(lib);
}

int main(int argc, char **argv)
{
    int i;

    if (argc > 1)
        for (i = 1; i < argc; i++)
            Dump(argv[i]);
    else
        for (i = 0; deflibs[i]; i++)
            Dump(deflibs[i]);
    return 0;
}
