; =============================================================================
; os8088 - tests/x3xtbench/x3rmseg.inc - OSAPI_SEG AS A REAL-MODE KERNEL
;                                         ANSWERS IT (SPEC.md 50.6.11)
;
; NOT shipped software (tests/). The XT part asks OSAPI_SEG for XTGEN.BIN's
; segment (SEG_SUB, x3xt.asm's OPEN) since KERN-MEGA-MEMORY-PLAN MC7, so a
; harness that far-calls the part with no kernel under it has to answer that
; cell or the part runs whatever is at KERNEL_SEG:cell. This is
; kernel/kernel.asm's osapi_seg below its `.rm:` - the arm every real-mode
; kernel takes - instruction for instruction:
;   SEG_SUB, SEG_SUBC          DX = DX + AX, CF = 0
;   SEG_LIN                    DX:AX = DX x 16, CF = 0
;   SEG_REAL, SEG_WIN, SEG_FREE, SEG_BWIN    the value is the answer, CF = 0
;   SEG_AIM, SEG_AT, anything past         CF = 1, every register kept
; Position-independent: x3xtbench.asm copies it to KERNEL_SEG:the cell and
; tests/x3xtunit.py assembles it alone and writes it there. Its labels carry
; x3rm_ so it can sit in either.
; =============================================================================
X3RM_LIN    equ 0                       ; apps/os88api.inc's SEG_LIN
X3RM_SUB    equ 3                       ; SEG_SUB
X3RM_SUBC   equ 5                       ; SEG_SUBC
X3RM_BWIN   equ 6                       ; SEG_BWIN: the last real-mode verb

x3rm_seg:
    cmp bl, X3RM_SUB
    je .sub
    cmp bl, X3RM_SUBC
    je .sub
    cmp bl, X3RM_BWIN
    ja .no
    cmp bl, X3RM_LIN
    jne .ok
    push cx
    mov ax, dx
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl                          ; DX:AX = DX x 16
    pop cx
.ok:
    clc
    retf
.sub:
    add dx, ax
    clc
    retf
.no:
    stc
    retf
x3rm_seg_end:
