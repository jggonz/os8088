; =============================================================================
; os8088 - apps/tamegram/tamegram.asm
;
; TameGram, the thirteenth software package (SPEC.md 48): a four-direction,
; dual-faction containment matrix. A .o88 binary at org 0 that owns a segment
; (SPEC.md 20.1) and reaches every service through the API table in
; os88api.inc. Prefix `tg_`, embedded icon, one worker task.
;
; Game design and original implementation: Jason Page - https://store.amfile.org
; Credited in the About panel under the app's own name (SPEC.md 12.2).
;
; You are not playing a game. You are being tamed.
;
; Units spawn at the centre of a fixed 32x32 Containment Matrix and fall
; toward a randomised edge - the gravity vector re-rolls with every piece
; unless the vector is locked. Each piece belongs to one faction, AMBER or
; GREEN (BLUE/ORANGE in colour-blind mode). Contiguous runs of eight
; same-faction cells purge, what is left settles along the current vector,
; and a cascade keeps going until nothing more clears.
;
; Five things shape the os8088 port, and four of them are the kind of thing
; that reads as "the game is broken" rather than as the rule it violated:
;
;  - **The game IS the worker task** (SPEC.md 20.6), the same shape as
;    Arkanoid: a piece has to keep falling between keystrokes, and a window
;    callback only runs when something happens to the window. Everything the
;    UI task does is set a byte the worker reads.
;
;  - **The worker's UPDATE runs under the gfx lock, not just its drawing.**
;    This is the one place the port departs from Arkanoid, and it is not
;    tidiness. The piece geometry is computed through shared scratch words -
;    tg_cell_of writes [tg_cr]/[tg_cc] from [tg_tr]/[tg_tc] - and the SAME
;    words carry the drawing path. The UI task draws (W_PAINT, the About
;    panel, a menu command) while the worker was mid-`tg_fits`, and the four
;    cells of one trial position get evaluated against two different origins:
;    tg_fits then answers "fits" for a position it never tested, tg_step_grav
;    commits to it, and tg_lock_piece stamps a flat index derived from an
;    off-board row into tg_grid. Every UI callback already runs under the gfx
;    lock (SPEC.md 11/12.2), so taking it around tg_update is what makes a
;    tick atomic against them. It is affordable because the expensive half of
;    a frame - tg_draw_all - was always inside the lock anyway, and the
;    expensive half of an UPDATE (tg_scan_clears over 1,024 cells) runs once
;    per piece lock rather than once per tick.
;
;  - **Every grid index is bounds-checked at ONE place.** tg_gidx answers
;    CF=1 for a cell that is off the board instead of handing back an index,
;    so the belt-and-braces test lives in the routine every reader and the
;    single writer already go through. tg_grid is the last thing in the bss
;    and the package's region is a heap claim (SPEC.md 50.3), so a stray
;    `mov [tg_grid+bx], al` with bx = 1,057 writes into whatever claim
;    follows ours - not into a fault.
;
;  - **Nothing may draw outside the content box.** The gfx_* primitives take
;    absolute screen coordinates and clip to the SCREEN, not to a window
;    (SPEC.md 11.3), and W_PAINT runs with no clip region armed - so a rect
;    that is wrong by a row paints over the window next to us. tg_fillc,
;    tg_framec and tg_str are the only three ways this module reaches the
;    screen and all three clamp to [tg_cwid]/[tg_chgt] first. That is what
;    caught the layout: 32 8px cells plus the HUD want 284 content rows and
;    CGA's desktop band has 136, so the matrix used to hang 19 rows through
;    the bottom of its own window, and the HUD - laid out to 242px - ran off
;    the side of a 128px-wide one.
;
;  - **The cell size comes from the room there IS, and the HUD sets the
;    width.** tg_metrics re-derives [tg_csz] from the LIVE content box on
;    every callback and every frame, so it follows whatever wm_fit actually
;    granted rather than what the entry proc asked for: 8px cells on VGA and
;    Hercules, 3px on CGA's 200 rows. The content is never narrower than the
;    HUD needs (TG_HUD_W) and the matrix is centred in it.
;
; Colour is never the only carrier (SPEC.md 39.4): AMBER is CYELLOW and GREEN
; is CLGREEN, which reduce to the WHITE and DITHER classes respectively, so
; the two factions stay apart on Hercules and CGA. Colour-blind mode swaps in
; CLBLUE/CLRED, which reduce the same two ways round. The HUD band and the
; matrix border are the only two colours picked at BOOT rather than pinned
; ([tg_hudbg]/[tg_frcol]), because CDGRAY under CLGRAY is a legible status
; line on VGA and a 50% pattern under a 50% pattern on the other two.
;
; Keys: arrows are screen-absolute - perpendicular to the vector strafes, with
; the vector is a soft drop, against it is ignored; A/D strafe laterally;
; Space/Z/R rotate; Enter hard-drops; P halts; N new; C colour-blind;
; F locks the vector. The bar carries two menus - Protocol (the four state
; toggles) and Help (How to Play, which puts that same list on screen, since
; nothing else in the app says what any of the keys do).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'TAMEGRAM', tg_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) --------------------------
; The matrix, its cross and two faction blocks. The mask is solid so the glyph
; sits on a clean white underlay on every adapter.
    OS88_ICON16
    dw 0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF
    dw 0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF
    dw 0xFFFF,0x8001,0x8C31,0x8C31,0x8001,0x8181,0x8181,0x9FF9
    dw 0x9FF9,0x8181,0x8181,0x8001,0x8C31,0x8C31,0x8001,0xFFFF
    OS88_ICON16_END

; --- the board, which is the one size there is -------------------------------
TG_SIZE     equ 32
TG_CELLS    equ TG_SIZE * TG_SIZE
TG_CLEAR_N  equ 8
TG_NPCELL   equ 4

TG_G_N      equ 0
TG_G_E      equ 1
TG_G_S      equ 2
TG_G_W      equ 3

TG_M_PLAY   equ 0
TG_M_PAUSE  equ 1
TG_M_OVER   equ 2
TG_M_FLASH  equ 3
TG_M_SETTLE equ 4

TG_EMPTY    equ 0
TG_FA       equ 1
TG_FB       equ 2
TG_MARK     equ 0x80

TG_FALL0    equ 12
TG_FALLMIN  equ 3
; Purge animation frames written to VRAM (the shrinking white core); ~0.5s.
TG_FLASH_T  equ 10

TG_HUD_H    equ 28              ; three 8px text rows and their margins
TG_HUD_W    equ 248             ; the widest HUD row is 242px; this is the
                                ; content width the layout will not go under,
                                ; because tg_str drops a string that would
                                ; leave the box and a silently missing THREAT
                                ; counter is worse than a wide window
TG_CSZ_MAX  equ 8
TG_ABLH     equ 10              ; panel line pitch, and the floor tg_pmeas
TG_ABLH_MIN equ 8               ; walks down to on a short content box
; A panel line that BEGINS with this byte is drawn LEFT-ALIGNED at the panel's
; text margin instead of centred, and the byte itself is not drawn. Same shape
; as the SDK's MENU_DIS, and for the same reason: it costs no second array and
; no per-line struct. Prose and a credit read well centred; a two-column key
; table does not - centring turns it into a ragged diamond.
TG_PLEFT    equ 1

TG_BG       equ CBLACK
; The HUD band and the matrix border are the two things whose colour is
; chosen at BOOT rather than pinned, because SPEC.md 39.4 leaves a 4bpp
; screen three greys and a 1bpp one none. On VGA the band is dark grey with a
; light-grey border; on Hercules and CGA both of those land in the DITHER
; class, and white text on a 50% pattern is legible only in principle - so
; there the band goes black and the border goes solid white, which is the
; only pairing that still says "the status line ends here".
TG_HUD_BG   equ CDGRAY
TG_HUD_BG1  equ CBLACK
TG_FRAME    equ CLGRAY
TG_FRAME1   equ CWHITE
TG_COL_A    equ CYELLOW         ; -> WHITE class on 1bpp (SPEC.md 39.4)
TG_COL_B    equ CLGREEN         ; -> DITHER class
TG_COL_ACB  equ CLBLUE          ; -> DITHER class
TG_COL_BCB  equ CLRED           ; -> WHITE class

; =============================================================================
; Entry
; =============================================================================

; -----------------------------------------------------------------------------
; tg_entry - package entry point (SPEC.md 20.2)
; in:  DS = ES = CS = our segment, IF = 1, gfx lock NOT held
; out: BX = window ptr, CF = wm_create's
;
; An ordinary NEAR proc ending in a near `ret`: the kernel reaches every
; callback through the three-byte dispatcher in our own header (SPEC.md
; 20.1/20.2), so nothing in this file ever writes `retf`. Nothing is drawn and
; no task is spawned here - the loader has not published our instance yet, so
; OSAPI_TASK_SPAWN would simply refuse. The first W_PAINT hires the worker.
; -----------------------------------------------------------------------------
tg_entry:
    push si
    push di
    call OSAPI_VIDEO                ; AX=w BX=h CX=dock top row DH=bpp
    mov [tg_scrw], ax
    mov [tg_dock], cx
    mov byte [tg_hudbg], TG_HUD_BG  ; the two colours SPEC.md 39.4 decides
    mov byte [tg_frcol], TG_FRAME
    cmp dh, 1
    ja .pal
    mov byte [tg_hudbg], TG_HUD_BG1
    mov byte [tg_frcol], TG_FRAME1
