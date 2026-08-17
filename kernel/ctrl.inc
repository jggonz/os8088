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

; --- radio hit bands, pane-relative y: generous, contiguous ------------------
CP_B0Y1   equ CP_PR0Y - 4                     ; 22: row 0 (Pre-emptive) top
CP_B0Y2   equ CP_PR0Y + CP_GW + 3             ; 41: row 0 bottom = row 1 top-1
CP_B1Y2   equ CP_PR0Y + CP_PROWH + CP_GW + 3  ; 61: row 1 (Cooperative) bottom
section .text


; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
; No onkey: the panel is mouse-only. Content is CP_CW x CP_CH (318x132).
cp_tpl:     dw 160, 130, 320, 151, cp_ttl, cp_paint, 0, cp_onclick
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
    dw cp_s_drv,   cp_drv_paint,   cp_drv_click,   0
    dw cp_s_snd,   cp_snd_paint,   cp_snd_click,   0
    dw cp_s_vid,   cp_vid_paint,   cp_vid_click,   0
cp_items_end:
CP_ITEMS  equ (cp_items_end - cp_items) / CP_ISTRIDE
CP_ITIME  equ 1                 ; the Date/Time item: ui.inc selects it by
                                ; name when the menu bar clock is clicked
                                ; (SPEC.md 12.1/31.5)
CP_IDRV   equ 2                 ; ...and the Drivers item, which drv_notice
                                ; opens the panel on when a driver did not
                                ; load at boot (SPEC.md 51.3)
CP_ISND   equ 3                 ; ...and Sound, the tone tier's route (34.8)
CP_IVID   equ 4                 ; ...and Display, the one row that is HIDDEN
                                ; on a machine with a single adapter (SPEC.md
                                ; 39.11.1). vid_probe_avail names it by this
                                ; rather than by "the last one", which is what
                                ; frees the order above

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

cp_s_sched: db 'Scheduler', 0   ; list name AND the page heading
cp_s_pre:   db 'Pre-emptive', 0
cp_s_coop:  db 'Cooperative', 0
cp_s_cap:   db 'Takes effect immediately', 0

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
cp_s_vid:   db 'Display', 0     ; list name AND the page heading
cp_s_vvga:  db 'Vga 640x480', 0
cp_s_vherc: db 'Hercules 720x348', 0
cp_s_vcga:  db 'Cga 640x200', 0
cp_vidnam:  dw cp_s_vvga, cp_s_vherc, cp_s_vcga
cp_s_vact:  db 'Set Primary', 0
; The caption, inside the pane's 27-character limit. It is ONE string and the
; page's only constant line: it says what the button does, on every state the
; page can be in. It had three answers once - 'No other adapter found' went
; when SPEC.md 31.10.1 stopped drawing the page at all on a machine with one
; adapter, and 'This adapter is running' - SPEC.md 47 rule 7's say-why-not for
; the greyed button - went at SPEC.md 31.10.3, where the room it was holding
; paid for the desktop line's own label. So nothing here follows the
; selection: cp_vid_click no longer redraws it, and it is drawn once, inline
; in cp_vid_paint, on ground cp_page has already erased.
cp_s_vcap:  db 'Switches now, and is kept', 0

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
cp_s_vdesk: db 'Desktop Extension Mode:', 0
CPV_DESKN   equ $ - cp_s_vdesk - 1   ; ...measured rather than counted, so the
                                     ; %if guard below cannot go stale behind
                                     ; an edit to the string above it
cp_s_vsing: db 'Single', 0
cp_s_vrgt:  db 'Right', 0
cp_s_vblw:  db 'Below', 0
cp_vmodnam: dw cp_s_vsing, cp_s_vrgt, cp_s_vblw
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
%ifdef KERN_BIG
%if CP_PMX + CPV_DESKN*8 > CP_PRGT
%error "ctrl: the Display page's desktop label runs past the pane (31.10.3)"
%endif
%endif
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
; same reason SAVING it can. This one keeps its subject and spends the outcome
; instead, because there is nothing on screen to infer the subject FROM - the
; panel never opened. It says the DISK and the DRIVE both, which is what the
; 24-cell field will just carry: the disk because that is what is wanted, and
; the letter because mod_need looks on exactly one volume - [dsk_bootvol] -
; and telling the user which one is the difference between a message they can
; act on and one they have to guess at.
cp_s_noload: db 'Panel Needs Sys Disk '
cp_s_nodrv:  db 'A:', 0         ; stamped beside cp_s_dsdrv, same rule
cp_opentab: dw 0, cp_s_noload
CP_NOPEN  equ 2

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
CP_NENT     equ 4
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
    mov al, [cp_sel]
    call cp_item_click          ; in DI/BP = pane origin, CX/DX = relative -
                                ; or, for a driver's page, its own ABI
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
    call COLD_SEG:drvf_drv_cp_count
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

