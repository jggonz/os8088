; TWOOPEN.COM - is it the FILE, or is it the SECOND HANDLE?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; A program that reads two files at once is ordinary and a box that has ONE
; read window is not, so "this file will not read" and "this file will not read
; WHILE THAT ONE IS OPEN" are different faults with one symptom.  Three cases,
; in one run, so the answer needs no second experiment:
;
;   A  open X, read 6, close                     - X alone
;   B  open Y, read 6, close                     - Y alone
;   C  open Y, read 6, then open X and read 6    - both open at once
;
; A and B passing with C failing is the window; all three failing is the file;
; B alone failing is usually the DISK (`tools/os88fat.py reach`).
;
; Edit the two names below for the files under test.  They are built in rather
; than parsed from the command tail because the fault this exists to find is
; usually met by double-clicking a program in the file manager, which passes
; no arguments at all.
    org 0x100
    cpu 8086
start:
    mov si, s_a
    call puts
    mov dx, n_x
    call one
    mov si, s_b
    call puts
    mov dx, n_y
    call one

    mov si, s_c
    call puts
    mov dx, n_y                     ; hold Y open...
    mov ax, 0x3D00
    int 0x21
    jc .cfail
    mov [h1], ax
    mov bx, ax
    mov cx, 6
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    call axcf
    mov dx, n_x                     ; ...and open X on top of it
    mov ax, 0x3D00
    int 0x21
    jc .cfail
    mov [h2], ax
    mov bx, ax
    mov cx, 6
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    call axcf
    mov bx, [h2]
    mov ah, 0x3E
    int 0x21
    mov bx, [h1]
    mov ah, 0x3E
    int 0x21
    jmp short .done
.cfail:
    mov si, s_open
    call puts
.done:
    mov si, s_rdy
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

one:                                ; open DS:DX, read 6, close
    mov ax, 0x3D00
    int 0x21
    jc .no
    mov [h1], ax
    mov bx, ax
    mov cx, 6
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    call axcf
    mov bx, [h1]
    mov ah, 0x3E
    int 0x21
    ret
.no:
    mov si, s_open
    call puts
    ret

axcf:
    mov [sav], ax
    pushf
    pop ax
    mov [savf], ax
    mov si, s_got
    call puts
    mov ax, [sav]
    call hex4
    mov al, '/'
    call putc
    mov al, [savf]
    and al, 1
    add al, '0'
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
hex2:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call hexd
    pop ax
    and al, 0x0F
    call hexd
    pop cx
    pop ax
    ret
hexd:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
putc:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop si
    pop dx
    pop cx
    pop bx
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

s_a:    db 'A  IBM_SND1 alone     ', 0
s_b:    db 'B  DIGISND1 alone     ', 0
s_c:    db 'C  DIGISND1 then IBM_SND1, both open', 13, 10, '   ', 0
s_got:  db 'AX/CF=', 0
s_open: db 'open FAILED', 13, 10, 0
s_rdy:  db 'TWOOPEN READY', 13, 10, 0
n_x:    db 'IBM_SND1.DAT', 0   ; X - edit for the files under test
n_y:    db 'DIGISND1.DAT', 0   ; Y
h1:     dw 0
h2:     dw 0
sav:    dw 0
savf:   dw 0
buf:    times 64 db 0
