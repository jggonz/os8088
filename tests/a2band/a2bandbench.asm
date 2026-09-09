; =============================================================================
; os8088 - tests/a2band/a2bandbench.asm
;
; A2BANDBENCH: what does the Apple II's row composer cost?
; apps/apple2/a2band.inc turns a run of Apple cells into a 1bpp band that ONE
; OSAPI_GFX_BLIT1 puts down (docs/APPLE2-SPEC.md section 7.3), and every
; number in that document's cost table (section 7.9) and its fullscreen tier
; table (7.8) rests on what a cell of that compose costs. PERFORMANCE.md
; rule 4 says measure before quoting, and this is the measurement: the SAME
; a2band.inc the package ships is %included here and timed with
; tests/benchlib.inc, on the harness that priced the C64's composer
; (tests/c64band) and RUNCPM's before it (PERFORMANCE.md Set 65).
;
; **MEASURE THE BAND BEFORE BELIEVING A PER-CELL GUESS.** RUNCPM's first row
; composer was 306 us a cell against a model that had guessed 40, and this
; port's composers are SHIFT-ACCUMULATOR composers - a class this tree has
; never measured, because the Apple's cell is SEVEN pixels and every other
; composer in the tree works on eight.
;
; Nothing here ships: `make a2bandbench` builds build/a2band.img and `all`
; does not.
;
;   make a2bandbench
;   make test TESTAPPS=build/a2band.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
;   ...double-click Disk B, A2BANDBENCH, click the window; read the counts
;   column: one -icount count is 0.359 ms of real XT (PERFORMANCE.md Part 4)
;
; THE ROWS, and why each:
;
;   FONT_RUN 40 aligned     the bar - forty cells lettered by the kernel on
;                           its own EIGHT-pixel grid, taken in the same run
;   BANDTEXT 5 groups       the compose alone, drawing nothing: 40 cells, the
;                           whole Apple row, and the number section 7.9's
;                           model quotes per cell
;   BANDLORES 5 / 1 group   ...and the LO-RES composer over the same span: two
;                           nibbles a byte through the luminance ladder, the
;                           same a2_pack under it
;   BANDHIRES 5 / 1 group   ...and the HI-RES one: forty source bytes a SCAN
;                           LINE through the 7-bit reverse table, up to EIGHT
;                           lines a call, which is why its per-call figure is
;                           the odd one out and its per-SOURCE-BYTE figure is
;                           not
;   BANDHIRES 5 grp x1ln    ...and the same span for ONE scan line, which is
;                           what a single-line HPLOT asks for. Hi-res is the
;                           mode the damage model marks a LINE at a time, so
;                           this composer takes a range and the other two do
;                           not - and one measurement of eight lines cannot
;                           price one
;   per source byte         each composer's per-call figure divided by the
;                           forty source bytes a five-group call consumes -
;                           the figure APPLE2-SPEC section 7.9's cost table is
;                           written from, printed rather than derived by hand
;   BLIT1 640x16 stride80   the DOUBLED emit, which is what full screen at 2x
;                           puts down for each character row (7.8)
;   BANDTEXT 1 group        ...and the floor - EIGHT cells, because eight
;                           cells is fifty-six bits is seven bytes and the
;                           group is the composer's unit (7.3). A one-cell
;                           poke composes one group
;   BLIT1 320x8 stride40    the emit alone, the band's real stride
;   BAND 5 groups           what a changed row actually pays, compose + blit
;   BAND 1 group            ...and what one changed cell pays
;   ROWSPAN 40 equal        the compare that decides NOTHING is drawn - the
;                           common case, and the one that must be cheap. ONE
;                           SCAN LINE, which is where this parts company with
;                           the C64's eight-row compare (7.7)
;   ROWSPAN 40 differing    ...and the same compare when it has to find the
;                           first and last differing byte, forward and back
;   ROWCOPY 40              bringing the shadow up to date with what was drawn
;   ROWSIG 40               one row's source signature: the k = 1..23 shift
;                           test asks for 24 of these before it decides (7.7)
;   ROWFLASH 40             the flash scan - asked once per row COMPOSE, and
;                           it is what makes the phase flip cost what it costs
;   BAND_X2 8r x 40b        fullscreen's pixel doubling, a whole character
;                           row of it (7.8)
;   BAND_X2 1r x 7b         ...and one RUN of one scan line of it, which is
;                           what a single-scan-line HPLOT asks for. The
;                           routine is called per RUN, so one shape cannot
;                           price it
;
; AND THE IDENTITY ROWS, a correctness test in a benchmark's clothes: a line
; of Apple screen codes composed by the SHIPPING a2_band_text and blitted
; straight to the glass; under it the same line with every other cell INVERSE,
; so the per-cell XOR mask is on the glass; and under that the same line
; FLASHING with the phase swapped, which is the same mask reached the other
; way. A screendump is the assertion.
;
; WHY THE KERNEL'S FONT AND NOT THE CHARACTER-GENERATOR ROM: this bench is a
; package of its own and does not read APPLE2.ROM. What is being timed is the
; composer's INNER LOOP - a table lookup, a mask, eight stores, then the seven
; shift-accumulator packs - and that costs the same whatever the glyph bytes
; are. The ROM's own bytes are on the glass in the package, in QEMU, where
; they belong.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'A2BANDBENCH', ab_entry

; a2band.inc opens `section .bss` for its scratch; a plain package keeps
; everything in .text and bases its bss on os88_image_end. So .bss is declared
; HERE, first, as align=1 following .text: it then starts exactly at
; os88_image_end and a2band's scratch and this harness's own bss share it.
section .bss align=1 follows=.text
section .text

