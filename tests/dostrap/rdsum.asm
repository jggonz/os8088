; RDSUM.COM - read a named file whole and print its length and a checksum, so
; the bytes a DOS program is GIVEN can be compared with the bytes on the disk.
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
;   RDSUM                 the name built in below (a double click has no tail)
;   RDSUM FILENAME.EXT    ...or one you type
;
; WHAT IT IS FOR.  "The program was given the wrong bytes" and "the program did
; the wrong thing with the right bytes" look identical from outside, and the
; difference decides which half of the machine to go and read.  This answers it
; in one run: the host computes the same length and checksum off the image with
;
;   python3 tools/os88fat.py cat IMG NAME | <the same rotate-and-add>
;
; and the two either agree or they do not.
;
; THE CHECKSUM IS POSITION-SENSITIVE ON PURPOSE - rotate, then add - because
; the failures worth catching here are ORDER failures: a cluster chain walked
; wrongly, a window refilled from the wrong offset, two handles sharing one
; buffer.  A plain sum of bytes is blind to every one of them, and a file
; delivered in the wrong order is exactly what a stale read window looks like.
;
; IT READS IN 512-BYTE CHUNKS, which is smaller than the box's read window on
; purpose: a whole-file read would take one path through apps/dos and hide the
; window's own refill, which is the code most likely to be wrong.
    org 0x100
    cpu 8086
start:
    cmp byte [0x80], 0              ; no argument: the default below, so the
    je .have                        ; program can be launched with a double
    mov si, 0x81                    ; click and still name a file
.skip:
    lodsb
    cmp al, ' '
    je .skip
    cmp al, 9
    je .skip
    dec si
    mov di, name
.cp:
    lodsb
    cmp al, 13
    je .end
    cmp al, ' '
    je .end
    stosb
    jmp short .cp
.end:
    xor al, al
    stosb
.have:
    mov si, name
    call puts
    mov si, s_sp
    call puts

    mov ax, 0x3D00
    mov dx, name
    int 0x21
    jc .no
    mov [fh], ax

.loop:
    mov bx, [fh]
    mov cx, 512
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    jc .rerr
    or ax, ax
    jz .done
    mov cx, ax
    add [total], ax
    adc word [total+2], 0
    mov si, buf
.sum:
    lodsb
    xor ah, ah
    add [sum], ax               ; position-sensitive: rotate, then add
    rol word [sum], 1
    loop .sum
    jmp short .loop
.done:
    mov bx, [fh]
    mov ah, 0x3E
    int 0x21
    mov si, s_len
    call puts
    mov ax, [total+2]
    call hex4
    mov ax, [total]
    call hex4
    mov si, s_sum
    call puts
    mov ax, [sum]
    call hex4
    call eol
    jmp short .fin
.no:
    mov si, s_noopen
    call puts
    jmp short .fin
.rerr:
    mov si, s_rerr
    call puts
    call hex4
    call eol
.fin:
    mov si, s_rdy
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

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
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp putc
puts:
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret

s_sp:     db ' ', 0
s_len:    db 'len=', 0
s_sum:    db ' sum=', 0
s_noopen: db 'OPEN FAILED', 13, 10, 0
s_rerr:   db 'READ FAILED AX=', 0
s_rdy:    db 'RDSUM READY', 13, 10, 0
fh:       dw 0
total:    dd 0
sum:      dw 0
name:     db 'DIGISND1.DAT', 0
          times 8 db 0
buf:      times 512 db 0
