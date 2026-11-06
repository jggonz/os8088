; =============================================================================
; os8088 - apps/pagedesk/pdwrt.asm                    PDWRT.MOD (PAGEDESK-SPEC 20.5)
;
; The writer module's door. apps/loom/lmpvmod.asm's shape (PAGEDESK-SPEC 3.1):
; an 8-byte header, the door at +8, DS = CS inside, the caller's DS restored,
; ES preserved, bss zeroed by the module on its first entry. Everything that
; does work is compiled C (pdwrt.c + the pdw*.c it #includes), reached through
; the one function pdw_verb(verb, bx, cx, dx).
;
; Registers: in AL = verb, BX/CX/DX = its arguments. Out AX = the answer, CF = 1
; when it is a failure (every PDE_* but PDE_OK and PDE_BUSY - a job that must be
; called again answers CF = 0, AX = PDE_BUSY). BX, CX and DX are clobbered;
; BP, SI, DI, DS, ES, SS:SP and DF are as they arrived.
;
; IT CALLS PDENG THROUGH ONE FAR VECTOR: pdxb_init's eng_off/eng_seg, kept in
; _pdw_engvec by PDV_WRT_INIT; _pdw_engcall is the C-callable way through it.
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

; The arithmetic: this module needs the 32-bit group, the 16.16 core (the line
; arrow's unit vector), the far movers and the CRC; not the matrices or the
; view transform.
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW

section .text
pdw_hdr:
    dw PDM_MAGIC_WRT                ; +0
    dw PDM_ABI                      ; +2
    dw PDW_IMAGE_SIZE               ; +4
    dw PDW_BSS_SIZE                 ; +6

%if ($ - $$) != PDM_ENTRY
  %error "pdwrt.asm: the door is not at PDM_ENTRY - the header grew"
%endif

pdw_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds                          ; every label below is DS-relative now
    cld
    cmp word [pdw_ready], 0
    jne .go
    mov word [pdw_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, pdw_bss_beg
    mov cx, PDW_BSS_SIZE
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
    call _pdw_verb
    add sp, 8
    mov cx, [_pdw_cf]               ; CF = (pdw_cf != 0); neg leaves AX alone
    neg cx
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned pdw_engcall(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
; One call through PDENG's door. Returns AX and leaves its CF in pdw_engcf.
; PDENG's door sets DS = CS itself and gives the caller's DS back.
; -----------------------------------------------------------------------------
_pdw_engcall:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov al, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [_pdw_engvec]
    sbb cx, cx                      ; CX = -CF
    neg cx
    mov [_pdw_engcf], cx
    pop es
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; DATA AND BSS FIRST - pdw_bss_beg must be the first label in .bss
; =============================================================================
section .data
pdw_ready:  dw 0

section .bss
pdw_bss_beg:

section .text
%include "pdwrt.gen.asm"            ; found through -I build/

; the thunks the C names (os88_file_write_seq and friends), gated
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

%include "pagedesk/pdmath.inc"

section .bss
pdw_bss_end:
section .data
align 2, db 0
pdw_image_end:
PDW_IMAGE_SIZE equ pdw_image_end
PDW_BSS_SIZE   equ pdw_bss_end - pdw_bss_beg
section .text
