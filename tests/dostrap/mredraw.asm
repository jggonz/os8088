; MREDRAW.COM - does the cursor come BACK after the PROGRAM overwrites it?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.10.5.4.  MCURSOR.COM asks whether the driver can draw; this asks
; what happens when the PROGRAM draws over what the driver drew.  A software
; text cursor is an attribute flipped into a cell the driver does not own, and
; it gets NO NOTIFICATION when the application stores over that cell - which
; every DOS application does, constantly, by writing straight into B800.
;
; **THE ANSWER IS THAT THE CURSOR IS LOST UNTIL THE POINTER MOVES, AND THAT IS
; NOT A DEFECT - IT IS WHAT A REAL DRIVER DOES.**  This file was written to
; prove the opposite and measured the other way round: CuteMouse 1.9.1 under
; IBM DOS 3.30, on the same machine, answers all four of these IDENTICALLY to
; this box.  A serial mouse that is not moving raises no interrupt, so there
; is nothing to repaint from; every DOS program of the era was written against
; drivers that behave exactly like this, and a box that "improved" on it would
; be diverging from the thing it exists to imitate.
;
;   A  the cursor is drawn on the cell to start with            (the control)
;   B  ...and the PROGRAM'S OWN WORD SURVIVES a store over it - the driver
;      does not put itself back, because nothing told it to
;   C  a hide then leaves that word alone rather than restoring the cell the
;      driver had saved, which would be a character the program never wrote
;   D  the pointer never moved, so B and C are about the cell they claim
;
; So this is a COMPATIBILITY ratchet and not a bug report: it goes red if this
; box ever starts repainting where CuteMouse does not.  C is the half that is
; easy to get wrong in the other direction - re-saving on every call banks the
; driver's OWN inverted cell as the thing to restore, after which the
; inversion is permanent and travels with the pointer - and two paints of one
; cell is all it takes to show, so the poll below is deliberately more than
; one.
;
; It runs under a real DOS unchanged, which is how the above was measured
; rather than argued (docs/DOS-DEBUGGING.md).  With no driver at all INT 33h
; is not installed, function 0 answers AX != FFFF, and it prints SKIP - the
; honest answer rather than a failure of ours.
    org 0x100
    cpu 8086

CELL    equ 0x0741                  ; 'A' plain grey - the ground it lands on
NEWC    equ 0x1E2A                  ; '*' yellow on blue - what the PROGRAM
                                    ; redraws with.  Chosen so that all four
                                    ; of NEWC, CELL and their two inversions
                                    ; are different words: any two of them
                                    ; colliding would let a wrong answer read
                                    ; as a right one
M_S     equ 0x77FF                  ; the pair every driver powers up with
M_C     equ 0x7700

POLLS   equ 5                       ; what a program's own loop does.  MORE
                                    ; THAN ONE ON PURPOSE - see C above

start:
    mov ax, 0x0000                  ; is there a mouse at all?
    int 0x33
    cmp ax, 0xFFFF
    je .have
    mov si, s_skip
    call puts
    jmp .bye
.have:
    call seg_of                     ; ES = the text screen, BX = columns
    jnc .gotseg
    mov si, s_nosc
    call puts
    jmp .bye
.gotseg:
    mov [scrseg], es
    mov [cols], bx

    mov ax, 0x000A                  ; the masks are state, so SAY them rather
    xor bx, bx                      ; than inherit whatever ran last
    mov cx, M_S
    mov dx, M_C
    int 0x33
    call fill                       ; every cell, so wherever the pointer is
    mov ax, 0x0001                  ; the answer is the same
    int 0x33

    call cellidx                    ; SI = the byte offset, and BANK it: every
    mov [coff], si                  ; read below is of THE SAME CELL, so a
    mov [c0x], cx                   ; pointer that drifted would be caught by
    mov [c0y], dx                   ; D rather than quietly change the answer

    mov es, [scrseg]
    mov ax, [es:si]
    mov [r_a], ax                   ; A: it is drawn

    ; --- THE PROGRAM REDRAWS ITS OWN SCREEN -----------------------------
    ; One store, straight into the framebuffer, telling nobody - which is the
    ; whole of what a DOS application does and the whole of why this is hard.
    mov ax, NEWC
    mov [es:si], ax

    ; ...and then does what a program does next: asks where the pointer is.
    mov cx, POLLS
.poll:
    push cx                         ; **INT 33h FUNCTION 3 ANSWERS IN CX**,
    mov ax, 0x0003                  ; which `loop` decrements: banking it is
    int 0x33                        ; not defensive, it is required
    pop cx
    loop .poll

    call waitticks                  ; ...and the TIMER gets its chance too, so
                                    ; a driver that repaints on either one is
                                    ; given both

    mov es, [scrseg]
    mov si, [coff]
    mov ax, [es:si]
    mov [r_b], ax                   ; B: the program's word is still there

    mov ax, 0x0002                  ; C: and a hide leaves it alone rather than
    int 0x33                        ; restoring the cell the driver had saved
    mov es, [scrseg]
    mov si, [coff]
    mov ax, [es:si]
    mov [r_c], ax

    mov ax, 0x0003                  ; D: the control.  Nothing here moves the
    int 0x33                        ; mouse, so if it moved anyway then B and
    mov ax, 1                       ; C were read off a cell the cursor had
    cmp cx, [c0x]                   ; left and neither means anything
    jne .moved
    cmp dx, [c0y]
    jne .moved
    xor ax, ax
