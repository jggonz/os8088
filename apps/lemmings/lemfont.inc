; =============================================================================
; os8088 - apps/lemmings/lemfont.inc      TEXT IN A FOREIGN MODE (SPEC.md 85.7)
;
; %included by apps/lemmings/lemmings.asm AFTER lemblit.inc, whose lb_rpix()
; and lb_kind it uses. CLAUDE.md's section discipline and the cdecl contract in
; lemmask.inc's header apply here word for word.
;
; NO KERNEL FONT SLOT IS LEGAL INSIDE THE BRACKET. os88_font_run(),
; os88_font_char() and every other drawing slot are REFUSED there (SPEC.md
; 53.1, apps/cc/os88.h), so the status line is lettered by this file out of
; MAIN.DAT's own glyphs - which is also what makes it look like Lemmings and
; not like the desktop.
;
; THE STATUS FONT is MAIN.DAT section 6 offset 0x1900: 38 glyphs, 8x16, THREE
; planes, in the order `%` `0`-`9` `-` `A`-`Z`
; (lemmings_3ds/doc/data/lemmings_main_dat_file_format.txt sections 5 and 9,
; which print the set literally). There is no lowercase and no space glyph, and
; that is why the index map below is a FOUR-ARM CASE with a fourth arm that
; draws an 8x16 BLACK CELL: Lemmix's Game.SkillPanel.pas DrawNewStr does
; exactly that for the space, for the template's own '.' and for anything else,
; after UpCase'ing both the old and the new character - so the whole line is
; uppercase on the glass whatever case it is stored in.
;
; THE PURPLE 16x16 FONT of the preview and postview screens arrived in wave 5,
; with those two screens (SPEC.md 116.4.3), and it is NOT a second index map
; over this file's status cell, which wave 1 expected: the two screens are in a
; different MODE (12h, the Hercules' own, mode 6) with a different row
; addressing, a 16-pixel cell and a transparent draw on a VGA. They are the
; "TWO SCREENS' SURFACE" section at the end of this file.
;
; IT DOES NOT PLOT PER PIXEL, AND THE FIRST VERSION DID. That version's header
; estimated a plot at "~50 cycles on an 8088, so a whole 40-character line is
; ~5,120 plots = ~54 ms" and a five-cell field at "~7 ms". THE BENCH SAID
; OTHERWISE: SPEC.md 116.7.1 conclusion 4 and PERFORMANCE.md Set 161 measured a
; five-cell field at 393 ms on VGA and 349 on CGA - 614 us a plot, ~2,900 cycles,
; FIFTY-EIGHT TIMES the estimate - which makes the whole line 3.1 s and puts the
; per-tick OUT / IN / clock update seven ticks behind the frame it belongs to.
; The estimate was a guess and the guess was wrong by nearly two orders of
; magnitude, which is PERFORMANCE.md rule 4 in one paragraph.
;
; SO THE CELL IS WRITTEN AS BYTES, NOT AS PIXELS, and the glyph's own storage is
; what makes that nearly free:
;
;   VGA        the three glyph planes ARE VGA planes 0..2 of the cell (the
;              colour's bit p is source plane p - the order the format
;              document gives and the order the deleted per-pixel decoder built),
;              and a cell column is col*8 - byte aligned by construction. So a
;              plane pass is `map mask 1<<p` and SIXTEEN byte stores, and plane
;              3 is sixteen zeros. 64 stores and 4 `out` pairs a cell, against
;              128 read-modify-writes and 1,280 `out`s.
;   CGA/Herc   the three planes OR to one 8-bit "ink" mask a row - the class map
;              is SOLID for 1..7 and there is no other colour in this font - and
;              lb_dbl doubles a nibble to a byte, which is exactly the 2-bits-a
;              -pixel CGA cell AND the doubled Hercules one. Two byte stores a
;              row, 32 a cell.
;
; AND THAT IS WHAT WAS MEASURED, not what is hoped for: the same five-cell
; field is 8.6 ms on VGA and 16.1 ms on CGA (PERFORMANCE.md Set 161's last row,
; SPEC.md 116.7.1's table and its conclusion 4, both of which carry the first
; pass beside it). 46x on VGA, 22x on CGA, and the per-tick update is a fifth
; of a tick rather than seven of them.
;
; THE LINE IS STILL DELTA-DRAWN - Lemmix's own rule, and the five field write
; offsets in SPEC.md 116's authority table are what make it cheap - so an
; ordinary tick letters the OUT and IN counts and the clock, about five cells.
; Cross-referenced BOTH WAYS with SPEC.md 116.7.1 conclusion 4 so the estimate
; and the measurement cannot drift apart again.
;
; IT IS SAFE INSIDE A lem_r_batch() BRACKET, and it was not: the VGA arm sets
; write mode 0 on the way in and used to leave the card in it, with the Map
; Mask at plane 3 alone, while [lb_batch] still said a bracket was open - so
; every later lb_rpix in that bracket wrote raw bit-plane data. See _lem_f_cell
; .out, which restores what it found.
;
; ATTRIBUTION. The glyphs are the original's, read off MAIN.DAT by
; tools/os88lem.py and never committed; the index map is the rule Lemmix's
; DrawNewStr implements. Lemmings is (C) 1991 DMA Design / Psygnosis and
; README.TXT carries the full attribution.
; =============================================================================

section .text

LEM_FGLYPHS equ 38                  ; '%' 0-9 '-' A-Z
LEM_FPLANE  equ 16                  ; bytes per plane of one 8x16 glyph
LEM_FCELL   equ 48                  ; ...and three planes of them

; -----------------------------------------------------------------------------
; int lem_f_index(int ch) - the glyph index of a character, or -1 for "an 8x16
; black cell". THE FOUR ARMS ARE Lemmix Game.SkillPanel.pas DrawNewStr's, in
; its own order: '%' -> 0, '0'..'9' -> ord - ord('0') + 1, '-' -> 11,
; 'A'..'Z' -> ord - ord('A') + 12, giving 0..37; anything else - the space and
; the template's own '.' included - falls to the black cell.
;
; The character is UPPERCASED first, because the font has no lowercase glyph
; and Lemmix's mixed-case strings are Lemmix's presentation, undone by its own
; UpCase at draw time.
; -----------------------------------------------------------------------------
_lem_f_index:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    and ax, 0xFF
    cmp al, 'a'
    jb .nolc
    cmp al, 'z'
    ja .nolc
    sub al, 32
.nolc:
    cmp al, '%'
    jne .dig
    xor ax, ax
    jmp .out
.dig:
    cmp al, '0'
    jb .dash
    cmp al, '9'
    ja .dash
    sub al, '0' - 1
    xor ah, ah
    jmp .out
.dash:
    cmp al, '-'
    jne .alpha
    mov ax, 11
    jmp .out
.alpha:
    cmp al, 'A'
    jb .none
    cmp al, 'Z'
    ja .none
    sub al, 'A' - 12
    xor ah, ah
    jmp .out
.none:
    mov ax, -1
.out:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_f_cell(unsigned seg, unsigned off, int col, int row, int ch)
;
; ONE 8x16 CELL of the status line, at PANEL character column `col` and pixel
; row `row`. `off` is the first byte of the 38-glyph bank inside LEMMAIN.
;
; A ZERO PIXEL IS WRITTEN TOO, and deliberately: that is what erases the
; character that was there and what draws the black cell of lem_f_index's fourth
; arm. Nothing is written twice - each plane byte, or each shadow byte, is
; stored exactly once.
;
; Locals: [bp-2] glyph index, [bp-4] the glyph's first byte, [bp-6] plane,
; [bp-8] the destination offset of row 0.
; -----------------------------------------------------------------------------
_lem_f_cell:
    push bp
    mov bp, sp
    sub sp, 8
    push si
    push di
    push es
    push ds
    ; the cell has to be inside the 320x40 panel, and it is byte aligned in
    ; every backend by construction (x = col*8)
    mov ax, [bp+8]                  ; col
    cmp ax, 0
    jl .out
    cmp ax, 40
    jge .out
    mov ax, [bp+10]                 ; row
    cmp ax, 0
    jl .out
    add ax, 16
    cmp ax, 40
    jg .out

    push word [bp+12]
    call _lem_f_index
    add sp, 2
    mov [bp-2], ax
    ; the glyph's first byte inside the bank, or -1 for "the black cell"
    cmp ax, 0
    jl .noglyph
    mov bx, LEM_FCELL
    mul bx
    add ax, [bp+6]
    mov [bp-4], ax
    jmp .have
.noglyph:
    mov word [bp-4], -1
.have:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .shadow

    ; --- VGA: three planes copied straight in, the fourth zeroed -----------
    ; write mode 0 with the whole bit mask, which is the ambient state every
    ; other writer in lemblit.inc leaves behind - stated rather than assumed,
    ; because a caller could be inside lb_batch's write mode 2. .out restores
    ; that bracket if there was one, so this is safe in both ambients.
    mov ah, 0
    mov al, 5
    call lb_gc
    mov ah, 0xFF
    mov al, 8
    call lb_gc
    mov ax, [bp+10]
    call lem_rowoff                 ; DI = row * 200, the VGA panel stride
    add di, [bp+8]                  ; + the byte column
    mov [bp-8], di
    mov ax, [lb_fb]                 ; ...before DS moves: see lb_pplane
    mov es, ax
    mov word [bp-6], 0
.vplane:
    mov ax, 1
    mov cx, [bp-6]
    shl ax, cl
    mov ah, al
    mov al, 2                       ; Map Mask: this plane only
    call lb_seq
    ; plane 3 of a three-plane glyph, and every plane of a black cell, is 16
    ; zero bytes; the rest is a straight copy of the glyph's own plane.
    cmp word [bp-2], 0
    jl .vzero                       ; the black cell: every plane is zeros
    cmp word [bp-6], 3
    je .vzero                       ; a three-plane glyph leaves plane 3 clear
    mov si, [bp-4]
    mov ax, [bp-6]
    mov cl, 4
    shl ax, cl                      ; plane * LEM_FPLANE (16)
    add si, ax
    mov ax, [bp+4]
    mov ds, ax
    mov di, [bp-8]
    mov cx, 16
.vcopy:
    mov al, [si]
    mov [es:di], al
    inc si
    add di, LEM_VSTRIDE
    loop .vcopy
    push cs
    pop ds
    jmp .vnext
.vzero:
    mov di, [bp-8]
    mov cx, 16
    xor al, al
.vclr:
    mov [es:di], al
    add di, LEM_VSTRIDE
    loop .vclr
.vnext:
    inc word [bp-6]
    cmp word [bp-6], 4
    jb .vplane
    jmp .out

    ; --- CGA and Hercules: one ink mask a row, doubled into two bytes ------
    ; The class map has one arm for this font: every non-zero colour of a
    ; 3-plane glyph is 1..7, which lb_spix draws SOLID, so the three planes OR
    ; to the row's ink and lb_dbl turns a nibble into the byte both backends
    ; want (CGA two bits a pixel, Hercules two doubled pixels).
.shadow:
    cmp word [bp-2], 0
    jl .sblank
    mov si, [bp-4]
    mov ax, [bp+4]
    mov es, ax                      ; the CLAIM in ES, so DS stays ours and
    xor di, di                      ; lf_mask below is reachable
.smrow:
    mov al, [es:si]
    or al, [es:si+LEM_FPLANE]
    or al, [es:si+2*LEM_FPLANE]
    mov [lf_mask+di], al
    inc si
    inc di
    cmp di, 16
    jb .smrow
    jmp .sdraw
.sblank:
    xor di, di
.sbrow:
    mov byte [lf_mask+di], 0
    inc di
    cmp di, 16
    jb .sbrow
.sdraw:
    mov ax, [lb_shadow]
    mov es, ax
    mov word [bp-6], 0
.srow:
    mov ax, [bp+10]
    add ax, [bp-6]
    add ax, LEM_VIEWH               ; the panel occupies shadow rows 160..199
    ; the row is marked dirty WHOLE, for lb_spix's own reason: two byte stores
    ; against a per-cell span merge, and the panel's forty rows are redrawn as
    ; a block or not at all
    mov bx, ax
    mov byte [lb_dlo+bx], 0
    mov byte [lb_dhi+bx], LEM_SSTRIDE - 1
    call lb_rowoff8                 ; DI = row * 80
    mov ax, [bp+8]
    shl ax, 1                       ; a cell is TWO shadow bytes on both cards
    add di, ax
    mov bx, [bp-6]
    mov al, [lf_mask+bx]
    mov ah, al
    and ah, 0x0F                    ; the RIGHT four pixels
    mov cl, 4
    shr al, cl                      ; ...and the left four
    push ax
    xor bx, bx
    mov bl, al
    mov dl, [lb_dbl+bx]             ; doubled: the first byte
    pop ax
    xor bx, bx
    mov bl, ah
    mov dh, [lb_dbl+bx]             ; ...and the second
    mov [es:di], dl
    mov [es:di+1], dh
    inc word [bp-6]
    cmp word [bp-6], 16
    jb .srow
.out:
    pop ds
    ; THE CALLER'S BRACKET, PUT BACK. The VGA arm above set write mode 0 and a
    ; whole Bit Mask on the way in and leaves the Map Mask at 0x08 (the last
    ; .vplane pass) on the way out - which is fatal INSIDE a lem_r_batch()
    ; bracket, because lb_rpix skips lb_batch_on whenever [lb_batch] is
    ; non-zero: every later plot in that bracket would write raw bit-plane
    ; data into plane 3 alone. No caller does it today (lem_frame_panel and
    ; lem_frame_first letter outside any bracket), so this is a trap being
    ; disarmed rather than a defect being fixed - but wave 3 redraws the status
    ; line every tick beside the minimap and the view rectangle, which is
    ; exactly the call order that lands it. lb_batch_on is the two GC/Seq
    ; writes lb_batch_in would have made, and it returns at its first compare
    ; on the two shadow backends. DS FIRST: lb_batch_on reads [lb_kind].
    cmp word [lb_batch], 0
    je .nobatch
    call lb_batch_on
.nobatch:
    pop es
    pop di
    pop si
    add sp, 8                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

section .bss
lf_mask:    resb 16                 ; one cell's ink, a byte a row (the shadow
                                    ; backends only)
