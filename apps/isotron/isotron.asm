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
; --- WAVE 2 (docs/plans/ISOTRON-PLAN.md section 19) ---------------------------
; The rules engine of wave 1 in its real shell, now DRAWING: the slab, the
; tower and every wall in a 320 x 200 canvas of surface ids (itcanvas.inc,
; SPEC.md 108.9 - 108.10: one column a step, at most 22 probes, nothing
; redrawn), presented in a desktop window on every adapter (itwin.inc,
; SPEC.md 108.11: 2x across, a row map down, mono through OSAPI_GFX_BLIT1 and
; 16 colours through OSAPI_GFX_BLITP, one lock hold a wake). Full screen is
; wave 3's, the round flow's sweep and the title text wave 4's.
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
; normal; P pauses; Space continues; Esc ends the game (and at the title,
; leaves full screen); F / Alt+Enter toggle full screen; C the colours. At
; the title 1, 2, 3 start a game with sound and A, B, C a silent one.
;
; --- WAVE 3 -------------------------------------------------------------------
; FULL SCREEN (itfs.inc, SPEC.md 108.12): the same canvas through a colour
; presenter for the window's display - CGA 320 x 200 palette 1, VGA 0Dh in
; 16 colours, Hercules 1bpp at 2 x 1.5, EGA's own 640 x 350 x 16 in a same-
; mode bracket - with a step's one column written straight into video
; memory, and the game run by the bracket's own loop on the sub-tick clock.
;
; --- WAVE 5 -------------------------------------------------------------------
; THE SOUND (itsnd.inc, SPEC.md 108.14.1): the speaker through OSAPI_SND_TONE,
; one writer after the measured work - the wall's tick, the crash's falling
; sweep, READY's beeps, the tower's chime, GAME OVER's notes - silent on every
; way out. THE KEYS (itinput.inc, 108.14.2): a held turn key is one turn, a
; double tap two. THE PACE (108.14.3) measured on the 5150 against the model.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'isoTron', it_entry, 1, OS88_STACK_192
                                ; the worker's stack class (SPEC.md 8.7):
                                ; tools/stkdepth.py prices it_worker's
                                ; deepest static chain at 40 bytes (a
                                ; band's boxes, presented in colour, wave
                                ; 2); with the 64-byte interrupt floor
                                ; that is 104 of 192, 1.85x. t_stkclass
                                ; holds the bar at 1.25x

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

; --- the instance's two claims (SPEC.md 108.8) --------------------------------
; TWO OSAPI_MEM_CLAIMs, pinned. THE CANVAS (it_cseg) is a segment of its own:
; one byte a pixel, 64,000 bytes (itcanvas.inc says why: a wall row is then
; ONE store, where a packed nibble is a read-modify-write - ~800 cycles a
; step). THE WORK CLAIM (it_seg) holds the grid, the presenters' strip and
; their tables, so a presenter's lookups and stores need no override and only
; its canvas reads carry one (itwin.inc).
IT_GRID     equ 0               ; 21,917: 217 x 101, one byte a cell. No
                                ; read reaches below it or past it: every
                                ; run, look and move stops AT the outline ring
                                ; (wave 2's grid probe, which read below, is
                                ; gone - k* comes off the canvas, 108.10.1)
IT_STRIP    equ 21920           ; 5,120: the presenters' strip, on a 16-byte
IT_STRIPSZ  equ 5120            ; boundary (21,917 rounded up)
IT_MLUT     equ 27648           ; 1,024: the mono tables, four 256-byte PAGES
                                ; (HI and LO for each row parity), 1,024-
                                ; ALIGNED: a lookup is `mov bl, b / mov al,
                                ; [bx]`, and the next row's pair of pages is
                                ; `xor bh, 2` (itwin.inc it_praw1)
IT_TA01     equ IT_MLUT + 1024  ; 2,048: the colour tables, four of 256 WORDS:
IT_TA23     equ IT_TA01 + 512   ; planes 0-1 and 2-3 of a canvas byte as the
IT_TB01     equ IT_TA23 + 512   ; left (TA) or the right (TB) half of a
IT_TB23     equ IT_TB01 + 512   ; device byte
IT_CLAIMEND equ IT_TB23 + 512
IT_CLAIMKB  equ (IT_CLAIMEND + 1023) / 1024
IT_CANVKB   equ (64000 + 1023) / 1024   ; the canvas claim: 63 KB
%if IT_CLAIMEND > 65535
  %error "isoTron's work claim passes 64 KB - one segment cannot address it"