.pal:

    ; The content height wm_fit will actually allow: the desktop band is
    ; MBAR_H .. dock-1 and wm_fit shaves one more row, because the drop
    ; shadow is on y+h and a frame that merely REACHES the strip is already
    ; on it. Taking the pixel here keeps our own layout in step with the
    ; window the kernel makes, exactly as apps/arkanoid does.
    mov ax, cx
    sub ax, MBAR_H + TITLE_H + 2
    jns .avail
    xor ax, ax
.avail:
    sub ax, TG_HUD_H                ; ...and the matrix gets what is left
    jns .cells
    xor ax, ax
.cells:
    xor dx, dx
    mov bx, TG_SIZE
    div bx                          ; AX = the biggest cell that fits
    cmp ax, TG_CSZ_MAX
    jbe .cap
    mov ax, TG_CSZ_MAX
.cap:
    or ax, ax
    jnz .csz
    inc ax
.csz:
    mov [tg_csz], ax
    mov bx, TG_SIZE
    mul bx
    mov [tg_boardpx], ax
    mov cx, ax
    add ax, TG_HUD_H
    mov [tg_chgt], ax               ; content height = HUD + matrix
    mov ax, TG_HUD_W                ; content width  = max(matrix, HUD)
    cmp cx, ax
    jbe .wid
    mov ax, cx
.wid:
    mov [tg_cwid], ax

    add ax, 2                       ; frame = content + the 1px sides
    mov [tg_tpl + WT_W], ax
    mov ax, [tg_chgt]
    add ax, TITLE_H + 1
    mov [tg_tpl + WT_H], ax

    mov ax, [tg_scrw]               ; centred horizontally, a third of the way
    sub ax, [tg_tpl + WT_W]         ; down the desktop band
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [tg_tpl + WT_X], ax
    mov ax, [tg_dock]
    sub ax, MBAR_H
    sub ax, [tg_tpl + WT_H]
    jns .yok
    xor ax, ax
.yok:
    xor dx, dx
    mov bx, 3
    div bx
    add ax, MBAR_H
    mov [tg_tpl + WT_Y], ax

    call OSAPI_GET_TICKS            ; seeded once; every later piece, faction
    call OSAPI_SRAND                ; and vector walks on down the same stream
    call tg_newgame

    mov si, tg_tpl
    call OSAPI_WM_CREATE
    jc .done
    mov [tg_win], bx
    mov si, tg_menus
    call OSAPI_MENU_SET
    mov si, tg_about
    call OSAPI_ABOUT_SET            ; 'About TameGram' under our name in the
                                    ; bar; preserves the flags, so the CF
                                    ; wm_create owes the loader survives
