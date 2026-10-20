; =============================================================================
; os8088 - apps/dotdel/ddlay.inc
;
; THE LAYOUT (SPEC.md 93.3): how a 28x31 grid becomes pixels on four adapters
; whose pixels are four different shapes.
;
; The grid never changes - a maze that lost columns on a CGA would be a
; different game, and the AI, the dot count and the ghost house would all have
; to be per-adapter. What changes is the TILE, computed here from two numbers:
; the live content box, and the adapter's PIXEL ASPECT.
;
; --- THE ASPECT IS THE POINT, AND IT IS 1:1 NOWHERE BUT VGA ------------------
;
; A 4:3 monitor showing 640x480 has square pixels; showing 640x200 it has
; pixels 2.4x taller than they are wide, and 720x348 gives 1.55. A board laid
; out in square TILES is therefore three different SHAPES on the glass, and
; the one the eye reads as "a maze chase board" is the portrait one. So the
; tile HEIGHT is chosen first, from the height available, and the tile WIDTH
; is the largest of {8, 16} that keeps the tile within 1.3x of square on the
; glass.
;
; TWO RATIOS, AND THEY ARE NOT THE SAME NUMBER (SPEC.md 93.3.2). This table
; used to give one column called "board" that was the TILE's ratio in two rows
; of four and the BOARD's in the other two, which is a difference of 11% on a
; Hercules. The tile's ratio is what makes movement faster along one axis
; (93.7.6); the board's is what the eye calls the shape, and it is the tile's
; times 28/31.
;
;                                          tile     TILE      BOARD
;   VGA  640x480  aspect 100  ->  16 x 13  448x403  1.23 : 1  1.11 : 1
;   HERC 720x348  aspect 155  ->  16 x  9  448x279  1.15 : 1  1.04 : 1
;   EGA  640x350  aspect 137  ->  16 x  9  448x279  1.30 : 1  1.17 : 1
;   CGA  640x200  aspect 240  ->   8 x  4  224x124  0.83 : 1  0.75 : 1
;
; ...and in a fullscreen bracket the same arithmetic gives a HERCULES 16 x 11
; and a board of 448 x 341, which is 0.85 : 1 - and the ARCADE's own maze is
; 0.871 (28x31 tiles of 8x8 on a portrait monitor whose pixels are 0.964
; square). So the closest thing to the real machine's proportions that this
; game draws is a fullscreen Hercules, and it gets there by the tile being
; TALLER, never by its being narrower. That is the whole argument for sizing
; from the surface rather than from a table.
;
; --- WHY THE TILE WIDTH IS A MULTIPLE OF EIGHT (binding) ---------------------
;
; `gfx_blit1` takes a destination x that is a multiple of 8 (SPEC.md 5.4.2),
; so an actor's band is its box rounded OUT to the byte grid. Rounding out is
; free when the extra columns hold something the band is allowed to draw, and
; expensive when they hold something it is not: a band is put down in ONE pen,
; so a wall inside it would be drawn in the actor's own colour.
;
; A tile width that is a multiple of 8 - with the board's own screen x a
; multiple of 8 - removes the case entirely:
;
;   * moving VERTICALLY, the actor's x is its column's x, which is already on
;     the grid, so the band is exactly the actor's own columns and reaches
;     neither wall beside it;
;   * moving HORIZONTALLY, the band rounds out along the corridor it is in,
;     and a corridor's neighbours along its own axis are corridor.
;
; So the only static ink a band can ever contain is DOTS, and SPEC.md 93.5.4
; is what that costs on a colour adapter.
; =============================================================================

