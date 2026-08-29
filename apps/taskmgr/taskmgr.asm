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
TMM_HSQ_X   equ 108             ; the claim swatch, centred in the two-space
                                ; gap between the RAM and HEAP figure pairs.
                                ; It was 105, when this line alone lettered
                                ; from the band's edge + 6 instead of TM_PEN;
                                ; the line is a tm_lrun now, so the gap is
                                ; [104,119] and an 8px square centred in it
                                ; starts here (SPEC.md 28.5.3)
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
TMC_LINE    equ 0               ; the RAM (+ HEAP) readout line
TMC_PKG     equ 1               ; (free - was the PACKAGES readout line)
TMC_MRAM    equ 2               ; the conventional-memory map interior
TMC_BAR     equ 3               ; the performance view's RAM bar
TMC_XMS     equ 4               ; the XMS readout line (SPEC.md 41)
TMC_XBAR    equ 5               ; ...and its bar
TMC_HTOT    equ 6               ; the heap page's totals line (SPEC.md 28.4)
TMC_HSPL    equ 7               ; ...its held/purgeable split
TMC_HFRG    equ 8               ; ...and its free/largest-run pair
TMC_HSB     equ 9               ; ...and its scroll bar (SPEC.md 28.4.4),
                                ; keyed on the whole seven-word block so that
                                ; a window MOVE redraws it as readily as a
                                ; claim does
TMC_QUIET   equ 10              ; ...and the WHOLE of what the memory view and
                                ; the heap page draw from (SPEC.md 28.6.1),
                                ; which is the one key tested with no lock
                                ; held and before one is taken
TMC_N       equ 11

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

TMM_ROWS    equ INST_MAX + 7    ; System, its four buffer rows, the two
                                ; headings, every instance - 19 x TM_ROW_H
                                ; from TMM_ROW_Y is 279 of the 281-pixel
                                ; content, which is what decides both the
                                ; template's height and that free slots are
                                ; not drawn at all. tm_mrow_open clamps to
                                ; the LIVE frame on top of this, for the
                                ; screens where wm_fit shrinks the window

; --- THE TWO FRAMES THIS WINDOW WANTS (SPEC.md 11.100.1) ---------------------
; tm_layout has always worked out the frame its column count needs; these are
; that arithmetic as CONSTANTS, so the KERNEL can apply one at the one moment a
; resize is safe. It has to be the kernel: an 11.98 handler may not draw, a
; resize repaints, and a repaint from the WORKER runs every overlapped window's
; W_PAINT on a worker task holding the gfx lock - which hard froze the machine
; the one build that tried it (a Disk window open, then dragged across twice).
;
; A GENEROUS height is the idiom (docs/WINDOW-SIZING-PLAN.md 9): the clamp
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

; --- heap-page geometry (SPEC.md 28.4) ---------------------------------------
; The third page: every claim record, grouped under the owner that holds it.
; Its rows start far higher up the content than either list above, because it
; has no map and no bar to sit under - two caption lines and a header, and the
; rest is list.
; Three caption lines, because the heap answers three different questions and
; one line running them together is what made the memory view's single HEAP
; figure ambiguous in the first place (SPEC.md 28.4.1): how big is the heap,
; how much of it is spoken for and on what terms, and how much is left.
; ...and the three read DOWN as well as across (SPEC.md 28.4.2): the right
; label starts at column 13 on every one of them and the right figure at 19,
; so the second column is a column. The pads that buy that are in the strings
; themselves (tm_s_havl, tm_s_hpurg, tm_s_hfrag), which is where the sums are.
;   col  0            13    19
;        |            |     |
;        HEAP  84K    AVAIL  60K
;        HELD 24K( 3) PURGE   0K( 0)
;        MAX RUN  60K FRAG    0K
TMH_TOT_Y   equ 4               ; 'HEAP nnnK    AVAIL nnnK' - the SIZE against
                                ; what a claim can still get out of it
TMH_SPL_Y   equ 15              ; 'HELD nnnK(nn) PURGE nnnK(nn)' - claimed,
                                ; split by whether the kernel can take it back
TMH_FRG_Y   equ 26              ; 'MAX RUN nnnK  FRAG nnnK' - the fragmentation
                                ; pair, and the reading this page exists for
                                ; (docs/FIELD-NOTES.md 2)
TMH_HDR_Y   equ 37              ; 'TYPE      ADDR SIZE TIER'
TMH_ROW_Y   equ 48              ; first claim row, same TM_ROW_H pitch

; Every record, plus a heading each, plus System. This is the TABLE's bound
; and not the screen's - tm_row_place still clamps to the live frame - and it
; is what tm_rowck has to be sized for, since a row past the array is a check
; word written over whatever follows it.
; ...plus one for the blank tm_mrow_nolast spends to keep a heading off the
; foot of a column. At most ONE, however many headings there are: only one row
; index per column is that column's last, and the list passes it once.
TMH_ROWS    equ MEM_MAX + INST_MAX + 3

