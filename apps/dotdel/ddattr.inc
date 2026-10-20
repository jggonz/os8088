; =============================================================================
; os8088 - apps/dotdel/ddattr.inc
;
; THE ATTRACT SCREEN (SPEC.md 93.11): the title, the table, the blink and the
; demo.
;
; --- THE TITLE IS A GRID, NOT A FACE -----------------------------------------
;
; The nine letters DOT DELIRIUM needs are 5x7 grids of solid cells, and a cell
; becomes a rectangle of whatever size the surface can afford. That is the
; whole of the type design and it is deliberate: a grid scaled to an integer
; number of pixels has FLAT edges and SHARP corners at every size, where an
; outline scaled the same way has neither. It is also why the title is legible
; at ch = 2 on a windowed CGA, which no curve would be.
;
; It goes down as SEVEN BANDS, one per grid row, rather than as ~170 rectangles
; through `gfx_fill`: at SPEC.md 5.7's ~756 us a call that would be 128 ms of
; arrival every time the window is raised.
;
; --- THE DEMO IS THE GAME, ON A SLICE (SPEC.md 93.11.2) ----------------------
;
; The playfield under the table is the REAL board - the same layout, the same
; ghosts, the same AI - restricted to its bottom eleven tile rows. Two words do
; all of it: `dd_rmin` makes `dd_tile` answer WALL above the slice, so nobody
; walks out of the top of it, and `dd_clipy0` moves where `dd_blit` thinks the
; board's top edge is. Every coordinate in the renderer stays a board
; coordinate and not one call site knows the difference.
;
; What the demo is NOT is a script. A recorded demo would have to be re-recorded
; for each of the three layouts and would desynchronise the moment anything in
; the AI changed; this one plays whatever board the level counter is on.
; =============================================================================

DD_ATR0    equ 20               ; the slice at its FULLEST: tile rows 20..30
DD_ATROWS  equ DD_ROWS - DD_ATR0
DD_ATRMIN  equ 4                ; ...and the fewest rows worth showing at all
DD_TGW     equ 5                ; the title grid, per glyph
DD_TGH     equ 7
DD_TGADV   equ 6                ; ...and the pitch, so there is a column of air
DD_TITLEN  equ 12               ; "DOT DELIRIUM"
; THE PAGE'S MINIMUM GAPS, and the slack over them is SPREAD (SPEC.md 93.11.4)
DD_ATTOP   equ 4                ; above the title
DD_ATG1    equ 6                ; title to the score table
DD_ATGM    equ 8                ; ...and the two either side of the play line,
                                ; together, because they are kept equal
DD_ATFIX   equ DD_ATTOP + DD_ATG1 + DD_ATGM \
                + (DD_NHS + 1) * DD_HUDROW + DD_HUDROW
DD_BLINKON equ 9                ; the play line, in ticks - TANK ATTACK's rate
DD_BLINKOF equ 5                ; (SPEC.md 85.10.3), so the two title screens
                                ; on this machine blink at one speed

; -----------------------------------------------------------------------------
; dd_attract_begin - back to the title
; -----------------------------------------------------------------------------
dd_attract_begin:
    cmp byte [dd_ok], 0             ; NO LAYOUT, NO TITLE (SPEC.md 93.3.4.5):
    jne .go                         ; Esc and a game over reach here too, and
    ret                             ; dd_demo_reset renders the board
.go:
    call dd_attract_layout          ; FIRST: it decides how many tile rows the
                                    ; slice is, and dd_demo_reset decodes the
                                    ; grid against that
    mov byte [dd_state], DDS_ATTRACT
    mov byte [dd_demo], 1
    mov byte [dd_paused], 0
    mov byte [dd_level], 1
    mov word [dd_score], 0
    mov word [dd_score+2], 0
    mov byte [dd_blink], 1
    mov byte [dd_blinkt], DD_BLINKON
    call dd_demo_reset
    mov byte [dd_full], 1
    ret

; dd_demo_reset - the slice, its dots, and four ghosts already loose in it
dd_demo_reset:
    push si
    mov al, [dd_atr0]
    mov [dd_rmin], al
    call dd_level_load
    call dd_board_render            ; the slice's own top edge is a wall now,
    call dd_demo_place              ; so the picture has to be laid again
    pop si
    ret

