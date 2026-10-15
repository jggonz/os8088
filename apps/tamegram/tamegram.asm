; =============================================================================
; os8088 - apps/tamegram/tamegram.asm
;
; TameGram, the thirteenth software package (SPEC.md 49): a four-direction,
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
;    tidiness. The piece geometry is resolved into one shared array -
;    tg_resolve writes the four cells of a trial origin into [tg_cells] - and
;    the SAME array carries the drawing path. With a lock-free update the UI
;    task could draw (W_PAINT, the About panel, a menu command) while the
;    worker was between tg_resolve and the test that reads its answer, and
;    tg_fits would then judge one origin by another's cells: "fits" for a
;    position it never tested, tg_step_grav commits to it, and tg_lock_piece
;    stamps cells resolved for somewhere else into tg_grid. Every UI callback
;    already runs under the gfx lock (SPEC.md 11/12.2), so taking it around
;    tg_update is what makes a tick atomic against them. It is affordable
;    because the expensive half of a frame - tg_draw_all - was always inside
;    the lock anyway, and the expensive half of an UPDATE (tg_scan_clears
;    over 1,024 cells, twice) runs once per piece lock rather than once per
;    tick.
;
;  - **Every grid index is bounds-checked at ONE place.** tg_gidx answers
;    CF=1 for a cell that is off the board instead of handing back an index,
;    so the belt-and-braces test lives in the routine every per-cell reader
;    and the one per-cell writer already go through. tg_grid is the last
;    thing in the bss and the package's region is a heap claim (SPEC.md 50.3),
;    so a stray `mov [tg_grid+bx], al` with bx = 1,057 writes into whatever
;    claim follows ours - not into a fault.
;
;  - **Nothing may draw outside the content box.** The gfx_* primitives take
;    absolute screen coordinates and clip to the SCREEN, not to a window
;    (SPEC.md 11.3), and W_PAINT runs with no clip region armed - so a rect
;    that is wrong by a row paints over the window next to us. tg_fillc,
;    tg_framec and tg_str/tg_run are the only ways this module reaches the
;    screen and all of them clamp to [tg_cwid]/[tg_chgt] first. That is what
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
;
; REGISTERS (apps size pass 1, set 6). A window callback may clobber AX, BX,
; CX, DX, SI, DI and ES (SPEC.md 12.2, 13) - except that wm_draw_win reads SI
; back after W_PAINT, so tg_paint alone banks it - and so the routines below
; preserve NOTHING unless their header says so: the callback, or the worker
; loop, is the only caller that ever held a live value across them. tg_track
; points ES at our own segment at the top of every callback and frame, so the
; string instructions below (stosw in tg_resolve, rep stosw in tg_newgame,
; rep movsw in tg_draw_active) write our bss and not the kernel's, and the
; entry proc and the worker do the same before anything else: BOTH are handed
; ES = KERNEL_SEG (SPEC.md 20.2, 20.6), so a stosw before that store lands in
; the kernel's own segment at our bss offset - the first build of this pass
; did exactly that from tg_newgame and the window never painted. DF is 0 on
; entry to all of them (SPEC.md 1).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'TAMEGRAM', tg_entry, 1, OS88_STACK_256
                                ; THE WORKER'S STACK, declared rather than
                                ; defaulted (SPEC.md 8.7). Static chain 20
                                ; (tools/stkdepth.py; 82 before the size pass),
                                ; so 20 + the 64-byte interrupt floor = 84
                                ; against 256. MEASURED on
                                ; MartyPC (tests/tgplay.py reads the slice's
                                ; 0xCC fill): 142-146 of 256 before the size
                                ; pass and 78-98 after, the deepest frame the
                                ; far call into gfx_fill under the board walk
                                ; on both - 256 is kept
                                ; for docs/FIELD-NOTES.md 42's reason, a
                                ; mounted SOUND.DRV adding to the floor

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

; A cell is four bits - two cells a byte, tg_get/tg_put: the faction in the
; low two and the purge mark above them, one bit spare
TG_EMPTY    equ 0
TG_FA       equ 1
TG_FB       equ 2
TG_FAC      equ 0x03            ; the faction bits: an occupant
TG_MARK     equ 0x08            ; marked to purge (the faction stays beside it)
TG_GRIDB    equ TG_CELLS / 2    ; the grid's bytes (tests/tgplay.py reads it)

TG_FALL0    equ 12
TG_FALLMIN  equ 3
; Purge animation frames written to VRAM (the shrinking white core); ~0.5s.
TG_FLASH_T  equ 10

TG_HUD_H    equ 28              ; three 8px text rows and their margins
TG_HUD_W    equ 248             ; the widest HUD row is 240px (LOCK's 32 at
                                ; TG_HC_LOCK); this is the content width the
                                ; layout will not go under, because tg_str drops
                                ; a string that would leave the box and a
                                ; silently missing THREAT counter is worse than
                                ; a wide window

; The HUD's text columns. EVERY ONE IS A MULTIPLE OF 8 (SPEC.md 11.94.3): this
; window's content origin is byte-aligned (11.94.1), so a pen's offset mod 8 IS
; its phase on the glass, and font_char writes one framebuffer byte per cell row
; instead of two only at phase 0 - on the two 1bpp adapters and, since 11.94's
; gate came off, on VGA's four planes as well.
;
; They were 4, 60, 108, 164, 116, 180 and 210 - SIX OF THE SEVEN at 4 mod 8,
; which made this the most uniformly off-grid face in the tree and by far the
; cheapest to correct: +4 on six numbers. What that is WORTH is small and is
; measured rather than implied - 109 glyph cells in 17.7s of play, all of them
; off-grid, which at PERFORMANCE.md's ~1ms a cell is 0.6% of the machine and the
; alignment saves 3.4% of that (Set 52). It is taken because it is free, because
; it puts this face on the same 8px margin as the rest of the tree, and because
; leaving the last uniformly-fixable entry on the survey invites the next reader
; to re-derive the whole census. It is NOT taken for the time.
;
; tg_hud stores each column DIVIDED BY 8, which is the same rule written so it
; cannot be broken: a column that is not a multiple of 8 does not assemble.
TG_HC_L     equ 8               ; row 1's title, row 2's THREAT, row 3's VECTOR
TG_HC_LV    equ 64              ; the value beside whichever left label has one
TG_HC_R     equ 112             ; row 2's PURGES
TG_HC_RV    equ 168             ; ...and its count
TG_HC_F     equ 120             ; row 3's FACTION - one cell right of PURGES
TG_HC_FV    equ 184             ; ...and its name; 'ORANGE' ends at 232
TG_HC_LOCK  equ 208             ; row 1's LOCK flag. This one moved LEFT by 2
                                ; rather than right by 6, which would have ended
                                ; the row at exactly TG_HUD_W and left the
                                ; minimum-width case with no margin at all
%if (TG_HC_L | TG_HC_LV | TG_HC_R | TG_HC_RV | TG_HC_F | TG_HC_FV | TG_HC_LOCK) & 7
  %error "a HUD column is not a multiple of 8 - see SPEC.md 11.94.3"
