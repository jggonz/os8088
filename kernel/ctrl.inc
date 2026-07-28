; =============================================================================
; os8088 - ctrl.inc
;
; The Control Panel window (SPEC.md 31): a two-pane browser. The LEFT pane
; is the list of panel items, one row each, the selected row drawn as a
; black bar with white text; the RIGHT pane is that item's settings page.
; Item 0 is 'Scheduler' - the scheduler mode (SPEC.md 8.2), "Pre-emptive"
; or "Cooperative", as a pair of radio rows - and is the selection at boot,
; so the panel opens showing it with no click needed.
;
; The two panes are joined only by the item table (cp_items, SPEC.md 31.1):
; one 8-byte record per item holding its list name and its page's paint and
; click procs. cp_paint and cp_onclick know nothing about scheduling; they
; draw the list, the divider, and whatever the selected record points at.
; Adding a second item is a table row plus a paint/click pair - no change to
; the pane machinery.
;
; Structurally still the simplest kind in the tree (SPEC.md 29): task-less,
; no KD_INIT, no pool, no .bss. Nothing exists at boot; an instance lives
; only while its window is open, and the kind's cap is 1. The one byte of
; panel state is cp_sel, the selected item, kept as initialised .text data
; (init 0 = Scheduler) so nothing has to be zeroed at boot; it persists
; across opens, which with one item is indistinguishable from resetting.
; The scheduler PAGE keeps no state at all: it reads the global mode live
; with sched_mode_get, so the panel can never disagree with the kernel (or
; with the About box and the Task Manager, which read the same byte) and a
; mode changed from anywhere else still paints correctly.
;
; Concurrency: both callbacks - and therefore every page proc they dispatch
; to - run on the UI task with the gfx lock ALREADY HELD by the caller
; (wm_draw_win for W_PAINT, ui.inc's dispatch for W_ONCLICK, SPEC.md 11/13),
; so they draw straight through the vga12/font primitives and must never
; call gfx_lock/gfx_unlock - the lock is not reentrant and re-acquiring it
; would deadlock the GUI (SPEC.md 7). They never block, never spawn, make no
; BIOS calls, and preserve every register. cp_onclick repaints in place and
; only what changed: a new selection redraws the two panes (each erases its
; own rectangle first), a mode flip redraws just the two 12x12 glyph boxes.
; Never wm_paint_all, never a whole-window redraw. Flipping the mode from
; inside a callback is safe - sched_mode_set is one guarded byte store and
; needs no rendezvous (SPEC.md 8.2). Other windows that show the mode (the
; About box's third line, SPEC.md 14) are already painted, so a flip also
; posts [cp_dirty] - ui_task's step 3 does the wm_paint_all outside the
; lock, exactly like inst_launch and ld_pending (SPEC.md 13).
;
; Everything is laid out from the window's live W_X/W_Y via wm_content, so
; the panel survives dragging (SPEC.md 31.1).
; =============================================================================

; --- content geometry, content-relative (pinned in SPEC.md 31.1) -------------
CP_CW     equ 318               ; content width  (frame 320 - 2 borders)
CP_CH     equ 101               ; content height (frame 120 - border - title)

; --- left pane: the item list -----------------------------------------------
CP_DIVX   equ 88                ; divider column: left pane is x 0..CP_DIVX-1
CP_IX     equ 6                 ; item label x
CP_I0Y    equ 6                 ; first item row: top
CP_IROWH  equ 14                ; row pitch (also the row's hit band height)
CP_IBH    equ 12                ; selection bar height inside the row
CP_ITDY   equ 2                 ; label baseline offset (8px font in 12px bar)
CP_IBX1   equ 2                 ; selection bar left edge
CP_IBX2   equ CP_DIVX - 3       ; 85: selection bar right edge

; --- right pane: the selected item's page -----------------------------------
CP_RX     equ 96                ; pane left edge (pane top = content top)

; --- Scheduler page, PANE-relative ------------------------------------------
CP_PMX    equ 2                 ; left margin for the heading and the caption
CP_PHY    equ 6                 ; 'Scheduler' heading text row
CP_PGX    equ 4                 ; radio glyph left edge
CP_GW     equ 12                ; radio glyph is 12x12
CP_PR0Y   equ 26                ; first radio row: glyph top
CP_PROWH  equ 20                ; row pitch: row i glyph top = CP_PR0Y + i*20
CP_PLX    equ CP_PGX + 18       ; 22: label text x
CP_PLDY   equ 2                 ; label baseline offset (8px font in 12px box)
CP_PCAPY  equ 74                ; caption text row

; --- radio hit bands, pane-relative y: generous, contiguous ------------------
CP_B0Y1   equ CP_PR0Y - 4                     ; 22: row 0 (Pre-emptive) top
CP_B0Y2   equ CP_PR0Y + CP_GW + 3             ; 41: row 0 bottom = row 1 top-1
CP_B1Y2   equ CP_PR0Y + CP_PROWH + CP_GW + 3  ; 61: row 1 (Cooperative) bottom

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
; No onkey: the panel is mouse-only. Content is 318x101.
cp_tpl:     dw 160, 130, 320, 120, cp_ttl, cp_paint, 0, cp_onclick
cp_ttl:     db 'Control Panel', 0
cp_sname:   db 'Control', 0     ; KD_NAME: fits the 7-char NAME column

; Deferred-repaint flag, drained by ui_task step 3 (SPEC.md 13/31.2). Inline
; .text data, not .bss, so it is initialised on disk and needs no cp_init -
; the osapi_seed trick from kernel.asm.
cp_dirty:   db 0                ; 1 = the mode changed, repaint everything

; Selected item, index into cp_items. Inline .text data for the same reason;
; 0 = Scheduler is the boot selection, so the panel opens on it.
cp_sel:     db 0

; --- item table: one 8-byte record per panel item (SPEC.md 31.1) ------------
; The 4th word is reserved (a future page onkey proc) and keeps the stride a
; power of two, so index -> record is three shl-by-1s and no CL.
CP_I_NAME   equ 0               ; -> list name, ASCIIZ
CP_I_PAINT  equ 2               ; -> page paint proc (in DI/BP = pane origin)
CP_I_CLICK  equ 4               ; -> page click proc (in DI/BP, CX/DX = rel)
CP_ISTRIDE  equ 8

cp_items:
    dw cp_s_sched, cp_sched_paint, cp_sched_click, 0
    dw cp_s_disp,  cp_disp_paint,  cp_disp_click,  0
cp_items_end:
CP_ITEMS  equ (cp_items_end - cp_items) / CP_ISTRIDE

cp_s_sched: db 'Scheduler', 0   ; list name AND the page heading
cp_s_pre:   db 'Pre-emptive', 0
cp_s_coop:  db 'Cooperative', 0
cp_s_cap:   db 'Takes effect immediately', 0

cp_s_disp:  db 'Display', 0     ; list name AND the page heading
cp_s_dbof:  db 'Direct to screen', 0
cp_s_dbon:  db 'Double buffered', 0
cp_s_dcap:  db 'Smoother; costs 150K', 0
cp_s_dna:   db 'Needs 500K of memory', 0

; -----------------------------------------------------------------------------
; cp_paint - W_PAINT for the Control Panel window
;
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] whatever the page left)
; The item list, the divider between the panes, then the selected item's
; page. DI/BP hold the content origin - the panel's own coordinate system,
; derived from the live window rect, never a screen constant - and every
; helper below takes it in the same pair.
; -----------------------------------------------------------------------------
cp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si                  ; wm_content takes the window in BX
    call wm_content             ; AX = content left, DX = content top
    mov di, ax                  ; DI = content left \ the panel's origin
    mov bp, dx                  ; BP = content top  /

    call cp_list                ; left pane
    call cp_divider             ; the rule between them
    call cp_page                ; right pane: the selected item's page

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_onclick - W_ONCLICK for the Control Panel window
;
; in:  CX = x, DX = y (ABSOLUTE screen coords), SI = window ptr
;      (gfx lock held by the caller, call already billed to the instance)
; out: nothing
; clobbers: nothing (flags only; [gfx_color] left wherever the redraw ended)
; Comparisons are SIGNED throughout - wm_hit dispatches the window's 1px
; border as content, so a content-relative coordinate can legitimately be
; negative or one past the content. A click left of the divider picks a list
; item: on a different one it becomes the selection and both panes are
; redrawn in place, on the live one (or in the empty space below the rows)
; nothing happens. A click right of the divider is handed to the selected
; item's page click proc in pane-relative coordinates; the divider column
; itself belongs to the page side and lands in its left gutter, which every
; page ignores. The lock is already held, so no gfx_lock/gfx_unlock and no
; wm_paint_all here (that would deadlock or recurse) - a page that needs
; other windows repainted posts a deferred flag instead (SPEC.md 31.2).
; -----------------------------------------------------------------------------
cp_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si                  ; wm_content takes the window in BX
    mov si, dx                  ; SI = click y; the window ptr is done with
    call wm_content             ; AX = content left, DX = content top
    mov di, ax                  ; DI = content left \ same origin pair as
    mov bp, dx                  ; BP = content top  / cp_paint's
    sub cx, di                  ; CX = content-relative click x
    mov dx, si
    sub dx, bp                  ; DX = content-relative click y

    cmp cx, CP_DIVX
    jge .page

    call cp_pick                ; --- left pane: which row? ---
    jc .done                    ; below the last row: nothing
    cmp al, [cp_sel]
    je .done                    ; already showing: change nothing
    mov [cp_sel], al
    call cp_list                ; move the bar...
    call cp_page                ; ...and swap the page under it
    jmp .done

.page:                          ; --- right pane: hand it to the page ---
    sub cx, CP_RX               ; CX = pane-relative x (negative in the
                                ; gutter between divider and pane)
    add di, CP_RX               ; DI = pane left; pane top = content top
    mov al, [cp_sel]
    call cp_entry               ; SI = the selected item's record
    call [si + CP_I_CLICK]      ; in DI/BP = pane origin, CX/DX = relative
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_entry - item index -> item record
;
; in:  AL = item index (0..CP_ITEMS-1)
; out: SI = record ptr
; clobbers: nothing else (flags only)
; CP_ISTRIDE is 8, so the multiply is three shl-by-1s: no CL to save, and no
; 8086-illegal shl by an immediate count.
; -----------------------------------------------------------------------------
cp_entry:
    push ax
    mov ah, 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, cp_items
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_pick - hit-test the item list
;
; in:  DX = content-relative click y
; out: CF = 0 and AL = item index if the click landed on a row,
;      CF = 1 (AL undefined) above the first row or below the last
; clobbers: nothing else
; Rows are contiguous: row i covers CP_I0Y + i*CP_IROWH for CP_IROWH rows,
; so the bar's 12 drawn rows plus the 2px gap under it all select the item.
; The x coordinate is not tested - the caller has already established that
; the click is in the left pane, and the whole pane width selects.
; -----------------------------------------------------------------------------
cp_pick:
    push dx
    sub dx, CP_I0Y
    jl .miss                    ; signed: above the first row
    xor al, al
.scan:
    cmp dx, CP_IROWH
    jl .hit
    sub dx, CP_IROWH
    inc al
    cmp al, CP_ITEMS
    jb .scan
.miss:
    stc                         ; below the last row: no item
    jmp .done
.hit:
    clc
.done:
    pop dx
    ret

; -----------------------------------------------------------------------------
; cp_list - draw the left pane: every item name, the selected one barred
;
; in:  DI = content left, BP = content top; caller holds the gfx lock
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK or CWHITE)
; Erases the whole pane first, so this doubles as the redraw path when the
; selection moves. BP is repurposed as the selected record's pointer once
; the first row's y has been computed from it - the loop then only needs a
; running y (DX) and a running record ptr (BX), which is what lets the two
; drawing calls (which preserve everything) sit inside it unguarded.
; -----------------------------------------------------------------------------
cp_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp

    mov byte [gfx_color], CWHITE ; erase the pane
    mov ax, di
    mov bx, bp
    mov cx, di
    add cx, CP_DIVX - 1
    mov dx, bp
    add dx, CP_CH - 1
    call gfx_fill

    mov dx, bp
    add dx, CP_I0Y              ; DX = row 0 top, screen y
    mov al, [cp_sel]
    call cp_entry
    mov bp, si                  ; BP = the record whose row is barred
    mov bx, cp_items            ; BX = the record being drawn