dd_demo_place:
    push si
    xor si, si
    mov cl, DD_STARTC
    mov ch, DD_ROWS - 2
    call dd_demo_open
    call dd_put_tile
    mov byte [dd_dir + 0], DIR_L
    mov byte [dd_want + 0], DIR_L
    mov byte [dd_alive + 0], 1
    mov byte [dd_shown + 0], 0
    mov byte [dd_lastd], DIR_L
    mov si, 1
.g:
    mov bx, si
    dec bx
    mov cl, [dd_demoC + bx]
    mov ch, [dd_demoR + bx]
    call dd_demo_open               ; the slice may be four rows: put them on a
    call dd_put_tile                ; tile it actually has
    mov byte [dd_alive + si], 1
    mov byte [dd_shown + si], 0
    mov byte [dd_dir + si], DIR_R
    mov byte [dd_want + si], DIR_R
    mov byte [dd_gs + bx], GS_ROAM  ; no house in the slice: they are all out
    mov byte [dd_glance + bx], 0
    mov byte [dd_gbig + bx], 0
    inc si
    cmp si, DD_NACT
    jb .g
    call dd_anims                   ; an image and a pen before anything draws
    mov word [dd_modeidx], 0
    mov word [dd_modech], 1         ; the demo CHASES: a scatter phase would
    mov ax, [dd_modet]              ; be four ghosts walking into corners
    mov [dd_modetim], ax
    mov word [dd_frcnt], 0
    mov byte [dd_eatn], 0
    mov word [dd_gpause], 0
    mov word [dd_eaten], 0
    mov word [dd_fruit], 0
    pop si
    ret

; where the four of them start in the slice - a column each, on the row above
; its floor, and dd_demo_open walks right from there to a tile that is open
dd_demoC: db 2, 10, 17, 25
dd_demoR: db DD_ROWS - 2, DD_ROWS - 2, DD_ROWS - 2, DD_ROWS - 2

; dd_demo_open - CL, CH = the first open tile at or right of CL on row CH
dd_demo_open:
    push ax
    push bx
    mov bl, 0
.step:
    call dd_tile
    cmp al, TT_WALL
    jne .out
    inc cl
    cmp cl, DD_COLS
    jb .next
    xor cl, cl
.next:
    inc bl
    cmp bl, DD_COLS
    jb .step
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_attract_step - one tick of the title screen
; -----------------------------------------------------------------------------
dd_attract_step:
    ; the blink, on its own clock and drawn by nothing else
    dec byte [dd_blinkt]
    jnz .noblink
    xor byte [dd_blink], 1
    mov al, DD_BLINKOF
    cmp byte [dd_blink], 0
    je .bt
    mov al, DD_BLINKON
.bt:
    mov [dd_blinkt], al
    mov byte [dd_blinkd], 1
.noblink:
    cmp word [dd_gpause], 0
    je .go
    dec word [dd_gpause]
    ret
.go:
    call dd_input                   ; ...which is the demo AI, [dd_demo] = 1
    call dd_modes
    xor si, si
    call dd_act_move
    mov si, 1
.g:
    push si
    call dd_gh_think
    call dd_act_move
    pop si
    inc si
    cmp si, DD_NACT
    jb .g
    call dd_collide
    call dd_anims
    cmp word [dd_ndots], 0
    jne .out
    call dd_demo_reset              ; it cleared the slice: lay it again
    mov byte [dd_full], 1
.out:
    ret

