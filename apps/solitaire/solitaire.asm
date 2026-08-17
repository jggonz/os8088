; =============================================================================
; os8088 - apps/solitaire/solitaire.asm
;
; Klondike Solitaire, the eighth software package (SPEC.md 43). Not kernel
; code: a .o88 binary at org 0 that owns a segment (SPEC.md 20.1) and reaches
; every service through the API table in os88api.inc. Prefix `sol_`, embedded
; icon (flags bit 0).
;
; Three things shape the whole program:
;
;  - **A drag is an XOR outline, exactly like a window drag.** No card moves
;    while the pointer does: sol_drag is ui_drag's loop (SPEC.md 13) written
;    against the API - draw the outline, linger a tick, erase, unlock, yield,
;    lock, re-read the mouse, redraw. Nothing is repainted until the button
;    comes up, so a drag costs four thin XOR strips a tick however many cards
;    are in the hand, and a 4.77MHz machine keeps up with the mouse. The
;    outline carries one internal rule per card boundary, so a seven-card
;    hand still reads as seven cards rather than one tall one.
;
;  - **Faces are drawn, backs are blitted.** A face is two fills, four edges,
;    one or two font glyphs and two suit pips run-coalesced out of 1-bit
;    masks. A back is a lattice with no runs worth speaking of, so it is
;    rendered ONCE into a packed 4bpp image at start-up and every later draw
;    is a single OSAPI_GFX_BLIT4 (SPEC.md 5.4) - one far call instead of
;    several hundred, twenty-one times over on a full repaint.
;
;    What that buys, measured (PERFORMANCE.md Part 9): a blit is priced by the
;    RUNS it emits, not by its pixels, because each run is a gfx_hline and a
;    drawing call on a 4.77MHz 8088 costs ~756us of fixed setup before it
;    draws anything - about 0.5ms per run all-in for the short runs a card
;    produces. So a 634-run whole back is ~0.3s of drawing and a 41-run sliver
;    is ~20ms. The "one far call instead of several hundred" above is the
;    saving on the RENDER; the blit itself still pays a call per run, which is
;    exactly why SPEC.md 43.7's buried-back skip is worth as much as it is.
;
;  - **Two metric sets, chosen by screen height, and a per-column fan.**
;    32x44 cards on VGA and Hercules, 28x28 on CGA's 200 rows; and on top of
;    that every tableau column computes its OWN fan step (sol_colfan), so a
;    column that would run past the content bottom tightens until it fits
;    instead of drawing outside the window. That is what lets one binary play
;    on a 640x200 screen and a 640x480 one.
;
; Colour is never the only carrier of anything (SPEC.md 39.4). On a 1bpp
; adapter the red pips are drawn HOLLOW and the black ones solid - the trick
; the black-and-white Macintosh card games used - because index 12 reduces to
; WHITE and a red pip would vanish into the card face; the rank text goes
; black there for the same reason. The felt is index 2, which is green on VGA
; and black on Hercules and CGA, and white cards on black read as well as
; white cards on green.
;
; The game is standard Klondike. Click the stock to deal (one or three, from
; the Deal menu); click it when empty to turn the waste back over. Drag a
; card - or a whole descending alternating run - onto a tableau column or a
; foundation. A press and release WITHOUT moving auto-plays that one card to
; a foundation if it will go, which is the double-click every version of this
; game has, without needing a double-click timer. Keys: N new game, R restart
; the same deal, A auto-finish, Space deal.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SOLITAIRE', sol_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; Two cards, the back one up and to the left, the front one carrying a spade.
; The mask is the union of the two card bodies, so the pair sits on a clean
; white underlay over the dithered desktop; the data is the outlines and the
; pip.
;
;   data                       mask
;   .#########......           .#########......
;   .#.......#......           .#########......
;   .#.......#......           .#########......
;   .#..###########.           .##############.
;   .#..#.........#.           .##############.
;   .#..#....#....#.           .##############.
;   .#..#...###...#.           .##############.
;   .#..#..#####..#.           .##############.
;   .#..#.#######.#.           .##############.
;   .#..#.#######.#.           .##############.
;   .#..#..##.##..#.           .##############.
;   .#..#....#....#.           .##############.
;   .####...###...#.           .##############.
;   ....#.........#.           ....##########..
;   ....#.........#.           ....##########..
;   ....###########.           ....##########..
    OS88_ICON16
    dw 0x7FC0                       ; 16 mask rows (white underlay)
    dw 0x7FC0
    dw 0x7FC0
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x0FFE
    dw 0x0FFE
    dw 0x0FFE
    dw 0x7FC0                       ; 16 data rows (black pixels)
    dw 0x4040
    dw 0x4040
    dw 0x4FFE
    dw 0x4802
    dw 0x4842
    dw 0x48E2
    dw 0x49F2
    dw 0x4BFA
    dw 0x4BFA
    dw 0x49B2
    dw 0x4842
    dw 0x78E2
    dw 0x0802
    dw 0x0802
    dw 0x0FFE
    OS88_ICON16_END

; --- the model ------------------------------------------------------------------
; A card is one byte: rank in bits 0..3 (0 = ace .. 12 = king), suit in bits
; 4..5, bit 6 set when it is face up. Suit order is spade, heart, diamond,
; club - which is also the foundations' left-to-right order - so "is it red"
; is one subtract and one unsigned compare (sol_isred).
SUIT_HE     equ 1
SUIT_DI     equ 2
C_FACEUP    equ 0x40                ; bit 6
C_NOTUP     equ 0xBF                ; ...and its complement, for `and`
C_RANK      equ 0x0F
RANK_A      equ 0
RANK_K      equ 12

; The thirteen piles. The tableau comes first, so a tableau pile index IS its
; column index and sol_pilexy needs no table.
P_TAB0      equ 0                   ; ..6, the seven tableau columns
P_TABN      equ 7                   ; one past the last tableau column
P_FND0      equ 7                   ; ..10, one foundation per suit
P_STOCK     equ 11
P_WASTE     equ 12
SOL_NPILE   equ 13
PILE_CAP    equ 24                  ; the deepest any pile gets: a tableau
                                    ; column is 6 face-down plus at most a
                                    ; K..A run = 19; the stock is 52-28 = 24

SOL_NMET    equ 13                  ; words in a metrics record (sol_met_*)
SOL_BACKMAX equ 704                 ; the back image at the LARGER metrics,
                                    ; (32/2) * 44 bytes of packed 4bpp
SOL_TABLE   equ CGREEN              ; the felt: green on VGA, black on 1bpp
SOL_GHOST   equ CDGRAY              ; empty-slot outlines and their ghost
                                    ; pips - in the dither class, so the
                                    ; distinction survives SPEC.md 39.4

; -----------------------------------------------------------------------------
; sol_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
;
; Everything the rest of the program treats as constant is decided here: the
; screen we actually got (OSAPI_VIDEO - SCREEN_W/H are the reference, not the
; truth), which metrics record to run, the window size that follows, and the
; one-time render of the card back. The loader shows the window, so nothing
; may be drawn from here.
;
; The window height is min(what the layout wants, the desktop band), which is
; the clamp wm_fit would apply anyway (SPEC.md 11). Computing it here instead
; of letting the clamp do it means the template and the record agree from the
; very first paint.
; -----------------------------------------------------------------------------
sol_entry:
    push si
    push di
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock top row, DH=bpp
    mov [sol_scrw], ax
    mov [sol_dock], cx
    mov [sol_bpp], dh

    call sol_metrics            ; the card metrics this screen wants, and
                                ; everything derived from them - re-run by
                                ; sol_onresize (SPEC.md 11.98)
    add ax, 2                       ; ...plus the 1px frame either side
    mov [sol_tpl + WT_W], ax

    mov ax, [sol_fand]              ; the tallest column the game can build -
    mov bx, 6                       ; 6 face-down and 13 face-up - as a frame
    mul bx                          ; height
    mov cx, ax
    mov ax, [sol_fanu]
    mov bx, 12
    mul bx
    add ax, cx
    add ax, [sol_taby]
    add ax, [sol_ch]
    add ax, TITLE_H + 1
    mov bx, [sol_dock]              ; ...clamped to the desktop band, and ONE
    sub bx, MBAR_H + 1              ; PIXEL SHORT of it: the kernel tests
                                    ; y+h against the dock's first row with
                                    ; `jae`, because the drop shadow lives on
                                    ; row y+h - so a frame that merely REACHES
                                    ; the dock is already covering it, and
                                    ; every window opened over this one then
                                    ; pays for the dock to be redrawn under
                                    ; it. wm_fit would hand out exactly that
                                    ; height; this is the same pixel tm_init
                                    ; spends for the same reason. It costs
                                    ; Hercules and CGA one row of tableau and
                                    ; VGA nothing, since VGA never clamps
    cmp ax, bx
    jbe .hok
    mov ax, bx
.hok:
    mov [sol_tpl + WT_H], ax

    mov ax, [sol_scrw]              ; centred across...
    sub ax, [sol_tpl + WT_W]
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [sol_tpl + WT_X], ax
    mov ax, [sol_dock]              ; ...and a third of the slack down
    sub ax, MBAR_H
    sub ax, [sol_tpl + WT_H]
    jns .yok
    xor ax, ax
.yok:
    xor dx, dx
    mov bx, 3
    div bx
    add ax, MBAR_H
    mov [sol_tpl + WT_Y], ax

    call OSAPI_GET_TICKS            ; seeded ONCE: every later New Game walks
    call OSAPI_SRAND                ; on down the same stream, so two deals
                                    ; inside one tick still differ
    call sol_mkback
    call sol_newgame

    mov si, sol_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .full
    mov ax, sol_onresize            ; the card metrics are picked from the
    call OSAPI_WM_ONRESIZE          ; SCREEN, so an adapter change invalidates
                                    ; them (SPEC.md 11.98)
    mov si, sol_menus
    call OSAPI_MENU_SET             ; draws nothing, and preserves the flags
    mov al, 1                       ; ...and it PROMISES its content stands
    call OSAPI_WM_SAVEU             ; still while it is not drawing (SPEC.md
                                    ; 11.96.1): no worker, so the table only
                                    ; moves when a card is dragged. sol_track
                                    ; in the paint proc is a GEOMETRY re-read,
                                    ; and geometry is what wm_su_ck already
                                    ; invalidates on, so skipping it costs
                                    ; nothing
    mov al, 1                       ; ...AND IT OWNS ITS BACKGROUND (SPEC.md
    call OSAPI_WM_OWNBG             ; 11.90.1/43.9.1): sol_drawall's first act
                                    ; is a felt fill of the WHOLE content and
                                    ; sol_pinv in front of it keeps no sliver,
                                    ; so every pixel is written by us. The
                                    ; kernel's white fill was therefore the
                                    ; first layer of a double draw - and the
                                    ; felt is CGREEN, which reduces to BLACK
                                    ; on both mono adapters (39.4), so on the
                                    ; machines this game is for it was the
                                    ; window going WHITE and then black in
                                    ; front of a 681 ms repaint. Neither this
                                    ; nor the promise above touches the flags,
                                    ; so wm_create's CF still rides out to the
                                    ; loader
    mov si, sol_about
    call OSAPI_ABOUT_SET            ; 'About Solitaire' under our name in the
                                    ; bar (SPEC.md 12.2) - same contract, and
                                    ; it preserves the flags for the same
                                    ; reason: the CF above is the loader's
.full:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; sol_track - adopt the content origin and size (top of every callback)
; in:  SI = window ptr
; out: [sol_ox]/[sol_oy], [sol_cwid]/[sol_chgt], [sol_avail]; ES = KERNEL_SEG
; clobbers: nothing else
;
; The record is read rather than trusted from sol_entry's arithmetic: the
; window moves, and wm_fit may have clamped what we asked for. ES is loaded
; here rather than relied on, because a card back blitted earlier in the same
; callback left it pointing at our own segment and never put it back
; (SPEC.md 20.1 says it may).
; -----------------------------------------------------------------------------
; sol_metrics - the card metric record this screen wants, and what it implies
; in:  BX = the screen HEIGHT
; out: sol_cw.. filled, sol_pitch / sol_taby / sol_wstep / sol_cwid derived
; clobbers: AX, BX, CX, SI, DI
; -----------------------------------------------------------------------------
sol_metrics:
    mov si, sol_met_big             ; Hercules' 348 rows still take the big
    cmp bx, 300                     ; cards; only CGA's 200 do not
    jae .metric
    mov si, sol_met_sml
.metric:
    mov di, sol_cw                  ; the record is copied wholesale, so the
    mov cx, SOL_NMET                ; bss words must stay in its order - see
