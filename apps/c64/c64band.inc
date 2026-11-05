; =============================================================================
; os8088 - apps/c64/c64band.inc     the 1bpp composers and the span compare
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
; VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later; apps/c64/ is
; GPL-2-or-later and apps/c64/COPYING is the licence text. Nothing of VICE's
; source is vendored: what the VIC-II puts on a scanline is reimplemented here
; from src/vicii/vicii-draw.c and viciitypes.h. rcband.inc (SPEC.md 74.2) is
; the shape.
;
; %included by apps/c64/c64.asm after the compiled C, by
; hosttest/c64memtest.asm (the SS != DS gate for the string loops,
; C64-SPEC §3.6) and by tests/c64band/c64bandbench.asm (`make
; c64bandbench`, the icount bench whose microseconds the tier table in
; c64scr.c is written FROM).
;
; -----------------------------------------------------------------------------
; WHY THIS IS ASSEMBLY, AND WHY IT TAKES A SPAN
; -----------------------------------------------------------------------------
; SPEC.md 73.11: C composes and decides, and the thing that touches a pixel
; per iteration is a hand-written proc called ONCE per row. A C64 text row is
; 40 cells x 8 pixel rows = 320 bytes of band; lettering it a glyph at a time
; would be 40 x 756 us of per-call floor before a single pixel was decided.
;
; And the composer takes `first..last`, never "always 40 cells"
; (C64-SPEC §9.5). A one-cell change composes one cell. That is the
; difference between a keystroke costing about a millisecond and about eight.
;
; THE STRIDE IS AN ASSEMBLE-TIME CONSTANT AND THE EIGHT ROWS ARE UNROLLED -
; PERFORMANCE.md Set 64's lesson taken whole, the one that cost RUNCPM's first
; row composer 306 us a cell against a model that had guessed 40.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved. Every `movs`/`cmps` loads DS/ES on
; purpose, restores both, leaves DF clear and carries `cc8086:allow` with its
; reason (C64-SPEC §3.6).
; =============================================================================

section .bss

; The interleaved {character, colour} scratch c64_band1 builds in phase A and
; walks with one `lodsw` per cell in phase B. Interleaving is what frees the
; last register: the character and the colour advance in lockstep, so they
; travel in AL and AH of one word instead of costing two pointers.
c64_crow:   resb 80

; The byte -> two-byte pixel-doubling table c64_band_x2 reads, built ONCE by
; c64_x2init. It is 512 bytes of BSS and not of .data on purpose: bss costs no
; file bytes and the loader zeroes it, while an initialised array is paid for
; twice (SPEC.md 73.9). Each entry is {high 8 doubled bits, low 8}, in that
; order, so one `mov ax,[tab+bx]` / `mov [di],ax` writes them left to right.
c64_x2tab:  resb 512

; c64_bandbm's per-cell colour block: four colours' 4x4 Bayer phases, laid
; [pair value * 4 + line & 3] so a line reads all four at [bx], [bx+4], ...
_c64_bfbest: resw 1                  ; c64_bandfind's best score...
c64_bfk:    resw 1                  ; ...its k...
c64_bfkk:   resw 1                  ; ...the k being tried...
c64_bfst:   resw 1                  ; ...and the run's first row
c64_bmblk:  resb 16
c64_bmend:  resw 1                  ; ...and where its cell's eight lines end

c64_spf:    resw 1                  ; c64_rowspan's first differing cell...
c64_spl:    resw 1                  ; ...and its last
c64_spa:    resw 1                  ; ...and the two row bases it measures
c64_spb:    resw 1                  ;    them from, one pixel row at a time

section .text

C64_BSTRIDE equ 40                  ; bytes per pixel row of a composed band -
                                    ; 40 cells x 8 px = 320 px = the C64's
                                    ; visible width. An assemble-time constant
                                    ; because Set 64 says so.
C64_X2STRIDE equ 80                 ; ...and of a pixel-doubled one

