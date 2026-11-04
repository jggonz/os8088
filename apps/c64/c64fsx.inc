; =============================================================================
; os8088 - apps/c64/c64fsx.inc
;
; THE COLOUR SCREEN (C64-SPEC 9.12): the C64's 320x200 display window in
; FSXM_VGA13, one C64 pixel a framebuffer byte, inside SPEC.md 53's exclusive
; bracket. The composers write an 8bpp band - 8 lines of 320 bytes, the
; package's own c64_b8 - and c64_fsx_put copies a span of it out. Everything
; in here runs only inside the bracket, after the mode set: the DAC and the
; attribute controller are the app's there (SPEC.md 53.7), and int 16h is the
; UI task's own keyboard (53.1).
;
; Derived from VICE (GPL-2-or-later, apps/c64/COPYING) by behaviour only.
; =============================================================================

section .bss
c64_c8tab:  resb 4                  ; a multicolour cell's four colours

section .text

; -----------------------------------------------------------------------------
; void c64_c8glyph(unsigned char *gb, const unsigned char *xm, int n,
;                  unsigned gseg, unsigned goff, int mask)
; n cells' eight glyph bytes, code & mask, from gseg:goff + code * 8, into gb
; at eight a cell - the layout the bitmap copy already has.
; -----------------------------------------------------------------------------
_c64_c8glyph:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [bp+10]                 ; gseg
    mov si, [bp+6]                  ; xm
    mov di, [bp+4]                  ; gb
    mov cx, [bp+8]
    cmp cx, 0
    jle .out
.c:
    mov bl, [si]
    inc si
    and bl, [bp+14]                 ; mask
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [bp+12]                 ; + goff
%rep 4
    mov ax, [es:bx]
    mov [di], ax
    add bx, 2
    add di, 2
%endrep
    loop .c
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_c8row(unsigned char *dst, const unsigned char *gb,
;                const unsigned char *k0, const unsigned char *k1,
;                const unsigned char *k2, const unsigned char *k3,
;                const unsigned char *mc, int n)
; n cells into the 8bpp band at dst (stride 320). Cell c's eight bytes are
; gb[c*8 .. c*8+7]; with mc[c] == 0 a set bit is k1[c] and a clear one k0[c],
; otherwise the pairs 00..11 are k0..k3. Every C64 mode is this with the
; per-cell colours c64scr.c's c64_fsx_compose fills (C64-SPEC 9.12).
;
; A HIRES PIXEL IS FOUR ALU OPS AND A STORE: `shl ah, 1 / sbb al, al` makes
; the bit a 0/FF mask, and k0 ^ (mask & (k0 ^ k1)) picks the colour.
; -----------------------------------------------------------------------------
_c64_c8row:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es                          ; the band is near: ES = DS for stosb
    cld
    mov si, [bp+6]                  ; gb
    mov di, [bp+4]                  ; dst
    cmp word [bp+18], 0
    jg .cell
    jmp .out
.cell:
    mov bx, [bp+8]
    mov al, [bx]                    ; k0
    mov bx, [bp+10]
    mov ah, [bx]                    ; k1
    mov bx, [bp+16]
    cmp byte [bx], 0
    jne .mc
    mov dh, al                      ; DH = k0
    xor ah, al
    mov dl, ah                      ; DL = k0 ^ k1
    mov cx, 8
.hl:
    mov ah, [si]
    inc si
%rep 8
    shl ah, 1
    sbb al, al
    and al, dl
    xor al, dh
    stosb
%endrep
    add di, 320 - 8
    loop .hl
    jmp short .next
.mc:
    mov [c64_c8tab], al
    mov [c64_c8tab+1], ah
    mov bx, [bp+12]
    mov al, [bx]
    mov [c64_c8tab+2], al
    mov bx, [bp+14]
    mov al, [bx]
    mov [c64_c8tab+3], al
    mov cx, 8
.ml:
    mov ah, [si]
    inc si
%rep 4
    xor bx, bx
    shl ah, 1
    rcl bl, 1
    shl ah, 1
    rcl bl, 1
    mov al, [c64_c8tab+bx]
    stosb
    stosb
%endrep
    add di, 320 - 8
    loop .ml
.next:
    sub di, 8 * 320 - 8             ; the next cell, line 0
    inc word [bp+8]
    inc word [bp+10]
    inc word [bp+12]
    inc word [bp+14]
    inc word [bp+16]
    dec word [bp+18]
    jz .out
    jmp .cell
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_c8spr(unsigned char *dst, const unsigned char *data, int x,
;                int lo, int hi, const unsigned char *cols, int mc, int xs)
; One sprite line - three data bytes - onto one band line whose pixel 0 is
; dst[0], from pixel x, only into pixels lo..hi-1. A hires bit is cols[2];
; a multicolour pair 01/10/11 is cols[1]/cols[2]/cols[3] and 00 is
; transparent. xs doubles every pixel (X-expansion).
; -----------------------------------------------------------------------------
_c64_c8spr:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]                  ; dst
    mov si, [bp+6]                  ; data
    mov bx, [bp+8]                  ; x
    mov cx, 3
