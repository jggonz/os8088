; Desktop serial lease, SPEC.md 115. On-demand SERIAL.DRV, both kernels.
; Resident: one routed mouse row, API/lifetime thunks, three far entries.
SER_NENT equ 3
SER_API equ 0
SER_IRQ equ 4
SER_RELEASE equ 8
SER_SIZE equ 517
SER_RX equ 5
SER_TX equ 261

section .text
ser_row: dw 0xffff

; X cell: ES is the caller's package segment. UI task only. Preserve all
; but AX/CF. The instance stamp is the OS's, never a caller-supplied owner.
osapi_serial:
    push dx
    push di
    call inst_caller
    cmp dh, INST_MAX
    jae .bad
    push ax
    mov al, dh
    call inst_ptr
    pop ax
    call COLD_SEG:ser_api_x
.out:
    pop di
    pop dx
    ret
.bad:
    mov ax, 5
    stc
    jmp short .out

; AL=instance being torn down. Release before its region can be freed.
ser_release_inst:
    cmp word [ser_row], 0xffff
    je .out
    pushf
    push ax
    call far [SERFP+SER_RELEASE]
    jc .done
    call COLD_SEG:ser_drop_x
.done:
    pop ax
    popf
.out:
    ret

section .cold
ser_api_x:
    cmp al, 0
    jne .existing
    push ax
    mov al, MOD_SER
    call mod_need
    pop ax
    jc .nodisk
.existing:
    cmp word [ser_row], 0xffff
    jne .call
    cmp al, 0
    je .call
    cmp al, 1
    je .closed
    mov ax, 1
    stc
    retf
.closed:
    xor ax, ax
    retf
.call:
    call far [SERFP+SER_API]
    pushf
    cmp word [ser_row], 0xffff
    jne .keep
    call ser_drop
.keep:
    popf
    retf
.nodisk:
    mov ax, 8
    stc
    retf

ser_drop_x:
    call ser_drop
    retf
ser_drop:
    push ax
    mov al, MOD_SER
    call mod_drop
    pop ax
    ret

section .mods
mods_ser_header:
    dw 0x384f
    db MOD_VER, MOD_SER
    dw BUILD_NUM, MOD_STAMP
    dw mods_ser_end, SER_NENT
    dw srl_api,srl_irq,srl_release

; DS=kernel; data owned by the image is always CS-relative. No ring lives
; in this module: the pinned caller's buffer holds all producer/consumer data.
srl_base: dw 0
srl_seg: dw 0
srl_buf: dw 0
srl_owner: db 0xff
srl_lcr: db 0
srl_ier: db 0
srl_dll: db 0
srl_dlm: db 0
srl_mcr: db 0
srl_div: db 12
srl_pic: db 0
srl_mask: db 0

; Module API. DI=stamped instance record, DH=instance slot, ES=caller.
srl_api:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    call srl_body
    pushf
    pop bp
    popf
    shr bp, 1                  ; retain the result's CF after restoring IF
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf
srl_body:
    cmp al, 0
    je srl_open
    cmp word [cs:srl_base], 0
    je .busy
    cmp dh, [cs:srl_owner]
    jne .busy
    cmp al, 1
    je srl_close
    cmp al, 2
    jne .ring
    push ax
    call srl_pump              ; a missed IRQ edge cannot strand pending bytes
    pop ax
.ring:
    mov bx, [cs:srl_buf]
    mov dx, [cs:srl_seg]
    mov es, dx
    cmp al, 2
    je .read
    cmp al, 3
    jne .bad
    mov dl, [es:bx+3]
    mov dh, dl
    inc dh
    cmp dh, [es:bx+4]
    je .full
    xor dh, dh
    mov di, bx
    add di, dx
    mov [es:di+SER_TX], cl
    inc dl
    mov [es:bx+3], dl           ; publish last
    mov dx, [cs:srl_base]
    inc dx
    mov al, 3                  ; RX+THRE; IRQ starts a stopped transmitter
    out dx, al
    call srl_pump              ; also kick the first ready byte immediately
    xor ax, ax
    ret
.read:
    mov dl, [es:bx+1]
    cmp dl, [es:bx]
    je .empty
    xor dh, dh
    mov di, bx
    add di, dx
    mov al, [es:di+SER_RX]
    mov ah, [es:bx+2]
    mov byte [es:bx+2], 0
    inc dl
    mov [es:bx+1], dl
    clc
    ret
.empty:
    mov ax, 6
    stc
    ret
.full:
    mov ax, 7
    stc
    ret
.bad:
    mov ax, 5
    stc
    ret
.busy:
    mov ax, 1
    stc
    ret

srl_open:
    cmp word [cs:srl_base], 0
    je .fresh
    cmp dh, [cs:srl_owner]      ; the holder MOVES its lease (115.1): the old
    jne srl_body.busy           ; port is put back as Close would, the module
    push dx                     ; stays, and a refusal below leaves it closed
    call srl_close
    pop dx
.fresh:
    or bh, bh                   ; BH = the divisor (115.1); 0 is 9600
    jnz .div
    mov bh, 12
