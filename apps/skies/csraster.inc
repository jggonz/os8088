; =============================================================================
; os8088 - apps/skies/csraster.inc
;
; THE RASTER (SPEC.md 88.4). Three backends behind five entries - cs_r_setup,
; cs_r_begin, cs_poly, cs_seg, cs_r_end - and NOT ONE kernel drawing slot
; among them, because SPEC.md 53.7 makes every one of them illegal the moment
; fsx_mode returns. It is TANK ATTACK's raster (SPEC.md 85.3) with the
; dirty-span machinery taken OUT of the view and a polygon filler put in:
;
;   * the VIEW is written whole every frame - the sky/ground pass covers every
;     byte of it (88.4.1), the polygons and lines go over that, and the blit
;     copies the view rows whole. Its span pairs are set once at entry and
;     never touched again;
;   * the PANEL rows below it keep one span set (88.3): an instrument is an
;     opaque rectangle, drawn only when its value changed and marked into the
;     span so the blit carries it; the blit empties the panel spans behind it;
;   * a POLYGON is two bounds a row (88.4.2): every edge is traced once per
;     row with Bresenham's integer step into cs_xl/cs_xr, and every row is one
;     masked run. The horizon is the same tracer on one edge.
;
; ONE STRIDE FOR ALL THREE, Tank's coincidence: a CGA 320x200x4 row, a
; Hercules 640-pixel 1bpp row and a Mode X plane row are each 80 bytes.
;
; SS != DS (CLAUDE.md): nothing here indexes a table with BP.
; =============================================================================
; FILLRUN - CX bytes of AL (= AH) at ES:DI, words where it can; advances DI
;           past them and clobbers CX. CX may be 0 or negative. DF must be 0.
;           INLINE, because at 112 rows a frame the `call`/`ret` and the
;           prologue behind them are 314 cycles a row of the 1,421 a split row
;           costs (SPEC.md 88.3.1.2). cs_fillrun below is the same code as a
;           routine, for the Mode X filler that calls it once a row.
%macro FILLRUN 0
    or cx, cx
    jle %%out
    test di, 1
    jz %%even
    stosb
    dec cx
    jz %%out
%%even:
    shr cx, 1
    rep stosw
    jnc %%out
    stosb
%%out:
%endmacro


; --- THE BACKEND'S RECORD, one row a backend indexed by CSB_* - 1 (SPEC.md
;     88.3.7): cs_r_setup copies a row onto the contiguous bss from cs_vx to
;     cs_pbg8 in one `rep movsw`. It was the nine words of geometry and then
;     some 300 bytes of `mov word [x], y` choosing the procs, the inks and the
;     panel's shape backend by backend - a pointer only some arms filled is how
;     `call [cs_lrunproc]` once reached package offset 0 on Mode X, and a row
;     has no arm to forget.
;      vx,  vy,  vw,  vh,  wx0,  ww,  wh, sclx, scly
;      panrows, pasp, plblp, plabw, msgtabp, msgx0
;      inktab, hrunproc, lrunproc, rowsproc, rowsprocc, glyphproc
;      lsh, lst, lvt (the line walk trio), shkb, shclr
;      conebase, gcellb, kshift, pbg8
%macro CS_BEROW 30
    dw %1, %2, %3, %4, %5, %6, %7, %8, %9
    dw %10, %11, %12, %13, %14, %15
    dw %16, %17, %18, %19, %20, %21
    dw %22, %23, %24, %25, %26
    db %27, %28, %29, %30
%endmacro
cs_vptab:
    CS_BEROW   0,   0, 320, 240,   40, 240, 108,  277,  277, \
               CS_PANROWS, 256, cs_plbl, 4, cs_msgtab, 0, \
               cs_inkmodex, cs_hrun_modex, cs_hrun_modex, \
               cs_polyrows, cs_polyrows, cs_glyph_modex, \
               cs_line_modex_sh, cs_line_modex_st, cs_line_modex_vt, 0, 0, \
               1, 2, 2, CSI_PBG          ; MODEX: square pixels, two VRAM pages
    CS_BEROW   0,   0, 320, 200,   40, 240,  84,  277,  231, \
               CS_PANROWS, 213, cs_plbl, 4, cs_msgtab, 0, \
               cs_inkcga, cs_hrun_cga, cs_hrun_cga, \
               cs_polyrows, cs_polyrows, cs_glyph_cga, \
               cs_line_cga_sh, cs_line_cga_st, cs_line_cga_vt, \
               CS_SHKB, CS_SHSEG / 2, \
               1, 2, 2, 0xFF             ; CGA: 4:3 in 320x200, and four
                                         ; pixels of CGA brown for a ground
    CS_BEROW  40,  74, 640, 200,  120, 400, 112,  443,  285, \
               CS_PANROWS, 330, cs_plbl, 4, cs_msgtab, 0, \
               cs_inkherc, cs_hrun_herc, cs_lrun_herc, \
               cs_polyrows_herc, cs_polyrows_herc_c, cs_glyph_herc, \
               cs_line_herc_sh, cs_line_herc_st, cs_line_herc_vt, \
               CS_SHKB, CS_SHSEG / 2, \
               0, 1, 3, 0                ; HERC: a 400-wide view in a 640x200
                                         ; box, a Hercules pair 1.29 tall, and
                                         ; 0.5 for the narrower field
    CS_BEROW   0,   0, 160, 100,   16, 128,  66,  138,  115, \
               CS_STRIPROWS, 213, cs_plbl160, 2, cs_msgtab160, CS_STRIPMX, \
               cs_inkc160, cs_hrun_c160, cs_hrun_c160, \
               cs_polyrows, cs_polyrows, cs_glyph_c160, \
               cs_line_c160_sh, cs_line_c160_st, cs_line_c160_vt, \
               CS_SHKB / 2, CS_SHSEG / 4, \
               1, 4, 1, 0                ; C160: the 16-colour text hack
                                         ; (88.15) - two pixels a byte, a cell
                                         ; four of them, the ONE-LINE strip's
                                         ; panel (88.15.5) and a HUNDRED rows
                                         ; of shadow, 8,000 bytes (88.10.9)
cs_vptab_end:
; THE TABLE IS THE **MODERATE** SIZE (SPEC.md 88.13.4), which is what the
; Hercules row always was - 400 of its 640 - and is now three quarters of the
; box on the other two as well. Full and Small are computed from it by
; cs_r_size; Full restores CGA's and Mode X's own 320 and gives Hercules all
; 640, which it will not enjoy.
;
; scly = sclx x the pixel aspect, so a square is square on all three. sclx is
; 60 degrees of horizontal field on CGA and Mode X (the view's half-width over
; it is 0.578); Hercules keeps the same pixel scale in a 400-pixel view, so
; its field is 49 degrees (0.451) - the width came down from 512 for the
; frame's sake and the picture's (88.3.4), and what it lost is the edges of
; the field, not the size of anything in it.

; -----------------------------------------------------------------------------
; cs_r_size - the view's width and height at the player's Size (SPEC.md
;             88.13.4), applied to the table's row before anything derives
;             from it. Clobbers AX, BX, CX, DX
;
; The scale does NOT change with it: sclx and scly stay the table's, so a
; smaller view is a smaller WINDOW on the same world rather than a zoom out -
; fewer pixels for the same picture, which is the whole point of the option,
; and the same trade Hercules already makes at 400 wide.
;
;   FULL     cs_fulltab's row - what each adapter shipped with, which is why
;            it is a table and not vw x (vh - CS_PANROWS): the computed
;            answer is Mode X's 320x152 where Mode X shipped 320x144
;   MODERATE the table's row - Hercules' 400, and three quarters elsewhere
;   SMALL    half of moderate each way
;
; The width stays a multiple of 8 and so does the left edge, because the view
; is addressed in BYTES on every backend. The height is bounded by the panel,
; which wants 88 rows of the box under the view.
;
; FULL is also the DEFAULT on CGA and Mode X, and MODERATE on Hercules
; (SPEC.md 88.13.4) - one line in cs_entry, so every adapter opens at the
; geometry it had before the page existed.
; -----------------------------------------------------------------------------
CS_PANROWS equ 88               ; what the cockpit's layout needs (88.9.2)
CS_STRIPMX equ 6 * 8            ; the strip's message starts at cell 6, so the
                                ; SPEED beside it is never covered (88.15.6)
CS_STRIPROWS equ 13             ; ...and what the ONE-LINE strip does (88.15.5):
                                ; row 0 its top edge, rows 3..10 the glyph,
                                ; rows 1..12 what a message erases. The box is
                                ; 100 rows tall here, so a cockpit's 88 would
                                ; leave the view twelve

cs_fulltab:                     ; ww, wh at CSZ_FULL, indexed by CSB_* - 1
    dw 320, 144                 ; MODEX
    dw 320, 112                 ; CGA
    dw 640, 112                 ; HERC: all 640 of the box, which it will
                                ; not enjoy
    dw 160,  87                 ; C160: the whole 160, and every row the
                                ; thirteen-row strip does not want (88.15.5)

cs_r_size:
    mov al, [cs_setsize]
    cmp al, CSZ_MOD
    je .done                        ; the table IS moderate
    cmp al, CSZ_FULL
    jne .small
    mov bl, [cs_back]               ; --- full: the geometry the simulator
    dec bl                          ;     SHIPPED with, adapter by adapter,
    xor bh, bh                      ;     rather than a computed maximum -
    shl bx, 1                       ;     Mode X left its panel 96 rows and
    shl bx, 1                       ;     the arithmetic would have taken 8
    mov ax, [cs_fulltab + bx]        ;     of them back
    mov [cs_ww], ax
    mov ax, [cs_fulltab + bx + 2]
    jmp short .fit
.small:
    mov ax, [cs_ww]                 ; --- small: half of moderate each way
    shr ax, 1
    and al, 0xF8
    mov [cs_ww], ax
    mov ax, [cs_wh]
    shr ax, 1
.fit:
    cmp ax, [cs_vh]                 ; ...and never past the box or the panel
    jl .h1
    mov ax, [cs_vh]
.h1:
    mov bx, [cs_vh]
    sub bx, [cs_panrows]            ; 88 for a cockpit, 12 for the strip
    cmp ax, bx
    jle .h2
    mov ax, bx
.h2:
    mov [cs_wh], ax
    mov ax, [cs_vw]                 ; ...centred, on a WORD boundary and not
    sub ax, [cs_ww]                 ; merely a byte: cs_blit rounds each row's
    shr ax, 1                       ; span out to whole words (`and al, 0xFE`),
    and al, 0xF0                    ; so a view whose first BYTE is odd is
    mov [cs_wx0], ax                ; copied out from the byte before it -
                                    ; always black, so it costs a word a row
                                    ; and shows nothing (88.13.4)
.done:
    mov ax, [cs_wh]                 ; ...and PUBLISHED where the panel's clip
    mov [cs_viewh], ax              ; cannot borrow it (88.9.2.3)
    ret

; -----------------------------------------------------------------------------
; cs_r_setup - adopt a backend for the length of one bracket
; in:  [cs_back] set, [cs_fsi] filled by OSAPI_FSX_MODE
; out: CF=1 if the raster cannot be had (no shadow); every register clobbered
; -----------------------------------------------------------------------------
cs_r_setup:
    mov al, [cs_back]
    dec al
    mov ah, CS_BEREC
    mul ah
    mov si, cs_vptab
    add si, ax
    mov di, cs_vx
    push ds
    pop es
    mov cx, CS_BEREC / 2            ; THE WHOLE RECORD (88.3.7): the geometry,
    cld                             ; the panel's shape, the procs, the inks
    rep movsw
    call cs_r_size                  ; ...and the player's Size over it, before
                                    ; anything below derives from it (88.13.4)
    mov ax, [cs_wx0]
    add ax, [cs_ww]
    dec ax
    mov [cs_wx1], ax                ; the view's last x
    mov ax, [cs_ww]
    shr ax, 1
    add ax, [cs_wx0]
    mov [cs_vcx], ax                ; the projection centre
    mov ax, [cs_wh]
    shr ax, 1
    mov [cs_vcy], ax
    mov ax, [cs_wh]
    cmp byte [cs_back], CSB_C160    ; THE STRIP IS ACROSS THE BOTTOM (88.15.5)
    jne .py                         ; and stays there when the view shrinks:
    mov ax, [cs_vh]                 ; it is a line and not a cockpit, so a
    sub ax, [cs_panrows]            ; smaller picture letterboxes above it