.byte:
    push cx
    mov ah, [si]
    inc si
    cmp word [bp+16], 0
    jne .mcb
    mov cx, 8
.hb:
    shl ah, 1
    jnc .hskip
    push bx
    mov bx, [bp+14]
    mov al, [bx+2]
    pop bx
    call .plot
    cmp word [bp+18], 0
    je .hn
    call .plot
    jmp short .hn
.hskip:
    inc bx
    cmp word [bp+18], 0
    je .hn
    inc bx
.hn:
    loop .hb
    jmp short .bnext
.mcb:
    mov cx, 4
.mb:
    xor dx, dx
    shl ah, 1
    rcl dl, 1
    shl ah, 1
    rcl dl, 1
    or dl, dl                       ; (RCL leaves ZF alone)
    jz .mskip
    push bx
    mov bx, [bp+14]
    add bx, dx
    mov al, [bx]
    pop bx
    call .plot
    call .plot
    cmp word [bp+18], 0
    je .mn
    call .plot
    call .plot
    jmp short .mn
.mskip:
    add bx, 2
    cmp word [bp+18], 0
    je .mn
    add bx, 2
.mn:
    loop .mb
.bnext:
    pop cx
    loop .byte
    pop di
    pop si
    pop bp
    ret
; AL to pixel BX of the line when lo <= BX < hi, and BX moves on either way
.plot:
    cmp bx, [bp+10]
    jl .pn
    cmp bx, [bp+12]
    jge .pn
    mov [di+bx], al
.pn:
    inc bx
    ret

; -----------------------------------------------------------------------------
; void c64_fsx_put(unsigned seg, unsigned off, const unsigned char *src, int n)
; n bytes (even) of the band to seg:off - the framebuffer.
; -----------------------------------------------------------------------------
_c64_fsx_put:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld
    mov es, [bp+4]
    mov di, [bp+6]
    mov si, [bp+8]
    mov cx, [bp+10]
    shr cx, 1
    rep movsw
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned c64_fsx_key(void) - int 16h's AX when a key waits, else 0xFFFF.
; The UI task's own keyboard, polled: no events are dispatched inside the
; bracket (SPEC.md 53.1), apps/apple2/a2fsx.inc's a2_fsx_key exactly.
; -----------------------------------------------------------------------------
_c64_fsx_key:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ah, 0x01
    int 0x16                        ; cc8086:allow int 16h is legal HERE and
    jz .none                        ; only here (SPEC.md 53.7)
    xor ah, ah
    int 0x16                        ; cc8086:allow ...and it cannot block: the
    jmp short .out                  ; peek above said a key is waiting
.none:
    mov ax, 0xFFFF
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; void c64_fsx_dac(void) - the C64's sixteen colours into DAC entries 0..15,
; so a C64 colour number IS the pixel byte (C64-SPEC 9.12: Pepto's PAL
; palette, 8-bit values >> 2).
; -----------------------------------------------------------------------------
_c64_fsx_dac:
    push si
    mov dx, 0x03C8
    xor al, al
    out dx, al                      ; from DAC entry 0
    inc dx                          ; 0x03C9, the data port
    mov si, c64_pal48
    mov cx, 48
.b:
    mov al, [si]
    inc si
    out dx, al
    loop .b
    pop si
    ret

; -----------------------------------------------------------------------------
; void c64_fsx_border(int c) - the attribute controller's overscan colour,
; index $11, which is the border a VGA draws round mode 13h's picture. The
; index is written with bit 5 (PAS) SET, or the display goes blank.
; -----------------------------------------------------------------------------
_c64_fsx_border:
    push bp
    mov bp, sp
    mov dx, 0x03DA
    in al, dx                       ; the flip-flop to the index
    mov dx, 0x03C0
    mov al, 0x31                    ; $11 | PAS
    out dx, al
    mov al, [bp+4]
    and al, 15
    out dx, al
    pop bp
    ret

; Pepto's PAL palette (C64-SPEC 9.12), R, G, B for colours 0..15, 6-bit
c64_pal48:
    db  0,  0,  0,   63, 63, 63,   26, 13, 10,   28, 41, 44
    db 27, 15, 33,   22, 35, 16,   13, 10, 30,   46, 49, 27
    db 27, 19,  9,   16, 14,  0,   38, 25, 22,   17, 17, 17
    db 27, 27, 27,   38, 52, 33,   27, 23, 45,   37, 37, 37
