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
MB_SEG      equ 12              ; word: the SEGMENT this cell's title and
                                ; item strings live in, 0 = the kernel's own
                                ; (SPEC.md 12.2/20.1). Per CELL and not per
                                ; SET, because cell 0 is the System menu and
                                ; its items are always the kernel's however
                                ; foreign the rest of the bar is - reading
                                ; them through a package's segment lands on
                                ; the .o88 header and every item draws "O8"
MB_ENTSZ    equ 14
MENU_APPMAX equ 4               ; app menus the bar can host
MENU_BARMAX equ MENU_APPMAX + 2 ; + the System cell, which is always cell 0,
                                ; + the app-NAME cell, which exists only when
                                ; the front window declared an About handler
                                ; (SPEC.md 12.2) and is APPENDED last so the
                                ; app's own menus keep bar index = set index+1
MENU_ABPFX_LEN equ 6            ; 'About ' - menu_abstr's name copy starts
                                ; here, which is why one buffer serves both
                                ; the cell's TITLE and its single ITEM

MENU_SYS_XR    equ 29           ; System cell: x 0..29, glyph at MENU_SYS_TX
MENU_SYS_TX    equ 10
MENU_NAME_X    equ 38           ; app-name label pen x
MENU_NAME_PAD  equ 16           ; gap after the name, before the first menu
MENU_TITLE_PAD equ 12           ; per-title cell padding (6px each side)

MENU_TEXT_Y equ 6               ; title text row (8px glyphs in a 19px bar)
MENU_LOGO_Y equ 4               ; logo glyph top row (11px tall)
MENU_ITEM_H equ 16              ; pull-down item cell height, px
MENU_POPMAX equ 11              ; items ANY pull-down may have (SPEC.md 12.4)

; The save-under bound (SPEC.md 2.2/50.2). gfx_save costs
; planes x rows x (byte span + 1), and until these two clamps existed
; nothing in the kernel bounded either factor: a package's menu set could
; declare items of any width and any count, and the heap it wrote into was
; 48KB of pinned address space chosen to be "probably enough". Now the
; pull-down is clamped to MENU_MAXW pixels wide and MENU_POPMAX items tall,
; strings are TRUNCATED to fit (menu_trunc - a clamp that only narrowed the
; rect would leave glyphs outside it that the restore can never erase), and
; kernel.asm asserts the product against MENU_SAVE_KB at build time.
MENU_MAXW   equ 208             ; widest pull-down, px (frame + 8px padding)
MENU_MAXCH  equ (MENU_MAXW - 16) / 8    ; ...so 24 glyphs of item text
MENU_SAVE_KB equ 20             ; the claim menu_init takes for the save-under
                                ; - 16 cells plus frame is 258px, which still
                                ; fits under the bar on a 480-row screen

