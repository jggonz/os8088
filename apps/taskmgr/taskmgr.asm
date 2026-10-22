; =============================================================================
; os8088 - Task Manager (SPEC.md 28)
;
; A live CPU load gauge with an oscilloscope-sweep history graph, a RAM
; readout, and the process list with per-instance CPU shares and memory from
; the PIT cycle accounting (SPEC.md 8.1). A content click toggles a second,
; memory page (SPEC.md 28.4): the RAM readout, a conventional-memory map, and
; ONE list of collapsible groups - an owner a row, each with its total, a
; click opening it into the claims it holds - in two views, PROGRAMS (every
; package billed, System one row) and SYSTEM (the kernel itemised, the
; programs one row). It was two pages, a memory view and a heap page, until
; KERN-MEGA-PLAN 14.70.2 merged them.
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
; tm_worker is the monitor task: it SLEEPS its 9-tick interval and then takes
; one sample (SPEC.md 28.7). The load gauge is 100 - 100*idle/total off the
; idle task's own cycles (SPEC.md 8.1.2), which is the same interval and the
; same total the rows below are shares of - so the gauge and the process list
; cannot disagree.
;
; IT USED TO SPIN, and the spin WAS the instrument: { count += 1; yield } for
; the interval, calibrated against a rolling maximum over two epochs, because
; a spin count has no absolute scale. That is why it could not simply be
; deleted - and it cost 34-38% of the machine for as long as this page was
; open, which the page charged to itself, correctly, while the graph beside it
; drew 0-2%. Both numbers were right and they were measuring different things:
; one CPU time, the other how much spinning got done.
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

; -----------------------------------------------------------------------------
; APP_SMALL - THE SMALL-APPS BUILD OF THIS PACKAGE (SPEC.md 28.12)
;
; `make smallapps` passes -DAPP_SMALL and the pages below compile out; the
; ordinary build defines both and is byte-for-byte what it was. Note Pad
; (SPEC.md 27.16) is the pattern and its rules hold unchanged - one package
; built twice, never a second ABI, and the small arm runs on kern_big too.
;
;   TMF_MEM    the memory page (SPEC.md 28): the conventional-memory map, the
;              XMS bar and the 640K-1MB rows
;   TMF_HEAP   that page's LIST (SPEC.md 28.4): every claim record classified
;              to the owner that holds it, the collapsible groups, the
;              TYPE/TIER decode and the scroll bar. It was a page of its own
;              until KERN-MEGA-PLAN 14.70.2 and is still its own flag, because
;              the two halves still compile out together and the guard below
;              is what says so
;
; **AT THE TOP** because these are PREPROCESSOR tests answered in file order
; and the blocks they guard start ~400 lines below (apps/paint/paint.asm and
; apps/notepad/notepad.asm both carry the same note for the same trap).
;
; THE LADDER IS ORDERED AND THE GUARD SAYS SO. The heap page BORROWS the
; memory view's row machinery whole - tm_mrow_open / tm_mrow_close /
; tm_row_draw / tm_row_place (SPEC.md 28.4) - so "heap without memory" is not
; a configuration this file has, and asking for it is a build error rather
; than a silently different product.
%ifndef APP_SMALL
%define TMF_MEM
%define TMF_HEAP
%endif
%ifdef TMF_HEAP
  %ifndef TMF_MEM
    %error "taskmgr: the heap page borrows the memory view's row machinery"
  %endif
%endif

; ...and how many pages the content click cycles through (SPEC.md 28.4). One
; number, derived, because tm_click's wrap and nothing else decides where the
; cycle ends - a literal here is a page that can be selected and not drawn.
; TWO since KERN-MEGA-PLAN 14.70.2: the memory view and the heap page are one
; page now, and the heap page's list is its list.
%ifdef TMF_MEM
TM_NVIEW    equ 2
%else
TM_NVIEW    equ 1
%endif

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
    OS88_HEADER 'TaskMgr', tm_entry, 1, OS88_STACK_256 | OS88_HIGH | OS88_HIGHMEM
                                ; THE WORKER'S STACK, declared rather than
                                ; defaulted (SPEC.md 8.7). It said 192 on a
                                ; static 56, and 56 WAS THE WRONG BRANCH:
                                ; tm_update reaches the performance list by
                                ; falling through and the other two pages by
                                ; `je tm_upd_mem` / `ja tm_upd_heap`, and
                                ; tools/stkdepth.py followed `call` edges
                                ; only - so the deepest thing this worker
                                ; does was invisible to the measurement that
                                ; sized the slice (SPEC.md 8.7.4).
                                ;
                                ; The heap page walks tm_upd_heap ->
                                ; tm_rows_heap -> tm_hgrp -> tm_hhead ->
                                ; tm_mrow_nolast -> tm_mrow_close ->
                                ; tm_row_draw -> tm_row_lead, which is
                                ; **96** bytes; 96 + SPEC.md 8.7's 64-byte
                                ; floor is 160, and 256 gives the 1.60x the
                                ; line above meant to be buying. MEASURED
                                ; with tools/stkwater.py on the field's own
                                ; recipe - this page open while PAINT holds
                                ; MEDIA/OS8088.GIF - the slice reads **180**,
                                ; and it reads 180 given a 384 slice too, so
                                ; that is the ceiling and not a clamp. 180
                                ; was 94% of 192.
    OS88_ICON16
    dw 0x0000, 0x7000, 0x701E, 0x701E, 0x71FE, 0x71FE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x0000
    dw 0x0000, 0x0000, 0x2000, 0x200C, 0x200C, 0x20CC, 0x20CC, 0x2CCC
    dw 0x2CCC, 0x2CCC, 0x2CCC, 0x3FFC, 0x0000, 0x0000, 0x0000, 0x0000
    OS88_ICON16_END

; --- geometry / cadence (SPEC.md 28) -----------------------------------------
TM_INT      equ 9               ; sample interval, ticks (~0.5s)
; ...and how many of those intervals a PAINT of the memory view or the heap
; page waits for (SPEC.md 28.6). 4 x TM_INT is 36 ticks, 1.98 s.
;
; IT IS THE PAINT THAT SLOWS AND NOT THE SAMPLE, which is the whole of the
; design: tm_sample takes the cycle diffs over exactly TM_INT ticks and pushes
; one history column per interval, so an interval that changed with the page
; would change what the performance view MEANS - a graph whose columns are 2 s
; wide on one page and 0.5 s on another. So the worker samples at the rate it
; always did and only DRAWS at this one.
;
; Measured on a 5150/CGA with tools/os88lockw.py, one quiet refresh, nothing
; changed on screen: the heap page held the gfx lock for 51.2 ms and the
; memory view for 45.1 ms, and BOTH DREW NOTHING AT ALL - no font_run, no
; gfx_fill, no gfx_hline, no gfx_frame. The cost is not drawing, it is the
; snapshot and the compose-and-hash of every row to conclude that nothing
; moved, and the pointer is frozen for all of it (SPEC.md 7.1.4). At TM_INT
; that was 102 ms of every second on the heap page; at TM_SLOW it is 26.
TM_SLOW     equ 4               ; intervals per paint on views 1 and 2
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
TM_ROWS     equ INST_MAX + 1    ; "System" plus one row per instance slot -
                                ; the per-row DATA (tm_rcyc, tm_pct)...
TM_PROWS    equ TM_ROWS + 1     ; ...and the rows DRAWN, which have IVT+BIOS
                                ; above System (SPEC.md 28.4.7)
TM_LOWKB    equ 2               ; IVT+BIOS: the 1.5K under KERNEL_SEG and the
                                ; half-KB the whole-KB heap and System leave
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
TMM_HSQ_X   equ 108             ; the claim swatch, centred in the two-space
                                ; gap between the RAM and HEAP figure pairs.
                                ; It was 105, when this line alone lettered
                                ; from the band's edge + 6 instead of TM_PEN;
                                ; the line is a tm_lrun now, so the gap is
                                ; [104,119] and an 8px square centred in it
                                ; starts here (SPEC.md 28.5.3)
TMM_HSQ_XPM equ TM_PEN + 16 * 8   ; ...and after 'Ram u.uu/tt.ttM' (15
                                ; characters at most), kern_mega's one pair
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
TMM_AVL_Y   equ 55              ; 'AVAIL nnnK  MAX RUN nnnK' - what a claim
                                ; can still get, and the largest run of it
                                ; (SPEC.md 28.4.1): the heap page's captions
                                ; folded to the one line the merged page has
                                ; room for (KERN-MEGA-PLAN 14.70.2)
TMM_HDR_Y   equ 66              ; the list's header: 'PROGRAMS   ADDR SIZE +ALL'

; --- ...and what all of that becomes on a machine with NO store above 1MB ----
; A bar whose scale is zero is not a reading, it is an empty rectangle: on the
; target machine the XMS row was 'XMS 0/ 0K' over a frame with nothing in it,
; twenty-two pixels of window saying that a thing which cannot exist here does
; not exist. So on a machine whose pool is 0 KB the bar is not drawn, the
; figures come off the caption (the CPU TIER stays - it is the only place in
; the UI that names it, and SPEC.md 60 is why it is a fact worth keeping), and
; everything below moves UP by TMM_XSHIFT.
;
; This is SPEC.md 31.10.1's rule rather than a new one - "one adapter is not a
; choice, so the page is not drawn at all" - and it is deliberately NOT
; SPEC.md 47's greying: greying says "this control is unavailable", and there
; is no control here and nothing unavailable. There is simply no such memory.
;
; TMM_XSHIFT is TM_ROW_H exactly, and that is the whole point rather than a
; coincidence: the space the bar gave back is one process row, so a machine
; that cannot show an XMS bar shows one more line of the list instead. It is
; asserted below, because the two are separately plausible numbers.
TMM_XSHIFT  equ 11              ; px the header, the rows and the frame rise
%if TMM_XSHIFT != TM_ROW_H
%error "TMM_XSHIFT must be one row pitch - the reclaimed space IS a row"
%endif
%if TMM_HDR_Y - TMM_XSHIFT <= TMM_XMS_Y + 8
%error "the no-XMS header would land on the caption line above it"
%endif

; --- what the periodic refresh checks before it draws (SPEC.md 28) -----------
; The rows have tm_rowck; these are the seven things AROUND them that cost
; real pixels and mostly do not move. A map interior is ~3,000 pixels of
; pattern fill; the figures above it change when a claim is made or a package
; loads, which on a desktop that is just sitting there is never. Every one of
; them goes through tm_elchk, rows included, so the never-zero rule there
; covers the lot.
; **THEY ARE COUNTED AND NOT NUMBERED** (SPEC.md 28.12): each name takes the
; running total and bumps it, so a page compiled out takes its check words
; out of tm_elck with it and no literal below has to be renumbered by hand.
; THE ORDER IS THE ONE THE LITERALS HAD, which is what keeps the full build
; byte-for-byte identical - a counter that also tidied the list would have
; renumbered every `mov bx, tm_elck + 2*TMC_*` in the file and broken the one
; gate this flag has (SPEC.md 27.16).
%assign TMC_I 0
TMC_LINE    equ TMC_I           ; the RAM (+ HEAP) readout line
%assign TMC_I TMC_I+1
%ifdef TMF_MEM
TMC_MRAM    equ TMC_I           ; the conventional-memory map interior
%assign TMC_I TMC_I+1
%endif
TMC_BAR     equ TMC_I           ; the performance view's RAM bar
%assign TMC_I TMC_I+1
%ifdef TMF_MEM
TMC_XMS     equ TMC_I           ; the XMS readout line (SPEC.md 41)
%assign TMC_I TMC_I+1
TMC_XBAR    equ TMC_I           ; ...and its bar
%assign TMC_I TMC_I+1
%endif
%ifdef TMF_HEAP
TMC_AVL     equ TMC_I           ; the list's AVAIL / MAX RUN line (SPEC.md
%assign TMC_I TMC_I+1           ; 28.4.1) - the heap page's three captions,
                                ; one line now (KERN-MEGA-PLAN 14.70.2)
TMC_HDR     equ TMC_I           ; ...the column header, whose two ends are
%assign TMC_I TMC_I+1           ; the PROGRAMS/SYSTEM and +ALL/-ALL switches
                                ; and so change when they are clicked
TMC_HSB     equ TMC_I           ; ...and its scroll bar (SPEC.md 28.4.4),
%assign TMC_I TMC_I+1           ; keyed on the whole seven-word block so that
                                ; a window MOVE redraws it as readily as a
                                ; claim does
%endif
%ifdef TMF_MEM
TMC_QUIET   equ TMC_I           ; ...and the WHOLE of what the memory view and
%assign TMC_I TMC_I+1           ; the heap page draw from (SPEC.md 28.6.1),
                                ; which is the one key tested with no lock
                                ; held and before one is taken. IT GOES WITH
                                ; THE PAGES: the performance view moves every
                                ; interval by construction and has never been
                                ; asked this question (tm_quiet)
%endif
TMC_N       equ TMC_I

; The instance fields those two pages draw from, as ONE contiguous span for
; tm_quiet's hash: tm_ist, tm_pist, tm_itsk, tm_isz, tm_ispt, tm_iknd, tm_ikb,
; at 1+1+1+2+2+1+2 bytes a slot. The NAMES are not in it and do not need to
; be: a name changes only when an instance appears or goes, and that moves
; tm_ist. Asserted where those labels exist, at the bottom of the file.
TM_IHASH    equ INST_MAX * 10

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
%ifdef TMF_HEAP
TMM_ROW_Y   equ 78              ; first list row (same TM_ROW_H pitch): one
                                ; line lower than the memory view's 67, the
                                ; AVAIL line above the header taking it
%else
TMM_ROW_Y   equ 67              ; ...and the small build's, which has no list
                                ; and sizes its frame from this (SPEC.md
                                ; 28.12): it stays where it was, byte for byte
%endif
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
; SPEC.md 28.5.1: cells in one caption line, so a padded run covers the band
; tm_rowfill used to erase. Asserted, because "the run is its own erase" is only
; true if it reaches exactly TM_RW.
TM_LCELLS   equ (TM_RW - TM_PEN + 1) / 8
%if TM_LCELLS * 8 != TM_RW - TM_PEN + 1
  %error "TM_RW - TM_PEN + 1 must be a whole number of 8px cells for tm_lrun"
%endif
; ...and tm_lrun pads INTO tm_str, so the buffer has to hold a full band plus
; its NUL. The RAM line is 27 characters and TM_LCELLS is 27, so this is EXACT
; rather than comfortable: it is the padding, not any label, that would be the
; first thing to write past the end of .bss if TM_RW grew.
%if TM_LCELLS + 1 > TM_STRMAX
  %error "tm_str cannot hold a padded tm_lrun band - widen TM_STRMAX"
%endif
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

TMM_ROWS    equ INST_MAX + 7    ; low memory, System, its three buffer rows,
                                ; the two headings, every instance - 19 x
                                ; TM_ROW_H
                                ; from TMM_ROW_Y, which is what decides both
                                ; the template's height and that free slots
                                ; are not drawn at all. tm_mrow_open clamps
                                ; to the LIVE frame on top of this, for the
                                ; screens where wm_fit shrinks the window.
                                ; It was INST_MAX + 7 while `Disk bufs` was a
                                ; row (SPEC.md 20.9), and the window is a row
                                ; shorter for losing it rather than a row
                                ; emptier - TM_PREF_H is cut from this. The
                                ; `IVT+BDA` row took that row back (SPEC.md
                                ; 28): 1.5K of every machine that no other
                                ; figure in this window could account for

; --- THE TWO FRAMES THIS WINDOW WANTS (SPEC.md 11.100.1) ---------------------
; tm_layout has always worked out the frame its column count needs; these are
; that arithmetic as CONSTANTS, so the KERNEL can apply one at the one moment a
; resize is safe. It has to be the kernel: an 11.98 handler may not draw, a
; resize repaints, and a repaint from the WORKER runs every overlapped window's
; W_PAINT on a worker task holding the gfx lock - which hard froze the machine
; the one build that tried it (a Disk window open, then dragged across twice).
;
; A GENEROUS height is the idiom (docs/plans/completed/WINDOW-SIZING-PLAN.md 9): the clamp
; brings it down, and on a CGA it comes back at the band.
;
; THE HEIGHT IS THE ONE A MACHINE WITH A STORE WANTS, and tm_entry SUBTRACTS
; [tm_xoff] from it before publishing: TMM_XSHIFT is added back when there is
; no XMS bar to draw, which is a property of the MACHINE and not of the
; adapter, so a constant is a row wrong on one kind of machine or the other.
; The table is in our own segment and the flag is settled long before the
; window exists, so it is written in rather than guessed at.
TM_PREF_H   equ TMM_ROWS * TM_ROW_H + TITLE_H + 1 + TMM_ROW_Y
TM_PREF_W1  equ TM_W                ; one column: VGA and Hercules
TM_PREF_W2  equ TM_W + TM_COLW      ; two: a CGA's band is 155 and a column is
                                    ; seven rows deep there, so the list runs
                                    ; in two of them and the frame doubles

; --- the memory page's LIST (SPEC.md 28.4) -----------------------------------
; Every claim record and every block above 1MB, classified to the owner that
; holds it, under one row per owner with that owner's total - a GROUP, which a
; click opens into its claims (KERN-MEGA-PLAN 14.70.2). It was the heap page,
; a third page with three caption lines of its own; merged into the memory view
; it keeps the list, the TYPE/TIER decode and the scroll bar, and its three
; captions are one line (TMM_AVL_Y) because HELD is the summary line's heap
; half now and PURGE is a group of its own.
;
; The claim row, in characters: two of indent, the type, A SEPARATOR, the
; segment, the KB column tm_kcol emits, a gap and the purge tier. Stated here
; because the header string has to land on the same columns and nothing else
; checks it. The separator is load-bearing rather than spacing - three of the
; type names use all eight columns, and without it 'Shortcut' and its segment
; read as 'Shortcut2000' (the memory list's 'ARKANOI9E40', SPEC.md 28).
;
; EIGHT, because tm_ktab's names are eight and three of them use it all:
; at seven 'Compress', 'Shortcut' and 'Disk img' showed as 'Compres',
; 'Shortcu' and 'Disk im' (SPEC.md 28.12.2). 2+8+1+4+5+1+4 is 25, the whole
; chunk span, so this row now has NO spare column and the guard below is
; exact: the 25th cell ends at TM_PEN + 200 - 1 = 207, two pixels clear of
; the scroll bar at TMH_SB_X1 = 210 (the guard after TMH_RW).
;
; A GROUP row is '+' or '-' (closed, open; a space for a row with nothing to
; open), the owner's name in TMH_GNAMEC, its total at the SIZE column and its
; legend square, when it has one, at TMH_SQC - the TIER column a group does
; not use. An ITEM row (a part of System that is not a claim, a program's
; total inside the Programs group, a 640K-1MB row) is indented like the group
; it sits in and puts its address, when it has one, at the ADDR column.
TMH_IND     equ 2               ; the indent that puts a claim under its owner
TMH_TYPEC   equ 8               ; TYPE - tm_ktab's eight-byte name, all of it
TMH_TIERC   equ 4               ; TIER - 'TRIV'/'LOW'/'MED'/'HIGH'/'HELD'
TMH_ADDRC   equ TMH_IND + TMH_TYPEC + 1     ; 11: where ADDR starts
TMH_SIZEC   equ TMH_ADDRC + 4   ; 15: where tm_kcol's five start, its leading
                                ; space the separator
TMH_GNAMEC  equ TMH_SIZEC - 1   ; 14: a group's name, after its marker column
TMH_SQC     equ TMH_SIZEC + 6   ; 21: a group's legend square, in the TIER
                                ; column a group leaves empty
%if TMH_IND + TMH_TYPEC + 1 + 4 + 5 + 1 + TMH_TIERC > TM_NCHUNK * TM_CHUNK
  %error "taskmgr: the claim row is wider than the chunk span it is padded to"
%endif
%if TMH_SQC + 1 > TM_NCHUNK * TM_CHUNK
  %error "taskmgr: a group's legend square is past the chunk span"
%endif

; --- ...and the SCROLL BAR that makes the rest of the table reachable --------
; No screen here shows more than 27 rows and the list, opened all the way, is
; a hundred, so it runs off the bottom of its own frame - silently, because
; tm_row_place refuses a row past tm_ylim and says nothing about the rows
; behind it (SPEC.md 28.4.4). The standard bar (SPEC.md 13.10) owns the
; rightmost TMH_SB_W of the LAST column's band and the rows stop short of it.
;
; THE ROWS GIVE UP 8 PIXELS ON THE LEFT TO BUY 14 ON THE RIGHT: the list
; letters from TM_PEN where the memory view's old list lettered from TM_MPEN,
; whose first ten pixels were the legend square's - the square sits in the
; TIER column now (TMH_SQC). That leaves the two pixels between the chunk span
; and the bar as a gutter, and keeps the pen on a multiple of 8 - which is
; what font_run's single-store path is sized by (SPEC.md 6.1), and is the
; reason TM_PEN is 8 rather than the 6 it started at.
TMH_SB_W    equ 14              ; the standard bar's width (SPEC.md 13.10.2):
                                ; what both kernel bars use and what the arrow
                                ; glyph's 9 pixels are drawn for
TMH_SB_X1   equ TM_RW - TMH_SB_W + 1    ; band-relative, so it is one number
TMH_RW      equ TMH_SB_X1 - 1   ; ...and a list row's own right edge, which is
                                ; what tm_rowr hands the last chunk's fill
%if TM_PEN + TM_NCHUNK * TM_CW > TMH_RW + 1
  %error "taskmgr: a list row now runs under its own scroll bar"
%endif

; The AVAIL line is 'AVAIL nnnK  MAX RUN nnnK': 6 + 4 + 2 + 8 + 4 = 24, and a
; caption is a tm_lrun of TM_LCELLS. Said as a guard because the note beside
; TM_STRMAX is that it has ONE character of slack.
%if (6+3+1) + (2+8+3+1) > TM_STRMAX - 1
  %error "taskmgr: the AVAIL / MAX RUN line no longer fits tm_str"
%endif

; The CPU/scheduler caption is chunked too, and it borrows the row machinery
; whole by being a row that no list draws: it is a 20-character line whose
; first two chunks carry a percentage that moves every interval and whose
; last two carry a scheduler mode that essentially never does. As one check
; word it redrew all twenty glyphs twice a second on a machine doing nothing,
; which on the mono adapters made it the single largest recurring cost of
; having this window open.
; ...and WHICH list is the deepest depends on which pages this build has.
; tm_rowck is a check word per chunk per row and is sized from it (SPEC.md
; 28.4), so an arm without the memory page is not merely not drawing those
; rows - it is not carrying the bss that stands behind them.
;
; The memory page's list has no depth of its own any more - opened all the
; way it is every claim, every block above 1MB and a heading each - and a
; check word is only ever believed for a row that is ON SCREEN: a composed
; row past the foot is never drawn, so its check words are never read. What
; the screen can show is bounded by tm_layout, which derives every depth from
; a frame height it has already capped at TMM_ROWS, and which runs two columns
; only when column 0 holds fewer than TM_COL2_MIN rows:
;   one column:  the list's column 0 under a TMM_ROWS frame (19)
;   two columns: its column 0 under a (TM_COL2_MIN - 1)-row frame, plus one
;                top-anchored column under the same frame (11 + 16)
; The larger of the two is the most tm_maxrow can ever be, and tm_layout caps
; it there as well, so the bound holds by construction and not by argument.
%ifdef TMF_MEM
TM_SHOW1    equ TMM_ROWS
TM_SHOW2    equ (TM_COL2_MIN - 1) + ((TM_COL2_MIN - 1) * TM_ROW_H + TMM_ROW_Y - TM_C2_ROW_Y) / TM_ROW_H
  %if TM_SHOW1 > TM_SHOW2
TM_SHOWN    equ TM_SHOW1
  %else
TM_SHOWN    equ TM_SHOW2
  %endif
%else
TM_SHOWN    equ TM_PROWS        ; the process list's own 14: the one list
%endif

TMR_CPU     equ TM_SHOWN        ; the virtual row index it draws as: past
                                ; every row any list on this arm can show
TM_NCK      equ TM_SHOWN + 1    ; ...so tm_rowck holds one more row than that
%if TM_SHOWN < TM_PROWS
  %error "taskmgr: tm_rowck is no longer sized for the process list"
%endif
%ifdef TMF_MEM
  %if TM_SHOWN < TMM_ROWS
    %error "taskmgr: tm_rowck is no longer sized for the memory list"
  %endif
%endif

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
; ...and TM_INKX, which CLOBBERS AL. The audit was done (apps size pass 2): at
; twelve of the fourteen sites AX was dead or could be loaded after the pen,
; so the push/pop was two bytes and 27 cycles each for nothing. TM_INK stays
; where AX really is live across the pen (tm_clear_owed's damage rect).
%macro TM_INKX 1
    mov al, %1
    call OSAPI_SET_COLOR
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
    mov word [tm_lowkb], TM_LOWKB
    call tm_ebda                ; ...and the BIOS's own KB above it, if the
    add [tm_lowkb], bx          ; BIOS says where (SPEC.md 28.4.8): IVT+BIOS
    add ax, bx                  ; carries it, and the total with it, so base
    mov [tm_totkb], ax          ; memory adds up to what is installed

    ; --- is there a store above 1MB at all? ----------------------------------
    ; Asked ONCE, here, because everything below derives the frame from it and
    ; the answer cannot change while the machine runs: xm_init sizes the pool
    ; at boot and nothing resizes it (SPEC.md 41.5). Asked of the POOL SIZE and
    ; never of xm_caps' free figure - a full pool answers 0 free and still very
    ; much exists - and on kern_small the slot is a stub answering tier 0's
    ; answers, so this needs no build-time test and gets that build right for
    ; free (SPEC.md 41.11).
    call tm_getkb
    cmp word [tm_kb+SK_XMS], 0  ; [tm_xoff] arrives 0 - the loader zeroes our
    jne .xms                    ; bss, and this runs once, at launch - so only
    mov byte [tm_xoff], TMM_XSHIFT  ; the no-store answer is written
.xms:
%ifdef TMF_MEM
    call OSAPI_CPU_INFO         ; UNDER PROTECTED MODE EXTENDED MEMORY IS RAM
    test ah, CPU_F_PM           ; (KERN-MEGA-PLAN 13.6): not a store a copy
    jz .nopm                    ; reaches but memory a package may run in -
    mov byte [tm_pm], 1         ; so RAM's figures count it and a region placed
                                ; above 1MB reads HIGH. Asked once: it cannot
                                ; change.
    mov byte [tm_xoff], TMM_XSHIFT  ; ...and RAM IS RAM, so it has ONE bar
                                ; (KERN-MEGA-PLAN 14.70.2): the XMS line's
                                ; figures and its bar are kern_big's separate
                                ; store and nothing else. 'HIGH 421/3456K'
                                ; under a summary line already counting the
                                ; same KB was the confusing half of the page,
                                ; so the page lays out as a machine with no
                                ; store does - the CPU tier alone on that line
                                ; and everything below it a row higher
.nopm:
    cmp word [tm_kb+SK_XMS], 0  ; ...and whatever the mode, memory above 1MB
    je .nohi                    ; is the user's RAM and is accounted when the
    xor cx, cx                  ; pool can say whose it is: OSAPI_XMEM_WALK
    call OSAPI_XMEM_WALK        ; answers (CX = 0 asks only that) on kern_mega
    jc .nohi                    ; in protected mode and on kern_big with a
    mov byte [tm_hion], 1       ; store (SPEC.md 28.4.8)
.nohi:
    call tm_romscan             ; ...and what is in 640K-1MB (28.4.9)
%endif

    call OSAPI_VIDEO            ; and the live geometry (SPEC.md 39.2): AX =
    mov [tm_vw], ax             ; width, CX = the dock strip's top row. Banked
    mov [tm_vdock], cx          ; because everything below spends AX..DX
    xchg ax, cx

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
    sub ax, MBAR_H + 1          ; AX = px available to the whole frame
    call tm_layout              ; ...and everything derived from it, which the
                                ; SPEC.md 11.98 handler below re-runs when the
                                ; box moves under us
    mov ax, [tm_tpl+6]          ; ...and AGAIN, from the frame it just CHOSE.
    call tm_layout              ; The first pass answers "how many rows would
                                ; the band allow", which is not the same
                                ; question as "how many does this window have"
                                ; whenever TMM_ROWS caps the height - 32 against
                                ; 19 on a 480-row screen. The handler can only
                                ; ever ask the second, so without this the two
                                ; paths disagree about an identical frame and
                                ; the launch figure is the loose one. It
                                ; converges in one step by construction: the
                                ; height is min(rows, TMM_ROWS) * pitch +
                                ; chrome, so feeding it back yields
                                ; min(rows, TMM_ROWS) and then stops

    mov dx, [tm_vdock]          ; ...and a top that keeps y+h off the dock.
    dec dx                      ; Deriving the height is not enough on its
    sub dx, [tm_tpl+6]          ; own: wm_fit clamps y to dock_y0 - h, which
    cmp dx, 100                 ; puts the shadow row back on the strip for
    jge .ykeep                  ; any frame tall enough to be clamped at all
    cmp dx, MBAR_H              ; (signed - a tiny screen can drive this below
    jge .yset                   ; the menu bar, and there it simply cannot win)
    mov dx, MBAR_H
.yset:
    mov [tm_tpl+2], dx
.ykeep:

    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_rowsfit - how many TM_ROW_H rows a frame AX px tall holds under DX px of
;              chrome; a screen too short for any still gets one row
; out: AX = rows (the quotient fits a byte: the tallest frame here is 295px
;      against an 11px pitch); clobbers DX - every caller loads it per call
; -----------------------------------------------------------------------------
tm_rowsfit:
    sub ax, dx
    jbe .one
    mov dl, TM_ROW_H
    div dl
    cbw                         ; AL < 128: the quotient fits, as above
    ret
.one:
    xor ax, ax
    inc ax
    ret

; -----------------------------------------------------------------------------
; tm_layout - every derived layout word, from a frame this window can have
;
; in:  AX = the frame HEIGHT available, [tm_vw] = the frame WIDTH available
; out: tm_colrows, tm_cols, tm_pcolrows, tm_col2rows, tm_maxrow and
;      tm_tpl+4 / tm_tpl+6, all set
; clobbers: AX, BX, CX, DX
;
; FACTORED OUT OF tm_init FOR SPEC.md 11.98, and the two callers hand it two
; different things that mean the same thing. At launch the frame does not
; exist yet, so what is available is the desktop band and the screen's width,
; and the template is what comes out. On a resize the frame is a fact and the
; window cannot change it from inside the handler - so the caller passes the
; box it was GIVEN, and the template words it writes are inert.
;
; Nothing here clears tm_rowck: the caller's repaint reaches tm_draw_full,
; which calls tm_rowck_clear, and that routine's own comment already names a
; resize as one of the three things that arrive there.
; -----------------------------------------------------------------------------
tm_layout:
    add ax, [tm_xoff]           ; the bar's height back on a machine with no
                                ; store, which is what turns the reclaimed
                                ; space into a ROW rather than into a shorter
                                ; window (TMM_XSHIFT = TM_ROW_H)...
    mov dx, TITLE_H + 1 + TMM_ROW_Y     ; ...less the chrome and the maps
    call tm_rowsfit             ; AX = rows that fit in ONE column (a screen
    mov [tm_colrows], ax        ; this short still gets one) - the column's
                                ; depth

    ; --- one column, or two? -------------------------------------------------
    mov cx, 1
    mov bx, TM_W
    cmp ax, TM_COL2_MIN
    jae .cols                   ; tall enough: one column shows a useful list
    cmp word [tm_vw], TM_W + TM_COLW    ; short: a second column IF the
    jb .cols                    ; screen is wide enough to carry it
    inc cx
    mov bx, TM_W + TM_COLW
.cols:
    mov [tm_cols], cx
    mov [tm_tpl+4], bx          ; the frame width the layout needs

    cmp ax, TMM_ROWS            ; the height is COLUMN 0's worth: it is the
    jbe .hok                    ; column that starts lowest, so it is the one
    mov al, TMM_ROWS            ; the frame has to be tall enough for (AH = 0)
.hok:
    mov dl, TM_ROW_H
    mul dl
    add ax, TITLE_H + 1 + TMM_ROW_Y
    sub ax, [tm_xoff]           ; the list starts higher with no bar above it
    mov [tm_tpl+6], ax          ; the frame height the machine can take

    ; --- how deep is the PROCESS list's column 0? -----------------------------
    ; The same frame again, and this one starts LOWER than [tm_colrows] rather
    ; than higher: TM_ROW_Y is under the graph, the RAM line and the bar, 30px
    ; below the memory view's TMM_ROW_Y that the depth above is cut from.
    ;
    ; Sharing that depth is what SPEC.md 28.1's second column was lost to. It
    ; named rows the frame has no height for, so tm_row_place placed them in
    ; column 0 and then refused them on [tm_ylim] - and a refusal is MONOTONE
    ; to the caller, which is the whole contract that lets tm_rows stop at the
    ; first one. It stopped inside column 0, and every row after it, column 1
    ; included, was never reached: on a CGA, three rows drawn of thirteen with
    ; an empty second column beside them and its header already on the glass.
    push ax
    mov dx, TITLE_H + 1 + TM_ROW_Y
    call tm_rowsfit
    mov [tm_pcolrows], ax
    pop ax

    ; --- how deep is a LATER column? -----------------------------------------
    ; Not [tm_colrows]: a later column is top-anchored (TM_C2_ROW_Y), so it
    ; holds every row the frame has height for, which on a short screen is
    ; three times what column 0 gets.
    mov dx, TITLE_H + 1 + TM_C2_ROW_Y
    call tm_rowsfit             ; rows in one top-anchored column
    mov [tm_col2rows], ax

    mov bx, [tm_cols]           ; the whole list's depth: column 0, plus every
    dec bx                      ; later column's own
    mul bx
%ifdef TMF_MEM
    add ax, [tm_colrows]        ; the memory page's column 0: its list starts
                                ; 30px above the process list's, so it is the
                                ; deeper of the two. Safe for the other: the
                                ; performance list is bounded by its own
                                ; `cmp si, TM_PROWS`, and tm_ylim clamps both.
                                ; (The heap page's column 0 was deeper still,
                                ; and went with the page, KERN-MEGA-PLAN
                                ; 14.70.2)
%else
    add ax, [tm_pcolrows]       ; ...and with no memory view there is one
                                ; list on this window, so its own depth is
                                ; the bound (SPEC.md 28.12)
%endif
    cmp ax, TM_SHOWN            ; never more than tm_rowck has check words
    jbe .rowcap                 ; for (TM_SHOWN)
    mov ax, TM_SHOWN
.rowcap:
    mov [tm_maxrow], ax
    ret

; -----------------------------------------------------------------------------
; tm_onresize - the box moved and we did not ask for it (SPEC.md 11.98)
;
; in:  SI = our window, CX = the new content width, DX = the new content
;      height; the gfx lock is HELD and this may not draw
; out: nothing (all registers preserved)
;
; The Task Manager derives its row count and its column count ONCE, at launch,
; from the desktop band - which is exactly the shape SPEC.md 11.98 exists for.
; Without this, switching a VGA to its CGA left it laying out for 284 rows of
; frame inside 155, and the surplus went through the bottom of its own frame
; onto the desktop (gfx_* clips to the SCREEN, not to the window).
;
; It re-derives and returns. The repaint that follows is the kernel's, and it
; is what puts the new layout on the glass.
; -----------------------------------------------------------------------------
tm_onresize:
    push ax
    push bx
    push cx
    push dx
    push si
    push dx                     ; **THE CONTENT BOX THIS PROC WAS HANDED** -
                                ; the kernel applies a published frame BEFORE
                                ; it notifies (SPEC.md 39.16.3.3), so CX/DX
                                ; already describe the window we now have
    mov bx, si                  ; ...and the CARD it is on (SPEC.md 39.16.4):
    call OSAPI_WM_DISPLAY       ; AX = w, CX = the first row the dock owns,
                                ; SI = the first the band has
    mov [tm_vw], ax             ; ...and it is THE DISPLAY'S WIDTH, not the
                                ; frame's. This used to pass the frame, and
                                ; tm_layout's one reader of it asks "is there
                                ; room for a SECOND COLUMN" - so a window that
                                ; had one column was 232px wide, 232 is less
                                ; than TM_W + TM_COLW, and the answer was NO
                                ; FOR EVER. A one-column Task Manager dragged
                                ; onto a CGA could never reach two columns,
                                ; which is what the field reported
    mov [tm_vdock], cx
    pop ax                      ; the CONTENT height, back as a FRAME one -
                                ; tm_layout's argument is a frame. Handing it
    add ax, TITLE_H + 1         ; the display's BAND instead makes tm_colrows
                                ; what fits the SCREEN rather than what fits
                                ; the window, which is 32 rows on a VGA against
                                ; the 19 the frame holds
    call tm_layout              ; ...which re-derives the column count and
                                ; everything under it. The FRAME is the
                                ; kernel's: this proc may not draw and a resize
                                ; repaints, so the size is PUBLISHED once at
                                ; launch and applied by wm_land_fit
    jmp tm_pop_sdcba

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
    mov al, 1                   ; **WE PAINT EVERY PIXEL** (SPEC.md 11.90.1,
    call OSAPI_WM_OWNBG         ; 28.10): the kernel's white fill was the first
                                ; layer of a double draw on a window whose
                                ; W_PAINT is ~450 ms, so the content went white
                                ; and re-lettered on every repaint. It also
                                ; unlocks OSAPI_WM_DAMAGE, which answers
                                ; "whole" without this flag (SPEC.md 11.90.2's
                                ; interlock)
    mov si, tm_about            ; 'About Task Manager' above the Close the
    call OSAPI_ABOUT_SET        ; kernel already puts in our pull-down
                                ; (SPEC.md 12.2) - BX is still the window and
                                ; the slot preserves the flags. AX and SI are
                                ; scratch here: the entry proc's own push/pop
                                ; of SI is what it owes the loader

    ; --- bind the window and ask for the byte-aligned content origin --------
    ; (this was tm_kinit, called once, from here). The loader zeroes our bss,
    ; so the state block does not need clearing the way it did as a built-in
    ; kind: a relaunched instance is a fresh image with a fresh bss and cannot
    ; inherit the previous one's calibration or graph.
    mov [tm_win], bx

    mov ax, TM_PREF_H           ; **THE HEIGHT IS PATCHED, NOT FUDGED.**
    sub ax, [tm_xoff]           ; TMM_XSHIFT is added back on a machine with no
    mov [tm_pref + 2], ax       ; XMS bar to draw, which is a property of the
    mov [tm_pref + 6], ax       ; MACHINE and not of the adapter - so a static
    mov [tm_pref + 10], ax      ; table is one row wrong on one kind of machine
                                ; or the other. [tm_xoff] is settled well
                                ; before the window exists and this table is
                                ; our own segment's, so the honest answer is to
                                ; write the number in rather than pick which
                                ; machine to be wrong on
    mov si, tm_pref             ; **THE FRAME PER ADAPTER** (SPEC.md 11.100.1).
    call OSAPI_WM_PREFER        ; tm_layout has always worked out the frame its
                                ; column count needs and nothing applied it
                                ; after launch, so a Task Manager that started
                                ; one-column could never become two - and the
                                ; kernel is the only thing that may resize a
                                ; window at a moment when drawing is safe
    mov ax, tm_onresize         ; tell us when the box moves under us and we
    call OSAPI_WM_ONRESIZE      ; did not ask - an adapter change (SPEC.md
                                ; 11.98). BX is the window, which is what this
                                ; routine was handed
    mov al, 1                   ; every row this window draws is an opaque
    call OSAPI_WM_SNAP          ; text run, so it asks for its content origin
                                ; on a multiple of 8 and every one of them
                                ; reaches font_run's single-store path
                                ; (SPEC.md 11.94/6.1). EVERY adapter: this was
                                ; mono-only until VGA was measured and gained
                                ; more (verified aligned there - content
                                ; origin 248). It PRESERVES FLAGS, so the CF
                                ; our ret owes the loader is the one above
%ifdef TMF_HEAP
    mov ax, tm_onwheel          ; the WHEEL scrolls the heap page's list
    call OSAPI_WM_ONWHEEL       ; (SPEC.md 13.18); flags preserved too, and
%endif                          ; kern_small's cell does nothing
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE
.out:                           ; the loader is wm_create's
    pop si                      ; POP leaves the flags alone
    ret

; -----------------------------------------------------------------------------
; tm_hire - claim the worker task, retrying until one is free (SPEC.md 20.6)
; in:  [tm_win] valid; called from tm_paint, which holds the gfx lock
; out: nothing (all registers preserved)
;
; Latches on SUCCESS only, so a refusal is retried by the next paint rather
; than being permanent - MAX_TASKS is 14 and shared, and closing one Timer
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
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2): the
    ; kernel wrote our segment into this worker's frame before its
    ; first instruction, so mem_frameless pins a region with an
    ; undeclared worker however that region is declared. What a restart
    ; costs is one pass of the loop - the park is inside
    ; OSAPI_TASK_ALIVE and nowhere else (this package is not
    ; OSAPI_MEM_PARKSAFE), which is the TOP of the loop, and every byte
    ; that outlives a pass is a static and moves with us.
    OS88_WORKER_RESTARTABLE tm_worker
.norun:
    pop bx
    pop ax
.out:
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
    OS88_PREFER tm_pref, TM_PREF_W1, TM_PREF_H,  TM_PREF_W1, TM_PREF_H,  TM_PREF_W2, TM_PREF_H

; =============================================================================
; 'About Task Manager' - the credit card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; The card is os88ui.inc's. What is here is the flag, the painter drawing it
; last, the click taking it down, and the worker's guard - tm_worker refreshes
; the same content on a timer and its tm_update is INCREMENTAL, so without the
; guard it would letter changed rows straight through the card rather than
; over the whole of it.

; -----------------------------------------------------------------------------
; tm_about - the OSAPI_ABOUT_SET handler (slot 0x018A)
; in:  SI = our window ptr; the UI task, gfx lock HELD
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
tm_about:
    push bx
    push si
    mov byte [tm_abon], 1
    mov bx, si
    mov si, tm_ablines
    call os88ui_about               ; arms the clip itself: a menu dispatch
    pop si                          ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_abdismiss - take the card down if it is up
; in:  SI = our window ptr; gfx lock held
; out: CF = 1 the click was spent doing it; preserves every register
;
; tm_clear_content + tm_draw_full, which is the view swap's own pair: this
; window carries WF_OWNBG (SPEC.md 11.90.1), so nothing else erases the
; ground under the card, and the incremental painter would leave it standing.
; -----------------------------------------------------------------------------
tm_abdismiss:
    cmp byte [tm_abon], 0
    je .none
    push bx
    mov byte [tm_abon], 0
    mov bx, si
    call OSAPI_WM_CLIP_SET      ; nothing has armed a region for a click
    jc .gone                    ; (SPEC.md 11.3)
    call tm_clear_content       ; SI = window ptr, as the view swap calls it
    call tm_draw_full
.gone:
    pop bx
    stc
    ret
.none:
    clc
    ret

; --- the About card's lines (SPEC.md 20.5.1) ----------------------------------
tm_ablines:
    dw tm_ab1, tm_ab2, tm_ab3, 0
tm_ab1:     db 'Task Manager for os8088', 0
tm_ab2:     db 0
tm_ab3:     db 'Contributed by Elendilon', 0

tm_tpl:     dw 250, 100, 232, 312, tm_ttl, tm_paint, 0, tm_click
tm_ttl:     db 'Task Manager', 0

tm_s_cpu:   db 'CPU ', 0
tm_s_pre:   db 'SCH preempt', 0 ; read-only scheduler-mode field, chars 9..19
tm_s_coop:  db 'SCH coop   ', 0 ; both exactly 11: the padding erases the
                                ; longer word when the mode changes
tm_s_ram:   db 'RAM ', 0
tm_s_heap:  db '  HEAP ', 0     ; + claimed/size KB (SPEC.md 50), on both
                                ; pages. The two leading spaces are the RAM
                                ; line's gap, and the memory view draws the
                                ; claim swatch into them
%ifdef TMF_MEM
tm_s_rampm: db 'Ram ', 0        ; kern_mega's ONE pair (KERN-MEGA-PLAN
                                ; 14.70.2): RAM the system is not using is
                                ; RAM a program can claim there, so used /
                                ; total of the whole machine IS the view, and
                                ; a HEAP pair beside it said the same thing
                                ; twice. In MB to two decimals, 'Ram
                                ; 0.47/4.00M'; the mixed case tells it from
                                ; the split line every other machine shows
