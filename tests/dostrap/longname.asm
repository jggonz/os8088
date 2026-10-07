; LONGNAME.COM - WHAT DOES DOS DO WITH A NAME THAT IS NOT 8.3?  MIT with the
; tree.
;
; PLAYEGA.EXE (The Playroom, 1989) opens `B:plysample.bin` - a NINE-character
; stem - and os8088 answers "path not found", so the program prints
; `B:plysample.bin FILE ERROR` and terminates abnormally.  The file on the
; disk is PLYSAMPL.BIN and the program plainly expects to reach it, which
; means a real DOS TRUNCATES rather than refusing.
;
; This is that question put to both machines (docs/DOS-DEBUGGING.md): it runs
; under os8088 AND under a real DOS unchanged, and each line is one AH=3Dh
; with the name printed beside the answer, so the two screens diff with
; nothing to interpret.
;
;   nasm -f bin -o LONGNAME.COM tests/dostrap/longname.asm
;
; The file every line is reaching for is the SAME FILE.  A row that opens on
; one machine and refuses on the other is the defect, and a row that refuses
; on BOTH is DOS's own limit and not ours to lift.
    org 0x100
    cpu 8086

start:
    mov si, tab
.next:
    cmp byte [si], 0
    je .done
    mov [pat], si
    mov si, s_open
    call puts
    mov si, [pat]
    call puts
    mov si, s_pad
    call puts

    mov dx, [pat]
    mov ax, 0x3D00
    int 0x21
    jc .err
    mov [hnd], ax
    mov si, s_ok
    call puts
    mov ax, [hnd]
    call hex4
    mov bx, [hnd]
    mov ah, 0x3E
    int 0x21
    call eol
    jmp short .step
.err:
    mov [gax], ax
    mov si, s_cf
    call puts
    mov ax, [gax]
    call hex4
    call eol
.step:
    mov si, [pat]
.skip:
    lodsb                           ; past this row's own NUL
    or al, al
    jnz .skip
    jmp short .next
.done:
    mov si, s_key
    call puts
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; The rows, and what each one asks:
;   the 8.3 name                      - the control: the file IS there
;   a 9-character stem                - Playroom's own name
;   a 12-character stem               - well past any truncation
;   a 4-character extension           - the other half of the rule
;   the drive letter in front of one  - "B:name" is a relative name too
tab:
    db 'PLYSAMPL.BIN', 0
    db 'plysample.bin', 0
    db 'plysamplelong.bin', 0
    db 'PLYSAMPL.BINARY', 0
    db 'B:plysample.bin', 0
    db 0

s_open: db 'open ', 0
s_pad:  db ' ', 0
s_ok:   db 'OK h=', 0
s_cf:   db 'CF ax=', 0
s_key:  db 'KEY', 13, 10, 0

putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
puts:
    push ax
.l:
    mov al, [si]
    inc si
    or al, al
    jz .o
    call putc
    jmp short .l
.o:
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret
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
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
    jmp short putc

pat:    equ $
hnd:    equ $ + 2
gax:    equ $ + 4
