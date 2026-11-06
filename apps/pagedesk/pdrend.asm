; =============================================================================
; os8088 - apps/pagedesk/pdrend.asm                         PDREND.MOD
;
; The rendering segment (PAGEDESK-SPEC 3.1, 10, 20.3): a module in the shape
; of apps/loom/lmpvmod.asm - an 8-byte header, the door at +8, DS = CS inside,
; the caller's DS and ES back on return - whose C is pdrend.c and whose
; primitives are pdrprim.inc.
;
; The door's contract (SPEC 3.1): AL = the verb, BX/CX/DX its arguments, the
; answer in AX, CF = 1 on failure with AX = a PDE_* code. PDE_BUSY is not a
; failure: it leaves CF clear (a job asks to be stepped again).
;
; cpu 8086, assembled with -w+error; the compiled C goes through
; tools/cc8086.py first, which refuses &local, movs/stos in C, long, and frames
; over 96 bytes. The segment rules are lmpvmod.asm's header, not repeated here.
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"

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

section .text
pdr_hdr:
    dw PDM_MAGIC_REND
    dw PDM_ABI
    dw PDR_IMAGE_SIZE
    dw PDR_BSS_SIZE
%if ($ - $$) != 8
  %error "pdrend.asm: the door is not at +8 - the header grew"
%endif

; -----------------------------------------------------------------------------
; the door, at +8. AL = verb, BX CX DX = arguments. AX = the answer.
; -----------------------------------------------------------------------------
pdr_entry:
    push bp
    push si
    push di
    push es
    push ds
    push cs
    pop ds
    cld
    cmp word [pdr_ready], 0
    jne .go
    mov word [pdr_ready], 1         ; in .data: a flag inside the cleared
    push ax                         ; region could not say it was cleared
    push cx
    push ds
    pop es
    mov di, pdr_bss_beg
    mov cx, PDR_BSS_SIZE
    xor al, al
    rep stosb
    pop cx
    pop ax
.go:
    xor ah, ah
    push dx                         ; cdecl, right to left:
    push cx                         ;   unsigned dx
    push bx                         ;   unsigned cx
    push ax                         ;   unsigned bx, unsigned verb
    call _pdr_verb
    add sp, 8
    or ax, ax
    jz .ok
    cmp ax, PDE_BUSY
    je .ok
    stc
    jmp short .out
.ok:
    clc
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; DATA AND BSS - BEFORE THE COMPILED C (lmpvmod.asm: the order is load-bearing:
; pdr_bss_beg must be the first label in .bss).
; -----------------------------------------------------------------------------
section .data
pdr_ready:  dw 0

section .bss
pdr_bss_beg:
_pdr_eng_off:   resw 1              ; the far vector into PDENG: offset, segment
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

; =============================================================================
; THE COMPILED C
; =============================================================================
%include "pdrend.gen.asm"           ; found through -I build/

; the API bridge, gated: only the thunks the C names (os88_gfx_lock/unlock,
; os88_wm_clip_set) are assembled - lmpvmod.asm's note on CC_THUNK_GATED
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

; the arithmetic and far-memory movers, shared as SOURCE with the other
; modules (PAGEDESK-SPEC 2.4). Until pdmath.inc lands a build-time stub with
; the same labels stands in for it (the Makefile writes it) so the size and
; the syntax can be checked; it is never shipped.
%ifdef PD_MATH_STUB
%include "pdr_mathstub.inc"
%else
%include "pagedesk/pdmath.inc"
%endif

%include "pagedesk/pdrprim.inc"

section .bss
pdr_bss_end:
section .data
align 2, db 0
pdr_image_end:
PDR_IMAGE_SIZE equ pdr_image_end
PDR_BSS_SIZE   equ pdr_bss_end - pdr_bss_beg
section .text