; (cp_drv_gone_x was here. It is RESIDENT now, above the .modc switch, because
; drv_shutdown calls it after cp_flush_close has dropped this image - SPEC.md
; 2.8. It kept its behaviour and lost its cp_drv_item call, which would have
; been a near call across the boundary.)

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
    jmp .text
.plain:
    mov byte [gfx_color], CBLACK
.text:
    mov al, bh
    call cp_item_name           ; SI -> it, static or staged (SPEC.md 31.9)
    mov cx, di
    add cx, CP_IX
    push dx
    add dx, CP_ITDY
    call KERNEL_SEG:cw_font_str
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
    call KERNEL_SEG:cw_font_str

    mov si, cp_s_pre            ; row 0 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PLDY
    call KERNEL_SEG:cw_font_str

    mov si, cp_s_coop           ; row 1 label
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CP_PR0Y + CP_PROWH + CP_PLDY
    call KERNEL_SEG:cw_font_str

    mov si, cp_s_cap            ; caption: the flip is immediate
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CP_PCAPY
    call KERNEL_SEG:cw_font_str

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
    call KERNEL_SEG:cw_sched_mode_get         ; AL = the live mode
    cmp al, ah
    je .done                    ; that row is already live: change nothing
    mov al, ah
    call KERNEL_SEG:cw_sched_mode_set         ; live on the next tick (SPEC.md 8.2)
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
; mode change: os88ui_glyph white-fills its own 12x12 box first, which is what
; lets cp_sched_click repaint in place without touching the rest of the page.
; -----------------------------------------------------------------------------
cp_radios:
    push ax
    push bx
    push cx
    push dx
    push si

    call KERNEL_SEG:cw_sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
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
    call COLD_SEG:os88ui_glyph_f
    add dx, CP_PROWH            ; --- row 1: Cooperative ---
    mov ax, OS88UI_GRADIO
    cmp bl, 1
    jne .r1
    mov al, OS88UI_GRADIO | OS88UI_GON
.r1:
    call COLD_SEG:os88ui_glyph_f
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
; in:  AL = slot, 0..2 (the visible row, top to bottom)
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
    cmp bl, 3
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
    mov si, [si + cp_vidnam]
    mov byte [gfx_color], CBLACK
    mov cx, di
    add cx, CP_PLX
    push dx
    add dx, CP_PLDY
    call KERNEL_SEG:cw_font_str
    pop dx

    mov ax, OS88UI_GRADIO       ; --- the glyph (AH = 0: a listed adapter is
    cmp bh, [cp_vsel]           ; one the machine has, so no row refuses)
    jne .g
    mov al, OS88UI_GRADIO | OS88UI_GON
.g:
    mov cx, di
    add cx, CP_PGX
    call COLD_SEG:os88ui_glyph_f
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
    mov si, cp_s_vdesk              ; No erase: cp_page has already whitened
    mov cx, di                      ; the pane and nothing else ever draws
    add cx, CP_PMX                  ; here - the caption below is constant
    mov dx, bp                      ; too, so this page has no state line
    add dx, CPV_MLY
    call KERNEL_SEG:cw_font_str

    call cp_vid_msel
    mov bh, al                  ; BH = the radio that is on
    xor bl, bl                  ; BL = the radio being drawn
.r:
    mov al, bl                  ; --- SI = this radio's name, BEFORE AX becomes
    xor ah, ah                  ; the x offset: both want it
    shl ax, 1
    mov si, ax
    mov si, [si + cp_vmodnam]

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
    call KERNEL_SEG:cw_font_str
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
    call COLD_SEG:os88ui_glyph_f
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
    mov si, cp_s_vact
    mov bx, os88ui_krect
    call COLD_SEG:os88ui_btn_f
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
    call KERNEL_SEG:cw_font_str

    call cp_vid_rows
    call cp_vid_btn
%ifdef KERN_BIG
    call cp_vid_mode
