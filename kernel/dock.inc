; =============================================================================
; os8088 - dock.inc
;
; The dock strip (SPEC.md 30): a taskbar along the bottom of the screen with
; one tile per RUNNING instance (I_STATE = 1, SPEC.md 29), built-in or
; package. Tile position i belongs to instance-table record i - a stable
; mapping with holes, so quitting one instance never moves another's tile.
;
; A tile carries TWO independent states, and they are drawn as two different
; kinds of mark on purpose. Minimized (I_FLAGS bit0) inverts the tile's
; interior - the app is not on screen at all. ACTIVE - the instance that owns
; the frontmost visible window - doubles the tile's border instead. They are
; mutually exclusive in practice (a minimized window is not visible, so it
; cannot be frontmost) but nothing depends on that, and a heavier border
; survives the 1bpp reduction (SPEC.md 39.4) where a second colour would not.
; Clicking a tile does whatever its own mark says is not true yet (SPEC.md
; 30.4): a minimized one restores, an ACTIVE one minimizes, any other comes
; to the front.
;
; dock_paint runs inside wm_paint_all right after desk_paint, under the
; caller's gfx lock - windows paint over the dock exactly like they cover
; the desktop icons (SPEC.md 26/30). dock_click is called by the UI task
; with no lock held, only after wm_hit declined the click, and takes the
; lock itself around wm calls.
;
; **It is called on every window operation and usually has nothing to do.**
; Raising a window, showing one, dragging one - each of those repaints the
; chrome because the chrome MIGHT have changed, and most of the time the only
; thing that did is which tile wears the active mark. So each tile carries a
; key (dock_key: its icon in the high bits, live/minimized/active in bits
; 0..2) and a tile whose key still matches what was drawn is left alone. A
; focus change costs two tiles; nothing changing costs no pixels at all, and
; dock_paint says so in CF, which is what lets wm_dock_under skip putting the
; windows back.
;
; **And a tile whose MARK moved is not redrawn either** (SPEC.md 30.3). A
; focus change is the commonest thing that happens here and it used to erase
; two tiles to white and rebuild them - fill, frame, masked icon blit, marks -
; to move one 1px rectangle, which on a 4.77MHz machine is the double-draw
; flash of PERFORMANCE.md Part 1 in the strip the user is looking at while
; they switch applications. Both marks are their own inverse, so old XOR new
; says what moved and the answer is one gfx_xor_rect or one gfx_xor_fill. The
; ICON IS NEVER TOUCHED unless the icon changed.
;
; The strip's own rule and white field are only redrawn when the damage span
; says so, and the one thing that fills it is somebody having drawn OVER the
; strip:
; wm_paint_all's dither, and wm_paint_dmg's when the damage reaches it - which
; is how "a window that was covering the dock moved away" gets its redraw.
; That restore is bounded to the DAMAGED SPAN (dock_force_x), and so is the
; clearing of the keys: a window closing in one corner used to repaint 640px
; of rule and field - nearly all of it white over white, where nothing had
; changed and nothing could have - and rebuild every tile on the strip.
; =============================================================================

; --- geometry (SPEC.md 30) ----------------------------------------------------
DOCK_H      equ 24              ; strip height: 1px black rule + 23 white
                                ; rows, pinned to the BOTTOM of whatever
                                ; screen the adapter gave us. The top row and
                                ; the tile top row therefore move with it and
                                ; live in [vid_dock_y0] / [vid_dock_ty0]
                                ; (SPEC.md 39.2) - 456/459 on VGA, 324/327 on
                                ; Hercules, 176/179 on CGA.
DOCK_TILE_W equ 24
DOCK_TILE_H equ 20
DOCK_X0     equ 8               ; first tile's left edge
DOCK_STEP   equ 28              ; tile + 4px gap; 8 + 12*28 = 344 < 640

; --- ...and on a SIDE of the screen (SPEC.md 30.5) ----------------------------
; The bottom numbers above were the whole geometry until the strip could stand
; on an edge; dock_geom turns [dock_cfg] into words, and it is the only reader
; of any of these.
DOCK_SW     equ 32              ; a side strip's width: a 24px tile, a 4px
                                ; margin either side, the rule and a column
DOCK_SX     equ 4               ; tile left, in from the strip's own left
DOCK_SY0    equ 4               ; first tile's top, down from MBAR_H
DOCK_SSTEP  equ 24              ; tile + 4px gap, top to bottom

DOCK_P_BOTTOM equ 0             ; [dock_cfg] bits 0..1: the edge
DOCK_P_LEFT   equ 1
DOCK_P_RIGHT  equ 2
DOCK_P_MASK   equ 3
DOCK_F_AUTO   equ 4             ; ...and bit 2: the strip hides (SPEC.md 30.6)

DOCK_HOVER_T  equ 5             ; ticks on the line before it opens: 0.27 s
DOCK_LEAVE_T  equ 14            ; ticks off the strip before it closes: 0.77 s

; --- the tile key's low three bits (SPEC.md 30.3) ----------------------------
; The MARKS live here and nothing else does, so old XOR new says exactly what
; moved: a key whose difference is confined to these bits is a tile whose
; ICON did not change, and the marks are both their own inverse.
DOCK_K_LIVE equ 1               ; ...so a live tile's key is never 0
DOCK_K_MINI equ 2               ; the interior inverts
DOCK_K_ACT  equ 4               ; the border doubles
DOCK_K_MARKS equ DOCK_K_MINI | DOCK_K_ACT
DOCK_K_ID   equ 0FFFFh & ~DOCK_K_MARKS  ; everything that is NOT a mark

DOCK_CTX_W  equ 56              ; ...and its WIDTH, for a right-hand strip:
                                ; menu_widest('Close') + 16. The same cheap
                                ; copy as DOCK_CTX_H below and for its reason
DOCK_CTX_N  equ 1               ; items in dock_ctx_items (SPEC.md 30.2)
DOCK_CTX_H  equ DOCK_CTX_N * MENU_ITEM_H + 2
                                ; ...and the height menu_popup will give them:
                                ; count*16 plus the two frame rows. A copy of
                                ; that arithmetic, and a deliberately cheap
                                ; one - it decides only where the menu is
                                ; ANCHORED, and menu_popup shifts and floors
                                ; the result anyway (SPEC.md 12.4), so drift
                                ; costs a few pixels of placement and can
                                ; never put a menu off the screen. Anchoring
                                ; at the press point instead would drop the
                                ; menu ON the strip, over the very tile it is
                                ; asking about

; THE SETTING (SPEC.md 30.5): the edge in bits 0..1, Auto-hide in bit 2.
; Initialised .text data and not .bss: SYSTEM.CFG's 'DK' record is stored
; straight into it by drv_cfg_unpack, and its default has to be right on a
; machine with no settings file at all - and across a warm boot, which keeps
; the old kernel's .bss. dock_geom is its one reader.
dock_cfg:   db DOCK_P_BOTTOM

; -----------------------------------------------------------------------------
; dock_init - reset module scratch
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
; IN THE BOOT OVERLAY (SPEC.md 2.5.3 - the WINDOW half, which is what OVWCALL
; reaches; 2.5.2 is `.ovl`), docs/plans/LAST-DROP-BYTES.md row 15. One caller,
; kmain:4525.
;
; It has NO outbound call at all now, which is what it costs `.text`: it used
; to ask cw_dock_force for "the whole strip is owed", and dock_force's answer
; is two words this can simply write - so the 4-byte `.text` shim goes with it
; and kmain's comment about "one outbound call left" becomes true of menu_init
; alone.
;
; **The dock_ck clearing loop is gone too, and it is not an assumption.**
; `.bss` arrives ZEROED (SPEC.md 2.6: it is `nobits`, so what lands on it is
; the image's own inter-section padding, which `-f bin` zero-fills;
; tests/unit/t_bsssentinel.py states the rule) - and independently of that,
; the damage span written here is 0..[vid_pwm1], so the first dock_paint that
; is not a fullscreen early-out enters `.full` and `.zap` clears EVERY key
; before `.scan` reads one: tile i spans 8+28i .. 8+28i+23, so the lefts are
; 8..316 and the rights 31..339, and every one of the twelve passes both
; bounds. [vid_pwm1] is live here - vid_init is kmain MARK 7 and this is
; between MARK 21 and 22.
section .ovlw
dock_init:
    push ax
    mov ax, [dock_la1]          ; THE WHOLE STRIP is owed - dock_force's own
    mov [dock_dx1], ax          ; answer, written here rather than called for.
    mov ax, [dock_la2]          ; dock_geom ran inside vid_apply (MARK 7), so
    mov [dock_dx2], ax          ; the along range is live
    mov ax, 07FFFh              ; ...and nothing has been PAINTED yet (SPEC.md
    mov [dock_px1], ax          ; 30.3.3) - the empty bounding box, which is
    inc ax                      ; 07FFFh..08000h and refuses every rect
    mov [dock_px2], ax
    pop ax
    retf                        ; FAR: kmain reaches this through OVLGATE1
section .text

; -----------------------------------------------------------------------------
; dock_force   - the WHOLE strip was painted over
; dock_force_x - ...only x AX..CX of it was (inclusive)
; in:  nothing / AX = x1, CX = x2
; out: nothing (all registers preserved)
;
; Called by whoever is about to draw, or has just drawn, over the strip. The
; per-tile keys go with it: a tile whose pixels were painted over is not
; "unchanged" however much its state still matches - but only the tiles the
; damage actually REACHED, and only that span of rule and white field.
;
; The span exists because wm_paint_dmg (SPEC.md 11.91) forced the whole strip
; for any damage that touched it: a window closing at x 300 repainted 640px of
; rule and field and rebuilt every tile, nearly all of it white over white
; with a tile flashing wherever it was not. wm_paint_all still asks for the
; whole width and means it - its dither runs from MBAR_H to the last row, so
; the strip really is gone.
;
; Repeated calls UNION, because several things can damage the strip between
; two dock_paints - and THE SPAN IS ITS OWN FLAG. An EMPTY span is
; 07FFFh..08000h, the empty bounding box dock_px_add already uses on the other
; pair (SPEC.md 30.3.3, wm_dmg_bands before it): the compares below are SIGNED,
; so a union onto the sentinel gives back exactly the caller's span and an
; untouched pair refuses every paint. That is what deleted [dock_full] - a
; boolean saying "the span is non-empty" beside a span that can say so itself.
; -----------------------------------------------------------------------------
dock_force:
    push ax
    push cx
    mov ax, [dock_la1]          ; THE WHOLE STRIP: its along range, whichever
    mov cx, [dock_la2]          ; edge it stands on (SPEC.md 30.5)
    jmp dock_force_r.union