section .text

; -----------------------------------------------------------------------------
; void lem_f_run(unsigned seg, unsigned off, int col, int row,
;                const char *s, int n)
;
; `n` cells from `s`, left to right. It exists so that a delta-drawn field is
; ONE call rather than five - the same reason os88_font_run() exists on the
; desktop side (SPEC.md 6.1) - and the caller may pass a shorter string than
; `n`, in which case the rest are the fourth arm's black cells, which is what
; Lemmix's PadRight does.
; -----------------------------------------------------------------------------
_lem_f_run:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+12]
    xor di, di
.cell:
    cmp di, [bp+14]
    jge .out
    xor ax, ax
    cmp si, 0
    je .blank
    mov al, [si]
    cmp al, 0
    jne .have
    mov si, 0                       ; the string ended: pad with black cells
    xor ax, ax
.blank:
.have:
    push ax
    mov ax, [bp+10]
    push ax
    mov ax, [bp+8]
    add ax, di
    push ax
    push word [bp+6]
    push word [bp+4]
    call _lem_f_cell
    add sp, 10
    cmp si, 0
    je .next
    inc si
.next:
    inc di
    jmp .cell
.out:
    pop di
    pop si
    pop bp
    ret

section .text

; =============================================================================
; WAVE 5: THE TWO SCREENS' SURFACE (SPEC.md 116.4.3)
;
; The preview and the postview are the original's 640x350 screens (Lemmix
; GameScreen.Preview.pas:114 / Postview.pas:59, "the original dos-screen
; size"), drawn inside the bracket in the SECOND foreign mode each adapter has
; for them: mode 12h on a VGA (640x480, the layout unscaled and centred, 65
; rows of margin above and below), the Hercules' own 720x348 (letterboxed at
; x = 40, rows 348 and 349 off the bottom - the lowest glyph row the screens
; letter is 337), and mode 6 on a CGA or an EGA (640x200: the C compresses the
; layout to fit, lemovl.c). The C speaks in the ORIGINAL'S coordinates and
; never knows which card it is on; lp_row is the one place the three meet.
;
; WHAT IS DRAWN, and with what:
;
;   lem_p_clear  the rows the tile does not cover: the picture's well, and a
;                VGA's margins
;   lem_p_tile   MAIN.DAT section 3's 320x104 2bpp brown tile, tiled down from
;                a row (Lemmix TileBackgroundBitmap) two tiles across
;   lem_p_text   the 16x16 3bpp purple glyphs of section 4 (94 of them, ASCII
;                33..126); a space advances 16 and draws nothing, which is
;                DrawPurpleText's own rule
;   lem_p_rect   a filled box: the level picture's black well, the load bar
;   lem_p_mini   ONE ROW of the shrunk level picture, out of the world the
;                compose left in RAM (T and the mask on a VGA, the mask on the
;                other two)
;   lem_p_pal    the sixteen colours: the menu palette's 0..7 and the level's
;                own 8..15 (VGA only)
;
; THE TWO MONOCHROME CARDS GET COLOUR CLASSES, NOT SHADES (SPEC.md 39.4), and
; what the classes are was decided by LOOKING: the tile is colours 0..3 -
; black, (128,64,32), (96,48,32) and (48,0,16) - and only colour 1, the
; lightest brown and 1.7% of the tile, is lit; lit at 1, 2 AND 3 the text sat
; in a field of speckle as dense as itself. Every purple glyph pixel is 4..7
; (measured over all 94), so PLANE 2 IS THE GLYPH'S INK, and on the two mono
; cards a glyph is drawn OPAQUE - its 16x16 cell black, its ink lit - so a
; speckle never touches a letter. On a VGA it is transparent, as Lemmix draws
; it, through the latches: bit mask = the ink, one write a plane.
;
; ATTRIBUTION. The tile and the glyphs are the original's, read off MAIN.DAT
; by tools/os88lem.py and never committed; the layout they are put in is
; Lemmix's (lemovl.c). Lemmings is (C) 1991 DMA Design / Psygnosis.
; =============================================================================

