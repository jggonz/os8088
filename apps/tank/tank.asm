; =============================================================================
; os8088 - apps/tank/tank.asm
;
; TANK ATTACK, a first-person wireframe tank game in the vein of Atari's 1980
; arcade original by way of the 1983 MS-DOS port (SPEC.md 85). A .o88 package
; at org 0 owning a segment (SPEC.md 20.1), prefix `tk_`, embedded icon, no
; worker task, and no kernel change of any kind.
;
; It is here to be a LOAD, and the load is the point. The kernel's line
; primitive got a fast walk (SPEC.md 5.6.4.1), a batch (5.6.8) and a band
; composer (5.9) in the last few cycles; WIREFRAME (SPEC.md 78) exercises them
; in a window at twelve edges a frame. This exercises what is on the OTHER
; side of SPEC.md 53.7's fence - the machine, in a foreign mode, where NO
; kernel drawing slot is legal and every pixel is the app's - at about a
; hundred edges a frame with a game attached. docs/GFX-FSX-PLAN.md is what
; that exercise found.
;
; THE THREE THINGS THAT DECIDE THE WHOLE DESIGN
;
;  1. **A frame must not flash**, and on a 4.77 MHz 8088 that rules out
;     clearing the screen. A 320x240 Mode X clear is 9,600 word stores into
;     VGA memory - about 50 ms, a whole frame's budget, before a single line
;     is drawn. So nothing is ever cleared wholesale: every backend keeps a
;     PER-ROW DIRTY SPAN in units of 8 pixels (tk_span0/tk_span1), the walk
;     maintains it as it draws, and the next frame clears exactly those runs.
;     A typical frame lights ~1,500 pixels inside ~500 span words.
;
;  2. **The erase must not be VISIBLE**, which is a different problem from
;     making it cheap - SPEC.md 78.5 measures the difference and shows that
;     no ordering of erase-and-draw ON THE GLASS wins. So no backend erases on
;     the glass. Mode X and Hercules have more video memory than one screen,
;     so they PAGE FLIP; CGA mode 4 uses 16,000 of a CGA's 16,384 bytes and
;     cannot, so it composes the frame in a 16,000-byte RAM shadow and blits
;     only the dirty spans. Every pixel that reaches the glass is written
;     ONCE per frame, which is PERFORMANCE.md rule 2 met rather than traded.
;
;  3. **The geometry is the same game on three very different rasters.** The
;     projection is parameterised by a VIEWPORT (tk_vx/vy/vw/vh) and by two
;     scales, tk_sclx and tk_scly. The horizontal field of view is a constant
;     - so every adapter sees the same world - and the vertical scale is
;     derived from the viewport's PIXEL ASPECT, so a square is square on all
;     three. That is the whole of the per-adapter difference in the engine.
;
; The adapters, and why each got what it got:
;
;   VGA  -> FSXM_MODEX, 320x240x256, 3 pages. Mode X is what the request
;           named and it is the right answer for the reason its author
;           intended: unchaining buys PAGES, and a page is what makes an
;           erase invisible for free. Two of the three are used.
;   CGA  -> FSXM_CGA320, 320x200x4. The 1983 port's own mode, and its palette
;           1 (cyan / magenta / white on black) is the port's own colour
;           scheme: cyan world, magenta HUD, white gunsight and cracks.
;   HERC -> FSXM_HERC, 720x348 mono, with the game in a 640x200 viewport in
;           the MIDDLE of the screen. 720x348 has no 320x200 to offer, and a
;           full-screen 720x348 frame is 2.2x the pixels of the CGA one on
;           the adapter that can least afford them.
;
; Keys: A/D or left/right turn, W/S or up/down drive, space fires, P pauses,
; N starts a new game, F or Esc leaves the bracket.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'TANK', tk_entry, 1, OS88_STACK_256
                                ; THE WORKER'S STACK, declared
                                ; rather than defaulted (SPEC.md 8.7):
                                ; static 82 for tk_worker
                                ; over the 64-byte interrupt floor
                                ; that is 146, and 256 gives 1.75x

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) --------------------------
; The gunsight over a pyramid on the horizon - the game's own first screen.
;
;   ................
;   ......#..#......
;   ......#..#......
;   ................
;   .......##.......
;   ......####......
;   .....######.....
;   ....########....
;   ...##########...
;   ..############..
;   ################
;   ................
;   ......#..#......
;   ......#..#......
;   ................
;   ................
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0x0240
    dw 0x0240
    dw 0x0000
    dw 0x0180
    dw 0x03C0
    dw 0x07E0
    dw 0x0FF0
    dw 0x1FF8
    dw 0x3FFC
    dw 0xFFFF
    dw 0x0000
    dw 0x0240
    dw 0x0240
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0240
    dw 0x0240
    dw 0x0000
    dw 0x0180
    dw 0x03C0
    dw 0x07E0
    dw 0x0FF0
    dw 0x1FF8
    dw 0x3FFC
    dw 0xFFFF
    dw 0x0000
    dw 0x0240
    dw 0x0240
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

; =============================================================================
; Constants
; =============================================================================

; --- which raster we are on ---------------------------------------------------
TKB_NONE  equ 0                 ; windowed: no bracket, nothing to draw into
TKB_MODEX equ 1                 ; 320x240x256 planar, 3 pages - PAGE FLIP
TKB_CGA   equ 2                 ; 320x200x4 banked, 1 page  - SHADOW + BLIT
TKB_HERC  equ 3                 ; 720x348 mono, 4 banks     - SHADOW + BLIT

; --- the logical inks (SPEC.md 85.4) ------------------------------------------
; Named by what they MEAN, never by a colour, because the three adapters do
; not agree on how many colours there are: Mode X has 256, CGA has three on
; black and Hercules has one. So a call site asks for a MEANING and
; tk_setink looks it up in the live backend's own table - which is what lets
; VGA have a depth-cued palette without CGA growing a single instruction.
TKI_W0    equ 1                 ; scenery: near, mid, far
TKI_W1    equ 2
TKI_W2    equ 3
TKI_HUD   equ 4                 ; the panel
TKI_MARK  equ 5                 ; the gunsight, the cracks, GAME OVER
TKI_RIDGE equ 6                 ; the mountains...
TKI_HORIZ equ 7                 ; ...and the line they stand on
TKI_T0    equ 8                 ; an enemy tank: near, mid, far
TKI_T1    equ 9
TKI_T2    equ 10
TKI_SHELL equ 11                ; a shell in flight, and a radar blip
TKI_RADAR equ 12                ; the dial's rim, under its own sweep
TKI_MASK  equ 13                ; not a colour: every bit of ONE pixel, which
                                ; is what the erase walk ANDs away (85.8.1)
TKI_NINK  equ 14                ; ...and the tables' length

; --- the world (SPEC.md 85.5) -------------------------------------------------
; An 8192-unit TORUS, and the wrap is three instructions rather than a
; special case: mask a difference to 13 bits and fold the top half negative
; and every distance in the game is already the short way round.
TK_WORLD  equ 8192
TK_WMASK  equ TK_WORLD - 1
TK_WHALF  equ TK_WORLD / 2

TK_NEARZ  equ 48                ; the near plane, in world units
TK_FARZ   equ 7000              ; beyond this an object is not drawn at all
TK_EYE    equ 60                ; the gunner's height above the plane

TK_NOBJ   equ 18                ; object slots (SPEC.md 85.6)
TK_NSTAT  equ 12                ; ...of which the first 12 are scenery

OT_FREE   equ 0
OT_CUBE   equ 1
OT_PYR    equ 2
OT_BLOCK  equ 3
OT_TANK   equ 4
OT_SHELL  equ 5                 ; the player's
OT_ESHELL equ 6                 ; theirs

; --- the projection -----------------------------------------------------------
TK_MAXV   equ 16                ; vertices in the largest model
TK_MAXROW equ 240               ; the tallest viewport any backend offers

TK_LASTB  equ 79                ; the last byte of a viewport row - and it is
                                ; 79 on ALL THREE, which is the coincidence
                                ; SPEC.md 85.3 is built on: 320 pixels at 2bpp,
                                ; 640 at 1bpp and 320 Mode X byte-addresses are
                                ; each 80 bytes
TK_SHSEG  equ 16000             ; the CGA/Hercules shadow, in bytes
TK_SHKB   equ 16                ; ...as a claim

; --- gameplay -----------------------------------------------------------------
TK_TURN   equ 2                 ; angle units per TICK at full lock
TK_MAXSTEP equ 3                ; ...and the most a frame may ever spend of
                                ; them, which tk_steps caps a stall at. It
                                ; lives here rather than beside tk_steps
                                ; because TK_MAXSTEP * TK_TURN is the COARSEST
                                ; heading lattice any machine puts the player
                                ; on, and SPEC.md 85.6.5.8 derives the aim
                                ; box from exactly that
TK_SPEED  equ 26                ; world units per frame, forward
TK_RSPEED equ 16                ; ...and reversing
TK_SHVEL  equ 130               ; a shell's speed, world units a TICK
TK_SHLIFE equ 30                ; ...and its life in ticks, so its reach is
                                ; 3,900 units - inside the 8,192 the world
                                ; wraps at, or a missed shot comes back and
                                ; shoots you in the back
TK_LIVES  equ 4
TK_HITR   equ 150               ; a shell within this of a target is a hit

; --- the ground the player starts on has to be drivable (SPEC.md 85.6.6) -----
; tk_newgame scattered TK_NSTAT pieces of scenery uniformly over the torus and
; stood the player at the origin without ever comparing the two. tk_blocked
; refuses a destination within 300 of a piece and a step is only TK_SPEED, so a
; player who starts inside that box cannot get out of it in ANY direction:
; reported as "sometimes I spawn into an obstacle, and can only turn, never
; drive", and modelled at 5.27% of games - one in nineteen - with another 2.15%
; fenced in on some headings but not all.
;
; TK_CLEARR is twice tk_blocked's own box, and the margin is what makes it a
; proof rather than an improvement: a piece this far off cannot refuse even the
; FIRST step, because 300 + TK_SPEED is 326. Modelled again with the rule in,
; over the same 40,000 games: zero stuck and zero fenced.
TK_CLEARR equ 600               ; ...and no piece may stand nearer the spawn

; --- aiming, and what the TURN's own step costs it (SPEC.md 85.6.5) -----------
; A heading is a byte, so a unit is 1.40625 degrees and TK_TURN spends two of
; them a TICK. tk_input latches once a FRAME and tk_pmove spends up to
; TK_MAXSTEP of them, so the finest turn a player can COMMAND is
; TK_TURN * min(ticks a frame, TK_MAXSTEP) - SIX units, 8.44 degrees, on both
; 1bpp adapters, where docs/GFX-FSX-PLAN.md 0 measures 6.06 and 4.32 fps. What
; that has to fit inside is tk_espoil's own window, 6,100/R units either side:
; 4.1 units wide at 3,000 and 3.1 at the shell's 3,900-unit reach. So the sweep
; steps clean over a distant tank and the phase of the press decides whether it
; was ever hittable.

