; MOUEVT.COM - does INT 33h's EVENT HANDLER ever get called?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.10.4.  Microsoft Works has a mouse and, under this box, had none.
; The DOSTRACE histogram (96.10.3) says why in one line: Works calls INT 33h
; function 00h, 08h, 0Ah and 0Ch - SET EVENT HANDLER - and then NOTHING. It
; never polls function 3. A box that answers 0Ch with `not supported` and
; makes no callbacks has told a program there is a mouse and then never
; mentions it again, which is indistinguishable from no mouse at all.
;
; So this is not "does the pointer move", it is "does OUR CODE RUN": a far
; handler that counts what it is called with, installed through 0Ch, and a
; wait long enough for the harness to move the mouse across the screen.
;
; It runs under os8088 AND under a real DOS unchanged - and under a DOS with
; no mouse driver it SAYS SO and stops rather than failing, because INT 33h
; not being there is not this box's defect. Compare it against a DOS with
; CuteMouse loaded and the answers are the reference's.
;
;   A  AX=0000h                 - a mouse is installed (AX=FFFF)
;   B  AX=000Ch, mask 1Fh       - the handler goes in
;   C  wait                     - and it is CALLED, with a live position
;   D  AX=000Ch, mask 0         - it comes out again...
;   E  wait                     - ...and stops being called
    org 0x100
    cpu 8086
EVMASK  equ 0x1F                    ; movement + both buttons, press and release
WAITT   equ 160                     ; ticks: ~8.8s at 18.2Hz, the harness's
                                    ; window to move the mouse in

start:
    mov si, s_a                     ; A: is there a mouse at all?
    call puts
    xor ax, ax
    int 0x33
    cmp ax, 0xFFFF
    jne .nomouse
    call ok

    mov si, s_b                     ; B: install the handler
    call puts
    push cs
    pop es
    mov dx, handler
    mov cx, EVMASK
    mov ax, 0x000C
    int 0x33
    call ok

    mov si, s_c                     ; C: ...and it is CALLED
    call puts
    mov cx, WAITT
    call waitn
    cmp word [n_ev], 0
    je .cfail
    call ok

    mov si, s_d                     ; D: take it out again
    call puts
    push cs
    pop es
    xor dx, dx
    xor cx, cx
    mov ax, 0x000C
    int 0x33
    call ok

    mov si, s_e                     ; E: ...and it STOPS. A handler that is
    call puts                       ; still called after the program asked for
    mov ax, [n_ev]                  ; none is one that will be called after the
    mov [n_was], ax                 ; program's code has been freed
    mov cx, 36
    call waitn
    mov ax, [n_ev]
    cmp ax, [n_was]
    jne .efail
    call ok

    call report
    mov si, s_pass
    call puts
    jmp short done
.nomouse:
    mov si, s_nomou                 ; NOT a failure: a DOS with no mouse driver
    call puts                       ; is a machine this test has nothing to say
    jmp short done                  ; about
.cfail:
    mov si, s_f_c
    jmp short bad
.efail:
    mov si, s_f_e
bad:
    call puts
    call report
    mov si, s_fail
    call puts
done:
    mov ah, 0x08                    ; wait for a key, so the screen can be read
    int 0x21
    mov ax, 0x4C00
    int 0x21

; --- THE HANDLER. FAR, called by the driver with AX = the event bits, BX =
; the buttons, CX = x, DX = y, SI/DI = mickeys - and DS = the DRIVER's, never
; ours, which is why the first thing it does is fetch its own.
handler:
    push ds
    push ax
    push bx
    push si
    mov si, cs
    mov ds, si
    inc word [n_ev]
    mov [l_ax], ax
    mov [l_bx], bx
    mov [l_cx], cx
    mov [l_dx], dx
    test al, 1
    jz .nm
    inc word [n_move]
.nm:
    test al, 2
    jz .np
    inc word [n_press]
.np:
    test al, 4
    jz .nr
    inc word [n_rel]
.nr:
    pop si
    pop bx
    pop ax
    pop ds
    retf

; --- CX ticks off the BDA's own counter, which needs no INT 21h and so cannot
; be the thing under test.  A tick is 54.9ms.
waitn:
    push ax
    push bx
    push cx
    push dx
    push es
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
.spin:
    mov dx, [es:0x6C]
    cmp dx, bx
    je .spin
    mov bx, dx
    loop .spin
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

report:
    mov si, s_ev
    call puts
    mov ax, [n_ev]
    call putw
    mov si, s_mv
    call puts
    mov ax, [n_move]
    call putw
    mov si, s_pr
    call puts
    mov ax, [n_press]
    call putw
    mov si, s_rl
    call puts
    mov ax, [n_rel]
    call putw
    mov si, s_at
    call puts
    mov ax, [l_cx]
    call putw
    mov si, s_com
    call puts
    mov ax, [l_dx]
    call putw
    mov si, s_nl
    call puts
    ret

putw:
    push ax
    push cx
    push dx
    mov cx, 4
.digit:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 7
.out:
    mov dl, al
    push ax
    mov ah, 0x02
    int 0x21
    pop ax
    pop ax
    loop .digit
    pop dx
    pop cx
    pop ax
    ret

ok:
    mov si, s_ok
puts:
    push ax
    push dx
.one:
    mov dl, [si]
    or dl, dl
    jz .out
    mov ah, 0x02
    int 0x21
    inc si
    jmp short .one
.out:
    pop dx
    pop ax
    ret

s_a:     db 'A mouse installed   ', 0
s_b:     db 'B handler in        ', 0
s_c:     db 'C handler CALLED    ', 0
s_d:     db 'D handler out       ', 0
s_e:     db 'E and it stops      ', 0
s_ok:    db 'ok', 13, 10, 0
s_f_c:   db 'FAILED', 13, 10, 0
s_f_e:   db 'FAILED', 13, 10, 0
s_nomou: db 'MOUEVT SKIP no INT 33h on this machine', 13, 10, 0
s_ev:    db 'events ', 0
s_mv:    db ' move ', 0
s_pr:    db ' press ', 0
s_rl:    db ' release ', 0
s_at:    db ' at ', 0
s_com:   db ',', 0
s_nl:    db 13, 10, 0
s_pass:  db 'MOUEVT PASS', 13, 10, 0
s_fail:  db 'MOUEVT FAIL', 13, 10, 0
n_ev:    dw 0
n_was:   dw 0
n_move:  dw 0
n_press: dw 0
n_rel:   dw 0
l_ax:    dw 0
l_bx:    dw 0
l_cx:    dw 0
l_dx:    dw 0
