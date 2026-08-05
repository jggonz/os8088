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
;    the same zone, so a rally had no continuity at all. A serve is thrown the
;    way the paddle is moving, at twice english's weight, because it has no
;    incoming direction to build on - the flick IS the aim.
;
;  - **The game IS the worker task.** A ball has to keep moving between
;    keystrokes, and a window callback only runs when something happens to the
;    window, so the loop lives in ark_worker (SPEC.md 20.6) - the same shape as
;    apps/fractal, sleeping one tick a frame for ~18 fps. Everything the UI
;    task does is set a word the worker reads.
;
;  - **The keyboard is edge-triggered, so the paddle glides on a deadline.**
;    int 16h gives keypresses and no key-up, so "is left held" cannot be asked.
;    Instead each arrow key sets a direction and refills [ark_pkeep] with a
;    deadline in ticks, and the worker moves the paddle while the deadline
;    lasts. Typematic repeat keeps refilling it, so a held key glides; the
;    deadline outlives the ~9-tick typematic DELAY on purpose, or a hold would
;    stall for half a second before it repeated. A tap is one short nudge, and
;    a repeat arriving while the deadline still stands means the key is HELD,
;    which is what winds the paddle up to its faster speed.
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
;    merely turning red, and a capsule is identified by the LETTER on it since
;    five colours cannot survive a reduction to three inks. The brick rows go
;    further - no row colour may reduce to BLACK, or that row disappears into
;    the background entirely, so the palette is drawn from the white and
;    dither classes only and alternates between them.
;
; Keys: left/right move, Space launches the ball (and fires the laser when one
; is armed), P pauses, N starts a new game.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'ARKANOID', ark_entry, 1

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
ARK_MAXROW  equ 6                   ; ...and the most rows either set uses
ARK_CELLS   equ ARK_COLS * ARK_MAXROW
ARK_NMET    equ 11                  ; words in a metrics record (ark_met_*)

ARK_LIVES   equ 3
ARK_PKEEP   equ 11                  ; ticks a keypress keeps the paddle moving.
                                    ; The BIOS typematic DELAY is ~9 ticks, so
                                    ; anything shorter stalls a held key
ARK_PSTEP   equ 4                   ; paddle pixels a tick, tapped...
ARK_PFAST   equ 8                   ; ...and held
ARK_VXMAX   equ 4                   ; the flattest angle the ball may reach.
                                    ; vx accumulates across bounces, so it
                                    ; needs a ceiling or a rally converges on
                                    ; horizontal and stops coming down

; powerup kinds, and the letter each capsule carries
PU_NONE     equ 0
PU_EXPAND   equ 1                   ; 'E' a wider paddle
PU_CATCH    equ 2                   ; 'C' the ball sticks until Space
PU_LASER    equ 3                   ; 'L' Space fires
PU_SLOW     equ 4                   ; 'S' the ball slows down
PU_LIFE     equ 5                   ; 'X' one more life
PU_KINDS    equ 5
ARK_MAXPU   equ 3                   ; capsules falling at once
ARK_MAXSHOT equ 2                   ; bullets in the air at once
ARK_PUCHANCE equ 4                  ; 1 in this many broken bricks drops one
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
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock top row, DH=bpp
    mov [ark_scrw], ax
    mov [ark_dock], cx
    mov [ark_bpp], dh

    mov si, ark_met_big
    cmp bx, 300
    jae .metric
    mov si, ark_met_sml
.metric:
    mov di, ark_bw                  ; copied wholesale: the bss words below
    mov cx, ARK_NMET                ; must stay in the record's order
.mcopy:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .mcopy

    mov ax, [ark_pw0]               ; the widest an Expand can make it
    add ax, 24
    mov [ark_pwmax], ax

    mov ax, [ark_bw]                ; content width = the wall plus both rails
    mov bx, ARK_COLS
    mul bx
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
    xor dx, dx
    mov bx, 3
    div bx
    add ax, MBAR_H
    mov [ark_tpl + WT_Y], ax

    call OSAPI_GET_TICKS            ; seeded once; every later wall and every
    call OSAPI_SRAND                ; capsule walks on down the same stream

    call ark_newgame

    mov si, ark_tpl
    call OSAPI_WM_CREATE
    jc .full
    mov [ark_win], bx
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
; ark_layout - the rows every other routine measures from
; in:  [ark_chgt]; out: [ark_bricky], [ark_pady], [ark_floor]
; preserves all registers
; -----------------------------------------------------------------------------
ark_layout:
    push ax
    mov ax, [ark_status]            ; the wall starts under the status strip
    add ax, [ark_gap]
    mov [ark_bricky], ax
    mov ax, [ark_chgt]              ; the paddle rides just off the bottom
    sub ax, [ark_pado]
    mov [ark_pady], ax
    mov ax, [ark_chgt]              ; ...and the ball dies below it
    dec ax
    mov [ark_floor], ax
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
ark_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ark_track
    call ark_draw_all
    mov byte [ark_full], 0
    mov byte [ark_msg], 0
    mov byte [ark_stat], 0
    call ark_hire                   ; idempotent: only the first paint spawns
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
; anything but set a word the worker reads - except N and P, which change what
; is on the screen and therefore repaint it.
;
; The `or bl, bl` gate is not optional: the numeric keypad sends '4' and '6'
; with the arrow scan codes, so without it typing a digit would steer the
; paddle (the same trap apps/notepad documents).
; -----------------------------------------------------------------------------
ark_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                      ; keep the key; ark_track needs AX
    call ark_track
    call ark_abdismiss              ; any key takes the credits down, and is
    jc .out                         ; spent doing it
    or bl, bl
    jnz .ascii

    cmp bh, 0x4B                    ; left arrow
    je .left
    cmp bh, 0x4D                    ; right arrow
    je .right
    jmp .out
