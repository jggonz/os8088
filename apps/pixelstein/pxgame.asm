; =============================================================================
; os8088 - apps/pixelstein/pxgame.asm
;
; PIXELSTEIN 3D (SPEC.md 97): a raycast first-person shooter in the shape of
; the 1992 one, on the 8088 this project is calibrated against - fullscreen
; in a foreign mode on every adapter (CGA 320x200x4, the 160x100x16 retime
; on a genuine CGA, the Hercules box, Mode X) and windowed as a 1bpp band.
;
; THIS IS PART 0 OF PXSTEIN.O88 (97.9, 20.12.10): a whole .o88 image, its
; bss shipped inside it, that apps/pixelstein/pxstein.asm - the loader, and
; the image the kernel launches - re-homes the instance onto after reading
; the parts. What the loader learned that this program cannot ask for
; itself is at the head of this bss, px_hand (the PXH_* words).
;
; WAVE 1 (docs/plans/PIXELSTEIN-PLAN.md 7): the cast (pxcast.inc), the Flat
; and Wire rungs on the static ladders (pxgen.inc, pxcomp.inc), the five
; backends' geometry and presents (pxrast.inc), the window and its worker
; (pxwin.inc), the clock, the keys and the player (pxgame.inc). WAVE 2: the
; textures - the generator, the transpose and the Textured arm (pxgen.inc,
; pxcomp.inc), the View row and PXSTEIN.CFG (pxset.inc). WAVE 3: the
; sprites and the weapon (pxspr.inc), the guards (pxact.inc), the doors
; that slide, the keys, the pickups, the hitscan and the player's health
; (pxgame.inc), three floors. WAVE 4: the status bar and the cards
; (pxhud.inc), the seven states, the timedemo, the sound and the mouse
; (pxgame.inc), the high scores and the floor passwords (pxhs.inc), eight
; floors; the region declared movable and the worker restartable. WAVE 6:
; the dog, a second actor kind through the same sprite path (pxact.inc),
; the Tab map of seen cells (pxspr.inc) and the WIN4 dither (pxwin.inc).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Pixelstein 3D', px_entry, OS88_F_ICON, OS88_STACK_256

    OS88_ICON16
%include "pxicon.inc"
    OS88_ICON16_END

; --- the picture (SPEC.md 97.1, 97.3) ----------------------------------------
PX_STRIDE   equ 80                  ; bytes a shadow row, every backend
PX_ROWS     equ 80                  ; view rows
PX_HUDROWS  equ 24                  ; the HUD band under the view (wave 4)
PX_BAND     equ 64                  ; the band's bytes at Size 64, a window's
                                    ; most (97.3). THERE IS NO PX_X0: the
                                    ; band's first byte is (80 - Size) / 2
                                    ; and lives in px_x0 / px_cbias, set by
                                    ; px_res_apply - the first cut composed
                                    ; every column at a constant 8 while the
                                    ; presents read the variable, so every
                                    ; Size but 64 drew its picture beside
                                    ; where it was shown (review, wave 2)
PX_HORIZON  equ 40                  ; where Wire's ground changes tone
PX_GEOMIN   equ 12                  ; a Flat wall this many rows tall or more
                                    ; whose tone stood writes its two ENDS
                                    ; only (pxcomp.inc's .fgeo, 97.5): the
                                    ; second ladder entry (~280 clk) is paid
                                    ; back by ~11 stores it does not make
PX_COLMAX   equ 80                  ; the column arrays' length (Size 80)
PX_SHKB     equ 16                  ; the shadow claim: 16 KB CLAIMED, of
                                    ; which wave 1 composes 80 x 80 = 6,400
                                    ; bytes (every backend but Mode X, which
                                    ; composes into VRAM). The rest is
                                    ; reserved for wave 2's Rows 100 (8,000)
                                    ; and wave 5's WIN4 - one 25-row strip
                                    ; of 256-byte rows in the spare 6,400,
                                    ; reused per 25 rows (PX_W4BUF, 97.14)
PX_W4ROWS   equ 25                  ; WIN4's strip (97.14): 25 rows...
PX_W4BUF    equ 9920                ; ...of 256 bytes, at the byte Rows 100's
                                    ; bar would end (100 + 24 rows x 80) - the
                                    ; plan's reservation, kept, so Rows 100
                                    ; needs no move...
PX_W4TAB    equ PX_W4BUF + PX_W4ROWS * PX_BAND * 4   ; ...and the two 32 ->
                                    ; 16 tables after it, the even rows' and
                                    ; the odd rows' (wave 6's dither):
                                    ; 16,320..16,383 - the claim's last byte
%if PX_W4BUF < (PX_ROWS + 20 + PX_HUDROWS) * PX_STRIDE
%error "WIN4's strip overlaps the rows Rows 100 and its bar would take (97.14)"
%endif
%if PX_W4TAB + 64 > PX_SHKB * 1024
%error "WIN4's strip and tables outgrew the shadow claim (97.14)"
%endif
%if PX_W4TAB & 63
%error "WIN4's two tables must sit 64-aligned: px_blit_w4 swaps them with xor bl, 0x20"
%endif
PX_MINDIST  equ 23                  ; nx clamped at 0.09 tiles (Q8.8)
PX_HEIGHTK  equ 51200               ; h = PX_HEIGHTK / nx rows
PX_SPOTD    equ 4096                ; a walker's mark is this far past its cell
PX_CGAROW0  equ 28                  ; the view's first device row, CGA 320x200
PX_HERCROW0 equ 134                 ; ...in the Hercules box (74 + 60)
PX_MXROW0   equ 48                  ; ...on a Mode X page
PX_WINW     equ 528                 ; the window's frame
PX_LINEMAX  equ 65                  ; the text line's cells: what 528 - the
                                    ; border holds, and never past it (pxwin.inc)
PX_WINH     equ 150

; --- the session (97.8) ------------------------------------------------------
PX_MAXSTEP  equ 3                   ; the catch-up cap
PX_TURN     equ 64                  ; angle units a tick: a turn in 3.5 s
PX_SPEED    equ 24                  ; Q8.8 a tick: 0.094 tile
PX_RUN      equ 48
PX_RADIUS   equ 64                  ; 0.25 tile, the collision radius
PX_BUDGET   equ 149165              ; 1/8 s in 838ns units: Auto's budget -
                                    ; the SCENE-A promise (8.0), PLAN 14's own
                                    ; definition (its 7.0 was scene A's when it
                                    ; was written); scene B may sit between the
                                    ; two promises without the ladder moving
                                    ; only while it is a pose passed through,
                                    ; and eight frames over the line IS the
                                    ; machine playing there (97.8)
PX_BUDGET50 equ 74582               ; ...and HALF of it, a 286's step-up
                                    ; line: the rung above measures up to
                                    ; 1.92x this one (Mode X's work), so 60%
                                    ; (1.67x) walked the 8086 off its default
                                    ; - and on an 8086 half is under every
                                    ; frame, so it has PX_AUP1/PX_AUPF (97.8)
PX_AUP1     equ 132591              ; an 8086's step-up line onto Textured
                                    ; Low res: 111.1 ms, PX_BUDGET / 1.125,
                                    ; the largest Textured Low res / Flat Low
                                    ; res ratio measured (97.8, 97.15)
PX_AUPF     equ 92419               ; ...and onto a Full res position: 77.4
                                    ; ms, PX_BUDGET / 1.614 (Textured Full /
                                    ; Textured Low res; Flat Full / Flat Low
                                    ; res reads up to 1.599) - the FULL
                                    ; REPAINT's and the turn's ratio only:
                                    ; the Full rungs' finished frames were
                                    ; never measured, and with sprites the
                                    ; ratio is likely ~1.63 (97.8 says what
                                    ; bounds it: Size 48 under a Full ceiling)
PX_AHOLD    equ 182                 ; ticks (10 s) no step up follows a step
                                    ; down: the hysteresis the threshold is not
PX_KA       equ 0x1E                ; A - strafe left
PX_KD       equ 0x20                ; D - strafe right
PX_KW       equ 0x11                ; W - forward
PX_KS       equ 0x1F                ; S - back
PX_KLSH     equ 0x2A                ; the two shifts: run
PX_KRSH     equ 0x36
PX_KCTRL    equ 0x1D                ; Ctrl: fire (97.8, wave 3)
PX_K1       equ 0x02                ; 1, 2, 3: the knife, the pistol, the gun
PX_K2       equ 0x03
PX_K3       equ 0x04

; --- the world (97.6, 97.7, 97.8; wave 3) -----------------------------------
PXC_BLOCK   equ 0x10                ; an OPEN cell's high nibble, which the
                                    ; walkers never read (they stop on bits
                                    ; 0-1 alone): a blocking static is here
PXC_ACTOR   equ 0x20                ; ...an actor's centre is here
PXC_PLAYER  equ 0x40                ; ...the player's
PX_GENKB_CAP equ 51                 ; pxstein.asm's PX_GENKB, restated for
                                    ; the greyed caption's guard below and
                                    ; held to the loader's by the loader
                                    ; (pxstein.asm's own %if)
PX_MAXDOORS equ 64
PX_MAXACT   equ 32
PX_MAXSTAT  equ 96
; a door (PXD_*): 8 bytes
PXD_CELL    equ 0                   ; word: the plain map's cell
PXD_FLAGS   equ 2                   ; byte: the cell's flags (PXC_DOOREW)
PXD_LOCK    equ 3                   ; byte: 0 none, 1 gold, 2 silver
PXD_POS     equ 4                   ; word: how far the slab has slid, 0..256
PXD_STATE   equ 6                   ; byte: PXDS_*
PXD_TIMER   equ 7                   ; byte: ticks held open
PXD_SIZE    equ 8
PXD_FOUND   equ 0x80                ; PXD_FLAGS: a secret door already opened
                                    ; (counted once toward the card's ratio)
PXDS_SHUT   equ 0
PXDS_OPENING equ 1
PXDS_OPEN   equ 2
PXDS_CLOSING equ 3
PX_DOORSTEP equ 16                  ; units a tick: shut to open in 16 ticks
PX_DOORPASS equ 128                 ; ...and a body passes from here
PX_DOORHOLD equ 91                  ; ticks a door stays open: 5 s
; an actor (PXA_*): 16 bytes
PXAC_X       equ 0                   ; word, Q8.8
PXAC_Y       equ 2
PXAC_KIND    equ 4                   ; byte: 0 guard, 1 dog (PXK_DOG, wave 6)
PXAC_STATE   equ 5                   ; byte: PXAS_*
PXAC_DIR     equ 6                   ; byte: 0 E, 1 S, 2 W, 3 N - the way it
                                    ; moves; 0xFF none
PXAC_TIMER   equ 7                   ; byte: ticks left in this state
PXAC_HP      equ 8                   ; byte
PXAC_FRAME   equ 9                   ; byte: the walk phase counter / the
                                    ; attack phase / the die frame
PXAC_FLAGS   equ 10                  ; byte: PXAF_*
PXAC_PAD     equ 11
PXAC_CELL    equ 12                  ; word: the cell its centre is in
PXAC_ANG     equ 14                  ; word: the way it FACES, 12 bits
PXAC_SIZE    equ 16
PXAS_NONE   equ 0
PXAS_STAND  equ 1
PXAS_PATROL equ 2
PXAS_ALERT  equ 3                   ; it saw the player: the reaction delay
PXAS_CHASE  equ 4
PXAS_ATTACK equ 5
PXAS_PAIN   equ 6
PXAS_DIE    equ 7
PXAS_DEAD   equ 8
PXAF_PATROL equ 1                   ; it patrols (the level's capital letter)
PXAF_ATTACK equ 2                   ; attack mode: it has seen the player
PXAF_SEEN   equ 4                   ; it was drawn last frame (the hit
                                    ; chance: the player can see it to dodge)
PX_GUARDHP  equ 25                  ; the 1992 engine's guard at "bring 'em on"
PXK_DOG     equ 1                   ; PXAC_KIND: the dog (wave 6, 97.8) -
PX_DOGHP    equ 1                   ; one hit kills it, as the 1992 engine's
PX_DOGWALK  equ 16                  ; patrolling, Q8.8 a tick (1.1 tile/s)
PX_DOGRUN   equ 40                  ; chasing (2.8 tile/s): FAST - 1.67x a
                                    ; guard's run, and a guard's own run is
                                    ; the player's walk (24)
PX_DOGBITE  equ 180                 ; a bite's chance in 256 at a tile (the
                                    ; 1992 engine's T_Bite)
PX_DOGLUNGE equ 8                   ; ticks from the leap to the bite...
PX_DOGBACK  equ 5                   ; ...and from the bite to the next chase
                                    ; step: a bite every ~0.7 s at the elbow
PX_ACTWALK  equ 8                   ; Q8.8 a tick, patrolling (0.57 tile/s)
PX_ACTRUN   equ 24                  ; ...chasing (1.7 tile/s)
PX_ACTRAD   equ 64                  ; an actor's collision radius, 0.25
PX_LOSMAX   equ 24                  ; no line of sight past this many tiles
PX_MELEE    equ 384                 ; the knife reaches 1.5 tiles (Q8.8)
; a static (PXT_*): 4 bytes
PXT_CELL    equ 0                   ; word
PXT_KIND    equ 2                   ; byte: pickup 0..7, decoration 8..13,
                                    ; 0xFF taken
PXT_FLAGS   equ 3                   ; byte: bit 0 blocking
PXT_SIZE    equ 4
PXK_AMMO    equ 0                   ; the pickup kinds (tools/pxslevel.py)
PXK_MEDKIT  equ 1
PXK_FOOD    equ 2
PXK_GOLDKEY equ 3
PXK_SILVKEY equ 4
PXK_TREAS   equ 5
PXK_CHALICE equ 6
PXK_LIFE    equ 7
PXK_DECO0   equ 8
; the player (97.8)
PX_HEALTH0  equ 100
PX_AMMO0    equ 8
PX_AMMOMAX  equ 99
PX_LIVES0   equ 3
PXW_KNIFE   equ 0
PXW_PISTOL  equ 1
PXW_MGUN    equ 2
PX_FADE     equ 12                  ; ticks the DIE wash stands before the
                                    ; floor restarts
PXST_PLAY   equ 0                   ; px_state: playing...
PXST_DYING  equ 1                   ; ...the wash is up (DIE)
PXST_ATTRACT equ 2                  ; ...the attract page (the launch's)
PXST_READY  equ 3                   ; ..."FLOOR n" before a floor
PXST_DONE   equ 4                   ; ...LEVELDONE: the ratios card
PXST_OVER   equ 5                   ; ...GAME OVER (or the episode won)
PXST_ENTER  equ 6                   ; ...the initials of a high score
PXST_DEMO   equ 7                   ; ...the timedemo running (97.13)
; a sprite candidate (PXS_C_*): 12 bytes, up to PX_MAXSPR of them a frame,
; sorted far to near (97.6)
PXS_C_H     equ 0                   ; word: the true height K / nx
PXS_C_C     equ 2                   ; word: the centre column, signed
PXS_C_COST  equ 4                   ; word: the stores it would make
PXS_C_C0    equ 6                   ; word: its first column, signed
PXS_C_W     equ 8                   ; byte: its width in columns
PXS_C_FR    equ 9                   ; byte: the frame
PXS_C_FL    equ 10                  ; byte: bit 0 mirrored, bit 1 every
                                    ; second column (the cap)
PXS_C_ACT   equ 11                  ; byte: the actor, 0xFF a static
PXS_C_SIZE  equ 12
PX_MAXSPR   equ 8
PX_SPRCAP   equ 8000                ; stores a frame the sprites may make
PX_WPNCOLS  equ 16                  ; the weapon: 16 bytes wide...
PX_WPNH     equ 24                  ; ...through the 24-row scaler...
PX_WPNROW0  equ 56                  ; ...on rows 56..79

; --- the backends and the rungs ----------------------------------------------
PXB_NONE    equ 0
PXB_CGA4    equ 1                   ; FSXM_CGA320
PXB_C160    equ 2                   ; FSXM_TEXT80 retimed (SPEC.md 88.15)
PXB_HERC    equ 3                   ; FSXM_HERC, TANK's box
PXB_MODEX   equ 4                   ; FSXM_MODEX, two pages
PXB_WIN1    equ 5                   ; the window's 1bpp band
PXB_WIN4    equ 6                   ; ...and its 16-colour one (97.14, wave 5)
PXR_WIRE    equ 0                   ; the detail ladder's rungs (97.1)
PXR_FLAT    equ 1
PXR_TEX     equ 2                   ; the compiled scalers (97.3, wave 2)
PXD_AUTO    equ 0                   ; the Detail row's items: the rung axis...
PXD_WIRE    equ 1
PXD_FLAT    equ 2
PXD_TEX     equ 3
PXD_RFULL   equ 4                   ; ...and the resolution axis under it
PXD_RLOW    equ 5                   ; (SPEC.md 97.8: one menu, two axes)
PXD_COLOUR  equ 6                   ; ...and the window's colour (97.14)
PXV_ROWS0   equ 5                   ; the View row: Size 48..80 are items
                                    ; 0..4, Rows 80/100 items 5 and 6

; --- the handoff (97.9): one package, two sources - pxstein.asm writes these -
PXH_MAGIC   equ 0
PXH_LEV     equ 2
PXH_GEN     equ 4
PXH_COLD    equ 6
PXH_NLEV    equ 8
PXH_LEVLEN  equ 10
PXH_ART     equ 12                  ; the expanded art masters (97.4), 0 = none
PXH_BT      equ 14                  ; the byte-texture part, 0 = refused
PXH_GENLEN  equ 16                  ; the scratch part's bytes (PX_GENKB * 1024):
                                    ; the bound px_gen_build emits under
PXH_SPR     equ 18                  ; the sprite set (97.6), 0 = refused
PXH_SIZE    equ 20

; =============================================================================
; px_entry - the program's entry proc, called once the loader has re-homed
;            the instance (SPEC.md 20.12.10): DS = CS = ours, ES = KERNEL_SEG
; =============================================================================
px_entry:
    push si
    push di
    cmp word [px_hand + PXH_MAGIC], 'PX'
    jne .refuse                     ; no loader ran: this image was launched
                                    ; as a package of its own, which it is not
    call OSAPI_VIDEO
    mov [px_scrw], ax
    mov [px_dock], cx
    mov al, KSC_SPACE               ; ARMING the scancode reader: the first
    call OSAPI_KEY_DOWN             ; answer is always "up" (SPEC.md 9.7)
    call OSAPI_GET_TICKS
    call OSAPI_SRAND
    call OSAPI_CPU_INFO
    mov [px_tier], al
    mov ax, PX_SHKB                 ; THE SHADOW, BEFORE THE WINDOW (97.9): a
    call OSAPI_MEM_CLAIM            ; refusal is a sentence in the window and
    jc .noshadow                    ; never a black bounce
    mov [px_shseg], dx
.noshadow:
    mov byte [px_levnext], 0xFF     ; the player, before the first floor
    mov byte [px_health], PX_HEALTH0
    mov byte [px_ammo], PX_AMMO0
    mov byte [px_lives], PX_LIVES0
    mov byte [px_weapon], PXW_PISTOL
    mov word [px_wdrawn], 0xFFFF    ; nothing on either page's glass
    mov byte [px_aim], 0xFF
    mov byte [px_state], PXST_ATTRACT   ; THE ATTRACT PAGE FIRST (97.13): the
    mov byte [px_codep], 0xFF       ; first floor loaded behind it for the
    mov byte [px_sound], 1          ; timedemo; sound ON unless PXSTEIN.CFG
    call px_hs_init                 ; says otherwise; the built-in table
    xor al, al                      ; THE WINDOW'S COLOUR DEFAULTS ON FROM THE
    cmp byte [px_tier], CPU_286     ; 286 UP (97.14): the planar present is
    jb .col0                        ; MEASURED at 297.6 ms of 8088 time at
    inc ax                          ; Textured Full (tests/pxswin.py --price),
.col0:                              ; ~50 ms on a 286 at 6x (~74 at 4x) - the
    mov [px_colour], al             ; frame ~74 ms (~110), under Auto's 125.
                                    ; An 8086 has the item greyed with its
                                    ; price; PXSTEIN.CFG's byte wins over both
    call px_font_init               ; until PXSTEIN.HS is read (below)
    xor al, al
    call px_level_load              ; E1M1 into the two map layouts and
    jc .refuse                      ; the tables
    call px_gen_init                ; where the bodies run (97.2.1)
    call px_auto_init
    mov byte [px_mode], PXB_NONE
    ; centre the window in the desktop band
    mov ax, [px_scrw]
    sub ax, PX_WINW
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [px_tpl + WT_X], ax
    mov ax, [px_dock]
    sub ax, MBAR_H
    sub ax, PX_WINH
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [px_tpl + WT_Y], ax
    mov si, px_tpl
    call OSAPI_WM_CREATE
    jc .full
    mov [px_win], bx
    OS88_REGION_MOVABLE px_reloc    ; THE CARVE MAY MOVE (SPEC.md 66.6.1.2,
                                    ; 97.9): part 0 is a re-homed program and
                                    ; the other parts are INSIDE its region,
                                    ; so the proc is not a `ret` - it moves
                                    ; the segment words the loader left and
                                    ; the ones px_gen_init derived from them
    mov al, 1                       ; THE LAYOUT IS FIXED (SPEC.md 11.93): 80
    call OSAPI_WM_KEEPH             ; band rows + 24 HUD + 16 line = 120
                                    ; content rows under an 18-row title, and
                                    ; the smallest desktop in the tree (640x200
                                    ; CGA) clamps a frame at 155 against this
                                    ; 150 - five rows. Without the flag wm_fit
                                    ; would shorten the RECORD in silence, the
                                    ; band go on drawing through the cut, and
                                    ; the rows below it take no clicks (apps/
                                    ; mines' worked example). Flags preserved,
                                    ; so the loader's CF survives it (97.3)
    mov al, 1                       ; an 8-aligned content origin: the band
    call OSAPI_WM_SNAP              ; blit wants x on the byte grid
    mov si, px_pref
    call OSAPI_WM_PREFER            ; per-adapter frame sizes; flags preserved
    mov bx, [px_win]
    mov al, 1
    call OSAPI_WM_OWNBG             ; every pixel of the content is ours
    mov bx, [px_win]
    mov ax, px_onresize
    call OSAPI_WM_ONRESIZE          ; the card can change under us
    call px_adapter                 ; which raster this display gives us
    call px_set_load                ; PXSTEIN.CFG: the player's last picks,
                                    ; clamped (pxset.inc) - after the adapter
                                    ; so the Mode pick can be checked against
                                    ; what this display offers
    call px_hs_load                 ; PXSTEIN.HS (pxhs.inc): the UI task, here
    call px_toggle_captions         ; "Sound: On" / "Mouse: Off" (97.13)
    call px_tex_caption             ; "Textured" says whether it can be had
    cmp word [px_shseg], 0
    je .noraster                    ; no shadow: no backend to set up either
    call px_r_setup_win             ; the window's own backend, from the start
.noraster:
    call px_auto_start              ; ...and the rung a window starts at on
                                    ; this tier (97.8): px_apply runs here,
                                    ; and a zeroed rung byte is Wire
    mov byte [px_cardl], 0          ; ...and the attract card owed on the
    call px_card_owe                ; first frame
    mov bx, [px_win]
    mov si, px_menus
    call OSAPI_MENU_SET
    mov bx, [px_win]
    mov si, px_about
    call OSAPI_ABOUT_SET            ; SPEC.md 12.2: every package has one
    clc
.full:
    pop di
    pop si
    ret
.refuse:
    stc
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; px_reloc - our region moved (SPEC.md 66.6.1, 66.6.1.2): BX = the segment it
;            WAS at, DX = where it is now, DS = the new one. Preserves every
;            register.
;
; NOT A `ret`, for tests/rehome's reason (rp_reloc is the model): this is a
; RE-HOMED program, its region is the parts carve, and the carve holds the
; scratch the scalers are generated into (part 1) and the byte textures
; (part 2) beside part 0 - so the loader's handoff named two segments that
; MOVE WITH US, and nothing in the kernel knows
; those words exist. EVERY WORD THAT NAMES OUR OWN REGION, enumerated
; (97.9's table), each moved by the delta when it is not 0 (a refused part
; is 0, and a delta added to 0 is a wild segment - 66.6.1's "a proc guards
; its zeros"):
;   px_hand + PXH_GEN / PXH_BT                the loader's two
;   px_bseg, and the segment half of px_qcur / px_qcurv / px_qcurh
;                                             the bodies: part 1, or part 0
;                                             itself when part 1 was refused
;   px_drvp + 2, px_drv2p + 2                 the driver's two far entries
;   [PXG_QTEX] INSIDE part 1                  the wall pass's ES: the byte
;                                             textures' segment, copied there
;                                             by px_gen_init - rewritten from
;                                             the fixed PXH_BT
; and NOT: PXH_LEV (the level stream is a lazy part since wave 4, fetched
; into a claim of its own), PXH_ART and PXH_SPR (the masters and the sprite set are claims of
; their OWN, slot-owned data claims the compactor never moves - the same
; rule tests/rehome's rp_cseg follows), px_shseg (a pinned claim), px_dseg
; (the shadow or the framebuffer), [PXG_QSPR] (the sprite set's). The
; far-call return part 0 leaves on a stack during a compose is the other
; half, and it is never there when this runs: the region moves only while
; the worker is PARKED in OSAPI_TASK_ALIVE, where its stack is empty, and
; the kernel then re-enters it at px_worker_rs (OS88_WORKER_RESTARTABLE,
; 66.6.2); a callback or a bracket names us in [wm_pkgs], which pins us
; -----------------------------------------------------------------------------
px_reloc:
    push ax
    push bx
    push cx
    push si
    push es
    mov ax, dx
    sub ax, bx                      ; AX = the delta, in paragraphs
    mov si, px_rltab
    mov cx, PX_RLN
.w:
    mov bx, [si]
    cmp word [bx], 0
    je .n                           ; a refused part: 0, and it stays 0
    add [bx], ax
.n:
    inc si
    inc si
    loop .w
    mov ax, [px_hand + PXH_GEN]     ; ...and part 1's own copy of the byte
    or ax, ax                       ; textures' segment, from the word just
    jz .out                         ; fixed
    mov es, ax
    mov ax, [px_hand + PXH_BT]
    mov [es:PXG_QTEX], ax
.out:
    inc byte [px_moved]             ; the gate reads this: a proc declared and
    pop es                          ; never called is 66.2's own failure
    pop si
    pop cx
    pop bx
    pop ax
    ret

px_rltab:   dw px_hand + PXH_GEN, px_hand + PXH_BT
            dw px_bseg, px_qcur + 2, px_qcurv + 2, px_qcurh + 2
            dw px_drvp + 2, px_drv2p + 2
PX_RLN      equ ($ - px_rltab) / 2

; =============================================================================
; THE WINDOW CALLBACKS - near procs, ES = KERNEL_SEG, the gfx lock held
; =============================================================================

; -----------------------------------------------------------------------------
; px_paint - W_PAINT: black, the band as it stands, the text line, the card
; -----------------------------------------------------------------------------
px_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [px_win], si
    inc word [px_npaint]            ; (tests/pxswin.py: a MOVE costs none)
    mov byte [px_hidden], 0         ; a paint proves the window can be seen
    call px_geom_win
    cmp word [px_shseg], 0
    jne .have
    mov al, CBLACK                  ; (px_black_around sets its own colour on
    call OSAPI_SET_COLOR            ; the other arm: one call, not two)
    mov ax, [px_cx]                 ; no shadow: black, and say so (SPEC.md
    mov bx, [px_cy]                 ; 42.6)
    mov cx, ax
    add cx, [px_cw]
    dec cx
    mov dx, bx
    add dx, [px_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov cx, [px_bx]
    mov dx, [px_cy]
    add dx, 8
    mov si, px_s_nomem
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_FONT_RUN
    jmp short .card
.have:
    call px_black_around
    call px_spawn_ck                ; the worker starts here, not at entry
    mov byte [px_whole], 1          ; the whole band, whatever the worker's
    mov byte [px_hudw], 1           ; own dirty rows say (97.5) - and the
    cmp byte [px_composing], 0      ; whole bar, which the black just took
    jne .line                       ; mid-compose: the worker's next pass
                                    ; blits it (px_render_win's .whole exit -
    call px_blit_win                ; no cast is owed for a blackened glass)
    call px_hud_blit
.line:                              ; ...else now, off the shadow as it stands
    call px_line_draw
.card:
    cmp byte [px_abon], 0
    je .out
    mov bx, [px_win]
    mov si, px_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed
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

; px_black_around - the black where the band is NOT (the lock held, the clip
;                   armed, px_geom_win done): under it (the HUD band and the
;                   line's row) and the margin either side, which
;                   OSAPI_WM_SNAP makes the right-hand one alone. NOTHING
;                   WRITES A PIXEL TWICE (CLAUDE.md): the band comes whole off
;                   the shadow. The worker calls this too, under its lock,
;                   when the Size shrank (px_repaint) - the glass right of a
;                   narrower band keeps the old picture otherwise
; clobbers AX, BX, CX, DX
px_black_around:
    mov byte [px_repaint], 0
    mov byte [px_hudw], 1           ; THE BAR IS UNDER THE BAND: the black
                                    ; below it takes the bar too, so the bar
                                    ; is owed whole (px_hud_blit) - the first
                                    ; cut set this in px_paint alone, and a
                                    ; Size change's black left the window's
                                    ; bar blank (tests/pxsfsx.py, 97.13)
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [px_cx]
    mov bx, [px_by]
    add bx, PX_ROWS                 ; the rows under the band, full width
    mov cx, ax
    add cx, [px_cw]
    dec cx
    mov dx, [px_cy]
    add dx, [px_ch]
    dec dx
    cmp bx, dx
    ja .margl
    call OSAPI_GFX_FILL
.margl:
    mov ax, [px_cx]
    mov cx, [px_bx]
    cmp ax, cx
    jae .margr                      ; no left margin (the snapped case)
    dec cx                          ; [cx, bx) x the band's rows
    mov bx, [px_by]
    mov dx, bx
    add dx, PX_ROWS - 1
    call OSAPI_GFX_FILL
.margr:
    mov al, [px_size]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [px_bx]                 ; [bx + Size * 8, cx + cw) x the band's rows
    mov cx, [px_cx]
    add cx, [px_cw]
    dec cx
    cmp ax, cx
    ja .band
    mov bx, [px_by]
    mov dx, bx
    add dx, PX_ROWS - 1
    call OSAPI_GFX_FILL
.band:
    ret

; -----------------------------------------------------------------------------
; px_onkey - W_ONKEY: AL = ascii, AH = scan, SI = window
; -----------------------------------------------------------------------------
px_onkey:
    push ax
    push bx
    call px_abdismiss               ; any key takes the card down, and is
    jc .out                         ; spent doing it
    call px_key_common
.out:
    pop bx
    pop ax
    ret

; px_onclick - W_ONCLICK: a click takes the card down and does nothing else
px_onclick:
    call px_abdismiss
    ret

; px_onresize - OSAPI_WM_ONRESIZE: the box or the display changed under us
px_onresize:
    call px_adapter
    mov byte [px_force], 1
    mov byte [px_lined], 1
    mov byte [px_hidden], 0         ; a window that moved may be seen again
    ret

; -----------------------------------------------------------------------------
; px_oncmd - the menu handler: AL = the item, AH = the menu (0-based)
; -----------------------------------------------------------------------------
px_oncmd:
    push ax
    push bx
    call px_abdismiss
    or ah, ah
    jz .game
    cmp ah, 1
    je .mode
    cmp ah, 2
    je .detail
    cmp ah, 3
    je .view
    jmp .out                        ; a menu the set does not have
.game:
    or al, al
    jnz .pause
    call px_go_fsx
    jmp .out
.pause:
    cmp al, 1
    jne .snd
    cmp byte [px_state], PXST_PLAY  ; a pause is PLAY's (px_key_common)
    jne .pref
    cmp byte [px_mapon], 0          ; OVER THE TAB MAP the pick takes the map
    je .pz                          ; down, as P does (px_key_common) - a
    call px_map_off                 ; bare toggle would clear the map's own
    jmp short .gout                 ; pause and run the world behind it
.pz:                                ; (review, wave 6)
    xor byte [px_pause], 1
    mov byte [px_lined], 1
    call px_pause_msg
.gout:
    jmp .out
.pref:                              ; ...and outside it the item SAYS SO on
    mov byte [px_lrefuse], 2        ; the window's line (SPEC.md 47; review
    mov byte [px_lined], 1          ; r2) rather than changing nothing
    jmp .out
.snd:
    cmp al, 2
    jne .mouse
    xor byte [px_sound], 1          ; Sound (97.13): the effects through
    jmp short .tog                  ; OSAPI_SND_TONE, on or off
.mouse:
    xor byte [px_mouse], 1          ; Mouse: steering by the pointer's offset
.tog:
    call px_toggle_captions
    jmp .save
.mode:
    mov bl, [px_mode0]
    or al, al
    jz .m
    mov bl, [px_mode1]
.m:
    or bl, bl
    jz .out                         ; "No second mode": nothing to pick
    mov [px_mode], bl
    mov [px_modepick], al           ; ...the item, for the settings file
    call px_adapter                 ; ...and its FSXM_*
    jmp short .save
.detail:
    cmp al, PXD_TEX
    jne .dok
    cmp byte [px_texok], 0
    je .out                         ; greyed (MENU_DIS: "Textured (needs
                                    ; 118 KB)") - the kernel never
                                    ; dispatches it, and this return is for
                                    ; a shortcut, which goes near no menu
.dok:
    cmp al, PXD_COLOUR
    jne .dnc
    xor byte [px_colour], 1         ; THE WINDOW'S COLOUR (97.14): the worker
    call px_adapter                 ; takes the backend at its next frame
    mov byte [px_force], 1          ; (px_back_ck), and the item says what
    jmp short .save                 ; is in force now
.dnc:
    cmp al, PXD_RFULL
    jae .res
    mov [px_detail], al
    mov byte [px_pend], 1
    jmp short .save
    ; --- the resolution axis of the same menu: Full res / Low res ---------
.res:
    sub al, PXD_RFULL               ; Full res = 0, Low res = 1
    mov [px_res], al
    cmp byte [px_detail], PXD_AUTO
    jne .rpend
    ; under Auto the pick RE-SEATS the position (SPEC.md 97.8) within the
    ; rung Auto is at: the ladder is (Textured Full, Textured Low res, Flat
    ; Full, Flat Low res), so the position's low bit IS the resolution.
    ; Otherwise the item changed nothing and said nothing, which SPEC.md 47
    ; forbids
    mov bl, [px_apos]
    and bl, 0xFE
    or bl, al
    mov [px_apos], bl
    mov [px_astart], bl             ; ...and it is the new ceiling (97.8)
    mov byte [px_amiss], 0
    mov byte [px_ahit], 0
.rpend:
    mov byte [px_pend], 1           ; applied between frames (px_frame_begin)
    jmp short .save
    ; --- View: Size 48..80, Rows 80 (100 is a later wave's, greyed) --------
.view:
    cmp al, PXV_ROWS0
    jae .out                        ; Rows: 80 is the one item that is live,
                                    ; and it is already in force
    mov [px_sizeix], al
    mov byte [px_pend], 1
.save:
    call px_set_save                ; PXSTEIN.CFG, on the UI task (pxset.inc)
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Menus, strings, the template, the preference
; =============================================================================
; THE BAR IS FOUR CELLS WHEN IT IS FINISHED, AND THREE OF THEM SHIP HERE
; (SPEC.md 97.8): MENU_APPMAX is 5 and the bar drops every cell from the
; first that reaches the clock band, so the row set is decided ONCE - Game
; (Full Screen, Pause; Sound and Mouse land in wave 4) . Mode (the adapter's
; two items) . Detail (the rung axis Auto / Wire / Flat / Textured AND the
; resolution axis Full res / Low res, one pull-down) . View (Size, Rows,
; wave 2). The first cut spent a fourth cell on "Resolution" and promised
; four more rows beside it, which nasm would have refused at wave 2
    OS88_MENUSET px_menus, px_name, px_oncmd
        OS88_MENU px_m_game, px_i_game, 4
        OS88_MENU px_m_mode, px_i_mode, 2
        OS88_MENU px_m_det, px_i_det, 7
        OS88_MENU px_m_view, px_i_view, 7
    OS88_MENUSET_END px_menus

px_name:     db 'Pixelstein 3D', 0
px_m_game:   db 'Game', 0
px_i_game:   dw px_s_gofull, px_s_pause, px_s_sndon, px_s_mouoff
                                            ; item 0's caption is rewritten by
px_s_gofull: db 'Full Screen', 0            ; px_adapter when no mode can be had
px_s_sndon:  db 'Sound: On', 0              ; items 2 and 3 name their state
px_s_sndoff: db 'Sound: Off', 0             ; (an app menu has no check mark,
px_s_mouon:  db 'Mouse: On', 0              ; SPEC.md 12.2 - apps/dotdel's
px_s_mouoff: db 'Mouse: Off', 0             ; reasoning), px_toggle_captions
px_s_gofulln: db 'Full Screen (no mode)', 0 ; (not MENU_DIS: apps/tank's
                                            ; precedent, SPEC.md 97.8 - the
                                            ; item still says why)
px_s_pause:  db 'Pause', 0
px_m_mode:   db 'Mode', 0
px_i_mode:   dw px_s_mnone, px_s_mnone      ; both rewritten by px_adapter
px_m_det:    db 'Detail', 0
px_i_det:    dw px_s_dauto, px_s_dwire, px_s_dflat, px_s_dtex, px_s_rfull, px_s_rlow
             dw px_s_colon          ; rewritten by px_adapter (97.14)
px_s_dauto:  db 'Auto', 0
px_s_dwire:  db 'Wire', 0
px_s_dflat:  db 'Flat', 0
px_s_dtex:   db 'Textured', 0
px_s_dtexs:  db 'Textured (sprites 76 KB)', 0 ; the walls textured and
                                            ; the sprites BOXES: the loader
                                            ; found the 118 KB but not the
                                            ; sprite set's claim after it
                                            ; (97.9: PXS_KB + the shadow) -
                                            ; live, and it says so (SPEC.md
                                            ; 47; review r1: it said nothing)
px_s_dtexn:  db MENU_DIS, 'Textured (needs 118 KB)', 0 ; SPEC.md 47:
                                            ; greyed, and the caption says
                                            ; why - the scratch, the byte
                                            ; set and the art claim (97.9),
                                            ; a FACT held below to the
                                            ; constants it is the sum of
                                            ; (the first cut said 82 with
                                            ; the sum at 111; review, wave 3 -
                                            ; 118 since wave 6's dog: the
                                            ; art claim 30 -> 37 KB)
                                            ; - the %if is after pxart.inc.
                                            ; EVERY caption of the four
                                            ; menus fits MENU_MAXCH = 24
                                            ; glyphs, which a pull-down clips
                                            ; at: this one and the sprites'
                                            ; were 28 and 29 until wave 5's
                                            ; review, the Size items' 26 -
                                            ; tests/pxswin.py counts them all
px_s_rfull:  db 'Full res', 0
px_s_rlow:   db 'Low res', 0
px_s_colon:  db 'Colour: On', 0     ; THE WINDOW'S 16 COLOURS (97.14): what
px_s_coloff: db 'Colour: Off', 0    ; is in force, or why it is not (SPEC.md
px_s_colxt:  db MENU_DIS, 'Colour: 0.4 s a frame', 0   ; 47) - the
px_s_colmono: db MENU_DIS, 'Colour: needs 16 colours', 0  ; 8086's price
                                            ; MEASURED on MartyPC's XT-VGA at
                                            ; the DEAREST rung a window can
                                            ; be put on, Textured Full - a
                                            ; fact about the item, not about
                                            ; its cheapest rung (review, wave
                                            ; 5), and on the PLANAR present
                                            ; (OSAPI_GFX_BLITP) an unobscured
                                            ; window takes: 438.4 ms (it was
                                            ; 952.0 through BLIT4)
                                            ; - and tests/pxswin.py --price
                                            ; holds it within 15% (952 ms
                                            ; against WIN1's 164),
                                            ; the other the display's own
                                            ; fact. BOTH UNDER MENU_MAXCH = 24
                                            ; glyphs: a pull-down clips past
                                            ; that, and the plan's "... on
                                            ; this CPU" was 33 (the gate
                                            ; counts them)
px_m_view:   db 'View', 0
px_i_view:   dw px_s_v48, px_s_v56, px_s_v64, px_s_v72, px_s_v80, px_s_r80, px_s_r100
px_s_v48:    db 'Size 48', 0
px_s_v56:    db 'Size 56', 0
px_s_v64:    db 'Size 64', 0
px_s_v72:    db MENU_DIS, 'Size 72 (full screen)', 0  ; GREYED (SPEC.md
px_s_v80:    db MENU_DIS, 'Size 80 (full screen)', 0  ; 47): the menu is
                                            ; the window's and a window shows
                                            ; 64 at most (97.3) - the first
                                            ; cut offered them live, and a
                                            ; pick changed the file and not
                                            ; the picture. The V key cycles
                                            ; them in the bracket, where there
                                            ; is no menu
px_s_r80:    db 'Rows 80', 0
px_s_r100:   db MENU_DIS, 'Rows 100 (not built)', 0 ; greyed: not built - a
                                            ; fact about the software, the
                                            ; only one there is until it
                                            ; exists (97.3)
px_s_nomem:  db 'Not enough memory for the picture (16 KB)', 0
px_ttl:      db 'Pixelstein 3D', 0

px_tpl:
    dw 0, 0, PX_WINW, PX_WINH
    dw px_ttl, px_paint, px_onkey, px_onclick

    OS88_PREFER px_pref, PX_WINW, PX_WINH, PX_WINW, PX_WINH, PX_WINW, PX_WINH

; px_toggle_captions - the Game menu's Sound and Mouse items say what is ON
px_toggle_captions:
    mov word [px_i_game + 4], px_s_sndon
    cmp byte [px_sound], 0
    jne .m
    mov word [px_i_game + 4], px_s_sndoff
.m:
    mov word [px_i_game + 6], px_s_mouoff
    cmp byte [px_mouse], 0
    je .out
    mov word [px_i_game + 6], px_s_mouon
.out:
    ret

; px_tex_caption - the Detail row's Textured item names what it is, or why
;                  it is not (SPEC.md 47): the parts that arrived decide,
;                  and so does the generator's fence FOR THIS BACKEND's
;                  phase class (px_genbad, pxgen.inc) - a class the part
;                  could not hold is greyed while that class is in force
;                  and offered again on a Mode change to one that fits
px_tex_caption:
    push ax
    push bx
    mov ax, px_s_dtexn
    cmp byte [px_texok], 0
    je .say
    mov bl, [px_back]
    xor bh, bh
    mov bl, [px_phcls + bx]
    cmp bl, [px_genbad]
    je .say
    mov ax, px_s_dtexs              ; the sprite set refused: the item is
    cmp word [px_hand + PXH_SPR], 0 ; live and names the boxes' price
    je .say
    mov ax, px_s_dtex
.say:
    mov [px_i_det + PXD_TEX * 2], ax
    pop bx
    pop ax
    ret

; =============================================================================
; 'About Pixelstein 3D' - the credit card (SPEC.md 12.2, 20.5.1)
; =============================================================================
px_about:
    push bx
    push si
    mov byte [px_abon], 1
    mov bx, si
    mov si, px_ablines
    call os88ui_about               ; arms the clip itself
    pop si
    pop bx
    ret

; px_abdismiss - take the card down if it is up. out: CF = 1 the key or click
;                was spent doing it
px_abdismiss:
    cmp byte [px_abon], 0
    je .none
    push bx
    mov byte [px_abon], 0
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET          ; nothing has armed a region for a click
    jc .gone                        ; or a key (SPEC.md 11.3)
    push si
    mov si, [px_win]
    call px_paint
    pop si
.gone:
    pop bx
    stc
    ret
.none:
    clc
    ret

px_ablines:
    dw px_ab1, px_ab2, px_ab3, px_ab4, px_ab5, px_ab6, px_ab7, px_ab8, px_ab9
    dw px_ab10, 0                   ; (TEN lines: the CGA desktop's content
                                    ; box clips an eleventh - review r2's
                                    ; screendump of the first eleven)
px_ab1:      db 'Pixelstein 3D  version 1.0', 0     ; THE NUMBERS ARE MEASURED
px_ab2:      db 'A raycast shooter in the shape of', 0   ; (SPEC.md 97.13):
px_ab3:      db 'the 1992 one: eight floors, guards,', 0 ; MartyPC's cycle-
px_ab4:      db 'dogs, doors and keys, for the 8088.', 0 ; exact 5150 CGA. The
px_ab5:      db 0                                        ; FINISHED frame on
px_ab6:      db 'A 4.77 MHz 5150 with CGA plays 8.5', 0  ; scene A (the sim
px_ab7:      db 'fps at Textured Low res 64x80 (the', 0  ; running - what a
px_ab8:      db 'finished frame, the world running);', 0 ; player sees; review
px_ab9:      db 'the timedemo (T on the title page)', 0  ; r2: the first cut
px_ab10:     db 'walks a frozen world at 9.9 fps.', 0  ; quoted a frozen
                                                         ; repaint), and the
                                                         ; timedemo's card -
                                                         ; the one a field
                                                         ; owner compares
                                                         ; against. Wave 6
                                                         ; re-measured both
                                                         ; on the last build
                                                         ; (SPEC.md 97.15)

; =============================================================================
; the modules
; =============================================================================
%include "pxtab.inc"
%include "pxlev.inc"
%include "pxcast.inc"
%include "pxgen.inc"
%include "pxcomp.inc"
%include "pxspr.inc"
%include "pxrast.inc"
%include "pxwin.inc"
%include "pxgame.inc"
%include "pxact.inc"
%include "pxset.inc"
%include "pxhuda.inc"
%include "pxhud.inc"
%include "pxhs.inc"
%include "pxart.inc"
; THE GREYED CAPTION IS A FACT (SPEC.md 47): held to the three constants it
; is the sum of, here because two of them are pxart.inc's
%if PX_GENKB_CAP + PXA_BTKB + PXA_KB != 118
%error "the greyed Textured caption names a number that is not PX_GENKB + PXA_BTKB + PXA_KB: fix px_s_dtexn"
%endif
%if PXS_KB + PX_SHKB != 76
%error "the sprites' caption names a number that is not PXS_KB + PX_SHKB: fix px_s_dtexs"
%endif
; THE LOADER RESTATES PX_SHKB as PXL_SHKB (pxstein.asm's sprite claim leaves
; room for the shadow after it); held here as PX_GENKB is held above
%if PX_SHKB != 16
%error "restate pxstein.asm's PXL_SHKB: PX_SHKB moved"
%endif
; THE EMISSION FENCE TRACKS THE PHASE (pxgen.inc's PXG_EMITMAX; review, wave
; 2): the tallest Low-res scaler is a load a texel, a store a row, a phase a
; texel run and two `mov ah, al` a run, plus its ret - and the fence is only
; a fence while that sum stays under it. Here, after pxart.inc, because
; PXA_TEX is its
%if PXA_TEX * 4 + PX_ROWS * 4 + PXA_TEX * PXG_PHMAX + 2 * PXA_TEX * 2 + 1 > PXG_EMITMAX
%error "PXG_EMITMAX is under the tallest Low-res scaler: the generator's fence would let one emission end past part 1"
%endif
%include "os88pit.inc"

; =============================================================================
; .bss (SPEC.md 20.5): an equ chain from os88_image_end, THE HANDOFF FIRST
; (97.9: pxstein.asm writes it at the head of this bss by the header's own
; image size, so it must be the first thing here)
; =============================================================================
%assign PX_BSS 0
%macro ZWORD 1
%1 equ os88_image_end + PX_BSS
%assign PX_BSS PX_BSS + 2
%endmacro
%macro ZBYTE 1
%1 equ os88_image_end + PX_BSS
%assign PX_BSS PX_BSS + 1
%endmacro
%macro ZBUF 2
%1 equ os88_image_end + PX_BSS
%assign PX_BSS PX_BSS + (%2)
%endmacro

    ZBUF  px_hand, PXH_SIZE
; --- the two map layouts and their marks (97.2): mapT then its spotvis, map
;     then its spotvis, so a walker's mark is its cell + PX_SPOTD ----------
%assign PX_MAPT_AT PX_BSS          ; ...and where they fall, for the guard below
    ZBUF  px_mapT, 4096
    ZBUF  px_spotT, 4096
    ZBUF  px_map, 4096
%assign PX_SPOT_AT PX_BSS
    ZBUF  px_spot, 4096
; --- the column arrays (97.2.5) ---------------------------------------------
    ZBUF  px_top, PX_COLMAX
    ZBUF  px_bot, PX_COLMAX
    ZBUF  px_mat, PX_COLMAX
    ZBUF  px_side, PX_COLMAX
    ZBUF  px_u, PX_COLMAX
    ZBUF  px_wallh, PX_COLMAX * 2
    ZBUF  px_h, PX_COLMAX           ; the QUANTISED height under Textured
                                    ; (97.3): the scaler the compose calls
; --- last frame's memory, a set per page (97.5) ------------------------------
    ZBUF  px_lu, PX_COLMAX * 2      ; ...and the texture column, Textured's
    ZBUF  px_lh, PX_COLMAX * 2      ; ...and its quantised height: eight of
                                    ; the 47 scalers clip to the same top/bot
                                    ; (h >= 80), so without this a wall walked
                                    ; at stopped zooming (review, wave 2)
    ZBUF  px_ltop, PX_COLMAX * 2
    ZBUF  px_lbot, PX_COLMAX * 2
    ZBUF  px_lmat, PX_COLMAX * 2    ; ...and the tone, so a Flat column that
    ZBUF  px_lside, PX_COLMAX * 2   ; did not change is skipped whole
    ZBUF  px_lu0, PX_COLMAX * 2
    ZBUF  px_lu1, PX_COLMAX * 2
    ZBUF  px_ll0, PX_COLMAX * 2
    ZBUF  px_ll1, PX_COLMAX * 2
; --- the cast's per-frame and per-column words --------------------------------
    ZBUF  px_xpq, 8
    ZBUF  px_ypq, 8
    ZBUF  px_vbase, 8
    ZBUF  px_hbase, 8
    ZBUF  px_ckey, 8
    ZBUF  px_bkey, 8
    ZBUF  px_qoff, 8                ; the four bodies' offsets
    ZBUF  px_qcur, 4                ; (off, seg) of this column's body...
    ZBUF  px_qcurv, 4               ; ...and its two pass tails
    ZBUF  px_qcurh, 4
    ZWORD px_bseg
    ZWORD px_col
    ZWORD px_qs2
    ZWORD px_tanv
    ZWORD px_tanh
    ZWORD px_hx
    ZWORD px_hy
    ZBYTE px_hu
    ZBYTE px_hmat
    ZBYTE px_hside
    ZBYTE px_q
    ZBYTE px_gen
; --- the eye ----------------------------------------------------------------
    ZWORD px_px
    ZWORD px_py
    ZWORD px_head
    ZWORD px_hcos
    ZWORD px_hsin
; --- the compose and the present ---------------------------------------------
    ZWORD px_dseg
    ZWORD px_dbase
    ZWORD px_pgofs
    ZWORD px_cdi
    ZWORD px_cbias                  ; px_x0 - 256: the compose's column bias
                                    ; (the ladders' -256, px_res_apply)
    ZWORD px_fanp
    ZWORD px_ladp
    ZWORD px_gsc                    ; this resolution's scaler directory
    ZBYTE px_cols
    ZBYTE px_bpc
    ZBYTE px_size                   ; the band's bytes (Size), and its first
    ZBYTE px_x0                     ; byte (80 - Size) / 2, and the present's
    ZBYTE px_sadv                   ; row advance past it (80 - Size)
    ZBYTE px_ush                    ; u's shift to a texture column: 3, or 4
                                    ; on C160 (a column is a texel pair)
    ZBYTE px_sizeix                 ; the Size row's pick (px_sizes)
    ZBYTE px_rowsix                 ; the Rows row's (0 = 80; 100 is later)
    ZBYTE px_modepick               ; the Mode row's item, for the file
    ZBYTE px_repaint                ; windowed: the black around the band is
                                    ; owed (a narrower Size)
    ZBYTE px_lastx0                 ; the band's first byte the shadow was
                                    ; last cleared and the glass blanked FOR
                                    ; (0xFF: neither, yet) - px_apply pays
                                    ; both only when the band moved (review,
                                    ; wave 2: an Auto step paid 43 ms of
                                    ; clearing for a rung change)
    ZWORD px_cbase                  ; px_dbase + px_cbias, summed once a
                                    ; compose (the page's base and the band's
                                    ; first byte less the ladders' 256)
; --- the generator and the transpose (97.3, 97.4; pxgen.inc) ----------------
    ZBYTE px_texok                  ; every part the Textured rung needs came
    ZBYTE px_genback                ; the phase class the scalers were made for
    ZBYTE px_genbad                 ; ...and the phase class the part could
                                    ; NOT hold (0xFF: none): a refusal is the
                                    ; class's, and a Mode change to another
                                    ; retries (px_tex_setup)
    ZBYTE px_btback                 ; the ink class the byte set was made for
    ZBUF  px_drvp, 4                ; (PXG_DRV, part 1): the driver's far entry
    ZBUF  px_drv2p, 4               ; ...and the sprite pass's (PXG_DRV2)
    ZWORD px_qp                     ; the draw queue's write pointer
    ZWORD px_gcot                   ; the codeofs table being reserved (97.3)
    ZBUF  px_gcotv, (PXA_TEX + 1) * 2   ; ...and the loads it is built from
    ZBYTE px_gphn                   ; the phase's byte count...
    ZBUF  px_gph, 4                 ; ...and bytes
    ZBYTE px_gword                  ; the set being emitted: 0 byte, 1 word
    ZBYTE px_gv                     ; the texel being emitted
    ZWORD px_gdir                   ; the directory being filled
    ZWORD px_gtop                   ; the scaler's top row, signed
    ZWORD px_gr1                    ; the run's row past the last
    ZWORD px_genend                 ; one past the last generated byte
    ZWORD px_glim                   ; ...and the most DI may hold before an
                                    ; emission: the part's end (PXH_GENLEN)
                                    ; less PXG_EMITMAX
    ZBUF  px_gent, PXG_DIRSZ        ; a generated height's entry, by h
    ZBUF  px_sctab, PXG_DIRSZ * 2   ; the two scaler directories, Full then Low
    ZBUF  px_c2t, PXG_DIRSZ * 2     ; ...and the two col2tex directories
    ZBYTE px_sound                  ; wave 4's two bytes, kept in the file
    ZBYTE px_mouse
    ZBYTE px_cfgdirty               ; a V key in the bracket: write on leaving
    ZBYTE px_setread                ; the file was read (once)
    ZWORD px_sdclus                 ; px_set_enter's way home
    ZBYTE px_sddrv
    ZBUF  px_sdfind, 32             ; OSAPI_FILE_FIND's row
    ZBUF  px_setbuf, PX_SETFSZ      ; the file as last read or written
    ZBUF  px_setnew, PX_SETFSZ      ; ...and the record being written, until
                                    ; the write says it landed (pxset.inc)
    ZWORD px_bink                   ; the transpose's ink table
    ZWORD px_bmat                   ; ...and the master it is on
    ZBUF  px_bstage, PXA_SPRMSZ     ; ...staged here (one master: a wall's
                                    ; 512 bytes or a sprite's 640)
    ZBYTE px_bcnt                   ; the sprite transpose: frames left...
    ZBYTE px_bcols                  ; ...a frame's columns
    ZWORD px_bmsz                   ; ...its master's bytes
    ZWORD px_balpha                 ; ...where its alpha bits begin
    ZWORD px_brtab                  ; ...and the run table being filled
    ZBYTE px_sprok                  ; the sprite set stands for this ink class
    ZBYTE px_sprback                ; ...the class it was built for (0xFF none)
    ZBYTE px_r0
    ZBYTE px_r1
    ZBYTE px_page
    ZBYTE px_wclip
    ZBUF  px_inkt, 64
    ZWORD px_inkc
    ZWORD px_inkf
    ZWORD px_inke
    ZWORD px_inkdl
    ZWORD px_inkdd
    ZWORD px_inks                   ; a sprite's silhouette tone (97.6)
    ZWORD px_inkw                   ; the DIE wash's (97.8)
    ZBUF  px_fsi, FSI_SIZE
    ZBUF  px_devoff, PX_ROWS * 2
    ZBUF  px_hudoff, PX_HUDROWS * 2 ; ...and the bar's rows after them: ONE
                                    ; table px_devrows walks on into (97.13)
    ZWORD px_shseg
; --- the window --------------------------------------------------------------
    ZWORD px_win
    ZWORD px_scrw
    ZWORD px_fmid                   ; the bracket's middle x, OSAPI_FSX_SURF's
                                    ; (px_fsx_main): the mouse steers from it
    ZWORD px_dock
    ZWORD px_cx
    ZWORD px_cy
    ZWORD px_cw
    ZWORD px_ch
    ZWORD px_bx
    ZWORD px_by
    ZWORD px_ly
    ZWORD px_caps
    ZBYTE px_bpp
    ZBYTE px_vkind
    ZBYTE px_fsxm
    ZBYTE px_mode
    ZBYTE px_mode0
    ZBYTE px_mode1
    ZBYTE px_back
    ZBYTE px_tier
    ZBYTE px_abon
    ZBYTE px_lined
    ZBYTE px_spawned
    ZBYTE px_hasfoc
    ZBYTE px_composing
    ZBYTE px_hidden                 ; not a pixel showing: no frame until a
                                    ; paint or a resize says otherwise
    ZBYTE px_whole                  ; a paint blackened the glass: the next
                                    ; blit sends the whole band (97.5)
    ZBYTE px_llen                   ; the text line's last length: the pad
    ZBUF  px_sbuf, 9                ; the eight stat cells and a NUL
    ZBUF  px_lbuf, 96               ; (69 with wave 3's health and ammo on
                                    ; it: the 64 of wave 2 overflowed into
                                    ; px_last and px_frames)
; --- the session --------------------------------------------------------------
    ZWORD px_last
    ZWORD px_frames
    ZBUF  px_t0, 4
    ZBUF  px_ftime, 4               ; the frame's WORK (97.8): cast, compose
    ZBUF  px_twait, 4               ; and present, less the waits px_wait_*
    ZBUF  px_tw0, 4                 ; bracketed inside it (a retrace, a lock)
    ZBYTE px_inbr
    ZWORD px_brn                    ; brackets left (px_go_fsx): the gates'
                                    ; marker that the exit path is done
    ZBYTE px_fsx
    ZBYTE px_fsxq
    ZBYTE px_quit
    ZBYTE px_pause
    ZBYTE px_dirty
    ZBYTE px_force
    ZBYTE px_kturn
    ZBYTE px_kfwd
    ZBYTE px_kstr
    ZBYTE px_tap
    ZBYTE px_rung
    ZBYTE px_lowres
    ZBYTE px_detail
    ZBYTE px_res
    ZBYTE px_apos
    ZBYTE px_astart                 ; the position the tier started at: a
                                    ; step up never passes it (97.8)
    ZBYTE px_amiss
    ZBYTE px_ahit
    ZWORD px_ahold                  ; the tick a step up is allowed from
    ZBYTE px_pend                   ; a Detail/Resolution pick awaiting a frame
; --- the world (97.6, 97.7, 97.8; wave 3) -----------------------------------
    ZBUF  px_doors, PX_MAXDOORS * PXD_SIZE
    ZBUF  px_drow, 65               ; the doors of map row y are px_drow[y]
                                    ; .. px_drow[y + 1] - 1 (97.7's sorted
                                    ; stream; px_door_of)
    ZBUF  px_act, PX_MAXACT * PXAC_SIZE
    ZBUF  px_stat, PX_MAXSTAT * PXT_SIZE
    ZBYTE px_ndoors
    ZBYTE px_nact
    ZBYTE px_nstat
    ZBYTE px_floor                  ; the level in force (0-based)
    ZBYTE px_levnext                ; a floor to load between frames (0xFF:
                                    ; none; the switch, or the DIE restart)
    ZBYTE px_simoff                 ; the gates' word: no door, actor or
                                    ; weapon moves (the sim's steps still
                                    ; walk the player)
    ZBYTE px_god                    ; ...and no damage to the player
    ZBYTE px_health
    ZBYTE px_ammo
    ZBYTE px_keys                   ; bit 0 gold, bit 1 silver
    ZBYTE px_weapon                 ; the weapon chosen (PXW_*)
    ZBYTE px_wframe                 ; its frame (0 ready, 1 fire, 2 recoil)
    ZBYTE px_wtimer                 ; ticks left in that frame
    ZBUF  px_wdrawn, 2              ; the weapon frame on each PAGE's glass
                                    ; (0xFF: none yet), so a still one is
                                    ; not redrawn (97.6)
    ZBYTE px_wnext                  ; ...and the frame this frame shows
    ZBYTE px_wtouch                 ; ...and the check erased another (wave 5)
    ZBYTE px_spg                    ; the page this frame is on (0, 1)
    ZBYTE px_firek                  ; Ctrl was down last tick (one shot a
                                    ; press for the pistol and the knife)
    ZBYTE px_usek                   ; Space was down last tick
    ZBYTE px_lives
    ZBYTE px_state                  ; PXST_*
    ZBYTE px_fade                   ; ticks of the DIE wash left
    ZBYTE px_fadedrawn              ; ...and whether the wash is on the glass
    ZWORD px_score
    ZWORD px_pcell                  ; the player's cell (its PXC_PLAYER mark)
    ZWORD px_dtick                  ; the world's tick count (the frame
                                    ; counter's twin, for the rows)
    ZBYTE px_aim                    ; the actor under the crosshair after the
                                    ; last sprite pass: 0xFF none (97.6)
    ZWORD px_aimh                   ; ...and its height (the knife's reach)
    ZBYTE px_nsc                    ; sprite candidates this frame
    ZBUF  px_sc, PX_MAXSPR * PXS_C_SIZE
    ZBUF  px_scn, PXS_C_SIZE        ; the candidate being built
    ZWORD px_sfr                    ; the post walk: the frame's base in
                                    ; the sprite claim (PXH_SPR)...
    ZWORD px_scot                   ; ...the scaler's codeofs table
    ZWORD px_sc2t                   ; ...its col2tex block (the width byte)
    ZWORD px_sdi                    ; ...the column's DI
    ZWORD px_stop                   ; ...the scaler's top row, signed
    ZBYTE px_shq                    ; ...the quantised height
    ZBYTE px_sflags                 ; ...the candidate's flags
    ZBYTE px_scol                   ; ...the screen column
    ZBYTE px_ssrc                   ; ...the source column
    ZWORD px_sdx                    ; the transform's deltas and nx
    ZWORD px_sdy
    ZWORD px_snx
    ZWORD px_ssi                    ; ...its texel column's base in the set
    ZBUF  px_sruns, PXS_RUNSZ + 1   ; a column's run table, copied in
    ZBUF  px_cwrote, PX_COLMAX          ; the compose wrote column c this frame:
                                    ; px_cwrote[c] == px_gen (97.6)
    ZBUF  px_swrote, PX_COLMAX          ; ...and a sprite DREW on it this frame
                                    ; (px_spr_stamp; review r1)
    ZWORD px_ccen                   ; the centre column (the aim's)
    ZWORD px_wc0                    ; the weapon's ray columns, [wc0, wc1)
    ZWORD px_wc1
    ZBYTE px_wpnhit                 ; a sprite drew over the weapon's rows on
                                    ; one of its columns this frame
    ZBYTE px_swpn                   ; the sprite set up reaches the weapon's rows
    ZBUF  px_statT, PX_MAXSTAT * 2  ; every static's cell in the TRANSPOSED
                                    ; layout, computed at load (the gather's
                                    ; probe; review r1)
    ZBUF  px_lsc, 2 * PX_MAXSPR * PXS_C_SIZE   ; the sprites on each page's
                                    ; glass, as last drawn (97.6)
    ZBUF  px_lnsc, 2                ; ...how many, per page
    ZBYTE px_srow0                  ; a silhouette's first and last row
    ZBYTE px_srow1
    ZWORD px_gc2t                   ; this resolution's col2tex directory
    ZWORD px_slos                   ; the LOS walk's scratch: the slope...
    ZWORD px_sfrac                  ; ...and the fraction
    ZWORD px_lx2                    ; ...the target
    ZWORD px_ly2
    ZBYTE px_asdx                   ; the signed tile deltas to the player
    ZBYTE px_asdy
    ZBYTE px_aspeed                 ; the mover's step this tick
    ZBYTE px_restart                ; the floor loads for a death (1) or the
                                    ; switch (0)
    ZWORD px_acur                   ; the actor being stepped (pxact.inc)
    ZBYTE px_adx                    ; ...and the tile deltas to the player
    ZBYTE px_ady
    ZBYTE px_adist
    ZBYTE px_hitdmg                 ; a hit's damage, banked across a call
    ZBYTE px_moved                  ; px_reloc has run this many times: the
                                    ; compaction gate's proof (tests/pxsmove.py)
; --- the bar and the cards (97.13; pxhud.inc) ---------------------------------
    ZWORD px_gputp                  ; the backend's writer...
    ZWORD px_hlay                   ; ...the bar's layout
    ZBYTE px_gcb                    ; ...a cell's bytes
    ZBYTE px_hx0                    ; ...the bar's first byte
    ZBYTE px_hrows                  ; ...its rows (24; 20 on C160)
    ZBUF  px_hinks, 4               ; ...its four inks
    ZBYTE px_gink                   ; the writer's ink this call
    ZBYTE px_gdim                   ; ...Mode X's bytes a row
    ZBUF  px_glyb, 8                ; a glyph, staged out of the kernel's face
    ZWORD px_gtab                   ; OSAPI_FONT_GLYPHS: the table...
    ZWORD px_gseg
    ZBYTE px_gfirst                 ; ...and its range (one word store:
    ZBYTE px_glast                  ; adjacent, px_font_init)
    ZBUF  px_hudv, 2 * PXF_N * 2    ; each field as last drawn, per PAGE
    ZBUF  px_hsnap, 2 * PXF_SNAP    ; ...the inputs they were drawn from
    ZBUF  px_hsnow, PXF_SNAP        ; ...and the inputs now
    ZBYTE px_hudd                   ; the bar owes a look (px_hud_poll)
    ZBYTE px_hudw                   ; a paint blackened it: blit it whole
    ZWORD px_hudn                   ; field rewrites (tests/pxshud.py reads)
    ZBYTE px_hr0                    ; this frame's bar rectangle: rows...
    ZBYTE px_hr1
    ZBYTE px_hb0                    ; ...and bytes
    ZBYTE px_hb1
    ZBUF  px_hrq, 4 * PXF_N          ; ...and each field's own, for the shadow
    ZBYTE px_hrqn                   ;    presents (0xFF: overflowed, the union)
    ZBUF  px_hnum, 6                ; a number's digits, right to left
    ZBUF  px_hcell, 2 * 14          ; each number's cells as last put, per
                                    ; PAGE (PX_HCELLS, rounded to words:
                                    ; px_hud_setup fills it a word at a time)
    ZWORD px_hcp                    ; px_hf_num's walk through it
    ZBUF  px_hmbuf, 24              ; a message, built
    ZBYTE px_hmsg                   ; the left label row's message (PXM_*)
    ZBYTE px_hmsgt                  ; ...ticks it stands (0: sticky)
    ZBYTE px_hmsgn                  ; ...its post's serial, in steps of 8
                                    ; (over a PXM_* of 0..7)
    ZBYTE px_wrs                    ; worker restarts at px_worker_rs (66.6.2)
    ZWORD px_sfxn                   ; OSAPI_SND_TONE effects played (px_sfx)
    ZBYTE px_sfxl                   ; ...the last one's PXSFX_*
    ZBYTE px_hmsgw                  ; ...one owed, posted by px_timers
    ZBYTE px_grin                   ; ticks of the face's grin
    ZBYTE px_cardd                  ; the card is owed (2: both Mode X pages)
    ZBYTE px_cardl                  ; ...one line of it alone (line + 1)
    ZBUF  px_cbuf, PXCD_LINES * PXCD_LW
; --- the states (97.13; pxgame.inc) ------------------------------------------
    ZBYTE px_stimer                 ; READY's and OVER's clock
    ZBYTE px_cardhold               ; ticks a new card ignores its keys
    ZBYTE px_newgame                ; the floor that loads next starts a game
    ZBYTE px_victory                ; the last floor was left: YOU ESCAPED
    ZBYTE px_fworld                 ; this frame composed the world
    ZBYTE px_vcur                   ; Mode X: bit p set, page p shows the pose
    ZBYTE px_vcatch                 ; ...a catch-up owed (px_modex_hud)
    ZBYTE px_fcatch                 ; ...and taken by this frame
    ZBYTE px_fown                   ; ...a force was owed at its start
    ZBYTE px_fdo                    ; ...a force or a dirty was
    ZBYTE px_ckill                  ; the floor's ratios: found...
    ZBYTE px_nkill                  ; ...of
    ZBYTE px_csec
    ZBYTE px_nsec
    ZBYTE px_ctreas
    ZBYTE px_ntreas
    ZWORD px_ltick0                 ; the floor's first tick (px_dtick)
    ZWORD px_ltime                  ; ...and its length, at the switch
; --- the scores and the codes (97.13; pxhs.inc) -------------------------------
    ZBUF  px_hs, PX_NHS * 2         ; the scores, best first...
    ZBUF  px_hsn, PX_NHS * 3        ; ...and the initials (adjacent: one copy)
    ZBUF  px_hsbuf, PX_HSFSZ        ; the file, staged
    ZBUF  px_ini, 4                 ; the initials being typed...
    ZBYTE px_inip                   ; ...and how many
    ZBUF  px_code, 4                ; a code being typed...
    ZBYTE px_codep                  ; ...how many (0xFF not taking one,
                                    ; 0xFE the last one was bad)
; --- the timedemo (97.13) ------------------------------------------------------
    ZBYTE px_demoreq                ; start one between frames
    ZBYTE px_lrefuse                ; the line says a refusal once: 1 'No
                                    ; full-screen mode', 2 'Pause is for play'
    ZBYTE px_demoab                 ; ...end this one, a key was pressed
    ZBYTE px_demodone               ; the attract card shows its numbers
    ZBYTE px_demon                  ; steps left in this run
    ZWORD px_demokt                 ; ...its turn (low) and walk (high)
    ZWORD px_demoi                  ; the script's next run
    ZWORD px_demof0                 ; px_frames and the ticks at the start
    ZWORD px_demot0
    ZWORD px_demof                  ; ...and the numbers: frames, ticks,
    ZWORD px_demot
    ZWORD px_demofps                ; fps in tenths
    ZBUF  px_demosv, 3              ; Detail, Res, Size in force before a run
    ZBYTE px_demosvd                ; ...kept (px_demo_restore owes them back)
    ZBUF  px_demorg, 3              ; the rung, resolution and Size it drew at
; --- the 16-colour window (97.14; wave 5) ---------------------------------------
    ZBYTE px_colour                 ; Detail > Colour: the player's pick
    ZBYTE px_scard                  ; the shadow's band holds a CARD's bits
                                    ; (WIN4 blits it as WIN1 does, 97.14)
    ZBYTE px_w4r                    ; the WIN4 present's next strip row...
    ZBYTE px_w4e                    ; ...and its last
    ZWORD px_nb4                    ; OSAPI_GFX_BLIT4 calls made (the gates')
    ZWORD px_nbp                    ; ...and OSAPI_GFX_BLITP calls drawn
    ZBYTE px_w4pl                   ; this frame's WIN4 present is PLANAR
    ZWORD px_npaint                 ; W_PAINTs taken (the gates')
; --- the Tab map (97.6; wave 6) -----------------------------------------------
    ZBUF  px_seen, 512              ; a bit a cell: crossed by a ray this
                                    ; floor (px_seen_fold, at every wrap)
    ZBYTE px_mapon                  ; the map is up
    ZBYTE px_mapd                   ; ...and owed (2: both Mode X pages)
    ZBYTE px_mapfa                  ; ...and taken down: px_force_all owed
    ZBYTE px_mox                    ; its origin cell, x (signed) and y
    ZBYTE px_moy
    ZBYTE px_mhi                    ; ...and the band byte its map columns end at
    ZWORD px_mmark                  ; ...and the player's marker tone
    ZBUF  px_mrows, 3 * PX_MRW      ; ...and three rows of seen flags
%ifdef PXPROBE
    ZWORD px_pr_lad                 ; tests/pxsperf.py --probe: the frame's
    ZWORD px_pr_skip                ; ladder entries and skipped columns. A
%endif                              ; build without the define is byte for
                                    ; byte the shipped one (t_pxsscale)

%define OS88UI_ABOUT                ; the standard About card...
%define OS88UI_NOBTN                ; ...and NOTHING else: a game's chrome is
%include "os88ui.inc"               ; its own

    OS88_BSS PX_BSS
    OS88_IMAGE_END

; THE FOUR MAP ARRAYS SIT INSIDE THE FAR KEYS' BAND (SPEC.md 97.2.3): a live
; walker's pointer runs from its array's base less the 191-byte overrun to its
; end plus it, and its key - parked at PX_FARJA or PX_FARJB and moved 64 a
; pass, the wrong way in two quadrants - must never cross that range in the
; 63 passes the solid border allows. This is also 97.2.3's "256 bytes from
; either end of the segment", asserted
; px_devrows WALKS ON from the view's rows into the bar's (97.13)
%if px_hudoff - px_devoff != PX_ROWS * 2
%error "px_hudoff must follow px_devoff: px_devrows fills them as one table"
%endif
%if px_glast - px_gfirst != 1
%error "px_glast must follow px_gfirst: px_font_init stores them as one word"
%endif
%if px_hr1 - px_hr0 != 1 || px_hb1 - px_hb0 != 1
%error "px_hr1/px_hb1 must follow px_hr0/px_hb0: px_hud_draw and px_hrq_add move each pair as one word"
%endif
%if px_hsn - px_hs != PX_NHS * 2
%error "px_hsn must follow px_hs: pxhs.inc copies them as one run"
%endif
%if OS88_IMAGE_SIZE + PX_MAPT_AT < PX_FARJA + 64 * 63 + 256
%error "px_mapT is under the ja far key's band: the cast's parked-walker keys are wrong"
%endif
%if OS88_IMAGE_SIZE + PX_SPOT_AT + 4096 + 256 > PX_FARJB - 64 * 63
%error "px_spot runs into the jb far key's band: the cast's parked-walker keys are wrong"
%endif

; --- AND THE BSS SHIPS INSIDE THE PART (SPEC.md 20.12.10, 51.1.2) -----------
; This image is PART 0 of PXSTEIN.O88 and the kernel does not zero a part, so
; the bss has to BE here - which is also what keeps the loader's handoff at
; its head alive. The row is OP_COMP and most of what follows is a run of
; zeros, which LZ4 packs to almost nothing.
    times OS88_BSS_SIZE db 0

; --- THE COLD PART'S TRIGGER, ENFORCED (SPEC.md 97.9) ------------------------
; The cold part is populated by whichever wave's build would leave part 0 with
; less than 2 KB of APP_MAX_SIZE's 61,440 spare - 59,392 bytes, image and bss
; together. It was a sentence until wave 6's close ended 14 bytes under it, so
; it is an assembly error now: the next growth fails HERE and moves 97.9's
; natural movers (pxhs.inc, px_card_text, the timedemo's script) behind the
; one far call, rather than trimming a feature to squeeze under.
PX_PART0_TRIGGER equ 61440 - 2048
%if OS88_IMAGE_SIZE + OS88_BSS_SIZE > PX_PART0_TRIGGER
%error "part 0 is past 97.9's cold-part trigger (59,392): populate the far-called cold part, do not trim a feature"
%endif
