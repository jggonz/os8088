; =============================================================================
; os8088 - apps/dotdel/ddmaze.inc
;
; THE BOARD AND ITS PICTURE (SPEC.md 93.2).
;
; A layout is 31 rows of 28 characters, written out as characters because a
; maze is a PICTURE and a nibble-packed one cannot be reviewed. `#` is wall,
; `.` a dot, `o` a power pellet, `=` the ghost-house door and a space is open
; floor with nothing on it. `tests/unit/t_ddmaze.py` reads these very bytes
; back out of this file and floods them: every layout is checked for width,
; height, four pellets, a reachable dot count, and the shared block below.
;
; --- WHAT EVERY LAYOUT SHARES, AND WHY (binding) -----------------------------
;
; Rows 9 to 19 are IDENTICAL in every layout: the ghost house, its door, the
; two columns above it the ghosts leave by, the tunnel row, and the two long
; verticals at columns 6 and 21 that carry the whole board's traffic past the
; house. Everything that knows where a ghost is born, where its eyes go home
; to, where the fruit appears and where Smiles starts reads those rows, so a
; layout that moved them would need its own copy of all of it. Changing the
; board every three levels is meant to change the CHASE, not the machinery.
;
; --- THE WALL IS THE CORRIDOR'S OUTLINE, NOT THE WALL'S FILL -----------------
;
; The ink is laid one line INTO the wall from every corridor edge, so what the
; player sees is each corridor traced in a thin line, with black inside the
; wall between two of them - which is what the arcade board looks like, and
; also what makes the sprite band cheap (ddlay.inc): the line lives in the
; wall tile and never in the corridor, so an actor's band may round out to the
; byte grid along its own corridor and still contain nothing but dots.
;
; A FILLED wall was the other candidate and loses twice. It is a screen mostly
; white on the two 1bpp adapters - the corridors read as the walls - and it
; puts ink inside the tile the band rounds into, which would cost a second
; blit per actor to keep the wall out of the actor's pen.
; =============================================================================

TT_WALL   equ 0
TT_EMPTY  equ 1
TT_DOT    equ 2
TT_PILL   equ 3
TT_DOOR   equ 4

DD_NPILL  equ 4                 ; power pellets on a board, and the layouts
                                ; are gated on exactly four (SPEC.md 93.2)
DD_NLAYS  equ 3                 ; ...and the board changes every three levels
DD_LEVLAY equ 3

; The fixed furniture of the shared block, in tiles.
DD_HOMEC  equ 13                ; above the door: where eyes go home to
DD_HOMER  equ 11
DD_HOUSER equ 14                ; the row the three penned ghosts wait on
; the pen's INTERIOR, which is six by three and open in all three layouts.
; dd_pen_ok walks a penned ghost inside this box and no further (SPEC.md 93.8.6)
DD_PENC0  equ 11
DD_PENC1  equ 16
DD_PENR0  equ 13
DD_PENR1  equ 15
DD_STARTC equ 13                ; Smiles
DD_STARTR equ 23
DD_FRUITC equ 13
DD_FRUITR equ 17

; -----------------------------------------------------------------------------
; THE LAYOUTS ARE CHARACTERS IN THE SOURCE AND TWO BITS A TILE IN THE IMAGE
;
; ddmzdat.inc stays the picture a person reviews - 31 rows of 28 characters a
; layout - and DDROW packs each row as the assembler reads it: every row is
; its own mirror (the assembly REFUSES one that is not), so only columns 0..13
; are kept, at two bits a tile, four tiles a byte, MSB first, padded to a
; byte boundary at the end of the row - four bytes a row. Rows 9..19 are the
; shared block (SPEC.md 93.2), so they are packed ONCE, from layout 0, after
; the three layouts, and every later layout's copy of them is CHECKED against
; layout 0's rather than stored. 2,604 bytes of characters become 284.
;
;   code 0 '#' wall   1 ' ' floor   2 '.' dot   3 'o' pellet / '=' door
;
; 'o' and '=' share code 3 because a pellet is never inside the shared block
; and the door never outside it - both of which DDROW also refuses - so the
; ROW says which one a 3 is.
; -----------------------------------------------------------------------------
DD_MZSH0  equ 9                 ; the shared block's first row...
DD_MZSH1  equ 20                ; ...and the first row after it
DD_MZRB   equ 4                 ; packed bytes a row
DD_MZLB   equ (DD_ROWS - (DD_MZSH1 - DD_MZSH0)) * DD_MZRB
                                ; ...and a layout's own, the shared rows out

%assign DDMZ_N 0                ; rows seen, over all three layouts

; DDROWENC 'row' - emit the four packed bytes of one 28-character row
%macro DDROWENC 1
    %assign %%acc 0
    %assign %%i 1
    %rep 16
        %if %%i <= 14
            %substr %%c %1 %%i
            %if %%c = '#'
                %assign %%v 0
            %elif %%c = ' '
                %assign %%v 1
            %elif %%c = '.'
                %assign %%v 2
            %else
                %assign %%v 3
            %endif
        %else
            %assign %%v 0
        %endif
        %assign %%acc (%%acc << 2) | %%v
        %if (%%i % 4) = 0
            db %%acc
            %assign %%acc 0
        %endif
        %assign %%i %%i + 1
    %endrep
%endmacro

