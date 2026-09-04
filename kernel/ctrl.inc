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
CP_CH     equ 132               ; content height (frame 151 - border - title;
                                ; the frame grew from 120 when the Date/Time
                                ; page took its two option rows - SPEC.md
                                ; 31.5 - rather than that page being cramped,
                                ; and from 140 when a THIRD driver published a
                                ; page and the list needed a ninth row. Every
                                ; other page just has more white below.)
                                ;
                                ; 151 IS THE MINIMUM THAT HOLDS NINE ROWS and
                                ; it was chosen as the minimum on purpose: CGA
                                ; is 200 rows with a 20px bar and a 24px dock,
                                ; so wm_fit's ceiling is 176 - 1 - 20 = 155 and
                                ; the panel has 4px of margin left. The next
                                ; page after this one does not fit the machine
                                ; this project is calibrated against, and the
                                ; answer then is a scrolling list, not a taller
                                ; window.

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
; The staging buffer for SPEC.md 2.8.6's page strings (see cp_stage, below).
; **A %assign and not an `equ`**, because CPS's length guard is a preprocessor
; `%if` and the preprocessor cannot see an `equ`. 28 is not arbitrary: a page
; string has to fit the pane's 27-character limit (the Display caption's own
; comment records it), so 27 + NUL is the widest string this panel can
; usefully carry. CPS asserts every string against it at ASSEMBLY time and
; cp_stage bounds the copy at RUN time, so neither a new string nor a
; hand-written `db` can walk off the end of it.
%assign CP_SBUF 28

; CPS - define one image string, and refuse it if it cannot be staged
%macro CPS 2
%1: db %2, 0
%strlen CPSLEN %2
  %if CPSLEN + 1 > CP_SBUF
    %error "ctrl: a Control Panel page string is longer than CP_SBUF - it cannot be staged, and it does not fit the pane either (SPEC.md 31.1)"
  %endif
%endmacro

; CPSTAGE  <string>          - load an image string and stage it
; CPSTAGEX <table>[, index]  - load one THROUGH an image table and stage it
;
; **These two are the only sanctioned way to name a string above**, and
; tools/os88ovlchk.py enforces it: a `.modc` data label mentioned anywhere but
; its own definition, another data directive in the image, or one of these
; macros fails the build. That is what closes the hole the old gate had - it
; could see `lodsb` in a module image and could not see a MISSING `call
; cp_stage` or a MISSING `cs:`, both of which draw rubbish on a page a user
; opens and neither of which moves a single byte.
%macro CPSTAGE 1
    mov si, %1
    call cp_stage
%endmacro
%macro CPSTAGEX 1-2 si
    mov si, [cs:%2 + %1]
    call cp_stage
%endmacro

CP_PMX    equ 2                 ; left margin for the heading and the caption
CP_PHY    equ 6                 ; 'Scheduler' heading text row
CP_PGX    equ 4                 ; radio glyph left edge
CP_GW     equ OS88UI_GW         ; the shared control's 12x12 glyph
                                ; (apps/os88ui.inc). NAMED rather
                                ; than repeated: nine layout equs
                                ; below derive row bottoms from it
CP_PR0Y   equ 26                ; first radio row: glyph top
CP_PROWH  equ 20                ; row pitch: row i glyph top = CP_PR0Y + i*20
CP_PLX    equ CP_PGX + 18       ; 22: label text x
CP_PLDY   equ 2                 ; label baseline offset (8px font in 12px box)
CP_PCAPY  equ 74                ; caption text row
CP_PCAP2  equ CP_PCAPY + 9      ; 83: second caption row. Only the mono answer
                                ; uses it - the caption is the lowest thing on
                                ; every page, so a second 8px line fits inside
                                ; CP_CH (121) with room to spare

%ifdef OS88_THEME
; --- Theme page (SPEC.md 76.4): TWO groups, so a 16px pitch and not CP_PROWH's
; 20. The Sound page's CPS_ROWH is the precedent - four radios, two headings
; and a two-line caption do not fit CP_CH (132) at the Scheduler page's pitch,
; and the pane is the same height on every adapter.
CPH_ROWH  equ 16                ; row pitch inside a group
CPH_R0Y   equ 18                ; Bright glyph top
CPH_R1Y   equ CPH_R0Y + CPH_ROWH        ; 34: Dark
CPH_R2Y   equ CPH_R1Y + CPH_ROWH        ; 50: Color (SPEC.md 76.12)
CPH_H2Y   equ 70                ; 'Window Animations' heading text row
CPH_A0Y   equ 82                ; On glyph top
CPH_A1Y   equ CPH_A0Y + CPH_ROWH        ; 98: Off; + 12px = 110, inside CP_CH
                                ; (132). The two-line caption that used to sit
                                ; under this is what the third theme row is
                                ; drawn in - the page had no other room and
                                ; SPEC.md 76.4's argument for the caption was
                                ; always the weaker of the two

; ...and its hit bands, contiguous WITHIN a group and dead BETWEEN them: a
; click in the gap belongs to neither group, where the Scheduler page's two
; bands can be contiguous because it has only one group to be in.
CPH_B0Y1  equ CPH_R0Y - 4                   ; 14: theme row 0 top
CPH_B0Y2  equ CPH_R0Y + CP_GW + 1           ; 31: ...bottom = row 1 top - 1
CPH_B1Y2  equ CPH_R1Y + CP_GW + 1           ; 47: theme row 1 bottom
CPH_B2Y2  equ CPH_R2Y + CP_GW + 1           ; 63: theme row 2 bottom
CPH_A0Y1  equ CPH_A0Y - 4                   ; 78: anim row 0 top
CPH_A0Y2  equ CPH_A0Y + CP_GW + 1           ; 79: ...bottom
CPH_A1Y2  equ CPH_A1Y + CP_GW + 1           ; 95: anim row 1 bottom

; ...and the SCREEN SAVER button (SPEC.md 79.7), under both groups in the last
; band of the pane the two radio groups leave: row 1's glyph ends at 110 and
; the content box is CP_CH tall, so a 16px button at 114 ends at 129 with two
; rows to spare. It is a BUTTON and not a third group because what is behind
; it is four checkboxes and two numbers, which is a window's worth (SPEC.md
; 79.7) - the same call HDD.DRV's page makes for its disk tool and ETHER.DRV's
; for its addresses (SPEC.md 52.2, 72.7).
CPH_BTX   equ CP_PGX
CPH_BTY   equ 114
CPH_BTW   equ 104                           ; 'Screen Saver' is 96px of glyph
CPH_BTH   equ 16
CPH_CSAVE equ 6                             ; its control id: 1..3 are the
                                            ; theme rows and 4..5 the zoom's

%if CPH_BTY + CPH_BTH > CP_CH
%error "ctrl: the Theme page runs past its pane - see SPEC.md 76.4"
%endif

%endif

; --- radio hit bands, pane-relative y: generous, contiguous ------------------
CP_B0Y1   equ CP_PR0Y - 4                     ; 22: row 0 (Pre-emptive) top
CP_B0Y2   equ CP_PR0Y + CP_GW + 3             ; 41: row 0 bottom = row 1 top-1
CP_B1Y2   equ CP_PR0Y + CP_PROWH + CP_GW + 3  ; 61: row 1 (Cooperative) bottom
section .text


; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
; Content is CP_CW x CP_CH (318x132). The onkey word was 0 for the panel's
; whole life - it is mouse-only, every control on the five static pages being
; a radio, a check box or a button - and it is non-zero on kern_big alone,
; where SPEC.md 31.9.2 gave a DRIVER's page a typed field (SPEC.md 62.9.15).
%ifdef KERN_BIG
cp_tpl:     dw 160, 130, 320, 151, cp_ttl, cp_paint, cp_onkey, cp_onclick
%else                           ; **NO KEYBOARD ON kern_small** (SPEC.md
                                ; 62.9.15): the one field in the machine that
                                ; can be typed into is the RAM disk's size, and
                                ; kern_small has no RAM disk. The word goes
                                ; back to 0, which is what it was for the whole
                                ; life of the panel before SPEC.md 31.9.2
cp_tpl:     dw 160, 130, 320, 151, cp_ttl, cp_paint, 0, cp_onclick
%endif

; -----------------------------------------------------------------------------
; cp_kinit - KD_INIT (SPEC.md 29.3): install the panel's extra edges
; in:  BX = window, DI = 0 (stateless), SI = record; UI task, no lock, the
;      window is not yet visible
; out: nothing; preserves all registers
;
; The panel had no init proc at all - a stateless task-less singleton needs
; none - and it has one now for the two words wm_create does not copy:
; W_ONMOUSEUP and W_ONDRAG are not template words (SPEC.md 13.7/13.8.2), for
; the reason the template is eight words and growing it would read one past
; every existing package's.
;
; **BOTH KERNELS INSTALL THE RELEASE** (SPEC.md 13.8.4). W_ONMOUSEUP is in
; kern_small's window record too (WIN_SIZE 28 includes it), and a DRIVER's
; page acts on the release now - so a panel without the edge is a Hard Drive
; page whose Mount, Format and Install do nothing at all, and the same of
; ETHER's Renew and Set Up and os88net's Connect. W_ONDRAG is kern_big's alone
; (SPEC.md 13.8.2), and so is the arm cleared below: kern_small's release
; forwards DSV_CPUP and spends no arm of its own.
;
; It is also where the arm is cleared, because a panel that was closed
; mid-gesture would otherwise leave one armed for the NEXT one to fire - and
; unlike the file dialog this window is an INSTANCE that can be closed from
; the dock, the bar and its own close box.
; -----------------------------------------------------------------------------
cp_kinit:
    push ax
%ifdef KERN_BIG
    mov byte [cp_down], 0
    xor ax, ax
    call COLD_SEG:os88ui_arm_f
%endif
    mov ax, cp_onup
    call wm_onmouseup           ; .text calling .text: apps.inc's kind table
%ifdef KERN_BIG                 ; is resident and so is this proc
    mov ax, cp_ondrag
    call wm_ondrag
%endif
    pop ax
    ret
cp_ttl:     db 'Control Panel', 0
cp_sname:   db 'Control', 0     ; KD_NAME: fits the memory view's 7-char
                                ; NAME column, which is the tighter of the two

; Deferred-repaint flag, drained by ui_task step 3 (SPEC.md 13/31.2). Inline
; .text data, not .bss, so it is initialised on disk and needs no cp_init -
; the osapi_seed trick from kernel.asm.
cp_dirty:   db 0                ; 1 = the mode changed, repaint everything
cp_wdirty:  db 0                ; 1 = a setting changed and SYSTEM.CFG is owed
                                ; a write. Deferred to the panel's teardown
                                ; (cp_flush_close) rather than taken on the
                                ; click, so a session of fiddling costs ONE
                                ; floppy write instead of one per click.
                                ;
                                ; **SETTING THIS IS ALL A PAGE MAY DO**, and
                                ; all a DRIVER's page may do either - the
                                ; hard disk's Mount was the last exception and
                                ; is gone (SPEC.md 31.8/51.9). Nothing outside
                                ; cp_flush_close_x calls cp_flush_x, and there
                                ; is no longer a .text thunk to call it
                                ; through. A SYSTEM.CFG write is 2+ SECONDS of
                                ; frozen UI on the floor machine (mount, data,
                                ; FAT, directory, FAT, remount, all under the
                                ; gfx lock with the cursor stopped), landing
                                ; in the middle of a click. Every setting
                                ; added since has looked like the exception
                                ; and none has been one: what the setting DOES
                                ; already happens on the spot, and only the
                                ; RECORD of it waits.
                                ; .text and not .bss, like everything else
                                ; here: nothing zeroes .bss (-f bin)
cp_dsave:   db 0                ; the last SYSTEM.CFG write: 0 = fine, 1 =
                                ; wrong disk in A:, 2 = the write failed. Said
                                ; as a TOAST at the moment it happens (SPEC.md
                                ; 60.6) rather than carried to a page caption
                                ; to be found next session; what is left of
                                ; its old job is telling cp_flush_close_x
                                ; whether to keep [cp_wdirty] and retry

; The selected item, as a RECORD index into cp_items - never as a position in
; the drawn list (SPEC.md 31.10.1). That distinction is what lets a hidden row
; sit anywhere in the table, and it is why ui.inc and drv_boot may keep saying
; CP_ITIME and CP_IDRV: those name pages, and a page does not move because
; another one is not being shown. Inline .text data for the same reason as the
; rest; 0 = Scheduler is the boot selection, so the panel opens on it.
cp_sel:     db 0
cp_dtop:    db 0                ; the FIRST DRIVER ROW SHOWN (SPEC.md 31.1.1).
                                ; The Drivers pane fits CP_DVIS rows and
                                ; drv_tab has more, so the list scrolls; this
                                ; is where the window into it starts. Inline
                                ; .text data for the same reason [cp_sel] is

%ifdef KERN_BIG
; --- SPEC.md 13.8.3: the panel acts on the RELEASE ---------------------------
; THE FIVE PAGE LADDERS ARE THE HIT TEST. Splitting each into a hit and an act
; would be five SECOND descriptions of geometry that then have to agree with
; five painters - the drift fm_hit discipline exists to stop, reintroduced by
; the fix for it. So a ladder is RUN to identify: [cp_probe] makes every
; action site report its id and return instead of acting, and the same ladder
; run with the flag clear does the thing. Which of THREE a pass is, HDD.DRV's
; own [hd_pgpr] exactly (SPEC.md 13.8.4):
;
;   0  act - the release, once it knows the pointer is still on the control
;   1  identify only - the drag, and the release's own hit test
;   2  the PRESS: a row or a field still SELECTS (a safe prefix action keeps
;      the press, SPEC.md 13.6) while a button only reports its id
;
; That third mode is why cp_ctl takes an id of 0: it is what a site that is
; not an armable control passes, and it is the only thing that can tell the
; two species apart inside one ladder. Without it the Display page's adapter
; rows and the Date/Time page's field rows - the two sites that keep the
; press - moved the selection on every DRAG packet and again on the release,
; from inside a pass whose whole contract is that it acts on nothing.
;
; Inline .text data with the rest of this block, and for the same reason - the
; module's own image is loaded and dropped, so nothing that must survive a
; drop can live in it.
cp_probe:   db 0                ; 0 = act, 1 = identify only, 2 = the press
cp_ctlid:   db 0                ; ...and what the ladder resolved to (0 = none)
cp_down:    db 0                ; which control is DRAWN pressed (0 = none).
                                ; cp_setdown is its one writer and the six
                                ; glyph sites and three button sites its
                                ; readers, so a repaint arriving mid-gesture
                                ; draws the held control held
%endif

; --- item table: one 8-byte record per panel item (SPEC.md 31.1) ------------
; The 4th word is reserved (a future page onkey proc) and keeps the stride a
; power of two, so index -> record is three shl-by-1s and no CL.
CP_I_NAME   equ 0               ; -> list name, ASCIIZ
CP_I_PAINT  equ 2               ; -> page paint proc (in DI/BP = pane origin)
CP_I_CLICK  equ 4               ; -> page click proc (in DI/BP, CX/DX = rel)
CP_ISTRIDE  equ 8

; ANY ROW HERE MAY BE HIDDEN, AND ITS POSITION DOES NOT MATTER (SPEC.md
; 31.10.1). Display is the one that is - a machine with one adapter has
; nothing to choose between - and it USED to have to be last, because
; [cp_sel] and every consumer of it indexed this table by the row's position
; in the DRAWN LIST. Hiding a row in the middle then renumbered every row
; below it, which is a second opinion about what the user clicked on at four
; call sites, so "last" was doing real work and was a real constraint: it had
; been spent here and by drv_tab's settings bitmap (SPEC.md 51.2.1), and
; SPEC.md 41.12 nearly spent it a third time.
;
; The two spaces are separated now. [cp_sel] and every routine below name a
; RECORD (an index into this table, stable for the life of the build);
; cp_list and cp_pick work in VISIBLE ORDINALS, and cp_v2r is the one place
; that converts. So [cp_hide] can carry a bit for any row, and the order here
; is free.
cp_items:
    dw cp_s_sched, cp_sched_paint, cp_sched_click, 0
    dw cp_s_time,  cp_time_paint,  cp_time_click,  0
%ifdef OS88_DRIVERS             ; SPEC.md 51.0: kern_small can load no driver,
    dw cp_s_drv,   cp_drv_paint,   cp_drv_click,   0
%endif                          ; so the page has nothing to list and is not
                                ; offered. The BODIES stay compiled - this
                                ; image is a module on both builds, so gating
                                ; them would move no resident byte and would
                                ; put a %ifdef through the Sound page, which
                                ; stays
    dw cp_s_snd,   cp_snd_paint,   cp_snd_click,   0
    dw cp_s_vid,   cp_vid_paint,   cp_vid_click,   0
%ifdef OS88_THEME
    dw cp_s_thm,   cp_thm_paint,   cp_thm_click,   0
%endif
cp_items_end:
CP_ITEMS  equ (cp_items_end - cp_items) / CP_ISTRIDE
CP_ITIME  equ 1                 ; the Date/Time item: ui.inc selects it by
                                ; name when the menu bar clock is clicked
                                ; (SPEC.md 12.1/31.5)
; ...and the three below it SHIFT when the Drivers row is not there
; (SPEC.md 51.0). An index into cp_items is what [cp_sel] holds, so every
; number after the gap has to move with it - written out per arm rather than
; derived, because deriving it would hide exactly the fact that makes it
; fragile: these are POSITIONS in a table one %ifdef above, and the two must
; be read together.
%ifdef OS88_DRIVERS
CP_IDRV   equ 2                 ; ...and the Drivers item, which drv_notice
                                ; opens the panel on when a driver did not
                                ; load at boot (SPEC.md 51.3)
CP_ISND   equ 3                 ; ...and Sound, the tone tier's route (34.8)
%ifdef OS88_THEME
CP_ITHM   equ 5                 ; ...and Theme (SPEC.md 76.4)
%endif
CP_IVID   equ 4                 ; ...and Display, the one row that is HIDDEN
                                ; on a machine with a single adapter (SPEC.md
                                ; 39.11.1). vid_probe_avail names it by this
                                ; rather than by "the last one", which is what
                                ; frees the order above
%else
CP_ISND   equ 2                 ; no Drivers row, so Sound, Display and Theme
CP_IVID   equ 3                 ; each come up one (SPEC.md 51.0)
%ifdef OS88_THEME
CP_ITHM   equ 4
%endif
%endif

; How many static rows the list is showing RIGHT NOW - CP_ITEMS, or one fewer
; when the Display page is hidden. It is the boundary between a static row and
; a driver's (SPEC.md 31.9), so every site that used to compare against
; CP_ITEMS reads this instead, and there is exactly one place that writes it:
; vid_probe_avail, which is also the only thing that could ever answer the
; question (SPEC.md 39.11.1). [vid_avail] is fixed for the life of the
; machine, so this is written once at boot and read for ever after.
;
; Initialised .text data, like cp_sel and cp_tsel - and the initialiser is the
; SAFE answer: a build whose probe never ran shows the page rather than losing
; a row of somebody else's.
cp_nst:   db CP_ITEMS

; ...and WHICH of them are not shown: bit n = static record n is hidden. One
; byte, so CP_ITEMS may reach 8 before this needs to be a word - and the
; %error below already stops the list growing that far.
;
; It is the reason cp_items has no ordering rule any more. [cp_nst] alone
; could only ever say HOW MANY rows are shown, which is enough to draw a
; shorter list and not enough to say WHICH row a drawn one is; that gap is
; exactly what forced the hidden row to be last. Written once, beside
; [cp_nst], by the same single writer.
cp_hide:  db 0

; THE LIST HAS ROOM FOR NINE ROWS AND NO MORE: row i's bar runs
; CP_I0Y + i*CP_IROWH .. + CP_IBH - 1, so row 8 ends at 129 inside a content
; box 132 tall and row 9 would run through the bottom border onto the
; desktop. Six static items plus the THREE drivers that publish a page
; (SPEC.md 31.9 - the hard disk's, the debug monitor's and the network link's;
; the sound driver publishes none) is exactly nine. The count was two until
; NET.DRV (docs/NET-PLAN.md), and growing this constant is what the guard
; below is FOR: it is a build-time %error, so the third page could not be
; added without the window being made to hold it. CP_ITEMS and not [cp_nst],
; because the guard has to hold on the machine that shows every row.
;
; A FOURTH page does not fit CGA (see CP_CH), so the answer then is a
; scrolling list rather than another 14 rows of window.
%if CP_ITEMS + 3 > (CP_CH - CP_I0Y) / CP_IROWH
%error "ctrl: the item list can overflow its pane - grow CP_CH (SPEC.md 31.1)"
%endif

; List names are at most 9 characters (72px): the selection bar ends at
; CP_IBX2 = 85 and the name starts at CP_IX = 6, so a tenth glyph would
; cross the divider. 'Scheduler' and 'Date/Time' are both exactly at it.

%ifdef OS88_THEME
; --- Theme page (SPEC.md 76.4) ----------------------------------------------
; Two radios, exactly the Scheduler page's shape: the chrome's two pens, and
; the ground the desktop is laid with. It changes no geometry and claims no
; memory, so the only thing a switch owes is a repaint - which is [cp_dirty],
; the flag that already means "every window has to be told".
cp_s_thm:   db 'Theme', 0       ; list name AND the page heading, so RESIDENT
                                ; (SPEC.md 31.9). The page's own strings are
                                ; in the image, at cp_modstr below
%endif

cp_s_sched: db 'Scheduler', 0   ; list name AND the page heading, so RESIDENT

; --- Display page (SPEC.md 31.10/39.11) --------------------------------------
; One radio row per adapter the machine actually HAS, and a button that moves
; the machine onto the selected one. The rows are the ADAPTERS, not the modes
; the kernel could theoretically set: each adapter has exactly one graphics
; mode here (SPEC.md 39), so naming the resolution beside it is a description
; rather than a second choice.
;
; The names are indexed by VID_* and so is everything else on this page - the
; kernel's own enum is the row order, which is what keeps the label, the dot,
; the click and vid_switch from ever disagreeing about which row is which.
cp_s_vid:   db 'Display', 0     ; list name AND the page heading, so RESIDENT

%ifdef KERN_BIG
; The DESKTOP line (SPEC.md 31.10.2): one row of three radios under the button.
; ONE choice and not two controls - a single display has no layout, so a
; checkbox plus a direction would carry a fourth state that means nothing.
;
; It is not drawn at all on a machine vid_dual_ok refuses, which is SPEC.md
; 31.10.1's own answer one level down: that page stopped being drawn on a
; single-adapter machine rather than carrying a caption apologising for
; itself, and a permanently greyed control here would have to displace the
; Set Primary button's caption on the one page where that button is the main
; control - the caption being the page's ONE line, and the whole of what
; SPEC.md 31.10.3 spent to put a label on this row.
;
; The row is LABELLED (SPEC.md 31.10.3), because 'Single / Right / Below' names
; three arrangements of something the row never said the name of - and the one
; other place in the panel where a control's subject is not obvious from its
; own words is the page heading, which is a label in exactly this position.
; The colon is what says it belongs to the row under it rather than being a
; fourth thing on the page.
; (the label, the three radio names and their table are in the image, at
; cp_modstr below; CPV_DESKN is measured off the label there, beside it)
%endif

