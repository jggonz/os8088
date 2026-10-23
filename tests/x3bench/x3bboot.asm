; =============================================================================
; os8088 - tests/x3bench/x3bboot.asm - the XTMP3 bench's boot sector
;
; NOT shipped software (tests/). A raw hard-disk image's sector 0: it reads
; the bench's directory (LBA 1, 16 sectors) to 0060:0000 and the harness
; (LBA 17, the directory's +14 sectors) to 1000:0000 with int 13h's extended
; read, and enters it with DL = the boot drive. tests/x3bench/x3bench.py
; builds the image; tests/x3bench/x3bench.asm is what it boots.
; =============================================================================
    cpu 8086
    bits 16
    org 0x7C00

    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [drive], dl
    mov word [dap_seg], 0x0060      ; the directory
    mov word [dap_lba], 1
    mov word [dap_cnt], 16
    call rd
    mov ax, 0x0060
    mov es, ax
    mov cx, [es:14]                 ; the harness's sectors
    mov word [dap_lba], 17
    mov word [dap_seg], 0x1000
.h:
    mov ax, cx
    cmp ax, 64
    jbe .n
    mov ax, 64
.n:
    mov [dap_cnt], ax
    sub cx, ax
    push cx
    push ax
    call rd
    pop ax
    pop cx
    add [dap_lba], ax
    mov bx, ax                      ; 512 bytes = 32 paragraphs a sector
    mov ax, 32
    mul bx
    add [dap_seg], ax
    or cx, cx
    jnz .h
    mov dl, [drive]
    jmp 0x1000:0x0000

rd:
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13
    jc .err
    ret
.err:
    mov al, '!'
    mov dx, 0xE9
    out dx, al
    hlt
    jmp .err

drive:  db 0
dap:    db 16, 0
dap_cnt: dw 0
dap_off: dw 0
dap_seg: dw 0
dap_lba: dw 0, 0, 0, 0

    times 510 - ($ - $$) db 0
    dw 0xAA55