.mcopy:                             ; the note over sol_cw below
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .mcopy

    mov ax, [sol_cw]                ; column pitch
    add ax, [sol_gap]
    mov [sol_pitch], ax
    mov ax, [sol_topy]              ; the tableau clears the top row by two
    add ax, [sol_ch]                ; gaps
    add ax, [sol_gap]
    add ax, [sol_gap]
    mov [sol_taby], ax
    mov ax, [sol_cw]                ; the waste fans right by a third of a
    mov bl, 3                       ; card, into the empty column the layout
    div bl                          ; leaves between it and the foundations
    mov ah, 0
    mov [sol_wstep], ax

    mov ax, [sol_pitch]             ; content width = 2 margins + 7 columns
    mov bx, 7
    mul bx
    add ax, [sol_marg]
    add ax, [sol_marg]
    sub ax, [sol_gap]
    mov [sol_cwid], ax
    ret

; -----------------------------------------------------------------------------
; sol_onresize - the adapter changed under us (SPEC.md 11.98)
;
; in:  SI = our window, CX/DX = the new content size; the gfx lock is HELD and
;      this may not draw
; out: nothing (all registers preserved)
;
; sol_track already reads the live box, so the table's EXTENT follows a resize
; on its own. The card METRICS do not: sol_cw / sol_ch and the gaps and fans
; around them are picked once from the screen height, so a 480-row VGA taken to
; its 200-row CGA keeps dealing big cards into 137 rows of content.
;
; NO GAME STATE MOVES, and it does not need to - which is the whole difference
; from apps/arkanoid's handler. Every card's position here is DERIVED at draw
; time from sol_pitch, sol_taby and the fan steps; a pile is a list, not a set
; of coordinates. So re-deriving the metrics is the entire operation and the
; repaint that follows puts the same game down in the new size.
; -----------------------------------------------------------------------------
sol_onresize:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock top row, DH=bpp - the
    mov [sol_scrw], ax              ; bpp banked again too, a switch being
    mov [sol_dock], cx              ; exactly when it stops being true
    mov [sol_bpp], dh
    call sol_metrics
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
sol_track:
    push ax
    push bx
    push dx
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [sol_ox], ax
    mov [sol_oy], dx
    cmp ax, [sol_lastox]            ; the window moved? every cached sliver is
    jne .moved                      ; remembered at the OLD origin
    cmp dx, [sol_lastoy]
    je .same
.moved:
    mov [sol_lastox], ax
    mov [sol_lastoy], dx
    call sol_pinv
.same:
    mov ax, [es:bx + W_W]
    sub ax, 2
    mov [sol_cwid], ax
    mov ax, [es:bx + W_H]
    sub ax, TITLE_H + 1
    mov [sol_chgt], ax
    sub ax, [sol_taby]              ; the deepest a column's LAST card may
    sub ax, [sol_ch]                ; start; sol_colfan tightens to fit it
    jns .ok
    xor ax, ax
.ok:
    mov [sol_avail], ax
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing
; =============================================================================

; -----------------------------------------------------------------------------
; sol_paint - W_PAINT: full content repaint
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sol_track
    call sol_drawall
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_drawall - felt, thirteen piles, and the plaque if the game is won
; in:  the origin already tracked; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_drawall:
    push ax
    push bx
    push cx
    push dx
    call sol_pinv                   ; the felt fill below wipes every column
    mov al, SOL_TABLE
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [sol_cwid]
    dec cx
    mov dx, [sol_chgt]
    dec dx
    call sol_fillc
    mov byte [sol_felt], 1          ; THE GROUND IS ALREADY FELT (SPEC.md
                                    ; 43.9.2): the fill above covered every
                                    ; pile rect, and pile rects are DISJOINT -
                                    ; a column is one card wide at its own
                                    ; pitch, the waste's is widened by the two
                                    ; fan steps its cards use, and the top row
                                    ; ends above [sol_taby] - so no pile drawn
                                    ; earlier in this loop has put a pixel
                                    ; inside a later one's. Each wipe below is
                                    ; therefore a second fill of pixels that
                                    ; are already that colour
    xor al, al
.pile:
    call sol_drawpile
    inc al
    cmp al, SOL_NPILE
    jb .pile
    mov byte [sol_felt], 0          ; ...and it stops being true here: the
                                    ; plaque and the panel below draw on the
                                    ; felt, and every later sol_drawpile is
                                    ; a move repainting two piles in place
    cmp byte [sol_won], 0
    je .about
    call sol_banner
.about:
    cmp byte [sol_abon], 0
    je .out
    call sol_abdraw
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_drawpile - erase one pile's slot back to felt and redraw it
; in:  AL = pile index; gfx lock held, origin tracked
; out: nothing; preserves all registers
;
; The unit of repaint for the whole program: a move touches two piles, so it
; costs two of these and never a full repaint. That matters because a tableau
; column's slot runs the full height of the content, and a full repaint is
; thirteen of them on top of the felt.
; -----------------------------------------------------------------------------
sol_drawpile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sol_dpp], al
    call sol_pilerect               ; [sol_rx1]..[sol_ry2], content-relative
    call sol_count                  ; CL = cards in the pile
    mov [sol_dpn], cl
    call sol_plan                   ; how much of a tableau column is already
                                    ; right on the screen (0 for the others)
    cmp byte [sol_felt], 0          ; ...and whether anything needs erasing at
    jne .nowipe                     ; all: sol_drawall has just felt-filled the
                                    ; whole content, so this rect is already
    call sol_covers                 ; the colour the wipe would make it
    jc .nowipe                      ; the card is about to cover every pixel
    mov al, SOL_TABLE               ; of this rect: erasing it first is a
    call OSAPI_SET_COLOR            ; second fill of the same pixels
    mov ax, [sol_rx1]
    mov bx, [sol_ry1]
    mov cx, [sol_rx2]
    mov dx, [sol_ry2]
    call sol_fillc
.nowipe:
    mov al, [sol_dpp]
    call sol_pilexy                 ; CX = x, DX = y (content-relative)
    mov [sol_dpx], cx
    mov [sol_dpy], dx
    mov al, [sol_dpp]
    cmp al, P_STOCK
    je .stock
    cmp al, P_WASTE
    je .waste
    cmp al, P_FND0
    jae .fnd

    ; --- a tableau column: every card, at this column's own fan -------------
    cmp byte [sol_dpn], 0
    je .slot
    mov ax, [sol_keepn]             ; sol_plan has already run sol_colfan and
    mov [sol_dpi], al               ; decided how many leading cards to skip
.tab:
    mov al, [sol_dpi]
    call sol_yoff                   ; AX = this card's y offset
    mov [sol_dpo], ax
    mov al, [sol_dpi]               ; the next card's offset is all of this
    inc al                          ; one that will still be visible
    cmp al, [sol_dpn]
    jb .tabnext
    mov ax, [sol_ch]                ; ...and the last card shows all of itself
    jmp .tabvis
.tabnext:
    call sol_yoff
    sub ax, [sol_dpo]
.tabvis:
    mov [sol_dpv], ax
    mov al, [sol_dpp]
    call sol_pilebase               ; BX = the pile's first card
    mov al, [sol_dpi]
    mov ah, 0
    add bx, ax
    mov cl, [bx]
    mov ax, [sol_dpx]
    mov bx, [sol_dpy]
    add bx, [sol_dpo]
    mov dx, [sol_dpv]
    call sol_card
    inc byte [sol_dpi]
    mov al, [sol_dpi]
    cmp al, [sol_dpn]
    jb .tab
    jmp .out

    ; --- the stock: one card back, or the turn-over-again ring --------------
.stock:
    cmp byte [sol_dpn], 0
    je .stockempty
    mov ax, [sol_dpx]
    mov bx, [sol_dpy]
    xor cl, cl                      ; face down: only bit 6 is looked at
    mov dx, [sol_ch]
    call sol_card
    jmp .out
.stockempty:
    call sol_slot
    mov si, sol_ic_ring
    call sol_ghost16
    jmp .out

    ; --- the waste: up to three cards, fanned right -------------------------
.waste:
    cmp byte [sol_dpn], 0
    je .slot
    call sol_wshow                  ; AL = how many to show, 1..3
    mov [sol_dpk], al
    mov al, [sol_dpn]
    sub al, [sol_dpk]
    mov [sol_dpi], al               ; the first card that shows
    mov byte [sol_dpj], 0
.wcard:
    mov al, [sol_dpp]
    call sol_pilebase
    mov al, [sol_dpi]
    mov ah, 0
    add bx, ax
    mov cl, [bx]
    mov al, [sol_dpj]
    mov ah, 0
    mul word [sol_wstep]
    add ax, [sol_dpx]
    mov bx, [sol_dpy]
    mov dx, [sol_ch]
    call sol_card
    inc byte [sol_dpi]
    inc byte [sol_dpj]
    mov al, [sol_dpi]
    cmp al, [sol_dpn]
    jb .wcard
    jmp .out

    ; --- a foundation: its top card, or a ghost pip -------------------------
.fnd:
    cmp byte [sol_dpn], 0
    je .fndempty
    mov al, [sol_dpp]
    call sol_topcard                ; AL = the card on top
    mov cl, al
    mov ax, [sol_dpx]
    mov bx, [sol_dpy]
    mov dx, [sol_ch]
    call sol_card
    jmp .out
.fndempty:
    call sol_slot
    mov al, [sol_dpp]               ; the ghost pip names the suit that goes
    sub al, P_FND0                  ; here
    mov ah, 1
    call sol_pipsold                ; SI = mask
    call sol_ghost16
    jmp .out

.slot:
    call sol_slot