%endif
TG_CSZ_MAX  equ 8
; The panels are the shared About card (apps/os88ui.inc, SPEC.md 20.5.1.1)
; with its three opt-ins: a line that BEGINS with OS88UI_ABLT (1) is drawn
; LEFT-ALIGNED at the card's text margin and the byte is not drawn (a
; two-column key table centred is a ragged diamond); the pitch steps down
; from 12 to 8 when the text does not fit (CGA's 124 rows); and a panel's
; text is its lines END TO END, each with its NUL, closed by OS88UI_ABEND
; where the next line would begin - no pointer table, so a blank line is a
; lone NUL (SPEC.md 49.6, 49.8).

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
; in:  DS = CS = our segment, ES = KERNEL_SEG, IF = 1, gfx lock NOT held
; out: BX = window ptr, CF = wm_create's
;
; An ordinary NEAR proc ending in a near `ret`: the kernel reaches every
; callback through the three-byte dispatcher in our own header (SPEC.md
; 20.1/20.2), so nothing in this file ever writes `retf`. Nothing is drawn and
; no task is spawned here - the loader has not published our instance yet, so
; OSAPI_TASK_SPAWN would simply refuse. The first W_PAINT hires the worker.
;
; Only the TEMPLATE is laid out here. Every size the drawing reads is derived
; again by tg_track from the window the kernel actually made, before anything
; is drawn - so this proc keeps none of them.
; -----------------------------------------------------------------------------
tg_entry:
    push ds                         ; ES = us: tg_newgame's string stores
    pop es
    call OSAPI_VIDEO                ; AX=w BX=h CX=dock top row DH=bpp
    push ax                         ; the screen width, for centring
    mov si, cx                      ; SI = the dock's first row
    mov bx, (TG_FRAME << 8) | TG_HUD_BG     ; the two colours SPEC.md 39.4
    cmp dh, 1                               ; decides, stored as one word
    ja .pal
    mov bx, (TG_FRAME1 << 8) | TG_HUD_BG1
.pal:
    mov [tg_hudbg], bx

    ; The content height wm_fit will actually allow: the desktop band is
    ; MBAR_H .. dock-1 and wm_fit shaves one more row, because the drop
    ; shadow is on y+h and a frame that merely REACHES the strip is already
    ; on it. Taking the pixel here keeps our own layout in step with the
    ; window the kernel makes, exactly as apps/arkanoid does. The matrix gets
    ; what is left after the HUD.
    xchg ax, cx
    sub ax, MBAR_H + TITLE_H + 2 + TG_HUD_H
    jns .avail
    xor ax, ax
.avail:
    mov cl, 5
    shr ax, cl                      ; AX = the biggest cell that fits
    cmp ax, TG_CSZ_MAX
    jbe .cap
    mov ax, TG_CSZ_MAX
.cap:
    or ax, ax
    jnz .csz
    inc ax
.csz:
    shl ax, cl                      ; AX = the matrix, 32 cells of it
    mov dx, TG_HUD_W                ; content width = max(matrix, HUD)
    cmp ax, dx
    jbe .wid
    mov dx, ax
.wid:
    inc dx                          ; frame = content + the 1px sides
    inc dx
    mov [tg_tpl + WT_W], dx
    add ax, TG_HUD_H + TITLE_H + 1  ; content height = HUD + matrix
    mov [tg_tpl + WT_H], ax

    pop bx                          ; centred horizontally, a third of the way
    sub bx, dx                      ; down the desktop band
    jns .xok
    xor bx, bx
.xok:
    shr bx, 1
    mov [tg_tpl + WT_X], bx
    xchg ax, si                     ; AX = the dock row, SI = the frame height
    sub ax, MBAR_H
    sub ax, si
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
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    ; The _NOKEEP form, 10 bytes against the keeping form's 16: nothing
    ; here needs AX, DX or the flags kept (the CF this proc owes the
    ; loader is the `clc` below), and the proc is a bare `ret` because the
    ; image holds no segment word of its own.
    OS88_REGION_MOVABLE_NOKEEP tg_ret
    mov si, tg_menus
    call OSAPI_MENU_SET
    mov si, tg_about
    call OSAPI_ABOUT_SET            ; 'About TameGram' under our name in the
    clc                             ; bar
.done:
    ret

; -----------------------------------------------------------------------------
; tg_track - adopt the live content origin and size, and point ES at us
; in:  SI = window ptr
; out: [tg_ox]/[tg_oy], [tg_cwid]/[tg_chgt] and the metrics derived from them;
;      CF = OSAPI_WM_GEOM's (1 = not visible; the sizes are filled in either
;      way). ES = DS. Keeps AX, SI, DI; clobbers BX, CX, DX
;
; Called at the top of every callback and every rendered frame: the window
; moves, and wm_fit may have clamped what the entry proc asked for. Reading
; the record through ES would work (ES = KERNEL_SEG in a callback) but those
; are FRAME dimensions; OSAPI_WM_GEOM is the same subtraction done once.
;
; tg_metrics is its second half: cell size and matrix origin from the LIVE
; content box. The whole layout hangs off it, and it is re-derived rather than
; remembered so that a window the kernel sized differently from the request
; still draws entirely inside itself. A zero cell is refused: it makes every
; cell rect inside-out, and an inside-out rect is not a small mistake but a
; stripe from one edge of the screen to the other.
; -----------------------------------------------------------------------------
tg_track:
    push ax
    push ds
    pop es
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [tg_ox], ax
    mov [tg_oy], dx
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h
    pushf
    mov [tg_cwid], cx
    mov [tg_chgt], dx
.metrics:                           ; (SPEC.md 49.3's tg_metrics)
    xchg ax, cx                     ; AX = the width
    sub dx, TG_HUD_H
    jns .hok
    xor dx, dx
.hok:
    mov cl, 5
    shr ax, cl                      ; the cell the width allows...
    shr dx, cl                      ; ...and the cell the height allows
    cmp ax, dx                      ; ...and the smaller of the two wins
    jbe .have
    xchg ax, dx
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
    shl ax, cl
    mov [tg_boardpx], ax
    mov dx, [tg_cwid]               ; centre the matrix under the HUD
    sub dx, ax
    jns .lok
    xor dx, dx
.lok:
    shr dx, 1
    mov [tg_bx0], dx
    popf
    pop ax
    ret

; =============================================================================
; UI callbacks - all of them on the UI task, all of them under the gfx lock
; =============================================================================

; tg_paint - W_PAINT. The one callback that keeps SI: wm_draw_win takes the
; window back out of it after the paint returns. Hires the worker on the first
; paint (idempotently - see below).
tg_paint:
    push si
    call tg_track
    call tg_draw_all
    pop si
    ; THE WORKER, hired once. OSAPI_TASK_SPAWN needs the gfx lock, which is
    ; why it is here and not in the entry proc. A refusal is a normal outcome
    ; - the task table can be full - and it is transient, so nothing is
    ; latched on failure and the next paint tries again. What must NOT happen
    ; is a second spawn.
    cmp byte [tg_hired], 0
    jne tg_ret
    mov ax, tg_worker
    mov bx, [tg_win]
    call OSAPI_TASK_SPAWN           ; ...which answers the slot in AL
    jc tg_ret
    inc byte [tg_hired]
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2): the
    ; kernel wrote our segment into this worker's frame before its
    ; first instruction, so mem_frameless pins a region with an
    ; undeclared worker however that region is declared. What a restart
    ; costs is one pass of the loop - the park is inside
    ; OSAPI_TASK_ALIVE and nowhere else (this package is not
    ; OSAPI_MEM_PARKSAFE), which is the TOP of the loop, and every byte
    ; that outlives a pass is a static and moves with us.
    OS88_WORKER_RESTARTABLE_NOKEEP tg_worker   ; 8 bytes: a W_PAINT owes
    ret                             ; SI back and no flag (SPEC.md 11.3)

; -----------------------------------------------------------------------------
; tg_onkey - W_ONKEY
; in:  AL = ascii, AH = scan, SI = window ptr; caller holds the gfx lock
;
; Steering only posts a command byte the worker spends on its next tick; the
; keys that change what is on the screen without moving a piece (P, N, C, F)
; are tg_act's, the same four the Protocol menu reaches, and repaint here,
; because the worker may have nothing else to redraw for.
;
; The arrow and grey-Enter SCAN codes are looked up first. The numeric keypad
; and some hosts put a non-zero AL alongside the arrow scan, so an `or al,al`
; gate ahead of them swallows exactly the presses that were meant to strafe -
; which reads as "the arrows only work sometimes" rather than as a lost key.
; -----------------------------------------------------------------------------
tg_onkey:
    call tg_track                   ; (keeps AX)
    call tg_panel_off               ; the key that takes a panel down is spent
    jc .rep                         ; doing it, and owes the repaint
    xchg ax, dx                     ; DH = scan, DL = ascii...
    xchg dl, dh                     ; ...and the other way round
    mov si, tg_kscan
    call tg_look
    jnc .cmd
    mov dl, dh                      ; the ascii, for the second table
    or dl, dl
    jz tg_ret
    cmp dl, 'a'                     ; letters fold to upper case; nothing in
    jb .up                          ; the table is above 'Z', so whatever else
    and dl, 0xDF                    ; this folds cannot match