.row:
    cmp bx, bp
    jne .plain

    push ax                     ; --- selected: white text on a black bar ---
    push bx
    push cx
    push dx
    mov byte [gfx_color], CBLACK
    mov ax, di
    add ax, CP_IBX1
    mov cx, di
    add cx, CP_IBX2
    mov bx, dx                  ; y1 = row top
    add dx, CP_IBH - 1          ; y2
    call gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    mov byte [gfx_color], CWHITE
    jmp .text
.plain:
    mov byte [gfx_color], CBLACK
.text:
    mov si, [bx + CP_I_NAME]
    mov cx, di
    add cx, CP_IX
    push dx
    add dx, CP_ITDY
    call font_str
    pop dx

    add dx, CP_IROWH
    add bx, CP_ISTRIDE
    cmp bx, cp_items_end
    jb .row

    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_divider - the 1px rule between the two panes
;
; in:  DI = content left, BP = content top; caller holds the gfx lock
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_divider:
    push ax
    push bx
    push dx
    mov byte [gfx_color], CBLACK
    mov ax, di
    add ax, CP_DIVX
    mov bx, bp
    mov dx, bp
    add dx, CP_CH - 1
    call gfx_vline              ; in AX = x, BX = y1, DX = y2
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_page - draw the right pane: the selected item's page
;
; in:  DI = content left, BP = content top; caller holds the gfx lock
; out: nothing
; clobbers: nothing (flags only; [gfx_color] is left wherever the page ended)
; Erases the pane, then dispatches through the item record with DI moved to
; the pane's left edge (the pane top is the content top). Like cp_list, the
; erase is what makes this the redraw path when the selection moves.
; -----------------------------------------------------------------------------
cp_page:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [gfx_color], CWHITE ; erase the pane, divider column excluded
    mov ax, di
    add ax, CP_DIVX + 1
    mov bx, bp
    mov cx, di
    add cx, CP_CW - 1
    mov dx, bp
    add dx, CP_CH - 1
    call gfx_fill

    mov al, [cp_sel]
    call cp_entry               ; SI = the selected item's record
    add di, CP_RX               ; DI = pane left, BP = pane top
    call [si + CP_I_PAINT]

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Item 0: the Scheduler page
; =============================================================================

; -----------------------------------------------------------------------------
; cp_sched_paint - paint the Scheduler page (stateless)
;
; in:  DI = pane left, BP = pane top (pane already white-filled, lock held)
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; The heading, the two radio labels and the caption in black, then both
; radio glyphs with the live mode's one filled (SPEC.md 31.1).
; -----------------------------------------------------------------------------
cp_sched_paint:
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK ; entry pen is CWHITE (cp_page's erase)

    mov si, cp_s_sched          ; 'Scheduler' heading, same string as the
    mov cx, di                  ; list row that selects this page
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call font_str

    mov si, cp_s_pre            ; row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PLDY
    call font_str

    mov si, cp_s_coop           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    call font_str

    mov si, cp_s_cap            ; caption: the flip is immediate
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    call font_str

    call cp_radios              ; both glyphs, filled per the live mode

    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cp_sched_click - a click inside the Scheduler page
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x (ignored),
;      DX = pane-relative y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK when the mode
;           actually changed, untouched otherwise)
; The x coordinate is ignored: the two hit bands span the whole pane, so the
; glyph and its label both select the row, and so does the gutter left of
; the pane (SPEC.md 31.2). Comparisons are SIGNED - see cp_onclick. A hit on
; the row that is ALREADY live does nothing at all; a hit that changes the
; mode sets it, redraws ONLY the two glyphs and posts [cp_dirty] so ui_task
; repaints the windows that quote the mode (SPEC.md 13/14).
; -----------------------------------------------------------------------------
cp_sched_click:
    push ax

    cmp dx, CP_B0Y1             ; outside both bands (the heading above and
    jl .done                    ; the caption below included): nothing
    cmp dx, CP_B1Y2
    jg .done

    mov al, 1                   ; lower band = Cooperative
    cmp dx, CP_B0Y2
    jg .set
    mov al, 0                   ; upper band = Pre-emptive
