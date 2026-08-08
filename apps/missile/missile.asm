; =============================================================================
; os8088 - apps/missile/missile.asm
;
; Missile Command, the twelfth software package (SPEC.md 47). A port of Atari's
; 1980 arcade game - the 6502 sources in the missile-command repository
; (W3MAIN / W3DSUP / W3COMN, "WWIII", project 23603, July 1979) - onto the
; published package ABI. A .o88 binary at org 0 that owns a segment
; (SPEC.md 20.1), prefix `mc_`, embedded icon, one worker task, and NO kernel
; change of any kind.
;
; What came across from the 6502, verbatim where it is a number:
;
;  - **The board.** Six cities and three ABM bases at the arcade's own
;    horizontal coordinates (W3COMN's CITY1H..CITY6H and MISB1H..MISB3H, on
;    its 0..255 field), scaled onto whatever content rectangle this window
;    actually got. Ten missiles a base (MAXMIS), eight ICBMs and eight ABMs in
;    flight (NICBMS/NABMS), seven ICBMs on screen (MXICON).
;  - **The waves.** `mc_icbwav` IS W3MAIN's ICBWAV - 12,15,18,12,16,... ICBMs
;    per wave - and `mc_crmwav` IS CRMWAV, the smart-bomb count that stays 0
;    until wave 6. Satellites and bombers arrive at SPUTWV = 2, MIRVs at
;    MIRVWV = 1.
;  - **The scoring.** 25 points an ICBM, 100 a satellite or bomber (SPUTKI's
;    "4X ICBM"), 125 a smart bomb (CMKILL's "5X"), all times the wave
;    multiplier `min(6, (wave+1)/2)` = SETICS's SMULTI capped at MAXMUL. End of
;    wave pays 5 x multiplier per unused ABM (ABMADD) and 100 x multiplier per
;    surviving city (ENDWV4's "4 ICBM POINTS/CITY"), tallied one at a time with
;    a beep each, exactly as the arcade does it. A bonus city every 10,000
;    points (BONINL's default interval).
;  - **The explosion.** `mc_rad` IS OLDRAD/NEWRAD: 0,0,2,3,...,13,13,...,1,0,0
;    over EXDONE = 27 frames. An explosion below `[mc_lowest]` does no damage
;    and pays nothing (LOWEST), which is what stops a player farming points off
;    the ground.
;
; Four things are this port's own, and each is a consequence of the target:
;
;  - **The game IS the worker task** (SPEC.md 20.6, apps/arkanoid's shape). A
;    missile has to keep falling between clicks, and a window callback only
;    runs when something happens to the window. `mc_worker` sleeps to a
;    DEADLINE rather than for a duration, for the reason SPEC.md 44.1 measures:
;    a frame that crosses one tick would otherwise halve the frame rate rather
;    than shade it.
;
;  - **A trackball becomes the mouse, and three fire buttons become one.** The
;    arcade aims with a trackball and picks a base with one of three buttons.
;    Here the crosshair follows the mouse (polled from the worker) and a click
;    fires from the NEAREST base that still has missiles - keys 1/2/3 still
;    pick a base outright, because the left and right bases are what a player
;    reaches for when the middle one runs dry.
;
;  - **There is no line primitive**, so `mc_line` is a Bresenham that COALESCES
;    each horizontal run into one `OSAPI_GFX_HLINE`. It draws the missile
;    trails, and it draws them one SEGMENT at a time - the two or three pixels
;    a missile moved this frame, not the whole trail - which is what makes
;    fifteen missiles in flight affordable on a 4.77MHz machine. The same
;    routine in the background colour erases a whole trail when its missile
;    dies, which is what the arcade's ERAMIS/ERAABM do.
;
;  - **The palette cycles per wave, and none of it may go black.** The arcade's
;    SETCOL walks ten palettes at one per two waves; so does `mc_pal`. But
;    everything here is drawn on a black field, so a colour from SPEC.md 39.4's
;    black class (0..6) makes that object INVISIBLE on Hercules and CGA - the
;    trap SPEC.md 44.6 records. Every entry is therefore from the white class
;    (12/14/15) or the dither class (7..11, 13), and within a palette the
;    ground and the cities are drawn from DIFFERENT classes, as are the ICBM
;    trails and the ABM trails, so the four things a player must tell apart
;    stay apart once colour has reduced to three inks.
;
; Memory: no heap claim at all. Every array is sized by the arcade's own
; object counts and lives in the package's bss - about 700 bytes of it - so
; this package costs exactly its image and nothing else (SPEC.md 50).
;
; Keys: click to fire, 1/2/3 fire from a named base, F full screen, Esc leave
; it, P pause, N new game.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MISSILE', mc_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A burst at the top, an ICBM trail down to a city skyline on the ground - the
; whole game in one glyph. The mask is the silhouette, so it sits on a clean
; white underlay on the desktop's grey dither.
;
;   ...#........#...
;   ....#......#....
;   .....######.....
;   ....########....
;   .....######.....
;   ..#........#..
;   ..........#.....
;   .........#......
;   ........#.......
;   .......#........
;   ......#.........
;   .....#..........
;   .##...##...###..
;   .##...##...###..
;   ################
;   ################
    OS88_ICON16
    dw 0x1008                       ; 16 mask rows (white underlay)
    dw 0x0810
    dw 0x07E0
    dw 0x0FF0
    dw 0x07E0
    dw 0x2004
    dw 0x0020
    dw 0x0040
    dw 0x0080
    dw 0x0100
    dw 0x0200
    dw 0x0400
    dw 0x631C
    dw 0x631C
    dw 0xFFFF
    dw 0xFFFF
    dw 0x1008                       ; 16 data rows (black pixels)
    dw 0x0810
    dw 0x07E0
    dw 0x0FF0
    dw 0x07E0
    dw 0x2004
    dw 0x0020
    dw 0x0040
    dw 0x0080
    dw 0x0100
    dw 0x0200
    dw 0x0400
    dw 0x631C
    dw 0x631C
    dw 0xFFFF
    dw 0xFFFF
    OS88_ICON16_END

; --- the arcade's counts (W3COMN) ----------------------------------------------
MC_NCITY    equ 6                   ; NCITY
MC_NBASE    equ 3                   ; NMISBA
MC_NTGT     equ MC_NCITY + MC_NBASE ; what an ICBM may be aimed at
MC_MAXMIS   equ 10                  ; MAXMIS: ABMs a base starts a wave with
MC_MAXICBM  equ 8                   ; NICBMS
MC_MAXABM   equ 8                   ; NABMS
MC_MAXEXP   equ 16                  ; NEXPLO is 20; 16 is what MC_MAXICBM +
                                    ; MC_MAXABM can actually produce at once
MC_MAXON    equ 7                   ; MXICON: ICBMs on screen at one time
MC_WLK      equ 16                  ; the stride of one resumable-walk block
%if GLS_SZ > MC_WLK                 ; (SPEC.md 5.6.7). GLS_SZ rounded UP to a
  %error "GLS_SZ no longer fits MC_WLK"   ; shift, so a slot's block is SI<<4
%endif                              ; rather than a multiply by fourteen

MC_MAXDRN   equ 16                  ; trails DRAINING at once (SPEC.md 48.15).
                                    ; MC_MAXICBM + MC_MAXABM, so the queue can
                                    ; never overflow: 8 held it for ordinary
                                    ; play and a salvo dying together spilled
                                    ; the rest into INLINE erases, which is
                                    ; the spike this exists to spread - a
                                    ; field log caught the worst second at
                                    ; 8.2 ms a frame in `wip` against a
                                    ; typical 0.7
MC_DRN_LEFT equ MC_WLK              ; pixels of it still on the screen
MC_DRN_X1   equ MC_WLK + 2          ; ...and the line, for the crosshair and
MC_DRN_Y1   equ MC_WLK + 4          ; for what the erase uncovers at each end
MC_DRN_X2   equ MC_WLK + 6
MC_DRN_Y2   equ MC_WLK + 8
MC_DRN_RATE equ MC_WLK + 10         ; pixels a frame, jittered per trail
MC_DRN_GX   equ MC_WLK + 12         ; the terrain the LAST pixels uncover
MC_DRN_BX   equ MC_WLK + 14         ; the launcher the FIRST ones do; both
                                    ; MC_NODMG for none, and BX doubles as
                                    ; "this entry has not been served yet"
MC_DRN_CX   equ MC_WLK + 16         ; where the erase has REACHED (SPEC.md
MC_DRN_CY   equ MC_WLK + 18         ; 48.19) - the walk block knows, and its
                                    ; layout is not ours to read, so this is
                                    ; carried alongside it
MC_DRN      equ 36                  ; the stride. Not a shift any more, and
                                    ; nothing shifted by it - every reader
                                    ; adds
MC_DRNRATE  equ 24                  ; ...and its floor: eight times the rate a
                                    ; trail is DRAWN at, so a spent one is
                                    ; gone in well under a second
MC_EGDEF    equ 3                   ; frames a dying burst will wait for a lit
                                    ; neighbour before erasing anyway (48.25.1)
MC_ERSH     equ 3                   ; the band quantum an ERASE uses, as a
                                    ; right shift of R (SPEC.md 48.25). 3 is
                                    ; the drawn shape; 0 is one bounding rect
MC_DHEXT    equ 6                   ; probes mc_drn_hold spends walking the
                                    ; Chebyshev square back to the DISC. The
                                    ; gap is at most 0.41r and r is 13, so
                                    ; six is exactly it
MC_DRNBUD   equ 64                  ; pixels a frame across the whole queue -
                                    ; the cap that stops one explosion's worth
                                    ; of dead missiles landing in one frame
MC_DSCMAX   equ 16                  ; walks handed to OSAPI_GFX_LSTEPV at once
%if MC_MAXICBM > MC_DSCMAX || MC_MAXABM > MC_DSCMAX || MC_MAXDRN > MC_DSCMAX
  %error "mc_dsc holds fewer walks than one batch can produce"
%endif
MC_EXPFR    equ 27                  ; EXDONE: frames an explosion lasts
MC_EXPFR3   equ 15                  ; ...and what the coarse ramp lasts, which
                                    ; is SHORTER on purpose: with no collapse
                                    ; the peak is held instead, so the life is
                                    ; cut to keep the sum of the radii - the
                                    ; whole of how lethal a burst is - where
                                    ; the arcade put it (SPEC.md 48.12). It was
                                    ; 21 with three drawn states; SPEC.md 48.18
                                    ; makes it TWO, so a burst is drawn once
                                    ; and erased once instead of twice and
                                    ; once. 13 x 13 = 169 against the old
                                    ; 5x9 + 13x10 = 175: -3.4%, inside 48.12's
                                    ; own tolerance and on the safe side
MC_RMAX     equ 13                  ; ...and the radius it peaks at, on the
                                    ; arcade's 256x231 field
MC_RMAXP    equ 34                  ; ...and the most mc_escale may scale that
                                    ; to on this one. A ceiling only: it bounds
                                    ; mc_disc's row loop and nothing else
MC_MAXMUL   equ 6                   ; MAXMUL
MC_SPUTWV   equ 2                   ; SPUTWV: first wave with a satellite
MC_NWAVE    equ 19                  ; entries in mc_icbwav / mc_crmwav

; --- this port's own numbers ----------------------------------------------------
MC_FP       equ 4                   ; fixed point: positions are pixels * 16
MC_ABMSPD   equ 120                 ; ABM speed, 1/16 px a frame (7.5 px)
MC_SATSPD   equ 2                   ; satellite/bomber, whole px a frame
MC_LAGMAX   equ 0                   ; frames behind its deadline the worker
                                    ; will chase before re-anchoring (44.1).
                                    ; ZERO, and measured: chasing is what the
                                    ; judder IS (SPEC.md 48.20) - a heavy
                                    ; frame is followed by a short one, and
                                    ; motion here is per-FRAME, so a short
                                    ; frame moves everything at 2x. It costs
                                    ; no frames at all: 18.21 fps either way
MC_ABMBON   equ 5                   ; points per unused ABM, per multiplier
MC_BONSTEP  equ 10000               ; ...and the bonus-city interval (BONINL)

; game modes ([mc_mode])
M_READY     equ 0                   ; the pause before a wave starts
M_PLAY      equ 1
M_ENDA      equ 2                   ; end of wave: tallying unused ABMs
M_ENDC      equ 3                   ; ...then the surviving cities
M_OVER      equ 4
M_PAUSE     equ 5

MC_BG       equ CBLACK
MC_NODMG    equ 0x7FFF              ; [mc_gdx1] when the terrain owes nothing
MC_SFW      equ 10                  ; status fields, in CELLS: score / high
MC_WFW      equ 9                   ; ...and 'W12 x6' with room to spare
MC_MSGW     equ 28                  ; the banner's span: 'THE END - N FOR A NEW
                                    ; GAME' is 26, and the padding is its erase

; -----------------------------------------------------------------------------
; mc_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; Sizes a large window onto the live screen and starts a game. Nothing is drawn
; and no task is spawned - the loader has not published our instance yet, so
; OSAPI_TASK_SPAWN would refuse (SPEC.md 20.6); the first W_PAINT hires the
; worker, exactly as apps/arkanoid does.
; -----------------------------------------------------------------------------
mc_entry:
    push si
    push di
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock top row, DH=bpp
    mov [mc_scrw], ax
    mov [mc_dock], cx

    mov byte [mc_expfr], MC_EXPFR   ; bss is zeroed, and a life of zero kills
                                    ; every burst on the frame it is lit
    cmp dh, 1                       ; a 1bpp adapter, or a tier-0 CPU, gets
    ja .chkcpu                      ; the three-state explosion (SPEC.md
    mov byte [mc_mono], 1           ; 48.8). Both are FACTS this code can
    jmp short .coarse               ; test, which is the honest way to spend
.chkcpu:                            ; an optimisation only the slow machine
    call OSAPI_CPU_INFO             ; needs (PERFORMANCE.md rule 10). The
    cmp al, CPU_8086                ; 1bpp half is banked as well, because
    jne .fine                       ; SPEC.md 48.21's trail pens want it
.coarse:
    mov byte [mc_ecoarse], 1
    mov byte [mc_expfr], MC_EXPFR3
.fine:

    call OSAPI_FSX_CAPS             ; can this adapter set Mode X? A fact,
    mov [mc_caps], ax               ; decided once (SPEC.md 47/53.4): the
    test ax, 1 << FSXM_MODEX        ; menu item is renamed to say WHY not,
    jnz .fsxok                      ; and mc_cmd_modex refuses with the same
    mov word [mc_mi_game+6], mc_s_xcmdn ; bit - one predicate for both
.fsxok:

    ; The window wants to be big: seven eighths of the desktop band, capped so
    ; a 640x480 screen still shows the desktop around it. Fullscreen (SPEC.md
    ; 11.2) is a menu item away for a player who wants the whole thing.
    mov ax, [mc_scrw]
    mov bx, ax
    mov cl, 3
    shr bx, cl
    sub ax, bx                      ; 7/8 of the screen width
    cmp ax, 200
    jae .wok
    mov ax, 200
.wok:
    mov [mc_tpl + WT_W], ax

    mov ax, [mc_dock]               ; the desktop band, less one row: the drop
    sub ax, MBAR_H + 1              ; shadow is on row y+h, so a frame that
    mov bx, ax                      ; merely REACHES the dock is already on it
    mov cl, 3                       ; (SPEC.md 11) - and wm_fit would shave the
    shr bx, cl                      ; pixel anyway, so taking it here keeps our
    sub ax, bx                      ; own layout in step with the window the
    cmp ax, 120                     ; kernel actually made
    jae .hok
    mov ax, 120
.hok:
    mov [mc_tpl + WT_H], ax

    mov ax, [mc_scrw]               ; centred horizontally...
    sub ax, [mc_tpl + WT_W]
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [mc_tpl + WT_X], ax
    mov ax, [mc_dock]               ; ...and in the band vertically
    sub ax, MBAR_H
    sub ax, [mc_tpl + WT_H]
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [mc_tpl + WT_Y], ax

    ; A provisional layout, so mc_newgame has somewhere to put the cities. The
    ; first mc_track replaces every one of these numbers with what the window
    ; really got.
    mov ax, [mc_tpl + WT_W]
    sub ax, 2
    mov [mc_cw], ax
    mov ax, [mc_tpl + WT_H]
    sub ax, TITLE_H + 1
    mov [mc_ch], ax
    call mc_layout

    call OSAPI_GET_TICKS            ; seeded once; every wave, target and
    call OSAPI_SRAND                ; satellite walks on down the same stream

    call mc_newgame

    mov si, mc_tpl
    call OSAPI_WM_CREATE
    jc .full
    mov [mc_win], bx
    mov al, 1                       ; keep our CONTENT ORIGIN 8-aligned
    call OSAPI_WM_SNAP              ; (SPEC.md 11.94): mono-only, a no-op on
                                    ; VGA, and what lets the status strip's
                                    ; three opaque runs take font_run's
                                    ; single-store path WINDOWED as well as
                                    ; fullscreen (SPEC.md 48.9). It preserves
                                    ; FLAGS, so the CF the loader reads is
                                    ; still WM_CREATE's. The price is 8px
                                    ; drag steps on the two 1bpp adapters,
                                    ; which for a game window is nothing
    mov si, mc_menus
    call OSAPI_MENU_SET
    mov si, mc_about
    call OSAPI_ABOUT_SET            ; 'About Missile' under our name in the bar
.full:                              ; (SPEC.md 12.2); preserves the flags
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; mc_track - adopt the content origin and size (top of every callback and of
;            every rendered frame)
; in:  SI = window ptr
; out: [mc_ox]/[mc_oy], [mc_cw]/[mc_ch] and every derived row
; preserves all registers
;
; OSAPI_WM_GEOM rather than [es:bx+W_W]: those are FRAME dimensions, and under
; WF_FULL (SPEC.md 11.2) the frame IS the content, so the -2 / -TITLE_H-1 an
; app would subtract itself is wrong in exactly the mode this game most wants
; to run in. One call answers both.
; -----------------------------------------------------------------------------
mc_track:
    push ax
    push bx
    push cx
    push dx
    cmp byte [mc_fsx], 0            ; on the Mode X surface (SPEC.md 53) the
    je .win                         ; content IS the screen: 320x240 at 0,0,
    xor ax, ax                      ; and the window record describes a
    mov [mc_ox], ax                 ; desktop this game is not on
    mov [mc_oy], ax
    mov word [mc_cw], 320
    mov word [mc_ch], 240
    jmp short .lay
.win:
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    cmp ax, [mc_ox]                 ; a trail's walk block (SPEC.md 48.14)
    jne .moved                      ; holds SCREEN coordinates, so a window
    cmp dx, [mc_oy]                 ; that moved invalidates every one of them
    je .keep                        ; at once - and owes a repaint anyway,
.moved:                             ; which is where they are re-laid
    mov byte [mc_full], 1
.keep:
    mov [mc_ox], ax
    mov [mc_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    mov [mc_cw], cx
    mov [mc_ch], dx
.lay:
    call mc_layout
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_layout - every row and column the rest of the game measures from
; in:  [mc_cw]/[mc_ch]; preserves all registers
;
; The city and base columns come from the arcade's own table on its 0..255
; field, mapped onto this content: x = MARGIN + v * (W - 2*MARGIN) / 256. That
; is what keeps the shape of the board - base, three cities, base, three
; cities, base - at any window size and on all three adapters.
; -----------------------------------------------------------------------------
MC_MARGIN   equ 10

mc_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov word [mc_statush], 11       ; the score strip
    mov ax, [mc_ch]                 ; the ground band: an eighth of the height,
    mov cl, 3                       ; held between 14 and 40 rows
    shr ax, cl
    cmp ax, 14
    jae .g1
    mov ax, 14
.g1:
    cmp ax, 40
    jbe .g2
    mov ax, 40
.g2:
    mov [mc_groundh], ax
    mov bx, [mc_ch]
    sub bx, ax
    mov [mc_groundy], bx            ; the ground's top row
    sub bx, 4
    mov [mc_lowest], bx             ; LOWEST: no damage, no score below this
    mov ax, [mc_statush]
    inc ax
    mov [mc_topy], ax               ; ICBMs cross the top edge here

    mov ax, [mc_cw]                 ; a city is a fourteenth of the width...
    xor dx, dx
    mov bx, 14
    div bx
    cmp ax, 12
    jae .c1
    mov ax, 12
.c1:
    cmp ax, 40
    jbe .c2
    mov ax, 40
.c2:
    mov [mc_citw], ax
    mov ax, [mc_cw]                 ; ...and a base a nineteenth
    xor dx, dx
    mov bx, 19
    div bx
    cmp ax, 10
    jae .b1
    mov ax, 10
.b1:
    cmp ax, 34
    jbe .b2
    mov ax, 34
.b2:
    mov [mc_basw], ax

    mov ax, [mc_groundh]            ; both stand three quarters of the band
    mov bx, ax
    shr bx, 1
    shr bx, 1
    sub ax, bx
    cmp ax, 8
    jae .h1
    mov ax, 8
.h1:
    mov [mc_objh], ax

    mov ax, [mc_cw]                 ; the scale the arcade table maps through
    sub ax, 2 * MC_MARGIN
    jns .sok
    xor ax, ax
.sok:
    mov [mc_span], ax
    call mc_escale                  ; ...and the explosion radii, which scale
                                    ; with the window like everything else

    xor si, si                      ; the six cities
.city:
    mov bl, [mc_cityv + si]
    mov bh, 0
    call mc_mapx                    ; AX = content x for arcade column BX
    mov di, si
    add di, di
    mov [mc_cityx + di], ax
    inc si
    cmp si, MC_NCITY
    jb .city

    xor si, si                      ; the three bases
.base:
    mov bl, [mc_basev + si]
    mov bh, 0
    call mc_mapx
    mov di, si
    add di, di
    mov [mc_basex + di], ax
    inc si
    cmp si, MC_NBASE
    jb .base

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; mc_escale - scale OLDRAD onto this window, into [mc_erad]
; in:  [mc_cw]/[mc_ch]; preserves all registers
;
; The arcade's explosion is 27 pixels across on a 256x231 field - a tenth of
; the screen, and that proportion IS the game: it decides how much sky one ABM
; covers and therefore whether a wave is survivable. Left at a literal 13-pixel
; radius on a 560-pixel-wide window it was less than half as big in relative
; terms, and the game became unwinnable for a reason that had nothing to do
; with the design.
;
; The scale is min(cw/256, ch/231) in eighths, taken as a MINIMUM over both
; axes rather than from the width alone: this window is much wider than it is
; tall (and on CGA's 200 rows dramatically so), and a width-scaled burst would
; swallow the whole sky vertically.
; -----------------------------------------------------------------------------
mc_escale:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [mc_cw]                 ; cw * 8 / 256 = cw / 32
    mov cl, 5
    shr ax, cl
    mov bx, ax
    mov ax, [mc_ch]                 ; ch * 8 / 231
    mov cl, 3
    shl ax, cl
    xor dx, dx
    mov cx, 231
    div cx
    cmp ax, bx                      ; ...whichever is smaller
    jbe .have
    mov ax, bx
.have:
    or ax, ax
    jnz .ok
    mov ax, 1                       ; never zero: a burst with no radius kills
.ok:                                ; nothing and the game cannot be played
    cmp ax, 8 * MC_RMAXP / MC_RMAX
    jbe .set
    mov ax, 8 * MC_RMAXP / MC_RMAX
.set:
    mov [mc_escl], ax

    xor si, si                      ; scale the whole ramp once, here, so no
.ramp:                              ; frame ever multiplies
    mov al, [mc_rad + si]           ; the arcade's own ramp...
    cmp byte [mc_ecoarse], 0
    je .src
    mov bl, [mc_step3 + si]         ; ...or SPEC.md 48.8's three-state one,
    mov bh, 0                       ; through the one table that names the
    mov al, [mc_rad3v + bx]         ; state, so radius and colour agree
.src:
    mov bl, al                      ; BL = the unscaled radius, either way
    mov ah, 0
    mul word [mc_escl]
    mov cl, 3
    shr ax, cl
    or bl, bl                       ; a step the ramp gives a radius keeps
    je .store                       ; one, however small the window
    or ax, ax
    jnz .store
    mov ax, 1
.store:
    mov [mc_erad + si], al
    inc si
    cmp si, 32
    jb .ramp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_mapx - one arcade column onto this content
; in:  BX = 0..255; out: AX = content x. Preserves BX, CX, DX
;
; span * v overflows a word (630 * 255 is 160,650), so the product is taken in
; DX:AX and the divide by 256 is a shift of the PAIR - which is the one place
; in this file where the 32-bit intermediate matters.
; -----------------------------------------------------------------------------
mc_mapx:
    push bx
    push cx
    push dx
    mov ax, [mc_span]
    mul bx                          ; DX:AX = span * v
    mov bx, dx
    mov cl, 8
    shr ax, cl
    shl bx, cl
    or ax, bx
    add ax, MC_MARGIN
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The UI task's half: paint, keys, clicks, menus
; =============================================================================

; -----------------------------------------------------------------------------
; mc_paint - W_PAINT: full content repaint, and where the worker is hired
; in:  SI = window ptr; caller holds the gfx lock; preserves all registers
; -----------------------------------------------------------------------------
mc_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mc_track
    call mc_draw_all
    call mc_hire                    ; idempotent: only the first paint spawns
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_hire - spawn the worker, once
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
;
; A refusal is normal and transient - the 12-slot task table can be full - so
; nothing is latched on failure and the next paint tries again. What must not
; happen is a second spawn.
; -----------------------------------------------------------------------------
mc_hire:
    push ax
    push bx
    cmp byte [mc_hired], 0
    jne .out
    mov ax, mc_worker
    mov bx, [mc_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [mc_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_onclick - W_ONCLICK: this is the fire button
; in:  CX = x, DX = y (SCREEN coords), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The click is not acted on here. The worker owns every object in the game, so
; the UI task's whole job is to leave the target behind in three words and let
; the next frame launch from it - apps/arkanoid's [ark_launch], with a point
; attached. [mc_fire] is a COUNTER rather than a flag so two clicks inside one
; frame both fire; the worker takes one off it per frame.
; -----------------------------------------------------------------------------
mc_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mc_track
    call mc_abdismiss               ; a click takes the credits down, and is
    jc .out                         ; spent doing it
    sub cx, [mc_ox]                 ; content coords, which is what everything
    sub dx, [mc_oy]                 ; below this line speaks
    mov [mc_firex], cx
    mov [mc_firey], dx
    mov byte [mc_fireb], 0FFh       ; 0FFh = "the nearest base with missiles"
    inc word [mc_fire]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_onkey - W_ONKEY
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; 1/2/3 fire from a named base at wherever the crosshair is standing, which is
; the arcade's three fire buttons; Space is the nearest base, like a click.
; -----------------------------------------------------------------------------
mc_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                      ; keep the key; mc_track needs AX
    call mc_track
    call mc_abdismiss               ; any key takes the credits down first
    jc .out
    cmp bl, 27                      ; Esc: leave fullscreen (SPEC.md 11.2 -
    je .esc                         ; the menu bar is unreachable up there)
    cmp bl, '1'
    je .b1
    cmp bl, '2'
    je .b2
    cmp bl, '3'
    je .b3
    cmp bl, ' '
    je .bany
    cmp bl, 'p'
    je .pause
    cmp bl, 'P'
    je .pause
    cmp bl, 'n'
    je .new
    cmp bl, 'N'
    je .new
    cmp bl, 'f'
    je .fs
    cmp bl, 'F'
    je .fs
    cmp bl, 'm'
    je .modex
    cmp bl, 'M'
    je .modex
    jmp .out
.b1:
    mov al, 0
    jmp short .fire
.b2:
    mov al, 1
    jmp short .fire
.b3:
    mov al, 2
    jmp short .fire
.bany:
    mov al, 0FFh
.fire:
    mov [mc_fireb], al
    mov ax, [mc_chx]                ; the crosshair is where the mouse is, and
    mov [mc_firex], ax              ; the worker has been tracking it all along
    mov ax, [mc_chy]
    mov [mc_firey], ax
    inc word [mc_fire]
    jmp short .out
.esc:
    call mc_fs_exit
    jmp short .out
.fs:
    call mc_fs_toggle
    jmp short .out
.pause:
    call mc_cmd_pause
    jmp short .out
.new:
    call mc_cmd_new
    jmp short .out
.modex:
    call mc_cmd_modex
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_oncmd - AM_ONCMD: a Game menu item (SPEC.md 12.2)
; in:  AL = item, AH = menu, SI = our window, BX = the set; gfx lock held
; out: nothing; clobbers AX/BX/CX/DX like any window callback
; -----------------------------------------------------------------------------
mc_oncmd:
    push si
    push di
    mov bl, al
    call mc_track
    call mc_abdismiss
    jc .out
    or bl, bl
    jz .new
    cmp bl, 1
    je .pause
    cmp bl, 2
    je .fs
    cmp bl, 3
    je .modex
.out:
    pop di
    pop si
    ret
.new:
    call mc_cmd_new
    jmp short .out
.pause:
    call mc_cmd_pause
    jmp short .out
.fs:
    call mc_fs_toggle
    jmp short .out
.modex:
    call mc_cmd_modex
    jmp short .out

; -----------------------------------------------------------------------------
; mc_cmd_new / mc_cmd_pause - the two commands, shared by keys and menu
; in:  the origin tracked; gfx lock held; preserve all registers
; -----------------------------------------------------------------------------
mc_cmd_new:
    call mc_newgame
    call mc_draw_all
    ret

mc_cmd_pause:
    push ax
    mov al, [mc_mode]
    cmp al, M_PAUSE
    je .resume
    cmp al, M_OVER
    je .out
    mov [mc_wasmode], al
    mov byte [mc_mode], M_PAUSE
    call mc_draw_all
    jmp short .out
.resume:
    mov al, [mc_wasmode]
    mov [mc_mode], al
    call mc_draw_all
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_cmd_modex - take the game onto the SPEC.md 53 exclusive surface
; in:  gfx lock held (menu command / key context); preserves all registers
;
; The refusal is the greying's own predicate: the caps bit mc_entry tested to
; rename the menu item. OSAPI_FSX_RUN does not return until mc_fsx_main does;
; the kernel then repaints the desktop whole, our window included, so there
; is nothing to redraw here.
; -----------------------------------------------------------------------------
mc_cmd_modex:
    push ax
    push bx
    push cx
    test word [mc_caps], 1 << FSXM_MODEX
    jz .out
    mov byte [mc_fsxm], FSXM_MODEX
    mov ax, mc_fsx_main
    mov bx, [mc_win]
    xor cx, cx                      ; no flags: our worker freezes with the
    call OSAPI_FSX_RUN              ; rest, and this loop replaces it
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_fsx_main - the exclusive main (SPEC.md 53.1): the arcade at 320x240
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, gfx lock held for
;      the whole session, every other task frozen
; out: near ret = leave the bracket; the kernel restores the desktop
;
; The worker's loop, rewritten for a machine we own: input is polled (no
; events are dispatched in a bracket), one frame per OSAPI_FSX_WAIT tick -
; the worker's own pace - and mc_rbody draws through the Mode X twins. The
; worker itself is frozen; when it thaws its deadline is hopelessly behind
; and its own re-anchor path absorbs that, nothing here needs to.
; -----------------------------------------------------------------------------
mc_fsx_main:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FONT_GLYPHS          ; SI/DX = the kernel glyph table's
    mov [mc_gtab], si               ; offset and segment, for mx_text
    mov [mc_gseg], dx
    push ds
    pop es
    mov di, mc_fsi
    mov al, [mc_fsxm]               ; SPEC.md 53.7: a bracket that never
    cmp al, 0FFh                    ; switches modes may keep drawing with the
    je .same                        ; kernel's own slots - "exclusive but same
    call OSAPI_FSX_MODE             ; mode: a game loop that wants zero
    jc .home                        ; jitter". [mc_fsx] stays 0 for it, which
    mov byte [mc_fsx], 1            ; is what leaves mc_track, mc_fillc and
.same:                              ; mc_line on their ordinary paths
    mov si, [mc_win]
    call mc_track                   ; 0,0,320,240 and the layout from it
    mov byte [mc_full], 1           ; everything draws from scratch
    mov byte [mc_chshown], 0        ; the crosshair is not on this screen yet
    call OSAPI_MOUSE
    mov [mc_pbtn], al               ; no fire from the click that got us here
.loop:
.keys:
    mov ah, 1                       ; the bracket's input model: poll int 16h
    int 0x16                        ; (this IS the UI task, SPEC.md 53.1)
    jz .nokey
    xor ah, ah
    int 0x16
    cmp al, 27                      ; Esc leaves, exactly as it leaves 11.2
    je .done
    cmp al, 'f'                     ; ...and so does the key that got us here,
    je .done                        ; because on the same-mode bracket this IS
    cmp al, 'F'                     ; the Full Screen the user asked for
    je .done
    call mc_fsx_key
    jmp short .keys
.nokey:
    call OSAPI_MOUSE                ; AL = buttons; a new press fires at the
    mov ah, [mc_pbtn]               ; crosshair, which mc_do_cross has been
    mov [mc_pbtn], al               ; tracking all along
    not ah
    and ah, al
    test ah, 1
    jz .nofire
    mov ax, [mc_chx]
    mov [mc_firex], ax
    mov ax, [mc_chy]
    mov [mc_firey], ax
    mov byte [mc_fireb], 0FFh
    inc word [mc_fire]
.nofire:
    call mc_update
    call mc_rbody
    xor al, al                      ; one frame a tick - the worker's pace,
    call OSAPI_FSX_WAIT             ; the frame clock's hlt (SPEC.md 53.5)
    jmp .loop
.done:
    mov byte [mc_fsx], 0
    mov byte [mc_chshown], 0        ; the crosshair on THIS screen dies with
                                    ; it - or the thawed worker XOR-erases
                                    ; one that was never drawn on the desktop
    mov byte [mc_full], 1
.home:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_fsx_key - the bracket's keyboard: the windowed bindings minus the ones
; that make no sense with no window (F, Esc handled by the caller)
; in: AL = ascii
mc_fsx_key:
    push ax
    cmp al, '1'
    je .b1
    cmp al, '2'
    je .b2
    cmp al, '3'
    je .b3
    cmp al, ' '
    je .bany
    cmp al, 'p'
    je .pause
    cmp al, 'P'
    je .pause
    cmp al, 'n'
    je .new
    cmp al, 'N'
    je .new
    jmp short .out
.b1:
    mov al, 0
    jmp short .fire
.b2:
    mov al, 1
    jmp short .fire
.b3:
    mov al, 2
    jmp short .fire
.bany:
    mov al, 0FFh
.fire:
    mov [mc_fireb], al
    mov ax, [mc_chx]
    mov [mc_firex], ax
    mov ax, [mc_chy]
    mov [mc_firey], ax
    inc word [mc_fire]
    jmp short .out
.pause:
    call mc_cmd_pause
    jmp short .out
.new:
    call mc_cmd_new
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_fs_toggle / mc_fs_enter / mc_fs_exit - the fullscreen transitions
; (SPEC.md 11.2; apps/artful's idiom - the flag flips BEFORE the call, because
; the W_PAINT that runs inside wm_front must already see the answer)
; in:  gfx lock held; preserve all registers
; -----------------------------------------------------------------------------
mc_fs_toggle:
    cmp byte [mc_fs], 0
    jne mc_fs_exit
    ; fall through

mc_fs_enter:
    push ax
    push bx
    push cx
    cmp byte [mc_fs], 0
    jne .out
    mov al, 1
    mov bx, [mc_win]
    call OSAPI_FULLSCREEN           ; fronts + repaints under the held lock,
    jc .out                         ; and that repaint is our own W_PAINT, so
    mov byte [mc_fs], 1             ; the layout re-derives itself

    ; ...and then take the machine. SPEC.md 53.7's "exclusive but same mode":
    ; no fsx_mode call, so the drawing slots stay legal and the geometry is
    ; still the desktop's - what the bracket buys is EXCLUSIVITY, and on this
    ; game that is the largest item there is. PERFORMANCE.md Set 4 measured a
    ; windowed-fullscreen frame paying 6.2 ms of gfx_lock + mc_track +
    ; wm_clip_set and 5.7 ms of gfx_unlock EVERY frame, drawn content or not:
    ; 21.8% of a 77-second session spent entering and leaving the drawing
    ; critical section. The bracket holds the lock for its whole life, so all
    ; of that goes - and the system arrow goes with it, which is the double
    ; cursor this surface has had since it was written.
    mov byte [mc_fsxm], 0FFh
    mov ax, mc_fsx_main
    mov bx, [mc_win]
    xor cx, cx                      ; no KEEPWORKER: our worker freezes with
    call OSAPI_FSX_RUN              ; the rest and this loop replaces it
    jc .out                         ; REFUSED: stay on the 11.2 surface, which
                                    ; is exactly what this did before - the
                                    ; bracket is an optimisation, not the
                                    ; feature
    mov byte [mc_fs], 0             ; ...and when it returns, so does the
    mov al, 0                       ; window: Esc or F left the bracket
    mov bx, [mc_win]
    call OSAPI_FULLSCREEN
.out:
    pop cx
    pop bx
    pop ax
    ret

mc_fs_exit:
    push ax
    push bx
    cmp byte [mc_fs], 0
    je .out
    mov byte [mc_fs], 0
    mov al, 0
    mov bx, [mc_win]
    call OSAPI_FULLSCREEN           ; restores geometry + wm_paint_all, which
.out:                               ; re-enters mc_paint windowed
    pop bx
    pop ax
    ret

; =============================================================================
; 'About Missile' - the credits (SPEC.md 12.2)
; =============================================================================
; The panel is drawn inside our own content: a package has no way to put up a
; second window, and the sky is exactly the rectangle a notice wants.
;
; Two rules make it safe against the worker, which is drawing the same content
; eighteen times a second. [mc_abon] is checked by mc_render UNDER THE LOCK,
; right after the clip is armed, and the frame is dropped whole while it is
; set. And the game is PAUSED while it is up: a skipped frame does not stop the
; ICBMs, so without the pause a player would read the credits and lose a city.

mc_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [mc_abon], 1
    mov al, [mc_mode]
    cmp al, M_PLAY                  ; a live wave is frozen underneath; every
    jne .draw                       ; other mode is already still and keeps
    mov [mc_wasmode], al            ; whatever banner it was showing
    mov byte [mc_mode], M_PAUSE
.draw:
    call mc_track
    call mc_draw_all                ; ...which draws the panel last, because
    pop di                          ; [mc_abon] is set
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_abdismiss - take the panel down if it is up
; in:  the origin tracked; gfx lock held
; out: CF = 1 the click/key was spent doing it; preserves every register
; -----------------------------------------------------------------------------
mc_abdismiss:
    cmp byte [mc_abon], 0
    je .none
    mov byte [mc_abon], 0
    call mc_draw_all                ; the game stays paused: the key that took
    stc                             ; the credits down is not also a resume
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; mc_abdraw - the panel itself, last of everything
; in:  gfx lock held, origin tracked; preserves every register
;
; Measured from the strings rather than pinned, because this window is sized
; from the live screen and CGA's 200 rows give a much smaller content than
; VGA's 480 (SPEC.md 39).
; -----------------------------------------------------------------------------
MC_ABLH equ 10                      ; line pitch, px (8px glyphs + 2 of air)

mc_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, mc_ablines
.meas:
    mov bx, [si]
    or bx, bx
    jz .done
    inc di
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH
    pop si
    cmp ax, cx
    jbe .meas
    mov cx, ax
    jmp short .meas
.done:
    add cx, 16
    cmp cx, [mc_cw]
    jbe .wok
    mov cx, [mc_cw]
.wok:
    mov [mc_abw], cx
    mov ax, di
    mov bx, MC_ABLH
    mul bx
    add ax, 14
    cmp ax, [mc_ch]
    jbe .hok
    mov ax, [mc_ch]
.hok:
    mov [mc_abh], ax
    mov ax, [mc_cw]
    sub ax, [mc_abw]
    shr ax, 1
    mov [mc_abl], ax
    mov ax, [mc_ch]
    sub ax, [mc_abh]
    shr ax, 1
    mov [mc_abt], ax

    mov al, CWHITE
    call mc_setcol
    call .rect
    call mc_fillc
    mov al, CBLACK                  ; black on white: the one pairing that
    call mc_setcol            ; survives SPEC.md 39.4 on every adapter
    call .rect
    call mc_framec

    mov si, mc_ablines
    mov di, [mc_abt]
    add di, 7
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH
    mov cx, [mc_abw]
    sub cx, ax
    shr cx, 1
    add cx, [mc_abl]
    mov dx, di
    call mc_textc
    pop si
    add di, MC_ABLH
    jmp short .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.rect:                              ; the panel as (x1,y1)-(x2,y2), inclusive
    mov ax, [mc_abl]
    mov bx, [mc_abt]
    mov cx, ax
    add cx, [mc_abw]
    dec cx
    mov dx, bx
    add dx, [mc_abh]
    dec dx
    ret

; =============================================================================
; The worker: the game itself (SPEC.md 20.6)
; =============================================================================

; -----------------------------------------------------------------------------
; mc_worker - THE background task
; in:  DX = our instance index, DS = ES = CS = our segment, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own: the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; One frame a tick. It sleeps to a DEADLINE rather than for a duration: sleep
; is relative to [ticks] at the call, so a loop that works and then sleeps 1
; has a period of ceil(work) + 1 - and the instant a frame's work crosses one
; 55ms tick the rate does not sag, it halves (SPEC.md 44.1).
; -----------------------------------------------------------------------------
mc_worker:
    call OSAPI_GET_TICKS
    mov [mc_due], ax                ; the first frame is due now
.loop:
    mov bx, [mc_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4)

    inc word [mc_due]
    call OSAPI_GET_TICKS            ; AX = now
    mov bx, [mc_due]
    sub bx, ax                      ; BX = ticks still to wait, SIGNED, and
    jle .behind                     ; wrap-safe by subtraction (SPEC.md 8)
    mov ax, bx
    call OSAPI_TASK_SLEEP
    jmp short .frame
.behind:
    cmp bx, -MC_LAGMAX              ; a little late: run now and keep the
    jg .frame                       ; deadline, so the next short frame catches
    mov [mc_due], ax                ; up. Hopelessly late and the deadline is
.frame:                             ; re-anchored, or it runs away and this
    call mc_update                  ; loop never sleeps again
    call mc_render
    jmp .loop

; -----------------------------------------------------------------------------
; mc_update - advance one frame. NO lock held, nothing drawn.
; preserves all registers
; -----------------------------------------------------------------------------
mc_update:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mc_do_cross                ; the crosshair follows the mouse in every
                                    ; mode, so aiming stays live while paused
    mov al, [mc_mode]
    cmp al, M_PAUSE
    je .out
    cmp al, M_OVER
    je .out
    cmp al, M_READY
    je .ready
    cmp al, M_ENDA
    je .enda
    cmp al, M_ENDC
    je .endc

    call mc_recount                 ; how many ICBMs are really in the sky
    call mc_do_fire
    call mc_do_launch
    call mc_do_sat
    call mc_move_icbm
    call mc_move_abm
    call mc_do_exp
    call mc_do_damage
    call mc_check_wave
    jmp short .out
.ready:
    dec word [mc_hold]
    cmp word [mc_hold], 0
    jg .out
    call mc_startwave
    jmp short .out
.enda:
    call mc_tally_abm
    jmp short .out
.endc:
    call mc_tally_city
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_recount - how many ICBMs are actually in the sky
; out: [mc_onscr]; preserves all registers
;
; DERIVED every frame rather than maintained by a counter. MXICON gates every
; launch, so a count that drifts high stops the wave launching, mc_check_wave
; then waits forever on a budget nothing will spend, and the game hangs with a
; still screen - which is the same failure the no-target case produces and is
; just as hard to see. Five launch sites and three death sites had to agree for
; a counter to stay honest; an eight-iteration loop needs no agreement at all.
; -----------------------------------------------------------------------------
mc_recount:
    push ax
    push si
    xor ax, ax
    xor si, si
.each:
    cmp byte [mc_ia + si], 1
    jb .next
    cmp byte [mc_ia + si], 2
    ja .next
    inc ax
.next:
    inc si
    cmp si, MC_MAXICBM
    jb .each
    mov [mc_onscr], al
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_do_cross - the crosshair follows the mouse
; preserves all registers
;
; OSAPI_MOUSE is worker-safe (SPEC.md 20.6 rule 7) and answers in SCREEN
; coordinates, so the origin comes off it here; the result is clamped to the
; sky, because an ABM aimed into the ground or the score strip is not a shot a
; player meant to take.
; -----------------------------------------------------------------------------
mc_do_cross:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    cmp byte [mc_fsx], 0            ; the mouse stays in DESKTOP coordinates
    je .desk                        ; through a bracket (SPEC.md 53.1), and
    shr cx, 1                       ; Mode X only exists where the desktop is
    shr dx, 1                       ; 640x480 - one shift maps it onto 320x240
.desk:
    sub cx, [mc_ox]
    sub dx, [mc_oy]
    mov ax, [mc_cw]                 ; clamp into the content
    dec ax
    cmp cx, ax
    jle .x1
    mov cx, ax
.x1:
    or cx, cx
    jns .x2
    xor cx, cx
.x2:
    mov ax, [mc_lowest]
    cmp dx, ax
    jle .y1
    mov dx, ax
.y1:
    mov ax, [mc_topy]
    cmp dx, ax
    jge .y2
    mov dx, ax
.y2:
    mov [mc_chx], cx
    mov [mc_chy], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_do_fire - spend one queued click
; in:  [mc_fire] counted up by the UI task; preserves all registers
; -----------------------------------------------------------------------------
mc_do_fire:
    push ax
    push bx
    push cx
    push dx
    cmp word [mc_fire], 0
    je .out
    dec word [mc_fire]
    mov al, [mc_fireb]
    cmp al, 0FFh
    jne .named
    call mc_nearest                 ; AL = the nearest base with missiles, or
    cmp al, 0FFh                    ; 0FFh if every one of them is out
    je .out
.named:
    mov bl, al
    mov bh, 0
    cmp bx, MC_NBASE
    jae .out
    cmp byte [mc_balive + bx], 0
    je .out
    cmp byte [mc_bmis + bx], 0
    je .empty
    mov cx, [mc_firex]
    mov dx, [mc_firey]
    call mc_launch_abm
    jmp short .out
.empty:
    mov ax, 180                     ; the dry click: a base with no missiles
    mov cx, 2
    call mc_beep_n
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_nearest - which base should answer a click?
; in:  [mc_firex]; out: AL = base index, or 0FFh if none can fire
; preserves every other register
;
; The arcade has three fire buttons and the player picks; a mouse has one, so
; the nearest live base with missiles left answers. Distance is measured in x
; alone - all three stand on the same row.
; -----------------------------------------------------------------------------
mc_nearest:
    push bx
    push cx
    push dx
    push si
    mov al, 0FFh
    mov cx, 32767                   ; CX = best distance so far
    xor si, si
.each:
    cmp byte [mc_balive + si], 0
    je .next
    cmp byte [mc_bmis + si], 0
    je .next
    mov bx, si
    add bx, bx
    mov dx, [mc_basex + bx]
    sub dx, [mc_firex]
    jns .abs
    neg dx
.abs:
    cmp dx, cx
    jae .next
    mov cx, dx
    mov ax, si                      ; AL = this base's index
.next:
    inc si
    cmp si, MC_NBASE
    jb .each
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mc_launch_abm - one ABM from base BL toward (CX,DX)
; in:  BX = base index, CX/DX = target in content coords
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mc_launch_abm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si                      ; a free slot?
.slot:
    cmp byte [mc_aa + si], 0
    je .free
    inc si
    cmp si, MC_MAXABM
    jb .slot
    jmp .out
.free:
    push bx
    add bx, bx
    mov ax, [mc_basex + bx]         ; the launcher: the top of the base mound
    pop bx
    mov [mc_aimsx], ax
    push ax
    mov ax, [mc_groundy]
    sub ax, [mc_objh]
    mov [mc_aimsy], ax
    pop ax
    mov [mc_aimtx], cx
    mov [mc_aimty], dx
    mov word [mc_aimspd], MC_ABMSPD
    call mc_aim                     ; -> [mc_aimvx]/[mc_aimvy]/[mc_aimn]

    mov di, si
    add di, di
    mov ax, [mc_aimsx]
    mov [mc_apx + di], ax           ; where the trail starts, and where it has
    mov [mc_asx + di], ax           ; been drawn to so far - the same point on
    push cx                         ; the frame it is launched
    mov cl, MC_FP
    shl ax, cl
    pop cx
    mov [mc_ax16 + di], ax
    mov ax, [mc_aimsy]
    mov [mc_apy + di], ax
    mov [mc_asy + di], ax
    push cx
    mov cl, MC_FP
    shl ax, cl
    pop cx
    mov [mc_ay16 + di], ax
    mov ax, [mc_aimvx]
    mov [mc_avx + di], ax
    mov ax, [mc_aimvy]
    mov [mc_avy + di], ax
    mov ax, [mc_aimn]
    mov [mc_astep + di], ax
    mov [mc_atx + di], cx           ; ...and the trail's far end, fixed here
    mov [mc_aty + di], dx
    mov word [mc_adrw + di], 0
    mov byte [mc_aarm + si], 0
    mov byte [mc_aa + si], 1

    dec byte [mc_bmis + bx]         ; the base is one missile lighter, and its
    call mc_bdirty_i                ; pyramid has to lose a dot - THAT pyramid
    mov ax, 1400
    call mc_beep
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_aim - velocity and step count from ([mc_aimsx],[mc_aimsy]) to
;          ([mc_aimtx],[mc_aimty]) at [mc_aimspd] sixteenths of a pixel a frame
; out: [mc_aimvx], [mc_aimvy] (signed, sixteenths), [mc_aimn] (frames)
; preserves all registers
;
; The step count is the authority, not a proximity test: a missile detonates
; when its counter reaches zero, so it arrives exactly on its target and can
; never overshoot or orbit it. Distance is the octagonal approximation
; max + min/2 - within about 8% of the true length, which changes a flight time
; and nothing else.
; -----------------------------------------------------------------------------
mc_aim:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mc_aimtx]
    sub ax, [mc_aimsx]
    mov [mc_aimdx], ax
    mov bx, ax
    or bx, bx
    jns .adx
    neg bx
.adx:
    mov ax, [mc_aimty]
    sub ax, [mc_aimsy]
    mov [mc_aimdy], ax
    mov cx, ax
    or cx, cx
    jns .ady
    neg cx
.ady:
    mov ax, bx                      ; AX = max, DX = min
    mov dx, cx
    cmp bx, cx
    jae .have
    mov ax, cx
    mov dx, bx
.have:
    shr dx, 1
    add ax, dx                      ; the octagonal distance
    or ax, ax
    jnz .dist
    mov ax, 1
.dist:
    mov cl, MC_FP                   ; steps = dist * 16 / speed
    shl ax, cl
    xor dx, dx
    div word [mc_aimspd]
    or ax, ax
    jnz .n
    mov ax, 1
.n:
    mov [mc_aimn], ax

    mov ax, [mc_aimdx]              ; vx = dx * 16 / steps, signed
    mov cl, MC_FP
    shl ax, cl
    cwd
    idiv word [mc_aimn]
    mov [mc_aimvx], ax
    mov ax, [mc_aimdy]
    mov cl, MC_FP
    shl ax, cl
    cwd
    idiv word [mc_aimn]
    mov [mc_aimvy], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_do_launch - put ICBMs into the sky
; preserves all registers
;
; The arcade launches while fewer than MXICON are on screen and the highest is
; below LAUHGT; here it is a countdown that shortens with the wave, gated on
; the same on-screen ceiling. A wave's ICBMs are a fixed budget ([mc_icbleft])
; and everything that consumes one - a salvo, a MIRV split, a satellite's drop
; - takes it from the same word, which is what makes a wave end.
; -----------------------------------------------------------------------------
mc_do_launch:
    push ax
    push bx
    push cx
    push dx
    cmp word [mc_icbleft], 0
    jle .out
    dec word [mc_ltick]
    cmp word [mc_ltick], 0
    jg .out
    mov al, [mc_wave]               ; the interval shortens with the wave, but
    mov ah, 0                       ; never below eight frames
    mov bx, 40
    sub bx, ax
    sub bx, ax                      ; 40 - 2 * wave
    cmp bx, 8
    jge .iv
    mov bx, 8
.iv:                                ; At wave 1 that is 38 frames a salvo, so
                                    ; ICBWAV's twelve arrive over about twenty
                                    ; seconds rather than eight - the arcade
                                    ; paces a wave by waiting for the previous
                                    ; salvo to descend past LAUHGT, and this
                                    ; plus the MXICON ceiling is the same
                                    ; pressure without the second test
    mov [mc_ltick], bx
    mov cx, 2                       ; one or two a salvo
    call mc_rand_mod                ; AX = 0..1
    inc ax
    mov cx, ax
.salvo:
    cmp word [mc_icbleft], 0
    jle .out
    cmp byte [mc_onscr], MC_MAXON
    jae .out
    call mc_launch_icbm
    loop .salvo
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_launch_icbm - one ICBM from the top edge at a live target
; out: nothing; preserves all registers
;
; Smart bombs come out of the same budget and the same slots - they differ in
; [mc_ia] (2 rather than 1), in being slower, and in dodging (mc_sb_dodge).
; -----------------------------------------------------------------------------
mc_launch_icbm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mc_freeslot                ; SI = a free ICBM slot, CF=1 if none
    jc .out                         ; ...which is transient: try again next tick
    call mc_pick_target             ; AL = target, CF=1 nothing left alive
    jnc .aim
    mov word [mc_icbleft], 0        ; NOTHING LEFT TO AIM AT, and this is the
    jmp .out                        ; one refusal that is not transient: the
                                    ; last city and the last base are gone and
                                    ; no later frame can change that. Leaving
                                    ; the budget standing hangs the game -
                                    ; mc_check_wave waits on it, so the wave
                                    ; never ends, mc_nextwave never runs and
                                    ; the THE END it would have reached never
                                    ; arrives. Spending it ends the wave on the
                                    ; next frame and the game with it
.aim:
    mov [mc_itgt + si], al
    call mc_tgt_pos                 ; CX/DX = where that target stands
    mov [mc_aimtx], cx
    mov [mc_aimty], dx

    mov cx, [mc_cw]                 ; ...from a random column along the top
    call mc_rand_mod
    mov [mc_aimsx], ax
    mov ax, [mc_topy]
    mov [mc_aimsy], ax

    mov al, 1                       ; a smart bomb, if this wave still owes one
    mov bx, [mc_icbspd]
    cmp word [mc_smleft], 0
    jle .kind
    mov cx, 4                       ; ...one launch in four
    call mc_rand_mod
    or ax, ax
    jnz .k1
    dec word [mc_smleft]
    mov al, 2
    mov bx, [mc_icbspd]             ; slower than an ICBM: it is meant to be
    shr bx, 1                       ; shot down, and it dodges while you try
    add bx, 8
    jmp short .kind
.k1:
    mov al, 1
.kind:
    mov [mc_ia + si], al
    mov [mc_aimspd], bx
    call mc_aim

    mov di, si
    add di, di
    mov ax, [mc_aimsx]
    mov [mc_ipx + di], ax
    mov [mc_isx + di], ax
    mov cl, MC_FP
    shl ax, cl
    mov [mc_ix16 + di], ax
    mov ax, [mc_aimsy]
    mov [mc_ipy + di], ax
    mov [mc_isy + di], ax
    mov cl, MC_FP
    shl ax, cl
    mov [mc_iy16 + di], ax
    call mc_itrail                  ; the trail's line is fixed HERE, and only
                                    ; here: a dodge re-aims the bomb, not it
    mov ax, [mc_aimvx]
    mov [mc_ivx + di], ax
    mov ax, [mc_aimvy]
    mov [mc_ivy + di], ax
    mov ax, [mc_aimn]
    mov [mc_istep + di], ax

    mov byte [mc_imirv + si], 0     ; MIRVs from wave MIRVWV = 1, one launch in
    cmp byte [mc_ia + si], 2        ; five, and never from a smart bomb
    je .nomirv
    mov cx, 5
    call mc_rand_mod
    or ax, ax
    jnz .nomirv
    mov ax, [mc_aimn]               ; ...splitting somewhere in the middle
    shr ax, 1                       ; third of its own flight
    mov [mc_imirv2 + di], ax
    mov byte [mc_imirv + si], 1
.nomirv:
    dec word [mc_icbleft]
    inc byte [mc_onscr]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_freeslot - the first unused ICBM slot
; out: CF=0 and SI = the slot; CF=1 all eight are busy
; preserves every other register
; -----------------------------------------------------------------------------
mc_freeslot:
    xor si, si
.each:
    cmp byte [mc_ia + si], 0
    je .got
    inc si
    cmp si, MC_MAXICBM
    jb .each
    stc
    ret
.got:
    clc
    ret

; -----------------------------------------------------------------------------
; mc_pick_target - a living city or base for an ICBM to aim at
; out: CF=0 and AL = 0..5 city / 6..8 base; CF=1 nothing is left standing
; preserves every other register
; -----------------------------------------------------------------------------
mc_pick_target:
    push bx
    push cx
    push dx
    push si
    xor cx, cx                      ; count what is alive
    xor si, si
.count:
    cmp byte [mc_calive + si], 0
    je .c1
    inc cx
.c1:
    inc si
    cmp si, MC_NCITY
    jb .count
    xor si, si
.countb:
    cmp byte [mc_balive + si], 0
    je .c2
    inc cx
.c2:
    inc si
    cmp si, MC_NBASE
    jb .countb
    jcxz .none

    call mc_rand_mod                ; the Nth living target, left to right
    mov bx, ax
    xor si, si
.walk:
    cmp si, MC_NCITY
    jae .wb
    cmp byte [mc_calive + si], 0
    je .wnext
    jmp short .wtest
.wb:
    mov dx, si
    sub dx, MC_NCITY
    push si
    mov si, dx
    cmp byte [mc_balive + si], 0
    pop si
    je .wnext
.wtest:
    or bx, bx
    jz .got
    dec bx
.wnext:
    inc si
    cmp si, MC_NTGT
    jb .walk
.none:
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
.got:
    mov ax, si
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; mc_tgt_pos - where a target index stands
; in:  AL = 0..5 city / 6..8 base; out: CX = x, DX = y (content coords)
; preserves every other register
; -----------------------------------------------------------------------------
mc_tgt_pos:
    push ax
    push bx
    mov bl, al
    mov bh, 0
    mov dx, [mc_groundy]
    cmp bx, MC_NCITY
    jae .base
    add bx, bx
    mov cx, [mc_cityx + bx]
    jmp short .out
.base:
    sub bx, MC_NCITY
    add bx, bx
    mov cx, [mc_basex + bx]
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_move_icbm - fly every ICBM one frame
; preserves all registers
;
; A slot that dies becomes 0FFh - "alive no longer, but its trail is still on
; the screen". mc_render erases it and clears the slot. That split is what lets
; the update run lock-free while the drawing stays inside one lock hold, and it
; means an erase is always aimed at where the trail was DRAWN rather than where
; the update last moved it (SPEC.md 44.4's rule).
; -----------------------------------------------------------------------------
mc_move_icbm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    mov al, [mc_ia + si]
    cmp al, 1
    jb .next
    cmp al, 2
    ja .next
    mov di, si
    add di, di

    cmp byte [mc_ia + si], 2        ; a smart bomb looks for something to dodge
    jne .fly
    call mc_sb_dodge
.fly:
    mov ax, [mc_ix16 + di]
    add ax, [mc_ivx + di]
    mov [mc_ix16 + di], ax
    mov ax, [mc_iy16 + di]
    add ax, [mc_ivy + di]
    mov [mc_iy16 + di], ax

    cmp byte [mc_imirv + si], 0     ; time to split?
    je .step
    dec word [mc_imirv2 + di]
    cmp word [mc_imirv2 + di], 0
    jg .step
    mov byte [mc_imirv + si], 0
    call mc_mirv
.step:
    dec word [mc_istep + di]
    cmp word [mc_istep + di], 0
    jg .next
    cmp byte [mc_ia + si], 2
    je .sbend
    call mc_icbm_land
    jmp short .next
.sbend:
    call mc_sb_arrive
.next:
    inc si
    cmp si, MC_MAXICBM
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_icbm_land - an ICBM reached what it was aimed at
; in:  SI = slot, DI = SI*2; preserves all registers
; -----------------------------------------------------------------------------
mc_icbm_land:
    push ax
    push bx
    push cx
    push dx
    call mc_ipix                    ; CX/DX = where it is, in pixels
    mov [mc_expx], cx
    mov [mc_expy], dx
    call mc_add_exp                 ; the ground burst
    mov al, [mc_itgt + si]
    call mc_destroy
    mov byte [mc_ia + si], 0FFh     ; its trail is still on the screen
    mov ax, 90                      ; the low thud of something being lost
    mov cx, 5
    call mc_beep_n
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_destroy - a city or a base is gone (W3MAIN's DESTROY)
; in:  AL = target index; preserves all registers
;
; A city takes one off [mc_lives], the entitlement the next wave regenerates
; from - which is the arcade's PLIVES exactly, and why a bonus city is simply
; an increment of it.
; -----------------------------------------------------------------------------
mc_destroy:
    push ax
    push bx
    mov bl, al
    mov bh, 0
    cmp bx, MC_NCITY
    jae .base
    cmp byte [mc_calive + bx], 0
    je .out
    mov byte [mc_calive + bx], 0
    cmp word [mc_lives], 0
    jle .cdirty
    dec word [mc_lives]
.cdirty:
    push di                         ; the city's own span, not the whole band
    mov di, bx
    add di, di
    mov ax, [mc_cityx + di]
    mov cx, [mc_citw]
    shr cx, 1
    inc cx
    push ax
    sub ax, cx
    add cx, cx
    add cx, ax
    call mc_gdmg
    pop ax
    pop di
    jmp short .out
.base:
    sub bx, MC_NCITY
    cmp byte [mc_balive + bx], 0
    je .out
    mov byte [mc_balive + bx], 0
    mov byte [mc_bmis + bx], 0      ; and everything still in its magazine
    push di
    mov di, bx
    add di, di
    mov ax, [mc_basex + di]
    mov cx, [mc_basw]
    shr cx, 1
    inc cx
    push ax
    sub ax, cx
    add cx, cx
    add cx, ax
    call mc_gdmg
    pop ax
    pop di
    mov cl, bl                      ; ...and that launcher alone
    mov al, 1
    shl al, cl
    or [mc_bdirty], al
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_mirv - one ICBM becomes three (W3MAIN's MIRV, wave MIRVWV = 1 onward)
; in:  SI = the splitting slot, DI = SI*2; preserves all registers
;
; The children come out of the same wave budget, so a MIRV does not add ICBMs
; to a wave - it delivers them all at once, lower down, which is the whole
; point of the weapon.
; -----------------------------------------------------------------------------
mc_mirv:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mc_ipix
    mov [mc_aimsx], cx
    mov [mc_aimsy], dx
    mov cx, 2                       ; two children, budget permitting
.child:
    push cx
    cmp word [mc_icbleft], 0
    jle .done
    cmp byte [mc_onscr], MC_MAXON
    jae .done
    call mc_freeslot                ; SI moves to the child's slot
    jc .done
    call mc_pick_target
    jc .done
    mov [mc_itgt + si], al
    call mc_tgt_pos
    mov [mc_aimtx], cx
    mov [mc_aimty], dx
    mov ax, [mc_icbspd]
    mov [mc_aimspd], ax
    call mc_aim
    call mc_spawn_child
    dec word [mc_icbleft]
    inc byte [mc_onscr]
.done:
    pop cx
    loop .child
    mov ax, 700
    call mc_beep
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_spawn_child - fill ICBM slot SI from the mc_aim* block
; in:  SI = slot; preserves all registers
; -----------------------------------------------------------------------------
mc_spawn_child:
    push ax
    push cx
    push di
    mov di, si
    add di, di
    mov ax, [mc_aimsx]
    mov [mc_ipx + di], ax
    mov [mc_isx + di], ax
    mov cl, MC_FP
    shl ax, cl
    mov [mc_ix16 + di], ax
    mov ax, [mc_aimsy]
    mov [mc_ipy + di], ax
    mov [mc_isy + di], ax
    mov cl, MC_FP
    shl ax, cl
    mov [mc_iy16 + di], ax
    call mc_itrail
    mov ax, [mc_aimvx]
    mov [mc_ivx + di], ax
    mov ax, [mc_aimvy]
    mov [mc_ivy + di], ax
    mov ax, [mc_aimn]
    mov [mc_istep + di], ax
    mov byte [mc_imirv + si], 0
    mov byte [mc_ia + si], 1
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_itrail - arm ICBM slot SI's trail from the mc_aim* block
; in:  SI = slot, DI = slot*2; preserves all registers
;
; The walk itself is laid on the next frame that DRAWS, not here: mc_update is
; lock-free, and although OSAPI_GFX_LINIT draws nothing, a walk laid before
; mc_track has settled the origin would hold the wrong screen coordinates.
; -----------------------------------------------------------------------------
mc_itrail:
    push ax
    mov ax, [mc_aimtx]
    mov [mc_iex + di], ax
    mov ax, [mc_aimty]
    mov [mc_iey + di], ax
    mov word [mc_idrw + di], 0
    mov byte [mc_iarm + si], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_ipix - an ICBM's position in whole pixels
; in:  DI = slot*2; out: CX = x, DX = y; preserves every other register
; -----------------------------------------------------------------------------
mc_ipix:
    push ax
    mov ax, [mc_ix16 + di]
    sar ax, 1                       ; four single-bit shifts, because an 8086
    sar ax, 1                       ; takes no immediate count but 1 - and
    sar ax, 1                       ; ARITHMETIC, because a MIRV child or a
    sar ax, 1                       ; dodging bomb can be off the left edge
    mov cx, ax
    mov ax, [mc_iy16 + di]
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov dx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_apix - the same, for an ABM
; in:  DI = slot*2; out: CX = x, DX = y; preserves every other register
; -----------------------------------------------------------------------------
mc_apix:
    push ax
    mov ax, [mc_ax16 + di]
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov cx, ax
    mov ax, [mc_ay16 + di]
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov dx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_sb_dodge - a smart bomb steers around a live explosion (W3MAIN's cruise
;               missile, wave 6 onward via CRMWAV)
; in:  SI = slot, DI = slot*2; preserves all registers
;
; It re-aims at a point displaced sideways from its target, rather than
; reversing: reversing makes it hover, and a bomb that never arrives is not a
; threat. The displacement decays because the re-aim is only good for a few
; frames - mc_sb_arrive puts it back on course afterwards.
; -----------------------------------------------------------------------------
MC_SBSEE    equ 14                  ; how far outside an explosion it reacts

mc_sb_dodge:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mc_sbslot], si             ; the bomb's slot, because SI is about to
                                    ; become the explosion loop's index
    call mc_ipix                    ; CX/DX = the bomb, from the caller's DI
    mov [mc_sbx], cx
    mov [mc_sby], dx
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    mov di, si
    add di, di
    mov ax, [mc_ex + di]            ; |dx| within reacting distance?
    sub ax, [mc_sbx]
    jns .ax
    neg ax
.ax:
    mov [mc_sbd], ax
    mov bl, [mc_et + si]            ; the explosion's live radius, plus the
    mov bh, 0                       ; margin the bomb reacts at
    mov bl, [mc_erad + bx]
    mov bh, 0
    add bx, MC_SBSEE
    cmp [mc_sbd], bx
    ja .next
    mov ax, [mc_ey + di]            ; ...and |dy|?
    sub ax, [mc_sby]
    jns .ay
    neg ax
.ay:
    cmp ax, bx
    jbe .swerve
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    jmp short .out
.swerve:
    mov si, [mc_sbslot]             ; back to the bomb
    mov di, si
    add di, di
    mov al, [mc_itgt + si]
    call mc_tgt_pos                 ; CX/DX = its target
    mov [mc_aimtx], cx
    mov [mc_aimty], dx
    mov ax, [mc_sbx]
    mov [mc_aimsx], ax
    mov ax, [mc_sby]
    mov [mc_aimsy], ax
    mov ax, cx                      ; ...displaced well to one side, AWAY from
    sub ax, [mc_sbx]                ; where it is now, so the detour is a
    jns .right                      ; detour and not a stall
    add word [mc_aimtx], 48
    jmp short .go
.right:
    sub word [mc_aimtx], 48
.go:
    mov ax, [mc_icbspd]
    shr ax, 1
    add ax, 8
    mov [mc_aimspd], ax
    call mc_aim
    mov ax, [mc_aimvx]
    mov [mc_ivx + di], ax
    mov ax, [mc_aimvy]
    mov [mc_ivy + di], ax
    mov word [mc_istep + di], 6     ; good for six frames; then mc_sb_arrive
.out:                               ; puts it back on course
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_sb_arrive - a smart bomb's step counter ran out
; in:  SI = slot, DI = slot*2; preserves all registers
;
; Unlike an ICBM, zero steps does not mean "arrived" - a dodge sets the counter
; to six on purpose. If it is genuinely on top of its target it lands; if not,
; it re-aims and keeps coming.
; -----------------------------------------------------------------------------
mc_sb_arrive:
    push ax
    push bx
    push cx
    push dx
    mov al, [mc_itgt + si]
    call mc_tgt_pos                 ; CX/DX = the target
    mov [mc_aimtx], cx
    mov [mc_aimty], dx
    push cx
    push dx
    call mc_ipix                    ; CX/DX = the bomb
    mov [mc_aimsx], cx
    mov [mc_aimsy], dx
    pop dx
    pop bx                          ; BX = target x
    mov ax, dx
    sub ax, [mc_aimsy]
    jns .ay
    neg ax
.ay:
    cmp ax, 6
    ja .again
    mov ax, bx
    sub ax, [mc_aimsx]
    jns .ax
    neg ax
.ax:
    cmp ax, 6
    ja .again
    call mc_icbm_land
    jmp short .out
.again:
    mov ax, [mc_icbspd]
    shr ax, 1
    add ax, 8
    mov [mc_aimspd], ax
    call mc_aim
    mov ax, [mc_aimvx]
    mov [mc_ivx + di], ax
    mov ax, [mc_aimvy]
    mov [mc_ivy + di], ax
    mov ax, [mc_aimn]
    mov [mc_istep + di], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_move_abm - fly every ABM one frame; arrival detonates it
; preserves all registers
; -----------------------------------------------------------------------------
mc_move_abm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    cmp byte [mc_aa + si], 1
    jne .next
    mov di, si
    add di, di
    mov ax, [mc_ax16 + di]
    add ax, [mc_avx + di]
    mov [mc_ax16 + di], ax
    mov ax, [mc_ay16 + di]
    add ax, [mc_avy + di]
    mov [mc_ay16 + di], ax
    dec word [mc_astep + di]
    cmp word [mc_astep + di], 0
    jg .next
    mov ax, [mc_atx + di]           ; detonate exactly where it was aimed
    mov [mc_expx], ax
    mov ax, [mc_aty + di]
    mov [mc_expy], ax
    call mc_add_exp
    mov byte [mc_aa + si], 0FFh     ; its trail still has to be erased
    mov ax, 2000
    call mc_beep
.next:
    inc si
    cmp si, MC_MAXABM
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_add_exp - a new explosion at ([mc_expx],[mc_expy])
; preserves all registers
; -----------------------------------------------------------------------------
mc_add_exp:
    push ax
    push di
    push si
    xor si, si
.slot:
    cmp byte [mc_ea + si], 0
    je .free
    inc si
    cmp si, MC_MAXEXP
    jb .slot
    jmp short .out                  ; sixteen at once is already more than
                                    ; MC_MAXICBM + MC_MAXABM can make; a
                                    ; seventeenth is simply not drawn
.free:
    mov di, si
    add di, di
    mov ax, [mc_expx]
    mov [mc_ex + di], ax
    mov ax, [mc_expy]
    mov [mc_ey + di], ax
    mov byte [mc_et + si], 0
    mov byte [mc_er + si], 0
    mov byte [mc_edef + si], 0
    mov byte [mc_ea + si], 1
.out:
    pop si
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_do_exp - age every explosion one frame
; preserves all registers
; -----------------------------------------------------------------------------
mc_do_exp:
    push ax
    push si
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    inc byte [mc_et + si]
    mov al, [mc_et + si]
    cmp al, [mc_expfr]              ; 27, or MC_EXPFR3 on the coarse ramp
    jb .next
    mov byte [mc_ea + si], 0FFh     ; done: mc_render owes it one erase
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_do_damage - what every live explosion is touching (W3MAIN's DAMAGE)
; preserves all registers
;
; An explosion below [mc_lowest] does nothing at all - LOWEST, and the reason a
; player cannot farm points off the ground. An ICBM caught this way explodes in
; turn, which is the chain reaction the whole game is played for.
; -----------------------------------------------------------------------------
mc_do_damage:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.exp:
    cmp byte [mc_ea + si], 1
    jne .nexte
    mov di, si
    add di, di
    mov ax, [mc_ey + di]
    cmp ax, [mc_lowest]             ; LOWEST: a burst on the deck does nothing
    jg .nexte                       ; and pays nothing
    mov [mc_dmgy], ax
    mov ax, [mc_ex + di]
    mov [mc_dmgx], ax
    mov bl, [mc_et + si]
    mov bh, 0
    mov bl, [mc_erad + bx]
    mov bh, 0
    add bx, 2                       ; the object's own half-size
    mov [mc_dmgr], bx
    call mc_dmg_icbm
    call mc_dmg_sat
.nexte:
    inc si
    cmp si, MC_MAXEXP
    jb .exp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_dmg_icbm - every ICBM inside the explosion in [mc_dmg*]
; preserves all registers
; -----------------------------------------------------------------------------
mc_dmg_icbm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    mov al, [mc_ia + si]
    cmp al, 1
    jb .next
    cmp al, 2
    ja .next
    mov di, si
    add di, di
    push si
    call mc_ipix                    ; CX/DX = the ICBM
    pop si
    call mc_inrange
    jc .next
    mov [mc_expx], cx               ; it explodes where it was caught
    mov [mc_expy], dx
    push si
    call mc_add_exp
    pop si
    mov ax, [mc_icbpts]             ; 25 x multiplier, or 125 for a smart bomb
    cmp byte [mc_ia + si], 2
    jne .pts
    mov bx, 5
    mul bx
.pts:
    call mc_score_add
    mov byte [mc_ia + si], 0FFh
    mov byte [mc_sdirty], 1
    mov ax, 2400
    call mc_beep
.next:
    inc si
    cmp si, MC_MAXICBM
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_inrange - is (CX,DX) inside the explosion in [mc_dmg*]?
; out: CF=0 inside, CF=1 outside; preserves every register
;
; The box test first is not an optimisation detail - it is what keeps the
; squared distance inside a word. dx and dy can be six hundred pixels apart, and
; 600 squared does not fit; inside the box neither exceeds thirty.
; -----------------------------------------------------------------------------
mc_inrange:
    push ax
    push bx
    push dx
    mov ax, cx
    sub ax, [mc_dmgx]
    jns .ax
    neg ax
.ax:
    cmp ax, [mc_dmgr]
    ja .no
    mov bx, ax
    mov ax, dx
    sub ax, [mc_dmgy]
    jns .ay
    neg ax
.ay:
    cmp ax, [mc_dmgr]
    ja .no
    mul ax                          ; dy*dy, at most 30*30
    mov dx, ax
    mov ax, bx
    mul ax                          ; dx*dx
    add ax, dx
    mov bx, [mc_dmgr]
    push ax
    mov ax, bx
    mul bx                          ; r*r
    mov bx, ax
    pop ax
    cmp ax, bx
    ja .no
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mc_dmg_sat - the satellite or bomber, if the explosion reaches it
; preserves all registers
; -----------------------------------------------------------------------------
mc_dmg_sat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [mc_sata], 1
    jb .out
    cmp byte [mc_sata], 2
    ja .out
    mov cx, [mc_satx]
    mov dx, [mc_saty]
    add word [mc_dmgr], 4           ; SPUTNIK/BOMBER: a bigger object than an
    call mc_inrange                 ; ICBM, so a bigger effective radius
    sub word [mc_dmgr], 4
    jc .out
    mov [mc_expx], cx
    mov [mc_expy], dx
    call mc_add_exp
    mov ax, [mc_icbpts]             ; SPUTKI: "4X ICBM"
    mov bx, 4
    mul bx
    call mc_score_add
    mov byte [mc_sata], 0FFh
    mov byte [mc_sdirty], 1
    mov ax, 2800
    mov cx, 4
    call mc_beep_n
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_do_sat - the satellite / bomber (W3MAIN's ACTPLA / PROPLA, SPUTWV = 2)
; preserves all registers
;
; It crosses the sky and drops ICBMs out of the same wave budget as everything
; else. Two shapes, one behaviour: which one is drawn is a coin toss at launch,
; exactly as the arcade's SOBJID is.
; -----------------------------------------------------------------------------
mc_do_sat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [mc_sata], 1
    jb .idle
    cmp byte [mc_sata], 2
    ja .idle

    mov ax, [mc_satx]               ; move it
    add ax, [mc_satvx]
    mov [mc_satx], ax
    cmp ax, -20
    jl .gone
    mov bx, [mc_cw]
    add bx, 20
    cmp ax, bx
    jg .gone

    dec word [mc_satfire]           ; ...and drop something now and then
    cmp word [mc_satfire], 0
    jg .out
    mov word [mc_satfire], 14
    cmp word [mc_icbleft], 0
    jle .out
    cmp byte [mc_onscr], MC_MAXON
    jae .out
    call mc_sat_drop
    jmp short .out
.gone:
    mov byte [mc_sata], 0FFh        ; off the edge: mc_render owes it an erase
    jmp short .out
.idle:
    cmp byte [mc_sata], 0
    jne .out
    mov al, [mc_wave]
    cmp al, MC_SPUTWV
    jb .out
    cmp word [mc_icbleft], 4        ; never with the wave nearly over: it would
    jl .out                         ; have nothing to drop
    dec word [mc_satcool]
    cmp word [mc_satcool], 0
    jg .out
    call mc_sat_launch
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_sat_launch:
    push ax
    push bx
    push cx
    push dx
    mov cx, 2
    call mc_rand_mod                ; satellite or bomber
    inc ax
    mov [mc_sata], al
    mov cx, 2
    call mc_rand_mod                ; ...from the left or from the right
    or ax, ax
    jz .left
    mov ax, [mc_cw]
    add ax, 12
    mov [mc_satx], ax
    mov word [mc_satvx], -MC_SATSPD
    jmp short .y
.left:
    mov word [mc_satx], -12
    mov word [mc_satvx], MC_SATSPD
.y:
    mov ax, [mc_groundy]            ; somewhere in the top third of the sky
    sub ax, [mc_topy]
    mov bx, 3
    xor dx, dx
    div bx
    mov cx, ax
    or cx, cx
    jnz .rnd
    mov cx, 1
.rnd:
    call mc_rand_mod
    add ax, [mc_topy]
    add ax, 6
    mov [mc_saty], ax
    mov ax, [mc_satx]               ; nothing is drawn yet, so "where it was
    mov [mc_satpx], ax              ; drawn" is where it starts
    mov word [mc_satfire], 8
    mov word [mc_satcool], 220
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_sat_drop:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mc_freeslot
    jc .out
    call mc_pick_target
    jc .out
    mov [mc_itgt + si], al
    call mc_tgt_pos
    mov [mc_aimtx], cx
    mov [mc_aimty], dx
    mov ax, [mc_satx]
    mov [mc_aimsx], ax
    mov ax, [mc_saty]
    add ax, 4
    mov [mc_aimsy], ax
    mov ax, [mc_icbspd]
    mov [mc_aimspd], ax
    call mc_aim
    call mc_spawn_child
    dec word [mc_icbleft]
    inc byte [mc_onscr]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Waves, scoring and the end-of-wave tally
; =============================================================================

; -----------------------------------------------------------------------------
; mc_check_wave - is the wave over?
; preserves all registers
;
; Over means: nothing left to launch, nothing in the sky, no satellite and no
; explosion still burning. The ABMs are allowed to finish - a shot in flight
; when the last ICBM dies still counts, and still costs a missile.
; -----------------------------------------------------------------------------
mc_check_wave:
    push ax
    push si
    cmp word [mc_icbleft], 0
    jg .out
    cmp byte [mc_onscr], 0
    jne .out
    cmp byte [mc_sata], 0
    jne .out
    xor si, si
.exp:
    cmp byte [mc_ea + si], 0
    jne .out
    inc si
    cmp si, MC_MAXEXP
    jb .exp
    xor si, si
.abm:
    cmp byte [mc_aa + si], 0
    jne .out
    inc si
    cmp si, MC_MAXABM
    jb .abm
    call mc_endwave
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_endwave - into the bonus tally (W3MAIN's ENDWV1)
; preserves all registers
; -----------------------------------------------------------------------------
mc_endwave:
    push ax
    mov byte [mc_mode], M_ENDA      ; SPEC.md 48.9.4: no repaint. The banner
    mov word [mc_hold], 6           ; is the only thing this changes and it
    mov byte [mc_tbase], MC_NBASE - 1   ; draws itself (48.9.3)
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_tally_abm - one unused ABM at a time, 5 x multiplier each (ENDWV2/ABMADD)
; preserves all registers
; -----------------------------------------------------------------------------
mc_tally_abm:
    push ax
    push bx
    push cx
    dec word [mc_hold]
    cmp word [mc_hold], 0
    jg .out
    mov word [mc_hold], 3
    mov bl, [mc_tbase]
.find:
    cmp bl, 0
    jl .done
    mov bh, 0
    cmp byte [mc_bmis + bx], 0
    jne .got
    dec bl
    jmp short .find
.got:
    mov bh, 0
    dec byte [mc_bmis + bx]
    mov [mc_tbase], bl
    mov ax, MC_ABMBON
    mov cl, [mc_mult]
    mov ch, 0
    mul cx
    call mc_score_add
    call mc_bdirty_i
    mov ax, 1760
    call mc_beep
    jmp short .out
.done:
    mov byte [mc_mode], M_ENDC      ; on to the cities. Nothing on screen
    mov word [mc_hold], 8           ; changes here at all - M_ENDA and M_ENDC
    mov byte [mc_tcity], 0          ; show the same banner
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_tally_city - one surviving city at a time, 100 x multiplier (ENDWV4)
; preserves all registers
; -----------------------------------------------------------------------------
mc_tally_city:
    push ax
    push bx
    push cx
    dec word [mc_hold]
    cmp word [mc_hold], 0
    jg .out
    mov word [mc_hold], 6
    mov bl, [mc_tcity]
.find:
    mov bh, 0
    cmp bx, MC_NCITY
    jae .done
    cmp byte [mc_calive + bx], 0
    jne .got
    inc bl
    jmp short .find
.got:
    inc bl
    mov [mc_tcity], bl
    mov ax, [mc_icbpts]             ; "4 ICBM POINTS/CITY" = 100 x multiplier
    mov cx, 4
    mul cx
    call mc_score_add
    mov ax, 1320
    call mc_beep
    jmp short .out
.done:
    call mc_nextwave
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_nextwave - regenerate, advance and pause before the next wave (ENDWV5)
; preserves all registers
;
; [mc_lives] is the arcade's PLIVES: the number of cities the player is
; entitled to, decremented when one is lost and incremented by a bonus. REGEN
; brings the living cities back up to it, capped at six - which is why a bonus
; city awarded mid-wave appears at the start of the next one and not before.
; -----------------------------------------------------------------------------
mc_nextwave:
    push ax
    push bx
    push cx
    push si
    cmp word [mc_lives], 0
    jg .alive
    mov byte [mc_mode], M_OVER      ; every city gone: THE END
    mov byte [mc_full], 1
    mov ax, 110
    mov cx, 18
    call mc_beep_n
    call mc_hiscore
    jmp short .out
.alive:
    xor cx, cx                      ; how many are standing?
    xor si, si
.count:
    cmp byte [mc_calive + si], 0
    je .c1
    inc cx
.c1:
    inc si
    cmp si, MC_NCITY
    jb .count
.regen:
    cmp cx, [mc_lives]
    jae .done
    cmp cx, MC_NCITY
    jae .done
    xor si, si
.find:
    cmp byte [mc_calive + si], 0
    je .give
    inc si
    cmp si, MC_NCITY
    jb .find
    jmp short .done
.give:
    mov byte [mc_calive + si], 1    ; a city came back: that is terrain
    push cx                         ; damage, not a screen (SPEC.md 48.9)
    push di
    mov di, si
    add di, di
    mov ax, [mc_cityx + di]
    mov cx, [mc_citw]
    shr cx, 1
    inc cx
    push ax
    sub ax, cx
    add cx, cx
    add cx, ax
    call mc_gdmg
    pop ax
    pop di
    pop cx
    inc cx
    jmp short .regen
.done:
    inc byte [mc_wave]
    mov byte [mc_mode], M_READY
    mov word [mc_hold], 22
    mov byte [mc_sdirty], 1         ; the wave counter in the strip moved
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_startwave - the wave itself (W3MAIN's NEWWV1 + SETICS)
; preserves all registers
; -----------------------------------------------------------------------------
mc_startwave:
    push ax
    push bx
    push cx
    push dx
    push si

    mov bl, [mc_wave]               ; the ICBM budget: ICBWAV, clamped to the
    mov bh, 0                       ; end of the table exactly as the arcade
    cmp bx, MC_NWAVE                ; clamps it
    jbe .w1
    mov bx, MC_NWAVE
.w1:
    dec bx
    mov al, [mc_icbwav + bx]
    mov ah, 0
    mov [mc_icbleft], ax
    mov al, [mc_crmwav + bx]        ; ...and the smart bombs: CRMWAV
    mov ah, 0
    mov [mc_smleft], ax

    mov bl, [mc_wave]               ; SMULTI = min(MAXMUL, (wave + 1) / 2)
    mov bh, 0
    inc bx
    shr bx, 1
    cmp bx, MC_MAXMUL
    jbe .m1
    mov bx, MC_MAXMUL
.m1:
    or bx, bx
    jnz .m2
    mov bx, 1
.m2:
    mov [mc_mult], bl
    mov ax, 25                      ; 25 points an ICBM, times the multiplier
    mul bx
    mov [mc_icbpts], ax

    mov bl, [mc_wave]               ; the speed ladder
    mov bh, 0
    cmp bx, MC_NWAVE
    jbe .s1
    mov bx, MC_NWAVE
.s1:
    dec bx
    mov al, [mc_spdwav + bx]
    mov ah, 0
    mov [mc_icbspd], ax

    mov bl, [mc_wave]               ; the palette: one per two waves, cycled
    mov bh, 0                       ; (SETCOL)
    dec bx
    shr bx, 1
.pmod:
    cmp bx, 10
    jb .pok
    sub bx, 10
    jmp short .pmod
.pok:
    add bx, bx
    add bx, bx
    mov al, [mc_pal + bx]
    mov [mc_cgnd], al
    mov al, [mc_pal + bx + 1]
    mov [mc_cicbm], al
    mov al, [mc_pal + bx + 2]
    mov [mc_ccity], al
    mov al, [mc_pal + bx + 3]
    mov [mc_cabm], al
    cmp byte [mc_mono], 0           ; SPEC.md 48.21: a TRAIL is one pixel
    je .palok                       ; wide, and a dither-class colour keeps
    mov byte [mc_cicbm], CWHITE     ; every other pixel of it. mc_pal puts
    mov byte [mc_cabm], CWHITE      ; exactly one of the two trails in that
.palok:                             ; class in every palette, on purpose -
                                    ; and on 1bpp that is not a second ink,
                                    ; it is half a line

    ; ALL THREE BASES COME BACK, with ten missiles each. That is NEWWV1
    ; verbatim - it writes MAXMIS into every NMMISB and 0E0 into MBLEFT, "ALL
    ; 3 BASES ALIVE" - and it is what makes the game winnable: only CITIES are
    ; permanent losses (they are what [mc_lives] counts), and a player who
    ; lost all three launchers to one bad wave would otherwise have nothing to
    ; defend with ever again. Leaving them dead read as a death spiral in
    ; testing: wave 2 opened with no bases and no way to fire.
    xor si, si
.base:
    mov byte [mc_balive + si], 1
    mov byte [mc_bmis + si], MC_MAXMIS
    inc si
    cmp si, MC_NBASE
    jb .base

    call mc_clear_objects
    mov word [mc_ltick], 12
    mov word [mc_satcool], 90
    mov byte [mc_mode], M_PLAY
    mov byte [mc_full], 1           ; THE sweep, and the only one a surviving
                                    ; wave gets (SPEC.md 48.9.4). It has to be
                                    ; HERE and not earlier: mc_clear_objects
                                    ; above clears object STATE and erases
                                    ; nothing, so an ABM frozen in flight when
                                    ; the wave ended - the modes between do not
                                    ; move or draw one - leaves its trail on
                                    ; screen with nothing that knows to wipe
                                    ; it. A repaint before the clear would
                                    ; faithfully redraw those trails and the
                                    ; clear would then strand them
    mov ax, 523
    mov cx, 4
    call mc_beep_n
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_clear_objects - nothing in flight, nothing burning
; preserves all registers
; -----------------------------------------------------------------------------
mc_clear_objects:
    push si
    xor si, si
.i:
    mov byte [mc_ia + si], 0
    mov byte [mc_imirv + si], 0
    inc si
    cmp si, MC_MAXICBM
    jb .i
    xor si, si
.a:
    mov byte [mc_aa + si], 0
    inc si
    cmp si, MC_MAXABM
    jb .a
    xor si, si
.e:
    mov byte [mc_ea + si], 0
    inc si
    cmp si, MC_MAXEXP
    jb .e
    mov byte [mc_sata], 0
    mov byte [mc_onscr], 0
    mov word [mc_fire], 0
    pop si
    ret

; -----------------------------------------------------------------------------
; mc_newgame - a whole new game
; preserves all registers
; -----------------------------------------------------------------------------
mc_newgame:
    push ax
    push si
    mov word [mc_scorelo], 0
    mov word [mc_scorehi], 0
    mov word [mc_bnlo], MC_BONSTEP
    mov word [mc_bnhi], 0
    mov word [mc_lives], MC_NCITY
    mov byte [mc_wave], 1
    xor si, si
.city:
    mov byte [mc_calive + si], 1
    inc si
    cmp si, MC_NCITY
    jb .city
    xor si, si
.base:
    mov byte [mc_balive + si], 1
    mov byte [mc_bmis + si], MC_MAXMIS
    inc si
    cmp si, MC_NBASE
    jb .base
    call mc_clear_objects
    mov byte [mc_mode], M_READY
    mov word [mc_hold], 16
    mov byte [mc_full], 1
    mov byte [mc_chshown], 0
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_score_add - AX points onto the 32-bit score, and the bonus city it may buy
; preserves all registers
;
; The score is a dword because Missile Command scores run past 65,535 in a
; competent game - the arcade keeps six BCD digits for the same reason.
; -----------------------------------------------------------------------------
mc_score_add:
    push ax
    push dx
    add [mc_scorelo], ax
    adc word [mc_scorehi], 0
    mov byte [mc_sdirty], 1
.bonus:
    mov ax, [mc_scorehi]
    cmp ax, [mc_bnhi]
    ja .earn
    jb .out
    mov ax, [mc_scorelo]
    cmp ax, [mc_bnlo]
    jb .out
.earn:
    cmp word [mc_lives], 12         ; a ceiling, because PLIVES is what REGEN
    jae .step                       ; walks and six cities is all the ground
    inc word [mc_lives]             ; there is
.step:
    add word [mc_bnlo], MC_BONSTEP
    adc word [mc_bnhi], 0
    mov ax, 1046
    push cx
    mov cx, 8
    call mc_beep_n
    pop cx
    jmp short .bonus
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_hiscore - keep the session's best
; preserves all registers
; -----------------------------------------------------------------------------
mc_hiscore:
    push ax
    mov ax, [mc_scorehi]
    cmp ax, [mc_hihi]
    ja .set
    jb .out
    mov ax, [mc_scorelo]
    cmp ax, [mc_hilo]
    jbe .out
.set:
    mov ax, [mc_scorelo]
    mov [mc_hilo], ax
    mov ax, [mc_scorehi]
    mov [mc_hihi], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_rand_mod - a pseudo-random value in 0..CX-1
; in:  CX = modulus (non-zero); out: AX = the value; preserves every other
;      register
; -----------------------------------------------------------------------------
mc_rand_mod:
    push dx
    call OSAPI_RAND
    xor dx, dx
    div cx
    mov ax, dx
    pop dx
    ret

; -----------------------------------------------------------------------------
; mc_beep / mc_beep_n - a tone, from the worker
; mc_beep:   AX = Hz, 2 ticks; mc_beep_n: AX = Hz, CX = ticks
; both preserve every register
;
; Worker-safe by construction (SPEC.md 34.3/44.5): outside a dispatched
; callback snd_req_inst stamps the grant with the running task's own instance,
; which is ours, so the tone is released at teardown like any other. Every one
; is duration-limited, so snd_tick turns it off and the worker never has to.
; A refusal is ignored: sound is decoration, and a game that stalled for it
; would be worse than a quiet one.
; -----------------------------------------------------------------------------
mc_beep:
    push cx
    mov cx, 2
    call mc_beep_n
    pop cx
    ret

mc_beep_n:
    push ax
    push cx
    push dx
    mov dl, 0x40                    ; the package default priority
    call OSAPI_SND_TONE
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; Drawing
; =============================================================================

; -----------------------------------------------------------------------------
; mc_render - the worker's one lock hold a frame
; preserves all registers
;
; Rule 5 of SPEC.md 20.6: windows move and get buried while we sleep, and the
; gfx_* primitives take absolute screen coordinates, so the origin is re-read
; and the clip armed before anything is drawn. CF=1 from wm_clip_set means not
; one pixel of our content shows - the frame is skipped and the game keeps
; running invisibly, which is what the kernel's own Bounce does.
;
; A skipped frame raises [mc_full], so the next frame that draws draws
; everything (SPEC.md 44.1's argument: the update ran and the screen did not
; follow, and every erase in this file is aimed at where something was last
; DRAWN).
; -----------------------------------------------------------------------------
mc_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_GFX_LOCK
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [mc_win]
    test word [es:bx + W_FLAGS], 2  ; still visible?
    jz .skip
    mov si, bx
    call mc_track                   ; the window may have been dragged
    mov bx, [mc_win]
    call OSAPI_WM_CLIP_SET
    jc .skip
    call mc_rbody
    jmp short .unlock
.skip:
    mov byte [mc_full], 1
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
; mc_rbody - one frame's drawing, the surface not asked about. Two callers:
; mc_render above (lock held, clip armed, the windowed worker) and
; mc_fsx_main's loop (an fsx bracket, where the lock is the caller's for the
; whole session and every primitive is a Mode X twin).
; in:  origin tracked; preserves all registers
; -----------------------------------------------------------------------------
mc_rbody:
    push ax
    cmp byte [mc_abon], 0           ; the credits own the content until a click
    jne .out                        ; or a key takes them down
    cmp byte [mc_full], 0
    je .parts
    call mc_draw_all
    jmp short .out
.parts:
    call mc_cross_moved             ; the crosshair STAYS on screen unless the
                                    ; mouse moved; anything that draws through
                                    ; it takes it off itself (SPEC.md 48.11)
    call mc_wipe_trails
    call mc_drn_run                 ; a spent trail leaves over several frames
                                    ; (SPEC.md 48.15), and BEFORE the bursts
                                    ; and the terrain, which draw over it
    call mc_draw_exp
    call mc_move_trails
    call mc_draw_sat
    cmp word [mc_gdx1], MC_NODMG    ; SPEC.md 48.9: repair the span that was
    je .base                        ; bitten, not the whole band
    mov ax, [mc_gdx1]
    mov cx, [mc_gdx2]
    call mc_gdclear
    mov dl, 7                       ; every base inside the span
    call mc_spanset
    call mc_draw_ground
    call mc_draw_cities
    call mc_draw_bases
    call mc_exp_restore             ; the terrain just painted over any burst
                                    ; standing in it, and SPEC.md 48.8's
                                    ; explosion only redraws when its RADIUS
                                    ; changes - so it would stay holed for up
                                    ; to nine frames. It used to heal itself
                                    ; because the disc was redrawn every frame
.base:
    cmp byte [mc_bdirty], 0
    je .stat
    xor ax, ax                      ; a launch owes ONE launcher, anywhere
    mov cx, [mc_cw]
    dec cx
    mov dl, [mc_bdirty]
    mov byte [mc_bdirty], 0
    call mc_spanset
    call mc_draw_bases
.stat:
    cmp byte [mc_sdirty], 0
    je .msg
    mov byte [mc_sdirty], 0
    call mc_draw_status
.msg:
    call mc_draw_msg
    call mc_cross_on
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_draw_all - the whole content
; in:  gfx lock held, origin tracked; preserves all registers
;
; The trails are reconstructible rather than remembered: every missile carries
; the point it launched from and the point it has been drawn to, so a full
; repaint is one mc_line per missile. That is the only reason this game
; survives being covered and uncovered without a frame buffer.
; -----------------------------------------------------------------------------
mc_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [mc_full], 0
    mov word [mc_msgp], 0           ; the content fill took the banner with it
    call mc_drn_clear               ; ...and every pending erase, which would
                                    ; otherwise erase what this repaint drew
    call mc_sshad_clr               ; ...and what the status strip held: the
                                    ; content fill took it with it
    call mc_gdclear
    mov byte [mc_bdirty], 0
    mov byte [mc_sdirty], 0
    mov byte [mc_chshown], 0
    xor ax, ax                      ; a full repaint IS the whole span
    mov cx, [mc_cw]
    dec cx
    mov dl, 7
    call mc_spanset
    mov al, MC_BG
    call mc_setcol
    xor ax, ax
    xor bx, bx
    mov cx, [mc_cw]
    dec cx
    mov dx, [mc_ch]
    dec dx
    call mc_fillc
    call mc_draw_ground
    call mc_draw_cities
    call mc_draw_bases
    call mc_redraw_trails
    call mc_draw_exp_all
    call mc_draw_sat_full
    call mc_draw_status
    call mc_draw_msg
    cmp byte [mc_abon], 0           ; the credits sit on top of everything, and
    je .noab                        ; every full repaint puts them back
    call mc_abdraw
    jmp short .out
.noab:
    call mc_cross_on
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_exp_restore - put back every live burst standing in the armed span
; in:  gfx lock held, mc_spanset armed; preserves all registers
;
; Terrain is drawn AFTER the bursts, so a repair writes ground over any burst
; low enough to overlap it. That was harmless while the disc was redrawn
; every frame and is not now (SPEC.md 48.8/48.9). Drawn at [mc_er], the
; radius actually on screen, so this repaints and never advances a state.
; -----------------------------------------------------------------------------
mc_exp_restore:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    mov bl, [mc_er + si]
    mov bh, 0
    or bx, bx
    jz .next
    mov di, si
    add di, di
    mov ax, [mc_ex + di]
    mov cx, ax
    sub ax, bx
    add cx, bx
    call mc_spanhit
    jc .next
    call mc_exp_pen
    mov ax, [mc_ex + di]
    mov dx, [mc_ey + di]
    mov bl, [mc_er + si]
    mov bh, 0
    cmp byte [mc_ecoarse], 0
    je .disc
    call mc_blob
    jmp short .next
.disc:
    call mc_disc
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_gdmg - the ground was bitten between x AX and x CX; remember the span
; preserves all registers (SPEC.md 48.9)
;
; This used to be one byte meaning "something touched the terrain", and the
; repair it bought was the WHOLE band plus all six cities and all three bases
; - 398 PIT counts, 143 ms on a 4.77MHz XT, about two and a half ticks, and
; it fires every time an ICBM or a burst reaches the deck. Measured at five
; times in 86 frames just by letting a wave land. A span costs two words and
; repairs what was actually damaged.
; -----------------------------------------------------------------------------
mc_gdmg:
    push ax
    push cx
    cmp ax, cx                      ; callers hand it either way round
    jle .ord
    xchg ax, cx
.ord:
    cmp ax, [mc_gdx1]
    jge .lo
    mov [mc_gdx1], ax
.lo:
    cmp cx, [mc_gdx2]
    jle .hi
    mov [mc_gdx2], cx
.hi:
    pop cx
    pop ax
    ret

; mc_gdclear / mc_gdall - forget the damage, or claim all of it
; preserve all registers
mc_gdclear:
    mov word [mc_gdx1], MC_NODMG
    mov word [mc_gdx2], -1
    ret
mc_gdall:
    push ax
    mov word [mc_gdx1], 0
    mov ax, [mc_cw]
    dec ax
    mov [mc_gdx2], ax
    pop ax
    ret

; mc_bdirty_i / mc_bdirty_at - one launcher owes a redraw, named by index BL
; or by a point AX inside its lane. Both preserve every register.
;
; SPEC.md 48.9: this was one byte meaning "some launcher changed", and it
; cost all three lanes cleared and all three pyramids redrawn - on EVERY
; shot, because a trail starts on the launcher and the wipe says so.
mc_bdirty_i:
    push ax
    push cx
    mov cl, bl
    mov al, 1
    shl al, cl
    or [mc_bdirty], al
    pop cx
    pop ax
    ret

mc_bdirty_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    mov di, si
    add di, di
    mov cx, [mc_basex + di]
    sub cx, ax
    jns .abs
    neg cx
.abs:
    mov dx, [mc_basw]
    shr dx, 1
    cmp cx, dx
    ja .next
    mov cx, si
    mov bl, 1
    shl bl, cl
    or [mc_bdirty], bl
.next:
    inc si
    cmp si, MC_NBASE
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_spanset - arm the three terrain painters for one span and base mask
; in:  AX = x1, CX = x2, DL = base mask; preserves all registers
mc_spanset:
    mov [mc_spx1], ax
    mov [mc_spx2], cx
    mov [mc_bmask], dl
    ret

; mc_spanhit - does [AX..CX] overlap the armed span? CF=0 yes, CF=1 no
; preserves all registers
mc_spanhit:
    cmp cx, [mc_spx1]
    jl .no
    cmp ax, [mc_spx2]
    jg .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; mc_draw_ground - the terrain band, with a coastline on top of it
; in:  gfx lock held; preserves all registers
;
; The bumps come off a fixed 16-byte table rather than the random stream, so
; the terrain a player learns stays where they learned it and a repaint puts
; back exactly what was there.
; -----------------------------------------------------------------------------
mc_draw_ground:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mc_cgnd]
    call mc_setcol
    mov ax, [mc_spx1]               ; the band itself, only across the span
    mov bx, [mc_groundy]
    mov cx, [mc_spx2]
    mov dx, [mc_ch]
    dec dx
    call mc_fillc

    xor si, si                      ; the coastline: one step every 16 px, its
    xor di, di                      ; height off a fixed table. SI indexes the
.bump:                              ; table, DI is the column
    cmp di, [mc_cw]
    jae .out
    mov ax, di                      ; a step outside the span is somebody
    mov cx, di                      ; else's pixels
    add cx, 15
    call mc_spanhit
    jc .next
    mov bx, si
    and bx, 15
    mov bl, [mc_coast + bx]         ; BL = how many rows this step rises
    mov bh, 0
    or bx, bx
    jz .next
    mov dx, [mc_groundy]            ; the step: (x, groundy - h)-(x+15, groundy-1)
    dec dx
    mov ax, [mc_groundy]
    sub ax, bx
    mov bx, ax                      ; BX = its top row
    mov ax, di
    mov cx, di
    add cx, 15
    call mc_fillc
.next:
    add di, 16
    inc si
    jmp short .bump
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_draw_cities - the six cities, skylines in the wave's city colour
; in:  gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
mc_draw_cities:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mc_ccity]
    call mc_setcol
    xor si, si
.each:
    mov di, si
    add di, di
    mov ax, [mc_cityx + di]
    mov bx, [mc_citw]
    shr bx, 1
    sub ax, bx                      ; AX = left edge
    mov cx, ax
    add cx, [mc_citw]
    dec cx
    call mc_spanhit                 ; a city clear of the damage keeps its
    jc .next                        ; pixels, and costs nothing
    cmp byte [mc_calive + si], 0
    je .dead
    mov [mc_dtmp], ax
    call mc_city_shape
    jmp short .next
.dead:                              ; SPEC.md 48.23: a DEAD city has to be
    push ax                         ; erased, and nothing else does it - the
    push cx                         ; band mc_draw_ground repaints starts AT
    mov bx, [mc_groundy]            ; [mc_groundy] and a city's towers stand
    mov dx, bx                      ; [mc_objh] ABOVE it
    dec dx
    sub bx, [mc_objh]
    mov al, MC_BG
    call mc_setcol
    call mc_fillc
    mov al, [mc_ccity]              ; the pen back for the cities after it
    call mc_setcol
    pop cx
    pop ax
.next:
    inc si
    cmp si, MC_NCITY
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_city_shape - three towers on a base row, at [mc_dtmp], width [mc_citw]
; in: gfx lock held, pen set; preserves all registers
;
; Four fills, and the reason a city is towers rather than one block: on a 1bpp
; adapter a city and the ground it stands on may reduce to the same ink
; (mc_pal keeps them in different classes, but the classes themselves are only
; three), so the SHAPE has to carry it.
mc_city_shape:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mc_citw]               ; a tower is a quarter of the width
    mov cl, 2
    shr ax, cl
    or ax, ax
    jnz .tw
    mov ax, 1
.tw:
    mov [mc_dtw], ax

    mov ax, [mc_dtmp]               ; the base row: full width, two tall
    mov cx, ax
    add cx, [mc_citw]
    dec cx
    mov dx, [mc_groundy]
    mov bx, dx
    dec bx
    call mc_fillc

    mov ax, [mc_dtmp]               ; the left tower: half height
    mov cx, ax
    add cx, [mc_dtw]
    dec cx
    mov bx, [mc_objh]
    shr bx, 1
    mov dx, [mc_groundy]
    sub dx, bx
    mov bx, dx
    mov dx, [mc_groundy]
    call mc_fillc

    mov ax, [mc_dtmp]               ; the middle one: the full object height
    add ax, [mc_dtw]
    mov cx, ax
    add cx, [mc_dtw]
    dec cx
    mov bx, [mc_groundy]
    sub bx, [mc_objh]
    mov dx, [mc_groundy]
    call mc_fillc

    mov ax, [mc_dtmp]               ; ...and the right one: three quarters, out
    add ax, [mc_dtw]                ; to the city's right edge
    add ax, [mc_dtw]
    mov cx, [mc_dtmp]
    add cx, [mc_citw]
    dec cx
    mov bx, [mc_objh]               ; three quarters of the object height
    shr bx, 1
    mov [mc_dtmp2], bx              ; = half...
    shr bx, 1                       ; ...plus a quarter
    add bx, [mc_dtmp2]
    mov dx, [mc_groundy]
    sub dx, bx
    mov bx, dx
    mov dx, [mc_groundy]
    call mc_fillc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_draw_bases - the three launchers, and the missiles each has left
; in:  gfx lock held; preserves all registers
;
; The pyramid is the arcade's: ten missiles in rows of 4, 3, 2, 1 with the tip
; at the top, drawn from the bottom row up so a magazine emptying looks like a
; magazine emptying. A destroyed base draws as a flat crater in the ground
; colour, which is a SHAPE and so survives the 1bpp reduction.
; -----------------------------------------------------------------------------
mc_draw_bases:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    mov di, si
    add di, di
    mov cx, si                      ; SPEC.md 48.9: a launch dirties ONE
    mov al, [mc_bmask]              ; launcher, not all three - CL is si, so
    shr al, cl                      ; bit si is this base's
    test al, 1
    jz .next
    mov ax, [mc_basex + di]
    mov bx, [mc_basw]
    shr bx, 1
    mov [mc_dtw], bx
    sub ax, bx
    mov [mc_dtmp], ax               ; the left edge
    mov cx, ax
    add cx, [mc_basw]
    dec cx
    call mc_spanhit
    jc .next

    mov al, MC_BG                   ; the lane this base owns, cleared
    call mc_setcol
    mov ax, [mc_dtmp]
    mov bx, [mc_groundy]
    sub bx, [mc_objh]
    dec bx
    mov cx, ax
    add cx, [mc_basw]
    dec cx
    mov dx, [mc_groundy]
    dec dx
    call mc_fillc

    cmp byte [mc_balive + si], 0
    je .dead
    mov al, [mc_cabm]
    call mc_setcol
    call mc_base_shape
    jmp short .next
.dead:
    ; A CRATER, and it has to be a hole rather than a stump. The first version
    ; drew two low stubs in the GROUND colour on top of the ground - invisible
    ; on every adapter, so a destroyed base looked like bare terrain and the
    ; player could not tell a launcher that was gone from one that was merely
    ; out of missiles. A notch bitten out of the ground reads on all three.
    mov al, MC_BG
    call mc_setcol
    mov ax, [mc_dtmp]
    add ax, 2
    mov bx, [mc_groundy]
    mov cx, [mc_dtmp]
    add cx, [mc_basw]
    sub cx, 3
    mov dx, bx
    add dx, 2
    call mc_fillc
    mov al, [mc_cgnd]               ; ...with a lip either side of it
    call mc_setcol
    mov ax, [mc_dtmp]
    mov bx, [mc_groundy]
    dec bx
    mov cx, ax
    inc cx
    mov dx, [mc_groundy]
    call mc_fillc
    mov ax, [mc_dtmp]
    add ax, [mc_basw]
    sub ax, 2
    mov bx, [mc_groundy]
    dec bx
    mov cx, ax
    inc cx
    mov dx, [mc_groundy]
    call mc_fillc
.next:
    inc si
    cmp si, MC_NBASE
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_base_shape - the mound plus the missile pyramid
; in: SI = base index, [mc_dtmp] = left edge, pen set; preserves all registers
mc_base_shape:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [mc_bmis + si]
    mov ch, 0
    mov [mc_dmis], cx               ; how many are still in the magazine

    mov ax, [mc_objh]               ; the mound: the bottom quarter of the block
    mov cl, 2
    shr ax, cl
    cmp ax, 2
    jae .mh
    mov ax, 2
.mh:
    mov bx, [mc_groundy]
    sub bx, ax
    mov [mc_dmound], bx             ; the mound's top row
    mov ax, [mc_dtmp]
    mov cx, ax
    add cx, [mc_basw]
    dec cx
    mov dx, [mc_groundy]
    dec dx
    call mc_fillc

    ; The pyramid: rows of 4, 3, 2 and 1 from the mound upward, tip at the top,
    ; drawn bottom row first so a magazine emptying looks like one. A missile
    ; is a 2px bar; the row pitch is whatever height is left above the mound.
    mov ax, [mc_dmound]
    mov bx, [mc_groundy]
    sub bx, [mc_objh]
    sub ax, bx                      ; AX = rows available
    mov cl, 2
    shr ax, cl
    or ax, ax
    jnz .ph
    mov ax, 1
.ph:
    mov [mc_dph], ax                ; the row pitch

    mov word [mc_drow], 0           ; which pyramid row, 0 = the widest
    mov word [mc_dleft], 0          ; missiles placed so far
.row:
    cmp word [mc_drow], 4
    jae .out
    mov ax, 4
    sub ax, [mc_drow]
    mov [mc_dn], ax                 ; missiles in this row

    mov bx, ax                      ; the row's left edge, centred in the base
    mov ax, bx
    mov cx, 3
    mul cx                          ; AX = row width in pixels
    mov bx, [mc_basw]
    sub bx, ax
    jns .cen
    xor bx, bx
.cen:
    shr bx, 1
    add bx, [mc_dtmp]
    mov [mc_dcol], bx

    mov ax, [mc_dmound]             ; ...and its top row
    mov bx, [mc_drow]
    inc bx
    push dx
    mov ax, bx
    mul word [mc_dph]
    mov bx, ax
    pop dx
    mov ax, [mc_dmound]
    sub ax, bx
    mov [mc_drowy], ax

    xor si, si
.mis:
    mov ax, [mc_dleft]
    cmp ax, [mc_dmis]
    jae .out                        ; the magazine ran out part way along
    inc word [mc_dleft]
    mov ax, si
    mov cx, 3
    mul cx
    add ax, [mc_dcol]               ; AX = this missile's left column
    mov cx, ax
    inc cx
    mov bx, [mc_drowy]
    mov dx, bx
    add dx, [mc_dph]
    dec dx
    call mc_fillc
    inc si
    cmp si, [mc_dn]
    jb .mis
    inc word [mc_drow]
    jmp short .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; A trail is ONE Bresenham, laid at launch and walked (SPEC.md 48.14)
;
; It used to be drawn as a chain of per-frame segments and erased as one long
; line, and those two rasterizations are not the same pixels - which is what
; SPEC.md 5.6.5's dilation existed to cover, at three read-modify-writes a
; pixel. The resumable walk (5.6.7) removes the mismatch instead of paying for
; it: the line from the launch point to the point the missile is AIMED at is
; laid down once, the draw advances it a few pixels a frame, and the erase
; re-lays the identical line and replays exactly the pixels the draw emitted.
;
; Two things follow, and both are corrections rather than trades. A dodging
; smart bomb's trail is a straight line now instead of a polyline the straight
; erase could never have removed - the erase always assumed a straight line,
; so this is the draw being brought into line with it rather than the other
; way round. And nothing is left behind: the 104-of-217 stranded pixels that
; the dilation was sized for cannot occur, because the two walks are the same
; walk.
;
; [mc_iarm]/[mc_aarm] carries which mode a slot is in: 0 not laid yet, 1
; walking, 2 no walk. The last is the Mode X surface (SPEC.md 53.1 puts every
; kernel drawing slot off-limits there) and a trail with an endpoint outside
; the content box (the kernel clips to the SCREEN, and only mc_fillc clamps to
; our window) - both keep the old segment-and-dilated-line path below, and a
; slot may move between the two, because mc_track raises [mc_full] whenever
; the origin moves and mc_redraw_trails re-lays every walk.
; -----------------------------------------------------------------------------

; mc_iblk / mc_ablk - DI = slot SI's walk block; preserve every other register
mc_iblk:
    push cx
    mov di, si
    mov cl, 4
    shl di, cl
    add di, mc_iwlk
    pop cx
    ret

mc_ablk:
    push cx
    mov di, si
    mov cl, 4
    shl di, cl
    add di, mc_awlk
    pop cx
    ret

; mc_tr_lay - lay the walk at DI along (AX,BX)-(CX,DX), content coords
; out: CF=1 and nothing laid: this trail has no walk
;      CF=0 and [mc_trlen] = the walk's LENGTH, signed: +n = n pixels with x
;      as the major axis, -n = n pixels with y as it
; preserves all registers
;
; The block holds SCREEN coordinates, which is why a moved window invalidates
; every one of them at once - see mc_track. The length is the app's business
; rather than the block's because the SDK does not publish the block's layout
; (SPEC.md 20.8) - and it is needed on every frame, to stop a bomb that dodged
; off its own line asking for pixels past the end of it.
mc_tr_lay:
    push ax
    push bx
    push cx
    push dx
    push es
    cmp byte [mc_fsx], 0            ; the Mode X surface has no kernel slots
    jne .no
    or ax, ax                       ; ...and neither end may leave the content
    js .no
    or bx, bx
    js .no
    or cx, cx
    js .no
    or dx, dx
    js .no
    cmp ax, [mc_cw]
    jge .no
    cmp cx, [mc_cw]
    jge .no
    cmp bx, [mc_ch]
    jge .no
    cmp dx, [mc_ch]
    jge .no
    push cx                         ; the span, before the origin goes on
    push dx
    sub cx, ax
    jns .adx
    neg cx
.adx:
    sub dx, bx
    jns .ady
    neg dx
.ady:
    cmp cx, dx
    jb .ymaj
    inc cx
    mov [mc_trlen], cx
    jmp short .span
.ymaj:
    inc dx
    neg dx
    mov [mc_trlen], dx
.span:
    pop dx
    pop cx
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    push ds
    pop es
    call OSAPI_GFX_LINIT
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_tr_step - draw the next CX pixels of the walk at DI in the current pen
; in:  gfx lock held; preserves all registers
mc_tr_step:
    push es
    push ds
    pop es
    call OSAPI_GFX_LSTEP
    pop es
    ret

; -----------------------------------------------------------------------------
; mc_dsc_add / mc_dsc_run - step every live trail in ONE call (SPEC.md 48.16)
;
; A walk step draws two or three pixels, and PERFORMANCE.md Part 2 prices the
; ARRIVING at a drawing call - not the drawing - at ~756us. One call a missile
; a frame was therefore seven or eight floors to move about twenty pixels: a
; field log put it at 10.2 ms of a 46 ms frame, which is 570us a pixel against
; the drain's 160 for the identical operation. Same pixels, one arrival.
;
; mc_dsc_add: DI = the block, CX = pixels; mc_dsc_run: spend the batch.
; Both preserve every register, and the batch flushes itself if it fills.
; -----------------------------------------------------------------------------
mc_dsc_add:
    push bx
    cmp word [mc_dscn], MC_DSCMAX
    jb .room
    call mc_dsc_run                 ; full: spend it and start again
.room:
    mov bx, [mc_dscn]
    add bx, bx
    add bx, bx
    mov [mc_dsc + bx], di
    mov [mc_dsc + bx + 2], cx
    inc word [mc_dscn]
    pop bx
    ret

mc_dsc_run:
    push cx
    push di
    push es
    mov cx, [mc_dscn]
    jcxz .out
    mov word [mc_dscn], 0
    mov di, mc_dsc
    push ds
    pop es
    call OSAPI_GFX_LSTEPV
.out:
    pop es
    pop di
    pop cx
    ret

; mc_tr_need - how much of a walk the head has reached
; in:  AX = the trail start's MAJOR coordinate, CX = the head's, DI = the
;      walk's length in pixels
; out: AX = 1..DI; clobbers CX
;
; The walk steps its MAJOR axis once a pixel, so the distance travelled along
; that axis IS the pixel index - and clamping it to the length is what keeps a
; bomb that dodged off its own line from asking for pixels past the end of it.
mc_tr_need:
    sub cx, ax
    jns .ok
    neg cx
.ok:
    mov ax, cx
    inc ax
    cmp ax, di
    jbe .fit
    mov ax, di
.fit:
    ret

; -----------------------------------------------------------------------------
; A spent trail DRAINS rather than popping (SPEC.md 48.15)
;
; A trail arrives over sixty frames and used to leave in one, and the one is
; where the stutter was: a burst can kill five missiles at once, and five
; whole-line erases in a single frame is exactly the batch a field log caught
; at 37.8 ms. Nothing needs it to be one frame - the resumable walk (5.6.7)
; can stop and resume anywhere - so a dead trail is queued and spent a few
; pixels a frame instead, at about eight times the rate it was drawn at.
;
; Two caps, and they answer different questions. MC_DRN_RATE is per trail and
; jittered, so two missiles killed by one burst do not finish on the same
; frame; MC_DRNBUD is per FRAME across the whole queue, which is the one that
; actually bounds the cost - a jitter alone still lets eight entries ask for
; eight rates at once. [mc_drnrr] rotates which entry the budget reaches
; first, so a long trail cannot starve the ones behind it.
;
; The queue is small and overflow is graceful: a full queue erases inline,
; which is exactly what every erase did before this existed.
; -----------------------------------------------------------------------------

; mc_drn_push - hand a dead trail to the drain instead of erasing it now
; in:  AX/BX = start, CX/DX = end (content coords), SI = pixels drawn,
;      [mc_drngx]/[mc_drnbx] = the damage the two ends owe, MC_NODMG for none
; out: CF=1 refused - the caller still owes the erase AND the damage marks
; preserves all registers
mc_drn_push:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or si, si                       ; nothing drawn is nothing to erase, and
    jz .full                        ; a zero LEFT is how a free entry reads
    xor di, di
.slot:
    cmp word [mc_drn + di + MC_DRN_LEFT], 0
    je .got
    add di, MC_DRN
    cmp di, MC_MAXDRN * MC_DRN
    jb .slot
    jmp short .full
.got:
    mov [mc_drn + di + MC_DRN_X1], ax
    mov [mc_drn + di + MC_DRN_Y1], bx
    mov [mc_drn + di + MC_DRN_X2], cx
    mov [mc_drn + di + MC_DRN_Y2], dx
    add di, mc_drn
    call mc_tr_lay                  ; the SAME line the draw walked
    jc .full
    sub di, mc_drn
    mov cx, 8                       ; the jitter, so a cluster does not all
    call mc_rand_mod                ; finish together
    add ax, MC_DRNRATE
    mov [mc_drn + di + MC_DRN_RATE], ax
    mov ax, [mc_drngx]
    mov [mc_drn + di + MC_DRN_GX], ax
    mov ax, [mc_drnbx]
    mov [mc_drn + di + MC_DRN_BX], ax
    mov ax, [mc_drn + di + MC_DRN_X1]   ; the erase has reached the launch
    mov [mc_drn + di + MC_DRN_CX], ax   ; point and no further
    mov ax, [mc_drn + di + MC_DRN_Y1]
    mov [mc_drn + di + MC_DRN_CY], ax
    mov [mc_drn + di + MC_DRN_LEFT], si
    inc word [mc_drnq]
    clc
    jmp short .out
.full:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_drn_hold - are the next AX pixels of this entry inside a burst that is
;               still lit? (SPEC.md 48.19)
; in:  SI = entry offset, AX = pixels about to be stepped
; out: CF=1 hold the entry this frame and step nothing; CF=0 go ahead, and
;      MC_DRN_CX/CY have been advanced to where the step ends
; preserves all registers
;
; A missile dies INSIDE the burst that killed it, so the far end of its trail
; is under the fireball - and SPEC.md 48.8 draws a burst only when its radius
; changes, which the shipped ramp does once. A background line replayed
; through a standing burst therefore stays cut into it for the rest of its
; life, which is 48.9.1's rule again: the erase used to be harmless because
; the disc was redrawn every frame.
;
; Those pixels do not need erasing at all. The burst drew OVER them, and its
; own end-of-life pass (mc_draw_exp's .gone) takes the whole disc back to sky
; and takes them with it. So the entry WAITS rather than the burst being
; repainted - no extra drawing anywhere, and what it reads as is smoke
; hanging in the fireball until the fireball fades.
;
; The walk block will not say where it has got to, so the point is carried
; alongside it: the MAJOR axis advances by exactly the pixel count (that is
; what makes it the major axis) and the minor is derived from it, so nothing
; drifts however many frames an entry takes.
; -----------------------------------------------------------------------------
mc_drn_hold:
    push bx
    push cx
    push dx
    push si
    push di
    mov [mc_dhent], si
    mov [mc_dhcnt], ax
    mov ax, [mc_drn + si + MC_DRN_CX]   ; where the erase has reached
    mov [mc_dhx0], ax
    mov ax, [mc_drn + si + MC_DRN_CY]
    mov [mc_dhy0], ax
    mov ax, [mc_drn + si + MC_DRN_X2]
    sub ax, [mc_drn + si + MC_DRN_X1]
    mov [mc_dhdx], ax               ; dx and dy, signed
    mov bx, [mc_drn + si + MC_DRN_Y2]
    sub bx, [mc_drn + si + MC_DRN_Y1]
    mov [mc_dhdy], bx
    or ax, ax                       ; ...and their magnitudes, which is what
    jns .adx                        ; decides the major axis. The DIVISIONS
    neg ax                          ; below use the signed deltas, not these:
.adx:                               ; the numerator carries the direction too,
    or bx, bx                       ; so dividing by a magnitude puts the
    jns .ady                        ; point on the wrong side of the start
    neg bx                          ; whenever the line runs left or up
.ady:
    mov byte [mc_dhmaj], 0          ; 0 x-major / 1 y-major / 2 a single point
    cmp ax, bx
    jae .maj
    mov byte [mc_dhmaj], 1
.maj:
    or ax, ax
    jnz .live
    or bx, bx
    jnz .live
    mov byte [mc_dhmaj], 2
.live:
    ; --- how far may this entry go before it reaches a lit burst? ----------
    ; A step moves each axis by at most one, so the CHEBYSHEV distance to a
    ; burst falls by at most one per step - which makes this bound safe for
    ; the whole PATH rather than just its end, and it costs no division at
    ; all. It is the square round the disc, so it stops up to 0.41r short on
    ; a diagonal approach; .ext below spends that back a pixel at a time.
    mov cx, [mc_dhcnt]
    xor si, si
.each:
    cmp byte [mc_ea + si], 1        ; a burst at mc_er 0 has drawn nothing to
    jne .enext                      ; protect, and one on its way out is
    mov bl, [mc_er + si]            ; erased whole anyway
    mov bh, 0
    or bx, bx
    jz .enext
    mov di, si
    add di, di
    mov ax, [mc_ex + di]
    sub ax, [mc_dhx0]
    jns .cxp
    neg ax
.cxp:
    mov dx, [mc_ey + di]
    sub dx, [mc_dhy0]
    jns .cyp
    neg dx
.cyp:
    cmp ax, dx                      ; AX = Chebyshev distance to the centre
    jae .cheb
    mov ax, dx
.cheb:
    sub ax, bx                      ; ...less the radius, less one
    dec ax
    jns .cok
    xor ax, ax                      ; already inside it: nothing may be
.cok:                               ; erased at all
    cmp ax, cx
    jae .enext
    mov cx, ax
.enext:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    cmp cx, [mc_dhcnt]              ; nothing in the way: the whole step, and
    jae .take                       ; no probe needed to know it

    ; --- the square stopped us short of the DISC; walk the gap back --------
    ; Each probe tests the point itself, so this can only extend into space
    ; that is genuinely outside every burst - and it stops at the first one
    ; that is not, so it can never step over a disc.
    mov si, MC_DHEXT
.ext:
    cmp cx, [mc_dhcnt]
    jae .take
    inc cx
    mov [mc_dhn], cx
    call mc_dh_pt
    call mc_dh_in                   ; CF=1: that pixel is inside a lit burst
    jnc .enext2
    dec cx                          ; ...so the one before it is the edge
    jmp short .take
.enext2:
    dec si
    jnz .ext
.take:
    mov [mc_dhk], cx
    jcxz .hold
    mov [mc_dhn], cx
    call mc_dh_pt
    mov si, [mc_dhent]
    mov ax, [mc_dhnx]
    mov [mc_drn + si + MC_DRN_CX], ax
    mov ax, [mc_dhny]
    mov [mc_drn + si + MC_DRN_CY], ax
    mov ax, [mc_dhk]
    clc
    jmp short .out
.hold:
    xor ax, ax
    stc
.out:                               ; pop leaves the flags alone, which is
    pop di                          ; what carries CF out of here
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mc_dh_pt - the point [mc_dhn] steps further along this entry's line
; in:  [mc_dhent], [mc_dhx0]/[mc_dhy0], [mc_dhdx]/[mc_dhdy], [mc_dhmaj]
; out: [mc_dhnx]/[mc_dhny]; preserves every register
;
; The MAJOR axis advances by exactly the step - that is what makes it the
; major axis - and the minor is derived from it rather than accumulated, so
; an entry that takes twenty frames drifts by nothing.
mc_dh_pt:
    push ax
    push cx
    push dx
    push si
    mov si, [mc_dhent]
    mov cx, [mc_dhx0]
    mov dx, [mc_dhy0]
    cmp byte [mc_dhmaj], 2
    je .point
    cmp byte [mc_dhmaj], 1
    je .ymaj
    mov ax, [mc_dhn]
    cmp word [mc_dhdx], 0
    jl .xneg
    add cx, ax
    jmp short .xd
.xneg:
    sub cx, ax
.xd:
    mov [mc_dhnx], cx               ; banked before the divide eats DX
    mov ax, cx                      ; y = y1 + dy * (x - x1) / dx
    sub ax, [mc_drn + si + MC_DRN_X1]
    imul word [mc_dhdy]
    idiv word [mc_dhdx]
    add ax, [mc_drn + si + MC_DRN_Y1]
    mov [mc_dhny], ax
    jmp short .out
.ymaj:
    mov ax, [mc_dhn]
    cmp word [mc_dhdy], 0
    jl .yneg
    add dx, ax
    jmp short .yd
.yneg:
    sub dx, ax
.yd:
    mov [mc_dhny], dx
    mov ax, dx                      ; x = x1 + dx * (y - y1) / dy
    sub ax, [mc_drn + si + MC_DRN_Y1]
    imul word [mc_dhdx]
    idiv word [mc_dhdy]
    add ax, [mc_drn + si + MC_DRN_X1]
    mov [mc_dhnx], ax
    jmp short .out
.point:
    mov [mc_dhnx], cx
    mov [mc_dhny], dx
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mc_dh_in - is [mc_dhnx]/[mc_dhny] inside a burst that is lit?
; out: CF=1 inside; preserves every register
;
; The DISC and not its bounding square, which is the whole point of this
; probe. Both deltas are bounded before they are squared: this is only ever
; called within a radius and a few pixels of a centre, so a far one is
; rejected on the compare and the products stay well inside a word.
mc_dh_in:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    mov bl, [mc_er + si]
    mov bh, 0
    or bx, bx
    jz .next
    mov di, si
    add di, di
    mov ax, [mc_ex + di]
    sub ax, [mc_dhnx]
    jns .xp
    neg ax
.xp:
    cmp ax, bx                      ; INSIDE needs |dx| <= r and |dy| <= r, so
    ja .next                        ; this rejects every burst but the one
    mov cx, [mc_ey + di]            ; being approached without a single
    sub cx, [mc_dhny]               ; multiply - which is what keeps a probe
    jns .yp                         ; affordable, since the loop runs
    neg cx                          ; MC_DHEXT times per entry per frame. It
.yp:                                ; is exact, not a heuristic: |dx| > r
    cmp cx, bx                      ; already means dx*dx > r*r. It also
    ja .next                        ; bounds the squares below, r being a byte
    imul ax                         ; dx*dx + dy*dy <= r*r
    mov dx, ax
    mov ax, cx
    imul ax
    add ax, dx
    mov cx, ax
    mov ax, bx
    imul ax
    cmp cx, ax
    jbe .hit
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    clc
    jmp short .out
.hit:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_drn_clear - forget every pending erase
; preserves all registers
;
; A full repaint has already put the background back, so a queue that
; survived it would erase pixels the repaint drew.
mc_drn_clear:
    push di
    xor di, di
.each:
    mov word [mc_drn + di + MC_DRN_LEFT], 0
    add di, MC_DRN
    cmp di, MC_MAXDRN * MC_DRN
    jb .each
    mov word [mc_drnq], 0
    mov word [mc_drnrr], 0
    pop di
    ret

; mc_drn_run - spend this frame's budget on the queue
; in:  gfx lock held; preserves all registers
;
; The damage marks belong HERE and not at the push, because the erase reaches
; the two ends of the line frames apart: the launcher is under the FIRST
; pixels and the ground bite under the LAST, so one is owed on the first
; serve and the other when the entry finishes.
mc_drn_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [mc_drnq], 0
    je .out
    mov al, MC_BG
    call mc_setcol
    mov word [mc_drnbud], MC_DRNBUD
    mov word [mc_drnn], MC_MAXDRN
    mov si, [mc_drnrr]
.each:
    cmp word [mc_drn + si + MC_DRN_LEFT], 0
    je .next
    cmp word [mc_drnbud], 0
    jle .fin
    mov ax, [mc_drn + si + MC_DRN_RATE]
    cmp ax, [mc_drn + si + MC_DRN_LEFT]
    jbe .r1
    mov ax, [mc_drn + si + MC_DRN_LEFT]
.r1:
    cmp ax, [mc_drnbud]
    jbe .r2
    mov ax, [mc_drnbud]
.r2:
    call mc_drn_hold                ; SPEC.md 48.19: pixels under a lit burst
    jc .next                        ; are that burst's to erase, not ours -
                                    ; and AX comes back CLAMPED to the disc's
                                    ; edge, so the erase stops exactly there
    sub [mc_drnbud], ax
    sub [mc_drn + si + MC_DRN_LEFT], ax
    mov [mc_drncnt], ax
    mov ax, [mc_drn + si + MC_DRN_X1]
    mov bx, [mc_drn + si + MC_DRN_Y1]
    mov cx, [mc_drn + si + MC_DRN_X2]
    mov dx, [mc_drn + si + MC_DRN_Y2]
    call mc_cross_need              ; the two ends still bound every pixel
    mov ax, [mc_drn + si + MC_DRN_BX]
    cmp ax, MC_NODMG
    je .step
    mov word [mc_drn + si + MC_DRN_BX], MC_NODMG
    call mc_bdirty_at               ; an ABM's trail begins ON its launcher,
.step:                              ; and these are the pixels uncovering it
    mov di, si
    add di, mc_drn
    mov cx, [mc_drncnt]
    call mc_dsc_add                 ; every crosshair erase in this loop still
                                    ; runs before any pixel is drawn, because
                                    ; the batch is spent after it
    cmp word [mc_drn + si + MC_DRN_LEFT], 0
    jne .next
    dec word [mc_drnq]              ; finished: the far end is uncovered now
    mov cx, [mc_drn + si + MC_DRN_GX]
    cmp cx, MC_NODMG
    je .next
    mov ax, cx
    sub ax, 2
    add cx, 2
    call mc_gdmg
.next:
    add si, MC_DRN
    cmp si, MC_MAXDRN * MC_DRN
    jb .go
    xor si, si
.go:
    dec word [mc_drnn]
    jnz .each
.fin:
    call mc_dsc_run
    mov si, [mc_drnrr]              ; a different entry gets first call on the
    add si, MC_DRN                  ; budget next frame
    cmp si, MC_MAXDRN * MC_DRN
    jb .rr
    xor si, si
.rr:
    mov [mc_drnrr], si
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_redraw_trails - every live trail, from its launch point to where it has
;                    been drawn to
; in:  gfx lock held; preserves all registers
;
; This is also where a walk is re-laid, so it is what heals a window that
; moved, a surface that changed and a slot that has to swap paths.
; -----------------------------------------------------------------------------
mc_redraw_trails:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mc_cicbm]
    call mc_setcol
    xor si, si
.icbm:
    mov al, [mc_ia + si]
    cmp al, 1
    jb .inext
    cmp al, 2
    ja .inext
    mov di, si
    add di, di
    mov ax, [mc_isx + di]
    mov bx, [mc_isy + di]
    mov cx, [mc_iex + di]
    mov dx, [mc_iey + di]
    push di
    call mc_iblk
    call mc_tr_lay
    pop bx                          ; BX = slot*2 again; a pop leaves CF alone
    jc .iold
    mov byte [mc_iarm + si], 1
    mov ax, [mc_trlen]
    mov [mc_ilen + bx], ax
    mov cx, [mc_idrw + bx]
    jcxz .inext
    call mc_tr_step
    jmp short .inext
.iold:
    mov byte [mc_iarm + si], 2      ; no walk: the segment path owns this one,
    mov word [mc_idrw + bx], 0      ; and [mc_ipx] is what it draws to
    mov ax, [mc_isx + bx]
    mov cx, [mc_ipx + bx]
    mov dx, [mc_ipy + bx]
    mov bx, [mc_isy + bx]
    call mc_line
.inext:
    inc si
    cmp si, MC_MAXICBM
    jb .icbm

    mov al, [mc_cabm]
    call mc_setcol
    xor si, si
.abm:
    cmp byte [mc_aa + si], 1
    jne .anext
    mov di, si
    add di, di
    mov ax, [mc_asx + di]
    mov bx, [mc_asy + di]
    mov cx, [mc_atx + di]
    mov dx, [mc_aty + di]
    push di
    call mc_ablk
    call mc_tr_lay
    pop bx
    jc .aold
    mov byte [mc_aarm + si], 1
    mov ax, [mc_trlen]
    mov [mc_alen + bx], ax
    mov cx, [mc_adrw + bx]
    jcxz .anext
    call mc_tr_step
    jmp short .anext
.aold:
    mov byte [mc_aarm + si], 2
    mov word [mc_adrw + bx], 0
    mov ax, [mc_asx + bx]
    mov cx, [mc_apx + bx]
    mov dx, [mc_apy + bx]
    mov bx, [mc_asy + bx]
    call mc_line
.anext:
    inc si
    cmp si, MC_MAXABM
    jb .abm
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_wipe_trails - erase the trail of everything that died since the last frame
; in:  gfx lock held; preserves all registers
;
; This is the arcade's ERAMIS/ERAABM, and it is why a slot dies to 0FFh rather
; than to 0: the update is lock-free and may not draw, so "dead" and "erased"
; are two different states one frame apart.
; -----------------------------------------------------------------------------
mc_wipe_trails:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, MC_BG
    call mc_setcol
    xor si, si
.icbm:
    cmp byte [mc_ia + si], 0FFh
    jne .inext
    mov byte [mc_ia + si], 0
    mov di, si
    add di, di
    cmp byte [mc_iarm + si], 1
    jne .iold
    mov ax, [mc_isx + di]
    mov bx, [mc_isy + di]
    mov cx, [mc_iex + di]
    mov dx, [mc_iey + di]
    mov word [mc_drngx], MC_NODMG   ; a trail that reached the ground takes a
    mov word [mc_drnbx], MC_NODMG   ; bite out of it as it leaves - but at
    push ax                         ; the point it actually DIED, which is
    mov ax, [mc_ipy + di]           ; not the point it was aimed at when it
    cmp ax, [mc_groundy]            ; was shot down on the way (SPEC.md 48.9)
    jl .ing
    mov ax, [mc_ipx + di]
    mov [mc_drngx], ax
.ing:
    pop ax
    push si
    mov si, [mc_idrw + di]
    call mc_drn_push                ; queued? then the drain owes the erase
    pop si                          ; AND the damage marks (SPEC.md 48.15)
    jnc .iclr
    call mc_cross_need              ; the two ends bound every pixel of it
    push di
    call mc_iblk
    call mc_tr_lay                  ; the SAME line, replayed exactly
    pop bx
    jc .ignd
    mov cx, [mc_idrw + bx]
    jcxz .ignd
    call mc_tr_step
    jmp short .ignd
.iold:
    mov ax, [mc_isx + di]
    mov bx, [mc_isy + di]
    mov cx, [mc_ipx + di]
    mov dx, [mc_ipy + di]
    mov byte [mc_lfat], 1           ; see mc_lflush: the segment path's erase
    call mc_line                    ; has to be one pixel wider than its draw
    mov byte [mc_lfat], 0
.ignd:
    mov di, si
    add di, di
    mov dx, [mc_ipy + di]
    cmp dx, [mc_groundy]            ; a trail that reached the ground took a
    jl .iclr                        ; bite out of it on the way out - and the
    mov cx, [mc_ipx + di]           ; bite is at the trail's END, not across
    mov ax, cx                      ; the whole band (SPEC.md 48.9)
    sub ax, 2
    add cx, 2
    call mc_gdmg
.iclr:
    mov di, si
    add di, di
    mov word [mc_idrw + di], 0
    mov byte [mc_iarm + si], 0
.inext:
    inc si
    cmp si, MC_MAXICBM
    jb .icbm

    xor si, si
.abm:
    cmp byte [mc_aa + si], 0FFh
    jne .anext
    mov byte [mc_aa + si], 0
    mov di, si
    add di, di
    cmp byte [mc_aarm + si], 1
    jne .aold
    mov ax, [mc_asx + di]
    mov bx, [mc_asy + di]
    mov cx, [mc_atx + di]
    mov dx, [mc_aty + di]
    mov word [mc_drngx], MC_NODMG   ; an ABM detonates in the air; what its
    mov [mc_drnbx], ax              ; trail uncovers is its own launcher
    push si
    mov si, [mc_adrw + di]
    call mc_drn_push
    pop si
    jnc .aclr
    call mc_cross_need
    push di
    call mc_ablk
    call mc_tr_lay
    pop bx
    jc .abd
    mov cx, [mc_adrw + bx]
    jcxz .abd
    call mc_tr_step
    jmp short .abd
.aold:
    mov ax, [mc_asx + di]
    mov bx, [mc_asy + di]
    mov cx, [mc_apx + di]
    mov dx, [mc_apy + di]
    mov byte [mc_lfat], 1
    call mc_line
    mov byte [mc_lfat], 0
.abd:
    mov di, si
    add di, di
    mov ax, [mc_asx + di]           ; the trail starts ON the launcher
    call mc_bdirty_at
.aclr:
    mov di, si
    add di, di
    mov word [mc_adrw + di], 0
    mov byte [mc_aarm + si], 0
.anext:
    inc si
    cmp si, MC_MAXABM
    jb .abm
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_move_trails - advance every live trail to where its missile has got to
; in:  gfx lock held; preserves all registers
;
; Two or three pixels a missile a frame, which is what makes fifteen of them
; affordable. [mc_ipx]/[mc_ipy] mean "where the trail has been DRAWN to", and
; only this routine may write them - the same rule SPEC.md 44.4 gives for
; [ark_puold], and for the same reason: derive an erase from the update and it
; drifts the first time a frame is skipped. They are kept in both modes: the
; segment path draws to them, and the walk path owes them to the ground bite.
; -----------------------------------------------------------------------------
mc_move_trails:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mc_cicbm]
    call mc_setcol
    xor si, si
.icbm:
    mov al, [mc_ia + si]
    cmp al, 1
    jb .inext
    cmp al, 2
    ja .inext
    mov di, si
    add di, di
    push si
    call mc_ipix                    ; CX/DX = where it is now
    pop si
    cmp byte [mc_iarm + si], 1
    jne .iold
    mov ax, [mc_ipx + di]           ; the pixels this frame can add are bounded
    mov bx, [mc_ipy + di]           ; by the old head and the new one
    call mc_cross_need
    mov [mc_ipx + di], cx
    mov [mc_ipy + di], dx
    mov bx, di                      ; BX = slot*2 from here down
    mov di, [mc_ilen + bx]          ; +n = x is the major axis, -n = y is
    or di, di
    jns .ixmaj
    neg di
    mov cx, dx
    mov ax, [mc_isy + bx]
    jmp short .ineed
.ixmaj:
    mov ax, [mc_isx + bx]
.ineed:
    call mc_tr_need                 ; AX = pixels the head has reached
    cmp ax, [mc_idrw + bx]
    jbe .inext
    mov cx, ax
    sub cx, [mc_idrw + bx]
    mov [mc_idrw + bx], ax
    call mc_iblk                    ; DI = the walk
    call mc_dsc_add                 ; ...into this frame's ONE call
    jmp short .inext
.iold:
    cmp byte [mc_iarm + si], 0      ; not laid yet: lay it and pick a path
    jne .iseg
    mov ax, [mc_isx + di]
    mov bx, [mc_isy + di]
    push cx
    push dx
    mov cx, [mc_iex + di]
    mov dx, [mc_iey + di]
    push di
    call mc_iblk
    call mc_tr_lay
    pop di
    pop dx
    pop cx
    mov byte [mc_iarm + si], 2
    jc .iseg
    mov byte [mc_iarm + si], 1      ; laid: take it from the top, walking
    mov ax, [mc_trlen]
    mov [mc_ilen + di], ax
    jmp .icbm
.iseg:
    mov ax, [mc_ipx + di]
    mov bx, [mc_ipy + di]
    cmp ax, cx
    jne .idraw
    cmp bx, dx
    je .inext
.idraw:
    call mc_line
    mov [mc_ipx + di], cx
    mov [mc_ipy + di], dx
.inext:
    inc si
    cmp si, MC_MAXICBM
    jb .icbm
    call mc_dsc_run                 ; spent before the pen changes

    mov al, [mc_cabm]
    call mc_setcol
    xor si, si
.abm:
    cmp byte [mc_aa + si], 1
    jne .anext
    mov di, si
    add di, di
    push si
    call mc_apix
    pop si
    cmp byte [mc_aarm + si], 1
    jne .aold
    mov ax, [mc_apx + di]
    mov bx, [mc_apy + di]
    call mc_cross_need
    mov [mc_apx + di], cx
    mov [mc_apy + di], dx
    mov bx, di
    mov di, [mc_alen + bx]
    or di, di
    jns .axmaj
    neg di
    mov cx, dx
    mov ax, [mc_asy + bx]
    jmp short .aneed
.axmaj:
    mov ax, [mc_asx + bx]
.aneed:
    call mc_tr_need
    cmp ax, [mc_adrw + bx]
    jbe .anext
    mov cx, ax
    sub cx, [mc_adrw + bx]
    mov [mc_adrw + bx], ax
    call mc_ablk
    call mc_dsc_add
    jmp short .anext
.aold:
    cmp byte [mc_aarm + si], 0
    jne .aseg
    mov ax, [mc_asx + di]
    mov bx, [mc_asy + di]
    push cx
    push dx
    mov cx, [mc_atx + di]
    mov dx, [mc_aty + di]
    push di
    call mc_ablk
    call mc_tr_lay
    pop di
    pop dx
    pop cx
    mov byte [mc_aarm + si], 2
    jc .aseg
    mov byte [mc_aarm + si], 1
    mov ax, [mc_trlen]
    mov [mc_alen + di], ax
    jmp .abm
.aseg:
    mov ax, [mc_apx + di]
    mov bx, [mc_apy + di]
    cmp ax, cx
    jne .adraw
    cmp bx, dx
    je .anext
.adraw:
    call mc_line
    mov [mc_apx + di], cx
    mov [mc_apy + di], dx
.anext:
    inc si
    cmp si, MC_MAXABM
    jb .abm
    call mc_dsc_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_draw_exp / mc_draw_exp_all - the explosions
; in:  gfx lock held; preserve all registers
;
; Growing, the new disc covers the old one, so one filled circle is the whole
; frame's work. Shrinking, the ring it vacated has to be erased first. The
; colour cycles every frame either way, which is the arcade's flashing and
; costs nothing because the disc is redrawn regardless - which was true on a
; VGA machine and was the whole problem on the floor one, because it is what
; made every frame a redraw when the RADIUS had not moved. SPEC.md 48.8: on
; 1bpp or a tier-0 CPU the cycle goes, and a burst is then drawn only when
; the radius changes - three times over its 27-frame life, as mc_step3 lays
; it out - through mc_blob rather than mc_disc.
; -----------------------------------------------------------------------------
mc_draw_exp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [mc_egone], 0          ; SPEC.md 48.24: ONE erase a frame
    xor si, si
.each:
    mov di, si
    add di, di
    cmp byte [mc_ea + si], 0FFh
    je .gone
    cmp byte [mc_ea + si], 1
    jne .next
    mov bl, [mc_et + si]
    mov bh, 0
    mov bl, [mc_erad + bx]
    mov bh, 0                       ; BX = the radius it should be now
    mov cl, [mc_er + si]
    mov ch, 0                       ; CX = the radius it is drawn at
    cmp byte [mc_ecoarse], 0
    jne .coarse
    cmp bx, cx
    jae .draw
    call mc_erase_ring              ; shrank: give the ring back to the sky
.draw:
    mov [mc_er + si], bl
    call mc_exp_pen
    mov ax, [mc_ex + di]
    mov dx, [mc_ey + di]
    mov bl, [mc_er + si]
    mov bh, 0
    call mc_disc
    jmp .next

.coarse:                            ; SPEC.md 48.8: with no colour cycle the
    cmp bx, cx                      ; radius is the only thing that can have
    je .next                        ; changed, and the shipped ramp grows it
    ja .cdraw                       ; exactly ONCE (48.12)
    push bx                         ; shrank: the old blob back to the sky
    mov al, MC_BG                   ; whole. Unreachable with mc_step3 as it
    call mc_setcol                  ; ships, and kept because it is the ramp
    mov ax, [mc_ex + di]            ; table that decides that - edit the table
    mov dx, [mc_ey + di]            ; and this is what stops the burst leaving
    mov bx, cx                      ; a ring behind. The annulus was MEASURED
    call mc_blob                    ; against this pair and lost, 22 calls to
    pop bx                          ; 16, bands or not (48.11)
.cdraw:
    mov [mc_er + si], bl
    call mc_exp_pen
    mov ax, [mc_ex + di]
    mov dx, [mc_ey + di]
    mov bl, [mc_er + si]
    mov bh, 0
    call mc_blob
    jmp .next
.gone:
    mov bl, [mc_er + si]            ; SPEC.md 48.25.1: while a LIT burst still
    mov bh, 0                       ; overlaps, WAIT. Erasing now would take a
    mov di, si                      ; bite out of it and buy a repair - and a
    add di, di                      ; repair is a whole mc_blob, which is the
    mov ax, [mc_ex + di]            ; larger half of this stage. A cluster
    mov dx, [mc_ey + di]            ; expires together, so the wait is a frame
    call mc_exp_lit                 ; or two and nothing can see it
    jnc .gogo
    inc byte [mc_edef + si]
    cmp byte [mc_edef + si], MC_EGDEF
    jb .next                        ; ...but never forever: a chain of fresh
.gogo:                              ; bursts must not pin one on the screen
    cmp byte [mc_egone], 0          ; a burst already left this frame: this one
    jne .next                       ; waits, and stays 0FFh to be found again
    mov byte [mc_egone], 1
    mov byte [mc_ea + si], 0
    mov byte [mc_bqsh], MC_ERSH     ; SPEC.md 48.25: an erase may OVER-cover
    mov al, MC_BG
    call mc_setcol
    mov ax, [mc_ex + di]
    mov dx, [mc_ey + di]
    mov bl, [mc_er + si]
    mov bh, 0
    cmp byte [mc_ecoarse], 0        ; the last shape, back to sky - and it
    je .gdisc                       ; must be the SAME shape that was drawn,
    call mc_blob                    ; or the erase misses pixels or takes
    jmp short .gdone                ; ones the burst never wrote
.gdisc:
    call mc_disc
.gdone:
    mov byte [mc_bqsh], 3           ; the drawn shape's quantum back
    mov bl, [mc_er + si]            ; SPEC.md 48.22: that erase was a DISC of
    mov bh, 0                       ; background, and any other burst it
    mov ax, [mc_ex + di]            ; overlapped has a bite out of it now
    mov dx, [mc_ey + di]
    call mc_exp_hole
    mov byte [mc_er + si], 0
    mov ax, [mc_ey + di]
    mov bl, [mc_erad + 13]          ; the scaled peak radius
    mov bh, 0
    add ax, bx
    cmp ax, [mc_groundy]
    jl .next
    mov ax, [mc_ex + di]            ; it bit into the ground on the way out,
    mov cx, ax                      ; across its own diameter and no further
    sub ax, bx
    add cx, bx
    call mc_gdmg
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_exp_hole - a disc of background was just laid at AX,DX radius BX; forget
;               the drawn radius of every LIT burst it overlapped
; in:  AX/DX = centre, BX = radius; preserves all registers
;
; SPEC.md 48.22. Explosions overlap - one ABM kills a cluster and the bursts
; sit on top of each other - and mc_draw_exp's .gone erases the WHOLE blob it
; drew. SPEC.md 48.8 then never redraws the neighbour, because its radius has
; not changed, so it stays holed for the rest of its life. Marking it here
; costs one mc_blob and only when two bursts really did overlap.
; -----------------------------------------------------------------------------
; mc_exp_lit - is any LIT burst overlapping the disc at AX,DX radius BX?
; out: CF=1 yes; preserves every register (SPEC.md 48.25.1)
mc_exp_lit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mc_ehx], ax
    mov [mc_ehy], dx
    mov [mc_ehr], bx
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    mov bl, [mc_er + si]
    mov bh, 0
    or bx, bx
    jz .next
    add bx, [mc_ehr]
    mov di, si
    add di, di
    mov ax, [mc_ex + di]
    sub ax, [mc_ehx]
    jns .xp
    neg ax
.xp:
    cmp ax, bx
    ja .next
    mov cx, [mc_ey + di]
    sub cx, [mc_ehy]
    jns .yp
    neg cx
.yp:
    cmp cx, bx
    ja .next
    stc
    jmp short .out
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_exp_hole:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mc_ehx], ax
    mov [mc_ehy], dx
    mov [mc_ehr], bx
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    mov bl, [mc_er + si]
    mov bh, 0
    or bx, bx
    jz .next
    add bx, [mc_ehr]                ; two discs overlap when their centres are
    mov di, si                      ; closer than the sum of the radii, and
    add di, di                      ; the SQUARE round that is the cheap test
    mov ax, [mc_ex + di]            ; - generous, which is the safe direction:
    sub ax, [mc_ehx]                ; a needless repaint against a hole that
    jns .xp                         ; would stand for the burst's whole life
    neg ax
.xp:
    cmp ax, bx
    ja .next
    mov cx, [mc_ey + di]
    sub cx, [mc_ehy]
    jns .yp
    neg cx
.yp:
    cmp cx, bx
    ja .next
    mov byte [mc_er + si], 0        ; "not drawn", which is all it takes: the
                                    ; NEXT frame's mc_draw_exp finds target !=
                                    ; drawn and puts it back (SPEC.md 48.24)
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_draw_exp_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    mov di, si
    add di, di
    mov bl, [mc_et + si]
    mov bh, 0
    mov bl, [mc_erad + bx]
    mov [mc_er + si], bl
    call mc_exp_pen
    mov ax, [mc_ex + di]
    mov dx, [mc_ey + di]
    mov bl, [mc_er + si]
    mov bh, 0
    cmp byte [mc_ecoarse], 0
    je .disc
    call mc_blob
    jmp short .next
.disc:
    call mc_disc
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_exp_pen - the flashing colour explosion SI is on this frame
; in: SI = explosion; clobbers AX and BX like the drawing around it
mc_exp_pen:
    mov bl, [mc_et + si]
    mov bh, 0
    cmp byte [mc_ecoarse], 0
    jne .coarse
    and bl, 3
    mov al, [mc_expcol + bx]
    call mc_setcol
    ret
.coarse:
    mov bl, [mc_step3 + bx]         ; one colour per DRAWN state, and all
    mov al, [mc_col3 + bx]          ; three from SPEC.md 39.4's white class:
    call mc_setcol                  ; a dithered frame reduces to grey on the
    ret                             ; two 1bpp adapters and buys nothing

; -----------------------------------------------------------------------------
; mc_erase_ring - the annulus between radius CX (drawn) and BX (wanted)
; in:  SI = explosion, BX = new radius, CX = old; gfx lock held
; preserves all registers
; -----------------------------------------------------------------------------
mc_erase_ring:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mc_rnew], bx
    mov [mc_rold], cx
    mov di, si
    add di, di
    mov ax, [mc_ex + di]
    mov [mc_rcx], ax
    mov ax, [mc_ey + di]
    mov [mc_rcy], ax
    mov al, MC_BG
    call mc_setcol
    mov ax, [mc_rold]               ; two running half-widths, both falling
    mul ax                          ; monotonically as dy rises - mc_shrink's
    mov [mc_dr2], ax                ; O(R)-per-disc trick, run twice
    mov ax, [mc_rold]
    mov [mc_dhalf], ax
    mov ax, [mc_rnew]
    mul ax
    mov [mc_rn2], ax
    mov ax, [mc_rnew]
    mov [mc_rhalf], ax
    xor di, di                      ; DI = dy; every row is mirrored
.row:
    cmp di, [mc_rold]
    jg .out
    mov ax, di
    mul ax
    mov [mc_dd2], ax
    call mc_shrink                  ; [mc_dhalf] = the OLD half-width here
    mov ax, [mc_dhalf]
    mov [mc_rho], ax
    mov ax, di                      ; ...and the NEW one, if this row is inside
    cmp ax, [mc_rnew]               ; the new disc at all
    jbe .inner
    mov word [mc_rhn], 0FFFFh
    jmp short .have
.inner:
    call mc_shrink2
    mov ax, [mc_rhalf]
    mov [mc_rhn], ax
.have:
    mov ax, [mc_rcy]                ; the row below the centre...
    add ax, di
    mov [mc_ry], ax
    call mc_ring_band
    or di, di                       ; ...and the one above, unless they are
    jz .next                        ; the same row
    mov ax, [mc_rcy]
    sub ax, di
    mov [mc_ry], ax
    call mc_ring_band
.next:
    inc di
    jmp .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_ring_band - erase row [mc_ry] between the two half-widths
; in:  [mc_rho] outer, [mc_rhn] inner (0FFFFh = the row is wholly outside the
;      new disc); gfx lock held, pen set. Preserves all registers
mc_ring_band:
    push ax
    push bx
    push cx
    push dx
    cmp word [mc_rhn], 0FFFFh
    jne .sides
    mov ax, [mc_rcx]                ; wholly outside: one line
    sub ax, [mc_rho]
    mov cx, [mc_rcx]
    add cx, [mc_rho]
    mov bx, [mc_ry]
    mov dx, bx
    call mc_fillc
    jmp short .out
.sides:
    mov ax, [mc_rcx]                ; the left segment...
    sub ax, [mc_rho]
    mov cx, [mc_rcx]
    sub cx, [mc_rhn]
    dec cx
    cmp ax, cx
    jg .right
    mov bx, [mc_ry]
    mov dx, bx
    call mc_fillc
.right:
    mov ax, [mc_rcx]                ; ...and the right one
    add ax, [mc_rhn]
    inc ax
    mov cx, [mc_rcx]
    add cx, [mc_rho]
    cmp ax, cx
    jg .out
    mov bx, [mc_ry]
    mov dx, bx
    call mc_fillc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_shrink2 - mc_shrink again, on the second running half-width
mc_shrink2:
    push ax
    push dx
.loop:
    cmp word [mc_rhalf], 0
    jbe .out
    mov ax, [mc_rhalf]
    mul ax
    add ax, [mc_dd2]
    cmp ax, [mc_rn2]
    jbe .out
    dec word [mc_rhalf]
    jmp short .loop
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_disc - a filled circle, one hline a row
; in:  AX = centre x, DX = centre y, BX = radius; pen set; gfx lock held
; preserves all registers
;
; The half-widths are found by mc_shrink, which is a square root that costs
; O(R) for the WHOLE disc - see there. There is no circle table: the radii a
; scaled explosion needs would have made one 1KB.
; -----------------------------------------------------------------------------
mc_disc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or bx, bx
    jz .out
    cmp bx, MC_RMAXP
    jbe .r
    mov bx, MC_RMAXP
.r:
    mov [mc_dcx], ax
    mov [mc_dcy], dx
    mov [mc_dr], bx
    mov ax, bx
    mul bx
    mov [mc_dr2], ax                ; r squared, the loop's only invariant
    mov [mc_dhalf], bx              ; the running half-width, R at dy = 0
    xor di, di                      ; DI = dy, and each row is drawn twice
.row:
    cmp di, [mc_dr]
    jg .out
    mov ax, di
    mul ax
    mov [mc_dd2], ax                ; dy squared
    call mc_shrink                  ; [mc_dhalf] = isqrt(r2 - dy2)
    mov ax, [mc_dcx]
    sub ax, [mc_dhalf]
    mov cx, [mc_dcx]
    add cx, [mc_dhalf]
    mov bx, [mc_dcy]
    add bx, di
    mov dx, bx
    call mc_fillc
    or di, di                       ; row 0 is its own mirror
    jz .next
    mov ax, [mc_dcx]
    sub ax, [mc_dhalf]
    mov cx, [mc_dcx]
    add cx, [mc_dhalf]
    mov bx, [mc_dcy]
    sub bx, di
    mov dx, bx
    call mc_fillc
.next:
    inc di
    jmp short .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_shrink - walk [mc_dhalf] down until half^2 + [mc_dd2] <= [mc_dr2]
; preserves all registers
;
; This is the square root, and it is free. The half-width falls monotonically
; as dy rises, so across a whole disc it is decremented exactly R times in
; total - O(R) for the entire circle, against O(R) multiplies per ROW for an
; honest isqrt and 1KB of table for the radii a scaled explosion now needs.
; It is also why there is no circle table in this file any more.
; -----------------------------------------------------------------------------
mc_shrink:
    push ax
    push dx
.loop:
    cmp word [mc_dhalf], 0
    jbe .out
    mov ax, [mc_dhalf]
    mul ax
    add ax, [mc_dd2]
    cmp ax, [mc_dr2]
    jbe .out
    dec word [mc_dhalf]
    jmp short .loop
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_blob - the coarse explosion's disc: a few NESTED rects instead of one
;           filled row per scan line (SPEC.md 48.8.1)
; in:  AX = centre x, DX = centre y, BX = radius; pen set; gfx lock held
; preserves all registers
;
; The half-width falls monotonically as dy rises, so a filled circle is
; EXACTLY the union of one rect per distinct half-width - nested, widest and
; shortest first. Letting the drawn edge run up to R/8 + 1 pixels outside the
; true circle before a new rect is opened collapses that to four to seven
; rects at every radius this game can produce: 27 fills to 5 at Hercules'
; peak radius of 13, 39 to 6 at VGA's 19, 13 to 5 at CGA's 6, 69 to 7 at the
; MC_RMAXP ceiling. One gfx_fill costs about what an 8x8 glyph cell does on
; the floor machine - it is per-call overhead, not the bytes written - so
; that ratio is the whole optimisation.
;
; The error is ALWAYS OUTWARD - checked at every radius, never once inward -
; which is the direction that matters: mc_do_damage already tests r+2 against
; the object's own half size, so the burst never kills anything it does not
; visibly touch. It costs one mc_shrink walk - the same O(R)-for-the-whole-
; disc square root mc_disc uses, and for the same reason.
; -----------------------------------------------------------------------------
mc_blob:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or bx, bx
    jz .out
    cmp bx, MC_RMAXP
    jbe .r
    mov bx, MC_RMAXP
.r:
    mov [mc_dcx], ax
    mov [mc_dcy], dx
    mov [mc_dr], bx
    mov ax, bx
    mul bx
    mov [mc_dr2], ax                ; r squared, the loop's only invariant
    mov [mc_dhalf], bx              ; the running half-width, R at dy = 0
    mov [mc_bw], bx                 ; ...and the half-width of the rect that
                                    ; is currently open, which lags it
    mov ax, bx
    mov cl, [mc_bqsh]               ; the quantum: R >> [mc_bqsh], + 1. THREE
    shr ax, cl                      ; for a drawn burst; an ERASE may be
    inc ax                          ; coarser (SPEC.md 48.25)
    mov [mc_bq], ax
    mov word [mc_bprev], -1         ; no band closed yet (SPEC.md 48.10)
    mov di, 1                       ; DI = dy; row 0 is inside the first rect
.row:
    cmp di, [mc_dr]
    jg .last
    mov ax, di
    mul ax
    mov [mc_dd2], ax
    call mc_shrink                  ; [mc_dhalf] = isqrt(r2 - dy2)
    mov ax, [mc_dhalf]
    add ax, [mc_bq]
    cmp ax, [mc_bw]                 ; has the true edge fallen a whole
    ja .next                        ; quantum inside what we are drawing?
    mov bx, di
    dec bx
    call mc_blob_rect               ; then close the open rect one row back...
    mov ax, [mc_dhalf]
    mov [mc_bw], ax                 ; ...and open one at the new width
.next:
    inc di
    jmp short .row
.last:
    mov bx, [mc_dr]                 ; the innermost rect reaches the poles,
    call mc_blob_rect               ; so the blob is as TALL as the circle
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_blob_rect - one BAND of the blob: half-width [mc_bw], out to half-height
;                BX, starting just past [mc_bprev]. Preserves all registers.
;
; SPEC.md 48.10. These were NESTED rects - each one spanning the blob's full
; height at its own width - which is the same pixels and a very different
; price. A gfx_fill costs 756us of arriving plus 177us PER SCAN LINE (the
; 5150 field set, PERFORMANCE.md Part 2), and nested rects overlap: at
; fullscreen Hercules' peak radius of 19 they wrote 184 scan lines to cover a
; 39-row disc. Six calls instead of thirty-nine looked like a win and was
; not - 37.1 ms against mc_disc's 36.4. Bands write each row ONCE: eleven
; calls, thirty-nine rows, 15.2 ms.
mc_blob_rect:
    push ax
    push bx
    push cx
    push dx
    cmp word [mc_bprev], 0          ; the first band is the centre one, and it
    jge .ring                       ; is the only one that is a single rect
    mov ax, [mc_dcy]
    sub ax, bx
    mov dx, [mc_dcy]
    add dx, bx
    mov [mc_bprev], bx
    mov bx, ax
    mov ax, [mc_dcx]
    sub ax, [mc_bw]
    mov cx, [mc_dcx]
    add cx, [mc_bw]
    call mc_fillc
    jmp short .out
.ring:
    push bx
    mov ax, [mc_bprev]              ; the band below the centre
    inc ax
    mov dx, [mc_dcy]
    add dx, bx
    mov bx, [mc_dcy]
    add bx, ax
    mov ax, [mc_dcx]
    sub ax, [mc_bw]
    mov cx, [mc_dcx]
    add cx, [mc_bw]
    call mc_fillc
    pop bx
    push bx
    mov ax, [mc_bprev]              ; ...and its mirror above
    inc ax
    mov dx, [mc_dcy]
    sub dx, ax
    mov ax, [mc_dcy]
    sub ax, bx
    mov bx, ax
    mov ax, [mc_dcx]
    sub ax, [mc_bw]
    mov cx, [mc_dcx]
    add cx, [mc_bw]
    call mc_fillc
    pop bx
    mov [mc_bprev], bx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_draw_sat / mc_draw_sat_full - the satellite or bomber
; in:  gfx lock held; preserve all registers
; -----------------------------------------------------------------------------
MC_SATW equ 13
MC_SATH equ 7

mc_draw_sat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [mc_sata], 0FFh
    je .gone
    cmp byte [mc_sata], 1
    jb .out
    cmp byte [mc_sata], 2
    ja .out
    mov ax, [mc_satpx]              ; erase where it was drawn...
    cmp ax, [mc_satx]
    je .out
    call mc_sat_erase
    call mc_sat_shape               ; ...and draw where it is
    mov ax, [mc_satx]
    mov [mc_satpx], ax
    jmp short .out
.gone:
    call mc_sat_erase
    mov byte [mc_sata], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_draw_sat_full:
    push ax
    cmp byte [mc_sata], 1
    jb .out
    cmp byte [mc_sata], 2
    ja .out
    call mc_sat_shape
    mov ax, [mc_satx]
    mov [mc_satpx], ax
.out:
    pop ax
    ret

mc_sat_erase:
    push ax
    push bx
    push cx
    push dx
    mov al, MC_BG
    call mc_setcol
    mov ax, [mc_satpx]
    sub ax, MC_SATW
    mov bx, [mc_saty]
    sub bx, MC_SATH
    mov cx, [mc_satpx]
    add cx, MC_SATW
    mov dx, [mc_saty]
    add dx, MC_SATH
    call mc_fillc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_sat_shape - a satellite (a body with two panels) or a bomber (a fuselage
; with swept wings). Two SHAPES, not two colours: on 1bpp they are the only
; thing telling them apart, and the arcade scores them the same anyway.
mc_sat_shape:
    push ax
    push bx
    push cx
    push dx
    mov al, [mc_cicbm]
    call mc_setcol
    cmp byte [mc_sata], 2
    je .bomber
    mov ax, [mc_satx]               ; satellite: a 5x5 body...
    sub ax, 2
    mov bx, [mc_saty]
    sub bx, 2
    mov cx, ax
    add cx, 4
    mov dx, bx
    add dx, 4
    call mc_fillc
    mov ax, [mc_satx]               ; ...and two panels either side
    sub ax, 9
    mov bx, [mc_saty]
    dec bx
    mov cx, ax
    add cx, 5
    mov dx, bx
    inc dx
    call mc_fillc
    mov ax, [mc_satx]
    add ax, 4
    mov bx, [mc_saty]
    dec bx
    mov cx, ax
    add cx, 5
    mov dx, bx
    inc dx
    call mc_fillc
    jmp short .out
.bomber:
    mov ax, [mc_satx]               ; bomber: a long fuselage...
    sub ax, 8
    mov bx, [mc_saty]
    dec bx
    mov cx, ax
    add cx, 16
    mov dx, bx
    inc dx
    call mc_fillc
    mov ax, [mc_satx]               ; ...a fin, and a tail
    sub ax, 1
    mov bx, [mc_saty]
    sub bx, 4
    mov cx, ax
    add cx, 2
    mov dx, [mc_saty]
    call mc_fillc
    mov ax, [mc_satx]
    sub ax, 8
    mov bx, [mc_saty]
    sub bx, 3
    mov cx, ax
    add cx, 1
    mov dx, [mc_saty]
    call mc_fillc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_cross_on / mc_cross_off - the aiming crosshair, as an XOR overlay
; in:  gfx lock held; preserve all registers
;
; XOR because the crosshair stands on top of trails, explosions and the ground
; alike, and there is nothing behind it to restore: drawing it twice puts back
; whatever was there. Nothing else may draw while it is on the screen - which
; is the only condition an XOR overlay needs - the same argument SPEC.md 32
; makes one level up for the window manager's own drag outline.
;
; It used to be the LAST thing drawn each frame and the FIRST thing undone the
; next, unconditionally, which is eight gfx_xor_fill calls a frame whether the
; mouse moved or not: the field log measured 8.6 ms of EVERY frame on a 4.77MHz
; Hercules machine, idle frames included, against a 55 ms tick. It is four
; one-pixel arms, so there is nothing to win inside the calls - only in not
; making them (SPEC.md 48.11).
;
; So it stays on screen across frames now, and comes off in exactly the two
; cases that need it: the mouse moved (mc_cross_moved, at the top of the frame,
; because the crosshair has to go back somewhere else), or something is about
; to draw through it (mc_cross_need, from the content primitives). The second
; is what makes it exact rather than approximate - the erase happens BEFORE the
; overdraw, while the crosshair is still whole, so the XOR is undoing what it
; drew. A frame that touches neither pays four compares per primitive instead
; of eight far calls, and mc_cross_on at the end is a compare.
; -----------------------------------------------------------------------------
MC_CHARM  equ 8                     ; arm length. Big enough to read AROUND the
                                    ; system arrow, which the kernel keeps
                                    ; drawing at the same point (SPEC.md 11.2:
                                    ; even fullscreen, the cursor stays live)
                                    ; - a short crosshair simply hides under it
                                    ;
                                    ; TWO bars, not four arms (SPEC.md 48.18).
                                    ; XOR is its own inverse, so where the two
                                    ; cross the second undoes the first and the
                                    ; centre hole comes back for free - one
                                    ; pixel of it rather than the three-pixel
                                    ; gap the four arms left. Two calls instead
                                    ; of four, on EVERY frame the mouse moves:
                                    ; a field log put this at 6.8 ms of a 63 ms
                                    ; frame with no content in it at all

mc_cross_on:
    push ax
    cmp byte [mc_chshown], 0
    jne .out
    mov ax, [mc_chx]
    mov [mc_chpx], ax
    mov ax, [mc_chy]
    mov [mc_chpy], ax
    call mc_cross_xor
    mov byte [mc_chshown], 1
.out:
    pop ax
    ret

mc_cross_off:
    cmp byte [mc_chshown], 0
    je .out
    mov byte [mc_chshown], 0
    call mc_cross_xor
.out:
    ret

; mc_cross_moved - take the crosshair off if it can no longer stay where it is.
; Called once at the top of a part frame; preserves every register.
mc_cross_moved:
    push ax
    mov ax, [mc_chx]
    cmp ax, [mc_chpx]
    jne .off
    mov ax, [mc_chy]
    cmp ax, [mc_chpy]
    je .out
.off:
    call mc_cross_off
.out:
    pop ax
    ret

; mc_cross_need - (AX,BX)-(CX,DX) in CONTENT coordinates is about to be drawn;
; if it reaches the crosshair, the crosshair comes off first. Ends in any
; order - a line hands over its two endpoints - and unclamped, because an
; over-large rect only ever costs a redundant erase. Preserves every register.
mc_cross_need:
    cmp byte [mc_chshown], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    cmp ax, cx
    jle .xok
    xchg ax, cx
.xok:
    cmp bx, dx
    jle .yok
    xchg bx, dx
.yok:
    mov si, [mc_chpx]               ; signed throughout: content coordinates
    sub si, MC_CHARM                ; go negative and mc_clamp is downstream
    cmp cx, si
    jl .miss
    mov si, [mc_chpx]
    add si, MC_CHARM
    cmp ax, si
    jg .miss
    mov si, [mc_chpy]
    sub si, MC_CHARM
    cmp dx, si
    jl .miss
    mov si, [mc_chpy]
    add si, MC_CHARM
    cmp bx, si
    jg .miss
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp mc_cross_off                ; it is still whole, so this erase is exact
.miss:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

mc_cross_xor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mc_chpx]               ; ONE horizontal bar, straight through
    sub ax, MC_CHARM
    mov bx, [mc_chpy]
    mov cx, [mc_chpx]
    add cx, MC_CHARM
    mov dx, bx
    call mc_xorc
    mov ax, [mc_chpx]               ; ...and one vertical, likewise
    mov bx, [mc_chpy]
    sub bx, MC_CHARM
    mov cx, ax
    mov dx, [mc_chpy]
    add dx, MC_CHARM
    call mc_xorc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_draw_status - the strip across the top: score, high score, wave, multiplier
; in:  gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
mc_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mc_statush]            ; the baseline, centred in the strip
    sub ax, 8
    shr ax, 1
    mov [mc_texty], ax

    ; SPEC.md 48.9: three OPAQUE runs, not one full-width erase and then the
    ; figures. [mc_sdirty] is set on every kill, so the old shape blanked the
    ; whole strip and re-lettered it several times a second - PERFORMANCE.md
    ; Part 1's erase-and-letter pair, in its classic form, on the widest
    ; element this game draws. font_run (SPEC.md 6.1) is the fix: each cell
    ; goes from what it held to what it will hold in one store, so the strip
    ; is never momentarily blank. Every field is space-padded to a FIXED span
    ; because that padding IS the erase - a score that loses a digit has the
    ; cell it vacated painted background by the same call that draws the rest.
    ;
    ; Each x is a multiple of 8 in CONTENT coordinates, which is what the
    ; fast path wants (SPEC.md 6.1.2 - one store a cell row, no shift, no
    ; read). Fullscreen puts the content origin at 0, so there it is aligned
    ; absolutely and the run is genuinely flash-free; windowed it depends on
    ; where the window sits, and the fallback costs what the old pair did
    ; while flashing only its own field instead of the whole strip.
    mov ax, [mc_scorelo]
    mov dx, [mc_scorehi]
    call mc_num32
    mov si, [mc_numptr]
    mov di, mc_sbuf
    mov cx, MC_SFW
    call mc_field
    mov si, mc_sbuf
    mov di, mc_shad0
    mov bx, MC_SFW
    xor cx, cx                      ; hard left, already aligned
    mov dx, [mc_texty]
    mov al, CWHITE
    mov ah, MC_BG
    call mc_srun

    mov ax, [mc_hilo]               ; the high score, centred on its SPAN so
    mov dx, [mc_hihi]               ; the field does not move as it grows
    call mc_num32
    mov si, [mc_numptr]
    mov di, mc_sbuf
    mov cx, MC_SFW
    call mc_field
    mov si, mc_sbuf
    mov di, mc_shad1
    mov bx, MC_SFW
    mov cx, [mc_cw]
    sub cx, MC_SFW * 8
    shr cx, 1
    and cx, 0xFFF8
    mov dx, [mc_texty]
    mov al, CYELLOW
    mov ah, MC_BG
    call mc_srun

    ; Wave and multiplier, right-aligned. All THREE figures in this strip are
    ; drawn from SPEC.md 39.4's white class (15, 14, 12) and none from its
    ; dither class, because a dithered 8x8 glyph on a black field loses the
    ; half of each stroke the pattern masks out - and a 1px stroke has nothing
    ; left. This was CLGREEN, and on CGA the wave counter simply was not there:
    ; not faint, ABSENT, with zero lit pixels across the hundred columns it
    ; occupied. Colour is decoration here; the strip has to be legible on all
    ; three adapters.
    mov di, mc_wbuf
    mov byte [di], 'W'
    inc di
    mov al, [mc_wave]
    mov ah, 0
    call mc_num16
    mov si, [mc_numptr]
    call mc_wcat
    mov byte [di], ' '
    inc di
    mov byte [di], 'x'
    inc di
    mov al, [mc_mult]
    mov ah, 0
    call mc_num16
    mov si, [mc_numptr]
    call mc_wcat
    mov byte [di], 0
    mov si, mc_wbuf
    mov di, mc_sbuf
    mov cx, MC_WFW
    call mc_field
    mov si, mc_sbuf
    mov di, mc_shad2
    mov bx, MC_WFW
    mov cx, [mc_cw]
    sub cx, MC_WFW * 8
    and cx, 0xFFF8
    mov dx, [mc_texty]
    mov al, CLRED
    mov ah, MC_BG
    call mc_srun
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_wcat - append the NUL string at SI to DI, leaving DI past it
; preserves every register but DI/SI
mc_wcat:
    push ax
.each:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc di
    inc si
    jmp short .each
.out:
    pop ax
    ret

; mc_field - SI -> a NUL string, into the CX-cell buffer at DI, space padded
; and NUL terminated. The padding is what erases a field that got SHORTER.
; preserves all registers
mc_field:
    push ax
    push cx
    push si
    push di
.copy:
    or cx, cx
    jz .term
    mov al, [si]
    or al, al
    jz .pad
    inc si
    mov [di], al
    inc di
    dec cx
    jmp short .copy
.pad:
    mov byte [di], ' '
    inc di
    dec cx
    jnz .pad
.term:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_srun - mc_runc, but only the cells of the field that CHANGED
; in:  SI = the new text, DI = its shadow, BX = the field width in CELLS,
;      CX = x, DX = y, AL = ink, AH = background
; preserves all registers
;
; The strip is three fields of 10, 10 and 9 cells and [mc_sdirty] is set on
; every kill, so a kill re-lettered all TWENTY-NINE - about 29 ms on a 4.77MHz
; Hercules machine (PERFORMANCE.md's ~1 ms a cell), in one frame, several
; times a second. A field log caught it as `rst` at 28-37 ms in the worst
; frame of eight different seconds, with only eleven fills in them.
;
; Only the score changes on a kill, and usually only its last digit or two. So
; each field carries the text it was last DRAWN with, and what goes out is the
; span from the first differing cell to the last - one or two cells for a
; score bump, nothing at all for the two fields that did not move.
;
; This subsumes a per-field dirty bit rather than needing one: a field whose
; text is unchanged draws nothing, whatever the flag said.
; -----------------------------------------------------------------------------
mc_srun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mc_sax], ax
    mov [mc_sdx], dx
    mov [mc_scx], cx
    mov [mc_ssi], si
    mov [mc_sfw], bx
    mov word [mc_sf0], -1
    mov word [mc_sf1], -1
    xor bx, bx
.scan:
    cmp bx, [mc_sfw]
    jae .done
    mov al, [si + bx]
    cmp al, [di + bx]
    je .nx
    cmp word [mc_sf0], -1
    jne .lo
    mov [mc_sf0], bx
.lo:
    mov [mc_sf1], bx
.nx:
    inc bx
    jmp short .scan
.done:
    cmp word [mc_sf0], -1
    je .out                         ; unchanged: nothing goes out at all
    xor bx, bx                      ; the shadow becomes what is on screen
.cp:
    cmp bx, [mc_sfw]
    jae .cpd
    mov al, [si + bx]
    mov [di + bx], al
    inc bx
    jmp short .cp
.cpd:
    mov bx, [mc_sf0]
    mov si, [mc_ssi]
    add si, bx                      ; the run starts at the first change...
    mov ax, bx
    mov cl, 3
    shl ax, cl
    add ax, [mc_scx]
    mov cx, ax
    mov ax, [mc_sf1]                ; ...and ends at the last
    sub ax, [mc_sf0]
    inc ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    mov [mc_runw], ax
    mov dx, [mc_sdx]
    mov ax, [mc_sax]
    call mc_runc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_sshad_clr - forget what the strip holds, so the next paint draws it whole
; preserves all registers
mc_sshad_clr:
    push cx
    push di
    mov di, mc_shad0
    mov cx, MC_SFW + MC_SFW + MC_WFW + 3
.z:
    mov byte [di], 0FFh             ; a byte no field can contain, so every
    inc di                          ; cell reads as changed
    loop .z
    pop di
    pop cx
    ret

; mc_runc - one opaque run in CONTENT coordinates (the mc_textc of SPEC.md 6.1)
; in:  SI = string, CX = x, DX = y, AL = ink, AH = background, [mc_runw] = the
;      field's PIXEL width (the Mode X fallback has to erase it by hand)
; preserves all registers.  On the Mode X surface the kernel's drawing slots
; are off limits (SPEC.md 53.7), so it falls back to the twins there.
mc_runc:
    push cx
    push dx
    push ax
    push bx
    mov ax, cx                      ; the run's own band, for the overlay
    mov bx, dx
    add cx, [mc_runw]
    dec cx
    add dx, 7
    call mc_cross_need
    mov cx, ax
    mov dx, bx
    pop bx
    pop ax
    cmp byte [mc_fsx], 0
    jne .fsx
    add cx, [mc_ox]
    add dx, [mc_oy]
    call OSAPI_FONT_RUN
    jmp short .out
.fsx:
    push ax
    mov al, ah
    call mc_setcol
    push cx
    push dx
    push si
    mov ax, cx                      ; the field's own span, cleared
    mov bx, dx
    mov cx, ax
    add cx, [mc_runw]
    dec cx
    mov dx, bx
    add dx, 7
    call mc_fillc
    pop si
    pop dx
    pop cx
    pop ax
    call mc_setcol
    call mc_textc
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; mc_draw_msg - the banner in the middle of the sky
; in:  gfx lock held; preserves all registers
;
; SPEC.md 48.9.3. This ran EVERY FRAME the banner was showing, and every one
; of those frames erased a full-content-width band and then lettered the text
; back into it - PERFORMANCE.md Part 1's erase-and-letter pair at 18 Hz, for
; the whole of M_READY and the whole end-of-wave tally. Its own comment called
; that affordable, which it was in WORK and never was on the glass: the strip
; is blank between the fill and the glyphs, and nothing that measures time
; reports it.
;
; Two things fix it and both are needed. [mc_msgp] is the banner that is
; ACTUALLY on screen, so an unchanged one costs a compare and nothing else -
; which is most frames. And when it does change it is ONE opaque run (SPEC.md
; 6.1), centred inside a fixed span of spaces, so the padding erases whatever
; was there and no cell is ever momentarily blank. Taking the banner down is
; the same call with an empty string.
; -----------------------------------------------------------------------------
mc_draw_msg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, 0
    mov al, [mc_mode]
    cmp al, M_READY
    je .ready
    cmp al, M_PAUSE
    je .pause
    cmp al, M_OVER
    je .over
    cmp al, M_ENDA
    je .bonus
    cmp al, M_ENDC
    je .bonus
    jmp short .have
.ready:
    mov si, mc_s_ready
    jmp short .have
.pause:
    mov si, mc_s_pause
    jmp short .have
.over:
    mov si, mc_s_over
    jmp short .have
.bonus:
    mov si, mc_s_bonus
.have:
    cmp si, [mc_msgp]               ; the one already on screen? then the
    jne .paint                      ; frame owes this strip nothing at all -
    mov al, [mc_mode]               ; UNLESS something can still draw over it.
    cmp al, M_ENDA                  ; SPEC.md 48.9.1's trap in miniature: the
    je .paint                       ; every-frame redraw this replaced was
    cmp al, M_ENDC                  ; also what healed a trail crossing the
    je .paint                       ; banner. In M_READY and M_PAUSE nothing
    cmp al, M_OVER                  ; moves, so a compare is the whole cost;
    je .paint                       ; in the three modes where ABMs are still
    jmp short .out                  ; finishing it repaints - which is now
.paint:                             ; opaque, so it costs work and not a flash
    mov [mc_msgp], si
    call mc_msgcentre               ; mc_msgbuf = the text inside MC_MSGW
    mov si, mc_msgbuf               ; cells of spaces, or all spaces for none
    mov word [mc_runw], MC_MSGW * 8
    mov cx, [mc_cw]
    sub cx, MC_MSGW * 8
    jns .xok
    xor cx, cx
.xok:
    shr cx, 1
    and cx, 0xFFF8                  ; the fast path wants a byte column
    call mc_msgy
    mov dx, ax
    mov al, CWHITE
    mov ah, MC_BG
    call mc_runc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_msgcentre - SI -> a NUL string (or 0), centred in MC_MSGW cells of
; spaces at mc_msgbuf, NUL terminated. Preserves all registers.
mc_msgcentre:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, mc_msgbuf               ; the whole field, spaces
    mov cx, MC_MSGW
.blank:
    mov byte [di], ' '
    inc di
    loop .blank
    mov byte [di], 0
    or si, si
    jz .out
    mov bx, si                      ; BX = the string, CX = its length
    xor cx, cx
.len:
    cmp byte [si], 0
    je .haveln
    inc si
    inc cx
    jmp short .len
.haveln:
    cmp cx, MC_MSGW                 ; a banner longer than the field is left
    ja .out                         ; flush left rather than centred badly
    mov ax, MC_MSGW
    sub ax, cx
    shr ax, 1                       ; AX = the left margin, in cells
    mov di, mc_msgbuf
    add di, ax
    mov si, bx
.copy:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc di
    inc si
    jmp short .copy
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_msgy:
    push bx
    mov ax, [mc_groundy]
    add ax, [mc_topy]
    shr ax, 1
    sub ax, 4
    pop bx
    ret

; -----------------------------------------------------------------------------
; mc_line - a Bresenham line that COALESCES horizontal runs into hlines
; in:  AX = x0, BX = y0, CX = x1, DX = y1 (content coords); pen set; gfx lock
;      held
; preserves all registers
;
; There is no line primitive in the API, and a per-pixel OSAPI_GFX_PIXEL is a
; far call each. A steep line - which is what a falling missile draws - still
; costs one call a row, but a shallow one costs one call for the whole run,
; and a trail erase (the only long line this game draws) is shallow far more
; often than it is steep.
; -----------------------------------------------------------------------------
mc_line:
    push ax
    push bx
    push cx
    push dx
    push si
    call mc_cross_need              ; the two endpoints bound the line
    cmp byte [mc_fsx], 0            ; SPEC.md 5.6 does the whole line in one
    jne .own                        ; call now - but not on the Mode X surface
    or ax, ax                       ; (53.7: the drawing slots are off-limits
    js .own                         ; there) and not for a line with an end
    or bx, bx                       ; outside our content, where the kernel
    js .own                         ; would clip to the SCREEN and paint over
    or cx, cx                       ; the desktop. mc_clamp is what stops that
    js .own                         ; today and a whole-line call cannot carry
    or dx, dx                       ; it - so the rare line that needs it
    js .own                         ; keeps the per-run path below
    cmp ax, [mc_cw]
    jge .own
    cmp cx, [mc_cw]
    jge .own
    cmp bx, [mc_ch]
    jge .own
    cmp dx, [mc_ch]
    jge .own
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    mov si, 0
    cmp byte [mc_lfat], 0           ; the erase owes the dilation (5.6.5): we
    je .thin                        ; DRAW in per-frame segments and erase in
    mov si, 1                       ; one long line, and those two Bresenhams
.thin:                              ; disagree by a pixel
    call OSAPI_GFX_LINE
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.own:
    push di
    mov [mc_lx], ax
    mov [mc_ly], bx
    mov [mc_lx1], cx
    mov [mc_ly1], dx

    mov ax, cx                      ; dx = |x1 - x0|, sx = sign
    sub ax, [mc_lx]
    mov word [mc_lsx], 1
    or ax, ax
    jns .dx
    neg ax
    mov word [mc_lsx], -1
.dx:
    mov [mc_ldx], ax
    mov ax, dx                      ; dy = -|y1 - y0|, sy = sign
    sub ax, [mc_ly]
    mov word [mc_lsy], 1
    or ax, ax
    jns .dy
    neg ax
    mov word [mc_lsy], -1
.dy:
    neg ax
    mov [mc_ldy], ax
    mov ax, [mc_ldx]
    add ax, [mc_ldy]
    mov [mc_lerr], ax

    mov ax, [mc_lx]                 ; the run being accumulated starts here
    mov [mc_lrun], ax
.step:
    mov ax, [mc_lx]                 ; arrived? the last run still has to go out
    cmp ax, [mc_lx1]
    jne .go
    mov ax, [mc_ly]
    cmp ax, [mc_ly1]
    je .fin
.go:
    mov ax, [mc_lerr]               ; e2 = 2 * err, and BOTH tests read the
    add ax, ax                      ; same e2 - which is what makes this
    mov si, ax                      ; Bresenham and not two of them
    xor di, di                      ; DI = "y is going to move"
    cmp si, [mc_ldx]                ; e2 <= dx?
    jg .nox
    mov di, 1
.nox:
    or di, di                       ; ...and if it is, THIS row's run ends at
    jz .xstep                       ; the x we have NOW, before the x step: a
    call mc_lflush                  ; diagonal step never plots the corner
.xstep:
    cmp si, [mc_ldy]                ; e2 >= dy (dy is held negative)
    jl .ystep
    mov ax, [mc_lerr]
    add ax, [mc_ldy]
    mov [mc_lerr], ax
    mov ax, [mc_lx]
    add ax, [mc_lsx]
    mov [mc_lx], ax
.ystep:
    or di, di
    jz .step
    mov ax, [mc_lerr]
    add ax, [mc_ldx]
    mov [mc_lerr], ax
    mov ax, [mc_ly]
    add ax, [mc_lsy]
    mov [mc_ly], ax
    mov ax, [mc_lx]                 ; the new row's run starts where we landed
    mov [mc_lrun], ax
    jmp short .step
.fin:
    call mc_lflush
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_lflush - emit the run from [mc_lrun] to [mc_lx] on row [mc_ly]
;
; In FAT mode ([mc_lfat]) the run is grown by one pixel in every direction. It
; is the same number of gfx_fill calls - a run is a rect either way - and it is
; what makes an erase actually erase.
;
; THE TRAIL IS DRAWN AS ONE SEGMENT PER FRAME AND ERASED AS ONE WHOLE LINE, and
; those two rasterizations are not the same pixels. Each per-frame segment is
; its own Bresenham between two rounded endpoints; the whole-line Bresenham
; runs between the extremes. Both approximate the same straight line, so they
; never differ by more than a pixel in the minor axis - but "never more than a
; pixel" still left HALF of a measured 217-pixel trail on the screen (104 of
; them), which is what turned every dead missile's trail into a dashed line
; that never went away. Growing the erase by one pixel each way covers exactly
; that error, and costs nothing.
;
; The honest alternative - replaying the erase segment by segment - needs the
; frame count since launch AND breaks the moment mc_render skips a frame or a
; smart bomb re-aims, because then the drawn segments are not the ones a replay
; would produce. This has neither failure mode.
mc_lflush:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mc_lrun]
    mov cx, [mc_lx]
    cmp ax, cx
    jbe .ord
    xchg ax, cx
