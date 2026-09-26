; =============================================================================
; os8088 - apps/dotdel/dotdel.asm
;
; DOT DELIRIUM (SPEC.md 93) - a maze-chase game in the Pac-Man tradition, with
; SMILES as its hero. A .o88 package at org 0 owning a segment (SPEC.md 20.1),
; prefix `dd_`, embedded icon, one worker task, and no kernel change of any
; kind.
;
; It is NOT a port. The two ports in this tree (SPEC.md 89, 91) were written
; from somebody else's source and inherit its raster; this is written from the
; primitives out, which is why it is the only one of the three that is bigger
; on a Hercules than on a CGA and the only one that goes fullscreen.
;
; --- THE THREE DECISIONS THAT DECIDE EVERYTHING ------------------------------
;
;  1. **ONE `gfx_blit1` PER ACTOR, AND THE BAND IS COMPOSED FROM THE BOARD.**
;     SPEC.md 79.5's fish arrived here first: a sprite is a BAND the caller
;     composed, put down in one call that lays its own background, so drawing
;     it over where it used to be IS the erase and nothing is ever absent. The
;     fish swim over a black sea and could compose the ground from nothing;
;     Smiles runs over a board with dots on it, so the band is copied out of
;     `dd_bd` - this game's own 1bpp picture of the maze - and the sprite is
;     shifted into it. That is the whole renderer: five bands a frame, ~4 KB
;     of RAM, and no erase pass at all. SPEC.md 93.5 is the costing and
;     SPEC.md 93.5.4 the one artefact it trades away.
;
;  2. **THE FRAME IS THE TICK AND THE CLOCK IS NOT THE FRAME.** The render
;     runs once a tick - 18.2 Hz, flat, on every adapter - because that is
;     the fastest clock this machine has that costs nothing to read. The
;     LOGIC runs once per tick of ELAPSED time, however many that is, so a
;     frame the machine could not fit does not slow the game down: it makes
;     the next one advance twice. SPEC.md 93.6. Cyclone's worker (SPEC.md
;     67) misses a frame rather than chasing one, which is right for a game
;     whose motion is per-frame; this one's motion is per-TICK, so it catches
;     up exactly and caps the catch-up at DD_MAXSTEP so a swapped-out machine
;     cannot teleport anybody through a wall.
;
;  3. **THE BOARD IS SIZED FROM THE SURFACE AND THE PIXELS ARE NOT SQUARE.**
;     28x31 tiles is the classic grid and it is the grid on all four
;     adapters; what changes is the tile, computed at layout time from the
;     live content box and from the adapter's PIXEL ASPECT - 1:1 on VGA,
;     1:1.55 on Hercules, 1:2.4 on CGA, 1:1.37 on EGA. So the board is
;     448x403 on a VGA, 448x279 on a Hercules and 224x124 on a CGA, and all
;     three are the same SHAPE on the glass. SPEC.md 93.3.
;
; Keys: arrows or WASD steer, Enter starts, P pauses, F fullscreen, Esc leaves
; fullscreen or returns to the attract screen.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'DOTDEL', dd_entry, 1, OS88_STACK_256
                                ; the worker's stack class (SPEC.md 8.7):
                                ; dd_worker's own chain is 5 deep and its
                                ; deepest leaf is dd_blit_actor, which pushes
                                ; eight registers in front of a far call.
                                ; Measured static 96 over the 64-byte
                                ; interrupt floor is 160; 256 gives 1.6x

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) --------------------------
; Smiles, mouth open to the right, over three dots.
;
;   ................
;   .....######.....
;   ...##########...
;   ..############..
;   .#####......####
;   .#####........##
;   ###########.....
;   ##########......
;   ##########......
;   ###########.....
;   .#####........##
;   .#####......####
;   ..############..
;   ...##########...
;   .....######.....
;   ................
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0x07E0
    dw 0x1FF8
    dw 0x3FFC
    dw 0x7C0F
    dw 0x7C03
    dw 0xFFE0
    dw 0xFFC0
    dw 0xFFC0
    dw 0xFFE0
    dw 0x7C03
    dw 0x7C0F
    dw 0x3FFC
    dw 0x1FF8
    dw 0x07E0
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x07E0
    dw 0x1FF8
    dw 0x3FFC
    dw 0x7C0F
    dw 0x7C03
    dw 0xFFE0
    dw 0xFFC0
    dw 0xFFC0
    dw 0xFFE0
    dw 0x7C03
    dw 0x7C0F
    dw 0x3FFC
    dw 0x1FF8
    dw 0x07E0
    dw 0x0000
    OS88_ICON16_END

; =============================================================================
; Constants
; =============================================================================

DD_COLS   equ 28                ; the classic grid, on every adapter
DD_ROWS   equ 31
DD_TUNR   equ 14                ; THE TUNNEL, which is a hole in the board's
DD_TUNW   equ 6                 ; own edge: this row, and this many columns of
DD_PKT0   equ 9                 ; it at each end. The pocket it opens into is
DD_PKT1   equ 19                ; wall the maze never shows, so the playfield's
DD_INKB   equ (DD_COLS * DD_ROWS + 7) / 8
                                ; outer line follows the board's edge round it
                                ; (SPEC.md 93.2.4) - and every layout in
                                ; ddmzdat.inc carries the same middle band, so
                                ; these are the GAME's numbers, not a layout's
DD_HUDW   equ 88                ; the HUD column, 11 cells wide
DD_TWMAX  equ 16                ; the tile ceiling: 28*16 = 448, and a sprite
DD_FITPADX equ 16               ; the air Thin leaves around the board when it
DD_FITPADY equ 8                ; fits the window to it (SPEC.md 93.3.3.2)
DD_TWMIN  equ 8                 ; ...and its floor: under eight a ghost has no
                                ; face and Smiles has no mouth (SPEC.md 93.3.3)
DD_THMAX  equ 16                ; row is then TWO bytes, which is what keeps
                                ; the composer's inner loop a word wide
DD_SPRB   equ (DD_TWMAX / 8)    ; bytes in one scaled sprite row
DD_SPRSZ  equ DD_SPRB * DD_THMAX ; ...and in one whole scaled sprite

; --- the scratch every band but the title is composed in ----------------------
; FOUR shapes share it, and it is sized from the LARGEST, which is not the one
; it is named after:
;
;   an ACTOR   its tile rounded out to the byte grid at both ends plus the
;              distance it moved: at most 4 bytes by 26 rows = 104
;   a DOT RUN  a whole tile row of the board by the dot's own height:
;              56 bytes by 4 rows = 224, and this is the one that binds
;   the LIVES  five tiles by a tile: 10 bytes by 16 rows = 160
;   a TILE     one tile: 2 bytes by 16 rows = 32
;
; It was sized at the actor's 130 and the dot run walked 26 bytes past it, into
; `dd_bbase` and `dd_rb` - so the composer's own base and stride were zeroed by
; the wipe that was supposed to clear the band, and every rect after it went to
; DS:0000. Every composer clamps its wipe now as well, so the ceiling is
; enforced rather than merely computed.
DD_BANDB  equ 56                ; the widest stride any of them needs
DD_BANDH  equ 4
DD_BANDSZ equ 288               ; ...with room over the 224 that binds
; THE PLANAR BAND'S PLANE (SPEC.md 93.5.19): the widest ACTOR band there can be
; - three tile columns of DD_TWMAX by two tiles of DD_THMAX, the union of two
; boxes less than a tile apart - and not DD_BANDSZ, which is sized by the TEXT
; line. Four of them are the four planes a gfx_blitp takes; a band any bigger
; takes the one-pen band it always did.
DD_PBMAX  equ (3 * DD_TWMAX / 8) * (2 * DD_THMAX)

; --- actors -------------------------------------------------------------------
DD_NGH    equ 4                 ; ghosts
DD_NACT   equ DD_NGH + 1        ; ...and Smiles, who is actor 0
DD_SMILES equ 0

; --- directions. The order is (dx,dy) = right, down, left, up ------------------
DIR_R     equ 0
DIR_D     equ 1
DIR_L     equ 2
DIR_U     equ 3
DIR_NONE  equ 4

; --- sub-pixel ----------------------------------------------------------------
; Positions are the sprite box's top-left in 1/16 px, in BOARD coordinates.
; 448 px of board is 7,168 of these and 465 rows is 7,440, so a word holds
; either with room for the tunnel's overshoot.
DD_SUB    equ 16

; --- game states --------------------------------------------------------------
DDS_ATTRACT equ 0               ; the title, the table and the demo
DDS_READY   equ 1               ; "READY!" before a life starts
DDS_PLAY    equ 2
DDS_DIE     equ 3               ; Smiles' death animation
DDS_CLEAR   equ 4               ; the board flashes between levels
DDS_OVER    equ 5               ; GAME OVER
DDS_ENTER   equ 6               ; typing initials into the table

; --- ghost states -------------------------------------------------------------
GS_HOUSE  equ 0                 ; bobbing in the house, waiting for its cue
GS_OUT    equ 1                 ; leaving the house, walking to the door
GS_ROAM   equ 2                 ; scatter or chase, as the mode timer says
GS_FRIGHT equ 3                 ; running away
GS_EYES   equ 4                 ; eaten: eyes going home

; --- timings, in ticks (18.2 Hz) ----------------------------------------------
DD_MAXSTEP  equ 4               ; the catch-up cap (SPEC.md 93.6.1)
DD_READYT   equ 36              ; ~2.0 s of "READY!"
DD_DIET     equ 18              ; the death: 18 ticks, 989 ms (SPEC.md 93.5.16)
DD_CLEART   equ 30              ; the level-clear flash
DD_OVERT    equ 18              ; GAME OVER alone, before the initials box
                                ; (SPEC.md 93.12.4). It was 72 - four seconds
                                ; of a frozen board with nothing to do - and
                                ; the field asked for one
DD_FRUITT   equ 170             ; how long a fruit waits to be taken
DD_EATPAUSE equ 6               ; the freeze while a ghost's score shows

; --- scoring ------------------------------------------------------------------
DD_PTDOT    equ 10
DD_PTPILL   equ 50
DD_EXTRA    equ 10000           ; ...and one life, once (SPEC.md 93.9.3)

DD_LIVES0   equ 3

DD_PREFW  equ DD_COLS * DD_TWMAX + DD_HUDW + 10
DD_THINW  equ DD_COLS * DD_TWMIN + DD_HUDW + DD_FITPADX + 2
DD_THINH  equ DD_ROWS * 9 + DD_FITPADY + 22

; The title's band: one grid row of "DOT DELIRIUM" at the widest cell the
; layout will hand out (10 px), which is 80 bytes a row by 10 rows.
DD_TBANDSZ equ 80 * 12
DD_INKTITL equ CYELLOW
DD_INKPLAY equ CWHITE

; =============================================================================
; dd_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; Everything expensive happens here and NOT in the first paint: the heap claim
; for the board picture, the scaled sprite set and the maze render are all
; sized from a content box that does not exist until wm_create has returned,
; and a package that discovers it cannot have its memory owes the user a
; refusal rather than a window that never draws.
; =============================================================================
dd_entry:
    push si
    mov si, dd_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [dd_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE
    mov si, dd_pref
    call OSAPI_WM_PREFER            ; preserves the flags, so the CF we owe
                                    ; the loader is still wm_create's
    mov si, dd_menus
    call OSAPI_MENU_SET
    mov si, dd_about
    call OSAPI_ABOUT_SET
    mov bx, [dd_win]
    mov al, 1
    call OSAPI_WM_OWNBG             ; every pixel of the content is ours: the
                                    ; kernel's white fill would be one whole
                                    ; screen of flash before the first frame
    mov bx, [dd_win]
    mov ax, dd_onresize
    call OSAPI_WM_ONRESIZE          ; the box moved under us - a drag across a
                                    ; display seam is the case that matters
                                    ; (SPEC.md 93.4)
    mov bx, [dd_win]
    mov ax, dd_onwake
    call OSAPI_WM_ONWAKE            ; ...and the ONE callback without the gfx
                                    ; lock, which is where a resize has to
                                    ; happen (SPEC.md 93.3.3.2)
    mov byte [dd_wmode], 0          ; THIN by default (SPEC.md 93.3.3.1): it is
                                    ; the board the field's overlay of the real
                                    ; machine matched, and Full is one menu
                                    ; item away. A CGA overrides it at the
                                    ; layout rather than here, because the
                                    ; window can be dragged to one.
                                    ;
                                    ; AND NO FIT IS ASKED FOR HERE. OS88_PREFER
                                    ; already opens the window at DD_THINW x
                                    ; DD_THINH, so the resize this used to
                                    ; request only ever differed by the few
                                    ; rows the WM clamps off - and it cost a
                                    ; second and a third full repaint at
                                    ; startup, which the field counted
                                    ; (SPEC.md 93.3.4.2)
    mov byte [dd_snd], 1            ; ON by default: a maze chase that has to be
                                    ; switched on from a menu before it makes a
                                    ; sound is one that has none, and Game ->
                                    ; Sound is the way off
    call dd_font_get                ; the glyph table, once (SPEC.md 93.5.5)
    call dd_hs_init
    call dd_hs_load
    mov bx, [dd_win]                ; NOTHING IS LAID OUT OR SPAWNED HERE. Two
    clc                             ; different reasons and one moment:
.out:                               ; wm_geom answers CF=1 for a window that is
    pop si                          ; not visible, and OSAPI_TASK_SPAWN refuses
    ret                             ; outright because the loader has not
                                    ; published this instance yet. The LOADER
                                    ; is what shows the window, so both wait
                                    ; for the first paint - dd_relayout_ck
                                    ; claims the picture and cuts the sprites,
                                    ; and dd_spawn_ck starts the game

; =============================================================================
; THE WINDOW CALLBACKS
; =============================================================================

; -----------------------------------------------------------------------------
; dd_paint - W_PAINT: the whole content, from scratch
; in:  SI = window ptr; caller holds the gfx lock and the clip is armed
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
dd_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dd_geom_win                ; the content box may have moved
    xor bl, bl                      ; the UI task: mine to recut
    call dd_relayout_ck             ; ...and may have changed size or display
    call dd_spawn_ck                ; ...and the worker starts here, not at the
    ; --- WHAT DOES THE KERNEL SAY WE OWE? (SPEC.md 11.90.2, 93.5.18) -------
    ; WF_OWNBG is the interlock that lets it narrow a W_PAINT at all, and this
    ; window has carried the flag since its entry proc - so the answer was
    ; there to be read and was not read. An UNCOVER measures as THREE paints:
    ; two that owe an EMPTY rect and one that owes 43% of the content, and all
    ; three cost a whole board. The empty ones are free to skip and that is
    ; taken here; the sub-rect wants a partial draw and is 93.5.18's own item.
    mov bx, [dd_win]
    call OSAPI_WM_DAMAGE
    jc .whole                       ; CF=1: the whole content, and AX..DX are it
    cmp cx, ax
    jb .nothing                     ; x1 < x0 - 11.90.2's "draw NOTHING at all"
    cmp dx, bx
    jb .nothing
.whole:
    mov byte [dd_full], 1
    mov byte [dd_inpaint], 1
    cmp byte [dd_bpp], 1            ; A COLOUR SURFACE MAY ASK FOR PLANES HERE
    jbe .pk                         ; TOO (SPEC.md 93.5.19): the region the
    mov byte [dd_pok], 1            ; kernel arms round a W_PAINT is its own,
.pk:                                ; so gfx_blitp is left to say whether it
                                    ; binds - a real one it refuses, and the
                                    ; band goes down in one pen as it always did
    mov byte [dd_drawing], 1        ; SPEC.md 93.5.17
    call dd_draw
    mov byte [dd_drawing], 0
    mov byte [dd_pok], 0
    mov byte [dd_inpaint], 0
.nothing:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_spawn_ck - start the worker, once, from a window callback with the lock
;               held - which is what OSAPI_TASK_SPAWN requires and what an
;               entry proc is not (it refuses there: the loader has not
;               published the instance yet)
; in:  the gfx lock is held. Preserves every register.
;
; A refusal is TRANSIENT - the task table is full - so the flag is set only on
; success and the next paint tries again.
; -----------------------------------------------------------------------------
dd_spawn_ck:
    cmp byte [dd_spawned], 0
    jne .out
    push ax
    push bx
    mov ax, dd_worker
    mov bx, [dd_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [dd_spawned], 1
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2): the
    ; kernel wrote our segment into this worker's frame before its
    ; first instruction, so mem_frameless pins a region with an
    ; undeclared worker however that region is declared. What a restart
    ; costs is one pass of the loop - the park is inside
    ; OSAPI_TASK_ALIVE and nowhere else (this package is not
    ; OSAPI_MEM_PARKSAFE), which is the TOP of the loop, and every byte
    ; that outlives a pass is a static and moves with us.
    OS88_WORKER_RESTARTABLE dd_worker
.no:
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; dd_onresize - OSAPI_WM_ONRESIZE: our box changed and we did not ask
; in:  SI = window ptr; gfx lock held. Preserves all registers.
;
; A drag across a display seam is the case this exists for: the box is the
; same size and the DEPTH is not, so the pens change even though the layout
; does not. dd_relayout_ck answers both questions from one geometry read.
; -----------------------------------------------------------------------------
dd_onresize:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dd_geom_win
    xor bl, bl                      ; the UI task: mine to recut
    call dd_relayout_ck
    mov byte [dd_full], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_onkey - W_ONKEY: AL = ascii, AH = scan, SI = window ptr
; in:  gfx lock held. Preserves all registers.
;
; STEERING IS NOT HERE. A key event arrives at the typematic rate and stops
; when the key is held without repeating cleanly, which is exactly the input
; a maze game must not be built on; dd_input polls OSAPI_KEY_DOWN once a
; logic step instead (SPEC.md 93.7). What is here is the discrete commands.
; -----------------------------------------------------------------------------
dd_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dd_key_common
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_key_common - the one command handler, shared by the window and the bracket
; in:  AL = ascii, AH = scan. Clobbers everything.
;
; The bracket dispatches no events at all (SPEC.md 53.1), so its loop polls
; int 16h and hands the result here - the same two registers W_ONKEY would
; have brought. One handler, so the two worlds cannot drift about what P does.
; -----------------------------------------------------------------------------
dd_key_common:
    cmp byte [dd_state], DDS_ENTER
    je .initials
    cmp ax, KEY_ALTENTER            ; Alt+Enter is the same door as F (SPEC.md
    je .full                        ; 11.2.1.1) - on AX, because the KSC_ENTER
                                    ; test below shares its scancode and means
                                    ; START. Behind the initials gate with
                                    ; Esc, so typing a name keeps the keyboard
    cmp ah, KSC_ESC
    je .esc
    cmp al, 'f'
    je .full
    cmp al, 'F'
    je .full
    cmp al, 'p'
    je .pause
    cmp al, 'P'
    je .pause
    cmp ah, KSC_ENTER
    je .start
    cmp ah, KSC_SPACE
    je .start
    ret
.start:
    cmp byte [dd_state], DDS_ATTRACT
    jne .ret
    call dd_new_game
.ret:
    ret
.pause:
    cmp byte [dd_state], DDS_PLAY
    jne .ret
    xor byte [dd_paused], 1
    mov byte [dd_hudd], 1
    ret
.full:
    cmp byte [dd_fsx], 0
    jne .leave                      ; F leaves as well as enters (SPEC.md
    call dd_go_fsx                  ; 11.2.1). Entering is only ever reached
    ret                             ; from W_ONKEY, which is the UI task with
                                    ; the lock held - which is the whole of
                                    ; OSAPI_FSX_RUN's context contract
.leave:
    mov byte [dd_fsxq], 1
    ret
.esc:
    cmp byte [dd_fsx], 0
    jne .leave
    cmp byte [dd_state], DDS_ATTRACT
    je .ret
    call dd_attract_begin           ; abandon the game and go back to the
    mov byte [dd_full], 1           ; title, which is what Esc means windowed
    ret
.initials:
    call dd_hs_key
    ret

; -----------------------------------------------------------------------------
; dd_onclick - W_ONCLICK: a click in the content
; in:  CX = x, DX = y, SI = window ptr; gfx lock held. GIVE SI BACK.
;
; A click starts a game from the attract screen and nothing else: this is a
; keyboard game, and a mouse that did anything during play would be a second
; way to steer that the AI demo could trip over.
; -----------------------------------------------------------------------------
dd_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [dd_abon], 0
    jne .card
    cmp byte [dd_state], DDS_ATTRACT
    jne .out
    call dd_new_game
    jmp short .out
.card:
    call dd_abdismiss
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

; -----------------------------------------------------------------------------
; dd_oncmd - AM_ONCMD: the Game menu
; in:  AL = item index, AH = menu index, SI = the owning window
; -----------------------------------------------------------------------------
dd_oncmd:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dd_abdismiss
    or ah, ah
    jnz .winmenu                    ; menu 1 is Window; 0 is Game
    cmp al, 0
    je .new
    cmp al, 1
    je .pause
    cmp al, 2
    je .full
    cmp al, 3
    je .sound
    jmp short .out2
.winmenu:
    or al, al
    jnz .wfull
    xor al, al                      ; Thin. A greyed item cannot be picked, so
    jmp short .wset                 ; there is no CGA case to test for here
.wfull:
    mov al, 1
.wset:
    cmp al, [dd_wmode]
    je .out2                        ; already that way: no re-cut, no flash
    mov [dd_wmode], al
    call dd_weff_calc               ; ...and RESOLVE it here, because the
                                    ; repaint that used to do it is skipped
                                    ; when a resize is owed (93.3.4.1)
    mov byte [dd_lvkind], 0FFh      ; nothing MOVED, so dd_relayout_ck has to
    mov ah, 1                       ; be told the answer changed anyway
    cmp byte [dd_wmode], 0
    jz .wfit
    mov ah, 2                       ; ...to Full: the size the package prefers
.wfit:
    mov [dd_wantfit], ah
    jmp short .out2
.new:
    call dd_new_game
    jmp short .out2
.pause:
    cmp byte [dd_state], DDS_PLAY
    jne .out2
    xor byte [dd_paused], 1
    mov byte [dd_hudd], 1
    jmp short .out2
.full:
    call dd_go_fsx                  ; a menu handler is the UI task with the
    jmp short .out2                 ; lock held, which is what fsx_run wants.
                                    ; It does not return until the game does
.sound:
    xor byte [dd_snd], 1
.out2:
    ; A COMMAND THAT OWES A RESIZE DOES NOT PAINT FIRST (SPEC.md 93.3.4.1).
    ; Painting here draws the whole page at the size the window still has, and
    ; the resize a tick later draws it again at the new one - two layouts, and
    ; the first one's title and table are what the field saw as "duplicated
    ; high score rows" and "the title txt gains an extra line". The resize
    ; repaints; there is nothing this could add.
    cmp byte [dd_wantfit], 0
    jne .out3
    call dd_repaint_now
.out3:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_about / dd_abdismiss - the standard About card (SPEC.md 20.5.1)
; -----------------------------------------------------------------------------
dd_about:
    push bx
    push si
    mov byte [dd_abon], 1
    mov bx, si
    mov si, dd_ablines
    call os88ui_about
    pop si
    pop bx
    ret

dd_abdismiss:
    cmp byte [dd_abon], 0
    je .none
    mov byte [dd_abon], 0
    mov byte [dd_full], 1
.none:
    ret

; -----------------------------------------------------------------------------
; dd_onwake - THE ONE CALLBACK WITHOUT THE GFX LOCK (SPEC.md 93.3.3.2)
;
; Thin means the window fits the board, and OSAPI_WM_RESIZE says in as many
; words that it may not be called with the lock held. Every path that KNOWS a
; fit is owed - the layout, a menu pick - runs under it, so those set
; [dd_wantfit] and ask for this wake instead.
;
; It only ever makes the window SMALLER. Growing would re-cut a bigger tile,
; which would want a bigger window, and the flag is cleared before the resize
; so one request is one resize whatever comes of it.
; in:  SI = our window; ES = KERNEL_SEG (SPEC.md 20's callback rule)
; -----------------------------------------------------------------------------
dd_onwake:
    push ax
    push bx
    push cx
    push dx
    cmp byte [dd_needcut], 0        ; THE WORKER SAW THE WINDOW CHANGE SHAPE
    je .wf                          ; and may not act on it (SPEC.md 93.3.4.3)
    mov byte [dd_needcut], 0
    cmp byte [dd_wantfit], 0        ; ...unless a FIT is about to resize us
    jne .wf                         ; anyway: that recuts and repaints, and
                                    ; doing it here first is one of the four
                                    ; whole frames the field counted (93.3.4.5)
    call OSAPI_GFX_LOCK             ; a wake is the one callback the kernel
    mov ax, KERNEL_SEG              ; runs WITHOUT the lock (SPEC.md 12.8), and
    mov es, ax                      ; dd_repaint_now wants it held
    mov byte [dd_didcut], 0         ; ...AND ONLY IF THERE IS STILL A CUT OWED:
    call dd_geom_win                ; the worker asks a tick before the UI task
    xor bl, bl                      ; can answer, and by the time this runs
    call dd_relayout_ck             ; dd_onresize has usually done it already
    cmp byte [dd_didcut], 0
    je .unlk
    call dd_repaint_now
.unlk:
    call OSAPI_GFX_UNLOCK
    mov ax, KERNEL_SEG
    mov es, ax
.wf:
    cmp byte [dd_wantfit], 0
    je .out
    mov byte [dd_wantfit], 0        ; ...FIRST, so one request is one resize
    cmp byte [dd_ok], 0             ; whatever comes of it
    je .out                         ; no layout, so nothing to fit to
    cmp byte [dd_fsx], 0
    jne .out                        ; a bracket owns the screen: a window
                                    ; resize under it means nothing and the
                                    ; layout on the way out will ask again
    mov bx, [dd_win]
    ; --- BOTH SIZES ARE CONSTANTS, and that is the point -------------------
    ; The first version derived Thin's from [dd_mw]/[dd_mh] and Full's from a
    ; banked size, and both have the same hole: THE LAYOUT HAS NOT RE-RUN when
    ; this fires. Picking Thin from a Full window asked for the FULL board's
    ; fit and shrank by nothing; picking Full with nothing banked did nothing
    ; at all. The shape rule pins Thin's tile at 8 x 9 and Full's at 16 x
    ; whatever fits, so the two answers were never variable - and the window
    ; manager clamps either one to the live screen, exactly as it does at
    ; OSAPI_WM_CREATE.
    ; THE SIZE FOLLOWS [dd_weff], not what was asked for: the display wins
    ; over the pick, so a CGA gets the wide one whichever item set the flag.
    mov cx, DD_THINW
    mov dx, DD_THINH
    cmp byte [dd_weff], 0
    je .fit
    mov cx, DD_PREFW
    mov dx, 520
.fit:
    cmp cx, [es:bx + W_W]
    jne .go
    cmp dx, [es:bx + W_H]
    je .out                         ; already the right size: no repaint
.go:
    call OSAPI_WM_RESIZE
    mov byte [dd_full], 1           ; whatever the kernel repaints, the next
                                    ; frame of OURS is a whole one: a partial
                                    ; frame at the new geometry over a picture
                                    ; drawn at the old one is the whole bug
                                    ; class here (SPEC.md 93.3.4)
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_repaint_now - a full content redraw from a UI callback (lock held)
; -----------------------------------------------------------------------------
dd_repaint_now:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov byte [dd_full], 1
    mov bx, [dd_win]
    call OSAPI_WM_CLIP_SET          ; a menu dispatch arrives with no region
    jc .gone                        ; armed (SPEC.md 11.3)
    mov byte [dd_inpaint], 1
    call dd_pok_win                 ; ...and may the walls have planes? (93.5.19)
    call dd_geom_win
    xor bl, bl                      ; the UI task: mine to recut
    call dd_relayout_ck             ; ...and dd_paint's other half, which this
                                    ; did not have: a menu command can change
                                    ; what the layout is CUT FROM and not one
                                    ; coordinate, so poisoning the banked card
                                    ; kind did nothing until the next resize
                                    ; (SPEC.md 93.3.3.1). It is four compares
                                    ; when nothing moved.
    mov byte [dd_drawing], 1        ; SPEC.md 93.5.17
    call dd_draw
    mov byte [dd_drawing], 0
    mov byte [dd_pok], 0
    mov byte [dd_inpaint], 0
    call OSAPI_WM_CLIP_CLEAR
.gone:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6, 93.6)
;
; The game IS the worker: ghosts have to keep moving between keystrokes, and a
; window callback only runs when something happens to the window.
;
; THE CLOCK AND THE FRAME ARE TWO DIFFERENT THINGS and this loop is where that
; is arranged. It renders once a tick and it advances the game once per tick
; of ELAPSED time. On a machine that keeps up those are the same number and
; nothing is ever visible; on one that does not, the render is what gets
; dropped and the game keeps its speed.
; =============================================================================
dd_worker:
    call OSAPI_GET_TICKS
    mov [dd_last], ax
.loop:
    mov bx, [dd_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here; a
                                    ; clicked close box never returns
    ; --- a fit is asked for HERE, outside any paint (SPEC.md 93.3.4.1) -----
    ; dd_fit_ask used to call OSAPI_WM_WAKE from inside dd_relayout_ck, which
    ; runs inside a W_PAINT: the wake was then dispatched while the kernel was
    ; part-way through painting this very window, and the resize landed in the
    ; middle of its own damage bookkeeping. What the field saw was the desktop
    ; left unpainted. The worker is the one place in this package that is
    ; neither a callback nor under the lock.
    cmp byte [dd_wantfit], 0
    jne .wake
    cmp byte [dd_needcut], 0        ; ...and a RECUT is asked for the same way
    je .tick                        ; (SPEC.md 93.3.4.3)
.wake:
    mov bx, [dd_win]
    call OSAPI_WM_WAKE
.tick:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub bx, [dd_last]               ; signed and wrap-safe by subtraction
    jz .nowork
    js .resync
    cmp bx, DD_MAXSTEP
    jbe .have
    mov bx, DD_MAXSTEP              ; a machine that was away does not get to
.have:                              ; teleport anybody through a wall
    mov [dd_last], ax
    mov cx, bx
.steps:
    push cx
    call dd_step
    pop cx
    loop .steps
    call dd_focus_ck
    call dd_render
    jmp short .sleep
.resync:
    mov [dd_last], ax               ; the counter wrapped or the clock moved
.nowork:
.sleep:
    mov ax, 1
    call OSAPI_TASK_SLEEP           ; one tick, and the deadline is the tick
    jmp .loop                       ; counter itself rather than a private one

; -----------------------------------------------------------------------------
; dd_focus_ck - a real-time game is told when it gains the front and never
;               when it loses it, so it has to ask (SPEC.md 12.6, 44.8)
; -----------------------------------------------------------------------------
dd_focus_ck:
    push ax
    push bx
    cmp byte [dd_fsx], 0
    jne .out                        ; in a bracket we ARE the machine
    call OSAPI_WM_TOP
    cmp bx, [dd_win]
    jne .away
    call OSAPI_MENU_OWNER           ; ...and the second question catches a
    cmp bx, [dd_win]                ; click on the bare desktop, which hands
    jne .away                       ; the bar to Locator without moving the
    mov byte [dd_hasfoc], 1         ; z-order
    jmp short .out
.away:
    mov byte [dd_hasfoc], 0
    cmp byte [dd_state], DDS_PLAY
    jne .out
    cmp byte [dd_paused], 0
    jne .out
    mov byte [dd_paused], 1         ; sticky: a game that restarts the instant
    mov byte [dd_hudd], 1           ; a window is raised is one nobody was
.out:                               ; watching yet
    pop bx
    pop ax
    ret

; =============================================================================
; FULLSCREEN EXCLUSIVE (SPEC.md 53, 93.4.2)
;
; A SAME-MODE bracket: no fsx_mode call, so every kernel drawing slot stays
; legal and the renderer above is the renderer here. What the bracket buys is
; the LOCK - windowed, every frame is an unlock/yield/lock round trip with the
; system arrow erased and redrawn inside it - and the whole screen, which on a
; CGA is the difference between a 224x124 board and a 448x186 one.
;
; It deliberately does NOT also take SPEC.md 11.2's fullscreen window: Paint
; measured what that costs on the way out (SPEC.md 42.7), three full content
; draws where the bracket alone costs one.
; =============================================================================
dd_go_fsx:
    push ax
    push bx
    push cx
    cmp byte [dd_fsx], 0
    jne .out
    call dd_abdismiss               ; a card may not cross into the bracket:
                                    ; no event is dispatched in there, so
                                    ; there would be no way to take it down
    mov byte [dd_fsxq], 0
    mov byte [dd_fsx], 1            ; BEFORE the call: fsx_restore's
                                    ; wm_paint_all runs INSIDE fsx_run and
                                    ; re-enters our own W_PAINT
    mov byte [dd_inbr], 1
    mov ax, dd_fsx_main
    mov bx, [dd_win]
    xor cx, cx                      ; no FSXF_KEEPWORKER: the worker IS the
    call OSAPI_FSX_RUN              ; game loop, and two loops driving one
                                    ; screen is two writers
    mov byte [dd_fsx], 0            ; whether it ran or was refused, we are
    mov byte [dd_inbr], 0           ; back on the desktop
    mov byte [dd_fsxq], 0
    call dd_geom_win                ; ...and NOTHING is banked from the
    xor bl, bl                      ; the UI task: mine to recut
    call dd_relayout_ck             ; bracket: those four words describe a
    mov byte [dd_full], 1           ; rect only a bracket owns
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_fsx_main - the exclusive main. SI = our window, gfx lock held for the
;               whole session, every other task frozen. A near ret is the only
;               way out.
; -----------------------------------------------------------------------------
dd_fsx_main:
    push si
    call dd_geom_fsx                ; **THE RECT THIS BRACKET OWNS** (SPEC.md
    xor bl, bl                      ; the UI task: mine to recut
    call dd_relayout_ck             ; 53.7.1). NOT (0,0) plus OSAPI_VIDEO's
    mov byte [dd_hasfoc], 1         ; in here we ARE the keyboard's owner
    mov byte [dd_full], 1           ; size: a same-mode bracket does not
                                    ; collapse a two-display desktop, so
                                    ; "fullscreen" is THIS display's rect
    OS88_ALTENTER_SEED              ; the Alt+Enter that got us here is still
                                    ; held, and a level read cannot tell that
                                    ; hold from the press that would leave
    call OSAPI_GET_TICKS
    mov [dd_last], ax
.loop:
.keys:
    call os88alt_edge               ; ...and in HERE it arrives by neither
    jnc .k16                        ; route int 16h below serves: no XT BIOS
    mov byte [dd_fsxq], 1           ; enqueues the combination (SPEC.md 9.7.1)
    jmp short .done                 ; and a bracket dispatches no events
                                    ; (53.1). The SAME byte dd_key_common's
.k16:                               ; .leave sets, so one exit path serves both
    mov ah, 1                       ; no events are dispatched in a bracket:
    int 0x16                        ; this IS the UI task, so poll int 16h
    jz .nokey
    xor ah, ah
    int 0x16
    call dd_key_common
    cmp byte [dd_fsxq], 0
    jne .done
    jmp short .keys
.nokey:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub bx, [dd_last]
    jle .nostep
    cmp bx, DD_MAXSTEP
    jbe .have
    mov bx, DD_MAXSTEP
.have:
    mov [dd_last], ax
    mov cx, bx
.steps:
    push cx
    call dd_step
    pop cx
    loop .steps
.nostep:
    call dd_render
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT             ; the frame clock, and it costs nothing:
    cmp byte [dd_fsxq], 0           ; the machine was going to wait anyway
    je .loop
.done:
    mov byte [dd_fsx], 0            ; clear the overlay state INSIDE the
    mov byte [dd_inbr], 0           ; bracket, before fsx_restore's
    mov byte [dd_fsxq], 0           ; wm_paint_all runs - or the thawed worker
    mov byte [dd_paused], 0         ; draws the fullscreen geometry over the
    pop si                          ; desktop (Tracker's and Missile's bug)
    ret

; =============================================================================
; GEOMETRY - the ONE place that answers where we draw and how big it is
; =============================================================================

; -----------------------------------------------------------------------------
; dd_geom_win - bank the windowed content box and this window's display
; out: dd_cx/cy/cw/ch and dd_bpp filled. Clobbers AX/BX/CX/DX/SI.
; -----------------------------------------------------------------------------
dd_geom_win:
    push si
    mov bx, [dd_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [dd_cx], ax
    mov [dd_cy], dx
    mov bx, [dd_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    jc .hidden
    mov [dd_cw], cx
    mov [dd_ch], dx
.hidden:
    mov bx, [dd_win]
    call OSAPI_WM_DISPLAY           ; ...ABOUT THE DISPLAY WE ARE ON, which on
    mov [dd_bpp], dh                ; a mixed machine is not what OSAPI_VIDEO
    mov [dd_vkind], dl              ; answers (SPEC.md 39.16.4)
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_geom_fsx - the same four numbers for the bracket (SPEC.md 53.7.1)
; -----------------------------------------------------------------------------
dd_geom_fsx:
    push si
    call OSAPI_FSX_SURF             ; AX = x, BX = y, CX = w, DX = h
    jc .no
    mov [dd_cx], ax
    mov [dd_cy], bx
    mov [dd_cw], cx
    mov [dd_ch], dx
.no:
    xor bx, bx
    call OSAPI_FSX_CAPS             ; DL = THIS display's VID_* kind, which on
    mov [dd_vkind], dl              ; a mixed machine OSAPI_VIDEO gets wrong
    call dd_depth_of                ; (SPEC.md 53.7.1's second question)
    mov [dd_bpp], al
    pop si
    ret

; dd_depth_of - AL = bits per pixel for the VID_* kind in [dd_vkind]
dd_depth_of:
    mov al, [dd_vkind]
    cmp al, VID_HERC
    je .one
    cmp al, VID_CGA
    je .one
    mov al, 4
    ret
.one:
    mov al, 1
    ret

%include "ddlay.inc"
%include "ddmaze.inc"
%include "ddspr.inc"
%include "ddgame.inc"
%include "ddattr.inc"
%include "ddhs.inc"
%include "ddrend.inc"

; =============================================================================
; TABLES
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) --------------------------
dd_tpl:
    dw 40, 30, DD_COLS * DD_TWMAX + DD_HUDW + 10, 300
    dw dd_ttl, dd_paint, dd_onkey, dd_onclick

; --- what we ask for on each adapter (SPEC.md 11.100.1) -----------------------
; The width is the board at its largest tile plus the HUD column plus the
; frame's two borders. The HEIGHTS ARE DELIBERATELY GENEROUS: a preference is
; clamped by the screen exactly as a template is, so asking for 520 rows on a
; CGA comes back with the 155 it has and the number never had to be right.
    ; VGA and Hercules OPEN AT THE THIN SIZE, because Thin is the default and
    ; a window that opens Full and then resizes itself is a flash the field
    ; reported as a bug (SPEC.md 93.3.3.2). A CGA resolves to Full whatever
    ; the menu says, so it keeps the wide row - and its 200 lines clamp the
    ; height anyway.
    OS88_PREFER dd_pref, DD_THINW, DD_THINH, DD_THINW, DD_THINH, \
                DD_PREFW, 520

; --- the app menu set (SPEC.md 12.2) ------------------------------------------
    OS88_MENUSET dd_menus, dd_name, dd_oncmd
        OS88_MENU dd_m_game, dd_i_game, 4
        OS88_MENU dd_m_win, dd_i_win, 2
    OS88_MENUSET_END dd_menus

dd_name:    db 'Dot Delirium', 0
dd_m_game:  db 'Game', 0
dd_i_game:  dw dd_it_new, dd_it_pause, dd_it_full, dd_it_snd
dd_it_new:   db 'New Game', 0
dd_it_pause: db 'Pause', 0
dd_it_full:  db 'Full Screen', 0
dd_it_snd:   db 'Sound', 0

; --- the Window menu (SPEC.md 93.3.3.1) ---------------------------------------
; TWO ITEMS AND NOT A TOGGLE, because an app menu has no check mark (SPEC.md
; 12.2's OS88_MENU is a title and a list) - so one "Shape" item would give the
; player no way to see which is on, and the board itself is the indicator.
;
; dd_i_win's FIRST WORD IS REWRITTEN AT LAYOUT. A CGA has no Thin to offer -
; 200 lines cannot give 31 rows of a taller tile, so both modes come out 8x4 -
; and 12.2's way of saying so is a string that begins with MENU_DIS. The
; kernel greys it and menu_hover will not land on it, so it cannot be picked.
; SPEC.md 47 rule 3 wants a greyed item to say WHY, which is what the
; parenthesis is for.
dd_m_win:   db 'Window', 0
dd_i_win:   dw dd_it_thin, dd_it_wfull
dd_it_thin:  db 'Thin', 0
dd_it_thind: db MENU_DIS, 'Thin (200 lines)', 0
dd_it_wfull: db 'Full', 0

dd_ttl:     db 'Dot Delirium', 0

; SEVEN lines: the name, the blurb, the two key hints - and the CREDIT, which
; is what SPEC.md 20.5.1.1 made this a shared control for (SPEC.md 93.9). It
; goes LAST, after a blank line, so the keys stay where a player's eye already
; learned to find them.
;
; 'Contributed by Elendilon' is 24 cells, which is exactly what 'A maze chase
; for os8088.' and 'Arrows steer.  P pauses.' already are - so the card is not
; one pixel wider than it was and no adapter's clamp moves.
dd_ablines:
    dw dd_ab1, dd_ab2, dd_ab3, dd_ab4, dd_ab5, dd_ab6, dd_ab7, 0
dd_ab1:     db 'DOT DELIRIUM', 0
dd_ab2:     db 0
dd_ab3:     db 'A maze chase for os8088.', 0
dd_ab4:     db 'Arrows steer.  P pauses.', 0
dd_ab5:     db 'F is full screen.', 0
dd_ab6:     db 0
dd_ab7:     db 'Contributed by Elendilon', 0

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88alt.inc"              ; SPEC.md 11.2.1.1's edge, for the bracket
%include "os88ui.inc"


; --- strings ------------------------------------------------------------------
dd_s_score:  db 'SCORE', 0
dd_s_high:   db 'HIGH', 0
dd_s_level:  db 'LEVEL', 0
dd_s_lives:  db 'LIVES', 0
dd_s_ready:  db 'READY!', 0
dd_s_over:   db 'GAME OVER', 0
dd_s_clear:  db 'LEVEL CLEAR', 0
dd_s_paused: db 'PAUSED', 0
dd_s_play:   db 'PRESS ENTER TO PLAY', 0
dd_s_hshead: db 'HIGH SCORES', 0
dd_s_newhs:  db 'NEW HIGH SCORE: ', 0
dd_s_small:  db 'Window too small.', 0
dd_s_nomem:  db 'Not enough memory.', 0

; The five speeds, as percentages, in SPD_* order (SPEC.md 93.7.5). This is
; the only place the percentages are still numbers rather than an index -
; dd_layout reads them once and dd_axis_step reads the answers.
dd_spct:     dw DD_PCTPAC, DD_PCTGH, DD_PCTFRI, DD_PCTEYE, DD_PCTTUN

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes DD_BSS bytes after the image, and every
; name below is an offset from os88_image_end)
; =============================================================================
%assign DD_BSS 0
%macro DWORDV 1
%1 equ os88_image_end + DD_BSS
%assign DD_BSS DD_BSS + 2
%endmacro
%macro DBYTEV 1
%1 equ os88_image_end + DD_BSS
%assign DD_BSS DD_BSS + 1
%endmacro
%macro DBUFV 2
%1 equ os88_image_end + DD_BSS
%assign DD_BSS DD_BSS + (%2)
%endmacro

; --- the window and the surface ----------------------------------------------
    DWORDV dd_win
    DWORDV dd_cx                    ; the content box, ABSOLUTE screen coords
    DWORDV dd_cy
    DWORDV dd_cw
    DWORDV dd_ch
    DWORDV dd_lcx                   ; ...as it was when the layout was cut
    DWORDV dd_lcy
    DWORDV dd_lcw
    DWORDV dd_lch
    DBYTEV dd_bpp                   ; of the display THIS WINDOW is on
    DBYTEV dd_vkind
    DBYTEV dd_lvkind                ; ...as it was when the layout was cut
    DBYTEV dd_ok                    ; the surface can hold a board
    DBYTEV dd_nomem                 ; ...and when it cannot, it was the ARENA
                                    ; that refused (93.3.4.5)
    DBYTEV dd_started               ; ...and one has been laid at least once
    DBYTEV dd_spawned               ; the worker is running
    DBYTEV dd_full                  ; the next frame owes a whole repaint
    DBYTEV dd_inpaint
    DBYTEV dd_abon                  ; the About card is up
    DBYTEV dd_hasfoc
    DBYTEV dd_snd
    DBYTEV dd_wmode                 ; the WINDOW menu's choice: 0 Thin, 1 Full
    DBYTEV dd_weff                  ; ...and what it RESOLVES to this layout,
                                    ; which a CGA and a bracket both override
    DBYTEV dd_wantfit               ; 1 = Thin owes the window a fit to the
                                    ; board, 2 = Full owes it the full size
    DBYTEV dd_wakph                 ; the dot's warble (SPEC.md 93.10.1): which
    DBYTEV dd_wakt                  ; way round this bite is, how long until
    DWORDV dd_wak2                  ; its second syllable, and what that is
    DBYTEV dd_hudd                  ; the HUD has changed under itself
    DBYTEV dd_hudall                ; ...and ALL of it is owed, labels included
    DBUFV  dd_lscore, 4             ; what each HUD field last had on the glass
    DBUFV  dd_lhigh, 4
    DBUFV  dd_llevel, 4
    DBYTEV dd_llives
    DBYTEV dd_still                 ; nothing GLOBAL is owed this frame...
    DBYTEV dd_drewany               ; ...and in the end nothing was drawn
    DWORDV dd_ovsi                  ; the overlay line last lettered

; --- fullscreen ---------------------------------------------------------------
    DBYTEV dd_fsx                   ; a bracket is up
    DBYTEV dd_inbr                  ; ...and we are inside its proc
    DBYTEV dd_fsxq                  ; it has been asked to leave

; --- the derived geometry ------------------------------------------------------
    DWORDV dd_tw                    ; the tile, px
    DWORDV dd_th
    DWORDV dd_asp                   ; the adapter's pixel aspect * 100
    DWORDV dd_mw                    ; the board, px
    DWORDV dd_mh
    DWORDV dd_sb                    ; the wall picture's row stride, bytes
    DWORDV dd_bdx                   ; the board's screen origin (x on the byte
    DWORDV dd_bdy                   ; grid, which gfx_blit1 requires)
    DWORDV dd_hx                    ; the HUD column
    DWORDV dd_hy
    DWORDV dd_hgap                  ; ...and the gap between its groups
    DWORDV dd_spx                   ; one tick of travel ACROSS, in 1/16 px
    DWORDV dd_spdi                  ; which of the five speeds an actor is on
    DBUFV  dd_stpx, SPD_N * 2       ; ...and the ten answers, resolved once in
    DBUFV  dd_stpy, SPD_N * 2       ; dd_relayout (SPEC.md 93.7.5)
    DWORDV dd_twS                   ; a tile, in the same 1/16 px
    DWORDV dd_thS
    DWORDV dd_mwS                   ; ...and the board's width, for the tunnel
    DWORDV dd_lth                   ; the wall line's thickness
    DWORDV dd_bgap                  ; the border's outer line, that far beyond
    DWORDV dd_rnd                   ; the corner round, in pixels (93.2.3)
    DWORDV dd_tol                   ; how far from square a tile may be, x10
    DBYTEV dd_didcut                ; dd_relayout_ck actually did something
    DBYTEV dd_rn1                   ; dd_rnd_one's two adjacent neighbours
    DBYTEV dd_rn2
    DWORDV dd_bXL                   ; dd_bord_draw's path, banked once (93.2.4)
    DWORDV dd_bXR
    DWORDV dd_bYT
    DWORDV dd_bYB
    DWORDV dd_bPT
    DWORDV dd_bPB
    DWORDV dd_bTT
    DWORDV dd_bTB
    DWORDV dd_bXA                   ; ...and the side being drawn
    DWORDV dd_bXN
    DWORDV dd_bTX
    DWORDV dd_bTW
    DWORDV dd_bSA
    DBYTEV dd_needcut               ; the worker owes the UI task a recut
    DBYTEV dd_inrender              ; dd_board_render is walking the board
    DBYTEV dd_drawing               ; ...and a frame is being drawn off it
    DBYTEV dd_newg                  ; 1 = dd_new_game is loading the board, and
                                    ; the worker takes no step until it has
    DBUFV  dd_cnrmap, DD_INKB          ; which CORRIDOR tiles carry ink (93.2.3.2)
    DWORDV dd_dotw
    DWORDV dd_doth
    DWORDV dd_pilw
    DWORDV dd_pilh
    DWORDV dd_bdseg                 ; the wall picture's claim
    DWORDV dd_bdkb
    DWORDV dd_clipy0                ; the board row dd_blit treats as its top

; --- the board -----------------------------------------------------------------
    DBUFV  dd_grid, DD_COLS * DD_ROWS
    DWORDV dd_ndots
    DBYTEV dd_rmin                  ; rows above this are wall (the demo slice)
    DBUFV  dd_rowoff, DD_ROWS * 2   ; row * DD_COLS, so dd_tile has no MUL
    DBUFV  dd_hdir, DD_COLS * DD_ROWS   ; the way home from every tile, walked
    DBUFV  dd_hq, DD_COLS * DD_ROWS * 2 ; once a board (SPEC.md 93.8.5)
    DWORDV dd_hqh
    DWORDV dd_hqt
    DBYTEV dd_hpc
    DBYTEV dd_hpr
    DBUFV  dd_pilc, DD_NPILL        ; where the pellets are, so the blink
    DBUFV  dd_pilr, DD_NPILL        ; does not have to go and look
    DWORDV dd_piln
    DBYTEV dd_gc                    ; the tile the board walkers are on
    DBYTEV dd_gr
    DBYTEV dd_gt
    DWORDV dd_gx
    DWORDV dd_gy
    DBYTEV dd_nL
    DBYTEV dd_nR
    DBYTEV dd_nT
    DBYTEV dd_nB
    DBYTEV dd_hrunk                 ; the ink a picture run is laying

; --- the sprite set ------------------------------------------------------------
    DBUFV  dd_rmap, DD_THMAX
    DBUFV  dd_cmap, DD_TWMAX       ; ...and its column twin (SPEC.md 93.3.3)
    DBUFV  dd_spr, DD_NSPR * DD_SPRSZ
    DBUFV  dd_w0, DD_MSZ            ; three 16x16 scratch masters, plus one the
    DBUFV  dd_w1, DD_MSZ            ; row shifters build into
    DBUFV  dd_w2, DD_MSZ
    DBUFV  dd_w3, DD_MSZ
    DBYTEV dd_rotd
    DBYTEV dd_ph

; --- the band composer ----------------------------------------------------------
    DBUFV  dd_band, DD_BANDSZ
    DBUFV  dd_pb, 4 * DD_PBMAX      ; the planar band's four planes, plane 3
                                    ; holding the GROUND until it is composed
                                    ; (SPEC.md 93.5.19)
    DWORDV dd_pn                    ; ...one plane's bytes, which is the step
    DBYTEV dd_pok                   ; 1 = gfx_blitp may be asked this frame
    DBYTEV dd_bgnd                  ; the band's ground: 0 none, 1 in plane 3,
                                    ; 2 folded into the band in one pen
    DBYTEV dd_bok                   ; 1 = this actor's bands put every wall and
                                    ; corner pixel down in the WALL's pen
    DBYTEV dd_pink                  ; dd_emit_planar's ink
    DBYTEV dd_bplan                 ; 1 = dd_blit is gfx_blitp's (dd_blitp)
    DWORDV dd_bbase                 ; which buffer dd_band_rect is filling
    DWORDV dd_rb                    ; ...and its stride
    DWORDV dd_rx                    ; a dot run
    DWORDV dd_ry
    DWORDV dd_rw
    DBYTEV dd_runa
    DWORDV dd_nx                    ; where an actor is, in whole px
    DWORDV dd_ny
    DWORDV dd_px                    ; ...and where it was
    DWORDV dd_py
    DWORDV dd_bx0                   ; the union band
    DWORDV dd_bx1
    DWORDV dd_by0
    DWORDV dd_by1
    DWORDV dd_bw
    DWORDV dd_bh
    DWORDV dd_sbo                   ; the sprite's byte and bit inside it
    DWORDV dd_ssh
    DBYTEV dd_vmask                 ; ...and which of its three byte columns
                                    ; fall inside the band at all
    DWORDV dd_sx                    ; where dd_band_one is putting a sprite
    DWORDV dd_sy
    DWORDV dd_srow
    DWORDV dd_c0                    ; the tile rectangle a band covers
    DWORDV dd_c1
    DWORDV dd_r0
    DWORDV dd_r1
    DWORDV dd_ux0                   ; ...and the PIXEL rect it has to hold,
    DWORDV dd_ux1                   ; which is a sub-rectangle of that one
    DWORDV dd_uy0                   ; (SPEC.md 93.5.15)
    DWORDV dd_uy1
    DWORDV dd_ic                    ; ...and the tile of it being composed
    DWORDV dd_ir
    DWORDV dd_ix                    ; ...at this origin inside the band, which
    DWORDV dd_iy                    ; is <= 0 once the band is cut to the union
    DWORDV dd_ix0                   ; ...the value dd_ix restarts each row at
    DWORDV dd_iy0
    DBYTEV dd_sh2                   ; 8 - [dd_ssh], out of the row loop

; --- the HUD --------------------------------------------------------------------
    DWORDV dd_hrow
    DWORDV dd_gseg                  ; the kernel's 8x8 glyph table
    DWORDV dd_goff
    DBYTEV dd_gfirst
    DBYTEV dd_glast
    DWORDV dd_tn                    ; the line being composed
    DWORDV dd_tx
    DWORDV dd_ty
    DBYTEV dd_tink
    DBYTEV dd_tblank                ; ...and it is BLANK, glyphs and all
    DWORDV dd_ovx                   ; the overlay line, banked so it can be
    DWORDV dd_ovy                   ; taken down again
    DWORDV dd_ovw
    DBYTEV dd_ovc0                  ; ...and the tiles its 8 rows SPILT on to,
    DBYTEV dd_ovc1                  ; which is none of them until a tile is
    DBYTEV dd_ovr0                  ; shorter than the band (dd_ov_spill)
    DBYTEV dd_ovr1
    DBYTEV dd_ovcc                  ; ...and the column the walk is at
    DBUFV  dd_numbuf, DD_NUMW + 2
    DBYTEV dd_ltmp

; --- the actors -----------------------------------------------------------------
    DBUFV  dd_x, DD_NACT * 2        ; the sprite box's top-left, 1/16 px
    DBUFV  dd_y, DD_NACT * 2
    DBUFV  dd_ox, DD_NACT * 2       ; ...and where it was last DRAWN, whole px
    DBUFV  dd_oy, DD_NACT * 2
    DBUFV  dd_pbx, DD_NACT * 2      ; where each actor was DRAWN, banked per
    DBUFV  dd_pby, DD_NACT * 2      ; actor because dd_actor_px writes one pair
                                    ; and prep runs five times before an emit
    DBUFV  dd_bux0, DD_NACT * 2     ; ...and the PIXEL union of the two boxes,
    DBUFV  dd_bux1, DD_NACT * 2     ; banked for the same reason.  The tile
    DBUFV  dd_buy0, DD_NACT * 2     ; range beside it is what the composers
    DBUFV  dd_buy1, DD_NACT * 2     ; iterate; this is what is DRAWN
    DWORDV dd_fx                    ; ...and the tile box dd_tile_free tests
    DWORDV dd_fy
    DBUFV  dd_dir, DD_NACT
    DBUFV  dd_want, DD_NACT
    DBUFV  dd_alive, DD_NACT
    DBUFV  dd_shown, DD_NACT
    DBUFV  dd_img, DD_NACT
    DBUFV  dd_limg, DD_NACT
    DBUFV  dd_qc0, DD_NACT          ; the band rect each actor held LAST frame,
    DBUFV  dd_qc1, DD_NACT          ; so the tiles it has since left can be put
    DBUFV  dd_qr0, DD_NACT          ; back in their own pen (SPEC.md 93.5.10)
    DBUFV  dd_qr1, DD_NACT
    DBUFV  dd_qok, DD_NACT
    DBUFV  dd_qgk, DD_NACT          ; ...and whether that rect's walls came out
                                    ; in their own pen, so leaving one owes
                                    ; nothing (SPEC.md 93.5.19)
    DBUFV  dd_repc, DD_REPN         ; ...the ring of tiles that owe one
    DBUFV  dd_repr, DD_REPN
    DWORDV dd_reph
    DWORDV dd_rept         ; ...and the image it was last DRAWN with
    DBUFV  dd_inkof, DD_NACT
    DBUFV  dd_nc0, DD_NACT          ; the tile rectangle of each actor's band
    DBUFV  dd_nc1, DD_NACT          ; for this frame (SPEC.md 93.5.1)
    DBUFV  dd_nr0, DD_NACT
    DBUFV  dd_nr1, DD_NACT
    DBUFV  dd_uok, DD_NACT          ; ...whether it has one
    DBUFV  dd_utel, DD_NACT         ; ...and whether it got here by teleport
    DBUFV  dd_ac, DD_NACT           ; the tile it is in NOW, carried
    DBUFV  dd_ar, DD_NACT           ; along by dd_advance instead of divided
    DBUFV  dd_acx, DD_NACT * 2      ; out of the position (SPEC.md 93.7)
    DBUFV  dd_ary, DD_NACT * 2
    DBYTEV dd_lastd                 ; the way Smiles was last going

; --- the ghosts ------------------------------------------------------------------
    DBUFV  dd_gs, DD_NGH
    DBUFV  dd_glook, DD_NGH
    DBUFV  dd_gbig, DD_NGH
    DBUFV  dd_glance, DD_NGH
    DBUFV  dd_gsee, DD_NGH          ; it has a clear line to Smiles this tick
    DBUFV  dd_gseed, DD_NGH         ; ...and the direction of it
    DBUFV  dd_ghunt, DD_NGH         ; it is tracking him (SPEC.md 93.8.1)
    DBUFV  dd_ghlc, DD_NGH          ; ...and the tile he was last seen on
    DBUFV  dd_ghlr, DD_NGH
    DBUFV  dd_gwout, DD_NGH         ; it was walking out of the pen when a
                                    ; pellet turned it blue
    DBUFV  dd_gwait, DD_NGH         ; ticks it still owes the pen after getting
                                    ; home as eyes (SPEC.md 93.8.6)
    DWORDV dd_dstart                ; where the demo's try order starts this
                                    ; decision (SPEC.md 93.11.3)
    DWORDV dd_gi
    DWORDV dd_gtry
    DWORDV dd_gc0
    DWORDV dd_pc                    ; Smiles' tile, banked for the targets
    DWORDV dd_pcx                   ; ...and his box, for the collision test
    DWORDV dd_pcy
    DWORDV dd_tgt
    DWORDV dd_best
    DBYTEV dd_bestd
    DBYTEV dd_trydir
    DWORDV dd_dc                    ; the tile a decision is being taken on
    DWORDV dd_dc2
    DWORDV dd_modeidx
    DWORDV dd_modetim
    DWORDV dd_modech                ; 0 scatter, 1 chase
    DWORDV dd_frcnt                 ; the frightened clock
    DBYTEV dd_eatn                  ; ghosts eaten on this pellet
    DWORDV dd_gpause
    DWORDV dd_relt                  ; ticks since anything was eaten

; --- the game --------------------------------------------------------------------
    DBUFV  dd_score, 4
    DWORDV dd_eaten                 ; dots eaten on this board
    DBYTEV dd_lives
    DBYTEV dd_level
    DBYTEV dd_extra                 ; the free life has been given
    DBYTEV dd_demo
    DBYTEV dd_paused
    DBYTEV dd_state
    DWORDV dd_tim
    DWORDV dd_anim
    DWORDV dd_frames                ; frames drawn - the check on SPEC.md 93.6
    DWORDV dd_fulls                 ; ...and how many of them were WHOLE ones
    DWORDV dd_last                  ; the tick the last logic step was for
    DWORDV dd_rem
    DBYTEV dd_fruits
    DWORDV dd_fruit
    DWORDV dd_frtim
    DBYTEV dd_frdirty
    DBYTEV dd_pilon                 ; the pellets are in their lit phase
    DBYTEV dd_pilt
    DBYTEV dd_pilrf

; --- the attract screen ------------------------------------------------------------
    DBYTEV dd_blink
    DBYTEV dd_blinkt
    DBYTEV dd_blinkd
    DBYTEV dd_strip
    DWORDV dd_atr0                  ; the first board row the slice shows
    DWORDV dd_tcw                   ; the title grid's cell
    DWORDV dd_tch
    DWORDV dd_titw
    DWORDV dd_tith
    DWORDV dd_tity
    DWORDV dd_taby
    DWORDV dd_tabx
    DWORDV dd_ply
    DWORDV dd_stripy
    DWORDV dd_striph                ; the strip's pixel height, decided before
                                    ; the page is placed (SPEC.md 93.11.4)
    DWORDV dd_grow
    DWORDV dd_gcol
    DBYTEV dd_grbits                ; the title cells of one glyph row
    DBUFV  dd_tband, DD_TBANDSZ
    DBUFV  dd_rowbuf, 24

; --- the table ---------------------------------------------------------------------
    DBUFV  dd_hs, DD_NHS * 4
    DBUFV  dd_hsn, DD_NHS * 3
    DBUFV  dd_hsbuf, DD_HSFSZ
    DBUFV  dd_ini, 4
    DBYTEV dd_inip
    DBUFV  dd_promptb, 32
    DBYTEV dd_boxd                  ; the GAME OVER / initials panel owes a
    DWORDV dd_boxx                  ; repaint, and where it is (SPEC.md 93.12.4)
    DWORDV dd_boxy
    DWORDV dd_boxw
    DWORDV dd_boxh
    DWORDV dd_boxlx                 ; ...and where the PROMPT LINE went, so the
    DWORDV dd_boxly                 ; initials can be redrawn without the panel
    DWORDV dd_inioff                ; where they start inside dd_promptb
    DWORDV dd_dbclus
    DBYTEV dd_dbdrv
    DBUFV  dd_dfind, OSAPI_FIND_SZ

    OS88_BSS DD_BSS
    OS88_IMAGE_END
