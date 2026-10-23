; =============================================================================
; os8088 - tests/x3bench/x3bboot.asm - the XTMP3 bench's boot sector
;
; NOT shipped software (tests/). Sector 0 of the bench's image: it reads the
; bench's directory (LBA 1, 16 sectors) to 0060:0000 and the harness (LBA 17,
; the directory's +14 sectors) to 1000:0000, and enters it with DL = the boot
; drive. From a hard disk (QEMU) with int 13h's extended read; from a floppy
; (MartyPC's 5150, whose BIOS has no extended read) with CHS reads a track at
; most, never across a 64 KB physical boundary, on the geometry x3bench.py
; patches into geo_spt / geo_heads. tests/x3bench/x3bench.py builds the
; image; tests/x3bench/x3bench.asm is what it boots.
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
    cmp byte [drive], 0x80
    jb rdchs
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13
    jc rderr
    ret

; rdchs - the DAP's request as CHS reads: a track's rest at most, cut at a
; 64 KB physical boundary (DMA error 09h otherwise)
rdchs:
    mov ax, [dap_lba]               ; (a copy: the caller advances the DAP)
    mov [w_lba], ax
    mov ax, [dap_seg]
    mov [w_seg], ax
    mov ax, [dap_cnt]
    mov [w_cnt], ax
.next:
    mov ax, [w_seg]                 ; sectors to the next 64 KB boundary
    mov cl, 4
    shl ax, cl                      ; (the segment's low 12 bits, x 16)
    neg ax
    mov cl, 9
    shr ax, cl
    jnz .b
    mov ax, 128
.b:
    mov [lim], ax
    mov ax, [w_lba]                 ; LBA -> track, sector
    xor dx, dx
    div word [geo_spt]
    mov bx, dx                      ; sector - 1
    xor dx, dx
    div word [geo_heads]            ; AX = cylinder, DX = head
    mov ch, al
    mov dh, dl
    mov cl, bl
    inc cl
    mov ax, [geo_spt]               ; this track's rest
    sub ax, bx
    cmp ax, [w_cnt]
    jbe .t
    mov ax, [w_cnt]
.t:
    cmp ax, [lim]
    jbe .l
    mov ax, [lim]
.l:
    push ax
    mov bx, [w_seg]
    mov es, bx
    xor bx, bx
    mov dl, [drive]
    mov ah, 0x02
    int 0x13
    pop ax
    jc rderr
    add [w_lba], ax
    sub [w_cnt], ax
    mov bx, 32
    mul bx
    add [w_seg], ax
    cmp word [w_cnt], 0
    jne .next
    ret

rderr:
    mov al, '!'
    mov dx, 0xE9
    out dx, al
    hlt
    jmp rderr

drive:  db 0
dap:    db 16, 0
dap_cnt: dw 0
dap_off: dw 0
dap_seg: dw 0
dap_lba: dw 0, 0, 0, 0
lim:    dw 0
w_lba:  dw 0
w_seg:  dw 0
w_cnt:  dw 0

    times 504 - ($ - $$) db 0
geo_spt:   dw 18                    ; the floppy's geometry: x3bench.py's
geo_heads: dw 2                     ; (offsets 504, 506)

    times 510 - ($ - $$) db 0
    dw 0xAA55
