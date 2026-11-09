; =============================================================================
; os8088 - apps/os88inflate.inc - INFLATE: raw DEFLATE (RFC 1951), memory to
; memory, small rather than fast
;
; For a package that has to read a ZIP (ARCADE's ROM import, docs/plans/
; ARCADE-RELEASE-PLAN.md 2.2) or any other deflated stream it holds whole. It
; is zlib's `puff` written in 8086: the canonical Huffman codes decoded a BIT
; AT A TIME against each length's count, which needs no lookup table beyond
; the counts and the symbols - about 1KB of work space - and no window of its
; own, because the output IS the window (a distance reaches back into what
; was written). That makes it slow next to PIXEL's table-driven decoder
; (apps/pixel/pxpng.asm, SPEC.md 106.18), which streams a PNG through a 32KB
; ring on a worker; this is for a stream that fits its destination whole and
; is decoded once.
;
;   inflate   in   DS:SI = the stream, ES:DI = where its bytes go, CX = how
;                  many bytes it must make (0 = 65,536), DX = how many the
;                  stream may take (0 = 65,536): no byte past them is read
;             out  CF=0: exactly CX bytes made, DI past them, SI past the
;                  stream; CF=1: the stream is bad, makes more or fewer, or
;                  runs past DX. Clobbers AX, BX, CX, DX, BP; DS, ES kept.
;
; THE INCLUDER defines INF_WS, a label of INF_WS_SZ bytes in its own segment
; (bss), reached through CS - the stream's segment is DS. Interrupts may come
; and go; nothing here holds them off.
;
; ...OR it defines INF_DSWS, and nothing is stored through CS: kern_mega runs
; CS as a code selector that faults a store, ~77 us each, and the work space
; is written several times a BIT (docs/plans/ARCADE-MEGA-PLAN.md M2). Then
;   inflate   in   DS = the includer's own segment (INF_WS's), AX:SI = the
;                  stream, ES:DI, CX and DX as above; out as above, DS kept.
; The stream's segment is banked and loaded into DS for a byte's refill and
; a stored block's copy only - a segment load every 8 bits, the price.
; =============================================================================

