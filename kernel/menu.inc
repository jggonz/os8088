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
; The right end of the bar is the system clock (SPEC.md 12.1): composed with
; the menus, repainted once a minute (a second, with seconds shown) by the UI
; task, and - because ui.inc hit-tests its cell before menu_track ever sees
; the click - a shortcut to the Control Panel's Date/Time page rather than a
; menu title.
;
; **THE BAR IS ONE COMPOSITION** (SPEC.md 12.9). The logo never changes, the
; menus change when the owner does, the clock changes once a minute, a toast
; comes and goes, and SPEC.md 12.8's file-activity widget borrows the cells
; against the clock. Those are five cadences and there used to be one flag,
; so any of them moving white-filled the whole bar and put every glyph back -
; tens of milliseconds of a BLANK bar on a 4.77MHz machine, on every window
; operation. Now [menu_bovr] owns the static furniture and is the only thing
; here that erases, and everything lettered - menus, clock, toast strip - is
; composed into menu_bcell and emitted as the runs that differ from what is
; on the glass.
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
CMD_SEP     equ 4               ; the RULE above Hibernate. It is a MENU_DIS
                                ; item, so menu_hover can never land on it -
                                ; and it needs no handler either, because
                                ; ui_cmd is a compare ladder and an id it
                                ; does not match falls out of the bottom and
                                ; returns. The id exists only so that
                                ; the items below it still equal base+item
%ifdef KERN_BIG
CMD_HIBER   equ 5               ; Hibernate... (SPEC.md 87), kern_big only:
CMD_REBOOT  equ 6               ; kern_small's menu has no such item and its
CMD_SHUTDOWN equ 7
CMD_CLOSE   equ 8               ; ids stay one lower
%ifdef OS88_SHORTCUTS
CMD_SCDEL   equ 9               ; Locator: File > Remove Shortcut (SPEC.md 26.8.5)
CMD_FILES   equ 10
%else
CMD_FILES   equ 9
%endif
CMD_STIK    equ CMD_FILES + 1   ; the Sticky Apps menu's pick, POSTED by
                                ; STIKMENU.DRV from under the lock and spent
                                ; by ui_cmd with nothing held (SPEC.md 110.12)
CMD_LAST    equ CMD_STIK
                                ; No Builtins menu on either build (SPEC.md
                                ; 14.6, 14.8): CMD_TIMER and CMD_BOUNCE are
                                ; gone with their kinds - the two are packages
                                ; - and CMD_FILES survives only because ui_cmd
                                ; still needs the id: a Disk window is opened
                                ; from the desktop zone (SPEC.md 26.1), which
                                ; posts it
%else
CMD_REBOOT  equ 5
CMD_SHUTDOWN equ 6
CMD_CLOSE   equ 7               ; --- Locator: File ---
                                ; SPEC.md 14.6: no Builtins menu here, so no
                                ; CMD_TIMER and no CMD_BOUNCE - and CMD_FILES
                                ; survives only because ui_cmd still needs the
                                ; id: a Disk window is opened from the desktop
                                ; zone (SPEC.md 26.1), which posts it
CMD_FILES   equ 8
CMD_LAST    equ CMD_FILES
%endif

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
MB_SEG      equ 10              ; word: the SEGMENT this cell's title and
                                ; item strings live in, 0 = the kernel's own
                                ; (SPEC.md 12.2/20.1). Per CELL and not per
                                ; SET, because cell 0 is the System menu and
                                ; its items are always the kernel's however
                                ; foreign the rest of the bar is - reading
                                ; them through a package's segment lands on
                                ; the .o88 header and every item draws "O8"
MB_ENTSZ    equ 12               ; there is no MB_TX: a cell's pen x is
                                ; MB_XL + MENU_TITLE_PAD/2 for every cell
                                ; that has text, and menu_bent - the one
                                ; reader - does the addition. Cell 0 has no
                                ; text (MB_TITLE 0 = the logo glyph, drawn
                                ; from the literal MENU_SYS_TX)
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

; --- the bar is ONE COMPOSITION, and only what changed is drawn (SPEC.md 12.9)
; The logo is a bitmap, drawn only after an overdraw. Everything lettered is
; composed into menu_bcell, one byte per 8px cell from MENU_TXT_X0:
;
;   cells 0 .. [menu_bn]-1      the menus: the app name and every title
;   [menu_bn] .. [menu_bc0]-1   SPEC.md 12.8's widget, while it SHOWS
;   [menu_bc0] .. menu_ckend    the clock, right-aligned by leading spaces
;   ...and then the toast strip, inverted, over the gap the menus leave or
;   over the clock's field when it does not fit there (SPEC.md 59.9)
;
; and the speaker's picture sits in the last 24px before the margin while it
; shows (SPEC.md 34.15). menu_bcell is also the record of what is on the
; glass, so composing it IS the diff, and the padding is the erase.
;
; MENU_TXT_X0 is a glyph-cell boundary and so is [vid_clk_hx], so the bar is
; a whole number of 8px cells - which is what earns font_run's single-pass
; path (SPEC.md 6.1) on the two mono adapters, where a cell row is one store
; and the run is never momentarily blank. Everything the bar lays out is
; therefore snapped to 8 as well: MENU_NAME_X, MENU_TITLE_PAD's half, and
; the name cell's own edges.
MENU_TXT_X0 equ 32              ; the menus segment's left edge (8-aligned,
                                ; and clear of MENU_SYS_XR = 29)
%ifdef VID_EXT
MENU_BCMAX  equ (1024 - 8 - MENU_TXT_X0) / 8    ; cells on the bar, worst
                                ; case: VESA.DRV's widest primary is 1024
                                ; (SPEC.md 120.2.2, VX_XMAX), so 123 cells.
                                ; IT WAS 720's 85 HERE TOO, and a 1024 bar's
                                ; clock field starts at cell 98: every
                                ; composition wrote the clock, its padding and
                                ; the strip up to 34 cells PAST menu_bcell -
                                ; over [menu_bn] .. [menu_blast] mid-pass (the
                                ; widget's leftovers, tests/vesa.py's "chrome
                                ; still on the bar") and into what followed
                                ; it. Found by kernel size pass 14 when the
                                ; .bss after the buffer changed and the About
                                ; cell's name read "  10:54" (SPEC.md 120.5)
%else
MENU_BCMAX  equ (720 - 8 - MENU_TXT_X0) / 8 ; cells on the bar, worst case:
                                ; Hercules is 720 wide and the margin is 8, so
                                ; the menus, the widget and the clock are 85
                                ; cells between them (SPEC.md 12.9)
%endif

MENU_TEXT_Y equ 6               ; title text row (8px glyphs in a 19px bar)
MENU_LOGO_Y equ 4               ; logo glyph top row (11px tall)
MENU_ITEM_H equ 16              ; pull-down item cell height, px
MENU_POPMAX equ 11              ; items ANY pull-down may have (SPEC.md 12.4)

; ...AND [vid_popmax] IS THIS NUMBER, laid down rather than computed.
; vid_apply used to work out min((h - MBAR_H - 2) >> 4, MENU_POPMAX) from
; whichever vid_tab row the adapter selected; the four heights are 480, 348,
; VID_H_MIN and EGA_H, so the answers were 28, 20, 11 and 20 and the clamp
; fired every time - the CGA landing exactly on it, with two scan lines of
; margin (heights >= 198 give 11).  Two things can end that and this is what
; watches for them: MENU_POPMAX growing, and MBAR_H growing.  A FIFTH vid_tab
; row shorter than the CGA's would end it too, which is why the test names
; VID_H_MIN rather than a literal 200 - the constant vid_tab's own shortest
; row is written from (viddet.inc).  The guard lives HERE and not beside
; [vid_popmax] because `%if` cannot see an `equ` declared further down the
; build, and viddet.inc is %included first.
%if ((VID_H_MIN - (MBAR_H + 2)) >> 4) < MENU_POPMAX
  %error "the shortest row of vid_tab no longer fits MENU_POPMAX pull-down items - [vid_popmax] has to be computed again (viddet.inc, vid_apply)"
%endif

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
                                ; - MENU_POPMAX cells plus frame is 178px,
                                ; which fits under the bar on every screen
                                ; this OS drives, VID_H_MIN included. The
                                ; number here used to be 16 cells / 258px,
                                ; from when MENU_POPMAX was 16 - and at that
                                ; count kernel.asm's own assertion on this
                                ; claim FIRES: 4 x 258 x 28 is 28,896 bytes
                                ; against 20,480

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
                                            ; - and viddet.inc's vid_apply and
                                            ; [vid_clk_hx] initialiser both read
                                            ; it now, forward, rather than
                                            ; carrying 198 and 206 as literals
MENU_CLK_X  equ SCREEN_W - 8 - MENU_CLK_W   ; 440: cell left, 8px right margin
MENU_CLK_HX equ (MENU_CLK_X - 6) & 0FFF8h   ; 432: hit band left edge, and the
                                            ; left edge of the clock SEGMENT -
                                            ; floored to a glyph cell, so the
                                            ; field is a whole number of them

; -----------------------------------------------------------------------------
; menu_init - boot state: Locator owns the bar (SPEC.md 12)
; in:  nothing (called from kmain after wm_init, before the first paint)
; out: nothing (all registers preserved but BP: spw_near's target is left in
;      it, and the one caller holds nothing there)
; -----------------------------------------------------------------------------
; IN THE BOOT OVERLAY (SPEC.md 2.5.2, docs/plans/LAST-DROP-BYTES.md row 17). One
; caller, kmain:4299. menu_force is INLINED - it is the two stores below and a
; `ret`, one of which menu_init was already making itself, so a cw_ shim would
; have cost `.text` more than the call saved.
section .ovl
menu_init:
    push ax
    push bx
    push dx
    mov word [menu_bdirty], 0x0101 ; [menu_bovr] too. .bss is not zeroed at
                                ; boot (-f bin), and the
                                ; furniture has never been drawn: menu_force's
                                ; whole body, and the second store is the one
                                ; this routine used to make before calling it
    xor bx, bx
    mov [menu_win], bx
    mov word [menu_sseg], 0     ; the save-under is claimed by menu_drop for
    mov bp, menu_relayout       ; (spw_near's `call bp`: kmain_o, the one
    call KERNEL_SEG:spw_near    ; caller, holds nothing in BP) as long as a
                                ; menu is actually down, not
                                ; here for the session (SPEC.md 12.4/50.2)
    pop dx
    pop bx
    pop ax
    retf                        ; FAR: kmain reaches this through SPLGATE1
section .text

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
    mov word [menu_bdirty], 0x0101  ; [menu_bovr] too: adjacent, asserted
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
    mov dx, cx                  ; x2, out of the way of menu_x2cell's CL
    call menu_x2cell            ; AX = first cell, rounded DOWN - from the
                                ; segment's start when x1 is left of it - and
                                ; CL = 3 for the shift below
    sub dx, MENU_TXT_X0
    js .out                     ; wholly left of it: nothing of ours
    shr dx, cl                  ; ...and the cell x2 lands IN, which is the
                                ; outward rounding at that end (pass 14: 10)
    cmp dx, MENU_BCMAX
    jb .hi
    mov dx, MENU_BCMAX-1
.hi:
    cmp ax, dx                  ; a first cell at or past MENU_BCMAX is past
    ja .out                     ; the clamped DX too, so this one compare is
                                ; also the `cmp ax, MENU_BCMAX` that stood
                                ; above it (kernel size pass 14: 10 bytes
                                ; with the walk below)
    xchg ax, bx                 ; BX = the cell; AX comes back off the stack
.z:
    mov byte [bx + menu_bcell], 0   ; 0 is never composed: every byte that
    inc bx                      ; goes into this buffer is a printable one,
    cmp bx, dx                  ; so it can never match and the cell is
    jbe .z                      ; redrawn
.out:
    jmp kret_dx

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
    inc byte [ui_post]          ; ...and SAY SO once (SPEC.md 13.13): tested
                                ; zero/non-zero and swept every tick, so a
                                ; count is as good as a 1
                                ; window may have just gained the focus with
                                ; a refresh owing (SPEC.md 22.8). This is the
                                ; catch-all rather than the main path -
                                ; wm_raise spends the debt before it draws -
                                ; and it is here because THIS is where every
                                ; focus change passes, including the one no
                                ; raise reaches: losing the front window
                                ; promotes the window under it (menu_check)
    or bx, bx                   ; ...and for the same reason the DESKTOP'S
    jz .loc                     ; selection is let go here when a window
    call COLD_SEG:desk_unsel_x  ; takes the bar (SPEC.md 26.9.5)
.loc:
    call menu_relayout
    stc
    ret
.same:
    clc
    ret

; -----------------------------------------------------------------------------
; OSAPI_MENU_OWNER - who is the ACTIVE APPLICATION? (SPEC.md 12/12.6)
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
;
; AN INLINE CELL (SPEC.md 20.3): the cell is `mov bx, [cs:menu_win] / retf`
; and there is no body - which is also what makes the per-frame poll cheap.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; menu_win_set - attach an app menu set to a window (API 0x011E, SPEC 12.2)
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
.mine:                          ; (wm_about_set joins here with its own store)
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
    test byte [bx + W_FLAGS], 2 ; still visible?
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
    call kent_di                ; be different: the bar owes a full redraw
                                ; (kent_di = push ax/bx/cx/dx/si/di)
    push es

    mov bx, [menu_win]
    or bx, bx
    jnz .app
    mov si, menu_loc_set        ; no window owns the bar: Locator does, and
    mov [menu_set], si          ; its set is kernel data - so its name is
    mov si, [cs:si + AM_NAME]   ; read through CS and .kseg below makes the
    jmp short .kseg             ; label's segment ours (it is never 0)

.app:
    mov di, bx                  ; the owner's segment, once, for every read
    call wm_strseg              ; of the set below AND for menu_draw_bar,
                                ; menu_drop and menu_widest afterwards
                                ; (SPEC.md 12.2/20.1). NO DI BANK: nothing
                                ; reads DI again before inst_win_owner WRITES
                                ; it or the copy below loads menu_bar into it,
                                ; and the entry value is the frame's to put
                                ; back at kret_di (kernel size pass 10: 2)
    mov ax, es
    KSEGR cx                    ; the kernel's DATA segment: wm_strseg's ES
    cmp ax, cx                  ; for a kernel window (kern_mega's alias)
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
    lea si, [di + I_NAME]       ; the 16-byte inline name field - KERNEL data,
    jmp short .kseg             ; so the label's segment is ours after all
.from_title:
    mov si, [bx + W_TITLE]

.name_done:
    or si, si                   ; a nameless window still gets a label
    jnz .have_name
    mov si, menu_loc_name
.kseg:                          ; ...the ONE place the three kernel-data
    mov word [menu_seg], 0      ; names set the segment (kernel size pass 9:
    KES                         ; it was written out three times)
.have_name:
    mov [menu_namep], si

    ; --- cell 0: the System menu, fixed in every application ---------------
    push es                     ; ES:SI is the name, wanted below
    push si
    push ss                     ; menu_bar is .lowbss, so SS - and the six
    pop es                      ; words are menu_cell0's, a fixed record in
    mov si, menu_cell0          ; .text: one rep movsw where six immediate
    mov di, menu_bar            ; stores through an SS override were 41
    mov cx, MB_ENTSZ / 2        ; bytes with the `add di` after them; DI
    cld                         ; ends one entry on, which is the add
    rep movsw                   ; (kernel size pass 9)
    pop si
    pop es
    inc cx                      ; the rep left CX = 0, so this is 1 - the
    mov [menu_nbar], cx         ; logo cell - a byte under the immediate

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
    push ds                     ; the entry's three words go across AS THEY
    push es                     ; STAND - AMENU_TITLE/ITEMS/NITEM are
    push es                     ; MB_TITLE/ITEMS/NITEM field for field (the
    pop ds                      ; %if below) - so it is three movsw with the
    push ss                     ; segments swapped, where it was three loads
    pop es                      ; and three stores through two overrides
    movsw                       ; (kernel size pass 10: 17 bytes). No prefix
    movsw                       ; on the string op, so the restart hazard does
    movsw                       ; not arise; SI -> the next entry, DI -> MB_XL
    pop es
    pop ds
    mov ax, [menu_seg]          ; ...and these are the owner's
    mov [ss:di + MB_SEG - 6], ax
    mov [ss:di + MB_XL - 6], bx ; the text is centred at XL + PAD/2 - and
                                ; 8-aligned, since BX is and a glyph width
                                ; is a multiple of 8, so every cell after
                                ; this one is too. menu_bent re-derives it
    mov ax, dx
    dec ax
    mov [ss:di + MB_XR - 6], ax ; xr = left + width - 1
    mov bx, dx                  ; next cell starts where this one ended
    inc word [menu_nbar]
    add di, MB_ENTSZ - 6
    pop cx
    loop .cell
%if AMENU_TITLE != MB_TITLE || AMENU_ITEMS != MB_ITEMS || AMENU_NITEM != MB_NITEM || MB_TITLE != 0 || MB_ITEMS != 2 || MB_NITEM != 4 || AMENU_ENTSZ != 6
  %error "menu_relayout copies an AMENU entry's first three words into the MB record with three movsw, and steps SI by the copy alone"
%endif
    jmp short .done

.full:
    pop cx                      ; the bar is full: everything from here on
                                ; is dropped, so the cells that DID fit keep
                                ; bar index == set index
.done:
    call menu_ab_cell           ; the NAME cell goes LAST (DI = next slot)
    pop es
    jmp kret_di

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
    xor ax, ax                  ; AX is wm_about_get's to fill below
    mov [menu_abcell], ax
    mov bx, [menu_win]
    or bx, bx
    jz .out                     ; Locator: see above

    push di                     ; --- 'About ' + the name, into menu_abstr ---
    mov di, menu_abstr
    KES
                                ; the prefix is ours...
    mov si, menu_s_abpfx
    call menu_ab_cat
    call menu_es                ; ...the name is the owner's (SPEC.md 12.2)
    mov si, [menu_namep]
    call menu_ab_cat
    mov byte [di], 0
    pop di

    mov bx, [menu_win]          ; --- the items: About if it is offered, then
    call wm_about_get           ; Close, which every application gets (12.7)
    xor bx, bx                  ; BX = Close's index
    or ax, ax
    jz .noab
    mov word [menu_abitems], menu_abstr
    inc bx
.noab:
    mov cx, bx                  ; CX = item count = Close's index + 1, which
    inc cx                      ; is MENU_ABMAX when About is offered (kernel
                                ; size pass 10: 5 bytes over two immediates)
%if MENU_ABMAX != 2
  %error "menu_ab_cell counts its items as Close's index + 1: About and Close are the only two"
%endif
    mov [menu_abclose], bl
    add bx, bx                  ; ...to a word offset
    mov word [menu_abitems + bx], menu_s_appclose

    KES                         ; --- the cell, over the label's own rect ---
                                ; the copy is kernel data now, so measure it
    mov si, menu_abstr + MENU_ABPFX_LEN
    call font_width_x           ; AX = the label's pixel width
    mov [ss:di + MB_TITLE], si
    mov word [ss:di + MB_ITEMS], menu_abitems
    mov [ss:di + MB_NITEM], cx
    mov word [ss:di + MB_XL], MENU_NAME_X - MENU_TITLE_PAD / 2
    add ax, MENU_NAME_X + MENU_TITLE_PAD / 2 - 1
    mov [ss:di + MB_XR], ax     ; ...so this cell is 8-aligned and a whole
                                ; number of cells wide, like the app's own
                                ; (SPEC.md 12.9), and its pen x comes back
                                ; out of MB_XL as MENU_NAME_X exactly
    mov word [ss:di + MB_SEG], 0   ; every string it names is kernel data
    mov ax, [menu_nbar]
    mov [menu_abcell], ax
    inc word [menu_nbar]
.out:
    pop es
    pop di
    pop si
    jmp kret_cx

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
    es lodsb                    ; (no REP, so the override survives an
    or al, al                   ; interrupt; SI is banked, so the one-past on
    jz .done                    ; the NUL is not seen - kernel size pass 10)
    mov [di], al
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
; out: ES; nothing else (flags clobbered)
;
; Every string and every word of an app menu set is an offset in the OWNING
; package's segment (SPEC.md 12.2/20.1). One window owns the bar at a time,
; so one word answers for the whole set - there is no per-entry segment in
; menu_bar, and there does not need to be.
; -----------------------------------------------------------------------------
menu_es:
    push ax
    mov ax, [menu_seg]
.seg:                           ; ...menu_des joins here with its own word
    or ax, ax
    jnz .set
    KSEGR ax                    ; 0 = the kernel's own (.text: CS is it)
.set:
    mov es, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_des - point ES at the segment the DROPPED menu's items live in
; in:  nothing ([menu_dseg], set by menu_track from the cell / menu_popup)
; out: ES; nothing else (flags clobbered)
; -----------------------------------------------------------------------------
menu_des:
    push ax
    mov ax, [menu_dseg]
    jmp short menu_es.seg       ; one body for the two words (kernel size
                                ; pass 9, -8)

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
; set it rather than reason about it. The UI task's own clock step calls
; menu_bar_clock direct - the clock's cells and nothing else - and it has
; nothing to do with the Timer app (SPEC.md 14).
; -----------------------------------------------------------------------------
menu_draw_bar:
    call kent_di                ; = push ax/bx/cx/dx/si/di

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
    shr byte [menu_bovr], 1     ; --- segment 1: the static furniture -------
    jnc .txt                    ; only ever owed after an OVERDRAW. Taken and
                                ; cleared in one instruction: [menu_bovr] is
                                ; only ever 0 or 1 (menu_force's word store
                                ; and the fullscreen guard above are its
                                ; writers), so the shift leaves 0 and CF is
                                ; the bit (kernel size pass 14: 6 bytes)
                                ;
                                ; **The only path in the machine that erases
                                ; the menu bar**, and it is reached only from
                                ; [menu_bovr] - somebody painted over us.
                                ; Everything else that changes the bar changes
                                ; TEXT, and text is put back by an opaque run
                                ; that never blanks what it is replacing
                                ; (SPEC.md 6.1/12.9). The two things drawn
                                ; here are the two that cannot be recomputed
                                ; from the owner: the rule along the bottom row
                                ; and the 11x11 System logo. It was
                                ; menu_furniture, a routine of its own with
                                ; this as its one caller, and written here it
                                ; needs none of the five pushes it made -
                                ; kent_di banked them above (pass 14: 12)
%ifdef OS88_SNDCARD
    or word [menu_mixcap], -1   ; the speaker is lost with the field: 0FFFFh
%endif                          ; = unknown, so menu_sound draws it again
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

    xor ax, ax                  ; ...and the fill took every glyph with it,
    mov cx, 0x7FFF              ; so nothing that was drawn is still drawn:
    call menu_inval             ; menu_inval over x = 0..7FFFh IS that - it
                                ; clamps to all MENU_BCMAX cells and raises
                                ; [menu_bdirty] itself, where this carried the
                                ; loop and the store a second time (kernel size
                                ; pass 10: 13 bytes) - the clock's cells
                                ; with them, the clock being composed there
    test byte [fpg_on], FPG_SHOWN   ; ...and that fill took SPEC.md 12.8's
    jz .txt                     ; widget with it, which can be up across
    call fpg_paint              ; a repaint at all. Its own pixels only - the
                                ; cells are already ours, the text pass below
                                ; keeps off them, and fpg_paint deliberately
                                ; does not call back into this routine

.txt:
    shr byte [menu_bdirty], 1   ; --- the text: menus, clock and toast -----
                                ; taken and cleared in one instruction, as
                                ; [menu_bovr] above (0 or 1 at every writer):
                                ; CF = 1 the whole bar's cells (SPEC.md
                                ; 12.9), CF = 0 only the clock's, nothing
                                ; else having moved (SPEC.md 12.1) - and
                                ; menu_bt_ent takes the choice in CF and ends
                                ; in the speaker, which answers to either
    call menu_bt_ent
.out:
    jmp kret_di

; -----------------------------------------------------------------------------
; menu_bar_text  - the whole bar's text: compose it, diff it, emit what moved
; menu_bar_clock - ...the clock and the toast strip only (SPEC.md 12.9)
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; **THE BAR IS ONE COMPOSITION** (SPEC.md 12.9). Everything lettered on it -
; the app name, every menu title, the toast strip and the clock - is composed
; into menu_bcell, ONE BYTE PER GLYPH CELL from MENU_TXT_X0 to the margin, and
; menu_bcell is also the record of what is ON THE GLASS. So composing it IS
; the diff: the cells that differ are the only ones drawn, as one font_run per
; run, and a bar whose content has not moved emits nothing at all. A minute
; rolling over is the one digit that changed and not the 25-cell field.
;
; It is sound for the reason the Task Manager's row check is (SPEC.md 28): the
; bar is never covered (windows clamp to y >= MBAR_H), so what the last emit
; drew is still what is showing, and the only thing that can falsify that is
; an overdraw - which is exactly what menu_force and menu_inval say. The
; padding IS the erase: a space is an opaque cell like any other, so there is
; no fill anywhere in this path.
;
; Left to right, and each part has its own bound in [menu_blim]:
;   cells 0 .. [menu_bn]-1      the menus, padded with spaces
;   [menu_bn] .. [menu_bc0]-1   SPEC.md 12.8's widget, when it is SHOWING -
;                               drawn straight onto the glass, composed into
;                               nothing, its cells left 0 so the emit skips
;                               them (FPG_CELLS of them; none otherwise)
;   [menu_bc0] .. menu_ckend    the clock, right-aligned by leading spaces
; and then the toast strip over whichever of those it was placed on
; (toast_place, SPEC.md 59.9). Two painters for one strip of glass cannot
; happen: there is one painter.
;
; menu_bar_clock is the same routine entered past the menus, for the UI
; task's clock tick and for a menu_draw_bar with nothing dirty: the menus'
; cells are already right, so what is left to compare is 25 cells. A minute
; rolling over is the digit that changed, lettered by one font_run - measured
; on a 4.77MHz 5150 with a CGA, 40,791 cycles when the clock was its own
; painter and re-lettered all 25 cells, 20,111 composed here.
;
; FOUR DOORS AND ONE CHOICE, carried in CF (kernel size pass 14):
;   menu_bar_clock  CF = 0, then menu_bt_ent        - the UI task's tick
;   menu_bt_ent     CF as given, then the speaker   - menu_draw_bar's
;   menu_bar_text   CF = 1, the composition alone   - menu_sound's
;   menu_bt_raw     CF as given, the composition alone
; Every composition but menu_sound's own is preceded by toast_place and
; menu_sound, so the geometry it composes against is this pass's, and
; followed by menu_sound (SPEC.md 34.15), whose picture and geometry answer
; to what was just composed - so
; that is said once, here, where it was said at both callers and again in a
; menu_draw_clock of its own. menu_sound's own re-composition goes through
; menu_bar_text and so does NOT come back round: it is the one place the
; geometry has just been decided.
; -----------------------------------------------------------------------------
menu_bar_clock:
    clc                         ; CF = 0: past the menus
menu_bt_ent:
%ifdef OS88_SNDCARD
    pushf                       ; the choice, across the settle
    call toast_place            ; THE GEOMETRY BEFORE THE COMPOSITION: this
    call menu_sound             ; pass's toast decides [vid_clk_hx] (SPEC.md
    popf                        ; 34.15), whose own menu_bar_text letters it
                                ; on a move - so nothing below is lettered
                                ; from a stale [menu_bc0] and then moved
    call menu_bt_raw            ; ...and FALLS INTO menu_sound, which is
                                ; written here for that (pass 14: 3 bytes):
                                ; a whole-bar composition may move the menus'
                                ; end, and with it whether the strip fits
