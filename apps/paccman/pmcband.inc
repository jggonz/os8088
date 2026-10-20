; =============================================================================
; os8088 - apps/paccman/pmcband.inc      PACCMAN's band composer (SPEC.md 91)
;
; Derived from Andre Weissflog's pacman.c (github.com/floooh/pacman.c, MIT,
; (c) 2020 Andre Weissflog) at commit 0f5ec5a: the 2bpp tile format and the
; colour-block indirection these loops walk are the reference's (its
; gfx_decode_tile at pacman.c 2800-2845 and gfx_decode_color_palette at
; 2865-2891), decoded on the host by tools/paccman_assets.py. The tile data
; itself is Pac-Man arcade ROM data (Namco); see apps/paccman/README.md.
;
; -----------------------------------------------------------------------------
; WHY THIS IS ASSEMBLY (SPEC.md 73.11, PERFORMANCE.md)
; -----------------------------------------------------------------------------
; A frame is composed in the package's own RAM and sent with ONE blit per dirty
; band. Everything above that line is C deciding WHAT to compose; everything
; below is per-pixel and per-byte work, which C costs 3-5x. These four entry
; points are the whole of the per-byte half:
;
;   _pmc_tile      one 8x8 tile through its 4-entry colour block into the
;                  packed 4bpp band (two pixels a byte, high nibble leftmost -
;                  which is OSAPI_GFX_BLIT4's own format, so the BLIT4 fallback
;                  path needs no conversion at all)
;   _pmc_pack_pl   the packed band -> four bitplanes for OSAPI_GFX_BLITP
;   _pmc_pack_1    the packed band -> 1bpp for OSAPI_GFX_BLIT1, through the
;                  monochrome class table and the row-alternating checkerboard
;   _pmc_sprite    one 16x16 sprite OVER the band, colour index 0 transparent
;                  - and, on the CGA layout, the same per-pixel ROW MERGE
;                    _pmc_tile does, because a sprite loses rows to a short
;                    display exactly as a glyph does (SPEC.md 91)
;
; NO GLOBAL IS NAMED FROM HERE. Every table and every buffer arrives as a
; POINTER argument, computed by the C. That is what lets the host harness
; define byte-identical C twins with the same signatures, and what lets
; hosttest/pmcbandtest.asm %include this file and feed it vectors of its own.
;
; ES IS NEVER LOADED. There is not one string instruction here that writes:
; `lodsb` reads DS:SI, which is the package's own segment, and every store is
; `mov [di], r8`, which is DS-relative on the 8086. So the routines below need
; no `push es` and cannot write into KERNEL_SEG (SPEC.md 20.4, 73.5.1) - and
; hosttest/pmcbandtest.asm asserts exactly that with an ES sentinel.
;
; cdecl throughout, SmallerC's: arguments pushed right to left, the CALLER
; cleans, and BP/SI/DI/DS/ES/SS are the callee's to restore. The leading
; underscore is SmallerC's, which is why the C prototypes say `pmc_tile`.
; =============================================================================

PMC_BAND_W      equ 224                 ; the arcade field, in pixels
PMC_BAND_STRIDE equ (PMC_BAND_W / 2)    ; 112 - packed 4bpp, two pixels a byte

