; =============================================================================
; os8088 - apps/win16/w16sel.inc
;
; THE SELECTOR FUNCTIONS (SPEC.md 124.31.6): what standard mode's KERNEL gave
; a program that builds its own segments - a VB runtime, a setup toolkit, an
; installer. The kern_mega arm's alone, over OSAPI_SEG: a selector a program
; makes is a WINDOW of WIN16's (SEG_WIN), so it is re-aimed rather than
; rewritten, it lies on a paragraph below 1 MB, it reaches 64 KB at most, and
; kern_mega frees every one at WIN16's end. An alias of a selector in a claim
; is that claim's SUB or pair (SEG_SUB, SEG_SUBC). LSL, a 286 instruction,
; reads a limit: the arm runs on nothing older. The system-library part's
; (SPEC.md 124.24.1).
; =============================================================================

section .w16sys

; w16q_lin - DX = a selector -> CF=0 DX = the paragraph its base is (below
; 1 MB, on a paragraph), CX = its reach in bytes (0 = 64 KB); CF=1 neither,
; DX kept. Keeps the rest.
w16q_lin:
    push ax
    push bx
    push si
    mov si, dx                      ; SI = the selector
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its base
    jc .no
    test al, 0x0F
    jnz .no
    cmp dx, 0x000F
    ja .no
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or dx, ax                       ; DX = the paragraph
cpu 286
    lsl cx, si                      ; CX = its limit
cpu 8086
    jnz .no
    inc cx                          ; ...its reach, 0 = 64 KB
    clc
    jmp short .out
.no:
    mov dx, si
    stc
.out:
    pop si
    pop bx
    pop ax
    ret

; w16q_win - DX = a paragraph, CX = bytes (0 = 64 KB), AX = a window of
; WIN16's to re-aim, or 0 -> AX = the window over it (SEG_WIN), 0 when none.
; Keeps the rest.
w16q_win:
    push bx
    push dx
    mov bl, SEG_WIN
    call OSAPI_SEG                  ; DX = the window
    mov ax, dx
    jnc .out
    xor ax, ax
.out:
    pop dx
    pop bx
    ret

; AllocSelector (KERNEL.175) - args: +0 wSelector. A new selector: wSelector's
; base and reach, or a 16-byte one at 0 when it is 0, for SetSelectorBase and
; SetSelectorLimit to aim. 0 when none can be had
W16SYS w16q_alloc
    ICALL w16_args
    mov dx, [es:bx]
    mov cx, 16
    or dx, dx
    jz .new
    call w16q_lin                   ; DX = its paragraph, CX = its reach
    mov ax, 0
    jc .out
.new:
    xor ax, ax
    call w16q_win
.out:
    xor dx, dx
    ret

; FreeSelector (KERNEL.176) - args: +0 wSelector. A window back; an alias of
; a claim goes with the claim. Answers 0, as a freed selector does
W16SYS w16q_free
    ICALL w16_args
    mov dx, [es:bx]
    mov bl, SEG_FREE
    call OSAPI_SEG
    xor ax, ax
    xor dx, dx
    ret

; GetSelectorBase (KERNEL.186) - args: +0 wSelector. DX:AX = its linear base
W16SYS w16q_getbase
    ICALL w16_args
    mov dx, [es:bx]
    mov bl, SEG_LIN
    call OSAPI_SEG
    jnc .out
    xor ax, ax
    xor dx, dx
.out:
    ret

; GetSelectorLimit (KERNEL.188) - args: +0 wSelector. DX:AX = its limit
W16SYS w16q_getlim
    ICALL w16_args
    mov ax, [es:bx]
cpu 286
    lsl ax, ax
cpu 8086
    jz .out
    xor ax, ax
.out:
    xor dx, dx
    ret