%endif

    mov byte [gfx_color], CBLACK ; ...and the caption, LAST on the page as it
    mov si, cp_s_vcap            ; is on every other one
    mov cx, di
    add cx, CP_PMX
    mov dx, bp
    add dx, CPV_CAPY
    call KERNEL_SEG:cw_font_str

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
    jl .done
    cmp cx, CPV_BTNX + CPV_BTNW - 1
    jg .done
    call cpf_vidok              ; greyed: the dot is already on the running
    jc .done                    ; adapter, so a refused press says nothing
                                ; more (SPEC.md 47 rule 5)
    mov al, [cp_vsel]
    call KERNEL_SEG:cw_vid_switch
    jc .done                    ; refused: the adapter went away under us
    mov byte [cp_wdirty], 1     ; ...remembered on close (SPEC.md 31.8)
    mov byte [cp_dirty], 1      ; ...and the whole screen is a new shape
    jmp short .done

.mode:                          ; --- the desktop row (SPEC.md 31.10.2) ---
%ifdef KERN_BIG
    cmp dx, CPV_MB1
    jl .done
    cmp dx, CPV_MB2
    jg .done
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
    call KERNEL_SEG:cw_vid_disp_relay
    mov byte [cp_wdirty], 1
    mov byte [cp_dirty], 1
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
    call KERNEL_SEG:cw_font_str

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
    call KERNEL_SEG:cw_font_str
    mov si, cp_s_tsec
    mov cx, di
    add cx, CP_PLX
    mov dx, bp
    add dx, CPT_O1Y + CP_PLDY
    call KERNEL_SEG:cw_font_str
    call cp_timechk

    mov byte [gfx_color], CBLACK
    mov si, cp_s_tnrt           ; the caption: read live from the probe's
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
    call KERNEL_SEG:cw_font_str

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
    call KERNEL_SEG:cw_clk_fld_adj           ; in AL = field, BL = +1/-1 (SPEC.md 37)
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
    test word [bx+W_FLAGS], 2   ; visible?
    jz .out
    call KERNEL_SEG:cw_wm_obscured
    jc .out
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
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
    call KERNEL_SEG:cw_gfx_fill

    mov byte [gfx_color], CBLACK ; the time row's two ':' separators (the
    mov si, cp_s_tcol            ; date row's gaps are simply blank)
    mov cx, di
    add cx, CPT_FX + 16
    mov dx, bp
    add dx, CPT_TY
    call KERNEL_SEG:cw_font_str
    mov cx, di
    add cx, CPT_FX + 40
    call KERNEL_SEG:cw_font_str

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
    call KERNEL_SEG:cw_gfx_fill
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
    call KERNEL_SEG:cw_clk_fld_str            ; SI = that field's text (SPEC.md 37)
    call KERNEL_SEG:cw_font_str               ; in SI, CX = x, DX = y

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
    call COLD_SEG:os88ui_glyph_f
    mov dx, bp
    add dx, CPT_O1Y
    mov ax, OS88UI_GCHECK       ; --- row 1: seconds in the menu bar ---
    cmp byte [clk_secs], 0
    je .r1
    mov al, OS88UI_GCHECK | OS88UI_GON
.r1:
    call COLD_SEG:os88ui_glyph_f
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

    mov si, cp_s_tplus
    mov dx, CPT_BUY
    or al, al
    jz .have
    mov si, cp_s_tminus
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
    mov bx, os88ui_krect
    call COLD_SEG:os88ui_btn_f
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    call COLD_SEG:drvf_drv_cfg_save
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
    call KERNEL_SEG:cw_toast_say
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
    call KERNEL_SEG:cw_font_str

    xor bx, bx                      ; BX = row 0..2
.lbl:
    mov al, bl
    call cp_snd_rowok               ; greyed rows still draw - they are what
    call KERNEL_SEG:cw_gfx_pen_cf                 ; the machine does not have, and a missing
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
    call KERNEL_SEG:cw_font_str
    inc bl
    cmp bl, 3
    jb .lbl
    call KERNEL_SEG:cw_gfx_pen_live

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
    mov si, cp_s_stest
    mov bx, os88ui_krect
    push di                         ; DI is the pane's left edge everywhere in
    xor di, di                      ; this file and the flags word wants it -
                                    ; the one site that had to bank it. No
                                    ; fill: the page is painted once onto a
                                    ; pane cp_page has already whited, and
                                    ; nothing redraws this button in place
    call COLD_SEG:os88ui_btn_f
    pop di

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
    call COLD_SEG:os88ui_glyph_f ; ...which leaves the pen LIVE by itself, so
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
    call KERNEL_SEG:cw_font_str
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
                                ; has already said so
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
    call KERNEL_SEG:cw_font_str

    xor bx, bx                      ; BX = row index
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
    call KERNEL_SEG:cw_font_str                   ; the driver's name
    pop bx

    call COLD_SEG:drvf_drv_status                 ; ...and what it is doing right now
    pop ax
    push ax
    call cp_drv_rowy
    add dx, CP_DSTDY
    mov cx, di
    add cx, CP_PLX
    call KERNEL_SEG:cw_font_str                   ; no erase needed: cp_page has
                                                  ; just white-filled the pane