LEM_PVGAY   equ 65                  ; (480 - 350) / 2: mode 12h, centred
LEM_PHERCX  equ 5                   ; 40 pixels: the Hercules letterbox
LEM_PROWS   equ 350                 ; the original's surface...
LEM_PHERCH  equ 348                 ; ...what a Hercules shows of it
LEM_PCGAH   equ 200                 ; ...and a CGA (the C fits the layout)
LEM_PTILEH  equ 104                 ; the brown tile's rows
LEM_PTPLANE equ 4160                ; ...and one of its two planes, 320x104/8
LEM_PGLYPH  equ 96                  ; one purple glyph: three planes of 32

; -----------------------------------------------------------------------------
; void lem_p_setup(int kind, unsigned fbseg) - which card, and its framebuffer
; as os88_fsx_mode() just described it (FSI_SEG). `kind` is lb_kind's code.
; -----------------------------------------------------------------------------
_lem_p_setup:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [lp_kind], ax
    mov ax, [bp+6]
    mov [lp_fb], ax
    mov ax, LEM_PROWS
    cmp word [lp_kind], LEM_KIND_HERC
    jne .nh
    mov ax, LEM_PHERCH
.nh:
    cmp word [lp_kind], LEM_KIND_CGA
    jne .nc
    mov ax, LEM_PCGAH
