; =============================================================================
; os8088 - apps/pagedesk/pdxi.asm                    PDXI.MOD (PAGEDESK-SPEC 20.22.11)
;
; The image-decoder occupant's door. apps/pagedesk/pdwrt.asm's shape (PAGEDESK-SPEC
; 3.1): an 8-byte header, the door at +8, DS = CS inside, the caller's DS restored,
; ES preserved, bss zeroed by the module on its first entry. Everything that does
; work is compiled C (pdxi.c + pdxidct.c and the bilevel agent's files) and the
; four inner loops of pdxidct.inc, reached through pdi_verb(verb, bx, cx, dx).
;
; Registers: in AL = verb, BX/CX/DX its arguments. Out AX = the answer, CF = 1
; when it is a failure (every PDE_* but PDE_OK and PDE_BUSY). BX, CX and DX are
; clobbered; BP, SI, DI, DS, ES, SS:SP and DF are as they arrived.
;
; IT CALLS PDENG THROUGH ONE FAR VECTOR: pdxb_init's eng_off/eng_seg, kept in
; _pdi_engvec by PDV_X_INIT; _pdi_engcall is the C-callable way through it.
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

; The arithmetic: the 32-bit group (block offsets), the far movers; not the
; matrices, the view transform or the PDF-number parser/printer.
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW
%define PDM_SKIP_PARSE

section .text
pdi_hdr:
    dw PDM_MAGIC_IMG                ; +0
    dw PDM_ABI                      ; +2
    dw PDI_IMAGE_SIZE               ; +4
    dw PDI_BSS_SIZE                 ; +6

%if ($ - $$) != PDM_ENTRY
  %error "pdxi.asm: the door is not at PDM_ENTRY - the header grew"
%endif

pdi_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds                          ; every label below is DS-relative now
    cld
    cmp word [pdi_ready], 0
    jne .go
    mov word [pdi_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, pdi_bss_beg
    mov cx, PDI_BSS_SIZE
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
    call _pdi_verb
    add sp, 8
    mov cx, [_pdi_cf]               ; CF = (pdi_cf != 0); neg leaves AX alone
    neg cx
    cmp ax, PDE_NEEDMEM            ; CX = 0 = PDMEM_SLOT here; DX = KiB wanted
    jne .nd
    mov dx, [_pdi_rdx]
.nd:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned pdi_engcall(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
; One call through PDENG's door. Returns AX and leaves its CF in pdi_engcf.
; PDENG's door sets DS = CS itself and gives the caller's DS back.
; -----------------------------------------------------------------------------
_pdi_engcall:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov al, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [_pdi_engvec]
    sbb cx, cx                      ; CX = -CF
    neg cx
    mov [_pdi_engcf], cx
    pop es
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; DATA AND BSS FIRST - pdi_bss_beg must be the first label in .bss
; =============================================================================
section .data
pdi_ready:  dw 0

section .bss
pdi_bss_beg:

section .text
%include "pdxi.gen.asm"            ; found through -I build/

; the thunks the C names (os88_file_write_seq and friends), gated
%include "pagedesk/pdkgate.inc"   ; D23: every kernel call on the UI stack (PAGEDESK-SPEC 20.15)
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

%include "pagedesk/pdmath.inc"
%ifndef PDXI_NODCT
%include "pagedesk/pdxidct.inc"
%endif
%ifdef JB_ASM
%include "pagedesk/pdxijb2.inc"      ; bilevel: _mq_bytein, _mq_bit, _gb_row0 (needs the C statics it names)
%endif

section .bss
pdi_bss_end:
section .data
align 2, db 0
pdi_image_end:
PDI_IMAGE_SIZE equ pdi_image_end
PDI_BSS_SIZE   equ pdi_bss_end - pdi_bss_beg
section .text