; The claim row, in characters: two of indent, the type, A SEPARATOR, the
; segment, the KB column tm_kcol emits, a gap and the purge tier. Stated here
; because the header string has to land on the same columns and nothing else
; checks it. The separator is load-bearing rather than spacing - five of the
; type names use all seven columns, and without it 'DirRead' and its segment
; read as 'DirRead2000' (the memory list's 'ARKANOI9E40', SPEC.md 28).
TMH_IND     equ 2               ; the indent that puts a claim under its owner
TMH_TYPEC   equ 7               ; TYPE - seven, the memory list's name width
TMH_TIERC   equ 4               ; TIER - 'TRIV'/'LOW'/'MED'/'HIGH'/'-'
%if TMH_IND + TMH_TYPEC + 1 + 4 + 5 + 1 + TMH_TIERC > TM_NCHUNK * TM_CHUNK
  %error "taskmgr: the heap row is wider than the chunk span it is padded to"
%endif

; --- ...and the SCROLL BAR that makes the rest of the table reachable --------
; TMH_ROWS is 47 and no screen here shows more than 22, so this page has
; always been able to run off the bottom of its own frame - silently, because
; tm_row_place refuses a row past tm_ylim and says nothing about the rows
; behind it (SPEC.md 28.4.4). The standard bar (SPEC.md 13.10) owns the
; rightmost TMH_SB_W of the LAST column's band and the rows stop short of it.
;
; THE ROWS GIVE UP 8 PIXELS ON THE LEFT TO BUY 14 ON THE RIGHT: this page
; letters from TM_PEN where the memory list letters from TM_MPEN, whose first
; ten pixels are the legend square's and which this page has already declined
; to draw (tm_hclaim). That leaves the two pixels between the chunk span and
; the bar as a gutter, and keeps the pen on a multiple of 8 - which is what
; font_run's single-store path is sized by (SPEC.md 6.1), and is the reason
; TM_PEN is 8 rather than the 6 it started at.
TMH_SB_W    equ 14              ; the standard bar's width (SPEC.md 13.10.2):
                                ; what both kernel bars use and what the arrow
                                ; glyph's 9 pixels are drawn for
TMH_SB_X1   equ TM_RW - TMH_SB_W + 1    ; band-relative, so it is one number
TMH_RW      equ TMH_SB_X1 - 1   ; ...and a heap row's own right edge, which is
                                ; what tm_rowr hands the last chunk's fill
%if TM_PEN + TM_NCHUNK * TM_CW > TMH_RW + 1
  %error "taskmgr: a heap row now runs under its own scroll bar"
%endif

; The widest of this page's three captions is the HELD/PURGE line, at 27
; characters - and since tm_put2 it is 27 at EVERY count, not just two-digit
; ones:
;   'HELD' nnn 'K(' nn ')' ' PURGE ' nnn 'K(' nn ')'
; which is exactly TM_STRMAX's limit, so a label lengthened there has to be
; paid for somewhere. The other two are 23 and have four characters spare.
; Said as a guard rather than a comment, because the note beside TM_STRMAX is
; that it has ONE character of slack, and a caption grown here is exactly how
; that gets spent silently.
%if (4+3+2+2+1) + (7+3+2+2+1) > TM_STRMAX - 1
  %error "taskmgr: the heap page's HELD/PURGE line no longer fits tm_str"
%endif
%if (5+3+1) + (10+3+1) > TM_STRMAX - 1
  %error "taskmgr: the heap page's HEAP/AVAIL line no longer fits tm_str"
%endif
%if (8+3+1) + (7+3+1) > TM_STRMAX - 1
  %error "taskmgr: the heap page's MAX RUN/FRAG line no longer fits tm_str"
%endif
; What no guard here can check is the ALIGNMENT, because both sides of it are
; characters inside string literals rather than constants: each line's left
; half plus the pad leading its right-hand label has to come to 13, and the
; three arrive there as 9+4, 12+1 and 12+1. That sum is stated at tm_s_havl,
; beside the spaces that carry it, and it is verified by looking at the page.

; The CPU/scheduler caption is chunked too, and it borrows the row machinery
; whole by being a row that no list draws: it is a 20-character line whose
; first two chunks carry a percentage that moves every interval and whose
; last two carry a scheduler mode that essentially never does. As one check
; word it redrew all twenty glyphs twice a second on a machine doing nothing,
; which on the mono adapters made it the single largest recurring cost of
; having this window open.
TMR_CPU     equ TMH_ROWS        ; the virtual row index it draws as: past
                                ; every real row of the DEEPEST list, which
                                ; is the heap page's (SPEC.md 28.4) and no
                                ; longer the memory view's
TM_NCK      equ TMH_ROWS + 1    ; ...so tm_rowck holds one more row than any
                                ; of the three lists can show
%if TMH_ROWS < TMM_ROWS || TMH_ROWS < TM_ROWS
  %error "taskmgr: tm_rowck is no longer sized for the deepest list"
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

    ; --- is there a store above 1MB at all? ----------------------------------
    ; Asked ONCE, here, because everything below derives the frame from it and
    ; the answer cannot change while the machine runs: xm_init sizes the pool
    ; at boot and nothing resizes it (SPEC.md 41.5). Asked of the POOL SIZE and
    ; never of xm_caps' free figure - a full pool answers 0 free and still very
    ; much exists - and on kern_small the slot is a stub answering tier 0's
    ; answers, so this needs no build-time test and gets that build right for
    ; free (SPEC.md 41.11).
    push di
    push es
    TM_ES_DATA
    mov di, tm_kb
    TM_SYS_KB
    pop es
    pop di
    mov word [tm_xoff], 0
    cmp word [tm_kb+SK_XMS], 0
    jne .xms
    mov word [tm_xoff], TMM_XSHIFT
.xms:

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
; tm_layout - every derived layout word, from a frame this window can have
;
; in:  AX = the frame HEIGHT available, [tm_vw] = the frame WIDTH available
; out: tm_colrows, tm_cols, tm_hcolrows, tm_col2rows, tm_maxrow and
;      tm_tpl+4 / tm_tpl+6, all set
; clobbers: AX, BX, DX
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
    sub ax, TITLE_H + 1 + TMM_ROW_Y     ; ...less the chrome and the maps
    add ax, [tm_xoff]           ; ...and back the bar's height on a machine
                                ; with no store, which is what turns the
                                ; reclaimed space into a ROW rather than into
                                ; a shorter window (TMM_XSHIFT = TM_ROW_H)
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
    sub ax, [tm_xoff]           ; the list starts higher with no bar above it
    mov [tm_tpl+6], ax          ; the frame height the machine can take

    ; --- how deep is the HEAP page's column 0? --------------------------------
    ; The same frame, but its list starts at TMH_ROW_Y instead of TMM_ROW_Y
    ; (SPEC.md 28.4) - there is no map and no bar above it - so it holds the
    ; rows that fit in the extra height.
    ;
    ; Derived from the FRAME and never from the dock, which is what
    ; [tm_colrows] above uses. On a screen where TMM_ROWS caps the height
    ; below what the dock would allow, a dock-derived depth names rows the
    ; frame cannot show - and tm_row_place would then wrap PAST rows tm_ylim
    ; had already refused, so they would be lost rather than moved into
    ; column 1.
    push ax
    sub ax, TITLE_H + 1 + TMH_ROW_Y
    jbe .nohrow
    mov bl, TM_ROW_H
    div bl                      ; AL = rows, and the quotient fits: the tallest
    xor ah, ah                  ; frame here is 295px against an 11px pitch
    jmp short .havehrow
.nohrow:
    mov ax, 1
.havehrow:
    mov [tm_hcolrows], ax
    pop ax

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
    add ax, [tm_hcolrows]       ; the DEEPEST column 0 of the three pages, so
                                ; the heap page is not clipped by a cap sized
                                ; for a shallower one. Safe for the other two:
                                ; the performance list is bounded by its own
                                ; `cmp si, TM_ROWS`, the memory list tests
                                ; TMM_ROWS first, and tm_ylim clamps both
    cmp ax, TMH_ROWS            ; never more than there is data for - and the
    jbe .rowcap                 ; DEEPEST list is the heap page's now, so a
    mov ax, TMH_ROWS            ; tall screen may show more than the memory
.rowcap:                        ; view's TMM_ROWS (SPEC.md 28.4). The frame
    mov [tm_maxrow], ax         ; HEIGHT above is still TMM_ROWS' and did not
                                ; move; the performance list is bounded by its
                                ; own `cmp si, TM_ROWS`, and the memory list's
                                ; blank loop tests TMM_ROWS before this
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
    push cx                     ; **THE CONTENT BOX THIS PROC WAS HANDED** -
    push dx                     ; the kernel applies a published frame BEFORE
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
    pop cx                      ; tm_layout's argument is a frame. Handing it
    add ax, TITLE_H + 1         ; the display's BAND instead makes tm_colrows
                                ; what fits the SCREEN rather than what fits
                                ; the window, which is 32 rows on a VGA against
                                ; the 19 the frame holds
    call tm_layout              ; ...which re-derives the column count and
                                ; everything under it. The FRAME is the
                                ; kernel's: this proc may not draw and a resize
                                ; repaints, so the size is PUBLISHED once at
                                ; launch and applied by wm_land_fit
    pop si
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
                                ; origin 248). It
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
    OS88_PREFER tm_pref, TM_PREF_W1, TM_PREF_H,  TM_PREF_W1, TM_PREF_H,  TM_PREF_W2, TM_PREF_H

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

; --- heap page (SPEC.md 28.4) ------------------------------------------------
; The header's gaps are what put ADDR at column 9, SIZE's four digits at 14
; and TIER at 19 - exactly where tm_hclaim composes them, and where a heading
; row's tm_kcol total lands too.
tm_s_hhdr:  db 'TYPE      ADDR SIZE TIER', 0
; The three captions are a two-column TABLE and the gaps below are what make
; them one (SPEC.md 28.4.2). Each right-hand label starts at column 13 and
; each right-hand figure at 19, whatever the left half of its own line spent -
; so AVAIL, PURGE and FRAG read down the page instead of stepping across it.
; The leading spaces here are therefore load-bearing arithmetic, not spacing:
; change a left-hand label and the pad in front of the right-hand one owes the
; same number back. The left figures are deliberately NOT aligned with each
; other - a common left column would need 8 (label) + 4 (nnnK) + 4 ('(nn)')
; + 1 + 6 + 4 + 4 = 31 characters and the line has 27 (TM_STRMAX), so the
; choice was one aligned column or none.
tm_s_hheap: db 'HEAP ', 0
tm_s_hheld: db 'HELD', 0        ; the two halves of what is claimed, never one
tm_s_hpurg: db ' PURGE ', 0     ; figure: they are owed on different terms.
                                ; 'HELD' takes no trailing space - tm_put3
                                ; right-aligns into three and brings its own
