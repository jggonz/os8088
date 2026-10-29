; =============================================================================
; os8088 - apps/win16/w16core.inc
;
; THE COROUTINE AND THE GATE (SPEC.md 124.2, 124.2.1). Two stacks, one task:
;
;   the UI task's own, SS = LOW_SEG - every callback arrives on it, and every
;     line of this package runs on it, OSAPI_* calls included;
;   the program's, SS = DS = DGROUP - the Windows program's code runs on it,
;     and while it does the scheduler declines to switch (SPEC.md 8.5), which
;     is Windows 3.x's cooperative contract for free.
;
; A WAITING PROGRAM IS ONE FRAME ON ITS OWN STACK, the six words a finishing
; import call pops (top first):
;
;   +0 DI   +2 SI   +4 DS   +6 BP   +8 IP   +10 CS   +12 the call's arguments
;
; w16_resume (from a callback) switches to that stack and runs w16_epi, which
; finishes the call. w16_gate (from the program's far call into a stub) builds
; the frame, switches to LOW_SEG at the SP the resume left, and runs the
; import's handler there. A handler that must wait sets [w16_pend] and the
; gate returns from w16_resume instead of to the program.
; =============================================================================

W16F_DI equ 0                   ; the frame (above)
W16F_SI equ 2
W16F_DS equ 4
W16F_BP equ 6
W16F_IP equ 8
W16F_CS equ 10
W16F_ARGS equ 12

W16R_SIDI equ 1                 ; [w16_rflags]: write [w16_rsi]/[w16_rdi] into
                                ; the frame (a register-convention return)

; -----------------------------------------------------------------------------
; w16_resume - run the program until it waits or ends
; in:  AX:DX = the waiting call's result; [w16_rbx]/[w16_rcx]/[w16_res] and
;      [w16_nargb]/[w16_rflags] as the call's handler left them (the loader
;      sets them for the first run); [w16_locked] = whether we hold the lock
; out: [w16_state] = W16S_ENDED if it ended. Clobbers AX, BX, CX, DX - the
;      window-callback contract - and keeps SI, DI, BP, DS and ES
; -----------------------------------------------------------------------------
w16_resume:
    ; STKBALANCE-OK: a COROUTINE SWITCH (SPEC.md 124.2.1). It banks the host
    ; stack and leaves on the PROGRAM's through w16_epi's retf; the host frame
    ; it pushed is popped by w16_tohost when the program next waits or ends
    push bp
    push si
    push di
    push es
    push ds
    mov [w16_rax], ax
    mov [w16_rdx], dx
    mov [w16_hss], ss
    mov [w16_hsp], sp
    pushf
    pop bx
    mov [w16_hflags], bx
    cli
    call w16_i21_on                 ; int 21h is the program's from here
    mov ss, [w16_pss]
    mov sp, [w16_psp_]
    push bx                         ; the host's FLAGS, IF included, onto the
    popf                            ; program's stack and back off it
    ; fall into the epilogue