.py:                                ; rather than dragging it up the screen
    mov [cs_pany], ax               ; the panel's first row

    mov byte [cs_ink], 0xFF         ; no ink set yet (cs_setink's cache)

    mov ax, [cs_wx0]                ; the view's bytes: first, and how many
    mov cl, [cs_kshift]
    shr ax, cl
    mov [cs_wb0], ax
    mov ax, [cs_ww]
    shr ax, cl
    mov [cs_wbn], ax

    call cs_devrows
    mov byte [cs_artok], 0          ; THE TITLE BANDS GO: the four tables below
                                    ; are built over them, in the union they
                                    ; share (88.10.7), and cs_fsx_main reads
                                    ; them back on its way out
    call cs_ktabs                   ; the projection tables for these scales
    call cs_endtab                  ; ...and the row loop's end tables (88.4.5.5)
    call cs_mktabs                  ; ...and the stepped mark's widen-and-clamp
                                    ; pair, which the two lines above are the
                                    ; only other readers of cs_wb0/cs_wbn like

    ; --- the target: two VRAM pages, or one RAM shadow --------------------
    cmp byte [cs_back], CSB_MODEX
    jne .shadow
    mov ax, [cs_fsi+FSI_SEG]
    mov [cs_tseg], ax
    mov word [cs_page0], 0
    mov word [cs_page1], 19200      ; 240 rows x 80 bytes a plane
    call cs_modex_pal
    jmp short .spans
.shadow:
    cmp byte [cs_back], CSB_C160    ; the hack is programmed by US, on top of
    jne .sh1                        ; the plain 80x25 the kernel set (88.15.2)
    call cs_c160_mode
.sh1:
    mov dx, [cs_shseg]              ; ONCE FOR THE BRACKET: this runs again on
    or dx, dx                       ; a Size hotkey (88.13.5), and a second
    jnz .haveshadow                 ; claim would be SPEC.md 85.3's leak.
                                    ; cs_fsx_main FREES it on the way out
                                    ; (88.10.9), so the title page never holds
                                    ; 16KB that only a flight can use
    mov ax, [cs_shkb]               ; the backend's own size: 8KB on C160
    call OSAPI_MEM_CLAIM
    jc .noshadow
    mov [cs_shseg], dx
.haveshadow:
    mov [cs_tseg], dx
    mov word [cs_page0], 0
    mov word [cs_page1], 0
    cmp byte [cs_back], CSB_CGA
    jne .spans
    call cs_cga_pal                 ; (C160 needs no palette call at all: an
                                    ; attribute IS the colour, 88.15.3)
.spans:
    ; --- the span set: every row starts empty, and a view row is marked by
    ;     whatever touches it (SPEC.md 88.3); cs_fullspan is the pair a
    ;     touched view row takes, and every row kind starts DIRTY so the
    ;     first frame fills the whole view ---------------------------------
    mov al, [cs_wb0]                ; lo = the first byte...
    mov ah, al
    add ah, [cs_wbn]
    dec ah                          ; ...hi = the last
    mov [cs_fullspan], ax
    mov word [cs_spanp], cs_span0
    mov word [cs_spanp+2], cs_span1
    mov word [cs_spcur], cs_span0
    mov word [cs_spprv], cs_span1
    push ds
    pop es
    mov di, cs_span0                ; BOTH SETS EMPTY, and one `rep` cannot
    mov cx, CS_MAXROW               ; do it: a set is ONE WORD A ROW, so
    mov ax, 0x00FF                  ; CS_MAXROW words and not CS_MAXROW * 2,
    cld                             ; and the two are not contiguous either -
    rep stosw                       ; cs_spguard1 sits between them. The one
    mov di, cs_span1                ; sweep wrote 960 bytes into a 480-byte
    mov cx, CS_MAXROW               ; buffer, over that guard and 8 bytes
    rep stosw                       ; short of span1's end
    mov di, cs_rowkind
    mov cx, CS_MAXROW
    mov al, 3                       ; every row: kind unknown
    rep stosb
    clc
    ret
.noshadow:
    stc
    ret

; -----------------------------------------------------------------------------
; cs_c160_mode - turn the plain 80x25 the kernel just set into 160x100 in
;                SIXTEEN COLOURS (SPEC.md 88.15.2). Clobbers AX, CX, DX, SI,
;                DI, ES
;
; THE WHOLE MODE IS SIX 6845 WRITES AND ONE PORT BYTE, and not one of them is
; a kernel call: SPEC.md 53.4 gives a text bracket the CRTC and the mode
; register outright, so this backend costs the kernel nothing at all - the
; same promise SPEC.md 88 opens with.
;
;   * MAX SCAN LINE (R9) = 1 makes a character row TWO scan lines instead of
;     eight, so the 200-line screen holds a HUNDRED rows rather than 25;
;   * VERTICAL TOTAL (R4) = 127 and VERTICAL DISPLAYED (R6) = 100 put the
;     262 scan lines an NTSC field wants back where the ROM had them -
;     128 x 2 + the 6 of R5 - and show 100 of the 128;
;   * VERTICAL SYNC (R7) = 112 leaves 24 scan lines of bottom border;
;   * CURSOR START (R10) = 0x20 is the 6845's own "do not display it", which
;     is cheaper than int 10h AH=01h and does not depend on a BIOS the
;     original CGA machines have not got;
;   * 3D8h bit 5 CLEAR is what buys the sixteen colours: with blink off an
;     attribute's high nibble is a BACKGROUND COLOUR rather than eight
;     colours and a blink bit.
;
; Every cell then holds character 0xDE - the CP437 RIGHT HALF BLOCK, whose
; eight rows are all 0x0F - so the left half of the cell shows the attribute's
; background nibble and the right half its foreground nibble: TWO PIXELS A
; CELL, each any of sixteen colours, 160 x 100 of them. THERE IS NOTHING TO
; LOAD: the glyph comes out of the CGA card's own character generator, which
; is a ROM and could not be written to even if the OS wanted to.
;
; The video is turned OFF for the retime and the fill and back on after it,
; which is one `out` either side: the CRTC spends a field or two out of lock
; while R4 and R9 disagree, and a machine that shows that shows a rolling
; picture of the desktop that was there a moment ago.
; -----------------------------------------------------------------------------
CS_C160_CH equ 0xDE                 ; the right half block: 0x0F on all 8 rows

cs_c160_crtc:                       ; register, value - in the order the 6845
    db  4, 127                      ; wants nothing in particular, so ours
    db  5,   6                      ; reads top to bottom
    db  6, 100
    db  7, 112
    db  9,   1
    db 10, 0x20
CS_C160_NREG equ ($ - cs_c160_crtc) / 2

cs_c160_mode:
    mov dx, 0x3D8                   ; 80-column text, blink OFF, VIDEO OFF
    mov al, 0x01
    out dx, al
    mov si, cs_c160_crtc
    mov cx, CS_C160_NREG
    mov dx, 0x3D4
.r:
    lodsw                           ; AL = the register, AH = its value
    out dx, al
    inc dx
    xchg al, ah
    out dx, al
    dec dx
    loop .r
    mov dx, 0x3D9                   ; a BLACK border round the picture: in a
    xor al, al                      ; text mode this register is the overscan
    out dx, al                      ; colour and nothing else
    mov es, [cs_fsi+FSI_SEG]        ; every cell the half block, on black
    xor di, di
    mov ax, (0 << 8) | CS_C160_CH
    mov cx, 160 * 100 / 2
    cld
    rep stosw
    mov dx, 0x3D8                   ; ...and the picture back on
    mov al, 0x09
    out dx, al
    ret

; -----------------------------------------------------------------------------
; cs_cga_pal - CGA palette 0 at low intensity - green, red, brown - over a
;              LIGHT BLUE background: the sky is colour 0, which is the one
;              colour of the four a CGA lets an app choose freely, and that
;              is what makes a blue sky possible at all.
;
; THROUGH THE BIOS, NOT PORT 3D9h. The colour-select register is the CGA's;
; a VGA or an EGA running the same mode 04h has no such port and takes its
; four colours from the attribute controller, so the first build's `out`
; left a VGA on the BIOS's own mode-4 default - cyan, magenta and white over
; BLACK - which is what a player who chose CGA on a VGA for the frame rate
; (the Mode menu, 88.10) was shown. INT 10h AH=0Bh is the one call every
; adapter's BIOS answers, writing 3D9h on a CGA and the palette registers
; on the others. Inside the bracket the BIOS is ours to call (SPEC.md 53.7)
; -----------------------------------------------------------------------------
cs_cga_pal:
    push ax
    push bx
    mov ax, 0x0B00
    mov bx, 0x0100                  ; BH = 1: the palette, BL = 0 (green, red,
    int 0x10                        ; brown)
    mov ax, 0x0B00
    mov bx, 0x0009                  ; BH = 0: the background, BL = 9 light blue
    int 0x10                        ; with bit 4 clear, low intensity
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_modex_pal - the DAC, one row per CSI_*
; -----------------------------------------------------------------------------
cs_modex_pal:
    push si
    mov si, cs_dactab
    mov cx, CSI_NINK
    xor bl, bl
.e:
    mov dx, 0x3C8
    mov al, bl
    out dx, al
    inc dx
    lodsb
    out dx, al
    lodsb
    out dx, al
    lodsb
    out dx, al
    inc bl
    loop .e
    pop si
    ret

cs_dactab:                          ; 6-bit DAC values, indexed by CSI_*
    db 22, 36, 60                   ;  0 sky
    db 14, 38, 14                   ;  1 ground
    db 20, 20, 20                   ;  2 runway
    db 14, 26, 54                   ;  3 river
    db 50, 47, 42                   ;  4 a wall toward us
    db 36, 33, 29                   ;  5 a side
    db 43, 40, 36                   ;  6 a roof
    db 18, 44, 18                   ;  7 a hill
    db 26, 20, 14                   ;  8 the tower and every line
    db 60, 60, 60                   ;  9 runway edges, roads
    db  6,  6, 10                   ; 10 the panel
    db 56, 56, 56                   ; 11 its ink
    db 63, 44, 10                   ; 12 its warning
    db 24, 24, 26                   ; 13 the cockpit's face
    db  0,  0,  0                   ; 14 nothing at all (88.13.3)
    db 24, 44, 62                   ; 15 a river reduced to a LINE (88.6.5) -
                                    ;    the river's blue, lifted so a one
                                    ;    pixel stroke reads against the ground
    db 11, 32, 12                   ; 16 a hill's other sides (88.4.4.1)
    db 23, 47, 20                   ; 17 ...and its cap: a shade UP, and only
                                    ;    a shade - a bright cap made every
                                    ;    face that flickers in at range a
                                    ;    flash (88.4.4.1)
    db 30, 42, 60                   ; 18 the sky's outer haze (88.4.1.1)
    db 40, 48, 60                   ; 19 ...its inner, at the horizon
    db 24, 41, 28                   ; 20 the ground's ONE band (88.4.1.1.1)
    db  8, 26,  8                   ; 21 a field (88.13.1.5): dark green
    db 30, 36, 12                   ; 22 ...and the other kind: stubble
    db 10, 31, 10                   ; 23 the ground's second green, Mode X's
                                    ;    checker (88.13.1.5.2)
    db 20, 34, 24                   ; 24 ...and the haze band's, the same step
                                    ;    down from its own
    db 60, 60, 60                   ; 25 a road's line: the marks' (88.13.1.5.3)
    db 63, 54, 22                   ; 26 lift on the map (88.16.1): warm air

; ...and what each MEANING is worth as FOUR ROW PATTERNS on the three
; adapters (SPEC.md 88.4.4). Indexed by CSI_* x 4.
cs_inkherc:
    db 0x00, 0x00, 0x00, 0x00       ; sky: black - what every Hercules flight
                                    ; simulator of the period showed, and the
                                    ; cheapest row there is
    db 0x88, 0x00, 0x22, 0x00       ; ground: 12.5% - two lit pixels in every
                                    ; 4x4. At 25% a view that is mostly ground
                                    ; reads as a grey wall the buildings sit
                                    ; on; at 12.5% it reads as ground
    db 0x00, 0x00, 0x00, 0x00       ; runway: black, with white edges
    db 0xFF, 0x00, 0xFF, 0x00       ; river: stripes
    db 0xFF, 0xFF, 0xFF, 0xFF       ; a wall toward us: white
    db 0xAA, 0x55, 0xAA, 0x55       ; a side: 50%
    db 0xEE, 0xBB, 0xEE, 0xBB       ; a roof: 75%
    db 0xCC, 0xCC, 0x33, 0x33       ; a hill: 50% in blocks
    db 0xFF, 0xFF, 0xFF, 0xFF       ; lines
    db 0xFF, 0xFF, 0xFF, 0xFF       ; marks
    db 0x00, 0x00, 0x00, 0x00       ; the panel
    db 0xFF, 0xFF, 0xFF, 0xFF       ; its ink
    db 0xFF, 0xFF, 0xFF, 0xFF       ; its warning
    db 0xAA, 0x00, 0x55, 0x00       ; the cockpit's face: 25%
    db 0x00, 0x00, 0x00, 0x00       ; nothing: black (88.13.3)
    db 0xFF, 0xFF, 0xFF, 0xFF       ; a river as a line: SOLID WHITE. The
                                    ; river's own ink here is stripes, and a
                                    ; line drawn in stripes is half a line
                                    ; (88.6.5)
    db 0x88, 0x22, 0x88, 0x22       ; a hill's other sides: 25%, against the
                                    ; near ones' 50% (88.4.4.1)
    db 0xDD, 0x55, 0x77, 0x55       ; ...and its cap: 62.5%, between them
    db 0x00, 0x00, 0x08, 0x00       ; THE HAZE (88.4.1.1): the black sky
    db 0x80, 0x00, 0x08, 0x00       ; dusted toward the horizon...
    db 0x88, 0x00, 0xAA, 0x00       ; ...and the ground PALER there: 19%
                                    ; against its own 12.5%
    db 0x80, 0x00, 0x08, 0x00       ; a field (88.13.1.5): DARK, 6%...
    db 0xFF, 0x00, 0x00, 0x00       ; ...and stubble, in furrows
    db 0x88, 0x00, 0x22, 0x00       ; the ground's second green: the ground
    db 0x88, 0x00, 0xAA, 0x00       ; ...and the band's: the band
    db 0xFF, 0xFF, 0xFF, 0xFF       ; a road's line: the marks' white
    db 0xF0, 0x00, 0x0F, 0x00       ; lift on the map (88.16.1): a rough
                                    ; checker of 4-pixel dashes, coarser than
                                    ; anything else here - the ground's 12.5%
                                    ; and fine 25% dots read as one grey. Only
                                    ; rows 0 and 2 are ever laid (cs_maptile)
cs_inkcga:                          ; palette 0: 0 light blue, 1 green,
    db 0x00, 0x00, 0x00, 0x00       ;   2 red, 3 brown
    db 0x55, 0x55, 0x55, 0x55       ; ground: green
    db 0xFF, 0xFF, 0xFF, 0xFF       ; runway: brown
    db 0x00, 0x00, 0x00, 0x00       ; river: the sky's blue
    db 0xFF, 0xFF, 0xFF, 0xFF       ; a wall: brown
    db 0xAA, 0xAA, 0xAA, 0xAA       ; a side: red
    db 0xAA, 0xAA, 0xAA, 0xAA       ; a roof: red
    db 0xFF, 0xFF, 0xFF, 0xFF       ; a hill: brown
    db 0xAA, 0xAA, 0xAA, 0xAA       ; lines: red
    db 0x55, 0x55, 0x55, 0x55       ; marks: green
    db 0xFF, 0xFF, 0xFF, 0xFF       ; the panel: brown
    db 0x55, 0x55, 0x55, 0x55       ; its ink: green
    db 0xAA, 0xAA, 0xAA, 0xAA       ; its warning: red
    db 0xAA, 0xAA, 0xAA, 0xAA       ; the cockpit's face: red
    db 0x00, 0x00, 0x00, 0x00       ; nothing: black (88.13.3)
    db 0x00, 0x00, 0x00, 0x00       ; a river as a line: colour 0, the light
                                    ; blue this palette's river is - drawn
                                    ; over GROUND, which is green (88.6.5)
    db 0xEE, 0xBB, 0xEE, 0xBB       ; a hill's other sides: brown and red,
                                    ; dithered - the shade a 4-colour card
                                    ; has (88.4.4.1)
    db 0xFD, 0xFF, 0xDF, 0xFF       ; ...and its cap: brown with a little
                                    ; green in it
    db 0x00, 0x00, 0x00, 0x00       ; THE HAZE (88.4.1.1): palette 0 has
    db 0x00, 0x00, 0x00, 0x00       ; nothing paler than its sky, so the
    db 0x44, 0x11, 0x44, 0x11       ; GROUND fades into the sky's blue
    db 0x5F, 0xF5, 0x5F, 0xF5       ; a field (88.13.1.5): brown and green,
                                    ; not solid brown - that is a hill's...
    db 0x66, 0x99, 0x66, 0x99       ; ...and red and green
    db 0x55, 0x55, 0x55, 0x55       ; the ground's second green: green
    db 0x44, 0x11, 0x44, 0x11       ; ...and the band's: the band
    db 0xFF, 0xFF, 0xFF, 0xFF       ; a road's line: BROWN, where the marks'
                                    ; green is the ground's (88.13.1.5.3)
    db 0x44, 0x55, 0x11, 0x55       ; lift on the map (88.16.1): the sky's
                                    ; blue in a quarter of the green

; --- the SIXTEEN-COLOUR text hack's inks (SPEC.md 88.15.3) --------------------
; A pattern byte is two pixels, high nibble LEFT, and every one of the four
; rows is the same byte: there is no dither anywhere in this table, because
; for the first time on a CGA there is a colour for every one of these things.
; This is the one backend whose ground is GREEN, whose runway is ASPHALT and
; whose two building faces differ by SHADE rather than by hue.
%macro CS_C16 1                     ; %1 = the CGA attribute colour, 0..15
    db (%1 << 4) | %1, (%1 << 4) | %1, (%1 << 4) | %1, (%1 << 4) | %1
%endmacro
cs_inkc160:
    CS_C16 9                        ; sky: light blue
    CS_C16 2                        ; ground: green
    CS_C16 8                        ; runway: dark grey - asphalt, and the
                                    ; first runway in this game that is not
                                    ; the same colour as a wall
    CS_C16 1                        ; river: blue
    CS_C16 7                        ; a wall toward us: light grey
    CS_C16 8                        ; a side: dark grey - the SAME material a
                                    ; shade down, which is what makes a box
                                    ; read as a solid rather than as two
                                    ; colours meeting
    CS_C16 4                        ; a roof: red tile
    CS_C16 6                        ; a hill: brown
    CS_C16 15                       ; lines: white
    CS_C16 15                       ; marks: white
    CS_C16 0                        ; the panel's window: black
    CS_C16 10                       ; its ink: light green, which is what a
                                    ; lit instrument looks like
    CS_C16 12                       ; its warning: light red
    CS_C16 0                        ; the strip's face: BLACK, and not the
                                    ; dark grey a cockpit's is. The runway is
                                    ; dark grey here (this is the only
                                    ; backend where it can be), so a grey
                                    ; strip under a parked aeroplane was one
                                    ; continuous grey band with the panel's
                                    ; own rule the only thing dividing it
    CS_C16 0                        ; nothing: black (88.13.3)
    CS_C16 1                        ; a river as a line: BLUE, the river's own
                                    ; (88.6.5) - the one backend where a line
                                    ; can be the colour of the thing it stands
                                    ; for with nothing given up
    CS_C16 4                        ; a hill's other sides: the brown's shade
                                    ; down, which on RGBI is red (88.4.4.1)
    db 0x67, 0x76, 0x67, 0x76       ; ...and its cap: brown and grey
    CS_C16 9                        ; THE HAZE (88.4.1.1): the sky's outer
    CS_C16 11                       ; band is the sky, its inner light cyan,
    CS_C16 10                       ; and the ground goes light green to meet
                                    ; it
    db 0x62, 0x26, 0x62, 0x26       ; a field (88.13.1.5): brown and green,
                                    ; not the haze band's light green...
    db 0xAE, 0xEA, 0xAE, 0xEA       ; ...and light green and yellow
    CS_C16 2                        ; the ground's second green: green
    CS_C16 10                       ; ...and the band's: the band
    CS_C16 15                       ; a road's line: the marks' white
    db 0xE2, 0x22, 0x2E, 0x22       ; lift on the map (88.16.1): yellow dots
                                    ; in a quarter of the green
cs_inkmodex:                        ; the DAC above, one for one
    db 0, 0, 0, 0
    db 1, 1, 1, 1
    db 2, 2, 2, 2
    db 3, 3, 3, 3
    db 4, 4, 4, 4
    db 5, 5, 5, 5
    db 6, 6, 6, 6
    db 7, 7, 7, 7
    db 8, 8, 8, 8
    db 9, 9, 9, 9
    db 10, 10, 10, 10
    db 11, 11, 11, 11
    db 12, 12, 12, 12
    db 13, 13, 13, 13
    db 14, 14, 14, 14               ; nothing: black (88.13.3)
    db 15, 15, 15, 15               ; a river as a line (88.6.5)
    db 16, 16, 16, 16               ; a hill's other sides (88.4.4.1)
    db 17, 17, 17, 17               ; ...and its cap
    db 18, 18, 18, 18               ; the haze (88.4.1.1)
    db 19, 19, 19, 19
    db 20, 20, 20, 20
    db 21, 21, 21, 21               ; the fields (88.13.1.5)
    db 22, 22, 22, 22
    db 23, 23, 23, 23               ; the ground's second green
    db 24, 24, 24, 24               ; ...and the haze band's
    db 25, 25, 25, 25               ; a road's line
    db CS_MXSTIP + 5, CS_MXSTIP, CS_MXSTIP + 10, CS_MXSTIP
                                    ; lift on the map (88.16.1): DAC 26
                                    ; through half the planes of every other
                                    ; row, cs_hrun_modex's .stip

; The pixel value of an ink on the two colour adapters, for the WALKS and the
; glyphs: the 2-bit colour on CGA and the index on Mode X. Hercules is the one
; bit.
cs_inkval_cga:   db 0, 1, 3, 0, 3, 2, 2, 3, 2, 1, 3, 1, 2, 2, 0, 0, 3, 3
                 db 0, 0, 1, 3, 2, 1, 1, 3, 0
cs_inkval_modex: db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
                 db 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26
; ...and C160's is the colour in BOTH NIBBLES, which is what its walk and its
; glyph both want: a mask picks the half, and `ink8 & mask` is then the ink at
; whichever pixel the mask names, with no shift at all.
cs_inkval_c160:  db 0x99, 0x22, 0x88, 0x11, 0x77, 0x88, 0x44, 0x66
                 db 0xFF, 0xFF, 0x00, 0xAA, 0xCC, 0x88, 0x00, 0x11
                 db 0x44, 0x77, 0x99, 0xBB, 0xAA, 0x66, 0xEE, 0x22, 0xAA, 0xFF
                 db 0xEE

; -----------------------------------------------------------------------------
; cs_hznone - the horizon band with the TERRAIN fill off (SPEC.md 88.13.3):
;             every ground row takes CSI_BLACK and every sky row keeps the
;             sky, so what is left is the horizon as the edge between them.
;             Clobbers AL.
;
; It was the SKY's ink on both sides, which is right on a 1bpp adapter for
; the accidental reason that the sky there IS black - and on a colour one
; painted the whole world blue, horizon and all. Black is the ground being
; ABSENT rather than the sky reaching the bottom of the frame.
; -----------------------------------------------------------------------------
cs_hznone:
    cmp byte [cs_hzab], CSI_SKY     ; whichever side is not the sky is the
    jne .below                      ; ground, and that is the side to black
    mov byte [cs_hzbl], CSI_BLACK
    mov al, [cs_hzl]                ; ...and so is the band's own half, which
    cmp al, CSI_SKY                 ; is the row the horizon crosses
    je .l1
    mov byte [cs_hzl], CSI_BLACK
.l1:
    mov al, [cs_hzr]
    cmp al, CSI_SKY
    je .out
    mov byte [cs_hzr], CSI_BLACK
    ret
.below:
    mov byte [cs_hzab], CSI_BLACK
    mov al, [cs_hzl]
    cmp al, CSI_SKY
    je .b1
    mov byte [cs_hzl], CSI_BLACK
.b1:
    mov al, [cs_hzr]
    cmp al, CSI_SKY
    je .out
    mov byte [cs_hzr], CSI_BLACK
.out:
    ret

; -----------------------------------------------------------------------------
; cs_setink - adopt a logical ink (CSI_*) for the fills and walks that follow
; in:  AL = CSI_*.  preserves every register
; -----------------------------------------------------------------------------
cs_setink:
    cmp al, [cs_ink]                ; as it is: nothing to do
    je .same
    push ax
    push bx
    push si
    mov [cs_ink], al
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    add si, [cs_inktab]
    mov bx, [si]                    ; the four pattern bytes
    mov [cs_pat], bx
    mov bx, [si+2]
    mov [cs_pat+2], bx
    mov bl, al
    xor bh, bh
    mov al, 1
    cmp byte [cs_back], CSB_HERC
    je .have
    mov al, [cs_inkval_cga + bx]
    cmp byte [cs_back], CSB_CGA
    je .cga
    mov al, [cs_inkval_c160 + bx]
    cmp byte [cs_back], CSB_C160
    je .have
    mov al, [cs_inkval_modex + bx]
    jmp short .have
.cga:
    push ax
    mov bl, al                      ; the glyph expander for this ink: three
    xor bh, bh                      ; 16-byte tables, one per 2bpp colour
    dec bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, cs_nib1
    mov [cs_nibp], bx
    pop ax
.have:
    mov [cs_ink8], al
    pop si
    pop bx
    pop ax
.same:
    ret

; -----------------------------------------------------------------------------
; cs_devrows - the device offset of every box row, and 80 x row, once a
;              bracket (SPEC.md 85.3.4's tables)
; -----------------------------------------------------------------------------
cs_devrows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cs_devoff
    xor bx, bx                      ; BX = r
.row:
    mov ax, bx
    mov cx, 80
    mul cx
    mov si, bx
    shl si, 1
    mov [cs_rowoff + si], ax
    mov ax, bx
    add ax, [cs_vy]                 ; AX = the device row
    cmp byte [cs_back], CSB_C160
    je .c160
    cmp byte [cs_back], CSB_HERC
    je .herc
    mov dx, ax                      ; CGA: bank = y & 1, 80 bytes a row
    and dx, 1
    push dx
    shr ax, 1
    mov cx, 80
    mul cx
    pop dx
    jmp short .bank
.herc:
    mov dx, ax                      ; HERC: bank = y & 3, 90 bytes a row
    and dx, 3
    push dx
    mov cl, 2
    shr ax, cl
    mov cx, 90
    mul cx
    pop dx
    add ax, 5                       ; ...and the box starts at x = 40
    jmp short .bank
.c160:
    mov cx, 160                     ; C160: no bank and no interleave, a
    mul cx                          ; character row being 80 words - so this
    xor dx, dx                      ; is the ONE backend whose device offset
                                    ; is TWICE its shadow offset, which is
                                    ; what cs_blit expands across (88.15.4)
.bank:
    mov cl, 5
    shl dx, cl
    mov cl, 8
    shl dx, cl
    add ax, dx
    mov [di], ax
    add di, 2
    inc bx
    cmp bx, [cs_vh]
    jb .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_r_begin - open a frame: pick the target page
; -----------------------------------------------------------------------------
cs_r_begin:
    mov al, [cs_par]                ; the frame's parity picks the span set
    and al, 1                       ; (SPEC.md 85.3.1's two sets)...
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [cs_spanp + bx]
    mov [cs_spcur], si
    xor bx, 2
    mov si, [cs_spanp + bx]
    mov [cs_spprv], si
    push ds
    pop es
    mov di, [cs_spcur]              ; ...armed empty
    mov word [di - 4], 0x7FFF       ; ...and its row range with it: the two
    mov word [di - 2], -1           ; guard words before a set are the
    mov cx, [cs_vh]                 ; first and last row anything marked
    mov ax, 0x00FF                  ; (SPEC.md 88.3.3)
    cld
    rep stosw
    mov word [cs_tbase], 0
    cmp byte [cs_back], CSB_MODEX
    jne .out
    mov al, [cs_par]                ; ...and the page
    and al, 1
    mov cx, [cs_page0]
    jz .p0
    mov cx, [cs_page1]
.p0:
    mov [cs_tbase], cx
    mov di, cs_rowkind              ; two pages alternate, so a row is never
    mov cx, [cs_wh]                 ; "as it was": every row is refilled
    mov al, 3
    rep stosb
.out:
    ret

; -----------------------------------------------------------------------------
; cs_r_end - put the frame on the glass
; -----------------------------------------------------------------------------
cs_r_end:
    cmp byte [cs_back], CSB_MODEX
    je .flip
    call cs_blit
    inc byte [cs_par]
    ret
.flip:
    mov al, [cs_par]                ; ONE CALL, the kernel's (SPEC.md 53.10):
    and al, 1                       ; the page named is scanned when it returns
    call OSAPI_FSX_PAGE
    inc byte [cs_par]
    ret

; -----------------------------------------------------------------------------
; cs_blit - this frame's spans, shadow to VRAM (CGA and Hercules)
;
; The view rows carry the view's whole byte range and the panel rows carry
; what their items drew; one set, because nothing on the panel is ever
; erased - only redrawn (SPEC.md 88.3). DS stays ours (85.3.7).
; -----------------------------------------------------------------------------
cs_blit:
    mov si, [cs_spcur]              ; this frame's set...
    mov di, [cs_spprv]              ; ...and last frame's: a byte the last
    mov ax, [si - 4]                ; frame drew and this one refilled still
    cmp ax, [di - 4]                ; has to reach the glass (85.3.1).
    jle .y0                         ; The rows to look at are the union of
    mov ax, [di - 4]                ; the two sets' ranges, so the panel's
.y0:                                ; eighty-odd rows cost nothing on a
    mov bp, [si - 2]                ; frame that left them alone (88.3.3)
    cmp bp, [di - 2]
    jge .y1
    mov bp, [di - 2]
.y1:
    sub bp, ax
    jl .out
    inc bp                          ; BP = rows
    mov bx, ax
    shl bx, 1
    add si, bx
    add di, bx
    add bx, cs_devoff
    mov dx, ax
    shl dx, 1
    shl dx, 1
    add dx, ax
    mov cl, 4
    shl dx, cl                      ; DX = 80 y, the shadow row
    mov es, [cs_fsi+FSI_SEG]
    cld
    ; --- DS IS THE SHADOW FOR THE WHOLE WALK (SPEC.md 88.3.6) --------------
    ; It used to be swapped and swapped back INSIDE the row - push ds, load,
    ; pop ds, 43 clocks a row for a segment that cannot change inside a frame.
    ; A package runs CS = DS (apps/os88api.inc), so the three reads that want
    ; the package's own data take a `cs:` override, one byte and two clocks
    ; each, and the segment register stops moving.
    push ds
    mov ds, [cs_shseg]
.row:
    cs lodsw                        ; AL/AH = lo/hi
    mov cx, [cs:di]                 ; CL/CH = lo'/hi'
    inc di
    inc di
    cmp ax, cx
    jne .work
    cmp ax, 0x00FF                  ; both empty: the common row
    je .next
.work:
    cmp al, cl                      ; AL = min(lo, lo')
    jbe .l1
    mov al, cl
.l1:
    cmp ah, ch                      ; AH = max(hi, hi')
    jae .h1
    mov ah, ch
.h1:
    cmp al, ah
    ja .next
    and al, 0xFE                    ; whole words
    or ah, 1
    sub ah, al
    shr ah, 1
    mov cl, ah
    xor ch, ch
    inc cx                          ; CX = words
    xor ah, ah                      ; AX = the first byte
    push si
    push di
    mov di, [cs:bx]
    add di, ax                      ; ES:DI = the device row
    mov si, dx
    add si, ax                      ; DS:SI = the shadow row
    cmp byte [cs:cs_back], CSB_C160 ; (a SHADOW byte at that address is 0, so
    je .expand                      ;  reading it without the override sent
    rep movsw                       ;  every C160 row through rep movsw and
.done:                              ;  landed the packed nibbles in the
    pop di                          ;  character bytes as well as the
    pop si                          ;  attribute ones - the right colours
.next:                              ;  behind the wrong glyphs)
    inc bx
    inc bx
    add dx, 80
    dec bp
    jnz .row
    pop ds
.out:
    ret
; --- THE ONE BLIT THAT IS NOT A COPY (SPEC.md 88.15.4). A shadow byte IS an
;     attribute byte - high nibble the left pixel, low nibble the right - so
;     what reaches the card is the byte itself, laid at every OTHER address
;     because the character bytes between them were written once by
;     cs_c160_mode and are never touched again. Two bytes a turn, so the
;     `add di, 2` that a stride-2 `stosb` would need is paid once instead of
;     twice, and the count is the span's whole WORDS, which is what the
;     rounding above already left in CX.
.expand:
    add di, ax                      ; the byte's device offset is TWICE its
    inc di                          ; offset in the shadow, and the attribute
                                    ; is the ODD byte of the pair
.e:
    lodsw
    stosb
    inc di
    xchg al, ah
    stosb
    inc di
    loop .e
    jmp short .done

; =============================================================================
; The sky and the ground (SPEC.md 88.4.1)
; =============================================================================

; -----------------------------------------------------------------------------
; cs_skyground - every row of the view, split at the horizon
; in:  [cs_nx]/[cs_ny]/[cs_nz] = the world's up vector in camera space, Q15
; clobbers everything
;
; With the pixel's ray r = (X/sclx, -Y/scly, 1) about the centre, sky is where
; r.n > 0. The line r.n = 0 is solved in whichever form is BOUNDED: steeper
; than a pixel a row it is x(row) with one clamped intercept; flatter it is
; row(x), and the rows where it meets the view's two side edges are found
; instead. Either way the crossing per row comes out of cs_edge, the same
; integer tracer every polygon edge uses, into cs_xl.
; -----------------------------------------------------------------------------
cs_skyground:
    push ds
    pop es
    mov di, cs_xl                   ; +big over the view rows: an untouched
    mov cx, [cs_wh]                 ; row is wholly one side
    mov ax, 0x7FFF
    cld
    rep stosw
    mov word [cs_hzy0], 0x7FFF      ; the rows the line runs between
    mov word [cs_hzy1], -1
    mov byte [cs_hzform], 0         ; ...and which form it took (88.4.1.1)
    mov byte [cs_hzhave], 0         ; ...and no horizon SEGMENT yet (88.13.3)

    mov ax, [cs_nx]                 ; the up vector, quartered: keeps every
    sar ax, 1                       ; product below inside 2^31
    sar ax, 1
    mov [cs_hnx], ax
    mov ax, [cs_ny]
    sar ax, 1
    sar ax, 1
    mov [cs_hny], ax
    mov ax, [cs_nz]
    sar ax, 1
    sar ax, 1
    mov [cs_hnz], ax

    ; --- which form: |nx x scly| >= |ny x sclx| is the steep one ------------
    mov ax, [cs_hnx]
    imul word [cs_scly]
    call cs_abs32
    mov [cs_hza], ax
    mov [cs_hza+2], dx
    mov ax, [cs_hny]
    imul word [cs_sclx]
    call cs_abs32
    cmp dx, [cs_hza+2]
    ja .flat
    jb .steep
    cmp ax, [cs_hza]
    ja .flat

    ; ===== STEEP: x = X0 + Y x (ny sclx)/(nx scly), |slope| <= 1 ============
.steep:
    mov ax, [cs_hnz]                ; X0 = -nz sclx / nx, at the centre row
    imul word [cs_sclx]
    neg dx
    neg ax
    sbb dx, 0
    mov bx, [cs_hnx]
    call cs_cdiv
    jc .whole                       ; the line is off the view entirely, and
                                    ; every row is the centre's side
    mov [cs_hzx0], ax
    mov ax, [cs_vcy]
    neg ax                          ; Y at row 0
    call cs_hzsteep_t
    add ax, [cs_hzx0]
    add ax, [cs_vcx]
    mov [cs_hzxa], ax
    mov ax, [cs_wh]
    dec ax
    sub ax, [cs_vcy]                ; Y at the last row
    call cs_hzsteep_t
    add ax, [cs_hzx0]
    add ax, [cs_vcx]
    mov cx, ax
    mov [cs_hzxb], ax               ; the two ends, for the haze's parallels
    mov byte [cs_hzform], 1
    mov dx, [cs_wh]
    dec dx
    mov ax, [cs_hzxa]
    xor bx, bx
    call cs_hzkeep                  ; ...banked, for the line the ground's
    mov byte [cs_eside], 1          ; fill leaves behind when it is off
    call cs_edge                    ; cs_xl[row] = the crossing, every row
    mov word [cs_hzy0], 0
    mov ax, [cs_wh]
    dec ax
    mov [cs_hzy1], ax
    ; which side is which: as X -> -inf, r.n ~ nx X, so nx > 0 puts ground
    ; on the left. Above and below the band do not arise: it spans every row.
    mov al, CSI_GROUND
    mov ah, CSI_SKY
    cmp word [cs_hnx], 0
    jg .sides
    xchg al, ah
.sides:
    mov [cs_hzl], al
    mov [cs_hzr], ah
    mov [cs_hzab], ah
    mov [cs_hzbl], al
    jmp .rows

    ; ===== FLAT: row = Y0 + X x (nx scly)/(ny sclx), |slope| < 1 ============
.flat:
    mov ax, [cs_hnz]                ; Y0 = nz scly / ny, at the centre column
    imul word [cs_scly]
    mov bx, [cs_hny]
    call cs_cdiv
    jc .whole
    mov [cs_hzx0], ax
    mov ax, [cs_wx0]
    sub ax, [cs_vcx]                ; X at the left edge
    call cs_hzflat_t
    add ax, [cs_hzx0]
    add ax, [cs_vcy]
    mov [cs_hzya], ax               ; the row the line meets the left edge on
    mov ax, [cs_wx1]
    sub ax, [cs_vcx]
    call cs_hzflat_t
    add ax, [cs_hzx0]
    add ax, [cs_vcy]
    mov [cs_hzyb], ax               ; ...and the right edge
    mov byte [cs_hzform], 2
    mov bx, [cs_hzya]
    mov dx, ax
    cmp bx, dx
    jle .o1
    xchg bx, dx
.o1:
    mov [cs_hzy0], bx               ; the band, top row and bottom row
    mov [cs_hzy1], dx
    mov ax, [cs_wx0]
    mov bx, [cs_hzya]
    mov cx, [cs_wx1]
    mov dx, [cs_hzyb]
    call cs_hzkeep
    mov byte [cs_eside], 1
    call cs_edge                    ; cs_xl[row] = the crossing inside it
    ; above the band is where -ny Y (Y -> -inf) is positive: ny > 0 means sky
    ; above. Inside the band the line descends to the right when the left row
    ; is the higher one, and then the LEFT of a row is below the line.
    mov al, CSI_SKY
    mov ah, CSI_GROUND
    cmp word [cs_hny], 0
    jg .ab
    xchg al, ah
.ab:
    mov [cs_hzab], al               ; above...
    mov [cs_hzbl], ah               ; ...and below
    mov bx, [cs_hzya]
    cmp bx, [cs_hzyb]
    jg .rises
    mov [cs_hzl], ah                ; left of the crossing is below the line
    mov [cs_hzr], al
    jmp short .rows
.rises:
    mov [cs_hzl], al
    mov [cs_hzr], ah
    jmp short .rows

    ; ===== the line is off the view: every row is the centre's side =======
.whole:
    mov al, CSI_SKY
    cmp word [cs_hnz], 0
    jg .w1
    mov al, CSI_GROUND
.w1:
    mov [cs_hzab], al
    mov [cs_hzbl], al

    ; ===== the rows =========================================================
    ; Three runs: the rows above the band, all of one kind; the band, split
    ; row by row; the rows below. A WHOLE row is refilled only when it is
    ; dirty or its kind changed, and marked for the blit only when its kind
    ; changed - a dirty row's old ink is in LAST frame's spans, and the blit
    ; carries the union (SPEC.md 88.3). That is most of the view in level
    ; flight, at a compare a row.
.rows:
    test byte [cs_setfill], CSFL_HAZE ; --- THE HAZE (SPEC.md 88.4.1.1): when
    jz .nohz                        ; it is on (F6), over a ground that is
    cmp byte [cs_hzform], 0         ; drawn and a horizon that is somewhere,
    je .nohz                        ; the rows it reaches are its own pass
    test byte [cs_setfill], CSFL_TERRAIN
    jz .nohz
%ifndef CSPROBE                     ; (the instrument has no haze: skies.asm)
    jmp cs_hazerows
%endif
.nohz:
    mov byte [cs_hzlast], 0         ; (the haze knows its rows are not its)
    test byte [cs_setfill], CSFL_TERRAIN   ; --- THE GROUND, if it is wanted
    jnz .ink                        ; (88.13.3): with it off the ground's
    call cs_hznone                  ; rows go BLACK and the sky stays sky
.ink:
    mov al, [cs_hzl]
    call cs_inkpat
    mov [cs_hzlt], si
    mov al, [cs_hzr]
    call cs_inkpat
    mov [cs_hzrt], si
    mov ax, 7                       ; ...and the crossing byte's pixel mask,
    mov bx, cs_hlm                  ; which is the ADAPTER's and so the
    cmp byte [cs_kshift], 3         ; frame's: the row filler used to re-decide
    je .mk                          ; it once a row (88.3.1.2)
    mov ax, 3
    mov bx, cs_clm
.mk:
    mov [cs_hzmm], ax
    mov [cs_hzmt], bx
    mov si, [cs_hzlt]               ; ...and the ink PAIR per row phase, so a
    mov di, [cs_hzrt]               ; fused row reads one word where the band
    mov bx, cs_hzpat4               ; loop read two bytes through two pointers
    mov cx, 4
.pk:
    mov al, [si]
    mov ah, [di]
    mov [bx], ax
    inc si
    inc di
    inc bx
    inc bx
    loop .pk
    mov es, [cs_tseg]
    cmp byte [cs_back], CSB_MODEX
    jne .p0
    mov dx, 0x3C4                   ; every plane, once
    mov ax, 0x0F02
    out dx, ax
.p0:
    xor bx, bx                      ; BX = the row
    mov di, [cs_tbase]              ; DI = its first view byte
    add di, [cs_wb0]
    mov cx, [cs_hzy0]               ; the rows above the band...
    cmp cx, [cs_wh]
    jle .abv
    mov cx, [cs_wh]
.abv:
    mov al, [cs_hzab]
    call cs_hzrows
    mov cx, [cs_hzy1]               ; ...the band...
    inc cx
    cmp cx, [cs_wh]
    jle .bd
    mov cx, [cs_wh]
.bd:
    sub cx, bx
    jle .below
    ; --- THE SET'S ROW RANGE TAKES THE BAND (SPEC.md 88.3.3.1) ------------
    ; Each split row writes its own span below, and that is not enough:
    ; cs_blit walks only the rows the set's RANGE names, and the range is
    ; extended by cs_hzrows' .kind arm and by cs_markrows and by nothing
    ; else. A frame whose rows all keep their kind and whose objects mark
    ; nothing therefore drew the horizon into the shadow and never carried
    ; it to the glass - which is a horizon that does not turn.
    push di
    mov di, [cs_spcur]
    cmp bx, [di - 4]
    jge .r0
    mov [di - 4], bx
.r0:
    mov ax, bx
    add ax, cx
    dec ax                          ; the band's last row
    cmp ax, [di - 2]
    jle .r1
    mov [di - 2], ax
.r1:
    pop di
    ; ===== THE BAND'S SPANS, IN A PASS OF THEIR OWN (SPEC.md 88.3.1.1) =====
    ; A split row used to be given `cs_fullspan`, so a held bank marked and
    ; CARRIED the whole view every frame - 5,600 bytes on a 400x112 Hercules
    ; view - when what changes on it is the crossing's own byte and one
    ; either side. The row still LAYS the whole view (the fill's fixed cost
    ; is ~1,100 cycles a row against ~350 for the 50 bytes, so a narrower
    ; fill buys almost nothing), but the span says what CHANGED, and that is
    ; all cs_blit has to carry: last frame's set holds last frame's band and
    ; whatever an object drew, and cs_blit copies the UNION of the two.
    ;
    ; IT IS A PASS OF ITS OWN BECAUSE OF WHERE THE TIME GOES. Built inline in
    ; the fill loop below it is the same arithmetic and it cost 12.5 ms a
    ; frame - 533 cycles a row - against a blit saving of 7.9, so the whole
    ; change measured 280.2 ms against 280.1. Here every constant is hoisted
    ; into a register, the walk is `lodsw`/`stosw`, and the row measures ~168
    ; cycles: the same work for a third of the money. The 8088's fetch floor
    ; is why - a row is priced by the BYTES of code it runs through.
    mov al, [cs_wb0]                ; the pass's frame constants, once
    xor ah, ah
    mov [cs_hzlo], ax
    add al, [cs_wbn]
    dec al
    mov [cs_hzhi], al               ; the view's first and last byte...
    mov al, 3                       ; ...and the kind a row must have had.
    cmp byte [cs_hzfull], 0         ; THREE things refuse the band: the A/B
    jne .hk1                        ; knob, and Mode X - where cs_r_begin
    cmp byte [cs_back], CSB_MODEX   ; sets every row to 3 precisely so that
    jne .hk2                        ; nothing is "as it was", the two pages
.hk1:                               ; alternating under it (88.3.1)
    mov al, 0xFF
.hk2:
    ; --- ...AND THE SIDES SWAPPING IS THE THIRD (SPEC.md 88.3.1.1.3) -------
    ; A band row's span is the crossing's byte and one either side, on the
    ; argument that everything else on the row is what it was. When the roll
    ; changes sign the two sides EXCHANGE - the fill lays the whole row
    ; mirrored about a crossing that has barely moved - and that argument
    ; fails over the row's whole width. Every other row is repaired by
    ; cs_hzrows' .kind arm as the band sweeps past it; the CENTRE row is the
    ; one the band never leaves, which is why the field saw exactly one line
    ; (docs/FIELD-NOTES.md 40). The pair is one word, cs_hzl and cs_hzr being
    ; adjacent, and this costs one compare a frame.
    mov dx, [cs_hzl]
    cmp dx, [cs_hzsides]
    mov [cs_hzsides], dx
    je .hk3
    mov al, 0xFF
.hk3:
    mov [cs_hzsplit], al
    push cx
    push bx
    push di
    push bp
    push es
    mov dx, cx                      ; DX = the band's rows
    mov si, bx
    shl si, 1
    mov di, si
    add di, [cs_spcur]              ; DI -> this frame's span for the row
    add si, cs_xl                   ; SI -> its crossing
    add bx, cs_rowkind              ; BX -> its kind
    push ds
    pop es                          ; (ES is the SHADOW at this point)
    mov bp, [cs_hzlo]
    mov cl, [cs_kshift]
    mov ch, [cs_hzsplit]
    cld
.hs:
    lodsw                           ; the crossing, and 0x7FFF - the row the
    or ax, ax                       ; horizon does not reach - clamps to the
    jns .hs0                        ; last byte, which is where the fill's
    xor ax, ax                      ; own cs_wx1+1 puts it too
.hs0:
    shr ax, cl                      ; AX = the crossing's byte, AH = 0
    cmp ax, bp
    ja .hs1
    mov ax, bp
    mov ah, al                      ; at or left of the view's first byte
    jmp short .hs3
.hs1:
    cmp al, [cs_hzhi]
    jb .hs2
    mov al, [cs_hzhi]
    mov ah, al                      ; at or past its last
    jmp short .hs3
.hs2:
    mov ah, al
    dec al
    inc ah                          ; AL..AH = the byte and one either side
.hs3:
    cmp [bx], ch                    ; split last frame too? A row that was
    je .hs4                         ; not has the whole view to carry, and
    mov ax, [cs_fullspan]           ; cs_hzrows' .kind arm says so the same
.hs4:                               ; way one row along
    mov byte [bx], 3
    inc bx
    stosw
    dec dx
    jnz .hs
    pop es
    pop bp
    pop di
    pop bx
    pop cx
    cmp byte [cs_back], CSB_MODEX
    jne .fused                      ; Mode X keeps the per-row call: its own
    jmp .bmx                        ; filler selects planes as it goes
.fused:
    ; ===== THE BAND, FUSED (SPEC.md 88.3.1.3) =============================
    ; The row used to be a CALL, and both sides of it paid for that. The loop
    ; banked three registers, re-derived the row's ink pair through two
    ; pointers and re-tested the 0x7FFF sentinel; the routine then rebuilt the
    ; row pointer it had just been handed, re-took the crossing's byte and
    ; re-picked a pixel mask that is the adapter's. 88.3.1.2 took the second
    ; half of that (1,421 -> 1,042 cycles a row); this takes the seam itself,
    ; and the loop's own 601 with it. Everything that is the FRAME's is in a
    ; register or a hoisted word, the walk of cs_xl is `lodsw`, and the row's
    ; three runs write straight through DI without ever restoring it.
    push bp
    mov si, bx
    shl si, 1
    add si, cs_xl                   ; SI -> this row's crossing
    mov ax, bx
    add ax, cx                      ; ...and the row the band ends before
    shl ax, 1
    add ax, cs_xl
    mov [cs_hzend], ax
    mov es, [cs_tseg]
    cld
.fb:
    lodsw                           ; AX = the crossing
    cmp ax, 0x7FFF
    jne .fb0
    mov ax, [cs_wx1]                ; untouched inside the band: the crossing
    inc ax                          ; is off the right, so the row is the
.fb0:                               ; left side's
    mov bx, si
    sub bx, cs_xl + 2               ; BX = the row's phase, doubled - SI is
    and bx, 6                       ; the row counter here, and lodsw has
    mov dx, [cs_hzpat4 + bx]        ; already stepped it
    cmp ax, [cs_wx0]
    jg .fb1
    mov dl, dh                      ; wholly the right pattern
    jmp short .fbw
.fb1:
    cmp ax, [cs_wx1]
    jg .fbw                         ; wholly the left pattern, in DL already
    mov bx, ax                      ; --- SPLIT: the crossing's pixel mask,
    and bx, [cs_hzmm]               ;     taken before AX becomes its byte
    add bx, [cs_hzmt]
    mov bl, [bx]                    ; BL = pixels i.., the right side's
    mov cl, [cs_kshift]
    shr ax, cl                      ; AX = the crossing's byte
    mov bp, ax
    mov cx, ax
    sub cx, [cs_wb0]                ; CX = the bytes wholly left of it
    mov al, dl
    mov ah, dl
    push di
    FILLRUN                         ; the left run - DI lands ON the crossing
    mov al, bl                      ; the crossing's byte: the two patterns
    mov ah, al                      ; through the mask and its complement
    not ah
    and al, dh
    and ah, dl
    or al, ah
    stosb
    mov cx, [cs_wb0]                ; the right run: the bytes after it
    add cx, [cs_wbn]
    sub cx, bp
    dec cx
    mov al, dh
    mov ah, dh
    FILLRUN
    pop di
    jmp short .fbn
.fbw:
    mov al, dl                      ; a whole view row of one pattern
    mov ah, dl
    mov cx, [cs_wbn]
    shr cx, 1
    push di
    rep stosw
    pop di
.fbn:
    add di, 80
    cmp si, [cs_hzend]
    jb .fb
    pop bp
    mov bx, si                      ; BX = the row the band ended before,
    sub bx, cs_xl                   ; which is what .below counts from
    shr bx, 1
    jmp .below
    ; --- MODE X's BAND, THROUGH THE LATCHES (88.13.1.5.2.1): with the
    ;     textured ground on one side of the horizon every band row is a
    ;     ground run and a sky run, and the old row filler asked cs_mxlon and
    ;     cs_mxloff about both: four calls a row, ~480 cycles of an 8088 for a
    ;     test whose answer is the BAND's - 24 ms of a 266 ms banked frame.
    ;     So EVERY Mode X band is laid by cs_hzrow_mxl in WRITE MODE 1: a
    ;     plain side is a latch byte too (its colour in all four planes at
    ;     CS_MXPAT + 8 or + 9, written once here - which writes exactly the
    ;     pixels the colour byte did), each side's latch byte per row phase
    ;     is worked out once here, and a row is two latch reads and its
    ;     stores, with no call and no test in it - and the old filler and
    ;     cs_hzwhole, whose only caller it was, are gone.
    ;     (Two passes - every row's sky, then every row's ground - were built
    ;     first and measured WORSE, 265.6 -> 320.1 ms, a split row's own
    ;     setup being the bigger cost and they paid it twice; and keeping the
    ;     mode armed across the band with the calls left in measured 265.67,
    ;     no gain at all: the calls were the cost, not the OUTs)
.bmx:
    push bx
    push cx
    push dx
    push di
    call cs_mxloff                  ; (write mode 0 to lay the latch bytes)
    push es
    mov es, [cs_tseg]
    mov dx, 0x3C4                   ; every plane
    mov ax, 0x0F02
    out dx, ax
    mov si, [cs_hzlt]               ; each side's colour, for a side that
    mov al, [si]                    ; is PLAIN - a latch byte of one colour
    mov [es:CS_MXPAT + 8], al       ; in all four planes is that colour
    mov si, [cs_hzrt]
    mov al, [si]
    mov [es:CS_MXPAT + 9], al
    pop es
    mov si, [cs_hzlt]               ; ...and each side's latch byte a phase
    mov di, cs_mxla4
    mov dx, CS_MXPAT + 8
    call cs_mxadr4
    mov si, [cs_hzrt]
    mov di, cs_mxra4
    mov dx, CS_MXPAT + 9
    call cs_mxadr4
    call cs_mxarm
    pop di
    pop dx
    pop cx
    pop bx
.blx:
    push cx
    push bx
    push di
    mov si, bx
    shl si, 1
    mov cx, [cs_xl + si]
    cmp cx, 0x7FFF
    jne .sx
    mov cx, [cs_wx1]                ; untouched inside the band: the left
    inc cx                          ; side's whole row
.sx:
%ifdef CSHZPROBE
    call cs_probe_hz                ; what an incremental refill would cost
%endif
    and si, 6                       ; the row's phase, x 2
    mov ax, [cs_mxla4 + si]
    mov [cs_mxla], ax
    mov ax, [cs_mxra4 + si]
    mov [cs_mxra], ax
    call cs_hzrow_mxl
    pop di
    pop bx
    pop cx
    add di, 80
    inc bx
    loop .blx
    call cs_mxloff                  ; ...and write mode 0 again, once
.below:
    mov cx, [cs_wh]                 ; ...and the rows below it
    sub cx, bx
    mov al, [cs_hzbl]
    call cs_hzrows
    test byte [cs_setfill], CSFL_TERRAIN
    jnz .out
    cmp byte [cs_hzhave], 0         ; the ground is off: what tells the sky
    je .out                         ; from the earth is this one segment
    mov al, CSI_LINE
    call cs_setink
    ; --- AND IT BELONGS TO NO OBJECT (SPEC.md 88.13.3.1). cs_skyground runs
    ;     BEFORE cs_scene, so all three of the words cs_seg reads here are
    ;     the PREVIOUS frame's last object's: cs_pinview would skip the clip,
    ;     cs_pwhole would skip the marking, and cs_markacc would accumulate
    ;     into an object box that cs_drawobj resets for its first object a
    ;     moment later. That is the panel's situation exactly, and cs_pclip
    ;     already says so in as many words - so the horizon takes the same
    ;     three stores, and cs_ownmk makes it mark its own rows.
    mov byte [cs_pinview], 0
    mov byte [cs_pwhole], 0
    mov byte [cs_ownmk], -1
    mov ax, [cs_hzsa]
    mov bx, [cs_hzsa+2]
    mov cx, [cs_hzsa+4]
    mov dx, [cs_hzsa+6]
    call cs_seg
    mov byte [cs_ownmk], 0
.out:
    ret

%ifdef CSHZPROBE
; -----------------------------------------------------------------------------
; cs_probe_hz - PROBE ONLY: a split row is about to be refilled WHOLE and its
;               span set to the whole view (88.3.1). Record what an
;               INCREMENTAL refill would have touched instead - the bytes
;               between last frame's crossing and this one, and no others.
;               in: BX = the row, CX = the crossing x.  Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_hz:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    inc word [cs_dbg_hzrow]
    mov ax, [cs_wbn]
    add [cs_dbg_hzby], ax           ; what it lays today: the whole row
    mov ax, cx                      ; ...and the crossing's BYTE column
    or ax, ax
    jns .p
    xor ax, ax
.p:
    mov cl, 3
    shr ax, cl
    cmp ax, [cs_wbn]
    jb .ok
    mov ax, [cs_wbn]
    dec ax
.ok:
    mov si, bx
    mov dl, [cs_hzpb + si]          ; last frame's, 0xFF = it was not split
    mov [cs_hzpb + si], al
    cmp dl, 0xFF
    je .whole
    mov dh, al
    cmp dl, dh
    jbe .ord
    xchg dl, dh
.ord:
    mov al, dh
    sub al, dl
    cmp al, 0
    jne .some
    inc word [cs_dbg_hzsame]        ; the crossing did not move a byte at all
.some:
    xor ah, ah
    add ax, 3                       ; a byte either side, and the crossing's
    add [cs_dbg_hzinc], ax
    cmp ax, [cs_dbg_hzmax]
    jbe .out
    mov [cs_dbg_hzmax], ax
    jmp short .out
.whole:
    mov ax, [cs_wbn]
    add [cs_dbg_hzinc], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret
%endif

; cs_hzkeep - bank the horizon's two ends (AX/BX to CX/DX) for the line the
;             ground's fill leaves behind when it is off. Preserves all
cs_hzkeep:
    mov [cs_hzsa], ax
    mov [cs_hzsa+2], bx
    mov [cs_hzsa+4], cx
    mov [cs_hzsa+6], dx
    mov byte [cs_hzhave], 1
    ret

; -----------------------------------------------------------------------------
; cs_hzrows - CX whole rows of kind AL (0 sky, 1 ground) from row BX, whose
;             first view byte is ES:DI. Advances BX and DI past them.
;             Clobbers AX, CX, SI.
; -----------------------------------------------------------------------------
cs_hzrows:
    or cx, cx
    jle .out
    mov dl, al                      ; DL = the kind (cs_inkpat zeroes AH: the
    call cs_inkpat                  ; first build kept it there and every row
    mov [cs_hzkt], si               ; was recorded as kind 0, so a row that
    mov dh, 0                       ; ...and is it a LATCHED pattern (Mode X's
    cmp byte [cs_back], CSB_MODEX   ; textured ground, 88.13.1.5.2)? Asked
    jne .nl                         ; once a call, so a row on every other
    cmp byte [si], CS_MXLAT         ; backend pays one compare and no more
    jb .nl
    inc dh
    call cs_mxarm                   ; ...and write mode 1, once for the call
.nl:
    mov [cs_hzlat], dh
    mov al, dl                      ; went from ground to sky was "as it was"
    mov si, bx                      ; and never refilled - 88.3.1)
    shl si, 1
    add si, [cs_spprv]              ; SI -> last frame's span of row BX
.r:
    cmp al, [cs_rowkind + bx]
    jne .kind                       ; a new kind: the whole row goes over
    mov dx, [si]                    ; the same kind: what last frame drew on
    cmp dl, dh                      ; the row IS the erase - the blit carries
    ja .next                        ; it, being in the union - and an
    push cx                         ; untouched row reads FF/00 and is left
    push di                         ; alone (88.3.1)
    mov cl, dl
    xor ch, ch
    sub cx, [cs_wb0]                ; the span is absolute bytes of the row
    add di, cx                      ; and DI is the view's first: the first
    mov cl, dh                      ; build added them and refilled fifteen
    xor ch, ch                      ; ...AND CH IS CLEARED AGAIN HERE, because
                                    ; the subtract above BORROWS for a span
                                    ; that starts left of the view and leaves
                                    ; CH = 0xFF. The count below is a BYTE
                                    ; subtract, so CH survived into it and the
                                    ; `rep` ran 65,000 bytes instead of twenty:
                                    ; cs_skyground 25.13 ms -> 490.52, and a
                                    ; write far past the row (FIELD-NOTES 41).
                                    ; DI moving backwards is CORRECT - the
                                    ; refill may start in the row's left
                                    ; margin - and only the count was wrong
    sub cl, dl                      ; bytes to the right of everything, which
    inc cx                          ; left the left of every object on the
    jmp short .fill                 ; glass and a bar past the view's edge
.kind:
    mov [cs_rowkind + bx], al
    push cx
    push di
    mov di, [cs_spcur]              ; the set's row range takes the row...
    cmp bx, [di - 4]
    jge .y0
    mov [di - 4], bx
.y0:
    cmp bx, [di - 2]
    jle .y1
    mov [di - 2], bx
.y1:
    mov di, bx                      ; ...and its span is the whole view
    shl di, 1
    add di, [cs_spcur]
    mov cx, [cs_fullspan]
    mov [di], cx
    pop di
    push di
    mov cx, [cs_wbn]
.fill:                              ; CX bytes at ES:DI in the kind's pattern
    push ax
    push si
    mov si, bx
    and si, 3
    add si, [cs_hzkt]
    mov al, [si]
    pop si
    cmp byte [cs_hzlat], 0
    jne .lat
    mov ah, al
    shr cx, 1                       ; the 8088's bus is a byte wide: an odd
    rep stosw                       ; address costs a word store nothing
    jnc .even
    stosb
.even:
    pop ax
    pop di
    pop cx
.next:
    add di, 80
    inc bx
    inc si
    inc si
    LOOPF .r
.out:
    jmp cs_mxloff                   ; (a latched run's mode, if one was armed)
.lat:                               ; the same row through the LATCHES: the
    push si                         ; data byte is ignored and each store
    mov si, ax                      ; writes the row phase's four pixels.
    and si, 7                       ; Inline, with the mode armed once at the
    mov al, [es:CS_MXPAT + si]      ; top: a call to cs_mxlon a row was ~200
    pop si                          ; cycles of an 8088 a ground row
    shr cx, 1
    rep stosw
    jnc .even                       ; (write mode 1 stays armed for the next
    stosb                           ; row: cs_hzrows' exit disarms it)
    jmp short .even

; -----------------------------------------------------------------------------
; cs_markrows - rows SI..DI of the view had bytes [cs_mklo]..[cs_mkhi] drawn
;               on: the blit carries them, and the next frame's sky/ground
;               pass refills the rows
; preserves AX, BX, CX, DX; clobbers SI, DI
; -----------------------------------------------------------------------------
cs_markrows:
    push ax
    push bx
    push cx
    push dx
    mov bx, [cs_spcur]
    cmp si, [bx - 4]                ; the set's row range takes them
    jge .y0
    mov [bx - 4], si
.y0:
    cmp di, [bx - 2]
    jle .y1
    mov [bx - 2], di
.y1:
    mov ax, [cs_mklo]               ; AL = the first byte, AH = the last
    mov cx, di
    sub cx, si
    inc cx                          ; CX = rows
    mov bx, si
    shl bx, 1
    add bx, [cs_spcur]              ; BX -> the first row's pair
.m:                                 ; one read and one write a row: the
    mov dx, [bx]                    ; first build compared and stored each
    cmp dl, al                      ; byte on its own and set a dirty bit
    jbe .lo                         ; besides, which the sky pass now reads
    mov dl, al                      ; off the pair itself (88.3.1) - at
.lo:                                ; 170 cycles a row it was a quarter of
    cmp dh, ah                      ; the tower frame (88.3.2)
    jae .hi
    mov dh, ah
.hi:
    mov [bx], dx
    inc bx
    inc bx
    LOOPF .m
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_markacc - the same arguments, ACCUMULATED into the object's mark
;              instead: a polygon or a segment costs four compares, and the
;              per-row walk is paid once per object by cs_drawobj. A
;              wireframe tower is 32 segments over the same hundred rows,
;              and marking each was dearer than walking it (SPEC.md 88.3.2)
; preserves AX, BX, CX, DX
cs_markacc:
    cmp si, [cs_oby0]
    jge .y0
    mov [cs_oby0], si
.y0:
    cmp di, [cs_oby1]
    jle .y1
    mov [cs_oby1], di
.y1:
    push ax
    mov al, [cs_mklo]
    cmp al, [cs_oblo]
    jae .lo
    mov [cs_oblo], al
.lo:
    mov al, [cs_mkhi]
    cmp al, [cs_obhi]
    jbe .hi
    mov [cs_obhi], al
.hi:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_markstep - AX/BX and CX/DX are a CLIPPED segment's two ends: widen each
;               row's OWN span by the bytes the segment covers on THAT row,
;               where cs_markacc hands every row of the box to cs_markrows
;               (SPEC.md 88.3.2.2)
; preserves AX, BX, CX, DX
;
; The byte column is MONOTONIC along a segment, so a row's interval is the
; column at its top edge and the column at the next row's - one 8.8
; accumulator and one `add` a row on a loop that reads, compares and writes a
; pair anyway. That is ~26 cycles a row more than cs_markrows' body, against
; a blit that carries 4.5 cycles a byte and was being given the whole box.
;
; THE WIDENED INTERVAL IS CLAMPED TO THE VIEW, in the TABLE that widens it
; (SPEC.md 88.3.2.3.6). cs_hzrows' erase arm and cs_blit both take the span at
; its word - the first refills [lo,hi] as absolute bytes of the row, the second
; copies the same range to the card - so a pair naming a byte outside the view
; lays a ground byte into the box border, and one whose low byte borrows past
; zero lands a quarter of the way into a row three lines down. The ENDS are not
; clamped, because pulling an end MOVES THE INTERPOLATED LINE (FIELD-NOTES 41);
; the OUTPUT is, which is a different quantity and costs nothing at all.
; -----------------------------------------------------------------------------
CS_MKD_ROWS equ 8               ; a segment shallower than this has a tight
CS_MKD_PX   equ 128             ; box already, and so has a narrower one.
                                ; 128 PIXELS IS 16 BYTES AND THE BREAK-EVEN
                                ; IS 8: a stepped row costs its own 1-2 bytes
                                ; plus 2 x CS_MKD_SLOP, so a box narrower
                                ; than ~8 bytes is already CHEAPER than
                                ; stepping it. This is MARGIN over that and
                                ; nothing more - it measured identically to
                                ; 64 on every angle, because what the gate
                                ; actually turns away at level is the ROW
                                ; test: a level Seine is two rows
CS_MKD_SLOP equ 4               ; bytes either side of the stepped interval.
                                ; THE WIDENING IS A TABLE READ (cs_mktabs), so
                                ; the clamp to the view rides in it and the row
                                ; body pays for neither: a stored pair can name
                                ; no byte outside [wb0, wb0+wbn-1], where the
                                ; refill and the blit both need it, and a low
                                ; byte can never borrow to 0xFF, which is the
                                ; span set's EMPTY sentinel besides

; -----------------------------------------------------------------------------
; cs_mktabs - the widen-and-clamp tables for cs_markstep, built once a bracket
;             (SPEC.md 88.3.2.3.6). Clobbers AX, BX, CX, DX
;
;   cs_mkslo[c] = max(wb0,            c - CS_MKD_SLOP)
;   cs_mkshi[c] = min(wb0 + wbn - 1,  c + CS_MKD_SLOP)
;
; A column is never above 79 on any backend, every row being 80 bytes; the
; tables are 96 entries, which is that bound plus sixteen - and an overrun
; would be benign anyway, both tables holding clamped columns, so reading one
; for the other still names a byte of the view.
; -----------------------------------------------------------------------------
CS_MKT_N    equ 96
cs_mktabs:
    mov dx, [cs_wb0]                ; DL = the view's first byte...
    mov ax, [cs_wb0]
    add ax, [cs_wbn]
    dec ax
    mov dh, al                      ; ...DH its last
    xor bx, bx
    mov cx, CS_MKT_N
.t:
    mov al, bl
    sub al, CS_MKD_SLOP             ; the low side: a borrow IS below the view
    jc .l0
    cmp al, dl
    jae .l1
.l0:
    mov al, dl
.l1:
    mov [cs_mkslo + bx], al
    mov al, bl
    add al, CS_MKD_SLOP             ; ...and a carry IS above it
    jc .h0
    cmp al, dh
    jbe .h1
.h0:
    mov al, dh
.h1:
    mov [cs_mkshi + bx], al
    inc bx
    LOOPF .t
    ret

cs_markstep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp bx, dx                      ; the TOP end first
    jle .ord
    xchg ax, cx
    xchg bx, dx
.ord:
    mov si, [cs_spcur]              ; the set's row range takes the segment,
    cmp bx, [si - 4]                ; which is what cs_markrows would have
    jge .r0                         ; done for it (88.3.3.1)
    mov [si - 4], bx
.r0:
    cmp dx, [si - 2]
    jle .r1
    mov [si - 2], dx
.r1:
    mov di, dx
    sub di, bx                      ; DI = dy, at least CS_MKD_ROWS
    mov bp, cx                      ; the two ends as BYTES
    mov cl, [cs_kshift]
    shr ax, cl
    shr bp, cl
    ; --- AND THE ENDS ARE NOT CLAMPED (docs/FIELD-NOTES.md 41) ---------------
    ; They used to be pulled CS_MKD_SLOP inside the view so the byte
    ; arithmetic below could not wrap - and PULLING AN END MOVES THE
    ; INTERPOLATED LINE, by up to the clamp, all along it. The widening then
    ; had to cover the displacement AND the divide's truncation, which needs
    ; W > C, while keeping the stored low byte at or above wb0 needs C >= W.
    ; Those cannot both hold, which is why every widening was either short of
    ; the ink (3,558 rows leaked at C=3 W=3) or below the view (W=6 put a
    ; stored lo at wb0-3 and found the count bug above).
    ;
    ; cs_seg CLIPS to the view before it marks, so both ends are already
    ; inside it and the clamp was a no-op on geometry and a bug on placement.
    ; Deleted. Modelled over 40,000 segments against the true Bresenham range:
    ; unclamped needs W = 4 for ZERO leaks (W = 3 leaves 64, worst 1 byte).
    ;
    ; WHAT THE ENDS NO LONGER DO, THE OUTPUT DOES: the widened pair would
    ; otherwise reach wb0-4 and wb0+wbn+3, which is outside the row's view on
    ; every backend and outside the ROW at CSZ_FULL, where the view is the
    ; whole box and has no margin at all. cs_mktabs clamps it (88.3.2.3.6).
    mov [cs_mkb2], bp               ; the BOTTOM end's byte, kept exact: the
                                    ; accumulator is truncated at every step
                                    ; and a shallow segment's last row is a
                                    ; whole step wide, so stepping into it
                                    ; under-marks by up to that
    sub bp, ax                      ; BP = the byte span, signed, |BP| < 128
    mov cx, ax                      ; CX = the top end's byte
    mov ax, bp
    mov ah, al
    xor al, al                      ; AX = the span << 8, exact for |BP| < 128
    cwd
    idiv di                         ; AX = the step a row, 8.8 signed
    mov si, ax
    mov dh, cl
    xor dl, dl                      ; DX = the top byte, 8.8
    mov cx, di                      ; CX = dy rows stepped...
    mov di, bx
    shl di, 1
    add di, [cs_spcur]              ; DI -> the top row's pair
    xor bh, bh                      ; ...and BX is the tables' index from here:
                                    ; BL takes the column, BH stays zero for
                                    ; the whole walk
    or si, si
    js .neg
.pos:                               ; the column RISES down the segment
    mov bl, dh                      ; SLOP EITHER SIDE, and both sides need it:
    add dx, si                      ; the divide truncates, and cs_seg's own
    mov al, [cs_mkslo + bx]         ; Bresenham does not put a row's pixel
    mov bl, dh                      ; exactly where the row's top edge is. The
    mov ah, [cs_mkshi + bx]         ; TABLE clamps as it widens, so the pair is
                                    ; inside the view with no test here - and
                                    ; the body is TWO BYTES SHORTER than the
                                    ; `sub`/`add` pair it replaces, which on an
                                    ; 8088 is what decides it: at 30 bytes the
                                    ; loop was FETCH-bound at 130 clocks and at
                                    ; 28 it is bound at 121, against 117 of work
    cmp al, [di]
    jae .p1
    mov [di], al
.p1:
    inc di
    cmp ah, [di]
    jbe .p2
    mov [di], ah
.p2:
    inc di
    LOOPF .pos
    jmp short .last
.neg:                               ; ...or FALLS down it, which only swaps
    mov bl, dh                      ; which end of the row's interval the top
    mov ah, [cs_mkshi + bx]         ; edge is
    add dx, si
    mov bl, dh
    mov al, [cs_mkslo + bx]
    cmp al, [di]
    jae .n1
    mov [di], al
.n1:
    inc di
    cmp ah, [di]
    jbe .n2
    mov [di], ah
.n2:
    inc di
    LOOPF .neg
.last:                              ; the row the segment ENDS on: from where
    mov cl, [cs_mkb2]               ; the accumulator got to, to the end's
    mov bl, dh                      ; TRUE byte - which is the whole of that
    cmp bl, cl                      ; row's ink and no more. CX is ZERO here,
    jbe .z0                         ; LOOPF having run it out, so CL is the
    xchg bl, cl                     ; scratch the swap wants and BH keeps the
.z0:                                ; table index's zero
    mov al, [cs_mkslo + bx]
    mov bl, cl
    mov ah, [cs_mkshi + bx]
    cmp al, [di]
    jae .z1
    mov [di], al
.z1:
    cmp ah, [di+1]
    jbe .z2
    mov [di+1], ah
.z2:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_inkpat - AL = an ink; out SI = its four pattern bytes
cs_inkpat:
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    add si, [cs_inktab]
    ret

; cs_hzsteep_t - AX = Y; out AX = ((ny Y)/scly x sclx)/nx, clamped
cs_hzsteep_t:
    imul word [cs_hny]
    idiv word [cs_scly]
    imul word [cs_sclx]
    mov bx, [cs_hnx]
    call cs_cdiv
    ret

; cs_hzflat_t - AX = X; out AX = ((nx X)/sclx x scly)/ny, clamped
cs_hzflat_t:
    imul word [cs_hnx]
    idiv word [cs_sclx]
    imul word [cs_scly]
    mov bx, [cs_hny]
    call cs_cdiv
    ret

; cs_cdiv - AX = DX:AX / BX, or CF=1 (and AX = +-30000) when the quotient
; would not fit a word: |DX| < |BX|/2 is sufficient (SPEC.md 85.5). Clobbers
; CX, DX.
cs_cdiv:
    mov cx, bx
    or cx, cx
    jns .p
    neg cx
.p:
    shr cx, 1
    push dx
    or dx, dx
    jns .q
    not dx
.q:
    cmp dx, cx
    pop dx
    jae .no
    idiv bx
    clc
    ret
.no:
    mov ax, 30000
    xor dx, bx                      ; the sign the quotient would have had
    jns .s
    neg ax
.s:
    stc
    ret

; cs_abs32 - DX:AX = |DX:AX|
cs_abs32:
    or dx, dx
    jns .o
    neg dx
    neg ax
    sbb dx, 0
.o:
    ret


; -----------------------------------------------------------------------------
; cs_mxlon / cs_mxloff - MODE X'S TEXTURED GROUND (SPEC.md 88.13.1.5.2).
;
; A Mode X byte is four pixels, one a plane, and a fill stores ONE byte into
; every plane - so a row is one colour across, and the only dither it could
; carry was a row of a second colour, which is the lines the field found "not
; quite right". The VGA has the answer in hardware: in WRITE MODE 1 a store
; copies the four LATCHES into the enabled planes and ignores the CPU's byte,
; and any read loads the latches. So cs_grsync keeps eight bytes past the
; two pages at CS_MXPAT, one a row phase of two 4x4 tiles - the haze's ground
; band's and the ground's - each holding a row across its four planes, and a
; latched run is: arm write mode 1, read the phase's byte, store exactly as
; before. A checker costs nothing at all per byte.
;
; The pattern byte says which: a latched row's bytes are CS_MXLAT + the byte
; index (0xF8..0xFB the band, 0xFC..0xFF the ground: DAC indices nothing else
; names), so a site that
; already has the byte in hand tests one compare. cs_mxlon takes it in AL and
; does nothing for any other byte; ES = the VGA. Both preserve every register.
;
; THE PORT IS THE PRICE, so it is touched as little as it can be: the mode
; register's other bits are read ONCE a frame (cs_mxtile, into [cs_mxgc5]),
; a change of mode is one word OUT, and [cs_mxon] keeps write mode 1 armed
; across a run of ground rows, so a row after the first costs the latch READ
; alone. The first build read-modified-wrote the register twice a run and
; was 16.75 ms of a 402 ms banked frame on a 4.77 MHz VGA XT
; ----------------------------------------------------------------------------
cs_mxlon:
    cmp al, CS_MXLAT
    jb .no
    push ax
    push si
    call cs_mxarm
    and ax, 7                       ; (0..3 the haze band's, 4..7 the ground's)
    add ax, CS_MXPAT
    mov si, ax
    mov al, [es:si]                 ; a READ: the latches take the phase's
    pop si                          ; four pixels
    pop ax
.no:
    ret

; cs_mxarm - write mode 1, if it is not already. Preserves every register
cs_mxarm:
    cmp byte [cs_mxon], 0
    jne .out
    push ax
    push dx
    mov dx, 0x3CE                   ; Graphics Mode: write mode 1, the rest
    mov ah, [cs_mxgc5]              ; (256-colour shift) as it is
    or ah, 1
    mov al, 5
    out dx, ax
    mov byte [cs_mxon], 1
    pop dx
    pop ax
.out:
    ret

cs_mxloff:
    cmp byte [cs_mxon], 0
    je .no
    push ax
    push dx
    mov dx, 0x3CE
    mov ah, [cs_mxgc5]              ; write mode 0
    mov al, 5
    out dx, ax
    mov byte [cs_mxon], 0
    pop dx
    pop ax
.no:
    ret

; cs_fillrun - CX bytes of AL (= AH) at ES:DI, words where it can. Advances
; DI past them. Clobbers CX. CX may be 0 or negative (nothing is stored).
cs_fillrun:
    cld
    FILLRUN
    ret

; -----------------------------------------------------------------------------
; cs_hzrow_mxl - a band row, Mode X (SPEC.md 88.13.1.5.2.1), with write mode
;   1 armed by the band and no call or test in it. In: BX = the row, CX = the crossing x, DI = the row's first view
;   byte, [cs_mxla]/[cs_mxra] the left and right sides' latch bytes. A read
;   loads the latches and every store after it writes them, the CPU's byte
;   ignored - so the runs and the crossing byte are the old row filler's, and
;   the pattern bytes they used to carry are two reads. Clobbers everything
;   but BP
; -----------------------------------------------------------------------------
cs_hzrow_mxl:
    mov es, [cs_tseg]
    cmp cx, [cs_wx0]
    jg .l1
    mov si, [cs_mxra]               ; the crossing off the left: the right
    jmp short .whole                ; side's whole row
.l1:
    cmp cx, [cs_wx1]
    jle .split
    mov si, [cs_mxla]               ; ...off the right: the left side's
.whole:
    mov al, [es:si]                 ; the latches
    mov dx, 0x3C4
    mov ax, 0x0F02                  ; every plane
    out dx, ax
    mov cx, [cs_wbn]
    shr cx, 1
    cld
    rep stosw
    ret
.split:
    shl bx, 1
    mov di, [cs_rowoff + bx]
    add di, [cs_tbase]
    mov bx, cx
    shr bx, 1
    shr bx, 1                       ; BX = the crossing byte
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax                      ; every plane for the runs
    mov si, [cs_mxla]
    mov al, [es:si]                 ; the LEFT side's latches
    push cx
    push di
    mov cx, bx
    sub cx, [cs_wb0]
    add di, [cs_wb0]
    call cs_fillrun                 ; ...its run
    pop di
    pop cx
    and cx, 3
    jz .noleft
    mov si, cx
    mov ah, [cs_pmr + si - 1]       ; planes 0..i-1: the left side's share
    mov al, 2                       ; of the crossing byte
    out dx, ax
    mov [es:di+bx], al
.noleft:
    mov si, cx
    mov ah, [cs_pml + si]           ; planes i..3: the right side's
    mov al, 2
    out dx, ax
    mov si, [cs_mxra]
    mov al, [es:si]                 ; the RIGHT side's latches
    mov [es:di+bx], al
    mov ax, 0x0F02
    out dx, ax
    mov cx, [cs_wb0]
    add cx, [cs_wbn]
    sub cx, bx
    dec cx
    add di, bx
    inc di
    jmp cs_fillrun                  ; ...and its run

; cs_mxadr4 - SI = a side's four pattern bytes, DI = four words: each phase's
;             latch byte, CS_MXPAT + its index for a latched byte and DX -
;             the side's own plain latch byte - for any other. Clobbers AX,
;             CX, SI, DI
cs_mxadr4:
    mov cx, 4
.p:
    lodsb
    cmp al, CS_MXLAT
    mov ax, dx
    jb .w
    mov al, [si - 1]
    and ax, 7
    add ax, CS_MXPAT
.w:
    mov [di], ax
    inc di
    inc di
    loop .p
    ret

; =============================================================================
; The polygon filler (SPEC.md 88.4.2)
; =============================================================================

; -----------------------------------------------------------------------------
; cs_edge - trace one edge into cs_xl or cs_xr, one x per row
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (box coords, |x| <= 4000ish);
;      [cs_eside] = 1 the polygon's LEFT chain (stores into cs_xl), 2 its
;      right (cs_xr) - and NOTHING ELSE. A horizontal edge is recognised
;      here, from BX = DX, and lays both ends of its one row itself; there
;      is no min/max arm for a sloped edge any more, because the only thing
;      that read a chain back was what made the sentinel pass necessary
;      (88.4.2.2), and a caller that does not know its winding cannot use
;      this routine
; clobbers everything
;
; Bresenham's integer step: q = floor(dx/dy), r = dx - q dy, and per row
; x += q; err += r; if err >= dy then x += 1, err -= dy. An edge that starts
; above the view is JUMPED to row 0 with one multiply and one divide, because
; a vertex a hand's breadth past the near plane projects thousands of rows
; away and stepping there would cost more than the face. A convex polygon's
; edges are one chain or the other, so a row costs ONE store and no compare.
; -----------------------------------------------------------------------------
cs_edge:
    cmp bx, dx
    jne .slope
    ; --- horizontal: one row, both ends -------------------------------------
    or bx, bx
    js .out
    cmp bx, [cs_wh]
    jge .out
    cmp ax, cx
    jle .ord
    xchg ax, cx
.ord:
    ; --- UNCONDITIONAL, which is what retires the sentinel pass (88.4.2.2).
    ;     A convex polygon at its top row IS its top edge, and at its bottom
    ;     row its bottom edge - those are the only two rows a horizontal edge
    ;     can be on - so this pair is the row's extent and not a candidate
    ;     for it. Taking min/max here is what needed a value to have been
    ;     laid, and laying it was two `rep stosw` down every polygon
    shl bx, 1
    mov [cs_xl + bx], ax
    mov [cs_xr + bx], cx
.out:
    ret
.slope:
    jl .down
    xchg ax, cx                     ; make (AX, BX) the upper end
    xchg bx, dx
.down:
    cmp bx, [cs_wh]                 ; wholly below the view
    jge .out
    or dx, dx                       ; wholly above it
    js .out
    mov [cs_ey2], dx                ; the lower end's row
    sub dx, bx                      ; DX = dy > 0
    mov [cs_edy], dx
    sub cx, ax                      ; CX = dx, signed
    mov si, ax                      ; SI = x
    mov ax, cx
    cwd
    idiv word [cs_edy]              ; AX = trunc(dx/dy), DX = the remainder
    or dx, dx
    jns .fl
    dec ax                          ; ...made a FLOOR: q down one, r up by dy
    add dx, [cs_edy]
.fl:
    mov [cs_eq], ax
    mov [cs_er], dx
    xor di, di                      ; DI = err
    or bx, bx
    jns .inview
    ; --- jump n = -y1 rows: x += n q + (n r) div dy, err = (n r) mod dy ------
    mov cx, bx
    neg cx                          ; CX = n
    mov ax, [cs_eq]
    imul cx
    add si, ax
    mov ax, [cs_er]
    mul cx                          ; DX:AX = n r (both non-negative)
    div word [cs_edy]
    add si, ax
    mov di, dx
    xor bx, bx
    mov ax, [cs_eq]                 ; ...and THIS ARM is the only one that
    mov dx, [cs_er]                 ; clobbers them, so the reload lives here
.inview:
    mov cx, [cs_ey2]                ; the last row to trace, clipped
    cmp cx, [cs_wh]
    jl .cl
    mov cx, [cs_wh]
    dec cx
.cl:
    sub cx, bx
    inc cx                          ; CX = rows to trace
    jle .out
    shl bx, 1
    ; --- BX BECOMES A REAL POINTER, and that is what merges the two chains
    ;     into ONE loop (SPEC.md 88.4.2.3). The left arm and the right were
    ;     the same 36 lines differing only in the array, and the array is
    ;     also why each store cost FOUR bytes: `mov [cs_xl + bx], si` is a
    ;     disp16, `mov [bx], si` is two bytes and `mov [bx + 2], si` three.
    ;     This loop is fetch-bound like every other one here - 90 cycles a
    ;     row over 689 rows a frame - so three bytes off every two rows is
    ;     the whole of the saving, and about fifty bytes of image come off
    ;     with the duplicate.
    mov bp, [cs_edy]
    cmp byte [cs_eside], 1
    jne .rt
    add bx, cs_xl
    jmp short .step
.rt:
    add bx, cs_xr
    ; the chain, two rows an iteration: the 8088 charges a `loop` and its
    ; refill about as much as the row's own work
.step:
    shr cx, 1
    jnc .s0
    mov [bx], si
    add si, ax
    add di, dx
    cmp di, bp
    jb .s1
    sub di, bp
    inc si
.s1:
    inc bx
    inc bx
.s0:
    jcxz .send
.s2:
    mov [bx], si
    add si, ax
    add di, dx
    cmp di, bp
    jb .s3
    sub di, bp
    inc si
.s3:
    mov [bx + 2], si
    add si, ax
    add di, dx
    cmp di, bp
    jb .s4
    sub di, bp
    inc si
.s4:
    add bx, 4
    loop .s2
.send:
    ret

; -----------------------------------------------------------------------------
; cs_poly - fill one convex polygon
; in:  SI = CX pairs of (x, y) words in box coords, [cs_ink] set via
;      cs_setink, [cs_pwind] = 0 when an edge whose y INCREASES along the
;      list is on the polygon's left (the winding a visible face has), 1 the
;      other way round
; clobbers everything
; -----------------------------------------------------------------------------
cs_poly:
    mov [cs_pn], cx
    mov [cs_pvp], si
    cld
    ; --- the row range, and a cheap reject ----------------------------------
    mov bx, 0x7FFF                  ; BX = ymin, DX = ymax
    mov dx, -32768
    mov bp, 0x7FFF                  ; BP = xmin, DI = xmax
    mov di, -32768
.mm:
    lodsw
    cmp ax, bp
    jge .x1
    mov bp, ax
.x1:
    cmp ax, di
    jle .x2
    mov di, ax
.x2:
    lodsw
    cmp ax, bx
    jge .y1
    mov bx, ax
.y1:
    cmp ax, dx
    jle .y2
    mov dx, ax
.y2:
    loop .mm
    or dx, dx                       ; wholly above the view
    js .out
    cmp bx, [cs_wh]                 ; ...or below it
    jge .out
    cmp di, [cs_wx0]                ; ...or left of it
    jl .out
    cmp bp, [cs_wx1]                ; ...or right
    jg .out
    or bx, bx
    jns .t
    xor bx, bx
.t:
    cmp dx, [cs_wh]
    jl .b
    mov dx, [cs_wh]
    dec dx
.b:
    mov [cs_py0], bx
    mov [cs_py1], dx
    ; --- its bytes on those rows go to the glass, and the rows are dirty:
    ;     once for the polygon, off its bounding box -------------------------
    mov ax, bp
    cmp ax, [cs_wx0]
    jge .mx1
    mov ax, [cs_wx0]
.mx1:
    mov cx, di
    cmp cx, [cs_wx1]
    jle .mx2
    mov cx, [cs_wx1]
.mx2:
    mov [cs_pbx0], ax
    mov [cs_pbx1], cx
    ; --- ...AND WHETHER ANY ROW CAN REACH A VIEW EDGE (SPEC.md 88.4.5.1).
    ;     Every row's xl/xr lies between the box's own extremes, so a box
    ;     STRICTLY inside the view has no row that needs clamping and no row
    ;     that spans the view - which is the row loop's whole 25-byte clamp
    ;     block, fetched 257 times a frame for an answer the polygon already
    ;     has.
    ;
    ;     TOUCHING AN EDGE IS NOT REACHING PAST IT, and the first version of
    ;     this test could not tell them apart. `cs_edge` interpolates
    ;     strictly BETWEEN an edge's two endpoints - the above-view arm jumps
    ;     n rows along the edge rather than extrapolating past it - so every
    ;     xl and xr lies inside [bp, di], and a box with bp = wx0 has no row
    ;     that needs the left clamp: clamping xl to wx0 when xl >= wx0 is a
    ;     no-op. `jle`/`jge` therefore turned the block on for a box that
    ;     merely touched, and MEASURED, that was ONE POLYGON A FRAME
    ;     CARRYING 81.6 ROWS - 32% of every row in `turnhold` - not one of
    ;     which was clamped at either end (88.4.5.3).
    ;
    ;     The WHOLE-VIEW row is what a strict test could lose - it wants
    ;     xl <= wx0 and xr >= wx1, and with bp >= wx0 and di <= wx1 that can
    ;     only be bp = wx0 AND di = wx1 - and it does not, because nothing
    ;     lands there: `cs_fclip` clips to the FRUSTUM and not to the view,
    ;     and 88.5.7 puts its side crossings at 4z, about 1,772 pixels, so a
    ;     polygon that spans the view spans it by hundreds of pixels either
    ;     side. The eight bytes that tested for the tie were measured and
    ;     came back NEGATIVE - 0.1 ms of cs_poly against 0.1 ms of nothing -
    mov si, 1                       ; SI = 1: the general row loop
    cmp bp, [cs_wx0]
    jl .mx3                         ; a row can fall left of the view
    cmp di, [cs_wx1]
    jg .mx3                         ; ...or right of it
    xor si, si                      ; ...neither can happen: skip the block
.mx3:
    mov [cs_pnoclip], si
    cmp byte [cs_pwhole], 0         ; a whole object's box was taken off its
    jne .marked                     ; vertices (88.3.2)
    push bx
    push dx
    mov dx, cx
    mov cl, [cs_kshift]
    shr ax, cl
    shr dx, cl
    mov [cs_mklo], al
    mov [cs_mkhi], dl
    pop dx
    pop bx
    mov si, bx
    mov di, dx
    call cs_markacc
.marked:
    ; --- one row or two: the box IS the polygon at that size, and four
    ;     edges traced for it would cost more than the fill (SPEC.md 88.4.5)
    mov ax, dx
    sub ax, bx
    cmp ax, 1
    ja .trace
%ifdef CSPROBE
    inc word [cs_dbg_fbox]
%endif
    mov si, bx
    shl si, 1
    mov ax, [cs_pbx0]
    mov cx, [cs_pbx1]
    mov [cs_xl + si], ax
    mov [cs_xr + si], cx
    cmp bx, dx
    je .rows
    mov [cs_xl + si + 2], ax
    mov [cs_xr + si + 2], cx
    jmp .rows
.trace:
    ; --- NO SENTINEL PASS, AND NO ES FOR IT (SPEC.md 88.4.2.2). Two
    ;     `rep stosw` down the row range used to lay 0x7FFF and -32768 for
    ;     cs_edge to take min/max against - which is why this wanted
    ;     ES = DS at all. Nothing takes min/max against them now, and every
    ;     row of a convex polygon gets one left-chain store and one
    ;     right-chain store whatever else happens to it.
    ; --- every edge ---------------------------------------------------------
    mov si, [cs_pvp]
    mov cx, [cs_pn]
    mov [cs_pei], cx
.e:
    mov ax, [si]
    mov bx, [si+2]
    add si, 4
    cmp word [cs_pei], 1            ; the last edge closes on vertex 0
    jne .nx
    mov si, [cs_pvp]
.nx:
    mov cx, [si]
    mov dx, [si+2]
    push si
    mov byte [cs_eside], 0          ; a horizontal edge marks both ends...
    cmp bx, dx
    je .side
    mov byte [cs_eside], 1          ; ...a descending one is on the left
    jl .desc                        ; chain and an ascending one on the
    mov byte [cs_eside], 2          ; right, or the other way round when
.desc:                              ; the winding says so
    cmp byte [cs_pwind], 0
    je .side
    xor byte [cs_eside], 3
.side:
%ifdef CSPROBE
    call cs_probe_edge
    ; --- A/B 1: with [cs_dbl] every edge is traced TWICE. cs_edge is
    ;     idempotent - a chain takes the same value and `.both` takes min/max
    ;     - so the picture is identical and the frame's difference over the
    ;     trace count is what ONE trace costs, exactly. The push/pop and the
    ;     test are in BOTH arms, so the A/B prices the call alone
    push ax
    push bx
    push cx
    push dx
    cmp byte [cs_dbl], 0
    je .nodbl
    call cs_edge
.nodbl:
    pop dx
    pop cx
    pop bx
    pop ax
%endif
    call cs_edge
    pop si
    dec word [cs_pei]
    jnz .e
    ; --- the rows: the backend's loop (88.4.6) -------------------------------
.rows:
    ; --- TWO ROW LOOPS, and BP is what pays for the second (88.4.5.4). The
    ;     general loop still reads BP as the flag; the Hercules pair does
    ;     not, so it has BP for the row counter and the clamp inline -----
    mov bp, [cs_pnoclip]
    or bp, bp
    jnz .rowsc
    jmp [cs_rowsproc]
.rowsc:
    jmp [cs_rowsprocc]
.out:
%ifdef CSPROBE
    inc word [cs_dbg_fout]
%endif
    ret

; cs_polyrows - the general row loop: the row's bounds clamped, its pattern
;               by XLAT, and the backend's run called with the registers it
;               wants rather than through cs_hrun's dispatch, because the
;               8088 charges for every instruction byte (SPEC.md 85.3.4)
cs_polyrows:
    mov bx, [cs_py0]
    mov di, bx
    shl di, 1
    mov di, [cs_rowoff + di]
    add di, [cs_tbase]              ; DI = the row's base
    mov es, [cs_tseg]
.row:
    mov si, bx
    shl si, 1
    mov ax, [cs_xl + si]
    mov cx, [cs_xr + si]
    cmp ax, [cs_wx0]
    jge .c1
    mov ax, [cs_wx0]
.c1:
    cmp cx, [cs_wx1]
    jle .c2
    mov cx, [cs_wx1]
.c2:
    cmp ax, cx
    jg .nrow
    push bx
    push di
    mov si, cx                      ; SI = x2, BP = x1, DL = the pattern...
    mov bp, ax
    mov al, bl
    and al, 3
    mov bx, cs_pat
    xlat
    mov dl, al
    mov bx, di                      ; ...BX = the row base
    call [cs_hrunproc]
    pop di
    pop bx
.nrow:
    add di, 80
    inc bx
    cmp bx, [cs_py1]
    jle .row
    ret

; CS_POLYROW - the Hercules row loop, %1 = 1 for the arm that CLAMPS
;
; The run is INLINE: no dispatch, no second address computation, the ends
; blended by old ^ ((old ^ pattern) & mask), and the middle a `rep stosw` on
; whatever alignment - the 8088's bus is eight bits wide, so an odd address
; costs it nothing. Measured against the general loop: 2,000 cycles a
; full-width row to ~1,100 (88.4.6).
;
; IT IS ASSEMBLED TWICE and BP is what pays for that (88.4.5.4). The clamp
; used to be a block behind `or bp, bp / jne`, which is four bytes on every
; row of the program to ask a question the POLYGON already answered - and
; this loop is FETCH-BOUND, so four bytes is seventeen cycles. With the
; question moved to the caller BP is free: the row count goes in it, and the
; clipping arm carries its clamp INLINE with no jump out and back, which the
; rows that need neither end moved fall straight through (88.4.5.3 measured
; that at 43-100% of a clipping polygon's rows).
; -----------------------------------------------------------------------------
%macro CS_POLYROW 1
    mov bx, [cs_py0]
    mov di, bx
    shl di, 1
    mov di, [cs_rowoff + di]
    add di, [cs_tbase]              ; DI = the row's base
    mov es, [cs_tseg]
    mov bp, [cs_py1]
    sub bp, bx
    inc bp                          ; BP = the rows, counted DOWN
%%row:
    mov si, bx
    shl si, 1
    mov ax, [cs_xl + si]
    mov cx, [cs_xr + si]
%if %1
    cmp ax, [cs_wx0]                ; the clamp, INLINE - and both jumps are
    jle %%lo                        ; the rare way out of it
    cmp cx, [cs_wx1]
    jg %%hi
%endif
%%c2:
    cmp ax, cx
    jg %%nrow
%ifdef CSPROBE
    call cs_probe_prow              ; a ROW about to be laid, and how wide
%endif
    push bx
    ; --- BX IS DEAD here until it is rebuilt as the address below, so the
    ;     pattern index goes in BX and costs no SI at all - and the `and`
    ;     leaves BH zero, which is what the `mov bl, al` below needs -----
    and bx, 3
    mov dl, [cs_pat + bx]           ; DL = the row's pattern
    mov si, ax                      ; --- the two ends, one word table each
    shl si, 1                       ;     (88.4.5.5): AL = the first byte and
    mov ax, [cs_lend + si]          ;     AH = its mask, in ONE load
    mov si, cx
    shl si, 1
    mov cx, [cs_rend + si]          ; CL = the last byte, CH = its mask
    mov dh, ch                      ; DH = it, where the blend wants it...
    xor ch, ch                      ; ...and CX the byte alone, which the
                                    ; width and the run both need
    mov bl, al                      ; (BH is 0 - the `and bx, 3` left it so)
    add bx, di                      ; BX = the first byte's address
    sub cl, al                      ; CX = bytes - 1
    jz %%one
    mov al, [es:bx]
    xor al, dl
    and al, ah
    xor [es:bx], al                 ; the first byte
    mov si, bx
    add si, cx
    mov al, [es:si]
    xor al, dl
    and al, dh
    xor [es:si], al                 ; the last
    dec cx
    jz %%done
    push di
    mov di, bx
    inc di
    mov al, dl
    mov ah, dl
    shr cx, 1
    rep stosw
    jnc %%even
    stosb
%%even:
    pop di
%%done:
    pop bx
%%nrow:
    add di, 80
    inc bx
    dec bp
    jnz %%row
    ret
    ; --- the one-byte row, out of the loop's span (88.4.5.2) --------------
%%one:
    and ah, dh                      ; one byte holds both ends
    mov al, [es:bx]
    xor al, dl
    and al, ah
    xor [es:bx], al
    pop bx
    jmp short %%nrow
%if %1
    ; --- ...and the two clamps with it, reached only by a row that really
    ;     does reach a view edge -------------------------------------------
%%hi:
    mov cx, [cs_wx1]
    jmp %%c2
%%lo:
    cmp cx, [cs_wx1]
    jge %%full
    mov ax, [cs_wx0]
    jmp %%c2
    ; --- the whole view row: no masks, no ends, one store - the runway
    ;     under the wheels is forty of these a frame (88.5.5) ---------------
%%full:
    push bx
    push di
    and bx, 3
    mov al, [cs_pat + bx]
    mov ah, al
    add di, [cs_wb0]
    mov cx, [cs_wbn]
    shr cx, 1
    rep stosw
    pop di
    pop bx
    jmp %%nrow
%endif
%endmacro

; -----------------------------------------------------------------------------
; cs_polyrows_herc   - the arm for a polygon no row of which can reach a view
;                      edge, which cs_poly decides once (88.4.5.3)
; cs_polyrows_herc_c - ...and the arm that clamps. cs_rect always takes this
;                      one: its rows are clamped already, but a rectangle is
;                      how the runway under the wheels is drawn
; -----------------------------------------------------------------------------
cs_polyrows_herc:
    CS_POLYROW 0

cs_polyrows_herc_c:
    CS_POLYROW 1

; -----------------------------------------------------------------------------
; cs_endtab - the row loop's two END tables (88.4.5.5), built once a bracket
;
; cs_lend[x] = (cs_hlm[x & 7] << 8) | (x >> 3), and cs_rend the same with
; cs_hrm. That is exactly the AH:AL the left end wants and the CH:CL the
; right one does, so a masked end becomes ONE `mov` from a word table where
; it was an `and`, three `shr` and a byte load - ten bytes a row off a loop
; that is 80% fetch-bound. It is ~4 ms once, beside cs_ktabs, which is the
; same shape: a table built at setup because the inner loop cannot afford it.
; -----------------------------------------------------------------------------
cs_endtab:
    xor bx, bx                      ; BX = x
    xor si, si                      ; SI = 2x
    mov cl, 3
.e:
    mov ax, bx
    shr ax, cl                      ; AL = x >> 3, and AH is 0 with it
    mov di, bx
    and di, 7
    mov ah, [cs_hlm + di]
    mov [cs_lend + si], ax
    mov ah, [cs_hrm + di]
    mov [cs_rend + si], ax
    inc bx
    inc si
    inc si
    cmp bx, CS_ENDN
    jb .e
    ret

; -----------------------------------------------------------------------------
; cs_hrun - one masked run of the current ink, opaque
; in:  AX = x1 <= CX = x2 (box coords, inside the row), BX = the row
; clobbers everything. The row's pattern is the ink's byte for row & 3.
; -----------------------------------------------------------------------------
cs_hrun:
    mov si, bx
    and si, 3
    mov dl, [cs_pat + si]           ; DL = this row's pattern
    mov bp, ax                      ; BP = x1
    mov si, cx                      ; SI = x2
    shl bx, 1
    mov bx, [cs_rowoff + bx]
    add bx, [cs_tbase]              ; BX = the row base
    mov es, [cs_tseg]
    jmp [cs_hrunproc]

; --- the sub-byte masks the run's two ends need -------------------------------
CS_ENDN equ 640                     ; cs_lend/cs_rend's length: a view's x
                                    ; cannot leave [0, 639], the shadow row
                                    ; being 80 bytes (88.4.5.5)
; cs_srm - THE SHORT RUN'S MASK PAIR (88.4.6.2), 64 words indexed by
;          (x & 7) * 8 + (run - 1): the low byte is the first byte's bits and
;          the high byte the second's, 0 there when the run does not cross.
;          NASM computes it from the same MSB-first convention cs_hlm and
;          cs_hrm are written in, so there is no table to keep in step by
;          hand and no generator to run.
cs_srm:
%assign csb 0
%rep 8
  %assign csq 1
  %rep 8
    %assign csm1 0
    %assign csm2 0
    %assign csi 0
    %rep 8
      %if csi < csq
        %assign csp csb + csi
        %if csp < 8
          %assign csm1 csm1 | (0x80 >> csp)
        %else
          %assign csm2 csm2 | (0x80 >> (csp - 8))
        %endif
      %endif
      %assign csi csi + 1
    %endrep
    dw csm1 | (csm2 << 8)
    %assign csq csq + 1
  %endrep
  %assign csb csb + 1
%endrep

cs_hlm:  db 0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01   ; 1bpp, pixels i..7
cs_hrm:  db 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF   ; 1bpp, pixels 0..i
cs_clm:  db 0xFF, 0x3F, 0x0F, 0x03                           ; 2bpp, pixels i..3
cs_crm:  db 0xC0, 0xF0, 0xFC, 0xFF                           ; 2bpp, pixels 0..i
cs_dlm:  db 0xFF, 0x0F                                       ; 4bpp, pixels i..1
cs_drm:  db 0xF0, 0xFF                                       ; 4bpp, pixels 0..i
cs_pml:  db 0x0F, 0x0E, 0x0C, 0x08                           ; planes i..3
cs_pmr:  db 0x01, 0x03, 0x07, 0x0F                           ; planes 0..i

; in: BX = the row base, BP = x1, SI = x2, DL = the pattern, ES = the target
; A MASKED STORE, not an OR: a fill is opaque, and a white wall over the
; ground's dither has to take the dither out.
%macro CS_HRUN_SH 1                 ; %1 = 3 (1bpp) or 2 (2bpp)
    mov di, si
    mov cl, %1
    shr di, cl                      ; DI = the last byte
    mov cx, bp
    shr cx, cl                      ; ...no: CL was spent. See below
%endmacro
; (the macro above is not used - the two bodies are written out, because the
; shift count register is also the count the body wants)

cs_hrun_herc:
    mov di, si
    mov cl, 3
    shr di, cl                      ; DI = the last byte
    mov cx, bp
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; CX = the first byte
    and si, 7
    mov ah, [cs_hrm + si]           ; AH = the last byte's mask
    mov si, bp
    and si, 7
    mov al, [cs_hlm + si]           ; AL = the first byte's mask
    jmp short cs_hrun_ends
cs_hrun_cga:
    mov di, si
    shr di, 1
    shr di, 1
    mov cx, bp
    shr cx, 1
    shr cx, 1
    and si, 3
    mov ah, [cs_crm + si]
    mov si, bp
    and si, 3
    mov al, [cs_clm + si]
    jmp short cs_hrun_ends          ; (the first build fell through into the
                                    ; Hercules OR run below, and every CGA
                                    ; polygon row was a 1bpp OR at the wrong
                                    ; bytes: tests/skies.py's stale-pixel
                                    ; check is what found it - 88.4.4)
; cs_lrun_herc - the same run as an OR: a LINE on Hercules is the lit bit, so
; nothing under it needs taking out, and an OR is a read less per end byte
; than the masked store. What the slice lays its runs with (SPEC.md 85.3.3).
cs_lrun_herc:
    mov di, si
    mov cl, 3
    shr di, cl
    mov cx, bp
    shr cx, 1
    shr cx, 1
    shr cx, 1
    and si, 7
    mov ah, [cs_hrm + si]
    mov si, bp
    and si, 7
    mov al, [cs_hlm + si]
    cmp cx, di
    jne .multi
    and al, ah
    mov si, bx
    add si, cx
    or [es:si], al
    ret
.multi:
    mov si, bx
    add si, cx
    or [es:si], al
    mov si, bx
    add si, di
    or [es:si], ah
    sub di, cx
    dec di
    jz .done
    push di
    mov di, bx
    add di, cx
    inc di
    pop cx
    mov ax, 0xFFFF
    call cs_fillrun
.done:
    ret

cs_hrun_ends:                       ; CX = first byte, DI = last, AL/AH masks
    cmp cx, di
    jne .multi
    and al, ah                      ; one byte carries both ends
    mov si, bx
    add si, cx
    mov ah, al
    not ah
    and ah, [es:si]
    and al, dl
    or al, ah
    mov [es:si], al
    ret
.multi:
    mov si, bx
    add si, cx
    mov dh, al
    not dh
    and dh, [es:si]                 ; what the first byte keeps
    and al, dl
    or al, dh
    mov [es:si], al
    mov si, bx
    add si, di
    mov dh, ah
    not dh
    and dh, [es:si]
    and ah, dl
    or ah, dh
    mov [es:si], ah
    sub di, cx
    dec di
    jz .done
    push di
    mov di, bx
    add di, cx
    inc di
    pop cx                          ; CX = whole bytes between
    mov al, dl
    mov ah, dl
    call cs_fillrun
.done:
    ret

; cs_hrun_c160 - the 16-colour text hack's run: cs_hrun_cga with the shift and
; the two mask tables one step narrower, because a byte here is TWO pixels
; rather than four (SPEC.md 88.15.4). It shares cs_hrun_ends with the other
; two masked backends, so a fill's middle is the same `rep stosw` on all of
; them - which is the whole reason the shadow is packed nibbles and not the
; card's own char/attribute pairs.
cs_hrun_c160:
    mov di, si
    shr di, 1
    mov cx, bp
    shr cx, 1
    and si, 1
    mov ah, [cs_drm + si]
    mov si, bp
    and si, 1
    mov al, [cs_dlm + si]
    jmp cs_hrun_ends                ; NEAR, and BELOW the body it jumps to:
                                    ; the two 1bpp runs above reach it with a
                                    ; `jmp short` that has bytes to spare and
                                    ; not many, so a fourth run goes after it

; Mode X's "mask" is a set of PLANES: the two end bytes are a map-mask write
; and a store, the middle is 0x0F and a run.
cs_hrun_modex:
    cmp dl, CS_MXSTIP               ; a stipple or a latch: not the plain run
    jae .lat
.go:
    mov di, si
    shr di, 1
    shr di, 1                       ; DI = the last byte
    mov cx, bp
    shr cx, 1
    shr cx, 1                       ; CX = the first byte
    and si, 3
    mov ah, [cs_pmr + si]           ; the last byte's planes
    mov si, bp
    and si, 3
    mov al, [cs_pml + si]           ; ...and the first's
    cmp cx, di
    jne .multi
    and al, ah
    call cs_mxmask
    mov si, bx
    add si, cx
    mov [es:si], dl
    ret
.multi:
    push ax
    call cs_mxmask
    mov si, bx
    add si, cx
    mov [es:si], dl
    pop ax
    mov al, ah
    call cs_mxmask
    mov si, bx
    add si, di
    mov [es:si], dl
    sub di, cx
    dec di
    jz .done
    mov al, 0x0F
    call cs_mxmask
    push di
    mov di, bx
    add di, cx
    inc di
    pop cx
    mov al, dl
    mov ah, dl
    call cs_fillrun
.done:
    ret
.lat:
    cmp dl, CS_MXLAT                ; Mode X's textured ground: the same run
    jb .stip                        ; through the latches (cs_mxlon)
    mov al, dl
    call cs_mxlon
    call .go
    jmp cs_mxloff
.stip:
    ; --- THE MAP'S LIFT (88.16.1): CSI_LIFT through the planes DL's low
    ;     nibble names and no others, so a row lays one pixel in two of its
    ;     run and leaves the ground showing in the rest. .go's shape with
    ;     every mask ANDed by DH, and its own copy because .go is every
    ;     face's run and a stipple is drawn only on the map
    mov dh, dl
    and dh, 0x0F
    jz .sdone                       ; a row with no planes is no write
    mov dl, CSI_LIFT
    mov di, si
    shr di, 1
    shr di, 1                       ; DI = the last byte
    mov cx, bp
    shr cx, 1
    shr cx, 1                       ; CX = the first byte
    and si, 3
    mov ah, [cs_pmr + si]
    and ah, dh
    mov si, bp
    and si, 3
    mov al, [cs_pml + si]
    and al, dh
    cmp cx, di
    jne .smulti
    and al, ah
    call cs_mxmask
    mov si, bx
    add si, cx
    mov [es:si], dl
.sdone:
    ret
.smulti:
    call cs_mxmask
    mov si, bx
    add si, cx
    mov [es:si], dl
    mov al, ah
    call cs_mxmask
    mov si, bx
    add si, di
    mov [es:si], dl
    sub di, cx
    dec di
    jz .sdone
    mov al, dh
    call cs_mxmask
    push di
    mov di, bx
    add di, cx
    inc di
    pop cx
    mov al, dl
    mov ah, dl
    jmp cs_fillrun

; cs_mxmask - AL = the plane mask to arm. Preserves everything but flags.
cs_mxmask:
    push ax
    push dx
    mov ah, al
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_prect - an opaque rectangle in the PANEL, marked into the span
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (box coords, inclusive), [cs_ink]
; clobbers everything
; -----------------------------------------------------------------------------
cs_prect:
    mov [cs_rx1], ax
    mov [cs_rx2], cx
    mov [cs_ry2], dx
.row:
    push bx
    mov ax, [cs_rx1]
    mov cx, [cs_rx2]
    call cs_markspan
    call cs_hrun
    pop bx
    inc bx
    cmp bx, [cs_ry2]
    jle .row
    ret

; -----------------------------------------------------------------------------
; cs_rect - an opaque rectangle in the VIEW, clipped, marked through the
;           object's accumulator, and laid by the backend's row loop
; in:  AX = x0, BX = y0, CX = x1, DX = y1 (box coords, inclusive), [cs_ink]
; clobbers everything
; -----------------------------------------------------------------------------
cs_rect:
    or dx, dx
    js .out                         ; wholly above the view
    cmp bx, [cs_wh]
    jge .out                        ; ...or below it
    cmp cx, [cs_wx0]
    jl .out                         ; ...or left of it
    cmp ax, [cs_wx1]
    jg .out                         ; ...or right
    or bx, bx
    jns .t
    xor bx, bx
.t:
    cmp dx, [cs_wh]
    jl .b
    mov dx, [cs_wh]
    dec dx
.b:
    cmp ax, [cs_wx0]
    jge .l
    mov ax, [cs_wx0]
.l:
    cmp cx, [cs_wx1]
    jle .r
    mov cx, [cs_wx1]
.r:
    mov [cs_py0], bx
    mov [cs_py1], dx
    push ax
    push cx
    mov dx, cx
    mov cl, [cs_kshift]
    shr ax, cl
    shr dx, cl
    mov [cs_mklo], al
    mov [cs_mkhi], dl
    mov si, bx
    mov di, [cs_py1]
    call cs_markacc
    pop cx
    pop ax
    mov si, [cs_py0]
    shl si, 1
.row:
    mov [cs_xl + si], ax
    mov [cs_xr + si], cx
    inc si
    inc si
    inc bx
    cmp bx, [cs_py1]
    jle .row
    mov bp, 1                       ; THE GENERAL ROW LOOP (88.4.5.1). Its
                                    ; rows are clamped already - ax and cx
                                    ; were, once, above - but a RECTANGLE is
                                    ; how the runway under the wheels is
                                    ; drawn, forty whole-view rows a frame
                                    ; (88.5.5), and that test lives in the
                                    ; block BP skips
    jmp [cs_rowsprocc]
.out:
    ret

; cs_markspan - AX = x1, CX = x2, BX = the row: widen the row's span pair
; to cover them. Preserves AX, BX, CX; clobbers DX, SI.
cs_markspan:
    push ax
    push cx
    mov dx, cx
    mov cl, [cs_kshift]
    shr ax, cl                      ; AX = the first byte
    shr dx, cl                      ; DX = the last
    mov si, [cs_spcur]
    cmp bx, [si - 4]                ; the set's row range takes the row
    jge .y0
    mov [si - 4], bx
.y0:
    cmp bx, [si - 2]
    jle .y1
    mov [si - 2], bx
.y1:
    mov si, bx
    shl si, 1
    add si, [cs_spcur]
    cmp [si], al
    jbe .lo
    mov [si], al
.lo:
    cmp [si+1], dl
    jae .hi
    mov [si+1], dl
.hi:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_pclip / cs_vclip - the segment walk clipped to the whole BOX instead of
;                       the view, for the panel's own drawing (88.9.2), and
;                       back. Segments in the panel mark their own rows
;                       (cs_ownmk): they belong to no object that would.
;
; It BORROWS cs_wh, so for as long as the panel is drawing that word reads the
; BOX's height and not the view's - which is a trap for anything reading the
; geometry off a running machine. cs_viewh is the same number and is never
; borrowed; cs_r_size publishes it and cs_vclip restores from it (88.9.2.3).
; -----------------------------------------------------------------------------
cs_pclip:
    mov ax, [cs_wx0]
    mov [cs_svclip], ax
    mov ax, [cs_wx1]
    mov [cs_svclip + 2], ax
    mov word [cs_wx0], 0
    mov ax, [cs_vw]
    dec ax
    mov [cs_wx1], ax
    mov ax, [cs_vh]
    mov [cs_wh], ax
    mov byte [cs_ownmk], -1
    mov byte [cs_pwhole], 0         ; the panel is no object's
    mov byte [cs_pinview], 0
    ret
cs_vclip:
    mov ax, [cs_svclip]
    mov [cs_wx0], ax
    mov ax, [cs_svclip + 2]
    mov [cs_wx1], ax
    mov ax, [cs_viewh]              ; the view's height from the word cs_r_size
    mov [cs_wh], ax                 ; published, not from a save of the last
                                    ; thing that happened to be in cs_wh
    mov byte [cs_ownmk], 0
    ret

; =============================================================================
; The walk (SPEC.md 88.4.3): Tank's, without the span marks
; =============================================================================

; -----------------------------------------------------------------------------
; cs_seg - one screen-space segment: clip it to the VIEW and walk it
; in:  AX = x1, BX = y1, CX = x2, DX = y2, box coords, any magnitude to
;      +-4000; [cs_ink] set.  CLOBBERS EVERY REGISTER (SPEC.md 85.3.4)
; -----------------------------------------------------------------------------
cs_seg:
    cmp byte [cs_pinview], 0        ; the object's box is inside the view:
    je .test                        ; nothing to test, and its box is taken
    jmp .mk1
.test:
    mov si, [cs_wx0]
    mov di, [cs_wh]
    cmp ax, si
    jl .slow
    cmp ax, [cs_wx1]
    jg .slow
    or bx, bx
    js .slow
    cmp bx, di
    jge .slow
    cmp cx, si
    jl .slow
    cmp cx, [cs_wx1]
    jg .slow
    or dx, dx
    js .slow
    cmp dx, di
    jl .accept
.slow:
    mov [cs_cx1], ax
    mov [cs_cy1], bx
    mov [cs_cx2], cx
    mov [cs_cy2], dx
    mov byte [cs_ctry], 8           ; the cap is load-bearing (SPEC.md 85.3)
.again:
    dec byte [cs_ctry]
    jz .out
    mov ax, [cs_cx1]
    mov bx, [cs_cy1]
    call cs_outcode
    mov cl, al
    mov ax, [cs_cx2]
    mov bx, [cs_cy2]
    call cs_outcode
    mov ch, al
    mov al, cl
    or al, ch
    jz .clipped
    mov al, cl
    and al, ch
    jnz .out
    xor si, si
    mov al, cl
    or al, al
    jnz .pick
    mov si, 4
    mov al, ch
.pick:
    test al, 8
    jnz .bot
    test al, 4
    jnz .top
    test al, 2
    jnz .right
    mov bp, [cs_wx0]
    call cs_clip_x
    jmp short .again
.right:
    mov bp, [cs_wx1]
    call cs_clip_x
    jmp short .again
.top:
    xor bp, bp
    call cs_clip_y
    jmp short .again
.bot:
    mov bp, [cs_wh]
    dec bp
    call cs_clip_y
    jmp short .again
.clipped:
    mov ax, [cs_cx1]
    mov bx, [cs_cy1]
    mov cx, [cs_cx2]
    mov dx, [cs_cy2]
.accept:
    cmp byte [cs_pwhole], 0         ; a whole object's box is off its
    jne .mk1                        ; vertices already (88.3.2)
    push ax                         ; the rows it lies on, and the bytes its
    push cx                         ; ends span, go to the glass
    cmp ax, cx
    jle .xo
    xchg ax, cx
.xo:
    push cx
    mov cl, [cs_kshift]
    shr ax, cl
    mov [cs_mklo], al
    pop ax
    shr ax, cl
    mov [cs_mkhi], al
    pop cx
    pop ax
    mov si, bx
    mov di, dx
    cmp si, di
    jle .mk
    xchg si, di
.mk:
    cmp byte [cs_ownmk], 0          ; the scene's segments go into their
    jne .own                        ; object's box, marked once (88.3.2);
    ; --- ...UNLESS IT IS A THIN DIAGONAL (SPEC.md 88.3.2.2) ---------------
    ; A box is the wrong shape for a diagonal and the waste is measured: at
    ; 12 degrees of bank THE SEINE lights 495 pixels and its box is 52 rows
    ; of 46 bytes. The gate is the two numbers already to hand - a segment
    ; that is shallow in ROWS or narrow in BYTES has a tight box already,
    ; and only one long in BOTH is worth a walk of its own. The tower's
    ; thirty-two near-vertical edges fail the byte test, which is what
    ; 88.3.2 refused per-segment marking over.
    ; BP IS THE ONLY REGISTER FREE HERE and that is the whole shape of the
    ; test: AX and CX are the segment's x's, BX and DX its y's, SI and DI the
    ; ordered rows, and cs_markstep needs every one of them. The first draft
    ; measured the row span into AX, which is x1, and the marks landed
    ; somewhere else entirely - caught by tests/skiesstale.py in one run,
    ; 433 stale rows, because an under-mark IS a stale byte.
    mov bp, di
    sub bp, si
    cmp bp, CS_MKD_ROWS
    jb .acc
    mov bp, cx                      ; the width in PIXELS: a byte compare
    sub bp, ax                      ; would want a register there is not one
    jge .g0                         ; of
    neg bp
.g0:
    cmp bp, CS_MKD_PX
    jb .acc
    cmp byte [cs_mknostep], 0       ; the A/B, and the box mark is what
    jne .acc                        ; shipped before 88.3.2.2
    call cs_markstep
    jmp short .mk1
.own:
    call cs_markrows                ; the panel's mark their own rows
    jmp short .mk1
.acc:
    call cs_markacc
.mk1:
    mov si, cx                      ; SI = |dx|
    sub si, ax
    jge .dxp
    neg si
.dxp:
    mov di, dx                      ; DI = |dy|
    sub di, bx
    jge .dyp
    neg di
.dyp:
    cmp si, di
    jb .steep
    ; ---- shallow: x increasing --------------------------------------------
    cmp ax, cx
    jbe .nsw
    xchg ax, cx
    xchg bx, dx
.nsw:
    mov cx, 80
    cmp dx, bx
    jae .yd
    neg cx
.yd:
    mov [cs_ystep], cx
    mov bp, di                      ; BP = e1 = 2|dy|
    shl bp, 1
    mov cx, si                      ; CX = |dx| + 1 pixels
    inc cx
    mov dx, si
    shl dx, 1
    mov [cs_e2], dx
    neg si
    mov dx, si                      ; err = -|dx|
    mov si, bx
    shl si, 1
    mov di, [cs_rowoff + si]
    add di, [cs_tbase]
    mov bx, dx
%ifdef CSPROBE
    call cs_probe_sh
%endif
    jmp [cs_lsh]
    ; ---- steep: y increasing ----------------------------------------------
.steep:
    cmp bx, dx
    jbe .nsw2
    xchg ax, cx
    xchg bx, dx
.nsw2:
    mov byte [cs_xdir], 0
    cmp cx, ax
    jae .xd
    mov byte [cs_xdir], 1
.xd:
    mov bp, si                      ; BP = e1 = 2|dx|
    shl bp, 1
    mov cx, di                      ; CX = |dy| + 1 rows
    inc cx
    mov dx, di
    shl dx, 1
    mov [cs_e2], dx
    neg di
    mov dx, di
    mov si, bx
    shl si, 1
    mov di, [cs_rowoff + si]
    add di, [cs_tbase]
    mov bx, dx
    or bp, bp
    jz .vert
%ifdef CSPROBE
    inc word [cs_dbg_wst]
    add [cs_dbg_wstpx], cx
%endif
    jmp [cs_lst]
.vert:
%ifdef CSPROBE
    inc word [cs_dbg_wvt]
    add [cs_dbg_wvtpx], cx
%endif
    jmp [cs_lvt]
.out:
    ret

; cs_outcode - in AX = x, BX = y; out AL = the code. Preserves BX, CX, DX.
cs_outcode:
    push dx
    xor dl, dl
    cmp ax, [cs_wx0]
    jge .r
    or dl, 1
    jmp short .v
.r:
    cmp ax, [cs_wx1]
    jle .v
    or dl, 2
.v:
    or bx, bx
    jns .b
    or dl, 4
    jmp short .d
.b:
    cmp bx, [cs_wh]
    jl .d
    or dl, 8
.d:
    mov al, dl
    pop dx
    ret

; cs_clip_x - move endpoint SI (0 or 4) onto the vertical line x = BP
cs_clip_x:
    push si
    mov ax, [cs_cy2]
    sub ax, [cs_cy1]
    mov bx, bp
    sub bx, [cs_cx1]
    imul bx
    mov bx, [cs_cx2]
    sub bx, [cs_cx1]
    idiv bx
    add ax, [cs_cy1]
    pop si
    mov [cs_cx1 + si], bp
    mov [cs_cy1 + si], ax
    ret

; cs_clip_y - move endpoint SI onto the horizontal line y = BP
cs_clip_y:
    push si
    mov ax, [cs_cx2]
    sub ax, [cs_cx1]
    mov bx, bp
    sub bx, [cs_cy1]
    imul bx
    mov bx, [cs_cy2]
    sub bx, [cs_cy1]
    idiv bx
    add ax, [cs_cx1]
    pop si
    mov [cs_cx1 + si], ax
    mov [cs_cy1 + si], bp
    ret

; --- the walk bodies ----------------------------------------------------------
; Entered by `jmp` from cs_seg with AX = lx, DI = the row's base, BX = err,
; CX = the pixel count, BP = e1, [cs_e2]/[cs_ystep]/[cs_xdir] set. Every one
; ends in `ret`, to cs_seg's caller. DX and SI are free here - Tank's walk
; spent them on the span pair - and the CGA plot uses DL for its mask.

; the entry every walk shares: DI = the pixel's byte, AL = the ink at its
; position (AH = the wrap value; DL/DH the same for the CGA mask)
%macro CS_LENTER 1
    mov dx, ax
    shr dx, 1
  %if %1 != 4
    shr dx, 1
  %endif
  %if %1 == 1
    shr dx, 1
  %endif
    add di, dx
    push cx
  %if %1 == 4
    and al, 1                       ; the 16-colour hack: TWO pixels a byte,
    mov dl, 0xF0                    ; so the mask is a nibble and the ink at
    or al, al                       ; a pixel is just `ink8 & mask` - both
    jz %%p0                         ; nibbles of ink8 holding the colour
    mov dl, 0x0F
%%p0:
    mov al, [cs_ink8]
    and al, dl                      ; AL = the ink at p
    mov ah, [cs_ink8]
    and ah, 0xF0                    ; AH = the ink at pixel 0: the wrap value
    mov dh, 0xF0
  %elif %1 == 1
    mov cl, al
    and cl, 7
    mov al, 0x80
    shr al, cl
    mov ah, 0x80
  %elif %1 == 2
    and al, 3
    mov cl, al
    shl cl, 1                       ; CL = 2p: pixel 0 is bits 7..6
    mov dl, 0xC0
    shr dl, cl                      ; the mask at p
    mov ah, [cs_ink8]
    push cx
    mov cl, 6
    shl ah, cl                      ; AH = the ink at pixel 0: the wrap value
    pop cx
    mov al, ah
    shr al, cl                      ; AL = the ink at p
    mov dh, 0xC0
  %else
    and al, 3
    mov cl, al
    mov ah, 1
    shl ah, cl
    mov al, [cs_ink8]
    mov dx, 0x3C4
  %endif
    pop cx
    mov es, [cs_tseg]
%endmacro

%macro CS_PLOT 1
  %if %1 == 1
    or [es:di], al
  %elif %1 == 2 || %1 == 4
    not dl
    and [es:di], dl
    not dl
    or [es:di], al
  %else
    mov si, ax
    mov al, 2
    out dx, ax
    mov ax, si
    mov [es:di], al
  %endif
%endmacro

; x + 1 along the row; at the byte boundary the pixel wraps to the next byte
%macro CS_XFWD 1
  %if %1 == 1
    shr al, 1
    jnz %%d
    mov al, 0x80
    inc di
  %elif %1 == 2
    shr al, 1
    shr al, 1
    shr dl, 1
    shr dl, 1
    jnz %%d
    mov al, ah
    mov dl, dh
    inc di
  %elif %1 == 4
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    shr dl, 1                       ; F0 -> 0F stays in the byte; 0F -> 00 is
    shr dl, 1                       ; the next one
    shr dl, 1
    shr dl, 1
    jnz %%d
    mov al, ah
    mov dl, dh
    inc di
  %else
    shl ah, 1
    cmp ah, 16
    jne %%d
    mov ah, 1
    inc di
  %endif
%%d:
%endmacro

%macro CS_XBACK 1
  %if %1 == 1
    shl al, 1
    jnz %%d
    mov al, 0x01
    dec di
  %elif %1 == 2
    shl al, 1
    shl al, 1
    shl dl, 1
    shl dl, 1
    jnz %%d
    mov al, [cs_ink8]
    mov dl, 0x03
    dec di
  %elif %1 == 4
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    jnz %%d
    mov al, [cs_ink8]
    and al, 0x0F                    ; the previous byte's RIGHT pixel, and its
    mov dl, 0x0F                    ; nibble only: ink8 carries both
    dec di
  %else
    shr ah, 1
    jnz %%d
    mov ah, 8
    dec di
  %endif
%%d:
%endmacro

; ---- run-length slice: a shallow line as one RUN a row (SPEC.md 85.3.6) ----
; Taken when a line lays at least eight pixels a row. Bresenham's 1985 slice:
; one `div` gives the whole step q and its remainder, each row's run is q or
; q + 1 by an error term that moves once a ROW, and the run is laid the way
; cs_hrun lays one - the backend's own run routine, called rather than jumped
; to. The first and last runs are half runs, which keeps the ends on the
; endpoints. A road across the whole view is 64 runs instead of 512 pixels.
;
; in: AX = lx, DI = the row base, CX = |dx|+1, BP = 2|dy|, [cs_ystep]
%macro CS_SLICE 0
    mov es, [cs_tseg]
    mov [cs_slx], ax                ; X = the run's first pixel
    shr bp, 1                       ; BP = |dy|
    jz %%flat
    mov ax, cx
    dec ax                          ; AX = |dx|
    xor dx, dx
    div bp                          ; AX = q, DX = the remainder
    mov [cs_slq], ax
    mov [cs_slcnt], bp              ; runs after the first: |dy|
    mov bx, bp
    shl bx, 1
    mov [cs_e2], bx                 ; the adjust-down: 2|dy|
    mov cx, dx                      ; err = rem - 2|dy|, plus |dy| for an odd q
    sub cx, bx
    test al, 1
    jz %%even
    add cx, bp
%%even:
    mov [cs_slerr], cx
    shl dx, 1
    mov bp, dx                      ; BP = the adjust-up: 2 x rem
    shr ax, 1
    inc ax                          ; the half runs: q/2 + 1 ...
    mov [cs_slfin], ax
    or dx, dx                       ; ...and the first loses one when there is
    jnz %%go                        ; nothing fractional and q is even, so the
    test byte [cs_slq], 1           ; pixel count still comes to |dx|+1
    jnz %%go
    dec ax
%%go:
    mov dx, [cs_slx]
    jmp short %%row
%%flat:
    mov ax, cx                      ; dy = 0: one run of every pixel
    mov word [cs_slcnt], 0
    mov dx, [cs_slx]                ; ...STARTING WHERE X DOES (SPEC.md
                                    ; 88.4.6.1). The sloped path loads this at
                                    ; %%go and this one did not, so DX arrived
                                    ; holding 6|dy| from the caller's own
                                    ; slice test - which for a flat line is
                                    ; ZERO, and every exactly-horizontal
                                    ; segment was drawn at the view's LEFT
                                    ; EDGE. Four bytes, on the one path that
                                    ; can reach here: no other line pays
%%row:                              ; AX = this row's run, DX = its first x
    push bp
    push di
    mov bx, di                      ; BX = the row base
    mov si, dx
    add si, ax
    dec si                          ; SI = the run's last x
    mov bp, dx                      ; BP = its first
    add dx, ax                      ; DX = the next row's first x
    push dx
    mov dl, [cs_pat]
    call [cs_lrunproc]              ; a jump target, called: it returns here
    pop dx
    pop di
    pop bp
    add di, [cs_ystep]
    dec word [cs_slcnt]             ; runs still to come after this one
    js %%done
    jz %%fin
    mov ax, [cs_slq]
    add [cs_slerr], bp
    jle %%torow
    inc ax
    mov bx, [cs_e2]
    sub [cs_slerr], bx
%%torow:
    jmp %%row
%%fin:
    mov ax, [cs_slfin]
    jmp %%row
%%done:
    ret
%endmacro

; ---- the Hercules walks, with the per-row constants in registers ----------
; The CGA walk spends DX on its pixel mask and Mode X's on the port, so the
; general body reads e2 and the y step from memory. Hercules - the machine
; this is built against - has DX and SI free: SI = e2, DX = the y step, and
; the steep body is two bodies, one per x direction, so the x step is a
; shift and a test with no direction compare and no second jump.
; cs_slice_herc - the slice on Hercules, the run inline (88.4.6's argument
;                 again): per row two masked ORs and a `rep stosb` between,
;                 the DDA in registers - DX = the error, BP = 2 x the
;                 remainder - and nothing pushed but the run's own x
; in: AX = x, CX = pixels, BP = 2|dy|, DI = the row base, [cs_ystep]
cs_slice_herc:
    mov es, [cs_tseg]
    mov si, ax                      ; SI = x
    mov bx, di                      ; BX = the row base
    shr bp, 1                       ; BP = |dy|
    jz .flat
    mov ax, cx
    dec ax                          ; AX = |dx|
    xor dx, dx
    div bp                          ; AX = q, DX = the remainder
    mov [cs_slq], ax
    mov [cs_slcnt], bp              ; runs after the first: |dy|
    mov di, bp
    shl di, 1
    mov [cs_e2], di                 ; the adjust-down: 2|dy|
    mov cx, dx                      ; err = rem - 2|dy|, plus |dy| for an odd q
    sub cx, di
    test al, 1
    jz .even
    add cx, bp
.even:
    mov bp, dx
    shl bp, 1                       ; BP = the adjust-up: 2 x rem
    mov dx, cx                      ; DX = err
    shr ax, 1
    inc ax                          ; the half runs: q/2 + 1 ...
    mov [cs_slfin], ax
    or bp, bp                       ; ...and the first loses one when there is
    jnz .go                         ; nothing fractional and q is even, so the
    test byte [cs_slq], 1           ; pixel count still comes to |dx|+1
    jnz .go
    dec ax
.go:
    ; --- A SHORT RUN TAKES A BODY OF ITS OWN (SPEC.md 88.4.6.2) ------------
    ; Every row's run is q or q+1, and the half runs at the ends are q/2+1,
    ; so q <= 7 bounds all of them at 8 - and 8 pixels from any bit span at
    ; most TWO BYTES. That is one mask pair off a table and two ORs, against
    ; .row's two table reads, two end computations and three pushes. The
    ; test is one compare on a word already stored, paid once a SEGMENT.
    cmp word [cs_slq], 7
    ja .long
    cmp byte [cs_slnoshort], 0      ; ...and the A/B is a RUNTIME byte, not a
    je .srow                        ; knob: one binary, both arms, so a
.long:                              ; picture comparison cannot be a build
    jmp short .row                  ; difference. It is one test a SEGMENT
.flat:
    mov ax, cx                      ; dy = 0: one run of every pixel
    mov word [cs_slcnt], 0
.row:                               ; AX = this row's run, SI = its first x
    push si
    add si, ax
    push si                         ; the next row's first x
    dec si                          ; SI = the run's last x
    mov di, si
    and di, 7
    mov ch, [cs_hrm + di]           ; CH = the last byte's mask
    shr si, 1
    shr si, 1
    shr si, 1                       ; SI = the last byte
    pop di
    pop ax                          ; AX = the first x, DI = the next row's
    push di
    mov di, ax
    and di, 7
    mov cl, [cs_hlm + di]           ; CL = the first byte's mask
    shr ax, 1
    shr ax, 1
    shr ax, 1                       ; AX = the first byte
    mov di, ax
    add di, bx                      ; DI = its address
    sub si, ax                      ; SI = bytes - 1
    jnz .multi
    and cl, ch
    or [es:di], cl
    jmp short .step
.multi:
    or [es:di], cl
    mov ax, si
    add si, di
    or [es:si], ch                  ; the last byte
    mov cx, ax
    dec cx
    jz .step
    inc di
    mov al, 0xFF
    rep stosb                       ; the whole bytes between: one to nine
.step:
    pop si                          ; SI = the next row's first x
    add bx, [cs_ystep]
    dec word [cs_slcnt]             ; runs still to come after this one
    js .done
    jz .fin
    mov ax, [cs_slq]
    add dx, bp
    jle .row
    inc ax
    sub dx, [cs_e2]
    jmp short .row
.fin:
    mov ax, [cs_slfin]
    jmp short .row
.done:
    ret
    ; --- THE SHORT-RUN BODY (88.4.6.2) -------------------------------------
    ; in: AX = this row's run (1..8), SI = its first x, BX = the row base,
    ;     DX = the error, BP = the adjust-up, ES = the target
    ; It carries the next row's first x in SI by ADDING the run, which is
    ; what .row's push / add / push computes the long way round.
.srow:
    mov di, si
    and di, 7                       ; the bit within its byte...
    shl di, 1
    shl di, 1
    shl di, 1
    add di, ax
    dec di
    shl di, 1                       ; ...times 8, plus run-1, a word index
    mov cx, [cs_srm + di]           ; CL = the first byte's bits, CH = the
    mov di, si                      ; second's
    shr di, 1
    shr di, 1
    shr di, 1
    add di, bx
    or [es:di], cl
    ; ...AND THE SECOND OR IS UNCONDITIONAL, which is what makes the body
    ; branch-free: CH is 0 for a run that does not cross, and `or` with 0
    ; writes back the byte that was already there. DI+1 cannot leave the
    ; claim - the widest 1bpp shadow here is 200 rows of 80 bytes and
    ; CS_SHKB is 16,384, so 384 bytes stand past the last of them.
    or [es:di + 1], ch
    add si, ax                      ; the next row's first x
    add bx, [cs_ystep]
    dec word [cs_slcnt]
    js .done
    jz .sfin
    mov ax, [cs_slq]
    add dx, bp
    jle .srow
    inc ax
    sub dx, [cs_e2]
    jmp short .srow
.sfin:
    mov ax, [cs_slfin]
    jmp short .srow

%macro CS_HWALKS 1
%1_sh:
    mov dx, bp                      ; six or more pixels a row takes the slice
    shl dx, 1
    add dx, bp
    cmp cx, dx
    jbe %%walk
    jmp cs_slice_herc
%%walk:
    CS_LENTER 1
    mov si, [cs_e2]
    mov dx, [cs_ystep]
%%spx:
%ifdef CSPROBE
    ; A/B 5: `or` is idempotent, so a second plot draws the identical picture
    ; and the frame's difference over the plot count is ONE plot, exactly
    cmp byte [cs_dblplot], 0
    je %%npsh
    or [es:di], al
%%npsh:
%endif
    or [es:di], al
    add bx, bp
    jg %%ys
%%cont:
    shr al, 1
    jz %%nb
    loop %%spx
    ret
%%nb:
    mov al, 0x80
    inc di
    loop %%spx
    ret
%%ys:
    add di, dx
    sub bx, si
    jmp short %%cont
%1_st:
    CS_LENTER 1
    mov si, [cs_e2]
    cmp byte [cs_xdir], 0
    jne %%back
%%fwd:
%ifdef CSPROBE
    ; A/B 5: `or` is idempotent, so a second plot draws the identical picture
    ; and the frame's difference over the plot count is ONE plot, exactly
    cmp byte [cs_dblplot], 0
    je %%npstf
    or [es:di], al
%%npstf:
%endif
    or [es:di], al
    add di, 80
    add bx, bp
    jg %%fx
    loop %%fwd
    ret
%%fx:
    sub bx, si
    shr al, 1
    jnz %%f1
    mov al, 0x80
    inc di
%%f1:
    loop %%fwd
    ret
%%back:
%ifdef CSPROBE
    ; A/B 5: `or` is idempotent, so a second plot draws the identical picture
    ; and the frame's difference over the plot count is ONE plot, exactly
    cmp byte [cs_dblplot], 0
    je %%npstb
    or [es:di], al
%%npstb:
%endif
    or [es:di], al
    add di, 80
    add bx, bp
    jg %%bx
    loop %%back
    ret
%%bx:
    sub bx, si
    shl al, 1
    jnz %%b1
    mov al, 0x01
    dec di
%%b1:
    loop %%back
    ret
%1_vt:
    CS_LENTER 1
    mov bp, 80
%%v:
%ifdef CSPROBE
    ; A/B 5: `or` is idempotent, so a second plot draws the identical picture
    ; and the frame's difference over the plot count is ONE plot, exactly
    cmp byte [cs_dblplot], 0
    je %%npvt
    or [es:di], al
%%npvt:
%endif
    or [es:di], al
    add di, bp
    loop %%v
    ret
%endmacro

%macro CS_WALKS 2
%2_sh:
  %if %1 != 3
    mov dx, bp                      ; six or more pixels a row - |dx| >= 6|dy|,
    shl dx, 1                       ; which is CX-1 >= 3BP - takes the slice
    add dx, bp
    cmp cx, dx
    jbe %%walk
    CS_SLICE
%%walk:
  %endif
    CS_LENTER %1
%%spx:
    CS_PLOT %1
    add bx, bp
    jg %%ys
%%cont:
    CS_XFWD %1
    loop %%spx
    ret
%%ys:
    add di, [cs_ystep]
    sub bx, [cs_e2]
    jmp short %%cont
%2_st:
    CS_LENTER %1
%%st:
    CS_PLOT %1
    add di, 80
    add bx, bp
    jg %%xs
    loop %%st
    ret
%%xs:
    cmp byte [cs_xdir], 0
    jne %%back
    CS_XFWD %1
    jmp short %%xd
%%back:
    CS_XBACK %1
%%xd:
    sub bx, [cs_e2]
    loop %%st
    ret
%2_vt:
    CS_LENTER %1
    mov bp, 80
%%v:
    CS_PLOT %1
    add di, bp
    loop %%v
    ret
%endmacro

CS_HWALKS cs_line_herc
CS_WALKS 2, cs_line_cga
CS_WALKS 3, cs_line_modex
CS_WALKS 4, cs_line_c160

; (the four walk trios are in cs_vptab's rows now, SPEC.md 88.3.7)

; =============================================================================
; Text in a foreign mode (SPEC.md 85.7, taken as is): the kernel's 8x8 face
; through OSAPI_FONT_GLYPHS, every run on an 8-pixel boundary, every cell
; opaque, in the panel's inks. Marks the span (the panel is the only place
; text goes).
; =============================================================================
cs_glyphs:
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL/AH = its range
    mov [cs_gtab], si
    mov [cs_gseg], dx
    mov [cs_gfirst], al
    mov [cs_glast], ah
    ret

; cs_text - SI = a NUL string, CX = x, DX = y (box coords); [cs_ink] = the
; ink. Preserves every register.
cs_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    and cl, 0xF8
.ch:
    mov al, [si]
    or al, al
    jz .done
    inc si
    push si
    push cx
    push dx
    call cs_glyph
    pop dx
    pop cx
    pop si
    add cx, 8
    cmp cx, [cs_vw]
    jl .ch
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_glyph - AL = the character, CX = x, DX = y.  clobbers AX, BX, CX, SI, DI, BP
cs_glyph:
    cmp al, [cs_gfirst]
    jb .out
    cmp al, [cs_glast]
    ja .out
    or dx, dx
    js .out
    mov bx, dx
    add bx, 8
    cmp bx, [cs_vh]
    jg .out
    or cx, cx
    js .out

    push cx                         ; --- stage the glyph into our segment ---
    push dx
    sub al, [cs_gfirst]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [cs_gtab]
    mov ax, [cs_gseg]
    push ds
    push ds
    pop es
    mov si, bx
    mov ds, ax
    mov di, cs_gbuf
    mov cx, 8
    cld
    rep movsb
    pop ds
    pop dx
    pop cx

    push cx                         ; --- the span rows and the address -----
    push dx
    mov bx, cx
    mov cl, [cs_kshift]
    shr bx, cl
    mov [cs_gcol], bx               ; the first byte of every row
    mov al, [cs_gcellb]             ; ...and how many bytes it spans: 1 on
    add bl, al                      ; Hercules, 2 on CGA and Mode X, 4 on the
                                    ; 16-colour hack, where a cell's eight
                                    ; pixels are four two-pixel bytes
    dec bl
    mov [cs_gcol2], bx              ; ...and the last
    pop dx
    pop cx

    push cx
    mov bx, [cs_spcur]              ; the set's row range takes the eight
    cmp dx, [bx - 4]                ; rows (88.3.3 - the first build marked
    jge .y0                         ; the spans and not the range, so an
    mov [bx - 4], dx                ; instrument reached the glass only
.y0:                                ; when the throttle bar's rectangle
    mov ax, dx                      ; happened to widen it)
    add ax, 7
    cmp ax, [bx - 2]
    jle .y1
    mov [bx - 2], ax
.y1:
    mov bx, dx                      ; mark eight rows of span
    shl bx, 1
    add bx, [cs_spcur]
    mov cx, 8
    mov ax, [cs_gcol]
.sp:
    cmp [bx], al
    jbe .s1
    mov [bx], al
.s1:
    push ax
    mov ax, [cs_gcol2]
    cmp [bx+1], al
    jae .s2
    mov [bx+1], al
.s2:
    pop ax
    add bx, 2
    loop .sp
    pop cx

    mov bx, dx                      ; DI = tbase + the row's base + the byte
    shl bx, 1
    mov di, [cs_rowoff + bx]
    add di, [cs_tbase]
    add di, [cs_gcol]
    mov es, [cs_tseg]
    call [cs_glyphproc]
.out:
    ret

; The three glyph stores. A set bit is the ink and a clear one the PANEL's
; ground ([cs_pbg8]); every cell is opaque.
cs_glyph_herc:                      ; the ground is 0, so the row IS the cell
    mov si, cs_gbuf
    mov cx, 8
    cld
.r:
    lodsb
    mov [es:di], al
    add di, 80
    loop .r
    ret

; cs_glyph_c160 - the 16-colour hack's cell: each PAIR of glyph bits becomes
; one byte, the ink where the bit is set and the panel's ground where it is
; not - which on this backend is `ink8` and `pbg8` masked against each other,
; both already being the colour in both nibbles (88.15.3). Four bytes a row.
cs_glyph_c160:
    mov si, cs_gbuf
    mov bp, 8
    cld
.r:
    lodsb
    mov dl, al                      ; DL = the row's eight bits
    xor bx, bx
.p:
    mov al, dl                      ; the top two, as an index 0..3
    mov cl, 6
    shr al, cl
    xor ah, ah
    push bx
    mov bx, ax
    mov ah, [cs_dib + bx]           ; AH = which halves the glyph lights
    pop bx
    mov al, [cs_ink8]
    and al, ah
    not ah
    and ah, [cs_pbg8]
    or al, ah
    mov [es:di+bx], al
    shl dl, 1
    shl dl, 1
    inc bx
    cmp bx, 4
    jb .p
    add di, 80
    dec bp
    jnz .r
    ret
cs_dib:  db 0x00, 0x0F, 0xF0, 0xFF  ; two glyph bits -> the halves they light

cs_glyph_cga:
    mov si, cs_gbuf
    mov bp, 8
    cld
.r:
    lodsb
    mov dl, al
    mov cl, 4
    shr al, cl
    xor ah, ah
    mov bx, ax
    mov ah, [cs_nib3 + bx]          ; the pixels the glyph lights, both bits
    add bx, [cs_nibp]
    mov al, [bx]                    ; ...in the ink
    not ah
    and ah, [cs_pbg8]               ; ...and the ground under the others
    or al, ah
    mov [es:di], al
    mov al, dl
    and al, 0x0F
    xor ah, ah
    mov bx, ax
    mov ah, [cs_nib3 + bx]
    add bx, [cs_nibp]
    mov al, [bx]
    not ah
    and ah, [cs_pbg8]
    or al, ah
    mov [es:di+1], al
    add di, 80
    dec bp
    jnz .r
    ret

; Mode X letters PLANE BY PLANE: a glyph's eight rows touch every plane, so
; the other way is 32 map-mask writes instead of 4.
cs_glyph_modex:
    xor bl, bl
.pl:
    mov al, 1
    mov cl, bl
    shl al, cl
    call cs_mxmask
    push di
    mov si, cs_gbuf
    mov bp, 8
.r:
    mov al, [si]
    mov cl, 7
    sub cl, bl
    mov ah, al
    shr ah, cl
    and ah, 1
    neg ah                          ; 1 -> 0FFh, 0 -> 0
    mov ch, ah
    and ah, [cs_ink8]
    not ch
    and ch, [cs_pbg8]
    or ah, ch
    mov [es:di], ah
    mov cl, 3
    sub cl, bl
    mov ah, al
    shr ah, cl
    and ah, 1
    neg ah
    mov ch, ah
    and ah, [cs_ink8]
    not ch
    and ch, [cs_pbg8]
    or ah, ch
    mov [es:di+1], ah
    inc si
    add di, 80
    dec bp
    jnz .r
    pop di
    inc bl
    cmp bl, 4
    jb .pl
    ret

; The 2bpp glyph expanders: a nibble of glyph bits to a byte of four pixels
; in colour 1, 2 or 3 (a set bit is the colour, a clear one is 0).
cs_nib1: db 0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15, 0x40, 0x41, 0x44, 0x45, 0x50, 0x51, 0x54, 0x55
cs_nib2: db 0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A, 0x80, 0x82, 0x88, 0x8A, 0xA0, 0xA2, 0xA8, 0xAA
cs_nib3: db 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF

; -----------------------------------------------------------------------------
; cs_utoa - AX in decimal at DS:DI, NUL-terminated, zero-padded to CL digits
;           (CL = 0: none). Preserves every register.
; -----------------------------------------------------------------------------
cs_utoa:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in BP
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    xor ch, ch
    xor bp, bp
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc bp
    or ax, ax
    jnz .d
    xor ax, ax
.pad:
    cmp bp, cx
    jae .p
    push ax
    inc bp
    jmp short .pad
.p:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    dec bp
    jnz .p
    mov byte [di], 0
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