; --- AND THE ROW PITCH IS NOT THE ROW WIDTH (wave 2) --------------------------
; A sprite is 16 pixels wide and its position is its actor's centre less 8, so
; at the tunnel mouth it hangs EIGHT PIXELS off each end of the field: Pac-Man
; at x = 0 puts a sprite at -8, and at x = 223 one at 215 whose last pixel is
; 231 of a 224-pixel field. The reference draws both halves and lets the window
; clip - it wraps the ACTOR and never the sprite (pacman.c 1329-1355).
;
; So every band row carries PMC_BAND_PAD bytes of slack at EACH end and the
; field starts at + PMC_BAND_PAD. _pmc_sprite then never clips and never tests
; a bound: it writes its 16 pixels a row, some of them into slack that is never
; packed and never blitted, which is what keeps its inner loop to the ten
; instructions a pixel a 4.77 MHz 8088 can afford. The C hands
; `pmc_band + PMC_BAND_PAD` to the tile composer, to both packers and to the
; BLIT4 fallback, so nothing outside this file ever sees the slack.
; AND FOUR IS EXACT, NOT GENEROUS. The worst LEGAL case is Pac-Man at the left
; tunnel mouth: pmc_move.c wraps the actor at x = 223, so sp_x = 215, bp2 =
; 223, the destination byte is + 111 at an ODD nibble, and sixteen pixels touch
; bytes 111..119 - the row's LAST byte. On band row 7 that is offset 959 of a
; 960-byte scratch, so there is ZERO slack left over: one more pixel of sprite
; x writes into pmc_planes, and nothing in the C or in this file would catch
; it. The invariant that makes the whole no-clip design safe is therefore
;
;     an enabled sprite's x is in [-8, PMC_BAND_W - 9],
;
; which holds because every sprite position in the port is a WRAPPED actor
; centre less 8. A caller that positions a sprite by anything else - the
; wave-3 intro screen is exactly that code - must clamp it or this pad must
; grow. apps/paccman/hosttest/pmcuitest.c asserts both halves: its twin checks
; the destination against the scratch's end, and its per-frame audit checks
; every enabled sprite's x against that range.
PMC_BAND_PAD    equ 4                   ; 8 pixels each side
PMC_BAND_ROW    equ (PMC_BAND_STRIDE + 2 * PMC_BAND_PAD)   ; 120 - the PITCH
PMC_PL_STRIDE   equ (PMC_BAND_W / 8)    ; 28 - one plane's bytes per row
PMC_PL_STEP     equ (PMC_PL_STRIDE * 8) ; 224 - and the step to the next plane.
                                        ; It is 8 rows' worth WHATEVER the band
                                        ; holds, so that the four displacements
                                        ; below are CONSTANTS: a 4-row CGA band
                                        ; leaves half of each plane unwritten
                                        ; and OSAPI_GFX_BLITP is told 4 rows,
                                        ; so nothing reads it

section .text

