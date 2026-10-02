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

| LONG aa_CallOnStack(struct StackSwapStruct *sss, LONG (*func)(APTR), APTR arg)
| Switches to the stack described by sss (exec/StackSwap, LVO -732),
| calls func(arg) there and switches back. Returns func's result.

        .globl  _aa_CallOnStack
_aa_CallOnStack:
        movem.l %d2/%a2-%a3/%a6,-(%sp)  | 16 bytes, args start at 20(sp)
        move.l  20(%sp),%a2             | sss
        move.l  24(%sp),%a3             | func
        move.l  28(%sp),%d2             | arg
        move.l  4,%a6
        move.l  %a2,%a0
        jsr     -732(%a6)               | StackSwap: now on the new stack
        move.l  %d2,-(%sp)
        jsr     (%a3)
        addq.l  #4,%sp
        move.l  %d0,%d2
        move.l  4,%a6
        move.l  %a2,%a0
        jsr     -732(%a6)               | StackSwap: back on the old stack
        move.l  %d2,%d0
        movem.l (%sp)+,%d2/%a2-%a3/%a6
        rts
