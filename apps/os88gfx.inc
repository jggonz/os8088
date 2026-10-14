; =============================================================================
; os8088 - apps/os88gfx.inc
;
; THE EMBEDDABLE GRAPHICS LIBRARY (docs/plans/completed/GFX-EMBEDDABLE-PLAN.md): drawing
; code that lives in the PACKAGE rather than in the kernel, taken capability by
; capability, so a program pays for what it draws and a machine that runs none
; of those programs pays nothing at all.
;
; The premise is the owner's: duplicate code only used by apps that monopolise
; the machine anyway, instead of permanently spending kernel RAM on them. Two
; measured facts turn that from a size trade into a straight win - an app-side
; rasteriser is FASTER than the slot (24.6 us a pixel against gfx_line's 31.6
; with the arrival removed), and kern_small has never had gfx_line_fast at all
; (SPEC.md 5.6.4.1 is %ifdef KERN_BIG), so in a library the floor machine can
; BUY a capability the kernel never gave it.
;
; --- THE LATTICE RULE, as set -----------------------------------------------
;
;   wanting one capability should not drag in unused others. So walk drags in
;   "how to draw a line", but "how to draw a line" does not drag in the walk.
;
; So every capability is a %define before the %include, an implication is a
; %define at the top of this file (the way os88ui.inc's OS88UI_BARONLY already
; defines OS88UI_NOBTN), and an arm nobody asked for emits NOTHING.
;
;   %define GFXE_BAND_W   128            ; a multiple of 8
;   %define GFXE_BAND_H   128
;   %define GFXE_BAND_BUF my_mask        ; GFXE_BAND_SZ bytes, yours
;   %define GFXE_LINE                    ; implies GFXE_BAND
;   %include "os88gfx.inc"
;
; --- WHERE IT GOES ----------------------------------------------------------
; AT THE END OF YOUR CODE, just before OS88_BSS - os88ui.inc's rule and for
; os88ui.inc's reason: the header and any OS88_ICON16 block are at FIXED
; OFFSETS in the image (SPEC.md 20.2), and code emitted between them fails the
; icon macro's own offset assertion.
;
; This file's own state is a handful of words IN THE IMAGE, not in bss. A
; package image is copied into the instance's own region and is writable, so
; an image word is per-instance exactly as a bss word is; what it saves is the
; caller having to find offsets for it. The BAND ITSELF is the caller's,
; because only the caller knows how big it can afford to be and only the
; caller can put it in bss.
;
; --- CAPABILITIES -----------------------------------------------------------
;
;   GFXE_BAND     compose into your own 1bpp band and commit it with one
;                 OSAPI_GFX_BLIT1. The PIXEL primitive lives here - a bit-set,
;                 not a slot - and so does the running-bounds bookkeeping that
;                 turns a figure into the smallest band that covers it.
;   GFXE_LINE     Bresenham, whole line, into the band. Implies GFXE_BAND.
;   GFXE_POINTS   the other sink: a point array of your own, committed with
;                 one OSAPI_GFX_POINTS (SPEC.md 5.6.9). For a SPARSE figure -
;                 a few pixels scattered over a large area - where a band
;                 would be mostly paper.
;   GFXE_TEXT     the kernel's 8x8 glyphs INTO the band at any pixel x -
;                 normal or inverse, x1/x2/x3, optionally bold - and a
;                 sub-rectangle fill; rows clipped to the band, so a card can
;                 be composed a slab at a time (SPEC.md 5.12.8). Implies
;                 GFXE_BAND.
;   GFXE_BITS     a 16-wide bit record ANDed or ORed into the band at any x:
;                 a tick, a bell, a close box's x (SPEC.md 5.12.8). Implies
;                 GFXE_BAND.
;   GFXE_WALK     SPEC.md 5.6.7's RESUMABLE walk, in your image: "draw the
;                 next N pixels of this line", state in a block you own, so
;                 the erase replays exactly what the draw laid down. Implies
;                 GFXE_POINTS, because that is what it plots through.
;
; Later waves add GFXE_LINE_FAST (SPEC.md 5.6.4.1's interval walk, 4.9x) and
; GFXE_WIDE (5.6.5's dilation). The plan's 4.1 is the table and 4.1.1 the
; sub-dial inside GFXE_LINE_FAST, which is already priced against what
; dropping each piece costs in speed.
;
; --- THE INK POLARITY, and where the opposite one is (and is not) ----------
; A band is composed on LIT paper and inked by CLEARING bits: 1 = a lit pixel
; is OSAPI_GFX_BLIT1's own convention and SPEC.md 42.23 measured it the right
; way round to store (it agrees with the BMP palette AND takes rep stosw's
; 12.5 clocks a byte for the ground instead of a complementing loop's 17 - the
; intuitive polarity costs 36% of every clear for ever).
;
; THE OPPOSITE POLARITY - ink that SETS bits, white on a black ground - is NOT
; in the PLOT (gfxe_bpix, gfxe_line), because an untested arm in a shared
; library is worse than an absent one. GFXE_TEXT and GFXE_BITS DO carry it
; (GFXE_INV, SPEC.md 5.12.8), and it is tested there. What the plot would
; need is written down rather than guessed at: BL walks a ONE instead of a
; zero (`mov bl, 080h` in place of `07Fh`), the combine is `or` instead of
; `and`, and the two wrap branches invert sense because the carry out of the
; rotate is then the bit that just left. gfxe_bclear already takes the paper
; word in AX, so THAT half needs nothing.
;
; --- REGISTERS --------------------------------------------------------------
; Every routine documents what it clobbers rather than preserving everything:
; gfxe_line is an inner loop and a pair of pushes a line is a real cost on the
; machine this targets. Nothing here touches a segment register except
; gfxe_bput, which restores it.
; =============================================================================

%ifdef GFXE_LINE
  %define GFXE_BAND                     ; the lattice, downward only: a line
%endif                                  ; needs somewhere to land, a band does
                                        ; not need a line
%ifdef GFXE_TEXT
  %define GFXE_BAND                     ; glyphs land in the band (SPEC.md
%endif                                  ; 5.12.8)...
%ifdef GFXE_BITS
  %define GFXE_BAND                     ; ...and so do bit records
%endif
%ifdef GFXE_WALK
  %define GFXE_POINTS                   ; ...and a walk needs somewhere too.
%endif                                  ; A BAND is the wrong one for it: two
                                        ; or three pixels scattered over eight
                                        ; places is a band that is almost all
                                        ; paper (docs/plans/completed/GFX-EMBEDDABLE-PLAN.md 8.1.2)

%ifdef GFXE_BAND

  %ifndef GFXE_BAND_W
    %error "os88gfx.inc: GFXE_BAND wants GFXE_BAND_W (a multiple of 8)"
  %endif
  %ifndef GFXE_BAND_H
    %error "os88gfx.inc: GFXE_BAND wants GFXE_BAND_H"
  %endif
  %ifndef GFXE_BAND_BUF
    %error "os88gfx.inc: GFXE_BAND wants GFXE_BAND_BUF - GFXE_BAND_SZ bytes"
  %endif
  %if (GFXE_BAND_W & 7) != 0
    %error "os88gfx.inc: GFXE_BAND_W must be a multiple of 8"
  %endif

%assign GFXE_BAND_ST GFXE_BAND_W / 8    ; the stride, in bytes a row
%assign GFXE_BAND_SZ GFXE_BAND_ST * GFXE_BAND_H

; The row step. A power-of-two stride is a shift and anything else is a mul -
; decided HERE, at assembly time, so a caller who sizes a band conveniently
; gets the cheap arm and one who does not still gets a correct one. It is once
; a line, not once a pixel, so even the mul is noise against the walk.
%assign GFXE_BAND_SH 0
%assign gfxe_t GFXE_BAND_ST
%rep 16
  %if gfxe_t > 1
    %assign gfxe_t gfxe_t / 2
    %assign GFXE_BAND_SH GFXE_BAND_SH + 1
  %endif
%endrep
%if (1 << GFXE_BAND_SH) == GFXE_BAND_ST
  %define GFXE_BAND_POW2
%endif

; AX = y -> AX = the row's byte offset; clobbers CL (and DX, BX on the mul arm)
%macro GFXE_ROWOFF 0
  %ifdef GFXE_BAND_POW2
    mov cl, GFXE_BAND_SH
    shl ax, cl
  %else
    mov bx, GFXE_BAND_ST
    mul bx
  %endif
%endmacro

; -----------------------------------------------------------------------------
; gfxe_bnew - start a band: seed the running bounds from one point
; in:  AX = x, DX = y (in the caller's own drawing space, whatever that is)
; out: nothing; clobbers nothing
;
; The bounds are SIGNED throughout (gfxe_bfold's compares are jge/jle, not
; jae/jbe): a figure can put a vertex off the left of its area for a frame,
; and an unsigned minimum takes -3 for 65533 and sizes the band to the screen.
; -----------------------------------------------------------------------------
gfxe_bnew:
    mov [gfxe_mnx], ax
    mov [gfxe_mxx], ax
    mov [gfxe_mny], dx
    mov [gfxe_mxy], dx
    ret

; -----------------------------------------------------------------------------
; gfxe_bfold - widen the running bounds to include a whole vertex array
; in:  SI = the x array, DI = the y array, CX = how many (>= 1)
; out: nothing; clobbers AX, BX, CX, flags
;
; Call it once per FIGURE, and once more for the figure already on the glass:
; a band erases by COVERING, so anything still down has to be inside it.
; -----------------------------------------------------------------------------
gfxe_bfold:
    xor bx, bx
.v:
    mov ax, [si + bx]
    cmp ax, [gfxe_mnx]
    jge .nx
    mov [gfxe_mnx], ax
.nx:
    cmp ax, [gfxe_mxx]
    jle .xx
    mov [gfxe_mxx], ax
.xx:
    mov ax, [di + bx]
    cmp ax, [gfxe_mny]
    jge .ny
    mov [gfxe_mny], ax
.ny:
    cmp ax, [gfxe_mxy]
    jle .xy
    mov [gfxe_mxy], ax
.xy:
    add bx, 2
    dec cx
    jnz .v
    ret

; -----------------------------------------------------------------------------
; gfxe_bsize - close the bounds into a band the buffer can hold
; in:  the running bounds
; out: CF = 1 the band does not fit - draw this frame some other way;
;      CF = 0 and [gfxe_bx0]/[gfxe_by0]/[gfxe_bw]/[gfxe_bh] are the band
;      clobbers AX, flags
;
; One pixel of margin all round, then x0 and the width onto the BYTE GRID that
; OSAPI_GFX_BLIT1's fast path insists on (SPEC.md 5.4.2.5 takes any width, but
; x must be a multiple of 8). y needs no grid.
;
; REFUSING IS A NORMAL PATH and it is why this is separate from gfxe_bput: the
; caller has to find out that the band will not fit BEFORE it has spent a blit
; on it, and refusing is a lot cheaper than clipping.
; -----------------------------------------------------------------------------
gfxe_bsize:
    mov ax, [gfxe_mnx]
    dec ax
    and ax, 0FFF8h
    mov [gfxe_bx0], ax
    mov ax, [gfxe_mxx]
    add ax, 8                       ; +1 of margin, then rounded UP to the byte
    and ax, 0FFF8h
    sub ax, [gfxe_bx0]
    cmp ax, GFXE_BAND_W
    ja .no
    mov [gfxe_bw], ax
    mov ax, [gfxe_mny]
    dec ax
    mov [gfxe_by0], ax
    mov ax, [gfxe_mxy]
    add ax, 2                       ; +1 of margin, +1 to make it a COUNT
    sub ax, [gfxe_by0]
    cmp ax, GFXE_BAND_H
    ja .no
    mov [gfxe_bh], ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; gfxe_bclear - paper the band's rows
; in:  AX = the paper word (0FFFFh = lit, the usual; 0 = a black ground)
; out: nothing; clobbers AX, CX, DI, ES, flags (and DX, BX on the mul arms)
;
; Only the rows this band HAS. The buffer is sized for GFXE_BAND_H and a real
; frame rarely wants them all, so clearing the lot would be most of the cost
; of a small band.
; -----------------------------------------------------------------------------
gfxe_bclear:
    push es
    push ax
    push ds
    pop es
    mov di, GFXE_BAND_BUF
    mov ax, [gfxe_bh]
%if (GFXE_BAND_ST & 1) != 0
    mov bx, GFXE_BAND_ST            ; an ODD stride is counted in BYTES: ST/2
    mul bx                          ; would drop a byte a row and leave the
                                    ; band's tail unpapered, and ST = 1 (W = 8)
                                    ; would make the shift below a -1, which an
                                    ; 8086 does not mask - AX = 0 and nothing
                                    ; papered at all. Decided here, at assembly
                                    ; time, the way GFXE_ROWOFF decides the
                                    ; row step; the multiple-of-8 contract
                                    ; above stays what it says
%elifdef GFXE_BAND_POW2
    mov cl, GFXE_BAND_SH - 1        ; bh * stride bytes = bh * stride/2 words
    shl ax, cl
%else
    mov cx, GFXE_BAND_ST / 2
    mul cx
%endif
    mov cx, ax
    pop ax
    cld
%if (GFXE_BAND_ST & 1) != 0
    rep stosb                       ; the paper word's low byte IS the paper
%else                               ; byte, 0FFh or 0
    rep stosw
%endif
    pop es
    ret

; -----------------------------------------------------------------------------
; gfxe_bpix - one pixel into the band, in BAND coordinates
; in:  AX = x, DX = y; both inside the band - THIS DOES NOT CLIP
; out: nothing; clobbers AX, BX, CX, DX, DI, flags
;
; The pixel primitive is a bit-set and not a slot, which is the whole of why a
; band composer is worth having: a plotted pixel here is a read-modify-write,
; against OSAPI_GFX_PIXEL's 640.87 us on the field machine.
; -----------------------------------------------------------------------------
gfxe_bpix:
    push ax
    mov ax, dx
    GFXE_ROWOFF
    mov di, ax
    pop ax
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add di, bx
    and al, 7
    mov cl, al
    mov bl, 07Fh
    ror bl, cl
    and [GFXE_BAND_BUF + di], bl
    ret

; -----------------------------------------------------------------------------
; gfxe_bput - the band onto the glass, ONE arrival (SPEC.md 5.4.2)
; in:  the gfx lock held, the clip armed, the band composed
; out: CF = 1 REFUSED - a kern_small kernel before SPEC.md 5.4.2.5.1 carries
;      the slot and not the body, so a package that can be run on one has to
;      have a second way to draw; clobbers AX, BX, CX, DX, SI, BP, flags
;
; The STRIDE stays the buffer's whatever the band's width is, so gfxe_line's
; row step never has to change - only the COLUMNS committed shrink.
; -----------------------------------------------------------------------------
gfxe_bput:
    push es
    push ds
    pop es
    mov si, GFXE_BAND_BUF
    mov bp, GFXE_BAND_ST
    mov ax, [gfxe_bx0]
    mov bx, [gfxe_by0]
    mov cx, [gfxe_bw]
    mov dx, [gfxe_bh]
    call OSAPI_GFX_BLIT1
    pop es                          ; `pop` writes no flag, so BLIT1's CF is
    ret                             ; still the caller's answer

%endif ; GFXE_BAND

%ifdef GFXE_LINE
; -----------------------------------------------------------------------------
; gfxe_line - one line into the band, in BAND coordinates
; in:  AX = x1, BX = y1, CX = x2, DX = y2; all inside the band - NO CLIPPING
; out: nothing; clobbers AX, BX, CX, DX, SI, DI, BP, flags
;
; Ordered left to right first, so the x step is always +1 and only the y step
; carries a sign - which halves the octant count for free and is why there are
; two loop bodies here rather than eight.
;
; The bit is an AND MASK and not a set bit (see the polarity note in the header
; at the top of this file), so it walks by rotating a ZERO along with stc/rcr,
; and the carry OUT of that rotate is the wrap into the next byte. That is one
; instruction for the advance and one branch for the wrap, which is the whole
; reason this is worth having over a per-pixel slot.
;
; ~24.6 us a pixel on a 4.77MHz 8088 against OSAPI_GFX_LINE's 31.6 with the
; arrival removed and 37.1 with it (PERFORMANCE.md Set 139) - so the library
; is FASTER than the slot it replaces as well as cheaper on the kernel.
; -----------------------------------------------------------------------------
gfxe_line:
    cmp ax, cx
    jle .ord
    xchg ax, cx                     ; left to right, both ends
    xchg bx, dx
.ord:
    mov [gfxe_lx1], ax
    mov [gfxe_ly1], bx
    sub cx, ax
    mov [gfxe_ldx], cx
    mov ax, dx
    sub ax, bx
    mov word [gfxe_lsy], GFXE_BAND_ST
    jns .dy
    neg ax
    mov word [gfxe_lsy], -GFXE_BAND_ST
.dy:
    mov [gfxe_ldy], ax

    mov ax, [gfxe_ly1]              ; the first byte, and the bit in it
    GFXE_ROWOFF
    mov di, ax
    mov ax, [gfxe_lx1]
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add di, bx
    and al, 7
    mov cl, al
    mov bl, 07Fh
    ror bl, cl                      ; the ZERO walks, not the one

    mov ax, [gfxe_ldx]
    cmp ax, [gfxe_ldy]
    jl .majy

    mov si, ax                      ; --- x is the major axis
    inc si
    mov bp, [gfxe_ldy]
    add bp, bp
    mov [gfxe_le1], bp
    sub bp, ax
    add ax, ax
    mov [gfxe_le2], ax
.mx:
    and [GFXE_BAND_BUF + di], bl
    stc
    rcr bl, 1
    jc .mx1
    mov bl, 07Fh
    inc di
.mx1:
    add bp, [gfxe_le1]
    jle .mx2
    sub bp, [gfxe_le2]
    add di, [gfxe_lsy]
.mx2:
    dec si
    jnz .mx
    ret

.majy:                              ; --- ...or y is
    mov si, [gfxe_ldy]
    inc si
    mov bp, [gfxe_ldx]
    add bp, bp
    mov [gfxe_le1], bp
    sub bp, [gfxe_ldy]
    mov ax, [gfxe_ldy]
    add ax, ax
    mov [gfxe_le2], ax
.my:
    and [GFXE_BAND_BUF + di], bl
    add di, [gfxe_lsy]
    add bp, [gfxe_le1]
    jle .my2
    sub bp, [gfxe_le2]
    stc
    rcr bl, 1
    jc .my2
    mov bl, 07Fh
    inc di
.my2:
    dec si
    jnz .my
    ret
%endif ; GFXE_LINE

%ifdef GFXE_TEXT
  %define GFXE_STYLED
%endif
%ifdef GFXE_BITS
  %define GFXE_STYLED
%endif

%ifdef GFXE_STYLED
; =============================================================================
; GFXE_TEXT / GFXE_BITS - a widget's face, composed (SPEC.md 5.12.8, 107.11)
;
; Both draw into the band at the caller's stride, in the STYLE [gfxe_tsty]:
;
;   GFXE_INV   set bits: WHITE ink on a black ground. Without it the ink
;              CLEARS bits - black on the lit paper, the polarity above
;   GFXE_BOLD  the glyph ORed with itself one pixel to the right
;   GFXE_X2 / GFXE_X3   the glyph at twice or three times its size
;
; THE OPPOSITE POLARITY THE PLOT KEEPS OUT IS HERE, and it is tested: the
; DIA widget's focused plate and its handle are white text on black, and
; tests/desklayer.py's deskground leg compares them pixel for pixel with the
; guest's own font. Only these two capabilities carry it.
;
; x is a band PIXEL and need not be on the byte grid: a row's pattern is built
; left-aligned in 32 bits (DX:AX, bit 15 of DX leftmost) and shifted into
; place. y is SIGNED, and a row outside 0 .. GFXE_BAND_H - 1 is skipped - so
; a card composed one slab at a time draws a glyph that straddles two slabs
; half into each. Bytes past the stride are skipped; x itself is >= 0.
; =============================================================================
GFXE_INV    equ 1
GFXE_BOLD   equ 2
GFXE_X2     equ 4
GFXE_X3     equ 8

; gfxe_rowput - merge one row's pattern into the band
; in:  DX:AX = the pattern, left-aligned; BX = the band row (the caller has
;      clipped it to 0 .. GFXE_BAND_H - 1); [gfxe_tx] = x (>= 0)
; out: clobbers AX, BX, CX, DX, DI, flags
gfxe_rowput:
    call gfxe_phase             ; DX:AX shifted by x & 7
    push ax
    push dx
    mov ax, bx
    GFXE_ROWOFF                 ; AX = the row's offset (CL, and DX/BX on the
    mov di, ax                  ; mul arm - both banked or spent)
    call gfxe_col               ; + the byte column; CX = the bytes left
    pop dx
    pop ax
    jle gfxe_rmr                ; x past the stride: nothing
    jmp short gfxe_merge

; gfxe_phase - DX:AX shifted right by [gfxe_tx] & 7, the pixel phase. CL spent
gfxe_phase:
    mov cl, [gfxe_tx]
    and cl, 7
    jz .r
.sh:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .sh
.r:
    ret

; gfxe_col - DI += [gfxe_tx]'s byte column; CX = the bytes left in the row,
; the flags its sign against zero (JLE: none). AX spent
gfxe_col:
    mov ax, [gfxe_tx]
    mov cl, 3
    shr ax, cl
    add di, ax
    mov cx, GFXE_BAND_ST
    sub cx, ax
    ret

; gfxe_merge - DX:AX (already in place) into the band at DI, at most CX bytes
; and four, in the style's ink; it stops at the first all-zero remainder.
; Clobbers AX, BX, CX, DX, DI, flags
gfxe_merge:
    test byte [gfxe_tsty], GFXE_INV
    jnz .w
.k:
    mov bl, dh                  ; black ink: clear the bits
    not bl
    and [GFXE_BAND_BUF + di], bl
    inc di
    dec cx
    jz gfxe_rmr                 ; the stride's end
    mov dh, dl
    mov dl, ah
    mov ah, al
    xor al, al
    mov bx, dx
    or bx, ax
    jnz .k                      ; ...until nothing is left
    ret
.w:
    or [GFXE_BAND_BUF + di], dh ; white ink: set them
    inc di
    dec cx
    jz gfxe_rmr
    mov dh, dl
    mov dl, ah
    mov ah, al
    xor al, al
    mov bx, dx
    or bx, ax
    jnz .w
gfxe_rmr:
    ret
%endif ; GFXE_STYLED

%ifdef GFXE_TEXT
; -----------------------------------------------------------------------------
; gfxe_glyph - one 8x8 glyph into the band, in the style [gfxe_tsty]
; in:  AL = the character, CX = x (>= 0), DX = y (signed); band coordinates
; out: CX = x + 8 x the scale; clobbers AX, BX, DX, SI, DI, BP, flags. ES kept
;
; The kernel's table (OSAPI_FONT_GLYPHS) - [gfxe_fseg] zero asks again, which
; is how a caller follows a table that MOVED (SPEC.md 5.12.8). A code outside
; it advances the pen and draws nothing.
;
; A glyph wholly outside the band's rows costs the reject and nothing else -
; a card composed a slab at a time walks every glyph once a slab - and an x1
; glyph (bold or not, either ink) that has two band bytes to land in takes
; THE STRAIGHT PATH: its visible rows clipped once, the row offset taken once
; and stepped by the stride, the ink sense and the bold chosen once, and each
; row one `shr ax, cl` into two bytes - a bold x1 row is 9 bits and x & 7 is
; at most 7, so 16 bits always hold it. x2, x3 and a glyph in the stride's
; last byte take the general path through gfxe_expand and gfxe_rowput.
; -----------------------------------------------------------------------------
; GFXE_G1 ink, bold - one straight-path body: CH rows from ES:SI into the band
; at DI, CL the pixel phase
%macro GFXE_G1 2
%%r:
    mov al, [es:si]
    inc si
    mov ah, al
    xor al, al                  ; AX = the row, left-aligned in 16
  %if %2
    mov dx, ax
    shr dx, 1
    or ax, dx                   ; ...ORed one pixel right
  %endif
    shr ax, cl                  ; ...in place
  %if %1
    or [GFXE_BAND_BUF + di], ah ; white ink: set the bits
    or [GFXE_BAND_BUF + di + 1], al
  %else
    not ax                      ; black ink: clear them
    and [GFXE_BAND_BUF + di], ah
    and [GFXE_BAND_BUF + di + 1], al
  %endif
    add di, GFXE_BAND_ST
    dec ch
    jnz %%r
    jmp .adv
%endmacro

gfxe_glyph:
    push es
    mov [gfxe_tx], cx
    mov [gfxe_ty], dx
    mov bl, [gfxe_tsty]
    and bl, GFXE_X2 | GFXE_X3
    mov cl, 2
    shr bl, cl
    inc bl                      ; 1, 2 or 3
    cmp bl, 3
    jbe .s
    mov bl, 3
.s:
    mov [gfxe_ts], bl
    cmp dx, GFXE_BAND_H         ; THE REJECT: below the band...
    jge .adv
    xor bh, bh
    mov cl, 3
    shl bx, cl                  ; BX = the glyph's rows, 8 x the scale
    add bx, dx
    jle .adv                    ; ...or above it: nothing to draw
    call gfxe_font
    cmp al, [gfxe_f0]
    jb .adv
    cmp al, [gfxe_f1]
    ja .adv
    sub al, [gfxe_f0]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1                   ; 8 bytes a glyph
    add ax, [gfxe_foff]
    mov si, ax
    mov es, [gfxe_fseg]
    cmp byte [gfxe_ts], 1
    jne .gen
    mov ax, [gfxe_tx]           ; --- THE STRAIGHT PATH (x1)
    mov cl, 3
    shr ax, cl
    cmp ax, GFXE_BAND_ST - 1
    jae .gen                    ; the stride's last byte, or past it
    mov bp, ax                  ; BP = the byte column
    mov ax, 8                   ; BX = y + 8: the rows to the glyph's end
    cmp bx, GFXE_BAND_H
    jle .e
    sub ax, bx                  ; ...clipped at the band's foot
    add ax, GFXE_BAND_H
.e:
    or dx, dx
    jns .t
    sub si, dx                  ; ...and at its head: skip -y glyph rows
    add ax, dx
    xor dx, dx
.t:
    push ax                     ; the visible rows
    mov ax, dx
    GFXE_ROWOFF                 ; the first one's offset (CL; DX, BX)
    add ax, bp
    mov di, ax
    pop ax
    mov ch, al
    mov cl, [gfxe_tx]
    and cl, 7                   ; the pixel phase
    test byte [gfxe_tsty], GFXE_INV
    jnz .wi
    test byte [gfxe_tsty], GFXE_BOLD
    jnz .kb
    GFXE_G1 0, 0
.kb:
    GFXE_G1 0, 1
.wi:
    test byte [gfxe_tsty], GFXE_BOLD
    jnz .wb
    GFXE_G1 1, 0
.wb:
    GFXE_G1 1, 1
.gen:
    mov ax, [gfxe_ty]           ; --- THE GENERAL PATH: DI at the glyph's
    GFXE_ROWOFF                 ; first row - above the band it is off the
    mov di, ax                  ; buffer, and modular arithmetic steps it on
    call gfxe_col               ; (mod 65536, a row's stride a step)
    jle .adv                    ; x past the stride: nothing
    mov [gfxe_tl], cx           ; the bytes left in a row
    mov bp, 8                   ; eight glyph rows
.row:
    mov al, [es:si]
    inc si
    mov bx, [gfxe_ty]           ; a glyph row whose copies are all off the
    cmp bx, GFXE_BAND_H         ; band is never expanded: below it, the rest
    jge .adv                    ; are too...
    mov cl, [gfxe_ts]
    xor ch, ch
    add bx, cx
    jg .vis
    add [gfxe_ty], cx           ; ...above it, step over this one
.over:
    add di, GFXE_BAND_ST
    loop .over
    jmp short .rnx
.vis:
    call gfxe_expand            ; DX:AX = the row, scaled, bolded, left-aligned
    call gfxe_phase             ; ...and in place, once for all its copies
    mov cl, [gfxe_ts]
.rep:
    mov bx, [gfxe_ty]
    inc word [gfxe_ty]
    or bx, bx
    js .skip                    ; above the band
    cmp bx, GFXE_BAND_H
    jge .skip                   ; below it
    push ax
    push cx
    push dx
    push di
    mov cx, [gfxe_tl]
    call gfxe_merge
    pop di
    pop dx
    pop cx
    pop ax
.skip:
    add di, GFXE_BAND_ST
    dec cl
    jnz .rep
.rnx:
    dec bp
    jnz .row
.adv:
    mov al, [gfxe_ts]
    mov cl, 3
    shl al, cl
    xor ah, ah
    mov cx, [gfxe_tx]
    add cx, ax
    pop es
    ret

; gfxe_expand - AL = a glyph row -> DX:AX, left-aligned: [gfxe_ts] copies of
; each bit - a NIBBLE at a time out of gfxe_x2t / gfxe_x3t, never a bit at a
; time - then ORed one pixel right with GFXE_BOLD. Keeps BX and CX
gfxe_expand:
    push bx
    push cx
    mov cl, [gfxe_ts]
    cmp cl, 1
    jne .wide
    mov dh, al
    xor dl, dl
    xor ax, ax
    jmp short .bold
.wide:
    mov bl, al
    mov cl, 4
    shr al, cl                  ; AL = the high nibble, BL's low one the other
    and bx, 0Fh
    cmp byte [gfxe_ts], 2
    jne .three
    mov dl, [gfxe_x2t + bx]     ; x2: a byte a nibble, DX the row's 16
    mov bl, al
    mov dh, [gfxe_x2t + bx]
    xor ax, ax
    jmp short .bold
.three:
    shl bx, 1                   ; x3: 12 bits a nibble, a word each
    mov cx, [gfxe_x3t + bx]     ; CX = the low nibble's
    mov bl, al
    shl bx, 1
    mov dx, [gfxe_x3t + bx]     ; DX = the high one's...
    push cx
    mov cl, 4
    shl dx, cl                  ; ...in DX's top 12 bits
    pop cx
    or dl, ch                   ; the low nibble's top 4 bits beside them
    mov ah, cl                  ; ...and its low 8 in AH
    xor al, al
.bold:
    test byte [gfxe_tsty], GFXE_BOLD
    jz .r
    mov bx, dx
    mov cx, ax
    shr bx, 1
    rcr cx, 1
    or dx, bx
    or ax, cx
.r:
    pop cx
    pop bx
    ret

; a nibble's four bits, each twice (x2) and three times (x3), right-aligned
gfxe_x2t:
%assign gfxe_n 0
%rep 16
  %assign gfxe_v 0
  %assign gfxe_b 0
  %rep 4
    %if gfxe_n & (1 << gfxe_b)
      %assign gfxe_v gfxe_v | (3 << (gfxe_b * 2))
    %endif
    %assign gfxe_b gfxe_b + 1
  %endrep
    db gfxe_v
  %assign gfxe_n gfxe_n + 1
%endrep
gfxe_x3t:
%assign gfxe_n 0
%rep 16
  %assign gfxe_v 0
  %assign gfxe_b 0
  %rep 4
    %if gfxe_n & (1 << gfxe_b)
      %assign gfxe_v gfxe_v | (7 << (gfxe_b * 3))
    %endif
    %assign gfxe_b gfxe_b + 1
  %endrep
    dw gfxe_v
  %assign gfxe_n gfxe_n + 1
%endrep

; gfxe_font - the kernel's glyph table, into [gfxe_fseg]:[gfxe_foff] and the
; codes it covers - asked while [gfxe_fseg] is 0, so a caller zeroes it to
; follow a table a resume MOVED (SPEC.md 5.12.8). Every register kept
gfxe_font:
    cmp word [gfxe_fseg], 0
    jne .r
    push ax
    push cx
    push dx
    push si
    call OSAPI_FONT_GLYPHS      ; DX:SI, AL = first, AH = last, CX = 8
    mov [gfxe_fseg], dx
    mov [gfxe_foff], si
    mov [gfxe_f0], al
    mov [gfxe_f1], ah
    pop si
    pop dx
    pop cx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; gfxe_text - a NUL string of glyphs, in the style [gfxe_tsty]
; in:  SI = the string (DS), CX = x, DX = y
; out: CX = past the last glyph, SI = past the NUL; clobbers AX, BX, DI, BP
; -----------------------------------------------------------------------------
gfxe_text:
    cld
.c:
    lodsb
    or al, al
    jz .r
    push si
    push dx
    call gfxe_glyph
    pop dx
    pop si
    jmp short .c
.r:
    ret

; -----------------------------------------------------------------------------
; gfxe_bfill - a rectangle of ink: cleared bits (black), or set ones with
; GFXE_INV (white)
; in:  AX = x, BX = y, CX = w, DX = h (band coordinates, signed; clipped to the
;      band both ways - an empty one draws nothing)
; out: clobbers AX, BX, CX, DX, SI, DI, BP, flags
; -----------------------------------------------------------------------------
gfxe_bfill:
    or ax, ax
    jns .x0
    add cx, ax
    xor ax, ax
.x0:
    mov si, ax
    add si, cx                  ; SI = x end, exclusive
    cmp si, GFXE_BAND_W
    jle .x1
    mov si, GFXE_BAND_W
.x1:
    cmp si, ax
    jle .out
    or bx, bx
    jns .y0
    add dx, bx
    xor bx, bx
.y0:
    mov di, bx
    add di, dx                  ; DI = y end, exclusive
    cmp di, GFXE_BAND_H
    jle .y1
    mov di, GFXE_BAND_H
.y1:
    cmp di, bx
    jle .out
    sub di, bx
    mov [gfxe_frows], di        ; the rows
    dec si                      ; the last pixel
    mov cl, al
    and cl, 7
    mov dl, 0FFh
    shr dl, cl                  ; DL = the first byte's mask
    mov cx, si
    and cl, 7
    mov dh, 0FFh
    mov ch, 7
    sub ch, cl
    mov cl, ch
    shl dh, cl                  ; DH = the last byte's
    mov cl, 3
    shr si, cl                  ; the last byte column
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1                   ; ...and the first
    sub si, cx                  ; SI = the bytes between, less one
    jnz .w
    and dl, dh                  ; one byte: both edges in it
.w:
    mov [gfxe_fmask], dx
    mov [gfxe_fcol], cx
    mov ax, bx
    GFXE_ROWOFF                 ; the first row's offset
    add ax, [gfxe_fcol]
    mov di, ax
.row:
    push di
    mov al, [gfxe_fmask]        ; the first byte
    call gfxe_fbyte
    inc di
    mov cx, si
    jcxz .rowx                  ; one byte: done
    dec cx
    jz .last
.mid:
    mov al, 0FFh                ; every byte between, whole
    call gfxe_fbyte
    inc di
    loop .mid
.last:
    mov al, [gfxe_fmask + 1]
    call gfxe_fbyte
.rowx:
    pop di
    add di, GFXE_BAND_ST
    dec word [gfxe_frows]
    jnz .row
.out:
    ret

; gfxe_fbyte - AL = a mask: those bits of the band byte at DI set (GFXE_INV)
; or cleared. Keeps everything but AL and the flags
gfxe_fbyte:
    test byte [gfxe_tsty], GFXE_INV
    jnz .o
    not al
    and [GFXE_BAND_BUF + di], al
    ret
.o:
    or [GFXE_BAND_BUF + di], al
    ret
%endif ; GFXE_TEXT

%ifdef GFXE_BITS
; -----------------------------------------------------------------------------
; gfxe_bits - a 16-wide bit record into the band, in the style's GFXE_INV
; in:  SI = the record - db rows, then a word a row, bit 15 the leftmost
;      pixel; CX = x (>= 0), DX = y (signed), band coordinates
; out: clobbers AX, BX, CX, DX, SI, DI, BP, flags
; -----------------------------------------------------------------------------
gfxe_bits:
    cld
    mov [gfxe_tx], cx
    mov [gfxe_ty], dx
    lodsb
    xor ah, ah
    mov bp, ax
    or bp, bp
    jz .r
.row:
    lodsw
    mov bx, [gfxe_ty]
    inc word [gfxe_ty]
    or bx, bx
    js .skip
    cmp bx, GFXE_BAND_H
    jge .skip
    mov dx, ax                  ; the row's 16 bits, left-aligned in 32
    xor ax, ax
    push si
    call gfxe_rowput
    pop si
.skip:
    dec bp
    jnz .row
.r:
    ret
%endif ; GFXE_BITS

%ifdef GFXE_POINTS
; =============================================================================
; GFXE_POINTS - the SPARSE sink: your own point array, one OSAPI_GFX_POINTS
;
; The caller declares, before the %include:
;   GFXE_PT_BUF   the address of GFXE_PT_MAX * 4 bytes, yours
;   GFXE_PT_MAX   how many points it holds
;
; WHY A SLOT HERE AND A BIT-SET IN GFXE_BAND. A band is priced by its AREA and
; a point list by the INK it lays down, so the right sink is decided by the
; figure and not by taste: a stroke or a grid fills its band and commits at
; 0.40-0.77 us a band pixel, while a walk two or three pixels wide over eight
; places would commit a band that is almost entirely paper. OSAPI_GFX_POINTS
; is 165.96 us a point (PERFORMANCE.md Set 140) and carries the clip region,
; the cursor's save-under, the adapter and the second display for you - the
; six per-call concerns of docs/plans/completed/GFX-EMBEDDABLE-PLAN.md 3.2.1 - which is
; exactly why an app-side rasteriser cannot simply write pixels itself.
;
; A FULL LIST COMMITS ITSELF and the caller never finds out. That is what
; makes the size a TUNING choice rather than a correctness one: the buffer is
; a batching window, so one too small costs an extra arrival and never a
; pixel, and there is no second path for a caller to carry. [gfxe_pflush]
; counts the forced commits, so a test can assert 0 to say the buffer is
; sized for the worst frame - and the picture is right either way.
; =============================================================================
  %ifndef GFXE_PT_BUF
    %error "os88gfx.inc: GFXE_POINTS wants GFXE_PT_BUF - GFXE_PT_MAX*4 bytes"
  %endif
  %ifndef GFXE_PT_MAX
    %error "os88gfx.inc: GFXE_POINTS wants GFXE_PT_MAX"
  %endif
  %if GFXE_PT_MAX < 1
    %error "os88gfx.inc: GFXE_PT_MAX must be at least 1"
  %endif

; gfxe_pnew - empty the list; clobbers nothing
gfxe_pnew:
    mov word [gfxe_pn], 0
    ret

; gfxe_padd - append one point; AX = x, DX = y (screen px); clobbers BX, flags
; A FULL list is committed here and the caller carries on - see above. That
; costs an arrival and nothing else, and everything live is banked across it
; because this is called from inside gfxe_wstep's loop.
gfxe_padd:
    mov bx, [gfxe_pn]
    cmp bx, GFXE_PT_MAX
    jae .full
    add bx, bx
    add bx, bx
    mov [GFXE_PT_BUF + bx], ax
    mov [GFXE_PT_BUF + bx + 2], dx
    inc word [gfxe_pn]
    ret
.full:
    ; ONLY CX AND SI ARE BANKED, and that is `gfxe_pput`'s contract below
    ; rather than an assumption: it clobbers CX and SI - its own two `mov`s -
    ; and `OSAPI_GFX_POINTS` under it **preserves every register** (SPEC.md
    ; 5.6.9). AX and DX are the POINT this call was handed and they are read
    ; again at the `jmp` below, so they matter more than the rest; they were
    ; banked here too, and did not need to be.
    ;
    ; Worth 4 bytes at the DEEPEST FRAME of every walk in the tree - this is
    ; the bottom of the chain in Cyclone, Missile, Tank, Paint and wire alike
    ; (docs/FIELD-NOTES.md 42.2) - and four instructions off the flush path.
    ; If `gfxe_pput` ever clobbers more, the list here grows with it.
    inc word [gfxe_pflush]
    push cx
    push si
    call gfxe_pput
    pop si
    pop cx
    jmp gfxe_padd               ; ...and now there is room, GFXE_PT_MAX >= 1

; gfxe_pput - the list onto the glass, ONE arrival, and empty it again
; in:  the gfx lock held, the clip armed, [gfx_color] the ink
; out: nothing; clobbers CX, SI, flags
gfxe_pput:
    mov cx, [gfxe_pn]
    or cx, cx
    jz .out
    mov word [gfxe_pn], 0
    mov si, GFXE_PT_BUF
    call OSAPI_GFX_POINTS       ; an X slot: it puts OUR segment in ES itself
.out:
    ret
%endif ; GFXE_POINTS

%ifdef GFXE_WALK
; =============================================================================
; GFXE_WALK - SPEC.md 5.6.7's resumable walk, in the caller's image
;
; THE BLOCK LAYOUT IS THE KERNEL'S, unchanged (GLS_X, GLS_Y, GLS_ERR, GLS_DX,
; GLS_DY, GLS_SX, GLS_SY; GLS_SZ bytes). That is not laziness - a program
; converting from OSAPI_GFX_LINIT/LSTEP changes the CALL and nothing else, and
; its own arithmetic on the block's x and y keeps working (SPEC.md 48.14 has
; Missile Command doing exactly that).
;
; **THE SLOT IS GONE NOW, not merely retired**: SPEC.md 18.95.8 reused cell
; 0x0300 for OSAPI_DSK_CACHE, so `gfxe_winit` below is the ONLY thing that
; sets one of these blocks up. That is what a retired cell is for - it answers
; CF=1, every caller of one has to test CF, and so the number can be spent -
; but it does mean the conversion is no longer optional.
;
; WHAT IS NOT HERE IS THE INTERESTING PART. The kernel's walk carries a clip
; rect, a framebuffer byte and bit, the second display's translation and the
; cursor - and NONE of that comes with it, because OSAPI_GFX_POINTS does every
; one of them at the commit. So the app-side walk is PURE ARITHMETIC over the
; caller's own words, which is why it is a fifth of the size the plan costed
; and why it can be a `add si, [di + GLS_SX]` where the kernel needs a branch
; (its sx also has to move a bit mask and a byte pointer).
;
; The blocks and the descriptor array are in DS, not ES - the callers this
; replaces all set ES = DS before the call anyway, and dropping the override
; is worth more than the generality.
; =============================================================================
GLS_X   equ 0                   ; SPEC.md 5.6.7's block, mirrored from
GLS_Y   equ 2                   ; kernel/kernel.asm - tests/unit/t_mirror.py
GLS_ERR equ 4                   ; is what keeps the two the same
GLS_DX  equ 6
GLS_DY  equ 8
GLS_SX  equ 10
GLS_SY  equ 12

; -----------------------------------------------------------------------------
; gfxe_winit - set a walk block up for the line (x1,y1)-(x2,y2)
; in:  AX = x1, BX = y1, CX = x2, DX = y2; DI = a GLS_SZ-byte block (in DS)
; out: the block; preserves every register
;
; OSAPI_GFX_LINIT's arithmetic, exactly - it has to be, or a block set up one
; way and stepped the other walks a different line. The kernel's version was a
; slot only because the kernel's walk was; there was nothing in it that needed
; the kernel at all, which is why its cell could be spent on something else.
;
; THE WALK RUNS IN THE CALLER'S DIRECTION and is not normalised downward the
; way gfx_line's pixel set is (SPEC.md 5.6.2), because the caller is drawing
; outward from a fixed point and needs step 1 to be next to it. Two blocks
; built from the same endpoints agree by construction, which is all a
; draw/erase pair needs.
; -----------------------------------------------------------------------------
gfxe_winit:
    push ax
    push bx
    push cx
    push dx
    mov [di + GLS_X], ax
    mov [di + GLS_Y], bx
    mov word [di + GLS_SX], 1
    sub cx, ax
    jns .dxok
    neg cx
    mov word [di + GLS_SX], -1
.dxok:
    mov [di + GLS_DX], cx
    mov word [di + GLS_SY], 1
    sub dx, bx
    jns .dyok
    neg dx
    mov word [di + GLS_SY], -1
.dyok:
    mov [di + GLS_DY], dx
    sub cx, dx                      ; err = dx - dy, exactly gfx_line's
    mov [di + GLS_ERR], cx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfxe_wstep - append the next CX pixels of one walk to the point list
; in:  DI = the block (in DS), CX = how many (0 legal, does nothing)
; out: the block advanced; clobbers AX, BX, CX, DX, SI, BP, flags
;
; The CURRENT point is appended first and the walk advances after, so N then M
; pixels are exactly the N+M one call would have appended - 5.6.7's contract,
; and what a draw/erase pair rests on.
; -----------------------------------------------------------------------------
gfxe_wstep:
    or cx, cx
    jnz .go                     ; not `jcxz`: every conditional jump on an 8086
    ret                         ; is rel8 and the body below is longer
.go:
    mov si, [di + GLS_X]
    mov dx, [di + GLS_Y]
    mov bp, [di + GLS_ERR]
.px:
    mov ax, si
    call gfxe_padd              ; clobbers BX only
    mov ax, bp
    add ax, ax                  ; e2 = 2 * err
    mov bx, [di + GLS_DY]
    neg bx
    cmp ax, bx                  ; e2 > -dy: step x
    jle .noxs
    sub bp, [di + GLS_DY]
    add si, [di + GLS_SX]       ; sx is +1 or -1 as a WORD, so no branch
.noxs:
    cmp ax, [di + GLS_DX]       ; e2 < dx: step y, EITHER WAY (5.6.7)
    jge .noys
    add bp, [di + GLS_DX]
    add dx, [di + GLS_SY]
.noys:
    dec cx
    jnz .px
    mov [di + GLS_X], si
    mov [di + GLS_Y], dx
    mov [di + GLS_ERR], bp
    ret

; -----------------------------------------------------------------------------
; gfxe_wline - the WHOLE line (x1,y1)-(x2,y2) into the point list, one call
; in:  AX = x1, BX = y1, CX = x2, DX = y2; DI = a GLS_SZ block (in DS)
; out: the block left at the far end - so an ERASE is `gfxe_wline` again with
;      the paper ink and the same endpoints, which is exact by SPEC.md 5.6.2
;      rather than dilated by 5.6.5; clobbers AX, BX, CX, DX, SI, BP, flags
;
; A WHOLE LINE IS A WALK OF ITS FULL LENGTH, which is the lattice's own claim
; (SPEC.md 5.12's capability table) made into eighteen bytes: `gfxe_winit` has already reduced the line to
; |dx| and |dy|, so the pixel count is the larger of them plus one and the walk
; does the rest. It is the piece a program wants when it is NOT animating -
; drawing a figure once - and it needs nothing GFXE_WALK did not already carry.
; -----------------------------------------------------------------------------
gfxe_wline:
    call gfxe_winit
    mov cx, [di + GLS_DX]
    cmp cx, [di + GLS_DY]
    jae .n
    mov cx, [di + GLS_DY]
.n:
    inc cx
    jmp gfxe_wstep              ; tail: its ret is ours

; -----------------------------------------------------------------------------
; gfxe_wstepv - step MANY walks into the list, OSAPI_GFX_LSTEPV's shape
; in:  SI = an array of CX descriptors, each `dw block_offset, pixels`, the
;      blocks in DS too; CX = how many pairs (0 legal)
; out: every block advanced; clobbers everything but the segments
;
; A descriptor with a zero count is legal and skipped, so a caller may build
; the array once and vary what it asks for - the slot's own contract, kept, so
; that a conversion is one line.
; -----------------------------------------------------------------------------
gfxe_wstepv:
    or cx, cx
    jnz .go
    ret
.go:
    mov [gfxe_wn], cx
    mov [gfxe_wp], si
.each:
    mov si, [gfxe_wp]
    mov di, [si]
    mov cx, [si + 2]
    add word [gfxe_wp], 4
    call gfxe_wstep
    dec word [gfxe_wn]
    jnz .each
    ret
%endif ; GFXE_WALK

; --- the library's own state, in the IMAGE (see the header) ------------------
%ifdef GFXE_BAND
gfxe_mnx:   dw 0                    ; the running bounds gfxe_bfold widens
gfxe_mxx:   dw 0
gfxe_mny:   dw 0
gfxe_mxy:   dw 0
gfxe_bx0:   dw 0                    ; ...closed by gfxe_bsize into the band
gfxe_by0:   dw 0
gfxe_bw:    dw 0
gfxe_bh:    dw 0
%endif
%ifdef GFXE_STYLED
gfxe_tsty:  db 0                    ; the style: GFXE_INV / BOLD / X2 / X3
gfxe_ts:    db 1                    ; ...its scale, as gfxe_glyph decoded it
gfxe_tx:    dw 0                    ; the pen, as a glyph or record is merged
gfxe_ty:    dw 0
gfxe_tl:    dw 0                    ; gfxe_glyph's bytes left in a row
%endif
%ifdef GFXE_TEXT
gfxe_fseg:  dw 0                    ; the kernel's glyph table; 0 asks again
gfxe_foff:  dw 0
gfxe_f0:    db 0                    ; ...the first code it covers
gfxe_f1:    db 0                    ; ...and the last
gfxe_frows: dw 0                    ; gfxe_bfill's working set
gfxe_fmask: dw 0
gfxe_fcol:  dw 0
%endif
%ifdef GFXE_POINTS
gfxe_pn:    dw 0                    ; how many points are in the list...
gfxe_pflush: dw 0                   ; ...and how often a full one committed
%endif                              ; itself (see GFXE_POINTS above)
%ifdef GFXE_WALK
gfxe_wn:    dw 0                    ; gfxe_wstepv's cursor over the descriptors
gfxe_wp:    dw 0
%endif
%ifdef GFXE_LINE
gfxe_lx1:   dw 0                    ; gfxe_line's working set
gfxe_ly1:   dw 0
gfxe_ldx:   dw 0
gfxe_ldy:   dw 0
gfxe_lsy:   dw 0
gfxe_le1:   dw 0
gfxe_le2:   dw 0
%endif
