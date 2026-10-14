; =============================================================================
; os8088 - apps/isotron/isotron.asm
;
; isoTron (SPEC.md 108) - a light-wall duel, the player against the computer,
; on a raised slab drawn in a 45-degree cabinet projection. Every trail is an
; extruded wall; a player win that follows a player win stacks the next arena
; one slab higher, up to four, and past four the tower starts again.
;
; A remake from OBSERVED RULES: there is no source and no disassembly behind
; it, and every surface it will draw is drawn by code (SPEC.md 108.1). The
; rules are pinned in SPEC.md 108.3 - 108.6 and written down a second time,
; normatively, in tools/isotronsim.py; tests/isotron.py holds the guest and the
; model together round by round on MartyPC.
;
; --- WAVE 1 (docs/plans/ISOTRON-PLAN.md section 19) ---------------------------
; This build is the RULES ENGINE in its real shell: the window, the menus, the
; About card, the one claim with its refusal in words, the worker with its
; pace governor, the turn FIFO and the harness. What it draws is a FLAT
; top-down DEBUG VIEW of the occupancy grid, one pixel a cell, composed into
; the claim's canvas region and put down through OSAPI_GFX_BLIT1 a dirty row
; range at a time. Wave 2 replaces the view with the 3D canvas and deletes it.
;
; --- THE THREE DECISIONS THAT DECIDE EVERYTHING ------------------------------
;
;  1. **THE ITERATION IS THE UNIT OF THE GAME.** Every live computer head
;     moves one cell an iteration and the player p/6 of one; an iteration is
;     CHARGED its fitted 5150 cost in 1/768ths of a tick, and the worker runs
;     as many as the clock has paid for (SPEC.md 108.4). So a 4.77 MHz XT and a
;     386 play at the same pace, the couplings the footage shows survive (more
;     heads slow everyone, a boost slows the computer), and a slow PRESENT
;     costs frames, never pace.
;
;  2. **ONE RNG BYTE A COMPUTER HEAD-MOVE.** The computer's decisions draw
;     exactly one byte of a 16-bit LCG per move, whatever they decide, so the
;     stream depends on the move count alone and a seeded game is a function
;     of its seed and its keys (SPEC.md 108.6). That is what lets a MartyPC
;     row play whole games and compare them with the host model.
;
;  3. **THE GRID IS THE TRUTH; THE PICTURE IS DERIVED.** One byte a cell,
;     217 x 101 with the outline ring occupied, so collision is one compare
;     and the occlusion probe of wave 2 is an unrolled run of compares.
;
; Keys: Ins / Del (or Left / Right, Z / X) turn; F10 / Up faster, F9 / Down
; normal; P pauses; Space continues; Esc ends the game. At the title 1, 2, 3
; start a game with sound and A, B, C a silent one.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'isoTron', it_entry, 1, OS88_STACK_192
                                ; the worker's stack class (SPEC.md 8.7):
                                ; tools/stkdepth.py prices it_worker's
                                ; deepest static chain at 34 bytes (a
                                ; relayout's HUD line); with the 64-byte
                                ; interrupt floor that is 98 of 192, 1.96x.
                                ; t_stkclass holds the bar at 1.25x

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) --------------------------
; A slab in the game's own projection with a wall standing on it.
;
;   ................
;   ..........##....
;   ..........##....
;   ......########..
;   ......########..
;   ......##........
;   .....###........
;   ....###########.
;   ...##.........##
;   ..##.........##.
;   .##############.
;   #.#.#.#.#.#.#.##
;   .#.#.#.#.#.#.##.
;   ###############.
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay): each row
    dw 0x0030                       ; filled between its outermost pixels, so
    dw 0x0030                       ; the slab is solid and not see-through
    dw 0x03FC
    dw 0x03FC
    dw 0x0300
    dw 0x0700
    dw 0x0FFE
    dw 0x1FFF
    dw 0x3FFE
    dw 0x7FFE
    dw 0xFFFF
    dw 0x7FFE
    dw 0xFFFE
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0030
    dw 0x0030
    dw 0x03FC
    dw 0x03FC
    dw 0x0300
    dw 0x0700
    dw 0x0FFE
    dw 0x1803
    dw 0x3006
    dw 0x7FFE
    dw 0xAAAB
    dw 0x5556
    dw 0xFFFE
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

; =============================================================================
; Constants
; =============================================================================

; --- IT_MARK name: a harness MARK (ittest.inc), `name equ $`, emitted only
; when IT_MARKS is defined. It emits no byte either way, so the image is the
; same; what the switch keeps out is the SYMBOL: tools/stkdepth.py splits
; routines at every symbol in nasm's map, and a mark in the middle of the
; worker made t_stkclass price a worker that never calls it_wake.
; tests/isotronlib.py assembles with -DIT_MARKS and checks the image is the
; one that shipped.
%macro IT_MARK 1
  %ifdef IT_MARKS
%1 equ $
  %endif
%endmacro

; --- the instance claim (SPEC.md 108.8) ---------------------------------------
; ONE OSAPI_MEM_CLAIM, pinned, holding the three things that are too big for
; the package's own segment. Wave 1 uses the GRID, and borrows the CANVAS's
; first 32,000 bytes for the debug view's band (80 bytes x 400 rows); wave 2
; gives the canvas back to the 4-bit picture it is laid out for.
IT_CANVAS   equ 0               ; 32,000: 320 x 200 x 4 bits (wave 2)
IT_GRID     equ 32000           ; 21,917: 217 x 101, one byte a cell
IT_STRIP    equ 53920           ; 5,120: the presenters' strip (wave 2), on a
IT_STRIPSZ  equ 5120            ; 16-byte boundary (53,917 rounded up)
IT_CLAIMEND equ IT_STRIP + IT_STRIPSZ
IT_CLAIMKB  equ (IT_CLAIMEND + 1023) / 1024
%if IT_CLAIMEND > 65535
  %error "isoTron's claim layout passes 64 KB - one segment cannot address it"