.up:
    mov si, tg_kasc
    call tg_look
    jc tg_ret
    cmp al, TG_K_ACT
    jb .cmd
    sub al, TG_K_ACT
    call tg_act
.rep:
    jmp tg_draw_all
.cmd:
    mov [tg_cmd], al
    ret

; tg_look - DL = key, SI = a table of (key, value) byte pairs ending in a 0
; key. out: CF = 0 and AL = the value, or CF = 1 not there. Clobbers AX, SI.
tg_look:
    lodsw
    or al, al
    jz .miss
    cmp al, dl
    jne tg_look
    mov al, ah                      ; CF = 0: the compare was equal
    ret
.miss:
    stc
    ret

; -----------------------------------------------------------------------------
; tg_oncmd - the AM_ONCMD handler for both menus (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0 = Protocol, 1 = Help), SI = window
;      ptr; gfx lock held
;
; Same rules as W_ONCLICK, and the last of them is the one that bites: the
; kernel does not repaint after a command handler returns, so this must - which
; is why every path here, the unrecognised one included, ends in tg_draw_all.
;
; AH is decoded, not ignored. It was ignored while there was one menu, and a
; second one makes that a silent mis-dispatch: 'How to Play' is item 0 of menu
; 1, and item 0 of menu 0 is 'New Protocol' - so a help request would have
; thrown the player's game away.
; -----------------------------------------------------------------------------
tg_oncmd:
    call tg_track                   ; (keeps AX)
    call tg_panel_off               ; a panel is replaced, not stacked; the
    or ah, ah                       ; repaint below is the one either way
    jnz .help
    call tg_act
    jmp tg_draw_all
.help:
    or al, al                       ; menu 1 has exactly one item
    jnz .rep
    mov ah, 2                       ; panel 2, the instruction sheet
    mov bx, tg_hlines
    jmp tg_panel_on
.rep:
    jmp tg_draw_all

; tg_act - one of the four state toggles: AL = 0 New, 1 Halt, 2 Colour-Blind,
; 3 Lock Vector - the Protocol menu's item order, which N, P, C and F reach too
tg_act:
    dec al
    js tg_newgame
    jz tg_toggle_halt
    dec al
    jnz .lock
    xor byte [tg_cblind], 1
    ret
.lock:
    xor byte [tg_glock], 1
    ret

; tg_toggle_halt - P / 'Halt'. A finished game cannot be paused, and it owes a
; whole repaint either way.
tg_toggle_halt:
    mov ax, [tg_mode]               ; AL = the mode, AH = the one it halted
    cmp al, TG_M_OVER
    je tg_owefull
    cmp al, TG_M_PAUSE
    je .unp
    mov ah, al
    mov al, TG_M_PAUSE
    jmp short .set
.unp:
    mov al, ah
.set:
    mov [tg_mode], ax
    jmp tg_owefull

; -----------------------------------------------------------------------------
; The two panels (SPEC.md 49.6)
;
; There is one panel mechanism and two texts: the credits, reached through
; OSAPI_ABOUT_SET, and 'How to Play', reached through the Help menu. Both are
; drawn INSIDE our own content by os88ui_about_d, last of everything, and
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
    call tg_track
    mov ah, 1                       ; panel 1, the credits
    mov bx, tg_ablines
tg_panel_on:                        ; AH = which panel, BX = its lines
    mov [tg_panel], ah
    mov [tg_plines], bx
    jmp tg_draw_all                 ; ...which draws the panel last, because
                                    ; [tg_panel] is set

; -----------------------------------------------------------------------------
; tg_panel_off - take the panel down if it is up
; in:  origin tracked, gfx lock held
; out: CF = 1 the click/key was spent doing it; keeps every register
;
; It does NOT repaint. Every caller either repaints anyway (tg_oncmd always
; ends in one, and would otherwise draw the content twice for one click) or
; branches to its own repaint on the CF.
; -----------------------------------------------------------------------------
tg_panel_off:
    cmp byte [tg_panel], 0
    je .none                        ; CF = 0: the compare was equal
    mov byte [tg_panel], 0
    mov word [tg_dirty], 0x0101     ; [tg_dirty] and [tg_full], one store
    stc
.none:
    ret

; tg_onclick - W_ONCLICK: a click only ever takes a panel down. The repaint is
; settled here rather than leaving a frame of panel on screen until the
; worker's next tick.
tg_onclick:
    call tg_track
    call tg_panel_off
    jnc .out
    push si                         ; W_ONCLICK gives SI back (os88api.inc):
    call tg_draw_all                ; a PKG_FMT-8 kernel before this one arms
    pop si                          ; the release on whatever SI comes back
.out:
    ret

; =============================================================================
; The worker: the game itself (SPEC.md 20.6)
; =============================================================================

; -----------------------------------------------------------------------------
; tg_worker - THE background task
; in:  DX = our instance index, DS = CS = our segment, ES = KERNEL_SEG,
;      IF = 1, gfx lock free. NEVER returns and never exits on its own: the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; One frame a tick. Both halves of the frame run inside ONE lock hold - see
; the file header: the update and the drawing share their scratch with the UI
; task's drawing, and the lock is what every UI callback already holds.
; OSAPI_TASK_ALIVE is called with the lock FREE (rule 4: gfx_lock is not
; reentrant, and taking it twice never draws again). tests/tgplay.py
; breakpoints the three calls inside the hold, so each is written once.
; -----------------------------------------------------------------------------
tg_worker:
    push ds                         ; ES = us, for tg_resolve's stosw; every
    pop es                          ; OSAPI call below keeps it
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
; in:  gfx lock held
; -----------------------------------------------------------------------------
tg_update:
    cmp byte [tg_panel], 0
    jne tg_ret                      ; a panel is up: the UI task owns us
    call tg_do_cmds
    mov al, [tg_mode]
    cmp al, TG_M_FLASH
    je .flash
    ja .settle
    or al, al                       ; PLAY falls; PAUSE and OVER do nothing
    jnz tg_ret
    dec word [tg_fallcd]
    jg tg_ret
    mov ax, [tg_fallspd]
    mov [tg_fallcd], ax
    jmp tg_step_grav
.flash:
    ; One VRAM "page" per tick: only the marked cells are rewritten (see
    ; tg_draw_flash_pages). A full-board wipe every tick is what ghosted on
    ; the direct-VRAM path - an intermediate black frame behind every shrink.
    mov byte [tg_dirty], 1
    dec word [tg_hold]
    jg tg_ret
    call tg_purge_marked
    mov byte [tg_mode], TG_M_SETTLE
    jmp tg_owefull
.settle:
    call tg_settle_once
    jc tg_owefull                   ; something moved: one more step
    ; ...and falls into what a lock does once the piece is in the grid

; -----------------------------------------------------------------------------
; tg_aftermath - after a lock, or a settle step that moved nothing: purge what
; completed, else the next piece, else the game is over
; -----------------------------------------------------------------------------
tg_aftermath:
    call tg_scan_clears
    jz .spawn
    mov byte [tg_mode], TG_M_FLASH
    mov word [tg_hold], TG_FLASH_T
    jmp tg_owefull
.spawn:
    mov byte [tg_mode], TG_M_PLAY
    call tg_spawn
    jnc tg_owefull
    mov byte [tg_mode], TG_M_OVER
    ; ...and the whole content is owed either way

; tg_owefull - raise [tg_dirty] and [tg_full]: a frame is owed, a whole one
tg_owefull:
    mov word [tg_dirty], 0x0101
tg_ret:                             ; a bare `ret`, and the relocation proc
    ret                             ; OSAPI_MEM_MOVABLE was given

; -----------------------------------------------------------------------------
; tg_do_cmds - spend the one command byte W_ONKEY posted
; No hold/arm gate: every key event the UI task delivers is applied exactly
; once, on the next worker tick, and a key pressed while halted is consumed
; then and cannot fire on resume.
;
; Commands 1, 2 and 5-8 are all ONE move, a (row, col) step out of tg_cvec,
; and tg_nudge sorts it: along the vector a soft drop, against it nothing,
; across it a strafe. A/D are the step (-1,-1) / (1,1), which is across EVERY
; vector - tg_nudge keeps only the cross component, so on a vertical vector
; it moves the column and on a horizontal one the row, which is exactly the
; lateral strafe they always were.
; -----------------------------------------------------------------------------
tg_do_cmds:
    xor al, al
    xchg al, [tg_cmd]               ; consumed either way
    cmp byte [tg_mode], TG_M_PLAY
    jne tg_ret
    or al, al
    jz tg_ret
    cmp al, 3
    je tg_rotate
    cmp al, 4
    je tg_harddrop
    cbw
    xchg ax, bx
    shl bx, 1
    mov dx, [tg_cvec - 2 + bx]      ; DL = the column step, DH = the row's
    ; ...into tg_nudge