.ord:
    mov bx, [mc_ly]
    mov dx, bx
    cmp byte [mc_lfat], 0
    je .thin
    dec ax
    inc cx
    dec bx
    inc dx
.thin:
    call mc_fillc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_num16 / mc_num32 - a value as decimal, NUL-terminated
; mc_num16: AX = value; mc_num32: DX:AX = value
; out: [mc_numptr] -> the first digit; both preserve all registers
; -----------------------------------------------------------------------------
mc_num16:
    push dx
    xor dx, dx
    call mc_num32
    pop dx
    ret

mc_num32:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, mc_numbuf + 11
    mov byte [di], 0
    mov bx, 10
.dig:
    mov cx, ax                      ; DX:AX / 10, the two-step way: the HIGH
    mov ax, dx                      ; half first, and its remainder becomes the
    xor dx, dx                      ; high half of the second divide, which is
    div bx                          ; what keeps a 32-bit quotient out of the
    mov si, ax                      ; 16-bit one `div` can produce
    mov ax, cx
    div bx                          ; DX = the digit, AX = the low quotient
    add dl, '0'
    dec di
    mov [di], dl
    mov dx, si                      ; the quotient, back in DX:AX
    mov cx, ax
    or ax, dx
    mov ax, cx
    jnz .dig
    mov [mc_numptr], di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_fillc / mc_framec / mc_textc / mc_xorc - the four primitives, in CONTENT