%endif
%if (IT_GRID < 32000) || (IT_STRIP < IT_GRID + 21917) || (IT_STRIP & 15)
  %error "isoTron's claim layout overlaps or the strip is not 16-byte aligned"
%endif

; --- the grid (SPEC.md 108.2) -------------------------------------------------
GW          equ 217             ; u = 0..216, the ring at 0 and 216
GH          equ 101             ; d = 0..100, the ring at 0 and 100
GSIZE       equ GW * GH         ; 21,917
HD_N        equ 0               ; headings; left = (h+3)&3, right = (h+1)&3,
HD_E        equ 1               ; and bit 0 is the AXIS: 1 = E/W
HD_S        equ 2
HD_W        equ 3
C_EDGE      equ 0x03            ; a grid cell: the outline ring...
C_PLAYER    equ 0x10            ; ...or an owner, OR'd with the AXIS of the
C_COMP      equ 0x20            ; move that entered it (bit 0: 1 = N/S)
P_U0        equ 108             ; the player starts here, heading N
P_D0        equ 27
C_U0        equ 108             ; ...and h computer heads from this one cell
C_D0        equ 90
OPEN_LEG    equ 32              ; the opening head turns S after 32 cells
P_NORMAL    equ 2               ; the player's speed in SIXTHS of a cell
P_MAX       equ 9
RUN_CAP     equ 255
U_TURN_MIN  equ 16
BOT_LOOK    equ 3
MAXHEADS    equ 4

; --- a head (the player is record 4) ------------------------------------------
H_IDX       equ 0               ; word: d*217 + u
H_U         equ 2
H_D         equ 3
H_HD        equ 4
H_ALIVE     equ 5
H_LEG       equ 6               ; cells moved since the last turn
H_TURNED    equ 7               ; turned at all this round
H_UPEND     equ 8               ; a U-turn armed: moves until it fires
H_USIDE     equ 9               ; ...and its side: 0 left, 1 right
H_SIZE      equ 10

; --- turn keys, the FIFO's bytes and the harness script's -------------------
K_LEFT      equ 1
K_RIGHT     equ 2
K_BOOST     equ 3
K_NORMAL    equ 4
FIFO_N      equ 3               ; the turn FIFO's depth (a 4-slot ring)

; --- the pace (SPEC.md 108.4) -------------------------------------------------
U_TICK      equ 768             ; a BIOS tick in the governor's units
DEBT_CAP    equ 2 * U_TICK      ; a stall owes at most one tick past this one
IT_FASTN    equ 16              ; iterations a FAST (harness) wake runs

; --- game states --------------------------------------------------------------
ST_TITLE    equ 0
ST_PLAY     equ 1
ST_CRASH    equ 2               ; the 0.8 s after a crash
ST_WAIT     equ 3               ; ...waiting for Space
ST_OVER     equ 4               ; three losses in a row
ST_NEXT     equ 5               ; the harness's NOFLOW: the next round at once
WIN_NONE    equ 0
WIN_PLAYER  equ 1
WIN_COMP    equ 2
IT_CRASHT   equ 15              ; 0.8 s of CRASH, in ticks
IT_OVERT    equ 64              ; 3.5 s of GAME OVER

; --- the debug view's band (wave 1 only) --------------------------------------
IT_BSTRIDE  equ 80              ; bytes a band row: 640 px
IT_BROWS    equ 400             ; ...and its rows: 32,000 bytes, the canvas
IT_GX       equ 16              ; the grid's top-left inside the band, px
IT_GY       equ 12              ; (u = 0 at x = 16; d = 100 on row 12)
IT_HUDY     equ 2               ; the HUD line's band row
IT_MSGY     equ IT_GY + GH + 4  ; ...and the message line's
IT_OXPAD   equ 7               ; the most the band's origin is pushed right
                                ; to reach the byte grid (it_geom)
IT_NEEDW    equ IT_GX + GW + 8 + IT_OXPAD ; the smallest content that holds it
IT_NEEDH    equ IT_MSGY + 8 + 1
IT_TEXTN    equ 76              ; cells a text line may carry (78 on 640)

; =============================================================================
; it_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; The CLAIM is taken here, where a refusal can still be said in words before
; anything has been drawn: a package that discovers it cannot have its memory
; owes the user a sentence rather than a window that never draws. kern_small's
; arena is 52.5 KB, so that is where the sentence is read (SPEC.md 108.8).
; =============================================================================
it_entry:
    push si
    mov si, it_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [it_win], bx
    OS88_REGION_MOVABLE             ; our region may move (SPEC.md 66.6.1)
    mov si, it_pref
    call OSAPI_WM_PREFER
    mov si, it_menus
    call OSAPI_MENU_SET
    mov si, it_about
    call OSAPI_ABOUT_SET
    OS88_ALTENTER_ARM               ; arms OSAPI_KEY_DOWN as well: the full-
                                    ; screen chord is wave 3's, the arming is
                                    ; the entry proc's (SPEC.md 11.2.1.1)
    mov bx, [it_win]
    mov ax, it_onresize
    call OSAPI_WM_ONRESIZE
    mov byte [it_state], ST_TITLE
    mov byte [it_tier], 1
    mov byte [it_snd], 1
    call OSAPI_MEM_AVAIL            ; ASK BEFORE CLAIMING (os88api.inc): a
    cmp ax, IT_CLAIMKB              ; claim that fails SHEDS the disk caches on
    jb .nomem                       ; its way down, and kern_small - the
    mov ax, IT_CLAIMKB              ; machine this refusal is for - would pay
    call OSAPI_MEM_CLAIM            ; that on every launch. The CF test stays:
    jc .nomem                       ; the answer is the deliverable, not a vow
    mov [it_seg], dx
    mov bx, [it_win]
    mov al, 1
    call OSAPI_WM_OWNBG             ; the band is every pixel of the content
    call OSAPI_FONT_GLYPHS          ; the 8x8 table the band's text is copied
    mov [it_gseg], dx               ; from (DX:SI; AL..AH the codes it covers)
    mov [it_goff], si
    mov [it_gfirst], al
    mov [it_glast], ah
    jmp short .ok