; SetSelectorBase (KERNEL.187) - args: +0 dwBase, +4 wSelector. The window
; re-aimed there, its reach kept: a base on a paragraph below 1 MB. Answers
; wSelector, or 0
W16SYS w16q_setbase
    ICALL w16_args
    mov dx, [es:bx + 4]
    call w16q_lin                   ; CX = its reach
    mov ax, 0
    jc .out
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    test al, 0x0F
    jnz .no
    cmp dx, 0x000F
    ja .no
    push cx
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or dx, ax                       ; DX = the paragraph
    pop cx
    mov ax, [es:bx + 4]
    call w16q_win
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    ret

; SetSelectorLimit (KERNEL.189) - args: +0 dwLimit, +4 wSelector. The window
; re-aimed at its own base with that reach, 64 KB at most. Answers
; wSelector, or 0
W16SYS w16q_setlim
    ICALL w16_args
    mov dx, [es:bx + 4]
    call w16q_lin                   ; DX = its paragraph
    mov ax, 0
    jc .out
    xor cx, cx                      ; 64 KB or more: 64 KB
    cmp word [es:bx + 2], 0
    jne .lim
    mov cx, [es:bx]
    inc cx
.lim:
    mov ax, [es:bx + 4]
    call w16q_win
.out:
    xor dx, dx
    ret

; PrestoChangoSelector (KERNEL.177) - args: +0 wDestSel, +2 wSourceSel.
; wDestSel, a window, re-aimed at wSourceSel's base and reach: the code/data
; flip itself is kern_mega's to complete, as every far call through a data
; selector is (SPEC.md 20.18). Answers wDestSel, or 0
W16SYS w16q_presto
    ICALL w16_args
    mov dx, [es:bx + 2]
    call w16q_lin
    mov ax, 0
    jc .out
    mov ax, [es:bx]
    call w16q_win
.out:
    xor dx, dx
    ret

; AllocCStoDSAlias (KERNEL.170) - args: +0 wSelector. A data selector at its
; base (SEG_SUB): the claim's own when it is in one. 0 when none
W16SYS w16q_csds
    ICALL w16_args
    mov dx, [es:bx]
    xor ax, ax
    xor cx, cx
    mov bl, SEG_SUB
    call OSAPI_SEG
    mov ax, dx
    jnc .out
    xor ax, ax
.out:
    xor dx, dx
    ret

; AllocDStoCSAlias (KERNEL.171) - args: +0 wSelector. A code selector at its
; base: a pair's code half (SEG_SUBC) in a claim; elsewhere a data one, whose
; far calls kern_mega completes. 0 when none
W16SYS w16q_dscs
    ICALL w16_args
    mov dx, [es:bx]
    push dx
    xor ax, ax
    xor cx, cx
    mov bl, SEG_SUBC
    call OSAPI_SEG
    pop ax
    jc .data
    sub dx, 8
    mov ax, dx
    jmp short .out
.data:
    mov dx, ax
    xor ax, ax
    xor cx, cx
    mov bl, SEG_SUB
    call OSAPI_SEG
    mov ax, dx
    jnc .out
    xor ax, ax
.out:
    xor dx, dx
    ret

; GlobalDOSAlloc (KERNEL.184) - args: +0 dwBytes. A fixed global block below
; 1 MB: DX = its real-mode paragraph, AX = its selector; 0 when none. WIN16's
; claims are conventional on this arm (124.31.1), so it is GlobalAlloc's
W16SYS w16q_dosalloc
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    or ax, ax
    jz .no
    mov ax, dx
    ICALL w16_pblk                  ; AX = its selector
    mov dx, ax
    push ax
    mov bl, SEG_REAL
    call OSAPI_SEG                  ; DX = its real-mode paragraph
    pop ax
    jnc .out
.no:
    xor ax, ax
    xor dx, dx
.out:
    ret

; GlobalDOSFree (KERNEL.185) - args: +0 wSelector. GlobalFree's. Answers 0
W16SYS w16q_dosfree
    ICALL w16_args
    mov ax, [es:bx]
    ICALL w16_pin
    ICALL w16_gfreeh
    xor ax, ax
    xor dx, dx
    ret

section .text
