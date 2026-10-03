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
;               with AX = the includer's answer. May clobber AX, BX, CX, DX,
;               SI, DI; keeps DS, ES, BP
;   LZW_EBAD    (optional, default FFFFh) the answer for a code the table
;               never built
;   LZW_EARLY   (optional, default 0) 1 grows the code size one code early -
;               TIFF's rule, for the day the TIFF reader adds its MSB-first
;               code reader beside lzw_get (wave 8)
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
LZW_BSSSZ   equ 22

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
    call lzw_reset
.next:
    call lzw_get                    ; AX = the code
    jc .end
    cmp ax, [lzw_clr]
    je .clear
    cmp ax, [lzw_eoi]
    je .eoi
    mov [lzw_code], ax
    cmp word [lzw_old], 0xFFFF
    jne .chain
    cmp ax, [lzw_clr]               ; the first code after a Clear is a ROOT
    jae .bad
    mov [lzw_old], ax
    mov [lzw_first], al
    mov di, LZW_STK - 1
    mov [es:di], al
    mov si, di
    mov cx, 1
    call LZW_RUN
    jc .out
    jmp short .next
.chain:
    mov dx, [lzw_clr]
    mov di, LZW_STK - 1             ; the string is built downwards from the
    mov bx, ax                      ; top, so it reads upwards afterwards
    cmp ax, [lzw_free]
    ja .bad                         ; past the table: never built
    std                             ; (std touches neither CF nor ZF, so the
    jb .walk                        ; compare above is still the live one)
    mov al, [lzw_first]             ; == the next free code: the forward
    stosb                           ; reference, closed by its own first
    mov bx, [lzw_old]               ; character
.walk:
    cmp bx, dx
    jb .root
.walkin:
    mov al, [es:bx + LZW_SUFF]
    stosb
    add bx, bx
    mov bx, [es:bx + LZW_PREF]
    cmp bx, dx
    jae .walkin
.root:
    mov al, bl                      ; a root is its own character
    mov [lzw_first], al
    stosb
    cld
    mov si, di
    inc si                          ; ES:SI = the string, CX = its length
    mov cx, LZW_STK
    sub cx, si
    call LZW_RUN
    jc .out
    mov bx, [lzw_free]              ; --- and the code this one defines
    cmp bx, 4096
    jae .noadd                      ; full: nothing until a Clear (deferred)
    mov ax, [lzw_old]
    cmp ax, bx
    jae .noadd                      ; a prefix at or above its own index is
    mov di, bx                      ; a chain that cycles: never added
    add di, di
    mov [es:di + LZW_PREF], ax
    mov al, [lzw_first]
    mov [es:bx + LZW_SUFF], al
    inc bx
    mov [lzw_free], bx
    cmp byte [lzw_cs], 12
    jae .noadd
%if LZW_EARLY
    inc bx
%endif
    mov cl, [lzw_cs]
    mov ax, 1
    shl ax, cl
    cmp bx, ax                      ; the table crossed a power of two
    jb .noadd
    inc byte [lzw_cs]
    stc
    rcl word [lzw_mask], 1
.noadd:
    mov ax, [lzw_code]
    mov [lzw_old], ax
    jmp .next
.clear:
    call lzw_reset
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
    dec ax
    mov [lzw_mask], ax
    mov word [lzw_old], 0xFFFF
    pop cx
    pop ax
    ret

; lzw_get - the next code, LSB first: AX. CF = 1 when it would need bits
; past the input's end. A code spans at most three bytes (7 bits left over
; and twelve), so it is a word and a byte shifted down together. Clobbers
; BX, CX, DX
lzw_get:
    mov bx, [lzw_ib]
    mov ax, [lzw_ie]
    sub ax, bx
    cmp ax, 3
    jae .have
    call lzw_refill                 ; AX = the bytes there now
    mov bx, [lzw_ib]
    mov cl, 3
    shl ax, cl                      ; ...as bits (at most LZW_INSZ x 8)
    mov dl, [lzw_bit]
    xor dh, dh
    sub ax, dx
    mov dl, [lzw_cs]
    cmp ax, dx
    jb .end                         ; not a whole code: the input has ended
.have:
    mov ax, [es:bx]
    mov cl, [lzw_bit]
    mov dl, cl
    add dl, [lzw_cs]                ; DL = where the code ends
    shr ax, cl                      ; the bits of the first two bytes
    cmp dl, 16
    jbe .two
    mov dh, [es:bx + 2]             ; ...and the third's, at 16 - bit:
    mov cl, 8                       ; (byte << (8 - bit)) in AH
    sub cl, [lzw_bit]
    shl dh, cl
    or ah, dh
.two:
    and ax, [lzw_mask]              ; AX = the code
    mov cl, dl                      ; ...and on past it
    and dl, 7
    mov [lzw_bit], dl
    shr cl, 1
    shr cl, 1
    shr cl, 1
    xor ch, ch
    add bx, cx
    mov [lzw_ib], bx
    clc
    ret
.end:
    stc
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
