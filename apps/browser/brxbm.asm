; =============================================================================
; os8088 - apps/browser/brxbm.asm
;
; Browser 1.7 - XBM, the X bitmap Netscape showed inline (SPEC.md 71.31):
; part 6 of BROWSER.O88, a decoder part of PiXEL's shape (pxpart.inc) hosted
; by part 1 as pxpng.asm and the others are. An XBM is C source:
;
;     #define name_width 16
;     #define name_height 8
;     static char name_bits[] = { 0x00, 0xff, ... };
;
; a row (w + 7) / 8 bytes, a byte's LOW bit the leftmost pixel, a 1 bit the
; ink. Two entries: 0 white, 1 black.
;
;   HEAD     the two #defines and where the data starts, from the file (it is
;            in memory whole: [px_hseg]:0, PXK_HLEN bytes)
;   DECODE   each byte's hex in turn, a row at a time into the row buffer,
;            each to K_EMIT
;
; tools/pixelsim.py's xbm_header and xbm_rows are this file in Python.
; =============================================================================

%include "pxpart.inc"
%include "pxlink.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD xb_init, xb_decode, xb_info, xb_head, xb_plans

XB_DIMMAX   equ 4096

; PXV_INIT - out AX = PXP_PROBE
xb_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO, PXV_PLANS - not this part's
xb_info:
xb_plans:
    mov ax, PXE_NOTSUP
    stc
    retf

; =============================================================================
; PXV_HEAD - DS = the host, DI = the context
; =============================================================================
xb_head:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [di + PXK_HSEG]
    mov cx, [di + PXK_HLEN]
    xor si, si
    xor ax, ax
    mov [cs:xb_w], ax
    mov [cs:xb_h], ax
.scan:                              ; the next '#define' or '{'
    cmp si, cx
    jae .bad
    mov al, [es:si]
    cmp al, '{'
    je .data
    cmp al, '#'
    jne .nx
    call xb_define
    jmp .scan
.nx:
    inc si
    jmp .scan
.data:
    inc si
    mov [cs:xb_doff], si
    mov ax, [cs:xb_w]
    mov bx, [cs:xb_h]
    or ax, ax
    jz .bad
    or bx, bx
    jz .bad
    cmp ax, XB_DIMMAX
    ja .big
    cmp bx, XB_DIMMAX
    ja .big
    mov [di + PXK_SW], ax
    mov [di + PXK_SH], bx
    mov byte [di + PXK_RF], RF_IDX
    mov byte [di + PXK_BITS], 1
    mov byte [di + PXK_PACK], 0
    mov word [di + PXK_NPAL], 2
    mov word [di + PXK_DPARA], 0
    mov byte [di + PXK_FLAGS], 0
    mov bx, px_spal                 ; 0 the paper, 1 the ink
    mov word [bx], 0xFFFF
    mov word [bx + 2], 0x00FF
    mov word [bx + 4], 0
    clc
    jmp short .out
.big:
    mov ax, PXD_BIG
    stc
    jmp short .out