%endif
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
tm_s_sys:   db 'System', 0      ; row 1: the kernel's own share
; Row 0, IVT+BIOS (SPEC.md 28.4.7, 28.4.8): no state, no CPU - up to the
; MEM column, which tm_memcol_kb fills from [tm_lowkb]
tm_s_lowrow:
            db 'IVT+BIOS'
            times TM_NAMEF - 8     db ' '
            times TM_STC + 1       db ' '
            db '  -'
            times TM_CPUC - 3      db ' '
            db 0
; The ST column, each WITH the separator the CPU column needs after it - the
; one place every state string is copied, so the space is data rather than a
; store after every copy (TM_STC + 1)
tm_s_run:   db 'run ', 0
tm_s_rdy:   db 'rdy ', 0
tm_s_slp:   db 'slp ', 0
tm_s_evt:   db 'evt ', 0        ; no worker task: runs only in callbacks
tm_s_die:   db 'die ', 0        ; I_STATE 2: closing, task not yet reaped
; A FREE SLOT'S ROW IS A CONSTANT, so it is one string rather than tm_namef,
; three copies and a store: '-' padded to TM_NAMEF, '---' and its separator,
; then the CPU and MEM columns' dashes. ITS TAIL IS THE MEMORY PAGES' DASHES,
; overlapped rather than repeated: ' ' + tm_s_dash5 is the blank SIZE/HEAP
; column with its leading gap (tm_s_dash6), tm_s_dash5 that column with
; tm_kcol's leading space, tm_s_dash4 one bare KB column
tm_s_freerow:
            db '-'
            times TM_NAMEF - 1     db ' '
            db '---   -'
tm_s_dash6: db ' '
tm_s_dash5: db ' '
tm_s_dash4: db '   -', 0
%if tm_s_dash4 + 5 - tm_s_freerow != TM_NCHUNK * TM_CHUNK + 1
  %error "taskmgr: the free slot's row is no longer exactly one row long"
%endif

%ifdef TMF_MEM
; --- the memory page (SPEC.md 28) --------------------------------------------
tm_s_t86:   db '8086  ', 0      ; the detected tier (SPEC.md 41.1), which
tm_s_t286:  db '286   ', 0      ; nothing else in the UI says out loud. Each
tm_s_t386:  db '386+  ', 0      ; name is padded to the SAME six columns, so
                                ; the XMS figures beside it do not shuffle
                                ; left and right between machines. The 'CPU '
                                ; in front of them is tm_s_cpu, the
                                ; performance view's own label - one string,
                                ; because it is one word
tm_s_xms:   db 'XMS ', 0        ; + used/size KB above 1MB (SPEC.md 41)

; The list's HEADER is three runs, two of them SWITCHES lettered white on
; black so they read as something to press (KERN-MEGA-PLAN 14.70.2): the view
; at the left, over the names, and +ALL / -ALL at the right, over the TIER
; column. The middle run is what puts ADDR at column 11 and SIZE over the
; 'nnnK' at 16 - exactly where a claim row composes them (TMH_ADDRC,
; TMH_SIZEC) and where every group's tm_kcol total lands. Each switch is
; TMH_SWC characters, so the three are one 25-character line.
TMH_SWC     equ 8               ; the view switch's width
TMH_XSWC    equ 4               ; ...and +ALL's
tm_s_vprog: db 'PROGRAMS'       ; the view each one SHOWS - a click on it is
tm_s_vsys:  db 'SYSTEM  '       ; the other one
tm_s_hmid:  db '   ADDR SIZE ', 0
tm_s_xall:  db '+ALL', '-ALL'   ; open every group / close every group
%if TMH_SWC + (tm_s_xall - tm_s_hmid - 1) + TMH_XSWC != TM_NCHUNK * TM_CHUNK
  %error "taskmgr: the list header is no longer exactly one row long"
%endif
%if TMH_SWC + 3 != TMH_ADDRC
  %error "taskmgr: the header's ADDR is no longer over the rows' ADDR"
%endif

tm_s_avl:   db 'AVAIL ', 0      ; NOT 'FREE': it counts the purgeables, so it
                                ; is what a claim can GET rather than what is
                                ; unused (SPEC.md 28.4.1) - the Purgeable
                                ; group below names what of it is a cache
tm_s_hmax:  db '  MAX RUN ', 0  ; the largest single RUN of AVAIL - the
                                ; biggest claim that can succeed in ONE call
                                ; ('run' is the kernel's own word for it,
                                ; mem_run, SPEC.md 50.3). AVAIL less it is the
                                ; heap page's FRAG, which is arithmetic the
                                ; page no longer prints (KERN-MEGA-PLAN
                                ; 14.70.2)

; GROUP names: an owner and its total. A program's is its I_NAME, a driver's
; its file's (tm_drvnam), and the rest are these.
tm_s_gprog: db 'Programs', 0    ; the SYSTEM view's one row for every package
tm_s_gkern: db 'Kernel', 0      ; the kernel's own claims, and above 1MB its
                                ; own pool blocks - its caches on kern_mega
tm_s_gpurg: db 'Purgeable', 0   ; every cache the next claim may take back:
                                ; in NOBODY's total and so in no total at all
                                ; (SPEC.md 28.4.1), and the reason AVAIL is
                                ; more than the heap less what is held
tm_s_drv:   db '.DRV', 0

; ITEM names: the parts of System that are not claims, and the 640K-1MB rows.
; Below 640K every figure is a term of the kernel's own ladder (SPEC.md 20.9),
; so they cost string copies and no arithmetic; IVT+BIOS is the 1.5K below
; KERNEL_SEG plus the half-KB the whole-KB heap and System leave, and the
; BIOS's extended data area when it states one (SPEC.md 28.4.7, 28.4.8).
%if KERNEL_SEG * 16 != 1536
  %error "taskmgr: the low-memory row is 1.5K plus a half-KB and KERNEL_SEG moved"
%endif
tm_s_low:   db 'IVT+BIOS', 0
tm_s_bimg:  db 'Code+data', 0
tm_s_bstk:  db 'Stacks', 0
tm_s_bfat:  db 'FAT snap', 0
tm_s_bhi:   db 'Above 1MB', 0   ; between 1MB and the pool, named no further
                                ; - kern_big's, or a kernel that names no
                                ; fixed area (tm_hikb)
tm_s_uhdr:  db '640K-1MB', 0    ; the read-only map's heading (28.4.9)
tm_s_uvid:  db 'Video', 0
tm_s_urom:  db 'ROM', 0
tm_s_ubios: db 'BIOS', 0
tm_s_unrep: db 'Unrep.', 0

; ...and the fixed areas kern_mega keeps between 1MB and its pool, which the
; walk names when it is asked to (OSAPI_XMEM_WALK, BX = XMW_FIXED): records of
; TMX_NREC bytes indexed by the owner byte less XMW_HMA. The HMA is the DOS
; trip's (KERN-MEGA-PLAN 2.7) and the padding is the pool's 64KB alignment;
; KERN-MEGA-PLAN 14.70.1 is what would make both of them RAM again.
TMX_NREC    equ 12
tm_xnames:  db 'HMA (DOS)', 0, 0, 0
            db 'GDT', 0, 0, 0, 0, 0, 0, 0, 0, 0
            db 'LDT+IDT', 0, 0, 0, 0, 0
            db 'Kernel code', 0
            db 'Padding', 0, 0, 0, 0, 0
%if $ - tm_xnames != (XMW_PAD - XMW_HMA + 1) * TMX_NREC
  %error "taskmgr: tm_xnames is one TMX_NREC record per fixed area"
%endif

; A DRIVER's group is named for its FILE, by its class (DRV_H_CLASS in a
; conventional image's own header; the walk's XMW_CLS for one kern_mega moved
; above 1MB): eight-byte records indexed by class, NUL-padded, and '.DRV'
; after. Class 0 - an overlay (DRVC_OVL), or a block whose row the kernel
; could not find - and the retired 3 are 'Drivers', one group for the lot.
TMD_NCLS    equ 10
tm_dnames:  db 'Drivers', 0
            db 'SOUND', 0, 0, 0
            db 'HDD', 0, 0, 0, 0, 0
            db 'Drivers', 0
            db 'ETHER', 0, 0, 0
            db 'NET', 0, 0, 0, 0, 0
            db 'USBMOUSE'
            db 'EMS', 0, 0, 0, 0, 0
            db 'RAMDISK', 0
            db 'CDROM', 0, 0, 0
%if $ - tm_dnames != TMD_NCLS * 8
  %error "taskmgr: tm_dnames is one 8-byte record per driver class"
%endif
%if DRVC_SOUND != 1 || DRVC_DISK != 2 || DRVC_NET != 4 || DRVC_FILE != 5 || DRVC_POINT != 6 || DRVC_EMS != 7 || DRVC_FILE2 != 8 || DRVC_CD != 9
  %error "taskmgr: tm_dnames is indexed by the DRVC_* class numbers"
%endif

; TYPE names for a block ABOVE 1MB, by its owner byte (OSAPI_XMEM_WALK)
tm_s_txmem: db 'XMem', 0        ; a slot's OSAPI_XMEM_ALLOC block
tm_s_tdimg: db 'DrvImg', 0      ; FDh: a driver's image, moved above 1MB
tm_s_thicl: db 'HiClaim', 0     ; 0A0h | slot: an OS88_HIGHMEM claim
tm_s_tkern: db 'Kernel', 0      ; FEh, the kernel's own; FFh, a cell called
                                ; outside any instance
%endif

%ifdef TMF_HEAP
; TIER names, FOUR-CHARACTER RECORDS indexed by the owner's high byte -
; MEM_PG_TRIV first, so the index is (high byte - MEM_PG_MIN) - and copied
; by tm_htier at TMH_TIERC = 4, so a record needs no NUL and no pointer
; (apps size pass 2; it was a pointer table and five strings). The fifth
; record IS 'HELD' below: an unpurgeable claim is HELD, and the record is the
; string.
tm_tiers:   db 'TRIV', 'LOW ', 'MED ', 'HIGH'
%if TMH_TIERC != 4 || MEM_PG_MAX - MEM_PG_MIN != 3
  %error "taskmgr: tm_tiers is four 4-character records"
%endif
tm_s_hheld: db 'HELD', 0

; TYPE names, TMH_TYPEC = 8 columns. A claim the table does not know prints its owner
; word in hex instead (tm_htype) - a debug page must not label an unknown tag
; as the nearest thing it recognises.
tm_s_tregn: db 'Region', 0      ; owner = the slot, base = I_SPTR (SPEC.md 20.1)
tm_s_tdata: db 'Data', 0        ; ...anything else that instance holds (50.3)
tm_s_tdrvb: db 'DrvBuf', 0      ; ...and what that driver CLAIMED (SPEC.md
                                ; 28.4.6): a ring, a pool, a listing buffer.
                                ; It is NOT in tm_ktab and cannot be - a
                                ; driver's claim carries the driver's own
                                ; IMAGE SEGMENT as its owner word (mem_own),
                                ; which is a different number on every boot,
                                ; so tm_hdrv decides this one by walking the
                                ; snapshot rather than by matching a constant
tm_s_tfatw: db 'FATwin', 0
tm_s_tview: db 'DirView', 0     ; kern_small's listing cache (SPEC.md 50.6.5)
tm_s_twsav: db 'WinSave', 0

; (owner word, name) RECORDS of TMK_REC bytes, ended by a 0 owner: the name
; is IN the record, NUL-padded to eight, rather than a pointer to a string
; elsewhere (apps size pass 2: the pointer and the terminator were three
; bytes a tag). tm_htype copies TMH_TYPEC = 8 of it, the whole name - at 7
; the three eight-letter ones lost their last letter (SPEC.md 28.12.2).
; MEM_P_WSAVE is NOT here: it
; is a RANGE (SPEC.md 11.96.3), one cache per window slot, and tm_htype tests
; it before it walks this. Why some rows are here at all:
;   MEM_P_ICO   - the machine-wide icon store (SPEC.md 25.9), one row per
;                 DISTINCT body, claimed at the first mount that harvests one
;                 and alive until purged: leaving it out put a permanent hex
;                 row on the heap page of every machine - MEM_K_BAND's lesson
;   MEM_K_MOD, MEM_K_CLONE, MEM_K_BAND - the three that were MISSING, every
;                 one printed as a bare owner word (SPEC.md 28.4.3). The hex
;                 fallback is the table's honesty rule about a tag this build
;                 has never SEEN, not a place to leave one the kernel ships:
;                 MEM_K_BAND is claimed at boot and never freed, so 'FF0A' was
;                 a permanent row on every machine with a band composer.
;                 tests/unit/t_ktags.py is the gate that stops a fourth.
;                 ModImg sits beside DrvImg because it is the same thing one
;                 rung down: an image claimed top-down whose base IS its CS
;                 (SPEC.md 2.8); Cloner is the job block AND the sectors in
;                 flight (SPEC.md 18.99); BandBuf the 1bpp composer's (5.9.2)
;   MEM_K_HIB   - the resume's extent list (SPEC.md 87.5), alive only on the
;                 way into the stub
;   MEM_K_FONT  - the glyph table copied out of an option ROM at boot (SPEC.md
;                 6.0.1): 1KB, top-down
;   MEM_K_FDLG  - the Standard File dialog's listing (SPEC.md 38.2): the FOURTH
;                 the prose above warned about - 'FF0F' was on this page for as
;                 long as any Save or Open was up
;   MEM_K_SC    - the desktop shortcuts' one table (SPEC.md 26.8), read off
;                 SYSTEM.CFG at boot and held while any shortcut exists
;   MEM_K_IMG   - a mounted .IMG's run table (SPEC.md 18.101), 1KB a mount
;
; MEM_K_OVL used to be a row, and was the one kernel tag with a lifetime
; SHORTER than the session - a heap page open early enough could catch it. The
; boot overlay rides stage 2's blob now (SPEC.md 2.9.6), which is not a claim,
; so there is no moment of any boot at which it is on this page.
;
; NOTHING MAY GO BETWEEN THESE `dw` LINES: tests/unit/t_ktags.py reads the table
; out of this file with a regex over consecutive rows, and a comment inside it
; ends the capture early - which drops every row below it from the gate without
; failing anything.
tm_ktab:
    dw MEM_P_MSAVE, 'MenuSav'
    dw MEM_K_DRV,   'DrvImg', 0
    dw MEM_K_COPY,  'CopyBuf'
    dw MEM_K_ASC,   'Assoc', 0
    dw MEM_K_CLIP,  'Clipbrd'
    dw MEM_K_MOD,   'ModImg', 0
    dw MEM_K_CLONE, 'Cloner', 0
    dw MEM_K_BAND,  'BandBuf'
    dw MEM_K_HIB,   'Resume', 0
    dw MEM_K_CMPR,  'Compress'
    dw MEM_K_FDLG,  'FileDlg'
    dw MEM_K_FONT,  'Font', 0, 0
    dw MEM_K_SC,    'Shortcut'
    dw MEM_K_IMG,   'Disk img'
    dw MEM_P_DIRW,  'DirRead'
    dw MEM_P_RAHB,  'RdLand', 0
    dw MEM_P_FLIST, 'FileClp'
    dw MEM_P_ICO,   'Icons', 0
    dw 0
TMK_REC     equ 10              ; owner word + an eight-byte name
%if ($ - tm_ktab - 2) % TMK_REC
  %error "taskmgr: a tm_ktab record is no longer TMK_REC bytes (pad its name to 8)"
%endif

; An unpurgeable claim's TIER is 'HELD' (tm_tiers' fifth record, which is
; tm_s_hheld), and NOT a dash. The tiers are what LOSING a block costs, and a
; dash in that column read as a rank BELOW 'TRIV' - the opposite of the
; truth, since an ordinary claim is the only rank that outranks every cache
; (MEM_LVL_TOP, SPEC.md 50.6.4). 'HELD' says the thing the scale cannot: this
; one is not a cache at all and the kernel cannot take it back.
%endif

%ifdef TMF_MEM
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
; is framed in solid black by tm_map_ram and this fills the inside, which
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

%endif

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
    mov ax, TM_INT              ; SLEEP the interval (SPEC.md 28.7). It used to
    call OSAPI_TASK_SLEEP       ; SPIN it - { count += 1; yield } - because the
                                ; count WAS the load meter, and a spin counter
                                ; has no absolute scale so it had to be
                                ; calibrated against a rolling maximum. That
                                ; cost 34-38% of the machine for as long as
                                ; this page was open, and the page charged it
                                ; to itself, correctly, while the graph beside
                                ; it drew 0-2%. Both numbers were right and
                                ; they were measuring different things. The
                                ; idle task (SPEC.md 8.1.2) is the scale now,
                                ; so the spin can go

    mov bx, [tm_win]            ; close requested? (once per interval) - this
    call OSAPI_TASK_ALIVE       ; may never come back, and the lock is free
                                ; here, which rule 4 requires

    call tm_sample              ; no lock: computes load, shares, RAM

%ifdef TMF_MEM
    call tm_paintdue            ; does this interval owe a PAINT at all? On
    jc .interval                ; views 1 and 2 only every TM_SLOW of them
    call tm_quiet               ; ...and if it does, has anything those pages
    jc .interval                ; draw FROM moved? (SPEC.md 28.6.1) - no lock
                                ; held, and none taken if the answer is no
                                ;
                                ; THIS ORDER IS LOAD-BEARING and the other one
                                ; LOSES UPDATES. tm_quiet answers through
                                ; tm_elchk, which RECORDS the key it was asked
                                ; about - so asking on an interval that is not
                                ; going to paint anyway banks the change and
                                ; answers "unchanged" on the interval that
                                ; would have drawn it. The page then sits
                                ; stale until something else moves. Measured
                                ; the wrong way round first, and it does not
                                ; fail every time: an opener that touches the
                                ; table over several intervals still lands on
                                ; one that paints, so this reads as a page
                                ; that updates unreliably rather than as one
                                ; that is broken
                                ; does (SPEC.md 28.6). BEFORE the lock and not
                                ; inside it: what costs on those pages is the
                                ; walk, and the walk is what the lock is held
                                ; across - a skip that still took the lock
                                ; would save the work and keep the freeze