; -----------------------------------------------------------------------------
; void c64_band1(unsigned char *dst, int first, int last,
;                unsigned mseg, unsigned moff,
;                const unsigned char *col,
;                unsigned gseg, unsigned goff,
;                int mode, int bg)
;
; Compose cells first..last of ONE character row into an 8-row 1bpp band at
; dst (stride C64_BSTRIDE), in the framebuffer's polarity: a set bit is a LIT
; pixel (SPEC.md 5.4.2).
;
;   mseg:moff   the screen matrix row - 40 bytes in the RAM claim
;   col         the colour row - 40 near bytes of the package's colour RAM
;   gseg:goff   the character generator: the CHARGEN ROM in the ROM claim, or
;               the RAM character set the VIC is pointed at. NEVER
;               OSAPI_FONT_GLYPHS - this is a C64 face (C64-SPEC §9.5)
;   mode        0 = standard text, the only mode the flush hands this
;               composer; the other modes are c64scr.c's c64_bandx (C64-SPEC
;               §9.10). A nonzero mode still fills the span with c64_bgfill,
;               a fallback nothing calls. The harness asserts mode 0 against
;               tools/c64ref.py bit for bit
;   bg          $D021, for the mode != 0 fill
;
; MONOCHROME BY LUMINANCE, RELATIVELY (C64-SPEC §9.6). The C hands over
; c64_cmask, 16 {and, xor} pairs indexed by the cell's foreground nibble:
; out = (glyph & and) ^ xor. Ink brighter than paper is {0xFF, 0x00} (the
; glyph as it stands); paper brighter is {0xFF, 0xFF} (inverted); a cell whose
; ink and paper have EQUAL luminance is {0x00, level} - a uniform cell, which
; is what the machine really shows and what an XOR alone cannot express.
; -----------------------------------------------------------------------------
_c64_band1:
    push bp
    mov bp, sp
    push si
    push di
    push es

    mov cx, [bp+8]                  ; last
    sub cx, [bp+6]                  ; - first
    inc cx                          ; = n
    cmp cx, 0
    jle .out                        ; an empty span composes nothing
    cmp word [bp+20], 0             ; mode
    jne .fill

    ; --- phase A: interleave {character, colour nibble} into c64_crow --------
    mov es, [bp+10]                 ; mseg
    mov si, [bp+12]                 ; moff
    add si, [bp+6]                  ; + first
    mov di, [bp+6]
    shl di, 1
    add di, c64_crow                ; &c64_crow[first * 2]
    mov bx, [bp+14]                 ; col
    add bx, [bp+6]                  ; &col[first]
.a:
    mov al, [es:si]
    inc si
    mov [di], al
    mov al, [bx]
    inc bx
    and al, 15
    mov [di+1], al
    add di, 2
    loop .a

    ; --- phase B: one decision per cell, eight unrolled stores --------------
    mov cx, [bp+8]
    sub cx, [bp+6]
    inc cx
    mov es, [bp+16]                 ; gseg - the character generator
    mov si, [bp+6]
    shl si, 1
    add si, c64_crow
    mov di, [bp+4]                  ; dst
    add di, [bp+6]                  ; + first: cell c is byte c of every row
.b:
    lodsw                           ; AL = the character, AH = its colour
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov dx, [_c64_cmask+bx]         ; DL = and, DH = xor
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; the glyph is eight bytes
    add bx, [bp+18]                 ; + goff
    mov al, [es:bx]
    and al, dl
    xor al, dh
    mov [di], al
    mov al, [es:bx+1]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE], al
    mov al, [es:bx+2]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE*2], al
    mov al, [es:bx+3]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE*3], al
    mov al, [es:bx+4]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE*4], al
    mov al, [es:bx+5]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE*5], al
    mov al, [es:bx+6]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE*6], al
    mov al, [es:bx+7]
    and al, dl
    xor al, dh
    mov [di+C64_BSTRIDE*7], al
    inc di
    loop .b
    jmp short .out

    ; --- a mode wave 1 does not compose: the span becomes background --------
.fill:
    mov di, [bp+4]
    add di, [bp+6]
    mov al, [_c64_bgfill]
    mov dx, 8                       ; the eight pixel rows
.f1:
    push cx
    push di
.f2:
    mov [di], al
    inc di
    loop .f2
    pop di
    pop cx
    add di, C64_BSTRIDE
    dec dx
    jnz .f1

.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_rowspan(const unsigned char *a, const unsigned char *b, int n)
;
; Compare a composed band against the frame shadow - eight pixel rows of n
; bytes at stride C64_BSTRIDE each, both in the package's own segment - and
; answer -1 when they are identical, or (first << 8) | last, the first and
; last differing CELL COLUMN relative to a.
;
; This is what makes the shadow the glass and not the model (docs/C64-SPEC.md
; 9.3): if the composed row equals the shadow, nothing is drawn, whatever the
; model believed.
;
; It is the reason the ES sentinel case exists in hosttest/c64memtest.asm: the
; forward and reverse `repe cmpsb` below load ES on purpose, and DF is set for
; the reverse scan and cleared before the return.

