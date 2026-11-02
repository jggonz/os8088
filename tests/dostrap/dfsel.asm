; DFSEL.COM - does AH=36h answer for the drive AH=0Eh just SELECTED?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.27.2.  Prince of Persia's INSTALL.EXE reads its file list off
; the floppy, selects C: with AH=0Eh, and asks AH=36h about C: - and this DOS
; answered with the FLOPPY's figures (106 free clusters of 1KB), so it said
; "Not enough space on destination disk" about a hard disk with 31MB free.
; Since SPEC.md 96.48 a select moves only the PROGRAM and the mount waits for
; the next NAME, and AH=36h has no name: it asked whatever was mounted.
;
; dfree.asm cannot see that, because it never selects - it names each drive
; in DL, and naming one that is not the program's drive took the old code
; through a path that did mount.  The bug needs exactly the installer's
; shape: SELECT, then ask about the drive you are now on, by number or as 0.
;
;   nasm -f bin -o DFSEL.COM tests/dostrap/dfsel.asm
;
; Run it from B: with another floppy in A:.  It prints, for the four asks,
;
;   Ln D=d AX=hhhh BX=hhhh CX=hhhh DX=hhhh
;
; where D is the drive the line is ABOUT (1 = A) and the four are AH=36h's
; answer; tests/dosdfsel.py compares each with tools/os88fat.py reading that
; drive's image.  CUR=d after each ask is AH=19h, because AH=36h must not move
; the program either.  It runs under a real DOS unchanged.

    org 0x100
    cpu 8086

start:
    mov ah, 0x19
    int 0x21
    mov [home], al
    xor al, 1                       ; the OTHER floppy - on the drive NUMBER,
    mov [other], al                 ; before any 'A' is added (renref.asm's
                                    ; trap: 'A' XOR 1 is '@')

    mov byte [tag], '1'             ; L1: the drive we started on, as 0
    mov al, [home]
    xor dl, dl
    call ask

    mov dl, [other]                 ; SELECT the other one - and ask nothing
    mov ah, 0x0E                    ; that names a file, which is what would
    int 0x21                        ; have mounted it

    mov byte [tag], '2'             ; L2: ...and ask about it as 0, the
    mov al, [other]                 ; default
    xor dl, dl
    call ask

    mov byte [tag], '3'             ; L3: ...and by number, which is the
    mov al, [other]                 ; installer's own shape (DL = 3 for C:)
    mov dl, al
    inc dl
    call ask

    mov dl, [home]                  ; and back, by select again
    mov ah, 0x0E
    int 0x21
    mov byte [tag], '4'             ; L4: home, by number
    mov al, [home]
    mov dl, al
    inc dl
    call ask

    mov si, s_done
    call puts
    xor ax, ax                      ; WAIT FOR A KEY, diskcost.asm's reason:
    int 0x16                        ; under os8088 the fsx bracket ends with
    mov ax, 0x4C00                  ; the program and the desktop comes back
    int 0x21

; --- ask: AH=36h with DL, about drive AL (0 = A); print the line and CUR ----
ask:
    push ax
    mov ah, 0x36
    int 0x21
    push dx                         ; the four answers, in order, before any
    push cx                         ; of them is spent on printing
    push bx
    push ax
    mov al, 'L'
    call putc
    mov al, [tag]
    call putc
    mov si, s_d
    call puts
    mov bp, sp
    mov al, [bp+8]                  ; the drive the line is about, 1 = A
    inc al
    call putdec
    mov si, s_ax
    call puts
    pop ax
    call puthex
    mov si, s_bx
    call puts
    pop ax
    call puthex
    mov si, s_cx
    call puts
    pop ax
    call puthex
    mov si, s_dx
    call puts
    pop ax
    call puthex
    mov si, s_cur
    call puts
    mov ah, 0x19
    int 0x21
    inc al                          ; 1 = A, as D is
    call putdec
    mov si, s_crlf
    call puts
    pop ax
    ret

; --- AX as four hex digits ---------------------------------------------------
puthex:
    push cx
    mov cx, 4
.d:
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .ok
    add al, 7
.ok:
    call putc
    pop ax
    loop .d
    pop cx
    ret

; --- AL as one decimal digit, which is all a drive number needs -------------
putdec:
    add al, '0'
    call putc
    ret

putc:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

puts:
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret

home:   db 0
other:  db 0
tag:    db 0
s_d:    db ' D=', 0
s_ax:   db ' AX=', 0
s_bx:   db ' BX=', 0
s_cx:   db ' CX=', 0
s_dx:   db ' DX=', 0
s_cur:  db ' CUR=', 0
s_crlf: db 13, 10, 0
s_done: db 'DFSEL READY', 13, 10, 0
