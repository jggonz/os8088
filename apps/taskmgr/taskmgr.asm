; =============================================================================
; os8088 - Task Manager (SPEC.md 28)
;
; A live CPU load gauge with an oscilloscope-sweep history graph, a RAM
; readout, and the process list with per-instance CPU shares and memory from
; the PIT cycle accounting (SPEC.md 8.1). A content click toggles a second,
; memory view: the RAM readout, a conventional-memory map, and the process
; list re-columned as NAME/ADDR/SIZE with each resident package's region drawn
; in its slot's dither pattern and a matching legend square per row.
;
; **This was a built-in, and the reason it stopped being one is memory.** It
; is a VIEWER, not a manager - nothing here can kill a task - so six kilobytes
; of kernel, on every machine, forever, bought a window most sessions never
; open. As a package it costs that much heap while it is open and nothing at
; all when it is not, and the two guards it came off (SPEC.md 15.1) got the
; whole of it back. What it costs in exchange is honest and worth stating: it
; now needs a working disk and about seven kilobytes of free heap to open at
; all, on the machine where you are opening it precisely because something is
; wrong. The Control Panel, which is the one you want when a driver will not
; attach, stayed resident (SPEC.md 2.6), and the chip menu says exactly why a
; launch failed rather than greying itself out on a guess (SPEC.md 47 rule 3).
;
; Everything it knows about the kernel arrives through the four cells of
; SPEC.md 20.9 - the scheduler and instance tables in one cli window, the
; claim map, the kernel's own footprint in KB - and through the ordinary
; drawing and window slots. There is no other channel, and there was no other
; obstacle: the module's logic is unchanged from the day it lived inside.
;
; The list is an INSTANCE list, not a task list (SPEC.md 29). A task list
; would show only the kinds that own a task; every task-less app - About,
; Disk, and every loaded package that has not claimed a worker (SPEC.md 20.6)
; - runs purely inside window callbacks on the UI task and would be invisible.
; Each instance's row therefore adds two counters: the callback cycles the
; kernel bills to it around every W_PAINT / W_ONKEY / W_ONCLICK, and, if it
; owns a task, that task's own. Row 0 is "System" - task 0's remainder, i.e.
; the kernel's own work once every callback has been debited away. The two
; are disjoint by construction, so the rows partition one total.
;
; tm_worker is the monitor task and doubles as the system idle soak: it never
; sleeps, spinning { count += 1; OSAPI_TASK_YIELD } for 9-tick intervals. The
; spin count measures how quickly a yield loop gets the CPU back, calibrated
; against a rolling maximum - this self-calibrates away the UI task's constant
; busy-poll cost, so the gauge reads ~0% at idle and rises when any task
; (including the UI) holds the CPU. It also absorbs the far-call overhead of
; every measurement below, for the same reason: the percentages are shares of
; a rolling maximum, not absolute rates, so a spin iteration that costs more
; recalibrates within two epochs and only the granularity moves.
;
; Locking follows the Timer pattern (SPEC.md 14): tm_paint is a bare,
; unconditional full redraw (the kernel calls it with the gfx lock already
; held); the worker's periodic path takes the lock itself, re-checks visible +
; arms OSAPI_WM_CLIP_SET under it, and redraws only what changed (one graph
; column, the text lines, the bar, the rows) so the lock hold stays
; Bounce-scale.
;
; **A region, not the wm_obscured veto.** The veto was the right answer while
; this repainted its whole pane in one go; it has not done that since the
; refresh became per-element, and the cost was a window that stopped updating
; because something overlapped a corner. SPEC.md 11.3's granularity rule
; applies in return: a fill clips per PIXEL and a glyph per whole CELL, so
; anything that erases a band and then letters it gates BOTH on one
; OSAPI_WM_CLIP_TEST of that band - tm_rowok - or a row crossed by a clip edge
; goes blank rather than stale, and re-blanks every refresh. The maps and the
; graph are fills only and need no gate.
; tm_sample runs without the lock and only touches module state; a sample
; landing mid-tm_paint can at worst misplace the sweep gap by one column for
; one frame.
; =============================================================================

%include "os88api.inc"

; --- the dock/desktop icon (SPEC.md 20.2): a bar chart on an axis ------------
; Distinct from the Control Panel's slider panel at 16px, which a framed list
; of bars was not.
;   ................
;   ................
;   ..#.............
;   ..#.........##..
;   ..#.........##..
;   ..#.....##..##..
;   ..#.....##..##..
;   ..#.##..##..##..
;   ..#.##..##..##..
;   ..#.##..##..##..
;   ..#.##..##..##..
;   ..############..
;   ................
;   ................
;   ................
;   ................
    OS88_HEADER 'TaskMgr', tm_entry, 1
    OS88_ICON16
    dw 0x0000, 0x7000, 0x701E, 0x701E, 0x71FE, 0x71FE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x0000
    dw 0x0000, 0x0000, 0x2000, 0x200C, 0x200C, 0x20CC, 0x20CC, 0x2CCC
    dw 0x2CCC, 0x2CCC, 0x2CCC, 0x3FFC, 0x0000, 0x0000, 0x0000, 0x0000
    OS88_ICON16_END

; --- geometry / cadence (SPEC.md 28) -----------------------------------------
TM_INT      equ 9               ; sample interval, ticks (~0.5s)
TM_EPOCH    equ 20              ; samples per calibration epoch (~10s)
TM_GW       equ TM_RW - 7       ; graph interior width = history ring size
                                ; (7..TM_RW-1 inclusive), so it follows TM_RW
                                ; and the two maps below always fill their
                                ; frames edge to edge
TM_GH       equ 40              ; graph interior height, px
TM_CPU_Y    equ 4               ; "CPU nnn% SCH xxxxxxx" line, content-rel y
TM_GF_Y1    equ 14              ; graph frame top (interior rows 15..54)
TM_GF_Y2    equ 55              ; graph frame bottom
TM_RAM_Y    equ 61              ; "RAM uuuK/tttK" line
TM_BAR_Y1   equ 71              ; RAM bar frame top (interior rows 72..79)
TM_BAR_Y2   equ 80              ; RAM bar frame bottom
TM_HDR_Y    equ 87              ; process-list header line
TM_ROW_Y    equ 97              ; first process row
TM_ROW_H    equ 11              ; row pitch, px
TM_ROWS     equ INST_MAX + 1    ; "System" plus one row per instance slot
TM_W        equ 232             ; the window's own width - the template's, and
                                ; one column's. Two columns add TM_COLW.
TM_RW       equ 223             ; the rows' right edge, content-relative. It
                                ; is the memory view's TOP LINE that sizes the
                                ; window: 'RAM nnn/nnnK', the claim swatch and
                                ; 'HEAP nnn/nnnK' land exactly on it

; --- memory-view geometry (SPEC.md 28) ---------------------------------------
; **ONE map, captioned on the line directly above it.** There were two: the
; conventional address space, and the package pool magnified. The pool is
; gone (SPEC.md 20.1) - a package's region is an ordinary heap claim now - so
; the second map had nothing left to magnify, and the regions it used to show
; are drawn in the first one at their real addresses, in the same per-slot
; patterns. That is a strictly better place for them: they are now next to
; the data claims they compete with for the heap.
TMM_RAM_Y   equ 4               ; 'RAM nnn/nnnK [] HEAP nnn/nnnK' - the
                                ; conventional map's caption, both figures
TMM_M1_Y1   equ 14              ; conventional-memory map frame top
TMM_M1_Y2   equ 29              ; frame bottom (interior rows 15..28)
TMM_HSQ_X   equ 105             ; the claim swatch, centred in the two-space
                                ; gap between the RAM and HEAP figure pairs
TMM_XMS_Y   equ 33              ; 'XMS nnn/nnnK' - memory above 1MB
                                ; (SPEC.md 41), directly BELOW the map
                                ; because that is where it is NOT: the map
                                ; above is the conventional address space and
                                ; extended memory has no conventional address
                                ; to draw at. So it gets a figure line and a
                                ; bar of its own scale, under the map that
                                ; does. On tier 0 it reads 'XMS 0/0K'
TMM_XB_Y1   equ 42              ; the XMS bar's frame, directly under its
TMM_XB_Y2   equ 51              ; figures - same shape as the RAM bar in the
                                ; performance view, because it answers the
                                ; same question about a different pool
TMM_HDR_Y   equ 55              ; "NAME    ADDR SIZE   HEAP" header line

; --- what the periodic refresh checks before it draws (SPEC.md 28) -----------
; The rows have tm_rowck; these are the seven things AROUND them that cost
; real pixels and mostly do not move. A map interior is ~3,000 pixels of
; pattern fill; the figures above it change when a claim is made or a package
; loads, which on a desktop that is just sitting there is never. Every one of
; them goes through tm_elchk, rows included, so the never-zero rule there
; covers the lot.
TMC_LINE    equ 0               ; the RAM (+ HEAP) readout line
TMC_PKG     equ 1               ; (free - was the PACKAGES readout line)
TMC_MRAM    equ 2               ; the conventional-memory map interior
TMC_BAR     equ 3               ; the performance view's RAM bar
TMC_XMS     equ 4               ; the XMS readout line (SPEC.md 41)
TMC_XBAR    equ 5               ; ...and its bar
TMC_N       equ 6

; Every line in this window is composed into tm_str before any of it is
; drawn - that is what lets one hash decide whether to draw it at all - so
; the buffer has to hold the LONGEST of them, and it is the memory view's
; top line by a good margin. Named in its parts because it grew by four
; characters the day the HEAP figures joined the RAM figures on that line,
; and a 24-byte buffer went on silently writing past the end of .bss.
TM_L_RAM    equ 4               ; 'RAM '
TM_L_HEAP   equ 7               ; '  HEAP '   (the gap is part of the label)
TM_L_KPAIR  equ 8               ; 'nnn/nnnK'  (tm_kpair)
TM_L_CPU    equ 4 + 6           ; 'CPU ' + a six-column tier name
TM_L_XMS    equ 4               ; 'XMS '
TM_L_K5PAIR equ 5 + 1 + 5 + 1   ; 'nnnnn/nnnnnK' (two tm_put5, SPEC.md 41.6)
; Two candidates for longest, and which one wins has already changed once, so
; take the max rather than naming the winner: the RAM line grew by four the
; day HEAP joined it, and the XMS line grew by ten the day the CPU tier did.
TM_LN_RAM   equ TM_L_RAM + TM_L_KPAIR + TM_L_HEAP + TM_L_KPAIR
TM_LN_XMS   equ TM_L_CPU + TM_L_XMS + TM_L_K5PAIR
%if TM_LN_XMS > TM_LN_RAM
TM_STRMAX   equ TM_LN_XMS + 1
%else
TM_STRMAX   equ TM_LN_RAM + 1
%endif
                                ; = 28: the RAM line at 27 + NUL, one ahead of
                                ; the XMS line's 26. The memory view's ROWS are
                                ; 25 (see tm_s_mhdr) and the captions shorter
                                ; still. There is ONE character of slack, which
                                ; is the number to check before widening
                                ; anything - a 24-byte buffer once went on
                                ; silently writing past the end of .bss

; --- what the kernel occupies, in KB -----------------------------------------
; Seven assembly-time constants used to live here - the image, the FAT
; snapshot, the stacks, the disk buffers and the two spans over them - and
; every one of them was a term of the kernel's own memory ladder read
; straight out of kernel.asm. That is the one thing a program outside the
; kernel cannot do, and it was the last thing keeping this window inside it.
; They are the SK_* fields of osapi_sys_kb's buffer now (SPEC.md 20.9), read
; into [tm_kb] by tm_view_begin and by every sample. Still constants, still
; free on the drawing path - just fetched rather than assembled in, and the
; kernel is now the one place that has to know its own size.
TMM_ROW_Y   equ 67              ; first process row (same TM_ROW_H pitch)
TM_COLW     equ TM_RW + 9       ; column pitch when the list runs in two of
                                ; them: a row's own width plus the margin the
                                ; next one starts at
TM_COL2_MIN equ 12              ; ...and the threshold. A screen that fits at
                                ; least this many rows in ONE column keeps
                                ; one - VGA (19) and Hercules (17) do. CGA's
                                ; 200 rows fit three, so it gets the second
                                ; column and a window twice as wide, which
                                ; 640 px across has room for
TM_C2_HDR_Y equ 4               ; A later column's header, at the very top of
TM_C2_ROW_Y equ 16              ; the content, and its first row under it.
                                ; Column 0 starts below the maps because the
                                ; maps are there; nothing is above column 1, so
                                ; anchoring it at TMM_ROW_Y too would waste the
                                ; taller two thirds of it. On CGA that is the
                                ; difference between 3 rows beside 3 and 3
                                ; beside 10
TM_CHUNK    equ 5               ; characters per independently-checked chunk
TM_NCHUNK   equ 5               ; ...and chunks per row: 25 characters, which
                                ; covers the widest row either view composes.
                                ; A row used to be ONE check word, so a CPU
                                ; percentage ticking over redrew the name, the
                                ; state and the memory figure with it - 20
                                ; glyphs to change three. Per chunk it is four
TM_CW       equ TM_CHUNK * 8    ; a chunk's width in pixels
TM_PEN      equ 8               ; the process list's and the captions' inset
                                ; from the row band. It was 6, and 8 is what
                                ; puts every cell in this window on a multiple
                                ; of 8 once WF_SNAP has put the content origin
                                ; on one (SPEC.md 11.94): the band starts at
                                ; the content left, TM_COLW is 232, and the
                                ; memory list's own inset was already 16. Two
                                ; pixels of margin bought the whole window
                                ; font_run's single-store path
TM_MPEN     equ 16              ; ...and the memory list's, which leaves the
                                ; band's first ten pixels for the legend
                                ; square and was already aligned

; --- the performance view's columns (SPEC.md 28.1) ---------------------------
; A row is exactly TM_NCHUNK*TM_CHUNK characters - tm_row_draw's pad makes it
; so - and this view composed 22 of the 25, which left the last 40 px of the
; band empty while the graph frame and the RAM bar directly above it ran the
; band's full width. The three spare columns go to NAME, because it is the
; only column here that can overflow: CPU is bounded by 100% and MEM by the
; 640 KB of conventional memory, so both are three digits forever, while a
; name is up to fifteen (I_NAME) and ArtfulType, Solitaire, Arkanoid and
; Recorder were all truncated at seven.
;
; The widths below are the ONE statement of the layout - tm_s_hdr's gaps are
; derived from them and TM_ROWC is checked against the chunk span - because
; the header, the row builders and the free row's tail all have to agree
; about where a column starts and there is nothing on screen to catch a
; disagreement except reading it.
;
; It falls out one column better than it went in. A field that straddles a
; chunk boundary dirties two chunks whenever it moves, and at the old widths
; both of the moving ones did: CPU sat at 13..16 across chunks 2 and 3, MEM
; at 18..21 across 3 and 4. At these widths CPU is 16..19 and MEM 21..24, one
; chunk each, so the twice-a-second refresh of an idle row touches one chunk
; instead of two.
TM_NAMEC    equ 10              ; characters of a name before it truncates
TM_NAMEF    equ TM_NAMEC + 2    ; ...and the field around it: the indent a
                                ; built-in kind carries under System, plus the
                                ; separator that keeps a full-width name off
                                ; the ST column ('ArtfulTyprun')
TM_STC      equ 3               ; 'run' / 'rdy' / 'slp' / 'evt' / 'die'
TM_CPUC     equ 5               ; 'nnn%' and the gap MEM needs after it
TM_MEMC     equ 4               ; 'nnnK'
TM_ROWC     equ TM_NAMEF + TM_STC + 1 + TM_CPUC + TM_MEMC
%if TM_ROWC != TM_NCHUNK * TM_CHUNK
  %error "taskmgr: the process row no longer fills the chunk span exactly"
%endif
; The span itself is capped by the MEMORY list, not by this one: both views
; are padded to it and the memory list letters from the wider TM_MPEN, so it
; is that pen plus the span that must stay inside the band. At TM_MPEN = 16
; that allows 26 characters and the grid gives 25; this view's own pen leaves
; room for 27. Widen the span and BOTH have to be re-checked.
%if TM_PEN + TM_NCHUNK * TM_CW > TM_RW + 1
  %error "taskmgr: a process row now runs past the right edge of its band"
%endif
%if TM_MPEN + TM_NCHUNK * TM_CW > TM_RW + 1
  %error "taskmgr: a memory row now runs past the right edge of its band"
%endif

TMM_ROWS    equ INST_MAX + 7    ; System, its four buffer rows, the two
                                ; headings, every instance - 19 x TM_ROW_H
                                ; from TMM_ROW_Y is 279 of the 281-pixel
                                ; content, which is what decides both the
                                ; template's height and that free slots are
                                ; not drawn at all. tm_mrow_open clamps to
                                ; the LIVE frame on top of this, for the
                                ; screens where wm_fit shrinks the window

; The CPU/scheduler caption is chunked too, and it borrows the row machinery
; whole by being a row that no list draws: it is a 20-character line whose
; first two chunks carry a percentage that moves every interval and whose
; last two carry a scheduler mode that essentially never does. As one check
; word it redrew all twenty glyphs twice a second on a machine doing nothing,
; which on the mono adapters made it the single largest recurring cost of
; having this window open.
TMR_CPU     equ TMM_ROWS        ; the virtual row index it draws as
TM_NCK      equ TMM_ROWS + 1    ; ...so tm_rowck holds one more row than
                                ; either list can show

