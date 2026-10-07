; =============================================================================
; os8088 - apps/dotdel/ddrend.inc
;
; THE RENDERER (SPEC.md 93.5). Five bands a frame and nothing else moves.
;
; --- WHAT IS IN THE PICTURE, AND WHAT IS NOT ---------------------------------
;
; `dd_bd` holds the WALLS and only the walls. It was going to hold the dots as
; well - the board is one picture, so one blit puts it up - and that is wrong
; for a reason that is worth writing down, because it is the same reason twice:
; **a band is put down in ONE pen**, so every pixel in one has to be the same
; colour. Walls and dots in one picture is walls and dots in one colour, and on
; a VGA that is a blue board with blue dots on it.
;
; So the dots are not a picture at all. They live in `dd_grid`, one byte a
; tile, and they are STAMPED into whichever band needs them:
;
;   * an ACTOR's band stamps the two or three tiles it overlaps, which is
;     cheaper than reading them out of a bitmap would have been;
;   * a WHOLE-BOARD repaint walks each tile row and puts down one band per RUN
;     of adjacent dot tiles - about eighty bands for the classic board, ~36 ms -
;     and a run is safe to blit opaquely because two adjacent corridor tiles
;     have no wall between them to erase.
;
; That leaves the walls as the only thing big enough to want a bitmap, and one
; `gfx_blit1` a half puts the whole board up.
;
; --- THE ACTOR BAND ----------------------------------------------------------
;
; The band is the UNION of where the actor was drawn and where it is now,
; rounded out to the byte grid, with the sprite OR'd in at the bit offset its x
; has landed on. Drawing it IS the erase (SPEC.md 79.5): there is no instant at
; which the sprite is off the glass, no second call to be pre-empted between,
; and no erase pass to get the order of.
;
; A move too big for that band is a TELEPORT - the tunnel, a new life, the
; start of a level - and is drawn as two operations instead: put the tile back
; where the actor was, then draw it where it is.
; =============================================================================

DD_HUDROW equ 10                ; the HUD's line pitch, px
DD_HUDGAP equ 10                ; ...and the gap BETWEEN groups, which is a
DD_HUDGAPMIN equ 4              ; whole blank line where a row's own leading is
                                ; 2px: a label and its number are a PAIR and
                                ; read as one, and the field asked for the
                                ; pairs to be told apart. dd_layout drops to
                                ; the MIN when the column would not fit, which
                                ; is the small-window CGA case - it has 44px
                                ; of room under the lives and the wide gap
                                ; wants 18 of them

; The pen each actor draws in on a colour adapter. Index 0 is Smiles.
dd_ink:    db CYELLOW, CRED, CLMAGENTA, CLCYAN, CBROWN
DD_INKDOT  equ CWHITE
DD_INKWALL equ CBLUE
DD_INKFRI  equ CLBLUE           ; frightened - and CWHITE on the flash
DD_INKEYE  equ CWHITE
DD_INKFRU  equ CLRED
DD_INKTXT  equ CWHITE

; -----------------------------------------------------------------------------
; dd_pen - AL = ink, on black paper, and NOT SET AT ALL on a 1bpp adapter
; in:  AL = ink. Preserves every register.
;
; SPEC.md 5.4.2.2: the pen is ignored on one plane, where a band already means
; lit and unlit. Skipping it there is not tidiness - it is ~78 us a band, five
; bands a frame, on the two adapters that have the least to spare.
; -----------------------------------------------------------------------------
dd_pen:
    cmp byte [dd_bpp], 1
    jbe .out
    push ax
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
    pop ax
.out:
    ret

; =============================================================================
; TEXT (SPEC.md 93.5.5)
;
; Every line this package letters goes down as ONE BAND, composed out of the
; kernel's own 8x8 glyph table (`OSAPI_FONT_GLYPHS`) and put up with one
; `gfx_blit1`.
;
; THE REASON THIS COMMENT USED TO GIVE WAS WRONG, and the correction is worth
; more than the decision. It said `font_run` could not reach SPEC.md 6.1's
; single-store path on a VGA and cost 176 ms for the attract screen's
; nineteen-character play line. That figure was never measured - it was
; inherited from two sentences in this tree that described the pre-6.1.10 world
; in the present tense - and it is about THIRTY TIMES out.
;
; Measured in situ, the same string at the same place with the same pen, on a
; cycle-accurate 4.77 MHz 8088 (PERFORMANCE.md Set 144):
;
;                          band, as first written   band, cell-outer   font_run
;   VGA 640x480                        12.882 ms           4.887 ms    5.422 ms
;   Hercules 720x348                   12.969              4.988       5.663
;   CGA 640x200                        12.986              5.000       5.797
;
; So the band as first written was 2.4x SLOWER than the slot it replaced, and
; it is kept at a margin of 10-14% rather than an order of magnitude. What it
; buys beyond that is that text is the same one-`gfx_blit1` operation as
; everything else here draws, at a width the 8x8 run cannot compose.
;
; It is not load-bearing. `font_run` at an aligned x is a defensible choice and
; SPEC.md 6.1.10.1 is the rule that decides whether a PEN reaches its fast path
; - the plane bits of one colour must be a subset of the other's, which black
; paper always satisfies, and every pen in this package is on black.
;
; SO WHAT WOULD THE SWAP GIVE BACK? It was asked for as a SIZE question with a
; floor on it - take it if it is worth a kilobyte - and the answer is measured
; rather than guessed. `dd_font_get`, `dd_text`'s composer and the eight bss
; fields that serve them, replaced by one `OSAPI_FONT_RUN`:
;
;   the image      19,654 -> 19,386 bytes      -268
;   .bss           the glyph table's four fields and the line's four   -13
;   the .o88 on a floppy, lz4    14,156 -> 13,930                     -226
;
; ~281 bytes of RAM and 226 of disk, which is a quarter of the floor it was
; given, and 226 bytes is NO CLUSTERS on any geometry this ships on. A further
; 48 come off `DD_BANDSZ` - text at DD_TEXTMAX is 272 bytes and is what pushes
; the shared band past the 224 of SPEC.md 93.5.3's dot run - and 329 is still a
; third of a kilobyte.
;
; The PERFORMANCE half of the question is affordable and is not what decides
; it: nothing here letters per frame. The HUD draws on [dd_hudd], the title
; once, the panel on [dd_boxd], so the 10-14% above is paid when the score
; changes rather than 18.2 times a second. There is no argument against the
; swap except that it does not reach the bar it was set, and the composer is
; kept because it is already written and already measured.
; =============================================================================

; -----------------------------------------------------------------------------
; dd_font_get - bank the kernel's glyph table, once
; -----------------------------------------------------------------------------
dd_font_get:
    push ax
    push cx
    push dx
    push si
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL/AH = its range
    mov [dd_gseg], dx
    mov [dd_goff], si
    mov [dd_gfirst], al
    mov [dd_glast], ah
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_text - the NUL string at SI, in ink AL, at (BX, DX) in ABSOLUTE screen
;           coordinates. BX is rounded DOWN to the byte grid.
; Preserves every register. A string over DD_TEXTMAX cells is cut.
; -----------------------------------------------------------------------------
DD_TEXTMAX equ 34

dd_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [dd_tink], al
    and bx, ~7                      ; gfx_blit1 wants the byte grid, and a line
    mov [dd_tx], bx                 ; of type nudged left by up to seven pixels
    mov [dd_ty], dx                 ; is a line of type
    ; --- how many cells, capped --------------------------------------------
    xor cx, cx
    push si
.len:
    cmp byte [si], 0
    je .have
    inc si
    inc cx
    cmp cx, DD_TEXTMAX
    jb .len
.have:
    pop si
    or cx, cx
    jnz .go
    jmp .out                        ; a near jump: the unrolled cell below puts
.go:                                ; .out past a jcxz's reach
    mov [dd_tn], cx
    cmp byte [dd_tblank], 0
    je .glyphs
    ; --- BLANKED, which is not the same as drawn in the paper colour --------
    ; A 1bpp band has no pen at all (SPEC.md 5.4.2.2): a set bit is lit and a
    ; clear one is not, so `dd_text` with ink CBLACK still lights every glyph
    ; pixel. That is why "PRESS ENTER TO PLAY" was solid on a Hercules instead
    ; of blinking - the off phase was drawing the same letters in a colour the
    ; adapter does not have.
    mov byte [dd_tblank], 0
    mov ax, [dd_tn]
    mov cl, 3
    shl ax, cl
    mov cx, ax
    push es
    push ds
    pop es
    mov di, dd_band
    xor al, al
    cld
    rep stosb
    pop es
    jmp .emit
.glyphs:
    ; --- eight rows of CX bytes, out of the glyph table ---------------------
    ;
    ; CELL-OUTER, ROW-INNER, and the eight stores unrolled. It was the other
    ; way round, which made the whole lookup - two bounds compares, a shift and
    ; two adds - happen EIGHT TIMES a character instead of once, and cost 393
    ; cycles a band byte. The tell that the cost was the COMPOSER and never the
    ; blit is that a nineteen-cell line read 12.88 ms on a VGA, 12.97 on a
    ; Hercules and 12.99 on a CGA - three adapters whose blits differ by a
    ; factor of two, agreeing to 0.9% (SPEC.md 93.5.5).
    mov ax, [dd_gseg]
    mov es, ax
    mov di, dd_band                 ; DI = this cell's column, in band row 0
    mov dx, [dd_tn]                 ; DX = the band's stride, one byte a cell
    mov cx, dx
.cell:
    mov al, [si]
    inc si
    cmp al, [dd_gfirst]
    jb .blank
    cmp al, [dd_glast]
    ja .blank
    sub al, [dd_gfirst]
    xor ah, ah
    push cx
    mov cl, 3
    shl ax, cl                      ; eight bytes a glyph
    pop cx
    add ax, [dd_goff]
    mov bx, ax
    push di
    mov al, [es:bx]
    mov [di], al
    add di, dx
    mov al, [es:bx+1]
    mov [di], al
    add di, dx
    mov al, [es:bx+2]
    mov [di], al
    add di, dx
    mov al, [es:bx+3]
    mov [di], al
    add di, dx
    mov al, [es:bx+4]
    mov [di], al
    add di, dx
    mov al, [es:bx+5]
    mov [di], al
    add di, dx
    mov al, [es:bx+6]
    mov [di], al
    add di, dx
    mov al, [es:bx+7]
    mov [di], al
    pop di
    inc di
    loop .cell
    jmp short .emit
.blank:
    push cx                         ; a character the table does not carry, so
    push di                         ; the cell is ground and nothing else
    mov cx, 8
    xor al, al
.bz:
    mov [di], al
    add di, dx
    loop .bz
    pop di
    pop cx
    inc di
    loop .cell
.emit:
    ; --- ...and one call ----------------------------------------------------
    mov al, [dd_tink]
    call dd_pen
    push ds
    pop es
    mov si, dd_band
    mov bp, [dd_tn]
    mov ax, [dd_tx]
    mov bx, [dd_ty]
    mov cx, [dd_tn]
    mov dx, cx
    mov cl, 3
    shl dx, cl                      ; the width in pixels
    mov cx, dx
    mov dx, 8
    call OSAPI_GFX_BLIT1
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; PUTTING A BAND DOWN
; =============================================================================

; -----------------------------------------------------------------------------
; dd_blit - ES:SI = a band of ours, BP = stride, AX/BX = BOARD x/y, CX = width
;           in pixels, DX = rows. Clips to the board, translates to the screen.
; in:  AX must be a multiple of 8. Preserves every register.
; out: nothing - a refusal (kern_small has no body, SPEC.md 5.4.2.5) is
;      swallowed, because the honest degrade for a moving sprite is to leave
;      the frame alone rather than letter something in its place.
; -----------------------------------------------------------------------------
dd_blitp:                           ; ...the same clip, and SI = plane 0 of
    mov byte [dd_bplan], 1          ; FOUR, [dd_pn] apart (SPEC.md 93.5.19).
    call dd_blit                    ; out CF = 1: gfx_blitp refused, and
    mov byte [dd_bplan], 0          ; nothing was drawn. A MOV writes no flag
    ret
dd_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or cx, cx
    jle .none
    or dx, dx
    jle .none
    ; --- clip to the board in x, by whole byte columns -----------------------
    or ax, ax
    jge .noleft
    mov di, ax
    neg di                          ; DI = pixels off the left
    add cx, ax
    jle .none
    push cx
    mov cl, 3
    shr di, cl
    pop cx
    add si, di                      ; ...which is DI/8 band bytes skipped
    xor ax, ax
.noleft:
    mov di, ax
    add di, cx
    cmp di, [dd_mw]
    jbe .noright
    mov cx, [dd_mw]
    sub cx, ax
    jle .none
.noright:
    ; --- clip to the board in y ---------------------------------------------
    ; ...against [dd_clipy0] and not against zero: the attract screen shows a
    ; SLICE of the board (SPEC.md 93.11.2), and it says so by moving that word
    ; and the origin together rather than by having its own coordinates.
    cmp bx, [dd_clipy0]
    jge .notop
    mov di, [dd_clipy0]
    sub di, bx                      ; rows above the slice
    sub dx, di
    jle .none
    push ax
    push dx                         ; ...and MUL writes DX, which is the ROW
    mov ax, di                      ; COUNT: without saving it the band arrives
    mul bp                          ; nought rows tall and gfx_blit1 refuses it -
    add si, ax                      ; silently, because a refusal here is swallowed
    pop dx
    pop ax
    mov bx, [dd_clipy0]
.notop:
    mov di, bx
    add di, dx
    cmp di, [dd_mh]
    jbe .nobot
    mov dx, [dd_mh]
    sub dx, bx
    jle .none
.nobot:
    cmp dx, 255
    jbe .hok
    mov dx, 255                     ; gfx_blit1 takes 1..255 rows
.hok:
    add ax, [dd_bdx]                ; ...and only now into screen coordinates
    add bx, [dd_bdy]
    cmp byte [dd_bplan], 0
    jne .planes
    call OSAPI_GFX_BLIT1
    jmp short .out
.planes:
    mov di, [dd_pn]                 ; the plane step
    call OSAPI_GFX_BLITP            ; ...whose CF is the answer: the pops
    jmp short .out                  ; below write no flag
.none:
    clc                             ; clipped away is DRAWN, not refused
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE WALL PICTURE, ON THE GLASS
; =============================================================================

; -----------------------------------------------------------------------------
; dd_walls_blit - the whole wall picture, in halves (gfx_blit1 takes 255 rows)
; Clobbers everything.
; -----------------------------------------------------------------------------
dd_walls_blit:
    mov al, DD_INKWALL
    call dd_pen
    xor bx, bx
.half:
    mov dx, [dd_mh]
    sub dx, bx
    jle .out
    cmp dx, 200
    jbe .last
    mov dx, 200