; --- where the auto-aim applies, DERIVED and not chosen (SPEC.md 85.6.5.8) ---
; THE PLAYER HAS TO AIM IT THEMSELVES, so the gun only corrects a shot at a
; tank inside the box the CLOSED sight draws. Which makes the box's width a
; reachability question rather than a taste one: the player can only command a
; heading every TK_TURN * tk_lstep units and tk_lstep is capped at TK_MAXSTEP,
; so the coarsest lattice anywhere is Q = TK_MAXSTEP * TK_TURN = 6 units and
; the nearest heading to an arbitrary bearing is within Q/2 = 3 of it.
;
; A BOX NARROWER THAN Q/2 THEREFORE HAS BEARINGS NOTHING CAN REACH. At the 18
; px this was first drawn at - 2.65 units - it is 11.8% of them: one tank in
; nine that no sequence of presses can put in the box, which is SPEC.md
; 85.6.5's original defect in miniature. At 21 px it is none, ever, on any
; machine.
;
; And tk_aimfix's cap is Q/2 as well, which makes the pair sufficient rather
; than merely necessary: at the nearest reachable heading the error is inside
; the box AND inside the cap, so the correction is exact and the shot lands at
; every range. At the box's EDGE the residual is 0.24 units, against a window
; of 1.56 at the shell's longest reach.
TK_BOXQ   equ TK_MAXSTEP * TK_TURN * 2 + 1      ; Q/2 in quarter units, plus a
                                                ; quarter unit of margin
TK_BOXPX  equ 22                ; ...and what that is in tk_t_lock's own
                                ; 320x200 units: 3.25 * sclx * tan(1u) = 22.1

%if TK_BOXQ < TK_MAXSTEP * TK_TURN * 2
  %error "TK_BOXQ is under half the coarsest turn lattice: there are bearings no sequence of presses can put a tank inside the box"
%endif
; ...and the same inequality read the other way is what lets tk_aimfix clamp
; nothing at runtime: the cap is TK_MAXSTEP * TK_TURN * 2 at its largest, which
; is inside the box by construction, so a correction can never reach past the
; brackets the player is being asked to aim within.
%assign TK_BOXDRAWN (TK_BOXPX * 5882) / 10      ; the DRAWN box, quarters x1000
%if TK_BOXDRAWN > TK_BOXQ * 1000 + 588
  %error "tk_t_lock is drawn WIDER than the box the gun uses: the closed sight would promise a correction outside it"
%endif
%if TK_BOXDRAWN < TK_BOXQ * 1000 - 588
  %error "tk_t_lock is drawn NARROWER than the box the gun uses"
%endif

TK_RETQ   equ 20                ; the gunsight's OWN half-width, in quarter
                                ; angle units, and the outer gate on every
                                ; assist. tk_t_sight brackets at 34 of
                                ; 320x200's units and a unit of heading is
                                ; sclx*tan(1u) pixels, so it is
                                ; 34/(277*0.024536) = 5.00 units on both
                                ; 320-wide viewports and 68/(554*0.024536) =
                                ; 5.00 on Hercules - one constant on all three,
                                ; because 85.3's table sets sclx for the same
                                ; field of view everywhere
TK_ERRK   equ 163               ; 40.756 * 4: units per (ocx/ocz), in quarters
TK_HITQ   equ 24444             ; tk_espoil's window, in quarter units times
                                ; the range: TK_HITR*256*4/(2*pi). Divided by
                                ; the range it is how far off the sights a shot
                                ; may be and still hit - 6.2 quarters at the
                                ; shell's longest reach and 35 at 700
TK_LOCKHYS equ 2                ; ...widened by half a unit once the sight has
                                ; closed, so a target on the boundary does not
                                ; chatter the reticle and its blip
TK_BLIPHZ equ 2200              ; the acquisition blip: one tick, and BELOW the
TK_BLIPPRI equ 020h             ; muzzle's SND_PRI_PKG, so a shot always wins
TK_AIMBAN equ 36                ; ticks the mode banner stays up: two seconds

; --- what a shell cannot pass through (SPEC.md 85.6.2) ------------------------
; One radius per scenery type, indexed by its OT_*. A slab is wider than a
; cube and a pyramid is between them, and a single figure for all three either
; lets a shot through the middle of a slab or stops one that went past a cube.


; --- how long a tank must WAIT before it may shoot (SPEC.md 85.6.1) ---------
; DERIVED, not chosen. A tank can arrive anywhere, including directly behind
; the player, and the player has to be able to answer it:
;
;   turn 180 degrees   128 angle units at TK_TURN a tick  =  64 ticks
;   the shell's flight 3,250 units at TK_SHVEL a tick     =  25 ticks
;   and a second on top of that                           =  18 ticks
;                                                          ----------
;                                                            107 ticks
;
; TK_GRACE is that, rounded up. It is what a tank starts with; TK_REGRACE is
; what EVERY live tank is re-armed to when the player is hit, because a crack
; that clears straight into the next shell is the same complaint in a
; different place - the tank has been aiming throughout the freeze.
TK_GRACE  equ 115               ; ...before a NEW tank may fire
TK_REGRACE equ 70               ; ...and after it has hit you once
TK_HUNT   equ 45                ; below this it stops wandering and hunts
TK_COOL   equ 62                ; between rounds thereafter