; --- disabled items (SPEC.md 12.2) -------------------------------------------
; An item string that BEGINS with this byte is unavailable: the kernel draws
; the rest of it in grey and menu_hover never lands on it, so it cannot be
; highlighted and cannot be selected. It is a string prefix and not a flags
; array on purpose - an application that wants an item disabled already has
; to decide that per repaint, and it already had to point AM_ITEMS at a
; different string to say so ("Save Gif" vs "Save Gif (NoRam)"). One byte in
; front of that string costs no structure change, no ABI change, and works
; for a built-in's menus exactly as it does for a package's.
MENU_DIS    equ 1

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
    push ax
    push bx
    push dx
    mov byte [menu_bdirty], 1   ; .bss is not zeroed at boot (-f bin)
    xor bx, bx
    mov [menu_win], bx
    mov word [menu_sseg], 0     ; the save-under is claimed by menu_drop for
    call menu_relayout          ; as long as a menu is actually down, not
                                ; here for the session (SPEC.md 12.4/50.2)
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_force - the whole bar must be redrawn, whoever owns it
; in:  nothing
; out: nothing (all registers preserved)
;
; For callers that drew over the bar rather than changed what belongs on it.
; -----------------------------------------------------------------------------
menu_force:
    mov byte [menu_bdirty], 1
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
; menu_win_set - attach an app menu set to a window (API 0x0148, SPEC 12.2)
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
; menu_check - hand the bar on when its owner stops being visible
; in:  nothing
; out: nothing (all registers preserved)
;
; One validation covers close, minimize and hide (SPEC.md 12): wm_destroy
; clears W_FLAGS outright and wm_hide clears the visible bit, so neither
; path needs to know the menu bar exists.
;
; The bar goes to the window PROMOTED in its place, not to Locator. Losing
; the front window promotes whatever was under it - wm_paint_dmg redraws
; that window's title bar with the pinstripes and the two boxes for exactly
; that reason (SPEC.md 11.91) - and the bar has to name the same application
; the pinstripes do, or the screen says two different things about which app
; is active. wm_top answers 0 when no visible window is left, and 0 IS
; Locator, so the old fallback is still the last rung of this one.
;
; A deliberate switch to Locator (the .desk_icons branch of the event
; ladder, SPEC.md 12) stays put: [menu_win] = 0 leaves here at the first
; test, so a later hide cannot drag the bar back onto a window.
; -----------------------------------------------------------------------------
menu_check:
    push bx
    mov bx, [menu_win]
    or bx, bx
    jz .out
    test word [bx + W_FLAGS], 2 ; still visible?
    jnz .out
    call wm_top                 ; BX = the window promoted in its place, or
    call menu_activate          ; 0 = nothing visible = Locator (CF ignored:
.out:                           ; our caller is menu_draw_bar, already
    pop bx                      ; drawing)
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
    mov byte [menu_bdirty], 1   ; the cells, the name, or both, are about to
    push ax                     ; be different: the bar owes a full redraw
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov bx, [menu_win]
    or bx, bx
    jnz .app
    mov word [menu_seg], 0      ; no window owns the bar: Locator does, and
    push cs                     ; its set is kernel data
    pop es
    mov si, menu_loc_set
    mov [menu_set], si
    mov si, [es:si + AM_NAME]
    jmp .name_done

.app:
    push di                     ; the owner's segment, once, for every read
    mov di, bx                  ; of the set below AND for menu_draw_bar,
    call wm_strseg              ; menu_drop and menu_widest afterwards
    pop di                      ; (SPEC.md 12.2/20.1)
    mov ax, es
    mov cx, cs
    cmp ax, cx
    jne .pkgset
    xor ax, ax
.pkgset:
    mov [menu_seg], ax
    mov si, [bx + W_MENUS]      ; 0 = a window with no menus of its own
    mov [menu_set], si
    or si, si
    jz .from_inst
    mov si, [es:si + AM_NAME]
    or si, si
    jnz .name_done
.from_inst:
    call inst_win_owner         ; DI = owning instance record, 0 = unowned
    or di, di
    jz .from_title
    mov si, di
    add si, I_NAME              ; the 16-byte inline name field - KERNEL data,
    mov word [menu_seg], 0      ; so the label's segment is ours after all
    push cs
    pop es
    jmp .name_done
.from_title:
    mov si, [bx + W_TITLE]

.name_done:
    or si, si                   ; a nameless window still gets a label
    jnz .have_name
    mov si, menu_loc_name
    mov word [menu_seg], 0
    push cs
    pop es
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
    mov word [di + MB_SEG], 0   ; the System menu is the kernel's, always
    mov word [menu_nbar], 1
    add di, MB_ENTSZ

    ; --- the app's own menus, left to right after the name label -----------
    call font_width_x           ; SI = the name at ES:SI; AX = its width
    add ax, MENU_NAME_X + MENU_NAME_PAD
    mov bx, ax                  ; BX = running left edge

    mov si, [menu_set]
    or si, si
    jz .done
    call menu_es                ; ES = the set's segment (0 = ours)
    mov cx, [es:si + AM_COUNT]
    cmp cx, MENU_APPMAX         ; never trust a package's count (SPEC 12.2)
    jbe .cnt_ok
    mov cx, MENU_APPMAX
.cnt_ok:
    jcxz .done
    add si, AM_LIST