.out:
    call sol_prec                   ; ...and record what is on the glass now.
                                    ; ONE site, at the single exit, so a pile
                                    ; kind added later cannot forget it; it is
                                    ; sol_prec that refuses a non-tableau pile
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The shadow - what is on the glass, per tableau column (SPEC.md 43.10)
;
; A tableau column redrew every card whenever any of them changed, and a card
; is expensive: measured on a cycle-accurate 5150, a face averages ~41 drawing
; calls and a drawing call has a ~756us floor (PERFORMANCE.md Part 2). A back
; is worse - a lattice does not coalesce, and gfx_blit4 emits one gfx_fill per
; run of equal pixels (41 runs for a fanned sliver, 634 for a whole back).
;
; Nearly none of that redrawing is wanted. Cards leave and arrive at the TOP
; of a column, so the ones below the change do not move at all - same card,
; same offset, same visible height, already on the screen and already right.
; Measured on Hercules: one card dragged off a six-card run onto another
; column drew ELEVEN faces for 481 drawing calls and 332.5ms, eight of the
; eleven pixel-identical to what was already there. It is 3 faces, 210 calls
; and 171.2ms now.
;
; So this keeps a SHADOW of what was drawn - one row per column, sol_shw:
;
;   +SHW_N     how many cards were drawn
;   +SHW_CFA   the face-down fan they were drawn at
;   +SHW_CFU   ...and the face-up one
;   +SHW_CARD  the card bytes themselves, as drawn
;
; and sol_keep is the diff: the leading run whose card bytes still match, at a
; fan that has not moved. sol_plan turns that count into the row the erase
; STARTS at, which is the load-bearing half - an erase reaching any higher
; would wipe the very cards being kept.
;
; Three things make the comparison sufficient, and all three are about a card
; being drawn from (byte, x, y, visible height) and nothing else:
;
;  - **x is the column's**, which a keep does not move.
;  - **y is sol_yoff(i)**, a pure function of the index, [sol_cfd] and the two
;    fan steps. The fan is compared outright; [sol_cfd] cannot differ over a
;    matched prefix, because it IS the count of leading face-down cards and
;    those bytes are equal. (Either both counts run past the prefix - and then
;    every kept offset is index*cfa on both sides - or the first face-up card
;    is inside it and the two agree exactly.)
;  - **the visible height** is what the NEXT card leaves showing, except for
;    the last card, which is drawn whole and carries a bottom edge no buried
;    card has. So the last card of EITHER drawing is never kept: the bound is
;    min(now, then) - 1, which is also what catches the card that was on top
;    and now has one fanned over it, and the one uncovered by a card leaving.
;
; A face-down card needs no special argument any more - every back is the same
; image, so identical bytes are identical pixels, and SPEC.md 43.7's
; "indistinguishable" reasoning falls out of the byte compare instead of
; standing beside it. The shadow costs 29 bytes a column in the package's own
; region (SPEC.md 20.1 - a package's memory is a heap claim), against the two
; words a column the sliver cache cost.
;
; It is invalidated - sol_pinv - wherever something else may have painted over
; a column: a full repaint (which fills the content with felt first), the win
; plaque (which lands on the felt between the piles), and a change of content
; origin, which is what a window move looks like from here.
; =============================================================================

SHW_N       equ 0                   ; cards drawn; 0 = nothing here to trust
SHW_CFA     equ 1                   ; the face-down fan they were drawn at
SHW_CFU     equ 3                   ; ...and the face-up one
SHW_CARD    equ 5                   ; the card bytes, as drawn
SOL_SHW     equ SHW_CARD + PILE_CAP ; ...one row per tableau column

; -----------------------------------------------------------------------------
; sol_shwbase - a column's shadow row
; in:  AL = tableau pile; out: SI = the row; preserves all registers but SI
; -----------------------------------------------------------------------------
sol_shwbase:
    push ax
    push dx
    mov dl, SOL_SHW
    mul dl                          ; AX = pile * SOL_SHW (6*29 = 174, one byte)
    mov si, ax
    add si, sol_shw
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_keep - how many leading cards are already correct on the screen
; in:  [sol_dpp] (a tableau pile), [sol_dpn], sol_colfan already run
; out: AX; preserves all other registers
; -----------------------------------------------------------------------------
sol_keep:
%ifdef SOLNOKEEP
    xor ax, ax                      ; `make SOLNOKEEP=1`: keep nothing, so every
    ret                             ; column is erased and redrawn whole. The
                                    ; reference build the shadow is diffed
                                    ; against - see the Makefile
%endif
    push bx
    push cx
    push dx
    push si
    xor cx, cx                      ; CH = kept so far, and the answer
    mov al, [sol_dpp]
    call sol_shwbase                ; SI = this column's shadow
    mov dl, [si + SHW_N]
    or dl, dl
    jz .stop                        ; nothing was drawn from this shadow
    mov ax, [sol_cfa]
    cmp ax, [si + SHW_CFA]
    jne .stop                       ; a re-fan moved every card in the column
    mov ax, [sol_cfu]
    cmp ax, [si + SHW_CFU]
    jne .stop
    mov cl, [sol_dpn]               ; the bound: the LAST card of either
    cmp cl, dl                      ; drawing is drawn whole and is never kept
    jbe .bound
    mov cl, dl
.bound:
    or cl, cl
    jz .stop
    dec cl
    jz .stop
    mov al, [sol_dpp]
    call sol_pilebase               ; BX = the live pile
    add si, SHW_CARD
.walk:
    mov al, [bx]
    cmp al, [si]
    jne .stop                       ; the first card that differs ends the run
    inc bx
    inc si
    inc ch
    dec cl
    jnz .walk
.stop:
    mov al, ch                      ; banked before CX is popped
    mov ah, 0
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_prec - record what this column now looks like on the screen
; in:  [sol_dpp], [sol_dpn], sol_colfan already run; preserves all registers
;
; Called from sol_drawpile's single exit, and refuses anything that is not a
; tableau column there rather than at the call site: the stock, the waste and
; the foundations all leave through it too, and sol_shw has seven rows.
; -----------------------------------------------------------------------------
sol_prec:
    push ax
    push bx
    push cx
    push si
    mov al, [sol_dpp]
    cmp al, P_TABN
    jae .out
    call sol_shwbase                ; SI = this column's shadow
    mov al, [sol_dpn]
    mov [si + SHW_N], al
    mov ax, [sol_cfa]               ; sol_colfan has run for any column with a
    mov [si + SHW_CFA], ax          ; card in it; an empty one records 0 cards
    mov ax, [sol_cfu]               ; and sol_keep never reads the rest
    mov [si + SHW_CFU], ax
    mov cl, [sol_dpn]
    mov ch, 0
    jcxz .out
    mov al, [sol_dpp]
    call sol_pilebase               ; BX = the pile
    add si, SHW_CARD
.copy:
    mov al, [bx]
    mov [si], al
    inc bx
    inc si
    loop .copy
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_pinv - forget every column's shadow; the next draw of each redraws whole
; preserves all registers
; -----------------------------------------------------------------------------
sol_pinv:
    push cx
    push si
    mov si, sol_shw
    mov cx, P_TABN
.each:
    mov byte [si + SHW_N], 0        ; 0 drawn: sol_keep's bound can only be 0
    add si, SOL_SHW
    loop .each
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; sol_plan - decide the keep count, and raise the erase to match it
; in:  [sol_dpp], [sol_dpn]
; out: [sol_keepn]; [sol_ry1] moved down when anything is kept; and for a
;      tableau column sol_colfan has been run. Preserves all registers.
; -----------------------------------------------------------------------------
sol_plan:
    push ax
    mov word [sol_keepn], 0
    mov al, [sol_dpp]
    cmp al, P_TABN
    jae .out                        ; only a tableau column has a run of
    cmp byte [sol_dpn], 0           ; identical slivers to keep
    je .out
    call sol_colfan
    call sol_keep
    mov [sol_keepn], ax
    or ax, ax
    jz .out
    call sol_yoff                   ; AL = the first index we WILL draw...
    add ax, [sol_taby]
    mov [sol_ry1], ax               ; ...and the erase may not start above it
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_covers - will this pile's own drawing cover every pixel of the rect
;              sol_drawpile is about to erase?
; in:  [sol_dpp], [sol_dpn]
; out: CF = 1 yes, so skip the erase; preserves all registers
;
; True for exactly two piles, and only when they hold a card: the stock and a
; foundation are one card drawn into a one-card rect. A tableau's rect runs
; the whole height of the content and the waste's is two fan steps wider than
; a card, so both really do need clearing; an empty pile draws an outline and
; needs it most of all.
; -----------------------------------------------------------------------------
sol_covers:
    push ax
    cmp byte [sol_dpn], 0
    je .no
    mov al, [sol_dpp]
    cmp al, P_STOCK
    je .yes
    cmp al, P_FND0
    jb .no                          ; a tableau column
    cmp al, P_WASTE
    je .no
.yes:
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; sol_slot - an empty pile's outline: a 1px rectangle in the dither class
; in:  [sol_dpx]/[sol_dpy]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_slot:
    push ax
    push bx
    push cx
    push dx
    mov al, SOL_GHOST
    call OSAPI_SET_COLOR
    mov ax, [sol_dpx]
    mov bx, [sol_dpy]
    mov cx, ax
    add cx, [sol_cw]
    dec cx
    mov dx, bx
    add dx, [sol_ch]
    dec dx
    call sol_framec
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_ghost16 - a 16x16 mask centred in an empty pile, in the dither class
; in:  SI = mask, [sol_dpx]/[sol_dpy]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_ghost16:
    push ax
    push bx
    push cx
    push dx
    mov al, SOL_GHOST
    call OSAPI_SET_COLOR
    mov ax, [sol_cw]
    sub ax, 16
    shr ax, 1
    add ax, [sol_dpx]
    add ax, [sol_ox]
    mov bx, [sol_ch]
    sub bx, 16
    shr bx, 1
    add bx, [sol_dpy]
    add bx, [sol_oy]
    mov cx, 16
    mov dx, 16
    call sol_maskrun
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_card - draw one card
; in:  AX = x, BX = y (content-relative), CL = card byte, DX = visible height
; out: nothing; preserves all registers
;
; The visible height is what the NEXT card in the fan leaves showing, so a
; buried card costs only the rows that survive and the centre pip is skipped
; before it is drawn rather than after it is covered. It is clamped to the
; content bottom as well, because the gfx primitives clip to the SCREEN and
; would otherwise let a pathologically short window paint on the desktop.
; -----------------------------------------------------------------------------
sol_card:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    add ax, [sol_ox]
    mov [sol_cdx], ax
    add bx, [sol_oy]
    mov [sol_cdy], bx
    mov [sol_ccard], cl
    mov ax, [sol_ch]
    cmp dx, ax
    jbe .vok
    mov dx, ax
.vok:
    mov ax, [sol_chgt]
    add ax, [sol_oy]
    sub ax, [sol_cdy]               ; rows left inside our own content
    jle .out
    cmp dx, ax
    jbe .vok2
    mov dx, ax
.vok2:
    or dx, dx
    jz .out
    mov [sol_cvis], dx
    test byte [sol_ccard], C_FACEUP
    jnz .face
    call sol_drawback
    jmp .out
.face:
    call sol_drawface
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
; sol_drawback - blit the prebuilt back image
; in:  [sol_cdx]/[sol_cdy] absolute, [sol_cvis] rows; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, BP, ES
; -----------------------------------------------------------------------------
sol_drawback:
    push ds
    pop es                          ; the image is OURS; blit4 takes ES:SI and
    mov si, sol_backbuf             ; no stub is involved (SPEC.md 20.3)
    mov bp, [sol_stride]
    mov ax, [sol_cdx]
    mov bx, [sol_cdy]
    mov cx, [sol_cw]
    mov dx, [sol_cvis]
    call OSAPI_GFX_BLIT4
    ret

; -----------------------------------------------------------------------------
; sol_drawface - white face, black edges, rank glyph, corner pip, centre pip
; in:  [sol_cdx]/[sol_cdy]/[sol_cvis]/[sol_ccard]; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; The edges are four separate lines rather than one gfx_frame, because a card
; with another fanned over it has no bottom edge to draw and drawing one
; anyway puts a second black rule one row above the next card's top edge.
; Everything below the fill is gated on [sol_cvis]: a two-pixel sliver draws
; its top edge and nothing else, and no glyph can spill past the card.
; -----------------------------------------------------------------------------
sol_drawface:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sol_cdx]
    mov bx, [sol_cdy]
    mov cx, ax
    add cx, [sol_cw]
    dec cx
    mov dx, bx
    add dx, [sol_cvis]
    dec dx
    call OSAPI_GFX_FILL

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sol_cdx]               ; top edge
    mov bx, ax
    add bx, [sol_cw]
    dec bx
    mov dx, [sol_cdy]
    call OSAPI_GFX_HLINE
    mov ax, [sol_cdx]               ; left edge
    mov bx, [sol_cdy]
    mov dx, bx
    add dx, [sol_cvis]
    dec dx
    call OSAPI_GFX_VLINE
    mov ax, [sol_cdx]               ; right edge
    add ax, [sol_cw]
    dec ax
    call OSAPI_GFX_VLINE
    mov ax, [sol_cvis]              ; bottom edge, only when this really is
    cmp ax, [sol_ch]                ; the bottom of the card
    jb .noedge
    mov ax, [sol_cdx]
    mov bx, ax
    add bx, [sol_cw]
    dec bx
    mov dx, [sol_cdy]
    add dx, [sol_ch]
    dec dx
    call OSAPI_GFX_HLINE
.noedge:

    mov al, [sol_ccard]             ; the ink: red only where red survives
    call sol_isred
    mov al, CBLACK
    jnc .ink
    cmp byte [sol_bpp], 4
    jne .ink                        ; on 1bpp index 12 reduces to WHITE, which
    mov al, CLRED                   ; on a white face is nothing at all
.ink:
    call OSAPI_SET_COLOR

    mov ax, [sol_ry]                ; --- rank ---
    add ax, 8
    cmp ax, [sol_cvis]
    ja .done
    mov al, [sol_ccard]
    and al, C_RANK
    mov bl, al
    mov bh, 0
    mov al, [sol_rankch+bx]
    mov cx, [sol_cdx]
    add cx, [sol_rx]
    mov dx, [sol_cdy]
    add dx, [sol_ry]
    or al, al
    jnz .one
    mov al, '1'                     ; the ten is the only two-glyph rank
    call OSAPI_FONT_CHAR
    add cx, 8
    mov al, '0'
.one:
    call OSAPI_FONT_CHAR

    mov ax, [sol_psy]               ; --- corner pip, 8x8 ---
    add ax, 8
    cmp ax, [sol_cvis]
    ja .done
    mov al, [sol_ccard]
    call sol_suit
    mov ah, 0
    call sol_pipsel                 ; SI = mask
    mov ax, [sol_cdx]
    add ax, [sol_psx]
    mov bx, [sol_cdy]
    add bx, [sol_psy]
    mov cx, 8
    mov dx, 8
    call sol_maskrun

    mov ax, [sol_ply]               ; --- centre pip, 16x16, if it shows ---
    add ax, 16
    cmp ax, [sol_cvis]
    ja .done
    mov al, [sol_ccard]
    call sol_suit
    mov ah, 1
    call sol_pipsel
    mov ax, [sol_cdx]
    add ax, [sol_plx]
    mov bx, [sol_cdy]
    add bx, [sol_ply]
    mov cx, 16
    mov dx, 16
    call sol_maskrun
.done:
    ret

; -----------------------------------------------------------------------------
; sol_pipsel  - pick a suit's pip mask, hollow where hollow means something
; sol_pipsold - the same, always SOLID
; in:  AL = suit 0..3, AH = 0 for the 8x8 mask / 1 for the 16x16
; out: SI = mask address; preserves all registers except SI
;
; On a 1bpp adapter the two red suits get the HOLLOW mask, and that is the
; whole of how red and black are told apart there (SPEC.md 39.4). On four
; bits the colour does that job and every suit is solid.
;
; The ghost pips in the empty foundations take the second entry, because they
; are drawn in the DITHER class rather than in black: a 50% dither eats every
; other pixel, which a solid shape survives and a 1px outline does not - the
; hollow heart came out as three disconnected dashes. Nothing is lost by it,
; either. A ghost says which suit belongs here, not what colour it is.
; -----------------------------------------------------------------------------
sol_pipsold:
    push ax
    push bx
    push cx
    mov bl, al
    mov bh, 0
    mov cl, 4
    jmp sol_pipsel.solid

