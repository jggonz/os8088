; =============================================================================
; os8088 - apps/modplug/mppset.inc
;
; ModPlug Player's Setup window (SPEC.md 52.5). Prefix mpps_.
;
; ModPlugPlayer's SetupWindow.ui is a 5,034-line Qt form: a page TREE down the
; left - General, Audio, Player, DSP, Directories, Visualization (with
; Display, Oscilloscope, Spectrum Analyzer and VU Meter under it), Window,
; Colors, Plug-Ins - and a pane on the right that swaps with the selection.
; That shape is what is ported: a page list on the left, a pane on the right,
; and the controls in each pane are ModPlugPlayer's own, in its own words.
;
; WHAT IS GREY, AND WHY IT IS DRAWN AT ALL. Most of that dialog cannot work on
; an 8086 with an 8-bit mono DAC, and the tempting thing is to ship only the
; controls that can. SPEC.md 47's rule 4 says the opposite wherever the reason
; is a FACT and not a guess: a greyed control that names its reason tells a
; user what this machine is and is not, and the SHAPE of the app is what is
; being ported. So `16 Bit (8-Bit DAC)` and `Cubic (Too Slow)` are drawn,
; greyed, and refuse a click without further comment - the greying already
; said it (rule 6). Nothing here is greyed on a guess: every reason is a
; property of the hardware, of the mixer, or of the mode that is on right now.
;
; THE CONTROLS ARE A TABLE, not code. Each row is MPPS_CTLSZ bytes (page,
; kind, x, y, label, value, flags, target) and one drawing loop plus one hit
; loop walk it. A pane is a run of rows and adding a control is adding a row -
; which is the only way a dialog with sixty controls stays readable in
; assembly, and the only way its drawing and its hit test cannot drift apart
; (the fm_hit rule again).
;
; THE WINDOW IS NOT BOUND to the instance (SPEC.md 52.1). The entry proc
; returned the player, so wm_owner never names this one - which means its
; close box and its minimize box both reduce to a plain wm_hide, exactly like
; the kernel's own Standard File dialog (SPEC.md 38), and reopening is one
; wm_show. There is no close-path code in this file because there is nothing
; for it to do; teardown sweeps the window by its W_SEG creator stamp.
;
; It draws with mppui.inc's helpers, pointed at its own content origin through
; mppu_org - which is why there is no bevel, no well and no text formatter in
; this file.
; =============================================================================

; Widths, and they are not arbitrary. The page list has to hold
; 'Visualization' - ModPlugPlayer's own word, 13 glyphs - and the pane has to
; hold two columns of labelled controls plus full-width prose. Working
; backwards from the 8x8 font: the list is 112, the pane's controls start at
; MPPS_CTLX, the right column is at pane-x 190, and that leaves 21 glyphs for
; a left-column label, 20 for a right-column one and 46 for a line of prose.
; Every string in the table below was written to those three numbers; a label
; that outgrows its column silently overprints the other one.
MPPS_W      equ 500                 ; content width
MPPS_H      equ 176                 ; ...and height, full layout
MPPS_HC     equ 128                 ; ...compact (CGA)
MPPS_FW     equ MPPS_W + 2
MPPS_LISTW  equ 112                 ; the page list down the left
MPPS_ROWH   equ 12                  ; one page-list row
MPPS_NPAGE  equ 6
MPPS_PANEX  equ 120                 ; the pane's well...
MPPS_CTLX   equ MPPS_PANEX + 6      ; ...and the origin control x/y are from
MPPS_CTLY   equ 10
MPPS_COL2   equ 190                 ; the right column, pane-relative
MPPS_BOXW   equ 9                   ; a check box / radio ring
MPPS_LABX   equ 13                  ; the label, right of the box
MPPS_RULE   equ 40                  ; a group caption's underline

; --- control kinds --------------------------------------------------------------
MPPK_GROUP  equ 0                   ; a group caption with a rule under it
MPPK_RADIO  equ 1                   ; one of a set: sets [target] = value
MPPK_CHECK  equ 2                   ; a toggle: flips [target] between 0 and 1
MPPK_TEXT   equ 3                   ; a plain line of prose - no target

; --- control flags --------------------------------------------------------------
MPPF_DEAD   equ 1                   ; never available on this machine at all
MPPF_XT     equ 2                   ; unavailable while XT mode is on
MPPF_CPU0   equ 4                   ; unavailable on a tier-0 8086/8088
MPPF_RATE   equ 8                   ; changing it restarts the stream, so the
                                    ; store goes through mpp_rate_set
MPPF_XTSET  equ 16                  ; ...and this row IS the XT toggle
MPPF_FIXED  equ 32                  ; always drawn selected: the only choice
                                    ; this machine has, shown so the group
                                    ; reads as a group and not as an omission

; --- the pages, in ModPlugPlayer's own order ------------------------------------
mpps_pages:
    dw mpps_p_gen, mpps_p_aud, mpps_p_ply, mpps_p_dsp, mpps_p_viz, mpps_p_xt
mpps_p_gen: db 'General', 0
mpps_p_aud: db 'Audio', 0
mpps_p_ply: db 'Player', 0
mpps_p_dsp: db 'DSP', 0
mpps_p_viz: db 'Visualization', 0
mpps_p_xt:  db 'XT', 0

