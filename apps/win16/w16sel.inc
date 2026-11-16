; =============================================================================
; os8088 - apps/win16/w16sel.inc
;
; THE SELECTOR FUNCTIONS (SPEC.md 124.31.6): what standard mode's KERNEL gave
; a program that builds its own segments - a VB runtime, a setup toolkit, an
; installer. The kern_mega arm's alone, over OSAPI_SEG: a selector a program
; makes is a WINDOW of WIN16's (SEG_WINL), so it is re-aimed rather than
; rewritten, it lies at any linear base, it reaches 64 KB at most, and
; kern_mega frees every one at WIN16's end. An alias of a selector in a claim
; is that claim's SUB or pair (SEG_SUB, SEG_SUBC). LSL, a 286 instruction,
; reads a limit: the arm runs on nothing older. The system-library part's
; (SPEC.md 124.24.1).
; =============================================================================

section .w16sys

; w16q_lin - DX = a selector -> CF=0 DI:DX = its linear base, CX = its
; reach in bytes (0 = 64 KB); CF=1 neither, DX kept. Keeps the rest but DI.
w16q_lin:
    push ax
    push bx
    push si
    mov si, dx                      ; SI = the selector
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its base
    jc .no
    mov di, dx
    mov dx, ax                      ; DI:DX = it
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

; w16q_win - DI:DX = a linear base, CX = bytes (0 = 64 KB), AX = a selector
; the program made to re-aim, or 0 for a new one -> AX = the window over it
; (SEG_WINL, above 1 MB too: SPEC.md 124.31.3), 0 when none. A new one is made by SEG_AT, which is never matched
; against another window: SEG_WIN asked for a new window answers one of
; WIN16's own that has the same base and reach, which may be the program's
; DS. It is recorded in w16q_wins, and only a recorded one is re-aimed
; (124.31.6). Keeps the rest.
w16q_win:
    push bx
    push dx
    push si
    or ax, ax
    jnz .aim
    call w16q_slot                  ; SI = a free row
    jc .none
    push cx
    push dx
    mov dx, ds                      ; a fresh window over anything of ours,
    xor cx, cx                      ;   aimed below
    mov bl, SEG_AT
    call OSAPI_SEG                  ; DX = it, 0 none
    mov ax, dx
    pop dx
    pop cx
    jc .none
    or ax, ax
    jz .none
    mov [si], ax
    call .win
    jnc .out
    call w16q_wfree                 ; not aimed: given back
    jmp short .none
.aim:
    call w16q_slot                  ; a window of the program's?
    jc .none
    call .win
    jnc .out
.none:
    xor ax, ax
.out:
    pop si
    pop dx
    pop bx
    ret
.win:                               ; AX re-aimed: CF=0 AX = it
    push si
    push dx
    mov si, ax
    mov ax, dx
    mov dx, di                      ; DX:AX = the base
    mov bl, SEG_WINL
    call OSAPI_SEG                  ; DX = the window
    mov ax, dx
    pop dx
    pop si
    ret

; w16q_slot - AX = a selector, or 0 for a free row -> CF=0 SI = its row in
; w16q_wins; CF=1 none. Keeps the rest
w16q_slot:
    push cx
    mov si, w16q_wins
    mov cx, W16Q_NWIN
.l:
    cmp [si], ax
    je .out                         ; (CF = 0: equal)
    inc si
    inc si
    loop .l
    stc
.out:
    pop cx
    ret

; w16q_wfree - AX = a selector: given back when the program made it
; (w16q_wins). An alias or a block's selector is left alone: SEG_SUB and
; SEG_WIN answer one already made for the same base, which WIN16 or the
; program may still be using (124.31.6). Keeps all
w16q_wfree:
    push bx
    push dx
    push si
    or ax, ax
    jz .out
    call w16q_slot
    jc .out
    mov word [si], 0
    mov dx, ax
    mov bl, SEG_FREE
    call OSAPI_SEG
.out:
    pop si
    pop dx
    pop bx
    ret