; DDROW 'row' - one row of a layout: checked, and packed unless it is shared
%macro DDROW 1
    %assign %%r DDMZ_N % DD_ROWS
    %assign %%l DDMZ_N / DD_ROWS
    %strlen %%n %1
    %if %%n != DD_COLS
        %error "DDROW: a layout row is 28 characters"
    %endif
    %assign %%i 1
    %rep DD_COLS
        %substr %%a %1 %%i
        %substr %%b %1 (DD_COLS + 1 - %%i)
        %if %%a != %%b
            %error "DDROW: a layout row must be its own mirror"
        %endif
        %if %%a = '='
            %if %%r < DD_MZSH0 || %%r >= DD_MZSH1
                %error "DDROW: the door is only in the shared block"
            %endif
        %elif %%a = 'o'
            %if %%r >= DD_MZSH0 && %%r < DD_MZSH1
                %error "DDROW: no pellet in the shared block"
            %endif
        %elif %%a != '#' && %%a != ' ' && %%a != '.'
            %error "DDROW: not a maze character"
        %endif
        %assign %%i %%i + 1
    %endrep
    %if %%r >= DD_MZSH0 && %%r < DD_MZSH1
        %if %%l = 0
            %xdefine DDMZ_SH%[%%r] %1
        %else
            %ifnidn DDMZ_SH%[%%r], %1
                %error "DDROW: rows 9..19 must be layout 0's (SPEC.md 93.2)"
            %endif
        %endif
    %else
        DDROWENC %1
    %endif
    %assign DDMZ_N DDMZ_N + 1
%endmacro

; DDSHARED - the shared block, once, out of layout 0's own rows
%macro DDSHARED 0
    %if DDMZ_N != DD_NLAYS * DD_ROWS
        %error "DDSHARED: three layouts of 31 rows come first"
    %endif
    %assign %%r DD_MZSH0
    %rep DD_MZSH1 - DD_MZSH0
        DDROWENC DDMZ_SH%[%%r]
        %assign %%r %%r + 1
    %endrep
%endmacro

; -----------------------------------------------------------------------------
; dd_level_load - decode the layout this level uses into dd_grid
; in:  [dd_level], 1-based;  out: [dd_ndots] = dots + pellets left to eat
; Clobbers everything.
; -----------------------------------------------------------------------------
dd_level_load:
    push bp
    mov al, [dd_level]
    dec al
    xor ah, ah
    mov bl, DD_LEVLAY
    div bl                          ; AL = (level-1)/3, the board's ordinal
    xor ah, ah
    mov bl, DD_NLAYS
    div bl                          ; AH = that, modulo the boards we have
    mov al, DD_MZLB
    mul ah
    add ax, dd_lay0
    mov si, ax                      ; SI = this layout's own packed rows
    mov di, dd_grid
    xor dx, dx                      ; DX = things left to eat
    xor ch, ch                      ; CH = the row
.row:
    cmp ch, DD_MZSH0
    jne .r1
    mov bp, si                      ; the shared block, from its one copy
    mov si, dd_mzshr
.r1:
    cmp ch, DD_MZSH1
    jne .r2
    mov si, bp                      ; ...and back to this layout's own
.r2:
    xor bx, bx                      ; BX = the column, 0..13
.col:
    test bl, 3
    jnz .have
    lodsb                           ; four tiles a byte, MSB first
    mov ah, al
.have:
    rol ah, 1
    rol ah, 1
    mov al, ah
    and al, 3                       ; AL = the code, which IS the tile type
    cmp ch, [dd_rmin]               ; for wall, floor and dot
    jae .live
    xor al, al                      ; above the slice there is nothing but wall
.live:
    cmp al, TT_DOT
    jb .put
    je .eat
    cmp ch, DD_MZSH0                ; a 3: the door inside the shared block,
    jb .eat                         ; a pellet anywhere else
    cmp ch, DD_MZSH1
    jae .eat
    mov al, TT_DOOR
    jmp short .put
.eat:
    inc dx                          ; ...twice: the tile and its mirror
    inc dx
.put:
    mov [di + bx], al
    neg bx
    mov [di + bx + DD_COLS - 1], al ; ...the mirror, column 27 - c
    neg bx
    inc bx
    cmp bl, DD_COLS / 2
    jb .col
    add di, DD_COLS
    inc ch
    cmp ch, DD_ROWS
    jb .row
    mov [dd_ndots], dx
    pop bp
    call dd_pill_list
    call dd_rowoff_fill
    call dd_home_map                ; the route home, once (SPEC.md 93.8.5)
    ret

; -----------------------------------------------------------------------------
; dd_pill_list - where the power pellets are, so the blink need not look
; -----------------------------------------------------------------------------
dd_pill_list:
    push ax
    push bx
    push cx
    push si
    xor si, si                      ; SI = how many we have found
    xor bx, bx                      ; BX = the tile ordinal
    xor cx, cx
.rows:
    xor cl, cl
.cols:
    cmp byte [dd_grid + bx], TT_PILL
    jne .next
    cmp si, DD_NPILL
    jae .next
    mov [dd_pilc + si], cl
    mov [dd_pilr + si], ch
    inc si
.next:
    inc bx
    inc cl
    cmp cl, DD_COLS
    jb .cols
    inc ch
    cmp ch, DD_ROWS
    jb .rows
    mov [dd_piln], si
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_rowoff_fill - row * 28, once, so dd_tile is a table read and not a MUL
; -----------------------------------------------------------------------------
dd_rowoff_fill:
    push ax
    push bx
    push cx
    xor bx, bx
    xor ax, ax
    mov cx, DD_ROWS
.r:
    mov [dd_rowoff + bx], ax
    add ax, DD_COLS
    add bx, 2
    loop .r
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE WAY HOME (SPEC.md 93.8.5)
;
; A pair of eyes needs a route to the pen and dd_gh_aim cannot give it one: a
; greedy descent on squared distance has LOCAL MINIMA, and the ghost house is
; behind a one-tile door that no descent finds from the wrong side. Two eyes
; ping-ponged between (11,20) and (12,20) for thirty seconds, which is the
; field report - "once eaten a ghost goes out a side path, and either never
; returns or loops across the map once".
;
; So the route is worked out ONCE, when the board is decoded: a breadth-first
; walk out from the pen leaves every reachable tile holding the direction to
; step to get one tile closer. An eaten ghost then reads one NIBBLE a tile and
; walks it. It is 434 bytes of map and 128 of queue against a board that
; changes three times in a game, and the walk itself is a few ms - a
; hundredth of what a per-move search would cost on this machine.
;
; THE QUEUE IS A RING, and the walk's FRONTIER is what sizes it, not the
; board: a tile is queued once and popped once, and at no moment are more than
; 22 waiting in any of the three layouts. It was 868 words, one for every tile
; the walk might ever reach, and all but a few dozen were never live at once.
; tests/unit/t_ddmaze.py floods every layout with this walk's own order and
; fails a frontier that would not fit DD_HQN with room to spare; a ring that
; filled anyway would leave the tiles past it UNMAPPED, which dd_gh_home
; already answers by aiming - a slower way home, never a wrong one.
;
; An entry is the tile's (column, row) as CL/CH, so a pop is a load and not
; the 16-bit DIV per tile the flat index cost.
; =============================================================================

