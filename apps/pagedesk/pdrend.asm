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
%include "pagedesk/pdabi.inc"       ; PDM_MAGIC_REND, PDM_ABI, PDE_* (generated)

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
    mov cx, [_pdr_out_cx]           ; PDE_NEEDMEM's region, PDE_NEEDMOD's and
    mov dx, [_pdr_out_dx]           ; NEEDMEM's DX (20.22.7); scratch otherwise
    or ax, ax
    jz .ok
    mov bx, ax                      ; PDE_BUSY, PDE_NEEDMOD and PDE_NEEDMEM
    sub bx, PDE_BUSY                ; (65..67) are not failures: CF = 0
    cmp bx, PDE_NEEDMEM - PDE_BUSY + 1
    jb .ok
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
pdr_bss_beg:                        ; FIRST label in .bss: everything from here to
                                    ; pdr_bss_end is zeroed by the door; the
                                    ; primitives' own variables (pdrprim.inc)
                                    ; come after the compiled C's

section .text

; =============================================================================
; THE COMPILED C
; =============================================================================
%include "pdrend.gen.asm"           ; found through -I build/

; the API bridge, gated: only the thunks the C names (os88_gfx_lock/unlock,
; os88_wm_clip_set) are assembled - lmpvmod.asm's note on CC_THUNK_GATED
%include "pagedesk/pdkgate.inc"   ; D23: every kernel call on the UI stack (PAGEDESK-SPEC 20.15)
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

; the arithmetic and far-memory movers, shared as SOURCE with the other
; modules (PAGEDESK-SPEC 2.4). Until pdmath.inc lands a build-time stub with
; the same labels stands in for it (the Makefile writes it) so the size and
; the syntax can be checked; it is never shipped.
%ifdef PD_MATH_STUB
%include "pdr_mathstub.inc"
%else
; this segment never parses or prints a number and never checksums (PDENG's and PDWRT's work)
%define PDM_SKIP_PARSE
%define PDM_SKIP_CRC
; nor uses these (20.22.7's room): the base matrix is its own (pdrv.c view_base),
; it maps no device point back to the page, writes no pt16 and no far word
%define PDM_SKIP_U32X
%define PDM_SKIP_TOPT16
%define PDM_SKIP_VIEWX
%define PDM_SKIP_POKEW
%include "pagedesk/pdmath.inc"
%endif

%ifdef PDR_NO_READSEQ
; SCAFFOLDING, never shipped: until eng's os88_file_read_seq thunk (DECISIONS D1)
; is merged into apps/cc/os88thunk.asm the Makefile defines PDR_NO_READSEQ and this
; stands in for it, answering 0 bytes (every strike read refuses, so a module built
; so draws no text). The real thunk replaces it with no source change here.
_os88_file_read_seq:
    xor ax, ax
    ret
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