; Pixel aspect as (pixel height / pixel width) * 100, indexed by VID_* kind:
; VGA, Hercules, CGA, EGA.
;
; TWO TABLES, AND IT IS A RUNTIME CHOICE (SPEC.md 93.3.3.1). The number is a
; claim about the MONITOR, and the monitor is not something a program can ask
; about: a 4:3 Hercules tube really does make a pixel 1.55x taller than wide,
; and an emulator showing the same 720x348 in a square-pixel window really does
; not. Both are true of somebody, so Game has an item for each.
;
; The table is read in exactly two places and BOTH follow the choice, which is
; why this is one number and not two: the tile's shape, and the vertical step
; that makes across and down the same speed (93.7.6).
;
; CGA IS 240 IN BOTH, and it earned the exception. 640x200 has not got 279
; lines, so its tile stays 8x4 whatever this says and the value only reaches
; the SPEED there - where at 100 a returning pair of eyes moved 60 of a tile's
; 64 sixteenths in one tick. That is dd_advance being asked to find two
; crossings in one step, and what it looks like is not a fast ghost: the demo
; froze, the score went backwards and the frame fell to 24% of a tick. At 240
; it is 24 of 64, and 240 is what a real CGA monitor does anyway.
dd_asp43:  db 100, 155, 240, 137    ; a 4:3 tube
dd_asppx:  db 100, 100, 240, 100    ; ...and what a square-pixel window shows
DD_ASPPIX equ 1                 ; ...and 1 = the first three are what the GLASS
                                ; shows, not what a 4:3 tube would

; -----------------------------------------------------------------------------
; dd_layout - fill every derived geometry word from dd_cx/cy/cw/ch + dd_vkind
; out: CF = 1 the surface is too small for a board; every register clobbered.
; -----------------------------------------------------------------------------
dd_layout:
    ; --- the tile height: as tall as 31 rows of it will fit, capped ----------
    mov ax, [dd_ch]
    cmp ax, DD_ROWS * DD_THMAX
    jb .divh
    mov ax, DD_THMAX
    jmp short .thok
.divh:
    mov bl, DD_ROWS
    div bl                          ; AL = ch / 31, and ch < 31*16 = 496 so
    xor ah, ah                      ; the quotient cannot overflow AL
.thok:
    cmp ax, 3
    jb .tiny                        ; under three rows to a tile there is no
    mov [dd_th], ax                 ; board worth drawing

    ; --- the aspect for the display we are on -------------------------------
    mov bl, [dd_vkind]
    xor bh, bh
    cmp bl, 4
    jb .kok
    xor bx, bx
.kok:
    ; --- THIN or FULL, resolved HERE and not at the menu (93.3.3.1) --------
    ; The window can be dragged from one display to another, so this is a
    ; runtime answer: a bracket owns the whole screen and wants FULL, and a CGA
    ; has no Thin to offer at all - 200 lines cannot give 31 rows of a taller
    ; tile, so both modes come out 8x4 there. dd_i_win's first item is pointed
    ; at the greyed string to say so, which is 12.2's way of saying it and
    ; SPEC.md 47 rule 3's requirement that a greyed item name its reason.
    call dd_weff_calc
    mov si, dd_asp43
    cmp byte [dd_weff], 0
    jne .asptab
    mov si, dd_asppx
.asptab:
    mov al, [si + bx]
    xor ah, ah
    mov [dd_asp], ax

    ; --- the tile is within 1.2x of square ON THE GLASS, and as large as ---
    ; fits (SPEC.md 93.3.3). It was 1.3x, and the whole of what that extra
    ; tenth allowed was a windowed Hercules keeping a 16-wide tile against a
    ; 9-tall one: 1.147 on a 4:3 tube, which is fine, and 1.78 on a square
    ; pixel display, which is a board 1.6 times wider than tall to look at.
    ;
    ; BOTH TESTS READ [dd_asp], so both follow the Game menu's choice, and that
    ; is the whole mechanism - the rest of this file did not have to change.
    ; A Hercules at 155 comes out 16x9 exactly as it always did, and at 100 it
    ; comes out 8x9: a board of 224x279 against the arcade's 224x248.
    ; ...AND THE TOLERANCE IS THE MODE'S OWN (SPEC.md 93.3.3.3). 1.2 is Thin's,
    ; because Thin exists to hold the arcade's proportion; Full's is the 1.3
    ; this was before the aspect work, because Full exists to FILL THE WINDOW
    ; and 1.3 is exactly what it used to allow. On a VGA the two aspect tables
    ; agree at 100, so 1.2 made Full a no-op there: a 544x417 content box gives
    ; th = 13, a 16-wide tile is 1.23 of it, and the whole mode fell back to
    ; Thin's 8x9 with the window merely bigger round it.
    mov ax, 12
    cmp byte [dd_weff], 0
    je .tolok
    mov ax, 13