.done:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; tg_track - adopt the live content origin and size
; in:  SI = window ptr
; out: [tg_ox]/[tg_oy], [tg_cwid]/[tg_chgt] and the metrics derived from them
; preserves every register
;
; Called at the top of every callback and every rendered frame: the window
; moves, and wm_fit may have clamped what the entry proc asked for. Reading
; the record through ES would work (ES = KERNEL_SEG in a callback) but those
; are FRAME dimensions; OSAPI_WM_GEOM is the same subtraction done once.
; -----------------------------------------------------------------------------
tg_track:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [tg_ox], ax
    mov [tg_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h; CF=1
    mov [tg_cwid], cx               ; only means "not visible", and the two
    mov [tg_chgt], dx               ; sizes are filled in either way
    call tg_metrics
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_metrics - cell size and matrix origin from the LIVE content box
; out: [tg_csz], [tg_boardpx], [tg_bx0]; preserves every register
;
; The whole layout hangs off this, and it is re-derived rather than remembered
; so that a window the kernel sized differently from the request still draws
; entirely inside itself. A zero cell is refused: it makes every cell rect
; inside-out, and an inside-out rect is not a small mistake but a stripe from
; one edge of the screen to the other.
; -----------------------------------------------------------------------------
tg_metrics:
    push ax
    push bx
    push cx
    push dx
    mov bx, TG_SIZE
    mov ax, [tg_chgt]
    sub ax, TG_HUD_H
    jns .hok
    xor ax, ax
.hok:
    xor dx, dx
    div bx
    mov cx, ax                      ; CX = the cell the height allows
    mov ax, [tg_cwid]
    xor dx, dx
    div bx
    cmp ax, cx                      ; ...and the smaller of the two wins
    jbe .have
    mov ax, cx
.have:
    cmp ax, TG_CSZ_MAX
    jbe .cap
    mov ax, TG_CSZ_MAX
.cap:
    or ax, ax
    jnz .set
    inc ax
.set:
    mov [tg_csz], ax
    mul bx
    mov [tg_boardpx], ax
    mov cx, [tg_cwid]               ; centre the matrix under the HUD
    sub cx, ax
    jns .lok
    xor cx, cx
.lok:
    shr cx, 1
    mov [tg_bx0], cx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; UI callbacks - all of them on the UI task, all of them under the gfx lock
; =============================================================================

tg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tg_track
    call tg_draw_all
    call tg_hire                    ; idempotent: only the first paint spawns
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_hire - spawn the worker, once
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
;
; A refusal is a normal outcome - the 12-slot task table can be full - and it
; is transient, so nothing is latched on failure and the next paint tries
; again. What must NOT happen is a second spawn.
; -----------------------------------------------------------------------------
tg_hire:
    push ax
    push bx
    cmp byte [tg_hired], 0
    jne .out
    mov ax, tg_worker
    mov bx, [tg_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [tg_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_onkey - W_ONKEY
; in:  AL = ascii, AH = scan, SI = window ptr; caller holds the gfx lock
;
; Steering only posts a command byte the worker spends on its next tick; the
; keys that change what is on the screen without moving a piece (P, N, C, F)
; repaint here, because the worker may have nothing else to redraw for.
;
; The arrow and grey-Enter SCAN codes are tested first. The numeric keypad and
; some hosts put a non-zero AL alongside the arrow scan, so an `or al,al`
; gate ahead of them swallows exactly the presses that were meant to strafe -
; which reads as "the arrows only work sometimes" rather than as a lost key.
; -----------------------------------------------------------------------------
tg_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax
    call tg_track
    call tg_panel_off               ; the key that takes a panel down is spent
    jc .rep                         ; doing it, and owes the repaint
    cmp bh, 0x4B
    je .aleft
    cmp bh, 0x4D
    je .aright
    cmp bh, 0x48
    je .aup
    cmp bh, 0x50
    je .adown
    cmp bh, 0x1C                    ; Enter (main or grey)
    je .drop
    or bl, bl
    jnz .asc
    jmp .out

.left:                              ; A/D lateral strafe
    mov byte [tg_cmd], 1
    jmp .out
.right:
    mov byte [tg_cmd], 2
    jmp .out
.aleft:                             ; the arrows are screen-absolute
    mov byte [tg_cmd], 5
    jmp .out
.aright:
    mov byte [tg_cmd], 6
    jmp .out
.aup:
    mov byte [tg_cmd], 7
    jmp .out
.adown:
    mov byte [tg_cmd], 8
    jmp .out
.rot:
    mov byte [tg_cmd], 3
    jmp .out
.drop:
    mov byte [tg_cmd], 4
    jmp .out

.asc:
    cmp bl, 'a'
    je .left
    cmp bl, 'A'
    je .left
    cmp bl, 'd'
    je .right
    cmp bl, 'D'
    je .right
    cmp bl, ' '
    je .rot
    cmp bl, 'z'
    je .rot
    cmp bl, 'Z'
    je .rot
    cmp bl, 'r'
    je .rot
    cmp bl, 'R'
    je .rot
    cmp bl, 13                      ; Enter (CR)
    je .drop
    cmp bl, 10                      ; LF (some hosts)
    je .drop
    cmp bl, 'p'
    je .pause
    cmp bl, 'P'
    je .pause
    cmp bl, 'n'
    je .new
    cmp bl, 'N'
    je .new
    cmp bl, 'c'
    je .cb
    cmp bl, 'C'
    je .cb
    cmp bl, 'f'
    je .flock
    cmp bl, 'F'
    je .flock
    jmp .out

.pause:
    call tg_toggle_halt
    jmp .rep
.new:
    call tg_newgame
    jmp .rep
.cb:
    xor byte [tg_cblind], 1
    jmp .rep
.flock:
    xor byte [tg_glock], 1
.rep:
    call tg_draw_all
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_oncmd - the AM_ONCMD handler for both menus (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0 = Protocol, 1 = Help), SI = window
;      ptr; gfx lock held
;
; Same rules as W_ONCLICK, and the last of them is the one that bites: the
; kernel does not repaint after a command handler returns, so this must - which
; is why every path here, the unrecognised one included, falls into `.rep`.
;
; AH is decoded, not ignored. It was ignored while there was one menu, and a
; second one makes that a silent mis-dispatch: 'How to Play' is item 0 of menu
; 1, and item 0 of menu 0 is 'New Protocol' - so a help request would have
; thrown the player's game away.
; -----------------------------------------------------------------------------
tg_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tg_track
    call tg_panel_off               ; a panel is replaced, not stacked; the
    or ah, ah                       ; `.rep` below is the one repaint either
    jnz .help                       ; way
    cmp al, 0
    je .new
    cmp al, 1
    je .pause
    cmp al, 2
    je .cb
    cmp al, 3
    je .flock
    jmp .rep
.help:
    or al, al                       ; menu 1 has exactly one item
    jnz .rep
    call tg_help_on
    jmp .rep
.new:
    call tg_newgame
    jmp .rep
.pause:
    call tg_toggle_halt
    jmp .rep
.cb:
    xor byte [tg_cblind], 1
    jmp .rep
.flock:
    xor byte [tg_glock], 1
.rep:
    call tg_draw_all
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tg_toggle_halt - P / 'Halt'. A finished game cannot be paused.
tg_toggle_halt:
    push ax
    cmp byte [tg_mode], TG_M_OVER
    je .out
    cmp byte [tg_mode], TG_M_PAUSE
    je .unp
    mov al, [tg_mode]
    mov [tg_wasmode], al
    mov byte [tg_mode], TG_M_PAUSE
    jmp .out
.unp:
    mov al, [tg_wasmode]
    mov [tg_mode], al
.out:
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
    pop ax
    ret

; -----------------------------------------------------------------------------
; The two panels (SPEC.md 48.7)
;
; There is one panel mechanism and two texts: the credits, reached through
; OSAPI_ABOUT_SET, and 'How to Play', reached through the Help menu. Both are
; drawn INSIDE our own content by tg_draw_panel, last of everything, and
; [tg_panel] is what says one is up: while it stands the worker neither
; updates nor draws, so a falling unit is frozen rather than landing behind
; the text, and the UI task owns the content until a key or a click takes it
; down. [tg_plines] names which text, so opening one panel over the other
; simply replaces it.
;
; A dialog of its own would be a second window, and a package's instance is
; bound to ONE (SPEC.md 29) - so the second window's close box lands on the
; instance teardown path and takes the game with it. Drawing in our own
; content is what apps/arkanoid and apps/missile do for the same reason.
; -----------------------------------------------------------------------------

; tg_about - the OSAPI_ABOUT_SET handler: a window callback in every respect
; in:  SI = our window ptr; caller holds the gfx lock
tg_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tg_track
    mov byte [tg_panel], 1
    mov word [tg_plines], tg_ablines
    call tg_draw_all                ; ...which draws the panel last, because
    pop di                          ; [tg_panel] is set
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tg_help_on - arm the 'How to Play' panel; the caller repaints
; preserves every register
tg_help_on:
    mov byte [tg_panel], 2
    mov word [tg_plines], tg_hlines
    ret

; -----------------------------------------------------------------------------
; tg_panel_off - take the panel down if it is up
; in:  origin tracked, gfx lock held
; out: CF = 1 the click/key was spent doing it; preserves every register
;
; It does NOT repaint. Every caller either repaints anyway (tg_oncmd always
; ends in one, and would otherwise draw the content twice for one click) or
; branches to its own `.rep` on the CF.
; -----------------------------------------------------------------------------
tg_panel_off:
    cmp byte [tg_panel], 0
    je .none
    mov byte [tg_panel], 0
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
    stc
    ret
.none:
    clc
    ret

tg_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tg_track
    call tg_panel_off
    jnc .out
    call tg_draw_all                ; settle the repaint here rather than leave
.out:                               ; a frame of panel on screen until the
    pop di                          ; worker's next tick
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The worker: the game itself (SPEC.md 20.6)
; =============================================================================

; -----------------------------------------------------------------------------
; tg_worker - THE background task
; in:  DX = our instance index, DS = ES = CS = our segment, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own: the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; One frame a tick. Both halves of the frame run inside ONE lock hold - see
; the file header: the update and the drawing share their scratch words with
; the UI task's drawing, and the lock is what every UI callback already holds.
; OSAPI_TASK_ALIVE is called with the lock FREE (rule 4: gfx_lock is not
; reentrant, and taking it twice never draws again).
; -----------------------------------------------------------------------------
tg_worker:
.loop:
    mov bx, [tg_win]
    call OSAPI_TASK_ALIVE
    mov ax, 1
    call OSAPI_TASK_SLEEP
    call OSAPI_GFX_LOCK
    call tg_update
    call tg_frame
    call OSAPI_GFX_UNLOCK
    jmp .loop

; -----------------------------------------------------------------------------
; tg_update - advance the state machine by one tick
; in:  gfx lock held; preserves every register
; -----------------------------------------------------------------------------
tg_update:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tg_panel], 0
    jne .out                        ; the credits are up: the UI task owns us
    call tg_do_cmds
    mov al, [tg_mode]
    cmp al, TG_M_PAUSE
    je .out
    cmp al, TG_M_OVER
    je .out
    cmp al, TG_M_FLASH
    je .flash
    cmp al, TG_M_SETTLE
    je .settle
    ; --- PLAY -------------------------------------------------------------
    dec word [tg_fallcd]
    cmp word [tg_fallcd], 0
    jg .out
    mov ax, [tg_fallspd]
    mov [tg_fallcd], ax
    call tg_step_grav
    jmp .out
.flash:
    ; One VRAM "page" per tick: only the marked cells are rewritten (see
    ; tg_draw_flash_pages). A full-board wipe every tick is what ghosted on
    ; the direct-VRAM path - an intermediate black frame behind every shrink.
    mov byte [tg_dirty], 1
    dec word [tg_hold]
    cmp word [tg_hold], 0
    jg .out
    call tg_purge_marked
    mov byte [tg_mode], TG_M_SETTLE
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
    jmp .out
.settle:
    call tg_settle_once
    jc .moved
    call tg_scan_clears
    or ax, ax
    jnz .more
    mov byte [tg_mode], TG_M_PLAY
    call tg_spawn
    jc .dead
    jmp .moved
.more:
    mov byte [tg_mode], TG_M_FLASH
    mov word [tg_hold], TG_FLASH_T
.moved:
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
    jmp .out
.dead:
    mov byte [tg_mode], TG_M_OVER
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_do_cmds - spend the one command byte W_ONKEY posted
; No hold/arm gate: every key event the UI task delivers is applied exactly
; once, on the next worker tick.
; -----------------------------------------------------------------------------
tg_do_cmds:
    push ax
    push bx
    mov al, [tg_cmd]
    mov byte [tg_cmd], 0            ; consumed either way, so a key pressed
    cmp byte [tg_mode], TG_M_PLAY   ; while halted cannot fire on resume
    jne .out
    or al, al
    jz .out
    cmp al, 1
    je .L
    cmp al, 2
    je .R
    cmp al, 3
    je .T
    cmp al, 4
    je .D
    cmp al, 5
    je .AL
    cmp al, 6
    je .AR
    cmp al, 7
    je .AU
    cmp al, 8
    je .AD
    jmp .out
.L:
    mov byte [tg_lat], -1
    call tg_strafe
    jmp .out
.R:
    mov byte [tg_lat], 1
    call tg_strafe
    jmp .out
.T:
    call tg_rotate
    jmp .out
.D:
    call tg_harddrop
    jmp .out
.AL:
    mov word [tg_drow], 0
    mov word [tg_dcol], -1
    call tg_nudge
    jmp .out
.AR:
    mov word [tg_drow], 0
    mov word [tg_dcol], 1
    call tg_nudge
    jmp .out
.AU:
    mov word [tg_drow], -1
    mov word [tg_dcol], 0
    call tg_nudge
    jmp .out
.AD:
    mov word [tg_drow], 1
    mov word [tg_dcol], 0
    call tg_nudge
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Piece geometry
; =============================================================================

; -----------------------------------------------------------------------------
; tg_cell_of - resolve one cell of the current piece
; in:  SI = 0..3, origin [tg_tr]/[tg_tc], [tg_ptype]/[tg_prot] live
; out: [tg_cr]/[tg_cc] absolute row/col - which MAY be off the board
; -----------------------------------------------------------------------------
tg_cell_of:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, [tg_ptype]
    xor bh, bh
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; *8: four (row,col) byte pairs per shape
    mov bx, ax
    mov ax, si
    shl ax, 1
    add bx, ax
    add bx, tg_shapes
    mov al, [bx]                    ; signed row
    mov ah, [bx+1]                  ; signed col
    mov cl, [tg_prot]
    or cl, cl
    jz .nr
.rl:
    mov dl, al                      ; (r,c) -> (c, -r)
    mov al, ah
    neg dl
    mov ah, dl
    dec cl
    jnz .rl
.nr:
    mov dl, al
    mov dh, ah
    mov al, dl
    cbw
    add ax, [tg_tr]
    mov [tg_cr], ax
    mov al, dh
    cbw
    add ax, [tg_tc]
    mov [tg_cc], ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_gidx - flat grid index for (tg_cr, tg_cc)
; out: CF = 0 and BX = the index; CF = 1 the cell is off the board and BX is
;      meaningless. Clobbers AX, DX.
;
; THE bounds test, and it lives here because every reader and the single
; writer already come through this routine. tg_grid is the last object in the
; bss and the region is an ordinary heap claim (SPEC.md 50.3), so an index
; built from an off-board row does not fault - it writes into whatever claim
; sits above ours, or into our own state a row below zero.
; -----------------------------------------------------------------------------
tg_gidx:
    mov ax, [tg_cr]
    cmp ax, 0
    jl .off
    cmp ax, TG_SIZE
    jge .off
    mov dx, [tg_cc]
    cmp dx, 0
    jl .off
    cmp dx, TG_SIZE
    jge .off
    mov dx, TG_SIZE
    mul dx                          ; DX:AX - row is < 32, so AX is the whole
    add ax, [tg_cc]                 ; answer and DX is scratch
    mov bx, ax
    clc
    ret
.off:
    stc
    ret

; -----------------------------------------------------------------------------
; tg_fits - would the piece sit at trial origin BX=row, CX=col?
; out: CF = 1 blocked (off the board, or over an occupied cell)
; -----------------------------------------------------------------------------
tg_fits:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [tg_tr], bx
    mov [tg_tc], cx
    xor si, si
.loop:
    call tg_cell_of
    call tg_gidx
    jc .bad
    mov al, [tg_grid + bx]
    and al, 0x7F                    ; the purge mark is not an occupant
    or al, al
    jnz .bad
    inc si
    cmp si, TG_NPCELL
    jb .loop
    clc
    jmp .done
.bad:
    stc
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tg_strafe:
    push ax
    push bx
    push cx
    mov al, [tg_grav]
    cmp al, TG_G_N
    je .vert
    cmp al, TG_G_S
    je .vert
    mov al, [tg_lat]                ; horizontal fall: lat -1 = up, +1 = down
    cbw
    mov bx, [tg_pr]
    add bx, ax
    mov cx, [tg_pc]
    jmp .try
.vert:
    mov al, [tg_lat]
    cbw
    mov bx, [tg_pr]
    mov cx, [tg_pc]
    add cx, ax
.try:
    call tg_fits
    jc .out
    mov [tg_pr], bx
    mov [tg_pc], cx
    mov byte [tg_dirty], 1
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_nudge - a screen-absolute arrow, from [tg_drow]/[tg_dcol]
; Along the vector -> soft drop (one tg_step_grav); against it -> ignored;
; perpendicular -> one-cell strafe. Never hard-drops; Enter does that.
; -----------------------------------------------------------------------------
tg_nudge:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx                      ; BX/CX = the fall vector
    xor cx, cx
    mov al, [tg_grav]
    cmp al, TG_G_N
    je .fn
    cmp al, TG_G_E
    je .fe
    cmp al, TG_G_S
    je .fs
    mov cx, -1                      ; WEST
    jmp .havef
.fn:
    mov bx, -1
    jmp .havef
.fe:
    mov cx, 1
    jmp .havef
.fs:
    mov bx, 1
.havef:
    mov ax, [tg_drow]               ; soft drop if the input IS the vector
    cmp ax, bx
    jne .notsoft
    mov ax, [tg_dcol]
    cmp ax, cx
    jne .notsoft
    call tg_step_grav
    cmp byte [tg_mode], TG_M_PLAY
    jne .out
    mov ax, [tg_fallspd]
    mov [tg_fallcd], ax
    jmp .out
.notsoft:
    mov ax, bx                      ; against the vector: ignored
    neg ax
    cmp ax, [tg_drow]
    jne .perp
    mov ax, cx
    neg ax
    cmp ax, [tg_dcol]
    je .out
.perp:
    mov al, [tg_grav]               ; keep only the perpendicular component
    cmp al, TG_G_N
    je .vert
    cmp al, TG_G_S
    je .vert
    mov bx, [tg_pr]                 ; horizontal fall: only the row changes
    add bx, [tg_drow]
    mov cx, [tg_pc]
    jmp .try
.vert:
    mov bx, [tg_pr]
    mov cx, [tg_pc]
    add cx, [tg_dcol]
.try:
    call tg_fits
    jc .out
    mov [tg_pr], bx
    mov [tg_pc], cx
    mov byte [tg_dirty], 1
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tg_rotate:
    push ax
    push bx
    push cx
    mov al, [tg_prot]
    mov ah, al
    inc al
    and al, 3
    mov [tg_prot], al
    mov bx, [tg_pr]
    mov cx, [tg_pc]
    call tg_fits
    jnc .ok
    mov [tg_prot], ah               ; refused: put the rotation back
    jmp .out
.ok:
    mov byte [tg_dirty], 1
.out:
    pop cx
    pop bx
    pop ax
    ret

tg_harddrop:
    push ax
.loop:
    call tg_step_grav
    cmp byte [tg_locked], 0
    je .loop
    mov byte [tg_locked], 0
    pop ax
    ret

tg_step_grav:
    push ax
    push bx
    push cx
    mov byte [tg_locked], 0
    mov bx, [tg_pr]
    mov cx, [tg_pc]
    mov al, [tg_grav]
    cmp al, TG_G_N
    je .n
    cmp al, TG_G_E
    je .e
    cmp al, TG_G_S
    je .s
    dec cx                          ; WEST
    jmp .try
.n:
    dec bx
    jmp .try
.e:
    inc cx
    jmp .try
.s:
    inc bx
.try:
    call tg_fits
    jc .lock
    mov [tg_pr], bx
    mov [tg_pc], cx
    mov byte [tg_dirty], 1
    jmp .out
.lock:
    call tg_lock_piece
    mov byte [tg_locked], 1
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_lock_piece - stamp the active piece into the grid and take what follows
;
; The only writer of tg_grid outside tg_settle_once/tg_purge_marked, and the
; `jc .next` on tg_gidx is the whole of its memory safety: the origin is one
; the last tg_fits accepted, so every cell IS on the board - but "is" here
; rests on two tasks agreeing about [tg_tr]/[tg_tc], and the cost of being
; wrong is a write past the end of the bss rather than a misdrawn cell.
; -----------------------------------------------------------------------------
tg_lock_piece:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [tg_pr]
    mov cx, [tg_pc]
    mov [tg_tr], bx
    mov [tg_tc], cx
    xor si, si
.stamp:
    call tg_cell_of
    call tg_gidx
    jc .next
    mov al, [tg_pfac]
    mov [tg_grid + bx], al
.next:
    inc si
    cmp si, TG_NPCELL
    jb .stamp
    mov byte [tg_hasop], 0          ; stamped: these cells are the board's now
    add word [tg_score], 1          ; and must NOT be erased as a stale piece
    call tg_scan_clears
    or ax, ax
    jz .noclear
    mov byte [tg_mode], TG_M_FLASH
    mov word [tg_hold], TG_FLASH_T
    jmp .paint
.noclear:
    call tg_spawn
    jc .dead
.paint:
    mov byte [tg_full], 1           ; the HUD moved and the board grew a piece
    mov byte [tg_dirty], 1
    jmp .out
.dead:
    mov byte [tg_mode], TG_M_OVER
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Clears / settle
; =============================================================================

; -----------------------------------------------------------------------------
; tg_scan_clears - mark every complete run of eight
; out: AX = cells marked (0 = nothing purges)
; -----------------------------------------------------------------------------
tg_scan_clears:
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = marked count

    xor bx, bx                      ; --- rows
.rrow:
    xor cx, cx
.rcol:
    call tg_at                      ; AL = cell & 7F at (bx, cx)
    or al, al
    jz .rskip
    mov ah, al                      ; the run's faction
    mov si, cx                      ; where it started
    mov dx, 1                       ; how long it is
.rrun:
    inc cx
    cmp cx, TG_SIZE
    jge .rend
    call tg_at
    cmp al, ah
    jne .rend
    inc dx
    jmp .rrun
.rend:
    call tg_run_mark_h
    jmp .rcont
.rskip:
    inc cx
.rcont:
    cmp cx, TG_SIZE
    jl .rcol
    inc bx
    cmp bx, TG_SIZE
    jl .rrow

    xor cx, cx                      ; --- columns
.ccol:
    xor bx, bx
.crow:
    call tg_at
    or al, al
    jz .cskip
    mov ah, al
    mov si, bx
    mov dx, 1
.crun:
    inc bx
    cmp bx, TG_SIZE
    jge .cend
    call tg_at
    cmp al, ah
    jne .cend
    inc dx
    jmp .crun
.cend:
    call tg_run_mark_v
    jmp .ccont
.cskip:
    inc bx
.ccont:
    cmp bx, TG_SIZE
    jl .crow
    inc cx
    cmp cx, TG_SIZE
    jl .ccol

    mov ax, di
    or ax, ax
    jz .done
    push ax                         ; purges += cells/8, score += 10 each
    mov bx, TG_CLEAR_N
    xor dx, dx
    div bx
    add [tg_purges], ax
    mov bx, ax
    mov ax, 10
    mul bx
    add [tg_score], ax
    pop ax
    cmp word [tg_fallspd], TG_FALLMIN
    jle .done
    mov bx, [tg_purges]             ; every fourth purge speeds the fall
    and bx, 3
    jnz .done
    dec word [tg_fallspd]
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tg_at - AL = grid[bx][cx] & 7F, for BX/CX known to be on the board
; Preserves AH (and BX/CX/DX). Binding: tg_scan_clears keeps the run's faction
; in AH across calls, so clobbering AH with a flat index's high byte makes
; almost every run look one cell long and no purge ever fires.
; -----------------------------------------------------------------------------
tg_at:
    push bx
    push dx
    push ax                         ; keep AH
    mov ax, bx
    mov dx, TG_SIZE
    mul dx
    add ax, cx
    mov bx, ax
    mov dl, [tg_grid + bx]
    and dl, 0x7F
    pop ax
    mov al, dl
    pop dx
    pop bx
    ret

; tg_run_mark_h - mark floor(dx/8)*8 cells of row BX from col SI; adds to DI
tg_run_mark_h:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, dx
    cmp ax, TG_CLEAR_N
    jb .out
    mov dl, TG_CLEAR_N
    div dl                          ; AL = whole groups
    xor ah, ah
    mov dl, TG_CLEAR_N
    mul dl                          ; AX = cells to mark
    mov dx, ax
    mov cx, si
.mk:
    or dx, dx
    jz .out
    call tg_mark
    inc di
    inc cx
    dec dx
    jmp .mk
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tg_run_mark_v - the same, down column CX from row SI
tg_run_mark_v:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, dx
    cmp ax, TG_CLEAR_N
    jb .out
    mov dl, TG_CLEAR_N
    div dl
    xor ah, ah
    mov dl, TG_CLEAR_N
    mul dl
    mov dx, ax
    mov bx, si
.mk:
    or dx, dx
    jz .out
    call tg_mark
    inc di
    inc bx
    dec dx
    jmp .mk
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tg_mark - OR TG_MARK into grid[bx][cx]
tg_mark:
    push ax
    push bx
    push dx
    mov ax, bx
    mov dx, TG_SIZE
    mul dx
    add ax, cx
    mov bx, ax
    or byte [tg_grid + bx], TG_MARK
    pop dx
    pop bx
    pop ax
    ret

tg_purge_marked:
    push bx
    xor bx, bx
.p:
    test byte [tg_grid + bx], TG_MARK
    jz .n
    mov byte [tg_grid + bx], TG_EMPTY
.n:
    inc bx
    cmp bx, TG_CELLS
    jb .p
    pop bx
    ret

; -----------------------------------------------------------------------------
; tg_settle_once - slide everything one step along the vector
; out: CF = 1 if anything moved
; -----------------------------------------------------------------------------
tg_settle_once:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di
    mov al, [tg_grav]
    cmp al, TG_G_S
    je .south
    cmp al, TG_G_N
    je .north
    cmp al, TG_G_E
    je .east
    jmp .west

.south:
    xor cx, cx
.sc:
    mov bx, TG_SIZE - 2
.sr:
    call tg_at
    or al, al
    jz .sn
    push bx
    inc bx
    call tg_at
    pop bx
    or al, al
    jnz .sn
    call tg_at
    mov dl, al
    call tg_clr
    inc bx
    call tg_put
    dec bx
    mov di, 1
.sn:
    dec bx
    jns .sr
    inc cx
    cmp cx, TG_SIZE
    jl .sc
    jmp .fin

.north:
    xor cx, cx
.nc:
    mov bx, 1
.nr:
    call tg_at
    or al, al
    jz .nn
    push bx
    dec bx
    call tg_at
    pop bx
    or al, al
    jnz .nn
    call tg_at
    mov dl, al
    call tg_clr
    dec bx
    call tg_put
    inc bx
    mov di, 1
.nn:
    inc bx
    cmp bx, TG_SIZE
    jl .nr
    inc cx
    cmp cx, TG_SIZE
    jl .nc
    jmp .fin

.east:
    xor bx, bx
.er:
    mov cx, TG_SIZE - 2
.ec:
    call tg_at
    or al, al
    jz .en
    push cx
    inc cx
    call tg_at
    pop cx
    or al, al
    jnz .en
    call tg_at
    mov dl, al
    call tg_clr
    inc cx
    call tg_put
    dec cx
    mov di, 1
.en:
    dec cx
    jns .ec
    inc bx
    cmp bx, TG_SIZE
    jl .er
    jmp .fin

.west:
    xor bx, bx
.wr:
    mov cx, 1
.wc:
    call tg_at
    or al, al
    jz .wn
    push cx
    dec cx
    call tg_at
    pop cx
    or al, al
    jnz .wn
    call tg_at
    mov dl, al
    call tg_clr
    dec cx
    call tg_put
    inc cx
    mov di, 1
.wn:
    inc cx
    cmp cx, TG_SIZE
    jl .wc
    inc bx
    cmp bx, TG_SIZE
    jl .wr

.fin:
    or di, di
    jz .no
    stc
    jmp .done
.no:
    clc
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tg_clr / tg_put at (bx,cx), both on the board by construction; put takes DL
tg_clr:
    push ax
    push bx
    push dx
    mov ax, bx
    mov dx, TG_SIZE
    mul dx
    add ax, cx
    mov bx, ax
    mov byte [tg_grid + bx], 0
    pop dx
    pop bx
    pop ax
    ret

tg_put:
    push ax
    push bx
    push dx
    push dx                         ; DL is the value; the mul below writes DX
    mov ax, bx
    mov dx, TG_SIZE
    mul dx
    add ax, cx
    mov bx, ax
    pop dx
    mov [tg_grid + bx], dl
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Spawn / new game
; =============================================================================
tg_spawn:
    push ax
    push bx
    push cx
    call OSAPI_RAND
    and ax, 7                       ; 0..7 over seven shapes: 7 folds onto 6
    cmp ax, 7
    jb .pt
    mov ax, 6
.pt:
    mov [tg_ptype], al
    mov byte [tg_prot], 0
    call OSAPI_RAND
    and al, 1
    inc al
    mov [tg_pfac], al
    cmp byte [tg_glock], 0
    jne .sg
    call OSAPI_RAND                 ; a fresh vector unless it is locked
    and al, 3
    mov [tg_grav], al
.sg:
    mov word [tg_pr], 14
    mov word [tg_pc], 14
    mov bx, 14
    mov cx, 14
    call tg_fits
    jc .fail
    mov ax, [tg_fallspd]
    mov [tg_fallcd], ax
    mov byte [tg_dirty], 1
    clc
    jmp .out
.fail:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

tg_newgame:
    push ax
    push bx
    xor bx, bx
.z:
    mov byte [tg_grid + bx], 0
    inc bx
    cmp bx, TG_CELLS
    jb .z
    mov word [tg_score], 0
    mov word [tg_purges], 0
    mov word [tg_fallspd], TG_FALL0
    mov word [tg_fallcd], TG_FALL0
    mov byte [tg_mode], TG_M_PLAY
    mov byte [tg_grav], TG_G_S
    mov byte [tg_glock], 0
    mov byte [tg_panel], 0
    mov byte [tg_cmd], 0
    mov byte [tg_hasop], 0          ; no previous piece to erase yet
    mov byte [tg_full], 1
    mov byte [tg_dirty], 1
    call tg_spawn                   ; cannot fail on an empty board
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing
; =============================================================================

; -----------------------------------------------------------------------------
; tg_frame - the worker's half of a frame
; in:  gfx lock HELD by tg_worker; preserves every register
;
; A PLAY frame costs the four cells the piece left and the four it arrived at.
; [tg_full] is the latch for everything else: a lock, a purge, a settle step,
; a mode change or a dismissed About all raise it and buy one whole repaint.
; -----------------------------------------------------------------------------
tg_frame:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tg_panel], 0
    jne .out                        ; the UI task owns the content
    cmp byte [tg_dirty], 0
    je .out
    mov bx, [tg_win]
    call OSAPI_WM_GEOM
    jc .out                         ; hidden: nothing to draw on
    mov si, bx
    call tg_track                   ; the window may have been dragged
    mov bx, [tg_win]
    call OSAPI_WM_CLIP_SET          ; cut to whatever of us still shows
    jc .out                         ; (SPEC.md 11.3); CF=1 = wholly covered
    mov byte [tg_dirty], 0
    cmp byte [tg_full], 0
    jne .full
    cmp byte [tg_mode], TG_M_FLASH
    je .pages
    cmp byte [tg_mode], TG_M_PLAY
    je .piece
.full:
    mov byte [tg_full], 0
    call tg_draw_all
    jmp .out
.pages:
    call tg_draw_flash_pages
    jmp .out
.piece:
    call tg_erase_prev
    call tg_draw_active
    call tg_snap_active
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_draw_all - the whole content, from the black up
; in:  gfx lock held, origin tracked; preserves every register
; -----------------------------------------------------------------------------
tg_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, TG_BG
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [tg_cwid]
    dec cx
    mov dx, [tg_chgt]
    dec dx
    call tg_fillc
    call tg_draw_hud
    call tg_draw_board
    cmp byte [tg_mode], TG_M_PLAY
    je .act
    cmp byte [tg_mode], TG_M_PAUSE
    je .act
    jmp .noa
.act:
    call tg_draw_active
    call tg_snap_active             ; the erase target for the next PLAY frame
.noa:
    call tg_draw_banner
    cmp byte [tg_panel], 0
    je .out
    call tg_draw_panel
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_clampc - clamp a content rect to the content box, then add the origin
; in:  AX/BX/CX/DX = x1/y1/x2/y2, inclusive, in content coords
; out: CF = 1 nothing is left to draw; else AX/BX/CX/DX are screen coords
;
; The one place this module turns its own coordinates into the screen's, and
; the reason it exists is SPEC.md 11.3: the gfx_* primitives clip to the
; SCREEN, not to a window, and W_PAINT runs with no clip region armed. A rect
; that is one row too tall therefore paints over the window below us rather
; than being trimmed. Comparisons are SIGNED - a content coordinate can be
; negative, and an unsigned test would read -1 as 65,535 and pass it.
; -----------------------------------------------------------------------------
tg_clampc:
    push si
    or ax, ax
    jns .x1
    xor ax, ax
.x1:
    or bx, bx
    jns .y1
    xor bx, bx
.y1:
    mov si, [tg_cwid]
    dec si
    cmp cx, si
    jle .x2
    mov cx, si
.x2:
    mov si, [tg_chgt]
    dec si
    cmp dx, si
    jle .y2
    mov dx, si
.y2:
    pop si
    cmp ax, cx
    jg .none
    cmp bx, dx
    jg .none
    add ax, [tg_ox]
    add bx, [tg_oy]
    add cx, [tg_ox]
    add dx, [tg_oy]
    clc
    ret
.none:
    stc
    ret

; tg_fillc / tg_framec - a filled / outlined rect in content coords
tg_fillc:
    push ax
    push bx
    push cx
    push dx
    call tg_clampc
    jc .out
    call OSAPI_GFX_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tg_framec:
    push ax
    push bx
    push cx
    push dx
    call tg_clampc
    jc .out
    call OSAPI_GFX_FRAME
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_str - a string in content coords
; in:  CX = x, CX/DX = content coords, SI = NUL string; pen already set
;
; All-or-nothing, and deliberately so: font_char draws a whole 8x8 cell or
; none of it, so a string half outside the box cannot be trimmed to fit. One
; that would leave the box is DROPPED rather than clipped - a missing HUD
; field is a layout bug you can see, and a glyph painted onto the window next
; door is one you cannot.
; -----------------------------------------------------------------------------
tg_str:
    push ax
    push bx
    push cx
    push dx
    or cx, cx
    js .out
    or dx, dx
    js .out
    mov ax, dx
    add ax, 8
    cmp ax, [tg_chgt]
    jg .out
    call OSAPI_FONT_WIDTH           ; AX = the string's pixel width
    add ax, cx
    cmp ax, [tg_cwid]
    jg .out
    add cx, [tg_ox]
    add dx, [tg_oy]
    call OSAPI_FONT_STR
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_draw_hud - the status band above the matrix
; Three 8px rows inside TG_HUD_H, and the layout the TG_HUD_W floor is for.
; -----------------------------------------------------------------------------
tg_draw_hud:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [tg_hudbg]
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [tg_cwid]
    dec cx
    mov dx, TG_HUD_H - 1
    call tg_fillc

    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov cx, 4
    mov dx, 2
    mov si, tg_s_title
    call tg_str
    cmp byte [tg_glock], 0
    je .rows
    mov al, CYELLOW
    call OSAPI_SET_COLOR
    mov cx, 210
    mov dx, 2
    mov si, tg_s_lock
    call tg_str
.rows:
    ; Every column here is one glyph clear of the field before it, and the
    ; rightmost - FACTION's 'ORANGE' at 180 - ends at 228, inside TG_HUD_W.
    ;
    ; CWHITE, not the CLGRAY this had: SPEC.md 39.4 puts CLGRAY and the band's
    ; own CDGRAY in the SAME dither class, so on Hercules and CGA the labels
    ; were a 50% pattern on a 50% pattern - and a dithered glyph loses half of
    ; every stroke besides, which is what left Missile Command's wave counter
    ; not faint but absent. Text comes from the white class; a filled BAND may
    ; dither, because an area survives what a 1px stroke does not.
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov cx, 4
    mov dx, 11
    mov si, tg_s_score
    call tg_str
    mov ax, [tg_score]
    mov cx, 60
    mov dx, 11
    call tg_num
    mov cx, 108
    mov dx, 11
    mov si, tg_s_purges
    call tg_str
    mov ax, [tg_purges]
    mov cx, 164
    mov dx, 11
    call tg_num

    mov cx, 4
    mov dx, 20
    mov si, tg_s_vec
    call tg_str
    mov bl, [tg_grav]
    xor bh, bh
    shl bx, 1
    mov si, [tg_vecname + bx]
    mov cx, 60
    mov dx, 20
    call tg_str
    mov cx, 116
    mov dx, 20
    mov si, tg_s_fac
    call tg_str
    call tg_fac_name                ; SI = the name
    mov cx, 180
    mov dx, 20
    call tg_str
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tg_fac_name:
    cmp byte [tg_cblind], 0
    jne .cb
    cmp byte [tg_pfac], TG_FA
    je .a
    mov si, tg_s_fb
    ret
.a:
    mov si, tg_s_fa
    ret
.cb:
    cmp byte [tg_pfac], TG_FA
    je .ca
    mov si, tg_s_fb_cb
    ret
.ca:
    mov si, tg_s_fa_cb
    ret

; -----------------------------------------------------------------------------
; tg_num - AX in decimal at CX/DX. tg_numbuf holds five digits and a NUL,
;          which is exactly what a 16-bit counter can need.
;
; Binding, and it hid for a whole game at a time: `div bx` takes its dividend
; in DX:AX and leaves the REMAINDER in DX - which is the y coordinate. The y
; is therefore banked in SI before the loop. Left in DX it survived every
; counter that was still zero (the loop does not run at all for 0) and then,
; on the first piece to lock, drew THREAT's digit at y = the digit's own
; ASCII code: '3' is 0x33, so a score of 3 put a stray 3 on board row 3 and
; left the HUD field blank. A wrong number would have been obvious; a number
; somewhere else entirely reads as a corrupted board.
; -----------------------------------------------------------------------------
tg_num:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dx                      ; SI = y, out of the divide's way
    mov di, tg_numbuf + 5
    mov byte [di], 0
    mov bx, 10
    or ax, ax
    jnz .lp
    dec di
    mov byte [di], '0'
    jmp .dr
.lp:
    xor dx, dx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    or ax, ax
    jnz .lp
.dr:
    mov dx, si                      ; ...and back
    mov si, di
    call tg_str
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tg_draw_board:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [tg_frcol]
    call OSAPI_SET_COLOR
    mov ax, [tg_bx0]
    mov bx, TG_HUD_H
    mov cx, ax
    add cx, [tg_boardpx]
    dec cx
    mov dx, TG_HUD_H
    add dx, [tg_boardpx]
    dec dx
    call tg_framec
    xor bx, bx
.row:
    xor cx, cx
.col:
    mov [tg_cr], bx
    mov [tg_cc], cx
    call tg_gidx
    jc .next
    mov al, [tg_grid + bx]
    mov ah, al
    and al, 0x7F
    or al, al
    jz .next
    test ah, TG_MARK
    jnz .fl
    call tg_cell_paint
    jmp .next
.fl:
    call tg_flash_cell
.next:
    mov bx, [tg_cr]                 ; tg_gidx answered in BX; the row is here
    inc cx
    cmp cx, TG_SIZE
    jl .col
    inc bx
    cmp bx, TG_SIZE
    jl .row
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_draw_flash_pages - one purge-animation page, marked cells only
; Leaving the rest of the content alone is what stops the ghost trails a full
; tg_draw_all wipe left behind every shrink on the direct-to-VRAM path.
; -----------------------------------------------------------------------------
tg_draw_flash_pages:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx
.row:
    xor cx, cx
.col:
    mov [tg_cr], bx
    mov [tg_cc], cx
    call tg_gidx
    jc .next
    mov al, [tg_grid + bx]
    test al, TG_MARK
    jz .next
    call tg_flash_cell
.next:
    mov bx, [tg_cr]
    inc cx
    cmp cx, TG_SIZE
    jl .col
    inc bx
    cmp bx, TG_SIZE
    jl .row
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; paint faction AL at (tg_cr, tg_cc)
tg_cell_paint:
    push ax
    call tg_fac_col
    mov byte [tg_finset], 0
    mov byte [tg_pad], 1            ; a 1px gap between solid cells, where the
    call tg_cell_fill               ; cell is big enough to spare it
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_fac_col - AL = faction in, AL = colour out
; Both pairs straddle SPEC.md 39.4's WHITE and DITHER classes, so the two
; factions stay apart when the adapter has one bit per pixel.
; -----------------------------------------------------------------------------
tg_fac_col:
    cmp byte [tg_cblind], 0
    jne .cb
    cmp al, TG_FA
    je .a
    mov al, TG_COL_B
    ret
.a:
    mov al, TG_COL_A
    ret
.cb:
    cmp al, TG_FA
    je .ca
    mov al, TG_COL_BCB
    ret
.ca:
    mov al, TG_COL_ACB
    ret

; -----------------------------------------------------------------------------
; tg_flash_cell - one page of the purge animation for (tg_cr, tg_cc)
; The full cell goes black, then a white core inset by the phase, so the
; shrink leaves nothing behind. The caller must NOT have wiped the board.
; -----------------------------------------------------------------------------
tg_flash_cell:
    push ax
    push bx
    push cx
    push dx
    mov byte [tg_pad], 0
    mov al, TG_BG
    mov byte [tg_finset], 0
    call tg_cell_fill
    mov ax, TG_FLASH_T              ; inset = phase * (csz/2) / FLASH_T
    sub ax, [tg_hold]
    jns .pos
    xor ax, ax
.pos:
    mov bx, [tg_csz]
    shr bx, 1
    or bx, bx
    jnz .mul
    mov bx, 1
.mul:
    mul bx
    mov bx, TG_FLASH_T
    div bx                          ; AX = inset px
    mov bx, [tg_csz]                ; a core that has vanished is not drawn -
    sub bx, ax                      ; the last page is simply empty
    sub bx, ax
    cmp bx, 1
    jl .out
    mov [tg_finset], al
    mov al, CWHITE
    call tg_cell_fill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_cell_fill - fill colour AL over the cell at (tg_cr, tg_cc), inset by
;                [tg_finset] on every edge
;
; Binding: 8086 `mul reg` writes DX:AX. Never keep a live coordinate in DX
; across a mul - an earlier version stashed y1 there, the next mul zeroed it,
; and every cell painted a full-height strip from y=0 down to y2.
; -----------------------------------------------------------------------------
tg_cell_fill:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_SET_COLOR
    mov bl, [tg_finset]
    xor bh, bh
    mov si, bx                      ; SI = the inset
    mov bx, [tg_csz]
    mov ax, [tg_cc]                 ; x1 = bx0 + col*csz + inset
    mul bx
    add ax, [tg_bx0]
    add ax, si
    push ax
    mov ax, [tg_cr]                 ; y1 = HUD + row*csz + inset
    mul bx
    add ax, TG_HUD_H
    add ax, si
    push ax
    mov ax, [tg_cc]                 ; x2 = bx0 + col*csz + csz - 1 - inset
    mul bx
    add ax, [tg_bx0]
    add ax, [tg_csz]
    dec ax
    sub ax, si
    push ax
    mov ax, [tg_cr]                 ; y2 = HUD + row*csz + csz - 1 - inset
    mul bx
    add ax, TG_HUD_H
    add ax, [tg_csz]
    dec ax
    sub ax, si
    mov dx, ax
    pop cx
    pop bx
    pop ax
    cmp byte [tg_pad], 0            ; the decorative gap, only where a cell
    je .go                          ; is big enough to give a pixel away
    cmp word [tg_csz], 4
    jbe .go
    inc ax
    inc bx
    dec cx
    dec dx
.go:
    call tg_fillc
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tg_snap_active - remember the active piece as the next frame's erase target
tg_snap_active:
    push ax
    mov ax, [tg_pr]
    mov [tg_opr], ax
    mov ax, [tg_pc]
    mov [tg_opc], ax
    mov al, [tg_prot]
    mov [tg_oprot], al
    mov al, [tg_ptype]
    mov [tg_otype], al
    mov byte [tg_hasop], 1
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_erase_prev - black out the previous active piece
; A no-op when there is no snapshot, and never after a lock: tg_lock_piece
; drops [tg_hasop] because those cells belong to the board now. The grid is
; not touched. Off-board cells are skipped rather than clamped - a clamped
; rect would erase a cell on the far side of the matrix.
; -----------------------------------------------------------------------------
tg_erase_prev:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [tg_hasop], 0
    je .out
    mov al, [tg_ptype]              ; borrow the shape fields for the old
    mov ah, [tg_prot]               ; piece, and hand them straight back
    push ax
    mov al, [tg_otype]
    mov [tg_ptype], al
    mov al, [tg_oprot]
    mov [tg_prot], al
    mov bx, [tg_opr]
    mov cx, [tg_opc]
    mov [tg_tr], bx
    mov [tg_tc], cx
    xor si, si
.e:
    call tg_cell_of
    mov ax, [tg_cr]
    cmp ax, 0
    jl .next
    cmp ax, TG_SIZE
    jge .next
    mov ax, [tg_cc]
    cmp ax, 0
    jl .next
    cmp ax, TG_SIZE
    jge .next
    mov byte [tg_pad], 0
    mov byte [tg_finset], 0
    mov al, TG_BG
    call tg_cell_fill
.next:
    inc si
    cmp si, TG_NPCELL
    jb .e
    pop ax
    mov [tg_ptype], al
    mov [tg_prot], ah
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tg_draw_active:
    push ax
    push bx
    push cx
    push si
    mov bx, [tg_pr]
    mov cx, [tg_pc]
    mov [tg_tr], bx
    mov [tg_tc], cx
    xor si, si
.d:
    call tg_cell_of
    mov al, [tg_pfac]
    call tg_cell_paint
    inc si
    cmp si, TG_NPCELL
    jb .d
    pop si
    pop cx
    pop bx
    pop ax
    ret

tg_draw_banner:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [tg_mode], TG_M_PAUSE
    je .p
    cmp byte [tg_mode], TG_M_OVER
    je .o
    jmp .out
.p:
    mov si, tg_s_pause
    jmp .d
.o:
    mov si, tg_s_over
.d:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_FONT_WIDTH
    mov cx, [tg_cwid]
    sub cx, ax
    jns .ok
    xor cx, cx
.ok:
    shr cx, 1
    mov dx, TG_HUD_H
    mov ax, [tg_boardpx]
    shr ax, 1
    add dx, ax
    sub dx, 4
    call tg_str
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_draw_panel - whichever panel [tg_plines] names, last of everything
; in:  gfx lock held, origin tracked; preserves every register
;
; Measured from the strings rather than pinned, because the content box is
; 256px wide on VGA and 248 on CGA and a box sized for one hangs out of the
; other - and because the two texts are different shapes. Black on white is
; the one pairing that survives SPEC.md 39.4 on all three adapters.
; -----------------------------------------------------------------------------
tg_draw_panel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tg_pmeas
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call .rect
    call tg_fillc
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call .rect
    call tg_framec
    mov si, [tg_plines]
    mov di, [tg_abt]
    add di, 7                       ; the first baseline, inside the frame
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    cmp byte [si], TG_PLEFT
    je .left
    call OSAPI_FONT_WIDTH
    mov cx, [tg_abw]
    sub cx, ax
    shr cx, 1
    add cx, [tg_abl]
    jmp .draw
.left:
    inc si                          ; step over the sentinel; it is not a glyph
    mov cx, [tg_abl]
    add cx, 8                       ; the margin tg_pmeas budgeted for
.draw:
    mov dx, di
    call tg_str
    pop si
    add di, [tg_ablh]
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.rect:
    mov ax, [tg_abl]
    mov bx, [tg_abt]
    mov cx, ax
    add cx, [tg_abw]
    dec cx
    mov dx, bx
    add dx, [tg_abh]
    dec dx
    ret

; -----------------------------------------------------------------------------
; tg_pmeas - size and centre the panel from the strings themselves
; out: [tg_abw]/[tg_abh]/[tg_abl]/[tg_abt] and the line pitch [tg_ablh]
; preserves every register
;
; The PITCH is measured too, and that is what lets one mechanism carry a
; 9-line credit panel and a 13-line instruction sheet. CGA's content box is
; 124 rows - 96 for the matrix and 28 for the HUD - so thirteen lines at the
; 10px pitch VGA uses would need 144 and hang out of the window; tg_str would
; then DROP the overflowing lines rather than clip them, and a help panel
; missing its last four lines is worse than a tight one. The ladder walks
; 10 -> 9 -> 8 and stops there, because a glyph is 8 rows tall and a pitch
; under that overlaps the text with itself.
; -----------------------------------------------------------------------------
tg_pmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, [tg_plines]
.next:
    mov bx, [si]
    or bx, bx
    jz .done
    inc di
    add si, 2
    push si
    mov si, bx
    cmp byte [si], TG_PLEFT         ; measure the TEXT, not the sentinel
    jne .meas
    inc si
.meas:
    call OSAPI_FONT_WIDTH
    pop si
    cmp ax, cx
    jbe .next
    mov cx, ax
    jmp .next
.done:
    add cx, 16                      ; 8px of margin either side
    cmp cx, [tg_cwid]
    jbe .wok
    mov cx, [tg_cwid]
.wok:
    mov [tg_abw], cx
    mov bx, TG_ABLH                 ; the pitch ladder: 10, then 9, then 8
.pitch:
    mov ax, di
    mul bx
    add ax, 14
    cmp ax, [tg_chgt]
    jbe .pok
    cmp bx, TG_ABLH_MIN
    jbe .pok                        ; already at the floor - clamp instead
    dec bx
    jmp .pitch
.pok:
    mov [tg_ablh], bx
    cmp ax, [tg_chgt]
    jbe .hok
    mov ax, [tg_chgt]
.hok:
    mov [tg_abh], ax
    mov ax, [tg_cwid]
    sub ax, [tg_abw]
    shr ax, 1
    mov [tg_abl], ax
    mov ax, [tg_chgt]
    sub ax, [tg_abh]
    shr ax, 1
    mov [tg_abt], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================
tg_tpl:
    dw 0, 0, 0, 0
    dw tg_ttl, tg_paint, tg_onkey, tg_onclick

    OS88_MENUSET tg_menus, tg_m_name, tg_oncmd
        OS88_MENU tg_m_game, tg_mi_game, 4
        OS88_MENU tg_m_help, tg_mi_help, 1
    OS88_MENUSET_END tg_menus

tg_m_name:   db 'TameGram', 0
tg_m_game:   db 'Protocol', 0
tg_mi_game:  dw tg_i_new, tg_i_pause, tg_i_cb, tg_i_lock
tg_i_new:    db 'New Protocol', 0
tg_i_pause:  db 'Halt', 0
tg_i_cb:     db 'Colour-Blind', 0
tg_i_lock:   db 'Lock Vector', 0
tg_m_help:   db 'Help', 0
tg_mi_help:  dw tg_i_howto
tg_i_howto:  db 'How to Play', 0

tg_ttl:      db 'TameGram', 0
tg_s_title:  db 'CONTAINMENT MATRIX', 0
tg_s_score:  db 'THREAT', 0
tg_s_purges: db 'PURGES', 0
tg_s_vec:    db 'VECTOR', 0
tg_s_fac:    db 'FACTION', 0
tg_s_fa:     db 'AMBER', 0
tg_s_fb:     db 'GREEN', 0
tg_s_fa_cb:  db 'BLUE', 0
tg_s_fb_cb:  db 'ORANGE', 0
tg_s_lock:   db 'LOCK', 0
tg_s_pause:  db '// HALT', 0
tg_s_over:   db 'OVERFLOW - N', 0

tg_vecname:  dw tg_vn, tg_ve, tg_vs, tg_vw
tg_vn:       db 'NORTH', 0
tg_ve:       db 'EAST', 0
tg_vs:       db 'SOUTH', 0
tg_vw:       db 'WEST', 0

; The credit panel. A blank line is a line with no glyphs, not a null entry -
; the 0 at the end of the pointer list is what terminates it.
tg_ablines:
    dw tg_ab1, tg_ab2, tg_ab3, tg_ab4, tg_ab5, tg_ab6, tg_ab7, tg_ab8, tg_ab9, 0
tg_ab1:      db 'TameGram for os8088', 0
tg_ab2:      db '32x32 Containment Matrix', 0
tg_ab3:      db 0
tg_ab4:      db 'Game design and code by', 0
tg_ab5:      db 'Jason Page', 0
tg_ab6:      db 'store.amfile.org', 0
tg_ab7:      db 0
tg_ab8:      db 'After the Alignment,', 0
tg_ab9:      db 'order is the Cull Schedule.', 0

; 'How to Play' (Help menu). Thirteen lines against CGA's 124-row content box,
; which is what tg_pmeas's pitch ladder is for; the widest is 25 glyphs = 200px,
; inside TG_HUD_W with room for the panel's own margins. The title and the
; dismiss hint are centred; everything between them is TG_PLEFT, so the key
; column lines up down the left edge instead of drifting with each line's
; length.
tg_hlines:
    dw tg_hp1, tg_hp2, tg_hp3, tg_hp4, tg_hp5, tg_hp6, tg_hp7
    dw tg_hp8, tg_hp9, tg_hp10, tg_hp11, tg_hp12, tg_hp13, 0
tg_hp1:      db 'HOW TO PLAY', 0
tg_hp2:      db TG_PLEFT, 'Eight in a row or column', 0
tg_hp3:      db TG_PLEFT, 'of one faction purges it.', 0
tg_hp4:      db TG_PLEFT, 'Units fall along VECTOR,', 0
tg_hp5:      db TG_PLEFT, 'which re-rolls each unit.', 0
tg_hp6:      db 0
tg_hp7:      db TG_PLEFT, 'Arrows  strafe/soft drop', 0
tg_hp8:      db TG_PLEFT, 'A / D   strafe sideways', 0
tg_hp9:      db TG_PLEFT, 'Space   rotate', 0
tg_hp10:     db TG_PLEFT, 'Enter   hard drop', 0
tg_hp11:     db TG_PLEFT, 'P halt      N new game', 0
tg_hp12:     db TG_PLEFT, 'C colour-blind   F lock', 0
tg_hp13:     db 'Any key or click closes.', 0

; seven tetrominoes x four cells x (row, col), signed bytes
tg_shapes:
    db 0,-1, 0,0,  0,1,  0,2        ; I
    db 0,0,  0,1,  1,0,  1,1        ; O
    db 0,-1, 0,0,  0,1,  1,0        ; T
    db 0,0,  0,1,  1,-1, 1,0        ; S
    db 0,-1, 0,0,  1,0,  1,1        ; Z
    db 0,-1, 1,-1, 1,0,  1,1        ; J
    db 0,1,  1,-1, 1,0,  1,1        ; L

tg_numbuf:   db 0,0,0,0,0,0

; =============================================================================
; .bss - offsets from os88_image_end, which the loader zeroes for us
; =============================================================================
%assign TG_BSS 0
%macro TWORD 1
%1 equ os88_image_end + TG_BSS
%assign TG_BSS TG_BSS + 2
%endmacro
%macro TBYTE 1
%1 equ os88_image_end + TG_BSS
%assign TG_BSS TG_BSS + 1
%endmacro
%macro TBUF 2
%1 equ os88_image_end + TG_BSS
%assign TG_BSS TG_BSS + (%2)
%endmacro

    TWORD tg_win
    TWORD tg_scrw
    TWORD tg_dock
    TWORD tg_ox
    TWORD tg_oy
    TWORD tg_cwid
    TWORD tg_chgt
    TWORD tg_csz
    TWORD tg_boardpx
    TWORD tg_bx0                    ; matrix left edge, content coords
    TWORD tg_score
    TWORD tg_purges
    TWORD tg_fallspd
    TWORD tg_fallcd
    TWORD tg_hold
    TWORD tg_pr
    TWORD tg_pc
    TWORD tg_tr
    TWORD tg_tc
    TWORD tg_cr
    TWORD tg_cc
    TWORD tg_drow
    TWORD tg_dcol
    TWORD tg_plines                 ; the panel text now on screen
    TWORD tg_ablh                   ; ...and the pitch tg_pmeas chose for it
    TWORD tg_abw
    TWORD tg_abh
    TWORD tg_abl
    TWORD tg_abt
    TWORD tg_opr                    ; previous active origin, row
    TWORD tg_opc                    ; ...and column
    TBYTE tg_mode
    TBYTE tg_wasmode
    TBYTE tg_grav
    TBYTE tg_glock
    TBYTE tg_ptype
    TBYTE tg_prot
    TBYTE tg_pfac
    TBYTE tg_cmd
    TBYTE tg_lat
    TBYTE tg_hired
    TBYTE tg_dirty
    TBYTE tg_full                   ; 1 = this frame owes a whole repaint
    TBYTE tg_locked
    TBYTE tg_cblind
    TBYTE tg_panel
    TBYTE tg_hudbg                  ; HUD band colour, picked at boot
    TBYTE tg_frcol                  ; matrix border colour, likewise
    TBYTE tg_finset                 ; flash-page inset, px
    TBYTE tg_pad                    ; 1 = 1px gap around a solid cell
    TBYTE tg_hasop                  ; 1 = tg_o* holds a previous active piece
    TBYTE tg_oprot                  ; ...its rotation
    TBYTE tg_otype                  ; ...and its shape
    TBUF  tg_grid, TG_CELLS

    OS88_BSS TG_BSS
    OS88_IMAGE_END
