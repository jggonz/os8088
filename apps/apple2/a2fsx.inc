; =============================================================================
; os8088 - apps/apple2/a2fsx.inc    the foreign-mode raster writers
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md section 13).
; apps/apple2/ is GPL-2-or-later (apps/apple2/COPYING). The artifact-colour
; rule and both colour tables are MII's - src/mii_video.c's `palettes[0]`
; ("Color NTSC", :92-113), `mii_base_clut.lores[0]` (:173-177) and
; `mii_base_clut.hires` (:186-189), with `_mii_line_render_hires`'s
; ten-entry integer rule (:417-450) transcribed into a2_hitab below. Nothing
; of MII's SOURCE is vendored (CONTRIBUTING.md 6): what is carried is the
; behaviour and the tables, and APPLE2-SPEC section 2 names the file.
;
; %included by apps/apple2/apple2.asm after the compiled C, and by
; tests/a2band/a2bandbench.asm, which is where every millisecond in
; APPLE2-SPEC section 7.9's foreign rows comes from.
;
; -----------------------------------------------------------------------------
; WHAT A FOREIGN FRAME IS, AND WHY IT IS NOT A FRAME WRITE
; -----------------------------------------------------------------------------
; SPEC.md 53's bracket hands the app the machine and the video mode. The
; obvious shape - "compose the whole 280x192 raster and write it" - is
; precisely what the dirty-page bitmap, the write window, the scan-line map
; and the span compare exist to avoid (PERFORMANCE.md Part 5): VGA13 at
; 280 x 192 is 53,760 per-pixel decisions, and the bench below prices that at
; SECONDS of a 4.77 MHz 8088.
;
; So THE FOREIGN WRITERS ARE DRIVEN OFF THE SAME DIRTY-LINE SET THE WINDOWED
; FLUSH COMPUTES (section 13.2), against a foreign-frame SHADOW in a heap
; claim taken at the fullscreen LATCH - where a refusal is legal, and where
; the flush's own buffers may never be, because the flush cannot refuse.
;
; -----------------------------------------------------------------------------
; TWO ROUTINES, **ONE** WRITER - AND THE OTHER TWO WERE MEASURED AND CUT
; -----------------------------------------------------------------------------
; THIS FILE HOLDS ONE WRITER AND THIS HEADER USED TO DESCRIBE THREE. It named
; FSXM_CGA640 and FSXM_HERC as things the file does, and neither exists:
; a2_fsx_row has three MODES and not one of them is 1bpp, a2_fsx_avail has one
; arm, and the word "cut" appeared nowhere. It is the file a later author
; opens to add a writer, so a stale list here is worse than a stale list
; anywhere else in the package.
;
;   FSXM_VGA13    a2_fsx_row composes ONE scan line of 280 palette indices
;                 (one byte a pixel, which is the only geometry in this OS
;                 that can express artifact colour at all), and a2_fsx_put
;                 compares it against the shadow and writes what differs.
;                 That is the whole of it.
;
; FSXM_CGA640 AND FSXM_HERC WERE A SPEED CLAIM AND THE BENCH REFUSED IT
; (APPLE2-SPEC section 13.4, tests/a2band/a2bandbench.asm): 695.4 ms a frame
; against the windowed path's 632.6, and 34.8 ms a character row against 26.4.
; Both paths compose with a2_band_text and double with a2_band_x2, byte for
; byte the same, and what differs is the EMIT - one os88_gfx_blit1 of 640x16
; at 8.875 counts against sixteen a2_fsx_put compares at 2.0 each. A band
; going down WHOLE does not need a span compare. So they are cut, Machine >
; `Color NTSC` greys on CGA and Hercules with that number, and colour is a
; statement about a VGA-class machine.
;
; WHAT SURVIVES THE CUT IS a2_fsx_put's SHAPE, and section 15.4's lever 3 with
; it. a2_fsx_put takes a segment, an offset and a byte range and knows nothing
; about the geometry above it, so a 1bpp writer would still be an offset
; arithmetic in the C and no second routine here - the lever is STILL
; AVAILABLE rather than already spent, which is the opposite of what this
; header used to imply.
;
; a2_fsx_put IS THE SPAN COMPARE ONE GEOMETRY ALONG. It answers 0 for a line
; that has not changed - which on an ordinary keystroke is 191 of the 192 -
; and writes only the differing run when it has, to the framebuffer AND to the
; shadow, in one pass over each.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved. Every `movs`/`cmps` loads ES on purpose,
; restores it, leaves DF clear and carries `cc8086:allow` with its reason.
; =============================================================================

section .bss

; MII's artifact rule, FLATTENED (see a2_fsx_init). 128 entries indexed
; `(base << 4) | (parity << 3) | (right << 2) | (pixel << 1) | left`, holding
; the palette index MII's ten-entry integer CLUT would have answered. The
; alternative is MII's own five branches and three variable shifts PER PIXEL,
; 53,760 times a frame.
a2_hitab:   resb 128

a2_fsx_fm:  resw 1              ; the flash phase, parked across phase B
a2_fsx_ln:  resw 1              ; ...and the pixel row inside the character row
a2_fsx_hi:  resw 1              ; ...and whether lo-res wants the HIGH nibble
a2_fsx_b0:  resb 1              ; hi-res needs the byte before and the byte
a2_fsx_b1:  resb 1              ; after: the artifact colour of a pixel is
a2_fsx_b2:  resb 1              ; decided by its NEIGHBOURS
a2_fsx_odd: resb 1              ; ...and by the cell's parity (MII's `odd`)
a2_fsx_end: resw 1              ; the ROW's last source byte (moff + 39), which
                                ; is what says "this cell has no next byte" -
                                ; a CELL RANGE ending short of the row still
                                ; has one, and reading `cx == 1` instead put a
                                ; black column in the middle of the picture
a2_fsx_f:   resw 1              ; a2_fsx_put's first differing byte
a2_fsx_l:   resw 1              ; ...and its last

section .text

A2_FSXW     equ 280             ; the Apple's raster, one byte a pixel

; MII's mii_video_color_mode_e (src/mii_video.c:80-84), used here as the VGA
; palette index itself: a2_fsx_dac writes DAC entries 0..15 in this order, so
; a colour NAME and a DAC slot are the same number and nothing has to map
; between them.
A2_CI_BLACK     equ 0
A2_CI_PURPLE    equ 1
A2_CI_GREEN     equ 2
A2_CI_BLUE      equ 3
A2_CI_ORANGE    equ 4
A2_CI_WHITE     equ 5
A2_CI_MAGENTA   equ 6
A2_CI_DARKBLUE  equ 7
A2_CI_DARKGREEN equ 8
A2_CI_GRAY1     equ 9
A2_CI_GRAY2     equ 10
A2_CI_LIGHTBLUE equ 11
A2_CI_BROWN     equ 12
A2_CI_PINK      equ 13
A2_CI_YELLOW    equ 14
A2_CI_AQUA      equ 15

; -----------------------------------------------------------------------------
; void a2_fsx_row(unsigned char *dst, int mode, unsigned mseg, unsigned moff,
;                 int line, int fmask, int cell0, int ncells)
;
; ONE scan line of palette indices at dst - `ncells` cells of it, starting at
; cell `cell0`, written at dst + cell0 * 7 and nowhere else.
;
;   mode    0 TEXT, 1 LO-RES, 2 HI-RES - a2scr.c's A2_MODE_*, which is what
;           the flush's own per-ROW dispatch already answers
;   mseg    the Apple's RAM claim
;   moff    TEXT/LO-RES: the character row's forty source bytes.
;           HI-RES: the SCAN LINE's own forty, which is the row base plus
;           (line << 10) - the interleave the caller already walks
;   line    TEXT: the pixel row 0..7 inside the character row, which is the
;           glyph row. LO-RES: 0..7, and the low nibble is the top four.
;           HI-RES: unused, because moff already names the line
;   fmask   the flash phase, exactly as a2_band_text takes it
;   cell0   the first cell to compose, 0..39
;   ncells  how many, 1..40-cell0
;
; THE CELL RANGE IS a2_flush's OWN GROUP SPAN ONE GEOMETRY ALONG, and it is
; here for the reason PERFORMANCE rule 1 gives: an Applesoft COUT changes one
; or two cells, and composing all forty of them is 15.39 XT ms a hi-res line
; where 1.5 was owed. What the caller must not do is narrow a HI-RES range to
; exactly the cells that were written: artifact colour is decided by a
; pixel's NEIGHBOURS, so a write to cell k moves pixels in k-1 and k+1, and
; a2_fsx_frame pads the span by one cell each side for that mode. This
; routine composes what it is asked for and takes the neighbours from the
; SOURCE either side of the range, which is what makes a partial compose
; produce the same bytes a whole one would.
;
; THE SEVEN-BIT ORDER IS a2_grow's: bit 6 is the LEFTMOST pixel. That is not a
; coincidence and it is worth stating - the 1bpp composers next door pack the
; same seven-bit values with a2_pack, and a foreign frame that reversed them
; would draw every glyph mirrored while every windowed screendump stayed
; right.
; -----------------------------------------------------------------------------
_a2_fsx_row:
    push bp
    mov bp, sp
    push si
    push di
    push es

    mov ax, [bp+14]
    mov [a2_fsx_fm], ax
    mov ax, [bp+12]
    mov [a2_fsx_ln], ax
    mov word [a2_fsx_hi], 0
    cmp ax, 4
    jb .lowhalf
    mov word [a2_fsx_hi], 1
.lowhalf:
    mov es, [bp+8]                  ; cc8086:allow ES is the RAM claim, loaded
                                    ; here and restored below; DS stays ours,
                                    ; which is where dst is
    mov si, [bp+10]
    mov ax, si
    add ax, A2_FSXW / 7 - 1         ; the ROW's last source byte, before the
    mov [a2_fsx_end], ax            ; range moves si off it
    mov cx, [bp+18]                 ; ncells
    mov bx, [bp+16]                 ; cell0
    add si, bx                      ; ...so the source starts at the range
    mov ax, bx                      ; di = dst + cell0 * 7, and a cell is
    shl ax, 1                       ; seven bytes: 8c - c, because this is an
    shl ax, 1                       ; 8086 and `imul` by a constant is not
    shl ax, 1                       ; free
    sub ax, bx
    add ax, [bp+4]
    mov di, ax
    mov ax, [bp+6]
    cmp ax, 1
    je .lores
    cmp ax, 2
    je .hires
    ; fall through to TEXT

; --- TEXT: the masked glyph row, expanded a bit to a byte --------------------
; The per-cell XOR mask is a2_band_text's, byte for byte: $00-$3F inverse,
; $40-$7F the flash phase, $80-$FF normal, and the glyph index is `byte & 0x3F`
; in all three ranges (section 7.3).
%macro A2FSX_TPX 1
    shl al, 1
    mov bl, A2_CI_BLACK
    jnc %%dark
    mov bl, A2_CI_WHITE
%%dark:
    mov [di+%1], bl
%endmacro

.text:
.tcell:
    mov al, [es:si]
    inc si
    cmp al, 0x40
    jb .inv
    cmp al, 0x80
    jae .norm
    mov dh, [a2_fsx_fm]
    jmp short .have
.inv:
    mov dh, 0x7F
    jmp short .have
.norm:
    xor dh, dh
.have:
    and al, 0x3F
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; a glyph is eight bytes
    add bx, [a2_fsx_ln]
    mov al, [_a2_chr+bx]
    xor al, dh
    shl al, 1                       ; bit 6 -> bit 7, so seven `shl` carries
                                    ; walk the cell left to right
    A2FSX_TPX 0
    A2FSX_TPX 1
    A2FSX_TPX 2
    A2FSX_TPX 3
    A2FSX_TPX 4
    A2FSX_TPX 5
    A2FSX_TPX 6
    add di, 7
    dec cx                          ; ...and not `loop`: the unrolled seven
    jnz .tcell                      ; stores put the top of this loop more
                                    ; than 128 bytes back
    jmp .out

; --- LO-RES: two colour nibbles a byte, four scan lines each -----------------
; MII's `clut.lores[0]` (src/mii_video.c:173-177) is a2_locol below, and it is
; the table the reference actually DISPLAYS. The windowed path's a2_lopat is
; the same sixteen colours through the LUMINANCE ladder, which is what a 1bpp
; screen can say about them; this is what a byte-per-pixel frame can.
.lores:
.lcell:
    mov al, [es:si]
    inc si
    cmp word [a2_fsx_hi], 0
    je .low
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.low:
    and al, 0x0F
    mov bl, al
    xor bh, bh
    mov al, [a2_locol+bx]
    mov [di], al
    mov [di+1], al
    mov [di+2], al
    mov [di+3], al
    mov [di+4], al
    mov [di+5], al
    mov [di+6], al
    add di, 7
    loop .lcell
    jmp .out

; --- HI-RES: MII's artifact rule, through the flattened table ----------------
; `_mii_line_render_hires` (src/mii_video.c:417-450) builds an eleven-bit
; window - the two pixels before, this cell's seven, the two after - and for
; each pixel asks five questions of (left, pixel, right), the cell's parity,
; the pixel's own parity and BIT 7 OF THE CELL, which is the half-dot shift
; and which MII carries as `offset = (b1 & 0x80) >> 5`, a colour-set select
; into a TEN-ENTRY integer CLUT.
;
; **THE HALF-DOT SHIFT IS HONOURED AS MII HONOURS IT, AND THIS IS THE PLACE TO
; SAY SO.** On the hardware bit 7 delays the cell's seven pixels by half a dot
; clock; a 280-pixel frame has no half pixel, and neither does MII's own
; 560-wide renderer, which expresses the same fact as the colour-set select
; above. What a byte-per-pixel frame CAN express, and what a 1bpp windowed
; band cannot at any width, is the colour - so bit 7 reaches the glass here as
; blue and orange where a cleared bit 7 gives purple and green, which is what
; the reader sees on a real machine.
;
; ...AND THE FIVE BRANCHES ARE A TABLE (a2_fsx_init). Per pixel MII does three
; variable shifts and up to five compares; 280 x 192 is 53,760 of them a
; frame, and the bench in APPLE2-SPEC 7.9 is what says that matters.
%macro A2FSX_HPX 0
    mov bl, dl
    and bl, 7                       ; right:pixel:left, already aligned
    or bl, ah                       ; | parity << 3 | base << 4
    xor bh, bh
    mov al, [a2_hitab+bx]
    mov [di], al
    inc di
    shr dx, 1
    xor ah, 8                       ; the pixel's own parity, MII's `i & 1`
%endmacro

.hires:
    ; THE THREE PIECES OF CROSS-CELL STATE, SEEDED FROM cell0 RATHER THAN FROM
    ; ZERO - which is the whole of what makes a partial hi-res compose produce
    ; the bytes a whole one would. b0 is the byte BEFORE the range (MII's
    ; "last columns are clear, don't wrap around" at cell 0, and the real
    ; neighbour anywhere else), and `odd` is the cell's parity, which is
    ; cell0's own low bit.
    mov byte [a2_fsx_b0], 0
    mov byte [a2_fsx_odd], 0
    cmp bx, 0                       ; bx is still cell0
    je .hseeded
    mov al, [es:si-1]
    mov [a2_fsx_b0], al
    test bl, 1
    jz .hseeded
    mov byte [a2_fsx_odd], 2
.hseeded:
    mov al, [es:si]
    mov [a2_fsx_b1], al
.hcell:
    xor al, al
    cmp si, [a2_fsx_end]
    je .hlast                        ; the fortieth cell of the ROW has no
                                     ; next byte - and a range that ends short
                                     ; of it DOES
    mov al, [es:si+1]
.hlast:
    mov [a2_fsx_b2], al
    ; run = ((b0 & 0x60) >> 5) | ((b1 & 0x7F) << 2) | ((b2 & 0x03) << 9)
    mov dl, [a2_fsx_b1]
    and dl, 0x7F
    xor dh, dh
    shl dx, 1
    shl dx, 1
    mov al, [a2_fsx_b0]
    and al, 0x60
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xor ah, ah
    or dx, ax
    mov al, [a2_fsx_b2]
    and al, 0x03
    mov ah, al
    xor al, al                      ; << 8...
    shl ax, 1                       ; ...and << 9
    or dx, ax
    ; base = ((b1 & 0x80) >> 5) + odd, and the table wants it in bits 4..6
    mov al, [a2_fsx_b1]
    and al, 0x80
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1                       ; 0 or 4 - MII's colour-set select
    add al, [a2_fsx_odd]            ; + 0 or 2 - MII's `odd`
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1                       ; base << 4
    mov ah, al                      ; ...and the parity starts at 0
    shr dx, 1                       ; bit 0 = left, 1 = pixel, 2 = right
    A2FSX_HPX
    A2FSX_HPX
    A2FSX_HPX
    A2FSX_HPX
    A2FSX_HPX
    A2FSX_HPX
    A2FSX_HPX
    mov al, [a2_fsx_b1]
    mov [a2_fsx_b0], al
    mov al, [a2_fsx_b2]
    mov [a2_fsx_b1], al
    inc si
    xor byte [a2_fsx_odd], 2
    dec cx
    jnz .hcell

; ...AND THE THREE PHASE Bs ARE NASM-LOCAL LABELS OF THIS ROUTINE, not names
; of their own. `tests/unit/t_stkbalance.py` walks apps/ from every GLOBAL
; label as if it were an entry, so `a2_fsx_text:` - which is reached by
; falling through the prologue's three pushes - read as a routine whose `ret`
; returns at depth -4. It is not deliberate surgery and it does not want a
; STKBALANCE-OK: it is one routine with three phase Bs in it, and the labels
; now say so.
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_fsx_zero(unsigned seg, unsigned off, unsigned n) - n bytes of ZERO.
;
; THE SHADOW'S CONTENTS ARE NOT PROMISED, AND a2_fsx_put IS WHY THAT MATTERS.
; The heap gives the 53KB claim back with whatever was in it - and on a second
; entry into the mode that is very often the LAST session's shadow, because a
; free followed by a claim of the same size lands on the same block. The
; compare below then answers "nothing moved" for every line the picture still
; agrees with, writes NOTHING, and leaves those lines as the mode set's own
; clear left them: BLACK. Seen on the glass, entering Machine > Color NTSC a
; second time on an unchanged hi-res screen drew three of its six lines and a
; truncated pair of verticals.
;
; `a2_fsx_ok = 0` cannot fix it: that defeats the per-LINE skip, and the defect
; is in the per-BYTE compare one level down. What fixes it is making the
; shadow TRUE rather than unknown - SPEC.md 53.4 is binding that `fsx_mode`
; clears the screen, so a zeroed shadow describes the glass exactly, and every
; compare from the first frame on is sound. It is also the cheaper arm: the
; black parts of the picture are already black and are not written.
; -----------------------------------------------------------------------------
_a2_fsx_zero:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [bp+4]                  ; cc8086:allow ES is the shadow claim,
    mov di, [bp+6]                  ; loaded here on purpose and restored below
    mov cx, [bp+8]
    xor al, al
    jcxz .out
    rep stosb
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_fsx_put(unsigned fbseg, unsigned fboff, unsigned shseg, unsigned shoff,
;                const unsigned char *src, int n)
;
; THE SPAN COMPARE, ONE GEOMETRY ALONG. Compare n bytes of src (ours) against
; the foreign-frame shadow at shseg:shoff; if they are identical answer 0 and
; write nothing at all; otherwise write the differing RUN - first differing
; byte to last, inclusive - to the framebuffer at fbseg:fboff and to the
; shadow, and answer 1.
;
; It is ONE routine for all three writers (see this file's header): what makes
; a CGA640 frame different from a Hercules one is the caller's fboff, and what
; makes a VGA13 frame different from either is that its source is a2_fsx_row's
; 280 palette bytes rather than 70 bytes of doubled band.
;
; THE ANSWER IS WHY THE FOREIGN PATH IS AFFORDABLE AT ALL. A keystroke changes
; one character row, so 184 of the 192 scan lines answer 0 here and cost one
; `repe cmpsb` each; the eight that changed write the columns that moved and
; not the row.
;
; ES is loaded three times and put back; DF is left clear (the backward scan
; below sets it and clears it inside its own window).
; -----------------------------------------------------------------------------
_a2_fsx_put:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld

    mov cx, [bp+14]                 ; n
    mov ax, 0
    jcxz .out
    mov es, [bp+8]                  ; cc8086:allow ES is the shadow claim
    mov si, [bp+12]                 ; src, ours, DS-relative
    mov di, [bp+10]                 ; shoff
    repe cmpsb                      ; cc8086:allow the compare IS the routine
    je .out                         ; not one byte moved: nothing is written
    dec si                          ; ...both pointers stepped past the pair
    mov [a2_fsx_f], si              ; that differed
    ; --- and the LAST differing byte, from the far end -------------------
    mov bx, si
    sub bx, [bp+12]                 ; f, as an index
    mov ax, [bp+14]
    dec ax                          ; n - 1
    mov si, [bp+12]
    add si, ax
    mov di, [bp+10]
    add di, ax
    mov cx, [bp+14]
    sub cx, bx                      ; n - f bytes are still in question
    std
    repe cmpsb                      ; cc8086:allow ...and it stops on the LAST
    cld                             ; differing pair, DF put back at once
    inc si
    mov [a2_fsx_l], si
    ; --- the run, to the framebuffer and to the shadow -------------------
    mov cx, si
    sub cx, [a2_fsx_f]
    inc cx                          ; the run length, inclusive
    mov si, [a2_fsx_f]
    mov di, [bp+6]
    add di, bx                      ; fboff + f
    mov es, [bp+4]                  ; cc8086:allow ES is the FRAMEBUFFER, and
    push cx                         ; this is the whole point of the routine
    push bx
    rep movsb
    pop bx
    pop cx
    mov si, [a2_fsx_f]
    mov di, [bp+10]
    add di, bx                      ; shoff + f
    mov es, [bp+8]                  ; cc8086:allow ...and the shadow follows
    rep movsb
    mov ax, 1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_fsx_init(void) - build a2_hitab, ONCE.
;
; MII's rule (src/mii_video.c:428-445), for every (base, parity, left, pixel,
; right):
;
;     if (pixel)  idx = (left || right) ? 9 : base + parity + 1;
;     else        idx = (left && right) ? base + 2 - parity : 0;
;
; and the entry is a2_hicol[idx], which is MII's own ten-entry `hires` CLUT.
; base is `offset + odd` and is 0, 2, 4 or 6.
;
; IT IS BUILT AT THE FULLSCREEN LATCH AND NOT IN os88_main, which is the one
; place this port's "the display path is resident" rule (section 7.3) does not
; apply: nothing outside the foreign mode reads this table, so a machine that
; never enters colour never spends the 128 iterations.
; -----------------------------------------------------------------------------
_a2_fsx_init:
    push bx
    push cx
    push dx
    push di
    xor di, di                      ; the table index, 0..127
.e:
    mov bx, di
    and bx, 7                       ; right:pixel:left
    xor dx, dx                      ; idx = black
    test bl, 2                      ; pixel?
    jz .dark
    mov al, bl
    and al, 5                       ; left | right
    jz .colour
    mov dx, 9                       ; a lit pixel between two lit ones is
    jmp short .have                 ; WHITE, whatever the colour set
.colour:
    ; the index carries base at bits 4..6 and parity at bit 3, so >> 4 is base
    ; (0, 2, 4 or 6) exactly and bit 3 is the parity on its own
    mov dx, di
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1                       ; DX = base, 0, 2, 4 or 6
    mov cx, di
    and cx, 8
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; CX = parity, 0 or 1
    add dx, cx
    inc dx                          ; base + parity + 1
    jmp short .have
.dark:
    mov al, bl
    and al, 5
    cmp al, 5                       ; left AND right?
    jne .have                       ; no: black, and DX is already 0
    mov dx, di
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1                       ; base
    mov cx, di
    and cx, 8
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; parity
    add dx, 2
    sub dx, cx                      ; base + 2 - parity
.have:
    mov bx, dx
    mov al, [a2_hicol+bx]
    mov bx, di
    mov [a2_hitab+bx], al
    inc di
    cmp di, 128
    jb .e
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; int a2_fsx_key(void) - THE BRACKET'S WHOLE INPUT PATH.
;
; -1 when nothing is waiting, else (scan << 8) | ascii - int 16h's own AX.
;
; IT IS ASSEMBLY BECAUSE THERE IS NO int 16h IN THE C SDK, and there is none
; because a package normally has no business polling the keyboard: keys arrive
; as W_ONKEY events. A bracket is the one place that is not true - SPEC.md
; 53.1 dispatches NO events while it is up, and 53.7 makes the polled int 16h
; legal precisely because the bracket runs on the UI task, which is the one
; context allowed it (SPEC.md 7).
;
; AH=01h is the PEEK and leaves the key in the buffer; ZF=1 means empty. Only
; then is AH=00h issued, which is what makes this non-blocking - AH=00h on an
; empty buffer waits for ever, inside a bracket that has parked every other
; task, which is a machine that has hung.
; -----------------------------------------------------------------------------
_a2_fsx_key:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ah, 0x01
    int 0x16                        ; cc8086:allow int 16h is legal HERE and
    jz .none                        ; only here (SPEC.md 53.7)
    xor ah, ah
    int 0x16                        ; cc8086:allow ...and it cannot block: the
    jmp short .out                  ; peek above said a key is waiting
.none:
    mov ax, 0xFFFF
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; void a2_fsx_dac(void) - MII's "Color NTSC" palette into DAC entries 0..15.
;
; Legal only inside the bracket and only after a mode set: SPEC.md 53.7 says
; the video hardware is the app's while a foreign mode is up, the DAC
; explicitly, and the exit mode set reprograms everything it touches. Sixteen
; entries and not 256 - the frame only ever holds indices 0..15.
; -----------------------------------------------------------------------------
_a2_fsx_dac:
    push ax
    push cx
    push dx
    push si
    mov dx, 0x03C8
    xor al, al
    out dx, al                      ; start at DAC entry 0
    inc dx                          ; 0x03C9, the data port
    mov si, a2_pal48
    mov cx, 48
.b:
    mov al, [si]
    inc si
    out dx, al
    loop .b
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; the tables
; =============================================================================

; MII's mii_base_clut.lores[0] (src/mii_video.c:173-177) - the 40-column
; mapping, which is the only one a II+ has.
a2_locol:
    db A2_CI_BLACK, A2_CI_MAGENTA, A2_CI_DARKBLUE, A2_CI_PURPLE
    db A2_CI_DARKGREEN, A2_CI_GRAY1, A2_CI_BLUE, A2_CI_LIGHTBLUE
    db A2_CI_BROWN, A2_CI_ORANGE, A2_CI_GRAY2, A2_CI_PINK
    db A2_CI_GREEN, A2_CI_YELLOW, A2_CI_AQUA, A2_CI_WHITE

; ...and mii_base_clut.hires (:186-189) - TEN entries, and the shape of it is
; the whole of artifact colour: 1/2 and 4/8 are the same two colours reached
; from either parity, and 9 is white.
a2_hicol:
    db A2_CI_BLACK, A2_CI_PURPLE, A2_CI_GREEN, A2_CI_GREEN, A2_CI_PURPLE
    db A2_CI_BLUE, A2_CI_ORANGE, A2_CI_ORANGE, A2_CI_BLUE, A2_CI_WHITE

; MII's palettes[0], "Color NTSC" (:92-113), as SIX-BIT DAC values - the VGA's
; DAC is six bits a channel, so each of MII's 8-bit components is >> 2.
a2_pal48:
    db  0,  0,  0                   ; BLACK      000000
    db 63, 17, 63                   ; PURPLE     ff44fd
    db  5, 61, 15                   ; GREEN      14f53c
    db  5, 51, 63                   ; BLUE       14cffd
    db 63, 26, 15                   ; ORANGE     ff6a3c
    db 63, 63, 63                   ; WHITE      ffffff
    db 56,  7, 24                   ; MAGENTA    e31e60
    db 24, 19, 47                   ; DARKBLUE   604ebd
    db  0, 40, 24                   ; DARKGREEN  00a360
    db 39, 39, 39                   ; GRAY1      9c9c9c
    db 39, 39, 39                   ; GRAY2      9c9c9c
    db 52, 48, 63                   ; LIGHTBLUE  d0c3ff
    db 24, 28,  0                   ; BROWN      607203
    db 63, 40, 52                   ; PINK       ffa0d0
    db 52, 55, 35                   ; YELLOW     d0dd8d
    db 28, 63, 52                   ; AQUA       72ffd0