.last:
    push bx
    push dx
    mov ax, bx
    mul word [dd_sb]
    mov si, ax
    mov ax, [dd_bdseg]
    mov es, ax
    xor ax, ax
    mov cx, [dd_mw]
    mov bp, [dd_sb]
    pop dx
    pop bx
    push bx
    push dx
    call dd_blit
    pop dx
    pop bx
    add bx, dx
    cmp bx, [dd_mh]
    jb .half
.out:
    ret

; =============================================================================
; DOTS
; =============================================================================

; -----------------------------------------------------------------------------
; dd_dots_blit - every dot still on the board, one band per RUN of adjacent
;                dot tiles in a tile row
; Clobbers everything. ~36 ms for the classic board.
; -----------------------------------------------------------------------------
dd_dots_blit:
    mov al, DD_INKDOT
    call dd_pen
    mov byte [dd_gr], 0
.rows:
    mov byte [dd_gc], 0
.scan:
    ; find the start of a run
    mov cl, [dd_gc]
    cmp cl, DD_COLS
    jae .nextrow
    mov ch, [dd_gr]
    call dd_dotish
    jnc .adv
    mov al, [dd_gc]
    mov [dd_runa], al               ; the run's first column
.grow:
    inc byte [dd_gc]
    mov cl, [dd_gc]
    cmp cl, DD_COLS
    jae .emit
    mov ch, [dd_gr]
    call dd_dotish
    jc .grow
.emit:
    call dd_run_emit
    jmp short .scan
.adv:
    inc byte [dd_gc]
    jmp short .scan
.nextrow:
    inc byte [dd_gr]
    cmp byte [dd_gr], DD_ROWS
    jb .rows
    ret

; dd_dotish - CF = 1 when the tile at CL,CH carries a DOT. A pellet is not one:
; it is taller than the dot row a run's band is, and there are four of them, so
; each is put down whole by dd_tile_put instead.
dd_dotish:
    call dd_tile
    cmp al, TT_DOT
    je .yes
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; dd_pills_blit - the four pellets, each as its own tile
;
; IT READS A LIST AND DOES NOT WALK THE BOARD, and that is not tidiness: the
; blink turns over three times a second, and a walk of all 868 tiles to find
; four of them measured at 45 ms a turn - 11 ms of every frame amortised, and
; the whole of why a 4.77 MHz CGA ran at 12 fps against the tick's 18.2
; (SPEC.md 93.5.3). With the list it is four tile blits and the same machine
; holds 18.2.
; -----------------------------------------------------------------------------
dd_pills_blit:
    push si
    xor si, si
.each:
    cmp si, [dd_piln]
    jae .out
    mov cl, [dd_pilc + si]
    mov ch, [dd_pilr + si]
    call dd_tile_put
    inc si
    jmp short .each
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_pills_flip - the blink, as one RECT FILL a pellet (SPEC.md 93.5.7)
;
; A pellet tile holds nothing but the pellet, so turning it over is the
; pellet's own rectangle in ink or in black - and that is a quarter of what
; putting the whole tile back costs. dd_tile_put composes a 16x13 band and
; blits it, which on a VGA is 3.48 ms; four of those is 14.05 ms of a 54.9 ms
; tick, landing on one frame in five, and the frame did not fit. A fill is the
; 756 us arrival and near enough nothing else.
;
; It draws the same picture because it draws the same rectangle: dd_tile_put's
; pellet arm is dd_band_rect at exactly this offset and size, and everything
; else in the tile is the black it already is. An actor crossing the tile is
; not this routine's problem either way - its band redraws the tile off the
; grid, in whichever phase is current.
; -----------------------------------------------------------------------------
dd_pills_flip:
    push si
    mov al, CBLACK
    cmp byte [dd_pilon], 0
    je .pen
    mov al, DD_INKDOT
.pen:
    call OSAPI_SET_COLOR
    xor si, si
.each:
    cmp si, [dd_piln]
    jae .out
    mov cl, [dd_pilc + si]
    mov ch, [dd_pilr + si]
    call dd_tile
    cmp al, TT_PILL
    jne .next                       ; EATEN. The list is where the pellets ARE
                                    ; and the grid is whether they are still
                                    ; there: dd_eat_tile clears the tile, the
                                    ; eater's own band puts black over it, and
                                    ; this used to letter it straight back in
                                    ; on the next phase - a pellet that gave
                                    ; its fright and its 50 points and then
                                    ; blinked away for the rest of the board
    mov al, [dd_pilc + si]
    xor ah, ah
    mul word [dd_tw]
    mov di, ax                      ; DI = the tile's board x
    mov al, [dd_pilr + si]
    xor ah, ah
    mul word [dd_th]
    mov bx, ax                      ; BX = ...and its board y
    mov ax, [dd_tw]
    sub ax, [dd_pilw]
    shr ax, 1
    add ax, di                      ; AX = the pellet's own left edge
    mov cx, [dd_th]
    sub cx, [dd_pilh]
    shr cx, 1
    add bx, cx                      ; BX = ...and its top
    mov cx, [dd_pilw]
    mov dx, [dd_pilh]
    push si
    call dd_fill_board
    pop si
.next:
    inc si
    jmp short .each
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_fill_board - fill AX,BX size CX,DX in BOARD pixels, in the colour already
;                 set. Clobbers AX/BX/CX/DX/DI.
;
; dd_blit's clipping, for a rect rather than a band - including [dd_clipy0],
; because the attract screen shows a SLICE of the board and says so by moving
; that word and the origin together (SPEC.md 93.11.2).
; -----------------------------------------------------------------------------
dd_fill_board:
    or cx, cx
    jle .out
    or dx, dx
    jle .out
    or ax, ax
    jge .noleft
    add cx, ax
    jle .out
    xor ax, ax
.noleft:
    mov di, ax
    add di, cx
    cmp di, [dd_mw]
    jbe .noright
    mov cx, [dd_mw]
    sub cx, ax
    jle .out
.noright:
    cmp bx, [dd_clipy0]
    jge .notop
    mov di, [dd_clipy0]
    sub di, bx
    sub dx, di
    jle .out
    mov bx, [dd_clipy0]
.notop:
    mov di, bx
    add di, dx
    cmp di, [dd_mh]
    jbe .nobot
    mov dx, [dd_mh]
    sub dx, bx
    jle .out
.nobot:
    add ax, [dd_bdx]                ; ...and only now into screen coordinates
    add bx, [dd_bdy]
    add cx, ax
    dec cx                          ; gfx_fill takes the far corner, inclusive
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
.out:
    ret

; -----------------------------------------------------------------------------
; dd_run_emit - one band for the run of dot tiles [dd_runa] .. [dd_gc]-1 of
;               tile row [dd_gr]
; Clobbers everything but [dd_gc] / [dd_gr] / [dd_runa].
;
; The band is the run's tiles WHOLE - dots sit at a tile centre, so rounding
; their extent out to the byte grid lands exactly on the tile boundaries the
; run already owns, and two adjacent corridor tiles have no wall between them
; for an opaque band to erase.
; -----------------------------------------------------------------------------
dd_run_emit:
    mov al, [dd_runa]
    xor ah, ah
    mul word [dd_tw]
    mov [dd_rx], ax                 ; the band's board x, on the byte grid
    mov al, [dd_gc]
    xor ah, ah
    sub al, [dd_runa]
    sbb ah, 0
    mul word [dd_tw]
    mov [dd_rw], ax                 ; ...and its width, a whole number of tiles
    mov al, [dd_gr]
    xor ah, ah
    mul word [dd_th]
    mov bx, [dd_th]
    sub bx, [dd_doth]
    shr bx, 1
    add ax, bx
    mov [dd_ry], ax                 ; the dot row inside the tile row

    ; compose: one band, [dd_doth] rows of ceil(rw/8) bytes
    mov ax, [dd_rw]
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [dd_rb], ax                 ; the band's stride
    mul word [dd_doth]
    mov cx, ax
    cmp cx, DD_BANDSZ
    jbe .fits
    mov cx, DD_BANDSZ
.fits:
    mov word [dd_bbase], dd_band
    mov di, dd_band
    xor al, al
    push es
    push ds
    pop es
    cld
    rep stosb                       ; a clean band
    pop es

    mov al, [dd_runa]
.each:
    push ax
    mov [dd_gt], al
    ; this tile's dot, in band coordinates
    xor ah, ah
    sub al, [dd_runa]
    sbb ah, 0
    mul word [dd_tw]
    mov bx, [dd_tw]
    sub bx, [dd_dotw]
    shr bx, 1
    add ax, bx                      ; AX = the dot's x inside the band
    mov bx, ax                      ; BX = the dot's x inside the band
    mov ax, bx
    xor bx, bx                      ; y = 0: the band IS the dot row
    mov cx, [dd_dotw]
    mov dx, [dd_doth]
    call dd_band_rect
.next:
    pop ax
    inc al
    cmp al, [dd_gc]
    jb .each

    push ds
    pop es
    mov si, dd_band
    mov bp, [dd_rb]
    mov ax, [dd_rx]
    mov bx, [dd_ry]
    mov cx, [dd_rw]
    mov dx, [dd_doth]
    call dd_blit
    ret

; -----------------------------------------------------------------------------
; dd_band_rect - set a CX x DX rectangle at (AX, BX) inside dd_band
; in:  AX = x, BX = y, CX = w, DX = h, [dd_rb] = the band's stride
; Preserves every register.
; -----------------------------------------------------------------------------
dd_band_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or cx, cx
    jle .out
    or dx, dx
    jle .out
    ; --- THE FIRST ROW'S BASE, ONCE (SPEC.md 93.5.9) ------------------------
    ; It was a 16-bit MUL and four push/pop pairs PER ROW - about 230 cycles
    ; of the ~400 a row of a four-pixel dot cost - for a base that advances by
    ; exactly one stride each time.
    push ax
    push dx                         ; MUL WRITES DX, WHICH IS THE ROW COUNTER.
    mov ax, bx                      ; The old code banked it per row and this
    mul word [dd_rb]                ; one has to bank it once - dropping the
    mov di, [dd_bbase]              ; push turned the height into the product's
    add di, ax                      ; high word, so `dec dx / jnz` walked the
    mov si, [dd_rb]                 ; whole segment. It is the same catastrophe
    pop dx                          ; dd_band_build's own comment describes
    pop ax
    ; --- AND THE RUN'S BYTES, ONCE: every row of a RECTANGLE is the same ----
    ; dd_bandrun rebuilt the identical mask for every row of a dot. Two bytes
    ; hold any run this game asks for - a dot and a pellet are a few pixels -
    ; and anything wider still has the general walk below.
    mov bx, ax
    and bx, 7                       ; BX = the bit offset into the first byte
    add bx, cx
    cmp bx, 16
    ja .slow                        ; wider than two bytes: the general walk,
                                    ; which finds its own first byte
    sub bx, cx                      ; ...the offset again
    shr ax, 1                       ; AND THE BYTE COLUMN, which dd_bandrun
    shr ax, 1                       ; does for itself and this path has to do
    shr ax, 1                       ; too. Without it every rectangle landed at
    add di, ax                      ; the LEFT EDGE of its band instead of at
                                    ; its x: the title's cells all piled into
                                    ; byte column 0, and every dot was drawn in
                                    ; the band's leftmost tile - so dots went
                                    ; missing where they belonged and turned up
                                    ; inside the corners the band also covered
    mov ax, 0FFFFh
    neg cl
    add cl, 16                      ; CL = 16 - the width
    shl ax, cl                      ; the width's worth of high bits...
    mov cl, bl
    shr ax, cl                      ; ...slid to the offset: AH is the first
    or al, al                       ; byte's and AL the second's
    jz .one
.two:
    or [di], ah
    or [di+1], al
    add di, si
    dec dx
    jnz .two
    jmp short .out
.one:
    or [di], ah                     ; it fits in ONE byte, and [di+1] may be
    add di, si                      ; the row after's - so it is not touched
    dec dx
    jnz .one
    jmp short .out
.slow:
    call dd_bandrun
    add di, si
    dec dx
    jnz .slow
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_bandrun - set CX bits from x = AX in the band row based at DI
; Preserves every register.
; -----------------------------------------------------------------------------
dd_bandrun:
    push ax
    push bx
    push cx
    push dx
    push di
    mov dx, cx                      ; DX = bits still to lay
    or dx, dx
    jle .done
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add di, bx                      ; DI = the first byte
    mov bl, al
    and bl, 7                       ; BL = the bit offset into it
.byte:
    mov al, 8
    sub al, bl                      ; AL = bits this byte can still take
    xor ah, ah
    cmp ax, dx
    jbe .n
    mov ax, dx                      ; ...capped by what is left
.n:
    mov ch, al                      ; CH = n, 1..8
    mov cl, 8
    sub cl, ch
    mov al, 0FFh
    shl al, cl                      ; n high bits...
    mov cl, bl
    shr al, cl                      ; ...slid to where they belong
    or [di], al
    xor ah, ah
    mov al, ch
    sub dx, ax
    jle .done
    inc di
    xor bl, bl                      ; every byte after the first starts left
    jmp short .byte
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_tile_put - put tile (CL, CH) back on the glass exactly as the board says
;               it should look: its own ground, with its dot or pellet on it
; Clobbers everything BUT SI. This is the one "undo whatever was drawn there"
; primitive - a blinking pellet, a vanished fruit, a score that has been read.
;
; THE GROUND IS NOT ALWAYS BLACK, and it took the repair queue to find that
; out. This routine was written for the pellet blink, where the tile is a
; corridor and a corridor held no wall ink at all (dd_walls_of draws every
; line OUTSIDE the tile it outlines, SPEC.md 93.2.1) - so zeros were the whole
; picture and nobody noticed the assumption. SPEC.md 93.5.10's queue then
; started handing it WALL tiles, because a turn puts the corner's wall tile in
; the actor's 2x2 band and the queue owes every tile that band has left. A
; zeroed band took the corner off the glass, which is exactly the defect
; dd_band_ground's wall arm was written to fix, one routine along: "corners
; are back to disappearing".
;
; IT IS NOT TRUE OF A CORRIDOR EITHER ANY MORE, and the field found that the
; same way: SPEC.md 93.2.3's CONCAVE round puts its corner block one line width
; INSIDE the corridor tile, so the pellet at a maze corner blinked its own
; corner away. The type test is gone - every tile takes the picture when there
; is one, which is the assumption that was wrong twice.
;
; SI IS PRESERVED, and that is a bug fix rather than a courtesy. `.emit` loads
; SI with the band's address, and dd_pills_blit walks the pellet list with SI
; as its counter - so the first pellet trampled the loop and the other three
; were never refreshed again. They did not merely stop blinking: the first
; actor to cross one composed it in the phase that was current, and if that
; was the dark half the pellet was gone for the rest of the board. The field
; report was "the dot in the upper left corner is blinking, the rest can be
; eaten but not seen", and the upper left is pellet 0.
; -----------------------------------------------------------------------------
dd_tile_put:
    push si
    push ax                         ; THE WALK'S OWN STATE IS A GLOBAL, and a
    mov al, [dd_gc]                 ; repair can land in the middle of one
    mov ah, [dd_gr]                 ; (SPEC.md 93.5.17): dd_board_render is
    push ax                         ; ~200 ms of walking [dd_gc]/[dd_gr], and
    mov al, [dd_gt]                 ; this routine sets both. Banking them is
    xor ah, ah                      ; four words against a walk that otherwise
    push ax                         ; jumps and leaves the maze half drawn
    mov ax, [dd_gx]
    push ax
    mov ax, [dd_gy]
    push ax
    mov [dd_gc], cl
    mov [dd_gr], ch
    call dd_tile
    mov [dd_gt], al
    call dd_tile_org
    ; a band the size of one tile
    mov ax, [dd_tw]
    mov cl, 3
    shr ax, cl
    mov [dd_rb], ax
    mov bx, [dd_th]
    mul bx
    cmp ax, DD_BANDSZ
    jbe .fits
    mov ax, DD_BANDSZ               ; a tile that would not FIT loses rows off
    xor dx, dx                      ; the bottom rather than the next variable
    div word [dd_rb]                ; along - dd_band_build's rule, and the
    mov bx, ax                      ; same reason
    mul word [dd_rb]