; =============================================================================
; How this module reaches kernel state (SPEC.md 20.9)
;
; This window is the one place in the tree that wanted to know things no API
; cell could answer: the scheduler's cycle counters, the instance table, the
; claim map, and a handful of assembly-time constants describing the kernel's
; own footprint. It read them directly, because it was part of the kernel and
; could - which was exactly the property that made it the only built-in that
; could not be lifted out of one.
;
; Four cells answer all of it now, and every use of them in this file goes
; through the macros below. They were near calls to the cells' own bodies
; while this module was still built in - which is how the API got proved
; sufficient before the move - and the move changed these macro bodies and
; nothing else.
;
; TM_ES_DATA names the segment this module's own buffers live in - CS here,
; because kernel code runs with CS = DS = KERNEL_SEG, and a package's DS
; there. Every snapshot cell writes through ES:DI, so a caller states that
; once and the cells need no stub.
; =============================================================================
%macro TM_ES_DATA 0             ; ES = where tm_snapshot/tm_claims/tm_kb are
    push ds                     ; our own segment, and the three cells write
    pop es                      ; through ES:DI. Restore ES afterwards: it is
%endmacro                       ; KERNEL_SEG on entry to every callback AND to
                                ; the worker, which is what makes [es:bx+W_*]
                                ; legal below

%macro TM_SYS_SNAPSHOT 0            ; in ES:DI = a SYS_SNAPSHOT_SIZE buffer
    call OSAPI_SYS_SNAPSHOT
%endmacro

%macro TM_CLAIM_SNAPSHOT 0          ; in ES:DI = a CLAIM_SNAPSHOT_SIZE buffer
    call OSAPI_CLAIM_SNAPSHOT
%endmacro

%macro TM_SYS_KB 0              ; in ES:DI = a SYSKB_SIZE buffer
    call OSAPI_SYS_KB
%endmacro

%macro TM_FILL_PAT 0            ; in AX/BX/CX/DX = rect, SI = 8 pattern bytes
    call OSAPI_GFX_FILL_PAT     ; X: the stub puts our DS in ES for the stage
%endmacro

; TM_INK - the pen colour, in AL, preserving AX. It was a store to
; [gfx_color] at twenty sites while this was kernel code; OSAPI_SET_COLOR has
; always been the cell for it, and a store is three bytes and cannot be
; reached from outside, so nothing had ever asked. AX is live at enough of
; those sites that pushing it here is cheaper than auditing each one - and
; than being wrong about one.
%macro TM_INK 1
    push ax
    mov al, %1
    call OSAPI_SET_COLOR
    pop ax
%endmacro

; -----------------------------------------------------------------------------
; tm_init - total conventional RAM and the window's derived geometry
;
; Called from tm_entry, which the loader runs on the UI task - so the int 12h
; call honours the only-the-UI-task-calls-BIOS rule, exactly as it did from
; kmain. It runs per LAUNCH now rather than once at boot, which costs one int
; 12h and a divide and buys the geometry being recomputed if the adapter ever
; changed underneath us.
;
; in:       nothing
; out:      nothing; [tm_totkb] and the tm_tpl geometry set
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_init:
    push ax
    push bx
    push cx
    push dx
    int 0x12                    ; AX = conventional memory, KB
    mov [tm_totkb], ax
    call OSAPI_VIDEO            ; and the live geometry (SPEC.md 39.2): AX =
    mov [tm_vw], ax             ; width, CX = the dock strip's top row. Banked
    mov [tm_vdock], cx          ; because everything below spends AX..DX

    ; --- fit the window between the menu bar and the dock ---------------------
    ; The template used to carry a fixed 312, which is fine on VGA's 480 rows
    ; and eight pixels too many on Hercules' 348 - so the window hung over the
    ; dock strip, and dock_paint then had to draw UNDER a window instead of
    ; over the desktop, which is the one case wm_fast_ok declines (SPEC.md
    ; 11.90). Every window opened on top of it paid for that with a full
    ; repaint. On CGA's 200 rows it never fitted at all.
    ;
    ; So the height is derived, once, at boot: as many process rows as the
    ; space between MBAR_H and [vid_dock_y0] will take, and never more than
    ; the table has records for.
    ;
    ; One px of that space is spent before the frame gets any of it, because
    ; wm_dock_clear tests y+h against [vid_dock_y0] with `jae`: the drop
    ; shadow lives on row y+h, so a frame that merely REACHES the dock is
    ; already covering its first row. wm_fit will happily hand out exactly
    ; that (it clamps y to dock_y0 - h), and the whole point of this is that
    ; it must not.
    mov ax, [tm_vdock]
    sub ax, MBAR_H + 1          ; AX = px available to the whole frame
    sub ax, TITLE_H + 1 + TMM_ROW_Y     ; ...less the chrome and the maps
    jbe .norows
    mov bl, TM_ROW_H
    div bl                      ; AL = rows that fit in ONE column
    xor ah, ah
    jmp short .haverows
.norows:
    mov ax, 1                   ; a screen this short still gets one row
.haverows:
    mov [tm_colrows], ax        ; ...and that is the column's depth

    ; --- one column, or two? -------------------------------------------------
    mov word [tm_cols], 1
    mov bx, TM_W
    cmp ax, TM_COL2_MIN
    jae .cols                   ; tall enough: one column shows a useful list
    mov dx, [tm_vw]             ; short: a second column IF the screen is wide
    cmp dx, TM_W + TM_COLW      ; enough to carry it
    jb .cols
    mov word [tm_cols], 2
    mov bx, TM_W + TM_COLW
.cols:
    mov [tm_tpl+4], bx          ; the frame width the layout needs

    mov ax, [tm_colrows]        ; the height is COLUMN 0's worth: it is the
    cmp ax, TMM_ROWS            ; column that starts lowest, so it is the one
    jbe .hok                    ; the frame has to be tall enough for
    mov ax, TMM_ROWS
.hok:
    mov dx, TM_ROW_H
    mul dx
    add ax, TITLE_H + 1 + TMM_ROW_Y
    mov [tm_tpl+6], ax          ; the frame height the machine can take

    ; --- how deep is a LATER column? -----------------------------------------
    ; Not [tm_colrows]: a later column is top-anchored (TM_C2_ROW_Y), so it
    ; holds every row the frame has height for, which on a short screen is
    ; three times what column 0 gets.
    sub ax, TITLE_H + 1 + TM_C2_ROW_Y
    jbe .norow2
    mov bl, TM_ROW_H
    div bl                      ; AL = rows in one top-anchored column
    xor ah, ah
    jmp short .haverow2
.norow2:
    mov ax, 1
.haverow2:
    mov [tm_col2rows], ax

    mov bx, [tm_cols]           ; the whole list's depth: column 0, plus every
    dec bx                      ; later column's own
    mul bx
    add ax, [tm_colrows]
    cmp ax, TMM_ROWS
    jbe .rowcap
    mov ax, TMM_ROWS            ; never more than there is data for
.rowcap:
    mov [tm_maxrow], ax

    mov ax, [tm_tpl+6]          ; (the y clamp below wants the height back)

    mov dx, [tm_vdock]          ; ...and a top that keeps y+h off the dock.
    dec dx                      ; Deriving the height is not enough on its
    sub dx, ax                  ; own: wm_fit clamps y to dock_y0 - h, which
    cmp dx, 100                 ; puts the shadow row back on the strip for
    jge .ykeep                  ; any frame tall enough to be clamped at all
    cmp dx, MBAR_H              ; (signed - a tiny screen can drive this below
    jge .yset                   ; the menu bar, and there it simply cannot win)
    mov dx, MBAR_H
.yset:
    mov [tm_tpl+2], dx
.ykeep:

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_kinit - bind the window and ask for the byte-aligned content origin
;
; The loader zeroes our bss, so the state block does not need clearing the way
; it did as a built-in kind: a relaunched instance is a fresh image with a
; fresh bss and cannot inherit the previous one's calibration or graph.
;
; in:  BX = window ptr
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tm_kinit:
    push ax
    mov [tm_win], bx

    mov al, 1                   ; every row this window draws is an opaque
    call OSAPI_WM_SNAP          ; text run, so it asks for its content origin
                                ; on a multiple of 8 and every one of them
                                ; reaches font_run's single-store path
                                ; (SPEC.md 11.94/6.1). A no-op on VGA. It
                                ; PRESERVES FLAGS, which the entry proc's own
                                ; ret owes the loader

    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_entry - the package entry proc (SPEC.md 20.2)
; in:  nothing; DS = CS = our segment, ES = KERNEL_SEG, UI task, no lock
; out: CF = 0 launched, CF = 1 the loader tears us down
;
; The worker is NOT spawned here, and cannot be: the kernel sets I_STATE = 1
; and binds our window to the record only after this returns (SPEC.md 21),
; so OSAPI_TASK_SPAWN would find no instance and refuse. tm_paint does it -
; the apps/fractal precedent (SPEC.md 40).
; -----------------------------------------------------------------------------
tm_entry:
    push si
    call tm_init                ; int 12h, then the template's live geometry
    mov si, tm_tpl
    call OSAPI_WM_CREATE        ; BX = window ptr, CF = the table is full
    jc .out
    call tm_kinit               ; preserves the flags, so the CF our ret owes
.out:                           ; the loader is wm_create's
    pop si                      ; POP leaves the flags alone
    ret

; -----------------------------------------------------------------------------
; tm_hire - claim the worker task, retrying until one is free (SPEC.md 20.6)
; in:  [tm_win] valid; called from tm_paint, which holds the gfx lock
; out: nothing (all registers preserved)
;
; Latches on SUCCESS only, so a refusal is retried by the next paint rather
; than being permanent - MAX_TASKS is 12 and shared, and closing one Timer
; frees a slot. Without a worker the window is still correct, just static
; until something repaints it.
; -----------------------------------------------------------------------------
tm_hire:
    cmp byte [tm_spawned], 0
    jne .out
    push ax
    push bx
    mov ax, tm_worker
    mov bx, [tm_win]
    call OSAPI_TASK_SPAWN       ; CF=1 refused: nothing was created
    jc .norun
    mov byte [tm_spawned], 1
.norun:
    pop bx
    pop ax
.out:
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
tm_tpl:     dw 250, 100, 232, 312, tm_ttl, tm_paint, 0, tm_click
tm_ttl:     db 'Task Manager', 0
tm_sname:   db 'TaskMgr', 0     ; KD_NAME: fits the memory view's 7-char
                                ; NAME column, which is the tighter of the two

tm_s_cpu:   db 'CPU ', 0
tm_s_pre:   db 'SCH preempt', 0 ; read-only scheduler-mode field, chars 9..19
tm_s_coop:  db 'SCH coop   ', 0 ; both exactly 11: the padding erases the
                                ; longer word when the mode changes
tm_s_ram:   db 'RAM ', 0
; Each caption sits at the LEFT of the column the widths above name, and the
; gap after it is that column's width less the caption's own - so the header
; is derived from the layout rather than typed out beside it, and it cannot
; drift off its columns the way two hand-written strings do. ST's column is
; TM_STC plus the explicit separator the row builders put after the state.
; The gap before MEM comes to two rather than one, which it has to: 100%
; next to 335K with a single space ran the two figures together.
tm_s_hdr:   db 'NAME'
            times TM_NAMEF - 4     db ' '
            db 'ST'
            times TM_STC + 1 - 2   db ' '
            db 'CPU'
            times TM_CPUC - 3      db ' '
            db 'MEM', 0
tm_s_sys:   db 'System', 0      ; row 0: the kernel's own share
tm_s_dash:  db '-', 0
tm_s_run:   db 'run', 0
tm_s_rdy:   db 'rdy', 0
tm_s_slp:   db 'slp', 0
tm_s_evt:   db 'evt', 0         ; no worker task: runs only in callbacks
tm_s_die:   db 'die', 0         ; I_STATE 2: closing, task not yet reaped
tm_s_free:  db '---', 0
tm_s_frtl:  db '  -     -', 0   ; free row's CPU + MEM columns

; --- memory view (SPEC.md 28) ------------------------------------------------
tm_s_mhdr:  db 'NAME     ADDR SIZE   HEAP', 0 ; 25 chars, the row width. The
                                ; NAME field is EIGHT wide plus a separator:
                                ; seven for the name and one for the indent a
                                ; nested row carries, so a seven-character
                                ; name still gets a gap before the address
                                ; Two spaces of gap, not one: a 150K back
                                ; buffer beside a 150K package ran the two
                                ; figures together at the old width
tm_s_heap:  db '  HEAP ', 0     ; + claimed/size KB (SPEC.md 50). The two
                                ; leading spaces are the RAM line's gap, and
                                ; the claim swatch is drawn into them
tm_s_t86:   db '8086  ', 0      ; the detected tier (SPEC.md 41.1), which
tm_s_t286:  db '286   ', 0      ; nothing else in the UI says out loud. Each
tm_s_t386:  db '386+  ', 0      ; name is padded to the SAME six columns, so
                                ; the XMS figures beside it do not shuffle
                                ; left and right between machines. The 'CPU '
                                ; in front of them is tm_s_cpu, the
                                ; performance view's own label - one string,
                                ; because it is one word
tm_s_xms:   db 'XMS ', 0        ; + used/size KB above 1MB (SPEC.md 41)
tm_s_pool:  db 'PACKAGES ', 0   ; + allocated/size KB of the pool. The two
                                ; caption lines are CAPS and the list's rows
                                ; are mixed case, which is the whole of the
                                ; distinction between a map's label and a row
tm_s_bhdr:  db 'Builtins', 0     ; no figures: a built-in owns no band on
                                ; either map - its code is inside Code+data
                                ; and its memory is heap claims, billed to
                                ; its own row
tm_s_phdr:  db 'Packages     ', 0; + allocated/size KB of the pool
tm_s_mfr:   db '   -    -', 0   ; the ADDR+SIZE pair a built-in has no answer
                                ; for: it owns no region of its own
tm_s_dash4: db '   -', 0        ; one empty KB column
tm_s_dash5: db '    -', 0       ; ...and one with tm_kcol's leading space
tm_s_sys0:  db '0600', 0        ; where the kernel starts: KERNEL_SEG, the
                                ; first paragraph above the BIOS data area

; The kernel's own buffers, one row each under System (SPEC.md 28). Every
; figure beside them is a compile-time constant (TM_K*_KB above), so drawing
; this list costs four string copies and no arithmetic at all - which is why
; it can sit on the once-a-second refresh path.
tm_s_bimg:  db '  Code+data', 0
tm_s_bstk:  db '  Stacks', 0
tm_s_bdsk:  db '  Disk bufs', 0
tm_s_bfat:  db '  FAT snap', 0

; The kernel-buffer texture for the RAM map (SPEC.md 28): 2-on-2-off
; horizontal bars. The band is 14 rows tall and, on a 640KB machine, four
; pixels wide - so a texture has to carry its signature VERTICALLY or it has
; nowhere to show it, which rules out every vertical and diagonal hatch and
; leaves this one. It reads as "reserved, and not code" against both the 50%
; checker the rest of the kernel span uses and the solid black of the package
; pool, and it survives the reduction to three inks (SPEC.md 39.4) where a
; second colour would not.
tm_pat_buf:
    db 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00

; ...and one for a live heap claim (SPEC.md 50). Claims used to be drawn in
; the same 50% checker as the kernel span, which made the RAM map say "gray"
; about two unrelated things: memory the kernel reserved at build time and
; memory something asked for at run time.
;
; The interior is deliberately LIGHT, because what has to be legible about a
; claim is its EDGES, not its middle: claims are the only bands on this map
; that come and go, they sit next to each other, and on a 640KB machine the
; scale is 4KB per pixel - a 3KB Disk-window cache is one column. So the band
; is framed in solid black by tm_map_claim and this fills the inside, which
; makes the frame the thing you read. A darker interior swallowed its own
; frame at four pixels wide, and the diagonal this replaces gave every claim
; a ragged edge that could not be told from the next one along. Every row
; carries a black column, though - at 8x8 with only every other row inked,
; the 6x6 legend swatch beside the HEAP caption came out empty.
tm_pat_clm:
    db 0xEE, 0xBB, 0xEE, 0xBB, 0xEE, 0xBB, 0xEE, 0xBB

; ...and the two the MAPS draw without a pattern at all, so that every legend
; square can go through one routine (tm_sq_pat). tm_pat_gray is exactly what
; gfx_fill_gray lays down - 0xAA on even rows, 0x55 on odd, and gfx_fill_pat
; indexes by the same y - so the square and the band it keys are the same
; pixels, not merely a similar grey.
tm_pat_gray:
    db 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
tm_pat_blk:                     ; a set bit is WHITE (SPEC.md 5), so solid
    db 0, 0, 0, 0, 0, 0, 0, 0   ; black is a pattern of no bits at all