AB_CELLS   equ 40                 ; the Apple's row: 40 cells of SEVEN pixels
AB_GROUPS  equ 5                  ; ...which is five groups of eight
AB_STRIDE  equ 40                 ; a2_band_text's stride: A2_BSTRIDE, always
AB_W       equ 320                ; the band, letterbox included
AB_N       equ 8                  ; iterations a row (bandbench's)
AB_SPANOFF equ 8 * 7               ; ab_spanck's slice: cells 8..15 of a
AB_SPANLEN equ 8 * 7               ; composed scan line, seven bytes a cell
AB_FSXW1   equ 70                 ; a 1bpp foreign scan line: 560 px doubled
                                  ; from the Apple's 280, which is 70 bytes
A2_ROWS_B  equ 24                 ; the frame, in character rows...
A2_LINES_B equ 192                ; ...and in scan lines
AB_SCROLL_L equ 23 * 8            ; A SCROLL'S OWN CHANGE SET: the ROM copies
                                  ; 23 character rows up one and clears the
                                  ; 24th, so 184 scan lines hold different
                                  ; bytes than they did and the 8 vacated ones
                                  ; are drawn whatever the compare says
                                  ; (APPLE2-SPEC section 13.2)

; -----------------------------------------------------------------------------
; ab_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
ab_entry:
    push si
    mov ax, ab_bss_end
    cmp ax, os88_image_end + AB_BSS_TOTAL
    ja .refuse
    call ab_setup
    call ab_hint
    mov si, ab_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [ab_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; ab_setup - the decoded character generator, the screen rows and the doubling
;            table
; -----------------------------------------------------------------------------
ab_setup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; THE KERNEL'S 8x8 FONT STANDS IN FOR THE CHARACTER GENERATOR. It answers
    ; DX:SI = the table and AL = the first code it covers. a2_band_text reads
    ; _a2_chr[(byte & 0x3F) * 8 + row] as a SEVEN-BIT value, so the glyphs are
    ; copied in with bit 7 masked off - which is also exactly what the
    ; package's own a2_chargen does to the ROM.
    call OSAPI_FONT_GLYPHS
    mov [ab_gfirst], al
    mov es, dx
    mov di, _a2_chr
    mov cx, 512
.g:
    mov al, [es:si]
    inc si
    and al, 0x7F
    mov [di], al
    inc di
    loop .g

    ; the screen row: forty Apple screen codes. NORMAL is `ascii | 0x80` and
    ; the glyph index is `byte & 0x3F`, so a byte of `0x80 | (c - first)`
    ; lands on the glyph the kernel would have drawn for c.
    mov di, ab_mat
    mov cx, AB_CELLS
    mov si, ab_seed
.c:
    mov al, [si]
    or al, al
    jnz .have
    mov si, ab_seed
    mov al, [si]
.have:
    sub al, [ab_gfirst]
    and al, 0x3F
    or al, 0x80                     ; NORMAL
    mov [di], al
    ; ...and a second row in which every other cell is INVERSE ($00-$3F),
    ; which is the per-cell XOR mask the identity line puts on the glass
    and al, 0x3F
    test cl, 1
    jz .inv
    or al, 0x80
.inv:
    mov [di+AB_CELLS], al
    inc si
    inc di
    loop .c

    ; THE 7-BIT REVERSE TABLE, built here exactly as os88_main builds it: bit
    ; b of the source becomes bit 6-b, which is what turns a hi-res byte (bit
    ; 0 leftmost) into the framebuffer's order (MSB first).
    xor si, si
.rev:
    xor al, al
    mov cx, 7
    mov bx, si
.revb:
    shr bl, 1
    rcl al, 1
    loop .revb
    mov bx, si
    mov [_a2_rev+bx], al
    inc si
    cmp si, 128
    jb .rev

    ; ...and the lo-res pattern table: eight dark colours and eight lit.
    ; THAT IS A DELIBERATELY SIMPLER TABLE AND NOT THE PACKAGE'S LADDER, which
    ; is nine lit and seven dark and disagrees with this stand-in on colours 5
    ; and 8. Nothing here is a statement about the ladder - what is being timed
    ; is two nibble reads, a table index and eight stores a cell, and that
    ; costs the same whatever the sixteen entries hold. The ladder's own gate
    ; is `tools/a2ref.py --lumcheck`, over all 256 ordered pairs.
    mov si, 16
.lop:
    dec si
    mov al, 0x7F
    cmp si, 8
    jae .lit
    xor al, al
.lit:
    mov bx, si
    mov [_a2_lopat+bx], al
    or si, si
    jnz .lop

    ; ...and the HI-RES source, eight scan lines $400 apart, so the composer
    ; is not timed on a page of zeroes. What it costs is a mask, a table read
    ; and a store a cell, which the data cannot change - but a bench whose
    ; input is degenerate is one nobody can check by looking at it.
    mov di, ab_hsrc
    mov dx, 8
    xor ah, ah
.hl:
    push di
    mov cx, AB_CELLS
    mov si, ab_seed
.hb:
    mov al, [si]
    inc si
    xor al, ah                      ; ...AND BIT 7 ALTERNATES CELL BY CELL, so
    xor ah, 0x80                    ; BOTH of MII's colour sets are in the
    mov [di], al                    ; fixture. The seed is ASCII and had bit 7
    inc di                          ; clear in all forty cells, which left the
    loop .hb                        ; half-dot shift's arm untaken - and the
                                    ; span self-check below is about exactly
                                    ; the cross-cell state that arm feeds
    pop di
    add di, 0x400
    dec dx
    jnz .hl

    call _a2_x2init                 ; the 512-byte doubling table, once,
                                    ; outside every timed row
    call _a2_fsx_init               ; ...and MII's artifact rule, flattened
                                    ; into a2_hitab. Both are per-launch
                                    ; tables and neither belongs in a timed
                                    ; row (APPLE2-SPEC section 13)

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ab_hint:
    push si
    call bl_blank
    mov si, ab_s_title
    call bl_sline
    call bl_head
    mov si, ab_s_hint
    call bl_sline
    pop si
    ret

ab_paint:
    call bl_paint
    ret

; THE CLIP IS ARMED HERE, because this is W_ONKEY and not W_PAINT. The kernel
; arms the damage clip for a paint and for nothing else (SPEC.md 11.3), and
; everything below draws.
ab_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [ab_win], si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    mov bl, al
    or bl, 0x20
    cmp bl, 'i'
    je .ident
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.ident:
    xor byte [ab_identon], 1        ; ...and the identity block is a KEY now
    call ab_repaint                 ; (see ab_repaint)
    jmp short .out
.run:
    call ab_run
    call ab_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ab_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [ab_win], si
    mov bx, si
    call OSAPI_WM_CLIP_SET      ; W_ONCLICK: ours to arm (11.3)
    jc .out
    call ab_run
    call ab_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ab_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [ab_win]
    call OSAPI_WM_CONTENT
    mov [ab_cx], ax
    mov [ab_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [ab_cx]
    mov bx, [ab_cy]
    mov cx, ax
    add cx, [ab_cw]
    dec cx
    mov dx, bx
    add dx, [ab_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [ab_win]
    call bl_paint
    ; THE IDENTITY BLOCK IS BEHIND `i` AND IS OFF BY DEFAULT, and that is the
    ; foreign rows' own doing: the report is 49 rows now and the four
    ; full-width picture rows sit on the last three of them, so what wave 5's
    ; measurement most needed to read was underneath a picture. It is a
    ; correctness assertion and not a measurement - a screendump IS the
    ; assertion - so it costs nothing to ask for it.
    cmp byte [ab_identon], 0
    je .noident
    call ab_ident
.noident:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the measured bodies - each draws at ([ab_bx], [ab_by]), the last content
; rows, so nothing scribbles over a result line
; =============================================================================

; --- the bar: 40 cells of 8x8, byte-aligned ----------------------------------
ab_b_run40:
    mov cx, [ab_bx]
    mov dx, [ab_by]
    mov si, ab_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; --- a2_band_text(dst, 0, [ab_last], DS, ab_mat, fmask) ----------------------
; The composer takes a GROUP span, so the row is driven by [ab_last]: 4 for
; the whole line and 0 for one group of eight cells.
ab_b_band:
    push word [ab_fmask]
    mov ax, ab_mat
    push ax
    mov ax, ds
    push ax                         ; mseg: this package's own segment
    push word [ab_last]
    xor ax, ax
    push ax                         ; g0
    mov ax, ab_band
    push ax                         ; dst
    call _a2_band_text
    add sp, 12
    ret

; --- a2_band_lores(dst, 0, [ab_last], DS, ab_mat) ---------------------------
; The lo-res composer over the SAME group span as the text one, off the same
; forty source bytes: a lo-res byte is two colour nibbles and a text byte is a
; screen code, and the composer does not care which - what it costs is two
; table reads and eight stores a cell, then the shared a2_pack.
ab_b_lores:
    mov ax, ab_mat
    push ax
    mov ax, ds
    push ax
    push word [ab_last]
    xor ax, ax
    push ax
    mov ax, ab_band
    push ax
    call _a2_band_lores
    add sp, 10
    ret

; --- a2_band_hires(dst, 0, [ab_last], DS, ab_hsrc, s0, nlines) --------------
; EIGHT SCAN LINES A CALL, $400 apart, which is the interleave the composer
; walks itself - so this row's per-call figure covers 320 source bytes where
; the other two cover 40, and the per-source-byte row under it is the one to
; compare across the three.
;
; ...AND ONE SCAN LINE A CALL, which is the shape a single-scan-line HPLOT
; asks for: hi-res is the mode the damage model marks a LINE at a time, so
; this composer takes a range where the other two do not (a2band.inc), and one
; measurement of eight lines cannot price one.
ab_b_hires:
    mov ax, 8                       ; nlines
    push ax
    xor ax, ax                      ; s0
    push ax
    mov ax, ab_hsrc
    push ax
    mov ax, ds
    push ax
    push word [ab_last]
    xor ax, ax
    push ax
    mov ax, ab_band
    push ax
    call _a2_band_hires
    add sp, 14
    ret

ab_b_hires1l:
    mov ax, 1                       ; nlines - ONE scan line
    push ax
    xor ax, ax                      ; s0
    push ax
    mov ax, ab_hsrc
    push ax
    mov ax, ds
    push ax
    push word [ab_last]
    xor ax, ax
    push ax
    mov ax, ab_band
    push ax
    call _a2_band_hires
    add sp, 14
    ret

; --- BLIT1: the emit alone, the whole 320-pixel band at stride 40 ------------
; ES IS SAVED AND PUT BACK: this runs inside a window callback, which the
; kernel enters with ES = KERNEL_SEG (SPEC.md 20.1), and a callback that
; returns with ES on its own segment has broken the contract for everything
; the kernel does next.
ab_b_blit:
    push es
    mov si, ab_band
    mov bp, AB_STRIDE
    mov ax, [ab_bx]
    mov bx, [ab_by]
    mov cx, AB_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    ret

ab_b_cband:
    call ab_b_band
    call ab_b_blit
    ret

; --- BLIT1 at 2x: the DOUBLED band, 640 x 16 at stride 80 -------------------
; What full screen at 2x puts down for one character row (APPLE2-SPEC section
; 7.8). It is a separate row because the cost table prices a blit per CALL and
; this one moves four times the pixels of the row above it: quoting the 320x8
; figure for a 640x16 blit would understate every fullscreen row in the table.
ab_b_blit2:
    push es
    mov si, ab_x2buf
    mov bp, A2_X2STRIDE
    mov ax, [ab_bx]
    mov bx, [ab_by]
    mov cx, AB_W * 2
    mov dx, 16
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    ret

; --- ROWSPAN: the compare that decides whether anything is drawn -------------
ab_b_span:
    mov ax, AB_STRIDE
    push ax
    mov ax, ab_shad
    push ax
    mov ax, ab_band
    push ax
    call _a2_rowspan
    add sp, 6
    ret

; --- ROWCOPY: the shadow brought up to date ----------------------------------
ab_b_copy:
    mov ax, AB_STRIDE
    push ax
    mov ax, ab_band
    push ax
    mov ax, ab_shad
    push ax
    call _a2_rowcopy
    add sp, 6
    ret

; --- ROWSIG: one row's source signature (the shift test asks for 24) ---------
ab_b_sig:
    mov ax, AB_CELLS
    push ax
    mov ax, ab_mat
    push ax
    mov ax, ds
    push ax
    call _a2_rowsig
    add sp, 6
    ret

; --- ROWFLASH: does this row hold a byte in $40-$7F? -------------------------
ab_b_flash:
    mov ax, AB_CELLS
    push ax
    mov ax, ab_mat
    push ax
    mov ax, ds
    push ax
    call _a2_rowflash
    add sp, 6
    ret

; --- BAND_X2: fullscreen's pixel doubling ------------------------------------
; TWO ROWS, because the routine is called PER RUN now (a2band.inc) and one
; measurement of one shape cannot price both ends of it. The whole band -
; 8 rows x 40 bytes, 320 source byte-rows - is what entering full screen
; costs a character row; 1 row x 7 bytes is a single-scan-line HPLOT, which
; is the shape the per-run change exists for. The two together give the call
; floor and the per-byte-row cost, the way BANDTEXT's two rows do.
ab_b_x2:
    mov ax, 8                       ; rows
    push ax
    mov ax, A2_BSTRIDE              ; nbytes
    push ax
    mov ax, ab_band
    push ax
    mov ax, ab_x2buf
    push ax
    call _a2_band_x2
    add sp, 8
    ret

ab_b_x2s:
    mov ax, 1                       ; rows
    push ax
    mov ax, 7                       ; nbytes - one run of one group
    push ax
    mov ax, ab_band
    push ax
    mov ax, ab_x2buf
    push ax
    call _a2_band_x2
    add sp, 8
    ret

; =============================================================================
; THE FOREIGN VIDEO MODE (APPLE2-SPEC section 13), AND THE MEASUREMENT THAT
; DECIDES WHICH OF ITS THREE WRITERS SHIP
;
; `FSXM_VGA13` ships on its COLOUR, which is not in question - a 1bpp window
; cannot express artifact colour at any width. `FSXM_CGA640` and `FSXM_HERC`
; are a SPEED claim and nothing else, and the SPEC states the arithmetic
; against them: 560 x 192 is 13,440 bytes a frame against the windowed
; 7,680, 1.75x the raster work on the slowest machine in the name of going
; faster. So they ship ONLY IF THEY BEAT THE WINDOWED PATH HERE, on the same
; change set, and are cut if they do not.
;
; TWO CHANGE SETS, because one number cannot answer it:
;
;   FRAME   every scan line differs - entering the mode, a HOME, a scroll in
;           a graphics mode, a picture load. The worst case, and the one the
;           1.75x argument is about.
;   ROW     one character row differs - a keystroke, which is what the machine
;           spends almost all of its time doing. The foreign path's span
;           compare answers "nothing moved" for the other 184 scan lines and
;           the windowed path never composes them either, so this is where
;           the two are closest.
;
; WHAT THIS BENCH DOES NOT MODEL, stated because it cuts BOTH ways: -icount
; prices INSTRUCTIONS, not bus contention, so a write into VRAM costs what a
; write into RAM costs here. That is true of every row in this file including
; the BLIT1 ones, which go to the real framebuffer through the kernel - so the
; comparison is like for like, and neither side is being flattered.
; =============================================================================

; --- a2_fsx_row(dst, mode, mseg, moff, line, fmask, cell0, ncells) ----------
; `ncells` cells of ONE scan line of palette indices. The three modes are three
; rows, because they are three phase Bs: a masked glyph row, a colour nibble
; and MII's artifact rule.
;
; AND EACH IS MEASURED AT TWO WIDTHS, which is what turns a per-CALL price into
; a per-CELL one. The composer takes a cell range now (a2fsx.inc), because an
; Applesoft COUT moves one or two cells and composing forty of them is the
; defect PERFORMANCE rule 1 names; a harness cost table that prices every call
; at the forty-cell figure would report the narrowing as free and a bench that
; only ever measured forty cells is what let it be written that way. Forty and
; EIGHT - one group, which is the narrowest span a2_flush's own predicate can
; produce - and the two numbers give the call floor and the slope.
%macro AB_FSXROW 5                  ; mode, moff, line, cell0, ncells
    mov ax, %5
    push ax                         ; ncells
    mov ax, %4
    push ax                         ; cell0
    xor ax, ax
    push ax                         ; fmask
    mov ax, %3
    push ax                         ; line
    mov ax, %2
    push ax                         ; moff
    mov ax, ds
    push ax                         ; mseg
    mov ax, %1
    push ax                         ; mode
    mov ax, ab_fsxrow
    push ax                         ; dst
    call _a2_fsx_row
    add sp, 16
%endmacro

ab_b_fsxrow_t:
    AB_FSXROW 0, ab_mat, 0, 0, 40
    ret

ab_b_fsxrow_l:
    AB_FSXROW 1, ab_mat, 0, 0, 40
    ret

ab_b_fsxrow_h:
    AB_FSXROW 2, ab_hsrc, 0, 0, 40
    ret

; ...AND THE SAME THREE OVER ONE GROUP, which is what a keystroke owes. The
; cell0 is 8 rather than 0 on purpose: a hi-res range that does not start at
; the row's first cell has to take its `b0` and its parity from the SOURCE,
; and a bench that always started at 0 would never execute that seeding.
; --- THE SPAN SELF-CHECK, WHICH IS NOT A TIMED ROW --------------------------
; A CELL RANGE MUST PRODUCE THE BYTES THE WHOLE ROW WOULD, and in hi-res that
; is not free: artifact colour is decided by a pixel's NEIGHBOURS, so a range
; starting at cell 8 has to seed `b0` from the byte before it and `odd` from
; cell 8's own parity, and a range ENDING short of cell 39 still has a next
; byte to read. Get any of the three wrong and the picture is right at 40
; cells and wrong at 8 - which is to say right the first time the mode is
; entered and wrong on every keystroke after it, and then RECORDED in the
; foreign shadow, so it stays for the session.
;
; Nothing else in this tree can see that: the host harness models a2_fsx_row
; in C rather than running it, tools/a2ref.py compares the WINDOWED composer,
; and a screendump of artifact colour is not a thing anyone can check by
; looking. So the bench, which is the only harness that runs this routine on
; an x86 at all, asserts it before it times it.
ab_spanck:
    push es
    push ds
    pop es
    cld
    AB_FSXROW 2, ab_hsrc, 0, 0, 40  ; the whole row...
    mov si, ab_fsxrow + AB_SPANOFF  ; ...cells 8..15 of it, kept
    mov di, ab_fsxspan
    mov cx, AB_SPANLEN
    rep movsb
    mov di, ab_fsxrow               ; ...and the row scrubbed, so a range that
    mov cx, A2_FSXW                 ; wrote nothing cannot pass by leaving the
    mov al, 0xEE                    ; previous answer where it was
    rep stosb
    AB_FSXROW 2, ab_hsrc, 0, 8, 8   ; the same eight cells, narrowly
    mov si, ab_fsxspan
    mov di, ab_fsxrow + AB_SPANOFF
    mov cx, AB_SPANLEN
    repe cmpsb
    mov si, ab_s_spanok
    je .ok
    mov si, ab_s_spanbad
.ok:
    pop es
    call bl_sline
    ret

ab_b_fsxrow_t8:
    AB_FSXROW 0, ab_mat, 0, 8, 8
    ret

ab_b_fsxrow_l8:
    AB_FSXROW 1, ab_mat, 0, 8, 8
    ret

ab_b_fsxrow_h8:
    AB_FSXROW 2, ab_hsrc, 0, 8, 8
    ret

; --- a2_fsx_put(fbseg, fboff, shseg, shoff, src, n) -------------------------
; The span compare one geometry along. TWO rows per width, because the
; answers are different in kind: a line that has not changed costs one
; `repe cmpsb` and writes nothing, and that is 184 of the 192 on an ordinary
; keystroke.
;
; THE DIFFERING ROW POKES BOTH ENDS OF THE SHADOW FIRST, and it has to: the
; routine's own job is to bring the shadow up to date, so without the poke the
; second iteration and every one after it would take the EQUAL path and the
; row would measure the thing above it. Two `mov byte [mem], imm` is about
; eight clocks against a 280-byte compare and two 280-byte copies.
%macro AB_FSXPUT 2                  ; src, n
    mov ax, %2
    push ax                         ; n
    mov ax, %1
    push ax                         ; src
    mov ax, ab_fsxsh
    push ax                         ; shoff
    mov ax, ds
    push ax                         ; shseg
    mov ax, ab_fsxfb
    push ax                         ; fboff
    mov ax, ds
    push ax                         ; fbseg - the bench's own bss stands in
    call _a2_fsx_put                ; for the foreign framebuffer (see above)
    add sp, 12
%endmacro

ab_b_fsxput_d:
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + A2_FSXW - 1], 0xFE
    AB_FSXPUT ab_fsxrow, A2_FSXW
    ret

ab_b_fsxput_e:
    AB_FSXPUT ab_fsxrow, A2_FSXW
    ret

ab_b_fsxput1_d:
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + AB_FSXW1 - 1], 0xFE
    AB_FSXPUT ab_x2buf, AB_FSXW1
    ret

ab_b_fsxput1_e:
    AB_FSXPUT ab_x2buf, AB_FSXW1
    ret

; --- THE WHOLE-FRAME AND ONE-ROW BODIES -------------------------------------
; The three paths, each doing the SAME visible work: 192 scan lines, or the
; eight of one character row.
;
; THE SHADOW IS ONE LINE AND IS REUSED FOR ALL 192, and that is a statement
; about addresses and not about work: the compare, the two copies and the
; compose are the same length whichever line's shadow they are handed, and an
; 53,760-byte buffer in a bench package's bss is a launch this OS refuses.
; THE COUNTER IS IN MEMORY AND NOT IN A REGISTER, and it is not tidiness:
; a2band.inc's and a2fsx.inc's routines are cdecl and cdecl here preserves BP,
; DS, SS:SP and DF AND NOTHING ELSE (SPEC.md 73.3) - `_a2_band_text` loads BL
; with the flash mask on its second instruction. A loop counter in BX
; therefore never comes back, and what that looks like is not a wrong number:
; the first cut of these four bodies HUNG, and a bench that hangs looks
; exactly like one that is merely slow.
ab_b_win2x:
    mov word [ab_lc], A2_ROWS_B
.r:
    call ab_b_band                  ; the windowed composer, five groups
    call ab_b_x2                    ; ...doubled, a whole character row
    call ab_b_blit2                 ; ...and down in ONE blit1, 640x16
    dec word [ab_lc]
    jnz .r
    ret

ab_b_fsx13:
    mov word [ab_lc], A2_LINES_B
.l:
    AB_FSXROW 2, ab_hsrc, 0, 0, 40
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + A2_FSXW - 1], 0xFE
    AB_FSXPUT ab_fsxrow, A2_FSXW
    dec word [ab_lc]
    jnz .l
    ret

; --- A SCROLL, WHICH IS THE ORDINARY EVENT THE FOREIGN FRAME HAS NO ANSWER
; FOR, MEASURED RATHER THAN ARGUED ABOUT.
;
; The windowed flush answers a scroll with ONE os88_gfx_scroll and the vacated
; row (a2_shift_test proves it, forty source bytes a row, and the kernel moves
; the pixels): APPLE2-SPEC section 7.9.1 measured 34 ms. THE FOREIGN FRAME HAS
; NO COUNTERPART - a2_fsx_frame starts at a2_dirty_scan and there is no shift
; block below it - so a RETURN typed at the bottom line of an Applesoft
; session recomposes and rewrites 184 scan lines, and this row is what that
; costs. TEXT and not hi-res on purpose: a scroll is a text-mode event, and it
; is a2_shift_test's own gate (`a2_v_text`) on the windowed side.
;
; It is a row and not a fix because a fix is a second damage model - a foreign
; a2_shsrc, a foreign mode key, a foreign phase and a mover for the framebuffer
; and the shadow - and section 13.2 records the number and the omission
; instead. What the row makes impossible is the omission being SILENT.
ab_b_fsx13sc:
    mov word [ab_lc], AB_SCROLL_L
.l:
    AB_FSXROW 0, ab_mat, 0, 0, 40
    mov byte [ab_fsxsh], 0xFE       ; ...and every line DIFFERS, which is what
    mov byte [ab_fsxsh + A2_FSXW - 1], 0xFE  ; a scrolled row is
    AB_FSXPUT ab_fsxrow, A2_FSXW
    dec word [ab_lc]
    jnz .l
    ret

ab_b_fsx1bpp:
    mov word [ab_lc], A2_ROWS_B
.r:
    call ab_b_band                  ; the SAME composer and the SAME doubler:
    call ab_b_x2                    ; a 1bpp foreign frame is the windowed
    dec word [ab_lc]                ; band at the windowed magnification
    jnz .r
    mov word [ab_lc], A2_LINES_B
.l:
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + AB_FSXW1 - 1], 0xFE
    AB_FSXPUT ab_x2buf, AB_FSXW1
    dec word [ab_lc]
    jnz .l
    ret