.tolok:
    mov [dd_tol], ax
    mov word [dd_tw], DD_TWMAX
    mov ax, DD_COLS * DD_TWMAX + DD_HUDW
    cmp ax, [dd_cw]
    ja .narrow
    mov ax, [dd_th]
    mul word [dd_asp]               ; at most 16 * 240 = 3,840
    mul word [dd_tol]               ; at most 49,920 - still one word
    cmp ax, DD_TWMAX * 1000
    jae .twok
.narrow:
    mov word [dd_tw], DD_TWMIN
    mov ax, DD_COLS * DD_TWMIN + DD_HUDW
    cmp ax, [dd_cw]
    ja .tiny
.twok:
    ; --- and cap the height by the same tolerance the other way ------------
    ; th  <=  tw * 100 * [dd_tol] / (aspect * 10)
    mov ax, [dd_tw]
    mul word [dd_tol]
    mov bx, 100
    mul bx                          ; at most 20,800
    mov bx, [dd_asp]
    mov cx, bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, cx
    add bx, cx                      ; BX = aspect * 10, without a second mul
    div bx
    cmp ax, [dd_th]
    jae .hok
    cmp ax, 3
    jb .tiny
    mov [dd_th], ax
.hok:

    ; --- the derived extents ------------------------------------------------
    mov ax, [dd_tw]
    mov bx, DD_COLS
    mul bx
    mov [dd_mw], ax                 ; board width in px, a multiple of 8
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [dd_sb], ax                 ; ...and the board picture's row stride
    mov ax, [dd_th]
    mov bx, DD_ROWS
    mul bx
    mov [dd_mh], ax                 ; board height in px

    ; --- where it sits ------------------------------------------------------
    ; THE BOARD IS CENTRED and the HUD lives in the margin beside it, whenever
    ; that margin is wide enough to hold one. It is the board the eye reads,
    ; and a board pushed right by the width of a score column looks like a
    ; mistake on a screen with 96 px of air on the other side. When the margin
    ; is too narrow the two are packed side by side and centred together,
    ; which is the only thing a narrow surface can do.
    mov ax, [dd_cw]
    sub ax, [dd_mw]
    jns .m0
    xor ax, ax
.m0:
    shr ax, 1
    mov bx, ax                      ; BX = the margin each side
    cmp ax, DD_HUDW + 8
    jb .packed
    add ax, [dd_cx]
    add ax, 7
    and ax, ~7                      ; **THE BOARD'S x IS ON THE BYTE GRID**,
    mov [dd_bdx], ax                ; which is what makes every column of it a
    sub bx, DD_HUDW                 ; legal gfx_blit1 destination
    shr bx, 1
    add bx, [dd_cx]
    mov [dd_hx], bx
    jmp short .vert
.packed:
    mov ax, [dd_cw]
    sub ax, DD_HUDW
    sub ax, [dd_mw]
    jns .padok
    xor ax, ax
.padok:
    shr ax, 1
    add ax, [dd_cx]
    mov [dd_hx], ax                 ; the HUD column's own left edge
    add ax, DD_HUDW
    add ax, 7
    and ax, ~7
    mov [dd_bdx], ax
.vert:
    mov ax, [dd_ch]
    sub ax, [dd_mh]
    jns .vpadok
    xor ax, ax
.vpadok:
    shr ax, 1
    add ax, [dd_cy]
    mov [dd_bdy], ax
    mov ax, [dd_cy]
    add ax, 6
    mov [dd_hy], ax

    ; --- and how far apart the HUD's GROUPS sit -----------------------------
    ; Seven rows of DD_HUDROW (three label/number pairs and the lives label),
    ; three gaps between the groups, the lives band itself and dd_hy's own 6.
    ; NOTHING CHECKS THE HUD'S HEIGHT anywhere else - the column just runs
    ; down from dd_hy - so the wide gap is taken only when the arithmetic says
    ; it lands inside the content box, and a window too short for it gets the
    ; spacing it had before rather than a lives row off the bottom.
    mov word [dd_hgap], DD_HUDGAP
    mov ax, 6 + DD_HUDROW * 7 + DD_HUDGAP * 3
    add ax, [dd_th]
    cmp ax, [dd_ch]
    jbe .hgapok
    mov word [dd_hgap], DD_HUDGAPMIN