; -----------------------------------------------------------------------------
; dd_demo_think - Smiles, played by the machine (SPEC.md 93.11.3)
;
; It is not a good player and is not meant to be: it prefers a dot, it will not
; walk into a ghost, and it keeps going rather than dithering at a junction.
;
; TWO THINGS STOP IT PACING, and both were reported from the field as *"Smiles
; gets stuck bouncing between two short vertical paths and almost never
; reaches the right half of the screen"*:
;
;   * THE TIE-BREAK WAS THE TRY ORDER. Every legal direction down an empty
;     corridor scores the same, `.try` takes the strictly-greatest, and
;     dd_tryo is up, left, down, right - so a tie always went UP and then
;     LEFT, which pins the demo to the top left of the slice. The order now
;     starts at a random one of the four, which is dd_gh_flee's trick;
;   * IT COULD ONLY SEE ONE TILE. A machine that scores the neighbouring tile
;     alone cannot tell a cleared corridor from a full one, so once the dots
;     near it were gone every way looked identical. It now adds up the food
;     in the DD_DEMOLOOK tiles beyond, which is what points it at the half of
;     the board it has not eaten yet - and at DD_DEMOFOOD a tile, four dots
;     outweigh the turning-round penalty, so it will go back for them.
; -----------------------------------------------------------------------------
DD_DEMOLOOK equ 10              ; tiles the demo looks down a corridor
DD_DEMOFOOD equ 14              ; ...and what each dot in there is worth

dd_demo_think:
    push si
    push di
    xor si, si
    call dd_at_tile
    jnc .out
    call dd_tile_of
    mov [dd_dc], cx
    mov word [dd_best], 0
    mov byte [dd_bestd], DIR_NONE
    call OSAPI_RAND                 ; where the try order starts, so a tie is
    and ax, 3                       ; not always the same direction
    mov [dd_dstart], ax
    xor di, di
.try:
    mov bx, di
    add bx, [dd_dstart]
    and bx, 3
    mov al, [dd_tryo + bx]
    mov [dd_trydir], al
    push ax
    mov cx, [dd_dc]
    call dd_can_go
    pop ax
    jnc .next
    call dd_demo_worth               ; AX = how much it likes that way
    cmp ax, [dd_best]
    jbe .next
    mov [dd_best], ax
    mov al, [dd_trydir]
    mov [dd_bestd], al
.next:
    inc di
    cmp di, 4
    jb .try
    mov al, [dd_bestd]
    cmp al, DIR_NONE
    je .out
    mov [dd_want + 0], al
.out:
    pop di
    pop si
    ret

; dd_demo_worth - AX = what the tile in direction [dd_trydir] is worth to a
;                 machine playing Smiles: a dot is worth having and a ghost is
;                 worth not walking into
dd_demo_worth:
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, [dd_trydir]
    xor bh, bh
    mov cx, [dd_dc]
    add cl, [dd_dx + bx]
    add ch, [dd_dy + bx]
    mov [dd_dc2], cx
    xor di, di                      ; DI = the running score
    call dd_tile
    cmp al, TT_DOT
    je .food
    cmp al, TT_PILL
    jne .nofood
.food:
    add di, 80
.nofood:
    ; ...and the food in the DD_DEMOLOOK tiles BEYOND it, which is the whole
    ; of how it finds a half of the board it has not been to
    mov si, [dd_dc2]
    mov cx, DD_DEMOLOOK
.look:
    mov ax, si
    add al, [dd_dx + bx]
    add ah, [dd_dy + bx]
    mov si, ax
    push cx
    mov cx, si
    call dd_tile
    pop cx
    cmp al, TT_WALL
    je .looked
    cmp al, TT_DOOR
    je .looked
    cmp al, TT_DOT
    je .lookfood
    cmp al, TT_PILL
    jne .looknext
.lookfood:
    add di, DD_DEMOFOOD
.looknext:
    loop .look
.looked:
    ; how far the nearest ghost that could eat him is from that tile
    mov dx, 12                      ; capped: past a dozen tiles they are all
    mov si, 1                       ; equally far away
.g:
    mov bx, si
    dec bx
    mov al, [dd_gs + bx]
    cmp al, GS_EYES
    je .gnext
    cmp al, GS_FRIGHT
    je .gnext                       ; a blue one is not a danger, it is lunch
    call dd_tile_of
    mov al, cl
    sub al, byte [dd_dc2]
    cbw
    or ax, ax
    jns .ax
    neg ax
.ax:
    mov bx, ax
    mov al, ch
    sub al, byte [dd_dc2 + 1]
    cbw
    or ax, ax
    jns .ay
    neg ax
.ay:
    add ax, bx                      ; the walking distance, near enough
    cmp ax, dx
    jae .gnext
    mov dx, ax
