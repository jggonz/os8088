; =============================================================================
; os8088 - tests/pmcband/pmcbandbench.asm
;
; PMCBANDBENCH: what does PACCMAN's band composer cost? apps/paccman/pmcband.inc
; turns tile codes into a packed 4bpp band and then into either four bitplanes
; for OSAPI_GFX_BLITP or one bit a pixel for OSAPI_GFX_BLIT1 (SPEC.md 91), and
; EVERY MICROSECOND in SPEC.md 91, in apps/paccman/README.md and in the host
; harness's cost table comes from this file and from nowhere else.
;
; That rule is the whole reason it exists. The port's premise - "maybe this
; port is more performant on XTs" - rests on OSAPI_GFX_BLITP avoiding the
; planar decoder that costs the shipped assembly Pac-Man ~202 of its 235 ms a
; frame (SPEC.md 89.2). But the blit is only ONE of four costs: the tile
; composition, the packed-to-planar repack and the C game logic are the other
; three, and LESSONS.md 13 records a first per-cell guess being SEVEN TIMES
; wrong. So the plan carries no fps prediction at all and this measures the
; terms instead.
;
; It %includes the SHIPPING apps/paccman/pmcband.inc - not a copy - and times
; it with tests/benchlib.inc, the harness that priced RUNCPM's composer
; (PERFORMANCE.md Set 65) and the C64's after it. Nothing here ships:
; `make pmcbandbench` builds build/pmcband.img and `all` does not.
;
;   make pmcbandbench
;   make test TESTAPPS=build/pmcband.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
;   ...double-click Disk B, PMCBANDBENCH, click the window
;
; THE TABLES ARE FILLED WITH A PATTERN, NOT WITH THE ARCADE ROM, and that is
; sound rather than lazy: not one of the three routines has a data-dependent
; branch. _pmc_tile is two XLATs a source byte for a fixed 8 or 4 rows,
; _pmc_pack_pl is four table reads and sixteen fixed shifts an output byte,
; and _pmc_pack_1's one branch is on the ROW PARITY, which alternates whatever
; the data says. So the instruction stream - and therefore the time - is the
; same for any table contents, and this package needs no generated file.
; CORRECTNESS is not this file's job: apps/paccman/hosttest/pmcbandtest.asm
; runs the same routines against the host harness's independently written
; vectors, on a real x86 with SS != DS.
;
; THE ROWS, and why each:
;
;   TILE step 1              one 8x8 tile into the packed band - the unit the
;                            whole frame is built out of, and the term the
;                            harness prices `tiles` with
;   TILE step 2              ...and the CGA layout, four output rows from
;                            alternate source rows. It should be about half
;                            and the bench says whether it is
;   PACK_PL 8 rows           the packed band -> four bitplanes, full width.
;                            THE REPACK IS THE COST THE PLAN COULD NOT PRICE
;                            ON PAPER: if it dominates the BLITP it feeds,
;                            wave 2 composes sprites straight into planar on
;                            the colour path instead (SPEC.md 91's risk)
;   PACK_1 8 rows            ...and the monochrome packer, for the two 1bpp
;                            adapters
;   BLITP 224x8              the emit alone on the colour path
;   BLIT4 224x8              ...and the fallback's, which at 224 px wide
;                            ALWAYS takes kernel/vga12.inc's planar decoder
;                            rather than its run path. The ratio between these
;                            two rows IS the hypothesis
;   BAND colour              28 tiles + PACK_PL + BLITP: one whole dirty band
;                            of a frame, which is what a frame is counted in
;   BAND mono                28 tiles + PACK_1 + BLIT1: the same on Hercules
;
; A PREFLIGHT asks each blit slot once, outside the timing: OSAPI_GFX_BLITP
; refuses on a 1bpp adapter, an unaligned x, an armed clip, a straddle and a
; kern_small kernel, and a row that timed a refusal would publish the cost of
; returning at once wearing the label of a draw (c64bandbench paid for exactly
; that once). A refused slot prints REFUSED instead of a number.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PMCBANDBENCH', pb_entry

; pmcband.inc opens `section .bss` for nothing at all - it keeps no state - but
; this harness does, and a plain package bases its bss on os88_image_end. So
; .bss is declared HERE, first, as align=1 following .text.
section .bss align=1 follows=.text
section .text

PB_COLS     equ 28                ; the arcade field: 28 tiles, 224 px
PB_N        equ 8                 ; iterations a row (bandbench's)

; THE THREE TILE ROWS RUN AT THEIR OWN, LARGER N, and that is a correctness
; requirement rather than a nicety. One tile is ~2 PIT counts, so at PB_N = 8
; the whole row is 16 counts and a per-operation figure lands on a 0.125-count
; grid - which is the size of the difference the CGA row MERGE makes to a
; tile. The first version of this bench published that difference as
; 1.250 - 1.125 = 0.125 counts, ONE PIT count spread over eight iterations,
; against a `BAND cga` A/B of 10.5 counts over the same 28 tiles: the two
; disagreed by 3x because the tile pair was at the instrument's floor. The
; band row is 28 x the tile row plus terms that CANCEL between its two arms,
; so `28 x delta tile` MUST equal `delta band` and the bench prints both -
; see pb_recon below. At 256 iterations a tile row is ~512 counts and the
; grid is 0.004 of one.
PB_N_TILE   equ 256               ; iterations for the three TILE rows

; -----------------------------------------------------------------------------
; pb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
pb_entry:
    push si
    mov ax, pb_bss_end
    cmp ax, os88_image_end + PB_BSS_TOTAL
    ja .refuse
    call pb_setup
    call pb_hint
    mov si, pb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [pb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP          ; the content origin on a multiple of 8, so
    clc                         ; the blits below are not refused for their x
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; pb_setup - the four tables and the source band, filled with a deterministic
;            pattern. See the header: the routines have no data-dependent
;            branch, so the contents do not reach the timing.
; -----------------------------------------------------------------------------
pb_setup:
    push ax
    push bx
    push cx
    push dx
    push di

    mov di, pb_planar               ; 256 words
    mov cx, 256
    mov ax, 0x1234
.p:
    mov [di], ax
    add ax, 0x1111
    inc di
    inc di
    loop .p

    mov di, pb_mono2                ; 2 x 256 bytes
    mov cx, 512
    mov al, 0
.m:
    mov [di], al
    inc al
    and al, 3
    inc di
    loop .m

    mov di, pb_pairs                ; one colour block's 16 pair bytes
    mov cx, 16
    mov al, 0x1F
.k:
    mov [di], al
    add al, 0x11
    inc di
    loop .k

    mov di, pb_tile                 ; one tile's 16 source bytes
    mov cx, 16
    mov al, 0x1B
.t:
    mov [di], al
    add al, 0x27
    inc di
    loop .t

    ; THE SPRITE'S SOURCE IS DELIBERATELY HALF TRANSPARENT. _pmc_sprite skips
    ; colour index 0, so a source with no zero pixels measures the SLOW path
    ; on every pixel and a source of all zeros measures the fast one; either
    ; would be a number no real sprite ever costs. 0x1B repeated with a stride
    ; of 0x27 gives a mix, and the explicit zero every fourth byte puts four
    ; transparent pixels in every row - about the coverage a ghost has.
    mov di, pb_spr
    mov cx, 64
    mov al, 0x1B
.s:
    mov [di], al
    add al, 0x27
    inc di
    dec cx
    jz .sd
    test cl, 3
    jnz .s
    mov byte [di], 0                ; one wholly transparent source byte
    inc di
    dec cx
    jnz .s
.sd:
    mov di, pb_pal4                 ; index 0 is the transparent one
    mov byte [di], 0
    mov byte [di+1], 0x0F
    mov byte [di+2], 0x09
    mov byte [di+3], 0x01

    xor bx, bx                      ; pb_brev[b] = b's four pixels reversed
.rv:
    mov al, bl
    mov ah, al
    and ah, 3
    mov cl, 6
    shl ah, cl
    mov dl, ah
    mov ah, al
    and ah, 0x0C
    shl ah, 1
    shl ah, 1
    or  dl, ah
    mov ah, al
    shr ah, 1
    shr ah, 1
    and ah, 0x0C
    or  dl, ah
    mov ah, al
    mov cl, 6
    shr ah, cl
    or  dl, ah
    mov [pb_brev + bx], dl
    inc bx
    cmp bx, 256
    jb .rv

    xor bx, bx                      ; pb_zmask[b] = 0b11 in every ZERO field
.zm:                                ; (the CGA row merge's table, SPEC.md 91)
    mov al, bl
    xor dl, dl
    mov cl, 4
.zf:
    shl dl, 1
    shl dl, 1
    test al, 0xC0                   ; the field this pass is looking at
    jnz .znz
    or  dl, 3
.znz:
    shl al, 1
    shl al, 1
    dec cl
    jnz .zf
    mov [pb_zmask + bx], dl
    inc bx
    cmp bx, 256
    jb .zm

    mov di, pb_band                 ; the packed band the packers read
    mov cx, 8 * PMC_BAND_ROW
    mov al, 0x39
.b:
    mov [di], al
    add al, 0x5B
    inc di
    loop .b

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pb_hint:
    push si
    call bl_blank
    mov si, pb_s_title
    call bl_sline
    call bl_head
    mov si, pb_s_hint
    call bl_sline
    pop si
    ret

pb_paint:
    call bl_paint
    ret

; THE CLIP IS ARMED HERE, because this is W_ONKEY and not W_PAINT: the kernel
; arms the damage clip for a paint and for nothing else (SPEC.md 11.3), and
; everything below draws. CF = 1 means not one pixel of our content is
; visible, and then the right amount to draw is none.
pb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pb_win], si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call pb_run
    call pb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pb_win], si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    call pb_run
    call pb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [pb_win]
    call OSAPI_WM_CONTENT
    mov [pb_cx], ax
    mov [pb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [pb_cx]
    mov bx, [pb_cy]
    mov cx, ax
    add cx, [pb_cw]
    dec cx
    mov dx, bx
    add dx, [pb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [pb_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the measured bodies. Each draws, where it draws at all, at ([pb_bx],[pb_by])
; - the last content rows, so nothing scribbles over a result line.
; =============================================================================

; --- _pmc_tile(src, pairs, dst, rowstep, zmask) -----------------------------
; [pb_zm] is 0 for the plain alternate-row SAMPLE and pb_zmask for the CGA row
; MERGE (SPEC.md 91) - the same routine, the same arguments, one pointer apart,
; which is what makes the two rows below an A/B rather than two benches.
pb_b_tile:
    push word [pb_zm]
    push word [pb_step]
    mov ax, pb_band
    push ax
    mov ax, pb_pairs
    push ax
    mov ax, pb_tile
    push ax
    call _pmc_tile
    add sp, 10
    ret

; --- _pmc_pack_pl(band, planes, rows, planar, cols) -------------------------
pb_b_packpl:
    mov ax, PB_COLS
    push ax
    mov ax, pb_planar
    push ax
    mov ax, 8
    push ax
    mov ax, pb_planes
    push ax
    mov ax, pb_band
    push ax
    call _pmc_pack_pl
    add sp, 10
    ret

; --- _pmc_pack_1(band, bits, rows, mono2, y0, cols) -------------------------
pb_b_pack1:
    mov ax, PB_COLS
    push ax
    xor ax, ax
    push ax                         ; y0
    mov ax, pb_mono2
    push ax
    mov ax, 8
    push ax
    mov ax, pb_bits
    push ax
    mov ax, pb_band
    push ax
    call _pmc_pack_1
    add sp, 12
    ret

; --- _pmc_sprite(src, pal4, dst, sinc, rows, flags, brev, zmask) ------------
; TWO CASES, BECAUSE THEY ARE DIFFERENT LOOPS' WORTH OF WORK. An even
; destination nibble is the cheap one; an odd one plus flipx adds a table pass
; per source byte and is what Pac-Man running left at an odd pixel costs, which
; is half of every frame.
;
; ...AND EACH IS RUN TWICE, [pb_zms] apart, which is the sprite layer's own
; A/B for the CGA row merge (SPEC.md 91). [pb_sinc] moves with it: the merged
; arm is the CGA layout, where a drawn row is two source rows on, so the
; dropped row is the four bytes between them. `sinc` itself is one `add` a row
; and costs the same either way - what the pair of rows measures is the merge.
; EIGHT rows on both arms, so the two are per-row comparable; the shipping CGA
; band draws four of them.
pb_b_sprite:
    push word [pb_zms]
    mov ax, pb_brev
    push ax
    xor ax, ax
    push ax                         ; flags: high nibble first, no flipx
    mov ax, 8
    push ax                         ; rows
    push word [pb_sinc]             ; 4 = every source row, 8 = the CGA layout
    mov ax, pb_band + 8
    push ax
    mov ax, pb_pal4
    push ax
    mov ax, pb_spr
    push ax
    call _pmc_sprite
    add sp, 16
    ret

pb_b_sprite2:
    push word [pb_zms]
    mov ax, pb_brev
    push ax
    mov ax, 3
    push ax                         ; flags: LOW nibble first, and flipx
    mov ax, 8
    push ax
    push word [pb_sinc]
    mov ax, pb_band + 9
    push ax
    mov ax, pb_pal4
    push ax
    mov ax, pb_spr
    push ax
    call _pmc_sprite
    add sp, 16
    ret

; --- the three emits --------------------------------------------------------
; ES IS SAVED AND PUT BACK at every one of them. This runs inside a window
; callback, which the kernel enters with ES = KERNEL_SEG (SPEC.md 20.1)
; because that is where the window record lives; a body that returned with ES
; on its own segment would break the contract for everything the kernel does
; next AND would measure a body the shipping thunk does not have.
pb_b_blitp:
    push es
    push bp
    mov si, pb_planes
    mov di, PMC_PL_STEP
    mov bp, PMC_PL_STRIDE
    mov ax, [pb_bx]
    mov bx, [pb_by]
    mov cx, PMC_BAND_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLITP
    pop bp
    pop es
    ret

pb_b_blit4:
    push es
    push bp
    mov si, pb_band
    mov bp, PMC_BAND_ROW
    mov ax, [pb_bx]
    mov bx, [pb_by]
    mov cx, PMC_BAND_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT4
    pop bp
    pop es
    ret

pb_b_blit1:
    push es
    push bp
    mov si, pb_bits
    mov bp, PMC_PL_STRIDE
    mov ax, [pb_bx]
    mov bx, [pb_by]
    mov cx, PMC_BAND_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop bp
    pop es
    ret

; --- a WHOLE BAND, which is the unit a frame is counted in ------------------
pb_b_bandc:                         ; 28 tiles + the repack + BLITP
    push cx
    push di
    mov cx, PB_COLS
    mov di, pb_band
.t:
    push cx
    push di
    push word [pb_zm]
    mov ax, 1
    push ax
    push di
    mov ax, pb_pairs
    push ax
    mov ax, pb_tile
    push ax
    call _pmc_tile
    add sp, 10
    pop di
    pop cx
    add di, 4
    loop .t
    pop di
    pop cx
    call pb_b_packpl
    call pb_b_blitp
    ret

pb_b_bandm:                         ; 28 tiles + the mono pack + BLIT1
    push cx
    push di
    mov cx, PB_COLS
    mov di, pb_band
.t:
    push cx
    push di
    push word [pb_zm]
    mov ax, 1
    push ax
    push di
    mov ax, pb_pairs
    push ax
    mov ax, pb_tile
    push ax
    call _pmc_tile
    add sp, 10
    pop di
    pop cx
    add di, 4
    loop .t
    pop di
    pop cx
    call pb_b_pack1
    call pb_b_blit1
    ret

; --- ...and the CGA one, which is the band the row MERGE is paid for on ------
; 28 tiles at rowstep 2 + the mono pack over FOUR rows + a 4-row BLIT1. Run
; twice, [pb_zm] apart: the pair of numbers is the whole decision in SPEC.md
; 91 about whether the arcade font's middle strokes are worth their cost on a
; 200-line screen.
pb_b_bandcga:
    push cx
    push di
    mov cx, PB_COLS
    mov di, pb_band
.t:
    push cx
    push di
    push word [pb_zm]
    mov ax, 2
    push ax
    push di
    mov ax, pb_pairs
    push ax
    mov ax, pb_tile
    push ax
    call _pmc_tile
    add sp, 10
    pop di
    pop cx
    add di, 4
    loop .t
    pop di
    pop cx
    call pb_b_pack1c
    call pb_b_blit1c
    ret

; the two 4-row emits the CGA band uses, written out rather than made a
; parameter of the 8-row pair above: those two are the rows SPEC.md 91 already
; quotes and a knob in them would re-date numbers this change does not touch.
pb_b_pack1c:
    mov ax, PB_COLS
    push ax
    xor ax, ax
    push ax                         ; y0
    mov ax, pb_mono2
    push ax
    mov ax, 4                       ; the CGA band's four rows
    push ax
    mov ax, pb_bits
    push ax
    mov ax, pb_band
    push ax
    call _pmc_pack_1
    add sp, 12
    ret

pb_b_blit1c:
    push es
    push bp
    mov si, pb_bits
    mov bp, PMC_PL_STRIDE
    mov ax, [pb_bx]
    mov bx, [pb_by]
    mov cx, PMC_BAND_W
    mov dx, 4
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop bp
    pop es
    ret

; -----------------------------------------------------------------------------
; pb_unclip / pb_reclip - AN ARMED CLIP REGION IS ONE OF OSAPI_GFX_BLITP'S SIX
; REFUSALS (SPEC.md 5.4.3), and this bench's own W_ONCLICK arms one, so the
; first run of it printed REFUSED for every plane row - the one number the
; whole port's premise rests on, unmeasurable by the harness meant to measure
; it. The clip is therefore CLEARED around each blit row and re-armed after.
;
; What that costs is the bench's own protection against painting over a window
; that covers it, for the width of one row. That is a trade a benchmark may
; make and the PACKAGE may not: apps/paccman/pmc_draw.c asks os88_wm_obscured()
; first and takes the clipped BLIT4 path when anything covers it.
;
; The clear/re-arm is OUTSIDE the timed span, so neither call is in the number.
; -----------------------------------------------------------------------------
pb_unclip:
    call OSAPI_WM_CLIP_CLEAR
    ret

pb_reclip:
    push ax
    push bx
    mov bx, [pb_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pb_bank - bank the row just run: [bl_lastus] (32 bits, hundredths of a
;           microsecond an iteration) -> the dword at DI
; Preserves every register.
; -----------------------------------------------------------------------------
pb_bank:
    push ax
    mov ax, [bl_lastus]
    mov [di], ax
    mov ax, [bl_lastus+2]
    mov [di+2], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; pb_uline - SI = label, DX:AX = hundredths of a microsecond -> one report line
;            in the same column as every measured row's us/op
; Preserves every register.
; -----------------------------------------------------------------------------
pb_uline:
    push ax
    push dx
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, BL_C_US
    call bl_usfield
    push si
    mov si, bl_s_us
    mov di, BL_C_UNIT
    call bl_lput
    pop si
    call bl_lcommit
    pop di
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pb_recon - THE BENCH CHECKING ITSELF (SPEC.md 91). `BAND cga` is 28 x the
;            step-2 TILE plus a 4-row pack and a 4-row blit, and those two
;            terms are IDENTICAL in its sampled and merged arms - so the row
;            merge's cost per band MUST be 28 times its cost per tile, and the
;            two rows below are the same quantity measured twice.
;
; It is here because they once disagreed by 3x and both were published: the
; tile A/B was one PIT count spread over PB_N = 8 iterations, at the
; instrument's floor, while the band A/B was 84 counts. PB_N_TILE is the fix
; and this is what says the fix worked. Read the two lines: if they are not
; within a few percent of each other, NEITHER is a number to quote.
; Preserves every register.
; -----------------------------------------------------------------------------
pb_recon:
    push ax
    push cx
    push dx
    push si

    mov si, pb_s_recon
    call bl_sline

    mov ax, [pb_us_t2m]             ; 28 x (merged tile - sampled tile)
    mov dx, [pb_us_t2m+2]
    sub ax, [pb_us_t2]
    sbb dx, [pb_us_t2+2]
    mov cx, PB_COLS
    call bl_mul48
    call bl_get32
    mov si, pb_r_rec1
    call pb_uline

    mov ax, [pb_us_bgm]             ; ...against the band A/B itself
    mov dx, [pb_us_bgm+2]
    sub ax, [pb_us_bg]
    sbb dx, [pb_us_bg+2]
    mov si, pb_r_rec2
    call pb_uline

    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pb_rowb - one report row whose BODY CONTAINS A BLIT, with [bl_body] and SI
; already set. If the preflight found the slot refusing, the row is not timed
; at all and says so: a number here would be the cost of a refusal wearing the
; label of a draw.
; -----------------------------------------------------------------------------
pb_rowb:
    cmp al, 0
    je pb_norow
    call pb_unclip
    xor al, al
    call bl_run
    jmp pb_reclip                   ; tail call: it returns to our caller

pb_norow:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov si, pb_s_refused
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit
    pop di
    ret

; =============================================================================
; pb_run - the whole report
; =============================================================================
pb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [pb_win]
    call OSAPI_WM_CONTENT
    mov [pb_cx], ax
    mov [pb_cy], dx
    mov bx, [pb_win]
    call OSAPI_WM_GEOM
    mov [pb_cw], cx
    mov [pb_ch], dx
    mov ax, [pb_cx]
    add ax, 7
    and ax, 0xFFF8                  ; the blits refuse an unaligned x
    mov [pb_bx], ax
    mov ax, [pb_cy]
    add ax, [pb_ch]
    sub ax, 13
    mov [pb_by], ax

    call bl_blank
    mov si, pb_s_title
    call bl_sline
    call bl_head
    call bl_baseline

    ; --- THE PREFLIGHT: which slots does THIS kernel and THIS adapter draw?
    ; Unclipped, for the reason at pb_unclip: asked with our own clip armed,
    ; OSAPI_GFX_BLITP answers "refused" about the clip and not about the card,
    ; and every plane row below would then print REFUSED on a plain VGA.
    call pb_unclip
    call pb_b_packpl                ; something real in the planes first
    call pb_b_blitp
    mov byte [pb_pok], 1
    jnc .pok
    mov byte [pb_pok], 0
    mov si, pb_s_nop
    call bl_sline
.pok:
    call pb_b_pack1
    call pb_b_blit1
    mov byte [pb_1ok], 1
    jnc .k1
    mov byte [pb_1ok], 0
    mov si, pb_s_no1
    call bl_sline
.k1:
    call pb_reclip

    mov si, pb_s_hdr
    call bl_sline
    mov word [bl_n], PB_N_TILE      ; the tile rows only - see PB_N_TILE

    mov word [pb_zm], 0             ; the sampled arm, for every row but the
    mov word [pb_step], 1           ; two that name the merge
    mov word [bl_body], pb_b_tile
    mov si, pb_r_tile1
    xor al, al
    call bl_run

    mov word [pb_step], 2
    mov word [bl_body], pb_b_tile
    mov si, pb_r_tile2
    xor al, al
    call bl_run
    mov di, pb_us_t2                ; bank it for pb_recon (hundredths of a us)
    call pb_bank

    mov word [pb_zm], pb_zmask
    mov word [bl_body], pb_b_tile
    mov si, pb_r_tile2m
    xor al, al
    call bl_run
    mov di, pb_us_t2m
    call pb_bank
    mov word [pb_zm], 0
    mov word [pb_step], 1

    mov word [bl_n], PB_N           ; ...and every row below is bandbench's N
    mov word [bl_body], pb_b_packpl
    mov si, pb_r_packpl
    xor al, al
    call bl_run

    mov word [bl_body], pb_b_pack1
    mov si, pb_r_pack1
    xor al, al
    call bl_run

    mov word [bl_body], pb_b_sprite
    mov si, pb_r_spr
    xor al, al
    call bl_run

    mov word [bl_body], pb_b_sprite2
    mov si, pb_r_spr2
    xor al, al
    call bl_run

    ; ...and the same two MERGED, which is what the CGA layout ships
    mov word [pb_zms], pb_zmask
    mov word [pb_sinc], 8
    mov word [bl_body], pb_b_sprite
    mov si, pb_r_sprm
    xor al, al
    call bl_run

    mov word [bl_body], pb_b_sprite2
    mov si, pb_r_spr2m
    xor al, al
    call bl_run
    mov word [pb_zms], 0
    mov word [pb_sinc], 4

    call bl_blank
    mov si, pb_s_hdr2
    call bl_sline

    mov word [bl_body], pb_b_blitp
    mov si, pb_r_blitp
    mov al, [pb_pok]
    call pb_rowb

    mov word [bl_body], pb_b_blit4
    mov si, pb_r_blit4
    mov al, 1                       ; BLIT4 never refuses - it is the fallback -
    call pb_rowb                    ; but it goes unclipped like its two peers,
                                    ; so all three rows are measured alike

    mov word [bl_body], pb_b_blit1
    mov si, pb_r_blit1
    mov al, [pb_1ok]
    call pb_rowb

    call bl_blank
    mov si, pb_s_hdr3
    call bl_sline

    mov word [bl_body], pb_b_bandc
    mov si, pb_r_bandc
    mov al, [pb_pok]
    call pb_rowb

    mov word [bl_body], pb_b_bandm
    mov si, pb_r_bandm
    mov al, [pb_1ok]
    call pb_rowb

    mov word [bl_body], pb_b_bandcga
    mov si, pb_r_bandg
    mov al, [pb_1ok]
    call pb_rowb
    mov di, pb_us_bg
    call pb_bank

    mov word [pb_zm], pb_zmask
    mov word [bl_body], pb_b_bandcga
    mov si, pb_r_bandgm
    mov al, [pb_1ok]
    call pb_rowb
    mov di, pb_us_bgm
    call pb_bank
    mov word [pb_zm], 0

    cmp byte [pb_1ok], 0
    je .norecon
    call pb_recon
.norecon:

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "paccman/pmcband.inc"      ; THE THING MEASURED - the package's own
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

pb_tpl:
    dw 7, 22, 632, 448
    dw pb_ttl, pb_paint, pb_onkey, pb_onclick

pb_ttl:     db 'PaccMan Band Bench', 0

pb_s_title: db 'PMCBANDBENCH - PaccMan band composer (apps/paccman/pmcband.inc)', 0
pb_s_hint:  db 'Click the window, or press R, to run.', 0
pb_s_hdr:   db '-- compose and pack: one tile, one 224x8 band --', 0
pb_s_hdr2:  db '-- the three emits, 224x8 --', 0
pb_s_hdr3:  db '-- a whole dirty band: 28 tiles + pack + blit --', 0
pb_s_nop:   db 'OSAPI_GFX_BLITP REFUSES on this adapter', 0
pb_s_no1:   db 'OSAPI_GFX_BLIT1 REFUSES on this adapter', 0
pb_s_refused: db 'REFUSED (CF=1)', 0

pb_r_tile1:  db 'TILE step 1 (8 rows)', 0
pb_r_tile2:  db 'TILE step 2 (4 rows)', 0
pb_r_tile2m: db 'TILE step 2 MERGED', 0
pb_r_packpl: db 'PACK_PL 8 rows x 28', 0
pb_r_pack1:  db 'PACK_1  8 rows x 28', 0
pb_r_spr:    db 'SPRITE 16x8 even', 0
pb_r_spr2:   db 'SPRITE 16x8 odd+flipx', 0
pb_r_sprm:   db 'SPRITE 16x8 even MERGED', 0
pb_r_spr2m:  db 'SPRITE odd+flipx MERGED', 0
pb_r_blitp:  db 'BLITP 224x8 4 planes', 0
pb_r_blit4:  db 'BLIT4 224x8 packed', 0
pb_r_blit1:  db 'BLIT1 224x8 1bpp', 0
pb_r_bandc:  db 'BAND colour 28 tiles', 0
pb_r_bandm:  db 'BAND mono 28 tiles', 0
pb_r_bandg:  db 'BAND cga 4 rows sampled', 0
pb_r_bandgm: db 'BAND cga 4 rows MERGED', 0

pb_s_recon:  db '-- the merge, measured twice: these two must agree --', 0
pb_r_rec1:   db '28 x (MERGED - sampled)', 0
pb_r_rec2:   db 'BAND MERGED - sampled', 0

pb_us_t2:    dd 0                   ; hundredths of a us an iteration, banked
pb_us_t2m:   dd 0                   ; by pb_bank for pb_recon
pb_us_bg:    dd 0
pb_us_bgm:   dd 0

pb_zm:      dw 0                    ; 0 = sample, pb_zmask = the CGA row merge
pb_zms:     dw 0                    ; ...the same, for the two SPRITE rows
pb_sinc:    dw 4                    ; 4 = every source row, 8 = the CGA layout
pb_win:     dw 0
pb_cx:      dw 0
pb_cy:      dw 0
pb_cw:      dw 0
pb_ch:      dw 0
pb_bx:      dw 0
pb_by:      dw 0
pb_step:    dw 1
pb_pok:     db 1                ; the preflight's two answers (see pb_run)
pb_1ok:     db 1

; pb_band 896 + pb_planes 896 + pb_bits 224 + pb_planar 512 + pb_mono2 512
; + pb_pairs 16 + pb_tile 16 + pb_spr 64 + pb_pal4 4 + pb_brev 256
; + pb_zmask 256, and eight bytes of slack that make the arithmetic legible -
; eleven objects, in the order the equation adds them. pb_entry checks the sum
; and REFUSES THE LAUNCH if it is short - which is what the first build of the
; C64's bench did, by 24 bytes.
PB_BSS_OWN  equ 8 * PMC_BAND_ROW + 4 * PMC_PL_STEP + 8 * PMC_PL_STRIDE \
                + 512 + 512 + 16 + 16 + 64 + 4 + 256 + 256 + 8
PB_BSS_TOTAL equ PB_BSS_OWN + BL_BSS_SIZE
    OS88_BSS PB_BSS_TOTAL
    OS88_IMAGE_END

section .bss
pb_band:    resb 8 * PMC_BAND_ROW
pb_planes:  resb 4 * PMC_PL_STEP
pb_bits:    resb 8 * PMC_PL_STRIDE
pb_planar:  resb 512
pb_mono2:   resb 512
pb_pairs:   resb 16
pb_tile:    resb 16
pb_spr:     resb 64                 ; one 16x16 sprite: 4 bytes a row
pb_pal4:    resb 4                  ; its colour block, index 0 transparent
pb_brev:    resb 256                ; _pmc_sprite's flipx lowering
pb_zmask:   resb 256                ; _pmc_tile's CGA row merge (SPEC.md 91)
pb_bl:      resb BL_BSS_SIZE
pb_bss_end:
section .text

    BL_BSS pb_bl
