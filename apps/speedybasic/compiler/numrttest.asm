; Raw 8086 gate for the generated-C numeric adapter plus flat sbnum engine.
cpu 8086
bits 16
org 0x7c00

SEG_STACK   equ 0x1000
IMG_SECTORS equ 24

section .text
section .rodata follows=.text align=2
section .data follows=.rodata align=2
section .bss follows=.data align=2 nobits
section .text

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    mov word [lba], 1
    mov bx, 0x7e00
.read:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    xor dl, dl
    mov ax, 0x0201
    int 0x13
    jc .diskerr
    add bx, 512
    inc word [lba]
    jmp .read
.go:
    jmp 0:body
.diskerr:
    mov al, 'D'
    call putc
    jmp halt
lba: dw 0
times 510-($-$$) db 0
dw 0xaa55

body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xfff0
    xor ax, ax
    mov ds, ax
    mov es, ax
    sti
    cld
    call _numrt_ccprobe
    mov bp, ax
    mov al, 'N'
    call putc
    mov al, 'R'
    call putc
    mov ax, bp
    call putdec
    mov si, msg_ok
    or bp, bp
    jz .answer
    mov si, msg_bad
.answer:
    call puts
    mov al, 1
    or bp, bp
    jz .exit
    mov al, 2
.exit:
    mov dx, 0xf4
    out dx, al
halt:
    cli
    hlt
    jmp halt

putc:
    push ax
    push dx
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3f8
    out dx, al
    pop dx
    ret

puts:
    push ax
.next:
    mov al, [cs:si]
    inc si
    or al, al
    jz .done
    call putc
    jmp .next
.done:
    pop ax
    ret

putdec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .divide
.digit:
    pop ax
    add al, '0'
    call putc
    loop .digit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

msg_ok:  db ' - numrt OK',13,10,0
msg_bad: db ' - numrt FAIL',13,10,0

%include "numrt_ccprobe.gen.asm"

; Array calls are host-tested.  These resolve numrt's optional sbmem seam in
; this numeric-only raw image without claiming memory from a boot sector.
_sbm_get_lo:
_sbm_get_hi:
_sbm_kind_get:
    xor ax, ax
    ret
_sbm_set_pair:
    xor ax, ax
    ret
_sbm_kind_set:
    ret

%define SBN_FLAT
%include "speedybasic/sbnum.inc"
