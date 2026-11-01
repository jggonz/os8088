; =============================================================================
; os8088 - apps/win16/w16task.inc
;
; TASKS (SPEC.md 124.28, docs/plans/WIN16-ANY-PLAN.md S21): one WIN16
; instance, several Windows programs. The current task's state is the bss's
; own variables, as it always was; every other task's is parked in a block
; of its own, and a switch EXCHANGES the two (124.28.2). The list of what is
; exchanged is w16_ctxtab, below. All of it is part 9's (124.24.1): the image
; had 898 bytes when it came. A handler here is W16SYS, near, ending in ret.
; =============================================================================

section .w16sys

%if W16C_SIZE != 32
  %error "w16u_regclass's class index is a shift by 5"
%endif

; -----------------------------------------------------------------------------
; The context list (SPEC.md 124.28.2): the bss ranges that belong to a task,
; each as its start and its length. What lives across a wait (w16_tohost)
; is here; what lives inside one handler is the instance's. THE QUEUE IS
; FIRST, so it is at the start of every context block (w16t_qpost)
; -----------------------------------------------------------------------------
%macro W16CTX 2
    dw %1, (%2) - (%1)
%endmacro
w16_ctxtab:
    W16CTX w16_q, w16_tmr           ; the queue, its quit flag and code
    W16CTX w16_state, w16_locked    ; W16S_*
    W16CTX w16_name, w16_l1         ; the name, its folder and volume
    W16CTX w16_fseg, w16_cpu186     ; the NE tables, segments, DGROUP, PSP
    W16CTX w16_cmb, w16_ldk         ; the code cache (SPEC.md 124.26)
    W16CTX w16_stop, w16_hss        ; ...its stack top, budget and census
    W16CTX w16_pss, w16_dhseg       ; the coroutine: stack, registers, wait
    W16CTX w16_exit, w16_mbtext     ; AH=4Ch's code
    W16CTX w16_cbax, w16_mvcx       ; a callback's answer; the wait's kind
    W16CTX w16_msg, w16_mx          ; the message being delivered
    W16CTX w16_dcon, w16_gcol       ; a DC open across a wait (124.25.5)
    W16CTX w16_dchide, w16m_st      ; ...its state, and the DCs under it
    W16CTX w16_mdsave, w16_odseg    ; ...a memory DC's
    W16CTX w16_odcur, w16_pgn       ; ...an own DC's, BeginPaint's
    W16CTX w16_svn, w16_dtr         ; SaveDC's states
    W16CTX w16_nmiss, w16_arena     ; its missing imports, ne_heap
    W16CTX w16_gnproc, w16_gflg     ; GlobalNotify's procedure
    W16CTX w16_clus, w16_rtk        ; its volume's cluster, its resources
    W16CTX w16_msys, w16_mclip      ; GetSystemMenu's
    W16CTX w16_mpl, w16_ahwnd       ; the menu loop it waits in
    W16CTX w16_closeq, w16_ctop     ; a WM_CLOSE not taken yet
    W16CTX w16_fpm, w16_fpseg       ; the x87's mode and fixups
    W16CTX w16x_hand, w16x_sw       ; ...the runtime's handler (124.5)
    W16CTX w16_cderr, w16_shdoc     ; a common dialog it waits in
    W16CTX w16_nfdefer, w16_svn     ; the focus notifications owed
    W16CTX w16_inmodal, w16_seq     ; the modal dialog it waits in
    W16CTX w16_odq, w16_cbdl        ; owner-drawn items owed
    W16CTX w16_widx, w16_mirec      ; the import waiting in Get/PeekMessage
    dw 0

; w16t_xchg - ES = a context block: every range of the list exchanged with
; it, the block's bytes in the order of the list. Keeps every register
w16t_xchg:
    push si
    mov si, w16_ctxtab              ; XSEC-OK: read through cs:
    jmp short w16t_xall

