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
MC_EXPFR    equ 27                  ; EXDONE: frames an explosion lasts
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
MC_LAGMAX   equ 4                   ; frames behind its deadline the worker
                                    ; will chase before re-anchoring (44.1)
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
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [mc_ox], ax
    mov [mc_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    mov [mc_cw], cx
    mov [mc_ch], dx
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
    mov al, [mc_rad + si]
    mov ah, 0
    mul word [mc_escl]
    mov cl, 3
    shr ax, cl
    cmp byte [mc_rad + si], 0       ; a step the arcade gives a radius keeps
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
    cmp byte [mc_fs], 0
    jne .out
    mov byte [mc_fs], 1
    mov al, 1
    mov bx, [mc_win]
    call OSAPI_FULLSCREEN           ; fronts + repaints under the held lock,
    jnc .out                        ; and that repaint is our own W_PAINT, so
    mov byte [mc_fs], 0             ; the layout re-derives itself
.out:
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
    call OSAPI_SET_COLOR
    call .rect
    call mc_fillc
    mov al, CBLACK                  ; black on white: the one pairing that
    call OSAPI_SET_COLOR            ; survives SPEC.md 39.4 on every adapter
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
    mov [mc_atx + di], cx
    mov [mc_aty + di], dx
    mov byte [mc_aa + si], 1

    dec byte [mc_bmis + bx]         ; the base is one missile lighter, and its
    mov byte [mc_bdirty], 1         ; pyramid has to lose a dot
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
    mov byte [mc_gdirty], 1
    jmp short .out
.base:
    sub bx, MC_NCITY
    cmp byte [mc_balive + bx], 0
    je .out
    mov byte [mc_balive + bx], 0
    mov byte [mc_bmis + bx], 0      ; and everything still in its magazine
    mov byte [mc_gdirty], 1
    mov byte [mc_bdirty], 1
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
    push si
    xor si, si
.each:
    cmp byte [mc_ea + si], 1
    jne .next
    inc byte [mc_et + si]
    cmp byte [mc_et + si], MC_EXPFR
    jb .next
    mov byte [mc_ea + si], 0FFh     ; done: mc_render owes it one erase
.next:
    inc si
    cmp si, MC_MAXEXP
    jb .each
    pop si
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
    mov byte [mc_mode], M_ENDA
    mov word [mc_hold], 6
    mov byte [mc_tbase], MC_NBASE - 1
    mov byte [mc_full], 1
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
    mov byte [mc_bdirty], 1
    mov ax, 1760
    call mc_beep
    jmp short .out
.done:
    mov byte [mc_mode], M_ENDC      ; on to the cities
    mov word [mc_hold], 8
    mov byte [mc_tcity], 0
    mov byte [mc_full], 1
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
    mov byte [mc_calive + si], 1
    inc cx
    jmp short .regen
.done:
    inc byte [mc_wave]
    mov byte [mc_mode], M_READY
    mov word [mc_hold], 22
    mov byte [mc_full], 1
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
    mov byte [mc_full], 1
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
    cmp byte [mc_abon], 0           ; the credits own the content until a click
    jne .skip                       ; or a key takes them down
    cmp byte [mc_full], 0
    je .parts
    call mc_draw_all
    jmp short .unlock
.parts:
    call mc_cross_off               ; the crosshair comes off FIRST: it is an
                                    ; XOR overlay, and everything below draws
                                    ; through where it was
    call mc_wipe_trails
    call mc_draw_exp
    call mc_move_trails
    call mc_draw_sat
    cmp byte [mc_gdirty], 0
    je .base
    mov byte [mc_gdirty], 0
    call mc_draw_ground
    call mc_draw_cities
    call mc_draw_bases
.base:
    cmp byte [mc_bdirty], 0
    je .stat
    mov byte [mc_bdirty], 0
    call mc_draw_bases
.stat:
    cmp byte [mc_sdirty], 0
    je .msg
    mov byte [mc_sdirty], 0
    call mc_draw_status
.msg:
    call mc_draw_msg
    call mc_cross_on
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
    mov byte [mc_gdirty], 0
    mov byte [mc_bdirty], 0
    mov byte [mc_sdirty], 0
    mov byte [mc_chshown], 0
    mov al, MC_BG
    call OSAPI_SET_COLOR
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
    call OSAPI_SET_COLOR
    xor ax, ax                      ; the band itself
    mov bx, [mc_groundy]
    mov cx, [mc_cw]
    dec cx
    mov dx, [mc_ch]
    dec dx
    call mc_fillc

    xor si, si                      ; the coastline: one step every 16 px, its
    xor di, di                      ; height off a fixed table. SI indexes the
.bump:                              ; table, DI is the column
    cmp di, [mc_cw]
    jae .out
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
    call OSAPI_SET_COLOR
    xor si, si
.each:
    cmp byte [mc_calive + si], 0
    je .next
    mov di, si
    add di, di
    mov ax, [mc_cityx + di]
    mov bx, [mc_citw]
    shr bx, 1
    sub ax, bx                      ; AX = left edge
    mov [mc_dtmp], ax
    call mc_city_shape
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
    mov ax, [mc_basex + di]
    mov bx, [mc_basw]
    shr bx, 1
    mov [mc_dtw], bx
    sub ax, bx
    mov [mc_dtmp], ax               ; the left edge

    mov al, MC_BG                   ; the lane this base owns, cleared
    call OSAPI_SET_COLOR
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
    call OSAPI_SET_COLOR
    call mc_base_shape
    jmp short .next
.dead:
    ; A CRATER, and it has to be a hole rather than a stump. The first version
    ; drew two low stubs in the GROUND colour on top of the ground - invisible
    ; on every adapter, so a destroyed base looked like bare terrain and the
    ; player could not tell a launcher that was gone from one that was merely
    ; out of missiles. A notch bitten out of the ground reads on all three.
    mov al, MC_BG
    call OSAPI_SET_COLOR
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
    call OSAPI_SET_COLOR
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
; mc_redraw_trails - every live trail, from its launch point to where it has
;                    been drawn to
; in:  gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
mc_redraw_trails:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mc_cicbm]
    call OSAPI_SET_COLOR
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
    mov cx, [mc_ipx + di]
    mov dx, [mc_ipy + di]
    call mc_line