DD_HDROOT equ 0Eh               ; the pen itself: visited, and nowhere to go
DD_HDNONE equ 0Fh               ; not reached - a wall, or a board with no pen
DD_HDB    equ (DD_COLS * DD_ROWS + 1) / 2   ; the map: two tiles a byte,
                                ; the EVEN tile in the low nibble
DD_HQN    equ 64                ; the ring, in entries (a power of two)

; -----------------------------------------------------------------------------
; dd_tindex - AX = the flat tile index of column CL, row CH (both in range)
; Preserves every other register.
; -----------------------------------------------------------------------------
dd_tindex:
    push bx
    mov bl, ch
    xor bh, bh
    shl bx, 1
    mov ax, [dd_rowoff + bx]
    mov bl, cl
    xor bh, bh
    add ax, bx
    pop bx
    ret

; dd_hd_at - DI = the dd_hdir byte holding tile CL, CH; CF = 1 when it is the
;            tile's HIGH nibble. Clobbers AX.
dd_hd_at:
    call dd_tindex
    shr ax, 1
    mov di, ax                      ; MOV leaves the SHR's carry alone
    ret

; -----------------------------------------------------------------------------
; dd_home_map - fill dd_hdir by breadth-first search out from the pen
; Clobbers everything.
; -----------------------------------------------------------------------------
dd_home_map:
    push si
    push bp
    mov di, dd_hdir                 ; nothing is reachable until it is reached
    mov cx, DD_HDB
    mov al, DD_HDNONE * 11h
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    xor si, si                      ; SI = the ring's head, BP = its tail -
    xor bp, bp                      ; counts that only rise, read mod DD_HQN
    mov cl, DD_HOMEC
    mov ch, DD_HOUSER
    call dd_tile
    cmp al, TT_WALL
    je .out                         ; no pen on this board - the attract slice
    mov dl, DD_HDROOT
    call .claim
.loop:
    cmp si, bp
    je .out                         ; the ring is empty: every tile is mapped
    mov di, si
    inc si
    and di, DD_HQN - 1
    shl di, 1
    mov dx, [dd_hq + di]            ; DX = the tile being expanded
    xor bx, bx
.nb:
    mov cx, dx
    add cl, [dd_dx + bx]
    add ch, [dd_dy + bx]
    cmp ch, 0
    jl .next
    cmp ch, DD_ROWS
    jge .next
    cmp cl, 0                       ; the tunnel wraps, and so does the route
    jge .nolo
    add cl, DD_COLS
.nolo:
    cmp cl, DD_COLS
    jl .nohi
    sub cl, DD_COLS
.nohi:
    call dd_tile
    cmp al, TT_WALL
    je .next                        ; a DOOR is passable: eyes go through it
    push dx
    mov dl, [dd_rev + bx]           ; from THERE, the way back here
    call .claim
    pop dx
.next:
    inc bx
    cmp bx, 4
    jb .nb
    jmp short .loop
.out:
    pop bp
    pop si
    ret

; --- tile CL,CH gets DL if nothing has reached it yet, and is queued -------
; in:  CX = the tile, DL = its direction home, SI/BP = the ring. Clobbers
;      AX/DX/DI.
.claim:
    call dd_hd_at
    mov ax, 0Fh
    jnc .lo
    push cx
    mov cl, 4
    shl dl, cl                      ; the HIGH nibble: value and mask with it
    shl al, cl
    pop cx
.lo:
    mov ah, [dd_hdir + di]
    and ah, al
    cmp ah, al
    jne .seen                       ; reached already
    not al
    and al, [dd_hdir + di]
    or al, dl
    mov [dd_hdir + di], al
    mov ax, bp                      ; ...and onto the ring, unless it is full
    sub ax, si
    cmp ax, DD_HQN
    jae .seen
    mov di, bp
    inc bp
    and di, DD_HQN - 1
    shl di, 1
    mov [dd_hq + di], cx
.seen:
    ret

; -----------------------------------------------------------------------------
; dd_tile - AL = the tile type at column CL, row CH
; in:  CL = column (signed), CH = row (signed).  Preserves BX/CX/DX/SI/DI.
;
; Rows off the board are wall. COLUMNS WRAP, because the tunnel is a real edge
; of the world and not an error: column -1 is column 27.
; -----------------------------------------------------------------------------
dd_tile:
    push bx
    push cx
    cmp ch, 0
    jl .off
    cmp ch, DD_ROWS
    jge .off
    cmp cl, 0
    jge .nolo
    add cl, DD_COLS
.nolo:
    cmp cl, DD_COLS
    jl .nohi
    sub cl, DD_COLS
.nohi:
    mov bl, ch
    xor bh, bh
    shl bx, 1
    mov bx, [dd_rowoff + bx]        ; a TABLE and not a multiply: dd_tile is
    mov al, cl                      ; the most-called routine in the game
    xor ah, ah
    add bx, ax
    mov al, [dd_grid + bx]
    pop cx
    pop bx
    ret