; =============================================================================
; The control table
;
;   MPPS_CTL page, kind, x, y, label, target, value, flags
;
; x and y are relative to the pane's control origin, so no row knows where the
; pane is. Two columns per page - 6 and 140 - and nothing below y 96, which is
; what lets the COMPACT window show every control the full one does rather
; than silently clipping the bottom of a page (SPEC.md 52.4).
; =============================================================================
%macro MPPS_CTL 8
    db %1, %2, %3, %4
    dw %5
    db %7, %8
    dw %6
%endmacro
MPPS_CTLSZ equ 10

mpps_ctls:
; --- General ------------------------------------------------------------------
    MPPS_CTL 0, MPPK_GROUP,   6,  0, mpps_s_about,    0, 0, 0
    MPPS_CTL 0, MPPK_TEXT,   10, 13, mpps_s_g1,       0, 0, 0
    MPPS_CTL 0, MPPK_TEXT,   10, 23, mpps_s_g2,       0, 0, 0
    MPPS_CTL 0, MPPK_GROUP,   6, 38, mpps_s_replayer, 0, 0, 0
    MPPS_CTL 0, MPPK_TEXT,   10, 51, mpps_s_g3,       0, 0, 0
    MPPS_CTL 0, MPPK_TEXT,   10, 61, mpps_s_g4,       0, 0, 0
    MPPS_CTL 0, MPPK_GROUP,   6, 76, mpps_s_settings, 0, 0, 0
    MPPS_CTL 0, MPPK_CHECK,  10, 89, mpps_s_saveimm,  0, 0, MPPF_DEAD

; --- Audio --------------------------------------------------------------------
    MPPS_CTL 1, MPPK_GROUP,   6,  0, mpps_s_freq,     0, 0, 0
    MPPS_CTL 1, MPPK_RADIO,  10, 13, mpps_s_r11,  mpp_rsel, 0, MPPF_RATE
    MPPS_CTL 1, MPPK_RADIO,  10, 25, mpps_s_r22,  mpp_rsel, 1, MPPF_RATE
    MPPS_CTL 1, MPPK_RADIO,  10, 37, mpps_s_r44,  mpp_rsel, 2, MPPF_RATE
    MPPS_CTL 1, MPPK_GROUP,   6, 52, mpps_s_resamp,   0, 0, 0
    MPPS_CTL 1, MPPK_RADIO,  10, 65, mpps_s_inone, mpm_interp, 0, 0
    MPPS_CTL 1, MPPK_RADIO,  10, 77, mpps_s_ilin, mpm_interp, 1, MPPF_XT | MPPF_CPU0
    MPPS_CTL 1, MPPK_RADIO,  10, 89, mpps_s_icub,     0, 0, MPPF_DEAD
    MPPS_CTL 1, MPPK_GROUP, MPPS_COL2,      0, mpps_s_depth,  0, 0, 0
    MPPS_CTL 1, MPPK_RADIO, MPPS_COL2 + 4, 13, mpps_s_8bit,   0, 0, MPPF_FIXED
    MPPS_CTL 1, MPPK_RADIO, MPPS_COL2 + 4, 25, mpps_s_16bit,  0, 0, MPPF_DEAD
    MPPS_CTL 1, MPPK_GROUP, MPPS_COL2,     40, mpps_s_chans,  0, 0, 0
    MPPS_CTL 1, MPPK_RADIO, MPPS_COL2 + 4, 53, mpps_s_mono,   0, 0, MPPF_FIXED
    MPPS_CTL 1, MPPK_RADIO, MPPS_COL2 + 4, 65, mpps_s_stereo, 0, 0, MPPF_DEAD
    MPPS_CTL 1, MPPK_GROUP, MPPS_COL2,     78, mpps_s_dither, 0, 0, 0
    MPPS_CTL 1, MPPK_CHECK, MPPS_COL2 + 4, 91, mpps_s_ddef, mpm_dither, 0, MPPF_XT

; --- Player -------------------------------------------------------------------
    MPPS_CTL 2, MPPK_GROUP,   6,  0, mpps_s_amiga,    0, 0, 0
    MPPS_CTL 2, MPPK_RADIO,  10, 13, mpps_s_aauto,    0, 0, MPPF_DEAD
    MPPS_CTL 2, MPPK_RADIO,  10, 25, mpps_s_a500, mpm_amiga, 1, MPPF_XT
    MPPS_CTL 2, MPPK_RADIO,  10, 37, mpps_s_a1200, mpm_amiga, 2, MPPF_XT
    MPPS_CTL 2, MPPK_RADIO,  10, 49, mpps_s_aoff, mpm_amiga, 0, MPPF_XT
    MPPS_CTL 2, MPPK_TEXT,   10, 80, mpps_s_apaula,   0, 0, 0
    MPPS_CTL 2, MPPK_TEXT,   10, 90, mpps_s_apaula2,  0, 0, 0
    MPPS_CTL 2, MPPK_GROUP, MPPS_COL2,      0, mpps_s_repeat, 0, 0, 0
    MPPS_CTL 2, MPPK_RADIO, MPPS_COL2 + 4, 13, mpps_s_rno, mpp_repeat, MPPR_OFF, 0
    MPPS_CTL 2, MPPK_RADIO, MPPS_COL2 + 4, 25, mpps_s_rsong, mpp_repeat, MPPR_SONG, 0
    MPPS_CTL 2, MPPK_RADIO, MPPS_COL2 + 4, 37, mpps_s_rlist, mpp_repeat, MPPR_LIST, 0
    MPPS_CTL 2, MPPK_GROUP, MPPS_COL2,     52, mpps_s_plgrp,  0, 0, 0
    MPPS_CTL 2, MPPK_CHECK, MPPS_COL2 + 4, 65, mpps_s_shuf, mpp_shuffle, 0, 0

