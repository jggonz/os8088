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
CP_CH     equ 121               ; content height (frame 140 - border - title;
                                ; the frame grew from 120 when the Date/Time
                                ; page took its two option rows - SPEC.md
                                ; 31.5 - rather than that page being cramped.
                                ; Every other page just has more white below.)

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
CP_PRGT   equ CP_CW - CP_RX - 1 ; ...and its LAST COLUMN, as an offset from
                                ; DI. A paint proc is handed DI = the PANE
                                ; left (cp_page adds CP_RX before dispatch),
                                ; so a line erase that reaches `di + CP_CW`
                                ; runs CP_RX pixels past the window's right
                                ; edge and paints the desktop

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
CP_PCAP2  equ CP_PCAPY + 9      ; 83: second caption row. Only the mono answer
                                ; uses it - the caption is the lowest thing on
                                ; every page, so a second 8px line fits inside
                                ; CP_CH (121) with room to spare

; --- radio hit bands, pane-relative y: generous, contiguous ------------------
CP_B0Y1   equ CP_PR0Y - 4                     ; 22: row 0 (Pre-emptive) top
CP_B0Y2   equ CP_PR0Y + CP_GW + 3             ; 41: row 0 bottom = row 1 top-1
CP_B1Y2   equ CP_PR0Y + CP_PROWH + CP_GW + 3  ; 61: row 1 (Cooperative) bottom

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
; No onkey: the panel is mouse-only. Content is 318x121.
cp_tpl:     dw 160, 130, 320, 140, cp_ttl, cp_paint, 0, cp_onclick
cp_ttl:     db 'Control Panel', 0
cp_sname:   db 'Control', 0     ; KD_NAME: fits the 7-char NAME column

; Deferred-repaint flag, drained by ui_task step 3 (SPEC.md 13/31.2). Inline
; .text data, not .bss, so it is initialised on disk and needs no cp_init -
; the osapi_seed trick from kernel.asm.
cp_dirty:   db 0                ; 1 = the mode changed, repaint everything
cp_wdirty:  db 0                ; 1 = a setting changed and SYSTEM.CFG is owed
                                ; a write. Deferred to the panel's teardown
                                ; (cp_flush_close) rather than taken on the
                                ; click, so a session of fiddling costs ONE
                                ; floppy write instead of one per click - and
                                ; a floppy write is ~a second on the floor
                                ; machine, during which the panel is frozen.
                                ; .text and not .bss, like everything else
                                ; here: nothing zeroes .bss (-f bin)
cp_dsave:   db 0                ; 1 = the last SYSTEM.CFG write failed, and
                                ; the Drivers page's caption says so (51.5)

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
    dw cp_s_time,  cp_time_paint,  cp_time_click,  0
    dw cp_s_drv,   cp_drv_paint,   cp_drv_click,   0
    dw cp_s_snd,   cp_snd_paint,   cp_snd_click,   0
cp_items_end:
CP_ITEMS  equ (cp_items_end - cp_items) / CP_ISTRIDE
CP_ITIME  equ 2                 ; the Date/Time item: ui.inc selects it by
                                ; name when the menu bar clock is clicked
                                ; (SPEC.md 12.1/31.5)
CP_IDRV   equ 3                 ; ...and the Drivers item, which drv_notice
                                ; opens the panel on when a driver did not
                                ; load at boot (SPEC.md 51.3)
CP_ISND   equ 4                 ; ...and Sound, the tone tier's route (34.8)

; List names are at most 9 characters (72px): the selection bar ends at
; CP_IBX2 = 85 and the name starts at CP_IX = 6, so a tenth glyph would
; cross the divider. 'Scheduler' and 'Date/Time' are both exactly at it.

cp_s_sched: db 'Scheduler', 0   ; list name AND the page heading
cp_s_pre:   db 'Pre-emptive', 0
cp_s_coop:  db 'Cooperative', 0
cp_s_cap:   db 'Takes effect immediately', 0

cp_s_disp:  db 'Display', 0     ; list name AND the page heading
cp_s_dbof:  db 'Direct to screen', 0
cp_s_dbon:  db 'Double buffered', 0
cp_s_dcap:  db 'Smoother; costs 150K', 0
cp_s_dna:   db 'Not Enough Ram', 0          ; the heap cannot fund 150KB
                                            ; RIGHT NOW - it is a claim, not
                                            ; a reservation, so this can
                                            ; change while the panel is open
                                            ; (close an app and it comes back)
                                            ; A 1bpp adapter draws straight to
                                            ; the framebuffer and has no buffer
                                            ; to double (SPEC.md 39.5) - a
                                            ; memory answer would be a lie on a
                                            ; 640K Hercules machine. It says
                                            ; WHY rather than just "no": the
                                            ; software renderer IS the direct
                                            ; path there, so there is nothing
                                            ; to double rather than no room.
                                            ; Two lines because the pane is 27
                                            ; characters wide (CP_PRGT-CP_PMX
                                            ; = 219px) and this is 49; the
                                            ; caption is the lowest thing on
                                            ; the page, so the second line has
                                            ; room under it.
cp_s_dmono: db 'Framebuffer is the display', 0
cp_s_dmono2: db 'driver on this adapter', 0

; --- Date/Time page (SPEC.md 31.5/37) ----------------------------------------
cp_s_time:  db 'Date/Time', 0   ; list name AND the page heading
cp_s_tcap:  db 'Click a field, then + or -', 0

; --- Drivers page (SPEC.md 31.6/51) ------------------------------------------
; One row per drv_tab row: a checkbox, the driver's name, and - in the caption
; - what the last load attempt actually answered. The click loads or unloads
; ON THE SPOT and then writes SYSTEM.CFG, so the box is never a promise about
; the next boot: what it shows is what is running right now.
cp_s_drv:   db 'Drivers', 0     ; list name AND the page heading
cp_s_dsave: db 'Cannot save to the system disk', 0

