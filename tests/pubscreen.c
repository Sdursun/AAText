/*
 * pubscreen - opens a public screen of a given size and font, for testing
 * how AATextPrefs (PUBSCREEN=AATEST) fits on small screens.
 *
 *   pubscreen [width] [height] [font.font] [size] [seconds]
 *
 * Defaults: 640 x 256 PAL high resolution, topaz.font 8, 60 seconds.
 * The screen closes after the given time or with Ctrl-C, once all its
 * visitor windows are gone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

int main(int argc, char **argv)
{
    LONG w = argc > 1 ? atol(argv[1]) : 640;
    LONG h = argc > 2 ? atol(argv[2]) : 256;
    struct TextAttr ta = { (STRPTR)(argc > 3 ? argv[3] : "topaz.font"),
                           (UWORD)(argc > 4 ? atol(argv[4]) : 8), 0, 0 };
    LONG secs = argc > 5 ? atol(argv[5]) : 60;
    UWORD pens[] = { (UWORD)~0 };
    struct Screen *scr;
    LONG t;

    scr = OpenScreenTags(NULL,
                         SA_Width, w, SA_Height, h, SA_Depth, 3,
                         SA_DisplayID, PAL_MONITOR_ID | HIRES_KEY,
                         SA_Font, (ULONG)&ta,
                         SA_Pens, (ULONG)pens,
                         SA_Title, (ULONG)"AATEST",
                         SA_PubName, (ULONG)"AATEST",
                         SA_Type, PUBLICSCREEN,
                         TAG_DONE);
    if (!scr)
    {
        printf("pubscreen: cannot open the screen\n");
        return RETURN_FAIL;
    }
    PubScreenStatus(scr, 0);
    printf("pubscreen: AATEST %ldx%ld, %s %ld\n", (long)w, (long)h,
           (char *)ta.ta_Name, (long)ta.ta_YSize);

    for (t = 0; t < secs * 50; t += 10)
    {
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        Delay(10);
    }
    /* make it private, then wait until the visitors have left */
    PubScreenStatus(scr, PSNF_PRIVATE);
    while (!CloseScreen(scr))
        Delay(25);
    return RETURN_OK;
}