sol_pipsel:
    push ax
    push bx
    push cx
    mov bl, al
    mov bh, 0
    mov cl, 4
    cmp byte [sol_bpp], 4
    je .solid
    cmp bl, SUIT_HE
    jb .solid
    cmp bl, SUIT_DI
    ja .solid
    dec bl                          ; the hollow tables index 0 = hearts
    or ah, ah
    jnz .h16
    shl bx, cl                      ; 8 words a suit
    mov si, sol_p8h
    jmp .add
.h16:
    shl bx, cl                      ; 16 words a suit
    shl bx, 1
    mov si, sol_p16h
    jmp .add
.solid:
    or ah, ah
    jnz .s16
    shl bx, cl
    mov si, sol_p8s
    jmp .add
.s16:
    shl bx, cl
    shl bx, 1
    mov si, sol_p16s
.add:
    add si, bx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_maskrun - plot a 1-bit mask transparently, one hline per run
; in:  AX = x, BX = y (absolute), SI = rows as words (bit 15 = leftmost),
;      CX = row count, DX = width in pixels; colour in [gfx_color]
; out: nothing; preserves all registers
;
; Transparent is the point: a pip goes on a card face that is already drawn
; and a ghost pip goes on felt. gfx_blit4 would be one far call instead of a
; dozen, but it is opaque and cannot carry "leave this pixel alone".
; -----------------------------------------------------------------------------
sol_maskrun:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sol_mrx], ax
    mov [sol_mry], bx
    mov [sol_mrw], dx
    mov [sol_mrn], cx
.row:
    mov dx, [si]
    add si, 2
    mov word [sol_mrs], 0xFFFF      ; no run open
    xor cx, cx
.bit:
    rol dx, 1                       ; bit 0 now holds the pixel at column CX
    test dl, 1
    jz .clear
    cmp word [sol_mrs], 0xFFFF
    jne .next
    mov [sol_mrs], cx               ; a run opens here
    jmp .next
.clear:
    cmp word [sol_mrs], 0xFFFF
    je .next
    call .emit
.next:
    inc cx
    cmp cx, [sol_mrw]
    jb .bit
    cmp word [sol_mrs], 0xFFFF
    je .rowdone
    call .emit                      ; a run that reached the right edge
.rowdone:
    inc word [sol_mry]
    dec word [sol_mrn]
    jnz .row
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.emit:                              ; the open run is [sol_mrs .. CX-1]
    push ax
    push bx
    push cx
    push dx
    mov ax, [sol_mrs]
    add ax, [sol_mrx]
    mov bx, cx
    add bx, [sol_mrx]
    dec bx
    mov dx, [sol_mry]
    call OSAPI_GFX_HLINE            ; AX = x1, BX = x2, DX = y
    pop dx
    pop cx
    pop bx
    pop ax
    mov word [sol_mrs], 0xFFFF
    ret

; -----------------------------------------------------------------------------
; sol_mkback - render the card back into a packed 4bpp image, once
; in:  the metrics; called from the entry proc (no lock held, nothing drawn)
; out: [sol_backbuf] filled, [sol_stride] set; preserves all registers
;
; A lattice has a run every two or three pixels, so drawing it through the
; gfx primitives would be hundreds of far calls a card. Rendered into RAM
; once it is one OSAPI_GFX_BLIT4 forever after, and the design can be as
; fussy as it likes because nothing pays for it twice.
;
; Black edge, white margin, then a field of diamonds - white wherever (x+y)
; or (x-y) lands on a multiple of eight. The field colour is the only thing
; that changes with the adapter: deep blue on VGA, and index 9 on 1bpp so it
; reduces to the 50% dither with the white lattice still crossing it.
; -----------------------------------------------------------------------------
sol_mkback:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [sol_cw]
    shr ax, 1
    mov [sol_stride], ax
    mul word [sol_ch]
    mov cx, ax
    mov di, sol_backbuf
.white:
    mov byte [di], 0xFF             ; the whole card starts white, which
    inc di                          ; leaves the 1px margin for free
    loop .white

    mov cl, CBLUE
    cmp byte [sol_bpp], 4
    je .have
    mov cl, CLBLUE                  ; 1bpp: a colour in the dither class
.have:
    mov [sol_bkcol], cl
    mov bx, 2
.frow:
    mov ax, 2
.fcol:
    mov cl, [sol_bkcol]
    mov dx, ax                      ; the lattice: two families of diagonals
    add dx, bx
    and dl, 7
    jz .lat
    mov dx, ax
    sub dx, bx
    add dx, 64                      ; bias, because x-y goes negative
    and dl, 7
    jnz .plot
.lat:
    cmp ax, 2                       ; the outermost ring of the field stays
    je .plot                        ; flat, so the lattice never touches the
    cmp bx, 2                       ; white margin
    je .plot
    mov dx, [sol_cw]
    sub dx, 3
    cmp ax, dx
    jae .plot
    mov dx, [sol_ch]
    sub dx, 3
    cmp bx, dx
    jae .plot
    mov cl, CWHITE
.plot:
    call sol_bpx
    inc ax
    mov dx, [sol_cw]
    sub dx, 2
    cmp ax, dx
    jb .fcol
    inc bx
    mov dx, [sol_ch]
    sub dx, 2
    cmp bx, dx
    jb .frow

    xor cx, cx                      ; the black edge, last
    xor ax, ax
.top:
    xor bx, bx
    call sol_bpx
    mov bx, [sol_ch]
    dec bx
    call sol_bpx
    inc ax
    cmp ax, [sol_cw]
    jb .top
    xor bx, bx
.side:
    xor ax, ax
    call sol_bpx
    mov ax, [sol_cw]
    dec ax
    call sol_bpx
    inc bx
    cmp bx, [sol_ch]
    jb .side

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_bpx - set one pixel of the back image
; in:  AX = x, BX = y, CL = colour 0..15
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_bpx:
    push ax
    push bx
    push cx
    push dx
    push di
    push ax
    mov ax, bx
    mul word [sol_stride]
    mov di, ax
    pop ax
    mov bx, ax
    shr bx, 1
    add di, bx
    add di, sol_backbuf
    mov bl, [di]
    test al, 1
    jnz .lo
    and bl, 0x0F                    ; an even pixel is the HIGH nibble
    mov bh, cl
    mov cl, 4
    shl bh, cl
    or bl, bh
    jmp .put
.lo:
    and bl, 0xF0
    mov bh, cl
    and bh, 0x0F
    or bl, bh
.put:
    mov [di], bl
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sol_about - the About handler (API 0x01E0, SPEC.md 12.2/43)
; in:  SI = our window; the UI task, gfx lock HELD, far-called at our segment
; out: nothing; may clobber the callback set
;
; A window callback in every respect that matters, so it draws directly and
; repaints itself. The panel is state, not a modal loop: [sol_abon] goes up,
; the content is redrawn with it on top, and the next click or key anywhere
; in the window takes it down again (sol_abdismiss). A loop here would hold
; the gfx lock against every background task for as long as the player left
; the credits up.
; -----------------------------------------------------------------------------
sol_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [sol_abon], 1
    call sol_track
    call sol_drawall
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_abdismiss - take the About panel down, if it is up
; in:  the origin already tracked; gfx lock held
; out: CF = 1 if it WAS up (and the content has been repainted), so the
;      caller swallows the click or key that dismissed it; CF = 0 otherwise.
;      All registers preserved.
; -----------------------------------------------------------------------------
sol_abdismiss:
    cmp byte [sol_abon], 0
    je .none
    mov byte [sol_abon], 0
    call sol_drawall
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; sol_abmeas - size and place the About panel
; in:  the origin already tracked
; out: [sol_abw]/[sol_abh]/[sol_abl]/[sol_abt] (content coords)
; clobbers: nothing (flags)
;
; Measured, never pinned: the widest line decides the width and the line
; count decides the height, so the panel is right on all three adapters and
; nothing has to be re-measured when a line of the credit changes. Both are
; clamped to the content, because CGA's 640x200 gives this window 220px of
; width and about 137 of height (SPEC.md 39).
; -----------------------------------------------------------------------------
sol_abmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, sol_ablines
.next:
    mov bx, [si]
    or bx, bx
    jz .done
    inc di
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    pop si
    cmp ax, cx
    jbe .next
    mov cx, ax
    jmp .next
.done:
    add cx, 24                      ; 12px of margin either side
    cmp cx, [sol_cwid]
    jbe .wok
    mov cx, [sol_cwid]
.wok:
    mov [sol_abw], cx
    mov ax, di                      ; height = lines * SOL_ABLH + margins
    mov bx, SOL_ABLH
    mul bx
    add ax, 16
    cmp ax, [sol_chgt]
    jbe .hok
    mov ax, [sol_chgt]
.hok:
    mov [sol_abh], ax
    mov ax, [sol_cwid]
    sub ax, [sol_abw]
    shr ax, 1
    mov [sol_abl], ax
    mov ax, [sol_chgt]
    sub ax, [sol_abh]
    shr ax, 1
    mov [sol_abt], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_abdraw - the About panel: white card, black frame, the credit centred
; in:  the origin already tracked; gfx lock held
; out: nothing; preserves all registers
;
; Every line is centred on the PANEL rather than on the content, so the block
; still reads as one card when the panel has been clamped narrower than the
; text wanted.
; -----------------------------------------------------------------------------
sol_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sol_abmeas
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call .rect
    call sol_fillc
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call .rect
    call sol_framec

    mov si, sol_ablines
    mov di, [sol_abt]
    add di, 8                       ; the first baseline, inside the frame
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = this line's width
    mov cx, [sol_abw]
    sub cx, ax
    shr cx, 1
    add cx, [sol_abl]
    add cx, [sol_ox]                ; font_str wants SCREEN coords
    mov dx, di
    add dx, [sol_oy]
    call OSAPI_FONT_STR
    pop si
    add di, SOL_ABLH
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.rect:                              ; the panel as (x1,y1)-(x2,y2), inclusive
    mov ax, [sol_abl]
    mov bx, [sol_abt]
    mov cx, ax
    add cx, [sol_abw]
    dec cx
    mov dx, bx
    add dx, [sol_abh]
    dec dx
    ret

; -----------------------------------------------------------------------------
; sol_banner - the win plaque, centred in the content
; in:  gfx lock held, origin tracked
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_banner:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, sol_s_win
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    mov [sol_tmpa], ax
    add ax, 24
    cmp ax, [sol_cwid]
    jbe .wok
    mov ax, [sol_cwid]
.wok:
    mov [sol_tmpb], ax              ; plaque width
    mov ax, [sol_cwid]
    sub ax, [sol_tmpb]
    shr ax, 1
    mov [sol_tmpc], ax              ; plaque left
    mov ax, [sol_chgt]
    sub ax, 28
    jns .hok
    xor ax, ax
.hok:
    shr ax, 1
    mov [sol_tmpd], ax              ; plaque top
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call .box
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sol_tmpc]
    mov bx, [sol_tmpd]
    mov cx, ax
    add cx, [sol_tmpb]
    dec cx
    mov dx, bx
    add dx, 27
    call sol_framec
    mov cx, [sol_cwid]
    sub cx, [sol_tmpa]
    shr cx, 1
    add cx, [sol_ox]
    mov dx, [sol_tmpd]
    add dx, 10
    add dx, [sol_oy]
    mov si, sol_s_win
    call OSAPI_FONT_STR
    call sol_pinv                   ; the plaque lands on the felt BETWEEN the
    pop si                          ; piles, and on a short window its top row
    pop dx                          ; is a pixel off a column's slivers
    pop cx
    pop bx
    pop ax
    ret
.box:
    mov ax, [sol_tmpc]
    mov bx, [sol_tmpd]
    mov cx, ax
    add cx, [sol_tmpb]
    dec cx
    mov dx, bx
    add dx, 27
    call sol_fillc
    ret

