; =============================================================================
; os8088 - apps/cword/cwmove.inc       the one piece of assembly in CWORD
;
; %included by apps/cword/cword.asm (the package) and by the host-driven boot
; test that exercises it, so that what is tested is the shipping routine and
; not a restatement of it. It defines one label and switches back to .text,
; as the section discipline in CLAUDE.md requires.
; =============================================================================

; -----------------------------------------------------------------------------
; cw_memmove - void cw_memmove(void *dst, const void *src, unsigned n)
;
; cdecl, the way the compiler calls it: arguments pushed right to left, the
; CALLER cleans the stack, AX is the return value and is not one here. The
; leading underscore is SmallerC's, which is why the C prototype says
; `cw_memmove` and this label says `_cw_memmove`.
;
; in:  [bp+4] = dst, [bp+6] = src, [bp+8] = n, all in DS (the package segment -
;      every object a C program can name is there)
; out: n bytes moved, correctly when the ranges overlap in either direction
; clobbers: nothing the ABI does not allow. DS, SS, BP, SI, DI and ES are all
;      restored; AX, BX, CX, DX are the caller's to lose under cdecl.
;
; The direction is chosen the way every memmove chooses it: copy ascending when
; the destination is BELOW the source and descending when it is above, so the
; bytes already written are never the bytes still to be read.
;
; ES IS LOADED AND PUT BACK. It arrives as KERNEL_SEG (SPEC.md 20.4) and the
; kernel's callbacks expect it back that way; `push es` / `pop es` around the
; body is the whole of the contract, and it is why this is eleven lines of
; assembly rather than a rule about when it is safe to call.
;
; DF IS CLEARED ON THE WAY OUT, unconditionally. The descending path sets it,
; and every other piece of code in this system - the kernel's ISRs included -
; assumes DF=0 (CLAUDE.md's register discipline). Leaving it set is the kind of
; defect that shows up somewhere else entirely.
; -----------------------------------------------------------------------------
section .text

_cw_memmove:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push cx

    mov di, [bp+4]                  ; dst
    mov si, [bp+6]                  ; src
    mov cx, [bp+8]                  ; n
    jcxz .done                      ; 0 bytes: rep movsb with CX=0 is a no-op
                                    ; anyway, but the descending path below
                                    ; would compute a bogus tail address first

    push ds
    pop es                          ; ES = DS: both ends are ours

    cmp di, si
    jbe .up                         ; dst <= src: ascending is safe (and it is
                                    ; the ONLY safe direction when they overlap
                                    ; that way)

    ; descending: start at the last byte of each range
    add si, cx
    dec si
    add di, cx
    dec di
    std
    rep movsb
    cld                             ; DF=0 is this machine's standing state
    jmp .done

.up:
    cld
    rep movsb

.done:
    pop cx
    pop es
    pop di
    pop si
    pop bp
    ret                             ; cdecl: the CALLER pops the arguments