.fits:
    mov cx, ax
    mov word [dd_bbase], dd_band
    mov [dd_bh], bx                 ; ...which dd_band_walls counts down
    mov ax, [dd_gx]
    mov [dd_bx0], ax
    mov ax, [dd_gy]
    mov [dd_by0], ax
    mov byte [dd_bgnd], 0
    cmp word [dd_bdseg], 0          ; no picture claimed yet: black is all
    je .black                       ; there is to put back
    ; --- A CORNER BLOCK IN A CORRIDOR IS A PLANE OF ITS OWN (SPEC.md 93.5.19)
    ; ...when gfx_blitp may be asked. The tile then goes down ONCE, the dot in
    ; the dot's pen and the block in the wall's, where the one-pen band below
    ; put the block down white and blitted its rows again blue: a white corner
    ; for the length of a far call, every time a pellet on one blinked.
    cmp byte [dd_pok], 0
    je .walls
    mov al, [dd_gt]
    cmp al, TT_WALL                 ; a WALL or a DOOR is one pen already
    je .walls
    cmp al, TT_DOOR
    je .walls
    push cx
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    call dd_ink_ck
    pop cx
    jnc .walls
    mov [dd_pn], cx                 ; CX = the band's bytes, and a tile is far
    mov byte [dd_bgnd], 1           ; under DD_PBMAX
    push cx
    mov di, cx
    shl di, 1
    add di, cx
    add di, dd_pb                   ; plane 3's slot holds the ground...
    call dd_band_walls_di
    pop cx
    jmp short .black                ; ...and the band itself starts empty
.walls:
    call dd_band_walls              ; ...and otherwise the PICTURE, whatever
    jmp short .item                 ; the tile is (93.2.3.2)
.black:
    mov di, dd_band
    xor al, al
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
.item:
    mov al, [dd_gt]
    cmp al, TT_DOT
    je .dot
    cmp al, TT_PILL
    je .pill
    jmp short .emit
.dot:
    mov ax, [dd_tw]
    sub ax, [dd_dotw]
    shr ax, 1
    mov bx, [dd_th]
    sub bx, [dd_doth]
    shr bx, 1
    mov cx, [dd_dotw]
    mov dx, [dd_doth]
    call dd_band_rect
    jmp short .emit
.pill:
    cmp byte [dd_pilon], 0
    je .emit
    mov ax, [dd_tw]
    sub ax, [dd_pilw]
    shr ax, 1
    mov bx, [dd_th]
    sub bx, [dd_pilh]
    shr bx, 1
    mov cx, [dd_pilw]
    mov dx, [dd_pilh]
    call dd_band_rect
.emit:
    cmp byte [dd_bgnd], 1
    jne .onepen
    mov byte [dd_pink], DD_INKDOT
    mov ax, [dd_gx]
    mov bx, [dd_gy]
    mov cx, [dd_tw]
    mov dx, [dd_bh]
    call dd_emit_planar
    jnc .done                       ; drawn, block and all
    mov byte [dd_pok], 0            ; refused: the one-pen road, repair and all
    call dd_gnd_fold
.onepen:
    mov al, DD_INKDOT               ; THE PEN THE TILE'S CONTENT WANTS. It was
    mov ah, [dd_gt]                 ; always the dot's, which was true while
    cmp ah, TT_WALL                 ; the only caller was the pellet blink -
    je .pw                          ; and a repaired WALL came back white
    cmp ah, TT_DOOR
    jne .pen
.pw:
    mov al, DD_INKWALL
.pen:
    call dd_pen
    push ds
    pop es
    mov si, dd_band
    mov bp, [dd_rb]
    mov ax, [dd_gx]
    mov bx, [dd_gy]
    mov cx, [dd_tw]
    mov dx, [dd_th]
    call dd_blit
    ; --- and the CORNER BLOCK back in the WALL's pen (SPEC.md 93.2.3.2) -----
    ; The band is one pen, so the block 93.2.3's concave round put inside this
    ; corridor tile has just come back in the dot's. Two lookups settle it -
    ; is this tile marked, and which of its rows is the block in - and the
    ; band is already the picture, so the repair is a blit and not a re-read.
    ; The block is at a tile CORNER and a dot is centred, so a row that holds
    ; one never holds the other.
    mov al, [dd_gt]
    cmp al, TT_WALL
    je .done
    cmp al, TT_DOOR
    je .done
    cmp byte [dd_bpp], 1            ; ONE PLANE HAS NO PEN AT ALL (SPEC.md
    jbe .done                       ; 5.4.2.2), so the band .emit has just
                                    ; blitted is already the right pixels -
                                    ; dd_pen's own first test, and the two
                                    ; blits below would write them again
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    call dd_ink_ck
    jnc .done
    mov al, DD_INKWALL
    call dd_pen
    xor ax, ax                      ; the top [dd_lth] rows...
    call dd_cnr_rows
    mov ax, [dd_th]
    sub ax, [dd_lth]                ; ...and the bottom
    call dd_cnr_rows
.done:
    pop ax                          ; ...and putting them back is what makes
    mov [dd_gy], ax                 ; this safe from any task
    pop ax
    mov [dd_gx], ax
    pop ax
    mov [dd_gt], al
    pop ax
    mov [dd_gc], al
    mov [dd_gr], ah
    pop ax
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_cnr_rows - put the [dd_lth] band rows at offset AX back on the glass, in
;               whatever pen is armed. Nothing at all when they are blank,
;               which is the "which corner" half: a block is in one row of the
;               two and re-blitting the other costs a far call for no pixels.
; Clobbers everything but SI... which is why it is called after .emit's blit.
; -----------------------------------------------------------------------------
dd_cnr_rows:
    push ax
    mul word [dd_rb]
    mov si, dd_band
    add si, ax                      ; SI = the band's row
    mov cx, [dd_lth]
    mov ax, [dd_rb]
    mul cx
    mov cx, ax                      ; CX = the bytes in them
    mov di, si
    xor al, al
.blank:
    or al, [di]
    inc di
    loop .blank
    or al, al
    jz .none
    pop ax
    push ax
    add ax, [dd_gy]
    mov bx, ax
    push ds
    pop es
    mov bp, [dd_rb]
    mov ax, [dd_gx]
    mov cx, [dd_tw]
    mov dx, [dd_lth]
    call dd_blit
.none:
    pop ax
    ret

; dd_dmg_tile - the same, from the [dd_gc]/[dd_gr] the caller already set
dd_dmg_tile:
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    call dd_tile_put
    ret

; =============================================================================
; THE ACTOR BAND (SPEC.md 93.5.1)
;
; ONE BAND AN ACTOR, AND THE BAND IS THE WHOLE PICTURE UNDER IT. A band covers
; the tiles the actor's box was drawn on and the tiles it is on now, and it is
; composed from the bottom up: the wall picture's own bytes for that rectangle,
; then whatever the grid still says is lying on those tiles, then every actor
; whose box meets it. One gfx_blit1 puts the lot down, and there is no wipe
; pass and no restore pass - a band that already covers where the sprite was
; takes it up by drawing what was under it.
;
; Two things in that are bug fixes, and both are about what a band is ROUNDED
; to and what it is allowed to leave out.
;
; **A BAND IS CUT ON THE TILE GRID, NOT THE BYTE GRID.** gfx_blit1 only asks
; for a multiple of 8, and a band cut there reaches PART of the way into the
; tile ahead - so a dot in that tile was composed partly, which is a dot that
; changes shape as an actor comes near and changes back once it has gone. A
; tile is a multiple of 8 wide by construction (SPEC.md 93.3), so cutting on
; the tile grid costs nothing at all and makes every tile a band touches
; wholly inside it: a dot is drawn or it is not.
;
; **AND A BAND CARRIES EVERY ACTOR IT OVERLAPS.** Two ghosts that met used to
; eat each other - the second band is opaque, so it laid its own ground over
; the first one's pixels and the first put them back on the next frame, which
; is the flicker. Composing the other actor into this band instead means both
; are in every frame either of them draws. They share the band's one pen while
; they overlap, which on a colour adapter means the pair is briefly one
; colour; that is the honest price of a one-pen band, where being eaten is not.
;
; The wall picture is in the band for the same reason the dots are: a band is
; three tile columns wide when an actor's box straddles two boundaries, so the
; far column can be a wall tile, and blacking it would take that piece of wall
; off the glass for the rest of the level. Corridor tiles hold no wall ink at
; all (dd_walls_of draws every line OUTSIDE the corridor it outlines), so in
; the ordinary case those bytes are the zeros the band would have had anyway.
; =============================================================================

; =============================================================================
; THE REPAIR QUEUE (SPEC.md 93.5.10)
;
; A BAND GOES DOWN IN ONE PEN, and the tile an actor has LEFT keeps that pen
; for ever. The band covers where the actor was and where it is, so the
; vacated tile IS redrawn - in the actor's ink again - and once the actor is
; two tiles away no band covers it at all. Measured on a VGA after twelve
; seconds of play: 24 of 228 dots not white and 3 of 436 walls not blue, all
; of them the pink ghost's magenta, in a contiguous trail behind it. The
; field report was "note the trail of purple the purple ghost has left in his
; wake", and the correction that came with it is the point - the blend is a
; fine compromise WHILE the actor is over the tile, and what is wrong with it
; is that it is permanent.
;
; So a tile that falls out of an actor's band is queued, and a couple a frame
; are put back in the pen their own content wants. It is a QUEUE and not a
; repaint on the spot because the cost is real - a tile is ~2.5 ms on a VGA -
; and the latency is invisible: a tile that goes back to white one frame late
; is a tile the actor is still standing next to.
; =============================================================================

DD_REPN   equ 64                ; tiles the ring holds
DD_REPPF  equ 2                 ; ...and how many one frame puts back

; -----------------------------------------------------------------------------
; dd_rep_push - tile CL, CH owes a repaint. Preserves every register.
; -----------------------------------------------------------------------------
dd_rep_push:
    push ax
    push bx
    push dx
    mov dx, [dd_reph]
.scan:
    cmp dx, [dd_rept]               ; already owed? The ring is 64 and a frame
    je .add                         ; queues a handful, so the walk is cheaper
    mov bx, dx                      ; than the duplicate repaint would be
    and bx, DD_REPN - 1
    cmp [dd_repc + bx], cl
    jne .next
    cmp [dd_repr + bx], ch
    je .out
.next:
    inc dx
    jmp short .scan
.add:
    mov ax, [dd_rept]
    sub ax, [dd_reph]
    cmp ax, DD_REPN
    jae .out                        ; full: this one waits for the next actor
    mov bx, [dd_rept]               ; to cross it, which is a tile late rather
    and bx, DD_REPN - 1             ; than a tile wrong for ever
    mov [dd_repc + bx], cl
    mov [dd_repr + bx], ch
    inc word [dd_rept]
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_rep_covered - CF = 1 when tile CL, CH is within one of any actor
; A repaint there would take the actor off the glass for a frame, so it goes
; back on the queue instead. Preserves every register.
; -----------------------------------------------------------------------------
dd_rep_covered:
    push ax
    call dd_tile                    ; A WALL IS TESTED ON THE BOX, EVERYTHING
    cmp al, TT_WALL                 ; ELSE ON THE RING, and the split is the
    je .exact                       ; whole of what this costs.
    cmp al, TT_DOOR
    je .exact
    pop ax
    push ax
    push si                         ; the RING, for a dot or a pellet: a tile
    xor si, si                      ; within one of an actor is left for later,
.each:                              ; which is cheap because dd_rep_run then
    mov al, cl                      ; re-queues it INSTEAD OF DRAWING it. Most
    sub al, [dd_ac + si]            ; of the queue is Smiles' own trail and it
    cmp al, 1                       ; is in no hurry - 93.5.10's latency
    ja .next                        ; argument - so making this exact made the
    mov al, ch                      ; drain draw two tiles EVERY frame where it
    sub al, [dd_ar + si]            ; had drawn almost none, and a windowed VGA
    cmp al, 1                       ; frame has no room for 5 ms it did not
    jbe .yes                        ; have before: the soak read 78.0% of the
.next:                              ; tick against 96.3 before the corner work
    inc si
    cmp si, DD_NACT
    jb .each
    pop si
    pop ax
    clc
    ret
.yes:
    pop si
    pop ax
    stc
    ret
.exact:                             ; ...and a WALL is the corner 93.5.13 owes,
    pop ax                          ; which is beside the actor BY CONSTRUCTION
    call dd_tile_free               ; and so is deferred for ever by that ring.
    cmc                             ; The box is the question the ring was
    ret                             ; asking, and there are ~0.05 of these a
                                    ; frame against the trail's two

; -----------------------------------------------------------------------------
; dd_rep_run - put DD_REPPF of them back. Clobbers everything.
; -----------------------------------------------------------------------------
dd_rep_run:
    cmp byte [dd_bpp], 1
    jbe .out2                       ; ...and so nothing is ever queued there
    push si
    mov si, DD_REPPF
.each:
    mov ax, [dd_reph]
    cmp ax, [dd_rept]
    je .out
    mov bx, ax
    and bx, DD_REPN - 1
    mov cl, [dd_repc + bx]
    mov ch, [dd_repr + bx]
    inc word [dd_reph]
    call dd_rep_covered
    jnc .put
    call dd_rep_push                ; still under somebody: come back to it
    jmp short .next
.put:
    call dd_tile_put
.next:
    dec si
    jnz .each
.out:
    pop si
.out2:
    ret

; -----------------------------------------------------------------------------
; dd_rep_vacated - the tiles actor SI's band held last frame and does not now
; in:  SI = the actor, [dd_n*] its new rect. Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_rep_vacated:
    cmp byte [dd_bpp], 1
    jbe .none                       ; ONE PLANE HAS NO PEN AT ALL (SPEC.md
    push si                         ; 5.4.2.2), so a band there already means
    mov bx, si                      ; lit and unlit and nothing can be left in
                                    ; the wrong colour. Hercules and CGA would
                                    ; have paid two tile blits a frame for a
                                    ; defect they cannot have
    cmp byte [dd_qok + si], 0
    je .bank                        ; nothing was banked yet
    mov ch, [dd_qr0 + bx]