.nc:
    mov [lp_h], ax
    pop bp
    ret

; lp_row - in: AX = a row of the 640x350 surface. out: DI = the framebuffer
; offset of its first byte (the Hercules letterbox included), CF = 1 when the
; card does not show that row. Everything else preserved. A `mul` a row is
; ~130 cycles against a glyph's sixteen rows of drawing; nothing calls it per
; pixel.
lp_row:
    cmp ax, 0
    jl .clip
    cmp ax, [lp_h]
    jge .clip
    push ax
    push cx
    push dx
    cmp word [lp_kind], LEM_KIND_VGA
    jne .nvga
    add ax, LEM_PVGAY
    mov cx, 80
    mul cx
    mov di, ax
    jmp .ok
.nvga:
    mov di, ax
    cmp word [lp_kind], LEM_KIND_HERC
    jne .cga
    and di, 3                       ; four banks of 0x2000, 90 bytes a row
    mov cl, 13
    shl di, cl
    shr ax, 1
    shr ax, 1
    mov cx, 90
    mul cx
    add di, ax
    add di, LEM_PHERCX
    jmp .ok
.cga:
    and di, 1                       ; two banks of 0x2000, 80 bytes a row
    mov cl, 13
    shl di, cl
    shr ax, 1
    mov cx, 80
    mul cx
    add di, ax
