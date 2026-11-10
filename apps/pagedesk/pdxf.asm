; =============================================================================
; os8088 - apps/pagedesk/pdxf.asm                    PDXF.MOD (PAGEDESK-SPEC 20.22.8)
;
; The font module's door, in pdwrt.asm's shape (PAGEDESK-SPEC 3.1): an 8-byte
; header, the door at +8, DS = CS inside, the caller's DS restored, ES preserved,
; bss zeroed on the first entry. Everything that does work is compiled C
; (pdxf.c and the pdxf*.c it #includes), reached through pdxf_verb(verb, bx, cx,
; dx).
;
; Registers: in AL = verb, BX/CX/DX = its arguments. Out AX = the answer, CF = 1
; when it is a failure; with AX = PDE_NEEDMEM, CF = 0, CX = 0 (PDMEM_SLOT) and
; DX = the KiB the workspace should have (20.22.2). BX, CX and DX are
; clobbered otherwise; BP, SI, DI, DS, ES, SS:SP and DF are as they arrived.
;
; IT CALLS PDENG THROUGH ONE FAR VECTOR (pdxb_init's eng_off/eng_seg, kept in
; _pdx_engvec by PDV_X_INIT); _pdx_eng is the C-callable way through it. The
; kernel is never called: the module draws nothing and reads only streams.
;
; THE INNER LOOPS are here too: pdx_muldiv (a*b/c with a 32-bit intermediate),
; and the edge accumulation and the 1-bit pack of the rasteriser (pdxfrs.inc).
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

; the arithmetic: the 32-bit group and the 16.16 core (CFF real numbers); not the
; matrices, the view transform or the PDF-number parser/printer.
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW
%define PDM_SKIP_PARSE

section .text
pdx_hdr:
    dw PDM_MAGIC_FNT                ; +0
    dw PDM_ABI                      ; +2
    dw PDX_IMAGE_SIZE               ; +4
    dw PDX_BSS_SIZE                 ; +6

%if ($ - $$) != PDM_ENTRY
  %error "pdxf.asm: the door is not at PDM_ENTRY - the header grew"
%endif

pdx_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds                          ; every label below is DS-relative now
    cld
    cmp word [pdx_ready], 0
    jne .go
    mov word [pdx_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, pdx_bss_beg
    mov cx, PDX_BSS_SIZE
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
    call _pdxf_verb
    add sp, 8
    mov dx, [_pdx_odx]              ; PDE_NEEDMEM's KiB (CF = 0 and CX = 0 = PDMEM_SLOT below)
    mov cx, [_pdx_cf]               ; CF = (pdx_cf != 0); neg leaves AX alone
    neg cx
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned pdx_eng(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
; One call through PDENG's door. Returns 0, or the PDE_ code when its CF was
; set; the AX of a success is left in pdx_eng_ax.
; -----------------------------------------------------------------------------
_pdx_eng:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov al, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [_pdx_engvec]
    jc .fail
    mov [_pdx_eng_ax], ax
    xor ax, ax
.fail:
    pop es
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; DATA AND BSS FIRST - pdx_bss_beg must be the first label in .bss
; =============================================================================
section .data
pdx_ready:  dw 0

section .bss
pdx_bss_beg:

section .text
%include "pdxf.gen.asm"             ; found through -I build/

%include "pagedesk/pdkgate.inc"     ; D23: every kernel call on the UI stack (PAGEDESK-SPEC 20.15)
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

%include "pagedesk/pdmath.inc"

%define PDXF_THR_ASM 128               ; = pdxfol.c's PDXF_THR
%include "pagedesk/pdxfrs.inc"       ; _pdx_muldiv, _pdx_accline, _pdx_pack

section .bss
pdx_bss_end:
section .data
align 2, db 0
pdx_image_end:
PDX_IMAGE_SIZE equ pdx_image_end
PDX_BSS_SIZE   equ pdx_bss_end - pdx_bss_beg