; coordinates. Everything above measures from the content origin; only these
; add it - and only these CLAMP to the content box.
;
; The clamp is not decoration. gfx_fill clips to the SCREEN, not to our window,
; and an explosion at the top edge or a trail erase past the left one would
; otherwise paint over the desktop - or, under a W_PAINT with no clip armed,
; over another window entirely.
;
; mc_fillc/framec: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; mc_textc:        SI = string, CX = x, DX = y
; all preserve every register
; -----------------------------------------------------------------------------
mc_fillc:
    push ax
    push bx
    push cx
    push dx
    call mc_cross_need              ; the overlay comes off before we draw
    call mc_clamp                   ; through it, never after (SPEC.md 48.11)
    jc .out
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    cmp byte [mc_fsx], 0            ; on Mode X the drawing slots are
    jne .fsx                        ; off-limits (SPEC.md 53.7) - the twin
    call OSAPI_GFX_FILL             ; writes A000 itself
    jmp short .out
.fsx:
    call mx_fill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_xorc:
    push ax
    push bx
    push cx
    push dx
    call mc_clamp
    jc .out
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    cmp byte [mc_fsx], 0
    jne .fsx
    call OSAPI_GFX_XOR_FILL
    jmp short .out
.fsx:
    call mx_xor
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_framec:
    push ax
    push bx
    push cx
    push dx
    call mc_cross_need
    call mc_clamp
    jc .out
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    cmp byte [mc_fsx], 0
    jne .fsx
    call OSAPI_GFX_FRAME
    jmp short .out
