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
    dw cp_s_snd,   cp_snd_paint,   cp_snd_click,   0
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

; --- Sound page (SPEC.md 31.4/34) --------------------------------------------
cp_s_snd:   db 'Sound', 0       ; list name AND the page heading
cp_s_sspk:  db 'Speaker', 0     ; tone route, row 0
cp_s_sopl:  db 'AdLib', 0       ; tone route, row 1 (arms in Phase 3)
cp_s_sexc:  db 'Exclusive clips', 0
cp_s_stst:  db 'Test', 0
cp_s_sano:  db 'AdLib: no', 0   ; caption, read live from the driver table
cp_s_sayes: db 'AdLib: yes', 0
cp_s_sfrz:  db 'Clips freeze GUI', 0   ; the trade, stated honestly (34.4)
cp_s_soff:  db 'Clips: off', 0

; The built-in test clip is the Recorder demo's 1 s sine sweep (SPEC.md
; 31.4/35): 400 Hz rising linearly to 800 Hz over the first half second
; and back down over the second, synthesised by cp_sndclip straight into
; SND_SEG (8-bit unsigned) and played at 8,000 Hz through slot 0x0080's
; target - which is what feeds the emitted/resync counters the caption
; below reports. Its own copy of the recorder's table (kernel code cannot
; call into a package): 128 + round(88*sin(2*pi*i/256)), range 40..216.
cp_sine:
    db 128,130,132,134,137,139,141,143,145,147,149,151,154,156,158,160
    db 162,164,166,168,169,171,173,175,177,179,180,182,184,185,187,189
    db 190,192,193,195,196,197,199,200,201,202,203,205,206,207,208,208
    db 209,210,211,212,212,213,213,214,214,215,215,215,216,216,216,216
    db 216,216,216,216,216,215,215,215,214,214,213,213,212,212,211,210
    db 209,208,208,207,206,205,203,202,201,200,199,197,196,195,193,192
    db 190,189,187,185,184,182,180,179,177,175,173,171,169,168,166,164
    db 162,160,158,156,154,151,149,147,145,143,141,139,137,134,132,130
    db 128,126,124,122,119,117,115,113,111,109,107,105,102,100, 98, 96
    db  94, 92, 90, 88, 87, 85, 83, 81, 79, 77, 76, 74, 72, 71, 69, 67
    db  66, 64, 63, 61, 60, 59, 57, 56, 55, 54, 53, 51, 50, 49, 48, 48
    db  47, 46, 45, 44, 44, 43, 43, 42, 42, 41, 41, 41, 40, 40, 40, 40
    db  40, 40, 40, 40, 40, 41, 41, 41, 42, 42, 43, 43, 44, 44, 45, 46
    db  47, 48, 48, 49, 50, 51, 53, 54, 55, 56, 57, 59, 60, 61, 63, 64
    db  66, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 85, 87, 88, 90, 92
    db  94, 96, 98,100,102,105,107,109,111,113,115,117,119,122,124,126
cp_swdir:   dw 1                ; sweep direction, +1 up / -1 down (re-armed
                                ; by cp_sndclip on every click)

cp_sndbtn_f: db 0               ; Test button look: 0 normal / 1 pressed
cp_sndbuf:  times 16 db 0       ; counters caption text ('E:nnnnn R:nnnnn');
                                ; .text data, not .bss - the panel stays
                                ; boot-clean with nothing to zero (SPEC.md 31)