INF_LCNT    equ 0                   ; 16 words: codes of each length, lit/len
INF_LSYM    equ 32                  ; 288 words: its symbols by code
INF_DCNT    equ 608                 ; the distance code's
INF_DSYM    equ 640                 ; 30 words
INF_LENS    equ 700                 ; 320 lengths (and the code-length code's)
INF_OFFS    equ 1020                ; 16 words: a build's offsets
INF_BITB    equ 1052                ; the byte being taken apart
INF_BITN    equ 1053                ;   ...and its bits left
INF_LEFT    equ 1054                ; bytes the output still owes (0: 65,536)
INF_LEFTH   equ 1056                ;   its high word (1 for 65,536)
INF_FIN     equ 1058                ; the last block's bit
INF_SP      equ 1060                ; SP at entry: the one way out of an error
INF_NL      equ 1062                ; a dynamic block's code counts
INF_ND      equ 1064
INF_TMP     equ 1066                ; inf_decode's length, as an offset
INF_HSYM    equ 1068                ;   ...and the symbols it decodes to
INF_PREV    equ 1070                ; the last code length, for code 16
INF_SSEG    equ 1072                ; INF_DSWS: the stream's segment
INF_INN     equ 1074                ; bytes the stream may still give (0:
INF_INNH    equ 1076                ;   65,536), its high word
INF_WS_SZ   equ 1078

%ifdef INF_DSWS
%define INF(x) ds:INF_WS+(x)
%define INF_WSEG ds                 ; the work space's segment register
%else
%define INF(x) cs:INF_WS+(x)
%define INF_WSEG cs
%endif

inflate:
    push si
    push di
    mov [INF(INF_SP)], sp
%ifdef INF_DSWS
    mov [INF(INF_SSEG)], ax
%endif
    mov word [INF(INF_LEFT)], cx
    mov word [INF(INF_LEFTH)], 0
    or cx, cx
    jnz .sized
    inc word [INF(INF_LEFTH)]       ; 65,536
.sized:
    mov word [INF(INF_INN)], dx
    mov word [INF(INF_INNH)], 0
    or dx, dx
    jnz .bound
    inc word [INF(INF_INNH)]        ; 65,536
.bound:
    mov byte [INF(INF_BITN)], 0
    cld
.block:
    call inf_bit
    mov al, 0
    adc al, 0
    mov [INF(INF_FIN)], al
    mov cl, 2
    call inf_bits
    cmp al, 0
    je .stored
    cmp al, 1
    je .fixed
    cmp al, 2
    je .dynamic
    jmp inf_bad
.stored:
    call inf_stored
    jmp short .next
.fixed:
    call inf_fixed
    call inf_codes
    jmp short .next
.dynamic:
    call inf_dynamic
    call inf_codes
.next:
    cmp byte [INF(INF_FIN)], 0
    je .block
    mov ax, [INF(INF_LEFT)]         ; exactly what was asked
    or ax, [INF(INF_LEFTH)]
    jnz inf_bad
    pop ax                          ; (DI is past the output now)
    pop ax                          ; (SI past the stream)
    clc
    ret

; inf_bad - out of anywhere: SP as inflate found it, CF=1
inf_bad:
    ; STKBALANCE-OK: never called - jumped to from any depth inside inflate;
    ; SP comes back from INF_SP, banked at entry below its two pushes
    ; STKBALANCE-LOOP: not a loop - reached at every depth by design, which
    ; is what the SP reload is for
    mov sp, [INF(INF_SP)]
    pop di
    pop si
    stc
    ret

; inf_bit - CF = the stream's next bit (LSB first). Kept: all but flags
inf_bit:
    dec byte [INF(INF_BITN)]
    jns .have
    mov byte [INF(INF_BITN)], 7
    push ax
    mov ax, 1
    call inf_in
%ifdef INF_DSWS
    push ds
    mov ds, [INF(INF_SSEG)]
    lodsb
    pop ds
%else
    lodsb
%endif
    mov [INF(INF_BITB)], al
    pop ax
.have:
    shr byte [INF(INF_BITB)], 1
    ret

; inf_bits - AX = the next CL bits (CL 0-13), the first the lowest. Kept: BX,
; CX, DX
inf_bits:
    push bx
    push cx
    xor ax, ax
    mov bx, 1
.l:
    or cl, cl
    jz .d
    call inf_bit
    jnc .z
    or ax, bx
.z:
    shl bx, 1
    dec cl
    jmp .l
.d:
    pop cx
    pop bx
    ret

; inf_out - AX = bytes about to be written: out of room is an error
inf_out:
    sub [INF(INF_LEFT)], ax
    sbb word [INF(INF_LEFTH)], 0
    jc inf_bad
    ret

; inf_in - AX = bytes about to be read from the stream: past DX is an error
inf_in:
    sub [INF(INF_INN)], ax
    sbb word [INF(INF_INNH)], 0
    jc inf_bad
    ret

; inf_stored - a stored block: to the byte, LEN and its complement, LEN bytes
inf_stored:
    mov byte [INF(INF_BITN)], 0     ; the rest of this byte is padding
    mov ax, 4
    call inf_in
%ifdef INF_DSWS
    push ds
    mov ds, [INF(INF_SSEG)]
%endif
    lodsw
    mov cx, ax
    lodsw
%ifdef INF_DSWS
    pop ds                          ; (inf_bad reads INF_SP through DS)
%endif
    not ax
    cmp ax, cx
    jne inf_bad
    mov ax, cx
    call inf_out
    call inf_in
%ifdef INF_DSWS
    push ds
    mov ds, [INF(INF_SSEG)]
    rep movsb
    pop ds
%else
    rep movsb
%endif
    ret

; inf_fixed - the fixed codes (RFC 1951 3.2.6)
inf_fixed:
    push di
    push es
    push INF_WSEG
    pop es
    mov di, INF_WS + INF_LENS
    mov al, 8
    mov cx, 144
    rep stosb
    mov al, 9
    mov cx, 112
    rep stosb
    mov al, 7
    mov cx, 24
    rep stosb
    mov al, 8
    mov cx, 8
    rep stosb
    mov al, 5                       ; the distances, after them
    mov cx, 30
    rep stosb
    pop es
    pop di
    mov word [INF(INF_NL)], 288
    mov word [INF(INF_ND)], 30
    jmp inf_bothcodes

; inf_dynamic - a dynamic block's header: the code-length code, then the
; literal/length and distance codes' lengths through it
inf_dynamic:
    mov cl, 5
    call inf_bits
    add ax, 257
    mov [INF(INF_NL)], ax
    mov cl, 5
    call inf_bits
    inc ax
    mov [INF(INF_ND)], ax
    cmp word [INF(INF_NL)], 286
    ja inf_bad
    cmp word [INF(INF_ND)], 30
    ja inf_bad
    mov cl, 4
    call inf_bits
    add ax, 4
    mov dx, ax                      ; DX = the code-length code's lengths given
    push di
    push es
    push INF_WSEG
    pop es
    mov di, INF_WS + INF_LENS       ; all 19 zero first
    xor al, al
    mov cx, 19
    rep stosb
    pop es
    pop di
    xor bx, bx
.cl:
    cmp bx, dx
    jae .clbuilt
    mov cl, 3
    call inf_bits
    push bx
    mov bl, [cs:inf_order+bx]
    xor bh, bh
    mov [INF(INF_LENS)+bx], al
    pop bx
    inc bx
    jmp .cl
.clbuilt:
    mov bx, INF_LCNT                ; the code-length code, in the lit/len
    mov word [INF(INF_HSYM)], INF_LSYM  ; tables for now
    mov dx, INF_LENS
    mov cx, 19
    call inf_build
    xor bx, bx                      ; BX = lengths read
    mov byte [INF(INF_PREV)], 0
    mov cx, [INF(INF_NL)]
    add cx, [INF(INF_ND)]
.len:
    cmp bx, cx
    jae .lens
    push bx
    mov bx, INF_LCNT
    call inf_decode
    pop bx
    cmp ax, 16
    jae .rep
    mov [INF(INF_LENS)+bx], al
    mov [INF(INF_PREV)], al
    inc bx
    jmp .len
.rep:
    push cx
    mov dl, [INF(INF_PREV)]         ; 16: the last length, 3-6 times
    cmp ax, 17
    jb .r16
    mov dl, 0
    je .r17
    mov cl, 7                       ; 18: zero, 11-138 times
    call inf_bits
    add ax, 11
    jmp short .fill
.r17:
    mov cl, 3                       ; 17: zero, 3-10 times
    call inf_bits
    add ax, 3
    jmp short .fill
.r16:
    or bx, bx                       ; (no last length yet)
    jz inf_bad
    mov cl, 2
    call inf_bits
    add ax, 3
.fill:
    pop cx
    push cx
    sub cx, bx                      ; no run past the end
    cmp ax, cx
    ja inf_bad
    mov cx, ax
.f:
    mov [INF(INF_LENS)+bx], dl
    inc bx
    loop .f
    pop cx
    jmp .len
.lens:
    ; fall through
; inf_bothcodes - the lit/len code from INF_LENS[0..NL), the distance code
; from INF_LENS[NL..NL+ND)
inf_bothcodes:
    mov bx, INF_LCNT
    mov word [INF(INF_HSYM)], INF_LSYM
    mov dx, INF_LENS
    mov cx, [INF(INF_NL)]
    call inf_build
    mov bx, INF_DCNT
    mov word [INF(INF_HSYM)], INF_DSYM
    mov dx, INF_LENS
    add dx, [INF(INF_NL)]
    mov cx, [INF(INF_ND)]
    jmp inf_build

; inf_build - a canonical code from CX lengths at INF_WS+DX: its counts at
; INF_WS+BX (16 words), its symbols at INF_WS+[HSYM]. Kept: SI, DI, BX
inf_build:
    push si
    push di
    push cx
    mov di, bx                      ; the counts, zero
    xor ax, ax
.z:
    mov [INF(0)+di], ax
    add di, 2
    lea si, [bx+32]
    cmp di, si
    jb .z
    mov si, dx                      ; each length counted
    pop cx
    push cx
.c:
    mov al, [INF(0)+si]
    xor ah, ah
    shl ax, 1
    mov di, ax
    inc word [INF(0)+bx+di]
    inc si
    loop .c
    xor ax, ax                      ; the offsets: each length's first
    mov di, 2                       ;   symbol's place
    mov [INF(INF_OFFS)+2], ax
.o:
    add ax, [INF(0)+bx+di]
    mov [INF(INF_OFFS)+di+2], ax
    add di, 2
    cmp di, 30
    jb .o
    mov si, dx                      ; the symbols, in order
    pop cx
    push cx
    xor ax, ax                      ; AX = the symbol
.s:
    push ax
    mov al, [INF(0)+si]
    or al, al
    jz .none
    xor ah, ah
    shl ax, 1
    mov di, ax
    mov di, [INF(INF_OFFS)+di]
    shl di, 1
    add di, [INF(INF_HSYM)]
    pop ax
    mov [INF(0)+di], ax
    push ax
    mov al, [INF(0)+si]
    xor ah, ah
    shl ax, 1
    mov di, ax
    inc word [INF(INF_OFFS)+di]
.none:
    pop ax
    inc ax
    inc si
    loop .s
    pop cx
    pop di
    pop si
    ret

; inf_decode - AX = the next symbol of the code at INF_WS+BX (its counts) and
; INF_WS+[HSYM] (its symbols); no code of 15 bits or fewer is an error.
; Kept: BX, CX, DX
inf_decode:
    push cx
    push dx
    push bp
    xor dx, dx                      ; DX = the code so far
    xor cx, cx                      ; CX = the first code of this length
    xor bp, bp                      ; BP = the first symbol of this length
    mov word [INF(INF_TMP)], 2      ; (the length, as a word offset)
.l:
    call inf_bit
    adc dx, 0
    push bx
    add bx, [INF(INF_TMP)]
    mov ax, [INF(0)+bx]             ; how many codes this long
    pop bx
    push dx
    sub dx, cx
    cmp dx, ax
    jb .found
    pop dx
    add bp, ax
    add cx, ax
    shl cx, 1
    shl dx, 1
    add word [INF(INF_TMP)], 2
    cmp word [INF(INF_TMP)], 32
    jb .l
    jmp inf_bad
.found:
    pop ax                          ; (the code, not wanted now)
    add dx, bp
    shl dx, 1
    add dx, [INF(INF_HSYM)]
    push bx
    mov bx, dx
    mov ax, [INF(0)+bx]
    pop bx
    pop bp
    pop dx
    pop cx
    ret

; inf_codes - a block's literals and copies, to its end
inf_codes:
.sym:
    mov bx, INF_LCNT
    mov word [INF(INF_HSYM)], INF_LSYM
    call inf_decode
    cmp ax, 256
    jae .notlit
    push ax
    mov ax, 1
    call inf_out
    pop ax
    stosb
    jmp .sym
.notlit:
    je .end
    sub ax, 257                     ; a length
    cmp ax, 29
    jae inf_bad
    mov bx, ax
    mov cl, [cs:inf_lext+bx]
    shl bx, 1
    mov dx, [cs:inf_lbase+bx]
    call inf_bits
    add dx, ax                      ; DX = the length
    push dx
    mov bx, INF_DCNT                ; its distance
    mov word [INF(INF_HSYM)], INF_DSYM
    call inf_decode
    cmp ax, 30
    jae inf_bad
    mov bx, ax
    mov cl, [cs:inf_dext+bx]
    shl bx, 1
    mov dx, [cs:inf_dbase+bx]
    call inf_bits
    add dx, ax                      ; DX = the distance
    pop cx                          ; CX = the length
    mov ax, cx
    call inf_out
    push si                         ; from ES:DI - distance, forward, so an
    push ds                         ;   overlap repeats as it should
    mov ax, es
    mov ds, ax
    mov si, di
    sub si, dx
    rep movsb
    pop ds
    pop si
    jmp .sym
.end:
    ret

inf_order:  db 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
inf_lbase:  dw 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31
            dw 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
inf_lext:   db 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2
            db 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
inf_dbase:  dw 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193
            dw 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145
            dw 8193, 12289, 16385, 24577
inf_dext:   db 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6
            db 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