.next:
    pop bx
    inc bx
    cmp bx, DRV_MAX
    jb .row

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
; rows (CP_DB0Y1/CP_DB0Y2/CP_DB1Y2), so the third driver row (SPEC.md 58)
; drew, said what it was doing, and could not be clicked - a control that
; looks live and is not, which is SPEC.md 47 rule 4's failure arriving where
; rule 4 does not look because no predicate refuses anything. One band per
; row, one pitch tall, and cp_drv_click divides.
CP_DBY1   equ CP_DR0Y - 4                       ; the first band's top
CP_DBYN   equ CP_DBY1 + CP_DROWH * DRV_MAX      ; ...and one past the last


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
    mov cx, di
    add cx, CP_PGX
    mov ax, OS88UI_GCHECK           ; AH = 0: a driver row never greys - what
    cmp word [bx+DRVR_SEG], 0       ; it cannot do, its caption says
    je .off
    mov al, OS88UI_GCHECK | OS88UI_GON
.off:
    call COLD_SEG:os88ui_glyph_f
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
    xor bx, bx
.row:
    mov al, bl
    call cp_drv_box1
    inc bx
    cmp bx, DRV_MAX
    jb .row
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

    mov ax, dx                  ; which band, by division rather than by a
    sub ax, CP_DBY1             ; constant per row: DRV_MAX has changed twice
    js .done                    ; and a hand-written ladder does not follow it
    xor dx, dx                  ; (DX is banked on the stack above)
    mov bx, CP_DROWH
    div bx                      ; AX = the row, DX = where in it (unused)
    cmp ax, DRV_MAX
    jae .done
    push ax
    call COLD_SEG:drvf_drv_row
    cmp word [bx+DRVR_FILE], 0  ; an empty table row is not a control
    pop ax
    je .done

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
    call COLD_SEG:drvf_drv_status   ; SI = what it is doing right now
    call cp_drv_rowy                ; DX = this row's glyph top
    add dx, CP_DSTDY
    mov ax, CP_PLX                  ; ...and NOT CP_PMX: see above
    call cp_drv_wipe
    mov cx, di
    add cx, CP_PLX
    call KERNEL_SEG:cw_font_str
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
; The shape is the same three lines each time and it is worth reading once:
;
;   push ax            - the caller's AL/AH may be arguments (cp_tick_x takes
;                        none, cp_onclick_x takes CX/BX, and the next module
;                        to use this will take something else). mod_need
;                        preserves every other register itself.
;   call mod_need      - a NEAR call: the loader is .cold too, so this whole
;                        mechanism reaches nothing through .text
;   pop ax             - `pop` does not touch CF, so mod_need's answer stands
;   jc  .gone
;   call far [mod_fp + K]
;
; K is an assembly-time constant, so the dispatch takes no register and needs
; no scratch word two callers could race for. `.gone` is a refusal and the
; CALLER owes the user an explanation - ui_cp_open puts up the toast for the
; three launch paths, which is the only place a human is waiting on one.
cpf_cp_paint:
    push ax
    mov al, MOD_CTRL
    call mod_need
    pop ax
    jc .p_gone
    call far [CPFP + CPE_PAINT*4]
    retf
.p_gone:                        ; the window is up and its content cannot be
    retf                        ; drawn. ui_cp_open makes this unreachable in
                                ; practice by loading BEFORE app_launch; it is
                                ; here because "unreachable" is a claim about
                                ; today's callers

cpf_cp_onclick:
    push ax
    mov al, MOD_CTRL
    call mod_need
    pop ax
    jc .c_gone
    call far [CPFP + CPE_ONCLICK*4]
.c_gone:
    retf

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
    push ax
    mov al, MOD_CTRL
    call mod_need
    pop ax
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
    push ax
    mov al, MOD_CTRL
    call mod_need
    pop ax
    jc .t_gone
    call far [CPFP + CPE_TICK*4]
.t_gone:
    retf

; ...and the two that never leave. cp_tick_due runs once a second forever and
; cp_drv_gone runs on a driver detach, including during shutdown - so both are
; resident bodies with the plain thunk they always had.
cpf_cp_drv_gone:       call cp_drv_gone_x
                    retf
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
    push ax
    mov al, MOD_CTRL
    call mod_need
    jc .no
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
    call KERNEL_SEG:cw_toast_say
    pop cx
    pop bx
    pop ax
    stc
    ret

section .text
