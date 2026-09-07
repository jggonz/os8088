; =============================================================================
; os8088 - apps/apple2/a2band.inc   the 1bpp composers, the span compare and
;                                   the row signature
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
; apps/apple2/ is GPL-2-or-later (apps/apple2/COPYING). What a II+ puts on a
; scan line is reimplemented here from AppleWin's NTSC_CharSet.cpp, MII's
; src/mii_video.c and apple2emu's src/video.cpp - none of which is vendored.
; c64band.inc is the SHAPE and not the source: its cell is eight pixels wide
; and this machine's is seven, which is the whole reason all three of this
; port's composers are new work.
;
; %included by apps/apple2/apple2.asm after the compiled C, by
; hosttest/a2memtest.asm (the SS != DS gate for the string loops) and by
; tests/a2band/a2bandbench.asm (`make a2bandbench`, the icount bench whose
; microseconds the tier table and the cost table are written FROM).
;
; -----------------------------------------------------------------------------
; THE APPLE TEXT CELL IS SEVEN PIXELS (APPLE2-SPEC section 1.2)
; -----------------------------------------------------------------------------
; 280 / 40 = 7. Therefore no cell after the first lands on a byte boundary, a
; decoded glyph is never a pixel byte, there is no per-cell store shape
; anywhere on this machine, and all three composers - text here, lo-res and
; hi-res in wave 3 - are SHIFT-ACCUMULATOR composers of one class.
;
; EIGHT CELLS ARE FIFTY-SIX BITS ARE SEVEN BYTES, EXACTLY, and that is the
; whole of the design: the composer's unit is a GROUP of eight cells, a group
; is byte-aligned in the output at both ends, and the span argument is a group
; span. A one-cell change composes one group - seven output bytes - which is
; the difference between a keystroke costing a millisecond and costing eight.
;
; -----------------------------------------------------------------------------
; THE OUTPUT IS 40 BYTES WIDE AND THE APPLE IS 35 OF THEM
; -----------------------------------------------------------------------------
; The window's content is 336 px: an 8 px border each side around a 320 px
; band, and the Apple's 280 pixels sit inside that band letterboxed 16 px left
; and 24 px right (APPLE2-SPEC section 7.1). 16 px is TWO WHOLE BYTES, which
; is why the split is 16/24 and not 20/20: the composer's output stays byte
; aligned even though its cells are not.
;
; So a band row is A2_BSTRIDE = 40 bytes; bytes 0-1 and 37-39 are the
; letterbox and THIS FILE NEVER WRITES THEM. The band buffer is bss, the
; loader zeroes it, and the letterbox is therefore dark and part of every
; blit and every compare, at no cost and with no second code path.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved. Every `movs`/`cmps` loads DS/ES on
; purpose, restores both, leaves DF clear and carries `cc8086:allow` with its
; reason.
; =============================================================================

section .bss

; The masked glyph bytes of ONE character row, laid out as eight pixel rows of
; forty cells, which phase C then packs seven bits at a time. Interleaving the
; two phases would need the glyph pointer AND the pixel row in addressing
; registers at once, and the 8086 has no [reg + reg + reg]; this way phase B
; writes eight bytes per cell at immediate displacements (the c64_band1 shape)
; and phase C reads them with one pointer.
a2_grow:    resb 8 * 40

; The byte -> two-byte pixel-doubling table a2_band_x2 reads, built ONCE by
; a2_x2init. It is 512 bytes of BSS and not of .data on purpose: bss costs no
; file bytes and the loader zeroes it, while an initialised array is paid for
; twice (SPEC.md 73.9). Each entry is {high 8 doubled bits, low 8}, in that
; order, so one `mov ax,[tab+bx]` / `mov [di],ax` writes them left to right.
a2_x2tab:   resb 512

a2_spf:     resw 1                  ; a2_rowspan's first differing byte...
a2_spl:     resw 1                  ; ...and its last