; -----------------------------------------------------------------------------
; void c64_sprconv(const unsigned char *sd, unsigned char *on,
;                  unsigned char *lit, int mode, const unsigned *xexp)
; One sprite line's three data bytes as c64_sprrow's ON and LIT (C64-SPEC
; 9.11), all near: MODE bit 0 multicolour, bit 1 X-expanded (each byte is
; two, through XEXP's 256 doubled words, high byte first), bits 2-4 whether
; $D025, the sprite's own colour and $D026 are brighter than the paper. A
; multicolour byte's pairs 01/10/11 are split as c64_bandbm splits a bitmap
; byte. It was c64_spr_draw's C, a line at a time: 12% of a 286 under PCUG's
; logo in the window.
; -----------------------------------------------------------------------------
_c64_sprconv:
    push bp
    mov bp, sp
    sub sp, 8
    push si
    push di
    cld
    mov si, [bp+4]
    mov di, [bp+6]
    mov ax, [bp+8]
    mov [bp-8], ax                  ; the LIT pointer
    mov dx, [bp+10]
    xor ax, ax
    test dl, 4
    jz .m1
    mov al, 0xFF
.m1:
    mov [bp-2], al                  ; $D025 lit: all or nothing
    xor ax, ax
    test dl, 8
    jz .m2
    mov al, 0xFF
.m2:
    mov [bp-4], al                  ; the sprite's own colour
    xor ax, ax
    test dl, 16
    jz .m3
    mov al, 0xFF
.m3:
    mov [bp-6], al                  ; $D026
    mov cx, 3
.byte:
    push cx
    lodsb
    test byte [bp+10], 1
    jz .hires
    mov ah, al                      ; the pairs' high bits...
    shr ah, 1
    and ah, 0x55
    and al, 0x55                    ; ...and their low bits
    mov cl, ah
    and cl, al                      ; 11
    xor ah, cl                      ; 10
    xor al, cl                      ; 01
    mov ch, al                      ; each to both pixels of its pair
    shl ch, 1
    or al, ch
    mov ch, ah
    shl ch, 1
    or ah, ch
    mov ch, cl
    shl ch, 1
    or cl, ch
    mov dl, al
    and dl, [bp-2]
    mov dh, ah
    and dh, [bp-4]
    or dl, dh
    mov dh, cl
    and dh, [bp-6]
    or dl, dh                       ; LIT
    or al, ah
    or al, cl                       ; ON
    jmp short .store
.hires:
    mov dl, al
    and dl, [bp-4]
.store:
    test byte [bp+10], 2
    jnz .xs
    mov [di], al
    inc di
    mov bx, [bp-8]
    mov [bx], dl
    inc word [bp-8]
    jmp short .next
.xs:
    mov bx, [bp+12]
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov ax, [bx]
    mov [di], ah
    mov [di+1], al
    add di, 2
    mov bx, [bp+12]
    mov al, dl
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov ax, [bx]
    mov bx, [bp-8]
    mov [bx], ah
    mov [bx+1], al
    add word [bp-8], 2
.next:
    pop cx
    dec cx
    jz .done
    jmp .byte
.done:
    pop di
    pop si
    add sp, 8
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_sprrow(unsigned char *dst, const unsigned char *on,
;                 const unsigned char *lit, int n, int cell, int sh,
;                 int f, int l)
; One line of a sprite over one band line (C64-SPEC 9.11): N bytes of pixels,
; ON (the pixels the sprite sets) and LIT (those of them brighter than the
; paper), placed from band cell CELL shifted SH (0..7) pixels right, and
; only into cells F..L - the rest are another flush's. Each byte straddles
; two cells: `shr ax, cl` with the byte in AH splits it into both at once.
; -----------------------------------------------------------------------------
_c64_sprrow:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    add [bp+16], ax                 ; f -> &dst[f]
    add [bp+18], ax                 ; l -> &dst[l]
    mov di, ax
    add di, [bp+12]                 ; &dst[cell]
    mov si, [bp+6]
    mov bx, [bp+8]
    mov cx, [bp+14]                 ; CL = the shift
    cmp word [bp+10], 0
    jle .out
.lp:
    mov ah, [si]
    xor al, al
    shr ax, cl                      ; AH this cell's part, AL the next's
    mov dh, [bx]
    xor dl, dl
    shr dx, cl
    cmp di, [bp+16]
    jb .n1
    cmp di, [bp+18]
    ja .n1
    not ah
    and [di], ah
    or [di], dh
.n1:
    inc di
    cmp di, [bp+16]
    jb .n2
    cmp di, [bp+18]
    ja .n2
    not al
    and [di], al
    or [di], dl
.n2:
    inc si
    inc bx
    dec word [bp+10]
    jnz .lp
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_bandfind(const unsigned *sig, const unsigned *shsig, int n, int kmax)
; C64-SPEC 9.4.1's band test: for k = -kmax..kmax (not 0), the longest run of
; rows i with sig[i] == shsig[i+k], scored by how many of them CHANGED
; (sig[i] != shsig[i]). Answers the best k (0: no run scored), and leaves the
; run's first and last row in _c64_sb0/_c64_sb1 and its score in _c64_bfbest.
; It was C, and 14% of a 286 under the Portland CUG demo's snow.
; -----------------------------------------------------------------------------
_c64_bandfind:
    push bp
    mov bp, sp
    push si
    push di
    mov word [_c64_bfbest], 0
    mov word [c64_bfk], 0
    mov ax, [bp+10]
    neg ax
    mov [c64_bfkk], ax              ; k = -kmax
.kloop:
    mov ax, [c64_bfkk]
    cmp ax, [bp+10]
    jle .kgo
    jmp .done
.kgo:
    or ax, ax
    jnz .k1
    jmp .knext
.k1:
    xor cx, cx                      ; CX = the run, DX = its changed rows
    xor dx, dx
    xor bx, bx                      ; BX = i
.iloop:
    cmp bx, [bp+8]
    jge .iend
    mov ax, bx
    add ax, [c64_bfkk]              ; j = i + k
    js .brk
    cmp ax, [bp+8]
    jge .brk
    mov si, bx
    shl si, 1
    add si, [bp+4]
    mov si, [si]                    ; sig[i]
    mov di, ax
    shl di, 1
    add di, [bp+6]
    cmp si, [di]                    ; shsig[j]
    jne .brk
    or cx, cx
    jnz .run
    mov [c64_bfst], bx
.run:
    inc cx
    mov di, bx
    shl di, 1
    add di, [bp+6]
    cmp si, [di]                    ; shsig[i]: did it change?
    je .inext
    inc dx
    jmp short .inext
.brk:
    call .score
    xor cx, cx
    xor dx, dx
.inext:
    inc bx
    jmp .iloop
.iend:
    call .score
.knext:
    inc word [c64_bfkk]
    jmp .kloop
.done:
    mov ax, [c64_bfk]
    pop di
    pop si
    pop bp
    ret
; the run that just ended (CX rows, DX changed, from c64_bfst): the best yet?
.score:
    cmp dx, [_c64_bfbest]
    jle .sr
    mov [_c64_bfbest], dx
    mov ax, [c64_bfkk]
    mov [c64_bfk], ax
    mov ax, [c64_bfst]
    mov [_c64_sb0], ax
    add ax, cx
    dec ax
    mov [_c64_sb1], ax
.sr:
    ret

; -----------------------------------------------------------------------------
; int c64_neq(const unsigned char *a, const unsigned char *b, int nwords)
; 1 when NWORDS words at A and B differ, else 0 (C64-SPEC 9.4.1's source
; proof: a row's 80 source bytes against what it was drawn from)
; -----------------------------------------------------------------------------
_c64_neq:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    xor ax, ax
    jcxz .same
    repe cmpsw
    je .same
    inc ax
.same:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_rvdiff(const unsigned char *a, const unsigned char *b)
; 1 when two 48-byte display-register sets (C64-SPEC 9.10, C64_RVN) differ,
; else 0. The latch asks it for every row a write landed on, every frame -
; it was a C word loop and 8% of a 286 under a demo that splits every row.
; -----------------------------------------------------------------------------
; int c64_runfind(const unsigned char *a, const unsigned char *b, int from)
; The next RUN of changed cells in two 80-byte source records (C64-SPEC 9.4.1:
; 40 matrix bytes, then 40 colour bytes), both near: from cell FROM on, the
; first cell whose matrix or colour byte differs, extended over every later
; differing cell until three equal cells in a row (a gap of two or fewer is
; folded in). Answers (first << 8) | last, or -1 when nothing from FROM on
; differs. It was c64scr.c's loop, and SmallerC's byte loop cost the 286 more
; than the cells it saved composing.
_c64_runfind:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov di, [bp+6]
    mov bx, [bp+8]
.seek:
    cmp bx, 40
    jae .none
    mov al, [si+bx]
    cmp al, [di+bx]
    jne .hit
    mov al, [si+bx+40]
    cmp al, [di+bx+40]
    jne .hit
    inc bx
    jmp short .seek
.hit:
    mov cx, bx                      ; CL = the run's first cell
    mov dx, bx                      ; DL = its last differing cell
.ext:
    inc bx
    cmp bx, 40
    jae .done
    mov ax, bx
    sub ax, dx
    cmp ax, 3                       ; three equal cells since the last: ends
    ja .done
    mov al, [si+bx]
    cmp al, [di+bx]
    jne .more
    mov al, [si+bx+40]
    cmp al, [di+bx+40]
    je .ext
.more:
    mov dx, bx
    jmp short .ext
.done:
    mov ah, cl
    mov al, dl
    jmp short .out
.none:
    mov ax, -1
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
_c64_rvdiff:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es                          ; both sets are near: ES = DS for cmpsw
    cld
    mov si, [bp+4]
    mov di, [bp+6]
    mov cx, 24
    xor ax, ax
    repe cmpsw
    je .same
    inc ax
.same:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_bandbm(unsigned char *dst, const unsigned char *xm,
;                 const unsigned char *xb, const unsigned char *col,
;                 int n, int mc, int bg)
; N cells of a BITMAP row into the band (C64-SPEC 9.10): dst is the band at
; the first cell, xm the cells' matrix bytes, xb their bitmap bytes (eight a
; cell), col their colour RAM nibbles, all near - c64scr.c copies the matrix
; and bitmap out of the RAM claim first. mc 0 is hires (a 1 bit takes the
; matrix's high nibble, a 0 its low); mc 1 is multicolour (pairs 00 paper,
; 01 high nibble, 10 low, 11 colour RAM). Each colour is its 4x4 ordered
; dither out of _c64_dmask[colour][line & 3].
;
; THE MULTICOLOUR BYTE HAS NO LOOP AND NO SHIFT BY A COUNT: AH = the pairs'
; high bits and AL their low bits, both on the 0x55 lattice, give the four
; pair-value selectors in seven ALU ops; one word shift doubles two of them
; to both pixels of their pair at once (only bits 0-6 are set, so nothing
; crosses from the low byte to the high), and four ANDs against the block
; pick the colours. It is c64scr.c's loop at ~22 instructions a byte.
; -----------------------------------------------------------------------------
_c64_bandbm:
    push bp
    mov bp, sp
    push si
    push di
    cld
    mov cx, [bp+12]                 ; n
    cmp cx, 0
    jg .go
    jmp .out
.go:
    mov bx, [bp+16]                 ; the paper's phases, once a call
    and bx, 15
    shl bx, 1
    shl bx, 1
    mov ax, [_c64_dmask+bx]
    mov [c64_bmblk], ax
    mov ax, [_c64_dmask+bx+2]
    mov [c64_bmblk+2], ax
    mov si, [bp+8]                  ; xb
    mov di, [bp+4]                  ; dst
.cell:
    push cx
    mov bx, [bp+6]                  ; the matrix byte
    mov al, [bx]
    inc bx
    mov [bp+6], bx
    mov bl, al                      ; its high nibble's phases -> block+4
    xor bh, bh
    and bl, 0xF0
    shr bx, 1
    shr bx, 1
    mov dx, [_c64_dmask+bx]
    mov [c64_bmblk+4], dx
    mov dx, [_c64_dmask+bx+2]
    mov [c64_bmblk+6], dx
    mov bl, al                      ; ...its low nibble's -> block+8
    and bl, 15
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov dx, [_c64_dmask+bx]
    mov [c64_bmblk+8], dx
    mov dx, [_c64_dmask+bx+2]
    mov [c64_bmblk+10], dx
    mov bx, [bp+10]                 ; ...and the colour RAM's -> block+12
    mov al, [bx]
    inc bx
    mov [bp+10], bx
    mov bl, al
    and bl, 15
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov dx, [_c64_dmask+bx]
    mov [c64_bmblk+12], dx
    mov dx, [_c64_dmask+bx+2]
    mov [c64_bmblk+14], dx
    mov ax, di
    add ax, 320                     ; eight lines of a 40-byte band
    mov [c64_bmend], ax
    mov bx, c64_bmblk
    cmp word [bp+14], 0
    je .hline
.mline:
    lodsb
    mov ah, al
    shr ah, 1
    and ax, 0x5555                  ; AH = high bits, AL = low bits
    mov ch, ah
    and ch, al                      ; s3 = 11
    mov cl, ah
    xor cl, ch                      ; s2 = 10
    mov dl, al
    xor dl, ch                      ; s1 = 01
    mov dh, ah
    or dh, al
    xor dh, 0x55                    ; s0 = 00
    mov ax, cx
    shl ax, 1
    or cx, ax                       ; s3, s2 doubled
    mov ax, dx
    shl ax, 1
    or dx, ax                       ; s0, s1 doubled
    and dh, [bx]                    ; paper
    and dl, [bx+4]                  ; high nibble
    and cl, [bx+8]                  ; low nibble
    and ch, [bx+12]                 ; colour RAM
    or ch, cl
    or dl, dh
    or ch, dl
    mov [di], ch
    add di, 40
    inc bx
    cmp bx, c64_bmblk+4
    jne .m2
    mov bx, c64_bmblk
.m2:
    cmp di, [c64_bmend]
    jne .mline
    jmp .next
.hline:
    lodsb                           ; hires: (b & high) | (~b & low)
    mov ah, al
    not ah
    and al, [bx+4]
    and ah, [bx+8]
    or al, ah
    mov [di], al
    add di, 40
    inc bx
    cmp bx, c64_bmblk+4
    jne .h2
    mov bx, c64_bmblk
.h2:
    cmp di, [c64_bmend]
    jne .hline
.next:
    sub di, 320 - 1                 ; the next cell, line 0
    pop cx
    dec cx
    jz .out
    jmp .cell
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
_c64_rowspan:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, ds
    mov es, ax                      ; ES = DS on purpose: both operands are
                                    ; the package's own memory
    mov word [c64_spf], 0x7FFF      ; min sentinel
    mov word [c64_spl], -1          ; max sentinel
    mov dx, 8                       ; the pixel row counter
    mov si, [bp+4]
    mov di, [bp+6]
.row:
    mov [c64_spa], si               ; this pixel row's two bases, so both
    mov [c64_spb], di               ; scans below can measure from them
    mov cx, [bp+8]                  ; n
    jcxz .next
    cld
    repe cmpsb                      ; cc8086:allow ES = DS, loaded above and
                                    ; restored below; DF clear
    je .next                        ; the whole pixel row matched
    dec si                          ; SI was one PAST the differing byte
    mov ax, si
    sub ax, [c64_spa]               ; the offset within the row
    cmp ax, [c64_spf]
    jae .nofirst                    ; c64_spf's sentinel is 0x7FFF, so either
                                    ; compare is right here; the one below is
                                    ; not
    mov [c64_spf], ax
.nofirst:
    ; ...and the LAST differing byte, from the far end. The count stops at the
    ; byte just found, which is known to differ, so this scan always ends on a
    ; mismatch and never runs off the front.
    mov cx, [bp+8]
    sub cx, ax
    mov si, [c64_spa]
    add si, [bp+8]
    dec si
    mov di, [c64_spb]
    add di, [bp+8]
    dec di
    std
    repe cmpsb                      ; cc8086:allow ES = DS as above; DF is set
                                    ; here on purpose and cleared on the next
                                    ; line, and again at .done
    cld
    inc si                          ; SI was one BEFORE it, DF being set
    mov ax, si
    sub ax, [c64_spa]
    cmp ax, [c64_spl]
    jle .nolast                     ; SIGNED: c64_spl's "nothing yet" sentinel
                                    ; is -1, and an unsigned compare against
                                    ; 0xFFFF would never store anything
    mov [c64_spl], ax
.nolast:
.next:
    mov si, [c64_spa]
    mov di, [c64_spb]
    add si, C64_BSTRIDE
    add di, C64_BSTRIDE
    dec dx
    jnz .row
.done:
    cld
    mov ax, [c64_spl]
    cmp ax, 0
    jl .same
    mov ah, [c64_spf]
    mov al, [c64_spl]
    jmp short .out
.same:
    mov ax, -1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_rowcopy(unsigned char *dst, const unsigned char *src, int n)
;     - n bytes of each of eight pixel rows, stride C64_BSTRIDE, within the
;       package's own segment: how the frame shadow is brought up to date with
;       what was just drawn (C64-SPEC §9.3 step 4).
; -----------------------------------------------------------------------------
_c64_rowcopy:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld
    mov ax, ds
    mov es, ax                      ; ES = DS on purpose (see c64_rowspan)
    mov si, [bp+6]
    mov di, [bp+4]
    mov dx, 8
.row:
    push si
    push di
    mov cx, [bp+8]
    jcxz .skip
    rep movsb                       ; cc8086:allow ES = DS, loaded above and
                                    ; restored below; DF clear
.skip:
    pop di
    pop si
    add si, C64_BSTRIDE
    add di, C64_BSTRIDE
    dec dx
    jnz .row
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned c64_rowsig(unsigned mseg, unsigned moff, const unsigned char *col,
;                     int n)
;
; A 16-bit signature of ONE character row's SOURCES - its matrix bytes and its
; colour nibbles - for the k = 1..24 shift test (C64-SPEC §9.4).
;
; WHY THE SOURCES AND NOT THE PIXELS. The shift test asks "did the whole frame
; move up by k rows?", and it has to ask before anything is composed, because
; composing 25 rows to find out is the cost the scroll exists to avoid. Within
; one frame the character generator and the background are fixed, so two rows
; with the same matrix bytes and the same colours compose to the same pixels.
;
; AND IT IS A HINT, NOT AN ORACLE. A 16-bit signature can collide. Nothing
; rests on it: after the scroll is emitted, the shadow is moved with it and
; every row still goes through c64_rowspan against the moved shadow - a row
; that did not really shift simply compares unequal and is drawn. The scroll
; is an optimisation whose failure mode is a redraw, never a wrong screen.
; -----------------------------------------------------------------------------
_c64_rowsig:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [bp+4]
    mov si, [bp+6]
    mov di, [bp+8]
    mov cx, [bp+10]
    xor ax, ax
    jcxz .out
.l:
    mov bl, [es:si]
    inc si
    mov bh, [di]
    inc di
    xor ax, bx
    rol ax, 1
    loop .l
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_x2init(void) - build c64_x2tab once (see its declaration above).
;
; IT WAS WRONG IN TWO PLACES AND NOTHING COULD SEE IT, because nothing called
; the table until fullscreen 2x shipped (C64-SPEC 9.8) and the host harness
; models the doubler in C rather than running this. It answered a table of
; 256 ZEROS, which on the glass is a fullscreen picture that is entirely
; black - found on the glass and nowhere else, LESSONS.md 7's trap exactly.
; Both bugs are worth naming, because both are the ordinary shape of a
; bit-twiddling loop written from the description:
;
;  - `shl bx, 1` answers BIT 15 in CF, not bit 7. With the byte in the LOW
;    half of BX the carry is zero for all eight passes whatever the byte is,
;    so every entry came out 0. The byte goes into the HIGH half first.
;  - the two result shifts must BOTH happen before the pair is set. Setting
;    the pair between them puts it at bits 1-2 instead of 0-1, so after eight
;    passes the leftmost pair has walked out of the sixteen-bit result.
;
; c64memtest.asm section (6b) now runs it on a real x86 and checks four
; entries plus the identity that every result byte is one of the 16 values a
; doubled nibble can take.
; -----------------------------------------------------------------------------
_c64_x2init:
    push bp
    mov bp, sp
    push si
    push di
    mov di, c64_x2tab
    xor si, si                      ; the byte being doubled, 0..255
.byte:
    mov cx, 8
    mov bx, si
    shl bx, cl                      ; the byte into the HIGH half, so that
                                    ; `shl bx, 1` below answers ITS bit 7 in CF
    xor dx, dx                      ; DH = the high result byte, DL = the low
.bit:
    shl dx, 1
    shl dx, 1                       ; two result bits per source bit, and BOTH
                                    ; shifts before the pair is set
    shl bx, 1
    jnc .zero
    or dl, 3                        ; a set source bit is two set result bits
.zero:
    loop .bit
    ; DX now holds the sixteen doubled bits, bit 15 leftmost
    mov [di], dh
    mov [di+1], dl
    add di, 2
    inc si
    cmp si, 256
    jb .byte
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_band_x2(unsigned char *dst, const unsigned char *src, int rows)
;
; Pixel-double a composed band: `rows` source rows of C64_BSTRIDE bytes become
; 2 * rows rows of C64_X2STRIDE, each source byte becoming two through
; c64_x2tab. This is fullscreen's 2x (C64-SPEC §9.8), where the tier
; table says the machine can afford it.
; -----------------------------------------------------------------------------
_c64_band_x2:
    push bp
    mov bp, sp
    push si
    push di
    mov dx, [bp+8]                  ; rows
    cmp dx, 0
    jle .out
    mov si, [bp+6]
    mov di, [bp+4]
.row:
    push si
    push di
    mov cx, C64_BSTRIDE
.cell:
    mov bl, [si]
    inc si
    xor bh, bh
    shl bx, 1
    mov ax, [c64_x2tab+bx]          ; AL = the high byte, AH = the low
    mov [di], ax                    ; ...written left to right
    add di, 2
    loop .cell
    ; the second scan line is a copy of the first
    pop di
    push di
    mov cx, C64_X2STRIDE
    mov bx, di
    add bx, C64_X2STRIDE
.dup:
    mov al, [di]
    mov [bx], al
    inc di
    inc bx
    loop .dup
    pop di
    pop si
    add si, C64_BSTRIDE
    add di, C64_X2STRIDE * 2
    dec dx
    jnz .row
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_colrow(const unsigned char *m, int n, int sh, const unsigned char *g)
; A SPRITE COLLISION ON ONE LINE (C64-SPEC 5.3.1): N bytes of opaque mask M,
; whose first pixel is SH (0..7) pixels into G's first byte, against G - the
; foreground of the cells under it, or another sprite's mask. Nonzero when a
; set bit of M meets a set bit of G. G is read N + 1 bytes long. Byte i of M
; covers the last 8 - SH pixels of G[i] and the first SH of G[i + 1]: one
; 16-bit shift splits it between the two.
; -----------------------------------------------------------------------------
_c64_colrow:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov dx, [bp+6]
    mov cx, [bp+8]
    mov di, [bp+10]
    xor ax, ax
    test dx, dx
    jle .out
.b:
    mov bh, [si]
    inc si
    xor bl, bl
    shr bx, cl
    and bh, [di]
    and bl, [di+1]
    or bh, bl
    jnz .hit
    inc di
    dec dx
    jnz .b
    jmp short .out
.hit:
    inc ax
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_colrows(const unsigned char *mk, int n, int sh,
;                 const unsigned char *g, int lines, int ye)
; c64_colrow over LINES lines of one sprite in one character row (C64-SPEC
; 5.3.1): line i's mask at MK (N bytes; a data line's mask 7 bytes after the
; last, c64_sprmasks' slots), its foreground G + i at a stride of 8 - a cell's eight lines, as
; c64_xb lays them out. YE bit 0: Y-expanded, the mask advancing every second
; line; bit 1: the first line is its data line's second. Nonzero at the first
; line that meets the foreground. One call a row, where the C loop it replaced
; was most of a scan.
; -----------------------------------------------------------------------------
_c64_colrows:
    push bp
    mov bp, sp
    push si
    push di
    mov cx, [bp+8]
    mov si, [bp+4]
    mov di, [bp+10]
.line:
    cmp word [bp+12], 0
    jle .none
    push si
    push di
    mov dx, [bp+6]
.b:
    mov bh, [si]
    inc si
    xor bl, bl
    shr bx, cl
    and bh, [di]
    and bl, [di+8]
    or bh, bl
    jnz .hit
    add di, 8
    dec dx
    jnz .b
    pop di
    pop si
    inc di                          ; the cells' next pixel line
    dec word [bp+12]
    test byte [bp+14], 1
    jz .adv
    xor byte [bp+14], 2             ; Y-expanded: a data line is two lines
    test byte [bp+14], 2
    jnz .line                       ; ...and this was its first
.adv:
    add si, 7
    jmp short .line
.hit:
    pop di
    pop si
    mov ax, 1
    jmp short .out
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_colblank(unsigned seg, unsigned a, int n, int mask,
;                  const unsigned char *bk)
; Whether the N character codes at SEG:A (each ANDed with MASK) are all
; BLANK by BK's 256 answers (C64-SPEC 5.3.1): 1 all blank, 0 one is not, and
; 2 + i when code i has no answer yet - the caller takes it and asks again
; from i. SEG is the C64's RAM: the codes are read where they are.
; -----------------------------------------------------------------------------
_c64_colblank:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    mov di, [bp+12]
    xor bx, bx
    mov ax, 1
    jcxz .out
.c:
    mov bl, [es:si]
    inc si
    and bl, dl
    cmp byte [di+bx], 1
    jb .unk
    ja .not
    loop .c
    jmp short .out                  ; (AX is still 1)
.not:
    xor ax, ax
    jmp short .out
.unk:
    mov ax, si
    sub ax, [bp+6]
    inc ax                          ; SI was one past code i: 2 + i
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; c64_mline - one sprite data line (3 bytes at SI, SI moved past them) as its
; OPAQUE mask, 7 bytes at DI (C64-SPEC 5.3.1): DL bit 0 multicolour (a pair is
; opaque unless 00), bit 1 X-expanded (each byte two, through c64_mlx's
; doubled words, high byte first). Three or six bytes and zeros to the
; seventh, so another sprite's line can be read against it one byte past its
; end. Clobbers AX BX CX.
; -----------------------------------------------------------------------------
section .bss
c64_mlx:    resw 1                          ; c64_xexp, for c64_mline
section .text
c64_mline:
    push di
    mov cx, 3
.b:
    lodsb
    test dl, 1
    jz .hi
    mov ah, al
    shr ah, 1
    or al, ah
    and al, 0x55
    mov ah, al
    shl ah, 1
    or al, ah                       ; a pair: both pixels, unless 00
.hi:
    test dl, 2
    jnz .xe
    mov [di], al
    inc di
    loop .b
    mov word [di], 0                ; bytes 3-6
    mov word [di+2], 0
    jmp short .e
.xe:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, [c64_mlx]
    mov ax, [bx]
    mov [di], ah                    ; the left half first
    mov [di+1], al
    add di, 2
    loop .b
    mov byte [di], 0                ; byte 6
.e:
    pop di
    ret

; -----------------------------------------------------------------------------
; void c64_sprmasks(const unsigned char *sd, unsigned char *mk, int md,
;                   const unsigned *xexp)
; A sprite's 21 data lines as their opaque masks, c64_mline's 7 bytes apart
; at MK (C64-SPEC 5.3.1); MD as c64_mline's DL.
; -----------------------------------------------------------------------------
_c64_sprmasks:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+10]
    mov [c64_mlx], ax
    mov si, [bp+4]
    mov di, [bp+6]
    mov dl, [bp+8]
    mov dh, 21