; --- GAME OVER (SPEC.md 85.8) -------------------------------------------------
TK_GOANIM equ 18                ; ticks the fly-in takes: one second
TK_GOX    equ 16                ; where the lettering lands, in 320x200 space
TK_GOY    equ 58
TK_GOSH   equ 2                 ; its halo: 0 none, 1 shadow, 2 outline
TK_RADR   equ 4000              ; what the radar's rim means, in world units

; --- the attract window (SPEC.md 85.10) ---------------------------------------
; The frame; the content is two narrower and nineteen shorter, and the window
; manager rounds the width up so the content lands on a multiple of 8 - so a
; multiple of 8 here IS the content width. What decides it is the BOTTOM PAIR:
; 8 of margin, 128 of score band, 8 of gutter, 192 of instructions and 8 of
; margin. The logo used to share the top row with the table and set the width
; itself at 480, which left a band of nothing under its left half; it is
; centred over both of them now, and tkattr.inc asserts this number against the
; columns rather than restating them. The window is not resizable, because
; every row is placed off a constant rather than measured.
TK_WINW   equ 344
TK_WINH   equ 152

; --- scancodes this game reads directly (SPEC.md 9.7) -------------------------
TK_KA     equ 0x1E              ; A - turn left
TK_KD     equ 0x20              ; D - turn right
TK_KW     equ 0x11              ; W - forward
TK_KS     equ 0x1F              ; S - back

; =============================================================================
; tk_entry - the loader calls this once per instance (SPEC.md 20.1)
; =============================================================================
tk_entry:
    push si
    push di
    call OSAPI_VIDEO                ; AX = w, BX = h, CX = the dock's top row
    mov [tk_scrw], ax
    mov [tk_dock], cx

    mov al, KSC_SPACE               ; ARMING the scancode reader: the first
    call OSAPI_KEY_DOWN             ; answer is always "up" and this is where
                                    ; the SDK says to spend it (SPEC.md 9.7)

    call OSAPI_GET_TICKS
    call OSAPI_SRAND                ; the field, the tanks and their aim all
                                    ; walk down one stream
    call tk_hs_init                 ; the built-in table; the file replaces it
                                    ; at the first paint, which is a UI-task
                                    ; callback and therefore a legal place to
                                    ; read one from
    mov byte [tk_gosh], TK_GOSH     ; the lettering's halo (SPEC.md 85.8.1)
    mov byte [tk_at_scrt], TK_SCRT  ; the band's first roll is a whole interval
                                    ; away, not 256 of them: the loader zeroes
                                    ; bss and this counts DOWN
    mov byte [tk_at_blink], 1       ; ...and the play line starts LIT, for the
    mov byte [tk_at_blt], TK_BLINKON    ; same reason: a zeroed counter would
                                    ; hold the first dark phase for 256 wakes
                                    ; and a zeroed flag would open the window
                                    ; on a panel with no play line at all

    ; Centre the attract window in the desktop band. It is not resizable and
    ; it never draws the game - the game is the bracket - so one size does.
    mov ax, [tk_scrw]
    sub ax, TK_WINW
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [tk_tpl + WT_X], ax
    mov ax, [tk_dock]
    sub ax, MBAR_H
    sub ax, TK_WINH
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [tk_tpl + WT_Y], ax

    call tk_adapter                 ; which raster we would take, and whether
                                    ; the machine will give it to us
    call tk_newgame

    mov si, tk_tpl
    call OSAPI_WM_CREATE
    jc .full
    mov [tk_win], bx
    mov al, 1                       ; an 8-aligned content origin: every
    call OSAPI_WM_SNAP              ; font_run below reaches SPEC.md 6.1's
                                    ; single-store cell, and the score band's
                                    ; OSAPI_GFX_SCROLL has ends it accepts
    mov ax, tk_onresize             ; the card can change under us
    call OSAPI_WM_ONRESIZE          ; (SPEC.md 11.98), and tk_adapter is every
                                    ; fact this package holds about it
    mov si, tk_menus
    call OSAPI_MENU_SET
    mov si, tk_about                ; ...and 'About Tank Attack' above the
    call OSAPI_ABOUT_SET            ; Close the kernel already puts in our
                                    ; pull-down (SPEC.md 12.2). WINDOWED only:
                                    ; inside an fsx bracket the app owns every
                                    ; pixel and there is no bar to pull down
                                    ; (SPEC.md 53.7, docs/GFX-FSX-PLAN.md)
.full:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; tk_adapter - the raster this machine would give us, and the mode to ask for
;
; in:  nothing.  out: [tk_want] = a TKB_*, [tk_fsxm] = the FSXM_* to set,
;      [tk_caps] = the caps mask, and the menu item's caption.
; preserves every register (tk_onresize is a callback)
;
; EVERY BRANCH WRITES BOTH WAYS - SPEC.md 48's own lesson, and it is the same
; trap here: a CGA -> VGA switch that only ever SET a flag would leave a Mode X
; machine composing into a CGA shadow it no longer needs.
;
; OSAPI_FSX_CAPS is asked with our WINDOW in BX, so on a two-card machine the
; answer is about the display this window is actually on (SPEC.md 39.18.2) -
; which is also where the bracket will run.
; -----------------------------------------------------------------------------
tk_adapter:
    push ax
    push bx
    push dx
    mov bx, [tk_win]
    call OSAPI_FSX_CAPS             ; AX = settable ids, DL = that display's
    mov [tk_caps], ax               ; VID_* kind
    mov [tk_vidk], dl               ; ...which is also the attract window's ink
                                    ; question, and it is asked HERE so that a
                                    ; window dragged to the other card of a
                                    ; two-card machine re-answers it
    mov byte [tk_want], TKB_NONE
    mov byte [tk_fsxm], 0FFh

    test ax, 1 << FSXM_MODEX        ; VGA first: Mode X is the only mode here
    jz .cga                         ; with a spare page in it
    mov byte [tk_want], TKB_MODEX
    mov byte [tk_fsxm], FSXM_MODEX
    jmp short .say