.row:
    mov cl, [dd_qc0 + bx]
.col:
    cmp cl, [dd_nc0 + bx]           ; still in the new rect? Then it is not
    jb .push                        ; vacated and the band has just redrawn it
    cmp cl, [dd_nc1 + bx]
    ja .push
    cmp ch, [dd_nr0 + bx]
    jb .push
    cmp ch, [dd_nr1 + bx]
    jbe .next
.push:
    call dd_tile                    ; AN EMPTY TILE IS BLACK IN EVERY PEN, so
    cmp al, TT_DOT                  ; it owes nothing - and that is most of
    je .owed                        ; them: it is exactly the tiles Smiles has
    cmp al, TT_PILL                 ; just EATEN, so his own trail is free.
    je .owed                        ; ...UNLESS 93.2.3's concave round put a
    cmp al, TT_EMPTY                ; CORNER BLOCK in it (93.2.3.2), which the
    jne .ground                     ; ghost house is ringed with
    call dd_ink_ck
    jnc .next
.ground:                            ; A WALL, A DOOR OR A CORNER BLOCK owes
    cmp byte [dd_qgk + si], 0       ; nothing when the band that left it drew
    jne .next                       ; it in the wall's pen (93.5.19)
.owed:
    call dd_rep_push
.next:
    inc cl
    cmp cl, [dd_qc1 + bx]
    jbe .col
    inc ch
    cmp ch, [dd_qr1 + bx]
    jbe .row
.bank:
    mov al, [dd_nc0 + bx]
    mov [dd_qc0 + bx], al
    mov al, [dd_nc1 + bx]
    mov [dd_qc1 + bx], al
    mov al, [dd_nr0 + bx]
    mov [dd_qr0 + bx], al
    mov al, [dd_nr1 + bx]
    mov [dd_qr1 + bx], al
    mov al, [dd_bok]
    mov [dd_qgk + si], al
    mov byte [dd_qok + si], 1
    pop si
.none:
    ret

; -----------------------------------------------------------------------------
; dd_actors_draw - the whole cast, one band each
; Two passes, because a band has to know where the OTHER actors are before it
; can carry them. Clobbers everything.
; -----------------------------------------------------------------------------
dd_actors_draw:
    push si
    xor si, si
.prep:
    call dd_actor_prep
    inc si
    cmp si, DD_NACT
    jb .prep
    xor si, si
.emit:
    call dd_actor_emit
    inc si
    cmp si, DD_NACT
    jb .emit
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_actor_prep - work out actor SI's band and bank it
; in:  SI = the actor. Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_actor_prep:
    mov byte [dd_utel + si], 0
    cmp byte [dd_alive + si], 0
    je .none
    call dd_actor_px                ; [dd_nx]/[dd_ny] now, [dd_px]/[dd_py] then
    ; --- has anything about it changed at all? ------------------------------
    cmp byte [dd_still], 0
    je .move
    cmp byte [dd_shown + si], 0
    je .move
    mov bx, si
    shl bx, 1
    mov ax, [dd_ox + bx]
    cmp ax, [dd_nx]
    jne .move
    mov ax, [dd_oy + bx]
    cmp ax, [dd_ny]
    jne .move
    mov al, [dd_img + si]
    cmp al, [dd_limg + si]
    jne .move
    mov byte [dd_uok + si], 2       ; same place, same picture: leave it there
    ret
.move:
    ; --- the pixel union of the two boxes -----------------------------------
    mov ax, [dd_nx]
    mov bx, [dd_px]
    cmp bx, ax
    jae .x0
    mov ax, bx
.x0:
    mov [dd_bx0], ax
    mov ax, [dd_nx]
    mov bx, [dd_px]
    cmp bx, ax
    jbe .x1
    mov ax, bx
.x1:
    add ax, [dd_tw]
    dec ax
    mov [dd_bx1], ax
    mov ax, [dd_ny]
    mov bx, [dd_py]
    cmp bx, ax
    jae .y0
    mov ax, bx
.y0:
    mov [dd_by0], ax
    mov ax, [dd_ny]
    mov bx, [dd_py]
    cmp bx, ax
    jbe .y1
    mov ax, bx
.y1:
    add ax, [dd_th]
    dec ax
    mov [dd_by1], ax
    ; --- ...taken out to the tile grid, WITHOUT DIVIDING (SPEC.md 93.5.14) --
    ; dd_advance carries dd_ac/dd_acx along with the position (SPEC.md 93.7),
    ; so the tile this actor is in and that tile's OWN pixel origin are both
    ; known before this routine starts - and every write to dd_x/dd_y in the
    ; program is either dd_advance's or dd_put_tile's, which is what makes
    ; that an invariant rather than a hope.  dd_actor_px has already refused
    ; a step longer than a tile as a TELEPORT, so the union reaches ONE tile
    ; back and TWO forward at most, and the range is three compares an axis
    ; where it was two 16-bit DIVs.  Four of those ran five times a frame at
    ; ~160 cycles each and were a quarter of dd_actor_prep
    ; (docs/reports/DOTDEL-FRAME-PROFILE-2026-09-09.md).
    mov bx, si
    shl bx, 1
    mov cl, 4
    mov di, [dd_acx + bx]
    shr di, cl                      ; DI = the pixel x of the tile SI is in
    mov dl, [dd_ac + si]            ; DL = the first column, DH the last
    mov dh, dl
    cmp [dd_bx0], di
    jae .c0
    dec dl                          ; the union opens in the tile BEFORE it
    jns .c0
    xor dl, dl                      ; column 0 cannot, its origin being 0 -
.c0:                                ; but a wrong band is a corrupted screen
    mov ax, [dd_bx1]
    add di, [dd_tw]
    cmp ax, di
    jb .ck
    inc dh
    add di, [dd_tw]
    cmp ax, di
    jb .ck
    inc dh
.ck:
    cmp dh, DD_COLS - 1
    jbe .ckok
    mov dh, DD_COLS - 1
.ckok:
    mov bx, si
    mov [dd_nc0 + bx], dl
    mov [dd_nc1 + bx], dh
    shl bx, 1                       ; --- and the same on y ------------------
    mov cl, 4
    mov di, [dd_ary + bx]
    shr di, cl
    mov dl, [dd_ar + si]
    mov dh, dl
    cmp [dd_by0], di
    jae .r0
    dec dl
    jns .r0
    xor dl, dl
.r0:
    mov ax, [dd_by1]
    add di, [dd_th]
    cmp ax, di
    jb .rk
    inc dh
    add di, [dd_th]
    cmp ax, di
    jb .rk
    inc dh
.rk:
    cmp dh, DD_ROWS - 1
    jbe .rkok
    mov dh, DD_ROWS - 1
.rkok:
    mov bx, si
    mov [dd_nr0 + bx], dl
    mov [dd_nr1 + bx], dh
    shl bx, 1                       ; ...and BANK the two boxes the union was
    mov ax, [dd_px]                 ; made of. dd_actor_px writes ONE pair and
    mov [dd_pbx + bx], ax           ; prep runs five times before the first
    mov ax, [dd_py]                 ; emit, so by then they are actor 4's -
    mov [dd_pby + bx], ax           ; and dd_box_rect on an unbanked zero is a
                                    ; band at tile (0,0) in the actor's pen,
                                    ; every frame, which is what the maze's
                                    ; top-left corner looked like when this
                                    ; block was left out
    mov ax, [dd_bx0]                ; ...and the PIXEL union itself, which is
    mov [dd_bux0 + bx], ax          ; what the band is CUT to (SPEC.md 93.5.15)
    mov ax, [dd_bx1]                ; rather than the tile rectangle above
    mov [dd_bux1 + bx], ax
    mov ax, [dd_by0]
    mov [dd_buy0 + bx], ax
    mov ax, [dd_by1]
    mov [dd_buy1 + bx], ax
.banked:                            ; ...and a name for the one moment BOTH
                                    ; halves are readable: the pixel union in
                                    ; dd_bx0/dd_bx1/dd_by0/dd_by1 and the tile
                                    ; range it was reduced to, before the next
                                    ; actor's prep overwrites the first pair.
                                    ; tests/dotdel.py leg J is what checks the
                                    ; divide-free reduction against a divide,
                                    ; and it emits no byte
    mov byte [dd_uok + si], 1
    ret
.none:
    mov byte [dd_uok + si], 0
    ret

; -----------------------------------------------------------------------------
; dd_actor_px - actor SI's box in whole px now, and where it was last DRAWN
; out: [dd_nx]/[dd_ny], [dd_px]/[dd_py], and [dd_utel + si] when the two are
;      too far apart to be one step. Clobbers AX/BX/CX/DX.
; -----------------------------------------------------------------------------
dd_actor_px:
    mov bx, si
    shl bx, 1
    mov ax, [dd_x + bx]
    mov cl, 4
    shr ax, cl
    mov [dd_nx], ax
    mov ax, [dd_y + bx]
    shr ax, cl
    mov [dd_ny], ax
    cmp byte [dd_shown + si], 0
    je .fresh
    mov ax, [dd_ox + bx]
    mov [dd_px], ax
    mov ax, [dd_oy + bx]
    mov [dd_py], ax
    mov ax, [dd_nx]                 ; a jump bigger than a tile is a TELEPORT -
    sub ax, [dd_px]                 ; the tunnel, a life lost, a new board - and
    jns .dxp                        ; not a step. Unioning it would make the
    neg ax                          ; band the width of the maze, so the old box
.dxp:                               ; is taken up by a band of its own instead.
    cmp ax, [dd_tw]
    ja .tele
    mov ax, [dd_ny]
    sub ax, [dd_py]
    jns .dyp
    neg ax
.dyp:
    cmp ax, [dd_th]
    jbe .out
.tele:
    mov byte [dd_utel + si], 1
.fresh:
    mov ax, [dd_nx]
    mov [dd_px], ax
    mov ax, [dd_ny]
    mov [dd_py], ax
.out:
    ret

; -----------------------------------------------------------------------------
; dd_actor_emit - compose actor SI's band and lay it down
; in:  SI = the actor. Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_actor_emit:
    cmp byte [dd_uok + si], 2
    je .out
    cmp byte [dd_uok + si], 0
    je .gone
    mov byte [dd_drewany], 1
    mov byte [dd_bok], 1            ; ...until a band puts a wall in its pen
    cmp byte [dd_utel + si], 0
    je .main
    mov byte [dd_utel + si], 0
    call dd_wipe_old                ; the far side of a teleport
.main:
    mov bx, si
    xor ah, ah
    mov al, [dd_nc0 + bx]
    mov [dd_c0], ax
    mov al, [dd_nc1 + bx]
    mov [dd_c1], ax
    mov al, [dd_nr0 + bx]
    mov [dd_r0], ax
    mov al, [dd_nr1 + bx]
    mov [dd_r1], ax
    mov bx, si
    shl bx, 1
    mov ax, [dd_bux0 + bx]          ; ...and the PIXEL rect inside it, which is
    mov [dd_ux0], ax                ; what is actually composed and blitted
    mov ax, [dd_bux1 + bx]          ; (SPEC.md 93.5.15)
    mov [dd_ux1], ax
    mov ax, [dd_buy0 + bx]
    mov [dd_uy0], ax
    mov ax, [dd_buy1 + bx]
    mov [dd_uy1], ax
    mov cl, 4
    mov ax, [dd_x + bx]
    shr ax, cl
    mov [dd_ox + bx], ax            ; where it is being DRAWN, which is what the
    mov ax, [dd_y + bx]             ; next frame's union is against
    shr ax, cl
    mov [dd_oy + bx], ax
    call dd_split_ck                ; is there a WALL in the union that no box
    jc .split                       ; of this actor's is on? (SPEC.md 93.5.13)
    call dd_band_actor
    jmp short .drawn
.split:
    mov bx, si
    shl bx, 1
    mov ax, [dd_pby + bx]
    push ax
    mov ax, [dd_pbx + bx]
    pop bx
    call dd_box_rect                ; where it WAS...
    call dd_band_actor
    mov bx, si
    shl bx, 1
    mov ax, [dd_oy + bx]            ; ...and where it IS - off dd_ox/dd_oy and
    push ax                         ; not dd_sx/dd_sy, which dd_band_actor has
    mov ax, [dd_ox + bx]            ; just handed to dd_band_others
    pop bx
    call dd_box_rect
    call dd_band_actor              ; The actor goes in BOTH, so the tile they
.drawn:                             ; share is drawn twice and identically
    call dd_rep_vacated             ; ...and the tiles it has LEFT owe their
    mov byte [dd_shown + si], 1     ; own pen back (SPEC.md 93.5.10)
    mov al, [dd_img + si]
    mov [dd_limg + si], al
    ret
.gone:
    cmp byte [dd_shown + si], 0
    je .out
    mov byte [dd_drewany], 1
    call dd_wipe_old
    mov byte [dd_shown + si], 0
.out:
    ret

; -----------------------------------------------------------------------------
; dd_wipe_old - lay down the tiles actor SI was last drawn on, WITHOUT it
; in:  SI = the actor. Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_wipe_old:
    mov bx, si
    shl bx, 1
    mov ax, [dd_oy + bx]
    push ax
    mov ax, [dd_ox + bx]
    pop bx
    call dd_box_rect
    call dd_band_build
    mov al, DD_INKDOT               ; nothing of this actor's is in it any more
    call dd_band_emit
    ret

; -----------------------------------------------------------------------------
; dd_box_rect - [dd_c0/c1/r0/r1] = the tile rect a ONE-TILE box at (AX, BX)
;               lies in, clamped to the board. Clobbers AX/BX/CX/DX.
;
; The TOP and LEFT are clamped as well as the bottom and right, and that is not
; belt and braces: clamping only the far edge is what makes r1 < r0, and a
; rectangle with a negative height is a band with a height of ZERO, which
; dd_band_build then has to refuse (see its own note).
; -----------------------------------------------------------------------------
dd_box_rect:
    push bx                         ; y
    push ax                         ; x
    push ax                         ; ...and the PIXEL rect with it (SPEC.md
    push bx                         ; 93.5.15), which for a ONE-TILE box needs
    mov [dd_ux0], ax                ; no divide at all: it IS the box
    add ax, [dd_tw]
    dec ax
    cmp ax, [dd_mw]
    jb .ux1
    mov ax, [dd_mw]
    dec ax
.ux1:
    mov [dd_ux1], ax
    mov [dd_uy0], bx
    add bx, [dd_th]
    dec bx
    cmp bx, [dd_mh]
    jb .uy1
    mov bx, [dd_mh]
    dec bx
.uy1:
    mov [dd_uy1], bx
    pop bx
    pop ax
    xor dx, dx
    div word [dd_tw]
    cmp ax, DD_COLS - 1
    jbe .c0
    mov ax, DD_COLS - 1