.gnext:
    inc si
    cmp si, DD_NACT
    jb .g
    mov ax, dx
    mov bx, 6
    mul bx
    add di, ax
    ; ...and it would rather not turn round
    mov bl, [dd_dir + 0]
    cmp bl, DIR_NONE
    je .norev
    xor bh, bh
    mov al, [dd_rev + bx]
    cmp al, [dd_trydir]
    jne .norev
    sub di, 60
    jns .norev
    xor di, di
.norev:
    mov ax, di
    inc ax                          ; never zero: zero is "nothing chosen"
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_attract_draw - the parts of the title screen that change
; -----------------------------------------------------------------------------
dd_attract_draw:
    cmp byte [dd_blinkd], 0
    je .noblink
    mov byte [dd_blinkd], 0
    call dd_play_line
.noblink:
    cmp byte [dd_strip], 0
    je .out
    call dd_pill_refresh
    call dd_actors_draw
    call dd_rep_run                 ; ...AND THE TILES THEY LEFT (SPEC.md
                                    ; 93.5.10). The demo is the same cast
                                    ; through the same dd_actors_draw, so it
                                    ; has the same one-pen band and queues the
                                    ; same tiles - and the drain was on the
                                    ; PLAY path only, so nothing on the title
                                    ; screen ever put one back. The field saw
                                    ; it as "the attract screen still has most
                                    ; of the bugs we have been fixing".
.out:
    ret

; -----------------------------------------------------------------------------
; dd_attract_all - the whole title screen
; -----------------------------------------------------------------------------
dd_attract_all:
    mov ax, [dd_atr0]
    push ax
    call dd_attract_layout          ; the positions here are ABSOLUTE, so a
    pop ax                          ; window that MOVED needs them again
    cmp ax, [dd_atr0]
    je .laid
    call dd_demo_reset              ; ...and a slice that changed size needs
.laid:                              ; the grid decoded against the new one
    call dd_title_draw
    call dd_table_draw
    call dd_play_line
    cmp byte [dd_strip], 0
    je .out                         ; no playfield: the demo still runs, and
    call dd_walls_blit              ; nothing of it reaches the glass
    call dd_dots_blit
    call dd_pills_blit
    call dd_actors_draw
.out:
    ret

; -----------------------------------------------------------------------------
; dd_attract_layout - where the four blocks go, and how big the title may be
;
; The strip is what gives way when the surface is short: a windowed CGA has 136
; rows of content and the table and the title are what the screen is FOR.
; -----------------------------------------------------------------------------
dd_attract_layout:
    ; The title cell comes from the WIDTH and is then REDUCED until the whole
    ; page fits the height. Sizing it from the width alone put a CGA's table
    ; below the window and its band 48 bytes past dd_tband, which is a score
    ; table with four billion in it.
    mov ax, [dd_cw]
    sub ax, 8
    mov bx, DD_TITLEN * DD_TGADV
    xor dx, dx
    div bx
    or ax, ax
    jnz .cw1
    mov ax, 1
.cw1:
    cmp ax, 10
    jbe .cw2
    mov ax, 10
.cw2:
    mov [dd_tcw], ax
.fit:
    ; the cell HEIGHT follows the pixel aspect: a CGA pixel is 2.4x taller
    ; than it is wide, so a square cell there is FEWER rows, not more
    mov ax, [dd_tcw]
    mov bx, 100
    mul bx
    div word [dd_asp]
    or ax, ax
    jnz .ch1
    mov ax, 1
.ch1:
    mov [dd_tch], ax
    mov bx, DD_TGH
    mul bx
    mov [dd_tith], ax
    ; ...and what the page then needs: title, table, play line and their gaps
    add ax, DD_ATFIX                ; ...and what the page then needs: the
    cmp ax, [dd_ch]                 ; three text blocks and the four gaps at
                                    ; their minimum
    jbe .fits
    cmp word [dd_tcw], 1
    jbe .fits                       ; as small as it goes: it will be cut, and
    dec word [dd_tcw]               ; a cut title is better than no table
    jmp short .fit