; Slot patterns: 12 x 8 row bytes, pattern i = tm_pats + i*8, bit set =
; white (SPEC.md 5/28). Black-on-white hatch textures, none of them the
; AA/55 50% checker (that means "system-reserved" in the maps).
tm_pats:
    db 0x77, 0xFF, 0xDD, 0xFF, 0x77, 0xFF, 0xDD, 0xFF   ; 0: sparse dots
    db 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55   ; 1: fine vertical
    db 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00   ; 2: fine horizontal
    db 0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE   ; 3: thin diagonal
    db 0x00, 0x77, 0x77, 0x77, 0x00, 0x77, 0x77, 0x77   ; 4: grid
    db 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F   ; 5: thin diagonal /
    db 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33   ; 6: thick vertical
    db 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00   ; 7: thick horizontal
    db 0x00, 0xF7, 0xF7, 0xF7, 0x00, 0x7F, 0x7F, 0x7F   ; 8: bricks
    db 0x7E, 0xBD, 0xDB, 0xE7, 0xE7, 0xDB, 0xBD, 0x7E   ; 9: diamond
    db 0x3F, 0x9F, 0xCF, 0xE7, 0xF3, 0xF9, 0xFC, 0x7E   ; 10: thick diagonal
    db 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00   ; 11: dense dots

; -----------------------------------------------------------------------------
; tm_worker - the monitor task: spin-measure for TM_INT ticks, sample, redraw
; in:  DX = our instance index (the spawn argument, SPEC.md 8); DS = CS = our
;      segment, ES = KERNEL_SEG, SS = LOW_SEG. NEVER RETURNS - it leaves
;      through OSAPI_TASK_ALIVE when the close box is clicked.
;
; The order of the loop is the contract of SPEC.md 20.6 rule 4:
; OSAPI_TASK_ALIVE with the lock NOT held, because it takes the lock itself
; and gfx_lock is not reentrant. It is also the only teardown path - as a
; built-in this read I_STATE off its own instance record and far-jumped to
; inst_task_die, which is the same operation with the kernel's side of it
; exposed.
; -----------------------------------------------------------------------------
tm_worker:
.interval:
    call OSAPI_GET_TICKS
    mov [tm_t0], ax
    mov word [tm_cnt], 0
    mov word [tm_cnt+2], 0
.spin:
    add word [tm_cnt], 1        ; 32-bit spin counter
    adc word [tm_cnt+2], 0
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    sub ax, [tm_t0]             ; wrap-safe interval check
    cmp ax, TM_INT
    jb .spin

    mov bx, [tm_win]            ; close requested? (once per interval) - this
    call OSAPI_TASK_ALIVE       ; may never come back, and the lock is free
                                ; here, which rule 4 requires

    call tm_sample              ; no lock: computes load, shares, RAM

    call OSAPI_GFX_LOCK
    mov bx, [tm_win]
    test word [es:bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14).
    jz .skip                    ; ES is KERNEL_SEG here: task_spawn seeds a
                                ; worker's ES with it and every TM_ES_DATA in
                                ; this file puts it back
    call OSAPI_WM_CLIP_SET            ; a REGION, not wm_obscured's veto (SPEC.md
    jc .skip                    ; 11.3): one covered pixel used to skip the
                                ; whole refresh, so a Task Manager with a
                                ; corner under something stopped updating at
                                ; all. CF=1 here is "wholly invisible" or
                                ; "more than 16 fragments", which genuinely
                                ; do mean skip. gfx_unlock disarms it
    call tm_update              ; incremental redraw, lock held
.skip:
    call OSAPI_GFX_UNLOCK
    jmp .interval

; -----------------------------------------------------------------------------
; tm_sample - one measurement pass: load gauge, history push, per-instance CPU
;             shares, RAM usage. Touches only module state; no lock needed.
; in:  [tm_cnt] = the interval's spin count
; out: tm_load/tm_hist/tm_pos/tm_lastcol/tm_pct/tm_state/tm_self/tm_usedk/
;      tm_barw and the whole instance snapshot block updated
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_sample:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    TM_ES_DATA                  ; the heap's totals, for the RAM figure below
    mov di, tm_kb
    TM_SYS_KB

    ; --- phased-window max calibration (SPEC.md 28) --------------------------
    ; Epoch rotation first: every TM_EPOCH samples, the current epoch's max
    ; becomes the previous one and the current restarts from this count. A
    ; poisoned-high calibration (menu_track's cheap poll loop) ages out in
    ; at most two epochs; an all-time max would stick forever.
    inc byte [tm_epc]
    cmp byte [tm_epc], TM_EPOCH
    jb .noroll
    mov byte [tm_epc], 0
    mov ax, [tm_cmax]
    mov [tm_pmax], ax
    mov ax, [tm_cmax+2]
    mov [tm_pmax+2], ax
    mov word [tm_cmax], 0
    mov word [tm_cmax+2], 0
.noroll:
    mov ax, [tm_cnt+2]          ; tm_cmax = max(tm_cmax, count)
    cmp ax, [tm_cmax+2]
    ja .newmax
    jb .maxed
    mov ax, [tm_cnt]
    cmp ax, [tm_cmax]
    jbe .maxed
.newmax:
    mov ax, [tm_cnt]
    mov [tm_cmax], ax
    mov ax, [tm_cnt+2]
    mov [tm_cmax+2], ax
.maxed:
    ; effective max = max(tm_cmax, tm_pmax) - always >= count, since cmax
    ; was just raised to at least count (no clamp needed before the DIV)
    mov cx, [tm_cmax]
    mov dx, [tm_cmax+2]
    cmp dx, [tm_pmax+2]
    ja .load
    jb .usepmax
    cmp cx, [tm_pmax]
    jae .load
.usepmax:
    mov cx, [tm_pmax]
    mov dx, [tm_pmax+2]

    ; --- load% = 100 - 100*count/max_eff -------------------------------------
.load:
    mov ax, [tm_cnt]            ; normalize both to 16 bits (count <= max,
    mov bx, [tm_cnt+2]          ; so count's high word clears first)
.norm:
    test dx, dx
    jz .normed
    shr dx, 1
    rcr cx, 1
    shr bx, 1
    rcr ax, 1
    jmp .norm
.normed:
    jcxz .load0                 ; max is 0: nothing measurable yet
    mov bx, cx                  ; BX = max16
    mov cx, 100
    mul cx                      ; DX:AX = count16 * 100
    div bx                      ; AX = 100*count/max <= 100
    mov cx, 100
    sub cx, ax                  ; CX = load
    jmp .loadok
.load0:
    xor cx, cx
.loadok:
    mov [tm_load], cx

    ; --- history push: v = load*40/100, sweep index advances -----------------
    mov ax, cx
    mov cx, TM_GH
    mul cx
    mov cx, 100
    div cx                      ; AX = 0..40
    mov bx, [tm_pos]
    mov [tm_hist+bx], al
    mov [tm_lastcol], bx
    inc bx
    cmp bx, TM_GW
    jb .posok
    xor bx, bx
.posok:
    mov [tm_pos], bx

    ; --- snapshot scheduler + instance state (one atomic block, 8.1/28) ------
    ; Task cycles and T_STATE, then the whole instance table: I_STATE, I_TASK,
    ; I_SIZE, I_CYC and the 16 I_NAME bytes of every slot. TM_SYS_SNAPSHOT does
    ; the lot in one cli window - task_exit frees a record atomically with its
    ; task slot (SPEC.md 8), so nothing read here can be torn against itself,
    ; and that window is the whole reason the API cell is a table snapshot
    ; rather than a getter per record (SPEC.md 20.9).
    ;
    ; Unpacking it back into the arrays below rather than reading the buffer
    ; in place is deliberate: this module wants a per-slot array of each field
    ; anyway - the diffs, the previous sample and the appeared-slot rule all
    ; index by slot - and unpacking once is forty instructions against a
    ; stride multiply at every one of a hundred-odd read sites.
    TM_ES_DATA
    mov di, tm_snapshot
    TM_SYS_SNAPSHOT

    mov si, tm_snapshot + SS_TCYC
    mov di, tm_tnew
    mov cx, MAX_TASKS * 2
.csnap:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .csnap
    mov si, tm_snapshot + SS_TSTATE
    mov di, tm_state
    mov cx, MAX_TASKS
.ssnap:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .ssnap
    mov al, [tm_snapshot + SS_CUR]  ; = our own slot (we are running)
    mov [tm_self], al
    mov al, [tm_snapshot + SS_COOP] ; the scheduler's mode rides along (SPEC.md
    mov [tm_coop], al           ; 8.2): the caption redraws off this sample

    mov si, tm_snapshot + SS_INST   ; SI = record, BX = instance index
    xor bx, bx
.isnap:
    mov al, [si+SSI_STATE]
    mov [tm_ist+bx], al
    mov al, [si+SSI_TASK]
    mov [tm_itsk+bx], al
    mov al, [si+SSI_KIND]       ; bit 7 = package: the memory view groups on
    mov [tm_iknd+bx], al        ; it, and bills claims by it (SPEC.md 28/50)
    mov di, bx
    shl di, 1                   ; DI = index*2
    mov ax, [si+SSI_SIZE]
    mov [tm_isz+di], ax
    mov ax, [si+SSI_SEG]        ; region base for the memory view's map
    mov [tm_ispt+di], ax        ; (meaningless for built-ins: I_SIZE 0
                                ; filters them out, SPEC.md 28)
    mov ax, [si+SSI_KB]         ; ...and what it holds off the heap, which the
    mov [tm_ikb+di], ax         ; kernel worked out from the owner word rule
    shl di, 1                   ; DI = index*4
    mov ax, [si+SSI_CYC]
    mov [tm_inew+di], ax
    mov ax, [si+SSI_CYC+2]
    mov [tm_inew+di+2], ax
    mov ax, bx                  ; name goes to tm_inm + index*16
    mov cl, 4
    shl ax, cl
    mov di, ax
    add di, tm_inm
    push si
    add si, SSI_NAME
    mov cx, 16
.incpy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .incpy
    pop si
    add si, SSI_RECSZ
    inc bx
    cmp bx, INST_MAX
    jb .isnap

    ; --- per-task diffs ------------------------------------------------------
    xor bx, bx
.tdiff:
    mov ax, [tm_tnew+bx]
    mov dx, [tm_tnew+bx+2]
    sub ax, [tm_told+bx]        ; wrap-safe 32-bit difference
    sbb dx, [tm_told+bx+2]
    mov cx, [tm_tnew+bx]
    mov [tm_told+bx], cx
    mov cx, [tm_tnew+bx+2]
    mov [tm_told+bx+2], cx
    test dx, dx                 ; negative diff = the counter regressed (a
    jns .tsign                  ; slot reused mid-interval, a rare PIT stamp
    xor ax, ax                  ; race, or a task_debit that reached back
    xor dx, dx                  ; into the previous interval): force 0 - a
.tsign:                         ; wrapped "huge" diff would clamp rows to 100%
    mov si, bx                  ; appeared-slot rule (SPEC.md 28): a slot
    shr si, 1                   ; free last sample but used now had its
    shr si, 1                   ; counter reset by task_spawn - the diff is
    cmp byte [tm_pstate+si], 0  ; meaningless, force 0
    jne .tkeep
    cmp byte [tm_state+si], 0
    je .tkeep
    xor ax, ax
    xor dx, dx
.tkeep:
    mov [tm_tdif+bx], ax
    mov [tm_tdif+bx+2], dx
    add bx, 4
    cmp bx, MAX_TASKS * 4
    jb .tdiff

    ; --- per-instance diffs (same two rules; inst_alloc zeroes I_CYC) --------
    xor bx, bx
.idiff:
    mov ax, [tm_inew+bx]
    mov dx, [tm_inew+bx+2]
    sub ax, [tm_iold+bx]
    sbb dx, [tm_iold+bx+2]
    mov cx, [tm_inew+bx]
    mov [tm_iold+bx], cx
    mov cx, [tm_inew+bx+2]
    mov [tm_iold+bx+2], cx
    test dx, dx
    jns .isign
    xor ax, ax
    xor dx, dx
.isign:
    mov si, bx
    shr si, 1
    shr si, 1
    cmp byte [tm_pist+si], 0
    jne .ikeep
    cmp byte [tm_ist+si], 0
    je .ikeep
    xor ax, ax
    xor dx, dx
.ikeep:
    mov [tm_idif+bx], ax
    mov [tm_idif+bx+2], dx
    add bx, 4
    cmp bx, INST_MAX * 4
    jb .idiff

    xor bx, bx                  ; this sample's states become the previous
.pcopy:
    mov al, [tm_state+bx]
    mov [tm_pstate+bx], al
    inc bx
    cmp bx, MAX_TASKS
    jb .pcopy
    xor bx, bx
.picopy:
    mov al, [tm_ist+bx]
    mov [tm_pist+bx], al
    inc bx
    cmp bx, INST_MAX
    jb .picopy

    ; --- fold task + instance cycles into one cycle figure per ROW ----------
    ; Row 0 is the UI task's remainder (callbacks have already been debited
    ; off it); row 1+i is instance i's callback time plus, if it owns a task,
    ; that task's slice. Cycles of a task whose instance died this interval
    ; belong to no row and simply drop out of the total.
    mov ax, [tm_tdif]
    mov [tm_rcyc], ax
    mov ax, [tm_tdif+2]
    mov [tm_rcyc+2], ax

    xor bx, bx                  ; BX = instance index
.rowc:
    mov si, bx
    shl si, 1
    shl si, 1                   ; SI = index*4
    mov ax, [tm_idif+si]
    mov dx, [tm_idif+si+2]
    cmp byte [tm_ist+bx], 0     ; a free record's I_TASK byte is stale
    je .rnotask
    mov cl, [tm_itsk+bx]
    cmp cl, MAX_TASKS           ; catches 0xFF (task-less) and any garbage
    jae .rnotask
    xor ch, ch
    shl cx, 1
    shl cx, 1
    mov di, cx
    add ax, [tm_tdif+di]
    adc dx, [tm_tdif+di+2]
.rnotask:
    mov di, si
    add di, 4                   ; row index = instance index + 1
    mov [tm_rcyc+di], ax
    mov [tm_rcyc+di+2], dx
    inc bx
    cmp bx, INST_MAX
    jb .rowc

    xor ax, ax                  ; total = sum of the rows, so the shares of
    mov [tm_total], ax          ; a fully accounted system add up to 100
    mov [tm_total+2], ax
    xor bx, bx
.tsum:
    mov ax, [tm_rcyc+bx]
    mov dx, [tm_rcyc+bx+2]
    add [tm_total], ax
    adc [tm_total+2], dx
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .tsum

    ; normalize: shift total and every row right until total fits 16 bits
.tnorm:
    cmp word [tm_total+2], 0
    je .tnormed
    shr word [tm_total+2], 1
    rcr word [tm_total], 1
    xor bx, bx
.tshr:
    shr word [tm_rcyc+bx+2], 1
    rcr word [tm_rcyc+bx], 1
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .tshr
    jmp .tnorm
.tnormed:

    ; share_i = row_i*100/total; total 0 -> all 0; clamp keeps DIV safe
    xor bx, bx
    mov di, tm_pct
.pct:
    mov cx, [tm_total]
    jcxz .pzero
    mov ax, [tm_rcyc+bx+2]
    or ax, ax
    jnz .pfull                  ; pathological row > total: clamp
    mov ax, [tm_rcyc+bx]
    cmp ax, cx
    ja .pfull
    mov si, 100
    mul si                      ; DX:AX = row*100
    div cx                      ; AX <= 100: no overflow possible
    jmp .pstore
.pfull:
    mov ax, 100
    jmp .pstore
.pzero:
    xor ax, ax
.pstore:
    mov [di], al
    inc di
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .pct

    ; --- RAM: the kernel's footprint, plus everything claimed (SPEC.md 28) --
    ; TWO terms, not three. There used to be a third - a sum of every resident
    ; package REGION off the instance snapshot - because a region came out of
    ; a pool that the claim heap knew nothing about. Since SPEC.md 20.1 a
    ; region IS a heap claim, so mem_claimed_kb already counts every one of
    ; them and the sum counted them a second time.
    ;
    ; That is what let this figure exceed the machine: with a package loaded,
    ; used was over by its region, and a 20KB menu save-under landing on top
    ; of an already-inflated figure could read as more RAM than the machine
    ; has. It cannot now - claims live in the heap, the heap is what is left
    ; above the kernel, so kernel + claims is bounded by the total.
    mov ax, [tm_kb+SK_KERN]     ; the WHOLE kernel (SPEC.md 2): image,
                                ; scratch, FAT snapshot, disk buffers and
                                ; every task stack are one contiguous span,
                                ; and there is no growth room in it to bill
    add ax, [tm_kb+SK_CLAIM]    ; + everything claimed out of the heap
                                ; (SPEC.md 50): the back buffer while it is
                                ; armed, the menu save-under while a menu is
                                ; down, each open Disk window's listing
                                ; cache, and every package's region and data
                                ; - live, not a constant
    mov [tm_usedk], ax          ; ...and what is left really is free: the
                                ; unclaimed heap is the only thing not
                                ; counted here, and it is handed out on demand
    mov cx, [tm_totkb]          ; barw = usedK*TM_GW/totalK (all KB)
    jcxz .nobar
    mov si, TM_GW
    mul si                      ; DX:AX = usedK*TM_GW
    div cx
    cmp ax, TM_GW
    jbe .barok
    mov ax, TM_GW
