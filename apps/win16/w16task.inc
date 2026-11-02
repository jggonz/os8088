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
%define W16CTX_AT 0
%macro W16CTX 2                 ; a range, and %1_bo: where it starts in a
    dw %1, (%2) - (%1)          ;   context block
%1_bo equ W16CTX_AT
%xdefine W16CTX_AT (%1_bo + (%2) - (%1))
%endmacro
%define W16BO(v, s) (s %+ _bo + (v) - (s))  ; variable v of range s, in a block
w16_ctxtab:
    W16CTX w16_q, w16_tmr           ; the queue, its quit flag and code
    W16CTX w16_state, w16_locked    ; W16S_*
    W16CTX w16_name, w16_l1         ; the name, its folder and volume
    W16CTX w16_fseg, w16_cpu186     ; the NE tables, segments, DGROUP, PSP
    W16CTX w16_cmb, w16_ldk         ; the code cache (SPEC.md 124.26)
    W16CTX w16_stop, w16_hss        ; ...its stack top, budget and census
    W16CTX w16_pss, w16_i21ss       ; the coroutine: stack, registers, wait;
                                    ;   its DOS (SPEC.md 124.21)
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
    W16CTX w16_show, w16_task       ; its nCmdShow
    dw 0
W16_CTXSZ equ W16CTX_AT             ; a context block's bytes

%if W16_MAXTASK != 4
  %error "w16t_turns walks the table with an AND"
%endif

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

; w16t_rec - AL = a task's index -> SI = its record. Keeps the rest
w16t_rec:
    push ax
    mov ah, W16K_SIZE
    mul ah
    add ax, w16_task
    mov si, ax
    pop ax
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
    or al, al
    jz .none
    dec ax
    call w16t_rec
    mov ax, [si + W16K_H]
    pop si
    ret
.none:
    xor ax, ax
    pop si
    ret

; w16t_hwtask - AX = an HWND -> AL = its task's index + 1, 0 none (124.28.3).
; Keeps all but AH
w16t_hwtask:
    push bx
    ICALL w16_hwnd2rec              ; BX = its record, AX kept; CF=1 none
    jc .no
    sub ax, W16_HWND0
    shr ax, 1
    shr ax, 1
    mov bx, ax
    mov al, [w16_wtask + bx]
    pop bx
    ret
.no:
    xor al, al
    pop bx
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
    ICALL w16_args
    mov ax, [es:bx]
    call w16t_hwtask
    jmp short w16t_hof

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
    xor al, al                      ; at the back
    call w16t_qpost
    jc .no
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    ret

; w16t_qpost - SI = a task's record, AL and [w16_sm_*] as w16_postq takes
; them: posted to its queue - in the bss for the current task, in its context
; block for a parked one, by exchanging the queue alone around the post
; (124.28.4). CF=1 the queue is full. Keeps all but AX
w16t_qpost:
    push es
    cmp word [si + W16K_CTX], 0
    je .live
    mov es, [si + W16K_CTX]
    call w16t_qxchg
    ICALL w16_postq
    pushf
    call w16t_qxchg
    popf
    jmp short .out
.live:
    ICALL w16_postq
.out:
    pop es
    ret

; w16t_post - w16_post when several tasks run: AL and [w16_sm_*] as it takes
; them, posted to the queue of the window's task (124.28.4). A parked task's
; turn is asked for with a wake, since the post may be made inside a run
; whose callback runs no turns. CF=1 the queue is full
W16SYS w16t_post
    push si
    push ax
    mov ax, [w16_sm_hwnd]
    call w16t_hwtask                ; AL = its task + 1
    dec al
    js .here                        ; none: the current task's
    cmp al, [w16_cur]
    je .here
    call w16t_rec
    pop ax
    call w16t_qpost
    pushf
    push bx
    mov bx, [w16_win]
    call OSAPI_WM_WAKE
    pop bx
    popf
    pop si
    ret
.here:
    pop ax
    pop si
    ICALL w16_postq
    ret

; -----------------------------------------------------------------------------
; w16t_to - AL = a task's index: made the current task, its context
; exchanged in and the leaving task's out into the same block (124.28.2).
; Host code only, with no run under way. Keeps every register
; -----------------------------------------------------------------------------
w16t_to:
    cmp al, [w16_cur]
    je .ret
    push ax
    push si
    push es
    call w16t_rec
    mov es, [si + W16K_CTX]
    call w16t_xchg
    mov word [si + W16K_CTX], 0
    xchg al, [w16_cur]
    call w16t_rec                   ; the task that left
    mov [si + W16K_CTX], es
    pop es
    pop si
    pop ax
.ret:
    ret

; w16t_cbin - SI = an os8088 window: the task whose top-level window it
; shows made current, for the callback about to run (124.28.4). The image's
; w16_cbin calls it only when several tasks run and none is under way.
; Keeps every register
W16SYS w16t_cbin
    push ax
    push bx
    push cx
    mov bx, w16_wnd
    xor cx, cx
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    cmp [bx + W16W_OSW], si
    je .y
.n:
    add bx, W16W_SIZE
    inc cx
    cmp cx, W16_MAXWND
    jb .w
    jmp short .out
.y:
    mov bx, cx
    mov al, [w16_wtask + bx]
    dec al
    js .out
    call w16t_to
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16t_turns - the tasks that have work, run in turn (SPEC.md 124.28.4):
; from w16_after, when a callback's run has come back with the current task
; waiting. Each pass looks at every parked task in the table's order after
; the current one; a pass that ran none ends the turns, and so does a budget
; of runs, so a task that has work no run can serve cannot hold the machine.
; A run's own w16_after finds the turns busy and leaves them to this one.
; -----------------------------------------------------------------------------
W16SYS w16t_turns
    cmp byte [cs:w16t_busy], 0
    jne .ret
    cmp byte [w16_inprog], 0
    jne .ret
    inc byte [cs:w16t_busy]
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov cx, W16_MAXTASK * 4
.round:
    mov bl, [w16_cur]               ; BL = the pass's first, BH = runs in it
    xor bh, bh
    mov dl, 1