; -----------------------------------------------------------------------------
; sol_fillc / sol_framec - a fill and a 1px outline in CONTENT coordinates
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (content-relative, inclusive)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_fillc:
    push ax
    push bx
    push cx
    push dx
    add ax, [sol_ox]
    add cx, [sol_ox]
    add bx, [sol_oy]
    add dx, [sol_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sol_framec:
    push ax
    push bx
    push cx
    push dx
    add ax, [sol_ox]
    add cx, [sol_ox]
    add bx, [sol_oy]
    add dx, [sol_oy]
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Geometry
; =============================================================================

; -----------------------------------------------------------------------------
; sol_pilexy - a pile's top-left slot corner
; in:  AL = pile index
; out: CX = x, DX = y (content-relative); preserves all registers
; -----------------------------------------------------------------------------
sol_pilexy:
    push ax
    push bx
    cmp al, P_STOCK
    je .c0
    cmp al, P_WASTE
    je .c1
    cmp al, P_FND0
    jb .have                        ; a tableau pile IS its column
    sub al, P_FND0 - 3              ; the foundations take columns 3..6
    jmp .have
.c0:
    xor al, al
    jmp .have
.c1:
    mov al, 1
.have:
    mov bl, [sol_pitch]             ; the pitch is far under 256
    mul bl                          ; AX = column * pitch
    add ax, [sol_marg]
    mov cx, ax
    mov dx, [sol_topy]
    pop bx
    pop ax                          ; AL = the pile again
    cmp al, P_TABN
    jae .done
    mov dx, [sol_taby]
.done:
    ret

; -----------------------------------------------------------------------------
; sol_pilerect - the rectangle sol_drawpile erases
; in:  AL = pile index
; out: [sol_rx1]/[sol_ry1]/[sol_rx2]/[sol_ry2]; preserves all registers
;
; A tableau column owns everything under it to the content bottom; the waste
; owns the two fan steps it can spill to its right, which is what the empty
; column between it and the foundations is for.
; -----------------------------------------------------------------------------
sol_pilerect:
    push ax
    push bx
    push cx
    push dx
    call sol_pilexy                 ; CX = x, DX = y
    mov [sol_rx1], cx
    mov [sol_ry1], dx
    add cx, [sol_cw]
    dec cx
    cmp al, P_WASTE
    jne .noskew
    mov bx, [sol_wstep]
    add bx, bx
    add cx, bx
.noskew:
    mov [sol_rx2], cx
    add dx, [sol_ch]
    dec dx
    cmp al, P_TABN
    jae .short
    mov dx, [sol_chgt]
    dec dx
.short:
    mov [sol_ry2], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_colfan - the fan steps THIS tableau column will be drawn at
; in:  AL = tableau pile
; out: [sol_cfd] face-down count, [sol_cfa] down step, [sol_cfu] up step;
;      preserves all registers
;
; The default steps are what the metrics say. A column whose last card would
; start past [sol_avail] tightens the face-up step first (it has the most of
; them, and the most to give), then the face-down one, never below one pixel.
; Nothing is stored per pile: the draw pass and the hit test both call this,
; so they cannot end up disagreeing about where a card is.
; -----------------------------------------------------------------------------
sol_colfan:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sol_cfp], al
    mov ax, [sol_fand]
    mov [sol_cfa], ax
    mov ax, [sol_fanu]
    mov [sol_cfu], ax
    mov al, [sol_cfp]
    call sol_count
    mov ch, 0
    cmp cx, 2
    jb .out                         ; nought or one card: nothing can overflow

    mov al, [sol_cfp]               ; count the face-down cards, which are
    call sol_pilebase               ; always the bottom of the column
    xor dx, dx
.down:
    cmp dx, cx
    jae .haved
    mov al, [bx]
    test al, C_FACEUP
    jnz .haved
    inc bx
    inc dx
    jmp .down
.haved:
    mov [sol_cfd], dx

    dec cx                          ; CX = the LAST card's index
    mov ax, cx                      ; ...and its y offset is what must fit
    sub ax, dx
    jbe .allface
    mov bx, ax                      ; BX = face-up gaps
    mul word [sol_cfu]
    mov si, ax
    mov ax, dx
    mul word [sol_cfa]
    add ax, si
    jmp .test
.allface:
    xor bx, bx
    mov ax, cx
    mul word [sol_cfa]
.test:
    cmp ax, [sol_avail]
    jbe .out                        ; it fits at full stretch

    or bx, bx
    jz .shrinkd                     ; no face-up gaps to give
    mov ax, [sol_cfd]
    mul word [sol_cfa]
    mov si, ax                      ; SI = what the face-down half costs
    mov ax, [sol_avail]
    sub ax, si
    jbe .shrinkboth
    cmp ax, bx
    jb .shrinkboth                  ; not even one pixel a gap
    xor dx, dx
    div bx
    mov [sol_cfu], ax
    jmp .out
.shrinkboth:
    mov word [sol_cfu], 1           ; one pixel per face-up gap, and whatever
    mov ax, [sol_avail]             ; is left over to the face-down half
    sub ax, bx
    jbe .minimum
    xor dx, dx
    div word [sol_cfd]              ; cfd cannot be 0 here: with no face-down
    or ax, ax                       ; cards the branch above would have taken
    jz .minimum                     ; the division by BX
    mov [sol_cfa], ax
    jmp .out
.minimum:
    mov word [sol_cfa], 1
    jmp .out
.shrinkd:
    mov ax, [sol_avail]
    xor dx, dx
    div cx                          ; CX = the last index = the gap count
    or ax, ax
    jnz .keepd
    inc ax
.keepd:
    mov [sol_cfa], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_yoff - a card's y offset within its column
; in:  AL = card index; sol_colfan already run for that column
; out: AX = the offset in pixels; clobbers AX only
; -----------------------------------------------------------------------------
sol_yoff:
    push bx
    push cx
    push dx
    mov ah, 0
    mov cx, ax
    cmp cx, [sol_cfd]
    jbe .down                       ; still inside the face-down run
    mov ax, [sol_cfd]
    mul word [sol_cfa]
    mov bx, ax
    mov ax, cx
    sub ax, [sol_cfd]
    mul word [sol_cfu]
    add ax, bx
    jmp .out
.down:
    mov ax, cx
    mul word [sol_cfa]
.out:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The model
; =============================================================================

; -----------------------------------------------------------------------------
; sol_pilebase - where a pile's cards start
; in:  AL = pile index
; out: BX = the offset of card 0; preserves all registers except BX
; -----------------------------------------------------------------------------
sol_pilebase:
    push ax
    mov bl, PILE_CAP
    mul bl                          ; AX = pile * PILE_CAP
    mov bx, ax
    add bx, sol_pile
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_count - how many cards a pile holds
; in:  AL = pile index
; out: CL = the count (CH untouched); preserves all other registers
; -----------------------------------------------------------------------------
sol_count:
    push bx
    mov bl, al
    mov bh, 0
    mov cl, [sol_pcnt+bx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_topcard - the card on top of a pile
; in:  AL = pile index (must be non-empty)
; out: AL = the card byte; preserves all other registers
; -----------------------------------------------------------------------------
sol_topcard:
    push bx
    push cx
    call sol_count
    call sol_pilebase
    mov ch, 0
    dec cl
    add bx, cx
    mov al, [bx]
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_push / sol_pop - the two ends of a pile
; sol_push: in AL = pile, AH = card;  out: nothing
; sol_pop:  in AL = pile (non-empty); out: AH = the card
; both preserve every other register.
; -----------------------------------------------------------------------------
sol_push:
    push ax
    push bx
    push cx
    call sol_count
    call sol_pilebase
    mov ch, 0
    add bx, cx
    mov [bx], ah
    mov bl, al
    mov bh, 0
    inc byte [sol_pcnt+bx]
    pop cx
    pop bx
    pop ax
    ret

sol_pop:
    push bx
    push cx
    call sol_count
    call sol_pilebase
    mov ch, 0
    dec cl
    add bx, cx
    mov ah, [bx]
    mov bl, al
    mov bh, 0
    dec byte [sol_pcnt+bx]
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_suit / sol_isred - decode a card
; sol_suit:  in AL = card;  out AL = the suit, 0..3
; sol_isred: in AL = card;  out CF = 1 when the suit is red; AL untouched
; -----------------------------------------------------------------------------
sol_suit:
    push cx
    mov cl, 4
    shr al, cl
    and al, 3
    pop cx
    ret

sol_isred:
    push ax
    push cx
    mov cl, 4
    shr al, cl
    and al, 3
    dec al                          ; hearts and diamonds become 0 and 1...
    cmp al, 2                       ; ...so CF = 1 is exactly "red"
    pop cx                          ; (neither pop touches the flags)
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_canfnd - may this card go on that foundation?
; in:  AL = card, AH = foundation 0..3
; out: CF = 0 yes, CF = 1 no; preserves all registers
; -----------------------------------------------------------------------------
sol_canfnd:
    push ax
    push bx
    push cx
    push dx
    mov dl, al                      ; DL = the card
    mov dh, ah                      ; DH = the foundation
    call sol_suit
    cmp al, dh
    jne .no                         ; a foundation takes one suit and no other
    mov bl, dh
    add bl, P_FND0
    mov al, bl
    call sol_count                  ; CL = cards already there
    or cl, cl
    jnz .onto
    mov al, dl
    and al, C_RANK
    cmp al, RANK_A
    jne .no
    jmp .yes
.onto:
    mov al, bl
    call sol_topcard
    and al, C_RANK
    inc al
    mov ah, dl
    and ah, C_RANK
    cmp al, ah
    jne .no
.yes:
    clc
    jmp .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_cantab - may this card go on that tableau column?
; in:  AL = card, AH = tableau 0..6
; out: CF = 0 yes, CF = 1 no; preserves all registers
; -----------------------------------------------------------------------------
sol_cantab:
    push ax
    push bx
    push cx
    push dx
    mov dl, al
    mov dh, ah
    mov al, dh
    call sol_count
    or cl, cl
    jnz .onto
    mov al, dl                      ; an empty column takes a king, and only
    and al, C_RANK                  ; a king
    cmp al, RANK_K
    jne .no
    jmp .yes
.onto:
    mov al, dh
    call sol_topcard
    mov bl, al                      ; BL = the card underneath
    test bl, C_FACEUP
    jz .no
    and al, C_RANK
    mov ah, dl
    and ah, C_RANK
    inc ah
    cmp al, ah
    jne .no                         ; it must descend by one...
    mov al, bl
    call sol_isred
    mov bh, 0
    jnc .b1
    mov bh, 1
.b1:
    mov al, dl
    call sol_isred
    mov bl, 0
    jnc .b2
    mov bl, 1
.b2:
    cmp bl, bh
    je .no                          ; ...and alternate colour
.yes:
    clc
    jmp .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_move - move the top N cards of one pile to another, in order
; in:  AL = source pile, AH = destination pile, CL = count
; out: nothing; preserves all registers
;
; The one place a card changes hands, so it is also the one place that turns
; a newly uncovered tableau card face up and that keeps the waste's fan
; honest. Nothing here draws.
; -----------------------------------------------------------------------------
sol_move:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sol_mvs], al
    mov [sol_mvd], ah
    mov [sol_mvn], cl

    mov al, [sol_mvs]               ; the run starts at count - N
    call sol_count
    mov ch, 0
    sub cl, [sol_mvn]
    call sol_pilebase
    add bx, cx
    mov si, bx                      ; SI = the first card to move
    mov al, [sol_mvd]
    call sol_count
    call sol_pilebase
    mov ch, 0
    add bx, cx
    mov di, bx                      ; DI = where it lands
    mov cl, [sol_mvn]
    mov ch, 0
.copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .copy

    mov bl, [sol_mvs]               ; both counts follow
    mov bh, 0
    mov al, [sol_mvn]
    sub [sol_pcnt+bx], al
    mov bl, [sol_mvd]
    add [sol_pcnt+bx], al

    mov al, [sol_mvs]               ; a tableau card that has just been
    cmp al, P_TABN                  ; uncovered turns over
    jae .notab
    call sol_count
    or cl, cl
    jz .done
    call sol_pilebase
    mov ch, 0
    dec cl
    add bx, cx
    or byte [bx], C_FACEUP
    jmp .done
.notab:
    cmp al, P_WASTE                 ; a card taken off the waste shrinks its
    jne .done                       ; fan, so the one under shows through
    cmp byte [sol_wfan], 2
    jb .done
    dec byte [sol_wfan]
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_domove - sol_move, then the win test, then the repaint
; in:  AL = source, AH = destination, CL = count; gfx lock held
; out: nothing; preserves all registers
;
; Two piles are all a move can touch, so two sol_drawpile calls are the whole
; repaint. The exception is the plaque: it sits on the felt between the piles
; and no pile owns its rectangle, so a move that puts it up OR takes it down
; costs the full content instead. Testing the flag on BOTH sides of the win
; test is what catches the second case.
; -----------------------------------------------------------------------------
sol_domove:
    push ax
    push bx
    mov [sol_mds], al
    mov [sol_mdd], ah
    mov bl, [sol_won]               ; the win state going in - through BL, not
    mov [sol_mdw], bl               ; AL: AL is sol_move's source argument
    call sol_move
    call sol_checkwin
    mov bl, [sol_won]
    or bl, [sol_mdw]
    jnz .all
    mov al, [sol_mds]
    call sol_drawpile
    mov al, [sol_mdd]
    call sol_drawpile
    pop bx
    pop ax
    ret
