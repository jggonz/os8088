; =============================================================================
; os8088 - apps/pagedesk/pdrx8.inc        PDREND: the 8-bit band's assembly
;
; %included by pdrend.asm after pdrprim.inc (PAGEDESK-SPEC 20.22.10). The C
; twin of each routine is in pdrx8.c under PD_HOST; same register contract as
; pdrprim.inc (cdecl, BP/DS/SS/SP preserved, the rest scratch, DF clear).
;
;   pdr_x8xlat(seg, off, n, lut)   p[i] = lut[p[i]] over n far bytes
; =============================================================================
section .text

_pdr_x8xlat:
    push bp
    mov bp, sp
    mov es, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    mov bx, [bp+10]                 ; the table, in DS
    jcxz .done
.l: mov al, [es:di]
    xlat
    stosb
    loop .l
.done:
    pop bp
    ret

; -----------------------------------------------------------------------------
; pdr_x8quant()   the parameter block _pdr_xq (pdrend.h struct pdr_xq)
;
; n pixels of 8-bit grey from sseg:soff into a band row, read-modify-write on the
; two ends: 4bpp the seven-level checker of PAGEDESK-SPEC 20.19.2 (thresholds
; 21 64 106 149 191 234, the two greys of each level in xq_a / xq_b, `par` flips
; every pixel), 1bpp the 4x4 Bayer (lit when v*17>>8 exceeds the cell).
; -----------------------------------------------------------------------------
XQ_SSEG equ 0
XQ_SOFF equ 2
XQ_DSEG equ 4
XQ_DOFF equ 6
XQ_X0   equ 8
XQ_N    equ 10
XQ_PAR  equ 12
XQ_BPP  equ 14

section .bss
_pdr_xq: resw 8
section .text

xq_cls:                                     ; grey -> level, thresholds 21 64 106 149 191 234
%assign xqv 0
%rep 256
 %if xqv < 21
  db 0
 %elif xqv < 64
  db 1
 %elif xqv < 106
  db 2
 %elif xqv < 149
  db 3
 %elif xqv < 191
  db 4
 %elif xqv < 234
  db 5
 %else
  db 6
 %endif
 %assign xqv xqv+1
%endrep
xq_a:   db 0, 0, 8, 8, 7, 7, 15            ; the colour of a level at parity 0
xq_b:   db 0, 8, 8, 7, 7, 15, 15           ; ... at parity 1
xq_bay: db 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5

_pdr_x8quant:
    push bp
    push ds
    mov cx, [_pdr_xq+XQ_N]
    jcxz .zero
    jmp short .go
.zero:
    pop ds
    pop bp
    ret
.go:
    mov bx, [_pdr_xq+XQ_X0]
    mov dx, [_pdr_xq+XQ_PAR]
    mov di, [_pdr_xq+XQ_DOFF]
    mov si, [_pdr_xq+XQ_SOFF]
    mov es, [_pdr_xq+XQ_DSEG]
    cmp word [_pdr_xq+XQ_BPP], 4
    mov ds, [_pdr_xq+XQ_SSEG]               ; DS = the strip; tables and parameters via CS
    jne .one
.p4:                                        ; ---- 4bpp: DL = the parity, BX = x
    lodsb
    xor ah, ah
    mov bp, ax
    mov al, [cs:bp+xq_cls]                  ; the level 0..6 of this grey (AH stays 0)
    mov bp, ax
    test dl, 1
    jnz .pb
    mov al, [cs:bp+xq_a]
    jmp short .pw
.pb:
    mov al, [cs:bp+xq_b]
.pw:
    xor dl, 1
    test bl, 1
    jnz .lo
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    and byte [es:di], 0x0F
    or [es:di], al
    jmp short .n4
.lo:
    and byte [es:di], 0xF0
    or [es:di], al
    inc di
.n4:
    inc bx
    dec cx
    jz .out
    jmp .p4
.one:                                       ; ---- 1bpp: DH = the bit mask
    mov cl, bl                              ; (CH is zero: n fits the loop below)
    and cl, 7
    mov dh, 0x80
    shr dh, cl
    mov cx, [cs:_pdr_xq+XQ_N]
.p1:
    lodsb
    mov ah, 17
    mul ah                                  ; AX = v * 17, AH = the level
    mov bp, bx
    and bp, 3
    add bp, [cs:_pdr_xq+XQ_PAR]
    cmp ah, [cs:bp+xq_bay]
    jbe .dark
    or [es:di], dh
    jmp short .n1
.dark:
    mov al, dh
    not al
    and [es:di], al
.n1:
    inc bx
    shr dh, 1
    jnc .c1
    mov dh, 0x80
    inc di
.c1:
    loop .p1
.out:
    pop ds
    pop bp
    ret