.barok:
    mov [tm_barw], ax
    jmp .ramdone
.nobar:
    mov word [tm_barw], 0
.ramdone:

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing. All routines below run with the gfx lock held by the caller and
; draw self-backgrounding elements (each white-fills its own rect or paints
; both segments), so no separate content clear is needed. [tm_cx]/[tm_cy]
; hold the content origin, cached by tm_paint / tm_update.
; =============================================================================

; -----------------------------------------------------------------------------
; tm_paint - W_PAINT: bare, unconditional full redraw of the active view
; in:  SI = window ptr (gfx lock held by caller; no lock, no visibility
;      check in here - SPEC.md 28)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_paint:
    call tm_hire                ; the first paint is the earliest point at
    call tm_draw_full           ; which our instance exists to attach one to
    ret

; -----------------------------------------------------------------------------
; tm_draw_full - full redraw of whichever view [tm_view] selects (also the
;                tail of tm_click's in-place view swap)
; in:  SI = window ptr (gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; tm_view_begin - the setup every paint and every update does first
;
; Both views open by asking the window where its content is and how far down it
; may draw, and both did it inline until the second one of those answers grew a
; second consumer. [tm_rowx] is why it is one routine now: it is the left edge
; of the row being drawn, it equals [tm_cx] everywhere except inside the memory
; view's second column, and a view that forgot to seed it drew its rows at x=0 -
; white bands across the desktop, well outside the window, which is exactly what
; the process list did the first time the columns landed.
;
; [tm_ylim] is the row clamp. The row lists are fixed-pitch and TM_ROWS long,
; and nothing in the kernel clips a draw to a WINDOW - vga_rect_setup clips to
; the SCREEN. That was harmless while every template fitted, but since SPEC.md
; 39.7 wm_create clamps a frame onto the live screen, and on a 200-row CGA this
; window's 264px template becomes 156px: unbounded, the last rows would be
; white-filled and lettered over the dock strip, once a second.
; in:       BX = window ptr
; out:      [tm_cx]/[tm_cy] = content origin, [tm_rowx] = [tm_cx],
;           [tm_ylim] = the largest row top whose 8px band stays inside the
;           frame's bottom border
; clobbers: flags
; -----------------------------------------------------------------------------
tm_view_begin:
    push ax
    push dx
    push di
    push es
    TM_ES_DATA                  ; the kernel's footprint and the heap's
    mov di, tm_kb               ; totals, live: every figure below this point
    TM_SYS_KB                   ; reads tm_kb, and a paint may arrive before
    pop es                      ; the first sample ever runs
    pop di
    call OSAPI_WM_CONTENT             ; AX = content left, DX = content top
    mov [tm_cx], ax
    mov [tm_cy], dx
    mov [tm_rowx], ax           ; column 0 until tm_mrow_open says otherwise
    mov ax, [es:bx+W_Y]
    add ax, [es:bx+W_H]
    sub ax, 9                   ; 1px bottom border + the row's 8px band
    mov [tm_ylim], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rowck_clear - forget every row's last-drawn content, and every check
;                  word around them
;
; Called by both full-paint bodies, and that is the whole of the cache's
; invalidation rule: the ONLY things that can put different pixels in this
; window without going through a checked draw are a wm_draw_win content fill,
; a view switch and a resize - and all three arrive here. A window MOVE is a
; wm_paint_dmg repaint, so it arrives here too, which is what stops a cached
; "unchanged" from leaving a map behind at the window's old position.
; in:       nothing
; out:      nothing (all registers preserved)
;
; TWO spans, not one. This used to zero tm_elck and tm_rowck as a single run
; on the strength of a comment saying they were declared adjacent - and they
; were not: five unrelated words had drifted in between, so the run stopped
; five words short and the LAST rows of tm_rowck were never invalidated at
; all. Harmless while those were the memory list's two bottom rows, which are
; empty on any machine that fits its instances; fatal the moment the CPU
; caption became the last row, because a view switch then white-filled the
; content and left four fifths of 'CPU nnn% SCH preempt' recorded as drawn.
; An address and a count each, so the layout cannot say otherwise again.
; -----------------------------------------------------------------------------
tm_rowck_clear:
    push bx
    push cx
    mov bx, tm_elck
    mov cx, TMC_N
    call tm_ckz
    mov bx, tm_rowck
    mov cx, TM_NCK * TM_NCHUNK
    call tm_ckz
    pop cx
    pop bx
    ret

; tm_ckz - zero CX check words at BX (all registers preserved)
tm_ckz:
    push bx
    push cx
.z:
    mov word [bx], 0
    inc bx
    inc bx
    loop .z
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
tm_draw_full:
    call tm_rowck_clear
    cmp byte [tm_view], 0
    jne tm_draw_mem
    ; fall through: performance view

tm_draw_perf:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call tm_view_begin          ; [tm_cx]/[tm_cy]/[tm_rowx]/[tm_ylim]

    call tm_txt_cpu

    TM_INK CBLACK
    mov ax, [tm_cx]             ; graph frame (6,14)-(TM_RW,55)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_GF_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TM_GF_Y2
    call OSAPI_GFX_FRAME

    xor bx, bx                  ; every column; tm_pos is the sweep gap
.col:
    cmp bx, [tm_pos]
    je .gap
    call tm_col
    jmp .next
.gap:
    call tm_gapcol
.next:
    inc bx
    cmp bx, TM_GW
    jb .col

    call tm_txt_ram

    TM_INK CBLACK
    mov ax, [tm_cx]             ; bar frame (6,71)-(TM_RW,80)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2
    call OSAPI_GFX_FRAME
    call tm_bar

    TM_INK CBLACK
    mov cx, [tm_cx]             ; task-list header - once per COLUMN, because
    mov bx, [tm_cols]           ; each carries its own list
    mov dx, [tm_cy]
    add dx, TM_HDR_Y            ; column 0's sits under the graph and bar...
.hdr:
    push cx
    push dx
    add cx, TM_PEN              ; the PEN, not the band's left edge: the
    mov si, tm_s_hdr            ; captions have to stand over the columns
    call OSAPI_FONT_STR               ; they name, and tm_rows letters from here
    pop dx
    pop cx
    add cx, TM_COLW
    mov dx, [tm_cy]             ; ...every later one at the top, with its
    add dx, TM_C2_HDR_Y         ; column
    dec bx
    jnz .hdr
    call tm_rows

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_update - periodic incremental redraw (gfx lock held, window visible)
;
; Frames and the header are static and already on screen; only the dynamic
; elements are touched. Performance view: the CPU + scheduler-mode line,
; the freshly written column + the advanced sweep gap, RAM line + bar, and
; the task rows. Memory view: the RAM line, the pool caption, both map
; interiors and the rows (SPEC.md 28).
;
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_update:
    cmp byte [tm_view], 0
    jne tm_upd_mem
    push ax
    push bx
    push dx

    mov bx, [tm_win]
    call tm_view_begin          ; [tm_cx]/[tm_cy]/[tm_rowx]/[tm_ylim]

    call tm_txt_cpu
    mov bx, [tm_lastcol]
    call tm_col
    mov bx, [tm_pos]
    call tm_gapcol
    call tm_txt_ram
    call tm_bar
    call tm_rows

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_click - W_ONCLICK (an ordinary near proc; the kernel arrives through
;             the package dispatcher, SPEC.md 20.2): ANY content click
;             toggles the view (SPEC.md 28). ui.inc only feeds clicks to the
;             front window, so a raising click never lands here. The lock is
;             already held: white-fill the whole content and run the new
;             view's full paint body in place.
; in:  CX = x, DX = y (absolute, unused), SI = window ptr
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_click:
    push ax
    push bx
    push cx
    push dx

    xor byte [tm_view], 1
    mov bx, si
    call OSAPI_WM_CONTENT             ; AX = content left, DX = content top
    push ax                     ; ...and the far corner off the record, not
    mov ax, [es:bx+W_Y]            ; a pair of constants: this fill is the ONLY
    add ax, [es:bx+W_H]            ; thing that erases the outgoing view, and a
    sub ax, 2                   ; short one leaves its tail rows lettered
    mov cx, [es:bx+W_X]            ; under the incoming one (they were 198x245
    add cx, [es:bx+W_W]            ; against a content that had grown to 231x282).
    sub cx, 2                   ; TWO, not one: y+h-1 and x+w-1 are the frame's
                                ; own bottom and right BORDERS, and a fill that
                                ; reached them left the window open-sided until
                                ; something repainted the frame
    mov bx, dx
    mov dx, ax
    pop ax
    TM_INK CWHITE
    call OSAPI_GFX_FILL
    call tm_draw_full           ; SI = window ptr still

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_upd_mem - the memory view's periodic redraw (gfx lock held): everything
;              but the two static map frames and the header
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_upd_mem:
    push ax
    push bx
    push dx

    mov bx, [tm_win]
    call tm_view_begin          ; [tm_cx]/[tm_cy]/[tm_rowx]/[tm_ylim]

    mov ax, TMM_RAM_Y
    call tm_txt_ram_y
    call tm_map_ram
    call tm_cap_xms
    call tm_xbar
    call tm_rows_mem

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_draw_mem - the memory view's full redraw: frames + header + everything
;               tm_upd_mem redraws
; in:  SI = window ptr (gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_draw_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call tm_view_begin          ; [tm_cx]/[tm_cy]/[tm_rowx]/[tm_ylim]

    mov ax, TMM_RAM_Y
    call tm_txt_ram_y

    TM_INK CBLACK
    mov ax, [tm_cx]             ; RAM map frame (6,14)-(TM_RW,29)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TMM_M1_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TMM_M1_Y2
    call OSAPI_GFX_FRAME


    mov ax, [tm_cx]             ; XMS bar frame (6,71)-(TM_RW,80) - here and
    add ax, 6                   ; not in tm_xbar, which is element-checked and
    mov bx, [tm_cy]             ; skips itself on an empty pool
    add bx, TMM_XB_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TMM_XB_Y2
    call OSAPI_GFX_FRAME

    mov cx, [tm_cx]             ; header, at the rows' text x - once per
    mov bx, [tm_cols]           ; COLUMN, because each carries its own list
    mov dx, [tm_cy]
    add dx, TMM_HDR_Y           ; column 0's sits under the maps...
.hdr:
    push cx
    push dx
    add cx, 16
    mov si, tm_s_mhdr
    call OSAPI_FONT_STR
    pop dx
    pop cx
    add cx, TM_COLW
    mov dx, [tm_cy]             ; ...every later one at the top, with its
    add dx, TM_C2_HDR_Y         ; column
    dec bx
    jnz .hdr

    call tm_map_ram
    call tm_cap_xms
    call tm_xbar
    call tm_rows_mem

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_cap_xms - white-fill the caption band and draw "CPU 8086  XMS n/nK"
;              (tier, then used / sized above 1MB - SPEC.md 41.1/41.6)
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
;
; No map goes with it, and that is the honest layout rather than a missing
; feature: the two maps above are conventional memory and a magnified slice
; of conventional memory, and extended memory is in NEITHER - real mode has
; no address for it, which is the whole reason xm_copy exists (SPEC.md 41.5).
; A third bar would have to invent a scale of its own and would read as if
; it were part of the same span. So: the figures, directly under the last
; map, above the process list.
;
; On tier 0 - the target machine - this reads 'CPU 8086  XMS 0/0K' and costs
; the refresh one string compare, because xm_caps answers 0 having touched
; nothing (SPEC.md 41.9 rule 1). The tier shares this line rather than taking
; one of its own because it is the same fact: what the CPU is decides whether
; there can be any memory above 1MB at all, and on the machine this OS is for
; the honest reading of the whole line is "an 8086, so none".
;
; It needs no check word of its own either - tm_rowsum hashes the composed
; tm_str, so the tier folds into TMC_XMS's for free.
; -----------------------------------------------------------------------------
tm_cap_xms:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call OSAPI_XMEM_CAPS                ; AX = KB still free, SK_XMS = the whole pool
    mov bx, [tm_kb+SK_XMS]
    push bx
    sub bx, ax                  ; BX = KB in use: sized minus free
    mov ax, bx
    pop bx
    push ax                     ; --- the bar's width, worked out HERE, while
    push bx                     ; AX and BX still hold used and sized: the
    or bx, bx                   ; formatting below leaves both equal to sized
    jz .nobar
    mov si, TM_GW
    mul si                      ; DX:AX = used * TM_GW
    div bx
    cmp ax, TM_GW
    jbe .barok
    mov ax, TM_GW
.barok:
    mov [tm_xbarw], ax
    jmp short .barset
.nobar:
    mov word [tm_xbarw], 0      ; no pool: an empty bar, not an absent one
.barset:
    pop bx
    pop ax

    push ax
    push bx
    mov di, tm_str
    mov si, tm_s_cpu            ; 'CPU ' + the tier, ahead of the XMS figures:
    call tm_copy                ; the two belong together, because the tier is
    mov si, tm_s_t86            ; exactly what decides whether there can BE any
    push ax
    call OSAPI_CPU_INFO               ; AL = the tier (SPEC.md 41.1)
    cmp al, CPU_286
    pop ax
    jb .tier
    mov si, tm_s_t286
    je .tier
    mov si, tm_s_t386
.tier:
    call tm_copy
    mov si, tm_s_xms
    call tm_copy
    pop bx                      ; BX = total, AX = used
    pop ax
    call tm_put5                ; FIVE digits, not tm_kpair's three: AH=88h
    mov byte [di], '/'          ; answers up to 65,535 KB and tm_put3 emits a
    inc di                      ; non-digit above 999. Every other figure in
    mov ax, bx                  ; this window is bounded by 640K and fits
    call tm_put5
    mov byte [di], 'K'
    inc di
    mov byte [di], 0

    call tm_rowsum              ; unchanged? then so are its pixels
    mov bx, tm_elck + 2*TMC_XMS
    call tm_elchk
    jc .out

    mov ax, TMM_XMS_Y
    call tm_lfill
    jnc .draw                   ; a clip edge crosses it: neither half draws,
    mov word [bx], 0            ; so the key must not stand either
    jmp short .out
.draw:
    TM_INK CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_cy]
    add dx, TMM_XMS_Y
    mov si, tm_str
    call OSAPI_FONT_STR
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_pool_kb - how much heap the resident package REGIONS hold, in KB
; in:  the tm_ist/tm_isz snapshot
; out: AX = KB
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_pool_kb:
    push bx
    push cx
    push si
    push di
    xor bx, bx
    xor si, si
    mov cl, 10
.slot:
    cmp byte [tm_ist+si], 0
    je .next
    mov di, si
    shl di, 1
    mov ax, [tm_isz+di]         ; per slot in KB, not bytes summed and
    add ax, 1023                ; divided once: three big resident packages
    shr ax, cl                  ; overrun 65,535 BYTES and a word sum wraps
    add bx, ax                  ; silently. (A region is whole KB - the
                                ; loader claims ceil(file/1KB) - so the
                                ; per-slot rounding loses nothing.) The old
                                ; byte sum was safe only under the retired
                                ; 60KB pool, which capped it by construction
.next:
    inc si
    cmp si, INST_MAX
    jb .slot
    mov ax, bx
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_map_ram - the conventional-memory map interior (SPEC.md 28/50)
;
; Four kinds of band, and between them they now cover every byte the machine
; has: gray = the kernel's fixed reservation (low memory, then its own
; segment), black = the package pool, gray = every live heap claim at its
; real address, white = free. The old version painted a band for SND_SEG
; that no longer exists and left SAVE_SEG, FAT_SEG and task 0's stack white
; while billing them nowhere - the audit's headline finding.
; in:  [tm_totkb]
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_map_ram:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    push es                     ; the claim table, whole, before anything is
    TM_ES_DATA                  ; hashed or drawn: the walk below wants every
    mov di, tm_claims           ; record and the check word wants every record,
    TM_CLAIM_SNAPSHOT               ; so one call serves both (SPEC.md 20.9)
    pop es

    mov si, tm_claims           ; the claim map is the only part of this that
    mov cx, CLAIM_SNAPSHOT_SIZE      ; moves - the kernel and its buffers are fixed
    xor ax, ax                  ; at build time and the total is the boot int
    call tm_sumb                ; 12h figure - so it IS the key
    mov bx, tm_elck + 2*TMC_MRAM
    call tm_elchk
    jc .out

    TM_INK CWHITE
    xor ax, ax                  ; whole interior white first
    mov cx, TM_GW - 1
    mov bx, TMM_M1_Y1
    call tm_map_rect
    call OSAPI_GFX_FILL

    xor ax, ax                  ; [0, kernel end): the BIOS data area and the
    mov dx, [tm_kb+SK_KERN]     ; kernel entire - image, scratch, buffers and
    dec dx                      ; stacks (SPEC.md 2). 50% gray: reserved, and
    call tm_band                ; not available to anything else
    call OSAPI_GFX_FILL_GRAY

    mov ax, [tm_kb+SK_KERN]           ; ...and its BUFFERS, over the top of
    sub ax, [tm_kb+SK_BUF]            ; that, in a texture of their own: the
    mov dx, [tm_kb+SK_KERN]           ; FAT snapshot, the disk caches and
    dec dx                            ; every task stack are the part of the
    call tm_band                      ; kernel that is scratch rather than
    mov si, tm_pat_buf                ; program, and the part these figures
    TM_FILL_PAT                       ; are steered by (docs/KERNEL-MEMORY.md)


    xor si, si                  ; every live claim, at its real address
.claim:
    mov ax, si                  ; a record in the snapshot, not in mem_tab:
    mov cl, CLS_RECSZ           ; the whole table came over in one call at the
    mul cl                      ; top of this routine, so reading one here is
    mov di, ax                  ; a stride multiply and not a far call
    mov dx, [tm_claims+di+CLS_SEG]
    or dx, dx                   ; 0 = a free record
    jz .cnext
    mov cx, [tm_claims+di+CLS_PARA]
    push si
    mov si, cx                  ; bank the size; CL is about to become 6
    mov ax, dx
    mov cl, 6
    shr ax, cl                  ; AX = base KB (a segment is KB<<6)
    xchg ax, si                 ; SI = base KB, AX = paragraphs
    shr ax, cl                  ; AX = size KB (CL is still 6)
    mov dx, si
    add dx, ax
    dec dx                      ; DX = inclusive end KB
    mov ax, si
    call tm_band
    call tm_map_claim           ; framed, so its edges are unmistakable
    pop si
.cnext:
    inc si
    cmp si, MEM_MAX
    jb .claim

    ; --- ...and each resident package's REGION, over the top of its claim ---
    ; A region IS a claim now (SPEC.md 20.1), so the loop above has already
    ; drawn it as one. Redrawing it in the slot's own pattern is what keeps
    ; the legend true: the per-slot pattern means "this package's region"
    ; and it used to mean that on a map of its own. They sit at the TOP of
    ; the heap and the data claims at the bottom (SPEC.md 50.3), so on this
    ; map the two kinds separate visibly, which is the point of allocating
    ; them from opposite ends.
    xor si, si
.reg:
    cmp byte [tm_ist+si], 0
    je .rnext
    test byte [tm_iknd+si], KIND_PKG
    jz .rnext
    mov di, si
    shl di, 1
    mov ax, [tm_isz+di]
    or ax, ax                   ; a built-in owns no region
    jz .rnext
    push si                     ; the slot: .rnext still needs it
    mov di, si
    shl di, 1
    mov ax, [tm_isz+di]
    mov cl, 10
    shr ax, cl                  ; AX = size KB (I_SIZE is a whole KB now)
    mov di, [tm_ispt+di]        ; DI = base SEGMENT
    mov cl, 6
    shr di, cl                  ; -> base KB
    mov dx, di
    add dx, ax
    dec dx                      ; DX = inclusive end KB
    mov cl, 3                   ; SI = tm_pats + slot*8, this slot's own
    shl si, cl                  ; texture - and it has to be worked out BEFORE
    add si, tm_pats             ; tm_band, whose CX output is the rect's right
    mov ax, di                  ; edge and would be spent on the shift
    call tm_band
    TM_FILL_PAT
    pop si
.rnext:
    inc si
    cmp si, INST_MAX
    jb .reg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_band - a KB span of the conventional map, as a fill rect
; in:  AX = first KB, DX = last KB (inclusive)
; out: AX = x1, BX = y1, CX = x2, DX = y2, ready for any fill
; clobbers: flags
;
; Every band on that map is "these KB, in this texture", and writing that out
; longhand was eleven instructions of scale-both-ends-and-shuffle each - four
; times, once per band, with the claim loop spilling to the stack to do it.
; -----------------------------------------------------------------------------
tm_band:
    push si
    push dx
    call tm_ramkb2x
    mov si, ax
    pop ax
    call tm_ramkb2x
    mov cx, ax
    mov ax, si
    mov bx, TMM_M1_Y1
    pop si
    jmp tm_map_rect

; -----------------------------------------------------------------------------
; tm_map_rect - map a pair of interior pixel columns + a frame-top constant
;               to the absolute fill rect of a 14-row map band, clamping so
;               no region drops below 1px (SPEC.md 28)
; in:  AX = x1 px (0..159), CX = x2 px, BX = map frame top (TMM_M*_Y1)
; out: AX = x1, BX = y1, CX = x2, DX = y2, absolute
; clobbers: flags
; -----------------------------------------------------------------------------
tm_map_rect:
    add ax, 7
    add ax, [tm_cx]
    add cx, 7
    add cx, [tm_cx]
    cmp cx, ax
    jae .wok
    mov cx, ax                  ; sub-pixel region: still show 1px
.wok:
    add bx, [tm_cy]
    inc bx                      ; interior = frame rows +1..+14
    mov dx, bx
    add dx, 13
    ret

; -----------------------------------------------------------------------------
; tm_map_claim - draw one heap claim's band: light interior, hard black frame
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (a tm_map_rect result)
; out: nothing (all registers preserved)
;
; The frame is the point. A claim is the only kind of band on this map that
; appears and disappears while you watch, several of them sit shoulder to
; shoulder, and the scale is coarse - 4KB per pixel on a 640KB machine, so a
; 3KB Disk-window cache is a single column. A texture alone cannot say where
; one claim stops and the next starts at that size; a 1px rule can.
;
; gfx_frame degenerates gracefully: a one-column claim comes out as a solid
; vertical line, which is exactly what it should look like.
; -----------------------------------------------------------------------------
tm_map_claim:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, tm_pat_clm
    TM_FILL_PAT
    pop si
    TM_INK CBLACK
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_ramkb2x - KB -> conventional-map interior pixel column
; in:  AX = KB
; out: AX = px = KB*TM_GW/totalK, clamped to 0..TM_GW-1
; clobbers: flags
; -----------------------------------------------------------------------------
tm_ramkb2x:
    push cx
    push dx
    mov cx, TM_GW
    mul cx                      ; DX:AX = KB*TM_GW
    mov cx, [tm_totkb]
    jcxz .zero                  ; int 12h can't return 0, but stay DIV-safe
    div cx
    jmp .clamp
.zero:
    xor ax, ax
.clamp:
    cmp ax, TM_GW - 1
    jbe .done
    mov ax, TM_GW - 1
.done:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_rows_mem - the memory view's rows (SPEC.md 28)
;
; Grouped, not slot-ordered: System, then a Builtins heading and every live
; built-in indented under it, then a Packages heading and every live package.
; Free slots are not drawn at all - a column of dashes told the reader
; nothing that "12 slots, 3 used" does not.
;
; The two headings carry the numbers that used to be nowhere: how much of the
; KERNEL SEGMENT the built-ins share is spent (they have no memory of their
; own - they are kernel code running on the UI task's stack), and how much of
; the PACKAGE POOL is allocated. The CLAIM column is the heap (SPEC.md 50):
; what this instance asked for on top of everything above, which for Paint is
; most of the machine and used to be invisible.
;
; in:  the instance snapshot block
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rows_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov word [tm_mrow], 0

    ; --- row 0: the kernel's fixed reservation --------------------------------
    call tm_mrow_open
    push si
    mov si, tm_s_sys
    call tm_copy7
    pop si
    mov byte [di], ' '          ; row 0 has no indent, so it pays BOTH of the
    inc di                      ; NAME field's trailing columns here
    mov byte [di], ' '
    inc di
    push si
    mov si, tm_s_sys0           ; it starts at the bottom of low memory
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di
    mov ax, [tm_kb+SK_KERN]     ; the kernel, whole (SPEC.md 2)
    call tm_kcol
    mov byte [di], ' '          ; the HEAP column's own gap (tm_s_mhdr)
    inc di
    mov ax, [tm_kb+SK_KCLAIM]   ; ...and the kernel's own heap claims, KB
    call tm_kcol
    mov word [tm_sqp], tm_pat_gray  ; the kernel's band on the RAM map above
    call tm_mrow_close

    ; --- the kernel's own buffers, one row each -------------------------------
    ; Indented under System, and between them they account for every byte of
    ; it: image + scratch + cold code, the task stacks, the disk buffers and
    ; the FAT window are the whole of KERN_SIZE (SPEC.md 2). All four figures
    ; come out of one osapi_sys_kb call, and they sum to the System row above
    ; exactly - which is the property that block is built around, and which
    ; the cold segment quietly broke while these were constants of this
    ; module's own (SPEC.md 20.9).
    mov bx, tm_s_bimg           ; NO square: the image is drawn in the same
    mov cx, [tm_kb+SK_IMG]      ; gray as the System row above it, and a
    xor dx, dx                  ; square that repeats one is not a legend
    call tm_buf_row
    mov bx, tm_s_bstk
    mov cx, [tm_kb+SK_STK]
    add cx, [tm_kb+SK_STK0]
    mov dx, tm_pat_buf
    call tm_buf_row
    mov bx, tm_s_bdsk
    mov cx, [tm_kb+SK_DSK]
    mov dx, tm_pat_buf
    call tm_buf_row
    mov bx, tm_s_bfat
    mov cx, [tm_kb+SK_FAT]
    mov dx, tm_pat_buf
    call tm_buf_row

    ; --- the Builtins heading, then every live built-in -----------------------
    call tm_mrow_open
    push si
    mov si, tm_s_bhdr
    call tm_copy
    pop si
    call tm_mrow_close          ; NO square, and that is the information: a
                                ; built-in owns no band on either map. Its
                                ; code is already inside Code+data and its
                                ; memory is heap claims, billed to its own row

    xor si, si
.blt:
    cmp byte [tm_ist+si], 0
    je .bnext
    test byte [tm_iknd+si], KIND_PKG
    jnz .bnext
    call tm_inst_row            ; SI = slot; built-in shape (no region)
.bnext:
    inc si
    cmp si, INST_MAX
    jb .blt

    ; --- the Packages heading, then every live package ------------------------
    ; The sum comes FIRST, because tm_pool_kb needs DI and tm_mrow_open hands
    ; DI over as the string cursor - the two cannot overlap. It is the KB of
    ; HEAP the regions hold now, not of a pool: there is no denominator to
    ; put it over any more, so it takes the SIZE column like a package row
    ; does, and no square - the regions wear their own per-slot patterns on
    ; the map above (SPEC.md 20.1/28).
    call tm_pool_kb             ; AX = KB of region held
    push ax
    call tm_mrow_open
    push si
    mov si, tm_s_phdr
    call tm_copy
    pop si
    pop ax
    call tm_kcol
    call tm_mrow_close

    xor si, si
.pkg:
    cmp byte [tm_ist+si], 0
    je .pknext
    test byte [tm_iknd+si], KIND_PKG
    jz .pknext
    call tm_inst_row
.pknext:
    inc si
    cmp si, INST_MAX
    jb .pkg

    ; --- blank whatever rows the list did not reach ---------------------------
.blank:
    cmp word [tm_mrow], TMM_ROWS
    jae .done
    mov ax, [tm_maxrow]         ; ...and at what this screen actually shows
    cmp word [tm_mrow], ax
    jae .done
    call tm_mrow_open
    call tm_mrow_close
    jmp .blank
.done:

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_inst_row - one instance's row, indented under its heading
; in:  SI = instance slot; the snapshot block
; out: nothing (SI preserved); the row is drawn and [tm_mrow] advanced
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_inst_row:
    push ax
    push bx
    push cx
    push dx
    push di
    call tm_mrow_open
    mov byte [di], ' '          ; the indent: these live INSIDE the region
    inc di                      ; their heading names
    push si
    mov ax, si                  ; name: the I_NAME snapshot, seven wide, then
    mov cl, 4                   ; a separator. A name that USES all seven -
    shl ax, cl                  ; ARKANOI, SOLITAI - ran straight into the
    mov si, ax                  ; segment beside it without one: 'ARKANOI9E40'
    add si, tm_inm
    call tm_copy7
    pop si
    mov byte [di], ' '
    inc di

    mov bx, si
    shl bx, 1
    mov ax, [tm_isz+bx]         ; I_SIZE: 0 for a built-in
    mov dx, [tm_ispt+bx]        ; I_SPTR: the package's SEGMENT
    or ax, ax
    jz .noreg

    xchg ax, dx                 ; a package: segment as hex, size in KB
    call tm_put4x
    mov byte [di], ' '
    inc di
    xchg ax, dx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call tm_kcol
    push si                     ; legend square in the slot's pattern, which
    mov si, bx                  ; is how the row keys the pool map above
    shl si, 1
    shl si, 1
    add si, tm_pats
    mov [tm_sqp], si
    pop si
    jmp short .claim
.noreg:                         ; a built-in: it owns no region, and says so
    push si
    mov si, tm_s_mfr
    call tm_copy
    pop si
.claim:
    mov byte [di], ' '          ; the HEAP column's own gap (tm_s_mhdr)
    inc di
    call tm_inst_claim          ; SI = slot; AX = KB claimed off the heap
    call tm_kcol
    call tm_mrow_close
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_inst_claim - how much heap has this instance claimed? (SPEC.md 50.5)
; in:  SI = instance slot
; out: AX = KB
; clobbers: nothing else (flags)
;
; A package's claims are stamped with its segment and a built-in's with its
; slot (SPEC.md 50.2), so the owner word depends on which this is.
; -----------------------------------------------------------------------------
tm_inst_claim:
    push bx
    mov bx, si
    shl bx, 1
    mov ax, [tm_ikb+bx]         ; the snapshot already carries it: the kernel
    pop bx                      ; applies the owner-word rule while it has the
    ret                         ; record open (SPEC.md 20.9)

; -----------------------------------------------------------------------------
; tm_buf_row - one indented kernel-buffer row: name, size and legend square
; in:  BX = the buffer's NUL name, CX = its size in KB, DX = the pattern its
;      band is drawn in on the RAM map, or 0 for the kernel's 50% gray
; out: nothing (all registers preserved)
;
; The name is given the ADDR column's width as well as its own, because a
; buffer's segment is an implementation detail and its NAME is the thing worth
; twelve characters. SIZE and CLM land exactly where row 0 puts them: the
; System row above pads to 8, writes four ADDR characters and then two
; five-wide KB columns, so padding to 12 and writing the same two columns
; keeps every figure in this view in one pair of vertical rules.
;
; The CLM column is a dash on purpose. A buffer is not a claim - it is part of
; the kernel, present whether or not anything is running - and the whole point
; of these four rows is that the System figure above them is not a lump.
; -----------------------------------------------------------------------------
tm_buf_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push cx                     ; the size, banked across tm_mrow_open
    call tm_mrow_open
    or dx, dx                   ; a square only where the texture is this
    jz .name                    ; row's OWN - the three buffers share one
    mov word [tm_sqox], 22      ; band, and it is theirs. Indented to sit
    mov [tm_sqp], dx            ; beside the names rather than out at the
                                ; margin two columns to their left
.name:
    pop ax
    push ax
    mov si, bx
    call tm_copy                ; the name carries its own two-space indent
    mov cx, tm_str + 13         ; the NAME field plus its separator: a buffer
    sub cx, di                  ; has no address, so the name spans both
    jle .size
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.size:
    pop ax
    call tm_kcol
    mov byte [di], ' '          ; the HEAP column's own gap (tm_s_mhdr)
    inc di
    push si
    mov si, tm_s_dash5          ; HEAP: a buffer is not a claim
    call tm_copy
    pop si
    call tm_mrow_close
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_row_place - where on screen does list row AX go?
;
; Both views' lists run down one column and, on a screen too short to show a
; useful number of entries, wrap into a second one beside it (SPEC.md 28.1) -
; so the index-to-pixel mapping is here rather than in each of them. The order
; is COLUMN-MAJOR, which is what makes "this row has no place" monotone: once
; one row is refused, so is every row after it, and a caller may stop.
;
; The columns are NOT the same depth. Column 0 begins under the maps, where
; the caller's BX puts it; every later column begins at TM_C2_ROW_Y, the top
; of the content, because nothing is drawn above it there.
;
; in:       AX = row index, BX = COLUMN 0's first-row content-relative y
; out:      [tm_rowx]/[tm_rowy] = that row's top-left; CF=1 if the row has no
;           place on this screen and must not be drawn
; clobbers: flags
; -----------------------------------------------------------------------------
tm_row_place:
    push ax
    push bx
    push cx
    push dx

    push ax                     ; the index again, for the tm_maxrow test
    mov cx, [tm_cx]
    cmp ax, [tm_colrows]
    jb .place                   ; inside column 0: BX and CX already stand

    sub ax, [tm_colrows]        ; ...otherwise which LATER column, and how far
    xor dx, dx                  ; down it
    div word [tm_col2rows]
    inc ax                      ; 0 was column 1
    push dx                     ; DX is the row within the column and `mul`
    mov bx, ax                  ; writes it - it does not survive this
    mov ax, TM_COLW
    mul bx
    pop dx
    add cx, ax
    mov bx, TM_C2_ROW_Y         ; top-anchored, whatever column 0 does
    mov ax, dx
.place:
    mov [tm_rowx], cx
    mov cx, TM_ROW_H            ; y1 = cy + the column's first row
    mul cx                      ;      + TM_ROW_H * row-within-column
    add ax, bx
    add ax, [tm_cy]
    mov [tm_rowy], ax
    pop cx                      ; CX = the row index

    cmp ax, [tm_ylim]           ; does this row's 8px band still fall inside
    ja .nofit                   ; the frame? Nothing in the kernel clips a
    cmp cx, [tm_maxrow]         ; draw to a WINDOW (tm_view_begin), and a short
    jae .nofit                  ; screen clamps this window hard - and a row
    clc                         ; past the LAST column's depth has no place at
    jmp short .out              ; all, in either direction. Unbounded, the tail
.nofit:                         ; of the list would be lettered over the dock
    stc                         ; strip once a second
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_mrow_open - start a memory-view row: compute x and y, DI = the text buffer
; in:  [tm_mrow]
; out: DI = tm_str, [tm_rowx]/[tm_rowy]/[tm_mfit] set
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_mrow_open:
    push ax
    push bx
    mov ax, [tm_mrow]
    mov bx, TMM_ROW_Y
    call tm_row_place
    mov byte [tm_mfit], 1
    jnc .fits
    mov byte [tm_mfit], 0
.fits:
    mov word [tm_sqox], 6       ; the legend square's left edge, at the margin
                                ; unless the row moves it (tm_buf_row)
    mov word [tm_sqp], 0        ; ...and no square unless the row asks for one
    pop bx
    pop ax
    mov di, tm_str
    ret

; -----------------------------------------------------------------------------
; tm_mrow_close - terminate and draw the row, then advance [tm_mrow]
; in:  DI = one past the text, [tm_rowy]
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tm_mrow_close:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [tm_mrow]
    shl bx, 1
    cmp byte [tm_mfit], 0       ; off the bottom: nothing was drawn, and
    je .forget                  ; forget what used to be here so it comes
    mov byte [di], 0            ; back if the window ever grows
    mov ax, [tm_rowx]           ; this list letters from tm_rowx+TM_MPEN,
    add ax, TM_MPEN             ; leaving the band's first pixels for the square
    mov [tm_penx], ax
    mov bx, [tm_mrow]
    call tm_row_draw            ; chunked: only what changed, and only what
    jc .out                     ; the region lets us (SPEC.md 11.3/28)
    mov si, [tm_sqp]            ; something drew, so the legend square may
    or si, si                   ; have been erased with it - put it back
    jz .out
    call tm_sq_pat
    jmp short .out
.forget:
    mov word [tm_rowck+bx], 0
.out:
    inc word [tm_mrow]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rowfill - white the row band at [tm_rowx],[tm_rowy], one column wide
; in:  [tm_rowx], [tm_rowy]
; out: nothing (all registers preserved)
; Both views' row loops erase before they letter, and only once they know the
; row changed, so the rect lives in one place rather than in each of them.
; -----------------------------------------------------------------------------
; tm_lfill is the same band named by a CONTENT-RELATIVE y, which is how the
; caption lines above the list ask for it. They are chrome and live in column 0
; whatever the list is doing, so this says so rather than relying on being
; called before the row loop has moved [tm_rowx] - every row draw sets it back
; through tm_row_place, so putting it back here costs nothing.
tm_lfill:
    push ax
    mov ax, [tm_cx]
    mov [tm_rowx], ax
    pop ax
    push ax
    add ax, [tm_cy]
    mov [tm_rowy], ax
    call tm_rowok               ; a caption erases a band and then letters it,
    jc .out                     ; so BOTH halves answer to one test and the
    call tm_rowfill             ; caller skips its font_str on CF=1 (SPEC.md
.out:                           ; 11.3's granularity rule)
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_row_draw - draw one list row, a CHUNK at a time (SPEC.md 11.3/28)
;
; in:  [tm_rowx]/[tm_rowy] placed, [tm_penx] = the text's left edge, tm_str
;      composed, BX = the row index; gfx lock held, clip armed
; out: CF = 0 something was drawn, CF = 1 nothing was; all registers preserved
;
; ONE routine now answers both of the questions a row draw has to ask, and it
; asks them at the same granularity: a chunk of TM_CHUNK characters.
;
; **What changed?** A row used to carry a single check word over its whole
; composed string, so a CPU percentage ticking over redrew the name, the state
; and the memory figure with it. Measured on an idle machine that was ~49
; glyphs per refresh for three fields' worth of change. Per chunk, a row whose
; percentage moved redraws the four or five characters that moved.
;
; **Can it be drawn?** The granularity rule: a fill clips per PIXEL and a
; glyph per whole CELL, so erase-then-letter blanks anything the region cuts.
; A whole-row test threw the row away to protect one cut cell; a chunk is
; TM_CW wide, so a vertical edge costs one chunk instead of the row, and a
; chunk that IS cut zeroes its own check word so it retries rather than being
; recorded as drawn while blank.
;
; The string is zero-padded to the full chunk span first, so every chunk
; hashes deterministically and a row that got SHORTER changes the chunk it
; lost its characters from - which is what erases them.
; -----------------------------------------------------------------------------
tm_row_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es

    mov di, tm_str              ; pad to TM_NCHUNK*TM_CHUNK with SPACES, and
    mov cx, TM_NCHUNK * TM_CHUNK + 1    ; terminate one past the end
    xor al, al
    cld
    repne scasb                 ; DI just past the NUL, CX = bytes after it
    dec di                      ; ...back ONTO it: the pad overwrites it
    inc cx
    jcxz .padded
    mov al, ' '                 ; SPACES, not zeros. font_run paints a space
    rep stosb                   ; as background on its fast path (the glyph's
                                ; rows are all clear, so the mask leaves the
                                ; background byte) - which is exactly the
                                ; field-fill this row wants, and is the
                                ; opposite of font_str, where a space draws
                                ; nothing at all
.padded:
    mov byte [tm_str + TM_NCHUNK * TM_CHUNK], 0
    mov ax, bx                  ; this row's check words: TM_NCHUNK of them
    mov cl, TM_NCHUNK
    mul cl                      ; AX = row * TM_NCHUNK (both are small)
    shl ax, 1
    add ax, tm_rowck
    mov [tm_ckb], ax            ; &tm_rowck[row][0], stepped a chunk at a time
    mov word [tm_ci], 0
    xor si, si                  ; SI = 0: nothing drawn yet

.chunk:
    push si                     ; the "drew something" flag rides the stack
    mov si, [tm_ci]
    mov ax, si
    mov cl, TM_CHUNK
    mul cl
    add ax, tm_str              ; AX -> this chunk's characters
    mov di, ax
    call tm_chunksum            ; AX = their hash (DI preserved)
    mov bx, [tm_ckb]
    call tm_elchk               ; changed? (BX = this chunk's check word)
    jc .next                    ; no - its pixels are already right
    call tm_row_lead            ; the FIRST chunk owns the band's LEAD-IN too
    jc .rect                    ; - and erasing it counts as a draw, because
    pop si                      ; the legend square lives in it and the caller
    inc si                      ; has to know to put it back
    push si
.rect:
    mov ax, [tm_ci]             ; the chunk's band: [penx + i*TM_CW, +TM_CW)
    mov cl, TM_CW
    mul cl
    add ax, [tm_penx]
    mov cx, ax
    add cx, TM_CW - 1
    push ax
    mov ax, [tm_ci]             ; ...and the LAST chunk runs on to the row's
    inc ax                      ; right edge instead, so the tail past the
    cmp ax, TM_NCHUNK           ; text - which no chunk covers, the pen being
    jb .x2ok                    ; inset from the band - is scrubbed by
    mov cx, [tm_rowx]           ; something
    add cx, TM_RW
.x2ok:
    pop ax
    mov dx, [tm_rowy]
    mov bx, dx
    add dx, 7                   ; AX/BX/CX/DX = the chunk's rect
    call OSAPI_WM_CLIP_TEST           ; NOT a veto any more - font_run decides per
    pushf                       ; CELL on both its paths (SPEC.md 6.1.2), so
                                ; this only asks whether the chunk will be
                                ; drawn WHOLE, which is whether its check word
                                ; may be believed next time

    push ax                     ; --- the chunk, as ONE opaque run ----------
    push cx
    push dx
    mov cx, ax                  ; x
    mov dx, [tm_rowy]           ; y
    mov si, di                  ; DI -> this chunk's TM_CHUNK characters
    mov bx, si                  ; terminate at the chunk's end and put the
    add bx, TM_CHUNK            ; byte back after: the row is one 25-character
    mov ah, [bx]                ; string and font_run runs to a NUL. The
    mov byte [bx], 0            ; buffer has room for the last chunk's, which
    push bx                     ; lands on tm_str[25] of TM_STRMAX = 28
    push ax
    mov al, CBLACK              ; AL = ink, AH = background: the erase and the
    mov ah, CWHITE              ; letters as one decision per cell
    call OSAPI_FONT_RUN
    pop ax
    pop bx
    mov [bx], ah
    pop dx
    pop cx
    pop ax

    cmp word [tm_ci], TM_NCHUNK - 1     ; the LAST chunk owns the band's tail
    jne .gdone                          ; too - the pen is inset, so the row
    push ax                             ; runs past the 25 cells and nothing
    push bx                             ; else would scrub it
    push cx
    push dx
    mov ax, [tm_penx]
    add ax, TM_NCHUNK * TM_CW
    mov cx, [tm_rowx]
    add cx, TM_RW
    mov bx, [tm_rowy]           ; y1/y2 FROM THE ROW, not from whatever the
    mov dx, bx                  ; run above left behind - terminating the
    add dx, 7                   ; chunk needed a register and BX was it
    cmp ax, cx
    ja .tdone
    TM_INK CWHITE
    call OSAPI_GFX_FILL
.tdone:
    pop dx
    pop cx
    pop bx
    pop ax
.gdone:
    popf
    jnc .kept
    mov bx, [tm_ckb]            ; an edge cut it, so some cell was skipped:
    mov word [bx], 0            ; forget the record and ask again next time
.kept:
    pop si
    inc si                      ; something was drawn
    push si
.next:
    pop si
    inc word [tm_ci]
    mov bx, [tm_ckb]            ; the check word for the NEXT chunk
    add bx, 2
    mov [tm_ckb], bx
    mov ax, [tm_ci]
    cmp ax, TM_NCHUNK
    jb .chunk

    or si, si
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jnz .drew
    stc
    ret
.drew:
    clc
    ret

; -----------------------------------------------------------------------------
; tm_row_lead - erase the row band's lead-in: the inset between the band's
;               left edge and the pen, which belongs to no chunk
; in:  [tm_ci] (acts only on chunk 0), [tm_rowx]/[tm_penx]/[tm_rowy]
; out: CF = 0 it filled, CF = 1 it did not; all registers preserved
;
; The band is [rowx+6, rowx+TM_RW] and the memory list letters from rowx+16,
; leaving ten pixels at the left that no chunk covers - and that is exactly
; where the legend square goes. tm_rowfill used to erase the whole band, so
; the square went with the text and came back with it; chunks start at the
; PEN, so nothing erased it any more and a row that went away left its square
; behind forever. Closing a package was the way to see it: the name vanished
; and its little swatch stayed, on a row that no longer existed.
;
; It is the mirror of the last chunk's fill running on to the band's right
; edge, and for the same reason - the pen is inset, so the band is wider than
; the chunks at both ends. The process list letters from rowx+6 and has no
; lead-in at all, which the width test below answers with CF=1.
;
; The clip needs no thought here: this is a plain fill, fills clip per pixel,
; and there are no glyphs in the lead-in for the granularity rule to disagree
; with. The square that goes back into it is a pattern fill, which clips the
; same way.
; -----------------------------------------------------------------------------
tm_row_lead:
    cmp word [tm_ci], 0
    jne .no
    push ax
    push bx
    push cx
    push dx
    mov ax, [tm_rowx]
    add ax, 6
    mov cx, [tm_penx]
    dec cx
    cmp cx, ax
    jb .none                    ; the pen IS the band's left edge: no lead-in
    mov bx, [tm_rowy]
    mov dx, bx
    add dx, 7
    TM_INK CWHITE
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.none:
    pop dx
    pop cx
    pop bx
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tm_chunksum - one word standing for TM_CHUNK characters at DI
; in:  DI -> the characters (already zero-padded by tm_row_draw)
; out: AX = the hash; DI preserved
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
tm_chunksum:
    push bx
    push cx
    push si
    mov si, di
    xor bx, bx
    xor ah, ah
    mov cx, TM_CHUNK
.next:
    lodsb
    rol bx, 1
    add bx, ax
    loop .next
    mov ax, bx
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_rowok - may the band at [tm_rowy] be drawn at all?
; out: CF = 0 draw it; CF = 1 a clip edge crosses it, so draw NOTHING - not
;      the fill, not the square, not the letters
; clobbers: nothing else (flags)
;
; Asked BEFORE the row's check word is updated, so a row skipped for being
; clipped is still owed a draw when the thing covering it goes away. It gets
; one either way: uncovering is a wm_paint_dmg repaint, which reaches
; tm_draw_full and clears every check word.
; -----------------------------------------------------------------------------
tm_rowok:
    push ax
    push bx
    push cx
    push dx
    mov bx, [tm_rowy]
    mov dx, bx
    add dx, 7
    mov ax, [tm_rowx]
    add ax, 6
    mov cx, [tm_rowx]
    add cx, TM_RW
    call OSAPI_WM_CLIP_TEST
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tm_rowfill:
    push ax
    push bx
    push cx
    push dx
    TM_INK CWHITE
    mov bx, [tm_rowy]
    mov dx, bx
    add dx, 7
    mov ax, [tm_rowx]
    add ax, 6
    mov cx, [tm_rowx]
    add cx, TM_RW
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rowsum - one word standing for the whole of tm_str
; in:  tm_str, NUL-terminated
; out: AX = the sum; every other register preserved
;
; What makes the once-a-second refresh nearly free: a row's NAME, its legend
; square, its ADDR and its SIZE/HEAP figures are all functions of this string,
; so one comparison answers "has anything in this row changed" for all of
; them at once - and the answer is no for most rows most of the time. A free
; slot never changes at all; a built-in's row changes only when it claims or
; releases something.
;
; Rotate-then-add rather than a plain sum, so that transposing two characters
; is not invisible. It is a hash and not a proof: a collision would leave one
; row stale for one interval, until its content moves again. For a status
; display refreshed twice a second that is the right trade against 38 bytes
; of .bss versus the 456 a full text cache would take.
; -----------------------------------------------------------------------------
tm_rowsum:
    push bx
    push si
    mov si, tm_str
    xor bx, bx
    xor ah, ah                  ; lodsb leaves AH alone, so this holds
.next:
    lodsb
    or al, al
    jz .done
    rol bx, 1
    add bx, ax
    jmp short .next
.done:
    mov ax, bx
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_sumb - the same hash over a COUNTED span, so it can key a table
; in:  SI = ptr, CX = bytes, AX = seed (0, or a previous result to chain)
; out: AX = the sum; SI advanced past the span, everything else preserved
;
; What the maps are keyed on. A map's pixels are a function of one table -
; the conventional map of the claim map (SPEC.md 50), the pool map of the
; instance snapshot - so hashing the table answers "has this map changed"
; without drawing anything. Reading the claim table unlocked can tear against
; a claim made on another task, and the worst that costs is one extra redraw:
; a torn read differs from the stored key, which is the safe direction.
; -----------------------------------------------------------------------------
tm_sumb:
    push bx
    push cx
    mov bx, ax
    xor ah, ah                  ; lodsb leaves AH alone, so this holds
    jcxz .done
.next:
    lodsb
    rol bx, 1
    add bx, ax
    loop .next
.done:
    mov ax, bx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_elchk - has the element whose check word is at BX changed to key AX?
; in:  AX = key, BX = a TMC_* check word's address
; out: CF = 1 unchanged (skip the draw), CF = 0 changed (and recorded)
; clobbers: nothing else
;
; Every checked draw in this window comes through here - the seven elements
; in the TMC_* list and each row of both lists - and tm_rowck_clear zeroes the
; lot. That single gate is where the never-zero rule below belongs.
; -----------------------------------------------------------------------------
tm_elchk:
    or ax, ax                   ; 0 is what tm_rowck_clear writes, so no LIVE
    jnz .cmp                    ; key may be 0 - an element whose state
    inc ax                      ; legitimately hashes to zero would read as
.cmp:                           ; "unchanged" on the very first paint and never
    cmp ax, [bx]                ; be drawn at all. That is not hypothetical:
    je .same                    ; mem_tab is all zeroes on a machine with no
    mov [bx], ax                ; heap to claim from, so on 128KB the RAM map
    clc                         ; hashed to 0 and stayed blank forever
    ret
.same:
    stc
    ret

; -----------------------------------------------------------------------------
; tm_kcol - one right-aligned KB column: 3 digits + 'K', or "   -" for zero
; in:  AX = KB, DI = dest
; out: DI advanced by 5 (a leading space, then the four)
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_kcol:
    push ax
    mov byte [di], ' '
    inc di
    or ax, ax
    jz .none
    call tm_put3
    mov byte [di], 'K'
    inc di
    jmp short .out
.none:
    push si
    mov si, tm_s_dash4
    call tm_copy
    pop si
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_sq_frame - the 1px black frame of the current row's legend square,
;               (6,rowy)-(13,rowy+7)
; in:  [tm_rowx], [tm_rowy]
; out: nothing
; clobbers: flags ([gfx_color] left black)
; -----------------------------------------------------------------------------
tm_sq_frame:
    push ax
    push bx
    push cx
    push dx
    TM_INK CBLACK
    mov ax, [tm_rowx]
    add ax, [tm_sqox]
    mov bx, [tm_rowy]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_sq_pat - a legend square in the 8-byte texture at SI
; in:  [tm_rowx], [tm_rowy], [tm_sqox]; SI = an 8-byte pattern
; out: nothing (all registers preserved)
;
; **The square keys the row to a band**, so there is exactly one texture per
; kind of memory across the two maps and a row may only wear the one its own
; memory is drawn in. That is not decoration: the System row used to carry a
; solid black square, and solid black became the PACKAGE POOL's band when the
; maps were reworked - so the legend was pointing at the wrong region entirely.
;
; Every kind goes through this one routine, including the two the maps draw
; with gfx_fill_gray and a plain black gfx_fill: tm_pat_gray is byte for byte
; what gfx_fill_gray lays down (0xAA on even rows, 0x55 on odd - the pattern
; fill indexes by the same y), and tm_pat_blk is a solid one. A square is 6x6;
; there is nothing to be gained by rendering it three different ways.
; -----------------------------------------------------------------------------
tm_sq_pat:                      ; SI = the pattern: the kernel's span, a
    push ax                     ; buffer, a claim, or one package's region
    push bx
    push cx
    push dx
    call tm_sq_frame
    call tm_sq_int              ; SI is already the pattern - the cell takes
    TM_FILL_PAT                 ; it there, and tm_sq_int leaves it alone
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_sq_int - the legend square's interior rect, ready for a fill
; in:  [tm_rowx], [tm_rowy]
; out: AX = x1, BX = y1, CX = x2, DX = y2 = (7,rowy+1)-(12,rowy+6)
; clobbers: flags
; -----------------------------------------------------------------------------
tm_sq_int:
    mov ax, [tm_rowx]           ; the ROW's left edge, not the content's: in a
    add ax, [tm_sqox]           ; second column those are 232px apart, and the
                                ; frame (tm_sq_frame) already knows it
    inc ax
    mov bx, [tm_rowy]
    inc bx
    mov cx, ax
    add cx, 5
    mov dx, bx
    add dx, 5
    ret

; -----------------------------------------------------------------------------
; tm_put4x - AX as exactly 4 uppercase hex chars at DI
; in:  AX = value, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_put4x:
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 0x0404              ; CH = digits left, CL = rotate count
.dig:
    rol bx, cl
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 'A' - '0' - 10
.put:
    mov [di], al
    inc di
    dec ch
    jnz .dig
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_col - draw one graph column from the history ring
; in:  BX = column index 0..TM_GW-1; value 0..40 from tm_hist
; out: nothing
; clobbers: nothing (flags only)
; White vline above the value, black vline below - self-backgrounding, so
; a column can be redrawn in place without a clear.
; -----------------------------------------------------------------------------
tm_col:
    push ax
    push bx
    push cx
    push dx
    push si

    mov al, [tm_hist+bx]
    cbw                         ; v is 0..40
    mov si, ax                  ; SI = v
    mov ax, [tm_cx]
    add ax, 7                   ; interior starts at content x=7
    add ax, bx                  ; AX = column x
    mov cx, [tm_cy]             ; CX = content top (scratch)

    cmp si, TM_GH               ; white: rows 15..54-v (skip when v=40)
    jae .black
    TM_INK CWHITE
    mov bx, cx
    add bx, TM_GF_Y1 + 1
    mov dx, cx
    add dx, TM_GF_Y2 - 1
    sub dx, si
    call OSAPI_GFX_VLINE
.black:
    or si, si                   ; black: rows 55-v..54 (skip when v=0)
    jz .done
    TM_INK CBLACK
    mov bx, cx
    add bx, TM_GF_Y2
    sub bx, si
    mov dx, cx
    add dx, TM_GF_Y2 - 1
    call OSAPI_GFX_VLINE
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_gapcol - draw one all-white column (the sweep gap at tm_pos)
; in:  BX = column index 0..159
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_gapcol:
    push ax
    push bx
    push dx

    mov ax, [tm_cx]
    add ax, 7
    add ax, bx
    mov bx, [tm_cy]
    mov dx, bx
    add bx, TM_GF_Y1 + 1
    add dx, TM_GF_Y2 - 1
    TM_INK CWHITE
    call OSAPI_GFX_VLINE

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_txt_cpu - the 20-char "CPU nnn% SCH xxxxxxx" line (SPEC.md 28): chars
;              0..7 the load, 8 a space, 9..19 the read-only scheduler-mode
;              field. A mode flip from the Control Panel (SPEC.md 31) shows up
;              within one sample period.
; in:  [tm_load]; live mode from sched_mode_get (SPEC.md 8.2)
; out: nothing
; clobbers: nothing (flags only)
;
; Composed and then drawn THROUGH tm_row_draw, as the virtual row TMR_CPU -
; so it is checked and erased a chunk at a time, exactly like a list row. It
; used to be the one unconditional draw in the window, twenty glyphs and a
; full-width erase every interval whether or not anything had moved; and even
; checked whole it would have redrawn all twenty for a percentage that ticks
; over almost every time. Chunked, a load that moves costs the five or ten
; characters it moved and leaves 'SCH preempt' alone.
; -----------------------------------------------------------------------------
tm_txt_cpu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, tm_str
    mov si, tm_s_cpu
    call tm_copy
    mov ax, [tm_load]
    call tm_put3
    mov byte [di], '%'
    inc di
    mov byte [di], ' '
    inc di
    mov si, tm_s_pre            ; chars 9..19: the scheduler mode, as the
    mov al, [tm_coop]           ; last sample found it: 0 pre-emptive, 1
    or al, al                   ; cooperative
    jz .mode
    mov si, tm_s_coop
.mode:
    call tm_copy                ; 11 chars either way, space-padded
    mov byte [di], 0

    mov ax, [tm_cx]             ; a caption sits in column 0 whatever the row
    mov [tm_rowx], ax           ; loops have left [tm_rowx] at, and letters
    add ax, TM_PEN              ; from the margin
    mov [tm_penx], ax
    mov ax, [tm_cy]
    add ax, TM_CPU_Y
    mov [tm_rowy], ax
    mov bx, TMR_CPU
    call tm_row_draw

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_txt_ram - the RAM readout line (tm_txt_ram_y draws it at any y - the
;              memory view puts it at the top, SPEC.md 28)
; in:  [tm_usedk], [tm_totkb]; tm_txt_ram_y: AX = content-relative y
; out: nothing
; clobbers: nothing (flags only)
;
; **In the memory view it carries the HEAP figures too**, in ONE string and
; one font_str, with the claim swatch drawn into the two-space gap between
; the pairs. Two draws on one line would need two check words and a fill that
; belongs to neither; as one string it is one key, and the key is exact.
;
; The heap has no map of its own and never will - a claim is drawn in the
; conventional map at its real address - so its figures belong to that map's
; caption, which is this line.
; -----------------------------------------------------------------------------
tm_txt_ram:
    push ax
    mov ax, TM_RAM_Y
    call tm_txt_ram_y
    pop ax
    ret

tm_txt_ram_y:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [tm_liny], ax
    mov di, tm_str
    mov si, tm_s_ram
    call tm_copy
    mov ax, [tm_usedk]
    mov bx, [tm_totkb]
    call tm_kpair               ; 'nnn/nnnK' - one K, not two, because the
                                ; memory view fits a second pair on this line
                                ; and eight characters is what it costs
    cmp byte [tm_view], 0
    je .built
    mov si, tm_s_heap           ; ...whose two leading spaces are where the
    call tm_copy                ; claim swatch goes
    mov ax, [tm_kb+SK_CLAIM]    ; AX = claimed KB, BX = heap size KB
    mov bx, [tm_kb+SK_HEAP]
    call tm_kpair
.built:
    call tm_rowsum              ; unchanged since the last refresh? then the
    mov bx, tm_elck + 2*TMC_LINE            ; pixels on screen are already right
    call tm_elchk
    jc .out

    mov ax, [tm_liny]
    call tm_lfill               ; only NOW is the line worth erasing
    jnc .draw                   ; ...unless a clip edge crosses it, in which
    mov word [bx], 0            ; case nothing was drawn and the key tm_elchk
    jmp short .out              ; just recorded is a lie that would keep this
.draw:                          ; line stale until its content moved again
    TM_INK CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_cy]
    add dx, [tm_liny]
    mov si, tm_str
    call OSAPI_FONT_STR

    cmp byte [tm_view], 0
    je .out
    mov ax, [tm_cy]             ; ...and the claim legend square, last, over
    add ax, [tm_liny]           ; the gap the string just lettered white
    mov [tm_rowy], ax
    mov word [tm_sqox], TMM_HSQ_X
    mov si, tm_pat_clm
    call tm_sq_pat
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_bar - the RAM bar interior: black for barw px from the left, white rest
; in:  [tm_barw] = 0..TM_GW; redrawn only when it changed (TMC_BAR)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_bar:
    push ax
    push bx
    push cx
    push dx
    push si

    mov ax, [tm_barw]           ; the bar IS its width: same width, same two
    mov bx, tm_elck + 2*TMC_BAR             ; rectangles, same pixels
    call tm_elchk
    jc .done

    mov si, [tm_barw]
    or si, si
    jz .rest
    TM_INK CBLACK
    mov ax, [tm_cx]
    add ax, 7
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1 + 1
    mov cx, ax
    add cx, si
    dec cx                      ; x2 = 7 + barw - 1
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2 - 1
    call OSAPI_GFX_FILL
.rest:
    cmp si, TM_GW
    jae .done
    TM_INK CWHITE
    mov ax, [tm_cx]
    add ax, 7
    add ax, si                  ; x1 = 7 + barw
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1 + 1
    mov cx, [tm_cx]
    add cx, TM_RW - 1
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2 - 1
    call OSAPI_GFX_FILL
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_xbar - the XMS bar interior: black for [tm_xbarw] px, white for the rest
; in:  [tm_xbarw] = 0..TM_GW; redrawn only when it CHANGED (TMC_XBAR)
; out: nothing
; clobbers: nothing (flags only)
;
; tm_bar's twin, for the pool above 1MB (SPEC.md 41.6). Same element-check
; discipline: the bar IS its width, so the same width means the same two
; rectangles and the refresh draws nothing at all - which on a desktop that
; is merely sitting there is every refresh, twice a second, forever.
;
; On tier 0 the width is 0 and the bar stands empty beside the 'XMS 0/ 0K'
; figures. That is deliberate - an absent bar would read as a missing feature
; rather than an empty pool - and it is the reason the FRAME is drawn by
; tm_draw_mem and not here, exactly as the perf view draws the RAM bar's. A
; zero width matches the cleared check word, so this body skips on the very
; first paint, and a frame drawn inside the gate would never appear at all.
; -----------------------------------------------------------------------------
tm_xbar:
    push ax
    push bx
    push cx
    push dx
    push si

    mov ax, [tm_xbarw]
    mov bx, tm_elck + 2*TMC_XBAR
    call tm_elchk
    jc .done

    mov si, [tm_xbarw]
    or si, si
    jz .rest
    TM_INK CBLACK
    mov ax, [tm_cx]
    add ax, 7
    mov bx, [tm_cy]
    add bx, TMM_XB_Y1 + 1
    mov cx, ax
    add cx, si
    dec cx
    mov dx, [tm_cy]
    add dx, TMM_XB_Y2 - 1
    call OSAPI_GFX_FILL
.rest:
    cmp si, TM_GW
    jae .done
    TM_INK CWHITE
    mov ax, [tm_cx]
    add ax, 7
    add ax, si
    mov bx, [tm_cy]
    add bx, TMM_XB_Y1 + 1
    mov cx, [tm_cx]
    add cx, TM_RW - 1
    mov dx, [tm_cy]
    add dx, TMM_XB_Y2 - 1
    call OSAPI_GFX_FILL
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_order - the performance view's row order: built-ins first, then packages,
;            then the free slots (SPEC.md 28)
; in:  the tm_ist/tm_iknd snapshot
; out: tm_ord[0..INST_MAX-1] = instance indices, grouped
; clobbers: nothing (flags)
;
; This is the one place the Task Manager stops mirroring the instance table
; slot for slot. Grouping is what makes the list readable: the built-ins the
; user cannot close sit together under System, everything they launched sits
; below, and the free slots fall to the bottom instead of punching holes
; through the middle. The cost is that a row can move when an instance dies -
; the dock is what keeps the stable slot<->tile mapping (SPEC.md 30), and this
; list never used it for anything, it only inherited it.
; -----------------------------------------------------------------------------
tm_order:
    push ax
    push bx
    push cx
    push si
    xor bx, bx                  ; BX = write cursor into tm_ord
    xor cx, cx                  ; CX = the pass: 0 built-ins, 1 packages, 2 free
.pass:
    xor si, si
.scan:
    mov al, [tm_ist+si]
    or al, al
    jz .isfree
    mov al, [tm_iknd+si]
    and al, KIND_PKG
    jz .isbuilt
    mov al, 1
    jmp short .cls
.isbuilt:
    xor al, al
    jmp short .cls
.isfree:
    mov al, 2
.cls:
    cmp al, cl
    jne .next
    mov ax, si
    mov [tm_ord+bx], al
    inc bx
.next:
    inc si
    cmp si, INST_MAX
    jb .scan
    inc cx
    cmp cx, 3
    jb .pass
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rows - the TM_ROWS process rows: white-fill each band, build the 20-char
;           row string, draw it (SPEC.md 28 pins the format)
; in:  tm_self/tm_state/tm_pct + the instance snapshot block
; out: nothing
; clobbers: nothing (flags only)
;
; Row 0 is "System"; rows 1.. walk tm_order's grouping, so the built-ins are
; indented directly under it and the packages follow. The share in tm_pct is
; still indexed by INSTANCE (row = instance + 1 when it was built), which is
; why every read of it goes through [tm_rowi] rather than the loop counter.
; -----------------------------------------------------------------------------
tm_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call tm_order               ; group the rows before laying any of them out
    xor si, si                  ; SI = row index
.row:
    mov ax, si
    mov bx, TM_ROW_Y
    call tm_row_place           ; column-major, so a refused row ends the list
    jc .rdone

    ; --- build "NAME     ST  nnn%mmmK" in tm_str -----------------------------
    ; The band is NOT erased yet: most of these rows are the same as they
    ; were half a second ago - a free slot always is - and tm_rowsum below
    ; decides whether any of it is worth drawing.
    mov di, tm_str
    or si, si
    jnz .inst

    ; row 0: the kernel - task 0 minus everything debited to an instance
    push si
    mov si, tm_s_sys
    xor al, al                  ; the one row that is never indented
    call tm_namef
    mov si, tm_s_rdy            ; task 0 never sleeps; it shows "run" only
    cmp byte [tm_self], 0       ; when it happened to be current at snapshot
    jne .sysst
    mov si, tm_s_run
.sysst:
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di
    mov bx, si
    mov al, [tm_pct+bx]
    call tm_cpucol
    mov ax, [tm_kb+SK_KERN]     ; the kernel's footprint, whole (SPEC.md 2)
                                ; - the same term the RAM line above uses, so
                                ; the rows still sum to the bar
    add ax, [tm_kb+SK_KCLAIM]   ; + the kernel's OWN heap claims (SPEC.md
                                ; 42.2): the save-under and the back buffer.
                                ; A package's claim belongs to its own row.
    call tm_memcol_kb
    jmp .draw

.inst:
    mov bx, si
    dec bx
    mov bl, [tm_ord+bx]         ; BX = the instance this ROW shows, held to
    xor bh, bh                  ; the end; [tm_rowi] is the same thing for the
    mov [tm_rowi], bl           ; stretches where BX is needed for something
    cmp byte [tm_ist+bx], 0
    je .free

    push si                     ; name: the I_NAME snapshot, 7 chars
    mov ax, bx
    mov cl, 4
    shl ax, cl
    mov si, ax
    add si, tm_inm
    mov al, [tm_iknd+bx]        ; a built-in sits indented under System, a
    not al                      ; package at the top level with it
    and al, KIND_PKG
    call tm_namef
    pop si

    push si                     ; state (SPEC.md 28)
    cmp byte [tm_ist+bx], 2
    je .stdie
    mov cl, [tm_itsk+bx]
    cmp cl, MAX_TASKS           ; 0xFF = no worker: lives in callbacks only
    jae .stevt
    cmp cl, [tm_self]
    je .strun
    xor ch, ch
    mov si, cx                  ; SI = the owning task's slot
    cmp byte [tm_state+si], 2
    je .stslp
    mov si, tm_s_rdy
    jmp .stput
.stslp:
    mov si, tm_s_slp
    jmp .stput
.strun:
    mov si, tm_s_run
    jmp .stput
.stevt:
    mov si, tm_s_evt
    jmp .stput
.stdie:
    mov si, tm_s_die
.stput:
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di

    mov bl, [tm_rowi]           ; share, right-aligned in 3, then '%'
    xor bh, bh                  ; (tm_pct is indexed by instance + 1, not by
    inc bx                      ; the display row - see tm_order)
    mov al, [tm_pct+bx]
    call tm_cpucol
    mov bl, [tm_rowi]           ; region bytes (0 for a built-in kind)...
    xor bh, bh
    shl bx, 1
    mov ax, [tm_isz+bx]
    add ax, 1023                ; ...as KB, rounded up; 0 stays 0
    mov cl, 10
    shr ax, cl
    mov dx, ax
    push si
    mov si, bx
    shr si, 1                   ; SI = instance slot
    call tm_inst_claim          ; ...plus everything it holds off the claim
    pop si                      ; heap (SPEC.md 50.5). The MEM column is what
    add ax, dx                  ; this app COSTS, and a package that claimed a
    call tm_memcol_kb0          ; canvas costs far more than its region - the
    jmp .draw                   ; same sum the System row already shows

.free:                          ; free slot: "-        ---   -    -"
    push si
    mov si, tm_s_dash
    xor al, al
    call tm_namef
    mov si, tm_s_free
    call tm_copy
    mov byte [di], ' '
    inc di
    mov si, tm_s_frtl
    call tm_copy
    pop si

.draw:
    mov byte [di], 0
    mov ax, [tm_rowx]           ; this list letters from tm_rowx+TM_PEN - there
    add ax, TM_PEN              ; is no square in front of it
    mov [tm_penx], ax
    push si
    mov bx, si
    call tm_row_draw            ; chunked: only what changed, and only what
    pop si                      ; the region lets us (SPEC.md 11.3/28)

.rnext:
    inc si
    cmp si, TM_ROWS
    jb .row
.rdone:

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_cpucol - the CPU column: share right-aligned in 3, then '%', then the
;             gap MEM needs after it - TM_CPUC characters, not four
; in:  AL = share 0..100, DI = dest
; out: DI advanced by TM_CPUC
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_cpucol:
    push ax
    xor ah, ah
    call tm_put3
    mov byte [di], '%'
    inc di
    mov byte [di], ' '          ; the gap MEM needs: 100% against 335K with
    inc di                      ; nothing between reads as one number
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_memcol - the MEM column: KB right-aligned in 3, then 'K'; an instance
;             that owns no region of its own (every built-in kind) shows
;             "   -" instead of a misleading 0 (SPEC.md 28)
; in:  AX = bytes, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_memcol:
    push ax
    push cx
    or ax, ax
    jz .none
    add ax, 1023                ; round up; AX <= 0xB000 here, no overflow
    mov cl, 10
    shr ax, cl
.kb:
    call tm_put3
    mov byte [di], 'K'
    inc di
    jmp .out
.none:
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], '-'
    inc di
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_memcol_kb - tm_memcol's tail for a caller that already has KB in AX
;                (nonzero) - the System row adds the 150K back buffer,
;                which cannot be expressed as a 16-bit byte count
; in:  AX = KB (> 0), DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_memcol_kb:
    push ax
    push cx
    jmp tm_memcol.kb

; -----------------------------------------------------------------------------
; tm_memcol_kb0 - the same, but 0 KB still means "owns nothing" and prints "-"
; in:  AX = KB, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_memcol_kb0:
    push ax
    push cx
    or ax, ax
    jnz tm_memcol.kb
    jmp tm_memcol.none

; -----------------------------------------------------------------------------
; tm_namef - the performance view's NAME field: an optional one-space indent,
;            the name in TM_NAMEC, then padding out to exactly TM_NAMEF
; in:  SI = name, AL = 0 top level / non-zero indented, DI = dest
; out: DI advanced by TM_NAMEF
; clobbers: nothing else (internal helper)
;
; TM_NAMEC + 2 rather than + 1 because the indent has to come from somewhere
; and the alternative was taking it out of the name - which truncated the
; longest one in the tree by a character and left every other row's separator
; doing double duty.
; -----------------------------------------------------------------------------
tm_namef:
    push ax
    push cx
    mov cl, al
    or al, al
    jz .noind
    mov byte [di], ' '
    inc di
.noind:
    push cx
    mov cx, TM_NAMEC
    call tm_copyn
    pop cx
    mov byte [di], ' '
    inc di
    or cl, cl
    jnz .out
    mov byte [di], ' '          ; not indented: the column the indent would
    inc di                      ; have taken is padding instead
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_copy - copy a NUL string (without the NUL) to DI
; in:  SI = string, DI = dest
; out: DI advanced past the copied chars
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_copy:
    push ax
    push si
.c:
    lodsb                       ; DF=0 per SPEC.md 1
    or al, al
    jz .done
    mov [di], al
    inc di
    jmp .c
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_copy7 - copy a NUL string into exactly 7 chars at DI, space-padded,
;            truncated at 7
; in:  SI = string, DI = dest
; out: DI advanced by 7
; clobbers: nothing else (internal helper)
;
; The MEMORY view's name width, and still 7: its row is already the full
; chunk span and it letters from the wider TM_MPEN, so unlike the performance
; view there is no slack in the band to widen into.
; -----------------------------------------------------------------------------
tm_copy7:
    push cx
    mov cx, 7
    call tm_copyn
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_copyn - copy a NUL string into exactly CX chars at DI, space-padded,
;            truncated at CX
; in:  SI = string, CX = width, DI = dest
; out: DI advanced by CX
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_copyn:
    push ax
    push cx
    push si
.c:
    lodsb
    or al, al
    jz .pad
    mov [di], al
    inc di
    loop .c
    jmp .done
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.done:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_kpair - 'nnn/nnnK' at DI, NUL-terminated
; in:  AX = used, BX = total, DI = dest
; out: DI left ON the NUL, so a row builder's own terminator lands on it
; clobbers: nothing else
;
; ONE 'K', at the end. Both figures are KB and the memory view has to fit two
; of these pairs plus their labels and a swatch on one line - eight characters
; each is what makes that come out level with the map frames below.
; -----------------------------------------------------------------------------
tm_kpair:
    push ax
    call tm_put3
    mov byte [di], '/'
    inc di
    mov ax, bx
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_put5 - decimal 0..65535 as 5 chars at DI, right-aligned, space-padded
;
; tm_put3's shape does not extend: it emits one digit per hard-coded place,
; and extended memory needs five of them - int 15h AH=88h answers up to
; 65,535 KB and tm_put3 emits a non-digit above 999. This fills the field
; with spaces and writes digits leftward from the ones column instead, so
; the padding falls out of the division rather than out of a chain of
; special cases.
;
; in:  AX = value, DI = dest
; out: DI advanced by 5
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_put5:
    push ax
    push bx
    push cx
    push dx
    push di

    mov bx, di
    mov cx, 5
.pad:
    mov byte [bx], ' '
    inc bx
    loop .pad
    dec bx                      ; BX = the ones column

    mov cx, 10
.dig:
    xor dx, dx
    div cx                      ; AX = quotient, DX = this digit
    add dl, '0'
    mov [bx], dl
    dec bx
    or ax, ax
    jnz .dig                    ; at most 5 passes: AX <= 65535

    pop di
    add di, 5
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_put3 - decimal 0..999 as 3 chars at DI, right-aligned, space-padded
; in:  AX = value, DI = dest
; out: DI advanced by 3
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_put3:
    push ax
    push bx
    push cx
    push dx

    mov bx, 100
    xor dx, dx
    div bx                      ; AX = hundreds, DX = 0..99
    mov cx, dx
    mov bl, ' '                 ; BL = ' ' until a digit is emitted
    or ax, ax
    jz .hpad
    add al, '0'
    mov [di], al
    mov bl, '0'
    jmp .hdone
.hpad:
    mov byte [di], ' '
.hdone:
    inc di

    mov ax, cx
    mov cx, 10
    xor dx, dx
    div cx                      ; AX = tens, DX = ones
    or ax, ax
    jnz .tdig
    cmp bl, '0'
    je .tdig                    ; hundreds printed: this zero counts
    mov byte [di], ' '
    jmp .tdone
.tdig:
    add al, '0'
    mov [di], al
.tdone:
    inc di

    mov ax, dx
    add al, '0'
    mov [di], al
    inc di

    pop dx
    pop cx
    pop bx
    pop ax
    ret

    OS88_BSS TM_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
; All zero is the performance view, an empty history ring and no worker, which
; is exactly what a fresh launch wants. As a built-in this block had to be
; cleared by hand at every kinit, because .bss survives between instances of a
; kind; a package gets a fresh image and a zeroed bss per launch instead.

tm_win      equ os88_image_end + 0     ; word: our window ptr

; --- what the API cells fill (SPEC.md 20.9) ----------------------------------
; Three buffers this module owns and the kernel writes through ES:DI.
; tm_snapshot is unpacked into the per-slot arrays below the moment it lands (see
; tm_sample); tm_claims and tm_kb are read where they sit.
tm_snapshot     equ os88_image_end + 2     ; osapi_sys_snapshot: scheduler + instance table
tm_claims   equ os88_image_end + 428   ; osapi_claim_snapshot: the claim map
tm_kb       equ os88_image_end + 620   ; osapi_sys_kb: the kernel's footprint and the
                                ; heap's totals, refreshed by tm_view_begin
                                ; and by every sample

tm_hist     equ os88_image_end + 642   ; history ring: column heights 0..40

; The three buffer slices above are hand-chained literals; these pin them to
; the SDK's derived sizes, so a kernel table growing (MAX_TASKS, INST_MAX,
; MEM_MAX, a record gaining a field) fails THIS assembly instead of letting
; osapi_sys_snapshot write through tm_claims/tm_kb/tm_hist with no diagnostic.
%if (tm_claims - tm_snapshot) != SYS_SNAPSHOT_SIZE
  %error "tm_snapshot's slice != SYS_SNAPSHOT_SIZE - re-derive the bss map below"
%endif
%if (tm_kb - tm_claims) != CLAIM_SNAPSHOT_SIZE
  %error "tm_claims' slice != CLAIM_SNAPSHOT_SIZE - re-derive the bss map below"
%endif
%if (tm_hist - tm_kb) != SYSKB_SIZE
  %error "tm_kb's slice != SYSKB_SIZE - re-derive the bss map below"
%endif
tm_pos      equ os88_image_end + 867   ; sweep index = next column to write
tm_lastcol  equ os88_image_end + 869   ; column written by the latest sample
tm_cnt      equ os88_image_end + 871   ; interval spin count, dword
tm_cmax     equ os88_image_end + 875   ; calibration max, current epoch, dword
tm_pmax     equ os88_image_end + 879   ; calibration max, previous epoch, dword
tm_epc      equ os88_image_end + 883   ; samples into the current epoch
tm_t0       equ os88_image_end + 884   ; interval start tick
tm_load     equ os88_image_end + 886   ; latest load, 0..100
tm_told     equ os88_image_end + 888   ; previous sch_cycles snapshot
tm_tnew     equ os88_image_end + 936   ; current snapshot
tm_tdif     equ os88_image_end + 984   ; per-task interval cycles
tm_state    equ os88_image_end + 1032  ; T_STATE snapshot
tm_pstate   equ os88_image_end + 1044  ; previous sample's T_STATE (appeared rule)
tm_self     equ os88_image_end + 1056  ; our own slot ([sch_cur] at snapshot)
tm_iold     equ os88_image_end + 1057  ; previous I_CYC snapshot
tm_inew     equ os88_image_end + 1105  ; current snapshot
tm_idif     equ os88_image_end + 1153  ; per-instance interval callback cycles
tm_ist      equ os88_image_end + 1201  ; I_STATE snapshot
tm_pist     equ os88_image_end + 1213  ; previous sample's I_STATE (appeared rule)
tm_itsk     equ os88_image_end + 1225  ; I_TASK snapshot (owning task or 0xFF)
tm_isz      equ os88_image_end + 1237  ; I_SIZE snapshot, bytes
tm_ispt     equ os88_image_end + 1261  ; I_SPTR snapshot (memory view, SPEC.md 28)
tm_iknd     equ os88_image_end + 1285  ; I_KIND snapshot: the Builtins/Packages split
                                ; (SPEC.md 28) needs bit 7, and the CLAIM
                                ; column needs to know which owner word an
                                ; instance's claims carry (SPEC.md 50.2)
