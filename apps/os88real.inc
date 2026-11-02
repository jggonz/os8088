; =============================================================================
; os8088 - apps/os88real.inc - a package's half of FSXF_REAL (SPEC.md 53.2.3)
;
; Inside a REAL-MODE bracket every segment a package uses must be a real one.
; Its own CS and DS are (the kernel runs the entry proc at a real paragraph),
; but a word holding a CLAIM's base holds whatever the kernel handed out: on
; kern_mega in protected mode that is an LDT selector, which real mode reads
; as a paragraph number of no meaning. The kernel renames nothing of the
; package's (MEGA-VIDEO-REALMODE-PLAN 7.4: a relocation proc does a move's
; arithmetic, and a selector-to-paragraph rename is not a move), so the
; package does it, at its own door, with OSAPI_SEG's SEG_REAL. Two routines,
; one LIST and one BANK of the package's:
;
;   OS88REAL_LIST name, word, word, ...   the words holding segments, by
;                                         address in your image
;   os88real_prep  SI = the list, DI = its bank. At the DOOR, before
;                  OSAPI_FSX_RUN: SEG_REAL of every listed word into the
;                  bank, the words themselves untouched. CF = 1: one has no
;                  real-mode segment (a claim above 1MB) - SI then points
;                  at its entry, and at the list's end on CF = 0 - and the
;                  package does not ask for FSXF_REAL: that is the
;                  package's own fallback (53.2.3). Every other register
;                  kept
;   os88real_swap  SI = the list, DI = the bank. FIRST and LAST inside the
;                  entry proc: the listed words exchanged with the bank -
;                  only when the bracket IS a real-mode one (OSAPI_CPU_INFO
;                  without CPU_F_PM), so a bracket the kernel ran in
;                  protected mode instead keeps its selectors. Called twice,
;                  it puts every word back before the entry returns - which
;                  must happen, because the copy the entry ran in is copied
;                  back over the window's. A real-mode kernel's SEG_REAL is
;                  the value itself, so there the exchange changes nothing.
;                  A word that is 0 is left as it is both ways: 0 at the
;                  door is no claim, and 0 at the second call is a claim
;                  the bracket let go of, which stays let go of. A word the
;                  bracket changed to ANOTHER segment is the package's to
;                  rename. Every register kept
;
; Every listed claim must be PINNED (OSAPI_MEM_MOVABLE AX = 0) from before
; os88real_prep until OSAPI_FSX_RUN returns, as the Video Player's vp_smov
; pins its own: the door's conventional claims can compact the heap, and a
; move on kern_mega rebases the selector and nothing else - the banked
; paragraph would name memory the claim has left (53.2.3).
;
; A hook (FSXF_RATE) that reads a listed word must be a no-op until the
; first swap and again from before the second - the "ready" byte 53.2.2
; already asks for, set after the first and cleared before the second.
;
; Include it once, anywhere in the image. It costs ~60 bytes.
; =============================================================================

%ifndef OS88REAL_INC
%define OS88REAL_INC

; OS88REAL_LIST name, w1, w2, ... - the list (NUL-ended) and its bank, both
; in the image, so the bank travels into the bracket's copy with the words.
; `2-*` and NOT `2+`: a greedy last parameter took "w1, w2" as ONE, so %0 was
; 2 whatever the list and the bank was one word long - the second word's
; bank was the first word ITSELF, and os88real_prep wrote a real segment
; into it at the door (MEGA-VIDEO-REALMODE-PLAN W4: tests/megarmb/megarmbs.asm,
; the first list of two)
%macro OS88REAL_LIST 2-*
%1:
 %rep %0 - 1
    dw %2
  %rotate 1
 %endrep
 %rotate 1                      ; (%0 rotations in all: %1 is the name again)
    dw 0
%{1}_bank:
    times %0 - 1 dw 0
%endmacro

os88real_prep:
    push ax
    push bx
    push dx
    push di
.l:
    mov bx, [si]
    or bx, bx
    jz .ok                      ; (CF = 0 from the or)
    mov dx, [bx]
    or dx, dx                   ; 0 - NO CLAIM in that word - banks 0: the
    jz .z                       ; null selector has no segment, and asking
    mov bl, SEG_REAL            ; refused the whole list
    call OSAPI_SEG              ; DX = the real segment with the same base
    jc .no                      ; ...or none: SI at its entry
.z:
    mov [di], dx
    inc si
    inc si
    inc di
    inc di
    jmp short .l
.ok:
.no:
    pop di
    pop dx
    pop bx
    pop ax
    ret

os88real_swap:
    push ax
    push bx
    push si
    push di
    call OSAPI_CPU_INFO         ; AH = the feature byte
    test ah, CPU_F_PM
    jnz .out                    ; a protected-mode bracket: selectors stay
.l:
    mov bx, [si]
    or bx, bx
    jz .out
    cmp word [bx], 0            ; A WORD THAT IS 0 STAYS 0, both ways: no
    je .n                       ; claim at the door, or one the bracket let
    mov ax, [di]                ; go of - and its bank not put back over it
    xchg ax, [bx]
    mov [di], ax
.n:
    inc si
    inc si
    inc di
    inc di
    jmp short .l
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

%endif
