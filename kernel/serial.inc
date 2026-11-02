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
; but AX/CF. The instance stamp is the OS's, never a caller-supplied owner:
; DH = the caller's slot, which ser_api_x refuses past INST_MAX and the module
; turns into its record (srl_open) - so DI is never spent here (kernel size
; pass 14: inst_ptr and its AX/DI banks lived here, -20 resident)
osapi_serial:
    push dx
    call inst_caller
    call COLD_SEG:ser_api_x
    pop dx
    ret

; AL=instance being torn down, or FFh (mouse_unhook's reboot sweep). Release
; before its region can be freed. Clobbers AX and FLAGS: both callers pop or
; overwrite AX next and read no flag (inst_rel_rec, mouse_unhook)
ser_release_inst:
    cmp word [ser_row], 0xffff
    je .out
    call far [SERFP+SER_RELEASE]
    call COLD_SEG:ser_tidy      ; drops the module iff that closed the lease
.out:
    ret

section .cold
; DH = the caller's slot (osapi_serial). No lease is held exactly when the
; module is not loaded (ser_tidy drops it on the call that ends one), so only
; Open with no lease reads the disk; with one, every verb goes straight to it
ser_api_x:
    cmp dh, INST_MAX
    jae .bad
    cmp word [ser_row], 0xffff
    jne .call
    cmp al, 1
    jb .open
    mov ax, 0                   ; Close with nothing open: CF = 0 from the
    je .r                       ; compare (AL = 1), done
    inc ax                      ; any other verb: busy
    stc
.r:
    retf
.bad:
    mov ax, 5
    stc
    retf
.open:
    push ax
    mov al, MOD_SER
    call mod_need
    pop ax
    jnc .call
    mov ax, 8                   ; CF = 1 from mod_need
    retf
.call:
    call far [SERFP+SER_API]
                                ; ...and on into ser_tidy, whose retf is ours
; ser_tidy - far: drop SERIAL.DRV unless a lease is held. Every register and
; the FLAGS kept, so the verb's AX/CF answer comes back through it
ser_tidy:
    pushf
    cmp word [ser_row], 0xffff
    jne .keep
    push ax
    mov al, MOD_SER
    call mod_drop
    pop ax
.keep:
    popf
    retf

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
srl_seen: db 0                 ; [mou_seen] when the lease was taken
SRL_O_SEG   equ srl_seg         ; three OFFSETS in the image, for code that
SRL_O_BUF   equ srl_buf         ; holds the image's own DATA selector -
SRL_O_MASK  equ srl_mask        ; fsx.inc's real-mode bracket (SPEC.md 53.2.3,
                                ; W7) - and reads them through it, never
                                ; through KERNEL_SEG

; Module API. DH=the stamped instance slot (< INST_MAX), ES=caller.
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
    CSDO mov [CSEG srl_div], bh
    mov al, dh                  ; DI = the caller's record: inst_ptr's sum,
    mov ah, I_RECSZ             ; done here so that no resident byte does it
    mul ah                      ; (DH < INST_MAX: ser_api_x refused the rest)
    add ax, inst_tab
    xchg ax, di
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
    mov ch, bl                  ; bit 7: the USER named this port (115.1.1)
    and bl, 0x7f
    cmp bl, 1
    jb .absent
    cmp bl, 2
    ja .absent
    dec bl
    xor bh, bh
    shl bx, 1
    mov ax, [bx+mou_bases]
    or ax, ax
    jz .absent
    push es                     ; ADVERTISED, in ANY of the four BIOS COM
    mov bp, 0x40                ; slots: a Compaq lists them in its SETUP's
    mov es, bp                  ; order, not IBM's 3F8-then-2F8 (115.1)
    xor bp, bp
.bda:
    cmp ax, [es:bp]
    je .listed
    inc bp
    inc bp
    cmp bp, 8
    jb .bda
    pop es
    mov ax, 9                   ; a UART the BIOS does not list (115.1)
    stc
    ret
.listed:
    pop es
    cmp byte [mou_hpst], 2
    jne .unsettled
    cmp bl, [mou_port]
    je .mouse