; Sound page geometry, PANE-relative. Three control rows at a tighter
; 16px pitch than the two-row pages (the page holds more), the Test
; button beside them, two caption rows at the bottom.
CPS_R0Y   equ 20                ; tone route row 0 glyph top (Speaker)
CPS_ROWH  equ 16                ; control row pitch
CPS_R1Y   equ CPS_R0Y + CPS_ROWH        ; 36: row 1 glyph top (AdLib)
CPS_CBY   equ CPS_R1Y + CPS_ROWH        ; 52: checkbox glyph top
CPS_BTNX  equ 150               ; Test button, pane-relative rect
CPS_BTNY  equ 20
CPS_BTNW  equ 52
CPS_BTNH  equ 16
CPS_CAP1Y equ 74                ; status caption row
CPS_CAP2Y equ 88                ; counters caption row
CPS_CAP2X equ 92                ; caption 1's second column (the clip note)
CPS_TESTN equ 8000              ; test clip length: 1 s at 8,000 Hz (the
                                ; recorder demo's length, SPEC.md 35);
                                ; staged in a verb-7 grant since Phase 4
                                ; brought the allocator (SPEC.md 34.6)
CPS_INC400 equ 3277             ; sweep phase increment: 400*65536/8000
CPS_INC800 equ 6554             ; 800*65536/8000
; hit bands: generous and contiguous like the Scheduler page's (31.2)
CPS_B0Y1  equ CPS_R0Y - 4               ; 16: route row 0 top
CPS_B0Y2  equ CPS_R0Y + CP_GW - 1       ; 31: row 0 bottom = row 1 top - 1
CPS_B1Y2  equ CPS_R1Y + CP_GW - 1       ; 47: route row 1 bottom
CPS_CBY2  equ CPS_CBY + CP_GW - 1       ; 63: checkbox band bottom

; -----------------------------------------------------------------------------
; Everything above this line is DATA and stays in the kernel segment, because
; far code still reads it through DS (SPEC.md 33 rule 2). Everything below is
; code and lives in .fartext, outside the kernel's 64KB window. The two entry
; points the kernel dispatches to - the window template's paint and onclick
; procs - keep their near names here as 6-byte shims, so instance.inc's kind
; table and wm's `call [bx+W_PAINT]` are unchanged.
;
; Note that cp_items (above) holds .fartext offsets in its paint/click words.
; That is legal because the only code that calls through them, cp_page and
; cpf_onclick, is itself far and therefore runs with CS = FAR_SEG
; (SPEC.md 33 rule 5).
; -----------------------------------------------------------------------------
    FARSHIM cp_paint,   cpf_paint
    FARSHIM cp_onclick, cpf_onclick

section .fartext

; -----------------------------------------------------------------------------
; cpf_paint - W_PAINT for the Control Panel window (far body of cp_paint)
;
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] whatever the page left)
; The item list, the divider between the panes, then the selected item's
; page. DI/BP hold the content origin - the panel's own coordinate system,
; derived from the live window rect, never a screen constant - and every
; helper below takes it in the same pair.
; -----------------------------------------------------------------------------
cpf_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si                  ; wm_content takes the window in BX
    KCALL wm_content             ; AX = content left, DX = content top
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
    retf                        ; far body: reached through the cp_paint shim

; -----------------------------------------------------------------------------
; cpf_onclick - W_ONCLICK for the Control Panel window (far body)
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
cpf_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si                  ; wm_content takes the window in BX
    mov si, dx                  ; SI = click y; the window ptr is done with
    KCALL wm_content             ; AX = content left, DX = content top
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
    retf                        ; far body: reached through the cp_onclick shim

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
    KCALL gfx_fill

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
    KCALL gfx_fill
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
    KCALL font_str
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
    KCALL gfx_vline              ; in AX = x, BX = y1, DX = y2
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
    KCALL gfx_fill

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
    KCALL font_str

    mov si, cp_s_pre            ; row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PLDY
    KCALL font_str

    mov si, cp_s_coop           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    KCALL font_str

    mov si, cp_s_cap            ; caption: the flip is immediate
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    KCALL font_str

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
    KCALL sched_mode_get         ; AL = the live mode
    cmp al, ah
    je .done                    ; that row is already live: change nothing
    mov al, ah
    KCALL sched_mode_set         ; live on the next tick (SPEC.md 8.2)
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

    KCALL sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
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
    KCALL font_str

    mov si, cp_s_dbof           ; row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PLDY
    KCALL font_str

    mov si, cp_s_dbon           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    KCALL font_str

    mov si, cp_s_dcap           ; caption: what it costs...
    cmp byte [bb_avail], 0
    jne .cap
    mov si, cp_s_dna            ; ...or why the machine cannot have it