; a2_band_text's group count, parked across phase B. DX cannot hold it: DH is
; the per-cell XOR mask and DL would be all that was left, which is one byte
; for a count phase C then decrements per row. Four bytes of bss against a
; register the loop does not have.
a2_bgc:     resw 1

section .text

A2_BSTRIDE  equ 40                  ; bytes per pixel row of a composed band -
                                    ; 320 px, the letterboxed Apple raster. An
                                    ; assemble-time constant because
                                    ; PERFORMANCE.md Set 64 says so
A2_GSTRIDE  equ 40                  ; ...and of a2_grow, one byte a cell
A2_X2STRIDE equ 80                  ; ...and of a pixel-doubled band
A2_LBOX     equ 2                   ; the left letterbox, in BYTES: the Apple's
                                    ; pixel 0 is band byte 2
A2_CELLS    equ 40                  ; cells across
A2_GROUPS   equ 5                   ; ...which is five groups of eight

; -----------------------------------------------------------------------------
; void a2_band_text(unsigned char *dst, int g0, int g1,
;                   unsigned mseg, unsigned moff, int fmask)
;
; Compose groups g0..g1 (each eight cells) of ONE character row into an 8-row
; 1bpp band at dst, stride A2_BSTRIDE, in the framebuffer's polarity: a set
; bit is a LIT pixel (SPEC.md 5.4.2).
;
;   mseg:moff   the character row - 40 bytes in the RAM claim, at the row base
;               the interleaved map gives (APPLE2-SPEC section 7.2)
;   fmask       THE FLASH PHASE, as the mask a flashing cell takes right now:
;               0x00 while flashing text shows its normal form, 0x7F while it
;               shows its inverse. One byte, and it is the whole of flashing
;               (section 7.6) - there is no second mechanism
;
; THE PER-CELL XOR MASK IS WHY 64 GLYPHS COVER 128 BITMAPS (section 7.3):
;
;   byte $00-$3F   INVERSE      mask 0x7F
;   byte $40-$7F   FLASHING     mask fmask
;   byte $80-$FF   NORMAL       mask 0x00
;
; and the glyph index is `byte & 0x3F` in every one of the three ranges,
; because the character ROM's blocks $40, $80 and $C0 are byte-identical and
; block $00 is their XOR $7F. tools/a2ref.py --romshape asserts exactly that
; about the pinned ROM, so this is checkable rather than remembered.
;
; _a2_chr is the decoded generator - 64 glyphs of 8 rows - built by the C in
; os88_main and RESIDENT, never in the overlay: a disk with no APPLE2.OVL must
; be a program whose menus refuse, not a window that draws nothing.
; -----------------------------------------------------------------------------
_a2_band_text:
    push bp
    mov bp, sp
    push si
    push di
    push es

    mov cx, [bp+8]                  ; g1
    sub cx, [bp+6]                  ; - g0
    inc cx                          ; = the group count
    cmp cx, 0
    jle .out                        ; an empty span composes nothing
    mov [a2_bgc], cx                ; phase C wants it back
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; ...and CX is the CELL count, groups * 8

    ; --- phase B: one decision per cell, eight unrolled stores --------------
    mov es, [bp+10]                 ; mseg
    mov si, [bp+12]                 ; moff
    mov di, [bp+6]                  ; g0
    shl di, 1
    shl di, 1
    shl di, 1                       ; cells = groups * 8
    add si, di                      ; the source row, at the first cell
    add di, a2_grow                 ; ...and a2_grow, likewise
    mov bl, [bp+14]                 ; fmask, kept in BL across the loop
.cell:
    mov al, [es:si]
    inc si
    cmp al, 0x40
    jb .inv
    cmp al, 0x80
    jae .norm
    mov dh, bl                      ; $40-$7F: the flash phase decides
    jmp short .have
.inv:
    mov dh, 0x7F                    ; $00-$3F: inverse, always
    jmp short .have
.norm:
    xor dh, dh                      ; $80-$FF: normal, always