.moved:
    mov [r_d], ax

    call blank                      ; a clear ground for the verdict: a screen
    mov ah, 0x02                    ; of 'A' buries the one thing anybody reads
    mov dl, 13
    int 0x21
    mov dl, 10
    int 0x21

    ; --- the verdict, which is arithmetic and not a photograph -----------
    mov si, s_a
    mov ax, [r_a]
    mov bx, (CELL & M_S) ^ M_C
    call judge
    mov si, s_b
    mov ax, [r_b]
    mov bx, NEWC                    ; **NOT its inversion**: measured against
    call judge                      ; CuteMouse 1.9.1, which answers the same
    mov si, s_c
    mov ax, [r_c]
    mov bx, NEWC
    call judge
    mov si, s_d
    mov ax, [r_d]
    xor bx, bx
    call judge

    cmp byte [n_bad], 0
    jne .fail
    mov si, s_pass
    call puts
    jmp short .bye
.fail:
    mov si, s_fail
    call puts
.bye:
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; --- judge: SI = the label, AX = what was there, BX = what should be --------
judge:
    push ax
    push bx
    call puts
    pop bx
    pop ax
    push ax
    push bx
    call putw
    mov si, s_want
    call puts
    pop ax
    push ax
    call putw
    pop bx
    pop ax
    cmp ax, bx
    je .ok
    mov byte [n_bad], 1
    mov si, s_no
    jmp short .say
.ok:
    mov si, s_yes
.say:
    call puts
    mov si, s_nl
    call puts
    ret

; --- waitticks: let the TIMER have its chance ------------------------------
; **AND IT MUST NOT HANG WAITING FOR ONE.**  The BDA tick is the ROM's, so a
; machine that does not keep it would spin here for ever and the row would
; report a timeout - a sentence about the harness for a defect in the box.
; The bound is per tick and generous: ~65k times round a ~25-cycle loop is
; several ticks of a 4.77MHz 8088.
waitticks:
    push ax
    push bx
    push cx
    push dx
    push es
    mov ax, 0x0040
    mov es, ax
    mov cx, 3
.next:
    mov dx, [es:0x6C]
    mov bx, 0xFFFF
.spin:
    cmp dx, [es:0x6C]
    jne .moved
    dec bx
    jnz .spin
    jmp short .out                  ; the tick is not moving on this machine
.moved:
    loop .next
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- seg_of: the text screen, off the BDA the ROM keeps ---------------------
; out: ES = segment, BX = columns, CF=1 = not a text mode
seg_of:
    push ax
    push si
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x49]
    cmp al, 7
    je .mono
    cmp al, 3
    ja .no
    mov si, 0xB800
    jmp short .have
.mono:
    mov si, 0xB000
.have:
    mov bx, [es:0x4A]
    or bx, bx
    jz .no
    mov es, si
    pop si
    pop ax
    clc
    ret
.no:
    pop si
    pop ax
    stc
    ret

; --- cellidx: WHERE the driver should have drawn ----------------------------
; out: SI = the byte offset into the text screen, CX = x, DX = y (raw)
; INT 33h's units are a 640x200 virtual screen whatever the text mode is, so
; a cell is 8 of them on BOTH axes.
cellidx:
    push ax
    push bx
    mov ax, 0x0003
    int 0x33                        ; CX = x, DX = y
    push cx
    push dx
    mov si, cx
    mov ax, dx
    mov cl, 3
    shr si, cl
    shr ax, cl
    mov bx, [cols]
    mul bl                          ; AX = row * columns; AH is 0 coming in,
    add ax, si                      ; rows being at most 50
    shl ax, 1                       ; ...and a cell is two bytes
    mov si, ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- fill: CELL into every cell of the screen -------------------------------
; **IT USES `stosw` AND NOT THE BIOS**, which is what a DOS application does
; and is the whole point of this file.
fill:
    mov ax, CELL
    jmp short fillax
blank:
    mov ax, 0x0720
fillax:
    push ax
    push cx
    push di
    push es
    push ax
    mov es, [scrseg]
    xor di, di
    mov ax, [cols]                  ; cols * 25, in the one multiply an 8086
    mov cl, 25                      ; has: `imul r,r,imm` is a 186
    mul cl
    mov cx, ax
    pop ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
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

s_a:     db 'A drawn     ', 0       ; **EVERY LABEL ENDS IN A SPACE** and they
s_b:     db 'B kept      ', 0       ; are all the same width: the host side
s_c:     db 'C restored  ', 0       ; splits this line on whitespace, and
s_d:     db 'D unmoved   ', 0       ; `kept1e2a` is one token
s_want:  db ' want ', 0
s_yes:   db ' ok', 0
s_no:    db ' BAD', 0
s_nl:    db 13, 10, 0
s_skip:  db 'MREDRAW SKIP - no INT 33h on this machine', 13, 10, 0
s_nosc:  db 'MREDRAW SKIP - not a text mode', 13, 10, 0
s_pass:  db 'MREDRAW PASS', 13, 10, 0
s_fail:  db 'MREDRAW FAIL', 13, 10, 0
scrseg:  dw 0
cols:    dw 0
coff:    dw 0
c0x:     dw 0
c0y:     dw 0
r_a:     dw 0
r_b:     dw 0
r_c:     dw 0
r_d:     dw 0
n_bad:   db 0
