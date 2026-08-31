; =============================================================================
; os8088 - apps/weave/wblob.inc     reading the bundle claim
;
; Part of WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). %included by
; apps/weave/weave.asm after the compiled C. apps/c64/c64mem.inc is the shape
; and its header carries the doctrine this file obeys.
;
; WHY THESE EXIST. A `.WAB` is read whole into ONE pinned heap claim
; (WEAVE-SPEC 2.1) - up to 62KB, so it can never live in the package's own
; 60KB segment - and every byte of it is hostile (SPEC.md 19). The validator
; (wval.c) walks the header, the section table, the atom pool, the display
; list and every property block; the flow walk (wflow.c) walks the display
; list again at open and at every resize. The SDK's os88_peek() is ~11 us of
; near call per byte, which os88.h itself prices as "fine for a header, wrong
; for a copy" - and an atom pool is a copy.
;
; THE RULE IS WIDER THAN THE ROUTINES (C64-SPEC 3.6's rule, borrowed): data in
; the claim is passed as an explicit (segment, offset) PAIR, never as a C
; pointer. A C pointer here is a package-DS offset and nothing else, so
; "a pointer to the atom pool" is a defect that assembles cleanly and reads
; this package's own image instead.
;
; Every routine below that loads DS or ES loads it on purpose, restores it,
; leaves DF clear, and carries the `cc8086:allow` marker with its reason - the
; convention the gate would ask for if it read this file (it reads the
; compiled C only, SPEC.md 73.6).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. A routine answering in AL clears AH itself - without that the C `int`
; carries a stale high byte, which is a plausible value and never the right
; one (apps/cword/cwtype.inc's rule 2).
;
; SS != DS, so [bp+n] addresses the STACK segment and is right for arguments
; and wrong for anything of ours; every reference to our own data below is
; DS-relative by construction (a bare label), which is what CLAUDE.md's hard
; rule asks for.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; unsigned w_b(unsigned seg, unsigned off) - one byte of the claim, unsigned.
;
; UNSIGNED is the whole point of the return type. `char` is signed in this
; toolchain (docs/C-TOOLCHAIN.md), so a bundle byte of 0x80 read into a char
; is -128, and every range test written against it silently inverts. Every
; caller in wval.c and wflow.c reads bundle bytes through here.
; -----------------------------------------------------------------------------
_w_b:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]                  ; the claim
    mov bx, [bp+6]
    mov al, [es:bx]
    xor ah, ah                      ; a shim answering in AL must clear AH
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void w_pb(unsigned seg, unsigned off, unsigned v) - one byte INTO a claim
; void w_pw(unsigned seg, unsigned off, unsigned v) - ...and one word
;
; The bundle claim is read-only at run time (WEAVE-SPEC 2.1) and these are not
; for it: the GRID claim (5.6) is written every commit, every setCell and
; every recalculation, and it is a claim for the same reason the bundle is -
; a 26KB cell store can never live in the package's own segment.
; -----------------------------------------------------------------------------
_w_pb:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov bx, [bp+6]
    mov ax, [bp+8]
    mov [es:bx], al
    pop es
    pop bp
    ret

_w_pw:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov bx, [bp+6]
    mov ax, [bp+8]
    mov [es:bx], ax
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void w_pcopy(char *src, unsigned seg, unsigned off, unsigned n)
;
; ...and a run of OUR bytes into a claim: a label the user typed, or the RPN
; 6.9.2 just compiled. The mirror of w_copy, which brings bytes the other way.
; -----------------------------------------------------------------------------
_w_pcopy:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov si, [bp+4]
    mov es, [bp+6]                  ; cc8086:allow ES = the claim for one copy
    mov di, [bp+8]                  ; out of our own segment; restored below
    mov cx, [bp+10]
    jcxz .out
    cld
.l:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .l
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned w_w(unsigned seg, unsigned off) - the little-endian word at off.
;
; The claim is <= 62KB and every internal offset is a 16-bit word within one
; segment (WEAVE-SPEC 2.1), so `off` cannot wrap and no ES:BX+1 carry case
; exists - which is exactly why the format has that cap.
; -----------------------------------------------------------------------------
_w_w:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov bx, [bp+6]
    mov ax, [es:bx]
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void w_copy(unsigned seg, unsigned off, char *dst, unsigned n)
;
; n bytes out of the claim into a buffer in OUR segment. The caller has
; already bounds-checked (seg:off .. off+n) against the section it came from -
; this routine trusts its arguments, in the way every mover in this tree does,
; and the check belongs at the one place that knows the bound.
;
; It is the only way to get bundle bytes into a C string: os88_font_run() and
; os88_strlen() take a DS pointer, and the bundle is not in DS.
; -----------------------------------------------------------------------------
_w_copy:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    mov ax, [bp+4]                  ; SS-relative, so it is read before DS
    mov si, [bp+6]                  ; moves - and BP/SS are untouched below
    mov di, [bp+8]
    mov cx, [bp+10]
    push ds
    pop es                          ; ES = ours: the DESTINATION
    mov ds, ax                      ; DS = the claim: the SOURCE
    cld
    rep movsb                       ; cc8086:allow both segments loaded here on
                                    ; purpose and both restored below; DF is
                                    ; cleared above and left clear
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

; -----------------------------------------------------------------------------
; int w_print(unsigned seg, unsigned off, unsigned n) - are all n bytes ASCII
; 0x20..0x7E?  1 yes, 0 no.  n = 0 answers 1.
;
; WEAVE-SPEC 2.7 and 3.1: everything in a bundle is folded to the cell font's
; 95 glyphs at pack time, so a byte outside that range is a malformed bundle
; (10.4, field `atom pool`) rather than something to fold at run time -
; WEAVE-SPEC 9.4 is explicit that the runtime parses no text.
;
; In assembly because it is a per-byte loop over the whole atom pool: on the
; target machine a 600-byte pool through os88_peek() is ~7 ms of near calls
; before the first pixel, and this is a few hundred microseconds.
; -----------------------------------------------------------------------------
_w_print:
    push bp
    mov bp, sp
    push ds
    push si
    mov ax, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    mov ds, ax                      ; cc8086:allow DS is the claim for the loop
                                    ; below and is restored at the end; no
                                    ; string instruction and no ES
    jcxz .yes
.next:
    mov al, [si]
    inc si
    cmp al, 0x20
    jb .no
    cmp al, 0x7E
    ja .no
    loop .next
.yes:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    pop ds
    pop bp
    ret

; -----------------------------------------------------------------------------
; int w_zero(unsigned seg, unsigned off, unsigned n) - are all n bytes 0x00?
; 1 yes, 0 no.  n = 0 answers 1.
;
; WEAVE-SPEC 2.1: "Padding bytes between sections are 0x00", and 2.3 leaves no
; tail padding at all. t_wab.py checks both and the runtime must be at least as
; strict as t_wab (WEAVE-SPEC 12.2), so a byte the format does not account for
; refuses the bundle (10.4, field `section table`) rather than being ignored.
; -----------------------------------------------------------------------------
_w_zero:
    push bp
    mov bp, sp
    push ds
    push si
    mov ax, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    mov ds, ax                      ; cc8086:allow as above
    jcxz .yes
.next:
    cmp byte [si], 0
    jne .no
    inc si
    loop .next
.yes:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    pop ds
    pop bp
    ret