; --- Sound page (SPEC.md 31.7/34.8) ------------------------------------------
; The three row labels are FIXED and name TIERS, not whatever a driver called
; itself. The row used to be labelled from the driver's own DSV_NAME, which on
; a Sound Blaster read 'Sound Blaster' with the DSP tier up and 'AdLib' with it
; down - the label moved as the setting moved, so the one control on the page
; could not be read as a choice between two things.
cp_s_snd:   db 'Sound', 0       ; list name AND the page heading
cp_s_sspk:  db 'PC Speaker', 0
cp_s_sfm:   db 'AdLib', 0
cp_s_ssb:   db 'Sound Blaster', 0
cp_s_stest: db 'Test', 0
; The Date/Time page names the RTC ladder tier that answered (SPEC.md 37.90),
; not just yes/no: on a machine whose clock will not hold a setting, which of
; the four answered is the whole diagnosis, and there is nowhere else to see
; it. **Indexed by [clk_tier], i.e. by the CLK_T_* enum - NOT by the order
; clk_probe tries them in, which is a different order.**
cp_s_tnrt:  db 'Hardware clock: none', 0
cp_s_trt1:  db 'Hardware clock: AT 70h', 0
cp_s_trt2:  db 'Hardware clock: 58167 2C0h', 0
cp_s_trt3:  db 'Hardware clock: 5C01 2C0h', 0
cp_s_trt4:  db 'Hardware clock: BIOS', 0
cp_rtcnam:  dw cp_s_tnrt, cp_s_trt1, cp_s_trt2, cp_s_trt3, cp_s_trt4
cp_s_t12:   db '12-hour clock', 0       ; option row 0 label
cp_s_tsec:  db 'Seconds in menu bar', 0 ; option row 1 label
cp_s_tplus: db '+', 0
cp_s_tminus: db '-', 0
cp_s_tcol:  db ':', 0

; Date/Time geometry, PANE-relative: the date row above the time row, the
; +/- buttons in their own column to the right of both, and the clock's two
; display options below (SPEC.md 31.5/37).
CPT_FX    equ 8                 ; field text left x
CPT_DY    equ 26                ; date row text top
CPT_TY    equ 48                ; time row text top
CPT_MONW  equ 24                ; 'Mmm' field width
CPT_NUMW  equ 16                ; 'DD' / 'HH' field width
CPT_YRW   equ 32                ; 'YYYY' field width
CPT_BX    equ 150               ; button column left
CPT_BW    equ 28                ; button size
CPT_BH    equ 18
CPT_BUY   equ 22                ; '+' button top
CPT_BDY   equ 44                ; '-' button top
CPT_O0Y   equ 68                ; option row 0 glyph top ('12-hour clock')
CPT_O1Y   equ 84                ; option row 1 glyph top ('Seconds in...')
CPT_CAP1Y equ 102               ; instruction caption row
CPT_CAP2Y equ 112               ; hardware-clock caption row

; Option hit bands: the whole pane width, generous and contiguous like the
; other pages' (SPEC.md 31.2), and starting BELOW the '-' button's band.
CPT_OB0Y1 equ CPT_O0Y - 4                   ; 64: row 0 top
CPT_OB0Y2 equ CPT_O1Y - 3                   ; 81: row 0 bottom = row 1 top-1
CPT_OB1Y2 equ CPT_O1Y + CP_GW + 3           ; 99: row 1 bottom

; The editable fields, {x, w, y, pad} - stride 4, so index -> record is two
; shl-by-1s. The INDEX IS the field number clk_fld_str/clk_fld_adj take
; (SPEC.md 37), which is what keeps this page free of any knowledge of what
; a month or an hour actually is. The LAST entry exists only in 12-hour
; mode: the active count is 6 + [clk_h12], and both the draw loop and the
; hit test take their bound from it.
cp_tflds:
    db CPT_FX,    CPT_MONW, CPT_DY, 0       ; 0 month  'Mmm'
    db CPT_FX+32, CPT_NUMW, CPT_DY, 0       ; 1 day    'DD'
    db CPT_FX+56, CPT_YRW,  CPT_DY, 0       ; 2 year   'YYYY'
    db CPT_FX,    CPT_NUMW, CPT_TY, 0       ; 3 hour   'HH'
    db CPT_FX+24, CPT_NUMW, CPT_TY, 0       ; 4 minute 'MM'
    db CPT_FX+48, CPT_NUMW, CPT_TY, 0       ; 5 second 'SS'
    db CPT_FX+72, CPT_NUMW, CPT_TY, 0       ; 6 AM/PM  - 12-hour mode only
cp_tflds_end:
CPT_NFLD  equ (cp_tflds_end - cp_tflds) / 4

cp_tsel:  db 0                  ; selected field; initialised .text data like
                                ; cp_sel, so the panel stays boot-clean
cp_tendp: dw 0                  ; cp_time_pick scratch: one past the last
                                ; ACTIVE field record

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
; Everything above this line is DATA and everything below is code, and both
; are ordinary .text in the kernel's own segment. This module used to live in
; .fartext, copied down to a segment of its own at boot so its ~1,800 bytes
; would not count against the kernel's 64KB window; SPEC.md 33 records why
; that was retired - the mechanism reserved 10,752 bytes of low memory to
; hold a 5,455-byte blob, so it cost more RAM than it saved as soon as the
; whole footprint became the number being steered by.
; -----------------------------------------------------------------------------
cp_tick_due:
    push ax
    push bx
    push di
    cmp byte [cp_sel], CP_ITIME
    jne .no
    mov al, KIND_CTRL
    call inst_find_kind         ; CF = 1: no live Control Panel
    jc .no
    mov bx, [di+I_WIN]
    test word [bx+W_FLAGS], 2   ; visible?
    jz .no
    stc
    jmp .out
.no:
    clc
.out:
    pop di
    pop bx
    pop ax
    ret

section .text

; -----------------------------------------------------------------------------
; cp_paint - W_PAINT for the Control Panel window (far body of cp_paint)
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
; cp_onclick - W_ONCLICK for the Control Panel window (far body)
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
    mov byte [cp_wdirty], 1     ; ...and remembered on close (31.8)
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

    call cpf_dbok               ; CF = 1: the option cannot be taken now, so
    call gfx_pen_cf             ; its label takes the disabled pen (SPEC.md
                                ; 31.3/46). One call, because the predicate
                                ; already answers in CF and the pen is two
                                ; pieces of state that must not disagree
    mov si, cp_s_dbon           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    call font_str
    call gfx_pen_live

    mov si, cp_s_dcap           ; caption: what it costs...
    call cpf_dbok
    jnc .cap
    mov si, cp_s_dna            ; ...or why it cannot be had right now:
    cmp byte [vid_mono], 0      ; no room in the heap, or the wrong adapter
    je .cap
    mov si, cp_s_dmono
.cap:
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    call font_str
    cmp si, cp_s_dmono          ; ...and that one answer runs to a second line.
    jne .capdone                ; font_str clobbers nothing, so SI still names
    mov si, cp_s_dmono2         ; the string that was drawn
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAP2
    call font_str