.hgapok:

    ; --- the sub-pixel speeds (SPEC.md 93.7.1) ------------------------------
    ; One tile in DD_TILET ticks ACROSS, so the game runs at the same tiles per
    ; second on every adapter and the pixel rate follows the tile.
    mov ax, [dd_tw]
    mov bx, DD_SUB
    mul bx
    mov bl, DD_TILET
    div bl
    xor ah, ah
    mov [dd_spx], ax
    ; DOWN is not computed here at all - it is each horizontal step converted,
    ; in the loop below (SPEC.md 93.7.2.1).
    ; --- ...and the five speeds against both axes, ONCE (SPEC.md 93.7.5) ----
    ; dd_axis_step used to scale a base speed by a percentage with a MUL and a
    ; DIV, per actor per tick.  There are five speeds in this game, so there
    ; are ten answers; they are resolved here, where the tile size is being
    ; decided anyway, and dd_axis_step indexes them.
    push si
    xor si, si
.spd:
    mov bx, si
    shl bx, 1
    push bx
    mov ax, [dd_spx]
    mul word [dd_spct + bx]
    mov cx, 100                     ; MUL has set DX:AX and DIV wants both:
    div cx                          ; clearing DX here would discard the high
                                    ; half of the product
    or ax, ax
    jnz .sxok
    mov ax, 1                       ; never nothing: a rounded-down speed that
.sxok:                              ; reached zero would be a frozen actor
    pop bx
    push bx
    mov [dd_stpx + bx], ax
    ; --- ...and DOWN is THAT STEP converted to the glass (SPEC.md 93.7.2.1) ---
    ; A tile is not square on the glass anywhere - 1.15 : 1 on a Hercules,
    ; 1.23 on a VGA, 1.30 on an EGA and 0.83 on a CGA - so one tile in
    ; DD_TILET ticks EITHER WAY is an actor that travels that much faster
    ; along its longer axis. The field read it exactly as it is: "moving
    ; across is faster than vertical ... the projection is slightly skewed
    ; horizontally", and then "the horizontal speed FEELS RIGHT and the
    ; vertical a bit slow" - which is why across is the reference and down is
    ; brought up to it, rather than the other way round.
    ;
    ; The tile cannot be narrowed to fix it. dd_tw is a MULTIPLE OF EIGHT by
    ; the band rule at the top of this file, and the only step below 16 is 8 -
    ; a board of 224 where 14 is what squares a Hercules.
    ;
    ; PER SPEED and not once, because rounding is the whole difficulty on the
    ; adapter with the smallest steps: a CGA's frightened ghost moves SEVEN
    ; sixteenths of a pixel a tick, so one unit is 13% of it, and converting
    ; the base speed once and then taking 60% of THAT left the two axes 13%
    ; apart on the adapter that had least to spare.
    mov bx, 100
    mul bx                          ; DX:AX = stpx * 100, at most 12,100
    mov cx, [dd_asp]
    shr cx, 1
    add ax, cx                      ; ...to NEAREST, and 12,220 cannot carry
    div word [dd_asp]               ; out of AX, so the MUL's DX still stands
    or ax, ax
    jnz .synz
    mov ax, 1
.synz:
    ; ...AND NEVER MORE THAN HALF A TILE (SPEC.md 93.7.2.1). dd_advance finds one
    ; crossing per step, so a step near a whole tile asks it to find two - and
    ; what that looks like is not a fast ghost, it is a frozen demo and a score
    ; going backwards. The margin used to be 1.7x by arithmetic; it is enforced
    ; here now, because the arithmetic changed under it once.
    mov cx, [dd_th]
    mov bx, 8
    push ax
    mov ax, cx
    mul bx                          ; AX = th * 8, which is half a tile in
    mov cx, ax                      ; sixteenths
    pop ax
    cmp ax, cx
    jbe .syok
    mov ax, cx