.off:
    mov al, TT_WALL
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; THE INK MAP (SPEC.md 93.2.3.2) - one bit a tile, set when 93.2.3's CONCAVE
; round has put a corner block INSIDE that tile. It is always a corridor tile,
; because a concave corner's arms run out of the wall into the corridor - and
; a corridor was the one thing dd_band_ground and dd_tile_put were both
; entitled to assume was empty. The map is what makes that assumption exact
; again for the price of 109 bytes and a bit test, instead of either copying
; the wall picture under every band (12 ms of a VGA frame, SPEC.md 93.5.3
; item 4) or working the predicate out from twelve tile lookups a corner.
;
; It is rebuilt by dd_board_render and never written after, so nothing has to
; keep it in step with a dot being eaten.
; -----------------------------------------------------------------------------
; dd_ink_bit - BX = the map byte, AL = the bit, for tile (CL, CH)
;              CF = 1 when the tile is off the board. Clobbers AX/BX.
dd_ink_bit:
    push cx
    cmp ch, 0
    jl .off
    cmp ch, DD_ROWS
    jge .off
    cmp cl, 0
    jl .off
    cmp cl, DD_COLS
    jge .off
    mov bl, ch
    xor bh, bh
    shl bx, 1
    mov bx, [dd_rowoff + bx]
    mov al, cl
    xor ah, ah
    add bx, ax                      ; BX = the tile's index
    mov cl, bl
    and cl, 7
    mov al, 1
    shl al, cl
    shr bx, 1
    shr bx, 1
    shr bx, 1
    pop cx
    clc
    ret
.off:
    pop cx
    stc
    ret

; dd_ink_set - tile (CL, CH) carries a corner block. Preserves every register.
dd_ink_set:
    push ax
    push bx
    call dd_ink_bit
    jc .out
    or [dd_cnrmap + bx], al
.out:
    pop bx
    pop ax
    ret

; dd_ink_ck - CF = 1 when tile (CL, CH) does. Preserves every register.
dd_ink_ck:
    push ax
    push bx
    call dd_ink_bit
    jc .no
    test [dd_cnrmap + bx], al
    jz .no
    pop bx
    pop ax
    stc
    ret
.no:
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dd_iswall - CF = 1 when the tile at CL,CH blocks everything
; A DOOR IS A WALL TO SMILES AND A FLOOR TO A GHOST, so it is NOT answered
; here: the two callers that care test TT_DOOR themselves.
; -----------------------------------------------------------------------------
dd_iswall:
    call dd_tile
    cmp al, TT_WALL
    je .yes
    clc
    ret
.yes:
    stc
    ret

; =============================================================================
; THE PICTURE - a 1bpp bitmap of the board, one bit a pixel, bit 7 leftmost
;
; It is the only thing the renderer copies a band out of, and the only place a
; dot's death is recorded in pixels: eating one clears its bits here and the
; very next band that crosses the tile carries the absence with it.
; =============================================================================

; -----------------------------------------------------------------------------
; dd_bd_hrun - lay CX pixels of row BX from board x = AX, in ink DH (1 set,
;              0 clear). Pixels off the board are DROPPED, never wrapped.
; in:  AX = x (signed), BX = y (signed), CX = width, DH = ink
; out: nothing; preserves every register but the flags.
; -----------------------------------------------------------------------------
dd_bd_hrun:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov [dd_hrunk], dh              ; THE INK, out of DX: the row-base multiply
                                    ; below writes DX, and the ink has to
                                    ; survive it
    or cx, cx
    jle .out
    or bx, bx
    jl .out
    cmp bx, [dd_mh]
    jge .out
    or ax, ax
    jge .noleft
    add cx, ax                      ; a negative x eats into the width
    jle .out
    xor ax, ax
.noleft:
    mov di, ax                      ; NOT dx: DH is the ink for the whole run
    add di, cx
    cmp di, [dd_mw]
    jbe .noright
    mov cx, [dd_mw]
    sub cx, ax
    jle .out
.noright:
    ; ES:DI = the first byte, DL = the pixel's bit offset from the left (0..7)
    push ax
    mov ax, bx
    mul word [dd_sb]
    mov di, ax
    pop ax
    mov bx, ax
    push cx
    mov cl, 3
    shr bx, cl
    pop cx
    add di, bx
    and al, 7
    mov dl, al                      ; DL = offset into the byte, 0 = leftmost
    mov bx, [dd_bdseg]
    mov es, bx
.byte:
    mov al, 8
    sub al, dl                      ; AL = bits this byte can still take
    cmp cx, 8
    jae .nfull
    cmp al, cl
    jbe .nfull
    mov al, cl                      ; ...capped by what is left to lay
.nfull:
    mov bl, al                      ; BL = n, the bits this byte gets
    mov ah, 0FFh
    push cx
    mov cl, 8
    sub cl, bl
    shl ah, cl                      ; AH = n high bits set
    mov cl, dl
    shr ah, cl                      ; ...slid to where they belong
    pop cx
    cmp byte [dd_hrunk], 0
    je .clr
    or [es:di], ah
    jmp short .adv
.clr:
    not ah
    and [es:di], ah
.adv:
    xor bh, bh
    sub cx, bx
    jle .out
    inc di
    xor dl, dl                      ; every byte after the first starts left
    jmp short .byte
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_bd_rect - CX x BP pixels of ink DH, top-left (AX, BX)
; in:  AX = x, BX = y, CX = w, BP = h, DH = ink. Preserves every register.
; -----------------------------------------------------------------------------
dd_bd_rect:
    push bx
    push bp
    or bp, bp
    jle .out
.row:
    call dd_bd_hrun
    inc bx
    dec bp
    jnz .row
.out:
    pop bp
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_bd_wipe - zero the whole picture
; -----------------------------------------------------------------------------
dd_bd_wipe:
    push ax
    push cx
    push di
    push es
    mov ax, [dd_bdseg]
    or ax, ax                       ; NO PICTURE, NO WIPE (SPEC.md 93.3.4.5):
    jz .out                         ; a refused claim leaves this 0, and a
    mov es, ax                      ; rep stosw at 0000:0000 is the vector table
    xor di, di
    mov ax, [dd_sb]
    mul word [dd_mh]                ; 56 * 496 = 27,776: still one word
    mov cx, ax
    inc cx
    shr cx, 1
    xor ax, ax
    cld
    rep stosw
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; DRAWING THE BOARD INTO THE PICTURE
; =============================================================================

