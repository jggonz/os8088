; =============================================================================
; os8088 - apps/loom/lmpv.inc          THE SEAM TO LOOM.WPV (WEAVE-SPEC 1.2.4)
;
; %included by apps/loom/loom.asm - so this is the PACKAGE, and everything it
; reaches is in a segment of its own. Five small routines and one dword, and
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

; THE MODULE'S SIZE AND ITS BSS, injected rather than assembled: LOOM.WPV is a
; separate nasm job, so this assembly cannot see its last label. The Makefile
; writes build/wpvsize.inc from the module it has just built, and the package
; is built after it. That ordering is what makes the third stamp word (1.2.4)
; a real staleness check rather than a tautology.
;
; THE BSS WORD IS THE ONE THING wcv.inc DOES NOT HAVE. WEAVE.WSM is hand
; written assembly whose state is initialised bytes inside its own image, so
; its claim is exactly the file; LOOM.WPV is compiled C with a .bss the file
; does not carry, so the claim is image + bss and the module zeroes the tail
; itself on its first entry (apps/loom/lmpvmod.asm). Claim the file's size
; alone and the walk's layout table lands outside the claim - which is a
; module that draws one correct picture and then writes over the heap.
%include "wpvsize.inc"

section .text

; -----------------------------------------------------------------------------
; int lpv_kb(void) - the KB the module's claim needs, ceil((size + bss)/1024).
; -----------------------------------------------------------------------------
_lpv_kb:
    mov ax, WPV_SIZE + WPV_BSS + 1023
    mov cl, 10
    shr ax, cl
    ret

; -----------------------------------------------------------------------------
; unsigned lpv_bytes(void) - WPV_SIZE, so the caller can check the read.
; -----------------------------------------------------------------------------
_lpv_bytes:
    mov ax, WPV_SIZE
    ret

; -----------------------------------------------------------------------------
; int lpv_stamp(unsigned seg) - is this really OUR module?
; out: 0 yes, 1 not a module or truncated, 2 stale.
;
; THREE WORDS, and 1.2.4 says what each catches and what the set does not.
; The check is here rather than in C for wcv.inc's reason: WPV_MAGIC, WPV_ABI
; and WPV_SIZE are nasm equs out of the two files this assembly shares with
; the module, and the C copies in apps/weave/wpvabi.h are guarded against
; these rather than trusted instead of them.
; -----------------------------------------------------------------------------
_lpv_stamp:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov ax, 1
    cmp word [es:WPV_H_MAGIC], WPV_MAGIC
    jne .out
    mov ax, 2
    cmp word [es:WPV_H_ABI], WPV_ABI    ; a CONTRACT change need not change a
    jne .out                            ; size, which is the half .OVL's two
    cmp word [es:WPV_H_SIZE], WPV_SIZE  ; size words cannot express
    jne .out
    cmp word [es:WPV_H_BSS], WPV_BSS
    jne .out
    xor ax, ax
.out:
    pop es
    pop bp
    ret

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
    mov [lpv_vec + 2], ax
    mov word [lpv_vec], WPV_ENTRY
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned lpv_call(unsigned verb, unsigned a, unsigned b, unsigned c)
;
; One far call into LOOM.WPV (WEAVE-SPEC 1.2.4). With no module bound the
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
                                    ; until the load path has read, stamped
                                    ; and believed the module
section .text
