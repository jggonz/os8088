; =============================================================================
; os8088 - apps/cyclone/cyclone.asm
;
; Cyclone 88 (SPEC.md 67) - a Tempest 2000 clone, and the tree's second
; vector-ish arcade game after Missile Command. A .o88 at org 0 that owns a
; segment (SPEC.md 20.1), prefix `cy_`, embedded icon, one worker task.
;
; WHAT MAKES A TUBE SHOOTER AFFORDABLE ON A 4.77MHz 8088
;
; Tempest is a vector game: the arcade redraws its whole web every frame
; because a vector monitor has no other mode of operation. This machine
; cannot - PERFORMANCE.md Part 2 prices a `gfx_*` call at ~756us of ARRIVING
; whatever it draws, a `gfx_fill` at ~1.16ms nearly all of which is that
; floor, and a line-walk pixel at ~175us. A 16-lane web is 49 line segments
; of 40-80 pixels: drawing it ONCE is ~180ms. Per frame it is impossible, and
; no amount of tuning inside the primitives changes that by the order of
; magnitude required.
;
; So the web is drawn ONCE and never again, and the four decisions below all
; fall out of that:
;
;  - **The playfield is STATIC and the movers never touch it.** Every enemy,
;    shot and the player's claw is drawn strictly INSIDE its lane, clear of
;    the spoke lines that bound it - so erasing a mover to the background
;    colour cannot cut the web, and an erase is therefore one `gfx_fill`
;    rather than a repair. That single constraint is what removes the whole
;    save-under/repair machinery a sprite over static art would otherwise
;    need. The depth rings are drawn only at the far end, where nothing
;    travels. **It has to be ENFORCED at both ends of the tube**: inside it
;    by `cy_lanecap`, which bounds a mover by its lane rather than by the
;    depth ramp, and on the lip by `cy_lip_build`, because CY_TOPD is a SCALE
;    and a scale clears nothing on a web whose rim edge runs radially - the
;    flat ribbon put the claw exactly on the web at every window size.
;
;  - **A mover that did not move is not drawn.** `cy_mv_step` compares the
;    rounded screen rect against the one on the glass and returns early when
;    they match, which at low depth (where an enemy advances a fraction of a
;    pixel a frame) is most frames and most enemies.
;
;  - **A mover that DID move writes every pixel once** (SPEC.md 7.1.2's rule,
;    as Arkanoid's `ark_rsub` and Paint's `pt_ptr_sub` apply it): the new rect
;    is drawn FIRST and then only the part of the old rect the new one does
;    not cover is erased. Erase-then-draw would leave the shared pixels dark
;    for the length of the gap, which is PERFORMANCE.md's double-draw flash
;    and is plainly visible on the target machine.
;
;  - **The warp is the one thing that draws the web, and it PAYS ITSELF OFF.**
;    Level entry does not zoom a finished web; it EXTRUDES one, a few pixels
;    of every spoke per frame, through SPEC.md 5.6.7's resumable walk batched
;    into ONE `OSAPI_GFX_LSTEPV` call a frame. Nothing is ever erased during
;    it - the animation ACCUMULATES - so the frame cost is 17 block setups
;    plus the pixels actually laid (~20ms), it is smooth at 18fps, and when
;    it finishes the static playfield is already on the glass because drawing
;    it WAS the animation. The 180ms full-web draw is never paid as a lump.
;    Leaving a level replays the identical walks in the background colour, so
;    the erase visits exactly the pixels the draw visited (5.6.7's whole
;    point) and no remnant is possible.
;
; Everything else is bookkeeping around those four.
;
; WHAT CAME FROM TEMPEST 2000 (Jeff Minter / Llamasoft, Atari Jaguar, 1994)
; rather than from the 1981 arcade: the powerup progression is T2K's - the
; particle laser, the AI droid, the jump and the zapper are its four, they
; arrive as pickups dropped by kills rather than as a fixed loadout, and they
; ramp with the level so the player is measurably stronger at 10 than at 1.
; The score-attack furniture (the bonus at level end, the high-score scroll on
; the attract screen) is T2K's shape too. The enemy roster - flipper, tanker,
; spiker, fuseball, pulsar - and the web geometry are the original's, because
; T2K keeps them.
;
; Keys: left/right or A/D move, Space/Ctrl fire, Z superzapper, J jump,
;       F full screen, Esc leave it, P pause, N new game, Enter start.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'CYCLONE 88', cy_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) --------------------------
; The tube seen down its own axis: concentric rings converging on a vanishing
; point, which is the one picture that says "Tempest" at 16 pixels. The mask is
; the full square, so it sits on a clean white tile over the desktop dither
; rather than letting the dither show between the rings.
;
;   ################
;   #..............#
;   #.############.#
;   #.#..........#.#
;   #.#.########.#.#
;   #.#.#......#.#.#
;   #.#.#.####.#.#.#
;   #.#.#.#..#.#.#.#
;   #.#.#.#..#.#.#.#
;   #.#.#.####.#.#.#
;   #.#.#......#.#.#
;   #.#.########.#.#
;   #.#..........#.#
;   #.############.#
;   #..............#
;   ################
    OS88_ICON16
    dw 0xFFFF                       ; 16 mask rows - a solid white underlay
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF                       ; 16 data rows - the black pixels
    dw 0x8001
    dw 0xBFFD
    dw 0xA005
    dw 0xAFF5
    dw 0xA815
    dw 0xABD5
    dw 0xAA55
    dw 0xAA55
    dw 0xABD5
    dw 0xA815
    dw 0xAFF5
    dw 0xA005
    dw 0xBFFD
    dw 0x8001
    dw 0xFFFF
    OS88_ICON16_END

; =============================================================================
; Constants
; =============================================================================

CY_MAXV     equ 17                  ; vertices in the widest web (16 + wrap)
CY_MAXLANE  equ 16
CY_DEPTH    equ 16                  ; depth steps: 0 = the far end, 16 = the rim
CY_NDEPTH   equ 18                  ; ...plus CY_TOPD above it = 18 table rows

; THE DEPTHS A MOVER MAY OCCUPY. The two rings that ARE drawn - the far ring
; at depth 0 and the rim polygon at CY_DEPTH - are static art, and 67.1's
; whole scheme depends on a mover's erase never reaching either.
;
; So there is a depth ABOVE the rim, and the player sits there: CY_TOPD is
; outside the web looking down into it, which is Tempest 2000's claw rather
; than the original's, and it leaves the playfield cleaner because nothing the
; player does ever happens among the spokes. A fully ascended enemy arrives
; there too - it has climbed out of the tube and is on the lip with you.
;
; Nothing is ever DRAWN at CY_DEPTH itself (cy_dtab maps it to CY_TOPD), so a
; mover crossing the rim hops it in a single frame and the rim polygon is
; never touched.
CY_TOPD     equ 17                  ; on the lip: the player, and arrivals
; ...and the far end. FOUR, not two, and the two pixels are the whole reason:
; a lane's width is proportional to its ring's radius, so at depth 2 it is
; about 4px across while a mover with cy_lanecap's one pixel of air is still
; 3px wide - and the rounding put it on the spoke often enough to nibble the
; web. At depth 4 the lane is ~6px and the clearance is a pixel and a half
; either side. Verified by the incremental-vs-full-repaint diff, which is what
; this constant is really tuned against.
CY_FARD     equ 4

CY_HUD_H    equ 10                  ; score strip: an 8px cell and 2 of air
CY_MARGIN   equ 3