; -----------------------------------------------------------------------------
; dd_board_render - the whole board: wall outline, dots, pellets, door
; Clobbers everything. ~200 ms on a 4.77 MHz 8088 at the VGA tile, which is a
; level-start cost and is spent behind "READY!".
;
; THREE PASSES, and the split is a bug fix rather than tidiness (93.2.3.1). The
; outline is drawn by CORRIDOR tiles, one line into each wall that touches
; them, so a wall tile's corner is made by two lines from two DIFFERENT tiles -
; and in one row-major walk only the corridor ABOVE and the corridor to the
; LEFT have drawn theirs by the time the wall is reached. Rounding as it went
; took the elbow out and the corridor below or to the right then drew straight
; back over it: every block's top-left corner rounded and the other three did
; not.
; -----------------------------------------------------------------------------
dd_board_render:
    cmp word [dd_bdseg], 0          ; NO PICTURE, NO WALK (SPEC.md 93.3.4.5):
    jne .go                         ; every tile below is written through it
    ret
.go:
    ; --- THE WALK IS A GLOBAL AND SO IS THE PICTURE (SPEC.md 93.5.17) -------
    ; [dd_gc]/[dd_gr] are the loop, and dd_dots_blit and dd_tile_put both set
    ; them - so a frame drawn on the other task WHILE this walks makes the
    ; walk jump, and the maze comes out with holes in it that no later paint
    ; heals. Two flags and one yield: say so first, then wait for any frame
    ; already in flight. The worker skips a frame while this is set, so the
    ; wait is bounded by the frame that had already started.
    inc byte [dd_inrender]
.wait:
    cmp byte [dd_drawing], 0
    je .clear
    call OSAPI_TASK_YIELD
    jmp short .wait
.clear:
    call dd_bd_wipe
    mov bx, DD_INKB - 1             ; nothing carries a corner block yet
.zink:
    mov byte [dd_cnrmap + bx], 0
    dec bx
    jns .zink
    mov byte [dd_gr], 0
.rows:
    mov byte [dd_gc], 0
.cols:
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    call dd_tile
    cmp al, TT_WALL
    je .next                        ; a wall draws nothing of its own: it is
    mov [dd_gt], al                 ; its neighbours' outline
    call dd_tile_org                ; [dd_gx], [dd_gy] = the tile's top-left
    call dd_walls_of
    cmp byte [dd_gt], TT_DOOR
    jne .next
    call dd_door_draw
.next:
    inc byte [dd_gc]
    cmp byte [dd_gc], DD_COLS
    jb .cols
    inc byte [dd_gr]
    cmp byte [dd_gr], DD_ROWS
    jb .rows
    ; --- pass 2: the corners, every line they are made of now being down ----
    mov byte [dd_gr], 0
.rows2:
    mov byte [dd_gc], 0
.cols2:
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    call dd_iswall
    jnc .next2
    call dd_tile_org
    call dd_wall_round
.next2:
    inc byte [dd_gc]
    cmp byte [dd_gc], DD_COLS
    jb .cols2
    inc byte [dd_gr]
    cmp byte [dd_gr], DD_ROWS
    jb .rows2
    ; --- pass 3: the playfield's own outer line, which is no tile's job -----
    call dd_bord_draw
    dec byte [dd_inrender]
    ret

; dd_tile_org - [dd_gx],[dd_gy] = the board pixel of tile ([dd_gc],[dd_gr])
dd_tile_org:
    push ax
    push dx
    mov al, [dd_gc]
    xor ah, ah
    mul word [dd_tw]
    mov [dd_gx], ax
    mov al, [dd_gr]
    xor ah, ah
    mul word [dd_th]
    mov [dd_gy], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_walls_of - the outline this CORRIDOR tile owes, one line into each wall
;               that touches it. Clobbers AX/BX/CX/DX/BP.
;
; Each line is EXTENDED by the line thickness at an end whose perpendicular
; neighbour is also wall, which is the whole of how the corners close: two
; lines that both stop at the tile boundary leave the corner pixel unlit, and
; a maze full of pinholes is what that looks like on the glass.
;
; This is the INNER line only. The playfield's outer one is dd_bord_draw's,
; because it has to run straight past a stub of wall - and where a stub hangs
; off the ring there is no corridor tile to draw it from (93.2.4).
; -----------------------------------------------------------------------------
dd_walls_of:
    ; bank the four neighbours as bytes: 1 = wall
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    dec cl
    call dd_iswall
    call dd_cf_byte
    mov [dd_nL], al
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    inc cl
    call dd_iswall
    call dd_cf_byte
    mov [dd_nR], al
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    dec ch
    call dd_iswall
    call dd_cf_byte
    mov [dd_nT], al
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    inc ch
    call dd_iswall
    call dd_cf_byte
    mov [dd_nB], al
    mov dh, 1                       ; every wall line is ink
    ; --- left ---------------------------------------------------------------
    cmp byte [dd_nL], 0
    je .noL
    mov ax, [dd_gx]
    sub ax, [dd_lth]
    mov bx, [dd_gy]
    mov bp, [dd_th]
    cmp byte [dd_nT], 0
    je .lnt
    sub bx, [dd_lth]
    add bp, [dd_lth]
.lnt:
    cmp byte [dd_nB], 0
    je .lnb
    add bp, [dd_lth]
.lnb:
    mov cx, [dd_lth]
    call dd_bd_rect
.noL:
    ; --- right --------------------------------------------------------------
    cmp byte [dd_nR], 0
    je .noR
    mov ax, [dd_gx]
    add ax, [dd_tw]
    mov bx, [dd_gy]
    mov bp, [dd_th]
    cmp byte [dd_nT], 0
    je .rnt
    sub bx, [dd_lth]
    add bp, [dd_lth]
