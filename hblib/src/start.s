| harfbuzz.library - first code in the file and the stack switcher.

        .text
        .even

| A library is not a program: if someone runs it from the Shell, the first
| instruction is executed. Return an error instead of crashing.

        .globl  _LibNoRun
_LibNoRun:
        moveq   #-1,%d0
        rts

| ULONG hb_CallSwapped(struct StackSwapStruct *sss, ULONG (*func)(APTR), APTR arg)
| Switches to the stack described by sss (exec/StackSwap, LVO -732),
| calls func(arg) there and switches back. Returns func's result.
| In assembler because C code must not touch its locals on the other stack.

        .globl  _hb_CallSwapped
_hb_CallSwapped:
        movem.l %d2/%a2-%a3/%a6,-(%sp)  | 16 bytes, args start at 20(sp)
        move.l  20(%sp),%a2             | sss
        move.l  24(%sp),%a3             | func
        move.l  28(%sp),%d2             | arg
        move.l  4,%a6
        move.l  %a2,%a0
        jsr     -732(%a6)
        move.l  %d2,-(%sp)
        jsr     (%a3)
        addq.l  #4,%sp
        move.l  %d0,%d2
        move.l  %a2,%a0
        jsr     -732(%a6)
        move.l  %d2,%d0
        movem.l (%sp)+,%d2/%a2-%a3/%a6
        rts