.fsx:
    call mx_frame
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_clamp - cut (AX,BX)-(CX,DX) to the content box
; out: CF=1 nothing is left of it; clobbers nothing but AX/BX/CX/DX
mc_clamp:
    or ax, ax
    jns .x1
    xor ax, ax
.x1:
    or bx, bx
    jns .y1
    xor bx, bx
.y1:
    push si
    mov si, [mc_cw]
    dec si
    cmp cx, si
    jle .x2
    mov cx, si
.x2:
    mov si, [mc_ch]
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

mc_textc:
    push cx
    push dx
    add cx, [mc_ox]
    add dx, [mc_oy]
    cmp byte [mc_fsx], 0
    jne .fsx
    call OSAPI_FONT_STR
    jmp short .out
.fsx:
    call mx_text
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; mc_setcol - every colour change comes through here. The kernel's
; [gfx_color] cannot be read back, so the Mode X twins take theirs from the
; shadow; the API call stays (state only, no drawing - and the desktop path
; still needs it).
; -----------------------------------------------------------------------------
mc_setcol:
    mov [mc_col], al
    call OSAPI_SET_COLOR
    ret

; -----------------------------------------------------------------------------
; mx_fill / mx_xor / mx_frame / mx_text - the Mode X twins (SPEC.md 53.4).
; Same registers as the primitives above, coordinates already absolute
; (origin 0 on this surface). Unchained planar: a column lives in plane
; x & 3, four pixels to a byte address, stride 80 - one map-mask OUT per
; COLUMN, not per pixel, and the row walk inside it is adds alone.
; -----------------------------------------------------------------------------
mx_fill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [mc_fsi+FSI_SEG]
    mov si, ax                      ; SI = the column walker
.col:
    push cx
    push dx
    mov cx, si
    and cl, 3
    mov ah, 1
    shl ah, cl                      ; AH = the column's plane
    mov al, 2                       ; SEQ index 2: map mask
    mov dx, 0x3C4
    out dx, ax
    mov di, bx                      ; DI = y1*80 + x/4
    mov cl, 4
    shl di, cl
    mov ax, di
    shl di, 1
    shl di, 1
    add di, ax
    mov ax, si
    shr ax, 1
    shr ax, 1
    add di, ax
    pop dx
    pop cx
    push bx
    mov al, [mc_col]
.row:
    mov [es:di], al
    add di, 80
    inc bx
    cmp bx, dx
    jle .row
    pop bx
    inc si
    cmp si, cx
    jle .col
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mx_xor:                             ; reads need GC index 4 (read map select,
    push ax                         ; a plane NUMBER) beside the write mask
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [mc_fsi+FSI_SEG]
    mov si, ax
.col:
    push cx
    push dx
    mov cx, si
    and cl, 3
    mov ah, 1
    shl ah, cl
    mov al, 2
    mov dx, 0x3C4
    out dx, ax                      ; write: map mask
    mov ah, cl
    mov al, 4
    mov dx, 0x3CE
    out dx, ax                      ; read: map select = the plane number
    mov di, bx
    mov cl, 4
    shl di, cl
    mov ax, di
    shl di, 1
    shl di, 1
    add di, ax
    mov ax, si
    shr ax, 1
    shr ax, 1
    add di, ax
    pop dx
    pop cx
    push bx
.row:
    mov al, [es:di]
    xor al, [mc_col]
    mov [es:di], al
    add di, 80
    inc bx
    cmp bx, dx
    jle .row
    pop bx
    inc si
    cmp si, cx
    jle .col
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mx_frame:                           ; four 1px strips of mx_fill
    push ax
    push bx
    push cx
    push dx
    push dx
    mov dx, bx
    call mx_fill                    ; top
    pop dx
    push bx
    mov bx, dx
    call mx_fill                    ; bottom
    pop bx
    push cx
    mov cx, ax
    call mx_fill                    ; left
    pop cx
    push ax
    mov ax, cx
    call mx_fill                    ; right
    pop ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mx_text - SI = string, CX = x, DX = y. The kernel's glyphs, this game's
; renderer: each glyph is staged out of KERNEL_SEG into mc_gbuf (two
; segments are in play and DS holds the string), then plotted a pixel at a
; time - text on this surface is a score line, not a bandwidth problem.
mx_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
.ch:
    lodsb
    or al, al
    jz .done
    cmp al, 32
    jb .skip
    cmp al, 126
    ja .skip
    push si
    xor ah, ah
    sub al, 32
    mov si, ax
    push cx
    mov cl, 3
    shl si, cl                      ; (c-32)*8
    pop cx
    add si, [mc_gtab]
    mov es, [mc_gseg]
    mov di, 0
.stage:
    mov al, [es:si]
    mov [mc_gbuf+di], al
    inc si
    inc di
    cmp di, 8
    jb .stage
    mov es, [mc_fsi+FSI_SEG]
    xor bx, bx                      ; BX = glyph row
.grow:
    mov ah, [mc_gbuf+bx]            ; the row's bits, MSB leftmost
    xor di, di                      ; DI = bit
.gbit:
    shl ah, 1
    jnc .gnext
    push ax
    push cx
    push dx
    mov ax, cx
    add ax, di                      ; x + bit
    add dx, bx                      ; y + row
    call mx_px
    pop dx
    pop cx
    pop ax
.gnext:
    inc di
    cmp di, 8
    jb .gbit
    inc bx
    cmp bx, 8
    jb .grow
    pop si
.skip:
    add cx, 8
    jmp short .ch
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mx_px - one pixel: AX = x, DX = y, colour [mc_col]; ES = the framebuffer
mx_px:
    push ax
    push cx
    push dx
    push di
    mov di, dx
    mov cl, 4
    shl di, cl
    mov dx, di
    shl di, 1
    shl di, 1
    add di, dx                      ; y*80
    mov cx, ax
    shr ax, 1
    shr ax, 1
    add di, ax                      ; + x/4
    and cl, 3
    mov ah, 1
    shl ah, cl
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    mov al, [mc_col]
    mov [es:di], al
    pop di
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; x/y/w/h are computed by mc_entry from the live screen.
mc_tpl:
    dw 0, 0, 0, 0
    dw mc_ttl, mc_paint, mc_onkey, mc_onclick

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
    OS88_MENUSET mc_menus, mc_m_name, mc_oncmd
        OS88_MENU mc_m_game, mc_mi_game, 4
    OS88_MENUSET_END mc_menus

mc_m_name:   db 'Missile', 0
mc_m_game:   db 'Game', 0
mc_mi_game:  dw mc_s_new, mc_s_pcmd, mc_s_fcmd, mc_s_xcmd
mc_s_new:    db 'New Game', 0
mc_s_pcmd:   db 'Pause', 0
mc_s_fcmd:   db 'Full Screen', 0
mc_s_xcmd:   db 'Mode X', 0         ; the SPEC.md 53 exclusive surface -
mc_s_xcmdn:  db 'Mode X (Vga)', 0   ; mc_entry swaps the pointer to the
                                    ; second string when the adapter cannot
                                    ; (SPEC.md 47: say WHY not)

mc_ttl:      db 'Missile Command', 0
mc_s_ready:  db 'DEFEND YOUR CITIES', 0
mc_s_pause:  db 'PAUSED', 0
mc_s_over:   db 'THE END - N FOR A NEW GAME', 0
mc_s_bonus:  db 'BONUS POINTS', 0

; The credits. Kept short because this window is sized from the live screen and
; CGA's 200 rows leave a much smaller content than VGA's 480.
mc_ablines:
    dw mc_ab1, mc_ab2, mc_ab3, mc_ab4, mc_ab5, mc_ab6, mc_ab7, mc_ab8, 0
mc_ab1:      db 'Missile Command for os8088', 0
mc_ab2:      db 0
mc_ab3:      db 'After the 6502 arcade game', 0
mc_ab4:      db 'by Atari, 1980 - waves,', 0
mc_ab5:      db 'scoring and explosions from', 0
mc_ab6:      db 'the original W3MAIN source.', 0
mc_ab7:      db 0
mc_ab8:      db 'Click to fire. 1/2/3 pick a base.', 0

; --- the arcade's own tables ----------------------------------------------------
; ICBWAV: ICBMs per wave, clamped at the end of the table (W3MAIN).
mc_icbwav:   db 12, 15, 18, 12, 16, 14, 17, 10, 13, 16, 19, 12, 14, 16, 18
             db 14, 16, 18, 20
; CRMWAV: smart bombs per wave. Zero until wave 6, exactly as the arcade.
mc_crmwav:   db  0,  0,  0,  0,  0,  1,  1,  2,  3,  4,  4,  5,  5,  6,  6
             db  7,  7,  7,  7
; The speed ladder. WICSPL/WICSPH is a 6502 frame-update RATE (smaller is
; faster) on a 60Hz machine; this is the same shape expressed as sixteenths of
; a pixel a frame at 18fps, so wave 1 crosses the sky in about twelve seconds
; and wave 15 in four.
mc_spdwav:   db 28, 32, 36, 40, 45, 50, 55, 60, 66, 72, 78, 84, 90, 96,100
             db 104,108,112,116

; The city and base columns, on the arcade's own 0..255 field (W3COMN's
; CITY1H..CITY6H and MISB1H..MISB3H). Left to right that is base, three
; cities, base, three cities, base - the shape of the board.
mc_cityv:    db 0x5F, 0xB4, 0x94, 0x2C, 0x47, 0xD0
mc_basev:    db 0x14, 0x7B, 0xF0

; OLDRAD/NEWRAD, verbatim: the radius an explosion has on each of its 27
; frames. Padded to 32 so an index of MC_EXPFR-1 can never walk off it.
mc_rad:      db 0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
             db 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0

; The wave palettes: ground, ICBM trail, city, ABM trail - one per two waves,
; cycled, which is SETCOL's shape. Every entry is from SPEC.md 39.4's white
; class (12/14/15) or its dither class (7..11, 13) and never the black class
; (0..6), because everything here is drawn on a black field and a black-class
; colour would make that object invisible on Hercules and CGA. Within a
; palette the ground and the cities come from DIFFERENT classes, as do the two
; kinds of trail, so the four things a player must tell apart stay apart once
; colour has reduced to three inks.
mc_pal:
    db CYELLOW,   CLRED,     CLCYAN,    CLBLUE
    db CLGREEN,   CYELLOW,   CWHITE,    CLCYAN
    db CLCYAN,    CWHITE,    CYELLOW,   CLMAGENTA
    db CLRED,     CLGREEN,   CLBLUE,    CYELLOW
    db CLMAGENTA, CLRED,     CWHITE,    CLGREEN
    db CLGRAY,    CYELLOW,   CLRED,     CLCYAN
    db CWHITE,    CLBLUE,    CLGREEN,   CYELLOW
    db CLGREEN,   CLRED,     CYELLOW,   CLBLUE
    db CYELLOW,   CLMAGENTA, CLCYAN,    CWHITE
    db CLBLUE,    CWHITE,    CLRED,     CLGREEN

; An explosion flashes: four colours, one a frame. All four are in the white
; class or the dither class for the reason above.
mc_expcol:   db CWHITE, CYELLOW, CLRED, CLMAGENTA

; SPEC.md 48.8: the coarse ramp, for a 1bpp adapter or a tier-0 CPU. The same
; 27-frame life, drawn in THREE states - one byte a frame naming the state,
; with the radius and the colour both hanging off it, so the shape of the
; animation and the colour of it cannot disagree.
;
; 5, 13, 5 over 9, 7 and 9 frames is not a guess: it preserves BOTH of
; OLDRAD's integrals over its 27 frames, sum(r) exactly (181 = 181) and
; sum(r^2) to 0.2% (1633 against 1637). How much sky one ABM covers and for
; how long IS the game (SPEC.md 48.4), and this leaves it alone.
mc_step3:    db 0, 0                            ; 0..1:  not yet lit, as OLDRAD
             db 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1  ; 2..14: the peak, HELD
             db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; padding
mc_rad3v:    db 0, 13, 13
; ...and one colour per DRAWN state, both from SPEC.md 39.4's WHITE class.
; There is no dithered frame on this path: on the two adapters that reach it a
; dithered burst is just a grey one, and the flash it was buying is what made
; the old explosion redraw itself 27 times.
;
; There is no third colour any more either, and that is not an omission -
; SPEC.md 48.12 dropped the collapse, and a state that changes only the COLOUR
; would never be drawn: the coarse path's whole economy is that it compares
; RADIUS, so a same-radius state is a state nobody sees.
mc_col3:     db MC_BG, CYELLOW, CYELLOW

; mc_blob's band quantum, as a right shift of R (SPEC.md 48.25). It lives HERE
; and not in bss because bss arrives zeroed and zero means "one bounding rect",
; which is the erase's shape and must never be the drawn one.
mc_bqsh:     db 3

; The coastline: how many rows the ground rises above its base every 16px.
; Fixed rather than random, so a repaint puts back exactly what was there.
mc_coast:    db 0, 1, 2, 3, 2, 1, 0, 2, 4, 3, 1, 0, 1, 3, 2, 1

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes MC_BSS bytes after the image, and every
; name below is an offset from os88_image_end)
; =============================================================================

%assign MC_BSS 0
%macro MWORD 1
%1 equ os88_image_end + MC_BSS
%assign MC_BSS MC_BSS + 2
%endmacro
%macro MBYTE 1
%1 equ os88_image_end + MC_BSS
%assign MC_BSS MC_BSS + 1
%endmacro
%macro MBUF 2
%1 equ os88_image_end + MC_BSS
%assign MC_BSS MC_BSS + (%2)
%endmacro

; --- the screen and the window --------------------------------------------------
    MWORD mc_scrw
    MWORD mc_dock
    MBYTE mc_fs                     ; we hold the screen (SPEC.md 11.2)
    MWORD mc_win
    MWORD mc_ox
    MWORD mc_oy
    MWORD mc_cw
    MWORD mc_ch
    MBYTE mc_fsx                    ; inside an fsx bracket in a FOREIGN mode
                                    ; (Mode X). NOT "inside a bracket" - a
                                    ; same-mode bracket (SPEC.md 53.7) leaves
                                    ; this 0, because what it changes is who
                                    ; owns the machine and not how to draw
    MBYTE mc_fsxm                   ; ...and the FSXM_* to set, or 0FFh for
                                    ; the same-mode bracket
                                    ; (SPEC.md 53) - the four primitives,
                                    ; mc_track and the mouse read dispatch
                                    ; on it
    MBYTE mc_col                    ; the colour shadow mc_setcol keeps: the
                                    ; kernel's [gfx_color] cannot be read
                                    ; back, and the Mode X twins need it
    MBYTE mc_pbtn                   ; last-seen mouse buttons (edge detect -
                                    ; no W_ONCLICK arrives in a bracket)
    MWORD mc_caps                   ; the OSAPI_FSX_CAPS mask, from mc_entry
    MWORD mc_gtab                   ; the kernel glyph table's offset, for
                                    ; mx_text (OSAPI_FONT_GLYPHS, once)
    MWORD mc_gseg                   ; ...and its SEGMENT: the table lives in
                                    ; LOW_SEG, not KERNEL_SEG - the slot's DX
                                    ; answer, not an assumption
    MBUF  mc_gbuf, 8                ; one glyph staged out of KERNEL_SEG
    MBUF  mc_fsi, FSI_SIZE          ; the Mode X info block

; --- the derived layout ---------------------------------------------------------
    MWORD mc_statush
    MWORD mc_groundh
    MWORD mc_groundy
    MWORD mc_lowest
    MWORD mc_topy
    MWORD mc_citw
    MWORD mc_basw
    MWORD mc_objh
    MWORD mc_span
    MWORD mc_texty
    MBUF  mc_cityx, MC_NCITY * 2
    MBUF  mc_basex, MC_NBASE * 2

; --- the game -------------------------------------------------------------------
    MBYTE mc_mode
    MBYTE mc_wasmode                ; what Pause or About interrupted
    MBYTE mc_wave
    MBYTE mc_mult
    MWORD mc_icbpts                 ; 25 x multiplier, precomputed
    MWORD mc_icbspd
    MWORD mc_icbleft                ; ICBMs this wave still owes
    MWORD mc_smleft                 ; ...of which this many are smart bombs
    MBYTE mc_onscr
    MWORD mc_hold                   ; frames left in a pause or a tally step
    MWORD mc_lives                  ; PLIVES: cities the player is entitled to
    MWORD mc_scorelo
    MWORD mc_scorehi
    MWORD mc_hilo
    MWORD mc_hihi
    MWORD mc_bnlo                   ; the next bonus-city threshold
    MWORD mc_bnhi
    MWORD mc_ltick                  ; frames to the next salvo
    MBYTE mc_tbase                  ; the end-of-wave tally's cursors
    MBYTE mc_tcity
    MBUF  mc_calive, MC_NCITY
    MBUF  mc_balive, MC_NBASE
    MBUF  mc_bmis,   MC_NBASE

; --- input ----------------------------------------------------------------------
    MWORD mc_fire                   ; clicks the UI task has queued
    MWORD mc_firex
    MWORD mc_firey
    MBYTE mc_fireb                  ; which base, or 0FFh for the nearest
    MWORD mc_chx                    ; the crosshair, tracked from the mouse
    MWORD mc_chy
    MWORD mc_chpx                   ; ...and where it is DRAWN
    MWORD mc_chpy
    MBYTE mc_chshown

; --- the ICBMs ------------------------------------------------------------------
    MBUF  mc_ia,     MC_MAXICBM     ; 0 free / 1 ICBM / 2 smart bomb / FF dying
    MBUF  mc_ix16,   MC_MAXICBM * 2 ; position, pixels * 16
    MBUF  mc_iy16,   MC_MAXICBM * 2
    MBUF  mc_ivx,    MC_MAXICBM * 2
    MBUF  mc_ivy,    MC_MAXICBM * 2
    MBUF  mc_istep,  MC_MAXICBM * 2
    MBUF  mc_ipx,    MC_MAXICBM * 2 ; where the trail has been DRAWN to
    MBUF  mc_ipy,    MC_MAXICBM * 2
    MBUF  mc_isx,    MC_MAXICBM * 2 ; ...and where it started
    MBUF  mc_isy,    MC_MAXICBM * 2
    MBUF  mc_iex,    MC_MAXICBM * 2 ; ...and the point the trail is LAID at
    MBUF  mc_iey,    MC_MAXICBM * 2 ; (SPEC.md 48.14): fixed at launch, so a
                                    ; dodging bomb's trail stays a straight
                                    ; line - which is what the erase always
                                    ; assumed
    MBUF  mc_iwlk,   MC_MAXICBM * MC_WLK
    MBUF  mc_idrw,   MC_MAXICBM * 2 ; pixels of that walk on the screen
    MBUF  mc_ilen,   MC_MAXICBM * 2 ; ...and how many it has, signed: +n = x
                                    ; is the major axis, -n = y is
    MBUF  mc_iarm,   MC_MAXICBM     ; 0 not laid / 1 walking / 2 no walk
    MBUF  mc_itgt,   MC_MAXICBM
    MBUF  mc_imirv,  MC_MAXICBM
    MBUF  mc_imirv2, MC_MAXICBM * 2 ; frames until it splits
    MWORD mc_sbslot                 ; the smart bomb mc_sb_dodge is steering

; --- the ABMs -------------------------------------------------------------------
    MBUF  mc_aa,     MC_MAXABM
    MBUF  mc_ax16,   MC_MAXABM * 2
    MBUF  mc_ay16,   MC_MAXABM * 2
    MBUF  mc_avx,    MC_MAXABM * 2
    MBUF  mc_avy,    MC_MAXABM * 2
    MBUF  mc_astep,  MC_MAXABM * 2
    MBUF  mc_apx,    MC_MAXABM * 2
    MBUF  mc_apy,    MC_MAXABM * 2
    MBUF  mc_asx,    MC_MAXABM * 2
    MBUF  mc_asy,    MC_MAXABM * 2
    MBUF  mc_atx,    MC_MAXABM * 2  ; where it is to detonate - and, being
    MBUF  mc_aty,    MC_MAXABM * 2  ; fixed at launch, where the trail is laid
    MBUF  mc_awlk,   MC_MAXABM * MC_WLK
    MBUF  mc_adrw,   MC_MAXABM * 2
    MBUF  mc_alen,   MC_MAXABM * 2
    MBUF  mc_aarm,   MC_MAXABM
    MWORD mc_trlen                  ; mc_tr_lay's answer, out of the registers

; --- the drain (SPEC.md 48.15) --------------------------------------------------
    MBUF  mc_drn,    MC_MAXDRN * MC_DRN
    MWORD mc_drnq                   ; entries in use
    MWORD mc_drnrr                  ; which one this frame serves first
    MWORD mc_drnbud                 ; the frame's budget, being spent
    MWORD mc_drnn                   ; ...and the walk's counter, out of a reg
    MWORD mc_drncnt
    MWORD mc_drngx                  ; mc_drn_push's two damage arguments,
    MWORD mc_drnbx                  ; which no register was left for
    MBUF  mc_dsc,    MC_DSCMAX * 4  ; the batch: `dw block, pixels` pairs
    MWORD mc_dscn
    MWORD mc_dhent                  ; mc_drn_hold's workings (SPEC.md 48.19):
    MWORD mc_dhn                    ; the entry, the step, the line's two
    MWORD mc_dhdx                   ; deltas, and the span the step would
    MWORD mc_dhdy                   ; erase. All of it is in memory because
                                    ; the burst scan wants every register it
                                    ; can get, and BP addresses SS
    MWORD mc_dhnx
    MWORD mc_dhny
    MWORD mc_dhx0
    MWORD mc_dhy0
    MWORD mc_dhcnt                  ; the step asked for, and the step allowed
    MWORD mc_dhk
    MBYTE mc_dhmaj                  ; 0 x-major / 1 y-major / 2 a single point

; --- the explosions -------------------------------------------------------------
    MBUF  mc_ea,     MC_MAXEXP      ; 0 free / 1 burning / FF needs erasing
    MBUF  mc_ex,     MC_MAXEXP * 2
    MBUF  mc_ey,     MC_MAXEXP * 2
    MBUF  mc_et,     MC_MAXEXP      ; frame, 0..[mc_expfr]-1
    MBUF  mc_er,     MC_MAXEXP      ; the radius it is DRAWN at
    MBYTE mc_egone                  ; a burst has already been erased this
                                    ; frame; the rest wait (SPEC.md 48.24)
    MBUF  mc_edef,   MC_MAXEXP      ; ...and frames it has waited for a lit
                                    ; neighbour to go first (SPEC.md 48.25.1)
    MWORD mc_ehx                    ; ...and the disc of background it laid
    MWORD mc_ehy
    MWORD mc_ehr

; --- the satellite / bomber -----------------------------------------------------
    MBYTE mc_sata                   ; 0 none / 1 satellite / 2 bomber / FF gone
    MWORD mc_satx
    MWORD mc_saty
    MWORD mc_satvx
    MWORD mc_satpx                  ; where it was drawn
    MWORD mc_satfire
    MWORD mc_satcool

; --- the aiming block (mc_aim's inputs and outputs) -----------------------------
    MWORD mc_aimsx
    MWORD mc_aimsy
    MWORD mc_aimtx
    MWORD mc_aimty
    MWORD mc_aimspd
    MWORD mc_aimdx
    MWORD mc_aimdy
    MWORD mc_aimvx
    MWORD mc_aimvy
    MWORD mc_aimn

; --- damage scratch -------------------------------------------------------------
    MWORD mc_dmgx
    MWORD mc_dmgy
    MWORD mc_dmgr
    MWORD mc_expx
    MWORD mc_expy

; --- the wave's colours ---------------------------------------------------------
    MBYTE mc_cgnd
    MBYTE mc_cicbm
    MBYTE mc_ccity
    MBYTE mc_cabm

; --- drawing state --------------------------------------------------------------
    MBYTE mc_full                   ; the next frame must repaint everything
    MWORD mc_gdx1                   ; ...or at least the ground the frame BIT
    MWORD mc_gdx2                   ; INTO: an x span, MC_NODMG = nothing owed
    MBYTE mc_bdirty                 ; ...or at least the bases, one bit each
    MBUF  mc_sbuf, 16               ; mc_field's padded status field
    MWORD mc_spx1                   ; the span the three terrain painters are
    MWORD mc_spx2                   ; drawing this call (SPEC.md 48.9)
    MBYTE mc_bmask                  ; ...and which bases they may touch
    MBYTE mc_sdirty                 ; ...or at least the score strip
    MBUF  mc_shad0,  MC_SFW + 1     ; what each field was last DRAWN with
    MBUF  mc_shad1,  MC_SFW + 1
    MBUF  mc_shad2,  MC_WFW + 1
    MWORD mc_sax                    ; mc_srun's arguments, out of the
    MWORD mc_sdx                    ; registers its scan needs
    MWORD mc_scx
    MWORD mc_ssi
    MWORD mc_sfw
    MWORD mc_sf0
    MWORD mc_sf1
    MWORD mc_msgp                   ; the banner ACTUALLY on screen, 0 = none
    MBUF  mc_msgbuf, MC_MSGW + 2
    MWORD mc_runw                   ; mc_runc's field width, in pixels
    MBYTE mc_hired                  ; the worker exists
    MBYTE mc_abon                   ; the credits are up
    MWORD mc_abw
    MWORD mc_abh
    MWORD mc_abl
    MWORD mc_abt
    MWORD mc_due                    ; the tick the next frame is owed at

; --- drawing scratch ------------------------------------------------------------
    MWORD mc_dtmp
    MWORD mc_dtmp2
    MWORD mc_dtw
    MWORD mc_dmis
    MWORD mc_dmound
    MWORD mc_dph
    MWORD mc_dleft
    MWORD mc_drow
    MWORD mc_drowy
    MWORD mc_dn
    MWORD mc_dcol
    MWORD mc_dcx
    MWORD mc_dcy
    MWORD mc_dr
    MWORD mc_dr2
    MWORD mc_dhalf
    MWORD mc_dd2
    MWORD mc_rn2
    MWORD mc_rhalf
    MWORD mc_escl                   ; explosion scale, eighths
    MBUF  mc_erad, 32               ; the ramp scaled onto this window
    MBYTE mc_ecoarse                ; 1 = SPEC.md 48.8's three-state burst:
                                    ; 1bpp adapter or tier-0 CPU, decided
                                    ; once in mc_entry
    MBYTE mc_mono                   ; ...and the 1bpp half of that on its own,
                                    ; which is what SPEC.md 48.21's trail pens
                                    ; turn on - a tier-0 VGA still has colour
    MBYTE mc_expfr                  ; ...and how many frames a burst lasts,
                                    ; which the coarse ramp shortens (48.12)
    MWORD mc_bw                     ; mc_blob: the open rect's half-width
    MWORD mc_bprev                  ; mc_blob: the last band's half-height, -1
                                    ; before the centre band is drawn
    MWORD mc_bq                     ; ...and how far outside the true circle
                                    ; the drawn edge may run before the next
    MWORD mc_rnew
    MWORD mc_rold
    MWORD mc_rho
    MWORD mc_rhn
    MWORD mc_ry
    MWORD mc_rcx
    MWORD mc_rcy
    MWORD mc_sbx                    ; where mc_sb_dodge's bomb is standing
    MWORD mc_sby
    MWORD mc_sbd
    MWORD mc_lx
    MWORD mc_ly
    MWORD mc_lx1
    MWORD mc_ly1
    MWORD mc_ldx
    MWORD mc_ldy
    MWORD mc_lsx
    MWORD mc_lsy
    MWORD mc_lerr
    MWORD mc_lrun
    MBYTE mc_lfat                   ; the erase's one-pixel dilation
    MWORD mc_numptr
    MBUF  mc_numbuf, 12
    MBUF  mc_wbuf, 16

    OS88_BSS MC_BSS
    OS88_IMAGE_END