%else
                                ; ...and with ONE page there is neither
                                ; question to ask: the performance view has
                                ; always painted every interval and has always
                                ; moved when it did (SPEC.md 28.12)
%endif
    call OSAPI_GFX_LOCK
    mov bx, [tm_win]
    test word [es:bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14).
    jz .skip                    ; ES is KERNEL_SEG here: task_spawn seeds a
                                ; worker's ES with it and every TM_ES_DATA in
                                ; this file puts it back
                                ; **A WORKER MAY NOT CALL OSAPI_WM_RESIZE.**
                                ; This is where a deferred one lived for one
                                ; build and it HARD FROZE the machine: a resize
                                ; repaints, and a repaint runs every overlapped
                                ; window's W_PAINT through wm_pkgcall - on THIS
                                ; task, holding the gfx lock. A Disk window's
                                ; is fm_repaint, which can re-list a folder, so
                                ; the reproduction was "open a file manager,
                                ; then drag me across twice". The frame comes
                                ; from OSAPI_WM_PREFER now (SPEC.md 11.100.1),
                                ; which the KERNEL applies at the one moment it
                                ; is safe - and in ONE repaint rather than two
    call OSAPI_WM_CLIP_SET            ; a REGION, not wm_obscured's veto (SPEC.md
    jc .skip                    ; 11.3): one covered pixel used to skip the
                                ; whole refresh, so a Task Manager with a
                                ; corner under something stopped updating at
                                ; all. CF=1 here is "wholly invisible" or
                                ; "more than 16 fragments", which genuinely
                                ; do mean skip. gfx_unlock disarms it
    cmp byte [tm_abon], 0       ; the credits are up: the UI task owns the
    jne .skip                   ; content until a click takes them down, and
                                ; tm_abdismiss repaints it whole (SPEC.md
                                ; 20.5.1). Above tm_update, not below: the
                                ; incremental painter would letter rows
                                ; through the card
    call tm_update              ; incremental redraw, lock held
%ifdef TMF_MEM
    mov ax, [tm_qkey]           ; THE PAINT HAPPENED, so record what tm_quiet
    mov [tm_elck + 2*TMC_QUIET], ax     ; asked about (SPEC.md 28.6.1). Here
                                ; and not in tm_quiet, which only PEEKS: it is
                                ; asked with no lock and the paint can still be
                                ; refused two lines above, and recording a
                                ; change on an interval that drew nothing
                                ; leaves the page stale until something else
                                ; moves. Nothing recorded it at all between
                                ; 28.11 and here, so the answer was always
                                ; `changed`: the lock was taken and the cache
                                ; dropped (11.96.18) on every TM_SLOW interval,
                                ; whatever tm_quiet computed
%endif
.skip:
    call OSAPI_GFX_UNLOCK
    jmp .interval

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_quiet - has anything the two quiet pages DRAW FROM moved? (SPEC.md 28.6.1)
; in:  [tm_view]; the worker calls this with NO lock held
; out: CF = 1 nothing moved, so skip the paint; CF = 0 paint it
; clobbers: nothing else (flags)
;
; SPEC.md 28.6 slowed those pages to a quarter and left 51 ms on the table:
; the walk still runs, still concludes nothing changed, and still holds the
; gfx lock while it does. This is that walk's question asked the cheap way -
; three hashes over 334 bytes against one key - and asked BEFORE the lock,
; which is the whole point: a skip that took the lock would save the cycles
; and keep the pointer frozen.
;
; IT IS SAFE WITHOUT THE LOCK BECAUSE THE SNAPSHOT ALWAYS WAS. The claim
; table's API takes no cli and says why (SPEC.md 20.9): a claim is published
; by a single word store, so a record is there or it is not, and the gfx lock
; never guarded it. A torn read hashes to neither the before nor the after and
; so forces a paint, which is the safe direction - the same direction
; tm_elchk's collision risk already runs in.
;
; The three spans are the whole of what those pages read: the claim table
; (every row and every caption on the heap page, and the memory map's bands),
; [tm_kb] (the RAM, HEAP and XMS figures), and the instance block (the
; headings, the names' presence, the sizes and the per-instance heap). All
; three are refreshed by tm_sample or by this routine, both outside the lock.
;
; TWO HOLES IN THE KEY, AND THEY ARE THE SAME HOLE (SPEC.md 28.8.1). OUR OWN
; raise cache is left out of the claim span, and SK_CLAIM/SK_KCLAIM are left
; out of the KB span because they move with it. This window is the only one in
; the tree whose content is a report of the machine's memory, so it is the only
; one whose cache it CAUSES by looking: the cache appears, the page has changed,
; the page paints, and wm_clip_set drops the cache to let it (SPEC.md 11.96.18).
; Measured before this: a drag that partially covered the memory page banked
; 33KB, binned it one interval later, and cost 527 cells - about 505 ms on a
; 4.77 MHz 8088 - to come back, every time.
;
; It is NOT a mask on the range. Another window's raise cache is real memory
; arriving: it persists while that window stays covered, it does not go away
; because this one came to the front, and the page wakes for it exactly as it
; wakes for any other claim. Only the self-reference is cut, which is what
; 28.8's loop is made of.
;
; And it costs no arithmetic. Dropping the two totals from the key rather than
; subtracting our own paragraphs out of them is not a shortcut: both are
; derived from the table already hashed, so every real claim is still caught by
; the record it occupies - while a paragraphs-to-KB correction ROUNDS, and a
; rounding key is a key that sometimes lies in both directions.
; -----------------------------------------------------------------------------
tm_quiet:
%ifdef TMF_MEM
    cmp byte [tm_view], 0
    je .paint                   ; the performance view moves every interval by
    push ax                     ; construction: a percentage, a graph column
    push bx                     ; and a spin count that are never twice the
    push cx                     ; same
    push dx                     ; OSAPI_WM_OWNSEG answers in it, and this
    push si                     ; routine's contract is "clobbers: flags"
    push di
    push es
    TM_ES_DATA
    mov di, tm_claims
    TM_CLAIM_SNAPSHOT           ; ...into the buffer the paint would use anyway
    pop es

    ; RECORD BY RECORD rather than one span, so OUR OWN raise cache can be
    ; left out of the key (SPEC.md 28.8.1). Nothing is written to the
    ; snapshot: the paint below draws every claim including that one, and
    ; truthfully. What is skipped is the REASON to paint.
    mov si, tm_claims
    xor ax, ax                  ; AX chains through all three spans
.rec:
    cmp word [si+CLS_SEG], 0    ; A FREE RECORD WEIGHS NOTHING, and that is
    je .zero                    ; half of what makes this work: its CLS_PARA
                                ; and CLS_OWN are a previous tenant's leftovers,
                                ; the page draws no band for it, and they are
                                ; what our own record BECOMES the moment the
                                ; cache is freed
    mov dx, [si+CLS_OWN]        ; a raise cache at all? Its tag is a base plus
    sub dx, MEM_P_WSAVE         ; the window's SLOT (SPEC.md 11.96.3), so this
    cmp dx, MEM_P_WSAVE_N       ; is a range and not an equality - unsigned, so
    jae .hash                   ; a lower owner wraps high and fails
    push ax
    mov ax, dx                  ; AL = the slot the tag carries...
    call OSAPI_WM_OWNSEG        ; ...and 11.96.3.1 turns it back into a segment
    mov di, ds                  ; (CF and DX survive both of these)
    pop ax
    jc .hash                    ; no live window in that slot: not ours
    cmp dx, di
    jne .hash                   ; SOMEBODY ELSE'S, and that one is real memory
                                ; arriving - it persists while its window stays
                                ; covered, it does not go away because we came
                                ; to the front, and this page should wake for it
.zero:
    mov cl, CLS_RECSZ           ; ...OURS, or free: weigh it as SIX ZERO BYTES
    rol ax, cl                  ; instead of stepping past it. tm_sumb is
    add si, CLS_RECSZ           ; rol/add PER BYTE and so position-sensitive -
    jmp short .next             ; skipping a record shifts every byte after it
                                ; and changes the very key this is trying to
                                ; hold still. Six zero bytes IS `rol ax, 6` to
                                ; the chain, and weighing a free record the
                                ; same way makes the key blind to WHICH record
                                ; the cache lands in as well as to whether it
                                ; is there at all
.hash:
    mov cx, CLS_RECSZ
    call tm_sumb                ; AX chains; SI advances past the record
.next:
    cmp si, tm_claims + CLAIM_SNAPSHOT_SIZE
    jb .rec
                                ; SI = tm_kb now: it follows the claim table
    mov cx, SK_CLAIM            ; ...and the two TOTALS that move with our own
                                ; cache are SKIPPED rather than corrected: both
    call tm_sumb                ; are derived from the table just hashed, so a
    add si, 4                   ; real claim is still caught by the record it
    mov cx, SYSKB_SIZE - (SK_KCLAIM + 2)    ; occupies, and the arithmetic to
    call tm_sumb                ; take our paragraphs back out of a KB figure
                                ; - which rounds - is not owed at all
    mov si, tm_ist
    mov cx, TM_IHASH
    call tm_sumb
    cmp byte [tm_hion], 0       ; ...AND THE POOL ABOVE 1MB, which the list
    je .nox                     ; draws a block a row of since KERN-MEGA-
    mov si, tm_xwn              ; PLAN 14.70.2: a driver or a program taking
    mov cx, 2 + TM_XWN * XMW_SIZE   ; a block there moved nothing hashed
    call tm_sumb                ; above, so the page sat unchanged over it.
.nox:                           ; tm_hikb refreshes it every sample, outside
                                ; the lock like the rest; a machine with no
                                ; pool (tm_hion 0) has only zeroes there, and
                                ; does not pay 386 bytes of hash for them
    mov bx, tm_elck + 2*TMC_QUIET
    call tm_qpeek               ; CF = 1 unchanged, which IS the skip - and the
    mov [tm_qkey], ax           ; AX is the key AFTER tm_qpeek's never-zero
                                ; rule, which is the value the store below has
                                ; to match; a mov writes no flags
    jmp tm_pop_dsdcba
.paint:                         ; ...and with no quiet page left this is the
%endif                          ; whole routine: there is nothing it could
    clc                         ; answer but "paint"
    ret

; -----------------------------------------------------------------------------
; tm_paintdue - does this interval owe a paint? (SPEC.md 28.6)
; in:  [tm_view]
; out: CF = 0 paint it, CF = 1 skip it; all registers preserved
;
; The performance view is the live one - a graph column, a percentage and a
; scheduler mode that move every interval by construction - so it is never
; slowed. The other two are a list of claims and a map of memory: on a machine
; that is sitting there they are the SAME PIXELS twice a second, and the
; measurement beside TM_SLOW says they are drawn zero times to prove it.
;
; A view switch and a scroll do not come through here at all: tm_click
; repaints inline and tm_hscroll calls tm_upd_heap directly, so interaction
; stays immediate and only the unattended refresh waits.
; -----------------------------------------------------------------------------
tm_paintdue:
%ifdef TMF_MEM
    cmp byte [tm_view], 0
    je .now                     ; the performance view keeps TM_INT
    inc byte [tm_slow]
    cmp byte [tm_slow], TM_SLOW
    jb .not_yet
.now:
    mov byte [tm_slow], 0
%endif
    clc
    ret
.not_yet:
    stc
    ret

%endif

; -----------------------------------------------------------------------------
; tm_sample - one measurement pass: load gauge, history push, per-instance CPU
;             shares, RAM usage. Touches only module state; no lock needed.
; in:  nothing - it reads the kernel's snapshot, not a counter of its own
; out: tm_load/tm_hist/tm_pos/tm_lastcol/tm_pct/tm_state/tm_idle/tm_self/
;      tm_usedk/
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

    call tm_getkb               ; the heap's totals, for the RAM figure below

    ; --- snapshot scheduler + instance state (one atomic block, 8.1/28) ------
    ; Task cycles and T_STATE, then the whole instance table: I_STATE, I_TASK,
    ; I_SIZE, I_CYC and the 16 I_NAME bytes of every slot. TM_SYS_SNAPSHOT does
    ; the lot in one cli window - task_exit frees a record atomically with its
    ; task slot (SPEC.md 8), so nothing read here can be torn against itself,
    ; and that window is the whole reason the API cell is a table snapshot
    ; rather than a getter per record (SPEC.md 20.9).
    ;
    ; The TASK side is read in place: SS_TSTATE and SS_TCYC are already
    ; per-slot arrays in exactly the shape this module wants, so tm_state is
    ; an alias of the buffer and not a copy of it, and so are tm_self and
    ; tm_coop. The INSTANCE side is unpacked into per-field arrays, because
    ; every read site indexes by slot and the record stride is 32 - except
    ; the names and the I_CYC counters, which are read where they lie: a name
    ; is reached through one address computation per row (tm_iname) and a
    ; counter only by the diff below, so a copy of either bought nothing and
    ; cost 240 bytes of bss and a 16-byte copy loop per record per sample.
    TM_ES_DATA
    mov di, tm_snapshot
    TM_SYS_SNAPSHOT

    ; --- per-task diffs, and WHICH slot is the idle task on the way --------
    ; The snapshot names the idle task with a state of its own (SPEC.md 28.7).
    ; It was a separate pass over the same bytes; this loop already reads
    ; every one of them. The last slot answering 3 wins, as it did.
    ;
    ; ES = DS from here to the end (TM_ES_DATA above), which is what lets
    ; tm_dif bank a counter with two MOVSWs - and advance SI and DI by the
    ; record's four bytes as it does.
    mov byte [tm_idle], 0xFF
    mov si, tm_snapshot + SS_TCYC
    mov di, tm_told
    xor bx, bx
.tdiff:
    mov cl, [tm_pstate+bx]
    mov ch, [tm_state+bx]       ; = the snapshot's own T_STATE bytes
    mov [tm_pstate+bx], ch      ; this sample's state becomes the previous
    cmp ch, 3
    jne .tnotidle
    mov [tm_idle], bl
.tnotidle:
    call tm_dif                 ; ...and SI, DI step on by 4
    mov [di+tm_tdif-tm_told-4], ax
    mov [di+tm_tdif-tm_told-2], dx
    inc bx
    cmp bx, MAX_TASKS
    jb .tdiff

    ; --- the instance table, unpacked, and its diffs (same two rules;
    ; inst_alloc zeroes I_CYC). SI walks the records AT their I_CYC field, so
    ; tm_dif reads [si] and the other fields are small negative displacements.
    mov si, tm_snapshot + SS_INST + SSI_CYC
    mov di, tm_iold
    xor bx, bx                  ; BX = instance index
.isnap:
    mov ch, [si+SSI_STATE-SSI_CYC]
    mov [tm_ist+bx], ch
    mov cl, [tm_pist+bx]        ; the previous state, for the appeared rule...
    mov [tm_pist+bx], ch        ; ...and this one becomes it
    mov al, [si+SSI_TASK-SSI_CYC]
    mov [tm_itsk+bx], al
    mov al, [si+SSI_KIND-SSI_CYC]   ; bit 7 = package: the memory view groups
    mov [tm_iknd+bx], al        ; on it, and bills claims by it (SPEC.md 28/50)
    push di
    mov di, bx
    shl di, 1                   ; DI = index*2
    mov ax, [si+SSI_SIZE-SSI_CYC]
    mov [tm_isz+di], ax
    mov ax, [si+SSI_SEG-SSI_CYC]    ; region base for the memory view's map
    mov [tm_ispt+di], ax        ; (meaningless for built-ins: I_SIZE 0
                                ; filters them out, SPEC.md 28)
    mov ax, [si+SSI_KB-SSI_CYC] ; ...and what it holds off the heap, which the
    mov [tm_ikb+di], ax         ; kernel worked out from the owner word rule
    pop di
    call tm_dif                 ; DX:AX = its callback cycles this interval,
                                ; SI and DI on by 4
    ; ...and straight into its ROW: row 1+i is instance i's callback time
    ; plus, if it owns a task, that task's slice (the task diffs above are
    ; already done). Cycles of a task whose instance died this interval belong
    ; to no row and simply drop out of the total.
    or ch, ch                   ; a free record's I_TASK byte is stale
    jz .rnotask
    push bx
    mov bl, [tm_itsk+bx]
    cmp bl, MAX_TASKS           ; catches 0xFF (task-less) and any garbage
    jae .rnopop
    xor bh, bh
    shl bx, 1
    shl bx, 1
    add ax, [tm_tdif+bx]
    adc dx, [tm_tdif+bx+2]
.rnopop:
    pop bx
.rnotask:
    mov [di+tm_rcyc-tm_iold], ax
    mov [di+tm_rcyc-tm_iold+2], dx
    add si, SSI_RECSZ - 4       ; (tm_dif has already stepped the 4)
    inc bx
    cmp bx, INST_MAX
    jb .isnap

    ; --- row 0, the one row the loop above does not fold -------------------
    ; Row 0 is the UI task's remainder (callbacks have already been debited
    ; off it) - AND THE IDLE TASK, into the same row (SPEC.md 28.7). It owns
    ; no instance, so it belongs to no application, and a machine that is 97%
    ; idle reads as a System row of 97% - which is what the older Windows
    ; Task Manager shows and what a reader expects. Kept in tm_idlec as well
    ; because it is the load meter's numerator, and the meter's denominator
    ; has to be this same total or the two can disagree again.
    xor ax, ax
    cwd
    mov bl, [tm_idle]
    cmp bl, MAX_TASKS           ; 0xFF when no slot answered to state 3
    jae .noidle
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [tm_tdif+bx]
    mov dx, [tm_tdif+bx+2]
.noidle:
    mov [tm_idlec], ax
    mov [tm_idlec+2], dx
    add ax, [tm_tdif]
    adc dx, [tm_tdif+2]
    mov [tm_rcyc], ax
    mov [tm_rcyc+2], dx

    ; total = sum of the rows, so the shares of a fully accounted system add
    ; up to 100. In DI:CX rather than in memory: it is summed, normalised and
    ; read here and nowhere else.
    xor cx, cx
    xor di, di
    xor bx, bx
.tsum:
    add cx, [tm_rcyc+bx]
    adc di, [tm_rcyc+bx+2]
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .tsum

    ; normalize: how many right shifts bring the total into 16 bits. Every
    ; row and the meter's numerator take the SAME count (tm_shn) - or the
    ; shares are ratios of two different scales. It used to shift all
    ; thirteen rows in MEMORY once per bit, four bits on any machine a 9-tick
    ; interval holds ~590,000 PIT cycles of; the count is shifted in
    ; registers per row instead, which is fewer cycles and no second loop.
    xor si, si
.tnorm:
    or di, di
    jz .tnormed
    shr di, 1
    rcr cx, 1
    inc si
    jmp short .tnorm
.tnormed:

    ; share_i = row_i*100/total; total 0 -> all 0; clamp keeps DIV safe
    xor bx, bx
    mov di, tm_pct
.pct:
    mov ax, [tm_rcyc+bx]
    mov dx, [tm_rcyc+bx+2]
    call tm_shn
    stosb                       ; ES = DS (TM_ES_DATA)
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .pct

    ; --- the LOAD METER: 100 - 100*idle/total (SPEC.md 28.7) ----------------
    ; Off the same interval and the same total as the rows above, which is the
    ; whole point: the meter and the process list cannot disagree any more.
    ; They did, and both were right - the page charged its own spinning worker
    ; 34-38% of CPU TIME while the graph drew 0-2% of SPIN COUNT.
    ;
    ; No total (nothing accounted yet, or the very first sample) reads 0 rather
    ; than 100: an unknown load is better drawn as quiet than as a machine
    ; apparently pinned.
    xor ax, ax
    jcxz .loadok
    mov ax, [tm_idlec]
    mov dx, [tm_idlec+2]
    call tm_shn                 ; AX = idle%...
    neg ax
    add ax, 100                 ; ...and the load is the rest
.loadok:
    mov [tm_load], ax

    ; --- history push: v = load*TM_GH/100, sweep index advances -------------
    mov cl, TM_GH               ; byte arithmetic: load <= 100, so load*TM_GH
    mul cl                      ; is 4,000 at most and the quotient fits AL
    mov cl, 100
    div cl                      ; AL = 0..TM_GH
    mov bx, [tm_pos]
    mov [tm_hist+bx], al
    mov [tm_lastcol], bx
    inc bx
    cmp bx, TM_GW
    jb .posok
    xor bx, bx
.posok:
    mov [tm_pos], bx

    ; --- RAM: the kernel's footprint, plus everything claimed (SPEC.md 28) --
    ; TWO terms, not three. There used to be a third - a sum of every resident
    ; package REGION off the instance snapshot - because a region came out of
    ; a pool that the claim heap knew nothing about. Since SPEC.md 20.1 a
    ; region IS a heap claim, so the kernel's mem_sum_kb already counts every
    ; one of them and the sum counted them a second time.
    ;
    ; That is what let this figure exceed the machine: with a package loaded,
    ; used was over by its region, and a 20KB menu save-under landing on top
    ; of an already-inflated figure could read as more RAM than the machine
    ; has. It cannot now - claims live in the heap, the heap is what is left
    ; above the kernel, so kernel + claims is bounded by the total.
    ;
    ; SK_CLAIM leaves the PURGEABLES out (SPEC.md 50.6.3/28.4.1), which is
    ; what makes this figure the one the rows underneath add up to. It did
    ; not, and on the field machine that read `RAM 195/384K` over rows
    ; accounting for 132 - the missing 63 being the DirRead window, which
    ; belongs to no instance and so appears in no column. The map below still
    ; DRAWS it: a map is about addresses, and a cache occupies a real one.
    ;
    ; AND THE LOW 1.5K: the IVT, the BIOS data area and the page at 0500, which
    ; are below KERNEL_SEG and so neither the kernel's nor the heap's. They
    ; were in no figure on this page, so RAM's free half came out a KB or two
    ; over the heap's free half on every machine. So the fixed half is TAKEN
    ; rather than built: everything that is not the heap is IVT+BIOS + System,
    ; and that is the total less the heap's size (SPEC.md 28.4.7)
    mov ax, [tm_totkb]          ; int 12h's total, less the claim heap: the
    sub ax, [tm_kb+SK_HEAP]     ; WHOLE kernel (SPEC.md 2) - image, scratch,
                                ; FAT snapshot, disk buffers and every task
                                ; stack, one contiguous span with no growth
                                ; room in it to bill - and the IVT+BDA under it
    add ax, [tm_kb+SK_CLAIM]    ; + everything claimed out of the heap
                                ; (SPEC.md 50): the clipboard while it is
                                ; armed, the menu save-under while a menu is
                                ; down, each open Disk window's listing
                                ; cache, and every package's region and data
                                ; - live, not a constant
    mov [tm_usedk], ax          ; ...and what is left really is free: the
                                ; unclaimed heap is the only thing not
                                ; counted here, and it is handed out on demand
    mov cx, [tm_totkb]          ; barw = usedK*TM_GW/totalK (all KB)
%ifdef TMF_MEM
    cmp byte [tm_hion], 0       ; RAM above 1MB: RAM is both halves, and the
    je .convbar                 ; bar is drawn against the whole machine
    call tm_hikb
    add ax, [tm_hiused]
    add cx, [tm_hitot]
.convbar:
%endif
    call tm_scale
    mov [tm_barw], ax
    jmp tm_pop_edsdcba

