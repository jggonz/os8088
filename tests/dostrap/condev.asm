; CONDEV.COM - is CON a DEVICE, or is it a file that is not there?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.11.7.  Microsoft Works opens CON at ONE CALL SITE over and over
; until DOS refuses, to find out how many handles it has left, then closes
; them all.  A box that answers `file not found` to the first one tells it the
; answer is ZERO - and it then refuses to open its own files, with the message
; `Too many files open` for a condition nothing reported.
;
; So the test is not "does CON open", it is "does opening CON TWICE give two
; DIFFERENT handles" - the property the counting loop rests on, and the one an
; implementation that hands back a standard handle silently fails.
;
; It runs under os8088 AND under a real DOS unchanged, which is the point of
; everything in this directory: the reference says what the answers should be.
;
;   A  open CON twice        - two handles, both >= 5, and NOT equal
;   B  write to one          - the bytes come back on the screen
;   C  close both            - and a third open then reuses a slot
;   D  AH=44h AL=08h, BL=2   - is drive B removable?  DOS says AX=0
;   E  AH=0Dh                - disk reset.  DOS cannot fail it
    org 0x100
    cpu 8086
start:
    mov si, s_a
    call puts
    mov dx, n_con
    mov ax, 0x3D02
    int 0x21
    jc .afail
    mov [h1], ax
    mov dx, n_con
    mov ax, 0x3D02
    int 0x21
    jc .afail
    mov [h2], ax
    mov ax, [h1]
    cmp ax, 5
    jb .afail                       ; a device handle is a handle like any
    mov ax, [h2]                    ; other and the five standard ones are
    cmp ax, 5                       ; taken
    jb .afail
    mov ax, [h1]
    cmp ax, [h2]
    je .afail                       ; THE WHOLE POINT: two opens, two handles
    call ok

    mov si, s_b                     ; B: it writes where a console writes
    call puts
    mov bx, [h1]
    mov cx, s_blen
    mov dx, s_bmsg
    mov ah, 0x40
    int 0x21
    jc .bfail
    cmp ax, s_blen
    jne .bfail
    call ok

    mov si, s_c                     ; C: and closing hands the slots back
    call puts
    mov bx, [h1]
    mov ah, 0x3E
    int 0x21
    jc .cfail
    mov bx, [h2]
    mov ah, 0x3E
    int 0x21
    jc .cfail
    mov dx, n_con
    mov ax, 0x3D02
    int 0x21
    jc .cfail
    mov bx, ax
    mov ah, 0x3E
    int 0x21
    jc .cfail
    call ok

    mov si, s_d                     ; D: is drive B removable?
    call puts
    mov ax, 0x4408
    mov bl, 2
    int 0x21
    jc .dfail
    or ax, ax
    jnz .dfail                      ; 0 = removable, which a floppy is
    call ok

    mov si, s_e                     ; E: and the disk reset cannot fail
    call puts
    mov ah, 0x0D
    int 0x21
    jc .efail
    call ok

    mov si, s_pass
    call puts
    jmp short done
.afail:
    mov si, s_fa
    jmp short bad
.bfail:
    mov si, s_fb
    jmp short bad
.cfail:
    mov si, s_fc
    jmp short bad
.dfail:
    mov si, s_fd
    jmp short bad
.efail:
    mov si, s_fe
bad:
    call puts
    mov si, s_fail
    call puts
done:
    mov ah, 0x08                    ; wait for a key, so the screen can be read
    int 0x21
    mov ax, 0x4C00
    int 0x21

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

n_con:  db 'CON', 0
s_a:    db 'A open CON twice   ', 0
s_b:    db 'B write to it      ', 0
s_c:    db 'C close and reopen ', 0
s_d:    db 'D 44h/08h drive B  ', 0
s_e:    db 'E 0Dh disk reset   ', 0
s_ok:   db 'ok', 13, 10, 0
s_fa:   db 'FAILED', 13, 10, 0
s_fb:   db 'FAILED', 13, 10, 0
s_fc:   db 'FAILED', 13, 10, 0
s_fd:   db 'FAILED', 13, 10, 0
s_fe:   db 'FAILED', 13, 10, 0
s_bmsg: db '[written through CON]', 13, 10
s_blen  equ $ - s_bmsg
s_pass: db 'CONDEV PASS', 13, 10, 0
s_fail: db 'CONDEV FAIL', 13, 10, 0
h1:     dw 0
h2:     dw 0