.cap:
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    KCALL font_str

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
    KCALL bb_set                 ; flips [bb_on]; we hold the lock as required
    call cp_dbradios            ; the two glyph boxes only

.done:
    pop ax
    ret

; =============================================================================
; Sound page (SPEC.md 31.4/34) - tone route, the exclusive-clips policy, a
; Test button, and the emitted/resync counters caption (the Phase 2 gate's
; instrument, SPEC.md 34.4).
;
; Everything is read live at paint time - the live tone route from
; snd_tone_route, presence from the driver table, the policy byte and the
; counters through DS - so the page keeps no state of its own and can
; never disagree with the router. The AdLib row obeys the bb_avail idiom
; three layers deep (SPEC.md 31.4): snd_route_set refuses an absent
; driver, the caption says 'AdLib: no', and the click is ignored outright.
;
; The Test button is the one sanctioned BLOCKING page click (SPEC.md 31.4):
; it synthesises the built-in clip into SND_SEG and plays it through slot
; 0x0080's target, which freezes the desktop for the clip - exactly the
; trade the caption discloses - and is what makes the counters caption a
; real instrument rather than decoration.
; =============================================================================

; -----------------------------------------------------------------------------
; cp_snd_paint - the Sound page (page contract of SPEC.md 31.1)
; in:  DI = pane left, BP = pane top; gfx lock held; entry pen is CWHITE
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
cp_snd_paint:
    push ax
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK ; entry pen is CWHITE (cp_page's erase)

    mov si, cp_s_snd            ; heading, same string as the list row
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    KCALL font_str

    mov si, cp_s_sspk           ; route row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CPS_R0Y + CP_PLDY
    KCALL font_str

    mov si, cp_s_sopl           ; route row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CPS_R1Y + CP_PLDY
    KCALL font_str

    mov si, cp_s_sexc           ; checkbox label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CPS_CBY + CP_PLDY
    KCALL font_str

    call cp_sndradios           ; route glyphs, filled per the live route
    call cp_sndchk              ; the policy checkbox
    mov al, 0
    call cp_sndbtn              ; the Test button, released look
    call cp_sndcap1             ; 'AdLib: yes/no' + the clip trade
    call cp_sndcap2             ; the emitted/resync counters

    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_snd_click - a click inside the Sound page
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x, DX = pane-
;      relative y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only)
;
; Comparisons SIGNED throughout (SPEC.md 31.2). The pane splits at x =
; CPS_BTNX: left of it the three control bands (y decides, the gutter's
; negative x included); at or right of it only the Test button rect. A
; click on the live route row does nothing; the AdLib row is ignored while
; no OPL2 is present; the checkbox toggles the policy byte and repaints
; its glyph + the caption stating the trade. The Test click BLOCKS for the
; clip (sanctioned, SPEC.md 31.4), then repaints the counters caption -
; the screendump-readable observable the Phase 2 gate reads.
; -----------------------------------------------------------------------------
cp_snd_click:
    push ax
    push bx
    push si

    cmp cx, CPS_BTNX
    jge .btncol

    cmp dx, CPS_B0Y1            ; above every band: nothing
    jl .done
    cmp dx, CPS_B0Y2
    jle .spk
    cmp dx, CPS_B1Y2
    jle .opl
    cmp dx, CPS_CBY2
    jg .done

    xor byte [snd_excl_ok], 1   ; the checkbox: flip the policy byte
    call cp_sndchk              ; (SPEC.md 34.4) and say what it means
    call cp_sndcap1
    jmp .done