; -----------------------------------------------------------------------------
; tm_dif - one 32-bit cycle counter's interval delta, and the counter banked
; in:  SI -> the counter now, DI -> its previous value (replaced by the new
;      one), CL = the slot's previous state, CH = its state now
; out: DX:AX = new - old, or 0 if that went negative (the counter regressed:
;      a slot reused mid-interval, a rare PIT stamp race, or a task_debit
;      that reached back into the previous interval - a wrapped "huge" diff
;      would clamp rows to 100%) or the slot APPEARED (free last sample, used
;      now: its counter was reset at spawn, so the diff is meaningless);
;      SI and DI each advanced by 4 (the counter's MOVSW bank)
; in:  ES = DS
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_dif:
    mov ax, [si]
    mov dx, [si+2]
    sub ax, [di]                ; wrap-safe 32-bit difference
    sbb dx, [di+2]
    movsw                       ; bank the new value; MOVSW leaves the flags
    movsw                       ; the sbb set (ES = DS: tm_sample's TM_ES_DATA)
    js .zero
    or cl, cl                   ; appeared-slot rule (SPEC.md 28)
    jnz .keep
    or ch, ch
    jz .keep
.zero:
    xor ax, ax
    cwd
.keep:
    ret

; tm_shn - tm_share of DX:AX shifted right SI places first: tm_sample's
;          normalisation, applied per row (SI = the total's shift count)
; out: AX; clobbers DX
tm_shn:
    push si
.sh:
    dec si
    js .go
    shr dx, 1
    rcr ax, 1
    jmp short .sh
.go:
    pop si
                                ; ...and on into tm_share
; tm_share - DX:AX's share of CX in percent: 0..100, clamped (a pathological
;            part over the total reads 100), and 0 when CX is 0. CX is the
;            normalised total, so the divide below cannot overflow
; out: AX; clobbers DX
tm_share:
    jcxz .zero
    or dx, dx
    jnz .full
    cmp ax, cx
    ja .full
    mov dx, 100
    mul dx                      ; DX:AX = part*100
    div cx                      ; AX <= 100: no overflow possible
    ret
.full:
    mov ax, 100
    ret
.zero:
    xor ax, ax
    ret

; tm_scale - AX of CX as a bar width: AX*TM_GW/CX, clamped to TM_GW, and 0
;            when CX is 0
; out: AX; clobbers nothing else
tm_scale:
    jcxz .zero
    push dx
    mov dx, TM_GW
    mul dx                      ; DX:AX = part * TM_GW
    div cx
    pop dx
    cmp ax, TM_GW
    jbe .out
    mov ax, TM_GW
.out:
    ret
.zero:
    xor ax, ax
    ret

; tm_getkb - tm_kb, refreshed from OSAPI_SYS_KB; every register preserved
tm_getkb:
    push di
    push es
    TM_ES_DATA
    mov di, tm_kb
    TM_SYS_KB
    pop es
    pop di
    ret

; tm_iname - SI = instance BX's name, where the snapshot holds it
%if SSI_RECSZ != 32
  %error "taskmgr: tm_iname's shift assumes a 32-byte instance record"
%endif
tm_iname:
    push cx
    mov si, bx
    mov cl, 5
    shl si, cl
    add si, tm_snapshot + SS_INST + SSI_NAME
    pop cx
    ret

; =============================================================================
; Drawing. All routines below run with the gfx lock held by the caller and
; draw self-backgrounding elements (each white-fills its own rect or paints
; both segments), so no separate content clear is needed. [tm_cx]/[tm_cy]
; hold the content origin, cached by tm_paint / tm_update.
; =============================================================================

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_promise - the raise cache, and the promise that is a REPAIR
; in:  [tm_win], [tm_view]; THE GFX LOCK HELD BY THE CALLER
; out: nothing (every register, ES and the flags preserved)
;
; SPEC.md 28.11, and it is 28.8 answered rather than restated. That section
; measured why this window could not hold a cache: the cache is a purgeable
; heap claim, these two pages are the tree's only reporters of purgeable
; claims, so taking one changes what the page must show and showing it frees
; the cache. The loop existed only because a promise had to be WITHDRAWN when
; the content moved. It does not have to be, if the window repairs itself on
; the way back.
;
; THE BAND IS THE DOORWAY AND IT COSTS NO KERNEL BYTES. SPEC.md 11.96.11's
; contract is "the kernel banks the band, and wm_damage tells you you owe the
; rest"; a band covering the WHOLE content is the degenerate case of it -
; everything banked, nothing owed - and wm_draw_win already routes a banded
; restore through W_PAINT (its `wm_su_bget / jnc .paint`) where a plain one
; skips it. wm_su_orect then insets x1 past x2 and wm_damage answers the
; EMPTY rect 11.90.2 documents as legal. So the app is called, told it owes
; nothing, and spends its own debt.
;
; Named on every paint: OSAPI_WM_BAND drops nothing when the extent has not
; changed, so this is free after the first and it follows a resize for
; nothing. The extent is a BYTE (11.96.11.1), so a content wider than 255
; refuses - and a refusal is a normal path: no band, no promise, and this
; window behaves exactly as it did before.
;
; PER PAGE, like the Control Panel's (31.12): the performance view is the load
; meter and moves every TM_INT by construction, so it is never promised.
;
; NO DEPTH CLAIM: wm_su_1bpp refuses a banded window, so this is four planes -
; 32,874 bytes for a 232x265 content, inside wm_su_kb's 64,512 ceiling.
;
; ES IS PRESERVED: OSAPI_WM_BAND and OSAPI_WM_SAVEU are X stubs and leave the
; CALLER's segment in ES, and every [es:bx+W_*] read in this file needs
; KERNEL_SEG (TM_ES_DATA exists for exactly that).
; -----------------------------------------------------------------------------
tm_promise:
    pushf
    push ax
    push bx
    push cx
    push dx                     ; OSAPI_WM_GEOM answers the content SIZE, so DX
    push es                     ; is an output of it whether or not we read it
    mov bx, [tm_win]
    or bx, bx                   ; before wm_create, and after a refused one
    jz .out
    cmp byte [tm_view], 0
    je .no                      ; the live page: never
    call OSAPI_WM_GEOM          ; CX = content width, CF = not visible
    jc .no
    xor al, al                  ; edge 0 = left, the whole content
    call OSAPI_WM_BAND
    jc .no                      ; wider than 255, or the kernel declined
    mov al, 1
    call OSAPI_WM_SAVEU         ; ...and the promise is 28.11's, not 11.96.1's:
    jmp short .out              ; "I will make the restored pixels right"
.no:
    xor al, al
    call OSAPI_WM_SAVEU         ; withdraw, and wm_saveu drops the cache
    xor al, al
    xor cx, cx
    call OSAPI_WM_BAND          ; ...and retire the band with it, or a later
.out:                           ; cache is laid out for one nobody claims
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

%endif

; -----------------------------------------------------------------------------
; tm_paint - W_PAINT: redraw the active view, WHOLE or as a repair
; in:  SI = window ptr (gfx lock held by caller; no lock, no visibility
;      check in here - SPEC.md 28)
; out: nothing
; clobbers: nothing (flags only)
;
; SPEC.md 28.11. tm_clear_owed asks OSAPI_WM_DAMAGE and answers CF, so the
; question is asked once: CF = 1 means the restore put the WHOLE content back
; and this paint owes nothing but its own debt - the elements whose keys moved
; while it was covered, and nothing else. tm_qpeek is what leaves those keys
; standing to be read.
; -----------------------------------------------------------------------------
tm_paint:
    call tm_hire                ; the first paint is the earliest point at
                                ; which our instance exists to attach one to
    call tm_clear_owed          ; the GROUND is ours now (SPEC.md 28.10) - and
    jc .repair                  ; the rect it filled is in [tm_dmg], which
                                ; every checked draw below asks (28.10.2): a
                                ; chunk, a caption or a map the damage never
                                ; touched keeps the pixels it has.
                                ;
                                ; THE SCROLL BAR IS THE ONE EXCEPTION and is
                                ; forced: it is the only element with no band
                                ; written down (tm_elchk_y says why), and a
                                ; strip the damage erased has to come back.
                                ; With no heap page there is no bar, and the
                                ; element it keyed is not in tm_elck at all
                                ; (SPEC.md 28.12)
%ifdef TMF_HEAP
    mov word [tm_elck + 2*TMC_HSB], 0
%endif
    call tm_draw_band
    jmp short .out
.repair:
    call tm_update
.out:
    call tm_dmg_none            ; the band is this paint's and no later one's
%ifdef TMF_MEM
    call tm_promise             ; ...and the promise, re-stated: free when
                                ; nothing changed, and it follows a resize for
                                ; the same nothing. Only a QUIET page ever
                                ; takes one (SPEC.md 28.11), so this arm is
                                ; the pages' and goes with them
%endif
    cmp byte [tm_abon], 0       ; ...and the About card LAST, over the rows it
    je .noab                    ; is opaque about (SPEC.md 20.5.1)
    push bx
    push si
    mov bx, si
    mov si, tm_ablines
    call os88ui_about_d         ; _d: this paint's region is already armed
    pop si
    pop bx
.noab:
    ret

; -----------------------------------------------------------------------------
; tm_clear_content - white-fill this window's whole content
; in:  SI = window ptr; the gfx lock held
; out: nothing; preserves all registers
;
; SPEC.md 28.10. It was inline in tm_click's view swap and is a routine now
; because the kernel stopped doing it: this window carries WF_OWNBG, so
; wm_draw_win skips its white fill and W_PAINT owes the ground as well as the
; ink (SPEC.md 11.90.1).
;
; THE FAR CORNER COMES OFF THE RECORD, not from a pair of constants: this is
; the only thing that erases the outgoing view, and a short one leaves its
; tail rows lettered under the incoming one (they were 198x245 against a
; content that had grown to 231x282). TWO, not one - y+h-1 and x+w-1 are the
; frame's own bottom and right BORDERS, and a fill that reached them left the
; window open-sided until something repainted the frame.
; -----------------------------------------------------------------------------
tm_clear_content:
    push ax
    push bx
    push cx
    push dx
    TM_INKX CWHITE              ; first: AX is not ours yet
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov cx, [es:bx+W_X]
    add cx, [es:bx+W_W]
    dec cx
    dec cx
    push dx
    mov dx, [es:bx+W_Y]
    add dx, [es:bx+W_H]
    dec dx
    dec dx
    pop bx                      ; BX = content top
    call OSAPI_GFX_FILL
    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_clear_owed - white-fill only the part of the content this paint OWES
; in:  SI = window ptr; the gfx lock held
; out: CF = 1 the rect was EMPTY and nothing was filled; preserves all registers
;
; SPEC.md 28.10.1. OSAPI_WM_DAMAGE answers CF = 1 "the whole content" or CF = 0
; and a rect, absolute and inclusive, which may be EMPTY. A window redrawn
; because something ABOVE it moved owes only the strip that moved, and the
; rest of its pixels are on the glass and correct - so filling the whole
; content is a white hole across a window whose W_PAINT is ~450 ms on the
; target (PERFORMANCE.md, the heap page's list repaint).
;
; The DRAWING is still whole, and that is deliberate rather than unfinished.
; A repaint is priced by the primitive calls it makes and not by the pixels
; they cover, so skipping elements outside the rect is what would save the
; 450 ms - and for the commonest damage here, a narrow VERTICAL strip where a
; window beside this one moved, every row intersects it and nothing can be
; skipped. The fill is the half that pays either way. SPEC.md 28.10.2 records
; what element-level skipping would take and when it would pay.
;
; It cannot paint anything wrong: wm_dmg_wins redraws marked windows WHOLE and
; bottom to top, so a window drawing outside its damage is covered by the ones
; above it in the same pass (SPEC.md 11.90.2 says so in as many words - "a
; caller that never tests for it cannot paint anything wrong, only waste the
; answer").
;
; THE EMPTY ANSWER IS PASSED ON IN CF, and that is why tm_paint asks nothing
; itself: an empty rect is SPEC.md 28.11's whole-content band coming back off
; the raise cache, which is the one case where the ink is owed and the ground
; is not. A pop writes no flag, so the answer survives the four below it.
; -----------------------------------------------------------------------------
tm_clear_owed:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_DAMAGE        ; AX/BX/CX/DX = the rect we owe
    jc .whole                   ; CF = 1: all of it
    cmp ax, cx
    jg .none                    ; empty (x1 > x2): we owe NOTHING, and the
    cmp bx, dx                  ; kernel promises a primitive would treat it
    jg .none                    ; as a no-op anyway
    call tm_dmg_set             ; ...AND THE ROWS READ THE SAME RECT: what was
    TM_INK CWHITE               ; filled is exactly what has to be lettered
    call OSAPI_GFX_FILL         ; again (SPEC.md 28.10.2)
    jmp short .out
.whole:
    call tm_dmg_all
    call tm_clear_content
.out:
    clc
    jmp short .pop
.none:
    call tm_dmg_none
    stc
.pop:
    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_dmg_* - WHICH ROWS THIS PAINT OWES, as a rect the chunk loop can ask
;
; SPEC.md 28.10.2. tm_clear_owed has always filled only the part of the
; content wm_damage named; the DRAWING was still whole, and on the window
; whose W_PAINT is the densest in the tree that is ~450 ms of glyphs for a
; strip a person dragged a window off. The rows are almost all of it - a heap
; page is 19 rows of 25 characters, and a glyph cell is ~900 us on the target.
;
; The structure to ask with was already there. SPEC.md 28.2 made a row FIVE
; independently-checked chunks so that a percentage ticking over would not
; redraw the name beside it, and every one of them comes through tm_elchk. So
; the rule the chunk loop wants is one word wider than the one it had:
;
;     draw this chunk if its key CHANGED, or if the damage took its pixels
;
; and the second half is this rect. It is absolute, like [tm_rowy] and
; [tm_penx] and like wm_damage's own answer, so no coordinate is converted
; anywhere; two columns come out right for free, because the test reads the
; live pen rather than deriving x from a row index.
;
; BOTH AXES, and the x half is the one that pays for the common damage. A
; window moved off this one's right leaves a VERTICAL strip - every row
; intersects it, so a y band alone would force the lot - and the chunks are
; 40px each, so the strip names two or three of five. The y half is what a
; window uncovering the bottom, a dock repaint or a downward drag hits.
;
; tm_dmg_all is a repaint that owes everything and tm_dmg_none one that owes
; nothing: mirrors, sharing their four stores. `none` is what the WORKER's
; own update runs under, where the keys alone decide, and tm_paint restores
; it on the way out so no later interval inherits this paint's rect.
; -----------------------------------------------------------------------------
tm_dmg_set:                     ; AX/BX/CX/DX = x1/y1/x2/y2 (all preserved)
    mov [tm_dmg+0], ax
    mov [tm_dmg+2], bx
    mov [tm_dmg+4], cx
    mov [tm_dmg+6], dx
    ret

tm_dmg_all:
    push ax
    push bx
    xor ax, ax                  ; x1/y1 = 0, x2/y2 = 0xFFFF: everything hits
    mov bx, 0FFFFh
    jmp short tm_dmg_w
tm_dmg_none:
    push ax
    push bx
    mov ax, 0FFFFh              ; ...and the mirror, which nothing can
    xor bx, bx
tm_dmg_w:
    mov [tm_dmg+0], ax
    mov [tm_dmg+2], ax
    mov [tm_dmg+4], bx
    mov [tm_dmg+6], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_dmg_hit - does the chunk the row loop is on lie in this paint's damage?
; in:  [tm_rowy], [tm_penx], [tm_ci]
; out: CF = 0 it does (draw it), CF = 1 it does not (its pixels are still
;      there); all registers preserved
;
; The y test is first because it rejects a whole row in two compares, and the
; unsigned forms are deliberate: tm_dmg_all's 0xFFFF is a maximum only if
; nothing here reads it as -1.
; -----------------------------------------------------------------------------
tm_dmg_hit:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tm_rowy]
    cmp ax, [tm_dmg+6]
    ja .no                      ; the row starts below the damage
    add ax, 7                   ; a row band is one glyph tall
    cmp ax, [tm_dmg+2]
    jb .no                      ; ...or ends above it
    mov ax, [tm_ci]             ; --- x1, and a chunk's band is not always its
    mov cl, TM_CW               ; own text: 5 chunks of 40px, so AL is the
    mul cl                      ; whole of the multiply
    mov bx, ax                  ; BX = i * TM_CW, kept for x2
    add ax, [tm_penx]
    mov dx, ax                  ; DX = the chunk's pen x, kept for x2
    or bx, bx
    jnz .x1
    mov ax, [tm_rowx]           ; CHUNK 0 OWNS THE LEAD-IN, here as in the
    add ax, 6                   ; draw (tm_row_lead's own left edge). A damage
.x1:                            ; rect ending inside those pixels erased a
    cmp ax, [tm_dmg+4]          ; legend square, and this is the chunk that
    ja .no                      ; has to put it back - test from the pen and
                                ; it is erased until the row's key moves
    cmp bx, (TM_NCHUNK - 1) * TM_CW     ; --- x2
    jae .yes                    ; THE LAST CHUNK HAS NO RIGHT EDGE to test:
    add dx, TM_CW - 1           ; tm_rowr runs it on to the row's own, so it
    cmp dx, [tm_dmg+0]          ; owns the tail past the text and damage out
    jb .no                      ; there is still its to repair
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_elchk_y - tm_elchk, and the damage clause for an element with a BAND
; in:  AX = key, BX = the check word, CX/DX = the element's CONTENT-relative
;      y range, inclusive
; out: CF = 1 skip the draw, CF = 0 draw it; all registers preserved
;
; SPEC.md 28.10.2. The rows get their band from the chunk loop's own pen; the
; seven TMC_* elements have nothing to derive one from, so each site names its
; y range - which is a constant it already had, being the one it hands tm_lrun
; or tm_map_rect three instructions later.
;
; X IS NOT ASKED. Every one of these spans the pane, so a damage rect that
; crosses the band at all crosses the element, and the two compares would
; always answer the same. The rows are the opposite case and that is where the
; x half pays (tm_dmg_hit).
;
; tm_hbar is deliberately NOT on this: its own prologue does not save DX, and
; a scroll bar is a full-height strip that a damage rect crossing this window
; at all is going to cross. Sixteen drawing calls, kept.
; -----------------------------------------------------------------------------
tm_elchk_y:
    call tm_elchk
    jnc .out                    ; changed: drawn whatever the damage says
    call tm_dmg_yhit            ; ...otherwise the band decides
.out:
    ret

; -----------------------------------------------------------------------------
; tm_dmg_yhit - does this paint's damage cross a CONTENT-RELATIVE y band?
; in:  CX/DX = the band, inclusive, content-relative
; out: CF = 0 it does (draw it), CF = 1 it does not; all registers preserved
;
; SPEC.md 28.10.3 lifted this out of tm_elchk_y, which is the same question
; with a key in front of it. The graph has no key - a new sample lands in it
; every interval and the periodic path draws the two columns that moved - so
; what it wants is this half on its own, and the bar's FRAME wants it too.
;
; X IS NOT ASKED, for tm_elchk_y's reason: every element that comes through
; here spans the pane. tm_graph asks about x itself, being the one element
; with 216 independently repairable columns in it.
; -----------------------------------------------------------------------------
tm_dmg_yhit:
    push ax
    mov ax, [tm_cy]
    add ax, dx
    cmp ax, [tm_dmg+2]
    jb .no                      ; the band ends above the damage
    mov ax, [tm_cy]
    add ax, cx
    cmp ax, [tm_dmg+6]
    ja .no                      ; ...or starts below it
    pop ax
    clc                         ; a pop writes no flag, so the answer is set
    ret                         ; after it and survives
.no:
    pop ax
    stc
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
    call tm_getkb               ; the kernel's footprint and the heap's
                                ; totals, live: every figure below this point
                                ; reads tm_kb, and a paint may arrive before
                                ; the first sample ever runs
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
; ONE span again, and this time the ASSEMBLER says so. It used to zero
; tm_elck and tm_rowck as a single run on the strength of a comment saying
; they were declared adjacent - and they were not: five unrelated words had
; drifted in between, so the run stopped five words short and the LAST rows
; of tm_rowck were never invalidated at all. Harmless while those were the
; memory list's two bottom rows, which are empty on any machine that fits
; its instances; fatal the moment the CPU caption became the last row,
; because a view switch then white-filled the content and left four fifths
; of 'CPU nnn% SCH preempt' recorded as drawn. That was answered with two
; spans; apps size pass 2 makes it one again by DEFINITION - the bss chain
; says `tm_rowck equ tm_elck + TMC_N * 2`, so nothing can drift in between
; without rewriting that line - which is the same guarantee for half the code.
; -----------------------------------------------------------------------------
tm_rowck_clear:
    push bx
    push cx
    mov bx, tm_elck
    mov cx, TMC_N + TM_NCK * TM_NCHUNK
    jmp short tm_ckz.z

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
    call tm_rowck_clear         ; every KEY is owed...
    call tm_dmg_all             ; ...and so is every BAND (SPEC.md 28.10.3).
    call tm_draw_band           ; The truth rather than a belt: both callers
    jmp tm_dmg_none             ; tm_clear_content first. Restored on the way
                                ; out, as tm_paint restores its own - the
                                ; worker's intervals run under `none`, where
                                ; the keys alone decide (SPEC.md 28.10.2)

tm_draw_band:                   ; ...and the entry that does NOT, for a damage
                                ; repaint: [tm_dmg] decides which rows are
                                ; forced and the keys decide the rest
                                ; (SPEC.md 28.10.2)
%ifdef TMF_MEM
    cmp byte [tm_view], 0       ; 0 performance, 1 memory (SPEC.md 28.4) -
    jne tm_draw_mem             ; the heap page is the memory page's list now
%endif                          ; (KERN-MEGA-PLAN 14.70.2)
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
    call tm_graph               ; its frame and the columns this paint owes -
                                ; 216 of them and up to two calls each, so it
                                ; is gated and run-coded (SPEC.md 28.10.3)
    call tm_txt_ram

    mov cx, TM_BAR_Y1           ; the bar's FRAME on the bar's own band: its
    mov dx, TM_BAR_Y2           ; INTERIOR has been gated since 28.10.2 and
    call tm_dmg_yhit            ; the frame around it was not
    jc .nobar
    call tm_frame               ; bar frame (6,71)-(TM_RW,80)
.nobar:
    call tm_bar

    mov ax, TM_PEN              ; task-list header, at the PEN and not the
    mov dx, TM_HDR_Y            ; band's left edge: the captions have to
    mov si, tm_s_hdr            ; stand over the columns they name, and
    call tm_hdrs                ; tm_rows letters from here
    call tm_rows

    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_frame - a black frame across the content, from x 6 to TM_RW
; in:  CX = its top, DX = its bottom, both content-relative
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_frame:
    push ax
    push bx
    push cx
    push dx
    TM_INKX CBLACK
    mov ax, [tm_cy]
    mov bx, ax
    add bx, cx
    add dx, ax
    mov ax, [tm_cx]
    mov cx, ax
    add ax, 6
    add cx, TM_RW
    call OSAPI_GFX_FRAME
    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_hdrs - a list's column header, once per COLUMN, because each carries its
;           own list: column 0's at DX, every later one at the top
; in:  SI = the header, AX = its pen, DX = column 0's y, both content-relative
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_hdrs:
    push ax
    push bx
    push cx
    push dx
    mov cx, [tm_cx]
    add cx, ax                  ; the pen's x is AX's last use...
    add dx, [tm_cy]
    TM_INKX CBLACK              ; ...so the pen may take AL after it
    mov bx, [tm_cols]
    mov ax, (CWHITE << 8) | CBLACK
.hdr:
    call OSAPI_FONT_RUN         ; opaque, over the content fill W_PAINT just
                                ; laid down (SPEC.md 28.5.2)
    add cx, TM_COLW
    mov dx, [tm_cy]
    add dx, TM_C2_HDR_Y
    dec bx
    jnz .hdr
    jmp short tm_pop_dcba

; THE SHARED EPILOGUE LADDER: a routine that pushed AX..DX (and SI, and DI)
; in that order (and ES last, for tm_sample) jumps here instead of carrying
; its own pops. Only routines
; that run once a refresh or less take it - never one called per row or per
; chunk, where the jump's few cycles would be paid dozens of times over.
tm_pop_edsdcba:
    pop es
tm_pop_dsdcba:
    pop di
tm_pop_sdcba:
    pop si
tm_pop_dcba:
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
%ifdef TMF_MEM
    cmp byte [tm_view], 0
    jne tm_upd_mem
%endif
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
    call tm_abdismiss           ; the credits are up: this click is spent
    jc .out                     ; taking them down rather than cycling the page

%ifdef TMF_HEAP
    cmp byte [tm_view], 0       ; the memory page's SCROLL BAR gets the point
    je .cycle                   ; first (SPEC.md 28.4.4), and answers CF = 1
    mov bx, si                  ; (against where the window is NOW: a title
    call tm_view_begin          ; drag replays the content and calls no
    call tm_hsbset              ; W_PAINT, SPEC.md 11.96.12.0)
    call tm_hscroll             ; for anything that is not in it...
    jnc .out
    call tm_lclick              ; ...then its LIST: the two header switches
    jnc .out                    ; and a group's row (KERN-MEGA-PLAN 14.70.2).
.cycle:                         ; What neither takes - the captions and the
%endif                          ; map above the list - turns the page, so the
                                ; one click target this window always had is
                                ; still there, above the list
%ifdef TMF_MEM
    mov al, [tm_view]           ; CYCLE 0 -> 1 -> 0 (SPEC.md 28.4), not a
    inc al                      ; toggle: a third page would need no chrome
    cmp al, TM_NVIEW            ; and no second hit-test, which is why the
    jb .vset                    ; wrap is TM_NVIEW's and not a literal
    xor al, al
.vset:
    cbw                         ; AH = 0: tm_slow is the byte after tm_view,
    mov [tm_view], ax           ; so the slow count starts over in the same
                                ; store, and the first unattended refresh of
                                ; a page arrives one interval after it opened
                                ; rather than up to TM_SLOW of them (28.6)
    call tm_clear_content       ; SI = window ptr; the ONLY thing that erases
    call tm_draw_full           ; the outgoing view (SPEC.md 28.10)
    call tm_promise             ; ...and WHICH page it is now decides whether
                                ; this window promises at all: 0 -> 1 takes the
                                ; band, 1 -> 0 retires it (SPEC.md 28.11)
%else
                                ; ONE PAGE, so a content click has nothing to
                                ; cycle to and the About card's dismiss above
                                ; is the whole of this handler (SPEC.md 28.12)
%endif
.out:
    jmp tm_pop_dcba

%ifdef TMF_HEAP
; -----------------------------------------------------------------------------
; tm_hscroll - a press on the memory page: did the scroll bar take it?
; in:  CX = x, DX = y (ABSOLUTE, W_ONCLICK's own coordinates), the gfx lock
;      held; [tm_hsb] as the last paint left it, which is where the bar is on
;      the glass right now
; out: CF = 0 the bar took it, CF = 1 it did not and the caller owns the click
; clobbers: nothing else (flags)
;
; A page is `fit - 1` rows - one row of overlap, so the reader keeps a line
; they have already read to place the new ones against - and an arrow is one
; row. A press on the THUMB pages toward it, which is SPEC.md 13.10.1's "the
; caller decides" answered the way files.inc answers it: the alternative is a
; part of the bar that does nothing at all.
;
; IT REDRAWS ONLY IF THE POSITION ACTUALLY MOVED, which is what makes an end
; stop free rather than a repaint of the same twenty rows.
; -----------------------------------------------------------------------------
tm_hscroll:
    push ax
    push bx
    push cx
    push dx
    mov bx, tm_hsb
    call os88ui_sbhit           ; AL = OS88UI_SB*, and NONE for a point that
    cmp al, OS88UI_SBNONE       ; is not in the bar at all - so the cycle
    je .no                      ; click needs no rect of its own

    mov dx, [tm_htop]           ; DX = where the view would go
    mov cx, [tm_maxrow]         ; ...and CX = one page of it
    dec cx
    jnz .page                   ; a frame with one row on it still pages by
    inc cx                      ; one, or the bar would have an inert half
.page:
    cmp al, OS88UI_SBDOWN       ; an ARROW moves one row, the track a page
    ja .step
    mov cx, 1
.step:
    cmp al, OS88UI_SBUP         ; UP and PGUP go back; DOWN, PGDN and the
    je .back                    ; THUMB (which pages the way it lies) go on
    cmp al, OS88UI_SBPGUP
    jne .on
.back:
    sub dx, cx
    jnc .set
    xor dx, dx                  ; ...the only place this can wrap: the top is
    jmp short .set              ; an end stop the compare below turns into a
.on:                            ; repaint of nothing
    add dx, cx
.set:
    mov [tm_htop], dx
    call tm_hclamp              ; ...and the bottom end stop, which is the one
                                ; place that knows how far down there is to go
    mov cx, [tm_htop]           ; DID THE VIEW MOVE? The block still carries
    cmp cx, [tm_hsb+12]         ; what the last paint drew from, so an end stop
    je .took                    ; costs nothing instead of repainting twenty
    call tm_upd_mem             ; rows that did not move
.took:
    clc
    jmp short .out
.no:
    stc
.out:
    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_onwheel - W_ONWH (SPEC.md 13.18): AX = signed notches, SI = window, gfx
; lock held. ONLY the memory page has a list to scroll, so the performance
; view ignores the turn - the About card included. WHEEL_STEPS ROWS a notch,
; a row being this list's line (an arrow is one), entered at tm_hscroll's
; own .set with its frame: tm_hclamp is the one place that knows how far
; down the LAST walk says there is (SPEC.md 28.4.4), and the moved-or-not
; test there is what makes a turn against an end stop draw nothing.
; -----------------------------------------------------------------------------
tm_onwheel:
    cmp byte [tm_view], 0
    je .ret
    call tm_abdismiss           ; the credits are up: the turn takes them
    jc .ret                     ; down, as a click does
    push ax                     ; tm_hscroll's frame, which .set pops
    push bx
    push cx
    push dx
    mov dx, WHEEL_STEPS
    imul dx
    add ax, [tm_htop]           ; signed, and [tm_htop] is a row count: only
    jns .set                    ; the TOP can wrap, and it is an end stop
    xor ax, ax
.set:
    xchg dx, ax                 ; DX = where the view would go
    jmp tm_hscroll.set
.ret:
    ret
%endif

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_upd_mem - the memory page's periodic redraw (gfx lock held): everything
;              but the map's frame and the XMS bar's, which only the full
;              redraw draws
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
;
; ONE PAGE SINCE KERN-MEGA-PLAN 14.70.2: the memory view's captions and map,
; then the heap page's list - every claim and every block above 1MB under one
; row per owner - with the heap page's scroll bar beside it. The claim table
; is snapshotted ONCE and every record classified ONCE (tm_classify), so the
; summary line, the map, the group totals and the claim rows under them are
; one picture of one moment and cannot disagree with each other.
; -----------------------------------------------------------------------------
tm_upd_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [tm_win]
    xor si, si                  ; SI = 0: the refresh, and no chrome
.body:                          ; (the full redraw joins here, SI = window)
    call tm_view_begin          ; [tm_cx]/[tm_cy]/[tm_rowx]/[tm_ylim]
    call tm_hsnap               ; ...the claim table and MEM_AVAIL's pair...
    call tm_classify            ; ...and every record and block to its owner
    mov ax, TMM_RAM_Y
    call tm_txt_ram_y
    or si, si
    jz .figs

    mov cx, TMM_M1_Y1           ; RAM map frame (6,14)-(TM_RW,29)
    mov dx, TMM_M1_Y2
    call tm_frame
    cmp word [tm_xoff], 0       ; a machine with no store above 1MB draws no
    jne .figs                   ; bar at all - not an empty one - and neither
                                ; does kern_mega, whose RAM above 1MB is RAM
                                ; (tm_init). The figures come off the caption
                                ; to match and everything below rises by
                                ; TMM_XSHIFT
    mov cx, TMM_XB_Y1           ; XMS bar frame - here and not in tm_xbar,
    mov dx, TMM_XB_Y2           ; which is element-checked and skips itself
    call tm_frame               ; on an empty pool
.figs:
    call tm_map_ram
    call tm_cap_xms
    call tm_xbar
    call tm_cap_avl             ; AVAIL / MAX RUN
    call tm_mhdr                ; the header and its two switches
    call tm_hsbset              ; BEFORE the rows: a row's band stops where
    call tm_rows_mem            ; the bar begins (tm_rowr)...
    call tm_hbar                ; ...and the bar itself once the walk has said
                                ; how deep the list is (SPEC.md 28.4.4)
    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_draw_mem - the memory page's full redraw: the frames plus everything
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
    jmp short tm_upd_mem.body

; -----------------------------------------------------------------------------
; tm_cap_xms - the caption under the map: "CPU 8086  XMS n/nK" (tier, then
;              used / sized above 1MB - SPEC.md 41.1/41.6), or the tier alone
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
;
; No map goes with it, and that is the honest layout rather than a missing
; feature: the map above is conventional memory, and extended memory is not in
; it - real mode has no address for it, which is the whole reason xm_copy
; exists (SPEC.md 41.5). So: the figures and a bar of their own scale, directly
; under the map.
;
; THE FIGURES ARE KERN_BIG'S STORE AND NOTHING ELSE (KERN-MEGA-PLAN 14.70.2).
; On a machine with no store this reads 'CPU 8086' and no bar is drawn - 'CPU
; 8086' IS the reason there is none (SPEC.md 60.2) - and on kern_mega in
; protected mode it reads 'CPU 286' the same way: RAM above 1MB is RAM there,
; already on the summary line and in the list, and a second bar of it beside
; the first was the confusing half of the page. tm_init sets [tm_xoff] for
; both.
;
; It needs no check word of its own for the tier - tm_rowsum hashes the
; composed tm_str, so the tier folds into TMC_XMS's for free.
; -----------------------------------------------------------------------------
tm_cap_xms:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, tm_str
    mov si, tm_s_cpu            ; 'CPU ' + the tier, ahead of the XMS figures:
    call tm_copy                ; the two belong together, because the tier is
    mov si, tm_s_t86            ; exactly what decides whether there can BE any
    call OSAPI_CPU_INFO         ; AL = the tier (SPEC.md 41.1) - asked FIRST,
    cmp al, CPU_286             ; while AX is still nobody's
    jb .tier
    mov si, tm_s_t286
    je .tier
    mov si, tm_s_t386
.tier:
    call tm_copy

    cmp word [tm_xoff], 0       ; no store above 1MB, or RAM there: the line
    jne .term                   ; is the TIER and nothing else
    call OSAPI_XMEM_CAPS        ; AX = KB still free, SK_XMS = the whole pool
    mov bx, [tm_kb+SK_XMS]
    neg ax
    add ax, bx                  ; AX = KB in use: sized minus free
    push ax                     ; --- the bar's width, worked out HERE, while
    mov cx, bx                  ; AX and BX still hold used and sized (BX
    call tm_scale               ; survives tm_scale; AX rides the stack)
    mov [tm_xbarw], ax
    pop ax
    mov si, tm_s_xms
    call tm_copy                ; BX = total, AX = used
    call tm_put5                ; FIVE digits, not tm_kpair's three: AH=88h
    mov byte [di], '/'          ; answers up to 65,535 KB and tm_put3 emits a
    inc di                      ; non-digit above 999
    mov ax, bx
    call tm_put5
    mov byte [di], 'K'
    inc di
.term:
    mov byte [di], 0

    mov bx, tm_elck + 2*TMC_XMS
    mov ax, TMM_XMS_Y
    call tm_capline             ; checked, placed, padded and drawn
    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_cap_avl - 'AVAIL nnnK  MAX RUN nnnK' (SPEC.md 28.4.1)
; in:  [tm_avl], [tm_maxrun] (tm_hsnap)
; out: nothing
; clobbers: nothing (flags only)
;
; The heap page's three caption lines, folded to the one the merged page has
; room for (KERN-MEGA-PLAN 14.70.2), and nothing they said is lost: HEAP and
; HELD are the summary line's heap half, PURGE is the Purgeable group's total,
; and FRAG is AVAIL less MAX RUN, two figures on this very line. What stays is
; the pair the heap page existed for: everything a claim could get, against
; the most it can get in ONE call - docs/FIELD-NOTES.md 2 is a refusal where
; the first says there is room and the second does not.
; -----------------------------------------------------------------------------
tm_cap_avl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tm_str
    mov si, tm_s_avl
    call tm_copy
    mov ax, [tm_avl]
    call tm_kfig
    mov si, tm_s_hmax
    call tm_copy
    mov ax, [tm_maxrun]
    call tm_kfig
    mov byte [di], 0
    mov bx, tm_elck + 2*TMC_AVL
    mov ax, TMM_AVL_Y
    sub ax, [tm_xoff]           ; it rises with the rows when there is no bar
    call tm_capline
    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_mhdr - the list's column header, once per column, with its two SWITCHES
; in:  [tm_mview], [tm_exp] (and the geometry tm_view_begin set)
; out: nothing
; clobbers: nothing (flags only)
;
; 'PROGRAMS   ADDR SIZE +ALL' as THREE opaque runs (SPEC.md 6.1), the two ends
; white on black so they read as something to press: the view this list is
; showing, a click on which shows the other, and +ALL or -ALL, which opens
; every group or - once every one is open - closes them all
; (KERN-MEGA-PLAN 14.70.2). Nothing is drawn twice: the runs are side by
; side, not one over another.
;
; Keyed like every element in this window (SPEC.md 28): on the two switches'
; state, so a click that flips one redraws it and the refresh twice a second
; draws nothing; and on the header's own band for a damage repaint.
; -----------------------------------------------------------------------------
tm_mhdr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tm_allopen             ; AH = 1 every group is open
    mov al, [tm_mview]
    inc ah                      ; (tm_elchk maps a zero key anyway)
    mov bx, tm_elck + 2*TMC_HDR
    mov dx, TMM_HDR_Y
    sub dx, [tm_xoff]
    mov cx, dx                  ; the band: column 0's header...
    cmp word [tm_cols], 1
    je .band
    mov cx, TM_C2_HDR_Y         ; ...and with two columns, from column 1's,
.band:                          ; which is at the top of the content
    add dx, 7
    call tm_elchk_y
    jc .out
    mov cx, [tm_cx]
    add cx, TM_PEN              ; CX = the pen of column 0's header
    mov dx, TMM_HDR_Y
    sub dx, [tm_xoff]
    add dx, [tm_cy]             ; DX = its y
    mov bx, [tm_cols]
.col:
    mov si, tm_s_vprog          ; --- the view switch: the view it SHOWS
    cmp byte [tm_mview], 0
    je .v
    mov si, tm_s_vsys
.v:
    mov ax, TMH_SWC
    call tm_hrun                ; white on black
    push cx
    add cx, TMH_SWC * 8         ; --- the column captions between them
    mov si, tm_s_hmid
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add cx, (TM_NCHUNK * TM_CHUNK - TMH_SWC - TMH_XSWC) * 8
    mov si, tm_s_xall           ; --- +ALL, or -ALL once all are open
    call tm_allopen
    or ah, ah
    jz .x
    add si, TMH_XSWC
.x:
    mov ax, TMH_XSWC
    call tm_hrun
    pop cx
    add cx, TM_COLW             ; the next column's header is at the top
    mov dx, [tm_cy]
    add dx, TM_C2_HDR_Y
    dec bx
    jnz .col
.out:
    jmp tm_pop_dsdcba

; tm_hrun - AX characters of SI, white on black, at CX/DX: one of the header's
; switches. Copies them into tm_str to put a NUL after them. Clobbers AX, DI
tm_hrun:
    push cx
    mov di, tm_str
    xchg ax, cx                 ; CX = how many, AX = the pen
    call tm_copyn
    mov byte [di], 0
    xchg ax, cx
    push si
    mov si, tm_str
    mov ax, (CBLACK << 8) | CWHITE
    call OSAPI_FONT_RUN
    pop si
    pop cx
    ret

; tm_allopen - AH = 1 when every group is open, else 0. Keeps everything else
tm_allopen:
    mov ah, 0
    cmp word [tm_exp], 0FFFFh
    jne .o
    cmp word [tm_exp+2], 0FFFFh
    jne .o
    inc ah
.o:
    ret

; -----------------------------------------------------------------------------
; tm_lclick - a press on the memory page's LIST: a switch or a group?
; in:  CX = x, DX = y (absolute); the gfx lock held
; out: CF = 0 the list took it (and repainted what it changed), CF = 1 it is
;      above the list and the caller turns the page
; clobbers: nothing else (flags)
;
; THE TWO SWITCHES (tm_mhdr) and every GROUP row (KERN-MEGA-PLAN 14.70.2). A
; group is found by the SCREEN ROW the press is in - [tm_rgid] is what the last
; walk put on each one, so a scrolled list needs no arithmetic about where its
; table starts - and the press flips that group's bit in [tm_exp]. A press on a
; claim row, or on the list's empty foot, is taken and does nothing: a page
; that turned when somebody clicked a claim would be a page that runs away.
;
; THE VIEW SWITCH puts the list back at its top: the other view is a different
; list, and an offset into this one means nothing there.
; -----------------------------------------------------------------------------
tm_lclick:
    push ax
    push bx
    push cx
    push dx
    sub cx, [tm_cx]             ; content-relative
    sub dx, [tm_cy]
    xor bx, bx                  ; BX = column 0...
    cmp word [tm_cols], 1
    je .c0
    cmp cx, TM_COLW
    jb .c0
    sub cx, TM_COLW             ; ...or 1
    inc bx
.c0:
    mov ax, TMM_HDR_Y           ; --- the header: column 0's under the map,
    sub ax, [tm_xoff]           ; a later one's at the top
    or bx, bx
    jz .hy
    mov ax, TM_C2_HDR_Y
.hy:
    cmp dx, ax
    jl .not                     ; above it: the captions, the map
    add ax, TM_ROW_H
    cmp dx, ax
    jge .rows
    sub cx, TM_PEN              ; a header: which switch?
    jl .took
    cmp cx, TMH_SWC * 8
    jb .view
    cmp cx, (TM_NCHUNK * TM_CHUNK - TMH_XSWC) * 8
    jb .took
    cmp cx, TM_NCHUNK * TM_CHUNK * 8
    jae .took
    call tm_allopen             ; +ALL opens every group, -ALL closes them
    mov al, ah
    dec al                      ; 1 -> 0 (close all), 0 -> FFh (open all)
    cbw
    mov [tm_exp], ax
    mov [tm_exp+2], ax
    jmp short .redraw
.view:
    xor byte [tm_mview], 1
    mov word [tm_htop], 0       ; the other view's list, from its top
    jmp short .redraw
.rows:                          ; --- a row: which one ON THE SCREEN?
    sub dx, ax                  ; DX = y below the first row (AX is it: the
    mov ax, dx                  ; header is one TM_ROW_H above it)
    mov dl, TM_ROW_H
    div dl                      ; AL = the row within this column
    xor ah, ah
    or bx, bx
    jz .r0
    add ax, [tm_colrows]        ; column 1 continues where column 0 stopped
    jmp short .rset
.r0:
    cmp ax, [tm_colrows]
    jae .took                   ; under column 0's last row
.rset:
    cmp ax, [tm_maxrow]
    jae .took
    mov bx, ax
    mov cl, [tm_rgid+bx]        ; the group the last walk put there
    cmp cl, TMG_N
    jae .took                   ; not a group: a claim, an item, a blank
    mov ax, 1                   ; flip bit CL of the dword [tm_exp]
    xor dx, dx
    cmp cl, 16
    jb .lo
    sub cl, 16
    xchg ax, dx
.lo:
    shl ax, cl
    shl dx, cl
    xor [tm_exp], ax
    xor [tm_exp+2], dx
.redraw:
    call tm_upd_mem             ; the keys decide what is lettered again
.took:
    clc
    jmp short .out
.not:
    stc
.out:
    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_kfig - AX = KB as four characters at DI: 'nnnK' to 999, then 'd.dM' and
;           ' ddM' - the list's one width for a figure that passes three digits
;           of KB only on a machine with RAM above 1MB (System, or a program
;           holding megabytes there, KERN-MEGA-PLAN 14.70.2). A list mixes K and
;           M the way any size column does; the summary line does not, and has
;           its own (tm_mpair)
; out: DI += 4. Keeps everything else
; -----------------------------------------------------------------------------
tm_kfig:
    cmp ax, 1000
    jae .mb
    call tm_put3
    jmp short .k
.mb:
    push ax
    push cx
    push dx
    cmp ax, 10240
    jae .whole
    mov cx, 10
    mul cx                      ; DX:AX = KB x 10
    mov cx, 1024
    div cx                      ; AX = tenths of a MB, 9..99
    mov cl, 10
    div cl                      ; AL = MB, AH = the tenth
    add ax, '00'
    mov [di], al
    mov byte [di+1], '.'
    mov [di+2], ah
    add di, 3
    jmp short .o
.whole:
    mov cl, 10
    shr ax, cl
    call tm_put3
.o:
    pop dx
    pop cx
    pop ax
    mov byte [di], 'M'
    inc di
    ret
.k:
    mov byte [di], 'K'
    inc di
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

    ; The snapshot is taken by tm_upd_mem/tm_draw_mem above, because the
    ; CAPTION reads it too now (SPEC.md 28.4.1) and two calls a refresh would
    ; walk the table twice for one picture.
    mov si, tm_claims           ; the claim map is the only part of this that
    mov cx, CLAIM_SNAPSHOT_SIZE      ; moves - the kernel and its buffers are fixed
    xor ax, ax                  ; at build time and the total is the boot int
    call tm_sumb                ; 12h figure - so it IS the key
    mov bx, tm_elck + 2*TMC_MRAM
    mov cx, TMM_M1_Y1
    mov dx, TMM_M1_Y2
    call tm_elchk_y
    jc .out

    TM_INKX CWHITE
    xor ax, ax                  ; whole interior white first
    mov cx, TM_GW - 1
    mov bx, TMM_M1_Y1
    call tm_map_rect
    call OSAPI_GFX_FILL

    xor ax, ax                  ; [0, kernel end): low memory and the
    mov dx, [tm_kb+SK_KERN]     ; kernel entire - image, scratch, buffers and
                                ; stacks (SPEC.md 2). 50% gray: reserved, and
    call tm_band                ; not available to anything else. The kernel
    call OSAPI_GFX_FILL_GRAY    ; starts 1.5K up (the IVT+BDA row), so the
                                ; inclusive end is SK_KERN and not SK_KERN - 1:
                                ; the span's KB count less one, plus the whole
                                ; KB under KERNEL_SEG. SK_KERN rounds UP, so
                                ; this end never paints a free KB as kernel

    mov ax, [tm_kb+SK_KERN]           ; ...and its BUFFERS, over the top of
    sub ax, [tm_kb+SK_BUF]            ; that, in a texture of their own: the
    inc ax                            ; FAT snapshot, the disk caches and
    mov dx, [tm_kb+SK_KERN]           ; every task stack are the part of the
    call tm_band                      ; kernel that is scratch rather than
    mov si, tm_pat_buf                ; program, and the part these figures
    TM_FILL_PAT                       ; are steered by (docs/KERNEL-MEMORY.md)


    ; Every live claim, at its real address: a record in the snapshot, not in
    ; mem_tab - the whole table came over in one call at the top of this
    ; routine, so DI simply walks it.
    ;
    ; THE FRAME IS THE POINT. A claim is the only kind of band on this map
    ; that appears and disappears while you watch, several of them sit
    ; shoulder to shoulder, and the scale is coarse - 4KB per pixel on a 640KB
    ; machine, so a 3KB Disk-window cache is a single column. A texture alone
    ; cannot say where one claim stops and the next starts at that size; a
    ; 1px rule can, and gfx_frame degenerates gracefully: a one-column claim
    ; comes out as a solid vertical line, which is what it should look like.
    ;
    ; THE PEN IS SET ONCE, before the walk and not per claim: FILL_PAT
    ; ignores the pen (it writes only 0/15), and FILL_GRAY above does too, so
    ; nothing between two frames can move it. One far call where there were
    ; as many as there are claims.
    TM_INKX CBLACK              ; the claims' hard black frames
    mov di, tm_claims
.claim:
    mov dx, [di+CLS_SEG]
    or dx, dx                   ; 0 = a free record
    jz .cnext
    mov ax, [di+CLS_PARA]
    mov cl, 6
    shr ax, cl                  ; AX = size KB (a segment is KB<<6)
    shr dx, cl                  ; DX = base KB
    xchg ax, dx
    add dx, ax
    dec dx                      ; DX = inclusive end KB
    call tm_band
    mov si, tm_pat_clm          ; light interior...
    TM_FILL_PAT
    call OSAPI_GFX_FRAME        ; ...hard black frame
.cnext:
    add di, CLS_RECSZ
    cmp di, tm_claims + CLAIM_SNAPSHOT_SIZE
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
    add ax, 1023                ; AX = size KB, rounded with the carry kept:
    rcr ax, 1                   ; a 64KB region's I_SIZE is 0xFFFF (SPEC.md
    mov cl, 9                   ; 20.1.1), every other one a whole KB
    shr ax, cl
    mov di, [tm_ispt+di]        ; DI = base SEGMENT
    cmp byte [tm_pm], 0         ; ...unless it is a SELECTOR, a region placed
    je .rconv                   ; above 1MB: it has no address on this map,
    test di, 4                  ; and its row says HIGH (tm_inst_row)
    jz .rconv
    pop si
    jmp short .rnext
.rconv:
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
    jmp tm_pop_dsdcba

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
; tm_ramkb2x - KB -> conventional-map interior pixel column
; in:  AX = KB
; out: AX = px = KB*TM_GW/totalK, clamped to 0..TM_GW-1
; clobbers: flags
; -----------------------------------------------------------------------------
tm_ramkb2x:
    push cx
    mov cx, [tm_totkb]          ; int 12h can't return 0, but stay DIV-safe
    call tm_scale
    pop cx
    cmp ax, TM_GW - 1
    jbe .done
    mov ax, TM_GW - 1
.done:
    ret

; -----------------------------------------------------------------------------
; tm_romscan - the option ROMs between C000 and EFFF (SPEC.md 28.4.9), into
;              [tm_umn] records of {segment, KB} at tm_uma. A ROM is what
;              says it is one: 55AAh at a 2K boundary and a length byte; the
;              scan steps over its whole length, clamped to the window. Read
;              once, at tm_init - what is in ROM does not change while the
;              machine runs
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_romscan:
    push ax
    push bx
    push cx
    push dx
    push es
    mov dx, 0xC000
    xor bx, bx
.l:
    cmp dx, 0xF000
    jae .out
    cmp bl, TM_UMAX
    jae .out
    mov es, dx
    mov cx, 0x80                ; the next 2K by default
    cmp word [es:0], 0xAA55
    jne .n
    mov al, [es:2]              ; length in 512-byte blocks
    xor ah, ah
    or ax, ax
    jz .n
    inc ax                      ; KB, rounded up
    shr ax, 1
    mov cx, 0xF000              ; ...and NEVER PAST F000, where the BIOS row
    sub cx, dx                  ; starts: what a ROM's header claims is its
    push ax                     ; length is clamped to the window it is in,
    mov ax, cx                  ; so the rows close at 384 whatever a header
    mov cl, 6                   ; says (KERN-MEGA-PLAN 14.70.2), and the step
    shr ax, cl                  ; below cannot carry DX past FFFF - which
    xchg ax, cx                 ; wrapped the scan round into conventional
    pop ax                      ; memory, to find C000 again and again until
    cmp ax, cx                  ; TM_UMAX records were spent
    jbe .fit
    mov ax, cx
.fit:
    push bx
    shl bx, 1
    shl bx, 1
    mov [tm_uma+bx], dx
    mov [tm_uma+bx+2], ax
    pop bx
    inc bx
    mov cl, 6                   ; KB -> paragraphs, then up to the next 2K
    shl ax, cl
    add ax, 0x7F
    and ax, 0xFF80
    xchg ax, cx
.n:
    add dx, cx
    jmp short .l
.out:
    mov [tm_umn], bl
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hsnap - refresh the claim table (SPEC.md 20.9)
; in:  nothing
; out: nothing (all registers preserved)
;
; Taken once at the top of each paint: the map, the AVAIL line and the whole
; list read it, and a snapshot per element would let a group's total disagree
; with the rows under it.
; -----------------------------------------------------------------------------
tm_hsnap:
    push ax
    push di
    push es
    TM_ES_DATA
    mov di, tm_claims
    TM_CLAIM_SNAPSHOT
    pop es
                                ; ...and the heap's two live figures with it:
                                ; AX = largest run, BX = total available. Once
                                ; a refresh, because mem_avail walks the table
                                ; per candidate
%ifdef TMF_HEAP
    push bx
    call OSAPI_MEM_AVAIL
    mov [tm_maxrun], ax
    mov [tm_avl], bx
    pop bx
%endif
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hkb - a claim's size in KB
; in:  AX = paragraphs (MC_PARA/CLS_PARA)
; out: AX = KB
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_hkb:
    push cx
    mov cl, 6                   ; MEM_PARA_KB: the heap deals in whole KB, so
    shr ax, cl                  ; this divides exactly
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_hdrv - is the owner word in AX a DRIVER's image segment?
; in:  AX = a claim's CLS_OWN
; out: CF = 0 the owner is a driver image, so the claim is that driver's
;      buffer, and BX = the image's segment; CF = 1 it is not (BX spent)
; clobbers: BX (the output) and the flags
;
; SPEC.md 28.4.6, and it is drv_owns_seg (kernel/driver.inc) read off the
; snapshot. A driver is neither an instance nor a kernel tag - mem_own stamps a
; claim with the CALLING SEGMENT (SPEC.md 50.3), which for a driver is the
; segment its image was loaded into, so SOUND.DRV's 8KB ring carries a plain
; conventional segment as its owner. The heap page's own grouping said no to
; it and the ring was in a total on the caption line and on no row under it.
;
; THE SNAPSHOT ALREADY CARRIES THE ANSWER, so this needs no API cell. A
; driver's image is itself a record in this table tagged MEM_K_DRV, so "is
; this segment a driver image" is "is there a MEM_K_DRV record based there":
; the same set drv_tab names, off the copy the page has already taken - and
; the image's segment is what names the driver's GROUP (tm_drvgid reads its
; class out of its header).
;
; TWO HOPS, and they are the kernel's two. drv_owns_seg walks drv_tab, then
; for a segment not in it asks who owns the claim based there and walks
; drv_tab again - ONE level, like mem_own_drv's (SPEC.md 52.11.6) - for a
; driver's SECOND image (52.11.7). Hop 2 here is literally that: the claim
; based at the owner word is owned by a MEM_K_DRV record. The walk stopping at
; a record that does not exist is what bounds it, so a package's data claim
; finds its region, reads an instance SLOT as that region's owner, and finds
; nothing based at 0..11 - two passes and a no.
; -----------------------------------------------------------------------------
tm_hdrv:
    push ax
    push cx
    push si
    mov cx, 2                   ; the image, then a second image and no further
.hop:
    mov si, tm_claims
.rec:
    mov bx, [si+CLS_SEG]
    or bx, bx
    jz .next                    ; a free record
    cmp bx, ax
    je .found
.next:
    add si, CLS_RECSZ
    cmp si, tm_claims + CLAIM_SNAPSHOT_SIZE
    jb .rec
    jmp short .no               ; nothing is based there, so it is no claim of
                                ; ours and cannot be an image
.found:                         ; BX = the claim based at AX...
    mov ax, [si+CLS_OWN]
    cmp ax, MEM_K_DRV
    je .yes                     ; ...which is the image
    loop .hop                   ; ...or whoever owns IT is
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop si                      ; pop leaves the flags alone
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_htype - the TYPE column: what KIND of claim is this?
; in:  AX = CLS_OWN, BX = CLS_SEG, DI = dest
; out: DI advanced by TMH_TYPEC
; clobbers: nothing else (flags)
;
; An owner word the table does not know prints as ITSELF, in hex. A debug page
; must not label an unknown tag as the nearest thing it recognises: a tag added
; to the kernel and not mirrored into apps/os88api.inc shows up here as a bare
; number, which is visible and true, where a wrong name would be neither.
; -----------------------------------------------------------------------------
tm_htype:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp ah, MEM_PG_MIN
    jae .kern
    cmp ax, INST_MAX
    jae .data                   ; a plain SEGMENT: a package's own claim (a
                                ; DRIVER's buffer is tm_crow's, which knows
                                ; the group, SPEC.md 28.4.6)
    mov si, ax                  ; an instance slot: its region, or a built-in's
    shl si, 1                   ; claim. SI, not DX: an 8086 indexes through
    cmp word [tm_isz+si], 0     ; BX/BP/SI/DI only (SPEC.md 1)
    je .data
    cmp bx, [tm_ispt+si]        ; the region is the claim AT the region's base
    jne .data
    mov si, tm_s_tregn
    jmp short .put
.data:
    mov si, tm_s_tdata
    jmp short .put
.kern:                          ; (each range's name is loaded BEFORE its
                                ; test, so a hit goes straight to .put)
    mov si, tm_s_twsav
    mov dx, ax                  ; the raise-cache RANGE first: one per window
    sub dx, MEM_P_WSAVE         ; slot (SPEC.md 11.96.3), so it is not in the
    cmp dx, MEM_P_WSAVE_N       ; exact-match table below
    jb .put
    mov si, tm_s_tfatw
    mov dx, ax                  ; ...and the FAT windows, one per VOLUME, which
    sub dx, MEM_P_FATW          ; became a range when they became a cache
    cmp dx, MEM_P_FATW_N        ; (SPEC.md 18.8.4)
    jb .put
    mov si, tm_s_tview
    mov dx, ax                  ; ...and a Disk window's LISTING cache, one per
    sub dx, MEM_P_VIEW          ; fm_pool slot, which became a range when it
    cmp dx, MEM_P_VIEW_N        ; became a cache on kern_small (SPEC.md 50.6.5).
    jb .put                     ; On kern_big nothing is stamped with it and
                                ; this compare simply never fires
    mov si, tm_ktab
.scan:
    mov dx, [si]
    or dx, dx
    jz .hex
    cmp dx, ax
    je .found
    add si, TMK_REC
    jmp short .scan
.found:
    inc si                      ; the name is the record's own tail
    inc si
    jmp short .put
.hex:
    call tm_put4x               ; AX is still the owner word
    mov cx, TMH_TYPEC - 4
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
    jmp short .out
.put:
    mov cx, TMH_TYPEC
    call tm_copyn
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_htier - the TIER column: what does LOSING this block cost? (SPEC.md 50.6.4)
; in:  AX = CLS_OWN, DI = dest
; out: DI advanced by TMH_TIERC
; clobbers: nothing else (flags)
;
; The high byte alone answers it, which is the whole point of the encoding. A
; dash covers both unpurgeable cases - MEM_LVL_TOP and every instance-owned
; row - because neither can be taken and the reader does not need them
; distinguished here.
; -----------------------------------------------------------------------------
tm_htier:
    push ax
    push bx
    push cx
    push si
    mov si, tm_s_hheld          ; 'HELD'
    cmp ah, MEM_PG_MIN
    jb .put                     ; an instance's own: never a cache
    cmp ah, MEM_PG_MAX
    ja .put                     ; MEM_LVL_TOP: an ordinary kernel claim
    mov bl, ah
    sub bl, MEM_PG_MIN
    xor bh, bh
    shl bx, 1
    shl bx, 1
    lea si, [bx+tm_tiers]
.put:
    mov cx, TMH_TIERC
    call tm_copyn
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hclamp - hold [tm_htop] inside what there is to look at
; in:  [tm_hrows] from the last walk, [tm_maxrow]
; out: nothing (all registers preserved)
;
; Measured against the LAST walk's count and not this one's, because this
; one has not happened yet - which means a list that SHRANK under a scrolled
; view corrects itself on the next refresh rather than on the paint that
; discovered it. That is one interval, twice a second; clamping after the walk
; would need a second walk to act on it.
;
; [tm_maxrow] is the right `fit` for this page without qualification: it is
; already tm_layout's sum of the heap page's own column 0 (tm_hcolrows) and
; every later column, which is exactly the set of rows the frame shows.
; -----------------------------------------------------------------------------
tm_hclamp:
    push ax
    mov ax, [tm_hrows]
    sub ax, [tm_maxrow]
    jbe .top                    ; the whole table fits: there is nothing to
    cmp [tm_htop], ax           ; scroll and the view is pinned at the top
    jbe .out
    mov [tm_htop], ax
    jmp short .out
.top:
    mov word [tm_htop], 0
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hsbset - fill the scroll bar's seven-word block (SPEC.md 13.10.2/28.4.4)
; in:  [tm_cx]/[tm_cy]/[tm_ylim]/[tm_cols] (tm_view_begin), [tm_hrows]
; out: nothing (all registers preserved)
;
; Called TWICE per paint and that is deliberate rather than wasteful: the
; GEOMETRY has to be standing before the rows draw, because tm_rowr stops a
; row's band where the bar begins; the COUNT is only known once the walk has
; finished. Thirty instructions is cheaper than either a second place that
; knows where the bar is or a flag saying which half to fill.
;
; The bar goes beside the LAST column, which is the one a row wrapping off the
; bottom of column 0 arrives in - so it is at the end of the list in reading
; order as well as on the glass.
; -----------------------------------------------------------------------------
tm_hsbset:
    push ax
    push dx
    mov ax, [tm_cx]             ; x2 = the last column's own right edge...
    add ax, TM_RW
    mov dx, TMM_ROW_Y           ; y1 = that column's FIRST ROW: column 0 sits
    sub dx, [tm_xoff]           ; under the map and the captions, every later
    cmp word [tm_cols], 1       ; one at the top of the content. ONE or TWO
    je .one                     ; columns (tm_layout), so the second is a
    add ax, TM_COLW             ; constant step and not a multiply
    mov dx, TM_C2_ROW_Y
.one:
    add dx, [tm_cy]
    mov [tm_hsb+4], ax
    sub ax, TMH_SB_W - 1        ; ...and the bar is its rightmost TMH_SB_W
    mov [tm_hsb], ax
    mov [tm_hsb+2], dx
    mov ax, [tm_ylim]           ; y2 = the bottom of the lowest row band the
    add ax, 7                   ; frame holds, which is one pixel clear of the
    mov [tm_hsb+6], ax          ; frame's own bottom border

                                ; total and fit ARE [tm_hrows] and [tm_maxrow]
                                ; (they live in this block), so only pos is
    mov ax, [tm_htop]           ; copied - it must stay the value the last
    mov [tm_hsb+12], ax         ; paint drew from, for tm_hscroll
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hbar - draw the scroll bar, if anything about it moved
; in:  the block, after the walk that settled [tm_hrows]
; out: nothing (all registers preserved)
;
; Keyed on the WHOLE block rather than on `pos` alone, so a window move or a
; resize redraws the bar as readily as a claim does - the same reason every
; other element in this window is keyed on the thing it is a function of. It
; costs os88ui_sbar's sixteen drawing calls when it fires and nothing at all
; on the twice-a-second refresh of a machine where no claim has moved, which
; is the normal case.
; -----------------------------------------------------------------------------
tm_hbar:
    push ax
    push bx
    push cx
    push si
    call tm_hsbset              ; ...again: the walk has settled the count,
                                ; and tm_hclamp may have moved pos with it
    mov si, tm_hsb
    mov cx, 14
    xor ax, ax
    call tm_sumb
    mov bx, tm_elck + 2*TMC_HSB
    call tm_elchk
    jc .out
    mov bx, tm_hsb
    call os88ui_sbar
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The memory page's LIST (SPEC.md 28.4, KERN-MEGA-PLAN 14.70.2)
;
; ONE ROW PER OWNER, each with its total, and a click on one opens it into the
; claims it holds. It is the memory view's list and the heap page's merged:
; the memory view answered HOW MUCH each owner holds, the heap page WHICH of
; its claims is the big one and WHAT the next claim could take back, and a
; collapsed group is the first answer while an open one is the second.
;
; Every record is CLASSIFIED once a paint, into the group that holds it
; (tm_classify), and every figure on the page is read off that: a group's
; total is the sum of its members because they are the same bytes, and the
; groups partition everything claimed, so the rows add up to the summary
; line's used figure by construction (SPEC.md 28.4.1) - the one exception
; being PURGEABLE, which is in nobody's total on purpose.
;
; THE GROUP IDS are bits of [tm_exp], the open/closed state, and bytes in
; [tm_cgid] / [tm_xgid] / [tm_rgid]:
;   0..INST_MAX-1   an instance: a program's group, or a built-in's
;   TMG_SYS         System, as ONE row (the PROGRAMS view)
;   TMG_PROG        the programs, as ONE row (the SYSTEM view)
;   TMG_PURGE       every purgeable claim, whoever caused it
;   TMG_KERN        the kernel's own claims and blocks
;   TMG_DRV + c     a driver, by its DRVC_* class c (tm_dnames)
; =============================================================================
TMG_SYS     equ INST_MAX
TMG_PROG    equ INST_MAX + 1
TMG_PURGE   equ INST_MAX + 2
TMG_KERN    equ INST_MAX + 3
TMG_DRV     equ INST_MAX + 4
TMG_N       equ TMG_DRV + 16    ; 32: one bit each in the dword [tm_exp]
TMG_FIX     equ 0FEh            ; a fixed area above 1MB, not a group's
%if TMG_N != 32
  %error "taskmgr: the group ids are the 32 bits of [tm_exp] (INST_MAX moved)"
%endif
%if CLAIM_SNAPSHOT_SIZE != MEM_MAX * CLS_RECSZ
  %error "taskmgr: tm_cgid is one byte per claim record"
%endif
TMX_DRVIMG  equ 0FDh            ; OSAPI_XMEM_WALK's owner byte for a driver's
                                ; image (kern_mega's MX_KDRV)
TMX_KERN    equ 0FEh            ; ...for the kernel's own (MX_KOWN); FFh is a
                                ; cell called outside any instance

; -----------------------------------------------------------------------------
; tm_classify - every claim record and every block above 1MB to its group,
;               and every group's total
; in:  tm_claims (tm_hsnap), tm_xw/[tm_xwn] (tm_hikb), the instance snapshot
; out: tm_cgid[MEM_MAX], tm_xgid[TM_XWN], tm_gkb[TMG_N] (KB)
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_classify:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, tm_gkb
    mov cx, TMG_N
    xor ax, ax
    cld
    rep stosw
    pop es
    mov si, tm_claims
    mov di, tm_cgid
.c:
    call tm_cgidof              ; AL = the group, or FFh for a free record
    mov [di], al
    cmp al, TMG_N
    jae .cn
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [si+CLS_PARA]
    call tm_hkb
    add [tm_gkb+bx], ax
.cn:
    add si, CLS_RECSZ
    inc di
    cmp di, tm_cgid + MEM_MAX
    jb .c
    mov si, tm_xw               ; ...and above 1MB, where the pool walked
    mov di, tm_xgid
    mov cx, [tm_xwn]
.x:
    jcxz .done
    call tm_xgidof
    mov [di], al
    cmp al, TMG_N
    jae .xn                     ; a fixed area: tm_sysitems names it
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [si+XMW_KB]
    add [tm_gkb+bx], ax
.xn:
    add si, XMW_SIZE
    inc di
    dec cx
    jmp short .x
.done:
    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_cgidof - the group of the CLAIM RECORD at SI
; out: AL = its group, or FFh for a free record. Keeps everything else (AH
;      included)
;
; The heap page's tm_hmatch asked "does this record belong to group BX" once
; per group per record, and filed a raise cache under the package whose window
; it was (SPEC.md 28.4.5). This answers the other way round, once: a record
; belongs to exactly ONE group, so no claim can be counted twice and none can
; be left in no group at all (SPEC.md 28.4.6's defect) - an owner word nothing
; recognises is the Kernel's, where the kernel's own totals already put it.
;
;   free                         -> FFh
;   a purgeable tier             -> Purgeable, whoever caused it: a cache is in
;                                   nobody's total (SPEC.md 28.4.1), and a raise
;                                   cache under its package's group was a total
;                                   that did not match the package's row
;   MEM_K_DRV                    -> that driver, by the class in its image
;   any other kernel tag         -> Kernel
;   a live instance's slot       -> that instance (a region, a built-in's claim)
;   a live package's segment     -> that package (a data claim, SPEC.md 50.3)
;   a driver's image segment     -> that driver (its buffers, SPEC.md 28.4.6)
;   a driver's selector          -> that driver (kern_mega, drv_hi_move)
;   anything else                -> Kernel
; -----------------------------------------------------------------------------
tm_cgidof:
    push bx
    push cx
    mov al, 0FFh
    cmp word [si+CLS_SEG], 0
    je .out
    mov bx, [si+CLS_OWN]
    cmp bh, MEM_PG_MIN
    jb .notk
    mov al, TMG_PURGE           ; 0xFB..0xFE: a purge tier...
    cmp bh, MEM_PG_MAX
    jbe .out
    mov al, TMG_KERN            ; ...or MEM_LVL_TOP, an ordinary kernel tag
    cmp bx, MEM_K_DRV
    jne .out
    mov bx, [si+CLS_SEG]        ; a driver's IMAGE: its class is in it
    jmp short .drv
.notk:
    cmp bx, INST_MAX
    jae .seg
    mov al, bl                  ; an instance's slot, if the instance lives
    cmp byte [tm_ist+bx], 0
    jne .out
    jmp short .kern
.seg:
    mov cx, bx                  ; CX = the owner word
    xor bx, bx                  ; BX = slot
.p:
    cmp byte [tm_ist+bx], 0
    je .pn
    shl bx, 1
    cmp word [tm_isz+bx], 0     ; a built-in owns no segment to match
    je .pn2
    cmp cx, [tm_ispt+bx]
    je .pk
.pn2:
    shr bx, 1
.pn:
    inc bx
    cmp bx, INST_MAX
    jb .p
    push ax
    mov ax, cx
    call tm_hdrv                ; BX = the image whose claim this is
    pop ax
    jnc .drv
    cmp byte [tm_pm], 0         ; a driver kern_mega moved above 1MB names its
    je .kern                    ; claims with its DATA SELECTOR (drv_hi_move):
    push ax                     ; VERR says it can be read without a fault
    mov ax, cx
    db 0x0F, 0x00, 0xE0         ; verr ax - a 286 instruction, reached only
    pop ax                      ; in protected mode ([tm_pm])
    jnz .kern
    mov bx, cx
.drv:
    call tm_drvgid
    jmp short .out
.pk:
    shr bx, 1
    mov al, bl
    jmp short .out
.kern:
    mov al, TMG_KERN
.out:
    pop cx
    pop bx
    ret

; tm_drvgid - BX = a driver image's segment or selector -> AL = its group,
; TMG_DRV + its DRVC_* class when its header says it is a driver, TMG_DRV
; (the 'Drivers' group) for an overlay or anything else. Keeps the rest
tm_drvgid:
    push es
    mov es, bx
    mov al, TMG_DRV
    cmp word [es:0], 0x384F     ; 'O8', the driver header's magic
    jne .o
    cmp byte [es:3], TMD_NCLS   ; DRV_H_CLASS
    jae .o
    add al, [es:3]
.o:
    pop es
    ret

; -----------------------------------------------------------------------------
; tm_xgidof - the group of the block ABOVE 1MB at SI (an OSAPI_XMEM_WALK
;             record)
; out: AL = its group, or TMG_FIX for one of kern_mega's fixed areas.
;      Keeps everything else
;
; The owner byte says it: a slot (an OSAPI_XMEM_ALLOC block), 80h | slot (a
; region placed there), 0A0h | slot (an OS88_HIGHMEM claim), 0C0h | row (a
; driver's claim) and FDh (a driver's image) - the last two carrying the
; driver's class in XMW_CLS - FEh the kernel's and FFh a cell called outside
; any instance. RAM IS RAM (KERN-MEGA-PLAN 13.6): a program's blocks here are
; in its group exactly as its conventional claims are.
; -----------------------------------------------------------------------------
tm_xgidof:
    push bx
    mov al, [si+XMW_OWN]
    cmp al, XMW_HMA
    jb .nm
    cmp al, XMW_PAD
    ja .nm
    mov al, TMG_FIX
    jmp short .o
.nm:
    cmp al, TMX_DRVIMG
    je .drv
    mov bl, al
    and bl, 0E0h
    cmp bl, 0C0h
    je .drv
    cmp al, TMX_KERN
    jae .kern
    and al, 1Fh                 ; slot, 80h | slot, 0A0h | slot
    cmp al, INST_MAX
    jae .kern
    mov bl, al
    xor bh, bh
    cmp byte [tm_ist+bx], 0
    jne .o
.kern:
    mov al, TMG_KERN
    jmp short .o
.drv:
    mov al, [si+XMW_CLS]
    cmp al, TMD_NCLS
    jb .dc
    xor al, al
.dc:
    add al, TMG_DRV
.o:
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_rows_mem - the memory page's list (SPEC.md 28.4)
;
; PROGRAMS view: System as one group, then each package, then Purgeable.
; SYSTEM view: System's parts itemised - the fixed spans, Kernel, each driver
; and each built-in - then the programs as one group, then Purgeable. Both
; end with the 640K-1MB rows, which are not RAM and in no total (28.4.9).
;
; THE SCROLL is the heap page's, unchanged (SPEC.md 28.4.4): [tm_hskip] rows
; above the view are composed and dropped by tm_mrow_close, so [tm_mrow] counts
; SCREEN rows and tm_rowck stays indexed by where a row is drawn.
;
; in:  tm_classify's tables and the snapshots
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

    call tm_hclamp              ; the scroll offset against what the LAST walk
    mov ax, [tm_htop]           ; counted (SPEC.md 28.4.4), then spend it: a
    mov [tm_hskip], ax          ; row above the view is composed and dropped
    xor ax, ax                  ; by tm_mrow_close, so this counts from the
    mov [tm_mrow], ax           ; first row that is actually on screen
    mov [tm_hpad], ax

    cmp byte [tm_mview], 0
    jne .sysv

    ; --- PROGRAMS: System as one row, each package, Purgeable -----------------
    mov bl, TMG_SYS
    mov si, tm_s_sys
    call tm_systot              ; AX = everything System holds
    mov dx, tm_pat_gray         ; ...and its band on the map is the gray one
    call tm_grp
    jc .progs
    mov dx, 0x0002              ; open: its parts, as ITEMS under it
    call tm_sysitems
.progs:
    xor bx, bx
.pk:
    call tm_ispkg
    jc .pkn
    call tm_iname               ; SI = its name
    push bx
    shl bx, 1
    mov ax, [tm_gkb+bx]
    shl bx, 1                   ; the slot's own texture, which is how the row
    shl bx, 1                   ; keys its region on the map
    lea dx, [bx+tm_pats]
    pop bx
    call tm_grp
    jc .pkn
    call tm_mbrs                ; open: its claims and its blocks
.pkn:
    inc bx
    cmp bx, INST_MAX
    jb .pk
    jmp short .purge

    ; --- SYSTEM: System's parts, then the programs as one row ----------------
.sysv:
    mov dx, 0x0101              ; at the top level, its owners as GROUPS
    call tm_sysitems
    mov bl, TMG_PROG
    mov si, tm_s_gprog
    call tm_progtot
    xor dx, dx
    call tm_grp
    jc .purge
    xor bx, bx                  ; open: each package's total, as an ITEM
.pi:
    call tm_ispkg
    jc .pin
    call tm_iname
    push bx
    shl bx, 1
    mov ax, [tm_gkb+bx]
    shl bx, 1
    shl bx, 1
    lea dx, [bx+tm_pats]
    pop bx
    mov cx, 0FFFFh              ; no address: a program may be in pieces
    push bx
    mov bx, dx                  ; its square
    mov dl, 2
    call tm_item
    pop bx
.pin:
    inc bx
    cmp bx, INST_MAX
    jb .pi

    ; --- Purgeable, in both: nobody's, and reclaimable ------------------------
.purge:
    mov ax, [tm_gkb + 2*TMG_PURGE]
    or ax, ax                   ; only when there is something to take back:
    jz .uma                     ; an empty group opens onto nothing
    mov bl, TMG_PURGE
    mov si, tm_s_gpurg
    mov dx, tm_pat_clm          ; a claim's texture, as the swatch on the
    call tm_grp                 ; summary line keys it
    jc .uma
    call tm_mbrs

    ; --- 640K-1MB, read-only: what is there, not RAM (SPEC.md 28.4.9) --------
    ; After the programs, so on a screen it does not fit it is these rows the
    ; clip takes. None of it is in a total: the summary line is RAM
.uma:
    call tm_mrow_nolast
    call tm_mrow_open
    mov si, tm_s_uhdr
    call tm_copy
    call tm_mrow_close
    xor bx, bx                  ; no squares from here down
    mov dl, 2
    mov si, tm_s_uvid
    mov cx, 0xA000              ; the video window is the PC's, whatever card
    mov ax, 128                 ; answers in it
    call tm_item
    xor di, di                  ; DI = the KB the rows above state, C000-EFFF
.rom:
    cmp bl, [tm_umn]            ; the option ROMs tm_init found, each already
    jae .romd                   ; inside C000-EFFF and clear of the next
    push bx                     ; (tm_romscan), so their KB cannot sum past
    shl bx, 1                   ; the 192 the window has
    shl bx, 1
    mov cx, [tm_uma+bx]
    mov ax, [tm_uma+bx+2]
    pop bx
    add di, ax
    mov si, tm_s_urom
    push bx
    xor bx, bx
    call tm_item
    pop bx
    inc bx
    jmp short .rom
.romd:
    mov si, tm_s_unrep          ; ...the rest, called unreported rather than
    mov ax, 384 - 128 - 64      ; anything (KERN-MEGA-PLAN 14.1 answer 10):
    sub ax, di                  ; RAM a board shadows or remaps there is the
    mov cx, 0FFFFh              ; board's business, and it says nothing
    xor bx, bx
    call tm_item
    mov si, tm_s_ubios
    mov cx, 0xF000
    mov ax, 64
    call tm_item

    mov ax, [tm_htop]           ; how deep the TABLE is: the rows that were
    sub ax, [tm_hskip]          ; drawn, plus the ones dropped above them -
    add ax, [tm_mrow]           ; which is [tm_htop] less whatever the walk
    sub ax, [tm_hpad]           ; was too short to spend...
                                ;
                                ; ...LESS THE ROWS tm_mrow_nolast SPENT, and
                                ; that subtraction is the whole of SPEC.md
                                ; 28.4.4's limit cycle. A pad row is a
                                ; property of the LAYOUT and not of the table:
                                ; WHICH heading lands on a column's foot
                                ; depends on the scroll offset, so counting it
                                ; made [tm_hrows] a function of [tm_htop].
                                ; Measured on a two-column CGA: 21 rows at
                                ; offset 0 and 22 at offset 4, so tm_hclamp
                                ; allowed 5 from one position and 4 from the
                                ; next and the view stuck one row short of its
                                ; own end, for ever
    add ax, [tm_cols]           ; ...and the RANGE carries the worst case back,
    dec ax                      ; one per column but the last, so the bottom of
                                ; the table stays reachable when a pad row is
                                ; eating a screen slot under it. Position-
                                ; independent, which is the property that
                                ; matters: a bound that moves with the view is
                                ; a bound the view can never settle against
    mov [tm_hrows], ax          ; The bar's `total`, and what tm_hclamp
                                ; measures against next time
    mov word [tm_hskip], 0      ; NOTHING below here skips: the blank loop
                                ; runs in screen rows
.blank:                         ; blank whatever rows the list did not reach
    mov ax, [tm_maxrow]
    cmp word [tm_mrow], ax
    jae .done
    call tm_mrow_open
    call tm_mrow_close
    jmp .blank
.done:
    jmp tm_pop_dsdcba

; tm_ispkg - CF = 0 when slot BX is a live PACKAGE (a program, not a
; built-in). Keeps everything
tm_ispkg:
    cmp byte [tm_ist+bx], 0
    je .no
    test byte [tm_iknd+bx], KIND_PKG
    jz .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tm_systot - AX = everything System holds, KB: IVT+BIOS and the kernel below
;             640K, its fixed spans above 1MB, and every group that is not a
;             program's - Kernel, each driver, each built-in. Keeps the rest
; tm_progtot - AX = everything the programs hold, KB. Keeps the rest
;
; The two are complements over the groups, and with the Purgeable group (in
; neither, and in no total) and the fixed spans they partition the summary
; line's used figure (SPEC.md 28.4.1, 28.4.7, 28.4.8).
; -----------------------------------------------------------------------------
tm_systot:
    push bx
    call tm_sysk                ; the kernel, whole KB rounded down...
    add ax, [tm_lowkb]          ; ...the low memory below it...
    add ax, [tm_hifix]          ; ...what it keeps between 1MB and the pool
    add ax, [tm_gkb + 2*TMG_KERN]
    mov bx, TMG_DRV * 2
.d:
    add ax, [tm_gkb+bx]         ; ...every driver...
    inc bx
    inc bx
    cmp bx, TMG_N * 2
    jb .d
    xor bx, bx
.b:
    cmp byte [tm_ist+bx], 0     ; ...and every built-in
    je .bn
    test byte [tm_iknd+bx], KIND_PKG
    jnz .bn
    shl bx, 1
    add ax, [tm_gkb+bx]
    shr bx, 1
.bn:
    inc bx
    cmp bx, INST_MAX
    jb .b
    pop bx
    ret

tm_progtot:
    push bx
    xor ax, ax
    xor bx, bx
.p:
    call tm_ispkg
    jc .n
    shl bx, 1
    add ax, [tm_gkb+bx]
    shr bx, 1
.n:
    inc bx
    cmp bx, INST_MAX
    jb .p
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_sysitems - System's parts, in order: IVT+BIOS, Code+data, Stacks, FAT
;               snap, the fixed spans above 1MB, Kernel, each driver, each
;               built-in
; in:  DL = the indent (1 at the top level, 2 inside System's group), DH = 1
;      when each OWNER is a group that opens into its claims (the SYSTEM
;      view), 0 when it is an item showing its total (inside System's group in
;      the PROGRAMS view)
; out: nothing (all registers preserved)
;
; The parts below 640K are the kernel's own ladder (SPEC.md 20.9) and between
; them they are the System row's conventional figure: Code+data takes the
; half-KB IVT+BIOS gave up (SPEC.md 28.4.7). Above 1MB, kern_mega names each
; fixed span when it is asked (OSAPI_XMEM_WALK, XMW_FIXED) and anything else
; is one 'Above 1MB' row (KERN-MEGA-PLAN 14.70.2).
; -----------------------------------------------------------------------------
tm_sysitems:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor bx, bx                  ; no square unless a row says otherwise
    mov si, tm_s_low            ; IVT+BIOS, at 0000
    mov ax, [tm_lowkb]
    xor cx, cx
    call tm_item
    call tm_sysk                ; Code+data: the kernel's code and tables,
    xchg ax, cx                 ; and the half-KB (28.4.7). It starts at
    add cx, [tm_kb+SK_IMG]      ; KERNEL_SEG, and gives its ADDR column up to
    sub cx, [tm_kb+SK_KERN]     ; its name, which is nine characters - a
    xchg ax, cx                 ; separator and an address would leave eight
    mov si, tm_s_bimg           ; inside System's group
    mov cx, 0FFFFh
    or dh, dh                   ; its square is the kernel's gray - but not
    jz .img                     ; inside System's group, whose own row wears
    mov bx, tm_pat_gray         ; it already
.img:
    call tm_item
    mov bx, tm_pat_buf          ; the stacks and the FAT window: the part of
    mov cx, 0FFFFh              ; the kernel that is scratch, banded on the
    mov si, tm_s_bstk           ; map in its own texture
    mov ax, [tm_kb+SK_STK]
    add ax, [tm_kb+SK_STK0]
    call tm_item
    mov si, tm_s_bfat
    mov ax, [tm_kb+SK_FAT]
    call tm_item
    xor bx, bx

    ; --- above 1MB, the fixed spans under the pool ----------------------------
    cmp word [tm_hifix], 0
    je .owners
    mov si, tm_xw               ; the walk's own names for them, if it gave
    mov di, tm_xgid             ; any (BX counts them)
    mov cx, [tm_xwn]
.fx:
    jcxz .fxd
    cmp byte [di], TMG_FIX
    jne .fxn
    inc bx
    push si
    push cx
    mov al, [si+XMW_OWN]
    mov cx, [si+XMW_KB]
    sub al, XMW_HMA
    mov ah, TMX_NREC
    mul ah
    add ax, tm_xnames
    mov si, ax
    xchg ax, cx                 ; AX = KB
    mov cx, 0FFFFh
    push bx
    xor bx, bx
    call tm_item
    pop bx
    pop cx
    pop si
.fxn:
    add si, XMW_SIZE
    inc di
    dec cx
    jmp short .fx
.fxd:
    or bx, bx
    jnz .owners
    mov si, tm_s_bhi            ; ...or one row, named no further
    mov ax, [tm_hifix]
    mov cx, 0FFFFh
    call tm_item

    ; --- the OWNERS: Kernel, each driver, each built-in ------------------------
.owners:
    xor bx, bx
    mov bl, TMG_KERN
    cmp word [tm_gkb+TMG_KERN*2], 0
    je .drv                     ; nothing the kernel claimed: no empty group
    mov si, tm_s_gkern
    call tm_owner
.drv:
    inc bx                      ; TMG_DRV.. one group per class that holds
    cmp bx, TMG_N               ; anything
    jae .blt
    push bx
    shl bx, 1
    cmp word [tm_gkb+bx], 0
    pop bx
    je .drv
    call tm_drvnam              ; SI = 'SOUND.DRV', in tm_nbuf
    call tm_owner
    jmp short .drv
.blt:
    xor bx, bx
.bl:
    cmp byte [tm_ist+bx], 0
    je .bln
    test byte [tm_iknd+bx], KIND_PKG
    jnz .bln
    call tm_iname
    call tm_owner
.bln:
    inc bx
    cmp bx, INST_MAX
    jb .bl
    jmp tm_pop_dsdcba

; tm_owner - one of System's owners: a GROUP that opens into its claims
; (DH = 1) or an ITEM with its total (DH = 0). in: BL = its group, SI = its
; name, DL = the indent. Keeps everything
tm_owner:
    push ax
    push bx
    push cx
    push dx
    xor bh, bh
    push bx
    shl bx, 1
    mov ax, [tm_gkb+bx]
    pop bx
    or dh, dh
    jz .item
    xor dx, dx                  ; no square: no band on the map is an owner's
    call tm_grp
    jc .o
    call tm_mbrs
    jmp short .o
.item:
    mov cx, 0FFFFh
    xor bx, bx
    call tm_item
.o:
    jmp tm_pop_dcba

; tm_drvnam - SI = the name of driver group BL (TMG_DRV + class), in tm_nbuf:
; the file's, 'SOUND.DRV', or 'Drivers' for class 0. Keeps the rest
tm_drvnam:
    push ax
    push cx
    push di
    mov al, bl
    sub al, TMG_DRV
    mov ah, 8
    mul ah
    add ax, tm_dnames
    mov si, ax
    mov di, tm_nbuf
    mov cx, 8
.c:
    lodsb
    or al, al
    jz .e
    mov [di], al                ; not stosb: ES is the kernel's in a callback
    inc di
    loop .c
.e:
    cmp bl, TMG_DRV
    je .z
    mov si, tm_s_drv
    call tm_copy
.z:
    mov byte [di], 0
    mov si, tm_nbuf
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_grp - a GROUP row: '+' or '-', the name, the total and a legend square
; in:  BL = the group, SI = its name, AX = its KB, DX = its square's pattern
;      or 0
; out: CF = 0 the group is OPEN (its members follow), CF = 1 it is closed;
;      every register preserved
;
; A heading is never left on the foot of a column (tm_mrow_nolast, SPEC.md
; 28.4): it is a label for what follows it. The square goes in the TIER
; column a group leaves empty (TMH_SQC), and keys the row to a band on the
; map the way the memory view's squares did.
; -----------------------------------------------------------------------------
tm_grp:
    push ax
    push cx
    push di
    call tm_mrow_nolast
    call tm_mrow_open
    mov [tm_cgrp], bl
    mov byte [di], '+'
    call tm_isopen
    jc .mk
    mov byte [di], '-'
.mk:
    pushf
    inc di
    mov cx, TMH_GNAMEC
    call tm_copyn               ; space-padded to the SIZE column
    call tm_kcol
    or dx, dx
    jz .nosq
    mov word [tm_sqox], TM_PEN + TMH_SQC * 8
    mov [tm_sqp], dx
.nosq:
    call tm_mrow_close
    popf
    pop di
    pop cx
    pop ax
    ret

; tm_isopen - CF = 0 when group BL is open, CF = 1 closed. Keeps the rest
tm_isopen:
    push ax
    push cx
    mov cl, bl
    mov ax, [tm_exp]
    cmp cl, 16
    jb .lo
    mov ax, [tm_exp+2]
    sub cl, 16
.lo:
    shr ax, cl
    shr ax, 1                   ; CF = the bit...
    cmc                         ; ...and set means open
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_item - an ITEM row: a part of System that is not a claim, a program's
;           total inside the Programs group, a 640K-1MB row
; in:  SI = its name, AX = its KB, CX = the segment for ADDR or 0FFFFh for
;      none, DL = the indent, BX = its square's pattern or 0
; out: nothing (all registers preserved)
;
; With an address the name runs to the ADDR column and the address fills it;
; without one the name has the ADDR column too. Either way the total lands
; under SIZE, where every group's and every claim's does.
; -----------------------------------------------------------------------------
tm_item:
    push ax
    push cx
    push dx
    push di
    call tm_mrow_open
    push cx
    mov cl, dl
    xor ch, ch
.ind:
    mov byte [di], ' '
    inc di
    loop .ind
    pop cx
    push cx
    mov dx, TMH_SIZEC
    cmp cx, 0FFFFh
    je .w
    mov dx, TMH_ADDRC - 1       ; ...and a separator before an address
.w:
    add dx, tm_str
    sub dx, di
    xchg cx, dx                 ; CX = the name's field, DX = the address
    call tm_copyn
    pop cx
    cmp cx, 0FFFFh
    je .kb
    mov byte [di], ' '
    inc di
    push ax
    xchg ax, cx
    call tm_put4x
    pop ax
.kb:
    call tm_kcol
    or bx, bx
    jz .close
    mov word [tm_sqox], TM_PEN + TMH_SQC * 8
    mov [tm_sqp], bx
.close:
    call tm_mrow_close
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_mbrs - every member of group BL, a row each: its claims, then its blocks
;           above 1MB
; in:  BL = the group; tm_classify's tables
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tm_mbrs:
    push ax
    push cx
    push si
    push di
    mov si, tm_claims
    mov di, tm_cgid
.c:
    mov al, [di]
    cmp al, bl
    jne .cn
    call tm_crow
.cn:
    add si, CLS_RECSZ
    inc di
    cmp di, tm_cgid + MEM_MAX
    jb .c
    mov si, tm_xw
    mov di, tm_xgid
    mov cx, [tm_xwn]
.x:
    jcxz .done
    cmp [di], bl
    jne .xn
    call tm_xrow
.xn:
    add si, XMW_SIZE
    inc di
    dec cx
    jmp short .x
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_crow - one CLAIM's row, indented under its group: TYPE, ADDR, SIZE, TIER
; in:  SI = the claim record, AL = its group
; out: nothing (all registers preserved)
;
; A driver's claim that is not its image is 'DrvBuf' (SPEC.md 28.4.6) - the
; group already knows whose it is, which on kern_mega includes a driver above
; 1MB whose claims carry its selector rather than a segment tm_htype could
; walk to.
; -----------------------------------------------------------------------------
tm_crow:
    push ax
    push bx
    push cx
    push di
    call tm_mrow_open
    mov word [di], '  '         ; TMH_IND
    inc di
    inc di
    mov bx, [si+CLS_SEG]
    cmp al, TMG_DRV
    mov ax, [si+CLS_OWN]
    jb .type                    ; (a mov writes no flags)
    cmp ax, MEM_K_DRV
    je .type
    push si
    mov si, tm_s_tdrvb
    mov cx, TMH_TYPEC
    call tm_copyn
    pop si
    jmp short .sep
.type:
    call tm_htype               ; AX = the owner, BX = the base
.sep:
    mov byte [di], ' '          ; the separator, and it is NOT decoration: a
    inc di                      ; type that uses all eight columns runs straight
                                ; into the segment without it ('Shortcut2000')
    mov ax, bx
    call tm_put4x
    mov ax, [si+CLS_PARA]
    call tm_hkb
    call tm_kcol
    mov byte [di], ' '
    inc di
    mov ax, [si+CLS_OWN]
    call tm_htier
    call tm_mrow_close
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_xrow - one block ABOVE 1MB's row: its TYPE by the owner byte, HIGH for
;           ADDR (a segment means nothing there - tm_inst_row's word for a
;           region placed above 1MB), its KB and HELD
; in:  SI = the walk's record
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tm_xrow:
    push ax
    push cx
    push si
    push di
    call tm_mrow_open
    mov word [di], '  '
    inc di
    inc di
    mov al, [si+XMW_OWN]
    push si
    mov si, tm_s_tdimg
    cmp al, TMX_DRVIMG
    je .t
    mov si, tm_s_tkern
    cmp al, TMX_KERN
    jae .t
    mov ah, al
    and ah, 0E0h
    mov si, tm_s_tdrvb
    cmp ah, 0C0h
    je .t
    mov si, tm_s_thicl
    cmp ah, 0A0h
    je .t
    mov si, tm_s_tregn
    cmp ah, 80h
    je .t
    mov si, tm_s_txmem
.t:
    mov cx, TMH_TYPEC
    call tm_copyn
    pop si
    mov word [di], ' H'
    mov word [di+2], 'IG'
    mov byte [di+4], 'H'
    add di, 5
    mov ax, [si+XMW_KB]
    call tm_kcol
    mov byte [di], ' '
    inc di
    push si
    mov si, tm_s_hheld
    call tm_copy
    pop si
    call tm_mrow_close
    pop di
    pop si
    pop cx
    pop ax
    ret

%endif

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
    push si

    mov si, [tm_pcolrows]       ; column 0's depth, and it is ONE PER PAGE:
                                ; the two lists start at different heights up
                                ; the content, so the number of rows one
                                ; column of the frame holds is two different
                                ; numbers. The process list is the LOWER (a
                                ; graph and a bar above it), and sharing a
                                ; depth is wrong in both directions - the heap
                                ; page, when it was a page, wrapped early and
                                ; left a map's height of white at the foot,
                                ; and the process list wrapped LATE and lost
                                ; every row after the first one [tm_ylim]
                                ; refused
%ifdef TMF_MEM
    cmp byte [tm_view], 0
    je .depth
    mov si, [tm_colrows]
.depth:
%endif

    push ax                     ; the index again, for the tm_maxrow test
    mov cx, [tm_cx]
    cmp ax, si
    jb .place                   ; inside column 0: BX and CX already stand

    sub ax, si                  ; ...otherwise which LATER column, and how far
    xor dx, dx                  ; down it
    div word [tm_col2rows]
    inc ax                      ; 0 was column 1
    cmp ax, [tm_cols]           ; ...which has to EXIST: [tm_maxrow] is ONE
    jb .col                     ; bound over three pages whose column 0 are not
    pop cx                      ; the same depth (SPEC.md 28.4), so it can name
    jmp short .nofit            ; an index this page has no column for - and the
.col:                           ; row then went a TM_COLW past the frame's right
                                ; edge, onto the desktop, which nothing clips
    add cx, TM_COLW             ; ...and it can only be column 1: tm_layout
                                ; makes ONE or TWO, so this is a step and
                                ; not a multiply (DX = the row within it)
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
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_mrow_open - start a list row: compute x and y, DI = the text buffer
; in:  [tm_mrow]
; out: DI = tm_str, [tm_rowx]/[tm_rowy]/[tm_mfit] set, and the row's square
;      and group cleared (tm_grp and tm_item set them after)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_mrow_open:
    push ax
    push bx
    mov ax, [tm_mrow]
    mov bx, TMM_ROW_Y           ; the list's first row, under the map, the
    sub bx, [tm_xoff]           ; captions and the header - and a row higher
                                ; with no XMS bar to sit under
    call tm_row_place
    mov byte [tm_mfit], 1
    jnc .fits
    mov byte [tm_mfit], 0
.fits:
    mov word [tm_sqp], 0        ; no square unless the row asks for one
    mov byte [tm_cgrp], 0FFh    ; ...and no group: a click on it does nothing
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
                                ; ABOVE the top of the view (SPEC.md 28.4.4):
                                ; composed, not drawn, and - the whole point -
                                ; NOT counted, so every [tm_mrow] below the
                                ; offset is a SCREEN row rather than a table
                                ; one. tm_rowck is indexed by where a row is
                                ; DRAWN, so a scroll reuses the cache instead
                                ; of invalidating it; subtracting the offset
                                ; down in tm_row_place would have left every
                                ; check word describing pixels that had moved
    cmp word [tm_hskip], 0
    je .live
    dec word [tm_hskip]
    jmp short .gone
.live:
    mov bx, [tm_mrow]           ; WHICH GROUP this screen row is, for a click
    cmp bx, TM_SHOWN            ; on it (tm_lclick) - what the walk put HERE,
    jae .rg                     ; so a scrolled list needs no arithmetic
    mov al, [tm_cgrp]
    mov [tm_rgid+bx], al
    shl bx, 1                   ; ...and WHICH SQUARE: one that came, went or
    mov ax, [tm_sqp]            ; changed texture is a change the text cannot
    cmp ax, [tm_rsq+bx]         ; show, the TIER column a square sits in being
    je .rg                      ; blank either side of it - so a row that
    mov [tm_rsq+bx], ax         ; scrolled or opened into this slot left the
    mov ax, [tm_mrow]           ; old square standing beside the new text.
    mov cl, TM_NCHUNK           ; The square's CHUNK is owed instead, which
    mul cl                      ; erases it with the row's own spaces
    add ax, TMH_SQC / TM_CHUNK
    shl ax, 1
    mov bx, ax
    mov word [tm_rowck+bx], 0
.rg:
    cmp byte [tm_mfit], 0       ; off the bottom: nothing was drawn, and
    je .forget                  ; forget what used to be here so it comes
    mov byte [di], 0            ; back if the window ever grows
    mov ax, [tm_rowx]           ; this list letters from tm_rowx+TM_PEN: the
    add ax, TM_PEN              ; pixels the memory view's TM_MPEN left for a
                                ; square are the scroll bar's at the other end
    mov [tm_penx], ax           ; now, and a square is in the TIER column
    mov bx, [tm_mrow]
    call tm_row_draw            ; chunked: only what changed, and only what
    jc .out                     ; the region lets us (SPEC.md 11.3/28)
    mov si, [tm_sqp]            ; something drew, so the legend square may
    or si, si                   ; have been erased with it - put it back
    jz .out
    call tm_sq_pat
    jmp short .out
.forget:
    ; A ROW is TM_NCHUNK check words, at row*TM_NCHUNK - which is where
    ; tm_row_draw puts them, and the only place that indexes this array.
    ; This zeroed ONE word at row*2, so it invalidated nothing it meant to
    ; and instead cleared a word belonging to an EARLIER row: row n reached
    ; chunk (2n mod TM_NCHUNK) of row (2n div TM_NCHUNK). Harmless-looking
    ; while the lists were shorter than the window and .forget almost never
    ; ran; on the heap page (SPEC.md 28.4) most rows are off the bottom on a
    ; small screen, so every refresh was spuriously dirtying a visible row's
    ; chunk - a redraw twice a second, for ever, of text that had not moved.
    mov ax, [tm_mrow]
    cmp ax, TM_SHOWN            ; a row past any screen's foot has no check
    jae .out                    ; words to forget (TM_SHOWN)
    mov cl, TM_NCHUNK
    mul cl                      ; AX = row * TM_NCHUNK (both are small)
    shl ax, 1
    add ax, tm_rowck
    mov bx, ax
    mov cx, TM_NCHUNK
    call tm_ckz
.out:
    inc word [tm_mrow]
.gone:                          ; ...a skipped row advances nothing at all
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_mrow_nolast - keep a HEADING off the foot of a column
;
; The list is column-major (tm_row_place), so a heading landing on a column's
; last row is separated from every row it heads: on CGA the heap page put
; 'APPS' at the foot of column 0 and its one claim at the top of column 1. A
; heading is a label for what follows it and says nothing on its own, so it
; gives up that row and starts the next column instead.
;
; WHICH heading this catches is not fixed - it is whichever one the claim
; count happens to push there, and one claim fewer makes it a different one.
;
; It costs at most ONE row per column however many headings there are, because
; only one row index per column is that column's last and the list passes it
;
; Does nothing in the LAST column, where there is nowhere to push to and the
; foot of the column is simply the end of the list - a heading pushed there
; would be lost rather than moved.
;
; in:       [tm_mrow]
; out:      [tm_mrow] advanced over a blank row if it was about to be the last
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
tm_mrow_nolast:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tm_mrow]
    mov bx, [tm_colrows]        ; ...the depth of the column this row is in
    xor cx, cx                  ; CX = which column
    cmp ax, bx
    jb .have                    ; column 0, and BX is already its depth
    sub ax, bx
    mov bx, [tm_col2rows]       ; every later column is top-anchored and so
    xor dx, dx                  ; deeper - never 0, by tm_init
    div bx
    inc ax
    mov cx, ax                  ; 1 + how many later columns in
    mov ax, dx                  ; ...and the row within this one
.have:
    inc cx                      ; is there a column AFTER this one to push to?
    cmp cx, [tm_cols]
    jae .out                    ; no - see the header
    inc ax
    cmp ax, bx
    jne .out                    ; not this column's last row: nothing to do
    cmp word [tm_hskip], 0      ; ...and a row spent while the walk is still
    jne .spend                  ; SKIPPING is not on the screen at all, so it
    inc word [tm_hpad]          ; is not one of the layout's either
.spend:
    call tm_mrow_open           ; spend it on a blank row, and DRAW it, so
    call tm_mrow_close          ; whatever used to be here is erased
.out:
    jmp tm_pop_dcba

; -----------------------------------------------------------------------------
; tm_rowfill - white the row band at [tm_rowx],[tm_rowy], one column wide
; in:  [tm_rowx], [tm_rowy]
; out: nothing (all registers preserved)
; Both views' row loops erase before they letter, and only once they know the
; row changed, so the rect lives in one place rather than in each of them.
; -----------------------------------------------------------------------------
; It had a twin, tm_lfill, which named the same band by a CONTENT-RELATIVE y
; for the caption lines above the list. Every caption is a tm_lrun now - the
; padding IS the erase (SPEC.md 27.2), because a caption band is exactly a
; glyph tall - so nothing erases a caption any more and the twin is gone.

%endif

; -----------------------------------------------------------------------------
; tm_capline - a caption line, gated on its content and drawn as one run
; in:  tm_str composed; BX = its check word; AX = its y, content-relative
; out: CF = 0 it was drawn; CF = 1 nothing was - the line is unchanged since
;      the last refresh, so the pixels on screen are already right, or a clip
;      edge crosses it, in which case the key tm_elchk just recorded would be
;      a lie that kept the line stale until its content moved again, so it is
;      cleared
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_capline:
    push cx
    push dx
    mov cx, ax
    mov dx, ax
    add dx, 7
    push ax
    call tm_rowsum
    call tm_elchk_y
    pop ax
    jc .out
    call tm_lrun                ; SPEC.md 28.5.1: placed, padded and drawn as
    jnc .out                    ; ONE opaque run - no fill, no blank interval
    mov word [bx], 0            ; (a mov: CF is still the clip's 1)
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_lrun - a caption line, placed and drawn as ONE OPAQUE RUN (SPEC.md 28.5.1)
;
; in:  AX = the line's content-relative y; tm_str composed and NUL-terminated
; out: CF = 0 it was drawn, CF = 1 the clip region refused it and NOTHING was
;      drawn - the caller must then clear its check word, or the line stays
;      stale behind whatever uncovers it
; clobbers: nothing else (flags)
;
; This replaces tm_lfill + a font_str at four call sites. The pair wrote every
; pixel of the band twice - white, then the glyphs over it - and left it BLANK
; in between, which on a 4.77MHz machine is visible (SPEC.md 6.6). Padding
; tm_str to the band's full width instead makes the run its own erase (27.2),
; so there is no fill and no blank interval.
;
; THE GEOMETRY HAS TO LINE UP EXACTLY and it is asserted rather than trusted:
; the band runs from the pen to TM_RW inclusive, and TM_LCELLS cells of 8px
; must cover precisely that. The two pixels tm_rowfill used to whiten to the
; LEFT of the pen are not covered and do not need to be - nothing ever writes
; there, so what is on them is the page's own white.
tm_lrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [tm_cx]
    mov [tm_rowx], cx
    add ax, [tm_cy]
    mov [tm_rowy], ax
    call tm_rowok
    jc .out

    push es                     ; pad to the band's width, in place - with
    push ds                     ; the string instructions, as tm_row_draw
    pop es                      ; pads a row (ES = our segment for them)
    mov di, tm_str
    mov cx, TM_LCELLS + 1
    xor al, al
    repne scasb                 ; DI just past the NUL (every line is at most
    dec di                      ; TM_LCELLS: the guards at TM_STRMAX), back
    inc cx                      ; onto it; CX >= 1 cells from there to the
    mov al, ' '                 ; band's end, inclusive
    rep stosb
    mov byte [tm_str + TM_LCELLS], 0
    pop es

    mov cx, [tm_cx]
    add cx, TM_PEN
    mov dx, [tm_rowy]
    mov si, tm_str
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    clc                         ; ...and CF = 0 says so. The pops below write
.out:                           ; no flags, so tm_rowok's CF survives them
    jmp tm_pop_dsdcba

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
    push bp                     ; BORROWED, and given back below: BP counts
    push es                     ; the chunks drawn, which rode the stack in
    push ds                     ; SI before - three pushes and pops a chunk
    pop es

    mov di, tm_str              ; pad to TM_NCHUNK*TM_CHUNK with SPACES, and
    mov cx, TM_NCHUNK * TM_CHUNK + 1    ; terminate one past the end
    xor al, al
    cld
    repne scasb                 ; DI just past the NUL, CX = bytes after it
    dec di                      ; ...back ONTO it: the pad overwrites it
    inc cx                      ; (>= 1: a row is at most the span, so the
    mov al, ' '                 ; NUL is found) SPACES, not zeros. font_run paints a space
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
    xor bp, bp                  ; BP = 0: nothing drawn yet
    mov di, tm_str              ; DI -> this chunk's characters, stepped by
                                ; TM_CHUNK below rather than multiplied out
.chunk:
    mov si, di                  ; the chunk's hash, which is all tm_chunksum
    mov cx, TM_CHUNK            ; was: tm_sumb over a counted span
    xor ax, ax
    call tm_sumb                ; AX = their hash
    mov bx, [tm_ckb]
    call tm_elchk               ; changed? (BX = this chunk's check word)
    jnc .draw
    call tm_dmg_hit             ; no - so its pixels are right UNLESS this
    jc .next                    ; paint's damage took them (SPEC.md 28.10.2)
.draw:
    call tm_row_lead            ; the FIRST chunk owns the band's LEAD-IN too
    jc .rect                    ; - and erasing it counts as a draw, because
    inc bp                      ; the legend square lives in it and the caller
                                ; has to know to put it back
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
    call tm_rowr                ; something
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

                                ; --- the chunk, as ONE opaque run ----------
                                ; AX..DX are dead after it: everything below
                                ; reloads what it uses, so nothing is saved
    mov cx, ax                  ; x
    mov dx, [tm_rowy]           ; y
    mov si, di                  ; DI -> this chunk's TM_CHUNK characters
    lea bx, [di+TM_CHUNK]       ; terminate at the chunk's end and put the
                                ; byte back after: the row is one 25-character
    mov ah, [bx]                ; string and font_run runs to a NUL. The
    mov byte [bx], 0            ; buffer has room for the last chunk's, which
                                ; lands on tm_str[25] of TM_STRMAX = 28
    push ax
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = background: the erase
                                ; and the letters as one decision per cell
    call OSAPI_FONT_RUN         ; (preserves BX, the terminator's address)
    pop ax
    mov [bx], ah

    cmp word [tm_ci], TM_NCHUNK - 1     ; the LAST chunk owns the band's tail
    jne .gdone                          ; too - the pen is inset, so the row
                                        ; runs past the 25 cells and nothing
                                        ; else would scrub it
    TM_INKX CWHITE              ; before AX is the tail's x1. `ja .gdone`
                                ; below is never taken at TM_PEN/TM_MPEN (the
                                ; tail is at least a pixel on every page)
    mov ax, [tm_penx]
    add ax, TM_NCHUNK * TM_CW
    call tm_rowr
    mov bx, [tm_rowy]           ; y1/y2 FROM THE ROW, not from whatever the
    mov dx, bx                  ; run above left behind - terminating the
    add dx, 7                   ; chunk needed a register and BX was it
    cmp ax, cx
    ja .gdone
    call OSAPI_GFX_FILL
.gdone:
    popf
    jnc .kept
    mov bx, [tm_ckb]            ; an edge cut it, so some cell was skipped:
    mov word [bx], 0            ; forget the record and ask again next time
.kept:
    inc bp                      ; something was drawn
.next:
    add di, TM_CHUNK
    inc word [tm_ci]
    add word [tm_ckb], 2        ; the check word for the NEXT chunk
    cmp word [tm_ci], TM_NCHUNK
    jb .chunk

    or bp, bp
    pop es
    pop bp
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
; tm_rowr - CX = the right edge of the band the row being drawn may use
; in:  [tm_rowx], and [tm_hsb] filled by the paint that is running
; out: CX = the edge; every other register preserved (flags)
;
; TM_RW everywhere, less the scroll bar on the memory page (SPEC.md 28.4.4).
; BOTH of tm_row_draw's users are the scrub past the text - the last chunk's
; clip rect and the fill under it - so without this the twice-a-second refresh
; would white the bar out from under itself, one row at a time, for ever.
;
; It asks whether the BAND would reach the bar rather than which column this
; row is in, which is the same question with no second opinion about the
; layout in it: on a two-column screen the bar is beside column 1 and column 0
; keeps its whole band, and neither this nor tm_hsbset has to say so twice.
; That does mean tm_hsbset has to run BEFORE the rows, which is why both paint
; bodies call it there and tm_hbar calls it again for the count.
; -----------------------------------------------------------------------------
tm_rowr:
    mov cx, [tm_rowx]
    add cx, TM_RW
%ifdef TMF_HEAP
    cmp byte [tm_view], 0       ; the memory page's list has the bar
    je .out
    cmp cx, [tm_hsb]            ; the bar's x1: does this band reach it?
    jb .out
    mov cx, [tm_hsb]
    dec cx
.out:
%endif
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
    TM_INKX CWHITE              ; before AX is x1 (.none is never taken at
                                ; TM_PEN/TM_MPEN, both wider than 6)
    mov ax, [tm_rowx]
    add ax, 6
    mov cx, [tm_penx]
    dec cx
    cmp cx, ax
    jb .none                    ; the pen IS the band's left edge: no lead-in
    mov bx, [tm_rowy]           ; (CF=1 from the jb rides through the pops)
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL
    clc
.none:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.no:
    stc
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
    mov cx, ax
    add ax, 6
    add cx, TM_RW
    call OSAPI_WM_CLIP_TEST
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
; What a row's CHUNKS are keyed on (tm_row_draw, seed 0 over TM_CHUNK bytes),
; and what the maps are keyed on. A map's pixels are a function of one table -
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

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_qpeek - tm_elchk's answer WITHOUT recording it
; in:  AX = key, BX = a TMC_* check word's address
; out: CF = 1 unchanged, CF = 0 changed; the stored key is NOT touched
; clobbers: nothing else
;
; SPEC.md 28.11. tm_elchk records what it was asked about, which is right for
; a draw - it has just put those pixels on the glass. tm_quiet is not a draw:
; it is the question "is a paint owed at all", asked with no lock held and
; answered on intervals where the paint may then be REFUSED (the window is
; wholly covered and OSAPI_WM_CLIP_SET says so). Recording there spends the
; change on an interval that drew nothing, and the page sits stale until
; something else moves - 28.6.1 names that hazard and works around it by
; ordering the two calls; this removes it.
;
; It is the precondition for 28.11's repair: a debt that is consumed while
; covered cannot be replayed when the window comes back.
; -----------------------------------------------------------------------------
tm_qpeek:
    or ax, ax                   ; the never-zero rule is tm_elchk's and applies
    jnz .cmp                    ; here for the same reason: 0 is what
    inc ax                      ; tm_rowck_clear writes
.cmp:
    cmp ax, [bx]
    je .same
    clc
    ret
.same:
    stc
    ret

%endif

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

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_kcol - one right-aligned KB column: 3 digits + 'K' (tm_kfig, so MB past
;           999), or "   -" for zero
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
    call tm_kfig                ; 'nnnK', or 'd.dM' past 999 - a group above
    jmp short .out              ; 1MB can be megabytes (KERN-MEGA-PLAN 14.70.2)
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
    TM_INKX CBLACK
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
; fill indexes by the same y). A square is 6x6;
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
    jmp tm_pop_dcba

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

%endif

; -----------------------------------------------------------------------------
; tm_col - draw one graph column from the history ring
; in:  BX = column index 0..TM_GW-1; value 0..40 from tm_hist
; out: nothing
; clobbers: nothing (flags only)
;
; A ONE-COLUMN RUN, and that is the whole of it since SPEC.md 28.10.3: the
; white above the value and the black below are tm_grun's two rectangles at
; x1 = x2, drawing the identical pixels the pair of vlines here drew, because
; gfx_vline IS a one-column gfx_fill (kernel/vga12.inc). The worker's periodic
; path is the caller that wants a single column - the newly written one and
; the sweep gap - and it keeps it; what it no longer keeps is a second opinion
; about where the graph's rows are.
; -----------------------------------------------------------------------------
tm_col:
    push ax
    push cx
    mov al, [tm_hist+bx]        ; v is 0..TM_GH
    mov cx, bx                  ; the run is this column and no other
    call tm_grun
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_gapcol - draw one all-white column (the sweep gap at tm_pos)
; in:  BX = column index 0..TM_GW-1
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_gapcol:
    push ax
    push bx
    push dx

    TM_INKX CWHITE              ; first, while AX is still free
    mov ax, [tm_cx]
    add ax, 7
    add ax, bx
    mov bx, [tm_cy]
    mov dx, bx
    add bx, TM_GF_Y1 + 1
    add dx, TM_GF_Y2 - 1
    call OSAPI_GFX_VLINE

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_grun - one RUN of equal-height graph columns
; in:  BX = first column, CX = last column (inclusive), AL = height 0..TM_GH
; out: nothing
; clobbers: nothing (flags only)
;
; SPEC.md 28.10.3, and the whole of the graph's geometry lives here. White
; above the value, black below - self-backgrounding, so a run can be redrawn
; in place without a clear.
;
; OSAPI_GFX_FILL and never a vline, INCLUDING for a run of one: gfx_vline is a
; one-column gfx_fill in the kernel, so the narrow case is the same primitive
; on the same pixels and there is no case to choose between. That is what
; makes coalescing free rather than a trade - the worst case, every column a
; different height, issues exactly the calls this window issued before.
; -----------------------------------------------------------------------------
tm_grun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    xor ah, ah
    mov di, ax                  ; DI = v, across both calls: an X cell restores
                                ; every register but the routine's outputs
                                ; (SPEC.md 20.3), and gfx_fill has none
    mov si, [tm_cx]
    add si, 7                   ; the interior starts at content x = 7
    add bx, si                  ; BX = the run's first screen x
    add cx, si                  ; CX = ...and its last
    mov si, [tm_cy]             ; SI = content top

    cmp di, TM_GH               ; white: rows TM_GF_Y1+1 .. TM_GF_Y2-1-v, and
    jae .black                  ; none of it for a full-height column
    TM_INKX CWHITE              ; AL is spent: v is in DI
    push bx
    mov ax, bx
    mov bx, si
    add bx, TM_GF_Y1 + 1
    mov dx, si
    add dx, TM_GF_Y2 - 1
    sub dx, di
    call OSAPI_GFX_FILL
    pop bx
.black:
    or di, di                   ; black: rows TM_GF_Y2-v .. TM_GF_Y2-1, and
    jz .done                    ; none of it for an empty one
    TM_INKX CBLACK
    mov ax, bx
    mov bx, si
    add bx, TM_GF_Y2
    sub bx, di
    mov dx, si
    add dx, TM_GF_Y2 - 1
    call OSAPI_GFX_FILL
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_graph - the history graph's frame, and the columns THIS PAINT OWES
; in:  [tm_cx]/[tm_cy], [tm_hist], [tm_pos], [tm_dmg]; the gfx lock held
; out: nothing
; clobbers: nothing (flags only)
;
; SPEC.md 28.10.3. The loop this replaces walked all TM_GW columns on every
; paint, whatever the damage said, at up to two primitive calls each - 432 of
; them for a 216x40 box, 334 ms of a Hercules and 127 of a VGA - so uncovering
; the RAM bar underneath redrew a graph nothing had touched.
;
; Two questions, in the order that makes the second one cheap: does the damage
; cross our BAND at all, and then WHICH COLUMNS of it does it cross. The x
; half is done in SCREEN coordinates and unsigned throughout, which is
; tm_dmg_hit's own reason - tm_dmg_all's 0xFFFF is a maximum only as long as
; nothing subtracts from it or reads it as -1.
; -----------------------------------------------------------------------------
tm_graph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov cx, TM_GF_Y1
    mov dx, TM_GF_Y2
    call tm_dmg_yhit
    jc .done                    ; the damage never reached the graph at all

    call tm_frame

    mov ax, [tm_cx]             ; --- the damaged span, clamped to the interior
    add ax, 7                   ; AX = the interior's first screen x, kept
    mov si, ax                  ; SI = the span's first...
    lea di, [si+TM_GW-1]        ; DI = ...and its last
    mov dx, [tm_dmg+0]
    cmp dx, si
    jbe .lo                     ; the damage starts left of us: from column 0
    mov si, dx
.lo:
    mov dx, [tm_dmg+4]
    cmp dx, di
    jae .hi                     ; ...and runs past our right: to the last
    mov di, dx
.hi:
    cmp si, di
    ja .done                    ; it crossed our band beside us, not over us
    sub si, ax                  ; SI = the first column owed
    sub di, ax                  ; DI = the last

    mov bx, si
.run:
    cmp bx, [tm_pos]
    jne .grow
    call tm_gapcol              ; the sweep gap: one column, its own shape
    inc bx
    jmp short .next
.grow:
    mov al, [tm_hist+bx]        ; AL = this run's height...
    mov cx, bx                  ; CX = ...and its last column so far
.wider:
    cmp cx, di
    jae .emit                   ; the damage ends here
    mov si, cx
    inc si
    cmp si, [tm_pos]
    je .emit                    ; the sweep gap breaks a run...
    cmp al, [tm_hist+si]
    jne .emit                   ; ...and so does a different height
    mov cx, si
    jmp short .wider
.emit:
    call tm_grun
    mov bx, cx
    inc bx
.next:
    cmp bx, di
    jbe .run
.done:
    jmp tm_pop_dsdcba

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
    mov al, [tm_load]           ; 0..100, then '%' and the gap
    call tm_cpucol
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

    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_txt_ram - the RAM readout line (tm_txt_ram_y draws it at any y - the
;              memory view puts it at the top, SPEC.md 28)
; in:  [tm_usedk], [tm_totkb]; tm_txt_ram_y: AX = content-relative y
; out: nothing
; clobbers: nothing (flags only)
;
; **It carries the HEAP figures too**, in ONE string and one font_str. Two
; draws on one line would need two check words and a fill that belongs to
; neither; as one string it is one key, and the key is exact.
;
; ON BOTH PAGES, and the first is the one that matters. It used to be the
; memory view's alone, which left the process page - the one a launch opens
; on, and the ONLY one on the small build (28.12) - saying how much of the
; machine was used and not how much a program could still claim. On a 128KB
; machine those are different questions by tens of KB: RAM's total is int
; 12h, and the kernel is not heap. The figures are already in [tm_kb] on
; every sample, so the line costs a string copy and a tm_kpair.
;
; The claim SWATCH in the two-space gap is the memory view's alone, because
; it keys the claim texture of that view's map - the process page has no map,
; so a square there would be a legend for nothing and the gap stays blank.
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
%ifdef TMF_MEM
    cmp byte [tm_pm], 0         ; kern_mega in protected mode: ONE pair, the
    je .split                   ; whole machine (KERN-MEGA-PLAN 14.70.2)
    mov si, tm_s_rampm
    call tm_copy
    mov ax, [tm_usedk]
    add ax, [tm_hiused]
    mov bx, [tm_totkb]
    add bx, [tm_hitot]
    call tm_mpair2              ; 'u.uu/t.ttM'
    jmp short .heapd
.split:                         ; ...every other machine: RAM and HEAP, split
%endif
    mov si, tm_s_ram
    call tm_copy
    mov ax, [tm_usedk]
    mov bx, [tm_totkb]
%ifdef TMF_MEM
    cmp byte [tm_hion], 0       ; kern_big with a store: the whole machine,
    je .ramk                    ; whose total is past three digits of KB, in
    add ax, [tm_hiused]         ; MB to one place - the same eight characters.
    add bx, [tm_hitot]          ; Its HEAP stays the conventional heap, in KB:
    call tm_mpair               ; the store is not memory a package runs in
    jmp short .ramd
.ramk:
%endif
    call tm_kpair               ; 'nnn/nnnK' - one K, not two, because the
                                ; memory view fits a second pair on this line
                                ; and eight characters is what it costs
%ifdef TMF_MEM
.ramd:
%endif
    mov si, tm_s_heap           ; ...whose two leading spaces are where the
    call tm_copy                ; memory view's claim swatch goes
    mov ax, [tm_kb+SK_CLAIM]    ; the claimed half EXCLUDES the purgeables,
    mov bx, [tm_kb+SK_HEAP]     ; because no row below shows one (28.4.1) -
    call tm_kpair               ; and it is SK_CLAIM that does the excluding
%ifdef TMF_MEM
.heapd:
%endif
                                ; now, so the two pairs on this one line
                                ; cannot disagree about what claimed MEANS:
                                ; RAM's used half is SK_KERN + this word
    mov bx, tm_elck + 2*TMC_LINE    ; neither view puts this line at the same
    mov ax, [tm_liny]           ; y, so the band comes off the variable the
    call tm_capline             ; draw reads (SPEC.md 28.10.2)
    jc .out                     ; unchanged, or clipped: nothing was drawn
%ifdef TMF_MEM
    cmp byte [tm_view], 0
    je .out
    mov word [tm_sqox], TMM_HSQ_X   ; ...and the claim legend square, last, over
    cmp byte [tm_pm], 0             ; (after kern_mega's one pair, which has no
    je .sq                          ; gap: TMM_HSQ_X is inside its figures)
    mov word [tm_sqox], TMM_HSQ_XPM
.sq:
    mov si, tm_pat_clm              ; the gap the run just lettered white.
    call tm_sq_pat                  ; [tm_rowx]/[tm_rowy] are tm_lrun's - it
                                    ; places the band before it draws it
%endif
.out:
    jmp tm_pop_dsdcba

%if TMM_XB_Y2 - TMM_XB_Y1 != TM_BAR_Y2 - TM_BAR_Y1
  %error "taskmgr: the RAM and XMS bars are drawn by one body at one height"
%endif
%ifdef TMF_MEM
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
; ON A MACHINE WITH NO STORE THERE IS NO BAR AT ALL, and that REVERSES what
; this comment used to say. The old reasoning was that "an absent bar would
; read as a missing feature rather than an empty pool", so tier 0 got an empty
; rectangle beside 'XMS 0/ 0K'. Looked at on the target machine that is the
; wrong way round: it is twenty-two pixels of window devoted to saying that a
; thing which cannot exist here does not exist, and an empty frame reads as a
; reading of zero rather than as an absence. The pool's SIZE - not its free
; figure - decides, once, in tm_init, and everything below rises to fill the
; space (TMM_XSHIFT). SPEC.md 31.10.1 is the precedent: a Display page with one
; adapter on it is not drawn either.
;
; The FRAME is still drawn by tm_draw_mem and not here, exactly as the perf
; view draws the RAM bar's: a zero width matches the cleared check word, so
; this body skips on the very first paint, and a frame drawn inside the gate
; would never appear at all. tm_draw_mem carries the same [tm_xoff] test.
; -----------------------------------------------------------------------------
tm_xbar:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp word [tm_xoff], 0       ; no store: no bar, and the rows are DOWN there
    jne tm_barfill.done         ; now - ungated this would paint the interior
                                ; across the process list
    mov si, [tm_xbarw]
    mov bx, tm_elck + 2*TMC_XBAR
    mov cx, TMM_XB_Y1
    jmp short tm_barfill
%endif

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
    mov si, [tm_barw]           ; the bar IS its width: same width, same two
    mov bx, tm_elck + 2*TMC_BAR ; rectangles, same pixels
    mov cx, TM_BAR_Y1
                                ; ...and on into the body both bars share
; tm_barfill - the body of tm_bar and tm_xbar, entered by a jump with their
; five registers pushed: SI = the black run, BX = the element's check word,
; CX = the frame's top, content-relative
tm_barfill:
    mov ax, si
    mov dx, cx
    add dx, TM_BAR_Y2 - TM_BAR_Y1
    call tm_elchk_y
    jc .done
    mov bx, [tm_cy]
    add bx, cx
    inc bx                      ; the interior's top row...
    mov dx, bx
    add dx, TM_BAR_Y2 - TM_BAR_Y1 - 2   ; ...and its bottom
    mov cx, [tm_cx]
    add cx, 7                   ; CX = the interior's left, so AL is free for
    or si, si                   ; the pen at both fills
    jz .rest
    TM_INKX CBLACK
    mov ax, cx
    add cx, si
    dec cx                      ; x2 = 7 + barw - 1
    call OSAPI_GFX_FILL
    inc cx                      ; CX = 7 + barw, the white run's x1
.rest:
    cmp si, TM_GW
    jae .done
    TM_INKX CWHITE
    mov ax, cx                  ; x1 = 7 + barw
    mov cx, [tm_cx]
    add cx, TM_RW - 1
    call OSAPI_GFX_FILL
.done:
    jmp tm_pop_sdcba

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
    mov al, 2                   ; free...
    cmp byte [tm_ist+si], 0
    je .cls
    mov al, [tm_iknd+si]        ; ...else KIND_PKG, bit 7, as the class bit:
    rol al, 1                   ; 1 a package, 0 a built-in
    and al, 1
%if KIND_PKG != 0x80
  %error "taskmgr: tm_order rotates KIND_PKG down from bit 7"
%endif
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
    mov bx, si                  ; BX = the LOGICAL row: a screen too short for
    call tm_pshort              ; every row has no IVT+BIOS row, and System
    adc bx, 0                   ; carries its 2K instead (SPEC.md 28.4.7)
    cmp bx, 1
    ja .inst
    je .sys

    ; row 0: IVT+BIOS - no state, no CPU, its KB (SPEC.md 28.4.7, 28.4.8)
    push si
    mov si, tm_s_lowrow
    call tm_copy
    pop si
    mov ax, [tm_lowkb]
    call tm_memcol_kb
    jmp .draw

.sys:                           ; row 1: the kernel - task 0 minus everything
    push si                     ; debited to an instance
    mov si, tm_s_sys
    xor al, al                  ; the one row that is never indented
    call tm_namef
    mov si, tm_s_rdy            ; task 0 never sleeps; it shows "run" only
    cmp byte [tm_self], 0       ; when it happened to be current at snapshot
    jne .sysst
    mov si, tm_s_run
.sysst:
    call tm_copy                ; (the separator is in the string)
    pop si
    mov al, [tm_pct]            ; task 0's share: tm_pct is by instance + 1
    call tm_cpucol
    call tm_sysk                ; the kernel, whole KB, rounded DOWN - the row
                                ; above carries its tail (SPEC.md 28.4.7), so
                                ; the rows still sum to the RAM line's used
    call tm_pshort              ; ...and when there is no such row, System
    jnc .nofold                 ; carries IVT+BIOS itself
    add ax, [tm_lowkb]
.nofold:
    call tm_hisysk              ; + what it holds above 1MB (SPEC.md 28.4.8)
    add ax, [tm_kb+SK_KCLAIM]   ; + the kernel's OWN heap claims (SPEC.md
                                ; 42.2): the save-under and the clipboard.
                                ; A package's claim belongs to its own row.
    call tm_memcol_kb
    jmp .draw

.inst:
    dec bx                      ; logical row - 2: IVT+BIOS and System are
    dec bx                      ; above the first instance
    mov bl, [tm_ord+bx]         ; BX = the instance this ROW shows, held to
    xor bh, bh                  ; the end; [tm_rowi] is the same thing for the
    mov [tm_rowi], bl           ; stretches where BX is needed for something
    cmp byte [tm_ist+bx], 0
    je .free

    push si                     ; name: the I_NAME snapshot, 7 chars
    call tm_iname
    mov al, [tm_iknd+bx]        ; a built-in sits indented under System, a
    not al                      ; package at the top level with it
    and al, KIND_PKG
    call tm_namef
    pop si

    push si                     ; state (SPEC.md 28)
    mov si, tm_s_die
    cmp byte [tm_ist+bx], 2
    je .stput
    mov si, tm_s_evt
    mov bl, [tm_itsk+bx]        ; BX = the owning task's slot (reloaded from
    cmp bl, MAX_TASKS           ; [tm_rowi] below): 0xFF = no worker, it
    jae .stput                  ; lives in callbacks only
    mov si, tm_s_run
    cmp bl, [tm_self]
    je .stput
    mov si, tm_s_slp
    xor bh, bh
    cmp byte [tm_state+bx], 2
    je .stput
    mov si, tm_s_rdy
.stput:
    call tm_copy                ; (the separator is in the string)
    pop si

    mov bl, [tm_rowi]           ; share, right-aligned in 3, then '%'
    xor bh, bh                  ; (tm_pct is indexed by instance + 1, not by
    inc bx                      ; the display row - see tm_order)
    mov al, [tm_pct+bx]
    call tm_cpucol
    mov bl, [tm_rowi]           ; region bytes (0 for a built-in kind)...
    xor bh, bh
    shl bx, 1
    mov ax, [tm_isz+bx]
    add ax, 1023                ; ...as KB, rounded up; 0 stays 0, and the
    rcr ax, 1                   ; carry kept makes 0xFFFF 64 (SPEC.md 20.1.1)
    mov cl, 9
    shr ax, cl
    add ax, [tm_ikb+bx]         ; ...plus everything it holds off the claim
    call tm_slotxkb             ; heap (SPEC.md 50.5), and above 1MB (28.4.8).
                                ; The MEM column is what
                                ; this app COSTS, and a package that claimed a
    call tm_memcol_kb0          ; canvas costs far more than its region - the
    jmp .draw                   ; same sum the System row already shows

.free:                          ; free slot: one constant row
    push si
    mov si, tm_s_freerow
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
    cmp si, TM_PROWS
    jb .row
.rdone:

    jmp tm_pop_dsdcba

; -----------------------------------------------------------------------------
; tm_pshort - can this screen NOT show every process row? (SPEC.md 28.4.7)
; out: CF = 1 when column 0 and the later columns hold fewer than TM_PROWS -
;      a CGA, where the list fitted exactly before IVT+BIOS joined it
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_pshort:
    push ax
    push dx
    mov ax, [tm_cols]
    dec ax
    mul word [tm_col2rows]
    add ax, [tm_pcolrows]
    cmp ax, TM_PROWS            ; CF = capacity < TM_PROWS
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_sysk - the System row's kernel figure: whole KB, rounded DOWN (SPEC.md
;           28.4.7). Everything not the heap, less IVT+BIOS's constant 2K
; in:  [tm_totkb], [tm_kb]
; out: AX = KB
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_sysk:
    mov ax, [tm_totkb]
    sub ax, [tm_kb+SK_HEAP]
    sub ax, [tm_lowkb]
    ret

; -----------------------------------------------------------------------------
; tm_ebda - the BIOS's extended data area, KB (SPEC.md 28.4.8): BX = its size
;           when it sits exactly at the top int 12h reports and says its own
;           size in its first byte, else 0 - only what the BIOS states is
;           counted (KERN-MEGA-PLAN 14.1 answer 10)
; in:  AX = int 12h's KB
; out: BX = KB
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_ebda:
    push cx
    push es
    xor bx, bx
    cmp ax, 640                 ; the area is under 640K or there is none
    jae .out
    mov cx, 0x0040
    mov es, cx
    mov cx, [es:0x0E]           ; 40:0E, the area's segment...
    push ax
    push cx
    mov cl, 6
    shl ax, cl                  ; ...which must be int 12h's top: KB x 64
    pop cx
    cmp cx, ax
    pop ax
    jne .out
    mov es, cx                  ; (a multiple of 64 paragraphs: on kern_mega's
    mov bl, [es:0]              ; identity grid too) - its size, KB
    mov cx, 640
    sub cx, ax                  ; no bigger than the gap to 640K
    cmp bx, cx
    jbe .out
    xor bx, bx
.out:
    pop es
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_cpucol - the CPU column: share right-aligned in 3, then '%', then the
;             gap MEM needs after it - TM_CPUC characters, not four
; in:  AL = share 0..100, DI = dest
; out: DI advanced by TM_CPUC
; clobbers: AH (every caller reloads AX after it)
; -----------------------------------------------------------------------------
tm_cpucol:
    xor ah, ah
    call tm_put3
    mov word [di], '% '         ; '%' and the gap MEM needs: 100% against 335K
    inc di                      ; with nothing between reads as one number
    inc di
    ret

; -----------------------------------------------------------------------------
; tm_memcol_kb0 - the MEM column: KB right-aligned in 3, then 'K'; 0 KB means
;                 the instance owns nothing at all (no region, no claim) and
;                 shows "   -" instead of a misleading 0 (SPEC.md 28)
; tm_memcol_kb  - the same for a caller whose KB is never 0 - the System row,
;                 which adds the kernel's own claims and so cannot be a byte
;                 count. (There was a byte-count entry, `tm_memcol`; nothing
;                 had called it since the MEM column became KB.)
; in:  AX = KB, DI = dest
; out: DI advanced by 4
; clobbers: AX (both callers go straight to .draw, which reloads it)
; -----------------------------------------------------------------------------
tm_memcol_kb0:
    or ax, ax
    jnz tm_memcol_kb
    mov ax, '  '
    mov [di], ax
    mov ax, ' -'
    mov [di+2], ax
    add di, 4
    ret
tm_memcol_kb:
    call tm_put3
    mov byte [di], 'K'
    inc di
    ret

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
    push cx
    or al, al
    jz .noind
    mov byte [di], ' '
    inc di
.noind:
    mov cx, TM_NAMEC
    call tm_copyn               ; (keeps AL, the indent flag)
    mov byte [di], ' '
    inc di
    or al, al
    jnz .out
    mov byte [di], ' '          ; not indented: the column the indent would
    inc di                      ; have taken is padding instead
.out:
    pop cx
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

%ifdef TMF_MEM
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

%endif

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

%ifdef TMF_MEM
; -----------------------------------------------------------------------------
; tm_hikb - RAM above 1MB (KERN-MEGA-PLAN 13.6, SPEC.md 28.4.8), and whose
; in:  [tm_kb] (tm_getkb)
; out: [tm_hitot] = KB above 1MB in all: the pool and, under its base, the
;      kernel's tables and its .cold; [tm_hiused] = KB of it in use;
;      [tm_hifix] = the KB under the pool's base; the pool's blocks in tm_xw
;      ([tm_xwn] of them, for tm_slotxkb and the list), led on kern_mega by a
;      record per fixed area under the base (XMW_HMA..XMW_PAD)
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_hikb:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call OSAPI_XMEM_CAPS        ; AX = free, DX:CX = the pool's linear base
    sub dx, 0x10                ; ...less 1MB: the kernel's own, below it
    mov bx, cx
    mov cl, 10
    shr bx, cl
    mov cl, 6
    shl dx, cl
    or dx, bx                   ; DX = KB between 1MB and the pool
    mov bx, [tm_kb+SK_XMS]
    sub bx, ax
    add bx, dx
    mov [tm_hiused], bx
    mov ax, 0xFFFF              ; THE TOTAL IS HELD to what still sums in 16
    sub ax, [tm_totkb]          ; bits with the conventional KB, as every
    mov bx, [tm_kb+SK_XMS]      ; reader of it does: an AH=88h answer near
    add bx, dx                  ; 64MB would wrap 'RAM' and the bar
    jc .cap
    cmp bx, ax
    jbe .tot
.cap:
    mov bx, ax
.tot:
    mov [tm_hitot], bx
    cmp [tm_hiused], bx         ; ...and what is in use, inside it
    jbe .usd
    mov [tm_hiused], bx
.usd:
    mov [tm_hifix], dx          ; the span under the pool, which the list's
                                ; System itemises (tm_sysitems)
    push ds
    pop es
    mov di, tm_xw
    mov cx, TM_XWN
    mov bx, XMW_FIXED           ; ...and NAMED, on a kernel that names it:
    call OSAPI_XMEM_WALK        ; kern_mega puts a record per fixed area in
    jnc .walked                 ; front of the blocks (KERN-MEGA-PLAN 14.70.2)
    xor ax, ax                  ; and every other kernel ignores BX
.walked:
    mov [tm_xwn], ax
    mov cx, ax                  ; the SYSTEM's share above 1MB: everything
    mov di, tm_xw               ; under the pool's base (DX, the kernel's
.sys:                           ; tables and .cold) and every block the
    jcxz .sysd                  ; kernel or a driver owns - owner C0h and up
    mov al, [di+XMW_OWN]        ; (SPEC.md 28.4.8): the top of RAM is RAM
    cmp al, 0xC0                ; here, so what holds it is billed for it
    jb .sysn
    cmp al, XMW_HMA             ; ...but NOT a fixed area's record, which is
    jb .sysa                    ; a name for KB already in DX
    cmp al, XMW_PAD
    jbe .sysn
.sysa:
    add dx, [di+XMW_KB]
.sysn:
    add di, XMW_SIZE
    dec cx
    jmp .sys
.sysd:
    mov [tm_hisys], dx
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tm_slotxkb - AX += the KB slot BX/2 holds in the pool's blocks - the XMEM
; cells' (owner = the slot) and its OS88_HIGHMEM claims (0A0h | the slot) -
; protected mode only; its region is in SIZE already. Keeps everything else
tm_slotxkb:
    cmp byte [tm_hion], 0
    je .ret
    push cx
    push dx
    push si
    mov dx, bx
    shr dx, 1                   ; DL = the slot...
    mov dh, dl
    or dh, 0xA0                 ; ...DH = its claims' owner
    mov si, tm_xw
    mov cx, [tm_xwn]
    jcxz .done
.l:
    cmp [si+XMW_OWN], dl
    je .add
    cmp [si+XMW_OWN], dh
    jne .n
.add:
    add ax, [si+XMW_KB]
.n:
    add si, XMW_SIZE
    loop .l
.done:
    pop si
    pop dx
    pop cx
.ret:
    ret

; tm_mpair - 'u.u/t.tM' at DI, NUL-terminated: tm_kpair's eight characters
; in MB, for kern_big's whole-machine figure with a store. in AX = used KB, BX
; = total KB. out: DI on the NUL. Clobbers nothing else
; tm_mpair2 - 'u.uu/t.ttM', the same with TWO decimals: kern_mega's one pair
; (KERN-MEGA-PLAN 14.70.2), whose line has the room
tm_mpair2:
    push si
    mov si, tm_mfig2
    jmp short tm_mpair.go
tm_mpair:
    push si
    mov si, tm_mfig
.go:
    push ax
    call si
    mov byte [di], '/'
    inc di
    mov ax, bx
    call si
    mov word [di], 'M'
    inc di
    pop ax
    pop si
    ret
; tm_mfig - AX = KB -> three characters at DI: 'd.d' MB under 10MB, else
; the whole MB right-aligned. Clobbers nothing else
tm_mfig:
    push ax
    push cx
    push dx
    cmp ax, 10240
    jae .whole
    mov cx, 10
    mul cx                      ; DX:AX = KB x 10
    mov cx, 1024
    div cx                      ; AX = tenths of a MB, 0..99
    mov cl, 10
    div cl                      ; AL = MB, AH = the tenth
    add ax, '00'
    mov [di], al
    mov byte [di+1], '.'
    mov [di+2], ah
    add di, 3
    jmp short .o
.whole:
    mov cl, 10
    shr ax, cl
    call tm_put3
.o:
    pop dx
    pop cx
    pop ax
    ret
; tm_mfig2 - AX = KB -> 'd.dd' or 'dd.dd' MB at DI, DI past it: always two
; decimals, as the owner asked for kern_mega's one pair (a 286 stops at 16MB,
; so two digits of MB is the most there is). Truncated, never rounded up:
; 3,654K is 3.56, and a full 4MB is 4.00. Clobbers nothing else
tm_mfig2:
    push ax
    push cx
    push dx
    mov cx, 100
    mul cx
    mov cx, 1024
    div cx                      ; AX = the figure in hundredths
    mov cl, 100
    div cl                      ; AL = MB, AH = the hundredths
    mov dl, ah
    cmp al, 10
    jb .one
    aam                         ; AH = tens of MB, AL = units
    add ah, '0'
    mov [di], ah
    inc di
.one:
    add al, '0'
    mov [di], al
    mov byte [di+1], '.'
    mov al, dl
    aam                         ; AH = tenths, AL = hundredths
    add ax, '00'
    mov [di+2], ah
    mov [di+3], al
    add di, 4
    pop dx
    pop cx
    pop ax
    ret
; tm_hisysk - AX += [tm_hisys] in protected mode: what the system holds above
; 1MB, billed to System (SPEC.md 28.4.8). Keeps everything else
tm_hisysk:
    cmp byte [tm_hion], 0
    je .ret
    add ax, [tm_hisys]
.ret:
    ret
%else
tm_slotxkb:                     ; the small build has no memory view
tm_hisysk:
    ret
%endif

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
    mov word [di], 'K'          ; 'K' and the NUL in one store
    inc di                      ; DI -> the NUL, where the next copy lands
    pop ax
    ret

%ifdef TMF_MEM
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
    push cx
    mov cx, 5
    jmp short tm_putw

%endif

; -----------------------------------------------------------------------------
; tm_put3 - decimal 0..999 as 3 chars at DI, right-aligned, space-padded
; in:  AX = value, DI = dest
; out: DI advanced by 3
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_put3:
    push cx
    mov cx, 3
    cmp ax, 999                 ; out of contract, and a field keeps the LOW
    jbe tm_putw                 ; digits of a number too wide for it: pin it
    push ax
    mov ax, 999
    call dec_fld
    pop ax
    pop cx
    ret

; tm_putw - the field, CX wide; tm_put2/3/5 pushed CX and this pops it
tm_putw:
    call dec_fld
    pop cx
    ret

%define DEC_FLD                 ; tm_put2/3/5's fixed fields (SPEC.md 20.5.2)
%include "os88dec.inc"

; --- the shared scroll bar (SPEC.md 13.10) -----------------------------------
; AT THE END OF THE CODE, just before OS88_BSS, because the header and the
; icon block are at fixed offsets in the image (SPEC.md 20.2) and code emitted
; between them fails the icon macro's own offset assertion.
;
; BARONLY (SPEC.md 13.10.6.5): this window draws no standard button - its one
; click target is the content itself (tm_click) - so it pays for the bar and
; not for os88ui_btn, os88ui_glyph and four 12x12 bitmaps beside it.
;
; NO SBDRAG, and that is a refusal rather than an omission (PERFORMANCE.md
; rule 6). A scroll here is a LIST REPAINT: rows move between screen slots and,
; on a two-column layout, between COLUMNS, so there is no rectangle for
; OSAPI_GFX_SCROLL to blit and Note Pad's answer (SPEC.md 27.7.2) does not
; transfer. At SPEC.md 28.2's granularity that is ~20 rows of changed text,
; ~450 ms on the target machine - which is about what a page switch already
; costs and is fine for a deliberate click, and is unusable at one per pixel
; of thumb travel. The travel is two page-downs in any case: 47 rows, 22 of
; them on screen.
%ifdef TMF_HEAP
%define OS88UI_BARONLY
%define OS88UI_SCROLL
%else
%define OS88UI_NOBTN            ; the same statement without the bar: the small
                                ; build has no heap page and so no scroll bar,
                                ; and the About card draws no button either -
                                ; os88ui_btn, the glyph and the gesture were
                                ; ~760 bytes it never reached (apps size pass
                                ; 2b, SPEC.md 28.12)
%endif
%define OS88UI_ABOUT            ; ...and the standard About card (SPEC.md
%include "os88ui.inc"           ; 20.5.1), which draws no button either

    OS88_BSS TM_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
; All zero is the performance view, an empty history ring and no worker, which
; is exactly what a fresh launch wants. As a built-in this block had to be
; cleared by hand at every kinit, because .bss survives between instances of a
; kind; a package gets a fresh image and a zeroed bss per launch instead.

tm_win      equ os88_image_end     ; word: our window ptr

; --- what the API cells fill (SPEC.md 20.9) ----------------------------------
; Three buffers this module owns and the kernel writes through ES:DI.
; tm_snapshot is unpacked into the per-slot arrays below the moment it lands (see
; tm_sample); tm_claims and tm_kb are read where they sit.
tm_snapshot equ tm_win + 2
tm_state    equ tm_snapshot + SS_TSTATE ; T_STATE, read in place (tm_sample)
tm_self     equ tm_snapshot + SS_CUR    ; our own slot ([sch_cur] at snapshot)
tm_coop     equ tm_snapshot + SS_COOP   ; the scheduler mode at the snapshot     ; osapi_sys_snapshot: scheduler + instance table
%ifdef TMF_MEM
tm_claims   equ tm_snapshot + SYS_SNAPSHOT_SIZE   ; osapi_claim_snapshot: the claim map
tm_kb       equ tm_claims + CLAIM_SNAPSHOT_SIZE   ; osapi_sys_kb: the kernel's footprint and the
                                ; heap's totals, refreshed by tm_view_begin
                                ; and by every sample
%else
tm_kb       equ tm_snapshot + SYS_SNAPSHOT_SIZE   ; ...and the CLAIM MAP is
                                ; nobody's on this arm (SPEC.md 28.12): the
                                ; process list's own memory column comes off
                                ; the INSTANCE snapshot's SSI_KB, so 192 bytes
                                ; of buffer go with the two pages that read
                                ; mem_tab itself
%endif

tm_hist     equ tm_kb + SYSKB_SIZE   ; history ring: column heights 0..40,
                                ; TM_GW of them - the hand-chained map gave it
                                ; NINE MORE than the ring can ever use, which
                                ; is the sort of thing a literal hides and a
                                ; derivation cannot

; **EVERY OFFSET BELOW IS DERIVED, and that is the whole of what makes
; MAX_TASKS a constant and a rebuild.** It was a chain of hand-written
; literals - `tm_claims equ os88_image_end + 428` - so a kernel table changing
; size renumbered sixty-odd of them by hand, and two fields at the end are
; still sitting out of subject order because inserting one mid-block was too
; expensive to contemplate. Dropping MAX_TASKS from 12 to 10 shrank
; SYS_SNAPSHOT_SIZE by ten bytes and the `%error` guards that used to stand
; here fired, correctly, on every one below it.
;
; Each field now names the one before it plus that one's SIZE, and every size
; that depends on a kernel table says so - MAX_TASKS, INST_MAX, MEM_MAX,
; TM_ROWS, TM_NCK. Change the constant, rebuild, done. The guards are gone
; because there is nothing left for them to catch: the thing they checked is
; now the way the offsets are computed.
;
; Proven equal to the literals it replaced: with the one deliberate difference
; below held back, the assembled package was byte-for-byte the old one.
tm_pos      equ tm_hist + TM_GW   ; sweep index = next column to write
tm_lastcol  equ tm_pos + 2   ; column written by the latest sample
tm_idlec    equ tm_lastcol + 2   ; the interval's IDLE cycles, dword: the load
                                 ; meter's numerator (SPEC.md 28.7), shifted by
                                 ; the total's own count so the ratio is one scale
tm_idle     equ tm_idlec + 4  ; the idle task's slot, 0xFF = none answered
tm_load     equ tm_idle + 1   ; latest load, 0..100
tm_told     equ tm_load + 2   ; previous sch_cycles snapshot
tm_tdif     equ tm_told + MAX_TASKS * 4   ; per-task interval cycles
tm_pstate   equ tm_tdif + MAX_TASKS * 4  ; previous sample's T_STATE (appeared rule)
tm_iold     equ tm_pstate + MAX_TASKS  ; previous I_CYC snapshot
tm_ist      equ tm_iold + INST_MAX * 4  ; I_STATE snapshot
tm_pist     equ tm_ist + INST_MAX  ; previous sample's I_STATE (appeared rule)
tm_itsk     equ tm_pist + INST_MAX  ; I_TASK snapshot (owning task or 0xFF)
tm_isz      equ tm_itsk + INST_MAX  ; I_SIZE snapshot, bytes
tm_ispt     equ tm_isz + INST_MAX * 2  ; I_SPTR snapshot (memory view, SPEC.md 28)
tm_iknd     equ tm_ispt + INST_MAX * 2  ; I_KIND snapshot: the Builtins/Packages split
                                ; (SPEC.md 28) needs bit 7, and the CLAIM
                                ; column needs to know which owner word an
                                ; instance's claims carry (SPEC.md 50.2)
tm_ikb      equ tm_iknd + INST_MAX  ; ...and the KB it holds off the heap, which
                                ; the kernel derives from that same rule
%if tm_ikb + INST_MAX * 2 - tm_ist != TM_IHASH
  %error "taskmgr: the instance block tm_quiet hashes is no longer contiguous"
%endif
tm_iend     equ tm_ikb + INST_MAX * 2  ; (the end of the unpacked block)
%ifdef TMF_MEM
tm_mrow     equ tm_iend  ; the memory view's row cursor
tm_mfit     equ tm_mrow + 2  ; ...and whether the row it names is on screen
tm_sqox     equ tm_mfit + 1  ; ...and where its legend square starts
tm_sqp      equ tm_sqox + 2  ; ...and its texture, or 0 for no square. A
                                ; REQUEST, not a draw: tm_mrow_close erases
                                ; the band before it letters it, so a square
                                ; drawn while the row was being composed
                                ; would be erased again
tm_qkey     equ tm_sqp + 2  ; tm_quiet's key, held until a paint
                                ; actually happens (SPEC.md 28.6.1): the
                                ; question is asked with no lock and the paint
                                ; can still be REFUSED under it, so the answer
                                ; is recorded by whoever draws and not by
                                ; whoever asks
tm_rowx     equ tm_qkey + 2  ; the left edge of the row being drawn: tm_cx
%else                           ; for column 0, one TM_COLW on for column 1.
tm_rowx     equ tm_iend     ; The other arm closes over the row cursor, its
%endif                          ; fit flag, the legend square and tm_quiet's
                                ; key, there being no quiet page to hold one

%ifdef TMF_MEM
tm_xbarw    equ tm_rowx + 2  ; the XMS bar's black run, px (TMC_XBAR)
tm_penx     equ tm_xbarw + 2  ; where the row's text starts: the process
%else                           ; list letters from tm_rowx+6 and the memory
tm_penx     equ tm_rowx + 2     ; list from tm_rowx+16, and tm_row_draw has to
%endif                          ; be told which - drawing a process row at the
                                ; memory list's pen puts it 10px right of the
                                ; text it is replacing
tm_ci       equ tm_penx + 2  ; tm_row_draw's chunk index (BP is the task's
                                ; instance record and cannot be borrowed)
