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
    call w16_dcunpark               ; a DC open across the wait (124.25.5)
    push bp
    push si
    push di
    push es
    push ds
    mov byte [w16_inprog], 1        ; (w16_mkick: never from inside a run)
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
%ifdef PKG_MEGA
cpu 286                             ; a selector the call freed (GlobalFree,
    verr word [w16_res]             ;   FreeSelector) comes back as 0, as
cpu 8086                            ;   standard mode's KERNEL gave it
    jz .esok
    mov word [w16_res], 0
.esok:
%endif
    mov es, [w16_res]
    mov ax, [w16_rax]
    mov dx, [w16_rdx]
    pop di
    pop si
    pop ds
    pop bp
    retf

; -----------------------------------------------------------------------------
; w16_gstub - a stub's near call (w16_stubs): AX = its slot, off the return
; address, which is popped - then the gate, with the program's far call on top
; w16_gate - every import arrives here (SPEC.md 124.2.1)
; in:  a far call from the program, AX = the export row (the stub's), the
;      program's SS:SP and DS; BX, CX, ES may be a register function's inputs
; -----------------------------------------------------------------------------
w16_gstub:
    ; STKBALANCE-OK: the stub's near call is popped here and never returned to
    pop ax
    sub ax, w16_stubs + 3
    shr ax, 1
    shr ax, 1
%if W16_STUBSZ != 4
  %error "w16_gstub: a stub is not four bytes"
%endif
w16_gate:
    ; STKBALANCE-OK: the other half of w16_resume's switch. It arrives on the
    ; program's stack by a far call and leaves on the HOST's, through
    ; w16_tohost, or back on the program's through w16_epi's retf
    push bp
    push ds
    push si
    push di
    W16CSDS bp
    mov ds, bp
    mov [w16_idx], ax
%ifdef W16_TRACE
    push bx                         ; AX = the slot, which the tool names
    mov bx, [w16_trn]               ;   from w16_bind (SPEC.md 124.17.4)
%ifdef W16_TRFIRST                 ; keep the FIRST W16_TRN, the last slot
    cmp bx, W16_TRN - 1             ;   taking every call after them
    jb short $+5
    mov bx, W16_TRN - 1
%endif
    and bx, W16_TRN - 1
    shl bx, 1
    mov [w16_trc + bx], ax
    inc word [w16_trn]
    pop bx
%endif
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
    mov bx, [w16_idx]               ; the slot: args, part, handler
    shl bx, 1                       ;   (SPEC.md 124.19.1)
    shl bx, 1
    add bx, w16_bind
    mov al, [bx + W16B_ARGB]
    cbw
    mov [w16_nargb], ax
    call w16_dispatch               ; AX(:DX) = the result, or [w16_pend]
    mov [w16_rax], ax
    mov [w16_rdx], dx
    cmp byte [w16_state], W16S_ENDED
    je w16_tohost
    cmp byte [w16_pend], 0
    jne .wait
    cmp byte [w16_cbgo], 0          ; a call INTO the program (SPEC.md 124.2.2)
    jne w16_cbenter
    pushf                           ; back to the program, finished
    pop bx
    cli
    call w16_i21_on
    mov ss, [w16_pss]
    mov sp, [w16_psp_]
    push bx
    popf
    jmp w16_epi
.wait:                              ; it waits: an open DC gives the lock back
    call w16_dcpark                 ;   (SPEC.md 124.25.5)
    jmp w16_tohost

; -----------------------------------------------------------------------------
; w16_dispatch - run slot BX's handler (SPEC.md 124.19.2), with AX = the
; argument bytes as the gate always gave it. Part 0 is the image's own,
; W16B_MISS an import WIN16 lacks, and the library part's are reached through
; its gate, the part fetched first if it is not here. Returns what the
; handler returns.
; -----------------------------------------------------------------------------
w16_dispatch:
    mov al, [bx + W16B_PART]
    and al, ~W16B_NORET & 0xFF
    cmp al, W16B_MISS               ; (before W16B_MDC: 7Fh has its bit)
    je .miss
    test al, W16B_RES               ; a resource call: a library's hInstance
    jz .nores                       ;   reads the library's resources, and
    push word [w16_rtab]            ;   the program's are current again after
    push word [w16_rtlen]           ;   (SPEC.md 124.23.6)
    push word [w16_rfp]
    push word [w16_rown]
    call w16_rin
    call .nores
    pop word [w16_rown]
    pop word [w16_rfp]
    pop word [w16_rtlen]
    pop word [w16_rtab]
    ret
.nores:
    test al, W16B_MDC
    jz .plain
    call w16_odarg                  ; an own DC's the open DC (124.25.11)
    call w16_mdcin                  ; CF=1: a memory DC's, its state the open
    jnc .plain                      ;   DC's for the call (SPEC.md 124.25.3)
    call .plain
    jmp w16_mdcout
.plain:
    mov al, [bx + W16B_PART]
    and al, ~(W16B_NORET | W16B_MDC | W16B_RES) & 0xFF
    test al, W16B_DCF
    jnz .dcf
.go:
    and al, ~W16B_DCF & 0xFF
    jz .image
    cmp al, W16P_SYS
    je .sys
    cmp al, W16P_TSK
    je .tsk
    mov ax, [bx + W16B_PROC]
    mov [w16_lfn], ax
    call w16_dlgin
    jc .nolib
    mov ax, [w16_nargb]
    call far [w16_lbv]
    ret
.sys:                               ; the system-library part (124.24.1)
    mov ax, [bx + W16B_PROC]
    mov [w16_sfn], ax
    call w16_sysin
    jc .nolib
    mov ax, [w16_nargb]
    call far [w16_sbv]
    ret
.tsk:                               ; the task part (124.28.13)
    mov ax, [bx + W16B_PROC]
    mov [w16_tfn], ax
    call w16_tskin
    jc .nolib
    mov ax, [w16_nargb]
    call far [w16_tbv]
    ret
.image:
    mov ax, [w16_nargb]
    jmp [bx + W16B_PROC]
.miss:
    mov ax, [bx + W16B_PROC]
    mov [w16_missi], ax
    jmp w16_missed
.dcf:                               ; a text call (SPEC.md 124.24.4): in the
    push ax                         ;   dialog face while the DC holds its
    mov ax, [w16_dface]             ;   font and came with the System font's
    cmp word [w16_font], W16_HFDLG
    jne .nof
    or ax, ax
    jz .nof
    xchg ax, [w16_face]
    cmp ax, [w16_sface]
    jne .back
    pop ax
    call .go
    push ax
    mov ax, [w16_sface]
    mov [w16_face], ax
    pop ax
    ret
.back:
    mov [w16_face], ax
.nof:
    pop ax
    jmp .go
.nolib:                             ; it could not be read: ended, as a missing
    push si                         ;   import ends it, saying why
    push di
    mov si, w16_s_nolib
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_s_nolib2
    mov di, w16_l2
    call w16_strcpy
    mov byte [w16_endmsg], 1
    mov byte [w16_state], W16S_ENDED
    pop di
    pop si
    xor ax, ax
    cwd
    ret

w16_s_nolib:  db 'WIN16', 39, 's library part', 0
w16_s_nolib2: db 'could not be read.', 0

; -----------------------------------------------------------------------------
; w16_lcall - LCALL's far half (SPEC.md 124.19.2): [w16_lfn] near-called in
; the library part, fetched first if it is not here. Registers and flags pass
; both ways, as ICALL's. CF=1 with nothing called if the part could not be
; read - and op_fetch has said why.
; -----------------------------------------------------------------------------
w16_lcall:
    call w16_dlgin
    jc .out
    call far [w16_lbv]
.out:
    ret

; -----------------------------------------------------------------------------
; w16_cbenter - enter the program's callee instead of finishing the import
; (SPEC.md 124.2.2). The handler pushed the continuation, the callee's
; arguments, w16_cbret and the callee's own address below [w16_psp_]; a retf
; takes the last off, and the callee meets AX = DS = DGROUP.
; -----------------------------------------------------------------------------
w16_cbenter:
    ; STKBALANCE-OK: it leaves on the PROGRAM's stack by retf into the callee;
    ; the host frame stays for w16_tohost, like the gate's own return
%ifdef PKG_MEGA
    call w16_cbpre                  ; a discarded callee loaded first (124.26.12)
%endif
    mov byte [w16_cbgo], 0
    pushf
    pop bx
    cli
    call w16_i21_on
    mov ss, [w16_pss]
    mov sp, [w16_psp_]
    push bx
    popf
    mov ax, [w16_dgroup]
    mov ds, ax
    retf

; -----------------------------------------------------------------------------
; w16_resumecb - a HOST SEND (SPEC.md 124.2.2): from a callback, while the
; program waits in an import, enter the callee a handler's code queued
; (w16_sendwnd: the continuation, the arguments, w16_cbret, the callee). The
; continuation runs as that import's handler again, so it can keep it waiting
; ([w16_pend]) or finish it. The menu loop is the user: WM_INITMENUPOPUP sent
; while DefWindowProc waits (w16menu.inc). Returns as w16_resume does.
; -----------------------------------------------------------------------------
w16_resumecb:
    ; STKBALANCE-OK: w16_resume's switch, ending in w16_cbenter's retf into
    ; the callee instead of w16_epi's; w16_tohost pops the host frame
    call w16_dcunpark
    push bp
    push si
    push di
    push es
    push ds
%ifdef PKG_MEGA
    call w16_cbpre                  ; a discarded callee loaded first (124.26.12)
%endif
    mov byte [w16_inprog], 1
    mov [w16_hss], ss
    mov [w16_hsp], sp
    pushf
    pop bx
    mov [w16_hflags], bx
    cli
    call w16_i21_on
    mov byte [w16_cbgo], 0
    mov ss, [w16_pss]
    mov sp, [w16_psp_]
    push bx
    popf
    mov ax, [w16_dgroup]
    mov ds, ax
    retf

; w16_cbret - where every callee returns: an import of its own (W16_CBROW), so
; the gate's frame and the continuation take it from here. DX:AX is the
; callee's answer, kept through CS because DS is the program's.
w16_cbret:
    ; STKBALANCE-OK: reached by the callee's retf, and left through the gate
%ifdef PKG_MEGA
    call w16_dxax                   ; (no write through CS: 124.31.5)
%else
    mov [cs:w16_cbax], ax
    mov [cs:w16_cbdx], dx
%endif
    push cs                         ; a far return for the gate's frame, never
    call w16_cbret2                 ; used: the handler discards the frame
w16_cbret2:
    ; STKBALANCE-OK: the near call above is only the frame's IP
    mov ax, W16_CBROW
    jmp w16_gate

; w16_thkhit - where a discarded segment's thunk far-calls (SPEC.md 124.26.2):
; an import of its own (W16_SLROW), whose frame returns to the thunk + 5. AX
; and DX are the entry's, kept through CS because DS is the program's.
w16_thkhit:
    ; STKBALANCE-OK: reached by the thunk's far call, and left through the gate
%ifdef PKG_MEGA
    call w16_dxth
%else
    mov [cs:w16_thax], ax
    mov [cs:w16_thdx], dx
%endif
    mov ax, W16_SLROW
    jmp w16_gate

%ifdef PKG_MEGA
; -----------------------------------------------------------------------------
; w16_npent - the #NP handler (SPEC.md 124.26.12, KERN-MEGA-PLAN 14.143), far
; returned to by the kernel with [selector] [IP] [CS] [FLAGS] on the faulting
; stack, every register as at the fault and IF = 0. The program's: the
; selector dropped, AX and DX kept as w16_thkhit keeps them, and the gate
; entered as W16_SLROW with a far return to the iret below, which restarts the
; instruction; w16_segld loads the segment. WIN16's own (SS is not the
; program's): the segment is loaded in place, no code discarded (part 5's
; w16l_npload). Refused by an undefined opcode (kern_mega closes WIN16): a
; load already going.
; -----------------------------------------------------------------------------
cpu 286
w16_npent:
    ; STKBALANCE-OK: far-returned to by the kernel with the CPU's frame on the
    ; faulting stack. Its program arm pushes the gate's far return (CS and the
    ; call's IP) onto an iret, and the gate's retf lands there; the other arm's
    ; iret is the CPU frame's own
    push bp
    push ds
    W16CSDS bp
    mov ds, bp
    mov bp, sp
    mov bp, [bp + 4]
    mov [w16_npsel], bp
    mov [w16_thax], ax
    mov [w16_thdx], dx
    mov bp, ss
    cmp bp, [w16_pss]
    pop ds
    pop bp
    jne .low
    add sp, 2                       ; the selector
    push cs
    call .go
    iret                            ; the gate returns here, then the fault's
