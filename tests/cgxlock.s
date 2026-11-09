| pxbench only: cybergraphics.library LockBitMapTagList()/UnLockBitMap().
| LVOs -168/-174 from the AROS cybergraphics function list (the same list
| gives -96 GetCyberMapAttr and -120/-126 Read/WritePixelArray, which
| src/cgx.h uses); not verified on CyberGraphX itself.

        .text
        .even

| APTR cgx_LockBitMapTagList(base, bm, tags)        bm:A0 tags:A1
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