%endif
%if (IT_STRIP < IT_GRID + 21917) || (IT_STRIP & 15) || (IT_MLUT < IT_STRIP + IT_STRIPSZ) || (IT_MLUT & 1023)
  %error "isoTron's work claim overlaps, the strip is not 16-byte aligned or the mono tables are not 1,024-aligned"
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
H_U         equ 2               ; (it_span reads u and d as one word:
H_D         equ 3               ; H_D must follow H_U)
H_HD        equ 4
H_ALIVE     equ 5
H_LEG       equ 6               ; cells moved since the last turn
H_TURNED    equ 7               ; turned at all this round
H_UPEND     equ 8               ; a U-turn armed: moves until it fires
H_USIDE     equ 9               ; ...and its side: 0 left, 1 right
H_BX0       equ 10              ; word: the head's DIRTY BOX this wake, in
H_BX1       equ 12              ; logical px - columns BX0..BX1 (BX0 > BX1:
H_BY0       equ 14              ; empty), rows BY0 .. BY1 - 1 (itcanvas.inc
H_BY1       equ 15              ; it_span grows it, itwin.inc presents it)
H_BMV       equ 16              ; byte: the head moved since the box went out
H_SIZE      equ 18

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
ST_SWEEP    equ 6               ; the new arena going onto the glass (itsweep.inc)
ST_READY    equ 7               ; ...and the 0.5 s before the walls move
WIN_NONE    equ 0
WIN_PLAYER  equ 1
WIN_COMP    equ 2
IT_CRASHT   equ 15              ; 0.8 s of CRASH, in ticks
IT_HUDT     equ 5               ; ...the score changes 0.27 s into it
IT_OVERT    equ 64              ; 3.5 s of GAME OVER
IT_READYT   equ 9               ; 0.5 s of READY
IT_BQ_WAIT  equ 1               ; it_bldq: WAIT's next arena (it_sw_build)
IT_BQ_TITLE equ 2               ; ...the title's picture (it_title_build)
IT_BQ_ROUND equ 3               ; ...a game's first arena, then its round
IT_BQ_BUSY  equ 0x80            ; ...being built, outside the lock

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
    cmp ax, IT_CANVKB               ; claim that fails SHEDS the disk caches on
    jb .nomem                       ; its way down, and kern_small - the
    mov ax, IT_CANVKB               ; machine this refusal is for - would pay
    call OSAPI_MEM_CLAIM            ; that on every launch. The CF test stays:
    jc .nomem                       ; the answer is the deliverable, not a vow
    mov [it_cseg], dx               ; THE CANVAS, then the work claim
    call OSAPI_MEM_AVAIL
    cmp ax, IT_CLAIMKB
    jb .nomem2
    mov ax, IT_CLAIMKB
    call OSAPI_MEM_CLAIM
    jc .nomem2
    mov [it_seg], dx
    mov bx, [it_win]
    mov al, 1
    call OSAPI_WM_OWNBG             ; the picture is every pixel of the content
    mov word [it_spanhk], it_span_box ; a step's tail: windowed (itfs.inc)
    mov word [it_lookp], it_col     ; the isoTron look, until C
    push es
    mov es, [it_seg]
    call it_canvas_init             ; the row bases
    call it_lut_build               ; the presenters' tables, in the claim
    call it_title_build             ; the title's picture, before the first
    call it_box_clear               ; paint shows anything
    pop es
    mov byte [it_full], 1
    jmp short .ok
.nomem2:
    xor dx, dx                      ; half a game is no game: the canvas back,
    xchg dx, [it_cseg]              ; and the word zeroed with it - a freed
    call OSAPI_MEM_FREE             ; segment still held reads as a claim
.nomem:
    call it_nomem_msg               ; "isoTron needs 63 KB and 30 KB ..."
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
    mov ax, IT_CANVKB
    call it_putdec
    mov si, it_s_nomem1b
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
    call it_paint_win               ; itwin.inc: the damage, from the canvas
    call it_fs_menu                 ; the items that depend on the display
    cmp byte [it_abon], 0
    je .done
    mov bx, [it_win]
    mov si, it_ablines
    call os88ui_about_d             ; the card LAST, over its own rect
    jmp short .done
.nomem:
    ; NO CLAIM, SO NO PICTURE: the kernel filled the content white (no OWNBG
    ; on this path) and one opaque run letters the refusal over it
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
    cmp al, IT_MI_PAUSE
    je .pause
    cmp al, IT_MI_COL
    je .look                        ; Colours: C's look (itfs.inc)
    cmp al, IT_MI_FULL
    je .full                        ; Full Screen: F's bracket (itfs.inc)
    cmp al, IT_MI_SND
    je .snd                         ; Sound: M's toggle, posted
    cmp al, IT_MI_END
    jne .out                        ; (a rule is greyed: never picked)
    mov byte [it_qesc], 1           ; End Game
    jmp short .out