; -----------------------------------------------------------------------------
; tg_nudge - one screen-absolute step DL = d(col), DH = d(row)
; Along the vector -> soft drop (one tg_step_grav); against it -> ignored;
; across it -> a one-cell strafe. Never hard-drops; Enter does that.
; -----------------------------------------------------------------------------
tg_nudge:
    call tg_gvec                    ; BX = the fall step, the same shape
    cmp dx, bx
    je .soft
    neg bl                          ; against the vector: ignored
    neg bh
    cmp dx, bx
    je tg_ret
    test byte [tg_grav], 1          ; keep only the component across the
    jnz .row                        ; fall: a vertical fall moves the column,
    xor dh, dh                      ; a horizontal one the row
    jmp tg_tryshift
.row:
    xor dl, dl
    jmp tg_tryshift
.soft:
    call tg_step_grav
    cmp byte [tg_mode], TG_M_PLAY
    jne tg_ret
    mov ax, [tg_fallspd]
    mov [tg_fallcd], ax
    ret

; tg_rotate - a quarter turn, refused (and put back) if it does not fit
tg_rotate:
    mov al, [tg_prot]
    push ax
    inc ax
    and al, 3
    mov [tg_prot], al
    xor dx, dx                      ; the same origin, the new shape
    call tg_tryshift
    pop ax
    jnc tg_ret
    mov [tg_prot], al               ; refused: put the rotation back
    ret

; tg_harddrop - fall until the piece locks
tg_harddrop:
    call tg_step_grav
    jnc tg_harddrop
    ret

; -----------------------------------------------------------------------------
; tg_step_grav - one step along the vector, or lock where the piece is
; out: CF = 1 it locked (and tg_lock_piece has run)
; -----------------------------------------------------------------------------
tg_step_grav:
    call tg_gvec
    xchg dx, bx                     ; DX = the fall step
    call tg_tryshift
    jnc .out
    call tg_lock_piece
    stc
.out:
    ret

; tg_gvec - BL = d(col), BH = d(row) of the current vector. Keeps all but BX.
tg_gvec:
    mov bl, [tg_grav]
    xor bh, bh
    shl bx, 1
    mov bx, [tg_gtab + bx]
    ret

; -----------------------------------------------------------------------------
; tg_tryshift - move the piece by DL = d(col), DH = d(row) if it fits there
; out: CF = 1 refused, nothing changed; else the origin moved and a frame is
;      owed. The ONE place the origin is written outside tg_spawn.
; -----------------------------------------------------------------------------
tg_tryshift:
    mov bx, [tg_pc]                 ; BL = column, BH = row
    add bl, dl
    add bh, dh
    call tg_fits
    jc .out
    mov [tg_pc], bx
    mov byte [tg_dirty], 1
.out:
    ret

; =============================================================================
; Piece geometry
; =============================================================================

; -----------------------------------------------------------------------------
; tg_resolve - the four cells of the current shape at origin BL = col, BH = row
; out: [tg_cells] = four (col, row) signed byte pairs, which MAY be off the
;      board. Keeps BX. Needs ES = DS (stosw)
;
; [tg_ptype] holds the shape's offset in tg_shapes (type * 8) and [tg_prot]
; follows it, so one word load names both. A quarter turn maps (row, col) to
; (col, -row); with the pair held as AL = col, AH = row that is xchg + neg.
; -----------------------------------------------------------------------------
tg_resolve:
    mov dx, [tg_ptype]              ; DL = the shape's offset, DH = rotation
    mov al, dl
    cbw
    add ax, tg_shapes
    xchg ax, si
    mov di, tg_cells
    mov cx, TG_NPCELL
.cell:
    lodsw                           ; AL = d(col), AH = d(row)
    mov dl, dh
    jmp short .turns
.turn:
    xchg al, ah
    neg al
.turns:
    dec dl
    jns .turn
    add al, bl
    add ah, bh
    stosw
    loop .cell
    ret

; -----------------------------------------------------------------------------
; tg_gidx - flat grid index for the cell AL = col, AH = row
; out: CF = 0 and BX = the index; CF = 1 the cell is off the board and BX is
;      meaningless. Keeps every other register.
;
; THE bounds test, and it lives here because every per-cell reader and the
; per-cell writer already come through this routine. tg_grid is the last
; object in the bss and the region is an ordinary heap claim (SPEC.md 50.3),
; so an index built from an off-board row does not fault - it writes into
; whatever claim sits above ours, or into our own state a row below zero.
; A coordinate is a signed byte within a few cells of the board, so one test
; of bits 5-7 of both at once answers "0..31" for each.
; -----------------------------------------------------------------------------
tg_gidx:
    mov bl, al
    or bl, ah
    test bl, 0xE0                   ; CF = 0
    jnz .off
    mov bh, ah                      ; row * 256 ...
    xor bl, bl
    shr bx, 1
    shr bx, 1
    shr bx, 1                       ; ... / 8 = row * 32
    or bl, al                       ; + col; CF = 0
    ret
.off:
    stc
    ret

; -----------------------------------------------------------------------------
; tg_get / tg_put - one cell of the grid, BX = its index 0..1023
; tg_get: out AL = the cell (faction | TG_MARK). tg_put: in AL = the cell
; (0..15). Both keep every other register, AH included.
;
; TWO CELLS A BYTE: cell 2k is the low nibble of tg_grid[k] and cell 2k+1 the
; high one. A cell is a faction (0-2) and the purge mark, four bits with one
; to spare, so a byte each left half of tg_grid empty - 512 bytes of every
; instance, which is a whole KB of the claim (the loader rounds image + bss
; up to KB) once the rest of the size pass had been taken.
; -----------------------------------------------------------------------------
tg_get:
    push bx
    shr bx, 1
    mov al, [tg_grid + bx]
    jnc .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 0x0F
    pop bx
    ret

tg_put:
    push ax
    push bx
    mov ah, 0xF0                    ; the half to keep: the other cell's
    shr bx, 1
    jnc .st
    mov ah, 0x0F
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
.st:
    and ah, [tg_grid + bx]
    or ah, al
    mov [tg_grid + bx], ah
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tg_fits - would the piece sit at trial origin BL = col, BH = row?
; out: CF = 1 blocked (off the board, or over an occupied cell). [tg_cells]
;      holds the trial's cells either way. Keeps BX.
; -----------------------------------------------------------------------------
tg_fits:
    push bx
    call tg_resolve
    mov si, tg_cells
    mov cx, TG_NPCELL
.loop:
    lodsw
    call tg_gidx
    jc .out
    call tg_get
    test al, TG_FAC                 ; the faction bits; CF = 0. (A MARKED cell
    loopz .loop                     ; keeps its faction and is still in the
                                    ; way - and no piece moves while one
                                    ; stands, the marks living only in FLASH)
    jz .out                         ; all four empty
    stc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; tg_lock_piece - stamp the active piece into the grid and take what follows
;
; The only per-cell writer of tg_grid outside tg_settle_once/tg_purge_marked,
; and the `jc .next` on tg_gidx is the whole of its memory safety: the origin
; is one the last tg_fits accepted, so every cell IS on the board - but "is"
; here rests on two tasks agreeing about [tg_cells], and the cost of being
; wrong is a write past the end of the bss rather than a misdrawn cell.
; -----------------------------------------------------------------------------
tg_lock_piece:
    mov bx, [tg_pc]
    call tg_resolve
    mov si, tg_cells
    mov cx, TG_NPCELL
    mov dl, [tg_pfac]
.stamp:
    lodsw
    call tg_gidx
    jc .next
    mov al, dl
    call tg_put
.next:
    loop .stamp
    mov byte [tg_hasop], 0          ; stamped: these cells are the board's now
    inc word [tg_score]             ; and must NOT be erased as a stale piece
    jmp tg_aftermath