; Display page geometry, PANE-relative. The Sound page's tighter 16px row
; pitch rather than the two-row pages' 20, because three adapter rows, a
; button, SPEC.md 31.10.2's desktop row and its label and a caption all have
; to fit CP_CH - and the %if guards below are what say so rather than a
; comment claiming it. Of 132 rows: adapters at 20/36/52, button 72..89,
; desktop label 94..101, desktop row 106..117, caption 122..129.
CPV_R0Y   equ 20                ; row 0 glyph top
CPV_ROWH  equ 16                ; row pitch: rows at 20, 36, 52
CPV_BTNX  equ CP_PMX            ; the Set Primary button, under the rows
CPV_BTNY  equ 72
CPV_BTNW  equ 100               ; 'Set Primary' is 11 glyphs = 88px, and
                                ; os88ui_btn centres it, so the box keeps the
                                ; 6px each side the 116/104 pair had
CPV_BTNH  equ 18
CPV_MLY   equ 94                ; SPEC.md 31.10.3's label for the desktop row.
                                ; It is NOT a control and has no hit band -
                                ; CPV_MB1 starts below it, so a press here
                                ; falls through rather than picking a radio
                                ; out of an x that means nothing
CPV_MY    equ 106               ; SPEC.md 31.10.2's desktop row: glyph top
CPV_MSTEP equ 74                ; ...and its radio pitch, so the hit test is
                                ; one divide like the adapter rows'. Glyphs at
                                ; 4/78/152, labels at 22/96/170, and the last
                                ; label ends at 210 inside a CP_PRGT of 221
CPV_CAPY  equ 122               ; caption row, LAST as it is on every page -
                                ; which is why the label went above it rather
                                ; than below (the caption was 96, then 112)
%if CPV_CAPY + 8 > CP_CH
%error "ctrl: the Display page's caption runs past the pane (SPEC.md 31.10.2)"
%endif
%if CPV_MLY + 8 > CPV_MY
%error "ctrl: the Display page's desktop label runs into its radios (31.10.3)"
%endif
                                ; (the desktop label's own width guard travels
                                ; with CPV_DESKN, which is measured off the
                                ; string and so lives in the image now)
%if CP_PLX + 2*CPV_MSTEP + 5*8 > CP_PRGT
%error "ctrl: the Display page's desktop radios run past the pane"
%endif
; Row hit bands: generous and contiguous, like every other page's (31.2). The
; bands are laid out for THREE rows whatever the machine has - a row that is
; not drawn is refused by cp_vid_row, not by the geometry.
CPV_B0Y1  equ CPV_R0Y - 4                   ; 16: row 0 top
CPV_BROWH equ CPV_ROWH                      ; ...and one band per row pitch
CPV_B2Y2  equ CPV_R0Y + 2*CPV_ROWH + CP_GW - 1   ; 63: row 2 bottom
; ...and the button's, which must start below them
CPV_BB1   equ CPV_BTNY - 4
CPV_BB2   equ CPV_BTNY + CPV_BTNH + 3
; ...and the desktop row's, which must start below THAT and below SPEC.md
; 31.10.3's label: generous and contiguous like every other band on the page
; (SPEC.md 31.2), so a press between two radios lands on one of them rather
; than on nothing - but a press on the LABEL lands on nothing, because a
; label is not a control and its x carries no radio.
CPV_MB1   equ CPV_MLY + 8
CPV_MB2   equ CPV_MY + CP_GW + 3
%if CPV_MB1 <= CPV_BB2
%error "ctrl: the Display page's desktop band overlaps the button's"
%endif
%if CPV_MB1 > CPV_MY
%error "ctrl: the Display page's desktop band starts below its own glyphs"
%endif

; --- Date/Time page (SPEC.md 31.5/37) ----------------------------------------
cp_s_time:  db 'Date/Time', 0   ; list name AND the page heading

; --- Drivers page (SPEC.md 31.6/51) ------------------------------------------
; One row per drv_tab row: a checkbox, the driver's name, and - in the caption
; - what the last load attempt actually answered. The click loads or unloads
; ON THE SPOT and then writes SYSTEM.CFG, so the box is never a promise about
; the next boot: what it shows is what is running right now.
cp_s_drv:   db 'Drivers', 0     ; list name AND the page heading
; Both 27 characters, which is the pane's limit: 222px of pane less CP_PMX,
; and font_str clips to the SCREEN and not to the pane (SPEC.md 5). The one
; string these replace was THIRTY, and painted through the window's right
; border into the desktop - invisibly, because until cp_drv_cap existed the
; caption was only ever drawn by a driver tick.
; OUTCOME, CAUSE - and one string per CAUSE. A message that says only that
; something failed sends the reader looking, and the two causes here want
; opposite things done about them: one is the user's disk and one is the
; disk's own answer.
;
; The SUBJECT is what these gave up when the strip moved into the clock's
; field and TOAST_MAX became 23 (SPEC.md 59.8): they read
; `Settings not saved: wrong disk in A:` while the bar had 50 cells to say it
; in. It is the right word to lose of the three - the panel the user just
; closed is the subject, and it was the only thing on screen - where dropping
; the cause would leave a message that cannot be acted on. The success line
; keeps it, being short enough to.
;
; `No Sys in A:` rather than `need disk A:`, because the two are not the same
; refusal and only one of them is this one: there IS a disk in the drive - the
; user has been working off it - and what it is not is the SYSTEM disk
; (drv_cfg_save asks dskw_stat for KERNEL.SYS, SPEC.md 51.5.1). `need disk`
; reads as an empty drive and sends the reader to look at the light.
cp_s_dsok:  db 'Settings Saved', 0
cp_s_dsdsk: db 'Not Saved: No Sys in '
cp_s_dsdrv: db 'A:', 0          ; the letter, stamped (SPEC.md 52.10.3): the
                                ; system volume is not A: on a machine booted
                                ; from a hard disk, and a message that names
                                ; the wrong drive is worse than one that names
                                ; none. This is the only string in the kernel
                                ; that needs it, and dsk_ltrtab's third entry
                                ; exists for it
cp_s_dserr: db 'Not saved: disk error', 0
; Indexed by [cp_dsave] + 1, so slot 0 keeps toast_say's "nothing to say" and
; SUCCESS gets a message of its own - a save is a thing the user asked for and
; waited seconds of floppy for, so silence is the wrong report. It only fires
; when [cp_wdirty] was set, because that is what cp_flush_close_x gates on:
; closing a panel nobody changed says nothing.
cp_savetab: dw 0, cp_s_dsok, cp_s_dsdsk, cp_s_dserr
CP_NSAVE  equ 4

; ...and the other end of the same conversation (SPEC.md 2.8.4): the panel's
; own code is a file on the system disk now, so OPENING it can fail for the
; same reason SAVING it can. It says the DISK and the DRIVE both, which is
; what the 24-cell field will just carry: the disk because that is what is
; wanted, and the letter because the load looks on exactly one volume -
; [dsk_bootvol] - and telling the user which one is the difference between a
; message they can act on and one they have to guess at.
;
; **IT USED TO NAME ITS SUBJECT AND NO LONGER CAN**, which is a change worth
; explaining rather than quietly making. It read `Panel Needs Sys Disk A:`,
; and the argument for the word `Panel` was that there is nothing on screen to
; infer the subject from - the panel never opened. There are TWO subjects now:
; the panel's own image (mod_need, SPEC.md 2.8) and SAVER.DRV behind the
; Screen Saver button (SPEC.md 79.7), and the second one fails with the panel
; plainly open and the button plainly just clicked. One message that names the
; wrong subject is worse than one that names none - which is this string's own
; argument about the drive letter, turned round - and what BOTH readers
; actually need is identical: put the system disk in that drive. A second
; string and a second table slot would have been 26 bytes of an image rung
; with 27 in it (docs/KERNEL-MEMORY.md), so this is also the affordable
; answer, and it is the right one first.
cp_s_noload: db 'Needs Sys Disk '
cp_s_nodrv:  db 'A:', 0         ; stamped beside cp_s_dsdrv, same rule
cp_opentab: dw 0, cp_s_noload
CP_NOPEN  equ 2