.have:
    and al, 0x3F                    ; the glyph index, in all three ranges
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; a glyph is eight bytes
    add bx, _a2_chr
    mov al, [bx]
    xor al, dh
    mov [di], al
    mov al, [bx+1]
    xor al, dh
    mov [di+A2_GSTRIDE], al
    mov al, [bx+2]
    xor al, dh
    mov [di+A2_GSTRIDE*2], al
    mov al, [bx+3]
    xor al, dh
    mov [di+A2_GSTRIDE*3], al
    mov al, [bx+4]
    xor al, dh
    mov [di+A2_GSTRIDE*4], al
    mov al, [bx+5]
    xor al, dh
    mov [di+A2_GSTRIDE*5], al
    mov al, [bx+6]
    xor al, dh
    mov [di+A2_GSTRIDE*6], al
    mov al, [bx+7]
    xor al, dh
    mov [di+A2_GSTRIDE*7], al
    pop bx
    inc di
    loop .cell

    ; --- phase C: the shift accumulator, eight cells to seven bytes ---------
    ; out[j] = (g[j] << (j+1)) | (g[j+1] >> (6-j)),  j = 0..6
    ;
    ; and that is exactly the high byte of `(g[j] << 8 | g[j+1] << 1) << (j+1)`,
    ; which is why AL is pre-shifted by one and AH is not: a 7-bit stream and
    ; an 8-bit byte differ by one place, and this is where the difference is
    ; paid. `shl ax, cl` with an immediate CL is 8 + 4k clocks against 7
    ; separate `shl ax, 1` at 2 bytes of prefetch each, which on an 8088 the
    ; prefetch queue decides and not the ALU.
    mov si, [bp+6]                  ; g0
    shl si, 1
    shl si, 1
    shl si, 1
    add si, a2_grow                 ; the first group's glyph bytes
    mov di, [bp+6]
    mov ax, di
    shl di, 1
    shl di, 1
    add di, ax
    add di, ax
    add di, ax                      ; g0 * 7 - the group's output offset
    add di, [bp+4]                  ; + dst
    add di, A2_LBOX                 ; + the left letterbox
    mov bx, 8                       ; the eight pixel rows
.row:
    push si
    push di
    mov dx, [a2_bgc]                ; the group count, fresh for each row
.grp:
    mov al, [si+1]
    shl al, 1
    mov ah, [si]
    mov cl, 1
    shl ax, cl
    mov [di], ah
    mov al, [si+2]
    shl al, 1
    mov ah, [si+1]
    mov cl, 2
    shl ax, cl
    mov [di+1], ah
    mov al, [si+3]
    shl al, 1
    mov ah, [si+2]
    mov cl, 3
    shl ax, cl
    mov [di+2], ah
    mov al, [si+4]
    shl al, 1
    mov ah, [si+3]
    mov cl, 4
    shl ax, cl
    mov [di+3], ah
    mov al, [si+5]
    shl al, 1
    mov ah, [si+4]
    mov cl, 5
    shl ax, cl
    mov [di+4], ah
    mov al, [si+6]
    shl al, 1
    mov ah, [si+5]
    mov cl, 6
    shl ax, cl
    mov [di+5], ah
    mov al, [si+7]
    shl al, 1
    mov ah, [si+6]
    mov cl, 7
    shl ax, cl
    mov [di+6], ah
    add si, 8
    add di, 7
    dec dx
    jnz .grp
    pop di
    pop si
    add si, A2_GSTRIDE
    add di, A2_BSTRIDE
    dec bx
    jnz .row

.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_rowflash(unsigned mseg, unsigned moff, int n)
;
; Does this character row hold a byte in $40-$7F - that is, does it FLASH?
;
; THE ONE THING ON THE GLASS THE DAMAGE MODEL CANNOT SEE (APPLE2-SPEC section
; 7.6). Flashing changes pixels with no memory write, so nothing write-driven
; will ever mark its lines dirty and the `]` cursor would simply never blink.
; The flush therefore keeps one byte per character row saying whether that row
; has anything that flashes, and forces exactly those rows when the phase
; flips. This is what fills that byte, and it is asked once per row COMPOSE
; and never per flush - a row that was not recomposed did not change its
; sources either.
; -----------------------------------------------------------------------------
_a2_rowflash:
    push bp
    mov bp, sp
    push si
    push es
    mov es, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    xor ax, ax
    jcxz .out
.l:
    mov dl, [es:si]
    inc si
    cmp dl, 0x40
    jb .next
    cmp dl, 0x80
    jae .next
    mov ax, 1
    jmp short .out
.next:
    loop .l
.out:
    pop es
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_rowspan(const unsigned char *a, const unsigned char *b, int n)
;
; Compare ONE composed pixel row of n bytes against the frame shadow, both in
; the package's own segment, and answer -1 when they are identical or
; (first << 8) | last - the first and last differing BYTE, relative to a.
;
; This is what makes the shadow the glass and not the model (APPLE2-SPEC
; section 7.7): if the composed line equals the shadow, nothing is drawn,
; whatever the model believed.
;
; ONE PIXEL ROW AND NOT EIGHT, which is where this parts company with
; c64_rowspan. A C64 row is eight pixel rows of one character row and nothing
; else; an Apple frame is 192 scan lines that MIXED can split between two
; renderers, and hi-res has no character row at all. The unit that survives
; all three modes is the scan line.
;
; It is the reason the ES sentinel case exists in hosttest/a2memtest.asm: the
; forward and reverse `repe cmpsb` below load ES on purpose, and DF is set for
; the reverse scan and cleared before the return.
; -----------------------------------------------------------------------------
_a2_rowspan:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, ds
    mov es, ax                      ; ES = DS on purpose: both operands are
                                    ; the package's own memory
    mov si, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]                  ; n
    jcxz .same
    cld
    repe cmpsb                      ; cc8086:allow ES = DS, loaded above and
                                    ; restored below; DF clear
    je .same                        ; the whole line matched
    dec si                          ; SI was one PAST the differing byte
    mov ax, si
    sub ax, [bp+4]                  ; the offset within the line
    mov [a2_spf], ax
    ; ...and the LAST differing byte, from the far end. The count stops at the
    ; byte just found, which is known to differ, so this scan always ends on a
    ; mismatch and never runs off the front.
    mov cx, [bp+8]
    sub cx, ax
    mov si, [bp+4]
    add si, [bp+8]
    dec si
    mov di, [bp+6]
    add di, [bp+8]
    dec di
    std
    repe cmpsb                      ; cc8086:allow ES = DS as above; DF is set
                                    ; here on purpose and cleared on the next
                                    ; line
    cld
    inc si                          ; SI was one BEFORE it, DF being set
    mov ax, si
    sub ax, [bp+4]
    mov [a2_spl], ax
    mov ah, [a2_spf]
    mov al, [a2_spl]
    jmp short .out
.same:
    cld
    mov ax, -1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_rowcopy(unsigned char *dst, const unsigned char *src, int n)
;
; n bytes within the package's own segment: how the frame shadow is brought up
; to date with what was just drawn (APPLE2-SPEC section 7.7 step 4). One line,
; for a2_rowspan's reason.
; -----------------------------------------------------------------------------
_a2_rowcopy:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld
    mov ax, ds
    mov es, ax                      ; ES = DS on purpose (see a2_rowspan)
    mov si, [bp+6]
    mov di, [bp+4]
    mov cx, [bp+8]
    jcxz .out
    rep movsb                       ; cc8086:allow ES = DS, loaded above and
                                    ; restored below; DF clear
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned a2_rowsig(unsigned mseg, unsigned moff, int n)
;
; A 16-bit signature of ONE character row's SOURCES, for the k-row shift test
; (APPLE2-SPEC section 7.7 step 2).
;
; WHY THE SOURCES AND NOT THE PIXELS. The shift test asks "did the whole frame
; move up by k rows?", and it has to ask before anything is composed, because
; composing 24 rows to find out is the cost the scroll exists to avoid. Within
; one frame the character generator and the flash phase are fixed, so two rows
; with the same source bytes compose to the same pixels.
;
; AND IT IS A HINT, NOT AN ORACLE. A 16-bit signature can collide. Nothing
; rests on it: after the scroll is emitted, the shadow is moved with it and
; every line still goes through a2_rowspan against the moved shadow - a row
; that did not really shift simply compares unequal and is drawn. The scroll
; is an optimisation whose failure mode is a redraw, never a wrong screen.
; -----------------------------------------------------------------------------
_a2_rowsig:
    push bp
    mov bp, sp
    push si
    push es
    mov es, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    xor ax, ax
    jcxz .out
.l:
    mov bl, [es:si]
    inc si
    xor al, bl
    rol ax, 1
    loop .l
.out:
    pop es
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_x2init(void) - build a2_x2tab once (see its declaration above).
;
; TWO BUGS LIVE IN THIS LOOP AND THE C64 SHIPPED BOTH (c64band.inc's own
; header), so they are named here rather than rediscovered:
;
;  - `shl bx, 1` answers BIT 15 in CF, not bit 7. With the byte in the LOW
;    half of BX the carry is zero for all eight passes whatever the byte is,
;    so every entry comes out 0 - a fullscreen picture that is entirely black,
;    visible on the glass and nowhere else. The byte goes into the HIGH half
;    first.
;  - the two result shifts must BOTH happen before the pair is set. Setting
;    the pair between them puts it at bits 1-2 instead of 0-1, so after eight
;    passes the leftmost pair has walked out of the sixteen-bit result.
;
; hosttest/a2memtest.asm runs it on a real x86 and checks four entries plus
; the identity that every result byte is one of the sixteen values a doubled
; nibble can take.
; -----------------------------------------------------------------------------
_a2_x2init:
    push bp
    mov bp, sp
    push si
    push di
    mov di, a2_x2tab
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
; void a2_band_x2(unsigned char *dst, const unsigned char *src, int rows)
;
; Pixel-double a composed band: `rows` source rows of A2_BSTRIDE bytes become
; 2 * rows rows of A2_X2STRIDE, each source byte becoming two through
; a2_x2tab. This is fullscreen's 2x, where the tier table says the machine can
; afford it (APPLE2-SPEC section 7.8) - and ONE ROUTINE SERVES BOTH AXES,
; because X-only doubling is the same buffer read at twice the stride.
;
; THE FLUSH DOES NOT CALL IT IN WAVE 1. Fullscreen is 1:1 centred until wave 3
; writes the tier table FROM tests/a2band's measured numbers; the routine and
; its table are here because the bench times them and because the alternative
; is a wave that discovers the doubler is wrong on the glass.
; -----------------------------------------------------------------------------
_a2_band_x2:
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
    mov cx, A2_BSTRIDE
.b:
    mov bl, [si]
    inc si
    xor bh, bh
    shl bx, 1
    mov ax, [a2_x2tab+bx]           ; {high, low} in that order, so one word
    mov [di], ax                    ; store writes them left to right
    add di, 2
    loop .b
    pop di
    pop si
    ; the doubled row is written TWICE, which is what makes the vertical half
    ; free: the caller reads it at A2_X2STRIDE for 2x both axes and at twice
    ; that for X only
    push si
    push di
    mov si, di
    add di, A2_X2STRIDE
    mov cx, A2_X2STRIDE / 2
    push es
    mov ax, ds
    mov es, ax
    cld
    rep movsw                       ; cc8086:allow ES = DS, loaded here on
                                    ; purpose and restored on the next line
    pop es
    pop di
    pop si
    add si, A2_BSTRIDE
    add di, A2_X2STRIDE * 2
    dec dx
    jnz .row
.out:
    pop di
    pop si
    pop bp
    ret