tm_ckb      equ tm_ci + 2  ; ...and the running check-word pointer, for
                                ; the same reason
tm_elck     equ tm_ckb + 2  ; the last drawn state of the non-row
                                ; elements, one word each (TMC_*), and
                                ; DIRECTLY in front of tm_rowck: the two are
                                ; cleared as ONE span (tm_rowck_clear), which
                                ; is why tm_rowck is defined off tm_elck's end
                                ; - nothing may go between them
tm_rowck    equ tm_elck + TMC_N * 2  ; the last drawn content of each row, a
                                ; word per CHUNK (tm_sumb's), plus one
                                ; virtual row for the CPU caption (TMR_CPU).
                                ; Shared by all THREE pages, so it is sized
                                ; for the deepest of them - the heap page's
                                ; TMH_ROWS (SPEC.md 28.4) - and a page switch
                                ; clears it whichever way it went.
                                ; tm_rowck_clear names this
                                ; and tm_elck separately; they used to be
                                ; cleared as one span on the strength of
                                ; being declared adjacent, which they have
                                ; not been for some time
%ifdef TMF_MEM
tm_view     equ tm_rowck + TM_NCK * TM_NCHUNK * 2  ; 0 = performance, 1 = memory, 2 = heap
tm_slow     equ tm_view + 1  ; byte: intervals since the last paint of a
                                ; SLOW page (SPEC.md 28.6). A byte and not a
                                ; word: TM_SLOW is 4. Directly after tm_view
                                ; so tm_click resets both with one word store;
                                ; the performance-only arm has no tm_slow at
                                ; all, that view having always painted every
                                ; interval
tm_rcyc     equ tm_slow + 1  ; per-ROW cycles: task + instance, dword
%else
tm_rcyc     equ tm_rowck + TM_NCK * TM_NCHUNK * 2  ; ...and with one page there
                                ; is no page to record: every `cmp byte
                                ; [tm_view], n` in the file is inside a gate
%endif
tm_pct      equ tm_rcyc + TM_ROWS * 4  ; per-row share, 0..100 (the rows'
                                       ; sum lives in registers, tm_sample)
tm_usedk    equ tm_pct + TM_ROWS  ; used RAM, KB
tm_barw     equ tm_usedk + 2  ; RAM bar black width, 0..TM_GW

; --- worked out once by tm_init, before the window exists --------------------
; These used to need saying out loud, because a built-in's .bss survives
; between instances of its kind and tm_kinit therefore cleared the block above
; by hand - leaving anything tm_init had computed at BOOT to be wiped by the
; first launch. It cost a divide by zero the first time the window was opened,
; tm_colrows being a divisor. A package has no such hazard: the bss is zeroed
; once per launch, before the entry proc runs, and tm_init runs inside it.
tm_cols     equ tm_barw + 2  ; process-list columns, 1 or 2 (SPEC.md 28.1)
tm_colrows  equ tm_cols + 2  ; rows in COLUMN 0, which starts under the maps
tm_pcolrows equ tm_colrows + 2  ; ...and the PROCESS list's own column 0, which
                                ; starts under the graph and the bar as well -
                                ; TM_ROW_Y against TMM_ROW_Y, 30px lower, so it
                                ; wraps EARLIER. Per page, for tm_hcolrows'
                                ; reason and with the opposite sign
tm_col2rows equ tm_pcolrows + 2  ; rows in each LATER column, which starts at the
                                ; top and so holds more - A DIVISOR, never 0
%ifdef TMF_HEAP
tm_totkb    equ tm_col2rows + 2  ; total conventional RAM (int 12h, boot)
%else                           ; (tm_maxrow lives in tm_hsb on this arm)
tm_maxrow   equ tm_col2rows + 2  ; rows this SCREEN shows, <= TMM_ROWS: derived
                                ; from [vid_dock_y0] so the window never
                                ; overlaps the dock (SPEC.md 28.1)
tm_totkb    equ tm_maxrow + 2  ; total conventional RAM (int 12h, boot)
%endif
tm_vw       equ tm_totkb + 2  ; the live screen width and the dock strip's
tm_vdock    equ tm_vw + 2  ; top row (osapi_video, SPEC.md 39.2), banked
                                ; at boot: the template's whole geometry is
                                ; derived from them
tm_cx       equ tm_vdock + 2  ; cached content origin (draw paths only,
tm_cy       equ tm_cx + 2  ; always under the gfx lock)
tm_ord      equ tm_cy + 2  ; tm_order: display row - 1 -> instance slot
tm_rowi     equ tm_ord + INST_MAX  ; ...the one tm_rows is drawing right now
tm_rowy     equ tm_rowi + 1  ; tm_rows/tm_rows_mem scratch: current row y
tm_ylim     equ tm_rowy + 2  ; ...and the lowest row top the frame holds
                                ; (tm_view_begin): the row lists are fixed-pitch
                                ; and nothing clips a draw to a window
tm_liny     equ tm_ylim + 2  ; tm_txt_ram_y scratch: the line's y
tm_str      equ tm_liny + 2  ; line-format scratch

tm_spawned  equ tm_str + TM_STRMAX  ; byte: 1 = this instance owns its
                                       ; worker. Latches on SUCCESS only, so
                                       ; every paint retries until a task slot
                                       ; frees up (SPEC.md 20.6)

; THE HEAP PAGE'S OWN STATE, and the chain closes over it when the page is
; not built (SPEC.md 28.12). Each group below is ONE `%ifdef`: the heap arm
; is the chain as it was, and the other arm is the same chain with the gated
; fields taken out, so the field after a group still names its predecessor in
; exactly one place per arm.
%ifdef TMF_HEAP
tm_avl      equ tm_spawned + 1  ; OSAPI_MEM_AVAIL's pair, banked once a
tm_maxrun   equ tm_avl + 2  ; refresh by tm_hsnap: total available,
                                       ; and the largest single run of it.
                                       ; mem_avail walks the claim table once
                                       ; per candidate, so it is not a call
                                       ; to make twice (SPEC.md 28.4.1)
tm_xoff     equ tm_maxrun + 2  ; 0, or TMM_XSHIFT on a machine with no
%else                                  ; store above 1MB: how far the memory
tm_xoff     equ tm_spawned + 1         ; view's header, rows and frame rise
%endif                                 ; where the XMS bar would have been.
                                       ; SET ONCE, by tm_init, and never from
                                       ; a draw - the frame is SIZED from it,
                                       ; so a value that could change under a
                                       ; live window would leave the height
                                       ; and the row placement disagreeing.
                                       ; The `%else` arm closes over tm_avl
                                       ; and tm_maxrun

%ifdef TMF_HEAP
tm_htop     equ tm_xoff + 2  ; the list's scroll position: the
                                       ; first row of the TABLE that is on
                                       ; screen (SPEC.md 28.4.4). Survives a
                                       ; page switch - coming back to where
                                       ; you were is the better behaviour, and
                                       ; tm_hclamp is what keeps it honest
tm_hskip    equ tm_htop + 2  ; ...how many rows THIS paint still owes
                                       ; the offset. tm_mrow_close spends one
                                       ; instead of drawing, and does not
                                       ; advance [tm_mrow] - which is what
                                       ; makes that a SCREEN row and leaves
                                       ; tm_rowck indexed by the thing it is
                                       ; drawn at
tm_hpad     equ tm_hskip + 2  ; ...and how many of the rows this paint
                                       ; drew were tm_mrow_nolast's BLANKS,
                                       ; which belong to the layout and not to
                                       ; the table (SPEC.md 28.4.4)
tm_hsb      equ tm_hpad + 2  ; the shared bar's seven-word block
                                       ; (SPEC.md 13.10.2): x1 y1 x2 y2, then
                                       ; total, fit, pos - and the first two
                                       ; of those ARE the page's own words:
tm_hrows    equ tm_hsb + 8   ; how many rows the last walk produced in
                                       ; all - the bar's `total`, and what
                                       ; tm_hclamp measures against...
tm_maxrow   equ tm_hsb + 10  ; ...and the rows this SCREEN shows, which
                                       ; is its `fit` (on the other arms
                                       ; tm_maxrow sits after tm_col2rows).
                                       ; os88ui_sbar only reads the block, so
                                       ; tm_hsbset copies neither. `pos` stays
                                       ; a COPY of tm_htop: tm_hscroll tells a
                                       ; move from an end stop by comparing them
tm_dmg      equ tm_hsb + 14  ; THIS PAINT'S DAMAGE, absolute, x1 y1 x2
%else                                  ; y2 (SPEC.md 28.10.2) - the rect
tm_dmg      equ tm_xoff + 2            ; tm_clear_owed filled, which is
%endif                                 ; exactly the rect that has to be
                                       ; lettered again. Read by tm_dmg_hit
                                       ; once per chunk and by nothing else

tm_abon     equ tm_dmg + 8   ; byte: the About card is up (SPEC.md 20.5.1)
tm_lowkb    equ tm_abon + 1  ; word: IVT+BIOS, KB - TM_LOWKB plus the BIOS's
                                ; extended data area (tm_init, SPEC.md 28.4.8)

%ifdef TMF_MEM
TM_XWN      equ 64              ; OSAPI_XMEM_WALK's records: kern_mega's pool
                                ; table is 96 (MX_NBLK) and a full INST_MAX
                                ; of programs takes ~60, plus the five fixed
                                ; areas in front (KERN-MEGA-PLAN 14.70.2). A
                                ; block past this is in no group and no total
tm_pm       equ tm_lowkb + 2    ; byte: CPU_F_PM - extended memory is RAM
tm_hion     equ tm_pm + 1       ; byte: RAM above 1MB is accounted - the pool
                                ; walks (SPEC.md 28.4.8): tm_hikb runs
tm_hiused   equ tm_hion + 1     ; KB above 1MB in use (tm_hikb)...
tm_hitot    equ tm_hiused + 2   ; ...and in all
tm_hisys    equ tm_hitot + 2    ; ...and of it the SYSTEM's: the kernel's
                                ; tables and .cold under the pool, and the
                                ; blocks the kernel and drivers own (28.4.8)
tm_xwn      equ tm_hisys + 2    ; records in tm_xw
tm_xw       equ tm_xwn + 2      ; the pool's blocks, TM_XWN x XMW_SIZE
TM_UMAX     equ 8               ; option ROMs the 640K-1MB map lists
tm_umn      equ tm_xw + TM_XWN * XMW_SIZE   ; byte: found (tm_romscan)
tm_uma      equ tm_umn + 1      ; TM_UMAX x {dw segment, dw KB}

; --- THE LIST'S GROUPS (KERN-MEGA-PLAN 14.70.2, SPEC.md 28.4) ----------------
tm_hifix    equ tm_uma + TM_UMAX * 4    ; KB between 1MB and the pool's base
tm_mview    equ tm_hifix + 2    ; byte: 0 PROGRAMS, 1 SYSTEM. Survives a page
                                ; switch, as the scroll position does
tm_exp      equ tm_mview + 1    ; dword: bit g set = group g is OPEN. Zero -
                                ; every group closed - is what a launch
                                ; shows: one row per owner
tm_cgrp     equ tm_exp + 4      ; byte: the group of the row being composed,
                                ; FFh for a row that is not one
tm_cgid     equ tm_cgrp + 1     ; MEM_MAX bytes: each claim record's group
                                ; (tm_classify), FFh for a free record
tm_xgid     equ tm_cgid + MEM_MAX   ; TM_XWN bytes: each block above 1MB's
tm_gkb      equ tm_xgid + TM_XWN    ; TMG_N words: each group's KB
tm_rgid     equ tm_gkb + TMG_N * 2  ; TM_SHOWN bytes: the group each SCREEN
                                ; row is, as the last walk drew it - what a
                                ; click is read against (tm_lclick)
tm_rsq      equ tm_rgid + TM_SHOWN  ; TM_SHOWN words: the square each SCREEN
                                ; row last wore (tm_mrow_close)
tm_nbuf     equ tm_rsq + TM_SHOWN * 2   ; a driver group's name: 'USBMOUSE.DRV'
TM_BSS_TOTAL equ tm_nbuf + 13 - os88_image_end
%else
TM_BSS_TOTAL equ tm_lowkb + 2 - os88_image_end
%endif
