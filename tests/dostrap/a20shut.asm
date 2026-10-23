; =============================================================================
; os8088 - tests/dostrap/a20shut.asm
;
; A20SHUT.COM - kern_mega's kern_dos in the HMA (docs/plans/KERN-MEGA-DOS-PLAN.md
; 7, wave W4) against a program that SHUTS A20 behind DOS's back.
;
;   nasm -f bin -w+error -o A20SHUT.COM tests/dostrap/a20shut.asm
;   make build/A20SHUT.COM
;
; It closes the gate with port 92h (the fast gate, which Bochs and every
; PS/2-class chipset have), proves it is shut with the wrap test (0000:FFE0
; against FFFF:FFF0), and then makes an ordinary INT 21h call. With kern_dos
; in the HMA that call can only work if the vector it goes through re-opens
; A20 first - kerndos/kdhma.inc's trampolines. A machine without them jumps
; into whatever wraps to FFFF:xxxx and dies.
;
; Exit codes: 0x20 the gate shut and the call after it was served (W4's
; claim); 0x21 the gate would not shut (nothing was tested - port 92h is not
; this machine's gate); the call failing is a hang, not a code. Under a real
; DOS with DOS=HIGH it runs unchanged and should also exit 0x20.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

start:
    mov dx, s_banner
    mov ah, 0x09
    int 0x21
    cli
    in al, 0x92
    and al, 0xFC                ; A20 off; bit 0 is a reset, left clear
    out 0x92, al
    sti
    mov cx, 0x200               ; ...and let it take
.w:
    call shut
    jnc .shut
    loop .w
    mov dx, s_noshut
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C21
    int 0x21
.shut:
    mov dx, s_shut              ; THE CALL: through int 21h with the gate shut
    mov ah, 0x09
    int 0x21
    call shut                   ; ...and the gate is open again, which only
    mov dx, s_open              ; the trampoline can have done
    jc .o
    mov dx, s_still
.o:
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C20
    int 0x21

; shut - CF = 0 the gate is SHUT, CF = 1 open (the wrap test, mega_a20t's)
shut:
    push ds
    push es
    push ax
    push bx
    cli
    xor ax, ax
    mov ds, ax
    dec ax
    mov es, ax
    mov bx, [0xFFE0]
    not word [es:0xFFF0]
    mov ax, [0xFFE0]
    not word [es:0xFFF0]
    sti
    cmp ax, bx
    pop bx
    pop ax
    pop es
    pop ds
    je .open
    clc                         ; the flip landed on 0000:FFE0: wrapped, shut
    ret
.open:
    stc
    ret

s_banner:   db 'A20SHUT: closing the A20 gate behind DOS', 13, 10, '$'
s_shut:     db 'A20 is SHUT, and this line came through INT 21h', 13, 10, '$'
s_open:     db '...and the call opened it again', 13, 10, '$'
s_still:    db '...and it is still shut', 13, 10, '$'
s_noshut:   db 'port 92h did not shut A20 - nothing tested', 13, 10, '$'
