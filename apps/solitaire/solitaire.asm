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

; -----------------------------------------------------------------------------
; APP_SMALL - THE SMALL-APPS BUILD OF THIS PACKAGE (SPEC.md 43.12)
;
; Note Pad's SPEC.md 27.16 is the pattern and its rules hold: one source
; assembled twice, `make smallapps` passes -DAPP_SMALL, never a second ABI.
;
; There is very little to take here and that is the finding, not a shortfall -
; SPEC.md 43.11 is where this package's bytes actually came from, and they
; came off BOTH builds. What is left is two things a game can do without:
;
;   SOLF_AUTO   Auto Finish - the A key and the Game menu's third item. The
;               CLICK-to-foundation convenience stays (a press and release
;               without moving), because that is how the game is played rather
;               than a labelled extra, and it is what sol_tofnd is really for
;   SOLF_ABOUT  the About card
;
; **AT THE TOP** for the reason apps/notepad/notepad.asm's note gives: these
; are preprocessor tests answered in file order.
; -----------------------------------------------------------------------------
%ifndef APP_SMALL
%define SOLF_AUTO
%define SOLF_ABOUT
%endif

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
; is whether the two suit bits differ: the parity flag (see sol_suit).
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
; The deepest each KIND of pile gets, and each is stored at its own depth
; (sol_pofs) rather than all at the deepest - 233 bytes of .bss against 312:
CAP_TAB     equ 19                  ; a column: 6 face-down plus at most a K..A
                                    ; run on top of them - the face-up cards of
                                    ; a column are ALWAYS one descending run
CAP_FND     equ 13                  ; a foundation: its suit, A..K
CAP_SW      equ 24                  ; the stock and the waste: 52 - 28 dealt
SOL_PILES   equ CAP_TAB*7 + CAP_FND*4 + CAP_SW*2

SOL_NMET    equ 14                  ; words in a metrics record (sol_met_*)