.ok:
    pop dx
    pop cx
    pop ax
    clc
    ret
.clip:
    stc
    ret

; lp_vga0 - write mode 0, the whole Bit Mask, no Set/Reset, no rotate: the
; ambient state every routine here assumes, stated once on a freshly set mode
; 12h rather than trusted. Clobbers AX, DX.
lp_vga0:
    mov ax, 0x0005                  ; AL = index, AH = value (lb_gc's order)
    call lb_gc
    mov ax, 0xFF08
    call lb_gc
    mov ax, 0x0001
    call lb_gc
    mov ax, 0x0003
    call lb_gc
    mov ax, 0x0F02                  ; ...and the Map Mask: every plane
    jmp lb_seq

; -----------------------------------------------------------------------------
; void lem_p_clear(int y0, int y1, int margins) - surface rows [y0, y1) black,
; and on a VGA, when `margins` is not 0, mode 12h's two 65-row margins around
; the surface as well.
;
; ONLY WHAT NOTHING ELSE WRITES (wave 5 review): the brown tile is opaque over
; every row it covers, so clearing the whole page first wrote most of it twice
; - 38,400 bytes a VGA screen, 32 KB a Hercules one, 16 KB a CGA one, on every
; preview and postview. The callers clear the picture's WELL (the preview) and
; nothing on the mono cards for the postview. The VGA margins are cleared on
; every screen because they are the one place a BIOS that leaves mode 12h
; dirty (SPEC.md 39.23 - the mode 3 character generator in plane 2) would
; show - and ONLY AFTER A MODE SET THAT ACTUALLY HAPPENED (`margins`, wave 6,
; wave 5's third review): a screen that follows a screen in the same 12h
; skips the set (ovl_r_mode) and nothing ever writes outside the surface rows,
; so the margins are the black the first set's clear left, and re-clearing
; them was 5,200 words of rep stosw through four planes - ~10 KB of ISA VRAM,
; ~20-30 ms on the XT - on every postview and every next preview. The Hercules letterbox is never written here: FSXM_HERC's vid_setmode
; wipes the whole 32 KB page itself (kernel/fsx.inc), and a screen that skips
; the mode set (ovl_r_mode) is following one that left the border black.
; -----------------------------------------------------------------------------
_lem_p_clear:
    push bp
    mov bp, sp
    push di
    push es
    mov ax, [lp_fb]
    mov es, ax
    cld
    cmp word [lp_kind], LEM_KIND_VGA
    jne .rows
    call lp_vga0
    cmp word [bp+8], 0
    je .rows                        ; the margins are still the set's black
    xor ax, ax
    xor di, di                      ; the top margin
    mov cx, (LEM_PVGAY * 80) / 2
    rep stosw
    mov di, (LEM_PVGAY + LEM_PROWS) * 80
    mov cx, ((480 - LEM_PVGAY - LEM_PROWS) * 80) / 2
    rep stosw                       ; ...and the bottom one
.rows:
    mov ax, [bp+4]
.row:
    cmp ax, [bp+6]
    jge .out
    call lp_row                     ; DI; CF = the card does not show it
    jc .next
    push ax
    xor ax, ax
    mov cx, 40                      ; the surface's 640 pixels on every card
    rep stosw
    pop ax
.next:
    inc ax
    jmp .row
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_p_rect(int x, int y, int w, int h, int colour) - x and w in pixels,
; multiples of 8. On a VGA `colour` is the palette index (the colour's planes
; set, the others clear: two passes a row); on the mono cards 0 is black and
; anything else is lit.
; -----------------------------------------------------------------------------
_lem_p_rect:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [lp_fb]
    mov es, ax
    cld
    cmp word [lp_kind], LEM_KIND_VGA
    jne .go
    call lp_vga0
.go:
    mov si, [bp+10]
.row:
    cmp si, 0
    jle .out
    mov ax, [bp+6]
    call lp_row
    jc .next
    mov ax, [bp+4]
    mov cl, 3
    shr ax, cl
    add di, ax
    mov cx, [bp+8]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    cmp word [lp_kind], LEM_KIND_VGA
    jne .mono
    push cx
    push di
    mov ah, [bp+12]
    and ah, 0x0F
    mov al, 2
    call lb_seq
    pop di
    pop cx
    push cx
    push di
    mov al, 0xFF
    rep stosb
    mov ah, [bp+12]
    not ah
    and ah, 0x0F
    mov al, 2
    call lb_seq
    pop di
    pop cx
    xor al, al
    rep stosb
    jmp .next
.mono:
    xor al, al
    cmp word [bp+12], 0
    je .fill
    mov al, 0xFF
.fill:
    rep stosb
.next:
    inc word [bp+6]
    dec si
    jmp .row
.out:
    cmp word [lp_kind], LEM_KIND_VGA
    jne .done
    mov ax, 0x0F02                  ; the Map Mask back to every plane
    call lb_seq
.done:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_p_tile(unsigned seg, unsigned off, int y0, int y1) - the brown tile
; at seg:off, its first row at surface row y0 and repeated down to y1 (Lemmix
; TileBackgroundBitmap(0, y0)), two copies across the 640.
;
; VGA: tile planes 0 and 1 straight into VGA planes 0 and 1, and planes 2 and 3
; zeroed - the tile is colours 0..3. Mono: lit where the colour is 1 (plane 0
; set, plane 1 clear) - see the section header for why only 1.
;
; Locals: [bp-2] the tile row.
; -----------------------------------------------------------------------------
_lem_p_tile:
    push bp
    mov bp, sp
    sub sp, 2
    push si
    push di
    push es
    mov ax, [lp_fb]
    mov es, ax
    cld
    cmp word [lp_kind], LEM_KIND_VGA
    jne .start
    call lp_vga0
.start:
    mov word [bp-2], 0
.row:
    mov ax, [bp+8]
    cmp ax, [bp+10]
    jge .out
    call lp_row
    jc .next
    mov ax, [bp-2]
    mov cx, 40
    mul cx
    add ax, [bp+6]
    mov si, ax                      ; the tile row's plane 0
    cmp word [lp_kind], LEM_KIND_VGA
    jne .mono
    mov ax, 0x0102                  ; plane 0
    call lb_seq
    call lp_cpy2
    add si, LEM_PTPLANE
    mov ax, 0x0202                  ; plane 1
    call lb_seq
    call lp_cpy2
    mov ax, 0x0C02                  ; planes 2 and 3: the tile has none
    call lb_seq
    xor ax, ax
    mov cx, 40
    rep stosw
    jmp .next
.mono:
    push ds
    mov ax, [bp+4]
    mov ds, ax
    mov cx, 40
.mb:
    mov al, [si]
    mov ah, [si+LEM_PTPLANE]
    not ah
    and al, ah                      ; colour 1: plane 0 and not plane 1
    mov [es:di], al
    mov [es:di+40], al
    inc si
    inc di
    loop .mb
    pop ds
.next:
    inc word [bp+8]
    inc word [bp-2]
    cmp word [bp-2], LEM_PTILEH
    jb .row
    mov word [bp-2], 0
    jmp .row
.out:
    cmp word [lp_kind], LEM_KIND_VGA
    jne .done
    mov ax, 0x0F02
    call lb_seq
.done:
    pop es
    pop di
    pop si
    add sp, 2                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; lp_cpy2 - one 40-byte tile row at [bp+4]:SI into ES:DI and again 40 bytes
; along. SI and DI preserved; clobbers AX, CX. Reads the CALLER'S [bp+4],
; which is the tile's segment (lem_p_tile's frame - BP is its).
lp_cpy2:
    push ds
    push si
    push di
    mov ax, [bp+4]
    mov ds, ax
    mov cx, 20
    rep movsw
    sub si, 40
    mov cx, 20
    rep movsw
    pop di
    pop si
    pop ds
    ret

; -----------------------------------------------------------------------------
; void lem_p_text(unsigned seg, unsigned off, int x, int y, const char *s,
;                 int n)
;
; `n` characters of `s` from surface pixel (x, y), x a multiple of 8 - every
; place the C letters is one (16 x a column, or 320 - 8 x a length when
; centred). `seg:off` is the 94-glyph bank. A space advances 16 and draws
; nothing; a character outside 33..126 advances nothing (DrawPurpleText's
; `case`, which a CR in the middle of a line would otherwise turn into a
; glyph); a NUL ends the string early.
; -----------------------------------------------------------------------------
_lem_p_text:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+4]
    mov [lp_fseg], ax
    mov ax, [lp_fb]
    mov es, ax
    cmp word [lp_kind], LEM_KIND_VGA
    jne .go
    call lp_vga0
