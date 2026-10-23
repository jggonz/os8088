; =============================================================================
; os8088 - tests/megahib/megahib.asm
;
; MEGAHIB, a TEST instrument (nothing under tests/ ships): the XMS WITNESS for
; a hibernate on kern_mega (docs/plans/KERN-MEGA-DOS-PLAN.md 8, wave W5). It
; is MEGAXM's patterns HELD: on open it allocates 64KB through the XMEM cells
; and copies sixteen 4KB patterns out, and keeps its window - so the block is
; a live instance's, in the pool when the machine hibernates. Any key in its
; window reads all sixteen back and compares. On port E9:
;
;   MEGAHIB: held DDDDAAAA      the block's linear base, at the open
;   MEGAHIB: ok | bad NN | no NN    a key's verdict (NN the chunk or step)
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAHIB', mz_entry

MZ_CHUNK    equ 4096
MZ_N        equ 16

mz_entry:
    push si
    mov si, mz_tpl
    call OSAPI_WM_CREATE
    jc .out
    push bx
    xor ax, ax                  ; DX:AX = 64KB
    mov dx, 1
    call OSAPI_XMEM_ALLOC
    mov cl, 1
    jc .no
    mov [mz_base], ax
    mov [mz_base+2], dx
    xor bx, bx
.fill:
    call mz_fill
    mov di, 0                   ; conventional -> extended
    call mz_copy
    mov cl, 2
    jc .no
    inc bx
    cmp bx, MZ_N
    jb .fill
    mov si, mz_s_held
    call mz_say
    mov ax, [mz_base+2]
    call mz_hex4
    mov ax, [mz_base]
    call mz_hex4
    call mz_nl
    jmp short .ok
.no:
    mov si, mz_s_no
    call mz_say
    mov al, cl
    call mz_hex2
    call mz_nl
.ok:
    pop bx
    clc
.out:
    pop si
    ret

mz_paint:
    ret

mz_onkey:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    xor bx, bx
.in:
    push bx
    mov di, mz_buf              ; cleared first, so a copy that moved nothing
    mov cx, MZ_CHUNK / 2        ; cannot pass
    xor ax, ax
    push ds
    pop es
    cld
    rep stosw
    pop bx
    mov di, 1                   ; extended -> conventional
    call mz_copy
    mov cl, 3
    jc .no
    call mz_cmp
    mov cl, bl
    jc .bad
    inc bx
    cmp bx, MZ_N
    jb .in
    mov si, mz_s_ok
    call mz_say
    call mz_nl
    jmp short .done
.no:
    mov si, mz_s_no
    jmp short .code
.bad:
    mov si, mz_s_bad
.code:
    call mz_say
    mov al, cl
    call mz_hex2
    call mz_nl
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; BL = the chunk. mz_buf <- its pattern
mz_fill:
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, mz_buf
    mov al, bl
    mov cx, MZ_CHUNK
.l:
    stosb
    add al, 7
    loop .l
    pop di
    pop cx
    pop ax
    ret

; BL = the chunk -> CF = 1 if mz_buf is not its pattern
mz_cmp:
    push ax
    push cx
    push si
    mov si, mz_buf
    mov al, bl
    mov cx, MZ_CHUNK
.l:
    cmp [si], al
    jne .no
    inc si
    add al, 7
    loop .l
    clc
    jmp short .x
.no:
    stc
.x:
    pop si
    pop cx
    pop ax
    ret

; BL = the chunk, DI = the direction -> OSAPI_XMEM_COPY of mz_buf and
; base + chunk * 4KB. CF as the cell's. Keeps BX
mz_copy:
    push bx
    push si
    mov ax, bx
    mov cl, 12
    shl ax, cl                  ; chunk * 4096: under 64KB, no carry
    mov dx, [mz_base+2]
    add ax, [mz_base]
    adc dx, 0
    push ds
    pop es
    mov si, mz_buf
    mov cx, MZ_CHUNK
    call OSAPI_XMEM_COPY
    pop si
    pop bx
    ret

mz_say:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mz_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mz_hex4:
    push ax
    mov al, ah
    call mz_hex2
    pop ax
mz_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

mz_s_held:  db 'MEGAHIB: held ', 0
mz_s_ok:    db 'MEGAHIB: ok', 0
mz_s_no:    db 'MEGAHIB: no ', 0
mz_s_bad:   db 'MEGAHIB: bad ', 0
mz_tpl:     dw 40, 300, 200, 60, mz_ttl, mz_paint, mz_onkey, 0
mz_ttl:     db 'MEGAHIB', 0

    OS88_BSS 4 + MZ_CHUNK
    OS88_IMAGE_END

mz_base     equ os88_image_end + 0
mz_buf      equ os88_image_end + 4