.nomem:
    call it_nomem_msg               ; "isoTron needs 58 KB ..." in it_msgbuf
    mov byte [it_nomem], 1
.ok:
    mov bx, [it_win]
    clc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; it_nomem_msg - compose the refusal into it_msgbuf, with the largest run the
;                heap can give (OSAPI_MEM_AVAIL: the deliverable figure)
; -----------------------------------------------------------------------------
it_nomem_msg:
    push ax
    push bx
    push si
    push di
    mov di, it_msgbuf
    mov si, it_s_nomem1
    call it_strcpy
    mov ax, IT_CLAIMKB
    call it_putdec
    mov si, it_s_nomem2
    call it_strcpy
    call OSAPI_MEM_AVAIL            ; AX = the largest free run, KB
    call it_putdec
    mov si, it_s_nomem3
    call it_strcpy
    mov byte [di], 0
    pop di
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; THE WINDOW CALLBACKS - each one runs on the UI task with the gfx lock held,
; ES = KERNEL_SEG (SPEC.md 20), and preserves every register
; =============================================================================

; -----------------------------------------------------------------------------
; it_paint - W_PAINT. The kernel has armed the clip for us.
; -----------------------------------------------------------------------------
it_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [it_nomem], 0
    jne .nomem
    call it_spawn_ck                ; the worker starts here, not at the entry
                                    ; proc: TASK_SPAWN refuses there (the
                                    ; loader has not published the instance)
    call it_geom
    jc .done                        ; not visible: nothing to draw
    call it_layout_ck
    mov byte [it_full], 1
    call it_present_now             ; the whole band, every pixel once
    cmp byte [it_abon], 0
    je .done
    mov bx, [it_win]
    mov si, it_ablines
    call os88ui_about_d             ; the card LAST, over its own rect
    jmp short .done