; AllocSelector (KERNEL.175) - args: +0 wSelector. A new selector: wSelector's
; base and reach, or a 16-byte one at 0 when it is 0, for SetSelectorBase and
; SetSelectorLimit to aim. 0 when none can be had
W16SYS w16q_alloc
    push di
    ICALL w16_args
    mov dx, [es:bx]
    mov cx, 16
    xor di, di
    or dx, dx
    jz .new
    call w16q_lin                   ; DI:DX = its base, CX = its reach
    mov ax, 0
    jc .out
.new:
    xor ax, ax
    call w16q_win
.out:
    xor dx, dx
    pop di
    ret

; FreeSelector (KERNEL.176) - args: +0 wSelector. A window the program made
; back; anything else is left (w16q_wfree). Answers 0, as a freed selector does
W16SYS w16q_free
    ICALL w16_args
    mov ax, [es:bx]
    call w16q_wfree
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
; re-aimed there, its reach kept: any linear base, above 1 MB too. Answers
; wSelector, or 0
W16SYS w16q_setbase
    push di
    ICALL w16_args
    mov dx, [es:bx + 4]
    call w16q_lin                   ; CX = its reach
    mov ax, 0
    jc .out
    mov dx, [es:bx]
    mov di, [es:bx + 2]             ; DI:DX = the base
    mov ax, [es:bx + 4]
    call w16q_win
.out:
    xor dx, dx
    pop di
    ret

; SetSelectorLimit (KERNEL.189) - args: +0 dwLimit, +4 wSelector. The window
; re-aimed at its own base with that reach, 64 KB at most. Answers
; wSelector, or 0
W16SYS w16q_setlim
    push di
    ICALL w16_args
    mov dx, [es:bx + 4]
    call w16q_lin                   ; DI:DX = its base
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
    pop di
    ret

; w16q_ctype - DX = a selector -> CF = 0 CX = the type a selector made from it
; takes: 1 code when it is data, 0 data when it is code (LAR reads the access
; byte, bit 3 = code); CF = 1 not a live selector. Keeps the rest. 286 code
w16q_ctype:
    push ax
    xor cx, cx
cpu 286
    lar ax, dx
cpu 8086
    jnz .no
    test ah, 8
    jnz .o                          ; code: its alias is data (CF = 0)
    inc cx
.o:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; w16q_retype - DX = a window of the program's, CX = 0 data / 1 code -> CF = 0
; its type set in place (OSAPI_SEG SEG_TYPE, SPEC.md 50.6.11); CF = 1 refused.
; Keeps the rest
w16q_retype:
    push ax
    push bx
    mov bl, SEG_TYPE
    call OSAPI_SEG
    pop bx
    jc .no
    pop ax
    ret
.no:
    pop ax
    stc
    ret

; w16q_touch - DX = a selector an alias is about to be made from: a discarded
; code segment's (SPEC.md 124.26.12) loaded first, so the alias reads its code
; and not the module block it is parked on. Clobbers AX
w16q_touch:
%ifdef PKG_MEGA
    mov ax, dx
    ICALL w16_touch
%endif
    ret

; PrestoChangoSelector (KERNEL.177) - args: +0 wDestSel, +2 wSourceSel.
; wDestSel, a window, re-aimed at wSourceSel's base and reach and given the
; OTHER type, code from data and data from code, as Windows does (SPEC.md
; 124.31.6): a REAL code selector, which a far call takes with no help from
; kern_mega. Answers wDestSel, or 0
W16SYS w16q_presto
    push di
    ICALL w16_args
    mov dx, [es:bx + 2]
    call w16q_touch
    call w16q_ctype
    mov ax, 0
    jc .out
    mov ax, [es:bx]
    cmp ax, dx
    jne .two
    mov dx, ax                      ; source = destination: the selector
    call w16q_retype                ; flipped in place - a window, or the
    mov ax, dx                      ; handle descriptor of a block of the
    jnc .out                        ; program's (Arcade's ASTEROID)
    xor ax, ax
    jmp short .out