; -----------------------------------------------------------------------------
; _pmc_tile - void pmc_tile(const unsigned char *src, const unsigned char *pairs,
;                           unsigned char *dst, int rowstep,
;                           const unsigned char *zmask)
;
; in:  [bp+4]  src     the tile's 16 bytes: two per row, four 2-bit pixels a
;                      byte, the LEFTMOST pixel in the high bits
;      [bp+6]  pairs   the 16-entry pair table for this tile's colour block -
;                      pairs[hi*4 + lo] is the packed byte for the pixel pair
;                      (hi, lo), which is exactly what a source NIBBLE indexes.
;                      Built on the host into pmc_pairs[32][16], so a tile
;                      costs no table setup at all
;      [bp+8]  dst     the tile's top-left byte inside the packed band
;      [bp+10] rowstep 1 = every source row (8 output rows), 2 = alternate
;                      rows (4 output rows - the CGA layout, SPEC.md 91)
;      [bp+12] zmask   pmc_zmask[256], the row MERGE's table - used on the
;                      rowstep-2 path ONLY, and 0 there asks for the plain
;                      alternate-row SAMPLE instead. Ignored at rowstep 1
; out: 8 (or 4) rows of 4 bytes written at PMC_BAND_ROW apart
;
; Two XLATs a source byte and nothing else: the colour indirection, the
; nibble split and the pack are ONE table lookup each, because the pair table
; is indexed by the very nibble the source byte already holds.
; -----------------------------------------------------------------------------
; THE ROW MERGE, AND WHY A PLAIN `OR` IS WRONG (SPEC.md 91). At rowstep 2 the
; screen has half the rows the field has, and sampling rows 0, 2, 4, 6 drops
; every horizontal MIDDLE STROKE the arcade font keeps on row 3: `B E F G H S`
; and the digits `3 6 9` lose theirs, so `HIGH SCORE` reads `IIIGII SCORC`.
; The merge keeps them - but a tile pixel is a 2-bit colour INDEX, not a bit,
; so OR-ing the two rows INVENTS a colour wherever one row has index 1 and the
; other index 2 (`1 | 2` is 3, an ink the tile does not have). The merge is
; therefore per PIXEL:
;
;     merged = even | (odd & zmask[even])
;
; take the odd row's pixel only where the even row's is 0. `zmask[b]` is 0b11
; in every 2-bit field of `b` that is zero, generated by tools/paccman_assets.py
; beside pmc_planar as a property of the format rather than of the reference.
;
; It costs ONE xchg pair, one XLAT and two memory ALUs a source byte, and it is
; on the CGA arm only: the rowstep-1 LOOP BODY below is unchanged, and what did
; change for the merge is the PROLOGUE both arms share (the row skip is now a
; branch rather than arithmetic, because DX carries a table on the merge arm) -
; which is also what SPEC.md 91 names when the bench's step-2 sample moves. The
; two tables cannot both live in BX (XLAT's only base), so DX carries the other
; one and `xchg bx, dx` swaps them: two bytes and four clocks (the one-byte
; three-clock form of XCHG is the accumulator's, and AL is carrying the pixel),
; which is why the merge is not a second pass over a scratch.
;
; THE ROW COUNTER IS IN CH AND THE ROW SKIP IN DX, so this routine has no
; local and its epilogue is a plain sequence of pops. CL already holds the
; nibble shift for the whole loop and its other half was going spare; the
; first version spent a stack local on the skip and then had to drop it with
; `mov sp, bp`, which tools/stkbalance.py cannot follow and rightly refuses.
; (On the merge arm DX is the second TABLE and the skip is the constant 4 an
; `add si, 4` carries, which is what frees it.)
_pmc_tile:
    push bp
    mov  bp, sp
    push si
    push di
    push bx
    cld

    mov  si, [bp+4]                 ; the tile's rows
    mov  bx, [bp+6]                 ; the colour block's pair table (XLAT's)
    mov  di, [bp+8]                 ; where it lands in the band
    mov  cl, 4                      ; the nibble shift, held for the whole loop

    mov  ch, 8
    xor  dx, dx                     ; rowstep 1: no source row skipped
    cmp  word [bp+10], 1
    je   .row
    mov  ch, 4                      ; alternate rows: half as many
    mov  dx, [bp+12]                ; the merge's table, or 0 for a sample
    or   dx, dx
    jnz  .mrow
    mov  dx, 2                      ; (rowstep - 1) * 2 bytes skipped a row

.row:
    lodsb                           ; pixels 0..3
    mov  ah, al
    shr  al, cl                     ; the high nibble = pixels 0 and 1
    xlatb
    mov  [di], al
    mov  al, ah
    and  al, 0x0F                   ; the low nibble  = pixels 2 and 3
    xlatb
    mov  [di+1], al

    lodsb                           ; pixels 4..7
    mov  ah, al
    shr  al, cl
    xlatb
    mov  [di+2], al
    mov  al, ah
    and  al, 0x0F
    xlatb
    mov  [di+3], al

    add  si, dx                     ; skip the row we are not sampling
    add  di, PMC_BAND_ROW
    dec  ch
    jnz  .row
    jmp  short .done

    ; --- the CGA merge: two source rows per output row ----------------------
    ; BX = pairs, DX = zmask, swapped by `xchg bx, dx` around each source
    ; byte's merge. SI walks the pair of rows and steps by 4.
.mrow:
    xchg bx, dx                     ; BX = zmask
    mov  al, [si]                   ; the even row's first byte
    xlatb                           ; 0b11 in every field it leaves at 0
    and  al, [si+2]                 ; ...where the odd row's pixel shows
    or   al, [si]
    xchg bx, dx                     ; BX = pairs
    mov  ah, al
    shr  al, cl
    xlatb
    mov  [di], al
    mov  al, ah
    and  al, 0x0F
    xlatb
    mov  [di+1], al

    xchg bx, dx
    mov  al, [si+1]                 ; ...and the second byte, the same way
    xlatb
    and  al, [si+3]
    or   al, [si+1]
    xchg bx, dx
    mov  ah, al
    shr  al, cl
    xlatb
    mov  [di+2], al
    mov  al, ah
    and  al, 0x0F
    xlatb
    mov  [di+3], al

    add  si, 4                      ; the NEXT pair of source rows
    add  di, PMC_BAND_ROW
    dec  ch
    jnz  .mrow

.done:
    pop  bx
    pop  di
    pop  si
    pop  bp
    ret                             ; cdecl: the CALLER pops the arguments

; -----------------------------------------------------------------------------
; _pmc_pack_pl - void pmc_pack_pl(const unsigned char *band,
;                                 unsigned char *planes, int rows,
;                                 const unsigned int *planar, int cols)
;
; The packed 4bpp band -> the four bitplanes OSAPI_GFX_BLITP takes (SPEC.md
; 5.4.3, apps/os88api.inc's OSAPI_GFX_BLITP): plane p at planes + p*PMC_PL_STEP,
; row r at + r*PMC_PL_STRIDE, bit 7 leftmost.
;
; in:  [bp+4]  band    the SPAN's first packed byte; rows of PMC_BAND_ROW
;      [bp+6]  planes  4 * PMC_PL_STEP bytes of scratch
;      [bp+8]  rows    8 or 4 (decremented in place - it is the caller's slot)
;      [bp+10] planar  pmc_planar[256]: a packed byte's four planes as 2-bit
;                      fragments, NIBBLE SPACED (plane 0 in bits 1:0, plane 3
;                      in bits 13:12)
;      [bp+12] cols    output bytes a row, 1..PMC_PL_STRIDE - the damage span,
;                      which is a whole number of 8-pixel TILES by construction
;                      (SPEC.md 91's per-band column span). The row strides
;                      stay PMC_BAND_ROW and PMC_PL_STRIDE whatever it is,
;                      so BLITP is always told the same two numbers and the
;                      bytes past the span are simply not written
;
; THE NIBBLE SPACING IS THE WHOLE TRICK. Two source bytes are four pixels, and
; four pixels are four bits of every plane. `w0 << 2 | w1` accumulates all four
; planes' four bits AT ONCE, because each plane's field is a nibble wide and
; two 2-bit fragments fit it exactly. Two such accumulators - the left four
; pixels and the right four - then split into the four output bytes with one
; shift and one mask each, and a byte shifted left by four discards the nibble
; that does not belong in it, so no mask is needed on that side.
; -----------------------------------------------------------------------------
_pmc_pack_pl:
    push bp
    mov  bp, sp
    sub  sp, 4                      ; [bp-2] source skip a row, [bp-4] dest
    push si
    push di
    push bx
    cld

    mov  ax, [bp+12]                ; cols
    or   ax, ax
    jle  .done
    cmp  word [bp+8], 0
    jle  .done
    mov  dx, ax
    add  ax, ax
    add  ax, ax                     ; cols * 4 packed bytes are consumed a row
    mov  bx, PMC_BAND_ROW
    sub  bx, ax
    mov  [bp-2], bx
    mov  bx, PMC_PL_STRIDE
    sub  bx, dx                     ; ...and cols bytes are written a row
    mov  [bp-4], bx

    mov  si, [bp+4]                 ; the packed band
    mov  di, [bp+6]                 ; plane 0, row 0
    mov  cl, 4                      ; the nibble shift, for the whole routine

.row:
    mov  ch, [bp+12]                ; one output byte per 8 pixels of the span
.byte:
    ; --- the LEFT four pixels, from two source bytes ----------------------
    mov  bl, [si]
    xor  bh, bh
    add  bx, bx                     ; a word table
    add  bx, [bp+10]
    mov  ax, [bx]
    mov  bl, [si+1]
    xor  bh, bh
    add  bx, bx
    add  bx, [bp+10]
    add  ax, ax
    add  ax, ax                     ; each plane's fragment up one 2-bit place
    or   ax, [bx]
    push ax                         ; accA

    ; --- the RIGHT four pixels --------------------------------------------
    mov  bl, [si+2]
    xor  bh, bh
    add  bx, bx
    add  bx, [bp+10]
    mov  ax, [bx]
    mov  bl, [si+3]
    xor  bh, bh
    add  bx, bx
    add  bx, [bp+10]
    add  ax, ax
    add  ax, ax
    or   ax, [bx]                   ; accB
    pop  dx                         ; accA
    add  si, 4

    ; --- accA's nibble p and accB's nibble p are plane p's eight bits -----
    mov  bl, dl
    shl  bl, cl                     ; accA nibble 0 into the high half
    mov  bh, al
    and  bh, 0x0F
    or   bl, bh
    mov  [di], bl                   ; plane 0

    mov  bl, dl
    and  bl, 0xF0
    mov  bh, al
    shr  bh, cl
    or   bl, bh
    mov  [di + PMC_PL_STEP], bl     ; plane 1

    mov  bl, dh
    shl  bl, cl
    mov  bh, ah
    and  bh, 0x0F
    or   bl, bh
    mov  [di + 2*PMC_PL_STEP], bl   ; plane 2

    mov  bl, dh
    and  bl, 0xF0
    mov  bh, ah
    shr  bh, cl
    or   bl, bh
    mov  [di + 3*PMC_PL_STEP], bl   ; plane 3

    inc  di
    dec  ch
    jnz  .byte

    add  si, [bp-2]                 ; on to the next row of each
    add  di, [bp-4]
    dec  word [bp+8]                ; the row count, in the caller's own slot
    jnz  .row
.done:
    pop  bx
    pop  di
    pop  si
    add  sp, 4                      ; drop the two locals - the ARITHMETIC
                                    ; inverse of the `sub sp, 4` above, and
                                    ; not `mov sp, bp`: tools/stkbalance.py
                                    ; walks add/sub and cannot follow a load,
                                    ; so the tidier spelling reads to it as a
                                    ; `ret` at depth +2
    pop  bp
    ret

; -----------------------------------------------------------------------------
; _pmc_pack_1 - void pmc_pack_1(const unsigned char *band, unsigned char *bits,
;                               int rows, const unsigned char *mono2, int y0,
;                               int cols)
;
; The packed 4bpp band -> the 1bpp band OSAPI_GFX_BLIT1 takes: bit 7 leftmost,
; 1 = lit, PMC_PL_STRIDE bytes a row, in final screen polarity.
;
; in:  [bp+4]  band   rows of PMC_BAND_ROW packed bytes
;      [bp+6]  bits   rows * PMC_PL_STRIDE bytes of scratch
;      [bp+8]  rows   (decremented in place)
;      [bp+10] mono2  pmc_mono2[2][256]: [parity][packed byte] -> two 1bpp
;                     bits, bit 1 the LEFT pixel. The parity half resolves the
;                     50% dither class against the checkerboard, so this loop
;                     never tests a class and never tests a column
;      [bp+12] y0     the band's first row ON THE SCREEN FIELD, which is what
;                     the checkerboard's parity has to follow: on CGA the band
;                     samples alternate SOURCE rows but draws consecutive
;                     SCREEN rows, and it is the screen the dither is seen on
;      [bp+14] cols   output bytes a row, as _pmc_pack_pl's
;
; Grey rounds to black on a 1bpp adapter (SPEC.md 39.4), so the class table -
; not a threshold - is what makes the maze, the dots and four different ghosts
; distinguishable there. Whether it succeeds is a look question and the answer
; is a screendump on VIDEO=cga and VIDEO=herc.
; -----------------------------------------------------------------------------
_pmc_pack_1:
    push bp
    mov  bp, sp
    sub  sp, 4                      ; [bp-2] source skip a row, [bp-4] dest
    push si
    push di
    push bx
    cld

    mov  ax, [bp+14]                ; cols
    or   ax, ax
    jle  .done
    cmp  word [bp+8], 0
    jle  .done
    mov  dx, ax
    add  ax, ax
    add  ax, ax
    mov  bx, PMC_BAND_ROW
    sub  bx, ax
    mov  [bp-2], bx
    mov  bx, PMC_PL_STRIDE
    sub  bx, dx
    mov  [bp-4], bx

    mov  si, [bp+4]
    mov  di, [bp+6]
    mov  dx, [bp+12]                ; DL carries the screen row's parity

.row:
    mov  bx, [bp+10]
    test dl, 1
    jz   .even
    add  bx, 256                    ; the odd rows' half of the pair table
.even:
    mov  ch, [bp+14]
.byte:
    mov  al, [si]                   ; four source bytes = eight pixels
    xlatb
    mov  dh, al
    mov  al, [si+1]
    xlatb
    shl  dh, 1
    shl  dh, 1
    or   dh, al
    mov  al, [si+2]
    xlatb
    shl  dh, 1
    shl  dh, 1
    or   dh, al
    mov  al, [si+3]
    xlatb
    shl  dh, 1
    shl  dh, 1
    or   dh, al
    mov  [di], dh
    inc  di
    add  si, 4
    dec  ch
    jnz  .byte

    add  si, [bp-2]                 ; on to the next row of each
    add  di, [bp-4]
    inc  dl                         ; the next screen row, the other parity
    dec  word [bp+8]
    jnz  .row
.done:
    pop  bx
    pop  di
    pop  si
    add  sp, 4                      ; drop the two locals - the ARITHMETIC
                                    ; inverse of the `sub sp, 4` above, and
                                    ; not `mov sp, bp`: tools/stkbalance.py
                                    ; walks add/sub and cannot follow a load,
                                    ; so the tidier spelling reads to it as a
                                    ; `ret` at depth +2
    pop  bp
    ret

; -----------------------------------------------------------------------------
; _pmc_sprite - void pmc_sprite(const unsigned char *src,
;                               const unsigned char *pal4,
;                               unsigned char *dst, int sinc, int rows,
;                               int flags, const unsigned char *brev,
;                               const unsigned char *zmask)
;
; One 16x16 sprite merged OVER a band that already holds its tiles, which is
; the order the arcade board composites in (pacman.c's renderer draws the tile
; layer then the sprite layer) and the only order that lets colour index 0 mean
; "leave what is underneath alone".
;
; in:  [bp+4]  src    the sprite's first SOURCE ROW - four bytes, four 2-bit
;                     pixels a byte, the leftmost pixel in the HIGH bits
;      [bp+6]  pal4   this sprite's colour block: four nibbles, index 0 being
;                     the transparent one, which is never looked up
;      [bp+8]  dst    the band byte holding the sprite's leftmost pixel
;      [bp+10] sinc   SIGNED bytes from one drawn row's source to the next:
;                     +-4 for every row, +-8 for the CGA layout's alternate
;                     rows, and NEGATIVE for flipy - so the vertical flip costs
;                     no test at all
;      [bp+12] rows   band rows to write (decremented in the caller's slot)
;      [bp+14] flags  bit 0: that first pixel lands on the LOW nibble
;                     bit 1: flipx
;      [bp+16] brev   pmc_brev[256]: a source byte with its four 2-bit pixel
;                     fields reversed. flipx is the four bytes walked BACKWARDS
;                     and each one passed through this table, which is why
;                     there is one pixel loop below and not two
;      [bp+18] zmask  pmc_zmask[256], THE ROW MERGE'S TABLE, or 0 for the plain
;                     alternate-row sample. Used only where the caller asks -
;                     the CGA layout - and ignored at rowstep 1 by the caller
;                     passing 0 there (SPEC.md 91)
; out: 16 pixels a row merged into the band, and DS/ES/DF untouched
;
; -----------------------------------------------------------------------------
; THE ROW MERGE REACHES THE SPRITE LAYER TOO, and it is the same sentence
; `_pmc_tile` above implements: at rowstep 2 the screen has half the rows the
; field has, so a drawn row takes the DROPPED row's pixel wherever its own is
; index 0 -
;
;     merged = even | (odd & zmask[even])
;
; - which for a sprite is exactly "index 0 is transparent" one level up: a
; sprite pixel that shows the band keeps showing it only if the dropped row is
; transparent there as well. What it buys is the SILHOUETTE. Sampling alternate
; rows of Pac-Man's 16x16 takes the top and bottom caps of the circle away
; (his widest sprite goes from a disc to a flat-topped blob) and thins every
; ghost's fringe; the merge is the union of the pair, so the cap survives and
; the mouth - a WEDGE of transparency, several rows deep - still reads as open.
;
; THE PARTNER ROW IS ALWAYS THE NEXT ONE DOWN THE SPRITE, whichever way the
; rows are walked: `sinc` is negative for flipy, so `zoff = sinc >> 1` is the
; ±4 bytes to it and no test for the flip is needed here either. What the
; routine does NOT do is bound it - source row 15 (or, flipped, row 0) has no
; partner inside the sprite - so THE CALLER splits that one row off and asks
; for it with `zmask` = 0. pmc_draw.c's `pmc_band_sprites` is that caller and
; says so; passing 0 costs nothing, because a row merged with itself is the
; sample (`even | (even & zmask[even])` is `even`, the table being 0 in every
; field `even` is not).
;
; It costs ONE test a source byte on the arm that does NOT merge - four a row,
; ~2.8% of a 16-pixel row against the ~9% the merge itself costs - which is
; why there is one loop here and not two: the loop body is four PMCPX
; expansions and a second copy of it would cost more image than the test costs
; time (SPEC.md 91's measured frame is the arithmetic).
;
; -----------------------------------------------------------------------------
; IT NEVER CLIPS, AND THAT IS PMC_BAND_PAD'S WHOLE PURPOSE. A test of two
; bounds per pixel is four instructions on the hottest loop in the package -
; ~2,000 of them a frame - to catch a case the band's own slack absorbs for
; free (see PMC_BAND_ROW above). The caller's only duty is that the span it is
; composing into really is the widened one pmc_flush_laid guarantees.
; -----------------------------------------------------------------------------

; One pixel: rotate the working byte's top two bits down, look the colour up,
; and merge it into the current nibble. A transparent pixel still advances the
; nibble - it is a HOLE in the sprite, not the end of it.
%macro PMCPX 0
    rol  dh, 1
    rol  dh, 1
    mov  al, dh
    and  al, 3
    jz   %%adv                  ; index 0: the band keeps what it had
    xlatb                       ; bx = pal4, so al = the VGA colour nibble
    test dl, dl
    jnz  %%lo
    mov  ah, [di]
    and  ah, 0x0F
    shl  al, cl                 ; cl = 4 throughout
    or   al, ah
    mov  [di], al
    jmp  short %%adv
%%lo:
    mov  ah, [di]
    and  ah, 0xF0
    or   al, ah
    mov  [di], al
%%adv:
    xor  dl, 1
    jnz  %%same                 ; it was the high nibble: same byte
    inc  di
%%same:
%endmacro

_pmc_sprite:
    push bp
    mov  bp, sp
    sub  sp, 8                  ; [bp-2] src row, [bp-4] dst row, [bp-6] step,
                                ; [bp-8] the merge's ±4 to the dropped row
    push si
    push di
    push bx
    cld

    cmp  word [bp+12], 0
    jle  .done

    mov  ax, [bp+10]            ; sinc: ±4 a row, ±8 on the CGA layout...
    sar  ax, 1                  ; ...so half of it is the ±4 to the row the
    mov  [bp-8], ax             ; layout drops, flip and all

    mov  ax, [bp+4]
    mov  bx, 1                  ; the source BYTE step, forwards
    test byte [bp+14], 2
    jz   .fwd
    add  ax, 3                  ; flipx starts at the row's last byte...
    mov  bx, -1                 ; ...and walks back
.fwd:
    mov  [bp-2], ax
    mov  [bp-6], bx
    mov  ax, [bp+8]
    mov  [bp-4], ax

.row:
    mov  si, [bp-2]
    mov  di, [bp-4]
    mov  bx, [bp+6]             ; pal4, held in BX for xlatb
    mov  dl, [bp+14]
    and  dl, 1                  ; the starting nibble phase
    mov  cx, 0x0404             ; ch = four source bytes, cl = the shift
.byte:
    mov  al, [si]
    cmp  word [bp+18], 0        ; the CGA row merge, or the plain sample
    je   .nomrg
    push bx
    mov  bx, [bp+18]
    xlatb                       ; 0b11 in every field this row leaves at 0
    mov  bx, [bp-8]
    and  al, [bx+si]            ; ...where the dropped row's pixel shows
    or   al, [si]
    pop  bx
.nomrg:
    test byte [bp+14], 2
    jz   .noflip
    push bx
    mov  bx, [bp+16]            ; pmc_brev - the four pixels reversed
    xlatb
    pop  bx
.noflip:
    add  si, [bp-6]
    mov  dh, al                 ; the working byte
    PMCPX
    PMCPX
    PMCPX
    PMCPX
    dec  ch
    jnz  .byte

    mov  ax, [bp-2]
    add  ax, [bp+10]            ; sinc is signed: flipy walks the rows back
    mov  [bp-2], ax
    add  word [bp-4], PMC_BAND_ROW
    dec  word [bp+12]
    jnz  .row
.done:
    pop  bx
    pop  di
    pop  si
    add  sp, 8                  ; the ARITHMETIC inverse of the `sub sp, 8`,
                                ; never `mov sp, bp` - tools/stkbalance.py
                                ; walks add/sub and cannot follow a load
    pop  bp
    ret

section .text