; ...and the ONE-ROW forms of the three, which is what a keystroke costs.
ab_b_win2x1:
    call ab_b_band
    call ab_b_x2
    call ab_b_blit2
    ret

ab_b_fsx131:
    mov word [ab_lc], 8
.l:
    AB_FSXROW 2, ab_hsrc, 0, 0, 40
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + A2_FSXW - 1], 0xFE
    AB_FSXPUT ab_fsxrow, A2_FSXW
    dec word [ab_lc]
    jnz .l
    ret

; ...AND THE ONE-ROW FORM AS THE PACKAGE ACTUALLY DRAWS IT, which is the row
; the first cut of this bench did not have and the reason the column narrowing
; was written without a number on it. `ROW FSXM_VGA13` above composes all forty
; cells of eight scan lines, and an Applesoft COUT moves ONE cell: a2_fsx_frame
; hands the composer a2_flush's own group span, which is eight cells, and the
; compare the matching byte range.
ab_b_fsx131n:
    mov word [ab_lc], 8
.l:
    AB_FSXROW 2, ab_hsrc, 0, 8, 8
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + AB_SPANLEN - 1], 0xFE
    AB_FSXPUT ab_fsxrow + AB_SPANOFF, AB_SPANLEN
    dec word [ab_lc]
    jnz .l
    ret