; dock_force_r - ...only the part of it inside rect AX,BX,CX,DX was
; in:  AX = x1, BX = y1, CX = x2, DX = y2, inclusive, VIRTUAL
; out: nothing (all registers preserved)
;
; SPEC.md 30.5. This was dock_force_x, which took an x span and trusted its one
; caller - wm_paint_dmg - to have asked `y2 >= [vid_dock_y0]` first. A strip on
; a SIDE is an x question and not a y one, so the caller now hands over the
; whole damage and the overlap test is here, against the LIVE rect: the 1px
; line while the strip is hidden, the strip otherwise. The along span is then
; clamped to the strip's own extent, which is also what keeps a damage rect on
; a second monitor off the primary's strip (SPEC.md 39.19.5) - the clamp
; dock_force_x carried under KERN_BIG for that reason is the intersection now.
dock_force_r:
    push ax
    push cx
    cmp byte [dock_up], 0       ; an OPEN strip is ON TOP (SPEC.md 30.6.1):
    jne .out                    ; nothing under it drew over it, being clipped
                                ; round it, so the damage owes it nothing
    call dock_live_hit          ; does the damage reach the strip at all?
    jnc .out
%ifdef REDRAWFULL
    mov ax, [dock_la1]          ; REFERENCE BUILD: every damage is the whole
    mov cx, [dock_la2]          ; strip, the behaviour this replaced
%else
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .clamp                   ; bottom: the along axis IS x
    mov ax, bx                  ; a side: it is y
    mov cx, dx
.clamp:
    cmp ax, [dock_la1]
    jge .c1
    mov ax, [dock_la1]
.c1:
    cmp cx, [dock_la2]
    jle .union
    mov cx, [dock_la2]
%endif
.union:                         ; dock_force enters HERE with its own pushes
    cmp ax, [dock_dx1]          ; SIGNED, because empty is 07FFFh..08000h -
    jge .u2                     ; dock_px_add's own compares, on the other span
    mov [dock_dx1], ax
.u2:
    cmp cx, [dock_dx2]
    jle .out
    mov [dock_dx2], cx
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_live_hit - does rect AX,BX,CX,DX overlap the LIVE strip? (SPEC.md 30.6)
; out: CF = 1 yes; every register preserved
; -----------------------------------------------------------------------------
dock_live_hit:
    cmp ax, [dock_lx2]
    jg .no
    cmp cx, [dock_lx1]
    jl .no
    cmp bx, [dock_ly2]
    jg .no
    cmp dx, [dock_ly1]
    jl .no
    stc
    ret
.no:
    clc
    ret

; dock_live_rect - AX,BX,CX,DX = the live strip's rect. Clobbers the four.
dock_live_rect:
    mov ax, [dock_lx1]
    mov bx, [dock_ly1]
    mov cx, [dock_lx2]
    mov dx, [dock_ly2]
    ret

; dock_px_rect - AX,BX,CX,DX = the part of the live strip the last dock_paint
; PAINTED (SPEC.md 30.3.3), as a rect. Clobbers the four.
dock_px_rect:
    mov ax, [dock_px1]
    mov cx, [dock_px2]
    mov bx, [dock_lc1]
    mov dx, [dock_lc2]
    jmp short dock_swap_side

; dock_span_fill - fill ALONG AX..CX, ACROSS BX..DX in the current pen
; in:  AX = a1, CX = a2, BX = c1, DX = c2; gfx lock held
; out: nothing (all registers preserved)
dock_span_fill:
    push ax
    push bx
    push cx
    push dx
    call dock_swap_side
    call gfx_fill
    jmp kret_dx

; dock_swap_side - along/across <-> x/y: nothing on the bottom strip, where the
; along axis IS x, and the two exchanges on a side. Flags only otherwise.
dock_swap_side:
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .out
    xchg ax, bx
    xchg cx, dx
.out:
    ret

; -----------------------------------------------------------------------------
; dock_geom - [dock_cfg] and the primary's extent -> every word of geometry
; in:  [dock_cfg], [vid_pw]/[vid_ph] (the chrome's extent, SPEC.md 39.16)
; out: the strip rect, the tile layout, the band ([vid_dock_y0],
;      [vid_band_x0], [vid_band_xe]), [vid_desk_zx] and the live set.
;      All registers preserved.
;
; SPEC.md 30.5's table, and its ONLY writer. vid_apply calls it once the
; chrome's extent is published, so an adapter switch re-derives the lot, and
; dock_apply calls it when the setting itself moves. It closes the strip -
; [dock_up] = 0 - because a geometry that moved under an open one would make
; its save-under describe a rect that is not there.
; -----------------------------------------------------------------------------
dock_geom:
    push ax
    push bx
    push cx
    push dx
    call dock_drop              ; an OPEN strip's save-under and hole describe
                                ; the geometry this is about to replace
    mov al, [dock_cfg]
    mov ah, al
    and al, DOCK_P_MASK
    cmp al, DOCK_P_RIGHT
    jbe .have
    xor al, al                  ; 3: a byte from a future version
.have:
    mov [dock_side], al
    and ah, DOCK_F_AUTO
    mov [dock_auto], ah
    mov byte [dock_cap], INST_MAX   ; every tile fits a bottom strip (SPEC.md
                                ; 30.5.1): 8 + 12*28 = 344 of 640
    mov bx, [vid_pw]
    mov dx, [vid_ph]
    mov cx, [vid_phm1]          ; every strip ends on the primary's last row
    mov [dock_sy2], cx
    mov [dock_la2], cx          ; ...which is a side strip's along end too
    or al, al
    jnz .side

    ; --- bottom: the geometry this module always had --------------------------
    xor ax, ax
    mov [dock_sx1], ax
    mov [dock_la1], ax
    mov [vid_band_x0], ax
    mov [vid_band_xe], bx
    mov [dock_ffc2], cx         ; the field runs to the last row...
    mov [dock_ffar], cx         ; ...which is where a hidden strip's line is
    mov ax, [vid_pwm1]
    mov [dock_sx2], ax
    mov [dock_la2], ax
    mov ax, dx
    sub ax, DOCK_H
    mov [dock_sy1], ax
    mov [dock_frc], ax          ; the rule is the strip's first row
    inc ax
    mov [dock_ffc1], ax
    inc ax
    inc ax
    mov [dock_c0], ax           ; tile top = rule + 3
    mov word [dock_a0], DOCK_X0
    mov word [dock_astep], DOCK_STEP
    mov word [dock_alen], DOCK_TILE_W
    mov ax, DOCK_H
    call .thick
    sub dx, ax
    mov [vid_dock_y0], dx       ; the band stops above what the strip keeps
    jmp .done

.side:
    mov [vid_dock_y0], dx       ; no ROW is the strip's: the band is the height
    mov ax, MBAR_H
    mov [dock_sy1], ax
    mov [dock_la1], ax
    add ax, DOCK_SY0
    mov [dock_a0], ax
    mov word [dock_astep], DOCK_SSTEP
    mov word [dock_alen], DOCK_TILE_H
    mov ax, dx                  ; --- HOW MANY TILES THE SIDE HOLDS (SPEC.md
    sub ax, MBAR_H + 2*DOCK_SY0 + DOCK_TILE_H   ; 30.5.1): every one on VGA,
    jb .one                     ; eleven on Hercules and EGA, seven on CGA -
    mov bl, DOCK_SSTEP          ; and the tiles then pack rather than keeping
    div bl                      ; their slots (dock_map). AX < 480 and the
    inc al                      ; step is 24, so the quotient is a byte
    cmp al, INST_MAX
    jbe .cap
    mov al, INST_MAX
    jmp short .cap
.one:
    mov al, 1
.cap:
    mov [dock_cap], al
    mov bx, [vid_pw]            ; (the divide spent BX)
    mov ax, DOCK_SW
    call .thick                 ; AX = the reserved thickness
    cmp byte [dock_side], DOCK_P_LEFT
    jne .right
    xor cx, cx                  ; --- left
    mov [dock_sx1], cx
    mov [dock_ffc1], cx         ; the field starts at the screen edge...
    mov [dock_ffar], cx         ; ...and so does a hidden strip's line
    mov word [dock_c0], DOCK_SX
    mov word [dock_sx2], DOCK_SW - 1
    mov word [dock_frc], DOCK_SW - 1    ; the rule faces the desktop
    mov word [dock_ffc2], DOCK_SW - 2
    mov [vid_band_x0], ax
    mov [vid_band_xe], bx
    jmp short .done
.right:
    mov cx, bx                  ; --- right
    sub cx, DOCK_SW
    mov [dock_sx1], cx
    mov [dock_frc], cx          ; the rule faces the desktop
    inc cx
    mov [dock_ffc1], cx
    add cx, DOCK_SX - 1
    mov [dock_c0], cx
    mov cx, [vid_pwm1]
    mov [dock_sx2], cx
    mov [dock_ffc2], cx
    mov [dock_ffar], cx
    mov word [vid_band_x0], 0
    sub bx, ax
    mov [vid_band_xe], bx

.done:
    mov [dock_thk], ax          ; (wm_dock_snap's half-a-strip test)
    mov ax, [vid_band_xe]       ; --- the drive column hangs off the BAND's
    sub ax, 56                  ; right edge, so a right-hand strip moves it
    mov [vid_desk_zx], ax       ; in (SPEC.md 26.4) - this was vid_apply's
    call dock_live_set
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.thick:                         ; AX = the strip's width -> AX = what it
    cmp byte [dock_auto], 0     ; RESERVES: all of it, or the line
    je .tout
    mov ax, 1
.tout:
    ret

; -----------------------------------------------------------------------------
; dock_live_set - the LIVE rect, rule, field and cross, from [dock_up]
; in:  the strip's geometry (dock_geom), [dock_auto], [dock_up]
; out: [dock_hidden] and the live set; all registers preserved
;
; SPEC.md 30.6. Hidden is "auto and not open", and while it is the live strip
; is the outer edge's one line: its rect, a rule that IS the line, and no field
; (fc1 > fc2). Otherwise the live strip is the strip. Every painter and every
; overlap test reads the live set and none of them asks which mode it is in.
; -----------------------------------------------------------------------------
dock_live_set:
    push ax
    push cx
    mov ax, [dock_sx1]
    mov [dock_lx1], ax
    mov ax, [dock_sy1]
    mov [dock_ly1], ax
    mov ax, [dock_sx2]
    mov [dock_lx2], ax
    mov ax, [dock_sy2]
    mov [dock_ly2], ax
    mov ax, [dock_frc]
    mov [dock_rc], ax
    mov ax, [dock_ffc1]
    mov [dock_fc1], ax
    mov ax, [dock_ffc2]
    mov [dock_fc2], ax
    mov byte [dock_hidden], 0
    cmp byte [dock_auto], 0
    je .cross
    cmp byte [dock_up], 0
    jne .cross
    inc byte [dock_hidden]
    mov ax, [dock_ffar]         ; the line: the outer edge
    mov [dock_rc], ax
    mov word [dock_fc1], 1      ; ...and no field: an empty cross
    mov word [dock_fc2], 0
    cmp byte [dock_side], DOCK_P_BOTTOM
    jne .sline
    mov [dock_ly1], ax
    mov [dock_ly2], ax
    jmp short .cross
.sline:
    mov [dock_lx1], ax
    mov [dock_lx2], ax
.cross:
    mov ax, [dock_ly1]          ; the live strip's extent ACROSS its long axis
    mov cx, [dock_ly2]
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .c
    mov ax, [dock_lx1]
    mov cx, [dock_lx2]
.c:
    mov [dock_lc1], ax
    mov [dock_lc2], cx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_apply - the SETTING moved: geometry, drive column, windows, strip
; in:  [dock_cfg] already written; gfx lock held by the caller, or at boot
; out: nothing (all registers preserved). THE CALLER OWES THE REPAINT, which
;      is vid_switch's own contract: the Control Panel posts [cp_dirty]
;
; SPEC.md 30.5. wm_refit moves every window back inside the new band, and the
; raise caches go because a window that did not move can have had the strip
; stand up or lie down behind it.
; -----------------------------------------------------------------------------
dock_apply:
    call dock_geom
    call desk_rowcalc
    call wm_refit
    call wm_su_drop_all
    call dock_force
    ret

; -----------------------------------------------------------------------------
; dock_paint - bring the strip up to date: the rule and white field when the
;              damage span says they are owed, then every tile whose key moved
; in:  nothing (caller holds the gfx lock)
; out: CF = 1 if any pixel was drawn, CF = 0 if the strip was already right.
;      All registers preserved; [gfx_color] restored.
; -----------------------------------------------------------------------------
dock_paint:
    call wm_fs_vis              ; a visible fullscreen window is OVER the strip
    jnc .go                     ; (SPEC.md 11.2). Same reasoning as
    call dock_force             ; menu_draw_bar's guard: whether the dock is on
    clc                         ; screen is a fact about the screen - and while
    ret                         ; one is up our pixels are being destroyed, so
                                ; the whole strip is owed when it goes. That is
                                ; how wm_paint_all stopped needing dock_force:
                                ; its dither no longer reaches us (SPEC 30.3.2)
                                ; and this is the only other thing that can.
                                ; CF = 0, "nothing was drawn", which is what
                                ; wm_dock_under needs to hear
.go:
    push ax
    push bx
    push cx
    push dx
    push si                     ; SI IS ALREADY PRESERVED (nothing between
    push di                     ; here and the exit touches it) - it is pushed
                                ; to make ax..di the shared epilogue ladder's
                                ; own run, which is what kret_di wants

    push word [gfx_hole]        ; THE OPEN STRIP DRAWS ITSELF THROUGH ITS OWN
    push word [wm_clip_n]       ; HOLE (SPEC.md 30.6.1): the hole and any
    cmp byte [dock_up], 0       ; region armed round it are put away for the
    je .nohole                  ; paint and back at the one exit. Zeroed ONLY
    xor ax, ax                  ; while it is open - a region armed under a
    mov [gfx_hole], ax          ; strip that is under windows is one this
    mov [wm_clip_n], ax         ; paint must keep honouring
.nohole:
    mov al, [gfx_color]         ; byte-only read: gfx_lock_flag lives next
    push ax                     ; door. ON THE STACK, not in .bss, and that
                                ; rests on dock_paint having EXACTLY ONE EXIT
                                ; below `.go` - the five pops at the bottom.
                                ; The wm_fs_vis refusal returns ABOVE this, and
                                ; the CF answer is after the pops. A second
                                ; early return added below here leaks a word
                                ; onto the UI task's stack on every dock_paint,
                                ; which is a slow death nothing in the tree
                                ; would report. See the matching `pop ax`.
    mov byte [dock_drew], 0
    mov word [dock_px1], 07FFFh ; ...and nothing of the strip is painted YET
    mov word [dock_px2], 08000h ; (SPEC.md 30.3.3): [dock_px1]/[dock_px2] are
                                ; this pass's answer, not the last one's, and
                                ; EMPTY is the inverted bounding box 07FFFh..
                                ; 08000h so that dock_px_hit's signed compares
                                ; refuse every rect rather than accepting all
                                ; of them - the same idiom wm_dmg_bands uses.
                                ; Inlined: dock_px_add and dock_px_tile grow
                                ; this pair and had one caller each, but this
                                ; had ONE caller in the whole tree

    call wm_top                 ; who owns the frontmost visible window? Its
    xor di, di                  ; tile gets the active mark (0 = nobody), and
    or bx, bx                   ; every tile's key depends on it, so it is
    jz .noact                   ; resolved before the first one is tested.
    call inst_win_owner         ; DI = its instance record, or 0 - and the
.noact:                         ; `or bx,bx` guard must stay, because
    mov [dock_act], di          ; inst_of_win compares I_WIN against BX and
                                ; BX = 0 would match a live instance with no
                                ; window. ONE store, both ways round

.full:                          ; somebody drew over us: the rule and field
                                ; they covered, and the tiles they reached.
                                ; THE SPAN IS SPENT HERE, at the top, where
                                ; [dock_full]'s clear used to be - a dock_force
                                ; arriving mid-paint then unions onto the fresh
                                ; sentinel and re-arms the NEXT pass, where a
                                ; reset at the BOTTOM would union into the live
                                ; span and be wiped by it: a lost repaint.
                                ;
                                ; The exchange is BRACKETED because two words
                                ; are two instructions: with only one of them
                                ; reset, a force unioning in has its other end
                                ; overwritten by our second store and the pair
                                ; comes out empty either way round - the same
                                ; lost repaint through a window one instruction
                                ; wide instead of a whole paint. Three bytes
                                ; make it a construction rather than a smaller
                                ; argument; the gfx lock is believed to close
                                ; the window as well, and this does not depend
                                ; on believing it. `pushf`/`popf`, never
                                ; `cli`/`sti` - the caller's IF is its own.
    mov ax, 07FFFh
    mov cx, 08000h
    pushf
    cli
    xchg ax, [dock_dx1]
    xchg cx, [dock_dx2]
    popf
    cmp ax, cx                  ; empty (x1 > x2) is "nothing drew over us"
    jg .scan
    inc byte [dock_drew]        ; ...and it cannot wrap: this byte is zeroed at
                                ; the top of every pass and reaches at most
                                ; 1 + INST_MAX = 13, while the test below is
                                ; `cmp x,1 / cmc`, which is exact for all 256
    call dock_px_add            ; ...over exactly this span, and it is the
                                ; span a window has to overlap to be damaged
                                ; by it (SPEC.md 30.3.3)

    ; FOUR REGISTER CONTRACTS CARRY THE NEXT TWELVE LINES, and they are
    ; named here because the day one of them is loosened this reads as a
    ; drawing bug rather than as what it is. AX and CX still hold
    ; [dock_dx1]/[dock_dx2] from the call above; DX comes back from gfx_hline
    ; still holding [vid_dock_y0]. The four:
    ;   dock_px_add   "every register preserved"          (its header above)
    ;   thm_ink/thm_bg "clobbers: nothing (flags preserved)" (vga12.inc)
    ;   gfx_hline     "clobbers: flags"                   (vga12.inc)
    ;   gfx_fill      "clobbers: flags"                   (vga12.inc)
    ; Four memory reads off this path, and it is the [dock_full] path - the
    ; one somebody drawing over the strip takes.
    call thm_ink                ; the rule, over the damaged span only - the
    mov bx, [dock_rc]           ; LINE, while the strip is hidden (SPEC.md
    mov dx, bx                  ; 30.6), and it is one fill on any edge
    call dock_span_fill

    mov bx, [dock_fc1]          ; ...and the field beside it, which a hidden
    mov dx, [dock_fc2]          ; strip does not have (fc1 > fc2)
    cmp bx, dx
    jg .nofield
    call thm_bg
    call dock_span_fill
.nofield:

    mov dx, ax                  ; ...and the tiles it actually covered: a
    mov si, cx                  ; tile outside the damage still has its
                                ; pixels, so its key is still the truth.
                                ; DX/SI = the SPAN, in registers, because the
                                ; xchg above has already put the sentinel back
                                ; in [dock_dx1]/[dock_dx2] - AX and CX have
                                ; carried it since `.full` (dock_px_add is
                                ; "every register preserved", gfx_hline and
                                ; gfx_fill are "clobbers: flags"). Net zero
                                ; bytes and two register compares an iteration
                                ; instead of two memory ones: the two `mov`s
                                ; here cost exactly the four the body saves
    mov ax, [dock_a0]           ; ALONG the strip, whichever edge it is on
    mov di, dock_ck
    mov cx, INST_MAX
.zap:
    mov bx, ax
    add bx, [dock_alen]
    dec bx
    cmp bx, dx
    jb .znext                   ; wholly left of the damage
    cmp ax, si
    ja .scan                    ; wholly right of it - AND SO IS EVERY TILE
                                ; AFTER IT, because AX walks left to right by
                                ; DOCK_STEP. Free: the same 2-byte `ja`, and
                                ; it skips up to eleven passes of this body on
                                ; the commonest damage shape (SPEC.md 30.3.1's
                                ; own example, a window closing in one corner)
    mov word [di], 0
.znext:
    add ax, [dock_astep]
    inc di
    inc di
    loop .zap

.scan:
    cmp byte [dock_hidden], 0   ; a hidden strip has the line and no tiles
    jne .tdone                  ; (SPEC.md 30.6): the keys keep what they were,
                                ; and the reveal forces the whole strip
    call dock_map               ; which record each POSITION shows (30.5.1)
    mov si, dock_pos            ; SI walks the positions
    mov bx, dock_ck             ; ...and ITS KEY BESIDE IT: BX walks the side
                                ; table rather than indexing it, which spends
                                ; no shift and no 16-bit displacement per
                                ; iteration. BX costs no new push (dock_paint
                                ; already banks it) and every callee in this
                                ; loop preserves it - dock_key touches only AX,
                                ; and dock_px_tile, dock_xor_ring,
                                ; dock_xor_body, dock_erase_tile and
                                ; dock_draw_tile all `push bx` and leave
                                ; through kret_dx / kret_si
    xor cx, cx                  ; CX = the POSITION
.tile:
    xor di, di                  ; DI = the record it shows, 0 = none
    mov al, [si]
    cmp al, 0FFh
    je .key
    call inst_ptr
.key:
    call dock_key               ; AX = what this tile should look like, or 0
    mov dx, [bx]                ; for no tile at all (SPEC.md 29.2 rule 3)
    cmp ax, dx
    je .next                    ; already drawn that way
    mov [bx], ax
    inc byte [dock_drew]        ; ...and cannot wrap - see `.full`
    call dock_px_tile           ; ...and this tile's columns join the painted
                                ; span, wherever it stands - a tile outside the
                                ; forced span still puts pixels down, so the
                                ; span is the UNION of the two (SPEC.md 30.3.3)

    xor ax, dx                  ; AX = what MOVED (SPEC.md 30.3), DX = the key
                                ; it WAS. The diff lives in the ACCUMULATOR
                                ; because `test ax,imm16` is 3 bytes where
                                ; `test dx,imm16` is 4, and `test al,imm8` 2
                                ; where `test dl,imm8` is 3 - three sites
%ifdef REDRAWFULL
    jmp short .rebuild          ; REFERENCE BUILD: erase and rebuild any tile
%endif                          ; that changed at all, the behaviour this
                                ; replaced
    test ax, DOCK_K_ID
    jnz .rebuild                ; the icon changed, or the tile came or went

    test al, DOCK_K_ACT         ; --- only the marks moved: the ICON IS NOT
    jz .mini                    ; TOUCHED, and neither is the frame or the
    call dock_xor_ring          ; white around them. Both marks are their own
.mini:                          ; inverse, so putting one on and taking it off
    test al, DOCK_K_MINI        ; are the same operation - which is the whole
    jz .next                    ; reason the erase existed
    call dock_xor_body
    jmp short .next

.rebuild:
    call dock_erase_tile        ; a new icon is not a superset of the old one,
                                ; so this one does start from white. It
                                ; preserves AX itself (push ax .. kret_dx), so
                                ; the bracket that used to be here was dead
    cmp ax, dx                  ; new == diff XOR old, so "new == 0" is
    je .next                    ; "diff == old": the tile went away, and
                                ; erased is all it needed
    call dock_draw_tile         ; in: CX = index, DI = record
.next:
    inc si
    inc bx
    inc bx
    inc cx
    cmp cx, INST_MAX
    jb .tile

.tdone:
    pop ax                      ; the pen, saved at `.go` - see there for the
    mov [gfx_color], al         ; one-exit invariant this rests on. AH is dead
                                ; (AX itself is popped by the rung below).
    pop word [wm_clip_n]        ; ...and the hole and its region, back
    pop word [gfx_hole]
    cmp byte [dock_up], 0       ; AN OPEN STRIP DAMAGES NO WINDOW: it is on
    je .answer                  ; top, so "something drew" must not send
    mov byte [dock_drew], 0     ; wm_dock_under off to repaint what is under it
.answer:
    cmp byte [dock_drew], 1     ; THE CF ANSWER BEFORE THE POPS - `pop` spends
    cmc                         ; no flag, which twenty other sites in this
                                ; kernel already depend on (font.inc:629
                                ; writes the property down, and disk.inc:5725
                                ; puts a FAR CALL between the `clc` and its
                                ; rung). `cmp x,1` borrows exactly when x == 0,
                                ; so after the `cmc` CF is "something drew" -
                                ; and that is EXACT FOR ALL 256 VALUES, not
                                ; only for the 0/1 this byte happens to hold
    jmp kret_di                 ; pops di, si, dx, cx, bx, ax: `.go`'s push run

; -----------------------------------------------------------------------------
; dock_px_add  - grow the painted span to cover x AX..CX as well (inclusive)
; dock_px_tile - ...and to cover tile CX's own columns
; out: nothing; every register preserved (dock_px_add spends flags, which no
;      caller of it needs)
;
; SPEC.md 30.3.3. [dock_px1]/[dock_px2] is what dock_paint PUT PIXELS ON in
; the pass that has just run, which is not [dock_dx1]/[dock_dx2]: that pair is
; what somebody drew OVER US and is only half the answer, because a tile whose
; key moved is repainted wherever it stands, damage or no damage.
;
; **Empty is 0x7FFF..0x8000**, so an untouched span refuses every rect through
; dock_px_hit's signed compares rather than accepting all of them - the same
; empty-bounding-box idiom wm_dmg_bands uses, and now the damage span's too
; (see dock_force_x). There is no dock_px_none: emptying the span is two
; immediate stores and it had exactly ONE caller in the tree, so it is written
; out at the top of dock_paint where the reason for it is.
; -----------------------------------------------------------------------------
dock_px_add:
    cmp ax, [dock_px1]
    jge .x2
    mov [dock_px1], ax
.x2:
    cmp cx, [dock_px2]
    jle .out
    mov [dock_px2], cx
.out:
    ret

dock_px_tile:
    push ax
    push bx                     ; dock_tile_rect eats BX and DX where
    push cx                     ; dock_tile_x ate only BX - and DX IS LIVE in
    push dx                     ; dock_paint's tile loop (it carries the old
                                ; key across the `xor dx, ax`), so this push
                                ; is not tidiness. CX is the instance index
                                ; and AX a key.
    call dock_tile_rect         ; AX..DX = the tile
    call dock_swap_side         ; ...and its ALONG pair into AX/CX, which is
    call dock_px_add            ; all dock_px_add reads (SPEC.md 30.5)
    jmp kret_dx

; -----------------------------------------------------------------------------
; dock_px_hit - does this rect overlap the strip dock_paint just painted?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 - inclusive, VIRTUAL coordinates
; out: CF = 1 = the strip was drawn under it, so it owes a redraw;
;      all registers preserved
;
; SPEC.md 30.3.3. wm_dmg_wins asked `y + h >= [vid_dock_y0]` and marked the
; window on that alone, which is wrong in two directions at once and both of
; them cost a whole window repaint, W_PAINT included.
;
;   - The strip is painted over a SPAN (30.3.1) and the test had no x in it,
;     so a window over one end of the strip was redrawn because something
;     moved at the other end.
;   - The strip is the PRIMARY's and this rect is virtual (39.19.5), so on an
;     extended desktop a window on the second monitor - which has no dock at
;     all - was marked for reaching the primary's dock ROW. That is the
;     expensive one: a window across the seam can hold no raise cache
;     (39.14.8), so its redraw is a full W_PAINT rather than a ~10 ms blit.
;
; So it is an ordinary rect overlap against the strip that was drawn: the
; painted span in x, and [vid_dock_y0]..[vid_phm1] in y, the primary sitting
; at the virtual origin by construction (39.16). A one-display machine reads
; [vid_phm1] = [vid_hm1] and the y half is exactly what it always was.
; -----------------------------------------------------------------------------
dock_px_hit:
%ifdef REDRAWFULL
    jmp dock_live_hit           ; THE REFERENCE BUILD (SPEC.md 30.3.3): every
                                ; window whose rect reaches the strip, wherever
                                ; along it the paint was
%else
    push ax
    push bx
    push cx
    push dx
    call dock_swap_side         ; AX/CX = ALONG, BX/DX = ACROSS (SPEC.md 30.5)
    cmp ax, [dock_px2]
    jg .no
    cmp cx, [dock_px1]
    jl .no
    cmp bx, [dock_lc2]          ; ...and across it, the live strip's own
    jg .no                      ; extent: which is also what keeps a window on
    cmp dx, [dock_lc1]          ; a second monitor off the primary's strip
    jl .no                      ; (39.19.5) - the display bound the old test
    stc                         ; spelt out is the across test on any edge
    jmp kret_dx
.no:
    clc                         ; SIGNED throughout: [dock_px1]/[dock_px2]
    jmp kret_dx                 ; carry the 07FFFh/08000h empty sentinel
%endif

; -----------------------------------------------------------------------------
; dock_key - what tile i should look like, in one word
; in:  CX = instance index, DI = its record
; out: AX = the key, 0 = this slot has no tile
; clobbers: AX (the output) and flags - which is strictly LESS than it used to
;           (it banked BX and CX for two scratch values it no longer needs), so
;           no caller can be surprised. There is one caller.
;
; Everything dock_draw_tile reads: the icon it blits, and the two bits that
; decide the marks around it. The icon pointer is rotated rather than added so
; that its high bits reach the low end of the word, which spreads the identity
; over the whole word instead of leaving it in the high bits alone.
;
; **That rotation is NOT what makes a freed-and-refilled slot safe, and the
; sentence that used to say so was wrong**: every package's I_ICON is
; I_ICO_PKG (loader.inc:265) or 0 (:261), so two different PACKAGES in one slot
; key identically whatever the rotation does. What actually protects the case
; is SPEC.md 30's own guarantee - every dock-state transition rides a wm_show,
; wm_front, wm_hide or wm_destroy repaint and all four call dock_paint
; unconditionally, so the slot goes to key 0 and back, and the intermediate
; paint is the redraw.
;
; **The marks own bits 0..2 and the identity owns the rest** (SPEC.md 30.3).
; The rotation used to XOR the icon's top three bits straight into the mark
; bits, which is fine for "did anything change" and useless for "what
; changed" - so those three are FOLDED into the high byte instead of landing
; on top of the marks. Nothing is lost and one thing is gained: old XOR new
; now names the difference, and a key of 0 means 'no tile' by construction
; rather than by the bump this used to need.
; -----------------------------------------------------------------------------
dock_key:
    xor ax, ax
    or di, di                   ; a position that shows no record (dock_map)
    jz .out
    cmp byte [di+I_STATE], 1    ; live only - dying and free records have no
    jne .out                    ; tile (SPEC.md 29.2 rule 3)
    mov ax, [di+I_ICON]
    rol ax, 1                   ; x3 - the pointer's high bits reach the low
    rol ax, 1                   ; end. THREE 1-BIT ROTATES: 6 bytes against
    rol ax, 1                   ; `mov cl,3` + `rol ax,cl` + the push/pop of
                                ; CX they need, which is also 6 - and 6 clocks
                                ; against 20 on an 8088
    xor ah, al                  ; the WHOLE low byte folds up, not just its
                                ; bottom three - so no BL and no push bx. The
                                ; five extra bits fold into a byte that is
                                ; pure identity, which changes the hash's
                                ; spread and not its meaning: over all 65,536
                                ; I_ICON values x the four mark combinations
                                ; the collision CLASSES are bit-for-bit the
                                ; old ones (8,192 distinct keys, max class 8),
                                ; so a tile the old code noticed as changed is
                                ; noticed by this one and vice versa
    and al, (DOCK_K_ID & ~DOCK_K_LIVE) & 0FFh
    inc ax                      ; = or al, DOCK_K_LIVE: bit 0 is clear after
                                ; the AND, so this cannot carry out of AL
    test byte [di+I_FLAGS], 1
    jz .nomini
    or al, DOCK_K_MINI          ; minimized: the interior inverts
.nomini:
    cmp di, [dock_act]
    jne .out
    or al, DOCK_K_ACT           ; active: the border doubles
.out:
    ret

; -----------------------------------------------------------------------------
; dock_tile_xy - tile i's top-left corner, on whichever edge the strip stands
; in:  CX = instance index
; out: AX = x, BX = y
; clobbers: the outputs and flags
;
; ALONG the strip by [dock_astep] from [dock_a0], and at [dock_c0] ACROSS it
; (SPEC.md 30.5): x and y on the bottom strip, y and x on a side.
; -----------------------------------------------------------------------------
dock_tile_xy:
    push dx
    mov ax, cx
    mul word [dock_astep]       ; index <= 11 and a step <= 28: no DX
    add ax, [dock_a0]
    mov bx, [dock_c0]
    pop dx
    jmp dock_tile_swap          ; bottom: AX along = x. A side: exchange

dock_tile_swap:
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .out
    xchg ax, bx
.out:
    ret

; -----------------------------------------------------------------------------
; dock_tile_rect - tile i's rect: dock_erase_tile's own fill, dock_px_tile's
;                  span, and SPEC.md 11.99's minimize zoom
; in:  CX = instance index
; out: AX/BX/CX/DX = x1,y1,x2,y2 inclusive
; clobbers: the outputs and flags
;
; Lifted out of dock_erase_tile rather than open-coded a second time, for
; dock_hit's reason (SPEC.md 30.2): where a tile IS has one answer here, not
; one per caller that can drift from the others.
;
; dock_px_tile reads it as a SPAN, so on a side strip it hands back the along
; pair in AX/CX - see there.
; -----------------------------------------------------------------------------
dock_tile_rect:
    call dock_tile_xy           ; AX = tile left, BX = tile top
dock_rect_ab:                   ; ...and the arithmetic ALONE, for a caller
                                ; that already holds the corner in AX/BX -
                                ; dock_draw_tile's frame, and dock_mark_ab
    mov cx, ax
    add cx, DOCK_TILE_W-1
    mov dx, bx
    add dx, DOCK_TILE_H-1
    ret

; -----------------------------------------------------------------------------
; dock_erase_tile - white out tile i's rect
; in:  CX = instance index; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; A tile is redrawn from white because its marks are not nested: going from
; active to plain has to remove the inner frame, and going from minimized to
; plain has to un-invert an interior that a second XOR would only re-invert
; if the tile had not also been redrawn underneath it.
; -----------------------------------------------------------------------------
dock_erase_tile:
    push ax
    push bx
    push cx
    push dx
    call dock_tile_rect         ; AX/BX/CX/DX = the whole tile
    call thm_bg                 ; ("clobbers: nothing", so the rect survives)
    call gfx_fill
    jmp kret_dx

; -----------------------------------------------------------------------------
; dock_xor_ring - toggle tile CX's ACTIVE mark   (the inner frame)
; dock_xor_body - toggle tile CX's MINIMIZED mark (the whole interior)
; in:  CX = instance index; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; Both are exactly the rect dock_draw_tile draws that mark in, and both are
; XOR, so they compose in either order and each undoes itself (SPEC.md 30.3).
;
; The ring is sound because the inner frame's four edges never touch the icon
; - the body is 16x16 at +4,+2 inside a 24x20 tile, clearing the frame by 2px
; and 1px - so every pixel it writes is plain background, white normally and
; black under an inverted interior. XOR is right in both cases, which is why
; the minimized state needs no branch here.
; -----------------------------------------------------------------------------
dock_xor_ring:
    push ax
    push bx
    push cx
    push dx
    call dock_mark_rect
    call gfx_xor_rect
    jmp short dock_xor_pop

dock_xor_body:
    push ax
    push bx
    push cx
    push dx
    call dock_mark_rect
    call gfx_xor_fill
dock_xor_pop:                   ; ONE epilogue: the two routines differ in a
    jmp kret_dx

; dock_mark_rect - the tile interior both marks live in
; in:  CX = instance index
; out: AX = x1, BX = y1, CX = x2, DX = y2
dock_mark_rect:
    call dock_tile_xy           ; AX = tile left, BX = tile top
dock_mark_ab:                   ; ...and the same rect from a caller ALREADY
                                ; holding the tile's corner in AX/BX, which is
                                ; dock_draw_tile's two inner rects. No NASM
                                ; local in the body below, so nothing is
                                ; re-parented by the label.
    call dock_rect_ab           ; THE MARK RECT IS THE TILE RECT INSET 1px on
    inc ax                      ; every side: (L+1, T+1, L+22, T+18) either
    inc bx                      ; way, and it was written out from scratch
    dec cx                      ; here rather than derived
    dec dx
    ret

; -----------------------------------------------------------------------------
; dock_draw_tile - one tile: frame + icon (+ XOR invert when minimized)
; in:  CX = instance index, DI = instance record (I_STATE = 1); gfx lock
;      held by the caller
; out: nothing (all registers preserved; module internal)
; -----------------------------------------------------------------------------
dock_draw_tile:
    push ax
    push bx
    push cx
    push dx
    push si

    call dock_tile_xy           ; AX = tile left, BX = tile top
    mov si, ax                  ; SI = tile left for the duration...
    push bx                     ; ...and the top on the stack, popped below

    call thm_ink                ; "clobbers: nothing (flags preserved)", so AX
    call dock_rect_ab           ; and BX are STILL the corner here: the 1px
    call gfx_frame              ; tile frame IS the tile rect, and gfx_frame
                                ; is "clobbers: flags", so the rect survives it

    mov cx, ax                  ; icon at (left+4, top+2): 16x16 body, off the
    add cx, 4                   ; rect's own x1/y1, which gfx_frame gave back
    mov dx, bx
    inc dx
    inc dx
    push si
    call inst_icon_ptr          ; SI -> 64 bytes in OUR segment, staged if
    jnc .icon                   ; this instance's body lives in LOW_SEG
    mov si, ico_app16 + 2       ; generic fallback: skip the library
.icon:                          ; record's 2-byte header (SPEC.md 25/30)
    call icon_draw16
    pop si
    pop bx                      ; BX = the tile top again

    cmp di, [dock_act]          ; active: a second frame just inside the
    jne .mini                   ; first, which reads as a heavier border and
    call thm_ink                ; costs no colour (SPEC.md 39.4). The icon
    push bx                     ; body is at +4,+2 and 16x16, so it clears
    mov ax, si                  ; both new edges by 2px and 1px
    call dock_mark_ab
    call gfx_frame
    pop bx

.mini:
    test byte [di+I_FLAGS], 1   ; minimized: invert the tile interior
    jz .done
    mov ax, si
    call dock_mark_ab
    call gfx_xor_fill

.done:
    jmp kret_si

; -----------------------------------------------------------------------------
; dock_hit - which tile is under a point? (SPEC.md 30/30.2)
; in:  CX = x, DX = y (absolute)
; out: CF = 1 the point is not in the strip at all;
;      CF = 0 the strip owns it, and DI = the LIVE instance record under the
;      pointer with AL = its slot - or DI = 0 for the bare strip, the gap
;      between two tiles, a slot past the last, and an empty or dying one.
; clobbers: AX, DI (the outputs)
;
; Lifted whole out of dock_click when the right button wanted the same find
; (SPEC.md 30.2). Factored rather than copied because "which records have a
; tile" is one question - SPEC.md 29.2 rule 3 - and a second copy of the
; answer is a second place for a dying record to be offered a menu.
; -----------------------------------------------------------------------------
dock_hit:
    push bx
    cmp cx, [dock_lx1]          ; the LIVE strip (SPEC.md 30.6): the line while
    jl .miss                    ; it is hidden, and all of it otherwise. SIGNED:
    cmp cx, [dock_lx2]          ; every bound is a screen coordinate and so is
    jg .miss                    ; the point, and the miss is a stated CF = 1
    cmp dx, [dock_ly1]          ; rather than a borrow the compare happened to
    jl .miss                    ; leave
    cmp dx, [dock_ly2]
    jg .miss

    xor di, di                  ; in the strip, no tile yet
    cmp byte [dock_hidden], 0
    jne .in                     ; the line: consumed, and no tile on it
    mov ax, cx                  ; the ALONG coordinate (SPEC.md 30.5): x on the
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .along                   ; bottom strip, y on a side
    mov ax, dx
.along:
    sub ax, [dock_a0]           ; before the first tile (unsigned borrow)
    jb .in
    mov bl, [dock_astep]        ; slot = (along - a0) / step
    div bl                      ; AL = slot, AH = within-slot offset
    cmp ah, [dock_alen]         ; in the gap between tiles?
    jae .in
    cmp al, INST_MAX            ; past the last position?
    jae .in
    call dock_map               ; ...and which SLOT it shows (SPEC.md 30.5.1)
    mov bl, al
    xor bh, bh
    mov al, [dock_pos+bx]
    cmp al, 0FFh
    je .in                      ; none
    call inst_ptr               ; DI = record (in: AL = index; AX survives)
    cmp byte [di+I_STATE], 1
    je .in                      ; live: DI is the answer
    xor di, di                  ; empty or dying: no tile here
.in:
    clc
    pop bx
    ret
.miss:
    stc
    pop bx
    ret

; -----------------------------------------------------------------------------
; dock_act_slot - do what tile AL's own mark says is not true yet (SPEC.md 30.4)
; in:  AL = the slot; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; **The tile is a TOGGLE**: minimized (inverted interior) -> inst_restore;
; ACTIVE (doubled border - this instance owns the frontmost visible window) ->
; inst_minimize; anything else -> wm_front. The active tile used to call
; wm_front on a window that was already frontmost, which repaints no window at
; all (SPEC.md 11.90) - a click that consumed itself and did nothing.
;
; It takes a SLOT and re-resolves the record under the lock the caller holds,
; which is where a read of a record the lock protects belongs - and it is what
; lets SPEC.md 30.6.1's tracker call it after the strip has been put away,
; with the record the press found possibly gone in between.
;
; It asks wm_top HERE rather than reading [dock_act], which is dock_paint's
; scratch - fdlg_sel_bar's rule about [fdlg_shown] (SPEC.md 38.3). And it
; compares WINDOWS where dock_key compares records: inst_bind_win writes I_WIN
; and wm_owner together, so the two cannot disagree.
; -----------------------------------------------------------------------------
dock_act_slot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call inst_ptr               ; DI = the record
    cmp byte [di+I_STATE], 1
    jne .out                    ; gone since the press: nothing to do
    test byte [di+I_FLAGS], 1
    jz .front
    call inst_restore           ; minimized: restore (clears bit0, wm_show)
    jmp short .out
.front:
    mov cx, [di+I_WIN]          ; visible: front it - or minimize it, if it is
    jcxz .out                   ; already the front one (SPEC.md 30.4)
    call wm_top                 ; BX = the frontmost VISIBLE window, or 0
    cmp bx, cx
    mov bx, cx                  ; mov does not touch flags: BX = our window for
    je .fmin                    ; whichever of the two calls is taken
    call wm_front
    jmp short .out
.fmin:
    call inst_minimize          ; sets I_FLAGS bit0 and wm_hides, so the tile
                                ; inverts (SPEC.md 30.3)
.out:
    jmp kret_di

; -----------------------------------------------------------------------------
; dock_click - hit-test a click against the strip (SPEC.md 30)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the click was consumed (anywhere in the live strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
; -----------------------------------------------------------------------------
dock_click:
    push ax
    push bx
    push cx
    push dx                     ; DX IS NOT SPENT HERE - it is pushed so that
    push di                     ; dock_rclick can share the tail below, whose
                                ; pop run is ax,bx,cx,dx,di for both

    call dock_hit               ; DI = the live instance under the pointer
    jc .miss
    or di, di
    jz .consumed                ; bare strip, gap or dead slot: consumed no-op

    call gfx_lock
    call dock_act_slot          ; AL = the slot dock_hit found
    call gfx_unlock

.consumed:                      ; --- THE SHARED TAIL. dock_rclick jumps in
    stc                         ; HERE and at `.miss`, and both push
    jmp .out                    ; ax,bx,cx,dx,di in that order - which is now
.miss:                          ; this label's CONTRACT, enforced by nothing
    clc                         ; but the comment at both ends
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_rclick - right-press on the strip: a tile's context menu (SPEC.md 30.2)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the press was consumed (anywhere in the live strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
;
; One item, 'Close': the close box of SPEC.md 13 reached from the dock. The
; strip's own background is consumed and drops NOTHING.
;
; **The record pointer does not survive the tracker; the slot does.**
; menu_popup holds the button and yields (SPEC.md 12.4), so a package worker
; can reach inst_task_die and free the very record this menu is about. So the
; SLOT is carried across it, and dock_close_slot re-resolves it and re-tests
; I_STATE = 1 before anything is done.
;
; Unbilled, exactly like menu_track and fm_rclick (SPEC.md 13).
; -----------------------------------------------------------------------------
dock_rclick:
    push ax                     ; the same push run dock_click has, in the same
    push bx                     ; order, because this routine ENDS IN ITS TAIL
    push cx
    push dx
    push di

    call dock_hit               ; DI = the live instance under the pointer
    jc dock_click.miss
    or di, di
    jz dock_click.consumed      ; bare strip: consumed, and no menu

    call gfx_lock
    call dock_ctx               ; CF = 1 nothing chosen; AL = the slot still
    jc .rdone
    call dock_close_slot
.rdone:
    call gfx_unlock
    jmp dock_click.consumed     ; ...and out through dock_click's tail, whose
                                ; five pops are this routine's five pushes

; -----------------------------------------------------------------------------
; dock_ctx - drop the tile's one-item menu beside the strip (SPEC.md 30.2)
; in:  CX = the press x, DX = the press y, AL = the slot; gfx lock held
; out: CF = 1 nothing was chosen, CF = 0 'Close' was. Every register preserved.
;
; ANCHORED OFF THE STRIP, so it does not land on the tile it is asking about -
; an open hidden strip included, which the menu draws OVER (menu_drop puts the
; hole away while it is down):
; above a bottom strip, right of a left one and left of a right one. menu_popup
; shifts and floors whatever it is handed (SPEC.md 12.4), so DOCK_CTX_H and
; DOCK_CTX_W decide placement and nothing else.
; -----------------------------------------------------------------------------
dock_ctx:
    push ax
    push bx
    push cx
    push dx
    cmp byte [dock_side], DOCK_P_LEFT
    je .left
    ja .right
    mov dx, [dock_ly1]          ; bottom: above the strip, at the press x
    sub dx, DOCK_CTX_H
    jmp short .drop
.left:
    mov cx, [dock_lx2]          ; left: just right of it, at the press y
    inc cx
    jmp short .drop
.right:
    mov cx, [dock_lx1]          ; right: just left of it
    sub cx, DOCK_CTX_W
.drop:
    mov bx, dock_ctx_items
    mov ax, DOCK_CTX_N
    call menu_popup             ; CF = 1 nothing chosen
    pop dx                      ; POP spends no flag
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_close_slot - 'Close' on tile AL, if its instance is still there
; in:  AL = the slot; gfx lock held, UI task (SPEC.md 29.4)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dock_close_slot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call inst_ptr               ; the tracker yielded: find the record again,
    cmp byte [di+I_STATE], 1    ; and take it only if it is still live
    jne .out
    mov bx, [di+I_WIN]
    or bx, bx
    jz .out
    call app_close_win          ; in: BX; lock held, UI task (SPEC.md 29.4)
.out:
    jmp kret_di

; -----------------------------------------------------------------------------
; dock_map - which instance SLOT each tile POSITION shows (SPEC.md 30.5.1)
; in:  nothing
; out: [dock_pos] = INST_MAX bytes, a slot or 0FFh; all registers preserved
;
; A strip that holds every tile keeps SPEC.md 30's stable mapping - position i
; IS slot i, holes and all, so quitting one instance never moves another's
; tile. A side strip on a short screen holds fewer ([dock_cap]: seven on CGA),
; and there the live instances PACK in slot order instead, because a hole
; would cost a tile the strip does not have. When there are still more live
; instances than tiles, the MINIMIZED ones are taken first: the strip is the
; only way back to a minimized window, and an open window can be clicked.
; -----------------------------------------------------------------------------
dock_map:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor bx, bx
.fill:                          ; identity, which is the whole answer when
    mov [dock_pos+bx], bl       ; every slot has a tile
    inc bx
    cmp bx, INST_MAX
    jb .fill
    mov dl, [dock_cap]
    cmp dl, INST_MAX
    jae .out

    mov al, 0FFh                ; --- packed: nothing yet
    xor bx, bx
.none:
    mov [dock_pos+bx], al
    inc bx
    cmp bx, INST_MAX
    jb .none

    xor cx, cx                  ; CL = live, CH = live and minimized
    mov si, inst_tab
    xor bx, bx
.count:
    cmp byte [si+I_STATE], 1
    jne .cnext
    inc cl
    test byte [si+I_FLAGS], 1
    jz .cnext
    inc ch
.cnext:
    add si, I_RECSZ
    inc bx
    cmp bx, INST_MAX
    jb .count

    mov dh, 0FFh                ; DH = minimized still wanted, AH = others
    mov ah, 0FFh
    cmp cl, dl
    jbe .take                   ; they all fit
    mov dh, ch                  ; ...or the minimized first, up to the cap,
    cmp dh, dl                  ; and the open ones in what is left
    jbe .mq
    mov dh, dl
.mq:
    mov ah, dl
    sub ah, dh
.take:
    mov si, inst_tab
    xor bx, bx                  ; BL = the slot
    mov di, dock_pos            ; DI = the next position
.walk:
    cmp byte [si+I_STATE], 1
    jne .wnext
    test byte [si+I_FLAGS], 1
    jz .open
    or dh, dh
    jz .wnext
    dec dh
    jmp short .put
.open:
    or ah, ah
    jz .wnext
    dec ah
.put:
    mov [di], bl
    inc di
.wnext:
    add si, I_RECSZ
    inc bx
    cmp bx, INST_MAX
    jb .walk
.out:
    jmp kret_di

; -----------------------------------------------------------------------------
; dock_slot_rect - the rect of the tile slot CX is SHOWN at (SPEC.md 11.99's
;                  minimize zoom). A slot with no tile on a packed strip flies
;                  to the last one there is.
; in:  CX = instance slot; out: AX/BX/CX/DX = the tile; clobbers flags
; -----------------------------------------------------------------------------
dock_slot_rect:
    push si
    call dock_map
    xor si, si
.find:
    cmp [dock_pos+si], cl
    je .have
    inc si
    cmp si, INST_MAX
    jb .find
    mov si, [dock_cap]          ; (a byte, and the word's high half is masked)
    and si, 0FFh
    dec si
.have:
    mov cx, si
    pop si
    jmp dock_tile_rect

; -----------------------------------------------------------------------------
; dock_pass - the hidden strip's hover, and the open strip's linger
; in:  nothing; UI task, NO lock held - ui_task's tick passes (SPEC.md 13.12)
; out: nothing (all registers preserved)
;
; SPEC.md 30.6. One byte compare on a machine that does not hide its strip.
; NOTHING HERE WAITS: opening draws the strip and returns, and closing puts the
; pixels back and returns, so every task goes on running - and drawing - while
; the strip is open, round the hole the clip region makes for it (30.6.1).
; -----------------------------------------------------------------------------
dock_pass:
    cmp byte [dock_auto], 0
    je .ret
    push ax
    push cx
    push dx
    push di
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    cmp byte [dock_up], 0
    jne .open

    cmp byte [mouse_btn], 0     ; --- HIDDEN: a held button is a drag or a
    jne .off                    ; press somebody else owns
%ifdef KERN_BIG
    cmp byte [blk_sv], 0        ; a saver session covers the screen (79.5)
    jne .off
%endif
    cmp word [fdlg_win], 0      ; a modal dialog owns every press (38.2)
    jne .off
    call wm_fs_vis              ; ...and a fullscreen window covers the strip
    jc .off
    call dock_hit               ; on the line? (clobbers AX and DI)
    jc .off
    mov ax, [ticks]
    cmp byte [dock_hvon], 0
    jne .count
    mov [dock_hvt], ax          ; ...just arrived: start the count
    mov byte [dock_hvon], 1
    jmp short .out
.count:
    sub ax, [dock_hvt]          ; modular, so a [ticks] wrap is one more tick
    cmp ax, DOCK_HOVER_T
    jb .out
    call dock_open
.off:
    mov byte [dock_hvon], 0
    jmp short .out

.open:                          ; --- OPEN: close under a fullscreen window
    call wm_fs_vis              ; or a saver at once - they own every pixel,
    jc .close                   ; the hole included
%ifdef KERN_BIG
    cmp byte [blk_sv], 0        ; a saver is drawing the whole screen and
    je .nosv                    ; repaints it on the way out, so the strip is
    call dock_drop              ; FORGOTTEN rather than put back - a damage
    jmp short .out              ; repaint here would draw over the saver
.nosv:
%endif
    call dock_in_strip          ; ...and otherwise after the linger
    jc .away
    mov byte [dock_lvon], 0     ; on the strip: no count
    jmp short .out
.away:
    mov ax, [ticks]
    cmp byte [dock_lvon], 0
    jne .lcount
    mov [dock_lvt], ax          ; ...just left: start it
    mov byte [dock_lvon], 1
    jmp short .out
.lcount:
    sub ax, [dock_lvt]
    cmp ax, DOCK_LEAVE_T
    jb .out
.close:
    call gfx_lock
    call dock_shut
    call gfx_unlock
.out:
    pop di
    pop dx
    pop cx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; dock_open - put the hidden strip up, OVER everything (SPEC.md 30.6.1)
; in:  nothing; UI task, NO lock held
; out: nothing (all registers preserved)
;
; The lock is held for the draw and for nothing else. What keeps the strip on
; top afterwards is [gfx_hole]: every primitive that finds no region armed
; arms "the desktop minus the strip" before it draws, and every region built
; for a window has the strip subtracted, so nothing any task draws lands on it.
; -----------------------------------------------------------------------------
dock_open:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call gfx_lock
    mov byte [dock_up], 1
    call dock_live_set          ; the live strip is the whole strip now
    mov byte [dock_stale], 1    ; ...and stale until a save-under says not
    mov word [dock_sseg], 0
    call dock_save_kb           ; AX = what THIS strip's rect needs
    mov bx, MEM_K_SAVE
    call mem_claim
    jc .nosave                  ; refused: the close repaints instead
    mov [dock_sseg], dx
    mov es, dx
    xor di, di
    call dock_live_rect
    call gfx_save
    mov byte [dock_stale], 0
.nosave:
    call dock_force             ; the whole strip is owed, every key with it
    call dock_paint             ; (which draws through the hole it is about to
    call dock_hole_build        ; have, and would anyway)
    mov word [gfx_hole], 1
    mov byte [dock_lvon], 0
    call gfx_unlock
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_shut - put the open strip away (SPEC.md 30.6.1)
; in:  gfx lock held; out: nothing (all registers preserved)
;
; The save-under is the answer only while it is still TRUE: something that
; wanted to draw under the strip and was clipped round it ([dock_stale]) left
; the pixels beneath older than the buffer's, and then what is owed is the
; damage repaint a closing window gets - wm_paint_dmg over the strip's rect,
; with the line live again so the repaint draws the line and not the strip.
; -----------------------------------------------------------------------------
dock_shut:
    cmp byte [dock_up], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov word [gfx_hole], 0      ; the hole goes first, and any region armed
    mov word [wm_clip_n], 0     ; round it in this hold with it
    call dock_live_rect         ; the WHOLE strip, while it is still live
    mov byte [dock_up], 0
    call dock_live_set          ; ...and the line from here
    mov si, [dock_sseg]
    or si, si
    jz .repaint
    cmp byte [dock_stale], 0
    jne .free
    push si
    mov es, si
    xor si, si
    call gfx_restore
    pop si
.free:
    push ax
    push bx
    push cx
    push dx
    mov dx, si
    mov bx, MEM_K_SAVE
    call mem_free
    pop dx
    pop cx
    pop bx
    pop ax
    mov word [dock_sseg], 0
    cmp byte [dock_stale], 0
    je .out
.repaint:
    call wm_paint_dmg           ; the desktop, the windows and the line
.out:
    mov byte [dock_hvon], 0     ; the pointer has to rest on the line afresh
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; dock_drop - forget an open strip WITHOUT drawing: the caller repaints the
;             whole screen (dock_geom's callers, an fsx bracket, a saver)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dock_drop:
    cmp byte [dock_up], 0
    je .ret
    push bx
    push dx
    mov word [gfx_hole], 0
    mov word [wm_clip_n], 0
    mov byte [dock_up], 0
    mov dx, [dock_sseg]
    or dx, dx
    jz .nofree
    mov bx, MEM_K_SAVE
    call mem_free
    mov word [dock_sseg], 0
.nofree:
    call dock_live_set
    pop dx
    pop bx
.ret:
    ret

; -----------------------------------------------------------------------------
; dock_hole_build - the region "the desktop minus the open strip"
; in:  the live rect is the whole strip; out: [dock_hole_tab]/[dock_hole_n]
; All registers preserved.
;
; The virtual desktop's rect less one rect: at most four pieces - above,
; below, left and right of the strip - and on an edge strip on one display it
; is one. Built once at the open, copied into wm_clip_tab by gfx_hole_arm.
; -----------------------------------------------------------------------------
dock_hole_build:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, dock_hole_tab
    xor ax, ax
    mov [dock_hole_n], ax
    mov bx, [dock_ly1]          ; --- above
    dec bx
    js .below
    xor dx, dx
    mov cx, [vid_wm1]
    call .add
.below:
    mov bx, [dock_ly2]          ; --- below
    inc bx
    cmp bx, [vid_hm1]
    jg .left
    mov dx, [vid_hm1]
    xor ax, ax
    mov cx, [vid_wm1]
    xchg bx, dx                 ; .add takes x1 AX, y1 DX, x2 CX, y2 BX
    call .add
.left:
    mov cx, [dock_lx1]          ; --- left of it, over its own rows
    dec cx
    js .right
    xor ax, ax
    mov dx, [dock_ly1]
    mov bx, [dock_ly2]
    call .add
.right:
    mov ax, [dock_lx2]          ; --- right of it
    inc ax
    cmp ax, [vid_wm1]
    jg .done
    mov cx, [vid_wm1]
    mov dx, [dock_ly1]
    mov bx, [dock_ly2]
    call .add
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.add:                           ; AX = x1, DX = y1, CX = x2, BX = y2
    mov [di+WCR_X1], ax
    mov [di+WCR_Y1], dx
    mov [di+WCR_X2], cx
    mov [di+WCR_Y2], bx
    add di, WCR_SZ
    inc word [dock_hole_n]
    ret

; -----------------------------------------------------------------------------
; gfx_hole_arm - a primitive found no region armed and the strip open: arm the
;                desktop minus the strip (SPEC.md 30.6.1)
; in:  gfx lock held; [wm_clip_n] = 0, [gfx_hole] set
; out: ZF = 0 (armed); every register preserved
;
; Called from GFXCLIP and CLIPQ, the one question every region-aware primitive
; already asks. It spends the deferred cursor hide first, because an armed
; region is what tells a primitive the hide was already decided (SPEC.md
; 7.1.4). It marks the strip STALE: something is drawing while the strip is
; open, and it cannot be told here whether any of it was meant for the pixels
; under the strip.
; -----------------------------------------------------------------------------
gfx_hole_arm:
    push ax
    push cx
    push si
    push di
    push es
    call cur_unlazy
    mov byte [dock_stale], 1
    push ds
    pop es
    mov si, dock_hole_tab
    mov di, wm_clip_tab
    mov cx, [dock_hole_n]
    mov [wm_clip_n], cx
    shl cx, 1                   ; four words a rect
    shl cx, 1
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    pop ax
    cmp word [wm_clip_n], 0     ; ZF = 0: the list is never empty
    ret

; -----------------------------------------------------------------------------
; dock_hole_sub - subtract the open strip from a region being BUILT
; in:  the list holds zero or more rects; out: CF = 1 it overflowed
; clobbers: flags
;
; wm_clip_occl, wm_dmg_occl and wm_clip_rect subtract the windows over a
; region; this is the strip, which is over all of them. A region that reached
; the strip at all marks it STALE - the window it belongs to wanted pixels the
; strip is covering.
; -----------------------------------------------------------------------------
dock_hole_sub:
    cmp word [gfx_hole], 0
    je .no
    push ax
    push si
    push cx
    mov si, wm_clip_tab
    mov cx, [wm_clip_n]
    jcxz .clean
.scan:
    mov ax, [si+WCR_X2]
    cmp ax, [dock_lx1]
    jl .next
    mov ax, [si+WCR_X1]
    cmp ax, [dock_lx2]
    jg .next
    mov ax, [si+WCR_Y2]
    cmp ax, [dock_ly1]
    jl .next
    mov ax, [si+WCR_Y1]
    cmp ax, [dock_ly2]
    jle .hit
.next:
    add si, WCR_SZ
    loop .scan
.clean:
    pop cx
    pop si
    pop ax
.no:
    clc
    ret
.hit:
    pop cx
    pop si
    mov byte [dock_stale], 1
    mov ax, [dock_lx1]
    mov [wm_cl_ox1], ax
    mov ax, [dock_ly1]
    mov [wm_cl_oy1], ax
    mov ax, [dock_lx2]
    mov [wm_cl_ox2], ax
    mov ax, [dock_ly2]
    mov [wm_cl_oy2], ax
    pop ax
    jmp wm_clip_subr            ; CF is its overflow answer

; -----------------------------------------------------------------------------
; dock_hole_win - is window DI (or BX, for dock_hole_winb) under the open strip?
; out: CF = 1 yes - its pixels there are the strip's, so no cache may be taken
;      from or restored to them; every register preserved
; -----------------------------------------------------------------------------
dock_hole_winb:
    push di
    mov di, bx
    call dock_hole_win
    pop di
    ret
dock_hole_win:
    cmp word [gfx_hole], 0
    je .no
    push ax
    mov ax, [di+W_X]
    cmp ax, [dock_lx2]
    jg .pno
    add ax, [di+W_W]
    cmp ax, [dock_lx1]
    jl .pno
    mov ax, [di+W_Y]
    cmp ax, [dock_ly2]
    jg .pno
    add ax, [di+W_H]
    cmp ax, [dock_ly1]
    jl .pno
    pop ax
    mov byte [dock_stale], 1    ; whatever it skips is owed at the close
    stc
    ret
.pno:
    pop ax
.no:
    clc
    ret

; dock_in_strip - CX,DX on the strip's whole rect? CF = 1 no. Preserves all.
dock_in_strip:
    cmp cx, [dock_sx1]
    jl .no
    cmp cx, [dock_sx2]
    jg .no
    cmp dx, [dock_sy1]
    jl .no
    cmp dx, [dock_sy2]
    jg .no
    clc
    ret
.no:
    stc
    ret

; dock_save_kb - KB the strip's save-under needs (menu_save_kb's arithmetic)
; out: AX; clobbers flags
dock_save_kb:
    push bx
    push cx
    push dx
    mov ax, [dock_sx2]
    mov cl, 3
    shr ax, cl
    mov bx, [dock_sx1]
    shr bx, cl
    sub ax, bx
    inc ax                      ; span -> count, plus the misalignment byte
    inc ax
    mov bx, [dock_sy2]
    sub bx, [dock_sy1]
    inc bx
    mul bx
    mov bx, [vid_planes_w]
    mul bx
    call mem_bytes_kb
    pop dx
    pop cx
    pop bx
    ret

; The whole descriptor (SPEC.md 12.4/30.2): one near pointer to a NUL string,
; and the string is menu.inc's - the same bytes the app-name menu's own Close
; item draws (SPEC.md 12.7), so the two can never come to say different
; things. Kernel data, so menu_popup's [menu_dseg] = 0 is right for it.
dock_ctx_items:
    dw menu_s_appclose

; -----------------------------------------------------------------------------
section .bss

dock_dx1:   resw 1              ; the rule and the white field are owed over
dock_dx2:   resw 1              ; this x span, and no wider: the union of what
                                ; has been drawn over us since the last
                                ; dock_paint (SPEC.md 30.3). THE SPAN IS ITS
                                ; OWN FLAG - empty is 07FFFh..08000h, which is
                                ; what deleted the [dock_full] byte that used
                                ; to sit above it (see dock_force_x)
dock_drew:  resb 1              ; ...and dock_paint's CF: did anything move?
dock_px1:   resw 1              ; the x span dock_paint PUT PIXELS ON in the
dock_px2:   resw 1              ; pass that has just run - the forced span
                                ; above unioned with every tile it redrew, and
                                ; the question dock_px_hit answers about a
                                ; window (SPEC.md 30.3.3)
dock_act:   resw 1              ; the instance record owning the frontmost
                                ; visible window, 0 = none. Read per tile, so
                                ; it is resolved once per dock_paint
dock_ck:    resw INST_MAX       ; what each tile was last DRAWN as (dock_key)

; --- SPEC.md 30.5/30.6: where the strip stands, written by dock_geom and
; dock_live_set alone. .bss, because vid_apply writes every one of them before
; anything reads it.
dock_side:  resb 1              ; the EDGE in use: DOCK_P_*, [dock_cfg]'s or
                                ; the bottom when a side cannot hold the tiles
dock_auto:  resb 1              ; non-0: the strip hides
dock_up:    resb 1              ; ...and is open right now (dock_open)
dock_stale: resb 1              ; something wanted the pixels under the open
                                ; strip, so its save-under is older than them
dock_cap:   resb 1              ; tiles the strip holds (SPEC.md 30.5.1)
dock_pos:   resb INST_MAX       ; which slot each position shows (dock_map)
dock_hole_n: resw 1             ; "the desktop minus the open strip", as the
dock_hole_tab: resw 4 * 4       ; region gfx_hole_arm copies (<= 4 rects)
dock_hidden: resb 1             ; auto and not open: the line is live
dock_sx1:   resw 1              ; the strip's whole rect
dock_sy1:   resw 1
dock_sx2:   resw 1
dock_sy2:   resw 1
dock_frc:   resw 1              ; ...its rule, ACROSS the long axis
dock_ffc1:  resw 1              ; ...its field, across
dock_ffc2:  resw 1
dock_ffar:  resw 1              ; ...and its outer edge, where the line is
dock_la1:   resw 1              ; the long axis's extent: x on the bottom
dock_la2:   resw 1              ; strip, y on a side
dock_a0:    resw 1              ; tile 0's along coordinate
dock_c0:    resw 1              ; ...and every tile's across one
dock_astep: resw 1              ; along pitch
dock_alen:  resw 1              ; along length of one tile
dock_thk:   resw 1              ; what the strip RESERVES of the band
dock_lx1:   resw 1              ; the LIVE rect: the line while hidden
dock_ly1:   resw 1
dock_lx2:   resw 1
dock_ly2:   resw 1
dock_rc:    resw 1              ; ...its rule, across
dock_fc1:   resw 1              ; ...its field, across (fc1 > fc2: none)
dock_fc2:   resw 1
dock_lc1:   resw 1              ; ...and its across extent
dock_lc2:   resw 1
dock_hvon:  resb 1              ; the pointer is on the line, since [dock_hvt]
dock_hvt:   resw 1
dock_lvon:  resb 1              ; the pointer is off the open strip, since
dock_lvt:   resw 1              ; [dock_lvt]
dock_sseg:  resw 1              ; the open strip's save-under, 0 = none

section .text