tm_s_havl:  db '    AVAIL ', 0  ; NOT 'FREE': this counts the purgeables, so
                                ; it is what a claim can GET rather than what
                                ; is unused, and the two differ by PURGE
                                ; exactly (SPEC.md 28.4.1)
tm_s_hmax:  db 'MAX RUN ', 0    ; the largest single RUN of AVAIL - not a
                                ; maximum heap size, which is what a bare
                                ; 'MAX' beside 'HEAP' reads as. It is the
                                ; biggest claim that can succeed in ONE call,
                                ; and 'run' is the kernel's own word for it
                                ; (mem_run, SPEC.md 50.3)...
tm_s_hfrag: db ' FRAG  ', 0     ; ...and everything available OUTSIDE it, so
                                ; the two SUM to AVAIL. Without it the page
                                ; said '519K available, 515K max', which is
                                ; arithmetic nobody can close (SPEC.md 28.4.1)
                                ; Two trailing spaces: FRAG is a character
                                ; shorter than AVAIL and PURGE, and it is the
                                ; FIGURES that have to line up, not the label

; TYPE names, seven columns. A claim the table does not know prints its owner
; word in hex instead (tm_htype) - a debug page must not label an unknown tag
; as the nearest thing it recognises.
tm_s_tregn: db 'Region', 0      ; owner = the slot, base = I_SPTR (SPEC.md 20.1)
tm_s_tdata: db 'Data', 0        ; ...anything else that instance holds (50.3)
tm_s_tsave: db 'MenuSav', 0
tm_s_tdrv:  db 'DrvImg', 0
tm_s_tcopy: db 'CopyBuf', 0
tm_s_tfatw: db 'FATwin', 0
tm_s_tasc:  db 'Assoc', 0
tm_s_tclip: db 'Clipbrd', 0
tm_s_twsav: db 'WinSave', 0
tm_s_tdirw: db 'DirRead', 0
; ...and the three that were MISSING, every one of which this page had been
; printing as a bare owner word (SPEC.md 28.4.3). The hex fallback below is
; the table's honesty rule about a tag this build has never SEEN, and not a
; place to leave one the kernel ships: MEM_K_BAND is claimed at boot and
; never freed, so 'FF0A' was a permanent row on every machine with a band
; composer. tests/unit/t_ktags.py is the gate that stops a fourth.
tm_s_tmod:  db 'ModImg', 0      ; ...beside DrvImg, because it is the same
                                ; thing one rung down: an image claimed
                                ; top-down whose base IS its CS (SPEC.md 2.8)
tm_s_tclon: db 'Cloner', 0      ; the job block AND the sectors in flight,
                                ; which is why freeing it is the whole of a
                                ; clone's teardown (SPEC.md 18.99)
tm_s_tband: db 'BandBuf', 0     ; the 1bpp composer's, one per machine (5.9.2)
; (owner word, name) pairs, ended by a 0 owner. MEM_P_WSAVE is NOT here: it is
; a RANGE (SPEC.md 11.96.3), one cache per window slot, and tm_htype tests it
; before it walks this.
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
    dw MEM_K_SAVE,  tm_s_tsave
    dw MEM_K_DRV,   tm_s_tdrv
    dw MEM_K_COPY,  tm_s_tcopy
    dw MEM_K_FATW,  tm_s_tfatw
    dw MEM_K_ASC,   tm_s_tasc
    dw MEM_K_CLIP,  tm_s_tclip
    dw MEM_K_MOD,   tm_s_tmod
    dw MEM_K_CLONE, tm_s_tclon
    dw MEM_K_BAND,  tm_s_tband
    dw MEM_P_DIRW,  tm_s_tdirw
    dw 0

; TIER names, four columns, indexed by the owner's high byte - MEM_PG_TRIV
; first, so the index is (high byte - MEM_PG_MIN). An unpurgeable claim gets
; the dash, which is the answer for every instance-owned row as well.
tm_ztab:
    dw tm_s_ztriv, tm_s_zlow, tm_s_zmed, tm_s_zhigh
tm_s_ztriv: db 'TRIV', 0
tm_s_zlow:  db 'LOW', 0
tm_s_zmed:  db 'MED', 0
tm_s_zhigh: db 'HIGH', 0
tm_s_zno:   db 'HELD', 0        ; NOT a dash. The tiers are what LOSING a
                                ; block costs, and a dash in that column read
                                ; as a rank BELOW 'TRIV' - the opposite of the
                                ; truth, since an ordinary claim is the only
                                ; rank that outranks every cache (MEM_LVL_TOP,
                                ; SPEC.md 50.6.4). 'HELD' says the thing the
                                ; scale cannot: this one is not a cache at all
                                ; and the kernel cannot take it back

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
    call tm_update              ; incremental redraw, lock held
.skip:
    call OSAPI_GFX_UNLOCK
    jmp .interval

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
; -----------------------------------------------------------------------------
tm_quiet:
    cmp byte [tm_view], 0
    je .paint                   ; the performance view moves every interval by
    push ax                     ; construction: a percentage, a graph column
    push bx                     ; and a spin count that are never twice the
    push cx                     ; same
    push si
    push di
    push es
    TM_ES_DATA
    mov di, tm_claims
    TM_CLAIM_SNAPSHOT           ; ...into the buffer the paint would use anyway
    pop es
    mov si, tm_claims
    mov cx, CLAIM_SNAPSHOT_SIZE
    xor ax, ax
    call tm_sumb                ; AX chains through all three spans
    mov si, tm_kb
    mov cx, SYSKB_SIZE
    call tm_sumb
    mov si, tm_ist
    mov cx, TM_IHASH
    call tm_sumb
    mov bx, tm_elck + 2*TMC_QUIET
    call tm_elchk               ; CF = 1 unchanged, which IS the skip - and the
    pop di                      ; pops below write no flags
    pop si
    pop cx
    pop bx
    pop ax
    ret
.paint:
    clc
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
    cmp byte [tm_view], 0
    je .now                     ; the performance view keeps TM_INT
    inc byte [tm_slow]
    cmp byte [tm_slow], TM_SLOW
    jb .not_yet
.now:
    mov byte [tm_slow], 0
    clc
    ret
.not_yet:
    stc
    ret

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

    TM_ES_DATA                  ; the heap's totals, for the RAM figure below
    mov di, tm_kb
    TM_SYS_KB

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
    mov byte [tm_idle], 0xFF    ; ...and WHICH slot is the idle task, which the
.ssnap:                         ; snapshot names with a state of its own
    mov al, [si]                ; (SPEC.md 28.7). Found here rather than by a
    mov [di], al                ; second walk, because this one is already
    cmp al, 3                   ; touching every byte of it
    jne .snotidle
    mov al, MAX_TASKS
    sub al, cl                  ; CX counts DOWN from MAX_TASKS: the slot is
    mov [tm_idle], al           ; MAX_TASKS - CX
.snotidle:
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

    ; ...AND THE IDLE TASK, into the same row (SPEC.md 28.7). It owns no
    ; instance, so it belongs to no application, and a machine that is 97%
    ; idle reads as a System row of 97% - which is what the older Windows
    ; Task Manager shows and what a reader expects. Kept in tm_idlec as well
    ; because it is the load meter's numerator, and the meter's denominator
    ; has to be this same total or the two can disagree again.
    xor ax, ax
    mov [tm_idlec], ax
    mov [tm_idlec+2], ax
    mov bl, [tm_idle]
    cmp bl, MAX_TASKS           ; 0xFF when no slot answered to state 3
    jae .noidle
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [tm_tdif+bx]
    mov dx, [tm_tdif+bx+2]
    mov [tm_idlec], ax
    mov [tm_idlec+2], dx
    add [tm_rcyc], ax
    adc [tm_rcyc+2], dx