.all:
    call sol_drawall                ; the plaque needs the whole content
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_wshow - how many waste cards to fan
; in:  nothing
; out: AL = 1..3, or 0 when the waste is empty; preserves all other registers
; -----------------------------------------------------------------------------
sol_wshow:
    push cx
    mov al, P_WASTE
    call sol_count
    or cl, cl
    jz .none
    mov al, [sol_wfan]
    or al, al
    jnz .clamp
    mov al, 1
.clamp:
    cmp al, 3
    jbe .lo
    mov al, 3
.lo:
    cmp al, cl
    jbe .out
    mov al, cl
    jmp .out
.none:
    xor al, al
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; sol_newgame - shuffle a fresh deck into [sol_deck], then deal it
; sol_deal    - deal [sol_deck] again, which is what Restart Deal is
; in:  nothing; out: nothing; both preserve every register and draw nothing
; -----------------------------------------------------------------------------
sol_newgame:
    push ax
    push bx
    push cx
    push dx
    push di
    xor bx, bx                      ; the deck in suit order...
.build:
    mov ax, bx
    mov cl, 13
    div cl                          ; AL = suit, AH = rank
    mov cl, 4
    shl al, cl
    or al, ah
    mov [sol_deck+bx], al
    inc bx
    cmp bx, 52
    jb .build

    mov di, 51                      ; ...then Fisher-Yates down it
.shuf:
    call OSAPI_RAND                 ; AX = a pseudo-random word
    xor dx, dx
    mov bx, di
    inc bx
    div bx                          ; DX = 0 .. DI
    mov bx, dx
    mov al, [sol_deck+bx]
    mov bx, di
    mov ah, [sol_deck+bx]
    mov [sol_deck+bx], al
    mov bx, dx
    mov [sol_deck+bx], ah
    dec di
    jnz .shuf
    call sol_deal
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sol_deal:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
.clear:
    mov byte [sol_pcnt+bx], 0
    inc bx
    cmp bx, SOL_NPILE
    jb .clear
    mov byte [sol_won], 0
    mov byte [sol_wfan], 0

    xor si, si                      ; SI walks the deck
    xor cl, cl                      ; CL = column
.col:
    xor ch, ch                      ; CH = the card within it
.card:
    mov bx, si
    mov ah, [sol_deck+bx]
    inc si
    cmp ch, cl
    jne .down
    or ah, C_FACEUP                 ; the last card of each column shows
.down:
    mov al, cl
    call sol_push
    inc ch
    cmp ch, cl
    jbe .card
    inc cl
    cmp cl, 7
    jb .col
.stock:
    mov bx, si
    mov ah, [sol_deck+bx]
    inc si
    mov al, P_STOCK
    call sol_push
    cmp si, 52
    jb .stock
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_stock - the stock was clicked: deal, or turn the waste back over
; in:  nothing; out: nothing; preserves all registers. Draws nothing.
; -----------------------------------------------------------------------------
sol_stock:
    push ax
    push bx
    push cx
    push dx
    mov al, P_STOCK
    call sol_count
    or cl, cl
    jz .recycle
    mov dl, 1
    cmp byte [sol_draw3], 0
    je .deal
    mov dl, 3
.deal:
    xor dh, dh                      ; DH = how many actually turned
.one:
    mov al, P_STOCK
    call sol_count
    or cl, cl
    jz .dealt
    mov al, P_STOCK
    call sol_pop                    ; AH = the card
    or ah, C_FACEUP
    mov al, P_WASTE
    call sol_push
    inc dh
    dec dl
    jnz .one
.dealt:
    mov [sol_wfan], dh
    jmp .out
.recycle:
    mov al, P_WASTE
    call sol_count
    or cl, cl
    jz .out
.back:
    mov al, P_WASTE
    call sol_pop
    and ah, C_NOTUP
    mov al, P_STOCK
    call sol_push
    mov al, P_WASTE
    call sol_count
    or cl, cl
    jnz .back
    mov byte [sol_wfan], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_checkwin - are all four foundations full?
; in:  nothing; out: [sol_won] set or CLEARED; preserves all registers
;
; It clears as well as sets, because a won game is not a dead end: the cards
; on the foundations can still be dragged back off, and a flag that only ever
; went up would leave the plaque on the felt after the win it named was
; undone. sol_domove is what turns that into a repaint.
; -----------------------------------------------------------------------------
sol_checkwin:
    push ax
    push cx
    mov byte [sol_won], 0
    mov al, P_FND0
.f:
    call sol_count
    cmp cl, 13
    jne .out
    inc al
    cmp al, P_FND0 + 4
    jb .f
    mov byte [sol_won], 1
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_tofnd - send a pile's top card to a foundation if any will take it
; in:  AL = source pile; gfx lock held, origin tracked
; out: CF = 0 and the move is made and painted; CF = 1 nothing doing
; clobbers: nothing (every register preserved)
; -----------------------------------------------------------------------------
sol_tofnd:
    push ax
    push bx
    push cx
    push dx
    mov [sol_tfs], al
    call sol_count
    or cl, cl
    jz .no
    call sol_topcard
    mov dl, al                      ; DL = the card
    test dl, C_FACEUP
    jz .no
    xor dh, dh                      ; DH = the foundation being tried
.try:
    mov al, dl
    mov ah, dh
    call sol_canfnd
    jnc .go
    inc dh
    cmp dh, 4
    jb .try
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.go:
    mov al, [sol_tfs]
    mov ah, dh
    add ah, P_FND0
    mov cl, 1
    call sol_domove
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; =============================================================================
; Input
; =============================================================================

; -----------------------------------------------------------------------------
; sol_onclick - W_ONCLICK: deal, turn a card over, or start a drag
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call sol_track
    call sol_abdismiss              ; a click anywhere takes the credits down
    jc .out                         ; and does nothing else
    sub cx, [sol_ox]                ; content-relative
    sub dx, [sol_oy]
    call sol_hit                    ; AL = pile, AH = card index
    jc .out
    cmp al, P_STOCK
    jne .grab
    call sol_cmd_deal               ; the same path the Space key takes, so
    jmp .out                        ; the redraw rule lives in one place
.grab:
    call sol_grab                   ; CF = 1: there is nothing to drag
    jc .out
    call sol_drag
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
; sol_hit - what is under a content-relative point?
; in:  CX = x, DX = y (content-relative)
; out: CF = 0 and AL = pile, AH = card index; CF = 1 nothing
; clobbers: AX (BX, CX, DX, SI, DI preserved)
;
; A tableau column is walked from the TOP card DOWN, so the first card whose
; rect contains the point is the one the player can actually see there. The
; waste is tested last and at its fanned position, because it is the one pile
; whose top card does not sit on its own column.
; -----------------------------------------------------------------------------
sol_hit:
    push bx
    push cx
    push dx
    push si
    mov [sol_htx], cx
    mov [sol_hty], dx
    cmp dx, [sol_taby]
    jae .tableau

    ; --- the top row ---------------------------------------------------------
    cmp dx, [sol_topy]
    jb .none
    mov ax, [sol_topy]
    add ax, [sol_ch]
    cmp dx, ax
    jae .none
    mov al, P_STOCK
    call sol_inx
    jnc .stock
    mov al, P_FND0
.fnd:
    call sol_inx
    jnc .fndhit
    inc al
    cmp al, P_FND0 + 4
    jb .fnd
    call sol_wshow                  ; AL = cards shown, 0 when it is empty
    or al, al
    jz .none
    mov ah, 0
    dec ax
    mul word [sol_wstep]            ; the top card's extra offset
    mov si, ax
    mov al, P_WASTE
    call sol_pilexy                 ; CX = x
    add cx, si
    mov bx, [sol_htx]
    cmp bx, cx
    jb .none
    add cx, [sol_cw]
    cmp bx, cx
    jae .none
    mov al, P_WASTE
    call sol_count
    dec cl
    mov ah, cl
    jmp .hit
.stock:
    mov al, P_STOCK
    xor ah, ah
    jmp .hit
.fndhit:
    call sol_count
    or cl, cl
    jz .none                        ; an empty foundation is not a grab
    dec cl
    mov ah, cl
    jmp .hit

    ; --- a tableau column ----------------------------------------------------
.tableau:
    xor al, al
.tcol:
    call sol_inx
    jnc .tgot
    inc al
    cmp al, P_TABN
    jb .tcol
    jmp .none
.tgot:
    mov [sol_htp], al
    call sol_count
    or cl, cl
    jz .none
    call sol_colfan
    dec cl                          ; CL = index, walked from the top down
.tcard:
    mov al, cl
    call sol_yoff
    add ax, [sol_taby]
    mov si, ax
    cmp [sol_hty], si
    jb .tdown
    add si, [sol_ch]
    cmp [sol_hty], si
    jae .tdown
    mov al, [sol_htp]
    mov ah, cl
    jmp .hit
.tdown:
    dec cl
    jns .tcard
.none:
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
.hit:
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; sol_inx - is [sol_htx] inside this pile's card-wide column?
; in:  AL = pile index
; out: CF = 0 inside; preserves all registers
; -----------------------------------------------------------------------------
sol_inx:
    push ax
    push bx
    push cx
    push dx
    call sol_pilexy                 ; CX = x
    mov bx, [sol_htx]
    cmp bx, cx
    jb .no
    add cx, [sol_cw]
    cmp bx, cx
    jae .no
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sol_grab - decide what a press picks up, and arm the drag state
; in:  AL = pile, AH = card index; gfx lock held, origin tracked
; out: CF = 0 the drag state is armed; CF = 1 there is nothing to drag
; clobbers: nothing (every register preserved)
;
; A tableau run may only be lifted whole and only if it is ALREADY a legal
; sequence - the same rule the drop applies, checked before the outline goes
; up rather than after it comes down. A face-down top card is not a grab at
; all: it turns over, which is the other thing a click on a column can mean.
; -----------------------------------------------------------------------------
sol_grab:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sol_dsrc], al
    mov [sol_didx], ah
    cmp al, P_TABN
    jae .single

    ; --- a tableau column ----------------------------------------------------
    call sol_count
    mov [sol_gcnt], cl
    call sol_pilebase
    mov al, [sol_didx]
    mov ah, 0
    add bx, ax
    mov si, bx                      ; SI -> the card that was pressed
    mov al, [si]
    test al, C_FACEUP
    jnz .run
    mov al, [sol_didx]              ; face down: the top one turns over, and
    inc al                          ; a buried one does nothing at all
    cmp al, [sol_gcnt]
    jne .no
    or byte [si], C_FACEUP
    mov al, [sol_dsrc]
    call sol_drawpile
    jmp .no
.run:
    mov al, [sol_gcnt]
    sub al, [sol_didx]
    mov [sol_dnum], al              ; cards in the hand
    cmp al, 1
    jbe .tabok
    mov di, si
    mov dl, al
    dec dl                          ; DL = adjacent pairs left to check
.seq:
    mov al, [di]
    mov ah, [di+1]
    push ax
    and al, C_RANK
    and ah, C_RANK
    inc ah
    cmp al, ah
    pop ax
    jne .no                         ; must descend by one...
    push ax
    call sol_isred
    mov bl, 0
    jnc .c1
    mov bl, 1
.c1:
    pop ax
    mov al, ah
    call sol_isred
    mov bh, 0
    jnc .c2
    mov bh, 1
.c2:
    cmp bl, bh
    je .no                          ; ...and alternate colour
    inc di
    dec dl
    jnz .seq
.tabok:
    mov al, [sol_dsrc]
    call sol_colfan                 ; the hand is fanned as its column is
    mov ax, [sol_cfu]
    mov [sol_dfan], ax
    mov al, [sol_didx]
    call sol_yoff
    mov [sol_dtop], ax
    jmp .rect

    ; --- the waste or a foundation: the top card, alone ----------------------
.single:
    call sol_count
    or cl, cl
    jz .no
    dec cl
    cmp cl, [sol_didx]
    jne .no                         ; only the card that is really on top
    mov byte [sol_dnum], 1
    mov ax, [sol_ch]
    mov [sol_dfan], ax
    xor ax, ax
    mov [sol_dtop], ax

.rect:
    mov al, [sol_dsrc]
    call sol_pilexy                 ; CX = x, DX = y (content-relative)
    mov [sol_dtx], cx
    mov [sol_dty], dx
    cmp byte [sol_dsrc], P_WASTE
    jne .norm
    call sol_wshow                  ; the waste's top card sits fanned right
    mov ah, 0
    dec ax
    mul word [sol_wstep]
    add [sol_dtx], ax