.c0:
    mov [dd_c0], ax
    pop ax
    add ax, [dd_tw]
    dec ax
    xor dx, dx
    div word [dd_tw]
    cmp ax, DD_COLS - 1
    jbe .c1
    mov ax, DD_COLS - 1
.c1:
    mov [dd_c1], ax
    pop ax                          ; y
    push ax
    xor dx, dx
    div word [dd_th]
    cmp ax, DD_ROWS - 1
    jbe .r0
    mov ax, DD_ROWS - 1
.r0:
    mov [dd_r0], ax
    pop ax
    add ax, [dd_th]
    dec ax
    xor dx, dx
    div word [dd_th]
    cmp ax, DD_ROWS - 1
    jbe .r1
    mov ax, DD_ROWS - 1
.r1:
    mov [dd_r1], ax
    ret

; -----------------------------------------------------------------------------
; dd_band_actor - build the band for [dd_c0..dd_c1] x [dd_r0..dd_r1], put actor
;                 SI in it at [dd_sx],[dd_sy], and blit it in the actor's pen
; Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_band_actor:
    call dd_band_build              ; walls, dots, everybody else
    mov bx, si                      ; [dd_sx]/[dd_sy] ARE dd_band_others' OWN
    shl bx, 1                       ; CURSOR - it walks the other four through
    mov cl, 4                       ; them - so this actor's position is put in
    mov ax, [dd_x + bx]             ; AFTER the build and never before it. Set
    shr ax, cl                      ; ahead of it, dd_band_one drew this actor
    mov [dd_sx], ax                 ; at the LAST OTHER ACTOR's position, which
    mov ax, [dd_y + bx]             ; is outside the band, so it clipped to
    shr ax, cl                      ; nothing and the whole cast went invisible
    mov [dd_sy], ax                 ; on every adapter
    mov al, [dd_img + si]
    call dd_band_one                ; ...and this actor over them
    mov al, [dd_inkof + si]
    call dd_band_emit
    ret

; -----------------------------------------------------------------------------
; dd_split_ck - CF = 1 when this band covers a WALL no box of the actor is on
;               (SPEC.md 93.5.13). in: the union in [dd_c0..dd_r1], SI = actor.
;               Clobbers AX/CX. Preserves everything else.
;
; A BAND GOES DOWN IN ONE PEN, so every tile in it wears the actor's ink while
; it is there - the honest price of SPEC.md 93.5.1 for a tile the actor is ON,
; and not honest at all for one it is merely NEAR. The union is of two boxes,
; and when they differ on BOTH axes the rectangle containing them has a fourth
; corner neither box touches. In a one-tile corridor that corner is the maze's
; own, so a ghost rounding a bend lit the wall corner in its colour.
;
; Three tests, cheapest first, and only the last one costs: ONE PLANE HAS NO
; PEN AT ALL (SPEC.md 5.4.2.2), so Hercules and CGA cannot have this defect
; and pay a single compare; a union spanning one axis has no fourth corner;
; and dd_rect_wall is the predicate dd_band_ground already takes for its
; ground, so asking it here walks no new tiles in the common case.
; -----------------------------------------------------------------------------
dd_split_ck:
    cmp byte [dd_bpp], 1
    jbe .no
    cmp byte [dd_pok], 0            ; ...and a band whose walls are a plane of
    jne .no                         ; their own draws them blue (93.5.19)
    mov ax, [dd_c1]
    cmp ax, [dd_c0]
    jbe .no
    mov ax, [dd_r1]
    cmp ax, [dd_r0]
    jbe .no
    call dd_rect_wall
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
dd_tile_free:
    push cx
    push si
    push di
    mov al, cl                      ; the tile's own pixel box
    xor ah, ah
    mul word [dd_tw]
    mov [dd_fx], ax
    mov al, ch
    xor ah, ah
    mul word [dd_th]
    mov [dd_fy], ax
    xor si, si
.each:
    cmp byte [dd_alive + si], 0
    je .next
    mov di, si
    shl di, 1
    mov ax, [dd_ox + di]            ; actor right > tile left?
    add ax, [dd_tw]
    cmp ax, [dd_fx]
    jbe .next
    mov ax, [dd_fx]                 ; tile right > actor left?
    add ax, [dd_tw]
    cmp ax, [dd_ox + di]
    jbe .next
    mov ax, [dd_oy + di]
    add ax, [dd_th]
    cmp ax, [dd_fy]
    jbe .next
    mov ax, [dd_fy]
    add ax, [dd_th]
    cmp ax, [dd_oy + di]
    jbe .next
    pop di                          ; somebody is on it
    pop si
    pop cx
    clc
    ret
.next:
    inc si
    cmp si, DD_NACT
    jb .each
    pop di
    pop si
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; dd_band_build - fill dd_band with the picture under the tile rectangle
;                 [dd_c0..dd_c1] x [dd_r0..dd_r1], minus actor SI
; out: [dd_bx0]/[dd_by0]/[dd_bw]/[dd_bh]/[dd_rb]. Clobbers all but SI.
; -----------------------------------------------------------------------------
dd_band_build:
    ; THE BAND IS THE PIXEL UNION, NOT THE TILE RECTANGLE (SPEC.md 93.5.15).
    ; Only x has to be on the byte grid, because that is gfx_blit1's contract;
    ; y was rounded out to a whole tile for no reason the blit has, and on a
    ; VGA that was 26 rows where 17 were owed. The tile range is still what
    ; the composers ITERATE - it is one rectangle out, and this one is cut
    ; from it.
    ; EVERY FIELD IS STORED BEFORE ANYTHING IS REFUSED. dd_band_actor calls
    ; dd_band_one and dd_band_emit whether this routine built a band or not,
    ; so a `.none` that returns early leaves the PREVIOUS band's base, stride
    ; and origin standing - and the actor is then composed into it and blitted
    ; where that one was. A degenerate rectangle becomes a zero here and the
    ; guards below refuse the zero, which is what the old tile arithmetic did
    ; by construction and this has to do on purpose.
    mov byte [dd_bgnd], 0           ; no ground until dd_band_ground says so
    mov ax, [dd_ux0]
    and ax, ~7                      ; ...out to the byte column each side
    mov [dd_bx0], ax
    mov bx, [dd_ux1]
    or bx, 7
    inc bx
    sub bx, ax
    jg .wok
    xor bx, bx
.wok:
    mov [dd_bw], bx
    mov cl, 3
    shr bx, cl
    mov [dd_rb], bx
    mov ax, [dd_uy0]
    mov [dd_by0], ax
    mov bx, [dd_uy1]
    sub bx, ax
    jge .hok
    mov bx, -1
.hok:
    inc bx
    mov [dd_bh], bx
    mov word [dd_bbase], dd_band
    ; A DEGENERATE RECTANGLE IS REFUSED HERE, and it has to be refused rather
    ; than clamped: dd_band_walls copies dd_bh rows with `dec dx / jnz`, so a
    ; height of ZERO is 65,536 rows of `rep movsb` walking the whole package
    ; segment - the bss, the code, everything - and what comes out the far side
    ; is a machine running on rubble. It read as STACK OVERFLOW because the
    ; rubble is what the worker then executed.
    mov ax, [dd_rb]                 ; ...and a band that would not FIT loses
    or ax, ax                       ; rows off the bottom rather than the next
    jz .none                        ; variable along
    cmp word [dd_bh], 0
    je .none
    mul word [dd_bh]
    cmp ax, DD_BANDSZ
    jbe .fits
    mov ax, DD_BANDSZ
    xor dx, dx
    div word [dd_rb]
    mov [dd_bh], ax
.fits:
    call dd_band_ground
    call dd_band_items
    call dd_band_others
.none:
    ret

; -----------------------------------------------------------------------------
; dd_band_ground - what is under the band before anything is stamped on it
;
; ZEROS, ALMOST ALWAYS - and the almost is a bug fix. The rectangle can only
; hold tiles one of the actor's two boxes was over, and a CORRIDOR tile holds
; no wall ink at all, because dd_walls_of draws every line OUTSIDE the corridor
; it outlines (SPEC.md 93.2.1). So the ground is a `rep stosw` of nothing.
;
; The exception is a TURN. The band is the union of where the actor was DRAWN
; and where it is now, and at a turn those differ on BOTH axes - one step back
; along the old direction, one step on along the new - so the rectangle is a
; 2x2 block of tiles whose diagonal member is the WALL tile in the corner. Its
; outline is the corner of the maze, dd_walls_of having extended both lines by
; the line thickness to close it, and a zeroed band took it off the glass for
; the rest of the level. The field report was exactly that: "corners disappear
; when a ghost or Smiles crosses them".
;
; So the tiles are walked first, and a rectangle holding a WALL or a DOOR takes
; its bytes out of the wall picture instead. That is one memcpy at a turn and
; none at all in a straight line - SPEC.md 93.5.3's item 4 is what copying it
; on EVERY band cost.
; Clobbers AX/BX/CX/DX/DI.
; -----------------------------------------------------------------------------
dd_band_ground:
    call dd_rect_wall
    jnc .zero
    mov byte [dd_bgnd], 2           ; ...folded into the band, in one pen
    cmp byte [dd_pok], 0
    je dd_band_walls
    mov ax, [dd_rb]                 ; ...or, when gfx_blitp may be asked, in a
    mul word [dd_bh]                ; PLANE OF ITS OWN (SPEC.md 93.5.19), so it
    cmp ax, DD_PBMAX                ; goes down in the wall's pen in the same
    ja dd_band_walls                ; write as the actor goes down in its own
    mov [dd_pn], ax
    mov byte [dd_bgnd], 1
    mov di, ax
    shl di, 1
    add di, ax
    add di, dd_pb                   ; plane 3's slot: composed LAST, so it can
    call dd_band_walls_di           ; hold the ground until then
.zero:
    mov ax, [dd_rb]
    mul word [dd_bh]
    mov cx, ax
    push es
    push ds
    pop es
    mov di, dd_band
    xor ax, ax
    cld
    shr cx, 1
    rep stosw
    jnc .even
    stosb
.even:
    pop es
    ret

; -----------------------------------------------------------------------------
; dd_rect_wall - CF = 1 when [dd_c0..dd_c1] x [dd_r0..dd_r1] holds a tile with
;                a picture on it. Clobbers AX/CX. Preserves SI.
;
; A WALL, A DOOR - or a corridor tile carrying one of 93.2.3's concave corner
; blocks, which is what [dd_cnrmap] is for. Without that third test a ghost
; circling the house took the house's four corners off the glass and the
; repair queue put them back without them: 6 of the board's 34 concave corners
; were gone inside a second and stayed gone.
; -----------------------------------------------------------------------------
dd_rect_wall:
    push bx
    push dx
    mov dx, [dd_r0]
.row:
    mov bx, [dd_c0]
.col:
    mov cl, bl
    mov ch, dl
    call dd_tile
    cmp al, TT_WALL
    je .yes
    cmp al, TT_DOOR
    je .yes
    call dd_ink_ck                  ; ...and a CORRIDOR can carry a corner block
    jc .yes                         ; of its own now (93.2.3.2)
    inc bx
    cmp bx, [dd_c1]
    jbe .col
    inc dx
    cmp dx, [dd_r1]
    jbe .row
    pop dx
    pop bx
    clc
    ret
.yes:
    pop dx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dd_band_walls - the wall picture's own bytes for the band's rectangle
; The band is on the tile grid and a tile is a multiple of 8 wide, so this is a
; row-by-row copy and never a shift. Clobbers everything but SI.
;
; dd_band_walls_di is the same into DI rather than dd_band, which is how the
; planar band puts its GROUND in a plane of its own (SPEC.md 93.5.19).
; -----------------------------------------------------------------------------
dd_band_walls:
    mov di, dd_band
dd_band_walls_di:
    cmp word [dd_bh], 0             ; belt and braces: `dec dx / jnz` on a zero
    je .none                        ; count is 65,536 rows (see dd_band_build)
    push si
    mov ax, [dd_by0]                ; (nothing from here to the copy writes DI)
    mul word [dd_sb]
    mov si, ax
    mov ax, [dd_bx0]
    mov cl, 3
    shr ax, cl
    add si, ax                      ; SI = the picture byte the rectangle is at
    mov dx, [dd_bh]
    mov bx, [dd_rb]
    mov ax, [dd_bdseg]
    push es
    push ds
    pop es                          ; ES:DI the band, DS:SI the wall picture
    mov ds, ax
    cld
.row:
    push si
    push di
    mov cx, bx
    rep movsb
    pop di
    pop si
    add si, [es:dd_sb]              ; DS is the picture now, so our own words
    add di, bx                      ; have to be read through ES
    dec dx
    jnz .row
    push es
    pop ds
    pop es
    pop si
.none:
    ret

; -----------------------------------------------------------------------------
; dd_band_items - the dots, pellets and fruit still lying on the band's tiles
; Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_band_items:
    ; THE TILE GRID NO LONGER STARTS AT THE BAND'S CORNER (SPEC.md 93.5.15).
    ; The band is cut to the pixel union, so tile (c0, r0) opens ABOVE and to
    ; the LEFT of it - both offsets are <= 0 - and an item on a tile the band
    ; only partly covers is clipped by dd_band_rect rather than skipped. Two
    ; multiplies once a band, where every row of every item used to carry one.
    mov ax, [dd_c0]
    mul word [dd_tw]
    sub ax, [dd_bx0]
    mov [dd_ix0], ax
    mov ax, [dd_r0]
    mul word [dd_th]
    sub ax, [dd_by0]
    mov [dd_iy0], ax
    mov ax, [dd_r0]
    mov [dd_ir], ax
    mov ax, [dd_iy0]
    mov [dd_iy], ax                 ; the tile's origin INSIDE the band, walked
.row:                               ; along rather than multiplied out
    mov ax, [dd_c0]
    mov [dd_ic], ax
    mov ax, [dd_ix0]
    mov [dd_ix], ax
.col:
    mov cl, [dd_ic]
    mov ch, [dd_ir]
    call dd_tile
    cmp al, TT_DOT
    je .dot
    cmp al, TT_PILL
    je .pill
    jmp short .next                 ; ...and a DOOR needs nothing here: the
.dot:                               ; ground came out of the wall picture
    mov ax, [dd_tw]
    sub ax, [dd_dotw]
    shr ax, 1
    mov bx, [dd_th]
    sub bx, [dd_doth]
    shr bx, 1
    mov cx, [dd_dotw]
    mov dx, [dd_doth]
    call dd_item_rect
    jmp short .next
.pill:
    cmp byte [dd_pilon], 0
    je .next
    mov ax, [dd_tw]
    sub ax, [dd_pilw]
    shr ax, 1
    mov bx, [dd_th]
    sub bx, [dd_pilh]
    shr bx, 1
    mov cx, [dd_pilw]
    mov dx, [dd_pilh]
    call dd_item_rect