.inext:
    inc si
    cmp si, MC_MAXICBM
    jb .icbm

    mov al, [mc_cabm]
    call OSAPI_SET_COLOR
    xor si, si
.abm:
    cmp byte [mc_aa + si], 1
    jne .anext
    mov di, si
    add di, di
    mov ax, [mc_asx + di]
    mov bx, [mc_asy + di]
    mov cx, [mc_apx + di]
    mov dx, [mc_apy + di]
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
    call OSAPI_SET_COLOR
    mov byte [mc_lfat], 1           ; see mc_lflush: an erase has to be one
                                    ; pixel wider than the draw, or half of it
                                    ; stays on the screen
    xor si, si
.icbm:
    cmp byte [mc_ia + si], 0FFh
    jne .inext
    mov byte [mc_ia + si], 0
    mov di, si
    add di, di
    mov ax, [mc_isx + di]
    mov bx, [mc_isy + di]
    mov cx, [mc_ipx + di]
    mov dx, [mc_ipy + di]
    call mc_line
    cmp dx, [mc_groundy]            ; a trail that reached the ground took a
    jl .inext                       ; bite out of it on the way out
    mov byte [mc_gdirty], 1
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
    mov ax, [mc_asx + di]
    mov bx, [mc_asy + di]
    mov cx, [mc_apx + di]
    mov dx, [mc_apy + di]
    call mc_line
    mov byte [mc_bdirty], 1         ; the trail starts ON the launcher
.anext:
    inc si
    cmp si, MC_MAXABM
    jb .abm
    mov byte [mc_lfat], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_move_trails - draw the segment every live missile added this frame
; in:  gfx lock held; preserves all registers
;
; Two or three pixels a missile a frame, which is what makes fifteen of them
; affordable. [mc_ipx]/[mc_ipy] mean "where the trail has been DRAWN to", and
; only this routine may write them - the same rule SPEC.md 44.4 gives for
; [ark_puold], and for the same reason: derive an erase from the update and it
; drifts the first time a frame is skipped.
; -----------------------------------------------------------------------------
mc_move_trails:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mc_cicbm]
    call OSAPI_SET_COLOR
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

    mov al, [mc_cabm]
    call OSAPI_SET_COLOR
    xor si, si
.abm:
    cmp byte [mc_aa + si], 1
    jne .anext
    mov di, si
    add di, di
    push si
    call mc_apix
    pop si
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
; costs nothing because the disc is redrawn regardless.
; -----------------------------------------------------------------------------
mc_draw_exp:
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
    jmp short .next
.gone:
    mov byte [mc_ea + si], 0
    mov al, MC_BG
    call OSAPI_SET_COLOR
    mov ax, [mc_ex + di]
    mov dx, [mc_ey + di]
    mov bl, [mc_er + si]
    mov bh, 0
    call mc_disc                    ; the last disc, back to sky
    mov byte [mc_er + si], 0
    mov ax, [mc_ey + di]
    mov bl, [mc_erad + 13]          ; the scaled peak radius
    mov bh, 0
    add ax, bx
    cmp ax, [mc_groundy]
    jl .next
    mov byte [mc_gdirty], 1         ; it bit into the ground on the way out
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
    and bl, 3
    mov bh, 0
    mov al, [mc_expcol + bx]
    call OSAPI_SET_COLOR
    ret

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
    call OSAPI_SET_COLOR
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
    call OSAPI_SET_COLOR
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
    call OSAPI_SET_COLOR
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
; whatever was there. It is the LAST thing drawn each frame and the FIRST thing
; undone the next, so nothing else ever draws while it is on the screen -
; which is the only condition an XOR overlay needs - the same argument SPEC.md
; 32 makes one level up for the window manager's own drag outline.
; -----------------------------------------------------------------------------
MC_CHARM  equ 8                     ; arm length. Big enough to read AROUND the
MC_CHGAP  equ 3                     ; system arrow, which the kernel keeps
                                    ; drawing at the same point (SPEC.md 11.2:
                                    ; even fullscreen, the cursor stays live)
                                    ; - a short crosshair simply hides under it

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

mc_cross_xor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mc_chpx]               ; the left arm
    sub ax, MC_CHARM
    mov bx, [mc_chpy]
    mov cx, [mc_chpx]
    sub cx, MC_CHGAP
    mov dx, bx
    call mc_xorc
    mov ax, [mc_chpx]               ; the right arm
    add ax, MC_CHGAP
    mov bx, [mc_chpy]
    mov cx, [mc_chpx]
    add cx, MC_CHARM
    mov dx, bx
    call mc_xorc
    mov ax, [mc_chpx]               ; up
    mov bx, [mc_chpy]
    sub bx, MC_CHARM
    mov cx, ax
    mov dx, [mc_chpy]
    sub dx, MC_CHGAP
    call mc_xorc
    mov ax, [mc_chpx]               ; ...and down
    mov bx, [mc_chpy]
    add bx, MC_CHGAP
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
    mov al, MC_BG
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [mc_cw]
    dec cx
    mov dx, [mc_statush]
    dec dx
    call mc_fillc

    mov ax, [mc_statush]            ; the baseline, centred in the strip
    sub ax, 8
    shr ax, 1
    mov [mc_texty], ax

    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [mc_scorelo]
    mov dx, [mc_scorehi]
    call mc_num32
    mov si, [mc_numptr]
    mov cx, 2
    mov dx, [mc_texty]
    call mc_textc

    mov al, CYELLOW                 ; the high score, centred
    call OSAPI_SET_COLOR
    mov ax, [mc_hilo]
    mov dx, [mc_hihi]
    call mc_num32
    mov si, [mc_numptr]
    call OSAPI_FONT_WIDTH
    mov cx, [mc_cw]
    sub cx, ax
    shr cx, 1
    mov si, [mc_numptr]
    mov dx, [mc_texty]
    call mc_textc

    ; Wave and multiplier, right-aligned. All THREE figures in this strip are
    ; drawn from SPEC.md 39.4's white class (15, 14, 12) and none from its
    ; dither class, because a dithered 8x8 glyph on a black field loses the
    ; half of each stroke the pattern masks out - and a 1px stroke has nothing
    ; left. This was CLGREEN, and on CGA the wave counter simply was not there:
    ; not faint, ABSENT, with zero lit pixels across the hundred columns it
    ; occupied. Colour is decoration here; the strip has to be legible on all
    ; three adapters.
    mov al, CLRED
    call OSAPI_SET_COLOR
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
    call OSAPI_FONT_WIDTH
    mov cx, [mc_cw]
    sub cx, ax
    sub cx, 3
    mov si, mc_wbuf
    mov dx, [mc_texty]
    call mc_textc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_wcat - append the NUL string at SI to DI; DI ends past the last character
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

; -----------------------------------------------------------------------------
; mc_draw_msg - the banner in the middle of the sky
; in:  gfx lock held; preserves all registers
;
; It is erased and redrawn every frame it is showing, which is affordable
; because it only shows between waves and is one strip. [mc_msgon] is what
; makes the erase happen on the frame it goes away.
; -----------------------------------------------------------------------------
mc_draw_msg:
    push ax
    push bx
    push cx
    push dx
    push si
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
    or si, si
    jnz .draw
    cmp byte [mc_msgon], 0          ; nothing to say: erase the last thing said
    je .out
    mov byte [mc_msgon], 0
    call mc_msg_erase
    jmp short .out
.draw:
    call mc_msg_erase
    mov byte [mc_msgon], 1
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_FONT_WIDTH
    mov cx, [mc_cw]
    sub cx, ax
    shr cx, 1
    call mc_msgy
    mov dx, ax
    call mc_textc
.out:
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

mc_msg_erase:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, MC_BG
    call OSAPI_SET_COLOR
    call mc_msgy
    mov bx, ax
    xor ax, ax
    mov cx, [mc_cw]
    dec cx
    mov dx, bx
    add dx, 8
    call mc_fillc
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    call mc_clamp
    jc .out
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    call OSAPI_GFX_FILL
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
    call OSAPI_GFX_XOR_FILL
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
    call mc_clamp
    jc .out
    add ax, [mc_ox]
    add cx, [mc_ox]
    add bx, [mc_oy]
    add dx, [mc_oy]
    call OSAPI_GFX_FRAME
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
    call OSAPI_FONT_STR
    pop dx
    pop cx
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
        OS88_MENU mc_m_game, mc_mi_game, 3
    OS88_MENUSET_END mc_menus

mc_m_name:   db 'Missile', 0
mc_m_game:   db 'Game', 0
mc_mi_game:  dw mc_s_new, mc_s_pcmd, mc_s_fcmd
mc_s_new:    db 'New Game', 0
mc_s_pcmd:   db 'Pause', 0
mc_s_fcmd:   db 'Full Screen', 0

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
    MBUF  mc_atx,    MC_MAXABM * 2  ; where it is to detonate
    MBUF  mc_aty,    MC_MAXABM * 2

; --- the explosions -------------------------------------------------------------
    MBUF  mc_ea,     MC_MAXEXP      ; 0 free / 1 burning / FF needs erasing
    MBUF  mc_ex,     MC_MAXEXP * 2
    MBUF  mc_ey,     MC_MAXEXP * 2
    MBUF  mc_et,     MC_MAXEXP      ; frame, 0..26
    MBUF  mc_er,     MC_MAXEXP      ; the radius it is DRAWN at

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
    MBYTE mc_gdirty                 ; ...or at least the ground
    MBYTE mc_bdirty                 ; ...or at least the bases
    MBYTE mc_sdirty                 ; ...or at least the score strip
    MBYTE mc_msgon
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
    MBUF  mc_erad, 32               ; mc_rad scaled onto this window
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