.noidle:

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
    shr word [tm_idlec+2], 1    ; the meter's numerator rides the SAME shift,
    rcr word [tm_idlec], 1      ; or it is a ratio of two different scales
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

    ; --- the LOAD METER: 100 - 100*idle/total (SPEC.md 28.7) ----------------
    ; Off the same interval and the same total as the rows above, which is the
    ; whole point: the meter and the process list cannot disagree any more.
    ; They did, and both were right - the page charged its own spinning worker
    ; 34-38% of CPU TIME while the graph drew 0-2% of SPIN COUNT.
    ;
    ; No total (nothing accounted yet, or the very first sample) reads 0 rather
    ; than 100: an unknown load is better drawn as quiet than as a machine
    ; apparently pinned.
    mov cx, [tm_total]
    xor ax, ax
    jcxz .loadok
    mov ax, [tm_idlec+2]
    or ax, ax
    jnz .loadidle               ; idle > 64K units: it is the whole total
    mov ax, [tm_idlec]
    cmp ax, cx
    jae .loadidle
    mov si, 100
    mul si                      ; DX:AX = idle*100
    div cx                      ; AX = idle% <= 100
    jmp .loadsub
.loadidle:
    mov ax, 100
.loadsub:
    mov cx, 100
    sub cx, ax
    mov ax, cx
.loadok:
    mov [tm_load], ax

    ; --- history push: v = load*TM_GH/100, sweep index advances -------------
    mov cx, TM_GH
    mul cx
    mov cx, 100
    div cx                      ; AX = 0..TM_GH
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
    ; region IS a heap claim, so mem_claimed_kb already counts every one of
    ; them and the sum counted them a second time.
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
    mov ax, [tm_kb+SK_KERN]     ; the WHOLE kernel (SPEC.md 2): image,
                                ; scratch, FAT snapshot, disk buffers and
                                ; every task stack are one contiguous span,
                                ; and there is no growth room in it to bill
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
    cmp byte [tm_view], 1       ; 0 performance, 1 memory, 2 heap (SPEC.md 28.4)
    je tm_draw_mem
    ja tm_draw_heap
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
    mov ax, (CWHITE << 8) | CBLACK  ; they name, and tm_rows letters from here
    call OSAPI_FONT_RUN         ; opaque, over the content fill W_PAINT just
                                ; laid down (SPEC.md 28.5.2)
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
    cmp byte [tm_view], 1
    je tm_upd_mem
    ja tm_upd_heap
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

    cmp byte [tm_view], 2       ; the heap page's SCROLL BAR gets the point
    jne .cycle                  ; first (SPEC.md 28.4.4), and answers CF = 1
    call tm_hscroll             ; for anything that is not in it - so the one
    jnc .out                    ; click target below is untouched everywhere
.cycle:                         ; the bar is not
    mov al, [tm_view]           ; CYCLE 0 -> 1 -> 2 -> 0 (SPEC.md 28.4), not a
    inc al                      ; toggle: the content has exactly one click
    cmp al, 3                   ; target and this is the whole of the
    jb .vset                    ; navigation a third page needs - no tabs, no
    xor al, al                  ; chrome, no second hit-test
.vset:
    mov [tm_view], al
    mov byte [tm_slow], 0       ; ...and the slow count starts over, so the
                                ; first unattended refresh of a page arrives
                                ; one interval after it opened rather than up
                                ; to TM_SLOW of them (SPEC.md 28.6)
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
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hscroll - a press on the heap page: did the scroll bar take it?
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
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .down
    cmp al, OS88UI_SBPGUP
    je .pgup
    add dx, cx                  ; PGDN, and the THUMB pages the way it lies
    jmp short .set
.pgup:
    sub dx, cx
    jnc .set
    xor dx, dx                  ; ...which is the only place this can wrap
    jmp short .set
.up:
    or dx, dx
    jz .set                     ; already at the top: a dec would wrap, and
    dec dx                      ; the compare below turns it into an end stop
    jmp short .set
.down:
    inc dx
.set:
    mov [tm_htop], dx
    call tm_hclamp              ; ...and the bottom end stop, which is the one
                                ; place that knows how far down there is to go
    mov cx, [tm_htop]           ; DID THE VIEW MOVE? The block still carries
    cmp cx, [tm_hsb+12]         ; what the last paint drew from, so an end stop
    je .took                    ; costs nothing instead of repainting twenty
    call tm_upd_heap            ; rows that did not move
.took:
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
    call tm_hsnap               ; ...and the claim table, which the caption
                                ; reads as well as the map (SPEC.md 28.4.1)

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
    call tm_hsnap               ; ...and the claim table (SPEC.md 28.4.1)

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


    cmp word [tm_xoff], 0       ; a machine with no store above 1MB draws no
    jne .nobar                  ; bar at all - not an empty one. The figures
                                ; come off the caption to match and everything
                                ; below rises by TMM_XSHIFT
    mov ax, [tm_cx]             ; XMS bar frame (6,71)-(TM_RW,80) - here and
    add ax, 6                   ; not in tm_xbar, which is element-checked and
    mov bx, [tm_cy]             ; skips itself on an empty pool
    add bx, TMM_XB_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TMM_XB_Y2
    call OSAPI_GFX_FRAME
.nobar:

    mov cx, [tm_cx]             ; header, at the rows' text x - once per
    mov bx, [tm_cols]           ; COLUMN, because each carries its own list
    mov dx, [tm_cy]
    add dx, TMM_HDR_Y           ; column 0's sits under the maps...
    sub dx, [tm_xoff]           ; ...higher when there is no bar between them
.hdr:
    push cx
    push dx
    add cx, 16
    mov si, tm_s_mhdr
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN         ; opaque (SPEC.md 28.5.2)
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
    cmp word [tm_xoff], 0       ; no store above 1MB: the line is the TIER and
    je .figs                    ; nothing else. 'XMS 0/ 0K' is the same empty
    pop bx                      ; statement the bar was making, and the tier is
    pop ax                      ; what makes the absence legible - 'CPU 8086'
    jmp short .term             ; IS the reason there is none (SPEC.md 60.2)
.figs:
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
.term:
    mov byte [di], 0

    call tm_rowsum              ; unchanged? then so are its pixels
    mov bx, tm_elck + 2*TMC_XMS
    call tm_elchk
    jc .out

    mov ax, TMM_XMS_Y              ; SPEC.md 28.5.1: placed, padded and drawn as
    call tm_lrun                ; ONE opaque run - no fill, no blank interval
    jnc .out
    mov word [bx], 0            ; a clip edge crossed it: nothing was drawn, so
                                ; the key just recorded would keep it stale
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

    ; The snapshot is taken by tm_upd_mem/tm_draw_mem above, because the
    ; CAPTION reads it too now (SPEC.md 28.4.1) and two calls a refresh would
    ; walk the table twice for one picture.
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
    call tm_mrow_nolast         ; ...not on a column's foot, where the rows it
                                ; heads are in the next one (SPEC.md 28.4)
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
    call tm_mrow_nolast
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

; =============================================================================
; The heap page (SPEC.md 28.4)
;
; mem_tab itself, grouped under the owner that holds each record. The memory
; view above answers how MUCH heap an instance holds - one figure, out of
; SSI_KB - and that is the wrong shape for the two questions a heap goes wrong
; in: WHICH of an app's claims is the big one, and WHAT is still purgeable when
; the next claim is refused.
;
; It cost the kernel nothing. The owner word already carries the namespace
; (SPEC.md 50.2) and, since SPEC.md 50.6, the purge tier in its high byte -
; the encoding was chosen so this module could pick the class out of what it
; already reads, and this is that being cashed. No new cell, no new snapshot
; field, no .o88 invalidated anywhere else in the tree.
; =============================================================================