.nomem:
    ; NO CLAIM, SO NO BAND: the kernel filled the content white (no OWNBG on
    ; this path) and one opaque run letters the refusal over it
    mov bx, [it_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov si, it_msgbuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.done:
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
; it_spawn_ck - start the worker, once (dd_spawn_ck's shape: a refusal is
;               TRANSIENT, so the flag is set only on success)
; -----------------------------------------------------------------------------
it_spawn_ck:
    cmp byte [it_spawned], 0
    jne .out
    push ax
    push bx
    mov ax, it_worker
    mov bx, [it_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [it_spawned], 1
    OS88_WORKER_RESTARTABLE it_worker   ; every byte that outlives a pass of
.no:                                    ; the loop is a static (SPEC.md 66.6.2)
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_onresize - OSAPI_WM_ONRESIZE: our box changed and we did not ask
; -----------------------------------------------------------------------------
it_onresize:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [it_nomem], 0
    jne .out
    call it_geom
    jc .out
    call it_layout_ck
    mov byte [it_full], 1           ; the worker presents it on its next wake
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_onkey - W_ONKEY: AL = ascii, AH = scan, SI = window ptr
; -----------------------------------------------------------------------------
it_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call it_abdismiss
    cmp byte [it_nomem], 0
    jne .out
    call it_key                     ; itinput.inc: the one key map
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
; it_onclick - W_ONCLICK: a click takes the About card down, and that is all
;              (a keyboard game: a mouse that steered would be a second input)
; -----------------------------------------------------------------------------
it_onclick:
    call it_abdismiss
    ret

; -----------------------------------------------------------------------------
; it_oncmd - AM_ONCMD: the Game menu. AL = item, AH = menu, SI = the window
; -----------------------------------------------------------------------------
it_oncmd:
    push ax
    push bx
    call it_abdismiss
    cmp byte [it_nomem], 0
    jne .out
    or ah, ah
    jnz .out
    cmp al, 3
    jb .tier                        ; 0..2: a new game at that tier, with sound
    je .pause
    cmp al, 7
    jne .out                        ; 4..6 are greyed (MENU_DIS): never picked
    mov byte [it_qesc], 1           ; End Game
    jmp short .out
.tier:
    inc al
    or al, 0x80                     ; bit 7: sound on (the plan's 9.2: the
    mov [it_qstart], al             ; menu's numbers start with sound)
    jmp short .out
.pause:
    mov byte [it_qpause], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_about / it_abdismiss - the standard About card (SPEC.md 20.5.1). A game
; in play is PAUSED under it: the card is opaque over its own rect and the
; worker does not present while it is up. The pause is POSTED (it_qhold), not
; written: this is the UI task, and the game state has one writer, the worker
; (SPEC.md 108.7.1), which takes it on its next wake inside its lock hold.
; -----------------------------------------------------------------------------
it_about:
    push bx
    push si
    mov byte [it_abon], 1
    mov byte [it_qhold], 1          ; pause a round in play (it_commands)
    mov bx, si
    mov si, it_ablines
    call os88ui_about
    pop si
    pop bx
    ret

it_abdismiss:
    cmp byte [it_abon], 0
    je .none
    mov byte [it_abon], 0
    mov byte [it_full], 1           ; the worker puts the band back
.none:
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6, 108.4)
;
; The game IS the worker: the computer's heads move between keystrokes, and a
; window callback only runs when something happens to the window. One wake a
; tick; under the lock it runs the governor (as many iterations as the clock
; has paid for), then presents what changed. The lock hold is a short burst:
; a few head-steps of logic and one blit of a dirty row range.
;
; A FAST harness game (it_tflags bit 0) yields instead of sleeping and runs
; IT_FASTN iterations a wake regardless of the clock, so a MartyPC row plays a
; whole seeded game in guest seconds.
; =============================================================================
it_worker:
    call OSAPI_GET_TICKS
    mov [it_lastt], ax
.loop:
    mov bx, [it_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here
    test byte [it_tflags], IT_TF_FAST
    jz .sleep
    cmp byte [it_state], ST_TITLE   ; FAST, and a game to play: no sleep
    je .sleep
    call OSAPI_TASK_YIELD
    jmp short .wake
.sleep:
    mov ax, 1
    call OSAPI_TASK_SLEEP
.wake:
    call it_focus_ck                ; no lock: WM_TOP and MENU_OWNER take none
    call OSAPI_GFX_LOCK
    IT_MARK it_wake0
    call it_wake
    IT_MARK it_wake1
    call OSAPI_GFX_UNLOCK
    IT_MARK it_frame_end
    jmp .loop

; -----------------------------------------------------------------------------
; it_focus_ck - a real-time game is told when it gains the front and never
;               when it loses it, so it asks (dd_focus_ck, SPEC.md 12.6)
; -----------------------------------------------------------------------------
it_focus_ck:
    push bx
    cmp byte [it_state], ST_PLAY
    jne .out
    cmp byte [it_paused], 0
    jne .out
    test byte [it_tflags], IT_TF_BOT
    jnz .out                        ; the harness's bot needs no audience
    call OSAPI_WM_TOP
    cmp bx, [it_win]
    jne .away
    call OSAPI_MENU_OWNER           ; ...and a click on the bare desktop hands
    cmp bx, [it_win]                ; the bar to Locator without moving the
    je .out                         ; z-order
.away:
    mov byte [it_paused], 1         ; sticky: P or Space resumes - and the
    mov byte [it_msgd], 1           ; message line is what says so
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_wake - one wake's work, lock held. Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_wake:
    cmp byte [it_nomem], 0
    jne .out
    mov es, [it_seg]                ; ES = the claim for the whole of the logic
    call it_commands                ; what the UI task asked for (itinput.inc)
    call it_flow                    ; the state machine and the governor
    call it_present                 ; ...and what changed, to the glass
.out:
    ret

; =============================================================================
; GEOMETRY AND THE DEBUG VIEW'S BAND (wave 1; wave 2 replaces all of this)
; =============================================================================

; -----------------------------------------------------------------------------
; it_geom - bank the content box. out: CF=1 the window is not visible.
; Clobbers AX, BX, CX, DX.
; -----------------------------------------------------------------------------
it_geom:
    mov bx, [it_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top (a drag moves these
    mov [it_cx], ax                 ; and tells nobody, so it is asked every
    mov [it_cy], dx                 ; time - SPEC.md 93.3.4.2's lesson)
    add ax, IT_OXPAD                ; the band goes down at the first BYTE
    and ax, 0xFFF8                  ; column inside the content: gfx_blit1
    mov [it_ox], ax                 ; reads an x off the grid as a HEAD mask
                                    ; (SPEC.md 5.4.2.8) - the band's first
                                    ; x & 7 px not drawn, its bit 7 on the
                                    ; byte column holding x - and kern_small
                                    ; refuses one outright. A content box at
                                    ; x = 1 (CGA, VGA) or 3 (Hercules) lost
                                    ; the ring's u = 0 side to exactly that.
    mov bx, [it_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height, CF = hidden
    jc .out
    mov [it_cw], cx
    mov [it_ch], dx
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; it_layout_ck - recompose the band if the content box changed size (or was
;                never laid out). Clobbers AX..DI; ES = the claim on return.
; -----------------------------------------------------------------------------
it_layout_ck:
    mov ax, [it_cw]
    cmp ax, [it_lcw]
    jne it_layout
    mov ax, [it_ch]
    cmp ax, [it_lch]
    jne it_layout
    cmp byte [it_laid], 0
    je it_layout
    ret

; -----------------------------------------------------------------------------
; it_layout - the band: cleared, the text lines composed, the grid walked
; -----------------------------------------------------------------------------
it_layout:
    mov ax, [it_cw]
    mov [it_lcw], ax
    mov bx, [it_ch]
    mov [it_lch], bx
    mov byte [it_laid], 1
    sub ax, IT_OXPAD                ; the band starts up to 7 px in (it_ox),
    jnc .wpad                       ; so it is 7 px narrower than the content
    xor ax, ax                      ; whatever the drag phase: no relayout is
.wpad:                              ; owed when only the window moved
    cmp ax, IT_BSTRIDE * 8
    jbe .w
    mov ax, IT_BSTRIDE * 8
.w:
    mov [it_bw], ax                 ; the band's width in px: the content's,
    cmp bx, IT_BROWS                ; to 640 (wider content is filled once)
    jbe .h
    mov bx, IT_BROWS
.h:
    mov [it_bh], bx
    mov es, [it_seg]
    mov cx, bx                      ; clear the band's rows: (rows * 80) / 2
    mov ax, IT_BSTRIDE / 2          ; words
    mul cx
    mov cx, ax
    xor di, di
    xor ax, ax
    cld
    rep stosw
    mov byte [it_layok], 0
    cmp word [it_cw], IT_NEEDW
    jb .small
    cmp word [it_ch], IT_NEEDH
    jb .small
    mov byte [it_layok], 1
    call it_dbg_grid
    mov byte [it_hudd], 1
    mov byte [it_msgd], 1
    call it_compose_text
    jmp short .out
.small:
    mov si, it_s_small              ; "Window too small." - in the band, so the
    mov di, IT_HUDY * IT_BSTRIDE    ; same single blit paints it
    call it_text
.out:
    mov byte [it_full], 1
    ret

; -----------------------------------------------------------------------------
; it_dbg_grid - walk the whole grid into the band: one bit a cell, d = 100 at
;               the top. Clears the grid's 101 rows first. ES = the claim.
; MEASURED on MartyPC's 5150: 2,136,141 cycles, ~97 a cell, 448 ms - so it is
; a RELAYOUT's alone (the grid is arbitrary there), never a round start's
; (it_dbg_ring) and never a step's.
; -----------------------------------------------------------------------------
it_dbg_grid:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    mov di, IT_GY * IT_BSTRIDE
    mov cx, GH * IT_BSTRIDE / 2
    xor ax, ax
    cld
    rep stosw
    mov ax, es
    mov ds, ax                      ; DS = ES = the claim for the walk
    mov si, IT_GRID + (GH - 1) * GW ; d = 100 first: the top row
    mov di, IT_GY * IT_BSTRIDE + IT_GX / 8
    mov dx, GH
.row:
    push si
    push di
    mov cx, GW
    mov bl, 0x80                    ; the bit a cell lands on
    xor bh, bh                      ; ...and the byte it is building
.cell:
    lodsb
    or al, al
    jz .nob
    or bh, bl
.nob:
    shr bl, 1
    jnz .more
    mov [es:di], bh
    inc di
    mov bl, 0x80
    xor bh, bh
.more:
    loop .cell
    mov [es:di], bh                 ; the last partial byte (217 = 27*8 + 1)
    pop di
    pop si
    sub si, GW
    add di, IT_BSTRIDE
    dec dx
    jnz .row
    pop ds
    mov word [it_dmin], IT_GY
    mov word [it_dmax], IT_GY + GH - 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_dbg_ring - the debug view of a FRESH grid (a round start): the band's
;               grid rows cleared and the outline set directly - two full rows
;               and a bit each side of the 99 between - instead of walking
;               21,917 cells. The start cells are it_dbg_cell's. ES = the
;               claim. Preserves everything. ~4,040 word stores and ~250 byte
;               writes, against it_dbg_grid's 2.14M cycles under the lock.
; -----------------------------------------------------------------------------
%if IT_GX & 7
%error "it_dbg_ring assumes the grid starts on a band byte"
%endif
it_dbg_ring:
    cmp byte [it_layok], 0
    je .out
    push ax
    push cx
    push di
    cld
    mov di, IT_GY * IT_BSTRIDE
    mov cx, GH * IT_BSTRIDE / 2
    xor ax, ax
    rep stosw
    mov al, 0xFF
    mov di, IT_GY * IT_BSTRIDE + IT_GX / 8          ; d = 100, every u
    mov cx, GW / 8
    rep stosb
    mov byte [es:di], (0xFF00 >> (GW & 7)) & 0xFF
    mov di, (IT_GY + GH - 1) * IT_BSTRIDE + IT_GX / 8   ; d = 0, every u
    mov cx, GW / 8
    rep stosb
    mov byte [es:di], (0xFF00 >> (GW & 7)) & 0xFF
    mov di, (IT_GY + 1) * IT_BSTRIDE                ; u = 0 and u = 216
    mov cx, GH - 2
.side:
    or byte [es:di + IT_GX / 8], 0x80
    or byte [es:di + (IT_GX + GW - 1) / 8], 0x80 >> ((IT_GX + GW - 1) & 7)
    add di, IT_BSTRIDE
    loop .side
    mov word [it_dmin], IT_GY
    mov word [it_dmax], IT_GY + GH - 1
    pop di
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_dbg_cell - one cell's bit, after a move. in: BX = the head record (its
;               H_U / H_D are the cell). ES = the claim. Preserves everything.
; MEASURED ~463 cycles on MartyPC's 5150 (the review's bracket), not the
; ~110 first pinned: the min/max compares and the shift-multiply are fetch-
; bound. Wave 1's alone - wave 2 deletes the view.
; -----------------------------------------------------------------------------
it_dbg_cell:
    cmp byte [it_layok], 0
    je .out
    push ax
    push cx
    push di
    mov al, GH - 1 + IT_GY
    sub al, [bx + H_D]              ; the band row: IT_GY + 100 - d
    xor ah, ah
    cmp ax, [it_dmin]
    jae .lo
    mov [it_dmin], ax
.lo:
    cmp ax, [it_dmax]
    jbe .hi
    mov [it_dmax], ax
.hi:
    mov cl, 4
    shl ax, cl                      ; row * 16
    mov di, ax
    shl ax, 1
    shl ax, 1                       ; row * 64
    add di, ax                      ; row * 80
    mov al, [bx + H_U]
    add al, IT_GX
    xor ah, ah
    mov cl, al
    and cl, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    mov al, 0x80
    shr al, cl
    or [es:di], al
    pop di
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_text - compose one 8-row text line into the band. in: SI = a NUL string
;           (DS), DI = the band offset of the line's first cell (row * 80 +
;           byte). ES = the claim. The line's 8 rows are cleared across the
;           band's full width first. Clobbers AX, CX, SI, DI.
; -----------------------------------------------------------------------------
it_text:
    push bx
    push dx
    push di
    mov ax, di                      ; the row's start: DI - (DI mod 80)
    xor dx, dx
    mov bx, IT_BSTRIDE
    div bx
    mul bx
    mov di, ax
    mov cx, 8 * IT_BSTRIDE / 2
    xor ax, ax
    cld
    rep stosw
    pop di
    mov dx, IT_TEXTN
.ch:
    lodsb
    or al, al
    jz .done
    dec dx
    js .done                        ; ...and no further than the line holds
    cmp al, [it_gfirst]
    jb .blank
    cmp al, [it_glast]
    ja .blank
    sub al, [it_gfirst]
    xor ah, ah
    mov cl, 3
    shl ax, cl                      ; 8 bytes a glyph
    add ax, [it_goff]
    push si
    push ds
    mov si, ax
    mov ds, [it_gseg]
    mov cx, 8
    mov bx, di
.gl:
    mov al, [si]
    mov [es:bx], al
    inc si
    add bx, IT_BSTRIDE
    loop .gl
    pop ds
    pop si
.blank:
    inc di
    jmp short .ch
.done:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_compose_text - the HUD and message lines, when they are owed
; -----------------------------------------------------------------------------
it_compose_text:
    cmp byte [it_layok], 0
    je .out
    push ax
    push cx
    push si
    push di
    cmp byte [it_hudd], 0
    je .msg
    call it_hud_str
    mov si, it_hudbuf
    mov di, IT_HUDY * IT_BSTRIDE + IT_GX / 8
    call it_text
.msg:
    cmp byte [it_msgd], 0
    je .done
    call it_msg_str                 ; SI = the state's line
    mov di, IT_MSGY * IT_BSTRIDE + IT_GX / 8
    call it_text
.done:
    pop di
    pop si
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_hud_str - "PLAYER n   COMPUTER n   TOWER h   Rookie   SPEED p/6"
; -----------------------------------------------------------------------------
it_hud_str:
    push ax
    push si
    push di
    mov di, it_hudbuf
    mov si, it_s_player
    call it_strcpy
    mov ax, [it_sp]
    call it_putdec
    mov si, it_s_comp
    call it_strcpy
    mov ax, [it_sc]
    call it_putdec
    mov si, it_s_tower
    call it_strcpy
    mov al, [it_h]
    xor ah, ah
    call it_putdec
    mov si, it_s_sep
    call it_strcpy
    mov al, [it_tier]
    xor ah, ah
    dec ax
    shl ax, 1
    mov si, ax
    mov si, [it_tiername + si]
    call it_strcpy
    mov si, it_s_speed
    call it_strcpy
    mov al, [it_pspeed]
    xor ah, ah
    call it_putdec
    mov si, it_s_sixth
    call it_strcpy
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_msg_str - out: SI = the line the state wants under the grid
; -----------------------------------------------------------------------------
it_msg_str:
    mov si, it_s_paused
    cmp byte [it_paused], 0
    jne .out
    mov si, it_s_title
    cmp byte [it_state], ST_TITLE
    je .out
    mov si, it_s_play
    cmp byte [it_state], ST_PLAY
    je .out
    mov si, it_s_over
    cmp byte [it_state], ST_OVER
    je .out
    mov si, it_s_pwin
    cmp byte [it_winner], WIN_PLAYER
    je .out
    mov si, it_s_cwin
.out:
    ret

; -----------------------------------------------------------------------------
; it_present - the worker's present: lock held. Re-checks visibility and arms
;              the clip (SPEC.md 20.6 rule 5), then puts down what is owed.
;
; NOTHING OWED, NOTHING ASKED: a wake with no dirty row, no text line and no
; full repaint owed returns before the clip, the content box and the layout
; check - MEASURED at 12.5k of a 13.2k-cycle idle wake (the title, a pause,
; WAIT, OVER: ~5% of an XT, 18 times a second). It is safe because nothing
; that needs the glass to follow arrives without one of the four: a resize
; sets it_full (it_onresize), an expose or a move comes through it_paint,
; and a hidden window's skip below sets it_full.
; -----------------------------------------------------------------------------
it_present:
    cmp byte [it_abon], 0
    jne .out                        ; the card is up: it owns its rect
    cmp byte [it_full], 0
    jne .owed
    cmp byte [it_hudd], 0
    jne .owed
    cmp byte [it_msgd], 0
    jne .owed
    mov ax, [it_dmax]
    cmp ax, [it_dmin]
    jb .out                         ; no grid row either: not a call made
.owed:
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [it_win]
    test word [es:bx + W_FLAGS], 2
    jz .skip
    call OSAPI_WM_CLIP_SET
    jc .skip                        ; not one pixel of us is showing
    call it_geom
    jc .skip
    call it_layout_ck               ; the box changed size under us
    call it_present_now
    jmp short .out
.skip:
    mov byte [it_full], 1           ; the glass did not follow: owe it all
.out:
    ret

; -----------------------------------------------------------------------------
; it_present_now - put the owed rows down. A clip is armed (the paint's or
; it_present's). Clobbers everything but DS.
;
; WHAT A WAKE COSTS, MEASURED on MartyPC's 5150 (the W1 review): a paced
; Rookie h1 wake is 104.9k cycles median (~22 ms), not the ~36k first
; estimated, because the dirty range is the UNION of every head's rows - the
; player near d = 27 and the computer near d = 90 make it ~60 rows a wake,
; each blitted 217 px (28 bytes) wide. The text lines are one 8-row blit
; each, and only when they changed. Wave 1's alone: wave 2 replaces it.
; -----------------------------------------------------------------------------
it_present_now:
    mov es, [it_seg]
    call it_compose_text
    cmp byte [it_full], 0
    je .part
    ; --- the whole band, and whatever of the content lies outside it --------
    mov byte [it_full], 0
    xor ax, ax
    mov cx, [it_bh]
    mov dx, [it_bw]
    xor bx, bx
    call it_blit_rows               ; rows 0..bh-1, the full band width
    call it_fill_rest
    jmp short .clean
.part:
    cmp byte [it_layok], 0
    je .clean                       ; too small: nothing of it to show, and
                                    ; nothing left owed for the early-out
    cmp byte [it_hudd], 0
    je .msg
    mov ax, IT_HUDY
    mov cx, 8
    mov dx, [it_bw]
    xor bx, bx
    call it_blit_rows
.msg:
    cmp byte [it_msgd], 0
    je .grid
    mov ax, IT_MSGY
    mov cx, 8
    mov dx, [it_bw]
    xor bx, bx
    call it_blit_rows
.grid:
    mov ax, [it_dmin]
    mov cx, [it_dmax]
    cmp cx, ax
    jb .clean                       ; nothing moved
    sub cx, ax
    inc cx
    mov dx, GW                      ; the grid's own columns, not the band's
    mov bx, IT_GX / 8
    call it_blit_rows
.clean:
    mov byte [it_hudd], 0
    mov byte [it_msgd], 0
    mov word [it_dmin], 0xFFFF
    mov word [it_dmax], 0
.out:
    ret

; -----------------------------------------------------------------------------
; it_blit_rows - band rows [AX, AX+CX) to the glass, DX px wide from band
;                byte BX. ES = the claim. 255 rows a call (gfx_blit1's limit).
; Clobbers AX, BX, CX, DX, SI, BP.
; -----------------------------------------------------------------------------
it_blit_rows:
    push di
    mov [it_bx0], bx
    mov [it_bwid], dx
.chunk:
    jcxz .out
    mov di, cx
    cmp di, 255
    jbe .n
    mov di, 255                     ; DI = this call's rows
.n:
    push ax
    push cx
    mov bx, ax                      ; the first row
    mov dx, IT_BSTRIDE
    mul dx
    add ax, [it_bx0]
    mov si, ax                      ; SI = row * 80 + the first byte
    add bx, [it_cy]                 ; BX = y
    mov ax, [it_bx0]
    mov cl, 3
    shl ax, cl
    add ax, [it_ox]                 ; AX = x: the band's origin, ON the byte
                                    ; grid, + the byte's px
    mov cx, [it_bwid]               ; CX = width
    mov dx, di                      ; DX = rows
    mov bp, IT_BSTRIDE
    call OSAPI_GFX_BLIT1            ; CF = refused: the arguments are ours and
    pop cx                          ; legal, and a debug view has no second
    pop ax                          ; path to fall back to
    add ax, di
    sub cx, di
    jmp short .chunk
.out:
    pop di
    ret

; -----------------------------------------------------------------------------
; it_fill_rest - the content outside the band, filled once in black: the
;                0..7 columns left of it_ox, the columns right of the band
;                (past 640 on a Hercules, and the 7 - pad the narrowing
;                leaves) and the rows past 400. Clobbers AX..DX.
; -----------------------------------------------------------------------------
it_fill_rest:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [it_cx]                 ; the left strip: x = cx .. ox-1
    mov cx, [it_ox]
    cmp cx, ax
    jbe .right                      ; the content is on the grid already
    dec cx
    mov bx, [it_cy]
    mov dx, bx
    add dx, [it_bh]
    dec dx
    call OSAPI_GFX_FILL
.right:
    mov ax, [it_ox]                 ; the right strip: x = ox + bw ..
    add ax, [it_bw]                 ; cx + cw - 1, all bh rows
    mov cx, [it_cx]
    add cx, [it_cw]
    cmp ax, cx
    jae .rows                       ; the band reaches the right edge
    dec cx
    mov bx, [it_cy]
    mov dx, bx
    add dx, [it_bh]
    dec dx
    call OSAPI_GFX_FILL
.rows:
    mov ax, [it_ch]
    cmp ax, [it_bh]
    jbe .out
    mov ax, [it_cx]                 ; the bottom strip: every column
    mov bx, [it_cy]
    add bx, [it_bh]
    mov cx, [it_cx]
    add cx, [it_cw]
    dec cx
    mov dx, [it_cy]
    add dx, [it_ch]
    dec dx
    call OSAPI_GFX_FILL
.out:
    ret

; -----------------------------------------------------------------------------
; small string helpers. it_strcpy: SI -> DI, without the NUL (DI is left on
; it). it_putdec: AX as decimal at DI. Both preserve all but DI and SI.
; -----------------------------------------------------------------------------
it_strcpy:
    push ax
.lp:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short .lp
.out:
    pop ax
    ret

it_putdec:
    push ax
    push bx
    push dx
    push si
    mov si, it_decbuf + 5           ; the digits are made backwards into a
    mov bx, 10                      ; buffer, so no push is left in a loop
.dv:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .dv
.em:
    mov al, [si]
    mov [di], al
    inc di
    inc si
    cmp si, it_decbuf + 5
    jb .em
    pop si
    pop dx
    pop bx
    pop ax
    ret

%include "itrules.inc"
%include "itai.inc"
%include "itinput.inc"

; =============================================================================
; TABLES
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) --------------------------
it_tpl:
    dw 0, 30, 638, 420
    dw it_ttl, it_paint, it_onkey, it_onclick

; --- what we ask for on each adapter (SPEC.md 11.100.1): the frame sizes the
; 3D picture of wave 2 is cut from (ISOTRON-PLAN 7.1); the kernel clamps
    OS88_PREFER it_pref, 638, 420, 718, 300, 638, 200

; --- the app menu set (SPEC.md 12.2) ------------------------------------------
    OS88_MENUSET it_menus, it_name, it_oncmd
        OS88_MENU it_m_game, it_i_game, 8
    OS88_MENUSET_END it_menus

it_name:     db 'isoTron', 0
it_ttl:      db 'isoTron', 0
it_m_game:   db 'Game', 0
it_i_game:   dw it_it_rook, it_it_vet, it_it_lun, it_it_pause
             dw it_it_snd, it_it_col, it_it_full, it_it_end
it_it_rook:  db 'Rookie', 0
it_it_vet:   db 'Veteran', 0
it_it_lun:   db 'Lunatic', 0
it_it_pause: db 'Pause', 0
; GREYED WITH THEIR REASON (SPEC.md 47 rule 3): the three are real items of
; the finished game and arrive with the waves that build them
it_it_snd:   db MENU_DIS, 'Sound (not yet)', 0
it_it_col:   db MENU_DIS, 'Colours (not yet)', 0
it_it_full:  db MENU_DIS, 'Full Screen (not yet)', 0
it_it_end:   db 'End Game', 0

; --- the About card: the name, what it is, the keys. No credit line, no
; legal line and no year (SPEC.md 108.1)
it_ablines:
    dw it_ab1, it_ab2, it_ab3, it_ab4, it_ab5, 0
it_ab1:      db 'isoTron', 0
it_ab2:      db 0
it_ab3:      db 'A light-wall duel in three dimensions.', 0
it_ab4:      db 'Ins/Del or the arrows turn. F10 faster.', 0
it_ab5:      db '1, 2 or 3 starts a game; P pauses.', 0

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"

; --- strings ------------------------------------------------------------------
it_tiername: dw it_it_rook, it_it_vet, it_it_lun
it_s_player: db 'PLAYER ', 0
it_s_comp:   db '   COMPUTER ', 0
it_s_tower:  db '   TOWER ', 0
it_s_sep:    db '   ', 0
it_s_speed:  db '   SPEED ', 0
it_s_sixth:  db '/6', 0
it_s_title:  db '1 Rookie, 2 Veteran, 3 Lunatic (A, B, C: silent)', 0
it_s_play:   db 'Ins/Del or Left/Right turn. F10 faster, F9 normal.', 0
it_s_paused: db 'PAUSED - PRESS P', 0
it_s_pwin:   db 'The player wins the round. Space to continue.', 0
it_s_cwin:   db 'The computer wins the round. Space to continue.', 0
it_s_over:   db 'THREE LOSSES IN A ROW. GAME OVER.', 0
it_s_small:  db 'Window too small.', 0
it_s_nomem1: db 'isoTron needs ', 0
it_s_nomem2: db ' KB of free memory; this machine has ', 0
it_s_nomem3: db ' KB.', 0

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes IT_BSS bytes after the image, and every
; name below is an offset from os88_image_end)
; =============================================================================
%assign IT_BSS 0
%macro WORDV 1
%1 equ os88_image_end + IT_BSS
%assign IT_BSS IT_BSS + 2
%endmacro
%macro BYTEV 1
%1 equ os88_image_end + IT_BSS
%assign IT_BSS IT_BSS + 1
%endmacro
%macro BUFV 2
%1 equ os88_image_end + IT_BSS
%assign IT_BSS IT_BSS + (%2)
%endmacro

; --- the window and the surface ------------------------------------------------
    WORDV it_win
    WORDV it_seg                    ; the claim
    BYTEV it_nomem                  ; ...which was refused
    BYTEV it_spawned
    BYTEV it_abon
    WORDV it_cx                     ; the content box, absolute
    WORDV it_ox                     ; the band's origin: cx up to the grid
    WORDV it_cy
    WORDV it_cw
    WORDV it_ch
    WORDV it_lcw                    ; ...as it was when the band was laid out
    WORDV it_lch
    BYTEV it_laid
    BYTEV it_layok                  ; the view fits
    WORDV it_bw                     ; the band: px wide, rows
    WORDV it_bh
    BYTEV it_full                   ; the whole band is owed
    BYTEV it_hudd                   ; the HUD line changed
    BYTEV it_msgd                   ; ...the message line
    WORDV it_dmin                   ; the grid rows dirtied since the present
    WORDV it_dmax
    WORDV it_bx0                    ; it_blit_rows' scratch
    WORDV it_bwid
    WORDV it_gseg                   ; the 8x8 glyph table
    WORDV it_goff
    BYTEV it_gfirst
    BYTEV it_glast
    BUFV  it_hudbuf, 96
    BUFV  it_msgbuf, 96
    BUFV  it_decbuf, 6              ; it_putdec's five digits

; --- the game --------------------------------------------------------------------
    BYTEV it_state
    BYTEV it_tier                   ; 1..3
    BYTEV it_snd                    ; sound wanted (the tones are wave 5's)
    BYTEV it_paused
    WORDV it_tend                   ; a timed state's end, in ticks
    WORDV it_lastt                  ; the governor's last tick
    WORDV it_debt                   ; now - V, in u (signed; SPEC.md 108.4.2)
    WORDV it_A                      ; the tier's cost and the computer's
    WORDV it_B                      ; parameters (it_tiers)
    WORDV it_C
    BYTEV it_look
    BYTEV it_icpt
    BYTEV it_wander
    BYTEV it_nheads
    BYTEV it_opener                 ; the head the opening rule is for
    BYTEV it_live                   ; computer heads alive (a running count)
    WORDV it_costb                  ; A + B * it_live, kept with it
    WORDV it_cost                   ; this iteration's cost, built as it runs
    BYTEV it_pspeed                 ; the player's speed, sixths
    BYTEV it_pacc
    BYTEV it_inboost                ; the iteration's keys
    BYTEV it_inf9
    BYTEV it_r                      ; the AI's RNG byte
    BYTEV it_n                      ; ...and the head it is for
    BYTEV it_side
    WORDV it_t                      ; the intercept's t
    BUFV  it_heads, (MAXHEADS + 1) * H_SIZE ; four computer heads, the player
IT_PLAYER equ it_heads + MAXHEADS * H_SIZE

; --- the UI task's mailbox (itinput.inc: single producer, single consumer) ------
    BUFV  it_fifo, 4                ; the turn ring
    BYTEV it_fhead                  ; written by the producer only
    BYTEV it_ftail                  ; written by the consumer only
    BYTEV it_kboost                 ; F10s not yet taken (inc / sub: one RMW
    BYTEV it_kf9                    ; instruction each, so neither can tear)
    BYTEV it_qstart                 ; a game asked for: tier | 0x80 sound
    BYTEV it_qpause
    BYTEV it_qhold                  ; pause if in play (the About card)
    BYTEV it_qesc
    BYTEV it_qcont

%include "ittest.inc"

    OS88_BSS IT_BSS
    OS88_IMAGE_END