.div:
    mov [cs:srl_div], bh
    cmp byte [di+I_STATE], 0
    je srl_body.bad
    mov ax, es
    cmp ax, [di+I_SPTR]
    jne srl_body.bad
    mov ax, si
    add ax, SER_SIZE
    jc srl_body.bad
    cmp ax, [di+I_SIZE]
    ja srl_body.bad
    cmp bl, 1
    jb .absent
    cmp bl, 2
    ja .absent
    dec bl
    xor bh, bh
    shl bx, 1
    cmp byte [mou_hpst], 2
    jne .unsettled
    cmp bl, [mou_port]
    je .mouse
    mov ax, [bx+mou_bases]
    or ax, ax
    jz .absent
    push es
    mov bp, 0x40
    mov es, bp
    cmp ax, [es:bx]
    pop es
    jne .absent
    mov [cs:srl_base], ax
    mov [cs:srl_seg], es
    mov [cs:srl_buf], si
    mov [cs:srl_owner], dh
    mov [ser_row], bx
    mov dl, [bx+mou_masks]
    mov [cs:srl_mask], dl
    in al, 0x21
    and al, dl
    mov [cs:srl_pic], al
    in al, 0x21
    not dl
    and al, dl
    out 0x21, al
    mov di, si
    mov cx, SER_SIZE
    xor ax, ax
    cld
    rep stosb
    mov dx, [cs:srl_base]
    add dx, 3
    in al, dx
    mov [cs:srl_lcr], al
    and al, 0x7f
    out dx, al
    sub dx, 2
    in al, dx
    mov [cs:srl_ier], al
    xor al, al
    out dx, al
    add dx, 2
    mov al, 0x80
    out dx, al
    sub dx, 3
    in al, dx
    mov [cs:srl_dll], al
    mov al, [cs:srl_div]       ; 115200/divisor: 12 = 9600, 48 = 2400
    out dx, al
    inc dx
    in al, dx
    mov [cs:srl_dlm], al
    xor al, al
    out dx, al
    add dx, 2
    mov al, 3
    out dx, al
    inc dx
    in al, dx
    mov [cs:srl_mcr], al
    and al, 0xe7
    or al, 0x0b                ; DTR/RTS/OUT2, no loopback
    out dx, al
    inc dx
    in al, dx                  ; clear LSR and an old mouse byte
    test al, 1
    jz .arm
    sub dx, 5
    in al, dx
    add dx, 5
.arm:
    sub dx, 4
    mov al, 1
    out dx, al
    xor ax, ax
    ret
.unsettled:
    mov ax, 2
    stc
    ret
.mouse:
    mov ax, 3
    stc
    ret
.absent:
    mov ax, 4
    stc
    ret

srl_close:
    mov word [ser_row], 0xffff ; withdraw IRQ routing before state is released
    mov dx, [cs:srl_base]
    add dx, 3
    mov al, 0x80
    out dx, al
    sub dx, 3
    mov al, [cs:srl_dll]
    out dx, al
    inc dx
    mov al, [cs:srl_dlm]
    out dx, al
    add dx, 2
    mov al, [cs:srl_lcr]
    and al, 0x7f
    out dx, al
    inc dx
    mov al, [cs:srl_mcr]
    out dx, al
    sub dx, 3
    mov al, [cs:srl_ier]
    out dx, al
    add dx, 2
    mov al, [cs:srl_lcr]
    out dx, al
    in al, 0x21
    mov ah, [cs:srl_mask]
    not ah
    and al, ah
    or al, [cs:srl_pic]
    out 0x21, al
    mov word [cs:srl_base], 0
    xor ax, ax
    ret

; The mouse ISR has saved all registers and owns EOI. At most 16 RX bytes
; per entry, then one TX byte; original 8250s normally have one waiting.
srl_irq:
    call srl_pump
    retf
srl_pump:
    mov dx, [cs:srl_base]
    add dx, 2
    in al, dx                  ; acknowledge THRE before queuing its next byte
    mov ax, [cs:srl_seg]
    mov es, ax
    mov bx, [cs:srl_buf]
    mov bp, 16
.rx:
    mov dx, [cs:srl_base]
    add dx, 5
    in al, dx
    mov ah, al
    and al, 0x1e
    or [es:bx+2], al
    test ah, 1
    jz .tx
    sub dx, 5
    in al, dx
    mov dl, [es:bx]
    mov dh, dl
    inc dh
    cmp dh, [es:bx+1]
    je .overflow
    xor dh, dh
    mov di, bx
    add di, dx
    mov [es:di+SER_RX], al
    inc dl
    mov [es:bx], dl
    jmp short .more
.overflow:
    or byte [es:bx+2], 0x80
.more:
    dec bp
    jnz .rx
    mov dx, [cs:srl_base]
    add dx, 5
    in al, dx
    mov ah, al
.tx:
    test ah, 0x20
    jz .out
    mov dl, [es:bx+4]
    cmp dl, [es:bx+3]
    je .idle
    xor dh, dh
    mov di, bx
    add di, dx
    mov al, [es:di+SER_TX]
    mov dx, [cs:srl_base]
    out dx, al
    inc byte [es:bx+4]
.out:
    ret
.idle:
    mov dx, [cs:srl_base]
    inc dx
    mov al, 1                  ; RX only; no empty THRE interrupt storm
    out dx, al
    ret

; AL=instance, or FFh reboot sweep. Every register and FLAGS preserved
; except CF (clear means a lease was closed).
srl_release:
    push ax
    push dx
    pushf
    cli
    cmp al, 0xff
    je .close
    cmp al, [cs:srl_owner]
    jne .no
.close:
    call srl_close
    popf
    pop dx
    pop ax
    clc
    retf
.no:
    popf
    pop dx
    pop ax
    stc
    retf
mods_ser_end:
SER_IMAGE_SIZE equ mods_ser_end - $$
section .text