; -----------------------------------------------------------------------------
; tm_hsnap - refresh the claim table (SPEC.md 20.9)
; in:  nothing
; out: nothing (all registers preserved)
;
; The memory view takes its copy inside tm_map_ram, because the map is the only
; thing there that reads it. Here BOTH captions and the whole list do, so it is
; taken once at the top of each paint instead - a snapshot per element would
; let the count in the caption disagree with the rows under it.
; -----------------------------------------------------------------------------
tm_hsnap:
    push ax
    push di
    push es
    TM_ES_DATA
    mov di, tm_claims
    TM_CLAIM_SNAPSHOT
    pop es
    push bx                     ; ...and the heap's two live figures with it:
    call OSAPI_MEM_AVAIL        ; AX = largest run, BX = total available. Once
    mov [tm_maxrun], ax         ; a refresh, because BOTH captions read them
    mov [tm_avl], bx            ; and mem_avail walks the table per candidate
    pop bx
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
; tm_hmatch - does the record at SI belong to group BX?
; in:  SI = a claim record, BX = 0xFFFF for System (every kernel tag) or an
;      instance slot
; out: CF = 0 it does, CF = 1 it does not (or the record is free)
; clobbers: nothing else (flags)
;
; An instance owns claims under TWO owner words and both belong on its row: the
; REGION is stamped with the slot and its data claims with the segment
; (SPEC.md 50.2/20.1). A built-in has no region and stamps everything with the
; slot, which the same two tests cover without a special case.
; -----------------------------------------------------------------------------
tm_hmatch:
    push ax
    push bx                     ; BX is the group AND, below, the slot's word
    mov ax, [si+CLS_SEG]        ; index: DX cannot index on an 8086 (SPEC.md 1)
    or ax, ax
    jz .no                      ; a free record
    mov ax, [si+CLS_OWN]
    cmp bx, 0xFFFF
    je .sys
    cmp ax, bx                  ; its slot: a package's region, or a built-in's
    je .yes                     ; claim
    shl bx, 1
    cmp word [tm_isz+bx], 0     ; a built-in owns no segment to match against
    je .no
    cmp ax, [tm_ispt+bx]        ; ...a package's data claims carry it
    je .yes
    jmp short .no
.sys:
    cmp ah, MEM_PG_MIN          ; 0xFB..0xFF is a kernel tag, purgeable or not.
    jb .no                      ; A SEGMENT can never reach that: conventional
.yes:                           ; memory tops out at 0xA000
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx                      ; pop leaves the flags alone
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hsum - one group's total and how many records are in it
; in:  BX = the group (tm_hmatch)
; out: AX = KB, CX = records
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_hsum:
    push dx
    push si
    xor ax, ax
    xor cx, cx
    mov si, tm_claims
.rec:
    call tm_hmatch
    jc .next
    mov dx, [si+CLS_PARA]
    xchg ax, dx
    call tm_hkb
    xchg ax, dx
    add ax, dx
    inc cx
.next:
    add si, CLS_RECSZ
    cmp si, tm_claims + CLAIM_SNAPSHOT_SIZE
    jb .rec
    pop si
    pop dx
    ret

; -----------------------------------------------------------------------------
; tm_put2 - a record count in exactly two columns, right-aligned
; in:  AX = value (0..99), DI = dest
; out: DI advanced by 2
; clobbers: nothing else (flags)
;
; The parenthesised counts on the HELD/PURGE caption are the one variable-width
; field on the heap page's three caption lines, and a variable width is what
; stopped PURGE lining up with AVAIL and FRAG: at nine records the line is a
; character shorter than at ten, so the label moved sideways as claims came
; and went (SPEC.md 28.4.2). Two columns is the NARROWEST fixed field that
; holds MEM_MAX, which is what tm_putn's note about '(  5)' was really
; objecting to - three columns of padding, not the idea of a column. The
; guard below is the whole of what makes two enough.
;
; It JUMPS to tm_putn rather than falling through into it, though tm_putn is
; the next thing in the file and three bytes cheaper that way: a routine
; inserted between the two would assemble cleanly, boot, and pad every count
; with whatever that routine left at DI.
; -----------------------------------------------------------------------------
%if MEM_MAX > 99
  %error "taskmgr: MEM_MAX no longer fits tm_put2's two columns"
%endif
tm_put2:
    cmp ax, 10                  ; AX survives a cmp, and tm_putn clobbers no
    jae .n                      ; more than the flags this just spent
    mov byte [di], ' '
    inc di
.n:
    jmp tm_putn

; -----------------------------------------------------------------------------
; tm_putn - an unsigned count in as few digits as it needs
; in:  AX = value, DI = dest
; out: DI advanced by 1..5
; clobbers: nothing else (flags)
;
; tm_put3 pads to three columns, which is right for a KB figure in a column
; and wrong for a parenthesised count: '(  5)' against '(5)'. The split line
; is 27 characters at two-digit counts and there is no room for the padding.
; -----------------------------------------------------------------------------
tm_putn:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.split:
    xor dx, dx
    div bx                      ; AX = quotient, DX = this digit
    push dx
    inc cx
    or ax, ax
    jnz .split
.emit:                          ; ...back off the stack, most significant first
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hsplit - what is claimed, split by whether the kernel can take it back
; in:  the claim snapshot
; out: AX = KB held (unpurgeable), DX = KB purgeable,
;      BX = records held,           CX = records purgeable
; clobbers: nothing else (flags)
;
; The two are never added into one figure anywhere the user can see, and that
; is the whole point (SPEC.md 28.4.1): they are owed on completely different
; terms. AX is also exactly what the memory view's rows account for between
; their SIZE and HEAP columns - a region is a claim owned by the instance slot
; and lands in SIZE, a data claim lands in HEAP, and a PURGEABLE claim lands
; in neither because it belongs to no instance at all.
; -----------------------------------------------------------------------------
tm_hsplit:
    push si
    push di                     ; DI and not BP: a callback's BP is the task's
    xor ax, ax                  ; instance record and is never scratch here
    xor bx, bx                  ; (tm_row_draw says so for the same reason)
    xor di, di                  ; AX/BX = held KB and records, DI/CX = the
    xor cx, cx                  ; purgeable pair
    mov si, tm_claims
.rec:
    cmp word [si+CLS_SEG], 0
    je .next                    ; a free record
    push ax
    mov ax, [si+CLS_PARA]
    call tm_hkb
    mov dx, ax
    pop ax
    cmp byte [si+CLS_OWN+1], MEM_PG_MIN   ; the owner's high byte IS the rank
    jb .held
    cmp byte [si+CLS_OWN+1], MEM_PG_MAX
    ja .held                    ; MEM_LVL_TOP: an ordinary kernel claim
    add di, dx
    inc cx
    jmp short .next
.held:
    add ax, dx
    inc bx
.next:
    add si, CLS_RECSZ
    cmp si, tm_claims + CLAIM_SNAPSHOT_SIZE
    jb .rec
    mov dx, di
    pop di
    pop si
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
    jae .data                   ; a package's own segment: one of its claims
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
.kern:
    mov dx, ax                  ; the raise-cache RANGE first: one per window
    sub dx, MEM_P_WSAVE         ; slot (SPEC.md 11.96.3), so it is not in the
    cmp dx, MEM_P_WSAVE_N       ; exact-match table below
    jb .wsave
    mov si, tm_ktab
.scan:
    mov dx, [si]
    or dx, dx
    jz .hex
    cmp dx, ax
    je .found
    add si, 4
    jmp short .scan
.found:
    mov si, [si+2]
    jmp short .put
.wsave:
    mov si, tm_s_twsav
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
    mov si, tm_s_zno
    cmp ah, MEM_PG_MIN
    jb .put                     ; an instance's own: never a cache
    cmp ah, MEM_PG_MAX
    ja .put                     ; MEM_LVL_TOP: an ordinary kernel claim
    mov bl, ah
    sub bl, MEM_PG_MIN
    xor bh, bh
    shl bx, 1
    mov si, [tm_ztab+bx]