.go:
    mov si, [bp+12]
.ch:
    cmp word [bp+14], 0
    jle .out
    dec word [bp+14]
    mov al, [si]
    inc si
    cmp al, 0
    je .out
    cmp al, ' '
    je .adv
    cmp al, '!'
    jb .ch
    cmp al, '~'
    ja .ch
    sub al, '!'
    xor ah, ah
    mov cx, LEM_PGLYPH
    mul cx
    add ax, [bp+6]
    mov bx, [bp+8]
    mov dx, [bp+10]
    push si
    mov si, ax
    call lp_glyph
    pop si
.adv:
    add word [bp+8], 16
    jmp .ch
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; lp_glyph - one glyph whose plane 0 starts at [lp_fseg]:SI, at surface
; (BX, DX), BX a multiple of 8. ES = the framebuffer. Clobbers AX, BX, CX, DX,
; SI, DI. A glyph whose cell is not wholly inside the 640 columns is not drawn
; (every caller centres or indents within them).
lp_glyph:
    cmp bx, 624
    ja .out                         ; unsigned: a negative x too
    mov cl, 3
    shr bx, cl                      ; the byte column
    cmp word [lp_kind], LEM_KIND_VGA
    jne .mono

    ; --- VGA: transparent, through the latches ----------------------------
    ; mode 12h is linear and LEM_PVGAY keeps every row of a 350-row surface on
    ; its 480, so row 0's offset and +80 a row is the whole address
    mov ax, dx
    call lp_row
    jc .out
    add di, bx
    xor bx, bx                      ; BX = the plane's offset in the glyph
    mov cx, 1                       ; CX = the Map Mask bit