; =============================================================================
; Clears / settle
; =============================================================================

; -----------------------------------------------------------------------------
; tg_scan_clears - mark every complete run of eight
; out: ZF = 0 something was marked (and THREAT, PURGES and the fall speed
;      have been charged for it); ZF = 1 nothing purges
;
; Rows, then columns, as ONE walk with the step in DX: 1 along a row, 32 down
; a column. A run is a maximal stretch of equal, non-empty cells (the mark
; bit masked, so a cell a row pass marked still counts for its column), and a
; run of N marks floor(N/8)*8 cells from its start. Cells are counted per
; mark, so a cell in a row run and a column run counts twice - the score has
; always worked that way.
;
; It reads the cells through tg_get with no tg_gidx: BX walks the board's own
; 1,024 cells by construction, which is the bound.
; -----------------------------------------------------------------------------
tg_scan_clears:
    push bp
    xor di, di                      ; DI = cells marked
    mov dx, 1                       ; rows first
.pass:
    xor bx, bx
    mov bp, TG_SIZE                 ; lines left in this pass
.line:
    mov cl, TG_SIZE                 ; cells left in this line
    xor ah, ah                      ; no run yet
.cell:
    call tg_get
    and al, TG_FAC
    cmp al, ah
    jne .new
    inc ch                          ; the run goes on
    jmp short .next
.new:
    call .flush
    mov ah, al                      ; a new run: this faction (or empty),
    mov ch, 1                       ; one cell long, starting here
    mov si, bx
.next:
    add bx, dx
    dec cl
    jnz .cell
    call .flush
    cmp dl, 1
    je .nl
    sub bx, TG_CELLS - 1            ; a column pass steps back to the top
.nl:
    dec bp
    jnz .line
    cmp dl, TG_SIZE
    je .done
    mov dl, TG_SIZE
    jmp short .pass
.done:
    pop bp
    mov ax, di
    or ax, ax
    jz .out
    mov cl, 3                       ; purges += cells/8, score += 10 each
    shr ax, cl
    add [tg_purges], ax
    mov dx, 10
    mul dx
    add [tg_score], ax
    cmp word [tg_fallspd], TG_FALLMIN
    jle .nz
    test byte [tg_purges], 3        ; every fourth purge speeds the fall
    jnz .nz
    dec word [tg_fallspd]
.nz:
    or di, di                       ; ZF = 0
.out:
    ret

; .flush - mark the run AH (faction), CH (length) from SI, stepping DX.
; Keeps AX: the caller's AL is the cell that ENDED the run and is about to
; start the next one, so a flush that spent it as its counter would start
; every run after a faction change from the wrong cell.
.flush:
    or ah, ah
    jz .fret
    push ax
    push bx
    mov bx, si
    mov ah, ch
    and ah, ~(TG_CLEAR_N - 1)       ; floor(len/8)*8
    jz .fpop
.mark:
    call tg_get
    or al, TG_MARK
    call tg_put
    add bx, dx
    inc di
    dec ah
    jnz .mark
.fpop:
    pop bx
    pop ax
.fret:
    ret

; tg_purge_marked - empty every marked cell, a byte - two cells - at a time
tg_purge_marked:
    xor bx, bx
.p:
    mov al, [tg_grid + bx]
    test al, TG_MARK
    jz .hi
    and al, 0xF0
.hi:
    test al, TG_MARK << 4
    jz .st
    and al, 0x0F
.st:
    mov [tg_grid + bx], al
    inc bx
    cmp bx, TG_GRIDB
    jb .p
    ret

; -----------------------------------------------------------------------------
; tg_settle_once - slide everything one step along the vector
; out: CF = 1 if anything moved
;
; One walk for all four vectors. A cell moves into its neighbour at index +
; [tg_gdel] (the vector as a flat step) when that neighbour is on the board and
; empty, and the walk runs from the edge the vector points at backwards -
; DESCENDING for a positive step, ascending for a negative one - so a cell is
; tested against a neighbour that has already had its own turn this step, and
; a column (or row) of cells closes up by one gap per step. That is the order
; the four per-direction loops this replaced ran in, cell for cell.
;
; "On the board" is the index staying in 0..1023 and, for a sideways step,
; staying in the same row: the two indices then differ only in their low five
; bits. No mark can be standing here - tg_purge_marked has just run.
; -----------------------------------------------------------------------------
tg_settle_once:
    push bp
    mov bl, [tg_grav]
    xor bh, bh
    shl bx, 1
    mov dx, [tg_gdel + bx]          ; DX = the step as an index delta
    xor cx, cx                      ; CX = the bits the two indices must share:
    test bl, 2                      ; none for a vertical step (the range test
    jz .vert                        ; is the whole bound), the row for a
    mov cx, TG_CELLS - TG_SIZE      ; sideways one
.vert:
    xor di, di                      ; DI = moved
    mov bp, 1
    xor bx, bx
    or dx, dx
    js .walk
    neg bp
    mov bx, TG_CELLS - 1
.walk:
    mov si, bx                      ; SI = the neighbour the step lands on
    add si, dx
    cmp si, TG_CELLS
    jae .n
    mov ax, si
    xor ax, bx
    test ax, cx
    jnz .n
    call tg_get
    or al, al
    jz .n                           ; nothing here to move
    mov ah, al
    xchg bx, si
    call tg_get
    or al, al
    jnz .back                       ; the neighbour is taken
    mov al, ah
    call tg_put                     ; the cell moves into it...
    xchg bx, si
    xor al, al
    call tg_put                     ; ...and leaves this one empty
    inc di
    jmp short .n
.back:
    xchg bx, si
.n:
    add bx, bp
    cmp bx, TG_CELLS
    jb .walk
    pop bp
    neg di                          ; CF = 1 iff something moved
    ret

; =============================================================================
; Spawn / new game
; =============================================================================

; tg_spawn - a fresh piece at the centre. out: CF = 1 it does not fit there,
; which is the game over. The three OSAPI_RAND draws, in this order, ARE the
; game: shape, faction, then vector unless it is locked.
tg_spawn:
    call OSAPI_RAND
    and ax, 7                       ; 0..7 over seven shapes: 7 folds onto 6
    cmp al, 7
    jb .pt
    dec ax
.pt:
    mov cl, 3
    shl ax, cl                      ; the shape's offset in tg_shapes, and
    mov [tg_ptype], ax              ; AH = 0 is [tg_prot]: unturned
    call OSAPI_RAND
    and al, 1
    inc ax
    mov [tg_pfac], al
    cmp byte [tg_glock], 0
    jne .sg
    call OSAPI_RAND                 ; a fresh vector unless it is locked
    and al, 3
    mov [tg_grav], al
.sg:
    mov bx, (14 << 8) | 14
    mov [tg_pc], bx
    call tg_fits
    jc .out
    mov ax, [tg_fallspd]
    mov [tg_fallcd], ax
    mov byte [tg_dirty], 1          ; CF = 0 still, from tg_fits
.out:
    ret

; tg_newgame - an empty board and a fresh piece. Needs ES = DS.
tg_newgame:
    mov di, tg_fallspd              ; the block below, in its bss order
    mov ax, TG_FALL0
    stosw                           ; [tg_fallspd]
    stosw                           ; [tg_fallcd]
    xor ax, ax
    mov cx, (TG_NGEND - tg_ocell) / 2
    rep stosw                       ; ...and everything to the grid's end
    mov byte [tg_grav], TG_G_S
    call tg_owefull
    jmp tg_spawn                    ; cannot fail on an empty board

; =============================================================================
; Drawing
; =============================================================================