.snd:
    mov byte [it_qsnd], 1
    jmp short .out
.look:
    call it_look_key
    jmp short .out
.full:
    call it_fs_go                   ; a menu handler: the UI task, the lock
    jmp short .out                  ; held - OSAPI_FSX_RUN's own contract
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

; it_abdismiss - the card down, and THE CARD'S RECT put back from the canvas
; here and now (itwin.inc it_paint_rect, with a clip armed for it as
; os88ui_about arms one): the picture under it is the canvas, so a whole
; present would redraw ~40 times the pixels it covered (rule 1). Called from
; the callbacks, the lock held; preserves every register.
it_abdismiss:
    cmp byte [it_abon], 0
    je .none
    mov byte [it_abon], 0
    cmp byte [it_nomem], 0
    jne .none
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bx, [it_win]
    call OSAPI_WM_CLIP_SET
    jc .x                           ; not one pixel shows: the uncover paints
    call it_geom
    jc .x
    call it_layout_ck
    cmp byte [it_layok], 0
    je .small
    call it_surf_wake
    call it_paint_pen
    call it_abrect                  ; AX, BX, CX, DX: the card, as os88ui drew it
    call it_paint_rect
    jmp short .x
.small:
    call it_draw_small
.x:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

; it_abrect - the About card's rect, inclusive and absolute, measured exactly
; as os88ui_about_d measures it (SPEC.md 20.5.1): the widest line plus the
; side margins and the lines' pitch plus the top and bottom ones, each
; clamped to the content box, centred in it. it_geom first.
; out: AX, BX, CX, DX. Clobbers SI, DI, BP.
it_abrect:
    mov si, it_ablines
    xor bp, bp                      ; BP = the widest line, px
    mov di, OS88UI_ABPADY * 2       ; DI = the card's height
.m:
    lodsw
    or ax, ax
    jz .md
    push si
    xchg si, ax
    call OSAPI_FONT_WIDTH           ; AX = its pixels
    pop si
    add di, OS88UI_ABLH
    cmp ax, bp
    jbe .m
    mov bp, ax
    jmp short .m
.md:
    cmp di, [it_ch]
    jbe .h
    mov di, [it_ch]
.h:
    add bp, OS88UI_ABPADX * 2
    cmp bp, [it_cw]
    jbe .w
    mov bp, [it_cw]
.w:
    mov ax, [it_cw]                 ; left = cx + (cw - w) / 2
    sub ax, bp
    shr ax, 1
    add ax, [it_cx]
    mov bx, [it_ch]                 ; top = cy + (ch - h) / 2
    sub bx, di
    shr bx, 1
    add bx, [it_cy]
    mov cx, ax
    add cx, bp
    dec cx
    mov dx, bx
    add dx, di
    dec dx
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
    cmp byte [it_byield], 0         ; A WHOLE PRESENT GOING OUT IN BANDS
    jne .yield                      ; (itwin.inc it_present_band): the next
    test byte [it_tflags], IT_TF_FAST ; band at once, the lock released and
    jz .sleep                       ; every other task given its turn between
    cmp byte [it_state], ST_PLAY    ; FAST, and a round to play: no sleep -
    je .yield                       ; nor in the harness's NOFLOW step to the
    cmp byte [it_state], ST_NEXT    ; next round; every other state is paced
    jne .sleep                      ; on the ticks, a sweep's above all
.yield:
    call OSAPI_TASK_YIELD
    jmp short .wake
.sleep:
    mov ax, 1
    call OSAPI_TASK_SLEEP
.wake:
    call it_focus_ck                ; no lock: WM_TOP and MENU_OWNER take none
    IT_MARK it_wake0                ; (a wake's price includes the lock: the
    call OSAPI_GFX_LOCK             ; pointer comes off and goes back there)
    call it_wake
    call OSAPI_GFX_UNLOCK
    IT_MARK it_wake1
    call it_bld_run                 ; a canvas build owed: here, OUTSIDE the
    IT_MARK it_frame_end            ; lock (itrules.inc, SPEC.md 108.13.2)
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
    jne .nm
    call it_tsample                 ; the turn key's level (itinput.inc 108.14.2)
    call it_wake_run
    jmp it_snd_frame                ; THE SOUND, after the measured work: the
.nm:                                ; governor has read the hold (itsnd.inc)
    ret