.vp:
    push cx
    mov ah, cl
    mov al, 2
    call lb_seq
    push si
    push di
    push ds
    mov ax, [lp_fseg]
    mov ds, ax
    mov cx, 16
    mov dx, VGA_GC_IDX
.vr:
    mov ah, [si+64]                 ; plane 2: the ink of the left byte
    mov al, 8
    out dx, ax                      ; Bit Mask = the ink
    mov al, [es:di]                 ; the latches: what is there now
    mov al, [si+bx]
    mov [es:di], al
    mov ah, [si+65]                 ; ...and the right byte
    mov al, 8
    out dx, ax
    mov al, [es:di+1]
    mov al, [si+bx+1]
    mov [es:di+1], al
    add si, 2
    add di, 80
    loop .vr
    pop ds
    pop di
    pop si
    pop cx
    add bx, 32
    shl cx, 1
    cmp cx, 8
    jb .vp
    mov ax, 0xFF08                  ; the Bit Mask back to whole bytes
    call lb_gc
    mov ax, 0x0F02                  ; ...and the Map Mask to every plane
    jmp lb_seq

    ; --- mono: opaque, the ink lit and the rest of the cell black ----------
.mono:
    mov cx, 16
.mr:
    mov ax, dx
    call lp_row
    jc .mnext
    add di, bx
    push ds
    mov ax, [lp_fseg]
    mov ds, ax
    mov ax, [si+64]                 ; plane 2's two bytes, in screen order
    pop ds
    mov [es:di], ax
.mnext:
    add si, 2
    inc dx
    loop .mr
.out:
    ret

; -----------------------------------------------------------------------------
; void lem_p_mini(int y, int srow) - ONE ROW of the shrunk level picture: world
; row `srow` into surface row y, the world's 1,600-pixel stride 2:5 into 640.
;
; 2:5 AND NOT LEMMIX'S 1,584:640, and on purpose. Lemmix stretches the world
; to the 640 exactly (GameScreen.Preview.pas: DrawTo(target, DstRect,
; fullLevel.BoundsRect)) - 2.475 world pixels a screen pixel, a phase that
; returns to a byte boundary only every 320 pixels. At 2.5 it returns every 16:
; five world bytes are two screen bytes, and the sixteen picked bits are fixed
; positions (lp_samp), so a row is 40 groups of straight-line shifts with no
; per-pixel test or branch - about four times fewer cycles than a nearest-
; neighbour walk, which was priced at ~6 s a VGA picture on a 4.77 MHz 8088
; (204,800 samples at ~140 cycles). What it costs the picture is its right
; edge: 640 x 2.5 is 1,600, so the last six columns are the sixteen world
; pixels past x 1,583, which are never terrain (SPEC.md 116.4.3).
;
; A VGA row is four passes, one a plane: T's planes 0..2 (lem_r_tsetup) and
; the solid mask as plane 3 - so a terrain pixel is colour 8..15 and the
; preview's palette entries 8..15 are the level's own (lem_p_pal). On the
; mono cards it is the mask alone, 50% dithered by the screen row - the
; Hercules world's own class (lemmask.inc lem_m_derive).
;
; Locals: [bp-2] the screen row's offset, [bp-4] the world row's.
; -----------------------------------------------------------------------------
_lem_p_mini:
    push bp
    mov bp, sp
    sub sp, 4
    push si
    push di
    push es
    mov ax, [bp+4]
    call lp_row
    jc .out
    mov [bp-2], di
    mov ax, [bp+6]
    cmp ax, LEM_MROWS
    jae .out
    call lem_rowoff
    mov [bp-4], di
    mov ax, [lp_fb]
    mov es, ax
    cld
    cmp word [lp_kind], LEM_KIND_VGA
    je .vga
    mov bh, 0xAA
    test byte [bp+4], 1
    jz .dith
    mov bh, 0x55
