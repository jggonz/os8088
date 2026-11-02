; BIGSUM - the CODE half of DRVCUT.EXE (tests/drvcut.py, SPEC.md 18.91.4.1).
; OURS, MIT with the rest of the tree.
;
; tests/drvcut.py builds the .EXE: an MZ header, this at its load module's
; offset 0, and BIGSUM_N bytes of seeded data at offset 100h - over 64KB, so a
; 64KB physical page boundary ALWAYS falls inside the load module wherever the
; program lands. The program checksums its own data where the loader put it
; (rotate, then add - rdsum.asm's sum, position-sensitive) and prints
;
;   DRVCUT SUM=hhhh PSP=hhhh
;
; then waits for a key. The host computes the same sum off the bytes it wrote
; and reads the PSP to say whether the load buffer was 512-aligned - a row
; whose buffer happened to be aligned has tested nothing, and says so.
    cpu 8086
    org 0

%ifndef BIGSUM_W
  %define BIGSUM_W 36000            ; words of data (72,000 bytes)
%endif

start:
    mov bp, es                      ; the PSP, as DOS enters an .EXE
    mov ax, cs
    mov ds, ax
    mov si, data
    mov cx, BIGSUM_W
    xor dx, dx
    cld
.l:
    lodsw
    rol dx, 1
    add dx, ax
    cmp si, 0x8000
    jb .n
    sub si, 0x8000                  ; ...and the segment carries it on
    mov ax, ds
    add ax, 0x800
    mov ds, ax
.n:
    loop .l
    mov ax, cs
    mov ds, ax
    mov ax, dx
    mov di, msum
    call hex4
    mov ax, bp
    mov di, mpsp
    call hex4
    mov dx, msg
    mov ah, 0x09
    int 0x21
    mov ah, 0x08                    ; wait for a key: the bracket ends with us
    int 0x21
    mov ax, 0x4C00
    int 0x21

hex4:                               ; AX -> four hex digits at DS:DI
    mov cx, 4
.h:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 7
.d:
    mov [di], al
    inc di
    pop ax
    loop .h
    ret

msg:  db 13, 10, 'DRVCUT SUM='
msum: db '0000 PSP='
mpsp: db '0000', 13, 10, '$'

    times 0x100 - ($ - $$) db 0
data:
