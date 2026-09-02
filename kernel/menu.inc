; =============================================================================
; os8088 - menu.inc
;
; The menu bar and pull-down menus (SPEC.md 12). The bar is three zones:
; the System menu (an 11x11 hand-authored chip silhouette - the os8088
; logo, and the one place bitmap data is hand-made), which is cell 0 in
; EVERY application; the ACTIVE APPLICATION'S NAME, which is a real menu
; cell for every application (SPEC.md 12.7 - 'About <Name>' when the window
; registered a handler, and 'Close' always, at the bottom) and a bare label
; for Locator alone; and that application's own menus (SPEC.md 12.2).
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
; persistent and what is a transient VRAM overlay is subtle, and two
; copies of it would drift.
;
; The right end of the bar is the system clock (SPEC.md 12.1): drawn by
; menu_draw_clock, repainted once a second by the UI task, and - because
; ui.inc hit-tests its cell before menu_track ever sees the click - a
; shortcut to the Control Panel's Date/Time page rather than a menu title.
;
; **THE BAR IS THREE SEGMENTS AND EACH ANSWERS FOR ITSELF** (SPEC.md 12.9).
; The logo never changes, the menus change when the owner does, the clock
; changes once a minute, and SPEC.md 12.8's file-activity widget borrows the
; right end of the menus. Those are four cadences and there used to be one
; flag, so any of them moving white-filled the whole bar and put every glyph
; back - tens of milliseconds of a BLANK bar on a 4.77MHz machine, on every
; window operation. Now [menu_bovr] owns the static furniture and is the only
; thing here that erases, the menus are composed into menu_bcell and emitted
; as the one font_run span that differs from what is on the glass, and the
; clock has a check word of its own.
; =============================================================================

; --- the kernel's own commands (SPEC.md 12.3; reconstructed by ui.inc) -------
; The bar no longer carries these: ui_dispatch rebuilds cmd = base + item
; from ui_loc_base for the System menu and for Locator's own menus, which
; keeps the "each menu's commands stay consecutive from its base"
; arithmetic exactly where it always was, just off the data path.
; The ids are contiguous WITHIN a menu, because that is what makes
; ui_dispatch's `ui_loc_base[cell] + item` work; they are internal constants
; and renumber freely whenever a menu gains an item, which SPEC.md 12.3.1
; has just made them do.
CMD_ABOUT   equ 1               ; --- System (cell 0, every application) ---
CMD_CTRL    equ 2
CMD_TASKS   equ 3
CMD_SEP     equ 4               ; the RULE above Restart. It is a MENU_DIS
                                ; item, so menu_hover can never land on it -
                                ; and it needs no handler either, because
                                ; ui_cmd is a compare ladder and an id it
                                ; does not match falls out of the bottom and
                                ; returns. The id exists only so that
                                ; CMD_REBOOT below it still equals base+item
CMD_REBOOT  equ 5
CMD_CLOSE   equ 6               ; --- Locator: File ---
CMD_TIMER   equ 7               ; --- Locator: Builtins (SPEC.md 12.3.1) ---
CMD_BOUNCE  equ 8
CMD_FILES   equ 9

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
MENU_APPMAX equ 5               ; app menus the bar can host. It was 4 until
                                ; SPEC.md 22.12 split the Disk window's File
                                ; menu and gave Cut/Copy/Paste an Edit menu -
                                ; a WIDENING, so no package that assembled
                                ; against 4 changed, and the clamp below is
                                ; still what an untrusted count meets.
                                ; Nothing had to be re-measured for it: the
                                ; layout is bounded per CELL by [vid_clk_hx]
                                ; and never by the count, so a fifth title
                                ; that would not fit simply is not laid out
MENU_BARMAX equ MENU_APPMAX + 2 ; + the System cell, which is always cell 0,
                                ; + the app-NAME cell, which exists for every
                                ; application (SPEC.md 12.7) and is APPENDED
                                ; last so the app's own menus keep bar
                                ; index = set index + 1
MENU_ABMAX  equ 2               ; items the name cell can carry: About, Close
MENU_ABPFX_LEN equ 6            ; 'About ' - menu_abstr's name copy starts
                                ; here, which is why one buffer serves both
                                ; the cell's TITLE and its single ITEM

MENU_SYS_XR    equ 29           ; System cell: x 0..29, glyph at MENU_SYS_TX
MENU_SYS_TX    equ 10
MENU_NAME_X    equ 40           ; app-name label pen x
MENU_NAME_PAD  equ 16           ; gap after the name, before the first menu
MENU_TITLE_PAD equ 16           ; per-title cell padding (8px each side)

; --- the bar is THREE SEGMENTS, and each is redrawn on its own (SPEC.md 12.9)
; The logo never changes, the menus change when the owner does, the clock
; changes once a minute, and the file-activity widget (SPEC.md 12.8) borrows
; the right end of the menus segment. Redrawing any of them used to redraw
; all of them - one white fill across the whole bar and then every glyph put
; back, which on a 4.77MHz machine is tens of milliseconds of a BLANK bar
; and is the double-draw flash of PERFORMANCE.md Part 1 in its plainest form.
;
;   x 0 .. MENU_TXT_X0-1     the logo. Static: drawn only when something
;                            painted OVER the bar ([menu_bovr]).
;   x MENU_TXT_X0 .. hx-1    the menus: the app name and every cell title,
;                            composed into menu_bcell and emitted as the
;                            font_run span(s) that differ from what is already
;                            on the glass - AND, in the free gap past the last
;                            title, the toast strip when it fits there
;                            (SPEC.md 59.9).
;   x [vid_clk_hx] .. w-1    the clock - and the toast strip when it does NOT
;                            fit that gap (SPEC.md 59.8), taking the LEFT end
;                            of this field in inverse video on the same check
;                            word. It is only as wide as its message, so what
;                            is left of the field goes on showing the time.
;
; **THE TOAST HAS TWO HOMES AND ONE PREFERENCE** (SPEC.md 59.9), and both of
; them are corrections of the one before. It began at the right end of the
; MENUS segment, where a long enough message covered the frontmost
; application's own menu titles: the strip sat on File, Edit and View and
; menu_bput's clamp dropped them, which reads as an application losing its
; menus rather than as a message arriving. 59.8 moved it to the CLOCK, which
; is the right neighbour to displace for a reason about kind rather than room -
; it is the one thing on the bar that is never a CONTROL, nothing in that field
; can be clicked into except SPEC.md 31.5's Date/Time shortcut, it says the
; same thing again a second later, and it is where a Macintosh puts this kind
; of message.
;
; 59.9 adds the placement that has no victim at all. The menus end wherever
; the active application's last title ends, and everything from there to
; [vid_clk_hx] is blank white - so a message that FITS between them is
; centred in that gap and covers neither a menu title nor a clock cell. Only
; when it will not fit does it fall back to 59.8's clock field and cost the
; date. So the width the messages are written against is still the clock's -
; the field is 25 cells on every screen this runs on, against the menus
; segment's 50, which is what TOAST_MAX = 23 is - and the gap is a bonus
; rather than a constraint, because a message may never depend on how many
; menus the application in front happens to have.
;
; MENU_TXT_X0 is a glyph-cell boundary and so is [vid_clk_hx], so a segment
; is a whole number of 8px cells - which is what earns font_run's single-pass
; path (SPEC.md 6.1) on the two mono adapters, where a cell row is one store
; and the run is never momentarily blank. Everything the bar lays out is
; therefore snapped to 8 as well: MENU_NAME_X, MENU_TITLE_PAD's half, and
; the name cell's own edges.
MENU_TXT_X0 equ 32              ; the menus segment's left edge (8-aligned,
                                ; and clear of MENU_SYS_XR = 29)
MENU_BCMAX  equ 62              ; cells in it, worst case: Hercules is 720
                                ; wide, so [vid_clk_hx] = (720-206) & ~7 =
                                ; 512 and the segment is (512-32)/8 = 60.
                                ; menu_bar_text clamps to the LIVE count, so
                                ; this only has to be big enough

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
MENU_CLK_HX equ (MENU_CLK_X - 6) & 0FFF8h   ; 432: hit band left edge, and the
                                            ; left edge of the clock SEGMENT -
                                            ; floored to a glyph cell, so the
                                            ; field is a whole number of them