.syok:
    pop bx
    mov [dd_stpy + bx], ax
    inc si
    cmp si, SPD_N
    jb .spd
    pop si
    ; the sub-pixel extent of one tile, which every wrap and every centring
    ; test below is measured in
    mov ax, [dd_tw]
    mov cl, 4
    shl ax, cl
    mov [dd_twS], ax
    mov ax, [dd_th]
    shl ax, cl
    mov [dd_thS], ax
    mov ax, [dd_mw]
    shl ax, cl
    mov [dd_mwS], ax
    call dd_metrics
    clc
    ret
.tiny:
    stc
    ret

; -----------------------------------------------------------------------------
; dd_metrics - how thick a wall line is, and how big a dot and a pellet are
; Every one is derived from the tile, so a windowed CGA's 8x4 and a fullscreen
; VGA's 16x15 get the same picture at two sizes rather than two pictures.
; -----------------------------------------------------------------------------
dd_metrics:
    mov word [dd_lth], 1            ; TWO pixels of wall once the tile has room
    mov ax, [dd_th]                 ; for two ROUNDS as well, which is six line
    cmp ax, 12                      ; widths (93.2.3) - so a windowed Full's
    jb .lok                         ; 16x9 keeps a 1px line and gets the arcade
    cmp word [dd_tw], 16            ; round, and only a fullscreen tile is
    jb .lok                         ; thick. It was 8 rows; the field's A/B on
    mov word [dd_lth], 2            ; the glass is what moved it (93.2.3.3)
.lok:
    mov cx, [dd_lth]                ; THE BORDER'S GAP (93.2.4) and THE CORNER
    mov ax, cx                      ; ROUND (93.2.3), both off the line width
    shl ax, 1
    add ax, cx                      ; three widths: the outer border line sits
    mov bx, [dd_th]                 ; that far out, leaving two of black, which
    sub bx, cx                      ; is the arcade's own proportion - but never
    cmp ax, bx                      ; past the WALL RING, which is one tile, so
    jbe .gok                        ; a three-row tile keeps its top line on
    mov ax, bx                      ; the picture instead of clipping it away
.gok:
    mov [dd_bgap], ax
    mov ax, cx
    shl ax, 1
    add ax, cx
    shl ax, 1                       ; AX = six widths
    mov [dd_rnd], cx                ; the round is ONE width...
    cmp [dd_tw], ax
    jb .rok
    cmp [dd_th], ax
    jb .rok
    shl cx, 1                       ; ...and TWO where a one-tile wall still
    mov [dd_rnd], cx                ; has a width of line left between the two
.rok:                               ; rounded ends of it - six widths to a tile
    mov ax, [dd_tw]                 ; a dot is a fifth of the tile, never under
    mov bl, 5                       ; two pixels wide or one tall
    div bl
    xor ah, ah
    cmp ax, 2
    jae .dw
    mov ax, 2
.dw:
    mov [dd_dotw], ax
    mov ax, [dd_th]
    mov bl, 4
    div bl
    xor ah, ah
    or ax, ax
    jnz .dh
    mov ax, 1
.dh:
    mov [dd_doth], ax
    mov ax, [dd_tw]                 ; a pellet is half of it
    shr ax, 1
    cmp ax, 3
    jae .pw
    mov ax, 3
.pw:
    mov [dd_pilw], ax
    mov ax, [dd_th]
    shr ax, 1
    cmp ax, 2
    jae .ph
    mov ax, 2
.ph:
    mov [dd_pilh], ax
    ret

; -----------------------------------------------------------------------------
; dd_weff_calc - resolve Window > Thin/Full for the display we are on NOW
; out: [dd_weff] = 0 Thin, 1 Full, and dd_i_win's first item greyed or not.
;      Clobbers AX. Preserves everything else.
;
; It is its own proc because TWO callers need the answer and only one of them
; paints: dd_layout, and the menu handler - which does NOT repaint when a
; resize is owed (SPEC.md 93.3.4.1), so without this the pick set [dd_wmode]
; and nothing ever turned it into [dd_weff].
;
; A DISPLAY CHANGE DOES NOT ASK FOR A FIT, which is why this only resolves and
; never sets [dd_wantfit] (93.3.3.2): dragging between adapters changes the
; effective mode, and asking for a resize there PRE-EMPTS the window manager's
; own - OS88_PREFER gives this window 520 rows and the WM grows it when it
; lands on a Hercules, so a fit computed from the CGA-sized box it still had
; pinned it there and the tile never re-cut. A fit belongs to a menu pick.
; -----------------------------------------------------------------------------
dd_weff_calc:
    mov ax, dd_it_thin
    cmp byte [dd_vkind], VID_CGA
    jne .thin
    mov ax, dd_it_thind
