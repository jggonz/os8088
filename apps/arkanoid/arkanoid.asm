; =============================================================================
; os8088 - apps/arkanoid/arkanoid.asm
;
; Arkanoid, the ninth software package (SPEC.md 44). A .o88 binary at org 0
; that owns a segment (SPEC.md 20.1) and reaches every service through the API
; table in os88api.inc. Prefix `ark_`, embedded icon, one worker task.
;
; Four things shape it:
;
;  - **The paddle reflects, it does not re-launch.** A bounce mirrors vy and
;    KEEPS vx, then adds where along the paddle it landed (ark_zbias) and how
;    the paddle itself was moving (ark_english) to the vx it kept. An earlier
;    version assigned both from a zone table instead, and that is what made
;    the game feel arbitrary: a ball arriving steeply from the left and one
;    drifting in from the right left the paddle identically if they landed in
;    the same zone, so a rally had no continuity at all. A serve has no
;    incoming direction to build on, so the flick IS the aim: it is thrown the
;    way the KEY is pointing rather than the way the paddle is moving - three
;    rungs, nothing latched serving straight up, a flick serving sideways and a
;    hold serving harder (ark_throw). That is the one place intent beats
;    physics, and the taper's replacement below is what forced the distinction.
;
;  - **The game IS the worker task.** A ball has to keep moving between
;    keystrokes, and a window callback only runs when something happens to the
;    window, so the loop lives in ark_worker (SPEC.md 20.6) - the same shape as
;    apps/fractal, sleeping one tick a frame for ~18 fps. Everything the UI
;    task does is set a word the worker reads.
;
;  - **A hold is the KEY BEING DOWN, and the kernel is asked (SPEC.md 9.7).**
;    int 16h gives keypresses and no key-up, and a typematic repeat is
;    byte-identical to a fresh press, so "is left held" cannot be asked OF IT -
;    and every version of this file until now inferred a hold from the INTERVAL
;    between events instead. Two of those inferences shipped and both failed in
;    the same place, because the evidence they need is not there to be read.
;    Reading the typematic DELAY meant latching the paddle in motion for longer
;    than ~9 ticks so the first repeat would land while the latch still stood,
;    and that latch doubled as the tap: 11 ticks whether the player wanted them
;    or not, 44 pixels, a whole paddle width, for a key already released.
;    Reading the RATE fixed the tap - two events inside ~2 ticks are a repeat
;    and nothing else produces that - but it cannot see the delay at all, so
;    between the tap running out at 7 ticks and the first repeat arriving at 9
;    the paddle STOPPED, and then went again: measured on a cycle-accurate
;    5150, six ticks at 5 px, two ticks at zero, then eight. That is the "go,
;    stop, then go" the field reported, and no arrangement of the constants
;    removes it - a tap that ends before the delay does stops, and one that
;    does not is the 44-pixel tap again. The rate reading also lost the key
;    entirely when a SECOND key was pressed, the keyboard repeating only the
;    last key down, so serving with Space froze the paddle until the arrow was
;    released and pressed afresh.
;    So the kernel tracks the scancodes - make and break, which do carry it -
;    and ark_do_paddle asks once a tick. What is left is three speeds with
;    nothing between them: stopped, tapping at ARK_PSTEP for ARK_PTAP ticks and
;    then stopping mid-stride, and holding at ARK_PFAST, promoted the moment
;    the key has been down for those same ARK_PTAP ticks. The first 35 pixels
;    of a hold are pixel-for-pixel a tap, because until the tap would have
;    ended the player has not done anything different. [ark_pspd] is those
;    three values and is still the whole state machine.
;
;  - **Sound comes from the worker**, which the SDK's worker-safe list does not
;    mention and which is nevertheless correct: `snd_req_inst` (SPEC.md 34.3)
;    falls back to the running task's `T_INST` when no callback is being
;    dispatched, so a tone asked for by our worker is attributed to our
;    instance and released by `snd_release_inst` at teardown like any other.
;    Only the two BLOCKING slots - SND_PLAY and the retired SND_STREAM - are
;    forbidden there, and for a different reason: they freeze the desktop for
;    the length of the clip. Every tone here is duration-limited, so it
;    self-expires via `snd_tick` and the worker never has to turn it off.
;
;  - **Two metric sets, chosen by screen height**, exactly as apps/solitaire
;    does: a 24x10 brick on VGA and Hercules, 20x7 on CGA's 200 rows, with the
;    rows, the paddle and the ball all scaling with them. Colour is never the
;    only carrier (SPEC.md 39.4): a two-hit brick carries a white notch rather
;    than a second colour, an armed laser paddle grows two muzzles rather than
;    merely turning red - and those muzzles are where the bolts come from, the
;    only two x a bolt can reach the end columns from - and a capsule is
;    identified by the LETTER on it, since five colours cannot survive a
;    reduction to three inks. The brick rows go
;    further - no row colour may reduce to BLACK, or that row disappears into
;    the background entirely, so the palette is drawn from the white and
;    dither classes only and alternates between them.
;
; Keys: left/right move, Space launches the ball (fires the laser when one is
; armed, and resumes when the game is paused - it never pauses), P pauses and
; resumes, N starts a new game.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'ARKANOID', ark_entry, 1, OS88_STACK_192
                                ; THE WORKER'S STACK, declared
                                ; rather than defaulted (SPEC.md 8.7):
                                ; static 64 for ark_worker
                                ; over the 64-byte interrupt floor
                                ; that is 128, and 192 gives 1.5x
                                ; (76 and 1.37x before size pass 1:
                                ; ark_update banks nothing now)

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A brick wall over a ball over the paddle - the whole game in three objects.
; The mask is the wall block plus the two sprites, so the glyph sits on a clean
; white underlay and the mortar lines read as white against it.
;
;   data                       mask
;   ################           ################
;   #...#...#...#...           ################
;   #...#...#...#...           ################
;   ################           ################
;   ..#...#...#...#.           ################
;   ..#...#...#...#.           ################
;   ################           ################
;   ................           ................
;   ................           ................
;   .......##.......           .......##.......
;   .......##.......           .......##.......
;   ................           ................
;   ................           ................
;   ................           ................
;   ...##########...           ...##########...
;   ...##########...           ...##########...
    OS88_ICON16
    dw 0xFFFF                       ; 16 mask rows (white underlay)
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000
    dw 0x0000
    dw 0x0180
    dw 0x0180
    dw 0x0000
    dw 0x0000
    dw 0x0000
    dw 0x1FF8
    dw 0x1FF8
    dw 0xFFFF                       ; 16 data rows (black pixels)
    dw 0x8888
    dw 0x8888
    dw 0xFFFF
    dw 0x2222
    dw 0x2222
    dw 0xFFFF
    dw 0x0000
    dw 0x0000
    dw 0x0180
    dw 0x0180
    dw 0x0000
    dw 0x0000
    dw 0x0000
    dw 0x1FF8
    dw 0x1FF8
    OS88_ICON16_END

; --- the board ------------------------------------------------------------------
ARK_COLS    equ 10                  ; bricks across, both metric sets

; --- THE THREE FIELDS A WINDOW SIZE IS A FUNCTION OF ------------------------
; Named here and spent twice: once in the metric records at the bottom of this
; file, and once in the per-adapter preferences ark_entry publishes (SPEC.md
; 11.100.1). Two places that must agree about a number is how a size drifts
; from the layout it is meant to hold, and the answer is that only one of them
; carries the number.
ARK_BW_BIG   equ 24                 ; brick width, VGA and Hercules
ARK_RAIL_BIG equ 4                  ; side rail
ARK_CHW_BIG  equ 300                ; content height the layout wants
ARK_BW_SML   equ 20                 ; ...and CGA's 200 rows
ARK_RAIL_SML equ 3
ARK_CHW_SML  equ 137

; ark_entry's own arithmetic, as constants: content = bricks + both rails, and
; the frame is that plus the 1px border either side / the title bar and one
; more. A GENEROUS height is the idiom (docs/plans/completed/WINDOW-SIZING-PLAN.md 9) - no
; package can know how tall another adapter's band is, so publish a number the
; kernel's clamp can bring down. On Hercules it comes back 303.
ARK_PREF_BW  equ ARK_BW_BIG * ARK_COLS + 2 * ARK_RAIL_BIG + 2
ARK_PREF_BH  equ ARK_CHW_BIG + TITLE_H + 1
ARK_PREF_SW  equ ARK_BW_SML * ARK_COLS + 2 * ARK_RAIL_SML + 2
ARK_PREF_SH  equ ARK_CHW_SML + TITLE_H + 1

; ...AND THE FLOOR IS ONE PIXEL SHORTER THAN THE PREFERENCE, because a floor
; is the one number no clamp may cut. On a CGA wm_fit's height cap is
; vid_dock_y0 - MBAR_H - 1 = 155 - the `dec di` that keeps a frame off the
; dock strip, which is the same pixel ark_entry's own `sub bx, MBAR_H + 1`
; below takes - and wm_min_floor runs AFTER that clamp and wins. Published as
; the floor, ARK_PREF_SH's 156 put it straight back, y floored at MBAR_H, and
; the frame's drop shadow landed on row 176: exactly vid_dock_y0. From there
; wm_dock_clear's `jae` is true for this window forever, so every window
; opened or moved over the game pays a dock repaint it did not owe.
ARK_MIN_SH   equ ARK_CHW_SML + TITLE_H
ARK_MAXROW  equ 6                   ; ...and the most rows either set uses
ARK_NZONE   equ 5                   ; paddle zones the bounce divides it into
                                    ; (ark_zbias/ark_zbq)
ARK_CELLS   equ ARK_COLS * ARK_MAXROW
ARK_NMET    equ 12                  ; words in a metrics record (ark_met_*)

ARK_LIVES   equ 3
ARK_LIVEMAX equ 6                   ; spare-paddle slots drawn: more than this
                                    ; would run into the level field
; --- the paddle has three speeds and nothing between them (SPEC.md 44.2) ------
; [ark_pspd] IS the state machine: 0 stopped, ARK_PSTEP tapping, ARK_PFAST
; holding. Nothing ramps, tapers or coasts between them.
ARK_PTAP    equ 7                   ; ticks a TAP moves for, then stops dead.
                                    ; Times ARK_PSTEP this is the whole tap:
                                    ; 35 pixels. It is ALSO how long a HOLD
                                    ; spends at the slow speed before it is
                                    ; promoted, which is what makes the first
                                    ; 35 pixels of a hold identical to a tap -
                                    ; they have to be, because for those ticks
                                    ; the two are the same gesture
ARK_PHOLD   equ 4                   ; ...and what a tick with the key DOWN
                                    ; refills, so it is also exactly how long
                                    ; the paddle coasts once the key comes up.
                                    ; Refilled only when it has fallen BELOW
                                    ; this, so the first three ticks of a tap's
                                    ; countdown are untouched and a tap is
                                    ; still exactly ARK_PTAP long
ARK_VQ      equ 4                   ; QUARTER pixels: the sub-pixel unit BOTH
                                    ; the paddle (just below) and the ball
                                    ; (SPEC.md 44.3.2, further below) move in
ARK_PSTEP   equ 5 * ARK_VQ          ; tap speed: 5 px a tick, from the first
                                    ; tick of the press, held flat, then gone.
                                    ; Halfway to ARK_PFAST, which is what makes
                                    ; a tap read as a MOVE rather than a nudge
                                    ; and very nearly hides the step up to a
                                    ; hold. In quarter pixels because whole
                                    ; ones give a tap of 28, 35 or 42 pixels
                                    ; and nothing in between - the ladder the
                                    ; ball hit in SPEC.md 44.3.2, same fix
ARK_PFAST   equ 8 * ARK_VQ          ; hold speed: 8 px a tick, and a whole
                                    ; number of pixels so the accumulator never
                                    ; shows in a rally
ARK_THRTAP  equ 18                  ; QUARTER pixels of sideways speed a SERVE
ARK_THRHOLD equ 24                  ; gets from a flick, and from a hold. These
                                    ; are the two answers ark_throw gives, and
                                    ; it picks between them by asking WHICH KEY
                                    ; IS DOWN rather than how fast the paddle is
                                    ; going - see ark_throw for why the serve
                                    ; is the one place that reads the intent.
                                    ; They track the two SPEEDS: a flick that
                                    ; moves the paddle 5 px a tick has to throw
                                    ; harder than one that moved it 2, or the
                                    ; aim stops matching the gesture. The hold
                                    ; figure is ARK_VXMAX exactly - the
                                    ; flattest angle the game has - which is a
                                    ; ceiling a serve may ASK for and nothing
                                    ; afterwards can exceed; asserted below,
                                    ; where ARK_VXMAX is declared. Quarter
                                    ; units rather than the whole pixels these
                                    ; were, because the velocity SCALE below
                                    ; divides them and 3 and 4 have nowhere to
                                    ; go on a small screen

; --- ball velocity is in QUARTER pixels (SPEC.md 44.3.2) ----------------------
; It used to be whole pixels a frame, which made the speed ladder 3, 4, 5 and
; nothing in between - and 3 was too slow while 4 was a jump. A quarter-pixel
; unit with a per-axis remainder carried across frames buys fractional speeds
; for two adds and two shifts, and the collision walk below never sees it: the
; accumulator hands ark_do_ball a whole-pixel delta exactly as [ark_bvx] used
; to.
;
; It also fixes what a dead-centre paddle hit felt like. The walk takes
; max(|dx|,|dy|) single-pixel steps, so with vx at 0 the ball moved vymag
; pixels a frame and with vx at the ceiling it moved the ceiling - a centre
; hit was measurably the slowest shot in the game. At quarter resolution the
; two are 3.75 and 4, and ARK_VXMIN keeps a centre hit off the vertical
; entirely.
;
; ARK_VQ, the unit itself, is declared up with the paddle constants: the paddle
; came to need fractional speeds for the same reason and now shares it.
;
; --- and the whole family is SCALED PER ADAPTER (SPEC.md 44.3.3) --------------
; Every figure below is the BIG-metric value; each metrics record carries a
; percentage (ARK_VSCALE, in ark_met_*) that ark_entry applies to all of them
; once, at launch, into the bss words the game actually reads.
;
; It is one knob per adapter rather than ten because the ten are not
; independent: the ANGLE of a shot is vx over vy, the serve ceiling has to stay
; under ARK_VXMAX, ARK_VYFLOOR has to stay under ARK_VYBASE, and the paddle's
; english has to stay small against ARK_VXMAX or every bounce saturates the
; angle. Tuned separately they drift apart silently - and the drift shows up as
; "the ball feels wrong on this screen", which is exactly the report this
; change came from. Scaled together, an adapter is one number and the
; relationships hold by construction.
;
; What made it necessary: the speeds were absolute while ark_met_sml scales
; every SPATIAL number down for CGA, so CGA's rally band is 72px against VGA's
; 198 and the ball crossed it 2.7x as often - measured, not guessed. The
; percentages below are chosen so that BAND TRAVERSALS PER SECOND, not pixels
; per second, is what matches across the three adapters.
ARK_VXMAX   equ 6 * ARK_VQ          ; the flattest angle the ball may reach.
                                    ; vx accumulates across bounces, so it
                                    ; needs a ceiling or a rally converges on
                                    ; horizontal and stops coming down. It is
                                    ; NOT doubled with vy below: it has to stay
                                    ; clear of ARK_PFAST or the paddle cannot
                                    ; out-run the ball it is chasing
ARK_VXMIN   equ 2 * ARK_VQ          ; ...and the steepest one a PADDLE bounce
                                    ; may leave: a ball going straight up is
                                    ; covering no ground and reads as stalled.
                                    ; Walls and bricks are free to send it
                                    ; vertical - it is the shot the player
                                    ; aimed that has to stay lively. Doubled
                                    ; with vy, which is what keeps the steepest
                                    ; shot at the same ANGLE it always had
ARK_VYBASE  equ 30                  ; 7.5 px/frame: the rally speed on wall 1
ARK_VYSTEP  equ 6                   ; +1.5 a wall...
ARK_VYMAX   equ 10 * ARK_VQ         ; ...to a 10 px/frame ceiling
ARK_VYFLOOR equ 20                  ; 5 px/frame: Slow may not go below it
ARK_VYSLOW  equ 4                   ; ...and takes 1 px/frame at a time

; The vertical ladder is twice what it was. The walk is what makes that safe:
; ark_do_ball still takes max(|dx|,|dy|) SINGLE-PIXEL steps with a collision
; test after each, so 10 px/frame cannot tunnel through a 7px brick any more
; than 3.75 could - the frame just costs more steps.

%if ARK_THRHOLD > ARK_VXMAX
  %error "ark_throw would serve flatter than ARK_VXMAX, the game's own ceiling"
%endif
%if ARK_THRTAP > ARK_THRHOLD
  %error "a flick would serve flatter than a hold"
%endif
%if ARK_VYFLOOR > ARK_VYBASE
  %error "Slow's floor is above the opening rally speed"
%endif
%if ARK_VYBASE > ARK_VYMAX
  %error "the opening rally is already past the ceiling"
%endif
%if ARK_VXMAX >= ARK_PFAST
  %error "the ball would out-run the paddle sideways"
%endif

; powerup kinds, and the letter each capsule carries
PU_NONE     equ 0
PU_EXPAND   equ 1                   ; 'E' a wider paddle
PU_CATCH    equ 2                   ; 'C' the ball sticks until Space
PU_LASER    equ 3                   ; 'L' Space fires
PU_SLOW     equ 4                   ; 'S' the ball slows down
PU_LIFE     equ 5                   ; a HEART, and the only thing in the game
                                    ; that hands back a life. It carried an
                                    ; 'X' before, which said nothing about
                                    ; what it did - and an extra life is worth
                                    ; a glyph of its own rather than the one
                                    ; letter of five that a player has to
                                    ; learn by dying. The heart is drawn by
                                    ; ark_heart, not by the font: the kernel's
                                    ; ROM set is glyphs 32..126 (SPEC.md 6),
                                    ; so there is no character to ask for
PU_KINDS    equ 5
ARK_MAXPU   equ 3                   ; capsules falling at once
ARK_MAXSHOT equ 4                   ; bullets in the air at once - a volley is
                                    ; TWO, one per muzzle, so this is two
                                    ; presses' worth exactly as it was when a
                                    ; volley was a single bolt
ARK_PUCHANCE equ 8                  ; 1 in this many broken bricks drops one
; --- the composed capsule (SPEC.md 44.10.6) ---------------------------------
; A capsule is ONE gfx_blit1 now: body, edge, mark and the strip it vacated,
; composed into a 1bpp band at level start and put down in one call. It used to
; be SEVEN drawing calls a frame per capsule and the mark was a transparent
; pass over a body drawn in that same frame - so there was a letter-less
; instant, every frame, on every falling capsule.
ARK_SPW     equ 2                   ; band stride: 16 px, because gfx_blit1
                                    ; wants a whole number of BYTES and the
                                    ; capsule is 12. The four spare columns are
                                    ; paper, and paper is ARK_BG
ARK_SPTOP   equ 8                   ; blank rows ABOVE the capsule, which is
                                    ; how the ERASE rides in the same call: a
                                    ; capsule that fell `d` rows blits from row
                                    ; ARK_SPTOP-d for ARK_PUH+d rows, and the
                                    ; first d of them are background
ARK_SPH     equ ARK_SPTOP + ARK_PUH ; rows in one sprite
ARK_SPN     equ PU_KINDS + 1        ; ...and one sprite per kind: kinds 1..
                                    ; PU_KINDS, so this is the loop's BOUND -
                                    ; PU_NONE has no sprite and the band starts
                                    ; at kind 1 (ARK_SPBASE, by the bss)