; it_wake_run - the logic and the present, and the presentation governor.
; Clobbers everything but DS.
it_wake_run:
    mov es, [it_seg]                ; ES = the claim for the whole of the logic
    call pit_now                    ; the hold starts (os88pit.inc, 838 ns)
    mov [it_gt0], ax
    mov [it_gt0 + 2], dx
    call it_commands                ; what the UI task asked for (itinput.inc)
    call it_flow                    ; the state machine and the pace governor
    cmp byte [it_bldq], 0           ; (a canvas build owed: it is built after
    jne .out                        ; this hold, and the next wake presents it)
    call it_present                 ; ...and what changed, to the glass
    cmp byte [it_gpres], 0
    je .out
    ; --- THE PRESENTATION GOVERNOR (SPEC.md 108.11.6) -------------------------
    ; A hold over 45 ms presents every second wake from then on - the logic
    ; still runs every wake, and what a skipped wake owes stays in the boxes -
    ; and 32 presenting wakes under 30 ms put it back. An overloaded machine
    ; loses frame rate and keeps the game's pace (108.4.2).
    call pit_now
    sub ax, [it_gt0]
    sbb dx, [it_gt0 + 2]            ; DX:AX = the hold
    cmp byte [it_gskip], 0
    jne .on
    or dx, dx                       ; PRESENTING EVERY WAKE: over 45 ms
    jnz .slow                       ; engages it
    cmp ax, IT_G45
    jbe .out
.slow:
    mov byte [it_gskip], 1
    mov byte [it_gquiet], 0
    ret
.on:
    ; ENGAGED, a present carries TWO wakes of steps, so a wake's share is
    ; half the hold - and it is that share that must be under 30 ms, 32
    ; presents running, to go back: comparing the whole hold with 30 latched
    ; the governor on wherever one wake cost 15 to 30 ms (measured on the
    ; XT-VGA). The hold is under 60 ms when DX:AX < 2 * IT_G30.
    cmp dx, (2 * IT_G30) >> 16
    jb .quiet
    ja .busy
    cmp ax, (2 * IT_G30) & 0xFFFF
    jb .quiet
.busy:
    mov byte [it_gquiet], 0
    ret
.quiet:
    inc byte [it_gquiet]
    cmp byte [it_gquiet], IT_GQUIET
    jb .out
    mov byte [it_gskip], 0
    mov byte [it_gquiet], 0
.out:
    ret

; =============================================================================
; THE HUD AND THE MESSAGE (SPEC.md 108.11.5): the strings the overlays show
; =============================================================================

; -----------------------------------------------------------------------------
; it_hud_str - "PLAYER n" into it_hudl and "COMPUTER n" into it_hudr
; -----------------------------------------------------------------------------
it_hud_str:
    push ax
    push si
    push di
    mov di, it_hudl
    mov si, it_s_player
    call it_strcpy
    mov ax, [it_sp]
    call it_putdec
    mov byte [di], 0
    mov di, it_hudr
    mov si, it_s_comp
    call it_strcpy
    mov ax, [it_sc]
    call it_putdec
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_msg_str - out: SI = the message line the state wants, 0 = none (a round
; in play shows the HUD alone)
; -----------------------------------------------------------------------------
it_msg_str:
    mov si, it_s_paused
    cmp byte [it_paused], 0
    jne .out
    mov si, it_s_title
    cmp byte [it_state], ST_TITLE
    je .out
    xor si, si
    cmp byte [it_state], ST_PLAY
    je .out
    cmp byte [it_state], ST_CRASH   ; the crash itself says nothing: the
    je .out                         ; score changes, then WAIT says who won
    cmp byte [it_state], ST_SWEEP   ; ...and the new arena and READY are the
    jae .out                        ; picture's (SWEEP, READY)
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
%include "itcanvas.inc"
%include "itwin.inc"
%include "itfs.inc"
%include "itsweep.inc"
%include "itsnd.inc"
%include "os88pit.inc"

; =============================================================================
; TABLES
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) --------------------------
it_tpl:
    dw 0, 30, 640, 420
    dw it_ttl, it_paint, it_onkey, it_onclick

; --- what we ask for on each adapter (SPEC.md 11.100.1): a frame SPANNING THE
; DISPLAY, which drops both side borders (11.95.2), so the content is the
; whole width from x = 0 - 640 on VGA, EGA and CGA, 720 on a Hercules - and
; the 2x picture (640 device px, SPEC.md 108.11.2) fits it on the byte grid.
; A 638 frame was content 1..632, eight pixels short. The kernel clamps the
; heights.
    OS88_PREFER it_pref, 640, 420, 720, 300, 640, 200

; --- the app menu set (SPEC.md 12.2) ------------------------------------------
    OS88_MENUSET it_menus, it_name, it_oncmd
        OS88_MENU it_m_game, it_i_game, IT_MI_N
    OS88_MENUSET_END it_menus

it_name:     db 'isoTron', 0
it_ttl:      db 'isoTron', 0
it_m_game:   db 'Game', 0
; THE GAME MENU (ISOTRON-PLAN 9.2): the tiers, then the switches, then the
; end - its separators the kernel's own convention (a greyed rule)
it_i_game:   dw it_it_rook, it_it_vet, it_it_lun, it_it_sep
             dw it_it_snd, it_it_col, it_it_full, it_it_pause
             dw it_it_sep, it_it_end
IT_MI_N      equ ($ - it_i_game) / 2
IT_MI_SND    equ 4                      ; (the items a handler relabels)
IT_MI_COL    equ 5
IT_MI_FULL   equ 6
IT_MI_PAUSE  equ 7
IT_MI_END    equ 9
it_it_sep:   db MENU_DIS, '----------------', 0
it_it_rook:  db 'Rookie', 0
it_it_vet:   db 'Veteran', 0
it_it_lun:   db 'Lunatic', 0
it_it_pause: db 'Pause', 0
; SOUND NAMES ITS STATE (PIXELSTEIN's px_s_sndon: an app menu has no check
; mark), relabelled by itinput.inc it_snd_label wherever it_snd changes.
; GREYED WITH THEIR REASON (SPEC.md 47 rule 3): Colours and Full Screen,
; whose greyed forms itfs.inc it_fs_menu swaps in on every paint when the
; window's display has no colour or no mode
it_it_snd:   db 'Sound: On', 0
it_it_sndo:  db 'Sound: Off', 0
it_it_col:   db 'Colours', 0
it_it_coln:  db MENU_DIS, 'Colours (mono display)', 0
it_it_full:  db 'Full Screen', 0
it_it_fulln: db MENU_DIS, 'Full Screen (no mode)', 0
it_it_end:   db 'End Game', 0

; --- the About card: the name, what it is, the keys. No credit line, no
; legal line and no year (SPEC.md 108.1)
it_ablines:
    dw it_ab1, it_ab2, it_ab3, it_ab4, it_ab5, it_ab6, it_ab7, 0
it_ab1:      db 'isoTron', 0
it_ab2:      db 0
it_ab3:      db 'A light-wall duel in three dimensions.', 0
it_ab4:      db 'Ins/Del or the arrows turn. F10 faster.', 0
it_ab5:      db '1, 2 or 3 starts a game; P pauses.', 0
it_ab6:      db 'F or Alt+Enter: full screen.', 0
it_ab7:      db 'C in a game: the colours.', 0   ; (C at the title is a tier)

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%include "os88alt.inc"              ; Alt+Enter in the bracket (os88alt_edge)

; --- strings ------------------------------------------------------------------
it_s_player: db 'PLAYER ', 0
it_s_comp:   db 'COMPUTER ', 0
; (each at most 40 characters: the full-screen picture is 40 cells across)
it_s_title:  db '1 Rookie 2 Veteran 3 Lunatic; A-C silent', 0
; THE TITLE'S OWN LINES (SPEC.md 108.13.1), over the void above the slab:
; our wording, and no credit, publisher, year or legal line (108.1)
it_ttab:     dw it_s_t0, it_s_t1, it_s_t2, it_s_t3, it_s_t4
it_s_t0:     db 'A light-wall duel in three dimensions.', 0
it_s_t1:     db 'Turn: Ins/Del, Left/Right or Z/X.', 0
it_s_t2:     db 'Faster: F10 or Up. Normal: F9 or Down.', 0
it_s_t3:     db 'In a game: P pause, C colours, M sound.', 0
it_s_t4:     db 'F full screen. Esc ends, Space continues', 0
it_s_paused: db 'PAUSED - PRESS P', 0
it_s_pwin:   db 'Player wins. Press Space to continue.', 0
it_s_cwin:   db 'Computer wins. Press Space to continue.', 0
it_s_over:   db 'THREE LOSSES IN A ROW. GAME OVER.', 0
it_s_small:  db 'Window too small.', 0
it_s_nomem1: db 'isoTron needs ', 0
it_s_nomem1b: db ' KB and ', 0
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
    WORDV it_seg                    ; the work claim: grid, strip, tables
    WORDV it_cseg                   ; the canvas claim
    BYTEV it_nomem                  ; ...which was refused
    BYTEV it_spawned
    BYTEV it_abon
    WORDV it_cx                     ; the content box, absolute
    WORDV it_cy
    WORDV it_cw
    WORDV it_ch
    WORDV it_lcw                    ; ...as it was when the picture was laid out
    WORDV it_lch
    BYTEV it_lcx7                   ; ...and its phase on the byte grid
    BYTEV it_laid
    BYTEV it_layok                  ; the picture fits
    WORDV it_xoff                   ; the picture's origin from the content's x
    WORDV it_ox                     ; the picture's origin: ON the byte grid
    WORDV it_oy
    WORDV it_hc                     ; its rows: min(content height, 400)
    WORDV it_pwb                    ; its device bytes across: 80 (or 79)
    BYTEV it_surf                   ; 0 mono (BLIT1), 1 colour (BLITP)
    BYTEV it_bpp                    ; our display's bits a pixel
    BYTEV it_nocol                  ; a blitp refused after its probe said yes
    BYTEV it_pok                    ; this present cleared the clip (colour)
    WORDV it_scx                    ; where the content was when the surface
    WORDV it_scy                    ; was last asked
    BYTEV it_full                   ; a whole present is owed: 1 from the
                                    ; top, 2 going out in bands (it_band)
    WORDV it_band                   ; ...the next band's first device row
    WORDV it_bnew                   ; ...and this band's end
    WORDV it_bprev                  ; ...the last band's first row, and
    BYTEV it_bpend                  ; ...an overlay it passed owed its text
    BUFV  it_bt0, 4                 ; ...and when it started, pit_now
    BYTEV it_byield                 ; ...more owed: the worker yields
    WORDV it_fx                     ; the frame (W_X, W_Y, W_W, W_H) when the
    WORDV it_fy                     ; content box was last asked (it_geom_ck)
    WORDV it_fw
    WORDV it_fh
    BUFV  it_dmg, 8                 ; a paint's rect, absolute, inclusive...
    BYTEV it_dmgon                  ; ...and that one is being painted
    BYTEV it_pwhole                 ; ...and it is the whole content
    BYTEV it_fillr                  ; ...and the content around the picture
    BYTEV it_hudd                   ; the HUD changed: bit 0 left, bit 1 right
    BYTEV it_msgd                   ; ...the message line
    BYTEV it_hmask                  ; it_ovl_update's copy of it_hudd
    WORDV it_hright                 ; ...and the right line it wants
    BYTEV it_gpres                  ; this wake presented (the governor's)
    BYTEV it_gskip                  ; the governor: every second wake
    BYTEV it_gquiet                 ; ...presenting wakes under 30 ms since
    BYTEV it_godd
    BUFV  it_gt0, 4                 ; the hold's start, pit_now
    BUFV  it_ov, OV_N * OV_SIZE     ; the overlays (itwin.inc)
    BUFV  it_bxs, (MAXHEADS + 1) * 8  ; the boxes of a present
    WORDV it_nbx
    WORDV it_pr0                    ; it_pbox / it_praw: device rows [r0, r1)
    WORDV it_pr1
    WORDV it_pbe
    WORDV it_pj0                    ; ...and bytes [j0, j1]
    WORDV it_pj1
    WORDV it_pnb                    ; it_praw: bytes a row, rows a strip,
    WORDV it_pcap                   ; rows this strip, the plane step, the
    WORDV it_pn                     ; source's first byte, the strip row
    WORDV it_pps
    WORDV it_psrc
    WORDV it_pdst
    WORDV it_pend                   ; ...the strip's end row
    WORDV it_pst1                   ; ...DI's step past one strip row, two
    WORDV it_pst2
    BYTEV it_plast                  ; ...and the logical row it last converted
    BUFV  it_rmap, IT_HCMAX         ; device row -> logical row
    BUFV  it_rfirst, 2 * (IT_CROWS + 1) ; logical row -> first device row
    BUFV  it_xcol, 2 * IT_CCOLS     ; canvas column -> its first byte
    BUFV  it_ctop, IT_CCOLS         ; ...and its lowest TOP row so far
    BYTEV it_ybase                  ; level h's front outline row: 174 - 24(h-1)
    BYTEV it_rbh                    ; it_rebuild's height, the levels left
    BYTEV it_rbj
    WORDV it_rbo                    ; ...level h's offset, 24(h - 1)
    WORDV it_rbcol                  ; ...the column's row-0 byte
    WORDV it_rbcb                   ; ...the level's row t = 0 byte in it
    WORDV it_rbe0                   ; ...the lower body's outline row, and
    WORDV it_rbf0                   ; its face's rows, in a level's t
    WORDV it_rbf1
    BYTEV it_rbfid                  ; ...and the face's id, shifted
    WORDV it_lgx                    ; it_title_build's walk of the word: the
    WORDV it_lgy                    ; letter's column and cell row, its rows,
    WORDV it_lgp                    ; the cell's column, the pixel, the
    WORDV it_lgc                    ; letter's width and the row's bits
    WORDV it_lgxx
    WORDV it_lgyy
    BYTEV it_lgw
    BYTEV it_lgb
    BUFV  it_lhead, H_SIZE          ; ...the head record a letter cell borrows
    BUFV  it_hudl, 16               ; "PLAYER n"
    BUFV  it_hudr, 16               ; "COMPUTER n"
    BUFV  it_msgbuf, 96             ; the refusal's sentence
    BUFV  it_decbuf, 6              ; it_putdec's five digits
    WORDV it_nwhole                 ; windowed whole presents (the harness's)
    WORDV it_spanhk                 ; it_span's tail: it_span_box, or a hook
    WORDV it_lookp                  ; the 16-colour look: it_col or it_colc
    BYTEV it_clook                  ; ...1 = Classic
    BYTEV it_cgahi                  ; CGA full screen at high intensity
    BYTEV it_pfill                  ; it_praw: the piece is VOID, one gfx_fill

; --- the sweep (itsweep.inc, SPEC.md 108.13.3) ---------------------------------
    WORDV it_swk                    ; the next item, 0 .. SW_N
    WORDV it_swt0                   ; the tick it started (before the rebuild)
    WORDV it_swo                    ; the new arena's offset, 24(h - 1)
    WORDV it_swoo                   ; ...and the old picture's
    WORDV it_nsweep                 ; sweeps ended (the harness's)
    WORDV it_swrn                   ; it_sw_rects: the slots' bytes...
    BUFV  it_swr, SW_RMAX * SW_SIZE ; ...and them
%assign IT_SG0 IT_BSS                ; (it_sgr .. it_sgx: one block, which a
    BUFV  it_sgr, SW_RMAX * SW_SIZE ; paint's replay sets aside: it_sgsv)
    WORDV it_sgn                    ; it_sw_group: the merged present, its
    WORDV it_sgs                    ; slots' bytes, the slot going out, and
    WORDV it_sgy                    ; its next device row
    WORDV it_sglim                  ; ...the items it may take
    BYTEV it_sgx                    ; ...the slots that must match
%assign IT_SGSZ IT_BSS - IT_SG0
    BUFV  it_sgsv, IT_SGSZ          ; it_sw_paint: the group set aside
    WORDV it_swc0                   ; it_sw_put: device rows [swc0, swc1)
    WORDV it_swc1
    BYTEV it_swdn                   ; ...and the rect is out
    WORDV it_sbx0                   ; it_sw_band: the band's columns...
    WORDV it_sbx1
    WORDV it_sbp                    ; ...and its table entry (it_sbt)
    BUFV  it_sby, 14                ; it_sw_bandc: seven row boundaries
    BUFV  it_sbt, 7 * SW_BANDS      ; it_sw_tab: every band's, a byte each
    BUFV  it_sdr, 8                 ; it_sw_paint: the damage, device rows
    WORDV it_sbk                    ; ...and the items it may present
    WORDV it_sbd0                   ; ...the damage's bands [sbd0, sbd1]
    WORDV it_sbd1
    WORDV it_sbb                    ; ...the band, and a replay run's end
    WORDV it_sbe
    BYTEV it_rbnoe                  ; it_rebuild: the top level's outline VOID
    BYTEV it_rbedge                 ; ...its outline id this column
    BYTEV it_rbe                    ; ...and the lower body's, this level
    BYTEV it_swpre                  ; the next arena is built (WAIT, 108.13.3)
    BUFV  it_spl, SW_SPN * 4        ; it_sw_dspan: the span list...
    WORDV it_spn                    ; ...its bytes, its first device row,
    WORDV it_spr0                   ; the logical row and rows left, the x
    WORDV it_spy                    ; (or x + y) and a row's end, and 1 for
    WORDV it_spk                    ; a diagonal
    WORDV it_spc
    WORDV it_spc2
    BYTEV it_spd
    WORDV it_spxm                   ; ...the picture's last device x
    BYTEV it_spno                   ; ...GFX_SPANS refused: rects (it_sw_run)
    WORDV it_swbud                  ; it_sw_run: the hold's budget, half counts
    BYTEV it_bldq                   ; a canvas build owed outside the lock
                                    ; (IT_BQ_*; bit 7 = being built: it_bld_run)
    BYTEV it_qround                 ; ...and the round it was for, to start

; --- full screen (itfs.inc, SPEC.md 108.12) -------------------------------------
    BYTEV it_fsm                    ; a bracket is up
    BYTEV it_fsk                    ; ...its surface, FSK_*
    BYTEV it_fsq                    ; ...asked to end
    BYTEV it_fsega                  ; ...borrowed the windowed geometry (EGA)
    BYTEV it_fsret                  ; ...ended: the exit paint owes the picture
                                    ; (itwin.inc it_paint_win)
    WORDV it_fsn                    ; whole full-screen presents (the harness's)
    WORDV it_fsout                  ; brackets ended and repainted (likewise)
    WORDV it_fseg                   ; the video segment (FSI_SEG)
    BUFV  it_fsi, FSI_SIZE          ; OSAPI_FSX_MODE's block
    WORDV it_fstk                   ; the sub-tick clock: the tick, and the
    BYTEV it_fsph                   ; FSXW_FRAMEs since it moved (0..2)
    BYTEV it_lastph                 ; the governor's last phase (it_clock)
    WORDV it_fsptk                  ; EGA: the tick its boxes last went out
    WORDV it_fshook                 ; the surface's span hook
    WORDV it_fsrect                 ; ...its rect presenter
    WORDV it_fsglyph                ; ...its glyph writer
    WORDV it_gseg                   ; OSAPI_FONT_GLYPHS: the table, and the
    WORDV it_goff                   ; first and last character it holds
    BYTEV it_gfirst
    BYTEV it_glast
    WORDV it_fy0                    ; it_fs_rect: logical rows [fy0, fy1),
    WORDV it_fy1                    ; cells [fc0, fc1)
    WORDV it_fc0
    WORDV it_fc1
    WORDV it_fa0                    ; it_fs_area: logical rows [fa0, fa1),
    WORDV it_fa1                    ; cells [fac0, fac1), and the first row
    WORDV it_fac0                   ; not yet presented
    WORDV it_fac1
    WORDV it_fab
    WORDV it_fhc                    ; ...the presenters' row (CGA, Hercules: the
    WORDV it_fhb                    ; pair's first), and VGA's first byte /
                                    ; Hercules' bytes a row
    BUFV  it_ovf, OVF_N * OVF_SIZE  ; the full-screen overlay records
    BUFV  it_fsrow, 2 * IT_HROWS    ; a device / card row's first byte
    BUFV  it_hktop, 8               ; the span: TOP's bits a place (CGA: a
                                    ; byte each; Hercules: a word, by parity)
    BUFV  it_fvtop, 2               ; VGA: TOP's colour at an even / odd x

; --- the game --------------------------------------------------------------------
    BYTEV it_state
    BYTEV it_tier                   ; 1..3
    BYTEV it_snd                    ; sound wanted (M, Game > Sound, the tier)
    BYTEV it_paused
    WORDV it_tend                   ; a timed state's end, in ticks
    WORDV it_lastt                  ; the governor's last tick
    WORDV it_debt                   ; now - V, in u (signed; SPEC.md 108.4.2)
    BYTEV it_vsync                  ; V = now at the next wake (a round start)
    WORDV it_A                      ; the tier's cost and the computer's
    WORDV it_B                      ; parameters (it_tiers)
    WORDV it_C
    BYTEV it_look
    BYTEV it_icpt
    BYTEV it_wander
    BYTEV it_nheads
    WORDV it_hend                   ; it_heads + nheads * H_SIZE: the
                                    ; head loop's end, so it needs no index
    WORDV it_openp                  ; the head record the opening rule is for
    BYTEV it_live                   ; computer heads alive (a running count)
    WORDV it_costb                  ; A + B * it_live, kept with it
    WORDV it_cost                   ; this iteration's cost, built as it runs
    BYTEV it_pspeed                 ; the player's speed, sixths
    BYTEV it_pacc
    BYTEV it_inboost                ; the iteration's keys
    BYTEV it_inf9
    BYTEV it_r                      ; the AI's RNG byte
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
    BYTEV it_qsnd                   ; M or Game > Sound: toggle it_snd
    BYTEV it_hudp                   ; the winner's HUD line, owed 0.25 s on
    BYTEV it_tkey                   ; THE TYPEMATIC FILTER (itinput.inc): the
    BYTEV it_tup                    ; last turn keystroke's key, its up seen
    WORDV it_tacc                   ; since, and when a turn was last accepted

; --- the sound (itsnd.inc, SPEC.md 108.14.1): its one writer is it_snd_frame --
    WORDV it_sphn                   ; the phrase's next row, 0 = none playing
    WORDV it_spt0                   ; ...the tick it began
    BYTEV it_spnew                  ; ...2: posted, it begins at the next frame
    BYTEV it_sppri                  ; ...its priority
    BYTEV it_sndtk                  ; a wall tick owed (it_iterate posts it)
    BYTEV it_sndpc                  ; ...player cells to the next one
    BYTEV it_sndlive                ; a tone of ours may be sounding...
    WORDV it_sndexp                 ; ...until the tick its lease ends

%include "ittest.inc"

    OS88_BSS IT_BSS
    OS88_IMAGE_END