; --- Sound page (SPEC.md 31.7/34.8) ------------------------------------------
; The three row labels are FIXED and name TIERS, not whatever a driver called
; itself. The row used to be labelled from the driver's own DSV_NAME, which on
; a Sound Blaster read 'Sound Blaster' with the DSP tier up and 'AdLib' with it
; down - the label moved as the setting moved, so the one control on the page
; could not be read as a choice between two things.
cp_s_snd:   db 'Sound', 0       ; list name AND the page heading, so RESIDENT
cp_s_gone:  db 0                ; the empty string: what a vanished driver's
                                ; list row letters as (cp_item_name's guard)

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

; SPEC.md 31.5.1: is this draw a FULL one, or the once-a-second refresh? The
; tick changes digits and nothing else - the band's gaps, the two ':' and the
; selected field's black surround are all still on the glass from the last
; full draw - so an incremental pass draws seven opaque runs and NOTHING else.
; A byte rather than a register because cp_time_fld's AL and AH are already
; the field index and the field count.
; The page list row's PAPER (SPEC.md 31.11). The ink is [gfx_color] like every
; other label here, but this is the one surface whose ground is not the pane -
; a selected row is white on a black bar - so the run's other half is decided
; at the same branch that decides the pen and read back at the draw.
cp_ipap:  db CWHITE

; Which ONE control cp_ctlpaint is redrawing, and whether the next control emit
; is it (SPEC.md 31.1.3). [cp_only] = 0 means "all of them", which is what every
; caller outside cp_ctlpaint leaves it at; [cp_skip] is ONE-SHOT - cp_dngly and
; cp_dnbtn arm it and cp_gemit/cp_bemit consume it - so a draw site with no
; pressed look at all (cp_vid_rows') cannot inherit a stale one.
cp_only:  db 0
cp_skip:  db 0
cp_tfull: db 0
cp_tpair: dw 0                  ; ...and the field's (ink, bg), decided where
                                ; the selection is known and read where the
                                ; run is drawn
cp_tendp: dw 0                  ; cp_time_pick scratch: one past the last
                                ; ACTIVE field record

; The Display page's PENDING adapter - which row the dot is on, not which
; adapter is running (that is [vid_kind], read live like every other page
; reads its subject). The two differ exactly between picking a row and
; pressing Activate, which is the whole reason the button exists.
;
; 0FFh = "follow the live one", and it has to be a sentinel rather than a
; kind: this is initialised .text data like cp_sel and cp_tsel above, so it
; is fixed at assembly time, and which adapter a machine boots on is not
; known until vid_detect has run. cp_vid_pick resolves it - and re-resolves
; it whenever it names something this machine cannot do, which is what makes
; the byte safe to keep across a switch.
cp_vsel:  db 0xFF

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
; =============================================================================
; The module image (SPEC.md 2.8, docs/ONDEMAND-PLAN.md 2)
;
; Everything above this line is DATA, in ordinary .text in the kernel's own
; segment, and it STAYS THERE - a module moves code and never data, which is
; what spares this file every string-staging question (SPEC.md 31.9's
; DSV_CPNAME trap) and lets every reference below stand unchanged.
;
; Everything below is `.modc`: cut out of kernel.bin by tools/os88mod.py,
; shipped as CTRL.DRV, and read into a heap claim when the panel is opened.
; It is `.cold` in every respect but its address - vstart=0, CS of its own,
; DS still KERNEL_SEG, out through the cw_* shims - which is precisely why it
; could leave. This module used to live in .fartext, copied down to a segment
; of its own at boot; SPEC.md 33 records why that was retired.
;
; WHY THE PANEL AND NOT SOMETHING WITH A NARROWER SEAM: it writes SYSTEM.CFG
; to the system disk when it CLOSES (SPEC.md 31.8/51.5.1), so every session
; that changes anything already requires that disk to be reachable. Loading
; the image asks for nothing the feature did not already ask for. That is the
; whole test (docs/ONDEMAND-PLAN.md 1) and it is about the user rather than
; about the code.
;
; TWO ROUTINES DO NOT GO, and both would otherwise force a load at a moment
; when there is no panel at all:
;
;   cp_tick_due_x  is called once a SECOND from ui_task, panel or no panel.
;   cp_drv_gone_x  is called from drv_release whenever a published driver
;                  detaches - which includes drv_shutdown at reboot, AFTER
;                  cp_flush_close has dropped this image. Loading it back to
;                  fix a byte on the way to a warm boot is not a thing to do.
;
; Both are self-contained and both stay in .cold, where they cost what they
; always cost. cp_drv_gone_x carries its own copy of cp_drv_item's five
; instructions rather than far-calling across the boundary for them.
; =============================================================================
; The entry indices, which are the image's ABI with its own thunks. They are
; here rather than beside those thunks because the header below is built with
; `times MOD_NENT - CP_NENT`, and TIMES wants a constant it has already seen.
CPE_PAINT   equ 0
CPE_ONCLICK equ 1
CPE_FLUSH   equ 2
CPE_TICK    equ 3
CPE_ONUP    equ 4               ; SPEC.md 13.8.3's two edges. The RELEASE is on
%ifdef KERN_BIG                 ; both kernels - a driver's page acts on it
CPE_ONDRAG  equ 5               ; (SPEC.md 13.8.4) - and the drag is kern_big's
CP_NENT     equ 6               ; alone, so CP_NENT differs between the builds.
%else                           ; Which is safe because MOD_H_NENT is IN the
CP_NENT     equ 5               ; header and mod_check compares it against the
%endif                          ; file it just read, so a CTRL.DRV with the
                                ; wrong count is refused rather than
                                ; dispatched into
CPFP        equ mod_fp + MOD_CTRL * MODFP_STRIDE

section .modc                   ; the on-demand image starts HERE, at its own
                                ; offset 0, so the header below is the first
                                ; thing in the file
modc_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER                  ; MOD_H_VER - 5, so ld_check_hdr (3) and
                                ; drv_check (4) both refuse this file
    db MOD_CTRL                 ; MOD_H_ID
    dw BUILD_NUM                ; MOD_H_BUILD (SPEC.md 2.8.2): refuses a
                                ; CTRL.DRV from another COMMIT...
    dw MOD_STAMP                ; ...and this one refuses a CTRL.DRV from
                                ; another BUILD of this commit - kern_small,
                                ; VIDEO=cga, FONT= - whose kernel puts its
                                ; data somewhere else
    dw modc_end                 ; MOD_H_IMG - vstart is 0, so the end label IS
                                ; the size
    dw CP_NENT                  ; MOD_H_NENT
    dw modc_e_paint             ; CPE_PAINT
    dw modc_e_onclick           ; CPE_ONCLICK
    dw modc_e_flush             ; CPE_FLUSH
    dw modc_e_tick              ; CPE_TICK
    dw modc_e_onup              ; CPE_ONUP
%ifdef KERN_BIG
    dw modc_e_ondrag            ; CPE_ONDRAG
%endif
    times MOD_NENT - CP_NENT dw 0   ; the table is a FIXED MOD_NENT words, so
                                ; MOD_HDR_SZ is a constant and mod_check can
                                ; refuse an entry offset that points into the
                                ; header itself

; --- the far entries, which are the WHOLE reason this is not a body pointer --
; A module's entries are reached by `call far [mod_fp + K]`, so they have to
; END in a `retf`; every body here ends in the near `ret` it has always had.
; So the table names a four-byte stub that near-calls the body and does the
; far return itself - which is `.cold`'s own cpf_*/dwf_* idiom (SPEC.md 2.6)
; moved inside the image, and is why a module author never writes `retf`.
;
; Pointing the table straight at the bodies is what the first build did. It
; does not fault: cp_tick_x's near `ret` popped the far call's OFFSET, left
; CS as the module and the caller's segment on the stack, and execution
; marched forward through whatever the image had after it - which reached the
; panel's own drawing code and painted a Control Panel, with no window, over
; the desktop, once a minute. Nothing errored and nothing hung.
modc_e_paint:   call cp_paint_x
            retf
modc_e_onclick: call cp_onclick_x
            retf
modc_e_flush:   call cp_flush_close_x
            retf
modc_e_tick:    call cp_tick_x
            retf
modc_e_onup:    call cp_onup_x
            retf
%ifdef KERN_BIG
modc_e_ondrag:  call cp_ondrag_x
            retf
%endif

section .cold                   ; ...and the two that stay resident

; -----------------------------------------------------------------------------
; cp_tick_due_x - is a live Date/Time page showing?
;
; RESIDENT (SPEC.md 2.8): ui_task calls this once a second whether or not a
; Control Panel exists, so in the module it would read CTRL.DRV off the floppy
; every second forever. It is also its own gate - the whole body is the test -
; so there is nothing here that wanted to be in the image.
; -----------------------------------------------------------------------------
cp_tick_due_x:
    push ax
    push bx
    push di
    cmp byte [cp_sel], CP_ITIME
    jne .no
    mov al, KIND_CTRL
    call KERNEL_SEG:cw_inst_find_kind         ; CF = 1: no live Control Panel
    jc .no
    mov bx, [di+I_WIN]
    test byte [bx+W_FLAGS], 2   ; visible?
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

; -----------------------------------------------------------------------------
; cp_onkey - W_ONKEY for the Control Panel (SPEC.md 31.9.2)
; in:  AL = ASCII, AH = scan code, SI = the window; the gfx lock is HELD
; out: nothing
;
; **THE PANEL HAD NO KEYBOARD AT ALL** until this: `cp_tpl`'s onkey word was 0,
; which is why the hard disk's geometry is typed with `-` and `+`. Everything
; on the five static pages is a radio, a check box or a button and has nothing
; to do with a key, so what this exists for is a DRIVER's page with a typed
; field in it - the RAM disk's size is the first - and it forwards to exactly
; that, or drops the key.
;
; **IT IS RESIDENT, and that is a size decision rather than a taste.** The
; obvious home is the on-demand module beside cp_item_click, and `MOD_NENT` is
; FOUR far-pointer slots per module with all four spent (paint, click, flush,
; tick); a fifth doubles the table to eight, for every module, in `.bss` that
; every machine carries. Forty resident bytes here is the cheaper of the two,
; and cp_drv_gone_x is the same trade already taken - it carries its own copy
; of cp_drv_item's record-space test for exactly this reason, and so does this.
;
; An unclaimed key is DROPPED and not beeped at: a Control Panel is not a text
; window, and every key that reaches it while a button page is showing would
; otherwise be an error the user did not make.
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
cp_onkey_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bl, al                  ; the KEY, banked: every test below answers in
    mov bh, ah                  ; AL and the page wants both halves back
    mov al, [cp_sel]
    cmp al, CP_ITEMS            ; RECORD space (SPEC.md 31.10.1), so the
    jb .out                     ; boundary is the table's LENGTH - a static
    sub al, CP_ITEMS            ; page has nothing to type into
    call drv_cp_class_x         ; AL = its class; CF = 1 = it went away.
                                ; NEAR: this body is .cold and so are all three
                                ; of these (driver.inc:1028, :1613, :2058), so
                                ; CS is already right and the drvf_* thunk's
                                ; own `retf` is one nobody wanted. It is the
                                ; same call cp_drv_gone_x makes seven lines
                                ; below. The thunks STAY - the .modc sites
                                ; (ctrl.inc:1573 and five more) still need them
    jc .out
    push ax
    call drv_cls_svc_x          ; DI = that class's copied table
    pop ax
    jc .out
    mov bp, [di+DSV_CPKEY]
    or bp, bp
    jz .out                     ; ...or a driver page that takes no keys
    call drv_cp_call_x          ; BX IS THE KEY AND AL IS STILL THE CLASS.
                                ; This dispatcher takes the class in AL
                                ; (SPEC.md 31.9) - which is exactly why the
                                ; key was banked into BX at the top and why
                                ; `pop ax` above puts the class back rather
                                ; than being tidiness. Handing the key over in
                                ; AX assembles, boots, and delivers the CLASS
                                ; to the page as a keystroke
.out:
    jmp kretc_bp
%endif


; -----------------------------------------------------------------------------
; cp_drv_gone_x - a driver's page is on screen no longer (SPEC.md 31.9)
;
; in:  nothing
; out: nothing; [cp_sel] is clamped off a page that has gone
; clobbers: flags
;
; RESIDENT, for a sharper reason than cp_tick_due_x's. drv_release calls this
; whenever a PUBLISHED driver detaches, and one of those callers is
; drv_shutdown on the way to a warm boot - which ui_cmd_reboot reaches AFTER
; cp_flush_close has already dropped this module. In the image it would read
; CTRL.DRV back off the floppy, during shutdown, to fix one byte nothing will
; read again.
;
; It carries its own copy of cp_drv_item's test rather than far-calling the
; module for it. Five instructions, and the alternative is a boundary crossing
; in the one direction this design does not otherwise have (kernel -> module
; is a thunk; module -> kernel is a cw_* shim; resident -> module for a
; five-instruction predicate is neither).
; -----------------------------------------------------------------------------
cp_drv_gone_x:
    push ax
    mov al, [cp_sel]
    cmp al, CP_ITEMS            ; RECORD space (SPEC.md 31.10.1): [cp_sel] names
    jb .out                     ; a record, so the boundary is the table's
    sub al, CP_ITEMS            ; LENGTH and not [cp_nst], the count on screen -
                                ; a static row cannot have gone anywhere
    call drv_cp_class_x         ; CF=1: that class publishes no page now
    jnc .out
    mov byte [cp_sel], 0        ; Scheduler: the boot selection, always there
.out:
    pop ax
    ret

section .modc                   ; ...and back to the image

; -----------------------------------------------------------------------------
; cp_paint_x - W_PAINT for the Control Panel window (far body of cp_paint_x)
;
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only; leaves [gfx_color] whatever the page left)
; The item list, the divider between the panes, then the selected item's
; page. DI/BP hold the content origin - the panel's own coordinate system,
; derived from the live window rect, never a screen constant - and every
; helper below takes it in the same pair.
; -----------------------------------------------------------------------------
cp_paint_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si                  ; wm_content takes the window in BX
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
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
; cp_onclick_x - W_ONCLICK for the Control Panel window (far body)
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
cp_onclick_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si                  ; wm_content takes the window in BX
    mov si, dx                  ; SI = click y; the window ptr is done with
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    mov di, ax                  ; DI = content left \ same origin pair as
    mov bp, dx                  ; BP = content top  / cp_paint_x's
    sub cx, di                  ; CX = content-relative click x
    mov dx, si
    sub dx, bp                  ; DX = content-relative click y

    cmp cx, CP_DIVX
    jge .page

    call cp_pick                ; --- left pane: which row? ---
    jc .done                    ; below the last row: nothing
    call cp_v2r                 ; ...and which RECORD that row names
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
%ifdef KERN_BIG
    mov al, [cp_sel]            ; SPEC.md 13.8.4: a DRIVER page is handed the
    call cp_drv_item            ; press verbatim and owns the whole gesture -
    jnc .drvpress               ; [cp_probe] is a KERNEL byte and a driver's
                                ; ladder has never heard of it, so a driver
                                ; told to identify would ACT
    mov al, 2                   ; SPEC.md 13.8.3: run the ladder to IDENTIFY,
    call cp_pgprobe             ; acting on nothing but a row that SELECTS -
    or al, al                   ; mode 2, and only this caller passes it
    jz .done
    xor ah, ah
    call COLD_SEG:os88ui_arm_f
    call cp_setdown             ; ...and the pressed look
    jmp short .done
.drvpress:
    mov al, [cp_sel]
    call cp_item_click          ; its own arm, its own down state, its own
%else                           ; release (DSV_CPUP)
    mov al, [cp_sel]
    call cp_item_click          ; kern_small acts on the press, which is what
%endif                          ; it did before and all it can afford
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
; cp_onup_x - W_ONMOUSEUP (SPEC.md 13.7/13.8.3): a panel control fires HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = window ptr;
;      gfx lock held
; out: nothing
;
; The release re-runs the same ladder the press ran and acts only if it lands
; on the same control - so a mis-aimed press is cancelled by sliding off, which
; is what SPEC.md 13.6 says a control with no safe prefix action wants.
;
; A point OUTSIDE the pane resolves to 0 and cancels for free: cp_pgprobe
; hands the ladder a pane-relative point and every ladder's first test is a
; band, so there is no separate "is this even in the pane" question to get
; wrong.
;
; **ON kern_small IT IS THE FORWARD AND NOTHING ELSE** (SPEC.md 13.8.4): the
; static pages there still act on the press, but a DRIVER's page acts on the
; release on both kernels, so without this edge no button on any driver page
; could ever fire - the press armed and drew it held and nothing came after.
; What kern_small does not get is the kernel's own arm, probe and pressed
; look - that half is the drag's company inside the guard below.
; -----------------------------------------------------------------------------
cp_onup_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call cp_pt                  ; DI/BP = pane origin, CX/DX = pane-relative
    mov ax, DSV_CPUP            ; SPEC.md 13.8.4: a driver page owns its own
    call cp_drv_ev              ; gesture, so the probe below must not run
%ifdef KERN_BIG
    jnc .done
    call COLD_SEG:os88ui_fire_f ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .done
    mov bx, ax                  ; BL = the armed control
    push bx
    xor al, al
    call cp_setdown             ; ...upright FIRST and unconditionally, before
    pop bx                      ; anything acts (SPEC.md 13.8.2)
    mov al, 1                   ; AL = what the RELEASE is over, and nothing
    call cp_pgprobe             ; on the page may act from inside it
    cmp al, bl
    jne .done                   ; a different control, or none: CANCELLED
    call cp_pgact               ; ...and only now does anything happen
%endif
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cp_ondrag_x - W_ONDRAG (SPEC.md 13.8.2): the pointer moved, press still down
; in:  CX = x, DX = y (SCREEN), SI = window ptr; gfx lock held
; out: nothing
;
; The same question cp_onup_x asks at the release, one pass early, so what is
; drawn pressed is exactly what would fire. cp_setdown draws only on a change,
; so a pointer moving INSIDE the control it pressed costs one compare and no
; pixels - which is what makes an edge that arrives per mouse packet
; affordable at all.
; -----------------------------------------------------------------------------
cp_ondrag_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call cp_pt
    mov ax, DSV_CPDRAG          ; SPEC.md 13.8.4, and cp_onup_x's reasoning
    call cp_drv_ev
    jnc .ddone
    call COLD_SEG:os88ui_armed_f
    or ax, ax
    jz .ddone
    mov bx, ax
    mov al, 1                   ; AL = what the pointer is over now, and the
    call cp_pgprobe             ; same: identify only
    cmp al, bl
    je .dset
    xor al, al                  ; off it: nothing is down
.dset:
    call cp_setdown             ; ...which draws only if that CHANGED
.ddone:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; cp_pt - a SCREEN point to the pane origin and a pane-relative point
; in:  CX = x, DX = y (screen), SI = window ptr
; out: DI = pane left, BP = pane top, CX/DX = pane-relative x/y; AX clobbered
;
; cp_onclick_x's own arithmetic, factored out because three edges now need it
; - which is where a second copy of it would otherwise have been written.
; -----------------------------------------------------------------------------
cp_pt:
    push si
    mov bx, si                  ; wm_content takes the window in BX
    mov si, dx                  ; SI = click y
    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
    mov di, ax
    mov bp, dx
    sub cx, di                  ; CX = content-relative x
    mov dx, si
    sub dx, bp                  ; DX = content-relative y
    pop si
    sub cx, CP_RX               ; ...and pane-relative, DI the pane left
    add di, CP_RX
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cp_ctlpaint - redraw the CONTROLS the down state just moved between
; in:  AL = the control now down, AH = the one that was (0 = none, either way)
;      DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; NOT cp_page, and that is the whole point of the routine existing. cp_page is
; the redraw path for a selection MOVE, so it erases the pane first - driving
; the down state through it would white a 200x140 pane and letter it again on
; every press and on every slide across a control while held, which is
; PERFORMANCE.md's double-draw flash introduced by the feature whose argument
; is that it does not flash.
;
; Each page's controls already have a painter of their own, because a click
; changes one control and os88ui_glyph is ~35-50ms of the field machine's time
; (cp_drv_box1's header does that arithmetic). This is the switch over them.
;
; **THE IDS ARE AN ARGUMENT BECAUSE THE DRIVERS PAGE DRAWS PER CONTROL**
; (SPEC.md 31.1.2). Every branch below used to redraw its page's whole control
; set on both edges of every press, which is the same double-draw the header
; above objects to, one level down: os88ui_glyph white-fills its own box
; before it draws a pixel, so a control that did not change still BLINKS. On
; the Drivers page that is four checkboxes at ~35-50ms each, twice, in front
; of a scroll whose own repaint is the thing the user was waiting for - and
; the two arrows are not even in cp_drv_boxes, so all of it was drawn for a
; control that is not one of them. The pages whose ladders draw a whole set in
; one loop still do; the Drivers page names the two controls that moved.
; -----------------------------------------------------------------------------
cp_ctlpaint:
    push ax
    push bx
    mov bx, ax                  ; BL = coming down, BH = going up - banked
                                ; because AL is about to be the page
    mov [cp_only], bh           ; --- the one coming UP, and nothing else
    or bh, bh                   ; (SPEC.md 31.1.3). Zero is "no control", not
    jz .down                    ; "all of them", so it draws nothing at all
    call cp_ctlgroup
.down:
    mov [cp_only], bl           ; --- then the one going DOWN
    or bl, bl
    jz .done
    call cp_ctlgroup
.done:
    mov byte [cp_only], 0       ; ...and back to "all", which is what every
    mov byte [cp_skip], 0       ; other caller of these painters wants
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_ctlgroup - the current page's control set, filtered by [cp_only]
; in:  [cp_only] = the one control wanted; DI/BP = pane origin; lock held
; out: nothing (all registers preserved)
;
; This was cp_ctlpaint's whole body, and the split is the fix: it walks the
; page's controls and each one asks cp_skipset whether it is the one, so the
; walk is cheap and only the control that moved is DRAWN. A page's set is
; three or four glyphs and a glyph is 24-34 ms of the field machine's time
; (PERFORMANCE.md Set 83), which is what made "redraw them all on both edges
; of a press" a visible blink of the whole group.
; -----------------------------------------------------------------------------
cp_ctlgroup:
    push ax
    mov al, [cp_sel]
    or al, al
    jz .sched
    cmp al, CP_ITIME
    je .time
    cmp al, CP_IDRV
    je .drv
    cmp al, CP_ISND
    je .snd
    cmp al, CP_IVID
    je .vid
%ifdef OS88_THEME
    cmp al, CP_ITHM
    je .thm
%endif
    jmp short .out              ; a DRIVER's page (SPEC.md 31.9) draws its own
                                ; controls and knows nothing about this
.sched:
    call cp_radios
    jmp short .out
.time:
    call cp_timechk             ; the two checks...
    xor al, al
    call cp_timebtn             ; ...and the two buttons
    mov al, 1
    call cp_timebtn
    jmp short .out
.snd:
    call cp_snd_radios          ; the three tiers...
    call cp_snd_test            ; ...and the Test button
    jmp short .out
.vid:
    call cp_vid_btn             ; Activate...
    call cp_vid_mode            ; ...and the three arrangement radios (which
                                ; refuses itself on a one-display machine)
    jmp short .out
%ifdef OS88_THEME
.thm:
    call cp_thm_radios          ; the whole group: a radio glyph is one
    call cp_thm_btn             ; os88ui_glyph_f and the page has five of them
    jmp short .out              ; ...and the button, which is the only control
                                ; here whose pressed look ever moves
%endif
.drv:
    mov al, [cp_only]           ; the Drivers page got here first (SPEC.md
    call cp_drv_ctl1            ; 31.1.2) and already draws one control by id;
.out:                           ; the driver above now hands it the same one
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_setdown - MOVE the pressed state to control AL (0 = none)
; in:  AL = a control id; DI/BP = pane origin; gfx lock held
; out: nothing (all registers preserved)
;
; The ONE writer of [cp_down], and it draws NOTHING when the state has not
; changed - which is what keeps the tracking edge from repainting the
; controls per mouse packet, a pointer moving INSIDE the control it pressed
; being the common case.
;
; It hands cp_ctlpaint BOTH ids - the one going up as well as the one coming
; down - because a page that draws per control needs to know which two moved,
; and only this routine still knows the outgoing one.
; -----------------------------------------------------------------------------
cp_setdown:
    push ax
    cmp al, [cp_down]
    je .out
    mov ah, [cp_down]           ; AH = the control coming UP...
    mov [cp_down], al           ; ...and AL the one going down
    call cp_ctlpaint
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_pgprobe - run the current page's ladder to IDENTIFY (SPEC.md 13.8.3)
; in:  AL = the mode wanted (1 = identify only, 2 = the press, where a row or
;      a field still selects), DI/BP = pane origin, CX/DX = pane-relative pt
; out: AL = the control id the ladder landed on (0 = none); AH clobbered
;
; The ladder is the hit test, and this is how it is asked rather than told.
; [cp_probe] is cleared on the way out on EVERY path, because a page left
; probing would take no clicks at all for the rest of the session - which is
; why it is here and not two stores at each of the three call sites.
; -----------------------------------------------------------------------------
cp_pgprobe:
    mov ah, al                  ; AH = the mode, banked past the pane gate
    mov byte [cp_ctlid], 0
    ; --- THE PANE GATE, which used to be the DISPATCH's ---------------------
    ; cp_onclick_x tested `cx >= CP_DIVX` before ever reaching a page, so no
    ; ladder tests its own x unless it has two columns - cp_sched_click tests
    ; the y and nothing else. That was sound while the only caller was a click
    ; INSIDE the window, and it stops being sound the moment a RELEASE can land
    ; anywhere on the screen (SPEC.md 13.7). Without this, releasing over the
    ; item list at a mode row's height changed the mode.
    ;
    ; Signed compares throughout: the gutter between the divider and the pane
    ; is legitimately NEGATIVE here (cp_onclick_x says so), and a release
    ; outside the window is negative by hundreds.
    cmp cx, CP_DIVX - CP_RX
    jl .none
    cmp cx, CP_CW - 1 - CP_RX
    jg .none
    cmp dx, 0
    jl .none
    cmp dx, CP_CH - 1
    jg .none
    mov [cp_probe], ah
    mov al, [cp_sel]
    call cp_item_click
    mov byte [cp_probe], 0
    mov al, [cp_ctlid]
    ret
.none:
    xor al, al
    ret

; -----------------------------------------------------------------------------
; cp_pgact - run it for real, on control AL of the current page
; in:  DI/BP = pane origin, CX/DX = the point that resolved to it
; out: nothing kept; the page may repaint or destroy things
; -----------------------------------------------------------------------------
cp_pgact:
    mov byte [cp_probe], 0      ; belt to cp_pgprobe's braces: this is the one
    mov al, [cp_sel]            ; path that must not be probing, and the store
    jmp cp_item_click           ; is two bytes

%endif

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

; =============================================================================
; Pages a DRIVER owns (SPEC.md 31.9)
;
; The item list is the five static rows plus one row per loaded driver that
; publishes DSV_CPNAME - so a page exists exactly while its driver is
; attached, which is not arranged anywhere: it falls out of the publication
; slot being cleared at detach.
;
; The pane origin cannot travel in BP for a driver page, because BP is the
; dispatcher's selector, so the ABI moves the pane top to DX:
;
;   paint   DI = pane left, DX = pane top
;   click   DI = pane left, DX = pane top, CX = rel x, BX = rel y
;
; Everything else is a package callback's rules: the gfx lock is HELD, ES is
; KERNEL_SEG, and the page must never take the lock or repaint another window.
; =============================================================================

; -----------------------------------------------------------------------------
; cp_v2r - a VISIBLE list ordinal -> the RECORD it names (SPEC.md 31.10.1)
;
; in:  AL = the ordinal, 0 = the top row on screen
; out: AL = the record index
; clobbers: AL (the output), flags
;
; THE ONE PLACE THE TWO SPACES MEET, and that is the whole design. cp_list
; draws ordinals and cp_pick returns one; [cp_sel], cp_entry, cp_item_name,
; cp_item_paint, cp_item_click and cp_drv_item all name records. Keeping a
; second opinion about which is which - which is what indexing cp_items by a
; drawn position was - is what made a hidden row have to be LAST.
;
; Statics come first and may be hidden, so those are WALKED against
; [cp_hide]'s bits; driver pages follow and never are, so past them the answer
; is arithmetic. [cp_nst] is the count of statics actually shown, which is
; what makes that second half one subtraction and one addition.
;
; IT CANNOT FAIL AND ANSWERS NO CF. Both callers bound the ordinal with
; cp_nitems before they ask, so there is always such a row; the CP_ITEMS test
; in the walk is the terminator that keeps a caller's arithmetic mistake
; inside the table rather than a contract anybody reads.
; -----------------------------------------------------------------------------
cp_v2r:
    push bx
    push cx
    cmp al, [cp_nst]
    jae .drv                    ; past every shown static: a driver's page
    mov ch, al                  ; CH = the ordinal wanted
    xor bx, bx                  ; BL = the record, BH = ordinals counted
.scan:
    cmp bl, CP_ITEMS
    jae .hit                    ; the terminator (see above)
    mov cl, bl
    mov al, 1
    shl al, cl                  ; CP_ITEMS < 8, so the bit is in one byte
    test [cp_hide], al
    jnz .next                   ; hidden: it takes no ordinal
    cmp bh, ch
    je .hit
    inc bh
.next:
    inc bl
    jmp short .scan
.hit:
    mov al, bl
    pop cx
    pop bx
    ret
.drv:
    sub al, [cp_nst]            ; the driver's own ordinal...
    add al, CP_ITEMS            ; ...is a record past the end of the table
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cp_nitems - how many rows the list has right now
; in:  nothing
; out: AL = the count
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
cp_nitems:
    call COLD_SEG:drv_cp_count_x
    add al, [cp_nst]
    ret

; -----------------------------------------------------------------------------
; cp_drv_item - is this row a driver's page?
; in:  AL = row index
; out: CF = 0 and AL = the owning class; CF = 1 = a static row, AL untouched
; clobbers: AL (on a hit), flags
; -----------------------------------------------------------------------------
cp_drv_item:
    cmp al, CP_ITEMS            ; the static/driver boundary is a RECORD-space
    jb .static                  ; question, so it is the table's length and
    sub al, CP_ITEMS            ; not the count of rows on screen
    call COLD_SEG:drvf_drv_cp_class   ; CF=1 if the driver went away under us
    jc .gone
    ret
.gone:
    add al, CP_ITEMS            ; hand back the ROW index, not the rebased
    stc                         ; one (add clears CF): a CF=1 answer carrying
    ret                         ; the rebased index read as "static row 5 ->
                                ; dispatch the Scheduler page", and the
                                ; consumers below could not tell
.static:
    stc
    ret

; -----------------------------------------------------------------------------
; cp_item_name - one row's list name
; in:  AL = row index
; out: SI -> a NUL string in the KERNEL segment, always
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
cp_item_name:
    push ax
    call cp_drv_item
    jc .static
    call COLD_SEG:drvf_drv_cp_name            ; the staged copy, so font_str can read it
    pop ax
    ret
.static:
    cmp al, CP_ITEMS            ; a vanished driver's row has no name; the
    jae .gone                   ; empty string letters as a blank row
    call cp_entry
    mov si, [si + CP_I_NAME]
    pop ax
    ret
.gone:
    mov si, cp_s_gone
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_item_paint / cp_item_click - dispatch one row's page
; in:  AL = row index, DI = pane left, BP = pane top (+ CX/DX = the click,
;      content-relative, for the click half)
; out: nothing
; clobbers: whatever the page clobbers
; -----------------------------------------------------------------------------
cp_item_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call cp_drv_item
    jc .static
    mov bx, di                  ; BX = the pane left, banked: drv_cls_svc
    mov dx, bp                  ; answers in DI, and DX is the driver ABI's
    call COLD_SEG:drvf_drv_cls_svc            ; pane top
    jc .done                    ; (AL survives: it is drv_cls_svc's input)
    mov bp, [di+DSV_CPPAINT]
    mov di, bx
    call COLD_SEG:drvf_drv_cp_call
    jmp short .done
.static:
    cmp al, CP_ITEMS            ; a vanished driver's row paints nothing -
    jae .done                   ; unreachable while cp_drv_gone_x clamps
    call cp_entry               ; [cp_sel] at detach, and checked anyway
    call [si + CP_I_PAINT]
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cp_item_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call cp_drv_item
    jc .static
    mov bx, dx                  ; BX = the pane-relative y...
    mov dx, bp                  ; ...DX the pane top, CX already the x
    mov si, di
    call COLD_SEG:drvf_drv_cls_svc
    jc .done
    mov bp, [di+DSV_CPCLICK]
    mov di, si
    call COLD_SEG:drvf_drv_cp_call
    jmp short .done
.static:
    cmp al, CP_ITEMS            ; a vanished driver's row refuses the click
    jae .done                   ; (same reasoning as cp_item_paint's guard)
    call cp_entry
    call [si + CP_I_CLICK]
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
; cp_drv_ev - forward a RELEASE or a DRAG to a driver's page (SPEC.md 13.8.4)
; in:  AX = DSV_CPUP or DSV_CPDRAG - the WHOLE word, because it is banked in
;      SI and cp_pt leaves rubbish in AH - DI = pane left, BP = pane top,
;      CX/DX = pane-relative x/y
; out: CF = 1 if this page is not a driver's, in which case nothing was called
;      and the caller owes its own handling; CF = 0 = forwarded (or the driver
;      published no cell, which is the same thing to the caller)
;
; A DRIVER PAGE OWNS ITS WHOLE GESTURE and the kernel's probe machinery does
; not reach into it: [cp_probe] is a kernel byte, a driver's ladder has never
; heard of it, and a driver told to "identify" would ACT. That is not a
; hypothetical - it is what SPEC.md 13.8.3's conversion did to these two pages
; for a release, and the reason a hard disk could still be mounted from the
; press while every static page had stopped.
;
; So the two edges are handed over verbatim instead, and the driver does what
; a package window does: os88ui_arm on the press, os88ui_fire on the release,
; os88ui_armed on the drag, and its own control painter for the pressed look.
; cp_item_click's argument shuffle exactly, because the ABI is the same one.
; -----------------------------------------------------------------------------
cp_drv_ev:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, ax                  ; SI = the DSV_* cell wanted, banked across
    mov al, [cp_sel]            ; cp_drv_item's own use of AL
    call cp_drv_item
    jc .no                      ; a static page, or a driver that went away
    mov bx, dx                  ; BX = the pane-relative y...
    mov dx, bp                  ; ...DX the pane top, CX already the x
    push si
    mov si, di                  ; SI = pane left, banked past drv_cls_svc
    call COLD_SEG:drvf_drv_cls_svc
    pop bp                      ; BP = the cell (pushed as SI above)
    jc .yes                     ; the class went: nothing to call, and the
                                ; caller must still not run the static path
    add bp, di
    mov bp, [ds:bp]             ; BP = the published proc, 0 = not wanted.
                                ; THE OVERRIDE IS LOAD-BEARING (SPEC.md 1):
                                ; SS is LOW_SEG here and [bp] defaults to it,
                                ; so without it this reads a task stack and
                                ; answers 0 - which looks exactly like a
                                ; driver that did not publish the cell
    mov di, si
    or bp, bp
    jz .yes
    call COLD_SEG:drvf_drv_cp_call
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; (cp_drv_gone_x was here. It is RESIDENT now, above the .modc switch, because
; drv_shutdown calls it after cp_flush_close has dropped this image - SPEC.md
; 2.8. It kept its behaviour and lost its cp_drv_item call, which would have
; been a near call across the boundary.)

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cp_ctl - a page ladder reports which control it landed on (SPEC.md 13.8.3)
; in:  AL = this site's control id, unique within its page - or 0 for a site
;      that is not an armable control and SELECTS instead
; out: CF = 1 while PROBING, and the caller must return without acting;
;      CF = 0 to act. Every register preserved.
;
; Three instructions and about seven bytes at each of the twelve action sites,
; which is the whole cost of turning five act-on-the-press ladders into five
; identify-or-act ones. The id is per PAGE and needs no page number beside it:
; changing the page needs a click in the item list and the button is already
; down, so [cp_sel] cannot move between the press and the release.
;
; **AN ID OF 0 IS THE THIRD SPECIES**, hd_ctl's exactly (SPEC.md 13.8.4): a
; selecting site acts on the PRESS - a safe prefix action keeps it
; (SPEC.md 13.6) - and reports nothing at all, so the drag and the release
; find it and return. Two of those sites shipped with no cp_ctl call whatever,
; which meant they acted on every pass instead of on none.
; -----------------------------------------------------------------------------
cp_ctl:
    cmp byte [cp_probe], 0
    je .act
    or al, al
    jz .sel                     ; not an armable control: nothing to report
    mov [cp_ctlid], al
    stc
    ret
.sel:
    cmp byte [cp_probe], 2      ; ...and it still selects on the press
    je .act
    stc
    ret
.act:
    clc
    ret

; -----------------------------------------------------------------------------
; cp_isdown - is control AL the one drawn pressed? (SPEC.md 13.8/13.8.3)
; in:  AL = a control id
; out: ZF = 1 if it is; every register preserved
;
; The painters' half: every glyph and button site asks, so what is on the
; glass follows [cp_down] on a full page paint as well as on an edge.
;
; The id space is PER PAGE, so this needs no page number beside it - the same
; reason cp_ctl does not.
; -----------------------------------------------------------------------------
cp_isdown:
    cmp al, [cp_down]
    ret

; -----------------------------------------------------------------------------
; cp_dnbtn / cp_dnglyph - OR the pressed flag in if control AL is the one down
;   cp_dnbtn   in: AL = the id, DI = os88ui_btn's flag word    -> DI
;   cp_dngly   in: BH = the id, AX = os88ui_glyph's argument   -> AL
; out: as named; every other register preserved
;
; Two entries because the two controls take their flags in different registers
; - DI for the button, AL beside the index for the glyph (SPEC.md 20.5.1's own
; asymmetry, which exists because every page is already holding the pane's
; left edge in DI). And the GLYPH's id arrives in BH rather than AL for the
; reason its own header gives: AL is the argument, so it cannot also be the
; question. BH is free at all six glyph sites; AL is free at all three button
; ones.
; -----------------------------------------------------------------------------
cp_dnbtn:
    call cp_isdown
    jne .up
    or di, OS88UI_DOWN
.up:
    jmp cp_skipset              ; ...and AL is still the id (SPEC.md 31.1.3)

cp_dngly:
    push ax
    mov al, bh
    call cp_isdown
    pop ax
    jne .up
    or al, OS88UI_GDOWN
.up:
    push ax
    mov al, bh
    call cp_skipset
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_skipset - arm the next control emit's skip, if [cp_only] names another
; in:  AL = this control's id
; out: [cp_skip]; all registers preserved (flags aside)
;
; Called from cp_dnbtn and cp_dngly because between them they run immediately
; before every control this window draws, and they are the only two that know
; which control it is - the id reaches them for the pressed look and is spent
; twice rather than threaded to a second place that could disagree.
; -----------------------------------------------------------------------------
cp_skipset:
    push ax
    mov ah, [cp_only]
    or ah, ah
    jz .want                    ; 0 = every control, which is what a full page
    cmp ah, al                  ; paint and every setting change leave it at
    je .want
    mov byte [cp_skip], 1
    pop ax
    ret
.want:
    mov byte [cp_skip], 0
    pop ax
    ret
%endif

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
    push bx
    mov bl, al
    call cp_nitems              ; the static rows PLUS whatever a loaded
    cmp bl, al                  ; driver publishes (SPEC.md 31.9)
    mov al, bl
    pop bx
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
    call KERNEL_SEG:cw_gfx_fill

    mov dx, bp
    add dx, CP_I0Y              ; DX = row 0 top, screen y
    call cp_nitems
    mov bp, ax
    and bp, 0x00FF              ; BP = how many rows there are
    xor bx, bx                  ; BX = the ORDINAL being drawn
.row:
    mov al, bl
    call cp_v2r                 ; ...and BH = the record it names, resolved
    mov bh, al                  ; ONCE for both the bar and the name
    cmp al, [cp_sel]
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
    call KERNEL_SEG:cw_gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    mov byte [gfx_color], CWHITE
    mov byte [cp_ipap], CBLACK  ; ...on the bar this row just laid
    jmp .text
.plain:
    mov byte [gfx_color], CBLACK
    mov byte [cp_ipap], CWHITE  ; ...on the pane
.text:
    mov al, bh
    call cp_item_name           ; SI -> it, static or staged (SPEC.md 31.9)
    mov cx, di
    add cx, CP_IX
    push dx
    add dx, CP_ITDY
    push ax                     ; OPAQUE, and the one label on this window
    mov al, [gfx_color]         ; whose paper is not the pane (SPEC.md 31.11)
    mov ah, [cp_ipap]
    call KERNEL_SEG:cw_font_run
    pop ax
    pop dx

    add dx, CP_IROWH
    mov bh, 0                   ; BX is the ordinal again: the resolved record
    inc bx                      ; lived in BH for the length of one row
    cmp bx, bp
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
    call KERNEL_SEG:cw_gfx_vline              ; in AX = x, BX = y1, DX = y2
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_promise - the panel's content stands still unless the CLOCK is on it
; in:  nothing; the gfx lock HELD by the caller (cp_page's contract)
; out: nothing (every register and the flags preserved)
;
; SPEC.md 31.12, which is SPEC.md 11.96.1's promise answered per PAGE.
;
; The panel has NO WORKER, and on every page but one nothing in it changes
; without a click - which cannot reach a window that is not frontmost, and a
; frontmost window never holds a cache (11.96.4 drops it). The exception is
; Date & Time: ui_task calls cp_tick once a second, and cp_tick_x SKIPS
; DRAWING when wm_obscured says so. That is 11.96.1's disqualifier exactly -
; the clock advances and the glass does not - so a cache taken before it
; shows the wrong time on the raise.
;
; So the promise is [cp_sel] != CP_ITIME. No depth claim goes with it: the
; panel's content is 22,190 bytes at four planes and already fits under
; wm_su_kb's ceiling, and it draws in CDGRAY (SPEC.md 47), so a two-colour
; claim would be a lie for nothing.
; -----------------------------------------------------------------------------
cp_promise:
    pushf
    push ax
    push bx
    push di
    mov al, KIND_CTRL
    call KERNEL_SEG:cw_inst_find_kind   ; CF = 1: no live panel, nothing to
    jc .out                             ; promise about - cp_page runs from
                                        ; the paint, and the record is what
                                        ; carries the flag
    mov bx, [di+I_WIN]
    mov al, 1
    cmp byte [cp_sel], CP_ITIME
    jne .say
    xor al, al                          ; the clock is on the pane: withdraw,
.say:                                   ; and wm_saveu drops the claim with it
    call KERNEL_SEG:cw_wm_saveu
.out:
    pop di
    pop bx
    pop ax
    popf
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
    call cp_promise             ; ...and WHICH page it is decides whether this
                                ; window's content stands still (SPEC.md
                                ; 31.12). Here because every path that
                                ; changes the answer - the click, the paint,
                                ; and cp_open's first draw - goes through this
                                ; one routine
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
    call KERNEL_SEG:cw_gfx_fill

    mov al, [cp_sel]
    add di, CP_RX               ; DI = pane left, BP = pane top
    call cp_item_paint

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
    call cp_run

    CPSTAGE cp_s_pre            ; row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PLDY
    call cp_run

    CPSTAGE cp_s_coop           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    call cp_run

    CPSTAGE cp_s_cap            ; caption: the flip is immediate
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    call cp_run

    call cp_radios              ; both glyphs, filled per the live mode

    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cp_run - one Control Panel label, OPAQUE (SPEC.md 31.11)
; in:  SI -> ASCIIZ, CX = x, DX = y, [gfx_color] = the ink; gfx lock held
; out: nothing (all registers preserved)
;
; Every page here letters onto the pane `cp_page` has just white-filled, so
; the background is a constant and the ink is whatever pen the caller set -
; including CDGRAY with [gfx_dis], which SPEC.md 6.1.12 now folds into the
; run's own mask instead of sending it back to the pair.
;
; Reading the pen rather than taking it in AL is what made this a mechanical
; change: twenty-six call sites already set [gfx_color] and then called
; cw_font_str, and every one of them became this without touching the line
; above it. It is also SHORTER - a near call in this segment against a far
; call to the shim - so the conversion took .text off rather than adding it.
;
; DEFINED OUTSIDE EVERY %ifdef, and it landed inside OS88_THEME's first: the
; theme is on in the default build and off in kern_small, so `make` was clean
; and `make small` could not find the symbol at twenty-six sites. That is
; SPEC.md 62.9.15's whole point, and test-full's buildmatrix row is what said
; so before a release did.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; cp_gemit / cp_bemit - draw one control, unless [cp_only] named another
; in:  as os88ui_glyph_f / os88ui_btn_f, with cp_dngly / cp_dnbtn already run
; out: nothing (all registers preserved)
;
; Every control on every page goes through cp_dngly or cp_dnbtn to pick up its
; pressed look, and those are the only two routines that know a control's id -
; so they are where the filter belongs, and these two are where it is spent.
; SPEC.md 31.1.3 is the reason there is a filter at all: a press edge used to
; redraw the page's WHOLE control set, and a 12x12 glyph is 82.8 ms of the
; field machine's time for the Sound page's three.
;
; The skip is consumed rather than tested, so it cannot leak: cp_vid_rows draws
; a glyph with no down state and no cp_dngly before it, and would otherwise
; inherit whatever the last control armed.
; -----------------------------------------------------------------------------
cp_gemit:
%ifdef KERN_BIG
    cmp byte [cp_skip], 0
    mov byte [cp_skip], 0       ; ...one-shot (a `mov` writes no flags)
    jne .out
%endif
    call COLD_SEG:os88ui_glyph_f
%ifdef KERN_BIG
.out:
%endif
    ret

cp_bemit:
%ifdef KERN_BIG
    cmp byte [cp_skip], 0
    mov byte [cp_skip], 0
    jne .out
%endif
    call COLD_SEG:os88ui_btn_f
%ifdef KERN_BIG
.out:
%endif
    ret

cp_run:
    push ax
    mov al, [gfx_color]
    mov ah, CWHITE              ; the pane, and it is NOT themed (see
    call KERNEL_SEG:cw_font_run ; cp_thm_paint): content, in the ordinary pens
    pop ax
    ret

%ifdef OS88_THEME
; -----------------------------------------------------------------------------
; cp_thm_paint - the Theme page (SPEC.md 76.4)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; TWO groups: the theme, and SPEC.md 11.99's zoom. They share a page because
; they are the same question asked twice - what the machine spends on looking
; like itself - and the zoom is the one that costs TIME rather than pixels:
; ~137 ms of held gfx lock per window appearing, leaving or minimizing.
; -----------------------------------------------------------------------------
cp_thm_paint:
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK ; entry pen is CWHITE (cp_page's erase) - the
                                ; PANE is content and is not themed, so this
                                ; page is drawn in the ordinary pens like
                                ; every other one

    mov si, cp_s_thm            ; heading, same string as the list row
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call cp_run

    CPSTAGE cp_s_tbrt           ; theme row 0
    mov dx, bp
    add dx, CPH_R0Y + CP_PLDY
    call cp_thm_lbl
    CPSTAGE cp_s_tdrk           ; theme row 1
    mov dx, bp
    add dx, CPH_R1Y + CP_PLDY
    call cp_thm_lbl
%ifdef KERN_BIG
    CPSTAGE cp_s_tclr           ; theme row 2 - drawn on every adapter, and
    mov dx, bp                  ; GREYED where the machine has no colour
    add dx, CPH_R2Y + CP_PLDY   ; (SPEC.md 47 rule 3, 76.12.1)
    call cp_thm_colgrey
    call KERNEL_SEG:cw_gfx_pen_cf   ; the pen AND [gfx_dis] together, which on
    call cp_thm_lbl                 ; a 1bpp adapter is the whole difference
    call KERNEL_SEG:cw_gfx_pen_live ; (SPEC.md 47 rule 1)
%endif

%ifdef WM_ANIM
    CPSTAGE cp_s_tanim          ; --- the second group's heading
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPH_H2Y
    call cp_run

    CPSTAGE cp_s_ton            ; anim row 0
    mov dx, bp
    add dx, CPH_A0Y + CP_PLDY
    call cp_thm_lbl
    CPSTAGE cp_s_toff           ; anim row 1
    mov dx, bp
    add dx, CPH_A1Y + CP_PLDY
    call cp_thm_lbl
%endif


    call cp_thm_radios
%ifdef KERN_BIG
    call cp_thm_btn             ; ...and the Screen Saver button (SPEC.md 79.7)
%endif

    pop si
    pop dx
    pop cx
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cp_thm_btn - the Screen Saver button (SPEC.md 79.7)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; OS88UI_FILL, because a RELEASE redraws this button over a PRESSED one and
; nothing else erases it; on a full page paint cp_page has already whited the
; pane, so the fill is over white and costs one gfx_fill either way.
;
; IT DOES NOT GREY (SPEC.md 47 rule 3). Whether SAVER.DRV can be read is not a
; fact anybody holds - it is a mount, a directory search and a heap claim, and
; the only honest test is doing the thing. The press does it and says so with
; the toast the panel's own load refusal already uses.
; -----------------------------------------------------------------------------
cp_thm_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, di
    add ax, CPH_BTX
    mov [os88ui_krect+0], ax    ; THE kernel's one button rect (SPEC.md
    add ax, CPH_BTW - 1         ; 20.5.1): every caller of it is the UI task
    mov [os88ui_krect+4], ax    ; under the gfx lock, so one is enough and a
    mov ax, bp                  ; second is .bss nobody needs
    add ax, CPH_BTY
    mov [os88ui_krect+2], ax
    add ax, CPH_BTH - 1
    mov [os88ui_krect+6], ax
    mov bx, os88ui_krect
    CPSTAGE cp_s_tsave
    mov di, OS88UI_FILL
    mov al, CPH_CSAVE
    call cp_dnbtn               ; ...pressed if it is the one down (13.8.3)
    call cp_bemit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; cp_thm_colpen - the pen for the Color row: greyed unless this machine has
;                 an adapter that can show it (SPEC.md 76.12.1)
; out: CF = 1 when greyed - which is os88ui_glyph's argument too, so ONE
;      predicate drives the label, the glyph and cp_thm_click's refusal
; cp_thm_colgrey - MAY this machine show the Color theme? (internal)
; out: CF = 1 -> the row is greyed and the click refuses; every register kept
;
; SPEC.md 47 rule 1: ONE predicate drives the greying, the glyph and the
; refusal, so the three cannot disagree - and it is a FACT (rule 3), not a
; guess: SPEC.md 39.4 reduces every index outside {0,15} to black, white or
; the 50% dither, so on a 1bpp adapter Color would BE Bright. The test is the
; PRIMARY'S DEPTH - VID_VGA and VID_EGA are the two 4-plane kinds - and it
; must match thm_set's own refusal (SPEC.md 76.12.1). It was `cmp [vid_kind],
; VID_VGA` while VGA was the only colour adapter. It is NOT [vid_mono]: this
; is reached from the page's own paint and click, inside a lock hold in which
; the panel has just drawn on ITS display, and on a two-card desktop that byte
; names the display last drawn on (SPEC.md 39.14.3) - so spelt that way a
; panel dragged onto the Hercules greyed Color, and cancelled the click, on a
; machine whose primary is a VGA. [vid_kind] is the primary's and vid_ctx_act
; never moves it.
%ifdef KERN_BIG
cp_thm_colgrey:
    cmp byte [vid_kind], VID_VGA
    je .live
    cmp byte [vid_kind], VID_EGA
    je .live
    stc
    ret
.live:
    clc
    ret
%endif

; cp_thm_lbl - one row label at the group's x (internal)
; in:  SI -> string, DX = text y, DI = pane left; out: nothing, CX clobbered
cp_thm_lbl:
    mov cx, di
    add cx, CP_PLX
    jmp cp_run                  ; a tail call is safe HERE and was not before:
                                ; cp_run is a near proc in this segment, where
                                ; the cw_ shim this used to call ends in
                                ; `retf` and jumping to one pops this
                                ; routine's near return address as CS:IP.
                                ; desk_zone_xor's comment is the same finding

; -----------------------------------------------------------------------------
; cp_thm_click - a click inside the Theme page
; in:  DI = pane left, BP = pane top, CX = pane-relative x (ignored),
;      DX = pane-relative y; gfx lock held by the caller
; out: nothing (flags only)
;
; cp_sched_click's shape twice over. The x coordinate is ignored, as it is
; there: the bands span the pane, so the glyph and its label both select the
; row. Comparisons are SIGNED - see cp_onclick_x. THE GAP BETWEEN THE GROUPS
; IS DEAD: the Scheduler page can let its two bands touch because a click has
; only one group it could mean, and here a generous band would put "half way
; between Dark and On" into one of them.
; -----------------------------------------------------------------------------
cp_thm_click:
    push ax

%ifdef KERN_BIG
    cmp dx, CPH_BTY             ; --- the Screen Saver button, tested FIRST
    jl .groups                  ; because it is the lowest band on the page
    cmp dx, CPH_BTY + CPH_BTH - 1
    jg .donej
    cmp cx, CPH_BTX
    jl .donej
    cmp cx, CPH_BTX + CPH_BTW - 1
    jg .donej
    mov al, CPH_CSAVE
    call cp_ctl                 ; ...and it FIRES ON THE RELEASE (SPEC.md
    jc .donej                   ; 13.8.3): opening a window is not a safe
                                ; prefix action, so a mis-aimed press has to
                                ; be cancellable by sliding off
    call COLD_SEG:ssf_cfg       ; the overlay, and then its window (79.7)
    jnc .donej
    push bx                     ; ...and the ONE refusal on this page that the
    push cx                     ; user is standing in front of, so it is said
    mov al, 1                   ; out loud rather than swallowed the way every
    mov bx, cp_opentab          ; other one in SPEC.md 79 is (SPEC.md 2.8.4,
    mov cx, CP_NOPEN            ; 47 rule 3)
    call KERNEL_SEG:toast_say
    pop cx
    pop bx
.donej:                         ; ...and the five refusals above
    jmp .done
.groups:
%endif
    cmp dx, CPH_B0Y1
    jl .done
%ifdef KERN_BIG
    cmp dx, CPH_B2Y2
%else
    cmp dx, CPH_B1Y2            ; no Color row on this build (SPEC.md 76.12.3)
%endif
    jg .anim                    ; below the theme group: try the other one

    mov al, THM_DARK
    cmp dx, CPH_B0Y2
    jle .tbright
%ifdef KERN_BIG
    cmp dx, CPH_B1Y2
    jle .tset
    mov al, THM_COLOR           ; ...and the third refuses where it is greyed,
    call cp_thm_colgrey         ; through the predicate that greyed it
    jc .done
%endif
    jmp short .tset
.tbright:
    mov al, THM_BRIGHT
.tset:
    mov ah, al
%ifdef KERN_BIG
    inc al                      ; ...and it IS the control: id 1, 2 or 3
    call cp_ctl                 ; (SPEC.md 13.8.3)
    jc .done
    mov al, ah
%endif
    cmp al, [thm_kind]
    je .done                    ; already live: change nothing
    call KERNEL_SEG:cw_thm_set
    mov byte [cp_wdirty], 1     ; remembered on close (SPEC.md 31.8)
    mov byte [cp_dirty], 1      ; ui_task repaints the world (SPEC.md 13)
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
    call cp_thm_radios
    jmp short .done

.anim:
%ifndef WM_ANIM
    jmp short .done             ; no zoom in this build: the rows are drawn
%else                           ; greyed and refuse, which is SPEC.md 47's
    cmp dx, CPH_A0Y1            ; predicate shared by the greying and the
    jl .done                    ; refusal rather than two opinions
    cmp dx, CPH_A1Y2
    jg .done

    xor al, al                  ; --- lower band = Off
    cmp dx, CPH_A0Y2
    jg .aset
    inc al                      ; upper band = On
.aset:
    mov ah, al
%ifdef KERN_BIG
    add al, 4                   ; ...id 4 or 5 (SPEC.md 13.8.3)
    call cp_ctl
    jc .done
    mov al, ah
%endif
    cmp al, [wm_animon]
    je .done                    ; already live
    mov [wm_animon], al
    mov byte [cp_wdirty], 1     ; remembered on close - and NOTHING else is
    call cp_thm_radios          ; owed: the zoom is not on screen, so there is
%endif                          ; nothing to repaint and no [cp_dirty]
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_thm_radios - all four glyphs, each group's live row filled
; -----------------------------------------------------------------------------
cp_thm_radios:
    push ax
    push bx
    push cx
    push dx
    push si

    mov cx, di
    add cx, CP_PGX              ; CX = glyph left x (every row)

    mov bl, [thm_kind]          ; --- the theme group
    mov dx, bp
    add dx, CPH_R0Y
    mov ax, OS88UI_GRADIO       ; row 0: Bright
    or bl, bl
    jnz .r0
    mov al, OS88UI_GRADIO | OS88UI_GON
.r0:
%ifdef KERN_BIG
    mov bh, 1
    call cp_dngly
%endif
    call cp_gemit
    mov dx, bp
    add dx, CPH_R1Y
    mov ax, OS88UI_GRADIO       ; row 1: Dark
    cmp bl, THM_DARK
    jne .r1
    mov al, OS88UI_GRADIO | OS88UI_GON
.r1:
%ifdef KERN_BIG
    mov bh, 2
    call cp_dngly
%endif
    call cp_gemit
%ifdef KERN_BIG
    mov dx, bp
    add dx, CPH_R2Y
    mov ax, OS88UI_GRADIO       ; row 2: Color, greyed off a colour adapter
    cmp bl, THM_COLOR
    jne .r2
    mov al, OS88UI_GRADIO | OS88UI_GON
.r2:
    call cp_thm_colgrey
    jnc .r2live
    mov ah, OS88UI_DIS
.r2live:
    mov bh, 3
    call cp_dngly
    call cp_gemit
%endif

%ifdef WM_ANIM
    mov bl, [wm_animon]         ; --- the animation group
    mov dx, bp
    add dx, CPH_A0Y
    mov ax, OS88UI_GRADIO       ; row 0: On
    or bl, bl
    jz .a0
    mov al, OS88UI_GRADIO | OS88UI_GON
.a0:
    mov bh, 5                   ; ...id 5, which is what cp_thm_click's UPPER
    call cp_dngly               ; band reports for this row (SPEC.md 13.8.3):
    call cp_gemit               ; its `inc al` then `add al, 4` makes On 5 and
    mov dx, bp                  ; Off 4, so the painter must agree or a press
    add dx, CPH_A1Y             ; on one row draws the OTHER one pressed
    mov ax, OS88UI_GRADIO       ; row 1: Off
    or bl, bl
    jnz .a1
    mov al, OS88UI_GRADIO | OS88UI_GON
.a1:
    mov bh, 4
    call cp_dngly
    call cp_gemit
%endif

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif

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
; the pane (SPEC.md 31.2). Comparisons are SIGNED - see cp_onclick_x. A hit on
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
%ifdef KERN_BIG
    inc al                      ; ...and it IS the control: id 1 or 2
    call cp_ctl                 ; (SPEC.md 13.8.3)
    jc .done
    mov al, ah
%endif
    call KERNEL_SEG:sched_mode_get         ; AL = the live mode
    cmp al, ah
    je .done                    ; that row is already live: change nothing
    mov al, ah
    call KERNEL_SEG:sched_mode_set            ; live on the next tick (SPEC.md 8.2)
    mov byte [cp_wdirty], 1     ; ...and remembered on close (31.8)
    mov byte [cp_dirty], 1      ; ui_task step 3 repaints the About box's
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
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
; mode change: os88ui_glyph white-fills its own 12x12 box first, which is what
; lets cp_sched_click repaint in place without touching the rest of the page.
; -----------------------------------------------------------------------------
cp_radios:
    push ax
    push bx
    push cx
    push dx
    push si

    call KERNEL_SEG:sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
    mov bl, al                  ; BL = the row whose glyph is filled

    mov cx, di                  ; the pen is os88ui_glyph's now, taken from
    add cx, CP_PGX              ; AH so the colour and [gfx_dis] cannot be set
    mov dx, bp                  ; apart (SPEC.md 47 rule 1) - it used to be
    add dx, CP_PR0Y             ; set here because the click path's entry pen
                                ; is whatever the last drawing left behind,
                                ; and now that is nobody's business but the
                                ; control's. CX = glyph left x (both rows),
                                ; DX = row 0 glyph top

    mov ax, OS88UI_GRADIO       ; --- row 0: Pre-emptive --- (AH = 0: neither
    cmp bl, 0                   ; mode is ever refused)
    jne .r0
    mov al, OS88UI_GRADIO | OS88UI_GON
.r0:
%ifdef KERN_BIG
    mov bh, 1                   ; ...and PRESSED if it is the one being held
    call cp_dngly               ; (SPEC.md 13.8.3): row 0 is id 1
%endif
    call cp_gemit
    add dx, CP_PROWH            ; --- row 1: Cooperative ---
    mov ax, OS88UI_GRADIO
    cmp bl, 1
    jne .r1
    mov al, OS88UI_GRADIO | OS88UI_GON
.r1:
%ifdef KERN_BIG
    mov bh, 2
    call cp_dngly
%endif
    call cp_gemit
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Display page (SPEC.md 31.10/39.11) - which adapter, and where display 1 sits
; =============================================================================

; -----------------------------------------------------------------------------
; cp_vid_rowok - does this machine have adapter AL? (module internal)
; in:  AL = a VID_* kind
; out: CF = 0 yes, CF = 1 no
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
cp_vid_rowok:
    call KERNEL_SEG:cw_vid_avail_test
    ret

; -----------------------------------------------------------------------------
; cp_vid_pick - the pending adapter, resolved (module internal)
; in:  nothing
; out: AL = [cp_vsel], normalised to something this machine can actually do
; clobbers: AL, flags
;
; 0FFh means "whatever is running", which is the boot value; so does any kind
; the machine turns out not to have, which is what a settings byte written on
; a machine with a card this one does not have decays to. Both are repaired
; here rather than at the twelve places that read the byte.
; -----------------------------------------------------------------------------
cp_vid_pick:
    mov al, [cp_vsel]
    call cp_vid_rowok
    jnc .out
    mov al, [vid_kind]
    mov [cp_vsel], al
.out:
    ret

; -----------------------------------------------------------------------------
; cp_vid_slot - which adapter is drawn in row slot AL? (module internal)
;
; in:  AL = slot, 0..2 (the visible row, top to bottom - THREE is still the
;      most any machine has, VID_VGA and VID_EGA being mutually exclusive at
;      detection, so the bands below are still laid out for three)
; out: AL = the VID_* kind in it, CF = 0; CF = 1 = that slot has no row
; clobbers: AL, flags
;
; THE PAINT AND THE CLICK BOTH GO THROUGH THIS, which is the fm_hit argument
; of SPEC.md 22.2: the rows are COMPACTED - a machine with a Hercules and a
; CGA and no VGA draws them in slots 0 and 1, not 1 and 2 with a hole at the
; top - so "which kind is this row" is real arithmetic over [vid_avail] and
; not the identity. Two copies of it would be two opinions about what the
; user just clicked on.
; -----------------------------------------------------------------------------
cp_vid_slot:
    push bx
    push cx
    mov ch, al                  ; CH = the slot wanted
    xor cl, cl                  ; CL = slots filled so far
    xor bl, bl                  ; BL = the kind being considered
.n:
    mov al, bl
    call cp_vid_rowok
    jc .skip
    cmp cl, ch
    je .hit
    inc cl
.skip:
    inc bl
    cmp bl, 4                   ; VID_VGA / VID_HERC / VID_CGA / VID_EGA
    jb .n
    pop cx
    pop bx
    stc                         ; fewer rows than the slot asked for
    ret
.hit:
    mov al, bl
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; cpf_vidok - has the Set Primary button anything to do? (far, internal)
; in:  nothing
; out: CF = 0 yes, CF = 1 no (the dot is already on the running adapter)
; clobbers: nothing (flags)
;
; The SPEC.md 31.3 idiom: one predicate, read by the greying and by the
; click's refusal, so the two agree by construction. It had a third reader -
; the caption, which said WHY the button was grey - until SPEC.md 31.10.3
; made that line constant; two readers is still the reason this is a routine.
; -----------------------------------------------------------------------------
cpf_vidok:
    push ax
    call cp_vid_pick
    cmp al, [vid_kind]
    pop ax                      ; POP does not touch flags
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cp_vid_rows - the radio rows: glyph and label, one per available adapter
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; The label is drawn BEFORE the glyph because both want SI - the name out of
; cp_vidnam, then AX becomes the glyph's own argument - and os88ui_glyph
; fills its own box white
; first, so nothing is left half-erased between the two.
; -----------------------------------------------------------------------------
cp_vid_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    call cp_vid_pick            ; [cp_vsel] now names a row that exists
    xor bl, bl                  ; BL = slot
.r:
    mov al, bl
    call cp_vid_slot
    jc .done
    mov bh, al                  ; BH = the kind in this slot

    mov al, bl                  ; DX = this row's glyph top
    mov cl, CPV_ROWH
    mul cl                      ; AX = slot * pitch (mul r/m8: AX = AL * op)
    mov dx, bp
    add dx, CPV_R0Y
    add dx, ax

    mov al, bh                  ; --- the label
    xor ah, ah
    shl ax, 1
    mov si, ax
    CPSTAGEX cp_vidnam
    mov byte [gfx_color], CBLACK
    mov cx, di
    add cx, CP_PLX
    push dx
    add dx, CP_PLDY
    call cp_run
    pop dx

    mov ax, OS88UI_GRADIO       ; --- the glyph (AH = 0: a listed adapter is
    cmp bh, [cp_vsel]           ; one the machine has, so no row refuses)
    jne .g
    mov al, OS88UI_GRADIO | OS88UI_GON
.g:
    mov cx, di                  ; NO down state here: an adapter row SELECTS,
    add cx, CP_PGX              ; which is a safe prefix action, so it keeps
    call cp_gemit                ; the press (SPEC.md 13.6/13.8.3)
    inc bl
    cmp bl, 3
    jb .r
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cp_vid_msel - which of the three desktop radios is on (module internal)
; in:  nothing
; out: AL = 0 Single, 1 Right, 2 Below
; clobbers: AL, flags
;
; DERIVED from SPEC.md 39.19's two bytes every time it is asked - there is no
; page-local copy of the choice, the way the Scheduler page has none of the
; scheduler mode. A page that kept one would be a second opinion about a
; setting the kernel already holds.
; -----------------------------------------------------------------------------
cp_vid_msel:
    mov al, [vid_dmode]
    or al, al
    jz .out                     ; Single: no layout, and radio 0
    mov al, [vid_dlay]
    inc al
.out:
    ret

; -----------------------------------------------------------------------------
; cp_vid_mode - the desktop row: Single / Right / Below (SPEC.md 31.10.2)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; Drawn only where there is a choice - see the strings above. cp_vid_rows'
; label-before-glyph order, for its reason: both want AX, and os88ui_glyph
; fills its own box white first so nothing is left half-erased between the two.
;
; SPEC.md 31.10.3's row label is drawn HERE and not in cp_vid_paint, so that
; the one vid_dual_ok test covers both: a machine that gets no radios cannot
; be left with a heading over nothing.
; -----------------------------------------------------------------------------
cp_vid_mode:
    push ax
    push bx
    push cx
    push dx
    push si
    call KERNEL_SEG:cw_vid_dual_ok
    jc .done

    mov byte [gfx_color], CBLACK    ; --- the row's label (SPEC.md 31.10.3).
    CPSTAGE cp_s_vdesk              ; No erase: cp_page has already whitened
    mov cx, di                      ; the pane and nothing else ever draws
    add cx, CP_PMX                  ; here - the caption below is constant
    mov dx, bp                      ; too, so this page has no state line
    add dx, CPV_MLY
    call cp_run

    call cp_vid_msel
    mov bh, al                  ; BH = the radio that is on
    xor bl, bl                  ; BL = the radio being drawn
.r:
    mov al, bl                  ; --- SI = this radio's name, BEFORE AX becomes
    xor ah, ah                  ; the x offset: both want it
    shl ax, 1
    mov si, ax
    CPSTAGEX cp_vmodnam

    mov al, bl                  ; AX = this radio's x offset within the pane
    mov cl, CPV_MSTEP
    mul cl                      ; mul r/m8: AX = AL * op

    mov cx, di                  ; --- the label
    add cx, CP_PLX
    add cx, ax
    mov dx, bp
    add dx, CPV_MY + CP_PLDY
    mov byte [gfx_color], CBLACK
    push ax
    call cp_run
    pop ax

    mov cx, di                  ; --- the glyph. The x offset is SPENT into CX
    add cx, CP_PGX              ; before AX becomes the glyph's own argument,
    add cx, ax                  ; which is the same both-want-AX shuffle the
    mov dx, bp                  ; label above does one register over
    add dx, CPV_MY
    mov ax, OS88UI_GRADIO
    cmp bl, bh
    jne .g
    mov al, OS88UI_GRADIO | OS88UI_GON
.g:
    push bx                     ; ids 2..4, the Activate button being 1
    mov bh, bl
    add bh, 2
    call cp_dngly
    pop bx
    call cp_gemit
    inc bl
    cmp bl, 3
    jb .r
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; cp_vid_btn - the Activate button, greyed when there is nothing to activate
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; The frame AND the label take cpf_vidok's pen (SPEC.md 47 rule 2 - grey the
; WHOLE control), so on the two 1bpp adapters the ring comes out dotted and
; the caption a checkerboard rather than both staying solid black. The
; interior is white either way: [gfx_dis] is read by font_ink alone, so a
; white fill between the two pen calls neither carries the flag nor clears it.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; The two pages that draw a standard button build their rect in
; os88ui_krect - the ONE kernel scratch, declared in .bss by os88ui.inc for
; the reason its own comment gives (a word in .cold reached through DS is not
; that word). Neither page draws two buttons at once, and neither does anyone
; else on the UI task under the gfx lock.
; -----------------------------------------------------------------------------

cp_vid_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, di
    add ax, CPV_BTNX
    mov [os88ui_krect+0], ax
    add ax, CPV_BTNW - 1
    mov [os88ui_krect+4], ax
    mov ax, bp
    add ax, CPV_BTNY
    mov [os88ui_krect+2], ax
    add ax, CPV_BTNH - 1
    mov [os88ui_krect+6], ax

    call cpf_vidok              ; CF = 1 when there is nothing to activate
    mov di, OS88UI_FILL         ; the pane behind it is not white
    jnc .live
    or di, OS88UI_DIS           ; SPEC.md 47 rule 1: the frame AND the label
.live:                          ; dither together, which is one flag now
%ifdef KERN_BIG
    mov al, 1                   ; ...and pressed if it is being held; DIS
    call cp_dnbtn               ; outranks DOWN inside os88ui_btn
%endif
    CPSTAGE cp_s_vact
    mov bx, os88ui_krect
    call cp_bemit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_vid_paint - the Display page
; in:  DI = pane left, BP = pane top; gfx lock held; entry pen is CWHITE
; out: nothing
; clobbers: nothing (flags only)
;
; The caption is drawn INLINE here, and had a routine of its own until the
; string it chose between went (SPEC.md 31.10.3). It is one constant line on
; ground cp_page has already whitened, so it needs no erase, no predicate and
; no redraw when the selection moves - which is what a routine reached from
; the click path exists for.
; -----------------------------------------------------------------------------
cp_vid_paint:
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK ; entry pen is CWHITE (cp_page's erase)
    mov si, cp_s_vid            ; heading, same string as the list row
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PHY
    call cp_run

    call cp_vid_rows
    call cp_vid_btn
%ifdef KERN_BIG
    call cp_vid_mode
%endif

    mov byte [gfx_color], CBLACK ; ...and the caption, LAST on the page as it
    CPSTAGE cp_s_vcap            ; is on every other one
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPV_CAPY
    call cp_run

    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cp_vid_click - a click inside the Display page
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x, DX = pane-relative
;      y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only)
;
; X MATTERS HERE, unlike every other page in the panel: the radio bands run
; the pane's whole width as usual, but the button is a rect and a click to
; the right of it is not a press. The button's band is tested first because
; it sits below the rows and the two are contiguous.
;
; NOTHING IS REDRAWN AFTER A SUCCESSFUL SWITCH, and that is load-bearing
; rather than an economy: vid_switch changes the geometry and re-fits every
; window (SPEC.md 39.11.2), so DI/BP - this window's content origin, computed
; before the call - now describe a rectangle that may not exist. [cp_dirty]
; hands the whole screen to ui_task's step 3, which repaints it outside the
; lock (SPEC.md 13/31.2), and that is the only correct redraw here.
; -----------------------------------------------------------------------------
cp_vid_click:
    push ax
    push cx
    push dx

    cmp dx, CPV_BB1             ; --- the Activate button ---
    jl .rows
    cmp dx, CPV_BB2
    jg .mode                    ; ...and BELOW it is the desktop row now, not
                                ; the bottom of the page
    cmp cx, CPV_BTNX
    jl .donej
    cmp cx, CPV_BTNX + CPV_BTNW - 1
    jg .donej
    call cpf_vidok              ; greyed: the dot is already on the running
    jc .donej                   ; adapter, so a refused press says nothing
                                ; more (SPEC.md 47 rule 5)
%ifdef KERN_BIG
    mov al, 1                   ; the Activate button
    call cp_ctl
    jc .donej
%endif
    mov al, [cp_vsel]
    call KERNEL_SEG:vid_switch
    jc .donej                   ; refused: the adapter went away under us
    mov byte [cp_wdirty], 1     ; ...remembered on close (SPEC.md 31.8)
    mov byte [cp_dirty], 1      ; ...and the whole screen is a new shape
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
.donej:                         ; ...and SEVEN refusals share it
    jmp .done                   ; NEAR: 13.8.3's probe calls grew the body past
                                ; a short jump's reach
.mode:                          ; --- the desktop row (SPEC.md 31.10.2) ---
%ifdef KERN_BIG
    cmp dx, CPV_MB1
    jl .donej
    cmp dx, CPV_MB2
    jg .donej
    call KERNEL_SEG:cw_vid_dual_ok
    jc .done                    ; not drawn on this machine, so not clickable
    mov ax, cx                  ; AH = 0: cx is pane-relative and CP_PRGT is
    sub ax, CP_PGX              ; 221, so this cannot carry
    jl .done
    mov cl, CPV_MSTEP
    div cl                      ; AL = which radio, AH = the remainder
    cmp al, 3
    jae .done
    mov ah, al                  ; ...and is it the one already on?
%ifdef KERN_BIG
    add al, 2                   ; ids 2..4, the Activate button being 1
    call cp_ctl
    jc .done
    sub al, 2
%endif
    call cp_vid_msel
    cmp al, ah
    je .done                    ; draw nothing, exactly like the rows above

    mov al, ah                  ; SPEC.md 39.19's two bytes, from the three
    or al, al                   ; arrangements: Single has no layout
    jz .msing
    dec al
    mov [vid_dlay], al
    mov byte [vid_dmode], 1
    jmp short .mgo
.msing:
    mov byte [vid_dmode], 0
.mgo:
    ; ...and it takes effect NOW, with only the record of it waiting for the
    ; close (SPEC.md 31.8). NOTHING IS REDRAWN, for the Activate button's
    ; reason one screen up: vid_disp_init moves the desktop and re-fits every
    ; window, so DI/BP describe a rectangle that may no longer exist.
    call KERNEL_SEG:vid_disp_relayout
    mov byte [cp_wdirty], 1
    mov byte [cp_dirty], 1
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
%endif
    jmp short .done

.rows:                          ; --- the radio rows ---
    cmp dx, CPV_B0Y1
    jl .done
    cmp dx, CPV_B2Y2
    jg .done
    mov ax, dx                  ; AH = 0: the band check above bounds this
    sub ax, CPV_B0Y1
    mov cl, CPV_ROWH
    div cl                      ; AL = slot, AH = the remainder
    call cp_vid_slot
    jc .done                    ; a band with no row under it
%ifdef KERN_BIG
    push ax                     ; ...and the row SELECTS, which keeps the press
    xor al, al                  ; (SPEC.md 13.6): id 0, so the drag and the
    call cp_ctl                 ; release find it and return instead of moving
    pop ax                      ; the dot to whatever the pointer crossed on
    jc .done                    ; its way off the button (SPEC.md 13.8.3)
%endif
    cmp al, [cp_vsel]
    je .done                    ; already the pending choice: draw nothing
    mov [cp_vsel], al
    call cp_vid_rows            ; the dots...
    call cp_vid_btn             ; ...and the button, which may have just
                                ; un-greyed. The caption follows nothing now
                                ; (SPEC.md 31.10.3), so it is not redrawn
.done:
    pop dx
    pop cx
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
;
; Those two are at the BOTTOM of this page's code since SPEC.md 37.93: still
; SPEC.md 37's routines with SPEC.md 37's names, but assembled into this
; image because this page is the only thing that has ever called them.
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
    call cp_run

    mov al, 1                   ; the page draw: everything (SPEC.md 31.5.1)
    call cp_time_rows           ; both field rows, from the live clock

    mov al, 0                   ; the '+' button...
    call cp_timebtn
    mov al, 1                   ; ...and the '-' button
    call cp_timebtn

    mov byte [gfx_color], CBLACK ; the two option labels; their glyphs are
    CPSTAGE cp_s_t12            ; drawn by cp_timechk, which is also the
    mov cx, di                  ; redraw path when one is toggled
    add cx, CP_PLX
    mov dx, bp
    add dx, CPT_O0Y + CP_PLDY
    call cp_run
    CPSTAGE cp_s_tsec
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CPT_O1Y + CP_PLDY
    call cp_run
    call cp_timechk

    mov byte [gfx_color], CBLACK
    cmp byte [clk_rtc], 0       ; the caption: read live from the probe's
    je .nort                    ; verdict (SPEC.md 37.90) - informative only,
    push bx                     ; since editing works either way
    mov bl, [clk_tier]
    xor bh, bh
    add bx, bx
    CPSTAGEX cp_rtcnam, bx      ; ...and the TEST comes first now: staging the
    pop bx                      ; default and then overwriting SI would stage
    jmp short .cap              ; twice, and the second one is what SPEC.md
.nort:                          ; 2.8.6 says has to happen (the old shape was
    CPSTAGE cp_s_tnrt           ; `mov si, X` then a conditional `mov si, Y`,
.cap:                           ; which is the one place a blanket conversion
                                ; of this file stages the WRONG string)
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPT_CAP2Y
    call cp_run

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
    jc .donej
%ifdef KERN_BIG
    push ax                     ; the field SELECTS, so it keeps the press and
    xor al, al                  ; reports nothing: id 0 (SPEC.md 13.6/13.8.3).
    call cp_ctl                 ; Without it a slide across these rows with '+'
    pop ax                      ; held re-lettered both clock rows per mouse
    jc .donej                   ; packet - PERFORMANCE.md's input overrun
%endif
    cmp al, [cp_tsel]
    je .donej                   ; already selected: change nothing
    mov [cp_tsel], al
    mov al, 1                   ; the SELECTION moved, so the old black box has
    call cp_time_rows           ; to come off: a full draw (SPEC.md 31.5.1)
.donej:                         ; ...and the four refusals around it
    jmp .done

.btn:                           ; --- the button column ---
    cmp cx, CPT_BX + CPT_BW - 1
    jg .donej
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
%ifdef KERN_BIG
    mov al, 5                   ; '+' is 5 and '-' is 6; BL carries which and
    cmp bl, 1                   ; is what the action below reads
    je .stepc
    inc al
.stepc:
    call cp_ctl
    jc .done
%endif
    mov al, [cp_tsel]
    call clk_fld_adj            ; in AL = field, BL = +1/-1 (SPEC.md 37).
                                ; NEAR since SPEC.md 37.93: it is in this image
    mov byte [cp_wdirty], 1     ; ...and the panel is dirty (SPEC.md 31.8), which
                                ; is what makes cpf_cp_flush_close load this
                                ; module on the way out. WITHOUT IT a clock-only
                                ; edit - arrows and nothing else - hits that
                                ; thunk's [cp_wdirty] early-out and the RTC is
                                ; never written (SPEC.md 37.94). clk_fld_adj's
                                ; own [clk_dirty] still says whether the clock
                                ; is owed; this says the panel is.
    xor al, al                  ; a +/- step changes DIGITS and not the layout
    call cp_time_rows
    jmp .done

.opt:                           ; --- the two display options (SPEC.md 37) ---
    cmp dx, CPT_OB1Y2
    jg .done                    ; below the last band: nothing
    cmp dx, CPT_OB0Y2
    jg .osec

%ifdef KERN_BIG
    mov al, 7                   ; the 12-hour check
    call cp_ctl
    jc .done
%endif
    xor byte [clk_h12], 1       ; row 0: 12-hour clock. The hour field reads
    cmp byte [clk_h12], 0       ; differently now, and the meridiem field
    jne .oredraw                ; comes or goes with it - so if it was the
    cmp byte [cp_tsel], 6       ; selected one and just went away, the
    jb .oredraw                 ; selection has to come back inside the table
    mov byte [cp_tsel], 0
    jmp .oredraw
.osec:
%ifdef KERN_BIG
    mov al, 8                   ; the seconds check
    call cp_ctl
    jc .done
%endif
    xor byte [clk_secs], 1      ; row 1: seconds in the menu bar
.oredraw:
    mov byte [cp_wdirty], 1     ; both remembered on close (31.8)
    mov byte [clk_barq], 1      ; the bar's text (and width) changed: ui_task
                                ; redraws it in this same pass (SPEC.md 37).
                                ; NOT [cp_dirty] - a whole-desktop repaint to
                                ; fix a flicker would be a worse flash
    call cp_timechk             ; the glyph...
    mov al, 1                   ; 12/24 changes the FIELD COUNT, so the band
    call cp_time_rows           ; ...and the rows it just re-formatted

.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_tick_x - the once-a-second live refresh (far body of the cp_tick_x shim)
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
cp_tick_x:
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
    call KERNEL_SEG:cw_inst_find_kind         ; CF = 1: no live Control Panel
    jc .out
    mov bx, [di+I_WIN]
    test byte [bx+W_FLAGS], 2   ; visible?
    jz .out
    call KERNEL_SEG:cw_wm_obscured
    jc .out
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    mov di, ax
    add di, CP_RX               ; DI = pane left, BP = pane top
    mov bp, dx
    xor al, al                  ; SPEC.md 31.5.1: the tick draws seven opaque
    call cp_time_rows           ; runs and nothing else - no erase, no flash

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

    mov [cp_tfull], al          ; SPEC.md 31.5.1: 0 = the once-a-second tick
    or al, al
    jz .fields                  ; ...which erases nothing and draws no chrome

    mov byte [gfx_color], CWHITE
    mov ax, di
    mov bx, bp
    add bx, CPT_DY - 4
    mov cx, di
    add cx, CPT_BX - 4
    mov dx, bp
    add dx, CPT_TY + 11
    call KERNEL_SEG:cw_gfx_fill

    mov byte [gfx_color], CBLACK ; the time row's two ':' separators (the
    CPSTAGE cp_s_tcol            ; date row's gaps are simply blank)
    mov cx, di
    add cx, CPT_FX + 16
    mov dx, bp
    add dx, CPT_TY
    call cp_run
    mov cx, di
    add cx, CPT_FX + 40
    call cp_run
.fields:

    call KERNEL_SEG:cw_clk_snapshot               ; ONE snapshot for all the fields, so the
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

    ; --- selected: white text on a black box ---------------------------------
    mov word [cp_tpair], (CBLACK << 8) | CWHITE   ; AL = ink, AH = paper
    cmp byte [cp_tfull], 0
    je .text                    ; SPEC.md 31.5.1: on a tick the box is already
                                ; on the glass and its 2px surround is outside
                                ; the run, so drawing it again would be a
                                ; second write of pixels that did not change
    push cx
    push dx
    push bx
    push ax                     ; the box math below runs through AX, and AH
                                ; still has to name the field afterwards
    mov ax, cx
    dec ax                      ; x1 = x - 2
    dec ax
    mov si, cx
    mov cl, [bx+1]              ; the field's width
    mov ch, 0
    add cx, si
    inc cx                      ; x2 = x + w + 1
    mov bx, dx
    dec bx                      ; y1 = y - 2
    dec bx
    add dx, 9                   ; y2 = y + 9
    mov byte [gfx_color], CBLACK
    call KERNEL_SEG:cw_gfx_fill
    pop ax
    pop bx
    pop dx
    pop cx
    jmp short .text
.plain:
    mov word [cp_tpair], (CWHITE << 8) | CBLACK
.text:
    mov al, ah                  ; AL = field index for the formatter
    call clk_fld_str            ; SI = that field's text (SPEC.md 37) - near,
                                ; same image (SPEC.md 37.93)
    ; ...and the field is drawn OPAQUE (SPEC.md 6.1). Every field's text is
    ; exactly its own width - 'Mmm' is CPT_MONW, 'DD' is CPT_NUMW, 'YYYY' is
    ; CPT_YRW - so the run covers its field exactly and IS the erase for it
    ; (SPEC.md 27.2). That is what lets the tick above skip the band fill.
    mov ax, [cp_tpair]
    call KERNEL_SEG:cw_font_run               ; in SI, CX = x, DX = y

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
    dec ax                      ; AX = x1 = x - 2
    dec ax
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
; Both are always drawn and os88ui_glyph white-fills its own box, so this is
; also the redraw path after a toggle.
; -----------------------------------------------------------------------------
cp_timechk:
    push ax
    push cx
    push dx
    push si

    mov cx, di
    add cx, CP_PGX
    mov dx, bp
    add dx, CPT_O0Y
    mov ax, OS88UI_GCHECK       ; --- row 0: 12-hour clock --- (AH = 0: a
    cmp byte [clk_h12], 0       ; clock option is never refused)
    je .r0
    mov al, OS88UI_GCHECK | OS88UI_GON
.r0:
%ifdef KERN_BIG
    push bx
    mov bh, 7
    call cp_dngly
    pop bx
%endif
    call cp_gemit
    mov dx, bp
    add dx, CPT_O1Y
    mov ax, OS88UI_GCHECK       ; --- row 1: seconds in the menu bar ---
    cmp byte [clk_secs], 0
    je .r1
    mov al, OS88UI_GCHECK | OS88UI_GON
.r1:
%ifdef KERN_BIG
    push bx
    mov bh, 8
    call cp_dngly
    pop bx
%endif
    call cp_gemit
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
    push di

    CPSTAGE cp_s_tplus
    mov dx, CPT_BUY
    or al, al
    jz .have
    CPSTAGE cp_s_tminus
    mov dx, CPT_BDY
.have:
    mov ax, di
    add ax, CPT_BX
    mov [os88ui_krect+0], ax
    add ax, CPT_BW - 1
    mov [os88ui_krect+4], ax
    mov ax, bp
    add ax, dx
    mov [os88ui_krect+2], ax
    add ax, CPT_BH - 1
    mov [os88ui_krect+6], ax

    mov di, OS88UI_FILL         ; always live: a field is always adjustable
%ifdef KERN_BIG
    mov al, 5                   ; '+' is id 5, '-' is 6 - and AL arrived as
    cmp dx, CPT_BUY             ; 0/1 for which, spent above into DX
    je .dn
    inc al
.dn:
    call cp_dnbtn
%endif
    mov bx, os88ui_krect
    call cp_bemit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; SPEC.md 37's RTC WRITE HALF, hosted here too (SPEC.md 37.94). A file of its
; own rather than 600 lines of MC146818, MM58167 and RP5C01 in the middle of
; the Control Panel - it carries no `section` of its own precisely so that it
; lands in THIS one, and tools/os88ovlchk.py knows that through its EXTRA map.
%ifdef OS88_RTC                 ; SPEC.md 37.0.1: with no rung on this build
%include "clockw.inc"           ; there is no chip to write to either, and
%endif                          ; this is CTRL.DRV's copy of the write half

; =============================================================================
; SPEC.md 37's FIELD EDITOR, hosted here (SPEC.md 37.93)
;
; clk_fld_str / clk_fld_adj / clk_step are the clock's code and keep the
; clock's prefix - SPEC.md 37 owns their contract and clock.inc is where a
; reader looks for them. They are assembled HERE because this page is their
; only caller, so they belong in this page's image rather than in the kernel:
; the panel is shut almost always, and an editor for a shut window was 329
; bytes of .text that every machine carried all session.
;
; Not in clock.inc, and that is a layout fact rather than a preference:
; `.modc` is ONE image whose offset 0 must be modc_hdr, NASM lays a section
; out in source order, and kernel.asm %includes clock.inc BEFORE ctrl.inc - so
; a `section .modc` block over there would land in front of the header and the
; loader would check a header that is not one.
;
; DS is KERNEL_SEG here exactly as it was in .text, so every [clk_*] below is
; unchanged. What DID change is the six formatters these reach: those stay
; resident (clk_fmt draws the bar from five of them, clk_inc_sec's day carry
; reaches the sixth), so each is far-called through its cw_clk_* thunk in
; kernel.asm. os88ovlchk.py refuses any of them written near.
; =============================================================================

; -----------------------------------------------------------------------------
; clk_fld_str - format ONE field for the Date/Time page (SPEC.md 31.5)
; in:  AL = field: 0 month, 1 day, 2 year, 3 hour, 4 minute, 5 second,
;      6 meridiem (12-hour mode only)
; out: SI = clk_fbuf, the field alone, always its full width (zero-padded -
;      a field is a fixed-width editable cell whose highlight box must not
;      change size under it, so unlike clk_fmt the 12-hour hour keeps its
;      leading zero: '12', '01'..'11')
; clobbers: nothing else
; Reads the LAST SNAPSHOT and does not take one: a caller drawing several
; fields (cp_time_rows) calls clk_snapshot once, so its whole row is one
; instant.
; -----------------------------------------------------------------------------
clk_fld_str:
    push ax
    push bx
    mov bx, clk_fbuf
    or al, al
    jz .mon
    cmp al, 1
    je .day
    cmp al, 2
    je .year
    cmp al, 3
    je .hour
    cmp al, 4
    je .min
    cmp al, 5
    je .sec

    mov al, [clk_sn_hour]       ; 6: the meridiem
    call KERNEL_SEG:cw_clk_ampm ; AL = 'A' or 'P'
    mov [bx], al
    inc bx
    mov byte [bx], 'M'
    inc bx
    jmp .done
.sec:
    mov al, [clk_sn_sec]
    jmp .two
.min:
    mov al, [clk_sn_min]
    jmp .two
.hour:
    mov al, [clk_sn_hour]
    cmp byte [clk_h12], 0
    je .two
    call KERNEL_SEG:cw_clk_h12h ; AL = 1..12, still zero-padded below
    jmp .two
.day:
    mov al, [clk_sn_day]
.two:
    call KERNEL_SEG:cw_clk_put2
    jmp .done
.mon:
    mov al, [clk_sn_mon]
    call KERNEL_SEG:cw_clk_put_mon
    jmp .done
.year:
    mov ax, [clk_sn_year]
    call KERNEL_SEG:cw_clk_put4
.done:
    mov byte [bx], 0
    mov si, clk_fbuf
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_fld_adj - step one field, with wrap (SPEC.md 31.5)
; in:  AL = field (as clk_fld_str), BL = +1 or -1
; out: nothing (all registers preserved)
;
; A month or year step re-clamps the day to the new month's length, so
; 31 March minus one month is 28 February and never 31 February. The hour
; is always stored and stepped 0..23 - 12-hour mode is a rendering of it,
; not a second representation - so field 6 (the meridiem) is the only thing
; that moves by 12, and either sign of the step flips it. Any edit restarts
; the sub-second accumulator from now (setting the seconds to a value and
; having it advance a tenth of a second later would be wrong), posts
; [clk_dirty] so ui_task writes the hardware RTC outside the lock, and posts
; [clk_barq] so the menu bar follows even when it is not showing seconds.
; -----------------------------------------------------------------------------
clk_fld_adj:
    push ax
    push bx
    push cx
    push dx
    pushf                       ; one edit = one critical section, so a
    cli                         ; reader never catches the day mid-clamp
    mov ah, bl                  ; AH = the step, +1 or -1
    or al, al
    jz .mon
    cmp al, 1
    je .day
    cmp al, 2
    je .year
    cmp al, 3
    je .hour
    cmp al, 4
    je .min
    cmp al, 5
    je .sec

    mov al, [clk_hour]          ; 6: the meridiem - +-12 hours, either sign
    cmp al, 12
    jb .pm
    sub al, 12                  ; PM -> AM
    jmp .merset
.pm:
    add al, 12                  ; AM -> PM
.merset:
    mov [clk_hour], al
    jmp .done
.sec:
    mov cx, 0x3B00              ; second: CH = 59 max, CL = 0 min
    mov al, [clk_sec]
    call clk_step
    mov [clk_sec], al
    jmp .done
.min:
    mov cx, 0x3B00
    mov al, [clk_min]
    call clk_step
    mov [clk_min], al
    jmp .done
.hour:
    mov cx, 0x1700              ; CH = 23, CL = 0
    mov al, [clk_hour]
    call clk_step
    mov [clk_hour], al
    jmp .done
.day:
    mov al, [clk_mon]
    call KERNEL_SEG:cw_clk_mlen
    mov ch, al                  ; max = this month's length
    mov cl, 1
    mov al, [clk_day]
    call clk_step
    mov [clk_day], al
    jmp .done
.mon:
    mov cx, 0x0C01              ; CH = 12, CL = 1
    mov al, [clk_mon]
    call clk_step
    mov [clk_mon], al
    jmp .fixday
.year:
    mov bx, [clk_year]
    cmp ah, 0
    jl .ydn
    inc bx
    cmp bx, CLK_YMAX
    jbe .yset
    mov bx, CLK_YMIN
    jmp .yset
.ydn:
    dec bx
    cmp bx, CLK_YMIN
    jae .yset
    mov bx, CLK_YMAX
.yset:
    mov [clk_year], bx

.fixday:                        ; the month (or the leap year) may have got
    mov al, [clk_mon]           ; shorter under the current day
    call KERNEL_SEG:cw_clk_mlen
    cmp [clk_day], al
    jbe .done
    mov [clk_day], al

.done:
    mov ax, [ticks]             ; the new second starts now
    mov [clk_last], ax
    mov word [clk_acc], 0
    mov byte [clk_dirty], 1     ; ui_task step 3 writes the RTC (SPEC.md 13)
    mov byte [clk_barq], 1      ; ...and step 4 refreshes the bar, which may
                                ; otherwise be a minute from noticing
    popf
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_step - add a signed step to a byte, wrapping into a closed range
; in:  AL = value, AH = step (+1 / -1), CL = range min, CH = range max
; out: AL = the wrapped value
; clobbers: nothing else (flags)
; Values are small unsigned bytes, so stepping down from 0 lands on 0xFF -
; above the max - and the step's sign is what tells the two overflows apart.
; -----------------------------------------------------------------------------
clk_step:
    add al, ah
    cmp al, ch
    jbe .lo
    cmp ah, 0
    jl .under                   ; 0xFF from a -1: wrap to the top
    mov al, cl                  ; past the max: wrap to the bottom
    ret
.under:
    mov al, ch
    ret
.lo:
    cmp al, cl
    jae .out
    mov al, ch                  ; below the min: wrap to the top
.out:
    ret

section .modc                   ; the on-demand image (SPEC.md 2.8): a segment
                                ; of its own with DS still KERNEL_SEG, so
                                ; every reference below - this module's own
                                ; strings included - is unchanged


; -----------------------------------------------------------------------------
; cp_flush_x - persist every Control Panel setting (SPEC.md 31.8/51.5)
; in:  nothing
; out: nothing (all registers preserved)
;
; **NOT called by a page, and no longer callable by one.** cp_flush_close_x
; calls it, from the two ways the panel stops existing, and nothing else does -
; a page that changed something sets [cp_wdirty] and returns (SPEC.md 31.8, and
; the flag's own comment for why that is a rule rather than a default). It is
; a near call inside .cold with no .text thunk in front of it, so a page, a
; driver's page or an API cell cannot reach it even by naming it.
;
; It is one write of one 32-byte
; file, and the file carries the whole panel - the driver list, the sound
; route, the clock options and the scheduler mode - so there
; is no per-setting bookkeeping to keep in step.
;
; It writes ONCE, when the panel closes, rather than on every click. A floppy
; write is ~a second on the floor machine and the panel is frozen for it, so
; a session of trying settings out used to cost a second per click. Pages mark
; [cp_wdirty] instead and cp_flush_close_x spends it.
;
; What that gives up is the crash window: a machine switched off with the
; panel still open loses whatever was changed. That is the deliberate trade -
; the settings are a convenience, not a document - and it is bounded by
; flushing on the two ways the panel can go away, teardown and reboot, rather
; than on the one obvious one.
;
; **A FAILED WRITE IS SAID AS A TOAST** (SPEC.md 59.6). It used to be carried
; in [cp_dsave] to the Drivers page's caption - the only page with room for it
; - and reported the NEXT time the panel was opened, because by the time the
; write happens the page is gone. That is a verdict stored to be found later,
; which is exactly what a toast replaces: it says so WHEN IT HAPPENS, on the
; bar, which no window can cover and which is still there after this one is
; destroyed. [cp_dsave] survives for its other job - cp_flush_close_x leaves
; [cp_wdirty] set on failure, so the next close retries rather than silently
; dropping the settings.
;
; It is said AFTER the write and not before. That used to be forced - the
; write arms SPEC.md 12.8's file-activity widget, which shared the strip's
; pixels and retired it - and since SPEC.md 59.8 moved the strip into the
; clock's field the two are independent and the ordering is merely the honest
; one: the message IS the verdict, and drv_cfg_save is what produces it.
; -----------------------------------------------------------------------------
cp_flush_x:
    push ax
    push bx
    push cx
    call cp_cfg_save            ; SPEC.md 51.5.3: the writer is in THIS image
    mov byte [cp_dsave], 0
    jnc .say
    mov byte [cp_dsave], 2      ; the write itself failed: full, protected, I/O
    cmp ax, FERR_NODISK         ; ...as against no disk in A: at all, or a disk
    je .wrongdisk               ; that is not the SYSTEM one (SPEC.md 51.5.1),
    cmp ax, FERR_NOENT          ; which is one thing the user can act on and so
    jne .say                    ; earns a message that says what to do
.wrongdisk:
    mov byte [cp_dsave], 1
.say:
    mov al, [cp_dsave]
    inc al                      ; slot 0 stays toast_say's "nothing to say"
    mov bx, cp_savetab
    mov cx, CP_NSAVE
    call KERNEL_SEG:toast_say
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_flush_close_x - spend [cp_wdirty], if there is anything to spend
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
cp_flush_close_x:
%ifdef OS88_RTC
    cmp byte [clk_dirty], 0     ; THE HARDWARE CLOCK FIRST (SPEC.md 37.94), and
    je .cfg                     ; before the floppy: this is port I/O in
    mov byte [clk_dirty], 0     ; microseconds where drv_cfg_save below is two
    call clk_rtc_write          ; seconds of motor, and a write that fails
.cfg:                           ; leaves nothing to retry either way
%else
    mov byte [clk_dirty], 0     ; SPEC.md 37.0.1: no chip to write the setting
                                ; to, so the edit is the SESSION's and the
                                ; flag is cleared rather than acted on
%endif
    cmp byte [cp_wdirty], 0
    je .out
    call cp_flush_x
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
; That second half is the one place this page departs from the sw_avail idiom
; (SPEC.md 31.3), and deliberately: sw_canfit is a real predicate and
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
section .text


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
section .modc                   ; the on-demand image (SPEC.md 2.8): a segment
                                ; of its own with DS still KERNEL_SEG, so
                                ; every reference below - this module's own
                                ; strings included - is unchanged


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
    call cp_run

    xor bx, bx                      ; BX = row 0..2
.lbl:
    mov al, bl
    call cp_snd_rowok               ; greyed rows still draw - they are what
    call KERNEL_SEG:cw_gfx_pen_cf                 ; the machine does not have, and a missing
                                    ; row would just look like a shorter list
    CPSTAGE cp_s_sspk               ; three fixed labels, in ladder order. Each
    or bl, bl                       ; arm STAGES rather than the join doing it
    jz .str                         ; once - the join cannot, because the
    CPSTAGE cp_s_sfm                ; selection is a pointer into the image and
    cmp bl, 1                       ; naming one outside CPSTAGE is what the
    je .str                         ; gate refuses. Rows 1 and 2 therefore copy
    CPSTAGE cp_s_ssb                ; a string they discard: 6 bytes of the
.str:                               ; image and ~30 clocks, on a page paint
    mov cx, di
    add cx, CP_PLX
    mov ax, CP_PROWH
    mul bl                          ; AX = bl * CP_PROWH (bl <= 2, so AH = 0)
    mov dx, bp
    add dx, ax
    add dx, CP_PR0Y + CP_PLDY
    call cp_run
    inc bl
    cmp bl, 3
    jb .lbl
    call KERNEL_SEG:cw_gfx_pen_live

    call cp_snd_test

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
; THE GLYPH IS GREYED TOO, not just the label beside it, which is SPEC.md 47
; rule 2 - grey the WHOLE control - and not a workaround for anything.
;
; Both halves take cp_snd_rowok's pen through gfx_pen_cf, and on mono the two
; reductions differ in kind but agree in meaning: the ring is drawn with
; gfx_pixel, so gfx_ink maps CDGRAY to the 50% dither and it comes out DOTTED;
; the label is a font cell, so font_ink masks a flagged glyph and it comes out
; a CHECKERBOARD. Dark grey on VGA, dithered on the other two, and on all
; three the whole row reads as disabled.
;
; This comment used to say the label could not carry it - that a dithered 8x8
; glyph was unreadable and mono had no grey to draw a letter in, so the ring
; had to say it instead. That was false, and false about the renderer sitting
; underneath it: [gfx_dis] has masked disabled text since SPEC.md 47 landed.
; Go and look at the Display page's 'Double Buffered' on CGA.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; cp_snd_test - the Sound page's Test button (module internal)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; Lifted out of cp_snd_paint whole so that cp_ctlpaint can redraw it alone
; when it goes down or comes up (SPEC.md 13.8.3) - the page's other controls
; already had painters of their own and this one did not.
;
; IT TAKES THE FILL NOW, which the inline version deliberately did not: it
; was drawn once onto a pane cp_page had whited and nothing redrew it in
; place. A pressed state IS a redraw in place, and the ground under it is the
; other state of itself (apps/os88ui.inc's OS88UI_FILL note).
; -----------------------------------------------------------------------------
cp_snd_test:
    push ax
    push bx
    push si
    push di
    mov ax, di                      ; the Test button, through the shared
    add ax, CPS_BTX1                ; control (apps/os88ui.inc) like every
    mov [os88ui_krect+0], ax        ; other button in the machine
    mov ax, di
    add ax, CPS_BTX2
    mov [os88ui_krect+4], ax
    mov ax, bp
    add ax, CPS_BTY1
    mov [os88ui_krect+2], ax
    mov ax, bp
    add ax, CPS_BTY2
    mov [os88ui_krect+6], ax
    CPSTAGE cp_s_stest
    mov bx, os88ui_krect
    mov di, OS88UI_FILL             ; DI is the pane's left edge everywhere in
                                    ; this file and the flags word wants it -
%ifdef KERN_BIG
    mov al, 1                       ; the one site that has to bank it
    call cp_dnbtn
%endif
    call cp_bemit
    pop di
    pop si
    pop bx
    pop ax
    ret

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
    call cp_snd_rowok           ; ...and the same predicate cp_snd_paint gave
    mov ax, OS88UI_GRADIO       ; the label, so the two halves of a row can
    adc ah, 0                   ; never disagree about being disabled - the
                                ; answer travels as the FLAG now instead of as
                                ; a pen the caller sets first, so AH = 1 when
                                ; the row would refuse (ADC after a `mov` that
                                ; does not touch CF)
    cmp bl, bh
    jne .draw
    mov al, OS88UI_GRADIO | OS88UI_GON
.draw:
%ifdef KERN_BIG
    push bx                     ; ids 2..4, the Test button being 1
    mov bh, bl
    add bh, 2
    call cp_dngly
    pop bx
%endif
    call cp_gemit                ; ...which leaves the pen LIVE by itself, so
    add dx, CP_PROWH            ; the gfx_pen_live that used to close this
    inc bl                      ; loop is gone too
    cmp bl, 3
    jb .r
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
    mov ax, CP_PMX              ; nothing to this line's left on this page
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
    call cp_run
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
%ifdef KERN_BIG
    mov al, 1                   ; the Test button
    call cp_ctl
    jc .done
%endif
    mov ax, 660                 ; one second of a tone through whatever the
    mov cx, 18                  ; page currently selects. UI priority, so it
    mov dl, SND_PRI_UI          ; outranks anything a package is playing -
    call KERNEL_SEG:cw_osapi_snd_tone         ; the user asked for it
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
                                ; has already said so.
                                ; **AHEAD of cp_ctl, deliberately**, the way
                                ; cpf_vidok and cp_drv_arrowok are: a control
                                ; that will refuse must not arm, or the press
                                ; draws it held and SPEC.md 13.8's "disabled
                                ; outranks down" is broken by this page
%ifdef KERN_BIG
    add al, 2                   ; ids 2..4, the Test button being 1
    call cp_ctl
    jc .done
    mov al, bl
%endif
    xor bh, bh
    mov ah, [bx+cp_snd_tiers]   ; the SND_RT_* that row asks for
    mov byte [cp_snd_err], 0
    cmp ah, [snd_route]
    je .redraw                  ; already the setting: still redraw, because
                                ; the click clears a stale notice
    call COLD_SEG:drvf_drv_tier               ; ask the driver to move; AL = why not
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
; notice uses). That is the sw_avail idiom again: the box, the caption and
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
    call cp_run

    call cp_drv_mempar              ; ...and one bit for the whole page before
                                    ; any row is lettered: do the memory
                                    ; figures fit with their brackets
                                    ; (SPEC.md 31.6.2)

    mov bl, [cp_dtop]               ; BX = the drv_tab row, from the top of the
    xor bh, bh                      ; WINDOW rather than of the table
.row:
    push bx
    mov al, bl
    call COLD_SEG:drvf_drv_row                    ; BX = the drv_tab row
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
    call cp_run                                   ; the driver's name
    pop bx

    pop ax                          ; ...roughly what it costs to run, right of
    push ax                         ; the name on the name's own line
    call cp_drv_mem1                ; (SPEC.md 31.6.2). BX survives it, and the
                                    ; status call below still wants the row

    call COLD_SEG:drv_status_x                    ; ...and what it is doing right now
    pop ax
    push ax
    call cp_drv_rowy
    add dx, CP_DSTDY
    mov cx, di
    add cx, CP_PLX
    call cp_run                                   ; no erase needed: cp_page has
                                                  ; just white-filled the pane
.next:
    pop bx
    inc bx
    cmp bx, DRV_MAX
    jae .rdone
    mov al, [cp_dtop]
    add al, CP_DVIS
    xor ah, ah
    cmp bx, ax
    jb .row
.rdone:
    call cp_drv_arrows              ; ...and the two that move the window
    call cp_drv_boxes               ; every checkbox, from the live state.
                                    ; NO save caption: a failed write is said
                                    ; as a toast at the moment it fails
                                    ; (SPEC.md 59.6), so this page has nothing
                                    ; left to report from a previous session
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
CP_DSTDY  equ CP_PLDY + 9       ; 11: the reason line, one text row under the
                                ; label - and EXACTLY the checkbox's last row,
                                ; because the box is CP_GW (12) tall from the
                                ; same origin. Its erase therefore starts at
                                ; CP_PLX and not at the pane margin: the two
                                ; share a scan line and only the x separation
                                ; keeps them apart (cp_drv_line1)
; The hit bands are DERIVED from DRV_MAX rather than enumerated, and that is
; a fix rather than tidiness: they were three constants describing exactly two
; rows (CP_DB0Y1/CP_DB0Y2/CP_DB1Y2), so the third driver row (the serial
; monitor, since retired - SPEC.md 58)
; drew, said what it was doing, and could not be clicked - a control that
; looks live and is not, which is SPEC.md 47 rule 4's failure arriving where
; rule 4 does not look because no predicate refuses anything. One band per
; row, one pitch tall, and cp_drv_click divides.
CP_DVIS   equ 4               ; ROWS THE PANE FITS, and it is not DRV_MAX
                              ; (SPEC.md 31.1.1). 26px of pitch from CP_DR0Y
                              ; in a 132px pane means a FIFTH row's glyphs
                              ; start at 128 and its reason line falls off the
                              ; bottom - and the panel cannot grow, because
                              ; CGA is 200 rows and wm_fit's ceiling is 155
                              ; against a frame that is already 151. So the
                              ; list scrolls, which is what SPEC.md 31.1 named
                              ; as the answer the first time a fifth driver
                              ; was wanted
CP_DBY1   equ CP_DR0Y - 4                       ; the first band's top
CP_DBYN   equ CP_DBY1 + CP_DROWH * CP_DVIS      ; ...and one past the last

; The two arrows, in a 14px column at the pane's right - the Disk window's
; scroll-bar width, because it is the same gesture. NO THUMB and no track:
; a thumb reports a POSITION IN A LIST, and a list of five rows showing four
; has one bit of position in it that the two arrows' greying already carries
; (SPEC.md 47 rule 1 - disabled is a FACT here, not a guess).
CP_DSW    equ 14
CP_DSX1   equ 200
CP_DSX2   equ CP_DSX1 + CP_DSW - 1
CP_DSAH   equ 14              ; each arrow cell is square

; The MEMORY COLUMN (SPEC.md 31.6.2): roughly what a driver holds while it is
; doing its job, on the name's own line and right of it. It is a compile-time
; figure out of drv_memk, so it is drawn by cp_drv_paint and by nothing else -
; a click cannot change it, and the click path stays at the two controls
; SPEC.md 31.6.1 costed.
;
; The 'K' is what the rows ALIGN ON rather than the string's end, so a row
; carrying the RAM disk's trailing '+' keeps its digits under everybody
; else's. Nothing is padded with spaces: the x is computed from the string's
; own length, because a blank cell costs the same ~900us as a lettered one
; (PERFORMANCE.md Part 2) and four of them a row is 3.6ms of the field
; machine's time to draw nothing.
CP_DMKX   equ 160             ; the 'K' cell's LEFT edge, pane-relative. 160 and not
                              ; 168: the RAM disk's trailing '+' pushes its
                              ; bracket to 191, and eight pixels off the
                              ; scroll arrows at CP_DSX1 reads as touching
                              ; them
CP_DMGAP  equ 8               ; the least air between the longest driver name
                              ; and the column's first glyph, below which
                              ; cp_drv_mempar takes the brackets off
CP_DMMAX  equ 12              ; cp_dmbuf: '(~' + 5 digits + 'K+)' + NUL is 11
%if CP_DMKX + 8 * 3 > CP_DSX1
  %error "the driver memory column runs into the scroll arrows: 'K', '+' and ')'"
%endif

; The Drivers page's control ids (SPEC.md 13.8.3). A ROW is its table index
; plus one, so rows occupy 1..DRV_MAX and the arrows take the two above them -
; the id space is per PAGE, so nothing else has to be consulted.
CP_DIDUP  equ DRV_MAX + 1
CP_DIDDN  equ DRV_MAX + 2


cp_drv_rowy:
    push ax
    push cx
    mov ah, 0
    sub al, [cp_dtop]           ; ...THE VISIBLE POSITION of this table row.
                                ; Doing it here rather than at the call sites
                                ; is what let the list scroll without touching
                                ; one of them: every caller still names a
                                ; drv_tab row, which is the only index the
                                ; rest of the page has ever used
    mov cx, CP_DROWH
    mul cx
    add ax, CP_DR0Y
    add ax, bp
    mov dx, ax
    pop cx
    pop ax
    ret


; -----------------------------------------------------------------------------
; cp_drv_mempar - is there room for the brackets? (SPEC.md 31.6.2)
; in:  nothing
; out: [cp_dmpar] = 1 = yes; all registers preserved
;
; Over the WHOLE table and not the four rows on screen, so the answer cannot
; change under a scroll: a column that gained brackets halfway down the list
; would read as two different columns. No drawing call and no disk - it is a
; walk of five rows and two string lengths, which is why it can afford to be
; recomputed on every paint rather than cached and invalidated.
;
; Today it always says yes: 'Hard Drive' is ten cells and ends at 102, and a
; bracketed three-digit figure starts at 136. It exists for the driver whose
; name is longer than that, and it answers ONE bit for the page rather than
; one per row for exactly that reason.
; -----------------------------------------------------------------------------
cp_drv_mempar:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [cp_dmnw], 0
    mov byte [cp_dmdg], 1       ; a one-digit figure at the very least
    xor bx, bx
.row:
    push bx
    mov al, bl
    call COLD_SEG:drvf_drv_row  ; BX = the drv_tab row (AX preserved)
    cmp word [bx+DRVR_FILE], 0
    je .next                    ; an empty row names nothing and costs nothing
    mov si, [bx+DRVR_TITLE]
    xor cx, cx
.name:
    cmp byte [si], 0            ; the font is a fixed 8x8 (font_width), so a
    je .named                   ; length IS a width and there is no thunk to
    inc si                      ; reach for
    inc cx
    jmp short .name
.named:
    cmp cl, [cp_dmnw]
    jbe .num
    mov [cp_dmnw], cl
.num:
    pop si                      ; the ROW INDEX again, for drv_memk
    push si
    shl si, 1
    mov ax, [drv_memk + si]
    and ah, 0x7F                ; DRVM_PLUS is a flag, not a digit
    or ax, ax
    jz .next                    ; a row with no figure at all
    mov si, 10
    xor cx, cx
.dig:
    xor dx, dx
    div si
    inc cx
    or ax, ax
    jnz .dig
    cmp cl, [cp_dmdg]
    jbe .next
    mov [cp_dmdg], cl
.next:
    pop bx
    inc bx
    cmp bx, DRV_MAX
    jb .row

    mov al, [cp_dmdg]           ; where a BRACKETED field would start...
    add al, 2                   ; ...'(' and '~' in front of the digits
    xor ah, ah
    mov cl, 3
    shl ax, cl                  ; cells -> pixels
    mov dx, CP_DMKX
    sub dx, ax
    mov al, [cp_dmnw]           ; ...against where the longest name ends
    xor ah, ah
    shl ax, cl
    add ax, CP_PLX + CP_DMGAP
    mov byte [cp_dmpar], 1
    cmp ax, dx
    jbe .out
    mov byte [cp_dmpar], 0      ; two cells narrower without them, and the
                                ; digits still align on the 'K'
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_memstr - one drv_memk word as text (module internal)
; in:  AX = the drv_memk word, [cp_dmpar]
; out: cp_dmbuf = the NUL string, [cp_dmkc] = cells up to and including the
;      'K'; all registers preserved
;
; '(~34K)', or '~34K' where cp_drv_mempar found no room, or '(~21K+)' for the
; RAM disk - whose store is the USER's number and is shown where it is chosen
; (SPEC.md 62.9.10.1), so its row prices the driver and says '+' for the rest.
; -----------------------------------------------------------------------------
cp_drv_memstr:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    push di
    mov [cp_dmplus], ah         ; the flag rides bit 15, out of the way of the
    and ah, 0x7F                ; divide below
    mov di, cp_dmbuf
    cmp byte [cp_dmpar], 0
    je .tilde
    mov byte [di], '('
    inc di
.tilde:
    mov byte [di], '~'          ; SPEC.md 31.6.2: the figure is the SHAPE of
    inc di                      ; the cost and not a reading
    mov bx, 10
    xor cx, cx
.dv:
    xor dx, dx
    div bx                      ; AX = the quotient, DX = one digit
    push dx
    inc cx
    or ax, ax
    jnz .dv                     ; a zero figure still emits one '0'
.em:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .em
    mov byte [di], 'K'
    inc di
    mov cx, di
    sub cx, cp_dmbuf            ; ...the alignment column, banked before the
    mov [cp_dmkc], cl           ; two that may follow it
    test byte [cp_dmplus], 0x80
    jz .close
    mov byte [di], '+'
    inc di
.close:
    cmp byte [cp_dmpar], 0
    je .term
    mov byte [di], ')'
    inc di
.term:
    mov byte [di], 0
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_mem1 - one driver row's memory column (SPEC.md 31.6.2)
; in:  AL = row index, DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; Called from cp_drv_paint's row loop and from nowhere else. The figure is a
; constant, so a load or an unload cannot change it and cp_drv_click still
; redraws exactly the checkbox and the reason line (SPEC.md 31.6.1) - which is
; the whole reason this can be five or six glyph cells without showing up in
; the click budget.
; -----------------------------------------------------------------------------
cp_drv_mem1:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [drv_memk + bx]
    or bx, bx
    jz .out                     ; a row with no figure says nothing rather
                                ; than '~0K'
    call cp_drv_rowy            ; DX = this row's glyph top (AL = the row)
    add dx, CP_PLDY             ; ...and the NAME's baseline: this is a column
                                ; on that line, not a second caption
    mov ax, bx
    call cp_drv_memstr
    mov cl, [cp_dmkc]
    dec cl                      ; cells BEFORE the 'K'...
    xor ch, ch
    shl cx, 1
    shl cx, 1
    shl cx, 1                   ; ...in pixels (cpu 8086: no shl by an
                                ; immediate other than 1)
    mov ax, CP_DMKX
    sub ax, cx                  ; so every row's 'K' lands on one column
    add ax, di
    mov cx, ax
    mov si, cp_dmbuf
    mov byte [gfx_color], CBLACK
    call cp_run
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_box1 - one driver row's checkbox, drawn from the live table
; in:  AL = row index, DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; The box tracks DRVR_SEG - what is LOADED - and not DRVR_WANT, which is only
; what the settings file asks for. A driver the user enabled on a machine with
; no card is unchecked, and its caption says why.
;
; Per ROW because a click changes one, and os88ui_glyph is not cheap: a white
; 12x12 fill and then one gfx_pixel per set bit - 44 of them for the empty
; box, 64 for the crossed one - which at PERFORMANCE.md Part 2's ~756us of
; fixed cost per drawing call is ~35-50ms of the field machine's time. Redrawn
; unconditionally it is also a double-draw flash on a control that did not
; change.
; -----------------------------------------------------------------------------
cp_drv_box1:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [gfx_color], CBLACK    ; kept for the EMPTY-row path only: that
                                    ; one draws nothing, so os88ui_glyph never
                                    ; runs and the documented pen would not
                                    ; hold. On the drawn path the control
                                    ; leaves it live by itself
    call COLD_SEG:drvf_drv_row      ; AL survives (drv_row preserves AX)
    cmp word [bx+DRVR_FILE], 0
    je .out                         ; an empty table row is not a control
    call cp_drv_rowy                ; DX = this row's glyph top - taken BEFORE
                                    ; AX becomes the glyph's argument, because
                                    ; both want AL (the row index)
%ifdef KERN_BIG
    mov si, ax                      ; ...and the row is banked for the same
                                    ; reason, one register further along: it
                                    ; is this control's ID (SPEC.md 13.8.3)
%endif
    mov cx, di
    add cx, CP_PGX
    mov ax, OS88UI_GCHECK           ; AH = 0: a driver row never greys - what
    cmp word [bx+DRVR_SEG], 0       ; it cannot do, its caption says
    je .off
    mov al, OS88UI_GCHECK | OS88UI_GON
.off:
%ifdef KERN_BIG
    push bx                     ; one id per driver ROW, 1-based, out of the
    mov bx, si                  ; bank above (SI's low byte is the row)
    mov bh, bl
    inc bh
    call cp_dngly
    pop bx
%endif
    call cp_gemit
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_boxes - every driver row's checkbox (the page's own paint)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
; -----------------------------------------------------------------------------
cp_drv_boxes:
    push ax
    push bx
    mov bl, [cp_dtop]
    xor bh, bh
.row:
    mov al, bl
    call cp_drv_box1
    inc bx
    cmp bx, DRV_MAX
    jae .out
    mov al, [cp_dtop]
    add al, CP_DVIS
    xor ah, ah
    cmp bx, ax
    jb .row
.out:
    pop bx
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cp_drv_ctl1 - redraw the ONE control the down state moved to or from
; in:  AL = a control id on this page (0 = none), DI/BP = pane origin; lock held
; out: nothing (all registers preserved)
;
; The Drivers page's half of cp_ctlpaint (SPEC.md 31.1.2). Every other page
; redraws its whole control set on both edges of a press, which is affordable
; where a page has three or four of them and is not here: a row's checkbox is
; os88ui_glyph, ~35-50ms of the field machine's time, and it WHITE-FILLS its
; box before it draws a pixel - so redrawing the four that did not change is
; a visible blink of the whole list. Pressing an ARROW redrew all four and not
; the arrow, which is the reported defect in one sentence: two flashes of the
; list, and then the scroll.
;
; A row id can only ever be one that is on screen - cp_drv_click assigns them
; from the bands it hit, and cp_onup_x takes the down state OFF before
; anything acts, so [cp_dtop] cannot move under a held row.
; -----------------------------------------------------------------------------
cp_drv_ctl1:
    push ax
    or al, al
    jz .out                     ; nothing was down, or nothing is: the common
                                ; half of every edge
    cmp al, DRV_MAX
    ja .arrow
    dec al                      ; ids are 1-based: row 0 is id 1
    call cp_drv_box1
    jmp short .out
.arrow:
    sub al, CP_DIDUP            ; ...and the two above the rows are the arrows,
    call cp_drv_arrowx          ; in cp_drv_arrow1's own 0 = up / 1 = down
.out:
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; cp_drv_arrows - the two that move the window over drv_tab (SPEC.md 31.1.1)
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; GREYED AT THE ENDS, and that is SPEC.md 47 rule 3's `grey a FACT`: at the
; top of the list there is nothing above, which the panel knows for certain
; rather than having to try. Rule 1 is what makes it visible on the machine
; this OS is for - cw_gfx_pen_cf sets CDGRAY and [gfx_dis] together, so on a
; 1bpp adapter the arrow dithers instead of coming out solid black and
; identical to a live one.
; -----------------------------------------------------------------------------
cp_drv_arrows:
    push ax
    push bx
    push cx
    push dx
    mov al, 0                       ; the UP arrow, at the top of the column
    call cp_drv_arrowx
    mov al, 1                       ; ...and the DOWN one
    call cp_drv_arrowx
    mov byte [gfx_color], CBLACK
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cp_drv_arrowx - one arrow, greyed or not by its own end stop -----------
; in:  AL = 0 up / 1 down, DI/BP = pane origin; gfx lock held
; out: nothing (all registers preserved)
cp_drv_arrowx:
    call cp_drv_arrowok             ; CF straight into the pen (below)
    jmp cp_drv_arrow1

; --- cp_drv_arrowok - may arrow AL be used? (module internal) ---------------
; in:  AL = 0 up / 1 down
; out: CF = 0 live, CF = 1 disabled; every register preserved
;
; **CF = 0 IS LIVE AND CF = 1 IS DISABLED**, which is gfx_pen_cf's documented
; contract and the shape every greying predicate in the tree answers in
; (cp_snd_rowok two pages up: `.yes: clc`). This had the two backwards, under a
; comment asserting the opposite of that routine's own header - so the greying
; was exactly INVERTED: the up arrow dithered once you had scrolled DOWN (the
; one moment it is usable) and the down arrow dithered at the top. It reads as
; a scroll bar that greys the wrong end, and it was reported off a Hercules
; because that is where a dithered glyph is unmistakable - it was equally wrong
; on VGA, where CDGRAY against black is a subtler lie. SPEC.md 47's rule about
; looking at 1bpp, earning its keep on a control the rule's own author wrote.
;
; ONE PREDICATE FOR THE GREYING AND THE REFUSAL (SPEC.md 47 rule 2), which is
; what it was not: cp_drv_click carried its own copy of both end stops, so the
; drawn arrow and the click that arrow refuses answered two questions that
; only happened to agree.
cp_drv_arrowok:
    push ax
    push bx
    or al, al
    jnz .dn
    cmp byte [cp_dtop], 0
    je .no                          ; ...at the top: nothing above to scroll to
    jmp short .yes
.dn:
    mov al, [cp_dtop]
    add al, CP_DVIS
    xor ah, ah
    mov bx, ax
    cmp bx, DRV_MAX
    jae .no                         ; ...the last row is already shown
.yes:
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; --- cp_drv_arrow1 - AL = 0 up / 1 down, CF = 1 if it can be used -----------
; in:  DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved)
;
; THE DIRECTION IS CHOSEN BEFORE THE PEN IS SET, inside a pushf/popf, because
; cw_gfx_pen_cf reads CF and `or al, al` writes it. That is the whole reason
; this routine is shaped the way it is rather than testing AL where it reads.
cp_drv_arrow1:
    push ax
    push bx
    push cx
    push dx
    push si
    pushf                           ; ...CF is the ARGUMENT, and the branch
    mov si, cp_drv_triu             ; below spends it
    mov dx, CP_DBY1
    or al, al
    jz .have
    mov si, cp_drv_trid
    mov dx, CP_DBYN - CP_DSAH
.have:
%ifdef KERN_BIG
    mov byte [cp_darr_dn], 0        ; IS THIS THE ARROW BEING HELD? (SPEC.md
    push ax                         ; 13.8.3) - asked here, with CF still safe
    add al, CP_DIDUP                ; on the stack, because cp_isdown's compare
    call cp_isdown                  ; would spend it. The id IS the direction:
    jne .nodn                       ; up is CP_DIDUP and down the one above it
    mov byte [cp_darr_dn], 1
.nodn:
    pop ax                          ; (pop leaves the flags alone)
%endif
    add dx, bp
    mov bx, dx                      ; BX = y1
    add dx, CP_DSAH - 1             ; DX = y2
    mov ax, CP_DSX1
    add ax, di
    mov cx, CP_DSX2
    add cx, di
    call cp_drv_arrfill             ; ...the interior, BEFORE the pen is set
    popf
    call KERNEL_SEG:cw_gfx_pen_cf   ; the pen AND the disabled flag together
    call KERNEL_SEG:cw_gfx_frame
%ifdef KERN_BIG
    cmp byte [cp_darr_dn], 0
    je .tri
    mov byte [gfx_color], CWHITE    ; held: the triangle is the hole in the
.tri:                               ; black, the way os88ui_btn's label is
%endif
    call cp_drv_tri                 ; ...the rect is still in AX/BX/CX/DX: a
                                    ; kernel primitive preserves everything it
                                    ; does not document (SPEC.md 1)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cp_drv_arrfill - the cell's interior, so an arrow is IDEMPOTENT --------
; in:  AX/BX/CX/DX = the cell; gfx lock held
; out: nothing (all registers preserved; leaves the pen LIVE)
;
; The cell used to be a frame and a triangle drawn onto whatever was there,
; which is sound only while the only thing that ever draws it is a repaint of
; a pane that was just erased white. A DOWN state is not that: the black has
; to come off again at the release, and the erase is the only thing that takes
; it off.
;
; The pen is set here rather than inherited, and it is the LIVE one on both
; branches: a greyed arrow's dither belongs to its outline and its triangle,
; not to the paper behind them - inheriting cw_gfx_pen_cf's [gfx_dis] would
; dither the background itself.
cp_drv_arrfill:
    push ax
    push bx
    push cx
    push dx
    inc ax                          ; ...inside the frame, which is drawn after
    inc bx
    dec cx
    dec dx
    call KERNEL_SEG:cw_gfx_pen_live
%ifdef KERN_BIG
    cmp byte [cp_darr_dn], 0
    jne .go                         ; held: CBLACK, which pen_live just set
%endif
    mov byte [gfx_color], CWHITE    ; ...otherwise the pane's own background
.go:
    call KERNEL_SEG:cw_gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- cp_drv_tri - the triangle inside an arrow cell -------------------------
; in:  AX/BX/CX/DX = the cell, SI = a four-byte half-width table; pen already
;      set by the caller
;
; Four scan lines drawn as FILLS, because a gfx_fill with y1 = y2 IS an hline
; and one primitive is one thing to keep right. The half-widths come from a
; table rather than from arithmetic so that up and down are one loop: the
; alternative is two loops differing by a sign, which is two places to get an
; off-by-one into.
cp_drv_tri:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax
    add di, CP_DSW / 2              ; DI = the cell's centre column
    add bx, 4                       ; ...and the triangle's first row
    mov byte [cp_dtri_n], 0
.row:
    xor ax, ax
    mov al, [si]
    mov cx, di
    add cx, ax                      ; CX = x2
    mov dx, di
    sub dx, ax
    mov ax, dx                      ; AX = x1
    mov dx, bx                      ; DX = y2 = y1
    call KERNEL_SEG:cw_gfx_fill
    inc si
    inc bx
    inc byte [cp_dtri_n]
    cmp byte [cp_dtri_n], 4
    jb .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .text                   ; DATA, so out of the on-demand image and back
                                ; to the kernel segment (SPEC.md 2.8: a
                                ; module's data stays in .text, and
                                ; os88ovlchk.py refuses it here otherwise -
                                ; which is what caught this one)
cp_drv_triu: db 0, 1, 2, 3      ; up: a point at the top, widening downward
cp_drv_trid: db 3, 2, 1, 0      ; down: the same triangle upside down
cp_darr_dn:  db 0               ; is the arrow being drawn the one HELD? In
                                ; memory for cp_dtri_n's reason below, and
                                ; because CF is already this routine's
                                ; argument and AL its direction
; --- the memory column's scratch (SPEC.md 31.6.2) ---------------------------
; In memory rather than in registers for cp_dtri_n's reason: the panel runs on
; the UI task under the gfx lock, so a scratch byte here has exactly one user
; at a time - and cp_drv_memstr has a divide, which spends DX:AX, and a digit
; stack, which spends CX.
cp_dmbuf:    times CP_DMMAX db 0 ; the figure as text, built per row
cp_dmkc:     db 0               ; ...cells in it up to AND INCLUDING the 'K',
                                ; which is what cp_drv_mem1 aligns on
cp_dmplus:   db 0               ; DRVM_PLUS, parked out of AX for the divide
cp_dmpar:    db 1               ; 1 = there is room for the brackets, decided
                                ; once per paint by cp_drv_mempar and the same
                                ; for every row on the page
cp_dmnw:     db 0               ; its two working maxima, in 8px cells: the
cp_dmdg:     db 1               ; longest name in drv_tab, and the most digits
                                ; any drv_memk figure needs

section .bss                    ; ...and the one piece of ctrl.inc's data with
                                ; no resting value: a scratch buffer belongs
                                ; in .bss, where it costs the kernel image on
                                ; disk nothing at all
cp_sbuf:     resb CP_SBUF       ; cp_stage's landing ground - ONE string at a
                                ; time. Every site stages and then draws
                                ; before the next stage, and the panel runs on
                                ; the UI task under the gfx lock, so it has
                                ; exactly one user at a time (cp_dmbuf below
                                ; is the same argument for the Drivers page's
                                ; figure, and the two are never live together
                                ; only because that page has no staged string)
section .text

cp_dtri_n:   db 0               ; the row counter. In memory because every
                                ; general register is spoken for - DI is the
                                ; centre, SI the table, BX the row, and the
                                ; fill eats AX/CX/DX - and the panel runs on
                                ; the UI task under the gfx lock, so a scratch
                                ; byte here has exactly one user at a time
section .modc                   ; ...and back to the image

; --- cp_drv_pane - repaint the whole Drivers pane, after a SCROLL -----------
; in:  DI = PANE left, BP = pane top; gfx lock held
;
; A scroll moves every row, so this is the one gesture on this page that is
; honestly a whole-pane repaint - everything else here redraws the one control
; it changed (cp_drv_box1/cp_drv_line1), because a redraw of a control that
; did not move is PERFORMANCE.md's double-draw flash.
;
; cp_page wants the CONTENT origin, one pane back - the same adjustment
; cp_drv_click already makes to reach cp_list.
cp_drv_pane:
    push di
    sub di, CP_RX
    call cp_page
    pop di
    ret

; -----------------------------------------------------------------------------
; cp_drv_click - a click inside the Drivers page (SPEC.md 51.3)
;
; in:  DI = pane left, BP = pane top, CX = pane-relative x (ignored),
;      DX = pane-relative y; gfx lock held by the caller
; out: nothing
; clobbers: nothing (flags only)
;
; The click LOADS OR UNLOADS ON THE SPOT, so the page never shows a promise
; about the next boot. It does NOT write SYSTEM.CFG - it marks [cp_wdirty]
; like every other page and the close spends it (SPEC.md 31.8). This page is
; the one that most looks like it should be the exception, and is not: the
; driver is already up or already down by the time the box is redrawn, and
; what waits is only the record of having asked for it.
;
; A load that finds no disk or no card leaves the box clear with the reason
; under it. A settings write that cannot reach the disk says so in this
; page's caption - the next time the panel is opened, because by then this
; page is gone - while the driver it loaded stays loaded, which is the honest
; split: the two are genuinely separate outcomes.
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

    cmp cx, CP_DSX1             ; ...THE ARROWS FIRST, because they share the
    jb .rows                    ; rows' y bands and only x separates them
    cmp cx, CP_DSX2
    ja .rows
    cmp dx, CP_DBY1 + CP_DSAH
    jb .up
    cmp dx, CP_DBYN - CP_DSAH
    jae .down
.donej:                         ; ...and the three refusals below: an 8086 has
    jmp .done                   ; between the two: there is no track to click
.up:
    xor al, al                  ; ...in cp_drv_arrow1's own 0 = up / 1 = down,
    jmp short .arrow            ; so the refusal below and the greying are one
.down:                          ; question asked once (SPEC.md 47 rule 2)
    mov al, 1
.arrow:
    call cp_drv_arrowok
    jc .donej                   ; at that end: the arrow is already drawn
                                ; dithered and says so, so a refused click on
                                ; it says nothing more (SPEC.md 47 rule 5).
                                ; **AHEAD of cp_ctl, deliberately**: a control
                                ; that will refuse must not arm, or the press
                                ; draws it held and SPEC.md 13.8's "disabled
                                ; outranks down" is broken by this page
%ifdef KERN_BIG
    add al, CP_DIDUP            ; **THEY ARE CONTROLS, AND THIS LADDER RUNS
    call cp_ctl                 ; TWICE** (SPEC.md 13.8.3): once at the press
    jc .donej                   ; to IDENTIFY and once at the release to act.
    sub al, CP_DIDUP            ; Without an id the scroll happened on both
%endif                          ; passes and one click moved the list by two -
                                ; which is what a merge does to a branch that
                                ; grew a scrolling list while the trunk grew
                                ; press-and-release
    or al, al
    jz .scrup
    inc byte [cp_dtop]
    jmp short .scrolled
.scrup:
    dec byte [cp_dtop]
.scrolled:
    call cp_drv_pane            ; the whole pane: every row moved
    jmp .done

.rows:
    mov ax, dx                  ; which band, by division rather than by a
    sub ax, CP_DBY1             ; constant per row: DRV_MAX has changed twice
    js .donej                   ; and a hand-written ladder does not follow it
    xor dx, dx                  ; (DX is banked on the stack above)
    mov bx, CP_DROWH
    div bx                      ; AX = the visible row, DX = where in it
    cmp ax, CP_DVIS
    jae .done                   ; past the last band the pane shows
    add al, [cp_dtop]           ; ...and THEN into the table (SPEC.md 31.1.1)
    xor ah, ah
    cmp ax, DRV_MAX
    jae .done
    push ax
    call COLD_SEG:drvf_drv_row
    cmp word [bx+DRVR_FILE], 0  ; an empty table row is not a control
    pop ax
    je .done
%ifdef KERN_BIG
    push ax                     ; one id per driver ROW (SPEC.md 13.8.3)
    inc al
    call cp_ctl
    pop ax
    jc .done
%endif

    push ax
    call COLD_SEG:drvf_drv_row
    cmp word [bx+DRVR_SEG], 0
    pop ax
    je .load
    call COLD_SEG:drvf_drv_unload             ; it is up: take it down
    push ax
    call COLD_SEG:drvf_drv_row
    mov byte [bx+DRVR_WANT], 0
    mov byte [bx+DRVR_ERR], DRVE_OK ; nothing failed - it was turned off
    pop ax
    jmp short .save
.load:
    push ax
    call COLD_SEG:drvf_drv_row
    cmp byte [bx+DRVR_WANT], 0  ; unloaded but WANTED: the boot tried and
    je .try                     ; failed (no card, no disk), and on a machine
    mov byte [bx+DRVR_WANT], 0  ; where that is permanent this click is the
    mov byte [bx+DRVR_ERR], DRVE_OK ; only gesture there is for "stop asking"
    pop ax                      ; - without it the boot notice could not be
    jmp short .save             ; silenced at all, because retrying re-set
                                ; the want on every click. Clicking AGAIN
                                ; from here retries the load, so nothing
                                ; was lost, only unnagged
.try:
    mov byte [bx+DRVR_WANT], 1  ; wanted first, so a failed load still
    pop ax                      ; persists the intent: put the disk in and
    call COLD_SEG:drvf_drv_load               ; the next boot picks it up
.save:
    mov byte [cp_wdirty], 1     ; ...and the settings file is OWED, not
                                ; written: the close pays it (SPEC.md 31.8)
    push di                     ; a driver that came up or went down may own
    push bp                     ; a PAGE, so the item list changed too and
    sub di, CP_RX               ; not just this pane (SPEC.md 31.9). cp_list
    call cp_list                ; wants the CONTENT origin, one pane back
    pop bp
    pop di
.redraw:                        ; AL is still the clicked row - drv_load,
                                ; drv_unload, drv_row and cp_list all preserve
                                ; AX - and one row is all that changed: a load
                                ; or an unload touches its own drv_tab row and
                                ; no other, so redrawing the rest is a
                                ; double-draw flash on controls that did not
                                ; move. The save caption is not redrawn either:
                                ; only cp_flush_x writes [cp_dsave] and that
                                ; runs at the CLOSE, so it cannot change while
                                ; this page is on screen
    call cp_drv_box1
    call cp_drv_line1
.done:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_line1 - redraw the reason under one driver row
; in:  AL = row index, DI = pane left, BP = pane top; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; The strings differ in length, so the line is erased before it is drawn - the
; cpf_buf_cap idiom - and the erase is where this page's one drawing bug
; lived. The line's first row IS the checkbox's last (CP_DSTDY = CP_GW - 1),
; so a wipe starting at the pane margin runs straight through the box and
; takes its bottom edge off. It starts at CP_PLX, the column the text itself
; starts at, which is both the correct span and a narrower fill: nothing left
; of CP_PLX on this row belongs to the reason line.
;
; cp_drv_paint got away with it because it letters the reasons FIRST and draws
; the boxes last; the click path did the opposite, so every click ended with
; every box on the page missing its bottom line - the clicked row's and, since
; both painters walked all of drv_tab, the untouched row's too.
; -----------------------------------------------------------------------------
cp_drv_line1:
    push ax
    push bx
    push cx
    push dx
    push si
    call COLD_SEG:drvf_drv_row      ; AL survives both of these
    cmp word [bx+DRVR_FILE], 0
    je .out
    call COLD_SEG:drv_status_x      ; SI = what it is doing right now
    call cp_drv_rowy                ; DX = this row's glyph top
    add dx, CP_DSTDY
    mov ax, CP_PLX                  ; ...and NOT CP_PMX: see above
    call cp_drv_wipe
    mov cx, di
    add cx, CP_PLX
    call cp_run
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_drv_wipe - white out one text line of the pane (module internal)
; in:  DI = pane left, AX = the line's LEFT inset from the pane, DX = the
;      line's baseline y; gfx lock held
; out: nothing (all registers preserved; leaves [gfx_color] = CBLACK)
;
; The inset is an argument rather than CP_PMX because a line does not always
; own the pane's full width: a driver row's reason line shares a scan line
; with that row's checkbox and must not erase into it (cp_drv_line1). A
; caption, which has nothing to its left, passes CP_PMX.
; -----------------------------------------------------------------------------
cp_drv_wipe:
    push ax
    push bx
    push cx
    push dx
    mov byte [gfx_color], CWHITE
    add ax, di
    mov bx, dx
    mov cx, di
    add cx, CP_PRGT             ; the PANE's last column, not the content's
    add dx, 7
    call KERNEL_SEG:cw_gfx_fill
    mov byte [gfx_color], CBLACK
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; THE PAGE STRINGS, IN THE IMAGE (SPEC.md 2.8.6, 31.9)
; =============================================================================
; 443 bytes of page-body text that used to sit in `.text` and be resident for
; the life of the machine so that a panel nobody had opened could draw itself.
; They are here now, in CTRL.DRV's own image, which is claimed when the panel
; opens and given back when it closes.
;
; **What did NOT move, and why it is the line the trap is on.** SPEC.md 31.9's
; `DSV_CPNAME` trap is about the ITEM LIST: a row's name may be a static kernel
; string OR a driver's staged one, `cp_list` draws it with `font_str`, and
; `font_str` reads through DS - so a pointer into a driver's segment would
; render the driver's own image. All six list names therefore stay resident
; (`cp_s_thm`, `cp_s_sched`, `cp_s_vid`, `cp_s_drv`, `cp_s_snd`, `cp_s_time`),
; and each of them is ALSO its page's heading, which is why they read as page
; strings and are not. What moved is page BODY text, whose only readers are
; the `cp_*_paint` bodies and their click ladders - every one of them in this
; image, entered only through `call far [CPFP+...]` after `mod_need` succeeded.
;
; The module-cannot-load path reads NONE of these: `cp_open_x` is `.cold` and
; letters `cp_s_noload` / `cp_s_nodrv` out of `cp_opentab`, all of which stay
; in `.text`. A refusal costs no module read and reads no moved string.
;
; **Every reader goes through CPSTAGE/CPSTAGEX and `tools/os88ovlchk.py`
; refuses one that does not** - see the composer below. Do not write a bare
; `mov si, cp_s_*` here; it assembles, it draws rubbish, and the gate is the
; only thing between the two.
; -----------------------------------------------------------------------------
%ifdef OS88_THEME
CPS cp_s_tbrt,  'Bright'
CPS cp_s_tdrk,  'Dark'
%ifdef KERN_BIG
CPS cp_s_tclr,  'Color'         ; ...and the third, VGA's alone (SPEC.md 76.12)
%endif
%ifdef WM_ANIM
CPS cp_s_tanim, 'Window Animations'
CPS cp_s_ton,   'On'
CPS cp_s_toff,  'Off'
%endif
%ifdef KERN_BIG
CPS cp_s_tsave, 'Screen Saver'  ; the button, SPEC.md 79.7. The settings
                                ; window's own eleven strings are in
                                ; SAVER.DRV, which is a segment of its own and
                                ; is on the disk (SPEC.md 79.2) - this is now
                                ; the only byte the feature costs the panel,
                                ; and it is not resident either
%endif
%endif

CPS cp_s_pre,   'Pre-emptive'
CPS cp_s_coop,  'Cooperative'
CPS cp_s_cap,   'Takes effect immediately'

CPS cp_s_vvga,  'Vga 640x480'
CPS cp_s_vherc, 'Hercules 720x348'
CPS cp_s_vcga,  'Cga 640x200'
CPS cp_s_vega,  'Ega 640x350'
; FOUR ROWS SINCE SPEC.md 39.24, and the table is indexed by the VID_* enum -
; so this row is EGA's because it is fourth, not because of its name. It lives
; here rather than beside cp_s_vid because SPEC.md 51.5.3 moved the page's
; strings into CTRL.DRV's image; adding it up there would put 14 bytes back
; into every machine's resident .text.
cp_vidnam:  dw cp_s_vvga, cp_s_vherc, cp_s_vcga, cp_s_vega
CPS cp_s_vact,  'Set Primary'
; The Display caption, inside the pane's 27-character limit. It is ONE string
; and the page's only constant line: it says what the button does, on every
; state the page can be in. It had three answers once - 'No other adapter
; found' went when SPEC.md 31.10.1 stopped drawing the page at all on a machine
; with one adapter, and 'This adapter is running' - SPEC.md 47 rule 7's
; say-why-not for the greyed button - went at SPEC.md 31.10.3, where the room
; it was holding paid for the desktop line's own label. So nothing here follows
; the selection: cp_vid_click no longer redraws it, and it is drawn once,
; inline in cp_vid_paint, on ground cp_page has already erased.
CPS cp_s_vcap,  'Switches now, and is kept'

%ifdef KERN_BIG
CPS cp_s_vdesk, 'Desktop Extension Mode:'
CPV_DESKN   equ $ - cp_s_vdesk - 1   ; ...measured rather than counted, so the
                                     ; guard below cannot go stale behind an
                                     ; edit to the string above it. Both moved
                                     ; WITH the string: measured from `.text`
                                     ; this would have subtracted two
                                     ; different address spaces, and the guard
                                     ; has to follow what it measures
%if CP_PMX + CPV_DESKN*8 > CP_PRGT
%error "ctrl: the Display page's desktop label runs past the pane (31.10.3)"
%endif
CPS cp_s_vsing, 'Single'
CPS cp_s_vrgt,  'Right'
CPS cp_s_vblw,  'Below'
cp_vmodnam: dw cp_s_vsing, cp_s_vrgt, cp_s_vblw
%endif

CPS cp_s_sspk,  'PC Speaker'
CPS cp_s_sfm,   'AdLib'
CPS cp_s_ssb,   'Sound Blaster'
CPS cp_s_stest, 'Test'

; The Date/Time page names the RTC ladder tier that answered (SPEC.md 37.90),
; not just yes/no: on a machine whose clock will not hold a setting, which of
; the four answered is the whole diagnosis, and there is nowhere else to see
; it. **Indexed by [clk_tier], i.e. by the CLK_T_* enum - NOT by the order
; clk_probe tries them in, which is a different order.**
CPS cp_s_tnrt,  'Hardware clock: none'
CPS cp_s_trt1,  'Hardware clock: AT 70h'
CPS cp_s_trt2,  'Hardware clock: 58167 2C0h'
CPS cp_s_trt3,  'Hardware clock: 5C01 2C0h'
CPS cp_s_trt4,  'Hardware clock: BIOS'
cp_rtcnam:  dw cp_s_tnrt, cp_s_trt1, cp_s_trt2, cp_s_trt3, cp_s_trt4
CPS cp_s_t12,   '12-hour clock'         ; option row 0 label
CPS cp_s_tsec,  'Seconds in menu bar'   ; option row 1 label
CPS cp_s_tplus, '+'
CPS cp_s_tminus, '-'
CPS cp_s_tcol,  ':'

; -----------------------------------------------------------------------------
; cp_stage - copy one of the strings above down into the KERNEL segment
; in:  SI -> a NUL string in THIS IMAGE (CS)
; out: SI = cp_sbuf, holding a copy of it
; clobbers: nothing else (flags)
;
; `clo_cat`'s idea (SPEC.md 2.8.6) and deliberately NOT `clo_cat`'s SHAPE.
; clo_cat concatenates into a caller-supplied DI and leaves DI advanced; in
; this file **DI is the pane's left edge at every one of the call sites**
; (cp_paint dispatches DI = pane left, BP = pane top), so a composer written
; to that shape would destroy the pane origin and draw every page in the wrong
; place - assembling cleanly and passing every gate. AX and DI are banked here
; for exactly that reason, and CX because the copy is BOUNDED.
;
; The bound is the second half of the buffer's guard: CPS refuses a string
; that will not fit at assembly time, and this refuses to write past the end
; of cp_sbuf at run time even for a string CPS never saw. A bypass truncates a
; caption; it does not corrupt the kernel.
;
; `lodsb` is not used and cannot be: it reads DS:SI, and DS is KERNEL_SEG here.
; os88ovlchk.py refuses it in a module image for that reason.
; -----------------------------------------------------------------------------
cp_stage:
    push ax
    push cx
    push di
    mov di, cp_sbuf
    mov cx, CP_SBUF - 1         ; ...so the terminator below always fits
.c:
    mov al, [cs:si]
    inc si
    mov [di], al                ; DS = KERNEL_SEG (SPEC.md 2.8), which is where
    inc di                      ; cp_sbuf is
    or al, al
    jz .z
    loop .c
    mov byte [di], 0            ; the bound was hit: terminate what was copied
.z:
    mov si, cp_sbuf
    pop di
    pop cx
    pop ax
    ret


section .cold                   ; far entries: the resident thunks in
                                ; kernel.asm reach the bodies above through
                                ; these, which is what lets every body keep
                                ; the near `ret` it always had
;
; **Four of the six now LOAD before they dispatch (SPEC.md 2.8.1).** The
; kernel.asm side is untouched - `cp_paint: call COLD_SEG:cpf_cp_paint` is the
; same five bytes it always was - which is what keeps this mechanism off
; `.text`, the guard nothing can raise. What changed is here, where the body
; used to be one segment away and is now none or one, depending on whether the
; user has opened the panel yet.
;
; The shape is the same two lines each time and it is worth reading once:
;
;   call cpf_need      - "make MOD_CTRL resident", CF = 1 if it could not be.
;                        It banks AX because the caller's AL/AH may be
;                        arguments (cp_tick_x takes none, cp_onclick_x takes
;                        CX/BX, and the next module to use this will take
;                        something else); mod_need preserves every other
;                        register itself, and `pop` does not touch CF, so
;                        mod_need's answer survives the unbanking.
;   jc  .gone
;   call far [mod_fp + K]
;
; K is an assembly-time constant, so the dispatch takes no register and needs
; no scratch word two callers could race for. `.gone` is a refusal and the
; CALLER owes the user an explanation - ui_cp_open puts up the toast for the
; three launch paths, which is the only place a human is waiting on one.
;
; cpf_need - make the Control Panel image resident. Out: CF = 1 it is not.
; A NEAR call, and so is the mod_need inside it: the loader is .cold too, so
; this whole mechanism reaches nothing through .text.
cpf_need:
    push ax
    mov al, MOD_CTRL
    call mod_need
    pop ax
    ret

cpf_cp_paint:
    call cpf_need
    jc .p_gone
    call far [CPFP + CPE_PAINT*4]
.p_gone:                        ; the window is up and its content cannot be
    retf                        ; drawn. ui_cp_open makes this unreachable in
                                ; practice by loading BEFORE app_launch; it is
                                ; here because "unreachable" is a claim about
                                ; today's callers

; cp_onkey is a plain thunk: it is short, it runs only while the panel is on
; screen (so the image is already in), and a mod_need here would be a floppy
; read on a keystroke.
%ifdef KERN_BIG
cpf_cp_onkey:          call cp_onkey_x
                    retf
%endif

cpf_cp_onclick:
    call cpf_need
    jc .c_gone
    call far [CPFP + CPE_ONCLICK*4]
.c_gone:
    retf

; SPEC.md 13.8.3's two edges. They do NOT call mod_need, and that is the
; difference from every shim above: a release or a drag can only follow a
; PRESS this panel took, and taking it is what loaded the image - so if the
; module is gone the arm is stale and there is nothing to fire. Loading the
; Control Panel off a floppy to answer a mouse movement would be absurd, and
; mod_need on the drag path would do it eighteen times a second.
cpf_cp_onup:
    push ax
    mov al, MOD_CTRL
    call mod_live               ; already resident? (no disk, no claim)
    pop ax
    jc .u_gone
    call far [CPFP + CPE_ONUP*4]
.u_gone:
    retf
%ifdef KERN_BIG
cpf_cp_ondrag:
    push ax
    mov al, MOD_CTRL
    call mod_live
    pop ax
    jc .d_gone
    call far [CPFP + CPE_ONDRAG*4]
.d_gone:
    retf
%endif

; cp_flush_close is the panel's TEARDOWN (SPEC.md 31.8), so it is also where
; the image is handed back - after the call, because cp_flush_close_x runs
; inside it. Two things make that the right and only place. It is reached on
; BOTH ways the panel stops existing (app_close_win and ui_cmd_reboot), and it
; is gated on [cp_wdirty] here rather than inside, so a machine that never
; opened the panel never loads the module to be told there is nothing to
; write - which is what ui_cmd_reboot would otherwise do on every warm boot.
cpf_cp_flush_close:
    cmp byte [cp_wdirty], 0
    je .f_drop                  ; nothing to persist - but still drop, because
                                ; closing a panel that changed nothing is the
                                ; commonest close there is
    call cpf_need
    jc .f_drop                  ; the disk went away between opening the panel
                                ; and closing it: [cp_wdirty] stays set, which
                                ; is exactly what cp_flush_close_x's own
                                ; failure path does (SPEC.md 59.6)
    call far [CPFP + CPE_FLUSH*4]
.f_drop:
    push ax
    mov al, MOD_CTRL
    call mod_drop
    pop ax
    retf

; cp_tick is called from ui_task whenever the menu bar's clock TEXT changes -
; once a minute, panel or no panel - and its body is a no-op unless a
; Date/Time page is showing. In the image that no-op costs a floppy read, so
; the resident cp_tick_due_x runs FIRST and is the whole gate: it asks the two
; questions cp_tick_x's own preamble asks ([cp_sel] and a live visible panel)
; out of resident data, and answers CF = 1 only when there is something to
; draw. Without it the machine loaded CTRL.DRV once a minute for ever.
cpf_cp_tick:
    call cp_tick_due_x
    jnc .t_gone
    call cpf_need
    jc .t_gone
    call far [CPFP + CPE_TICK*4]
.t_gone:
    retf

; ...and the one that never leaves: cp_tick_due runs once a second forever, so
; it is a resident body with the plain thunk it always had. cp_drv_gone was
; the second of a pair here and its far entry had no caller - the body is
; `.cold` and so is drv_release, its only caller, which reaches it near.
cpf_cp_tick_due:       call cp_tick_due_x
                    retf
cpf_cp_open:           call cp_open_x
                    retf

; -----------------------------------------------------------------------------
; cp_open_x - open the Control Panel, or say why not (SPEC.md 2.8.4)
; in:  nothing ([cp_sel] set by the caller if it wants a particular page)
; out: CF = 0 the panel is up; CF = 1 it is not, and the user has been told
; clobbers: flags
;
; **The load happens HERE, before app_launch, and that is the whole of the
; failure design.** Left to cpf_cp_paint, a machine with the wrong disk in the
; drive would open an empty window - a Control Panel with no Control Panel in
; it - and the user would have nothing to act on. Loading first means the
; window is never created, and the refusal is a sentence in the menu bar that
; names the disk (SPEC.md 59): subject, outcome, cause.
;
; It is the only place a human is ever waiting on mod_need, because the panel
; stays loaded from here until cp_flush_close - so every other cp thunk finds
; the image already there and its mod_need is six compares.
; -----------------------------------------------------------------------------
cp_open_x:
    push ax                     ; the bank is the BODY's, not the load's:
    call cpf_need               ; cpf_need banks AX itself, but `mov al,
    jc .no                      ; KIND_CTRL` below and `mov al, 1` at .no do not
    mov al, KIND_CTRL
    call KERNEL_SEG:cw_app_launch
    jc .beep                    ; the panel itself refused - no instance slot,
                                ; no window. app_launch's own answer, and it
                                ; is not about the disk, so it keeps the beep
                                ; the three call sites used to give it
    pop ax
    clc
    ret
.beep:
    call KERNEL_SEG:cw_snd_beep ; audible refusal (SPEC.md 34.3), which is
    pop ax                      ; what every one of these call sites did with
    stc                         ; a CF=1 from app_launch before cp_open owned
    ret                         ; the decision
.no:
    push bx                     ; ...but THIS one is about the disk, and a
    push cx                     ; beep cannot say which disk
    mov al, 1
    mov bx, cp_opentab
    mov cx, CP_NOPEN
    call KERNEL_SEG:toast_say
    pop cx
    pop bx
    pop ax
    stc
    ret

section .text