ARK_PUW     equ 12                  ; capsule size. The height must CONTAIN the
ARK_PUH     equ 10                  ; 8px glyph drawn one row in, or the letter
                                    ; hangs a row below the rect that erases
                                    ; it and every frame leaves a slice of the
                                    ; last one behind
ARK_PUFALL  equ 2                   ; ...and how fast it falls
ARK_LAGMAX  equ 4                   ; how far behind its own deadline the
                                    ; worker will chase before giving up and
                                    ; re-anchoring (ark_worker). Small, because
                                    ; the point is to absorb ONE slow frame,
                                    ; not to run a backlog of them

; game modes ([ark_mode])
M_READY     equ 0                   ; ball parked on the paddle, Space to go
M_PLAY      equ 1
M_DEAD      equ 2                   ; life lost, pausing before the next
M_OVER      equ 3
M_PAUSE     equ 4
M_CLEAR     equ 5                   ; wall cleared, pausing before the next

ARK_BG      equ CBLACK
ARK_RAILCOL equ CLGRAY              ; the side rails: dithered on 1bpp
ARK_PADCOL  equ CWHITE
ARK_BALLCOL equ CWHITE

; -----------------------------------------------------------------------------
; ark_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; Picks the metrics off the live screen, sizes the window to them, builds a
; wall and returns. Nothing is drawn and no task is spawned: the loader has
; not published our instance yet, so OSAPI_TASK_SPAWN would simply refuse
; (SPEC.md 20.6) - the first W_PAINT hires the worker instead.
; -----------------------------------------------------------------------------
ark_entry:
    push si
    push di
    mov al, KSC_LEFT                ; ARM the key-state map, here and not at
    call OSAPI_KEY_DOWN             ; the first steer: asking is what starts
                                    ; the kernel tracking scancodes (SPEC.md
                                    ; 9.7), and a map armed by the first arrow
                                    ; has already MISSED that arrow's make
                                    ; code - so the paddle read the key as up
                                    ; until its first typematic repeat and
                                    ; stopped for the whole delay, which is
                                    ; the exact defect this was meant to fix.
                                    ; The answer is meaningless and discarded
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock top row, DH=bpp
    mov [ark_scrw], ax
    mov [ark_dock], cx
    mov [ark_bpp], dh

    call ark_metrics            ; the record this screen wants, and everything
                                ; it implies (SPEC.md 11.98 re-runs this)

    mov ax, [ark_bw]                ; content width = the wall plus both rails
    mov bl, ARK_COLS
    mul bl
    add ax, [ark_rail]
    add ax, [ark_rail]
    mov [ark_cwid], ax
    add ax, 2
    mov [ark_tpl + WT_W], ax

    mov ax, [ark_chwant]            ; the height the layout wants...
    add ax, TITLE_H + 1
    mov bx, [ark_dock]              ; ...clamped to the desktop band, and one
    sub bx, MBAR_H + 1              ; pixel short of it, exactly as wm_fit
                                    ; does (SPEC.md 11): the drop shadow is
                                    ; on row y+h, so a frame that merely
                                    ; REACHES the dock already covers it.
                                    ; wm_fit would shave this pixel anyway -
                                    ; taking it here keeps ark_chgt, and so
                                    ; the whole layout, in step with the
                                    ; window the kernel actually made
    cmp ax, bx
    jbe .hok
    mov ax, bx
.hok:
    mov [ark_tpl + WT_H], ax
    sub ax, TITLE_H + 1
    mov [ark_chgt], ax
    call ark_layout

    mov ax, [ark_scrw]
    sub ax, [ark_tpl + WT_W]
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [ark_tpl + WT_X], ax
    mov ax, [ark_dock]
    sub ax, MBAR_H
    sub ax, [ark_tpl + WT_H]
    jns .yok
    xor ax, ax
.yok:
    mov bl, 3                       ; a third of the slack above (a BYTE divide:
    div bl                          ; the band is under 768 rows)
    mov ah, 0
    add ax, MBAR_H
    mov [ark_tpl + WT_Y], ax

    call OSAPI_GET_TICKS            ; seeded once; every later wall and every
    call OSAPI_SRAND                ; capsule walks on down the same stream

    call ark_newgame

    mov si, ark_tpl
    call OSAPI_WM_CREATE
    jc .full
    mov [ark_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE
    mov ax, ark_onresize            ; the metric record is picked from the
    call OSAPI_WM_ONRESIZE          ; SCREEN, so an adapter change invalidates
                                    ; it and nothing else would say so
                                    ; (SPEC.md 11.98)
    mov si, ark_pref                ; **AND THE WINDOW FOLLOWS THE METRICS**
    call OSAPI_WM_PREFER            ; (SPEC.md 11.100.1). ark_metrics has always
                                    ; had two records and picked the right one;
                                    ; what never followed was the FRAME, so a
                                    ; game dragged onto a CGA re-derived a
                                    ; fatter, squatter wall and then drew it
                                    ; into a window still 303 rows tall - this
                                    ; window is not WF_SIZABLE, so until it
                                    ; published these three sizes the kernel
                                    ; had none for it that anyone had designed
    mov cx, ARK_PREF_SW             ; ...and the floor is the small record's
    mov dx, ARK_MIN_SH              ; own size, because a straddle still CLAMPS
    call OSAPI_WM_MINSIZE           ; a published size to the rows both cards
                                    ; have (SPEC.md 39.16.3) and a wall with
                                    ; the rows squeezed out of it is a
                                    ; different game
    mov si, ark_menus
    call OSAPI_MENU_SET
    mov si, ark_about
    call OSAPI_ABOUT_SET            ; 'About Arkanoid' under our name in the
                                    ; bar (SPEC.md 12.2); preserves the flags
.full:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; ark_track - adopt the content origin and size (top of every callback and of
;             every rendered frame)
; in:  SI = window ptr
; out: [ark_ox]/[ark_oy], [ark_cwid]/[ark_chgt] and the derived rows;
;      ES = KERNEL_SEG
;
; The window moves and wm_fit may have clamped what we asked for, so the
; layout is re-derived from the record rather than from ark_entry's
; arithmetic. ES is loaded rather than relied on: it is ours to clobber.
; -----------------------------------------------------------------------------
ark_track:
    push ax
    push bx
    push dx
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [ark_ox], ax
    mov [ark_oy], dx
    mov ax, [es:bx + W_W]
    sub ax, 2
    mov [ark_cwid], ax
    mov ax, [es:bx + W_H]
    sub ax, TITLE_H + 1
    mov [ark_chgt], ax
    call ark_layout
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_scale_vel - apply [ark_vscale] to the whole velocity family
; in:  [ark_vscale] (per cent), just copied out of the metrics record
; out: the ten ark_v*/ark_thr*/ark_pufall words; preserves all registers
;
; Called once, from ark_entry, before anything reads a speed. The clamps at the
; bottom are not belt-and-braces: rounding is per value, so a scale that lands
; ark_thrhold a quarter above ark_vxmax would let a serve ask for an angle the
; very next bounce clips - visible as a serve that bends the instant it leaves
; the paddle. Enforcing the relationships AFTER rounding is what lets the
; percentage be any number at all rather than one that has been checked by hand.
; -----------------------------------------------------------------------------
ark_scale_vel:
    push ax
    push si
    push di
    mov si, ark_vel_src             ; the BIG-metric values, in order...
    mov di, ark_vxmax               ; ...into the bss words that shadow them -
.each:                              ; and the five paddle zones straight after
    lodsw                           ; them, into ark_zbq: a zone's contribution
    call ark_scale                  ; is a vx like ark_english's, so it scales
    mov [di], ax                    ; for the same reason - unscaled, an edge
    inc di                          ; hit adds 8 quarters against CGA's ceiling
    inc di                          ; of 9 and saturates the angle, where on
    cmp di, ark_zbq + (ARK_NZONE - 1) * 2   ; VGA the same 8 is a third of 24
    jbe .each

    mov ax, [ark_vxmax]             ; a serve may ASK for the ceiling and no
    cmp [ark_thrhold], ax           ; more; rounding is what can part them
    jbe .tap
    mov [ark_thrhold], ax
.tap:
    mov ax, [ark_thrhold]           ; ...and a flick never throws harder than
    cmp [ark_thrtap], ax            ; a hold
    jbe .floor
    mov [ark_thrtap], ax
.floor:
    mov ax, [ark_vybase]            ; Slow's floor is a floor, not a target
    cmp [ark_vyfloor], ax
    jbe .top
    mov [ark_vyfloor], ax
.top:
    mov ax, [ark_vybase]            ; ...and the ceiling is above wall 1
    cmp [ark_vytop], ax
    jae .zb
    mov [ark_vytop], ax
.zb:
    pop di
    pop si
    pop ax
    ret

; --- the BIG-metric velocity family, in ark_vxmax..ark_pufall order -----------
; This table and those ten bss words are one thing in two halves: the copy
; loop above walks them in lockstep, so an entry added here needs its AWORD
; added there, in the same place.
ark_vel_src:
    dw ARK_VXMAX, ARK_VXMIN, ARK_VYBASE, ARK_VYSTEP, ARK_VYMAX
    dw ARK_VYFLOOR, ARK_VYSLOW, ARK_THRTAP, ARK_THRHOLD, ARK_PUFALL

; What each fifth of the paddle ADDS to the ball's existing vx. Not a velocity
; to replace it with: replacing is what made the bounce feel arbitrary, because
; a ball arriving steeply from the left and one drifting in from the right left
; the paddle identically if they landed in the same zone.
; The five paddle zones' vx contribution, BIG-metric. ark_scale_vel copies it
; into ark_zbq scaled for the adapter, and the bounce reads that copy - this
; table is never read at play time. It must FOLLOW ark_vel_src, as ark_zbq
; follows ark_pufall: the one loop walks both pairs as fifteen words.
ark_zbias:   dw -2*ARK_VQ, -1*ARK_VQ, 0, 1*ARK_VQ, 2*ARK_VQ

; -----------------------------------------------------------------------------
; ark_scale - one tuning figure, scaled by [ark_vscale] per cent
; in:  AX = the BIG-metric value, signed; out: AX = scaled, rounded to nearest,
;      its sign kept, and never rounded away to zero unless it was zero
; preserves all other registers
;
; The min-of-one matters more than the rounding: ark_pufall scales to 0.74 on
; CGA, and a capsule falling zero pixels a frame hangs in the air forever. The
; magnitude goes through the multiply because mul is unsigned; the sign goes
; round it on the stack.
; -----------------------------------------------------------------------------
ark_scale:
    push bx
    push dx
    cwd                             ; DX = 0 or -1: the sign...
    push dx
    xor ax, dx                      ; ...and AX = |v|
    sub ax, dx
    jz .sign                        ; zero scales to zero, not to one
    mul word [ark_vscale]           ; DX:AX = v * per-cent; both are small
    add ax, 50                      ; round to nearest rather than truncate
    adc dx, 0
    mov bx, 100
    div bx
    or ax, ax
    jnz .sign
    inc ax                          ; ...but never all the way to nothing
.sign:
    pop dx                          ; the sign back on
    xor ax, dx
    sub ax, dx
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ark_layout - the rows every other routine measures from
; in:  [ark_chgt]; out: [ark_bricky], [ark_pady], [ark_floor]
; preserves all registers
; -----------------------------------------------------------------------------
; ark_metrics - the metric record this screen wants, and what it implies
; in:  BX = the screen HEIGHT
; out: ark_bw.. filled, the velocities scaled, ark_pwmax derived
; clobbers: AX, CX, SI, DI
; -----------------------------------------------------------------------------
ark_metrics:
    mov si, ark_met_big
    cmp bx, 300
    jae .metric
    mov si, ark_met_sml
.metric:
    mov di, ark_bw                  ; copied wholesale: the bss words below
    mov cx, ARK_NMET                ; must stay in the record's order
.mcopy:
    lodsw
    mov [di], ax
    inc di
    inc di
    loop .mcopy

    call ark_scale_vel              ; ...and the speeds that record implies

    mov ax, [ark_pw0]               ; the widest an Expand can make it
    add ax, 24
    mov [ark_pwmax], ax
    ret

; -----------------------------------------------------------------------------
; ark_onresize - the adapter changed under us (SPEC.md 11.98)
;
; in:  SI = our window, CX/DX = the new content size; the gfx lock is HELD and
;      this may not draw
; out: nothing (all registers preserved)
;
; ark_track already reads the live box every frame, so the field's EXTENT
; follows a resize on its own. What does not is the metric RECORD: brick and
; paddle sizes, the ball, the rail width and the velocity scale are picked once
; from the screen height at launch, and a 480-row VGA taken to its 200-row CGA
; leaves a game laid out in 24x10 bricks inside 137 rows of content.
;
; THE LEVEL RESTARTS, and that is the honest answer rather than a shortcut. The
; wall's row COUNT is one of the metric words (6 against 5), so the alive map
; changes shape - a ball in flight is inside a wall that no longer exists at
; those coordinates, and there is no rescaling that makes a half-broken 6-row
; wall into a half-broken 5-row one. Score, lives and level survive; the ball
; goes back on the paddle, which is a state the game already has and the player
; already understands (M_READY).
;
; It does NOT resize the window. The kernel has already decided the box - that
; is why this is being called - and ark_track lays the field into whatever it
; is, which is what every other app in the tree does with its content.
; -----------------------------------------------------------------------------
ark_onresize:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si                      ; **THE CARD THIS WINDOW IS ON** (SPEC.md
    call OSAPI_WM_DISPLAY           ; 39.16.4), not the primary: this handler
                                    ; fires on a DRAG across the seam as well
                                    ; as on Activate Mode, and OSAPI_VIDEO
                                    ; answers about the primary either way.
                                    ; AX=w, BX=h, CX=dock row, SI=band top,
                                    ; DH=bpp - and SI stops being the window
                                    ; here, which is why BX was taken first
    mov [ark_scrw], ax              ; the bpp is banked again too, because a
    mov [ark_bpp], dh               ; switch is exactly when it stops being true
    sub cx, si                      ; the BAND - not CX - MBAR_H on a secondary,
    add cx, MBAR_H                  ; which has no menu bar and no dock - put
    mov [ark_dock], cx              ; back into the primary-shaped number every
                                    ; reader of [ark_dock] already subtracts
                                    ; MBAR_H from
    call ark_metrics                ; ...which reads BX, now THIS card's height:
                                    ; the one number the big/small choice turns
                                    ; on
    cmp byte [ark_mode], M_OVER     ; a finished game has nothing to restart,
    je .out                         ; and putting a ball back on the paddle
                                    ; there would look like a free life
    call ark_startlevel
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
ark_layout:
    push ax
    mov ax, [ark_status]            ; the wall starts under the status strip
    add ax, [ark_gap]
    mov [ark_bricky], ax
    mov ax, [ark_chgt]              ; the ball dies below the last row...
    dec ax
    mov [ark_floor], ax
    inc ax                          ; ...and the paddle rides just off the
    sub ax, [ark_pado]              ; bottom
    mov [ark_pady], ax
    pop ax
    ret

; =============================================================================
; The UI task's half: paint, keys, menu
; =============================================================================

; -----------------------------------------------------------------------------
; ark_paint - W_PAINT: full content repaint, and where the worker is hired
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; The three "owed" flags are cleared here, not just in ark_render's own full
; branch: ark_draw_all satisfies all three, and W_PAINT is the OTHER caller
; of it. Left set, the worker's very next frame drew the whole board a second
; time - most visibly on the launch, where ark_newgame raises [ark_full] and
; the first paint therefore rendered twice in a row.
; -----------------------------------------------------------------------------
ark_paint:                          ; (no registers banked here: every
    call ark_track                  ; routine it calls keeps them all)
    call ark_draw_full
    jmp ark_hire                    ; idempotent: only the first paint spawns

; -----------------------------------------------------------------------------
; ark_hire - spawn the worker, once
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
;
; A refusal is a normal outcome - the 12-slot task table can be full - and it
; is transient, so nothing is latched on failure and the next paint tries
; again. What must NOT happen is a second spawn.
; -----------------------------------------------------------------------------
ark_hire:
    push ax
    push bx
    cmp byte [ark_hired], 0
    jne .out
    mov ax, ark_worker
    mov bx, [ark_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [ark_hired], 1
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2): the
    ; kernel wrote our segment into this worker's frame before its
    ; first instruction, so mem_frameless pins a region with an
    ; undeclared worker however that region is declared. What a restart
    ; costs is one pass of the loop - the park is inside
    ; OSAPI_TASK_ALIVE and nowhere else (this package is not
    ; OSAPI_MEM_PARKSAFE), which is the TOP of the loop, and every byte
    ; that outlives a pass is a static and moves with us.
    OS88_WORKER_RESTARTABLE ark_worker
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_onkey - W_ONKEY: steer, launch, fire, pause, restart
; in:  AL = ascii, AH = scan, SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; This runs on the UI task and the worker runs the game, so nothing here does
; anything but set a word the worker reads - except N, which rebuilds the whole
; content; P, which draws the banner's band; and Space, which is one or the
; other depending on the mode: a resume when the game is paused (and then it
; draws that same band, to take the banner down), a word for the worker
; otherwise.
;
; The `or bl, bl` gate is not optional: the numeric keypad sends '4' and '6'
; with the arrow scan codes, so without it typing a digit would steer the
; paddle (the same trap apps/notepad documents).
; -----------------------------------------------------------------------------
ark_onkey:
    push ax                         ; the two this touches: every routine it
    push bx                         ; calls keeps all the others
    mov bx, ax                      ; keep the key; ark_track needs AX
    call ark_track
    call ark_abdismiss              ; any key takes the credits down, and is
    jc .out                         ; spent doing it
    or bl, bl
    jnz .ascii

    mov al, -1
    cmp bh, 0x4B                    ; left arrow
    je .steer
    mov al, 1
    cmp bh, 0x4D                    ; right arrow
    jne .out
.steer:
    cmp al, [ark_pdir]              ; the other arrow is always a fresh tap
    jne .fresh
    cmp word [ark_pspd], 0          ; same arrow and the paddle is ALREADY
    jne .out                        ; moving that way: this is a typematic
                                    ; repeat, and ark_do_paddle is following
                                    ; the KEY rather than these events, so
                                    ; there is nothing to do. Restarting the
                                    ; tap here is what used to drop a hold back
                                    ; to the slow speed on every repeat
.fresh:
    mov [ark_pdir], al
    mov word [ark_pspd], ARK_PSTEP
    mov word [ark_pkeep], ARK_PTAP  ; tick of a nudge must always move
    xor ax, ax
    mov [ark_pacc], ax              ; a new press owes no sub-pixel: the first
    mov [ark_pdn], ax               ; ...and it has been held for no ticks yet
    jmp .out

.ascii:
    or bl, 0x20                     ; fold the case: 'P' is 'p', 'N' is 'n',
    cmp bl, ' '                     ; and only a NUL - handled above - or a
    je .space                       ; space itself reads as a space after it
    cmp bl, 'p'
    je .pause
    cmp bl, 'n'
    je .new
    jmp short .out
.space:
    cmp byte [ark_mode], M_PAUSE    ; paused? then Space is the RESUME, and
    je .pause                       ; nothing else - see ark_cmd_pause, whose
    mov byte [ark_launch], 1        ; M_PAUSE half that is. Otherwise the
    jmp short .out                  ; worker decides what it means: serve, or
                                    ; fire the laser
.pause:
    call ark_cmd_pause
    jmp .out
.new:
    call ark_cmd_new
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_oncmd - AM_ONCMD: a Game menu item (SPEC.md 12.2)
; in:  AL = item, AH = menu, SI = our window, BX = the set; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX like any window callback
; -----------------------------------------------------------------------------
ark_oncmd:
    mov bl, al                      ; (AM_ONCMD may clobber, SPEC.md 12.2)
    call ark_track
    call ark_abdismiss              ; a menu pick takes the credits down first,
    jc .out                         ; and is spent doing it
    or bl, bl                       ; the two commands are tail calls
    jz ark_cmd_new
    cmp bl, 1
    je ark_cmd_pause
.out:
    ret

; -----------------------------------------------------------------------------
; ark_onclick - W_ONCLICK: nothing in this game steers with the mouse, so a
;               click exists only to take the credit panel down. A panel you
;               dismiss with a key but not with a click reads as a hung
;               window, which is the whole reason this callback exists.
; in:  CX = x, DX = y (screen), SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ark_onclick:
    call ark_track                  ; both keep every register, so nothing is
    jmp ark_abdismiss               ; banked here and the tail is a jump

; -----------------------------------------------------------------------------
; ark_cmd_new / ark_cmd_pause - the two commands, shared by keys and menu
; in:  the origin tracked; gfx lock held; preserve all registers
;
; ark_cmd_pause TOGGLES, and it is the single place the mode is banked and put
; back - the menu item, P, and the sticky focus pause (ark_focuschk) all end
; here, so none of them can leave [ark_mode] and [ark_wasmode] disagreeing.
; It costs a BAND, not a board: ark_draw_msgband, and the reasoning is there.
; ark_cmd_new is the other way round - a new wall, a new score and a new life
; count are the whole content, so it is one of the few honest ark_draw_alls.
;
; Space is deliberately NOT a caller of the halt half. It reaches the resume
; half only (ark_onkey), because Space already means serve and fire: a Space
; that also paused would stop the game every time a player fired a laser one
; frame after the ball came off the paddle. So the game is paused by P, by the
; menu, or by walking away, and Space is the one key that can only ever start
; it moving again - which is also what a player reaching for the keyboard in
; front of a PAUSED banner will press first.
; -----------------------------------------------------------------------------
ark_cmd_new:
    call ark_newgame
    jmp ark_draw_all

ark_cmd_pause:
    push ax
    mov al, [ark_mode]
    cmp al, M_PAUSE
    je .resume
    cmp al, M_PLAY
    je .halt
    cmp al, M_READY
    jne .out
.halt:
    mov [ark_wasmode], al
    mov al, M_PAUSE
    jmp short .set
.resume:
    call ark_refocus                ; BEFORE the mode goes live, or the very
    mov al, [ark_wasmode]           ; next worker frame pauses it again
.set:
    mov [ark_mode], al
    call ark_draw_msgband           ; the banner's band, not the content
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_refocus - become the ACTIVE APPLICATION again, as part of a resume
; in:  gfx lock held (every caller is a window callback); preserves all
;      registers
;
; **A key can reach a window that is not the active application**, and that is
; the whole reason this exists. The kernel routes a keystroke to OSAPI_WM_TOP's
; window (SPEC.md 13) and hands the menu bar to [menu_win] (SPEC.md 12), and a
; click on the bare desktop moves the second without moving the first - so
; after one, Space still arrives here while Locator owns the bar. Resuming on
; it then set M_PLAY and ark_focuschk put it straight back to M_PAUSE on the
; next frame, 55ms later, with nothing on screen to show for it: a game that
; could not be resumed from the keyboard at all. The pause is meant to be
; sticky, not permanent.
;
; So a resume takes the bar back. OSAPI_WM_FRONT is exactly that call - it
; activates before it raises - and the game is then running and named in the
; bar, which are the same fact and should never have been two.
;
; **The wm_top test is not redundant.** On the window that is already frontmost
; wm_front takes its chrome-only path: menu_activate, the bar and the dock, no
; window repaint. On any other it repaints - which from inside a callback means
; re-entering this package's own dispatcher through W_PAINT while we are still
; in it. So this refuses rather than raising, and ark_focuschk's next frame
; correctly leaves a game that is not frontmost paused.
; -----------------------------------------------------------------------------
ark_refocus:
    push ax
    push bx
    call OSAPI_WM_TOP               ; BX = frontmost visible, 0 = none
    cmp bx, [ark_win]
    jne .out
    call OSAPI_WM_FRONT             ; BX is already our window - the cmp above
.out:                               ; is what proved it
    pop bx
    pop ax
    ret

; =============================================================================
; The worker: the game itself (SPEC.md 20.6)
; =============================================================================

; =============================================================================
; 'About Arkanoid' - the credits (SPEC.md 12.2)
; =============================================================================
; The panel is drawn INSIDE our own content, not in a window of its own: a
; package has no way to put up a second window it does not own, and the game
; field is exactly the rectangle a notice wants anyway.
;
; Two rules make it safe against the worker, which is drawing the same content
; eighteen times a second. [ark_abon] is checked by ark_render UNDER THE LOCK,
; right after the clip is armed, and the frame is dropped whole while it is
; set - so the worker never paints over the panel and the UI task owns the
; content until the panel comes down. And the game is PAUSED while it is up:
; a skipped frame does not stop the ball (that is the point of SPEC.md 44.1),
; so without the pause a player would read the credits and lose a life doing
; it.

; -----------------------------------------------------------------------------
; ark_about - the OSAPI_ABOUT_SET handler: a window callback in every respect
; in:  SI = our window ptr; caller holds the gfx lock
; -----------------------------------------------------------------------------
ark_about:
    push ax                         ; the one register this touches
    mov byte [ark_abon], 1
    mov al, [ark_mode]              ; a LIVE ball is frozen underneath, because
    cmp al, M_PLAY                  ; a dropped frame does not stop it and a
    jne .draw                       ; player would lose a life reading this.
    mov [ark_wasmode], al           ; Every other mode is already still, so it
    mov byte [ark_mode], M_PAUSE    ; is left alone and the banner it was
.draw:                              ; showing comes back untouched
    call ark_track
    call ark_clipdraw               ; ...which draws the panel last, because
    pop ax                          ; [ark_abon] is set
    ret

; -----------------------------------------------------------------------------
; ark_clipdraw - ark_draw_full under our own clip region
; in:  the origin tracked; gfx lock held. Preserves every register
;
; The two UI-task paths that draw outside W_PAINT - the credits going up and
; coming down - drew with NO clip armed, so a window over ours took the board
; across its content (SPEC.md 11.3, the rule ark_render already keeps). Not one
; pixel visible draws nothing and leaves the whole frame owed.
; -----------------------------------------------------------------------------
ark_clipdraw:
    push bx
    mov bx, [ark_win]
    call OSAPI_WM_CLIP_SET
    jc .hid
    call ark_draw_full
    call OSAPI_WM_CLIP_CLEAR        ; the hold is the caller's: whatever it
    pop bx                          ; draws after us is not ours to clip
    ret
.hid:
    mov byte [ark_full], 1
    pop bx
    ret

; -----------------------------------------------------------------------------
; ark_abdismiss - take the panel down if it is up
; in:  the origin tracked; gfx lock held
; out: CF = 1 the click/key was spent doing it, CF = 0 there was no panel;
;      preserves every register
; -----------------------------------------------------------------------------
ark_abdismiss:
    cmp byte [ark_abon], 0
    je .none
    mov byte [ark_abon], 0
    call ark_clipdraw               ; the game stays paused: the key that took
                                    ; the credits down is not also a resume.
                                    ; This IS the whole repaint every skipped
                                    ; frame under the panel asked for, so it
    stc                             ; settles the debt rather than leaving the
    ret                             ; worker to draw the board a second time
.none:
    clc
    ret

; =============================================================================
; The worker: the game itself (SPEC.md 20.6)
; =============================================================================

; -----------------------------------------------------------------------------
; ark_worker - THE background task
; in:  DX = our instance index, DS = ES = CS = our segment, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own: the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; One frame a tick. The sleep is what sets the frame rate AND what keeps the
; machine usable: a worker that spun would starve the UI task it shares the
; scheduler with. Everything is lock-free except ark_render, which takes the
; lock for one short burst of drawing (rule 3).

; -----------------------------------------------------------------------------
; ark_worker - THE background task
; in:  DX = our instance index, DS = ES = CS = our segment, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own: the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; One frame a tick. The sleep is what sets the frame rate AND what keeps the
; machine usable: a worker that spun would starve the UI task it shares the
; scheduler with. Everything is lock-free except ark_render, which takes the
; lock for one short burst of drawing (rule 3).
; -----------------------------------------------------------------------------
ark_worker:
    call OSAPI_GET_TICKS
    mov [ark_due], ax               ; the first frame is due now
.loop:
    mov bx, [ark_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4)

    inc word [ark_due]              ; ...and the next one a tick after this
    call OSAPI_GET_TICKS            ; AX = now
    mov bx, [ark_due]
    sub bx, ax                      ; BX = ticks still to wait, SIGNED, and
    jle .behind                     ; wrap-safe by subtraction (SPEC.md 8)
    mov ax, bx
    call OSAPI_TASK_SLEEP
    jmp short .frame
.behind:
    cmp bx, -ARK_LAGMAX             ; a little late: run now and keep the
    jg .frame                       ; deadline, so the next short frame catches
    mov [ark_due], ax               ; back up. Hopelessly late - a long stall,
                                    ; or a machine that cannot hold 18fps at
                                    ; all - and the deadline is re-anchored to
                                    ; now, or it runs away and this loop never
                                    ; sleeps again
.frame:
    call ark_update
    call ark_render
    jmp .loop

; -----------------------------------------------------------------------------
; ark_update - advance one frame. NO lock held, nothing drawn.
; clobbers AX..DI: its one caller, ark_worker, holds nothing across it
; -----------------------------------------------------------------------------
ark_update:                         ; (ark_worker reloads everything it uses,
    call ark_do_paddle              ; so nothing is banked here)              ; the paddle moves even while parked
    call ark_focuschk               ; ...and a rally stops if we lost focus
    mov al, [ark_mode]
    cmp al, M_PAUSE
    je .out
    cmp al, M_OVER
    je .out
    cmp al, M_DEAD
    je .wait
    cmp al, M_CLEAR
    je .wait
    call ark_do_launch
    call ark_do_ball
    call ark_do_pu
    call ark_do_shots
    jmp .out
.wait:
    dec word [ark_hold]             ; the pause after a death or a clear
    jg .out
    cmp byte [ark_mode], M_CLEAR
    je .next
    call ark_respawn
    jmp .out
.next:
    call ark_nextwall
.out:
    ret

; -----------------------------------------------------------------------------
; ark_focuschk - pause the rally if the player has gone somewhere else
; preserves all registers
;
; A ball keeps moving while the game is covered - that is deliberate (SPEC.md
; 44.1: a dropped FRAME must not stop the game), and it is exactly wrong when
; the player has gone to another window. They come back to a lost life they
; never saw.
;
; **Two questions, because neither one alone is "did the player walk away".**
; OSAPI_WM_TOP answers who is frontmost, which catches a window raised over
; us. OSAPI_MENU_OWNER answers who the ACTIVE APPLICATION is, which catches
; the click on the bare desktop - that hands the menu bar to Locator (SPEC.md
; 12) and moves nothing in the z-order, so the frontmost visible window is
; still this one and WM_TOP alone reads it as nothing having happened. The
; player is looking at the Locator's menus with a live ball on screen. So the
; rally continues only while BOTH say us.
;
; **The pause is sticky.** Coming back to the front does not resume, because a
; ball that starts moving the instant a window is raised is a ball nobody was
; watching yet - the same reason a new life waits on Space. It resumes the way
; every other pause does, through ark_cmd_pause, and it uses ark_cmd_pause's
; own [ark_wasmode] so the two cannot leave the mode in different places.
;
; Only M_PLAY is interrupted. Every other mode is already still, and M_READY
; has the ball parked on the paddle where losing focus costs nothing.
;
; Runs on the WORKER, holding no lock. That is what both of those slots are
; for: neither takes a lock or touches VRAM - one reads wm_zord, the other one
; word - so a background task may ask every frame. Drawing the banner is not
; done here: [ark_full] makes the next ark_render frame repaint under the gfx
; lock, where drawing belongs.
; -----------------------------------------------------------------------------
ark_focuschk:
    push bx
    cmp byte [ark_mode], M_PLAY
    jne .out
    call OSAPI_WM_TOP               ; BX = frontmost visible, 0 = none
    cmp bx, [ark_win]
    jne .lost
    call OSAPI_MENU_OWNER           ; BX = the active application, 0 = Locator
    cmp bx, [ark_win]
    je .out
.lost:
    mov byte [ark_wasmode], M_PLAY
    mov byte [ark_mode], M_PAUSE
    mov byte [ark_full], 1          ; the banner, on the next frame that draws
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; ark_do_paddle - move the paddle at whichever of the three speeds is set
; in:  [ark_pdir]/[ark_pkeep]/[ark_pspd]/[ark_pacc]/[ark_pdn]; out: [ark_px]
;      moved, clamped; preserves all registers
;
; [ark_pkeep] is written by the UI task and decremented here. Both are plain
; word accesses, and the 8086 recognises interrupts only at instruction
; boundaries, so neither can be torn by the other - the same no-protocol
; sharing apps/fractal uses for its restart flag. [ark_pspd], [ark_pacc] and
; [ark_pdn] are written by both, and the same argument covers them: the UI
; task only ever stores a whole word, never reads one back to modify it.
;
; THE KEY ITSELF SAYS WHETHER IT IS HELD (SPEC.md 9.7), and that is what this
; routine is built around now. It used to infer a hold from the INTERVAL
; between int 16h events, because int 16h has no key-up in it - and that
; inference has two failures no arrangement of its constants can fix. It
; cannot see the typematic DELAY, so for the ~9 ticks between a press and its
; first repeat a held key produces no evidence at all and the paddle stopped
; dead in the middle of a hold - go, stop, go, which is what the field
; reported. And the keyboard repeats the LAST key pressed, so serving with
; Space ended the arrow's repeats while the finger was still on it and the
; paddle would not move again until the arrow was released and pressed afresh.
;
; So each tick asks OSAPI_KEY_DOWN. Down: refill the countdown (only when it
; has fallen below ARK_PHOLD, so a tap's first three ticks are untouched) and
; age [ark_pdn], the ticks this press has been held for. Up: the countdown
; runs out exactly as it always did. The SPEED is then one comparison -
; ARK_PSTEP until [ark_pdn] reaches ARK_PTAP, ARK_PFAST after - so the first
; 35 pixels of a hold are pixel-for-pixel a tap, which they have to be,
; because for those ticks the player has not yet done anything different.
; There is no stop anywhere in it.
;
; [ark_pspd] still holds one of exactly three values and is still the state
; machine; what changed is who sets it. Every ramp, taper and coast stays gone,
; and a tap still stops mid-stride, because the paddle is aimed by tapping.
;
; A LOST BREAK CODE is the failure this borrows from 9.7, and it degrades the
; way that section says: the key reads down until it is next pressed, so the
; paddle runs to a rail and clamps there. It also degrades if key state never
; works at all - [ark_pdn] then never reaches ARK_PTAP, and each repeat finds
; [ark_pspd] at 0 and starts a fresh tap, so a held key still moves, just
; always at ARK_PSTEP. Neither needs a line of code here.
;
; [ark_pacc] carries the quarter pixels a fractional speed owes between frames,
; which is what lets ARK_PSTEP be 2.0 or 1.75 rather than only a whole 1 or 2
; (SPEC.md 44.3.2's argument, applied to the paddle).
; -----------------------------------------------------------------------------
ark_do_paddle:
    push ax
    push bx
    push dx
    mov word [ark_pvel], 0          ; how far the paddle moves THIS frame,
                                    ; signed - the ball reads it on contact
    call ark_pkey                   ; is the arrow the paddle is following
    jnc .aged                       ; still down? then it is a HOLD, whatever
                                    ; int 16h has or has not delivered
    cmp word [ark_pkeep], ARK_PHOLD ; refill only from BELOW: a tap that is
    jge .held                       ; still inside its own countdown owns it
    mov word [ark_pkeep], ARK_PHOLD
.held:
    cmp word [ark_pdn], ARK_PTAP    ; ticks held, stopped at the only value
    jge .aged                       ; ever asked about so it cannot wrap
    inc word [ark_pdn]
.aged:
    cmp word [ark_pkeep], 0
    jg .live
    xor ax, ax
    mov [ark_pspd], ax              ; stopped, the third speed - and what tells
    mov [ark_pdn], ax               ; ark_onkey that this arrow is free again
    jmp .out
.live:
    dec word [ark_pkeep]
    mov ax, ARK_PSTEP               ; the promotion, and the whole of it: the
    cmp word [ark_pdn], ARK_PTAP    ; first ARK_PTAP ticks of a hold are a tap
    jl .spd
    mov ax, ARK_PFAST
.spd:
    mov [ark_pspd], ax
    add ax, [ark_pacc]              ; the quarter pixels owed, plus this tick's
                                    ; worth...
    xor dx, dx
    mov bx, ARK_VQ
    div bx                          ; ...is AX whole pixels and DX still owed
    mov [ark_pacc], dx
    or ax, ax
    jz .out                         ; less than a pixel this tick: nothing
    mov bx, ax                      ; moved, and [ark_pvel] correctly says so
    mov al, [ark_pdir]
    cbw
    imul bx                         ; DX:AX = dir * pixels
    add ax, [ark_px]
    mov bx, [ark_rail]              ; the rails are solid
    cmp ax, bx
    jge .hi
    mov ax, bx
.hi:
    mov bx, [ark_cwid]
    sub bx, [ark_rail]
    sub bx, [ark_pw]
    cmp ax, bx
    jle .set
    mov ax, bx
.set:
    mov bx, ax                      ; what it ACTUALLY moved, after the clamps:
    sub bx, [ark_px]                ; a paddle pinned against a rail is not
    mov [ark_pvel], bx              ; moving, and must impart nothing
    or bx, bx
    jz .out
    mov [ark_px], ax
    mov byte [ark_padwipe], 1
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_park - sit the ball on the middle of the bat
; out: [ark_bx]/[ark_by] set; preserves all registers
;
; One copy, because ark_nextwall needs it for a reason that is not "the ball is
; parked this frame": a ball still standing among the bricks when the next wall
; is BUILT would be drawn on it and then erased to background from there
; (SPEC.md 44.10.2).
; -----------------------------------------------------------------------------
ark_park:
    push ax
    push bx
    mov ax, [ark_px]
    mov bx, [ark_pw]
    shr bx, 1
    add ax, bx
    mov bx, [ark_bsz]
    shr bx, 1
    sub ax, bx
    mov [ark_bx], ax
    mov ax, [ark_pady]
    sub ax, [ark_bsz]
    mov [ark_by], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_pkey - is the arrow the paddle is following still DOWN? (SPEC.md 9.7)
; out: CF = 1 held, CF = 0 not; preserves every register
;
; [ark_pdir] is the direction, so the scancode falls out of its sign. A paddle
; with no direction latched at all answers "not held" without asking, which is
; also the one case where the answer would be meaningless.
;
; The slot takes no lock and touches no port, so this is legal from the worker
; task - which is where the game loop is (SPEC.md 44.1), and the whole reason
; the answer is a cheap read rather than an event.
; -----------------------------------------------------------------------------
ark_pkey:
    push ax
    cmp byte [ark_pdir], 0
    je .no
    mov al, KSC_RIGHT
    jg .ask
    mov al, KSC_LEFT
.ask:
    call OSAPI_KEY_DOWN             ; CF is the answer, and the pop below
    pop ax                          ; leaves it alone
    ret
.no:
    clc
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_english - the sideways kick the paddle's own motion imparts
; out: AX = -2..+2 PIXELS, in quarter units; preserves all other registers
;
; [ark_pvel] is PIXELS, so a held paddle at 8 gives a clean -2..+2 without a
; table, and a tapping one at 5 gives 1. idiv truncates toward zero, which is
; what makes a rail-clamped part-move round down to nothing - and what put a
; tap at 0 while ARK_PSTEP was 2, so that a rally could be steered by holding
; and not by tapping. At 5 that distinction has gone away on its own, which is
; the right answer for the same reason it was right before: the number tracks
; how fast the paddle is really moving, and nothing else.
; The result is scaled to quarter units at the end, so the arithmetic above
; stays the pixel arithmetic it reads as.
; -----------------------------------------------------------------------------
ark_english:
    push bx
    mov ax, [ark_pvel]              ; a few pixels either way, so a BYTE divide:
    mov bl, 4                       ; AL = pvel / 4, toward zero as the word
    idiv bl                         ; one was
    cbw
    cmp ax, 2
    jle .lo
    mov ax, 2
.lo:
    cmp ax, -2
    jge .out
    mov ax, -2
.out:
    shl ax, 1                       ; pixels -> quarter units (ARK_VQ), once,
    shl ax, 1                       ; here...
    call ark_scale                  ; ...and then SCALED like everything else,
    pop bx                          ; so the spin a paddle imparts stays the
                                    ; same fraction of ark_vxmax on every
    ret                             ; adapter (SPEC.md 44.3.3). It is the
                                    ; RESULT that is scaled and not the unit:
                                    ; scaling ARK_VQ itself rounds 1.48 to 1
                                    ; and costs CGA a third of the paddle's
                                    ; authority over the angle

; -----------------------------------------------------------------------------
; ark_throw - the sideways speed a SERVE gets from the key the player is on
; out: AX = -ARK_THRHOLD..+ARK_THRHOLD PIXELS, in quarter units; preserves all
;      other registers
;
; The serve has no incoming direction to build on, so the flick IS the aim, and
; a paddle standing still serves straight up - honest rather than a hidden
; default: the player who wants an angle flicks, and the one who does not gets
; to choose after the first bounce.
;
; This is the ONE place that reads the player's intent rather than the paddle's
; motion, and it has to. Everywhere else - ark_english, the rail clamp - the
; question is physical, "how fast is this thing actually going", and [ark_pvel]
; answers it. Here the question is "which way did you ask for", and the two
; stopped agreeing the moment the paddle stopped being fast: at the 2 px a tick
; ARK_PSTEP briefly was, a flick moved the paddle 2 pixels in the tick Space is
; pressed and the old halving took that to 1, so a serve barely left the
; vertical unless the key had been held for half a second first. The mechanism
; had gone quiet, not the intent - and reading the intent is what keeps the
; serve stable while ARK_PSTEP is tuned underneath it, which it has been three
; times.
;
; So the ladder is the STATE MACHINE, and it has exactly three rungs to match.
; Stopped: nothing is being asked for, straight up. Tapping, at ARK_PSTEP:
; ARK_THRTAP. Holding, wound up to ARK_PFAST by the typematic repeats:
; ARK_THRHOLD, harder. The two figures track the two speeds rather than deriving
; from them - a flick that moves the paddle 5 px a tick has to throw harder
; than one that moved it 2, or the aim stops matching the gesture.
;
; A paddle held against a rail still serves off it, which the physical reading
; would refuse. That is the intent answering: the player asked for left.
; -----------------------------------------------------------------------------
ark_throw:
    xor ax, ax                      ; no key latched: nothing has been asked
    cmp word [ark_pkeep], 0         ; for, so the serve goes straight up
    jle .out
    mov ax, [ark_thrtap]            ; a flick throws...
    cmp word [ark_pspd], ARK_PFAST
    jb .sign
    mov ax, [ark_thrhold]           ; ...and a hold throws harder
.sign:
    cmp byte [ark_pdir], 0          ; the key names the side, all of it
    jge .out
    neg ax
.out:
    ret

; -----------------------------------------------------------------------------
; ark_setspeed - the rally's vertical speed for this level
; out: [ark_vymag] = ARK_VYBASE .. ARK_VYMAX, in QUARTER pixels
; preserves all registers
;
; |vy| is the authority, not [ark_bvy]: a paddle bounce restores it rather
; than inventing one, so the vertical rhythm of a rally stays constant while
; the ANGLE is free to change. It is also the one number Slow reduces.
;
; 3.75 px/frame on wall 1, where it used to be 3. The ladder was 3/4/5 and
; could not express anything between, so the opening rally was sluggish and
; the only cure was a whole extra pixel a frame. Quarter units put the base a
; third of the way from the old 3 to the old ceiling and keep the ceiling.
; -----------------------------------------------------------------------------
ark_setspeed:
    push ax
    push dx                         ; mul writes DX, and this routine promises
                                    ; not to. It used to be an add
    mov al, [ark_level]
    mov ah, 0
    or ax, ax                       ; level 1 is the base; wall 0 never
    jz .base                        ; happens, but the multiply must not
    dec ax                          ; underflow if it ever does
.base:
    mul word [ark_vystep]           ; AX = (level-1) * step, both small
    add ax, [ark_vybase]
    cmp ax, [ark_vytop]
    jbe .set
    mov ax, [ark_vytop]
.set:
    mov [ark_vymag], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_do_launch - Space: send the ball off, or fire the laser
; in:  [ark_launch] set by the UI task; out: cleared; preserves all registers
; -----------------------------------------------------------------------------
ark_do_launch:
    push ax
    cmp byte [ark_launch], 0
    je .out
    mov byte [ark_launch], 0
    cmp byte [ark_stuck], 0
    je .laser
    mov byte [ark_stuck], 0         ; thrown the way the paddle is going
    mov ax, [ark_vymag]
    neg ax
    mov [ark_bvy], ax
    call ark_throw
    mov [ark_bvx], ax
    mov byte [ark_mode], M_PLAY
    mov byte [ark_msg], 1           ; the READY line has to come off
    mov ax, 660
    call ark_beep
    jmp .out
.laser:
    cmp byte [ark_laser], 0
    je .out
    call ark_fire
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_do_ball - walk the ball along its velocity, a pixel at a time
; preserves all registers
;
; Bresenham, one axis per step, with a collision test after each single-pixel
; move - not one jump of (vx,vy) a frame. At four pixels a tick a whole-vector
; jump can step straight over a brick's edge and out the other side, and the
; ball then tunnels through a wall it should have bounced off. Stepping means
; the ball is never inside anything, which is also what lets it be erased with
; a plain background fill.
; -----------------------------------------------------------------------------
ark_do_ball:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [ark_stuck], 0
    je .free
    call ark_park                   ; parked: ride the paddle
    jmp .out
.free:
    ; --- quarter-pixel velocity -> whole-pixel delta for THIS frame ---------
    ; The walk below is unchanged: it still gets a whole-pixel (dx,dy) and
    ; still steps one pixel at a time. All that moved is where those two come
    ; from - the velocity is in quarters now, and the remainder is carried
    ; rather than thrown away, which is the whole of what buys 3.75 px/frame.
    ;
    ; sar twice is a FLOOR, so the remainder stays 0..3 and never changes
    ; sign; the long-run average is exact either way, and floor keeps the
    ; accumulator from oscillating around zero. It is deliberately NOT reset
    ; on a bounce: what it holds is less than one pixel of travel, so the
    ; worst a sign flip can do is delay the first step of the new direction
    ; by a single frame.
    mov ax, [ark_accx]
    add ax, [ark_bvx]
    mov bx, ax
    sar bx, 1
    sar bx, 1                       ; BX = whole pixels this frame
    and ax, 3                       ; ...and 0..3 quarters left over, which is
    mov [ark_accx], ax              ; v - 4*floor(v/4) in two's complement
    xchg ax, bx
    cwd                             ; |dx| in AX, its sign in [ark_sx]: DX is
    xor ax, dx                      ; 0 or -1, so this is a branch-free abs,
    sub ax, dx                      ; and DX OR 1 is +1 or -1
    or dx, 1
    mov [ark_sx], dx
    mov si, ax                      ; SI = |dx|

    mov ax, [ark_accy]
    add ax, [ark_bvy]
    mov bx, ax
    sar bx, 1
    sar bx, 1
    and ax, 3
    mov [ark_accy], ax
    xchg ax, bx
    cwd
    xor ax, dx
    sub ax, dx
    or dx, 1
    mov [ark_sy], dx
    mov di, ax                      ; DI = |dy|

    ; THE WALK LIVES IN REGISTERS: ark_move1 preserves every one of them, so
    ; CX = the steps left, DX = the error term, SI = |dx| and DI = |dy| ride
    ; through it and nothing is read back out of memory per pixel
    mov cx, si                      ; steps along the major axis
    cmp cx, di
    jge .n1
    mov cx, di
.n1:
    mov dx, si
    sub dx, di                      ; err = |dx| - |dy|
.walk:
    dec cx                          ; none left (it was 0): done
    js .out
    mov ax, dx
    add ax, ax                      ; e2 = 2*err
    mov bx, di
    neg bx
    cmp ax, bx
    jle .nox
    sub dx, di
    mov ax, [ark_sx]
    xor bx, bx
    call ark_move1                  ; one pixel in x
    jc .out                         ; something reflected: the rest is stale
.nox:
    mov ax, dx                      ; 2*err AGAIN, off the err the x step just
    add ax, ax                      ; moved - the walk has always read it so
    cmp ax, si
    jge .walk
    add dx, si
    xor ax, ax
    mov bx, [ark_sy]
    call ark_move1                  ; ...and one in y
    jnc .walk
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_move1 - try to move the ball one pixel
; in:  AX = dx (-1/0/+1), BX = dy
; out: CF = 1 something was hit and a velocity flipped (stop walking);
;      CF = 0 the ball moved. Preserves every register.
; -----------------------------------------------------------------------------
ark_move1:
    push ax
    push bx
    push cx
    push dx
    mov cx, [ark_bx]
    add cx, ax
    mov dx, [ark_by]
    add dx, bx                      ; CX/DX = the candidate position, which
                                    ; everything below takes in registers

    cmp cx, [ark_rail]              ; --- the rails and the ceiling ---
    jl .bouncex
    mov ax, [ark_cwid]
    sub ax, [ark_rail]
    sub ax, [ark_bsz]
    cmp cx, ax
    jg .bouncex
    cmp dx, [ark_status]
    jl .bouncey

    call ark_hit_brick              ; --- the wall ---
    jc .brick
    call ark_hit_paddle             ; --- the paddle ---
    jc .paddle

    cmp dx, [ark_floor]             ; --- the floor: a life ---
    jg .lost

    mov [ark_bx], cx                ; nothing in the way
    mov [ark_by], dx
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

.bouncex:
    neg word [ark_bvx]
    jmp short .wall
.bouncey:
    neg word [ark_bvy]
.wall:
    mov ax, 1200
    call ark_beep
    jmp short .hit
.brick:
    call ark_reflect
    jmp .hit
.paddle:
    call ark_padbounce
    jmp .hit
.lost:
    call ark_die
.hit:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; ark_reflect - which way to bounce off a brick
; in:  CX = the blocked position's x; preserves all registers
;
; The step that was blocked names the axis: this is only ever reached from a
; single-pixel move, so exactly one coordinate changed and that is the one to
; reverse. No geometry, no corner cases.
; -----------------------------------------------------------------------------
ark_reflect:
    cmp cx, [ark_bx]
    je .yaxis
    neg word [ark_bvx]
    ret
.yaxis:
    neg word [ark_bvy]
    ret

; -----------------------------------------------------------------------------
; ark_hit_paddle - is the candidate rect over the paddle?
; in:  CX/DX = the candidate position, from ark_move1;
; out: CF = 1 hit; preserves all registers
; -----------------------------------------------------------------------------
ark_hit_paddle:
    push ax
    push bx
    cmp word [ark_bvy], 0           ; only ever on the way DOWN, or a ball
    jle .no                         ; clipping the side would stick to it
    mov ax, dx
    add ax, [ark_bsz]
    dec ax
    cmp ax, [ark_pady]
    jl .no
    mov bx, [ark_pady]
    add bx, [ark_ph]
    cmp dx, bx
    jge .no
    mov ax, cx
    add ax, [ark_bsz]
    dec ax
    cmp ax, [ark_px]
    jl .no
    mov bx, [ark_px]
    add bx, [ark_pw]
    cmp cx, bx                      ; CF = 1 is the last test passing - a hit.
    pop bx                          ; Unsigned, where the others are signed:
    pop ax                          ; both sides are on-screen x, positive
    ret
.no:
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; ark_padbounce - bounce off the paddle, with the angle the hit earned
; preserves all registers
;
; Where the ball lands across the paddle picks the outgoing angle - the whole
; of the game's control. Five zones, shallowest out at the ends. The Catch
; powerup parks the ball instead.
; -----------------------------------------------------------------------------
ark_padbounce:
    push ax
    push bx
    push cx
    push dx
    cmp byte [ark_catch], 0
    je .bounce
    mov byte [ark_stuck], 1
    xor ax, ax
    mov [ark_bvy], ax
    mov [ark_bvx], ax
    mov byte [ark_msg], 1
    mov ax, 520
    jmp .beep
.bounce:
    mov ax, [ark_vymag]             ; up again at the rally's speed: the
    neg ax                          ; bounce CONTINUES, it does not restart
    mov [ark_bvy], ax

    mov ax, [ark_bx]                ; the ball's centre, paddle-relative
    mov bx, [ark_bsz]
    shr bx, 1
    add ax, bx
    sub ax, [ark_px]
    jns .in
    xor ax, ax
.in:
    xor dx, dx                      ; zone = centre * 5 / paddle width
    mov bx, 5
    mul bx
    mov bx, [ark_pw]
    or bx, bx
    jz .z2
    div bx
    cmp ax, 4
    jbe .have
.z2:
    mov ax, 2
.have:
    mov [ark_zlast], ax             ; banked for the .minvx tie-break below
    mov bx, ax
    add bx, bx
    mov dx, [ark_zbq+bx]            ; where along the paddle it landed
    call ark_english                ; ...and which way the paddle was going
    add ax, dx
    add ax, [ark_bvx]               ; both ADDED to the incoming vx
    mov bx, [ark_vxmax]             ; BX is dead from the zbias lookup above,
    cmp ax, bx                      ; and the ceiling is a word now, not an
    jle .hi                         ; immediate (SPEC.md 44.3.3)
    mov ax, bx
.hi:
    neg bx
    cmp ax, bx
    jge .minvx
    mov ax, bx
.minvx:
    ; ...and a FLOOR as well as a ceiling. A dead-centre hit with a still
    ; paddle used to leave vx at 0, and a ball going straight up covers no
    ; ground: the walk takes max(|dx|,|dy|) steps, so it was also the slowest
    ; shot in the game, and it comes straight back to where the paddle already
    ; is. The sign is whichever way it was already going, and a ball with no
    ; opinion is sent the way the paddle is (ark_english's sign), falling back
    ; to the side of the paddle it landed on. Only PADDLE bounces get this -
    ; a brick or a wall may still send it vertical.
    mov bx, [ark_vxmin]
    cmp ax, bx
    jge .setvx
    neg bx
    cmp ax, bx
    jle .setvx
    or ax, ax
    jg .plus
    jl .minus
    mov ax, [ark_bvx]               ; exactly nothing: use the incoming side
    or ax, ax
    jg .plus
    jl .minus
    mov ax, [ark_pvel]              ; ...then the paddle's own motion...
    or ax, ax
    jg .plus
    jl .minus
    mov ax, [ark_zlast]             ; ...then which half of the paddle it hit,
    cmp ax, 2                       ; which is never a tie: zone 2 is the
    jl .minus                       ; middle, so <2 is the left half
.plus:                              ; (BX is -vxmin from the test above)
    mov ax, bx
    neg ax
    jmp short .setvx
.minus:
    mov ax, bx
.setvx:
    mov [ark_bvx], ax
    mov ax, 880
.beep:
    call ark_beep
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_hit_brick - is the candidate rect over a live brick? If so, break it
; in:  CX/DX = the candidate position, as ark_hit_paddle;
; out: CF = 1 hit; preserves all registers
;
; Four corners rather than a cell-range walk: the ball is smaller than a brick
; in both axes, so the corners are the only cells it can be in.
; -----------------------------------------------------------------------------
ark_hit_brick:
    push ax
    push bx
    push cx
    push dx
    mov bx, [ark_bsz]               ; BX = the ball's far edge, as an offset:
    dec bx                          ; ark_cell keeps CX and DX, so the four
                                    ; corners are walked by adding and taking it
                                    ; back rather than by re-reading all three
    call ark_cell                   ; (nx, ny)...
    jnc .got
    add cx, bx                      ; (nx+s-1, ny)...
    call ark_cell
    jnc .got
    sub cx, bx
    add dx, bx                      ; (nx, ny+s-1)...
    call ark_cell
    jnc .got
    add cx, bx                      ; ...(nx+s-1, ny+s-1)
    call ark_cell
    cmc                             ; CF = 1 is the ANSWER here: hit
    jnc .out
.got:
    call ark_break                  ; AX = the cell
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_cell - which live brick covers a point?
; in:  CX = x, DX = y (content-relative)
; out: CF = 0 and AX = cell index; CF = 1 no live brick there
; clobbers: AX (BX, CX, DX preserved)
; -----------------------------------------------------------------------------
ark_cell:
    push bx
    push cx
    push dx
    sub cx, [ark_rail]
    js .none
    sub dx, [ark_bricky]
    js .none
    mov bx, dx                      ; BX = y offset into the wall
    mov ax, cx
    xor dx, dx
    div word [ark_bw]               ; AX = column
    cmp ax, ARK_COLS
    jae .none
    mov cx, ax                      ; CX = column
    mov ax, bx
    xor dx, dx
    div word [ark_bh]               ; AX = row
    cmp ax, [ark_rows]
    jae .none
    mov bl, ARK_COLS
    mul bl                          ; AX = row * COLS: a row is a byte
    add ax, cx
    mov bx, ax
    cmp byte [ark_grid+bx], 1       ; CF = 1 exactly when there is no brick
    pop dx
    pop cx
    pop bx
    ret
.none:                              ; its own tail and not a jump to the one
    pop dx                          ; above: this is the COMMON exit, every
    pop cx                          ; corner of a ball below the wall
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; ark_break - take a hit off a brick, score it, maybe drop a capsule
; in:  AX = cell index; preserves all registers
; -----------------------------------------------------------------------------
ark_break:
    push ax
    push bx
    push cx
    push dx
    mov bx, ax
    dec byte [ark_grid+bx]
    mov cl, al                      ; ...and its bit in the dirty map: cell n is
    and cl, 7                       ; bit (n AND 7) of byte (n / 8)
    mov dl, 1
    shl dl, cl
    mov cl, 3
    shr bx, cl
    or [ark_dirty+bx], dl
    mov bx, ax
    mov byte [ark_stat], 1
    mov ax, 1500                    ; a chipped brick sounds different from a
    cmp byte [ark_grid+bx], 0       ; broken one
    jne .chip
    mov ax, 1900
    call ark_beep
    add word [ark_score], 10
    call ark_maybe_pu
    dec word [ark_left]             ; last, so its own flags say "the last one"
    jnz .out
    call ark_cleared
    jmp .out
.chip:
    call ark_beep
    add word [ark_score], 5
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_maybe_pu - one broken brick in ARK_PUCHANCE drops a capsule
; in:  BX = the cell it came from; preserves all registers
; -----------------------------------------------------------------------------
ark_maybe_pu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                      ; DI = the cell
    call OSAPI_RAND
    test al, ARK_PUCHANCE - 1       ; one in ARK_PUCHANCE: a power of two, so
    jnz .out                        ; the remainder is the low bits
    xor si, si                      ; a free slot?
.slot:
    cmp byte [ark_pukind+si], PU_NONE
    je .free
    inc si
    cmp si, ARK_MAXPU
    jb .slot
    jmp .out
.free:
    call OSAPI_RAND                 ; which kind
    xor dx, dx
    mov cx, PU_KINDS
    div cx
    inc dx
    mov [ark_pukind+si], dl
    mov ax, di                      ; the cell's own position, by a BYTE
    mov cl, ARK_COLS                ; divide (a cell is under 256): AL = row,
    div cl                          ; AH = column
    mov dl, ah
    mov dh, 0                       ; DX = column
    mov ah, 0                       ; AX = row
    mov bx, si
    add bx, bx
    push ax
    mov ax, dx
    mul word [ark_bw]
    add ax, [ark_rail]
    mov cx, [ark_bw]
    shr cx, 1
    add ax, cx                      ; the brick's middle, less half a capsule,
    add ax, [ark_ox]                ; rounded to the nearest (+4)... SPEC.md
    sub ax, ARK_PUW / 2 - 4         ; 44.10.6: gfx_blit1 wants an x on the
    and ax, 0FFF8h                  ; BYTE grid, so snap the capsule's -
                                    ; once, at spawn, since it falls straight
    sub ax, [ark_ox]                ; down and its x never changes again. At
                                    ; most 4px, on a 12px capsule dropped from
                                    ; a brick 20-odd wide, and the alternative
                                    ; is the whole band refusing
    mov [ark_pux+bx], ax
    pop ax
    mul word [ark_bh]
    add ax, [ark_bricky]
    mov [ark_puy+bx], ax
    mov [ark_puold+bx], ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_do_pu - fall every capsule, and catch the ones that reach the paddle
; preserves all registers
; -----------------------------------------------------------------------------
ark_do_pu:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.each:
    cmp byte [ark_pukind+si], PU_NONE
    je .next
    mov bx, si
    add bx, bx
    mov ax, [ark_puy+bx]
    add ax, [ark_pufall]            ; [ark_puold] is NOT touched here: it means
    mov [ark_puy+bx], ax            ; where the capsule was last DRAWN, and
                                    ; ark_draw_pu is the only thing that knows
                                    ; that. Moved here, it drifts from the
                                    ; pixels the moment ark_render skips a
                                    ; frame - and then the erase misses
    mov cx, ax                      ; caught? its bottom against the paddle
    add cx, ARK_PUH
    cmp cx, [ark_pady]
    jl .next
    cmp ax, [ark_pady]
    jg .floor
    mov cx, [ark_pux+bx]            ; horizontally over the paddle?
    add cx, ARK_PUW
    cmp cx, [ark_px]
    jl .next
    mov cx, [ark_pux+bx]
    mov dx, [ark_px]
    add dx, [ark_pw]
    cmp cx, dx
    jg .next
    mov al, [ark_pukind+si]
    call ark_apply
    jmp short .gone
.floor:
    cmp ax, [ark_floor]
    jl .next
.gone:                              ; caught or lost, one last whole erase
    mov byte [ark_pukind+si], PU_NONE
    mov byte [ark_puwipe+si], 1
.next:
    inc si
    cmp si, ARK_MAXPU
    jb .each
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_apply - a capsule was caught
; in:  AL = kind; preserves all registers
; -----------------------------------------------------------------------------
ark_apply:
    push ax
    push bx
    add word [ark_score], 50
    mov byte [ark_stat], 1
    cmp al, PU_EXPAND
    je .expand
    cmp al, PU_CATCH
    je .catch
    cmp al, PU_LASER
    je .laser
    cmp al, PU_SLOW
    je .slow
    cmp al, PU_LIFE
    jne .done
    inc byte [ark_lives]            ; the heart
    jmp short .done
.expand:
    mov ax, [ark_pw]
    add ax, 12
    cmp ax, [ark_pwmax]
    jle .setw
    mov ax, [ark_pwmax]
.setw:
    mov [ark_pw], ax
    mov bx, [ark_cwid]              ; A BAT THAT GREW MUST BE PUT BACK INSIDE
    sub bx, [ark_rail]              ; THE RAILS. Only ark_do_paddle clamped,
    sub bx, ax                      ; and it clamps a MOVE - so an Expand taken
    cmp [ark_px], bx                ; while the bat sat against a rail simply
    jle .setw2                      ; made it 12 pixels longer THROUGH the rail
    mov [ark_px], bx                ; (SPEC.md 44.10.1)
.setw2:
    mov byte [ark_padwipe], 1
    jmp .done
.catch:
    mov byte [ark_catch], 1
    jmp .done
.laser:
    mov byte [ark_laser], 1
    mov byte [ark_padwipe], 1
    jmp .done
.slow:
    call ark_slower
.done:
    mov ax, 1046                    ; the catch chime
    call ark_beep
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_slower - knock the rally's vertical speed down a notch, never below
;              ARK_VYFLOOR (a ball with no vy would never come back down)
; preserves all registers
;
; It moves [ark_vymag] rather than [ark_bvy], because vymag is what every
; paddle bounce restores - changing only the live velocity would last exactly
; until the next one.
;
; The notch is ARK_VYSLOW, a QUARTER of what it was: the old step was a whole
; pixel a frame off a base of three, so one capsule took a third of the
; rally's speed away and two made it sluggish. Against the 3.75 base this is
; an eighth, and the floor is 2.5 rather than 2.
; -----------------------------------------------------------------------------
ark_slower:
    push ax
    mov ax, [ark_vymag]
    cmp ax, [ark_vyfloor]
    jle .out
    sub ax, [ark_vyslow]
    cmp ax, [ark_vyfloor]
    jge .live
    mov ax, [ark_vyfloor]
.live:
    mov [ark_vymag], ax             ; ...and take the live ball with it, in
    cmp word [ark_bvy], 0           ; whichever direction it is already going
    jge .set
    neg ax
.set:
    mov [ark_bvy], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_fire / ark_bolt / ark_do_shots - the laser
; ark_fire:     a volley - one bolt out of each muzzle
; ark_bolt:     arm one slot; BX = slot, AX = x
; ark_do_shots: fly them, and let each break one brick
; all three preserve every register
;
; The bolts leave the MUZZLES, not the paddle's centre, and that is what makes
; the outermost brick column at either end reachable at all. ark_do_paddle
; clamps [ark_px] to [ark_rail] .. [ark_cwid]-[ark_rail]-[ark_pw], so a
; CENTRE-fired bolt reached only the middle of that span - half a paddle-width
; inside each rail. Against a 24px brick that margin is 22px with the paddle
; unexpanded, which left a 2px slice of column 0 hittable and nothing more;
; one Expand takes the paddle to [ark_pwmax] = 68 and the margin to 34, wider
; than a brick, and columns 0 and ARK_COLS-1 became unhittable outright - on
; both metric sets, and exactly when the player has most reason to be firing.
;
; The muzzles sit AT the clamp limits: the left one at [ark_px] and the right
; at [ark_px]+[ark_pw]-2, both 2px wide like the bolt itself, so a fully
; deflected paddle fires from the rail - which is exactly where column 0
; starts (ark_cell divides x-[ark_rail] by the brick width) and where column
; ARK_COLS-1 ends. The reach becomes the whole wall at every paddle width,
; and a wider paddle now widens the spread instead of narrowing the reach.
;
; A volley is a PAIR and is fired as one. With fewer than two free slots
; nothing goes out, because one muzzle firing alone reads as a dropped shot
; rather than a deliberate half-volley - and the muzzles are drawn as a pair,
; so the asymmetry would look like a bug rather than a limit.
; -----------------------------------------------------------------------------
ark_fire:
    push ax
    push bx
    push cx
    push si
    mov cx, -1                      ; CX = the first free slot, once found
    xor si, si
.slot:
    cmp byte [ark_shot+si], 0
    jne .next
    cmp cx, -1
    jne .pair                       ; a second free slot: SI is it
    mov cx, si
.next:
    inc si
    cmp si, ARK_MAXSHOT
    jb .slot
    jmp .out                        ; fewer than two free - no half-volleys
.pair:
    mov bx, cx                      ; the left muzzle
    mov ax, [ark_px]
    call ark_bolt
    mov bx, si                      ; ...and the right (ark_bolt kept AX)
    add ax, [ark_pw]
    dec ax
    dec ax
    call ark_bolt
    mov ax, 2200                    ; one beep for the volley, not one each
    call ark_beep
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

ark_bolt:
    push bx
    push dx
    mov byte [ark_shot+bx], 1
    add bx, bx
    mov [ark_shx+bx], ax
    mov dx, [ark_pady]              ; CLEAR of the paddle and its muzzles: a
    sub dx, 8                       ; bolt spawned on the paddle erases its
    mov [ark_shy+bx], dx            ; own first position out of it and leaves
    mov [ark_shold+bx], dx          ; a hole where it was fired from
    pop dx
    pop bx
    ret

ark_do_shots:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.each:
    cmp byte [ark_shot+si], 0
    je .next
    mov bx, si
    add bx, bx
    mov ax, [ark_shy+bx]
    sub ax, 6                       ; [ark_shold] is ark_draw_shots' to write,
    mov [ark_shy+bx], ax            ; for the reason ark_do_pu explains
    cmp ax, [ark_status]
    jle .kill
    mov cx, [ark_shx+bx]
    mov dx, ax
    call ark_cell
    jc .next
    call ark_break
.kill:
    mov byte [ark_shot+si], 0
    mov byte [ark_shwipe+si], 1
.next:
    inc si
    cmp si, ARK_MAXSHOT
    jb .each
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_die - the ball reached the floor
; preserves all registers
; -----------------------------------------------------------------------------
ark_die:
    push ax
    push cx
    mov ax, 160                     ; the long low one
    mov cx, 6
    call ark_beep_n
    dec byte [ark_lives]            ; (the MOVs below keep its flags)
    mov byte [ark_stat], 1
    mov word [ark_hold], 12
    mov al, M_DEAD
    jg .set
    mov al, M_OVER
    mov byte [ark_full], 1
.set:
    mov [ark_mode], al
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_respawn - park a fresh ball on the paddle after a death
; preserves all registers
; -----------------------------------------------------------------------------
ark_respawn:
    call ark_setspeed
    call ark_rearm
    jmp short ark_ready

; ark_rearm - a fresh bat with the ball stuck to it and no power-up live
; (respawn, the next wall and a level start all begin here); preserves all
ark_rearm:
    push ax
    mov word [ark_stuck], 1         ; [ark_stuck] = 1 and [ark_catch] = 0: the
    mov byte [ark_laser], 0         ; two bytes are adjacent (see the bss)
    mov ax, [ark_pw0]
    mov [ark_pw], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_cleared / ark_nextwall - the wall is gone; build the next one
; both preserve every register
; -----------------------------------------------------------------------------
ark_cleared:
    push ax
    mov byte [ark_mode], M_CLEAR
    mov word [ark_hold], 14
    add word [ark_score], 100
    mov ax, 1568
    call ark_beep
    pop ax
    ret

ark_nextwall:
    inc byte [ark_level]
    call ark_setspeed
    call ark_build
    call ark_rearm
    call ark_park                   ; PARK THE BALL BEFORE THE WALL IS DRAWN.
                                    ; The wall was cleared by a hit up among
                                    ; the bricks, so that is where the ball
                                    ; still is - and the full repaint below
                                    ; would draw it there, ON the new wall,
                                    ; making that its erase-from position. The
                                    ; next frame parks it for real and erases
                                    ; the old rect to BACKGROUND, which is a
                                    ; ball-sized hole in a brand new board
                                    ; (SPEC.md 44.10.2)
ark_ready:                          ; ...and the respawn's tail as well
    mov byte [ark_mode], M_READY
    mov byte [ark_full], 1
    ret

; -----------------------------------------------------------------------------
; ark_beep / ark_beep_n - a tone, from the worker (see the file header)
; ark_beep:   in AX = Hz, 2 ticks
; ark_beep_n: in AX = Hz, CX = ticks
; both preserve every register. A refusal (CF=1: something louder owns the
; speaker) is ignored on purpose - sound is decoration here, and a game that
; stalled for it would be worse than a quiet one.
; -----------------------------------------------------------------------------
ark_beep:
    push cx
    mov cx, 2
    call ark_beep_n
    pop cx
    ret

ark_beep_n:
    push ax
    push cx
    push dx
    mov dl, 0x40                    ; the package default priority
    call OSAPI_SND_TONE             ; duration-limited: snd_tick turns it off
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; Building the board
; =============================================================================

; -----------------------------------------------------------------------------
; ark_newgame - a whole new game: score, lives, level 1, a fresh wall
; ark_build   - lay out the wall for [ark_level]
; both preserve every register and draw nothing
; -----------------------------------------------------------------------------
ark_newgame:
    mov word [ark_score], 0
    mov byte [ark_lives], ARK_LIVES
    mov byte [ark_level], 1         ; ...and on into ark_startlevel

; ark_startlevel - lay this level out and park the ball (all regs preserved)
ark_startlevel:
    push ax
    call ark_pu_compose             ; SPEC.md 44.10.6: the capsule sprites, once
                                    ; a level rather than once a frame. Here and
                                    ; not at ark_entry because 11.98.1 rescales
                                    ; the board when the window moves cards -
                                    ; the capsule does not scale, but a level
                                    ; start is the one place that is certain to
                                    ; run after every metrics change
    call ark_setspeed               ; AFTER the level is 1: it reads it
    call ark_rearm
    mov ax, [ark_cwid]              ; the paddle starts centred
    sub ax, [ark_pw]
    shr ax, 1
    mov [ark_px], ax
    mov byte [ark_mode], M_READY
    xor ax, ax
    mov [ark_pkeep], ax
    mov [ark_pdn], ax
    mov [ark_pspd], ax              ; stopped, before the worker's next tick
                                    ; can say so - a key arriving in between
                                    ; would otherwise read as a live hold
    call ark_build
    pop ax
    ret

ark_build:
    push ax
    push bx
    push cx
    push di
    push es
    push ds                         ; ES = DS: everything below is a store into
    pop es                          ; our own bss (DF = 0, SPEC.md 1)
    mov di, ark_grid
    xor bx, bx                      ; BX = row
.row:
    mov al, 2                       ; the top two rows take two hits...
    cmp bx, 1
    jbe .hits
    mov al, 1                       ; ...the rest one...
.hits:
    cmp bx, [ark_rows]
    jb .fill
    mov al, 0                       ; ...and a row past this set's count none
.fill:
    mov cx, ARK_COLS
    rep stosb
    inc bx
    cmp bx, ARK_MAXROW
    jb .row
    mov ax, [ark_rows]              ; every cell of the live rows is a brick
    mov cl, ARK_COLS
    mul cl
    mov [ark_left], ax
    mov cx, ARK_CLRUN               ; DI = ark_dirty: the dirty map and nothing
    xor al, al                      ; in flight, one run (PU_NONE is 0)
    rep stosb
    mov byte [ark_full], 1
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing
; =============================================================================

; -----------------------------------------------------------------------------
; ark_render - the worker's one lock hold a frame
; preserves all registers
;
; Rule 5 of SPEC.md 20.6, and the reason the clip exists: windows move and get
; buried while we sleep, and the gfx_* primitives take absolute screen
; coordinates. Without the clip a covered game paints over whatever is on top
; of it. CF = 1 means not one pixel of our content shows - so the drawing is
; skipped and the game keeps running, which is exactly what the kernel's own
; Bounce does. [ark_full] survives the skip, so whatever was owed still gets
; drawn on the first frame that shows.
; -----------------------------------------------------------------------------
ark_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_GFX_LOCK
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [ark_win]
    test word [es:bx + W_FLAGS], 2  ; still visible?
    jz .skip
    mov si, bx
    call ark_track                  ; the window may have been dragged
    mov bx, [ark_win]
    call OSAPI_WM_CLIP_SET
    jc .skip
    cmp byte [ark_abon], 0          ; the credits are up: the UI task owns the
    jne .skip                       ; content until a click or a key takes them
                                    ; down, and the game is paused underneath
    cmp byte [ark_full], 0
    je .parts
    call ark_draw_full
    jmp short .unlock
.parts:
    call ark_wipe_pu
    call ark_wipe_shots
    call ark_draw_bricks_dirty
    call ark_move_paddle
    call ark_move_ball
    call ark_draw_pu
    call ark_draw_shots
    cmp byte [ark_stat], 0
    je .msg
    mov byte [ark_stat], 0
    call ark_draw_status
.msg:
    cmp byte [ark_msg], 0
    je .unlock
    mov byte [ark_msg], 0
    call ark_clear_msg
    call ark_draw_msg
    jmp short .unlock

    ; --- a frame that drew nothing owes a WHOLE one -------------------------
    ; ark_update ran and moved everything; the screen did not follow. Every
    ; erase this module does is aimed at where something was last DRAWN, so
    ; one skipped frame is survivable - but that is an invariant six separate
    ; pieces of state have to keep, and it cost a stranded capsule once
    ; already (SPEC.md 44.4).
    ;
    ; In practice the kernel repaints us anyway: un-hiding goes through
    ; wm_show, uncovering through wm_paint_dmg, and both end in W_PAINT. That
    ; is a guarantee this module cannot enforce and does not own - and it is
    ; being actively narrowed (SPEC.md 11.90/11.91 exist to make W_PAINT run
    ; LESS often). One byte buys independence from it.
.skip:
    mov byte [ark_full], 1
.unlock:
    call OSAPI_GFX_UNLOCK
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_all - the whole content
; in:  gfx lock held, origin tracked; preserves all registers
; -----------------------------------------------------------------------------
; ark_draw_full - ark_draw_all, settling every debt a partial frame would
; have paid: the flags are cleared FIRST, so one the worker raises while the
; board is being drawn is still standing for the next frame
ark_draw_full:
    mov byte [ark_full], 0
    mov word [ark_stat], 0          ; [ark_stat] and [ark_msg], adjacent bytes
                                    ; ...and on into ark_draw_all
ark_draw_all:
    push ax
    push bx
    push cx
    push dx
    mov al, ARK_BG                  ; THE GROUND AROUND THE RAILS AND NOT
    call OSAPI_SET_COLOR            ; UNDER THEM: it was one fill of the whole
    xor ax, ax                      ; content and the rails drawn over it,
    xor bx, bx                      ; every rail pixel written twice
    mov cx, [ark_cwid]              ; (PERFORMANCE.md rule 2). The status
    dec cx                          ; strip edge to edge...
    mov dx, [ark_status]
    dec dx
    call ark_fillc
    mov bx, [ark_status]            ; ...then the field between the rails,
    mov dx, [ark_chgt]              ; whose bottom the rails share
    dec dx
    mov ax, [ark_rail]
    sub cx, ax                      ; the last column left of the right rail
    call ark_fillc
    mov al, ARK_RAILCOL             ; the two side rails, under the status
    call OSAPI_SET_COLOR            ; strip and down to the same bottom row
    xor ax, ax                      ; (BX, DX: ark_fillc keeps all four)
    mov cx, [ark_rail]
    dec cx
    call ark_fillc
    mov ax, [ark_cwid]
    sub ax, [ark_rail]
    mov cx, [ark_cwid]
    dec cx
    call ark_fillc
    call ark_draw_wall
    call ark_draw_paddle
    call ark_draw_ball
    call ark_draw_pu
    call ark_draw_shots
    call ark_status_all             ; all three fields: the fill above took
                                    ; whatever was on the glass with it, so
                                    ; "unchanged" would draw nothing at all
    call ark_draw_msg
    mov ax, [ark_bx]                ; the erase trail starts here
    mov [ark_obx], ax
    mov ax, [ark_by]
    mov [ark_oby], ax
    mov ax, [ark_px]                ; ...and so does the bat's (SPEC.md 44.10):
    mov [ark_opx], ax               ; a subtraction is against what is ON THE
    mov ax, [ark_pw]                ; GLASS, so every full repaint has to say
    mov [ark_opw], ax               ; what it just put there
    mov al, [ark_laser]
    mov [ark_olaser], al
    cmp byte [ark_abon], 0          ; the credits sit on top of everything, and
    je .noab                        ; every full repaint puts them back - the
    push si                         ; STANDARD card (SPEC.md 20.5.1), measured
    mov bx, [ark_win]               ; from the lines and clamped to the content
    mov si, ark_ablines             ; box, so CGA's narrow field still holds it
    call os88ui_about_d
    pop si
.noab:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_wall / ark_draw_bricks_dirty - every brick, or only the ones that
; changed since the last frame; both preserve every register
; -----------------------------------------------------------------------------
ark_draw_wall:
    push bx
    mov bx, ARK_DIRTYB              ; the map is cleared FIRST: the worker breaks
.clr:                               ; bricks outside the lock, so one broken
    dec bx                          ; while the wall below is being drawn marks
    mov byte [ark_dirty+bx], 0      ; itself again and the next frame redraws it
    jnz .clr                        ; (`mov` writes no flags: the dec's ZF)
.cell:
    call ark_draw_brick             ; BX = 0 here
    inc bx
    cmp bx, ARK_CELLS
    jb .cell
    pop bx
    ret

; The dirty map is a BIT a cell (ark_break sets them), so a frame with nothing
; broken reads ARK_DIRTYB bytes and draws nothing, where a byte a cell was
; sixty compares a frame
ark_draw_bricks_dirty:
    push ax
    push bx
    push si
    xor si, si
.byte:
    mov al, [ark_dirty+si]
    or al, al
    jz .next
    mov byte [ark_dirty+si], 0
    mov bx, si                      ; BX = this byte's first cell
    shl bx, 1
    shl bx, 1
    shl bx, 1
.bit:
    shr al, 1
    jnc .skip
    call ark_draw_brick
.skip:
    inc bx
    or al, al
    jnz .bit
.next:
    inc si
    cmp si, ARK_DIRTYB
    jb .byte
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_brick - one cell: its colour, or the background if it is gone
; in:  BX = cell index; preserves all registers
;
; A two-hit brick carries a white notch rather than a second colour: on a 1bpp
; adapter (SPEC.md 39.4) two colours from the same reduction class are the
; same pixel, and "this one needs another hit" would vanish.
; -----------------------------------------------------------------------------
ark_draw_brick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx
    mov ax, bx                      ; row and column, by a BYTE divide: a cell
    mov cl, ARK_COLS                ; is under 256, so AL = row and AH = column
    div cl
    mov cl, ah
    mov ch, 0                       ; CX = column
    mov ah, 0                       ; AX = row
    cmp ax, [ark_rows]
    jae .out
    mov di, ax                      ; DI = row, for the colour
    mul word [ark_bh]
    add ax, [ark_bricky]
    xchg ax, cx                     ; CX = the brick's top, AX = column
    mul word [ark_bw]
    add ax, [ark_rail]              ; AX = its left
    mov bx, cx                      ; BX = its top
    push ax
    mov al, ARK_BG                  ; gone: back to background
    cmp byte [ark_grid+si], 0
    je .paint
    and di, 7
    mov al, [ark_rowcol+di]
.paint:
    call OSAPI_SET_COLOR
    pop ax
    mov cx, ax
    add cx, [ark_bw]
    dec cx                          ; 1px of mortar on the right and bottom
    dec cx
    mov dx, bx
    add dx, [ark_bh]
    dec dx
    dec dx
    call ark_fillc
    cmp byte [ark_grid+si], 2
    jb .out
    push ax
    mov al, CWHITE                  ; the "needs another hit" notch
    call OSAPI_SET_COLOR
    pop ax
    mov cx, ax
    inc cx
    inc cx
    lea dx, [bx + 1]
    call ark_fillc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Moving a thing writes every pixel ONCE (SPEC.md 44.10)
;
; Both movers here were erase-then-draw: fill the old place with background,
; then draw the new one. Every pixel the two places SHARE is therefore written
; twice a frame - background, then colour - and at 18 fps on a real machine
; the glass catches the gap. That is PERFORMANCE.md Part 1's double-draw flash
; and it is what the field reported as the paddle and ball flickering. The
; paddle had it twice over: its erase was the whole LANE, rail to rail, so a
; 380-pixel band went to background and back on every frame the paddle moved.
;
; The fix is the kernel's own (SPEC.md 7.1.2, and apps/paint's pointer at
; 42.7.1): draw the NEW rect first, then erase only the part of the OLD rect
; the new one does not cover. Nothing is ever absent, nothing is written
; twice, and a move that clears its old place entirely degenerates to one
; erase by itself - so there is no gate on how far the thing moved.
;
; ark_rsub is that subtraction, shared by both movers because both move a
; RECT and a second copy of this arithmetic is a second opinion about which
; pixels belong to the thing that moved.
; =============================================================================

; -----------------------------------------------------------------------------
; ark_rsub - fill with the background the part of the OLD rect that the NEW
;            rect does not cover
; in:  [ark_sol..ark_sob] = the old rect, [ark_snl..ark_snb] = the new one,
;      both (left, top, right, bottom) inclusive and in CONTENT coordinates;
;      the pen is set here; gfx lock held
; out: nothing; preserves every register
;
; Four strips - left of the overlap, right of it, then what is left above and
; below it between those two - and an empty one costs a compare. No overlap at
; all is the whole old rect in one call.
; -----------------------------------------------------------------------------
ark_rsub:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    mov ax, [ark_sol]               ; the overlap, or the whole-rect exit
    cmp ax, [ark_snl]
    jge .ovl
    mov ax, [ark_snl]
.ovl:
    mov cx, [ark_sor]
    cmp cx, [ark_snr]
    jle .ovr
    mov cx, [ark_snr]
.ovr:
    cmp ax, cx
    jg .whole
    mov bx, [ark_sot]
    cmp bx, [ark_snt]
    jge .ovt
    mov bx, [ark_snt]
.ovt:
    mov dx, [ark_sob]
    cmp dx, [ark_snb]
    jle .ovb
    mov dx, [ark_snb]
.ovb:
    cmp bx, dx
    jg .whole
    mov si, ax                      ; THE OVERLAP STAYS IN REGISTERS: SI/DI its
    mov di, cx                      ; left and right, AX/CX the same until a
    push bx                         ; strip below needs them, its top on the
                                    ; stack. The four strips are disjoint and
                                    ; all background, so their order is free:
    mov bx, dx                      ; below it first, while its bottom is in DX
    inc bx
    mov dx, [ark_sob]
    cmp bx, dx
    jg .above
    call ark_fillc                  ; AX/CX = the overlap's columns
.above:
    pop dx                          ; ...above it, between the two
    dec dx
    mov bx, [ark_sot]
    cmp bx, dx
    jg .left
    call ark_fillc
.left:
    mov ax, [ark_sol]               ; left of the overlap, full height
    lea cx, [si - 1]
    mov bx, [ark_sot]
    mov dx, [ark_sob]
    cmp ax, cx
    jg .right
    call ark_fillc
.right:
    lea ax, [di + 1]                ; right of it, full height
    mov cx, [ark_sor]
    cmp ax, cx
    jg .out
    call ark_fillc                  ; BX/DX are still the old rect's rows
    jmp short .out
.whole:
    mov ax, [ark_sol]
    mov bx, [ark_sot]
    mov cx, [ark_sor]
    mov dx, [ark_sob]
    call ark_fillc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_muzsub - subtract one MUZZLE's old rect from where that same muzzle is
;              now; both are 2x2, two rows above the bat
; in:  AX = the old muzzle's left x, BX = the new one's; gfx lock held
; out: nothing; preserves every register
; -----------------------------------------------------------------------------
ark_muzsub:
    push ax
    push bx
    mov [ark_sol], ax
    inc ax
    mov [ark_sor], ax
    mov [ark_snl], bx
    inc bx
    mov [ark_snr], bx
    mov ax, [ark_pady]
    dec ax
    dec ax
    mov [ark_sot], ax
    mov [ark_snt], ax
    inc ax
    mov [ark_sob], ax
    mov [ark_snb], ax
    call ark_rsub
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_paddle / ark_move_paddle - the bat, and the draw-then-subtract pair
; that follows it when it has moved; both preserve every register
; -----------------------------------------------------------------------------
ark_draw_paddle:
    push ax
    push bx
    push cx
    push dx
    mov al, ARK_PADCOL
    call OSAPI_SET_COLOR
    mov ax, [ark_px]
    mov bx, [ark_pady]
    mov cx, ax
    add cx, [ark_pw]
    dec cx
    mov dx, bx
    add dx, [ark_ph]
    dec dx
    call ark_fillc
    cmp byte [ark_laser], 0
    je .out
    mov al, CLRED                   ; two muzzles: a SHAPE, so an armed paddle
    call OSAPI_SET_COLOR            ; still reads where 12 reduces to white
    mov bx, [ark_pady]              ; both are the two rows above the bat...
    lea dx, [bx - 1]
    dec bx
    dec bx
    mov ax, [ark_px]                ; ...two pixels wide at its left end...
    mov cx, ax
    inc cx
    call ark_fillc
    add ax, [ark_pw]                ; ...and at its right
    dec ax
    dec ax
    mov cx, ax
    inc cx
    call ark_fillc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ark_move_paddle:
    push ax
    push bx
    push cx
    push dx
    cmp byte [ark_padwipe], 0
    je .out
    mov byte [ark_padwipe], 0
    mov ax, [ark_pw]                ; a WIDTH change (an Expand) is the
    cmp ax, [ark_opw]               ; whole-lane case, and so is the laser
    jne .lane                       ; being ARMED or lost - a muzzle that
    mov al, [ark_laser]             ; appears or goes has no old rect to
    cmp al, [ark_olaser]            ; subtract from. A muzzle that merely MOVED
    jne .lane                       ; is handled below
    mov ax, [ark_px]                ; a plain move: the new bat FIRST, then
    cmp ax, [ark_opx]               ; only the part of the old one it does not
    je .drawn                       ; cover (SPEC.md 44.10)
    call ark_draw_paddle
    mov cx, [ark_pw]                ; one width for both: a WIDTH change took
    dec cx                          ; the lane path above
    mov ax, [ark_opx]
    mov [ark_sol], ax
    add ax, cx
    mov [ark_sor], ax
    mov ax, [ark_px]
    mov [ark_snl], ax
    add ax, cx
    mov [ark_snr], ax
    mov ax, [ark_pady]              ; the bat's own rows; the muzzles stand
    mov [ark_sot], ax               ; two rows ABOVE them and are their own
    mov [ark_snt], ax               ; subtraction, below
    add ax, [ark_ph]
    dec ax
    mov [ark_sob], ax
    mov [ark_snb], ax
    call ark_rsub
    cmp byte [ark_laser], 0         ; ...and the two muzzles, each against the
    je .drawn                       ; one it IS rather than against the body:
    mov ax, [ark_opx]               ; the body rect covers the muzzle rows at
    mov bx, [ark_px]                ; no x at all, so subtracting them there
    call ark_muzsub                 ; would leave every stale muzzle pixel the
    dec cx                          ; move uncovered (SPEC.md 44.10.3). CX is
                                    ; still pw-1 (ark_rsub keeps it), so both
                                    ; right muzzles are pw-2 along, and
    add ax, cx                      ; ark_muzsub kept AX and BX
    add bx, cx
    call ark_muzsub
    jmp short .drawn
.lane:
    mov al, ARK_BG                  ; the whole lane, so a shrink or a muzzle
    call OSAPI_SET_COLOR            ; leaves nothing behind
    mov ax, [ark_rail]
    mov bx, [ark_pady]
    sub bx, 2
    mov cx, [ark_cwid]
    sub cx, [ark_rail]
    dec cx
    mov dx, [ark_pady]
    add dx, [ark_ph]
    dec dx
    call ark_fillc
    call ark_draw_paddle
.drawn:
    mov ax, [ark_px]                ; what is on the glass now, for the next
    mov [ark_opx], ax               ; frame to subtract against
    mov ax, [ark_pw]
    mov [ark_opw], ax
    mov al, [ark_laser]
    mov [ark_olaser], al
    cmp byte [ark_stuck], 0         ; a parked ball rides in that lane
    je .out
    call ark_draw_ball
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_ball / ark_move_ball - the ball, and the erase-then-draw pair
;
; The ball is never inside a brick or the paddle - ark_move1 reflects before
; the step that would put it there - so erasing it is a plain background fill.
; The one exception is the paddle, which can move into a parked ball, so the
; paddle is redrawn whenever the erased rect reaches its lane.
; -----------------------------------------------------------------------------
ark_draw_ball:
    push ax
    push bx
    push cx
    push dx
    mov al, ARK_BALLCOL
    call OSAPI_SET_COLOR
    mov ax, [ark_bx]
    mov bx, [ark_by]
    mov cx, ax
    add cx, [ark_bsz]
    dec cx
    mov dx, bx
    add dx, [ark_bsz]
    dec dx
    call ark_fillc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ark_move_ball:
    push ax
    push bx
    push cx
    push dx
    mov ax, [ark_bx]
    cmp ax, [ark_obx]
    jne .go
    mov ax, [ark_by]
    cmp ax, [ark_oby]
    je .out
.go:
    call ark_draw_ball              ; the new ball FIRST, then only the part of
                                    ; the old one it does not cover: the two
                                    ; overlap on almost every frame, and
                                    ; erasing first writes that overlap
                                    ; background-then-white where the glass can
                                    ; catch it (SPEC.md 44.10)
    mov cx, [ark_bsz]               ; CX = the far edge's offset, once
    dec cx
    mov ax, [ark_obx]
    mov [ark_sol], ax
    add ax, cx
    mov [ark_sor], ax
    mov ax, [ark_oby]
    mov [ark_sot], ax
    add ax, cx
    mov [ark_sob], ax
    mov ax, [ark_bx]
    mov [ark_snl], ax
    add ax, cx
    mov [ark_snr], ax
    mov ax, [ark_by]
    mov [ark_snt], ax
    add ax, cx
    mov [ark_snb], ax
    call ark_rsub
    mov ax, [ark_oby]               ; did the erase reach the paddle's lane?
    add ax, [ark_bsz]
    cmp ax, [ark_pady]
    jle .nopad
    call ark_draw_paddle            ; the bat is under the ball there, and the
    call ark_draw_ball              ; strips just took a bite out of it - and
                                    ; the bat draws OVER the ball, so the ball
                                    ; goes back on top. Both are rare: the ball
                                    ; is reflected before the step that would
                                    ; put it inside the bat
.nopad:
    mov ax, [ark_bx]
    mov [ark_obx], ax
    mov ax, [ark_by]
    mov [ark_oby], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The capsules: a coloured box with the powerup's letter in it, drawn from the
; kernel font. The LETTER is what identifies it, not the colour - five colours
; do not survive SPEC.md 39.4's reduction to three classes, and a player who
; cannot tell Laser from Slow has no game.
; -----------------------------------------------------------------------------
ark_draw_pu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    mov al, [ark_pukind+si]
    cmp al, PU_NONE
    je .next
    call ark_blit_pu                ; SPEC.md 44.10.6: one blit, if the kernel
    jnc .next                       ; takes it - and CF=1 means it would not
                                    ; take THIS band, so fall through and draw
                                    ; it the way
.slow:                              ; this always did rather than losing it
    mov byte [ark_puband+si], 0     ; ...and SAY SO: this draw lays no vacated
                                    ; strip, so the next frame's ark_wipe_pu
                                    ; owns the erase again (SPEC.md 44.10.6.2)
    push bp
    mov bx, si
    add bx, bx                      ; BX = the word-indexed slot
    mov bp, [ark_pux+bx]            ; BP = x, a VALUE (SS is not DS, SPEC.md 1)
    mov di, [ark_puy+bx]            ; DI = y
    mov [ark_puold+bx], di          ; ...and THIS is where it now sits, which
                                    ; is the only honest thing to erase from
    mov bl, [ark_pukind+si]         ; BX = kind, for the colour/letter tables
                                    ; (BH is 0: a slot is a byte)

    ; THE BODY FIRST, THEN THE EDGE AS FOUR STRIPS (SPEC.md 44.10.4). This was
    ; a solid black rect over the whole capsule with the body inset into it -
    ; two fills instead of five, and the same pixels once it settled. But a
    ; capsule is redrawn on EVERY frame it falls, so every body pixel was
    ; written black and then coloured 18 times a second, which is exactly the
    ; double-draw flash 44.10 is about; the letter went with it, since the body
    ; fill erased the glyph the frame before had drawn. Five fills, no pixel
    ; written twice, and the letter now lands on a body that is already the
    ; right colour. LEA only does the arithmetic - it touches no memory, so BP
    ; in it addresses nothing
    mov al, [ark_pucol+bx]
    call OSAPI_SET_COLOR
    lea ax, [bp + 1]                ; x+1 .. x+PUW-2, y+1 .. y+PUH-2
    lea bx, [di + 1]
    lea cx, [bp + ARK_PUW - 2]
    lea dx, [di + ARK_PUH - 2]
    call ark_fillc

    mov al, CBLACK                  ; ...and the 1px edge around it, which a
    call OSAPI_SET_COLOR            ; capsule needs against the background when
    mov ax, bp                      ; its own colour is a light one - and the
    mov bx, di                      ; mark below is black on the body too, so
    lea cx, [bp + ARK_PUW - 1]      ; this is the last pen it needs
    mov dx, di                      ; the top row, full width
    call ark_fillc
    lea bx, [di + ARK_PUH - 1]
    mov dx, bx                      ; the bottom row, full width (ark_fillc
    call ark_fillc                  ; keeps AX and CX)
    mov cx, ax
    lea bx, [di + 1]
    lea dx, [di + ARK_PUH - 2]      ; the left column, between the two
    call ark_fillc
    lea ax, [bp + ARK_PUW - 1]
    mov cx, ax                      ; ...and the right column
    call ark_fillc

    mov cx, bp
    mov dx, di
    mov bl, [ark_pukind+si]
    mov bh, 0                       ; BX held a y a moment ago
    mov al, [ark_puletter+bx]
    or al, al
    jnz .letter
    add cx, (ARK_PUW - ARK_HEARTW) / 2      ; the heart, centred in the body
    add dx, (ARK_PUH - ARK_HEARTH) / 2
    call ark_heart
    jmp short .marked
.letter:
    add cx, 2
    inc dx
    call ark_charc
.marked:
    pop bp
.next:
    inc si
    cmp si, ARK_MAXPU
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_blit_pu - capsule SI as ONE gfx_blit1 (SPEC.md 44.10.6)
; in:  SI = the slot; out: CF = 0 drawn, CF = 1 not taken (caller draws it the
;      old way); preserves all registers but the flags
;
; The band carries the strip the capsule VACATED as well as the capsule, which
; is why there is no erase to pair with this: a capsule that fell `d` rows
; blits from band row ARK_SPTOP-d for ARK_PUH+d rows, and the first d of those
; are paper. Same idea as SPEC.md 27.2's "the padding IS the erase", one
; dimension over.
;
; It refuses rather than clamping when `d` is larger than the blank margin -
; a frame ark_render skipped - because the honest fallback is the old path,
; which erases what it has to and does not care how far anything moved.
; -----------------------------------------------------------------------------
ark_blit_pu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov di, si                      ; DI = the SLOT: the band pointer is SI's
    mov bx, si
    add bx, bx
    mov ax, [ark_puy+bx]            ; DX = rows fallen since it was last drawn
    sub ax, [ark_puold+bx]
    jb .no                          ; upwards: not a thing a capsule does
    cmp ax, ARK_SPTOP
    ja .no
    mov dx, ax

    mov al, [ark_pukind+di]         ; SI = this kind's band, backed up by the
    mov cl, ARK_SPH * ARK_SPW       ; rows the erase needs: kind * 36, a BYTE
    mul cl                          ; multiply, which leaves DX alone...
    mov si, ax
    sub si, dx                      ; ...less ARK_SPW (2) bytes a row fallen
    sub si, dx
    add si, ARK_SPBASE + ARK_SPTOP * ARK_SPW
%if ARK_SPW != 2 || ARK_SPH * ARK_SPW > 255
  %error "ark_blit_pu's band arithmetic assumes a 2-byte stride and a byte-sized sprite"
%endif

    mov ax, [ark_pux+bx]            ; where: the vacated strip's top-left, in
    add ax, [ark_ox]                ; ABSOLUTE coordinates - gfx_blit1 takes no
    mov bx, [ark_puold+bx]          ; window origin, and clips signed
    add bx, [ark_oy]

    cmp byte [ark_bpp], 1           ; SPEC.md 5.4.2.2: the pen is not read on a
    jbe .pen_done                   ; 1bpp adapter - a band there already means
    push ax                         ; lit and unlit - so this is the VGA half
    push bx                         ; only, and mono gets white on black, which
    mov bl, [ark_pukind+di]         ; is what the reduction gave it anyway
    mov bh, 0
    mov al, [ark_pucol+bx]          ; ink = the capsule's colour...
    mov ah, ARK_BG                  ; ...on background paper, which is CBLACK -
    call OSAPI_GFX_BLIT1_PEN        ; and 0 is a subset of every colour, so
    pop bx                          ; 5.4.2.2's SINGLE-PASS arm takes it
    pop ax
.pen_done:

    add dx, ARK_PUH                 ; DX = rows: the capsule plus the strip
    mov cx, ARK_SPW * 8             ; CX = width, 16 px
    mov bp, ARK_SPW
    push ds                         ; ES:SI is the band, and it is OURS: the
    pop es                          ; slot is not an X stub, so nothing puts
                                    ; our segment in ES for us (SPEC.md 20.1)
    call OSAPI_GFX_BLIT1
    jc .no

    mov bx, di                      ; drawn: and THIS is where it now sits,
    add bx, bx                      ; which is the only honest thing the next
    mov ax, [ark_puy+bx]            ; frame can erase from (the slot rides in
    mov [ark_puold+bx], ax          ; DI, so it cannot come back swapped with
                                    ; the caller's DX - SPEC.md 44.10.6.1)
    mov byte [ark_puband+di], 1     ; ...and the band it laid carries the erase
                                    ; for the strip below it, which is the one
                                    ; thing ark_wipe_pu must not repeat
                                    ; (SPEC.md 44.10.6.2)
    clc
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
.no:                                ; refused, here or by the kernel: the
    stc                             ; caller draws it the old way
    jmp short .out

; -----------------------------------------------------------------------------
; ark_pu_compose - build one 1bpp sprite per capsule kind (SPEC.md 44.10.6)
;
; Called once, from ark_reset_level. Composing at level start rather than per
; frame is the whole point: the expensive half of compose-and-blit is the
; compose, and a capsule's picture does not change while it falls.
;
; A SET bit is the BODY and takes the ink; a CLEAR bit is paper, which is the
; edge, the mark and everything outside the 12px capsule. That polarity is
; chosen so the pen's varying planes want the band AS IT STANDS - ink over
; CBLACK paper, and 0 is a subset of every colour, so SPEC.md 5.4.2.2's
; single-pass arm takes it (a black-ink pen would want the complement and cost
; the slower loop). It also means the four spare columns and the blank rows
; above compose to ARK_BG for free.
;
; The mark CLEARS bits, which is SPEC.md 6.3's rule in the other direction and
; the one bug in this area that produces a plausible wrong result: OR the ink
; in against a set body and nothing changes at all.
; preserves all registers
; -----------------------------------------------------------------------------
ark_pu_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp

    push ds                         ; every byte starts as paper, and the band
    pop es                          ; is ours: ES = DS for the store (DF = 0,
    mov di, ark_sprite              ; SPEC.md 1)
    mov cx, PU_KINDS * ARK_SPH * ARK_SPW / 2
    xor ax, ax
    rep stosw

    mov bp, 1                       ; BP = kind, 1..PU_KINDS, and DI = its
    mov di, ark_sprite + ARK_SPTOP * ARK_SPW    ; capsule row 0, past the
.kind:                              ; blank rows - kind 1 is the band's FIRST
    push di                         ; sprite, PU_NONE having none (ARK_SPBASE)
    mov cx, ARK_PUH - 2             ; the BODY: rows 1..PUH-2, columns 1..10.
.body:                              ; Row 0 and row PUH-1 are the edge and stay
    add di, ARK_SPW                 ; paper, as do columns 0 and 11
    mov word [di], 0xE07F           ; little-endian: byte 0 = 7Fh (x1..x7),
    loop .body                      ; byte 1 = E0h (x8..x10)
    pop di

    push di
    mov bx, bp                      ; the MARK: a letter, or the heart
    mov al, [ark_puletter+bx]
    or al, al
    jz .heart
    call ark_pu_letter
    jmp short .knext
.heart:
    call ark_pu_heart
.knext:
    pop di
    add di, ARK_SPH * ARK_SPW       ; ...and the next kind's row 0
    inc bp
    cmp bp, ARK_SPN
    jb .kind

    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_pu_letter - clear the letter's pixels out of one kind's body
; in:  AL = the character, DI = that kind's capsule row 0
; clobbers AX/BX/CX/DX/SI/DI/ES: ark_pu_compose is its only caller, and it
; banks all of them
;
; The glyph comes from OSAPI_FONT_GLYPHS - the SAME table OSAPI_FONT_CHAR draws
; from, which is the point: no second typeface, and it works on all three
; adapters. It answers ES = DX, and the table is NOT in KERNEL_SEG.
; -----------------------------------------------------------------------------
ark_pu_letter:
    mov bl, al                      ; bank the character: FONT_GLYPHS returns
    call OSAPI_FONT_GLYPHS          ; the range in AL/AH and the table in DX:SI
    cmp bl, al
    jb .out                         ; outside the table: no mark rather than a
    cmp bl, ah                      ; walk past the end of it
    ja .out
    sub bl, al                      ; BL = the glyph's index
    mov bh, 0
    mov es, dx                      ; ES:SI is the table - and ES FIRST, because
                                    ; the `mul` below returns DX:AX and would
                                    ; take the segment with it
    mov al, cl                      ; CX = bytes per glyph (8)
    xor ah, ah                      ; ...and AH is still FONT_GLYPHS' last
                                    ; character, which a 16-bit mul would use
    mul bx
    add si, ax                      ; SI = this glyph's first row

    add di, ARK_SPW                 ; DI = capsule row 1
    mov dx, 8                       ; eight glyph rows onto body rows 1..8
    mov cl, 6                       ; column c sits at capsule x = 2+c, and a
.row:                               ; word here has x0 in bit 15 - so bit
    es lodsb                        ; (7-c) has to reach bit (13-c): shift 6
    mov ah, 0
    shl ax, cl
    xchg al, ah                     ; ...and the band is little-endian bytes
    not ax
    and [di], ax                    ; the mark CLEARS body bits
    add di, ARK_SPW
    dec dx
    jnz .row
.out:
    ret

; -----------------------------------------------------------------------------
; ark_pu_heart - the same, from ark_heartrun's horizontal runs
; in:  DI = the kind's capsule row 0; clobbers AX/BX/CX/SI/DI, as above
; -----------------------------------------------------------------------------
ark_pu_heart:
    add di, ((ARK_PUH - ARK_HEARTH) / 2) * ARK_SPW  ; the row it is centred on
    mov si, ark_heartrun
.row:
    mov ch, 2                       ; two run slots a row; 0xFF ends them
.run:
    lodsw                           ; AL..AH inclusive, in heart columns
    cmp al, 0xFF
    je .rend
.px:
    mov cl, al
    add cl, (ARK_PUW - ARK_HEARTW) / 2  ; ...into capsule columns
    mov bx, 0x7FFF                  ; x0 is bit 15, and this is its COMPLEMENT
    ror bx, cl                      ; rotated, so the hole travels alone
    xchg bl, bh                     ; ...and the band is little-endian bytes
    and [di], bx
    inc al
    cmp al, ah
    jbe .px
.rend:
    dec ch
    jnz .run
    add di, ARK_SPW
    cmp si, ark_heartend
    jb .row
    ret

; -----------------------------------------------------------------------------
; ark_wipe_pu - erase every capsule from where it was last frame
; preserves all registers
; -----------------------------------------------------------------------------
ark_wipe_pu:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.each:
    cmp byte [ark_pukind+si], PU_NONE
    jne .fall
    cmp byte [ark_puwipe+si], 0     ; caught or lost: one last erase, and it
    je .next                        ; has to be the WHOLE capsule
    mov byte [ark_puwipe+si], 0
    mov dx, ARK_PUH - 1
    jmp short .wipe
.fall:                              ; still falling: only the strip it VACATED
    cmp byte [ark_puband+si], 0     ; ...and NOT EVEN THAT once the capsule's
    jne .next                       ; LAST DRAW WAS A BAND (SPEC.md 44.10.6):
                                    ; the vacated strip is the band's own first
                                    ; rows, so erasing it here would write those
                                    ; pixels twice - which is the defect the
                                    ; band was built to remove, moved one
                                    ; routine over.
                                    ; PER SLOT AND SET BY THE DRAW THAT
                                    ; HAPPENED, never by a "bands composed"
                                    ; byte (ark_spok, until size pass 1): that
                                    ; only said a band was worth offering, and a
                                    ; kernel that refuses every one of them
                                    ; (kern_small has no gfx_blit1 at all) left
                                    ; this erase to nobody and streaked two rows
                                    ; of capsule black down the glass every
                                    ; frame - SPEC.md 44.10.6.2
    mov bx, si                      ; since it was last drawn. Two rows in the
    add bx, bx                      ; ordinary frame; more if ark_render
    mov ax, [ark_puy+bx]            ; skipped one and the capsule kept moving;
    sub ax, [ark_puold+bx]          ; NONE if it has not moved since, which is
    jbe .next                       ; every frame of the pause after a death
    dec ax
    mov dx, ax                      ; The other eight rows are drawn over
                                    ; anyway, and erasing them first was 120
                                    ; pixels a frame per capsule spent on
                                    ; pixels nothing would ever see
.wipe:
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    mov bx, si
    add bx, bx
    mov ax, [ark_pux+bx]
    mov bx, [ark_puold+bx]
    mov cx, ax
    add cx, ARK_PUW - 1
    add dx, bx
    call ark_fillc
.next:
    inc si
    cmp si, ARK_MAXPU
    jb .each
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_shots / ark_wipe_shots - the laser bolts
; both preserve every register
; -----------------------------------------------------------------------------
ark_draw_shots:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CLRED
    call OSAPI_SET_COLOR
    xor si, si
.each:
    cmp byte [ark_shot+si], 0
    je .next
    mov bx, si
    add bx, bx
    mov ax, [ark_shy+bx]            ; where it now sits, for the erase
    mov [ark_shold+bx], ax
    mov ax, [ark_shx+bx]
    mov bx, [ark_shy+bx]
    mov cx, ax
    inc cx
    lea dx, [bx + 5]
    call ark_fillc
.next:
    inc si
    cmp si, ARK_MAXSHOT
    jb .each
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ark_wipe_shots:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.each:
    cmp byte [ark_shot+si], 0
    jne .wipe
    cmp byte [ark_shwipe+si], 0
    je .next
    mov byte [ark_shwipe+si], 0
.wipe:
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    mov bx, si
    add bx, bx
    mov ax, [ark_shx+bx]
    mov bx, [ark_shold+bx]
    mov cx, ax
    inc cx
    lea dx, [bx + 5]
    call ark_fillc
.next:
    inc si
    cmp si, ARK_MAXSHOT
    jb .each
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_status - the strip across the top: score, lives, level
; in:  gfx lock held, origin tracked; preserves all registers
;
; The lives are little paddles rather than a number - it is the one figure a
; player reads mid-rally, and three white bars parse faster than a digit.
;
; THREE FIELDS THAT REDRAW SEPARATELY, AND NONE OF THEM BLANKS FIRST (SPEC.md
; 44.10.5). This used to fill the whole strip and re-letter all of it whenever
; anything changed - which is every brick - so the score, the lives and the
; level all went dark and came back several times a second to move one digit.
; That is PERFORMANCE.md Part 1's erase-and-letter pair in its classic form,
; and it is the same fix apps/missile took at SPEC.md 48.9.3.
;
; The score and the level are space-padded to a fixed width and drawn with ONE
; opaque OSAPI_FONT_RUN each: the padding IS the erase, so there is no fill in
; the path at all and no instant with the field missing. Both pens are
; multiples of 8, which is what earns 6.1's single-store path on the two mono
; adapters - a cell row becomes one store, no shift, no read, no second byte.
;
; Each field is drawn only when ITS OWN text changed, so a brick costs the
; score field and nothing else, and the commonest frame costs three compares.
; ark_status_all forces all three, for the full repaint that has just filled
; the content black underneath them.
; -----------------------------------------------------------------------------
ARK_SFW     equ 8                   ; the score field, in CELLS: 8 digits is
                                    ; more score than this game can produce
ARK_LFW     equ 6                   ; ...and the level field, 'LV' + 4

ark_status_all:
    mov word [ark_oscore], 0xFFFF   ; nothing on the glass matches these, so
    mov word [ark_olevel], 0xFFFF   ; every field below redraws (ark_olevel and
                                    ; ark_olives are adjacent bytes) - and on
                                    ; into ark_draw_status itself
ark_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bp, [ark_status]            ; BP = the text baseline, centred in the
    sub bp, 8                       ; strip: a VALUE, for LEA (SPEC.md 1)
    shr bp, 1

    mov ax, [ark_score]             ; --- the score, left, in ARK_SFW cells ---
    cmp ax, [ark_oscore]
    je .lives
    mov [ark_oscore], ax
    mov di, ark_sbuf + ARK_SFW      ; the digits at the field's END, then
    call ark_num2str                ; slid to its start by ark_padl - a copy
    mov si, di                      ; forward with the source ahead of the
    mov di, ark_sbuf                ; destination, so nothing is read after
                                    ; it is written
    mov cx, ARK_SFW
    call ark_padl                   ; left in the field, spaces after
    mov si, ark_sbuf
    mov cx, 8                       ; a multiple of 8, because the window's own
    mov dx, bp                      ; origin is (WF_SNAP is the default) - and
                                    ; 8 rather than 0, which is legal and sits
                                    ; flush against the frame
    mov ax, CWHITE | (ARK_BG << 8)
    call ark_runc

.lives:
    mov al, [ark_lives]             ; --- the lives, as spare paddles --------
    cmp al, [ark_olives]
    je .level
    mov [ark_olives], al
    mov cl, al
    mov ch, 0
    cmp cx, ARK_LIVEMAX
    jbe .livesn
    mov cx, ARK_LIVEMAX             ; more than six would run into the level
.livesn:
    mov al, CLGREEN
    call OSAPI_SET_COLOR
    mov ax, [ark_cwid]              ; AX = slot 0's left x, the slots 8px
    shr ax, 1                       ; apart from there
    sub ax, 16
    lea bx, [bp + 3]                ; ...and every slot is these two rows
    lea dx, [bp + 4]
.life:
    jcxz .livesx
    push cx
    mov cx, ax
    add cx, 5
    call ark_fillc
    pop cx
    add ax, 8
    dec cx
    jmp short .life
.livesx:
    mov cx, [ark_cwid]              ; ...and the slots that are no longer used,
    shr cx, 1                       ; erased in ONE fill from the first free
    add cx, 8 * (ARK_LIVEMAX - 1) + 5 - 16  ; one to the end of the last: none
    cmp ax, cx                      ; left when every slot is lit. Drawing the
    jg .level                       ; live ones and then erasing past them
    push ax                         ; writes no pixel twice, where blanking the
    mov al, ARK_BG                  ; band first would have flashed the ones
    call OSAPI_SET_COLOR            ; that stayed
    pop ax
    call ark_fillc

.level:
    mov al, [ark_level]             ; --- the level, right, in ARK_LFW cells -
    cmp al, [ark_olevel]
    je .out
    mov [ark_olevel], al
    mov ah, 0
    mov di, ark_lbuf                ; 'LV' then the number, right-aligned in
    mov cx, ARK_LFW                 ; the field so the digits sit at its end:
.lsp:                               ; spaces, the digits written backwards from
    mov byte [di], ' '              ; the field's end, and 'LV' in front of the
    inc di                          ; first one. A level is a byte, so three
    loop .lsp                       ; digits and the two letters always fit
    call ark_num2str
    mov word [di - 2], 'LV'
    mov si, ark_lbuf
    mov cx, [ark_cwid]              ; the pen: the right end of the strip,
    sub cx, ARK_LFW * 8             ; rounded DOWN to a multiple of 8 (SPEC.md
    and cx, ~7                      ; 44.10.5), DERIVED here rather than kept:
    jns .lvx                        ; [ark_cwid] is computed by ark_entry
    xor cx, cx                      ; AFTER ark_metrics and again on every
.lvx:                               ; resize, so a copy taken in the metrics
    mov dx, bp                      ; reads a width that does not exist yet.
    mov ax, CWHITE | (ARK_BG << 8)  ; A screen too narrow for the field gets
    call ark_runc                   ; the left edge, and the padding still erases
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
; ark_padl - copy SI into DI left-aligned in a CX-cell field, space padded and
;            NUL-terminated; the string must fit. Preserves every register.
;
; The padding is what makes the field opaque: a space paints background on
; OSAPI_FONT_RUN's fast path, so a field that got SHORTER erases the cells it
; gave up in the same pass that letters the rest, and never blanks.
; -----------------------------------------------------------------------------
ark_padl:
    push ax
    push cx
    push si
    push di
.c:
    lodsb
    or al, al
    jnz .put
    dec si                          ; parked on the NUL: every cell after it
    mov al, ' '                     ; reads the NUL again and pads
.put:
    mov [di], al
    inc di
    loop .c
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

ARK_MSGLH   equ 8                   ; banner line pitch: the glyph cell, so the
                                    ; blank line between M_OVER's two strings is
                                    ; exactly one empty row of text

; -----------------------------------------------------------------------------
; ark_msgh - how tall the banner is for the mode it is about to draw: one line
;            of glyphs, except M_OVER's three (the message, a blank, the key)
; out: AX = height in rows; preserves all other registers
; -----------------------------------------------------------------------------
ark_msgh:
    mov ax, ARK_MSGLH
    cmp byte [ark_mode], M_OVER
    jne .out
    mov ax, ARK_MSGLH * 3
.out:
    ret

; -----------------------------------------------------------------------------
; ark_msgy - the row the banner's FIRST line sits on: the banner is centred on
;            the middle of the open play area, between the bottom of the wall
;            and the paddle, so a taller banner grows symmetrically about the
;            same middle rather than hanging off the one-line row
; out: AX = content y; preserves all other registers
; -----------------------------------------------------------------------------
ark_msgy:
    push dx
    mov ax, [ark_rows]
    mul word [ark_bh]
    add ax, [ark_bricky]
    add ax, [ark_pady]
    shr ax, 1                       ; the middle of the open play area...
    mov dx, ax
    call ark_msgh                   ; ...less half of what is about to sit on it
    shr ax, 1
    sub dx, ax
    xchg ax, dx
    pop dx
    ret

; -----------------------------------------------------------------------------
; ark_draw_msg / ark_clear_msg - the banner, and the band it lives in
; both preserve every register
; -----------------------------------------------------------------------------
ark_clear_msg:
    push ax
    push bx
    push cx
    push dx
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    call ark_msgy
    mov bx, ax
    call ark_msgh                   ; the band is whatever the banner is tall,
    mov dx, bx                      ; plus the row of air it always had
    add dx, ax
    mov ax, [ark_rail]
    mov cx, [ark_cwid]
    sub cx, [ark_rail]
    dec cx
    call ark_fillc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ark_draw_msg:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, [ark_mode]              ; the banner each mode shows, 0 for none
    mov bh, 0
    add bx, bx
    mov si, [ark_msgtab+bx]
    or si, si
    jz .out
    mov al, CYELLOW
    call OSAPI_SET_COLOR
    call ark_msgy
    mov dx, ax
    call ark_msgline
    cmp byte [ark_mode], M_OVER     ; and M_OVER carries a second line, one
    jne .out                        ; blank row under the first: 'GAME OVER - N'
    add dx, ARK_MSGLH * 2           ; on one line read as a sentence cut off
    mov si, ark_s_overn
    call ark_msgline
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_msgline - one line of the banner, centred in the content
; in:  SI = string, DX = content y; the pen is already set
; out: nothing; preserves every register
; -----------------------------------------------------------------------------
ark_msgline:
    push ax
    push cx
    call OSAPI_FONT_WIDTH
    mov cx, [ark_cwid]
    sub cx, ax
    shr cx, 1
    call ark_textc
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_msgband - repaint the banner's BAND and nothing else
; in:  gfx lock held, origin tracked; preserves every register
;
; Pausing and resuming change one thing on screen: the nine rows the banner
; sits on. Both used to call ark_draw_all - the background, both rails, every
; brick in the wall, the paddle, the ball, every capsule and shot, and the
; status strip - to put six characters up and take them down again. On a
; 4.77MHz machine that is most of a second of drawing, and it is also the
; whole frame going white and coming back, which reads as a glitch rather
; than as a pause. Nothing else can have changed: the mode is the only thing
; the command touched, and while the game is paused nothing moves at all.
;
; **The ball is redrawn because a paused game will never redraw it.** The band
; is play area, so the erase takes whatever is standing in it - and every
; other object has a path back on the next frame (ark_draw_pu and
; ark_draw_shots redraw unconditionally, ark_move_paddle on its wipe flag)
; while ark_move_ball is gated on the ball having MOVED. Paused, it never has,
; so a ball inside the band would be erased and stay erased until the resume.
; The capsules and shots are here for the same reason one order lower: they
; would come back, but a frame later, and a capsule that blinks when you press
; P is still a bug.
;
; The order is ark_draw_all's - objects, then the banner over them - because
; the banner is what the player is being shown and it belongs on top.
;
; This does NOT satisfy [ark_full] or [ark_stat], and must not clear them: a
; full repaint the worker already owes is still owed, and the status strip is
; outside this band entirely. It does satisfy [ark_msg], which is exactly what
; it just drew.
; -----------------------------------------------------------------------------
ark_draw_msgband:
    call ark_clear_msg
    call ark_draw_ball
    call ark_draw_pu
    call ark_draw_shots
    call ark_draw_msg
    mov byte [ark_msg], 0
    ret

; -----------------------------------------------------------------------------
; ark_num2str - an unsigned word as decimal, NUL-terminated, written BACKWARDS
; in:  AX = value, DI = where the NUL goes; out: DI = the first digit;
;      preserves every other register
; -----------------------------------------------------------------------------
ark_num2str:
    push ax
    push bx
    push dx
    mov byte [di], 0
    mov bx, 10
.dig:
    xor dx, dx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    or ax, ax
    jnz .dig
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_fillc / ark_textc / ark_charc / ark_runc - the primitives, in CONTENT
; coordinates. Everything above measures from the content origin; only these
; add it.
; ark_fillc:        AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; ark_textc:        SI = string, CX = x, DX = y
; ark_charc:        AL = char,   CX = x, DX = y
; all preserve every register
; -----------------------------------------------------------------------------
ark_fillc:
    push ax
    push bx
    push cx
    push dx
    add ax, [ark_ox]
    add cx, [ark_ox]
    add bx, [ark_oy]
    add dx, [ark_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_heart - the extra-life capsule's mark, content-relative
; in:  CX = left x, DX = top y; the pen is already set
; out: nothing; preserves all registers
;
; Not a font character: the kernel's ROM set is glyphs 32..126 (SPEC.md 6), so
; there is no heart to ask for. Six rows of horizontal runs through ark_fillc,
; which is the same primitive every other shape in this module uses.
; -----------------------------------------------------------------------------
ark_heart:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cx                      ; DI = left x
    mov bx, dx                      ; BX = this row's y
    mov si, ark_heartrun
.run:
    lodsw                           ; AL = x1, AH = x2 (x1 = 0xFF: no run)
    cmp al, 0xFF
    je .skip
    mov cl, ah
    mov ch, 0
    add cx, di                      ; CX = x2
    mov ah, 0
    add ax, di                      ; AX = x1
    mov dx, bx                      ; one row tall
    call ark_fillc
.skip:
    test si, 2                      ; a row is TWO runs, four bytes, and the
    jnz .run                        ; table is aligned to them - so SI is on a
    inc bx                          ; multiple of 4 exactly at a row's end
    cmp si, ark_heartend
    jb .run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ark_textc:
    push cx
    push dx
    add cx, [ark_ox]
    add dx, [ark_oy]
    call OSAPI_FONT_STR_XPARENT
    pop dx
    pop cx
    ret

ark_charc:
    push cx
    push dx
    add cx, [ark_ox]
    add dx, [ark_oy]
    call OSAPI_FONT_CHAR_XPARENT
    pop dx
    pop cx
    ret

; ark_runc: SI = string, CX = x, DX = y, AL = ink, AH = background - ONE
; opaque run (SPEC.md 6.1), which is ark_fillc + ark_textc as a single
; decision per cell and so can never leave the field blank between them
ark_runc:
    push cx
    push dx
    add cx, [ark_ox]
    add dx, [ark_oy]
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    ret

; =============================================================================
; Data
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; x/y/w/h are computed by ark_entry from the live screen.
ark_tpl:
    dw 0, 0, 0, 0
    dw ark_ttl, ark_paint, ark_onkey, ark_onclick

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
    OS88_PREFER ark_pref, ARK_PREF_BW, ARK_PREF_BH,  ARK_PREF_BW, ARK_PREF_BH,  ARK_PREF_SW, ARK_PREF_SH

    OS88_MENUSET ark_menus, ark_m_name, ark_oncmd
        OS88_MENU ark_m_game, ark_mi_game, 2
    OS88_MENUSET_END ark_menus

ark_m_name:  db 'Arkanoid', 0
ark_m_game:  db 'Game', 0
ark_mi_game: dw ark_s_new, ark_s_pcmd
ark_s_new:   db 'New Game', 0
ark_s_pcmd:  db 'Pause', 0

ark_ttl     equ ark_m_name          ; the title IS the menu's name: one string
ark_s_ready: db 'SPACE TO SERVE', 0
ark_s_pause: db 'PAUSED', 0
ark_s_over:  db 'GAME OVER', 0
ark_s_overn: db 'N - NEW GAME', 0   ; the second line of the game-over banner,
                                    ; a blank line under the first. On one line
                                    ; it read 'GAME OVER - N', and a player took
                                    ; the trailing letter for a message that had
                                    ; been cut off rather than for the key that
                                    ; starts the next game
ark_s_clear: db 'WALL CLEARED', 0
ark_msgtab:                         ; ark_draw_msg's, indexed by M_*
    dw ark_s_ready                  ; M_READY
    dw 0                            ; M_PLAY
    dw 0                            ; M_DEAD
    dw ark_s_over                   ; M_OVER
    dw ark_s_pause                  ; M_PAUSE
    dw ark_s_clear                  ; M_CLEAR
%if M_READY != 0 || M_PLAY != 1 || M_DEAD != 2 || M_OVER != 3 || M_PAUSE != 4 || M_CLEAR != 5
  %error "ark_msgtab is indexed by the M_* values in this order"
%endif

; The credits. Kept to 21 characters a line because CGA's metric set gives a
; 206px content (SPEC.md 44.6) and 21 glyphs plus the margins is what fits it.
ark_ablines:
    dw ark_ab1, ark_ab2, ark_ab3, ark_ab4, ark_ab5, 0
ark_ab1:     db 'Arkanoid for os8088', 0
ark_ab2:     db 'A brick-breaker', 0
ark_ab3:     db 0                   ; a blank line is a line with no glyphs
ark_ab4:     db 'Contributed by', 0  ; two lines rather than one because the
ark_ab5:     db 'Elendilon', 0       ; whole credit is 24 glyphs and this
                                     ; window's CGA content holds 21


; Brick colours by row. Not free choices: every one is drawn on the BLACK
; background, so none of them may fall in SPEC.md 39.4's black class (0..6) or
; that row of bricks is invisible on a 1bpp adapter - which is exactly what
; CBROWN did here until a CGA screenshot showed row 1 missing. So the table is
; drawn only from the white class (12, 14, 15) and the dither class (7..11,
; 13), and it ALTERNATES between them, which is what keeps two touching rows
; apart once colour has reduced to three inks.
ark_rowcol:  db CLRED, CLCYAN, CYELLOW, CLGREEN, CWHITE, CLMAGENTA, CLBLUE, CLGRAY

; Capsules, indexed by PU_* (0 unused). The letter is the identifier; the
; colour is a hint that only a 4bpp screen can carry.
ark_pucol:   db 0, CLGREEN, CLCYAN, CLRED, CYELLOW, CLMAGENTA
ark_puletter: db 0, 'E', 'C', 'L', 'S', 0   ; 0 = not a letter (the heart)

; The heart, as horizontal runs: x1..x2 per row, 0xFF ends it. Seven columns
; and six rows, which fits the 10x8 capsule body with a pixel to spare all
; round. Runs and not a bitmap walk because ark_fillc is what this module
; already has, and six fills is cheaper than 42 pixel calls on a 4.77MHz
; machine.
    align 4, db 0                   ; ark_heart reads a row's end off SI's
ark_heartrun:                       ; low bits
    db 1,2,  4,5                    ; .##.##.
    db 0,6,  0xFF, 0                ; #######
    db 0,6,  0xFF, 0                ; #######
    db 1,5,  0xFF, 0                ; .#####.
    db 2,4,  0xFF, 0                ; ..###..
    db 3,3,  0xFF, 0                ; ...#...
ark_heartend:
ARK_HEARTW  equ 7
ARK_HEARTH  equ 6

; --- metrics records (ARK_NMET words each, copied into bss by ark_entry) --------
; brick w, brick h, rows, rail, status strip, gap under it, paddle w, paddle h,
; ball size, paddle inset from the bottom, wanted content height, VELOCITY
; SCALE (per cent of the ARK_V* constants; see them for why it is one number).
;
; The scale is NOT the ratio of the content heights, and that is the point.
; 100 and 37 make BAND TRAVERSALS PER SECOND match, which is what a player
; feels; matching pixels per second is what the code did before and is what
; made CGA feel 2.7x too fast. The measured bands are 198px (VGA), 182px
; (Hercules) and 72px (CGA), against one shared 18.2fps frame clock.
ark_met_big:                        ; VGA 640x480 and Hercules 720x348
    dw ARK_BW_BIG, 10, 6, ARK_RAIL_BIG, 14, 10, 44, 6, 4, 18, ARK_CHW_BIG, 100
ark_met_sml:                        ; CGA 640x200: 137 rows of content, all in
    dw ARK_BW_SML,  7, 5, ARK_RAIL_SML, 11,  5, 34, 4, 3, 13, ARK_CHW_SML,  37

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT            ; the standard About card, and NOTHING else: a
%define OS88UI_NOBTN            ; brick-breaker draws no standard button
%define OS88UI_ABDONLY          ; painter only: ark_clipdraw calls os88ui_about_d
%include "os88ui.inc"

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes ARK_BSS bytes after the image, and
; every name below is an offset from os88_image_end)
; =============================================================================

%assign ARK_BSS 0
%macro AWORD 1
%1 equ os88_image_end + ARK_BSS
%assign ARK_BSS ARK_BSS + 2
%endmacro
%macro ABYTE 1
%1 equ os88_image_end + ARK_BSS
%assign ARK_BSS ARK_BSS + 1
%endmacro
%macro ABUF 2
%1 equ os88_image_end + ARK_BSS
%assign ARK_BSS ARK_BSS + (%2)
%endmacro

; --- the metrics, copied WHOLESALE out of ark_met_* --------------------------
; These eleven words must stay in this order and stay contiguous: ark_entry
; copies the record over them with one loop.
    AWORD ark_bw                    ; brick width
    AWORD ark_bh                    ; brick height
    AWORD ark_rows                  ; brick rows in play
    AWORD ark_rail                  ; side rail width
    AWORD ark_status                ; status strip height
    AWORD ark_gap                   ; gap between it and the wall
    AWORD ark_pw0                   ; paddle width, unexpanded
    AWORD ark_ph                    ; paddle height
    AWORD ark_bsz                   ; ball size
    AWORD ark_pado                  ; paddle inset from the content bottom
    AWORD ark_chwant                ; content height the layout wants
    AWORD ark_vscale                ; velocity scale, PER CENT (SPEC.md 44.3.3)

; --- the velocity family, ARK_V*/ARK_THR* scaled by [ark_vscale] --------------
; Written once by ark_scale_vel and read everywhere after. They are bss rather
; than equs so one binary can hold two adapters' worth of tuning; every one of
; them is in QUARTER pixels except ark_pufall, which is whole ones.
; ark_english scales its own result rather than owning a word here.
    AWORD ark_vxmax
    AWORD ark_vxmin
    AWORD ark_vybase
    AWORD ark_vystep
    AWORD ark_vytop                 ; the CEILING; ark_vymag is the live speed
    AWORD ark_vyfloor
    AWORD ark_vyslow
    AWORD ark_thrtap
    AWORD ark_thrhold
    AWORD ark_pufall                ; capsule fall, WHOLE px/frame
    ABUF  ark_zbq, ARK_NZONE * 2    ; ark_zbias, scaled (SPEC.md 44.3.3)

; --- derived ------------------------------------------------------------------
    AWORD ark_pwmax
    AWORD ark_cwid
    AWORD ark_chgt
    AWORD ark_bricky
    AWORD ark_pady
    AWORD ark_floor
    AWORD ark_scrw
    AWORD ark_dock
    AWORD ark_ox
    AWORD ark_oy

; --- the game -----------------------------------------------------------------
    AWORD ark_win
    AWORD ark_score
    AWORD ark_left                  ; live bricks
    AWORD ark_hold                  ; ticks left in a death/clear pause
    AWORD ark_px                    ; paddle x
    AWORD ark_pw                    ; ...and its live width
    AWORD ark_pkeep                 ; ticks the paddle stays latched to a key
    AWORD ark_pspd                  ; ...how fast it moves while it is, in
                                    ; QUARTER pixels a tick...
    AWORD ark_pacc                  ; ...and the quarter pixels that speed owes
                                    ; it, carried between frames
    AWORD ark_pdn                   ; ticks this press has been HELD for
                                    ; (SPEC.md 9.7), capped at ARK_PTAP, which
                                    ; is the only value ever asked about; it is
                                    ; what promotes a tap to a hold
    AWORD ark_pvel                  ; pixels the paddle moved this frame
    AWORD ark_vymag                 ; the rally's vertical speed, QUARTER px
    AWORD ark_accx                  ; ...and the sub-pixel remainder each axis
    AWORD ark_accy                  ; carries between frames (SPEC.md 44.3.2)
    AWORD ark_zlast                 ; the paddle zone the last bounce landed
                                    ; in, for ark_padbounce's vx tie-break
    ABYTE ark_pdir
    ABYTE ark_bpp
    ABYTE ark_mode
    ABYTE ark_wasmode               ; what Pause interrupted
    ABYTE ark_lives
    ABYTE ark_level
    ABYTE ark_stuck                 ; the ball is parked on the paddle - and
    ABYTE ark_catch                 ; ark_rearm sets these two as ONE word
    ABYTE ark_laser
    ABYTE ark_launch                ; Space, set by the UI task
    ABYTE ark_hired                 ; the worker exists
    ABYTE ark_full                  ; the next frame must repaint everything
    ABYTE ark_abon                  ; 1 = it is up, and the worker draws nothing
    ABYTE ark_stat                  ; ...or at least the status strip
    ABYTE ark_msg                   ; ...or at least the banner (ark_draw_full
                                    ; clears these two as ONE word)
    ABYTE ark_padwipe
    ABYTE ark_olaser                ; whether the bat on the glass has muzzles

; --- the ball -----------------------------------------------------------------
    AWORD ark_bx
    AWORD ark_by
    AWORD ark_bvx
    AWORD ark_bvy
    AWORD ark_obx                   ; where it was drawn last
    AWORD ark_oby
    AWORD ark_opx                   ; ...and the same for the bat: where it was
    AWORD ark_opw                   ; last DRAWN, and how wide it was, which is
                                    ; what ark_rsub subtracts against
    AWORD ark_sol                   ; ark_rsub's two rects, (left, top, right,
    AWORD ark_sot                   ; bottom) inclusive: the OLD one...
    AWORD ark_sor
    AWORD ark_sob
    AWORD ark_snl                   ; ...the NEW one...
    AWORD ark_snt
    AWORD ark_snr
    AWORD ark_snb
    AWORD ark_sx                    ; the Bresenham walk's two signs (the
    AWORD ark_sy                    ; rest of it is in registers)

; --- the worker's frame clock -------------------------------------------------
    AWORD ark_due                   ; [ticks] the next frame is due at

; --- drawing scratch ----------------------------------------------------------
    AWORD ark_oscore                ; the three status fields as they are ON
    ABYTE ark_olevel                ; THE GLASS, so a frame that changed none
    ABYTE ark_olives                ; of them draws nothing (SPEC.md 44.10.5)
    ABUF  ark_sbuf, ARK_SFW + 1     ; the padded score...
    ABUF  ark_lbuf, ARK_LFW + 1     ; ...and the level, composed in place

; --- the board and everything in flight ---------------------------------------
    ABUF  ark_grid,  ARK_CELLS      ; hits left in each brick, 0 = gone
ARK_DIRTYB equ (ARK_CELLS + 7) / 8
    ABUF  ark_dirty, ARK_DIRTYB     ; ...and which ones need redrawing, a BIT
                                    ; a cell. The grid must stay right before
                                    ; it and the five below must follow it:
                                    ; ark_build fills the grid and then clears
                                    ; ARK_CLRUN bytes from here with one run
    ABUF  ark_pukind, ARK_MAXPU
    ABUF  ark_puwipe, ARK_MAXPU
    ABUF  ark_shot, ARK_MAXSHOT
    ABUF  ark_shwipe, ARK_MAXSHOT
ARK_CLRUN equ ARK_DIRTYB + 2 * ARK_MAXPU + 2 * ARK_MAXSHOT
    ABUF  ark_puband, ARK_MAXPU     ; 1 = THIS SLOT'S LAST DRAW WAS A BAND, so
                                    ; the strip it vacated is already erased and
                                    ; ark_wipe_pu must not erase it again
                                    ; (SPEC.md 44.10.6.2). Written by the draw
                                    ; that HAPPENED - set by ark_blit_pu's
                                    ; success path, cleared by ark_draw_pu's
                                    ; .slow - and never by the offer that was
                                    ; made
    ABUF  ark_pux, ARK_MAXPU*2
    ABUF  ark_puy, ARK_MAXPU*2
    ABUF  ark_puold, ARK_MAXPU*2
    ABUF  ark_sprite, PU_KINDS * ARK_SPH * ARK_SPW  ; SPEC.md 44.10.6
ARK_SPBASE equ ark_sprite - ARK_SPH * ARK_SPW   ; ...indexed by KIND, 1 first
    ABUF  ark_shx, ARK_MAXSHOT*2
    ABUF  ark_shy, ARK_MAXSHOT*2
    ABUF  ark_shold, ARK_MAXSHOT*2

    OS88_BSS ARK_BSS
    OS88_IMAGE_END