; --- DSP ----------------------------------------------------------------------
    MPPS_CTL 3, MPPK_GROUP,   6,  0, mpps_s_crush,    0, 0, 0
    MPPS_CTL 3, MPPK_RADIO,  10, 13, mpps_s_cr0, mpm_crush, 0, MPPF_XT
    MPPS_CTL 3, MPPK_RADIO,  10, 25, mpps_s_cr6, mpm_crush, 6, MPPF_XT
    MPPS_CTL 3, MPPK_RADIO,  10, 37, mpps_s_cr4, mpm_crush, 4, MPPF_XT
    MPPS_CTL 3, MPPK_RADIO,  10, 49, mpps_s_cr2, mpm_crush, 2, MPPF_XT
    MPPS_CTL 3, MPPK_TEXT,   10, 80, mpps_s_dsp1,     0, 0, 0
    MPPS_CTL 3, MPPK_TEXT,   10, 90, mpps_s_dsp2,     0, 0, 0
    MPPS_CTL 3, MPPK_GROUP, MPPS_COL2,      0, mpps_s_effects, 0, 0, 0
    MPPS_CTL 3, MPPK_CHECK, MPPS_COL2 + 4, 13, mpps_s_eq,     0, 0, MPPF_DEAD
    MPPS_CTL 3, MPPK_CHECK, MPPS_COL2 + 4, 25, mpps_s_reverb, 0, 0, MPPF_DEAD
    MPPS_CTL 3, MPPK_CHECK, MPPS_COL2 + 4, 37, mpps_s_bass,   0, 0, MPPF_DEAD
    MPPS_CTL 3, MPPK_CHECK, MPPS_COL2 + 4, 49, mpps_s_agc,    0, 0, MPPF_DEAD
    MPPS_CTL 3, MPPK_CHECK, MPPS_COL2 + 4, 61, mpps_s_surr,   0, 0, MPPF_DEAD

; --- Visualization ------------------------------------------------------------
    MPPS_CTL 4, MPPK_GROUP,   6,  0, mpps_s_vtype,    0, 0, 0
    MPPS_CTL 4, MPPK_RADIO,  10, 13, mpps_s_vspec, mpp_viz, MPPV_SPECTRUM, 0
    MPPS_CTL 4, MPPK_RADIO,  10, 25, mpps_s_vosc, mpp_viz, MPPV_SCOPE, 0
    MPPS_CTL 4, MPPK_RADIO,  10, 37, mpps_s_vvu, mpp_viz, MPPV_VU, 0
    MPPS_CTL 4, MPPK_GROUP,   6, 52, mpps_s_vwin,     0, 0, 0
    MPPS_CTL 4, MPPK_RADIO,  10, 65, mpps_s_wnone,    0, 0, MPPF_DEAD
    MPPS_CTL 4, MPPK_RADIO,  10, 77, mpps_s_whamm,    0, 0, MPPF_DEAD
    MPPS_CTL 4, MPPK_GROUP, MPPS_COL2,      0, mpps_s_vdisp,  0, 0, 0
    MPPS_CTL 4, MPPK_CHECK, MPPS_COL2 + 4, 13, mpps_s_vinfo, mpp_info, 0, 0
    MPPS_CTL 4, MPPK_TEXT,  MPPS_COL2 + 4, 32, mpps_s_v1,     0, 0, 0
    MPPS_CTL 4, MPPK_TEXT,  MPPS_COL2 + 4, 42, mpps_s_v2,     0, 0, 0
    MPPS_CTL 4, MPPK_TEXT,  MPPS_COL2 + 4, 52, mpps_s_v3,     0, 0, 0
    MPPS_CTL 4, MPPK_TEXT,  MPPS_COL2 + 4, 62, mpps_s_v4,     0, 0, 0
    MPPS_CTL 4, MPPK_TEXT,  MPPS_COL2 + 4, 72, mpps_s_v5,     0, 0, 0

; --- XT -----------------------------------------------------------------------
    MPPS_CTL 5, MPPK_GROUP,   6,  0, mpps_s_xtg,      0, 0, 0
    MPPS_CTL 5, MPPK_CHECK,  10, 14, mpps_s_xton, mpm_xt, 0, MPPF_XTSET
    MPPS_CTL 5, MPPK_TEXT,   10, 32, mpps_s_x1,       0, 0, 0
    MPPS_CTL 5, MPPK_TEXT,   10, 42, mpps_s_x2,       0, 0, 0
    MPPS_CTL 5, MPPK_TEXT,   10, 56, mpps_s_x3,       0, 0, 0
    MPPS_CTL 5, MPPK_TEXT,   10, 66, mpps_s_x4,       0, 0, 0
    MPPS_CTL 5, MPPK_TEXT,   10, 76, mpps_s_x5,       0, 0, 0
    MPPS_CTL 5, MPPK_TEXT,   10, 86, mpps_s_x6,       0, 0, 0
