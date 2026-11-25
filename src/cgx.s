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


| APTR cgx_LockBitMapTagList(base, bm, tags)        bm:A0 tags:A1
| LVO -168 (CGraphX-DevKit fd); verified on P96 and Emu68 with tests/pxbench
        .globl  _cgx_LockBitMapTagList
_cgx_LockBitMapTagList:
        move.l  %a6,-(%sp)
        move.l  8(%sp),%a6
        move.l  12(%sp),%a0
        move.l  16(%sp),%a1
        jsr     -168(%a6)
        move.l  (%sp)+,%a6
        rts

| void cgx_UnLockBitMap(base, handle)              handle:A0
        .globl  _cgx_UnLockBitMap
_cgx_UnLockBitMap:
        move.l  %a6,-(%sp)
        move.l  8(%sp),%a6
        move.l  12(%sp),%a0
        jsr     -174(%a6)
        move.l  (%sp)+,%a6
        rts

| Hook entry for C hook functions: h_SubEntry(hook:A0, object:A2, msg:A1)
| is called with the three as stack arguments (C calling convention).
        .globl  _aa_HookEntry
_aa_HookEntry:
        move.l  %a1,-(%sp)
        move.l  %a2,-(%sp)
        move.l  %a0,-(%sp)
        move.l  12(%a0),%a0             | h_SubEntry
        jsr     (%a0)
        lea     12(%sp),%sp
        rts