.fits:
    mov ax, [dd_tcw]
    mov bx, DD_TITLEN * DD_TGADV - 1
    mul bx
    mov [dd_titw], ax

    ; HOW MANY TILE ROWS THE STRIP GETS, decided BEFORE anything is placed:
    ; eleven on a VGA, six in a CGA fullscreen bracket, and on a windowed CGA -
    ; where the table and the title are what the screen is FOR - as few as
    ; four. Fewer than that is not a playfield and the demo would run unseen.
    mov ax, [dd_ch]
    sub ax, [dd_tith]
    sub ax, DD_ATFIX
    jle .nostrip
    xor dx, dx
    div word [dd_th]
    cmp ax, DD_ATRMIN
    jb .nostrip
    cmp ax, DD_ATROWS
    jbe .rows
    mov ax, DD_ATROWS
.rows:
    push ax
    mul word [dd_th]
    mov [dd_striph], ax
    call dd_attract_place           ; ...and only now the four y positions
    call dd_attract_bdx             ; ...and the board's own x
    pop ax
    mov bx, DD_ROWS
    sub bx, ax
    mov [dd_atr0], bx               ; the slice is the BOTTOM of the board
    mov byte [dd_strip], 1
    mov ax, bx
    mul word [dd_th]
    mov [dd_clipy0], ax             ; the board's top edge, as far as dd_blit
    mov bx, [dd_stripy]             ; is concerned
    sub bx, ax
    mov [dd_bdy], bx                ; ...and the origin that puts it there
    ret
.nostrip:
    mov word [dd_striph], 0
    call dd_attract_place
    call dd_attract_bdx
    mov byte [dd_strip], 0
    mov word [dd_clipy0], 0
    mov word [dd_atr0], DD_ROWS     ; nothing is playable: the grid is all wall
    ret

; -----------------------------------------------------------------------------
; dd_attract_bdx - the strip's x, centred on the CONTENT BOX
;
; dd_layout puts the board's x where it goes when the HUD column is beside it,
; and on a surface too narrow to hold both side by side it packs the pair and
; centres THEM - which pushes the board right by half the HUD's width. There
; is no HUD on the title screen, so that margin is reserved against nothing
; and the mini-board sat visibly right of everything above it. Centring it on
; the content box is the whole fix; dd_level_begin calls dd_layout, which puts
; the playing value back.
; -----------------------------------------------------------------------------
dd_attract_bdx:
    push ax
    mov ax, [dd_cw]
    sub ax, [dd_mw]
    jns .ok
    xor ax, ax
.ok:
    shr ax, 1
    add ax, [dd_cx]
    add ax, 4                       ; to the NEAREST byte column, not down to
    and ax, ~7                      ; the one below: gfx_blit1 wants the grid
    mov [dd_bdx], ax                ; and rounding one way is a lean
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_attract_place - the four vertical positions, with the SLACK SPREAD
;                    (SPEC.md 93.11.4)
;
; in:  [dd_tith] and [dd_striph], the two blocks whose height is not fixed
; out: dd_tity / dd_taby / dd_ply / dd_stripy.  Preserves every register.
;
; Everything used to be packed against the top of the content box and the
; strip took whatever was left - so on a surface with room to spare the page
; sat in the top two thirds with a band of black under it. A fifth of the
; slack goes above the title, a fifth under it, a fifth under the strip, and
; what is left is split EQUALLY either side of the play line, which is what
; "press enter equally between the high scores and the mini-board" asks for.
; -----------------------------------------------------------------------------
dd_attract_place:
    push ax
    push bx
    push cx
    push dx
    mov ax, [dd_ch]                 ; the slack: what the surface has over what
    sub ax, [dd_tith]               ; the page needs at its minimum gaps
    sub ax, [dd_striph]
    sub ax, DD_ATFIX
    jns .have
    xor ax, ax
