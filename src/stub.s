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

| ---------------------------------------------------------------------
| Measuring functions (real metrics mode). Same pattern as aa_TextStub:
| use counter, then the C hook with stack arguments; result in D0.

| TextLength(rp, string, count)   A1, A0, D0
        .globl  _aa_TextLengthStub
_aa_TextLengthStub:
        addq.l  #1,_aa_UseCount
        move.l  %a6,-(%sp)
        move.l  %d0,-(%sp)
        move.l  %a0,-(%sp)
        move.l  %a1,-(%sp)
        jsr     _aa_TextLengthHook
        lea     16(%sp),%sp
        subq.l  #1,_aa_UseCount
        rts

| TextExtent(rp, string, count, textExtent)   A1, A0, D0, A2
        .globl  _aa_TextExtentStub
_aa_TextExtentStub:
        addq.l  #1,_aa_UseCount
        move.l  %a6,-(%sp)
        move.l  %a2,-(%sp)
        move.l  %d0,-(%sp)
        move.l  %a0,-(%sp)
        move.l  %a1,-(%sp)
        jsr     _aa_TextExtentHook
        lea     20(%sp),%sp
        subq.l  #1,_aa_UseCount
        rts

| TextFit(rp, string, strLen, textExtent, constrainingExtent,
|         strDirection, constrainingBitWidth, constrainingBitHeight)
|   A1, A0, D0, A2, A3, D1, D2, D3
        .globl  _aa_TextFitStub
_aa_TextFitStub:
        addq.l  #1,_aa_UseCount
        move.l  %a6,-(%sp)
        move.l  %d3,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %d1,-(%sp)
        move.l  %a3,-(%sp)
        move.l  %a2,-(%sp)
        move.l  %d0,-(%sp)
        move.l  %a0,-(%sp)
        move.l  %a1,-(%sp)
        jsr     _aa_TextFitHook
        lea     36(%sp),%sp
        subq.l  #1,_aa_UseCount
        rts

| ---------------------------------------------------------------------
| Calling the original functions from C with register arguments.

| LONG aa_CallTextLength(fn, rp, string, count, gfx)
        .globl  _aa_CallTextLength
_aa_CallTextLength:
        movem.l %a2/%a6,-(%sp)          | 8 bytes, args start at 12(sp)
        move.l  12(%sp),%a2             | fn
        move.l  16(%sp),%a1
        move.l  20(%sp),%a0
        move.l  24(%sp),%d0
        move.l  28(%sp),%a6
        jsr     (%a2)
        movem.l (%sp)+,%a2/%a6
        rts

| LONG aa_CallTextExtent(fn, rp, string, count, te, gfx)
        .globl  _aa_CallTextExtent
_aa_CallTextExtent:
        movem.l %a2-%a3/%a6,-(%sp)      | 12 bytes, args start at 16(sp)
        move.l  16(%sp),%a3             | fn
        move.l  20(%sp),%a1
        move.l  24(%sp),%a0
        move.l  28(%sp),%d0
        move.l  32(%sp),%a2
        move.l  36(%sp),%a6
        jsr     (%a3)
        movem.l (%sp)+,%a2-%a3/%a6
        rts

| ULONG aa_CallTextFit(fn, rp, string, len, te, cte, dir, bw, bh, gfx)
        .globl  _aa_CallTextFit
_aa_CallTextFit:
        movem.l %d2-%d3/%a2-%a4/%a6,-(%sp)  | 24 bytes, args start at 28(sp)
        move.l  28(%sp),%a4             | fn
        move.l  32(%sp),%a1
        move.l  36(%sp),%a0
        move.l  40(%sp),%d0
        move.l  44(%sp),%a2
        move.l  48(%sp),%a3
        move.l  52(%sp),%d1
        move.l  56(%sp),%d2
        move.l  60(%sp),%d3
        move.l  64(%sp),%a6
        jsr     (%a4)
        movem.l (%sp)+,%d2-%d3/%a2-%a4/%a6
        rts