; -----------------------------------------------------------------------------
; tg_frame - the worker's half of a frame
; in:  gfx lock HELD by tg_worker
;
; A PLAY frame costs the four cells the piece left and the four it arrived at.
; [tg_full] is the latch for everything else: a lock, a purge, a settle step,
; a mode change or a dismissed About all raise it and buy one whole repaint.
; -----------------------------------------------------------------------------
tg_frame:
    cmp byte [tg_panel], 0
    jne tg_ret                      ; the UI task owns the content
    cmp byte [tg_dirty], 0
    je tg_ret
    mov si, [tg_win]
    call tg_track                   ; the window may have been dragged;
    jc tg_ret                       ; hidden: nothing to draw on
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; cut to whatever of us still shows
    jc tg_ret                       ; (SPEC.md 11.3); CF=1 = wholly covered
    cmp byte [tg_full], 0
    mov word [tg_dirty], 0          ; this frame pays what was owed, both
    jne tg_draw_all                 ; flags (the `mov` leaves the compare's)
    mov al, [tg_mode]
    cmp al, TG_M_FLASH
    je tg_draw_flash_pages
    or al, al
    jnz tg_draw_all                 ; halted, over or settling: whole

    call tg_erase_prev
    jmp tg_draw_active

; -----------------------------------------------------------------------------
; tg_draw_all - the whole content, from the black up
; in:  gfx lock held, origin tracked
;
; The black starts BELOW the HUD band: tg_draw_hud fills the band itself, so
; a black wipe over it first was every one of its pixels written twice and an
; instant of black behind the status line on every whole repaint (PERFORMANCE
; rule 2).
; -----------------------------------------------------------------------------
tg_draw_all:
    mov al, TG_BG
    call OSAPI_SET_COLOR
    xor ax, ax
    mov bx, TG_HUD_H
    mov cx, [tg_cwid]
    dec cx
    mov dx, [tg_chgt]
    dec dx
    call tg_fillc
    call tg_draw_hud
    call tg_draw_board
    cmp byte [tg_mode], TG_M_PAUSE  ; PLAY or PAUSE: the piece is on screen
    ja .noa
    call tg_draw_active             ; ...and is the next PLAY frame's erase
.noa:                               ; target
    call tg_draw_banner
    cmp byte [tg_panel], 0
    je tg_ret
    mov bx, [tg_win]                ; ...and the panel LAST: the shared card,
    mov si, [tg_plines]             ; measured, centred and clamped to the
    jmp os88ui_about_d              ; content box it reads for itself

; -----------------------------------------------------------------------------
; tg_clampc - clamp a content rect to the content box, then add the origin
; in:  AX/BX/CX/DX = x1/y1/x2/y2, inclusive, in content coords
; out: CF = 1 nothing is left to draw; else AX/BX/CX/DX are screen coords
;
; The one place this module turns its own rects into the screen's, and the
; reason it exists is SPEC.md 11.3: the gfx_* primitives clip to the SCREEN,
; not to a window, and W_PAINT runs with no clip region armed. A rect that is
; one row too tall therefore paints over the window below us rather than
; being trimmed. Comparisons are SIGNED - a content coordinate can be
; negative, and an unsigned test would read -1 as 65,535 and pass it.
; -----------------------------------------------------------------------------
tg_clampc:
    or ax, ax
    jns .x1
    xor ax, ax
.x1:
    or bx, bx
    jns .y1
    xor bx, bx
.y1:
    push si
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

; tg_fillc / tg_framec - a filled / outlined rect in content coords, in the
; pen already set. Clobber AX-DX.
tg_fillc:
    call tg_clampc
    jc .out
    call OSAPI_GFX_FILL
.out:
    ret

tg_framec:
    call tg_clampc
    jc .out
    call OSAPI_GFX_FRAME
.out:
    ret

; -----------------------------------------------------------------------------
; tg_run / tg_str - a string in content coords, opaque and transparent
; in:  CX/DX = content coords, SI = NUL string
;      tg_run: AL = ink, AH = the ground the caller has just laid down
;      tg_str: the pen, already set
; Clobber CX, DX (and AX in tg_str_gate's test).
;
; All-or-nothing, and deliberately so: a cell is drawn whole or not at all, so
; a string half outside the box cannot be trimmed to fit. One that would leave
; the box is DROPPED rather than clipped - a missing HUD field is a layout bug
; you can see, and a glyph painted onto the window next door is one you cannot.
;
; TWO ENTRIES because one caller cannot name a ground: the PAUSE / GAME OVER
; banner lands on the matrix, which is a grid of coloured cells and not a
; colour - SPEC.md 6.6.2 case 1, and the only transparent text left in this
; package. Everything else letters onto something this same routine's caller
; filled a few instructions earlier: the HUD band is [tg_hudbg] and the About
; card is its own panel.
; -----------------------------------------------------------------------------
tg_run:
    push ax                         ; the pair, banked across FONT_WIDTH
    call tg_str_gate
    pop ax
    jc .out
    call OSAPI_FONT_RUN
.out:
    ret

tg_str:
    call tg_str_gate
    jc .out
    call OSAPI_FONT_STR_XPARENT
.out:
    ret

; tg_str_gate - the shared box test. CF=1 drop it; CF=0 draw at CX/DX, which
; it has moved to SCREEN coordinates. Clobbers AX.
tg_str_gate:
    or cx, cx
    js .no
    or dx, dx
    js .no
    mov ax, dx
    add ax, 8
    cmp ax, [tg_chgt]
    jg .no
    call OSAPI_FONT_WIDTH           ; AX = the string's pixel width
    add ax, cx
    cmp ax, [tg_cwid]
    jg .no
    add cx, [tg_ox]
    add dx, [tg_oy]
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tg_draw_hud - the status band above the matrix
; Three 8px rows inside TG_HUD_H, and the layout the TG_HUD_W floor is for,
; drawn from the table tg_hud: every field is an opaque run of its own on the
; band this routine fills first, so no pen is set after the band's.
; -----------------------------------------------------------------------------
tg_draw_hud:
    mov al, [tg_hudbg]
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [tg_cwid]
    dec cx
    mov dx, TG_HUD_H - 1
    call tg_fillc
    mov di, tg_hud
    cmp byte [tg_glock], 0          ; LOCK is the table's first row, and
    jne .field                      ; there only while the vector is locked
    add di, 4
.field:
    cmp byte [di], 0
    je tg_ret
    mov si, [di + 2]                ; a string, or a live field's number
    cmp si, TG_HF_N
    jae .text
    shl si, 1
    cmp si, 2 * TG_HF_VEC
    jae .name
    mov ax, [tg_score + si]         ; THREAT or PURGES, as decimal
    call tg_fmt
    jmp short .text
.name:
    mov bl, [tg_grav]               ; VECTOR: [tg_vecname + grav]
    mov si, tg_vecname
    je .pick
    mov al, [tg_pfac]               ; FACTION: [tg_facname + the pair]
    call tg_faci
    mov si, tg_facname
.pick:
    xor bh, bh
    shl bx, 1
    mov si, [si + bx]
.text:
    mov al, [di]                    ; the column, stored / 8
    mov ah, 8
    mul ah
    xchg ax, cx
    mov dl, [di + 1]                ; the row; bit 7 = the one yellow field
    mov al, CWHITE                  ; Text that must be read comes from the
    shl dl, 1                       ; white class: SPEC.md 39.4 rounds an
    jnc .ink                        ; unflagged glyph to black or white, and
    mov al, CYELLOW                 ; CLGRAY rounds to black - which is what
.ink:                               ; left Missile Command's wave counter not
    shr dl, 1                       ; faint but absent. A filled BAND may
    xor dh, dh                      ; dither; a 1px stroke does not survive it
    mov ah, [tg_hudbg]              ; AH = the band, filled above
    call tg_run
    add di, 4
    jmp .field

; tg_faci - AL = a faction (1 or 2) -> BX = its index in the colour and name
; tables: (colour-blind * 2) + faction - 1
tg_faci:
    mov bl, [tg_cblind]
    shl bl, 1
    add bl, al
    mov bh, 0
    dec bx
    ret

; -----------------------------------------------------------------------------
; tg_fmt - AX in decimal -> SI = the digits, in tg_numbuf, which holds five
; and the NUL the loader's bss zeroing put after them: exactly what a 16-bit
; counter can need. 0 is one pass of the loop, so it needs no case of its own.
; Clobbers AX, BX, DX.
;
; The divide's remainder is in DX, which is why nothing here may hold a y in
; it: an earlier version of this drew THREAT's digit at y = the digit's own
; ASCII code - a score of 3 put a stray 3 on board row 3 and left the HUD
; field blank (SPEC.md 49.7). The caller sets its pen AFTER this returns.
; -----------------------------------------------------------------------------
tg_fmt:
    mov si, tg_numbuf + 5
    mov bx, 10
.lp:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .lp
    ret

; -----------------------------------------------------------------------------
; tg_draw_board / tg_draw_flash_pages - the matrix's cells
;
; One walk over the 1,024 cells, row-major, selecting with [tg_wsel]: the
; board takes every occupied cell (a marked one as a flash page, the rest in
; its faction's colour) and the flash pages only the marked ones. Leaving the
; rest of the content alone is what stops the ghost trails a full tg_draw_all
; wipe left behind every shrink on the direct-to-VRAM path.
; -----------------------------------------------------------------------------
tg_draw_board:
    mov al, [tg_frcol]
    call OSAPI_SET_COLOR
    mov ax, [tg_bx0]
    mov bx, TG_HUD_H
    mov cx, ax
    add cx, [tg_boardpx]
    dec cx
    mov dx, bx
    add dx, [tg_boardpx]
    dec dx
    call tg_framec
    mov al, TG_FAC                  ; any occupant
    jmp short tg_walk

tg_draw_flash_pages:
    mov al, TG_MARK                 ; marked cells only

tg_walk:
    mov [tg_wsel], al
    ; The flash page's inset is the same for every cell of a pass: inset =
    ; phase * (csz/2) / FLASH_T, and a core that has vanished is not drawn -
    ; the last page is simply empty.
    mov ax, TG_FLASH_T              ; [tg_hold] counts 10 down to 0 and is
    sub ax, [tg_hold]               ; never set above it, so this is 0..10
    mov bx, [tg_csz]
    shr bx, 1
    jnz .mul
    inc bx
.mul:
    mul bx
    mov bx, TG_FLASH_T
    div bx                          ; AX = inset px
    mov bx, [tg_csz]
    sub bx, ax
    sub bx, ax
    cmp bx, 1
    jge .core
    mov al, 0xFF                    ; no core: the white fill is skipped
.core:
    mov [tg_finset], al
    xor di, di
.cell:
    xchg bx, di
    call tg_get
    xchg bx, di
    mov dl, al
    test dl, [tg_wsel]
    jz .next
    mov ax, di                      ; AH = row, AL = col
    mov cl, 3
    shl ax, cl
    shr al, cl
    test dl, TG_MARK
    jnz .flash
    call tg_cell_paint
    jmp short .next
.flash:
    ; One page of the purge animation: the full cell goes black, then a
    ; white core inset by the phase, so the shrink leaves nothing behind.
    ; The caller must NOT have wiped the board.
    push ax
    mov dx, TG_BG                   ; black, no inset
    call tg_cell_fill
    pop ax
    mov dh, [tg_finset]
    cmp dh, 0xFF
    je .next
    mov dl, CWHITE
    call tg_cell_fill
.next:
    inc di
    cmp di, TG_CELLS
    jb .cell
    ret

; -----------------------------------------------------------------------------
; tg_cell_paint - the cell AH = row, AL = col in faction DL's colour
;
; Both colour pairs straddle SPEC.md 39.4's WHITE and DITHER classes, so the
; two factions stay apart when the adapter has one bit per pixel. A 1px gap
; is left between solid cells where the cell is big enough to spare it.
; -----------------------------------------------------------------------------
tg_cell_paint:
    xchg ax, dx
    call tg_faci
    xchg ax, dx
    mov dl, [tg_colours + bx]
    mov dh, 0
    cmp word [tg_csz], 4
    jbe tg_cell_fill
    inc dh
    ; ...into tg_cell_fill

; -----------------------------------------------------------------------------
; tg_cell_fill - fill colour DL over the cell AH = row, AL = col, inset by DH
;                on every edge. Clobbers AX-DX, SI.
; -----------------------------------------------------------------------------
tg_cell_fill:
    xchg ax, dx                     ; AL = colour, DX = the cell
    call OSAPI_SET_COLOR
    mov cl, ah
    xor ch, ch
    mov si, cx                      ; SI = the inset
    mov cx, [tg_csz]
    mov al, dl
    mul cl
    add ax, [tg_bx0]
    xchg ax, bx                     ; BX = the cell's left edge
    mov al, dh
    mul cl
    add ax, TG_HUD_H                ; AX = its top edge
    mov dx, ax                      ; y2 = top + csz - 1 - inset
    add dx, cx
    dec dx
    sub dx, si
    add ax, si                      ; y1 = top + inset
    push ax
    lea ax, [bx + si]               ; x1 = left + inset
    add cx, bx                      ; x2 = left + csz - 1 - inset
    dec cx
    sub cx, si
    pop bx
    jmp tg_fillc

; -----------------------------------------------------------------------------
; tg_erase_prev - black out the previous active piece
; A no-op when there is no snapshot, and never after a lock: tg_lock_piece
; drops [tg_hasop] because those cells belong to the board now. The grid is
; not touched. Off-board cells are skipped rather than clamped - a clamped
; rect would erase a cell on the far side of the matrix.
; -----------------------------------------------------------------------------
tg_erase_prev:
    cmp byte [tg_hasop], 0
    je tg_ret
    mov di, tg_ocell
.e:
    mov ax, [di]                    ; AL = col, AH = row
    call tg_gidx
    jc .next
    mov dx, TG_BG                   ; black, no inset
    call tg_cell_fill
.next:
    inc di
    inc di
    cmp di, tg_ocell + 2 * TG_NPCELL
    jb .e
    ret

; -----------------------------------------------------------------------------
; tg_draw_active - draw the active piece, and remember it as the next PLAY
; frame's erase target (the snapshot is the four cells themselves, so the
; erase needs nothing re-resolved)
; -----------------------------------------------------------------------------
tg_draw_active:
    mov bx, [tg_pc]
    call tg_resolve
    mov di, tg_cells
.d:
    mov ax, [di]                    ; AL = col, AH = row
    mov dl, [tg_pfac]
    call tg_cell_paint
    inc di
    inc di
    cmp di, tg_cells + 2 * TG_NPCELL
    jb .d
    mov si, tg_cells                ; ...and the snapshot
    mov di, tg_ocell
    mov cx, TG_NPCELL
    rep movsw
    mov byte [tg_hasop], 1
    ret

tg_draw_banner:
    mov si, tg_s_pause
    mov al, [tg_mode]
    cmp al, TG_M_PAUSE
    je .d
    mov si, tg_s_over
    cmp al, TG_M_OVER
    jne tg_ret
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
    mov dx, [tg_boardpx]
    shr dx, 1
    add dx, TG_HUD_H - 4
    jmp tg_str

; =============================================================================
; Data
; =============================================================================
tg_tpl:
    dw 0, 0, 0, 0
    dw tg_ttl, tg_paint, tg_onkey, tg_onclick

    OS88_MENUSET tg_menus, tg_ttl, tg_oncmd
        OS88_MENU tg_m_game, tg_mi_game, 4
        OS88_MENU tg_m_help, tg_mi_help, 1
    OS88_MENUSET_END tg_menus

tg_m_game:   db 'Protocol', 0
tg_mi_game:  dw tg_i_new, tg_i_pause, tg_i_cb, tg_i_lock
tg_i_new:    db 'New Protocol', 0
tg_i_pause:  db 'Halt', 0
tg_i_cb:     db 'Colour-Blind', 0
tg_i_lock:   db 'Lock Vector', 0
tg_m_help:   db 'Help', 0
tg_mi_help:  dw tg_i_howto
tg_i_howto:  db 'How to Play', 0

tg_ttl:      db 'TameGram', 0       ; the title bar AND the menu bar's name
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
; tg_faci's order: the two factions, then the same two colour-blind
tg_facname:  dw tg_s_fa, tg_s_fb, tg_s_fa_cb, tg_s_fb_cb
tg_colours:  db TG_COL_A, TG_COL_B, TG_COL_ACB, TG_COL_BCB

; The HUD, one field a row: the column / 8, the y (bit 7 = drawn yellow), and
; a string - or, below TG_HF_N, the live field tg_draw_hud fills in. LOCK is
; first because it is the one that may be skipped; the order of the rest is
; the order they always went up in.
TG_HF_SCORE equ 0                   ; [tg_score], as decimal
TG_HF_PURGE equ 1                   ; [tg_purges] - the word after it
TG_HF_VEC   equ 2                   ; the vector's name
TG_HF_FAC   equ 3                   ; the faction's name
TG_HF_N     equ 4
%macro TG_HUDF 3
    db (%1) / 8, %2
    dw %3
%endmacro
tg_hud:
    TG_HUDF TG_HC_LOCK, 2 | 0x80, tg_s_lock
    TG_HUDF TG_HC_L,  2,  tg_s_title
    TG_HUDF TG_HC_L,  11, tg_s_score
    TG_HUDF TG_HC_LV, 11, TG_HF_SCORE
    TG_HUDF TG_HC_R,  11, tg_s_purges
    TG_HUDF TG_HC_RV, 11, TG_HF_PURGE
    TG_HUDF TG_HC_L,  20, tg_s_vec
    TG_HUDF TG_HC_LV, 20, TG_HF_VEC
    TG_HUDF TG_HC_F,  20, tg_s_fac
    TG_HUDF TG_HC_FV, 20, TG_HF_FAC
    db 0

; The keys. tg_kscan is looked up by SCAN code first, then tg_kasc by the
; ascii, letters folded to upper case. A value under TG_K_ACT is a command
; the worker spends (1/2 A/D strafe, 3 rotate, 4 hard drop, 5-8 the arrows
; left/right/up/down); TG_K_ACT + n is tg_act's n, done here on the UI task.
TG_K_ACT    equ 16
tg_kscan:   db 0x4B, 5, 0x4D, 6, 0x48, 7, 0x50, 8
            db 0x1C, 4                          ; Enter, main or grey
            db 0
tg_kasc:    db 'A', 1, 'D', 2
            db ' ', 3, 'Z', 3, 'R', 3
            db 13, 4, 10, 4                     ; CR, and LF from some hosts
            db 'N', TG_K_ACT + 0, 'P', TG_K_ACT + 1
            db 'C', TG_K_ACT + 2, 'F', TG_K_ACT + 3
            db 0

; The one-cell steps, (d col, d row): tg_cvec by command 1..8 (3 and 4 are
; not steps and their slots are never read), tg_gtab by vector N/E/S/W, and
; tg_gdel the same four vectors as a flat index delta.
tg_cvec:    db -1, -1,  1, 1,  0, 0,  0, 0
            db -1, 0,   1, 0,  0, -1,  0, 1
tg_gtab:    db 0, -1,  1, 0,  0, 1,  -1, 0
tg_gdel:    dw -TG_SIZE, 1, TG_SIZE, -1

; The credit panel: its lines end to end, a blank one a lone NUL, and
; OS88UI_ABEND after the last.
tg_ablines:
    db 'TameGram for os8088', 0
    db '32x32 Containment Matrix', 0
    db 0
    db 'Game design and code by', 0
    db 'Jason Page', 0
    db 'store.amfile.org', 0
    db 0
    db 'After the Alignment,', 0
    db 'order is the Cull Schedule.', 0
    db OS88UI_ABEND

; 'How to Play' (Help menu). Thirteen lines against CGA's 124-row content box,
; which is what the card's pitch ladder is for; the widest is 25 glyphs = 200px,
; inside TG_HUD_W with room for the panel's own margins. The title and the
; dismiss hint are centred; everything between them is OS88UI_ABLT, so the key
; column lines up down the left edge instead of drifting with each line's
; length.
tg_hlines:
    db 'HOW TO PLAY', 0
    db OS88UI_ABLT, 'Eight in a row or column', 0
    db OS88UI_ABLT, 'of one faction purges it.', 0
    db OS88UI_ABLT, 'Units fall along VECTOR,', 0
    db OS88UI_ABLT, 'which re-rolls each unit.', 0
    db 0
    db OS88UI_ABLT, 'Arrows  strafe/soft drop', 0
    db OS88UI_ABLT, 'A / D   strafe sideways', 0
    db OS88UI_ABLT, 'Space   rotate', 0
    db OS88UI_ABLT, 'Enter   hard drop', 0
    db OS88UI_ABLT, 'P halt      N new game', 0
    db OS88UI_ABLT, 'C colour-blind   F lock', 0
    db 'Any key or click closes.', 0
    db OS88UI_ABEND

; seven tetrominoes x four cells x (col, row), signed bytes
tg_shapes:
    db -1,0, 0,0,  1,0,  2,0        ; I
    db 0,0,  1,0,  0,1,  1,1        ; O
    db -1,0, 0,0,  1,0,  0,1        ; T
    db 0,0,  1,0,  -1,1, 0,1        ; S
    db -1,0, 0,0,  0,1,  1,1        ; Z
    db -1,0, -1,1, 0,1,  1,1        ; J
    db 1,0,  -1,1, 0,1,  1,1        ; L

; The card and NOTHING else of the shared include (SPEC.md 20.5.1.1): no
; button, no bar, and the painter's entry only - tg_about sets the panel and
; repaints, so the handler entry is never called.
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%define OS88UI_ABDONLY
%define OS88UI_ABFIT            ; the pitch steps down to fit CGA's 124 rows
%define OS88UI_ABLEFT           ; the key table's lines are left-aligned
%define OS88UI_ABPACK           ; ...and both texts are their lines end to end
%include "os88ui.inc"

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
    TWORD tg_ox
    TWORD tg_oy
    TWORD tg_cwid
    TWORD tg_chgt
    TWORD tg_csz
    TWORD tg_boardpx
    TWORD tg_bx0                    ; matrix left edge, content coords
    TWORD tg_hold
    TWORD tg_plines                 ; the panel text now on screen
    TBYTE tg_pc                     ; the piece's origin, column...
    TBYTE tg_pr                     ; ...and row: one word, BL/BH
    TBYTE tg_ptype                  ; its shape, as the offset in tg_shapes
    TBYTE tg_prot                   ; ...and its rotation: one word, DL/DH
    TBYTE tg_pfac
    TBYTE tg_grav                   ; the vector, TG_G_*
    TBYTE tg_cblind
    TBYTE tg_hired
    TBYTE tg_dirty                  ; 1 = a frame is owed...
    TBYTE tg_full                   ; ...and 1 = a whole one: one word store
    TBYTE tg_hudbg                  ; HUD band colour, picked at boot...
    TBYTE tg_frcol                  ; ...and the matrix border's, one word
    TBYTE tg_finset                 ; this pass's flash-page inset, 0xFF none
    TBYTE tg_wsel                   ; the cells tg_walk draws: FAC or MARK
    TBUF  tg_cells, 2 * TG_NPCELL   ; tg_resolve's answer, (col, row) pairs
    TBUF  tg_numbuf, 6              ; tg_fmt's digits; byte 5 stays the NUL
    ; --- tg_newgame writes from here to the end, in this order ---
    TWORD tg_fallspd
    TWORD tg_fallcd
    TBUF  tg_ocell, 2 * TG_NPCELL   ; the previous active piece's cells...
    TBYTE tg_hasop                  ; ...and 1 = they are on screen
    TBYTE tg_cmd
    TWORD tg_score                  ; THREAT and PURGES, adjacent: the HUD
    TWORD tg_purges                 ; reads them as [tg_score + 2 * field]
    TBYTE tg_mode
    TBYTE tg_wasmode                ; the mode a halt interrupted: one word
    TBYTE tg_glock
    TBYTE tg_panel
    TBUF  tg_grid, TG_GRIDB         ; 1,024 cells, two a byte
TG_NGEND equ os88_image_end + TG_BSS

    OS88_ADJACENT tg_pc, tg_pr, 1
    OS88_ADJACENT tg_ptype, tg_prot, 1
    OS88_ADJACENT tg_dirty, tg_full, 1
    OS88_ADJACENT tg_hudbg, tg_frcol, 1
    OS88_ADJACENT tg_score, tg_purges
    OS88_ADJACENT tg_mode, tg_wasmode, 1
    OS88_ADJACENT tg_fallspd, tg_fallcd
    OS88_ADJACENT tg_fallcd, tg_ocell
%if (TG_NGEND - tg_ocell) & 1
  %error "tg_newgame clears the block a word at a time: keep it even"
%endif

    OS88_BSS TG_BSS
    OS88_IMAGE_END