.cell:
    push cx
    push si
    mov si, [es:si + AMENU_TITLE]
    call font_width_x           ; AX = title width
    pop si
    add ax, MENU_TITLE_PAD      ; AX = cell width
    mov dx, bx
    add dx, ax
    cmp dx, [vid_clk_hx]        ; would it reach the clock's band (12.1)?
    ja .full                    ; out of bar: stop here, never clip a cell -
                                ; and never skip one and keep laying out the
                                ; rest, or a cell's bar index would stop
                                ; matching its index in the app's set and
                                ; ui_dispatch's `dec ah` would hand the
                                ; handler the wrong menu (SPEC.md 12/12.2)
    mov ax, [es:si + AMENU_TITLE]
    mov [di + MB_TITLE], ax
    mov ax, [es:si + AMENU_ITEMS]
    mov [di + MB_ITEMS], ax
    mov ax, [es:si + AMENU_NITEM]
    mov [di + MB_NITEM], ax
    mov ax, [menu_seg]          ; ...and these are the owner's
    mov [di + MB_SEG], ax
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
    call menu_ab_cell           ; the NAME cell goes LAST (DI = next slot)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_ab_cell - turn the app-name label into a bar cell, if the front window
;                declared an About handler (SPEC.md 12.2)
; in:  DI = the next free menu_bar slot, [menu_namep]/[menu_seg] = the label
; out: [menu_abcell] = the cell's bar index, 0 = no cell (all regs preserved)
;
; It is APPENDED, after the application's own menus, and that is the whole
; trick: hit-testing and drawing both walk menu_bar by XL/XR and TX, so where
; a cell sits in the array has nothing to do with where it sits on the bar -
; while the app's menus keep bar index == set index + 1 and ui_dispatch's
; `dec ah` needs no adjustment. Its rect is the label's own, padded 6px
; either side like a title cell, so it highlights the way every other cell
; does and still clears the first menu by MENU_NAME_PAD - 6.
;
; BOTH of its strings are copied into menu_abstr, which is why its MB_SEG is
; 0 even when the bar belongs to a package: the item is the whole buffer
; ('About Paint') and the title is menu_abstr+MENU_ABPFX_LEN, the same bytes
; from the name onward. Reading the title out of the package's segment
; instead would work, but then one cell would need two segments.
;
; Locator never gets one: the kernel's own About is CMD_ABOUT, in the System
; menu, which is cell 0 of every application including this one.
; -----------------------------------------------------------------------------
menu_ab_cell:
    push ax
    push bx
    push si
    push di
    push es
    mov word [menu_abcell], 0
    mov bx, [menu_win]
    or bx, bx
    jz .out                     ; Locator: the System menu is its About
    call wm_about_get           ; AX = the window's handler offset, 0 = none
    or ax, ax
    jz .out

    push di                     ; --- 'About ' + the name, into menu_abstr ---
    mov di, menu_abstr
    push cs
    pop es                      ; the prefix is ours...
    mov si, menu_s_abpfx
    call menu_ab_cat
    call menu_es                ; ...the name is the owner's (SPEC.md 12.2)
    mov si, [menu_namep]
    call menu_ab_cat
    mov byte [di], 0
    pop di
    mov word [menu_abitems], menu_abstr

    push cs                     ; --- the cell, over the label's own rect ---
    pop es                      ; the copy is kernel data now, so measure it
    mov si, menu_abstr + MENU_ABPFX_LEN
    call font_width_x           ; AX = the label's pixel width
    mov [di + MB_TITLE], si
    mov word [di + MB_ITEMS], menu_abitems
    mov word [di + MB_NITEM], 1
    mov word [di + MB_XL], MENU_NAME_X - 6
    add ax, MENU_NAME_X + 5
    mov [di + MB_XR], ax
    mov word [di + MB_TX], MENU_NAME_X
    mov word [di + MB_SEG], 0   ; both its strings are in menu_abstr
    mov ax, [menu_nbar]
    mov [menu_abcell], ax
    inc word [menu_nbar]
