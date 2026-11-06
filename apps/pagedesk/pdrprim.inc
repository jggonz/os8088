; =============================================================================
; os8088 - apps/pagedesk/pdrprim.inc           PDREND's assembly primitives
;
; %included by pdrend.asm AFTER the compiled C (PAGEDESK-SPEC 2.3, 20.3.5:
; SPEC.md 73.11 - nothing that loops over pixels is C). Every routine is the
; cdecl twin of a C function declared in pdrend.h, and its C TWIN in
; apps/pagedesk/hosttest/pdr_host.c is the specification: tests/pdrprim runs
; both over the same vectors (hosttest/pdr_primtest.py).
;
;   pdr_muldiv   trunc(a*b/c), a 32-bit intermediate, SATURATING (never #DE)
;   pdr_fill1/4  one band row span, 1bpp pattern / 4bpp nibble
;   pdr_glyph    one glyph bitmap into 1bpp (clear/set) or 4bpp (nibble)
;   pdr_imgrow   one image row, nearest sample, dithered, into the band
;   pdr_eng      far call into PDENG's door through the vector in pdr_eng_off
;   pdr_blit     OSAPI_GFX_BLIT1 / BLIT4 on a band in a far segment
;
; REGISTERS: cdecl as SPEC.md 73.3 - the caller cleans, BP/DS/SS/SP are
; preserved, every other register is scratch, DF is clear on return. Arguments
; are read with [bp+n] (SS), the parameter blocks with [cs:...] - CS is this
; module's own segment and DS is whatever the routine points at the far data.
; cpu 8086: no 80186 shifts, no pusha.
; =============================================================================

; ---- parameter block offsets: THE ASSEMBLY SIDE of pdrend.h's struct pdr_gp and
; struct pdr_ip; hosttest/pdr_layout.py compares them with the C offsets ------
GP_BSEG   equ 0
GP_BOFF   equ 2
GP_STRIDE equ 4
GP_X      equ 6
GP_NROW   equ 8
GP_SKIP   equ 10
GP_GSEG   equ 12
GP_GOFF   equ 14
GP_WBYTES equ 16
GP_WPX    equ 18
GP_MODE   equ 20
GP_COL    equ 22
GP_SHEAR  equ 24
GP_ROWSTEP equ 26
GP_SIZE_  equ 28

IP_BSEG   equ 0
IP_BOFF   equ 2
IP_X0     equ 4
IP_X1     equ 6
IP_SSEG   equ 8
IP_SOFF   equ 10
IP_SW     equ 12
IP_XLO    equ 14
IP_XHI    equ 16
IP_SLO    equ 18
IP_SHI    equ 20
IP_SMODE  equ 22
IP_INV    equ 24
IP_DMODE  equ 26
IP_DYROW  equ 28
IP_COL    equ 30
IP_SIZE_  equ 32


PDM_MAGIC_REND  equ 0x5250          ; 'PR' - pdabi.h PDM_MAGIC_REND
PDM_ABI         equ 1               ; pdabi.h PDM_ABI
PDE_BUSY        equ 65              ; pdabi.h PDE_BUSY


; ---- the primitives' variables (in the bss the door clears) ------------------
section .bss
_pdr_eng_off:   resw 1              ; the far vector into PDENG: offset, then segment
_pdr_eng_seg:   resw 1
_pdr_eng_ax:    resw 1
_pdr_eng_dx:    resw 1
_pdr_gp:        resb GP_SIZE_
_pdr_ip:        resb IP_SIZE_
gl_row:         resw 1
gl_rows:        resw 1
gl_r:           resw 1
gl_bi:          resw 1
gl_x:           resw 1
gl_nb:          resw 1
gl_k:           resb 1
gl_collo:       resb 1
gl_colhi:       resb 1
gl_pad:         resb 1
gi_row:         resw 1
gi_dy4:         resw 1
section .text

; -----------------------------------------------------------------------------
; int pdr_muldiv(int a, int b, int c)
; -----------------------------------------------------------------------------
_pdr_muldiv:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    imul word [bp+6]                ; DX:AX = a * b
    mov cx, [bp+8]
    mov bx, dx
    xor bx, cx                      ; bit 15 = the sign of the quotient
    push bx
    or dx, dx
    jns .pos
    neg dx                          ; |DX:AX|
    neg ax
    sbb dx, 0
.pos:
    or cx, cx
    jns .cpos
    neg cx
.cpos:
    jz .ovf                         ; c == 0
    cmp dx, cx
    jae .ovf                        ; the quotient would not fit 16 bits
    div cx
    cmp ax, 0x7FFF
    ja .ovf
    jmp short .sign
.ovf:
    mov ax, 0x7FFF                  ; saturate
.sign:
    pop bx
    or bx, bx
    jns .done
    neg ax
.done:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void pdr_fill1(seg, rowoff, x0, x1, pat)   x0 <= x1, pixels of one band row
; -----------------------------------------------------------------------------
%macro FILL1_RMW 0                  ; mask in AL, pattern at [bp+12], ES:DI
    mov ah, [bp+12]
    and ah, al
    not al
    and al, [es:di]
    or al, ah
    mov [es:di], al
%endmacro

_pdr_fill1:
    push bp
    mov bp, sp
    push di
    mov ax, [bp+4]
    mov es, ax
    mov di, [bp+6]
    mov bx, [bp+8]
    mov dx, [bp+10]
    mov cl, bl
    and cl, 7
    mov ah, 0xFF
    shr ah, cl                      ; the mask from x0's bit to the right edge
    mov cl, dl
    and cl, 7
    xor cl, 7
    mov al, 0xFF
    shl al, cl                      ; the mask from the left edge to x1's bit
    mov cl, 3
    shr bx, cl
    shr dx, cl
    add di, bx
    sub dx, bx
    jnz .multi
    and al, ah
    FILL1_RMW
    jmp short .out
.multi:
    push ax
    mov al, ah
    FILL1_RMW
    inc di
    dec dx
    jz .last
    mov cx, dx
    mov al, [bp+12]
    cld
    rep stosb
.last:
    pop ax                          ; AL = the right mask
    FILL1_RMW
.out:
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void pdr_fill4(seg, rowoff, x0, x1, col)
; -----------------------------------------------------------------------------
_pdr_fill4:
    push bp
    mov bp, sp
    push di
    mov ax, [bp+4]
    mov es, ax
    mov di, [bp+6]
    mov bx, [bp+8]
    mov dx, [bp+10]
    mov cx, bx
    shr cx, 1
    add di, cx                      ; the byte of x0
    mov al, [bp+12]
    and al, 15                      ; AL = the colour
    test bl, 1
    jz .even
    mov ah, [es:di]                 ; an odd x0 starts on the low nibble
    and ah, 0xF0
    or ah, al
    mov [es:di], ah
    inc di
    inc bx
.even:
    mov cx, dx
    sub cx, bx
    inc cx                          ; pixels left
    jle .done
    mov si, cx
    mov ah, al
    mov cl, 4
    shl ah, cl
    or ah, al                       ; AH = the byte of two pixels
    mov cx, si
    shr cx, 1
    jz .tail
    mov al, ah
    cld
    rep stosb
.tail:
    test si, 1
    jz .done
    mov al, [bp+12]
    and al, 15
    mov cl, 4
    shl al, cl
    mov ah, [es:di]
    and ah, 0x0F
    or ah, al
    mov [es:di], ah
.done:
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned pdr_eng(verb, bx, cx, dx) - the far call into PDENG. Returns 0 and
; leaves AX/DX in pdr_eng_ax/pdr_eng_dx, or the PDE_ code when CF was set.
; -----------------------------------------------------------------------------
_pdr_eng:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    push ds
    call far [_pdr_eng_off]         ; off, seg: the pair the primary passed
    pop ds
    jc .err
    mov [_pdr_eng_ax], ax
    mov [_pdr_eng_dx], dx
    xor ax, ax
.err:
    pop bp
    ret

; -----------------------------------------------------------------------------
; int pdr_blit(kind, seg, stride, x, y, w, rows) - kind 1: BLIT1, else BLIT4.
; BP is an ARGUMENT to the slot, so the frame is read first and BP set last
; (apps/cc/os88thunk.asm's note on os88_gfx_blit1 is this paragraph).
; -----------------------------------------------------------------------------
_pdr_blit:
    push bp
    mov bp, sp
    mov di, [bp+4]
    mov ax, [bp+6]
    mov es, ax
    xor si, si
    mov ax, [bp+10]
    mov bx, [bp+12]
    mov cx, [bp+14]
    mov dx, [bp+16]
    mov bp, [bp+8]                  ; the stride, last
    cmp di, 1
    jne .b4
    call OSAPI_GFX_BLIT1
    sbb ax, ax                      ; CF -> 0 / -1
    jmp short .out
.b4:
    call OSAPI_GFX_BLIT4
    xor ax, ax
.out:
    mov bp, sp                      ; [sp] is the caller's BP: nothing is pushed
    pop bp
    ret

; -----------------------------------------------------------------------------
; void pdr_glyph(void) - the block _pdr_gp (pdrend.h). Preconditions the C
; checked: the glyph and its shear margin lie inside the band row.
;   mode 0/1: the bitmap row (up to 4 bytes) is shifted right by (x & 7) into
;             5 bytes and AND-NOT'ed (0) / OR'ed (1) into the band;
;   mode 2:   a nibble of col for every set bit.
; A row's shear shift is ((shear - 1) - row + 8) >> 2; the caller has taken 2
; off x, so the net is floor((ascent - row) / 4).
; -----------------------------------------------------------------------------
%define GPV(o) [cs:_pdr_gp + o]

_pdr_glyph:
    push bp
    push si
    push di
    push ds
    push es
    cld
    mov ax, GPV(GP_BSEG)
    mov es, ax
    mov ax, GPV(GP_GSEG)
    mov ds, ax
    mov si, GPV(GP_GOFF)
    mov ax, GPV(GP_BOFF)
    mov [cs:gl_row], ax
    mov ax, GPV(GP_NROW)
    mov [cs:gl_rows], ax
    mov ax, GPV(GP_SKIP)
    mov [cs:gl_r], ax
    mov al, GPV(GP_COL)
    and al, 15
    mov [cs:gl_collo], al
    mov cl, 4
    shl al, cl
    mov [cs:gl_colhi], al
.row:
    ; ---- this row's x (with the shear) --------------------------------------
    mov cx, GPV(GP_X)
    mov dx, GPV(GP_SHEAR)
    or dx, dx
    jz .noshear
    dec dx
    sub dx, [cs:gl_r]
    add dx, 8
    shr dx, 1
    shr dx, 1
    add cx, dx
.noshear:
    cmp word GPV(GP_MODE), 2
    je .nib
    ; ---- bytes: position, shift, byte count ---------------------------------
    mov [cs:gl_x], cx
    mov dx, cx
    mov cl, 3
    shr dx, cl                      ; byte index of the left edge
    mov [cs:gl_bi], dx
    mov ax, [cs:gl_x]
    and al, 7
    mov [cs:gl_k], al               ; the bit offset inside that byte
    xor ah, ah
    add ax, GPV(GP_WPX)
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [cs:gl_nb], ax              ; ((k + wpx + 7) >> 3) bytes are touched
    ; ---- the source row into AH AL BH BL = b0 b1 b2 b3 ----------------------
    xor bx, bx
    lodsb
    mov ah, al
    xor al, al
    mov cx, GPV(GP_WBYTES)
    cmp cx, 2
    jb .loaded
    lodsb                           ; b1
    cmp cx, 3
    jb .loaded
    push ax
    lodsb
    mov bh, al                      ; b2
    cmp cx, 4
    jb .l3
    lodsb
    mov bl, al                      ; b3
.l3:
    pop ax
.loaded:
    ; ---- shift right by k ---------------------------------------------------
    xor ch, ch
    mov cl, [cs:gl_k]
    or cl, cl
    jz .shifted
.sh:
    shr ah, 1
    rcr al, 1
    rcr bh, 1
    rcr bl, 1
    rcr ch, 1
    dec cl
    jnz .sh
.shifted:
    ; ---- combine into the band: DI = row + byte index ---------------------
    mov di, [cs:gl_row]
    add di, [cs:gl_bi]
    mov dx, [cs:gl_nb]
    cmp word GPV(GP_MODE), 0
    jne .setmode
    not ah
    not al
    not bh
    not bl
    not ch
    and [es:di], ah
    dec dx
    jz .rowdone
    and [es:di+1], al
    dec dx
    jz .rowdone
    and [es:di+2], bh
    dec dx
    jz .rowdone
    and [es:di+3], bl
    dec dx
    jz .rowdone
    and [es:di+4], ch
    jmp short .rowdone
.setmode:
    or [es:di], ah
    dec dx
    jz .rowdone
    or [es:di+1], al
    dec dx
    jz .rowdone
    or [es:di+2], bh
    dec dx
    jz .rowdone
    or [es:di+3], bl
    dec dx
    jz .rowdone
    or [es:di+4], ch
    jmp short .rowdone

    ; ---- mode 2: a nibble per set bit ---------------------------------------
.nib:
    mov bx, cx                      ; BX = x of the next pixel
    mov dx, GPV(GP_WPX)             ; pixels left
    mov bp, GPV(GP_WBYTES)          ; bytes left
.nbyte:
    lodsb
    mov ah, al
    mov cx, 8
.nbit:
    test ah, 0x80
    jz .nnext
    mov di, bx
    shr di, 1
    add di, [cs:gl_row]
    test bl, 1
    jnz .nlow
    mov al, [es:di]
    and al, 0x0F
    or al, [cs:gl_colhi]
    mov [es:di], al
    jmp short .nnext
.nlow:
    mov al, [es:di]
    and al, 0xF0
    or al, [cs:gl_collo]
    mov [es:di], al
.nnext:
    shl ah, 1
    inc bx
    dec dx
    jz .nrowend
    dec cx
    jnz .nbit
    dec bp
    jnz .nbyte
.nrowend:
.rowdone:
    mov ax, GPV(GP_ROWSTEP)
    add [cs:gl_row], ax
    inc word [cs:gl_r]
    dec word [cs:gl_rows]
    jnz .row
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void pdr_imgrow(void) - the block _pdr_ip. One band row from one source row:
; dest pixel x takes source (acc >> 16), acc += step, mirrored when smode bit
; 8 is set; sample -> value; the 4x4 ordered dither; nibble or bit.
; -----------------------------------------------------------------------------
%define IPV(o) [cs:_pdr_ip + o]

_pdr_imgrow:
    push bp
    push si
    push di
    push ds
    push es
    cld
    mov ax, IPV(IP_BSEG)
    mov es, ax
    mov ax, IPV(IP_BOFF)
    mov [cs:gi_row], ax
    mov ax, IPV(IP_SSEG)
    mov ds, ax
    mov ax, IPV(IP_DYROW)
    mov cl, 2
    shl ax, cl
    mov [cs:gi_dy4], ax
    mov bx, IPV(IP_X0)              ; BX = x
    mov bp, IPV(IP_XLO)             ; BP = acc low
    mov dx, IPV(IP_XHI)             ; DX = acc high (the source index)
.px:
    mov si, dx
    cmp si, IPV(IP_SW)
    jb .inr
    mov si, IPV(IP_SW)
    dec si
.inr:
    test byte IPV(IP_SMODE+1), 1
    jz .noflip
    mov di, IPV(IP_SW)
    dec di
    sub di, si
    mov si, di
.noflip:
    mov al, IPV(IP_SMODE)
    cmp al, 1
    je .grey
    cmp al, 2
    je .rgb
    ; ---- 1 bit: AL = the bit ------------------------------------------------
    mov di, si
    mov cl, 3
    shr di, cl
    add di, IPV(IP_SOFF)
    mov al, [di]
    mov cx, si
    and cl, 7
    xor cl, 7
    shr al, cl
    and al, 1
    cmp byte IPV(IP_SMODE), 3
    je .stencil
    neg al                          ; 0 / 255
    jmp short .sampled
.grey:
    add si, IPV(IP_SOFF)
    mov al, [si]
    jmp short .sampled
.rgb:
    mov di, si
    add di, si
    add di, si
    add di, IPV(IP_SOFF)
    xor ah, ah
    mov al, [di]
    mov cl, 77
    mul cl
    mov si, ax
    xor ah, ah
    mov al, [di+1]
    mov cl, 150
    mul cl
    add si, ax
    xor ah, ah
    mov al, [di+2]
    mov cl, 29
    mul cl
    add si, ax
    mov ax, si
    mov al, ah                      ; (77r + 150g + 29b) >> 8
.sampled:
    cmp word IPV(IP_INV), 0
    je .noinv
    not al
.noinv:
    ; ---- dither -----------------------------------------------------------
    mov di, bx
    and di, 3
    add di, [cs:gi_dy4]
    mov cl, [cs:gi_bayer + di]      ; CL = the threshold
    xor ah, ah
    cmp word IPV(IP_DMODE), 1
    jne .d4
    mov si, ax                      ; v
    mov cl, 4
    shl ax, cl
    add ax, si                      ; v * 17
    mov di, bx
    and di, 3
    add di, [cs:gi_dy4]
    mov cl, [cs:gi_bayer + di]
    cmp ah, cl                      ; lit when (v*17 >> 8) > threshold
    ja .lit
    mov cl, bl
    and cl, 7
    mov al, 0x80
    shr al, cl
    not al
    mov di, bx
    mov cl, 3
    shr di, cl
    add di, [cs:gi_row]
    and [es:di], al
    jmp .next
.lit:
    mov cl, bl
    and cl, 7
    mov al, 0x80
    shr al, cl
    mov di, bx
    mov cl, 3
    shr di, cl
    add di, [cs:gi_row]
    or [es:di], al
    jmp .next
.d4:
    mov si, ax                      ; v
    mov al, cl
    xor ah, ah
    mov di, ax
    shl ax, 1
    shl ax, 1
    add ax, di                      ; m * 5
    add ax, si
    sub ax, 37
    jns .c0
    xor ax, ax
.c0:
    cmp ax, 255
    jbe .c1
    mov ax, 255
.c1:
    add ax, 42
    mov cl, 85
    div cl                          ; AL = (t + 42) / 85  (0..3)
    xor ah, ah
    mov di, ax
    mov al, [cs:gi_pal + di]
    jmp short .nibble
.stencil:
    ; AL = the bit; paint where it is 0 (Decode [0 1]) or 1 (Decode [1 0])
    cmp word IPV(IP_INV), 0
    je .stn
    xor al, 1
.stn:
    or al, al
    jnz .next                       ; nothing painted
    cmp word IPV(IP_DMODE), 1
    jne .st4
    mov cl, bl
    and cl, 7
    mov al, 0x80
    shr al, cl
    mov di, bx
    mov cl, 3
    shr di, cl
    add di, [cs:gi_row]
    test byte IPV(IP_COL), 1
    jz .stclr
    or [es:di], al
    jmp short .next
.stclr:
    not al
    and [es:di], al
    jmp short .next
.st4:
    mov al, IPV(IP_COL)
    and al, 15
.nibble:                            ; AL = the colour index
    mov di, bx
    shr di, 1
    add di, [cs:gi_row]
    test bl, 1
    jnz .low
    mov cl, 4
    shl al, cl
    mov ah, [es:di]
    and ah, 0x0F
    or al, ah
    mov [es:di], al
    jmp short .next
.low:
    mov ah, [es:di]
    and ah, 0xF0
    or al, ah
    mov [es:di], al
.next:
    add bp, IPV(IP_SLO)
    adc dx, IPV(IP_SHI)
    inc bx
    cmp bx, IPV(IP_X1)
    jg .end
    jmp .px
.end:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; ---- tables and the primitives' private state ------------------------------
gi_bayer: db 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
gi_pal:   db 0, 8, 7, 15
