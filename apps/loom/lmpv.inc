; =============================================================================
; os8088 - apps/loom/lmpv.inc          THE SEAM TO THE PREVIEW MODULE
;                                      (WEAVE-SPEC 1.2.4) - part 0 of LOOM.O88
;
; %included by apps/loom/loom.asm - so this is the PACKAGE, and everything it
; reaches is in a segment of its own. Two small routines and one dword, and
; that is deliberately the whole of the resident cost: the module is entered
; by a far call through the dword, and it has no state LOOM reads back.
;
; It is apps/weave/wcv.inc with the canvas half removed. That file is the
; worked example and its header is the argument; this one says only what
; differs.
;
; cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; BP, DS, ES, SS:SP
; and DF preserved. The module preserves the same set, plus SI and DI.
; =============================================================================

; THE MODULE IS PART 0 OF LOOM.O88 (WEAVE-SPEC 1.2.4), and that took three of
; the five routines that used to stand here: lpv_kb (the claim is op_fetch's,
; sized from the row's `len`, which the Makefile makes the image PLUS the bss
; - see build/lmpvpart.bin), lpv_bytes and lpv_stamp. The four-word stamp
; caught a stale PAIR of files; a part is bytes of the package's own file,
; located by a table in the image it was built with, so there is no pair. The
; module's header is unchanged, and its bss word is what the Makefile pads the
; part with.

section .text

; -----------------------------------------------------------------------------
; void lpv_bindmod(unsigned seg)
;
; Stamp the module's claim into the vector, or 0 to forget it. The OFFSET is
; WPV_ENTRY, an assemble-time constant out of the one file both assemblies
; share - so the two halves cannot disagree about where the door is.
; -----------------------------------------------------------------------------
_lpv_bindmod:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    or ax, ax                       ; 0 = forget it, and stays 0: the callers'
    jz .set                         ; test for "no module"
    mov dx, ax                      ; THE CODE SELECTOR, not the claim's data
    call OSAPI_CPU_INFO             ; segment - the same word off kern_mega,
    OS88_CODESEL dx, ah             ; and a fault per far call through it on
    mov ax, dx                      ; it (os88api.inc, KERN-MEGA-PLAN M4)
.set:
    mov [lpv_vec + 2], ax
    mov word [lpv_vec], WPV_ENTRY
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned lpv_call(unsigned verb, unsigned a, unsigned b, unsigned c)
;
; One far call into the preview module (WEAVE-SPEC 1.2.4). With no module bound the
; answer is 0, which is the same "it did not happen" a refused overlay gives
; (apps/cc/crt0.asm) - so every caller can ask "did it run" separately from
; "what did it say", which for WPVV_PAINT is the difference between a pane
; that has no module and a pane whose bundle would not read.
; -----------------------------------------------------------------------------
_lpv_call:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cmp word [lpv_vec + 2], 0
    je .none
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [lpv_vec]
    jmp short .out
.none:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

section .data
lpv_vec:    dw 0, 0                 ; (offset, segment) - the segment is 0
                                    ; until lm_pv_need has fetched part 0
section .text