.set:
    mov ah, al                  ; AH = the mode this row selects
    call sched_mode_get         ; AL = the live mode
    cmp al, ah
    je .done                    ; that row is already live: change nothing
    mov al, ah
    call sched_mode_set         ; live on the next tick (SPEC.md 8.2)
    mov byte [cp_dirty], 1      ; ui_task step 3 repaints the About box's
                                ; third line and any other mode-quoting
                                ; window, outside the lock (SPEC.md 13)
    call cp_radios              ; the two glyph boxes only
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_radios - draw both radio glyphs, the live mode's one filled
;
; in:  DI = pane left, BP = pane top; caller holds the gfx lock
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; Both glyphs are always drawn, so this doubles as the redraw path after a
; mode change: cp_glyph white-fills its own 12x12 box first, which is what
; lets cp_sched_click repaint in place without touching the rest of the page.
; -----------------------------------------------------------------------------
cp_radios:
    push ax
    push bx
    push cx
    push dx
    push si

    call sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
    mov bl, al                  ; BL = the row whose glyph is filled

    mov byte [gfx_color], CBLACK ; the pen cp_glyph draws the bitmap with:
                                ; set here, not inside cp_glyph, because the
                                ; click path's entry pen is whatever the last
                                ; drawing anywhere left behind
    mov cx, di
    add cx, CP_PGX              ; CX = glyph left x (same for both rows)
    mov dx, bp
    add dx, CP_PR0Y             ; DX = row 0 glyph top

    mov si, cp_radio_off        ; --- row 0: Pre-emptive ---
    cmp bl, 0
    jne .r0
    mov si, cp_radio_on