.dith:
    mov ax, [lm_seg]
    mov si, [bp-4]
    mov di, [bp-2]
    push ds
    mov ds, ax
    call lp_samp
    pop ds
    jmp .out
.vga:
    xor cx, cx                      ; the plane
.vp:
    push cx
    mov ah, 1
    shl ah, cl
    mov al, 2
    call lb_seq
    pop cx
    push cx
    mov ax, [lm_seg]
    cmp cx, 3
    je .seg
    mov ax, LEM_TPLANE
    mul cx
    add ax, [lb_t]
.seg:
    mov si, [bp-4]
    mov di, [bp-2]
    mov bh, 0xFF
    push ds
    mov ds, ax
    call lp_samp
    pop ds
    pop cx
    inc cx
    cmp cx, 4
    jb .vp
    mov ax, 0x0F02
    call lb_seq
.out:
    pop es
    pop di
    pop si
    add sp, 4                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; lp_samp - 1,600 bits at DS:SI to 640 at ES:DI, 2:5. BH = an AND mask for
; every output byte (0xFF, or a dither). Screen pixel i of a 16-pixel group
; takes world bit floor(5i / 2): 0 2 5 7 | 10 12 15 | 17 20 22 | 25 27 30 |
; 32 35 37 of the group's five bytes, MSB first - so each byte is loaded once
; and shifted left, a `shl` a bit it passes and an `rcl` into DX for each bit
; it gives. Clobbers AX, CX, DX, SI, DI.
lp_samp:
    mov cx, 40
.grp:
    lodsb                           ; bits 0..7: 0 2 5 7
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    lodsb                           ; 8..15: 10 12 15
    shl al, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    lodsb                           ; 16..23: 17 20 22
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    lodsb                           ; 24..31: 25 27 30
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    lodsb                           ; 32..39: 32 35 37
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    shl al, 1
    shl al, 1
    rcl dx, 1
    mov al, dh                      ; the first eight pixels are DX's top
    mov ah, dl
    and al, bh
    and ah, bh
    stosw
    loop .grp
    ret

; -----------------------------------------------------------------------------
; void lem_p_pal(unsigned mseg, unsigned moff, unsigned bseg, unsigned coff)
;
; THE SCREENS' SIXTEEN COLOURS (VGA only): entries 0..7 are the MAIN-MENU
; palette's (LEMMAIN's header, tools/os88lem.py MENU_PALETTE - black, the
; tile's three browns, the font's four purples) and 8..15 are the LEVEL'S own
; custom eight, which are the colours the shrunk picture's terrain pixels carry
; (8 + planes 0..2). Sixteen fits exactly: the menu palette's own 8..15 - its
; greens, white, yellow, blue and pink - are colours neither screen draws.
; -----------------------------------------------------------------------------
_lem_p_pal:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    cmp word [lp_kind], LEM_KIND_VGA
    jne .out
    call lb_acid
    mov dx, VGA_DAC_WIDX
    xor al, al
    out dx, al
    mov dx, VGA_DAC_DATA
    cld
    mov ax, [bp+4]
    mov si, [bp+6]
    mov bx, [bp+8]
    mov di, [bp+10]                 ; the second source's offset, held
    mov ds, ax
    mov cx, 24
.a:
    lodsb
    out dx, al
    loop .a
    mov ds, bx
    mov si, di
    mov cx, 24
.b:
    lodsb
    out dx, al
    loop .b
.out:
    pop ds
    pop di
    pop si
    pop bp
    ret

section .bss
lp_kind:    resw 1                  ; LEM_KIND_* of the screens' surface
lp_fb:      resw 1                  ; ...its framebuffer segment
lp_h:       resw 1                  ; ...the surface rows it shows
lp_fseg:    resw 1                  ; the purple font's segment, per call
section .text
