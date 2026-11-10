; =============================================================================
; os8088 - apps/pagedesk/pdxg.asm                    PDXG.MOD (PAGEDESK-SPEC 20.22.9)
;
; The graphics module's door (PDXG). apps/loom/lmpvmod.asm's shape (PAGEDESK-SPEC 3.1):
; an 8-byte header, the door at +8, DS = CS inside, the caller's DS restored,
; ES preserved, bss zeroed by the module on its first entry. Everything that
; does work is compiled C (pdxg.c + the pdxg*.c it #includes), reached through
; the one function pdx_verb(verb, bx, cx, dx).
;
; Registers: in AL = verb, BX/CX/DX = its arguments. Out AX = the answer, CF = 1
; when it is a failure (every PDE_* but PDE_OK and PDE_BUSY - a job that must be
; called again answers CF = 0, AX = PDE_BUSY). BX, CX and DX are clobbered;
; BP, SI, DI, DS, ES, SS:SP and DF are as they arrived.
;
; IT CALLS PDENG THROUGH ONE FAR VECTOR: pdxb_init's eng_off/eng_seg, kept in
; _pdx_engvec by PDV_WRT_INIT; _pdx_engcall is the C-callable way through it.
; The kernel is called directly (OSAPI_* are KERNEL_SEG:offset far immediates).
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

; The arithmetic: the 32-bit group, the 16.16 core, the PDF-number parser and the
; far movers; not the matrices or the view transform.
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW

section .text
pdx_hdr:
    dw PDM_MAGIC_GFX                ; +0
    dw PDM_ABI                      ; +2
    dw PDX_IMAGE_SIZE               ; +4
    dw PDX_BSS_SIZE                 ; +6

%if ($ - $$) != PDM_ENTRY
  %error "pdxg.asm: the door is not at PDM_ENTRY - the header grew"
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
    call _pdx_verb
    add sp, 8
    mov cx, [_pdx_cf]               ; CF = (pdx_cf != 0); neg leaves AX alone
    neg cx
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned pdx_engcall(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
; One call through PDENG's door. Returns AX and leaves its CF in pdx_engcf.
; PDENG's door sets DS = CS itself and gives the caller's DS back.
; -----------------------------------------------------------------------------
_pdx_engcall:
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
    sbb cx, cx                      ; CX = -CF
    neg cx
    mov [_pdx_engcf], cx
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
%include "pdxg.gen.asm"            ; found through -I build/

; the thunks the C names (os88_file_write_seq and friends), gated
%include "pagedesk/pdkgate.inc"   ; D23: every kernel call on the UI stack (PAGEDESK-SPEC 20.15)
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

%include "pagedesk/pdmath.inc"

section .bss
pdx_bss_end:
section .data
align 2, db 0
pdx_image_end:
PDX_IMAGE_SIZE equ pdx_image_end
PDX_BSS_SIZE   equ pdx_bss_end - pdx_bss_beg
section .text