.r0:
    call cp_glyph

    add dx, CP_PROWH            ; --- row 1: Cooperative ---
    mov si, cp_radio_off
    cmp bl, 1
    jne .r1
    mov si, cp_radio_on
.r1:
    call cp_glyph

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Display page (SPEC.md 31.3) - double buffering on/off
;
; Same two-row radio geometry as the Scheduler page, so it shares cp_glyph
; and the CP_B*Y hit bands. Row 0 = direct to screen (the boot default),
; row 1 = double buffered.
; =============================================================================

; -----------------------------------------------------------------------------
; cp_disp_paint - the Display page
; in:  DI = pane left, BP = pane top; gfx lock held; entry pen is CWHITE
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
cp_disp_paint:
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK ; entry pen is CWHITE (cp_page's erase)

    mov si, cp_s_disp           ; heading, same string as the list row
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call font_str

    mov si, cp_s_dbof           ; row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PLDY
    call font_str

    mov si, cp_s_dbon           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    call font_str

    mov si, cp_s_dcap           ; caption: what it costs...
    cmp byte [bb_avail], 0
    jne .cap
    mov si, cp_s_dna            ; ...or why the machine cannot have it
.cap:
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    call font_str

    call cp_dbradios            ; both glyphs, filled per the live state

    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cp_dbradios - redraw the Display page's two radio glyphs from [bb_on]
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_dbradios:
    push ax
    push bx
    push cx
    push dx
    push si

    mov bl, [bb_on]             ; BL = the row whose glyph is filled
    mov byte [gfx_color], CBLACK ; the pen cp_glyph draws the bitmap with
    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CP_PR0Y

    mov si, cp_radio_off        ; --- row 0: direct to screen ---
    cmp bl, 0
    jne .r0
    mov si, cp_radio_on
.r0:
    call cp_glyph

    add dx, CP_PROWH            ; --- row 1: double buffered ---
    mov si, cp_radio_off
    cmp bl, 1
    jne .r1
    mov si, cp_radio_on
.r1:
    call cp_glyph

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_disp_click - a click inside the Display page
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x (ignored),
;      DX = pane-relative y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only)
;
; Same band logic and signed comparisons as cp_sched_click. bb_set wants the
; gfx lock held, which it is here, and does the real work: seeding the buffer
; from VRAM on the way in, flushing it on the way out. A machine that failed
; the RAM probe ignores the click entirely - the caption already says why -
; rather than moving a dot that bb_set would refuse to honour. No [cp_dirty]:
; no other window quotes this setting, and the buffer state is invisible.
; -----------------------------------------------------------------------------
cp_disp_click:
    push ax

    cmp dx, CP_B0Y1             ; outside both bands: nothing
    jl .done
    cmp dx, CP_B1Y2
    jg .done
    cmp byte [bb_avail], 0
    je .done                    ; not offered on this machine

    mov al, 1                   ; lower band = double buffered
    cmp dx, CP_B0Y2
    jg .set
    xor al, al                  ; upper band = direct to screen