.next:
    mov ax, [dd_tw]
    add [dd_ix], ax
    inc word [dd_ic]
    mov ax, [dd_c1]
    cmp [dd_ic], ax
    jbe .col
    mov ax, [dd_th]
    add [dd_iy], ax
    inc word [dd_ir]
    mov ax, [dd_r1]
    cmp [dd_ir], ax
    jbe .row
    ; ...and the fruit, which is a sprite rather than a rectangle
    cmp word [dd_fruit], 0
    je .out
    mov ax, DD_FRUITC
    cmp ax, [dd_c0]
    jb .out
    cmp ax, [dd_c1]
    ja .out
    mov ax, DD_FRUITR
    cmp ax, [dd_r0]
    jb .out
    cmp ax, [dd_r1]
    ja .out
    mov ax, DD_FRUITC
    mul word [dd_tw]
    mov [dd_sx], ax
    mov ax, DD_FRUITR
    mul word [dd_th]
    mov [dd_sy], ax
    mov al, SP_FRUIT
    call dd_band_one
.out:
    ret

; -----------------------------------------------------------------------------
; dd_item_rect - dd_band_rect with AX/BX given TILE-relative, for the tile the
;                walk is on. Clobbers AX/BX/CX/DX.
; -----------------------------------------------------------------------------
dd_item_rect:
    add ax, [dd_ix]
    add bx, [dd_iy]
    ; --- ...AND CLIPPED TO THE BAND (SPEC.md 93.5.15) -----------------------
    ; The band is the pixel union now, so a tile it only partly covers puts an
    ; item's rectangle over an edge. THIS is the routine that clips and not
    ; dd_band_rect, because the four other bands in this program - the dot
    ; run, the title's glyph grid, the HUD's - are built to fit what goes in
    ; them and do not all set dd_bh at all; a clip there would read a stale
    ; height. A rectangle is uniform, so nothing but the counts moves.
    push si
    push di
    or bx, bx
    jns .ytop
    add dx, bx                      ; rows above the band
    jle .none
    xor bx, bx
.ytop:
    mov di, bx
    add di, dx
    sub di, [dd_bh]
    jle .yok
    sub dx, di                      ; ...and rows below it
    jle .none
.yok:
    or ax, ax
    jns .xleft
    add cx, ax                      ; columns left of it
    jle .none
    xor ax, ax
.xleft:
    mov si, cx                      ; the width, while CL is the shift count
    mov di, [dd_rb]                 ; the band in PIXELS: dd_bw is not set by
    mov cl, 3                       ; dd_tile_put and dd_rb always is
    shl di, cl
    mov cx, si
    mov si, ax
    add si, cx
    sub si, di
    jle .xok
    sub cx, si                      ; ...and columns right of it
    jle .none
.xok:
    pop di
    pop si
    jmp dd_band_rect
.none:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_band_others - every actor but SI whose box meets this band
; Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_band_others:
    push si
    push di
    xor di, di
.each:
    cmp di, si
    je .next
    cmp byte [dd_alive + di], 0
    je .next
    mov bx, di
    shl bx, 1
    mov cl, 4
    mov ax, [dd_x + bx]
    shr ax, cl
    mov [dd_sx], ax
    mov ax, [dd_y + bx]
    shr ax, cl
    mov [dd_sy], ax
    mov ax, [dd_sx]                 ; its BOX, not its band: a band is a union
    add ax, [dd_tw]                 ; with where it WAS, and drawing that would
    cmp ax, [dd_bx0]                ; put a second copy of it down the trail
    jbe .next
    mov ax, [dd_bx0]
    add ax, [dd_bw]
    cmp ax, [dd_sx]
    jbe .next
    mov ax, [dd_sy]
    add ax, [dd_th]
    cmp ax, [dd_by0]
    jbe .next
    mov ax, [dd_by0]
    add ax, [dd_bh]
    cmp ax, [dd_sy]
    jbe .next
    mov al, [dd_img + di]
    call dd_band_one
.next:
    inc di
    cmp di, DD_NACT
    jb .each
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_band_one - OR image AL into the band at BOARD (dd_sx, dd_sy)
;
; The band is on the byte grid and the sprite is a whole number of bytes wide,
; so the only shift in the whole renderer is this one: the sprite's x modulo 8.
; A sprite composed in from a NEIGHBOURING band can hang off any edge, so every
; row is clipped and every one of the three byte columns is masked.
; Preserves every register.
; -----------------------------------------------------------------------------
dd_band_one:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    xor ah, ah
    mov bx, DD_SPRSZ
    mul bx
    mov si, dd_spr
    add si, ax                      ; SI = the scaled image
    ; --- rows ---------------------------------------------------------------
    mov cx, [dd_th]
    mov ax, [dd_sy]
    sub ax, [dd_by0]
    jns .ytop
    neg ax
    cmp ax, cx
    jae .out                        ; wholly above the band
    sub cx, ax
    mov bx, DD_SPRB
    mul bx
    add si, ax                      ; ...and those rows skipped in the image
    xor ax, ax
.ytop:
    cmp ax, [dd_bh]
    jae .out                        ; wholly below it
    mov [dd_srow], ax
    mov bx, [dd_bh]
    sub bx, ax
    cmp cx, bx
    jbe .rows
    mov cx, bx
.rows:
    ; --- columns ------------------------------------------------------------
    mov ax, [dd_sx]
    sub ax, [dd_bx0]
    mov bx, ax
    sar bx, 1
    sar bx, 1
    sar bx, 1                       ; floor(dx / 8), which is signed
    mov [dd_sbo], bx
    and ax, 7                       ; ...and its remainder, which is not
    mov [dd_ssh], al
    mov dl, 8
    sub dl, al
    mov [dd_sh2], dl
    mov dx, [dd_rb]
    xor al, al
    or bx, bx
    js .c1
    cmp bx, dx
    jge .c1
    or al, 1
.c1:
    inc bx
    or bx, bx
    js .c2
    cmp bx, dx
    jge .c2
    or al, 2
.c2:
    inc bx
    or bx, bx
    js .cm
    cmp bx, dx
    jge .cm
    or al, 4
.cm:
    or al, al
    jz .out                         ; wholly left or right of the band
    mov bh, al                      ; BH = which byte columns are in the band
    mov ax, [dd_srow]
    mul word [dd_rb]
    add ax, [dd_sbo]
    mov di, dd_band
    add di, ax                      ; DI = the band byte the first row starts at
    mov bp, [dd_rb]                 ; ...and its stride, out of memory
    ; BYTE-ALIGNED IS TWO STORES A ROW, and it is not a rare case: a tile is a
    ; multiple of 8 wide (SPEC.md 93.3), so an actor moving only in Y sits on
    ; a byte column for its whole journey and every ghost in a vertical
    ; corridor takes this path. The general row below is two variable shifts,
    ; three tests and three read-modify-writes.
    cmp byte [dd_ssh], 0
    jne .row
    mov al, bh
    and al, 3
    cmp al, 3
    jne .row                        ; ...and both of its columns are in
.arow:
    mov al, [si]
    or [di], al
    mov al, [si+1]
    or [di+1], al
    add si, DD_SPRB
    add di, bp
    loop .arow
    jmp .out
.row:
    push cx
    mov dh, [si]                    ; the sprite's row, always DD_SPRB bytes
    mov dl, [si+1]
    mov cl, [dd_ssh]
    mov ax, dx                      ; ONE 16-bit shift gives two of the three
    shr ax, cl                      ; bytes: AH leading, AL middle
    mov bl, dl
    mov cl, [dd_sh2]
    shl bl, cl                      ; ...and an 8-bit one the third
    test bh, 1
    jz .n0
    or [di], ah
.n0:
    test bh, 2
    jz .n1
    or [di+1], al
.n1:
    test bh, 4
    jz .n2
    or [di+2], bl
.n2:
    add si, DD_SPRB
    add di, bp
    pop cx
    loop .row
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_band_emit - AL = the pen, then the band
; -----------------------------------------------------------------------------
dd_band_emit:
    cmp byte [dd_bgnd], 0
    je .one
    cmp byte [dd_bgnd], 1
    jne .lost                       ; 2: the walls are in the band, in AL's pen
    mov [dd_pink], al               ; 1: THE GROUND IS A PLANE OF ITS OWN
    push ax                         ; (SPEC.md 93.5.19), so the band and the
    mov ax, [dd_bx0]                ; walls under it go down in one write, each
    mov bx, [dd_by0]                ; in its own pen
    mov cx, [dd_bw]
    mov dx, [dd_bh]
    call dd_emit_planar
    pop ax
    jnc .out
    mov byte [dd_pok], 0            ; refused: say so once, not every band
    call dd_gnd_fold                ; ...and put the walls in the band after all
    mov al, [dd_pink]               ; (the fold took AL: the pen is the band's)
.lost:
    mov byte [dd_bok], 0            ; a wall or a corner went down in AL's pen
.one:
    call dd_pen
    push ds
    pop es
    push si
    mov si, dd_band
    mov bp, [dd_rb]
    mov ax, [dd_bx0]
    mov bx, [dd_by0]
    mov cx, [dd_bw]
    mov dx, [dd_bh]
    call dd_blit
    pop si
.out:
    ret

; -----------------------------------------------------------------------------
; dd_emit_planar - compose the band and its GROUND into four planes and put
;                  them down with ONE gfx_blitp (SPEC.md 93.5.19)
; in:  [dd_pink] = the band's ink, AX/BX = board x (a multiple of 8) and y,
;      CX/DX = width and rows, [dd_rb] the stride, [dd_pn] one plane's bytes,
;      dd_band the band and plane 3's slot of dd_pb the ground.
; out: CF = 1 refused and NOTHING drawn - the caller still has the band, but
;      NOT the ground: plane 3 was composed over it (dd_gnd_fold rebuilds it).
;      Clobbers everything but SI.
;
; A BAND IS ONE PEN, AND A PEN IS A PROPERTY OF THE WRITE. gfx_blit1 says
; which colour a set bit is once for the whole band, so a wall pixel in an
; actor's band came out in the actor's colour for as long as the actor was
; there - 93.2.3.2's corner block most of all, because it sits inside the
; corridor tile an actor turns on. Four planes is what a colour IS on this
; card, so this band says each pixel's colour itself: the actor's ink where
; the band is set, the wall's where the ground is set and the band is not,
; black elsewhere. Every pixel is written once, in the colour it ends up.
;
;   plane k = (ink has k ? band : 0) | (wall has k ? ground & ~band : 0)
;
; which is one of four loops a plane, the band winning where the two overlap
; exactly as it did when the walls were OR'd into it. Plane 3 holds the
; ground until it is composed and is composed LAST, byte for byte in place.
;
; IT WAS TWENTY POINTS OF THE TICK until SPEC.md 5.4.3.5. gfx_blitp priced a
; colour per ROW - ~2,400 cycles a row against gfx_blit1's ~100 - so a whole
; band this way was 28,600 cycles against 6,200 and windowed VGA fell from 96%
; of the tick to 76%. The kernel now copies a block with nothing to decide per
; row plane-major, and a band here is ~8,500 against ~6,000: cheaper than the
; split it retires, which is why the windowed frame came out FASTER.
; -----------------------------------------------------------------------------
dd_emit_planar:
    push si
    push ax
    push bx
    push cx
    push dx
    push ds                         ; ES = DS from here on: the stores below,
    pop es                          ; and the planes are ours for the blit
    cld
    mov cx, [dd_pn]
    mov bx, cx
    shl bx, 1
    add bx, cx
    add bx, dd_pb - dd_band         ; BX = the ground, as an offset from a
                                    ; band byte: [bx+si-1] after a lodsb
    mov dl, [dd_pink]
    mov dh, DD_INKWALL
    mov di, dd_pb
    mov ah, 1                       ; this plane's bit
.pl:
    mov si, dd_band
    mov cx, [dd_pn]
    test dl, ah
    jnz .ink
    test dh, ah
    jnz .gnd
    xor al, al                      ; neither: black
    rep stosb
    jmp short .nx
.ink:
    test dh, ah
    jnz .both
    rep movsb                       ; the ink's alone: the band as it stands
    jmp short .nx
.both:
    lodsb                           ; both: wherever either is set
    or al, [bx+si-1]
    stosb
    loop .both
    jmp short .nx
.gnd:
    lodsb                           ; the wall's alone: the ground the band
    not al                          ; does not cover
    and al, [bx+si-1]
    stosb
    loop .gnd
.nx:
    shl ah, 1
    cmp ah, 16
    jb .pl
    pop dx
    pop cx
    pop bx
    pop ax
    mov si, dd_pb
    mov bp, [dd_rb]
    call dd_blitp
    pop si
    ret

; dd_gnd_fold - OR the ground back into the band: a refused planar band goes
; down in one pen after all, which is the picture it always was. The ground is
; BUILT AGAIN first: plane 3's slot is where it was parked and plane 3 is
; composed in place over it before gfx_blitp is asked, so by a refusal the slot
; holds the band or nothing - and a fold of that put the band down with no
; walls. [dd_bx0]/[dd_by0]/[dd_bh]/[dd_rb] still describe the band on both
; callers' paths. Clobbers everything but SI.
dd_gnd_fold:
    push si
    mov di, [dd_pn]
    mov ax, di
    shl di, 1
    add di, ax
    add di, dd_pb                   ; plane 3's slot, the ground's again
    call dd_band_walls_di
    mov cx, [dd_pn]
    mov bx, cx
    shl bx, 1
    add bx, cx
    add bx, dd_pb - dd_band
    mov si, dd_band
.b:
    mov al, [bx+si]
    or [si], al
    inc si
    loop .b
    pop si
    ret

; =============================================================================
; THE FRAME (SPEC.md 93.5.2)
; =============================================================================

; -----------------------------------------------------------------------------
; dd_render - one frame, windowed or in the bracket
; Clobbers everything. Takes the lock itself windowed; in a bracket the lock is
; already held for the whole session and there is no clip to arm.
; -----------------------------------------------------------------------------
dd_render:
    mov byte [dd_pok], 0
    cmp byte [dd_inbr], 0
    je .win
    cmp byte [dd_bpp], 1            ; the bracket arms no region, so a colour
    jbe .draw                       ; surface may take four planes (93.5.19)
    mov byte [dd_pok], 1
    jmp short .draw
.win:
    call OSAPI_GFX_LOCK
    mov ax, KERNEL_SEG              ; A WORKER MUST LOAD ES ITSELF - only a
    mov es, ax                      ; CALLBACK arrives with ES = KERNEL_SEG
    mov bx, [dd_win]
    test word [es:bx + W_FLAGS], 2  ; still visible?
    jz .skip
    call OSAPI_WM_CLIP_SET
    jc .skip                        ; not one pixel of us is showing
    call dd_pok_win                 ; ...and is anything OVER us? (93.5.19)
    ; --- WHERE ARE WE? ASKED EVERY FRAME (SPEC.md 93.3.4.2) ----------------
    ; NOTHING TELLS A PACKAGE ITS WINDOW MOVED. The kernel's drag cache banks
    ; the window's pixels and replays them at the new position (SPEC.md
    ; 11.96.12), so a drag calls no W_PAINT, no OSAPI_WM_ONRESIZE and no
    ; handler of ours at all - measured across a full drag as 0, 0 and 0, with
    ; [dd_cx]/[dd_cy] still naming where the window used to be.
    ;
    ; So the question is asked rather than waited for. It is three far calls,
    ; ~140 us on a frame that has 44 ms in it, and dd_relayout_ck answers in
    ; four compares when nothing moved.
    call dd_geom_win
    mov bl, 1                       ; ...but a RECUT is the UI task's, and
    call dd_relayout_ck             ; this is the worker (SPEC.md 93.3.4.3)
    jc .skip
    cmp byte [dd_inrender], 0       ; ...and a picture being WALKED is not one
    jne .skip                       ; to draw a frame off (SPEC.md 93.5.17)