.left:
    mov al, -1
    jmp .steer
.right:
    mov al, 1
.steer:
    cmp al, [ark_pdir]              ; a repeat in the same direction while the
    jne .fresh                      ; deadline still stands means the key is
    cmp word [ark_pkeep], 0         ; HELD: wind the paddle up
    je .fresh
    mov word [ark_pspd], ARK_PFAST
    jmp .keep
.fresh:
    mov [ark_pdir], al
    mov word [ark_pspd], ARK_PSTEP
.keep:
    mov word [ark_pkeep], ARK_PKEEP
    jmp .out

.ascii:
    cmp bl, ' '
    je .space
    cmp bl, 'p'
    je .pause
    cmp bl, 'P'
    je .pause
    cmp bl, 'n'
    je .new
    cmp bl, 'N'
    je .new
    jmp .out
.space:
    mov byte [ark_launch], 1        ; the worker decides what it means
    jmp .out
.pause:
    call ark_cmd_pause
    jmp .out
.new:
    call ark_cmd_new
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_oncmd - AM_ONCMD: a Game menu item (SPEC.md 12.2)
; in:  AL = item, AH = menu, SI = our window, BX = the set; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX like any window callback
; -----------------------------------------------------------------------------
ark_oncmd:
    push si
    push di
    mov bl, al
    call ark_track
    call ark_abdismiss              ; a menu pick takes the credits down first,
    jc .out                         ; and is spent doing it
    or bl, bl
    jz .new
    cmp bl, 1
    je .pause
    jmp .out
.new:
    call ark_cmd_new
    jmp .out
.pause:
    call ark_cmd_pause
.out:
    pop di
    pop si
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
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ark_track
    call ark_abdismiss
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_cmd_new / ark_cmd_pause - the two commands, shared by keys and menu
; in:  the origin tracked; gfx lock held; preserve all registers
; -----------------------------------------------------------------------------
ark_cmd_new:
    call ark_newgame
    call ark_draw_all
    ret

ark_cmd_pause:
    push ax
    mov al, [ark_mode]
    cmp al, M_PAUSE
    je .resume
    cmp al, M_PLAY
    je .halt
    cmp al, M_READY
    je .halt
    jmp .out
.halt:
    mov [ark_wasmode], al
    mov byte [ark_mode], M_PAUSE
    call ark_draw_all
    jmp .out
.resume:
    mov al, [ark_wasmode]
    mov [ark_mode], al
    call ark_draw_all
.out:
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
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [ark_abon], 1
    mov al, [ark_mode]              ; a LIVE ball is frozen underneath, because
    cmp al, M_PLAY                  ; a dropped frame does not stop it and a
    jne .draw                       ; player would lose a life reading this.
    mov [ark_wasmode], al           ; Every other mode is already still, so it
    mov byte [ark_mode], M_PAUSE    ; is left alone and the banner it was
.draw:                              ; showing comes back untouched
    call ark_track
    call ark_draw_all               ; ...which draws the panel last, because
    pop di                          ; [ark_abon] is set
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    call ark_draw_all               ; the game stays paused: the key that took
    mov byte [ark_full], 0          ; the credits down is not also a resume.
    mov byte [ark_msg], 0           ; This IS the whole repaint every skipped
    mov byte [ark_stat], 0          ; frame under the panel asked for, so it
    stc                             ; settles the debt rather than leaving the
    ret                             ; worker to draw the board a second time
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; ark_abmeas - size and centre the panel from the strings themselves
; out: [ark_abw]/[ark_abh]/[ark_abl]/[ark_abt], content coords
; preserves every register
;
; Measured rather than pinned because the two metric sets give two different
; content widths (SPEC.md 44.6) and CGA's is the narrower - a hard-coded box
; sized for VGA would hang out of it.
; -----------------------------------------------------------------------------
ark_abmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, ark_ablines
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
    add cx, 16                      ; 8px of margin either side
    cmp cx, [ark_cwid]
    jbe .wok
    mov cx, [ark_cwid]
.wok:
    mov [ark_abw], cx
    mov ax, di                      ; height = lines * ARK_ABLH + margins
    mov bx, ARK_ABLH
    mul bx
    add ax, 14
    cmp ax, [ark_chgt]
    jbe .hok
    mov ax, [ark_chgt]
.hok:
    mov [ark_abh], ax
    mov ax, [ark_cwid]
    sub ax, [ark_abw]
    shr ax, 1
    mov [ark_abl], ax
    mov ax, [ark_chgt]
    sub ax, [ark_abh]
    shr ax, 1
    mov [ark_abt], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_abdraw - the panel itself, last of everything
; in:  gfx lock held, origin tracked; preserves every register
; -----------------------------------------------------------------------------
ark_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ark_abmeas
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call .rect
    call ark_fillc
    mov al, CBLACK                  ; a black frame and black text on the white
    call OSAPI_SET_COLOR            ; panel: the one pairing that survives
    call .rect                      ; SPEC.md 39.4 on every adapter
    call ark_framec

    mov si, ark_ablines
    mov di, [ark_abt]
    add di, 7                       ; the first baseline, inside the frame
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = this line's width
    mov cx, [ark_abw]
    sub cx, ax
    shr cx, 1
    add cx, [ark_abl]
    mov dx, di
    call ark_textc                  ; content coords; ark_textc adds the origin
    pop si
    add di, ARK_ABLH
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
    mov ax, [ark_abl]
    mov bx, [ark_abt]
    mov cx, ax
    add cx, [ark_abw]
    dec cx
    mov dx, bx
    add dx, [ark_abh]
    dec dx
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
; preserves all registers
; -----------------------------------------------------------------------------
ark_update:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ark_do_paddle              ; the paddle moves even while parked
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
    cmp word [ark_hold], 0
    jg .out
    cmp byte [ark_mode], M_CLEAR
    je .next
    call ark_respawn
    jmp .out
.next:
    call ark_nextwall
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_do_paddle - move the paddle while its deadline lasts
; in:  [ark_pdir]/[ark_pkeep]/[ark_pspd]; out: [ark_px] moved, clamped
; preserves all registers
;
; [ark_pkeep] is written by the UI task and decremented here. Both are plain
; word accesses, and the 8086 recognises interrupts only at instruction
; boundaries, so neither can be torn by the other - the same no-protocol
; sharing apps/fractal uses for its restart flag.
; -----------------------------------------------------------------------------
ark_do_paddle:
    push ax
    push bx
    push dx
    mov word [ark_pvel], 0          ; how far the paddle moves THIS frame,
    cmp word [ark_pkeep], 0         ; signed - the ball reads it on contact
    jle .out
    dec word [ark_pkeep]
    mov al, [ark_pdir]
    cbw
    mov bx, [ark_pspd]
    imul bx                         ; DX:AX = dir * speed
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
; ark_english - the sideways kick the paddle's own motion imparts
; out: AX = -2..+2; preserves all other registers
;
; The paddle moves 4 pixels a tick tapped and 8 held (ARK_PSTEP/ARK_PFAST), so
; a quarter of that is a clean -2..+2 without a table. idiv truncates toward
; zero, which is what makes a rail-clamped part-move round down to nothing.
; -----------------------------------------------------------------------------
ark_english:
    push bx
    push dx
    mov ax, [ark_pvel]
    cwd
    mov bx, 4
    idiv bx
    cmp ax, 2
    jle .lo
    mov ax, 2
.lo:
    cmp ax, -2
    jge .out
    mov ax, -2
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ark_throw - the sideways speed a SERVE gets from the paddle's motion
; out: AX = -3..+3; preserves all other registers
;
; Twice ark_english's weight, because a serve has no incoming direction to
; build on: the flick IS the aim. A paddle standing still serves straight up,
; which is honest rather than a hidden default - the player who wants an angle
; flicks, and the one who does not gets to choose after the first bounce.
; -----------------------------------------------------------------------------
ark_throw:
    push bx
    push dx
    mov ax, [ark_pvel]
    cwd
    mov bx, 2
    idiv bx
    cmp ax, 3
    jle .lo
    mov ax, 3
.lo:
    cmp ax, -3
    jge .out
    mov ax, -3
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ark_setspeed - the rally's vertical speed for this level
; out: [ark_vymag] = 3..5; preserves all registers
;
; |vy| is the authority, not [ark_bvy]: a paddle bounce restores it rather
; than inventing one, so the vertical rhythm of a rally stays constant while
; the ANGLE is free to change. It is also the one number Slow reduces.
; -----------------------------------------------------------------------------
ark_setspeed:
    push ax
    mov al, [ark_level]
    mov ah, 0
    add ax, 2
    cmp ax, 5
    jbe .set
    mov ax, 5
.set:
    mov [ark_vymag], ax
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
    mov ax, [ark_px]                ; parked: ride the paddle
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
    jmp .out
.free:
    mov ax, [ark_bvx]               ; |vx|, sign in [ark_sx]
    mov word [ark_sx], 1
    or ax, ax
    jns .dxok
    neg ax
    mov word [ark_sx], -1
.dxok:
    mov [ark_adx], ax
    mov ax, [ark_bvy]
    mov word [ark_sy], 1
    or ax, ax
    jns .dyok
    neg ax
    mov word [ark_sy], -1
.dyok:
    mov [ark_ady], ax
    mov ax, [ark_adx]               ; steps along the major axis
    cmp ax, [ark_ady]
    jge .n1
    mov ax, [ark_ady]
.n1:
    mov [ark_nstep], ax
    mov ax, [ark_adx]
    sub ax, [ark_ady]
    mov [ark_err], ax
.walk:
    cmp word [ark_nstep], 0
    jle .out
    dec word [ark_nstep]
    mov ax, [ark_err]
    add ax, ax                      ; e2 = 2*err
    mov si, ax
    mov ax, [ark_ady]
    neg ax
    cmp si, ax
    jle .nox
    mov ax, [ark_err]
    sub ax, [ark_ady]
    mov [ark_err], ax
    mov ax, [ark_sx]
    xor bx, bx
    call ark_move1                  ; one pixel in x
    jc .out                         ; something reflected: the rest is stale
.nox:
    mov ax, [ark_err]
    add ax, ax
    cmp ax, [ark_adx]
    jge .walk
    mov ax, [ark_err]
    add ax, [ark_adx]
    mov [ark_err], ax
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
    add dx, bx
    mov [ark_nx], cx
    mov [ark_ny], dx

    mov ax, [ark_rail]              ; --- the rails and the ceiling ---
    cmp cx, ax
    jl .bouncex
    mov ax, [ark_cwid]
    sub ax, [ark_rail]
    sub ax, [ark_bsz]
    cmp cx, ax
    jg .bouncex
    mov ax, [ark_status]
    cmp dx, ax
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
    mov ax, 1200
    call ark_beep
    jmp .hit
.bouncey:
    neg word [ark_bvy]
    mov ax, 1200
    call ark_beep
    jmp .hit
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
; in:  [ark_nx] = the blocked position; preserves all registers
;
; The step that was blocked names the axis: this is only ever reached from a
; single-pixel move, so exactly one coordinate changed and that is the one to
; reverse. No geometry, no corner cases.
; -----------------------------------------------------------------------------
ark_reflect:
    push ax
    mov ax, [ark_nx]
    cmp ax, [ark_bx]
    je .yaxis
    neg word [ark_bvx]
    jmp .out
.yaxis:
    neg word [ark_bvy]
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_hit_paddle - is the candidate rect over the paddle?
; in:  [ark_nx]/[ark_ny]; out: CF = 1 hit; preserves all registers
; -----------------------------------------------------------------------------
ark_hit_paddle:
    push ax
    push bx
    cmp word [ark_bvy], 0           ; only ever on the way DOWN, or a ball
    jle .no                         ; clipping the side would stick to it
    mov ax, [ark_ny]
    add ax, [ark_bsz]
    dec ax
    cmp ax, [ark_pady]
    jl .no
    mov ax, [ark_ny]
    mov bx, [ark_pady]
    add bx, [ark_ph]
    cmp ax, bx
    jge .no
    mov ax, [ark_nx]
    add ax, [ark_bsz]
    dec ax
    cmp ax, [ark_px]
    jl .no
    mov ax, [ark_nx]
    mov bx, [ark_px]
    add bx, [ark_pw]
    cmp ax, bx
    jge .no
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
    mov word [ark_bvy], 0
    mov word [ark_bvx], 0
    mov byte [ark_msg], 1
    mov ax, 520
    call ark_beep
    jmp .out
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
    mov bx, ax
    add bx, bx
    mov dx, [ark_zbias+bx]          ; where along the paddle it landed
    call ark_english                ; ...and which way the paddle was going
    add ax, dx
    add ax, [ark_bvx]               ; both ADDED to the incoming vx
    cmp ax, ARK_VXMAX
    jle .hi
    mov ax, ARK_VXMAX
.hi:
    cmp ax, -ARK_VXMAX
    jge .setvx
    mov ax, -ARK_VXMAX
.setvx:
    mov [ark_bvx], ax
    mov ax, 880
    call ark_beep
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_hit_brick - is the candidate rect over a live brick? If so, break it
; in:  [ark_nx]/[ark_ny]; out: CF = 1 hit; preserves all registers
;
; Four corners rather than a cell-range walk: the ball is smaller than a brick
; in both axes, so the corners are the only cells it can be in.
; -----------------------------------------------------------------------------
ark_hit_brick:
    push ax
    push bx
    push cx
    push dx
    mov cx, [ark_nx]
    mov dx, [ark_ny]
    call ark_cell
    jnc .got
    mov cx, [ark_nx]
    add cx, [ark_bsz]
    dec cx
    mov dx, [ark_ny]
    call ark_cell
    jnc .got
    mov cx, [ark_nx]
    mov dx, [ark_ny]
    add dx, [ark_bsz]
    dec dx
    call ark_cell
    jnc .got
    mov cx, [ark_nx]
    add cx, [ark_bsz]
    dec cx
    mov dx, [ark_ny]
    add dx, [ark_bsz]
    dec dx
    call ark_cell
    jc .none
.got:
    call ark_break                  ; AX = the cell
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.none:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
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
    mov bx, ARK_COLS
    mul bx                          ; AX = row * COLS
    add ax, cx
    mov bx, ax
    cmp byte [ark_grid+bx], 0
    je .none
    pop dx
    pop cx
    pop bx
    clc
    ret
.none:
    pop dx
    pop cx
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
    mov byte [ark_dirty+bx], 1
    mov byte [ark_stat], 1
    mov ax, 1500                    ; a chipped brick sounds different from a
    cmp byte [ark_grid+bx], 0       ; broken one
    jne .chip
    mov ax, 1900
    call ark_beep
    dec word [ark_left]
    add word [ark_score], 10
    call ark_maybe_pu
    cmp word [ark_left], 0
    jne .out
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
    xor dx, dx
    mov cx, ARK_PUCHANCE
    div cx
    or dx, dx
    jnz .out
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
    mov ax, di                      ; the cell's own position
    xor dx, dx
    mov cx, ARK_COLS
    div cx                          ; AX = row, DX = column
    mov bx, si
    add bx, bx
    push ax
    mov ax, dx
    mul word [ark_bw]
    add ax, [ark_rail]
    push bx
    mov bx, [ark_bw]
    shr bx, 1
    add ax, bx
    pop bx
    sub ax, ARK_PUW / 2
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
    add ax, ARK_PUFALL              ; [ark_puold] is NOT touched here: it means
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
    mov byte [ark_pukind+si], PU_NONE
    mov byte [ark_puwipe+si], 1
    jmp .next
.floor:
    cmp ax, [ark_floor]
    jl .next
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
    je .life
    jmp .done
.expand:
    mov ax, [ark_pw]
    add ax, 12
    cmp ax, [ark_pwmax]
    jle .setw
    mov ax, [ark_pwmax]
.setw:
    mov [ark_pw], ax
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
    jmp .done
.life:
    inc byte [ark_lives]
.done:
    mov ax, 1046                    ; the catch chime
    call ark_beep
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_slower - knock the rally's vertical speed down one notch, never below 2
;              (a ball with no vy would never come back down)
; preserves all registers
;
; It moves [ark_vymag] rather than [ark_bvy], because vymag is what every
; paddle bounce restores - changing only the live velocity would last exactly
; until the next one.
; -----------------------------------------------------------------------------
ark_slower:
    push ax
    cmp word [ark_vymag], 2
    jle .out
    dec word [ark_vymag]
    mov ax, [ark_vymag]             ; ...and take the live ball with it, in
    cmp word [ark_bvy], 0           ; whichever direction it is already going
    jge .set
    neg ax
.set:
    mov [ark_bvy], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_fire / ark_do_shots - the laser
; ark_fire:     put a bullet in the air from the paddle's centre
; ark_do_shots: fly them, and let each break one brick
; both preserve every register
; -----------------------------------------------------------------------------
ark_fire:
    push ax
    push bx
    push si
    xor si, si
.slot:
    cmp byte [ark_shot+si], 0
    je .free
    inc si
    cmp si, ARK_MAXSHOT
    jb .slot
    jmp .out
.free:
    mov byte [ark_shot+si], 1
    mov bx, si
    add bx, bx
    mov ax, [ark_pw]
    shr ax, 1
    add ax, [ark_px]
    mov [ark_shx+bx], ax
    mov ax, [ark_pady]              ; CLEAR of the paddle and its muzzles: a
    sub ax, 8                       ; bolt spawned on the paddle erases its
    mov [ark_shy+bx], ax            ; own first position out of it and leaves
    mov [ark_shold+bx], ax          ; a hole where it was fired from
    mov ax, 2200
    call ark_beep
.out:
    pop si
    pop bx
    pop ax
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
    dec byte [ark_lives]
    mov byte [ark_stat], 1
    mov word [ark_hold], 12
    cmp byte [ark_lives], 0
    jg .again
    mov byte [ark_mode], M_OVER
    mov byte [ark_full], 1
    jmp .out
.again:
    mov byte [ark_mode], M_DEAD
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_respawn - park a fresh ball on the paddle after a death
; preserves all registers
; -----------------------------------------------------------------------------
ark_respawn:
    push ax
    call ark_setspeed
    mov byte [ark_stuck], 1
    mov byte [ark_catch], 0
    mov byte [ark_laser], 0
    mov ax, [ark_pw0]
    mov [ark_pw], ax
    mov byte [ark_mode], M_READY
    mov byte [ark_full], 1
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
    push ax
    inc byte [ark_level]
    call ark_setspeed
    call ark_build
    mov byte [ark_stuck], 1
    mov byte [ark_catch], 0
    mov byte [ark_laser], 0
    mov ax, [ark_pw0]
    mov [ark_pw], ax
    mov byte [ark_mode], M_READY
    mov byte [ark_full], 1
    pop ax
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
    push ax
    mov word [ark_score], 0
    mov byte [ark_lives], ARK_LIVES
    mov byte [ark_level], 1
    call ark_setspeed               ; AFTER the level is 1: it reads it
    mov ax, [ark_pw0]
    mov [ark_pw], ax
    mov ax, [ark_cwid]              ; the paddle starts centred
    sub ax, [ark_pw]
    shr ax, 1
    mov [ark_px], ax
    mov byte [ark_catch], 0
    mov byte [ark_laser], 0
    mov byte [ark_stuck], 1
    mov byte [ark_mode], M_READY
    mov word [ark_pkeep], 0
    call ark_build
    pop ax
    ret

ark_build:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx
    xor cx, cx                      ; CX = live bricks
.cell:
    mov ax, bx
    xor dx, dx
    push cx
    mov cx, ARK_COLS
    div cx                          ; AX = row
    pop cx
    cmp ax, [ark_rows]
    jae .empty
    mov dl, 1
    cmp ax, 1                       ; the top two rows take two hits
    jg .soft
    mov dl, 2
.soft:
    mov [ark_grid+bx], dl
    inc cx
    jmp .step
.empty:
    mov byte [ark_grid+bx], 0
.step:
    mov byte [ark_dirty+bx], 0
    inc bx
    cmp bx, ARK_CELLS
    jb .cell
    mov [ark_left], cx
    xor bx, bx                      ; nothing in flight
.clr:
    mov byte [ark_pukind+bx], PU_NONE
    mov byte [ark_puwipe+bx], 0
    inc bx
    cmp bx, ARK_MAXPU
    jb .clr
    xor bx, bx
.clr2:
    mov byte [ark_shot+bx], 0
    mov byte [ark_shwipe+bx], 0
    inc bx
    cmp bx, ARK_MAXSHOT
    jb .clr2
    mov byte [ark_full], 1
    pop dx
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
    mov byte [ark_full], 0
    mov byte [ark_msg], 0
    mov byte [ark_stat], 0
    call ark_draw_all
    jmp .unlock
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
ark_draw_all:
    push ax
    push bx
    push cx
    push dx
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [ark_cwid]
    dec cx
    mov dx, [ark_chgt]
    dec dx
    call ark_fillc
    mov al, ARK_RAILCOL             ; the two side rails
    call OSAPI_SET_COLOR
    xor ax, ax
    mov bx, [ark_status]
    mov cx, [ark_rail]
    dec cx
    mov dx, [ark_chgt]
    dec dx
    call ark_fillc
    mov ax, [ark_cwid]
    sub ax, [ark_rail]
    mov bx, [ark_status]
    mov cx, [ark_cwid]
    dec cx
    mov dx, [ark_chgt]
    dec dx
    call ark_fillc
    call ark_draw_wall
    call ark_draw_paddle
    call ark_draw_ball
    call ark_draw_pu
    call ark_draw_shots
    call ark_draw_status
    call ark_draw_msg
    mov ax, [ark_bx]                ; the erase trail starts here
    mov [ark_obx], ax
    mov ax, [ark_by]
    mov [ark_oby], ax
    cmp byte [ark_abon], 0          ; the credits sit on top of everything, and
    je .noab                        ; every full repaint puts them back
    call ark_abdraw
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
    xor bx, bx
.cell:
    call ark_draw_brick
    mov byte [ark_dirty+bx], 0
    inc bx
    cmp bx, ARK_CELLS
    jb .cell
    pop bx
    ret

ark_draw_bricks_dirty:
    push bx
    xor bx, bx
.cell:
    cmp byte [ark_dirty+bx], 0
    je .next
    mov byte [ark_dirty+bx], 0
    call ark_draw_brick
.next:
    inc bx
    cmp bx, ARK_CELLS
    jb .cell
    pop bx
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
    mov si, bx
    mov ax, bx                      ; row and column
    xor dx, dx
    mov cx, ARK_COLS
    div cx
    mov [ark_dcol], dx
    mov [ark_drow], ax
    cmp ax, [ark_rows]
    jae .out
    mov ax, [ark_dcol]
    mul word [ark_bw]
    add ax, [ark_rail]
    mov [ark_dx], ax
    mov ax, [ark_drow]
    mul word [ark_bh]
    add ax, [ark_bricky]
    mov [ark_dy], ax
    mov al, ARK_BG                  ; gone: back to background
    cmp byte [ark_grid+si], 0
    je .paint
    mov bx, [ark_drow]
    and bx, 7
    mov al, [ark_rowcol+bx]
.paint:
    call OSAPI_SET_COLOR
    mov ax, [ark_dx]
    mov bx, [ark_dy]
    mov cx, ax
    add cx, [ark_bw]
    sub cx, 2                       ; 1px of mortar on the right and bottom
    mov dx, bx
    add dx, [ark_bh]
    sub dx, 2
    call ark_fillc
    cmp byte [ark_grid+si], 2
    jb .out
    mov al, CWHITE                  ; the "needs another hit" notch
    call OSAPI_SET_COLOR
    mov ax, [ark_dx]
    mov bx, [ark_dy]
    mov cx, ax
    add cx, 2
    mov dx, bx
    inc dx
    call ark_fillc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_draw_paddle / ark_move_paddle - the bat, and the erase-then-draw pair
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
    mov ax, [ark_px]
    mov bx, [ark_pady]
    sub bx, 2
    mov cx, ax
    inc cx
    mov dx, [ark_pady]
    dec dx
    call ark_fillc
    mov ax, [ark_px]
    add ax, [ark_pw]
    sub ax, 2
    mov bx, [ark_pady]
    sub bx, 2
    mov cx, ax
    inc cx
    mov dx, [ark_pady]
    dec dx
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
    mov al, ARK_BG                  ; the whole lane, so a shrink or a move
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
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    mov ax, [ark_obx]
    mov bx, [ark_oby]
    mov cx, ax
    add cx, [ark_bsz]
    dec cx
    mov dx, bx
    add dx, [ark_bsz]
    dec dx
    call ark_fillc
    mov ax, [ark_oby]               ; did the erase reach the paddle's lane?
    add ax, [ark_bsz]
    cmp ax, [ark_pady]
    jle .nopad
    call ark_draw_paddle
.nopad:
    call ark_draw_ball
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
    mov ah, 0
    mov di, ax                      ; DI = kind, for the colour/letter tables
    mov bx, si
    add bx, bx                      ; BX = the word-indexed slot

    mov al, CBLACK                  ; the 1px frame, as a SOLID rect that the
    call OSAPI_SET_COLOR            ; body is then inset into: two fills for
    mov ax, [ark_pux+bx]            ; what a fill plus a gfx_frame did in five
    mov bx, [ark_puy+bx]            ; (a frame is four of them inside the
    mov cx, ax                      ; kernel), and the same pixels. A capsule
    add cx, ARK_PUW - 1             ; still has an edge against the background
    mov dx, bx                      ; when its own colour is a light one
    add dx, ARK_PUH - 1
    call ark_fillc

    mov al, [ark_pucol+di]
    call OSAPI_SET_COLOR
    mov bx, si
    add bx, bx
    mov ax, [ark_pux+bx]
    inc ax
    mov bx, [ark_puy+bx]
    inc bx
    mov cx, ax
    add cx, ARK_PUW - 3             ; x+1 .. x+PUW-2, y+1 .. y+PUH-2
    mov dx, bx
    add dx, ARK_PUH - 3
    call ark_fillc

    mov al, CBLACK                  ; the letter is black on the body, and the
    call OSAPI_SET_COLOR            ; body fill above left the pen its colour
    mov al, [ark_puletter+di]
    mov bx, si
    add bx, bx
    mov cx, [ark_pux+bx]
    add cx, 2
    mov dx, [ark_puy+bx]
    inc dx
    call ark_charc

    mov bx, si                      ; ...and THIS is where it now sits, which
    add bx, bx                      ; is the only honest thing to erase from
    mov ax, [ark_puy+bx]
    mov [ark_puold+bx], ax
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
    mov ax, [ark_shx+bx]
    mov bx, [ark_shy+bx]
    mov cx, ax
    inc cx
    mov dx, bx
    add dx, 5
    call ark_fillc
    mov bx, si                      ; where it now sits, for the erase
    add bx, bx
    mov ax, [ark_shy+bx]
    mov [ark_shold+bx], ax
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
    mov dx, bx
    add dx, 5
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
; -----------------------------------------------------------------------------
ark_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, ARK_BG
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [ark_cwid]
    dec cx
    mov dx, [ark_status]
    dec dx
    call ark_fillc

    mov ax, [ark_status]            ; the text baseline, centred in the strip
    sub ax, 8
    shr ax, 1
    mov [ark_texty], ax

    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [ark_score]
    call ark_num2str
    mov si, [ark_numptr]
    mov cx, 2
    mov dx, [ark_texty]
    call ark_textc

    mov al, CLGREEN                 ; the lives, as spare paddles
    call OSAPI_SET_COLOR
    mov cl, [ark_lives]
    mov ch, 0
    cmp cx, 6
    jbe .lives
    mov cx, 6                       ; more than six would run into the level
.lives:
    jcxz .level
    xor si, si
.life:
    mov ax, [ark_cwid]
    shr ax, 1
    sub ax, 16
    mov bx, si
    add bx, bx
    add bx, bx
    add bx, bx
    add ax, bx                      ; 8px apart
    mov bx, [ark_texty]
    add bx, 3
    push cx
    mov cx, ax
    add cx, 5
    mov dx, bx
    add dx, 1
    call ark_fillc
    pop cx
    inc si
    cmp si, cx
    jb .life

.level:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov si, ark_s_lv
    call OSAPI_FONT_WIDTH
    mov [ark_tmpa], ax
    mov al, [ark_level]
    mov ah, 0
    call ark_num2str
    mov si, [ark_numptr]
    call OSAPI_FONT_WIDTH
    add ax, [ark_tmpa]
    mov cx, [ark_cwid]
    sub cx, ax
    sub cx, 3
    mov [ark_tmpb], cx
    mov si, ark_s_lv
    mov dx, [ark_texty]
    call ark_textc
    mov cx, [ark_tmpb]
    add cx, [ark_tmpa]
    mov si, [ark_numptr]
    mov dx, [ark_texty]
    call ark_textc
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_msgy - the row the banner sits on: the middle of the open play area,
;            between the bottom of the wall and the paddle
; out: AX = content y; preserves all other registers
; -----------------------------------------------------------------------------
ark_msgy:
    push bx
    push dx
    mov ax, [ark_rows]
    mul word [ark_bh]
    add ax, [ark_bricky]
    mov bx, [ark_pady]
    add ax, bx
    shr ax, 1
    sub ax, 4
    pop dx
    pop bx
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
    mov ax, [ark_rail]
    mov cx, [ark_cwid]
    sub cx, [ark_rail]
    dec cx
    mov dx, bx
    add dx, 8
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
    mov si, ark_s_ready
    mov al, [ark_mode]
    cmp al, M_READY
    je .have
    mov si, ark_s_pause
    cmp al, M_PAUSE
    je .have
    mov si, ark_s_over
    cmp al, M_OVER
    je .have
    mov si, ark_s_clear
    cmp al, M_CLEAR
    je .have
    jmp .out
.have:
    mov al, CYELLOW
    call OSAPI_SET_COLOR
    call OSAPI_FONT_WIDTH           ; centred in the content
    mov cx, [ark_cwid]
    sub cx, ax
    shr cx, 1
    call ark_msgy
    mov dx, ax
    call ark_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_num2str - an unsigned word as decimal, NUL-terminated
; in:  AX = value; out: [ark_numptr] -> the first digit; preserves all
;      registers
; -----------------------------------------------------------------------------
ark_num2str:
    push ax
    push bx
    push dx
    push di
    mov di, ark_numbuf + 7
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
    mov [ark_numptr], di
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ark_fillc / ark_framec / ark_textc / ark_charc - the four primitives, in
; CONTENT coordinates. Everything above measures from the content origin; only
; these four add it.
; ark_fillc/framec: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
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

ark_framec:
    push ax
    push bx
    push cx
    push dx
    add ax, [ark_ox]
    add cx, [ark_ox]
    add bx, [ark_oy]
    add dx, [ark_oy]
    call OSAPI_GFX_FRAME
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
    call OSAPI_FONT_STR
    pop dx
    pop cx
    ret

ark_charc:
    push cx
    push dx
    add cx, [ark_ox]
    add dx, [ark_oy]
    call OSAPI_FONT_CHAR
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
    OS88_MENUSET ark_menus, ark_m_name, ark_oncmd
        OS88_MENU ark_m_game, ark_mi_game, 2
    OS88_MENUSET_END ark_menus

ark_m_name:  db 'Arkanoid', 0
ark_m_game:  db 'Game', 0
ark_mi_game: dw ark_s_new, ark_s_pcmd
ark_s_new:   db 'New Game', 0
ark_s_pcmd:  db 'Pause', 0

ark_ttl:     db 'Arkanoid', 0
ark_s_ready: db 'SPACE TO SERVE', 0
ark_s_pause: db 'PAUSED', 0
ark_s_over:  db 'GAME OVER - N', 0
ark_s_clear: db 'WALL CLEARED', 0
ark_s_lv:    db 'LV', 0

; The credits. Kept to 21 characters a line because CGA's metric set gives a
; 206px content (SPEC.md 44.6) and 21 glyphs plus the margins is what fits it.
ARK_ABLH equ 10                     ; line pitch, px (8px glyphs + 2 of air)
ark_ablines:
    dw ark_ab1, ark_ab2, ark_ab3, ark_ab4, ark_ab5, ark_ab6, ark_ab7, 0
ark_ab1:     db 'Arkanoid for os8088', 0
ark_ab2:     db 'A brick-breaker', 0
ark_ab3:     db 0                   ; a blank line is a line with no glyphs
ark_ab4:     db 'Contributed by', 0
ark_ab5:     db 'github.com/Elendilon,', 0
ark_ab6:     db 'who forked os8088 and', 0
ark_ab7:     db 'wrote Solitaire too.', 0

; What each fifth of the paddle ADDS to the ball's existing vx. Not a velocity
; to replace it with: replacing is what made the bounce feel arbitrary, because
; a ball arriving steeply from the left and one drifting in from the right left
; the paddle identically if they landed in the same zone.
ark_zbias:   dw -2, -1, 0, 1, 2

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
ark_puletter: db 0, 'E', 'C', 'L', 'S', 'X'

; --- metrics records (ARK_NMET words each, copied into bss by ark_entry) --------
; brick w, brick h, rows, rail, status strip, gap under it, paddle w, paddle h,
; ball size, paddle inset from the bottom, wanted content height.
ark_met_big:                        ; VGA 640x480 and Hercules 720x348
    dw 24, 10, 6, 4, 14, 10, 44, 6, 4, 18, 300
ark_met_sml:                        ; CGA 640x200: 137 rows of content, all in
    dw 20,  7, 5, 3, 11,  5, 34, 4, 3, 13, 137

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
    AWORD ark_texty

; --- the game -----------------------------------------------------------------
    AWORD ark_win
    AWORD ark_score
    AWORD ark_left                  ; live bricks
    AWORD ark_hold                  ; ticks left in a death/clear pause
    AWORD ark_px                    ; paddle x
    AWORD ark_pw                    ; ...and its live width
    AWORD ark_pkeep                 ; ticks the paddle keeps moving
    AWORD ark_pspd
    AWORD ark_pvel                  ; pixels the paddle moved this frame
    AWORD ark_vymag                 ; the rally's vertical speed, 2..5
    ABYTE ark_pdir
    ABYTE ark_bpp
    ABYTE ark_mode
    ABYTE ark_wasmode               ; what Pause interrupted
    ABYTE ark_lives
    ABYTE ark_level
    ABYTE ark_stuck                 ; the ball is parked on the paddle
    ABYTE ark_catch
    ABYTE ark_laser
    ABYTE ark_launch                ; Space, set by the UI task
    ABYTE ark_hired                 ; the worker exists
    ABYTE ark_full                  ; the next frame must repaint everything
    ABYTE ark_abon                  ; 1 = it is up, and the worker draws nothing
    AWORD ark_abw                   ; the credit panel's measured width, height,
    AWORD ark_abh                   ; left and top - content coords, all four
    AWORD ark_abl                   ; settled by ark_abmeas before a pixel of
    AWORD ark_abt                   ; it is drawn
    ABYTE ark_stat                  ; ...or at least the status strip
    ABYTE ark_msg                   ; ...or at least the banner
    ABYTE ark_padwipe

; --- the ball -----------------------------------------------------------------
    AWORD ark_bx
    AWORD ark_by
    AWORD ark_bvx
    AWORD ark_bvy
    AWORD ark_obx                   ; where it was drawn last
    AWORD ark_oby
    AWORD ark_nx                    ; the candidate position ark_move1 tests
    AWORD ark_ny
    AWORD ark_adx                   ; the Bresenham walk
    AWORD ark_ady
    AWORD ark_sx
    AWORD ark_sy
    AWORD ark_err
    AWORD ark_nstep

; --- the worker's frame clock -------------------------------------------------
    AWORD ark_due                   ; [ticks] the next frame is due at

; --- drawing scratch ----------------------------------------------------------
    AWORD ark_dx
    AWORD ark_dy
    AWORD ark_drow
    AWORD ark_dcol
    AWORD ark_tmpa
    AWORD ark_tmpb
    AWORD ark_numptr
    ABUF  ark_numbuf, 8

; --- the board and everything in flight ---------------------------------------
    ABUF  ark_grid,  ARK_CELLS      ; hits left in each brick, 0 = gone
    ABUF  ark_dirty, ARK_CELLS      ; ...and which ones need redrawing
    ABUF  ark_pukind, ARK_MAXPU
    ABUF  ark_puwipe, ARK_MAXPU
    ABUF  ark_pux, ARK_MAXPU*2
    ABUF  ark_puy, ARK_MAXPU*2
    ABUF  ark_puold, ARK_MAXPU*2
    ABUF  ark_shot, ARK_MAXSHOT
    ABUF  ark_shwipe, ARK_MAXSHOT
    ABUF  ark_shx, ARK_MAXSHOT*2
    ABUF  ark_shy, ARK_MAXSHOT*2
    ABUF  ark_shold, ARK_MAXSHOT*2

    OS88_BSS ARK_BSS
    OS88_IMAGE_END