; --- THE FIELDS THE TWO PUBLISHED FRAME SIZES ARE A FUNCTION OF -------------
; Named once and spent twice - in the metric records at the bottom of this file
; and in the per-adapter preferences sol_entry publishes (SPEC.md 11.100.1) -
; because two places that must agree about a number is how a size drifts from
; the layout it is meant to hold (apps/arkanoid's ARK_BW_* is the same rule).
SOL_CW_B    equ 32                  ; card, VGA and Hercules
SOL_CH_B    equ 44
SOL_GAP_B   equ 4
SOL_MARG_B  equ 4
SOL_TOPY_B  equ 4
SOL_FAND_B  equ 5
SOL_FANU_B  equ 14
SOL_TGAP_B  equ SOL_GAP_B * 2       ; foundation row to tableau

SOL_CW_S    equ 28                  ; ...and CGA's 200 rows
SOL_CH_S    equ 28
SOL_GAP_S   equ 3
SOL_MARG_S  equ 3
SOL_TOPY_S  equ 1
SOL_FAND_S  equ 3
SOL_FANU_S  equ 12
SOL_TGAP_S  equ 3                   ; **3, NOT 2*GAP.** sol_colfan compresses a
                                    ; deep column to fit, so a pixel taken from
                                    ; the chrome is a pixel the fan does not
                                    ; take from the cards - measured, this one
                                    ; change is a whole fan step for a 9- and
                                    ; an 11-card build. It stops at 3 because 1
                                    ; would buy the King column its seventh
                                    ; pixel and stop reading as a gap at all,
                                    ; which is a worse trade than the pixel

; sol_entry's own arithmetic as constants: content width = 7 columns and 2
; margins less the trailing gap, and the height is the tallest column the game
; can build - 6 face-down and 13 face-up - plus the chrome.
SOL_PW_B    equ (SOL_CW_B + SOL_GAP_B) * 7 + SOL_MARG_B * 2 - SOL_GAP_B + 2
SOL_PH_B    equ SOL_FAND_B * 6 + SOL_FANU_B * 12 + SOL_TOPY_B + SOL_CH_B \
                + SOL_TGAP_B + SOL_CH_B + TITLE_H + 1
SOL_PW_S    equ (SOL_CW_S + SOL_GAP_S) * 7 + SOL_MARG_S * 2 - SOL_GAP_S + 2
SOL_PH_S    equ SOL_FAND_S * 6 + SOL_FANU_S * 12 + SOL_TOPY_S + SOL_CH_S \
                + SOL_TGAP_S + SOL_CH_S + TITLE_H + 1
SOL_BACKMAX equ 704                 ; the back image at the LARGER metrics,
                                    ; (32/2) * 44 bytes of packed 4bpp
SOL_TABLE   equ CGREEN              ; the felt: green on VGA, black on 1bpp
SOL_NOPEN   equ 0xFF                ; [sol_pipink] for a mask the sprite pass
                                    ; must NOT draw - see sol_drawface .ink
SOL_GHOST   equ CDGRAY              ; empty-slot outlines and their ghost
                                    ; pips - in the dither class, so the
                                    ; distinction survives SPEC.md 39.4

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
    push es                         ; the record is copied wholesale, so the
    push ds                         ; bss words must stay in its order - see
    pop es                          ; the note over sol_cw below. It is stored
    mov di, sol_cw                  ; as BYTES - every field is under 128, so
    mov cx, SOL_NMET                ; `cbw` widens it - and the words it lands
.mcopy:                             ; in are what every reader takes
    lodsb
    cbw
    stosw
    loop .mcopy
    pop es

    mov ax, [sol_topy]              ; the tableau clears the top row by the
    add ax, [sol_ch]                ; record's own gap - two of sol_gap on the
    add ax, [sol_tgap]              ; big record, less on the small one, where
    mov [sol_taby], ax              ; every pixel here is one sol_colfan does
                                    ; not have to take from the cards
    mov ax, [sol_cw]                ; the waste fans right by a third of a
    mov bl, 3                       ; card, into the empty column the layout
    div bl                          ; leaves between it and the foundations
    mov ah, 0
    mov [sol_wstep], ax

    mov ax, [sol_cw]                ; column pitch
    add ax, [sol_gap]
    mov [sol_pitch], ax
    mov bl, 7                       ; content width = 2 margins + 7 columns
    mul bl                          ; (7 * 36 is a word: `mul bl` answers AX)
    add ax, [sol_marg]
    add ax, [sol_marg]
    sub ax, [sol_gap]
    mov [sol_cwid], ax              ; ...and AX, which sol_entry reads
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
    mov bx, si                      ; **THE CARD THIS WINDOW IS ON** (SPEC.md
    call OSAPI_WM_DISPLAY           ; 39.16.4), not the primary. This handler
                                    ; fires on a DRAG across the seam as well
                                    ; as on Activate Mode, and OSAPI_VIDEO
                                    ; answers about the primary either way - so
                                    ; on a Hercules-plus-CGA machine a game
                                    ; dragged onto the CGA was re-picking the
                                    ; HERCULES card metrics, which is the whole
                                    ; of what this routine exists to get right.
                                    ; AX=w, BX=h, CX=dock row, SI=band top,
                                    ; DH=bpp
    mov [sol_bpp], dh               ; the bpp banked again, a switch being
                                    ; exactly when it stops being true. (The
                                    ; width and the dock row are not: only
                                    ; sol_entry's PLACEMENT read them, and it
                                    ; has run - so they are not kept at all.)
    call sol_metrics                ; ...and sol_metrics reads BX, which is now
                                    ; THIS card's height: 348 or 200, which is
                                    ; the one number the big/small card choice
                                    ; turns on
    call sol_mkback                 ; ...AND THE BACK IS A FUNCTION OF BOTH:
                                    ; its size is the record's card and its
                                    ; field colour the bpp. Only sol_entry used
                                    ; to render it, so a VGA game taken to CGA
                                    ; blitted the 32x44 blue back through a
                                    ; 28x28 card at the old stride - no right
                                    ; or bottom edge, and the wrong field
                                    ; (SPEC.md 43.12.1). RAM only: legal here
    jmp sol_pop6

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
    call sol_track                  ; both keep every register (sol_track
    jmp sol_drawall                 ; reloads ES, which is KERNEL_SEG here)

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
    push si
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
    mov si, sol_winlines            ; the win plaque: 28 rows, its line on the
    mov cx, 17 * 256 + 10           ; tenth (sol_panel)
    call sol_panel
.about:
%ifdef SOLF_ABOUT
    cmp byte [sol_abon], 0
    je .out
    mov si, sol_ablines             ; the credit card: 16 rows of margin, the
    mov cx, 16 * 256 + 8            ; first line on the eighth
    call sol_panel
%endif
.out:
    pop si
    jmp sol_pop4

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
    call sol_pilexy                 ; CX = x, DX = y (content-relative)
    mov [sol_dpx], cx
    mov [sol_dpy], dx
    mov [sol_ry1], dx               ; the erase's top, which sol_plan moves
    call sol_count                  ; down past any cards it keeps
    mov [sol_dpn], cl
    call sol_plan                   ; how much of a tableau column is already
                                    ; right on the screen (0 for the others)
    cmp byte [sol_felt], 0          ; ...and whether anything needs erasing at
    jne .nowipe                     ; all: sol_drawall has just felt-filled the
                                    ; whole content, so this rect is already
                                    ; the colour the wipe would make it.
    or cl, cl                       ; The stock and a foundation are ONE card
    jz .wipe                        ; in a one-card rect, so a card there
    cmp al, P_FND0                  ; covers every pixel the wipe would - a
    jb .wipe                        ; second fill of the same pixels. A column
    cmp al, P_WASTE                 ; runs the whole content, the waste is two
    jne .nowipe                     ; fan steps wider, an EMPTY pile draws an
.wipe:                              ; outline: those three do need it
    mov al, SOL_TABLE
    call OSAPI_SET_COLOR
    mov ax, [sol_dpx]               ; THE RECT A PILE OWNS: one card wide...
    mov bx, [sol_ry1]
    mov cx, ax
    add cx, [sol_cw]
    dec cx
    mov dx, [sol_chgt]              ; ...a column, everything under it to the
    cmp byte [sol_dpp], P_TABN      ; content bottom...
    jb .tall
    mov dx, [sol_dpy]               ; ...anything else one card tall...
    add dx, [sol_ch]
    cmp byte [sol_dpp], P_WASTE
    jne .tall
    add cx, [sol_wstep]             ; ...and the waste the two fan steps it
    add cx, [sol_wstep]             ; spills to its right, which is what the
.tall:                              ; empty column beside it is for
    dec dx
    call sol_fillc
.nowipe:
    mov al, [sol_dpp]
    call sol_pilebase
    mov si, bx                      ; SI -> the pile's first card
    cmp al, P_STOCK
    je .stock
    cmp al, P_WASTE
    je .waste
    cmp al, P_FND0
    jae .fnd

    ; --- a tableau column: every card, at this column's own fan -------------
    ; SI walks the cards, CH counts them and DI carries the offset of the one
    ; being drawn - which is the NEXT card's offset computed last time round,
    ; so each card costs one sol_yoff and not two.
    cmp byte [sol_dpn], 0
    je .slot
    mov ax, [sol_keepn]             ; sol_plan has already run sol_colfan and
    mov ch, al                      ; decided how many leading cards to skip
    add si, ax
    call sol_yoff
    xchg ax, di                     ; DI = this card's y offset
.tab:
    mov dx, [sol_ch]                ; the last card shows all of itself...
    mov bx, di
    mov al, ch
    inc ax
    cmp al, [sol_dpn]
    jae .tabvis
    call sol_yoff                   ; ...and any other, what the next one
    mov dx, ax                      ; leaves of it
    sub dx, di
    mov di, ax
.tabvis:
    add bx, [sol_dpy]
    mov ax, [sol_dpx]
    mov cl, [si]
    call sol_card
    inc si
    inc ch
    cmp ch, [sol_dpn]
    jb .tab
    jmp .out

    ; --- the stock: one card back, or the turn-over-again ring --------------
.stock:
    cmp byte [sol_dpn], 0
    je .stockempty
    xor cl, cl                      ; face down: only bit 6 is looked at
    jmp .one
.stockempty:
    mov si, sol_ic_ring
    call sol_ghost16                ; the slot, and the ring in it
    jmp .out

    ; --- the waste: up to three cards, fanned right -------------------------
.waste:
    cmp byte [sol_dpn], 0
    je .slot
    call sol_wshow                  ; AL = how many to show, 1..3
    mov ch, al
    mov bl, [sol_dpn]
    sub bl, al
    mov bh, 0
    add si, bx                      ; SI -> the first card that shows
    mov ax, [sol_dpx]
    mov bx, [sol_dpy]
    mov dx, [sol_ch]
.wcard:
    mov cl, [si]
    call sol_card
    inc si
    add ax, [sol_wstep]
    dec ch
    jnz .wcard
    jmp .out

    ; --- a foundation: its top card, or a ghost pip -------------------------
.fnd:
    mov bl, [sol_dpn]
    or bl, bl
    jz .fndempty
    mov bh, 0
    mov cl, [si+bx-1]               ; the card on top
.one:
    mov ax, [sol_dpx]
    mov bx, [sol_dpy]
    mov dx, [sol_ch]
    call sol_card
    jmp .out
.fndempty:
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
    jmp sol_pop6

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
;   +SHW_FAN   the two fans they were drawn at, face-down then face-up, a
;              byte each (a fan step is under 15) and so compared as ONE word
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
SHW_FAN     equ 1                   ; the fans they were drawn at (sol_shwfan)
SHW_CARD    equ 3                   ; the card bytes, as drawn
SOL_SHW     equ SHW_CARD + CAP_TAB  ; ...one row per tableau column

; -----------------------------------------------------------------------------
; sol_shwbase - a column's shadow row
; in:  AL = tableau pile; out: SI = the row; preserves all registers but SI
; -----------------------------------------------------------------------------
sol_shwbase:
    push ax
    push dx
    mov dl, SOL_SHW
    mul dl                          ; AX = pile * SOL_SHW (6*22 = 132, one byte)
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
    call sol_shwfan
    cmp ax, [si + SHW_FAN]
    jne .stop                       ; a re-fan moved every card in the column
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
    call sol_pilebase               ; BX = the pile (AL is still it)
    call sol_shwfan                 ; sol_colfan has run for any column with a
    mov [si + SHW_FAN], ax          ; card in it; an empty one records 0 cards
    mov cl, [sol_dpn]               ; and sol_keep never reads the rest
    mov [si + SHW_N], cl
    mov ch, 0
    jcxz .out
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

; sol_shwfan - AX = the fan this column is being drawn at, as a shadow holds it
sol_shwfan:
    mov al, [sol_cfa]
    mov ah, [sol_cfu]
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
    jmp sol_pop4

; -----------------------------------------------------------------------------
; sol_ghost16 - a 16x16 mask centred in an empty pile, in the dither class
; in:  SI = mask, [sol_dpx]/[sol_dpy]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sol_ghost16:
    call sol_slot                   ; ...which leaves the pen SOL_GHOST: the
    push ax                         ; ghost's colour too.
    push bx                         ; CDGRAY, which is a DITHER on 1bpp: it
    push cx                         ; has to be composed per pixel by the walk,
    push dx
    mov byte [sol_pipink], SOL_NOPEN ; and the sprite pen would lay it down
                                    ; FLAT - black on black felt, so the ghost
                                    ; would simply vanish (SPEC.md 43.11.3)
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
    mov dx, cx
    call sol_maskrun
    jmp sol_pop4

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
    mov [sol_ccard], cl
    mov si, [sol_chgt]
    sub si, bx                      ; SI = rows left inside our own content
    jle .out
    add ax, [sol_ox]
    mov [sol_cdx], ax
    add bx, [sol_oy]
    mov [sol_cdy], bx
    cmp dx, [sol_ch]                ; no more than the card...
    jbe .vok
    mov dx, [sol_ch]
.vok:
    cmp dx, si                      ; ...and no more than the content
    jbe .vok2
    mov dx, si
.vok2:
    or dx, dx
    jz .out
    mov [sol_cvis], dx
    test cl, C_FACEUP
    jnz .face
    call sol_drawback
    jmp .out
.face:
    call sol_drawface
.out:
    pop bp
    jmp sol_pop6

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

    push ax                         ; THE FACE'S OWN RECT IS THE EDGES' TOO:
    mov al, CBLACK                  ; AX..DX are still (x1,y1)-(x2,y2) out of
    call OSAPI_SET_COLOR            ; the fill, which keeps them, so each edge
    pop ax                          ; is a register move from it rather than
    push bx                         ; four words re-read and re-added
    push dx
    mov dx, bx                      ; top edge: x1..x2 at y1
    mov bx, cx
    call OSAPI_GFX_HLINE
    pop dx
    pop bx
    call OSAPI_GFX_VLINE            ; left edge: x1, y1..y2
    xchg ax, cx
    call OSAPI_GFX_VLINE            ; right edge: x2, y1..y2
    xchg ax, cx
    mov si, [sol_cvis]              ; bottom edge, only when this really is
    cmp si, [sol_ch]                ; the bottom of the card - and then y2 IS
    jb .noedge                      ; that bottom, [sol_cvis] being clamped
    mov bx, cx                      ; to the card's height
    call OSAPI_GFX_HLINE
.noedge:

    test byte [sol_ccard], 0x30     ; the ink: red only where red survives
    mov al, CBLACK                  ; (PF: an even pair of suit bits is a
    jpe .ink                        ; black suit - see sol_suit)
    cmp byte [sol_bpp], 4
    jne .ink                        ; on 1bpp index 12 reduces to WHITE, which
    mov al, CLRED                   ; on a white face is nothing at all
.ink:
    mov [sol_pipink], al            ; ...and ARM the sprite pass with it: the
    call OSAPI_SET_COLOR            ; pips are masks, and the pen they go down
                                    ; through is its OWN (SPEC.md 25.6), not
                                    ; [gfx_color]. It lays a colour SOLID, so
                                    ; a DITHERED ink - CDGRAY on 1bpp, which
                                    ; gfx_* composes per pixel from screen
                                    ; (x + y) parity (SPEC.md 39.4) - would
                                    ; come out flat black on black felt and
                                    ; vanish (SPEC.md 43.11.3). So the sprite
                                    ; path is opted into here, with a solid
                                    ; ink, and sol_ghost16 opts out

    mov ax, [sol_ry]                ; --- rank ---
    add ax, 8
    cmp ax, [sol_cvis]
    ja .done
    mov bl, [sol_ccard]
    and bx, C_RANK
    mov al, [sol_rankch+bx]
    mov cx, [sol_cdx]
    add cx, [sol_rx]
    mov dx, [sol_cdy]
    add dx, [sol_ry]
    or al, al
    jnz .one
    mov al, '1'                     ; the ten is the only two-glyph rank
    call OSAPI_FONT_CHAR_XPARENT
    add cx, 8
    mov al, '0'
.one:
    call OSAPI_FONT_CHAR_XPARENT

    mov ah, 0                       ; --- corner pip, 8x8 ---
    call sol_facepip
    jc .done                        ; ...and nothing BELOW it fits either
    mov ah, 1                       ; --- centre pip, 16x16 ---
    call sol_facepip
.done:
    ret

; -----------------------------------------------------------------------------
; sol_facepip - one suit pip on the card face, if the visible band reaches it
; in:  AH = 0 the 8x8 corner pip, 1 the 16x16 centre one; [sol_ccard] the card,
;      [sol_cdx]/[sol_cdy] its top-left, [sol_cvis] how many rows of it show
; out: CF = 1 the band is too short - and it is too short for everything BELOW
;      this pip too, which is why the caller stops rather than skipping
; clobbers: nothing else
;
; The two pips differed in four things and shared the other eleven
; instructions: which card-relative offset pair they sit at, how big they are,
; which of sol_pipsel's two tables they come from, and that only the second can
; be the last thing on the card. The offsets are adjacent in .bss in draw order
; (sol_psx/sol_psy, then sol_plx/sol_ply), so ONE pointer selects the pair -
; and the size is the same AH that sol_pipsel already wanted.
; -----------------------------------------------------------------------------
sol_facepip:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, sol_psx                 ; the 8x8 corner pip's offsets...
    mov cx, 8
    or ah, ah
    jz .have
    mov si, sol_plx                 ; ...or the 16x16 centre one's
    mov cx, 16
.have:                              ; AH IS STILL LIVE from here to sol_pipsel -
                                    ; it is the size, and the arithmetic below
                                    ; must not touch it. `mov ax, bx` here drew
                                    ; the 8x8 table 16 rows tall, off the end
                                    ; of it and into the next suit, which is a
                                    ; picture rather than a crash
    mov bx, [si+2]                  ; does the visible band reach the BOTTOM of
    mov dx, bx                      ; it? A pip half off the card is not drawn
    add dx, cx                      ; half, it is not drawn
    cmp dx, [sol_cvis]
    ja .short
    mov dx, [si]
    add bx, [sol_cdy]               ; BX = absolute y
    add dx, [sol_cdx]               ; DX = absolute x
    mov al, [sol_ccard]
    call sol_suit                   ; AL = the suit; AH is still the size
    call sol_pipsel                 ; SI = the mask rows
    mov ax, dx                      ; ...and sol_maskrun wants AX/BX = x/y,
    mov dx, cx                      ; CX = rows, DX = width
    call sol_maskrun
    clc
    jmp .out
.short:
    stc
.out:
    pop si
    jmp sol_pop4

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
sol_pipsel:
    cmp byte [sol_bpp], 4
    je sol_pipsold
    test al, 3                      ; a black suit (PF even, sol_suit)
    jpe sol_pipsold
    mov byte [sol_mrh], 1           ; ...and THAT is the whole of hollow now:
                                    ; the same solid mask, outlined by
                                    ; sol_maskrun as it stages it. One-shot -
                                    ; sol_maskrun clears it - so a caller that
                                    ; never sets it draws solid, which is what
                                    ; sol_pipsold and the ring rely on
sol_pipsold:
    push ax
    push cx
    mov si, sol_p16s                ; 16 rows of words: 32 bytes a suit...
    mov cl, 5
    or ah, ah
    jnz .have
    mov si, sol_p8s                 ; ...8 rows of BYTES: 8 a suit
    mov cl, 3
.have:
    mov ah, 0
    shl ax, cl
    add si, ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_pop6 / sol_pop4 - the shared epilogue ladder
;
; Entered by a near JMP and never a call, which is what makes it free: the
; frame is the caller's and nothing is added to it (docs/plans/completed/STACK-SLOTS-PLAN.md
; found the same for the kernel's own pass). `pop` does not touch the flags,
; so a routine returning CF returns it through here unchanged.
;
; 22 sites, 4 or 2 bytes each, against 7 bytes once.
; -----------------------------------------------------------------------------
sol_pop6:
    pop di
    pop si
sol_pop4:
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; SPEC.md 25.6's sprite stage is 16 rows of one 16px word. A record taller or
; wider than that is REFUSED, not clipped, so this is the kernel's number
; (icons.inc ICO_STAGE_H) rather than a choice of ours.
SOL_ICROWS  equ 16

; -----------------------------------------------------------------------------
; sol_maskrun - plot a 1-bit mask transparently
; in:  AX = x, BX = y (absolute), SI = the rows, CX = row count, DX = width in
;      pixels: 16 is a WORD a row, 8 a BYTE a row (sol_p8s), bit 7/15 leftmost;
;      [sol_pipink] the ink, [sol_mrh] = 1 to draw the mask's OUTLINE
; out: nothing; preserves all registers
;
; Transparent is the point: a pip goes on a card face that is already drawn
; and a ghost pip goes on felt. gfx_blit4 would be one far call instead of a
; dozen, but it is opaque and cannot carry "leave this pixel alone".
;
; HOLLOW IS DRAWN, NOT STORED (SPEC.md 43.11): the outline of a mask is
;
;     outline(r) = r AND NOT (up AND r AND dn AND (r << 1) AND (r >> 1))
;
; - the row less the pixels whose four neighbours are all set, the pixels
; strictly INSIDE the shape. A row above or below the mask reads as zero,
; which makes the first and last rows entirely their own outline: a shape's
; top row is all boundary. It is computed IN PLACE over the staged rows, top
; down, with the row above carried SOLID in BX - and the row below is still
; solid when it is read, being staged and not yet outlined. The row below the
; last is the zero the stage writes after it.
; -----------------------------------------------------------------------------
sol_maskrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [sol_mrx], ax
    mov [sol_mry], bx
    mov [sol_mrw], dx
    mov [sol_mrr], cx

    ; --- stage the shape into the sprite record (SPEC.md 43.11.3) ----------
    ; The DATA half must be clear: ico_core lays the mask down in the pen's
    ; first colour and then draws the data rows OVER it, ungated by the mask,
    ; so a stray data bit would be drawn. So the rows go in as words and
    ; ZEROS run to the end of the buffer - mask and data, at any row count,
    ; under one rule.
    mov ah, cl                      ; the rows the caller asked for...
    mov al, 1                       ; ...one 16px word each
    mov [sol_icrec], ax
    push es
    push ds
    pop es
    mov di, sol_icmask
.copy:
    lodsw
    cmp dl, 8
    jne .word
    dec si                          ; an 8-wide row is one byte, the LEFT
    mov ah, al                      ; half of the word it stages as
    mov al, 0
.word:
    stosw
    loop .copy
    mov cx, SOL_ICROWS * 2
    sub cx, [sol_mrr]
    xor ax, ax
    rep stosw
    pop es

    cmp [sol_mrh], al               ; hollow? (AL is 0)
    je .staged
    mov [sol_mrh], al               ; ONE-SHOT (see sol_pipsel)
    mov si, sol_icmask
    mov cx, [sol_mrr]
    xor bx, bx                      ; BX = the row above, solid: none yet
.ol:
    mov dx, [si]                    ; r
    mov ax, [si+2]                  ; dn
    and ax, bx                      ; ...and up
    and ax, dx
    mov bx, dx
    shl bx, 1
    and ax, bx                      ; ...and the two sideways neighbours
    mov bx, dx
    shr bx, 1
    and ax, bx                      ; AX = the inside
    not ax
    and ax, dx
    mov [si], ax                    ; the outline goes in...
    mov bx, dx                      ; ...and the SOLID row is the next one's up
    inc si
    inc si
    loop .ol
.staged:

    ; --- ONE drawing call, when the whole shape may be drawn --------------
    ; ico_core clips a sprite WHOLE - one shape or none, the font_char rule -
    ; so a pip a damage rect cuts would vanish entirely. That is what the
    ; walk below is still here for, and it is os88ui_glyph's own pair.
    mov ax, [sol_mrx]
    mov bx, [sol_mry]
    mov cx, ax
    add cx, [sol_mrw]
    dec cx
    mov dx, bx
    add dx, [sol_mrr]
    dec dx
    cmp byte [sol_pipink], SOL_NOPEN ; an ink the pen cannot lay down flat
    je .walk
    call OSAPI_WM_CLIP_TEST         ; CF = 0 also when no clip is armed
    jc .walk
    mov al, [sol_pipink]            ; mask colour; the data rows are clear, so
    mov ah, al                      ; the second half of the pen never applies
    call OSAPI_ICON_PEN
    mov cx, [sol_mrx]
    mov dx, [sol_mry]
    mov si, sol_icrec
    call OSAPI_ICON_DRAW
    jnc .out                        ; ...and CF = 1 is a record it would not
                                    ; stage, which the walk still draws

    ; --- one OSAPI_GFX_HLINE per run of set bits, the original -------------
    ; SI walks the STAGED rows (already outlined), DI counts them, DX is the
    ; row being shifted out a pixel at a time and BP is the column the open
    ; run started at, -1 for none.
.walk:
    mov si, sol_icmask
    mov di, [sol_mrr]
.row:
    lodsw
    xchg ax, dx
    mov bp, -1
    xor cx, cx
.bit:
    shl dx, 1                       ; CF = the pixel at column CX
    jnc .clear
    cmp bp, -1
    jne .next
    mov bp, cx                      ; a run opens here
    jmp .next
.clear:
    cmp bp, -1
    je .next
    call .emit
.next:
    inc cx
    cmp cx, [sol_mrw]
    jb .bit
    cmp bp, -1
    je .rowdone
    call .emit                      ; a run that reached the right edge
.rowdone:
    inc word [sol_mry]
    dec di
    jnz .row
.out:
    pop bp
    pop di
    pop si
    jmp sol_pop4

.emit:                              ; the open run is [BP .. CX-1]
    push bx
    push dx
    mov ax, bp
    add ax, [sol_mrx]
    mov bx, cx
    add bx, [sol_mrx]
    dec bx
    mov dx, [sol_mry]
    call OSAPI_GFX_HLINE            ; AX = x1, BX = x2, DX = y
    pop dx
    pop bx
    mov bp, -1
    ret

; -----------------------------------------------------------------------------
; sol_mkback - render the card back into a packed 4bpp image, once
; in:  the metrics; called from the entry proc (no lock held, nothing drawn)
;      and from sol_onresize, which may not draw either - this writes RAM only
; out: [sol_backbuf] filled, [sol_stride] set, CF = 0; preserves all
;      registers. sol_backbuf STARTS ON sol_entry (SPEC.md 43.11.6), so this
;      must never be called before the entry proc is done with itself
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
;
; EVERY PIXEL IS A FUNCTION OF ITS DISTANCE TO THE NEAREST EDGE, d =
; min(x, y, w-1-x, h-1-y): 0 is the black edge, 1 the white margin, 2 the
; field's flat outer ring, and from 3 in the lattice. So the image is written
; once, in order, two pixels a byte - where it used to be a white fill, a
; field pass and an edge pass through a per-pixel `mul` that read the byte
; back each time. The picture is the same to the bit (SPEC.md 43.11.5).
; -----------------------------------------------------------------------------
sol_mkback:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [sol_cw]
    shr ax, 1
    mov [sol_stride], ax
    mov dl, CBLUE * 0x11            ; DL = the field, in BOTH nibbles, so a
    cmp byte [sol_bpp], 4           ; pixel is a byte masked to its half
    je .have
    mov dl, CLBLUE * 0x11           ; 1bpp: a colour in the dither class
.have:
    mov di, sol_backbuf
    xor bx, bx                      ; BX = y
.row:
    xor cx, cx                      ; CX = x
.pair:
    call .pix                       ; the EVEN pixel is the high nibble...
    and ah, 0xF0
    mov dh, ah
    inc cx
    call .pix                       ; ...and the odd one the low
    inc cx
    and ah, 0x0F
    or ah, dh
    mov [di], ah
    inc di
    cmp cx, [sol_cw]                ; both widths are even
    jb .pair
    inc bx
    cmp bx, [sol_ch]
    jb .row
    clc                             ; CF = 0 OUT: sol_entry tail-jumps here,
    jmp sol_pop6                    ; so this is the loader's "go"

.pix:                               ; AH = pixel (CX, BX) in both nibbles
    mov ax, [sol_cw]                ; d = min(x, w-1-x, ...
    dec ax
    sub ax, cx
    cmp ax, cx
    jb .dx
    mov ax, cx
.dx:
    mov si, [sol_ch]                ; ...y, h-1-y), all far under 256 - so
    dec si                          ; AH is 0 when this is done, which is
    sub si, bx                      ; the black edge's colour
    cmp si, bx
    jb .dy
    mov si, bx
.dy:
    cmp ax, si
    jb .d
    mov ax, si
.d:
    cmp al, 1
    jb .out                         ; d = 0: the black edge
    mov ah, CWHITE * 0x11
    je .out                         ; d = 1: the white margin
    mov ah, dl
    cmp al, 2
    je .out                         ; d = 2: the flat outermost ring of the
    mov al, cl                      ; field, so the lattice never touches
    add al, bl                      ; the margin
    test al, 7                      ; the lattice: two families of diagonals
    jz .lat
    mov al, cl
    sub al, bl                      ; (only the low three bits matter, so a
    test al, 7                      ; negative x-y needs no bias)
    jnz .out
.lat:
    mov ah, CWHITE * 0x11
.out:
    ret

; sol_about - the About handler (API 0x018A, SPEC.md 12.2/43)
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
%ifdef SOLF_ABOUT
sol_about:
    mov byte [sol_abon], 1
    call sol_track
    jmp sol_drawall

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
%endif   ; SOLF_ABOUT - with no card (APP_SMALL) the click and key paths
                ; do not ask sol_abdismiss at all

; -----------------------------------------------------------------------------
; sol_panel - a white card with a black frame, centred in the content, and
;             lines of text centred on the card: the win plaque and the About
;             card, which were two routines drawing the same thing
; in:  SI = a 0-terminated list of line pointers; CL = the first baseline,
;      rows below the card's top; CH = the card's height beyond SOL_ABLH a
;      line; the origin tracked; gfx lock held
; out: nothing; preserves all registers
;
; Measured, never pinned: the widest line decides the width and the line
; count decides the height, so the card is right on all three adapters and
; nothing has to be re-measured when a line changes. Both are clamped to the
; content, because CGA's 640x200 gives this window 220px of width and about
; 137 of height (SPEC.md 39). Every line is centred on the CARD rather than on
; the content, so the block still reads as one card when the card has been
; clamped narrower than the text wanted - and for an unclamped card the two
; centres are the same pixel, which is why the plaque, which used to centre on
; the content, draws exactly as it did (SPEC.md 43.11.5).
;
; The shadow is forgotten on the way out: a card lands on the felt BETWEEN
; the piles and over their tops, so a column it covered cannot be KEPT by the
; next draw (SPEC.md 43.10).
; -----------------------------------------------------------------------------
sol_panel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push cx                         ; the two margins, for later
    push si                         ; ...and the list, for the second pass
    xor bx, bx                      ; BX = the widest line
    xor di, di                      ; DI = lines * SOL_ABLH
.meas:
    lodsw
    or ax, ax
    jz .sized
    add di, SOL_ABLH
    push si
    mov si, ax
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    pop si
    cmp ax, bx
    jbe .meas
    xchg ax, bx
    jmp .meas
.sized:
    add bx, 24                      ; 12px of margin either side...
    mov ax, [sol_cwid]
    cmp bx, ax
    jbe .wok
    mov bx, ax                      ; ...clamped to the content
.wok:
    mov [sol_pnw], bx
    sub ax, bx
    shr ax, 1
    mov [sol_pnl], ax               ; AX = the card's left
    pop si
    pop cx
    mov dl, ch
    mov dh, 0
    add di, dx                      ; DI = its height...
    mov dx, [sol_chgt]
    cmp di, dx
    jbe .hok
    mov di, dx                      ; ...clamped
.hok:
    sub dx, di
    shr dx, 1                       ; DX = its top
    push dx
    push cx
    mov bx, dx                      ; the card as (x1,y1)-(x2,y2), inclusive
    add dx, di
    dec dx
    mov cx, ax
    add cx, [sol_pnw]
    dec cx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call sol_fillc
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call sol_framec
    pop cx
    pop di
    mov ch, 0
    add di, cx                      ; DI = the first baseline
.line:
    lodsw
    or ax, ax
    jz .done
    push si
    mov si, ax
    call OSAPI_FONT_WIDTH           ; AX = this line's width
    mov cx, [sol_pnw]
    sub cx, ax
    shr cx, 1
    add cx, [sol_pnl]
    add cx, [sol_ox]                ; font_str wants SCREEN coords
    mov dx, di
    add dx, [sol_oy]
    call OSAPI_FONT_STR_XPARENT
    pop si
    add di, SOL_ABLH
    jmp .line
.done:
    call sol_pinv
    jmp sol_pop6

; -----------------------------------------------------------------------------
; sol_fillc / sol_framec - a fill and a 1px outline in CONTENT coordinates
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (content-relative, inclusive)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
; These two differ in ONE instruction - which kernel entry the rect ends at -
; so they share the translation and part company at the call.
sol_fillc:
    push ax
    push bx
    push cx
    push dx
    call sol_toabs
    call OSAPI_GFX_FILL
    jmp sol_pop4

sol_framec:
    push ax
    push bx
    push cx
    push dx
    call sol_toabs
    call OSAPI_GFX_FRAME
    jmp sol_pop4

; sol_toabs - AX/BX/CX/DX, a content-relative rect, to absolute screen coords
sol_toabs:
    add ax, [sol_ox]
    add cx, [sol_ox]
    add bx, [sol_oy]
    add dx, [sol_oy]
sol_ret:                            ; OS88_REGION_MOVABLE's relocation proc:
    ret                             ; a lone `ret`, so nothing may go above it

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
    mov si, [sol_fand]              ; (through SI, so AL stays the column)
    mov [sol_cfa], si
    mov si, [sol_fanu]
    mov [sol_cfu], si
    call sol_count
    mov ch, 0
    cmp cx, 2
    jb .out                         ; nought or one card: nothing can overflow

    call sol_pilebase               ; count the face-down cards, which are
    xor dx, dx                      ; always the bottom of the column
.down:
    test byte [bx], C_FACEUP
    jnz .haved
    inc bx
    inc dx
    cmp dx, cx
    jb .down
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
    jmp sol_pop4

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
; sol_pileend  - ...and one past its top card, with the count
; in:  AL = pile index
; out: BX = the offset of card 0 (sol_pileend: of card [count]), and
;      sol_pileend also CX = the count; preserves all other registers
;
; A table rather than a multiply, because the piles are no longer one depth
; (CAP_* above): 13 bytes of it, and no `mul` on a path every card drawn takes.
; -----------------------------------------------------------------------------
sol_pileend:
    call sol_count
    mov ch, 0
    call sol_pilebase
    add bx, cx
    ret

sol_pilebase:
    mov bl, al
    mov bh, 0
    mov bl, [sol_pofs+bx]
    add bx, sol_pile
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
    call sol_pileend
    mov al, [bx-1]
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
    push bx
    push cx
    call sol_pileend
    mov [bx], ah
    mov bl, al
    mov bh, 0
    inc byte [sol_pcnt+bx]
    pop cx
    pop bx
    ret

sol_pop:
    push bx
    push cx
    call sol_pileend
    mov ah, [bx-1]
    mov bl, al
    mov bh, 0
    dec byte [sol_pcnt+bx]
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_suit - decode a card: in AL = card; out AL = the suit, 0..3
;
; RED IS A PARITY. The suits are spade 00, heart 01, diamond 10, club 11, so a
; suit is red exactly when its two bits DIFFER - and two cards are the same
; colour exactly when the XOR of their suit bits has an EVEN number of ones.
; `test reg, 0x30` and the 8086's own parity flag answer both in one
; instruction (sol_onto, sol_drawface, sol_pipsel), which is why there is no
; "is it red" routine.
; -----------------------------------------------------------------------------
sol_suit:
    push cx
    mov cl, 4
    shr al, cl
    and al, 3
    pop cx
    ret

; -----------------------------------------------------------------------------
; sol_onto - may card AH be played onto card AL in the tableau?
; in:  AL = the card underneath, AH = the card going on top of it
; out: CF = 0 yes - AL face up, one rank above AH, and the other colour;
;      CF = 1 no. Preserves all registers.
;
; THE ONE TABLEAU RULE, and it had two copies: the drop (sol_cantab) and the
; lift, which checks a run is already a legal sequence before it goes up
; (sol_grab). Every card in a lifted run is face up - face-up cards are always
; the top of a column - so asking the drop's question of each pair is exact.
; -----------------------------------------------------------------------------
sol_onto:
    push ax
    test al, C_FACEUP
    jz .no
    push ax
    and ax, C_RANK * 0x0101
    inc ah
    cmp al, ah                      ; it must descend by one...
    pop ax
    jne .no
    xor al, ah
    test al, 0x30                   ; ...and alternate colour: an ODD number
    jpo .yes                        ; of differing suit bits (see sol_suit)
.no:
    stc
.yes:                               ; (jpo is taken with CF = 0 from `test`)
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
    mov bl, al                      ; BL = the card
    call sol_suit
    cmp al, ah
    jne .no                         ; a foundation takes one suit and no other,
    add al, P_FND0                  ; and holds it A, 2, 3... in order - so
    call sol_count                  ; its COUNT is the rank it takes next, and
    and bl, C_RANK                  ; nothing about the card on top needs
    cmp bl, cl                      ; reading: equal leaves CF = 0
    je .out
.no:
    stc
.out:
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
    push cx
    xchg al, ah                     ; AL = the column, AH = the card
    call sol_count
    or cl, cl
    jz .empty
    call sol_topcard                ; AL = the card underneath, AH still ours
    call sol_onto
    jmp .out
.empty:
    and ah, C_RANK                  ; an empty column takes a king, and only
    cmp ah, RANK_K                  ; a king: equal leaves CF = 0
    je .out
    stc
.out:
    pop cx
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
    mov dx, ax                      ; DL = the source, DH = the destination
    mov ah, cl                      ; AH = N
    call sol_pileend                ; the run starts N below the source's top
    mov cl, ah                      ; (CH is 0 out of sol_pileend)
    sub bx, cx
    mov si, bx                      ; SI = the first card to move
    mov al, dh
    call sol_pileend
    mov di, bx                      ; DI = where it lands
    mov cl, ah
    push es
    push ds
    pop es
    rep movsb
    pop es

    mov bl, dl                      ; both counts follow
    mov bh, 0
    sub [sol_pcnt+bx], ah
    mov bl, dh
    add [sol_pcnt+bx], ah

    mov al, dl                      ; a tableau card that has just been
    cmp al, P_TABN                  ; uncovered turns over
    jae .notab
    call sol_pileend
    jcxz .done
    or byte [bx-1], C_FACEUP
    jmp .done
.notab:
    cmp al, P_WASTE                 ; a card taken off the waste shrinks its
    jne .done                       ; fan, so the one under shows through
    cmp byte [sol_wfan], 2
    jb .done
    dec byte [sol_wfan]
.done:
    jmp sol_pop6

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
    push bx
    mov bl, [sol_won]               ; the win state going in
    call sol_move
    call sol_checkwin
    or bl, [sol_won]
    jnz .all
    call sol_drawpile               ; the source...
    xchg al, ah
    call sol_drawpile               ; ...and the destination
    xchg al, ah
    pop bx
    ret
.all:
    call sol_drawall                ; the plaque needs the whole content
    pop bx
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
    mov al, [sol_wfan]              ; never above 3: sol_stock deals 1 or 3,
    or al, al                       ; and only sol_move takes it down
    jnz .lo
    inc ax                          ; a fan of nothing still shows the top card
.lo:
    cmp al, cl                      ; ...and never more than there are, which
    jbe .out                        ; is also the empty waste's 0
    mov al, cl
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
    mov bx, dx                      ; swap cards DX and DI
    mov al, [sol_deck+bx]
    xchg al, [sol_deck+di]
    mov [sol_deck+bx], al
    dec di
    jnz .shuf
    pop di                          ; ...and falls into sol_deal, which is
    pop dx                          ; what the deal half of a new game IS
    pop cx
    pop bx
    pop ax

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
    mov word [sol_wfan], 0          ; no fan, and not won (OS88_ADJACENT)

    mov si, sol_deck                ; SI walks the deck
    xor cl, cl                      ; CL = column
.col:
    xor ch, ch                      ; CH = the card within it
.card:
    lodsb
    cmp ch, cl
    jne .down
    or al, C_FACEUP                 ; the last card of each column shows
.down:
    mov ah, al
    mov al, cl
    call sol_push
    inc ch
    cmp ch, cl
    jbe .card
    inc cl
    cmp cl, 7
    jb .col
.stock:
    lodsb
    mov ah, al
    mov al, P_STOCK
    call sol_push
    cmp si, sol_deck + 52
    jb .stock
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sol_stock - the stock was clicked: deal, or turn the waste back over
; in:  nothing; out: CF = 1 when the stock's PICTURE changed - it emptied, or
;      a recycle refilled it - and CF = 0 when it shows what it showed.
;      Preserves all registers. Draws nothing.
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
    mov dl, [sol_draw3]             ; 1 or 3 at a time...
    shl dl, 1
    inc dl
    cmp dl, cl
    jbe .n
    mov dl, cl                      ; ...or what is left
.n:
    mov [sol_wfan], dl              ; ...which is the waste's new fan
.one:
    call sol_pop                    ; AH = the card (AL = the stock)
    or ah, C_FACEUP
    mov al, P_WASTE
    call sol_push
    mov al, P_STOCK
    dec dl
    jnz .one
    call sol_count                  ; the picture flipped if that emptied it
    cmp cl, 1
    jmp .out
.recycle:
    mov al, P_WASTE
    call sol_count
    mov ch, 0
    clc
    jcxz .out                       ; nothing to turn back: no change
.back:
    call sol_pop                    ; AL = the waste, which sol_pop keeps
    and ah, C_NOTUP
    mov al, P_STOCK
    call sol_push
    mov al, P_WASTE
    loop .back
    mov [sol_wfan], cl              ; CX is 0 here
    stc                             ; the ring has become a card back
.out:
    jmp sol_pop4

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
    mov ax, [sol_pcnt + P_FND0]     ; a foundation holds at most its 13, so
    add al, ah                      ; all 52 between the four is all four full
    add al, [sol_pcnt + P_FND0 + 2]
    add al, [sol_pcnt + P_FND0 + 3]
    cmp al, 52
    mov al, 0
    jne .set
    inc ax
.set:
    mov [sol_won], al
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
    mov bl, al                      ; BL = the source pile
    call sol_count
    or cl, cl
    jz .no
    call sol_topcard
    test al, C_FACEUP
    jz .no
    mov dl, al                      ; DL = the card...
    call sol_suit                   ; ...and the ONE foundation that could take
    mov ah, al                      ; it, its own suit's: sol_canfnd refuses
    mov dh, al                      ; every other, so asking all four was three
    mov al, dl                      ; refusals and an answer
    call sol_canfnd
    jc .no
    mov al, bl
    mov ah, dh
    add ah, P_FND0
    mov cl, 1
    call sol_domove
    clc
    jmp sol_pop4
.no:
    stc
    jmp sol_pop4

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
    call sol_track
%ifdef SOLF_ABOUT
    call sol_abdismiss              ; a click anywhere takes the credits down
    jc .out                         ; and does nothing else
%endif
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
    jmp sol_pop6

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
    mov ax, dx                      ; topy <= y < topy + ch, as sol_inx asks
    sub ax, [sol_topy]              ; its own question: one unsigned compare
    cmp ax, [sol_ch]
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
    dec al
    js .none
    mul byte [sol_wstep]            ; the top card's extra offset
    mov si, ax
    mov al, P_WASTE
    call sol_pilexy                 ; CX = x
    add cx, si
    neg cx                          ; ...and the same unsigned compare
    add cx, [sol_htx]
    cmp cx, [sol_cw]
    jae .none
.fndhit:                            ; AL = the pile: its TOP card is the grab,
    call sol_count                  ; and an empty one is not a grab at all
    dec cl
    js .none
    mov ah, cl
    jmp .hit
.stock:
    xor ah, ah                      ; (sol_inx kept AL = P_STOCK)
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
    mov dl, al                      ; DL = the column (DX is ours: saved)
    call sol_count
    call sol_colfan
    dec cl                          ; CL = index, walked from the top down -
    js .none                        ; and an empty column has none
.tcard:
    mov al, cl
    call sol_yoff
    add ax, [sol_taby]
    neg ax                          ; this card's top <= y < top + ch
    add ax, [sol_hty]
    cmp ax, [sol_ch]
    jae .tdown
    mov al, dl
    mov ah, cl
    jmp .hit
.tdown:
    dec cl
    jns .tcard
.none:
    stc
    jmp short .out
.hit:
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sol_inx - is [sol_htx] inside this pile's card-wide column?
; in:  AL = pile index
; out: CF = 0 inside; preserves all registers
; -----------------------------------------------------------------------------
sol_inx:
    push cx
    push dx
    call sol_pilexy                 ; CX = x
    neg cx                          ; inside is x <= htx < x + cw, which is
    add cx, [sol_htx]               ; ONE unsigned compare of htx - x: a
    cmp cx, [sol_cw]                ; point left of x wraps to a huge number
    cmc                             ; CF = 0 inside
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; sol_grab - decide what a press picks up, and arm the drag state
; in:  AL = pile, AH = card index; gfx lock held, origin tracked
; out: CF = 0 the drag state is armed; CF = 1 there is nothing to drag
; clobbers: nothing (every register preserved)
;
; A tableau run may only be lifted whole and only if it is ALREADY a legal
; sequence - the same rule the drop applies, checked before the outline goes
; up rather than after it comes down. A face-down card is not a grab at all.
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
    call sol_count                  ; CL = the pile's cards
    mov ch, 0
    cmp al, P_TABN
    jae .single

    ; --- a tableau column ----------------------------------------------------
    mov dl, ah
    mov dh, 0
    call sol_pilebase
    add bx, dx                      ; BX -> the card that was pressed
    sub cl, dl                      ; CL = it and the cards on top of it
    test byte [bx], C_FACEUP        ; a face-down card is never a grab - and
    jz .no                          ; never the top one: sol_move turns over
                                    ; the card it uncovers, so a press-to-turn
                                    ; here had no card it could ever reach
    mov [sol_dnum], cl              ; cards in the hand
    dec cx                          ; CX = adjacent pairs to check
    jcxz .tabok
.seq:
    mov ax, [bx]                    ; AL = a card, AH = the one on it
    call sol_onto                   ; the drop's own rule, pair by pair
    jc .no
    inc bx
    loop .seq
.tabok:
    mov al, [sol_dsrc]
    call sol_colfan                 ; the hand is fanned as its column is
    mov dx, [sol_cfu]
    mov al, [sol_didx]
    call sol_yoff                   ; AX = the hand's offset in its column
    jmp .rect

    ; --- the waste or a foundation: the top card, alone ----------------------
.single:
    dec cl                          ; (an empty pile wraps to 255: no index)
    cmp cl, ah
    jne .no                         ; only the card that is really on top
    mov byte [sol_dnum], 1
    mov dx, [sol_ch]
    xor ax, ax

.rect:                              ; AX = y offset in the pile, DX = the fan
    mov [sol_dfan], dx
    mov si, ax
    mov al, [sol_dsrc]
    call sol_pilexy                 ; CX = x, DX = y (content-relative)
    add dx, si
    cmp al, P_WASTE
    jne .norm
    call sol_wshow                  ; the waste's top card sits fanned right
    dec ax                          ; (a BYTE multiply: it leaves DX alone)
    mul byte [sol_wstep]
    add cx, ax
.norm:
    add cx, [sol_ox]
    mov [sol_dox], cx
    add dx, [sol_oy]
    mov [sol_doy], dx
    mov al, [sol_dnum]              ; height = the fan plus one whole card
    mov ah, 0
    dec ax
    mul word [sol_dfan]
    add ax, [sol_ch]
    mov [sol_dh], ax
    clc
    jmp sol_pop6
.no:
    stc
    jmp sol_pop6

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
    jmp sol_pop4

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

%ifdef SOLF_AUTO
sol_pace:
    call OSAPI_GFX_UNLOCK
    call sol_linger
    call OSAPI_GFX_LOCK
    ret
%endif   ; SOLF_AUTO - the pause exists so the auto-finish can be WATCHED

; -----------------------------------------------------------------------------
; sol_dxor - draw (or erase - XOR is its own inverse) the hand's outline
; in:  [sol_dox]/[sol_doy]/[sol_dh]/[sol_dnum]/[sol_dfan]
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
    push di
    mov al, [sol_dnum]              ; DI = the rules: one per card boundary
    cbw
    dec ax
    xchg ax, di
    mov ax, [sol_dox]
    mov bx, [sol_doy]
    mov cx, ax
    add cx, [sol_cw]                ; a hand is one card wide
    dec cx
    mov dx, bx
    add dx, [sol_dh]
    dec dx
    call OSAPI_GFX_XOR_RECT
    test di, di
    jz .out
.rule:                              ; AX and CX are the rule's ends already:
    add bx, [sol_dfan]              ; each is one row, a fan further down
    mov dx, bx
    call OSAPI_GFX_XOR_FILL         ; AX=x1, BX=y1, CX=x2, DX=y2: one row
    dec di
    jnz .rule
.out:
    pop di
    jmp sol_pop4

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
    mov ax, [sol_dox]               ; did it move at all? |d| <= 3 is d + 3 in
    sub ax, [sol_dsx]               ; 0..6, which is one unsigned compare
    add ax, 3
    cmp ax, 6
    ja .moved
    mov ax, [sol_doy]
    sub ax, [sol_dsy]
    add ax, 3
    cmp ax, 6
    ja .moved
    cmp byte [sol_dnum], 1          ; a press and release in place sends the
    jne .out                        ; one card home, if it will go
    mov al, [sol_dsrc]
    call sol_tofnd
    jmp .out

.moved:
    mov ax, [sol_cw]                ; the hand's top card, centred - and which
    shr ax, 1                       ; column is that over?
    add ax, [sol_dox]
    sub ax, [sol_ox]
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
    xchg ax, bx                     ; BL = the column
    mov ax, [sol_ch]                ; ...and is the centre in the top row?
    shr ax, 1
    add ax, [sol_doy]
    sub ax, [sol_oy]
    cmp ax, [sol_taby]
    mov al, [sol_dsrc]              ; (a `mov` leaves the flags alone)
    jae .totab

    sub bl, 3                       ; the top row: only a foundation takes a
    jb .out                         ; drop, and only ever one card
    cmp byte [sol_dnum], 1
    jne .out
    call sol_topcard                ; AL = the card being dropped
    mov ah, bl                      ; BL = the foundation, 0..3
    call sol_canfnd
    jc .out
    add bl, P_FND0
    mov cl, 1
    jmp .go

.totab:
    cmp bl, al
    je .out                         ; back onto its own column: nothing
    call sol_handcard               ; AL = the BOTTOM card of the hand
    mov ah, bl                      ; BL = the destination column
    call sol_cantab
    jc .out
    mov cl, [sol_dnum]
.go:
    mov ah, bl
    mov al, [sol_dsrc]
    call sol_domove
.out:
    jmp sol_pop4

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
    mov bl, al                      ; keep the key; sol_track needs AX
    call sol_track
%ifdef SOLF_ABOUT
    call sol_abdismiss              ; any key takes the credits down, and is
    jc .out                         ; spent doing it
%endif
    cmp bl, ' '
    je .deal
    or bl, 0x20                     ; fold the case - AFTER the space test,
    mov bh, 0                       ; which an extended key's 0 would pass;
    cmp bl, 'n'                     ; BH = the Game menu's item for the key
    je .cmd
    inc bh
    cmp bl, 'r'
    je .cmd
%ifdef SOLF_AUTO
    inc bh
    cmp bl, 'a'
    je .cmd
%endif
    jmp .out
.cmd:
    mov bl, bh
    call sol_gamecmd
    jmp .out
.deal:
    call sol_cmd_deal
.out:
    jmp sol_pop6

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
    mov bl, al                      ; the item; sol_track needs AX
    mov bh, ah                      ; the menu
    call sol_track
    or bh, bh
    jnz .deal
    call sol_gamecmd
    jmp .out
.deal:
    mov [sol_draw3], bl             ; Draw One is item 0, Draw Three item 1
    mov bh, 0                       ; ...and MENU_DIS (SPEC.md 12.2) greys the
    lea ax, [bx + sol_s_d1x]        ; one in force: an item string that begins
    mov [sol_mi_deal], ax           ; with that byte is drawn grey and never
    mov ax, sol_s_d3                ; selected, and each plain label is its
    sub ax, bx                      ; disabled twin one byte on - so the pair
    mov [sol_mi_deal+2], ax         ; is two pointers a byte apart, a radio
.out:                               ; pair with no kernel support at all
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; sol_gamecmd - the Game menu's commands, shared by the keys and the menu
; in:  BL = the item: 0 New Game, 1 Restart Deal, 2 Auto Finish; the origin
;      already tracked; gfx lock held
; out: nothing; preserves all registers but BL
; -----------------------------------------------------------------------------
sol_gamecmd:
    dec bl
    js .new
%ifdef SOLF_AUTO
    jnz sol_cmd_auto                ; (it is the next routine down)
%endif
    call sol_deal                   ; Restart Deal: the same deck again
    jmp sol_drawall
.new:
    call sol_newgame
    jmp sol_drawall

; Auto Finish: send everything that will go, a move a tick so it reads as
; play rather than as a repaint. The lock is dropped inside sol_pace, which
; is what puts each move on the glass before the next one is computed.
%ifdef SOLF_AUTO
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

%endif   ; SOLF_AUTO

; The stock's PICTURE is one bit - a card back, or the turn-over-again ring -
; so it needs redrawing only when that bit flips: when the last card leaves it,
; and when a recycle refills it. Dealing from a stock that still has cards
; leaves exactly the picture that is already on the screen, and redrawing it
; is not cheap: the back is a lattice, so gfx_blit4 coalesces it into 634
; gfx_fill runs on the 32x44 metrics (336 on CGA's 28x28). That was being paid
; on every single click of the stock, which is the one thing a player does
; over and over. sol_stock says which it was.
sol_cmd_deal:
    push ax
    call sol_stock
    jnc .waste                      ; same picture: leave it alone
    mov al, P_STOCK
    call sol_drawpile
.waste:
    mov al, P_WASTE
    call sol_drawpile
    pop ax
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
    OS88_PREFER sol_pref, SOL_PW_B, SOL_PH_B,  SOL_PW_B, SOL_PH_B,  SOL_PW_S, SOL_PH_S

    OS88_MENUSET sol_menus, sol_m_name, sol_oncmd
%ifdef SOLF_AUTO
        OS88_MENU sol_m_game, sol_mi_game, 3
%else
        OS88_MENU sol_m_game, sol_mi_game, 2
%endif
        OS88_MENU sol_m_deal, sol_mi_deal, 2
    OS88_MENUSET_END sol_menus

sol_m_name:                         ; the name in the bar IS the window's
sol_ttl:     db 'Solitaire', 0          ; title: one string, two labels
sol_m_game:  db 'Game', 0
sol_mi_game: dw sol_s_new, sol_s_rest
%ifdef SOLF_AUTO
             dw sol_s_auto
%endif
sol_s_new:   db 'New Game', 0
sol_s_rest:  db 'Restart Deal', 0
%ifdef SOLF_AUTO
sol_s_auto:  db 'Auto Finish', 0
%endif
sol_m_deal:  db 'Deal', 0
sol_mi_deal: dw sol_s_d1x, sol_s_d3     ; draw-one is the zero state, so it
; The two forms of each item are the SAME text, one of them disabled - which
; is what MENU_DIS is (os88api.inc): a leading byte, and the kernel greys the
; rest. So the plain label IS the disabled one from its second byte on, and
; the pair costs one byte rather than a second copy of the string. 20 bytes
; over the four - and sol_oncmd now computes each pointer as its twin plus or
; minus the item, so the overlap IS load-bearing: the `times` lines below
; refuse to assemble if either pair stops being one byte apart.
sol_s_d1x:   db MENU_DIS
sol_s_d1:    db 'Draw One', 0           ; draw-one starts as the greyed-out one
sol_s_d3x:   db MENU_DIS
sol_s_d3:    db 'Draw Three', 0
    times 1 - (sol_s_d1 - sol_s_d1x) db 0   ; (a negative count is an error)
    times 1 - (sol_s_d3 - sol_s_d3x) db 0

sol_s_win:   db 'You win!', 0
sol_winlines: dw sol_s_win, 0           ; the plaque, as sol_panel's line list

; --- the About panel's credit (SPEC.md 43.8) -----------------------------------
; A NUL-terminated array of line pointers, measured rather than pinned
; (sol_panel): the widest line sets the width and the count sets the
; height. Keep every line inside 24 glyphs - CGA gives this window 220px of
; content, which is 27 glyphs less the panel's own margins (SPEC.md 39).
SOL_ABLH equ 11                     ; line pitch, px (8px glyphs + 3 of air)
%ifdef SOLF_ABOUT
sol_ablines:
    dw sol_ab1, sol_ab2, sol_ab3, sol_ab4, 0
sol_ab1:     db 'Solitaire for os8088', 0
sol_ab2:     db 'Klondike, in 8086 asm', 0
sol_ab3:     db 0                   ; a blank line is a line with no glyphs
sol_ab4:     db 'Contributed by Elendilon', 0
%endif   ; SOLF_ABOUT

; Rank glyphs, indexed by rank 0..12. Zero means "the ten", the one rank that
; needs two glyphs, and sol_drawface spells it out.
sol_rankch:
    db 'A', '2', '3', '4', '5', '6', '7', '8', '9', 0, 'J', 'Q', 'K'

; --- where each pile starts in sol_pile (sol_pilebase): the columns, the
; foundations, the stock and the waste, each at the depth its kind reaches
sol_pofs:
    db 0, CAP_TAB, CAP_TAB*2, CAP_TAB*3, CAP_TAB*4, CAP_TAB*5, CAP_TAB*6
    db CAP_TAB*7, CAP_TAB*7 + CAP_FND, CAP_TAB*7 + CAP_FND*2
    db CAP_TAB*7 + CAP_FND*3
    db CAP_TAB*7 + CAP_FND*4, CAP_TAB*7 + CAP_FND*4 + CAP_SW
%if SOL_PILES > 255
%error "sol_pofs is a byte table"
%endif

; --- metrics records (SOL_NMET bytes each, widened into bss by sol_metrics) ------
; card w, card h, gap, margin, top row y, face-down fan, face-up fan,
; rank x, rank y, corner pip x, corner pip y, centre pip x, centre pip y.
; The face-up fan must clear the rank glyph's 8 rows or a buried card cannot
; be read; both records do.
; BYTES, widened by sol_metrics' `cbw` - so every field must stay under 128.
sol_met_big:                        ; VGA 640x480 and Hercules 720x348
    db SOL_CW_B, SOL_CH_B, SOL_GAP_B, SOL_MARG_B, SOL_TOPY_B, SOL_FAND_B
    db SOL_FANU_B, 3, 3, 21, 3, 8, 22, SOL_TGAP_B
sol_met_sml:                        ; CGA 640x200, over the dock: 179 rows
    db SOL_CW_S, SOL_CH_S, SOL_GAP_S, SOL_MARG_S, SOL_TOPY_S, SOL_FAND_S
    db SOL_FANU_S, 2, 2, 18, 2, 6, 10, SOL_TGAP_S

; --- suit pips, 1 bit per pixel, the leftmost pixel the top bit -------------
; Solid for every suit on a colour screen; on 1bpp the two red suits are drawn
; HOLLOW, which is the only thing carrying red vs black there (SPEC.md 39.4).
; The 16x16 masks are a word a row and the 8x8 ones a BYTE a row: sol_maskrun
; stages an 8-wide row as the high half of a word, so one routine plots both
; sizes, and the left-justified words the 8x8 masks used to be stored as were
; 32 bytes of zeros (SPEC.md 43.11.5).
;
; THERE ARE NO HOLLOW TABLES. There were - sol_p8h and sol_p16h, 96 bytes -
; and every row of both was EXACTLY the morphological outline of the solid
; mask beside it:
;
;     outline(r) = r AND NOT (up AND r AND dn AND (r << 1) AND (r >> 1))
;
; which is what "hollow" means drawn rather than stored. sol_maskrun computes
; it now (SPEC.md 43.11), so the two shapes cannot drift apart: a suit redrawn
; here changes its own outline by construction, where before it left a hollow
; table describing the pip that used to be.
sol_p8s:                            ; spade, heart, diamond, club - BYTES,
    db 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x66, 0x18, 0x3C      ; bit 7 leftmost;
    db 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00      ; sol_maskrun
    db 0x18, 0x3C, 0x7E, 0xFF, 0x7E, 0x3C, 0x18, 0x00      ; stages each as
    db 0x18, 0x3C, 0x3C, 0xBD, 0xFF, 0xDB, 0x18, 0x3C      ; a word's high half

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

; The empty stock's mark: a ring, for "round again".
sol_ic_ring:
    dw 0x07E0, 0x1FF8, 0x381C, 0x700E, 0x6006, 0xE007, 0xC003, 0xC003
    dw 0xC003, 0xC003, 0xE007, 0x6006, 0x700E, 0x381C, 0x1FF8, 0x07E0

; -----------------------------------------------------------------------------
; sol_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
;
; **THIS ROUTINE IS THE CARD BACK'S FIRST BYTES, ONCE IT HAS RUN** (SPEC.md
; 43.11.6). It is the LAST thing in the image, sol_backbuf is its own label,
; and the back is rendered by a TAIL jump to sol_mkback as the very last thing
; it does - so the bytes it was are not needed again by the time they are
; overwritten, and its ~180 bytes are 180 of the back's 704 that the region
; does not have to carry twice. Nothing may follow it in the image and
; nothing may call into it: an instance runs its entry proc once, from a
; fresh read of the file (SPEC.md 20.2), and never again.
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
    mov [sol_bpp], dh
    push ax                         ; the width and the dock row are for the
    push cx                         ; placement below, and for nothing else

    call sol_metrics            ; the card metrics this screen wants, and
                                ; everything derived from them - re-run by
                                ; sol_onresize (SPEC.md 11.98)
    add ax, 2                       ; ...plus the 1px frame either side
    mov [sol_tpl + WT_W], ax

    pop dx                          ; DX = the dock's first row
    mov ax, SOL_PH_B                ; the tallest column the game can build -
    cmp byte [sol_cw], SOL_CW_B     ; 6 face-down and 13 face-up - as a frame
    je .tall                        ; height. It IS the published preference's
    mov ax, SOL_PH_S                ; number for the record sol_metrics took
.tall:                              ; (the two are one formula, at the top)
    mov bx, dx                      ; ...clamped to the desktop band, and ONE
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

    xchg ax, dx                     ; a third of the slack down...
    sub ax, MBAR_H
    sub ax, dx
    jns .yok
    xor ax, ax
.yok:
    xor dx, dx
    mov bx, 3
    div bx
    add ax, MBAR_H
    mov [sol_tpl + WT_Y], ax
    pop ax                          ; ...and centred across
    sub ax, [sol_tpl + WT_W]
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [sol_tpl + WT_X], ax

    call OSAPI_GET_TICKS            ; seeded ONCE: every later New Game walks
    call OSAPI_SRAND                ; on down the same stream, so two deals
                                    ; inside one tick still differ
    call sol_newgame

    mov si, sol_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .full
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE sol_ret     ; ...naming a bare `ret` of our own (16
                                    ; bytes against the bare form's 19): this
                                    ; package holds no segment word of its own
    mov ax, sol_onresize            ; the card metrics are picked from the
    call OSAPI_WM_ONRESIZE          ; SCREEN, so an adapter change invalidates
                                    ; them (SPEC.md 11.98)
    mov al, 1                       ; **HANG OVER THE DOCK RATHER THAN BE
    call OSAPI_WM_KEEPH             ; SHORTENED** (SPEC.md 11.93), on EVERY
                                    ; adapter and not just the small one. This
                                    ; layout is fixed and every row taken off
                                    ; the frame is one sol_colfan takes off the
                                    ; cards, so the dock's 24 rows are worth a
                                    ; whole fan step - measured, they take a
                                    ; 7-card build from 9px a card to the
                                    ; record's full 12 on a CGA, and stop the
                                    ; Hercules compressing its deepest column
                                    ; at all. VGA has the room already, so it
                                    ; is a no-op there.
                                    ;
                                    ; UNCONDITIONAL is not laziness: a version
                                    ; that asked the adapter set it from
                                    ; OSAPI_WM_DISPLAY, which answers the MORE
                                    ; RESTRICTIVE display for a straddling
                                    ; window (SPEC.md 39.16.4.1) while wm_fit
                                    ; clamps to the ORIGIN's - so a game half
                                    ; over the seam was told CGA, set the flag,
                                    ; and got the HERCULES ceiling: 317 rows on
                                    ; a card whose band is 303. The flag is not
                                    ; an adapter question at all, and asking
                                    ; one was the bug
    mov si, sol_pref                ; ...and the size this game wants on each
    call OSAPI_WM_PREFER            ; card (SPEC.md 11.100.1). sol_metrics has
                                    ; always had two records and picked the
                                    ; right one; what never followed was the
                                    ; FRAME
    mov cx, SOL_PW_S                ; the small card's own width, and a height
    mov dx, 155                     ; the game is still itself at (11.100.2)
    call OSAPI_WM_MINSIZE
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
%ifdef SOLF_ABOUT
    mov si, sol_about
    call OSAPI_ABOUT_SET            ; 'About Solitaire' under our name in the
                                    ; bar (SPEC.md 12.2) - same contract, and
                                    ; it preserves the flags for the same
                                    ; reason: the CF above is the loader's
%endif   ; SOLF_ABOUT
    pop di
    pop si
    jmp sol_mkback                  ; ...and LAST, the card back, over these
                                    ; very bytes: a tail jump, so nothing here
                                    ; runs after it starts writing. It keeps
                                    ; every register and answers CF = 0, which
                                    ; is the loader's "go" (BX is the window)
.full:
    pop di                          ; no window: CF = 1 rides out, and the
    pop si                          ; region is the loader's to free - the
    ret                             ; back is never needed

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

; THE CARD BACK COMES FIRST AND STARTS INSIDE THE IMAGE, on sol_entry (SPEC.md
; 43.11.6): the entry proc's bytes are dead once it has run, and it renders
; the back over them as its last act. So the back is the first 704 bytes from
; sol_entry, and every name below is an offset from where the back ENDS.
sol_backbuf equ sol_entry           ; the card back, packed 4bpp
sol_bssbase equ sol_backbuf + SOL_BACKMAX

%assign SOL_BSS 0
%macro SWORD 1
%1 equ sol_bssbase + SOL_BSS
%assign SOL_BSS SOL_BSS + 2
%endmacro
%macro SBYTE 1
%1 equ sol_bssbase + SOL_BSS
%assign SOL_BSS SOL_BSS + 1
%endmacro
%macro SBUF 2
%1 equ sol_bssbase + SOL_BSS
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
    SWORD sol_tgap                  ; foundation row to tableau: two of sol_gap
                                    ; on the big record and LESS on the small
                                    ; one, which is a decision about a 200-row
                                    ; screen rather than a derived number

; --- derived once, in sol_entry ----------------------------------------------
    SWORD sol_pitch                 ; card width + gap
    SWORD sol_taby                  ; content y of the tableau
    SWORD sol_wstep                 ; the waste's horizontal fan step
    SWORD sol_stride                ; bytes per row of the back image

; --- refreshed by sol_track, every callback ----------------------------------
    SWORD sol_ox                    ; content origin, screen coords
    SWORD sol_oy
    SWORD sol_cwid                  ; content width
    SWORD sol_chgt                  ; content height
    SWORD sol_avail                 ; deepest a column's last card may start

; --- the game ----------------------------------------------------------------
    SBUF  sol_pcnt, SOL_NPILE       ; cards in each pile
    SBUF  sol_pile, SOL_PILES       ; each pile at its own depth (sol_pofs)
    SBUF  sol_deck, 52              ; the shuffled deal, kept for Restart
    SBYTE sol_bpp                   ; 4 or 1 (SPEC.md 39.2)
    SBYTE sol_draw3                 ; 0 = deal one, 1 = deal three
    SBYTE sol_wfan                  ; waste cards to fan, 0..3
    SBYTE sol_won
    OS88_ADJACENT sol_wfan, sol_won, 1  ; sol_deal clears the two as one word
%ifdef SOLF_ABOUT
    SBYTE sol_abon                  ; 1 = the credit panel is up (43.8)
%endif

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
    SWORD sol_dpx                   ; the pile's slot origin, content coords
    SWORD sol_dpy

; --- sol_card / sol_drawface -------------------------------------------------
    SWORD sol_cdx                   ; the card's origin, SCREEN coords
    SWORD sol_cdy
    SWORD sol_cvis
    SBYTE sol_ccard

    SWORD sol_ry1                   ; the top of the rect sol_drawpile erases

; --- sol_maskrun -------------------------------------------------------------
    SWORD sol_mrx
    SWORD sol_mry
    SWORD sol_mrw
    SWORD sol_mrr                   ; rows the caller asked for
    SBYTE sol_mrh                   ; 1 = outline this mask as it is plotted
                                    ; (SPEC.md 43.11). ONE-SHOT: sol_pipsel
                                    ; sets it, sol_maskrun clears it
    SBYTE sol_pipink                ; the colour a set mask bit lays down, or
                                    ; SOL_NOPEN for an ink the sprite pass
                                    ; cannot lay down at all. It is a REQUIRED
                                    ; input to sol_maskrun and all three
                                    ; callers set it, so the zero a fresh .bss
                                    ; starts at is never the one read

; The sprite record itself (SPEC.md 43.11.3), which must be one contiguous
; run: `db wwords, rows`, then rows MASK words, then rows DATA words. It is
; .bss and not .text because the mask is composed per pip - and being .bss the
; DATA half costs claim rather than image, and starts zero.
    SBUF  sol_icrec, 2              ; wwords (always 1), then rows
    SBUF  sol_icmask, SOL_ICROWS*4  ; rows of mask, then rows of data

; --- sol_colfan / sol_yoff ---------------------------------------------------
    SWORD sol_cfd                   ; face-down cards in the column
    SWORD sol_cfa                   ; ...their fan step
    SWORD sol_cfu                   ; ...and the face-up one
    SWORD sol_keepn                 ; leading cards this draw may skip
    SWORD sol_lastox                ; the origin the shadow below belongs to
    SWORD sol_lastoy
    SBUF  sol_shw, P_TABN*SOL_SHW   ; what is on the glass, per column (43.10)

; --- sol_hit -----------------------------------------------------------------
    SWORD sol_htx
    SWORD sol_hty

; --- the drag ----------------------------------------------------------------
    SBYTE sol_dsrc                  ; the pile the hand came off
    SBYTE sol_didx                  ; the card index inside it
    SBYTE sol_dnum                  ; cards in the hand
    SWORD sol_dfan                  ; the fan the outline's rules use
    SWORD sol_dox                   ; the outline, SCREEN coords
    SWORD sol_doy
    SWORD sol_dh
    SWORD sol_dsx                   ; where it started, for the move test
    SWORD sol_dsy
    SWORD sol_dgx                   ; pointer offset inside the outline
    SWORD sol_dgy

; --- odds and ends -----------------------------------------------------------
    SWORD sol_pnl                   ; sol_panel's card: its left and width,
    SWORD sol_pnw                   ; content coords

    OS88_BSS sol_bssbase + SOL_BSS - os88_image_end
    OS88_IMAGE_END
%if (os88_image_end - sol_entry) > SOL_BACKMAX
%error "sol_entry has outgrown the card back it becomes (SPEC.md 43.11.6)"
%endif