.draw:
    mov byte [dd_drawing], 1        ; SPEC.md 93.5.17: a board walk waits for
    call dd_draw                    ; this to clear before it starts
    mov byte [dd_drawing], 0
    mov byte [dd_pok], 0            ; a W_PAINT's region is not ours to lift
    cmp byte [dd_inbr], 0
    jne .out
    call OSAPI_GFX_UNLOCK
    jmp short .out
.skip:
    mov byte [dd_pok], 0
    ; the step ran and the glass did not follow, so every "where it is drawn"
    ; word is now a claim about pixels nobody can see: the next frame that CAN
    ; draw owes a whole one
    mov byte [dd_full], 1
    call dd_forget
    call OSAPI_GFX_UNLOCK
.out:
    ret

; -----------------------------------------------------------------------------
; dd_pok_win - [dd_pok] for a WINDOWED draw, after OSAPI_WM_CLIP_SET has armed
;              the region. in: BX = the window. Preserves every register.
;
; gfx_blitp refuses ANY armed region - a plane byte carries eight x's, so it
; cannot honour a fragment edge (SPEC.md 5.4.3) - and a windowed draw always
; arms one. When nothing at all is over the window the region IS the content
; rect, which this program never draws outside, so disarming it changes no
; pixel and lets the walls have their own pen (SPEC.md 93.5.19). CLIP_SET has
; already done the rest of its job - the raise cache and the pointer's deferred
; hide - and disarming undoes neither. One far call, and only on a colour
; surface: one plane has no pen to get wrong.
; -----------------------------------------------------------------------------
dd_pok_win:
    mov byte [dd_pok], 0
    cmp byte [dd_bpp], 1
    jbe .out
    call OSAPI_WM_OBSCURED          ; covered, or the open dock is over it:
    jc .out                         ; the region binds, and so does the pen
    call OSAPI_WM_CLIP_CLEAR
    mov byte [dd_pok], 1
.out:
    ret

; dd_is_attract - CF = 1 when the title screen is what is showing, which is
;                 both the attract loop and the initials being typed over it
dd_is_attract:
    cmp byte [dd_state], DDS_ATTRACT
    je .yes
    clc
    ret
.yes:
    stc
    ret

; dd_is_box - CF = 1 while the GAME OVER / initials panel owns the screen
;             (SPEC.md 93.12.4). DDS_ENTER used to be an ATTRACT state, which
;             is exactly why the initials were typed on the attract screen.
dd_is_box:
    cmp byte [dd_state], DDS_OVER
    je .yes
    cmp byte [dd_state], DDS_ENTER
    je .yes
    clc
    ret
.yes:
    stc
    ret

; dd_text_w - AX = the pixel width of the NUL string at SI, in whole 8px cells
dd_text_w:
    push cx
    push si
    xor cx, cx
.n:
    cmp byte [si], 0
    je .out
    inc si
    inc cx
    cmp cx, DD_TEXTMAX
    jb .n
.out:
    mov ax, cx
    mov cl, 3
    shl ax, cl
    pop si
    pop cx
    ret

; dd_forget - nothing on the glass is ours any more
;
; INCLUDING A MESSAGE OWED A TAKE-DOWN. dd_overlay banks the rectangle it drew
; one in and fills it black when the message goes, and that rectangle is in
; SCREEN coordinates: carrying it across a layout change - GAME OVER handing
; over to the attract screen is one, and it moves dd_bdy by 56 px and re-cuts
; the board - lands the erase somewhere else entirely. It ate a row of the
; maze's outline every time a game ended, AFTER the full repaint that would
; have healed it, because dd_draw_all runs before dd_overlay.
dd_forget:
    push si
    mov word [dd_ovw], 0
    mov word [dd_ovsi], 0
    mov ax, [dd_rept]               ; a whole repaint puts every tile back in
    mov [dd_reph], ax               ; its own pen, so nothing is owed
    xor si, si
.a:
    mov byte [dd_shown + si], 0
    mov byte [dd_qok + si], 0
    inc si
    cmp si, DD_NACT
    jb .a
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_draw - the picture, under whatever lock the caller has
; -----------------------------------------------------------------------------
dd_draw:
    inc word [dd_frames]            ; the ONE diagnostic this package keeps:
                                    ; SPEC.md 93.6's claim is that the frame
                                    ; is the tick, and a claim about a rate
                                    ; needs a count to check it against
    cmp byte [dd_ok], 0
    je dd_draw_toosmall
    mov byte [dd_drewany], 0
    cmp byte [dd_abon], 0
    jne .about                      ; the card is up and dd_step is suspended
    cmp byte [dd_full], 0
    je .parts
    inc word [dd_fulls]
    call dd_draw_all
    mov byte [dd_full], 0
    call dd_is_box                  ; ...AND THE PANEL GOES BACK ON TOP OF IT.
    jnc .over                       ; A full repaint is legal while the panel is
    mov byte [dd_boxd], 1           ; up - the window moved, or was uncovered -
    call dd_box_draw                ; and dd_draw_all paints the whole board,
    jmp short .over                 ; actors included, straight over it
.parts:
    ; A FRAME IN WHICH NOTHING MOVED DRAWS NOTHING (SPEC.md 93.5.6). Paused,
    ; behind READY!, waiting on the board flash - the actors are where they
    ; were and their images are the ones they had, and five bands of identical
    ; pixels is 65% of a 4.77 MHz machine for a picture nobody can tell from
    ; the last one. dd_still says the FRAME has nothing global owed; each actor
    ; then decides for itself in dd_actor_prep.
    mov al, [dd_pilrf]
    or al, [dd_frdirty]
    mov byte [dd_still], 1
    or al, al
    jz .stillok
    mov byte [dd_still], 0
.stillok:
    call dd_is_box                  ; GAME OVER / initials: the board is FROZEN
    jc .box                         ; under the panel (SPEC.md 93.12.4)
    call dd_is_attract
    jc .attract
    call dd_pill_refresh
    call dd_fruit_draw
    call dd_actors_draw
    call dd_rep_run                 ; the tiles the bands have left behind
    cmp byte [dd_hudd], 0
    je .over
    call dd_hud_draw
    mov byte [dd_hudd], 0
    jmp short .over
.attract:
    call dd_attract_draw
    jmp short .over
.box:
    ; NOTHING OF THE BOARD is drawn here - not the pellets' blink, not the
    ; actors, not the repair queue. They are all where they were when the game
    ; ended, the panel is over them, and a frame that drew any of them would
    ; punch a hole in it.
    ;
    ; THE HUD IS NOT THE BOARD, though, and it is the last life that has just
    ; gone: it has its own column beside dd_bdx and cannot reach the panel, so
    ; it draws here as it does anywhere else. Skipping it left a life on the
    ; glass at zero lives, which is what dd_full used to hide.
    cmp byte [dd_hudd], 0
    je .boxp
    call dd_hud_draw
    mov byte [dd_hudd], 0
.boxp:
    cmp byte [dd_boxd], 0
    je .out
    cmp byte [dd_boxd], 2           ; ...and 2 is A LETTER, not the panel
    je .boxi
    call dd_box_draw
    jmp short .out
.boxi:
    call dd_box_ini
    jmp short .out
.over:
    call dd_overlay
.out:
    ret
.about:
    cmp byte [dd_full], 0           ; ...so the only thing that can change
    je .out                         ; under it is the WINDOW
    inc word [dd_fulls]
    mov byte [dd_still], 0
    call dd_draw_all
    mov byte [dd_full], 0
    call dd_overlay
    push bx
    push si
    mov bx, [dd_win]
    mov si, dd_ablines
    call os88ui_about_d
    pop si
    pop bx
    ret