.norm:
    mov ax, [sol_dtx]
    add ax, [sol_ox]
    mov [sol_dox], ax
    mov ax, [sol_dty]
    add ax, [sol_dtop]
    add ax, [sol_oy]
    mov [sol_doy], ax
    mov ax, [sol_cw]
    mov [sol_dw], ax
    mov al, [sol_dnum]              ; height = the fan plus one whole card
    mov ah, 0
    dec ax
    mul word [sol_dfan]
    add ax, [sol_ch]
    mov [sol_dh], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sol_drag - follow the pointer with an XOR outline, then drop
; in:  the drag state armed by sol_grab; gfx lock HELD
; out: nothing; gfx lock still held; preserves all registers
;
; ui_drag's loop (SPEC.md 13), and its ordering is the binding part: the
; outline is XOR-erased BEFORE the lock is released and redrawn after it is
; taken again, because XOR is self-inverting only while nothing else touches
; the pixels underneath. The linger holds the outline lit for a whole tick,
; so the cursor blits inside the unlock/lock pair cannot dominate the loop
; and the outline is lit far more often than it is dark.
;
; The button is sampled by LEVEL rather than by event, which the kernel's own
; drag cannot do (it must not swallow a re-press). Here it is exactly right:
; a press and release too quick to be seen as movement lands on .up with the
; outline still over the card it came from, and that is the auto-play gesture.
; -----------------------------------------------------------------------------
sol_drag:
    push ax
    push bx
    push cx
    push dx
    mov ax, [sol_dox]
    mov [sol_dsx], ax               ; where the hand started, for the
    mov ax, [sol_doy]               ; did-it-move test at the end
    mov [sol_dsy], ax
    call OSAPI_MOUSE                ; the grab offset inside the outline
    mov ax, cx
    sub ax, [sol_dox]
    mov [sol_dgx], ax
    mov ax, dx
    sub ax, [sol_doy]
    mov [sol_dgy], ax
    call sol_dxor
.pass:
    call sol_linger                 ; lit, lock held, until the tick turns
    call sol_dxor                   ; erase...
    call OSAPI_GFX_UNLOCK           ; ...only then may the lock drop
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .up
    mov ax, cx
    sub ax, [sol_dgx]
    mov [sol_dox], ax
    mov ax, dx
    sub ax, [sol_dgy]
    mov [sol_doy], ax
    call sol_dxor                   ; redraw every pass, moved or not
    jmp .pass