.cga:
    test ax, 1 << FSXM_CGA320       ; ...then the 1983 port's own mode
    jz .herc
    mov byte [tk_want], TKB_CGA
    mov byte [tk_fsxm], FSXM_CGA320
    jmp short .say
.herc:
    test ax, 1 << FSXM_HERC
    jz .say
    mov byte [tk_want], TKB_HERC
    mov byte [tk_fsxm], FSXM_HERC
.say:
    mov dx, tk_s_play               ; and the menu says WHY not, off the same
    cmp byte [tk_want], TKB_NONE    ; predicate the command refuses on
    jne .ok                         ; (SPEC.md 47)
    mov dx, tk_s_playn
.ok:
    mov [tk_mi_game + 0], dx
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_onresize - the adapter changed under us (SPEC.md 11.98)
; in:  SI = our window, gfx lock held.  preserves all registers
; -----------------------------------------------------------------------------
tk_onresize:
    call tk_adapter
    ret

; =============================================================================
; The windowed half: an attract panel, and one command that leaves it
; =============================================================================
; The game itself is never drawn in the window, and that is a design decision
; rather than an omission. Every raster this game can draw on is a FOREIGN
; mode (SPEC.md 53.4), and in a foreign mode no kernel drawing slot is legal
; (SPEC.md 53.7) - so a windowed renderer would be a fourth backend, written
; against the desktop's geometry, for a surface a fifth of the size. What the
; window carries instead is what a player needs BEFORE and AFTER a session:
; the controls, the score, and the reason the machine cannot run it if it
; cannot.

; -----------------------------------------------------------------------------
; tk_paint - W_PAINT: the attract panel
; in:  SI = window ptr; gfx lock held.  preserves all registers
; -----------------------------------------------------------------------------
tk_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tk_hs_load                 ; once, and here rather than in the entry
    mov bx, [tk_win]                ; proc: SPEC.md 20.6 rule 7
    call OSAPI_WM_CONTENT
    mov [tk_winox], ax
    mov [tk_winoy], dx
    mov bx, [tk_win]
    call OSAPI_WM_GEOM              ; CX/DX = the content box
    jc .out
    mov [tk_cw], cx
    mov [tk_ch], dx
    call tk_at_layout

    mov al, CBLACK                  ; the panel is the game's own field: black
    call OSAPI_SET_COLOR            ; ground, and every run below letters onto
    mov ax, [tk_winox]              ; it opaquely, so no cell is ever drawn
    mov bx, [tk_winoy]              ; twice (SPEC.md 6.1)
    mov cx, ax
    add cx, [tk_cw]
    dec cx
    mov dx, bx
    add dx, [tk_ch]
    dec dx
    call OSAPI_GFX_FILL

    call tk_logo_full               ; whatever the animation had drawn so far,
    call tk_at_text                 ; and then the parts that never move
    call tk_band_full
    call tk_at_resume               ; the two gleam cursors are absolute, so a
    call tk_hire                    ; window that MOVED has to re-init them
    cmp byte [tk_abon], 0           ; ...and the About card LAST, over the
    je .out                         ; attract panel it is opaque about (20.5.1)
    push si
    mov bx, [tk_win]
    mov si, tk_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed
    pop si
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_onkey - W_ONKEY.  in: AL = ascii, SI = window; gfx lock held
; -----------------------------------------------------------------------------
tk_onkey:
    push ax
    call tk_abdismiss               ; any key takes the credits down, and is
    jc .out                         ; spent doing it
    cmp al, 'f'
    je .go
    cmp al, 'F'
    je .go
    cmp al, 0x0D
    je .go
    cmp al, 'n'
    je .new
    cmp al, 'N'
    jne .out
.new:
    call tk_newgame
    call tk_paint                   ; the lock is held in a key callback, so
.out:                               ; the panel is redrawn here and now
    pop ax
    ret
.go:
    call tk_cmd_play
    jmp short .out

; -----------------------------------------------------------------------------
; tk_onclick - W_ONCLICK: a click in the panel starts a session too
; -----------------------------------------------------------------------------
tk_onclick:
    call tk_abdismiss               ; the credits are up: this click takes them
    jc .out                         ; down rather than starting a session
    call tk_cmd_play
.out:
    ret

; -----------------------------------------------------------------------------
; tk_oncmd - the menu handler.  in: AL = item index within the menu
; -----------------------------------------------------------------------------
tk_oncmd:
    push ax
    push bx
    call tk_abdismiss               ; a menu pick takes the credits down first,
    cmp al, 0                       ; and then does what it says
    jne .n1
    call tk_cmd_play
    jmp short .out
.n1:
    cmp al, 1
    jne .out
    call tk_newgame
    call tk_paint
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Menus, strings and the attract panel's own table
; =============================================================================
    OS88_MENUSET tk_menus, tk_m_name, tk_oncmd
        OS88_MENU tk_m_game, tk_mi_game, 2
    OS88_MENUSET_END tk_menus