.rnt:
    cmp byte [dd_nB], 0
    je .rnb
    add bp, [dd_lth]
.rnb:
    mov cx, [dd_lth]
    call dd_bd_rect
.noR:
    ; --- top ----------------------------------------------------------------
    cmp byte [dd_nT], 0
    je .noT
    mov ax, [dd_gx]
    mov cx, [dd_tw]
    cmp byte [dd_nL], 0
    je .tnl
    sub ax, [dd_lth]
    add cx, [dd_lth]
.tnl:
    cmp byte [dd_nR], 0
    je .tnr
    add cx, [dd_lth]
.tnr:
    mov bx, [dd_gy]
    sub bx, [dd_lth]
    mov bp, [dd_lth]
    call dd_bd_rect
.noT:
    ; --- bottom -------------------------------------------------------------
    cmp byte [dd_nB], 0
    je .noB
    mov ax, [dd_gx]
    mov cx, [dd_tw]
    cmp byte [dd_nL], 0
    je .bnl
    sub ax, [dd_lth]
    add cx, [dd_lth]
.bnl:
    cmp byte [dd_nR], 0
    je .bnr
    add cx, [dd_lth]
.bnr:
    mov bx, [dd_gy]
    add bx, [dd_th]
    mov bp, [dd_lth]
    call dd_bd_rect
.noB:
    ret

; -----------------------------------------------------------------------------
; dd_wall_round - a WALL tile's four corners (SPEC.md 93.2.3)
;                 Clobbers AX/BX/CX/DX/BP.
;
; IT HAS TO BE THE WALL'S JOB. The outline is drawn by the CORRIDOR tiles, one
; line into each wall that touches them - so a block's corner is made by two
; lines from two DIFFERENT tiles, and neither of them can see that a corner is
; being formed. The first attempt at this looked for a corner square inside
; dd_walls_of and never fired once.
;
; The elbow is the wall tile's own corner square, because that is where the
; corridor drew: a line goes [dd_lth] INTO the wall from the shared edge, so
; the two lines that meet overlap exactly there. TWO corners meet there, not
; one, and this is the whole of the difference between them:
;
;   both adjacent neighbours OPEN  - a block's corner poking into the corridor,
;                                    and the arms run INTO the tile
;   both adjacent neighbours WALL, - the same elbow seen from the other side,
;   the DIAGONAL one open            the arms running OUT of it
;
; so a concave corner is the convex one with both signs flipped.
; -----------------------------------------------------------------------------
dd_wall_round:
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    dec ch
    call dd_iswall
    call dd_cf_byte
    mov [dd_nT], al
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    inc ch
    call dd_iswall
    call dd_cf_byte
    mov [dd_nB], al
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    dec cl
    call dd_iswall
    call dd_cf_byte
    mov [dd_nL], al
    mov cl, [dd_gc]
    mov ch, [dd_gr]
    inc cl
    call dd_iswall
    call dd_cf_byte
    mov [dd_nR], al

    mov al, [dd_nT]                 ; --- top-left ---
    mov [dd_rn1], al
    mov al, [dd_nL]
    mov [dd_rn2], al
    mov ax, [dd_gx]
    mov bx, [dd_gy]
    mov cx, [dd_lth]
    mov dx, cx
    call dd_rnd_one

    mov al, [dd_nT]                 ; --- top-right ---
    mov [dd_rn1], al
    mov al, [dd_nR]
    mov [dd_rn2], al
    mov ax, [dd_gx]
    add ax, [dd_tw]
    sub ax, [dd_lth]
    mov bx, [dd_gy]
    mov dx, [dd_lth]
    mov cx, dx
    neg cx
    call dd_rnd_one

    mov al, [dd_nB]                 ; --- bottom-left ---
    mov [dd_rn1], al
    mov al, [dd_nL]
    mov [dd_rn2], al
    mov ax, [dd_gx]
    mov bx, [dd_gy]
    add bx, [dd_th]
    sub bx, [dd_lth]
    mov cx, [dd_lth]
    mov dx, cx
    neg dx
    call dd_rnd_one

    mov al, [dd_nB]                 ; --- bottom-right ---
    mov [dd_rn1], al
    mov al, [dd_nR]
    mov [dd_rn2], al
    mov ax, [dd_gx]
    add ax, [dd_tw]
    sub ax, [dd_lth]
    mov bx, [dd_gy]
    add bx, [dd_th]
    sub bx, [dd_lth]
    mov cx, [dd_lth]
    neg cx
    mov dx, cx
    call dd_rnd_one
    ret

; -----------------------------------------------------------------------------
; dd_rnd_one - round ONE of a wall tile's corners, if it is a corner at all
; in:  AX, BX = the elbow's [dd_lth] square, CX, DX = the arm directions a
;      CONVEX corner would use, [dd_rn1]/[dd_rn2] = its two adjacent
;      neighbours (1 = wall). Preserves every register.
; -----------------------------------------------------------------------------
dd_rnd_one:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                      ; the elbow, out of AX and BX: every test
    mov di, bx                      ; below wants AL
    mov al, [dd_rn1]
    or al, [dd_rn2]
    jz .go                          ; both open: the block's own corner
    mov al, [dd_rn1]
    and al, [dd_rn2]
    jz .out                         ; one of each: no elbow is formed here
    mov al, [dd_gc]                 ; both wall - so this is a corner only if
    or cx, cx                       ; the tile DIAGONALLY between them is not,
    jns .dcl                        ; and the diagonal is one step against
    inc al                          ; each arm
    jmp short .drw
.dcl:
    dec al
.drw:
    mov ah, [dd_gr]
    or dx, dx
    jns .drd
    inc ah
    jmp short .dgo
.drd:
    dec ah
