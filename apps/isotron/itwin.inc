; =============================================================================
; apps/isotron/itwin.inc - the windowed renderer (SPEC.md 108.11)
;
; The canvas (itcanvas.inc) is the picture; this puts it in a desktop window
; on every adapter os8088 drives, at 2x across and through a ROW MAP down:
;
;   device x = 2 * logical x, so the 320 columns are 640 device pixels and a
;   device BYTE is 4 logical pixels - two packed canvas bytes;
;   device row r (of Hc = min(content height, 400)) shows logical row
;   s(r) = (r * 200) / Hc, so a logical span [a, b) is ONE run of device rows,
;   [rfirst[a], rfirst[b]).
;
; TWO PRESENTERS, one per kind of display - the SURFACE, asked when it can
; have changed: the content moved, or a whole present is owed (108.11.3):
;
;   MONO   CGA, Hercules, and a colour display the planes cannot reach: each
;          device row's bytes through a 2 x 256 table (the row's parity picks
;          the half), two canvas bytes a device byte, into a strip, and ONE
;          OSAPI_GFX_BLIT1 a box (pen CLCYAN on a colour display).
;   COLOUR VGA, EGA: each logical row's bytes through four 256-WORD tables
;          into a four-plane strip - a step's narrow box converts a logical
;          row once straight into every device row that shows it, a whole
;          present's wide one COPIES a repeated row - and ONE OSAPI_GFX_BLITP
;          a box, DI bit 14 so a covered window is walked.
;
; WHAT GETS PRESENTED is never the screen: each head's steps this wake union
; into its DIRTY BOX (itcanvas.inc it_span), boxes that intersect merge, and
; each is presented minus the overlay text rects - so every device pixel is
; written once (CLAUDE.md, PERFORMANCE.md rule 2). A whole present is owed
; only by a new arena, a relayout or a surface change, and it goes out in
; BANDS across wakes (it_present_band), never in one long hold; a paint that
; covered the whole content WAS one, and pays it off.
;
; THE LOCK PROTOCOL is Dot Delirium's dd_render (SPEC.md 93.5.2): one hold a
; wake, the logic and the canvas inside it, CLIP_SET, and on a colour display
; nothing over us -> CLIP_CLEAR, which unlocks gfx_blitp's plane-major path.
; =============================================================================

IT_HCMAX    equ 400                 ; Hc = min(content height, 400)
IT_HCMIN    equ 120                 ; ...and below this, "Window too small."
IT_PWBMAX   equ 80                  ; 640 device pixels: the whole canvas
IT_PWBMIN   equ 79                  ; ...or one byte short (column 316 is the
                                    ; right face's single top pixel)
IT_MPAGE    equ IT_MLUT >> 8        ; the mono table's first 256-byte page
%if IT_MLUT & 255
  %error "the mono table must start on a 256-byte page"
%endif

; the overlay records (it_ov): three, in row order - the HUD's left, the
; message, the HUD's right (SPEC.md 108.11.5)
OV_R0       equ 0                   ; word: the first device row (picture)
OV_J0       equ 2                   ; byte: the first device byte
OV_J1       equ 3                   ; byte: the last (inclusive)
OV_STR      equ 4                   ; word: the text, 0 = none
OV_SIZE     equ 6
OV_N        equ 3
OV_L        equ 0
OV_M        equ OV_SIZE
OV_R        equ 2 * OV_SIZE

; the governor (SPEC.md 108.11.6), in os88pit.inc's 838 ns counts
IT_G45      equ 53700               ; a lock hold over 45 ms: every 2nd wake
IT_G30      equ 35800               ; ...and back after 32 under 30 ms
IT_GQUIET   equ 32
; a whole present's bands (SPEC.md 108.11.4): IT_BANDR device rows a band, and
; bands while the hold so far plus the last band's cost is under 40 ms
IT_BANDR    equ 4
IT_GBAND    equ 47700

; -----------------------------------------------------------------------------
; it_geom - the content box (a drag moves it and tells nobody, SPEC.md
; 93.3.4.2), and the picture's origin in it: asked by every paint, resize and
; the About card's dismissal, and by the worker through it_geom_ck when the
; frame moved. out: CF = 1 hidden. Clobbers AX, BX, CX, DX.
; -----------------------------------------------------------------------------
it_geom:
    mov bx, [it_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [it_cx], ax
    mov [it_cy], dx
    mov [it_oy], dx
    add ax, [it_xoff]
    mov [it_ox], ax
    mov bx, [it_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height, CF = hidden
    jc .out
    mov [it_cw], cx
    mov [it_ch], dx
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; it_layout_ck - re-cut the geometry if the content's size, or its phase on
; the byte grid, changed (a move that keeps both needs nothing: the drag cache
; carried our pixels and the picture's phase with them). Clobbers AX..DI.
; -----------------------------------------------------------------------------
it_layout_ck:
    mov ax, [it_cw]
    cmp ax, [it_lcw]
    jne it_layout
    mov ax, [it_ch]
    cmp ax, [it_lch]
    jne it_layout
    mov al, byte [it_cx]
    and al, 7
    cmp al, [it_lcx7]
    jne it_layout
    cmp byte [it_laid], 0
    je it_layout
    ret

; -----------------------------------------------------------------------------
; it_layout - the picture's place, size and row map (SPEC.md 108.11.2):
;   the origin is the content's x rounded UP to the byte grid (gfx_blit1 reads
;   an x off it as a head mask, 5.4.2.8; gfx_blitp refuses one) and then
;   centred on whole bytes in what is left - 0 on a 640 desktop, 40 on a
;   Hercules; the width 80 bytes, or 79 if that is all there is; Hc and the
;   row tables; the overlays' rows. A whole present is owed.
; -----------------------------------------------------------------------------
it_layout:
    mov ax, [it_cw]
    mov [it_lcw], ax
    mov ax, [it_ch]
    mov [it_lch], ax
    mov al, byte [it_cx]
    and al, 7
    mov [it_lcx7], al
    mov byte [it_laid], 1
    mov byte [it_full], 1
    mov byte [it_fillr], 1
    mov byte [it_layok], 0
    neg al
    and al, 7                       ; AL = px to the byte grid
    xor ah, ah
    mov bx, ax                      ; BX = that
    mov cx, [it_cw]
    sub cx, ax
    jbe .small                      ; CX = px from the aligned origin
    mov ax, cx
    mov cl, 3
    shr ax, cl                      ; AX = whole bytes there
    cmp ax, IT_PWBMAX
    jbe .w
    mov ax, IT_PWBMAX
.w:
    mov [it_pwb], ax
    cmp ax, IT_PWBMIN
    jb .small
    mov dx, [it_cw]                 ; centre: ((avail - pwb * 8) / 2) & ~7
    sub dx, bx
    shl ax, cl
    sub dx, ax
    shr dx, 1
    and dx, 0xFFF8
    add bx, dx
    mov [it_xoff], bx
    mov ax, [it_cx]
    add ax, bx
    mov [it_ox], ax
    mov ax, [it_ch]
    cmp ax, IT_HCMAX
    jbe .h
    mov ax, IT_HCMAX
.h:
    cmp ax, IT_HCMIN
    jb .small
    mov [it_hc], ax
    mov byte [it_layok], 1
    ; --- rmap[r] = (r * 200) / Hc, rfirst[y] = ceil(y * Hc / 200) -----------
    xor si, si                      ; SI = r
.rm:
    mov ax, si
    mov dx, IT_CROWS
    mul dx                          ; DX:AX = r * 200 (<= 79,800)
    div word [it_hc]
    mov [it_rmap + si], al
    inc si
    cmp si, [it_hc]
    jb .rm
    xor si, si                      ; SI = y
.rf:
    mov ax, si
    mul word [it_hc]                ; DX:AX = y * Hc
    add ax, IT_CROWS - 1
    adc dx, 0
    mov bx, IT_CROWS
    div bx
    mov bx, si
    shl bx, 1
    mov [it_rfirst + bx], ax
    inc si
    cmp si, IT_CROWS
    jbe .rf
    ; --- the overlays' rows: the HUD's two lines, the message above the right
    mov word [it_ov + OV_L + OV_R0], 0
    mov ax, [it_hc]
    sub ax, 24
    mov [it_ov + OV_M + OV_R0], ax
    add ax, 16
    mov [it_ov + OV_R + OV_R0], ax
    call it_ovl_none                ; whatever showed was the old layout's
.small:
    ret

; -----------------------------------------------------------------------------
; it_surf_ck - THE SURFACE (108.11.3), asked through it_surf_wake - by the
; worker, a paint and the About card's dismissal - when it can have changed:
; colour when the window's display has 4 bits a pixel AND gfx_blitp's probe
; (DI bit 15, with bit 14 so the answer is the rect's, not the clip's) takes
; the picture's rect - the one question that also answers "no straddle, and
; not off the screen's side" (5.4.3.2). A change is a whole present.
; Clobbers AX, BX, CX, DX, SI, DI.
; -----------------------------------------------------------------------------
it_surf_ck:
    mov bx, [it_win]
    call OSAPI_WM_DISPLAY           ; DH = bits a pixel (of OUR display)
    mov [it_bpp], dh
    xor dl, dl                      ; DL = the surface wanted: mono...
    cmp dh, 4
    jb .set
    cmp byte [it_nocol], 0
    jne .set                        ; a blitp refused after a yes: mono stays
    push dx
    mov ax, [it_ox]
    mov bx, [it_oy]
    mov cx, [it_pwb]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, [it_hc]
    mov di, 0xC000                  ; the probe, as for the rect unclipped
    call OSAPI_GFX_BLITP
    pop dx
    jc .set
    inc dl                          ; ...or colour
.set:
    cmp dl, [it_surf]
    je .out
    mov [it_surf], dl
    mov byte [it_full], 1
.out:
    ret

; -----------------------------------------------------------------------------
; it_surf_wake - it_surf_ck, asked only when the answer CAN have changed: the
; content moved (a drag can carry the window onto another display), or a
; whole present is starting (a new arena, a relayout, an adapter switch -
; which fires OSAPI_WM_ONRESIZE). The question is a far call and a probe,
; ~11,900 cycles on the XT-VGA, measured: 2.5 ms of every wake - and of
; every band of a whole present - for an answer that does not move. The
; worker, a paint and the About card's dismissal all ask through this.
; -----------------------------------------------------------------------------
it_surf_wake:
    cmp byte [it_full], 1           ; a whole present STARTING (one going out
    je .ask                         ; in bands keeps the answer it started on)
    mov ax, [it_cx]
    cmp ax, [it_scx]
    jne .ask
    mov ax, [it_cy]
    cmp ax, [it_scy]
    je .out
.ask:
    mov ax, [it_cx]
    mov [it_scx], ax
    mov ax, [it_cy]
    mov [it_scy], ax
    call it_surf_ck
.out:
    ret

; -----------------------------------------------------------------------------
; it_pen_ck - after CLIP_SET, before a worker's present: on a colour surface
; with nothing over us the region is just the content rect, which nothing
; here draws outside of, so it is disarmed and gfx_blitp takes its
; plane-major path (Dot Delirium's dd_pok_win); a mono picture on a colour
; display is given its pen. Clobbers AX, BX.
; -----------------------------------------------------------------------------
it_pen_ck:
    mov byte [it_pok], 0
    cmp byte [it_surf], 0
    je .mono
    mov bx, [it_win]
    call OSAPI_WM_OBSCURED          ; covered: the region binds, and DI bit 14
    jc .out                         ; walks it
    call OSAPI_WM_CLIP_CLEAR
    mov byte [it_pok], 1            ; (the harness reads it: plane-major)
    ret
.mono:
    jmp it_paint_pen                ; the pen is the lock hold's (5.4.2.2.2):
.out:                               ; OSAPI_GFX_UNLOCK puts it back
    ret

; -----------------------------------------------------------------------------
; it_geom_ck - the content box and the layout, for the worker: the two far
; calls of it_geom (~3,000 cycles, MEASURED on all three of MartyPC's
; machines) only when the FRAME moved - W_X, W_Y, W_W, W_H, which the record
; publishes (os88api.inc) - or a whole present is owed. An adapter switch is
; it_onresize's, which asks it_geom itself and owes a whole present.
; in: ES = KERNEL_SEG, BX = it_win. out: CF = 1 hidden. Clobbers AX..DI.
; -----------------------------------------------------------------------------
it_geom_ck:
    cmp byte [it_full], 1           ; (a band carrying on asks only if moved)
    je .ask
    mov ax, [es:bx + W_X]
    cmp ax, [it_fx]
    jne .ask
    mov ax, [es:bx + W_Y]
    cmp ax, [it_fy]
    jne .ask
    mov ax, [es:bx + W_W]
    cmp ax, [it_fw]
    jne .ask
    mov ax, [es:bx + W_H]
    cmp ax, [it_fh]
    jne .ask
    clc
    ret
.ask:
    mov ax, [es:bx + W_X]
    mov [it_fx], ax
    mov ax, [es:bx + W_Y]
    mov [it_fy], ax
    mov ax, [es:bx + W_W]
    mov [it_fw], ax
    mov ax, [es:bx + W_H]
    mov [it_fh], ax
    call it_geom
    jc .hid
    call it_layout_ck
    clc
    ret
.hid:
    mov word [it_fw], 0             ; asked again next time
    stc
    ret

; -----------------------------------------------------------------------------
; it_present - the worker's present, lock held (SPEC.md 108.11.4). Nothing
; owed, nothing asked: a wake with no box, no overlay change and no whole
; present owed returns before the clip. Sets it_gpres when it drew, for the
; governor, and it_byield when a banded whole present has more to go.
; Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_present:
    mov byte [it_gpres], 0
    mov byte [it_byield], 0
    cmp byte [it_abon], 0
    jne .out                        ; the About card owns its rect
    cmp byte [it_full], 0
    jne .owed
    cmp byte [it_hudd], 0
    jne .owed
    cmp byte [it_msgd], 0
    jne .owed
    call it_box_any
    jnc .out
    ; --- THE PRESENTATION GOVERNOR (108.11.6): every second wake ------------
    cmp byte [it_gskip], 0
    je .owed
    xor byte [it_godd], 1
    jnz .out                        ; this one only steps; the boxes wait
.owed:
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [it_win]
    test word [es:bx + W_FLAGS], 2
    jz .skip
    call OSAPI_WM_CLIP_SET
    jc .skip                        ; not one pixel of us is showing
    call it_geom_ck
    jc .skip
    cmp byte [it_layok], 0
    je .small
    mov byte [it_gpres], 1
    call it_surf_wake
    call it_pen_ck
    cmp byte [it_full], 0
    je .part
    mov byte [it_gpres], 0          ; a WHOLE present is not the governor's:
    call it_present_band            ; it is a new arena's or a relayout's,
    ret                             ; never a step's (108.11.6)
.part:
    call it_present_boxes
    jc .out                         ; a refused blitp: a whole present owed
    cmp byte [it_hudd], 0           ; A LINE OF TEXT is not a step's either:
    jne .txt                        ; a round's end letters its result, and
    cmp byte [it_msgd], 0           ; on a VGA font_run is a fill and a
    je .out                         ; glyph pass (~1 ms a cell) - so the
.txt:                               ; governor does not count this wake
    mov byte [it_gpres], 0
    call it_ovl_update
.out:
    ret
.small:
    cmp byte [it_full], 0
    je .out
    call it_draw_small
    mov byte [it_full], 0
    call it_box_clear
    ret
.skip:
    ; NOTHING OF US SHOWS: the steps' boxes are dropped and nothing more is
    ; owed for them - the window has no save-under (it never promised
    ; OSAPI_WM_SAVEU), so whatever uncovers it is a W_PAINT of the exposed
    ; rect from the canvas as it is then (SPEC.md 11.3). A whole present
    ; owed stays owed.
    call it_box_clear
    ret

; -----------------------------------------------------------------------------
; it_present_band - THE WHOLE PICTURE, every pixel once, IN BANDS: it_full =
; 1 starts it at device row 0 (the overlays composed then, so their changes
; until it ends wait for it), 2 carries on at [it_band]. Each wake presents
; IT_BANDR rows at a time - the picture minus the overlays' rects, each
; overlay lettered as the band passes its last row - while the hold so far
; plus the last band's cost stays under IT_GBAND (40 ms), and always one; a
; band left over sets it_byield and the worker yields for the next rather
; than sleeping a tick, so the lock is let go between holds and the
; picture takes about what one hold took. The last band adds, after a
; relayout, the content around the picture, black, and settles the boxes.
; A step made while it goes out (a FAST harness game; a relayout in play)
; is presented ABOVE the band only: below it, the band reaches the canvas
; later. The round's pace waits for the end (it_vsync, itrules.inc).
; Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_present_band:
    cmp byte [it_full], 2
    je .go
    call it_ovl_compose
    mov word [it_band], 0
    mov byte [it_full], 2
    mov byte [it_hudd], 0
    mov byte [it_msgd], 0
.go:
    call it_box_any
    jnc .band
    call it_present_boxes           ; (clipped at it_band while it_full = 2)
    jc .out                         ; refused: it_full = 1, from the top again
.band:
    call pit_now
    mov [it_bt0], ax
    mov [it_bt0 + 2], dx
    mov ax, [it_band]
    mov [it_pr0], ax
    add ax, IT_BANDR
    cmp ax, [it_hc]
    jbe .r1
    mov ax, [it_hc]
.r1:
    mov [it_bnew], ax
    mov [it_pr1], ax
    mov word [it_pj0], 0
    mov ax, [it_pwb]
    dec ax
    mov [it_pj1], ax
    call it_pbox
    jc .out                         ; refused: it_full = 1, from the top again
    xor bx, bx                      ; each overlay whose last row this band
.ov:                                ; passed: [band, bnew) holds R0 + 7
    cmp word [it_ov + bx + OV_STR], 0
    je .on
    mov ax, [it_ov + bx + OV_R0]
    add ax, 8
    cmp ax, [it_band]
    jbe .on
    cmp ax, [it_bnew]
    ja .on
    call it_ovl_draw
.on:
    add bx, OV_SIZE
    cmp bx, OV_N * OV_SIZE
    jb .ov
    mov ax, [it_bnew]
    mov [it_band], ax
    cmp ax, [it_hc]
    jae .done
    ; --- another, if the hold has room for one more like it ----------------
    call pit_now
    mov bx, ax
    mov cx, dx                      ; CX:BX = now
    sub ax, [it_bt0]
    sbb dx, [it_bt0 + 2]            ; DX:AX = the band just made
    or dx, dx
    jnz .more
    mov si, ax
    mov ax, bx
    mov dx, cx
    sub ax, [it_gt0]
    sbb dx, [it_gt0 + 2]            ; DX:AX = the hold so far
    or dx, dx
    jnz .more
    add ax, si
    jc .more
    cmp ax, IT_GBAND
    jbe .band
.more:
    mov byte [it_byield], 1
.out:
    ret
.done:
    IT_MARK it_fdone
    mov byte [it_full], 0
    cmp byte [it_fillr], 0
    je .fr
    call it_fill_rest
    mov byte [it_fillr], 0
.fr:
    call it_box_clear
    ret

; -----------------------------------------------------------------------------
; it_box_any - CF = 1 when a head has moved since its box was presented.
; Clobbers AX.
; -----------------------------------------------------------------------------
it_box_any:
    push bx
    push cx
    mov bx, it_heads
    mov cx, MAXHEADS + 1
.l:
    cmp byte [bx + H_BMV], 0
    jne .yes
    add bx, H_SIZE
    loop .l
    clc
    jmp short .out
.yes:
    stc
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_present_boxes - every head's box this wake, in device rows and bytes,
; the INTERSECTING ones merged (two heads from one start cell, a player
; crossing a computer's column) so no pixel is written twice, each presented
; minus the overlays. out: CF = 1 a blitp refused (a whole present owed).
; -----------------------------------------------------------------------------
it_present_boxes:
    xor di, di                      ; DI = the boxes' list, 8 bytes each
    mov bx, it_heads
    mov cx, MAXHEADS + 1
.gather:
    cmp byte [bx + H_BMV], 0
    je .gn
    mov ax, [bx + H_BX0]
    shr ax, 1
    shr ax, 1
    mov [it_bxs + di], ax           ; j0 = x0 / 4
    mov ax, [bx + H_BX1]
    shr ax, 1
    shr ax, 1
    mov [it_bxs + di + 2], ax       ; j1 = x1 / 4
    mov al, [bx + H_BY0]
    xor ah, ah
    mov si, ax
    shl si, 1
    mov ax, [it_rfirst + si]
    mov [it_bxs + di + 4], ax       ; r0 = rfirst[y0]
    mov al, [bx + H_BY1]
    xor ah, ah
    mov si, ax
    shl si, 1
    mov ax, [it_rfirst + si]
    cmp byte [it_full], 2           ; a whole present going out in bands:
    jne .r1                         ; only the rows above the band, which it
    cmp ax, [it_band]               ; has passed - below it, the band reaches
    jbe .r1                         ; the canvas later
    mov ax, [it_band]
.r1:
    mov [it_bxs + di + 6], ax       ; r1 = rfirst[y1]
    cmp ax, [it_bxs + di + 4]
    jbe .gn                         ; no device row shows it (a CGA drop)
    add di, 8
.gn:
    add bx, H_SIZE
    loop .gather
    call it_box_clear
    mov [it_nbx], di
    ; --- merge: any two that intersect become their bounding box ----------
.again:
    xor si, si                      ; SI = a
.ma:
    mov di, si
.mb:
    add di, 8                       ; DI = b > a
    cmp di, [it_nbx]
    jae .an
    mov ax, [it_bxs + si]           ; a.j0 <= b.j1, b.j0 <= a.j1,
    cmp ax, [it_bxs + di + 2]       ; a.r0 < b.r1, b.r0 < a.r1
    ja .mb
    mov ax, [it_bxs + di]
    cmp ax, [it_bxs + si + 2]
    ja .mb
    mov ax, [it_bxs + si + 4]
    cmp ax, [it_bxs + di + 6]
    jae .mb
    mov ax, [it_bxs + di + 4]
    cmp ax, [it_bxs + si + 6]
    jae .mb
    mov ax, [it_bxs + di]           ; a = a U b
    cmp ax, [it_bxs + si]
    jae .u1
    mov [it_bxs + si], ax
.u1:
    mov ax, [it_bxs + di + 2]
    cmp ax, [it_bxs + si + 2]
    jbe .u2
    mov [it_bxs + si + 2], ax
.u2:
    mov ax, [it_bxs + di + 4]
    cmp ax, [it_bxs + si + 4]
    jae .u3
    mov [it_bxs + si + 4], ax
.u3:
    mov ax, [it_bxs + di + 6]
    cmp ax, [it_bxs + si + 6]
    jbe .u4
    mov [it_bxs + si + 6], ax
.u4:
    sub word [it_nbx], 8            ; b = the last; the list one shorter
    mov bx, [it_nbx]
    mov ax, [it_bxs + bx]
    mov [it_bxs + di], ax
    mov ax, [it_bxs + bx + 2]
    mov [it_bxs + di + 2], ax
    mov ax, [it_bxs + bx + 4]
    mov [it_bxs + di + 4], ax
    mov ax, [it_bxs + bx + 6]
    mov [it_bxs + di + 6], ax
    jmp .again                      ; a grew: ask again from the start
.an:
    add si, 8
    cmp si, [it_nbx]
    jb .ma
    ; --- present each --------------------------------------------------------
    xor si, si
.pr:
    cmp si, [it_nbx]
    jae .done
    mov ax, [it_bxs + si]
    mov [it_pj0], ax
    mov ax, [it_bxs + si + 2]
    mov [it_pj1], ax
    mov ax, [it_bxs + si + 4]
    mov [it_pr0], ax
    mov ax, [it_bxs + si + 6]
    mov [it_pr1], ax
    push si
    call it_pbox
    pop si
    jc .out
    add si, 8
    jmp short .pr
.done:
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; it_pbox - present device rows [it_pr0, it_pr1) by bytes [it_pj0, it_pj1] of
; the picture, MINUS every overlay's rect (each 8 rows; at most one an overlay
; row band, the three in row order), so text the overlays drew is never
; drawn over and then drawn again. out: CF = 1 a blitp refused.
; Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_pbox:
    mov ax, [it_pwb]                ; clip to the picture
    dec ax
    cmp [it_pj1], ax
    jle .c1
    mov [it_pj1], ax
.c1:
    mov ax, [it_hc]
    cmp [it_pr1], ax
    jle .c2
    mov [it_pr1], ax
.c2:
    mov ax, [it_pj0]
    cmp ax, [it_pj1]
    jg .none
    mov ax, [it_pr0]
    cmp ax, [it_pr1]
    jge .none
    mov ax, [it_pr1]                ; the box's end, kept across the bands
    mov [it_pbe], ax
    xor bx, bx                      ; BX = the overlay
.ov:
    cmp word [it_ov + bx + OV_STR], 0
    je .on
    mov ax, [it_ov + bx + OV_R0]    ; rows [R0, R0 + 8) against [r0, end)
    cmp ax, [it_pbe]
    jge .on
    add ax, 8
    cmp ax, [it_pr0]
    jle .on
    mov al, [it_ov + bx + OV_J1]    ; bytes [J0, J1] against [j0, j1]
    xor ah, ah
    cmp ax, [it_pj0]
    jl .on
    mov al, [it_ov + bx + OV_J0]
    cmp ax, [it_pj1]
    jg .on
    ; --- rows above the overlay: the whole width -----------------------------
    mov ax, [it_ov + bx + OV_R0]
    cmp ax, [it_pr0]
    jle .band
    mov [it_pr1], ax
    push bx
    call it_praw
    pop bx
    jc .out
    mov ax, [it_ov + bx + OV_R0]
    mov [it_pr0], ax
.band:
    ; --- the overlay's rows: left of it, then right of it --------------------
    mov ax, [it_ov + bx + OV_R0]
    add ax, 8
    cmp ax, [it_pbe]
    jle .be
    mov ax, [it_pbe]
.be:
    mov [it_pr1], ax
    push word [it_pj0]
    push word [it_pj1]
    mov al, [it_ov + bx + OV_J0]
    xor ah, ah
    dec ax
    cmp ax, [it_pj0]
    jl .right
    mov [it_pj1], ax
    push bx
    call it_praw
    pop bx
    jc .bad2
.right:
    pop word [it_pj1]
    pop word [it_pj0]
    push word [it_pj0]
    push word [it_pj1]
    mov al, [it_ov + bx + OV_J1]
    xor ah, ah
    inc ax
    cmp ax, [it_pj1]
    jg .bdone
    mov [it_pj0], ax
    push bx
    call it_praw
    pop bx
    jc .bad2
.bdone:
    pop word [it_pj1]
    pop word [it_pj0]
    mov ax, [it_pr1]                ; on below the band
    mov [it_pr0], ax
    cmp ax, [it_pbe]
    jge .none
.on:
    add bx, OV_SIZE
    cmp bx, OV_N * OV_SIZE
    jb .ov
    mov ax, [it_pbe]
    mov [it_pr1], ax
    call it_praw
    ret
.bad2:
    pop word [it_pj1]
    pop word [it_pj0]
    stc
.out:
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; it_praw - present device rows [it_pr0, it_pr1) by bytes [it_pj0, it_pj1],
; nothing excluded, in the surface's format. out: CF = 1 a blitp refused
; (it_nocol is set and a whole present owed: the mono presenter takes over).
; Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_praw:
    push word [it_pr0]              ; the rect comes back as it went in: the
    push word [it_pj0]              ; presenters walk it, and it_pbox cuts
    push word [it_pj1]              ; two pieces out of one band
    cmp byte [it_surf], 0
    jne .c
    call it_praw1
    jmp short .x
.c:
    call it_praw4
.x:
    pop word [it_pj1]               ; (pop writes no flag: CF comes out)
    pop word [it_pj0]
    pop word [it_pr0]
    ret

; --- MONO: one OSAPI_GFX_BLIT1 a chunk of rows ---------------------------------
; A device byte is two canvas bytes (E | O each), through the row parity's two
; table pages: HI[b0] | LO[b1] - a 37-byte loop, ~222 cycles a device byte
; MEASURED on MartyPC's 5150 Hercules (a whole present's 22,480 bytes); a
; step's box is 1 to 3 of them a row. The row loop keeps the page pair in BH
; (the next row's is `xor bh, 2`) and ends on a row number made once.
it_praw1:
    mov ax, [it_pj1]
    sub ax, [it_pj0]
    inc ax
    mov [it_pnb], ax                ; bytes a row
    mov cx, ax
    mov ax, IT_STRIPSZ
    xor dx, dx
    div cx
    cmp ax, 255                     ; gfx_blit1 takes 1..255 rows
    jbe .cap
    mov ax, 255
.cap:
    mov [it_pcap], ax               ; rows a strip
.chunk:
    mov ax, [it_pr1]
    sub ax, [it_pr0]
    jle .done
    cmp ax, [it_pcap]
    jbe .n
    mov ax, [it_pcap]
.n:
    mov [it_pn], ax
    add ax, [it_pr0]
    mov [it_pend], ax               ; the strip's end row
    ; --- convert it_pn rows into the strip -----------------------------------
    mov ax, [it_pj0]                ; device byte j0's first column, 4 * j0:
    mov dx, 4 * IT_CCOL             ; its row-0 byte is j0 * 800
    mul dx
    mov dx, ax                      ; DX = j0 * 800
    mov bp, [it_pr0]                ; BP = the device row
    mov bh, IT_MPAGE                ; BH = the row parity's HI page
    test bp, 1
    jz .par
    mov bh, IT_MPAGE + 2
.par:
    mov di, IT_STRIP
    push ds
    mov es, [it_cseg]               ; ES = the canvas: its reads carry the
    mov ds, [it_seg]                ; override; DS = the work claim: the
    cld                             ; tables and the strip; bss through CS
.row:
    mov al, [cs:it_rmap + bp]       ; (the package's own segment is CS)
    xor ah, ah
    add ax, dx
    mov si, ax                      ; SI = row y of column 4 * j0
    mov cx, [cs:it_pnb]
.col:
    mov al, [es:si]                 ; columns 4j and 4j + 1 (an even column's
    or al, [es:si + IT_CCOL]        ; id high, an odd one's low): b0 ...
    mov bl, al
    mov ah, [bx]                    ; HI[b0]
    mov al, [es:si + 2 * IT_CCOL]   ; ... columns 4j + 2 and 4j + 3: b1
    or al, [es:si + 3 * IT_CCOL]
    mov bl, al
    or ah, [bx + 256]               ; | LO[b1]
    mov [di], ah
    inc di
    add si, 4 * IT_CCOL             ; the next device byte's four columns
    loop .col
    xor bh, 2                       ; the other parity's pages (1,024-aligned)
    inc bp
    cmp bp, [cs:it_pend]
    jb .row
    pop ds
    ; --- and down -----------------------------------------------------------
    mov es, [it_seg]
    mov si, IT_STRIP                ; ES:SI = the band
    mov bp, [it_pnb]                ; its stride
    mov ax, [it_pj0]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [it_ox]                 ; x: on the byte grid
    mov bx, [it_pr0]
    add bx, [it_oy]
    mov cx, [it_pnb]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, [it_pn]
    call OSAPI_GFX_BLIT1            ; legal arguments, a band in our claim:
    mov ax, [it_pn]                 ; there is nothing to fall back to
    add [it_pr0], ax
    jmp .chunk
.done:
    clc
    ret

; IT_C4B ps, two - ONE device byte: canvas bytes at ES:SI (row y of the
; byte's first column; SI steps on to the next byte's) through the four word
; tables into the four-plane strip at DS:DI (= plane 2's byte, DI steps on),
; PS = ps; `two` = 1 writes the strip row below as well (stride 4 * PS). BH
; = 0 in and out. Clobbers AX, BX, CX, DX.
%macro IT_C4B 2
    mov al, [es:si]                 ; columns 4j, 4j + 1: b0
    or al, [es:si + IT_CCOL]
    mov ah, [es:si + 2 * IT_CCOL]   ; columns 4j + 2, 4j + 3: b1
    or ah, [es:si + 3 * IT_CCOL]
    add si, 4 * IT_CCOL
    mov bl, al
    shl bx, 1
    mov cx, [bx + IT_TA01]          ; planes 0 and 1 of b0's (high nibbles)
    mov dx, [bx + IT_TA23]          ; planes 2 and 3
    mov bl, ah
    mov bh, 0
    shl bx, 1
    or cx, [bx + IT_TB01]           ; | b1's (low nibbles)
    or dx, [bx + IT_TB23]
    mov bh, 0
    mov [di - 2 * %1], cl
    mov [di - %1], ch
    mov [di], dl
    mov [di + %1], dh
  %if %2
    mov [di + 4 * %1 - 2 * %1], cl  ; ...and the row below, the same bytes
    mov [di + 4 * %1 - %1], ch
    mov [di + 4 * %1], dl
    mov [di + 4 * %1 + %1], dh
  %endif
    inc di
%endmacro

; --- COLOUR: one OSAPI_GFX_BLITP a chunk of rows, at most 64 bytes across -------
; The strip is row-major with the four planes INSIDE a row, PS bytes apart
; (16 for a box of up to 16 bytes, 64 for a wider one), so each plane byte
; is a store on an 8-bit displacement from plane 2: [di-2PS], [di-PS], [di],
; [di+PS]. A canvas byte pair is two lookups of two words a pair - planes 0
; and 1 from one, 2 and 3 from the other - ~402 cycles a device byte
; MEASURED on MartyPC's XT-VGA (it_c4_64 in a whole present). The
; conversion is per LOGICAL row: a narrow box writes it into both device
; rows that show it (Hc 400: every row twice), a wide one copies the row.
it_praw4:
    mov ax, [it_pj1]                ; a piece of at most 64 bytes
    sub ax, [it_pj0]
    inc ax
    cmp ax, 64
    jbe .one
    push word [it_pj1]              ; wider: the first 64, then the rest
    push word [it_pr0]
    mov ax, [it_pj0]
    add ax, 63
    mov [it_pj1], ax
    call it_praw4p
    pop word [it_pr0]
    pop word [it_pj1]
    jc .out
    add word [it_pj0], 64
    jmp short it_praw4
.one:
    call it_praw4p
.out:
    ret

it_praw4p:
    mov ax, [it_pj1]
    sub ax, [it_pj0]
    inc ax
    mov [it_pnb], ax
    mov word [it_pps], 16
    mov word [it_pcap], IT_STRIPSZ / 64
    cmp ax, 16
    jbe .chunk
    mov word [it_pps], 64
    mov word [it_pcap], IT_STRIPSZ / 256
.chunk:
    mov ax, [it_pr1]
    sub ax, [it_pr0]
    jg .some
    clc
    ret
.some:
    cmp ax, [it_pcap]
    jbe .n
    mov ax, [it_pcap]
.n:
    mov [it_pn], ax
    mov ax, [it_pj0]
    mov dx, 4 * IT_CCOL
    mul dx
    mov [it_psrc], ax               ; device byte j0's row-0 canvas byte
    mov ax, [it_pr0]
    add ax, [it_pn]
    mov [it_pend], ax               ; the strip's end row
    mov bp, [it_pr0]                ; BP = the device row
    push ds
    mov es, [it_cseg]               ; ES = the canvas (reads overridden)
    mov ds, [it_seg]                ; DS = the work claim: tables, strip
    cld
    cmp word [cs:it_pps], 16
    jne .wide
    ; --- A NARROW BOX (a step's): each LOGICAL row converted once, straight
    ; into BOTH device rows when the row map shows it twice (Hc 400: every
    ; row) - four more 8-bit-displacement stores a byte, where a copy of the
    ; row was ~1,000 cycles. The row loop is HERE, around the byte loop: the
    ; device row rides on the stack across it, and DI steps on to the next
    ; strip row by a stride made once (64 or 128, less the bytes it moved)
    mov ax, 64
    sub ax, [cs:it_pnb]
    mov [cs:it_pst1], ax            ; past one strip row
    add ax, 64
    mov [cs:it_pst2], ax            ; ...and past two
    mov di, IT_STRIP + 32           ; plane 2 of the strip's first row
.r16:
    mov al, [cs:it_rmap + bp]       ; y, this device row's
    xor ah, ah
    mov si, ax
    add si, [cs:it_psrc]            ; SI = row y of column 4 * j0
    inc bp
    cmp bp, [cs:it_pend]
    jae .s16                        ; the strip's last row: alone
    cmp al, [cs:it_rmap + bp]
    jne .s16
    inc bp                          ; TWO device rows of one logical row
    push bp
    mov bp, [cs:it_pnb]
    xor bx, bx
.c16d:
    IT_C4B 16, 1
    dec bp
    jnz .c16d
    add di, [cs:it_pst2]
    pop bp
    cmp bp, [cs:it_pend]
    jb .r16
    jmp .down
.s16:
    push bp
    mov bp, [cs:it_pnb]
    xor bx, bx
.c16:
    IT_C4B 16, 0
    dec bp
    jnz .c16
    add di, [cs:it_pst1]
    pop bp
    cmp bp, [cs:it_pend]
    jb .r16
    jmp .down
    ; --- A WIDE ONE (a whole present): rows of 64 bytes, a repeated logical
    ; row a copy of the row above it
.wide:
    mov word [cs:it_pdst], IT_STRIP ; the strip row's plane 0
    mov byte [cs:it_plast], 0xFF    ; no row converted in this strip yet
.row:
    mov al, [cs:it_rmap + bp]
    cmp al, [cs:it_plast]
    jne .conv
    mov di, [cs:it_pdst]            ; the same logical row as the last: the
    mov si, di                      ; four planes of the row above, at once
    sub si, 256
    mov cx, (3 * 64 + 64) / 2
    push es
    push ds
    pop es
    rep movsw
    pop es
    jmp short .rnext
.conv:
    mov [cs:it_plast], al
    xor ah, ah
    mov si, ax
    add si, [cs:it_psrc]            ; SI = row y of column 4 * j0
    mov di, [cs:it_pdst]
    push bp
    mov bp, [cs:it_pnb]             ; BP = bytes, the loop's count
    xor bx, bx
    add di, 128                     ; DI = plane 2's byte
    call it_c4_64
    pop bp
.rnext:
    add word [cs:it_pdst], 256      ; the next strip row
    inc bp
    cmp bp, [cs:it_pend]
    jb .row
.down:
    pop ds
    ; --- and down -----------------------------------------------------------
    mov es, [it_seg]
    mov si, IT_STRIP                ; ES:SI = plane 0's first row
    mov di, [it_pps]
    or di, 0x4000                   ; walk an armed clip (5.4.3.6)
    mov bp, di
    and bp, 0x00FF
    shl bp, 1
    shl bp, 1                       ; BP = the row stride, 4 * PS
    mov ax, [it_pj0]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [it_ox]
    mov bx, [it_pr0]
    add bx, [it_oy]
    mov cx, [it_pnb]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, [it_pn]
    call OSAPI_GFX_BLITP
    jc .refused
    mov ax, [it_pn]
    add [it_pr0], ax
    jmp .chunk
.refused:
    ; REFUSED AFTER THE PROBE SAID YES (PIXELSTEIN's rule): mono from here,
    ; and the whole picture again in it, by the worker's next present
    mov byte [it_nocol], 1
    mov byte [it_surf], 0
    mov byte [it_full], 1
    stc
    ret

; it_c4_64 - one logical row's BP device bytes into a wide strip (PS = 64).
; BH = 0 on entry. Clobbers AX, BX, CX, DX, SI, DI, BP.
it_c4_64:
.col:
    IT_C4B 64, 0
    dec bp
    jnz .col
    ret

; -----------------------------------------------------------------------------
; it_lut_build - the presenters' tables, in the claim (SPEC.md 108.11.1),
; from it_mono and it_col: the mono pages HI/LO for each row parity, and the
; colour TA/TB word tables. Once at entry (~0.1 s). ES = the claim.
; Clobbers AX..DI, BP.
; -----------------------------------------------------------------------------
it_lut_build:
    xor bp, bp                      ; BP = the canvas byte b
.b:
    ; --- mono: for parity p, nib = pat(b>>4, p, even) << 2 | pat(b&15, p, odd)
    mov bx, bp
    mov cl, 4
    shr bl, cl
    mov al, [it_mono + bx]          ; AL = the even px's id's patterns
    mov bx, bp
    and bl, 15
    mov ah, [it_mono + bx]          ; AH = the odd px's
    ; it_mono: bits 7-6 = row 0 even x, 5-4 = row 0 odd x,
    ;          bits 3-2 = row 1 even x, 1-0 = row 1 odd x
    mov dl, al                      ; parity 0: (AL >> 6) << 2 | (AH >> 4) & 3
    mov cl, 4
    shr dl, cl
    and dl, 0x0C
    mov dh, ah
    shr dh, cl
    and dh, 3
    or dl, dh                       ; DL = parity 0's nibble
    mov di, bp
    mov cl, 4
    mov dh, dl
    shl dh, cl
    mov [es:di + IT_MLUT], dh       ; page 0: HI
    mov [es:di + IT_MLUT + 256], dl ; page 1: LO
    mov dl, al                      ; parity 1: (AL >> 2) & 3 << 2 | AH & 3
    and dl, 0x0C
    mov dh, ah
    and dh, 3
    or dl, dh
    mov dh, dl
    shl dh, cl
    mov [es:di + IT_MLUT + 512], dh ; page 2: HI
    mov [es:di + IT_MLUT + 768], dl ; page 3: LO
    ; --- colour: ce = colour of b>>4 at even x, co = of b&15 at odd x -------
    mov bx, bp
    mov cl, 4
    shr bl, cl
    mov al, [it_col + bx]
    shr al, cl                      ; AL = ce
    mov bx, bp
    and bl, 15
    mov ah, [it_col + bx]
    and ah, 15                      ; AH = co
    mov di, bp
    shl di, 1                       ; the word tables' entry
    xor si, si                      ; SI = the plane p
.p:
    xor dl, dl                      ; nib = (ce bit p ? 1100b) | (co bit p ? 0011b)
    test al, 1
    jz .p1
    or dl, 0x0C
.p1:
    test ah, 1
    jz .p2
    or dl, 0x03
.p2:
    shr al, 1
    shr ah, 1
    mov dh, dl
    mov cl, 4
    shl dh, cl                      ; DH = the high-nibble form
    mov bx, [it_c4off + si]         ; this plane's two table bytes
    mov [es:di + bx], dh            ; TA (b's px as the left half)
    mov [es:di + bx + IT_TB01 - IT_TA01], dl    ; TB (as the right half)
    add si, 2
    cmp si, 8
    jb .p
    inc bp
    cmp bp, 256
    jb .b
    ret

; THE LOOKS (SPEC.md 108.11.1) - ours, data, and to be LOOKED at (39.4): a
; byte an id. tools/isotronsim.py's MONO and COLOUR are the same tables and
; its selfcheck reads these two out of this file and compares them.
;
; it_mono: the 1bpp pattern, two device pixels a logical pixel (left, right),
;   bits 7-6 = device row 0 at an even x, 5-4 = row 0 odd x,
;   bits 3-2 = row 1 even x, 1-0 = row 1 odd x (the row's parity in the
;   picture, so a drag never shears it)
it_mono:
    db 0x00                         ; VOID
    db 0x00                         ; FLOOR
    db 0xA0                         ; FLOORDOT   10 / 00
    db 0xF0                         ; RIM        11 / 00: horizontal stripes
    db 0xFF                         ; EDGE
    db 0xA5                         ; SLABF      10 / 01: checker
    db 0xA0                         ; SLABR      10 / 00
    db 0xFF                         ; PFRONT
    db 0xFA                         ; PSIDE      11 / 10
    db 0xFF                         ; TOP        every wall's top pixel
    db 0x33                         ; CFRONT     00 even x, 11 odd: stripes
    db 0xCC                         ; CSIDE      11 even x, 00 odd
    db 0x00                         ; (12, free)
    db 0xA0                         ; LOGOF      dim, so the edges read
    db 0x00                         ; (14, free)
    db 0xFF                         ; FLASH
; it_col: the 16-colour "isoTron" look, (colour at an even x) << 4 | (at an
;   odd x), in the EGA/VGA default palette - so no DAC is ever written
it_col:
    db 0x00, 0x00, 0x11, 0x55       ; VOID, FLOOR, FLOORDOT blue, RIM magenta
    db 0xFF, 0x87, 0x08, 0xBB       ; EDGE white, SLABF greys, SLABR dark, PFRONT
    db 0x33, 0xFF, 0x5D, 0xD5       ; PSIDE cyan, TOP white, CFRONT, CSIDE
    db 0x00, 0x11, 0x00, 0xCC       ; (free), LOGOF blue, (free), FLASH red

; the colour tables' byte offsets of plane p in an entry: planes 0 and 1 are
; the low and high byte of TA01's word, 2 and 3 of TA23's
it_c4off:
    dw IT_TA01, IT_TA01 + 1, IT_TA23, IT_TA23 + 1
%if (IT_TB01 - IT_TA01) != (IT_TB23 - IT_TA23)
  %error "the TB tables must sit the same distance after their TA tables"
%endif

; -----------------------------------------------------------------------------
; THE OVERLAYS (SPEC.md 108.11.5): the HUD's two lines and one message line,
; text the desktop font draws (font_run) over the picture - never in the
; canvas, which therefore compares exactly. A present is the canvas minus
; their rects, then them: each pixel once.
; -----------------------------------------------------------------------------

; it_ovl_none - no overlay shows (a relayout: the old rects were the old
; picture's). Preserves all.
it_ovl_none:
    mov word [it_ov + OV_L + OV_STR], 0
    mov word [it_ov + OV_M + OV_STR], 0
    mov word [it_ov + OV_R + OV_STR], 0
    ret

; it_ovl_want - out: AX, CX, DX = the strings the state wants on the left
; HUD, the message and the right HUD (0 = none). The HUD's two are rebuilt.
it_ovl_want:
    xor ax, ax
    xor dx, dx
    cmp byte [it_state], ST_TITLE
    je .msg
    call it_hud_str
    mov ax, it_hudl
    mov dx, it_hudr
.msg:
    push ax
    call it_msg_str                 ; SI = the message, 0 = none
    mov cx, si
    pop ax
    ret

; it_ovl_compose - every overlay's record from the state, nothing drawn (a
; whole present draws them after the canvas). Clobbers AX..DX, SI, DI.
it_ovl_compose:
    call it_ovl_want
    mov bx, OV_L
    call it_ovl_rect
    mov ax, cx
    mov bx, OV_M
    call it_ovl_rect
    mov ax, dx
    mov bx, OV_R
    call it_ovl_rect
    ret

; it_ovl_rect - overlay BX's string = AX, and its bytes from the length:
; the left one from byte 1, the right one to byte pwb - 2, the message
; centred. Preserves all but the record.
it_ovl_rect:
    push ax
    push cx
    push si
    mov [it_ov + bx + OV_STR], ax
    or ax, ax
    jz .out
    mov si, ax
    xor cx, cx
.len:
    cmp byte [si], 0
    je .got
    inc si
    inc cx
    jmp short .len
.got:
    jcxz .out
    cmp bx, OV_L
    jne .nl
    mov al, 1
    jmp short .set
.nl:
    mov ax, [it_pwb]
    cmp bx, OV_R
    jne .mid
    sub ax, 1
    sub ax, cx                      ; J0 = pwb - 1 - len, J1 = pwb - 2
    jmp short .set
.mid:
    sub ax, cx
    shr ax, 1                       ; J0 = (pwb - len) / 2
.set:
    mov [it_ov + bx + OV_J0], al
    add al, cl
    dec al
    mov [it_ov + bx + OV_J1], al
.out:
    pop si
    pop cx
    pop ax
    ret

; it_ovl_draw - overlay BX's text, if any, opaque (font_run: the cells'
; ground and glyph in one pass) - in a paint, only if its rect meets the
; damage (a clipped-out font_run still pays its fixed part). Clobbers AX, CX,
; DX, SI.
it_ovl_draw:
    mov si, [it_ov + bx + OV_STR]
    or si, si
    jz .out
    cmp byte [it_dmgon], 0
    je .go
    push bx
    mov al, [it_ov + bx + OV_J1]    ; x2 = ox + 8 * J1 + 7
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [it_ox]
    add ax, 7
    mov cx, ax
    mov dx, [it_ov + bx + OV_R0]    ; y1 .. y1 + 7
    add dx, [it_oy]
    mov al, [it_ov + bx + OV_J0]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [it_ox]                 ; x1
    mov bx, dx
    add dx, 7
    call it_dmg_ck
    pop bx
    jc .out
.go:
    mov al, [it_ov + bx + OV_J0]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [it_ox]
    mov cx, ax
    mov dx, [it_ov + bx + OV_R0]
    add dx, [it_oy]
    mov al, CWHITE
    mov ah, CBLACK
    push bx
    call OSAPI_FONT_RUN
    pop bx
.out:
    ret

it_ovl_draw_all:
    xor bx, bx
.l:
    call it_ovl_draw
    add bx, OV_SIZE
    cmp bx, OV_N * OV_SIZE
    jb .l
    ret

; it_ovl_update - the overlays that changed since the glass last showed them
; (it_hudd: the HUD's; it_msgd: the message's): the OLD rect presented from
; the canvas minus every rect now wanted, then the new text. Clobbers all
; but DS.
it_ovl_update:
    mov al, [it_hudd]
    or al, al
    jz .msg
    mov byte [it_hudd], 0
    mov [it_hmask], al
    call it_ovl_want                ; AX = the left line, DX = the right
    mov [it_hright], dx
    test byte [it_hmask], 1
    jz .r
    mov bx, OV_L
    call it_ovl_swap
.r:
    test byte [it_hmask], 2
    jz .msg
    mov ax, [it_hright]
    mov bx, OV_R
    call it_ovl_swap
.msg:
    cmp byte [it_msgd], 0
    je .out
    mov byte [it_msgd], 0
    call it_ovl_want
    mov ax, cx
    mov bx, OV_M
    call it_ovl_swap
.out:
    ret

; it_ovl_swap - overlay BX becomes string AX: its old rect from the canvas
; (minus the new one, and the others), then the new text.
it_ovl_swap:
    mov si, [it_ov + bx + OV_STR]
    mov cl, [it_ov + bx + OV_J0]
    mov ch, [it_ov + bx + OV_J1]
    call it_ovl_rect                ; the record is the NEW one from here
    or si, si
    jz .new                         ; nothing showed there
    mov dl, cl
    xor dh, dh
    mov [it_pj0], dx
    mov dl, ch
    mov [it_pj1], dx
    mov dx, [it_ov + bx + OV_R0]
    mov [it_pr0], dx
    add dx, 8
    mov [it_pr1], dx
    push bx
    call it_pbox
    pop bx
.new:
    call it_ovl_draw
    ret

; -----------------------------------------------------------------------------
; it_fill_rest - the content outside the picture, black, once after a
; relayout or in a paint (the clip region bounds it): the 0..7 columns left
; of the origin, what lies right of the picture, and the rows below it.
; Clobbers AX..DX.
; -----------------------------------------------------------------------------
it_fill_rest:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [it_cx]                 ; left: cx .. ox - 1
    mov cx, [it_ox]
    cmp cx, ax
    jbe .right
    dec cx
    mov bx, [it_cy]
    mov dx, bx
    add dx, [it_hc]
    dec dx
    call it_dmg_ck
    jc .right
    call OSAPI_GFX_FILL
.right:
    mov ax, [it_pwb]                ; right: ox + pwb * 8 .. cx + cw - 1
    mov cl, 3
    shl ax, cl
    add ax, [it_ox]
    mov cx, [it_cx]
    add cx, [it_cw]
    cmp ax, cx
    jae .rows
    dec cx
    mov bx, [it_cy]
    mov dx, bx
    add dx, [it_hc]
    dec dx
    call it_dmg_ck
    jc .rows
    call OSAPI_GFX_FILL
.rows:
    mov ax, [it_ch]                 ; below: every column
    cmp ax, [it_hc]
    jbe .out
    mov ax, [it_cx]
    mov bx, [it_cy]
    add bx, [it_hc]
    mov cx, [it_cx]
    add cx, [it_cw]
    dec cx
    mov dx, [it_cy]
    add dx, [it_ch]
    dec dx
    call it_dmg_ck
    jc .out
    call OSAPI_GFX_FILL
.out:
    ret

; it_dmg_ck - in a paint (it_dmgon), CF = 1 when the inclusive rect AX, BX,
; CX, DX misses the damage [it_dmg]; CF = 0 otherwise, and always outside
; one. Preserves every register.
it_dmg_ck:
    cmp byte [it_dmgon], 0
    je .hit
    cmp ax, [it_dmg + 4]
    jg .miss
    cmp cx, [it_dmg]
    jl .miss
    cmp bx, [it_dmg + 6]
    jg .miss
    cmp dx, [it_dmg + 2]
    jl .miss
.hit:
    clc
    ret
.miss:
    stc
    ret

; -----------------------------------------------------------------------------
; it_draw_small - the content black and "Window too small." (a box under
; IT_HCMIN rows or 632 px). Clobbers AX..DX, SI.
; -----------------------------------------------------------------------------
it_draw_small:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [it_cx]
    mov bx, [it_cy]
    mov cx, ax
    add cx, [it_cw]
    dec cx
    mov dx, bx
    add dx, [it_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov cx, [it_cx]
    add cx, 8
    and cx, 0xFFF8                  ; (a whole cell on the byte grid)
    mov dx, [it_cy]
    add dx, 4
    mov si, it_s_small
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; it_paint_win - W_PAINT's picture (the UI task, the lock held, the kernel's
; clip armed): the damaged rect only, from the canvas, in the surface the
; worker chose - asked again only if it can have changed (it_surf_wake), and
; a CHANGE is still the worker's whole present unless this paint covers it.
; A paint whose damage is THE WHOLE CONTENT is a whole present - the picture,
; the overlays, the content around it - so it pays off what the worker owes
; (it_full, it_fillr, the overlays' changes, the boxes): at launch and after
; a relayout the picture is drawn once, not by the paint and then again by
; the worker (rule 2). Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_paint_win:
    call it_geom
    jc .out                         ; not visible
    call it_layout_ck
    cmp byte [it_layok], 0
    jne .ok
    call it_draw_small
    ret
.ok:
    call it_surf_wake
    call it_paint_pen
    mov bx, [it_win]
    call OSAPI_WM_DAMAGE            ; AX, BX, CX, DX: inclusive, absolute
    jnc .rect
    mov ax, [it_cx]                 ; CF: the whole content
    mov bx, [it_cy]
    mov cx, ax
    add cx, [it_cw]
    dec cx
    mov dx, bx
    add dx, [it_ch]
    dec dx
.rect:
    mov byte [it_pwhole], 0
    cmp ax, [it_cx]                 ; does it hold the whole content?
    jg .part
    cmp bx, [it_cy]
    jg .part
    push ax
    mov ax, [it_cx]
    add ax, [it_cw]
    dec ax
    cmp cx, ax
    pop ax
    jl .part
    push ax
    mov ax, [it_cy]
    add ax, [it_ch]
    dec ax
    cmp dx, ax
    pop ax
    jl .part
    mov byte [it_pwhole], 1
.part:
    ; THE OVERLAYS' RECORDS are what the glass shows, so a part of it is
    ; painted from them as they are - a change still owed swaps them later.
    ; A whole paint, or one before the first whole present (a relayout: the
    ; records are empty), composes them from the state.
    cmp byte [it_pwhole], 0
    jne .comp
    cmp byte [it_full], 1
    jne .draw
.comp:
    push ax
    push bx
    push cx
    push dx
    call it_ovl_compose
    pop dx
    pop cx
    pop bx
    pop ax
.draw:
    call it_paint_rect
    jc .out                         ; a blitp refused: the worker's, whole
    cmp byte [it_pwhole], 0
    je .out
    mov byte [it_full], 0           ; THIS WAS THE WHOLE PRESENT
    mov byte [it_fillr], 0
    mov byte [it_hudd], 0
    mov byte [it_msgd], 0
    mov byte [it_byield], 0
    call it_box_clear
.out:
    ret

; -----------------------------------------------------------------------------
; it_paint_pen - a mono picture on a colour display draws in CLCYAN on CBLACK,
; the pen being the lock hold's (5.4.2.2.2): a paint's and the About card's
; dismissal's own. Clobbers AX.
; -----------------------------------------------------------------------------
it_paint_pen:
    cmp byte [it_surf], 0
    jne .out
    cmp byte [it_bpp], 1
    jbe .out
    mov al, CLCYAN
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
.out:
    ret

; -----------------------------------------------------------------------------
; it_paint_rect - the rect AX, BX, CX, DX (inclusive, absolute) from the
; canvas in the current surface: the picture's part of it in whole device
; bytes (the clip cuts it), then the overlays and the content around the
; picture that meet it. A clip region armed; it_geom, the layout and the
; surface done. out: CF = 1 a blitp refused (a whole present owed).
; Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_paint_rect:
    mov [it_dmg], ax
    mov [it_dmg + 2], bx
    mov [it_dmg + 4], cx
    mov [it_dmg + 6], dx
    mov byte [it_dmgon], 1
    sub bx, [it_oy]                 ; rows: [y1 - oy, y2 - oy + 1)
    jns .r0
    xor bx, bx
.r0:
    mov [it_pr0], bx
    sub dx, [it_oy]
    inc dx
    mov [it_pr1], dx
    sub ax, [it_ox]                 ; bytes: (x - ox) >> 3, from 0
    jns .j0
    xor ax, ax
.j0:
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov [it_pj0], ax
    sub cx, [it_ox]
    sar cx, 1
    sar cx, 1
    sar cx, 1                       ; (left of the picture: -1, nothing)
    mov [it_pj1], cx
    call it_pbox
    pushf
    call it_ovl_draw_all            ; (each only if it meets the rect)
    call it_fill_rest               ; (likewise)
    mov byte [it_dmgon], 0
    popf
    ret