tm_ikb      equ os88_image_end + 1297  ; ...and the KB it holds off the heap, which
                                ; the kernel derives from that same rule
tm_coop     equ os88_image_end + 1321  ; the scheduler mode at the last sample
tm_mrow     equ os88_image_end + 1322  ; the memory view's row cursor
tm_mfit     equ os88_image_end + 1324  ; ...and whether the row it names is on screen
tm_sqox     equ os88_image_end + 1325  ; ...and where its legend square starts
tm_sqp      equ os88_image_end + 1327  ; ...and its texture, or 0 for no square. A
                                ; REQUEST, not a draw: tm_mrow_close erases
                                ; the band before it letters it, so a square
                                ; drawn while the row was being composed
                                ; would be erased again
tm_elck     equ os88_image_end + 1329  ; the last drawn state of the non-row
                                ; elements, one word each (TMC_*)
tm_rowx     equ os88_image_end + 1341  ; the left edge of the row being drawn: tm_cx
                                ; for column 0, one TM_COLW on for column 1
tm_xbarw    equ os88_image_end + 1343  ; the XMS bar's black run, px (TMC_XBAR)
tm_penx     equ os88_image_end + 1345  ; where the row's text starts: the process
                                ; list letters from tm_rowx+6 and the memory
                                ; list from tm_rowx+16, and tm_row_draw has to
                                ; be told which - drawing a process row at the
                                ; memory list's pen puts it 10px right of the
                                ; text it is replacing