.step:
    mov al, bl
    add al, dl
    and al, W16_MAXTASK - 1
    call w16t_rec
    cmp al, [w16_cur]               ; the current task waited with nothing
    jne .parked                     ;   to do - unless it has never run,
    cmp byte [si + W16K_ST], W16KS_NEW ; which a task's end can leave
    jne .next                       ;   current (w16t_end)
    jmp short .serve
.parked:
    cmp byte [si + W16K_ST], W16KS_FREE
    je .next
    mov es, [si + W16K_CTX]
    call w16t_work
    jnc .next
.serve:
    call w16t_serve
    inc bh
    cmp byte [w16_state], W16S_ENDED
    je .done
    dec cx
    jz .done
.next:
    inc dl
    cmp dl, W16_MAXTASK
    jbe .step
    or bh, bh
    jnz .round
.done:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    dec byte [cs:w16t_busy]
.ret:
    ret

; w16t_work - SI = a parked task's record, ES = its context block: CF=1 it
; has work - it has just started, waits in PeekMessage, or waits for a
; message (GetMessage, a dialog's loop) and has one: posted, WM_QUIT, a paint
; owed one of its windows or a timer of its own fired. Keeps every register
w16t_work:
    cmp byte [si + W16K_ST], W16KS_NEW
    je .yes
    cmp byte [es:W16BO(w16_state, w16_state)], W16S_ENDED
    je .yes                         ; (it ended in a directed run, w16t_drun)
    cmp byte [es:W16BO(w16_inpeek, w16_cbax)], 0
    jne .yes
    cmp byte [es:W16BO(w16_inget, w16_cbax)], 0
    jne .msgs
    cmp byte [es:W16BO(w16_inmodal, w16_inmodal)], 0
    je .no
.msgs:
    cmp byte [es:W16BO(w16_qcount, w16_q)], 0
    jne .yes
    cmp byte [es:W16BO(w16_quit, w16_q)], 0
    jne .yes
    push ax
    push bx
    push cx
    mov ax, si
    sub ax, w16_task
    mov cl, W16K_SIZE
    div cl
    inc ax                          ; AL = the owner byte, the index + 1
    mov bx, w16_wnd
    xor cx, cx
.w:
    mov ah, [bx + W16W_FLAGS]
    and ah, W16WF_USED | W16WF_INVAL | W16WF_DYING
    cmp ah, W16WF_USED | W16WF_INVAL
    jne .wn
    push bx
    mov bx, cx
    cmp [w16_wtask + bx], al
    pop bx
    je .ypop
.wn:
    add bx, W16W_SIZE
    inc cx
    cmp cx, W16_MAXWND
    jb .w
    mov bx, w16_tmr
    mov cx, W16_MAXTMR
.t:
    cmp [bx + W16T_USED], al        ; a timer's USED byte is its task + 1
    jne .tn
    cmp byte [bx + W16T_FIRED], 0
    jne .ypop
.tn:
    add bx, W16T_SIZE
    loop .t
    pop cx
    pop bx
    pop ax
.no:
    clc
    ret
.ypop:
    pop cx
    pop bx
    pop ax
.yes:
    stc
    ret

; w16t_serve - AL = a parked task with work: made current and run - its
; first instruction, its PeekMessage answered, or w16_kick - then what every
; run's caller does (w16_after). Keeps BX, CX, DX
w16t_serve:
    push bx
    push cx
    push dx
    push si
    push di
    call w16t_to
    call w16t_rec
    cmp byte [w16_state], W16S_ENDED
    je .after                       ; w16_after ends it (w16t_end)
    cmp byte [si + W16K_ST], W16KS_NEW
    jne .old
    mov byte [si + W16K_ST], W16KS_RUN
    xor ax, ax                      ; the first run, as w16_wake's
    xor dx, dx
    ICALL w16_resume
    jmp short .after
.old:
    cmp byte [w16_inpeek], 0
    je .kick
    ICALL w16_peekwake              ; (it runs w16_after itself)
    jmp short .out
.kick:
    ICALL w16_kick
.after:
    ICALL w16_after
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16t_xsend - w16_sendwnd's, when several tasks run: BX = the window, SI =
; the continuation, AX = its word, [w16_sm_*] the message. Inside a run, a
; window of another task (SPEC.md 124.28.6): its procedure is run on its own
; stack - the receiver made current, the send queued on it as a host send
; is, and run, nested inside the sender's call, until the receiver waits
; again - and the sender is made current again with the answer, and its
; continuation run with it. CF=0 that was done, AX:DX as the continuation
; answers. CF=1 the current task's window, or no run: every register kept
; -----------------------------------------------------------------------------
W16SYS w16t_xsend
    cmp byte [w16_inprog], 0
    je .mine
    push ax
    mov ax, bx
    ICALL w16_rec2hwnd              ; AX = its HWND
    call w16t_hwtask                ; AL = its task + 1
    dec al
    js .minep
    cmp al, [w16_cur]
    jne .other
.minep:
    pop ax
.mine:
    stc
    ret
.other:
    pop cx                          ; CX = the continuation's word
    push si
    push cx
    mov ah, [w16_cur]               ; AH = the sender, AL = the receiver
    push ax
    push word [w16_hss]             ; the sender's run's: a nested run sets
    push word [w16_hsp]             ;   them, and the sender's own return
    push word [w16_hflags]          ;   to the host needs them back
    call w16t_to
    mov al, [w16_pend]              ; the receiver waits in an import: its
    push ax                         ;   own, as it was, after the run
    mov si, w16_cont_xs             ; XSEC-OK: an image continuation
    xor ax, ax
    ICALL w16_sendwnd               ; queued on the receiver (it is current)
    ICALL w16_resumecb              ; ...and run until it waits again
    pop ax
    mov [w16_pend], al
    pop word [w16_hflags]
    pop word [w16_hsp]
    pop word [w16_hss]
    pop ax
    mov al, ah
    call w16t_to                    ; the sender, whose call is still being
    mov byte [w16_inprog], 1        ;   served
    pop cx
    pop si
    mov ax, [w16_sm_lp]             ; the answer, which w16_cont_xs kept
    mov dx, [w16_sm_lp + 2]
    push es
    push ax
    mov ax, KERNEL_SEG              ; a continuation's ES, as the gate's
    mov es, ax
    pop ax
    ICALL si
    pop es
    clc
    ret

; -----------------------------------------------------------------------------
; w16t_end - the current task has ended and others run (SPEC.md 124.28.7):
; its windows, classes, timers and hooks go without a message, its DOS, its
; floating point, its code and its blocks are given back, and the next task
; in the table is made current, its context exchanged in over the dead one's
; and the block freed. The instance's own window, if it was the dead task's,
; is handed to a window of the rest (w16t_hand). Host code, no run under way
; -----------------------------------------------------------------------------
W16SYS w16t_end
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [w16_cur]
    inc ax
    mov [cs:w16t_dead], al
    xor cx, cx                      ; its windows
.w:
    mov bx, cx
    mov al, [cs:w16t_dead]
    cmp [w16_wtask + bx], al
    jne .wn
    mov byte [w16_wtask + bx], 0
    ICALL w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .wn
    push cx
    ICALL w16_wndfree
    pop cx
.wn:
    inc cx
    cmp cx, W16_MAXWND
    jb .w
    mov bx, w16_cls                 ; its classes
    xor si, si
.c:
    mov al, [cs:w16t_dead]
    cmp [w16_ctask + si], al
    jne .cn
    mov byte [w16_ctask + si], 0
    mov byte [bx + W16C_NAME], 0
.cn:
    add bx, W16C_SIZE
    inc si
    cmp si, W16_MAXCLS
    jb .c
    mov bx, w16_tmr                 ; its timers with no window
    mov cx, W16_MAXTMR
.t:
    mov al, [cs:w16t_dead]
    cmp [bx + W16T_USED], al
    jne .tn
    mov byte [bx + W16T_USED], 0
.tn:
    add bx, W16T_SIZE
    loop .t
    mov bx, w16_hooks               ; its hooks: a procedure in its segments
    mov cx, W16_HKMAX
.h:
    mov ax, [bx + 4]
    cmp ax, [w16_pseg]
    jb .hn
    cmp ax, [w16_ptop]
    jae .hn
    xor ax, ax
    mov [bx], ax
    mov [bx + 2], ax
    mov [bx + 4], ax
.hn:
    add bx, 6
    loop .h
    ICALL w16_dosend                ; its files closed, its DOS freed
    ICALL w16x_end                  ; its floating point
    call w16t_codefree
    mov al, [cs:w16t_dead]
    call w16t_gfree
    dec ax                          ; (AL = its index again)
    call w16t_rec
    mov dx, [w16_pseg]              ; its segments: a claim, or a block
    or dx, dx
    jz .seg
    cmp byte [si + W16K_FL], 0
    jne .blk
    call OSAPI_MEM_FREE
    jmp short .seg
.blk:
    ICALL w16_kfree
.seg:
    mov byte [si + W16K_ST], W16KS_FREE
    dec byte [w16_ntk]
    mov di, si                      ; DI = its record
    mov al, [w16_cur]               ; the next in the table
.nx:
    inc al
    and al, W16_MAXTASK - 1
    call w16t_rec
    cmp byte [si + W16K_ST], W16KS_FREE
    je .nx
    mov es, [si + W16K_CTX]         ; the next one's context in, the dead
    call w16t_xchg                  ;   one's out, and its block freed
    mov word [si + W16K_CTX], 0
    mov word [di + W16K_CTX], 0
    mov [w16_cur], al
    mov ax, es
    ICALL w16_gfreeh
    cmp byte [w16_ntk], 1           ; the last left, its segments a block:
    jne .hand                       ;   they go with the arenas when it ends
    cmp byte [si + W16K_FL], 0      ;   (w16_freeprog frees only a claim)
    je .hand
    mov word [w16_pseg], 0
.hand:
    call w16t_hand
    call w16t_turns                 ; (the count may be one now, and the one
    pop es                          ;   left may never have run)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16t_codefree - the current task's discardable code segments that are
; loaded, and the module block that lists them (SPEC.md 124.26.4), freed.
; Clobbers AX, BX, CX, ES
w16t_codefree:
    cmp word [w16_cmb], 0
    je .out
    mov es, [w16_clive]
    xor bx, bx
    mov cx, [w16_cnseg]
    jcxz .blk
.s:
    test byte [es:bx + W16CL_FL], W16CLF_DISC
    jz .n
    mov ax, [es:bx + W16CL_BASE]
    or ax, ax
    jz .n
    ICALL w16_gfreeh                ; (a fixed block: its handle is its base)
.n:
    add bx, W16CL_SIZE
    loop .s
.blk:
    push dx
    mov dx, [w16_cmb]
    ICALL w16_kfree
    pop dx
    mov word [w16_cmb], 0
.out:
    ret

; w16t_gfree - AL = a task's owner byte: every block of the global heap it
; owns (GH_TASK) freed. Keeps every register
w16t_gfree:
    push bx
    push dx
    push si
    push es
    mov dl, al
    xor si, si
.arena:
    cmp si, 2 * W16_NARENA
    jae .done
    mov bx, [w16_arena + si]
    or bx, bx
    jz .nexta
.blk:
    cmp bx, [w16_arenae + si]
    jae .nexta
    mov es, bx
    test byte [es:GH_FLAGS], GHF_USED
    jz .skip
    cmp [es:GH_TASK], dl
    jne .skip
    push ax
    mov ax, [es:GH_HANDLE]
    ICALL w16_gfreeh
    pop ax
    jmp short .arena                ; (merged, and the arena may be gone)
.skip:
    add bx, [es:GH_SIZE]
    jmp short .blk
.nexta:
    inc si
    inc si
    jmp short .arena
.done:
    pop es
    pop si
    pop dx
    pop bx
    ret

; w16t_hand - WIN16's own window, when it shows no top-level window now, takes
; one of the current task's that has an os8088 window of its own (124.28.7):
; that window closes, and WIN16's is titled, sized and painted as it. With
; none, it shows the current program's name. Clobbers AX, BX, CX, DX, SI, DI
w16t_hand:
    cmp byte [w16_top], 0
    jne .out
    mov bx, w16_wnd
    xor cx, cx
.f:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    mov ax, [bx + W16W_OSW]
    or ax, ax
    jz .n
    cmp ax, [w16_win]
    je .n
    cmp byte [bx + W16W_KIND], 0    ; a window, not a dialog
    jne .n
    push bx
    mov bx, cx
    mov dl, [w16_wtask + bx]
    pop bx
    dec dl
    cmp dl, [w16_cur]
    je .take
.n:
    add bx, W16W_SIZE
    inc cx
    cmp cx, W16_MAXWND
    jb .f
    mov si, w16_name                ; none: the program's name on WIN16's
    mov di, w16_l1                  ;   own lines, until its first top-level
    ICALL w16_strcpy                ;   window takes the window (w16_mkwnd)
    ICALL w16_repaint
.out:
    ret
.take:
    inc cx
    mov [w16_top], cl
    ICALL w16_ttlfree
    mov dx, [w16_win]
    mov [bx + W16W_OSW], dx
    ICALL w16_lock
    push bx
    mov bx, ax
    call OSAPI_WM_DESTROY           ; its own os8088 window
    pop bx
    mov di, w16_wtitle
    ICALL w16_ttlcopy
    mov word [w16_titleptr], w16_wtitle
    push bx
    mov bx, [w16_win]
    mov al, 1                       ; W_PAINT paints every pixel
    call OSAPI_WM_OWNBG
    mov ax, w16_wtitle
    call OSAPI_WM_TITLE
    pop bx
    ICALL w16_unlock
    ICALL w16_oswshow               ; in front, fitted to its client
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    ICALL w16_invaltree
    ret

; -----------------------------------------------------------------------------
; WinExec (KERNEL.166) - args: +0 nCmdShow, +2 lpCmdLine. The program its
; first word names, started as a task of this instance (SPEC.md 124.28.5),
; the rest of the line its command tail. hInstance, or Windows' answer
; below 32
; -----------------------------------------------------------------------------
W16SYS w16t_winexec
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx]
    mov [cs:w16t_sh], ax
    les di, [es:bx + 2]
.sp:
    cmp byte [es:di], ' '           ; leading spaces
    jne .name
    inc di
    jmp short .sp
.name:
    mov [cs:w16t_np], di
    mov [cs:w16t_np + 2], es
.tok:
    mov al, [es:di]
    or al, al
    jz .tail
    cmp al, ' '
    je .tail
    inc di
    jmp short .tok
.tail:
    cmp byte [es:di], ' '           ; its leading spaces go (InitTask16)
    jne .tail1
    inc di
    jmp short .tail
.tail1:
    mov [cs:w16t_tp], di
    mov [cs:w16t_tp + 2], es
    xor cx, cx
.len:
    cmp byte [es:di], 0
    je .go
    inc di
    inc cx
    cmp cx, 126                     ; a PSP's tail holds 126 and its CR
    jb .len
.go:
    mov [cs:w16t_tn], cx
    call w16t_exec
    pop di
    pop si
    ret

; LoadModule (KERNEL.45) - args: +0 lpParameterBlock, +4 lpModuleName. A
; program started as WinExec starts one: the block's command tail (a count,
; then the text) and its nCmdShow (the second word lpCmdShow points at)
W16SYS w16t_loadmod
    push si
    push di
    ICALL w16_args
    push word [es:bx + 2]           ; the block
    push word [es:bx]
    les di, [es:bx + 4]
    mov [cs:w16t_np], di
    mov [cs:w16t_np + 2], es
    pop di
    pop es
    mov word [cs:w16t_sh], 1        ; SW_SHOWNORMAL
    mov word [cs:w16t_tn], 0
    mov ax, es
    or ax, di
    jz .go
    push es
    push di
    les si, [es:di + 6]             ; lpCmdShow
    mov ax, es
    or ax, si
    jz .nosh
    mov ax, [es:si + 2]
    mov [cs:w16t_sh], ax
