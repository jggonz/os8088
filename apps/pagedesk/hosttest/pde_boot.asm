; =============================================================================
; os8088 - apps/pagedesk/hosttest/pde_boot.asm      the PDENG target harness
;
; Entered from pde_bootsec.asm with DS = ES = CS = H_SEG and SS:SP = 0000:7C00
; (SS != DS, as under every os8088 task). It zeroes its own bss, runs
; pde_boot.c's pdb_main, writes DONE to the serial port and halts. The C talks
; to PDENG.MOD through pdb_call (the module's door is far-called at
; M_SEG:PDM_ENTRY), prints through pdb_putc (COM1) and reads the kernel stub's
; READ_SEQ counter. The arithmetic and far movers are pdmath.inc, the same
; source the module includes.
; =============================================================================
cpu 8086
bits 16
%include "pagedesk/pdabi.inc"

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

section .text
h_entry:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    push cs
    pop ds
    push cs
    pop es
    cld
    mov di, h_bss_beg
    mov cx, h_bss_end - h_bss_beg
    xor al, al
    rep stosb
    mov dx, 0x3F9                    ; COM1: 115200 8N1, interrupts off
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 3
    out dx, al
    call _pdb_main
    mov si, h_done
.d: mov al, [si]
    test al, al
    jz .h
    push si
    xor ah, ah
    push ax
    call _pdb_putc
    add sp, 2
    pop si
    inc si
    jmp .d
.h: cli
    hlt
    jmp .h
h_done: db 10, "DONE", 10, 0

; unsigned pdb_call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
_pdb_call:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [h_door]
    sbb dx, dx
    neg dx
    mov [_pdb_cf], dx
    pop bp
    ret

; void pdb_putc(unsigned c)
_pdb_putc:
    mov bx, sp
    mov ax, [ss:bx+2]
    mov dx, 0x3FD
.w: mov ah, al
    in al, dx
    test al, 0x20
    mov al, ah
    jz .w
    mov dx, 0x3F8
    out dx, al
    ret

; unsigned pdb_reads(void) - the kernel stub's READ_SEQ counter
_pdb_reads:
    push es
    mov ax, KERNEL_SEG_
    mov es, ax
    mov ax, [es:0x506]
    pop es
    ret

section .data
h_door: dw PDM_ENTRY, M_SEG
KERNEL_SEG_ equ 0x0060

section .bss
h_bss_beg:

section .text
%include "pde_boot.gen.asm"
%include "pagedesk/pdmath.inc"

section .bss
h_bss_end:
section .text