.thin:
    mov [dd_i_win], ax
    mov al, [dd_wmode]
    cmp byte [dd_fsx], 0
    jne .full
    cmp byte [dd_vkind], VID_CGA
    jne .set
.full:
    mov al, 1
.set:
    mov [dd_weff], al
    ret

; -----------------------------------------------------------------------------
; dd_relayout_ck - re-lay out only when something that decides the layout moved
; out: nothing; clobbers everything. Safe to call from any paint.
;
; Three things can change under a live window - its SIZE, its POSITION and the
; DISPLAY it is on - and only the first changes the tile. A move between two
; displays of different depth changes the PENS and not one coordinate, which
; is why the depth is banked in dd_geom_* and is not a reason to re-render the
; board picture: the picture is one bit a pixel and carries no colour at all.
; -----------------------------------------------------------------------------
dd_relayout_ck:
    cmp word [dd_bdseg], 0
    je .want                        ; nothing has ever been laid out
    mov al, [dd_vkind]
    cmp al, [dd_lvkind]
    jne .want                       ; a different CARD is a different pixel
    mov ax, [dd_cw]                 ; shape, and so a different tile
    cmp ax, [dd_lcw]
    jne .want
    mov ax, [dd_ch]
    cmp ax, [dd_lch]
    jne .want
    mov ax, [dd_cx]
    cmp ax, [dd_lcx]
    jne .move
    mov ax, [dd_cy]
    cmp ax, [dd_lcy]
    jne .move
    clc
    ret
; -----------------------------------------------------------------------------
; A RECUT IS THE UI TASK'S, ALWAYS (SPEC.md 93.3.4.3)
;
; The worker asks the same question every frame - it has to, because a drag
; tells a package nothing - but it may only take the MOVE arm. Two tasks in
; .redo at once is what the field saw as "it redraws twice and the second
; redraw has the wrong origin" and as Smiles flickering between two sizes: the
; tile is a global, so a sprite cut while the other task had already moved
; [dd_tw] comes out at the OTHER width, and the bank ends up half one and half
; the other.
; -----------------------------------------------------------------------------
.want:
    or bl, bl
    jz .redo
    mov byte [dd_needcut], 1        ; ...so the worker asks for one instead
    stc
    ret
.move:
    mov byte [dd_didcut], 1         ; ...and SOMETHING was owed after all
    call dd_bank_box                ; only the origin moved: the tile, the
    call dd_layout                  ; picture and the sprites all stand
    jc .fail
    mov byte [dd_ok], 1
    ; EVERY POSITION IN THIS FILE IS ABSOLUTE, so dd_layout has just moved
    ; dd_bdx, dd_hx and the rest onto the new origin - and dd_attract_layout's
    ; are absolute too and are recomputed nowhere else (SPEC.md 93.3.4).
    ;
    ; AND NOTHING IS REPAINTED, which is the whole point: the kernel's drag
    ; cache has ALREADY moved this window's pixels to the new place. They are
    ; correct there. What was stale is only the arithmetic, and this is it.
    call dd_is_attract
    jnc .mvout
    call dd_attract_layout
.mvout:
    mov byte [dd_boxd], 1           ; ...except the GAME OVER panel, whose pen
    clc                             ; is banked at draw time (93.12.4.1)
    ret
.redo:
    mov byte [dd_didcut], 1
    call dd_bank_box
    call dd_layout
    jc .fail
    call dd_fit_claim               ; claim it, or grow what is already there
    jc .nomem
    call dd_spr_build               ; the sprite set is cut to the new tile
    mov byte [dd_ok], 1
    cmp byte [dd_started], 0
    jne .again
    mov byte [dd_started], 1        ; the FIRST layout is also the first board:
    call dd_attract_begin           ; it lays the slice and renders it
    clc
    ret