.spk:
    KCALL snd_tone_route         ; BL = the live sink
    cmp bl, DRV_SPK
    je .done                    ; already the route: change nothing
    mov al, DRV_SPK
    mov bl, SND_TIER_TONE
    KCALL snd_route_set          ; cannot refuse: the speaker is always there
    call cp_sndradios
    jmp .done

.opl:
    test byte [snd_drv + SDRV_SIZE*DRV_OPL + SDRV_FLAGS], 1
    jz .done                    ; no OPL2: click ignored outright (31.4)
    KCALL snd_tone_route
    cmp bl, DRV_OPL
    je .done
    mov al, DRV_OPL
    mov bl, SND_TIER_TONE
    KCALL snd_route_set          ; presence just checked: stores
    call cp_sndradios
    jmp .done

.btncol:
    cmp cx, CPS_BTNX + CPS_BTNW - 1
    jg .done
    cmp dx, CPS_BTNY
    jl .done
    cmp dx, CPS_BTNY + CPS_BTNH - 1
    jg .done
    mov al, 1                   ; pressed look for the clip's duration -
    call cp_sndbtn              ; the desktop freezes with it (34.4)
    call cp_sndclip             ; synthesise + play; BLOCKS for the clip
    mov al, 0
    call cp_sndbtn
    call cp_sndcap2             ; the counters, freshly read

.done:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_sndradios - the tone-route glyph pair, filled per the LIVE route
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_sndradios:
    push ax
    push bx
    push cx
    push dx
    push si

    KCALL snd_tone_route         ; BL = the live sink (SI clobber saved)
    mov byte [gfx_color], CBLACK
    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CPS_R0Y

    mov si, cp_radio_on         ; --- row 0: Speaker ---
    cmp bl, DRV_OPL
    jne .r0
    mov si, cp_radio_off
.r0:
    call cp_glyph

    add dx, CPS_ROWH            ; --- row 1: AdLib ---
    mov si, cp_radio_off
    cmp bl, DRV_OPL
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
; cp_sndchk - the exclusive-clips checkbox glyph, from [snd_excl_ok]
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_sndchk:
    push ax
    push cx
    push dx
    push si
    mov byte [gfx_color], CBLACK
    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CPS_CBY
    mov si, cp_chk_off
    cmp byte [snd_excl_ok], 0
    je .draw
    mov si, cp_chk_on
.draw:
    call cp_glyph
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_sndbtn - draw the Test button
; in:  AL = 0 released / 1 pressed; DI = pane left, BP = pane top; lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = the label pen)
; -----------------------------------------------------------------------------
cp_sndbtn:
    push ax
    push bx
    push cx
    push dx
    push si

    mov [cp_sndbtn_f], al
    mov byte [gfx_color], CBLACK ; the frame is always black
    mov ax, di
    add ax, CPS_BTNX
    mov bx, bp
    add bx, CPS_BTNY
    mov cx, di
    add cx, CPS_BTNX + CPS_BTNW - 1
    mov dx, bp
    add dx, CPS_BTNY + CPS_BTNH - 1
    KCALL gfx_frame

    mov byte [gfx_color], CWHITE ; interior: white, inverted while pressed
    cmp byte [cp_sndbtn_f], 0
    je .fill
    mov byte [gfx_color], CBLACK
.fill:
    inc ax
    inc bx
    dec cx
    dec dx
    KCALL gfx_fill

    mov byte [gfx_color], CBLACK ; label: the interior's opposite
    cmp byte [cp_sndbtn_f], 0
    je .lbl
    mov byte [gfx_color], CWHITE