MENU_CLKC   equ 26              ; menu_clkbuf: the field is 25 cells on both
                                ; screen widths ((w-8 - (w-206 & ~7)) / 8),
                                ; and the longest form is 24 glyphs, so the
                                ; pad is never negative. + the NUL

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
    call menu_force             ; ...and the furniture has never been drawn
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
; menu_force - the whole bar was PAINTED OVER and owes every segment
; in:  nothing
; out: nothing (all registers preserved)
;
; For callers that drew over the bar rather than changed what belongs on it -
; wm_paint_all's dither and a fullscreen window coming down (SPEC.md 11.2).
; This is the only thing that redraws the STATIC furniture (the black rule
; and the logo glyph), because those are the only two ways they can be lost:
; nothing else in the machine ever draws in the bar's own rows.
;
; A caller that overdrew only PART of the bar wants menu_inval instead -
; forcing the whole bar for an 88px widget is what made the file-activity
; progress bar (SPEC.md 12.8) redraw the menus, the logo and the clock every
; time it finished.
; -----------------------------------------------------------------------------
menu_force:
    mov byte [menu_bovr], 1
    mov byte [menu_bdirty], 1
    ret

; -----------------------------------------------------------------------------
; menu_inval - forget what was drawn in x .. x2 of the bar (SPEC.md 12.9)
; in:  AX = x1, CX = x2 (inclusive, absolute screen x)
; out: nothing (all registers preserved)
;
; The drawn-state buffer is one byte per glyph cell, so the range is rounded
; OUTWARD to whole cells - always safe, since a forgotten cell costs one
; redrawn character and a remembered one costs a stale glyph. Clamped to the
; menus segment: a caller whose rect reaches the logo or the clock is asking
; for something this cannot express and should call menu_force.
; -----------------------------------------------------------------------------
menu_inval:
    push ax
    push bx
    push cx
    push dx
    mov byte [menu_bdirty], 1   ; ...so menu_draw_bar's text pass runs at all
    sub ax, MENU_TXT_X0
    jns .lo
    xor ax, ax                  ; left of the segment: from its start
.lo:
    sub cx, MENU_TXT_X0
    js .out                     ; wholly left of it: nothing of ours
    mov dx, cx
    mov cl, 3
    shr ax, cl                  ; AX = first cell, rounded DOWN
    xchg ax, dx
    shr ax, cl                  ; ...and the cell x2 lands IN, which is the
    xchg ax, dx                 ; outward rounding at that end
    cmp ax, MENU_BCMAX
    jae .out
    cmp dx, MENU_BCMAX
    jb .hi
    mov dx, MENU_BCMAX-1
.hi:
    cmp ax, dx
    ja .out
    mov cx, dx
    sub cx, ax
    inc cx                      ; CX = cells to forget
    mov bx, menu_bcell
    add bx, ax
.z:
    mov byte [bx], 0            ; 0 is never composed: every byte that goes
    inc bx                      ; into this buffer is a printable one, so it
    loop .z                     ; can never match and the cell is redrawn
.out:
    pop dx
    pop cx
    pop bx
    pop ax
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
    mov byte [fm_fchk], 1       ; the active application changed, so a Disk
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
                                ; window may have just gained the focus with
                                ; a refresh owing (SPEC.md 22.8). This is the
                                ; catch-all rather than the main path -
                                ; wm_raise spends the debt before it draws -
                                ; and it is here because THIS is where every
                                ; focus change passes, including the one no
                                ; raise reaches: losing the front window
                                ; promotes the window under it (menu_check)
    call menu_relayout
    stc
    ret
.same:
    clc
    ret

; -----------------------------------------------------------------------------
; menu_owner - who is the ACTIVE APPLICATION? (API 0x02B8, SPEC.md 12/12.6)
; in:  nothing
; out: BX = the window owning the menu bar, 0 = Locator. Every other
;      register, and FLAGS, preserved.
;
; The read half of menu_activate, and the answer to a question wm_top cannot
; be asked: clicking the bare desktop hands the bar to Locator (SPEC.md 12)
; and moves nothing in wm_zord, so the frontmost visible window is still
; whatever it was. A real-time package that must stop when the player walks
; away needs THIS one - apps/arkanoid (SPEC.md 44.8) pauses on it.
;
; Takes no lock and touches no VRAM, so a worker task may call it every
; frame, exactly like wm_top. It is a plain word read: [menu_win] is written
; only by menu_activate on the UI task, and the 8086 recognises interrupts
; only at instruction boundaries, so it cannot be torn.
; -----------------------------------------------------------------------------
menu_owner:
    mov bx, [menu_win]
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
; I_NAME (an accessory like Timer has no menu set at all), else the
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
    mov word [ss:di + MB_TITLE], 0 ; 0 = draw the logo glyph
    mov word [ss:di + MB_ITEMS], menu_items_logo
    mov word [ss:di + MB_NITEM], MENU_LOGO_N
    mov word [ss:di + MB_XL], 0
    mov word [ss:di + MB_XR], MENU_SYS_XR
    mov word [ss:di + MB_TX], MENU_SYS_TX
    mov word [ss:di + MB_SEG], 0   ; the System menu is the kernel's, always
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
    mov [ss:di + MB_TITLE], ax
    mov ax, [es:si + AMENU_ITEMS]
    mov [ss:di + MB_ITEMS], ax
    mov ax, [es:si + AMENU_NITEM]
    mov [ss:di + MB_NITEM], ax
    mov ax, [menu_seg]          ; ...and these are the owner's
    mov [ss:di + MB_SEG], ax
    mov [ss:di + MB_XL], bx
    mov ax, bx
    add ax, MENU_TITLE_PAD / 2  ; the text is centred - and 8-aligned, since
                                ; BX is and a glyph width is a multiple of 8,
                                ; so every cell after this one is too
    mov [ss:di + MB_TX], ax
    mov ax, dx
    dec ax
    mov [ss:di + MB_XR], ax        ; xr = left + width - 1
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
; menu_ab_cell - turn the app-name label into a bar cell (SPEC.md 12.7)
; in:  DI = the next free menu_bar slot, [menu_namep]/[menu_seg] = the label
; out: [menu_abcell] = the cell's bar index, 0 = no cell (Locator);
;      [menu_abclose] = the index of its Close item. All regs preserved.
;
; It is APPENDED, after the application's own menus, and that is the whole
; trick: hit-testing and drawing both walk menu_bar by XL/XR and TX, so where
; a cell sits in the array has nothing to do with where it sits on the bar -
; while the app's menus keep bar index == set index + 1 and ui_dispatch's
; `dec ah` needs no adjustment. Its rect is the label's own, padded 6px
; either side like a title cell, so it highlights the way every other cell
; does and still clears the first menu by MENU_NAME_PAD - 6.
;
; **The cell no longer depends on the About handler.** It used to exist only
; when wm_about_get answered non-zero, so most applications had a bare label
; where their name is and no way at all to quit from the bar. Now the whole
; condition is that an application owns the bar, and the handler decides only
; whether the cell has one item or two: 'About <Name>' if there is one, and
; 'Close' - the kernel's own, dispatched by ui_dispatch - at the bottom
; either way. WHICH index Close ended up at is recorded in [menu_abclose]
; rather than re-derived at dispatch time, or that would be a second place
; that has to agree with this one about what the cell looks like.
;
; menu_abstr is therefore built on every relayout and not only in the About
; case: the cell's TITLE is menu_abstr+MENU_ABPFX_LEN, the same bytes read
; from the name onward, which is why one buffer serves both and why the
; cell's MB_SEG is 0 even when the bar belongs to a package. Reading the
; title out of the package's segment instead would work, but then one cell
; would need two segments.
;
; Locator never gets one: the kernel's own About is CMD_ABOUT, in the System
; menu, which is cell 0 of every application including this one, and its
; Close Window is already an item of its own File menu (SPEC.md 12.3).
; -----------------------------------------------------------------------------
menu_ab_cell:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov word [menu_abcell], 0
    mov bx, [menu_win]
    or bx, bx
    jz .out                     ; Locator: see above

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

    mov bx, [menu_win]          ; --- the items: About if it is offered, then
    call wm_about_get           ; Close, which every application gets (12.7)
    mov cx, 1                   ; CX = item count, BX = Close's index
    xor bx, bx
    or ax, ax
    jz .noab
    mov word [menu_abitems], menu_abstr
    mov bx, 1
    mov cx, MENU_ABMAX
.noab:
    mov [menu_abclose], bl
    add bx, bx                  ; ...to a word offset
    mov word [menu_abitems + bx], menu_s_appclose

    push cs                     ; --- the cell, over the label's own rect ---
    pop es                      ; the copy is kernel data now, so measure it
    mov si, menu_abstr + MENU_ABPFX_LEN
    call font_width_x           ; AX = the label's pixel width
    mov [ss:di + MB_TITLE], si
    mov word [ss:di + MB_ITEMS], menu_abitems
    mov [ss:di + MB_NITEM], cx
    mov word [ss:di + MB_XL], MENU_NAME_X - MENU_TITLE_PAD / 2
    add ax, MENU_NAME_X + MENU_TITLE_PAD / 2 - 1
    mov [ss:di + MB_XR], ax     ; ...so this cell is 8-aligned and a whole
    mov word [ss:di + MB_TX], MENU_NAME_X   ; number of cells wide, like the
                                            ; app's own (SPEC.md 12.9)
    mov word [ss:di + MB_SEG], 0   ; every string it names is kernel data
    mov ax, [menu_nbar]
    mov [menu_abcell], ax
    inc word [menu_nbar]
.out:
    pop es
    pop di
    pop si
    pop cx
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
; menu_draw_clock fills its own cell and is what the UI task's own clock
; step calls direct - nothing to do with the Timer app (SPEC.md 14).
; -----------------------------------------------------------------------------
menu_draw_bar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call wm_fs_vis              ; a visible fullscreen window is OVER the bar
    jnc .live                   ; (SPEC.md 11.2), so there is nothing here to
    mov byte [menu_bovr], 1     ; draw on - AND our pixels are being destroyed
    jmp .out                    ; while it is up, which is the whole of how
                                ; wm_paint_all stopped needing menu_force: the
                                ; ONE thing that can cover the bar is seen
                                ; here, by the module that has to repair it,
                                ; rather than assumed by every caller that
                                ; repaints the screen for some other reason
.live:                          ;
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

%ifdef REDRAWFULL
    call menu_force             ; REFERENCE BUILD: every bar redraw is a full
%endif                          ; one, the behaviour this replaced
    cmp byte [menu_bovr], 0     ; --- segment 1: the static furniture -------
    je .txt                     ; only ever owed after an OVERDRAW
    call menu_furniture
    cmp byte [fpg_on], 0        ; ...and that fill took SPEC.md 12.8's widget
    je .txt                     ; with it, which since 12.8.3 can be up across
    call fpg_paint              ; a repaint at all. Its own pixels only - the
                                ; cells are already ours, the text pass below
                                ; keeps off them, and fpg_paint deliberately
                                ; does not call back into this routine

.txt:
    cmp byte [menu_bdirty], 0   ; --- segment 2: the menus ------------------
    je .clock
    mov byte [menu_bdirty], 0
    call menu_bar_text

.clock:
    call menu_draw_clock        ; --- segment 3: the clock (SPEC.md 12.1) ---
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_furniture - the black rule and the logo glyph, after an overdraw
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; **The only path in the machine that erases the menu bar**, and it is
; reached only from [menu_bovr] - somebody painted over us. Everything else
; that changes the bar changes TEXT, and text is put back by an opaque run
; that never blanks what it is replacing (SPEC.md 6.1/12.9).
;
; The two things drawn here are the two that cannot be recomputed from the
; owner: the rule along the bottom row and the 11x11 System logo, both
; identical in every application for the life of the machine. Drawing them
; on every relayout cost 121 gfx_pixel calls and a full-width hline to put
; back pixels that had not moved.
; -----------------------------------------------------------------------------
menu_furniture:
    push ax
    push bx
    push cx
    push dx
    push di
    mov byte [menu_bovr], 0

    call thm_bg                
    xor ax, ax                  ; white field, rows 0..MBAR_H-2
    xor bx, bx
    mov cx, [vid_pwm1]
    mov dx, MBAR_H - 2
    call gfx_fill

    call thm_ink                
    xor ax, ax                  ; the black rule along the bottom
    mov bx, [vid_pwm1]
    mov dx, MBAR_H - 1
    call gfx_hline

    mov cx, MENU_SYS_TX         ; the logo: cell 0's title is 0 because it is
    call menu_logo_glyph        ; a bitmap, and this is where it lives now

    mov di, menu_bcell          ; ...and the fill took every glyph with it,
    mov cx, MENU_BCMAX          ; so nothing that was drawn is still drawn
    xor al, al
.z:
    mov [di], al
    inc di
    loop .z
    mov word [menu_ckck], 0
    mov byte [menu_bdirty], 1

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_bar_text - the menus segment: compose it, diff it, emit what moved
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; The bar's text is composed into menu_bcell - ONE BYTE PER GLYPH CELL, the
; app name and every cell title at the x menu_layout gave them, spaces
; everywhere else - and menu_bcell is also the record of what is ON THE
; GLASS. So composing it IS the diff: the first and last cells that differ
; bound one font_run, and a bar whose owner has not changed emits nothing at
; all.
;
; That is the Task Manager's row check (SPEC.md 28) and Missile Command's
; status strip (SPEC.md 48.17) applied to the chrome, and the reason it is
; sound here is the same one: the bar is never covered (windows clamp to
; y >= MBAR_H), so what the last emit drew is still what is showing, and the
; only thing that can invalidate that is an overdraw - which is exactly what
; menu_force and menu_inval say.
;
; The padding IS the erase. A space is an opaque cell like any other, so a
; bar that got SHORTER blanks its tail in the same single pass that letters
; its head, and there is no fill anywhere in this path.
; -----------------------------------------------------------------------------
menu_bar_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, [vid_clk_hx]        ; the live segment, in whole cells - both
    sub ax, MENU_TXT_X0         ; ends are 8-aligned by construction
    mov cl, 3
    shr ax, cl
    cmp ax, MENU_BCMAX
    jbe .n
    mov ax, MENU_BCMAX
.n:
    cmp byte [fpg_on], 0        ; ...and the whole of it is the menus' UNLESS
    je .res                     ; SPEC.md 12.8's widget is up, when the last
    sub ax, FPG_CELLS           ; FPG_CELLS of it are ITS (SPEC.md 12.8.3).
    jns .res                    ; The widget is drawn straight onto them and
    xor ax, ax                  ; is composed into no buffer at all, so
.res:                           ; menu_bcell cannot see it and the diff cannot
    mov [menu_bn], ax           ; arbitrate: two painters, one strip of glass,
                                ; which is exactly what 59.1 exists to
                                ; prevent.
                                ;
                                ; Taking it off [menu_bn] rather than off the
                                ; toast alone is what SPEC.md 12.8.3 needed:
                                ; the widget now outlives the operation that
                                ; armed it, so a composition CAN happen while
                                ; it is up - launching an application from a
                                ; Disk window changes the bar's owner between
                                ; the package load and the document read, with
                                ; the machine frozen throughout - and the
                                ; padding below would have lettered spaces
                                ; straight through the icon and the box.
                                ; menu_bput already drops a cell at or past
                                ; this bound without advancing DI, and
                                ; menu_bpadc already clamps to it (59.7), so
                                ; one subtraction is the whole of it.
                                ;
                                ; The cells come back at fpg_finish, which
                                ; clears the flag and asks for one more
                                ; composition; what was under them is put back
                                ; by fpg_arm's menu_inval, taken when the
                                ; widget went up.
                                ;
                                ; The clamp at 0 is for a screen too narrow to
                                ; hold both - fpg_arm refuses such a screen,
                                ; so it cannot happen, but a negative cell
                                ; count here would be an unsigned bound
                                ; everywhere below

    mov word [menu_bfirst], 0FFFFh
    mov word [menu_blast], 0    ; SEEDED now that it is a maximum (menu_bput).
                                ; It needed none while it was assigned beside
                                ; [menu_bfirst] on every difference; 0 is
                                ; below every cell and is read only when
                                ; [menu_bfirst] says something differed at all
    xor di, di                  ; DI = cell index, 0 = x MENU_TXT_X0

    mov bx, [menu_abcell]       ; --- the application's name ----------------
    or bx, bx                   ; a CELL when the application declared an
    jz .bare                    ; About (SPEC.md 12.2/12.7), and it is
    call menu_bent              ; APPENDED last but positioned FIRST, so it
    jmp short .cells            ; is composed here to keep x order
.bare:
    mov ax, MENU_NAME_X         ; Locator: a bare label, same pen x
    call menu_bpad
    push es
    call menu_es                ; the name belongs to the owner (12.2/20.1)
    mov si, [menu_namep]
    call menu_bstr
    pop es

.cells:                         ; --- the app's own menus, left to right ----
    mov bx, 1                   ; cell 0 is the logo: a bitmap, no text
.cell:
    cmp bx, [menu_nbar]
    jae .tail
    cmp bx, [menu_abcell]
    je .next                    ; ...composed above
    call menu_bent
.next:
    inc bx
    jmp short .cell

.tail:
    lea ax, [di+1]              ; --- WHERE THE MENUS END (SPEC.md 59.9). DI is
    cmp ax, [menu_bn]           ; one past the last GLYPH, and a title's pen is
    jbe .bend                   ; MB_XL + 8 with MENU_TITLE_PAD split 8px each
    mov ax, [menu_bn]           ; side - so that cell is still inside the last
.bend:                          ; menu's BOX and the one after it is the first
    mov [menu_bend], ax         ; that no menu owns. Clamped, because the gap
                                ; arithmetic subtracts this from [menu_bn]

    mov ax, [menu_bn]           ; spaces to the clock: this is what erases
    call menu_bpadc             ; the menus of the app that just went away -
                                ; AND, on a toast's down edge, the strip below

    call toast_place            ; --- and the toast strip, if the gap those
    mov cx, [toast_scn]         ; spaces just laid down is wide enough for it
    jcxz .diff                  ; (SPEC.md 59.9); 0 = the clock's field has it
    mov di, [toast_scell]       ; and nothing here is owed
    mov al, ' ' | 0x80
    call menu_bput              ; the gap cell: menu_bemit white-fills its left
    dec cx                      ; 4px after the run, so both homes pad alike
    jcxz .strail
    mov si, toast_buf
.sch:
    mov al, [si]                ; ...the message, **BIT 7 SET ON EVERY CELL**
    inc si                      ; (SPEC.md 59.2): menu_bcell is the record of
    or al, 0x80                 ; what is on the glass, so a cell whose
    call menu_bput              ; CHARACTER did not change but whose HIGHLIGHT
    loop .sch                   ; did has to read as a difference to menu_bput -
.strail:                        ; and a space at the strip's edge is exactly
                                ; that cell. menu_bemit masks it off to draw.
                                ; NO trailing cell: SPEC.md 59.8.1's font
                                ; argument holds here too

.diff:
    mov bx, [menu_bfirst]       ; --- emit the span(s) that moved -------------
    cmp bx, 0FFFFh
    je .out                     ; nothing on the bar changed: no pixels
    mov dx, [menu_blast]

.piece:                         ; AT MOST THREE RUNS, and exactly one whenever
    mov al, [menu_bcell+bx]     ; the strip is not in this segment: the span is
    and al, 0x80                ; split wherever the inversion changes, and
    mov si, bx                  ; 59.9's strip is the only thing that inverts
.pscan:
    inc si
    cmp si, dx
    ja .pemit
    mov ah, [menu_bcell+si]
    and ah, 0x80
    cmp ah, al
    je .pscan
.pemit:
    push dx                     ; the piece is BX .. SI-1, and SI is the next
    push si                     ; piece's first cell
    dec si
    mov dx, si
    mov [menu_binv], al         ; SPEC.md 76.3: the STRIP, SAID rather than
    or al, al                   ; inferred - and AL already IS the inversion
    jz .pnorm                   ; bit, so this is the bit and not a literal
    call thm_rpair              ; INVERSE VIDEO, the one distinction that
                                ; survives SPEC.md 39.4's reduction intact
    jmp short .pgo              ; on both mono adapters
.pnorm:
    call thm_pair
                                
.pgo:
    call menu_bemit
    pop bx
    pop dx
    cmp bx, dx
    jbe .piece
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_bemit - letter cells BX..DX of the menus segment in one font_run
; in:  BX = first cell, DX = last cell (inclusive), AL = ink, AH = background
; out: nothing (all registers preserved)
;
; Split back out of menu_bar_text when SPEC.md 59.9 put the toast strip in the
; segment's free tail: the strip is inverse video, so a differing span that
; crosses one of its edges is more than one run with different colours rather
; than one. What is unchanged is the rest - the byte just past the run is
; banked and put back, because font_run wants ASCIIZ and menu_bcell carries one
; spare cell for exactly that.
;
; **BIT 7 IN A CELL IS THE INVERSION FLAG AND NOT A CHARACTER** (SPEC.md 59.2),
; so it is masked off across the run before the glyphs are drawn and put back
; after - without the mask font_char indexes past the 95 glyphs it has. That is
; exact rather than approximate: a run arriving here with CWHITE ink is wholly
; inside the strip by construction, its caller having split the span on that
; very bit, so every cell in it carries the bit and every cell in it gets it
; back.
; -----------------------------------------------------------------------------
menu_binv:  db 0                ; SPEC.md 76.3: menu_bar_text says whether the
                                ; piece it is handing over is the inverted one
menu_bemit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, ax                  ; the colours, out of the way of the x
    mov si, dx                  ; arithmetic below
    inc si
    mov al, [menu_bcell+si]
    mov byte [menu_bcell+si], 0
    push ax                     ; AL = the banked byte
    push bx
    push dx

    cmp byte [menu_binv], 0     ; IS THIS THE INVERTED RUN? (SPEC.md 76.3) It
    je .go                      ; used to be `cmp di, CWHITE` - the run's own
                                ; colours read as a proxy for the answer, which
                                ; the Dark theme swaps: the PLAIN run then
                                ; matches, bit 7 is never masked off, and
                                ; font_char indexes past the 95 glyphs it has
    mov si, bx
.m:
    and byte [menu_bcell+si], 0x7F
    inc si
    cmp si, dx
    jbe .m
.go:
    mov ax, bx
    mov cl, 3
    shl ax, cl
    add ax, MENU_TXT_X0
    mov cx, ax                  ; CX = the run's x (8-aligned, so font_run
    mov si, menu_bcell          ; takes its single-pass path on mono)
    add si, bx
    mov dx, MENU_TEXT_Y
    mov ax, di
    call font_run

    cmp byte [menu_binv], 0     ; the STRIP: take its first cell's left 4px
    je .nogap                   ; back to the ground, so the gap home pads
    mov ax, cx                  ; as the clock home does (SPEC.md 59.8.1).
    mov cx, ax                  ; Drawn and then half-erased rather than
    add cx, 3                   ; composed as two halves, because menu_bcell
    push bx                     ; is a record of CELLS and half a cell is not
    call thm_bg                
    mov bx, MENU_TEXT_Y
    mov dx, MENU_TEXT_Y + 7
    call gfx_fill
    pop bx
.nogap:
    pop dx
    pop bx
    cmp byte [menu_binv], 0
    je .rb
    mov si, bx
.s:
    or byte [menu_bcell+si], 0x80
    inc si
    cmp si, dx
    jbe .s
.rb:
    pop ax
    mov si, dx
    inc si
    mov [menu_bcell+si], al

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_bent - compose bar cell BX's title at its own pen x
; in:  BX = bar index, DI = the running cell index
; out: DI advanced
; clobbers: DI (flags)
; -----------------------------------------------------------------------------
menu_bent:
    push ax
    push bx
    push cx
    push si
    push es
    mov ax, MB_ENTSZ
    mul bl                      ; BX <= MENU_BARMAX, so BL is the whole index
    mov si, menu_bar            ; (menu_bar is .lowbss: read through SS)
    add si, ax
    mov ax, [ss:si + MB_TITLE]
    or ax, ax
    jz .out                     ; the logo cell has no text
    mov cx, [ss:si + MB_SEG]
    push ax
    mov ax, [ss:si + MB_TX]
    call menu_bpad              ; spaces up to this cell's pen
    pop ax
    push cs
    pop es                      ; this cell's own segment (SPEC.md 12.2)
    or cx, cx
    jz .seg
    mov es, cx