ab_b_fsx1bpp1:
    call ab_b_band
    call ab_b_x2
    mov word [ab_lc], 16            ; a character row is SIXTEEN doubled scan
.l:                                 ; lines, and each is its own compare
    mov byte [ab_fsxsh], 0xFE
    mov byte [ab_fsxsh + AB_FSXW1 - 1], 0xFE
    AB_FSXPUT ab_x2buf, AB_FSXW1
    dec word [ab_lc]
    jnz .l
    ret

; -----------------------------------------------------------------------------
; ab_ident - the identity lines: the kernel's own lettering on its EIGHT-pixel
; grid, then the SHIPPING composer's SEVEN-pixel band of the same text under
; it, then the same row with every other cell inverse, then the same row
; flashing with the phase swapped. A screendump is the assertion, and the
; seven-against-eight difference is the point: the two lines are the same
; letters and they are NOT the same width.
; -----------------------------------------------------------------------------
ab_ident:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov cx, [ab_ix]
    mov dx, [ab_iy]
    mov si, ab_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov word [ab_last], AB_GROUPS - 1
    mov word [ab_fmask], 0
    call ab_b_band
    mov si, ab_band
    mov bp, AB_STRIDE
    mov ax, [ab_ix]
    mov bx, [ab_iy]
    add bx, 10
    mov cx, AB_W
    mov dx, 8
    push es
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    mov byte [ab_cf], 0
    jnc .ok
    mov byte [ab_cf], 1