tm_ci       equ os88_image_end + 1347  ; tm_row_draw's chunk index (BP is the task's
                                ; instance record and cannot be borrowed)
tm_ckb      equ os88_image_end + 1349  ; ...and the running check-word pointer, for
                                ; the same reason
tm_rowck    equ os88_image_end + 1351  ; the last drawn content of each row, a
                                ; word per CHUNK (tm_chunksum), plus one
                                ; virtual row for the CPU caption (TMR_CPU).
                                ; Shared by both views - TM_ROWS is the
                                ; smaller of the two and a view switch clears
                                ; it either way. tm_rowck_clear names this
                                ; and tm_elck separately; they used to be
                                ; cleared as one span on the strength of
                                ; being declared adjacent, which they have
                                ; not been for some time
tm_view     equ os88_image_end + 1551  ; 0 = performance view, 1 = memory view
tm_inm      equ os88_image_end + 1552  ; I_NAME snapshots
tm_rcyc     equ os88_image_end + 1744  ; per-ROW cycles: task + instance, dword
tm_total    equ os88_image_end + 1796  ; sum of the rows, dword
tm_pct      equ os88_image_end + 1800  ; per-row share, 0..100
tm_usedk    equ os88_image_end + 1813  ; used RAM, KB
tm_barw     equ os88_image_end + 1815  ; RAM bar black width, 0..TM_GW

