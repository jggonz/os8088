; =============================================================================
; os8088 - apps/tracker/trkwin.inc
;
; Tracker's WINDOWED face (SPEC.md 45.21). Prefix tw_. The windowed Tracker
; used to be a splash card - a name, a key map and "press F" - with the whole
; application living fullscreen (45.1). This is ModPlug Player's face instead
; (SPEC.md 56.3), rebuilt to this tree's current standards rather than copied:
;
;   the LCD panel        title, elapsed time and file, format, position
;   the transport row    eight STANDARD buttons with pictures (os88ui.inc,
;                        OS88UI_BIMG): press draws them inverted, release on
;                        the same button fires, slide off and they cancel
;   the volume slider    click AND drag, live
;   the scrubber         click and drag; the seek lands on the release
;   the visualiser       XT (four needles, delta-drawn, or twelve bars) or
;                        286+ (a spectrum and an oscilloscope), 45.21.5
;   the option grid      standard buttons, LATCHED to show a setting is on
;   the status line
;
; THE FOUR RULES IT IS BUILT AROUND, and each is a ModPlug defect not carried:
;
;   1. NO PIXEL IS WRITTEN TWICE. Every element owns its rect and draws it
;      opaque - text as one font_run, a well's black as the strips BETWEEN its
;      lines, a slider as the groove either side of the thumb - and the body
;      is a TABLE OF TILES that covers exactly what no element does
;      (tw_tiles_*, generated and checked by tools/trkface.py). ModPlug filled
;      its body grey and drew every control over it; this never fills a ground
;      that something is about to be drawn on.
;   2. A FRAME DRAWS WHAT CHANGED. Every element keeps what it last drew (a
;      hash, a flag word, a thumb x, a bar length) and is drawn only when that
;      moves - SPEC.md 56.12's invalidation shape, kept.
;   3. THE ORIGIN IS ASKED FOR EVERY FRAME. A drag calls no W_PAINT (SPEC.md
;      11.96.12, 93.3.4.2), so tw_track re-reads the content origin, the
;      layout and the DEPTH of the display the window is on (SPEC.md 39.16.4)
;      at the top of every draw, and a move repaints nothing - the kernel's
;      drag cache already moved the pixels. The button rects are SCREEN
;      coordinates and are rebuilt there, and before every press.
;   4. THE ABOUT CARD IS THE STANDARD ONE and nothing draws over it: while it
;      is up the worker drops its frames, every UI refresh is refused and the
;      button record carries NO live buttons, so a press on a button hidden
;      under the card cannot draw that button through it.
; =============================================================================

TW_W        equ 416                 ; content width, both layouts
TW_HFULL    equ 184                 ; ...and the two heights: VGA/Hercules
TW_HCOMP    equ 136                 ; ...and CGA's 156-row desktop band
TW_NTB      equ 8                   ; transport buttons
TW_NBTN     equ 16                  ; ...plus up to eight options
TW_BX0      equ 8                   ; the transport row: first button's x,
TW_BPITCH   equ 32                  ; ...the pitch
TW_BW       equ 28                  ; ...and a button's width
TW_OX1      equ 248                 ; the option grid: first column's x,
TW_OPX      equ 84                  ; ...the column pitch
TW_OW       equ 80                  ; ...and a button's width
TW_TX       equ 8                   ; every text run's x - on the byte grid,
TW_CELLS    equ 50                  ; so font_run takes its one-store path
TW_LPITCH   equ 10                  ; LCD line pitch
TW_SHLN     equ 4                   ; LCD lines with a shadow: the most any
                                    ; layout shows (TWL_LN), then the status
TW_KEY      equ 16                  ; a line's key, bytes (tw_lkey): 1, the
TW_KEYSH    equ 4                   ; generation, up to nine inputs; a power
                                    ; of two so a slot's is a shift away
TWK_STAT    equ 0FFh                ; tw_lkey's kind for the status line
TW_THW      equ 8                   ; a slider thumb's width
TW_VUFALL   equ 2                   ; the needles' fall per frame (tui_vu_step)
TW_SPFALL   equ 3 * TW_VUFALL       ; the spectrum's bars fall THREE times as
                                    ; fast (SPEC.md 45.24): they show what is
                                    ; happening NOW, and the peak markers below
                                    ; carry what happened recently
TW_PKHOLD   equ 4                   ; a peak marker holds this many frames
                                    ; (~0.2 s at a frame a tick)...
TW_PKSTEP   equ 4                   ; ...then falls this far every
TW_PKEVERY  equ 2                   ; TW_PKEVERY frames: two levels a frame,
                                    ; the old bars' speed, so it trails the new.
                                    ; STEPPED rather than a level a frame because
                                    ; a marker's move is two fills whatever its
                                    ; size (the rows it leaves, the rows it
                                    ; takes), so the step is what is paid for
TW_PKH      equ 2                   ; the marker's height in rows

TWI_BODY    equ 1                   ; the whole face owes a draw
TWI_VIZ     equ 2                   ; ...or only the visualiser pane

; --- the layout record: words, copied into tw_lay ------------------------------
TWL_LX1 equ 0                       ; the LCD well
TWL_LY1 equ 2
TWL_LX2 equ 4
TWL_LY2 equ 6
TWL_LN  equ 8                       ; ...its line count
TWL_TY1 equ 10                      ; the transport row, top and bottom
TWL_TY2 equ 12
TWL_VX1 equ 14                      ; the volume groove
TWL_VY1 equ 16
TWL_VX2 equ 18
TWL_VY2 equ 20
TWL_SX1 equ 22                      ; the scrubber groove
TWL_SY1 equ 24
TWL_SX2 equ 26
TWL_SY2 equ 28
TWL_ZX1 equ 30                      ; the visualiser well
TWL_ZY1 equ 32
TWL_ZX2 equ 34
TWL_ZY2 equ 36
TWL_OY1 equ 38                      ; the option grid: top, button height,
TWL_OH  equ 40                      ; row pitch, how many
TWL_OPY equ 42
TWL_ON  equ 44
TWL_STY equ 46                      ; the status line's y
TWL_TIL equ 48                      ; the body tiles, and how many
TWL_NT  equ 50
TWL_MAP equ 52                      ; the option ids this layout shows
TWL_KND equ 54                      ; the LCD line kinds, in order
TWL_SZ  equ 56

; the option ids
TWO_REP     equ 0
TWO_SHUF    equ 1
TWO_LIST    equ 2
TWO_XT      equ 3
TWO_VIZ     equ 4
TWO_RATE    equ 5
TWO_FULL    equ 6
TWO_ABOUT   equ 7

; the visualiser modes (tw_viz is the PICK, tw_vizm what is drawn)
TWV_VU      equ 0
TWV_SPEC    equ 1
TWV_SCOPE   equ 2
TWV_OFF     equ 3                   ; nothing: a PICK now (SPEC.md 45.23), and
                                    ; still what XT mode's 11 kHz forces
TWV_N       equ 4                   ; ...the four a pick cycles through; the
TWV_THIN    equ 4                   ; one below is only ever FORCED: the XT meter
TW_OFFLEN   equ 19                  ; tw_s_voff's length (asserted below)
TW_UOFFLEN  equ 9                   ; ...and tw_s_uoff's
TW_NBAND    equ 16                  ; the 286+ spectrum: sixteen bands...
TW_BPITCH2  equ 14                  ; ...at this pitch...
TW_BANDW    equ 12                  ; ...and this bar width, with markers
%ifndef TW_XTNB
TW_XTNB     equ 12                  ; the XT spectrum's bands (SPEC.md 45.24.1):
%endif                              ; bars only, three semitones each
TW_XTPIT    equ (TW_NBAND * TW_BPITCH2) / TW_XTNB  ; the same span...
TW_XTBW     equ TW_XTPIT * TW_BANDW / TW_BPITCH2   ; ...and proportions
%if TW_XTNB > TW_NBAND || TW_XTNB < 2
  %error "TW_XTNB is 2..TW_NBAND"
%endif
%ifndef TW_BHOLD
TW_BHOLD    equ 2                   ; a struck bar holds this many TICKS
%endif                              ; before it falls (SPEC.md 45.24.1): a
                                    ; note struck again at the same level
                                    ; keeps it still instead of a fill down
                                    ; and a fill up every row. Two ticks
                                    ; (~110 ms): three held too long on the
                                    ; songs that are not BEVERLY.MOD
TW_TKMAX    equ 8                   ; the most ticks one frame decays by
TW_SCW      equ 232                 ; the scope band: 29 bytes a row
TW_SCSTR    equ TW_SCW / 8
TW_SCMAX    equ 64                  ; ...and at most this many rows

tw_lay_full:
    dw 4, 4, 411, 49, 4
    dw 54, 77
    dw 272, 60, 407, 71
    dw 8, 82, 407, 93
    dw 4, 98, 243, 167
    dw 98, 16, 18, 8
    dw 172
    dw tw_tiles_full, TW_NT_FULL
    dw tw_omap_full, tw_lk_full
tw_lay_comp:
    dw 4, 4, 411, 37, 3
    dw 41, 60
    dw 272, 45, 407, 56
    dw 8, 64, 407, 73
    dw 4, 77, 243, 124
    dw 77, 14, 16, 6
    dw 128
    dw tw_tiles_comp, TW_NT_COMP
    dw tw_omap_comp, tw_lk_comp
%if (tw_lay_comp - tw_lay_full) != TWL_SZ
  %error "a tw_lay_* record is not TWL_SZ bytes"
%endif
tw_omap_full: db TWO_REP, TWO_SHUF, TWO_LIST, TWO_XT, TWO_VIZ, TWO_RATE
              db TWO_FULL, TWO_ABOUT
tw_omap_comp: db TWO_REP, TWO_SHUF, TWO_LIST, TWO_XT, TWO_VIZ, TWO_FULL
tw_lk_full:   db 0, 1, 2, 3     ; title, time + file, format, position
tw_lk_comp:   db 0, 1, 3        ; ...and CGA gives the format line up

%macro TW_PEN 1                     ; the pen, with AX kept: every rect below
    push ax                         ; is AX-first, and a bare `mov al` would
    mov al, %1                      ; move a control by x mod 256
    call OSAPI_SET_COLOR
    pop ax
%endmacro

; =============================================================================
; Content-relative drawing: these add the origin and nothing else does
; =============================================================================
tw_fill:                            ; AX,BX,CX,DX content rect, the pen as set
    call tw_abs
    call OSAPI_GFX_FILL
    jmp short tw_rel
tw_gray:                            ; ...the 50% dither
    call tw_abs
    call OSAPI_GFX_FILL_GRAY
    jmp short tw_rel
tw_box:
    call tw_abs
    call OSAPI_GFX_FRAME
tw_rel:
    sub ax, [tw_ox]
    sub cx, [tw_ox]
    sub bx, [tw_oy]
    sub dx, [tw_oy]
    ret
tw_abs:
    add ax, [tw_ox]
    add cx, [tw_ox]
    add bx, [tw_oy]
    add dx, [tw_oy]
    ret

; tw_run - SI = string, CX = x, DX = y (content), AL = ink, AH = ground
tw_run:
    add cx, [tw_ox]
    add dx, [tw_oy]
    call OSAPI_FONT_RUN
    sub cx, [tw_ox]
    sub dx, [tw_oy]
    ret


; -----------------------------------------------------------------------------
; tw_well - a SUNKEN bevel ROUND a rect: black top and left, white bottom and
; right, each edge pixel once. The inside is the caller's and never filled.
; in:  AX,BX,CX,DX = the rect (content); preserves all
; -----------------------------------------------------------------------------
tw_well:
    push ax
    push bx
    push cx
    push dx
    mov [tw_e1], ax
    mov [tw_e2], bx
    mov [tw_e3], cx
    mov [tw_e4], dx
    TW_PEN CBLACK
    mov dx, bx                      ; top: x1..x2-1 at y1
    dec cx
    call tw_fill
    mov cx, ax                      ; left: x1 at y1+1..y2
    inc bx
    mov dx, [tw_e4]
    call tw_fill
    TW_PEN CWHITE
    inc ax                          ; bottom: x1+1..x2 at y2
    mov bx, dx
    mov cx, [tw_e3]
    call tw_fill
    mov ax, cx                      ; right: x2 at y1..y2-1
    mov bx, [tw_e2]
    dec dx
    call tw_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tw_pen - AL = a colour, as the pen; preserves all
tw_pen:
    push ax
    call OSAPI_SET_COLOR
    pop ax
    ret

; tw_fills - fill the rect in colour SI (low byte). A rect with x2 < x1 or
; y2 < y1 is EMPTY and draws nothing: every strip below is COMPUTED, and a
; strip of no width is the ordinary case. Preserves all.
tw_fills:
    cmp cx, ax
    jl .no
    cmp dx, bx
    jl .no
    push ax
    mov ax, si
    call OSAPI_SET_COLOR
    pop ax
    call tw_fill
.no:
    ret

; =============================================================================
; tw_track - the window as it is NOW: origin, layout, depth (rule 3 above)
; in:  gfx lock held; preserves all registers
;
; Called at the top of every draw and before every press, because a drag
; calls none of our handlers (SPEC.md 93.3.4.2) and the rects the button
; library hit-tests are SCREEN coordinates. A move rebuilds the rects and
; draws nothing: the kernel's drag cache has already put our pixels where the
; window now is. A LAYOUT or DEPTH change owes the whole face, and says so.
; =============================================================================
tw_track:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = 1: the rects are stale
    mov bx, [trk_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    cmp ax, [tw_ox]
    jne .moved
    cmp dx, [tw_oy]
    je .geom
.moved:
    mov [tw_ox], ax
    mov [tw_oy], dx
    inc di
.geom:
    call OSAPI_WM_GEOM              ; DX = content height
    mov si, tw_lay_full
    cmp dx, TW_HFULL
    jae .lay
    mov si, tw_lay_comp
.lay:
    cmp si, [tw_layp]
    je .depth
    mov [tw_layp], si
    push di
    push ds
    pop es
    mov di, tw_lay
    mov cx, TWL_SZ / 2
    cld
    rep movsw
    pop di
    or byte [tw_dirty], TWI_BODY
    inc di
.depth:
    push di
    mov bx, [trk_win]
    call OSAPI_WM_DISPLAY           ; DH = the bits per pixel of the display
    pop di                          ; THIS WINDOW IS ON (SPEC.md 39.16.4)
    cmp dh, [tw_bpp]
    je .rects
    mov [tw_bpp], dh
    mov ax, (CLGRAY << 8) | CLGREEN ; 4bpp: ModPlug's green LCD, a grey body
    cmp dh, 1
    jne .pal
    mov ax, (CWHITE << 8) | CWHITE  ; 1bpp: green is a dither class and a glyph
.pal:                               ; rounds it to BLACK on the black LCD, and
    mov [tw_ink], al                ; a grey body is a 50% screen under every
    mov [tw_body], ah               ; caption - white body, black outlines is
    or byte [tw_dirty], TWI_BODY    ; the 1bpp look (SPEC.md 56.4)
.rects:
    or di, di
    jz .out
    call tw_rects
.out:
    push ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_rects - the button record's rects, in SCREEN coordinates, from the layout
; and the origin; and its live count, which is ZERO under the About card
; preserves all
; -----------------------------------------------------------------------------
tw_rects:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, tw_rects_b
    mov ax, [tw_ox]
    add ax, TW_BX0
    mov bx, [tw_lay + TWL_TY1]
    add bx, [tw_oy]
    mov dx, [tw_lay + TWL_TY2]
    add dx, [tw_oy]
    mov cx, TW_NTB
.t:
    mov [di], ax
    mov [di+2], bx
    add ax, TW_BW - 1
    mov [di+4], ax
    mov [di+6], dx
    add ax, TW_BPITCH - TW_BW + 1
    add di, 8
    loop .t
    xor cx, cx                      ; CX = the option index
.o:
    cmp cx, [tw_lay + TWL_ON]
    jae .n
    mov ax, cx
    shr ax, 1                       ; the row
    mul word [tw_lay + TWL_OPY]
    add ax, [tw_lay + TWL_OY1]
    add ax, [tw_oy]
    mov [di+2], ax
    add ax, [tw_lay + TWL_OH]
    dec ax
    mov [di+6], ax
    mov ax, [tw_ox]
    add ax, TW_OX1
    test cl, 1
    jz .c0
    add ax, TW_OPX
.c0:
    mov [di], ax
    add ax, TW_OW - 1
    mov [di+4], ax
    add di, 8
    inc cx
    jmp short .o
.n:
    add cx, TW_NTB                  ; CX = every live button...
    cmp byte [trk_abon], 0
    je .live
    xor cx, cx                      ; ...or none, under the About card
.live:
    mov [tw_btns + OS88UI_BT_N], cx
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; tw_bstate - every button's flags and label AS THEY SHOULD BE NOW, into
;             tw_flags and tw_labels. A pure function of the player's state,
;             asked every frame; tw_buttons draws only what it changed.
; preserves all
;
; The transport's three lamps are LATCHES (OS88UI_LATCH, SPEC.md 20.5.1.4):
; the picture a press gives, standing for the state the transport is IN - one
; of Play, Pause and Stop is always down, which is a tape deck's row and needs
; no LED strip under each button the way ModPlug's did.
; =============================================================================
tw_bstate:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, tw_flags_b
    mov cx, TW_NTB
.img:
    mov word [di], OS88UI_IMG
    add di, 2
    loop .img
    cmp byte [mp_playing], 0
    je .stopped
    or word [tw_flags_b + 3*2], OS88UI_LATCH    ; Play
    jmp short .opts
.stopped:
    mov bx, 5*2                     ; Stop, unless this is a PAUSE: a module
    cmp byte [trk_pause], 0         ; parked where the listener was, which
    je .lamp                        ; Play resumes (SPEC.md 45.17)
    cmp byte [mp_loaded], 0
    je .lamp
    mov bx, 4*2
.lamp:
    or word [tw_flags_b + bx], OS88UI_LATCH

.opts:
    xor cx, cx
.o:
    cmp cx, [tw_lay + TWL_ON]
    jae .out
    mov si, [tw_lay + TWL_MAP]
    add si, cx
    mov bl, [si]                    ; BL = the option id
    xor bh, bh
    xor ax, ax                      ; AX = its flags
    shl bx, 1
    mov si, [tw_olab + bx]          ; SI = its fixed label, or 0
    call [tw_ostate + bx]           ; ...and the state routine may change both
    mov bx, cx
    shl bx, 1
    mov [tw_flags_b + TW_NTB*2 + bx], ax
    mov [tw_labels + TW_NTB*2 + bx], si
    inc cx
    jmp short .o
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- the option state routines: AX = flags, SI = label, in and out ------------
tw_os_rep:
    mov bl, [trk_rep]
    xor bh, bh
    shl bx, 1
    mov si, [tw_s_reps + bx]
    or bx, bx
    jz .r
    mov ax, OS88UI_LATCH
.r:
    ret
tw_os_shuf:
    cmp byte [trk_shuf], 0
    je .r
    mov ax, OS88UI_LATCH
.r:
    ret
tw_os_list:
    call tpl_visible                ; ASKED: its close box hides it without
    jc .r                           ; telling us
    mov ax, OS88UI_LATCH
.r:
    ret
tw_os_xt:
    cmp byte [mp_xt], 0
    je .r
    mov ax, OS88UI_LATCH
.r:
    ret
tw_os_viz:
    call tw_vizxhi                  ; ZF = 0: XT mode's 11 kHz forces none,
    je .pick                        ; and the button says it cannot move. The
    mov ax, OS88UI_DIS              ; forced XT meter does NOT grey it: VU
.pick:                              ; Meter <-> Off is still a pick (45.23.1)
    mov bl, [tw_vizm]
    xor bh, bh
    shl bx, 1
    mov si, [tw_s_vizs + bx]
    ret
tw_os_rate:
    push ax
    call trk_rsel_get               ; AH = this mode's pick
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov si, [trk_rname + bx]
    cmp byte [mp_xt], 0
    je .n
    mov si, [trk_xrname + bx]
.n:
    pop ax
    ret
tw_os_full:
    call trk_fs_ok                  ; ONE predicate for the greying and the
    jnc .r                          ; refusal (SPEC.md 47 rule 5)
    mov ax, OS88UI_DIS
.r:
    ret
tw_os_none:
    ret

tw_ostate: dw tw_os_rep, tw_os_shuf, tw_os_list, tw_os_xt, tw_os_viz
           dw tw_os_rate, tw_os_full, tw_os_none
tw_olab:   dw 0, tw_s_shuf, tw_s_list, tw_s_xt, 0, 0, tw_s_full, tw_s_about

; -----------------------------------------------------------------------------
; tw_vizfx - is the visualiser FORCED to the XT meter? ZF = 0 (jne) yes.
; XT mode is the playback setting that says "this is an 8088's budget"
; (SPEC.md 45.9), and [trk_cpu0] is the machine being one whatever the setting
; says (45.9.1) - either is reason enough. Preserves all but the flags.
; -----------------------------------------------------------------------------
tw_vizfx:
%ifdef TRKLOG
    or sp, sp                       ; ZF = 0: always the meter (tw_skick above)
%else
    cmp byte [mp_xt], 0
    jne .out
    cmp byte [trk_cpu0], 0
.out:
%endif
    ret

; -----------------------------------------------------------------------------
; tw_vgeom - the spectrum's shape for this machine and mode: the 286+ face's
; sixteen bands with peak markers (SPEC.md 45.24), or, where the XT meter is
; forced, TW_XTNB bars and no markers (45.24.1). Sets [tw_nb], [tw_mk],
; [tw_bpit], [tw_bw], [tw_bx0]; CF = 1 when the bands or the markers moved,
; so the pane starts again. Preserves all but the flags.
; -----------------------------------------------------------------------------
tw_vgeom:
    push ax
    push bx
    mov ax, TW_NBAND + 100h         ; AL = bands, AH = markers
    mov bx, TW_BPITCH2
    call tw_vizfx
    je .g
    mov ax, TW_XTNB
    mov bx, TW_XTPIT
.g:
    cmp ax, [tw_nb]                 ; tw_mk follows tw_nb: one word
    je .same
    mov [tw_nb], ax
    mov [tw_bpit], bx
    mov ax, bx
    mul byte [tw_nb]                ; the bands' span, centred in 16 x 14
    neg ax
    add ax, TW_NBAND * TW_BPITCH2
    shr ax, 1
    add ax, 8
    mov [tw_bx0], ax
    mov ax, TW_BANDW                ; the 286+ bar, or the XT's in proportion
    cmp byte [tw_mk], 0
    jne .w
    mov ax, TW_XTBW
.w:
    mov [tw_bw], ax
    stc
    jmp short .out
.same:
    clc
.out:
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; tw_vizxhi - is the visualiser forced to NONE? ZF = 0 (jne) yes: forced to
; the XT meter (tw_vizfx) AND in XT mode at 11 kHz, where the mixer leaves the
; XT no time to draw one (SPEC.md 45.21.5). The one state the visualiser
; button greys in. Preserves all but the flags.
; -----------------------------------------------------------------------------
tw_vizxhi:
    call tw_vizfx
    je .out                         ; not forced at all
    cmp byte [mp_xt], 0
    je .out                         ; a tier-0 machine outside XT mode
    cmp byte [trk_xhi], 0
.out:
    ret

; -----------------------------------------------------------------------------
; tw_buttons - draw every button whose flags or label moved since it was last
; drawn. A full paint sets every tw_lastf to 0FFFFh first, which no real flag
; word is. preserves all
; -----------------------------------------------------------------------------
tw_buttons:
    push ax
    push bx
    push cx
    push si
    call tw_bstate
    xor cx, cx
.b:
    cmp cx, [tw_btns + OS88UI_BT_N]
    jae .out
    mov si, cx
    shl si, 1
    mov ax, [tw_flags_b + si]
    mov bx, [tw_labels + si]
    cmp ax, [tw_lastf + si]
    jne .draw
    cmp bx, [tw_lastl + si]
    je .next
.draw:
    mov [tw_lastf + si], ax
    mov [tw_lastl + si], bx
    mov ax, cx
    inc ax                          ; index PLUS ONE, os88ui_btn's convention
    mov bx, tw_btns
    call os88ui_btn
.next:
    inc cx
    jmp short .b
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The LCD and the status line (SPEC.md 45.21.8). A frame asks each line for
; its KEY - the handful of values it is composed from - and a line whose key
; has not moved costs a ten-byte compare and nothing else. One that moved is
; composed and compared with a SHADOW of what is on the glass, and only the
; cells that differ are drawn: the clock ticking is two cells, not fifty.
; =============================================================================

; tw_put - append the NUL string at SI to [DI], printable ASCII only (a module
; title is bytes a stranger wrote, SPEC.md 56.3). DI advances; AX, SI spent.
tw_put:
    lodsb
    or al, al
    jz .out
    cmp al, ' '
    jb .dot
    cmp al, 7Eh
    jbe .st
.dot:
    mov al, '.'
.st:
    mov [di], al
    inc di
    jmp short tw_put
.out:
    ret

; tw_dec2 - AL (0..99) as two decimal digits at [DI]; DI advances
tw_dec2:
    push ax
    aam                             ; AH = tens, AL = units
    add ax, '00'
    xchg al, ah
    mov [di], ax
    add di, 2
    pop ax
    ret

; tw_pad - pad [DI] with spaces to tw_line + TW_CELLS and terminate.
; Preserves all but DI.
tw_pad:
    push ax
    push cx
    push es
    push ds
    pop es
    mov cx, tw_line + TW_CELLS
    sub cx, di
    jbe .t                          ; full already
    mov al, ' '
    cld
    rep stosb
.t:
    mov byte [tw_line + TW_CELLS], 0
    pop es
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_ldiff - draw tw_line where it differs from the shadow at BX (TW_CELLS
; bytes of what the glass shows), at content row DX in ink AL on ground AH,
; then bring the shadow up to date. Preserves all.
;
; The shadow is what replaced the line's HASH: a hash could only say "the
; line moved" and so the whole fifty cells were lettered again every second
; for a clock that moved two of them, and it cost the composing frame a
; 250-byte multiply loop every time whether anything moved or not - 10% of an
; 8088 at XT mode's 5.5 kHz (docs/reports/TRACKER-XT-SPECTRUM-2026-09-24.md).
; `repe cmpsb` finds the first and the last cell that differ and ONE
; font_run letters that span: the cells between two changes are redrawn
; rather than split into two calls, because a call costs more than a cell.
; A shadow of zeroes is never equal - no composed cell is NUL - which is how
; a full paint forgets it.
; -----------------------------------------------------------------------------
tw_ldiff:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, tw_line
    mov di, bx
    mov cx, TW_CELLS
    repe cmpsb
    je .out                         ; every cell as on the glass
    lea cx, [si - 1]                ; CX = the first cell that differs
    mov si, tw_line + TW_CELLS - 1
    lea di, [bx + TW_CELLS - 1]
    push cx
    mov cx, TW_CELLS
    std                             ; ...and from the other end, the last
    repe cmpsb                      ; (there is one: the forward pass found
    cld                             ; it), SI = one BEFORE it
    pop cx
    lea di, [si + 2]                ; DI = one past the last that differs,
    push word [di - 1]              ; terminated for the run. The word saved
    mov byte [di], 0                ; is the last cell and the one after, so
    mov si, cx                      ; it never reaches past tw_line's NUL
    sub cx, tw_line                 ; SI = the span
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, TW_TX                   ; CX = its x: cells are 8 px, so the run
    call tw_run                     ; stays on the byte grid (SPEC.md 6.1)
    pop word [di - 1]
    mov si, tw_line                 ; the shadow takes the whole line: the
    mov di, bx                      ; cells that did not differ are equal
    mov cx, TW_CELLS / 2
    rep movsw
.out:
    pop es
    pop di
    pop si
    pop cx
    ret
%if TW_CELLS & 1
  %error "tw_ldiff copies the shadow in words: TW_CELLS must be even"
%endif
%if TW_KEY != 1 << TW_KEYSH
  %error "TW_KEYSH is log2 of TW_KEY"
%endif

; -----------------------------------------------------------------------------
; tw_lcd_line - compose LCD line kind AL into tw_line
; -----------------------------------------------------------------------------
tw_lcd_line:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tw_line
    cmp al, 1
    je .time
    cmp al, 2
    je .fmt
    cmp al, 3
    je .pos
    ; --- 0: the title --------------------------------------------------------
    mov si, tw_s_nomod
    cmp byte [mp_loaded], 0
    je .t
    mov si, tw_s_untitled
    cmp byte [mp_title], 0
    je .t
    mov si, mp_title
.t:
    call tw_put
    jmp .done
.time:
    ; --- 1: the elapsed time and the file ------------------------------------
    call tw_secs                    ; AX = seconds
    cmp ax, 99*60 + 59              ; 99:59 at most, BEFORE the byte divide:
    jbe .ss                         ; a looping song passes 15,360 s and
    mov ax, 99*60 + 59              ; div bl would overflow into INT 0
.ss:
    mov bl, 60
    div bl                          ; AL = minutes, AH = seconds
    cmp al, 99
    jbe .mm
    mov al, 99
.mm:
    call tw_dec2
    mov byte [di], ':'
    inc di
    mov al, ah
    call tw_dec2
    mov word [di], '  '
    add di, 2
    mov si, tw_s_nofile
    cmp byte [mp_loaded], 0
    je .f
    mov si, trk_fname
.f:
    call tw_put
    jmp .done
.fmt:
    ; --- 2: the format -------------------------------------------------------
    call tw_rate_now                ; AX = Hz
    xor dx, dx
    mov bx, 100
    div bx                          ; AX = tenths of a kHz
    mov bl, 10
    div bl                          ; AL = kHz, AH = the tenth
    push ax
    cmp al, 10
    jae .k2
    mov byte [di], ' '
    inc di
    add al, '0'
    mov [di], al
    inc di
    jmp short .k3
.k2:
    call tw_dec2
.k3:
    pop ax
    mov byte [di], '.'
    mov al, ah
    add al, '0'
    mov [di+1], al
    add di, 2
    mov si, tw_s_khz
    call tw_put
    cmp byte [mp_xt], 0
    je .v
    mov si, tw_s_fxt
    call tw_put
.v:
    mov si, tw_s_vol
    call tw_put
    mov al, [mp_master]             ; 0..64 as a percentage
    mov bl, 100
    mul bl
    mov cl, 6
    shr ax, cl
    push di
    call tui_dec3                   ; tui_nbuf = three digits
    pop di
    mov si, tui_nbuf
    cmp byte [si], '0'              ; ...less the leading zeroes
    jne .pc
    inc si
    cmp byte [si], '0'
    jne .pc
    inc si
.pc:
    call tw_put
    mov byte [di], '%'
    inc di
    jmp .done
.pos:
    ; --- 3: where the song is ------------------------------------------------
    mov si, tw_s_pos
    call tw_put
    mov al, [tui_apos]
    call tui_hexb
    mov byte [di], '/'
    inc di
    mov al, [mp_songlen]
    call tui_hexb
    mov si, tw_s_pat
    call tw_put
    mov al, [tui_apat]
    call tui_hexb
    mov si, tw_s_spd
    call tw_put
    mov al, [tui_aspd]
    call tui_hexb
    mov si, tw_s_bpm
    call tw_put
    mov al, [tui_abpm]
    push di
    call tui_dec3
    pop di
    mov si, tui_nbuf
    call tw_put
    mov al, [tpl_n]                 ; ...and the list, when there is one
    or al, al
    jz .done
    mov si, tw_s_lst
    call tw_put
    mov al, [tpl_cur]
    inc al                          ; 0FFh (none) shows as 00
    call tw_dec2
    mov byte [di], '/'
    inc di
    mov al, [tpl_n]
    call tw_dec2
.done:
    call tw_pad
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; tw_secs - AX = the elapsed seconds: bytes HEARD over the rate they play at.
; Preserves the rest.
tw_secs:
    push bx
    push dx
    mov ax, [tw_el]
    mov dx, [tw_el + 2]
    mov bx, [mp_mixrate]
    or bx, bx
    jz .z
    cmp dx, bx
    jb .dv                          ; a quotient that fits a word
.z:
    xor ax, ax
    xor dx, dx
.dv:
    div bx
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tw_lkey - is line kind AL (TWK_STAT: the status line) still as slot BX drew
; it? Its key is every value the line is composed from, gathered into tw_kbuf
; and compared with the slot's. CF = 0 unchanged; CF = 1 moved, and the
; slot's key now says what it moved to. Preserves all but the flags.
;
; What the key cannot see - a title, a file name or a message whose TEXT
; changes under the same pointer - moves [tw_gen] instead, which is in every
; key: mp_load, the name copy and tui_msg bump it. A zeroed key (a full paint)
; never matches, because a real one starts with 1.
; -----------------------------------------------------------------------------
tw_lkey:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, tw_kbuf
    push ax
    xor ax, ax
    mov cx, TW_KEY / 2
    rep stosw
    pop ax
    mov di, tw_kbuf
    mov ah, [tw_gen]
    xchg al, ah                     ; AL = the generation, AH = the kind
    mov byte [di], 1
    mov [di + 1], al
    add di, 2
    cmp ah, 1
    je .time
    cmp ah, 2
    je .fmt
    cmp ah, 3
    je .pos
    cmp ah, TWK_STAT
    je .stat
    mov al, [mp_loaded]             ; 0: the title
    stosb
    jmp short .cmp
.time:
    mov al, [mp_loaded]
    stosb
    call tw_secs
    stosw
    jmp short .cmp
.fmt:
    call tw_rate_now
    stosw
    mov al, [mp_xt]
    stosb
    mov al, [mp_master]
    stosb
    jmp short .cmp
.pos:
    mov al, [tui_apos]
    stosb
    mov al, [mp_songlen]
    stosb
    mov al, [tui_apat]
    stosb
    mov al, [tui_aspd]
    stosb
    mov al, [tui_abpm]
    stosb
    mov al, [tpl_n]
    stosb
    mov al, [tpl_cur]
    stosb
    jmp short .cmp
.stat:
    mov ax, [tui_msgp]
    stosw
.cmp:
    mov di, bx                      ; DI = the slot's key: BX * TW_KEY
    mov cl, TW_KEYSH
    shl di, cl
    add di, tw_keys
    mov si, tw_kbuf
    mov cx, TW_KEY / 2
    repe cmpsw
    je .same
    mov di, bx                      ; moved: bring the slot's key up to date
    mov cl, TW_KEYSH
    shl di, cl
    add di, tw_keys
    mov si, tw_kbuf
    mov cx, TW_KEY / 2
    rep movsw
    stc
    jmp short .out
.same:
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; tw_rate_now - AX = the rate in Hz: the stream's while one is open, else the
; one the next Play will ask for (trk_play's own choice, trk_rate_pick)
tw_rate_now:
    mov ax, [mp_mixrate]
    cmp byte [trk_sopen], 0
    jne .out
    call trk_rate_pick
.out:
    ret

; -----------------------------------------------------------------------------
; tw_lcd - every LCD cell that moved. With [tw_full] set, also the
; well's bevel and the black strips between the lines - which is the whole
; of the well's ground: the lines are opaque and cover the rest (rule 1).
; preserves all
; -----------------------------------------------------------------------------
tw_lcd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tw_full], 0
    je .lines
    mov ax, [tw_lay + TWL_LX1]      ; the bevel
    mov bx, [tw_lay + TWL_LY1]
    mov cx, [tw_lay + TWL_LX2]
    mov dx, [tw_lay + TWL_LY2]
    call tw_well
    inc ax                          ; the interior: left and right margins
    inc bx                          ; the full height, the strips between the
    dec cx                          ; lines the text columns' width
    dec dx
    mov si, CBLACK
    push cx
    mov cx, TW_TX - 1
    call tw_fills                   ; left margin, x1+1 .. TW_TX-1
    pop cx
    push ax
    mov ax, TW_TX + TW_CELLS * 8
    call tw_fills                   ; right margin, past the text column
    pop ax
    mov [tw_t1], dx                 ; the interior's last row
    mov ax, TW_TX
    mov cx, TW_TX + TW_CELLS * 8 - 1
    xor di, di                      ; BX = the strip's first row, already
.strip:                             ; the interior's top for the first
    cmp di, [tw_lay + TWL_LN]
    jae .last
    push ax
    mov ax, di
    mov dl, TW_LPITCH
    mul dl
    mov dx, ax
    pop ax
    add dx, [tw_lay + TWL_LY1]
    add dx, 2                       ; the row ABOVE line DI (at LY1 + 3 + 10DI)
    call tw_fills
    add dx, 9                       ; ...and the next strip starts under it
    mov bx, dx
    inc di
    jmp short .strip
.last:
    mov dx, [tw_t1]
    call tw_fills                   ; under the last line, to the bottom
.lines:
    xor di, di
.l:
    cmp di, [tw_lay + TWL_LN]
    jae .out
    mov bx, [tw_lay + TWL_KND]
    mov al, [bx + di]
    mov bx, di
    call tw_lkey                    ; CF = 0: nothing it is made of moved
    jnc .n
    call tw_lcd_line
    mov ax, TW_CELLS
    mul di
    add ax, tw_shad
    mov bx, ax                      ; BX = this line's shadow
    mov ax, di
    mov dx, TW_LPITCH
    mul dx
    add ax, [tw_lay + TWL_LY1]
    add ax, 3
    mov dx, ax
    mov al, [tw_ink]
    mov ah, CBLACK
    call tw_ldiff
.n:
    inc di
    jmp short .l
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_status - the status line: [tui_msgp], or the key legend. preserves all
; -----------------------------------------------------------------------------
tw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, TWK_STAT
    mov bx, TW_SHLN
    call tw_lkey
    jnc .out                        ; the same message as drawn
    mov si, [tui_msgp]
    or si, si
    jnz .have
    mov si, tw_s_hint
.have:
    mov di, tw_line
    call tw_put
    call tw_pad
    mov bx, tw_shad + TW_SHLN * TW_CELLS
    mov dx, [tw_lay + TWL_STY]
    mov al, CBLACK
    mov ah, [tw_body]
    call tw_ldiff
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The two sliders. A groove is a well; inside it a THUMB, and either side of
; the thumb black. A move redraws only the strip the thumb left and the thumb
; where it arrived - the erase-the-band-it-left idea SPEC.md 56.12 named as
; ModPlug's missing next step, which also removes the last fill-then-draw
; pair on the face.
; =============================================================================

; tw_slin - SI = the groove's offset in tw_lay -> AX,BX,CX,DX = its INTERIOR
tw_slin:
    mov ax, [tw_lay + si]
    inc ax
    mov bx, [tw_lay + si + 2]
    inc bx
    mov cx, [tw_lay + si + 4]
    dec cx
    mov dx, [tw_lay + si + 6]
    dec dx
    ret

; tw_travel - SI = the groove -> AX = interior x1, CX = the thumb's travel in
; px (the interior's width less the thumb). Preserves the rest.
tw_travel:
    push bx
    push dx
    call tw_slin
    sub cx, ax
    sub cx, TW_THW - 1
    jg .ok
    mov cx, 1
.ok:
    pop dx
    pop bx
    ret

; tw_slx - SI = the groove, AX = value, CX = the value's maximum (>= 1)
;          -> AX = the thumb's content x. Preserves the rest.
tw_slx:
    push bx
    push cx
    push dx
    mov bx, cx
    push ax
    call tw_travel                  ; AX = x1, CX = travel
    pop dx
    push ax
    mov ax, dx
    cmp ax, bx
    jbe .v
    mov ax, bx
.v:
    mul cx                          ; value * travel / max
    div bx
    pop dx
    add ax, dx
    pop dx
    pop cx
    pop bx
    ret

; tw_slval - SI = the groove, AX = a content x, CX = the maximum
;            -> AX = the value under it, 0..max, rounded to the nearest.
;            Preserves the rest.
tw_slval:
    push bx
    push cx
    push dx
    mov bx, cx
    push ax
    call tw_travel                  ; AX = x1, CX = travel
    pop dx
    sub dx, ax
    sub dx, TW_THW / 2              ; the pointer holds the thumb's MIDDLE
    jg .p
    xor ax, ax
    jmp short .out
.p:
    cmp dx, cx                      ; a drag past the window's edge is the
    jbe .q                          ; END of the groove, so the product
    mov dx, cx                      ; below stays inside a word
.q:
    mov ax, dx
    mul bx                          ; x * max...
    mov dx, cx                      ; (x < 416 and max <= 127, so the
    shr dx, 1                       ; product has no high word)
    add ax, dx                      ; ...rounded...
    xor dx, dx
    div cx                          ; ...over the travel
    cmp ax, bx
    jbe .out
    mov ax, bx
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tw_slider - put a slider's thumb at content x AX
; in:  SI = the groove's offset in tw_lay, DI -> the word holding the x it was
;      last drawn at (0FFFFh = never: the whole groove is drawn)
; preserves all
; -----------------------------------------------------------------------------
tw_slider:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov bp, ax                      ; BP = the new thumb x
    cmp bp, [di]
    je .out                         ; where it was: nothing to draw
    call tw_slin                    ; AX..DX = the interior
    push si
    mov si, CBLACK
    cmp word [di], 0FFFFh
    jne .delta
    push ax
    push bx
    push cx
    push dx
    dec ax
    dec bx
    inc cx
    inc dx
    call tw_well                    ; the groove, once
    pop dx
    pop cx
    pop bx
    pop ax
    push cx
    mov cx, bp
    dec cx
    call tw_fills                   ; black left of the thumb...
    pop cx
    mov ax, bp
    add ax, TW_THW
    call tw_fills                   ; ...and right of it
    jmp short .thumb
.delta:
    mov ax, [di]                    ; the strip the thumb LEFT: the old rect
    mov cx, ax                      ; less whatever the new one covers
    add cx, TW_THW - 1
    cmp bp, ax
    jl .left
    push cx                         ; moved right: old x1 .. min(old x2, new-1)
    cmp cx, bp
    jl .r1
    mov cx, bp
    dec cx
.r1:
    call tw_fills
    pop cx
    jmp short .thumb
.left:
    mov ax, bp                      ; moved left: max(old x1, new x2+1) .. old x2
    add ax, TW_THW
    cmp ax, [di]
    jge .l1
    mov ax, [di]
.l1:
    call tw_fills
.thumb:
    pop si
    mov [di], bp
    mov ax, bp                      ; the thumb: a black frame round white,
    mov cx, bp                      ; one write each (rule 1)
    add cx, TW_THW - 1
    push si
    push bx
    push dx
    mov si, CBLACK
    call tw_pen_si
    call tw_box
    inc ax
    inc bx
    dec cx
    dec dx
    mov si, CWHITE
    call tw_fills
    pop dx
    pop bx
    pop si
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tw_pen_si:
    push ax
    mov ax, si
    call OSAPI_SET_COLOR
    pop ax
    ret

; tw_sliders - both thumbs where the state says; preserves all
tw_sliders:
    push ax
    push cx
    push si
    push di
    mov si, TWL_VX1
    mov al, [mp_master]
    xor ah, ah
    mov cx, 64
    call tw_slx                     ; a volume drag moves [mp_master] itself,
    mov di, tw_vthx                 ; so the thumb follows the hand from here
    call tw_slider
    mov si, TWL_SX1
    mov al, [tui_apos]              ; the position being HEARD (45.15)...
    xor ah, ah
    cmp byte [tw_drag], 2
    jne .s
    mov ax, [tw_dx]                 ; ...or the one the hand is holding
.s:
    mov cl, [mp_songlen]
    xor ch, ch
    dec cx
    jg .sm
    mov cx, 1                       ; a one-position song: the thumb stays left
    xor ax, ax
.sm:
    call tw_slx
    mov di, tw_sthx
    call tw_slider
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; The visualiser (SPEC.md 45.21.5): the pane is a well, and what is in it
; depends on the machine as well as the pick.
;
;   XT     four horizontal needles - the same note-driven needles the FT2
;          screen shows (tui_vu, 45.12.1) - each a bar that moves by the
;          difference: a steady needle costs nothing, a moving one ONE fill.
;          Forced on a tier-0 machine and in XT mode (tw_vizfx), because that
;          is the budget those two mean - where the button cycles VU Meter
;          -> Spectrum -> Off, and the spectrum is the XT's own: twelve bars,
;          no markers, one band a note (SPEC.md 45.24.1).
;   286+   a SPECTRUM - sixteen bands kicked by the notes as they are HEARD,
;          one fill per band that moved, with peak markers - or an
;          OSCILLOSCOPE, composed into a 1bpp band in our own RAM and put up
;          with ONE OSAPI_GFX_BLIT1.
;
; ModPlug's versions drew every column and every bar every frame; its scope
; alone was two hundred primitive calls a frame, which is why it did not keep
; up on a 286 either. The cost here is counted in calls, not pixels
; (PERFORMANCE.md), and the heaviest mode makes one.
; =============================================================================

; tw_zin - AX,BX,CX,DX = the pane's interior (content). Preserves the rest.
tw_zin:
    mov ax, [tw_lay + TWL_ZX1]
    inc ax
    mov bx, [tw_lay + TWL_ZY1]
    inc bx
    mov cx, [tw_lay + TWL_ZX2]
    dec cx
    mov dx, [tw_lay + TWL_ZY2]
    dec dx
    ret

; tw_vubh - AX = one needle row's height: (interior height - 5) / 4
tw_vubh:
    push bx
    push cx
    push dx
    call tw_zin
    mov ax, dx
    sub ax, bx
    sub ax, 4
    shr ax, 1
    shr ax, 1
    pop dx
    pop cx
    pop bx
    ret

; tw_vizdraw - the pane: whole when [tw_vfull], else the difference. preserves all
tw_vizdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [tw_vfull], 0
    je .mode
    mov ax, [tw_lay + TWL_ZX1]
    mov bx, [tw_lay + TWL_ZY1]
    mov cx, [tw_lay + TWL_ZX2]
    mov dx, [tw_lay + TWL_ZY2]
    call tw_well
    mov byte [tw_scflat], 0
.mode:
    mov al, [tw_vizm]
    cmp al, TWV_OFF
    je .off
%ifndef TRKLOG
    cmp al, TWV_SPEC
    je .spec
    cmp al, TWV_SCOPE
    je .scope
%endif
    call tw_vu
    jmp short .out
.off:
    call tw_voff
%ifndef TRKLOG
    jmp short .out
.spec:
    call tw_spec
    jmp short .out
.scope:
    call tw_scope
%endif
.out:
    mov byte [tw_vfull], 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_vu - the four needles. [tw_vw + 2i] is what row i shows: a bar length in
; px, or 0FFFFh for "drawn MUTED" (a 50% screen, which reads as off on every
; adapter). A click on a row mutes its channel, like the 1..4 keys.
; -----------------------------------------------------------------------------
tw_vu:
    call tw_vubh
    mov bp, ax                      ; BP = a row's SLOT (its pitch less one)
    mov [tw_t4], ax                 ; ...and [tw_t4] the needle's height in
    cmp byte [tw_vizm], TWV_THIN    ; it: the whole slot, or on the forced XT
    jne .h                          ; meter a THIRD of it, centred. A fill is
    inc ax                          ; priced per ROW on a 1bpp adapter
    xor dx, dx                      ; (PERFORMANCE.md, `fill ns per row`), so
    mov cx, 3                       ; every needle that moves costs a third
    div cx                          ; of what it did - the XT's frame is
    mov [tw_t4], ax                 ; where that is spent
.h:
    call tw_zin
    mov si, CBLACK
    cmp byte [tw_vfull], 0
    je .rows
    mov [tw_t1], dx                 ; the ground: everything in each slot
    xor di, di                      ; that is not its needle, the one-row
.g:                                 ; strips between slots, and what is
    push dx                         ; under the last
    call tw_vuy                     ; DX = this needle's first row
    dec dx
    call tw_fills                   ; BX .. the row above it
    add dx, [tw_t4]
    inc dx
    mov bx, dx                      ; BX = the row under it
    pop dx
    inc di
    cmp di, 4
    jb .g
    mov dx, [tw_t1]
    call tw_fills                   ; ...to the bottom
.rows:
    xor di, di                      ; DI = the channel
.r:
    call tw_vuy
    mov bx, dx
    add dx, [tw_t4]
    dec dx                          ; BX..DX = this needle
    call tw_zin_x                   ; AX, CX = the interior's x span
    cmp byte [mp_mute + di], 0
    je .live
    mov si, di
    shl si, 1
    cmp word [tw_vw + si], 0FFFFh
    jne .mute
    cmp byte [tw_vfull], 0
    je .next
.mute:
    mov word [tw_vw + si], 0FFFFh
    call tw_gray
    jmp short .next
.live:
    push dx
    mov si, cx                      ; the level as a length: vu * width / 64
    sub si, ax
    inc si
    push ax
    mov al, [tui_vu + di]
    xor ah, ah
    mul si
    mov cl, 6
    shr ax, cl
    mov si, ax                      ; SI = the new length
    pop ax
    pop dx
    call tw_zin_x                   ; (CX again)
    push di
    shl di, 1
    mov [tw_t2], di
    pop di
    call tw_bar                     ; AX..CX span, BX..DX rows, SI = length
.next:
    inc di
    cmp di, 4
    jb .r
    ret

; tw_vuy - DI = a channel, BP = the slot, [tw_t4] = the needle's height
;          -> DX = the needle's first row. Preserves the rest.
tw_vuy:
    push ax
    mov ax, bp
    inc ax
    mul di                          ; (DX = 0: four slots of a pane)
    mov dx, ax
    mov ax, [tw_lay + TWL_ZY1]
    add ax, 2                       ; the interior's top, and the strip over
    add dx, ax                      ; the first slot
    mov ax, bp
    sub ax, [tw_t4]
    shr ax, 1
    add dx, ax                      ; ...centred in its slot
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_voff - XT mode at 11 kHz: no meter, and the pane SAYS so rather than
; showing needles that cannot keep up. Drawn once, on a full pane: the line
; lettered in one pass and the black round it in four strips (rule 1).
; -----------------------------------------------------------------------------
tw_voff:
    cmp byte [tw_vfull], 0
    je .out
    mov si, tw_s_voff               ; XT mode's 11 kHz forced it...
    mov di, TW_OFFLEN
    cmp byte [tw_viz], TWV_OFF
    jne .str
    mov si, tw_s_uoff               ; ...or the user PICKED it (SPEC.md 45.23)
    mov di, TW_UOFFLEN
.str:
    mov [tw_t2], si
    call tw_zin
    mov [tw_t1], dx
    mov [tw_t3], cx
    push ax
    add ax, cx
    inc ax
    shr ax, 1
    push di
    shl di, 1
    shl di, 1
    sub ax, di                      ; less half the line (4 px a cell)
    pop di
    mov bp, ax                      ; BP = the line's x
    mov ax, bx
    add ax, dx
    shr ax, 1
    sub ax, 3
    mov [tw_t4], ax                 ; [tw_t4] = its top row
    pop ax
    mov si, CBLACK
    mov dx, [tw_t4]
    dec dx
    call tw_fills                   ; above
    mov bx, [tw_t4]
    add bx, 8
    mov dx, [tw_t1]
    call tw_fills                   ; below
    mov bx, [tw_t4]
    mov dx, bx
    add dx, 7
    mov cx, bp
    dec cx
    call tw_fills                   ; left
    mov ax, di
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, bp                      ; the line's end: 8 px a cell
    mov cx, [tw_t3]
    call tw_fills                   ; right
    mov cx, bp
    mov dx, [tw_t4]
    mov si, [tw_t2]
    mov al, [tw_ink]
    mov ah, CBLACK
    call tw_run
.out:
    ret

; tw_zin_x - AX, CX = the pane interior's x span; preserves the rest
tw_zin_x:
    mov ax, [tw_lay + TWL_ZX1]
    inc ax
    mov cx, [tw_lay + TWL_ZX2]
    dec cx
    ret

; -----------------------------------------------------------------------------
; tw_bar - one needle row to length SI, drawing only the difference
; in:  AX..CX = the row's x span, BX..DX its rows, SI = the new length,
;      [tw_t2] = the channel * 2 (into tw_vw); [tw_vfull] = draw it whole
; preserves all
; -----------------------------------------------------------------------------
tw_bar:
    push ax
    push cx
    push si
    push di
    push bp
    mov bp, ax                      ; BP = x1, [tw_t3] = x2
    mov [tw_t3], cx
    mov di, [tw_t2]
    mov ax, [tw_vw + di]            ; AX = the length on the glass
    mov [tw_vw + di], si            ; ...and from now on, the new one
    cmp byte [tw_vfull], 0
    jne .whole
    cmp ax, 0FFFFh                  ; coming back from MUTED: whole, too
    je .whole
    cmp ax, si
    je .out
    jb .grow
    mov cx, bp                      ; shrink: black from new to old
    add cx, ax
    dec cx
    mov ax, bp
    add ax, si
    mov si, CBLACK
    call tw_fills
    jmp short .out
.grow:
    mov cx, bp                      ; grow: ink from old to new
    add cx, si
    dec cx
    add ax, bp
    mov si, [tw_ink]
    and si, 0FFh
    call tw_fills
    jmp short .out
.whole:
    mov ax, bp
    mov cx, bp
    add cx, si
    dec cx
    push si
    mov si, [tw_ink]
    and si, 0FFh
    call tw_fills                   ; the lit part...
    pop si
    add ax, si
    mov cx, [tw_t3]
    mov si, CBLACK
    call tw_fills                   ; ...and the dark rest
.out:
    pop bp
    pop di
    pop si
    pop cx
    pop ax
    ret

%ifdef TRKLOG
; THE BENCH BUILD CARRIES NO SPECTRUM AND NO SCOPE. TRKLOG is an XT
; instrument (the field log, docs/FIELD-NOTES.md 16) and its tw_vizfx forces
; the meter always, so its button cycles VU Meter <-> Off: ~700 bytes of a
; package that has to fit 60KB with the log in it. The shipped XT spectrum
; (SPEC.md 45.24.1) is not in it.
tw_skick:
    ret
%else
; -----------------------------------------------------------------------------
; tw_spec - [tw_nb] bands, each a column that moves by its difference:
; sixteen with peak markers, or the XT's twelve bars (tw_vgeom). The levels
; live in tw_band (0..64), kicked by tw_skick as a note is HEARD and decayed
; by tw_vtick; tw_bh holds each column's drawn height, and tw_peak / tw_ph
; each band's peak marker (SPEC.md 45.24, 45.24.1).
; -----------------------------------------------------------------------------
tw_spec:
    call tw_zin
    mov [tw_t1], bx                 ; the interior's top...
    mov [tw_t3], dx                 ; ...and bottom
    cmp byte [tw_vfull], 0
    je .cols
    mov si, CBLACK                  ; the ground: every column gap, and the
    mov di, cx                      ; margins either side of the bands
    mov cx, ax
    add cx, [tw_bx0]
    dec cx
    call tw_fills                   ; left margin
    add ax, [tw_bx0]
    add ax, [tw_bw]                 ; ...then the gaps between the bands
    mov cl, [tw_nb]
    xor ch, ch
    dec cx
.gap:
    push cx
    mov cx, ax
    add cx, [tw_bpit]
    sub cx, [tw_bw]
    dec cx
    call tw_fills
    pop cx
    add ax, [tw_bpit]
    loop .gap
    mov cx, di                      ; ...and the right margin, from where the
    call tw_fills                   ; last band ends
.cols:
    call tw_zin_x                   ; AX = interior x1
    add ax, [tw_bx0]                ; AX = band 0's x
    xor di, di
.b:
    push ax
    mov bx, [tw_t3]                 ; BX = the rows: a level is level * rows
    sub bx, [tw_t1]                 ; / 64 of them
    inc bx
    mov cl, 6
    mov al, [tw_band + di]
    xor ah, ah
    mul bx
    shr ax, cl
    mov si, ax                      ; SI = the bar's new height
    mov al, [tw_peak + di]
    xor ah, ah
    mul bx
    shr ax, cl
    mov bp, ax                      ; BP = the marker's
    cmp byte [tw_mk], 0
    jne .mk
    xor bp, bp                      ; the XT spectrum: bars only
.mk:
    pop ax
    call tw_scol
    add ax, [tw_bpit]
    inc di
    mov cl, [tw_nb]
    xor ch, ch
    cmp di, cx
    jb .b
    ret

; -----------------------------------------------------------------------------
; tw_scol - band DI at x AX: a bar SI rows high and its peak marker at BP,
;           both drawn up from [tw_t3]. Preserves all.
;
; ONE PASS OVER THE COLUMN, NOTHING WRITTEN TWICE (PERFORMANCE.md rule 2). A
; marker is a second shape in the column, and the obvious order - the bar's
; own grow/shrink fill, then the marker over it - blacks the rows a marker is
; emerging into and paints them white a moment later, which is the
; double-draw flash. So the column is described as runs of three colours -
; bar (ink), marker (white), ground (black) - for the heights on the glass and
; the heights wanted, cut at every height where either changes colour; each
; run whose colour moved is filled ONCE, with neighbours of one colour merged.
; A settled column costs a compare. Owed whole ([tw_vfull]), every run is
; drawn: black, marker, black, bar - the old two fills plus the marker's two.
;
; A marker at or under the bar is not drawn at all (the bar covers it), which
; is where a fresh peak sits until the bar falls away from it (SPEC.md 45.24).
; -----------------------------------------------------------------------------
tw_scol:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [tw_scx], ax
    cmp bp, si                      ; a marker the bar covers is not one
    ja .pv
    xor bp, bp
.pv:
    mov bl, [tw_bh + di]            ; BX, CX = the column on the glass
    xor bh, bh
    mov cl, [tw_ph + di]
    xor ch, ch
    mov ax, si
    mov [tw_bh + di], al
    mov ax, bp
    mov [tw_ph + di], al
    mov byte [tw_sall], 0
    cmp byte [tw_vfull], 0
    je .same
    inc byte [tw_sall]              ; owed whole: the glass is not known, so
    mov bx, si                      ; every run is drawn and only the NEW
    mov cx, bp                      ; heights cut the column
    jmp short .build
.same:
    cmp cx, bp
    jne .build                      ; the marker moved: the general walk
    cmp bx, si
    jne .fast
    jmp .out                        ; unchanged: nothing to draw
.fast:                              ; THE COMMON FRAME: the marker holds and
    mov dx, bx                      ; the bar moves under it. DX = the higher
    cmp dx, si                      ; of the two bars; clear of the marker's
    jae .fh                         ; foot, the change is ONE run of ONE
    mov dx, si                      ; colour and needs no cuts at all
.fh:
    or bp, bp
    jz .fok
    mov ax, bp
    sub ax, TW_PKH
    cmp dx, ax
    ja .build
.fok:
    mov [tw_srn], bx                ; grow: ink over (old, new]
    mov [tw_srt], si
    mov byte [tw_src], 1
    cmp si, bx
    ja .fgo
    mov [tw_srn], si                ; shrink: ground over (new, old]
    mov [tw_srt], bx
    mov byte [tw_src], 0
.fgo:
    call .flush
    jmp .out
.build:
    mov [tw_sob], bx
    mov [tw_sop], cx
    mov [tw_snb], si
    mov [tw_snp], bp
    mov di, tw_sb                   ; the cut heights, unsorted: 0, the top,
    xor ax, ax                      ; both bars, both markers and the foot of
    call .put                       ; each marker
    mov ax, [tw_t3]
    sub ax, [tw_t1]
    inc ax
    call .put
    mov ax, si
    call .put
    mov ax, bx
    call .put
    mov ax, bp
    call .pk
    mov ax, cx
    call .pk
    sub di, tw_sb
    shr di, 1
    mov [tw_sn], di
    mov cx, 1                       ; insertion sort, at most eight words
.i:
    cmp cx, [tw_sn]
    jae .sorted
    mov si, cx
    shl si, 1
    mov ax, [tw_sb + si]
    mov di, si
.j:
    or di, di
    jz .ins
    mov dx, [tw_sb + di - 2]
    cmp dx, ax
    jbe .ins
    mov [tw_sb + di], dx
    sub di, 2
    jmp short .j
.ins:
    mov [tw_sb + di], ax
    inc cx
    jmp short .i
.sorted:
    mov byte [tw_src], 0FFh         ; no run pending
    mov si, 2                       ; SI = the boundary index, words
.seg:
    mov ax, [tw_sn]
    shl ax, 1
    cmp si, ax
    jae .done
    mov dx, [tw_sb + si - 2]        ; the run (DX, AX]
    mov ax, [tw_sb + si]
    add si, 2
    cmp ax, dx
    je .seg                         ; an empty cut
    push ax
    mov bx, [tw_snb]
    mov cx, [tw_snp]
    call tw_scolr                   ; AL = the colour wanted
    mov ah, al
    cmp byte [tw_sall], 0
    jne .chg
    pop bx                          ; BX = the run's top again
    push bx
    push ax
    mov ax, bx
    mov bx, [tw_sob]
    mov cx, [tw_sop]
    call tw_scolr                   ; AL = the colour on the glass
    mov bl, al
    pop ax
    cmp bl, ah
    jne .chg
    pop ax                          ; this run did not move
    call .flush
    jmp short .seg
.chg:
    pop bx                          ; BX = the run's top, DX its foot, AH its
    cmp byte [tw_src], ah           ; colour: joins the pending run if it
    jne .new                        ; continues it in the same colour
    cmp [tw_srt], dx
    jne .new
    mov [tw_srt], bx
    jmp short .seg
.new:
    call .flush
    mov [tw_srn], dx
    mov [tw_srt], bx
    mov [tw_src], ah
    jmp short .seg
.done:
    call .flush
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.put:                               ; [DI] = AX, DI += 2
    mov [di], ax
    add di, 2
    ret
.pk:                                ; a marker at AX: its top and its foot
    or ax, ax
    jz .pkr
    call .put
    sub ax, TW_PKH
    jnc .pkf
    xor ax, ax
.pkf:
    call .put
.pkr:
    ret
.flush:                             ; fill the pending run, if there is one:
    cmp byte [tw_src], 0FFh         ; heights (from, to] are screen rows
    je .fr                          ; t3 - to + 1 .. t3 - from
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tw_scx]
    mov cx, ax
    add cx, [tw_bw]
    dec cx
    mov bx, [tw_t3]
    sub bx, [tw_srt]
    inc bx
    mov dx, [tw_t3]
    sub dx, [tw_srn]
    mov si, CBLACK
    cmp byte [tw_src], 0
    je .fc
    mov si, CWHITE
    cmp byte [tw_src], 2
    je .fc
    mov si, [tw_ink]
    and si, 0FFh
.fc:
    call tw_fills
    mov byte [tw_src], 0FFh
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.fr:
    ret

; tw_scolr - the colour at height AX (1 = the column's foot) for a bar BX high
;            and a marker at CX (0 = none): AL = 0 ground, 1 bar, 2 marker.
;            Clobbers AX; preserves the rest.
tw_scolr:
    cmp ax, bx
    ja .nb
    mov al, 1
    ret
.nb:
    jcxz .blk
    cmp ax, cx
    ja .blk
    push cx
    sub cx, TW_PKH                  ; SIGNED: a marker nearer the foot than
    cmp ax, cx                      ; its own height runs to the foot
    pop cx
    jle .blk
    mov al, 2
    ret
.blk:
    xor al, al
    ret

; -----------------------------------------------------------------------------
; tw_skick - a note on channel BX became AUDIBLE in the row whose stamp is SI
;            (called by tui_sync, SPEC.md 45.12.1): kick the band it lands in
;            and half as much into each neighbour. preserves all
;
; The band is the NOTE, read from the pattern cell the listener is hearing -
; the stamp names the pattern and the row - so the spectrum follows the music
; exactly as the needles do, and the mixer pays nothing for it. A semitone
; index is a logarithmic frequency axis, which is the axis a spectrum is drawn
; on (SPEC.md 56.6's argument); what this cannot show is harmonic content, and
; it does not pretend to. A cell with no period (an instrument-only retrigger)
; keeps the channel's last note.
; -----------------------------------------------------------------------------
tw_skick:
    cmp byte [tw_vizm], TWV_SPEC
    jne .none
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [si + MP_SP_ROW]
    mov ah, bl                      ; AH = the channel
    mov dl, [si + MP_SP_PAT]
    xor dh, dh
    mov di, dx
    push bx
    call mp_cellptr                 ; ES:SI = the cell, CF = 1 none
    pop bx
    jc .old
    mov ah, [es:si]
    and ah, 0Fh
    mov al, [es:si + 1]             ; AX = the period
    or ax, ax
    jz .old
    push bx
    call mp_pfind                   ; BX = the note, 0..35
    mov al, bl
    pop bx
    mov [tw_chnote + bx], al
.old:
    mov al, [tw_chnote + bx]
    mul byte [tw_nb]                ; band = note * bands / 36
    mov cl, 36
    div cl
    mov cl, al                      ; CL = the band
    xor ch, ch
    mov al, [tui_vu + bx]           ; ...kicked to the needle's own level
    mov di, cx
    call tw_kick1
    cmp byte [tw_mk], 0             ; the XT's bars light ONE band a note:
    je .done                        ; a neighbour is two more columns moving
    shr al, 1
    dec di
    js .r
    call tw_kick1
.r:
    add di, 2
    cmp di, TW_NBAND
    jae .done
    call tw_kick1
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

; tw_kick1 - raise band DI to at least AL, and hold it there TW_BHOLD ticks.
; A strike AT the bar's level renews the hold too: that is the steady note at
; the top, which used to fall one frame and be kicked back the next
tw_kick1:
    cmp al, [tw_band + di]
    jb .out
    mov [tw_band + di], al
    mov byte [tw_bhold + di], TW_BHOLD
.out:
    ret

; -----------------------------------------------------------------------------
; tw_scope - the oscilloscope: the samples the mixer produced last, composed
; into a 1bpp band in our own segment and put up with ONE OSAPI_GFX_BLIT1.
;
; It is the MIXED output, which runs the ring ahead of the card - up to a few
; seconds - and a trace is noise-shaped enough that nobody can tell; the
; needles and the spectrum are the elements that promise synchronisation, and
; they keep it. Stopped, the trace is a flat line drawn once.
; -----------------------------------------------------------------------------
tw_scope:
    call tw_zin
    mov si, dx                      ; the band's rows: the interior less two
    sub si, bx                      ; above and below, at most TW_SCMAX
    sub si, 3
    cmp si, TW_SCMAX
    jbe .rows
    mov si, TW_SCMAX
.rows:
    mov [tw_scr], si
    push dx
    sub dx, bx
    inc dx
    sub dx, si
    shr dx, 1
    add dx, bx
    mov [tw_scy], dx                ; centred in the pane
    pop dx
    cmp byte [tw_vfull], 0
    je .trace
    mov si, CBLACK                  ; the ground round the band: above,
    push dx                         ; below, and the two sides
    mov dx, [tw_scy]
    dec dx
    call tw_fills
    pop dx
    push bx
    mov bx, [tw_scy]
    add bx, [tw_scr]
    call tw_fills
    pop bx
    mov bx, [tw_scy]
    mov dx, bx
    add dx, [tw_scr]
    dec dx
    push cx
    mov cx, TW_TX - 1
    call tw_fills
    pop cx
    mov ax, TW_TX + TW_SCW
    call tw_fills
.trace:
    cmp byte [trk_sopen], 0
    je .flat
    cmp byte [mp_playing], 0
    je .flat
    mov byte [tw_scflat], 0
    call tw_sccomp
    jmp short .blit
.flat:
    cmp byte [tw_scflat], 0         ; once: a stopped trace does not move
    jne .out
    mov byte [tw_scflat], 1
    call tw_scclr
    mov ax, [tw_scr]                ; one line across the middle
    shr ax, 1
    mov bx, TW_SCSTR
    mul bx
    mov di, ax
    add di, tw_scb
    mov cx, TW_SCSTR
    mov al, 0FFh
    push ds
    pop es
    rep stosb
.blit:
    push es
    push bp
    mov al, [tw_ink]
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
    push ds
    pop es
    mov si, tw_scb
    mov bp, TW_SCSTR
    mov ax, TW_TX
    add ax, [tw_ox]
    mov bx, [tw_scy]
    add bx, [tw_oy]
    mov cx, TW_SCW
    mov dx, [tw_scr]
    call OSAPI_GFX_BLIT1            ; CF = 1 refused: the pane stays as it was
    pop bp
    pop es
.out:
    ret

; tw_scclr - clear the band. clobbers AX, CX, DI; ES = DS
tw_scclr:
    push ds
    pop es
    mov ax, [tw_scr]
    mov cx, TW_SCSTR
    mul cx
    mov cx, ax
    shr cx, 1                       ; TW_SCSTR is odd, so round up a word
    inc cx
    mov di, tw_scb
    xor ax, ax
    cld
    rep stosw
    ret

; tw_sccomp - one trace: TW_SCW columns, every 8th sample of mp_outbuf, each
; joined to the last by a vertical run so a steep edge is a line and not dots
tw_sccomp:
    call tw_scclr                   ; (ES = DS from here)
    mov si, mp_outbuf
    xor cx, cx                      ; CX = the column
.c:
    mov al, [si]
    add si, 8
    not al                          ; a high sample is a HIGH row: 255 - s
    xor ah, ah
    mul word [tw_scr]               ; ...times the rows, over 256
    mov bl, ah
    xor bh, bh                      ; BX = this column's row, at unit gain -
    shl bx, 1                       ; and then TWICE it about the middle: four
    mov ax, [tw_scr]                ; channels summed and shifted down fill
    shr ax, 1                       ; the middle third of the pane, and a
    sub bx, ax                      ; trace that small reads as a flat line
    jns .g0                         ; on a 1bpp adapter
    xor bx, bx
.g0:
    cmp bx, [tw_scr]
    jb .g1
    mov bx, [tw_scr]
    dec bx
.g1:
    or cx, cx
    jnz .j
    mov [tw_t1], bx                 ; the first column joins to itself
.j:
    mov di, [tw_t1]                 ; DI = the last column's row
    mov [tw_t1], bx
    cmp di, bx
    jbe .lo
    xchg di, bx                     ; DI = the run's top, BX its bottom
.lo:
    push cx
    push si
    mov ax, cx
    and cl, 7
    mov dl, 80h
    shr dl, cl                      ; DL = the column's bit
    mov cl, 3
    shr ax, cl
    mov si, ax                      ; SI = its byte in a row
    mov ax, di
    mov cl, TW_SCSTR
    mul cl                          ; (a byte multiply: DL survives it)
    add ax, si
    add ax, tw_scb
    mov cx, bx
    sub cx, di
    inc cx                          ; CX = the run's rows
    mov di, ax
.set:
    or [di], dl
    add di, TW_SCSTR
    loop .set
    pop si
    pop cx
    inc cx
    cmp cx, TW_SCW
    jb .c
    ret

%endif

; -----------------------------------------------------------------------------
; tw_vtick - the decay, stepped once for every TICK since the last frame
; (SPEC.md 45.24.1). It was once a FRAME, which is the same thing at 18.2
; frames a second and is SLUGGISH below it: a machine drawing 8 frames a
; second let its bars fall at 44% of their speed. Now a slow machine draws
; fewer of the steps but every step happens, so a bar can be BEHIND and is
; never slow. The WORKER's frame only: a refresh from a key must not make the
; needles fall faster. preserves all
; -----------------------------------------------------------------------------
tw_vtick:
    push ax
    push cx
    call OSAPI_GET_TICKS
    mov cx, ax
    xchg ax, [tw_vtk]
    sub cx, ax                      ; CX = ticks since the last decay
    jz .done
    cmp cx, TW_TKMAX                ; (the first frame, or one after a long
    jbe .n                          ; stall: the bars are at the floor by
    mov cx, TW_TKMAX                ; then either way)
.n:
    call tw_vstep
    loop .n
.done:
    pop cx
    pop ax
    ret

; tw_vstep - one tick's decay. preserves all
tw_vstep:
    push ax
    push bx
    cmp byte [tw_vizm], TWV_SPEC
    je .spec
    xor bx, bx
.v:
    call tui_vu_step                ; the FT2 screen's own needle rule
    inc bx
    cmp bx, 4
    jb .v
    jmp short .out
.spec:                              ; the bars fall fast and a marker keeps
    xor bx, bx                      ; each one's recent high (SPEC.md 45.24)
.s:
    mov al, [tw_band + bx]
    cmp byte [tw_mk], 0
    je .bar                         ; the XT's bars carry no marker
    cmp al, [tw_peak + bx]          ; THE MARKER FIRST, and whatever its bar
    jb .pfall                       ; is doing: the bar's hold is the bar's.
    mov [tw_peak + bx], al          ; A neighbour kicked at half level every
    mov byte [tw_phold + bx], TW_PKHOLD ; row holds its bar for ever, and a
    jmp short .bar                  ; marker behind that hold never fell
.pfall:                             ; (SPEC.md 45.24.1). A new high: the
    cmp byte [tw_phold + bx], 0     ; marker jumps to it and holds a while;
    je .pdrop                       ; under it, it falls to the bar's level
    dec byte [tw_phold + bx]
    jmp short .bar
.pdrop:
    mov byte [tw_phold + bx], TW_PKEVERY - 1 ; the next step's wait
    mov ah, [tw_peak + bx]
    sub ah, TW_PKSTEP
    jnc .pst
    xor ah, ah
.pst:
    mov [tw_peak + bx], ah
.bar:
    cmp byte [tw_bhold + bx], 0     ; a bar just struck holds (45.24.1)
    je .bfall
    dec byte [tw_bhold + bx]
    jmp short .nx
.bfall:
    sub al, TW_SPFALL
    jnc .st
    xor al, al
.st:
    mov [tw_band + bx], al
.nx:
    inc bx
    cmp bx, TW_NBAND
    jb .s
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing: the one update, and the four ways in
; =============================================================================

; tw_body - the body tiles, in the body colour: exactly the pixels no element
; owns (tools/trkface.py checks that the tiles and the elements cover the
; content once). preserves all
tw_bodyfill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [tw_body]
    call tw_pen
    mov si, [tw_lay + TWL_TIL]
    mov di, [tw_lay + TWL_NT]
.t:
    lodsb                           ; x1 / 4
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov cx, ax
    lodsb                           ; (x2 + 1) / 4
    xor ah, ah
    shl ax, 1
    shl ax, 1
    dec ax
    xchg ax, cx
    mov bl, [si]
    xor bh, bh
    mov dl, [si + 1]
    xor dh, dh
    inc si
    inc si
    call tw_fill
    dec di
    jnz .t
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tw_update - the face as the state says, drawing what moved; everything when
; TWI_BODY is owed. Lock held and a clip armed by the caller. preserves all
tw_update:
    push ax
    push bx
    push cx
    push di
    push es
    call tw_track
    mov al, [tw_viz]                ; the visualiser this frame draws
    call tw_vizfx
    je .vm
    call tw_vizxhi                  ; forced: none at XT mode's 11 kHz, where
    mov al, TWV_OFF                 ; the mixer leaves no time to draw one...
    jne .vm
    mov al, [tw_viz]                ; ...the user's Off where it was picked
    cmp al, TWV_OFF                 ; (SPEC.md 45.23), or the XT's own bars
    je .vm                          ; (45.24.1)...
%ifndef TRKLOG
    cmp al, TWV_SPEC
    je .vm
%endif
    mov al, TWV_THIN                ; ...else the XT meter, thin (tw_vu)
.vm:
    call tw_vgeom                   ; CF = 1: the spectrum changed SHAPE (XT
    jc .new                         ; mode, under a spectrum already up)
    cmp al, [tw_vizm]
    je .vs
.new:
    mov [tw_vizm], al
    or byte [tw_dirty], TWI_VIZ
    push ds
    pop es
    mov di, tw_band                 ; a new mode starts from nothing - the
    mov cx, 4 * TW_NBAND            ; bands, their peak markers and both holds
    xor al, al
    cld
    rep stosb
.vs:
    test byte [tw_dirty], TWI_BODY
    jz .viz
    mov byte [tw_full], 1           ; EVERY element's record describes pixels
    mov byte [tw_vfull], 1          ; that are about to be replaced: forget
    push ds                         ; them all here, the one "pixels died"
    pop es                          ; site (SPEC.md 56.12 rule 2)
    mov di, tw_shad
    mov cx, (TW_SHLN + 1) * (TW_CELLS + TW_KEY) / 2
    xor ax, ax
    rep stosw                       ; every shadow and every key (tw_keys
                                    ; follows tw_shad)
    mov di, tw_lastf
    mov cx, TW_NBTN
    dec ax
    rep stosw
    mov [tw_vthx], ax
    mov [tw_sthx], ax
    call tw_bodyfill
    jmp short .draw
.viz:
    test byte [tw_dirty], TWI_VIZ
    jz .draw
    mov byte [tw_vfull], 1
.draw:
    mov byte [tw_dirty], 0
    call tw_lcd
    call tw_buttons
    call tw_sliders
    call tw_status
    call tw_vizdraw
    mov byte [tw_full], 0
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_paint - W_PAINT (the kernel armed the clip): the whole face, and the
; About card over it when it is up (os88ui_about_d: from a PAINTER)
; -----------------------------------------------------------------------------
tw_paint:
    push bx
    push si
    or byte [tw_dirty], TWI_BODY
    call tw_update
    cmp byte [trk_abon], 0
    je .out
    mov bx, [trk_win]
    mov si, tw_ablines
    call os88ui_about_d
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; tw_frame - the worker's frame (trk_render armed the clip and checked the
; About card): the decay, the clock, then what moved
; -----------------------------------------------------------------------------
tw_frame:
    mov byte [tw_inframe], 1
    call tw_tick
    call tw_update
    mov byte [tw_inframe], 0
    ret

; -----------------------------------------------------------------------------
; tw_book - a frame's BOOKKEEPING with nothing drawn: trk_render's answer when
; the About card is up, the window is hidden or it is wholly covered (SPEC.md
; 45.21.9). The position the card is playing, the decay and the clock move on
; every tick whether or not the face can be seen, so the paint that uncovers
; it draws NOW and not the moment it was covered.
;
; They were frame-only, and two 16-bit differences made a long freeze worse
; than stale. [tui_play] refuses a reading "behind" it, and "behind" is a
; signed difference: once the card had played 32 KB past the last frame
; (6 s at 5.5 kHz, 1.5 s at 22) every true reading was refused and the face
; stayed frozen on the old row until the gap wrapped. And the clock adds the
; bytes heard since the last frame modulo 64 KB, so a freeze past that lost
; 11.9 s (at 5.5 kHz) off the elapsed time for the rest of the song.
; Preserves all.
; -----------------------------------------------------------------------------
tw_book:
    cmp byte [tui_inited], 0
    je .out
    call tui_sync                   ; the position being HEARD (SPEC.md 45.15)
    call tw_tick
.out:
    ret

; tw_tick - the decay, then the clock: what a frame does before it draws, and
; all of what a skipped one does (tw_book). Preserves all.
tw_tick:
    push ax
    call tw_vtick
    cmp byte [trk_sopen], 0         ; the elapsed clock counts what the card
    je .up                          ; has PLAYED (tui_play, 45.15), so it
    push bx                         ; stops with the music, pauses with it,
    mov ax, [tui_play]              ; and a covered window misses nothing -
    mov bx, ax                      ; because a covered window's frame still
    sub ax, [tw_lplay]              ; books it (tw_book). AX = the bytes heard
    mov [tw_lplay], bx              ; since the last tick, 16-bit: exact
    add [tw_el], ax                 ; across the wrap only because that is at
    adc word [tw_el + 2], 0         ; most a tick or two of music
    pop bx
.up:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_want - CF = 1 when the worker's frame could draw something, CF = 0 when it
; would take the gfx lock to draw NOTHING (preserves all but flags).
;
; The worker drew ~18 frames a second whatever was happening, and a frame
; takes the lock, which lifts the pointer off the glass (SPEC.md 7.1.4) - so a
; PAUSED player, whose every element was already right, flashed the pointer
; eighteen times a second for nothing. Everything the UI task changes it draws
; itself (tw_refresh), so the frame is owed only to what moves on its own: an
; open stream, a start or stop the transport has not seen, and a visualiser
; still falling. Once all three are still, TW_IDLEF more frames run - the one
; that draws the last fall and the scope's flat line - and then none until
; something moves again.
; -----------------------------------------------------------------------------
TW_IDLEF    equ 2
tw_want:
    push ax
    push bx
    push si
    cmp byte [trk_sopen], 0
    jne .busy
    mov al, [mp_playing]
    cmp al, [tui_lplay]
    jne .busy
    mov si, tui_vu                  ; the needles, or the bands: whichever
    mov bx, 4                       ; the pane is showing is the one falling
    cmp byte [tw_vizm], TWV_SPEC
    jne .scan
    mov si, tw_band                 ; ...and the peak markers after them,
    mov bx, 2 * TW_NBAND            ; which fall after the bars have gone
.scan:
    cmp byte [si], 0                ; (no lodsb: a worker assumes no DF)
    jne .busy
    inc si
    dec bx
    jnz .scan
    cmp byte [tw_idlef], TW_IDLEF
    jae .skip
    inc byte [tw_idlef]
    jmp short .draw
.busy:
    mov byte [tw_idlef], 0
.draw:
    stc
    jmp short .out
.skip:
    clc
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_refresh - draw what changed NOW, from a UI handler (lock held, no clip)
;
; SPEC.md 56.3's lesson: the kernel arms a clip for W_PAINT and for nothing
; else, so a key handler that draws without one draws over whatever window
; is in front of us. This arms it on our own window, draws, and clears it.
; Inside the worker's frame it does nothing - the frame is already drawing,
; and clearing ITS clip would be the same defect from the other side. Under
; the About card it does nothing either (rule 4). preserves all
; -----------------------------------------------------------------------------
tw_redraw:
    or byte [tw_dirty], TWI_BODY
tw_refresh:
    cmp byte [trk_fs], 0
    jne .out
    cmp byte [tw_inframe], 0
    jne .out
    cmp byte [trk_abon], 0
    jne .out
    cmp byte [tui_inited], 0
    je .out
    push bx
    push cx
    push dx
    mov bx, [trk_win]
    call OSAPI_WM_GEOM              ; CF = 1: hidden
    jc .pop
    call OSAPI_WM_CLIP_SET          ; CF = 1: wholly covered
    jc .pop
    call tw_update
    call OSAPI_WM_CLIP_CLEAR
.pop:
    pop dx
    pop cx
    pop bx
.out:
    ret

; tw_clip - arm the clip on our window for a handler that is about to let the
; button library draw; CF ignored (a covered window draws nothing either way)
tw_clip:
    push bx
    push cx
    push dx
    mov bx, [trk_win]
    call OSAPI_WM_GEOM
    jc .out
    call OSAPI_WM_CLIP_SET
.out:
    pop dx
    pop cx
    pop bx
    ret

; tw_newstream - trk_play is opening a stream at mode AL (0 = from the top)
tw_newstream:
    mov word [tw_lplay], 0          ; tui_play restarts with the stream
    or al, al
    jnz .out
    mov word [tw_el], 0             ; ...and the clock with the SONG
    mov word [tw_el + 2], 0
.out:
    ret

; =============================================================================
; Input. The window's W_ONCLICK is tw_clickw, W_ONDRAG tw_ondrag, W_ONMOUSEUP
; tw_onup; the button library sits between them and hands a press that was
; not a button's to tw_onclick (SPEC.md 20.5.1.3.3).
; =============================================================================
tw_clickw:
    call trk_reap
    call trk_abdismiss              ; a click takes the About card down, and
    jc .out                         ; is spent doing it
    call tw_track                   ; the rects where the window IS (rule 3)
    call tw_clip                    ; ...and the press draws inside our window
    jmp os88ui_btnclick             ; the library's, which chains to
.out:                               ; tw_onclick for anything not a button
    ret

; tw_inrect - CX,DX (content) inside the rect at tw_lay + SI? CF = 0 yes
tw_inrect:
    cmp cx, [tw_lay + si]
    jl .no
    cmp cx, [tw_lay + si + 4]
    jg .no
    cmp dx, [tw_lay + si + 2]
    jl .no
    cmp dx, [tw_lay + si + 6]
    jg .no
    clc
    ret
.no:
    stc
    ret

tw_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    sub cx, [tw_ox]
    sub dx, [tw_oy]
    mov si, TWL_VX1
    call tw_inrect
    jc .scrub
    mov byte [tw_drag], 1
    jmp short .grab
.scrub:
    mov si, TWL_SX1
    call tw_inrect
    jc .viz
    cmp byte [mp_loaded], 0
    je .out
    mov byte [tw_drag], 2
.grab:
    call tw_dragto
    jmp short .ref
.viz:
    cmp byte [tw_vizm], TWV_VU      ; a needle row mutes its channel, the
    je .vrow                        ; same gesture as its number key - on
    cmp byte [tw_vizm], TWV_THIN    ; either meter and nothing else
    jne .out
.vrow:
    mov si, TWL_ZX1
    call tw_inrect
    jc .out
    call tw_vubh
    inc ax                          ; AX = a row's pitch
    xchg ax, dx
    sub ax, [tw_lay + TWL_ZY1]
    sub ax, 2
    js .out
    div dl
    cmp al, 4
    jae .out
    call mp_mutetog
.ref:
    call tw_refresh
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tw_dragto - CX = the pointer's content x: the dragged slider's value
tw_dragto:
    push ax
    push cx
    push si
    mov ax, cx
    cmp byte [tw_drag], 1
    jne .s
    mov si, TWL_VX1                 ; the volume is LIVE: heard as it moves
    mov cx, 64
    call tw_slval
    mov [mp_master], al
    jmp short .out
.s:
    mov si, TWL_SX1                 ; the scrubber only SHOWS where it will
    mov cl, [mp_songlen]            ; seek until the release
    xor ch, ch
    dec cx
    jg .m
    xor ax, ax
    jmp short .st
.m:
    call tw_slval
.st:
    mov [tw_dx], ax
.out:
    pop si
    pop cx
    pop ax
    ret

tw_ondrag:
    call tw_clip
    cmp byte [tw_drag], 0
    je .btn
    push cx
    sub cx, [tw_ox]
    call tw_dragto
    pop cx
    jmp tw_refresh
.btn:
    push bx
    mov bx, tw_btns
    call os88ui_btndrag             ; the pressed button follows the pointer
    pop bx
    ret

tw_onup:
    push ax
    push bx
    push cx
    call tw_clip
    cmp byte [tw_drag], 0
    je .btn
    sub cx, [tw_ox]
    call tw_dragto
    cmp byte [tw_drag], 2
    mov byte [tw_drag], 0
    jne .ref
    mov al, [tw_dx]                 ; the scrubber seeks on the RELEASE
    call tw_seek
    jmp short .ref
.btn:
    mov bx, tw_btns                 ; THE ACTION BEFORE THE RELEASE IS DRAWN.
    call tw_prefire                 ; The library redraws the armed button
    push ax                         ; (...and the clip AGAIN after it: the
    call tw_clip                    ; PlayList button opens a window over us,
    pop ax                          ; and the clip armed before it existed
                                    ; drew the release on top of it)
    call os88ui_btnup               ; upright on the release, and a latching
    call tw_synced                  ; one - Shuffle, Play - was then inverted
.ref:                               ; again by the state change a frame later:
                                    ; MEASURED as a 1,080-pixel transient on
                                    ; the glass. Fired first, the release is
                                    ; drawn once, in the state it leaves
    call tw_refresh
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_prefire - W_ONMOUSEUP, before os88ui_btnup: if the release lands on the
; button the press armed - the one case the library will call a FIRE - act
; NOW and recompute every button's flags, so the library's own release draw
; is the button's final picture rather than an upright one the next refresh
; inverts again. in: BX = the record, CX/DX = the release (screen).
; out: [tw_fired] = the armed button (index + 1, 0 none). preserves all
;
; tw_synced then marks that button as drawn with those flags, so the refresh
; after it does not draw it a second time. Both are shared with the PlayList
; editor's record (trklist.inc): BX says which, and [tw_pfact] the action
; table that record fires through.
;
; [tw_fired] IS ITS OWN WORD AND MUST STAY ONE. It is written here, the ACTION
; runs, and tw_synced reads it back - so it lives across whatever the action
; draws, and the face's scratch words (tw_t1..tw_t4) are "serial, under the
; lock" precisely because every drawing routine is free to spend them. It was
; tw_t2 once: an XT-mode rate click repaints the face (trk_rate_set ->
; tui_draw_all), tw_voff parks the 'No meters at 11 kHz' string pointer in
; tw_t2, and tw_synced then used that POINTER as a button index - two word
; stores at tw_lastf/tw_lastl + ~34KB, which land in this package's own CODE
; (tw_rects, tw_bstate). The next Play ran the smashed bytes and the machine
; hung hard. The R key never reached it: only a click goes through here
; (SPEC.md 45.21.10).
; -----------------------------------------------------------------------------
tw_prefire:
    push ax
    push bx
    push di
    call os88ui_armed               ; AX = what the press armed, still armed
    mov [tw_fired], ax
    or ax, ax
    jz .out
    mov di, ax
    push bx
    mov ax, [bx + OS88UI_BT_N]
    mov bx, [bx + OS88UI_BT_RECTS]
    call os88ui_bfind               ; AX = what the release is over
    pop bx
    cmp ax, di
    jne .out                        ; a cancel: nothing to do first
    dec ax
    cmp bx, tw_btns
    jne .list
    call tw_fire
    call tw_bstate
    jmp short .out
.list:
    call tpl_fire
    call tpl_bstate
.out:
    pop di
    pop bx
    pop ax
    ret

tw_synced:
    push ax
    push bx
    push si
    mov si, [tw_fired]
    or si, si
    jz .out
    dec si
    shl si, 1
    cmp bx, tw_btns
    jne .list
    mov ax, [tw_flags_b + si]
    mov [tw_lastf + si], ax
    mov ax, [tw_labels + si]
    mov [tw_lastl + si], ax
    jmp short .out
.list:
    mov ax, [tpl_flags_b + si]
    mov [tpl_lastf + si], ax
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_seek - AL = an order position to play from. Playing, the stream is
; RESTARTED there: a seek that only moved the mixer would be heard a ring's
; worth later - seconds on an XT - and the thumb would snap back to where the
; music still was. Stopped, it only moves the position.
; -----------------------------------------------------------------------------
tw_seek:
    push ax
    cmp byte [mp_playing], 0
    je .park
    push ax
    call trk_play_stop              ; drains the worker (SPEC.md 45.2)
    pop ax
    call mp_setposn
    call tw_seekclk
    mov byte [mp_row], 0
    mov al, 2                       ; ...and RESUMES there
    call trk_play
    jmp short .out
.park:
    call mp_setposn
    call tw_seekclk
.out:
    pop ax
    ret

; tw_seekclk - the clock goes WITH the seek: the time the song reaches the
; position it landed on (mp_timeat), which also leaves the tempo there for the
; resume. A clock that ran on from where the hand left it read the old place
; for the rest of the song. Preserves all
tw_seekclk:
    push ax
    push dx
    mov al, [mp_songpos]
    call mp_timeat
    mov [tw_el], ax
    mov [tw_el + 2], dx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tw_fire - AL = the button that fired (0-based); the gfx lock is held
; -----------------------------------------------------------------------------
tw_fire:
    push ax
    push bx
    xor ah, ah
    mov bx, ax
    cmp al, TW_NTB
    jb .t
    sub bl, TW_NTB
    add bx, [tw_lay + TWL_MAP]
    mov bl, [bx]                    ; the option's id
    xor bh, bh
    add bx, TW_NTB
.t:
    shl bx, 1
    call [tw_acts + bx]
    pop bx
    pop ax
    ret

tw_a_open:
    mov byte [trk_addpl], 0
    jmp trk_do_open
tw_a_rew:
    mov al, [tui_apos]              ; one position back from where the
    dec al                          ; LISTENER is, not the mixer
    jns tw_seek
    xor al, al
    jmp tw_seek
tw_a_fwd:
    mov al, [tui_apos]
    inc al
    jmp tw_seek
tw_a_pause:
    cmp byte [mp_playing], 0
    je .out
    call trk_play_stop
    call trk_transport
.out:
    ret
tw_a_shuf:
    xor byte [trk_shuf], 1
    ret
tw_a_viz:
    call tw_vizxhi
    jne .out                        ; forced none: greyed, and a click on a
    mov al, [tw_viz]                ; greyed control does nothing (SPEC.md 47
    call tw_vizfx                   ; rule 6)
    jne .xt
    inc ax
    cmp al, TWV_N
    jb .set
    xor al, al
    jmp short .set
.xt:                                ; forced to the XT's budget: VU Meter ->
    mov ah, TWV_VU                  ; Spectrum (bars only) -> Off (SPEC.md
    cmp al, TWV_OFF                 ; 45.23.1, 45.24.1). No Scope: it does
    je .xs                          ; not keep up on a 286 either
%ifdef TRKLOG
    mov ah, TWV_OFF                 ; the bench build carries no spectrum
%else
    mov ah, TWV_OFF
    cmp al, TWV_SPEC
    je .xs
    mov ah, TWV_SPEC
%endif
.xs:
    mov al, ah
.set:
    mov [tw_viz], al
.out:
    ret
tw_a_full:
    call trk_fs_ok
    jc .out
    call trk_fs_enter
.out:
    ret

tw_acts:
    dw tw_a_open, tpl_prev, tw_a_rew, trk_play_go
    dw tw_a_pause, trk_stop, tw_a_fwd, tpl_next
    dw trk_rep_cycle, tw_a_shuf, tpl_toggle, trk_xt_toggle
    dw tw_a_viz, trk_rcyc, tw_a_full, trk_about

; =============================================================================
; Data
; =============================================================================

; the button group: eight pictures and up to eight captions (SPEC.md 20.5.1.3)
    OS88UI_BTNREC tw_btns, tw_rects_b, tw_labels, tw_flags_b, 0
tw_labels:
    dw tw_i_open, tw_i_prev, tw_i_rew, tw_i_play
    dw tw_i_pause, tw_i_stop, tw_i_fwd, tw_i_next
    times 8 dw 0                    ; the options' captions, set per frame

tw_s_reps:  dw tw_s_rep0, tw_s_rep1, tw_s_rep2
tw_s_rep0:  db 'No Repeat', 0
tw_s_rep1:  db 'Rpt Song', 0
tw_s_rep2:  db 'Rpt List', 0
tw_s_shuf:  db 'Shuffle', 0
tw_s_list:  db 'PlayList', 0
tw_s_xt:    db 'XT Mode', 0
tw_s_vizs:  dw tw_s_vu, tw_s_spec, tw_s_scope, tw_s_vnone, tw_s_vu
tw_s_vnone: db 'Meter Off', 0
tw_s_voff:  db 'No meters at 11 kHz', 0
%if $ - tw_s_voff - 1 != TW_OFFLEN
%error "tw_s_voff: TW_OFFLEN is the string's length"
%endif
tw_s_uoff:  db 'Meter off', 0         ; ...the PICKED Off's line (SPEC.md 45.23)
%if $ - tw_s_uoff - 1 != TW_UOFFLEN
%error "tw_s_uoff: TW_UOFFLEN is the string's length"
%endif
tw_s_vu:    db 'VU Meter', 0
tw_s_spec:  db 'Spectrum', 0
tw_s_scope: db 'Scope', 0
tw_s_full:  db 'Full Scrn', 0
tw_s_about: db 'About', 0
tw_s_nomod:    db 'Tracker - no module loaded', 0
tw_s_untitled: db '(untitled module)', 0
tw_s_nofile:   db '- no module -', 0
tw_s_khz:   db ' kHz  8 bit mono', 0
tw_s_fxt:   db '  XT', 0
tw_s_vol:   db '  Vol ', 0
tw_s_pos:   db 'Pos ', 0
tw_s_pat:   db '  Pat ', 0
tw_s_spd:   db '  Spd ', 0
tw_s_bpm:   db '  BPM ', 0
tw_s_lst:   db '  List ', 0
tw_s_hint:  db 'ENTER play  SPACE pause  L load  E list  F full', 0

; the About card (os88ui.inc's, SPEC.md 20.5.1): a line table, 0 ends it
tw_ablines:
    dw tw_ab1, tw_ab2, tw_ab3, tw_ab4, tw_ab0, tw_ab5, tw_ab6, tw_ab7, 0
tw_ab1: db 'Tracker for os8088', 0
tw_ab2: db 'A FastTracker II homage', 0
tw_ab3: db 'MOD replay in the style of', 0
tw_ab4: db '8bitbubsy/ft2-clone', 0
tw_ab0: db 0
tw_ab5: db 'Windowed face after ModPlug Player V2', 0
tw_ab6: db 'Ported by Jorge Gonzalez', 0
tw_ab7: db 'Optimized by Elendilon', 0

; --- the transport's pictures: OSAPI_ICON_DRAW records with a FULL mask, so
; --- each is opaque over its own 16 x 10 cell (os88ui.inc OS88UI_IMG) ---------
tw_i_open:
    ;  .......##.......
    ;  ......####......
    ;  .....######.....
    ;  ....########....
    ;  ...##########...
    ;  ..############..
    ;  ................
    ;  ..############..
    ;  ..############..
    ;  ................
    db 1, 10
    times 10 dw 0FFFFh
    dw 00180h
    dw 003C0h
    dw 007E0h
    dw 00FF0h
    dw 01FF8h
    dw 03FFCh
    dw 00000h
    dw 03FFCh
    dw 03FFCh
    dw 00000h
tw_i_prev:
    ;  ..##....#....#..
    ;  ..##...##...##..
    ;  ..##..###..###..
    ;  ..##.####.####..
    ;  ..############..
    ;  ..############..
    ;  ..##.####.####..
    ;  ..##..###..###..
    ;  ..##...##...##..
    ;  ..##....#....#..
    db 1, 10
    times 10 dw 0FFFFh
    dw 03084h
    dw 0318Ch
    dw 0339Ch
    dw 037BCh
    dw 03FFCh
    dw 03FFCh
    dw 037BCh
    dw 0339Ch
    dw 0318Ch
    dw 03084h
tw_i_rew:
    ;  .......#....#...
    ;  ......##...##...
    ;  .....###..###...
    ;  ....####.####...
    ;  ...##########...
    ;  ...##########...
    ;  ....####.####...
    ;  .....###..###...
    ;  ......##...##...
    ;  .......#....#...
    db 1, 10
    times 10 dw 0FFFFh
    dw 00108h
    dw 00318h
    dw 00738h
    dw 00F78h
    dw 01FF8h
    dw 01FF8h
    dw 00F78h
    dw 00738h
    dw 00318h
    dw 00108h
tw_i_play:
    ;  ....##..........
    ;  ....####........
    ;  ....######......
    ;  ....########....
    ;  ....##########..
    ;  ....##########..
    ;  ....########....
    ;  ....######......
    ;  ....####........
    ;  ....##..........
    db 1, 10
    times 10 dw 0FFFFh
    dw 00C00h
    dw 00F00h
    dw 00FC0h
    dw 00FF0h
    dw 00FFCh
    dw 00FFCh
    dw 00FF0h
    dw 00FC0h
    dw 00F00h
    dw 00C00h
tw_i_pause:
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    ;  ....###..###....
    db 1, 10
    times 10 dw 0FFFFh
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
    dw 00E70h
tw_i_stop:
    ;  ................
    ;  ....########....
    ;  ....########....
    ;  ....########....
    ;  ....########....
    ;  ....########....
    ;  ....########....
    ;  ....########....
    ;  ....########....
    ;  ................
    db 1, 10
    times 10 dw 0FFFFh
    dw 00000h
    dw 00FF0h
    dw 00FF0h
    dw 00FF0h
    dw 00FF0h
    dw 00FF0h
    dw 00FF0h
    dw 00FF0h
    dw 00FF0h
    dw 00000h
tw_i_fwd:
    ;  ...#....#.......
    ;  ...##...##......
    ;  ...###..###.....
    ;  ...####.####....
    ;  ...##########...
    ;  ...##########...
    ;  ...####.####....
    ;  ...###..###.....
    ;  ...##...##......
    ;  ...#....#.......
    db 1, 10
    times 10 dw 0FFFFh
    dw 01080h
    dw 018C0h
    dw 01CE0h
    dw 01EF0h
    dw 01FF8h
    dw 01FF8h
    dw 01EF0h
    dw 01CE0h
    dw 018C0h
    dw 01080h
tw_i_next:
    ;  ..#....#....##..
    ;  ..##...##...##..
    ;  ..###..###..##..
    ;  ..####.####.##..
    ;  ..############..
    ;  ..############..
    ;  ..####.####.##..
    ;  ..###..###..##..
    ;  ..##...##...##..
    ;  ..#....#....##..
    db 1, 10
    times 10 dw 0FFFFh
    dw 0210Ch
    dw 0318Ch
    dw 039CCh
    dw 03DECh
    dw 03FFCh
    dw 03FFCh
    dw 03DECh
    dw 039CCh
    dw 0318Ch
    dw 0210Ch

; --- the body tiles: GENERATED by tools/trkface.py, which also checks that they
; --- and the elements cover the content exactly once. FOUR BYTES a tile: x1/4,
; --- (x2+1)/4, y1, y2 - the face's layout keeps every column boundary on a
; --- 4-pixel grid, and the checker refuses a layout that does not ------------
tw_tiles_full:                          ; 40 tiles
    db 0, 104, 0, 3
    db 103, 104, 4, 49
    db 0, 1, 4, 49
    db 0, 104, 50, 53
    db 65, 104, 54, 59
    db 65, 68, 60, 71
    db 102, 104, 60, 71
    db 0, 2, 54, 77
    db 9, 10, 54, 77
    db 25, 26, 54, 77
    db 33, 34, 54, 77
    db 17, 18, 54, 77
    db 49, 50, 54, 77
    db 41, 42, 54, 77
    db 57, 58, 54, 77
    db 65, 104, 72, 77
    db 0, 104, 78, 81
    db 0, 2, 82, 93
    db 102, 104, 82, 93
    db 0, 104, 94, 97
    db 82, 83, 98, 113
    db 103, 104, 98, 113
    db 61, 62, 98, 113
    db 61, 104, 114, 115
    db 82, 83, 116, 131
    db 103, 104, 116, 131
    db 61, 62, 116, 131
    db 61, 104, 132, 133
    db 82, 83, 134, 149
    db 103, 104, 134, 149
    db 61, 62, 134, 149
    db 61, 104, 150, 151
    db 0, 1, 98, 167
    db 82, 83, 152, 167
    db 103, 104, 152, 167
    db 61, 62, 152, 167
    db 0, 104, 168, 171
    db 0, 2, 172, 179
    db 102, 104, 172, 179
    db 0, 104, 180, 183
tw_tiles_comp:                          ; 36 tiles
    db 0, 104, 0, 3
    db 103, 104, 4, 37
    db 0, 1, 4, 37
    db 0, 104, 38, 40
    db 65, 104, 41, 44
    db 65, 68, 45, 56
    db 102, 104, 45, 56
    db 0, 2, 41, 60
    db 9, 10, 41, 60
    db 25, 26, 41, 60
    db 33, 34, 41, 60
    db 17, 18, 41, 60
    db 49, 50, 41, 60
    db 41, 42, 41, 60
    db 57, 58, 41, 60
    db 65, 104, 57, 60
    db 0, 104, 61, 63
    db 0, 2, 64, 73
    db 102, 104, 64, 73
    db 0, 104, 74, 76
    db 82, 83, 77, 90
    db 103, 104, 77, 90
    db 61, 62, 77, 90
    db 61, 104, 91, 92
    db 82, 83, 93, 106
    db 103, 104, 93, 106
    db 61, 62, 93, 106
    db 61, 104, 107, 108
    db 82, 83, 109, 122
    db 103, 104, 109, 122
    db 61, 62, 109, 122
    db 0, 1, 77, 124
    db 61, 104, 123, 124
    db 0, 104, 125, 127
    db 0, 2, 128, 135
    db 102, 104, 128, 135
TW_NT_FULL equ 40
TW_NT_COMP equ 36

; =============================================================================
; bss (through tracker.asm's TRKB/TRKW/TRKBUF accumulator)
; =============================================================================
    TRKW tw_ox                      ; the content origin, SCREEN
    TRKW tw_oy
    TRKW tw_layp                    ; which tw_lay_* is live, 0 = none yet
    TRKBUF tw_lay, TWL_SZ           ; ...and its copy
    TRKB tw_bpp                     ; the display's depth, 0 = not asked yet
    TRKB tw_ink                     ; the LCD's and the meters' ink
    TRKB tw_body                    ; the chassis
    TRKB tw_dirty                   ; TWI_* owed
    TRKB tw_full                    ; this update draws the whole face
    TRKB tw_vfull                   ; ...or the whole visualiser pane
    TRKB tw_inframe                 ; the worker's frame is drawing
    TRKW tw_e1                      ; tw_well's banked rect
    TRKW tw_e2
    TRKW tw_e3
    TRKW tw_e4
    TRKW tw_t1                      ; shared scratch (serial, under the lock)
    TRKW tw_t2
    TRKW tw_t3
    TRKW tw_t4                      ; (the needle height, tw_vu; tw_voff)
    TRKW tw_fired                   ; the button tw_prefire fired, for
                                    ; tw_synced AFTER the action: NOT scratch
    TRKBUF tw_rects_b, TW_NBTN * 8  ; the button record's rects, SCREEN
    TRKBUF tw_flags_b, TW_NBTN * 2  ; ...its flags, this frame
    TRKBUF tw_lastf, TW_NBTN * 2    ; what each button was LAST DRAWN with
    TRKBUF tw_lastl, TW_NBTN * 2
tw_line     equ tui_rbuf            ; one composed LCD, status or list line:
%if TW_CELLS + 1 > TUI_RDCELLS + 1  ; the FT2 screen's readout buffer, which
  %error "tw_line no longer fits tui_rbuf"  ; only the FULLSCREEN surfaces use
%endif                              ; now - and both are drawn under the lock
    TRKBUF tw_shad, (TW_SHLN + 1) * TW_CELLS ; each LCD line AS DRAWN, and
                                    ; the status line's after them (tw_ldiff)
    TRKBUF tw_keys, (TW_SHLN + 1) * TW_KEY  ; ...and what each was composed
                                    ; from (tw_lkey). FOLLOWS tw_shad: a full
                                    ; paint clears both in one stosw
    TRKBUF tw_kbuf, TW_KEY          ; the key being asked about
    TRKB tw_gen                     ; bumped when LCD TEXT changes under an
                                    ; unchanged key: a load, a name, a message
    TRKW tw_vthx                    ; each thumb's drawn x, 0FFFFh = never
    TRKW tw_sthx
    TRKB tw_drag                    ; 0 none, 1 the volume, 2 the scrubber
    TRKW tw_dx                      ; ...the scrubber's held position
    TRKB tw_viz                     ; the visualiser PICK (TWV_*)
    TRKB tw_vizm                    ; ...and the mode on the glass
    TRKBUF tw_vw, 8                 ; the needles' drawn lengths, 0FFFFh muted
    TRKBUF tw_band, TW_NBAND        ; the spectrum's levels, 0..64
    TRKBUF tw_peak, TW_NBAND        ; ...the peak markers' levels - RIGHT
                                    ; AFTER tw_band, so the idle scan and a
                                    ; mode's clear take both in one run
    TRKBUF tw_phold, TW_NBAND       ; ...ticks each marker still holds
    TRKBUF tw_bhold, TW_NBAND       ; ...and each BAR (TW_BHOLD, 45.24.1)
    TRKW tw_vtk                     ; the tick the decay last stepped on
    TRKB tw_nb                      ; the spectrum's shape, set with the mode:
    TRKB tw_mk                      ; bands, markers or none,
    TRKW tw_bpit                    ; pitch, bar width, and band 0's x from
    TRKW tw_bw                      ; the interior's left edge
    TRKW tw_bx0
    TRKBUF tw_bh, TW_NBAND          ; ...and its columns' drawn heights
    TRKBUF tw_ph, TW_NBAND          ; ...and its markers' (0 = none on glass)
    TRKBUF tw_sb, 8 * 2             ; tw_scol's run boundaries (words)
    TRKW tw_sn                      ; ...how many
    TRKW tw_scx                     ; ...the column's x
    TRKW tw_sob                     ; ...and the four heights it compares:
    TRKW tw_sop                     ;    old bar, old marker,
    TRKW tw_snb                     ;    new bar, new marker
    TRKW tw_snp
    TRKW tw_srn                     ; the pending run: from, to, colour code
    TRKW tw_srt
    TRKB tw_src
    TRKB tw_sall                    ; 1 = the column is owed whole
%if tw_peak != tw_band + TW_NBAND
  %error "tw_peak must follow tw_band: tw_want and tw_update take both as one run"
%endif
%if tw_mk != tw_nb + 1
  %error "tw_mk must follow tw_nb: tw_vgeom compares the pair as one word"
%endif

%if tw_bhold != tw_band + 3 * TW_NBAND || tw_phold != tw_band + 2 * TW_NBAND
  %error "tw_phold and tw_bhold follow tw_peak: tw_update clears all four as one run"
%endif
    TRKBUF tw_chnote, 4             ; each channel's last note
    TRKB tw_scflat                  ; the flat trace is on the glass
    TRKW tw_scr                     ; the scope band's rows...
    TRKW tw_scy                     ; ...and its y
    TRKBUF tw_scb, TW_SCSTR * TW_SCMAX + 2  ; the band itself. It BORROWED
                                    ; the text screen's pattern shadow until
                                    ; that became a claim held only inside a
                                    ; text bracket (SPEC.md 45.13.8)
    TRKW tw_el                      ; stream bytes HEARD this song (32-bit)
    TRKW tw_el_hi
    TRKW tw_lplay                   ; [tui_play] as the last frame saw it
    TRKB tw_idlef                   ; still frames run since all went still