.up:
    call sol_drop
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_linger - yield until the timer tick changes, gfx lock HELD throughout
; sol_pace   - the same wait with the lock RELEASED across it, so what was
;              just drawn reaches the glass (it only
;              does so at gfx_unlock's flush, SPEC.md 32) and other tasks run
; in:  nothing; out: nothing; both preserve every register
; -----------------------------------------------------------------------------
sol_linger:
    push ax
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .spin
    pop bx
    pop ax
    ret

sol_pace:
    call OSAPI_GFX_UNLOCK
    call sol_linger
    call OSAPI_GFX_LOCK
    ret

; -----------------------------------------------------------------------------
; sol_dxor - draw (or erase - XOR is its own inverse) the hand's outline
; in:  [sol_dox]/[sol_doy]/[sol_dw]/[sol_dh]/[sol_dnum]/[sol_dfan]
; out: nothing; preserves all registers
;
; The bounding rectangle plus one rule at each card boundary, so a hand of
; seven reads as seven cards rather than one tall one. Both primitives clip
; to the screen, so an outline dragged off the edge is fine.
; -----------------------------------------------------------------------------
sol_dxor:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [sol_dox]
    mov bx, [sol_doy]
    mov cx, ax
    add cx, [sol_dw]
    dec cx
    mov dx, bx
    add dx, [sol_dh]
    dec dx
    call OSAPI_GFX_XOR_RECT
    mov cl, [sol_dnum]
    mov ch, 0
    dec cx
    jcxz .out
    mov di, cx                      ; DI = rules still to draw
    mov si, [sol_doy]
.rule:
    add si, [sol_dfan]
    mov ax, [sol_dox]
    mov bx, si
    mov dx, si
    mov cx, ax
    add cx, [sol_dw]
    dec cx
    call OSAPI_GFX_XOR_FILL         ; AX=x1, BX=y1, CX=x2, DX=y2: one row
    dec di
    jnz .rule
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_drop - the button came up: play the hand, or leave it where it was
; in:  the drag state; the outline is already erased; gfx lock held
; out: nothing; preserves all registers
;
; The target is chosen by the CENTRE of the hand's TOP CARD, not by the
; pointer: the pointer may be anywhere inside a seven-card outline, and what
; the player is aiming is the card. An illegal drop repaints nothing at all -
; the cards never moved, so the screen is already right.
; -----------------------------------------------------------------------------
sol_drop:
    push ax
    push bx
    push cx
    push dx
    mov ax, [sol_dox]               ; did it move at all?
    sub ax, [sol_dsx]
    jns .absx
    neg ax
.absx:
    mov bx, [sol_doy]
    sub bx, [sol_dsy]
    jns .absy
    neg bx
.absy:
    cmp ax, 3
    ja .moved
    cmp bx, 3
    ja .moved
    cmp byte [sol_dnum], 1          ; a press and release in place sends the
    jne .out                        ; one card home, if it will go
    mov al, [sol_dsrc]
    call sol_tofnd
    jmp .out

.moved:
    mov ax, [sol_dox]               ; the hand's top card, centred
    sub ax, [sol_ox]
    mov bx, [sol_cw]
    shr bx, 1
    add ax, bx
    mov [sol_dcx], ax
    mov ax, [sol_doy]
    sub ax, [sol_oy]
    mov bx, [sol_ch]
    shr bx, 1
    add ax, bx
    mov [sol_dcy], ax

    mov ax, [sol_dcx]               ; which column is that over?
    sub ax, [sol_marg]
    mov bx, [sol_gap]
    shr bx, 1
    add ax, bx
    jns .col
    xor ax, ax
.col:
    xor dx, dx
    div word [sol_pitch]
    cmp ax, P_TABN
    jae .out                        ; past the right-hand end of the board
    mov [sol_dcol], al

    mov ax, [sol_taby]
    cmp [sol_dcy], ax
    jae .totab

    mov al, [sol_dcol]              ; the top row: only a foundation takes a
    cmp al, 3                       ; drop, and only ever one card
    jb .out
    cmp byte [sol_dnum], 1
    jne .out
    sub al, 3
    mov bl, al                      ; BL = the foundation, 0..3
    mov al, [sol_dsrc]
    call sol_topcard                ; AL = the card being dropped
    mov ah, bl
    call sol_canfnd
    jc .out
    mov al, [sol_dsrc]
    mov ah, bl
    add ah, P_FND0
    mov cl, 1
    call sol_domove
    jmp .out

.totab:
    mov al, [sol_dcol]
    cmp al, [sol_dsrc]
    je .out                         ; back onto its own column: nothing
    mov bl, al                      ; BL = the destination column
    call sol_handcard               ; AL = the BOTTOM card of the hand
    mov ah, bl
    call sol_cantab
    jc .out
    mov al, [sol_dsrc]
    mov ah, bl
    mov cl, [sol_dnum]
    call sol_domove
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_handcard - the lowest card of the hand, the one a drop is judged on
; in:  the drag state
; out: AL = the card byte; preserves all other registers
; -----------------------------------------------------------------------------
sol_handcard:
    push bx
    push cx
    mov al, [sol_dsrc]
    call sol_pilebase
    mov cl, [sol_didx]
    mov ch, 0
    add bx, cx
    mov al, [bx]
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_onkey - W_ONKEY: N new, R restart, A auto-finish, Space deal
; in:  AL = ascii, AH = scan, SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bl, al                      ; keep the key; sol_track needs AX
    call sol_track
    call sol_abdismiss              ; any key takes the credits down, and is
    jc .out                         ; spent doing it
    cmp bl, 'n'
    je .new
    cmp bl, 'N'
    je .new
    cmp bl, 'r'
    je .restart
    cmp bl, 'R'
    je .restart
    cmp bl, 'a'
    je .auto
    cmp bl, 'A'
    je .auto
    cmp bl, ' '
    je .deal
    jmp .out
.new:
    call sol_cmd_new
    jmp .out
.restart:
    call sol_cmd_restart
    jmp .out
.auto:
    call sol_cmd_auto
    jmp .out
.deal:
    call sol_cmd_deal
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
; sol_oncmd - AM_ONCMD: run a menu item (SPEC.md 12.2)
; in:  AL = item index, AH = menu index, SI = our window, BX = the set;
;      caller (the UI task) holds the gfx lock
; out: nothing; clobbers AX, BX, CX, DX like any window callback
;
; Called exactly like W_ONCLICK, so it draws directly and must not lock,
; block or spawn - and it must repaint itself, which every command below
; does, because the kernel does not repaint after a menu command returns.
; -----------------------------------------------------------------------------
sol_oncmd:
    push si
    push di
    push bp
    mov bl, al                      ; the item; sol_track needs AX
    mov bh, ah                      ; the menu
    call sol_track
    or bh, bh
    jnz .deal
    or bl, bl
    jz .new
    cmp bl, 1
    je .restart
    cmp bl, 2
    je .auto
    jmp .out
.new:
    call sol_cmd_new
    jmp .out
.restart:
    call sol_cmd_restart
    jmp .out
.auto:
    call sol_cmd_auto
    jmp .out
.deal:
    mov byte [sol_draw3], 0
    or bl, bl
    jz .mode
    mov byte [sol_draw3], 1
.mode:
    call sol_dealmenu
.out:
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; sol_cmd_* - the commands themselves, shared by the keys and the menu
; in:  the origin already tracked; gfx lock held
; out: nothing; preserve all registers
; -----------------------------------------------------------------------------
sol_cmd_new:
    call sol_newgame
    call sol_drawall
    ret

sol_cmd_restart:
    call sol_deal
    call sol_drawall
    ret

; The stock's PICTURE is one bit - a card back, or the turn-over-again ring -
; so it needs redrawing only when that bit flips: when the last card leaves it,
; and when a recycle refills it. Dealing from a stock that still has cards
; leaves exactly the picture that is already on the screen, and redrawing it
; is not cheap: the back is a lattice, so gfx_blit4 coalesces it into 634
; gfx_fill runs on the 32x44 metrics (336 on CGA's 28x28). That was being paid
; on every single click of the stock, which is the one thing a player does
; over and over.
sol_cmd_deal:
    push ax
    push bx
    push cx
    mov al, P_STOCK
    call sol_count
    mov bl, 0
    or cl, cl
    jz .before
    mov bl, 1                       ; BL = "the stock showed a card" before
.before:
    call sol_stock
    mov al, P_STOCK
    call sol_count
    mov bh, 0
    or cl, cl
    jz .after
    mov bh, 1                       ; ...and after
.after:
    cmp bl, bh
    je .waste                       ; same picture: leave it alone
    mov al, P_STOCK
    call sol_drawpile
.waste:
    mov al, P_WASTE
    call sol_drawpile
    pop cx
    pop bx
    pop ax
    ret

; Auto Finish: send everything that will go, a move a tick so it reads as
; play rather than as a repaint. The lock is dropped inside sol_pace, which
; is what puts each move on the glass before the next one is computed.
sol_cmd_auto:
    push ax
.round:
    mov al, P_WASTE
    call sol_tofnd
    jnc .again
    xor al, al
.tab:
    call sol_tofnd
    jnc .again
    inc al
    cmp al, P_TABN
    jb .tab
    jmp .out
.again:
    call sol_pace
    jmp .round
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_dealmenu - grey out whichever Deal item is already in force
; in:  [sol_draw3]; out: nothing; preserves all registers
;
; MENU_DIS (SPEC.md 12.2) is the whole of it: an item string that begins with
; that byte is drawn grey and never selected, so pointing the two items at
; their disabled twins turns the menu into a radio pair with no new kernel
; support at all.
; -----------------------------------------------------------------------------
sol_dealmenu:
    cmp byte [sol_draw3], 0
    jne .three
    mov word [sol_mi_deal], sol_s_d1x
    mov word [sol_mi_deal+2], sol_s_d3
    ret
.three:
    mov word [sol_mi_deal], sol_s_d1
    mov word [sol_mi_deal+2], sol_s_d3x
    ret

; =============================================================================
; Data
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; x/y/w/h are computed by sol_entry from the live screen; the four procs are
; all that is really pinned here.
sol_tpl:
    dw 0, 0, 0, 0
    dw sol_ttl, sol_paint, sol_onkey, sol_onclick

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
    OS88_MENUSET sol_menus, sol_m_name, sol_oncmd
        OS88_MENU sol_m_game, sol_mi_game, 3
        OS88_MENU sol_m_deal, sol_mi_deal, 2
    OS88_MENUSET_END sol_menus

sol_m_name:  db 'Solitaire', 0
sol_m_game:  db 'Game', 0
sol_mi_game: dw sol_s_new, sol_s_rest, sol_s_auto
sol_s_new:   db 'New Game', 0
sol_s_rest:  db 'Restart Deal', 0
sol_s_auto:  db 'Auto Finish', 0
sol_m_deal:  db 'Deal', 0
sol_mi_deal: dw sol_s_d1x, sol_s_d3     ; draw-one is the zero state, so it
sol_s_d1:    db 'Draw One', 0           ; starts as the greyed-out one
sol_s_d1x:   db MENU_DIS, 'Draw One', 0
sol_s_d3:    db 'Draw Three', 0
sol_s_d3x:   db MENU_DIS, 'Draw Three', 0

sol_ttl:     db 'Solitaire', 0
sol_s_win:   db 'You win!', 0

; Rank glyphs, indexed by rank 0..12. Zero means "the ten", the one rank that
; --- the About panel's credit (SPEC.md 43.8) -----------------------------------
; A NUL-terminated array of line pointers, measured rather than pinned
; (sol_abmeas): the widest line sets the width and the count sets the
; height. Keep every line inside 24 glyphs - CGA gives this window 220px of
; content, which is 27 glyphs less the panel's own margins (SPEC.md 39).
SOL_ABLH equ 11                     ; line pitch, px (8px glyphs + 3 of air)
sol_ablines:
    dw sol_ab1, sol_ab2, sol_ab3, sol_ab4, 0
sol_ab1:     db 'Solitaire for os8088', 0
sol_ab2:     db 'Klondike, in 8086 asm', 0
sol_ab3:     db 0                   ; a blank line is a line with no glyphs
sol_ab4:     db 'Contributed by Elendilon', 0

; needs two glyphs, and sol_drawface spells it out.
sol_rankch:
    db 'A', '2', '3', '4', '5', '6', '7', '8', '9', 0, 'J', 'Q', 'K'

; --- metrics records (SOL_NMET words each, copied into bss by sol_entry) --------
; card w, card h, gap, margin, top row y, face-down fan, face-up fan,
; rank x, rank y, corner pip x, corner pip y, centre pip x, centre pip y.
; The face-up fan must clear the rank glyph's 8 rows or a buried card cannot
; be read; both records do.
sol_met_big:                        ; VGA 640x480 and Hercules 720x348
    dw 32, 44, 4, 4, 4, 5, 14, 3, 3, 21, 3, 8, 22
sol_met_sml:                        ; CGA 640x200: 156 rows of desktop, all in
    dw 28, 28, 3, 3, 1, 3, 12, 2, 2, 18, 2, 6, 10

; --- suit pips, 1 bit per pixel, bit 15 leftmost -------------------------------
; Solid for every suit on a colour screen; on 1bpp the two red suits switch to
; the hollow masks below, which is the only thing carrying red vs black there
; (SPEC.md 39.4). The 8x8 masks are stored left-justified in words so one
; routine - sol_maskrun - plots both sizes.
sol_p8s:                            ; spade
    dw 0x1800, 0x3C00, 0x7E00, 0xFF00, 0xFF00, 0x6600, 0x1800, 0x3C00
    dw 0x6600, 0xFF00, 0xFF00, 0xFF00, 0x7E00, 0x3C00, 0x1800, 0x0000
    dw 0x1800, 0x3C00, 0x7E00, 0xFF00, 0x7E00, 0x3C00, 0x1800, 0x0000
    dw 0x1800, 0x3C00, 0x3C00, 0xBD00, 0xFF00, 0xDB00, 0x1800, 0x3C00

sol_p8h:                            ; hollow heart, hollow diamond
    dw 0x6600, 0x9900, 0x8100, 0x8100, 0x4200, 0x2400, 0x1800, 0x0000
    dw 0x1800, 0x2400, 0x4200, 0x8100, 0x4200, 0x2400, 0x1800, 0x0000

sol_p16s:                           ; spade
    dw 0x0180, 0x03C0, 0x07E0, 0x0FF0, 0x1FF8, 0x3FFC, 0x7FFE, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0x7E7E, 0x3C3C, 0x0180, 0x03C0, 0x0FF0, 0x3FFC
                                    ; heart
    dw 0x3C3C, 0x7E7E, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFE
    dw 0x7FFE, 0x3FFC, 0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180, 0x0000
                                    ; diamond
    dw 0x0180, 0x03C0, 0x07E0, 0x0FF0, 0x1FF8, 0x3FFC, 0x7FFE, 0xFFFF
    dw 0xFFFF, 0x7FFE, 0x3FFC, 0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180
                                    ; club
    dw 0x03C0, 0x07E0, 0x0FF0, 0x0FF0, 0x07E0, 0x33CC, 0x7BDE, 0xFFFF
    dw 0xFFFF, 0x7BDE, 0x33CC, 0x03C0, 0x03C0, 0x07E0, 0x1FF8, 0x3FFC

sol_p16h:                           ; hollow heart
    dw 0x3C3C, 0x4242, 0x8181, 0x8001, 0x8001, 0x8001, 0x8001, 0x4002
    dw 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180, 0x0000
                                    ; hollow diamond
    dw 0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x2004, 0x4002, 0x8001
    dw 0x8001, 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180

; The empty stock's mark: a ring, for "round again".
sol_ic_ring:
    dw 0x07E0, 0x1FF8, 0x381C, 0x700E, 0x6006, 0xE007, 0xC003, 0xC003
    dw 0xC003, 0xC003, 0xE007, 0x6006, 0x700E, 0x381C, 0x1FF8, 0x07E0

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes SOL_BSS bytes after the image, and
; every name below is an offset from os88_image_end). Assigned by the macros
; rather than written out, the way apps/paint does it - a hand-maintained
; running total over this many names is a defect waiting to happen.
;
; All zero is a coherent state: draw-one, nothing won, an empty waste fan.
; The deal itself is not, which is why sol_entry deals before it creates the
; window rather than leaving it to the first paint.
; =============================================================================

%assign SOL_BSS 0
%macro SWORD 1
%1 equ os88_image_end + SOL_BSS
%assign SOL_BSS SOL_BSS + 2
%endmacro
%macro SBYTE 1
%1 equ os88_image_end + SOL_BSS
%assign SOL_BSS SOL_BSS + 1
%endmacro
%macro SBUF 2
%1 equ os88_image_end + SOL_BSS
%assign SOL_BSS SOL_BSS + (%2)
%endmacro

; --- the metrics, copied WHOLESALE out of sol_met_* --------------------------
; These thirteen words must stay in this order and stay contiguous: sol_entry
; copies the record over them with one loop.
    SWORD sol_cw                    ; card width
    SWORD sol_ch                    ; card height
    SWORD sol_gap                   ; gap between columns
    SWORD sol_marg                  ; left/top margin inside the content
    SWORD sol_topy                  ; content y of the stock/foundation row
    SWORD sol_fand                  ; face-down fan step
    SWORD sol_fanu                  ; face-up fan step
    SWORD sol_rx                    ; rank glyph, card-relative
    SWORD sol_ry
    SWORD sol_psx                   ; corner pip, card-relative
    SWORD sol_psy
    SWORD sol_plx                   ; centre pip, card-relative
    SWORD sol_ply

; --- derived once, in sol_entry ----------------------------------------------
    SWORD sol_pitch                 ; card width + gap
    SWORD sol_taby                  ; content y of the tableau
    SWORD sol_wstep                 ; the waste's horizontal fan step
    SWORD sol_stride                ; bytes per row of the back image
    SWORD sol_scrw                  ; screen width
    SWORD sol_dock                  ; first row the dock owns

; --- refreshed by sol_track, every callback ----------------------------------
    SWORD sol_ox                    ; content origin, screen coords
    SWORD sol_oy
    SWORD sol_cwid                  ; content width
    SWORD sol_chgt                  ; content height
    SWORD sol_avail                 ; deepest a column's last card may start

; --- the game ----------------------------------------------------------------
    SBUF  sol_pcnt, SOL_NPILE       ; cards in each pile
    SBUF  sol_pile, SOL_NPILE*PILE_CAP
    SBUF  sol_deck, 52              ; the shuffled deal, kept for Restart
    SBYTE sol_bpp                   ; 4 or 1 (SPEC.md 39.2)
    SBYTE sol_draw3                 ; 0 = deal one, 1 = deal three
    SBYTE sol_wfan                  ; waste cards to fan, 0..3
    SBYTE sol_won
    SBYTE sol_abon                  ; 1 = the credit panel is up (43.8)
    SWORD sol_abw                   ; its measured width, height, left and top
    SWORD sol_abh                   ; - content coords, all four settled by
    SWORD sol_abl                   ; sol_abmeas before a pixel is drawn
    SWORD sol_abt
    SBYTE sol_bkcol                 ; the card back's field colour

; --- sol_drawpile's loop -----------------------------------------------------
    SBYTE sol_felt                  ; 1 = sol_drawall has just filled the whole
                                    ; content with felt and nothing has drawn
                                    ; over this pile's rect since, so its own
                                    ; wipe is a second fill (SPEC.md 43.9.2).
                                    ; Set only inside that loop; bss arrives
                                    ; zeroed, which is the safe value for the
                                    ; one-pile-at-a-time callers
    SBYTE sol_dpp                   ; the pile being drawn
    SBYTE sol_dpn                   ; its card count
    SBYTE sol_dpi                   ; card index
    SBYTE sol_dpj                   ; waste fan step counter
    SBYTE sol_dpk                   ; waste cards shown
    SWORD sol_dpx                   ; the pile's slot origin, content coords
    SWORD sol_dpy
    SWORD sol_dpo                   ; this card's y offset
    SWORD sol_dpv                   ; ...and its visible height

; --- sol_card / sol_drawface -------------------------------------------------
    SWORD sol_cdx                   ; the card's origin, SCREEN coords
    SWORD sol_cdy
    SWORD sol_cvis
    SBYTE sol_ccard

; --- sol_pilerect ------------------------------------------------------------
    SWORD sol_rx1
    SWORD sol_ry1
    SWORD sol_rx2
    SWORD sol_ry2

; --- sol_maskrun -------------------------------------------------------------
    SWORD sol_mrx
    SWORD sol_mry
    SWORD sol_mrw
    SWORD sol_mrn
    SWORD sol_mrs                   ; column the open run started at, or FFFF

; --- sol_colfan / sol_yoff ---------------------------------------------------
    SWORD sol_cfd                   ; face-down cards in the column
    SWORD sol_cfa                   ; ...their fan step
    SWORD sol_cfu                   ; ...and the face-up one
    SBYTE sol_cfp                   ; the column being measured
    SWORD sol_keepn                 ; leading cards this draw may skip
    SWORD sol_lastox                ; the origin the shadow below belongs to
    SWORD sol_lastoy
    SBUF  sol_shw, P_TABN*SOL_SHW   ; what is on the glass, per column (43.10)

; --- sol_move / sol_domove / sol_tofnd ---------------------------------------
    SBYTE sol_mvs
    SBYTE sol_mvd
    SBYTE sol_mvn
    SBYTE sol_mds
    SBYTE sol_mdd
    SBYTE sol_mdw                   ; [sol_won] as it was before the move
    SBYTE sol_tfs

; --- sol_hit -----------------------------------------------------------------
    SWORD sol_htx
    SWORD sol_hty
    SBYTE sol_htp

; --- the drag ----------------------------------------------------------------
    SBYTE sol_dsrc                  ; the pile the hand came off
    SBYTE sol_didx                  ; the card index inside it
    SBYTE sol_dnum                  ; cards in the hand
    SBYTE sol_gcnt                  ; sol_grab's copy of the source count
    SBYTE sol_dcol                  ; the column a drop landed over
    SWORD sol_dtop                  ; the hand's y offset within its column
    SWORD sol_dfan                  ; the fan the outline's rules use
    SWORD sol_dtx                   ; the hand's origin, content coords
    SWORD sol_dty
    SWORD sol_dox                   ; the outline, SCREEN coords
    SWORD sol_doy
    SWORD sol_dw
    SWORD sol_dh
    SWORD sol_dsx                   ; where it started, for the move test
    SWORD sol_dsy
    SWORD sol_dgx                   ; pointer offset inside the outline
    SWORD sol_dgy
    SWORD sol_dcx                   ; the dropped card's centre
    SWORD sol_dcy

; --- odds and ends -----------------------------------------------------------
    SWORD sol_tmpa
    SWORD sol_tmpb
    SWORD sol_tmpc
    SWORD sol_tmpd
    SBUF  sol_backbuf, SOL_BACKMAX  ; the card back, packed 4bpp

    OS88_BSS SOL_BSS
    OS88_IMAGE_END
