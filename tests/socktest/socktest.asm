; =============================================================================
; os8088 - tests/socktest/socktest.asm
;
; SOCKTEST: the gate for the SOCKET verbs (SPEC.md 62.11,
; docs/NET-STACK-PLAN.md stage B). It fetches a page over the parallel cable
; and reports what came back - which is the first time anything in this tree
; has reached a TCP connection at all.
;
; IT IS THE REFERENCE CONSUMER OF drivers/net/netpkg.inc, and it is shaped
; the way the browser will be rather than the way a test would be if nobody
; had to copy it. That means a WORKER TASK running a state machine (SPEC.md
; 20.6), one verb per turn, never blocking:
;
;   OPEN   NETV_STATE, then NETV_OPEN - which RETURNS IMMEDIATELY with a
;          handle in NSK_CONNECT. The connection has not happened yet
;   WAIT   NETV_STATUS every tick until the state leaves NSK_CONNECT. This
;          is the whole non-blocking design in one step: seconds may pass and
;          the desktop stays live through all of them
;   SEND   NETV_SEND, which takes WHAT IT CAN and says how much. The request
;          is short enough to go in one, and the loop is written for the
;          general case anyway because a caller that assumes otherwise is
;          broken on a real network too
;   RECV   NETV_RECV until NSK_CLOSING with nothing readable. **A zero-length
;          recv is NOT the end** and reading it as one truncates every page
;          that arrives in more than one piece (netpkg.inc says so twice)
;   DONE   NETV_CLOSE
;
; NEVER SHIPPED. Its own scratch image:
;   make socktest && python3 tests/socktest.py
;
; The far end is tests/lptlink/partner.py's SocketBox, which is REAL host
; sockets rather than a model - so a page fetched through this really did
; cross one. What the harness cannot arbitrate is the CABLE; that verdict is
; still the 5150's (SPEC.md 62.10.3).
;
; Every proc here is a near proc with a near `ret`: the kernel reaches a
; callback through the dispatcher in the package's own header (SPEC.md 20.1).
; =============================================================================

%include "os88api.inc"
%include "netpkg.inc"               ; ...THE DRIVER'S OWN HEADER, the same file
                                    ; drivers/net/net.asm includes. A driver
                                    ; ABI belongs beside the driver, and both
                                    ; ends including one file is what stops
                                    ; the two drifting (SPEC.md 20.11)

    OS88_HEADER 'SOCKTEST', sk_entry

SK_CONT_W   equ 318                 ; 320 outer - 2px borders
SK_CONT_H   equ 75                  ; 94 outer - TITLE_H - 1px border
SK_PORT     equ 8088                ; the harness binds this on 127.0.0.1
SK_HEAD     equ 30                  ; bytes of the reply we keep to show
SKT_BUF     equ 512                 ; one NETV_RECV's landing ground. Well
                                    ; under NET_SKMAX, deliberately: a package
                                    ; asking for more than it can hold is the
                                    ; bug the driver's cap exists to survive,
                                    ; and a package that asks for what it has
                                    ; is the one worth copying

; --- the state machine -------------------------------------------------------
SS_IDLE     equ 0
SS_OPEN     equ 1
SS_WAIT     equ 2
SS_SEND     equ 3
SS_RECV     equ 4
SS_DONE     equ 5
SS_ERR      equ 6

; -----------------------------------------------------------------------------
; sk_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
sk_entry:
    push si
    mov si, sk_tpl
    call OSAPI_WM_CREATE
    mov [sk_win], bx
    pop si
    ret

