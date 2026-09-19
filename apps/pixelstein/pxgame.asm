; =============================================================================
; os8088 - apps/pixelstein/pxgame.asm
;
; PIXELSTEIN 3D (SPEC.md 96): a raycast first-person shooter in the shape of
; the 1992 one, on the 8088 this project is calibrated against - fullscreen
; in a foreign mode on every adapter (CGA 320x200x4, the 160x100x16 retime
; on a genuine CGA, the Hercules box, Mode X) and windowed as a 1bpp band.
;
; THIS IS PART 0 OF PXSTEIN.O88 (96.9, 20.12.10): a whole .o88 image, its
; bss shipped inside it, that apps/pixelstein/pxstein.asm - the loader, and
; the image the kernel launches - re-homes the instance onto after reading
; the parts. What the loader learned that this program cannot ask for
; itself is at the head of this bss, px_hand (the PXH_* words).
;
; WAVE 1 (docs/plans/PIXELSTEIN-PLAN.md 7): the cast (pxcast.inc), the Flat
; and Wire rungs on the static ladders (pxgen.inc, pxcomp.inc), the five
; backends' geometry and presents (pxrast.inc), the window and its worker
; (pxwin.inc), the clock, the keys and the player (pxgame.inc). Textures,
; sprites, doors that slide, the HUD, the states and the scores are later
; waves'; the HUD band is black until wave 4 letters it.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Pixelstein 3D', px_entry, OS88_F_ICON, OS88_STACK_256

    OS88_ICON16
%include "pxicon.inc"
    OS88_ICON16_END