.have:
    mov bx, ax
    xor dx, dx
    mov cx, 5
    div cx
    mov cx, ax                      ; CX = a fifth of it
    mov ax, bx                      ; ...and the rest, less the three fifths
    sub ax, cx                      ; that are spent, joins the two middle gaps
    sub ax, cx
    sub ax, cx
    add ax, DD_ATGM
    shr ax, 1                       ; BX = ONE of them, so they are equal
    mov bx, ax
    mov ax, [dd_cy]
    add ax, DD_ATTOP
    add ax, cx
    mov [dd_tity], ax
    add ax, [dd_tith]
    add ax, DD_ATG1
    add ax, cx
    mov [dd_taby], ax               ; the table
    add ax, (DD_NHS + 1) * DD_HUDROW
    add ax, bx
    mov [dd_ply], ax                ; the play line, midway
    add ax, DD_HUDROW
    add ax, bx
    mov [dd_stripy], ax             ; ...and the strip, with a fifth under it
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_title_draw - DOT DELIRIUM, seven bands
; -----------------------------------------------------------------------------
dd_title_draw:
    mov ax, [dd_titw]
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [dd_rb], ax                 ; the band stride, one grid row's worth
    mul word [dd_tch]               ; ...and a CEILING on the pair, because the
    cmp ax, DD_TBANDSZ              ; arithmetic above is not the only place
    jbe .band_ok                    ; either of them can come from
    ret
.band_ok:
    mov word [dd_bbase], dd_tband
    mov word [dd_grow], 0
.row:
    ; a clean band
    mov ax, [dd_rb]
    mul word [dd_tch]
    mov cx, ax
    cmp cx, DD_TBANDSZ
    jbe .fits
    mov cx, DD_TBANDSZ
.fits:
    mov di, dd_tband
    xor al, al
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    ; every glyph's cells for this grid row
    mov word [dd_gcol], 0
.glyph:
    mov bx, [dd_gcol]
    mov al, [dd_s_title + bx]
    or al, al
    jz .emit
    call dd_glyph_row               ; stamps its set cells into the band
    inc word [dd_gcol]
    jmp short .glyph
.emit:
    mov al, DD_INKTITL              ; THE PEN FIRST, and then the geometry: AL
    call dd_pen                     ; is half of the destination x, and setting
                                    ; the ink into it is how the title landed
                                    ; a few hundred pixels left of the window
    push ds
    pop es
    mov si, dd_tband
    mov bp, [dd_rb]
    mov ax, [dd_grow]
    mul word [dd_tch]
    mov bx, ax
    add bx, [dd_tity]
    mov ax, [dd_cw]
    sub ax, [dd_titw]
    shr ax, 1
    add ax, [dd_cx]
    add ax, 4                       ; gfx_blit1 wants the byte grid, and the
    and ax, ~7                      ; NEAREST column rather than the one below
    mov cx, [dd_titw]
    mov dx, [dd_tch]
    call OSAPI_GFX_BLIT1            ; ABSOLUTE screen coordinates: the title is
                                    ; not on the board, so dd_blit's board
                                    ; clipping is the wrong clipping for it
    inc word [dd_grow]
    cmp word [dd_grow], DD_TGH
    jb .row
    ret

; dd_glyph_row - stamp glyph [dd_gcol] of the title's grid row [dd_grow]
dd_glyph_row:
    push ax
    push bx
    push cx
    push dx
    push si
    ; AL = the character; find its 7-row grid
    call dd_glyph_of                ; SI = the glyph's rows, or 0 for a space
    or si, si
    jz .out
    mov bx, [dd_grow]
    mov al, [si + bx]               ; the row's five cells, in bits 4..0
    or al, al
    jz .out
    mov [dd_grbits], al             ; NOT in AH: the two multiplies below both
    mov cx, 0                       ; write it, so the second cell of every
.cell:                              ; glyph row was reading a product
    mov al, [dd_grbits]
    push cx
    mov bx, DD_TGW - 1
    sub bx, cx
    mov cl, bl
    shr al, cl
    pop cx
    test al, 1
    jz .next
    ; a cell: one rectangle, in band coordinates
    push cx
    mov ax, [dd_gcol]
    mov bx, DD_TGADV
    mul bx
    add ax, cx
    mul word [dd_tcw]
    mov cx, [dd_tcw]
    xor bx, bx
    mov dx, [dd_tch]
    call dd_band_rect
    pop cx