.take:
    CSDO mov [CSEG srl_base], ax
    CSDO mov [CSEG srl_seg], es
    CSDO mov [CSEG srl_buf], si
    CSDO mov [CSEG srl_owner], dh
    mov dl, [mou_seen]          ; settled or not, as the lease begins: what
    CSDO mov [CSEG srl_seen], dl ; srl_close asks of the contest
    mov [ser_row], bx
    mov dl, [bx+mou_masks]
    CSDO mov [CSEG srl_mask], dl
    in al, 0x21
    and al, dl
    CSDO mov [CSEG srl_pic], al
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
    CSDO mov [CSEG srl_lcr], al
    and al, 0x7f
    out dx, al
    sub dx, 2
    in al, dx
    CSDO mov [CSEG srl_ier], al
    xor al, al
    out dx, al
    add dx, 2
    mov al, 0x80
    out dx, al
    sub dx, 3
    in al, dx
    CSDO mov [CSEG srl_dll], al
    mov al, [cs:srl_div]       ; 115200/divisor: 12 = 9600, 48 = 2400
    out dx, al
    inc dx
    in al, dx
    CSDO mov [CSEG srl_dlm], al
    xor al, al
    out dx, al
    add dx, 2
    mov al, 3
    out dx, al
    inc dx
    in al, dx
    CSDO mov [CSEG srl_mcr], al
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
.unsettled:                     ; no mouse has spoken yet (115.1.1): this
    cmp byte [bx+mou_ident], 0  ; port answered the reset edge LIKE one, so
    jne .mouse                  ; it is one, whoever says otherwise
    test ch, 0x80               ; the user said "not the mouse": the machine
    jnz .take                   ; may have no serial mouse at all
    mov cl, [mou_ident]         ; a mouse identified on the OTHER port
    or cl, [mou_ident+2]
    jnz .take
    push dx                     ; DSR or DCD driven: a modem, or a null
    mov dx, ax                  ; modem whose far end holds DTR. A serial
    add dx, 6                   ; mouse drives neither line
    in al, dx
    sub dx, 6
    xchg ax, dx                 ; AX = the base again, DL = the MSR
    test dl, 0xa0
    pop dx
    jnz .take
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
    cmp byte [cs:srl_seen], 0   ; THE CONTEST SETTLED WHILE THIS WAS LEASED
    jne .put                    ; (115.1.1), and mou_lockon skipped the row: it
    cmp byte [mou_seen], 0      ; is a loser now, so it goes back as mou_lockon
    je .put                     ; leaves one - IER 0, and its line masked
    CSDO mov byte [CSEG srl_ier], 0 ; unless it is the mouse's (or unknown) -
    mov al, [mou_line]          ; and not as the open found it, which was the
    or al, al                   ; contest's armed state (SPEC.md 115.2)
    jz .put
    mov ah, [cs:srl_mask]
    cmp al, ah
    je .put
    CSDO mov [CSEG srl_pic], ah
.put:
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
    CSDO mov word [CSEG srl_base], 0
    xor ax, ax
    ret

; The mouse ISR has saved all registers and owns EOI - all but BX, its scan
; index, which srl_pump leaves alone (the ring is addressed through SI) so
; the ISR need not bank it. At most 16 RX bytes per entry, then one TX byte;
; original 8250s normally have one waiting.
srl_irq:
    call srl_pump
    retf
srl_pump:
    mov dx, [cs:srl_base]
    add dx, 2
    in al, dx                  ; acknowledge THRE before queuing its next byte
    mov ax, [cs:srl_seg]
    mov es, ax
    mov si, [cs:srl_buf]
    mov bp, 16
.rx:
    mov dx, [cs:srl_base]
    add dx, 5
    in al, dx
    mov ah, al
    and al, 0x1e
    or [es:si+2], al
    test ah, 1
    jz .tx
    sub dx, 5
    in al, dx
    mov dl, [es:si]
    mov dh, dl
    inc dh
    cmp dh, [es:si+1]
    je .overflow
    xor dh, dh
    mov di, si
    add di, dx
    mov [es:di+SER_RX], al
    inc dl
    mov [es:si], dl
    jmp short .more
.overflow:
    or byte [es:si+2], 0x80
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
    mov dl, [es:si+4]
    cmp dl, [es:si+3]
    je .idle
    xor dh, dh
    mov di, si
    add di, dx
    mov al, [es:di+SER_TX]
    mov dx, [cs:srl_base]
    out dx, al
    inc byte [es:si+4]
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
