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
; The strip's own rule and white field are only redrawn when [dock_full] says
; so, and the one thing that sets it is somebody having drawn OVER the strip:
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

; --- the tile key's low three bits (SPEC.md 30.3) ----------------------------
; The MARKS live here and nothing else does, so old XOR new says exactly what
; moved: a key whose difference is confined to these bits is a tile whose
; ICON did not change, and the marks are both their own inverse.
DOCK_K_LIVE equ 1               ; ...so a live tile's key is never 0
DOCK_K_MINI equ 2               ; the interior inverts
DOCK_K_ACT  equ 4               ; the border doubles
DOCK_K_MARKS equ DOCK_K_MINI | DOCK_K_ACT
DOCK_K_ID   equ 0FFFFh & ~DOCK_K_MARKS  ; everything that is NOT a mark

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

; -----------------------------------------------------------------------------
; dock_init - reset module scratch
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dock_init:
    push cx
    push di
    mov byte [dock_gcol], 0
    mov byte [dock_full], 0     ; .bss is not zeroed at boot (-f bin), and
    call dock_px_none           ; the first paint owes the whole strip - and
    call dock_force             ; nothing has been PAINTED yet (SPEC.md 30.3.3)
    mov di, dock_ck
    mov cx, INST_MAX
.z:
    mov word [di], 0
    inc di
    inc di
    loop .z
    pop di
    pop cx
    ret

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
; two dock_paints.
; -----------------------------------------------------------------------------
dock_force:
    push ax
    push cx
    xor ax, ax
    mov cx, [vid_pwm1]
    call dock_force_x
    pop cx
    pop ax
    ret

dock_force_x:
    push ax
    push cx
%ifdef REDRAWFULL
    xor ax, ax                  ; REFERENCE BUILD: every damage is the whole
    mov cx, [vid_pwm1]           ; strip, the behaviour this replaced
%endif
%ifdef KERN_BIG
    ; --- THE STRIP IS THE PRIMARY'S, AND SO IS ITS DAMAGE (SPEC.md 39.19.5) -
    ; The span comes from a CALLER - wm_paint_dmg hands over the damage a drag
    ; made - and that damage is in VIRTUAL coordinates, so on an extended
    ; desktop it runs straight past the primary's last column. dock_paint then
    ; drew its rule and white field at [vid_dock_y0] across it, and the
    ; primary's dock ROW is inside the secondary's extent, so a full-width
    ; white band appeared across the second monitor on every drag. dock_force
    ; already clamped; this is the entry that did not, and clamping HERE is
    ; what makes it true of every caller rather than of the ones that
    ; remembered. The second monitor carries no menu bar (39.19.3) and it
    ; carries no dock either.
    cmp cx, [vid_pwm1]
    jbe .xok
    mov cx, [vid_pwm1]
.xok:
    cmp ax, cx
    ja .out                     ; wholly past the primary: nothing to restore
%endif
    cmp byte [dock_full], 0
    jne .union
    mov byte [dock_full], 1
    mov [dock_dx1], ax
    mov [dock_dx2], cx
    jmp short .out
.union:
    cmp ax, [dock_dx1]
    jae .u2
    mov [dock_dx1], ax
.u2:
    cmp cx, [dock_dx2]
    jbe .out
    mov [dock_dx2], cx
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_paint - bring the strip up to date: the rule and white field when
;              [dock_full] says they are owed, then every tile whose key moved
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
    push di

    mov al, [gfx_color]         ; byte-only save: gfx_lock_flag lives next
    mov [dock_gcol], al         ; door (see desk_draw_zone's precedent)
    mov byte [dock_drew], 0
    call dock_px_none           ; ...and nothing of the strip is painted YET
                                ; (SPEC.md 30.3.3): [dock_px1]/[dock_px2] are
                                ; this pass's answer, not the last one's

    call wm_top                 ; who owns the frontmost visible window? Its
    mov word [dock_act], 0      ; tile gets the active mark (0 = nobody), and
    or bx, bx                   ; every tile's key depends on it, so it is
    jz .full                    ; resolved before the first one is tested
    call inst_win_owner         ; DI = its instance record, or 0
    mov [dock_act], di

.full:
    cmp byte [dock_full], 0     ; somebody drew over us: the rule and field
    je .scan                    ; they covered, and the tiles they reached
    mov byte [dock_full], 0
    mov byte [dock_drew], 1
    mov ax, [dock_dx1]          ; ...over exactly this span, and it is the
    mov cx, [dock_dx2]          ; span a window has to overlap to be damaged
    call dock_px_add            ; by it (SPEC.md 30.3.3)

    call thm_ink                
    mov ax, [dock_dx1]          ; the rule, over the damaged span only
    mov bx, [dock_dx2]
    mov dx, [vid_dock_y0]
    call gfx_hline

    call thm_bg                
    mov ax, [dock_dx1]          ; ...and the white field under it, down to
    mov cx, [dock_dx2]          ; the last row
    mov bx, [vid_dock_y0]
    inc bx
    mov dx, [vid_phm1]
    call gfx_fill

    mov ax, DOCK_X0             ; ...and the tiles it actually covered: a
    mov di, dock_ck             ; tile outside the damage still has its
    mov cx, INST_MAX            ; pixels, so its key is still the truth
.zap:
    mov bx, ax
    add bx, DOCK_TILE_W-1
    cmp bx, [dock_dx1]
    jb .znext                   ; wholly left of the damage
    cmp ax, [dock_dx2]
    ja .znext                   ; wholly right of it
    mov word [di], 0
.znext:
    add ax, DOCK_STEP
    inc di
    inc di
    loop .zap

.scan:
    mov di, inst_tab            ; one tile per live record, stable slots
    xor cx, cx                  ; CX = instance index
.tile:
    call dock_key               ; AX = what this tile should look like, or 0
    mov bx, cx                  ; for no tile at all (SPEC.md 29.2 rule 3)
    shl bx, 1
    mov dx, [dock_ck+bx]
    cmp ax, dx
    je .next                    ; already drawn that way
    mov [dock_ck+bx], ax
    mov byte [dock_drew], 1
    call dock_px_tile           ; ...and this tile's columns join the painted
                                ; span, wherever it stands - a tile outside the
                                ; forced span still puts pixels down, so the
                                ; span is the UNION of the two (SPEC.md 30.3.3)

    xor dx, ax                  ; DX = what MOVED (SPEC.md 30.3)
%ifdef REDRAWFULL
    jmp short .rebuild          ; REFERENCE BUILD: erase and rebuild any tile
%endif                          ; that changed at all, the behaviour this
                                ; replaced
    test dx, DOCK_K_ID
    jnz .rebuild                ; the icon changed, or the tile came or went

    test dl, DOCK_K_ACT         ; --- only the marks moved: the ICON IS NOT
    jz .mini                    ; TOUCHED, and neither is the frame or the
    call dock_xor_ring          ; white around them. Both marks are their own
.mini:                          ; inverse, so putting one on and taking it off
    test dl, DOCK_K_MINI        ; are the same operation - which is the whole
    jz .next                    ; reason the erase existed
    call dock_xor_body
    jmp short .next

.rebuild:
    push ax
    call dock_erase_tile        ; a new icon is not a superset of the old one,
    pop ax                      ; so this one does start from white
    or ax, ax
    jz .next                    ; the tile went away: erased is all it needed
    call dock_draw_tile         ; in: CX = index, DI = record
.next:
    add di, I_RECSZ
    inc cx
    cmp cx, INST_MAX
    jb .tile

    mov al, [dock_gcol]
    mov [gfx_color], al

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [dock_drew], 0     ; POP does not touch flags, but say it here
    je .clean                   ; anyway rather than depend on that
    stc
    ret
.clean:
    clc
    ret

; -----------------------------------------------------------------------------
; dock_px_none - the painted span is empty
; dock_px_add  - ...grow it to cover x AX..CX as well (inclusive)
; dock_px_tile - ...and to cover tile CX's own columns
; out: nothing; every register preserved (dock_px_add spends flags, which no
;      caller of it needs)
;
; SPEC.md 30.3.3. [dock_px1]/[dock_px2] is what dock_paint PUT PIXELS ON in
; the pass that has just run, which is not [dock_dx1]/[dock_dx2]: that pair is
; what somebody drew OVER US and is only half the answer, because a tile whose
; key moved is repainted wherever it stands, damage or no damage.
;
; Empty is 0x7FFF..0x8000, so an untouched span refuses every rect through
; dock_px_hit's signed compares rather than accepting all of them - the same
; empty-bounding-box idiom wm_dmg_bands uses.
; -----------------------------------------------------------------------------
dock_px_none:
    mov word [dock_px1], 07FFFh
    mov word [dock_px2], 08000h
    ret

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
    push bx                     ; dock_tile_x eats BX, and the tile loop holds
    push cx                     ; the instance index in CX and a key in AX
    call dock_tile_x            ; AX = its left edge
    mov cx, ax
    add cx, DOCK_TILE_W-1
    call dock_px_add
    pop cx
    pop bx
    pop ax
    ret

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
    cmp dx, [vid_dock_y0]       ; THE REFERENCE BUILD (SPEC.md 30.3.3): every
    jge .yes                    ; window whose rect reaches the strip's rows,
    clc                         ; anywhere on the desktop, as this asked before
    ret                         ; the span and the primary's own bounds
.yes:
    stc
    ret
%else
    cmp ax, [dock_px2]
    jg .no
    cmp cx, [dock_px1]
    jl .no
    cmp bx, [vid_phm1]
    jg .no                      ; below the primary altogether: a second
                                ; monitor stacked under it carries no strip
    cmp dx, [vid_dock_y0]
    jl .no
    stc
    ret
.no:
    clc
    ret
%endif

; -----------------------------------------------------------------------------
; dock_key - what tile i should look like, in one word
; in:  CX = instance index, DI = its record
; out: AX = the key, 0 = this slot has no tile
; clobbers: nothing else (flags)
;
; Everything dock_draw_tile reads: the icon it blits, and the two bits that
; decide the marks around it. The icon pointer is rotated rather than added so
; that its high bits reach the low end of the word - two different apps in the
; same slot must not key alike, and a slot can be freed and refilled between
; two paints.
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
    push bx
    push cx
    xor ax, ax
    cmp byte [di+I_STATE], 1    ; live only - dying and free records have no
    jne .out                    ; tile (SPEC.md 29.2 rule 3)
    mov ax, [di+I_ICON]
    mov cl, 3
    rol ax, cl
    mov bl, al
    and bl, DOCK_K_MARKS | DOCK_K_LIVE
    xor ah, bl                  ; ...into the high byte, not onto the marks
    and al, DOCK_K_ID & 0FFh
    or al, DOCK_K_LIVE
    test byte [di+I_FLAGS], 1
    jz .nomini
    or al, DOCK_K_MINI          ; minimized: the interior inverts
.nomini:
    cmp di, [dock_act]
    jne .out
    or al, DOCK_K_ACT           ; active: the border doubles
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dock_tile_x - tile i's left edge
; in:  CX = instance index
; out: AX = x
; clobbers: BX (flags)
; -----------------------------------------------------------------------------
dock_tile_x:
    mov ax, cx                  ; tile left = DOCK_X0 + index*DOCK_STEP
    mov bl, DOCK_STEP           ; (28 is not a power of two: multiply)
    mul bl                      ; AX = AL*BL - index <= 11, fits AL
    add ax, DOCK_X0
    ret

%ifdef WM_ANIM
; -----------------------------------------------------------------------------
; dock_tile_rect - tile i's rect, for SPEC.md 11.99's minimize zoom
; in:  CX = instance index
; out: AX/BX/CX/DX = x1,y1,x2,y2 inclusive
; clobbers: the outputs and flags
;
; Lifted out of dock_erase_tile rather than open-coded a second time, for
; dock_hit's reason (SPEC.md 30.2): where a tile IS has one answer here, not
; one per caller that can drift from the others.
; -----------------------------------------------------------------------------
dock_tile_rect:
    call dock_tile_x            ; AX = tile left
    mov cx, ax
    add cx, DOCK_TILE_W-1
    mov bx, [vid_dock_ty0]
    mov dx, bx
    add dx, DOCK_TILE_H-1
    ret
%endif

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
    call dock_tile_x            ; AX = tile left
    call thm_bg                
    mov cx, ax
    add cx, DOCK_TILE_W-1
    mov bx, [vid_dock_ty0]
    mov dx, bx
    add dx, DOCK_TILE_H-1
    call gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dock_xor_body:
    push ax
    push bx
    push cx
    push dx
    call dock_mark_rect
    call gfx_xor_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dock_mark_rect - the tile interior both marks live in
; in:  CX = instance index
; out: AX = x1, BX = y1, CX = x2, DX = y2
dock_mark_rect:
    call dock_tile_x            ; AX = tile left (clobbers BX)
    mov cx, ax
    add cx, DOCK_TILE_W-2
    inc ax
    mov bx, [vid_dock_ty0]
    inc bx
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-2
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

    call dock_tile_x
    mov si, ax                  ; SI = tile left for the duration

    call thm_ink                
    mov ax, si                  ; 1px tile frame
    mov bx, [vid_dock_ty0]
    mov cx, si
    add cx, DOCK_TILE_W-1
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-1
    call gfx_frame

    mov cx, si                  ; icon at (left+4, top+2): 16x16 body
    add cx, 4
    mov dx, [vid_dock_ty0]
    add dx, 2
    push si
    call inst_icon_ptr          ; SI -> 64 bytes in OUR segment, staged if
    jnc .icon                   ; this instance's body lives in LOW_SEG
    mov si, ico_app16 + 2       ; generic fallback: skip the library
.icon:                          ; record's 2-byte header (SPEC.md 25/30)
    call icon_draw16
    pop si

    cmp di, [dock_act]          ; active: a second frame just inside the
    jne .mini                   ; first, which reads as a heavier border and
    call thm_ink                ; costs no colour (SPEC.md 39.4). The icon
    mov ax, si                  ; body is at +4,+2 and 16x16, so it clears
    inc ax                      ; both new edges by 2px and 1px
    mov bx, [vid_dock_ty0]
    inc bx
    mov cx, si
    add cx, DOCK_TILE_W-2
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-2
    call gfx_frame

.mini:
    test byte [di+I_FLAGS], 1   ; minimized: invert the tile interior
    jz .done
    mov ax, si
    inc ax
    mov bx, [vid_dock_ty0]
    inc bx
    mov cx, si
    add cx, DOCK_TILE_W-2
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-2
    call gfx_xor_fill

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    cmp dx, [vid_dock_y0]       ; above the rule: not ours at all
    jl .miss

    xor di, di                  ; in the strip, no tile yet
    cmp cx, DOCK_X0             ; left of the first tile
    jl .in
    mov ax, cx
    sub ax, DOCK_X0
    mov bl, DOCK_STEP           ; slot = (x - DOCK_X0) / DOCK_STEP
    div bl                      ; AL = slot, AH = within-slot x
    cmp ah, DOCK_TILE_W         ; in the gap between tiles?
    jae .in
    cmp al, INST_MAX            ; past the last slot?
    jae .in
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
; dock_click - hit-test a click against the strip (SPEC.md 30)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the click was consumed (anywhere in the strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
;
; **The tile is a TOGGLE** (SPEC.md 30.4): the click does whatever the tile's
; own mark says is not currently true. Minimized (inverted interior) ->
; inst_restore; ACTIVE (doubled border - this instance owns the frontmost
; visible window) -> inst_minimize; anything else -> wm_front. The active
; tile used to call wm_front on a window that was already frontmost, which
; repaints no window at all (SPEC.md 11.90) - a click that consumed itself
; and did nothing.
;
; Two things about the active test. It asks wm_top HERE rather than reading
; [dock_act], which is dock_paint's scratch and valid only inside the paint
; that resolved it - fdlg_sel_bar's rule about [fdlg_shown] (SPEC.md 38.3) in
; another module. And it compares WINDOWS where dock_key compares records:
; inst_bind_win is the only writer of I_WIN and of wm_owner and writes both
; together, so wm_top == [di+I_WIN] and inst_win_owner(wm_top) == di cannot
; disagree - this is the same predicate the tile's own mark is drawn from,
; taken the cheaper way round.
;
; A window hidden WITHOUT being minimized is never wm_top's answer (it counts
; visible windows only), so it still takes the wm_front path exactly as
; before.
; -----------------------------------------------------------------------------
dock_click:
    push ax
    push bx
    push cx
    push di

    call dock_hit               ; DI = the live instance under the pointer
    jc .miss
    or di, di
    jz .consumed                ; bare strip, gap or dead slot: consumed no-op

    test byte [di+I_FLAGS], 1
    jz .front
    call gfx_lock               ; minimized: restore (clears bit0, wm_show)
    call inst_restore           ; in: DI = record; lock held
    call gfx_unlock
    jmp .consumed

.front:
    call gfx_lock               ; visible: front it - or minimize it, if it is
    mov cx, [di+I_WIN]          ; already the front one (SPEC.md 30.4). CX is
    jcxz .fdone                 ; free (the click x is on the stack) and holds
                                ; the window across wm_top, which answers in BX
    call wm_top                 ; BX = the frontmost VISIBLE window, or 0
    cmp bx, cx
    mov bx, cx                  ; mov does not touch flags: BX = our window for
    je .fmin                    ; whichever of the two calls is taken
    call wm_front               ; in: BX
    jmp short .fdone
.fmin:
    call inst_minimize          ; in: BX; sets I_FLAGS bit0 and wm_hides, so
                                ; the tile inverts (SPEC.md 30.3) and the
                                ; repaint is 11.91's over the vacated frame
.fdone:
    call gfx_unlock

.consumed:
    stc
    jmp .out
.miss:
    clc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_rclick - right-press on the strip: a tile's context menu (SPEC.md 30.2)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the press was consumed (anywhere in the strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
;
; One item, 'Close': the close box of SPEC.md 13 reached from the dock, which
; is the only way to quit an application without first restoring its window
; off the strip. It is anchored at the press x and JUST ABOVE the strip
; (DOCK_CTX_H), so it does not land on the tile it is asking about - the one
; place this departs from fm_rclick, which anchors on the press outright
; because a Disk window's rows are nowhere near a screen edge. The strip's
; own background is
; consumed and drops NOTHING - a press there is neither a tile nor the
; desktop, and a menu about no application would be a menu with nothing in it.
;
; **The record pointer does not survive the tracker; the slot does.**
; menu_popup holds the button and yields (SPEC.md 12.4), so a package worker
; can reach inst_task_die and free the very record this menu is about. So the
; SLOT is banked, re-resolved afterwards, and re-tested for I_STATE = 1
; before anything is done to it. Nothing can refill the slot in that window -
; inst_alloc runs on the UI task, which is standing inside menu_drop - so the
; state test is sufficient and the I_WIN test after it is insurance.
;
; Unbilled, exactly like menu_track and fm_rclick (SPEC.md 13): a held
; button's dwell time is nobody's CPU cost, and the command that follows
; destroys the very record it would be charged to.
; -----------------------------------------------------------------------------
dock_rclick:
    push ax
    push bx
    push cx
    push dx
    push di

    call dock_hit               ; DI = the live instance under the pointer
    jc .miss
    or di, di
    jz .consumed                ; bare strip: consumed, and no menu

    mov [dock_rslot], al        ; AL = its slot - see above
    call gfx_lock
    mov dx, [vid_dock_y0]       ; the menu sits ABOVE the strip rather than on
    sub dx, DOCK_CTX_H          ; it: CX, the press x, is still the anchor
    mov bx, dock_ctx_items
    mov ax, DOCK_CTX_N
    call menu_popup             ; CF = 1 nothing chosen; else AL = the item,
    jc .rdone                   ; and 'Close' is the only one there is

    mov al, [dock_rslot]        ; the tracker yielded: find the record again
    call inst_ptr               ; and take it only if it is still live
    cmp byte [di+I_STATE], 1
    jne .rdone
    mov bx, [di+I_WIN]
    or bx, bx
    jz .rdone
    call app_close_win          ; in: BX; lock held, UI task (SPEC.md 29.4)
.rdone:
    call gfx_unlock

.consumed:
    stc
    jmp .out
.miss:
    clc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The whole descriptor (SPEC.md 12.4/30.2): one near pointer to a NUL string,
; and the string is menu.inc's - the same bytes the app-name menu's own Close
; item draws (SPEC.md 12.7), so the two can never come to say different
; things. Kernel data, so menu_popup's [menu_dseg] = 0 is right for it.
dock_ctx_items:
    dw menu_s_appclose

; -----------------------------------------------------------------------------
section .bss

dock_gcol:  resb 1              ; dock_paint's [gfx_color] save
dock_full:  resb 1              ; the rule and the white field are owed...
dock_dx1:   resw 1              ; ...over this x span, and no wider: the union
dock_dx2:   resw 1              ; of what has been drawn over us since the
                                ; last dock_paint (SPEC.md 30.3)
dock_drew:  resb 1              ; ...and dock_paint's CF: did anything move?
dock_px1:   resw 1              ; the x span dock_paint PUT PIXELS ON in the
dock_px2:   resw 1              ; pass that has just run - the forced span
                                ; above unioned with every tile it redrew, and
                                ; the question dock_px_hit answers about a
                                ; window (SPEC.md 30.3.3)
dock_act:   resw 1              ; the instance record owning the frontmost
                                ; visible window, 0 = none. Read per tile, so
                                ; it is resolved once per dock_paint
dock_rslot: resb 1              ; the SLOT dock_rclick's popup is about - a
                                ; record pointer cannot survive the tracker's
                                ; yields (SPEC.md 30.2)
dock_ck:    resw INST_MAX       ; what each tile was last DRAWN as (dock_key)

section .text