.ok:
    ; ...the alternating-INVERSE row, which exercises the per-cell XOR mask
    push word [ab_fmask]
    mov ax, ab_mat + AB_CELLS
    push ax
    mov ax, ds
    push ax
    mov ax, AB_GROUPS - 1
    push ax
    xor ax, ax
    push ax
    mov ax, ab_band
    push ax
    call _a2_band_text
    add sp, 12
    mov si, ab_band
    mov bp, AB_STRIDE
    mov ax, [ab_ix]
    mov bx, [ab_iy]
    add bx, 20
    mov cx, AB_W
    mov dx, 8
    push es
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es

    ; ...and the FLASHING row: the same cells with the phase swapped, which
    ; is the same mask reached from the other side (section 7.6)
    mov word [ab_fmask], 0x7F
    mov ax, ab_mat + AB_CELLS
    push word [ab_fmask]
    push ax
    mov ax, ds
    push ax
    mov ax, AB_GROUPS - 1
    push ax
    xor ax, ax
    push ax
    mov ax, ab_band
    push ax
    call _a2_band_text
    add sp, 12
    mov word [ab_fmask], 0
    mov si, ab_band
    mov bp, AB_STRIDE
    mov ax, [ab_ix]
    mov bx, [ab_iy]
    add bx, 30
    mov cx, AB_W
    mov dx, 8
    push es
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ab_rowb - one report row whose BODY CONTAINS A BLIT1. If the preflight found
; the kernel refusing bands, the row is not timed at all and says so: a number
; here would be the cost of a refusal wearing the label of a draw.
; -----------------------------------------------------------------------------
ab_rowb:
    cmp byte [ab_blitok], 0
    je ab_norow
    xor al, al
    jmp bl_run                      ; tail call: bl_run returns to our caller

