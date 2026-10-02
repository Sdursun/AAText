| AAText - cybergraphics.library call wrappers
|
| C calling convention (all arguments 32 bit on the stack, first argument
| is the library base) -> register calling convention of the library.

        .text
        .even

| ULONG cgx_GetCyberMapAttr(base, bm, attr)          bm:A0 attr:D0
        .globl  _cgx_GetCyberMapAttr
_cgx_GetCyberMapAttr:
        move.l  %a6,-(%sp)
        move.l  8(%sp),%a6
        move.l  12(%sp),%a0
        move.l  16(%sp),%d0
        jsr     -96(%a6)
        move.l  (%sp)+,%a6
        rts

| ULONG cgx_ReadPixelArray(base, dst, dstx, dsty, dstmod, rp,
|                          srcx, srcy, width, height, fmt)
|   A0, D0, D1, D2, A1, D3, D4, D5, D6, D7
        .globl  _cgx_ReadPixelArray
_cgx_ReadPixelArray:
        movem.l %d2-%d7/%a6,-(%sp)      | 28 bytes, args start at 32(sp)
        move.l  32(%sp),%a6
        move.l  36(%sp),%a0
        move.l  40(%sp),%d0
        move.l  44(%sp),%d1
        move.l  48(%sp),%d2
        move.l  52(%sp),%a1
        move.l  56(%sp),%d3
        move.l  60(%sp),%d4
        move.l  64(%sp),%d5
        move.l  68(%sp),%d6
        move.l  72(%sp),%d7
        jsr     -120(%a6)
        movem.l (%sp)+,%d2-%d7/%a6
        rts

| ULONG cgx_WritePixelArray(base, src, srcx, srcy, srcmod, rp,
|                           dstx, dsty, width, height, fmt)
|   A0, D0, D1, D2, A1, D3, D4, D5, D6, D7
        .globl  _cgx_WritePixelArray
_cgx_WritePixelArray:
        movem.l %d2-%d7/%a6,-(%sp)
        move.l  32(%sp),%a6
        move.l  36(%sp),%a0
        move.l  40(%sp),%d0
        move.l  44(%sp),%d1
        move.l  48(%sp),%d2
        move.l  52(%sp),%a1
        move.l  56(%sp),%d3
        move.l  60(%sp),%d4
        move.l  64(%sp),%d5
        move.l  68(%sp),%d6
        move.l  72(%sp),%d7
        jsr     -126(%a6)
        movem.l (%sp)+,%d2-%d7/%a6
        rts