.next:
    inc cx
    cmp cx, DD_TGW
    jb .cell
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dd_glyph_of - SI = the seven grid rows for the character in AL (0 = blank)
dd_glyph_of:
    push bx
    push cx
    mov si, dd_glyphs
    mov cx, DD_NGLYPH
.f:
    cmp al, [si]
    je .hit
    add si, DD_TGH + 1
    loop .f
    xor si, si
    pop cx
    pop bx
    ret
.hit:
    inc si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_table_draw - the saved scores
; -----------------------------------------------------------------------------
dd_table_draw:
    push si
    mov ax, [dd_taby]
    mov [dd_hrow], ax
    mov ax, [dd_cw]
    sub ax, DD_TABW
    shr ax, 1
    add ax, [dd_cx]
    add ax, 4
    and ax, ~7
    mov [dd_tabx], ax
    mov si, dd_s_hshead             ; the HEADING is centred over the table,
    call dd_text_w                  ; where the rows are a column and have to
    mov bx, DD_TABW                 ; line up with each other. It used to be
    sub bx, ax                      ; three spaces in the string
    shr bx, 1
    add bx, [dd_tabx]
    mov dx, [dd_hrow]
    mov al, DD_INKTXT
    call dd_text
    mov ax, [dd_hrow]
    add ax, DD_HUDROW
    mov [dd_hrow], ax
    xor di, di
.row:
    call dd_hs_row_str              ; dd_rowbuf = "n. AAA   nnnnnn"
    mov si, dd_rowbuf
    call dd_tab_line
    inc di
    cmp di, DD_NHS
    jb .row
    pop si
    ret

dd_tab_line:
    push ax
    push bx
    push cx
    push dx
    mov bx, [dd_tabx]
    mov dx, [dd_hrow]
    mov al, DD_INKTXT
    call dd_text
    mov ax, [dd_hrow]
    add ax, DD_HUDROW
    mov [dd_hrow], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_play_line - PRESS ENTER TO PLAY, lit or dark as [dd_blink] says
;
; The dark phase draws the line in the BACKGROUND colour rather than skipping
; it: FONT_RUN is opaque, so one call puts the line up and the same call takes
; it down, and there is no rectangle to remember and erase.
; -----------------------------------------------------------------------------
dd_play_line:
    push ax
    push cx
    push dx
    push si
    mov si, dd_s_play               ; ...and no DDS_ENTER arm: the initials are
                                    ; typed on the BOARD now (SPEC.md 93.12.4),
                                    ; so the attract screen never sees that
                                    ; state at all
    mov al, DD_INKPLAY
    cmp byte [dd_blink], 0
    jne .ink
    mov byte [dd_tblank], 1         ; blanked, not black: see dd_text
.ink:
    push ax
    call dd_text_w
    mov bx, [dd_cw]
    sub bx, ax
    shr bx, 1
    add bx, [dd_cx]
    mov dx, [dd_ply]
    pop ax
    call dd_text
    pop si
    pop dx
    pop cx
    pop ax
    ret
; --- the title's own type -----------------------------------------------------
; One byte of character then seven rows of five cells, bit 4 leftmost.
DD_NGLYPH equ 9
dd_glyphs:
    db 'D', 11110b, 10001b, 10001b, 10001b, 10001b, 10001b, 11110b
    db 'O', 01110b, 10001b, 10001b, 10001b, 10001b, 10001b, 01110b
    db 'T', 11111b, 00100b, 00100b, 00100b, 00100b, 00100b, 00100b
    db 'E', 11111b, 10000b, 10000b, 11110b, 10000b, 10000b, 11111b
    db 'L', 10000b, 10000b, 10000b, 10000b, 10000b, 10000b, 11111b
    db 'I', 11111b, 00100b, 00100b, 00100b, 00100b, 00100b, 11111b
    db 'R', 11110b, 10001b, 10001b, 11110b, 10100b, 10010b, 10001b
    db 'U', 10001b, 10001b, 10001b, 10001b, 10001b, 10001b, 01110b
    db 'M', 10001b, 11011b, 10101b, 10101b, 10001b, 10001b, 10001b

dd_s_title: db 'DOT DELIRIUM', 0