; -----------------------------------------------------------------------------
; sk_paint - W_PAINT: four lines, and hire the worker if we have none
;
; THE WORKER IS HIRED HERE AND NOT IN THE ENTRY PROC. The loader publishes
; the instance only after the entry returns (SPEC.md 21 step 9), so a spawn
; there is refused by contract - apps/fractal's note, and the same trap.
; -----------------------------------------------------------------------------
sk_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sk_win], si
    call sk_hire
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, sk_r1
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, sk_r2
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, sk_r3
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, sk_r4
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A click repaints, which is the whole of this package's UI. THE WORKER DOES
; NOT DRAW, deliberately: what it would need is the gfx lock plus SPEC.md
; 11.3's clip region, and this is a gate whose assertions are read out of the
; image by tests/socktest.py rather than off the screen. The window is here so
; a human watching a run can see where it got to, and a click is when they ask.
sk_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [sk_state], SS_IDLE    ; the first click STARTS the fetch; every
    jne .draw                       ; one after it just repaints
    mov byte [sk_state], SS_OPEN
.draw:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx
    mov cx, ax
    add cx, SK_CONT_W-1
    add dx, SK_CONT_H-1
    call OSAPI_GFX_FILL
    call sk_paint                   ; NEAR: SI is still the window
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sk_onkey:
    ret

; --- sk_hire - one worker, retried until granted (SPEC.md 20.6) --------------
; A full task table is a NORMAL, TRANSIENT outcome, so this is called from
; every paint and latches only on success.
sk_hire:
    push ax
    push bx
    cmp byte [sk_spawned], 1
    je  .out
    mov ax, sk_worker
    mov bx, [sk_win]
    call OSAPI_TASK_SPAWN
    jc  .out
    mov byte [sk_spawned], 1
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE WORKER - one verb per turn, and never a blocking call
; =============================================================================
sk_worker:
.loop:
    mov bx, [sk_win]
    call OSAPI_TASK_ALIVE           ; the death door: may never return
    call sk_step
    mov ax, 1                       ; one tick. The cable's own latency is far
    call OSAPI_TASK_SLEEP           ; longer than a tick, so polling faster
    jmp .loop                       ; would only spend the machine

; --- sk_step - advance one state -------------------------------------------
sk_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES = OURS for every NETV_* buffer. The
                                    ; door is the one driver entry point that
                                    ; is handed the CALLER's segment, and this
                                    ; is what makes that worth having
    mov al, [sk_state]
    cmp al, SS_OPEN
    je  .open
    cmp al, SS_WAIT
    je  .wait
    cmp al, SS_SEND
    je  .send
    cmp al, SS_RECV
    je  .recv
    jmp .out                        ; IDLE, DONE and ERR are all resting
                                    ; states: the harness reads them and the
                                    ; window shows them

; --- OPEN: WHO ARE YOU, then what have we, then start a connection ----------
.open:
    call net_find                   ; **WHICH CLASS, AND IS IT A SOCKET
    jc  .maybe                       ; DRIVER?** DRVC_NET is ETHER.DRV and
                                    ; DRVC_FILE is NET.DRV - and RAMDISK.DRV
                                    ; is DRVC_FILE too, with a verb 1 that
                                    ; upper-cases a string where ours opens a
                                    ; socket. net_find asks each for
                                    ; NETV_IDENT, which is the only thing
                                    ; between this package and handing a
                                    ; hostname to a RAM disk (SPEC.md 20.11.1)
    mov bh, NET_CLASS
    mov bl, NETV_STATE
    call OSAPI_DRV_CALL
    jc  .maybe                      ; no driver at all
    mov [sk_flags], al
    mov [sk_free], ah
    test al, NSTF_SOCK
    jz  .nosock
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, sk_host
    mov cx, SK_PORT
    call OSAPI_DRV_CALL
    jc  .maybe
    mov [sk_hnd], al
    mov byte [sk_state], SS_WAIT
    jmp .out
.nosock:
    mov ax, NETE_REFUSED            ; a link with no socket verbs behind it,
    jmp .err                        ; which is a partner too old for them
.wrongdrv:
    mov ax, NETE_VERB               ; ...somebody else answered the class
    jmp .err