.capdone:

    call cp_dbradios            ; both glyphs, filled per the live state

    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cpf_dbok - can double buffering be turned on right now? (far, internal)
; in:  nothing
; out: CF = 0 yes, CF = 1 no (wrong adapter, or the heap has no 150KB run)
; clobbers: nothing (flags)
;
; Three layers, the bb_avail idiom of SPEC.md 31.3: this is the question the
; caption answers, the label greys on and the click refuses on - so all three
; agree by construction rather than by three copies of the test.
; -----------------------------------------------------------------------------
cpf_dbok:
    cmp byte [bb_avail], 0
    je .no                      ; the adapter can never do it
    cmp byte [bb_dbl], 0
    jne .yes                    ; already armed: the memory is ours
    push ax
    push bx
    call mem_avail             ; AX = largest free run, KB
    cmp ax, BB_KB
    pop bx
    pop ax
    jb .no
.yes:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cp_dbradios - redraw the Display page's two radio glyphs from [bb_dbl]
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
;
; Row 1's glyph takes cpf_dbok's pen, the same one cp_disp_paint gives its
; label (SPEC.md 47 rule 2). It was drawn in CBLACK here while the label
; beside it went grey, so a row the click band refuses still showed a
; full-strength ring - which reads as a live control with faint writing next
; to it. The caption saying 'Not Enough Ram' was carrying the whole message
; on its own.
; -----------------------------------------------------------------------------
cp_dbradios:
    push ax
    push bx
    push cx
    push dx
    push si

    mov bl, [bb_dbl]            ; BL = the row whose glyph is filled. NOT
                                ; [bb_on], which is 1 on a mono adapter and
                                ; means only "software renderer" there
    mov byte [gfx_color], CBLACK ; the pen cp_glyph draws the bitmap with
    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CP_PR0Y

    mov si, cp_radio_off        ; --- row 0: direct to screen, always live ---
    cmp bl, 0
    jne .r0
    mov si, cp_radio_on
.r0:
    call cp_glyph

    add dx, CP_PROWH            ; --- row 1: double buffered ---
    call cpf_dbok               ; ...which is the row that can be refused
    call gfx_pen_cf
    mov si, cp_radio_off
    cmp bl, 1
    jne .r1
    mov si, cp_radio_on
.r1:
    call cp_glyph
    call gfx_pen_live

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpf_disp_cap - redraw the Display page's caption line only (far, internal)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
cpf_disp_cap:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [gfx_color], CWHITE    ; erase BOTH caption lines first - the
    mov ax, di                      ; strings differ in length AND in line
    add ax, CP_PMX                  ; count, so erasing only the first would
    mov bx, bp                      ; leave the mono answer's second line
    add bx, CP_PCAPY                ; stranded under a one-line caption
    mov cx, di
    add cx, CP_PRGT
    mov dx, bx
    add dx, CP_PCAP2 - CP_PCAPY + 7
    call gfx_fill
    mov byte [gfx_color], CBLACK
    mov si, cp_s_dcap
    call cpf_dbok
    jnc .draw
    mov si, cp_s_dna
    cmp byte [vid_mono], 0
    je .draw
    mov si, cp_s_dmono
.draw:
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    call font_str
    cmp si, cp_s_dmono              ; same two-line answer as the full paint
    jne .done                       ; (SPEC.md 47 rule 6: a partial redraw
    mov si, cp_s_dmono2             ; path draws what the full paint would)
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAP2
    call font_str
.done:
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

    mov al, 1                   ; lower band = double buffered
    cmp dx, CP_B0Y2
    jg .set
    xor al, al                  ; upper band = direct to screen
.set:
    cmp al, [bb_dbl]
    je .done                    ; that row is already live: change nothing
    or al, al
    jz .doit
    call cpf_dbok               ; arming: refuse if it cannot be funded, and
    jc .done                    ; leave the dot where it is (the caption and
.doit:                          ; the greyed label already say why)
    call bb_set                ; flips both flags; we hold the lock
    mov byte [cp_wdirty], 1     ; ...and remembered on close (31.8)
    call cp_dbradios            ; the two glyph boxes only
    call cpf_disp_cap           ; the caption can change with the claim:
                                ; turning it OFF hands 150KB back, which may
                                ; un-grey it for the next machine state

.done:
    pop ax
    ret

; =============================================================================
; Date/Time page (SPEC.md 31.5) - the editor for the system clock of
; SPEC.md 37, and what a click on the menu bar clock opens (SPEC.md 12.1).
;
; Two rows of fields - the date above, the time below - one of which is
; selected, plus a '+' / '-' button pair that steps the selected field. The
; page keeps exactly one byte of state (cp_tsel, which field is selected):
; the values themselves are always read live through clk_fld_str, so the
; page can no more disagree with the clock than the Scheduler page can with
; sched_mode_get. Nothing here knows what a month is - clk_fld_adj owns the
; ranges, the wrap and the day clamp - and nothing here calls BIOS: writing
; the hardware RTC is ui_task's job, off the [clk_dirty] flag (SPEC.md 13).
; =============================================================================

; -----------------------------------------------------------------------------
; cp_time_paint - the Date/Time page (page contract of SPEC.md 31.1)
; in:  DI = pane left, BP = pane top; gfx lock held; entry pen is CWHITE
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
cp_time_paint:
    push ax
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK ; entry pen is CWHITE (cp_page's erase)

    mov si, cp_s_time           ; heading, same string as the list row
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call font_str

    call cp_time_rows           ; both field rows, from the live clock

    mov al, 0                   ; the '+' button...
    call cp_timebtn
    mov al, 1                   ; ...and the '-' button
    call cp_timebtn

    mov byte [gfx_color], CBLACK ; the two option labels; their glyphs are
    mov si, cp_s_t12            ; drawn by cp_timechk, which is also the
    mov cx, di                  ; redraw path when one is toggled
    add cx, CP_PLX
    mov dx, bp
    add dx, CPT_O0Y + CP_PLDY
    call font_str
    mov si, cp_s_tsec
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CPT_O1Y + CP_PLDY
    call font_str
    call cp_timechk

    mov byte [gfx_color], CBLACK
    mov si, cp_s_tcap           ; caption 1: how the page is worked
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPT_CAP1Y
    call font_str

    mov si, cp_s_tnrt           ; caption 2: read live from the probe's
    cmp byte [clk_rtc], 0       ; verdict (SPEC.md 37.90) - informative only,
    je .cap                     ; since editing works either way
    push bx
    mov bl, [clk_tier]
    xor bh, bh
    add bx, bx
    mov si, [cp_rtcnam+bx]
    pop bx
.cap:
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPT_CAP2Y
    call font_str

    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_time_click - a click inside the Date/Time page
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x, DX = pane-
;      relative y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only)
;
; Comparisons SIGNED throughout (SPEC.md 31.2). Tested in THIS ORDER,
; because the option rows own the full pane width while the buttons own
; only their column: the two option bands first, then the button column,
; then the field bands. A step goes through clk_fld_adj, which also posts
; [clk_dirty] and [clk_barq] - so the hardware RTC is written by ui_task,
; outside this held lock, never from here.
; -----------------------------------------------------------------------------
cp_time_click:
    push ax
    push bx

    cmp dx, CPT_OB0Y1
    jge .opt                    ; below the buttons: the option rows

    cmp cx, CPT_BX
    jge .btn

    call cp_time_pick           ; --- the field rows: which field? ---
    jc .done
    cmp al, [cp_tsel]
    je .done                    ; already selected: change nothing
    mov [cp_tsel], al
    call cp_time_rows
    jmp .done