; The window this opens as. Deliberately smaller than the desktop band on
; every adapter (CGA's is 200-20-1 = 179 rows), because wm_fit clamps a
; template that does not fit and a clamped window is one whose layout the app
; did not choose. The playfield scales to whatever it actually gets.
CY_WIN_W    equ 320
CY_WIN_H    equ 200

CY_MINR     equ 12                  ; below this the web is unreadable; the
                                    ; layout refuses and the app says so

; --- movers -------------------------------------------------------------------
CY_MAXENEM  equ 10                  ; live enemies. The arcade's on-screen cap
CY_MAXSHOT  equ 6                   ; player shots in flight
CY_MAXESHOT equ 6                   ; enemy shots
CY_MAXPU    equ 3                   ; powerup pickups on the web at once
CY_RIMSTEP  equ 14                  ; frames between rim steps: slow enough
                                    ; to be shootable, fast enough to matter
CY_MAXDBR   equ 8                   ; debris particles in the game-over burst

; Every mover on the web has a 10-byte "what is on the glass" block, and they
; live in one array so a full repaint can forget all of them in one loop.
CY_OB_E     equ 0
CY_OB_S     equ CY_OB_E  + CY_MAXENEM
CY_OB_ES    equ CY_OB_S  + CY_MAXSHOT
CY_OB_PU    equ CY_OB_ES + CY_MAXESHOT
CY_OB_D     equ CY_OB_PU + CY_MAXPU
CY_OB_PL    equ CY_OB_D  + CY_MAXDBR
CY_NOBJ     equ CY_OB_PL + 1
CY_OBSZ     equ 12                  ; bytes of "what is on the glass" per object

; Pixels of every walk laid per warp frame. 4 puts a 60-pixel spoke on screen
; in 15 frames - a little under a second at 18fps - and costs at most
; CY_MAXV block setups plus CY_MAXV*4 walk pixels, ~20ms of a 55ms tick.
CY_WARPK    equ 4

CY_HUDW     equ 24                  ; cells in the status strip
CY_MSGW     equ 24                  ; ...and in the banner
; The scroll's row PITCH, and it is 12 rather than the glyph's 8 so that
; consecutive lines have leading between them - at 8 the rows abut and two
; lines read as one overlapping block. 12 is also a multiple of 4, which is
; what keeps gfx_scroll on its fast path on Hercules (4 banks) and CGA (2)
; alike; an odd delta misses it on both (SPEC.md 5.5.1, and Note Pad's find
; panel is the precedent for choosing the height around that).
CY_SCRROW   equ 12
CY_SCRN     equ 4                   ; rows in the band
CY_SCRH     equ CY_SCRROW * CY_SCRN
CY_FIRECD   equ 3                   ; frames between shots while fire is held
CY_SCRT     equ 12                  ; frames between scroll steps. It was 3,
                                    ; which is a row every 165ms and faster
                                    ; than a name-and-score line can be read;
                                    ; then 6, and 12 is that halved AGAIN once
                                    ; SPEC.md 67.21.1's nineteen lines started
                                    ; arriving. A row every 660ms, so a line is
                                    ; on the glass for 2.6s of its four-row
                                    ; crossing and a whole cycle is ~16s
CY_SCRMAXC  equ 40                  ; the widest scroll row we will letter

; --- the model swatches (SPEC.md 67.21.2) ------------------------------------
; A scroll line may carry a PICTURE of the thing it names, and which picture is
; in the line's own first byte: 1..CYE_KINDS is that enemy kind, CY_SCRM_PU is
; the powerup box. In the text rather than in a parallel table indexed by line,
; because a table is a second place that has to agree about which line is which
; and this tree has paid for that shape twice already.
;
; The shapes are NOT redrawn here. cy_ekext and cy_ekcol are the tables the
; game itself draws an enemy from - and cy_pal REWRITES two of those pens for
; the live adapter (a dither class on VGA, white on 1bpp) - so reading them is
; what makes a swatch a picture of what you will actually meet rather than a
; second artist's impression of it.
CY_SCRM_PU  equ CYE_KINDS + 1       ; ...and the powerup, which is not a kind
CY_SCRMW    equ 2                   ; glyph cells a swatch reserves at the pen
CY_SCRM_SPL equ 0xFE                ; a line that IS the high-score table
CY_SCRM_END equ 0xFF                ; ...and the one that ends the list
CY_NHS      equ 5
CY_HSFSZ    equ 4 + CY_NHS * 7       ; magic, then a score and a name per row
CY_INITW    equ 12                  ; glyph cells the initials prompt spans

; enemy kinds, in the order the difficulty ramp introduces them
CYE_FLIPPER equ 0
CYE_TANKER  equ 1
CYE_SPIKER  equ 2
CYE_FUSEBALL equ 3
CYE_PULSAR  equ 4
CYE_KINDS   equ 5

; powerup kinds - Tempest 2000's four
CYP_LASER   equ 0                   ; particle laser: shots pierce
CYP_DROID   equ 1                   ; AI droid: an autonomous second gun
CYP_JUMP    equ 2                   ; jump: hop over what is on your lane
CYP_ZAP     equ 3                   ; an extra superzapper charge
CYP_KINDS   equ 4

; game states
CYS_TITLE   equ 0                   ; attract screen, animating
CYS_WARPIN  equ 1                   ; the web extruding into view
CYS_PLAY    equ 2
CYS_WARPOUT equ 3                   ; flying down the tube, web draining
CYS_DIE     equ 4                   ; the player's death throe
CYS_OVER    equ 5                   ; game over animation
CYS_PAUSE   equ 6

; --- the colour plan (SPEC.md 39.4) -------------------------------------------
; Everything is drawn on a black field, so a colour from the BLACK class
; (0..6) is INVISIBLE on Hercules and CGA - the trap SPEC.md 44.6 records and
; Missile Command's wave counter fell into. Every ink here is therefore from
; the WHITE class (12/14/15) or the DITHER class (7..11, 13), and the four
; things a player must tell apart - web, player, enemy, shot - are drawn from
; classes that stay apart once colour has reduced to three inks.
;
; A dithered 8x8 GLYPH loses half of each stroke, so text is white-class only.
CY_C_WEB    equ CLBLUE              ; dither - the web recedes behind everything
CY_C_WEBFAR equ CDGRAY              ; dither - the far rings, fainter still
CY_C_PLAYER equ CYELLOW             ; white
CY_C_SHOT   equ CWHITE              ; white
CY_C_ENEMY  equ CLRED               ; white
; ...AND A SMALL MOVER MUST NOT BE DITHERED AT ALL. These two are dither-class
; on purpose so that a 4bpp screen gets four distinguishable inks - but on the
; two 1bpp adapters a dither is a checkerboard keyed on (x+y)&1, so an object
; three pixels across loses half of itself and CHANGES WHICH HALF as it moves.
; It reads as movers flashing in and out of existence, which is what it is.
; SPEC.md 48.21 is the same finding about a one-pixel trail - "not a second
; ink, half a line" - and 39.4's classes are about big flat areas, not about
; objects this size. cy_pal swaps both for the white class on 1bpp, where the
; SHAPES (67.9's aspect ratios) are what tell the kinds apart anyway.
CY_C_ENEMY2 equ CLGRAY              ; dither - VGA only, see cy_pal
CY_C_ESHOT  equ CLMAGENTA           ; dither - VGA only, see cy_pal
CY_C_PU     equ CYELLOW             ; white
CY_C_TEXT   equ CWHITE              ; white - glyphs must never dither
CY_C_BG     equ CBLACK

; =============================================================================
; The web shapes (SPEC.md 67.2)
;
; A level's web is a closed or open polygon of rim vertices in a normalised
; -128..127 space with (0,0) at the vanishing point. The far end of the tube
; is the same polygon scaled toward that point, and every depth between is a
; scale in `cy_scale` - so ONE table of rim vertices describes the whole
; three-dimensional tube and there is no 3D arithmetic anywhere in this app.
;
; Descriptor: db nlanes, closed; then nverts pairs of (db x, db y), where
; nverts = nlanes for a closed web and nlanes+1 for an open one. A closed web
; wraps lane nlanes-1 back onto vertex 0; an open one does not, and the player
; hits a wall at each end.
; =============================================================================

%macro CYSHAPE 2                    ; nlanes, closed
    db %1, %2
%endmacro

; --- 0: the circle. Tempest's level 1, and the one every player learns on ----
cy_sh_circle:
    CYSHAPE 16, 1
    db  100,   0
    db   92,  38
    db   71,  71
    db   38,  92
    db    0, 100
    db  -38,  92
    db  -71,  71
    db  -92,  38
    db -100,   0
    db  -92, -38
    db  -71, -71
    db  -38, -92
    db    0,-100
    db   38, -92
    db   71, -71
    db   92, -38

; --- 1: the square -----------------------------------------------------------
cy_sh_square:
    CYSHAPE 16, 1
    db -100,-100
    db  -50,-100
    db    0,-100
    db   50,-100
    db  100,-100
    db  100, -50
    db  100,   0
    db  100,  50
    db  100, 100
    db   50, 100
    db    0, 100
    db  -50, 100
    db -100, 100
    db -100,  50
    db -100,   0
    db -100, -50

; --- 2: the plus. Four inside corners, so a flipper can hide -----------------
cy_sh_plus:
    CYSHAPE 12, 1
    db  -33,-100
    db   33,-100
    db   33, -33
    db  100, -33
    db  100,  33
    db   33,  33
    db   33, 100
    db  -33, 100
    db  -33,  33
    db -100,  33
    db -100, -33
    db  -33, -33

; --- 3: the triangle ---------------------------------------------------------
cy_sh_tri:
    CYSHAPE 12, 1
    db    0,-100
    db   25, -55
    db   50, -10
    db   75,  35
    db  100,  80
    db   50,  80
    db    0,  80
    db  -50,  80
    db -100,  80
    db  -75,  35
    db  -50, -10
    db  -25, -55

; --- 4: the star -------------------------------------------------------------
cy_sh_star:
    CYSHAPE 16, 1
    db  100,   0
    db   41,  17
    db   71,  71
    db   17,  41
    db    0, 100
    db  -17,  41
    db  -71,  71
    db  -41,  17
    db -100,   0
    db  -41, -17
    db  -71, -71
    db  -17, -41
    db    0,-100
    db   17, -41
    db   71, -71
    db   41, -17

; --- 5: the vee. OPEN - the player hits a wall at both ends ------------------
cy_sh_vee:
    CYSHAPE 12, 0
    db -100, -80
    db  -83, -52
    db  -67, -23
    db  -50,   5
    db  -33,  33
    db  -17,  62
    db    0,  90
    db   17,  62
    db   33,  33
    db   50,   5
    db   67, -23
    db   83, -52
    db  100, -80

; --- 6: the flat ribbon. OPEN, and the level that reads most like falling ----
cy_sh_flat:
    CYSHAPE 12, 0
    db -100,   0
    db  -83,   0
    db  -67,   0
    db  -50,   0
    db  -33,   0
    db  -17,   0
    db    0,   0
    db   17,   0
    db   33,   0
    db   50,   0
    db   67,   0
    db   83,   0
    db  100,   0

; --- 7: the horseshoe. OPEN ---------------------------------------------------
cy_sh_uu:
    CYSHAPE 12, 0
    db -100, -70
    db -100, -25
    db -100,  20
    db  -88,  55
    db  -60,  80
    db  -25,  92
    db    0,  95
    db   25,  92
    db   60,  80
    db   88,  55
    db  100,  20
    db  100, -25
    db  100, -70

CY_NSHAPE   equ 8
cy_shapes:
    dw cy_sh_circle, cy_sh_square, cy_sh_plus, cy_sh_tri
    dw cy_sh_star,   cy_sh_vee,    cy_sh_flat, cy_sh_uu

; --- cy_pal -------------------------------------------------------------------
; Pick the mover inks for the display THIS WINDOW is on.
;
; OSAPI_FSX_CAPS rather than OSAPI_VIDEO's DH, because DH answers about the
; PRIMARY display (SPEC.md 39.2.1) and on an extended desktop we may not be on
; it. EVERY BRANCH WRITES BOTH WAYS, which is Missile Command's mc_adapter
; rule: this is re-run when the window changes display, and a body that only
; ever SET the mono case would leave a machine that moved back to VGA on the
; mono palette for ever.
; -----------------------------------------------------------------------------
cy_pal:
    push ax
    push bx
    push cx
    push dx
    mov bx, [cy_win]
    call OSAPI_FSX_CAPS             ; DL = that display's VID_* kind
    mov al, CY_C_ENEMY2
    mov ah, CY_C_ESHOT
    cmp dl, VID_VGA
    je .have
    mov al, CWHITE
    mov ah, CWHITE
.have:
    mov [cy_c_e2], al
    mov [cy_c_es], ah
    mov [cy_ekcol + CYE_TANKER], al
    mov [cy_ekcol + CYE_SPIKER], al
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the perspective ladder ---------------------------------------------------
; scale[d] as a fraction of 256. Geometric at about 1.15 a step, which is what
; makes the far end crowd together and the tube read as depth rather than as a
; stack of rings. scale[16] = 256 puts the rim at the full radius.
; scale[d] as a fraction of 256, and 256 IS THE RIM. The last entry is above
; it: that is the lip the player stands on, and cy_layout shrinks the radius
; so the whole ladder including that entry fits the playfield.
CY_TOPSCALE equ 296
cy_scale:
    dw 26, 30, 35, 41, 48, 56, 65, 76, 88, 102, 118, 137, 158, 183, 208, 230
    dw 256                          ; CY_DEPTH  - the rim polygon
    dw CY_TOPSCALE                  ; CY_TOPD   - the lip, outside it

; The depth a mover is DRAWN at, given the depth it is at. Only CY_DEPTH is
; remapped: that row is the rim polygon's own, and a mover centred on it would
; erase the rim. One table read beats a compare at five call sites.
cy_dtab:
    db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, CY_TOPD, CY_TOPD

; =============================================================================
; Strings
; =============================================================================
cy_title        db 'Cyclone 88', 0
cy_s_name       db 'CYCLONE 88', 0
cy_s_press      db 'PRESS ENTER TO START', 0
cy_s_keys       db 'ARROWS MOVE  SPACE FIRE  Z ZAP  J JUMP', 0
cy_s_hiscore    db 'HIGH SCORES', 0
cy_s_over       db 'GAME OVER', 0
cy_s_level      db 'LEVEL', 0
cy_s_bonus      db 'BONUS', 0
cy_s_small      db 'Window too small', 0
cy_s_paused     db 'PAUSED', 0
cy_s_avoid      db 'AVOID THE SPIKES', 0
cy_s_superzap   db 'SUPERZAPPER RECHARGE', 0

; the powerup names, indexed by CYP_*
cy_pu_names:
    dw cy_pn_laser, cy_pn_droid, cy_pn_jump, cy_pn_zap
cy_pn_laser     db 'PARTICLE LASER', 0
cy_pn_droid     db 'AI DROID', 0
cy_pn_jump      db 'JUMP', 0
cy_pn_zap       db 'ZAPPER CHARGE', 0

; The attract screen's scrolling text. One line per row, an empty string is a
; blank line, and the list ends with 0FFh.
;
; THE TERMINATOR IS 0FFh AND NOT A BARE NUL, for cy_ab_text's reason four
; hundred lines below - and this list is where that bug actually shipped. A
; blank separator line IS an empty string, so it is a single 0 byte, and
; `cy_scroll_next` read the first of them as the end of the list: the attract
; screen showed CYCLONE 88 and then went straight to the high scores for ever.
; Every line from here down had never been on screen. The panel next door
; carries the same warning because the same mistake was found there first and
; fixed only there.
;
; The bound is 40 glyphs (CY_SCRMAXC), which is the widest row the scroller
; will letter; cy_scroll_row CLAMPS rather than overflowing, so a longer line
; is silently cut off at both ends by the centring.
; A line beginning with a byte under 0x20 carries a model swatch (above); the
; two spaces after it are the room the picture is drawn in, so the centring
; counts them and the line and its picture cannot part company.
cy_scroll_txt:
    db 'CYCLONE 88', 0
    db '', 0
    db 'A TUBE SHOOTER FOR THE 8088', 0
    db '', 0
    db 'AFTER TEMPEST 2000', 0
    db '', 0
    db CY_SCRM_SPL                  ; THE HIGH SCORES GO HERE, which is above
    db '', 0                        ; the enemies now: the table is what a
                                    ; player comes back for and the bestiary is
                                    ; read once, so the table should not be at
                                    ; the far end of a 25-line cycle
    db 'ENEMIES', 0
    db CYE_FLIPPER  + 1, '  FLIPPER  CLIMBS THE WEB', 0
    db CYE_TANKER   + 1, '  TANKER   SPLITS WHEN SHOT', 0
    db CYE_SPIKER   + 1, '  SPIKER   BUILDS THE SPIKES', 0
    db CYE_FUSEBALL + 1, '  FUSEBALL WALKS THE RIM', 0
    db CYE_PULSAR   + 1, '  PULSAR   ELECTRIFIES A LANE', 0
    db '', 0
    ; ...and the powerups get a line each rather than a bare name, the way the
    ; enemies do: the names are the ones the pickup banner says when you take
    ; one, so the scroll and the game agree on what to call them. The hollow
    ; box is 67.9.1 - the one thing on the web that is not solid - and it is
    ; drawn on the HEADER and not on all four, because all four arrive as the
    ; same box and four copies of one picture is noise rather than information.
    db CY_SCRM_PU, '  POWERUPS - THE HOLLOW BOX', 0
    db 'PARTICLE LASER  SHOTS PIERCE', 0
    db 'AI DROID        A SECOND GUN', 0
    db 'JUMP            HOP A LANE', 0
    db 'ZAPPER CHARGE   ONE MORE ZAP', 0
    db '', 0
    db CY_SCRM_END

; =============================================================================
; The panels - About and How To Play (SPEC.md 67.14)
;
; ONE panel with two texts, because they are the same object: a box drawn last
; over a frozen game, taken down by any key or click. Each is CY_*LINE lines of
; at most CY_*COL glyphs, and the box is sized from those two numbers rather
; than measured - a panel that grows when a string does is a panel whose frame
; and whose text can disagree.
;
; THE LINE COUNT IS BOUNDED BY CGA AND NOT BY TASTE. The content box there is
; 136 rows (dock 176, less MBAR_H, TITLE_H and wm_fit's row), so a panel is
; lines*9 + 12 <= 136 - thirteen lines. The primitives clamp to the content box
; (cy_clamp), so a fourteenth would not draw through the frame; it would be cut
; off, which is worse, because nothing says so.
; =============================================================================
CY_ABCOL    equ 24                  ; glyph columns the widest line may use
CY_ABLINE   equ 9                   ; ...and lines, both asserted below
CY_ABW      equ CY_ABCOL * 8 + 16   ; ...plus an 8px inset each side
CY_ABH      equ CY_ABLINE * 9 + 12

CY_HPCOL    equ 29                  ; the same two numbers for How To Play
CY_HPLINE   equ 13
CY_HPW      equ CY_HPCOL * 8 + 16
CY_HPH      equ CY_HPLINE * 9 + 12

; THE TERMINATOR IS 0FFh AND NOT A BARE NUL, because a blank separator line
; IS an empty string: with 0 ending the block the panel stopped at its own
; first gap and drew one line of four.
cy_ab_text:
    db 'CYCLONE 88', 0
    db '', 0
    db 'A Tempest 2000 for the', 0
    db 'Intel 8088.', 0
    db '', 0
    db 'Arrows steer, Space', 0
    db 'fires, Z zaps, J jumps', 0
    db '', 0
    db 'Contributed by Elendilon', 0
    db 0xFF
cy_ab_end:
; The box is sized from CY_ABLINE and CY_ABCOL and the strings are NOT
; measured, so the two have to be tied together here: a line added later would
; otherwise run out through the frame, and the gfx primitives clip to the
; SCREEN and will not stop it. The bound is aggregate - every line plus its
; NUL, plus the block terminator - which catches a tenth line and catches
; the lines getting longer, together.
%if (cy_ab_end - cy_ab_text) > CY_ABLINE * (CY_ABCOL + 1) + 1
  %error "cy_ab_text no longer fits the CY_ABLINE x CY_ABCOL box it is drawn in"
%endif

; The Help menu's one item. The keys FIRST, because that is what a player who
; has just opened the window cannot guess, and the rules under them - the
; attract screen's scroll (SPEC.md 67.10) lists the enemies and the powerups
; and this does not repeat it. It could not, when this was written: the scroll
; stopped at its second line and nineteen of its lines had never been on screen
; (SPEC.md 67.21.1), so this panel was deferring to a list nobody could see.
cy_hp_text:
    db 'HOW TO PLAY', 0
    db '', 0
    db 'ARROWS   move round the rim', 0
    db 'SPACE    fire down the web', 0
    db 'Z  zap   J  jump   P  pause', 0
    db 'F        full screen', 0
    db 'ENTER    start a game', 0
    db '', 0
    db 'Shoot every enemy to warp to', 0
    db 'the next level. One that gets', 0
    db 'to the rim shoots back, so', 0
    db 'jump clear or kill it first.', 0
    db 'Grab the powerups on the web.', 0
    db 0xFF
cy_hp_end:
%if (cy_hp_end - cy_hp_text) > CY_HPLINE * (CY_HPCOL + 1) + 1
  %error "cy_hp_text no longer fits the CY_HPLINE x CY_HPCOL box it is drawn in"
%endif

; =============================================================================
; The menu set (SPEC.md 12)
; =============================================================================
; EVERY ONE OF THESE IS A POINTER. The macros emit `dw %1` for the app name,
; each menu title and each item array, and AMENU_ITEMS is an array of WORD
; pointers to NUL strings - not the strings laid end to end. Written with the
; strings inline, `dw 'Game'` assembles cleanly as the word 0x6147 and the
; kernel then letters whatever lives at that offset: the bar came up as a row
; of garbage glyphs with one item's text loose in it, on every adapter, for as
; long as this app has had a menu.
cy_m_name:  db 'Cyclone 88', 0
cy_m_game:  db 'Game', 0
cy_m_help:  db 'Help', 0
cy_i_new:   db 'New Game', 0
cy_i_pause: db 'Pause', 0
cy_i_full:  db 'Full Screen', 0
cy_i_how:   db 'How To Play', 0

cy_mi_game: dw cy_i_new, cy_i_pause, cy_i_full
cy_mi_help: dw cy_i_how

    OS88_MENUSET cy_menus, cy_m_name, cy_oncmd
        OS88_MENU cy_m_game, cy_mi_game, 3
        OS88_MENU cy_m_help, cy_mi_help, 1
    OS88_MENUSET_END cy_menus

; =============================================================================
; The window template
; =============================================================================
cy_tpl:
    dw 40                           ; WT_X   - fixed up by cy_entry
    dw 40                           ; WT_Y
    dw CY_WIN_W + 2                 ; WT_W   - content + the 1px sides
    dw CY_WIN_H + TITLE_H + 1       ; WT_H
    dw cy_title                     ; WT_TITLE
    dw cy_paint                     ; WT_PAINT
    dw cy_onkey                     ; WT_ONKEY
    dw cy_onclick                   ; WT_ONCLICK

; =============================================================================
; cy_entry - the package entry point (SPEC.md 20.2)
;
; Sizes the window against the live screen, creates it, registers the menus,
; the About handler and the resize callback, and returns CF to the loader. It
; does NOT spawn the worker: OSAPI_TASK_SPAWN wants the gfx lock held and is
; not callable from here, so the first W_PAINT hires it (tamegram's shape).
; =============================================================================
cy_entry:
    push si
    push di

    call OSAPI_VIDEO                ; AX=w BX=h CX=dock top DL=kind DH=bpp
    mov [cy_scrw], ax
    mov [cy_scrh], bx
    mov [cy_dock], cx
    mov [cy_vkind], dl
    mov [cy_bpp], dh

    ; Seed the stream once. Every wave, drop and debris vector runs off it.
    call OSAPI_GET_TICKS
    call OSAPI_SRAND

    ; The content height wm_fit will actually allow: the desktop band is
    ; MBAR_H .. dock-1 and wm_fit shaves one more row, because the drop
    ; shadow is on y+h and a frame that merely REACHES the dock strip is
    ; already on it (CLAUDE.md's wm_fit note). Taking the pixel here keeps
    ; our template in step with the window the kernel will actually make -
    ; otherwise the kernel clamps and our layout is one row out for ever.
    mov ax, cx
    sub ax, MBAR_H + TITLE_H + 2
    jns .havh
    xor ax, ax
.havh:
    cmp ax, CY_WIN_H
    jbe .hok
    mov ax, CY_WIN_H
.hok:
    add ax, TITLE_H + 1
    mov [cy_tpl + WT_H], ax

    mov ax, [cy_scrw]               ; content width: the window, or the screen
    sub ax, 2                       ; if the screen is narrower than it
    cmp ax, CY_WIN_W
    jbe .wok
    mov ax, CY_WIN_W
.wok:
    add ax, 2
    mov [cy_tpl + WT_W], ax

    mov ax, [cy_scrw]               ; centred horizontally...
    sub ax, [cy_tpl + WT_W]
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [cy_tpl + WT_X], ax

    mov ax, [cy_dock]               ; ...and centred in the desktop band
    sub ax, MBAR_H
    sub ax, [cy_tpl + WT_H]
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [cy_tpl + WT_Y], ax

    mov si, cy_tpl
    call OSAPI_WM_CREATE
    jc .fail
    mov [cy_win], bx

    ; We paint every pixel of our content ourselves - the field is black and
    ; the kernel's white fill before W_PAINT would be a full-content flash on
    ; every repaint, which is exactly PERFORMANCE.md's double-draw defect.
    mov al, 1
    call OSAPI_WM_OWNBG

    ; Aim by lane, not by pixel: the arrow is the wrong pointer over a tube.
    mov al, OSAPI_CUR_CROSS
    call OSAPI_WM_CURSOR

    ; The content origin on a multiple of 8 earns font_run's single-store
    ; path for the HUD (SPEC.md 6.1) - the strip is the only text drawn
    ; during play and it is drawn on every score change.
    mov al, 1
    call OSAPI_WM_SNAP

    call cy_pal                     ; the inks for the display we opened on
    mov bx, [cy_win]                ; both take BX explicitly rather than
    mov si, cy_menus                ; trusting six intervening slots to have
    call OSAPI_MENU_SET             ; left it alone
    mov bx, [cy_win]
    mov si, cy_about
    call OSAPI_ABOUT_SET

    ; "Your content box CHANGED and you did not ask" - the extended-desktop
    ; case (SPEC.md 39.11): a window moved between a CGA and a Hercules
    ; changes size without a resize, and the whole playfield is derived from
    ; that box. We may not draw in it, so it only marks.
    mov ax, cy_onresize
    call OSAPI_WM_ONRESIZE

    ; Arm the key-state map (SPEC.md 9.7). Asking is what arms it, and the
    ; first answer is always "up" - so ask now and throw it away, or the
    ; first arrow of the session reads up until its first repeat.
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN

    mov byte [cy_state], CYS_TITLE
    mov byte [cy_needlay], 1

    ; THE ATTRACT SCREEN NEEDS A WEB BEFORE ANY LEVEL HAS STARTED. Only
    ; cy_startlevel and cy_title_nextshape set this, and neither has run yet -
    ; so the first cy_layout read its descriptor from offset 0, which is the
    ; package HEADER (SPEC.md 20.2). 'O','8' decodes as 79 lanes and a closed
    ; flag of 56, which is not merely a wrong picture: 79 vertices is well
    ; past CY_MAXV and cy_build_verts wrote through the end of the vertex
    ; tables. It showed as a small tangle in the corner on the very first
    ; frame of the attract screen and nowhere else.
    mov word [cy_shape], cy_sh_circle
    mov word [cy_tshape], 0
    call cy_hs_init

    mov bx, [cy_win]
    call OSAPI_WM_SHOW
    pop di
    pop si
    clc
    ret
.fail:
    pop di
    pop si
    stc
    ret

; =============================================================================
; THE PANELS (SPEC.md 67.14)
;
; Two menu items put a page of text over the game: `About Cyclone 88` on the
; app-name cell (SPEC.md 12.7) and `How To Play` on the Help menu. Both used to
; be a bare cy_full_repaint - a menu item that visibly does nothing, in the two
; cells whose whole purpose is to say something.
;
; ONE panel serves both, because the drawing, the freeze and the dismissal are
; the same three things and a second copy of them is a second opinion about
; when the box comes down. What differs is a text pointer and two dimensions,
; banked at arm time; nothing re-derives which panel is up.
;
; Arkanoid's shape (SPEC.md 44): a flag, drawn LAST by the ordinary render so
; there is no second window and no second paint path, and any key or click
; takes it down. A live game is frozen underneath it, because a dropped frame
; does not stop the enemies and a player would lose a life reading the rules.
; =============================================================================

; cy_about / cy_help - the two arms. Each names its text and its box and falls
; into the body.
cy_about:
    mov si, cy_ab_text
    mov ax, CY_ABW
    mov bx, CY_ABH
    mov cl, CY_ABLINE
    jmp short cy_panel_up

cy_help:
    mov si, cy_hp_text
    mov ax, CY_HPW
    mov bx, CY_HPH
    mov cl, CY_HPLINE
    ; ...and fall through

; cy_panel_up - arm the panel. SI = text, AX/BX = box size, CL = its line cap.
; UI task, gfx lock held.
cy_panel_up:
    push si
    mov [cy_pntxt], si
    mov [cy_pnw], ax
    mov [cy_pnh], bx
    mov [cy_pnln], cl
    mov byte [cy_pnon], 1
    mov byte [cy_pndirty], 1
    cmp byte [cy_state], CYS_PLAY
    jne .draw
    mov byte [cy_state], CYS_PAUSE
    mov byte [cy_wasplay], 1
.draw:
    mov byte [cy_full], 1           ; the panel covers the playfield, so what
    call cy_full_repaint            ; takes it down owes a whole one anyway
    pop si
    ret

; cy_pn_off - clear the panel state and put a game the panel paused back, and
; draw NOTHING. For a caller that is about to repaint the world anyway.
; out: CF = 1 a panel was up. Preserves everything else.
cy_pn_off:
    cmp byte [cy_pnon], 0
    je .none
    mov byte [cy_pnon], 0
    cmp byte [cy_wasplay], 0
    je .yes
    mov byte [cy_wasplay], 0
    mov byte [cy_state], CYS_PLAY   ; the panel paused it; taking it down
.yes:                               ; puts it back, which is not a resume
    stc                             ; the player asked for separately
    ret
.none:
    clc
    ret

; cy_pn_dismiss - take the panel down if it is up, and repaint over it.
; out: CF = 1 the key or click was spent doing it. Preserves everything.
cy_pn_dismiss:
    call cy_pn_off
    jnc .none
    push si
    mov byte [cy_full], 1
    call cy_full_repaint
    pop si
    stc
    ret
.none:
    clc
    ret

; cy_panel_draw - the panel itself, drawn last and over everything.
cy_panel_draw:
    cmp byte [cy_pnon], 0
    je .out
    cmp byte [cy_pndirty], 0
    je .out                         ; already on the glass, and nothing under
    mov byte [cy_pndirty], 0        ; it has drawn since
    mov ax, [cy_cwid]               ; a centred box, sized to the widest line
    sub ax, [cy_pnw]
    jns .x
    xor ax, ax
.x:
    shr ax, 1
    and al, 0xF8                    ; the pen on a multiple of 8 earns
    mov [cy_pnx], ax                ; font_run's single-store path (SPEC.md 6.1)
    mov ax, [cy_chgt]
    sub ax, [cy_pnh]
    jns .y
    xor ax, ax
.y:
    shr ax, 1
    mov [cy_pny], ax

    mov al, CY_C_BG                 ; a bed, then a frame, then the lines: the
    call cy_setcol                  ; run is opaque so there is no second fill
    call cy_pn_rect
    call cy_fillc
    mov al, CY_C_TEXT               ; SOLID, not the web's dither: a 1px frame
    call cy_setcol                  ; in the dither class is a dotted line on
    call cy_pn_rect                 ; both mono adapters (SPEC.md 39.4)
    call cy_framec

    mov si, [cy_pntxt]
    mov byte [cy_pnrow], 0          ; the row INDEX, in bss - the pen used to
.line:                              ; be carried in BX across a far call into
    cmp byte [si], 0xFF             ; the kernel, which is a register this
    je .done                        ; loop cannot afford to be wrong about
    push si
    mov al, [cy_pnrow]
    mov ah, 0
    mov bx, ax
    shl ax, 1                       ; y = pny + 6 + 9*line
    shl ax, 1
    shl ax, 1
    add ax, bx
    add ax, [cy_pny]
    add ax, 6
    mov dx, ax
    mov cx, [cy_pnx]
    add cx, 8
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc
    pop si
    call cy_strskip
    inc byte [cy_pnrow]
    mov al, [cy_pnrow]
    cmp al, [cy_pnln]
    jb .line
.done:
    ; the panel is not a mover and nothing repairs what it covered, so the
    ; blocks must stop claiming pixels it took (Missile Command's rule)
    call cy_obj_forget
.out:
    ret

; cy_pn_rect - the banked box as (AX,BX)-(CX,DX), inclusive. The bed and the
; frame are the same rectangle and derived once, because two derivations of one
; rect are two chances for the frame to sit a pixel off the fill it lines.
cy_pn_rect:
    mov ax, [cy_pnx]
    mov bx, [cy_pny]
    mov cx, ax
    add cx, [cy_pnw]
    dec cx
    mov dx, bx
    add dx, [cy_pnh]
    dec dx
    ret

; cy_strskip - SI to just past the NUL.
cy_strskip:
    push ax
.n:
    lodsb
    or al, al
    jnz .n
    pop ax
    ret

; =============================================================================
; cy_onresize - the content box changed underneath us (SPEC.md 39.11)
;
; in:  SI = our window, CX/DX = the new content size. UI task, gfx lock HELD.
; out: nothing. WE MUST NOT DRAW HERE - so it marks and the worker relays out
;      on its next frame, which is also where the web has to be redrawn from.
; =============================================================================
cy_onresize:
    call cy_pal                     ; the box changed because the DISPLAY did
    mov byte [cy_needlay], 1        ; (SPEC.md 39.11), so the inks may have too
    ret

; =============================================================================
; cy_paint - W_PAINT. Content only; the gfx lock is held.
;
; Also where the worker is hired, because OSAPI_TASK_SPAWN requires the lock
; and refuses from the entry proc. The refusal is transient (the 12-slot task
; table may be full), so this retries on every paint rather than giving up.
; =============================================================================
cy_paint:
    push si
    mov bx, [cy_win]
    call cy_hs_load                 ; a UI-task callback, which the entry proc
    call cy_hire                    ; is not a safe place to read a file from
    call cy_full_repaint
    pop si
    ret

cy_hire:
    cmp byte [cy_hired], 0
    jne .out
    push ax
    push bx
    mov ax, cy_worker
    mov bx, [cy_win]
    call OSAPI_TASK_SPAWN
    jc .nope                        ; transient - try again next paint
    mov byte [cy_hired], 1
.nope:
    pop bx
    pop ax
.out:
    ret

; =============================================================================
; cy_onkey - W_ONKEY. AL = ascii, AH = scan, SI = our window.
;
; Typed keys only. The arrows a player HOLDS are read from the key-state map
; in the worker (SPEC.md 9.7) rather than from here, because int 16h has no
; key-up and the typematic gap is visible as a stutter - SPEC.md 44.2 is the
; worked example and Arkanoid is the precedent.
; =============================================================================
cy_onkey:
    push si
    call cy_pn_dismiss              ; any key takes a panel down, and is spent
    jc .spent                       ; doing it
    call cy_key_common
.spent:
    call cy_kbdrain                 ; the UI task takes ONE key per pass, so
    pop si                          ; this is the second drain point and the
    ret                             ; one whose rate tracks the arrivals

; The body, shared with the fullscreen-exclusive loop's own polling so that
; every key does the same thing in both worlds (Paint's rule, SPEC.md 42.7).
; in: AL = ascii, AH = scan. Assumes the gfx lock is held.
cy_key_common:
    call cy_init_key                ; FIRST: while initials are being typed the
    jc .taken                       ; letters are not the game's, and neither
    cmp ah, KSC_ESC                 ; is Enter
    je .esc
    or al, al
    jz .noascii                     ; AN ARROW HAS AL = 0, and `or al, 0x20`
                                    ; turns 0 into 0x20 - which is SPACE. So
                                    ; every arrow keypress fired, and holding
                                    ; one fired continuously. Test AL before
                                    ; folding anything, and let the arrows
                                    ; reach the key-state map (SPEC.md 9.7)
                                    ; where they belong.
    cmp al, ' '
    je .fire
    cmp al, 13
    je .start
    or al, 0x20                     ; fold case: a bare letter must work under
    cmp al, 'f'                     ; Caps Lock (SPEC.md 11.2.1)
    je .fs
    cmp al, 'p'
    je .pause
    cmp al, 'n'
    je .new
    cmp al, 'z'
    je .zap
    cmp al, 'j'
    je .jump
    ret
.noascii:
    cmp ah, KSC_ENTER
    je .start
.taken:
    ret

.esc:
    ; Esc is the escape hatch out of fullscreen and nothing else - in a
    ; window there is a close box, and quitting a game on a stray Esc is the
    ; worst available outcome.
    cmp byte [cy_fsx], 0
    je .ret0
    mov byte [cy_fsxq], 1           ; the bracket's own loop reads this
    ret
.fs:
    cmp byte [cy_fsx], 0
    je .fs_in
    mov byte [cy_fsxq], 1           ; F leaves as well as enters (11.2.1)
    ret
.fs_in:
    call cy_go_fsx
    ret
.pause:
    cmp byte [cy_state], CYS_PLAY
    jne .p2
    mov byte [cy_state], CYS_PAUSE
    mov byte [cy_msgdirty], 1
    ret
.p2:
    cmp byte [cy_state], CYS_PAUSE
    jne .ret0
    mov byte [cy_state], CYS_PLAY
    mov byte [cy_full], 1
    ret
.new:
    call cy_newgame
    ret
.start:
    cmp byte [cy_state], CYS_TITLE
    jne .ret0
    call cy_newgame
    ret
.zap:
    call cy_superzap
    ret
.jump:
    call cy_do_jump
    ret
.fire:
    mov byte [cy_firereq], 1
    ret
.ret0:
    ret

; =============================================================================
; cy_onclick - W_ONCLICK. CX/DX = screen x/y, SI = our window.
;
; A click fires. Aiming is by lane and the mouse picks one: the pointer's
; angle about the tube's centre is the lane the player wants, which is the
; trackball of the original in the only form this machine has.
; =============================================================================
cy_onclick:
    push si
    call cy_pn_dismiss
    jc .out
    mov byte [cy_firereq], 1
    cmp byte [cy_state], CYS_TITLE
    jne .aim
    call cy_newgame
    jmp .out
.aim:
    call cy_aim_mouse
.out:
    pop si
    ret

; =============================================================================
; cy_oncmd - the menu handler. AL = item, AH = menu, SI = our window.
; UI task, gfx lock held, billed to our instance.
; =============================================================================
cy_oncmd:
    push si
    or ah, ah                       ; BOTH indices are tested, menu and item.
    jnz .helpm                      ; A menu with one item today is a menu with
    cmp al, 0                       ; two tomorrow, and the second one then
    je .new                         ; dispatches as the first with nothing
    cmp al, 1                       ; saying so (tamegram's trap)
    je .pause
    cmp al, 2
    je .fs
    jmp short .out
.helpm:
    cmp ah, 1
    jne .out
    or al, al
    jz .how
    jmp short .out
.new:
    call cy_newgame
    jmp short .out
.pause:
    mov al, 'p'
    mov ah, 0
    call cy_key_common
    jmp short .out
.fs:
    call cy_go_fsx
    jmp short .out
.how:
    ; It used to be `cy_state = CYS_TITLE` plus a repaint, which is wrong in
    ; both directions: from the attract screen - where a player who has just
    ; opened the window IS - it drew the same screen again and the item did
    ; nothing at all, and from a live game it threw the game away to show one.
    call cy_help
.out:
    pop si
    ret

; =============================================================================
; GEOMETRY (SPEC.md 67.2)
;
; cy_layout - rebuild every derived coordinate from the live content box.
;
; Called whenever the box moved or resized (the extended-desktop case), on a
; level change, and on entry to and exit from the fullscreen bracket. It fills
; cy_vx/cy_vy - the screen position of every vertex at every depth - and after
; it has run NOTHING in the drawing path does any perspective arithmetic: a
; mover's position is two table reads and an average.
;
; in:  gfx lock held (it reads the window record).
; out: CF=1 the box is too small to hold a readable web; the caller says so
;      and draws no playfield. CF=0 and the tables are current.
; =============================================================================
cy_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call cy_org                     ; AX/DX = content origin, CX/BX = size
    mov [cy_ox], ax
    mov [cy_oy], dx
    mov [cy_cwid], cx
    mov [cy_chgt], bx

    ; The playfield is the content less the HUD strip along the top - and,
    ; on the attract screen, less the prompt line and the high-score band as
    ; well, so the web and the scroll never share a pixel and neither has to
    ; repair the other.
    sub bx, CY_HUD_H
    jns .ph
    xor bx, bx
.ph:
    cmp byte [cy_state], CYS_TITLE
    jne .noscr
    sub bx, CY_SCRH + 10
    jns .noscr
    xor bx, bx
.noscr:
    mov [cy_pfh], bx
    mov ax, bx
    add ax, CY_HUD_H + 10
    mov [cy_scry0], ax

    ; Radius: half the smaller side, less a margin. The web is a polygon in
    ; -128..127 so 128 units map onto R.
    mov ax, cx
    cmp ax, bx
    jbe .minok
    mov ax, bx
.minok:
    shr ax, 1
    sub ax, CY_MARGIN
    jns .rok
    xor ax, ax
.rok:
    ; cy_rad is the radius AT THE RIM (scale 256), and the ladder now runs
    ; past it to CY_TOPSCALE - so the space that has to fit is the lip, not
    ; the rim. Scaling here rather than at every read keeps the whole drawing
    ; path ignorant of it.
    cmp ax, 255
    jbe .rfit
    mov ax, 255
.rfit:
    mov cl, 8
    shl ax, cl                      ; * 256, and <= 65280 by the clamp above
    xor dx, dx
    mov cx, CY_TOPSCALE
    div cx
    mov [cy_rad], ax
    cmp ax, CY_MINR
    jb .toosmall

    ; The vanishing point, in CONTENT coordinates - which is the space the
    ; whole app works in, because cy_fillc is what adds the origin. The one
    ; exception is cy_walk_one: OSAPI_GFX_LINIT takes ABSOLUTE screen
    ; coordinates, so it adds the origin itself.
    mov ax, [cy_cwid]
    shr ax, 1
    mov [cy_cx], ax
    mov ax, [cy_pfh]
    shr ax, 1
    add ax, CY_HUD_H
    mov [cy_cy], ax

    call cy_build_verts
    mov byte [cy_needlay], 0
    clc
    jmp .out
.toosmall:
    mov word [cy_nlane], 0
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_build_verts -----------------------------------------------------------
; Fill cy_vx/cy_vy for the current shape.
;
; Two passes rather than one, and that is the whole performance argument here:
; scaling each normalised vertex to the radius ONCE (nverts multiplies) and
; then walking the depth ladder against that (nverts x ndepth multiplies)
; costs half what doing both multiplies per cell would, and this runs on every
; window move on a 4.77MHz machine.
; -----------------------------------------------------------------------------
cy_build_verts:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                         ; BP is the dispatcher's register and it
                                    ; addresses SS - it is borrowed here as a
                                    ; plain scalar and must go back

    mov si, [cy_shape]              ; the descriptor
    mov al, [si]
    mov ah, 0
    cmp ax, CY_MAXLANE              ; CLAMPED, not trusted. The tables are
    jbe .nl                         ; CY_MAXV wide and cy_build_verts writes
    mov ax, CY_MAXLANE              ; one entry per vertex per depth; a
.nl:                                ; descriptor claiming more does not draw
    or ax, ax                       ; a wrong web, it writes through the end
    jnz .nl2                        ; of them
    mov ax, 3
.nl2:
    mov [cy_nlane], ax
    mov bl, [si + 1]
    mov bh, 0
    mov [cy_closed], bx
    add si, 2                       ; -> the vertex pairs

    mov cx, [cy_nlane]              ; nverts = nlanes (+1 when open)
    cmp bx, 0
    jne .nv
    inc cx
.nv:
    mov [cy_nvert], cx

    ; --- pass 1: normalised (-128..127) -> pixels at the rim ----------------
    mov di, cy_rsx
    push cx
.p1:
    mov al, [si]                    ; x
    cbw
    imul word [cy_rad]              ; DX:AX = x * R
    ; >> 7, because the normalised space is 128 units to the radius. The high
    ; word is live (R can be 200, x can be 100), so shift the pair.
    mov bx, dx
    mov dx, ax
    mov ax, bx                      ; AX = high, DX = low
    ; a 15-bit arithmetic shift right of DX:AX by 7 == (<<1 then take AH:DL)
    shl dx, 1
    rcl ax, 1
    mov dl, dh
    mov dh, al
    mov [di], dx
    add di, 2

    mov al, [si + 1]                ; y
    cbw
    imul word [cy_rad]
    mov bx, dx
    mov dx, ax
    mov ax, bx
    shl dx, 1
    rcl ax, 1
    mov dl, dh
    mov dh, al
    mov [di], dx
    add di, 2

    add si, 2
    loop .p1
    pop cx

    ; --- pass 2: the depth ladder -------------------------------------------
    ; vx[d][v] = cx + (rsx[v] * scale[d]) >> 8
    mov word [cy_dd], 0
.p2d:
    mov bx, [cy_dd]
    shl bx, 1
    mov bp, [cy_scale + bx]         ; the scale for this depth

    mov bx, [cy_dd]                 ; row base = d * CY_MAXV * 2
    mov ax, CY_MAXV * 2
    mul bx
    mov di, ax                      ; DI = byte offset of row d
    mov si, cy_rsx
    mov cx, [cy_nvert]
.p2v:
    mov ax, [si]                    ; rsx
    imul bp
    mov al, ah                      ; (DX:AX) >> 8
    mov ah, dl
    add ax, [cy_cx]
    mov [cy_vx + di], ax

    mov ax, [si + 2]                ; rsy
    imul bp
    mov al, ah
    mov ah, dl
    add ax, [cy_cy]
    mov [cy_vy + di], ax

    add si, 4
    add di, 2
    loop .p2v

    inc word [cy_dd]
    cmp word [cy_dd], CY_NDEPTH
    jb .p2d

    call cy_lip_build               ; ...and then the lip, which is the one
                                    ; row that is NOT a scale of the rim
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE LIP HAS TO CLEAR THE RIM, AND A RADIAL SCALE DOES NOT DO IT (67.5.3.1)
;
; 67.5.3 puts the claw and every fully ascended enemy at CY_TOPD - a depth
; OUTSIDE the rim - so that nothing the player does happens among the spokes.
; 67.1's whole scheme rests on that: a mover's erase is one gfx_fill in the
; background colour, so a mover overlapping the web takes the web's pixels
; with it and nothing puts them back.
;
; CY_TOPD IS A SCALE ABOUT THE VANISHING POINT, and a scale separates nothing
; where the rim edge runs radially. It is right for the circle and the square,
; whose edges are roughly perpendicular to the radius, and it fails on:
;
;   - the FLAT ribbon, where every vertex has y = 0, so scaling slides the lip
;     ALONG the rim line: the claw is drawn ON the web at every window size and
;     on every adapter, and eats it on every lane it crosses;
;   - the STAR's inner vertices and the PLUS's inner corners, which sit at 0.44
;     and 0.47 of the radius, so 296/256 of that is a few pixels and the
;     NEIGHBOURING edge is inside the claw's own rect;
;   - the VEE and the TRIANGLE near their ends, the same thing in miniature.
;
; Measured with tests/cycweb.py - one sweep of the claw over every lane with
; 67.19's repair stubbed, so what is counted is what the claw ate rather than
; how fast the repair paints it back: 256 web pixels over the eight shapes,
; 85 of them on the star and 48 on the plus.
;
; So a lane's lip is pushed along its rim edge's own OUTWARD NORMAL - and only
; where it has to be, the scaled position being kept wherever it already
; clears. That is what keeps this a repair rather than a redesign: on a
; full-screen VGA only the star (<=3px) and the flat ribbon (<=7px) move at
; all, the square never moves at any size, and the circle and the horseshoe
; move only in a small window and only by 1-3px.
;
; What it clears by is the rect's OWN support along that normal, which is
; (|ey|*hw + |ex|*hh) / |e| - what an axis-aligned rect actually reaches in
; that direction - plus two pixels of air, one for the web line and one for
; the WALK (see below). The half-extents come from cy_lanecap, so a narrow
; lane asks for less rather than for more.
;
; It is a TABLE and not a formula at the call site because it is layout-time
; work: three or four divides a lane, sixteen lanes, once per window change,
; against a claw drawn eighteen times a second with an arrival beside it.
; =============================================================================
CY_LIPEXT   equ 5                   ; the widest half-extent anything at the
                                    ; lip is drawn with: the claw's own 5 (the
                                    ; ramp at CY_TOPD is 21/20, so 5 stays 5),
                                    ; and no kind in cy_ekext is wider
CY_LIPKMAX  equ 12                  ; ...so a push past twice that is a shape
                                    ; nothing can clear, and walking the claw
                                    ; off the playfield is not the better of
                                    ; the two wrong answers

; cy_lip_build - fill cy_lipx/cy_lipy for the current shape. Layout-time only,
; and called from cy_build_verts because it reads the rows that builds.
cy_lip_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si                      ; SI = the lane, the one loop counter
.each:
    cmp si, [cy_nlane]
    jae .out
    mov di, si
    shl di, 1                       ; DI = its slot in the two tables

    ; --- the rim edge this lane is drawn against ---------------------------
    mov ax, CY_DEPTH
    mov bx, si
    call cy_vert
    mov [cy_lptx], cx
    mov [cy_lpty], dx
    mov ax, si
    call cy_web_next
    mov [cy_lpv2], ax
    mov bx, ax
    mov ax, CY_DEPTH
    call cy_vert
    mov ax, cx
    sub ax, [cy_lptx]               ; e = B - A
    mov [cy_lpex], ax
    mov ax, dx
    sub ax, [cy_lpty]
    mov [cy_lpey], ax
    add cx, [cy_lptx]               ; ...and the edge's midpoint, which is the
    sar cx, 1                       ; point on the line the lip is measured
    mov [cy_lpmx], cx               ; from
    add dx, [cy_lpty]
    sar dx, 1
    mov [cy_lpmy], dx

    ; --- where the scaled lip put it, and how wide the lane is there -------
    mov ax, CY_TOPD
    mov bx, si
    call cy_vert
    mov [cy_lptx], cx
    mov [cy_lpty], dx
    mov bx, [cy_lpv2]
    mov ax, CY_TOPD
    call cy_vert
    mov ax, cx
    sub ax, [cy_lptx]
    jns .sx
    neg ax
.sx:
    mov bx, dx
    sub bx, [cy_lpty]
    jns .sy
    neg bx
.sy:
    cmp ax, bx                      ; the dominant span, which is what
    jae .sm                         ; cy_lanecap bounds a mover by
    mov ax, bx
.sm:
    mov [cy_lspm], ax
    add cx, [cy_lptx]
    sar cx, 1
    mov [cy_lipx + di], cx          ; the scaled position IS the answer until
    add dx, [cy_lpty]               ; something below says otherwise
    sar dx, 1
    mov [cy_lipy + di], dx
    sub cx, [cy_lpmx]               ; d = lip - rim: the lean it already has
    mov [cy_lpdx], cx
    sub dx, [cy_lpmy]
    mov [cy_lpdy], dx

    ; --- |e|, near enough: max + min/2, which is up to 12% LONG ------------
    mov ax, [cy_lpex]
    or ax, ax
    jns .ax
    neg ax
.ax:
    mov bx, [cy_lpey]
    or bx, bx
    jns .ay
    neg bx
.ay:
    mov [cy_lpax], ax               ; |ex| and |ey|, both wanted again below
    mov [cy_lpay], bx
    cmp ax, bx
    jae .lmax
    xchg ax, bx
.lmax:
    shr bx, 1
    add ax, bx
    jnz .lok
    inc ax                          ; a zero-length edge cannot happen; a
.lok:                               ; divide by zero only has to happen once
    mov [cy_lpl], ax

    ; --- what the lane NEEDS: the rect's support along that normal ---------
    mov ax, CY_LIPEXT
    call cy_lanecap                 ; ...at THIS lane's width, not the ramp's
    mov bx, [cy_lpax]
    add bx, [cy_lpay]
    mul bx                          ; cap * (|ex| + |ey|) - all of it scaled
    add ax, [cy_lpl]                ; by |e|, plus TWO pixels of air: one is
    add ax, [cy_lpl]                ; the web line's own pixel and the second
    mov [cy_lpn], ax                ; is the WALK's, because SPEC.md 5.6.7
                                    ; lays a line where Bresenham puts it and
                                    ; not where the ideal one runs. One pixel
                                    ; was measured and was one short: the vee
                                    ; kept a single pixel of its own rim edge
                                    ; inside the corner of the claw's rect,
                                    ; where the ideal line misses that corner
                                    ; by 0.7 of one

    ; --- what it HAS, and the push if that is not enough -------------------
    ; THE TEST IS EXACT AND THE ESTIMATE IS NOT, which is the whole shape of
    ; what follows. |e| is max + min/2 - within 12% and always long - so a
    ; push computed from it lands up to a tenth short, and the two roundings
    ; into whole pixels take more; measured, the first version left the star
    ; still eating 4 pixels of its own edge on three lanes. So the distance is
    ; re-measured from the CROSS PRODUCT of the offset actually applied, and
    ; the push grows a pixel at a time until that says it clears. It converges
    ; in one or two passes, it is bounded by CY_LIPKMAX, and it needs no
    ; square root anywhere.
    mov word [cy_lpox], 0
    mov word [cy_lpoy], 0
    call cy_lp_cross                ; CX = the distance the scale already has
    cmp cx, [cy_lpn]
    jae .next                       ; ...and it clears this edge: leave it
    mov al, [cy_lpdir]              ; the sign of THIS cross is the direction
    mov [cy_lpdir0], al             ; to push in, and the loop overwrites it

    mov ax, [cy_lpn]                ; k = ceil((need - dist) / |e|), at least 1
    sub ax, cx
    add ax, [cy_lpl]
    dec ax
    xor dx, dx
    div word [cy_lpl]
    or ax, ax
    jnz .kok
    mov ax, 1
.kok:
    mov [cy_lpk], ax
.try:
    mov cx, [cy_lpk]
    mov ax, [cy_lpey]               ; the normal is (ey, -ex)...
    call cy_lp_scale
    mov [cy_lpox], ax
    mov cx, [cy_lpk]
    mov ax, [cy_lpex]
    neg ax
    call cy_lp_scale
    mov [cy_lpoy], ax
    cmp byte [cy_lpdir0], 1         ; ...whose sign is the cross product's:
    je .test                        ; negative and it already leans the way
    cmp byte [cy_lpdir0], 2         ; the scaled lip does, positive and it is
    je .flip                        ; the other one
    ; A FLAT WEB LEANS NOWHERE. Every vertex has y = 0, so the lip is ON the
    ; rim line and the cross product is zero: up is as good an answer as down,
    ; and it is the one that puts the claw between the HUD and the tube rather
    ; than underneath it.
    cmp word [cy_lpoy], 0
    jl .test
    jg .flip
    cmp word [cy_lpox], 0           ; ...and a vertical ribbon, to the right
    jg .test
.flip:
    neg word [cy_lpox]
    neg word [cy_lpoy]
.test:
    call cy_lp_cross                ; the distance with THIS offset on it
    cmp cx, [cy_lpn]
    jae .apply
    inc word [cy_lpk]
    cmp word [cy_lpk], CY_LIPKMAX
    jbe .try                        ; ...and a shape nothing clears keeps the
.apply:                             ; last one rather than walking away
    mov ax, [cy_lpox]
    add [cy_lipx + di], ax
    mov ax, [cy_lpoy]
    add [cy_lipy + di], ax
.next:
    inc si
    jmp .each
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_lp_cross - the perpendicular distance from the lip to the rim edge's
; LINE, times |e| so that nothing has to be divided to compare it with
; cy_lpn. In 32 bits, because a lip that is miles clear must not wrap into
; one that reads as too close.
; out: CX = |ex*(dy+oy) - ey*(dx+ox)|, 0FFFFh when that will not fit a word;
;      [cy_lpdir] = 0 on the line, 1 negative, 2 positive. Preserves the rest.
cy_lp_cross:
    push ax
    push bx
    push dx
    mov ax, [cy_lpdy]
    add ax, [cy_lpoy]
    imul word [cy_lpex]
    mov cx, ax
    mov bx, dx
    mov ax, [cy_lpdx]
    add ax, [cy_lpox]
    imul word [cy_lpey]
    sub cx, ax
    sbb bx, dx
    mov byte [cy_lpdir], 2
    or bx, bx
    js .neg
    jnz .big
    or cx, cx
    jnz .out
    mov byte [cy_lpdir], 0          ; ON the line, and leaning nowhere
    jmp short .out
.neg:
    mov byte [cy_lpdir], 1
    cmp bx, -1
    jne .big
    neg cx
    jnz .out
.big:
    mov cx, 0xFFFF
.out:
    pop dx
    pop bx
    pop ax
    ret

; cy_lp_scale - AX = (AX * CX) / [cy_lpl], signed, rounded to nearest.
cy_lp_scale:
    push bx
    push cx
    push dx
    push si
    xor si, si
    or ax, ax
    jns .pos
    neg ax
    inc si
.pos:
    mul cx
    mov bx, [cy_lpl]
    shr bx, 1
    add ax, bx
    adc dx, 0
    div word [cy_lpl]
    or si, si
    jz .out
    neg ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- cy_vert ------------------------------------------------------------------
; The screen position of vertex BX at depth AX.
; in:  AX = depth 0..CY_DEPTH, BX = vertex index (already wrapped)
; out: CX = x, DX = y. Clobbers nothing else.
; -----------------------------------------------------------------------------
cy_vert:
    push ax
    push bx
    push si
    mov si, CY_MAXV * 2
    mul si                          ; AX = row offset (depth fits a byte, so
    shl bx, 1                       ; the high word is 0 and DX is scratch)
    add bx, ax
    mov cx, [cy_vx + bx]
    mov dx, [cy_vy + bx]
    pop si
    pop bx
    pop ax
    ret

; --- cy_wrap ------------------------------------------------------------------
; Wrap a lane index into range for the current web.
; in:  AX = a lane index, possibly -1 or nlanes
; out: AX = the lane actually stepped to. For an OPEN web this clamps (the
;      player hits a wall); for a closed one it wraps.
; -----------------------------------------------------------------------------
cy_wrap:
    cmp word [cy_closed], 0
    je .open
    or ax, ax
    jns .hi
    mov ax, [cy_nlane]
    dec ax
    ret
.hi:
    cmp ax, [cy_nlane]
    jb .out
    xor ax, ax
    ret
.open:
    or ax, ax
    jns .ohi
    xor ax, ax
    ret
.ohi:
    cmp ax, [cy_nlane]
    jb .out
    mov ax, [cy_nlane]
    dec ax
.out:
    ret

; cy_dmap - AH = the depth a mover IS at -> AH = the depth to DRAW it at.
; Preserves everything else.
cy_dmap:
    push bx
    mov bl, ah
    mov bh, 0
    cmp bx, CY_NDEPTH
    jb .ok
    mov bx, CY_TOPD
.ok:
    mov ah, [cy_dtab + bx]
    pop bx
    ret

; --- cy_lanepos ---------------------------------------------------------------
; The centre of lane AL at depth AH - where a mover on that lane sits.
;
; This is the routine the whole drawing path leans on, and it is deliberately
; two table reads and an average: all of the perspective was spent once in
; cy_layout.
;
; in:  AL = lane, AH = depth
; out: CX = x, DX = y. Preserves everything else.
; -----------------------------------------------------------------------------
cy_lanepos:
    push ax
    push bx
    push si
    push di

    mov bl, al                      ; lane
    mov bh, 0
    mov al, ah                      ; depth
    mov ah, 0
    mov si, CY_MAXV * 2
    mul si
    mov di, ax                      ; DI = row offset

    mov ax, bx                      ; the far vertex of the lane
    shl ax, 1
    add ax, di
    mov si, ax
    mov cx, [cy_vx + si]
    mov dx, [cy_vy + si]

    mov ax, bx                      ; ...and the next one round
    inc ax
    cmp word [cy_closed], 0
    je .nowrap
    cmp ax, [cy_nlane]
    jb .nowrap
    xor ax, ax
.nowrap:
    shl ax, 1
    add ax, di
    mov si, ax
    ; ...and publish the lane's SPAN as well as its centre, because that is
    ; what bounds a mover (cy_setrect). Both vertices are already in hand
    ; here; asking for them again anywhere else would be a second opinion
    ; about how wide a lane is.
    push ax
    mov ax, [cy_vx + si]
    sub ax, cx
    jns .sx
    neg ax
.sx:
    mov [cy_lspx], ax
    mov ax, [cy_vy + si]
    sub ax, dx
    jns .sy
    neg ax
.sy:
    mov [cy_lspy], ax
    cmp ax, [cy_lspx]               ; ...and the dominant of the two, which is
    ja .sm                          ; what cy_lanecap bounds BOTH axes by
    mov ax, [cy_lspx]
.sm:
    mov [cy_lspm], ax
    pop ax

    add cx, [cy_vx + si]
    add dx, [cy_vy + si]
    sar cx, 1
    sar dx, 1

    ; THE LIP IS NOT THE RING (67.5.3.1). Every other depth is a scale about
    ; the vanishing point and its midpoint is the answer; CY_TOPD's is where
    ; the claw and the arrivals are DRAWN, and cy_lip_build has already moved
    ; it clear of the rim edge on the webs a scale does not clear. The SPAN
    ; above is still the ring's, because that is the lane a mover is bounded
    ; by and the push does not widen it.
    ; DI is still the row offset and BX is still the lane, so the test needs
    ; NO scratch word - which matters, because cy_bottom_lane calls this from
    ; the worker's update phase with no gfx lock held and a UI callback can
    ; be inside it at the same time.
    cmp di, CY_TOPD * CY_MAXV * 2
    jne .out
    cmp bx, [cy_nlane]
    jae .out
    shl bx, 1
    mov cx, [cy_lipx + bx]
    mov dx, [cy_lipy + bx]
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; --- cy_org -------------------------------------------------------------------
; Where our content box is and how big it is - the ONE place that answers it,
; so the fullscreen-exclusive case is a branch here rather than at forty call
; sites (Paint's `pt_org`, SPEC.md 42.7).
;
; out: AX = content left, DX = content top, CX = width, BX = height
; -----------------------------------------------------------------------------
cy_org:
    cmp byte [cy_fsx], 0
    jne .fs
    push si
    mov bx, [cy_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    push ax
    push dx
    mov bx, [cy_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    mov bx, dx
    pop dx
    pop ax
    pop si
    ret
.fs:
    mov ax, [cy_scrx]               ; **NOT (0,0)** - a same-mode bracket does
    mov dx, [cy_scry]               ; not collapse a two-display desktop
    mov cx, [cy_scrw]               ; (SPEC.md 39.18.3), so the coordinates are
    mov bx, [cy_scrh]               ; still the whole virtual desktop's and
    ret                             ; (0,0) is the PRIMARY. cy_fsx_main asks
                                    ; OSAPI_FSX_SURF for these four

; =============================================================================
; THE CONTENT-COORDINATE PRIMITIVES (SPEC.md 67.3)
;
; Everything above measures from the content origin; only these add it, and
; only these CLAMP. The clamp is not decoration: `gfx_fill` clips to the
; SCREEN and not to our window, so a mover erased at the edge of a small
; window would otherwise paint over the desktop - or, inside a W_PAINT with
; no clip armed, over another window entirely. Missile Command's `mc_fillc`
; is the precedent and the reason.
; =============================================================================

; cy_setcol - every pen change goes through here, because [gfx_color] cannot
; be read back and several paths want to know what the pen currently is.
;
; THE CACHE IS ONE LOCK HOLD LONG, and cy_pen_forget is what says so. The pen
; is ONE GLOBAL shared by the whole machine (SPEC.md 5): between our lock
; holds the kernel has drawn window chrome, the menu bar and the dock with it,
; so what it holds when we come back is not what we left. Cached across the
; gap, the first cy_setcol of a frame compares equal to a pen we no longer own
; and skips the write.
;
; That is not a hypothetical. [cy_col] is 0 out of bss and CY_C_BG is CBLACK,
; which is also 0 - so the very first thing this app ever drew, cy_draw_all's
; background fill, was skipped and the content was filled in whatever the
; kernel had left. On CGA and Hercules that was black often enough to look
; right; on VGA it was WHITE, and the whole game drew on a white field.
; in: AL = colour. Clobbers nothing.
cy_setcol:
    cmp al, [cy_col]
    je .out                         ; the pen is already this: a far call saved
    mov [cy_col], al
    call OSAPI_SET_COLOR
.out:
    ret

; cy_pen_forget - a lock hold is beginning and the pen is not ours. 0FFh is
; not a colour, so the next cy_setcol always writes.
cy_pen_forget:
    mov byte [cy_col], 0xFF
    ret

; cy_clamp - cut (AX,BX)-(CX,DX) to the content box.
; out: CF=1 nothing is left of it.
cy_clamp:
    or ax, ax
    jns .x1
    xor ax, ax
.x1:
    or bx, bx
    jns .y1
    xor bx, bx
.y1:
    push si
    mov si, [cy_cwid]
    dec si
    cmp cx, si
    jle .x2
    mov cx, si
.x2:
    mov si, [cy_chgt]
    dec si
    cmp dx, si
    jle .y2
    mov dx, si
.y2:
    pop si
    cmp ax, cx
    jg .empty
    cmp bx, dx
    jg .empty
    clc
    ret
.empty:
    stc
    ret

; cy_fillx - a filled rect in content coordinates, preserving SI/DI and
; NOTHING else. This is the drawing spine's leaf (SPEC.md 67.5.5): every one
; of its callers reloads all four corners from bss on the next line, and the
; eight bytes cy_fillc spends banking them are eight bytes of a 256-byte
; worker stack held for the whole of the kernel's gfx_fill underneath.
; in: AX/BX/CX/DX = x1/y1/x2/y2 inclusive.
cy_fillx:
    call cy_clamp
    jc .out
    add ax, [cy_ox]
    add cx, [cy_ox]
    add bx, [cy_oy]
    add dx, [cy_oy]
%ifdef CYTRACE
    call cy_trace
%endif
    call OSAPI_GFX_FILL             ; NOT a `jmp`, tempting as one is here:
                                    ; an OSAPI cell is a FAR entry ending in
                                    ; retf, so a tail jump would pop this
                                    ; routine's own near return address as
                                    ; CS:IP - the kernel's own .cold-shim
                                    ; trap (SPEC.md 2.6) seen from a package
.out:
    ret

; cy_fillc - the same thing, preserving everything, for the call sites that
; are not on the spine and do want their rect back.
cy_fillc:
    push ax
    push bx
    push cx
    push dx
    call cy_fillx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_framec - a 1px outline in content coordinates.
cy_framec:
    push ax
    push bx
    push cx
    push dx
    call cy_clamp
    jc .out
    add ax, [cy_ox]
    add cx, [cy_ox]
    add bx, [cy_oy]
    add dx, [cy_oy]
    call OSAPI_GFX_FRAME
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_textc - an OPAQUE run in content coordinates (SPEC.md 6.1).
;
; Opaque and never a fill-then-letter pair, for the two reasons SPEC.md 6.1
; gives: the pair leaves the run blank between the fill and the last glyph,
; which on the target machine is tens of milliseconds of visible flash, and
; the pair clips at two different granularities so a clip edge crossing it
; blanks the line rather than leaving it stale.
; in: CX = x, DX = y (content), SI = string, AL = ink, AH = background
cy_textc:
    push cx
    push dx
    add cx, [cy_ox]
    add dx, [cy_oy]
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    ret

; --- cy_rsub ------------------------------------------------------------------
; Erase the part of the OLD rect that the NEW rect does not cover.
;
; SPEC.md 7.1.2's rule, in Arkanoid's `ark_rsub` shape: the new rect is drawn
; FIRST and this gives back only what is left over, so nothing is ever absent
; and no pixel is written twice. Erase-then-draw would leave every shared
; pixel dark for the length of the gap, which is PERFORMANCE.md's double-draw
; flash - about 15ms on the target machine and plainly visible.
;
; A move that clears its old place entirely degenerates to one whole-rect
; erase by itself, so there is no gate on how far the thing moved.
;
; in:  cy_sol..cy_sob = the old rect, cy_snl..cy_snb = the new one (inclusive)
; out: nothing; sets the pen to the background itself, because every caller
;      wants exactly that.
; -----------------------------------------------------------------------------
cy_rsub:
    ; Preserves SI/DI and nothing else: it is on the drawing spine
    ; (SPEC.md 67.5.5) and its one caller reloads from cy_snl.. afterwards.
    mov al, CY_C_BG
    call cy_setcol

    ; the overlap
    mov ax, [cy_sol]
    cmp ax, [cy_snl]
    jge .l1
    mov ax, [cy_snl]
.l1:
    mov [cy_ovl], ax
    mov ax, [cy_sor]
    cmp ax, [cy_snr]
    jle .r1
    mov ax, [cy_snr]
.r1:
    mov [cy_ovr], ax
    mov ax, [cy_sot]
    cmp ax, [cy_snt]
    jge .t1
    mov ax, [cy_snt]
.t1:
    mov [cy_ovt], ax
    mov ax, [cy_sob]
    cmp ax, [cy_snb]
    jle .b1
    mov ax, [cy_snb]
.b1:
    mov [cy_ovb], ax

    mov ax, [cy_ovl]
    cmp ax, [cy_ovr]
    jg .whole
    mov ax, [cy_ovt]
    cmp ax, [cy_ovb]
    jg .whole

    ; left strip, full height of the old rect
    mov ax, [cy_sol]
    mov cx, [cy_ovl]
    dec cx
    cmp ax, cx
    jg .right
    mov bx, [cy_sot]
    mov dx, [cy_sob]
    call cy_fillx
.right:
    mov ax, [cy_ovr]
    inc ax
    mov cx, [cy_sor]
    cmp ax, cx
    jg .top
    mov bx, [cy_sot]
    mov dx, [cy_sob]
    call cy_fillx
.top:
    ; above and below the overlap, between the two vertical strips
    mov ax, [cy_ovl]
    mov cx, [cy_ovr]
    mov bx, [cy_sot]
    mov dx, [cy_ovt]
    dec dx
    cmp bx, dx
    jg .bot
    call cy_fillx
.bot:
    mov ax, [cy_ovl]
    mov cx, [cy_ovr]
    mov bx, [cy_ovb]
    inc bx
    mov dx, [cy_sob]
    cmp bx, dx
    jg .out
    call cy_fillx
    ret
.whole:
    mov ax, [cy_sol]
    mov bx, [cy_sot]
    mov cx, [cy_sor]
    mov dx, [cy_sob]
    call cy_fillx
.out:
    ret

; =============================================================================
; THE BATCHED WALK (SPEC.md 5.6.8)
;
; A walk step's ARRIVING is ~128.7us and its per-block setup ~480us, so a
; caller with 17 live spokes that steps them one call each pays 17 arrivals a
; frame to lay a few pixels. One array, one call, same pixels.
; =============================================================================

; cy_dsc_add - queue walk DI for CX pixels.
cy_dsc_add:
    or cx, cx
    jz .out                         ; a zero count is legal and skipped, but
    push bx                         ; queueing one wastes a slot
    cmp word [cy_dscn], CY_MAXV
    jb .room
    call cy_dsc_run                 ; full: spend it and start again
.room:
    mov bx, [cy_dscn]
    shl bx, 1
    shl bx, 1
    mov [cy_dsc + bx], di
    mov [cy_dsc + bx + 2], cx
    inc word [cy_dscn]
    pop bx
.out:
    ret

; cy_dsc_run - spend the batch. MUST be called before the pen changes: a
; descriptor carries no colour and the whole array draws in one ink.
cy_dsc_run:
    push ax
    push cx
    push di
    push es
    mov cx, [cy_dscn]
    jcxz .out
    mov word [cy_dscn], 0
    mov di, cy_dsc
    push ds
    pop es
    call OSAPI_GFX_LSTEPV
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; THE WARP (SPEC.md 67.4)
;
; Level entry does not zoom a finished web - it EXTRUDES one. Every spoke is
; a resumable walk (SPEC.md 5.6.7) advanced a few pixels a frame, all of them
; in one `OSAPI_GFX_LSTEPV`, and NOTHING IS ERASED: the animation accumulates,
; so when it finishes the static playfield is already on the glass because
; drawing it WAS the animation. A frame costs the block setups plus the pixels
; actually laid, which is ~20ms - smooth at 18fps on a 4.77MHz 8088 - and the
; ~180ms a whole web costs is never paid as a lump.
;
; Leaving a level re-inits the identical walks from the identical endpoints in
; the identical order and replays them in the background colour, so the erase
; visits exactly the pixels the draw visited (5.6.7's whole point) and no
; remnant is possible. That is why the endpoints are re-derived from the
; vertex tables rather than banked: same table, same line, same pixel set.
;
; Two phases, not one, and that is a frame-cost decision: 17 spokes and 16 rim
; segments in one batch is 33 block setups a frame (~16ms before a pixel is
; laid). Run them one after the other and each frame sets up at most 17. It
; also reads better - the tube extrudes, then the rim lights up around it.
; =============================================================================

CY_WARP_SPOKE equ 0
CY_WARP_RIM   equ 1

; cy_warp_lay - initialise the walks for phase AL, and their pixel counts.
; in:  AL = CY_WARP_SPOKE or CY_WARP_RIM
; out: [cy_wn] = how many walks are live
cy_warp_lay:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es

    mov [cy_wphase], al
    mov word [cy_wn], 0
    cmp word [cy_nlane], 0
    je .out

    cmp al, CY_WARP_RIM
    je .rim

    ; --- the spokes: far vertex -> rim vertex, one per vertex ---------------
    mov cx, [cy_nvert]
    xor si, si                      ; vertex index
.sp:
    mov ax, 0                       ; the far end
    mov bx, si
    call cy_vert                    ; CX/DX = x/y  (CX is the loop counter -
    push cx                         ; save it, cy_vert answers in CX)
    push dx
    mov ax, CY_DEPTH
    mov bx, si
    call cy_vert
    mov [cy_wx2], cx
    mov [cy_wy2], dx
    pop dx
    pop cx
    mov [cy_wx1], cx
    mov [cy_wy1], dx
    call cy_walk_one
    inc si
    mov cx, [cy_nvert]
    cmp si, cx
    jb .sp
    jmp short .out

    ; --- the rim: vertex v -> vertex v+1, at depth CY_DEPTH ------------------
.rim:
    xor si, si
.rm:
    mov ax, CY_DEPTH
    mov bx, si
    call cy_vert
    mov [cy_wx1], cx
    mov [cy_wy1], dx
    mov bx, si
    inc bx
    cmp word [cy_closed], 0
    je .norw
    cmp bx, [cy_nlane]
    jb .norw
    xor bx, bx
.norw:
    mov ax, CY_DEPTH
    call cy_vert
    mov [cy_wx2], cx
    mov [cy_wy2], dx
    call cy_walk_one
    inc si
    cmp si, [cy_nlane]
    jb .rm
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_walk_one - init walk number [cy_wn] over (wx1,wy1)-(wx2,wy2) and record
; its pixel length. The length is max(|dx|,|dy|)+1, which is exactly how many
; pixels a Bresenham lays - so a walk asked for its own length is complete and
; one asked for more would step past its end.
cy_walk_one:
    push ax
    push bx
    push cx
    push dx
    push di

    mov di, [cy_wn]
    cmp di, CY_MAXV
    jae .out
    mov ax, di
    mov bx, GLS_SZ
    mul bx
    add ax, cy_walk
    mov di, ax                      ; ES:DI = the block (ES = DS, set by caller)

    mov ax, [cy_wx1]                ; content -> ABSOLUTE: gfx_linit is the
    add ax, [cy_ox]                 ; one primitive here that is not reached
    mov bx, [cy_wy1]                ; through cy_fillc
    add bx, [cy_oy]
    mov cx, [cy_wx2]
    add cx, [cy_ox]
    mov dx, [cy_wy2]
    add dx, [cy_oy]
    call OSAPI_GFX_LINIT

    mov ax, [cy_wx2]
    sub ax, [cy_wx1]
    jns .adx
    neg ax
.adx:
    mov bx, [cy_wy2]
    sub bx, [cy_wy1]
    jns .ady
    neg bx
.ady:
    cmp ax, bx
    jae .have
    mov ax, bx
.have:
    inc ax
    mov di, [cy_wn]
    shl di, 1
    mov [cy_wrem + di], ax
    inc word [cy_wn]
.out:
    pop di
    pop cx
    pop dx
    pop bx
    pop ax
    ret

; cy_warp_step - advance every live walk by AX pixels in the current pen.
; out: CF=1 every walk is finished (the phase is complete).
cy_warp_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov [cy_wk], ax
    mov word [cy_dscn], 0
    xor si, si                      ; walk index
    xor bp, bp                      ; how many still have pixels owing
.each:
    cmp si, [cy_wn]
    jae .go
    mov di, si
    shl di, 1
    mov cx, [cy_wrem + di]
    or cx, cx
    jz .next
    cmp cx, [cy_wk]
    jbe .last
    mov cx, [cy_wk]
.last:
    sub [cy_wrem + di], cx
    jz .noowe
    inc bp
.noowe:
    mov ax, si
    mov bx, GLS_SZ
    mul bx
    add ax, cy_walk
    mov di, ax
    call cy_dsc_add
.next:
    inc si
    jmp short .each
.go:
    call cy_dsc_run
    or bp, bp
    jnz .more
    stc
    jmp short .out
.more:
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_web_far - the far ring, drawn whole in one go at the start of a warp.
;
; It is affordable as a lump precisely because it is FAR: at scale 26/256 its
; perimeter is a tenth of the rim's, so this is a handful of very short walks
; where the rim would be ~500 pixels. It is also the only ring drawn at all -
; the depth rings a tube "should" have are exactly where enemies travel, and
; a mover crossing static art is what would force the whole erase-and-repair
; machinery this app is built to avoid (SPEC.md 67.1).
; in: AL = the pen. IT IS AN ARGUMENT, and it has to be: the warp-out and the
; attract loop both call this to ERASE the ring, and a body that set its own
; colour drew it again instead - so the far ring survived every level exit and
; every attract cycle, accumulating on the glass where nothing would ever
; erase it.
cy_web_far:
    push ax
    push cx
    call cy_setcol
    call cy_warp_lay_far
    mov ax, 0x7FFF                  ; every walk, to its end, in one call
    call cy_warp_step
    pop cx
    pop ax
    ret

; ...the far ring's walks: vertex v -> v+1 at depth 0.
cy_warp_lay_far:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    mov word [cy_wn], 0
    cmp word [cy_nlane], 0
    je .out
    xor si, si
.rm:
    xor ax, ax
    mov bx, si
    call cy_vert
    mov [cy_wx1], cx
    mov [cy_wy1], dx
    mov bx, si
    inc bx
    cmp word [cy_closed], 0
    je .norw
    cmp bx, [cy_nlane]
    jb .norw
    xor bx, bx
.norw:
    xor ax, ax
    call cy_vert
    mov [cy_wx2], cx
    mov [cy_wy2], dx
    call cy_walk_one
    inc si
    cmp si, [cy_nlane]
    jb .rm
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE OBJECT LAYER (SPEC.md 67.5)
;
; Every mover on the web - enemy, shot, powerup, the player's claw, a piece of
; game-over debris - is a RECT, and the only thing the drawing path knows
; about it is a 10-byte state block: whether it is on the glass and the rect
; it is on the glass AS.
;
;   +0 shown  +2 left  +4 top  +6 right  +8 bottom  +10 pen  (+11 spare)
;
; The pen is in there because of cy_obj_dmg: putting a damaged object back
; means drawing it again, and the thing that damaged it has no idea what
; colour it was.
;
; "Where it is" and "where it is DRAWN" are two different things and only
; these two routines may write the second (Missile Command's rule, and the
; reason its erases stayed exact across skipped frames). A frame that is
; dropped - covered window, refused clip - leaves the block naming what is
; genuinely still on the glass, so the next frame's erase is still right.
; =============================================================================

; cy_obj_show - put the rect in cy_snl..cy_snb on the glass for object DI.
; in:  DI = the state block, cy_snl/snt/snr/snb = the new rect, AL = the pen.
; out: CF=1 nothing was touched. Preserves SI and DI and NOTHING ELSE: it is
;      on the drawing spine (SPEC.md 67.5.5), and all six call sites are loops
;      that keep their cursor in SI and reload everything else from the arrays
;      on the next line.
cy_obj_show:
    mov [di + 10], al               ; the pen, for cy_obj_dmg's repair

    cmp word [di], 0
    je .draw                        ; not on the glass: nothing to compare
    mov ax, [cy_snl]
    cmp ax, [di + 2]
    jne .draw
    mov ax, [cy_snt]
    cmp ax, [di + 4]
    jne .draw
    mov ax, [cy_snr]
    cmp ax, [di + 6]
    jne .draw
    mov ax, [cy_snb]
    cmp ax, [di + 8]
    je .same                        ; IT DID NOT MOVE. At low depth this is
                                    ; most enemies most frames, and it is the
                                    ; single biggest saving in the game loop
.draw:
    mov al, [di + 10]               ; THE PEN, BACK OUT OF THE BLOCK. Three of
                                    ; the four ways into .draw are the compares
                                    ; above, and every one of them has just
                                    ; loaded a COORDINATE into AX - so AL was
                                    ; the low byte of cy_snl/snt/snr/snb and
                                    ; cy_setcol dutifully made that the colour.
                                    ; SPEC.md 67.5.7.
    call cy_setcol
    mov ax, [cy_snl]
    mov bx, [cy_snt]
    mov cx, [cy_snr]
    mov dx, [cy_snb]
    call cy_fillx

    cmp word [di], 0
    je .store
    mov ax, [di + 2]                ; the old rect, for the subtraction
    mov [cy_sol], ax
    mov ax, [di + 4]
    mov [cy_sot], ax
    mov ax, [di + 6]
    mov [cy_sor], ax
    mov ax, [di + 8]
    mov [cy_sob], ax
    call cy_rsub
    call cy_obj_dmg                 ; ...and put back whatever it cut into,
                                    ; on the spot (SPEC.md 67.5.4)
.store:
    mov word [di], 1
    mov ax, [cy_snl]
    mov [di + 2], ax
    mov ax, [cy_snt]
    mov [di + 4], ax
    mov ax, [cy_snr]
    mov [di + 6], ax
    mov ax, [cy_snb]
    mov [di + 8], ax
    clc                             ; CF=0: it was drawn, so anything layered
    ret                             ; inside the rect owes itself a redraw
.same:
    stc                             ; CF=1: nothing was touched
    ret

; cy_obj_hide - take object DI off the glass. Preserves everything.
cy_obj_hide:
    push ax
    push bx
    push cx
    push dx
    cmp word [di], 0
    je .out
    mov al, CY_C_BG
    call cy_setcol
    mov ax, [di + 2]
    mov bx, [di + 4]
    mov cx, [di + 6]
    mov dx, [di + 8]
    call cy_fillc
    mov ax, [di + 2]                ; ...and put back whatever else was in it
    mov [cy_sol], ax
    mov ax, [di + 4]
    mov [cy_sot], ax
    mov ax, [di + 6]
    mov [cy_sor], ax
    mov ax, [di + 8]
    mov [cy_sob], ax
    mov word [di], 0
    call cy_obj_dmg
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_obj_dmg ---------------------------------------------------------------
; The rect in cy_sol..cy_sob has just been given back to the background. Any
; OTHER object standing in it lost pixels, and it will not get them back on
; its own: it did not move, so its own cy_obj_show takes the did-not-move exit
; and draws nothing (67.5 rule 2).
;
; That is what "the movers appear and disappear" was. Shots run down the SAME
; lane the enemies climb, so a shot's erase passes straight over every enemy
; in that lane on its way to the far end - and each one it clipped stayed
; clipped until it happened to move a whole pixel. The lanes being shot at
; were the ones that looked empty.
;
; Repair rather than reorder: there is no draw order that fixes it, because
; the damage is done by an ERASE and the erased object may be anywhere in the
; pass. The whole OLD rect is used as the damage rather than cy_rsub's four
; strips - a superset, so an object that overlapped only the covered part is
; redrawn identically for nothing, which is cheaper than deciding.
;
; IT REPAIRS ON THE SPOT, and getting there is SPEC.md 67.5.5.
;
; It marked a byte and left a sweep at the end of the frame to spend the marks,
; and that is 55ms - a whole frame on the target machine - with the damaged
; mover dark. The raster catches it: pausing the emulator at random instants
; measured ~39% of the movers on the web absent at any given moment, which is
; exactly the "appearing and disappearing" this routine exists to fix, moved
; from most of a second down to one frame rather than removed.
;
; Repairing here instead is a fill under an already deep chain, and the first
; attempt HUNG: cy_obj_put + cy_fillc + a far call, five frames below a worker
; that gets 256 BYTES (SPEC.md 8), overran the slice and the kernel halted the
; machine in sch_stkdie exactly as it is meant to. Moving the call one frame
; shallower was not enough - four runs of four still hung - because the spine
; above it was 76 bytes of politeness. Flattening that (67.5.5) took the whole
; chain to 24 and this call is affordable out of what is left.
;
; The cost is that an object damaged TWICE in one frame is now drawn twice
; where the sweep drew it once. Both are correct; the second draw is not
; waste, because between them the object really was cut into again.
;
; in:  DI = the object that did the erasing (skipped), cy_sol..cy_sob = the
;      rect it gave back. Preserves SI, CX and DI; AX/BX/DX and the flags are
;      scratch, which every one of the four call sites is (each either ends
;      the routine or reloads on the next line).
; -----------------------------------------------------------------------------
cy_obj_dmg:
    push si
    push cx
    mov si, cy_ost
    mov cx, CY_NOBJ
.each:
    cmp si, di
    je .next                        ; the one that just drew is already right
    cmp word [si], 0
    je .next                        ; not on the glass
    mov ax, [si + 6]                ; its right vs the damage's left
    cmp ax, [cy_sol]
    jl .next
    mov ax, [si + 2]
    cmp ax, [cy_sor]
    jg .next
    mov ax, [si + 8]
    cmp ax, [cy_sot]
    jl .next
    mov ax, [si + 4]
    cmp ax, [cy_sob]
    jg .next
    call cy_obj_put                 ; it lost pixels and it will not get them
.next:                              ; back on its own: it did not move, so its
                                    ; own cy_obj_show takes the did-not-move
                                    ; exit and draws nothing
    add si, CY_OBSZ
    loop .each
    pop cx
    pop si
    ret

; cy_dmg_rect - AX/BX/CX/DX is a rect that has just been painted BACKGROUND by
; something that is not a mover. Mark whatever stood in it.
;
; Every background write in the playfield has to come through here or through
; cy_rsub, or the thing it erased is gone until it happens to move. The two
; that did not were the claw's own NOTCH and the message banner - and both sit
; exactly where the player is, now that CY_TOPD put the claw and the arrived
; enemies on the same ring.
cy_dmg_rect:
    push di
    mov [cy_sol], ax
    mov [cy_sot], bx
    mov [cy_sor], cx
    mov [cy_sob], dx
    xor di, di                      ; no self to skip
    call cy_obj_dmg
    pop di
    ret

; cy_obj_put - draw block SI again, from what it says is on the glass.
; Preserves CX (cy_obj_dmg, its one caller, is counting the table down in it)
; and SI; AX/BX/DX are scratch.
cy_obj_put:
    push cx
    mov al, [si + 10]
    call cy_setcol
    mov ax, [si + 2]
    mov bx, [si + 4]
    mov cx, [si + 6]
    mov dx, [si + 8]
    call cy_fillx
    mov ax, si                      ; the claw carries a notch inside its own
    sub ax, cy_ost                  ; rect (67.5), and a plain refill would
    cmp ax, CY_OB_PL * CY_OBSZ      ; fill it in
    jne .out
    mov al, CY_C_BG
    call cy_setcol
    mov ax, [si + 2]
    add ax, 2
    mov cx, [si + 6]
    sub cx, 2
    cmp ax, cx
    jg .out
    mov bx, [si + 4]
    mov dx, [si + 8]
    dec dx
    cmp bx, dx
    jg .out
    call cy_fillx                   ; NO cy_dmg_rect here. This routine is
                                    ; what cy_obj_dmg calls, so damaging from
                                    ; inside it is unbounded recursion - and
                                    ; it hangs the game rather than drawing
                                    ; anything wrong. Whatever this notch
                                    ; erases was already covered by the erase
                                    ; that caused the repair.
.out:
    pop cx
    ret

; cy_obj_forget - the glass was cleared under us (a full repaint), so every
; block must stop claiming pixels it no longer owns. Missile Command's
; `mc_draw_all` invalidator: an erase against a stale rect would cut a hole in
; whatever the repaint just drew.
cy_obj_forget:
    push cx
    push di
    mov di, cy_ost
    mov cx, CY_NOBJ
.each:
    mov word [di], 0
    add di, CY_OBSZ
    loop .each
    pop di
    pop cx
    ret

; cy_obj_ptr - the state block for object index AX.
; out: DI = the block. Preserves everything else.
cy_obj_ptr:
    push ax
    push dx
    mov dx, CY_OBSZ
    mul dx
    add ax, cy_ost
    mov di, ax
    pop dx
    pop ax
    ret

; --- cy_setrect ---------------------------------------------------------------
; The new rect for a mover on (lane AL, depth AH) with half-extents BL/BH at
; full depth. The extents shrink with depth so an object genuinely recedes:
; (extent * (depth + 4)) / 20, which is the extent at the rim and a fifth of
; it at the far end, never negative and never rounded away to nothing.
; out: cy_snl..cy_snb
; -----------------------------------------------------------------------------
cy_setrect:
    push ax
    push bx
    push cx
    push dx
    push si

    mov [cy_tmpk], bx               ; the extents, out of the way of the mul
    mov bl, ah
    mov bh, 0
    add bx, 4
    mov [cy_tmpd], bx               ; the depth term

    call cy_lanepos                 ; CX/DX = the centre
    mov [cy_tmpx], cx
    mov [cy_tmpy], dx

    mov al, [cy_tmpk]               ; half width
    mov ah, 0
    mul word [cy_tmpd]
    mov bx, 20
    xor dx, dx
    div bx
    call cy_lanecap
    mov si, ax
    mov ax, [cy_tmpx]
    sub ax, si
    mov [cy_snl], ax
    mov ax, [cy_tmpx]
    add ax, si
    mov [cy_snr], ax

    mov al, [cy_tmpk + 1]           ; half height
    mov ah, 0
    mul word [cy_tmpd]
    mov bx, 20
    xor dx, dx
    div bx
    call cy_lanecap
    mov si, ax
    mov ax, [cy_tmpy]
    sub ax, si
    mov [cy_snt], ax
    mov ax, [cy_tmpy]
    add ax, si
    mov [cy_snb], ax

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_lanecap ---------------------------------------------------------------
; Bound a half-extent by the LANE, so a mover cannot reach the spokes that
; bound it - which is what 67.1's "the movers never touch the web" actually
; requires, and what a fixed depth ramp does not deliver. Near the far end a
; lane is a few pixels across while the ramp still wants four, and on the flat
; and vee webs it is narrow at every depth.
;
; A THIRD OF THE LANE'S DOMINANT SPAN, ON BOTH AXES, WITH A FLOOR OF ONE.
; Three earlier rules were wrong, and what said so was the diff against a full
; repaint for the first two and LOOKING AT IT for the third:
;
;   - HALF the span left the mover as wide as its lane, and rounding put it on
;     the spoke.
;   - Bounding only the axis the lane mostly runs along, and leaving the other
;     free as "radial", is exact for an axis-aligned lane and WRONG NEAR 45
;     DEGREES, where neither screen axis is radial. The free one then reached
;     straight through the spokes.
;
;   - A QUARTER kept every mover strictly inside its lane and made them ONE
;     OR TWO PIXELS for most of the tube - which, against a web whose spokes
;     are a dither and therefore already a row of dots, is indistinguishable
;     from the web itself. It reads as movers that are not there, and it is
;     what "still flashing in and out" turned out to be: they were never
;     flashing, they were too small to see.
;
; A third is the compromise, with a floor of one so a mover is never smaller
; than 3x3. The corner of an axis-aligned rect reaches half-extent * sqrt(2),
; so a third of the dominant span is 0.471 of it - inside the chord's half
; for an axis-aligned lane and marginal on a diagonal one. What that buys
; back is a mover you can see; what it costs is the occasional pixel of spoke,
; which 67.1's own accounting has to live with.
;
; in:  AX = the half-extent the ramp wants
; out: AX = the one it may have, never less than 1.
; -----------------------------------------------------------------------------
cy_lanecap:
    push bx
    push cx
    push dx
    mov bx, [cy_lspm]
    cmp bx, 255                     ; the byte multiply below wants it small,
    jbe .sm                         ; and a span this big never binds anyway
    mov bx, 255
.sm:
    push ax
    mov al, bl                      ; span * 85 / 256 == span / 3.01, and no
    mov ah, 0                       ; divide: an 8086 DIV here would cost more
    mov cl, 85                      ; than the whole cap saves
    mul cl
    mov bl, ah
    mov bh, 0
    pop ax
    or bx, bx
    jnz .have
    mov bx, 1                       ; never smaller than one pixel either side
.have:
    cmp ax, bx
    jbe .out
    mov ax, bx
.out:
    or ax, ax
    jnz .done
    mov ax, 1
.done:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; SCORE
;
; A dword, because Tempest 2000 is a score-attack game and six figures is a
; single good level. The formatter is the standard 32-bit long division by
; ten: the high word first, then the remainder joined to the low word.
; =============================================================================

; cy_score_add - add AX to the score, and pay a bonus life every 20,000.
cy_score_add:
    push ax
    push bx
    push dx
    add [cy_score], ax
    adc word [cy_score + 2], 0
    mov ax, [cy_score]
    mov dx, [cy_score + 2]
    mov bx, 20000
    div bx                          ; AX = score / 20000
    cmp ax, [cy_bonus]
    jbe .out
    mov [cy_bonus], ax
    cmp byte [cy_lives], 9
    jae .out
    inc byte [cy_lives]
    mov byte [cy_huddirty], 1
.out:
    pop dx
    pop bx
    pop ax
    ret

; cy_fmt - format the dword at SI into CX digits at DI, leading zeros.
cy_fmt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [si]
    mov [cy_fnum], ax
    mov ax, [si + 2]
    mov [cy_fnum + 2], ax
    add di, cx
    dec di
.digit:
    mov ax, [cy_fnum + 2]
    xor dx, dx
    mov bx, 10
    div bx                          ; AX = hi/10, DX = remainder
    mov [cy_fnum + 2], ax
    mov ax, [cy_fnum]
    div bx                          ; DX:AX / 10 with DX carried in
    mov [cy_fnum], ax
    add dl, '0'
    mov [di], dl
    dec di
    loop .digit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_fmt2 - the same for a plain word in AX, CX digits at DI.
cy_fmt2:
    push ax
    push bx
    push cx
    push dx
    push di
    add di, cx
    dec di
    mov bx, 10
.digit:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .digit
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE HUD (SPEC.md 67.6)
;
; One strip, one opaque `font_run`, and only the cells that CHANGED go out.
;
; Missile Command's SPEC.md 48.17 is the precedent and the measurement: its
; status strip re-lettered all 29 cells on every kill - ~29ms on a 4.77MHz
; Hercules machine, several times a second - when only the score's last digit
; had moved. Keeping the text the strip was last DRAWN with and emitting the
; span from the first differing cell to the last subsumes a per-field dirty
; bit rather than needing one: a field whose text has not changed draws
; nothing, whatever any flag said.
; =============================================================================

cy_hud_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, cy_hudbuf               ; blank the whole strip first, so a field
    mov cx, CY_HUDW                 ; that got SHORTER is erased by its own
    mov al, ' '                     ; padding rather than by a fill
    push es
    push ds
    pop es
    rep stosb
    pop es

    mov si, cy_score
    mov di, cy_hudbuf
    mov cx, 7
    call cy_fmt

    mov word [cy_hudbuf + 8], 'L' | ('V' << 8)
    mov ax, [cy_level]
    mov di, cy_hudbuf + 10
    mov cx, 2
    call cy_fmt2

    mov byte [cy_hudbuf + 13], 'x'
    mov al, [cy_lives]
    mov ah, 0
    mov di, cy_hudbuf + 14
    mov cx, 1
    call cy_fmt2

    ; The superzapper: one glyph per charge, so its state is legible at a
    ; glance and on every adapter - a colour would collapse on 1bpp.
    mov al, [cy_zap]
    mov ah, 0
    or ax, ax
    jz .pu
    mov cx, ax
    cmp cx, 3
    jbe .zn
    mov cx, 3
.zn:
    mov di, cy_hudbuf + 16
.zl:
    mov byte [di], 'Z'
    inc di
    loop .zl
.pu:
    ; the live powerups, one letter each - L(aser) D(roid) J(ump)
    mov di, cy_hudbuf + 20
    cmp word [cy_pw_laser], 0
    je .nd
    mov byte [di], 'L'
    inc di
.nd:
    cmp word [cy_pw_droid], 0
    je .nj
    mov byte [di], 'D'
    inc di
.nj:
    cmp byte [cy_pw_jump], 0
    je .done
    mov byte [di], 'J'
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_hud_emit - compare against the shadow and letter only what moved.
cy_hud_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov cx, [cy_hudw]               ; how many cells this window can show
    or cx, cx
    jz .out
    mov word [cy_hf0], -1
    mov word [cy_hf1], -1
    xor bx, bx
.scan:
    cmp bx, cx
    jae .done
    mov al, [cy_hudbuf + bx]
    cmp al, [cy_hudsh + bx]
    je .nx
    cmp word [cy_hf0], -1
    jne .lo
    mov [cy_hf0], bx
.lo:
    mov [cy_hf1], bx
.nx:
    inc bx
    jmp short .scan
.done:
    cmp word [cy_hf0], -1
    je .out                         ; unchanged: nothing goes out at all

    mov si, [cy_hf0]                ; refresh the shadow over the span
    mov cx, [cy_hf1]
    sub cx, si
    inc cx
    mov di, si
    add di, cy_hudsh
    add si, cy_hudbuf
    push es
    push ds
    pop es
    push cx
    rep movsb
    pop cx
    pop es

    mov si, [cy_hf0]                ; ...and letter it as ONE opaque run
    add si, cy_hudbuf
    mov di, si
    add di, cx
    mov al, [di]
    mov [cy_hsav], al
    mov byte [di], 0                ; NUL-terminate the span in place, and put
                                    ; the byte back after: font_run takes a
    mov cx, [cy_hf0]                ; string and the strip is not one
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; cell -> pixels (8086: no shl reg, imm)
    mov dx, 1
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc
    mov al, [cy_hsav]
    mov [di], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_hud_clr - invalidate the shadow with a byte no field can contain, so
; every cell reads as changed. Called by every full repaint.
cy_hud_clr:
    push ax
    push cx
    push di
    mov di, cy_hudsh
    mov cx, CY_HUDW
    mov al, 0xFF
    push es
    push ds
    pop es
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

cy_hud_draw:
    call cy_hud_build
    call cy_hud_emit
    mov byte [cy_huddirty], 0
    ret

; =============================================================================
; THE SPIKES (SPEC.md 67.7)
;
; The one thing in this game that breaks the "movers never touch static art"
; invariant, and it is worth stating why it is allowed to.
;
; A spiker builds a spike UP ITS OWN LANE, which is exactly where everything
; else travels - so a mover's erase cuts it, and there is no arrangement of
; the drawing that avoids that. SPEC.md 48.9.1's rule applies instead: an
; optimisation that stops something being redrawn every frame inherits every
; place that used to rely on that redraw, so the fix is a damage MARK. A
; mover that erased over a spiked lane marks it, and at most CY_SPKREP lanes
; are repaired per frame - a bound, because a flipper descending a fully
; spiked lane would otherwise repair sixteen segments every frame for ever.
; A repair one frame late is invisible; an unbounded one is a stutter.
; =============================================================================

CY_SPKREP   equ 2

; cy_spk_mark - a mover at (lane AL, depth AH) is about to erase. Mark the
; lane if it has a spike that reaches that depth.
cy_spk_mark:
    call cy_web_mark                ; every caller is a mover with its lane in
                                    ; AL, so the web mark rides along rather
                                    ; than being threaded through four loops
    push bx
    mov bl, al
    mov bh, 0
    cmp bl, CY_MAXLANE
    jae .out
    mov al, [cy_spk + bx]
    or al, al
    jz .out
    cmp ah, al
    ja .out                         ; above the spike's tip: nothing to repair
    mov byte [cy_spkd + bx], 1
.out:
    pop bx
    ret

; cy_spk_draw - draw lane BL's spike, whole, in the enemy-secondary pen.
;
; A chain of small marks, one per depth step it has reached, because a lane
; is not axis-aligned and one rect cannot follow it. The chain is why the
; repair is BOUNDED (CY_SPKREP a frame): a full-height spike is CY_DEPTH
; fills, and repairing several of those every frame would be the stutter the
; bound exists to prevent.
cy_spk_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bh, 0
    mov [cy_tmpn], bx               ; the lane, out of the way of cy_lanepos
    mov bl, [cy_spk + bx]
    mov bh, 0
    mov [cy_tmpd], bx               ; how far up it has reached
    or bx, bx
    jz .clean
    mov al, [cy_c_e2]
    call cy_setcol
    xor si, si
.each:
    mov bx, [cy_tmpn]
    mov ax, si                      ; SI < CY_NDEPTH, so AH is already 0
    mov ah, al                      ; AH = depth
    mov al, bl                      ; AL = lane
    call cy_lanepos                 ; CX/DX = the lane centre at that depth
    mov ax, cx
    dec ax
    mov bx, dx
    dec bx
    mov cx, ax
    inc cx
    inc cx
    mov dx, bx
    inc dx
    inc dx
    call cy_fillc
    inc si
    cmp si, [cy_tmpd]
    jb .each
.clean:
    mov bx, [cy_tmpn]
    mov byte [cy_spkd + bx], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_spk_repair - spend up to CY_SPKREP marks.
cy_spk_repair:
    push ax
    push bx
    push cx
    mov cx, CY_SPKREP
    xor bx, bx
.each:
    cmp bx, [cy_nlane]
    jae .out
    cmp byte [cy_spkd + bx], 0
    je .next
    push bx
    call cy_spk_draw
    pop bx
    dec cx
    jz .out
.next:
    inc bx
    jmp short .each
.out:
    pop cx
    pop bx
    pop ax
    ret

%ifdef CYTRACE
; =============================================================================
; cy_trace (make CYTRACE=1) - WHO painted background over the watch rect?
;
; Every theory about a vanishing mover so far has been a reading of the source,
; and every one has been wrong. This asks the machine instead: the host writes
; a rect into cy_twl..cy_twb, and every background fill that lands in it
; records ITS CALLER'S RETURN ADDRESS - which maps straight back to a symbol
; with nasm's listing, exactly as tools/cyunwind does for the stack.
;
; No tagging of call sites, so it cannot miss one that was added later.
; Called from cy_fillx with the final content rect in AX/BX/CX/DX.
; =============================================================================
cy_trace:
    push bp
    push ax
    push bx
    push cx
    push dx
    mov bp, sp                      ; [bp+0]=dx +2=cx +4=bx +6=ax +8=bp
                                    ; [bp+10] = return into cy_fillx
                                    ; [bp+12] = cy_fillx's OWN caller
    cmp byte [cy_col], CY_C_BG
    jne .out
    mov ax, [ss:bp + 2]             ; x2 vs watch left
    cmp ax, [cy_twl]
    jl .out
    mov ax, [ss:bp + 6]             ; x1 vs watch right
    cmp ax, [cy_twr]
    jg .out
    mov ax, [ss:bp + 0]             ; y2 vs watch top
    cmp ax, [cy_twt]
    jl .out
    mov ax, [ss:bp + 4]             ; y1 vs watch bottom
    cmp ax, [cy_twb]
    jg .out
    inc word [cy_tn]
    ; COPY A WINDOW OF THE STACK, not a fixed frame. cy_fillx's callers push
    ; different numbers of registers before calling it, so "the caller's
    ; caller" is at a different offset for each - the first attempt read
    ; cy_fillc's saved DX as a return address. The host picks the code
    ; addresses out of the window.
    push si
    push di
    mov si, bp
    add si, 10
    mov di, [cy_ti]
    add di, cy_tbuf
    mov cx, CY_TWORDS
.cp:
    mov ax, [ss:si]                 ; NOT `rep movsw`: its source is DS:SI and
    mov [di], ax                    ; the stack is in SS, which is a different
    add si, 2                       ; segment here (SPEC.md 2.1)
    add di, 2
    loop .cp
    pop di
    pop si
    mov bx, [cy_ti]
    add bx, CY_TWORDS * 2
    cmp bx, CY_TBUF * CY_TWORDS * 2
    jb .keep
    xor bx, bx
.keep:
    mov [cy_ti], bx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    pop bp
    ret
%endif

; =============================================================================
; THE FULLSCREEN CURSOR (SPEC.md 67.17)
;
; A bracket holds the gfx lock for the whole session, so the kernel's arrow is
; off the screen and the aim-by-click that works in a window has nothing to aim
; WITH. The app draws its own.
;
; It is XOR, so the erase is the draw, and it is bracketed around the frame:
; erased FIRST, before the update and the render, and drawn LAST. That is what
; makes it correct without any knowledge of what the frame is going to draw -
; nothing runs between the draw and the next erase, so the pixels it inverts
; are exactly the pixels it un-inverts. The alternative, drawing it once and
; taking it off only where something is about to overlap, is Missile Command's
; SPEC.md 48.11 and it needs a hook in every primitive; here the frame already
; costs a hundred rects and two more do not show.
;
; The position is BANKED rather than re-read at erase time. Re-reading is the
; classic XOR smear: the mouse moves between the draw and the erase, the erase
; lands somewhere the draw never did, and the marker leaves a permanent trail.
; =============================================================================
CY_CURR     equ 3                   ; half-size: a 7x7 open square

cy_cur_erase:
    cmp byte [cy_curon], 0
    je .out
    mov byte [cy_curon], 0
    mov ax, [cy_curx]
    mov bx, [cy_cury]
    jmp short cy_cur_xor
.out:
    ret

cy_cur_draw:
    cmp byte [cy_inbr], 0
    je .out                         ; windowed: the kernel's arrow is there
    call OSAPI_MOUSE                ; CX/DX = SCREEN x/y
    mov ax, cx
    sub ax, [cy_ox]
    mov bx, dx
    sub bx, [cy_oy]
    mov [cy_curx], ax
    mov [cy_cury], bx
    mov byte [cy_curon], 1
    jmp short cy_cur_xor
.out:
    ret

; cy_cur_xor - invert the marker at content (AX,BX). Preserves everything.
cy_cur_xor:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    add cx, CY_CURR
    sub ax, CY_CURR
    mov dx, bx
    add dx, CY_CURR
    sub bx, CY_CURR
    call cy_clamp                   ; content coords, and the box is the
    jc .out                         ; playfield, not the screen
    add ax, [cy_ox]
    add cx, [cy_ox]
    add bx, [cy_oy]
    add dx, [cy_oy]
    call OSAPI_GFX_XOR_RECT         ; an OUTLINE, not a fill: a solid block
.out:                               ; hides whatever it is being aimed at
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_bottom_lane -----------------------------------------------------------
; The lane whose spot on the lip is lowest on screen - where a Tempest player
; expects to start, and the anchor SPEC.md 67.16's arrow winding is chosen for.
; out: AX = the lane. Preserves everything else.
; -----------------------------------------------------------------------------
; cy_home_ck - spend a new level's claim on the claw's start lane, if the web
; exists yet. It cannot live inside cy_layout: cy_startlevel does not force a
; relayout, so the flag would sit unspent until the next resize.
cy_home_ck:
    cmp byte [cy_phome], 0
    je .out
    cmp word [cy_nlane], 0
    je .out                         ; no web yet - ask again next frame
    push ax
    mov byte [cy_phome], 0
    call cy_bottom_lane
    mov [cy_plane], ax
    pop ax
.out:
    ret

cy_bottom_lane:
    push bx
    push cx
    push dx
    push si
    xor si, si
    xor bx, bx                      ; best lane
    mov word [cy_tmpbest], 0x8000   ; ...and its y, as low as a word goes
.each:
    cmp si, [cy_nlane]
    jae .done
    mov ax, si
    mov ah, CY_TOPD
    call cy_lanepos                 ; CX/DX = that lane's spot on the lip
    cmp dx, [cy_tmpbest]
    jle .next                       ; SIGNED: a content y can be negative on a
    mov [cy_tmpbest], dx            ; box too small for the web
    mov bx, si
.next:
    inc si
    jmp short .each
.done:
    mov ax, bx
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; SOUND (SPEC.md 67.15)
;
; One tone channel, one table, and the pitches chosen so the four things a
; player needs to hear cannot be confused with each other. Reported from the
; field as "I can't tell what is missing, what is hitting, and when they die":
; a kill made no sound at all, and death was one 120 Hz blip that a burst of
; fire walked straight over.
;
; The rule the table exists to enforce is that PRIORITY has to agree with what
; the sound is FOR. Fire happens every few frames and is the least important
; thing on the channel; a kill has to be audible over a stream of it; a death
; has to be audible over everything. OSAPI_SND_TONE refuses a request from
; below the current owner's priority (SPEC.md 34), so the ordering here IS the
; mixing policy, and getting it upside down is a kill sound nobody ever hears.
;
; A tone is worker-safe by construction (SPEC.md 20.6 rule 7): snd_req_inst
; stamps the grant with the running task's own instance when no callback is
; being dispatched, so it is attributed to us and released at teardown.
; in: AL = CYS_* index. Preserves everything.
; =============================================================================
CYSFX_FIRE  equ 0
CYSFX_KILL  equ 1
CYSFX_SPLIT equ 2
CYSFX_PU    equ 3
CYSFX_ZAP   equ 4
CYSFX_JUMP  equ 5
CYSFX_DIE1  equ 6
CYSFX_DIE2  equ 7
CYSFX_DIE3  equ 8

; freq, ticks, priority
cy_sfxtab:
    dw 1400
    db 1, 0x40                      ; FIRE  - short, high, and the lowest
    dw  700                         ;         priority on the channel
    db 3, 0x58                      ; KILL  - an octave down from the blip and
                                    ;         three times as long: it lands ON
                                    ;         a fire and wins
    dw  990                         ; SPLIT - a tanker coming apart is not a
    db 2, 0x58                      ;         kill, and says so by going UP
    dw 1800
    db 3, 0x50                      ; PU    - the reward, brightest of all
    dw  220
    db 6, 0x60                      ; ZAP   - the lowest thing you can do on
    dw  900
    db 2, 0x40                      ; JUMP
    dw  400
    db 8, 0x78                      ; DIE   - three steps FALLING across the
    dw  260                         ;         death animation. A sweep is what
    db 8, 0x78                      ;         a single tone cannot be, and the
    dw  160                         ;         one shape nothing else here has
    db 9, 0x78
cy_sfxtab_end:
%if (cy_sfxtab_end - cy_sfxtab) != (CYSFX_DIE3 + 1) * 4
  %error "cy_sfxtab is not one 4-byte row per CYSFX_* index"
%endif

cy_sfx:
    push ax
    push bx
    push cx
    push dx
    mov bl, al
    mov bh, 0
    shl bx, 1
    shl bx, 1                       ; 4 bytes a row
    mov ax, [cy_sfxtab + bx]
    mov cl, [cy_sfxtab + bx + 2]
    mov ch, 0
    mov dl, [cy_sfxtab + bx + 3]
    call OSAPI_SND_TONE
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; LEVEL AND GAME SET-UP
; =============================================================================

cy_newgame:
    push ax
    push bx
    push cx
    push di
    mov byte [cy_ient], 0           ; ABANDON a half-typed name. cy_init_key
                                    ; swallows every key while the prompt is
                                    ; up, so the player cannot reach here with
                                    ; the keyboard - but Game > New Game can,
                                    ; and a prompt left standing into the new
                                    ; game would eat every keystroke of it
    mov word [cy_score], 0
    mov word [cy_score + 2], 0
    mov word [cy_bonus], 0
    mov byte [cy_lives], 3
    mov word [cy_level], 1
    mov byte [cy_zap], 1
    mov word [cy_pw_laser], 0
    mov word [cy_pw_droid], 0
    mov byte [cy_pw_jump], 0
    call cy_startlevel
    pop di
    pop cx
    pop bx
    pop ax
    ret

; cy_startlevel - pick the shape, clear the board, and begin the warp.
cy_startlevel:
    push ax
    push bx
    push cx
    push dx
    push di

    mov ax, [cy_level]              ; the shape cycles through the eight
    dec ax
    xor dx, dx
    mov bx, CY_NSHAPE
    div bx
    mov bx, dx
    shl bx, 1
    mov ax, [cy_shapes + bx]
    mov [cy_shape], ax

    call cy_clearboard              ; ...which zeroes cy_left for us
    mov byte [cy_needlay], 1
    mov byte [cy_state], CYS_WARPIN
    mov byte [cy_wpha], CY_WARP_SPOKE
    mov byte [cy_wstarted], 0
    mov byte [cy_full], 1
    mov byte [cy_huddirty], 1
    mov word [cy_spawnt], 0
    call cy_wavesize
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_wavesize - how many enemies this level owes, and how fast they come.
;
; The ramp is the whole difficulty curve: 8 at level 1 rising by three a
; level to a cap of 40, and the kinds unlock one at a time so the first level
; is flippers and nothing else.
cy_wavesize:
    push ax
    push bx
    push dx
    mov ax, [cy_level]
    dec ax
    mov bx, 3
    mul bx
    add ax, 8
    cmp ax, 40
    jbe .cap
    mov ax, 40
.cap:
    mov [cy_towave], ax
    mov [cy_wleft], ax

    mov ax, [cy_level]              ; the kinds available, by level
    mov bl, 1                       ; L1: flippers
    cmp ax, 2
    jb .k
    mov bl, 2                       ; L2: + tankers
    cmp ax, 3
    jb .k
    mov bl, 3                       ; L3: + spikers
    cmp ax, 5
    jb .k
    mov bl, 4                       ; L5: + fuseballs
    cmp ax, 7
    jb .k
    mov bl, 5                       ; L7: + pulsars
.k:
    mov [cy_kinds], bl

    ; Base climb speed, in depth-units a frame. The tube is
    ; (CY_TOPD - CY_FARD) * 256 = 3,328 units, so 15 a frame is 222 frames -
    ; TWELVE SECONDS at 18.2fps - for a level-1 flipper end to end, rising to
    ; about three seconds by level 15. Each kind then scales this by its own
    ; trait (cy_ekspd), which is where "some enemies ascend faster" lives.
    mov ax, [cy_level]
    dec ax
    mov bx, 3
    mul bx
    add ax, 15
    cmp ax, 55
    jbe .sc
    mov ax, 55
.sc:
    mov [cy_espd], ax
    pop dx
    pop bx
    pop ax
    ret

; cy_kindspd - AL = kind; out AX = the depth-units a frame that kind climbs.
;
; The trait table is in EIGHTHS of the level's base speed, so it is one
; multiply and one shift and no division. A fuseball is the fast one and a
; spiker the slow one, which is what makes meeting a new kind feel like a new
; problem rather than more of the same - and both unlock late (67.8), so the
; variety arrives with the difficulty.
cy_kindspd:
    push bx
    push cx
    push dx
    mov bl, al
    mov bh, 0
    cmp bx, CYE_KINDS
    jb .ok
    xor bx, bx
.ok:
    mov al, [cy_ekspd + bx]
    mov ah, 0
    mul word [cy_espd]
    mov cl, 3
    shr ax, cl                      ; / 8
    or ax, ax
    jnz .out
    mov ax, 1                       ; never zero: an enemy that cannot climb
.out:                               ; is one the wave can never be rid of
    pop dx
    pop cx
    pop bx
    ret

cy_clearboard:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, cy_e_kind               ; every enemy slot free
    mov cx, CY_MAXENEM
    mov al, 0xFF
    rep stosb
    mov di, cy_e_rtm                ; ...and its rim walk's phase with it
    mov cx, CY_MAXENEM
    xor al, al
    rep stosb
    ; ONE PASS PER ARRAY. These are five separate buffers and only the first
    ; two happen to be adjacent, so the single run this used to be cleared
    ; cy_s_act and then ran on through cy_s_lane, cy_s_pierce and half of
    ; cy_s_dp - while enemy shots, powerups and DEBRIS were never cleared at
    ; all. Debris is what showed: it is drawn by the play pass and moved only
    ; by the death pass, so the particles from one death sat on the web as
    ; motionless dots for the rest of the game.
    xor al, al
    mov di, cy_s_act
    mov cx, CY_MAXSHOT
    rep stosb
    mov di, cy_x_act
    mov cx, CY_MAXESHOT
    rep stosb
    mov di, cy_u_act
    mov cx, CY_MAXPU
    rep stosb
    mov di, cy_d_act
    mov cx, CY_MAXDBR
    rep stosb
    mov di, cy_spk
    mov cx, CY_MAXLANE * 2          ; cy_spk and cy_spkd are adjacent
    xor al, al
    rep stosb
    pop es
    ; AND FORGET WHAT IS ON THE GLASS. Freeing the slots above does not erase
    ; anything - the caller's full repaint does that - but a block still
    ; claiming shown=1 for a slot nothing renders any more is a PHANTOM: no
    ; pass will ever hide it, and cy_obj_dmg will faithfully "repair" it back
    ; onto the web every time something erases nearby. Eight dead debris
    ; particles from one death did exactly that, appearing and vanishing as
    ; dots for the rest of the game.
    call cy_obj_forget
    mov word [cy_plane], 0
    mov byte [cy_phome], 1          ; ...and cy_layout puts it at the BOTTOM
    mov word [cy_pjump], 0
    mov byte [cy_firereq], 0
    mov byte [cy_firecd], 0
    mov word [cy_dietim], 0
    ; ...AND THE ALIVE COUNT. This is what hung the game after a death: the
    ; enemies were freed here without ever being counted out, so cy_left kept
    ; the tally of whatever had been on the web when the player died, and
    ; cy_wave_ck - which needs cy_wleft AND cy_left at zero - could never
    ; fire again. The level simply never ended.
    mov word [cy_left], 0
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6)
;
; The game IS the worker task, for Arkanoid's reason (SPEC.md 44): an enemy
; has to keep climbing between keystrokes, and a window callback only runs
; when something happens to the window.
;
; It sleeps to a DEADLINE rather than for a duration, and MISSES a frame
; rather than chasing one: chasing the deadline IS the judder, because motion
; here is per-frame and a catch-up frame moves everything at double speed.
; =============================================================================
cy_worker:
    call OSAPI_GET_TICKS
    mov [cy_due], ax
.loop:
    mov bx, [cy_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4);
                                    ; if our close box was clicked this never
                                    ; returns and the task ends inside it
    inc word [cy_due]
    call OSAPI_GET_TICKS
    mov bx, [cy_due]
    sub bx, ax                      ; signed, and wrap-safe by subtraction
    jle .behind
    mov ax, bx
    call OSAPI_TASK_SLEEP
    jmp short .frame
.behind:
    mov [cy_due], ax                ; late: re-anchor rather than catch up
.frame:
    call cy_focus_ck
    call cy_update
    call cy_render
    jmp .loop

; --- cy_focus_ck --------------------------------------------------------------
; A real-time game is told when it GAINS the front and never when it loses it,
; so it has to ask - and it takes TWO questions (SPEC.md 12.6/44.8).
; OSAPI_WM_TOP answers who is frontmost; OSAPI_MENU_OWNER answers who the
; active APPLICATION is, and only the second catches a click on the bare
; desktop, which hands the menu bar to Locator without moving the z-order.
; Neither takes a lock, so both are legal from here.
; -----------------------------------------------------------------------------
cy_focus_ck:
    push ax
    push bx
    cmp byte [cy_inbr], 0
    jne .out                        ; inside a bracket we ARE the machine
    call OSAPI_WM_TOP
    cmp bx, [cy_win]
    jne .away
    call OSAPI_MENU_OWNER
    cmp bx, [cy_win]
    jne .away
    jmp short .out
.away:
    cmp byte [cy_state], CYS_PLAY
    jne .out
    mov byte [cy_state], CYS_PAUSE  ; sticky: a game that restarts the instant
    mov byte [cy_msgdirty], 1       ; a window is raised is one nobody was
.out:                               ; watching yet (SPEC.md 44.8)
    pop bx
    pop ax
    ret

; =============================================================================
; UPDATE - lock-free. It may not draw and it may not take the gfx lock.
; =============================================================================
cy_update:
    ; Preserves nothing: it is on the worker's spine (SPEC.md 67.5.5), and
    ; both callers reload every register they use on the next line.
    call cy_kbdrain                 ; EVERY state, not just play: it lived in
                                    ; cy_input, which cy_play_update alone
                                    ; calls, so the drain stopped at the game
                                    ; over screen and the machine died 18
                                    ; seconds in instead of 1 (SPEC.md 67.11.2)
    mov al, [cy_state]
    cmp al, CYS_TITLE
    je .title
    cmp al, CYS_PLAY
    je .play
    cmp al, CYS_DIE
    je .die
    cmp al, CYS_OVER
    je .over
    jmp short .out
.title:
    call cy_title_update
    jmp short .out
.play:
    call cy_play_update
    jmp short .out
.die:
    call cy_die_update
    jmp short .out
.over:
    call cy_over_update
.out:
    ret

; =============================================================================
; RENDER - takes the lock, arms the clip, and draws.
;
; Inside a fullscreen-exclusive bracket the lock is already held for the whole
; session and there is no clip region to arm (nothing can be on top of us), so
; both are a branch here rather than at every call site.
;
; IT PRESERVES NOTHING, AND NEITHER DOES ANYTHING ON THE DRAWING SPINE BELOW
; IT. That is SPEC.md 67.5.5 and it is a stack decision, not a style: a worker
; gets 256 BYTES (SPEC.md 8) and this app's spine is seven frames deep, so six
; routines each politely banking AX..DX cost 60 of them for nothing. Both
; callers - cy_worker's loop and the fullscreen bracket's - reload every
; register they use from memory on the next line, so there was never anything
; to preserve.
; =============================================================================
cy_render:
    cmp byte [cy_inbr], 0
    jne .draw                       ; the bracket holds the lock already

    call OSAPI_GFX_LOCK
    mov ax, KERNEL_SEG              ; A WORKER MUST LOAD ES ITSELF. Only a
    mov es, ax                      ; CALLBACK arrives with ES = KERNEL_SEG
                                    ; (SPEC.md 20.1); a worker is entered
                                    ; through the dispatcher and ES is
                                    ; whatever the last slot left. Without
                                    ; this the visible test reads our own
                                    ; image at W_FLAGS - it assembles, it
                                    ; runs, and the frame is silently skipped
    mov bx, [cy_win]
    test word [es:bx + W_FLAGS], 2  ; still visible?
    jz .skip
    call OSAPI_WM_CLIP_SET
    jc .skip                        ; not one pixel of us is visible
.draw:
    call cy_pen_forget              ; the hold starts here
    call cy_track
    call cy_home_ck
    cmp word [cy_nlane], 0
    je .small

    cmp byte [cy_full], 0
    je .nofull
    call cy_draw_all                ; ...and FALL THROUGH. A repaint used to
                                    ; end the frame here (SPEC.md 67.5.6), and
                                    ; cy_draw_all draws the web, the spikes,
                                    ; the HUD and the banner and NOT ONE
                                    ; MOVER - so every repaint was a frame
                                    ; with no enemies and no player in it, and
                                    ; a repaint on the target machine is ~200ms
.nofull:
    cmp byte [cy_pnon], 0
    jne .fin                        ; THE PANEL OWNS THE SCREEN. Without this
                                    ; the state render runs underneath it and
                                    ; cy_panel_draw puts the whole box back on
                                    ; top - a bed fill, a frame and a dozen
                                    ; opaque runs EVERY FRAME, which is
                                    ; PERFORMANCE.md's double-draw with the
                                    ; panel as the second layer
.parts:
    call cy_state_render
    jmp short .fin
.small:
    call cy_small_render
.fin:
    call cy_panel_draw              ; LAST and over everything, whatever the
                                    ; state drew underneath it
    cmp byte [cy_inbr], 0
    jne .out
    call OSAPI_GFX_UNLOCK
    jmp short .out
.skip:
    ; The update ran and the screen did not follow, so every "where it is
    ; DRAWN" rect is now a claim about pixels nobody can see. The next frame
    ; that CAN draw owes a whole one (Missile Command's skipped-frame rule).
    mov byte [cy_full], 1
    call OSAPI_GFX_UNLOCK
.out:
    ret


; --- cy_state_render ----------------------------------------------------------
; Whatever the current state draws on top of the web. Factored out of cy_render
; because BOTH repaint paths owe it: a frame that clears the field and does not
; run this hands back a picture with no movers in it at all (SPEC.md 67.5.6).
; Preserves nothing: it is on the drawing spine (SPEC.md 67.5.5).
; -----------------------------------------------------------------------------
cy_state_render:
    mov al, [cy_state]
    cmp al, CYS_TITLE
    je .rtitle
    cmp al, CYS_WARPIN
    je .rwarp
    cmp al, CYS_WARPOUT
    je .rwarpo
    cmp al, CYS_PLAY
    je .rplay
    cmp al, CYS_DIE
    je .rplay
    cmp al, CYS_OVER
    je .rover
    cmp al, CYS_PAUSE
    je .rpause
    ret
.rtitle:
    jmp cy_title_render
.rwarp:
    jmp cy_warpin_render
.rwarpo:
    jmp cy_warpout_render
.rover:
    jmp cy_over_render
.rpause:
    ; cy_play_render, not cy_msg_render. A full repaint forgets every mover
    ; (cy_obj_forget), and the movers are put back by the PLAY pass and
    ; nowhere else - so a paused game that got repainted (covered and
    ; uncovered, minimised and restored) would sit there with its web and its
    ; HUD and no enemies at all until the player unpaused. Nothing moves
    ; while paused, so every object takes cy_obj_show's did-not-move exit and
    ; the pass costs a compare each.
.rplay:
    jmp cy_play_render

; --- cy_track -----------------------------------------------------------------
; Poll the content box. There is no "your window moved" callback that fires on
; a DRAG, and everything in this app is derived from the box - so it is asked
; every frame, which is Missile Command's SPEC.md 44.8 pattern and costs two
; far calls against a 55ms tick.
;
; A MOVE matters as much as a resize here, and more subtly: the warp's walk
; blocks hold SCREEN coordinates, so a window that moved invalidates every one
; of them at once.
; -----------------------------------------------------------------------------
cy_track:
    push ax
    push bx
    push cx
    push dx
    call cy_org                     ; AX/DX = origin, CX/BX = size
    cmp ax, [cy_ox]
    jne .moved
    cmp dx, [cy_oy]
    jne .moved
    cmp cx, [cy_cwid]
    jne .moved
    cmp bx, [cy_chgt]
    je .same
.moved:
    mov byte [cy_needlay], 1
.same:
    cmp byte [cy_needlay], 0
    je .out
    call cy_layout                  ; refuses (CF=1) when the box is too small
    mov byte [cy_full], 1
    call cy_hudw_calc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; how many HUD cells this window can actually show
cy_hudw_calc:
    push ax
    push cx
    push dx
    mov ax, [cy_cwid]
    mov cl, 3
    shr ax, cl
    cmp ax, CY_HUDW
    jbe .ok
    mov ax, CY_HUDW
.ok:
    mov [cy_hudw], ax
    pop dx
    pop cx
    pop ax
    ret

; --- cy_draw_all --------------------------------------------------------------
; A whole content repaint, and the single invalidation point for everything
; that caches what is on the glass.
; -----------------------------------------------------------------------------
cy_draw_all:
    ; Preserves nothing: it is on the drawing spine (SPEC.md 67.5.5).
    mov byte [cy_full], 0

    mov al, CY_C_BG                 ; the field
    call cy_setcol
    xor ax, ax
    xor bx, bx
    mov cx, [cy_cwid]
    dec cx
    mov dx, [cy_chgt]
    dec dx
    call cy_fillc

    inc word [cy_nfull]             ; how many whole repaints this session -
                                    ; a rate rather than a total, and the one
                                    ; number that says whether the incremental
                                    ; paths are being used at all
    call cy_obj_forget              ; the fill took every mover with it
    mov byte [cy_pndirty], 1        ; ...and the panel, if one is up
    call cy_hud_clr                 ; ...and the status strip's shadow
    mov word [cy_msgp], 0           ; ...and the banner
    mov byte [cy_msgdirty], 1

    mov al, [cy_state]
    cmp al, CYS_TITLE
    je .title
    cmp al, CYS_WARPIN
    je .warp
    cmp al, CYS_WARPOUT
    je .warp

    ; PLAY, DIE, OVER and PAUSE all sit on a finished web
    call cy_web_all
    call cy_spk_all
    call cy_hud_draw
    call cy_msg_render
    jmp short .out
.warp:
    ; A warp interrupted by a repaint restarts: the walks are the only record
    ; of how far it got and they cannot be replayed from the middle onto a
    ; blank field. Restarting is a fifth of a second and is always correct.
    mov byte [cy_wstarted], 0
    mov byte [cy_wpha], CY_WARP_SPOKE
    call cy_hud_draw
    jmp short .out
.title:
    call cy_title_static
.out:
    ret

; cy_web_all - the finished web, drawn whole. Only a full repaint calls this,
; and it is ~180ms on the target machine: it is the cost the warp exists to
; avoid paying as a lump, and it is paid here because a repaint has no
; animation to hide it behind.
; THE ORDER IS THE WARP'S ORDER, and it has to be. The far ring and the first
; pixels of every spoke land on the same vertices, so whichever is drawn
; SECOND owns those pixels - and the two are different colours. Drawn the
; other way round this produced a small ring of differing pixels at the
; vanishing point whenever a repaint replaced what the warp had laid: correct
; both times, and not the same picture.
cy_web_all:
    push ax
    mov al, CY_C_WEBFAR
    call cy_web_far
    mov al, CY_C_WEB
    call cy_setcol
    mov al, CY_WARP_SPOKE
    call cy_warp_lay
    mov ax, 0x7FFF
    call cy_warp_step
    mov al, CY_WARP_RIM
    call cy_warp_lay
    mov ax, 0x7FFF
    call cy_warp_step
    pop ax
    ret

cy_spk_all:
    push ax
    push bx
    xor bx, bx
.each:
    cmp bx, [cy_nlane]
    jae .out
    cmp byte [cy_spk + bx], 0
    je .next
    push bx
    call cy_spk_draw
    pop bx
.next:
    inc bx
    jmp short .each
.out:
    pop bx
    pop ax
    ret

cy_small_render:
    push ax
    push cx
    push dx
    push si
    mov al, CY_C_BG
    call cy_setcol
    xor ax, ax
    xor bx, bx
    mov cx, [cy_cwid]
    dec cx
    mov dx, [cy_chgt]
    dec dx
    call cy_fillc
    mov si, cy_s_small
    mov cx, 2
    mov dx, 2
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc
    mov byte [cy_full], 0
    pop si
    pop dx
    pop cx
    pop ax
    ret

; cy_full_repaint - a whole repaint from a CALLBACK, where the gfx lock is
; ALREADY held. It must not go through cy_render: gfx_lock is not reentrant
; (SPEC.md 20.6 rule 4) and taking it here deadlocks the machine with the UI
; task inside our own paint.
cy_full_repaint:
    push ax
    push cx
    call cy_pen_forget              ; a callback's hold is somebody else's
    mov byte [cy_full], 1
    call cy_track
    call cy_home_ck
    cmp word [cy_nlane], 0
    je .small
    call cy_draw_all
    call cy_state_render            ; the movers, in the SAME frame - a
    jmp short .out                  ; W_PAINT owes a whole picture and this
.small:                             ; used to hand back one with nothing
    call cy_small_render            ; moving in it (SPEC.md 67.5.6)
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; PLAY (SPEC.md 67.8)
; =============================================================================

cy_play_update:
    ; The frame counter is bumped HERE and not in cy_update, because the only
    ; two things that read it - the AI droid's cadence and the powerup's
    ; pulse - are play-only, and a counter that kept running while paused made
    ; a paused screen change under a player who had stopped it.
    inc word [cy_frame]
    cmp byte [cy_firecd], 0
    je .cd
    dec byte [cy_firecd]
.cd:
    call cy_input
    call cy_player_move
    call cy_enemies_update
    call cy_shots_update
    call cy_eshots_update
    call cy_pu_update
    call cy_spawn_tick
    call cy_droid_tick
    call cy_laser_tick
    call cy_wave_ck
    ret

; --- cy_input -----------------------------------------------------------------
; The arrows a player HOLDS come from the key-state map (SPEC.md 9.7), never
; from int 16h: int 16h has no key-up, so a held arrow is a typematic stream
; with a delay before the first repeat - the paddle stutter SPEC.md 44.2
; measures and Arkanoid shipped twice before diagnosing. This call takes no
; lock and touches no port, so it is legal from the worker.
; -----------------------------------------------------------------------------
cy_input:
    push ax
    push bx
    mov word [cy_dir], 0
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jnc .r
    mov word [cy_dir], -1
.r:
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jnc .wind
    mov word [cy_dir], 1
.wind:
    ; ON A CLOSED WEB THE LANE INDEX IS NOT A SCREEN DIRECTION (SPEC.md
    ; 67.16). The descriptors wind clockwise - lane 0 at 3 o'clock, then down
    ; and round - so a step of +1 moves RIGHT at the top of the ring and LEFT
    ; at the bottom, and the claw lives at the bottom: measured, it sits at
    ; content (222,115) in a 320x130 box, which is the near rim. So the arrows
    ; read backwards for the whole of where the game is played, and that is
    ; what was reported.
    ;
    ; The near rim is the anchor, so the sign is flipped there and nowhere
    ; else. An OPEN web (flat, vee, uu) needs no flip at all: its vertices run
    ; left to right, so +1 already means rightwards, and flipping it globally
    ; would break those exactly as badly.
    ;
    ; A tangent-following rule was considered and is wrong: at the left and
    ; right extremes of a ring the tangent is vertical, so "the neighbour with
    ; the greater x" cannot say which way to go, and holding a key bounces the
    ; claw between two lanes instead of carrying it round.
    cmp word [cy_closed], 0
    je .fire
    neg word [cy_dir]
.fire:
    ; FIRE IS A LEVEL TOO, and for the same reason the arrows are (SPEC.md
    ; 9.7): int 16h repeats only the LAST key pressed, so holding space and
    ; then touching an arrow stopped the repeats and the gun went quiet until
    ; space was released and pressed again. The map has no such rule - it
    ; answers about every key at once - so holding both works.
    ;
    ; The typed space in cy_key_common is kept as well, and cannot double up:
    ; both paths go through cy_fire's cooldown. That matters because
    ; OSAPI_KEY_DOWN is advice rather than an oracle - a break code lost in a
    ; long interrupts-off window leaves a key reading down - and a machine
    ; where the map never worked at all would otherwise have no gun.
    mov al, KSC_SPACE
    call OSAPI_KEY_DOWN
    jnc .done
    mov byte [cy_firereq], 1
.done:
    pop bx
    pop ax
    ret

; --- cy_kbdrain ---------------------------------------------------------------
; Drop the surplus typematic of the three keys read above AS STATE.
;
; A held key still goes into the BIOS's 16-entry buffer at ~10 a second, and
; os8088's UI task takes exactly ONE key per pass (kernel/ui.inc), dispatching
; each through gfx_lock and W_ONKEY. Two keys held is ~20 a second against a
; pass rate this game's own lock holds put near 18, so the buffer FILLS - and
; a full buffer makes the BIOS BEEP.
;
; That beep is the bug, not the noise. It is `call F000:E8C0`, two `loop $`
; delays run with INTERRUPTS ENABLED, so every IRQ1 that arrives inside it
; nests another int 09h - and another beep - on whichever task stack happens
; to be current. A worker gets 256 BYTES (SPEC.md 8). Measured on a
; cycle-accurate 5150: the buffer went 0 -> 9 -> 15 pending inside one second
; of held arrow-plus-fire, and the machine halted in sch_stkdie with a stack
; holding SEVEN nested copies of the same 26-byte beep frame. It reads as a
; hang in the game and it is a keyboard buffer nobody emptied.
;
; THERE IS NO SAFE THRESHOLD, which is the part worth writing down. The first
; version drained only above a high-water mark, on the reasoning that the
; buffer merely had to be kept off full; measured, that lasted 1.5 seconds
; instead of 1.0. The delay is `mov ch, 0x20` + `loop $` TWICE with CL left
; over, so a beep is up to ~60ms - LONGER than the ~50ms gap between two held
; keys - and once one beep starts every following key arrives inside it. The
; nesting is not something a margin can absorb: the buffer must never fill at
; all, so every repeat of these three keys is taken every time we look.
;
; Nothing is lost by taking them:
;
;   - the arrows do nothing in cy_key_common at all (its `.noascii` handles
;     Enter and returns), because they are read as state;
;   - a space raises [cy_firereq] here, which is exactly what cy_key_common
;     would have done with it, and cy_fire's cooldown dedupes it against the
;     state read - so the typed-space fallback moves INTO the drain rather
;     than being given up;
;   - the head is examined and left alone if it is anything else, so Esc, P,
;     F, N, Z, J and Enter always reach W_ONKEY untouched. It stops there too:
;     taking a key from behind one it left would deliver them out of order.
;
; The peek and the fetch are ONE IF=0 window, or the UI task takes the key
; between them and this eats the one after it. Both callers are task context -
; the worker through cy_input, and cy_onkey, which the UI task reaches once
; per pass - so the drain rate tracks the arrival rate from both sides.
;
; Preserves everything but the flags.
; -----------------------------------------------------------------------------
cy_kbdrain:
    push ax
    push es
    mov ax, 0x0040                  ; the BIOS data area, for int 16h's own
    mov es, ax                      ; buffer - head 001A, tail 001C
.again:
    mov ax, [es:0x1A]
    cmp ax, [es:0x1C]
    je .out                         ; empty: the common case, and no int at all
    pushf
    cli
    mov ah, 0x01                    ; peek: AH = scan, and the key stays put
    int 0x16
    jz .pop                         ; emptied under us after all
    cmp ah, KSC_LEFT
    je .eat
    cmp ah, KSC_RIGHT
    je .eat
    cmp ah, KSC_SPACE
    jne .pop
    mov byte [cy_firereq], 1        ; the typed-space fallback, kept
.eat:
    xor ah, ah
    int 0x16
    popf
    jmp short .again
.pop:
    popf
.out:
    pop es
    pop ax
    ret

; --- cy_player_move -----------------------------------------------------------
; The claw slides along the rim. It accelerates while a key is held and the
; ramp RESETS on a direction change, so a correction is precise and a long
; sweep is fast - the same shape SPEC.md 9.6.3 uses for the mouseless pointer.
; -----------------------------------------------------------------------------
cy_player_move:
    push ax
    push bx
    push cx
    push dx

    cmp word [cy_pjump], 0          ; a jump keeps its lane
    je .ground
    dec word [cy_pjump]
.ground:
    mov ax, [cy_dir]
    or ax, ax
    jz .stop
    cmp ax, [cy_lastdir]
    je .ramp
    mov word [cy_accel], 0          ; a change of direction starts again
.ramp:
    mov [cy_lastdir], ax
    inc word [cy_accel]
    mov bx, [cy_accel]
    cmp bx, 4
    jae .move
    ; the first three frames of a press move one lane and then wait, so a tap
    ; is exactly one lane and a hold runs
    cmp bx, 1
    jne .out
.move:
    mov ax, [cy_plane]
    add ax, [cy_dir]
    call cy_wrap
    mov [cy_plane], ax
    jmp short .out
.stop:
    mov word [cy_accel], 0
    mov word [cy_lastdir], 0
.out:
    cmp byte [cy_firereq], 0
    je .nf
    mov byte [cy_firereq], 0
    call cy_fire
.nf:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_aim_mouse -------------------------------------------------------------
; The trackball of the original, in the only form this machine has: the
; pointer's position about the tube's centre picks the lane. It compares
; against every lane's rim midpoint rather than doing any trigonometry - at
; sixteen lanes that is sixteen compares and no arctangent, and it is correct
; for the non-circular webs where an angle would not be.
; -----------------------------------------------------------------------------
cy_aim_mouse:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [cy_nlane], 0
    je .out
    call OSAPI_MOUSE                ; CX/DX = screen x/y
    sub cx, [cy_ox]
    sub dx, [cy_oy]
    mov [cy_tmpx], cx
    mov [cy_tmpy], dx
    mov word [cy_tmpbest], 0x7FFF
    xor si, si
.each:
    cmp si, [cy_nlane]
    jae .out
    mov ax, si
    mov ah, CY_TOPD
    call cy_lanepos                 ; CX/DX = that lane's own spot on the lip
    sub cx, [cy_tmpx]
    jns .ax
    neg cx
.ax:
    sub dx, [cy_tmpy]
    jns .ay
    neg dx
.ay:
    add cx, dx                      ; Manhattan distance: an ordering, not a
    cmp cx, [cy_tmpbest]            ; measurement, and the cheapest one that
    jae .next                       ; gets the ordering right
    mov [cy_tmpbest], cx
    mov [cy_plane], si
.next:
    inc si
    jmp short .each
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_fire ------------------------------------------------------------------
cy_fire:
    cmp byte [cy_firecd], 0
    jne .nope                       ; one gun, one rate, whichever path asked
    push ax
    push bx
    push cx
    push si
    mov byte [cy_firecd], CY_FIRECD
    xor si, si
.find:
    cmp si, CY_MAXSHOT
    jae .out
    cmp byte [cy_s_act + si], 0
    je .got
    inc si
    jmp short .find
.got:
    mov byte [cy_s_act + si], 1
    mov ax, [cy_plane]
    mov [cy_s_lane + si], al
    mov bx, si
    shl bx, 1
    mov word [cy_s_dp + bx], CY_TOPD * 256
    mov al, 0
    cmp word [cy_pw_laser], 0
    je .np
    mov al, 1                       ; the particle laser pierces: the shot
.np:                                ; survives its first kill
    mov [cy_s_pierce + si], al
    mov al, CYSFX_FIRE              ; a short blip - the arcade's fire sound
    call cy_sfx
.out:
    pop si
    pop cx
    pop bx
    pop ax
.nope:
    ret

; --- cy_superzap --------------------------------------------------------------
; Tempest's superzapper: the first use kills everything on the web, and the
; charge is spent. Powerups grant more.
; -----------------------------------------------------------------------------
cy_superzap:
    push ax
    push bx
    push cx
    push si
    cmp byte [cy_state], CYS_PLAY
    jne .out
    cmp byte [cy_zap], 0
    je .out
    dec byte [cy_zap]
    xor si, si
.each:
    cmp si, CY_MAXENEM
    jae .done
    cmp byte [cy_e_kind + si], 0xFF
    je .next
    mov al, [cy_e_kind + si]
    call cy_score_kind
    mov byte [cy_e_kind + si], 0xFE ; 0FEh = dead but still on the glass. The
                                    ; update may not draw, so "dead" and
                                    ; "erased" are two states one frame apart
    dec word [cy_left]              ; the ALIVE tally, not the still-to-spawn
                                    ; one - decrementing cy_wleft here took it
                                    ; below zero, where it wrapped to 65535
                                    ; and the wave never ended
.next:
    inc si
    jmp short .each
.done:
    mov al, CYSFX_ZAP
    call cy_sfx
    mov byte [cy_huddirty], 1
    mov si, cy_s_superzap
    call cy_msg_set
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- cy_do_jump ---------------------------------------------------------------
; Tempest 2000's jump: the claw hops off the rim for a few frames and nothing
; on the web can reach it.
; -----------------------------------------------------------------------------
cy_do_jump:
    cmp byte [cy_state], CYS_PLAY
    jne .out
    cmp byte [cy_pw_jump], 0
    je .out
    cmp word [cy_pjump], 0
    jne .out
    dec byte [cy_pw_jump]
    mov word [cy_pjump], 12
    push ax
    push cx
    push dx
    mov al, CYSFX_JUMP
    call cy_sfx
    pop dx
    pop cx
    pop ax
    mov byte [cy_huddirty], 1
.out:
    ret

; --- cy_spawn_tick ------------------------------------------------------------
; Feed the wave in. The interval shortens with the level, which together with
; cy_wavesize's count is the whole difficulty ramp.
; -----------------------------------------------------------------------------
cy_spawn_tick:
    push ax
    push bx
    push cx
    push dx
    cmp word [cy_wleft], 0
    je .out
    inc word [cy_spawnt]
    mov ax, 30
    mov bx, [cy_level]
    cmp bx, 12
    jbe .lv
    mov bx, 12
.lv:
    sub ax, bx
    sub ax, bx                      ; 28 frames at L1 down to 6 at L12+
    cmp word [cy_spawnt], ax
    jb .out
    mov word [cy_spawnt], 0
    call cy_spawn_one
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cy_spawn_one:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.find:
    cmp si, CY_MAXENEM
    jae .out
    cmp byte [cy_e_kind + si], 0xFF
    je .got
    inc si
    jmp short .find
.got:
    call OSAPI_RAND
    xor dx, dx
    mov bx, [cy_nlane]
    div bx
    mov [cy_e_lane + si], dl

    call OSAPI_RAND                 ; the kind, from those this level unlocks
    xor dx, dx
    mov bl, [cy_kinds]
    mov bh, 0
    div bx
    mov [cy_e_kind + si], dl

    mov bx, si
    shl bx, 1
    mov word [cy_e_dp + bx], CY_FARD * 256   ; born just inside the far ring
    mov al, [cy_e_kind + si]
    call cy_kindspd                 ; AX = this kind's own climb speed
    mov [cy_e_sp + bx], ax
    mov byte [cy_e_tim + si], 0
    dec word [cy_wleft]
    inc word [cy_left]
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_enemies_update --------------------------------------------------------
cy_enemies_update:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.each:
    cmp si, CY_MAXENEM
    jae .out
    mov al, [cy_e_kind + si]
    cmp al, 0xFE
    jae .next                       ; free, or dead awaiting its erase
    mov bx, si
    shl bx, 1

    ; climb
    mov ax, [cy_e_dp + bx]
    add ax, [cy_e_sp + bx]
    cmp ax, CY_TOPD * 256
    jb .store
    mov ax, CY_TOPD * 256
.store:
    mov [cy_e_dp + bx], ax

    ; kind behaviour
    mov al, [cy_e_kind + si]
    cmp al, CYE_FLIPPER
    je .flip
    cmp al, CYE_SPIKER
    je .spike
    cmp al, CYE_FUSEBALL
    je .flip                        ; a fuseball walks the web too, faster
    cmp al, CYE_PULSAR
    je .pulse
    jmp short .atrim
.flip:
    ; flippers change lane on a timer, and faster as they near the rim
    inc byte [cy_e_tim + si]
    mov al, [cy_e_tim + si]
    cmp al, 10
    jb .atrim
    mov byte [cy_e_tim + si], 0
    call OSAPI_RAND
    test al, 1
    jz .fl
    mov al, [cy_e_lane + si]
    mov ah, 0
    inc ax
    call cy_wrap
    mov [cy_e_lane + si], al
    jmp short .atrim
.fl:
    mov al, [cy_e_lane + si]
    mov ah, 0
    dec ax
    call cy_wrap
    mov [cy_e_lane + si], al
    jmp short .atrim
.spike:
    ; a spiker builds its lane's spike up behind it as it climbs
    mov bx, si
    shl bx, 1
    mov ax, [cy_e_dp + bx]
    mov cl, 8
    shr ax, cl                      ; AX = its depth
    mov bl, [cy_e_lane + si]
    mov bh, 0
    cmp al, [cy_spk + bx]
    jbe .atrim
    cmp al, CY_DEPTH - 2            ; a spike stops short of the rim polygon
    ja .atrim
    mov [cy_spk + bx], al
    mov byte [cy_spkd + bx], 1
    jmp short .atrim
.pulse:
    ; a pulsar electrifies its lane on a cycle; the player dies on that lane
    inc byte [cy_e_tim + si]
    mov al, [cy_e_tim + si]
    cmp al, 18
    jb .atrim
    mov byte [cy_e_tim + si], 0
.atrim:
    ; reached the rim?
    mov bx, si
    shl bx, 1
    cmp word [cy_e_dp + bx], CY_TOPD * 256
    jb .next

    ; AT THE RIM IT WALKS THE RIM TOWARDS YOU (SPEC.md 67.18). It used to
    ; clamp its depth here and do nothing else ever again: a flipper wandered
    ; because .flip runs at every depth, but a tanker, a spiker or a pulsar
    ; that arrived simply PARKED, firing down its own lane now and then. They
    ; accumulate, and a cluster of parked enemies on the rim is what was
    ; reported as things sticking around forever. The arcade's rim is where an
    ; enemy becomes the threat that ends the standoff, not furniture.
    inc byte [cy_e_rtm + si]
    mov al, [cy_e_rtm + si]
    cmp al, CY_RIMSTEP
    jb .hitck
    mov byte [cy_e_rtm + si], 0
    call cy_rim_step
.hitck:
    mov al, [cy_e_lane + si]
    cmp al, [cy_plane]
    jne .fire
    cmp word [cy_pjump], 0
    jne .fire                       ; jumped clear of it
    call cy_player_hit
    jmp short .next
.fire:
    ; an enemy at the rim shoots down its own lane
    call OSAPI_RAND
    and al, 31
    jnz .next
    mov al, [cy_e_lane + si]
    call cy_eshot_fire
.next:
    inc si
    jmp .each
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cy_rim_step --------------------------------------------------------------
; Move enemy SI one lane along the rim towards the player, the short way round.
;
; The short way is what makes it a threat rather than a coin toss: on a closed
; web the two directions differ, and an enemy that picks at random takes twice
; as long on average and sometimes walks away. d = (player - mine) mod n, and
; the step is +1 while d is in the near half.
; -----------------------------------------------------------------------------
cy_rim_step:
    push ax
    push bx
    push cx
    push dx
    mov cx, [cy_nlane]
    or cx, cx
    jz .out
    mov al, [cy_e_lane + si]
    mov ah, 0
    mov bx, [cy_plane]
    sub bx, ax                      ; d = player - mine
    jns .pos
    add bx, cx                      ; ...mod n, and n is small so one add does
.pos:
    or bx, bx
    jz .out                         ; already on the player's lane
    cmp word [cy_closed], 0
    je .open
    mov dx, cx
    shr dx, 1
    cmp bx, dx
    ja .down                        ; the far half: go the other way round
.up:
    inc ax
    call cy_wrap
    mov [cy_e_lane + si], al
    jmp short .out
.down:
    dec ax
    call cy_wrap
    mov [cy_e_lane + si], al
    jmp short .out
.open:
    ; an open web has no short way: just close the gap
    cmp bx, 0
    jg .up
    jmp short .down
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; WEB REPAIR (SPEC.md 67.19)
;
; A mover is bounded to a third of its lane's span (67.5.2) and that is a
; compromise, not a guarantee: the corner of an axis-aligned rect still reaches
; the spoke now and then, and the erase takes the web's pixels with it. The web
; is drawn ONCE and nothing puts them back, so the damage accumulates - measured
; against a forced repaint after 45 seconds of play, 116 pixels in 17 small
; clusters spread over the whole tube.
;
; Same shape as the spike repair, for the same reason: mark cheaply where the
; damage can be, and spend the marks on a BUDGET so a busy frame cannot turn
; into a redraw. A lane's two bounding spokes and the rim edge between them are
; three OSAPI_GFX_LINE calls, and CY_WEBREP of them a frame is the bound.
;
; The mark is per LANE and is taken at the sites that already know one - every
; cy_spk_mark caller is a mover with its lane in AL - so nothing new has to be
; threaded anywhere. It marks whether or not it really cut anything, which is
; why the budget matters: a wrong mark costs three lines eventually, and a
; missed one costs pixels for ever.
;
; WHAT IT IS NOT IS A FIX FOR SOMETHING THAT SITS ON THE WEB (67.5.3.1). A
; sweep at one lane every fourth frame is a few hundred milliseconds behind
; the damage and costs three line walks when it fires; it is affordable
; because it is cleaning up after the occasional corner of a rect. The claw
; used to be drawn ON the rim edge for the whole of a flat, star, plus or vee
; level - damage on every lane, on every frame it moved - and no budget makes
; that look right. That is a GEOMETRY bug and cy_lip_build is where it is
; fixed; nothing here was made to work harder for it.
; =============================================================================
CY_WEBREP   equ 1                   ; lanes repaired per repair frame...
CY_WEBEVERY equ 4                   ; ...and only every 4th frame is one.
                                    ; Two spokes and a rim edge is ~90 line
                                    ; pixels plus three arrivals - about 16ms
                                    ; on the target machine, which is a third
                                    ; of a frame for a cosmetic repair. At a
                                    ; quarter of the frames it is ~4ms
                                    ; amortised and a 16-lane web still sweeps
                                    ; clean in about three seconds. Redrawing
                                    ; an undamaged lane is invisible rather
                                    ; than a flicker - same pixels, same pen -
                                    ; so an over-eager mark costs time and
                                    ; never looks wrong.

; cy_web_mark - lane AL may have lost web pixels. Preserves everything.
cy_web_mark:
    push bx
    mov bl, al
    mov bh, 0
    cmp bx, CY_MAXLANE
    jae .out
    mov byte [cy_wdmg + bx], 1
.out:
    pop bx
    ret

; cy_web_repair - spend up to CY_WEBREP marks, round robin so no lane starves.
cy_web_repair:
    push ax
    push bx
    push cx
    push dx
    push si
    test word [cy_frame], CY_WEBEVERY - 1
    jnz .out
    mov cx, [cy_nlane]
    or cx, cx
    jz .out
    mov dx, CY_WEBREP
.each:
    mov bx, [cy_wnext]
    inc word [cy_wnext]
    mov ax, [cy_wnext]
    cmp ax, cx
    jb .in
    mov word [cy_wnext], 0
.in:
    cmp bx, cx
    jae .next
    cmp byte [cy_wdmg + bx], 0
    je .next
    mov byte [cy_wdmg + bx], 0
    call cy_web_lane
    dec dx
    jz .out
.next:
    loop .each
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_web_lane - redraw lane BX's two spokes and the rim edge between them.
;
; IT USES THE WARP'S OWN WALKER, not OSAPI_GFX_LINE, and that is the whole
; correctness argument. The web is drawn by SPEC.md 5.6.7's resumable walk and
; gfx_line is a different rasteriser - measured, repairing with gfx_line laid
; its line a pixel off the original in places, so the screen GAINED 86 pixels
; the web does not have while still missing some it does. A repair has to be
; the same walk over the same endpoints, which is what cy_web_all already does.
;
; The warp is not running during play, so its walk blocks are free; three are
; laid here and stepped to completion in one go. [cy_wstarted] is cleared
; afterwards so a later warp re-lays from scratch rather than inheriting these.
cy_web_lane:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; cy_walk_one builds through ES
    mov di, bx                      ; the lane
    mov al, CY_C_WEB
    call cy_setcol
    mov word [cy_wn], 0

    mov bx, di                      ; the spoke at this lane's own vertex
    call cy_web_spoke
    mov ax, di                      ; ...and the next one round
    call cy_web_next
    mov bx, ax
    call cy_web_spoke

    mov ax, CY_DEPTH                ; the rim edge between the two
    mov bx, di
    call cy_vert
    mov [cy_wx1], cx
    mov [cy_wy1], dx
    mov ax, di
    call cy_web_next
    mov bx, ax
    mov ax, CY_DEPTH
    call cy_vert
    mov [cy_wx2], cx
    mov [cy_wy2], dx
    call cy_walk_one
    inc word [cy_wn]

    mov ax, 0x7FFF                  ; ...and spend all three at once
    call cy_warp_step
    mov byte [cy_wstarted], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_web_next - AX = the vertex after AX, wrapping on a closed web.
cy_web_next:
    inc ax
    cmp word [cy_closed], 0
    je .out
    cmp ax, [cy_nvert]
    jb .out
    xor ax, ax
.out:
    ret

; cy_web_spoke - lay the spoke at vertex BX, far end to rim.
cy_web_spoke:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx
    xor ax, ax
    call cy_vert                    ; the far end
    mov [cy_wx1], cx
    mov [cy_wy1], dx
    mov ax, CY_DEPTH
    mov bx, si
    call cy_vert                    ; ...and the rim end
    mov [cy_wx2], cx
    mov [cy_wy2], dx
    call cy_walk_one
    inc word [cy_wn]
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; --- cy_shots_update ----------------------------------------------------------
cy_shots_update:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    cmp si, CY_MAXSHOT
    jae .out
    cmp byte [cy_s_act + si], 1
    jne .next
    mov bx, si
    shl bx, 1
    mov ax, [cy_s_dp + bx]
    sub ax, 380                     ; shots run down the tube fast
    cmp ax, CY_FARD * 256
    jge .live
    mov byte [cy_s_act + si], 2     ; spent: erase next frame
    ; a shot that reaches the far end takes a segment off that lane's spike
    mov bl, [cy_s_lane + si]
    mov bh, 0
    cmp byte [cy_spk + bx], 0
    je .next
    dec byte [cy_spk + bx]
    mov byte [cy_spkd + bx], 1
    jmp short .next
.live:
    mov [cy_s_dp + bx], ax
    call cy_shot_hit
.next:
    inc si
    jmp short .each
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_shot_hit - does shot SI overlap an enemy? Same lane, and within half a
; depth step - which is the whole collision model, and it is exact because
; both live on the same integer lane/depth grid.
cy_shot_hit:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, si
    shl bx, 1
    mov cx, [cy_s_dp + bx]          ; the shot's depth
    mov dl, [cy_s_lane + si]
    xor di, di
.each:
    cmp di, CY_MAXENEM
    jae .out
    cmp byte [cy_e_kind + di], 0xFE
    jae .next
    cmp dl, [cy_e_lane + di]
    jne .next
    mov bx, di
    shl bx, 1
    mov ax, [cy_e_dp + bx]
    sub ax, cx
    jns .abs
    neg ax
.abs:
    cmp ax, 300
    ja .next
    ; hit
    mov al, [cy_e_kind + di]
    call cy_score_kind
    call cy_maybe_drop
    cmp byte [cy_e_kind + di], CYE_TANKER
    jne .bang
    call cy_tanker_split            ; a tanker splits into two flippers
    mov al, CYSFX_SPLIT             ; ...which is not a kill, and must not
    jmp short .say                  ; sound like one - there are still two
.bang:                              ; things coming at you
    mov al, CYSFX_KILL
.say:
    call cy_sfx
.kill:
    mov byte [cy_e_kind + di], 0xFE
    dec word [cy_left]
    mov byte [cy_huddirty], 1
    cmp byte [cy_s_pierce + si], 0
    jne .out                        ; the particle laser carries on through
    mov byte [cy_s_act + si], 2
    jmp short .out
.next:
    inc di
    jmp short .each
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_score_kind - AL = the kind that died. The arcade's values, times the
; level multiplier capped at six.
cy_score_kind:
    push ax
    push bx
    push cx
    push dx
    mov bl, al
    mov bh, 0
    mov ax, [cy_kindsc + bx]
    mov bx, [cy_level]
    inc bx
    shr bx, 1
    cmp bx, 6
    jbe .m
    mov bx, 6
.m:
    or bx, bx
    jnz .mul
    mov bx, 1
.mul:
    mul bx
    call cy_score_add
    mov byte [cy_huddirty], 1
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cy_tanker_split:
    push ax
    push bx
    push cx
    push si
    push di
    mov cx, 2
.two:
    xor si, si
.find:
    cmp si, CY_MAXENEM
    jae .next
    cmp byte [cy_e_kind + si], 0xFF
    je .got
    inc si
    jmp short .find
.got:
    mov byte [cy_e_kind + si], CYE_FLIPPER
    mov al, [cy_e_lane + di]
    mov ah, 0
    add ax, cx
    dec ax                          ; one either side
    call cy_wrap
    mov [cy_e_lane + si], al
    mov bx, si
    shl bx, 1
    push bx
    mov bx, di
    shl bx, 1
    mov ax, [cy_e_dp + bx]
    pop bx
    mov [cy_e_dp + bx], ax
    mov al, CYE_FLIPPER
    call cy_kindspd
    mov [cy_e_sp + bx], ax
    mov byte [cy_e_tim + si], 0
    inc word [cy_left]
.next:
    loop .two
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- enemy shots --------------------------------------------------------------
cy_eshot_fire:
    push ax
    push bx
    push si
    mov ah, al                      ; the lane
    xor si, si
.find:
    cmp si, CY_MAXESHOT
    jae .out
    cmp byte [cy_x_act + si], 0
    je .got
    inc si
    jmp short .find
.got:
    mov byte [cy_x_act + si], 1
    mov [cy_x_lane + si], ah
    mov bx, si
    shl bx, 1
    mov word [cy_x_dp + bx], CY_FARD * 256   ; from the far end, climbing at us
.out:
    pop si
    pop bx
    pop ax
    ret

cy_eshots_update:
    push ax
    push bx
    push si
    xor si, si
.each:
    cmp si, CY_MAXESHOT
    jae .out
    cmp byte [cy_x_act + si], 1
    jne .next
    mov bx, si
    shl bx, 1
    mov ax, [cy_x_dp + bx]
    add ax, 200
    cmp ax, CY_TOPD * 256
    jb .live
    mov byte [cy_x_act + si], 2
    mov al, [cy_x_lane + si]
    cmp al, [cy_plane]
    jne .next
    cmp word [cy_pjump], 0
    jne .next
    call cy_player_hit
    jmp short .next
.live:
    mov [cy_x_dp + bx], ax
.next:
    inc si
    jmp short .each
.out:
    pop si
    pop bx
    pop ax
    ret

; --- powerups (SPEC.md 67.9) --------------------------------------------------
; Tempest 2000's four, and they RAMP: the early levels drop only the two that
; keep you alive (a zapper charge, a jump), and the two that make you stronger
; - the particle laser and the AI droid - unlock at 4 and 6. That progression
; is the reward curve, and it is why a level-10 player feels different from a
; level-1 one rather than merely busier.
; -----------------------------------------------------------------------------
cy_maybe_drop:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_RAND
    and ax, 7
    jnz .out                        ; one kill in eight
    xor si, si
.find:
    cmp si, CY_MAXPU
    jae .out
    cmp byte [cy_u_act + si], 0
    je .got
    inc si
    jmp short .find
.got:
    call OSAPI_RAND                 ; which kinds this level offers
    mov bl, 2                       ; zapper charge and jump
    cmp word [cy_level], 4
    jb .k
    mov bl, 3                       ; + the particle laser
    cmp word [cy_level], 6
    jb .k
    mov bl, 4                       ; + the AI droid
.k:
    mov bh, 0
    xor dx, dx
    div bx
    ; map 0,1 -> ZAP,JUMP and 2,3 -> LASER,DROID
    mov al, CYP_ZAP
    cmp dl, 0
    je .have
    mov al, CYP_JUMP
    cmp dl, 1
    je .have
    mov al, CYP_LASER
    cmp dl, 2
    je .have
    mov al, CYP_DROID
.have:
    mov [cy_u_kind + si], al
    mov byte [cy_u_act + si], 1
    mov al, [cy_e_lane + di]
    mov [cy_u_lane + si], al
    mov bx, si
    shl bx, 1
    push bx
    mov bx, di
    shl bx, 1
    mov ax, [cy_e_dp + bx]
    pop bx
    mov [cy_u_dp + bx], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cy_pu_update:
    push ax
    push bx
    push si
    xor si, si
.each:
    cmp si, CY_MAXPU
    jae .out
    cmp byte [cy_u_act + si], 1
    jne .next
    mov bx, si
    shl bx, 1
    mov ax, [cy_u_dp + bx]
    add ax, 90                      ; a pickup drifts out toward the lip
    cmp ax, CY_TOPD * 256
    jb .live
    mov byte [cy_u_act + si], 2     ; reached the rim
    mov al, [cy_u_lane + si]
    cmp al, [cy_plane]
    jne .next
    mov al, [cy_u_kind + si]
    call cy_pu_take
    jmp short .next
.live:
    mov [cy_u_dp + bx], ax
.next:
    inc si
    jmp short .each
.out:
    pop si
    pop bx
    pop ax
    ret

cy_pu_take:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    mov bh, 0
    cmp al, CYP_LASER
    jne .d
    add word [cy_pw_laser], 300     ; frames of pierce
    jmp short .say
.d:
    cmp al, CYP_DROID
    jne .j
    add word [cy_pw_droid], 300
    jmp short .say
.j:
    cmp al, CYP_JUMP
    jne .z
    cmp byte [cy_pw_jump], 8
    jae .say
    inc byte [cy_pw_jump]
    jmp short .say
.z:
    cmp byte [cy_zap], 3
    jae .say
    inc byte [cy_zap]
.say:
    shl bx, 1
    mov si, [cy_pu_names + bx]
    call cy_msg_set
    mov ax, 500
    call cy_score_add
    mov byte [cy_huddirty], 1
    mov al, CYSFX_PU
    call cy_sfx
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the AI droid: an autonomous second gun two lanes over, firing on its own
cy_droid_tick:
    push ax
    push si
    cmp word [cy_pw_droid], 0
    je .out
    dec word [cy_pw_droid]
    jnz .live
    mov byte [cy_huddirty], 1
.live:
    test word [cy_frame], 7
    jnz .out
    mov ax, [cy_plane]
    add ax, 2
    call cy_wrap
    push ax
    xor si, si
.find:
    cmp si, CY_MAXSHOT
    jae .none
    cmp byte [cy_s_act + si], 0
    je .got
    inc si
    jmp short .find
.got:
    pop ax
    mov byte [cy_s_act + si], 1
    mov [cy_s_lane + si], al
    mov bx, si
    shl bx, 1
    mov word [cy_s_dp + bx], CY_TOPD * 256
    mov byte [cy_s_pierce + si], 0
    jmp short .out
.none:
    pop ax
.out:
    pop si
    pop ax
    ret

; the particle laser's life
cy_laser_tick:
    cmp word [cy_pw_laser], 0
    je .out
    dec word [cy_pw_laser]
    jnz .out
    mov byte [cy_huddirty], 1
.out:
    ret

; --- cy_wave_ck ---------------------------------------------------------------
cy_wave_ck:
    cmp word [cy_wleft], 0
    jne .out
    cmp word [cy_left], 0
    jne .out
    ; the level is clear: fly down the tube
    push ax
    mov ax, [cy_level]
    mov bx, 100
    mul bx
    call cy_score_add
    pop ax
    mov byte [cy_state], CYS_WARPOUT
    mov byte [cy_wpha], CY_WARP_RIM
    mov byte [cy_wstarted], 0
    mov si, cy_s_bonus
    call cy_msg_set
.out:
    ret

; --- cy_player_hit ------------------------------------------------------------
cy_player_hit:
    cmp byte [cy_state], CYS_PLAY
    jne .out
    push ax
    push cx
    push dx
    mov byte [cy_state], CYS_DIE
    mov word [cy_dietim], 24
    call cy_debris_burst
    mov byte [cy_dieph], 0          ; the sweep is stepped by cy_die_update:
    call cy_die_sfx                 ; a single blip is what a burst of fire
                                    ; walked straight over
    pop dx
    pop cx
    pop ax
.out:
    ret

cy_die_update:
    call cy_debris_update
    call cy_die_sweep
    dec word [cy_dietim]
    jnz .out
    cmp byte [cy_lives], 0
    je .over
    dec byte [cy_lives]
    mov byte [cy_huddirty], 1
    call cy_clearboard
    call cy_wavesize
    mov byte [cy_state], CYS_WARPIN
    mov byte [cy_wpha], CY_WARP_SPOKE
    mov byte [cy_wstarted], 0
    mov byte [cy_full], 1
    jmp short .out
.over:
    call cy_hs_submit
    jc .noinit                      ; did not make the table: nothing to name
    call cy_init_begin
.noinit:
    mov byte [cy_state], CYS_OVER
    mov word [cy_overt], 90
    mov si, cy_s_over
    call cy_msg_set
    call cy_debris_burst
.out:
    ret

; cy_die_sweep - the next step of the falling death tone, if it is due.
;
; [cy_dietim] runs 24 down to 0 at a frame each, so the thirds of it are the
; three steps. It is stepped from the UPDATE and not queued at the moment of
; death because OSAPI_SND_TONE plays ONE tone: a sweep is three requests
; spread over the animation, and the priority in the table is what stops a
; fire blip landing between them.
cy_die_sweep:
    push ax
    mov al, 0
    cmp word [cy_dietim], 16
    ja .have
    inc al
    cmp word [cy_dietim], 8
    ja .have
    inc al
.have:
    cmp al, [cy_dieph]
    jbe .out                        ; this step has already played
    mov [cy_dieph], al
    call cy_die_sfx
.out:
    pop ax
    ret

; cy_die_sfx - play death step [cy_dieph].
cy_die_sfx:
    push ax
    mov al, [cy_dieph]
    add al, CYSFX_DIE1
    call cy_sfx
    pop ax
    ret

; =============================================================================
; THE PLAY RENDER
;
; One pass over every mover. Each one computes its rect, hands it to
; cy_obj_show and is done: the "did it move" test and the erase-only-the-
; difference subtraction both live in there, so no caller can get either
; wrong and no caller repeats them.
; =============================================================================
cy_play_render:
    ; Preserves nothing: it is on the drawing spine (SPEC.md 67.5.5).
    cmp byte [cy_huddirty], 0
    je .en
    call cy_hud_draw
.en:
    ; --- enemies ---------------------------------------------------------
    xor si, si
.eloop:
    cmp si, CY_MAXENEM
    jae .sh
    mov ax, si
    add ax, CY_OB_E
    call cy_obj_ptr                 ; DI = the state block
    mov al, [cy_e_kind + si]
    cmp al, 0xFF
    je .enext
    cmp al, 0xFE
    jne .elive
    ; dead: erase it, mark the lane's spike, and free the slot
    mov al, [cy_e_lane + si]
    mov bx, si
    shl bx, 1
    mov ah, [cy_e_dp + bx + 1]      ; the high byte IS the depth
    call cy_spk_mark
    call cy_obj_hide
    mov byte [cy_e_kind + si], 0xFF
    jmp short .enext
.elive:
    mov bl, al
    mov bh, 0
    shl bx, 1
    mov ax, [cy_ekext + bx]         ; the kind's half-extents
    mov bx, ax
    mov al, [cy_e_lane + si]
    mov cx, si
    shl cx, 1
    mov di, cx
    mov ah, [cy_e_dp + di + 1]
    push ax
    mov al, [cy_e_lane + si]
    call cy_spk_mark                ; the spike wants the REAL depth
    pop ax
    call cy_dmap                    ; ...the drawing wants the mapped one
    call cy_setrect
    mov ax, si
    add ax, CY_OB_E
    call cy_obj_ptr
    mov bl, [cy_e_kind + si]
    mov bh, 0
    mov al, [cy_ekcol + bx]
    call cy_obj_show
.enext:
    inc si
    jmp short .eloop

    ; --- player shots -----------------------------------------------------
.sh:
    xor si, si
.sloop:
    cmp si, CY_MAXSHOT
    jae .xs
    mov ax, si
    add ax, CY_OB_S
    call cy_obj_ptr
    mov al, [cy_s_act + si]
    or al, al
    jz .snext
    cmp al, 2
    jne .slive
    mov al, [cy_s_lane + si]
    mov bx, si
    shl bx, 1
    mov ah, [cy_s_dp + bx + 1]
    call cy_spk_mark
    call cy_obj_hide
    mov byte [cy_s_act + si], 0
    jmp short .snext
.slive:
    mov bx, si
    shl bx, 1
    mov al, [cy_s_lane + si]
    mov ah, [cy_s_dp + bx + 1]
    push ax
    call cy_spk_mark
    pop ax
    call cy_dmap
    mov bx, 0x0202                  ; a 2x2 blip
    call cy_setrect
    mov ax, si
    add ax, CY_OB_S
    call cy_obj_ptr
    mov al, CY_C_SHOT
    call cy_obj_show
.snext:
    inc si
    jmp short .sloop

    ; --- enemy shots ------------------------------------------------------
.xs:
    xor si, si
.xloop:
    cmp si, CY_MAXESHOT
    jae .pu
    mov ax, si
    add ax, CY_OB_ES
    call cy_obj_ptr
    mov al, [cy_x_act + si]
    or al, al
    jz .xnext
    cmp al, 2
    jne .xlive
    mov al, [cy_x_lane + si]
    mov bx, si
    shl bx, 1
    mov ah, [cy_x_dp + bx + 1]
    call cy_spk_mark
    call cy_obj_hide
    mov byte [cy_x_act + si], 0
    jmp short .xnext
.xlive:
    mov bx, si
    shl bx, 1
    mov al, [cy_x_lane + si]
    mov ah, [cy_x_dp + bx + 1]
    push ax
    call cy_spk_mark
    pop ax
    call cy_dmap
    mov bx, 0x0302
    call cy_setrect
    mov ax, si
    add ax, CY_OB_ES
    call cy_obj_ptr
    mov al, [cy_c_es]
    call cy_obj_show
.xnext:
    inc si
    jmp short .xloop

    ; --- powerups ---------------------------------------------------------
.pu:
    xor si, si
.uloop:
    cmp si, CY_MAXPU
    jae .pl
    mov ax, si
    add ax, CY_OB_PU
    call cy_obj_ptr
    mov al, [cy_u_act + si]
    or al, al
    jz .unext
    cmp al, 2
    jne .ulive
    mov al, [cy_u_lane + si]
    mov bx, si
    shl bx, 1
    mov ah, [cy_u_dp + bx + 1]
    call cy_spk_mark
    call cy_obj_hide
    mov byte [cy_u_act + si], 0
    jmp .unext
.ulive:
    mov bx, si
    shl bx, 1
    mov al, [cy_u_lane + si]
    mov ah, [cy_u_dp + bx + 1]
    push ax
    call cy_spk_mark
    pop ax
    call cy_dmap
    mov bx, 0x0304
    test word [cy_frame], 4         ; it pulses, so the eye finds it - and a
    jz .usz                         ; pulse is free: the rect changed, so the
    mov bx, 0x0405                  ; frame was going to redraw it anyway
.usz:
    call cy_setrect
    mov ax, si
    add ax, CY_OB_PU
    call cy_obj_ptr
    mov al, CY_C_PU
    call cy_obj_show
    jc .unext                       ; it did not move: the hole is still there

    ; THE HOLE (SPEC.md 67.9.1). A powerup was a solid white square pulsing
    ; between 0303h and 0404h - which are the FUSEBALL's and the TANKER's own
    ; footprints - and CY_C_PU and the fuseball's pen are both 39.4's white
    ; class, so on Hercules and CGA a powerup and a fuseball were the same
    ; pixels. Colour cannot separate them on a 1bpp adapter and the aspect
    ; ratios were already taken, so what is left is SOLID against HOLLOW:
    ; nothing else on the web is hollow, and that is a property no enemy can
    ; collide with however many kinds arrive later.
    ;
    ; The claw's notch is the shape and the reason (cy_player_draw): the whole
    ; rect goes down first, so every pixel inside it is written on every draw
    ; and cy_rsub stays exact; the hole is an ordinary background erase and is
    ; reported as one; and DI is still this powerup's own block, so cy_obj_dmg
    ; skips it and cannot recurse. Gated on cy_obj_show having drawn, or a
    ; parked powerup would cost a fill a frame for a hole already on the glass.
    ;
    ; THE WALLS ARE 2px WIDE AND 1px TALL, which are the claw's own numbers and
    ; are not a compromise: a CGA pixel is 2.4:1 TALL and a Hercules one 1.55:1
    ; (SPEC.md 39), so one row is thicker on the glass than one column is wide -
    ; and 1px on the sides is what the file already calls spindly. The 1 is also
    ; what opens the hole EARLY: an inset of 2 on both axes needs half-extents
    ; of 3, which the depth ramp only reaches at the rim, so the box would have
    ; been solid for the whole climb and hollow only where you grab it.
    mov al, CY_C_BG
    call cy_setcol
    mov ax, [cy_snl]
    add ax, 2
    mov cx, [cy_snr]
    sub cx, 2
    cmp ax, cx                      ; A DISTANT ONE STAYS SOLID, and that is
    jg .unext                       ; the right answer rather than a fallback:
    mov bx, [cy_snt]                ; cy_setrect scales the extents by depth,
    add bx, 1                       ; so far out this is a 1px dot - and so is
    mov dx, [cy_snb]                ; every enemy. The confusion is at grab
    sub dx, 1                       ; range and that is where the hole opens
    cmp bx, dx
    jg .unext
    call cy_fillc
    mov ax, [cy_snl]                ; the hole is an erase like any other
    add ax, 2
    mov [cy_sol], ax
    mov ax, [cy_snt]
    inc ax
    mov [cy_sot], ax
    mov ax, [cy_snr]
    sub ax, 2
    mov [cy_sor], ax
    mov ax, [cy_snb]
    dec ax
    mov [cy_sob], ax
    call cy_obj_dmg
.unext:
    inc si
    jmp .uloop                      ; NOT `jmp short`: the hole above put the
                                    ; loop body past 127 bytes

    ; --- the player's claw -------------------------------------------------
.pl:
    call cy_player_draw
    call cy_debris_render
    call cy_spk_repair
    call cy_web_repair
    call cy_msg_render
    ret

; --- cy_player_draw -----------------------------------------------------------
; The claw is drawn as its whole bounding rect in the player pen and THEN a
; notch in the background - so every pixel of the rect is written on every
; draw, which is what keeps cy_rsub exact. A shape that left holes inside its
; own bounding rect would leave stale pixels in the overlap when it moved, and
; that is the one way this scheme can go wrong.
; -----------------------------------------------------------------------------
cy_player_draw:
    ; Preserves nothing: it is on the drawing spine (SPEC.md 67.5.5).
    cmp byte [cy_state], CYS_OVER
    je .hide
    cmp byte [cy_state], CYS_DIE
    je .hide

    mov al, [cy_plane]
    mov ah, CY_TOPD
    mov bx, 0x0305                  ; wide and shallow: it straddles the lane
    call cy_setrect

    cmp word [cy_pjump], 0          ; a jump lifts it clear of the rim, out
    je .noj                         ; along the ray from the vanishing point
    mov ax, [cy_snl]
    add ax, [cy_snr]
    sar ax, 1
    sub ax, [cy_cx]
    call cy_sgn3
    add [cy_snl], ax
    add [cy_snr], ax
    mov ax, [cy_snt]
    add ax, [cy_snb]
    sar ax, 1
    sub ax, [cy_cy]
    call cy_sgn3
    add [cy_snt], ax
    add [cy_snb], ax
.noj:
    mov ax, CY_OB_PL
    call cy_obj_ptr
    mov al, CY_C_PLAYER
    call cy_obj_show
    jc .out                         ; it did not move: the notch is still there

    mov al, CY_C_BG                 ; the notch, inside the rect just written
    call cy_setcol
    mov ax, [cy_snl]
    add ax, 2
    mov cx, [cy_snr]
    sub cx, 2
    cmp ax, cx
    jg .out
    mov bx, [cy_snt]
    mov dx, [cy_snb]
    sub dx, 1
    cmp bx, dx
    jg .out
    call cy_fillc
    mov ax, [cy_snl]                ; the notch is an erase like any other -
    add ax, 2                       ; and DI is still the claw's own block, so
    mov [cy_sol], ax                ; cy_obj_dmg skips it and cannot recurse
    mov ax, [cy_snt]
    mov [cy_sot], ax
    mov ax, [cy_snr]
    sub ax, 2
    mov [cy_sor], ax
    mov ax, [cy_snb]
    dec ax
    mov [cy_sob], ax
    call cy_obj_dmg
.out:
    ret
.hide:
    mov ax, CY_OB_PL
    call cy_obj_ptr
    call cy_obj_hide
    ret

; cy_sgn3 - AX = 3 with the sign of AX (0 stays 0). The jump's offset.
cy_sgn3:
    or ax, ax
    jz .z
    jns .p
    mov ax, -3
    ret
.p:
    mov ax, 3
.z:
    ret

; =============================================================================
; THE WARP RENDERS
; =============================================================================

; cy_warpin_render - the tube extruding into view. One LSTEPV a frame.
cy_warpin_render:
    push ax
    push bx
    cmp byte [cy_wstarted], 0
    jne .step
    mov byte [cy_wstarted], 1
    cmp byte [cy_wpha], CY_WARP_SPOKE
    jne .lay
    mov al, CY_C_WEBFAR             ; the far ring first, whole: it is a tenth
    call cy_web_far                 ; of the rim's perimeter and it gives the
                                    ; extrusion something to grow out of
.lay:
    mov al, [cy_wpha]
    call cy_warp_lay
.step:
    mov al, CY_C_WEB
    call cy_setcol
    mov ax, CY_WARPK
    call cy_warp_step
    jnc .out                        ; still laying pixels
    cmp byte [cy_wpha], CY_WARP_RIM
    je .done
    mov byte [cy_wpha], CY_WARP_RIM ; spokes finished: light the rim round
    mov byte [cy_wstarted], 0
    jmp short .out
.done:
    mov byte [cy_state], CYS_PLAY   ; ...and the static playfield is already
    mov byte [cy_huddirty], 1       ; on the glass, because drawing it WAS
    call cy_spk_all                 ; the animation
.out:
    pop bx
    pop ax
    ret

; cy_warpout_render - flying down the tube. The identical walks, replayed in
; the background colour, so the erase visits exactly the pixels the draw
; visited and no remnant is possible (SPEC.md 5.6.7).
cy_warpout_render:
    push ax
    push bx
    push di

    cmp byte [cy_wstarted], 0
    jne .step
    mov byte [cy_wstarted], 1
    call cy_hideall                 ; the movers come off first, or their
                                    ; rects outlive the web they sat on
    mov al, [cy_wpha]
    call cy_warp_lay
.step:
    mov al, CY_C_BG
    call cy_setcol
    mov ax, CY_WARPK
    call cy_warp_step
    jnc .out
    cmp byte [cy_wpha], CY_WARP_SPOKE
    je .done
    mov byte [cy_wpha], CY_WARP_SPOKE
    mov byte [cy_wstarted], 0
    jmp short .out
.done:
    mov al, CY_C_BG                 ; the far ring last of all
    call cy_web_far
    inc word [cy_level]
    call cy_startlevel
.out:
    pop di
    pop bx
    pop ax
    ret

cy_hideall:
    push ax
    push cx
    push di
    mov di, cy_ost
    mov cx, CY_NOBJ
.each:
    push cx
    push di
    call cy_obj_hide
    pop di
    pop cx
    add di, CY_OBSZ
    loop .each
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; MESSAGES - the one-line banner across the foot of the playfield
;
; [cy_msgp] is the banner ACTUALLY on screen, so an unchanged one costs a
; compare and nothing else, which is most frames. When it does change it is
; ONE opaque centred run inside a fixed span of spaces, so the padding erases
; whatever was there and no cell is ever momentarily blank - Missile Command's
; SPEC.md 48.9.3, where the same strip re-lettered every frame and strobed at
; 18Hz for the whole time a banner was up.
; =============================================================================
cy_msg_set:
    mov [cy_msgs], si
    mov word [cy_msgt], 40
    mov byte [cy_msgdirty], 1
    ret

cy_msg_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    cmp word [cy_msgt], 0
    je .none
    dec word [cy_msgt]
    mov si, [cy_msgs]
    jmp short .have
.none:
    mov si, 0
.have:
    cmp byte [cy_state], CYS_PAUSE
    jne .p2
    mov si, cy_s_paused
.p2:
    cmp si, [cy_msgp]
    je .out                         ; already on the glass
    mov [cy_msgp], si

    mov di, cy_msgbuf               ; a fixed span of spaces, centred
    mov cx, CY_MSGW
    mov al, ' '
    push es
    push ds
    pop es
    rep stosb
    mov byte [cy_msgbuf + CY_MSGW], 0
    pop es
    or si, si
    jz .draw
    call cy_strlen                  ; CX = length
    cmp cx, CY_MSGW
    jbe .fit
    mov cx, CY_MSGW
.fit:
    mov ax, CY_MSGW
    sub ax, cx
    shr ax, 1
    mov di, cy_msgbuf
    add di, ax
    push es
    push ds
    pop es
    rep movsb
    pop es
.draw:
    mov cx, [cy_cwid]
    mov ax, CY_MSGW * 8
    cmp cx, ax
    jb .x0
    sub cx, ax
    shr cx, 1
    and cx, 0xFFF8                  ; the run's pen on a multiple of 8, which
    jmp short .y                    ; is what earns font_run's single-store
.x0:                                ; path (SPEC.md 6.1)
    xor cx, cx
.y:
    mov dx, [cy_chgt]
    sub dx, 9
    jns .go
    xor dx, dx
.go:
    mov si, cy_msgbuf
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc
    ; An opaque run paints its own background, so it erases everything in its
    ; band - and the band overlaps the bottom of CY_TOPD's ring, which is
    ; where the claw and every arrived enemy are.
    mov ax, cx
    mov bx, dx
    mov cx, ax
    add cx, CY_MSGW * 8 - 1
    mov dx, bx
    add dx, 7
    call cy_dmg_rect
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CX = the length of the NUL string at SI. Preserves SI.
cy_strlen:
    push ax
    push si
    xor cx, cx
.each:
    mov al, [si]
    or al, al
    jz .out
    inc cx
    inc si
    jmp short .each
.out:
    pop si
    pop ax
    ret

; =============================================================================
; DEBRIS - the death and game-over burst
; =============================================================================
cy_debris_burst:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [cy_plane]
    mov ah, CY_TOPD
    call cy_lanepos                 ; CX/DX = where the claw was
    xor si, si
.each:
    cmp si, CY_MAXDBR
    jae .out
    mov byte [cy_d_act + si], 1
    mov bx, si
    shl bx, 1
    mov ax, cx
    mov [cy_d_x + bx], ax
    mov ax, dx
    mov [cy_d_y + bx], ax
    push cx
    push dx
    call OSAPI_RAND
    and ax, 7
    sub ax, 3
    mov [cy_d_vx + bx], ax
    call OSAPI_RAND
    and ax, 7
    sub ax, 3
    mov [cy_d_vy + bx], ax
    pop dx
    pop cx
    inc si
    jmp short .each
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cy_debris_update:
    push ax
    push bx
    push si
    xor si, si
.each:
    cmp si, CY_MAXDBR
    jae .out
    cmp byte [cy_d_act + si], 1
    jne .next
    mov bx, si
    shl bx, 1
    mov ax, [cy_d_vx + bx]
    add [cy_d_x + bx], ax
    mov ax, [cy_d_vy + bx]
    add [cy_d_y + bx], ax
    mov ax, [cy_d_x + bx]
    cmp ax, 0
    jl .kill
    cmp ax, [cy_cwid]
    jge .kill
    mov ax, [cy_d_y + bx]
    cmp ax, 0
    jl .kill
    cmp ax, [cy_chgt]
    jl .next
.kill:
    mov byte [cy_d_act + si], 2
.next:
    inc si
    jmp short .each
.out:
    pop si
    pop bx
    pop ax
    ret

cy_debris_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    cmp si, CY_MAXDBR
    jae .out
    mov ax, si
    add ax, CY_OB_D
    call cy_obj_ptr
    mov al, [cy_d_act + si]
    or al, al
    jz .next
    cmp al, 2
    jne .live
    call cy_obj_hide
    mov byte [cy_d_act + si], 0
    jmp short .next
.live:
    mov bx, si
    shl bx, 1
    mov ax, [cy_d_x + bx]
    mov [cy_snl], ax
    add ax, 2
    mov [cy_snr], ax
    mov ax, [cy_d_y + bx]
    mov [cy_snt], ax
    add ax, 2
    mov [cy_snb], ax
    mov ax, si
    add ax, CY_OB_D
    call cy_obj_ptr
    mov al, CY_C_PLAYER
    call cy_obj_show
.next:
    inc si
    jmp short .each
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; GAME OVER
; =============================================================================
cy_over_update:
    call cy_debris_update
    cmp byte [cy_ient], 0
    jne .out                        ; the clock does not run while the player
                                    ; is typing: the attract screen taking the
                                    ; entry away mid-word is the one way this
    dec word [cy_overt]             ; feature can be worse than not having it
    jnz .out
    mov byte [cy_state], CYS_TITLE
    mov byte [cy_full], 1
    mov byte [cy_needlay], 1        ; the title screen's playfield stops above
    mov byte [cy_tphase], 0         ; the scroll band, so the geometry differs
    mov byte [cy_wstarted], 0
.out:
    ret

cy_over_render:
    call cy_debris_render
    call cy_init_render
    call cy_msg_render
    ret

; --- cy_init_render -----------------------------------------------------------
; The initials prompt, one opaque run under the banner (SPEC.md 6.1), redrawn
; only when a key changed it. A caret is a '_' in the next slot rather than a
; blinking cell: there is no timer here worth spending on it, and a static one
; is unambiguous about where the next letter goes.
; -----------------------------------------------------------------------------
cy_init_render:
    cmp byte [cy_ient], 0
    je .out
    cmp byte [cy_msgdirty], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cy_ibuf2                ; 'NAME: A B C' padded to a fixed span, so
    mov si, cy_s_initp              ; the run erases its own previous width
    call cy_scopy
    mov bx, 0
.ch:
    cmp bx, 3
    jae .pad
    mov al, [cy_ibuf + bx]
    cmp bl, [cy_ipos]
    jne .put
    mov al, '_'                     ; the caret sits ON the next empty slot
.put:
    mov [di], al
    inc di
    mov byte [di], ' '
    inc di
    inc bx
    jmp short .ch
.pad:
    mov byte [di], 0
    mov cx, [cy_cwid]               ; centred on a multiple of 8, which is what
    mov ax, CY_INITW * 8            ; earns font_run's single-store path
    cmp cx, ax
    jb .x0
    sub cx, ax
    shr cx, 1
    and cx, 0xFFF8
    jmp short .y
.x0:
    xor cx, cx
.y:
    mov dx, [cy_chgt]
    sub dx, 19                      ; a line above the banner
    jns .go
    xor dx, dx
.go:
    mov si, cy_ibuf2
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc
    mov ax, cx                      ; an opaque run erases its band, so mark
    mov bx, dx                      ; what stood in it (SPEC.md 67.5)
    mov cx, ax
    add cx, CY_INITW * 8 - 1
    mov dx, bx
    add dx, 7
    call cy_dmg_rect
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; cy_scopy - copy the NUL-terminated string at SI to DI, leaving DI on the NUL.
cy_scopy:
    push ax
.each:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc si
    inc di
    jmp short .each
.out:
    pop ax
    ret

; =============================================================================
; HIGH SCORES
; =============================================================================
cy_hs_init:
    push ax
    push bx
    push cx
    push di
    push si
    xor si, si
    mov cx, CY_NHS
    mov ax, 5000
.each:
    mov bx, si
    shl bx, 1
    shl bx, 1
    mov [cy_hs + bx], ax
    mov word [cy_hs + bx + 2], 0
    mov di, si                      ; ...and a placeholder name, three bytes
    add di, di                      ; per row (di = si*3)
    add di, si
    mov word [cy_hsn + di], '--'
    mov byte [cy_hsn + di + 2], '-'
    sub ax, 1000
    inc si
    loop .each
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret

; cy_hs_submit - offer [cy_score] to the table.
; out: CF = 0 and AL = the row it took, CF = 1 if it did not make it.
cy_hs_submit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.find:
    cmp si, CY_NHS
    jae .out
    mov bx, si
    shl bx, 1
    shl bx, 1
    mov ax, [cy_score + 2]
    cmp ax, [cy_hs + bx + 2]
    ja .ins
    jb .next
    mov ax, [cy_score]
    cmp ax, [cy_hs + bx]
    ja .ins
.next:
    inc si
    jmp short .find
.ins:
    mov di, CY_NHS - 1              ; shuffle the tail down one
.shift:
    cmp di, si
    jbe .put
    mov bx, di
    shl bx, 1
    shl bx, 1
    mov ax, [cy_hs + bx - 4]
    mov [cy_hs + bx], ax
    mov ax, [cy_hs + bx - 2]
    mov [cy_hs + bx + 2], ax
    push si                         ; ...and the NAME with it, or the table
    mov bx, di                      ; would keep the scores in order and
    add bx, bx                      ; leave every name one row behind
    add bx, di
    mov si, bx
    sub si, 3
    mov ax, [cy_hsn + si]
    mov [cy_hsn + bx], ax
    mov al, [cy_hsn + si + 2]
    mov [cy_hsn + bx + 2], al
    pop si
    dec di
    jmp short .shift
.put:
    mov bx, si
    shl bx, 1
    shl bx, 1
    mov ax, [cy_score]
    mov [cy_hs + bx], ax
    mov ax, [cy_score + 2]
    mov [cy_hs + bx + 2], ax
    mov bx, si                      ; a blank name until the player types one
    add bx, bx
    add bx, si
    mov word [cy_hsn + bx], '  '
    mov byte [cy_hsn + bx + 2], ' '
    mov [cy_hsidx], si              ; the row, in bss and not in AX: the pops
    clc                             ; below restore AX and would eat it. They
    jmp short .done                 ; do not touch the flags, so CF carries
.out:                               ; the verdict on its own
    stc
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; INITIALS AND THE SCORE FILE (SPEC.md 67.20)
; =============================================================================

cy_hs_file: db 'CYCLONE.HS', 0
cy_s_initp: db 'NAME ', 0
cy_hs_magic: db 'CY8', 1            ; the format, so a stale or foreign file is
                                    ; refused rather than decoded

; --- SYSTEM\APPDATA (SPEC.md 19.9) -------------------------------------------
; The table used to be written wherever this instance was standing, which is
; the folder it was launched from - GAMES, next to the games. The file browser
; is the whole of how a user reaches an application here, so a data file in an
; app folder is a misclick waiting to happen and one more row to scroll past.
;
; cy_data_enter stands us in SYSTEM\APPDATA on OUR OWN volume - the disk we
; were launched from, which is in the drive by definition, where the boot
; volume may well have been swapped out on a one-floppy machine - and
; cy_data_leave puts us back. Every path out of a save or a load must call the
; leave, or the app is left standing somewhere else and its next Save As opens
; there (SPEC.md 38.10) and every unqualified name it passes the file API
; afterwards resolves there.
;
; IT IS OSAPI_FILE_GOTO AND NOT ITS QUIET TWIN, and that is the whole of what
; the first version got wrong. OSAPI_FILE_GOTO_Q moves the GLOBAL cwd and
; deliberately NOT the instance's - its own contract says so: "this is where
; the caller is standing to do a job, not where the application now believes
; it lives" - while OSAPI_FILE_FIND, _READ and _WRITE every one resolve in the
; INSTANCE's folder through inst_vol_enter. So the quiet move was undone by
; the very next call: the walk listed GAMES, found no SYSTEM in it, and the
; save wrote nothing at all. The load path appeared to work, which is worse
; than failing - it was luck about which folder the globals happened to be
; standing in, and luck does not repeat.
;
; The price is that each step is a REMOUNT: four for a save, four for the load
; at first paint. That is affordable exactly here - a save is already seconds
; of floppy and happens once per qualifying game over - and it is NOT cached,
; because a cached cluster plus a swapped disk is a write into whatever
; cluster 3 is on somebody else's floppy.
cy_d_system: db 'SYSTEM', 0
cy_d_appdat: db 'APPDATA', 0

; cy_data_enter - bank where we are and go to SYSTEM\APPDATA on this volume.
; out: CF=1 we did not get there and NOTHING was banked or moved - the caller
;      does its file operation nowhere and must NOT call cy_data_leave.
;      CF=0 and the banked pair is in [cy_dbdrv]/[cy_dbclus].
; UI task only. Clobbers nothing the callers keep.
cy_data_enter:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call OSAPI_FILE_HERE            ; DX = our cwd cluster, BL = our drive
    mov [cy_dbclus], dx
    mov [cy_dbdrv], bl
    xor dx, dx                      ; the ROOT of that same volume
    call OSAPI_FILE_GOTO
    jc .back
    mov si, cy_d_system
    call cy_data_dive
    jc .back
    mov si, cy_d_appdat
    call cy_data_dive
    jc .back
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.back:
    ; A DISK WITHOUT THE FOLDER IS NOT AN ERROR - it is a user's own disk, or
    ; one written by something else. Put the volume back and refuse: the caller
    ; then does nothing, which is exactly what a refused write already did.
    call cy_data_home
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; cy_data_leave / cy_data_home - back to the banked folder. Preserves
; everything AND the flags: the caller's result is in CF and AX.
cy_data_leave:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    call cy_data_home
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

cy_data_home:
    push ax
    push bx
    push dx
    mov dx, [cy_dbclus]
    mov bl, [cy_dbdrv]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

; cy_data_dive - step into the folder named at SI, in the current directory.
; out: CF=1 there is no such folder. ES = DS on entry.
cy_data_dive:
    push ax
    push cx
    push si
    push di
    xor cx, cx                      ; the walk's ordinal, 0 to start
.each:
    push ds                         ; ES EVERY TIME, not once: the buffer is
    pop es                          ; ours and nothing promises ES survives a
    mov di, cy_dfind                ; far call into the kernel
    call OSAPI_FILE_FIND
    jc .none
    cmp word [cy_dfind + 14], OSAPI_FT_DIR
    jne .each                       ; only a FOLDER can be dived into, and the
                                    ; type word is the question to ask: type 1
                                    ; is a PACKAGE and not "a file"
    push cx
    push si
    mov di, cy_dfind
    call cy_data_same
    pop si
    pop cx
    jc .each
    mov dx, [cy_dfind + 16]         ; its first cluster
    mov bl, [cy_dbdrv]
    call OSAPI_FILE_GOTO
    jc .none
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.none:
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

; cy_data_same - is the NUL name at SI the one at DI? CF=0 yes.
cy_data_same:
    push ax
.n:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .n
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; cy_hs_load - read the table, once, from the first paint. Anything wrong with
; the file leaves the built-in defaults standing: a high score table is not
; worth an error box, and the first run has no file at all.
; UI task only (SPEC.md 20.6 rule 7). Preserves everything.
cy_hs_load:
    cmp byte [cy_hsload], 0
    jne .out
    mov byte [cy_hsload], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call cy_data_enter              ; SPEC.md 19.9: the table lives in
    jc .done                        ; SYSTEM\APPDATA, not beside the games
    mov si, cy_hs_file
    mov bx, cy_hsbuf
    mov cx, CY_HSFSZ
    xor dx, dx
    call OSAPI_FILE_READ
    call cy_data_leave              ; ...and we stand where we started again,
    jc .done                        ; whatever the read answered
    or dx, dx
    jnz .done                       ; longer than a word: not ours
    cmp ax, CY_HSFSZ
    jne .done
    mov si, cy_hsbuf                ; the magic, all four bytes of it
    mov di, cy_hs_magic
    mov cx, 4
.mag:
    mov al, [si]
    cmp al, [di]
    jne .done
    inc si
    inc di
    loop .mag
    mov si, cy_hsbuf + 4            ; scores, then names
    mov di, cy_hs
    mov cx, CY_NHS * 4
    call cy_bcopy
    mov di, cy_hsn
    mov cx, CY_NHS * 3
    call cy_bcopy
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; cy_hs_save - write it back, if anything changed. UI task only.
cy_hs_save:
    cmp byte [cy_hsdirty], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov byte [cy_hsdirty], 0        ; cleared FIRST: a refused write must not
                                    ; leave the app trying again on every
                                    ; keystroke, and the table is still right
                                    ; in memory either way
    mov si, cy_hs_magic
    mov di, cy_hsbuf
    mov cx, 4
    call cy_bcopy
    mov si, cy_hs
    mov cx, CY_NHS * 4
    call cy_bcopy
    mov si, cy_hsn
    mov cx, CY_NHS * 3
    call cy_bcopy
    call cy_data_enter
    jc .gone
    mov si, cy_hs_file
    mov bx, cy_hsbuf
    mov cx, CY_HSFSZ
    xor dx, dx
    call OSAPI_FILE_WRITE
    call cy_data_leave
.gone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; cy_bcopy - CX bytes DS:SI -> DS:DI, advancing both. ES = DS by the callers.
cy_bcopy:
    push ax
.each:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .each
    pop ax
    ret

; --- cy_init_begin ------------------------------------------------------------
; The score made the table: take three initials for it.
;
; It is a MODE rather than a state (SPEC.md 67.20): CYS_OVER already draws the
; debris and the banner, and a sixth state would have to be added to the render
; dispatch, cy_draw_all's list and cy_track's layout branch for the sake of one
; text line.
; -----------------------------------------------------------------------------
cy_init_begin:
    mov byte [cy_ient], 1
    mov byte [cy_ipos], 0
    mov word [cy_ibuf], '  '
    mov byte [cy_ibuf + 2], ' '
    mov byte [cy_ibuf + 3], 0
    mov byte [cy_msgdirty], 1
    ret

; cy_init_key - AL = ascii, AH = scan, while initials entry is up.
; out: CF = 1 the key was ours. Preserves everything else.
cy_init_key:
    cmp byte [cy_ient], 0
    je .no
    push ax
    push bx
    cmp al, 13
    je .commit
    cmp ah, KSC_ENTER
    je .commit
    cmp al, 8
    je .back
    cmp al, ' '
    jb .eat                         ; a control key that is not ours: swallow
    cmp al, 'z'                     ; it, because everything else on this
    ja .eat                         ; screen would restart the game
    cmp al, 'a'
    jb .up
    sub al, 0x20                    ; fold to upper case
.up:
    cmp byte [cy_ipos], 3
    jae .eat
    mov bl, [cy_ipos]
    mov bh, 0
    mov [cy_ibuf + bx], al
    inc byte [cy_ipos]
    mov byte [cy_msgdirty], 1
    jmp short .eat
.back:
    cmp byte [cy_ipos], 0
    je .eat
    dec byte [cy_ipos]
    mov bl, [cy_ipos]
    mov bh, 0
    mov byte [cy_ibuf + bx], ' '
    mov byte [cy_msgdirty], 1
    jmp short .eat
.commit:
    call cy_init_commit
.eat:
    pop bx
    pop ax
    stc
    ret
.no:
    clc
    ret

; cy_init_commit - stamp the initials into the row and WRITE THE FILE.
;
; This is the flush point, and it is the nearest thing a package has to the
; Control Panel's close (SPEC.md 31.8/67.20): a package gets NO close
; callback - the kernel answers the Close item itself and OSAPI_TASK_ALIVE
; never returns once the box is clicked - so there is no "on exit" to hang a
; write on. What there is instead is the one UI-task moment that ends a game,
; which is rarer than a panel close: at most one floppy write per game, and
; none at all for a game that did not make the table.
cy_init_commit:
    push ax
    push bx
    push si
    push di
    mov bx, [cy_hsidx]
    cmp bx, CY_NHS
    jae .done
    mov ax, bx
    add bx, bx
    add bx, ax                      ; row * 3
    mov si, cy_ibuf
    mov di, cy_hsn
    add di, bx
    mov al, [si]
    mov [di], al
    mov al, [si + 1]
    mov [di + 1], al
    mov al, [si + 2]
    mov [di + 2], al
.done:
    mov byte [cy_ient], 0
    mov byte [cy_hsdirty], 1
    mov byte [cy_msgdirty], 1
    call cy_hs_save
    pop di
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; THE TITLE SCREEN (SPEC.md 67.10)
;
; Three things at once, and each one costs about one drawing call a frame:
;
;  - a tube that warps in, holds, warps out and comes back as the NEXT shape,
;    which is the warp engine driven in a loop and therefore free of any code
;    of its own;
;  - a high-score scroll, which is ONE `OSAPI_GFX_SCROLL` plus ONE `font_run`
;    every few frames - the whole band moves as a blit and only the row that
;    just appeared is lettered;
;  - the prompt.
;
; The scroll is why the title screen re-lays the geometry: the web is drawn
; into a playfield that stops above the band, so the two never share a pixel
; and neither has to repair the other.
; =============================================================================

CY_TP_SPOKE equ 0
CY_TP_RIM   equ 1
CY_TP_HOLD  equ 2
CY_TP_ERIM  equ 3
CY_TP_ESPK  equ 4

cy_title_update:
    push ax
    cmp word [cy_holdt], 0
    je .out
    dec word [cy_holdt]
.out:
    pop ax
    ret

; cy_title_static - the parts that do not move. Called by cy_draw_all only.
cy_title_static:
    push ax
    push bx
    push cx
    push dx
    push si

    mov si, cy_s_name               ; the name, across the top strip
    call cy_strlen
    call cy_centre                  ; CX = a pen on a multiple of 8
    mov dx, 1
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc

    mov si, cy_s_press              ; ...and the prompt, just above the band
    call cy_strlen
    call cy_centre
    mov dx, [cy_scry0]
    sub dx, 9
    jns .p
    xor dx, dx
.p:
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc

    mov byte [cy_tphase], CY_TP_SPOKE
    mov byte [cy_wstarted], 0
    mov word [cy_scrt], 0
    call cy_scroll_reset

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CX = string length in cells -> CX = the x that centres it, on a multiple of
; 8 so font_run takes its single-store path (SPEC.md 6.1).
cy_centre:
    push ax
    push dx
    mov ax, cx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov cx, [cy_cwid]
    cmp cx, ax
    jb .zero
    sub cx, ax
    shr cx, 1
    and cx, 0xFFF8
    jmp short .out
.zero:
    xor cx, cx
.out:
    pop dx
    pop ax
    ret

cy_title_render:
    ; Preserves nothing: it is on the drawing spine (SPEC.md 67.5.5).

    mov al, [cy_tphase]
    cmp al, CY_TP_HOLD
    je .hold

    cmp byte [cy_wstarted], 0
    jne .step
    mov byte [cy_wstarted], 1
    cmp al, CY_TP_SPOKE
    jne .l2
    mov al, CY_C_WEBFAR
    call cy_web_far
.l2:
    mov al, CY_WARP_SPOKE
    cmp byte [cy_tphase], CY_TP_SPOKE
    je .lay
    cmp byte [cy_tphase], CY_TP_ESPK
    je .lay
    mov al, CY_WARP_RIM
.lay:
    call cy_warp_lay
.step:
    mov al, CY_C_WEB                ; drawing phases use the web pen, erasing
    cmp byte [cy_tphase], CY_TP_ERIM ; ones the background - same walks
    jb .pen
    mov al, CY_C_BG
.pen:
    call cy_setcol
    mov ax, CY_WARPK
    call cy_warp_step
    jnc .scroll
    ; this phase is done - advance
    inc byte [cy_tphase]
    mov byte [cy_wstarted], 0
    cmp byte [cy_tphase], CY_TP_HOLD
    jne .chk
    mov word [cy_holdt], 30
    jmp short .scroll
.chk:
    cmp byte [cy_tphase], CY_TP_ESPK + 1
    jb .scroll
    mov al, CY_C_BG                 ; the far ring goes with the spokes
    call cy_web_far
    mov byte [cy_tphase], CY_TP_SPOKE
    call cy_title_nextshape
    jmp short .scroll
.hold:
    cmp word [cy_holdt], 0
    jne .scroll
    mov byte [cy_tphase], CY_TP_ERIM
    mov byte [cy_wstarted], 0
.scroll:
    call cy_scroll_step
    ret

cy_title_nextshape:
    push ax
    push bx
    inc word [cy_tshape]
    mov ax, [cy_tshape]
    xor dx, dx
    mov bx, CY_NSHAPE
    div bx
    mov [cy_tshape], dx
    mov bx, dx
    shl bx, 1
    mov ax, [cy_shapes + bx]
    mov [cy_shape], ax
    call cy_build_verts             ; the shape changed, not the box, so this
    pop bx                          ; is a rebuild and not a whole relayout
    pop ax
    ret

; --- cy_scroll_span -----------------------------------------------------------
; The band's x span, in CONTENT coordinates: a CENTRED window of whole byte
; columns, no wider than the widest line the scroller emits.
;
; It is narrowed on purpose. `gfx_scroll` MOVES pixels and invents none, so
; between the blit and the `font_run` that letters the row it exposed, that
; row still holds the OLD bottom row - and the raster does not wait for our
; lock, so the glass can catch the band showing its last line twice. The gap
; is the blit's own duration, and the blit is priced by area (SPEC.md 5.5.1
; measures 256x128 at 34ms on a 4.77MHz Hercules), so halving the width
; halves the window in which that double image can be seen. Centring it also
; fixes the wide-window case, where the text was centred in the CONTENT and
; the band was blitted from x=0.
;
; out: AX = x1, CX = width, both multiples of 8. CF=1 = no usable band.
; -----------------------------------------------------------------------------
cy_scroll_span:
    push bx
    mov cx, [cy_cwid]
    and cx, 0xFFF8
    cmp cx, CY_SCRMAXC * 8
    jbe .have
    mov cx, CY_SCRMAXC * 8
.have:
    or cx, cx
    jz .none
    mov ax, [cy_cwid]
    sub ax, cx
    shr ax, 1
    and ax, 0xFFF8
    pop bx
    clc
    ret
.none:
    pop bx
    stc
    ret

; --- the scroll ---------------------------------------------------------------
cy_scroll_reset:
    mov word [cy_scrp], cy_scroll_txt
    mov byte [cy_scrm], 0
    mov byte [cy_scrsp], 0          ; the table has not been spliced this pass
    ret

cy_scroll_step:
    push ax
    push bx
    push cx
    push dx
    push si

    inc word [cy_scrt]
    cmp word [cy_scrt], CY_SCRT
    jb .out
    mov word [cy_scrt], 0

    call cy_scroll_span             ; AX = x1, CX = width (both x8)
    jc .out
    mov [cy_tmpw], cx
    mov [cy_tmpn], ax

    add ax, [cy_ox]
    mov bx, [cy_scry0]
    add bx, [cy_oy]
    mov cx, [cy_tmpw]
    add cx, [cy_tmpn]
    add cx, [cy_ox]
    dec cx
    mov dx, [cy_chgt]
    add dx, [cy_oy]
    dec dx
    mov si, CY_SCRROW               ; one row of the band, and a multiple of
                                    ; 4 so the blit takes its fast path on
                                    ; every adapter (SPEC.md 5.5.1)
    call OSAPI_GFX_SCROLL
    jc .out                         ; refused: partly covered, or the clip
                                    ; region does not contain us. Skip the
                                    ; frame; the text simply waits
    call cy_scroll_next             ; ...and letter the row it exposed
    call cy_scroll_row
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_scroll_row - letter SI across the WHOLE band width, centred, as one
; opaque run.
;
; It has to be the whole width, not just the text: gfx_scroll MOVES pixels and
; invents none, so what is in the row it vacated is unspecified - and a run
; that covers only its own span leaves whatever was there on either side of
; it. That showed as a stray letter at the start of every scrolled row, which
; reads as a font bug rather than as an un-erased band. Padding the line to
; the band and drawing it opaque erases and letters in ONE call, with no fill
; in front of it and so no moment when the row is blank (SPEC.md 6.1).
cy_scroll_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es

    call cy_scroll_span             ; the SAME span the blit moves, so the
    jc .out                         ; text and the band cannot disagree
    mov [cy_tmpn], ax
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; pixels -> cells
    or cx, cx
    jz .out
    mov [cy_tmpw], cx

    ; A MODEL BYTE IS PEELED OFF THE FRONT, so the length below - and therefore
    ; the centring - is the TEXT's. Peeling it after measuring would centre the
    ; line one cell left of where its picture goes.
    ;
    ; THE ZERO TEST IS NOT OPTIONAL AND IT IS SPEC.md 67.21.1's BUG A THIRD
    ; TIME: an empty line is a LONE NUL, which is under 20h, so `cmp byte [si],
    ; 0x20 / jae` peeled a blank separator's own terminator and stepped SI onto
    ; the NEXT line - which then rendered RAW, model byte lettered as a glyph
    ; and one cell out of centre, while the real row drew properly a moment
    ; later. Two adjacent band rows holding the same line, one with a swatch
    ; and one without. At the END of the list it was worse: the terminator row
    ; peeled the last blank's NUL, ran cy_strlen straight past 0FFh and
    ; lettered the About panel's first line out of the image behind it.
    mov byte [cy_scrmid], 0
    mov al, [si]
    or al, al
    jz .nomod                       ; an empty line, not a model
    cmp al, 0x20
    jae .nomod
    mov [cy_scrmid], al
    inc si
.nomod:

    mov di, cy_scrline              ; blank it, then centre the text in it
    mov al, ' '
    push cx
    rep stosb
    pop cx
    mov bx, cy_scrline
    add bx, cx
    mov byte [bx], 0

    push cx
    call cy_strlen                  ; CX = the text's length
    mov bx, cx
    pop cx
    cmp bx, cx
    jbe .fit
    mov bx, cx
.fit:
    mov ax, cx
    sub ax, bx
    shr ax, 1
    mov di, cy_scrline
    add di, ax
    mov cx, bx
    rep movsb

    shl ax, 1                       ; where the TEXT starts on the glass: the
    shl ax, 1                       ; run is emitted at the band's left edge
    shl ax, 1                       ; and the centring lives inside the buffer,
    add ax, [cy_tmpn]               ; so the swatch has to be told
    mov [cy_scrmx], ax

    ; THE LEADING HAS TO BE ERASED TOO. font_run is opaque over its own 8px
    ; cell and no further, so with a 12-row pitch the two rows above the
    ; glyphs and the two below are still holding what the blit shifted into
    ; them - fragments of the line before, which accumulate up the band as
    ; every later scroll carries them along. Two thin fills, on rows the run
    ; will not touch, so no pixel is written twice and there is no
    ; double-draw (PERFORMANCE.md Part 1).
    mov al, CY_C_BG
    call cy_setcol
    mov ax, [cy_tmpn]
    mov cx, [cy_tmpw]               ; width in cells -> pixels
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx
    mov bx, [cy_chgt]
    sub bx, CY_SCRROW
    mov dx, bx
    inc dx
    call cy_fillc                   ; the two rows above the glyphs
    mov bx, [cy_chgt]
    sub bx, 2
    mov dx, [cy_chgt]
    dec dx
    call cy_fillc                   ; ...and the two below

    mov si, cy_scrline
    mov cx, [cy_tmpn]
    mov dx, [cy_chgt]
    sub dx, CY_SCRROW - 2           ; 2px of leading above the glyphs
    mov al, CY_C_TEXT
    mov ah, CY_C_BG
    call cy_textc
    call cy_scroll_model            ; ...and the picture LAST, over the two
.out:                               ; cells the opaque run just blanked for it
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cy_scroll_model - the picture beside the row just lettered (SPEC.md 67.21.2).
;
; [cy_scrmid] is 0 for a line with no model, 1..CYE_KINDS for that enemy kind,
; CY_SCRM_PU for the powerup box. The shape and the pen come out of cy_ekext
; and cy_ekcol - THE GAME'S OWN TABLES - so a swatch cannot become a picture of
; something the web does not contain, and it follows cy_pal's per-adapter
; rewrite of the two dither pens for free.
;
; It draws in the two cells the opaque run reserved at the text's pen, centred
; on the glyph rows. Nothing else draws in the band (the playfield stops above
; it, SPEC.md 67.10), so there is no object block, no damage mark and no
; repair - the band is the scroller's alone and the next blit carries the
; swatch up with the text as ordinary pixels.
;
; Preserves nothing but the flags' irrelevance: it is the last thing in
; cy_scroll_row, which has pushed everything.
cy_scroll_model:
    mov al, [cy_scrmid]
    or al, al
    jz .out
    mov bl, al
    mov bh, 0
    dec bx                          ; 1..CYE_KINDS -> the kind index
    cmp bl, CYE_KINDS
    jb .kind
    cmp al, CY_SCRM_PU
    jne .out                        ; not a model we know: draw nothing
    mov bx, 0x0405                  ; the powerup's larger pulse phase, and its
    mov al, CY_C_PU                 ; pen; the hole is cut below
    mov byte [cy_scrmh], 1
    jmp short .have
.kind:
    mov byte [cy_scrmh], 0
    mov al, [cy_ekcol + bx]         ; the live pen for THIS adapter
    add bx, bx
    mov bx, [cy_ekext + bx]
.have:
    ; The centre: half way across the reserved cells, and on the middle of the
    ; 8 glyph rows. The 12-row pitch is cleared by the run and the two leading
    ; fills, so a model up to 11 rows tall has somewhere to be. SI and DI are
    ; free here - cy_scroll_row pushed both and this is the last thing it does -
    ; and cy_tmpx/cy_tmpy are deliberately NOT borrowed: those are cy_setrect's
    ; scratch, and a routine on the drawing spine reaching into another one's
    ; temporaries is how this file's worst bugs have started.
    mov [cy_scrmk], bx              ; the extents, out of the way of the pen:
    call cy_setcol                  ; OSAPI_SET_COLOR clobbers freely
    mov si, [cy_scrmx]
    add si, CY_SCRMW * 8 / 2        ; SI = centre x
    mov di, [cy_chgt]
    sub di, CY_SCRROW - 2 - 3       ; DI = centre y (the glyphs start 10 up)

    mov bx, [cy_scrmk]
    mov dl, bl                      ; half WIDTH
    mov dh, 0
    mov ax, si
    sub ax, dx                      ; AX = x1
    mov cx, si
    add cx, dx                      ; CX = x2
    mov dl, bh                      ; ...then half HEIGHT, before BX is spent
    mov dh, 0
    mov bx, di
    sub bx, dx                      ; BX = y1
    add dx, di                      ; DX = y2
    call cy_fillc                   ; preserves all four, which the hole needs

    cmp byte [cy_scrmh], 0
    je .out
    ; ...and the hole, on 67.9.1's terms and with its numbers: 2px of wall each
    ; side, 1px top and bottom. The whole rect was written just above, so this
    ; is the claw's shape here too.
    push ax
    push bx
    push cx
    push dx
    mov al, CY_C_BG
    call cy_setcol
    pop dx
    pop cx
    pop bx
    pop ax
    add ax, 2
    sub cx, 2
    cmp ax, cx
    jg .out
    inc bx
    dec dx
    cmp bx, dx
    jg .out
    call cy_fillc
.out:
    ret

; cy_scroll_next - SI = the next line. Walks the static list, then the high
; scores WHERE THE LIST ASKS FOR THEM, then round again.
;
; The table used to come after the whole static list, because the list simply
; ran out; it is spliced at a CY_SCRM_SPL marker now, so where it appears is a
; property of the text and not of this routine. [cy_scrsp] is what makes that
; safe: the marker is honoured once per pass, so two markers in a row - or one
; left where the table returns to - cannot bounce between the list and the
; table for ever. That would spin with the gfx lock held, which is SPEC.md
; 59.7's hang: a machine that looks alive and never draws again.
cy_scroll_next:
    push ax
    push bx
    push cx
    push di
.top:
    mov al, [cy_scrm]
    or al, al
    jnz .hs
    mov si, [cy_scrp]
    mov al, [si]
    cmp al, CY_SCRM_END             ; 0FFh, NOT 0: a blank separator line is an
    je .wrap                        ; empty string and so is a lone 0 byte
    cmp al, CY_SCRM_SPL
    jne .plain
    cmp byte [cy_scrsp], 0
    jne .plain                      ; already spliced: step over it below
    inc si                          ; past the marker, which is where the
    mov [cy_scrp], si               ; table returns to
    mov byte [cy_scrsp], 1
    mov byte [cy_scrm], 1
    jmp short .hs
.plain:
    push si
    call cy_strlen
    add si, cx
    inc si
    mov [cy_scrp], si
    pop si
    cmp byte [si], CY_SCRM_SPL      ; a marker reached a second time. It cannot
    je .spent                       ; be, [cy_scrp] having stepped past it - but
    jmp .out                        ; 0FEh is not under 20h, so cy_scroll_row
.spent:                             ; would letter it as a glyph rather than
    mov si, cy_scr_blank            ; peel it, and an unreachable garbage
    jmp .out                        ; character is not worth leaving reachable
.hs:
    mov al, [cy_scrm]
    cmp al, 1
    jne .line
    mov si, cy_s_hiscore
    mov byte [cy_scrm], 2
    jmp .out
.line:
    mov bl, al
    sub bl, 2
    cmp bl, CY_NHS
    jae .back                       ; the table is spent: back to the LIST,
    mov bh, 0                       ; which is where the marker left off
    mov di, cy_scrbuf
    mov byte [di], '1'
    add [di], bl
    mov byte [di + 1], '.'
    mov byte [di + 2], ' '
    push bx                         ; the NAME, then the score
    mov ax, bx
    add bx, bx
    add bx, ax
    mov si, cy_hsn
    add si, bx
    mov di, cy_scrbuf + 3
    mov al, [si]
    mov [di], al
    mov al, [si + 1]
    mov [di + 1], al
    mov al, [si + 2]
    mov [di + 2], al
    mov byte [di + 3], ' '
    pop bx
    mov si, bx
    shl si, 1
    shl si, 1
    add si, cy_hs
    mov di, cy_scrbuf + 7
    mov cx, 7
    call cy_fmt
    mov byte [cy_scrbuf + 14], 0
    inc byte [cy_scrm]
    mov si, cy_scrbuf
    jmp short .out
.back:
    mov byte [cy_scrm], 0           ; and round .top once. [cy_scrp] was stepped
    jmp .top                        ; PAST the marker when the table was
                                    ; spliced, so this reads the line after it
                                    ; and cannot splice again
.wrap:
    mov byte [cy_scrm], 0
    call cy_scroll_reset
    mov si, cy_scr_blank
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

cy_scr_blank db ' ', 0

; =============================================================================
; FULLSCREEN (SPEC.md 53.7 - the SAME-MODE bracket)
;
; This game sets no video mode. It does not need one: the web is derived from
; whatever content box it is given, so the desktop's own geometry is as good
; a playfield as any raster it could ask for - and a bracket that switches no
; mode needs no caps bit, so ONE body runs on VGA, Hercules and CGA and the
; menu item never greys (SPEC.md 47 rule 3: there is no fact about modes to
; grey on).
;
; What it takes the machine FOR is the LOCK. Windowed, every frame is an
; unlock/yield/lock round trip, which PERFORMANCE.md Set 4 priced at 21.8% of
; a Missile Command session with no pixel of the game in it - plus the system
; arrow erased and redrawn inside each one. In the bracket the lock is held
; for the whole session and that cost is zero, not smaller.
;
; It deliberately does NOT sit on SPEC.md 11.2's fullscreen window as well.
; Paint's 42.7 measured what that costs on the way out: the window is still
; WF_FULL when fsx_restore's wm_paint_all runs, so the repaint draws the
; fullscreen app and the FULLSCREEN AL=0 after it throws that away and
; repaints as a window - three full content draws where the bracket alone
; costs one. Here a full content draw includes the ~180ms web.
;
; The worker is NOT kept (no FSXF_KEEPWORKER): the worker IS the game loop,
; and two loops driving one screen is two writers. The bracket runs the same
; cy_update / cy_render the worker runs, with [cy_inbr] telling cy_render the
; lock is already held.
; =============================================================================
cy_go_fsx:
    push ax
    push bx
    push cx
    cmp byte [cy_fsx], 0
    jne .out
    ; A PANEL MAY NOT CROSS INTO THE BRACKET. Every windowed way to take one
    ; down is a key or a click through W_ONKEY/W_ONCLICK, and neither is
    ; dispatched in a bracket (SPEC.md 53.1) - the exclusive loop polls int 16h
    ; straight into cy_key_common. The menu is the one path that reaches here
    ; with a panel up (Help, then Game > Full Screen), and it would arrive on
    ; the exclusive surface with no way at all to dismiss it. Off, not
    ; dismissed: the entry below already owes a whole repaint.
    call cy_pn_off
    mov byte [cy_fsxq], 0
    mov byte [cy_fsx], 1            ; BEFORE the call: fsx_restore's
    mov byte [cy_inbr], 1           ; wm_paint_all runs INSIDE fsx_run and
                                    ; re-enters our own W_PAINT
                                    ; the rect this bracket will own is asked
                                    ; INSIDE it, with OSAPI_FSX_SURF - there is
                                    ; no way to know it out here, and the
                                    ; OSAPI_VIDEO that used to stand in for it
                                    ; answered the PRIMARY's size and an origin
                                    ; of (0,0) (SPEC.md 53.7.1)
    call cy_pal
    mov byte [cy_needlay], 1
    mov byte [cy_full], 1
    mov ax, cy_fsx_main
    mov bx, [cy_win]
    xor cx, cx                      ; no flags: the worker freezes, and the
    call OSAPI_FSX_RUN              ; bracket runs the game itself
    ; whether it ran or was refused, we are back on the desktop
    mov byte [cy_fsx], 0
    mov byte [cy_inbr], 0
                                    ; ...and NOTHING is re-banked here: those
                                    ; four words describe the rect a BRACKET
                                    ; owns and cy_org only reads them while one
                                    ; is up. Re-filling them from OSAPI_VIDEO
                                    ; on the way out put the primary's size
                                    ; back into a variable the next bracket
                                    ; re-asks for anyway - and left a pattern
                                    ; for somebody to copy
    call cy_pal
    mov byte [cy_needlay], 1
    mov byte [cy_full], 1
.out:
    pop cx
    pop bx
    pop ax
    ret

; The exclusive main. SI = our window, ES = KERNEL_SEG, DS = CS = ours, gfx
; lock held for the whole session, every other task frozen. A near ret is the
; only way out.
cy_fsx_main:
    push si
    call OSAPI_FSX_SURF             ; **THE RECT THIS BRACKET OWNS** (SPEC.md
    jc .nosurf                      ; 53.7.1): AX = x, BX = y, CX = w, DX = h.
    mov [cy_scrx], ax               ; This sets no video mode (53.7), so the
    mov [cy_scry], bx               ; desktop is NOT collapsed and "fullscreen"
    mov [cy_scrw], cx               ; is this display's rect rather than (0,0)
    mov [cy_scrh], dx               ; plus OSAPI_VIDEO's size - which on a
    mov byte [cy_needlay], 1        ; two-card machine drew the whole game onto
    mov byte [cy_full], 1           ; the monitor we were not on
.nosurf:                            ; (CF=1 is impossible here - we ARE the
                                    ; bracket - and leaves what cy_entry banked)
    call OSAPI_MOUSE
    mov [cy_pbtn], al               ; seed the button, or the click that got
                                    ; us here fires the moment we arrive
.loop:
.keys:
    mov ah, 1                       ; no events are dispatched in a bracket:
    int 0x16                        ; this IS the UI task, so poll int 16h
    jz .nokey
    xor ah, ah
    int 0x16                        ; AL = ascii, AH = scan - the arguments
    call cy_key_common              ; W_ONKEY would have been handed
    cmp byte [cy_fsxq], 0
    jne .done
    jmp short .keys
.nokey:
    call OSAPI_MOUSE                ; edge-detect the button ourselves
    mov ah, [cy_pbtn]
    mov [cy_pbtn], al
    not ah
    and ah, al
    test ah, 1
    jz .nofire
    mov byte [cy_firereq], 1
    call cy_aim_mouse
.nofire:
    call cy_cur_erase               ; FIRST, while the glass still holds what
                                    ; the draw inverted (SPEC.md 67.17)
    call cy_update
    call cy_render
    call cy_cur_draw                ; ...and LAST, over the finished frame
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    cmp byte [cy_fsxq], 0
    je .loop
.done:
    ; Clear the overlay state INSIDE the bracket, before fsx_restore's
    ; wm_paint_all runs - or the thawed worker draws the fullscreen geometry
    ; over the desktop (Tracker's [trk_fs] and Missile's [mc_fsx], the one bug
    ; both of them found).
    call cy_cur_erase               ; before the geometry changes under it
    mov byte [cy_fsx], 0
    mov byte [cy_inbr], 0
    mov byte [cy_fsxq], 0
    mov byte [cy_needlay], 1
    mov byte [cy_full], 1
    pop si
    ret

; =============================================================================
; TABLES
; =============================================================================

; the points a kill is worth, by kind, before the level multiplier
cy_kindsc:
    dw 150                          ; flipper
    dw 100                          ; tanker
    dw 50                           ; spiker
    dw 250                          ; fuseball
    dw 200                          ; pulsar

; the half-extents each kind is drawn at, at the rim: low byte width, high
; byte height. THE SHAPES DIFFER, not just the colours - on Hercules and CGA
; the colour is all that collapses and the aspect is all that is left, which
; is Missile Command's satellite/bomber lesson (SPEC.md 48, mc_sat_shape).
cy_ekext:
    dw 0x0204                       ; flipper  - wide and shallow
    dw 0x0404                       ; tanker   - a big square
    dw 0x0401                       ; spiker   - narrow and tall
    dw 0x0303                       ; fuseball - a small square
    dw 0x0105                       ; pulsar   - a wide bar

; ...and their pens. Two classes only (SPEC.md 39.4): everything here is on a
; black field, so a black-class colour would be invisible on both 1bpp
; adapters - the trap SPEC.md 44.6 records.
; ...and how fast each climbs, in EIGHTHS of the level's base speed.
cy_ekspd:
    db 8                            ; flipper  - the baseline
    db 6                            ; tanker   - heavy, and it splits
    db 5                            ; spiker   - slowest; it is building
    db 13                           ; fuseball - the fast one (arrives L5)
    db 10                           ; pulsar   - quick (arrives L7)

cy_ekcol:
    db CY_C_ENEMY                   ; flipper  - white class
    db CY_C_ENEMY2                  ; tanker   - dither class
    db CY_C_ENEMY2                  ; spiker   - dither class
    db CY_C_ENEMY                   ; fuseball - white class
    db CY_C_ENEMY                   ; pulsar   - white class

; =============================================================================
; .bss - offsets from os88_image_end, which the loader zeroes for us
; =============================================================================
%assign CY_BSS 0
%macro CWORD 1
%1 equ os88_image_end + CY_BSS
%assign CY_BSS CY_BSS + 2
%endmacro
%macro CBYTE 1
%1 equ os88_image_end + CY_BSS
%assign CY_BSS CY_BSS + 1
%endmacro
%macro CBUF 2
%1 equ os88_image_end + CY_BSS
%assign CY_BSS CY_BSS + (%2)
%endmacro

    CWORD cy_win
    CWORD cy_scrx                   ; the ORIGIN of the rect our bracket owns
    CWORD cy_scry                   ; (SPEC.md 53.7.1) - NOT (0,0) on a
    CWORD cy_scrw                   ; two-display machine
    CWORD cy_scrh
    CWORD cy_dock

    ; --- the live content box, and everything derived from it -------------
    CWORD cy_ox
    CWORD cy_oy
    CWORD cy_cwid
    CWORD cy_chgt
    CWORD cy_pfh                    ; playfield height: content less the HUD
    CWORD cy_rad                    ; the rim's radius in pixels
    CWORD cy_cx                     ; the vanishing point, in SCREEN coords
    CWORD cy_cy
    CWORD cy_scry0                  ; the scroll band's top (title screen)
    CWORD cy_hudw                   ; HUD cells this window can show

    ; --- the web ----------------------------------------------------------
    CWORD cy_shape                  ; -> the descriptor in use
    CWORD cy_nlane
    CWORD cy_nvert
    CWORD cy_closed
    CWORD cy_dd                     ; cy_build_verts' depth cursor

    ; --- the warp ---------------------------------------------------------
    CWORD cy_wn                     ; live walks
    CWORD cy_wk                     ; pixels asked of each this frame
    CWORD cy_wx1
    CWORD cy_wy1
    CWORD cy_wx2
    CWORD cy_wy2
    CBYTE cy_wphase
    CBYTE cy_wpha                   ; the phase the state machine is in
    CBYTE cy_wstarted               ; ...and whether its walks are laid
    CWORD cy_dscn                   ; entries in the LSTEPV batch

    ; --- game state -------------------------------------------------------
    CWORD cy_frame
    CWORD cy_nfull                  ; whole repaints (diagnostic)
%ifdef CYTRACE
CY_TBUF equ 6
CY_TWORDS equ 14
    CWORD cy_twl
    CWORD cy_twt
    CWORD cy_twr
    CWORD cy_twb
    CWORD cy_tn
    CWORD cy_ti
    CBUF  cy_tbuf, CY_TBUF * CY_TWORDS * 2
%endif
    CWORD cy_due                    ; the worker's frame deadline
    CBYTE cy_state
    CBYTE cy_needlay
    CBYTE cy_full
    CBYTE cy_hired
    CBYTE cy_pnon                   ; a panel (About or How To Play) is up
    CBYTE cy_wasplay                ; ...and it was a live game it paused
    CWORD cy_pnx                    ; where it was drawn, so nothing re-derives
    CWORD cy_pny
    CBYTE cy_pnrow                  ; the panel's row cursor
    CBYTE cy_pndirty                ; ...and whether it is owed pixels
    CWORD cy_pntxt                  ; WHICH panel: its text block, its box and
    CWORD cy_pnw                    ; its line cap, banked at arm time. Nothing
    CWORD cy_pnh                    ; downstream asks which of the two it is -
    CBYTE cy_pnln                   ; there is one panel and it has content
    CBYTE cy_dieph                  ; which step of the death sweep has played
    CBYTE cy_curon                  ; the fullscreen cursor is on the glass
    CWORD cy_curx                   ; ...and WHERE, banked - an XOR erased at a
    CWORD cy_cury                   ; re-read position smears permanently
    CBYTE cy_phome                  ; a new level owes the claw its start lane
    CBYTE cy_col                    ; the pen we last set (it cannot be read)
    CBYTE cy_vkind
    CBYTE cy_bpp
    CBYTE cy_huddirty
    CBYTE cy_msgdirty
    CBYTE cy_firereq
    CBYTE cy_firecd
    CBYTE cy_c_e2                   ; the mover inks for THIS display (cy_pal)
    CBYTE cy_c_es
    CBYTE cy_hsav

    CWORD cy_level
    CBUF  cy_score, 4               ; a dword: six figures is one good level
    CWORD cy_bonus                  ; the last 20,000 paid
    CBYTE cy_lives
    CBYTE cy_zap
    CWORD cy_towave
    CWORD cy_wleft                  ; still to be fed in
    CWORD cy_left                   ; still alive on the web
    CWORD cy_espd
    CWORD cy_spawnt
    CBYTE cy_kinds                  ; how many kinds this level unlocks

    ; --- the player -------------------------------------------------------
    CWORD cy_plane                  ; the lane the claw is on
    CWORD cy_pjump
    CWORD cy_dir
    CWORD cy_accel
    CWORD cy_lastdir
    CWORD cy_pw_laser
    CWORD cy_pw_droid
    CBYTE cy_pw_jump
    CWORD cy_dietim
    CWORD cy_overt

    ; --- fullscreen -------------------------------------------------------
    CBYTE cy_fsx                    ; the content box IS the screen
    CBYTE cy_inbr                   ; ...and the gfx lock is already held
    CBYTE cy_fsxq                   ; the bracket has been asked to leave
    CBYTE cy_pbtn

    ; --- the banner -------------------------------------------------------
    CWORD cy_msgs
    CWORD cy_msgt
    CWORD cy_msgp                   ; the banner ACTUALLY on the glass
    CBUF  cy_msgbuf, CY_MSGW + 1

    ; --- the title screen -------------------------------------------------
    CBYTE cy_tphase
    CBYTE cy_scrm
    CWORD cy_holdt
    CWORD cy_tshape
    CWORD cy_scrt
    CWORD cy_scrp
    CBYTE cy_scrsp                  ; the high-score table has been spliced in
    CBYTE cy_scrmid                 ; this row's model, 0 = none (67.21.2)
    CBYTE cy_scrmh                  ; ...and whether it is cut hollow
    CWORD cy_scrmk                  ; its extents, banked across the pen change
    CWORD cy_scrmx                  ; where the row's TEXT starts on the glass
    CBUF  cy_scrbuf, 16
    CBUF  cy_scrline, CY_SCRMAXC + 1

    ; --- the HUD ----------------------------------------------------------
    CBUF  cy_hudbuf, CY_HUDW + 1
    CBUF  cy_hudsh, CY_HUDW + 1     ; what the strip was last DRAWN with
    CWORD cy_hf0
    CWORD cy_hf1

    ; --- scratch that will not fit in registers ---------------------------
    CWORD cy_tmpx
    CWORD cy_tmpy
    CWORD cy_tmpk
    CWORD cy_tmpd
    CWORD cy_tmpn
    CWORD cy_tmpw
    CWORD cy_tmpbest
    CWORD cy_lspx                   ; the lane's span, published by cy_lanepos
    CWORD cy_lspy
    CWORD cy_lspm                   ; ...and the dominant of the two

    ; --- the lip (67.5.3.1): where a lane's CY_TOPD movers actually sit ---
    CBUF  cy_lipx, CY_MAXLANE * 2
    CBUF  cy_lipy, CY_MAXLANE * 2
    CWORD cy_lptx                   ; ...and cy_lip_build's own scratch, which
    CWORD cy_lpty                   ; runs once per window change and so keeps
    CWORD cy_lpv2                   ; none of it in registers
    CWORD cy_lpex
    CWORD cy_lpey
    CWORD cy_lpmx
    CWORD cy_lpmy
    CWORD cy_lpdx
    CWORD cy_lpdy
    CWORD cy_lpax
    CWORD cy_lpay
    CWORD cy_lpl
    CWORD cy_lpn
    CWORD cy_lpox
    CWORD cy_lpoy
    CWORD cy_lpk
    CBYTE cy_lpdir
    CBYTE cy_lpdir0
    CBUF  cy_fnum, 4

    ; --- cy_rsub's three rects --------------------------------------------
    CWORD cy_sol
    CWORD cy_sot
    CWORD cy_sor
    CWORD cy_sob
    CWORD cy_snl
    CWORD cy_snt
    CWORD cy_snr
    CWORD cy_snb
    CWORD cy_ovl
    CWORD cy_ovt
    CWORD cy_ovr
    CWORD cy_ovb

    ; --- the vertex tables (SPEC.md 67.2) ---------------------------------
    ; The whole perspective, spent once per layout: after these are filled,
    ; nothing in the drawing path does any 3D arithmetic at all.
    CBUF  cy_vx, CY_NDEPTH * CY_MAXV * 2
    CBUF  cy_vy, CY_NDEPTH * CY_MAXV * 2
    CBUF  cy_rsx, CY_MAXV * 4       ; the rim, scaled to pixels (x,y pairs)

    ; --- the walks --------------------------------------------------------
    CBUF  cy_walk, CY_MAXV * GLS_SZ
    CBUF  cy_wrem, CY_MAXV * 2
    CBUF  cy_dsc, CY_MAXV * 4       ; the LSTEPV `dw block, count` array

    ; --- what is on the glass ---------------------------------------------
    CBUF  cy_ost, CY_NOBJ * CY_OBSZ

    ; --- the spikes. ADJACENT ON PURPOSE: cy_clearboard clears both in one
    ; pass, and cy_spk_mark/cy_spk_repair index them with the same register
    CBUF  cy_spk, CY_MAXLANE
    CBUF  cy_spkd, CY_MAXLANE

    ; --- the movers -------------------------------------------------------
    CBUF  cy_e_kind, CY_MAXENEM     ; 0FFh free, 0FEh dead-awaiting-erase
    CBUF  cy_e_lane, CY_MAXENEM
    CBUF  cy_e_tim, CY_MAXENEM
    CBUF  cy_e_dp, CY_MAXENEM * 2   ; depth in 8.8: the high byte IS the depth
    CBUF  cy_e_sp, CY_MAXENEM * 2

    CBUF  cy_s_act, CY_MAXSHOT      ; 0 free, 1 live, 2 spent-awaiting-erase
    CBUF  cy_s_lane, CY_MAXSHOT
    CBUF  cy_s_pierce, CY_MAXSHOT
    CBUF  cy_s_dp, CY_MAXSHOT * 2

    CBUF  cy_wdmg, CY_MAXLANE       ; lanes owing a web repair
    CWORD cy_wnext                  ; ...and the round robin's cursor
    CBUF  cy_e_rtm, CY_MAXENEM      ; the rim walk's own timer - NOT cy_e_tim,
                                    ; which a flipper is already using to change
                                    ; lane as it climbs
    CBUF  cy_x_act, CY_MAXESHOT
    CBUF  cy_x_lane, CY_MAXESHOT
    CBUF  cy_x_dp, CY_MAXESHOT * 2

    CBUF  cy_u_act, CY_MAXPU
    CBUF  cy_u_kind, CY_MAXPU
    CBUF  cy_u_lane, CY_MAXPU
    CBUF  cy_u_dp, CY_MAXPU * 2

    CBUF  cy_d_act, CY_MAXDBR
    CBUF  cy_d_x, CY_MAXDBR * 2
    CBUF  cy_d_y, CY_MAXDBR * 2
    CBUF  cy_d_vx, CY_MAXDBR * 2
    CBUF  cy_d_vy, CY_MAXDBR * 2

    ; --- the high-score table ---------------------------------------------
    CBUF  cy_hs, CY_NHS * 4
    CBUF  cy_hsn, CY_NHS * 3        ; three initials per row
    CWORD cy_hsidx                  ; the row cy_hs_submit just filled
    CBYTE cy_hsdirty                ; the table has changed since it was read
    CBYTE cy_hsload                 ; ...and whether it has been read at all
    CBYTE cy_ient                   ; initials entry is up
    CBYTE cy_ipos                   ; ...and how many characters are in
    CBUF  cy_ibuf, 4
    ; **512-ALIGNED, because int 13h reads it** (SPEC.md 2.4, 77.31). It is a
    ; few hundred bytes and it still matters: a single sector transferred from
    ; an unaligned base can straddle a 64KB physical page, dsk_runcap's floor
    ; hands that one sector to the BIOS anyway, and the DMA controller answers
    ; a straddle with error 09h. Whether this buffer lands on such a page is
    ; decided by where the heap put the region, which is not this file's to
    ; know - so it is aligned rather than hoped for.
    ;
    ; The 511 of slack is what the rounding below spends: CY_BSS is a
    ; PREPROCESSOR count and os88_image_end is not defined until after it, so
    ; the alignment cannot be done by bumping the count - it is done in the
    ; label, and the room for it has to be reserved here.
%assign CY_HSRAWOFF CY_BSS          ; ...and its offset, banked as a NUMBER:
    CBUF  cy_hsraw, CY_HSFSZ + 511  ; a CBUF label is a forward reference to
                                    ; os88_image_end and stays non-scalar, so
                                    ; the rounding past OS88_IMAGE_END cannot
                                    ; be written in terms of the label
    CBUF  cy_dfind, OSAPI_FIND_SZ   ; one OSAPI_FILE_FIND record (SPEC.md 19.9)
    CWORD cy_dbclus                 ; where we were standing before the visit
    CBYTE cy_dbdrv
    CBUF  cy_ibuf2, CY_INITW + 2    ; ...and the prompt it is lettered from

    OS88_BSS CY_BSS
    OS88_IMAGE_END

; ...and the rounding goes HERE, past OS88_IMAGE_END, because os88_image_end
; has to be defined before `cy_hsraw - $$` is a scalar NASM will let `&` touch.
CY_HSOFF    equ ((os88_image_end - $$) + CY_HSRAWOFF + 511) & ~511
cy_hsbuf    equ $$ + CY_HSOFF
%if (CY_HSOFF & 511) != 0
  %error "cyclone: cy_hsbuf must be 512-aligned - int 13h reads it (SPEC.md 2.4)"
%endif

