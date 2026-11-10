; =============================================================================
; os8088 - apps/pagedesk/pdxj.asm                    PDXJ.MOD (PAGEDESK-SPEC 20.22.13)
;
; The JPEG 2000 decoder's door: pdwrt.asm's shape (PAGEDESK-SPEC 3.1). An 8-byte
; header, the door at +8, DS = CS inside, the caller's DS restored, ES preserved,
; bss zeroed by the module on its first entry. The work is compiled C (pdxj.c and
; the pdxj*.c it #includes), reached through the one function pdxj_verb(verb,
; bx, cx, dx); the inner loops are pdxjasm.inc.
;
; Registers: in AL = verb, BX/CX/DX = its arguments. Out AX = the answer, CF = 1
; when it is a failure. BX, CX and DX are clobbered; BP, SI, DI, DS, ES, SS:SP and
; DF are as they arrived.
;
; IT CALLS PDENG THROUGH ONE FAR VECTOR: pdxb_init's eng_off/eng_seg, kept in
; _pdxj_engvec by the C (PDV_X_INIT); _pdxj_engcall is the C-callable way through
; it. The module makes no kernel call.
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

; the arithmetic: the far movers and nothing else (no 32-bit group, no matrices,
; no view transform, no PDF numbers, no CRC)
%define PDM_SKIP_U32
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW
%define PDM_SKIP_PARSE
%define PDM_SKIP_CRC

section .text
pdxj_hdr:
    dw PDM_MAGIC_JPX                ; +0
    dw PDM_ABI                      ; +2
    dw PDXJ_IMAGE_SIZE              ; +4
    dw PDXJ_BSS_SIZE                ; +6

%if ($ - $$) != PDM_ENTRY
  %error "pdxj.asm: the door is not at PDM_ENTRY - the header grew"
%endif

pdxj_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds                          ; every label below is DS-relative now
    cld
    cmp word [pdxj_ready], 0
    jne .go
    mov word [pdxj_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, pdxj_bss_beg
    mov cx, PDXJ_BSS_SIZE
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
    call _pdxj_verb
    add sp, 8
    mov cx, [_pdxj_cf]              ; CF = (pdxj_cf != 0); neg leaves AX alone
    neg cx
    mov dx, [_pdxj_rdx]             ; NEEDMEM: KiB wanted (CX = PDMEM_SLOT = 0)
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned pdxj_engcall(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
; One call through PDENG's door. Returns AX and leaves its CF in pdxj_engcf.
; PDENG's door sets DS = CS itself and gives the caller's DS back.
; -----------------------------------------------------------------------------
_pdxj_engcall:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov al, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [_pdxj_engvec]
    sbb cx, cx                      ; CX = -CF
    neg cx
    mov [_pdxj_engcf], cx
    pop es
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; DATA AND BSS FIRST - pdxj_bss_beg must be the first label in .bss
; =============================================================================
section .data
pdxj_ready:  dw 0

section .bss
pdxj_bss_beg:

section .text
%include "pdxj.gen.asm"             ; found through -I build/
%include "pagedesk/pdxjasm.inc"     ; the inner loops (found through -I apps/)

; the far movers the C names, gated (the module makes no kernel call, so the gate
; carries none; the thunk file is still where the CC runtime helpers live)
%include "pagedesk/pdkgate.inc"
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

%include "pagedesk/pdmath.inc"

section .bss
pdxj_bss_end:
section .data
align 2, db 0
pdxj_image_end:
PDXJ_IMAGE_SIZE equ pdxj_image_end
PDXJ_BSS_SIZE   equ pdxj_bss_end - pdxj_bss_beg
section .text