; The speaker is the RIGHT end of the clock's field, and the clock is
; right-aligned against it (SPEC.md 34.15). The menus' boundary stays where
; it is with no speaker, so the gap a toast tries first is unchanged; it
; moves left only by the cells a clock line needs past the 22 left of the
; speaker. A toast that missed the gap hides the speaker instead. The driver owns the picture: no idle
; port reads or icon blits, only a capability, geometry or need change.
menu_sound:
    call kent_di                ; = push ax/bx/cx/dx/si/di - and with BP and
    push bp                     ; ES that is kret_es's run exactly (kernel
    push es                     ; size pass 14: 9 bytes against eight pops)
    mov ax, [vid_pw]
    sub ax, 8 + MENU_CLK_W + 6
    and ax, 0FFF8h             ; the clock's field with no speaker
    mov dx, SND_CAP_MIXER       ; the immediate in the 3-byte `mov` and the
    and dx, [drv_svc+DSV_CAPS]  ; word in the `and` (pass 14: 1). DX is now
    jz .origin                  ; SND_CAP_MIXER itself, so DL is a ZERO the
                                ; scov test below compares against, a byte
                                ; under the immediate (snd.inc's %if holds it)
    add ax, MENU_CLK_W - 16    ; the speaker: the field's last 24 pixels.
    mov [menu_mixx], ax         ; Through AX and back, one byte under BX's
    sub ax, MENU_CLK_W - 16    ; imm16 form, and an EVEN routine (t_hotpar)
    cmp [toast_scov], dl        ; **A TOAST THAT MISSED THE GAP COVERS THE
    je .need                    ; SPEAKER** (SPEC.md 34.15): the field is then
    xor dx, dx                  ; the one it has with no speaker, which holds
    jmp short .origin           ; its widest strip, and the boundary stays put
.need:
    call clk_fmt                ; CX = the clock's glyphs, 18..24
    sub cx, (MENU_CLK_W - 16) / 8
    jbe .origin
.wide:
    sub ax, 8                   ; one cell of the menus' per glyph past 22
    loop .wide
.origin:
    cmp ax, [vid_clk_hx]
    jne .changed
    cmp dx, [menu_mixcap]
    je .out
.changed:
    mov [vid_clk_hx], ax        ; nothing to forget: menu_bcell is the record
                                ; of menus and clock alike (SPEC.md 12.9)
    or dx, dx
    jnz .store
    cmp byte [menu_mixcap+1], SND_CAP_MIXER >> 8  ; 0, 0200h or 0FFFFh: the
    jne .store                  ; high byte says which (pass 14: 1)
    mov ax, [menu_mixx]         ; the speaker went: erase its picture ABOVE
    mov cx, ax                  ; and BELOW the text band - the band is the
    add cx, 23                  ; composition's, which letters the clock or
    call fpg_ring               ; the strip over it below. fpg_ring's rows
                                ; are the whole field's, 0..MBAR_H-2, where
                                ; the picture is rows 2..17: the four more are
                                ; the bar's own ground under no other painter
                                ; at these columns, so they are written with
                                ; the colour they already hold (pass 14: 16)
    xor dx, dx
.store:
    mov [menu_mixcap], dx
    call menu_relayout          ; bounded at the new right-hand hit band
    call menu_bar_text          ; compose only the changed cells
    or dx, dx                   ; ...both preserve DX, the new capability
    jz .out
    mov ax, [menu_mixx]         ; the speaker is drawn over cells the record
    mov cx, [vid_pw]            ; still holds: forget them, so the next toast
    call menu_inval             ; that hides it letters them again
    xchg ax, cx
    call thm_rpair              ; the THEMED icon: AL = the bar's ground, AH =
    mov bx, DRVC_SOUND*256 + SNDV_MIXICON_THEME ; its ink (SPEC.md 34.15).
    call drv_pkg_call_x         ; ES is whatever it is: the verb takes no
                                ; buffer, and mix_icon sets its own ES for
                                ; the blit (volume.inc) - the `push ds / pop
                                ; es` here was for nothing (pass 14: 2)
.out:
    jmp kret_es                 ; pops es, bp, di, si, dx, cx, bx, ax
menu_mixcap: dw 0FFFFh
menu_mixx:   dw 0
menu_bar_text:
    stc                         ; CF = 1: the menus too
menu_bt_raw:
%endif
    call kent_di                ; = push ax/bx/cx/dx/si/di (flags untouched)
    push es
    pushf                       ; ...the choice, across the prologue

    test byte [fpg_on], FPG_SHOWN   ; --- is SPEC.md 12.8's widget on the
    jz .nores                   ; bar? The cells are reserved only while its
    call fpg_vis                ; pixels are there (SPEC.md 12.8.7) and a
    mov dx, FPG_CELLS           ; toast has not outranked it - `mov` leaves
    jc .res                     ; fpg_vis's CF for the jump (pass 14: 4)
    call fpg_hide               ; a toast just outranked it: take it down
.nores:                         ; (the rows outside the text band; the band
    xor dx, dx                  ; is this composition's), and its cells are
.res:                           ; the menus' and the clock's again
    mov ax, [vid_clk_hx]        ; the clock field's first cell - both ends
    call menu_x2cell            ; are 8-aligned by construction
    mov [menu_bc0], ax
    sub ax, dx                  ; the widget sits against the clock: its
    mov [menu_bn], ax           ; cells come off the menus' end
    mov [menu_blim], ax

    xor di, di                  ; DI = cell index, 0 = x MENU_TXT_X0
    mov [menu_blast], di        ; SEEDED: a maximum (menu_bput), read only
    or word [menu_bfirst], -1   ; when [menu_bfirst] says something differed
                                ; ...and [menu_bfirst] = 0FFFFh (imm8: 1)
    popf
    jnc .clock

    mov bx, [menu_abcell]       ; --- the application's name ----------------
    or bx, bx                   ; a CELL when the application declared an
    jz .bare                    ; About (SPEC.md 12.2/12.7), and it is
    call menu_bent              ; APPENDED last but positioned FIRST, so it
    jmp short .cells            ; is composed here to keep x order
.bare:
    mov ax, MENU_NAME_X         ; Locator: a bare label, same pen x
    call menu_bpad
    call menu_es                ; the name belongs to the owner (12.2/20.1).
    mov si, [menu_namep]        ; ES is NOT banked round it: the entry's
    call menu_bstr              ; `push es` is, and nothing below reads ES
                                ; before the exit's `pop es` - menu_bent sets
                                ; its own, font_run and the rest take DS (and
                                ; what ES held there was the caller's, never
                                ; ours): pass 14, 2 bytes

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
    mov ax, [menu_bn]           ; --- WHERE THE MENUS END (SPEC.md 59.9). DI is
    lea bx, [di+1]              ; one past the last GLYPH, and a title's pen is
    cmp bx, ax                  ; MB_XL + 8 with MENU_TITLE_PAD split 8px each
    jbe .bend                   ; side - so that cell is still inside the last
    mov bx, ax                  ; menu's BOX and the one after it is the first
.bend:                          ; that no menu owns. BX is free here (the cell
    mov [menu_bend], bx         ; walk is done) and AX is the pad's bound
                                ; below as it stands (pass 14: 2 bytes)
    call menu_bpadc             ; spaces to the menus' end: this is what erases
                                ; the menus of the app that just went away

.clock:
    call toast_place            ; --- where the strip goes, FIRST: it decides
                                ; whether the speaker shows, and so where the
    call menu_ckend             ; clock's field ends (SPEC.md 34.15)
    mov [menu_blim], ax         ; AX = the field's end cell...
    call clk_fmt                ; SI = the line, CX = its glyphs (18..24) -
    sub ax, cx                  ; and AX kept: the cell its first glyph lands
                                ; in (clk_fmt banks AX: pass 14, 3 bytes)
    mov di, [menu_bc0]
    call menu_bpadc             ; the leading spaces, which are the erase of a
.cg:                            ; longer line that went before
    lodsb
    call menu_bput
    loop .cg

    mov cx, [toast_scn]         ; --- and the toast strip over it, if one is
    jcxz .diff                  ; up and within its time (SPEC.md 59.9)
    mov di, [toast_scell]
    mov si, toast_buf
    mov al, ' '                 ; the gap cell FIRST, into the loop past its
    jmp short .sput             ; load: [toast_scn] counts it, and menu_bemit
.sch:                           ; white-fills its left 4px after the run
    lodsb                       ; (SPEC.md 59.8.1). Pass 14: 4 bytes
.sput:                          ; **BIT 7 SET ON EVERY CELL** (SPEC.md 59.2):
    or al, 0x80                 ; a cell whose CHARACTER did not change but
    call menu_bput              ; whose HIGHLIGHT did has to read as a
    loop .sch                   ; difference. menu_bemit masks it off to draw
    cmp byte [toast_scov], 0    ; ...and over the CLOCK, the cells after it are
    je .diff                    ; blanked to the end of the word they cut into,
.cw:                            ; so no fragment of the date is left standing
    cmp di, [menu_blim]         ; beside it (SPEC.md 59.8.2)
    jae .diff
    cmp byte [menu_bcell+di], ' '
    je .diff
    mov al, ' '
    call menu_bput
    jmp short .cw

.diff:
    mov bx, [menu_bfirst]       ; --- emit the span(s) that moved -------------
    inc bx                      ; 0FFFFh: nothing on the bar changed, no
    jz .out                     ; pixels (the inc and dec are a byte under
    dec bx                      ; the compare's imm8)
    mov dx, [menu_blast]

.piece:                         ; the span is split wherever the inversion
    mov al, [menu_bcell+bx]     ; changes - the strip is the only thing that
    or al, al                   ; inverts - and wherever a cell is 0, which is
    jnz .pz                     ; a cell nobody composed: the widget's, when a
    inc bx                      ; menu and the clock moved in one composition
    jmp short .pnext            ; on either side of it
.pz:
    and al, 0x80
    mov si, bx
.pscan:
    inc si
    cmp si, dx
    ja .pemit
    mov ah, [menu_bcell+si]
    or ah, ah
    jz .pemit
    xor ah, al                  ; same highlight as the piece? AL is 0 or
    jns .pscan                  ; 80h, so the XOR's sign IS the answer
.pemit:
    push dx                     ; the piece is BX .. SI-1, and SI is the next
    push si                     ; piece's first cell
    dec si
    mov dx, si
    add al, al                  ; SPEC.md 76.3: the STRIP, SAID rather than
    jnc .pnorm                  ; inferred - AL is 0 or 80h, so CF = 1 is the
                                ; inverted piece, and it rides in CF into
                                ; menu_bemit: thm_rpair and thm_pair are a
                                ; `mov` and an `xchg`, which write no flag
    call thm_rpair              ; INVERSE VIDEO, the one distinction that
    jmp short .pgo              ; survives SPEC.md 39.4's reduction intact
.pnorm:
    call thm_pair
.pgo:
    call menu_bemit
    pop bx
    pop dx
.pnext:
    cmp bx, dx
    jbe .piece
.out:
    pop es
    jmp kret_di

; -----------------------------------------------------------------------------
; menu_bemit - letter cells BX..DX of the menus segment in one font_run
; in:  BX = first cell, DX = last cell (inclusive), AL = ink, AH = background,
;      CF = 1 the run is the toast strip's (inverted), 0 it is not
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
; exact rather than approximate: an inverted run is wholly inside the strip by
; construction, its caller having split the span on that very bit, so every
; cell in it carries the bit and every cell in it gets it back.
;
; **IS THIS THE INVERTED RUN is SAID, in CF** (SPEC.md 76.3). It used to be
; `cmp di, CWHITE` - the run's own colours read as a proxy for the answer,
; which the Dark theme swaps: the PLAIN run then matches, bit 7 is never masked
; off, and font_char indexes past the 95 glyphs it has. Then it was a byte in
; .text, menu_binv, written by the caller and read three times here; it is the
; caller's carry now, banked across font_run with `pushf` (kernel size pass
; 14: 17 bytes with the byte and its store). The popf restores the entry's IF
; and DF with it, which are what they were - this runs with neither changed.
; -----------------------------------------------------------------------------
menu_bemit:
    call kent_di                ; = push ax/bx/cx/dx/si/di (flags untouched)

    mov di, ax                  ; the colours, out of the way of the x
    mov si, dx                  ; arithmetic below
    mov al, [menu_bcell+1+si]   ; (the +1 in the displacement, not an `inc`:
    mov byte [menu_bcell+1+si], 0   ; SI is reloaded below either way)
    push ax                     ; AL = the banked byte
    pushf                       ; CF = the answer, past font_run
    push bx
    push dx

    jnc .go                     ; plain: nothing to mask
    mov si, bx
.m:
    and byte [menu_bcell+si], 0x7F
    inc si
    cmp si, dx
    jbe .m
.go:
%if MENU_TXT_X0 % 8
  %error "menu_bemit's x is (cell + MENU_TXT_X0/8) * 8: the segment must start on a cell"
%endif
    lea ax, [bx + MENU_TXT_X0 / 8]  ; CX = the run's x, (cell + X0/8) * 8
    mov cl, 3                   ; (8-aligned, so font_run takes its
    shl ax, cl                  ; single-pass path on mono)
    xchg ax, cx
    lea si, [bx + menu_bcell]
    mov dx, MENU_TEXT_Y
    mov ax, di
    call font_run

    pop dx
    pop bx
    popf
    jnc .rb                     ; plain: nothing to put back either
    cmp bx, [toast_scell]       ; the STRIP: take its first cell's left 4px
    jne .nogap                  ; back to the ground, so the gap home pads
                                ; - ITS first cell, not the first of a run
                                ; that began inside it because only the
                                ; message's tail changed
    push bx                     ; as the clock home does (SPEC.md 59.8.1).
    push dx                     ; Drawn and then half-erased rather than
    mov ax, cx                  ; composed as two halves, because menu_bcell
    add cx, 3                   ; is a record of CELLS and half a cell is not
    call thm_bg
    mov bx, MENU_TEXT_Y
    mov dx, MENU_TEXT_Y + 7
    call gfx_fill
    pop dx
    pop bx
.nogap:
    mov si, bx
.s:
    or byte [menu_bcell+si], 0x80
    inc si
    cmp si, dx
    jbe .s
.rb:
    pop ax
    mov si, dx
    mov [menu_bcell+1+si], al

    jmp kret_di

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
    mov al, MB_ENTSZ            ; `mul bl` is byte by byte, so AL alone
    mul bl                      ; needs loading. BX <= MENU_BARMAX, so BL is
    add ax, menu_bar            ; the whole index; the base goes on before the
    xchg ax, si                 ; one-byte move into SI (size pass 10: 2).
                                ; (menu_bar is .lowbss: read through SS)
    cmp word [ss:si + MB_TITLE], 0
    je .out                     ; the logo cell has no text
    mov ax, [ss:si + MB_XL]
    add ax, MENU_TITLE_PAD / 2  ; the cell's pen x (SPEC.md 12.2: no MB_TX)
    call menu_bpad              ; spaces up to this cell's pen (SI it keeps)
    mov cx, [ss:si + MB_SEG]
    mov si, [ss:si + MB_TITLE]  ; read last, into the register it is used
    KES                         ; from: no AX bank (pass 14: 2 bytes)
                                ; this cell's own segment (SPEC.md 12.2)
    or cx, cx
    jz .seg
    mov es, cx
.seg:
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
    call menu_x2cell            ; left of the segment: nothing to pad
    call menu_bpadc
    pop cx
    pop ax
    ret

menu_bpadc:
    push ax
    push bx
    xchg ax, bx                 ; BX = the target cell
    mov al, ' '                 ; ...and AL the pad, once: menu_bput keeps it
.p:                             ; **THE ONE TERMINATOR IS LOCAL.** menu_bput
    cmp di, bx                  ; ADVANCES DI on every call, a dropped cell
    jae .d                      ; included (kernel size pass 14), so this
    call menu_bput              ; loop ends at its own target whatever
    jmp short .p                ; [menu_blim] does meanwhile. It used to
.d:                             ; drop WITHOUT advancing, which made any loop
    pop bx                      ; padding past the bound spin forever with the
    pop ax                      ; gfx lock held - SPEC.md 59.7, and 59.7.1
    ret                         ; when the bound MOVED under a re-read clamp:
                                ; a terminator another task can lower is a
                                ; livelock. A drop that advances cannot spin
                                ; any caller, so the clamp and its re-read
                                ; went (7 bytes; SPEC.md 59.7.2)

menu_bstr:
    push ax
    push si
.n:
    es lodsb                    ; SI is banked, so the step past the NUL is
    or al, al                   ; never seen (kernel size pass 10: 2 bytes)
    jz .d
    call menu_bput
    cmp di, [menu_blim]
    jb .n
.d:
    pop si
    pop ax
    ret

menu_bput:
    cmp di, [menu_blim]
    jae .same                   ; past the segment: dropped, exactly as
                                ; menu_layout drops a cell that will not fit -
                                ; and DI ADVANCES, so no loop calling this can
                                ; spin on a bound it cannot reach (SPEC.md
                                ; 59.7.2). Every caller's later use of DI is
                                ; bounded already: menu_bstr stops at the
                                ; bound, the tail's [menu_bend] is the min of
                                ; DI+1 and [menu_bn], and the clock, the strip
                                ; and the blanking after it reset or count it
    cmp [menu_bcell+di], al     ; DI-based: [di+disp16] is an 8086 mode, so
    je .same                    ; no BX copy and no bank of it (kernel size
    mov [menu_bcell+di], al     ; pass 10: 4 bytes)
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
    ret

; -----------------------------------------------------------------------------
; menu_ckend - the cell the clock's field ends at (SPEC.md 12.9, 34.15)
; out: AX = that cell, counted from MENU_TXT_X0
; clobbers: CX (the shift count), flags
;
; The bar's margin, or the speaker's left edge while the speaker shows - which
; is while a toast is not covering the clock ([toast_scov], SPEC.md 34.15).
; [vid_wm8] and not [menu_mixx]: the speaker is always the last 24 pixels
; before the margin, and this is asked before menu_sound has ever run.
; -----------------------------------------------------------------------------
menu_ckend:
    mov ax, [vid_wm8]
%ifdef OS88_SNDCARD
    cmp byte [toast_scov], 0
    jne menu_x2cell
    test byte [drv_svc+DSV_CAPS+1], SND_CAP_MIXER >> 8  ; the byte that
    jz menu_x2cell              ; holds the bit (pass 14: 1)
    sub ax, 24
%endif                          ; ...FALLS INTO menu_x2cell

; -----------------------------------------------------------------------------
; menu_x2cell - absolute bar x -> menu_bcell index (SPEC.md 12.9)
; in:  AX = x;  out: AX = (x - MENU_TXT_X0) / 8, 0 for an x left of the
;      segment;  clobbers: CL (left 3, which menu_inval spends), flags
;
; Five copies of `sub ax, MENU_TXT_X0 / mov cl, 3 / shr ax, cl` were the
; composition's x arithmetic (kernel size pass 14: menu_bpad, menu_inval,
; menu_ckend and menu_bar_text's prologue). The clamp is menu_bpad's and
; menu_inval's; the other two never pass an x left of the segment.
; -----------------------------------------------------------------------------
menu_x2cell:
    sub ax, MENU_TXT_X0
    jns .c
    xor ax, ax
.c:
    mov cl, 3
    shr ax, cl
    ret

; -----------------------------------------------------------------------------
; menu_track - run one full BAR menu interaction. Finds the title under the
;              mousedown, xor-highlights it, hands the drop to menu_drop, and
;              un-highlights the title on the way out.
; in:  caller holds the gfx lock
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
    mov cx, [mouse_x]           ; use the coordinate the pointer on the glass
                                ; uses. The reported 86Box PS/2 failure has
                                ; EV_A one position behind that pointer; the
                                ; rest of this tracker already polls mouse_x/y
                                ; directly, so mixing the two coordinate ages
                                ; makes a shifted press land on cell 0.
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
%ifdef KERN_BIG
    mov ax, [ss:si + MB_SEG]    ; THE STICKY APPS CELL (SPEC.md 110.12): the
    or ax, [ss:si + MB_NITEM]   ; kernel's own (MB_SEG 0, which no package's
    jnz .plain                  ; cell is) and the one of those with no items.
    mov bx, [ss:si + MB_XL]     ; Its panel is STIKMENU.DRV's, drawn and
    call COLD_SEG:stm_track_x   ; tracked there; AX = 0xFFFF back, because a
    jmp short .untitle          ; pick is posted and never dispatched
.plain:
%endif

    ; --- menu rect: width = widest item + 16, items are 16px cells -----------
    mov bx, [ss:si + MB_ITEMS]
    mov ax, [ss:si + MB_SEG]       ; the dropped menu's segment: this CELL's,
    mov [menu_dseg], ax         ; not the bar owner's (SPEC.md 12.2)
    mov ax, [ss:si + MB_NITEM]
    call menu_setup             ; the height clamp a bar menu never had: a
    xchg ax, cx                 ; package's set is not trusted for count any
                                ; more than for width (SPEC.md 12.2/2.2)
    call menu_widest            ; AX = widest item text, px (BX/CX in)
    add ax, 16
    mov bx, [ss:si + MB_XL]        ; the pull-down hangs off the title's left
    mov dx, MBAR_H              ; it always hangs from the bar
    mov cl, 1                   ; the LEFT button keeps a bar menu open
    call menu_pull              ; AX = item index, or 0xFFFF
.untitle:
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
                                ; (kern_big's parity pad here cancelled
                                ; instance.inc's before inst_minimize: kernel
                                ; size pass 14, tests/unit/t_hotpar.py)

; -----------------------------------------------------------------------------
; menu_popup - drop a menu anywhere on screen and track it while a button
;              is held (SPEC.md 12.4)
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
    call menu_setup             ; never trust a caller's count - the rect has
                                ; to fit on the screen (menu_relayout's rule),
                                ; and how many fit depends on the adapter:
                                ; 16 at 480 or 348 rows, 11 at CGA's 200
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

    ; --- height, and the upward shift off the bottom edge --------------------
    mov si, ax                  ; the width, across the height's arithmetic
    mov ax, [menu_cnt]
    mov cl, 4
    shl ax, cl                  ; count * 16 ...
    inc ax                      ; ... + the top and bottom frame rows
    inc ax
    mov cx, [vid_ph]            ; DX = candidate y1 (the anchor y)
    sub cx, ax                  ; CX = lowest y1 that still fits
    cmp dx, cx
    jle .y_hi
    mov dx, cx
.y_hi:
    cmp dx, MBAR_H              ; never over the menu bar: it is the one
    jge .y_lo                   ; surface no window may cover either
    mov dx, MBAR_H
.y_lo:
    xchg ax, si                 ; AX = the width again

    mov cl, 3                   ; EITHER button keeps a popup open. It was
                                ; 2, the right, when every popup was a
                                ; right-click's; the Floppy page's drop-downs
                                ; (SPEC.md 31.14) open on a LEFT press held
                                ; exactly as a bar menu's is, and 3 serves
                                ; both for the same five bytes. A right-click
                                ; menu is unchanged unless the left is ALSO
                                ; held, when it stays up until both are let go
    call menu_pull              ; AX = item index, or 0xFFFF
    cmp ax, 0xFFFF
    je .none
    clc
    jmp short .out
.none:
    mov ax, 0xFFFF
    stc
.out:
    jmp short menu_track.out    ; pop does not touch the flags, and the five
                                ; pops are menu_track's, in its order

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
; menu_setup - the two droppers' shared head: [menu_cnt] and [menu_iptr]
; in:  AX = the item count, BX = the item array
; out: AX = the count clamped to [vid_popmax], and stored; [menu_iptr] = BX
; clobbers: flags
;
; menu_track and menu_popup each spelled this out (kernel size pass 10: 10
; bytes); the clamp is the one that keeps a package's count off the bottom
; of the screen, so one copy of it is one place for it to be right.
; -----------------------------------------------------------------------------
menu_setup:
    cmp ax, [vid_popmax]
    jbe .ok
    mov ax, [vid_popmax]
.ok:
    mov [menu_cnt], ax
    mov [menu_iptr], bx
    ret


; -----------------------------------------------------------------------------
%ifdef OS88_COMPACT
; menu_reloc - the compactor moved the save-under (SPEC.md 66.2). One word,
; re-read at every use: menu_drop loads it to save, menu_undrop to restore, and
; nothing holds a pointer into it across the tracking loop's yields.
;
; ...ON kern_mega. Everywhere else [menu_sseg] is a mem_rr_tab ROW (memory.inc)
; and this name is the shared `ret` (kernel size pass 14): the row is four
; bytes against these eleven, and it is exact because the word is zeroed
; before every free (here, stm_shut's) and by mem_shed_one's own protocol
; (mem_pg_own), so it never holds a base that is not its own live claim.
%ifdef KERN_MEGA
menu_reloc:
    cmp bx, [menu_sseg]
    jne .out
    mov [menu_sseg], dx
.out:
    ret
%else
menu_reloc  equ mem_rr_none
%endif
%endif                          ; OS88_COMPACT

; -----------------------------------------------------------------------------
; menu_pull - lay out the dropped rect and run menu_drop over it
; in:  BX = x1, AX = width, DX = y1, CL = the [menu_btn] mask; [menu_cnt] set
; out: menu_drop's: AX = item index, or 0xFFFF
; clobbers: BX, CX (both callers have them banked)
;
; menu_track's and menu_popup's last eleven lines were one sum written twice:
; x2 = x1 + width - 1, y2 = y1 + count * 16 + 1 (the frame's top row; the
; bottom one is y2 itself), the button, and the call. FALLS INTO menu_drop
; (the %if under its body), so the call is the caller's.
; -----------------------------------------------------------------------------
menu_pull:
    mov [menu_btn], cl
    mov [menu_x1], bx
    add bx, ax
    dec bx
    mov [menu_x2], bx
    mov [menu_y1], dx
    mov ax, [menu_cnt]
    mov cl, 4
    shl ax, cl                  ; count * 16
    inc ax                      ; + the top frame row
    add ax, dx
    mov [menu_y2], ax
.fall:

menu_drop:
%if menu_drop - menu_pull.fall != 0
  %error "menu_pull falls into menu_drop: nothing may sit between them"
%endif
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- claim the save-under for as long as this menu is down ----------------
    xor ax, ax                  ; AX is dead on entry (menu_pull's last use is
    mov [menu_sseg], ax         ; [menu_y2]) and the arithmetic fills it next
    ; --- how big a save-under does THIS menu need? (SPEC.md 12.4/50.2) ------
    ; This was menu_save_kb, with this as its one caller: written here it
    ; costs no call, no return and no bank of BX, CX and DX, all three of
    ; which are reloaded below before anything reads them (kernel size pass
    ; 10: 12 bytes, with the planes multiplied straight out of memory).
    ; It reads [menu_x1]/[menu_x2]/[menu_y1]/[menu_y2] and leaves AX = the KB
    ; to claim, never 0 (mem_claim refuses that).
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
    mov ax, [menu_x2]
    mov cl, 3
    shr ax, cl                  ; the rect's last byte column...
    mov bx, [menu_x1]
    shr bx, cl                  ; ...and its first
    sub ax, bx
    inc ax                      ; span -> count, plus the misalignment byte
    inc ax
    mov bx, [menu_y2]
    sub bx, [menu_y1]
    inc bx                      ; BX = rows
    mul bx                      ; AX = bytes for one plane
%ifdef GFX_VGA
    mul word [vid_planes_w]     ; ...times the planes this adapter HAS: 4 on
                                ; VGA, 1 on Hercules and CGA (SPEC.md 39.2).
                                ; The _w twin, because vid_planes itself is a
                                ; BYTE and a word read of it takes the low
                                ; half of this very variable with it - 0x0404,
                                ; a thousand planes, and a claim bigger than
                                ; the fixed one it replaced. A VGA kernel's
%endif                          ; alone: kern_small's planes are always 1
                                ; (SPEC.md 39.27.1), and x1 is no multiply
                                ; (kernel size pass 13)
    call mem_bytes_kb           ; ...as KB, rounded UP, and never 0: a rect
                                ; this small still needs somewhere to go
    mov bx, MEM_P_MSAVE         ; TRIVIAL-purgeable (SPEC.md 12.4.2)
    call mem_claim
    jc .noclaim                 ; refused: the restore repaints instead
    mov [menu_sseg], dx
%ifdef OS88_COMPACT
    push ax                     ; ...and MOVABLE (SPEC.md 66.5.6). It is only
    mov ax, menu_reloc          ; 20KB and only lives for one menu - but that
    mov bx, MEM_P_MSAVE         ; is 20KB sitting mid-arena at exactly the
    call COLD_SEG:mmf_mem_movable ; moment a MENU COMMAND claims, which is one of
    pop ax                      ; the commonest ways a claim happens at all
%endif                          ; ...and on kern_small nothing moves (SPEC.md
                                ; 66.0), where this one is 3.0KB measured and
                                ; alive for exactly as long as a menu is down
.noclaim:

    ; --- save the pixels under the menu to the claimed buffer -----------------
    mov ax, [menu_sseg]
    or ax, ax
    jz .nosave                  ; no buffer: the restore repaints instead
    mov es, ax
    xor di, di
    call rect_get               ; the rect -> AX..DX, four adjacent words
    dw menu_x1
    call gfx_save
.nosave:

%ifdef DOCK_OPT
    ; --- A MENU IS OVER AN OPEN DOCK (SPEC.md 30.6.1) ------------------------
    ; The strip's hole and any region armed round it are put away while the
    ; menu is down and back at the release, before the restore - so a menu
    ; dropped over the open strip draws whole, and the refused-save repaint
    ; below goes round the strip again like everything else.
    xor ax, ax                  ; AX is free: everything below reloads it
    xchg ax, [gfx_hole]
    push ax
    xor ax, ax
    xchg ax, [wm_clip_n]
    push ax

%endif
    ; --- draw the menu: the chrome's ground, its frame, items 8px in ---------
    call thm_bg
    call rect_get               ; the rect -> AX..DX, four adjacent words
    dw menu_x1
    call gfx_fill
    call thm_ink
    call rect_get               ; the rect -> AX..DX, four adjacent words
    dw menu_x1
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
    inc bx
    inc bx
    add dx, MENU_ITEM_H
    loop .item
    pop es
.drawn:

    ; --- follow the mouse while the button is down ----------------------------
    mov word [menu_sel], 0xFFFF ; nothing highlighted yet
%ifdef OS88_THEME
    mov al, [thm_xorm]          ; the bar's XOR mask, ONCE per menu (SPEC.md
    mov [menu_xorm], al         ; 76.14.14): a theme change while it is down
%endif                          ; cannot make the off XOR disagree with the on
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
    xchg ax, [menu_sel]         ; highlight moved: erase old, draw new - the
    inc ax                      ; new one stored and the old one taken in ONE
    jz .no_old                  ; exchange, and 0FFFFh ("none") asked as `inc
    dec ax                      ; / jz` with the `dec` only where it is not
    call menu_item_xor          ; (kernel size pass 10: 3 bytes). Nothing reads
.no_old:                        ; [menu_sel] before the second test below, and
    mov ax, [menu_sel]          ; menu_item_xor preserves every register
    inc ax
    jz .same
    dec ax
    call menu_item_xor
.same:
                                ; no flush: the highlights are VRAM-direct and
                                ; nothing else draws while we hold the lock,
                                ; a transient overlay (SPEC.md 32)
    call kbm_pollm              ; ...and let the keyboard mouse release the
                                ; button it latched, or this loop never ends
                                ; on a machine with no mouse (SPEC.md 9.6)
    call task_yield             ; keep the scheduler healthy while we spin -
                                ; and this is where the VMware absolute pointer
                                ; is drained (SPEC.md 9.11.3), so a pull-down
                                ; tracks it too
    jmp .poll

    ; --- released: put the screen back, report the selection -------------------
.up:
%ifdef KERN_BIG
    call far [thv + THV_STICK]  ; WINDOWS (SPEC.md 76.14.7): a release that
                                ; ran nothing may leave the menu up for a
                                ; second click, and the answer comes back in
                                ; [menu_sel]. winmod.inc's door - cw_kretf's
                                ; bare `retf` at rest, so every other theme
                                ; releases here
%endif
%ifdef DOCK_OPT
    pop word [wm_clip_n]        ; the open dock's hole, back (see above)
    pop word [gfx_hole]
%endif
    mov byte [menu_dropd], 0    ; the arrows are a pointer again (SPEC 9.6.3).
                                ; The poll loop above has exactly this one way
                                ; out, so no path can leave the flag standing
    mov ax, [menu_sseg]         ; restore erases the menu and any highlight
    or ax, ax
    jz .repaint
    mov es, ax
    xor si, si
    call rect_get               ; the rect -> AX..DX, four adjacent words
    dw menu_x1
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
    call rect_get               ; which is the rect it drew and nothing else -
    dw menu_x1                  ; gfx_save captures exactly this, there is no
                                ; shadow outside it, and everything beyond it
                                ; is still correct on screen (SPEC.md 11.91)
    call wm_paint_dmg
.sel:
    xor dx, dx                  ; the menu is off the screen: hand the buffer
    xchg dx, [menu_sseg]        ; back rather than hold 20KB until the next
    or dx, dx                   ; one (SPEC.md 12.4/50.2). Taken and zeroed in
    jz .nofree                  ; ONE exchange rather than read, freed and then
    mov bx, MEM_P_MSAVE         ; stored 0 (kernel size pass 10: 4 bytes) -
    call mem_free               ; nothing between reads [menu_sseg], and a
.nofree:                        ; menu_reloc then finds no match, which is right
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

%ifndef KERN_BIG                ; (kern_big's is in wm.inc, above wm_hit_ttl_b)
menu_dropd  db 0                ; a pull-down is on the screen (SPEC.md 9.6.3).
                                ; In .text with a real initialiser rather than
                                ; in .bss, for fdlg_win's reason: -f bin zeroes
                                ; nothing and this is read on the machine's
                                ; very first arrow key
%endif

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
section .cold                   ; COLD (SPEC.md 2.6): the keyboard mouse's menu
                                ; step. It is 177 bytes of the 64KB segment for
                                ; a path that runs when somebody drives a
                                ; dropped menu from the keypad - once a
                                ; keystroke, never per pixel and never from an
                                ; ISR: kbm_move's caller is kbm_key, which
                                ; mouse.inc names "the UI task's keystroke".
                                ; Its two outbound calls go through cw_ shims
                                ; and it keeps the CF it answers with, `retf`
                                ; touching no flags
menu_kbnav:
    cmp byte [menu_dropd], 0
    je .noj                     ; nothing is dropped: an ordinary pointer
    or al, al
    jz .noj                     ; Left/Right keep their pixel step, which is
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
    mov dx, [mouse_x]           ; the same x test menu_hover makes, so the two
    cmp dx, [menu_x1]           ; can never disagree about what "in the menu"
    jb .beside                  ; means - and ASKED ONCE: DX carries the x the
    cmp dx, [menu_x2]           ; pointer will be placed at down to .place,
    jbe .inx                    ; where the same test was made a second time
                                ; (kernel size pass 10: 11 bytes). Nothing
                                ; between writes DX - cw_menu_item_dis banks
                                ; what it uses, and DX is this frame's
.beside:                        ; the pointer is off to one side: come back in
    mov dx, [menu_x1]           ; ...at the x where the item text starts, and
    add dx, 8
    mov di, -1                  ; at the end this key is heading AWAY from, so
    or si, si                   ; Down enters at the first item and Up at the
    jns .step                   ; last
    mov di, [menu_cnt]
    jmp short .step
.noj:                           ; ...and the two swallows above, at 2 bytes
    jmp .no                     ; each instead of NASM's inverted pair (-3)
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
    call KERNEL_SEG:cw_menu_item_dis          ; a greyed item or a separator is not a place
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
    mov ax, dx                  ; x is left alone while it is inside the menu,
                                ; so a pointer the user has put in a column
                                ; stays in it - and otherwise it is where the
                                ; item text starts (.beside)
    call KERNEL_SEG:cw_mou_clamp              ; the clamp the ISR itself uses, so the
    mov [mouse_x], ax           ; two-display seam is honoured by construction
    mov [mouse_y], bx           ; rather than by a rectangle known here
    mov byte [cur_shchk], 1     ; ...and the pointer MOVED (SPEC.md 7.2.1.1)
    inc byte [ui_post]          ; ...and SAY SO once (SPEC.md 13.13)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    retf                        ; FAR: kbm_move reaches this across the segment
.no:                            ; boundary now, and no thunk stands in between
    stc                         ; (SPEC.md 2.6.1)
    retf
section .text

; -----------------------------------------------------------------------------
; menu_hover - which item cell is the mouse inside right now?
; in:  [menu_x1]/[menu_x2]/[menu_y1]/[menu_cnt] set up by menu_drop's caller
; out: AX = item index, 0xFFFF if the mouse is outside the menu rect
; clobbers: nothing else
; -----------------------------------------------------------------------------
menu_hover:
    push cx
    pushf                       ; ONE READING OF THE POINTER (SPEC.md 12.4.1).
    cli                         ; mou_apply stores [mouse_x] and THEN
    mov cx, [mouse_x]           ; [mouse_y], so a packet decoded between two
    mov ax, [mouse_y]           ; plain loads hands this routine the OLD x
    popf                        ; with the NEW y - a position the pointer
                                ; never occupied. Everywhere else that would
                                ; be one wrong XOR frame; here menu_drop
                                ; LATCHES the answer in [menu_sel] and the
                                ; release RUNS it, and one Microsoft packet
                                ; carries the deltas and the button level
                                ; together - so the report that lets go is
                                ; exactly the one that can tear. Measured: a
                                ; single (-100, +18, up) packet off a title
                                ; launched the menu's first item 4 times in
                                ; 320, from two coordinates neither of which
                                ; is over it
    cmp cx, [menu_x1]
    jb .none
    cmp cx, [menu_x2]
    ja .none
    sub ax, [menu_y1]           ; rows above the first cell borrow out, and
    jbe .none                   ; the frame row ITSELF is not an item cell -
    dec ax                      ; which is the [menu_y1] + 1 this used to
                                ; add, spelled so that the y can stay in AX
                                ; and the routine still clobbers nothing but
                                ; it
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
; out: CF = 1 if its string starts with MENU_DIS (or is empty)
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
    cmp byte [es:si], MENU_DIS + 1  ; CF = 1 for a first byte BELOW 2: the
                                ; MENU_DIS mark, or an EMPTY item, which is a
                                ; blank row nothing could mean picking - so it
                                ; is skipped as a greyed one is, and the
                                ; stc/clc ladder this was goes (kernel size
                                ; pass 9; it is hover's, per pointer move)
%if MENU_DIS != 1
  %error "menu_item_dis tests MENU_DIS by `cmp ..., MENU_DIS + 1`: it must be 1, the smallest non-NUL byte"
%endif
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
    jmp kret_dx

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
    jmp kret_si

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
    mov di, si                  ; does it already fit? - a NUL somewhere in
    mov cx, MENU_MAXCH + 1      ; the first MENU_MAXCH+1 bytes, exactly
    xor al, al                  ; MENU_MAXCH characters fitting too. ONE
    repne scasb                 ; `repne scasb` (ES:DI, no override, so the
    je .fits                    ; restart hazard does not arise) is the scan
                                ; the byte loop and its tail compare were
    push ds                     ; no: copy the prefix into our own buffer.
    push es                     ; The source is ES:SI (the owner's) and the
    pop ds                      ; destination kernel .bss, so the copy runs
    KES                         ; with the segments SWAPPED for one `rep
                                ; movsb` - no override on it, for the same
    mov di, menu_tbuf           ; reason - and ES is left KERNEL_SEG, which
    mov cl, MENU_MAXCH          ; is the answer anyway. CX ran out to 0 above
    rep movsb
    pop ds
    mov [di], al                ; AL is still the scan's 0; DS is ours again
    mov si, menu_tbuf           ; (kernel size pass 10: 15 bytes)
.fits:
    pop di
    pop cx
    pop ax
    ret
%if MENU_MAXCH > 0xFF
  %error "menu_trunc loads MENU_MAXCH into CL alone"
%endif

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
    inc bx
    inc bx
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
    call kent_di                ; = push ax/bx/cx/dx/si/di
    mov si, menu_logo_bmp
    mov di, cx                  ; DI = left x
    mov dx, MENU_LOGO_Y + 1     ; rows 0 and 10 are blank and are not stored
    mov bh, 9                   ; rows
.row:
    lodsw                       ; bit 15 = leftmost pixel of the row
    mov cx, di
.col:
    shl ax, 1
    jnc .skip
    call gfx_pixel              ; CX = x, DX = y
.skip:
    inc cx
    or ax, ax                   ; a row ends at its last LIT pixel: the bits
    jnz .col                    ; past column 10 are 0, so no column count is
                                ; needed (kernel size pass 10: 6 bytes with
                                ; the two blank rows, and the empty right-hand
                                ; columns stop being walked)
    inc dx
    dec bh
    jnz .row
    jmp kret_di

; -----------------------------------------------------------------------------
; menu data: logo bitmap, strings, item lists, the menu table
; -----------------------------------------------------------------------------

; 11x11 DIP-chip silhouette - the os8088 logo: a 7px-wide package body with
; a notch at the top and four pins down each side. One word per row,
; bit 15 = x offset 0.
menu_logo_bmp:                  ; (row 0, ..........., is not stored)
    dw 0x3B80                   ; ..###.###..   body top, orientation notch
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..   body bottom
                                ; (nor row 10, ...........)

menu_loc_name   db 'Locator', 0 ; the kernel's own application (SPEC 12.3)
                                ; SEVEN of the items below are the SAME BYTES
                                ; as a title or a label somewhere else in the
                                ; kernel, and every one of those is .text =
                                ; KERNEL_SEG, so an `equ` is the whole of the
                                ; sharing - no shim, no indirection, and the
                                ; menu still reads as a list of its own items.
                                ; A menu item is a window title said twice by
                                ; construction: the item OPENS the window.
menu_s_file     equ fm_s_mfile          ; the Disk window's own File menu
%ifdef KERN_BIG
menu_s_stik     db 'Sticky Apps', 0     ; ...and the desktop's widgets (SPEC.md
                                ; 110.12): a title with NO items, which is
                                ; what menu_track hands to STIKMENU.DRV
%endif
                                ; (Its Builtins menu - Timer, Bounce, Disk -
                                ; is gone from both bars: SPEC.md 12.3.1 put
                                ; it in one place, 22.28 took it off the Disk
                                ; window's, and 14.8 made the first two items
                                ; packages, leaving a menu of one item that
                                ; duplicated a double-click)

menu_s_about    db 'About os8088...', 0
menu_s_ctrl     equ cp_ttl              ; the Control Panel's window title
menu_s_closex   db MENU_DIS     ; ...with nothing to close; ui_loc_gate points
menu_s_close    db 'Close Window', 0    ; the item at one or the other before
%if menu_s_close - menu_s_closex != 1
  %error "ui_loc_gate reaches menu_s_closex as menu_s_close - 1 (a `dec ax`)"
%endif
                                ; the menu can drop (SPEC.md 47). Two labels
                                ; over one `db`, fm_s_clonex's idiom: a
                                ; disabled item is MENU_DIS then the string,
                                ; so the live label is the tail of the greyed
                                ; one
menu_s_tasks    equ ui_s_tmttl          ; the Task Manager's window title
menu_s_restart  db 'Restart', 0
menu_s_shutdown db 'Shut Down...', 0
%ifdef KERN_BIG
menu_s_hiber    db 'Hibernate...', 0        ; SPEC.md 87.2: ui_loc_gate points
menu_s_hibernh  db MENU_DIS, 'Hibernate (No HD)', 0  ; item 4 at one of the
menu_s_hiberxm  db MENU_DIS, 'Hibernate (XMS)', 0    ; three, and the two
%ifdef VID_EXT
menu_s_hibervx  db MENU_DIS, 'Hibernate (VESA)', 0   ; ...and a fourth: the
                                ; VESA display cannot be carried in an image
                                ; (SPEC.md 120.4). Resident, as a menu string
                                ; must be; HIBER.DRV's hb_ok chooses it
%endif
                                ; greyed ones say WHY NOT (SPEC.md 47 rule 3)
%ifdef KERN_MEGA
menu_s_hibernr  db MENU_DIS, 'Hibernate (No room)', 0 ; protected mode: the pool
%endif                          ; cannot hold the picture (hb_ok_x)
menu_s_msep     db MENU_DIS, '-----------------', 0
                                ; the rule above Hibernate: seventeen dashes,
                                ; the width of 'Hibernate (No HD)', so it
                                ; reads as a rule across the menu rather than
                                ; as a short dash in the middle of one
%else
menu_s_msep     db MENU_DIS, '---------------', 0
                                ; the rule above Restart: fifteen dashes, the
                                ; width of 'About os8088...', so it reads as a
                                ; rule across the menu rather than as a short
                                ; dash in the middle of one
%endif

; The app-name menu's bottom item (SPEC.md 12.7), and the whole of the dock
; tile's context menu (SPEC.md 30.2) - dock_ctx_items points here. One
; string, so two menus that both say 'Close' can never come to say
; different things.
menu_s_appclose db 'Close', 0

; The System menu (SPEC.md 12.1), cell 0 of EVERY application - which is
; exactly why Restart lives here now: it is the one command every app should
; be able to reach, and it used to be reachable only from Locator's own bar
; and from a Disk window's (SPEC.md 12.3.1).
menu_cell0:                     ; cell 0, copied whole by menu_relayout -
    dw 0                        ; MB_TITLE: 0 = draw the logo glyph
    dw menu_items_logo          ; MB_ITEMS
    dw MENU_LOGO_N              ; MB_NITEM
    dw 0                        ; MB_XL
    dw MENU_SYS_XR              ; MB_XR
    dw 0                        ; MB_SEG: the System menu is the kernel's
%if ($ - menu_cell0) != MB_ENTSZ || MB_TITLE != 0 || MB_ITEMS != 2 || MB_NITEM != 4 || MB_XL != 6 || MB_XR != 8 || MB_SEG != 10
  %error "menu_cell0 is one MB_* record in field order: menu_relayout copies it with rep movsw"
%endif
menu_items_logo:
    dw menu_s_about
    dw menu_s_ctrl
    dw menu_s_tasks
    dw menu_s_msep
%ifdef KERN_BIG
    dw menu_s_hiber             ; item 4, swapped by ui_loc_gate (SPEC.md 87.2)
    dw menu_s_restart
    dw menu_s_shutdown
MENU_LOGO_N equ 7               ; ...and menu_relayout stamps this into cell
%else
    dw menu_s_restart
    dw menu_s_shutdown
MENU_LOGO_N equ 6               ; ...and menu_relayout stamps this into cell
%endif
                                ; 0's MB_NITEM. It was a literal 3 there, in
                                ; a different file from the list it counts
menu_items_file:
    dw menu_s_close
%ifdef OS88_SHORTCUTS
    dw sc_s_remx                ; item 1, swapped by ui_loc_gate (SPEC.md 26.8.5)
MENU_FILE_N equ 2
%else
MENU_FILE_N equ 1
%endif

; Locator's menu set (SPEC.md 12.3) - an ordinary SPEC.md 12.2 structure
; whose AM_ONCMD is 0, the one value reserved to mean "dispatched by the
; kernel": ui_dispatch recognises it and rebuilds a CMD_* instead of
; calling through. It is the set the bar falls back to when no window owns
; it; a file-manager window carries fm_menus instead, which shares this
; set's AM_NAME so the bar still reads 'Locator' either way (SPEC.md 12.3).
menu_loc_set:
    dw menu_loc_name            ; AM_NAME
    dw 0                        ; AM_ONCMD: kernel-dispatched
%ifdef KERN_BIG
    dw 2                        ; AM_COUNT: File and Sticky Apps (SPEC.md
%else                           ; 110.12) - THIS set's alone: a Disk window's
    dw 1                        ; bar does not carry it (22.28.1); File alone
%endif                          ; on kern_small (SPEC.md 14.6, 14.8)
    dw menu_s_file,    menu_items_file,    MENU_FILE_N
%ifdef KERN_BIG
    dw menu_s_stik,    0,                  0
%endif
                                ; (The Builtins menu held Timer,
                                ; Bounce and Disk; the first two are packages
                                ; on the apps disk now and the third is
                                ; reachable from the desktop's own drive zones
                                ; (SPEC.md 26.1) - so what was left was a menu
                                ; with one item that duplicates a double-click)

; -----------------------------------------------------------------------------
menu_s_abpfx db 'About ', 0     ; + the app's name -> menu_abstr (12.2)

section .lowbss                 ; LOW_SEG (SPEC.md 2.1): the runtime bar is
                                ; walked by pointer (menu_relayout writes it,
                                ; menu_draw_bar and menu_hit read it) and is
                                ; never addressed by name outside this module,
                                ; so it costs one ss: prefix per field access
                                ; and nothing in the kernel's 64KB segment
menu_bar:   resb 84             ; MENU_BARMAX x MB_ENTSZ (7 x 12; a literal
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
menu_x1:    resw 1              ; dropped menu rect, left. IN RECT ORDER
menu_y1:    resw 1              ; (x1,y1,x2,y2) since size pass 4, so menu_drop
                                ; reads it with rect_get: top - MBAR_H from the
                                ; bar, the shifted anchor from a popup (12.4)
menu_x2:    resw 1              ; dropped menu rect, right
menu_y2:    resw 1              ; dropped menu rect, bottom
RECT4 menu_x1, menu_y1, menu_x2, menu_y2 ; wm.inc's rect_get assertion, here
                                ; and not at wm.inc's foot: menu.inc is
                                ; included after it, so there these are forward
menu_cnt:   resw 1              ; item count of the tracked menu
menu_iptr:  resw 1              ; its array of near item-string pointers
menu_btn:   resb 1              ; mouse_btn mask that keeps it open: 1 = the
                                ; bar's left button, 2 = a popup's right one
menu_sel:   resw 1              ; highlighted item, 0xFFFF = none
%ifdef OS88_THEME
menu_xorm:  resb 1              ; [thm_xorm] as the menu dropped (76.14.14)
%endif
menu_seg:   resw 1              ; the segment [menu_set] and the app's own
                                ; strings live in; 0 = the kernel's own
                                ; (Locator, and any built-in window)
menu_dseg:  resw 1              ; ...and the segment of the menu being
                                ; DROPPED, which is that cell's MB_SEG: 0
                                ; for the System menu even under a package
%if menu_dseg - menu_seg != 2   ; ADJACENT ON PURPOSE: memory.inc's mem_rr_tab
    %error "menu_seg and menu_dseg must stay adjacent (mem_rr_tab names them as one row)"
%endif                          ; names the pair as ONE row of two words
menu_bdirty: resb 1             ; the MENUS segment's text may have moved. Set
                                ; by menu_relayout, by menu_inval and by
                                ; menu_force; spent by menu_bar_text, which
                                ; then usually finds nothing differs
menu_bovr:  resb 1              ; ...and somebody painted OVER the bar, so the
                                ; static furniture is owed too (menu_force).
                                ; A different question from the one above and
                                ; a far rarer one: this is the only flag that
                                ; erases anything (SPEC.md 12.9)
%if menu_bovr - menu_bdirty != 1  ; ADJACENT: menu_force and menu_init
%error "menu_bovr must follow menu_bdirty - menu_force sets both with one word store"
%endif
menu_bcell: resb MENU_BCMAX + 1 ; the bar's text as it is ON THE GLASS, one
                                ; byte per 8px cell from MENU_TXT_X0 - menus,
                                ; clock and toast strip - composed into,
                                ; diffed against and emitted from. 0 = not
                                ; known (menu_inval) or not ours (the widget's
                                ; cells while it shows). The spare cell is the
                                ; NUL menu_bemit plants past a run
menu_bn:    resw 1              ; the menus' cells on THIS screen (SPEC.md 39.2)
menu_bc0:   resw 1              ; the clock field's first cell: [vid_clk_hx]'s
menu_blim:  resw 1              ; menu_bput's bound - [menu_bn] for the menus,
                                ; the field's end for the clock and the strip
menu_bend:  resw 1              ; ...and the first cell past the last menu's
                                ; BOX - where the gap the toast strip prefers
                                ; begins (SPEC.md 59.9). Written by
                                ; menu_bar_text, read by toast_place only
menu_bfirst: resw 1             ; the span that differs, 0FFFFh = none
menu_blast: resw 1
menu_sseg:  resw 1              ; the save-under claim's segment, 0 = none
                                ; (menu_drop then repaints instead)
menu_tbuf   equ clk_str         ; menu_trunc's staging buffer. It was carved
                                ; out of the clock's field buffer, which went
                                ; when the clock moved into menu_bcell - and
                                ; since kernel size pass 14 it is the clock's
                                ; LINE buffer again, clock.inc's clk_str, at
                                ; no bytes of its own (25). The two lifetimes
                                ; cannot meet: clk_str is written by clk_fmt
                                ; alone and read straight back by the bar's
                                ; composition, under the gfx lock on the UI
                                ; task; menu_tbuf is written by menu_trunc and
                                ; read by the font_run_x three instructions
                                ; on, inside menu_drop's item loop - which
                                ; holds the lock on the UI task, so no
                                ; composition can run between the two. The
                                ; boot probe's scratch shares it too (clock.inc)
%if MENU_MAXCH + 1 > CLK_STR_LEN
  %error "menu_tbuf is clk_str: an item of MENU_MAXCH glyphs and its NUL no longer fit"
%endif
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
