; DOSFULL.COM - a HELD stream that runs out of room, and a DELETE while its
; hold is pending (SPEC.md 18.4.9.1, 18.4.9.2, 96.53). OURS, MIT.
;
; Creates FULL.DAT and writes 8 KB chunks until AH=40h refuses - the box's
; flushes are one HELD WRITE_SEQ stream, so the refusing flush is a held call
; that failed with the disk full - then DELETES the file without closing it
; and exits. The host fscks the floppy afterwards: every cluster must be free
; but the program's own. Two defects each left hundreds of lost clusters:
; the failed call's half-built sub-chain flushed rather than freed, and a
; DELETE that never committed the hold, so the held chain was never linked
; to anything the delete could free.
;
; Prints "FULL <n> DEL <c>" - n chunks accepted, c the delete's carry - and
; READY, and waits for a key so the host can read it.

    org 0x100
    cpu 8086

    mov ah, 0x3C
    xor cx, cx
    mov dx, fname
    int 0x21
    jc bad
    mov [fh], ax
.l:
    mov ah, 0x40
    mov bx, [fh]
    mov cx, 8192
    mov dx, buf
    int 0x21
    jc .stop
    cmp ax, 8192
    jne .stop
    inc word [n]
    jmp short .l
.stop:
    mov dx, s_full
    mov ah, 0x09
    int 0x21
    mov ax, [n]
    call putn
    mov ah, 0x41                ; ...and away, with the handle still open
    mov dx, fname
    int 0x21
    mov al, '0'
    adc al, 0
    mov [s_del + 5], al
    mov dx, s_del
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21
bad:
    mov dx, s_bad
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C01
    int 0x21

putn:                           ; AX, unsigned decimal
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p:
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .p
    ret

fname:  db 'FULL.DAT', 0
s_full: db 'FULL $'
s_del:  db ' DEL ?', 13, 10, 'READY', 13, 10, '$'
s_bad:  db 'FAILED at create', 13, 10, 'READY', 13, 10, '$'
fh:     dw 0
n:      dw 0
buf:
