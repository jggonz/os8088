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
; IN THE BOOT OVERLAY (SPEC.md 2.5.3 - the WINDOW half, which is what OVWCALL
; reaches; 2.5.2 is `.ovl`), docs/LAST-DROP-BYTES.md row 15. One caller,
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
    mov word [dock_dx1], 0      ; THE WHOLE STRIP is owed - dock_force's own
    mov ax, [vid_pwm1]          ; answer, written here rather than called for
    mov [dock_dx2], ax
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
    push ax                     ; THE WHOLE STRIP is dock_force_x's span with
    push cx                     ; AX = 0 and CX = the last column, so this
    xor ax, ax                  ; enters that routine BELOW its own prologue
    mov cx, [vid_pwm1]          ; and lets its `.out` pop the pair we pushed.
    jmp dock_force_x.body       ; A REAL COUPLING: an instruction added between
                                ; `dock_force_x:` and `.body` is one this path
                                ; silently skips. Written `jmp` and never
                                ; `jmp short`, so a prologue that outgrows 128
                                ; bytes gets a longer encoding instead of an
                                ; assembly error.

dock_force_x:
    push ax
    push cx
.body:                          ; dock_force enters HERE - see there
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
    ja .out                     ; wholly past the primary: nothing to restore.
                                ; STAYS INSIDE THE %ifdef - it is unreachable
                                ; without the clamp above it, the one caller
                                ; passing x1 <= x2 by construction
                                ; (wm_paint_dmg), and kern_small pays 4 bytes
                                ; for a test it cannot fail
%endif
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
    call thm_ink                
    mov bx, cx                  ; the rule, over the damaged span only
    mov dx, [vid_dock_y0]
    call gfx_hline

    call thm_bg                
    mov bx, dx                  ; ...and the white field under it, down to
    inc bx                      ; the last row. DX is gfx_hline's own y, given
    mov dx, [vid_phm1]          ; back untouched
    call gfx_fill

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
    mov ax, DOCK_X0
    mov di, dock_ck
    mov cx, INST_MAX
.zap:
    mov bx, ax
    add bx, DOCK_TILE_W-1
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
    add ax, DOCK_STEP
    inc di
    inc di
    loop .zap

.scan:
    mov di, inst_tab            ; one tile per live record, stable slots
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
    xor cx, cx                  ; CX = instance index
.tile:
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
    add di, I_RECSZ
    inc bx
    inc bx
    inc cx
    cmp cx, INST_MAX
    jb .tile

    pop ax                      ; the pen, saved at `.go` - see there for the
    mov [gfx_color], al         ; one-exit invariant this rests on. AH is dead
                                ; (AX itself is popped by the rung below).
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
    call dock_tile_rect         ; AX = its left edge, CX its right
    call dock_px_add            ; (which reads AX and CX only)
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
    cmp dx, [vid_dock_y0]       ; THE LAST TEST IS THE ANSWER: CF = 1 says the
    cmc                         ; rect's y2 is above the rule, so `cmc` leaves
    ret                         ; CF = "it reaches the strip". The other three
                                ; stay SIGNED and must: [dock_px1]/[dock_px2]
                                ; carry the 07FFFh/08000h empty sentinel. This
                                ; one is a window's y2 out of wm_win_rect
                                ; ([si+W_Y] + [si+W_H], at or below the menu
                                ; bar with a positive height) against
                                ; [vid_dock_y0], which is 176..456 - so
                                ; signed-less and unsigned-below agree over
                                ; every reachable input, and a negative y2
                                ; could only ever cost a wasted repaint
                                ; (SPEC.md 30.3.3's safe direction)
.no:
    clc
    ret
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

; -----------------------------------------------------------------------------
; dock_tile_rect - tile i's rect: dock_erase_tile's own fill, dock_px_tile's
;                  span, and SPEC.md 11.99's minimize zoom
; in:  CX = instance index
; out: AX/BX/CX/DX = x1,y1,x2,y2 inclusive
; clobbers: the outputs and flags
;
; Lifted out of dock_erase_tile rather than open-coded a second time, for
; dock_hit's reason (SPEC.md 30.2): where a tile IS has one answer here, not
; one per caller that can drift from the others.  It was `%ifdef WM_ANIM`
; while the zoom was its only caller and dock_erase_tile kept the copy it had
; been lifted from - so the file carried the routine AND the thing it
; replaced.  It is unconditional now: kern_small pays 4 bytes for a routine
; its zoom never calls, and kern_big saves 14.
; -----------------------------------------------------------------------------
dock_tile_rect:
    call dock_tile_x            ; AX = tile left
dock_rect_ax:                   ; ...and the arithmetic ALONE, for a caller
                                ; that already holds the left edge in AX -
                                ; dock_draw_tile's frame, and dock_mark_ax
                                ; below. desk.inc's desk_zone_rect /
                                ; desk_rect_ax is the same shape one module
                                ; along (a different section, so nothing
                                ; merges - only the idiom is shared). Costs
                                ; nothing: a second entry point, not a
                                ; routine, and the body below has no NASM
                                ; local for this label to re-parent
    mov cx, ax
    add cx, DOCK_TILE_W-1
    mov bx, [vid_dock_ty0]
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
    call dock_tile_x            ; AX = tile left (clobbers BX)
dock_mark_ax:                   ; ...and the same rect from a caller ALREADY
                                ; holding the tile's left edge in AX, which is
                                ; dock_draw_tile's two inner rects. No NASM
                                ; local in the body below, so nothing is
                                ; re-parented by the label.
    call dock_rect_ax           ; THE MARK RECT IS THE TILE RECT INSET 1px on
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

    call dock_tile_x
    mov si, ax                  ; SI = tile left for the duration

    call thm_ink                ; "clobbers: nothing (flags preserved)", so AX
    call dock_rect_ax           ; is STILL the tile left here: the 1px tile
    call gfx_frame              ; frame IS the tile rect, and gfx_frame is
                                ; "clobbers: flags", so the rect survives it

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

    cmp di, [dock_act]          ; active: a second frame just inside the
    jne .mini                   ; first, which reads as a heavier border and
    call thm_ink                ; costs no colour (SPEC.md 39.4). The icon
    mov ax, si                  ; body is at +4,+2 and 16x16, so it clears
    call dock_mark_ax           ; both new edges by 2px and 1px
    call gfx_frame

.mini:
    test byte [di+I_FLAGS], 1   ; minimized: invert the tile interior
    jz .done
    mov ax, si
    call dock_mark_ax
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
    cmp dx, [vid_dock_y0]       ; above the rule: not ours at all. UNSIGNED,
    jb .out                     ; and that is what makes the two exits ONE - a
                                ; y out of the event queue is mou_clamp's,
                                ; 0..[vid_hm1] with an explicit `or bx,bx /
                                ; jns` on the negative side, so `jb` and `jl`
                                ; name the same rows and `jb` leaves CF = 1,
                                ; which IS the miss answer. `pop` spends no
                                ; flag, so it survives to the `ret`

    xor di, di                  ; in the strip, no tile yet
    cmp cx, DOCK_X0             ; left of the first tile (unsigned for the
    jb .in                      ; same reason, and byte-neutral)
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
    clc                         ; ...and falls into the exit the miss jumps to
.out:
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
    push dx                     ; DX IS NOT SPENT HERE - it is pushed so that
    push di                     ; dock_rclick can share the tail below, whose
                                ; pop run is ax,bx,cx,dx,di for both

    call dock_hit               ; DI = the live instance under the pointer
    jc .miss
    or di, di
    jz .consumed                ; bare strip, gap or dead slot: consumed no-op

    call gfx_lock               ; ONE bracket for all three commands, and the
                                ; I_FLAGS read is now INSIDE it - which is
                                ; where a read of a record the lock protects
                                ; belongs. gfx_lock is "clobbers: flags" and
                                ; preserves DI, which the restore arm below
                                ; already relied on
    test byte [di+I_FLAGS], 1
    jz .front
    call inst_restore           ; minimized: restore (clears bit0, wm_show).
    jmp short .fdone            ; in: DI = record; lock held

.front:
    mov cx, [di+I_WIN]          ; visible: front it - or minimize it, if it is
    jcxz .fdone                 ; already the front one (SPEC.md 30.4). CX is
                                ; free (the click x is on the stack) and holds
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

.consumed:                      ; --- THE SHARED TAIL. dock_rclick jumps in
    stc                         ; HERE and at `.miss`, and both push
    jmp .out                    ; ax,bx,cx,dx,di in that order - which is now
.miss:                          ; this label's CONTRACT, enforced by nothing
    clc                         ; but the comment at both ends. `jmp`, never
.out:                           ; `jmp short`: a body that outgrows the rel8
    pop di                      ; range then costs a byte instead of an error
    pop dx
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
; SLOT is banked - ON THE STACK, which is the UI task's own and which the
; tracker's yields do not touch; what cannot survive is the RECORD POINTER,
; not a small integer - re-resolved afterwards, and re-tested for I_STATE = 1
; before anything is done to it. Nothing can refill the slot in that window -
; inst_alloc runs on the UI task, which is standing inside menu_drop - so the
; state test is sufficient and the I_WIN test after it is insurance.
;
; Unbilled, exactly like menu_track and fm_rclick (SPEC.md 13): a held
; button's dwell time is nobody's CPU cost, and the command that follows
; destroys the very record it would be charged to.
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

    push ax                     ; AL = the SLOT - see above. menu_popup is
    call gfx_lock               ; "clobbers: AX (the result); everything else
    mov dx, [vid_dock_y0]       ; preserved", so SP balance across it is
    sub dx, DOCK_CTX_H          ; contractual. The menu sits ABOVE the strip
    mov bx, dock_ctx_items      ; rather than on it: CX, the press x, is still
    mov ax, DOCK_CTX_N          ; the anchor
    call menu_popup             ; CF = 1 nothing chosen; else AL = the item,
    pop ax                      ; and 'Close' is the only one there is. POP
    jc .rdone                   ; spends no flag, so the CF here is still
                                ; menu_popup's - and the stale AH it brings
                                ; back is harmless (inst_ptr reads AL)

    call inst_ptr               ; the tracker yielded: find the record again,
    cmp byte [di+I_STATE], 1    ; and take it only if it is still live
    jne .rdone
    mov bx, [di+I_WIN]
    or bx, bx
    jz .rdone
    call app_close_win          ; in: BX; lock held, UI task (SPEC.md 29.4)
.rdone:
    call gfx_unlock
    jmp dock_click.consumed     ; ...and out through dock_click's tail, whose
                                ; five pops are this routine's five pushes

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

section .text