.bad:
    mov ax, PXD_HEAD
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; xb_define - ES:SI at a '#' (CX the file's bytes): `#define name value`, a
; name ending _width or _height setting that dimension. SI past it. AX, BX,
; DX clobbered
xb_define:
    inc si
    mov bx, xb_s_def
    call xb_word                    ; 'define'
    jc .ret
    call xb_sp
    mov dx, si                      ; DX = the name's start
.nm:
    cmp si, cx
    jae .ret
    mov al, [es:si]
    cmp al, ' '
    jbe .nmd
    inc si
    jmp .nm
.nmd:
    push si                         ; which: the name's end, backwards
    mov bx, xb_s_w
    call xb_tail
    mov bx, xb_w
    jnc .val
    mov bx, xb_s_h
    call xb_tail
    mov bx, xb_h
    jnc .val
    pop si
    ret
.val:
    pop si
    call xb_sp
    xor ax, ax                      ; the decimal value
.d:
    cmp si, cx
    jae .put
    mov dl, [es:si]
    sub dl, '0'
    cmp dl, 9
    ja .put
    push dx
    mov dx, 10
    mul dx
    pop dx
    jc .ovf
    add al, dl
    adc ah, 0
    jc .ovf
    inc si
    jmp .d
.ovf:
    mov ax, 0xFFFF
.put:
    mov [cs:bx], ax            ; (BX: xb_w or xb_h, this part's)
.ret:
    ret

; xb_word - the NUL string at CS:BX at ES:SI? CF=0 yes, SI past it; CF=1
; no, SI as it was. AX kept? No: AL clobbered
xb_word:
    push si
.l:
    mov al, [cs:bx]
    or al, al
    jz .yes
    cmp si, cx
    jae .no
    cmp al, [es:si]
    jne .no
    inc bx
    inc si
    jmp .l
.yes:
    add sp, 2
    clc
    ret
.no:
    pop si
    stc
    ret

; xb_tail - does the name DX..SI end with the NUL string at CS:BX? CF=0 yes.
; AX, BX kept? No: AX clobbered; others kept
xb_tail:
    push bx
    push si
    push di
    mov di, bx                      ; DI = the string's end
.e:
    cmp byte [cs:di], 0
    je .cmp
    inc di
    jmp .e
.cmp:
    cmp di, bx
    je .yes
    cmp si, dx
    je .no
    dec di
    dec si
    mov al, [es:si]
    cmp al, [cs:di]
    je .cmp
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    pop si
    pop bx
    ret

; xb_sp - SI past spaces and tabs. AL clobbered
xb_sp:
    cmp si, cx
    jae .ret
    mov al, [es:si]
    cmp al, ' '
    je .n
    cmp al, 9
    jne .ret
.n:
    inc si
    jmp xb_sp
.ret:
    ret

; =============================================================================
; PXV_DECODE - the worker: DS = the host, DI = the context
; =============================================================================
xb_decode:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    push ds
    mov ax, [di + PXK_SVC + 2 * PXS_EMIT]
    mov [cs:xb_emitf], ax
    mov ax, [cs:PXP_PKG]
    mov [cs:xb_emitf + 2], ax
    mov ax, [di + PXK_RSEG]
    mov [cs:xb_rseg], ax
    mov ax, [px_flatlen]
    mov [cs:xb_len], ax
    mov ax, [px_flatseg]
    mov [cs:xb_fseg], ax
    mov si, [cs:xb_doff]
    xor bp, bp                      ; BP = y
.row:
    cmp bp, [cs:xb_h]
    jae .ok
    xor di, di                      ; DI = x
.byte:
    cmp di, [cs:xb_w]
    jae .emit
    call xb_hex                     ; AL = the next byte
    jc .trunc
    mov es, [cs:xb_rseg]
    mov cx, 8
.bit:
    cmp di, [cs:xb_w]
    jae .byte
    mov ah, al
    and ah, 1
    mov [es:di], ah
    inc di
    shr al, 1
    loop .bit
    jmp .byte
.emit:
    mov ax, bp
    mov bx, 0xFFFF
    mov dx, bx
    push si
    call far [cs:xb_emitf]
    pop si
    jc .out                         ; (AX = PXD_ABORT)
    inc bp
    jmp .row
.trunc:
    mov ax, PXD_TRUNC
    stc
    jmp short .out
.ok:
    xor ax, ax
    clc
.out:
    pop ds
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; xb_hex - AL = the next `0x..` byte from the file at SI; SI past it. CF=1
; the file ended first. AH, BX, CX clobbered
xb_hex:
    mov es, [cs:xb_fseg]
    mov cx, [cs:xb_len]
.find:                              ; an 'x' after a '0'
    cmp si, cx
    jae .end
    mov al, [es:si]
    inc si
    or al, 0x20
    cmp al, 'x'
    jne .find
    cmp si, 2
    jb .find
    cmp byte [es:si - 2], '0'
    jne .find
    xor bl, bl                      ; BL = the value
    mov bh, 2                       ; at most two digits
.dig:
    cmp si, cx
    jae .done
    mov al, [es:si]
    sub al, '0'
    cmp al, 9
    jbe .d
    or al, 0x20                     ; ('a' - '0') | 20h: a-f
    sub al, 'a' - '0'
    cmp al, 5
    ja .done
    add al, 10
.d:
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    or bl, al
    inc si
    dec bh
    jnz .dig
.done:
    mov al, bl
    clc
    ret
.end:
    stc
    ret

xb_s_def:   db 'define', 0
xb_s_w:     db '_width', 0
xb_s_h:     db '_height', 0

xb_emitf:   dd 0
xb_rseg:    dw 0
xb_fseg:    dw 0
xb_len:     dw 0
xb_doff:    dw 0
xb_w:       dw 0
xb_h:       dw 0