.go:                                ;   own frame: the instruction runs again
    mov ax, W16_SLROW
    jmp w16_gate
.low:
    add sp, 2
    pusha
    push ds
    push es
    W16CSDS bp
    mov ds, bp
    mov ax, [w16_npsel]
    mov bx, w16l_nplow              ; XSEC-OK: part 5's, w16_ldcall calls it there
    sti
    call w16_ldcall
    mov word [w16_npsel], 0
    pop es
    pop ds
    jc .die
    popa
    iret
.die:
    db 0x0F, 0x0B                   ; #UD: a program's fault closes WIN16

; w16_touch - AX = a selector the program holds: a code segment that is not
; present is loaded by reading it into DS, here (a #NP in WIN16's own code,
; w16_npent's second arm); a data selector, or one that is no selector, is
; left. Keeps every register, the flags not
w16_touch:
    test al, 4                      ; an LDT selector (a handle or a GDT value
    jz .ret                         ;   is no code segment of the program's)
    push cx
    push ds
    lar cx, ax
    jnz .no
    test ch, 8                      ; access byte: executable
    jz .no
    mov ds, ax
.no:
    pop ds
    pop cx
.ret:
    ret

; w16_cbpre - the callee a callback is about to far-return into (w16_cbenter,
; w16_resumecb): its CS, at the top of the program's stack, touched, so a
; discarded segment is loaded here on WIN16's stack and not at the retf.
; Clobbers AX, BX, ES
w16_cbpre:
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    mov ax, [es:bx + 2]
    jmp w16_touch
cpu 8086

; w16_dxax / w16_dxth - the arm's [cs:] store of DX:AX, through our data
; selector rather than CS (SPEC.md 124.31.5): to w16_cbax/w16_cbdx, or to
; w16_thax/w16_thdx. Keeps all, the flags too.
w16_dxax:
    push bx
    mov bx, w16_cbax
    jmp short w16_dxst
w16_dxth:
    push bx
    mov bx, w16_thax
w16_dxst:
    pushf
    push ds
    push bp
    W16CSDS bp
    mov ds, bp
    mov [bx], ax
    mov [bx + 2], dx
    pop bp
    pop ds
    popf
    pop bx
    ret
%endif

; -----------------------------------------------------------------------------
; w16_cbdone - the W16_CBROW handler: discard the stub's frame, the record
; and the extra bytes, make the served import current again, and run its
; continuation with DX:AX = the callee's answer and CX = the record's word
; -----------------------------------------------------------------------------
w16_cbdone:
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    mov si, [es:bx + W16F_ARGS]     ; the continuation
    mov ax, [es:bx + W16F_ARGS + 2] ; the served import's row
    mov cx, [es:bx + W16F_ARGS + 4] ; its word
    mov dx, [es:bx + W16F_ARGS + 6] ; the extra bytes
    add bx, W16F_ARGS + 8
    add bx, dx
    mov [w16_psp_], bx              ; the served import's frame, again
    mov [w16_idx], ax
    mov bx, ax
    shl bx, 1
    shl bx, 1                       ; * W16B_SIZE
    mov al, [w16_bind + bx + W16B_ARGB]
    cbw
    mov [w16_nargb], ax
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [w16_cbax]
    mov dx, [w16_cbdx]
    jmp si

; -----------------------------------------------------------------------------
; w16_ppush - AX onto the program's stack, below [w16_psp_]. Keeps all.
; -----------------------------------------------------------------------------
w16_ppush:
    push di
    push es
    sub word [w16_psp_], 2
    mov di, [w16_psp_]
    mov es, [w16_pss]
    mov [es:di], ax
    pop es
    pop di
    ret

; -----------------------------------------------------------------------------
; w16_cbopen - begin a call into the program (SPEC.md 124.2.2)
; in:  SI = the continuation, AX = its word, CX = the extra bytes (even)
; out: ES:DI -> the extra block, zeroed (the program's stack segment)
; -----------------------------------------------------------------------------
w16_cbopen:
    push ax
    push cx
    sub [w16_psp_], cx
    mov di, [w16_psp_]
    mov es, [w16_pss]
    push di
    shr cx, 1
    xor ax, ax
    rep stosw
    pop di
    pop ax                          ; the count ...
    call w16_ppush
    pop ax                          ; ...the word
    call w16_ppush
    mov ax, [w16_idx]               ; ...the import being served
    call w16_ppush
    mov ax, si                      ; ...and the continuation
    jmp w16_ppush

; w16_cbcall - the callee at DX:AX, its arguments already pushed: it runs
; when the handler returns to the gate. Keeps all but AX.
w16_cbcall:
    push ax
    mov ax, cs
    call w16_ppush
    mov ax, w16_cbret
    call w16_ppush
    mov ax, dx
    call w16_ppush
    pop ax
    call w16_ppush
    mov byte [w16_cbgo], 1
    ret

; -----------------------------------------------------------------------------
; w16_tohost - return from w16_resume (we are on the host stack, at its SP)
; The program's frame stays where it is: that IS the waiting program.
; -----------------------------------------------------------------------------
w16_tohost:
    ; STKBALANCE-OK: returns from w16_resume on the stack w16_resume banked,
    ; reached by a jump from whichever side of the switch the program left by
    mov sp, [w16_hsp]
    mov byte [w16_inprog], 0
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
; runs (SPEC.md 124.2.1), and so are int 2Fh, 34h-3Dh and int 2 (124.5). IF = 0
; on entry to both; DS = ours.
; -----------------------------------------------------------------------------
w16_i21_on:
    push ax
    push es
    W16SEG0 ax, es
    mov ax, [es:0x21 * 4]
    mov [w16_old21], ax
    mov ax, [es:0x21 * 4 + 2]
    mov [w16_old21 + 2], ax
    mov word [es:0x21 * 4], w16_i21
    mov [es:0x21 * 4 + 2], cs
    mov ax, [es:0x2F * 4]           ; ...the multiplex (SPEC.md 124.2.1)
    mov [w16_old2f], ax
    mov ax, [es:0x2F * 4 + 2]
    mov [w16_old2f + 2], ax
    mov word [es:0x2F * 4], w16_i2f
    mov [es:0x2F * 4 + 2], cs
%ifdef PKG_MEGA
    mov ax, [es:0x31 * 4]           ; ...and DPMI's (SPEC.md 124.31.6)
    mov [w16_old31], ax
    mov ax, [es:0x31 * 4 + 2]
    mov [w16_old31 + 2], ax
    mov word [es:0x31 * 4], w16_i31
    mov [es:0x31 * 4 + 2], cs
%endif
    pop es
    pop ax
    jmp w16x_on                     ; and the x87's (SPEC.md 124.5)

w16_i21_off:
    push ax
    push es
    W16SEG0 ax, es
    mov ax, [w16_old21]
    mov [es:0x21 * 4], ax
    mov ax, [w16_old21 + 2]
    mov [es:0x21 * 4 + 2], ax
    mov ax, [w16_old2f]
    mov [es:0x2F * 4], ax
    mov ax, [w16_old2f + 2]
    mov [es:0x2F * 4 + 2], ax
%ifdef PKG_MEGA
    mov ax, [w16_old31]
    mov [es:0x31 * 4], ax
    mov ax, [w16_old31 + 2]
    mov [es:0x31 * 4 + 2], ax
%endif
    pop es
    pop ax
    jmp w16x_off