.put:
    mov cx, TMH_TIERC
    call tm_copyn
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hhead - a group's heading row: the owner's name and what it holds
; in:  SI = the name, AX = the group's KB
; out: nothing (all registers preserved); [tm_mrow] advanced
;
; The pad is what puts the total in the SIZE column, on the same vertical rule
; as every claim row under it - a heading is not a claim, so it fills neither
; ADDR nor TIER.
; -----------------------------------------------------------------------------
tm_hhead:
    push ax
    push cx
    push di
    push si
    call tm_mrow_nolast         ; ...never at the foot of a column, where the
                                ; rows it heads are in the next one
    call tm_mrow_open
    call tm_copy7
    mov cx, TMH_IND + TMH_TYPEC + 1 + 4 - 7
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
    call tm_kcol
    call tm_mrow_close
    pop si
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hclaim - one claim row, indented under the owner that holds it
; in:  SI = a claim record
; out: nothing (all registers preserved); [tm_mrow] advanced
; -----------------------------------------------------------------------------
tm_hclaim:
    push ax
    push bx
    push cx
    push dx
    push di
    call tm_mrow_open           ; no legend square: the band a claim wears on
    mov cx, TMH_IND             ; the memory view's map is THAT page's statement
.ind:
    mov byte [di], ' '
    inc di
    loop .ind
    mov ax, [si+CLS_OWN]
    mov bx, [si+CLS_SEG]
    call tm_htype
    mov byte [di], ' '          ; the separator, and it is NOT decoration: a
    inc di                      ; type that uses all seven columns - DirRead,
                                ; Clipbrd, CopyBuf, MenuSav, WinSave - runs
                                ; straight into the segment without it, which
                                ; is 'DirRead2000'. The memory list learned
                                ; this the same way, with 'ARKANOI9E40'
    mov ax, [si+CLS_SEG]
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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_hgrp - one owner group: the heading, then every claim it holds
; in:  BX = the group (tm_hmatch), SI = the owner's name
; out: nothing (all registers preserved); [tm_mrow] advanced
; -----------------------------------------------------------------------------
tm_hgrp:
    push ax
    push cx
    push si
    call tm_hsum                ; AX = KB, and SI is still the name
    call tm_hhead
    mov si, tm_claims
.rec:
    call tm_hmatch
    jc .next
    call tm_hclaim
.next:
    add si, CLS_RECSZ
    cmp si, tm_claims + CLAIM_SNAPSHOT_SIZE
    jb .rec
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rows_heap - the heap page's list (SPEC.md 28.4)
;
; System first - every kernel tag, purgeable or not - then one group per live
; instance in slot order. An instance with no claims still gets its heading:
; "this app holds nothing" is an answer, and leaving it out would read as the
; app not running.
;
; in:  the claim snapshot and the instance snapshot
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rows_heap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call tm_hclamp              ; the scroll offset against what the LAST walk
    mov ax, [tm_htop]           ; counted (SPEC.md 28.4.4), then spend it: a
    mov [tm_hskip], ax          ; row above the view is composed and dropped
    mov word [tm_mrow], 0       ; by tm_mrow_close, so this counts from the
    mov word [tm_hpad], 0       ; first row that is actually on screen
    mov bx, 0xFFFF
    mov si, tm_s_sys
    call tm_hgrp

    xor bx, bx
.inst:
    cmp byte [tm_ist+bx], 0
    je .next
    mov si, bx                  ; its I_NAME snapshot, 16 bytes per slot
    mov cl, 4
    shl si, cl
    add si, tm_inm
    call tm_hgrp                ; BX survives: tm_hgrp banks AX/CX/SI only
.next:
    inc bx
    cmp bx, INST_MAX
    jb .inst

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
                                ; own end, for ever. One column never spends a
                                ; pad row at all (tm_mrow_nolast does nothing
                                ; in the LAST column), which is why no VGA or
                                ; Hercules machine could show this
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
                                ; runs in screen rows, and tm_mrow_close is
                                ; the memory view's too
.blank:                         ; blank whatever rows the list did not reach
    cmp word [tm_mrow], TMH_ROWS
    jae .done
    mov ax, [tm_maxrow]
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
    mov ax, [tm_cols]           ; x2 = the last column's own right edge...
    dec ax
    mov dx, TM_COLW
    mul dx
    add ax, [tm_cx]
    add ax, TM_RW
    mov [tm_hsb+4], ax
    sub ax, TMH_SB_W - 1        ; ...and the bar is its rightmost TMH_SB_W
    mov [tm_hsb], ax

    mov ax, [tm_cy]             ; y1 = that column's FIRST ROW: column 0 sits
    cmp word [tm_cols], 1       ; under two caption lines and a header, every
    je .y1                      ; later one at the top of the content
    add ax, TM_C2_ROW_Y
    jmp short .y1set
.y1:
    add ax, TMH_ROW_Y
.y1set:
    mov [tm_hsb+2], ax
    mov ax, [tm_ylim]           ; y2 = the bottom of the lowest row band the
    add ax, 7                   ; frame holds, which is one pixel clear of the
    mov [tm_hsb+6], ax          ; frame's own bottom border

    mov ax, [tm_hrows]
    mov [tm_hsb+8], ax          ; total
    mov ax, [tm_maxrow]
    mov [tm_hsb+10], ax         ; fit
    mov ax, [tm_htop]
    mov [tm_hsb+12], ax         ; pos
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

; -----------------------------------------------------------------------------
; tm_cap_htot - 'HEAP nnnK    AVAIL nnnK' (SPEC.md 28.4)
;
; How big the heap is, against how much of it a claim can still get. The pad
; inside tm_s_havl is what puts AVAIL on the same column as PURGE and FRAG
; below (SPEC.md 28.4.2); this line spends 9 characters before it, so the
; string carries 4.
; in:  nothing
; out: nothing (flags only)
; -----------------------------------------------------------------------------
tm_cap_htot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, tm_str
    mov si, tm_s_hheap
    call tm_copy
    mov ax, [tm_kb+SK_HEAP]     ; the heap, and what a claim can get out of
    call tm_put3                ; it: HEAP - AVAIL is HELD, so the pair closes
    mov byte [di], 'K'
    inc di
    mov si, tm_s_havl
    call tm_copy
    mov ax, [tm_avl]
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], 0

    call tm_rowsum
    mov bx, tm_elck + 2*TMC_HTOT
    call tm_elchk
    jc .out

    mov ax, TMH_TOT_Y              ; SPEC.md 28.5.1: placed, padded and drawn as
    call tm_lrun                ; ONE opaque run - no fill, no blank interval
    jnc .out
    mov word [bx], 0            ; a clip edge crossed it: nothing was drawn, so
                                ; the key just recorded would keep it stale
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_cap_hspl - 'HELD nnnK(nn) PURGE nnnK(nn)' (SPEC.md 28.4.1)
;
; What is claimed, and on what terms. One figure could not say it: HELD is
; memory nothing can get back, PURGE is memory the next claim can have for the
; price in the TIER column, and adding them produces a number that answers no
; question anybody has.
;
; The record counts go through tm_put2 rather than tm_putn, so this is 27
; characters at any count and PURGE cannot slide off AVAIL's and FRAG's column
; when a claim is made or released (SPEC.md 28.4.2). It is the widest of the
; three captions and has no slack at all - see the guard beside TMH_ROWS.
; in:  nothing
; out: nothing (flags only)
; -----------------------------------------------------------------------------
tm_cap_hspl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call tm_hsplit              ; AX/BX = held KB and records, DX/CX = the
    push dx                     ; purgeable pair
    push cx
    push bx
    mov di, tm_str
    mov si, tm_s_hheld
    call tm_copy
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], '('
    inc di
    pop ax                      ; held records
    call tm_put2                ; two columns, so PURGE below cannot move
    mov byte [di], ')'
    inc di
    mov si, tm_s_hpurg
    call tm_copy
    pop cx                      ; purgeable records, banked across the KB
    pop ax                      ; purgeable KB
    push cx
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], '('
    inc di
    pop ax
    call tm_put2                ; ...and so the line is one fixed width
    mov byte [di], ')'
    inc di
    mov byte [di], 0

    call tm_rowsum
    mov bx, tm_elck + 2*TMC_HSPL
    call tm_elchk
    jc .out

    mov ax, TMH_SPL_Y              ; SPEC.md 28.5.1: placed, padded and drawn as
    call tm_lrun                ; ONE opaque run - no fill, no blank interval
    jnc .out
    mov word [bx], 0            ; a clip edge crossed it: nothing was drawn, so
                                ; the key just recorded would keep it stale
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_cap_hfrg - 'MAX RUN nnnK  FRAG nnnK' (SPEC.md 28.4)
;
; The LARGEST single run against everything available outside it, from one
; OSAPI_MEM_AVAIL. That pair IS the fragmentation reading, and it is the one
; thing this page exists to put on screen: docs/FIELD-NOTES.md 2 is a refusal
; where the total says there is room and the largest run does not, and nothing
; in the OS showed both at once until here.
;
; tm_s_hfrag carries a leading space and two trailing ones: one to reach
; column 13, where AVAIL and PURGE start, and one to make up FRAG's missing
; fourth letter so the FIGURES line up too (SPEC.md 28.4.2).
; in:  nothing
; out: nothing (flags only)
; -----------------------------------------------------------------------------
tm_cap_hfrg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, tm_str
    mov si, tm_s_hmax
    call tm_copy
    mov ax, [tm_maxrun]
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov si, tm_s_hfrag
    call tm_copy
    mov ax, [tm_avl]            ; everything available OUTSIDE the largest run,
    sub ax, [tm_maxrun]         ; so MAX + FRAG = AVAIL exactly. The largest
    jnc .frag                   ; run is a subset of the total by construction,
    xor ax, ax                  ; so this cannot go negative - but a wrong 0