ab_norow:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov si, ab_s_refused
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit
    pop di
    ret

; -----------------------------------------------------------------------------
; ab_perbyte - one DERIVED row: the row just measured, per SOURCE BYTE.
;
; bl_run leaves [bl_lastus] - hundredths of a microsecond per iteration - for
; exactly this. A five-group call consumes forty source bytes in text and
; lo-res and 320 in hi-res, so the divisor is the caller's: SI is the label
; and CX the source bytes that call read. This is the figure APPLE2-SPEC
; section 7.9's cost table is written from, printed rather than derived by
; hand off the column beside it.
; -----------------------------------------------------------------------------
ab_perbyte:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    call bl_div32                   ; ...by the caller's CX
    mov di, BL_C_US
    call bl_usfield
    mov si, bl_s_us
    mov di, BL_C_UNIT
    call bl_lput
    call bl_lcommit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ab_run - the whole report
; =============================================================================
ab_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [ab_win]
    call OSAPI_WM_CONTENT
    mov [ab_cx], ax
    mov [ab_cy], dx
    mov bx, [ab_win]
    call OSAPI_WM_GEOM
    mov [ab_cw], cx
    mov [ab_ch], dx
    mov ax, [ab_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [ab_bx], ax
    mov [ab_ix], ax
    mov ax, [ab_cy]
    add ax, [ab_ch]
    sub ax, 13
    mov [ab_by], ax
    sub ax, 42
    mov [ab_iy], ax

    call bl_blank
    mov si, ab_s_title
    call bl_sline
    call bl_head
    call bl_baseline

    ; --- THE PREFLIGHT: DOES THIS KERNEL DRAW A BAND AT ALL? ---------------
    ; OSAPI_GFX_BLIT1 answers CF = 1 REFUSED with nothing drawn - a broken
    ; argument, or a kern_small kernel that carries the slot without the body.
    ; A measurement of a refusal is not a measurement of the thing refused, so
    ; it is taken ONCE here, outside the timing, and every row whose body
    ; contains a blit prints REFUSED instead of a number.
    mov word [ab_last], AB_GROUPS - 1
    mov word [ab_fmask], 0
    call ab_b_band
    push es
    mov si, ab_band
    mov bp, AB_STRIDE
    mov ax, [ab_bx]
    mov bx, [ab_by]
    mov cx, AB_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    mov byte [ab_blitok], 1
    jnc .bok
    mov byte [ab_blitok], 0
    mov si, ab_s_noblit
    call bl_sline
.bok:

    mov si, ab_s_hdr
    call bl_sline

    mov word [bl_n], AB_N
    mov word [ab_last], AB_GROUPS - 1

    mov word [bl_body], ab_b_run40
    mov si, ab_r_run40
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_band
    mov si, ab_r_band40
    xor al, al
    call bl_run
    mov si, ab_s_perb               ; ...IMMEDIATELY after its own row: the
    mov cx, AB_CELLS                ; derived figure reads [bl_lastus], which
    call ab_perbyte                 ; is whatever bl_run measured LAST

    mov word [bl_body], ab_b_blit
    mov si, ab_r_blit40
    call ab_rowb

    mov word [bl_body], ab_b_cband
    mov si, ab_r_cband40
    call ab_rowb

    mov word [bl_body], ab_b_lores
    mov si, ab_r_lores40
    xor al, al
    call bl_run
    mov si, ab_s_perb
    mov cx, AB_CELLS
    call ab_perbyte

    mov word [bl_body], ab_b_hires
    mov si, ab_r_hires40
    xor al, al
    call bl_run
    mov si, ab_s_perb
    mov cx, AB_CELLS * 8            ; EIGHT scan lines a call
    call ab_perbyte

    mov word [bl_body], ab_b_hires1l
    mov si, ab_r_hires1l
    xor al, al
    call bl_run
    mov si, ab_s_perb
    mov cx, AB_CELLS                ; ...and ONE, which is 40 source bytes
    call ab_perbyte

    mov word [ab_last], 0
    mov word [bl_body], ab_b_band
    mov si, ab_r_band1
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_cband
    mov si, ab_r_cband1
    call ab_rowb

    mov word [bl_body], ab_b_lores
    mov si, ab_r_lores1
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_hires
    mov si, ab_r_hires1
    xor al, al
    call bl_run
    mov word [ab_last], AB_GROUPS - 1

    ; --- the compare, the copy, the signature and the flash scan ----------
    call bl_blank
    mov si, ab_s_hdr2
    call bl_sline

    call ab_b_band                  ; band and shadow identical...
    call ab_b_copy
    mov word [bl_body], ab_b_span
    mov si, ab_r_spaneq
    xor al, al
    call bl_run

    mov byte [ab_shad + 5], 0x99    ; ...and now differing at bytes 5 and 30
    mov byte [ab_shad + 30], 0x99
    mov word [bl_body], ab_b_span
    mov si, ab_r_spandiff
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_copy
    mov si, ab_r_copy
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_sig
    mov si, ab_r_sig
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_flash
    mov si, ab_r_flash
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_x2
    mov si, ab_r_x2
    xor al, al
    call bl_run

    mov word [bl_body], ab_b_x2s
    mov si, ab_r_x2s
    xor al, al
    call bl_run

    call ab_b_x2                    ; ...and the doubled band it just made is
                                    ; what the 2x blit puts down
    mov word [bl_body], ab_b_blit2
    mov si, ab_r_blit2
    call ab_rowb

    ; --- THE FOREIGN FRAME (APPLE2-SPEC section 13) -----------------------
    call bl_blank
    mov si, ab_s_hdr3
    call bl_sline
    call ab_spanck                  ; ...and the cell range is CHECKED before
                                    ; it is timed (see ab_spanck)

    mov word [bl_body], ab_b_fsxrow_t
    mov si, ab_r_fsxrt
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsxrow_l
    mov si, ab_r_fsxrl
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsxrow_h
    mov si, ab_r_fsxrh
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsxrow_t8
    mov si, ab_r_fsxrt8
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsxrow_l8
    mov si, ab_r_fsxrl8
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsxrow_h8
    mov si, ab_r_fsxrh8
    xor al, al
    call bl_run

    call ab_b_fsxrow_h              ; ...so the shadow the EQUAL row compares
    call ab_b_fsxput_d              ; against really does match the line
    mov word [bl_body], ab_b_fsxput_d
    mov si, ab_r_fsxpd
    xor al, al
    call bl_run
    call ab_b_fsxput_d
    mov word [bl_body], ab_b_fsxput_e
    mov si, ab_r_fsxpe
    xor al, al
    call bl_run

    call ab_b_fsxput1_d
    mov word [bl_body], ab_b_fsxput1_d
    mov si, ab_r_fsxp1d
    xor al, al
    call bl_run
    call ab_b_fsxput1_d
    mov word [bl_body], ab_b_fsxput1_e
    mov si, ab_r_fsxp1e
    xor al, al
    call bl_run

    ; --- and the three PATHS, on the same change set ----------------------
    ; ONE iteration each: a whole frame is 192 lines of work and the row is
    ; the answer to `what does a frame cost`, not to `what does a call cost`.
    mov word [bl_n], 1
    mov word [bl_body], ab_b_win2x
    mov si, ab_r_win2x
    call ab_rowb                    ; it BLITS, so it is preflight-gated
    mov word [bl_body], ab_b_fsx13
    mov si, ab_r_fsx13
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsx1bpp
    mov si, ab_r_fsx1b
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsx13sc
    mov si, ab_r_fsxsc
    xor al, al
    call bl_run

    mov word [bl_n], 4
    mov word [bl_body], ab_b_win2x1
    mov si, ab_r_win2x1
    call ab_rowb
    mov word [bl_body], ab_b_fsx131
    mov si, ab_r_fsx131
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsx131n
    mov si, ab_r_fsx131n
    xor al, al
    call bl_run
    mov word [bl_body], ab_b_fsx1bpp1
    mov si, ab_r_fsx1b1
    xor al, al
    call bl_run
    mov word [bl_n], AB_N

    call ab_ident
    call bl_lclr
    mov si, ab_r_ident
    xor di, di
    call bl_lput
    mov al, [ab_cf]
    or al, al
    mov si, ab_s_drawn
    jz .cf0
    mov si, ab_s_refused
.cf0:
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "apple2/a2fsx.inc"         ; ...AND THE FOREIGN WRITERS (section 13),
                                    ; whose rows are what decide whether two
                                    ; of the three ship at all
%include "apple2/a2band.inc"        ; THE THING MEASURED - the package's own
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

ab_tpl:
    ; X = 0 AND WIDTH = THE WHOLE SCREEN, and that is the doubled blit's row
    ; rather than a preference: a window that SPANS the screen has no side
    ; border and its content is the whole frame width (SPEC.md 11.95.3), so a
    ; 640-pixel blit fits exactly. At 632 it did not, and `BLIT1 640x16
    ; stride80` measured 309.9 counts an operation - 111 ms of XT for four
    ; times the pixels of a row that costs 1.75 - because every one of those
    ; rows was going down the CLIPPED path. A measurement of a clip is not a
    ; measurement of the blit, and full screen, which is the only thing that
    ; issues this call, is never clipped.
    dw 0, 22, 640, 448
    dw ab_ttl, ab_paint, ab_onkey, ab_onclick

ab_ttl:     db 'Apple II Band Bench', 0

ab_s_title: db 'A2BANDBENCH - the 7-pixel composer (apps/apple2/a2band.inc)', 0
ab_s_hint:  db 'Click the window, or press R, to run; I for the identity rows.', 0
ab_s_hdr:   db '-- the line: 40 cells of 7x8, 280 px in a 320 band --', 0
ab_s_hdr2:  db '-- the compare, the copy, the signature, the flash --', 0
ab_s_noblit: db 'OSAPI_GFX_BLIT1 REFUSES: no band rows below', 0

ab_seed:    db 'THE QUICK BROWN FOX JUMPS OVER 13 LAZY DO', 0
ab_str:     db 'THE QUICK BROWN FOX JUMPS OVER 13 LAZY DO', 0

ab_r_run40:   db 'FONT_RUN 40 aligned', 0
ab_r_band40:  db 'BANDTEXT 5 groups', 0
ab_r_blit40:  db 'BLIT1 320x8 stride40', 0
ab_r_cband40: db 'BAND 5 grp comp+blit', 0
ab_r_band1:   db 'BANDTEXT 1 group', 0
ab_r_lores40: db 'BANDLORES 5 groups', 0
ab_r_lores1:  db 'BANDLORES 1 group', 0
ab_r_hires40: db 'BANDHIRES 5 grp x8ln', 0
ab_r_hires1:  db 'BANDHIRES 1 grp x8ln', 0
ab_r_hires1l: db 'BANDHIRES 5 grp x1ln', 0
ab_r_blit2:   db 'BLIT1 640x16 stride80', 0
ab_r_cband1:  db 'BAND 1 grp comp+blit', 0
ab_r_spaneq:  db 'ROWSPAN 40 equal', 0
ab_r_spandiff: db 'ROWSPAN 40 differing', 0
ab_r_copy:    db 'ROWCOPY 40 bytes', 0
ab_r_sig:     db 'ROWSIG 40 cells', 0
ab_r_flash:   db 'ROWFLASH 40 cells', 0
ab_r_x2:      db 'BAND_X2 8r x 40b', 0
ab_r_x2s:     db 'BAND_X2 1r x 7b', 0
ab_r_fsxrt:   db 'FSXROW13 text 280px', 0
ab_r_fsxrl:   db 'FSXROW13 lores 280px', 0
ab_r_fsxrh:   db 'FSXROW13 hires 280px', 0
ab_s_spanok:  db 'FSXROW13 cell range: 8 cells == the whole row', 0
ab_s_spanbad: db 'FSXROW13 CELL RANGE MISMATCH - see ab_spanck', 0
ab_r_fsxrt8:  db 'FSXROW13 text 8 cells', 0
ab_r_fsxrl8:  db 'FSXROW13 lores 8 cells', 0
ab_r_fsxrh8:  db 'FSXROW13 hires 8 cells', 0
ab_r_fsxpd:   db 'FSXPUT 280 differing', 0
ab_r_fsxpe:   db 'FSXPUT 280 equal', 0
ab_r_fsxp1d:  db 'FSXPUT 70 differing', 0
ab_r_fsxp1e:  db 'FSXPUT 70 equal', 0
ab_s_hdr3:    db '-- the foreign frame: 192 lines, then ONE row --', 0
ab_r_win2x:   db 'FRAME windowed 2x', 0
ab_r_fsx13:   db 'FRAME FSXM_VGA13', 0
ab_r_fsx1b:   db 'FRAME FSXM_CGA640/HERC', 0
ab_r_fsxsc:   db 'SCROLL FSXM_VGA13 (184 lines)', 0
ab_r_win2x1:  db 'ROW windowed 2x', 0
ab_r_fsx131:  db 'ROW FSXM_VGA13', 0
ab_r_fsx131n: db 'ROW FSXM_VGA13 one group', 0
ab_r_fsx1b1:  db 'ROW FSXM_CGA640/HERC', 0
ab_r_ident:   db 'identity blit says', 0
ab_s_perb:    db '  ...per source byte', 0
ab_s_drawn:   db 'DRAWN (CF=0)', 0
ab_s_refused: db 'REFUSED (CF=1)', 0

ab_win:     dw 0
ab_cx:      dw 0
ab_cy:      dw 0
ab_cw:      dw 0
ab_ch:      dw 0
ab_bx:      dw 0
ab_by:      dw 0
ab_ix:      dw 0
ab_iy:      dw 0
ab_last:    dw AB_GROUPS - 1
ab_fmask:   dw 0
ab_gfirst:  db 0
ab_cf:      db 0
ab_blitok:  db 1            ; the preflight's answer (see ab_run)
ab_lc:      dw 0            ; the composite bodies' loop counter, in MEMORY
                            ; (see ab_b_win2x)
ab_identon: db 0            ; ...and whether `i` has asked for the identity
                            ; rows, which sit on top of the report's last three

; a2band.inc reads the decoded character generator out of the C's own storage
; in the package (it is an ordinary global there, so nasm can see the label).
; Here it is the bench's, filled by ab_setup from the kernel's font.
_a2_chr:    times 512 db 0

; ...and the two tables the OTHER two composers read by name. The package
; builds both in os88_main and this fills them in ab_setup: _a2_rev is the
; 7-bit reverse (hi-res bytes carry bit 0 as the LEFTMOST pixel) and
; _a2_lopat the lo-res luminance ladder's pattern per colour, which is 0x00 or
; 0x7F because a monochrome block is uniform.
_a2_rev:    times 128 db 0
_a2_lopat:  times 16 db 0

; ab_band 320 + ab_shad 320 + ab_mat 80 + ab_x2buf 1280, and the eight bytes
; of slack that make the arithmetic legible
AB_BSS_OWN  equ AB_STRIDE * 8 + AB_STRIDE * 8 + AB_CELLS * 2 \
                + 80 * 16 + 0x1C00 + AB_CELLS + 8 + A2_FSXW * 3
AB_BSS_TOTAL equ AB_BSS_OWN + BL_BSS_SIZE + 1350  ; + a2band.inc's scratch
                                                  ; (a2_grow 320 + a2_x2tab
                                                  ; 512 + 6), with slack;
                                                  ; ab_entry checks the sum and
                                                  ; REFUSES THE LAUNCH if it is
                                                  ; short
    OS88_BSS AB_BSS_TOTAL
    OS88_IMAGE_END

section .bss
ab_band:    resb AB_STRIDE * 8      ; the 40x8 band, as a2_bnd is
ab_shad:    resb AB_STRIDE * 8      ; ...and the frame shadow's row
ab_mat:     resb AB_CELLS * 2       ; two screen rows: normal, and alternating
                                    ; inverse
ab_x2buf:   resb 80 * 16            ; the pixel-doubled band
; ...and the HI-RES source, which is EIGHT scan lines $400 apart and therefore
; $1C00 + 40 bytes of the package's own segment. a2_band_hires walks that
; stride itself off ONE address, so the bench has to give it a real one:
; handing it forty bytes and letting it read the seven lines after them would
; be reading past this claim and into somebody else's memory.
ab_hsrc:    resb 0x1C00 + AB_CELLS
ab_fsxrow:  resb A2_FSXW            ; the composed foreign scan line...
ab_fsxsh:   resb A2_FSXW            ; ...the foreign-frame shadow's row...
ab_fsxspan: resb AB_SPANLEN          ; ...ab_spanck's kept slice of a whole-row
                                    ; compose
ab_fsxfb:   resb A2_FSXW            ; ...and the framebuffer's, which on the
                                    ; machine is a segment of the VGA's own
                                    ; A000 and here is ours (see the foreign
                                    ; section's header)
ab_bl:      resb BL_BSS_SIZE
ab_bss_end:
section .text

    BL_BSS ab_bl