.seg:
    mov si, ax
    call menu_bstr
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_bpad  - spaces up to absolute x AX  |  menu_bpadc - up to cell index AX
; menu_bstr  - a NUL string at ES:SI       |  menu_bput  - one cell, AL
; in:  DI = the running cell index
; out: DI advanced
; clobbers: DI (flags)
; -----------------------------------------------------------------------------
menu_bpad:
    push ax
    push cx
    sub ax, MENU_TXT_X0
    jns .c
    xor ax, ax                  ; left of the segment: nothing to pad
.c:
    mov cl, 3
    shr ax, cl
    call menu_bpadc
    pop cx
    pop ax
    ret

menu_bpadc:
    push ax
    push bx
    mov bx, ax
    cmp bx, [menu_bn]           ; **NEVER PAST THE SEGMENT.** menu_bput DROPS
    jbe .p                      ; a cell at or past [menu_bn] and does NOT
    mov bx, [menu_bn]           ; advance DI, so a target beyond it spins here
.p:                             ; forever - with the gfx lock held, on the UI
    cmp di, bx                  ; task, while IRQ0 keeps the clock running, so
    jae .d                      ; the machine looks alive and answers nothing.
                                ; It was unreachable while [menu_bn] was
                                ; always the whole segment (menu_layout clamps
                                ; every pen into it); SPEC.md 59.2's toast
                                ; strip LOWERS it, so a message wide enough to
                                ; reach back over a menu title's pen hangs the
                                ; UI. Clamping here rather than at the two
                                ; callers is what keeps the invariant a
                                ; property of the routine
    cmp di, [menu_bn]           ; **AND THE BOUND IS RE-READ EVERY PASS.** The
    jae .d                      ; clamp above caches [menu_bn] in BX, and the
                                ; field hung a machine right here with the bar
                                ; padded to 60 cells and [menu_bn] BELOW the
                                ; pen: menu_bput drops a cell at or past it
                                ; without advancing DI, so the loop had no
                                ; terminator left. Only a composition writes
                                ; [menu_bn] and fpg_arm/fpg_finish each force
                                ; one (SPEC.md 12.8.3) - so the value the
                                ; clamp read is not the value the drop rule
                                ; uses, and a cached bound cannot be made
                                ; safe. This test IS menu_bput's drop rule,
                                ; asked before the call instead of inferred
                                ; from it: the pad stops where the cells stop,
                                ; whenever the bound moved and whoever moved
                                ; it. One compare a cell (SPEC.md 59.7)
    mov al, ' '
    call menu_bput
    jmp short .p
.d:
    pop bx
    pop ax
    ret

menu_bstr:
    push ax
    push si
.n:
    mov al, [es:si]
    or al, al
    jz .d
    inc si
    call menu_bput
    cmp di, [menu_bn]
    jb .n
.d:
    pop si
    pop ax
    ret

menu_bput:
    push bx
    cmp di, [menu_bn]
    jae .out                    ; past the segment: dropped, exactly as
    mov bx, di                  ; menu_layout drops a cell that will not fit
    cmp [menu_bcell+bx], al
    je .same
    mov [menu_bcell+bx], al
                                ; **A MINIMUM AND A MAXIMUM, because
                                ; composition is NOT in x order** (SPEC.md
                                ; 59.9.2). These were "the first difference"
                                ; and "the last difference" - which is the
                                ; same thing only while the cells are written
                                ; left to right, and 59.9's strip is composed
                                ; AFTER menu_bpadc has run to [menu_bn], at a
                                ; cell that may be to its left. So a strip
                                ; that moved left or got SHORTER dragged
                                ; [menu_blast] back over the padding that had
                                ; just erased its tail: those cells were
                                ; recorded as blank and never drawn, and
                                ; menu_bcell is the record of what is on the
                                ; GLASS, so nothing ever repaired them. Two
                                ; compares, and the ordering rule stops being
                                ; something a caller has to keep
    cmp di, [menu_bfirst]       ; 0FFFFh is above every cell, so the seed at
    jae .last                   ; the top of menu_bar_text still means "none"
    mov [menu_bfirst], di
.last:
    cmp di, [menu_blast]
    jbe .same
    mov [menu_blast], di
.same:
    inc di
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_draw_clock - draw the right-hand field: the clock, or a toast over it
; in:  nothing (caller holds the gfx lock)
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
;
; **The clock is its own segment and answers for itself** (SPEC.md 12.9).
; menu_draw_bar calls it on every window operation and ui_task calls it once
; a second, and the string changes once a MINUTE - so the first thing it does
; is check. It used to erase 206px of bar and re-letter ~20 glyph cells every
; time, which on a 4.77MHz machine is tens of milliseconds of a blank clock
; for content that had not moved: the flash was the whole cost, since nothing
; had changed to justify it.
;
; When it HAS changed it is one opaque font_run over a fixed field - the
; string right-aligned inside it by LEADING SPACES, which is what makes the
; erase and the lettering one pass instead of two (SPEC.md 6.1). The field is
; [vid_clk_hx] .. w-9, both ends on glyph cells, so a shorter form blanks the
; cells it gave up in the same pass. No wm_obscured check: windows clamp to
; y >= MBAR_H, so nothing can ever cover the bar. Ordinary persistent content
; - it is drawn like everything else and the caller's
; gfx_unlock flushes it (SPEC.md 32).
;
; **AND IT IS THE TOAST STRIP'S PAINTER TOO** (SPEC.md 59.8). While [toast_on]
; is set, the LEFT end of this field is the message, in inverse video, and
; the rest of it is still the clock - so "the clock may not draw over a toast"
; is not a rule anybody has to obey, because there is no second painter to
; obey it. The field is composed once, whole, and whoever owns a cell owns it.
; That is the same argument menu_bcell makes for the menus and it buys the
; same two things: the strip cannot diverge from a full repaint, because
; menu_furniture's `[menu_ckck] = 0` says "this field's glass is gone" about
; whatever is on it; and the clock COMES BACK by itself when the message
; expires - toast_pass calls menu_draw_bar, this runs, the strip is 0 cells
; wide, the composed field differs from what is recorded, and the whole thing
; is re-lettered. Nothing has to remember that a toast was ever there.
;
; **THE STRIP HIDES ONLY WHAT IT COVERS.** It is as wide as its message plus
; one inverted space at each end and no wider, and the clock is staged into
; the field FIRST - right-aligned, as it always was - so the cells the strip
; does not reach go on showing the time. A short message lands entirely in the
; leading spaces and the clock is not touched at all; a longer one eats into
; the date from the left, which is the end worth losing, because the time is
; what the corner is for. Blanking the field whole would have been one run
; instead of two and is the wrong answer: a toast is three seconds long and
; the clock is the one thing on the bar somebody may be watching.
;
; That is what makes it the LEFT end and not the right, which was the first
; cut: right-aligned, a snug strip covers the clock's last cells - the time -
; and leaves the date, and it also puts the message where the eye goes for the
; time. Left-aligned it costs the date first and reads as a message arriving
; beside the clock.
;
; So the emit is TWO runs whenever the strip is narrower than the field: the
; strip in inverse video and the clock's surviving tail in the ordinary
; colours. They are disjoint cell ranges, each written exactly once, so there
; is no double-draw and no blank moment (PERFORMANCE.md Part 1) - and the
; check word answers every call in between, so it is twice per CHANGE and not
; twice per repaint.
;
; [menu_ckstrip] is what makes that check exact rather than probable. The
; check word hashes the composed FIELD - message and surviving clock together,
; which is why a minute rolling over under a live toast still redraws - and
; the strip's WIDTH is the one thing about the field that hashing its
; characters cannot see. One byte, one compare.
; -----------------------------------------------------------------------------
; menu_tbar_k - the toast strip's 4px black lead (SPEC.md 59.8.1)
; in:  AX = x1, CX = x2 (inclusive); MENU_TEXT_Y is the row
; out: nothing - all registers AND the flags preserved
;
; A gfx_fill and not a glyph because there is no glyph: the table is ASCII
; 32..126 and holds no half-cell block. It costs 4,142 cycles on a 4.77MHz
; 8088 - PERFORMANCE.md Part 2's per-call floor almost exactly, the 32 pixels
; being ~110us of it - so a wider lead would cost the same.
menu_tbar_k:
    pushf
    push ax
    push bx
    push cx
    push dx
    call thm_ink                
    mov bx, MENU_TEXT_Y
    mov dx, MENU_TEXT_Y + 7
    call gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