.frag:                          ; beats a wrong 65,000 if it ever does
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], 0

    call tm_rowsum
    mov bx, tm_elck + 2*TMC_HFRG
    call tm_elchk
    jc .out

    mov ax, TMH_FRG_Y              ; SPEC.md 28.5.1: placed, padded and drawn as
    call tm_lrun                ; ONE opaque run - no fill, no blank interval
    jnc .out
    mov word [bx], 0            ; a clip edge crossed it: nothing was drawn, so
                                ; the key just recorded would keep it stale
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_upd_heap - the heap page's periodic redraw (gfx lock held): everything
;               but the static column header
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_upd_heap:
    push ax
    push bx
    push dx

    mov bx, [tm_win]
    call tm_view_begin          ; [tm_cx]/[tm_cy]/[tm_rowx]/[tm_ylim]
    call tm_hsnap               ; ...and one claim table for all four below
    call tm_cap_htot
    call tm_cap_hspl
    call tm_cap_hfrg
    call tm_hsbset              ; BEFORE the rows: a row's band stops where
    call tm_rows_heap           ; the bar begins (tm_rowr)...
    call tm_hbar                ; ...and the bar itself once the walk has said
                                ; how deep the table is (SPEC.md 28.4.4)
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_draw_heap - the heap page's full redraw: the header plus everything
;                tm_upd_heap redraws
; in:  SI = window ptr (gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_draw_heap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call tm_view_begin

    TM_INK CBLACK               ; the column header, at the ROWS' text x and
    mov cx, [tm_cx]             ; once per column, exactly as the memory view
    mov bx, [tm_cols]           ; places its own (SPEC.md 28.1.1)
    mov dx, [tm_cy]
    add dx, TMH_HDR_Y
.hdr:
    push cx
    push dx
    add cx, TM_PEN              ; the ROWS' text x, which on this page is
                                ; TM_PEN and not the memory list's TM_MPEN -
                                ; the ten pixels that pen leaves for a legend
                                ; square are the scroll bar's here (28.4.4)
    mov si, tm_s_hhdr
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN         ; opaque (SPEC.md 28.5.2)
    pop dx
    pop cx
    add cx, TM_COLW
    mov dx, [tm_cy]
    add dx, TM_C2_HDR_Y
    dec bx
    jnz .hdr

    call tm_hsnap
    call tm_cap_htot
    call tm_cap_hspl
    call tm_cap_hfrg
    call tm_hsbset
    call tm_rows_heap
    call tm_hbar

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
    push si

    mov si, [tm_colrows]        ; column 0's depth, and the HEAP page has its
    cmp byte [tm_view], 2       ; own: its list starts 30px higher (no map, no
    jne .depth                  ; bar), so the column takes more rows before it
    mov si, [tm_hcolrows]       ; has to wrap. Sharing this one wrapped early
.depth:                         ; and left a map's height of white at the foot

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
    pop si
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
    mov bx, TMM_ROW_Y           ; ...or the heap page's own first row, which is
    sub bx, [tm_xoff]           ; 30px higher: what the memory view has above
    cmp byte [tm_view], 2       ; its list is a MAP and a BAR, and this page
    jne .y                      ; has two caption lines. Shared, the list sat a
    mov bx, TMH_ROW_Y           ; map's height below its own header.
.y:                             ; The heap page has no bar to lose, so
                                ; [tm_xoff] is the memory view's alone
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
    cmp word [tm_hskip], 0      ; ABOVE the top of the view (SPEC.md 28.4.4):
    je .live                    ; composed, not drawn, and - the whole point -
    dec word [tm_hskip]         ; NOT counted, so every [tm_mrow] below the
    jmp short .gone             ; offset is a SCREEN row rather than a table
.live:                          ; one. tm_rowck is indexed by where a row is
                                ; DRAWN, so a scroll reuses the cache instead
                                ; of invalidating it; subtracting the offset
                                ; down in tm_row_place would have left every
                                ; check word describing pixels that had moved
    cmp byte [tm_mfit], 0       ; off the bottom: nothing was drawn, and
    je .forget                  ; forget what used to be here so it comes
    mov byte [di], 0            ; back if the window ever grows
    mov ax, [tm_rowx]           ; this list letters from tm_rowx+TM_MPEN,
    add ax, TM_MPEN             ; leaving the band's first pixels for the square
    cmp byte [tm_view], 2       ; ...except on the heap page, which draws no
    jne .pen                    ; square (tm_hclaim) and wants those pixels
    mov ax, [tm_rowx]           ; at the other end, for its scroll bar
    add ax, TM_PEN
.pen:
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
; once. TMH_ROWS carries that one.
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
    cmp byte [tm_view], 2
    jne .d0
    mov bx, [tm_hcolrows]
.d0:
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
; It had a twin, tm_lfill, which named the same band by a CONTENT-RELATIVE y
; for the caption lines above the list. Every caption is a tm_lrun now - the
; padding IS the erase (SPEC.md 27.2), because a caption band is exactly a
; glyph tall - so nothing erases a caption any more and the twin is gone.

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

    mov di, tm_str              ; pad to the band's width, in place
    xor cx, cx
.len:
    cmp byte [di], 0
    je .lend
    inc di
    inc cx
    cmp cx, TM_LCELLS
    jb .len
.lend:
    mov ax, TM_LCELLS
    sub ax, cx                  ; ...however many spaces that takes
    jbe .term                   ; already the full width, or over it
    mov cx, ax
    mov al, ' '
.sp:
    mov [di], al
    inc di
    loop .sp
.term:
    mov byte [di], 0

    mov cx, [tm_cx]
    add cx, TM_PEN
    mov dx, [tm_rowy]
    mov si, tm_str
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    clc                         ; ...and CF = 0 says so. The pops below write
.out:                           ; no flags, so tm_rowok's CF survives them
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    call tm_rowr
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
; tm_rowr - CX = the right edge of the band the row being drawn may use
; in:  [tm_rowx], and [tm_hsb] filled by the paint that is running
; out: CX = the edge; every other register preserved (flags)
;
; TM_RW everywhere, less the scroll bar on the heap page (SPEC.md 28.4.4).
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
    cmp byte [tm_view], 2
    jne .out
    cmp cx, [tm_hsb]            ; the bar's x1: does this band reach it?
    jb .out
    mov cx, [tm_hsb]
    dec cx
.out:
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
    mov ax, [tm_kb+SK_CLAIM]    ; the claimed half EXCLUDES the purgeables,
    mov bx, [tm_kb+SK_HEAP]     ; because no row below shows one (28.4.1) -
    call tm_kpair               ; and it is SK_CLAIM that does the excluding
                                ; now, so the two pairs on this one line
                                ; cannot disagree about what claimed MEANS:
                                ; RAM's used half is SK_KERN + this word
.built:
    call tm_rowsum              ; unchanged since the last refresh? then the
    mov bx, tm_elck + 2*TMC_LINE            ; pixels on screen are already right
    call tm_elchk
    jc .out

    mov ax, [tm_liny]           ; SPEC.md 28.5.1: placed, padded and drawn as
    call tm_lrun                ; ONE opaque run - no fill, no blank interval
    jnc .draw                   ; ...unless a clip edge crosses it, in which
    mov word [bx], 0            ; case nothing was drawn and the key tm_elchk
    jmp short .out              ; just recorded is a lie that would keep this
.draw:                          ; line stale until its content moved again
    cmp byte [tm_view], 0
    je .out
    mov word [tm_sqox], TMM_HSQ_X   ; ...and the claim legend square, last, over
    mov si, tm_pat_clm              ; the gap the run just lettered white.
    call tm_sq_pat                  ; [tm_rowx]/[tm_rowy] are tm_lrun's - it
.out:                               ; places the band before it draws it
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
    jne .done                   ; now - ungated this would paint the interior
                                ; across the process list

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
                                ; 42.2): the save-under and the clipboard.
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
;                (nonzero) - the System row adds the kernel's own claims,
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
%define OS88UI_BARONLY
%define OS88UI_SCROLL
%include "os88ui.inc"

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
tm_snapshot equ tm_win + 2     ; osapi_sys_snapshot: scheduler + instance table
tm_claims   equ tm_snapshot + SYS_SNAPSHOT_SIZE   ; osapi_claim_snapshot: the claim map
tm_kb       equ tm_claims + CLAIM_SNAPSHOT_SIZE   ; osapi_sys_kb: the kernel's footprint and the
                                ; heap's totals, refreshed by tm_view_begin
                                ; and by every sample

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
                                 ; meter's numerator (SPEC.md 28.7), normalised
                                 ; alongside tm_total so the ratio is one scale