.out:
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_ab_cat - append a NUL string to a buffer (internal to menu_ab_cell)
; in:  ES:SI = source, DI = destination (in the kernel's own segment)
; out: DI = one past the last byte copied (no NUL written)
; clobbers: DI (SI preserved)
;
; At most MENU_MAXCH bytes are taken from any one source: menu_abstr is a
; fixed size and the name is a PACKAGE's string, so its length is not the
; kernel's to trust (SPEC.md 12.2).
; -----------------------------------------------------------------------------
menu_ab_cat:
    push ax
    push cx
    push si
    mov cx, MENU_MAXCH
.next:
    mov al, [es:si]
    or al, al
    jz .done
    mov [di], al
    inc si
    inc di
    loop .next
.done:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_es - point ES at the segment the active menu set's data lives in
; in:  nothing ([menu_seg] is the answer, 0 = the kernel's own)
; out: ES; nothing else (flags untouched)
;
; Every string and every word of an app menu set is an offset in the OWNING
; package's segment (SPEC.md 12.2/20.1). One window owns the bar at a time,
; so one word answers for the whole set - there is no per-entry segment in
; menu_bar, and there does not need to be.
; -----------------------------------------------------------------------------
menu_es:
    push cs
    pop es
    cmp word [menu_seg], 0
    je .out
    mov es, [menu_seg]
.out:
    ret

; -----------------------------------------------------------------------------
; menu_des - point ES at the segment the DROPPED menu's items live in
; in:  nothing ([menu_dseg], set by menu_track from the cell / menu_popup)
; out: ES; nothing else (flags untouched)
; -----------------------------------------------------------------------------
menu_des:
    push cs
    pop es
    cmp word [menu_dseg], 0
    je .out
    mov es, [menu_dseg]
.out:
    ret

; -----------------------------------------------------------------------------
; menu_draw_bar - bring the menu bar up to date: the white field, the black
;                 rule, the active application's name and every live cell's
;                 title when [menu_bdirty] says they are owed - and the clock,
;                 which is on its own cadence, always.
; in:  nothing (caller holds the gfx lock)
; out: nothing
; clobbers: nothing (flags only)
;
; **Called on every window operation and usually with nothing to do.** Unlike
; the dock there is no useful per-cell granularity: the bar's contents ARE the
; active application's, so either the owner changed and every cell did, or the
; owner did not and none of them did. menu_relayout is the single rebuild
; point (SPEC.md 12) and sets the flag; menu_check just above may reach it.
;
; The other thing that sets the flag is somebody having drawn OVER the bar,
; which only two things can do - a fullscreen window (SPEC.md 11.2) and a
; dropped menu whose save-under was refused - so wm_paint_all and menu_track
; set it rather than reason about it. The clock is outside all of this:
; menu_draw_clock fills its own cell and is what the Clock task calls direct.
; -----------------------------------------------------------------------------
menu_draw_bar:
    push ax
    push bx
    push cx
    push dx
    push si

    call wm_fs_vis              ; a visible fullscreen window is OVER the bar
    jc .out                     ; (SPEC.md 11.2), so there is nothing here to
                                ; draw on. The guard is here and not at the
                                ; callers because "the bar is not on screen"
                                ; is a fact about the screen, not about the
                                ; path that reached it - fdlg_close was the
                                ; site that proved that, painting the bar
                                ; across a fullscreen player's content every
                                ; time its Open dialog was cancelled.
                                ; [menu_bdirty] is deliberately left ALONE:
                                ; the bar still owes a redraw, and it gets
                                ; one when fullscreen ends
    call menu_check             ; the owner may have closed since (SPEC 12)
    cmp byte [menu_bdirty], 0
    je .clock
    mov byte [menu_bdirty], 0

    mov byte [gfx_color], CWHITE
    xor ax, ax                  ; white field, rows 0..MBAR_H-2
    xor bx, bx
    mov cx, [vid_wm1]
    mov dx, MBAR_H - 2
    call gfx_fill

    mov byte [gfx_color], CBLACK
    xor ax, ax                  ; the black rule along the bottom
    mov bx, [vid_wm1]
    mov dx, MBAR_H - 1
    call gfx_hline

    cmp word [menu_abcell], 0   ; the name is a CELL when the application
    jne .cells                  ; declared an About (SPEC.md 12.2), and the
                                ; loop below draws it at the same pen x -
                                ; drawing it here too would double-strike it
    push es                     ; the name and every cell title belong to the
    call menu_es                ; owner (SPEC.md 12.2/20.1)
    mov si, [menu_namep]        ; the active application's name (SPEC 12)
    mov cx, MENU_NAME_X
    mov dx, MENU_TEXT_Y
    call font_str_x
    pop es

.cells:

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
    push es
    push cs
    pop es                      ; this cell's own segment (SPEC.md 12.2):
    cmp word [si + MB_SEG], 0   ; cell 0's title is never drawn (it is the
    je .tseg                    ; logo), but the rule is the same for all
    mov es, [si + MB_SEG]
.tseg:
    mov si, ax
    call font_str_x
    pop es
    pop si
    jmp .next
.glyph:
    mov cx, [si + MB_TX]
    call menu_logo_glyph
.next:
    pop cx
    add si, MB_ENTSZ
    loop .title

.clock:
    call menu_draw_clock        ; the right end of the bar (SPEC.md 12.1)
.out:
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
    mov ax, [vid_clk_hx]
    xor bx, bx
    mov cx, [vid_wm1]
    mov dx, MBAR_H - 2
    call gfx_fill

    call clk_fmt                ; SI = the line in its live form (SPEC.md 37)
    call font_width             ; AX = its width - right-align it, since the
    mov cx, [vid_wm8]           ; form (and so the width) can change
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
    mov ax, [si + MB_SEG]       ; the dropped menu's segment: this CELL's,
    mov [menu_dseg], ax         ; not the bar owner's (SPEC.md 12.2)
    mov cx, [si + MB_NITEM]
    cmp cx, [vid_popmax]        ; the height clamp a bar menu never had: a
    jbe .cnt_ok                 ; package's set is not trusted for count any
    mov cx, [vid_popmax]        ; more than for width (SPEC.md 12.2/2.2)
.cnt_ok:
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
    cmp ax, [vid_popmax]
    jbe .cnt_ok                 ; never trust a caller's count - the rect has
    mov ax, [vid_popmax]        ; to fit on the screen (menu_relayout's rule),
                                ; and how many fit depends on the adapter:
                                ; 16 at 480 or 348 rows, 11 at CGA's 200
.cnt_ok:
    mov [menu_cnt], ax
    mov [menu_iptr], bx
    mov word [menu_dseg], 0     ; every popup descriptor in the tree is the
                                ; kernel's own (SPEC.md 12.4)

    ; --- width, and the left shift off the right edge ------------------------
    push cx                     ; the anchor: menu_widest wants CX = count
    mov cx, ax
    call menu_widest            ; AX = widest item text, px (BX = the array)
    add ax, 16                  ; AX = menu width, and stays there
    pop cx                      ; CX = anchor x again

    mov bx, cx                  ; BX = candidate x1
    mov si, [vid_w]
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
    mov cx, [vid_h]
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
; task and both are driven by a held button - so the one save-under buffer
; needs no allocator of its own. It IS a heap claim (SPEC.md 50.2), and it is
; claimed HERE and released before this routine returns - MENU_SAVE_KB for
; exactly as long as a menu is on screen, bounded at build time by the clamps
; below and asserted in kernel.asm. It used to be taken once at menu_init and
; held for the session, which is 20KB a 128KB machine was paying for at every
; instant it was not looking at a menu.
;
; **The claim can now fail**, where before it could only fail at boot. That
; needs no new failure path: `[menu_sseg]` = 0 already means "no buffer", the
; save is skipped and the restore repaints instead - slower, and correct. A
; machine tight enough to hit it gets a flash when a menu closes rather than a
; menu it cannot open.
;
; That repaint is a DAMAGE RECT, not the screen: the menu covered the rect it
; drew and nothing else - which is precisely the rect gfx_save would have
; captured - so wm_paint_dmg over it puts back the windows it occluded and
; leaves the rest alone. It leaves the menu BAR alone too - but only because
; the repaint path DISARMS [menu_bdirty] before calling wm_paint_dmg. Without
; that it did not, and menu_title_xor's self-inverting highlight stopped being
; self-inverting; see the comment at .repaint.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; menu_save_kb - how big a save-under does THIS menu need? (SPEC.md 12.4/50.2)
;
; in:  [menu_x1]/[menu_x2]/[menu_y1]/[menu_y2], set by menu_drop's caller
; out: AX = KB to claim, never 0 (mem_claim refuses that)
; clobbers: AX (the output), flags
;
; gfx_save costs planes x rows x (byte span + 1), which is exactly what the
; build-time assert in kernel.asm bounds - and the claim used to BE that
; bound: a flat MENU_SAVE_KB, sized for eleven items at the widest a package
; may declare, taken whole for a four-item pull-down.
;
; That is a fifth of a 128KB machine's heap for the duration of a menu, and
; on one with a sound card it simply could not be had: 55K of heap, 38K held
; by the driver, 20K asked for. The save-under was refused on EVERY menu
; there, so every menu repainted instead of restoring - the slow path, taken
; permanently, on the machine least able to afford it.
;
; Sized from the rect, Locator's File menu wants ~4KB on VGA and ~1KB on
; Hercules, where [vid_planes] is 1 and three quarters of the fixed figure
; was never going to be written to at all.
;
; The +2 is the assert's own slack: +1 makes a span a count, and +1 covers
; the rect starting mid-byte, which is the same allowance MENU_MAXW/8 + 2
; makes. Every factor is clamped (MENU_MAXW, MENU_POPMAX, [vid_popmax]), so
; the multiplies cannot leave 16 bits - the assert is the proof of that, and
; it still holds as the ceiling this can never exceed.
; -----------------------------------------------------------------------------
menu_save_kb:
    push bx
    push cx
    push dx
    mov ax, [menu_x2]
    mov cl, 3
    shr ax, cl                  ; the rect's last byte column...
    mov bx, [menu_x1]
    shr bx, cl                  ; ...and its first
    sub ax, bx
    add ax, 2                   ; span -> count, plus the misalignment byte
    mov bx, [menu_y2]
    sub bx, [menu_y1]
    inc bx                      ; BX = rows
    mul bx                      ; AX = bytes for one plane
    mov bx, [vid_planes_w]      ; ...times the planes this adapter HAS: 4 on
    mul bx                      ; VGA, 1 on Hercules and CGA (SPEC.md 39.2).
                                ; The _w twin, because vid_planes itself is a
                                ; BYTE and a word read of it takes the low
                                ; half of this very variable with it - 0x0404,
                                ; a thousand planes, and a claim bigger than
                                ; the fixed one it replaced
    add ax, 1023
    mov cl, 10
    shr ax, cl                  ; ...as KB, rounded UP
    or ax, ax
    jnz .out
    inc ax                      ; a 0 KB claim is refused, and a rect this
.out:                           ; small still needs somewhere to go
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
menu_drop:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- claim the save-under for as long as this menu is down ----------------
    mov word [menu_sseg], 0
    call menu_save_kb           ; AX = what THIS menu's rect needs, not the
                                ; worst case any menu could
    mov bx, MEM_K_SAVE
    call mem_claim
    jc .noclaim                 ; refused: the restore repaints instead
    mov [menu_sseg], dx
.noclaim:

    ; --- save the pixels under the menu to the claimed buffer -----------------
    mov ax, [menu_sseg]
    or ax, ax
    jz .nosave                  ; no buffer: the restore repaints instead
    mov es, ax
    xor di, di
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_save
.nosave:

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
    push es
.item:
    push cx
    call menu_des               ; the item array AND the strings it points at
    mov si, [es:bx]             ; are that cell's (SPEC.md 12.2/20.1); ES is
                                ; re-established per pass because menu_trunc
                                ; may repoint it at the kernel's copy
    call gfx_pen_live
    cmp byte [es:si], MENU_DIS  ; disabled: the disabled pen, and skip the
    jne .live                   ; marker byte. gfx_pen_dis and not a bare
    inc si                      ; CDGRAY store, because on a mono adapter
    call gfx_pen_dis            ; grey text IS black text - the flag is what
.live:                          ; buys the checkerboard (SPEC.md 47)
    call menu_trunc             ; ES:SI = the item, truncated to MENU_MAXCH,
    mov cx, [menu_x1]           ; so no glyph lands outside the saved rect
    add cx, 8
    call font_str_x
    call gfx_pen_live
    pop cx
    add bx, 2
    add dx, MENU_ITEM_H
    loop .item
    pop es
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
    mov ax, [menu_sseg]         ; restore erases the menu and any highlight
    or ax, ax
    jz .repaint
    mov es, ax
    xor si, si
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_restore
    jmp short .sel
.repaint:                       ; no buffer: put back what the MENU covered,
    mov byte [menu_bdirty], 0   ; ...but FIRST disarm the bar, because the
                                ; title above is XOR-HIGHLIGHTED right now and
                                ; menu_title_xor undoes it by XORing the same
                                ; pixels again. wm_paint_dmg repaints the
                                ; chrome (SPEC.md 11.91) and menu_draw_bar
                                ; redraws the whole bar whenever this flag is
                                ; set - which menu_force below sets at the end
                                ; of EVERY interaction. So interaction N's
                                ; flag was being spent by interaction N+1's
                                ; repaint, landing between the two XORs: the
                                ; bar came back clean, and the second XOR put
                                ; a highlight ON and left it there. Nothing is
                                ; lost by clearing it - menu_force re-arms it
                                ; a dozen lines down, so the redraw is only
                                ; DEFERRED past the window where it does harm,
                                ; and the menu rect starts at MBAR_H so the
                                ; bar was never damaged in the first place.
                                ;
                                ; Only the refused path can do this, which is
                                ; why it took a machine small enough to refuse
                                ; the claim to see it: on 128KB the save-under
                                ; is refused every time, so this WAS the
                                ; normal path there and every menu after the
                                ; first left its title inverted.
    mov ax, [menu_x1]           ; which is the rect it drew and nothing else -
    mov bx, [menu_y1]           ; gfx_save captures exactly this, there is no
    mov cx, [menu_x2]           ; shadow outside it, and everything beyond it
    mov dx, [menu_y2]           ; is still correct on screen (SPEC.md 11.91)
    call wm_paint_dmg
    call gfx_flush
.sel:
    mov dx, [menu_sseg]         ; the menu is off the screen: hand the buffer
    or dx, dx                   ; back rather than hold 20KB until the next
    jz .nofree                  ; one (SPEC.md 12.4/50.2)
    mov bx, MEM_K_SAVE
    call mem_free
    mov word [menu_sseg], 0
.nofree:
    call menu_force             ; the cell highlight is XOR and undoes itself,
                                ; but the restore path above is a save-under
                                ; that can be refused - one bar redraw per
                                ; menu interaction is not worth reasoning about
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
    call menu_item_dis          ; a disabled item is not hoverable, which is
    jc .none                    ; also what makes it unselectable: the whole
    pop cx                      ; of the feature is this one call plus the
    ret                         ; grey in menu_drop
.none:
    mov ax, 0xFFFF
    pop cx
    ret

; -----------------------------------------------------------------------------
; menu_item_dis - is item AX of the dropped menu disabled? (SPEC.md 12.2)
; in:  AX = item index (< [menu_cnt])
; out: CF = 1 if its string starts with MENU_DIS
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
menu_item_dis:
    push bx
    push si
    push es
    mov bx, ax
    add bx, bx
    add bx, [menu_iptr]
    call menu_des               ; ES = the dropped cell's segment
    mov si, [es:bx]
    cmp byte [es:si], MENU_DIS
    jne .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop es
    pop si
    pop bx
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
; menu_trunc - stage an item string, truncated to MENU_MAXCH glyphs
;
; in:  SI = NUL string
; out: SI = a NUL string of at most MENU_MAXCH characters (the original if it
;      already fits, else a copy in menu_tbuf)
; clobbers: SI (the output) and the flags
;
; The other half of the width clamp. menu_widest narrows the RECT; without
; this the items would still be drawn full length, and font_char clips only
; whole cells against the screen edge - so the overhang would land outside
; the rect gfx_save captured and gfx_restore would never erase it. Permanent
; garbage on the desktop after one menu use.
; -----------------------------------------------------------------------------
menu_trunc:
    push ax
    push cx
    push di
    mov cx, MENU_MAXCH
    mov di, si                  ; does it already fit?
.len:
    cmp byte [es:di], 0
    je .fits
    inc di
    loop .len
    cmp byte [es:di], 0         ; exactly MENU_MAXCH characters also fits
    je .fits
    mov cx, MENU_MAXCH          ; no: copy the prefix into our own buffer.
    mov di, menu_tbuf           ; The source is ES:SI (the owner's) and the
.copy:                          ; destination is kernel .bss, so the store
    mov al, [es:si]             ; goes through CS - which IS KERNEL_SEG here,
    inc si                      ; the gfx_nextrow idiom of SPEC.md 39.3
    mov [cs:di], al
    inc di
    loop .copy
    mov byte [cs:di], 0
    mov si, menu_tbuf
    push cs
    pop es                      ; the copy is ours: so is the segment now
.fits:
    pop di
    pop cx
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
    push es
    call menu_des               ; the dropped menu's segment (SPEC.md 12.2)
    xor dx, dx                  ; running max
    jcxz .done                  ; `loop` would wrap CX to 0xFFFF
.next:
    mov si, [es:bx]
    cmp byte [es:si], MENU_DIS  ; the marker byte is not a glyph
    jne .meas
    inc si
.meas:
    call font_width_x           ; AX = 8 * strlen
    cmp ax, dx
    jbe .keep
    mov dx, ax
.keep:
    add bx, 2
    loop .next
.done:
    pop es
    cmp dx, MENU_MAXW - 16      ; the clamp both callers need (above): the
    jbe .clamped                ; save-under's width factor, bounded here
    mov dx, MENU_MAXW - 16      ; once instead of at two call sites
.clamped:
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
menu_s_closex   db MENU_DIS, 'Close Window', 0  ; ...with nothing to close;
                                ; ui_loc_gate points the item at one or the
                                ; other before the menu can drop (SPEC.md 47)
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
menu_s_abpfx db 'About ', 0     ; + the app's name -> menu_abstr (12.2)

section .bss

menu_win:   resw 1              ; window owning the bar, 0 = Locator (12)
menu_set:   resw 1              ; its app menu set (SPEC.md 12.2)
menu_namep: resw 1              ; the name string drawn at MENU_NAME_X
menu_bar:   resb 84             ; MENU_BARMAX x MB_ENTSZ (6 x 14; a literal
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
menu_seg:   resw 1              ; the segment [menu_set] and the app's own
                                ; strings live in; 0 = the kernel's own
                                ; (Locator, and any built-in window)
menu_dseg:  resw 1              ; ...and the segment of the menu being
                                ; DROPPED, which is that cell's MB_SEG: 0
                                ; for the System menu even under a package
menu_bdirty: resb 1             ; the bar's field, name and cells are owed a
                                ; redraw. Set by menu_relayout and by anyone
                                ; who drew over the bar (menu_force)
menu_sseg:  resw 1              ; the save-under claim's segment, 0 = none
                                ; (menu_drop then repaints instead)
menu_tbuf:  resb MENU_MAXCH + 1 ; menu_trunc's staging buffer
menu_abcell: resw 1             ; bar index of the app-NAME cell, 0 = none -
                                ; cell 0 is the System menu, so 0 is free to
                                ; mean "this application declared no About"
menu_abitems: resw 1            ; the name cell's item array: one entry, and
                                ; it points at menu_abstr below
menu_abstr: resb MENU_ABPFX_LEN + MENU_MAXCH + 1
                                ; 'About ' + the app's name + NUL. The cell's
                                ; TITLE is menu_abstr+MENU_ABPFX_LEN - the
                                ; same bytes, read from the name onward - so
                                ; both strings the cell needs are KERNEL data
                                ; and its MB_SEG is 0 even under a package

section .text