; --- the picture (SPEC.md 96.1, 96.3) ----------------------------------------
PX_STRIDE   equ 80                  ; bytes a shadow row, every backend
PX_ROWS     equ 80                  ; view rows
PX_HUDROWS  equ 24                  ; the HUD band under the view (wave 4)
PX_BAND     equ 64                  ; the band's bytes: Size 64
PX_X0       equ 8                   ; ...centred in the row
PX_HORIZON  equ 40                  ; where Wire's ground changes tone
PX_GEOMIN   equ 12                  ; a Flat wall this many rows tall or more
                                    ; whose tone stood writes its two ENDS
                                    ; only (pxcomp.inc's .fgeo, 96.5): the
                                    ; second ladder entry (~280 clk) is paid
                                    ; back by ~11 stores it does not make
PX_COLMAX   equ 80                  ; the column arrays' length (Size 80)
PX_SHKB     equ 16                  ; the shadow claim: 16 KB CLAIMED, of
                                    ; which wave 1 composes 80 x 80 = 6,400
                                    ; bytes (every backend but Mode X, which
                                    ; composes into VRAM). The rest is
                                    ; reserved for wave 2's Rows 100 (8,000)
                                    ; and wave 5's WIN4 - four 25-row 4bpp
                                    ; strips in the spare 6,400 (96.9)
PX_MINDIST  equ 23                  ; nx clamped at 0.09 tiles (Q8.8)
PX_HEIGHTK  equ 51200               ; h = PX_HEIGHTK / nx rows
PX_SPOTD    equ 4096                ; a walker's mark is this far past its cell
PX_CGAROW0  equ 28                  ; the view's first device row, CGA 320x200
PX_HERCROW0 equ 134                 ; ...in the Hercules box (74 + 60)
PX_MXROW0   equ 48                  ; ...on a Mode X page
PX_WINW     equ 528                 ; the window's frame
PX_WINH     equ 150

; --- the session (96.8) ------------------------------------------------------
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
                                    ; machine playing there (96.8)
PX_BUDGET50 equ 74582               ; ...and HALF of it, the step-up line: the
                                    ; rung above measures up to 1.92x this
                                    ; one (Mode X's work), so 60% (1.67x)
                                    ; walked the 8086 off its default (96.8)
PX_AHOLD    equ 182                 ; ticks (10 s) no step up follows a step
                                    ; down: the hysteresis the threshold is not
PX_KA       equ 0x1E                ; A - strafe left
PX_KD       equ 0x20                ; D - strafe right
PX_KW       equ 0x11                ; W - forward
PX_KS       equ 0x1F                ; S - back
PX_KLSH     equ 0x2A                ; the two shifts: run
PX_KRSH     equ 0x36

; --- the backends and the rungs ----------------------------------------------
PXB_NONE    equ 0
PXB_CGA4    equ 1                   ; FSXM_CGA320
PXB_C160    equ 2                   ; FSXM_TEXT80 retimed (SPEC.md 88.15)
PXB_HERC    equ 3                   ; FSXM_HERC, TANK's box
PXB_MODEX   equ 4                   ; FSXM_MODEX, two pages
PXB_WIN1    equ 5                   ; the window's 1bpp band
PXR_WIRE    equ 0                   ; the detail ladder's rungs (96.1)
PXR_FLAT    equ 1
PXD_AUTO    equ 0                   ; the Detail row's items: the rung axis...
PXD_WIRE    equ 1
PXD_FLAT    equ 2
PXD_TEX     equ 3
PXD_RFULL   equ 4                   ; ...and the resolution axis under it
PXD_RLOW    equ 5                   ; (SPEC.md 96.8: one menu, two axes)

; --- the handoff (96.9): one package, two sources - pxstein.asm writes these -
PXH_MAGIC   equ 0
PXH_LEV     equ 2
PXH_GEN     equ 4
PXH_COLD    equ 6
PXH_NLEV    equ 8
PXH_LEVLEN  equ 10
PXH_SIZE    equ 12

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
    mov ax, PX_SHKB                 ; THE SHADOW, BEFORE THE WINDOW (96.9): a
    call OSAPI_MEM_CLAIM            ; refusal is a sentence in the window and
    jc .noshadow                    ; never a black bounce
    mov [px_shseg], dx
.noshadow:
    call px_level_load              ; E1M1 into the two map layouts
    jc .refuse
    call px_gen_init                ; where the bodies run (96.2.1)
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
                                    ; so the loader's CF survives it (96.3)
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
    cmp word [px_shseg], 0
    je .noraster                    ; no shadow: no backend to set up either
    call px_r_setup_win             ; the window's own backend, from the start
.noraster:
    call px_auto_start              ; ...and the rung a window starts at on
                                    ; this tier (96.8): px_apply runs here,
                                    ; and a zeroed rung byte is Wire
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
    mov byte [px_hidden], 0         ; a paint proves the window can be seen
    call px_geom_win
    mov al, CBLACK
    call OSAPI_SET_COLOR
    cmp word [px_shseg], 0
    jne .have
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
    ; NOTHING WRITES A PIXEL TWICE (CLAUDE.md): the band's 512 x 80 comes
    ; whole off the shadow, so the black is painted only where the band is
    ; not - under it (the HUD band and the line's row), and the margin
    ; either side of it, which OSAPI_WM_SNAP makes the right-hand one alone
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
    mov ax, [px_bx]
    add ax, PX_BAND * 8             ; [bx + 512, cx + cw) x the band's rows
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
    call px_spawn_ck                ; the worker starts here, not at entry
    mov byte [px_whole], 1          ; the whole band, whatever the worker's
    cmp byte [px_composing], 0      ; own dirty rows say (96.5)
    jne .line                       ; mid-compose: the worker's next pass
                                    ; blits it (px_render_win's .whole exit -
    call px_blit_win                ; no cast is owed for a blackened glass)
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
    jmp short .out                  ; a menu the set does not have
.game:
    or al, al
    jnz .pause
    call px_go_fsx
    jmp short .out
.pause:
    xor byte [px_pause], 1
    mov byte [px_lined], 1
    jmp short .out
.mode:
    mov bl, [px_mode0]
    or al, al
    jz .m
    mov bl, [px_mode1]
.m:
    or bl, bl
    jz .out                         ; "No second mode": nothing to pick
    mov [px_mode], bl
    call px_adapter                 ; ...and its FSXM_*
    jmp short .out
.detail:
    cmp al, PXD_TEX
    je .out                         ; greyed (MENU_DIS: "Textured (wave 2)")
                                    ; - the kernel never dispatches it, and
                                    ; this return is for a shortcut, which
                                    ; goes near no menu
    cmp al, PXD_RFULL
    jae .res
    mov [px_detail], al
    mov byte [px_pend], 1
    jmp short .out
    ; --- the resolution axis of the same menu: Full res / Low res ---------
.res:
    sub al, PXD_RFULL               ; Full res = 0, Low res = 1
    mov [px_res], al
    cmp byte [px_detail], PXD_AUTO
    jne .rpend
    ; under Auto the pick RE-SEATS the position (SPEC.md 96.8): Full is
    ; position 0, Low res position 1 (wave 2's Textured pair shifts both).
    ; Otherwise the item changed nothing and said nothing, which SPEC.md 47
    ; forbids
    mov bl, al
    mov [px_apos], bl
    mov [px_astart], bl             ; ...and it is the new ceiling (96.8)
    mov byte [px_amiss], 0
    mov byte [px_ahit], 0
.rpend:
    mov byte [px_pend], 1           ; applied between frames (px_frame_begin)
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Menus, strings, the template, the preference
; =============================================================================
; THE BAR IS FOUR CELLS WHEN IT IS FINISHED, AND THREE OF THEM SHIP HERE
; (SPEC.md 96.8): MENU_APPMAX is 5 and the bar drops every cell from the
; first that reaches the clock band, so the row set is decided ONCE - Game
; (Full Screen, Pause; Sound and Mouse land in wave 4) . Mode (the adapter's
; two items) . Detail (the rung axis Auto / Wire / Flat / Textured AND the
; resolution axis Full res / Low res, one pull-down) . View (Size, Rows,
; wave 2). The first cut spent a fourth cell on "Resolution" and promised
; four more rows beside it, which nasm would have refused at wave 2
    OS88_MENUSET px_menus, px_name, px_oncmd
        OS88_MENU px_m_game, px_i_game, 2
        OS88_MENU px_m_mode, px_i_mode, 2
        OS88_MENU px_m_det, px_i_det, 6
    OS88_MENUSET_END px_menus

px_name:     db 'Pixelstein 3D', 0
px_m_game:   db 'Game', 0
px_i_game:   dw px_s_gofull, px_s_pause     ; item 0's caption is rewritten by
px_s_gofull: db 'Full Screen', 0            ; px_adapter when no mode can be had
px_s_gofulln: db 'Full Screen (no mode)', 0 ; (not MENU_DIS: apps/tank's
                                            ; precedent, SPEC.md 96.8 - the
                                            ; item still says why)
px_s_pause:  db 'Pause', 0
px_m_mode:   db 'Mode', 0
px_i_mode:   dw px_s_mnone, px_s_mnone      ; both rewritten by px_adapter
px_m_det:    db 'Detail', 0
px_i_det:    dw px_s_dauto, px_s_dwire, px_s_dflat, px_s_dtex, px_s_rfull, px_s_rlow
px_s_dauto:  db 'Auto', 0
px_s_dwire:  db 'Wire', 0
px_s_dflat:  db 'Flat', 0
px_s_dtex:   db MENU_DIS, 'Textured (wave 2)', 0  ; SPEC.md 47: greyed, and
                                            ; the caption says why
px_s_rfull:  db 'Full res', 0
px_s_rlow:   db 'Low res', 0
px_s_nomem:  db 'Not enough memory for the picture (16 KB)', 0
px_ttl:      db 'Pixelstein 3D', 0

px_tpl:
    dw 0, 0, PX_WINW, PX_WINH
    dw px_ttl, px_paint, px_onkey, px_onclick

    OS88_PREFER px_pref, PX_WINW, PX_WINH, PX_WINW, PX_WINH, PX_WINW, PX_WINH

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
    dw px_ab1, px_ab2, px_ab3, px_ab4, px_ab5, 0
px_ab1:      db 'Pixelstein 3D for os8088', 0
px_ab2:      db 0
px_ab3:      db 'A raycast shooter in the shape of', 0
px_ab4:      db 'the 1992 one, priced for the 8088.', 0
px_ab5:      db 'Flat and wireframe walls; textures follow.', 0

; =============================================================================
; the modules
; =============================================================================
%include "pxtab.inc"
%include "pxlev.inc"
%include "pxcast.inc"
%include "pxgen.inc"
%include "pxcomp.inc"
%include "pxrast.inc"
%include "pxwin.inc"
%include "pxgame.inc"
%include "os88pit.inc"

; =============================================================================
; .bss (SPEC.md 20.5): an equ chain from os88_image_end, THE HANDOFF FIRST
; (96.9: pxstein.asm writes it at the head of this bss by the header's own
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
; --- the two map layouts and their marks (96.2): mapT then its spotvis, map
;     then its spotvis, so a walker's mark is its cell + PX_SPOTD ----------
%assign PX_MAPT_AT PX_BSS          ; ...and where they fall, for the guard below
    ZBUF  px_mapT, 4096
    ZBUF  px_spotT, 4096
    ZBUF  px_map, 4096
%assign PX_SPOT_AT PX_BSS
    ZBUF  px_spot, 4096
; --- the column arrays (96.2.5) ---------------------------------------------
    ZBUF  px_top, PX_COLMAX
    ZBUF  px_bot, PX_COLMAX
    ZBUF  px_mat, PX_COLMAX
    ZBUF  px_side, PX_COLMAX
    ZBUF  px_u, PX_COLMAX
    ZBUF  px_wallh, PX_COLMAX * 2
; --- last frame's memory, a set per page (96.5) ------------------------------
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
    ZBYTE px_hdoor
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
    ZWORD px_fanp
    ZWORD px_ladp
    ZBYTE px_cols
    ZBYTE px_bpc
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
    ZBUF  px_fsi, FSI_SIZE
    ZBUF  px_devoff, PX_ROWS * 2
    ZWORD px_shseg
; --- the window --------------------------------------------------------------
    ZWORD px_win
    ZWORD px_scrw
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
                                    ; blit sends the whole band (96.5)
    ZBYTE px_llen                   ; the text line's last length: the pad
    ZBUF  px_lbuf, 64
; --- the session --------------------------------------------------------------
    ZWORD px_last
    ZWORD px_frames
    ZBUF  px_t0, 4
    ZBUF  px_ftime, 4               ; the frame's WORK (96.8): cast, compose
    ZBUF  px_twait, 4               ; and present, less the waits px_wait_*
    ZBUF  px_tw0, 4                 ; bracketed inside it (a retrace, a lock)
    ZBYTE px_inbr
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
                                    ; step up never passes it (96.8)
    ZBYTE px_amiss
    ZBYTE px_ahit
    ZWORD px_ahold                  ; the tick a step up is allowed from
    ZBYTE px_pend                   ; a Detail/Resolution pick awaiting a frame

%define OS88UI_ABOUT                ; the standard About card...
%define OS88UI_NOBTN                ; ...and NOTHING else: a game's chrome is
%include "os88ui.inc"               ; its own

    OS88_BSS PX_BSS
    OS88_IMAGE_END

; THE FOUR MAP ARRAYS SIT INSIDE THE FAR KEYS' BAND (SPEC.md 96.2.3): a live
; walker's pointer runs from its array's base less the 191-byte overrun to its
; end plus it, and its key - parked at PX_FARJA or PX_FARJB and moved 64 a
; pass, the wrong way in two quadrants - must never cross that range in the
; 63 passes the solid border allows. This is also 96.2.3's "256 bytes from
; either end of the segment", asserted
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
