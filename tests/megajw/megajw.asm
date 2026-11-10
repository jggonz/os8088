; =============================================================================
; os8088 - tests/megajw/megajw.asm
;
; MEGAJW, a TEST instrument (nothing under tests/ ships): THE LARGEST CLAIM
; AGAINST THE FREE MEMORY, on kern_mega (KERN-MEGA-PLAN 14.166, SPEC.md
; 66.4.4). At its start it asks what the machine says is free - the pool's KB
; (OSAPI_XMEM_CAPS) and the conventional heap's total and largest
; (OSAPI_MEM_AVAIL) - and what ONE claim can be given (OSAPI_MEM_HUGE, AX =
; 0), then claims exactly that, touches the first and the last word of every
; 64KB window, frees it, and asks for one KB more (which must be refused: the
; answer is the most). Every answer lands in MJ_RES, an 'MJWRES01' block in
; its own image, and the window stays open so a row can read it there - on
; 86Box, which has no port E9 - and close it. The same answers go to port E9
; for Bochs. A row opens it as often as it wants a reading: each instance
; measures once, at its start.
;
;   +0  'MJWRES01'          +8  the query's KB         +10 its CF (0 = asked)
;   +12 the claim's CF      +14 its first selector     +16 its step
;   +18 first window's linear base (dword)   +22 the last window's (dword)
;   +26 XMEM_CAPS AX (pool KB)   +28 MEM_AVAIL AX (largest)   +30 BX (total)
;   +32 the touch: 1 every window read back, 2 one did not
;   +34 one KB more: 1 refused (exact), 2 granted (the answer read short)
;   +36 1 = done (written last)
;   +38 OSAPI_XMEM_ALLOC of all +26 says is free - XT-MP3's hold (SPEC.md
;       121.6.1: CAPS, then ONE block of the file's size): 1 granted, 2
;       refused, 0 not asked (no pool)
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAJW', mj_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

mj_entry:
    push si
    push di
    call OSAPI_XMEM_CAPS            ; AX = the pool's free KB
    mov [mj_res+26], ax
    MEMKB_AVAIL                     ; AX = largest, BX = total
    mov [mj_res+28], ax
    mov [mj_res+30], bx
    xor ax, ax                      ; --- the query -------------------------
    xor bx, bx                      ; (64KB windows: the ask reads BX)
    call OSAPI_MEM_HUGE
    sbb cx, cx
    mov [mj_res+10], cx
    mov [mj_res+8], ax
    jc .win
    or ax, ax
    jz .win
    xor bx, bx                      ; --- the claim, of exactly that ---------
    call OSAPI_MEM_HUGE
    sbb bx, bx
    mov [mj_res+12], bx
    jc .win
    mov [mj_res+14], dx
    mov [mj_res+16], cx
    call mj_lin                     ; the first window's base
    mov [mj_res+18], ax
    mov [mj_res+20], dx
    mov ax, [mj_res+8]              ; windows: KB / 64, rounded up
    add ax, 63
    mov cl, 6
    shr ax, cl
    mov [mj_nwin], ax
    dec ax
    mul word [mj_res+16]
    add ax, [mj_res+14]
    mov dx, ax
    call mj_lin                     ; ...and the last's
    mov [mj_res+22], ax
    mov [mj_res+24], dx
    mov word [mj_res+32], 1
    xor bp, bp                      ; --- a word at each window's two ends --
.w:
    call mj_win                     ; ES = window BP, DI = its last word
    mov [es:0], bp
    mov ax, bp
    not ax
    mov [es:di], ax
    inc bp
    cmp bp, [mj_nwin]
    jb .w
    xor bp, bp
.r:
    call mj_win
    cmp [es:0], bp
    jne .bad
    mov ax, bp
    not ax
    cmp [es:di], ax
    je .rn
.bad:
    mov word [mj_res+32], 2
.rn:
    inc bp
    cmp bp, [mj_nwin]
    jb .r
    push ds
    pop es
    mov dx, [mj_res+14]             ; --- the free --------------------------
    call OSAPI_MEM_FREE
    mov ax, [mj_res+8]              ; --- one KB more: refused --------------
    inc ax
    xor bx, bx
    call OSAPI_MEM_HUGE
    mov word [mj_res+34], 1
    jc .win
    mov word [mj_res+34], 2
    push ds
    pop es
    call OSAPI_MEM_FREE
.win:
    push ds
    pop es
    mov ax, [mj_res+26]             ; --- XT-MP3's hold: ONE pool block of
    or ax, ax                       ; every KB CAPS says is free
    jz .nox
    mov dx, ax                      ; DX:AX = KB x 1024
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    call OSAPI_XMEM_ALLOC
    mov word [mj_res+38], 2
    jc .nox
    mov word [mj_res+38], 1
    call OSAPI_XMEM_FREE
.nox:
    mov word [mj_res+36], 1         ; the gate, last
    mov si, mj_s_e9                 ; ...and the line for Bochs
    call mj_say
    mov ax, [mj_res+8]
    call mj_hex4
    mov si, mj_s_pool
    call mj_say
    mov ax, [mj_res+26]
    call mj_hex4
    mov si, mj_s_conv
    call mj_say
    mov ax, [mj_res+30]
    call mj_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [mj_res+28]
    call mj_hex4
    mov si, mj_s_got
    call mj_say
    mov ax, [mj_res+12]
    call mj_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [mj_res+32]
    call mj_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [mj_res+34]
    call mj_hex4
    mov al, 10
    out 0xE9, al
    mov si, mj_tpl                  ; the window that keeps it open
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
    clc
.out:
    pop di
    pop si
    ret

mj_paint:                           ; W_PAINT arrives white-filled
    ret

; mj_win - BP = a window -> ES = its selector, DI = the offset of its last
; word (the claim's end in the last window). Clobbers AX, DX
mj_win:
    mov ax, bp
    mul word [mj_res+16]
    add ax, [mj_res+14]
    mov es, ax
    mov di, 0xFFFE
    mov ax, bp
    inc ax
    cmp ax, [mj_nwin]
    jb .o
    mov ax, bp                      ; the last: (KB - 64 x BP) KB, its end
    mov dx, 64
    mul dx
    mov di, [mj_res+8]
    sub di, ax                      ; DI = its KB (1..64)
    mov ax, di
    mov dx, 1024
    mul dx                          ; DX:AX = its bytes (64KB: DX = 1)
    sub ax, 2
    mov di, ax
.o:
    ret

; mj_lin - DX = a selector -> DX:AX its linear base
mj_lin:
    push bx
    mov bl, SEG_LIN
    call OSAPI_SEG
    pop bx
    ret

mj_say:
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
mj_hex4:
    push ax
    mov al, ah
    call mj_hex2
    pop ax
mj_hex2:
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

mj_tpl:
    dw 8, 400, 200, 40
    dw mj_ttl, mj_paint, 0, 0
mj_ttl:     db 'MegaJW', 0
mj_s_e9:    db 'MEGAJW: max ', 0
mj_s_pool:  db ' pool ', 0
mj_s_conv:  db ' conv ', 0
mj_s_got:   db ' claim ', 0

mj_res:     db 'MJWRES01'
            times 32 db 0
mj_nwin     dw 0

OS88_IMAGE_END