tk_m_name:   db 'Tank Attack', 0
tk_m_game:   db 'Game', 0
tk_mi_game:  dw tk_s_play, tk_s_new    ; item 0's caption is rewritten by
tk_s_play:   db 'Play', 0              ; tk_adapter when no mode can be had
tk_s_playn:  db 'Play (no mode)', 0
tk_s_new:    db 'New Game', 0

tk_ttl:      db 'Tank Attack', 0
tk_s_high:   db 'HIGH', 0
tk_s_pl1:    db 'PL 1', 0
; What a shell cannot pass through, by the object's own OT_* (SPEC.md 85.6.2).
; A slab is wider than a cube and a pyramid sits between them; one figure for
; all three either lets a shot through the middle of a slab or stops one that
; clearly went past a cube.
tk_rstop:    db 0, 110, 130, 165

tk_s_range:  db 'ENEMY IN RANGE', 0
tk_s_score:  db 'SCORE ', 0
tk_s_gnew:   db 'N - NEW GAME', 0
tk_s_gexit:  db 'F - EXIT', 0
tk_s_enter:  db 'PRESS ENTER', 0

; =============================================================================
; 'About Tank Attack' - the credit card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; WINDOWED ONLY, and that is the architecture rather than a choice: the game
; itself runs inside an fsx bracket where this package owns every pixel and no
; kernel drawing slot is legal at all (SPEC.md 53.7, docs/GFX-FSX-PLAN.md), so
; there is no bar to pull the item down from. The attract panel is where the
; kernel's chrome exists, and the card goes on it.
;
; The attract worker animates the same content two ticks at a time, so
; tk_at_step checks [tk_abon] under the lock it has just taken and drops the
; whole frame - the arkanoid/tracker pattern.

; -----------------------------------------------------------------------------
; tk_about - the OSAPI_ABOUT_SET handler (slot 0x01E0)
; in:  SI = our window ptr; the UI task, gfx lock HELD
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
tk_about:
    push bx
    push si
    mov byte [tk_abon], 1
    mov bx, si
    mov si, tk_ablines
    call os88ui_about               ; arms the clip itself: a menu dispatch
    pop si                          ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; -----------------------------------------------------------------------------
; tk_abdismiss - take the card down if it is up
; in:  gfx lock held ([tk_win] names the window; every caller is a callback of
;      ours and the worker never calls this)
; out: CF = 1 the click or key was spent doing it; preserves every register
;
; tk_paint is the whole attract panel, which is what the card covered AND what
; the worker skipped while it was up - one repaint settles both.
; -----------------------------------------------------------------------------
tk_abdismiss:
    cmp byte [tk_abon], 0
    je .none
    push bx
    mov byte [tk_abon], 0
    mov bx, [tk_win]
    call OSAPI_WM_CLIP_SET          ; nothing has armed a region for a click
    jc .gone                        ; or a key (SPEC.md 11.3)
    call tk_paint
.gone:
    pop bx
    stc
    ret
.none:
    clc
    ret

; --- the About card's lines (SPEC.md 20.5.1) ----------------------------------
tk_ablines:
    dw tk_ab1, tk_ab2, tk_ab3, 0
tk_ab1:      db 'Tank Attack for os8088', 0
tk_ab2:      db 0
tk_ab3:      db 'Contributed by Elendilon', 0

tk_tpl:
    dw 0, 0, TK_WINW, TK_WINH
    dw tk_ttl, tk_paint, tk_onkey, tk_onclick

%include "tklogo.inc"
%include "tkover.inc"
%include "tkhs.inc"
%include "tkraster.inc"
%include "tk3d.inc"
%include "tkgame.inc"
%include "tkattr.inc"

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes TK_BSS bytes after the image, and
; every name below is an offset from os88_image_end)
; =============================================================================
%assign TK_BSS 0
%macro ZWORD 1
%1 equ os88_image_end + TK_BSS
%assign TK_BSS TK_BSS + 2
%endmacro
%macro ZBYTE 1
%1 equ os88_image_end + TK_BSS
%assign TK_BSS TK_BSS + 1
%endmacro
%macro ZBUF 2
%1 equ os88_image_end + TK_BSS
%assign TK_BSS TK_BSS + (%2)
%endmacro

; --- the desktop side ---------------------------------------------------------
    ZWORD tk_scrw
    ZWORD tk_dock
    ZWORD tk_win
    ZWORD tk_winox
    ZWORD tk_winoy
    ZWORD tk_cw
    ZWORD tk_ch
    ZWORD tk_caps
    ZBYTE tk_want
    ZBYTE tk_fsxm
    ZBYTE tk_vidk                   ; the VID_* kind of the display we are on
    ZBUF  tk_numbuf, 14
    ZBYTE tk_ctry
    ZBYTE tk_espread
    ZBYTE tk_espoiled               ; ...and whether it was a deliberate miss
    ZBYTE tk_slow                   ; this tank is still wandering
    ZWORD tk_last                   ; the tick the last frame was stepped at

; --- the raster ---------------------------------------------------------------
    ZBUF  tk_fsi, FSI_SIZE
    ZBYTE tk_back
    ZWORD tk_vx
    ZWORD tk_vy
    ZWORD tk_vw
    ZWORD tk_vh
    ZWORD tk_vcx
    ZWORD tk_vcy
    ZWORD tk_sclx
    ZWORD tk_scly
    ZWORD tk_ridgek
    ZWORD tk_tseg
    ZWORD tk_tbase
    ZWORD tk_page0
    ZWORD tk_page1
    ZBYTE tk_npage
    ZWORD tk_shseg
    ZBYTE tk_par
    ZWORD tk_spcur
    ZWORD tk_spprv
    ZBUF  tk_spanp, 4
    ZWORD tk_lineproc
    ZWORD tk_elineproc              ; ...and the same walk taking ink away
    ZWORD tk_hrunproc
    ZWORD tk_glyphproc
    ZBYTE tk_ink
    ZBYTE tk_ink8
    ZBYTE tk_pat0
    ZBYTE tk_pat3
    ZBYTE tk_inkpat
    ZWORD tk_nibp
    ZWORD tk_inktab                 ; the live backend's TKI_* -> device map

; The two span sets, with slack either side: a walk's LAST step can advance
; the row pointer one past the row it drew on, and the guard is cheaper than
; a test in the inner loop (SPEC.md 85.3.2).
    ZBUF  tk_spguard0, 8
    ZBUF  tk_spans0, TK_MAXROW * 2
    ZBUF  tk_spguard1, 8
    ZBUF  tk_spans1, TK_MAXROW * 2
    ZBUF  tk_spguard2, 8
    ZBUF  tk_devoff, TK_MAXROW * 2

; --- the clipper and the walk -------------------------------------------------
    ZWORD tk_cx1                    ; these four are CONTIGUOUS and indexed
    ZWORD tk_cy1                    ; as a pair by tk_clip_x / tk_clip_y
    ZWORD tk_cx2
    ZWORD tk_cy2
    ZWORD tk_lx
    ZWORD tk_ly
    ZWORD tk_err
    ZWORD tk_e1
    ZWORD tk_e2
    ZWORD tk_cnt
    ZWORD tk_ystep
    ZWORD tk_sistep
    ZBYTE tk_xdir
    ZBYTE tk_steep

; --- the geometry -------------------------------------------------------------
    ZWORD tk_csin
    ZWORD tk_ccos
    ZWORD tk_osin
    ZWORD tk_ocos
    ZWORD tk_ocx
    ZWORD tk_ocz
    ZWORD tk_mdl
    ZBYTE tk_dtype
    ZBYTE tk_rotdone
    ZBUF  tk_rotx, TK_MAXV * 2
    ZBUF  tk_roty, TK_MAXV * 2
    ZBUF  tk_rotz, TK_MAXV * 2
    ZBUF  tk_cxv,  TK_MAXV * 2
    ZBUF  tk_cyv,  TK_MAXV * 2
    ZBUF  tk_czv,  TK_MAXV * 2
    ZBUF  tk_sxv,  TK_MAXV * 2
    ZBUF  tk_syv,  TK_MAXV * 2
    ZBUF  tk_fv,   TK_MAXV * 2
    ZWORD tk_na
    ZWORD tk_nb
    ZWORD tk_nnum
    ZWORD tk_nden
    ZWORD tk_ncx
    ZWORD tk_ex1
    ZWORD tk_ey1
    ZWORD tk_ex2
    ZWORD tk_ey2
    ZBUF  tk_ridgex, 25 * 2
    ZBUF  tk_rpy, 25 * 2
    ZBYTE tk_ra

; --- the world ----------------------------------------------------------------
    ZWORD tk_px
    ZWORD tk_pz
    ZBYTE tk_pa
    ZBYTE tk_kturn                  ; the held keys, latched by tk_input and
    ZWORD tk_kdrv                   ; spent one step at a time by tk_pmove
    ZBYTE tk_aimh                   ; the heading THIS shell leaves at
    ZBYTE tk_lstep                  ; steps the LAST frame owed - TRIM's whole
                                    ; input, because the lattice the player is
                                    ; stuck on is TK_TURN times this
    ZWORD tk_aimw                   ; tk_besttarget's window, best, pick and
    ZWORD tk_aimb                   ; the picked target's RANGE, in quarter
    ZWORD tk_aimq                   ; units but for the last
    ZWORD tk_aimz
    ZBYTE tk_locked                 ; the sight is closed on a target...
    ZBYTE tk_lockwas                ; ...and was last frame, which is what makes
                                    ; the blip an ACQUISITION and not a tone
    ZBUF  tk_ox, TK_NOBJ * 2        ; the world's, indexed by slot x 2
    ZBUF  tk_oz, TK_NOBJ * 2
    ZBUF  tk_otype, TK_NOBJ
    ZBUF  tk_oa, TK_NOBJ
    ZBUF  tk_otim, TK_NOBJ
    ZBUF  tk_ocool, TK_NOBJ
    ZWORD tk_score
    ZWORD tk_score_hi
    ZWORD tk_hiscore
    ZWORD tk_hiscore_hi
    ZBYTE tk_lives
    ZWORD tk_ntank                  ; tanks faced this game; the difficulty
                                    ; curve's only input (SPEC.md 85.6.3)
    ZBYTE tk_dead
    ZBYTE tk_spawn
    ZBYTE tk_quit
    ZBYTE tk_pause
    ZBYTE tk_over                   ; 0 playing, 1 the fly-in, 2 the menu
    ZWORD tk_ovt                    ; ...and the tick the fly-in began at
    ZWORD tk_goq                    ; how much of the journey is left, 0..256
    ZWORD tk_gok
    ZWORD tk_gdx
    ZWORD tk_gdy
    ZWORD tk_gtx
    ZWORD tk_gokx
    ZWORD tk_hox                    ; the halo pass's displacement (85.8.1)
    ZWORD tk_hoy
    ZWORD tk_hset                   ; ...where it has got to in the table
    ZWORD tk_lsave                  ; ...and the drawing walk, while it runs
    ZBYTE tk_gosh                   ; 0 none, 1 shadow, 2 outline
    ZBYTE tk_still                  ; the settled frame is already on the glass
    ZBYTE tk_pquick                 ; ...and only its prompt needs redrawing
    ZBUF  tk_wbuf, 24

; --- the high scores (SPEC.md 85.9) -------------------------------------------
    ZBUF  tk_hs, TK_NHS * 4         ; six dwords...
    ZBUF  tk_hsn, TK_NHS * 3        ; ...and three initials each
    ZBUF  tk_hsbuf, TK_HSFSZ        ; the file, as it sits on the disk
    ZWORD tk_hsidx                  ; the row this game took, x4; TK_NHS*4 none
    ZBYTE tk_hsload
    ZBYTE tk_ient                   ; the initials prompt is up
    ZBYTE tk_ipos
    ZBUF  tk_ibuf, 4
    ZWORD tk_dbclus                 ; where we were standing before a save
    ZBYTE tk_dbdrv
    ZBUF  tk_dfind, OSAPI_FIND_SZ
    ZWORD tk_frames                 ; frames rendered this session; the only
                                    ; instrument in the package, read from
                                    ; outside by tests/bzfps.py

; --- the simulation's scratch -------------------------------------------------
    ZWORD tk_sl
    ZWORD tk_sl2
    ZWORD tk_ndx
    ZWORD tk_ndz
    ZWORD tk_nx
    ZWORD tk_nz
    ZWORD tk_hx
    ZWORD tk_hz
    ZWORD tk_tx
    ZWORD tk_tz
    ZWORD tk_adx
    ZWORD tk_adz
    ZWORD tk_across
    ZWORD tk_adot
    ZWORD tk_sx
    ZWORD tk_sz
    ZWORD tk_nsl
    ZWORD tk_nsl2
    ZBYTE tk_ra2
    ZWORD tk_rrange
    ZWORD tk_wallr                  ; the box test's reach, this call

; --- the panel ----------------------------------------------------------------
    ZWORD tk_hsx
    ZWORD tk_hsy
    ZWORD tk_rcx
    ZWORD tk_rcy
    ZWORD tk_rr
    ZWORD tk_txx
    ZWORD tk_txy
    ZWORD tk_tox
    ZWORD tk_toy
    ZBYTE tk_sweep
    ZBUF  tk_rrx, 32
    ZBUF  tk_rry, 32
    ZWORD tk_gtab
    ZWORD tk_gseg
    ZBYTE tk_gfirst
    ZBYTE tk_glast
    ZBUF  tk_gbuf, 8
    ZWORD tk_gcol
    ZWORD tk_gcol2

; --- the attract window (SPEC.md 85.10) ---------------------------------------
    ZWORD tk_at_lx                  ; the logo's origin within the content
    ZWORD tk_at_ly
    ZBYTE tk_ctube                  ; the logo's two inks, settled per adapter
    ZBYTE tk_cgleam
    ZWORD tk_lgi                    ; the word cell the gaps are on...
    ZWORD tk_lgp                    ; ...and its glyph: first point, count,
    ZWORD tk_lgnp                   ; and the segment the two of them meet at
    ZWORD tk_lge
    ZWORD tk_lgox                   ; the cell's absolute origin
    ZWORD tk_lgoy
    ZWORD tk_lgflen                 ; the two halves, in walk pixels
    ZWORD tk_lgblen
    ZWORD tk_lgbp                   ; tk_lgpair's second cursor, out of DI
    ZWORD tk_at_pseg                ; the segment the draw-in has reached
    ZWORD tk_bx                     ; the score band's 8-aligned absolute left
    ZWORD tk_at_prog                ; segments of the stroke drawn so far
    ZBYTE tk_at_glow                ; 0 still drawing in, 1 gleaming
    ZBYTE tk_at_lag                 ; wakes the tail still owes the head
    ZBYTE tk_at_scrt                ; wakes until the band rolls
    ZWORD tk_at_scr                 ; the table row at the top of the band
    ZBYTE tk_at_blink               ; the play line: lit or dark, and the wakes
    ZBYTE tk_at_blt                 ; left of that phase (SPEC.md 85.10.3)
    ZWORD tk_e1x                    ; one segment's ends, in screen coords
    ZWORD tk_e1y
    ZWORD tk_e2x
    ZWORD tk_e2y
    ZWORD tk_cadv                   ; tk_cur_adv's pixels still owing...
    ZBYTE tk_cink                   ; ...its ink...
    ZWORD tk_cn                     ; ...and what this LSTEP takes of them
    ZWORD tk_due                    ; the tick the worker's next wake is due
    ZBYTE tk_hired
    ZBYTE tk_abon                   ; the About card is up (SPEC.md 20.5.1)
    ZBUF  tk_curfh, TKC_SZ          ; the two gaps and their two tails: one
    ZBUF  tk_curft, TKC_SZ          ; walk block each, plus where it has got
    ZBUF  tk_curbh, TKC_SZ          ; to and what it still owes this glyph
    ZBUF  tk_curbt, TKC_SZ

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT            ; the standard About card, and NOTHING else:
%define OS88UI_NOBTN            ; the attract panel is the game's own chrome
%include "os88ui.inc"

    OS88_BSS TK_BSS
    OS88_IMAGE_END