tm_idle     equ tm_idlec + 4  ; the idle task's slot, 0xFF = none answered
tm_load     equ tm_idle + 1   ; latest load, 0..100
tm_told     equ tm_load + 2   ; previous sch_cycles snapshot
tm_tnew     equ tm_told + MAX_TASKS * 4   ; current snapshot
tm_tdif     equ tm_tnew + MAX_TASKS * 4   ; per-task interval cycles
tm_state    equ tm_tdif + MAX_TASKS * 4  ; T_STATE snapshot
tm_pstate   equ tm_state + MAX_TASKS  ; previous sample's T_STATE (appeared rule)
tm_self     equ tm_pstate + MAX_TASKS  ; our own slot ([sch_cur] at snapshot)
tm_iold     equ tm_self + 1  ; previous I_CYC snapshot
tm_inew     equ tm_iold + INST_MAX * 4  ; current snapshot
tm_idif     equ tm_inew + INST_MAX * 4  ; per-instance interval callback cycles
tm_ist      equ tm_idif + INST_MAX * 4  ; I_STATE snapshot
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
tm_coop     equ tm_ikb + INST_MAX * 2  ; the scheduler mode at the last sample
tm_mrow     equ tm_coop + 1  ; the memory view's row cursor
tm_mfit     equ tm_mrow + 2  ; ...and whether the row it names is on screen
tm_sqox     equ tm_mfit + 1  ; ...and where its legend square starts
tm_sqp      equ tm_sqox + 2  ; ...and its texture, or 0 for no square. A
                                ; REQUEST, not a draw: tm_mrow_close erases
                                ; the band before it letters it, so a square
                                ; drawn while the row was being composed
                                ; would be erased again
tm_elck     equ tm_sqp + 2  ; the last drawn state of the non-row
                                ; elements, one word each (TMC_*)
tm_rowx     equ tm_elck + TMC_N * 2  ; the left edge of the row being drawn: tm_cx
                                ; for column 0, one TM_COLW on for column 1
tm_xbarw    equ tm_rowx + 2  ; the XMS bar's black run, px (TMC_XBAR)
tm_penx     equ tm_xbarw + 2  ; where the row's text starts: the process
                                ; list letters from tm_rowx+6 and the memory
                                ; list from tm_rowx+16, and tm_row_draw has to
                                ; be told which - drawing a process row at the
                                ; memory list's pen puts it 10px right of the
                                ; text it is replacing
tm_ci       equ tm_penx + 2  ; tm_row_draw's chunk index (BP is the task's
                                ; instance record and cannot be borrowed)
tm_ckb      equ tm_ci + 2  ; ...and the running check-word pointer, for
                                ; the same reason
tm_rowck    equ tm_ckb + 2  ; the last drawn content of each row, a
                                ; word per CHUNK (tm_chunksum), plus one
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
tm_view     equ tm_rowck + TM_NCK * TM_NCHUNK * 2  ; 0 = performance, 1 = memory, 2 = heap
tm_inm      equ tm_view + 1  ; I_NAME snapshots
tm_rcyc     equ tm_inm + INST_MAX * 16  ; per-ROW cycles: task + instance, dword
tm_total    equ tm_rcyc + TM_ROWS * 4  ; sum of the rows, dword
tm_pct      equ tm_total + 4  ; per-row share, 0..100
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
tm_col2rows equ tm_colrows + 2  ; rows in each LATER column, which starts at the
                                ; top and so holds more - A DIVISOR, never 0
tm_maxrow   equ tm_col2rows + 2  ; rows this SCREEN shows, <= TMM_ROWS: derived
                                ; from [vid_dock_y0] so the window never
                                ; overlaps the dock (SPEC.md 28.1)
tm_totkb    equ tm_maxrow + 2  ; total conventional RAM (int 12h, boot)
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

tm_hcolrows equ tm_spawned + 1  ; the HEAP page's column-0 depth
                                       ; (SPEC.md 28.4) - its list starts
                                       ; higher than the memory view's, so it
                                       ; wraps later. Frame-derived, never
                                       ; dock-derived: see tm_init
tm_avl      equ tm_hcolrows + 2  ; OSAPI_MEM_AVAIL's pair, banked once a
tm_maxrun   equ tm_avl + 2  ; refresh by tm_hsnap: total available,
                                       ; and the largest single run of it.
                                       ; Both captions read them and mem_avail
                                       ; walks the claim table once per
                                       ; candidate, so it is not a call to
                                       ; make twice (SPEC.md 28.4.1)
tm_xoff     equ tm_maxrun + 2  ; 0, or TMM_XSHIFT on a machine with no
                                       ; store above 1MB: how far the memory
                                       ; view's header, rows and frame rise
                                       ; where the XMS bar would have been.
                                       ; SET ONCE, by tm_init, and never from
                                       ; a draw - the frame is SIZED from it,
                                       ; so a value that could change under a
                                       ; live window would leave the height
                                       ; and the row placement disagreeing.

tm_htop     equ tm_xoff + 2  ; the heap page's scroll position: the
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
tm_hrows    equ tm_hskip + 2  ; ...and how many rows the last walk
                                       ; produced in all: the bar's `total`,
                                       ; and what tm_hclamp measures against
tm_slow     equ tm_hrows + 2  ; byte: intervals since the last paint of a
                                       ; SLOW page (SPEC.md 28.6). A byte and
                                       ; not a word: TM_SLOW is 4
tm_hpad     equ tm_slow + 2  ; ...and how many of the rows this paint
                                       ; drew were tm_mrow_nolast's BLANKS,
                                       ; which belong to the layout and not to
                                       ; the table (SPEC.md 28.4.4)
tm_hsb      equ tm_hpad + 2  ; the shared bar's seven-word block
                                       ; (SPEC.md 13.10.2): x1 y1 x2 y2, then
                                       ; total, fit, pos

TM_BSS_TOTAL equ tm_hsb + 14 - os88_image_end