mpps_ctls_end:
MPPS_NCTL equ (mpps_ctls_end - mpps_ctls) / MPPS_CTLSZ

; --- the record's field offsets -------------------------------------------------
MPPS_PAGE equ 0
MPPS_KIND equ 1
MPPS_X    equ 2
MPPS_Y    equ 3
MPPS_LAB  equ 4
MPPS_VAL  equ 6
MPPS_FLG  equ 7
MPPS_TGT  equ 8

; =============================================================================
; mpps_toggle - open the Setup window, or put it away again
; in:  gfx lock held (a key, a button or a menu command)
; preserves all registers
; =============================================================================
mpps_toggle:
    push ax
    push bx
    push si
    cmp word [mpps_win], 0
    je .make
    call mpps_visible
    or al, al
    jz .show
    mov bx, [mpps_win]
    call OSAPI_WM_HIDE              ; a second click puts it away, which is
    jmp .paint                      ; what a toggle button means
.show:
    mov bx, [mpps_win]
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW
    jmp .paint
.make:
    call mpps_geom                  ; the template's x/y/h, for THIS screen
    mov si, mpps_tpl
    call OSAPI_WM_CREATE            ; CF=1: the window table is full, which is
    jc .paint                       ; a refusal and not an error - the player
    mov [mpps_win], bx              ; stays perfectly usable
    call OSAPI_WM_SHOW
.paint:
    call mppu_refresh              ; the player's Setup button lamp and its
    pop si                          ; Setup option LED both just changed
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpps_visible - AL = 1 while the Setup window is on screen
; preserves every other register. Read by the player's Setup button lamp, by
; its option button's LED and by mpps_toggle itself, so all three agree - the
; SPEC.md 47 rule 2 predicate, applied to a window instead of a control.
; -----------------------------------------------------------------------------
mpps_visible:
    push bx
    push cx
    push dx
    xor al, al
    cmp word [mpps_win], 0
    je .out
    mov bx, [mpps_win]
    call OSAPI_WM_GEOM              ; CF=1 = hidden, and for an unbound window
    jc .out                         ; minimized IS hidden (SPEC.md 52.1)
    mov al, 1
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mpps_geom - place the window and pick its height for THIS screen
; preserves all registers
; -----------------------------------------------------------------------------
mpps_geom:
    push ax
    push bx
    push cx
    push dx
    mov word [mpps_tpl + WT_H], MPPS_H + TITLE_H + 1
    cmp byte [mpp_compact], 0
    je .h
    mov word [mpps_tpl + WT_H], MPPS_HC + TITLE_H + 1
.h:
    mov ax, [mpp_scrw]              ; centred, then nudged down and right, so
    sub ax, MPPS_FW                 ; opening Setup does not simply bury the
    jns .x                          ; player it was opened from
    xor ax, ax
.x:
    shr ax, 1
    add ax, 20
    mov cx, [mpp_scrw]
    sub cx, MPPS_FW
    jns .clamp
    xor cx, cx
.clamp:
    cmp ax, cx
    jbe .xok
    mov ax, cx
.xok:
    mov [mpps_tpl + WT_X], ax
    mov ax, [mpp_docky]
    sub ax, MBAR_H
    sub ax, [mpps_tpl + WT_H]
    jns .y
    xor ax, ax
.y:
    shr ax, 1
    add ax, MBAR_H
    mov [mpps_tpl + WT_Y], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mpps_paint - W_PAINT for the Setup window
; in:  SI = its window ptr; gfx lock held
; =============================================================================
mpps_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mpps_geom_live
    call mpps_draw_all
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mpps_geom_live - latch the content origin and size, and point the shared
; drawing helpers at it. Every entry point that draws this window calls it.
mpps_geom_live:
    push ax
    push bx
    push cx
    push dx
    mov bx, [mpps_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    call mppu_org                   ; ...which is where mppui.inc's helpers
    mov bx, [mpps_win]              ; will now draw
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h
    mov [mpps_w], cx
    mov [mpps_h], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mpps_redraw - repaint from outside a W_PAINT (a click on our own content)
mpps_redraw:
    push ax
    call mpps_geom_live
    call mpps_draw_all
    call mppu_org_player            ; hand the helpers back before anything
    pop ax                          ; else draws the player
    ret

mpps_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    MPPU_PEN [mppu_body]
    xor ax, ax
    xor bx, bx
    mov cx, [mpps_w]
    dec cx
    mov dx, [mpps_h]
    dec dx
    call mppu_fillr
    call mpps_draw_list
    call mpps_draw_pane
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the page list down the left ------------------------------------------------
mpps_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 4                       ; the list lives in its own sunken well -
    mov bx, 4                       ; what ModPlugPlayer's page tree looks
    mov cx, MPPS_LISTW              ; like once it has no scroll bar
    mov dx, [mpps_h]
    sub dx, 5
    call mppu_well
    xor si, si                      ; SI = the page
.p:
    call mpps_row_rect              ; -> AX BX CX DX
    mov byte [mpps_gnd], CBLACK     ; the selected page INVERTS - white band,
    mov byte [mpps_ink], CWHITE     ; black text - rather than taking a
    cmp si, [mpps_sel]              ; coloured one. CBLUE would have been the
    jne .txt                        ; obvious choice and is wrong on two
    mov byte [mpps_gnd], CWHITE     ; adapters out of three: colours 0..6 all
    mov byte [mpps_ink], CBLACK     ; reduce to BLACK at 1bpp (SPEC.md 39.4),
    MPPU_PEN CWHITE                 ; so a blue band on a black well is a
    call mppu_fillr                 ; selection you cannot see. Inversion is
.txt:                               ; the Macintosh answer and survives every
    push si                         ; reduction, which is why the Standard
    mov ax, si                      ; File dialog uses it too (SPEC.md 38.3)
    shl ax, 1
    mov di, ax
    mov si, [mpps_pages + di]
    mov cx, 9
    mov dx, bx
    add dx, 2
    mov al, [mpps_ink]
    mov ah, [mpps_gnd]
    call mppu_run                   ; opaque: this window repaints from a
    pop si                          ; click as well as from its own W_PAINT
    inc si
    cmp si, MPPS_NPAGE
    jb .p
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mpps_row_rect - SI = page -> AX BX CX DX, its row in the list
mpps_row_rect:
    push si
    mov ax, MPPS_ROWH
    mul si
    add ax, 6
    mov bx, ax
    mov dx, bx
    add dx, MPPS_ROWH - 1
    mov ax, 6
    mov cx, MPPS_LISTW - 2
    pop si
    ret

; --- the pane on the right --------------------------------------------------------
mpps_draw_pane:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, MPPS_PANEX              ; the pane's own well, which is what makes
    mov bx, 4                       ; the page list read as a list rather than
    mov cx, [mpps_w]                ; as a column of buttons
    sub cx, 5
    mov dx, [mpps_h]
    sub dx, 5
    mov di, 1
    call mppu_bevel
    MPPU_PEN [mppu_body]
    inc ax
    inc bx
    dec cx
    dec dx
    call mppu_fillr
    xor si, si                      ; SI = the control index
.c:
    call mpps_ctl_ptr               ; -> DI = the row
    mov al, [di+MPPS_PAGE]
    xor ah, ah
    cmp ax, [mpps_sel]
    jne .next
    call mpps_draw_ctl
.next:
    inc si
    cmp si, MPPS_NCTL
    jb .c
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mpps_ctl_ptr - SI = index -> DI = its row; preserves all other registers
mpps_ctl_ptr:
    push ax
    push dx
    mov ax, MPPS_CTLSZ
    mul si
    mov di, mpps_ctls
    add di, ax
    pop dx
    pop ax
    ret

; mpps_ctl_xy - DI = a row -> AX = its pane x, BX = its pane y (both already
; offset to the control origin). Preserves everything else.
mpps_ctl_xy:
    mov al, [di+MPPS_X]
    xor ah, ah
    add ax, MPPS_CTLX
    mov bl, [di+MPPS_Y]
    xor bh, bh
    add bx, MPPS_CTLY
    ret

; -----------------------------------------------------------------------------
; mpps_draw_ctl - DI = a row; draw it
; preserves all registers
; -----------------------------------------------------------------------------
mpps_draw_ctl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mpps_ctl_xy
    mov [mpps_cx], ax
    mov [mpps_cy], bx
    call mpps_ctl_live              ; CF=1 = unavailable right now
    mov byte [mpps_cdis], 0
    jnc .pen
    mov byte [mpps_cdis], 1
.pen:
    mov al, [di+MPPS_KIND]
    cmp al, MPPK_GROUP
    je .group
    cmp al, MPPK_TEXT
    je .text
    call mpps_draw_box              ; a radio ring or a check box, and its mark
.text:
    call mpps_draw_label
    jmp .out
.group:
    mov si, [di+MPPS_LAB]           ; a group caption is never greyed: it is a
    mov cx, [mpps_cx]               ; heading, not a control
    mov dx, [mpps_cy]
    mov al, CBLACK
    mov ah, [mppu_body]
    call mppu_run
    MPPU_PEN CBLACK
    call OSAPI_FONT_WIDTH           ; ...with a rule from its end to the
    mov bx, [mpps_cx]               ; column's edge, which is the whole of
    add bx, ax                      ; ModPlugPlayer's group-box border that
    add bx, 4                       ; survives at this size
    mov ax, bx
    add bx, MPPS_RULE
    mov dx, [mpps_cy]
    add dx, 4
    call mppu_hlr
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mpps_draw_box - DI = a row; the ring or box, and its mark if it is set
; A LIVE box is a black well with a white mark in it; a GREYED box is a
; dithered outline on the body colour with no well at all. That difference is
; the whole point, and it is a difference in SHAPE rather than in ink: at 1bpp
; a greyed LABEL is pixel-identical to a live one (a glyph rounds to black,
; SPEC.md 39.4), so if the box did not change the user would have no way to
; tell an unavailable control from an available unchecked one. A frame DOES
; dither, which is exactly what SPEC.md 47 means by greying the whole control.
mpps_draw_box:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [mpps_cx]
    mov bx, [mpps_cy]
    mov cx, ax
    add cx, MPPS_BOXW - 1
    mov dx, bx
    add dx, MPPS_BOXW - 1
    cmp byte [mpps_cdis], 0
    jne .grey
    push di
    mov di, 1                       ; live: a sunken well, blacked out, so a
    call mppu_bevel                 ; white mark in it reads on every adapter
    pop di
    MPPU_PEN CBLACK
    jmp .fill
.grey:
    MPPU_PEN CDGRAY                 ; greyed: a screened outline round the
    call mppu_framer                ; body colour - no well, no mark, and
    MPPU_PEN [mppu_body]            ; visibly not a live box at one bit
.fill:
    push ax
    push bx
    push cx
    push dx
    inc ax
    inc bx
    dec cx
    dec dx
    call mppu_fillr
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [mpps_cdis], 0
    jne .out                        ; a greyed control never draws a mark: it
    call mpps_ctl_set               ; cannot act, so what it would be showing
    jnc .out                        ; is not a state anyone can change
    MPPU_PEN CWHITE
    add ax, 2
    add bx, 2
    sub cx, 2
    sub dx, 2
    call mppu_fillr
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mpps_draw_label - DI = a row; its caption, in the pen its state earns
mpps_draw_label:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [di+MPPS_LAB]
    mov cx, [mpps_cx]
    cmp byte [di+MPPS_KIND], MPPK_TEXT
    je .x
    add cx, MPPS_LABX
.x:
    mov dx, [mpps_cy]
    cmp byte [di+MPPS_KIND], MPPK_TEXT
    je .y
    inc dx                          ; a caption beside a box sits one pixel
.y:                                 ; low, so the two read as one control
    mov al, CBLACK
    cmp byte [mpps_cdis], 0
    je .run
    mov al, CDGRAY
.run:
    mov ah, [mppu_body]
    call mppu_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpps_ctl_live - DI = a row; out CF=1 if it is UNAVAILABLE right now
; preserves all registers
;
; The one predicate: mpps_draw_ctl greys by it and mpps_click refuses by it,
; so the picture and the behaviour cannot disagree (SPEC.md 47 rule 2).
; -----------------------------------------------------------------------------
mpps_ctl_live:
    push ax
    mov al, [di+MPPS_FLG]
    test al, MPPF_DEAD
    jnz .dis                        ; not on this machine, ever
    test al, MPPF_XT
    jz .cpu
    cmp byte [mpm_xt], 0            ; the DSP tail is off while XT mode is on,
    jne .dis                        ; and XT mode is what turned it off
.cpu:
    test al, MPPF_CPU0
    jz .live
    cmp byte [mpp_cpu0], 0          ; too slow HERE - a fact about the machine
    jne .dis                        ; that answered OSAPI_CPU_INFO, not a guess
.live:
    pop ax
    clc
    ret
.dis:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mpps_ctl_set - DI = a row; out CF=1 if the control is currently SET
; preserves all registers
; -----------------------------------------------------------------------------
mpps_ctl_set:
    push ax
    push bx
    push si
    mov al, [di+MPPS_FLG]
    test al, MPPF_FIXED
    jnz .yes                        ; the only choice this machine has
    mov si, [di+MPPS_TGT]
    or si, si
    jz .no                          ; no target: a dead control, or prose
    mov al, [si]
    cmp byte [di+MPPS_KIND], MPPK_CHECK
    je .chk
    cmp al, [di+MPPS_VAL]           ; a radio: set when the target holds ITS
    je .yes                         ; value
    jmp .no
.chk:
    or al, al                       ; a check: set when the target is nonzero
    jnz .yes
.no:
    pop si
    pop bx
    pop ax
    clc
    ret
.yes:
    pop si
    pop bx
    pop ax
    stc
    ret

; =============================================================================
; mpps_onclick - W_ONCLICK for the Setup window
; in:  CX = x, DX = y (absolute), SI = its window ptr; gfx lock held
; =============================================================================
mpps_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mpps_hx], cx               ; bank the click BEFORE anything that
    mov [mpps_hy], dx               ; answers in DX - OSAPI_WM_CONTENT does,
                                    ; and it is the very next call
    call mpps_geom_live             ; the window may have been dragged since
    mov ax, [mppu_dx0]              ; the last paint, so the origin is re-read
    sub [mpps_hx], ax               ; rather than remembered
    mov ax, [mppu_dy0]
    sub [mpps_hy], ax
    ; --- the page list ------------------------------------------------------
    mov ax, [mpps_hx]
    cmp ax, 6
    jb .pane
    cmp ax, MPPS_LISTW - 2
    ja .pane
    mov ax, [mpps_hy]
    sub ax, 6
    js .pane
    xor dx, dx
    mov cx, MPPS_ROWH
    div cx
    cmp ax, MPPS_NPAGE
    jae .done
    mov [mpps_sel], ax
    jmp .redraw
    ; --- a control in the pane ----------------------------------------------
.pane:
    xor si, si
.c:
    call mpps_ctl_ptr
    mov al, [di+MPPS_PAGE]
    xor ah, ah
    cmp ax, [mpps_sel]
    jne .next                       ; a page that is not showing has no
    mov al, [di+MPPS_KIND]          ; clickable controls, whatever its rows say
    cmp al, MPPK_RADIO
    je .test
    cmp al, MPPK_CHECK
    jne .next
.test:
    call mpps_ctl_hit
    jc .next
    call mpps_ctl_live              ; CF=1: greyed. Refuse, and say nothing
    jc .done                        ; further - the greying already explained
    call mpps_apply                 ; itself (SPEC.md 47 rule 6)
    jmp .redraw
.next:
    inc si
    cmp si, MPPS_NCTL
    jb .c
    jmp .done
.redraw:
    call mpps_redraw
    call mppu_refresh              ; the player's own LEDs and LCD flags
.done:                              ; follow every one of these settings
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpps_ctl_hit - DI = a row; out CF=0 if [mpps_hx]/[mpps_hy] is inside it
; preserves all registers
;
; The target is the box AND its caption, because a 9-pixel ring is not
; something a serial mouse hits reliably - the same reason the Disk window
; aims a row click at the whole row.
; -----------------------------------------------------------------------------
mpps_ctl_hit:
    push ax
    push bx
    push cx
    push dx
    push si
    call mpps_ctl_xy                ; AX = x, BX = y
    mov cx, [mpps_hy]
    sub cx, bx
    js .no
    cmp cx, 11                      ; the row's clickable height
    jae .no
    mov cx, [mpps_hx]
    sub cx, ax
    js .no
    mov si, [di+MPPS_LAB]
    call OSAPI_FONT_WIDTH           ; out AX = the caption's pixel width; the
    add ax, MPPS_LABX               ; banked CX is why clobbering AX is fine
    cmp cx, ax
    jae .no
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mpps_apply - DI = a row the user just clicked; make it so
; in:  gfx lock held; preserves all registers
;
; Two settings do not go through the plain store, and both for the same
; reason: they cannot be changed under a running stream. The rate and the XT
; toggle route to mpp_rate_set / mpp_xt_toggle, which stop playback through
; the SPEC.md 52.2 drain first.
; -----------------------------------------------------------------------------
mpps_apply:
    push ax
    push si
    mov al, [di+MPPS_FLG]
    test al, MPPF_XTSET
    jnz .xt
    test al, MPPF_RATE
    jnz .rate
    mov si, [di+MPPS_TGT]
    or si, si
    jz .out                         ; no target: a fixed or a dead control
    cmp byte [di+MPPS_KIND], MPPK_CHECK
    je .chk
    mov al, [di+MPPS_VAL]           ; a radio stores ITS value...
    mov [si], al
    jmp .post
.chk:
    xor byte [si], 1                ; ...a check flips its target
.post:
    cmp si, mpm_amiga               ; a filter that changed pole starts from
    jne .out                        ; silence rather than carrying the old
    mov word [mpm_flt], 0           ; pole's state into the new one
    jmp .out
.rate:
    mov al, [di+MPPS_VAL]
    call mpp_rate_set
    jmp .out
.xt:
    call mpp_xt_toggle
.out:
    pop si
    pop ax
    ret

; =============================================================================
; mpps_onkey - W_ONKEY for the Setup window: Esc closes it, the arrows walk
;              the page list, and every player key still works from here so a
;              user does not have to go back to the player to press Stop
; in:  AL = ascii, AH = scan, SI = its window ptr; gfx lock held
; =============================================================================
mpps_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax
    or bl, bl                       ; the SPEC.md 44.2 keypad gate
    jnz .ascii
    cmp bh, 0x48
    je .up
    cmp bh, 0x50
    je .down
    jmp .done
.up:
    cmp word [mpps_sel], 0
    je .done
    dec word [mpps_sel]
    jmp .redraw
.down:
    cmp word [mpps_sel], MPPS_NPAGE - 1
    jae .done
    inc word [mpps_sel]
    jmp .redraw
.ascii:
    cmp bl, 27
    jne .pass
    mov bx, [mpps_win]
    call OSAPI_WM_HIDE
    call mppu_refresh              ; the player's Setup lamps go out
    jmp .done
.pass:
    mov ax, bx                      ; anything else is the player's: one
    call mpp_onkey                  ; keyboard for the app, not one per window
    jmp .done
.redraw:
    call mpps_redraw
.done:
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
mpps_tpl:
    dw 0, 0, MPPS_FW, MPPS_H + TITLE_H + 1      ; x/y/h set by mpps_geom
    dw mpps_ttl, mpps_paint, mpps_onkey, mpps_onclick
mpps_ttl: db 'ModPlug Player Setup', 0

; --- General (prose spans the pane: 46 glyphs) --------------------------------
mpps_s_about:    db 'ModPlug Player', 0
mpps_s_g1:       db 'Interface ported from ModPlug Player V2', 0
mpps_s_g2:       db 'by Volkan Orhan - modplugplayer.org, GPLv3', 0
mpps_s_replayer: db 'Replayer', 0
mpps_s_g3:       db '4-channel ProTracker, 8-bit mono output.', 0
mpps_s_g4:       db 'Not libopenmpt - see SPEC.md 52.1.', 0
mpps_s_settings: db 'Settings', 0
mpps_s_saveimm:  db 'Save Immediately (No Settings File)', 0

; --- Audio (left column: 21 glyphs; right: 20) --------------------------------
mpps_s_freq:     db 'Frequency', 0
mpps_s_r11:      db '11 kHz', 0
mpps_s_r22:      db '22 kHz', 0
mpps_s_r44:      db '44 kHz', 0
mpps_s_resamp:   db 'Resampling', 0
mpps_s_inone:    db 'No Interpolation', 0
mpps_s_ilin:     db 'Linear', 0
mpps_s_icub:     db 'Cubic (Too Slow)', 0
mpps_s_depth:    db 'Bit Depth', 0
mpps_s_8bit:     db '8 Bit', 0
mpps_s_16bit:    db '16 Bit (8-Bit DAC)', 0
mpps_s_chans:    db 'Channels', 0
mpps_s_mono:     db 'Mono', 0
mpps_s_stereo:   db 'Stereo (Mono Mixer)', 0
mpps_s_dither:   db 'Dither', 0
mpps_s_ddef:     db 'Default Dither', 0

; --- Player -------------------------------------------------------------------
mpps_s_amiga:    db 'Amiga Filter Type', 0
mpps_s_aauto:    db 'Auto (No Paula)', 0
mpps_s_a500:     db 'Amiga 500', 0
mpps_s_a1200:    db 'Amiga 1200', 0
mpps_s_aoff:     db 'Unfiltered', 0
mpps_s_apaula:   db "A one-pole lowpass in the Amiga's spirit.", 0
mpps_s_apaula2:  db 'It is not Paula emulation.', 0
mpps_s_repeat:   db 'Repeat', 0
mpps_s_rno:      db 'No Repeat', 0
mpps_s_rsong:    db 'Repeat Song', 0
mpps_s_rlist:    db 'Repeat PlayList', 0
mpps_s_plgrp:    db 'PlayList', 0
mpps_s_shuf:     db 'Shuffle', 0

; --- DSP ----------------------------------------------------------------------
mpps_s_crush:    db 'Bit Crush', 0
mpps_s_cr0:      db 'Off', 0
mpps_s_cr6:      db '6 Bits', 0
mpps_s_cr4:      db '4 Bits', 0
mpps_s_cr2:      db '2 Bits', 0
mpps_s_dsp1:     db 'Bit crush is one AND per output byte,', 0
mpps_s_dsp2:     db 'which is why this one survived the port.', 0
mpps_s_effects:  db 'Effects', 0
mpps_s_eq:       db 'Equalizer (No CPU)', 0
mpps_s_reverb:   db 'Reverb (No CPU)', 0
mpps_s_bass:     db 'Bass Boost (No CPU)', 0
mpps_s_agc:      db 'Auto Gain (No CPU)', 0
mpps_s_surr:     db 'Pro-Logic (Mono Out)', 0

; --- Visualization ------------------------------------------------------------
mpps_s_vtype:    db 'Visualization', 0
mpps_s_vspec:    db 'Spectrum Analyzer', 0
mpps_s_vosc:     db 'Oscilloscope', 0
mpps_s_vvu:      db 'VU Meter', 0
mpps_s_vwin:     db 'Window Function', 0
mpps_s_wnone:    db 'None (No FFT)', 0
mpps_s_whamm:    db 'Hamming (No FFT)', 0
mpps_s_vdisp:    db 'Display', 0
mpps_s_vinfo:    db 'Info On LCD', 0
mpps_s_v1:       db 'The analyzer maps', 0
mpps_s_v2:       db 'the four channels', 0
mpps_s_v3:       db 'onto bands by their', 0
mpps_s_v4:       db 'period - a semitone', 0
mpps_s_v5:       db 'axis. Not an FFT.', 0

; --- XT -----------------------------------------------------------------------
mpps_s_xtg:      db 'XT Mode', 0
mpps_s_xton:     db 'Play On A 4.77 MHz 8088', 0
mpps_s_x1:       db 'Sized for the machine this OS is aimed at.', 0
mpps_s_x2:       db 'Pre-armed there, off on anything faster.', 0
mpps_s_x3:       db 'Rate drops to 5,500 Hz; the volume tables', 0
mpps_s_x4:       db 'pre-scale the output stage away; the', 0
mpps_s_x5:       db "mixer's bounds check leaves its inner loop.", 0
mpps_s_x6:       db 'The DSP tail goes off with it.', 0

; =============================================================================
; mppset bss
; =============================================================================
    MPPW mpps_win                   ; 0 until Setup is opened for the first
                                    ; time; never bound, so never destroyed
                                    ; except by teardown (SPEC.md 52.1)
    MPPW mpps_sel                   ; the page showing, 0..MPPS_NPAGE-1
    MPPW mpps_w                     ; its content size, latched at paint
    MPPW mpps_h
    MPPW mpps_hx                    ; a click, banked content-relative
    MPPW mpps_hy
    MPPW mpps_cx                    ; the control being drawn
    MPPW mpps_cy
    MPPB mpps_cdis                  ; ...and whether it is greyed, so the box
                                    ; and the label cannot disagree
    MPPB mpps_gnd                   ; the page row's ground and ink, banked:
    MPPB mpps_ink                   ; AX carries the rect, so AH cannot

