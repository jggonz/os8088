; DFREE.COM - what AH=36h answers, for every drive letter.
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.27 implements Get Disk Free Space, and "the installer stopped
; complaining" is not the same as "the numbers are right": a wrong total
; cluster count is invisible to a program that only wants free bytes.  So this
; prints all four registers for drives 0..4 and the host compares them with an
; independent FAT reader (tools/os88fat.py) over the same image.
;
;   nasm -f bin -o DFREE.COM tests/dostrap/dfree.asm
;
; It runs UNDER A REAL DOS UNCHANGED, which is the point of everything in this
; directory: the reference answer is a machine, not a table in a document.
;
; A line reads   D=n AX=hhhh BX=hhhh CX=hhhh DX=hhhh
; and AX=FFFF is DOS's own "invalid drive", which is the answer that matters
; most here - it is the one a program can test when it ignores the carry.

    org 0x100
    cpu 8086

start:
    xor bp, bp                      ; BP = the drive, 0 = default
.loop:
    mov si, s_d
    call puts
    mov ax, bp
    call putdec

    mov dx, bp                      ; DL = the drive, 0 = default, 1 = A
    mov ah, 0x36
    int 0x21

    push dx                         ; the four answers, in order, before any
    push cx                         ; of them is spent on printing
    push bx
    push ax
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
    mov si, s_crlf
    call puts

    inc bp
    cmp bp, 5
    jb .loop

    mov si, s_done
    call puts
    xor ax, ax                      ; WAIT FOR A KEY, diskcost.asm's reason:
    int 0x16                        ; under os8088 the fsx bracket ends with
    mov ax, 0x4C00                  ; the program and the desktop comes back
    int 0x21

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

; --- AX as decimal, 0..9 is all this needs ----------------------------------
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

s_d:    db 'D=', 0
s_ax:   db ' AX=', 0
s_bx:   db ' BX=', 0
s_cx:   db ' CX=', 0
s_dx:   db ' DX=', 0
s_crlf: db 13, 10, 0
s_done: db 'DFREE READY', 13, 10, 0