; --- WAIT: poll until the connection resolves -------------------------------
.wait:
    call sk_stat
    jc  .maybe
    cmp ah, NSK_UP
    je  .up
    cmp ah, NSK_CONNECT
    je  .out                        ; still connecting: come back next tick
    mov ax, NETE_REFUSED            ; FAILED, or closed under us
    jmp .err
.up:
    mov byte [sk_state], SS_SEND
    mov word [sk_sent], 0
    jmp .out

; --- SEND: push what it will take, and come back for the rest ---------------
.send:
    mov si, sk_req
    add si, [sk_sent]
    mov cx, sk_req_end - sk_req
    sub cx, [sk_sent]
    mov al, [sk_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    jc  .maybe
    add [sk_sent], cx
    mov ax, [sk_sent]
    cmp ax, sk_req_end - sk_req
    jb  .out                        ; ...it took some of it; the rest next turn
    mov byte [sk_state], SS_RECV
    jmp .out

; --- RECV: drain until the peer has closed AND nothing is left --------------
.recv:
    mov al, [sk_hnd]
    mov di, sk_buf
    mov cx, SKT_BUF
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc  .maybe
    or  cx, cx
    jz  .empty
    call sk_keep                    ; the first SK_HEAD bytes, once
    add [sk_got], cx
    jmp .out                        ; more may be waiting: ask again next turn
.empty:
    call sk_stat                    ; NOTHING READ IS NOT AN END. The end is
    jc  .maybe                      ; NSK_CLOSING with nothing readable, and
    cmp ah, NSK_UP                  ; only NETV_STATUS can say so
    je  .out
    cmp ah, NSK_CONNECT
    je  .out
    or  cx, cx
    jnz .out                        ; closing, but there is still data behind
    mov byte [sk_state], SS_DONE
    mov al, [sk_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    jmp short .out

.maybe:
    mov [sk_err], ax                ; ...record it either way, so a run that
    cmp ax, NETE_BUSY               ; ends healthy still shows the last thing
    je  .out                        ; the wire said
                                    ;
                                    ; **NETE_BUSY IS NOT A FAILURE** and this
                                    ; is the only place that knows it: the
                                    ; Link volume's file verbs are on the same
                                    ; wire and a Disk window click can be
                                    ; mid-command when a turn comes round. The
                                    ; whole answer is to come back next tick,
                                    ; which is what a polling worker does
                                    ; anyway - a package that treated it as an
                                    ; error would drop a perfectly good fetch
                                    ; because somebody opened a folder
.err:
    mov [sk_err], ax
    mov byte [sk_state], SS_ERR
.out:
    call sk_report
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- sk_stat - NETV_STATUS on our handle. out AH = state, CX = readable -----
sk_stat:
    mov al, [sk_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    ret

; --- sk_keep - bank the first SK_HEAD bytes of the reply, once --------------
; in: CX = how many arrived in sk_buf; preserves CX
sk_keep:
    push ax
    push cx
    push si
    push di
    cmp word [sk_got], 0
    jne .out                        ; only the FIRST block
    mov si, sk_buf
    mov di, sk_head
    cmp cx, SK_HEAD
    jbe .n
    mov cx, SK_HEAD
.n:
    jcxz .out
.copy:
    mov al, [si]
    inc si
    cmp al, ' '                     ; a status line is text, and anything
    jb  .space                      ; below space (the CR LF) would draw as
    cmp al, 0x7E                    ; a glyph nobody can read - SPEC.md 19.1's
    jbe .put                        ; own rule about a name off a wire
.space:
    mov al, '_'
.put:
    mov [di], al
    inc di
    loop .copy
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- sk_report - rewrite the four lines from the state ----------------------
sk_report:
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, [sk_flags]
    mov di, sk_r1 + 7
    call sk_hex2
    mov al, [sk_free]
    add al, '0'
    mov [sk_r1 + 15], al
    mov al, [sk_hnd]
    add al, '0'
    mov [sk_r1 + 24], al
    mov bl, [sk_state]
    xor bh, bh
    shl bx, 1
    mov bx, [sk_names + bx]
    mov di, sk_r2 + 7
    call sk_str8
    mov ax, [sk_got]
    mov di, sk_r3 + 5
    call sk_dec5
    mov ax, [sk_err]
    mov di, sk_r3 + 21
    call sk_dec5
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- sk_hex2 - AL as two hex digits at DS:DI --------------------------------
sk_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .dig
    mov al, ah
    and al, 0x0F
    call .dig
    pop cx
    pop ax
    ret
.dig:
    cmp al, 10
    jb  .num
    add al, 'A' - 10
    jmp short .put
.num:
    add al, '0'
.put:
    mov [di], al
    inc di
    ret

; --- sk_dec5 - AX as five decimal digits at DS:DI ---------------------------
sk_dec5:
    push ax
    push bx
    push cx
    push dx
    push di
    add di, 4
    mov bx, 10
    mov cx, 5
.d: xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .d
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- sk_str8 - eight bytes from DS:BX to DS:DI ------------------------------
sk_str8:
    push ax
    push cx
    mov cx, 8
.c: mov al, [bx]
    inc bx
    mov [di], al
    inc di
    loop .c
    pop cx
    pop ax
    ret

; --- the state names, eight characters each so the field is fixed -----------
sk_names:
    dw sk_n0, sk_n1, sk_n2, sk_n3, sk_n4, sk_n5, sk_n6
sk_n0:  db 'idle    '
sk_n1:  db 'open    '
sk_n2:  db 'connect '
sk_n3:  db 'sending '
sk_n4:  db 'reading '
sk_n5:  db 'done    '
sk_n6:  db 'FAILED  '

sk_host:    db '127.0.0.1', 0
sk_req:     db 'GET /demo.htm HTTP/1.0', 13, 10
            db 'Host: os8088', 13, 10, 13, 10
sk_req_end:

; --- window template (SPEC.md 11) -------------------------------------------
sk_tpl:
    dw 160, 180, 320, 94
    dw sk_ttl, sk_paint, sk_onkey, sk_onclick

sk_ttl:     db 'Socket Test', 0
sk_r1:      db 'State: .. free . handle .', 0    ; [+7] [+15] [+24]
sk_r2:      db 'Sock:  ........', 0              ; [+7]
sk_r3:      db 'Got: ..... bytes err ..... ', 0  ; [+5] [+21]
sk_r4:      db 'Head: '                        ; NO TERMINATOR: sk_head is the
                                                ; rest of this line, so the
                                                ; reply's first bytes are both
                                                ; DRAWN and readable out of the
                                                ; image by the harness
sk_head:    times SK_HEAD db '.'
            db 0                                ; ...and sk_keep never writes a
                                                ; NUL into it: every byte off
                                                ; the wire outside 0x20..0x7E
                                                ; becomes '_', which is SPEC.md
                                                ; 19.1's rule about a name off a
                                                ; wire applied to a status line

sk_win:     dw 0
sk_spawned: db 0
sk_state:   db SS_IDLE              ; ...AND IT WAITS FOR A CLICK. Not
                                    ; politeness: the far end has to be
                                    ; DRIVING THE CABLE before the first wire
                                    ; command goes out, and the only click a
                                    ; harness can synchronise against is one
                                    ; delivered to a PAUSED machine
                                    ; (tests/lptlink/partner.py's
                                    ; click_paused). Starting on load would
                                    ; race the harness every time
sk_hnd:     db 0
sk_flags:   db 0
sk_free:    db 0
sk_sent:    dw 0
sk_got:     dw 0
sk_err:     dw 0

%include "os88sock.inc"         ; net_find (SPEC.md 72)

    OS88_BSS SKT_BUF
sk_buf      equ os88_image_end      ; one NETV_RECV's landing ground, in bss
                                    ; (SPEC.md 20.2): the loader zeroes it and
                                    ; the image on the floppy does not carry it

    OS88_IMAGE_END