.btn:                           ; --- the button column ---
    cmp cx, CPT_BX + CPT_BW - 1
    jg .done
    mov bl, 1                   ; the '+' band
    cmp dx, CPT_BUY
    jl .done
    cmp dx, CPT_BUY + CPT_BH - 1
    jle .step
    mov bl, 0xFF                ; the '-' band
    cmp dx, CPT_BDY
    jl .done
    cmp dx, CPT_BDY + CPT_BH - 1
    jg .done
.step:
    mov al, [cp_tsel]
    call clk_fld_adj           ; in AL = field, BL = +1/-1 (SPEC.md 37)
    call cp_time_rows
    jmp .done

.opt:                           ; --- the two display options (SPEC.md 37) ---
    cmp dx, CPT_OB1Y2
    jg .done                    ; below the last band: nothing
    cmp dx, CPT_OB0Y2
    jg .osec

    xor byte [clk_h12], 1       ; row 0: 12-hour clock. The hour field reads
    cmp byte [clk_h12], 0       ; differently now, and the meridiem field
    jne .oredraw                ; comes or goes with it - so if it was the
    cmp byte [cp_tsel], 6       ; selected one and just went away, the
    jb .oredraw                 ; selection has to come back inside the table
    mov byte [cp_tsel], 0
    jmp .oredraw
.osec:
    xor byte [clk_secs], 1      ; row 1: seconds in the menu bar
.oredraw:
    mov byte [cp_wdirty], 1     ; both remembered on close (31.8)
    mov byte [clk_barq], 1      ; the bar's text (and width) changed: ui_task
                                ; redraws it in this same pass (SPEC.md 37).
                                ; NOT [cp_dirty] - a whole-desktop repaint to
                                ; fix a flicker would be a worse flash
    call cp_timechk             ; the glyph...
    call cp_time_rows           ; ...and the rows it just re-formatted

.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_tick - the once-a-second live refresh (far body of the cp_tick shim)
;
; in:  nothing; the gfx lock is held by ui_task (SPEC.md 13 step 4)
; out: nothing
; clobbers: nothing (flags only)
;
; Every guard here is the SPEC.md 14 background-drawing contract, checked
; under the lock the caller already holds: the page must be the selected
; one, a Control Panel instance must be live, its window visible, and
; nothing may be on top of it. Only then is the origin re-derived from the
; live window rect and the two rows redrawn in place.
; -----------------------------------------------------------------------------
cp_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    cmp byte [cp_sel], CP_ITIME
    jne .out
    mov al, KIND_CTRL
    call inst_find_kind         ; CF = 1: no live Control Panel
    jc .out
    mov bx, [di+I_WIN]
    test word [bx+W_FLAGS], 2   ; visible?
    jz .out
    call wm_obscured
    jc .out
    call wm_content             ; AX = content left, DX = content top
    mov di, ax
    add di, CP_RX               ; DI = pane left, BP = pane top
    mov bp, dx
    call cp_time_rows

.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_time_rows - redraw both field rows from the live clock
;
; in:  DI = pane left, BP = pane top; caller holds the gfx lock
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK or CWHITE)
;
; Erases the band the rows occupy first (the button column excluded), so
; this is the single redraw path for everything that can change here: a new
; selection, a +/- step, and the once-a-second tick.
; -----------------------------------------------------------------------------
cp_time_rows:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CWHITE
    mov ax, di
    mov bx, bp
    add bx, CPT_DY - 4
    mov cx, di
    add cx, CPT_BX - 4
    mov dx, bp
    add dx, CPT_TY + 11
    call gfx_fill

    mov byte [gfx_color], CBLACK ; the time row's two ':' separators (the
    mov si, cp_s_tcol            ; date row's gaps are simply blank)
    mov cx, di
    add cx, CPT_FX + 16
    mov dx, bp
    add dx, CPT_TY
    call font_str
    mov cx, di
    add cx, CPT_FX + 40
    call font_str

    call clk_snap               ; ONE snapshot for all the fields, so the
                                 ; row cannot straddle a second (SPEC.md 37)
    mov ah, 6                   ; active field count: the meridiem exists
    add ah, [clk_h12]           ; only in 12-hour mode (SPEC.md 31.5)
    xor al, al                  ; then every field, the selected one barred
.fld:
    call cp_time_fld
    inc al
    cmp al, ah
    jb .fld

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_time_fld - draw one field, barred if it is the selected one
;
; in:  AL = field index, DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK or CWHITE)
; -----------------------------------------------------------------------------
cp_time_fld:
    push ax
    push bx
    push cx
    push dx
    push si

    mov ah, al                  ; AH = the field index; AL is needed below
    mov bl, al
    mov bh, 0
    shl bx, 1
    shl bx, 1
    add bx, cp_tflds            ; BX -> {x, w, y, pad}

    mov cl, [bx]                ; CX = the field text's screen x
    mov ch, 0
    add cx, di
    mov dl, [bx+2]              ; DX = its screen y
    mov dh, 0
    add dx, bp

    cmp ah, [cp_tsel]
    jne .plain

    push cx                     ; --- selected: white text on a black box ---
    push dx
    push bx
    push ax                     ; the box math below runs through AX, and AH
                                ; still has to name the field afterwards
    mov ax, cx
    sub ax, 2                   ; x1 = x - 2
    mov si, cx
    mov cl, [bx+1]              ; the field's width
    mov ch, 0
    add cx, si
    inc cx                      ; x2 = x + w + 1
    mov bx, dx
    sub bx, 2                   ; y1 = y - 2
    add dx, 9                   ; y2 = y + 9
    mov byte [gfx_color], CBLACK
    call gfx_fill
    pop ax
    pop bx
    pop dx
    pop cx
    mov byte [gfx_color], CWHITE
    jmp .text
