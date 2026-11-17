; =============================================================================
; os8088 - apps/pagedesk/pdeng.asm                       PDENG.MOD
;
; The PDF reader segment of PageDesk (PAGEDESK-SPEC 3, 6-8, 20.2): the door and
; the sections around apps/pagedesk/pdeng.c's compiled C. It is
; apps/loom/lmpvmod.asm's shape (PAGEDESK-SPEC 3.1): an 8-byte header, the door
; at +8, DS = CS inside, the caller's DS restored, ES preserved, bss zeroed by
; the module on first entry.
;
; The door's contract (PAGEDESK-SPEC 3.1):
;   in:  AL = the verb, BX / CX / DX = its arguments
;   out: AX = the answer (a count, a mark, or on failure the PDE_* code),
;        DX = 0, CF = 1 on failure. BX, CX, SI, DI, BP, DS, ES, SS:SP, DF as
;        they arrived. `PDE_BUSY` comes back with CF = 0 (it is not a
;        failure, DECISIONS D11) and means "call again" (OPEN's slices).
;
; THE SEGMENT RULES (apps/loom/lmpvmod.asm has the long form): SS is the
; caller's task stack, never ours, so no C `&local` (tools/cc8086.py refuses
; it); OSAPI_* are KERNEL_SEG:offset far immediates, so the module calls the
; kernel directly; `cpu 8086`, -w+error.
; =============================================================================

cpu 8086
bits 16

section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "pagedesk/pdabi.inc"

; =============================================================================
; THE HEADER - eight bytes, then the door at PDM_ENTRY
; =============================================================================
section .text
pde_hdr:
    dw PDM_MAGIC_ENG                ; +0  'PE'
    dw PDM_ABI                      ; +2
    dw PDE_IMAGE_SIZE               ; +4  our image size
    dw PDE_BSS_SIZE                 ; +6  ...and the bss past it, zeroed below

%if ($ - $$) != PDM_ENTRY
  %error "pdeng.asm: the door is not at PDM_ENTRY - the header grew"
%endif

; -----------------------------------------------------------------------------
; pde_entry - the door, at +8
; -----------------------------------------------------------------------------
pde_entry:
    push bp
    push si
    push di
    push es
    push ds
    push bx
    push cx
    push cs
    pop ds                          ; every label below is DS
    cld

    ; the bss, zeroed ONCE, by us (a module's bss is nobody else's job).
    ; pde_ready is in .data and not .bss: a flag in the region being cleared
    ; cannot say whether the region has been cleared.
    cmp word [pde_ready], 0
    jne .go
    mov word [pde_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, pde_bss_beg
    mov cx, PDE_BSS_SIZE
    xor al, al
    rep stosb
    pop di
    pop cx
    pop ax
.go:
    xor ah, ah                      ; AL is the verb
    push dx                         ; cdecl, right to left (SPEC.md 73.3):
    push cx                         ;   unsigned dx
    push bx                         ;   unsigned cx
    push ax                         ;   unsigned bx, unsigned verb
    call _pde_verb
    add sp, 8
    mov dx, [_pde_dx]
    cmp ax, 67                      ; PDE_NEEDMEM (PAGEDESK-SPEC 20.22.1): CX =
    jne .cx                         ;   the PDMEM_* region, in place of the
    mov bp, sp                      ;   caller's CX - BP is restored below,
    mov cx, [_pde_cx]               ;   and [bp] is the stack (SS)
    mov [bp], cx
.cx:
    cmp word [_pde_fail], 1         ; CF = (fail == 0) ...
    cmc                             ; ... CF = (fail != 0); pops keep the flags
    pop cx
    pop bx
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; =============================================================================
; DATA AND BSS - BEFORE THE COMPILED C, AND THE ORDER IS LOAD-BEARING
; (apps/loom/lmpvmod.asm: nasm coalesces a section in emission order, so
; pde_bss_beg must be the first label in .bss for the zeroing above to cover
; the C's own bss, which follows because the C is %included further down.)
; =============================================================================
section .data
pde_ready:  dw 0                    ; 0 until the bss has been zeroed

section .bss
pde_bss_beg:

section .text

; =============================================================================
; THE COMPILED C
; =============================================================================
%include "pdeng.gen.asm"            ; found through -I build/
%include "pagedesk/pdelex.inc"      ; the lexer's classes and word scan (perf)
%ifndef FL_NOASM
%include "pagedesk/pdeflate.inc"    ; FlateDecode's symbol loop (PAGEDESK-SPEC 20.13)
%endif
%define PDC_FARST                   ; the schedule, Td0 and the RC4 states are in the arena (pdecrypt.c, 20.22.5.1)
%include "pagedesk/pdcmd5.inc"      ; encryption (PAGEDESK-SPEC 20.22.5): the per-object key,
%include "pagedesk/pdcrc4.inc"      ;   RC4 and AES-CBC for streams and strings
%define PDC_NOENC                  ; PDENG only decrypts (the writer encrypts: PDWRT includes the same source)
%include "pagedesk/pdcaes.inc"

; =============================================================================
; THE API BRIDGE - AFTER THE C, GATED (SPEC.md 73.9.1): only the thunks the C
; names are assembled (tools/cc8086.py wrote a %define CC_U_<name> for each).
; =============================================================================
%include "pagedesk/pdkgate.inc"   ; D23: every kernel call on the UI stack (PAGEDESK-SPEC 20.15)
%define CC_THUNK_GATED
%include "cc/os88thunk.asm"

; =============================================================================
; THE ARITHMETIC AND THE FAR MOVERS (PAGEDESK-SPEC 4.2, 4.3, 20.7): the target
; bodies of everything in pdmath.h, shared as source. The CRC group stays: the
; fingerprint (6.2) needs it. Nothing here is the engine's own.
; =============================================================================
%define PDM_SKIP_MAT
%define PDM_SKIP_VIEW
%include "pagedesk/pdmath.inc"

; -----------------------------------------------------------------------------
; CLOSING THE IMAGE - lmpvmod.asm's: the `align 2` is taken BEFORE the image end
; so an odd-length .data cannot leave the last byte of .bss outside the range
; the zeroing above clears.
; -----------------------------------------------------------------------------
section .bss
pde_bss_end:
section .data
align 2, db 0
pde_image_end:
PDE_IMAGE_SIZE equ pde_image_end
PDE_BSS_SIZE   equ pde_bss_end - pde_bss_beg
section .text
