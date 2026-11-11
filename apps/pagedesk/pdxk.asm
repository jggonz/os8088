; =============================================================================
; os8088 - apps/pagedesk/pdxk.asm                    PDXK.MOD (PAGEDESK-SPEC 20.22.5)
;
; The key occupant's door, apps/pagedesk/pdwrt.asm's shape (PAGEDESK-SPEC 3.1):
; an 8-byte header, the door at +8, DS = CS inside, the caller's DS restored, ES
; preserved, bss zeroed by the module on its first entry. The work is compiled C
; (pdxk.c: algorithms 2, 4-7 and 2.A/2.B) over the crypto library's assembly
; (pdcmd5.inc, pdcrc4.inc, pdcaes.inc, pdcsha2.inc, PAGEDESK-SPEC 20.22.5).
;
; Registers: in AL = verb, BX/CX/DX = its arguments. Out AX = the answer, CF = 1
; when it is a failure (every PDE_* but PDE_OK and PDE_BUSY). BX, CX and DX are
; clobbered; BP, SI, DI, DS, ES, SS:SP and DF are as they arrived.
;
; IT CALLS PDENG THROUGH ONE FAR VECTOR: pdxb_init's eng_off/eng_seg, kept in
; _pdk_engvec by PDV_X_INIT; _pdk_engcall is the C-callable way through it.
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW
%define PDM_SKIP_PARSE
%define PDM_SKIP_U32
%define PDM_SKIP_CRC

section .text
pdk_hdr:
    dw PDM_MAGIC_KEY                ; +0  'PK'
    dw PDM_ABI                      ; +2
    dw PDK_IMAGE_SIZE               ; +4
    dw PDK_BSS_SIZE                 ; +6

%if ($ - $$) != PDM_ENTRY
  %error "pdxk.asm: the door is not at PDM_ENTRY - the header grew"
%endif

pdk_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds                          ; every label below is DS-relative now
    cld
    cmp word [pdk_ready], 0
    jne .go
    mov word [pdk_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, pdk_bss_beg
    mov cx, PDK_BSS_SIZE
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
    call _pdk_verb
    add sp, 8
    mov cx, [_pdk_cf]               ; CF = (pdk_cf != 0); neg leaves AX alone
    neg cx
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned pdk_engcall(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
; One call through PDENG's door. Returns AX and leaves its CF in pdk_engcf.
; -----------------------------------------------------------------------------
_pdk_engcall:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov al, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [_pdk_engvec]
    sbb cx, cx                      ; CX = -CF
    neg cx
    mov [_pdk_engcf], cx
    pop es
    pop di
    pop si
    pop bp
    ret

section .data
pdk_ready:  dw 0

section .bss
pdk_bss_beg:

section .text
%include "pdxk.gen.asm"             ; found through -I build/

%include "pagedesk/pdcmd5.inc"
%include "pagedesk/pdcrc4.inc"
%include "pagedesk/pdcaes.inc"
%include "pagedesk/pdcsha2.inc"

%include "pagedesk/pdkgate.inc"
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

%include "pagedesk/pdmath.inc"

section .bss
pdk_bss_end:
section .data
align 2, db 0
pdk_image_end:
PDK_IMAGE_SIZE equ pdk_image_end
PDK_BSS_SIZE   equ pdk_bss_end - pdk_bss_beg
section .text
