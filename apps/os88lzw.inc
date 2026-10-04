; =============================================================================
; os8088 - apps/os88lzw.inc
;
; LZW DECODE, GIF's flavour (SPEC.md 106.18), shared as source by every
; package that reads LZW: PiXEL's GIF part first (apps/pixel/pxgif.asm), and
; - PIXEL-PLAN.md decision 11 - Paint once its rows stay byte-identical on
; it (wave 9), TIFF's strips (wave 8) and the GIF encoder (wave 7). What two
; programs share they share as source, never as a copy (WEAVE-SPEC 1.2).
;
; It is Paint's pt_gdec (SPEC.md 42.21) with Paint taken out of it - no
; palette map, no canvas - and its two guarantees kept to the instruction:
;
;   - A CODE IS REFUSED unless it is at most the next free code (GIF's one
;     forward reference, a string closed by its own first character), and a
;     first code after a Clear unless it is a ROOT;
;   - AN ENTRY IS NEVER ADDED whose prefix is not below its own index,
;
; so a prefix chain strictly descends, cannot cycle, and is at most 4,092
; characters (the smallest Clear is 4, so entries are 6..4095, plus the
; root and the forward reference's one) into the 4,096-byte string stack.
; The walk is six unguarded instructions a character because of that, and
; no character of it is counted.
;
; CONFIGURATION, before the %include:
;   LZW_BSS     a DS-relative address of LZW_BSSSZ bytes: the state
;   LZW_FILL    a near routine: APPEND INPUT. in ES:DI = where, CX = room
;               (> 0); out CX = the bytes put there, 0 = the input has
;               ended. May clobber AX, BX, DX, SI, DI; keeps DS, ES, BP
;   LZW_RUN     a near routine: A DECODED STRING. in ES:SI = its bytes, in
;               order, CX = how many (>= 1). out CF = 1 stops the decode
;               with AX = the includer's answer. May clobber AX, BX, CX,
;               SI, DI; keeps DS, ES, BP and DX (the decode's code reader)
;   LZW_RUN1    (optional) a near routine: a ONE-character string, AL - a
;               root code's, the common case on a noisy picture: LZW_RUN's
;               contract without the string's round trip through the stack,
;               and it keeps SI too (the decode's next free code)
;   LZW_EBAD    (optional, default FFFFh) the answer for a code the table
;               never built
;   LZW_EARLY   (optional, default 0) 1 grows the code size one code early -
;               TIFF's rule, for the day the TIFF reader adds its MSB-first
;               code reader beside lzw_decode's (wave 8)
;
; THE TABLE SEGMENT, in ES for the whole decode: LZW_KB (18) KB, the
; includer's claim -
;   LZW_PREF   0       4,096 prefix words
;   LZW_SUFF   8192    4,096 suffix bytes
;   LZW_STK    16384   the top of the 4,096-byte string stack (12288..16383)
;   LZW_IN     16384   the INPUT buffer, LZW_INSZ bytes: the code bytes,
;                      read a whole code at a time, refilled through
;                      LZW_FILL when fewer than three remain
;
; lzw_decode - in AL = the minimum code size (2..8), ES = the table segment.
; out CF = 0, AX = LZW_EOI (the End code) or LZW_END (the input ended, or
; the next code needs bits past it); CF = 1, AX = LZW_EBAD or LZW_RUN's
; answer. DS and ES kept; every other register clobbered.
; =============================================================================

%ifndef OS88LZW_INC
%define OS88LZW_INC

%ifndef LZW_EBAD
  %define LZW_EBAD 0xFFFF
%endif
%ifndef LZW_EARLY
  %define LZW_EARLY 0
%endif

LZW_PREF    equ 0
LZW_SUFF    equ 8192
LZW_STK     equ 16384
LZW_IN      equ 16384
LZW_INSZ    equ 2048
LZW_KB      equ 18
LZW_EOI     equ 0
LZW_END     equ 1

lzw_clr     equ LZW_BSS + 0         ; word: 1 << min
lzw_eoi     equ LZW_BSS + 2         ; word: the End code
lzw_free    equ LZW_BSS + 4         ; word: the next code a string takes
lzw_old     equ LZW_BSS + 6         ; word: the last code, FFFFh after Clear
lzw_code    equ LZW_BSS + 8         ; word: the code being decoded
lzw_mask    equ LZW_BSS + 10        ; word: (1 << the code size) - 1
lzw_ib      equ LZW_BSS + 12        ; word: the input's current byte...
lzw_ie      equ LZW_BSS + 14        ; word: ...and its end, in LZW_IN
lzw_cs      equ LZW_BSS + 16        ; byte: the code size, 3..12
lzw_bit     equ LZW_BSS + 17        ; byte: the bit within [lzw_ib]
lzw_first   equ LZW_BSS + 18        ; byte: the last string's first char
lzw_min     equ LZW_BSS + 19        ; byte: the minimum code size
lzw_eof     equ LZW_BSS + 20        ; byte: LZW_FILL has said the end
lzw_lim     equ LZW_BSS + 22        ; word: 1 << the code size
lzw_ie2     equ LZW_BSS + 24        ; word: [lzw_ie] - 2, the fast reader's
                                    ; bound (three bytes from it)
LZW_BSSSZ   equ 26

; THE LOOP keeps the code reader in registers - BP the input byte, DL the
; bit in it, DH the code size - and SI the next free code, and reads a code
; as a word and, past bit 16, a third byte, shifted down (SPEC.md 106.22).
; LZW_RUN keeps BP and DX; LZW_RUN1 keeps SI too
lzw_decode:
    cmp al, 2
    jb .bad0
    cmp al, 8
    ja .bad0
    mov [lzw_min], al
    xor ax, ax
    mov [lzw_bit], al
    mov [lzw_eof], al
    mov word [lzw_ib], LZW_IN
    mov word [lzw_ie], LZW_IN
    mov word [lzw_ie2], LZW_IN - 2
    call lzw_reset
    mov bp, LZW_IN
    xor dl, dl
    mov dh, [lzw_cs]
    mov si, [lzw_free]
.next:
    cmp bp, [lzw_ie2]               ; --- the next code: AX ----------------
    jae .refill
.have:
    mov ax, [es:bp]
    mov cl, dl
    shr ax, cl
    add cl, dh                      ; CL = where the code ends
    cmp cl, 16
    ja .three
.got:
    and ax, [lzw_mask]
    mov dl, cl                      ; ...and on past it
    and dl, 7
    shr cl, 1
    shr cl, 1
    shr cl, 1
    xor ch, ch
    add bp, cx
    cmp ax, [lzw_clr]               ; --- what it is -----------------------
    jae .ctl
    mov [lzw_code], ax              ; a ROOT: one character, itself - the
    mov [lzw_first], al             ; common case first
%ifdef LZW_RUN1
    call LZW_RUN1
%else
    mov di, LZW_STK - 1
    mov [es:di], al
    mov si, di
    mov cx, 1
    call LZW_RUN
%endif
    jc .out
    cmp word [lzw_old], 0xFFFF
    jne .add
    mov ax, [lzw_code]              ; (the first after a Clear: no entry)
    mov [lzw_old], ax
    jmp .next
.add:
    cmp si, 4096                    ; --- and the code this one defines
    jae .noadd                      ; full: nothing until a Clear (deferred)
    mov ax, [lzw_old]
    cmp ax, si
    jae .noadd                      ; a prefix at or above its own index is
    mov di, si                      ; a chain that cycles: never added
    add di, di
    mov [es:di + LZW_PREF], ax
    mov al, [lzw_first]
    mov [es:si + LZW_SUFF], al
    inc si
    mov bx, si
%if LZW_EARLY
    inc bx
%endif
    cmp bx, [lzw_lim]               ; the table crossed a power of two
    jae .grow
.noadd:
    mov ax, [lzw_code]
    mov [lzw_old], ax
    jmp .next
.grow:
    cmp dh, 12
    jae .noadd
    inc dh
    mov [lzw_cs], dh
    shl word [lzw_lim], 1
    stc
    rcl word [lzw_mask], 1
    jmp short .noadd
.ctl:
    je .clear
    cmp ax, [lzw_eoi]
    je .eoi
    mov [lzw_code], ax              ; a table code: never the first after a
    cmp word [lzw_old], 0xFFFF      ; Clear (that must be a root)
    je .bad
    mov cx, [lzw_clr]
    mov di, LZW_STK - 1             ; the string is built downwards from the
    mov bx, ax                      ; top, so it reads upwards afterwards
    cmp ax, si
    ja .bad                         ; past the table: never built
    std                             ; (std touches neither CF nor ZF, so the
    jb .walkin                      ; compare above is still the live one)
    mov al, [lzw_first]             ; == the next free code: the forward
    stosb                           ; reference, closed by its own first
    mov bx, [lzw_old]               ; character
    cmp bx, cx
    jb .root
.walkin:
    mov al, [es:bx + LZW_SUFF]
    stosb
    add bx, bx
    mov bx, [es:bx + LZW_PREF]
    cmp bx, cx
    jae .walkin
.root:
    mov al, bl                      ; a root is its own character
    mov [lzw_first], al
    stosb
    cld
    push si
    mov si, di
    inc si                          ; ES:SI = the string, CX = its length
    mov cx, LZW_STK
    sub cx, si
    call LZW_RUN
    pop si
    jc .out
    jmp .add
.three:
    mov bl, [es:bp + 2]             ; the third byte's bits, at 16 - bit:
    mov ch, cl                      ; (byte << (8 - bit)) in AH
    mov cl, 8
    sub cl, dl
    shl bl, cl
    or ah, bl
    mov cl, ch
    jmp .got
.refill:
    mov [lzw_ib], bp                ; fewer than three bytes: the buffer
    call lzw_refill                 ; refilled - AX = the bytes there
    mov bp, [lzw_ib]
    mov bx, [lzw_ie]
    dec bx
    dec bx
    mov [lzw_ie2], bx
    mov cl, 3
    shl ax, cl                      ; ...as bits (at most LZW_INSZ x 8)
    mov bl, dl
    xor bh, bh
    sub ax, bx
    mov bl, dh
    cmp ax, bx
    jb .end                         ; not a whole code: the input has ended
    jmp .have
.clear:
    call lzw_reset
    mov dh, [lzw_cs]
    mov si, [lzw_free]
    jmp .next
.eoi:
    mov ax, LZW_EOI
    clc
    ret
.end:
    mov ax, LZW_END
    clc
    ret
.bad:
    cld
.bad0:
    mov ax, LZW_EBAD
    stc
.out:
    ret

; lzw_reset - the table back to its first state (a Clear). Preserves all
lzw_reset:
    push ax
    push cx
    mov cl, [lzw_min]
    mov ax, 1
    shl ax, cl
    mov [lzw_clr], ax               ; 1 << min
    inc ax
    mov [lzw_eoi], ax
    inc ax
    mov [lzw_free], ax              ; the first code a string can take
    inc cl
    mov [lzw_cs], cl
    mov ax, 1
    shl ax, cl
    mov [lzw_lim], ax
    dec ax
    mov [lzw_mask], ax
    mov word [lzw_old], 0xFFFF
    pop cx
    pop ax
    ret

; lzw_refill - the bytes not yet read go to the buffer's start, and LZW_FILL
; appends after them until three are there or the input ends. out AX = the
; bytes there. Preserves BX, CX, DX, SI, DI
lzw_refill:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    mov si, [lzw_ib]
    mov cx, [lzw_ie]
    sub cx, si                      ; the tail, at most two bytes
    mov di, LZW_IN
    push es
    pop ds
    cld
    rep movsb
    pop ds
    mov word [lzw_ib], LZW_IN
    mov [lzw_ie], di
.more:
    cmp byte [lzw_eof], 0
    jne .done
    mov ax, [lzw_ie]
    sub ax, LZW_IN
    cmp ax, 3
    jae .done
    mov di, [lzw_ie]
    mov cx, LZW_IN + LZW_INSZ
    sub cx, di
    call LZW_FILL                   ; CX = the bytes appended
    jcxz .eof
    add [lzw_ie], cx
    jmp short .more
.eof:
    mov byte [lzw_eof], 1
.done:
    mov ax, [lzw_ie]
    sub ax, [lzw_ib]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

%endif