.lbl:
    mov si, cp_s_stst
    mov cx, di
    add cx, CPS_BTNX + 10       ; 'Test' = 32px, centred in the 52px box
    mov dx, bp
    add dx, CPS_BTNY + 4
    KCALL font_str

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_sndcap1 - status caption: AdLib presence + what clips cost
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
cp_sndcap1:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CWHITE ; erase the caption row first, so the
    mov ax, di                   ; checkbox toggle redraws in place
    mov bx, bp
    add bx, CPS_CAP1Y
    mov cx, di
    add cx, CP_CW - CP_RX - 1
    mov dx, bp
    add dx, CPS_CAP1Y + 9
    KCALL gfx_fill

    mov byte [gfx_color], CBLACK
    mov si, cp_s_sano           ; presence, read live from the table -
    test byte [snd_drv + SDRV_SIZE*DRV_OPL + SDRV_FLAGS], 1
    jz .have                    ; the caption layer of the bb_avail idiom
    mov si, cp_s_sayes
.have:
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPS_CAP1Y
    KCALL font_str

    mov si, cp_s_sfrz           ; the trade, stated honestly (34.4)...
    cmp byte [snd_excl_ok], 0
    jne .frz
    mov si, cp_s_soff           ; ...or that play now returns err 3
.frz:
    mov cx, di
    add cx, CPS_CAP2X
    KCALL font_str              ; same DX: one text row, two columns

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_sndcap2 - the counters caption: 'E:<emitted> R:<resync>'
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only)
; The Phase 2 gate's instrument (SPEC.md 34.4): after a Test clip the test
; asserts emitted ~= clip length and resync ~= 0 from a screendump.
; -----------------------------------------------------------------------------
cp_sndcap2:
    push ax
    push bx
    push cx
    push dx
    push si

    mov bx, cp_sndbuf           ; format into the .text scratch line
    mov byte [bx], 'E'
    inc bx
    mov byte [bx], ':'
    inc bx
    mov ax, [snd_pcm_emitted]
    call cp_putu16
    mov byte [bx], ' '
    inc bx
    mov byte [bx], 'R'
    inc bx
    mov byte [bx], ':'
    inc bx
    mov ax, [snd_pcm_resync]
    call cp_putu16
    mov byte [bx], 0

    mov byte [gfx_color], CWHITE ; erase, then draw - the redraw path
    mov ax, di                   ; after every Test click
    mov bx, bp
    add bx, CPS_CAP2Y
    mov cx, di
    add cx, CP_CW - CP_RX - 1
    mov dx, bp
    add dx, CPS_CAP2Y + 9
    KCALL gfx_fill

    mov byte [gfx_color], CBLACK
    mov si, cp_sndbuf
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPS_CAP2Y
    KCALL font_str

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_putu16 - unsigned 16-bit value to decimal text, no padding
; in:  AX = value, BX -> destination (DS)
; out: BX advanced past the last digit written
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
cp_putu16:
    push ax
    push cx
    push dx
    push si
    mov si, 10
    xor cx, cx
.div:
    xor dx, dx
    div si                      ; AX = quotient, DX = digit
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop dx
    add dl, '0'
    mov [bx], dl
    inc bx
    loop .emit
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_sndclip - synthesise the built-in test clip into SND_SEG and play it
; in:  nothing (DI/BP inputs unused); gfx lock held by the caller
; out: nothing; [snd_pcm_emitted]/[snd_pcm_resync] hold the clip's tale
; clobbers: nothing (flags only; [cp_swdir] is scratch)
;
; Takes a staging grant through the public verb-7 surface (SPEC.md 34.6 -
; the Phase 4 allocator; stamped with the Control Panel's own instance and
; freed on the way out), synthesises the sine sweep into it (direct ES
; writes - kernel code may hold an ES into SND_SEG, packages may not): the
; recorder demo's generator (SPEC.md 35), a 16-bit phase accumulator whose
; top byte indexes cp_sine and whose increment is Bresenham-stepped from
; CPS_INC400 to CPS_INC800 across the first half and back down the second
; (BP carries the error term, preserved like everything else). Then plays
; it at 8,000 Hz (N = 149, the default carrier) through slot 0x0080's
; target - the same path a package takes, ES pointing away from DS and
; all. The result codes are deliberately ignored: the counters caption
; reports what actually happened, which is the point of the button. A
; refused grant (pool full) plays nothing and the counters stay 0 - the
; same honest tale.
; -----------------------------------------------------------------------------
cp_sndclip:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov ax, 0x0007              ; verb 7 grant, sub-op 0 alloc
    mov cx, CPS_TESTN
    KCALL osapi_snd_stream       ; out AX = 0 + SI = grant offset
    or ax, ax
    jnz .out                    ; no grant, no clip: counters stay 0

    mov ax, SND_SEG
    mov es, ax
    mov di, si                  ; ES:DI = the grant
    push si                     ; the grant offset, back for play/free
    xor bx, bx                  ; BX = phase accumulator
    mov dx, CPS_INC400          ; DX = phase increment (400 Hz)
    xor bp, bp                  ; BP = Bresenham error accumulator
    mov word [cp_swdir], 1      ; first half sweeps upward
    mov cx, CPS_TESTN
    cld