.nosh:
    pop di
    pop es
    les di, [es:di + 2]             ; lpCmdLine
    mov ax, es
    or ax, di
    jz .go
    mov cl, [es:di]
    xor ch, ch
.lsp:
    jcxz .n
    inc di
    cmp byte [es:di], ' '           ; its leading spaces go (InitTask16)
    jne .n0
    dec cx
    jmp short .lsp
.n0:
    cmp cx, 126
    jbe .n
    mov cx, 126
.n:
    mov [cs:w16t_tp], di
    mov [cs:w16t_tp + 2], es
    mov [cs:w16t_tn], cx
.go:
    call w16t_exec
    pop di
    pop si
    ret

; w16t_shexe - ShellExecute's (SPEC.md 124.24.10), its file found: an .EXE
; that WIN16 runs is started as a task of this instance (124.28.5), as
; WinExec starts one, with lpParameters as its tail and nShowCmd. out: CF=0
; AX = its hInstance, or SE_ERR_OOM; CF=1 not an .EXE, or not one WIN16
; runs, or not where a task's file is looked for - the caller posts it
w16t_shexe:
    push bx
    push cx
    push si
    push di
    push es
    ICALL w16_args
    les di, [es:bx + 10]            ; lpFile: ".EXE" at its end?
    mov [cs:w16t_np], di
    mov [cs:w16t_np + 2], es
    xor cx, cx