menu_draw_clock:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call clk_fmt                ; SI = the line in its live form (SPEC.md 37),
                                ; ALWAYS - a toast covers only the cells it
                                ; needs and the rest of the field is still the
                                ; clock, so there is no branch to take here

    mov bx, [vid_wm8]           ; --- the field, in whole cells
    sub bx, [vid_clk_hx]
    mov cl, 3
    shr bx, cl
    cmp bx, MENU_CLKC - 1       ; the buffer is the field plus its NUL, and a
    jbe .fw                     ; screen wider than any this runs on is the
    mov bx, MENU_CLKC - 1       ; one thing that would notice
.fw:
    or bx, bx                   ; ...and a field of no cells has nothing to
    jz .out                     ; hash: `loop` with CX = 0 runs 65,536 times.
                                ; Unreachable - the field is 25 cells on every
                                ; width, both ends being derived from it - but
                                ; this is the only routine that would find out
.len:
    mov di, si                  ; --- the clock's length, clamped to the field
    xor cx, cx
.cl:
    cmp byte [di], 0
    je .cld
    inc di
    inc cx
    jmp short .cl
.cld:
    cmp cx, bx
    jbe .stage
    mov cx, bx                  ; a form wider than the field cannot happen
                                ; (24 glyphs into 25 cells), but it must not
                                ; underflow the pad into a 64KB rep stosb
.stage:
    mov ax, bx                  ; --- the clock, right-aligned by LEADING
    sub ax, cx                  ; SPACES, which is what makes the erase and
    push ds                     ; the lettering one pass instead of two
    pop es                      ; (SPEC.md 6.1). The staging buffer is ours
    mov di, menu_clkbuf
    cld
    push cx
    mov cx, ax
    mov al, ' '
    jcxz .txt
    rep stosb
.txt:
    pop cx
    jcxz .nul
    rep movsb                   ; DS:SI -> ES:DI, the live string
.nul:
    mov byte [es:di], 0         ; DI = the field's end: pad + text is exactly
                                ; the field by construction

    xor dh, dh                  ; --- and the toast strip over its left end,
    cmp byte [toast_on], 0      ; DH = how many cells of it are the strip
    je .hash
    cmp word [toast_scn], 0     ; ...unless the MENUS segment took it, which is
    jne .hash                   ; SPEC.md 59.9's first choice and costs this
                                ; field nothing: the strip is then wholly in
                                ; the gap the menus left and no clock cell is
                                ; covered at all. One flag, read by the two
                                ; painters, written by toast_place alone
    mov si, toast_buf           ; ...its length, plus the inverted space at
    xor cx, cx                  ; each end
.tl:
    cmp byte [si], 0
    je .tld
    inc si
    inc cx
    jmp short .tl
.tld:
    inc cx                      ; the gap cell plus n glyph cells (SPEC.md
                                ; 59.8.1) - where this was n+2, one whole
                                ; inverted cell of padding at each end
    cmp cx, bx
    jbe .wide
    mov cx, bx                  ; wider than the whole field: it takes the lot
.wide:                          ; and truncates (TOAST_MAX = 23 into 25 cells,
    cmp cx, 2                   ; so unreachable) - and a field too narrow to
    jb .hash                    ; hold even the padding gets no strip at all,
                                ; because the two decs below would wrap
    mov dh, cl
    mov di, menu_clkbuf
    mov si, toast_buf
    mov al, ' '
    stosb                       ; cell 0 is the GAP, and it is a SPACE so the
    dec cx                      ; ordinary clock run paints it white for free
    jcxz .trail
    rep movsb                   ; ...and the message from cell 1, on the grid
.trail:

.hash:
    xor ax, ax                  ; --- one check word over the composed FIELD,
    mov si, menu_clkbuf         ; so a minute rolling over beside a live toast
    mov cx, bx                  ; is a difference like any other
.h:
    rol ax, 1                   ; position-sensitive: '10:23' and '23:10' are
    xor al, [si]                ; different times
    inc si
    loop .h
    or ax, ax
    jnz .hok
    inc ax                      ; 0 means 'nothing has been drawn' - never a
.hok:                           ; live key (the tm_elchk rule)
    cmp ax, [menu_ckck]
    jne .draw
    cmp dh, [menu_ckstrip]      ; ...and the same characters split differently
    je .out                     ; are a different field
.draw:
    mov [menu_ckck], ax
    mov [menu_ckstrip], dh

    mov al, dh                  ; the strip's width, out of DH before DX is
    xor ah, ah                  ; loaded: MENU_TEXT_Y fits in DL and so zeroes
    mov di, ax                  ; DH, which would read as "no toast" every time
    mov dx, MENU_TEXT_Y
    mov cx, [vid_clk_hx]
    mov si, menu_clkbuf
    or di, di
    jnz .strip
    call thm_pair               ; --- no toast: one opaque pass over the lot
                                
    call font_run
    jmp short .out