.dgo:
    push cx
    mov cl, al
    mov ch, ah
    call dd_iswall
    jc .solid                       ; solid: no line was ever drawn there
    call dd_ink_set                 ; the block goes back INSIDE that corridor
    pop cx                          ; tile, so a band over it owes the picture
    neg cx                          ; (93.2.3.2). The same elbow and the same
    neg dx                          ; two lines, seen from the other side
    jmp short .go
.solid:
    pop cx
    jmp short .out
.go:
    mov ax, si
    mov bx, di
    call dd_rnd_cnr
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_rnd_cnr - round the elbow whose [dd_lth] square has its top-left at
;              (AX, BX) and whose two arms run in the directions (CX, DX),
;              each of which is plus or minus [dd_lth]. Preserves everything.
;
; THE ARCADE'S OWN SHAPE, read off a pixel-perfect capture rather than guessed:
; at a block's corner the horizontal line stops two pixels short, one pixel
; sits on the diagonal, and the vertical starts two pixels down. So a round is
; a SQUARE TAKEN OUT and ONE BLOCK PUT BACK on the diagonal inside it - not an
; arc anybody has to walk, and not a chamfer either, a chamfer being what this
; degenerates to at [dd_rnd] = [dd_lth] where there is no room for the block.
; -----------------------------------------------------------------------------
dd_rnd_cnr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, ax
    add si, cx                      ; (SI, DI) = the block that goes back, one
    mov di, bx                      ; line width along each arm
    add di, dx
    mov bp, [dd_rnd]
    cmp bp, [dd_lth]
    je .clear                       ; one width: a chamfer, nothing goes back
    or cx, cx
    jns .rx
    mov ax, si                      ; an arm running LEFT or UP anchors the
.rx:                                ; square that comes out at its far end
    or dx, dx
    jns .clear
    mov bx, di
.clear:
    mov cx, bp
    xor dh, dh                      ; paper: the elbow comes out
    call dd_bd_rect
    cmp bp, [dd_lth]
    je .out
    mov ax, si
    mov bx, di
    mov cx, [dd_lth]
    mov bp, cx
    mov dh, 1                       ; ...and the diagonal block goes back in
    call dd_bd_rect
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
; dd_bord_draw - THE PLAYFIELD'S OUTER LINE, drawn as a PATH (SPEC.md 93.2.4)
;
; The inner line is the corridor tiles' own work and BREAKS where a stub of
; wall hangs off the ring - the pair at the top centre, the two lower ones -
; which is what the arcade does as well. The outer line does NOT break there,
; and so it cannot be drawn tile by tile at all: the tile that would draw it is
; exactly the one that is missing.
;
; It is a rectangle with a NOTCH cut into each side, because the tunnel's
; pocket is OUTSIDE the maze: the line follows the board's edge in, round the
; pocket and back out, and the tunnel's own mouth is the gap between the two
; horizontals that reach the edge of the picture. Every number below is a
; corridor's inner line plus [dd_bgap], on the side away from that corridor.
; -----------------------------------------------------------------------------
dd_bord_draw:
    mov ax, [dd_th]                 ; the four sides of the board
    call dd_bord_lo
    mov [dd_bYT], ax
    mov ax, DD_ROWS - 1
    mul word [dd_th]
    add ax, [dd_bgap]
    mov [dd_bYB], ax
    mov ax, [dd_tw]
    call dd_bord_lo
    mov [dd_bXL], ax
    mov ax, DD_COLS - 1
    mul word [dd_tw]
    add ax, [dd_bgap]
    mov [dd_bXR], ax
    mov ax, DD_PKT0                 ; the tunnel's pocket, top and bottom
    mul word [dd_th]
    add ax, [dd_bgap]
    mov [dd_bPT], ax
    mov ax, DD_PKT1 + 1
    mul word [dd_th]
    call dd_bord_lo
    mov [dd_bPB], ax
    mov ax, DD_TUNR                 ; ...and the mouth inside it
    mul word [dd_th]
    call dd_bord_lo
    mov [dd_bTT], ax
    mov ax, DD_TUNR + 1
    mul word [dd_th]
    add ax, [dd_bgap]
    mov [dd_bTB], ax

    mov ax, [dd_bXL]                ; the top and the bottom run the whole way
    mov bx, [dd_bYT]
    call dd_bord_wide
    mov ax, [dd_bXL]
    mov bx, [dd_bYB]
    call dd_bord_wide

    mov ax, DD_TUNW                 ; --- the left side and its notch ---
    mul word [dd_tw]
    call dd_bord_lo
    mov [dd_bXN], ax
    mov ax, [dd_bXL]
    mov [dd_bXA], ax
    xor ax, ax                      ; the mouth reaches the picture's edge
    mov [dd_bTX], ax
    mov ax, [dd_bXN]
    add ax, [dd_lth]
    mov [dd_bTW], ax
    mov ax, [dd_lth]
    mov [dd_bSA], ax                ; ...and from the side, the arms run right
    call dd_bord_side

    mov ax, DD_COLS - DD_TUNW       ; --- and the right, mirrored ---
    mul word [dd_tw]
    add ax, [dd_bgap]
    mov [dd_bXN], ax
    mov ax, [dd_bXR]
    mov [dd_bXA], ax
    mov ax, [dd_bXN]
    mov [dd_bTX], ax
    mov ax, [dd_mw]
    sub ax, [dd_bXN]
    mov [dd_bTW], ax
    mov ax, [dd_lth]
    neg ax
    mov [dd_bSA], ax
    call dd_bord_side
    ret

; -----------------------------------------------------------------------------
; dd_bord_lo - AX = a tile edge whose CORRIDOR is on the high side of it, so
;              the inner line is on the low side and the outer one is further
;              down still. The other case is a bare "add [dd_bgap]", the inner
;              line then being the [dd_lth] just above the edge.
; -----------------------------------------------------------------------------
dd_bord_lo:
    sub ax, [dd_lth]
    sub ax, [dd_bgap]
    ret