.end:
    cmp byte [es:di], 0
    je .ended
    inc di
    inc cx
    jmp short .end
.ended:
    cmp cx, 4
    jb .no
    mov ax, [es:di - 4]
    and ah, 0xDF
    cmp ax, 'E' * 256 + '.'
    jne .no
    mov ax, [es:di - 2]
    and ax, 0xDFDF
    cmp ax, 'E' * 256 + 'X'
    jne .no
    ICALL w16_args
    mov ax, [es:bx]
    mov [cs:w16t_sh], ax
    les di, [es:bx + 6]             ; lpParameters, leading spaces gone
    xor cx, cx
    mov ax, es
    or ax, di
    jz .tail
.sp:
    cmp byte [es:di], ' '
    jne .tail
    inc di
    jmp short .sp
.tail:
    mov [cs:w16t_tp], di
    mov [cs:w16t_tp + 2], es
    mov ax, es
    or ax, di
    jz .go
.len:
    cmp byte [es:di], 0
    je .go
    inc cx
    inc di
    cmp cx, 126
    jb .len
.go:
    mov [cs:w16t_tn], cx
    call w16t_exec
    cmp ax, 32
    jae .yes
    cmp ax, 2                       ; not found where a task's file is looked
    je .no                          ;   for, or not one WIN16 runs: posted
    cmp ax, 11
    je .no
    mov ax, SE_ERR_OOM
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16t_help - WinHelp's (USER.171), from the image's handler: ES:BX = its
; arguments, +0 dwData, +4 wCommand, +6 lpHelpFile, +10 hWnd. Windows 3.x's
; own way (SPEC.md 124.28.10): the window of class MS_WINHELP is sent the
; registered message WM_WINHELP with a block saying what to show - and when
; there is no such window, WINHELP.EXE -x is started as a task of this
; instance and run until it waits, which makes one. out: CF=1 WINHELP.EXE
; could not be started (the handler's message box says so); CF=0 AX = FFFFh
; the arguments at ES:BX are SendMessage's now, else AX = the answer
; -----------------------------------------------------------------------------
HELP_QUIT       equ 2
HELP_KEY        equ 0x0101
HELP_COMMAND    equ 0x0102
HELP_PARTIALKEY equ 0x0105
HELP_MULTIKEY   equ 0x0201
HELP_SETWINPOS  equ 0x0203

%assign w16t_wmh 0              ; RegisterWindowMessage("WM_WINHELP"), by
%strlen w16t_n 'WM_WINHELP'     ;   w16u_regmsg's own hash (124.20.4)
%assign w16t_i 1
%rep w16t_n
  %substr w16t_c 'WM_WINHELP' w16t_i
  %assign w16t_wmh (((w16t_wmh << 5) | (w16t_wmh >> 11)) & 0xFFFF) ^ w16t_c
  %assign w16t_i w16t_i + 1
%endrep
WM_WINHELP equ (w16t_wmh & 0x3FFF) | 0xC000

w16t_help:
    push cx
    push dx
    push si
    push di
    call w16t_hfind                 ; AX = the help window
    jnc .have
    mov ax, 1                       ; HELP_QUIT with no Help running: done
    cmp word [es:bx + 4], HELP_QUIT
    je .out
    push es
    push bx
    mov word [cs:w16t_np], w16t_s_help  ; XSEC-OK: a part 9 string, cs:
    mov [cs:w16t_np + 2], cs
    mov word [cs:w16t_tp], w16t_s_x     ; XSEC-OK: ...and its tail
    mov [cs:w16t_tp + 2], cs
    mov word [cs:w16t_tn], 2
    mov word [cs:w16t_sh], 1        ; SW_SHOWNORMAL
    call w16t_exec
    cmp ax, 32
    jb .none
    mov al, [cs:w16t_new]           ; its first run, now: it makes the window
    call w16t_drun
    pop bx
    pop es
    call w16t_hfind
    jnc .have
    xor ax, ax                      ; it made none: FALSE
    jmp .out
.none:
    pop bx
    pop es
    stc
    jmp .ret
.have:
    push ax                         ; the help window
    xor cx, cx                      ; CX = the file's bytes with its NUL
    push es
    les di, [es:bx + 6]
    call w16t_slen
    pop es
    mov si, cx
    xor cx, cx                      ; ...SI, and CX = the data's
    mov ax, [es:bx + 4]
    push es
    les di, [es:bx]
    cmp ax, HELP_KEY
    je .str
    cmp ax, HELP_COMMAND
    je .str
    cmp ax, HELP_PARTIALKEY
    je .str
    cmp ax, HELP_MULTIKEY           ; MULTIKEYHELP and HELPWININFO: their
    je .sized                       ;   first word is their size
    cmp ax, HELP_SETWINPOS
    jne .sz
.sized:
    mov ax, es
    or ax, di
    jz .sz
    mov cx, [es:di]
    jmp short .sz
.str:
    call w16t_slen
.sz:
    pop es
    mov ax, [cs:w16t_hblk]          ; the last request's block: Help has
    or ax, ax                       ;   read it by now
    jz .new
    push cx
    push si
    ICALL w16_gfreeh
    pop si
    pop cx
.new:
    mov ax, 16                      ; the WINHELP header, the file, the data
    add ax, si
    add ax, cx
    push ax
    push cx
    xor dx, dx
    xor cl, cl
    ICALL w16_gnew                  ; (fixed: the handle is the segment)
    pop cx
    pop dx                          ; DX = the size
    mov [cs:w16t_hblk], ax
    or ax, ax
    jnz .fill
    pop ax
    xor ax, ax                      ; FALSE
    jmp short .out
.fill:
    push ds
    push es
    push bx
    mov di, ax                      ; ES = the block, DS:BX = the arguments
    push es
    pop ds
    mov es, di
    xor di, di
    cld
    mov ax, dx                      ; +0 size
    stosw
    mov ax, [bx + 4]                ; +2 wCommand
    stosw
    mov ax, [bx]                    ; +4 dwData
    stosw
    mov ax, [bx + 2]
    stosw
    xor ax, ax                      ; +8 reserved
    stosw
    stosw
    mov ax, si                      ; +12 the file's offset, 0 none
    or ax, ax
    jz .nof
    mov ax, 16
.nof:
    stosw
    mov ax, cx                      ; +14 the data's, 0 none
    or ax, ax
    jz .nod
    mov ax, 16
    add ax, si
.nod:
    stosw
    push cx                         ; (a count of 0 loads no pointer: the
    mov cx, si                      ;   data of HELP_CONTEXT is a number)
    jcxz .nofile
    push ds
    lds si, [bx + 6]
    rep movsb
    pop ds
.nofile:
    pop cx
    jcxz .nodata
    lds si, [bx]
    rep movsb
.nodata:
    pop bx
    pop es
    pop ds
    mov ax, [cs:w16t_hblk]          ; SendMessage(help, WM_WINHELP, hWnd,
    mov [es:bx], ax                 ;   MAKELONG(the block, 0))
    mov word [es:bx + 2], 0
    mov ax, [es:bx + 10]
    mov [es:bx + 4], ax
    mov word [es:bx + 6], WM_WINHELP
    pop word [es:bx + 8]
    mov ax, 0xFFFF
.out:
    clc
.ret:
    pop di
    pop si
    pop dx
    pop cx
    ret

; w16t_slen - ES:DI = a string or NULL -> CX = its bytes with the NUL, 0 for
; NULL. Keeps the rest
w16t_slen:
    xor cx, cx
    push ax
    mov ax, es
    or ax, di
    jz .out
    push di
.c:
    inc cx
    cmp byte [es:di], 0
    je .end
    inc di
    jmp short .c
.end:
    pop di
.out:
    pop ax
    ret

; w16t_hfind - CF=0 AX = the top-level window of class MS_WINHELP, of any
; task; CF=1 none. Keeps the rest
w16t_hfind:
    push bx
    push cx
    push si
    push di
    push es
    push cs
    pop es
    xor cx, cx
.w:
    ICALL w16_slot2rec              ; BX = slot CX's record
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .n
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .n
    mov al, [bx + W16W_CLS]
    mov ah, W16C_SIZE
    mul ah
    add ax, w16_cls
    mov si, ax                      ; a class's name: upper case, NUL-padded
    mov di, w16t_s_wh               ; XSEC-OK: part 9's, compared through cs:
    push cx
    mov cx, 16
    cld
    repe cmpsb
    pop cx
    je .y
.n:
    inc cx
    cmp cx, W16_MAXWND
    jb .w
    stc
    jmp short .out
.y:
    ICALL w16_rec2hwnd
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; SendMessage (USER.111) and PostMessage (USER.110) - args: +0 lParam, +4
; wParam, +6 msg, +8 hWnd. HWND_BROADCAST is every task's top-level windows
; (SPEC.md 124.28.6); any other window is the image's w16u_sendmsg's and
; w16u_postmsg's. Here and not there for the image's room, which the
; kern_mega arm has none of
W16SYS w16t_sendmsg
    ICALL w16_args
    cmp word [es:bx + 8], HWND_BROADCAST
    je .bc
    ICALL w16u_sendmsg
    ret
.bc:
    ICALL w16_smargs
    jmp short w16t_bsend

W16SYS w16t_postmsg
    ICALL w16_args
    cmp word [es:bx + 8], HWND_BROADCAST
    je .bc
    ICALL w16u_postmsg
    ret
.bc:
    ICALL w16_smargs
    jmp w16t_bpost

; w16t_bsend - SendMessage to HWND_BROADCAST (USER.111, SPEC.md 124.28.6):
; [w16_sm_msg/wp/lp] sent to every top-level window of every task, in slot
; order, each with its own HWND, as Windows' USER did. Another task's window
; and a WIN16 control are answered at once (w16_sendwnd: w16t_xsend, the
; control's procedure); the caller's own window is a call into the program,
; and the rest of the broadcast is its continuation (w16t_bnext). The
; message rides in that call's extra bytes, not in WIN16's state, since the
; procedure may send or broadcast itself before it returns. The answer is 1
w16t_bsend:
    xor cx, cx
w16t_bfrom:                         ; CX = the first slot
    push bp
    push word [w16_sm_lp + 2]
    push word [w16_sm_lp]
    push word [w16_sm_wp]
    push word [w16_sm_msg]
    mov bp, sp                      ; (the host's stack: SS)
.w:
    cmp cx, W16_MAXWND
    jae .done
    ICALL w16_slot2rec              ; BX = slot CX's record
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .n
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .n
    mov ax, [bp]                    ; the message again: a send before this
    mov [w16_sm_msg], ax            ;   one may have changed every word
    mov ax, [bp + 2]
    mov [w16_sm_wp], ax
    mov ax, [bp + 4]
    mov [w16_sm_lp], ax
    mov ax, [bp + 6]
    mov [w16_sm_lp + 2], ax
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    call w16t_bown
    jc .own
    push cx
    push bp
    mov si, w16_cont_ret            ; XSEC-OK: an image continuation
    xor ax, ax
    ICALL w16_sendwnd               ; answered before it returns
    pop bp
    pop cx
.n:
    inc cx
    jmp short .w
.own:
    mov ax, cx                      ; the continuation's word: this slot
    mov cx, 8
    mov si, w16t_bnext_c            ; XSEC-OK: its image trampoline
    ICALL w16_cbopen                ; ES:DI = the extra bytes
    cld
    mov si, bp
    mov cx, 4
.cp:
    mov ax, [ss:si]
    stosw
    inc si
    inc si
    loop .cp
    ICALL w16_sendgo                ; BX = the window
    add sp, 8
    pop bp
    ret
.done:
    add sp, 8
    pop bp
    mov ax, 1
    xor dx, dx
    ret

; w16t_bown - BX = a window, AX = its HWND: CF=1 it is the caller's own and
; not a WIN16 control, so a send to it is a call into the program
w16t_bown:
    ICALL w16_isctl
    jnc .no
    cmp byte [w16_ntk], 2
    jb .yes
    call w16t_hwtask                ; AL = its task + 1
    dec al
    js .yes
    cmp al, [w16_cur]
    je .yes
.no:
    clc
    ret
.yes:
    stc
    ret

; w16t_bpost - PostMessage to HWND_BROADCAST (USER.110): [w16_sm_*] posted
; to every top-level window of every task, each to its own task's queue
; (w16_post) and with its own HWND. The answer is 1, as Windows'
w16t_bpost:
    push word [w16_sm_hwnd]
    xor cx, cx
.w:
    ICALL w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .n
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .n
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    push cx
    xor al, al                      ; (a full queue: that window misses it)
    cmp byte [w16_ntk], 2
    jb .q
    call w16t_post                  ; its task's queue
    jmp short .p
.q:
    ICALL w16_postq
.p:
    pop cx
.n:
    inc cx
    cmp cx, W16_MAXWND
    jb .w
    pop word [w16_sm_hwnd]
    mov ax, 1
    xor dx, dx
    ret

SCONT w16t_bnext                    ; ...a window of the caller's answered:
    push es                         ;   CX = its slot. The message, out of the
    mov es, [w16_pss]               ;   call's extra bytes, which end where
    mov bx, [w16_psp_]              ;   the served import's frame begins
    mov ax, [es:bx - 8]
    mov [w16_sm_msg], ax
    mov ax, [es:bx - 6]
    mov [w16_sm_wp], ax
    mov ax, [es:bx - 4]
    mov [w16_sm_lp], ax
    mov ax, [es:bx - 2]
    mov [w16_sm_lp + 2], ax
    pop es
    inc cx
    jmp w16t_bfrom

; w16t_drun - AL = a task that has never run, from inside the current task's
; call: its first run, now, nested in that call - a directed yield - until
; it waits or ends (124.28.10). The caller is current again after. A task
; that ended in it is ended by the turns (w16t_work). Keeps BX, CX, DX
w16t_drun:
    push ax
    push si
    mov ah, [w16_cur]
    push ax
    push word [w16_hss]             ; the caller's run's (w16t_xsend's)
    push word [w16_hsp]
    push word [w16_hflags]
    call w16t_to
    call w16t_rec
    mov byte [si + W16K_ST], W16KS_RUN
    push bx
    push cx
    push dx
    xor ax, ax                      ; the first run, as w16t_serve's
    xor dx, dx
    ICALL w16_resume
    pop dx
    pop cx
    pop bx
    pop word [w16_hflags]
    pop word [w16_hsp]
    pop word [w16_hss]
    pop ax
    mov al, ah
    call w16t_to
    mov byte [w16_inprog], 1        ; the caller's call is still being served
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16t_exec - [cs:w16t_np] the program's name (to a space or a NUL),
; [cs:w16t_tp]/[cs:w16t_tn] its command tail, [cs:w16t_sh] its nCmdShow:
; loaded as a new task while the caller waits in the call (124.28.5).
; out: AX = its hInstance, or 0 out of memory, 2 no such file, 11 not a
; program WIN16 runs, 16 every task taken
; -----------------------------------------------------------------------------
w16t_exec:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor al, al                      ; a free record
.free:
    call w16t_rec
    cmp byte [si + W16K_ST], W16KS_FREE
    je .slot
    inc al
    cmp al, W16_MAXTASK
    jb .free
    mov ax, 16
    jmp .out
.slot:
    mov [cs:w16t_new], al
    call w16t_name                  ; [w16_mbtext]; CF=1 none
    jc .nofile
    call w16t_where                 ; DX = its folder, BL = its volume
    jc .nofile
    mov [cs:w16t_dir], dx
    mov [cs:w16t_vol], bl
    mov ax, W16_CTXSZ               ; a block for a context, zero: the new
    xor dx, dx                      ;   task's state, as a launch's bss is
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; (fixed: the handle is the segment)
    or ax, ax
    jz .nomem
    mov [cs:w16t_blk], ax
    mov es, ax
    call w16t_xchg                  ; the caller's state out to it
    mov al, [cs:w16t_new]
    xchg al, [w16_cur]
    mov [cs:w16t_old], al
    mov si, w16_mbtext
    mov di, w16_name
    ICALL w16_strcpy
    mov dx, [cs:w16t_dir]
    mov [w16_dir], dx
    mov bl, [cs:w16t_vol]
    mov [w16_vol], bl
    mov byte [w16_state], W16S_RUN
    mov al, [cs:w16t_sh]
    xor al, 1                       ; (kept as its XOR with SW_SHOWNORMAL)
    mov [w16_show], al
    ICALL w16_ldin                  ; CF=1 refused: what it took, given back
    jc .refused
    push ds                         ; the command tail into its PSP
    mov es, [w16_psp]
    mov di, 0x81
    mov cx, [cs:w16t_tn]
    mov [es:0x80], cl
    lds si, [cs:w16t_tp]
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0             ; NUL-ended: lpCmdLine (InitTask)
    mov al, [w16_cur]
    call w16t_rec
    mov byte [si + W16K_ST], W16KS_NEW
    push word [si + W16K_H]
    mov es, [cs:w16t_blk]           ; the caller back, the new task parked
    call w16t_xchg                  ;   in the block
    mov [si + W16K_CTX], es
    mov al, [cs:w16t_old]
    mov [w16_cur], al
    mov bx, [w16_win]               ; its first run, at the next pass
    call OSAPI_WM_WAKE
    pop ax
    jmp short .home
.refused:
    mov es, [cs:w16t_blk]
    call w16t_xchg
    mov al, [cs:w16t_old]
    mov [w16_cur], al
    mov ax, es
    ICALL w16_gfreeh
    mov ax, 11
.home:
    push ax                         ; the caller's folder again, where its
    mov dx, [w16_dir]               ;   file cells resolve
    mov bl, [w16_vol]
    call OSAPI_FILE_GOTO_QM
    pop ax
    jmp short .out
.nomem:
    xor ax, ax
    jmp short .out
.nofile:
    mov ax, 2
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16t_name - [cs:w16t_np] -> [w16_mbtext]: its file part, past the last \ /
; or :, up to a space or a NUL, upper case, 8.3 at most, and .EXE when it
; has no extension. CF=1 there is no name. Clobbers AX, BX, CX, SI, DI, ES
w16t_name:
    push ds
    pop es
    mov di, w16_mbtext
    push ds
    lds si, [cs:w16t_np]
    mov bx, si
.scan:
    lodsb
    or al, al
    jz .scanned
    cmp al, ' '
    je .scanned
    cmp al, '\'
    je .cut
    cmp al, '/'
    je .cut
    cmp al, ':'
    jne .scan
.cut:
    mov bx, si
    jmp short .scan
.scanned:
    mov si, bx
    xor cx, cx                      ; CL = this part's letters, CH = 1 in
.c:                                 ;   the extension
    lodsb
    or al, al
    jz .end
    cmp al, ' '
    je .end
    cmp al, '.'
    jne .ch
    or ch, ch
    jnz .end
    mov cx, 0x0100
    stosb
    jmp short .c
.ch:
    mov ah, 8
    or ch, ch
    jz .lim
    mov ah, 3
.lim:
    cmp cl, ah
    jae .c
    cmp al, 'a'
    jb .st
    cmp al, 'z'
    ja .st
    sub al, 'a' - 'A'
.st:
    stosb
    inc cl
    jmp short .c
.end:
    pop ds
    cmp di, w16_mbtext
    je .none
    or ch, ch
    jnz .term
    mov ax, '.E'
    stosw
    mov ax, 'XE'
    stosw
.term:
    mov byte [di], 0
    clc
    ret
.none:
    stc
    ret

; w16t_where - the folder holding [w16_mbtext], looked for as Windows looked
; (124.28.5): the caller's own, the Windows directory, SYSTEM under it, then
; the folder WIN16 was started from. out: CF=0 DX = it, BL = its volume;
; CF=1 none has it. Clobbers AX, SI
w16t_where:
    mov dx, [w16_dir]
    mov bl, [w16_vol]
    call w16t_in
    jnc .ret
    ICALL od_enter                  ; SYSTEM\APPDATA, WIN16's volume
    jc .home
    mov si, w16pf_s_dir             ; ...WIN16
    ICALL od_dive
    jc .back
    call w16t_has
    jnc .found
    mov si, od_s_system             ; ...WIN16\SYSTEM
    ICALL od_dive
    jc .back
    call w16t_has
    jnc .found
.back:
    ICALL od_back
.home:
    mov dx, [w16_home]
    mov bl, [w16_homev]
    call w16t_in
.ret:
    ret
.found:
    call OSAPI_FILE_HERE            ; DX, BL: where it stands now
    ICALL od_back
    clc
    ret

; w16t_in - DX = a folder, BL = its volume: CF=0 [w16_mbtext] is there.
; Keeps DX and BL
w16t_in:
    push dx
    push bx
    call OSAPI_FILE_GOTO_QM
    pop bx
    pop dx
    jc w16t_ret
    ; fall into w16t_has

; w16t_has - CF=0 the file [w16_mbtext] is in the folder WIN16 stands in:
; read for no bytes, which looks it up and moves nothing. Keeps every register
w16t_has:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    mov si, w16_mbtext
    xor bx, bx
    xor cx, cx
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_READ_AT
    jnc .yes
    cmp ax, FERR_NOENT
    stc
    je .out
.yes:
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
w16t_ret:
    ret

; --- part 9's own, written through cs: --------------------------------------
w16t_np:    dd 0                    ; WinExec/LoadModule: the name
w16t_tp:    dd 0                    ;   ...the command tail
w16t_tn:    dw 0                    ;   ...its length
w16t_sh:    dw 0                    ;   ...nCmdShow
w16t_dir:   dw 0                    ;   ...where the file is
w16t_vol:   db 0
w16t_new:   db 0                    ;   ...the new task's index
w16t_old:   db 0                    ;   ...and the caller's
w16t_blk:   dw 0                    ;   ...its context block
w16t_hblk:  dw 0                    ; WinHelp's last WM_WINHELP block
w16t_s_help: db 'WINHELP.EXE', 0     ; ...the program it starts
w16t_s_x:   db '-x', 0
w16t_s_wh:  db 'MS_WINHELP', 0, 0, 0, 0, 0, 0   ; ...its window's class
w16t_busy:  db 0                    ; w16t_turns is running
w16t_dead:  db 0                    ; w16t_end: the dead task's owner byte

section .text
