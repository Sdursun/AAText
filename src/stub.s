| AAText - assembly entry points
|
| aa_TextStub replaces graphics.library/Text() (LVO -60):
|     Text(rp, string, count)  A1, A0, D0   with GfxBase in A6
| It bumps the use counter (so removal can wait for callers to leave)
| and forwards everything to the C hook using the stack calling convention.

        .text
        .even

        .globl  _aa_TextStub
_aa_TextStub:
        addq.l  #1,_aa_UseCount
        move.l  %a6,-(%sp)              | struct GfxBase *gfx
        move.l  %d0,-(%sp)              | LONG count (low word is the WORD arg)
        move.l  %a0,-(%sp)              | CONST_STRPTR string
        move.l  %a1,-(%sp)              | struct RastPort *rp
        jsr     _aa_TextHook
        lea     16(%sp),%sp
        subq.l  #1,_aa_UseCount
        rts

| RawDoFmt() putchar callback: character in D0, forwarded to
| exec/RawPutChar() (LVO -516), which writes to the serial port.

        .globl  _aa_RawPutChProc
_aa_RawPutChProc:
        movem.l %d0-%d1/%a0-%a1/%a6,-(%sp)
        move.l  4,%a6
        jsr     -516(%a6)
        movem.l (%sp)+,%d0-%d1/%a0-%a1/%a6
        rts