; dd_bord_wide - one full-width horizontal at (AX, BX)
dd_bord_wide:
    mov cx, [dd_bXR]
    sub cx, ax
    add cx, [dd_lth]
    mov bp, [dd_lth]
    mov dh, 1
    call dd_bd_rect
    ret

; -----------------------------------------------------------------------------
; dd_bord_side - one side of the border and its notch, from the banked numbers
; [dd_bXA] the side's own line, [dd_bXN] the notch's, [dd_bSA] the direction
; the notch runs in (plus or minus [dd_lth]), [dd_bTX]/[dd_bTW] the mouth.
; -----------------------------------------------------------------------------
dd_bord_side:
    mov dh, 1
    mov ax, [dd_bXA]                ; down to the pocket
    mov bx, [dd_bYT]
    mov cx, [dd_lth]
    mov bp, [dd_bPT]
    sub bp, bx
    add bp, [dd_lth]
    call dd_bd_rect
    mov ax, [dd_bXA]                ; ...and up from it
    mov bx, [dd_bPB]
    mov cx, [dd_lth]
    mov bp, [dd_bYB]
    sub bp, bx
    add bp, [dd_lth]
    call dd_bd_rect
    mov bx, [dd_bPT]                ; the two horizontals of the pocket
    call dd_bord_notch
    mov bx, [dd_bPB]
    call dd_bord_notch
    mov ax, [dd_bXN]                ; the notch's own line, above the mouth
    mov bx, [dd_bPT]
    mov cx, [dd_lth]
    mov bp, [dd_bTT]
    sub bp, bx
    add bp, [dd_lth]
    call dd_bd_rect
    mov ax, [dd_bXN]                ; ...and below it
    mov bx, [dd_bTB]
    mov cx, [dd_lth]
    mov bp, [dd_bPB]
    sub bp, bx
    add bp, [dd_lth]
    call dd_bd_rect
    mov ax, [dd_bTX]                ; the mouth, out to the picture's edge
    mov bx, [dd_bTT]
    mov cx, [dd_bTW]
    mov bp, [dd_lth]
    call dd_bd_rect
    mov ax, [dd_bTX]
    mov bx, [dd_bTB]
    mov cx, [dd_bTW]
    mov bp, [dd_lth]
    call dd_bd_rect
    ; --- and the eight corners the path just made ---------------------------
    mov ax, [dd_bXA]
    mov bx, [dd_bYT]
    mov cx, [dd_bSA]
    mov dx, [dd_lth]
    call dd_rnd_cnr
    mov ax, [dd_bXA]
    mov bx, [dd_bYB]
    mov cx, [dd_bSA]
    mov dx, [dd_lth]
    neg dx
    call dd_rnd_cnr
    mov ax, [dd_bXA]
    mov bx, [dd_bPT]
    mov cx, [dd_bSA]
    mov dx, [dd_lth]
    neg dx
    call dd_rnd_cnr
    mov ax, [dd_bXA]
    mov bx, [dd_bPB]
    mov cx, [dd_bSA]
    mov dx, [dd_lth]
    call dd_rnd_cnr
    mov ax, [dd_bXN]
    mov bx, [dd_bPT]
    mov cx, [dd_bSA]
    neg cx
    mov dx, [dd_lth]
    call dd_rnd_cnr
    mov ax, [dd_bXN]
    mov bx, [dd_bPB]
    mov cx, [dd_bSA]
    neg cx
    mov dx, [dd_lth]
    neg dx
    call dd_rnd_cnr
    mov ax, [dd_bXN]
    mov bx, [dd_bTT]
    mov cx, [dd_bSA]
    neg cx
    mov dx, [dd_lth]
    neg dx
    call dd_rnd_cnr
    mov ax, [dd_bXN]
    mov bx, [dd_bTB]
    mov cx, [dd_bSA]
    neg cx
    mov dx, [dd_lth]
    call dd_rnd_cnr
    ret

; dd_bord_notch - the horizontal at row BX between the side and the notch
dd_bord_notch:
    push bx
    mov ax, [dd_bXA]
    mov cx, [dd_bXN]
    cmp ax, cx
    jbe .lo
    xchg ax, cx
.lo:
    sub cx, ax
    add cx, [dd_lth]
    mov bp, [dd_lth]
    mov dh, 1
    call dd_bd_rect
    pop bx
    ret

; dd_cf_byte - AL = 1 when CF was set, 0 when it was not
dd_cf_byte:
    mov al, 0
    jnc .out
    mov al, 1
.out:
    ret

; The door: a bar across the tile, so the house reads as closed from outside.
dd_door_draw:
    mov ax, [dd_gx]
    mov bx, [dd_th]
    sub bx, [dd_lth]
    shr bx, 1
    add bx, [dd_gy]
    mov cx, [dd_tw]
    mov bp, [dd_lth]
    mov dh, 1
    call dd_bd_rect
    ret

; -----------------------------------------------------------------------------
; dd_eat_tile - take the dot or pellet at column CL, row CH off the board and
;               answer what it was in AL (TT_EMPTY when there was nothing)
; Clobbers AX/BX/DX.
;
; IT DRAWS NOTHING, and that is the whole point of keeping the dots out of the
; wall picture: the tile Smiles just ate is inside Smiles' own band, and the
; band is composed from dd_grid, so the next frame carries the absence without
; anybody erasing anything.
; -----------------------------------------------------------------------------
dd_eat_tile:
    call dd_tile
    cmp al, TT_DOT
    je .go
    cmp al, TT_PILL
    je .go
    mov al, TT_EMPTY
    ret
.go:
    push cx
    push ax
    mov bl, cl
    xor bh, bh
    mov al, ch
    xor ah, ah
    mov cl, DD_COLS
    mul cl
    add bx, ax
    mov byte [dd_grid + bx], TT_EMPTY
    dec word [dd_ndots]
    pop ax
    pop cx
    ret

; --- the layouts --------------------------------------------------------------

%include "ddmzdat.inc"
