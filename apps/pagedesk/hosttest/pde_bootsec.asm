; os8088 - apps/pagedesk/hosttest/pde_bootsec.asm
; The 512-byte boot sector of the PDENG target harness (pde_boot.py). It reads
; the rest of the image - harness, kernel stub, PDENG.MOD, the PDF - into the
; segments the build line gives it (-DH_SEG= -DH_SECT= ...), one sector a BIOS
; call, then jumps to the harness with DS = ES = its own segment and SS:SP =
; 0000:7C00, so SS != DS as it is under every os8088 task.
cpu 8086
bits 16
org 0x7C00
start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov ds, ax
    mov [drive], dl
    mov si, table
.next:
    mov cx, [si+4]                  ; sectors
    jcxz .done
    mov ax, [si+2]                  ; first LBA
    mov [lba], ax
    mov ax, [si]                    ; destination segment
    mov es, ax
    xor bx, bx
.sec:
    push cx
    mov ax, [lba]
    xor dx, dx
    mov cx, 18
    div cx                          ; AX = track, DX = sector - 1
    inc dx
    mov cl, dl                      ; CL = sector
    xor dx, dx
    mov bx, 2
    div bx                          ; AX = cylinder, DX = head
    mov ch, al
    mov dh, dl
    mov dl, [drive]
    xor bx, bx
    mov ax, 0x0201
    int 0x13
    jc .fail
    mov ax, es
    add ax, 0x20
    mov es, ax
    inc word [lba]
    pop cx
    loop .sec
    add si, 6
    jmp .next
.done:
    mov ax, H_SEG
    mov ds, ax
    mov es, ax
    jmp H_SEG:0
.fail:
    mov si, msg
.p: lodsb
    test al, al
    jz .h
    mov ah, 0x0E
    int 0x10
    jmp .p
.h: hlt
    jmp .h
msg: db "DISK ERROR", 0
drive: db 0
lba: dw 0
table:
    dw H_SEG, H_LBA, H_SECT
    dw K_SEG, K_LBA, K_SECT
    dw M_SEG, M_LBA, M_SECT
    dw P_SEG, P_LBA, P_SECT
    dw 0, 0, 0
times 510 - ($ - $$) db 0
dw 0xAA55