.plain:
    mov byte [gfx_color], CBLACK
.text:
    mov al, ah                  ; AL = field index for the formatter
    call clk_fld_str            ; SI = that field's text (SPEC.md 37)
    call font_str               ; in SI, CX = x, DX = y

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_time_pick - hit-test the six field bands
;
; in:  CX = pane-relative x, DX = pane-relative y
; out: CF = 0 and AL = field index on a hit, CF = 1 on a miss
; clobbers: AX
; Bands are generous: x-2..x+w+1 by y-4..y+11, so the gaps between fields
; belong to nobody and a click just misses. SIGNED compares (SPEC.md 31.2).
; -----------------------------------------------------------------------------
cp_time_pick:
    push bx
    push si
    push di
    mov al, 6                   ; one past the last ACTIVE record: the
    add al, [clk_h12]           ; meridiem is only there in 12-hour mode
    mov ah, 0
    shl ax, 1
    shl ax, 1
    add ax, cp_tflds
    mov [cp_tendp], ax
    mov bx, cp_tflds
.scan:
    mov ax, [bx]                ; AL = field x, AH = field width
    mov di, ax
    and di, 0x00FF              ; DI = x
    mov al, ah
    mov ah, 0                   ; AX = w
    mov si, di
    add si, ax
    inc si                      ; SI = x2 = x + w + 1
    mov ax, di
    sub ax, 2                   ; AX = x1 = x - 2
    cmp cx, ax
    jl .next
    cmp cx, si
    jg .next
    mov al, [bx+2]              ; the field's row y
    mov ah, 0
    mov si, ax
    add si, 11                  ; y2
    sub ax, 4                   ; y1
    cmp dx, ax
    jl .next
    cmp dx, si
    jg .next
    mov ax, bx                  ; hit: index = (record - table) / 4
    sub ax, cp_tflds
    shr ax, 1
    shr ax, 1
    clc                         ; after the shifts - they write CF
    jmp .out
.next:
    add bx, 4
    cmp bx, [cp_tendp]
    jb .scan
    stc
.out:
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; cp_timechk - draw both option checkbox glyphs from the live settings
;
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; Both are always drawn and cp_glyph white-fills its own box, so this is
; also the redraw path after a toggle.
; -----------------------------------------------------------------------------
cp_timechk:
    push ax
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK
    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CPT_O0Y
    mov si, cp_chk_off          ; --- row 0: 12-hour clock ---
    cmp byte [clk_h12], 0
    je .r0
    mov si, cp_chk_on
.r0:
    call cp_glyph

    mov dx, bp
    add dx, CPT_O1Y
    mov si, cp_chk_off          ; --- row 1: seconds in the menu bar ---
    cmp byte [clk_secs], 0
    je .r1
    mov si, cp_chk_on
.r1:
    call cp_glyph

    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_timebtn - draw one of the two stepper buttons
;
; in:  AL = 0 the '+' button / 1 the '-' button; DI = pane left, BP = pane
;      top; gfx lock held
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_timebtn:
    push ax
    push bx
    push cx
    push dx
    push si

    mov si, cp_s_tplus
    mov dx, CPT_BUY
    or al, al
    jz .have
    mov si, cp_s_tminus
    mov dx, CPT_BDY
.have:
    mov bx, bp
    add bx, dx                  ; BX = y1
    push bx
    mov ax, di
    add ax, CPT_BX              ; AX = x1
    mov cx, di
    add cx, CPT_BX + CPT_BW - 1 ; CX = x2
    mov dx, bx
    add dx, CPT_BH - 1          ; DX = y2
    mov byte [gfx_color], CBLACK
    call gfx_frame
    inc ax                      ; the interior, one pixel in from the frame
    inc bx
    dec cx
    dec dx
    mov byte [gfx_color], CWHITE
    call gfx_fill
    pop bx

    mov byte [gfx_color], CBLACK ; the 8px glyph, centred in the box
    mov cx, di
    add cx, CPT_BX + (CPT_BW - 8) / 2
    mov dx, bx
    add dx, (CPT_BH - 8) / 2
    call font_str

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

; -----------------------------------------------------------------------------
; cp_flush - persist every Control Panel setting (SPEC.md 31.8/51.5)
; in:  nothing
; out: nothing (all registers preserved)
;
; EVERY page calls this after it changes anything, so the machine boots in the
; state it was left in. It is one write of one 32-byte file, and the file
; carries the whole panel - the driver list, the sound route, the clock
; options, the scheduler mode and the back buffer - so there is no per-setting
; bookkeeping to keep in step.
;
; It writes ONCE, when the panel closes, rather than on every click. A floppy
; write is ~a second on the floor machine and the panel is frozen for it, so
; a session of trying settings out used to cost a second per click. Pages mark
; [cp_wdirty] instead and cp_flush_close spends it.
;
; What that gives up is the crash window: a machine switched off with the
; panel still open loses whatever was changed. That is the deliberate trade -
; the settings are a convenience, not a document - and it is bounded by
; flushing on the two ways the panel can go away, teardown and reboot, rather
; than on the one obvious one.
;
; [cp_dsave] carries a failed write to the Drivers page's caption, the only
; page with room to say it. It now says so the NEXT time the panel is opened,
; because by the time the write happens the page is gone - and cp_flush_close
; leaves [cp_wdirty] set on failure, so the next close retries rather than
; silently dropping it.
; -----------------------------------------------------------------------------
cp_flush:
    push ax
    call drv_cfg_save
    mov byte [cp_dsave], 0
    jnc .out
    mov byte [cp_dsave], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_flush_close - spend [cp_wdirty], if there is anything to spend
; in:  nothing
; out: nothing (all registers preserved)
;
; Called from the Control Panel's teardown (app_close_win, SPEC.md 29.4) and
; from the reboot path (SPEC.md 12.3) - the two ways the panel stops existing.
; Minimizing is NOT one of them: the panel is still open, and hiding it is not
; a decision to save.
;
; Safe to call when nothing changed, when the panel was never opened, and when
; no disk is in the drive: the flag gates the first two and drv_cfg_save's own
; failure path handles the third.
; -----------------------------------------------------------------------------
cp_flush_close:
    cmp byte [cp_wdirty], 0
    je .out
    call cp_flush
    cmp byte [cp_dsave], 0      ; the write failed: KEEP the flag, so closing
    jne .out                    ; the panel again - or rebooting - tries once
    mov byte [cp_wdirty], 0     ; more rather than dropping the settings