.two:
    push cx                         ; the type it gets
    call w16q_lin
    mov ax, 0
    jc .pop
    mov ax, [es:bx]
    call w16q_win
    or ax, ax
    jz .pop
    mov dx, ax
    pop cx
    call w16q_retype
    mov ax, dx
    jnc .out
    xor ax, ax                      ; (a window it would not flip: refused)
    jmp short .out
.pop:
    pop cx
.out:
    xor dx, dx
    pop di
    ret

; AllocCStoDSAlias (KERNEL.170) - args: +0 wSelector. A data selector at its
; base (SEG_SUB): the claim's own when it is in one. 0 when none
W16SYS w16q_csds
    ICALL w16_args
    mov dx, [es:bx]
    call w16q_touch
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
; base: a pair's code half (SEG_SUBC) in a claim, which follows the block; else
; a new CODE window over the same base and reach (SEG_TYPE, SPEC.md 124.31.6),
; recorded like any the program made, so FreeSelector gives it back. 0 when
; none
W16SYS w16q_dscs
    push di
    ICALL w16_args
    mov dx, [es:bx]
    call w16q_touch
    push dx
    xor ax, ax
    xor cx, cx
    mov bl, SEG_SUBC
    call OSAPI_SEG
    pop ax
    jc .win
    sub dx, 8                       ; the pair's code half
    mov ax, dx
    jmp short .out
.win:
    mov dx, ax
    call w16q_lin                   ; DI:DX = its base, CX = its reach
    jc .none
    xor ax, ax
    call w16q_win                   ; AX = a new window
    or ax, ax
    jz .none
    mov dx, ax
    mov cx, 1
    call w16q_retype
    mov ax, dx
    jnc .out
    call w16q_wfree                 ; (not flipped: given back)
.none:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    ret

; GlobalDOSAlloc (KERNEL.184) - args: +0 dwBytes. A fixed block below 1 MB:
; DX = its real-mode paragraph, AX = its selector; 0 when none. The program's
; arenas may be above 1 MB (124.31.3), so the block is a claim of its own
; through the DMA door (CX = 0: conventional), recorded in w16q_dosb for
; GlobalDOSFree and freed by kern_mega with WIN16 if the program never does
W16SYS w16q_dosalloc
    push si
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    add ax, 15                      ; paragraphs, 64KB at most
    adc dx, 0
    cmp dx, 1
    ja .no
    jb .kb
    or ax, ax
    jnz .no
.kb:
    mov cl, 4                       ; DX:AX / 16: 0-4,096
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    jz .no
    push ax
    xor ax, ax
    call w16q_dslot                 ; SI = a free row
    pop ax
    jc .no
    xor cx, cx
    push bx
    mov bl, MEMP_CONV
    call OSAPI_MEM_P            ; DX = the block's selector
    pop bx
    jc .no
    mov [si], dx
    mov ax, dx
    mov bl, SEG_REAL
    call OSAPI_SEG                  ; DX = its real-mode paragraph
    jnc .out
    mov dx, ax                      ; (a DMA-door claim always has one)
    mov word [si], 0
    call OSAPI_MEM_FREE
.no:
    xor ax, ax
    xor dx, dx
.out:
    pop si
    ret

; GlobalDOSFree (KERNEL.185) - args: +0 wSelector. GlobalDOSAlloc's claim
; back; any other block is GlobalFree's. Answers 0
W16SYS w16q_dosfree
    push si
    ICALL w16_args
    mov ax, [es:bx]
    or ax, ax
    jz .out
    call w16q_dslot
    jc .heap
    mov word [si], 0
    mov dx, ax
    call OSAPI_MEM_FREE
    jmp short .out
.heap:
    ICALL w16_pin
    ICALL w16_gfreeh
.out:
    xor ax, ax
    xor dx, dx
    pop si
    ret

; w16q_dslot - AX = a selector, or 0 for a free row -> CF=0 SI = its row in
; w16q_dosb; CF=1 none. Keeps the rest
w16q_dslot:
    push cx
    mov si, w16q_dosb
    mov cx, W16Q_NDOS
.l:
    cmp [si], ax
    je .out                         ; (CF = 0: equal)
    inc si
    inc si
    loop .l
    stc
.out:
    pop cx
    ret

section .text