.strip:
    mov al, [menu_clkbuf+1]     ; --- cell 0 FIRST, in the clock's own colours:
    push ax                     ; it is a space, so this is the 4px white gap
    mov byte [menu_clkbuf+1], 0 ; that keeps the bed off SPEC.md 12.8's widget
    call thm_pair               ; (whose trough fills BLACK) - and it is free,
                                ; the clock having to paint that cell anyway
    call font_run
    pop ax
    mov [menu_clkbuf+1], al

    mov ax, cx                  ; --- the 4px black lead. No glyph can do this:
    add ax, 4                   ; the table is 95 glyphs, ASCII 32..126 (6.2),
    push cx                     ; so CP437's half-block is not available
    mov cx, ax
    add cx, 3
    call menu_tbar_k
    pop cx

    mov al, [menu_clkbuf+di]    ; --- and the message. font_run wants ASCIIZ
    push ax                     ; and the buffer's own NUL is at the field's
    mov byte [menu_clkbuf+di], 0    ; end, so the cell past the strip is
    push cx                     ; banked and put back (menu_bar_text's idiom)
    push si
    add cx, 8                   ; ALWAYS cell 1: `test cx, 7` stays 0, so the
    inc si                      ; single-store path survives the inset - 2.76x
    call thm_rpair              ; on a Hercules 5150 if it does not (59.8.1)
                                ; INVERSE VIDEO: the one distinction that
    call font_run               ; survives SPEC.md 39.4's reduction intact
    pop si
    pop cx
    pop ax
    mov [menu_clkbuf+di], al

                                ; ...and NO trailing lead: column 7 is lit in
                                ; 2 of the 95 glyphs, so every other character
                                ; brings 1px of black with it, and a fill here
                                ; would have to reach outside the field, where
                                ; no painter owns the pixels (SPEC.md 59.8.1)

    cmp di, bx
    jae .out                    ; the strip took the whole field: no clock left
    mov ax, di                  ; --- and the clock the strip did not reach
    mov cl, 3
    shl ax, cl
    add ax, [vid_clk_hx]
    mov cx, ax
    mov si, menu_clkbuf
    add si, di
    call thm_pair               
                                
    call font_run
.out:
    call thm_ink                   ; the documented parting state, on both
                                   ; paths - font_run leaves its own
    pop es
    pop di
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
; point, the same save-under discipline. Two copies of that
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
    cmp cx, [ss:si + MB_XL]
    jb .miss
    cmp cx, [ss:si + MB_XR]
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
    mov bx, [ss:si + MB_ITEMS]
    mov [menu_iptr], bx
    mov ax, [ss:si + MB_SEG]       ; the dropped menu's segment: this CELL's,
    mov [menu_dseg], ax         ; not the bar owner's (SPEC.md 12.2)
    mov cx, [ss:si + MB_NITEM]
    cmp cx, [vid_popmax]        ; the height clamp a bar menu never had: a
    jbe .cnt_ok                 ; package's set is not trusted for count any
    mov cx, [vid_popmax]        ; more than for width (SPEC.md 12.2/2.2)
.cnt_ok:
    mov [menu_cnt], cx
    call menu_widest            ; AX = widest item text, px (BX/CX in)
    add ax, 16
    mov bx, [ss:si + MB_XL]        ; the pull-down hangs off the title's left
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
    mov si, [vid_pw]
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
    mov cx, [vid_ph]
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
; This is the one copy of the save-under discipline for dropped menus: the
; body is real persistent content, while the item highlight is a transient
; overlay drawn VRAM-direct like the cursor. Only one menu is ever open - both callers run on the UI
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
; menu_reloc - the compactor moved the save-under (SPEC.md 66.2). One word,
; re-read at every use: menu_drop loads it to save, menu_undrop to restore, and
; nothing holds a pointer into it across the tracking loop's yields.
menu_reloc:
    cmp bx, [menu_sseg]
    jne .out
    mov [menu_sseg], dx
.out:
    ret

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
    push ax                     ; ...and MOVABLE (SPEC.md 66.5.6). It is only
    mov ax, menu_reloc          ; 20KB and only lives for one menu - but that
    mov bx, MEM_K_SAVE          ; is 20KB sitting mid-arena at exactly the
    call COLD_SEG:mmf_mem_movable ; moment a MENU COMMAND claims, which is one of
    pop ax                      ; the commonest ways a claim happens at all
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

    ; --- draw the menu: the chrome's ground, its frame, items 8px in ---------
    call thm_bg
    mov ax, [menu_x1]
    mov bx, [menu_y1]
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_fill
    call thm_ink
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
    call thm_pen_live           ; the CHROME's pair (SPEC.md 76.6): this is
    call thm_pair               ; the one greying surface the theme reaches.
                                ; AL = chrome ink, AH = chrome background -
                                ; the run's pair, and thm_pair exists for it
    cmp byte [es:si], MENU_DIS
    jne .live
    inc si                      ; disabled: the disabled pen, and skip the
    call thm_pen_dis            ; marker byte. thm_pen_dis and not a bare
    mov al, [gfx_color]         ; CDGRAY store, because on a mono adapter grey
                                ; text IS black text - the flag is what buys
                                ; the checkerboard (SPEC.md 47), which
                                ; font_run FOLDS INTO ITS OWN MASK since
                                ; SPEC.md 6.1.12. Before that this branch had
                                ; to letter transparently, because a run with
                                ; [gfx_dis] set fell back to gfx_fill +
                                ; font_str and filled ground this routine had
                                ; laid four instructions earlier
.live:
    push ax
    call menu_trunc             ; ES:SI = the item, truncated to MENU_MAXCH,
    mov cx, [menu_x1]           ; so no glyph lands outside the saved rect
    add cx, 8
    pop ax
    call font_run_x             ; OPAQUE (SPEC.md 12.2.1): the item's own cells
    call thm_pen_live           ; are one decision each, not a fill and a glyph
                                ; over it
    pop cx
    add bx, 2
    add dx, MENU_ITEM_H
    loop .item
    pop es
.drawn:

    ; --- follow the mouse while the button is down ----------------------------
    mov word [menu_sel], 0xFFFF ; nothing highlighted yet
    mov byte [menu_dropd], 1    ; ...and while this is up, the keyboard mouse
                                ; steps by ITEM rather than by KBM_MIN pixels
                                ; (SPEC.md 9.6.3). Set HERE and not in either
                                ; caller, so the context menu gets it without
                                ; knowing it exists
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
                                ; a transient overlay (SPEC.md 32)
    call kbm_pollm              ; ...and let the keyboard mouse release the
                                ; button it latched, or this loop never ends
                                ; on a machine with no mouse (SPEC.md 9.6)
    call task_yield             ; keep the scheduler healthy while we spin
    jmp .poll

    ; --- released: put the screen back, report the selection -------------------
.up:
    mov byte [menu_dropd], 0    ; the arrows are a pointer again (SPEC 9.6.3).
                                ; The poll loop above has exactly this one way
                                ; out, so no path can leave the flag standing
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
.sel:
    mov dx, [menu_sseg]         ; the menu is off the screen: hand the buffer
    or dx, dx                   ; back rather than hold 20KB until the next
    jz .nofree                  ; one (SPEC.md 12.4/50.2)
    mov bx, MEM_K_SAVE
    call mem_free
    mov word [menu_sseg], 0
.nofree:
    mov byte [menu_bdirty], 1   ; put back only the flag .repaint cleared -
                                ; NOT menu_force, which now means "our pixels
                                ; were destroyed" and would redraw the logo
                                ; and white-fill the bar on every menu click
                                ; (SPEC.md 12.9). Nothing here damages the
                                ; bar: the cell highlight is XOR and undoes
                                ; itself, and both droppers floor at MBAR_H,
                                ; so no menu can ever cover it. The flag costs
                                ; one compose-and-compare that finds nothing
                                ; differs, which is the honest price of not
                                ; reasoning about whether .repaint ran
    mov ax, [menu_sel]

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

menu_dropd  db 0                ; a pull-down is on the screen (SPEC.md 9.6.3).
                                ; In .text with a real initialiser rather than
                                ; in .bss, for fdlg_win's reason: -f bin zeroes
                                ; nothing and this is read on the machine's
                                ; very first arrow key

; -----------------------------------------------------------------------------
; menu_kbnav - the keyboard mouse steps a dropped menu by ITEM (SPEC.md 9.6.3)
;
; in:  AL = the direction key's dy: -1 up, 0 none, +1 down
; out: CF = 0 - the pointer has been PLACED; the caller owes only the repaint
;      CF = 1 - not ours: step the pointer the ordinary way
; clobbers: AX, flags
;
; A menu is a LIST, so the unit an arrow key moves in it is one item - and the
; pixel ramp is measurably the wrong instrument for that. MENU_ITEM_H /
; KBM_MIN is 16 / 3, which a mouseless 5150 measured as 6, 5, 5 presses for
; three items, with the highlight moving on one press in six. The ramp cannot
; be the fix either: held, it is 3, 9, 18, 30, 45 px cumulative against 16px
; cells, so it steps OVER items - which is exactly why 9.6.2's direction reset
; had to exist - and a step sized for a menu could never cross a screen.
;
; Worse than slow, it goes BLANK: a separator is a MENU_DIS item (SPEC.md
; 12.2), so menu_hover answers 0xFFFF for that whole cell and six presses in
; the middle of a menu highlight nothing at all. So disabled cells are SKIPPED
; here rather than merely landed on and rejected.
;
; The row is taken from GEOMETRY and never from menu_hover, which answers
; 0xFFFF for a disabled cell as well as for "outside": stepping from that
; would restart at the end of the menu every time the pointer was parked on a
; separator.
; -----------------------------------------------------------------------------
menu_kbnav:
    cmp byte [menu_dropd], 0
    je .no                      ; nothing is dropped: an ordinary pointer
    or al, al
    jz .no                      ; Left/Right keep their pixel step, which is
                                ; how the pointer leaves a menu sideways
    push bx
    push cx
    push dx
    push si
    push di
    cbw
    mov si, ax                  ; SI = the direction, and the sign the scan
                                ; below walks in

    ; --- which row is the pointer on? -1 above the menu, [menu_cnt] below ----
    mov ax, [mouse_x]           ; the same x test menu_hover makes, so the two
    cmp ax, [menu_x1]           ; can never disagree about what "in the menu"
    jb .beside                  ; means
    cmp ax, [menu_x2]
    jbe .inx
.beside:                        ; the pointer is off to one side: come back in
    mov di, -1                  ; at the end this key is heading AWAY from, so
    or si, si                   ; Down enters at the first item and Up at the
    jns .step                   ; last
    mov di, [menu_cnt]
    jmp short .step
.inx:
    mov ax, [mouse_y]
    mov bx, [menu_y1]
    inc bx                      ; the top frame row is not a cell (menu_hover)
    sub ax, bx
    mov di, -1
    js .step                    ; above the first cell
    mov cl, 4
    shr ax, cl                  ; / MENU_ITEM_H -> the cell under the pointer
    mov di, ax
    cmp di, [menu_cnt]
    jb .step
    mov di, [menu_cnt]          ; below the last cell

    ; --- one cell that way, over any that cannot be selected ------------------
.step:
    add di, si
    js .top                     ; off the top...
    cmp di, [menu_cnt]
    jb .live
    mov di, [menu_cnt]          ; ...or off the bottom: park one cell OUTSIDE
    jmp short .place            ; the frame, where the mouse would be if it had
.top:                           ; left the menu the same way - nothing is
    mov di, -1                  ; highlighted there, and a button press
    jmp short .place            ; dismisses (SPEC.md 9.6.3)
.live:
    mov ax, di
    call menu_item_dis          ; a greyed item or a separator is not a place
    jc .step                    ; to stop. DI only ever moves one way, so this
                                ; walks off an end rather than looping

    ; --- and place the pointer ------------------------------------------------
.place:
    mov ax, di
    mov cl, 4
    shl ax, cl                  ; DI x MENU_ITEM_H, SIGNED - which is what lets
    add ax, [menu_y1]           ; the two off-the-end rows fall out of the one
    add ax, 1 + MENU_ITEM_H / 2 ; formula instead of needing cases of their own
    mov bx, ax                  ; BX = y, the cell's middle row
    mov ax, [mouse_x]           ; x is left alone while it is inside the menu,
    cmp ax, [menu_x1]           ; so a pointer the user has put in a column
    jb .xsnap                   ; stays in it
    cmp ax, [menu_x2]
    jbe .xok
.xsnap:
    mov ax, [menu_x1]
    add ax, 8                   ; ...and otherwise where the item text starts
.xok:
    call mou_clamp              ; the clamp the ISR itself uses, so the
    mov [mouse_x], ax           ; two-display seam is honoured by construction
    mov [mouse_y], bx           ; rather than by a rectangle known here
    mov byte [cur_shchk], 1     ; ...and the pointer MOVED (SPEC.md 7.2.1.1)
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    stc
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
    mov ax, [ss:si + MB_XL]
    xor bx, bx
    mov cx, [ss:si + MB_XR]
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
menu_s_builtin  db 'Builtins', 0    ; was 'Special', and it was the wrong word
                                    ; twice over: it named a leftover rather
                                    ; than a category, and the things in it
                                    ; were somewhere ELSE on the desktop
                                    ; (SPEC.md 12.3.1)

menu_s_about    db 'About os8088...', 0
menu_s_ctrl     db 'Control Panel', 0
menu_s_timer    db 'Timer', 0
menu_s_bounce   db 'Bounce', 0
menu_s_disk     db 'Disk', 0
menu_s_close    db 'Close Window', 0
menu_s_closex   db MENU_DIS, 'Close Window', 0  ; ...with nothing to close;
                                ; ui_loc_gate points the item at one or the
                                ; other before the menu can drop (SPEC.md 47)
menu_s_tasks    db 'Task Manager', 0
menu_s_restart  db 'Restart', 0
menu_s_msep     db MENU_DIS, '---------------', 0
                                ; the rule above Restart: fifteen dashes, the
                                ; width of 'About os8088...', so it reads as a
                                ; rule across the menu rather than as a short
                                ; dash in the middle of one

; The app-name menu's bottom item (SPEC.md 12.7), and the whole of the dock
; tile's context menu (SPEC.md 30.2) - dock_ctx_items points here. One
; string, so two menus that both say 'Close' can never come to say
; different things.
menu_s_appclose db 'Close', 0

; The System menu (SPEC.md 12.1), cell 0 of EVERY application - which is
; exactly why Restart lives here now: it is the one command every app should
; be able to reach, and it used to be reachable only from Locator's own bar
; and from a Disk window's (SPEC.md 12.3.1).
menu_items_logo:
    dw menu_s_about
    dw menu_s_ctrl
    dw menu_s_tasks
    dw menu_s_msep
    dw menu_s_restart
MENU_LOGO_N equ 5               ; ...and menu_relayout stamps this into cell
                                ; 0's MB_NITEM. It was a literal 3 there, in
                                ; a different file from the list it counts
menu_items_file:
    dw menu_s_close
menu_items_builtin:
    dw menu_s_timer
    dw menu_s_bounce
    dw menu_s_disk

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
    dw menu_s_file,    menu_items_file,    1
    dw menu_s_builtin, menu_items_builtin, 3

; -----------------------------------------------------------------------------
menu_s_abpfx db 'About ', 0     ; + the app's name -> menu_abstr (12.2)

section .lowbss                 ; LOW_SEG (SPEC.md 2.1): the runtime bar is
                                ; walked by pointer (menu_relayout writes it,
                                ; menu_draw_bar and menu_hit read it) and is
                                ; never addressed by name outside this module,
                                ; so it costs one ss: prefix per field access
                                ; and nothing in the kernel's 64KB segment
menu_bar:   resb 98             ; MENU_BARMAX x MB_ENTSZ (7 x 14; a literal
                                ; per the no-forward-ref-in-bss rule; guard 3b
                                ; in kernel.asm catches the next cell - and
                                ; caught THIS one, when MENU_APPMAX went 4 to
                                ; 5 for SPEC.md 22.12's Edit menu)

section .bss

menu_win:   resw 1              ; window owning the bar, 0 = Locator (12)
menu_set:   resw 1              ; its app menu set (SPEC.md 12.2)
menu_namep: resw 1              ; the name string drawn at MENU_NAME_X
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
menu_bdirty: resb 1             ; the MENUS segment's text may have moved. Set
                                ; by menu_relayout, by menu_inval and by
                                ; menu_force; spent by menu_bar_text, which
                                ; then usually finds nothing differs
menu_bovr:  resb 1              ; ...and somebody painted OVER the bar, so the
                                ; static furniture is owed too (menu_force).
                                ; A different question from the one above and
                                ; a far rarer one: this is the only flag that
                                ; erases anything (SPEC.md 12.9)
menu_bcell: resb MENU_BCMAX + 1 ; the menus segment as it is ON THE GLASS, one
                                ; byte per 8px cell - composed into, diffed
                                ; against and emitted from. The spare cell is
                                ; the NUL menu_bar_text plants past a run
menu_bn:    resw 1              ; cells in it on THIS screen (SPEC.md 39.2)
menu_bend:  resw 1              ; ...and the first cell past the last menu's
                                ; BOX, so [menu_bn] - this is the free gap the
                                ; toast strip prefers (SPEC.md 59.9). Written
                                ; by menu_bar_text, read by toast_place only
menu_bfirst: resw 1             ; the span that differs, 0FFFFh = none
menu_blast: resw 1
menu_ckck:  resw 1              ; the clock string's check word, 0 = nothing
                                ; drawn (menu_draw_clock)
menu_ckstrip: resb 1            ; ...and how many of the field's cells were
                                ; the TOAST STRIP, 0 = none (SPEC.md 59.8).
                                ; The check word hashes the field's characters
                                ; and cannot see where the inversion stops, so
                                ; without this a message could be left up for
                                ; ever behind a clock that hashed the same
menu_clkbuf: resb MENU_CLKC     ; ...and its field, space-padded so the erase
                                ; and the lettering are one pass
menu_sseg:  resw 1              ; the save-under claim's segment, 0 = none
                                ; (menu_drop then repaints instead)
menu_tbuf:  resb MENU_MAXCH + 1 ; menu_trunc's staging buffer
menu_abcell: resw 1             ; bar index of the app-NAME cell, 0 = none -
                                ; cell 0 is the System menu, so 0 is free to
                                ; mean "Locator owns the bar" (SPEC.md 12.7)
menu_abclose: resb 1            ; ...and which of that cell's items is Close:
                                ; 1 with an About above it, 0 without. Read by
                                ; ui_dispatch, which must know before it hands
                                ; the cell to the application (SPEC.md 12.7)
menu_abitems: resw MENU_ABMAX   ; the name cell's item array: menu_abstr below
                                ; when there is an About, then menu_s_appclose
menu_abstr: resb MENU_ABPFX_LEN + MENU_MAXCH + 1
                                ; 'About ' + the app's name + NUL. The cell's
                                ; TITLE is menu_abstr+MENU_ABPFX_LEN - the
                                ; same bytes, read from the name onward - so
                                ; both strings the cell needs are KERNEL data
                                ; and its MB_SEG is 0 even under a package

section .text