.out:
    ret

; =============================================================================
; The Sound page (SPEC.md 31.7/34.8)
;
; WHICH SOUND HARDWARE THE MACHINE USES. Three radio rows on the Display
; page's geometry, and a Test button under them that plays a tone through
; whatever is selected - so the answer to "did that do anything?" is one click
; away and does not need an app.
;
; The rows are a LADDER, not three devices: PC Speaker is the resident sink
; every machine has, AdLib adds the FM tier, and Sound Blaster adds the stream
; tier on top of it. Every Sound Blaster carries an OPL2, so an SB owner who
; picks AdLib is not giving up their card - they are giving up the DMA buffer,
; the IRQ and the worker task that only the stream tier needs. On a 128KB
; machine that is 12KB of a 55KB heap back, which is the whole reason the
; middle rung exists.
;
; A ROW IS GREYED FOR MISSING HARDWARE AND NEVER FOR MISSING MEMORY, and the
; line between those two is the whole design of this page:
;
;   - **Hardware is a fact the driver already established.** DSV_TIERS is a
;     bitmap of the tiers it COULD provide, decided at attach from what its
;     probes found and never touched by a tier change (SPEC.md 51.2). So the
;     page reads one word and greys what the machine does not have. It does
;     not ask the driver to look again, because an ISA card cannot arrive
;     while the machine is running and looking again is EXPENSIVE: the Sound
;     Blaster scan is six base addresses with a DSP reset timeout each and
;     then a slow retry on 220h, about a sixth of a second on a machine with
;     no card - spent on the UI task under the gfx lock, so the user sees the
;     desktop freeze on a click that then does nothing.
;   - **Memory is not a fact anyone has.** Whether the heap can fund a
;     page-safe 12KB right now is answerable ONLY by making the claim:
;     mem_avail reports the largest free run and says nothing about whether a
;     64KB-page-safe base exists inside it, so only mem_claim_dma's own scan
;     knows (SPEC.md 50.3). The click therefore tries, and cp_snd_note reports
;     what came back. Greying on a guess would keep a machine that has the
;     room off a tier it can afford.
;
; That second half is the one place this page departs from the bb_avail idiom
; (SPEC.md 31.3), and deliberately: bb_canfit is a real predicate and
; mem_claim_dma has none.
;
; A driver that publishes DSV_TIERS = 0 said nothing, and is taken at its
; word: every row stays live and the click finds out the slow way, which is
; exactly the behaviour before the cell existed.
; =============================================================================

CPS_B2Y2  equ CP_PR0Y + 2*CP_PROWH + CP_GW + 3  ; 81: route row 2's bottom
CPS_BTX1  equ CP_PGX                    ; the Test button
CPS_BTX2  equ CP_PGX + 44
CPS_BTY1  equ 84
CPS_BTY2  equ 99
CPS_NOTEY equ 104                       ; the refusal notice's text row

; Which row a click asked for, in [snd_route] order, so the band test is an
; index rather than a ladder of compares - and the same rows as DSV_TIERS
; bits, which is a table rather than a shift because cp_snd_rowok promises to
; clobber nothing and a variable shift count costs CL.
cp_snd_tiers: db SND_RT_SPK, SND_RT_FM, SND_RT_SB
cp_snd_tbit:  db 0, 1 << SND_RT_FM, 1 << SND_RT_SB

; The last click's refusal, as a DRVE_* (0 = the last click was fine, and the
; notice line is blank). .text and not .bss for the reason everything else
; here is: -f bin zeroes nothing.
cp_snd_err: db 0

