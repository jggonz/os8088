; =============================================================================
; os8088 - apps/win16/w16jgive.inc
;
; GIVE A WINDOWS PROGRAM TO A WIN16 ALREADY RUNNING ONE (SPEC.md 124.28.12).
; The CODE half of the join box, as w16join.inc is its layout: ONE routine,
; included as source by both of DOS.O88's places that recognise an NE - the
; loader, before any of DOS is read (apps/dos/dosload.asm's dsl_w16, a
; desktop double-click: SPEC.md 124.3.1), and the host, for an NE typed at the
; prompt, named in Run or reached through a .LNK (apps/dos/dosw16.inc's
; dos_w16_try). Two callers and one body, so the two routes cannot come to
; disagree about which instance takes a program.
;
; w16j_give - a live package instance whose image carries the join box,
; signed and of this version, that says a program runs and holds no locator,
; has the locator written into it, its request byte set last and its window
; woken
; in:  DS:SI = the 16-byte locator, OSAPI_ARG_FILE's shape (SPEC.md 21.5.3)
;      ES = a claim of at least 1KB, at offset 0, for OSAPI_SYS_SNAPSHOT
; out: CF=1 taken; CF=0 start WIN16 as before. Preserves every register but
;      the flags
; =============================================================================

%if SYS_SNAPSHOT_SIZE > 1024
 %error "the snapshot no longer fits the 1KB claim w16j_give is handed"
%endif

w16j_give:
    push ax
    push bx
    push cx
    push dx
    push di
    xor di, di
    mov cx, INST_MAX
    call OSAPI_SYS_SNAPSHOT         ; CX = the records written; DX spent
    mov dx, si                      ; DX = the locator, across the walk
    mov bx, SS_INST
.r:
    cmp byte [es:bx + SSI_STATE], 1
    jne .n
    test byte [es:bx + SSI_KIND], KIND_PKG
    jz .n
    cmp word [es:bx + SSI_SIZE], W16J_AT + W16J_SIZE
    jb .n                           ; a built-in (0), or too small to hold it
    push es
    mov es, [es:bx + SSI_SEG]
    cmp word [es:W16J_AT + W16J_MAGIC], W16J_SIG
    jne .p
    cmp word [es:W16J_AT + W16J_VER], W16J_V1 + 256 ; ...and OK = 1
    jne .p
    cmp byte [es:W16J_AT + W16J_REQ], 0
    jne .p                          ; one locator waits at a time
    push cx
    mov si, dx
    mov di, W16J_AT + W16J_DOC
    mov cx, 8
    cld
    rep movsw
    pop cx
    mov byte [es:W16J_AT + W16J_REQ], 1
    push bx
    mov bx, [es:W16J_AT + W16J_WIN]
    call OSAPI_WM_WAKE
    pop bx
    jnc .took
    mov byte [es:W16J_AT + W16J_REQ], 0 ; the ring is full: an instance
.p:
    pop es
.n:
    add bx, SSI_RECSZ
    loop .r
    clc
    jmp short .out
.took:
    pop es
    stc
.out:
    mov si, dx
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