.set:
    cmp al, [bb_on]
    je .done                    ; that row is already live: change nothing
    call bb_set                 ; flips [bb_on]; we hold the lock as required
    call cp_dbradios            ; the two glyph boxes only

.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_glyph - blit one 12x12 radio bitmap over a white box
;
; in:  CX = x, DX = y (top-left, screen px), SI -> 12 bitmap words, bitmap
;      color in [gfx_color]; caller holds the gfx lock
; out: nothing
; clobbers: nothing (flags only; [gfx_color] is restored)
; A menu_logo_glyph-style per-bit walk (bit 15 = leftmost pixel), preceded
; by a white gfx_fill of the glyph's own 12x12 box so a glyph can be
; redrawn in place. The caller's pen is saved across that fill and put back
; for the bit walk (SPEC.md 31.2), so this routine leaves no global behind.
; Written here rather than reused because menu_logo_glyph hardcodes both
; its top row (MENU_APPLE_Y) and its bitmap, and icon_draw16 wants a 16
; mask + 16 data word body (SPEC.md 25).
; -----------------------------------------------------------------------------
cp_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, cx                  ; DI = left x: each row's pen restarts here
    mov bx, dx                  ; BX = top y (gfx_fill preserves DX, but the
                                ; row loop needs BX for its counters)
    mov al, [gfx_color]         ; the caller's pen, back after the white box
    push ax
    mov byte [gfx_color], CWHITE
    mov ax, cx                  ; white box: (x,y) .. (x+11, y+11)
    add cx, CP_GW - 1
    add dx, CP_GW - 1
    call gfx_fill               ; in AX = x1, BX = y1, CX = x2, DX = y2
    pop ax
    mov [gfx_color], al

    mov dx, bx                  ; DX = pen y, back at the top row
    mov bh, CP_GW               ; BH = rows left
.row:
    lodsw                       ; AX = row bits, MSB = leftmost (DF=0 per
    mov cx, di                  ; SPEC.md 1); CX = pen x
    mov bl, CP_GW               ; BL = columns left
.col:
    shl ax, 1
    jnc .skip
    call gfx_pixel              ; in CX = x, DX = y, color [gfx_color]
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
; radio bitmaps: 12x12, one word per row, bit 15 = x offset 0 (the
; menu_logo_bmp convention). The low 4 bits of every word are unused.
; -----------------------------------------------------------------------------

; cp_radio_off - open ring (the mode that is NOT selected)
cp_radio_off:
    dw 0x0F00                   ;  0  ....####....
    dw 0x30C0                   ;  1  ..##....##..
    dw 0x4020                   ;  2  .#........#.
    dw 0x4020                   ;  3  .#........#.
    dw 0x8010                   ;  4  #..........#
    dw 0x8010                   ;  5  #..........#
    dw 0x8010                   ;  6  #..........#
    dw 0x8010                   ;  7  #..........#
    dw 0x4020                   ;  8  .#........#.
    dw 0x4020                   ;  9  .#........#.
    dw 0x30C0                   ; 10  ..##....##..
    dw 0x0F00                   ; 11  ....####....

; cp_radio_on - the same ring with a filled 4x4 centre dot (the live mode)
cp_radio_on:
    dw 0x0F00                   ;  0  ....####....
    dw 0x30C0                   ;  1  ..##....##..
    dw 0x4020                   ;  2  .#........#.
    dw 0x4020                   ;  3  .#........#.
    dw 0x8F10                   ;  4  #...####...#
    dw 0x8F10                   ;  5  #...####...#
    dw 0x8F10                   ;  6  #...####...#
    dw 0x8F10                   ;  7  #...####...#
    dw 0x4020                   ;  8  .#........#.
    dw 0x4020                   ;  9  .#........#.
    dw 0x30C0                   ; 10  ..##....##..
    dw 0x0F00                   ; 11  ....####....
