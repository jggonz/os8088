; =============================================================================
; os8088 - apps/runcpm/rcband.inc      the row composer and the row compare
;
; Part of RUNCPM (SPEC.md 71), a reimplementation of RunCPM 6.9 by Marcelo
; Dantas / "Mockba the Borg" (MIT). %included by apps/runcpm/runcpm.asm after
; the compiled C and before CC_IMAGE_END. Nothing here is RunCPM's - it is the
; platform's cost model made concrete (SPEC.md 70.11: the inner loop is
; assembly; C composes and decides, and calls it once per ROW).
;
; The three routines wear SmallerC's cdecl: arguments at [bp+4], [bp+6], ...
; (SS-relative, which is right - they are on the stack and are READ, never
; addressed), the answer in AX, BP/DS/SS:SP/DF preserved and nothing else.
;
; rc_band reads the kernel's 8x8 glyph table through ES, and ES is the one
; register that is not this package's (SPEC.md 20.4): it is loaded from what
; OSAPI_FONT_GLYPHS answered and PUT BACK before returning. No string
; instruction is used anywhere in this file, so nothing here needs a
; cc8086:allow - the compare loops are plain moves, and the gate does not
; read the shim in any case.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; void rc_band(unsigned char *dst, const unsigned char *chars,
;              const unsigned char *attr, int first, int n)
;
; Compose n cells - chars[0..n-1], the cell whose attribute bit is `first`
; (bit 7 of attr[0] is cell 0, bit 6 cell 1, ...) being chars[0] - into an
; 8-row 1bpp band at dst whose stride is RC_BAND_STRIDE (= RC_COLS, 80) bytes
; a pixel row - the band buffer is 80x8 whatever n is, and the blit is handed
; that stride and n*8 pixels - in the framebuffer's polarity: a set bit is a
; LIT (white) pixel (SPEC.md 5.4.2), so a plain cell is its glyph XOR 0xFF
; (paper lit, ink clear) and a reverse-video cell is the glyph as it stands. A
; character outside the table's range - it covers what OSAPI_FONT_GLYPHS says
; it covers, and this terminal stores 32..126 only - is a blank cell. One
; os88_gfx_blit1(dst, RC_COLS, x, y, n*8, 8) then puts the band down: ONE
; drawing call for a row (SPEC.md 71.2), whatever n is.
;
; THE STRIDE IS AN ASSEMBLE-TIME CONSTANT AND THE EIGHT ROWS ARE UNROLLED,
; which is PERFORMANCE.md Set 64's lesson taken whole: the first version of
; this loop walked the eight rows with `inc bx / add di,[bp+12] / loop` around
; a stride held in memory and recomputed the attribute bit with two `shr ..,cl`
; a cell, and it measured 306 us a cell on the icount harness (tests/rcband,
; `make rcbandbench`; SPEC.md 71.2 has the table) - seven times the ~40 the
; cost model had guessed. Now a cell is: the attribute bit held in DH and
; rotated once per cell (the next attribute byte is fetched only when it
; wraps), the glyph address as three shifts, and eight `mov al,[es:bx+k] /
; xor al,ah / mov [di+80*k],al` triplets with no counter and no add - 145 us
; a cell to compose and 173 with the emit, 860 us a call, measured on the same
; harness in the same run (icount counts, Part 4's 0.359 ms a count).
;
; The glyph table is asked for ONCE and banked: DX:SI, the first code and the
; span, in the statics below.
; -----------------------------------------------------------------------------
RC_BAND_STRIDE equ 80               ; = RC_COLS in rcterm.c; rc_bandbuf is
                                    ; RC_COLS * 8 bytes and the C hands
                                    ; blit1 the same 80

_rc_band:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push bx                         ; [bp-8]: the attribute byte's address,
                                    ; advanced once per eight cells
    cmp byte [rc_gok], 0
    jne .have
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL = first, AH = last
    mov [rc_gseg], dx
    mov [rc_goff], si
    mov [rc_gfirst], al
    sub ah, al
    mov [rc_gspan], ah              ; last - first
    mov byte [rc_gok], 1
    jmp short .have
.none:
    jmp .out                        ; n = 0: .out is past a short jump's reach
.have:
    mov es, [rc_gseg]
    mov cx, [bp+12]                 ; n
    jcxz .none
    ; --- the attribute cursor: DL = the byte, DH = this cell's bit ---------
    mov ax, [bp+10]                 ; first
    mov bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1                       ; first / 8
    add bx, [bp+8]                  ; &attr[first / 8]
    mov [bp-8], bx
    mov dl, [bx]
    and al, 7
    xchg cx, ax                     ; CL = first & 7 (n parked in AX)
    mov dh, 0x80
    shr dh, cl                      ; the first cell's bit
    xchg cx, ax                     ; CX = n again
    mov di, [bp+4]                  ; dst: cell 0 of pixel row 0
    mov si, [bp+6]                  ; the characters
    jmp short .cell
    ; The blank cell sits ABOVE the loop so that both back-edges are short
    ; jumps: the unrolled glyph body fills most of a short jump's reach.
.blank:                             ; a blank glyph is 0 ^ mask, eight rows
%assign RCR 0
%rep 8
    mov [di + RCR * RC_BAND_STRIDE], ah
%assign RCR RCR+1
%endrep
    inc di
    loop .cell
    jmp short .out
.cell:
    ; --- the mask: 0xFF for a plain cell, 0x00 for reverse ------------------
    mov ah, 0xFF
    test dl, dh
    jz .plain
    xor ah, ah
.plain:                             ; AH = the xor mask
    ror dh, 1                       ; the next cell's bit; CF = it wrapped...
    jnc .glyph
    inc word [bp-8]                 ; ...so the next attribute byte
    mov bx, [bp-8]
    mov dl, [bx]
.glyph:
    mov al, [si]
    inc si
    sub al, [rc_gfirst]             ; below the table's first code wraps to
    cmp al, [rc_gspan]              ; 0xE0.., which is above its span too:
    ja .blank                       ; one compare covers both ends
    ; --- the glyph: es:[rc_goff + code*8], eight rows unrolled ---------------
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [rc_goff]               ; ES:BX = the glyph
%assign RCR 0
%rep 8
    mov al, [es:bx + RCR]
    xor al, ah
    mov [di + RCR * RC_BAND_STRIDE], al     ; DS:DI - the band is ours
%assign RCR RCR+1
%endrep
    inc di                          ; the next cell's column
    loop .cell
.out:
    pop bx
    pop es                          ; ES = KERNEL_SEG again, as it arrived
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int rc_rowdiff(const unsigned char *a, const unsigned char *b, int n)
;   -> the index of the FIRST byte where a and b differ, or -1
; int rc_rowdiffl(const unsigned char *a, const unsigned char *b, int n)
;   -> the index of the LAST byte where they differ, or -1
;
; The shadow compare of SPEC.md 71.2: called once (each) per row that can
; have changed, so a 25-row screen is at most 50 calls of ~10 cycles a byte -
; and the two answers together are the span one drawing call covers.
; -----------------------------------------------------------------------------
_rc_rowdiff:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    mov ax, -1
    jcxz .out
    xor dx, dx
.l:
    mov bl, [si]
    cmp bl, [di]                    ; DS:DI - a plain compare, not cmps
    jne .found
    inc si
    inc di
    inc dx
    loop .l
    jmp short .out
.found:
    mov ax, dx
.out:
    pop di
    pop si
    pop bp
    ret

_rc_rowdiffl:
    push bp
    mov bp, sp
    push si
    push di
    mov cx, [bp+8]
    mov ax, -1
    jcxz .out
    mov si, [bp+4]
    mov di, [bp+6]
    add si, cx
    add di, cx
    dec si
    dec di                          ; the last byte of each
    mov dx, cx
    dec dx                          ; its index
.l:
    mov bl, [si]
    cmp bl, [di]
    jne .found
    dec si
    dec di
    dec dx
    loop .l
    jmp short .out
.found:
    mov ax, dx
.out:
    pop di
    pop si
    pop bp
    ret

section .bss
rc_gseg:    resw 1                  ; the glyph table's segment (LOW_SEG)
rc_goff:    resw 1                  ; ...and offset
rc_gfirst:  resb 1                  ; the first character code it covers
rc_gspan:   resb 1                  ; last - first
rc_gok:     resb 1                  ; the three above are filled

section .text