; -----------------------------------------------------------------------------
; cp_snd_paint - the page: heading, three tier rows, the Test button, notice
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
cp_snd_paint:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK    ; entry pen is CWHITE (cp_page's erase)
    mov si, cp_s_snd
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call font_str

    xor bx, bx                      ; BX = row 0..2
.lbl:
    mov al, bl
    call cp_snd_rowok               ; greyed rows still draw - they are what
    call gfx_pen_cf                 ; the machine does not have, and a missing
                                    ; row would just look like a shorter list
    mov si, cp_s_sspk               ; three fixed labels, in ladder order
    or bl, bl
    jz .str
    mov si, cp_s_sfm
    cmp bl, 1
    je .str
    mov si, cp_s_ssb
.str:
    mov cx, di
    add cx, CP_PLX
    mov ax, CP_PROWH
    mul bl                          ; AX = bl * CP_PROWH (bl <= 2, so AH = 0)
    mov dx, bp
    add dx, ax
    add dx, CP_PR0Y + CP_PLDY
    call font_str
    inc bl
    cmp bl, 3
    jb .lbl
    call gfx_pen_live

    mov ax, di                      ; the Test button: a plain framed box
    add ax, CPS_BTX1
    mov bx, bp
    add bx, CPS_BTY1
    mov cx, di
    add cx, CPS_BTX2
    mov dx, bp
    add dx, CPS_BTY2
    call gfx_frame
    mov si, cp_s_stest
    mov cx, di
    add cx, CPS_BTX1 + 11
    mov dx, bp
    add dx, CPS_BTY1 + 4
    call font_str

    call cp_snd_radios
    call cp_snd_note

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_snd_rowok - is tier row AL selectable? (module internal)
; in:  AL = 0 speaker, 1 AdLib, 2 Sound Blaster
; out: CF = 0 yes; CF = 1 no - the hardware that row names is not here
; clobbers: nothing (flags)
;
; ONE test for both driver rows, because both ask the same question and the
; driver answered it once, at attach. The bits are in the low byte for every
; SND_RT_* there is (they are 1..3), so this is a byte test.
; -----------------------------------------------------------------------------
cp_snd_rowok:
    or al, al
    jz .yes                     ; the PC speaker is resident and needs no
                                ; driver at all: never refused
    cmp word [drv_owner], 0
    je .no                      ; nothing published: the speaker is all the
                                ; machine has
    cmp word [drv_svc+DSV_TIERS], 0
    je .yes                     ; a driver that did not answer is taken at its
                                ; word - the click finds out the slow way,
                                ; which is what every row used to do
    push bx
    mov bl, al
    xor bh, bh
    mov bl, [bx+cp_snd_tbit]
    test byte [drv_svc+DSV_TIERS], bl
    pop bx                      ; POP does not touch flags
    jz .no
.yes:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cp_snd_row - which row is EFFECTIVELY live? (module internal)
; in:  nothing
; out: AL = 0 speaker, 1 AdLib, 2 Sound Blaster
; clobbers: AL (the output), flags
;
; The dot follows the effective tier, not the stored setting, so it can never
; claim hardware that is not sounding. The two disagree in exactly one case: a
; disk written on a machine with a Sound Blaster, carried to one without.
; [snd_route] keeps SND_RT_SB there - taking the setting away would mean the
; disk stopped working when it went home - and the dot honestly shows AdLib or
; the speaker, whichever the driver actually reached.
; -----------------------------------------------------------------------------
cp_snd_row:
    cmp byte [snd_route], SND_RT_SPK
    je .spk
    cmp byte [snd_route], SND_RT_FM
    je .fm                      ; AUTO and SB both take the best tier up
    cmp word [drv_svc+DSV_STREAM], 0
    jne .sb
.fm:
    cmp word [drv_svc+DSV_TONE], 0
    jne .adlib
.spk:
    xor al, al
    ret
.adlib:
    mov al, 1
    ret
.sb:
    mov al, 2
    ret

; -----------------------------------------------------------------------------
; cp_snd_radios - the three glyphs (module internal)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; THE GLYPH IS GREYED TOO, not just the label beside it, and on this page that
; is what carries the whole signal on two adapters out of three. font_ink
; rounds CDGRAY to BLACK for text - a dithered 8x8 glyph is unreadable, so
; mono has no grey to draw a letter in - which means a disabled LABEL is
; pixel-identical to a live one on Hercules and CGA. The Display page gets
; away with that because its caption says why in words; this page has no
; caption, so the row had to say it some other way.
;
; A glyph can, because it is drawn with gfx_pixel rather than as a font cell:
; gfx_ink maps CDGRAY to the 50% dither, so the ring comes out dotted. Dark
; grey on VGA, dotted on mono, and either way the whole row reads as disabled
; instead of a black control with faint writing next to it.
; -----------------------------------------------------------------------------
cp_snd_radios:
    push ax
    push bx
    push cx
    push dx
    push si
    call cp_snd_row             ; BH = the row that owns the dot
    mov bh, al
    xor bl, bl
    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CP_PR0Y
.r:
    mov al, bl
    call cp_snd_rowok           ; ...and the same pen cp_snd_paint gave the
    call gfx_pen_cf             ; label, so the two halves of a row can
    mov si, cp_radio_off        ; never disagree about being disabled
    cmp bl, bh
    jne .draw
    mov si, cp_radio_on
.draw:
    call cp_glyph
    add dx, CP_PROWH
    inc bl
    cmp bl, 3
    jb .r
    call gfx_pen_live
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_snd_note - the notice line: why the last click did not take (internal)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; Blank unless a tier change was refused, and it uses the LOADER's strings
; (drv_errstr) because a tier that cannot be had and a driver that cannot be
; loaded fail for the same two reasons - no hardware, or no memory - and one
; vocabulary for both is one less thing for the user to map.
; -----------------------------------------------------------------------------
cp_snd_note:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, bp
    add dx, CPS_NOTEY
    call cp_drv_wipe            ; the strings differ in length, and the line
    mov bl, [cp_snd_err]        ; is usually empty
    or bl, bl
    jz .out
    cmp bl, DRVE_NERR           ; the code came out of a driver, and a table
    jae .out                    ; index is not a thing to take on trust
    xor bh, bh
    shl bx, 1
    mov si, [bx+drv_errstr]
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPS_NOTEY
    call font_str
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_snd_click - a click inside the Sound page
; in:  DI = pane left, BP = pane top, CX = pane-relative x, DX = pane-relative
;      y; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; A tier change is ASKED FOR, not asserted: drv_tier carries the candidate to
; the driver, which may have to claim memory for it, and only an answer of yes
; stores it. That order is what lets the AdLib -> Sound Blaster switch simply
; try - there is no predicate to pre-check with, and refusing on a guess would
; keep a machine that has the room off a tier it can afford (SPEC.md 34.8).
; -----------------------------------------------------------------------------
cp_snd_click:
    push ax
    push bx
    push cx
    push dx

    cmp dx, CPS_BTY1            ; --- the Test button ------------------------
    jl .rows
    cmp dx, CPS_BTY2
    jg .done
    cmp cx, CPS_BTX1
    jl .done
    cmp cx, CPS_BTX2
    jg .done
    mov ax, 660                 ; one second of a tone through whatever the
    mov cx, 18                  ; page currently selects. UI priority, so it
    mov dl, SND_PRI_UI          ; outranks anything a package is playing -
    call osapi_snd_tone         ; the user asked for it
    jmp short .done

.rows:
    cmp dx, CP_B0Y1             ; --- the three tier rows -------------------
    jl .done
    xor bl, bl                  ; BL = the row the y lands in
    cmp dx, CP_B0Y2
    jle .band
    inc bl
    cmp dx, CP_B1Y2
    jle .band
    inc bl
    cmp dx, CPS_B2Y2
    jg .done
.band:
    mov al, bl
    call cp_snd_rowok           ; a row whose hardware is not here is refused
    jc .done                    ; without a notice: it is drawn grey, which
                                ; has already said so
    xor bh, bh
    mov ah, [bx+cp_snd_tiers]   ; the SND_RT_* that row asks for
    mov byte [cp_snd_err], 0
    cmp ah, [snd_route]
    je .redraw                  ; already the setting: still redraw, because
                                ; the click clears a stale notice
    call drv_tier               ; ask the driver to move; AL = why not
    jnc .take
    mov [cp_snd_err], al        ; the tier stands where it was, and the notice
    jmp short .redraw           ; line says which of the two reasons
.take:
    mov [snd_route], ah
    mov byte [cp_wdirty], 1
.redraw:
    call cp_snd_radios
    call cp_snd_note
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The Drivers page (SPEC.md 31.6/51)
; =============================================================================

; -----------------------------------------------------------------------------
; cp_drv_paint - the page: heading, a checkbox row per driver, one caption
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; A row's LABEL is the driver's name and its CAPTION is what its last load
; attempt answered - DRVE_OK reads 'Loaded', and every other value is the
; reason it is not (kernel/driver.inc's drv_errstr, the same strings the boot
; notice uses). That is the bb_avail idiom again: the box, the caption and
; the click all read one word, so they cannot disagree.
; -----------------------------------------------------------------------------
cp_drv_paint:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK    ; entry pen is CWHITE (cp_page's erase)
    mov si, cp_s_drv                ; heading, same string as the list row
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call font_str

    xor bx, bx                      ; BX = row index
.row:
    push bx
    mov al, bl
    call drv_row                    ; BX = the drv_tab row
    cmp word [bx+DRVR_FILE], 0
    je .next                        ; an empty table row draws nothing
    mov si, [bx+DRVR_TITLE]
    pop ax
    push ax                         ; AX = the row index again
    push bx
    call cp_drv_rowy                ; DX = this row's glyph top
    mov cx, di
    add cx, CP_PLX
    add dx, CP_PLDY
    call font_str                   ; the driver's name
    pop bx

    call drv_status                 ; ...and what it is doing right now
    pop ax
    push ax
    call cp_drv_rowy
    add dx, CP_PLDY + 9
    mov cx, di
    add cx, CP_PLX
    call font_str
.next:
    pop bx
    inc bx
    cmp bx, DRV_MAX
    jb .row

    call cp_drv_boxes               ; every checkbox, from the live state

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_rowy - a driver row's glyph top (module internal)
; in:  AL = row index, BP = pane top
; out: DX = the absolute y of that row's checkbox
; clobbers: nothing else (flags)
;
; Two text lines per row, so the pitch is wider than the radio pages': the
; name sits on the glyph's own baseline and the reason 9px under it.
; -----------------------------------------------------------------------------
CP_DR0Y   equ 24                ; first driver row: glyph top
CP_DROWH  equ 26                ; row pitch
CP_DB0Y1  equ CP_DR0Y - 4                       ; row 0's hit band, top...
CP_DB0Y2  equ CP_DR0Y + CP_DROWH - 5            ; ...and bottom = row 1 top-1
CP_DB1Y2  equ CP_DR0Y + CP_DROWH + CP_GW + 5    ; row 1's bottom

cp_drv_rowy:
    push ax
    push cx
    mov ah, 0
    mov cx, CP_DROWH
    mul cx
    add ax, CP_DR0Y
    add ax, bp
    mov dx, ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_boxes - redraw every driver row's checkbox from the live table
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; The box tracks DRVR_SEG - what is LOADED - and not DRVR_WANT, which is only
; what the settings file asks for. A driver the user enabled on a machine with
; no card is unchecked, and its caption says why.
; -----------------------------------------------------------------------------
cp_drv_boxes:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [gfx_color], CBLACK
    xor bx, bx
.row:
    push bx
    mov al, bl
    call drv_row
    cmp word [bx+DRVR_FILE], 0
    je .next
    mov si, cp_chk_off
    cmp word [bx+DRVR_SEG], 0
    je .off
    mov si, cp_chk_on
.off:
    pop ax
    push ax
    call cp_drv_rowy
    mov cx, di
    add cx, CP_PGX
    call cp_glyph
.next:
    pop bx
    inc bx
    cmp bx, DRV_MAX
    jb .row
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_click - a click inside the Drivers page (SPEC.md 51.3)
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x (ignored),
;      DX = pane-relative y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only)
;
; The click LOADS OR UNLOADS ON THE SPOT and only then writes SYSTEM.CFG, so
; the page never shows a promise about the next boot. Both halves can fail
; and both failures are reported in place: a load that finds no disk or no
; card leaves the box clear with the reason under it, and a settings write
; that cannot reach the disk says so in the caption while the driver it just
; loaded stays loaded - which is the honest split, because the two are
; genuinely separate outcomes.
;
; A load does floppy I/O with the gfx lock held. That is the same bargain
; every other file operation in this OS makes (SPEC.md 18.4): the cursor
; freezes for the length of the read, and the alternative - dropping the lock
; inside a click handler - is not one the window manager offers.
; -----------------------------------------------------------------------------
cp_drv_click:
    push ax
    push bx
    push dx

    cmp dx, CP_DB0Y1            ; outside every band: nothing
    jl .done
    cmp dx, CP_DB1Y2
    jg .done
    xor al, al                  ; upper band = row 0
    cmp dx, CP_DB0Y2
    jle .got
    mov al, 1                   ; lower band = row 1
.got:
    push ax
    call drv_row
    cmp word [bx+DRVR_FILE], 0  ; an empty table row is not a control
    pop ax
    je .done

    push ax
    call drv_row
    cmp word [bx+DRVR_SEG], 0
    pop ax
    je .load
    call drv_unload             ; it is up: take it down
    push ax
    call drv_row
    mov byte [bx+DRVR_WANT], 0
    mov byte [bx+DRVR_ERR], DRVE_OK ; nothing failed - it was turned off
    pop ax
    jmp short .save
.load:
    push ax
    call drv_row
    mov byte [bx+DRVR_WANT], 1  ; wanted first, so a failed load still
    pop ax                      ; persists the intent: put the disk in and
    call drv_load               ; the next boot picks it up
.save:
    mov byte [cp_wdirty], 1     ; ...and only now, the settings file
.redraw:
    call cp_drv_boxes
    call cp_drv_lines
.done:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_lines - redraw the reason under each row, and the save caption
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; The strings differ in length, so each line is erased before it is drawn -
; the cpf_disp_cap idiom.
; -----------------------------------------------------------------------------
cp_drv_lines:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx
.row:
    push bx
    mov al, bl
    call drv_row
    cmp word [bx+DRVR_FILE], 0
    je .next
    call drv_status
    pop ax
    push ax
    call cp_drv_rowy
    add dx, CP_PLDY + 9
    call cp_drv_wipe
    mov cx, di
    add cx, CP_PLX
    call font_str
.next:
    pop bx
    inc bx
    cmp bx, DRV_MAX
    jb .row

    mov dx, bp                  ; the caption: only a failed SAVE speaks here
    add dx, CP_PCAPY
    call cp_drv_wipe
    cmp byte [cp_dsave], 0
    je .out
    mov si, cp_s_dsave
    mov cx, di
    add cx, CP_PMX
    call font_str
.out:
    mov byte [gfx_color], CBLACK
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_wipe - white out one text line of the pane (module internal)
; in:  DI = pane left, DX = the line's baseline y; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_drv_wipe:
    push ax
    push bx
    push cx
    push dx
    mov byte [gfx_color], CWHITE
    mov ax, di
    add ax, CP_PMX
    mov bx, dx
    mov cx, di
    add cx, CP_PRGT             ; the PANE's last column, not the content's
    add dx, 7
    call gfx_fill
    mov byte [gfx_color], CBLACK
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