.l:
    call c64_mline
    add di, 7
    dec dh
    jnz .l
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_colpair(const unsigned char *sa, const unsigned char *sb, int nb,
;                 int d, int lines, int ye, int md, const unsigned *xexp,
;                 unsigned char *tmp)
; Two sprites over LINES lines (C64-SPEC 5.3.1), from their RAW data: SA and
; SB the data line each starts on, B's first pixel D (0..47) pixels right of
; A's, NB bytes of B that can meet A. YE bit 0 A is Y-expanded and bit 1 its
; first line is a data line's second; bits 2 and 3 the same for B. MD bits
; 0-1 A's c64_mline mode, 2-3 B's. TMP is 16 bytes of scratch. Each line is
; converted as it is reached - a pair usually meets on its first, where the
; sprites' 21 masks each would be most of the scan. Nonzero at the first
; line where an opaque pixel of each meets.
; -----------------------------------------------------------------------------
_c64_colpair:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+18]
    mov [c64_mlx], ax
.line:
    cmp word [bp+12], 0
    jle .none
    mov si, [bp+4]
    mov di, [bp+20]
    mov dl, [bp+16]
    call c64_mline                  ; A's line at TMP
    mov si, [bp+6]
    add di, 8
    mov dl, [bp+16]
    shr dl, 1
    shr dl, 1
    call c64_mline                  ; B's at TMP + 8
    mov si, di
    mov cx, [bp+10]
    mov di, cx
    and cx, 7
    shr di, 1
    shr di, 1
    shr di, 1
    add di, [bp+20]                 ; A from D's byte
    mov dx, [bp+8]
.b:
    mov bh, [si]
    inc si
    xor bl, bl
    shr bx, cl
    and bh, [di]
    and bl, [di+1]
    or bh, bl
    jnz .hit
    inc di
    dec dx
    jg .b
    dec word [bp+12]
    mov ax, [bp+14]
    test al, 1                      ; A Y-expanded:
    jz .anext
    xor al, 2
    test al, 2
    jnz .bstep                      ; ...a first half: the same line again
.anext:
    add word [bp+4], 3
.bstep:
    test al, 4
    jz .bnext
    xor al, 8
    test al, 8
    jnz .stepped
.bnext:
    add word [bp+6], 3
.stepped:
    mov [bp+14], ax
    jmp .line
.hit:
    mov ax, 1
    jmp short .out
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_sprbehind(unsigned char *on, int n, int sh, const unsigned char *g)
; A sprite BEHIND the foreground ($D01B, C64-SPEC 5.3): its N bytes of ON lose
; every pixel the foreground has. ON's first pixel is SH (0..7) pixels into
; G's first byte, and G steps 8 bytes a cell (c64_xb's layout: a cell's eight
; lines). Byte i of ON covers the last 8 - SH pixels of G[8i] and the first
; SH of G[8i + 8]: one 16-bit shift left joins them.
; -----------------------------------------------------------------------------
_c64_sprbehind:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov dx, [bp+6]
    mov cx, [bp+8]
    mov si, [bp+10]
.b:
    mov ah, [si]
    mov al, [si+8]
    shl ax, cl
    not ah
    and [di], ah
    inc di
    add si, 8
    dec dx
    jg .b
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_wmove(void *dst, const void *src, int nw)
; NW words, DS to DS, forward - a scrolled row's shadows (c64_row_slide, 9.4),
; ~150 rows a second under the PCUG demo's scroll, at half os88_memcpy's
; byte-at-a-time cost. Blocks that do not overlap.
; -----------------------------------------------------------------------------
_c64_wmove:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    rep movsw                       ; cc8086:allow ES = DS, loaded above and
                                    ; restored below; DF clear
    pop es
    pop di
    pop si
    pop bp
    ret