; w16t_qxchg - ES = a context block: the queue's range alone exchanged with
; its start (the list's first). Keeps every register
w16t_qxchg:
    push si
    mov si, w16_ctxtab              ; XSEC-OK: read through cs:
    push ax
    push cx
    push di
    xor di, di
    call w16t_xone
    jmp short w16t_xout

w16t_xall:
    push ax
    push cx
    push di
    xor di, di
.r:
    cmp word [cs:si], 0
    je w16t_xout
    call w16t_xone
    jmp short .r
w16t_xout:
    pop di
    pop cx
    pop ax
    pop si
    ret

; w16t_xone - [cs:SI] = a range of the list, ES:DI = its place in the block:
; exchanged, a word at a time and an odd byte last. SI and DI move past it.
; Clobbers AX, CX
w16t_xone:
    mov cx, [cs:si + 2]
    push si
    mov si, [cs:si]
    shr cx, 1
    jcxz .b
.w:
    mov ax, [si]
    xchg ax, [es:di]
    mov [si], ax
    inc si
    inc si
    inc di
    inc di
    loop .w
.b:
    jnc .done                       ; (the shift's carry: an odd length)
    mov al, [si]
    xchg al, [es:di]
    mov [si], al
    inc di
.done:
    pop si
    add si, 4
    ret

; w16t_find - AX = a task's handle -> SI = its record; CF=1 no running task
; has it. Keeps the rest
w16t_find:
    push cx
    mov si, w16_task
    mov cx, W16_MAXTASK
.f:
    cmp byte [si + W16K_ST], W16KS_FREE
    je .n
    cmp [si + W16K_H], ax
    je .y
.n:
    add si, W16K_SIZE
    loop .f
    pop cx
    stc
    ret
.y:
    pop cx
    clc
    ret

; w16t_hof - AL = a task's index + 1 (0 none) -> AX = its handle, 0 none.
; Keeps the rest
w16t_hof:
    push si
    xor ah, ah
    or al, al
    jz .out
    dec ax
    mov ah, W16K_SIZE
    mul ah
    mov si, ax
    mov ax, [w16_task + si + W16K_H]
.out:
    pop si
    ret

; GetNumTasks (KERNEL.152): the tasks running
W16SYS w16t_ntasks
    push cx
    push si
    xor ax, ax
    mov si, w16_task
    mov cx, W16_MAXTASK
.n:
    cmp byte [si + W16K_ST], W16KS_FREE
    je .s
    inc ax
.s:
    add si, W16K_SIZE
    loop .n
    pop si
    pop cx
    ret

; IsTask (KERNEL.320) - args: +0 hTask. TRUE for a running task's handle
W16SYS w16t_istask
    push si
    ICALL w16_args
    mov ax, [es:bx]
    call w16t_find
    mov ax, 0
    jc .out
    inc ax
.out:
    pop si
    ret

; GetWindowTask (USER.224) - args: +0 hWnd. Its task's handle, 0 none
W16SYS w16t_wndtask
    push si
    ICALL w16_args
    mov ax, [es:bx]
    ICALL w16_hwnd2rec
    mov si, ax
    mov ax, 0
    jc .out
    sub si, W16_HWND0
    shr si, 1
    shr si, 1
    mov al, [w16_wtask + si]
    call w16t_hof
.out:
    pop si
    ret

; PostAppMessage (USER.116) - args: +0 lParam, +4 wParam, +6 wMsg, +8 hTask:
; posted to that task's queue with no window (SPEC.md 124.28.6). TRUE
W16SYS w16t_postapp
    push si
    ICALL w16_args
    ICALL w16_smargs                ; [w16_sm_*]; the handle where hWnd goes
    mov ax, [w16_sm_hwnd]
    mov word [w16_sm_hwnd], 0
    call w16t_find
    jc .no
    call w16t_qpost
    jc .no
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    ret

; w16t_qpost - SI = a task's record, [w16_sm_*] a message: posted at the back
; of its queue - in the bss for the current task, in its context block for a
; parked one, by exchanging the queue alone around the post (124.28.4).
; CF=1 the queue is full. Keeps all but AX
w16t_qpost:
    push es
    mov es, [si + W16K_CTX]
    cmp word [si + W16K_CTX], 0
    je .live
    call w16t_qxchg
    xor al, al
    ICALL w16_post
    pushf
    call w16t_qxchg
    popf
    jmp short .out
.live:
    xor al, al
    ICALL w16_post
.out:
    pop es
    ret

section .text
