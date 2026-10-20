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
;   mode        0 = standard text. WAVE 1 COMPOSES STANDARD TEXT ONLY; every
;               other mode fills the span with c64_bgfill and wave 4 writes
;               the bitmap, multicolour and extended-background composers
;               (docs/plans/completed/C64-PORT-PLAN.md wave 4). It is a stated gap, not a
;               silent one: the harness asserts mode 0 against tools/c64ref.py
;               bit for bit and no wave-1 gate claims the others
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