; -----------------------------------------------------------------------------
; w16_epi - finish the waiting call: on the program's stack, DS = ours
; Moves the frame by [w16_nargb] (the pascal callee's pop, or -2 for
; InitTask's pushed zero), loads the bank, and returns into the program.
; -----------------------------------------------------------------------------
w16_epi:
    mov cx, [w16_nargb]
    or cx, cx
    jz .moved
    js .down
    mov si, sp                      ; UP by CX: copy the six words from the
    add si, W16F_CS                 ; top one down, so nothing is overwritten
    mov di, si                      ; before it is read
    add di, cx
    mov bx, 6
.up:
    mov ax, [ss:si]
    mov [ss:di], ax
    dec si
    dec si
    dec di
    dec di
    dec bx
    jnz .up
    jmp short .shift
.down:
    mov si, sp                      ; DOWN by -CX (InitTask): bottom one first
    mov di, si
    add di, cx
    mov bx, 6
.dn:
    mov ax, [ss:si]
    mov [ss:di], ax
    inc si
    inc si
    inc di
    inc di
    dec bx
    jnz .dn
    mov word [ss:si-2], 0           ; the old CS slot is the pushed zero
.shift:
    add sp, cx
.moved:
    test byte [w16_rflags], W16R_SIDI
    jz .bank
    mov bx, sp
    mov ax, [w16_rdi]
    mov [ss:bx + W16F_DI], ax
    mov ax, [w16_rsi]
    mov [ss:bx + W16F_SI], ax
.bank:
    mov bx, [w16_rbx]
    mov cx, [w16_rcx]
    mov es, [w16_res]
    mov ax, [w16_rax]
    mov dx, [w16_rdx]
    pop di
    pop si
    pop ds
    pop bp
    retf

; -----------------------------------------------------------------------------
; w16_gate - every import arrives here (SPEC.md 124.2.1)
; in:  a far call from the program, AX = the export row (the stub's), the
;      program's SS:SP and DS; BX, CX, ES may be a register function's inputs
; -----------------------------------------------------------------------------
w16_gate:
    ; STKBALANCE-OK: the other half of w16_resume's switch. It arrives on the
    ; program's stack by a far call and leaves on the HOST's, through
    ; w16_tohost, or back on the program's through w16_epi's retf
    push bp
    push ds
    push si
    push di
    mov bp, cs
    mov ds, bp
    mov [w16_idx], ax
    mov [w16_rbx], bx
    mov [w16_rcx], cx
    mov [w16_res], es
    mov [w16_pss], ss
    mov [w16_psp_], sp
    pushf
    pop bx
    cli
    call w16_i21_off                ; (on the program's stack - two words)
    mov ss, [w16_hss]
    mov sp, [w16_hsp]
    push word [w16_hflags]          ; the HOST's flags, not the program's: it
    popf                            ; may have left DF set, and every string
    cld                             ; instruction in here assumes it clear
    mov ax, KERNEL_SEG              ; a callback's ES, which os88ui and the
    mov es, ax                      ; kernel's conventions expect
    xor ax, ax
    mov [w16_pend], al
    mov [w16_rflags], al
    mov [w16_rdx], ax
    mov bx, [w16_idx]               ; the row: module, ordinal, args, handler
    mov ax, W16X_SIZE
    mul bx
    mov bx, ax
    add bx, w16_exports
    mov al, [bx + W16X_ARGB]
    cbw
    mov [w16_nargb], ax
    call [bx + W16X_PROC]           ; AX(:DX) = the result, or [w16_pend]
    mov [w16_rax], ax
    mov [w16_rdx], dx
    cmp byte [w16_state], W16S_ENDED
    je w16_tohost
    cmp byte [w16_pend], 0
    jne w16_tohost
    pushf                           ; back to the program, finished
    pop bx
    cli
    call w16_i21_on
    mov ss, [w16_pss]
    mov sp, [w16_psp_]
    push bx
    popf
    jmp w16_epi

; -----------------------------------------------------------------------------
; w16_tohost - return from w16_resume (we are on the host stack, at its SP)
; The program's frame stays where it is: that IS the waiting program.
; -----------------------------------------------------------------------------
w16_tohost:
    ; STKBALANCE-OK: returns from w16_resume on the stack w16_resume banked,
    ; reached by a jump from whichever side of the switch the program left by
    mov sp, [w16_hsp]
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; w16_args - ES:BX -> the waiting call's arguments (the last one pushed first)
; -----------------------------------------------------------------------------
w16_args:
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    add bx, W16F_ARGS
    ret

; -----------------------------------------------------------------------------
; w16_i21_on / w16_i21_off - int 21h is the program's only while its code
; runs (SPEC.md 124.2.1). IF = 0 on entry to both; DS = ours.
; -----------------------------------------------------------------------------
w16_i21_on:
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov ax, [es:0x21 * 4]
    mov [w16_old21], ax
    mov ax, [es:0x21 * 4 + 2]
    mov [w16_old21 + 2], ax
    mov word [es:0x21 * 4], w16_i21
    mov [es:0x21 * 4 + 2], cs
    pop es
    pop ax
    ret

w16_i21_off:
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov ax, [w16_old21]
    mov [es:0x21 * 4], ax
    mov ax, [w16_old21 + 2]
    mov [es:0x21 * 4 + 2], ax
    pop es
    pop ax
    ret