; --- worked out once by tm_init, before the window exists --------------------
; These used to need saying out loud, because a built-in's .bss survives
; between instances of its kind and tm_kinit therefore cleared the block above
; by hand - leaving anything tm_init had computed at BOOT to be wiped by the
; first launch. It cost a divide by zero the first time the window was opened,
; tm_colrows being a divisor. A package has no such hazard: the bss is zeroed
; once per launch, before the entry proc runs, and tm_init runs inside it.
tm_cols     equ os88_image_end + 1817  ; process-list columns, 1 or 2 (SPEC.md 28.1)
tm_colrows  equ os88_image_end + 1819  ; rows in COLUMN 0, which starts under the maps
tm_col2rows equ os88_image_end + 1821  ; rows in each LATER column, which starts at the
                                ; top and so holds more - A DIVISOR, never 0
tm_maxrow   equ os88_image_end + 1823  ; rows this SCREEN shows, <= TMM_ROWS: derived
                                ; from [vid_dock_y0] so the window never
                                ; overlaps the dock (SPEC.md 28.1)
tm_totkb    equ os88_image_end + 1825  ; total conventional RAM (int 12h, boot)
tm_vw       equ os88_image_end + 1827  ; the live screen width and the dock strip's
tm_vdock    equ os88_image_end + 1829  ; top row (osapi_video, SPEC.md 39.2), banked
                                ; at boot: the template's whole geometry is
                                ; derived from them
tm_cx       equ os88_image_end + 1831  ; cached content origin (draw paths only,
tm_cy       equ os88_image_end + 1833  ; always under the gfx lock)
tm_ord      equ os88_image_end + 1835  ; tm_order: display row - 1 -> instance slot
tm_rowi     equ os88_image_end + 1847  ; ...the one tm_rows is drawing right now
tm_rowy     equ os88_image_end + 1848  ; tm_rows/tm_rows_mem scratch: current row y
tm_ylim     equ os88_image_end + 1850  ; ...and the lowest row top the frame holds
                                ; (tm_view_begin): the row lists are fixed-pitch
                                ; and nothing clips a draw to a window
tm_liny     equ os88_image_end + 1852  ; tm_txt_ram_y scratch: the line's y
tm_str      equ os88_image_end + 1854  ; line-format scratch

tm_spawned  equ os88_image_end + 1882  ; byte: 1 = this instance owns its
                                       ; worker. Latches on SUCCESS only, so
                                       ; every paint retries until a task slot
                                       ; frees up (SPEC.md 20.6)

TM_BSS_TOTAL equ 1883