; A surface too small for a board says so rather than drawing a broken one.
dd_draw_toosmall:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [dd_cx]
    mov bx, [dd_cy]
    mov cx, ax
    add cx, [dd_cw]
    dec cx
    mov dx, bx
    add dx, [dd_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, dd_s_small
    cmp byte [dd_nomem], 0          ; the surface, or the arena (93.3.4.5):
    je .say                         ; "make it bigger" is the wrong advice
    mov si, dd_s_nomem              ; when bigger is what was refused
.say:
    mov bx, [dd_cx]
    add bx, 8
    mov dx, [dd_cy]
    add dx, 8
    mov al, DD_INKTXT
    call dd_text
    ret

; -----------------------------------------------------------------------------
; dd_draw_all - everything: the ground, the walls, the dots, the HUD, everybody
; -----------------------------------------------------------------------------
dd_draw_all:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [dd_cx]
    mov bx, [dd_cy]
    mov cx, ax
    add cx, [dd_cw]
    dec cx
    mov dx, bx
    add dx, [dd_ch]
    dec dx
    call OSAPI_GFX_FILL             ; the ONE full-surface fill in this game,
                                    ; and it happens when the window is
                                    ; painted rather than when it is playing
    call dd_forget
    mov byte [dd_still], 0
    mov byte [dd_hudall], 1
    call dd_is_attract
    jc .attract
    call dd_walls_blit
    call dd_dots_blit
    call dd_pills_blit
    call dd_hud_draw
    mov byte [dd_hudd], 0
    call dd_fruit_force
    call dd_actors_draw
    ret
.attract:
    call dd_attract_all
    ret

; -----------------------------------------------------------------------------
; dd_pill_refresh - the pellets' blink, on the glass (SPEC.md 93.5.7)
; Only when the phase actually turned over, and as four RECT FILLS.
; -----------------------------------------------------------------------------
dd_pill_refresh:
    cmp byte [dd_pilrf], 0
    je .out
    mov byte [dd_pilrf], 0
    call dd_pills_flip
.out:
    ret

; -----------------------------------------------------------------------------
; dd_fruit_draw / dd_fruit_force - the fruit under the house
; -----------------------------------------------------------------------------
dd_fruit_draw:
    cmp byte [dd_frdirty], 0
    je .out
    mov byte [dd_frdirty], 0
    call dd_fruit_force
.out:
    ret

dd_fruit_force:
    push si
    cmp word [dd_fruit], 0
    je .gone
    ; the fruit is one tile, drawn as its own band the same way an actor is
    mov ax, [dd_tw]
    mov cl, 3
    shr ax, cl
    mov [dd_rb], ax
    mul word [dd_th]
    mov cx, ax
    cmp cx, DD_BANDSZ
    jbe .ffits
    mov cx, DD_BANDSZ
.ffits:
    mov word [dd_bbase], dd_band
    mov di, dd_band
    xor al, al
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    mov si, dd_spr + SP_FRUIT * DD_SPRSZ
    mov di, dd_band
    mov cx, [dd_th]
.row:
    mov al, [si]
    mov [di], al
    cmp word [dd_tw], 16
    jb .one
    mov al, [si+1]
    mov [di+1], al
.one:
    add si, DD_SPRB
    add di, [dd_rb]
    loop .row
    mov al, DD_INKFRU
    call dd_pen
    push ds
    pop es
    mov si, dd_band
    mov bp, [dd_rb]
    mov ax, DD_FRUITC
    mul word [dd_tw]
    push ax
    mov ax, DD_FRUITR
    mul word [dd_th]
    mov bx, ax
    pop ax
    mov cx, [dd_tw]
    mov dx, [dd_th]
    call dd_blit
    pop si
    ret
.gone:
    mov cl, DD_FRUITC
    mov ch, DD_FRUITR
    call dd_tile_put
    pop si
    ret

; =============================================================================
; THE HUD - a column beside the board (SPEC.md 93.3.1)
; =============================================================================

; -----------------------------------------------------------------------------
; dd_hud_draw - score, high score, level, and the lives still in hand
; Clobbers everything.
; -----------------------------------------------------------------------------
dd_hud_draw:
    ; ONE FIELD, NOT THE PANEL. The score moves on every dot - four or five
    ; times a second - and lettering all seven lines for it was ~20 ms on top
    ; of a ~28 ms frame, which on a real 4.77 MHz Hercules is the difference
    ; between every frame and every other one. The labels never change at all
    ; and the other three numbers change on a life, a level or a new high, so
    ; each line is drawn only when its own value did (SPEC.md 93.5.6).
    mov ax, [dd_hy]
    mov [dd_hrow], ax
    mov si, dd_s_score
    call dd_hud_lab
    mov ax, [dd_score]
    mov dx, [dd_score + 2]
    mov di, dd_lscore
    call dd_hud_num
    call dd_hud_gap
    mov si, dd_s_high
    call dd_hud_lab
    call dd_hs_top                  ; DX:AX = the table's top row, or this
    mov di, dd_lhigh                ; game's score if it has passed it
    call dd_hud_num
    call dd_hud_gap
    mov si, dd_s_level
    call dd_hud_lab
    mov al, [dd_level]
    xor ah, ah
    xor dx, dx
    mov di, dd_llevel
    call dd_hud_num
    call dd_hud_gap
    call dd_hud_lives
    mov byte [dd_hudall], 0
    ret

; dd_hud_lab - SI = a label, drawn only when the whole panel is owed. Either
;              way the pen moves down a line.
dd_hud_lab:
    cmp byte [dd_hudall], 0
    jne dd_hud_label
    jmp short dd_hud_row

; dd_hud_row - the pen down one line, drawing nothing
dd_hud_row:
    push ax
    mov ax, [dd_hrow]
    add ax, DD_HUDROW
    mov [dd_hrow], ax
    pop ax
    ret

dd_hud_label:
    push ax
    push bx
    push cx
    push dx
    mov bx, [dd_hx]
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

; dd_hud_num - DX:AX as decimal at DI's banked copy, padded to the HUD's width
;              so the old digits go with the new ones - a shorter number over a
;              longer one is the classic way to leave a stray digit on the
;              glass. DRAWN ONLY WHEN THE VALUE MOVED.
dd_hud_num:
    push si
    push di
    cmp byte [dd_hudall], 0
    jne .draw
    cmp ax, [di]
    jne .draw
    cmp dx, [di + 2]
    je .same
.draw:
    mov [di], ax
    mov [di + 2], dx
    call dd_dec32
    mov si, dd_numbuf
    call dd_hud_label
    pop di
    pop si
    ret
.same:
    call dd_hud_row
    pop di
    pop si
    ret

dd_hud_gap:
    push ax
    mov ax, [dd_hrow]
    add ax, [dd_hgap]
    mov [dd_hrow], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_hud_lives - the lives in hand, drawn as SMILES rather than as characters
;
; One band wide enough for the CEILING of five, with the slots that are not
; owed left as paper: so a life lost is the same one call, and there is no
; erase to pair with it.
; -----------------------------------------------------------------------------
DD_LIVEMAX equ 5

dd_hud_lives:
    push si
    cmp byte [dd_hudall], 0
    jne .draw
    mov al, [dd_lives]
    cmp al, [dd_llives]
    je .same
.draw:
    mov al, [dd_lives]
    mov [dd_llives], al
    mov si, dd_s_lives
    call dd_hud_label
    mov al, [dd_lives]
    cmp al, DD_LIVEMAX
    jbe .n
    mov al, DD_LIVEMAX
.n:
    jmp short .go
.same:
    call dd_hud_row
    pop si
    ret
.go:
    mov [dd_ltmp], al
    ; a clean band, five tiles wide
    mov ax, [dd_tw]
    mov bx, DD_LIVEMAX
    mul bx
    mov cl, 3
    shr ax, cl
    mov [dd_rb], ax
    mov word [dd_bbase], dd_tband
    mul word [dd_th]
    mov cx, ax
    cmp cx, DD_TBANDSZ
    jbe .lfits
    mov cx, DD_TBANDSZ
.lfits:
    mov di, dd_tband
    xor al, al
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    ; ...and one Smiles a life, byte-aligned, so no shift is needed
    xor bx, bx
.life:
    cmp bl, [dd_ltmp]
    jae .emit
    push bx
    mov ax, bx
    mul word [dd_tw]
    mov cl, 3
    shr ax, cl
    mov di, dd_tband
    add di, ax                      ; the slot's first band byte
    mov si, dd_spr + (SP_PAC + DIR_L * 3 + 1) * DD_SPRSZ
    mov cx, [dd_th]
.row:
    mov al, [si]
    mov [di], al
    cmp word [dd_tw], 16
    jb .one
    mov al, [si+1]
    mov [di+1], al
.one:
    add si, DD_SPRB
    add di, [dd_rb]
    loop .row
    pop bx
    inc bx
    cmp bx, DD_LIVEMAX
    jb .life
.emit:
    mov al, [dd_ink]
    call dd_pen
    push ds
    pop es
    mov si, dd_tband
    mov bp, [dd_rb]
    mov ax, [dd_hx]
    add ax, 7
    and ax, ~7                      ; gfx_blit1 wants the byte grid
    mov bx, [dd_hrow]
    mov cx, [dd_tw]
    push dx
    mov dx, DD_LIVEMAX
    push ax
    mov ax, cx
    mul dx
    mov cx, ax
    pop ax
    pop dx
    mov dx, [dd_th]
    call OSAPI_GFX_BLIT1            ; ABSOLUTE: the HUD is not on the board
    mov ax, [dd_hrow]
    add ax, [dd_th]
    mov [dd_hrow], ax
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_dec32 - DX:AX into dd_numbuf, decimal, space-padded to DD_NUMW
; -----------------------------------------------------------------------------
DD_NUMW equ 9

dd_dec32:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, dd_numbuf + DD_NUMW     ; written backwards from its NUL
    mov byte [di], 0
    mov cx, DD_NUMW
.digit:
    dec di
    push cx
    mov cx, ax                      ; the standard 32-by-16 divide: the high
    mov ax, dx                      ; word first, its remainder carried into
    xor dx, dx                      ; the low one
    div word [dd_ten]
    mov bx, ax
    mov ax, cx
    div word [dd_ten]
    mov cx, dx                      ; CX = this digit
    mov dx, bx                      ; DX:AX = what is left
    add cl, '0'
    mov [di], cl
    pop cx
    mov bx, ax
    or bx, dx
    jz .done
    dec cx
    jnz .digit
.done:
    mov si, di                      ; left-justify...
    mov di, dd_numbuf
.mv:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .mv
    dec di                          ; ...and pad the rest with spaces, because
    mov cx, dd_numbuf + DD_NUMW     ; FONT_RUN is opaque over the cells it
    sub cx, di                      ; draws and no further: a shorter number
    jcxz .out                       ; over a longer one leaves a stray digit
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
    mov byte [di], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dd_ten: dw 10

; =============================================================================
; THE OVERLAY - one centred line over the board, in the states that want one
;
; It sits on the row under the ghost house, which is the one row of the board
; that is EMPTY in every layout: a message there covers no dot, so taking it
; down is a black fill and not a repaint. The line that put it up is opaque
; (`font_run`), so nothing has to erase it before it changes either.
; =============================================================================
dd_overlay:
    mov si, 0
    mov al, [dd_state]
    cmp al, DDS_READY
    je .ready
    cmp al, DDS_CLEAR
    je .clear
    cmp al, DDS_PLAY
    je .play
    jmp short .none
.play:
    cmp byte [dd_paused], 0
    je .none
    mov si, dd_s_paused
    jmp short .put
.ready:
    mov si, dd_s_ready
    jmp short .put
.clear:
    mov bx, [dd_anim]
    and bx, 2
    jz .none
    mov si, dd_s_clear
.put:
    cmp si, [dd_ovsi]               ; the same line as last frame, over pixels
    jne .draw                       ; nothing has touched? Then it is still
    cmp byte [dd_drewany], 0        ; there. READY! was being lettered 18 times
    je .out                         ; a second at ~5 ms a go
.draw:
    mov [dd_ovsi], si
    call dd_centre_line
    ret
.none:
    mov word [dd_ovsi], 0
    cmp word [dd_ovw], 0
    je .out
    mov al, CBLACK                  ; a message came down: the row under it is
    call OSAPI_SET_COLOR            ; empty board, so black is the whole erase
    mov ax, [dd_ovx]
    mov bx, [dd_ovy]
    mov cx, ax
    add cx, [dd_ovw]
    dec cx
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL
    call dd_ov_spill
    mov word [dd_ovw], 0
    mov byte [dd_frdirty], 1        ; ...and the fruit shares that row
.out:
    ret

; -----------------------------------------------------------------------------
; dd_ov_spill - the overlay's band is EIGHT ROWS and a tile need not be
;
; The line sits on DD_FRUITR, the one board row that is empty in every layout,
; which is what makes black the whole erase - THERE. It is not the whole erase
; anywhere else, and dd_centre_line already knows the band may not fit: when
; [dd_th] is under 8 it gives up centring and starts at the tile's top, so the
; other four rows land on the row BELOW. Under the ghost house those tiles are
; WALL, and the fill took the wall's own line off the glass for the rest of the
; level - one pixel high, six tiles wide, and permanent.
;
; It is the corner defect one row along and it takes the same answer: put the
; tile back rather than black it, now that dd_tile_put restores a wall's
; picture. It costs NOTHING on VGA or Hercules, whose tiles are 8 rows or more
; so the band fits and the walk is one compare - this is a small-window CGA
; defect, which is why twelve seconds of colour census never saw it.
; Clobbers everything.
; -----------------------------------------------------------------------------
dd_ov_spill:
    mov ax, [dd_th]
    cmp ax, 8
    jae .out                        ; the band fits its own row
    mov ax, [dd_ovy]
    sub ax, [dd_bdy]
    js .out
    add ax, 7
    xor dx, dx
    div word [dd_th]
    cmp ax, DD_ROWS - 1
    jbe .r1
    mov ax, DD_ROWS - 1
.r1:
    mov [dd_ovr1], al
    cmp al, DD_FRUITR               ; the band reaches no further than the row
    jbe .out                        ; it was drawn on
    mov byte [dd_ovr0], DD_FRUITR + 1
    mov ax, [dd_ovx]
    sub ax, [dd_bdx]
    jns .x0                         ; dd_centre_line rounds the pen DOWN to a
    xor ax, ax                      ; byte column, so the line can start up to
.x0:                                ; seven pixels left of the board
    xor dx, dx
    div word [dd_tw]
    mov [dd_ovc0], al
    mov ax, [dd_ovx]
    add ax, [dd_ovw]
    dec ax
    sub ax, [dd_bdx]
    xor dx, dx
    div word [dd_tw]
    cmp ax, DD_COLS - 1
    jbe .c1
    mov ax, DD_COLS - 1
.c1:
    mov [dd_ovc1], al
.row:
    mov al, [dd_ovc0]
    mov [dd_ovcc], al
.col:
    mov cl, [dd_ovcc]               ; dd_tile_put owns [dd_gc]/[dd_gr], so the
    mov ch, [dd_ovr0]               ; walk keeps its own cursor
    call dd_tile_put
    inc byte [dd_ovcc]
    mov al, [dd_ovcc]
    cmp al, [dd_ovc1]
    jbe .col
    inc byte [dd_ovr0]
    mov al, [dd_ovr0]
    cmp al, [dd_ovr1]
    jbe .row
.out:
    ret

; dd_centre_line - SI = a NUL string, centred on the board on the empty row
;                  below the ghost house, and banked so it can be taken down
; -----------------------------------------------------------------------------
; dd_box_draw - the GAME OVER / initials panel, centred on the BOARD
;               (SPEC.md 93.12.4)
;
; An outlined box with a black inside, drawn OVER the board rather than instead
; of it. The initials used to be typed on the ATTRACT screen, a moment after it
; had replaced the board - so the field's report was not that the prompt was
; missing but that it arrived somewhere else: *"I was expecting the entry on
; the board screen, not the attract screen."* The score was made on this board
; and it is typed on this board.
;
; It is drawn when [dd_boxd] says so and NOT once a frame. The board under it is
; frozen for as long as it is up (dd_draw's own arm below), so nothing can
; damage it, and a panel redrawn every tick would be a fill, a frame and two
; lines of type for a picture identical to the last one.
; Clobbers everything.
; -----------------------------------------------------------------------------
DD_BOXPX  equ 12                ; padding inside the outline, per axis...
DD_BOXPY  equ 8
DD_BOXGAP equ 6                 ; ...and between the two lines

dd_box_draw:
    mov byte [dd_boxd], 0
    cmp byte [dd_ok], 0
    je .out                         ; a surface too small for a board is too
                                    ; small for a panel over one
    ; --- the widest line decides the width ---------------------------------
    mov si, dd_s_over
    call dd_text_w
    mov cx, ax
    cmp byte [dd_state], DDS_ENTER
    jne .wok
    call dd_box_str
    mov si, dd_promptb
    call dd_text_w
    cmp ax, cx
    jbe .wok
    mov cx, ax
.wok:
    add cx, DD_BOXPX * 2 + 2
    cmp cx, [dd_mw]                 ; ...and the board is the ceiling
    jbe .wfits
    mov cx, [dd_mw]
.wfits:
    mov [dd_boxw], cx
    mov ax, 8 + DD_BOXPY * 2 + 2
    cmp byte [dd_state], DDS_ENTER
    jne .hok
    add ax, DD_BOXGAP + 8
.hok:
    mov [dd_boxh], ax
    ; --- centred on the BOARD, which is not the content box ----------------
    mov ax, [dd_mw]
    sub ax, [dd_boxw]
    shr ax, 1
    add ax, [dd_bdx]
    mov [dd_boxx], ax
    mov ax, [dd_mh]
    sub ax, [dd_boxh]
    shr ax, 1
    add ax, [dd_bdy]
    mov [dd_boxy], ax
    ; --- black inside, then the outline over it ----------------------------
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call dd_box_edge
    call OSAPI_GFX_FILL
    mov al, DD_INKTXT
    call OSAPI_SET_COLOR
    call dd_box_edge
    call OSAPI_GFX_FRAME
    ; --- and the lines ------------------------------------------------------
    mov si, dd_s_over
    mov bx, [dd_boxy]
    add bx, DD_BOXPY + 1
    call dd_box_line
    cmp byte [dd_state], DDS_ENTER
    jne .out
    mov si, dd_promptb
    mov bx, [dd_boxy]
    add bx, DD_BOXPY + 1 + 8 + DD_BOXGAP
    call dd_box_line
.out:
    ret

; dd_box_edge - AX/BX/CX/DX = the panel, as the two fill/frame slots take it
dd_box_edge:
    mov ax, [dd_boxx]
    mov bx, [dd_boxy]
    mov cx, ax
    add cx, [dd_boxw]
    dec cx
    mov dx, bx
    add dx, [dd_boxh]
    dec dx
    ret

; dd_box_line - the NUL string at SI, centred in the panel, its top row at BX
dd_box_line:
    push ax
    push bx
    push cx
    push dx
    push si
    call dd_text_w
    mov cx, [dd_boxw]
    sub cx, ax
    shr cx, 1
    add cx, [dd_boxx]
    and cx, ~7                      ; dd_text rounds to the byte grid itself,
    mov [dd_boxlx], cx              ; and what is banked has to be the pen it
    mov [dd_boxly], bx              ; ACTUALLY used, not the one asked for
    mov dx, bx
    mov bx, cx
    mov al, DD_INKTXT
    call dd_text
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_box_ini - THE INITIALS AND NOTHING ELSE (SPEC.md 93.12.4.1)
;
; The panel is a fill, an outline and two lines of type, and a keystroke changes
; four cells of the second one. Redrawing all of it per letter is what the field
; reported as *"its flashing every time I type"* - the black fill lands, then
; the outline, then the type, and the eye sees the box blink out and come back.
;
; The four cells go down where dd_box_line put them: it banks the pen it
; actually used, and the initials start a whole number of CELLS along that line,
; so adding dd_inioff * 8 keeps the byte grid gfx_blit1 wants. Nothing under
; them is redrawn because nothing under them changed - the panel's inside is
; black and dd_text composes its own ground.
; Clobbers everything.
; -----------------------------------------------------------------------------
dd_box_ini:
    mov byte [dd_boxd], 0
    cmp byte [dd_ok], 0
    je .out
    call dd_box_str                 ; the three letters and the caret
    mov si, dd_promptb
    add si, [dd_inioff]
    mov ax, [dd_inioff]
    mov cl, 3
    shl ax, cl
    add ax, [dd_boxlx]
    mov bx, ax
    mov dx, [dd_boxly]
    mov al, DD_INKTXT
    call dd_text
.out:
    ret

; -----------------------------------------------------------------------------
dd_centre_line:
    push ax
    push bx
    push cx
    push dx
    push si
    call dd_text_w                  ; AX = its width in whole cells
    mov [dd_ovw], ax
    mov cx, [dd_mw]
    sub cx, ax
    shr cx, 1
    add cx, [dd_bdx]
    and cx, ~7
    mov [dd_ovx], cx
    mov ax, DD_FRUITR
    mul word [dd_th]
    mov dx, ax
    mov ax, [dd_th]
    sub ax, 8
    jns .mid
    xor ax, ax
.mid:
    shr ax, 1
    add dx, ax
    add dx, [dd_bdy]
    mov [dd_ovy], dx
    mov bx, cx
    mov al, DD_INKTXT
    call dd_text
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