.again:
    call dd_actors_resnap           ; THE TILE CHANGED, so every actor's
                                    ; position is in the old one's units
    call dd_is_attract              ; ...and every later one re-draws it at the
    jnc .board                      ; new tile - or, on the title screen, cuts
    call dd_attract_begin           ; the slice again, which decides how much
    clc                             ; of the board there even is
    ret
.board:
    call dd_board_render
    clc
    ret
; -----------------------------------------------------------------------------
; A REFUSAL POISONS THE BANK (SPEC.md 93.3.4.5). .redo banked the box BEFORE
; the claim that refused, so without this the four compares above would match
; for the rest of the session: the worker would find nothing to recut, and a
; regrow that was refused would leave [dd_bdseg] naming the OLD claim under the
; NEW geometry. [dd_lvkind] = 0FFh is the file's own "ask again" (dd_oncmd's
; window-mode arm), and it sends the next paint back through .want/.redo, which
; re-asks the claim - so a refusal recovers the moment the arithmetic does.
; -----------------------------------------------------------------------------
.nomem:
    mov byte [dd_nomem], 1          ; the ARENA refused, not the surface -
    jmp short .failed               ; dd_draw_toosmall says which
.fail:
    mov byte [dd_nomem], 0
.failed:
    mov byte [dd_ok], 0
    mov byte [dd_lvkind], 0FFh
    clc
    ret

dd_bank_box:
    mov al, [dd_vkind]
    mov [dd_lvkind], al
    mov ax, [dd_cx]
    mov [dd_lcx], ax
    mov ax, [dd_cy]
    mov [dd_lcy], ax
    mov ax, [dd_cw]
    mov [dd_lcw], ax
    mov ax, [dd_ch]
    mov [dd_lch], ax
    ret

; dd_bdkb_calc - AX = KB the board picture needs at the current layout
dd_bdkb_calc:
    push dx
    push cx
    mov ax, [dd_sb]
    mul word [dd_mh]                ; 56 * 496 = 27,776: one word, just
    add ax, 1023
    adc dx, 0
    mov cl, 10
.sh:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .sh
    or ax, ax
    jnz .ok
    mov ax, 1
.ok:
    pop cx
    pop dx
    ret

; -----------------------------------------------------------------------------
; dd_fit_claim - make the board picture's claim big enough for this layout,
;                claiming it the first time and growing it after
; out: CF = 1 refused. Clobbers AX/BX/CX/DX.
;
; It is claimed rather than declared in bss for one reason: 56 bytes of stride
; by 496 rows is 27 KB and a windowed CGA needs 3.4 of them, so a package that
; reserved the ceiling would charge every machine for the largest board any
; machine could show - and APP_MAX_SIZE is 60 KB for the image and the bss
; together.
;
; ...AND IT GIVES THE DIFFERENCE BACK when a re-cut needs less. It used to
; only grow, so one trip into the bracket held the fullscreen picture for the
; rest of the session: 11 KB where the window needs 4 on the 128KB machine's
; CGA, 19 where it needs 8 on a Hercules (SPEC.md 24.5.5). A shrink always
; succeeds in place (OSAPI_MEM_REGROW), and every caller re-renders the picture
; at the new layout straight after, so nothing in the tail is ever read again.
; -----------------------------------------------------------------------------
dd_fit_claim:
    call dd_bdkb_calc
    cmp word [dd_bdseg], 0
    jne .grow
    call OSAPI_MEM_CLAIM            ; out DX = the base, AX = KB given
    jc .no
    mov [dd_bdseg], dx
    mov [dd_bdkb], ax
    clc
    ret
.grow:
    cmp ax, [dd_bdkb]
    je .fits
    push ax
    mov dx, [dd_bdseg]
    call OSAPI_MEM_REGROW           ; DX = the claim's base NOW - a regrow may
    pop cx                          ; move it, and every reader goes through
    jc .no                          ; [dd_bdseg]
    mov [dd_bdseg], dx
    mov [dd_bdkb], cx
.fits:
    clc
    ret
.no:
    stc
    ret