.samp:
    mov al, bh                  ; top phase byte indexes the table
    xor ah, ah
    mov si, ax
    mov al, [cp_sine+si]        ; DS = KERNEL_SEG in far code (SPEC.md 33)
    stosb
    add bx, dx                  ; phase += increment
    add bp, CPS_INC800-CPS_INC400   ; err += span; overflow past the
    cmp bp, CPS_TESTN/2             ; half-length steps the increment
    jb .keep                        ; by the sweep direction
    sub bp, CPS_TESTN/2
    add dx, [cp_swdir]
.keep:
    cmp cx, CPS_TESTN/2 + 1     ; halfway: the sweep turns around
    jne .on
    neg word [cp_swdir]
.on:
    loop .samp
    pop si

    mov cx, CPS_TESTN           ; ES:SI = the grant, exactly as staged
    mov dx, 8000
    KCALL osapi_snd_play         ; BLOCKS for the clip; AX = 0/err ignored

    mov ax, 0x0107              ; verb 7 grant, sub-op 1 free (SI = offset)
    KCALL osapi_snd_stream
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    KCALL gfx_fill               ; in AX = x1, BX = y1, CX = x2, DX = y2
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
    KCALL gfx_pixel              ; in CX = x, DX = y, color [gfx_color]
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
; DATA, so back to the kernel segment - cp_glyph walks them through DS.
; -----------------------------------------------------------------------------
section .text


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

; cp_chk_off - open square (the Sound page's unchecked box, SPEC.md 31.4)
cp_chk_off:
    dw 0xFFF0                   ;  0  ############
    dw 0x8010                   ;  1  #..........#
    dw 0x8010                   ;  2  #..........#
    dw 0x8010                   ;  3  #..........#
    dw 0x8010                   ;  4  #..........#
    dw 0x8010                   ;  5  #..........#
    dw 0x8010                   ;  6  #..........#
    dw 0x8010                   ;  7  #..........#
    dw 0x8010                   ;  8  #..........#
    dw 0x8010                   ;  9  #..........#
    dw 0x8010                   ; 10  #..........#
    dw 0xFFF0                   ; 11  ############

; cp_chk_on - the same square with an X through it (checked)
cp_chk_on:
    dw 0xFFF0                   ;  0  ############
    dw 0xC030                   ;  1  ##........##
    dw 0xA050                   ;  2  #.#......#.#
    dw 0x9090                   ;  3  #..#....#..#
    dw 0x8910                   ;  4  #...#..#...#
    dw 0x8610                   ;  5  #....##....#
    dw 0x8610                   ;  6  #....##....#
    dw 0x8910                   ;  7  #...#..#...#
    dw 0x9090                   ;  8  #..#....#..#
    dw 0xA050                   ;  9  #.#......#.#
    dw 0xC030                   ; 10  ##........##
    dw 0xFFF0                   ; 11  ############
