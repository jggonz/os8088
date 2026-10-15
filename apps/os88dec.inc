; =============================================================================
; os8088 - apps/os88dec.inc
;
; NUMBER -> DECIMAL TEXT, written once (SPEC.md 20.5.2). Every package that
; shows a count, a size or a score had its own `div 10` loop - forty-odd of
; them, several packages carrying two or three (Telnet a 32-bit one with
; thousands separators AND a three-division one for ANSI replies; Sheet a
; signed one beside os88chart.inc's identical `ch_itoa_t`; Scribe one at DI
; and a second copy of it at BX). This is the 0-resident include; there is no
; kernel slot, because a slot costs every machine resident bytes for a loop
; of ~35.
;
; Capabilities are %defines before the %include, os88gfx.inc's lattice rule:
; an arm nobody asked for emits NOTHING.
;
;   %define DEC_U16     ; dec_u16: a word
;   %define DEC_S16     ; dec_s16: a signed word, '-' first (implies DEC_U16)
;   %define DEC_U32     ; dec_u32: DX:AX
;   %define DEC_SEP     ; a ',' every three digits, in BOTH of the above -
;                       ;    one convention a package, the way Telnet's status
;                       ;    line and its refusals read `16,384`
;   %define DEC_FLD     ; dec_fld: a right-aligned, space-padded FIELD
;   %include "os88dec.inc"
;
; Asking for nothing is an assembly error, not an empty include.
;
; --- THE API -----------------------------------------------------------------
;   dec_u16   AX unsigned -> DS:DI, NUL-ended; out DI -> the NUL, so a second
;             call or a string copy appends.
;   dec_s16   the same for a SIGNED AX: '-' and the magnitude.
;   dec_u32   DX:AX unsigned -> DS:DI, NUL-ended; out DI -> the NUL.
;   dec_fld   AX unsigned, CX = the field's width -> CX bytes at DS:DI, the
;             digits at the RIGHT and spaces in front, so one opaque font_run
;             of a fixed width repaints the whole cell and nothing is drawn
;             twice (SPEC.md 6.1). No NUL; out DI += CX. The caller sizes CX
;             to hold the number (five holds any word): a wider number loses
;             its HIGH digits rather than writing in front of DI.
; Every entry keeps every register but DI. DS = your own segment (never
; ES, which is the kernel's in a callback); DF is not used.
;
; The digit loop pushes a digit a turn and pops them back, so stkbalance.py
; is told so (SPEC.md 8.7.3); the deepest it goes is ten words, a 32-bit
; number's ten digits, and the whole of a call is under 40 bytes of stack.
; `tests/decfmt.py` runs every arm under Unicorn against Python's own
; formatting.
; =============================================================================
%ifndef OS88DEC_INC
%define OS88DEC_INC

%ifdef DEC_S16
  %ifndef DEC_U16
    %define DEC_U16
  %endif
%endif
%ifdef DEC_U16
  %define DEC_CORE
%elifdef DEC_U32
  %define DEC_CORE
%elifndef DEC_FLD
  %error "os88dec.inc: %define DEC_U16, DEC_S16, DEC_U32 or DEC_FLD first"
%endif
%ifdef DEC_SEP
  %ifndef DEC_CORE
    %error "os88dec.inc: DEC_SEP separates dec_u16 or dec_u32 - ask for one"
  %endif
%endif
; A package that wants SOME numbers separated and some not was measured and
; refused the switch: MIDIRack's plain one and its /1000-over-mru_d3 comma one
; are 68 bytes, a core that separates on request was 67, so the shape that
; pays is one convention per package.

%ifdef DEC_S16
dec_s16:
    or ax, ax
    jns dec_u16
    mov byte [di], '-'
    inc di
    neg ax                          ; 8000h negates to itself and prints as
    call dec_u16                    ; the 32768 it is, unsigned
    neg ax
    ret
%endif

%ifdef DEC_CORE
%ifdef DEC_U32
  %ifdef DEC_U16
dec_u16:
    push dx
    xor dx, dx
    call dec_u32
    pop dx
    ret
  %endif
dec_u32:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
%else
dec_u16:
%endif
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
%ifdef DEC_U32
    push si
%endif
    mov bx, 10
    xor cx, cx
.d:                                 ; (ONE push in the text: stkbalance.py
%ifdef DEC_U32                      ; reads both arms of an %ifdef)
    mov si, ax                      ; the high half first, so its remainder
    xchg ax, dx                     ; carries down into the low half
    xor dx, dx
    div bx
    xchg ax, si                     ; AX = the low word, SI = the high quotient
%else
    xor dx, dx
%endif
    div bx                          ; AX = the (low) quotient, DX = the digit
    push dx
    inc cx
%ifdef DEC_U32
    mov dx, si                      ; DX:AX = the quotient
    or si, ax
%else
    or ax, ax
%endif
    jnz .d
.e:
    pop ax
    add al, '0'
    mov [di], al
    inc di
%ifdef DEC_SEP
    dec cx
    jz .z
    mov ax, cx                      ; a ',' when what is left is a whole
    mov bl, 3                       ; number of threes (BX is done dividing)
    div bl
    or ah, ah
    jnz .e
    mov byte [di], ','
    inc di
    jmp short .e
.z:
%else
    loop .e
%endif
    mov byte [di], 0
%ifdef DEC_U32
    pop si
%endif
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif ; DEC_CORE

%ifdef DEC_FLD
dec_fld:
    push ax
    push bx
    push cx
    push dx
    add di, cx                      ; the units first, from the RIGHT, so the
    push di                         ; field pads itself
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    dec cx
    jz .x                           ; the field is full
    or ax, ax
    jnz .d
.p:
    dec di
    mov byte [di], ' '
    loop .p
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

%endif
