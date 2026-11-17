; =============================================================================
; os8088 - apps/pagedesk/pdx8.asm                    PDX8.MOD (PAGEDESK-SPEC 20.22.10.11)
;
; The raster compositor's door. A part (magic 'P8') that plat loads into the
; PDMEM_X8 region at its base, so the header below is the first 16 bytes of
; the region and the strip's planes begin at seg:PDX8_RES. apps/pagedesk/
; pdwrt.asm's shape (SPEC 3.1): the door at +8, DS = CS inside, the caller's DS
; and ES back on return; everything that does work is compiled C (pdx8.c),
; reached through pdx8_verb(verb, bx, cx, dx).
;
; Registers: in AL = verb, BX/CX/DX = its arguments. Out AX = the answer, CX and
; DX = the answer's extra words, CF = 1 on failure (AX = a PDE_* code). BP, SI,
; DI, DS, ES, SS:SP and DF are as they arrived.
;
; THE HEADER (16 bytes, 20.22.10.11):
;   +0 magic 'P8'   +2 PDM_ABI   +4 image   +6 bss   +8 the door (jmp short)
;   +10 x8_cseg     the region's code selector, written by plat (kern_mega)
;   +12 eng_off  +14 eng_seg     PDENG's door, written by plat at load
; THE ONLY EDGE OUT is that PDENG vector. No kernel call, no PDREND call.
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

%define PDM_SKIP_U32
%define PDM_SKIP_PARSE
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW
%define PDM_SKIP_CRC

section .text
x8_hdr:
    dw PDM_MAGIC_X8                 ; +0
    dw PDM_ABI                      ; +2
    dw PDX8_IMAGE_SIZE              ; +4
    dw PDX8_BSS_SIZE                ; +6
%if ($ - $$) != PDM_ENTRY
  %error "pdx8.asm: the door is not at PDM_ENTRY - the header grew"
%endif
    jmp short x8_entry              ; +8
    dw 0                            ; +10 x8_cseg (plat)
x8_engvec:
    dw 0, 0                         ; +12 eng_off, +14 eng_seg (plat)
%if ($ - $$) != 16
  %error "pdx8.asm: the header is not 16 bytes"
%endif

x8_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds                          ; every label below is DS-relative now
    cld
    cmp word [x8_ready], 0
    jne .go
    mov word [x8_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, x8_bss_beg
    mov cx, PDX8_BSS_SIZE
    xor al, al
    rep stosb
    pop di
    pop cx
    pop ax
.go:
    xor ah, ah
    push dx                         ; cdecl, right to left (SPEC.md 73.3)
    push cx
    push bx
    push ax
    call _pdx8_verb
    add sp, 8
    mov cx, [_x8_ocx]
    mov dx, [_x8_odx]
    mov bx, [_x8_cf]
    neg bx                          ; CF = (x8_cf != 0); neg leaves AX alone
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned x8_eng(verb, bx, cx, dx): one call through PDENG's door. Returns 0 and
; leaves AX in x8_eng_ax, or the PDE_ code when CF was set. PDENG's door sets
; DS = CS itself and gives the caller's DS back.
; -----------------------------------------------------------------------------
_x8_eng:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, 53                      ; PDE_NOTFOUND: no vector yet
    cmp word [x8_engvec+2], 0
    je .err
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [x8_engvec]
    jc .err
    mov [_x8_eng_ax], ax
    xor ax, ax
.err:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int x8_muldiv(a, b, c): trunc(a*b/c), a 32-bit intermediate, SATURATING
; (pdrprim.inc's pdr_muldiv, which this part cannot link)
; -----------------------------------------------------------------------------
_x8_muldiv:
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

; =============================================================================
; DATA AND BSS FIRST - x8_bss_beg must be the first label in .bss
; =============================================================================
section .data
x8_ready:   dw 0

section .bss
x8_bss_beg:
_x8_eng_ax: resw 1

section .text
%include "pagedesk/pdrx8.inc"       ; pdr_x8xlat, pdr_x8quant (and pdr_xq in .bss)
%include "pdx8.gen.asm"             ; found through -I build/

%include "pagedesk/pdmath.inc"

section .bss
x8_bss_end:
section .data
align 2, db 0
x8_image_end:
PDX8_IMAGE_SIZE equ x8_image_end
PDX8_BSS_SIZE   equ x8_bss_end - x8_bss_beg
section .text
