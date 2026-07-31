; =============================================================================
; os8088 - menu.inc
;
; The menu bar and pull-down menus (SPEC.md 12). The bar is three zones:
; the System menu (an 11x11 hand-authored chip silhouette - the os8088
; logo, and the one place bitmap data is hand-made), which is cell 0 in
; EVERY application; the ACTIVE APPLICATION'S NAME, a label with no hit
; range; and that application's own menus (SPEC.md 12.2).
;
; So the bar is not static data any more. [menu_win] names the window that
; owns it (0 = Locator, the kernel's own application - SPEC.md 12.3), and
; menu_layout rebuilds the runtime menu_bar table from that window's
; W_MENUS set whenever the owner changes. wm_front activates the window it
; raises; a click on the bare desktop activates Locator; and menu_check -
; run at the top of every menu_draw_bar - reverts to Locator the moment the
; owning window stops being visible, which is how close, minimize and hide
; are handled without any of those paths knowing the bar exists.
;
; menu_track runs the entire mouse interaction while the button is held:
; the caller (the UI task) already holds the gfx lock, so tracking reads
; mouse_x/mouse_y/mouse_btn directly (the ISR keeps them fresh; the cursor
; stays hidden - the highlight is the feedback). The pixels under the
; dropped menu are saved to SAVE_SEG:0 and put back on release. It returns
; a (cell, item) pair rather than a flat command, because item numbering
; now has to mean something inside a menu set the kernel has never seen.
;
; Only the bar-specific half of that is menu_track's own. The tracker is
; menu_drop, anchored by variables rather than by MBAR_H and polling
; whichever mouse button [menu_btn] names, so menu_popup can drop the same
; menu machinery anywhere on screen under the RIGHT button (SPEC.md 12.4).
; That split exists for one reason: the SPEC.md 32 rule about what goes
; through the back buffer and what goes straight to VRAM is subtle, and two
; copies of it would drift.
;
; The right end of the bar is the system clock (SPEC.md 12.1): drawn by
; menu_draw_clock, repainted once a second by the UI task, and - because
; ui.inc hit-tests its cell before menu_track ever sees the click - a
; shortcut to the Control Panel's Date/Time page rather than a menu title.
; =============================================================================

; --- the kernel's own commands (SPEC.md 12.3; reconstructed by ui.inc) -------
; The bar no longer carries these: ui_dispatch rebuilds cmd = base + item
; from ui_loc_base for the System menu and for Locator's own menus, which
; keeps the "each menu's commands stay consecutive from its base"
; arithmetic exactly where it always was, just off the data path.
CMD_ABOUT   equ 1               ; --- System (cell 0, every application) ---
CMD_CTRL    equ 2
CMD_TASKS   equ 3
CMD_CLOCK   equ 4               ; --- Locator: File ---
CMD_BOUNCE  equ 5
CMD_FILES   equ 6
CMD_CLOSE   equ 7
CMD_REBOOT  equ 8               ; --- Locator: Special ---

; --- app menu set layout (SPEC.md 12.2; mirrored in apps/os88api.inc) --------
AM_NAME     equ 0               ; word: NUL app name for the bar label; 0 =
                                ; fall back to the instance's I_NAME
AM_ONCMD    equ 2               ; word: near ptr to the command handler, or
                                ; 0 = dispatched by the kernel (Locator)
AM_COUNT    equ 4               ; word: menu count, clamped to MENU_APPMAX
AM_LIST     equ 6               ; AM_COUNT entries of:
AMENU_TITLE equ 0               ; word: NUL menu title (the bar cell text)
AMENU_ITEMS equ 2               ; word: array of near ptrs to NUL item strings
AMENU_NITEM equ 4               ; word: item count
AMENU_ENTSZ equ 6

; --- runtime bar cell layout (menu_bar[], rebuilt by menu_layout) -----------
MB_TITLE    equ 0               ; word: near ptr to NUL title, 0 = logo glyph
MB_ITEMS    equ 2               ; word: near ptr to array of item string ptrs
MB_NITEM    equ 4               ; word: item count
MB_XL       equ 6               ; word: bar hit range, left x (inclusive)
MB_XR       equ 8               ; word: bar hit range, right x (inclusive)
MB_TX       equ 10              ; word: title text / glyph left x
MB_ENTSZ    equ 12
MENU_APPMAX equ 4               ; app menus the bar can host
MENU_BARMAX equ MENU_APPMAX + 1 ; + the System cell, which is always cell 0

MENU_SYS_XR    equ 29           ; System cell: x 0..29, glyph at MENU_SYS_TX
MENU_SYS_TX    equ 10
MENU_NAME_X    equ 38           ; app-name label pen x
MENU_NAME_PAD  equ 16           ; gap after the name, before the first menu
MENU_TITLE_PAD equ 12           ; per-title cell padding (6px each side)

MENU_TEXT_Y equ 6               ; title text row (8px glyphs in a 19px bar)
MENU_LOGO_Y equ 4               ; logo glyph top row (11px tall)
MENU_ITEM_H equ 16              ; pull-down item cell height, px
MENU_POPMAX equ 16              ; items a context menu may have (SPEC.md 12.4)
                                ; - 16 cells plus frame is 258px, which still
                                ; fits under the bar on a 480-row screen

; --- the clock cell at the right end of the bar (SPEC.md 12.1) --------------
; The string's width VARIES with the display settings (SPEC.md 37) - 18
; glyphs for 'Mmm DD YYYY  HH:MM' up to 24 for 'Mmm DD YYYY  HH:MM:SS PM' -
; so the cell is sized for the longest form and the text is right-aligned
; inside it: the erase is always the whole cell, and the time stays pinned
; to the same right margin whichever form it is in.
MENU_CLK_W  equ 24*8                        ; the LONGEST form, not the live one
MENU_CLK_X  equ SCREEN_W - 8 - MENU_CLK_W   ; 440: cell left, 8px right margin
MENU_CLK_HX equ MENU_CLK_X - 6              ; 434: hit band left edge, and the
                                            ; left edge of the erased cell

; -----------------------------------------------------------------------------
; menu_init - boot state: Locator owns the bar (SPEC.md 12)
; in:  nothing (called from kmain after wm_init, before the first paint)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
menu_init:
    push bx
    xor bx, bx
    mov [menu_win], bx
    call menu_relayout
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_activate - hand the menu bar to a window's application (SPEC.md 12)
; in:  BX = window ptr, or 0 for Locator (the desktop)
; out: CF = 1 if the active application CHANGED - the caller owes the bar a
;      repaint; CF = 0 if it was already active. All registers preserved.
;
; Draws nothing and takes no lock, so it is safe from wm_front (which
; repaints anyway) and from the event ladder (which does not).
; -----------------------------------------------------------------------------
menu_activate:
    cmp bx, [menu_win]
    je .same
    mov [menu_win], bx
    call menu_relayout
    stc
    ret
.same:
    clc
    ret

; -----------------------------------------------------------------------------
; menu_win_set - attach an app menu set to a window (API 0x00AC, SPEC 12.2)
; in:  BX = window ptr, SI = menu set ptr (0 = no menus of its own)
; out: nothing (all registers preserved, FLAGS INCLUDED)
;
; The intended caller is a package's entry proc, which runs before the
; loader shows the window - so the relayout below is usually redundant and
; the bar is drawn correctly by the wm_show that follows. Registering later
; is legal but draws nothing of itself: the bar catches up on the next
; wm_paint_all.
;
; Flags are preserved, unlike most of the kernel: the one place this is
; meant to be called from is a package entry proc sitting between
; wm_create and its own `ret`, and that proc owes the loader wm_create's
; CF (SPEC.md 20.2). Clobbering it here would make every package author
; discover the same trap.
; -----------------------------------------------------------------------------
menu_win_set:
    pushf
    mov [bx + W_MENUS], si
    cmp bx, [menu_win]
    jne .out
    call menu_relayout          ; the active application's menus changed
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; menu_check - revert to Locator if the bar's owner stopped being visible
; in:  nothing
; out: nothing (all registers preserved)
;
; One validation covers close, minimize and hide (SPEC.md 12): wm_destroy
; clears W_FLAGS outright and wm_hide clears the visible bit, so neither
; path needs to know the menu bar exists.
; -----------------------------------------------------------------------------
menu_check:
    push bx
    mov bx, [menu_win]
    or bx, bx
    jz .out
    test word [bx + W_FLAGS], 2 ; still visible?
    jnz .out
    xor bx, bx
    call menu_activate          ; back to Locator (CF ignored: our caller
.out:                           ; is menu_draw_bar, already drawing)
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_relayout - rebuild [menu_set], [menu_namep] and menu_bar from
;                 [menu_win] (SPEC.md 12)
; in:  nothing
; out: nothing (all registers preserved)
;
; The name falls back twice: the set's AM_NAME, else the owning instance's
; I_NAME (an accessory like Clock has no menu set at all), else the
; window's own title. Locator - [menu_win] = 0 - takes menu_loc_set, whose
; AM_NAME is the string 'Locator'.
; -----------------------------------------------------------------------------
menu_relayout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [menu_win]
    or bx, bx
    jnz .app
    mov si, menu_loc_set        ; no window owns the bar: Locator does
    mov [menu_set], si
    mov si, [si + AM_NAME]
    jmp .name_done

.app:
    mov si, [bx + W_MENUS]      ; 0 = a window with no menus of its own
    mov [menu_set], si
    or si, si
    jz .from_inst
    mov si, [si + AM_NAME]
    or si, si
    jnz .name_done
.from_inst:
    call inst_win_owner         ; DI = owning instance record, 0 = unowned
    or di, di
    jz .from_title
    mov si, di
    add si, I_NAME              ; the 16-byte inline name field
    jmp .name_done
.from_title:
    mov si, [bx + W_TITLE]

.name_done:
    or si, si                   ; a nameless window still gets a label
    jnz .have_name
    mov si, menu_loc_name
.have_name:
    mov [menu_namep], si

    ; --- cell 0: the System menu, fixed in every application ---------------
    mov di, menu_bar
    mov word [di + MB_TITLE], 0 ; 0 = draw the logo glyph
    mov word [di + MB_ITEMS], menu_items_logo
    mov word [di + MB_NITEM], 3
    mov word [di + MB_XL], 0
    mov word [di + MB_XR], MENU_SYS_XR
    mov word [di + MB_TX], MENU_SYS_TX
    mov word [menu_nbar], 1
    add di, MB_ENTSZ

    ; --- the app's own menus, left to right after the name label -----------
    call font_width             ; SI = the name; AX = its pixel width
    add ax, MENU_NAME_X + MENU_NAME_PAD
    mov bx, ax                  ; BX = running left edge

    mov si, [menu_set]
    or si, si
    jz .done
    mov cx, [si + AM_COUNT]
    cmp cx, MENU_APPMAX         ; never trust a package's count (SPEC 12.2)
    jbe .cnt_ok
    mov cx, MENU_APPMAX
.cnt_ok:
    jcxz .done
    add si, AM_LIST
.cell:
    push cx
    push si
    mov si, [si + AMENU_TITLE]
    call font_width             ; AX = title width
    pop si
    add ax, MENU_TITLE_PAD      ; AX = cell width
    mov dx, bx
    add dx, ax
    cmp dx, MENU_CLK_HX         ; would it reach the clock's band (12.1)?
    ja .full                    ; out of bar: stop here, never clip a cell -
                                ; and never skip one and keep laying out the
                                ; rest, or a cell's bar index would stop
                                ; matching its index in the app's set and
                                ; ui_dispatch's `dec ah` would hand the
                                ; handler the wrong menu (SPEC.md 12/12.2)
    mov ax, [si + AMENU_TITLE]
    mov [di + MB_TITLE], ax
    mov ax, [si + AMENU_ITEMS]
    mov [di + MB_ITEMS], ax
    mov ax, [si + AMENU_NITEM]
    mov [di + MB_NITEM], ax
    mov [di + MB_XL], bx
    mov ax, bx
    add ax, 6                   ; half of MENU_TITLE_PAD: the text is centred
    mov [di + MB_TX], ax
    mov ax, dx
    dec ax
    mov [di + MB_XR], ax        ; xr = left + width - 1
    mov bx, dx                  ; next cell starts where this one ended
    inc word [menu_nbar]
    add di, MB_ENTSZ
    pop cx
    add si, AMENU_ENTSZ
    loop .cell
    jmp short .done

.full:
    pop cx                      ; the bar is full: everything from here on
                                ; is dropped, so the cells that DID fit keep
                                ; bar index == set index
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_draw_bar - draw the menu bar: white rows 0..MBAR_H-2, black line at
;                 row MBAR_H-1, then every live cell's title, the active
;                 application's name label, and the clock.
; in:  nothing (caller holds the gfx lock)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_draw_bar:
    push ax
    push bx
    push cx
    push dx
    push si

    call menu_check             ; the owner may have closed since (SPEC 12)

    mov byte [gfx_color], CWHITE
    xor ax, ax                  ; white field, rows 0..MBAR_H-2
    xor bx, bx
    mov cx, SCREEN_W - 1
    mov dx, MBAR_H - 2
    call gfx_fill

    mov byte [gfx_color], CBLACK
    xor ax, ax                  ; the black rule along the bottom
    mov bx, SCREEN_W - 1
    mov dx, MBAR_H - 1
    call gfx_hline

    mov si, menu_namep          ; the active application's name (SPEC 12)
    mov si, [si]
    mov cx, MENU_NAME_X
    mov dx, MENU_TEXT_Y
    call font_str

    mov si, menu_bar            ; titles (gfx_color is already black)
    mov cx, [menu_nbar]
.title:
    push cx
    mov ax, [si + MB_TITLE]
    test ax, ax
    jz .glyph
    mov cx, [si + MB_TX]
    mov dx, MENU_TEXT_Y
    push si
    mov si, ax
    call font_str
    pop si
    jmp .next
.glyph:
    mov cx, [si + MB_TX]
    call menu_logo_glyph
.next:
    pop cx
    add si, MB_ENTSZ
    loop .title

    call menu_draw_clock        ; the right end of the bar (SPEC.md 12.1)

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_draw_clock - draw the system clock at the right end of the menu bar
; in:  nothing (caller holds the gfx lock)
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
;
; Erases the whole cell first - the longest form's worth, not the live
; string's - so this doubles as the redraw path ui_task drives when the
; text changes (SPEC.md 13 step 4), including a change of FORM. No
; wm_obscured check: windows
; clamp to y >= MBAR_H, so nothing can ever cover the bar. Ordinary
; persistent content - it goes through the back buffer like everything else
; and the caller's gfx_unlock flushes it (SPEC.md 32).
; -----------------------------------------------------------------------------
menu_draw_clock:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CWHITE ; the cell, the black rule below it excluded
    mov ax, MENU_CLK_HX
    xor bx, bx
    mov cx, SCREEN_W - 1
    mov dx, MBAR_H - 2
    call gfx_fill

    call clk_fmt                ; SI = the line in its live form (SPEC.md 37)
    call font_width             ; AX = its width - right-align it, since the
    mov cx, SCREEN_W - 8        ; form (and so the width) can change
    sub cx, ax
    mov byte [gfx_color], CBLACK
    mov dx, MENU_TEXT_Y
    call font_str

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_track - run one full BAR menu interaction. Finds the title under the
;              mousedown, xor-highlights it, hands the drop to menu_drop, and
;              un-highlights the title on the way out.
; in:  CX = mousedown x (caller holds the gfx lock)
; out: AX = 0xFFFF if nothing was selected, else AH = bar cell index
;      (0 = the System menu), AL = item index within that cell (SPEC.md 12)
; clobbers: nothing else
;
; Everything left here is bar-specific: the cell find, the title highlight,
; and the (cell, item) pack. The tracker itself moved into menu_drop so the
; context menu (SPEC.md 12.4) runs the same code - and, far more to the
; point, the same SPEC.md 32 back-buffer discipline. Two copies of that
; would drift.
; -----------------------------------------------------------------------------
menu_track:
    push bx
    push cx
    push dx
    push si
    push di

    ; --- which title was hit? ------------------------------------------------
    mov si, menu_bar
    mov di, [menu_nbar]
    mov byte [menu_cell], 0
.find:
    cmp cx, [si + MB_XL]
    jb .miss
    cmp cx, [si + MB_XR]
    jbe .found
.miss:
    add si, MB_ENTSZ
    inc byte [menu_cell]
    dec di
    jnz .find
    mov ax, 0xFFFF              ; dead zone / the name label: no menu, no work
    jmp short .out

.found:
    mov [menu_ent], si
    call menu_title_xor         ; highlight the title cell (VRAM-direct, so
                                ; no flush needed - SPEC.md 32)

    ; --- menu rect: width = widest item + 16, items are 16px cells -----------
    mov bx, [si + MB_ITEMS]
    mov [menu_iptr], bx
    mov cx, [si + MB_NITEM]
    mov [menu_cnt], cx
    call menu_widest            ; AX = widest item text, px (BX/CX in)
    add ax, 16
    mov bx, [si + MB_XL]        ; the pull-down hangs off the title's left
    mov [menu_x1], bx
    add bx, ax
    dec bx
    mov [menu_x2], bx           ; x2 = x1 + width - 1
    mov word [menu_y1], MBAR_H  ; it always hangs from the bar
    mov ax, [menu_cnt]
    mov cl, 4
    shl ax, cl                  ; count * 16
    add ax, MBAR_H + 1          ; + top frame row; bottom frame is y2 itself
    mov [menu_y2], ax
    mov byte [menu_btn], 1      ; the LEFT button keeps a bar menu open

    call menu_drop              ; AX = item index, or 0xFFFF
    call menu_title_xor         ; un-highlight the title (self-inverting)
    cmp ax, 0xFFFF
    je .out
    mov ah, [menu_cell]         ; out: AH = cell, AL = item (SPEC.md 12)

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_popup - drop a menu anywhere on screen and track it under the right
;              button (SPEC.md 12.4)
; in:  CX = anchor x, DX = anchor y (absolute screen), BX = array of near
;      pointers to NUL item strings, AX = item count. Caller holds the gfx
;      lock.
; out: CF = 1 nothing was chosen; CF = 0 and AL = the item index
; clobbers: AX (the result); everything else preserved
;
; The rect is sized exactly as a bar menu's is, then SHIFTED rather than
; clipped: right overflow moves x1 left (floor 0), bottom overflow moves y1
; up (floor MBAR_H). Clipping would cut items in half and leave the ones
; below unreachable, which is why a right-click in the bottom-right corner
; would otherwise be useless. The popup MAY cover the dock strip - the
; save-under puts it back.
; -----------------------------------------------------------------------------
menu_popup:
    push bx
    push cx
    push dx
    push si
    push di

    or ax, ax
    jz .none                    ; an empty descriptor drops nothing
    cmp ax, MENU_POPMAX
    jbe .cnt_ok                 ; never trust a caller's count - the rect has
    mov ax, MENU_POPMAX         ; to fit on the screen (menu_relayout's rule)
.cnt_ok:
    mov [menu_cnt], ax
    mov [menu_iptr], bx

    ; --- width, and the left shift off the right edge ------------------------
    push cx                     ; the anchor: menu_widest wants CX = count
    mov cx, ax
    call menu_widest            ; AX = widest item text, px (BX = the array)
    add ax, 16                  ; AX = menu width, and stays there
    pop cx                      ; CX = anchor x again

    mov bx, cx                  ; BX = candidate x1
    mov si, SCREEN_W
    sub si, ax                  ; SI = rightmost x1 that still fits
    cmp bx, si
    jle .x_hi
    mov bx, si
.x_hi:
    or bx, bx                   ; low bound last so it wins (a menu wider
    jns .x_lo                   ; than the screen cannot happen, but the
    xor bx, bx                  ; floor costs 4 bytes and removes the doubt)
.x_lo:
    mov [menu_x1], bx
    add bx, ax
    dec bx
    mov [menu_x2], bx

    ; --- height, and the upward shift off the bottom edge --------------------
    mov ax, [menu_cnt]
    mov cl, 4
    shl ax, cl                  ; count * 16 ...
    add ax, 2                   ; ... + the top and bottom frame rows
    mov bx, dx                  ; BX = candidate y1 (DX = anchor y)
    mov cx, SCREEN_H
    sub cx, ax                  ; CX = lowest y1 that still fits
    cmp bx, cx
    jle .y_hi
    mov bx, cx
.y_hi:
    cmp bx, MBAR_H              ; never over the menu bar: it is the one
    jge .y_lo                   ; surface no window may cover either
    mov bx, MBAR_H
.y_lo:
    mov [menu_y1], bx
    add bx, ax
    dec bx
    mov [menu_y2], bx

    mov byte [menu_btn], 2      ; the RIGHT button keeps a popup open
    call menu_drop              ; AX = item index, or 0xFFFF
    cmp ax, 0xFFFF
    je .none
    clc
    jmp short .out
.none:
    mov ax, 0xFFFF
    stc
.out:
    pop di                      ; pop does not touch the flags
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_drop - the tracker both menus share (SPEC.md 12.4): save the pixels
;             under the rect, draw the menu, follow the mouse with an item
;             highlight while the button is held, restore on release.
;             Polls mouse_btn/mouse_x/mouse_y directly and yields every
;             iteration so the scheduler stays healthy (background tasks
;             block on the gfx lock, which we hold, until we return).
; in:  [menu_x1]/[menu_x2]/[menu_y1]/[menu_y2] = the menu rect
;      [menu_cnt]  = item count
;      [menu_iptr] = array of near pointers to NUL item strings
;      [menu_btn]  = the mouse_btn bit mask that keeps the menu open
;      caller holds the gfx lock
; out: AX = item index, or 0xFFFF if released outside every item
; clobbers: nothing else
;
; This is the one copy of the SPEC.md 32 discipline for dropped menus: the
; body is real persistent content and goes through the back buffer with one
; gfx_flush (it would otherwise stay invisible for as long as the button is
; held), while the item highlight is a transient overlay drawn VRAM-direct
; like the cursor. Only one menu is ever open - both callers run on the UI
; task and both are driven by a held button - so SAVE_SEG:0 needs no
; allocator.
; -----------------------------------------------------------------------------
menu_drop:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- save the pixels under the menu to SAVE_SEG:0 -------------------------
    mov ax, SAVE_SEG
    mov es, ax
    xor di, di
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_save

    ; --- draw the menu: white fill, black frame, items 8px in from the left --
    mov byte [gfx_color], CWHITE
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_fill
    mov byte [gfx_color], CBLACK
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_frame

    mov bx, [menu_iptr]
    mov cx, [menu_cnt]
    mov dx, [menu_y1]
    add dx, 1 + 4               ; first cell top + 4px pad centers 8px glyphs
    jcxz .drawn
.item:
    push cx
    mov si, [bx]
    mov cx, [menu_x1]
    add cx, 8
    call font_str
    pop cx
    add bx, 2
    add dx, MENU_ITEM_H
    loop .item
.drawn:
    call gfx_flush              ; pull-down + items on screen (SPEC.md 32)

    ; --- follow the mouse while the button is down ----------------------------
    mov word [menu_sel], 0xFFFF ; nothing highlighted yet
.poll:
    mov al, [menu_btn]
    test [mouse_btn], al
    jz .up
    call menu_hover             ; AX = hovered item, 0xFFFF if outside
    cmp ax, [menu_sel]
    je .same
    push ax                     ; highlight moved: erase old, draw new
    mov ax, [menu_sel]
    cmp ax, 0xFFFF
    je .no_old
    call menu_item_xor
.no_old:
    pop ax
    mov [menu_sel], ax
    cmp ax, 0xFFFF
    je .same
    call menu_item_xor
.same:
                                ; no flush: the highlights are VRAM-direct and
                                ; nothing else draws while we hold the lock,
                                ; so the back buffer is untouched here (§32)
    call task_yield             ; keep the scheduler healthy while we spin
    jmp .poll

    ; --- released: put the screen back, report the selection -------------------
.up:
    mov ax, SAVE_SEG            ; restore erases the menu and any highlight
    mov es, ax
    xor si, si
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_restore

    mov ax, [menu_sel]

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_hover - which item cell is the mouse inside right now?
; in:  [menu_x1]/[menu_x2]/[menu_y1]/[menu_cnt] set up by menu_drop's caller
; out: AX = item index, 0xFFFF if the mouse is outside the menu rect
; clobbers: nothing else
; -----------------------------------------------------------------------------
menu_hover:
    push cx
    mov ax, [mouse_x]
    cmp ax, [menu_x1]
    jb .none
    cmp ax, [menu_x2]
    ja .none
    mov cx, [menu_y1]
    inc cx                      ; the top frame row is not an item cell
    mov ax, [mouse_y]
    sub ax, cx                  ; rows above the first cell borrow out
    jb .none
    mov cl, 4
    shr ax, cl                  ; / 16 -> cell index
    cmp ax, [menu_cnt]
    jae .none                   ; below the last cell (or on the frame)
    pop cx
    ret
.none:
    mov ax, 0xFFFF
    pop cx
    ret

; -----------------------------------------------------------------------------
; menu_item_xor - toggle the highlight on one item cell (xor is its own undo)
; in:  AX = item index; [menu_x1]/[menu_x2]/[menu_y1] set up as above
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_item_xor:
    push ax
    push bx
    push cx
    push dx
    mov cl, 4
    shl ax, cl                  ; index * 16
    add ax, [menu_y1]
    inc ax                      ; cell top row (the frame row is not a cell)
    mov bx, ax
    mov dx, ax
    add dx, MENU_ITEM_H - 1     ; cell bottom row
    mov ax, [menu_x1]
    inc ax                      ; keep the frame out of the highlight
    mov cx, [menu_x2]
    dec cx
    call vga_xor_fill_vram      ; VRAM-direct transient overlay (SPEC.md 32)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_title_xor - toggle the highlight on the tracked menu's title cell
; in:  [menu_ent] -> menu table entry
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_title_xor:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [menu_ent]
    mov ax, [si + MB_XL]
    xor bx, bx
    mov cx, [si + MB_XR]
    mov dx, MBAR_H - 2          ; the black rule stays black
    call gfx_xor_fill
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_widest - widest item text in a menu, in pixels
; in:  BX = array of near pointers to NUL item strings, CX = item count
; out: AX = max font_width over them (0 when CX = 0)
; clobbers: nothing else
;
; Parameterized by the array rather than by a bar cell (which is what it took
; before) precisely so menu_popup can size a menu that has no bar cell at all
; - a context menu's items live in a descriptor, not in menu_bar (SPEC 12.4).
; -----------------------------------------------------------------------------
menu_widest:
    push bx
    push cx
    push dx
    push si
    xor dx, dx                  ; running max
    jcxz .done                  ; `loop` would wrap CX to 0xFFFF
.next:
    mov si, [bx]
    call font_width             ; AX = 8 * strlen
    cmp ax, dx
    jbe .keep
    mov dx, ax
.keep:
    add bx, 2
    loop .next
.done:
    mov ax, dx
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_logo_glyph - draw the 11x11 chip silhouette (the os8088 logo) in
; [gfx_color]
; in:  CX = left x (top is MENU_LOGO_Y)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_logo_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, menu_logo_bmp
    mov di, cx                  ; DI = left x
    mov dx, MENU_LOGO_Y
    mov bh, 11                  ; rows
.row:
    lodsw                       ; bit 15 = leftmost pixel of the row
    mov cx, di
    mov bl, 11                  ; columns
.col:
    shl ax, 1
    jnc .skip
    call gfx_pixel              ; CX = x, DX = y
.skip:
    inc cx
    dec bl
    jnz .col
    inc dx
    dec bh
    jnz .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu data: logo bitmap, strings, item lists, the menu table
; -----------------------------------------------------------------------------

; 11x11 DIP-chip silhouette - the os8088 logo: a 7px-wide package body with
; a notch at the top and four pins down each side. One word per row,
; bit 15 = x offset 0.
menu_logo_bmp:
    dw 0x0000                   ; ...........
    dw 0x3B80                   ; ..###.###..   body top, orientation notch
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..   body bottom
    dw 0x0000                   ; ...........

menu_loc_name   db 'Locator', 0 ; the kernel's own application (SPEC 12.3)
menu_s_file     db 'File', 0
menu_s_special  db 'Special', 0

menu_s_about    db 'About os8088...', 0
menu_s_ctrl     db 'Control Panel', 0
menu_s_clock    db 'Clock', 0
menu_s_bounce   db 'Bounce', 0
menu_s_disk     db 'Disk', 0
menu_s_close    db 'Close Window', 0
menu_s_tasks    db 'Task Manager', 0
menu_s_restart  db 'Restart', 0

menu_items_logo:
    dw menu_s_about
    dw menu_s_ctrl
    dw menu_s_tasks
menu_items_file:
    dw menu_s_clock
    dw menu_s_bounce
    dw menu_s_disk
    dw menu_s_close
menu_items_special:
    dw menu_s_restart

; Locator's menu set (SPEC.md 12.3) - an ordinary SPEC.md 12.2 structure
; whose AM_ONCMD is 0, the one value reserved to mean "dispatched by the
; kernel": ui_dispatch recognises it and rebuilds a CMD_* instead of
; calling through. It is the set the bar falls back to when no window owns
; it; a file-manager window carries fm_menus instead, which shares this
; set's AM_NAME so the bar still reads 'Locator' either way (SPEC.md 12.3).
menu_loc_set:
    dw menu_loc_name            ; AM_NAME
    dw 0                        ; AM_ONCMD: kernel-dispatched
    dw 2                        ; AM_COUNT
    dw menu_s_file,    menu_items_file,    4
    dw menu_s_special, menu_items_special, 1

; -----------------------------------------------------------------------------
section .bss

menu_win:   resw 1              ; window owning the bar, 0 = Locator (12)
menu_set:   resw 1              ; its app menu set (SPEC.md 12.2)
menu_namep: resw 1              ; the name string drawn at MENU_NAME_X
menu_bar:   resb 60             ; MENU_BARMAX x MB_ENTSZ (5 x 12; a literal
                                ; per the no-forward-ref-in-bss rule)
menu_nbar:  resw 1              ; live cells in menu_bar (>= 1: the System
                                ; menu is always cell 0)
menu_ent:   resw 1              ; -> bar cell being tracked
menu_cell:  resb 1              ; its index (the AH half of menu_track's out)
menu_x1:    resw 1              ; dropped menu rect, left
menu_x2:    resw 1              ; dropped menu rect, right
menu_y1:    resw 1              ; dropped menu rect, top - MBAR_H from the
                                ; bar, the shifted anchor from a popup (12.4)
menu_y2:    resw 1              ; dropped menu rect, bottom
menu_cnt:   resw 1              ; item count of the tracked menu
menu_iptr:  resw 1              ; its array of near item-string pointers
menu_btn:   resb 1              ; mouse_btn mask that keeps it open: 1 = the
                                ; bar's left button, 2 = a popup's right one
menu_sel:   resw 1              ; highlighted item, 0xFFFF = none

section .text
