; =============================================================================
; os8088 - apps/word/word.asm
;
; MICROSOFT WORD (SPEC.md 68) - a faithful native reimplementation of Word
; for Windows 1.1a ("Opus") as an os8088 package. The reasoning and feature
; inventory are docs/WORD-PLAN.md; SPEC.md 68 is the binding contract.
;
; The text engine is Note Pad's (SPEC.md 27), transplanted wholesale with
; prefix sc_: the one-walk/many-queries layout pass, row signatures and
; damage ranges, the space-padded row buffer flushed as one opaque font_run,
; blit scrolling with the row description shifted alongside, the decimating
; checkpoint index, the append fast path, XOR selection, drag-and-drop, the
; 5-deep undo, find/replace with its docked panel, and the lazy worker that
; pays the wrap/height debts. The engine comments below still cite SPEC.md
; 27 - deliberately: they describe the transplanted machinery, and 27 is
; where that machinery is specified. SPEC.md 68.6 keeps the architecture
; whole by reference.
;
; On top of the transplant, the AUTHENTIC in-window chrome (SPEC.md 68.2):
; the nine-title menu bar with Opus's verbatim menus (dropdowns, separators,
; right-justified shortcut captions, check marks, mnemonic underlines,
; disabled-pen greying, press-drag-release AND click-open interaction,
; Alt+mnemonic and arrow/Enter keyboard driving), the ribbon (Font:/Pts:
; combos + the B I K | U W D | script | pilcrow toggles, inert until the
; formatting stage), the ruler (Style: combo, alignment/spacing/tab cells,
; the inch scale with indent markers), and the status line (Pg/Sec/At/Ln/Col
; live from the caret, CAPS/NUM lamps from the BIOS lock flags), all four
; strips toggling from the View menu. sc_bounds reserves the strips inside
; the content; the find panel docks BELOW the ruler (sc_bounds and sc_fpgeom
; both add the live [sc_ctop]). No kernel menus - the kernel bar carries only
; the app-name pull-down with About. The buffer ceiling is SC_MAXKB = 30
; (the CRLF staging arithmetic stays 16-bit exact - see the constant).
; =============================================================================

%include "os88api.inc"

; The picture decoders' CONSTANTS only (SPEC.md 93). Insert > Picture is
; assembled a long way above where a shared include may put its code, and
; the IMG_* offsets have to exist by then or every `mov [si+IMG_*]` sizes
; its displacement differently on the two passes. The code itself is
; %included into SCRIBE.OVL further down.
%define OS88IMG_CONSTS_ONLY
%include "os88img.inc"
%undef OS88IMG_CONSTS_ONLY

; SPEC.md 13.10.5's thumb GESTURE. AT THE TOP, not beside the %include that
; pulls os88ui.inc in - that one is 19,000 lines below here and a %ifdef is
; answered in FILE ORDER (13.10.7.4).
%ifndef SBDRAGOFF
%define OS88UI_SBDRAG
%ifndef SB_RATE
%define SB_RATE 0               ; RATE 0 (13.10.5.4): a scroll here ends in
%endif                          ; sc_redraw, and this window is the widest in
SC_SBRATE   equ SB_RATE         ; the system
%endif

; =============================================================================
; SCRIBE - a fork of apps/word/word.asm (SPEC.md 94).
;
; IT CARRIES THE sc_ PREFIX AND THE sc*.inc FILENAMES. It did not always: the
; fork began as a line-for-line copy of word.asm keeping every wd_ symbol, so
; that `diff -r apps/word apps/scribe` was the whole of what it changed and an
; upstream fix read straight across. That was the right trade while the two
; files were nearly identical, and it stopped being one:
;
;   - The app is called SCRIBE. Symbols that say `wd_` name the program it was
;     forked from, not the program you are reading.
;   - The two share a symbol NAMESPACE, not just a shape. tools/stkbalance.py
;     walking both files in one pass saw 645 NAMES DEFINED TWICE and merged the
;     two apps' call paths, so the fork had to be walked alone to be walked at
;     all - and a gate that cannot see two files together is a gate with a hole
;     in it.
;
; The cost is real and is paid knowingly: an upstream fix to WORD no longer
; applies here as a patch, and the diff against word.asm is now a rename plus
; the changes rather than the changes alone. SPEC.md 94.1 records both sides.
; NASM resolves the includes out of apps/scribe/ because that is the -I on
; this package's own rule.
;
; WHAT DIFFERS FROM WORD, AND ALL OF IT IS IDENTITY:
;   the header name, the icon glyph, the overlay's filename, the About box,
;   the title bar, and the association - which it does NOT declare.
; =============================================================================

    OS88_HEADER 'SCRIBE', sc_entry, 1  ; bit 0 icon. NOT bit 1: see below

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A white page with a big black 'W' - the document identity, one glyph. The
; mask is the page silhouette dilated 1px, so it sits on a clean white
; underlay over the desktop grey and over a selected row.
;
;   data               mask
;   ................   .#############..
;   ..###########...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#..###....#...   .#############..
;   ..#.#...#...#...   .#############..
;   ..#.#.......#...   .#############..
;   ..#..###....#...   .#############..
;   ..#.....#...#...   .#############..
;   ..#.#...#...#...   .#############..
;   ..#..###....#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..###########...   .#############..
;   ................   .#############..
;
; The same page silhouette Word's icon has - these are two word processors and
; the outline should say so - with an S where its W is. The MASK is unchanged:
; it is the page dilated 1px and does not depend on the glyph inside.
    OS88_ICON16
    dw 0x7FFC                       ; 16 mask rows
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x0000                       ; 16 data rows
    dw 0x3FF8                       ; the page's top edge
    dw 0x2008
    dw 0x2008
    dw 0x2708                       ; .###.  the S begins
    dw 0x2888                       ; #...#
    dw 0x2808                       ; #....
    dw 0x2708                       ; .###.
    dw 0x2088                       ; ....#
    dw 0x2888                       ; #...#
    dw 0x2708                       ; .###.
    dw 0x2008
    dw 0x2008
    dw 0x2008
    dw 0x3FF8                       ; ...and its bottom
    dw 0x0000
    OS88_ICON16_END

; --- NO ASSOCIATION BLOCK, AND THAT IS THE POINT (SPEC.md 54/68.4/88.2) ------
; Word declares .DOC. If SCRIBE declared it too, the winner on a disk holding
; both would be whichever registered LAST: kernel/assoc.inc's assoc_ext_new
; path ends in `mov [bx+3], dl`, which OVERWRITES the row's app index rather
; than refusing the second claim. So the owner of a double-click would be
; decided by directory order, silently, and would move when a disk was
; rebuilt. An extension has one owner.
;
; SCRIBE still opens and saves .DOC - File > Open and Save As are untouched,
; and it reads and writes exactly the bytes Word does. What it does not do is
; take the double-click away from Word.
;
; apps/cword reached the same place from the other side and it is the
; precedent: it claims .RTF (cword.c's os88_assoc_set) rather than fight for
; .DOC. Declaring nothing is the stronger version of that, because there is
; no third extension anything in this tree actually writes.
;
; TO GIVE SCRIBE THE ASSOCIATION INSTEAD: put the block back, set flags bit 1
; in OS88_HEADER above, and drop word.o88 from the disk. Two lines, and the
; second one is the one that matters.

; --- the document lives in the HEAP, not in this package's bss (SPEC.md 27.6)
; A note is data, and data of a size only the user knows; a package's region
; is image + bss capped at APP_MAX_SIZE, so anything sized by the user belongs
; in a claim (SPEC.md 50.3). The claim starts at SC_KB0, grows a kilobyte at a
; time as the note fills it, is sized to the file on a load, and shrinks back
; on File > New. [sc_dseg]:0000 is the text and [sc_cap] its capacity.
SC_KB0       equ 1              ; the document claim at launch, KB
SC_GROWKB    equ 1              ; ...and the quantum it grows by
SC_MAXKB     equ 30             ; ...and its ceiling (SPEC.md 68.3). Note
                                ; Pad's was 16, and what bounds it is the
                                ; SAVE arithmetic, not memory: a save expands
                                ; every 13 to CR LF, and its staging pass
                                ; walks that with a 16-bit DI and counts it
                                ; with a 16-bit BX. At 30KB the worst case is
                                ; 2 x 30,720 = 61,440 and both hold exactly
                                ; (so does sc_stghold's 2 x [sc_len] + 1023 =
                                ; 62,463, and SC_MAXKB * 1024 = 30,720 still
                                ; fits the [sc_cap] word); at 32KB all three
                                ; wrap to zero. So 30 is the last whole
                                ; kilobyte the 16-bit staging walk can carry
                                ; - going further means teaching that loop to
                                ; cross a segment and making its count 32-bit
SC_STGMIN    equ 1              ; the save's transient staging claim, KB
SC_MAXCOL    equ 341            ; cells a row can hold, plus one for the NUL.
                                ; A row is accumulated into a buffer and drawn
                                ; as ONE opaque font_run (SPEC.md 6.1/27.2) -
                                ; or, in a proportional face, as one composed
                                ; band (SPEC.md 6.3/68.13)
                                ;
                                ; 1360/4 AND NOT 1360/8 since SPEC.md 68.13:
                                ; the divisor is the NARROWEST ADVANCE a face
                                ; may have, which os88type.inc pins at
                                ; TY_MINADV = 4 and refuses a face below
                                ; (SPEC.md 6.4). It has to be that number and
                                ; not the one the shipped face happens to use,
                                ; because the face comes off a floppy and this
                                ; constant is in the tree - the two can only
                                ; be kept together by the OPEN checking it,
                                ; which is where the check is.
                                ;
                                ; 1360/8 AND NOT 720/8. It was the widest
                                ; SCREEN, and a window on an extended desktop
                                ; is not bounded by one: straddling the seam
                                ; it may be the whole VIRTUAL desktop wide,
                                ; which is 1360 px with a 720 Hercules beside
                                ; a 640 CGA or VGA (SPEC.md 39.19.2). Past the
                                ; clamp sc_rflush DROPS the cell - the guard
                                ; at .nocell even names the clamp as the case
                                ; where that happens - so the tail of every
                                ; row went unwritten AND unerased: text, then
                                ; a gap, then whatever those cells held
                                ; before. Reported from the field with a photo
                                ; of exactly that, and "a redraw shows the
                                ; same gap without the fragments" is the same
                                ; fact seen twice, the white fill clearing the
                                ; stale pixels and the clamp still stopping
                                ; the text short.
%define SC_MAXROWS 60           ; signature slots, one per row the content can
                                ; A %define and not an equ, because the bss
                                ; block at the foot of this file is laid out
                                ; by a preprocessor counter now (it was ~120
                                ; hand-computed offsets, and this change adds
                                ; forty fields to it) - and %assign can only
                                ; add up things the PREPROCESSOR knows.
                                ; Textually substituted, so every use below
                                ; reads exactly as it did.
                                ; show (SPEC.md 27.2). The tallest this window
                                ; can be is a fullscreen VGA frame, where the
                                ; frame IS the content (SPEC.md 11.2): 480 rows
                                ; less the 6px top margin and the 7px a row's
                                ; own band needs is 59. sc_bounds clamps to
                                ; this, so a taller screen degrades to "the
                                ; rows past 60 are always redrawn" rather than
                                ; writing past the array
                                ; (SC_BSS_TOTAL lives at the foot of this file
                                ; now, with the fields it counts)
SC_BRK_CELLS equ 60             ; the visual break's trigger (SPEC.md 27.3):
                                ; the CELLS a keystroke would repaint BELOW
                                ; the caret's row. Not rows - this window is
                                ; resizable and a row is 30 cells or 90
                                ; depending how wide the user dragged it, so
                                ; a row count means two different amounts of
                                ; work. A cell is ~0.9ms on a 4.77MHz 8088 -
                                ; measured four ways, PERFORMANCE.md Part 2 -
                                ; so 60 cells is ~54ms, which is the point
                                ; where a keystroke stops keeping up
SC_IDLE      equ 9              ; ticks of no typing before the break is
                                ; reconciled: ~500ms at 18.2Hz
SC_WTICKS    equ 3              ; ...and how often the worker looks, ~165ms.
                                ; Finer than SC_IDLE so the settle lands
                                ; near the deadline rather than a tick late
SC_HCHUNK    equ 4              ; rows of the height count per worker pass
                                ; (SPEC.md 27.7.3). The count is the one walk
                                ; that cannot be bounded by the view, so it is
                                ; bounded by TIME instead: this many rows, then
                                ; the lock goes back.
                                ;
                                ; It sizes TWO things and the second is what
                                ; set it. The lock HOLD is what a UI action
                                ; waits behind, and the DUTY CYCLE is what the
                                ; count costs the rest of the machine: the
                                ; worker sleeps SC_WTICKS between passes, so
                                ; the fraction spent counting is hold/(hold +
                                ; 165ms). At 16 rows the hold measured 124ms
                                ; (`make wdbench`) - two ticks, and 43% of the
                                ; machine for as long as the count runs, which
                                ; the field reported as exactly that. A
                                ; measure row is ~6ms, not the ~2 assumed, so
                                ; 4 rows is a ~25ms hold and ~13%.
                                ;
                                ; The count taking longer in WALL time is the
                                ; thing being traded away, and it is nearly
                                ; free to trade: SPEC.md 27.7.4's estimate
                                ; already put the bar in the right place, so
                                ; what the count adds is exactness, and
                                ; nothing needs that in a hurry
SC_SB_W      equ 14             ; scroll bar width, the Disk window's
                                ; (SPEC.md 22) so the two look like one OS.
                                ; It is reserved ALWAYS, present or not:
                                ; whether a bar is NEEDED depends on the row
                                ; count, which depends on the wrap width,
                                ; which would depend on the bar
SC_SB_STEP   equ 4              ; rows an arrow cell steps. The Disk window
                                ; steps one, but its rows are 16px list
                                ; entries and these are 8px lines of prose:
                                ; four of them is about the same travel, and
                                ; a blit-scrolled band costs the same whether
                                ; it moves one row or four (SPEC.md 27.7.2)
SC_GROW      equ 13             ; the grow box the kernel draws in the
                                ; content's bottom-right corner (SPEC.md
                                ; 11.1). The bar stops above it, exactly as
                                ; the Disk window's stops above its status
                                ; line - drawn into, the two overlap and the
                                ; down arrow comes out as a square
; --- the chrome strips (SPEC.md 68.2) ----------------------------------------
; Word's chrome is the WINDOW's, not the kernel's: MENU_APPMAX is five and
; Word's bar is nine menus, so sc_bounds reserves four strips inside the
; content and the text band is what remains. THIS STAGE paints each strip as
; a plain white band with a 1px black rule at its inner edge - the strip
; heights are the real ones, so the next stage draws chrome into geometry
; that is already load-bearing. The find panel docks BELOW the ruler:
; sc_bounds and sc_fpgeom both offset by SC_CHROME_TOP, and sc_panmove's
; open/close blit starts there too, so the chrome never rides a text blit.
;
; SC_CHROME_TOP is 48, A MULTIPLE OF 8 ON PURPOSE: the content origin is
; snapped to a byte column AND a byte-friendly y (OSAPI_WM_SNAP), and every
; blit delta and glyph y downstream is chrome top + a multiple of 4 or 8 -
; keeping the sum aligned is what keeps gfx_scroll's banked fast path and
; font_run's single-store path exactly as reachable as they were in Note Pad.
SC_MENU_H    equ 14             ; the in-window menu bar
SC_RIBBON_H  equ 16             ; the ribbon (View > Ribbon)
SC_RULER_H   equ 34             ; the ruler (View > Ruler): TWO rows, as
                                ; Opus's own is. ibdefs.h's ibdRuler2 puts
                                ; every button at y=1 with height 12 and then
                                ; ibidCustomWnd(1,14,...) - idRulMark, the
                                ; scale - on a row of its OWN below them. This
                                ; port had the scale sharing the button row,
                                ; starting at x=376, which put it over the
                                ; right-hand third of the window and lined it
                                ; up with nothing. Row 1 is the buttons, row 2
                                ; is the scale, and the scale spans the TEXT
                                ; COLUMN and starts where the text does
SC_RL_ROW2   equ 16             ; ...where that second row begins in the strip
SC_STATUS_H  equ 12             ; the status bar (View > Status Bar)
SC_CHROME_TOP equ SC_MENU_H + SC_RIBBON_H + SC_RULER_H   ; 48, all strips ON.
                                ; The LIVE chrome top is [sc_ctop], computed by
                                ; sc_ctcalc from the View toggles - ribbon and
                                ; ruler can each be hidden (SPEC.md 68.2), and
                                ; then the sum is 30 or 32 or 14. The align-
                                ; ment note above holds for the default stack;
                                ; a toggled top costs the blit its banked fast
                                ; path for that one transient move, never
                                ; correctness.
%if (SC_CHROME_TOP & 7) != 0
  %error "SC_CHROME_TOP must be a multiple of 8 - see the note above"
%endif

; --- the in-window menu system (SPEC.md 68.2) --------------------------------
; Word's nine menus, verbatim from Opus's menus.cmd. A menu (or a ribbon/ruler
; combo's one-entry list, which reuses all of this) is described by an 8-byte
; row of sc_mtab and an array of 8-byte item records; the open dropdown's
; rectangle is computed once at open (sc_mgeo) and banked in sc_mrect, so the
; painter, the hit test and the close repaint all read the same four words -
; the fm_hit discipline (SPEC.md 22).
SC_M_N       equ 9              ; menus on the bar
; SC_PROPDRAW - is a chosen face DRAWN, or only opened and named?
;
; 1, and SPEC.md 68.13's conversion is finished behind it: the face opens, the
; ribbon names it, sc_bandrun composes and emits a run through SPEC.md 6.3's
; band, the row advance and glyph band follow the face's own rows via
; [sc_gh]/[sc_ghb], and [sc_pxon] makes sc_px[] the truth about where a cell
; sits - so the pen advances by ty_advof, the wrap rule measures pixels and a
; click splits a character at half its own advance.
;
; The switch stays because it is the one place to stand a whole face's worth of
; drawing down from, and because a half-converted Word is worse than an
; unconverted one: it draws in one face and measures in another. That state
; DID ship once and SPEC.md 68.13.1 records what it was reported as.
;
; %assign AND NOT equ, and that distinction cost a whole debugging round: `%if`
; is a PREPROCESSOR directive and an `equ` is an ASSEMBLER symbol, so `%if
; SC_PROPDRAW` against an equ sees an undefined name, evaluates it as 0, and
; silently assembles neither arm. The build is clean, the flag reads 1 in the
; source, and nothing happens at run time.
%assign SC_PROPDRAW 1
%assign SC_BANDDBG 0            ; the band probe, off - see sc_bandrun

SC_MAXFONT   equ 10             ; faces the Font combo will list beside Pica.
                                ; Ten, which is os88type's TY_MAXFAM and what
                                ; FONTS/ carries (SPEC.md 6.4.1). It used to
                                ; be six, because eleven 10px items hang off a
                                ; ribbon box 60 rows down a 200-row CGA screen
                                ; ran off the bottom of the window; sc_mgeo
                                ; now SLIDES a combo's dropdown up until it
                                ; fits instead of clipping its tail off, which
                                ; is what makes the tenth face reachable there
SC_MI_SZ     equ 8              ; bytes in one SCMI record - four bytes then
                                ; two words, and sc_mitemp indexes by it. The
                                ; dropdown is filled at run time now (SPEC.md
                                ; 65.13), so the size the macro emits needs a
                                ; name rather than being counted by hand
SC_MT_SZ     equ 8              ; ...and one sc_mtab row, which sc_mgeti
                                ; reaches with three shifts
SC_M_FONTC   equ 9              ; the ribbon's Font combo, as a pseudo-menu
SC_M_PTSC    equ 10             ; ...its Pts combo
SC_M_STYLEC  equ 11             ; ...and the ruler's Style combo
SC_M_NONE    equ 0xFF           ; [sc_mopen]: nothing open
SC_MI_HGT    equ 10             ; an item band: 8px of glyph + 1 above + 1 under
SC_MS_HGT    equ 5              ; a separator band

SCMF_SEP     equ 1              ; item flags: a separator hline
SCMF_DIS     equ 2              ; disabled - drawn with the disabled pen
                                ; (OSAPI_GFX_PEN CF=1, SPEC.md 47), never fired
SCMF_CHK     equ 4              ; carries a check mark; sc_mchk answers it live

; Menu actions - what an ENABLED item does when it fires. One byte per item,
; dispatched by sc_mfire's table; menu items and shortcut keys are twin doors
; onto the same routines, exactly as the kernel-menu stage had it.
SCA_NONE     equ 0
SCA_NEW      equ 1
SCA_OPEN     equ 2
SCA_CLOSE    equ 3
SCA_SAVE     equ 4
SCA_SAVEAS   equ 5
SCA_EXIT     equ 6
SCA_UNDO     equ 7
SCA_CUT      equ 8
SCA_COPY     equ 9
SCA_PASTE    equ 10
SCA_SEARCH   equ 11
SCA_REPL     equ 12
SCA_DRAFT    equ 13             ; View > Draft (SPEC.md 68.11)
SCA_VRIB     equ 14
SCA_VRUL     equ 15
SCA_VSTA     equ 16
SCA_ABOUT    equ 17
SCA_WIN1     equ 18             ; Window > 1 <doc>: the one window; checked
SCA_CSEL     equ 19             ; a combo's entry - cosmetic select
SCA_CHAR     equ 20             ; Format > Character... - the modal dialog
SCA_PARA     equ 21             ; Format > Paragraph... (SPEC.md 68.3)
SCA_GOTO     equ 22             ; Edit > Go To... (SPEC.md 68.7)
SCA_SORT     equ 23             ; Utilities > Sort... (SPEC.md 68.9)
SCA_RENUM    equ 24             ; Utilities > Renumber...
SCA_TOC      equ 25             ; Insert > Table of Contents...
SCA_PAGE     equ 26             ; View > Page (SPEC.md 68.11)
SCA_PICT     equ 27             ; Insert > Picture... (SPEC.md 94.9)
SCA_MAX      equ 27

; --- the CHP attribute byte (SPEC.md 68.3) -----------------------------------
; One byte per character, in a claim that mirrors every gap operation the text
; claim makes. Bit 7 is reserved in the FILE format; in the ROW's attribute
; buffer it marks a paragraph-mark cell (Show-all's pilcrow), which never
; reaches the CHP claim.
SCAT_BOLD    equ 0x01
SCAT_ITAL    equ 0x02
SCAT_UL      equ 0x04
SCAT_WUL     equ 0x08
SCAT_DUL     equ 0x10
SCAT_SCAP    equ 0x20
SCAT_HID     equ 0x40
SCAT_PIL     equ 0x80           ; row-buffer only: this cell is a pilcrow

; --- the PAP dictionary (SPEC.md 68.3) ---------------------------------------
; Paragraph formatting lives ON THE PARAGRAPH MARK: a 13's CHP byte is an
; index into a 256-entry dictionary of unique 4-byte formats, exactly Word's
; arrangement shrunk to fit - so undo, cut/paste and the drag-move's rotate
; carry paragraph formats with the ordinary text+CHP machinery and no third
; structure exists to keep synchronized. The LAST paragraph has no mark, so
; its index lives in [sc_pap_tail]. Entry layout:
;   byte 0  packed: bits 0-1 alignment (left/center/right/justified),
;                   bits 2-3 line spacing (0/1/2 = 8/12/16px row advance),
;                   bit 4 space before (an open paragraph: +8px on the
;                   paragraph's first row)
;   byte 1  left indent, in cells (1 cell = 8px = 1/10" at pica pitch)
;   byte 2  first-line indent, SIGNED cells relative to the left indent
;   byte 3  right indent, in cells
; Entry 0 is Normal (all zero) and is pre-seeded; sc_papfind deduplicates,
; entries live for the session, and a 257th unique format is refused with a
; toast - the dictionary never moves or reuses an index, which is what lets
; an undo blob's old index still mean the old format.
SC_PAPMAX    equ 256            ; entries (4 bytes each: a 1KB claim)
SC_INDMAX    equ 100            ; indent clamp, cells (10 inches)
SC_TABSTOP   equ 5              ; default tab stops: every 5 cells (half inch)
                                ; from the row's own start pen - custom stops
                                ; are out of scope (the ruler's tab-type cells
                                ; are greyed; a real stop table is a later
                                ; stage's structure)
SCPA_ALIGN   equ 0x03           ; the packed byte's fields
SCPA_SPACE   equ 0x0C
SCPA_SB      equ 0x10

; sc_modpap operations - every door (Ctrl keys, ruler cells, marker drags,
; the Format Paragraph dialog) funnels through one span-modifier
SCPO_ALIGN   equ 0              ; arg = 0..3
SCPO_SPACE   equ 1              ; arg = 0..2
SCPO_SB      equ 2              ; arg = 0/1
SCPO_ADDLEFT equ 3              ; arg = signed cells (Ctrl-N: +5)
SCPO_HANG    equ 4              ; left +5, first -5 (Ctrl-T)
SCPO_UNHANG  equ 5              ; left -5, first +5 (Ctrl-G)
SCPO_RESET   equ 6              ; back to Normal, index 0 (Ctrl-X)
SCPO_SETLEFT equ 7              ; arg = cells (the ruler's left marker)
SCPO_SETFIRST equ 8             ; arg = SIGNED cells (the first-line marker)
SCPO_SETRIGHT equ 9             ; arg = cells (the right marker)
SCPO_SETALL  equ 10             ; the whole candidate [sc_pfb] (the dialog)

; --- the modal dialog framework (SPEC.md 68.3) -------------------------------
; A dialog is a descriptor: dw width, height, title; then 12-byte control
; records (db type, flags; dw x, y, p1, p2, p3 - x/y relative to the dialog's
; top-left), terminated by a type-0 byte. Painter and hit test walk the SAME
; records - the fm_hit discipline - so a control cannot be drawn one place
; and clicked another. Reused by the later dialog stages.
SCD_LBL      equ 1              ; label:      p1 = string
SCD_CHK      equ 2              ; check box:  p1 = string, p2 = attr mask,
                                ;             p3 = mnemonic index in p1
SCD_RAD      equ 3              ; radio:      p1 = string, p2 = selected
SCD_GRP      equ 4              ; group box:  p1 = title, p2 = w, p3 = h
SCD_BOX      equ 5              ; combo/edit mock: p1 = shown text, p2 = w
SCD_BTN      equ 6              ; button:     p1 = label, p2 = w,
                                ;             p3 = 1 OK (default ring) /
                                ;             2 Cancel / 3 No (the dirty
                                ;             prompt's third verb, 65.4)
SCD_EDIT     equ 7              ; live edit:  p1 = its buffer (NUL), p2 = w,
                                ;             p3 = capacity in chars (0 = 6).
                                ;             Click or Tab focuses; digits
                                ;             . - " type; BkSp deletes
                                ;             (SPEC.md 68.3). With SCDF_TXT
                                ;             in the flags it takes every
                                ;             printable 32..126 instead - the
                                ;             search dialogs' boxes (65.7)
                                ; ...and a SCD_RAD whose p3 is NON-ZERO is a
                                ; LIVE radio: p3 is its group, a click selects
                                ; it and deselects its group-mates. p3 = 0 is
                                ; the char dialog's decorative (greyed) kind
SCDF_DIS     equ 1              ; control flags bit 0: disabled (SPEC.md 47)
SCDF_TXT     equ 2              ; bit 1, SCD_EDIT only: free text (SPEC.md
                                ; 65.7). While one is FOCUSED it consumes
                                ; letters, so check-box mnemonics answer
                                ; only when no text edit has the focus

; --- ribbon and ruler layout (SPEC.md 68.2), content-relative x --------------
; Grouping and order per Opus ibdefs.h: Font/Pts combos, then Bold Italic
; SmallKaps | Underline Word Double | the stacked super/subscript pair | the
; show-all pilcrow at the far right; ruler: Style combo, align x4, spacing x3,
; open/close space, tab type x4, then the inch scale. The x offsets keep every
; combo's TEXT on a multiple of 8 from the (snapped) content origin, so the
; strings ride font_run's single-store path.
SC_BTN_W     equ 12             ; a toggle cell, 12x12
SC_BTN_P     equ 13             ; ...and the pitch inside a group
SC_RB_FLBL   equ 8              ; 'Font:' label
SC_RB_FBX    equ 56             ; Font combo box (text at +8)
SC_RB_FBW    equ 96
SC_RB_PLBL   equ 168            ; 'Pts:' label
SC_RB_PBX    equ 208            ; Pts combo box
SC_RB_PBW    equ 56
SC_RB_B1     equ 272            ; B I K
SC_RB_B2     equ 319            ; U W D
SC_RB_SS     equ 366            ; the super/subscript pair
SC_RL_SLBL   equ 8              ; 'Style:' label
SC_RL_SBX    equ 64             ; Style combo box
SC_RL_SBW    equ 96
SC_RL_AL     equ 168            ; align left/center/right/justified
SC_RL_SP1    equ 224            ; spacing '1'
SC_RL_SP15   equ 239            ; spacing '1.5' (26 wide: three glyph cells)
SC_RL_SP15W  equ 26
SC_RL_SP2    equ 268            ; spacing '2'
SC_RL_OC     equ 284            ; closed/open paragraph spacing
SC_RL_TAB    equ 314            ; tab type L C R D
SC_RL_SCALE  equ 376            ; the inch scale's zero
; The smallest FRAME the chrome still fits in (SPEC.md 11.100.2). The ribbon
; and the ruler are rows of FIXED positions - the last of them is the ruler's
; inch scale at SC_RL_SCALE and the ribbon's super/subscript pair at
; SC_RB_SS - so below this they are simply drawn past the window's right edge,
; and the gfx primitives clip to the SCREEN and not to a window (SPEC.md 11.3).
; WMIN_W is 96 and the grow box could reach it: measured, this window went to
; 96x85, content 94, with two strips of chrome laid out to 376 inside it.
SC_MIN_W     equ SC_RL_SCALE + 24 + 2
SC_MIN_H     equ SC_CHROME_TOP + SC_STATUS_H + 48 + TITLE_H + 1

SC_ST_CELLS  equ 46             ; status line text cells (delta-cached):
                                ; 'Pg 15  Sec 1  15/15  At 54li  Ln 54  Col
                                ; 171' is 44 at the worst (SPEC.md 68.2)
SC_PGCOLS    equ 60             ; the sheet's text column in Page view: 60
                                ; cells = 480px = 6 inches at this port's
                                ; scale (1 cell = 1/10"), which is US Letter
                                ; less Word's own 1.25" margins. The COLUMN
                                ; and not the full 8.5" sheet, on every
                                ; adapter: the sheet is 680px wide and only
                                ; Hercules is wider than 640 (SPEC.md 39), so
                                ; a full sheet would fit one adapter of three
                                ; and CLAUDE.md's rule is to look at a 1bpp
                                ; adapter before calling a layout done
SC_PGLINES   equ 54             ; lines to a page (SPEC.md 68): Pg/Ln derive
                                ; from the caret's absolute line by this

; --- the italic claim (SPEC.md 68.1) -----------------------------------------
; ONE claim carries two things: the sheared 4bpp glyph table, and the staging
; area a run is composed in before its single gfx_blit4. The staging area used
; to be BSS - 5,440 bytes of it, 55% of this package's whole bss, reserved on
; every machine whether a document ever showed an italic or not. A package's
; image + bss cannot reach 64KB (APP_MAX_SIZE, and the ceiling is the SEGMENT
; rather than a policy), so bss is the scarcest thing here and the heap is
; not: moving it costs one wider claim and the ES overrides that follow from
; it, and buys back more room than every other reclaim in the package put
; together.
;
; It rides the TABLE's claim rather than one of its own because the two have
; exactly the same lifetime and the same refusal: sc_itinit already degrades
; italic to the plain glyph when the heap says no (SPEC.md 47), and that one
; path now covers both.
SC_ITTAB     equ 95*8*4         ; the table: 95 glyphs x 8 rows x 4 bytes
SC_STG4      equ SC_ITTAB       ; ...and the staging area starts after it
SC_ITKB      equ 9              ; 3,040 + 5,440 = 8,480, so nine whole KB

SC_MARGIN    equ 8              ; left/top text margin inside the content. It
                                ; was 6, and 8 is what puts every glyph cell
                                ; on a multiple of 8 once OSAPI_WM_SNAP has
                                ; put the content origin on one (SPEC.md
                                ; 11.94): sc_tx is content left + this, and
                                ; sc_walk advances the pen by 8 from there.
                                ; A glyph at an unaligned x spills into a
                                ; SECOND framebuffer byte whenever the shift
                                ; carries ink into it, and this window redraws
                                ; text on every keystroke
                                ; The two F-keys are SC_KEY_NEXT and
                                ; SC_KEY_PREV below, and they are the only
                                ; two: F2 was Save and F3 was Load, the DOS
                                ; Editor's pair, and both are Ctrl-letters now
                                ; (SPEC.md 27.1/27.10). What made the DOS keys
                                ; worth keeping was that they were the ones a
                                ; user of that machine already knew, and this
                                ; window has a Macintosh menu bar over it
SC_K_F1      equ 0x3B           ; F1 - Help: opens the About box (the only
                                ; help there is; Help > Index is greyed)
SC_K_F4      equ 0x3E           ; F4 - repeat search down (SPEC.md 68.7)
SC_K_F5      equ 0x3F           ; F5 - Go To... (keys.cmd EditGoTo)
SC_K_F8      equ 0x42           ; F8 - extend selection (status EXT)
SC_K_SF4     equ 0x57           ; Shift-F4 - repeat search up (Shift-F1..F10
                                ; are scans 0x54..0x5D)
SC_K_HOME    equ 0x47           ; the caret keys, int 16h scan codes
SC_K_UP      equ 0x48
SC_K_LEFT    equ 0x4B
SC_K_RIGHT   equ 0x4D
SC_K_END     equ 0x4F
SC_K_DOWN    equ 0x50
SC_K_DEL     equ 0x53
SC_NAMEMAX   equ 12             ; 8 + '.' + 3, as SPEC.md 38.6 hands it over
SC_PICMAX    equ 8              ; pictures one document may hold. The CHP byte
                                ; of the 0x01 that marks one is its INDEX here,
                                ; exactly as a paragraph mark's CHP byte is its
                                ; PAP dictionary index (SPEC.md 68.3) - so a
                                ; picture needs no new bit anywhere, and bit 7
                                ; stays as unavailable as 68.3 says it is
SC_PICREC    equ 8              ; w, h, stride, segment
SC_PICCH     equ 1              ; the character. Word's own (chPicture), and
                                ; safe here because 68.4's readers drop every
                                ; control below 32 except tab and CR - so a
                                ; 0x01 in this buffer can only be ours
SC_PICKB     equ 40             ; the decoded picture's TRANSIENT claim
                                ; (SPEC.md 94.9). 40KB holds a 640x128 or a
                                ; 320x256 in packed 4bpp, and os88img.inc
                                ; refuses anything past what it is given
                                ; rather than writing past the claim

; --- keys (SPEC.md 27.8/68.3) ------------------------------------------------
; The control characters int 16h already hands over in AL. Backspace (8),
; Tab (9) and Enter (13) are Ctrl-H/I/M, so italic, hidden and UnIndent are
; spoken for (the first two ride the Format Character dialog; UnIndent rides
; Format Paragraph and the ruler markers).
;
; THE MAP IS keys.cmd's, and the authentic map has NO Ctrl-X/C/V editing trio
; - Word 1.1a cut/copy/pasted on Shift+Del / Ctrl+Ins / Shift+Ins (exactly
; the captions the Edit menu shows) and spent the letters on formatting:
; C-C CenterPara, C-X ResetPara, C-R RightPara. Those are bound below as
; keys.cmd says; the Ins/Del chords are decoded from the scan code plus the
; BIOS shift flags in sc_onkey. Deviations, each deliberate: Ctrl-A stays
; Select All (the authentic C-NumPad5 does not arrive as anything usable),
; Ctrl-S stays Save and Ctrl-F Search (their authentic meanings - style/font
; box focus - have no home yet, and a Save with no key is a document lost),
; Ctrl-Z stays undo beside Alt+BkSp (unbound in keys.cmd, harmless).
SC_C_SELALL  equ 0x01           ; Ctrl-A
SC_C_BOLD    equ 0x02           ; Ctrl-B - bold (keys.cmd; SPEC.md 68.3)
SC_C_CENTER  equ 0x03           ; Ctrl-C - CenterPara (keys.cmd!)
SC_C_DUL     equ 0x04           ; Ctrl-D - double underline (keys.cmd)
SC_C_CLOSESP equ 0x05           ; Ctrl-E - CloseUpPara
SC_C_FIND    equ 0x06           ; Ctrl-F
SC_C_UNHANG  equ 0x07           ; Ctrl-G - UnHang
SC_C_JUST    equ 0x0A           ; Ctrl-J - JustifyPara
SC_C_SCAP    equ 0x0B           ; Ctrl-K - small caps (keys.cmd)
SC_C_LEFT    equ 0x0C           ; Ctrl-L - LeftPara
SC_C_INDENT  equ 0x0E           ; Ctrl-N - Indent (+half inch)
SC_C_OPENSP  equ 0x0F           ; Ctrl-O - OpenUpPara (Open is Ctrl+F12)
SC_C_RIGHT   equ 0x12           ; Ctrl-R - RightPara
SC_C_SAVE    equ 0x13           ; Ctrl-S (deviation, kept - see above)
SC_C_HANG    equ 0x14           ; Ctrl-T - HangingIndent
SC_C_UL      equ 0x15           ; Ctrl-U - underline (keys.cmd)
SC_C_WUL     equ 0x17           ; Ctrl-W - word underline (keys.cmd)
SC_C_RSTP    equ 0x18           ; Ctrl-X - ResetPara (keys.cmd!)
SC_C_UNDO    equ 0x1A           ; Ctrl-Z
SC_C_ESC     equ 0x1B
SC_C_TAB     equ 0x09
SC_K_INS     equ 0x52           ; the Ins scan: + shift = Paste, + ctrl =
                                ; Copy (keys.cmd); Del + shift = Cut
SC_K_PGUP    equ 0x49
SC_K_PGDN    equ 0x51
SC_KEY_NEXT  equ 0x3D           ; F3 - Find Next. It WAS Load, and Load has a
                                ; menu item and Ctrl-O now: a find with no
                                ; key for the next match is not a find
SC_KEY_PREV  equ 0x56           ; Shift-F3 (Shift-F1..F10 are 0x54..0x5D)

; --- the selection (SPEC.md 27.8) --------------------------------------------
SC_SELDRAG   equ 3              ; pixels the pointer must travel before a
                                ; press inside a selection becomes a MOVE
                                ; rather than a click that collapses it

; --- undo (SPEC.md 27.9) -----------------------------------------------------
SC_UNDO      equ 5              ; edits deep, as asked
SC_UKB0      equ 1              ; the arena's first claim, KB...
SC_UMAXKB    equ 16             ; ...and its ceiling. Note Pad's 16, KEPT at
                                ; 16 while SC_MAXKB rose to 30: a Replace All
                                ; over a document whose rewritten tail is
                                ; past 16KB now falls back to "swept but not
                                ; undoable" (sc_uroom evicts, then drops the
                                ; stack - the sweep itself still runs), which
                                ; is the degrade-not-refuse path. Worth its
                                ; own decision when the undo arena is looked
                                ; at again, not a silent 14KB of heap here

; --- search and replace (SPEC.md 68.7) ---------------------------------------
; Word 1.1 had no regex, and neither does this: the engine is a literal scan
; honouring the authentic search.des options. Note Pad's regex matcher and
; its docked panel are gone - Edit > Search... / Replace... / Go To... are
; modal dialogs on the SPEC.md 68.3 framework now.
SC_FRXMAX    equ 96             ; the expanded replacement's ceiling
                                ; (SPEC.md 68.7): ^m can be as long as the
                                ; match and ^c as long as the clipboard, so
                                ; the splice buffer is bounded here and a
                                ; replacement that overflows it is REFUSED
                                ; with a toast rather than truncated
SC_PATMAX    equ 47             ; characters a pattern or a replacement holds
SC_EDCAP     equ 32             ; ...and what the dialogs' edit boxes cap at
                                ; (the box shows its whole text - no scroll)
SCFO_WORD    equ 1              ; [sc_fopt] bits, and the search/replace
SCFO_CASE    equ 2              ; dialogs' check-box masks: Whole Word,
SCFO_CONF    equ 4              ; Match Upper/Lowercase, Confirm Changes

; --- the native .DOC file (SPEC.md 68.4) -------------------------------------
; 16-byte FIB, then text, CHP, PAP (papn x 4). The flags word's LOW BYTE is
; the tail paragraph's PAP index - the one paragraph fact with no ¶ to ride.
SC_DOCMAGIC  equ 0xA59B         ; Opus's real wIdent
SC_DOCVER    equ 1
SC_FIB       equ 16             ; header bytes
; The load staging ceiling: the largest native file is 16 + 2*30,720 + 1,024
; = 62,480 bytes; 62KB (63,488) covers it and still fits a 16-bit count.
SC_LSTGKB    equ 62
SC_STGCAP    equ SC_FIB + 2*(SC_MAXKB*1024) + SC_PAPMAX*4

; --- the dirty prompt's pending action (SPEC.md 68.4) ------------------------
SCP_NEW      equ 1              ; what 'Yes'/'No' proceed to once the
SCP_OPEN     equ 2              ; save-changes question is answered
SCP_CLOSE    equ 3

; -----------------------------------------------------------------------------
; sc_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here. The
; bss arrives zeroed, which is already a fresh empty note - the built-in's
; KD_INIT proc (app_note_kinit) had nothing else to do either.
;
; The menu set is registered here rather than later because the loader's
; wm_show is what draws the first bar (SPEC.md 12.2): by the time the window
; appears, the bar already says "Scribe  File".
; -----------------------------------------------------------------------------
sc_entry:
    call OSAPI_FILE_HERE            ; where this package was LAUNCHED from,
    mov [sc_ovdir], dx              ; banked before anything can navigate away
    mov [sc_ovdrv], bl              ; - it is where SCRIBE.OVL lives, and the
                                    ; user is free to move the volume to DOCS\
                                    ; a moment later (SPEC.md 68.10)
    call ty_init                    ; face 0 (the kernel's 8x8) before anything
                                    ; can ask for a metric - SPEC.md 6.5, and
                                    ; the reason nothing below has to test
                                    ; whether a face was found
    call sc_facemetrics             ; the glyph band and the row advance, which
                                    ; are 8 and 7 for the kernel's cell - and
                                    ; bss arrives ZEROED, so without this every
                                    ; row-band test in the program compares
                                    ; against y+0 instead of y+7 and drops most
                                    ; of the note
    mov word [sc_fcap], sc_s_pica   ; ...and the ribbon's Font box names it.
                                    ; bss arrives zeroed, so without this the
                                    ; first ribbon paint draws a string at
                                    ; offset 0 - which is this package's own
                                    ; header, and looks like a corrupt heap

    call sc_xdrop                   ; the row index starts EMPTY and at its
                                    ; initial stride (SPEC.md 27.13). bss
                                    ; arrives zeroed, and a zero [sc_xksh] is
                                    ; a stride of ONE - not wrong, but 64
                                    ; entries covering 64 rows and halving four
                                    ; times to catch up on the first count
    mov ax, SC_KB0                  ; the document, before anything else: an
    call OSAPI_MEM_CLAIM            ; editor with nowhere to put the text is
    jc .nomem                       ; not a window worth opening, and the
    mov [sc_dseg], dx               ; loader's LD_EABORT says so for us
    mov ax, SC_KB0                  ; ...and the CHP claim, one attribute byte
    call OSAPI_MEM_CLAIM            ; per character, grown/shrunk in lockstep
    jc .nomem                       ; by sc_resize (SPEC.md 68.3). Claimed here
    mov [sc_cseg], dx               ; so sc_arg's load can zero it; a refusal
                                    ; aborts the launch and ld_unreserve frees
                                    ; the text claim with the region
    mov ax, 1                       ; ...and the PAP dictionary (SPEC.md 68.3):
    call OSAPI_MEM_CLAIM            ; 256 x 4 bytes, session-lived. Zeroed by
    jc .nomem                       ; hand - a claim arrives dirty and entry 0
    mov [sc_pseg], dx               ; must BE Normal, all zero
    push cx
    push di
    push es
    mov es, dx
    xor di, di
    mov cx, 512
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    mov word [sc_papn], 1
    mov word [sc_capkb], SC_KB0
    mov word [sc_cap], SC_KB0 * 1024
    call sc_defname                 ; the name and the TITLE the template
    call sc_compttl                 ; points at (SPEC.md 68.2: 'Microsoft
                                    ; Word - DOCUMENT.DOC') must exist before
                                    ; wm_create banks the pointer - the
                                    ; buffer is bss and arrives zeroed
    push si
    mov si, sc_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): sc_paint
    call OSAPI_WM_SIZABLE           ; already lays out from the live record,
    mov al, 1                       ; so the next repaint re-wraps for free
    mov al, 1                       ; ...and it PROMISES its content stands
    call OSAPI_WM_SAVEU             ; still while it is not drawing, so a
                                    ; raise puts the old pixels back instead
                                    ; of lettering 464 cells (SPEC.md 11.96.1).
                                    ; True of this app: everything that draws
                                    ; goes through sc_redraw or sc_paint, and
                                    ; the worker's two background drawers ask
                                    ; OSAPI_WM_OBSCURED first
    push cx                         ; ...and a FLOOR under the grow box: two
    push dx                         ; strips of fixed-position chrome cannot be
    mov cx, SC_MIN_W                ; expressed by WMIN_W, which is 96 for the
    mov dx, SC_MIN_H                ; whole machine (SPEC.md 11.100.2)
    call OSAPI_WM_MINSIZE
    pop dx
    pop cx
    push si                         ; ...and the HERCULES has 720 columns, of
    mov si, sc_pref                 ; which this window has never used more
    call OSAPI_WM_PREFER            ; than 600 (SPEC.md 11.100.1). A page is
    pop si                          ; the one thing that is always glad of them
    mov al, 1
    call OSAPI_WM_SNAP              ; ...and snapped (SPEC.md 11.94), because
    pop ax                          ; every keystroke redraws a row of text and
                                    ; an aligned cell writes ONE framebuffer
                                    ; byte where an unaligned one writes two.
                                    ; TRUE ON VGA TOO - mode 12h is 8 pixels
                                    ; to a byte per plane - and measured, this
                                    ; window was the worked example: it used
                                    ; to open at content x = 61 there, skew 5,
                                    ; which typebench prices at 9.4% of every
                                    ; keystroke (SPEC.md 11.94)
    push si
    mov si, sc_habout               ; NO kernel menus (SPEC.md 68.2): the
    call OSAPI_ABOUT_SET            ; nine-menu bar is drawn in the window.
    mov si, sc_menus0               ; The kernel bar carries only the app
    call OSAPI_MENU_SET             ; name pull-down - an EMPTY set (count 0)
    pop si                          ; whose AM_NAME makes the bar read
                                    ; 'Scribe' rather than the
                                    ; header's 'WORD', with About (the same
                                    ; box Help > About... opens) and Close
                                    ; under it. CF is still wm_create's: the
                                    ; branch above consumed it and both slots
                                    ; preserve flags (SPEC.md 12.2)
    mov byte [sc_vrib], 1           ; every strip starts SHOWN (View > Ribbon /
    mov byte [sc_vrul], 1           ; Ruler / Status Bar are checked toggles,
    mov byte [sc_vsta], 1           ; SPEC.md 68.2)
    mov word [sc_ctop], SC_CHROME_TOP
    mov byte [sc_mopen], SC_M_NONE  ; no dropdown, no highlight - bss zero is
    mov byte [sc_mhi], 0xFF         ; a REAL menu index and a real item
    mov [sc_win], bx                ; the worker (SPEC.md 27.3) has no callback
                                    ; to be handed this in SI
%ifdef OS88UI_SBDRAG
    pushf                           ; the entry still owes the loader
    push ax                         ; wm_create's CF (SPEC.md 13.10.7.1)
    mov ax, sc_onup                 ; SPEC.md 13.10.6.4: these two are the
    call OSAPI_WM_ONMOUSEUP         ; THUMB's alone. sc_mtrack's poll loop
    mov ax, sc_ondrag               ; (27.8.1) owns a gesture that cannot be
    call OSAPI_WM_ONDRAG            ; live at the same time as a bar drag - the
    sbb al, al                      ; menu runs modally INSIDE W_ONCLICK, and
    mov [sc_nodrag], al             ; the kernel's arm is not set until that
    pop ax                          ; returns - so 13.7's no-mixing rule is
    popf                            ; satisfied by "not at the same time"
%endif
    pushf                           ; the visual break exists for the machine
    push ax                         ; that cannot repaint a screenful between
    call OSAPI_CPU_INFO             ; keystrokes, and nowhere else: on anything
    cmp al, CPU_8086                ; faster the reflow is already invisible
    jne .nobrk                      ; and a temporary layout would be a lie
    mov byte [sc_brkok], 1          ; told for no gain. The flags are saved
.nobrk:                             ; around it because the CF this proc owes
    pop ax                          ; the loader is still riding in them and
    popf                            ; the compare above would eat it
    mov word [sc_prowi], 0xFFFF     ; .bss arrives zeroed and 0 is a REAL row
                                    ; index, so the delta cache has to be told
                                    ; it holds nothing (SPEC.md 27.2)
    mov word [sc_dpos], 0xFFFF      ; no drop marker, and 0 is a real index
    mov word [sc_dmark], 0xFFFF
    pushf                           ; the name was seeded above (before the
    call sc_arg                     ; template was banked); sc_arg opens a
    popf                            ; launch document (SPEC.md 54.5). The CF
                                    ; we owe the loader rides in the flags
.out:
    ret
.nomem:                             ; ld_unreserve gives the region back, and
    stc                             ; anything an entry proc claimed with it
    ret

; =============================================================================
; Scrolling (SPEC.md 27.7)
;
; The note used to be however much of it fitted: rows past the content bottom
; were dropped and the pen kept advancing, so text below the window existed
; and could be saved but could not be looked at. That was survivable at 512
; characters and is not at 16,384 (SPEC.md 27.6).
;
; [sc_top] is the note row drawn at the top of the view, and sc_walk's sc_row
; - the index into sc_sig, sc_rows and the dirty band - starts at MINUS it.
; Nothing downstream of that had to change: every array index is already an
; unsigned test against a limit, and a negative row read as unsigned is past
; all of them. The one place that could not see it is sc_rflush, because a row
; a little above the view has an ordinary small y rather than a huge one, so
; it grew a test of its own.
;
; The bar is the Disk window's (SPEC.md 22), drawn with the same proportions
; so the two read as one system, and it is RESERVED ALWAYS - whether one is
; needed depends on the row count, which depends on the wrap width, which
; would depend on the bar. Paging, not dragging, is what a track click does,
; which is also what the Disk window does.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_scrollmax - the largest [sc_top] that still shows text
; out: AX = max(sc_drows - sc_vrows, 0); preserves all other registers
; -----------------------------------------------------------------------------
sc_scrollmax:
    mov ax, [sc_drows]
    cmp byte [sc_hasfmt], 0
    je .uni
    sub ax, [sc_vfit]               ; formatted: how many rows fit depends on
    jns .out                        ; their heights, so the clamp loosens to
    xor ax, ax                      ; the guaranteed fit - a mostly-plain
    ret                             ; document can overscroll a little, and
.uni:                               ; the tail row is always reachable
    sub ax, [sc_vrows]
    jns .out
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_scrollto - put row AX at the top of the view
; in:  AX = the wanted row (signed; clamped here), SI = window ptr
; out: CF=0 if [sc_top] moved, CF=1 if it did not; preserves all registers
;
; Everything indexed by the visible row means something different afterwards -
; the signatures, the checkpoint and sc_rows all shift by the same amount as
; the view - so all three are dropped rather than adjusted. Adjusting them
; would be a second place that has to agree about what a row is.
; -----------------------------------------------------------------------------
sc_scrollto:
    push ax
    push bx
    or ax, ax
    jns .lo
    xor ax, ax
.lo:
    push ax
    call sc_scrollmax
    mov bx, ax
    pop ax
    cmp ax, bx
    jbe .hi
    mov ax, bx
.hi:
    cmp ax, [sc_top]
    je .same
    mov [sc_top], ax                ; sc_sig and sc_rows are NOT dropped here
                                    ; any more: they still describe the pixels
                                    ; that are still on screen, and
                                    ; sc_scrollpaint shifts all three together
                                    ; (SPEC.md 27.7.2). [sc_ptop] is what says
                                    ; the two have parted, and every path out
                                    ; of sc_redraw puts them back in step
    mov byte [sc_ckok], 0           ; the checkpoint is one row rather than an
                                    ; array, so it is cheaper to re-find than
                                    ; to carry: the view seed replaces it
    mov byte [sc_resume], 0         ; ...including one ALREADY LOADED: sc_walk
                                    ; takes [sc_sdr] as a visible row and
                                    ; sc_measure does not clear the flag, so a
                                    ; scroll between sc_seedck and the walk it
                                    ; seeded resumes at the wrong row and every
                                    ; number that walk produces - [sc_cury] and
                                    ; the note's height - is out by the scroll
    mov byte [sc_bmode], 0          ; and the visual break's fiction is over
    clc
    jmp short .out
.same:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_seecaret - scroll so the caret is inside the view
; in:  [sc_cury] from a walk, SI = window ptr
; out: CF=0 if the view moved; preserves all registers
; -----------------------------------------------------------------------------
sc_seecaret:
    push ax
    push cx
    push dx
    mov ax, [sc_currow]             ; the caret's VISIBLE row, banked by the
    or ax, ax                       ; walk (SPEC.md 68.6): its y is no longer
    js .up                          ; 8*row, but its NUMBER still is the row
    mov cx, [sc_cury]               ; visible = the caret's whole glyph band
    add cx, [sc_gh1]                       ; fits above the content bottom
    cmp cx, [sc_bot]
    jbe .same
    ; below the view. Formatted, the EXACT scroll is knowable: the caret is
    ; (cury+7 - bot) pixels past the bottom, and the pixels freed by
    ; scrolling k rows off the top are band-top(k) - ty, which the banked ys
    ; answer - so take the smallest k that frees enough (SPEC.md 68.6). The
    ; conservative [sc_vfit] target is the fallback, and the uniform case's
    ; exact answer as it always was.
    cmp byte [sc_hasfmt], 0
    je .dcons
    cmp byte [sc_rowsok], 0
    je .dcons
    sub cx, [sc_bot]                ; CX = cury+7 - bot, the overshoot
    xor dx, dx                      ; DX = k-1
.dk:
    cmp dx, [sc_vrows]
    jae .dcons                      ; the glass cannot free enough
    push bx
    mov bx, dx
    shl bx, 1
    mov bx, [bx+sc_ryb]
    add bx, 8
    sub bx, [sc_ty]                 ; freed by scrolling k = dx+1 rows
    cmp bx, cx
    pop bx
    jge .dhave
    inc dx
    jmp short .dk
.dhave:
    inc dx
    mov ax, dx
    add ax, [sc_top]
    jmp short .go
.dcons:
    mov ax, [sc_currow]
    sub ax, [sc_vfit]               ; ...bring it to the last row GUARANTEED
    inc ax                          ; to fit - conservative under formats,
    add ax, [sc_top]                ; exact while uniform (vfit = vrows)
    jmp short .go
.up:
    add ax, [sc_top]                ; ...above it: bring it to the first
.go:
    call sc_scrollto
    jmp short .out
.same:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_sbset - fill sc_sb with this window's bar (SPEC.md 13.10)
; out: BX = the block; every other register preserved
;
; **THIS APP'S BAR WAS PIXEL-IDENTICAL TO THE SHARED ELEMENT**, so the
; conversion changes nothing on the glass - it was simply the seventh copy of
; a picture the kernel already draws once (SPEC.md 13.10.6). Checked rule by
; rule before it was made: the two arrow-cell rules at ty+10 and sbb-10, the
; track between ty+11 and sbb-11, the arrow glyphs centred on x1+6 (the
; element COMPUTES x1 + (x2-x1)/2, which is 6 on a 14px bar), an 8px minimum
; thumb, fit*track/total for its height and scroll*track/total for its top,
; and the same clamp keeping its bottom inside the track.
;
; What went with sc_thumb and sc_trackh is ~120 bytes of arithmetic that had
; to agree with six other copies by hand.
; -----------------------------------------------------------------------------
sc_sbset:
    push ax
    mov ax, [sc_sbr]
    sub ax, SC_SB_W-1
    mov [sc_sb+0], ax
    mov ax, [sc_sbr]
    mov [sc_sb+4], ax
    mov ax, [sc_ty]
    mov [sc_sb+2], ax
    mov ax, [sc_sbb]
    mov [sc_sb+6], ax
    mov ax, [sc_drows]
    mov [sc_sb+8], ax
    mov ax, [sc_vrows]
    mov [sc_sb+10], ax
    mov ax, [sc_top]
    mov [sc_sb+12], ax
    mov bx, sc_sb
%ifdef OS88UI_SBDRAG
    call os88ui_sbfix           ; ...and while a thumb drag is live, word 6 is
%endif                          ; the HAND's row (SPEC.md 13.10.5.6)
    pop ax
    ret

sc_sb:      dw 0,0,0,0,0,0,0
sc_sbmoved: db 0                ; sc_sbclick's SECOND answer (SPEC.md 27.7.10):
                                ; CF says the bar took the click, this says
                                ; whether the VIEW moved because of it
%ifdef OS88UI_SBDRAG
sc_nodrag:  db 0                ; 0xFF = this kernel has no tracking edge
%endif                          ; (SPEC.md 13.8.2/13.10.7.1)

; -----------------------------------------------------------------------------
; sc_sbar - draw the scroll bar
; in:  SI = window ptr, sc_bounds run, gfx lock held
; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
sc_sbar:
    push ax
    push bx
    call sc_sbset
    call os88ui_sbar
    mov ax, [sc_top]                ; remember what is on screen, so a redraw
    mov [sc_sbtop], ax              ; that moved neither number draws nothing
    mov ax, [sc_drows]
    mov [sc_sbrows], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_sbcheck - redraw the bar only if it would look different
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
sc_sbcheck:
    push ax
    push bx
    mov ax, [sc_drows]
    cmp ax, [sc_sbrows]
    jne .full                       ; the TOTAL moved, so the thumb's height
    mov ax, [sc_top]                ; did: only a full draw can resize it
    cmp ax, [sc_sbtop]
    je .out                         ; neither moved: draw nothing at all
    call sc_sbset                   ; BX = the block, holding the NEW pos...
    mov ax, [sc_sbtop]              ; ...and this is where the thumb is DRAWN
    call os88ui_sbmove              ; THREE drawing calls against the bar's
                                    ; sixteen (SPEC.md 13.10.3): a scroll moves
                                    ; neither total nor fit, so the frame, both
                                    ; rules, both arrow glyphs and all of the
                                    ; track the thumb did not cover are exactly
                                    ; where they were. This is the half of
                                    ; sharing that is not about bytes, and it
                                    ; is ~10 ms a scroll on the target machine
    mov ax, [sc_top]
    mov [sc_sbtop], ax
    jmp short .out
.full:
    call sc_sbar
.out:
    pop bx
    pop ax
    ret

%ifdef OS88UI_SBDRAG
; -----------------------------------------------------------------------------
; sc_ondrag / sc_onup - the thumb gesture's other two edges (SPEC.md 13.10.5)
; in:  CX = x, DX = y (ABSOLUTE), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; **THE THUMB'S ALONE.** This app has no other use for either edge - its bar
; buttons and its menus are sc_mtrack's, a modal poll inside W_ONCLICK - so
; these two do the drag and nothing else, and a release that arrives with no
; drag live falls straight out. That disjointness is the whole of why 13.7's
; no-mixing rule is satisfied here (SPEC.md 13.10.6.4).
;
; sc_bounds first: the block's rect and both counts come from the live record,
; and this window is resizable.
; -----------------------------------------------------------------------------
sc_ondrag:
    push ax
    push bx
    push cx
    push dx
    call os88ui_sbdragging
    jc sc_sbd_out
    call sc_bounds
    call sc_sbset               ; BX = the block; DX is still the pointer's y
    call os88ui_sbtrack         ; CF = 1: nothing owed - the rate, or the same
    jc sc_sbd_out               ; row
    jmp short sc_sbd_go
sc_onup:
    push ax
    push bx
    push cx
    push dx
    call os88ui_sbdragging
    jc sc_sbd_out
    call sc_bounds
    call sc_sbset
    call os88ui_sbdrop
    jc sc_sbd_out
sc_sbd_go:                      ; FLAT labels: the two entries share one tail,
                                ; and a local one belongs to whichever
                                ; non-local label preceded it (13.10.7.4)
    call sc_scrollto            ; AX = the row the hand is on, SI = the window
    jc sc_sbd_out               ; an end stop: not one pixel changes
    call sc_redraw
sc_sbd_out:
    ; STKBALANCE-OK: a shared EXIT label, not a routine. sc_sbdrag above
    ; pushes these four and every one of its refusals jumps here to shed
    ; them, so the walk sees four pops and no pushes - the pushes are in a
    ; caller it cannot follow back to.
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; sc_sbclick - a press inside the scroll bar
; in:  CX = x, DX = y (absolute), sc_bounds run, gfx lock held
; out: CF=0 the bar took it (and may have scrolled), CF=1 it was not ours;
;      preserves all registers
;
; Zones are the Disk window's (SPEC.md 22): the two arrow cells step a row,
; the track pages against the thumb, and the thumb itself does nothing -
; there is no drag, here or there.
; -----------------------------------------------------------------------------
; sc_sbhit - is the click at (CX, DX) inside the scroll bar's rect?
; out: CF = 0 = yes; preserves every register
;
; Its own routine because sc_onclick asks the same question for a different
; reason - whether this click needs the note's height to be EXACT - and two
; copies of a rect are two things that have to agree about where the bar is.
sc_sbhit:
    push ax
    mov ax, [sc_sbr]
    sub ax, SC_SB_W-1
    cmp cx, ax
    jb .no
    cmp dx, [sc_ty]
    jb .no
    cmp dx, [sc_sbb]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

sc_sbclick:
    push ax
    push bx
    push dx
    mov byte [sc_sbmoved], 0        ; ...until something actually moves
    call sc_sbhit
    jc .no
    call sc_sbset                   ; the SHARED element's parts (SPEC.md
    call os88ui_sbhit               ; 13.10) - BX = the block sc_sbset just
                                    ; filled, and CX/DX are still the point,
                                    ; absolute, as it takes them
    cmp al, OS88UI_SBUP
    je .lineup
    cmp al, OS88UI_SBDOWN
    je .linedn
    cmp al, OS88UI_SBPGUP
    je .pageup
    cmp al, OS88UI_SBPGDN
    je .pagedn
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB          ; SPEC.md 13.10.5: the thumb is DRAGGED
    jne .yes                        ; now. BX is still the block and DX still
    cmp byte [sc_nodrag], 0         ; the press, absolute
    jne .yes
    mov al, SC_SBRATE
    call os88ui_sbgrab
%endif
    jmp short .yes                  ; the thumb itself, or an inert track
.lineup:
    mov ax, [sc_top]
    sub ax, SC_SB_STEP
    jmp short .set
.linedn:
    mov ax, [sc_top]
    add ax, SC_SB_STEP
    jmp short .set
.pageup:
    mov ax, [sc_top]
    sub ax, [sc_vfit]               ; a page is the rows GUARANTEED visible
    jmp short .set                  ; (= vrows while uniform, SPEC.md 68.6)
.pagedn:
    mov ax, [sc_top]
    add ax, [sc_vfit]
.set:
    ; [sc_drows] is a LOWER BOUND while the count is unfinished (SPEC.md
    ; 27.7.4), so sc_scrollmax would clamp this short of a row that really
    ; exists. Only a request that reaches past the counted extent needs the
    ; exact total - every other one is answered by what is already known, and
    ; every one of them used to pay for a full walk (SPEC.md 27.7.6).
    cmp byte [sc_hdirty], 0
    je .doset
    push ax                         ; the row being asked for...
    call sc_scrollmax               ; ...against the furthest one counted so far
    mov bx, ax                      ; (BX is this routine's own, saved above)
    pop ax
    cmp ax, bx
    jbe .doset
    push si                         ; SI is the CALLER's - sc_onclick has more
    mov si, [sc_win]                ; to do with it after this returns
    call sc_height
    pop si
.doset:
    call sc_scrollto                ; CF = 1: an END STOP - it did not move,
    jc .yes                         ; so there is nothing to draw
    mov byte [sc_sbmoved], 1
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Scrolling the PIXELS (SPEC.md 27.7.2)
;
; A scroll used to be a full repaint: white-fill the content and letter every
; visible row. But moving the view by d rows changes only d rows of what is on
; screen - the rest is the same text at a different y, which is exactly what
; OSAPI_GFX_SCROLL moves. So the view moves with one blit and d rows of glyphs
; instead of nineteen, and on a 4.77MHz machine that is the difference between
; a scroll bar that steps and one that redraws.
;
; What makes it safe is that the blit shifts the PIXELS and sc_shiftrows
; shifts their DESCRIPTION - sc_sig and sc_rows - by the same d, in the same
; operation. SPEC.md 27.7 says those two arrays are dropped rather than
; adjusted on a scroll, and that was right while the pixels were being
; redrawn wholesale: adjusting would have been a second place that has to
; agree about what a row is. Here there is no second place. The pixels and
; the arrays move together or not at all, and [sc_ptop] - the [sc_top] the
; screen was drawn for - is the one fact that says which.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_vshift - move the whole text band DI pixels (signed; positive = text up)
; in:  DI = the signed pixel distance, sc_bounds run, gfx lock held
; out: CF = OSAPI_GFX_SCROLL's answer; preserves all registers
;
; The x span is rounded OUTWARD to byte columns, and that is what lets this
; work on every adapter rather than only where OSAPI_WM_SNAP aligns the
; content (SPEC.md 11.94). Rounding x1 DOWN stays inside the content because
; SC_MARGIN is 8 and the rounding moves it at most 7; rounding x2+1 UP reaches
; at most seven columns into the scroll bar, which sc_sbar redraws
; immediately afterwards because the thumb has moved anyway. The band
; therefore CONTAINS every glyph pixel, which the break's sc_scroll - rounding
; inward, and needing [sc_tx] aligned for it - does not have to.
; -----------------------------------------------------------------------------
sc_vshift:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [sc_tx]
    and ax, 0xFFF8                  ; x1, down to a byte column
    mov bx, [sc_ty]                 ; y1
    mov cx, [sc_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                          ; x2, with x2+1 up to a byte column
    cmp byte [sc_hasfmt], 0         ; formatted rows land at arbitrary ys, so
    je .yuni                        ; the band is the whole [sc_ty..sc_bot] -
    mov dx, [sc_bot]                ; legal because the formatted scroll path
    jmp short .yok                  ; erases its exposed band to [sc_bot] too
.yuni:
    mov dx, [sc_vrows]              ; y2 stops at the bottom of the last WHOLE
    push cx                         ; row, not at [sc_bot]: a content height
    mov cl, 3                       ; that is not a multiple of 8 leaves a
    shl dx, cl                      ; sliver below it, sc_rflush refuses to
    pop cx                          ; draw a row that would cross it, and so
    add dx, [sc_ty]                 ; nothing would ever erase what the blit
    dec dx                          ; pushed into it. Scrolled to [sc_bot] it
    cmp dx, [sc_bot]                ; showed as a one-pixel band of the row
    jbe .yok                        ; above's descenders, left behind for good
    mov dx, [sc_bot]                ; - and only on a window whose content
.yok:                               ; height has a remainder, which is why VGA
    mov si, di                      ; was clean and Hercules was not
    call OSAPI_GFX_SCROLL
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; POP does not touch flags: CF is still
                                    ; the scroll's answer

; -----------------------------------------------------------------------------
; sc_shiftrows - move sc_sig and sc_rows by [sc_sdlt] rows
; in:  [sc_sdlt] = the signed row delta, |d| < [sc_vrows]
; out: nothing; preserves all registers
;
; Entry r must end up holding what entry r+d held, because the pixels of row
; r+d are now at row r. The slots with no source are the exposed rows, and the
; pass that letters them rewrites both arrays for exactly those.
; -----------------------------------------------------------------------------
sc_shiftrows:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                          ; both arrays are ours (SPEC.md 20.1)
    mov ax, [sc_sdlt]
    or ax, ax
    js .up
    mov cx, [sc_vrows]              ; DOWN: dst r, src r+d, ascending
    sub cx, ax
    jbe .out
    mov bx, ax
    shl bx, 1                       ; BX = d in bytes
    push cx
    mov di, sc_sig
    mov si, di
    add si, bx
    cld
    rep movsw
    pop cx
    push cx
    mov di, sc_rows
    mov si, di
    add si, bx
    rep movsw
    pop cx
    mov di, sc_ryb                  ; ...and the ys, which ride the same shift
    mov si, di                      ; (SPEC.md 68.6)
    add si, bx
    rep movsw
    jmp short .adj
.up:
    neg ax                          ; UP: dst r+|d|, src r, DESCENDING, or the
    mov cx, [sc_vrows]              ; copy would overwrite its own source
    sub cx, ax
    jbe .out
    mov bx, [sc_vrows]
    dec bx
    shl bx, 1                       ; BX = the last row's byte offset
    mov di, bx
    sub bx, ax
    sub bx, ax                      ; ...and BX = that minus |d| words
    mov si, bx
    push cx
    push si
    push di
    add si, sc_sig
    add di, sc_sig
    std
    rep movsw
    cld
    pop di
    pop si
    pop cx
    push cx
    push si
    push di
    add si, sc_rows
    add di, sc_rows
    std
    rep movsw
    cld
    pop di
    pop si
    pop cx
    add si, sc_ryb
    add di, sc_ryb
    std
    rep movsw
    cld
.adj:
    ; the blit moved every surviving row's pixels by [sc_sdpx], so their
    ; banked ys move with them - or the next pass 1 would call every row
    ; "moved" and repaint the view it just blitted
    mov ax, [sc_sdpx]
    mov cx, [sc_vrows]
    jcxz .out
    mov si, sc_ryb
.aj:
    sub [si], ax
    add si, 2
    loop .aj
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_scrollpaint - move the view with a blit instead of a repaint
; in:  SI = window ptr, sc_bounds run, gfx lock held, [sc_top] ALREADY moved,
;      [sc_dr0]/[sc_dr1] = whatever rows the caller found dirty in the frame
;      the screen is still showing (0xFFFF/0 = none)
; out: CF=0 the screen is correct and both arrays describe it; CF=1 nothing
;      was drawn and the caller must repaint in full. Preserves all registers.
; -----------------------------------------------------------------------------
sc_scrollpaint:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [sc_sigok], 0
    je .nope                        ; the arrays do not describe the screen,
                                    ; so there is nothing to shift
    cmp word [sc_vrows], 2
    jb .nope
    mov ax, [sc_top]
    sub ax, [sc_ptop]               ; AX = d, signed
    jz .nope                        ; the view did not actually move
    mov [sc_sdlt], ax
    mov bx, ax
    or bx, bx
    jns .abs
    neg bx
.abs:
    cmp bx, [sc_vrows]
    jae .nope                       ; nothing is retained: a repaint is the
                                    ; same work without the blit
    cmp byte [sc_hasfmt], 0
    je .pxuni
    or ax, ax                       ; formatted (SPEC.md 68.6): an UP scroll's
    js .nope                        ; entering rows have unknown heights -
                                    ; the full repaint is the honest path
    cmp byte [sc_ymoved], 0         ; ...and so is a scroll riding an edit
    jne .nope                       ; that MOVED rows: pass 1 has already
                                    ; rewritten the banked ys to the new
                                    ; layout, so they no longer describe the
                                    ; glass the blit would move
    cmp byte [sc_rowsok], 0
    je .nope
    cmp ax, [sc_rowsn]
    ja .nope                        ; ryb[d-1] must be a row the glass shows
    mov di, ax
    dec di
    shl di, 1
    mov di, [di+sc_ryb]
    add di, 8                       ; band top of row d - band top of row 0:
    sub di, [sc_ty]                 ; exactly the pixels the retained rows
    jle .nope                       ; move up by
    jmp short .pxhave
.pxuni:
    mov di, ax
    push cx
    mov cl, 3
    shl di, cl                      ; DI = d*8, and a positive d moves the
    pop cx                          ; view down, which moves the text UP
.pxhave:
    mov [sc_sdpx], di               ; sc_shiftrows adjusts the shifted ys by
                                    ; this (SPEC.md 68.6)
    call sc_vshift
    jc .nope                        ; refused, and having drawn nothing

    ; Rounding x2+1 outward carried up to seven columns of furniture with the
    ; text: the scroll bar's left frame at sc_rgt+1, and the left edge of the
    ; grow box below it. Blank that strip and let the two things that own it
    ; put themselves back - sc_sbar at the end of this routine, and the grow
    ; box here, because sc_sbar stops short of the corner it sits in.
    mov ax, [sc_rgt]
    inc ax                          ; x1, the first column past the text
    mov cx, [sc_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                          ; x2, the same one sc_vshift moved
    cmp ax, cx
    ja .nostrip
    mov bx, [sc_ty]
    mov dx, [sc_bot]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push bx
    mov bx, si
    call OSAPI_WM_GROW              ; SPEC.md 11.1/27
    pop bx
.nostrip:

    mov ax, [sc_sdlt]               ; the rows the blit did not fill in
    or ax, ax
    js .exup
    cmp byte [sc_hasfmt], 0
    je .exdn8
    ; formatted (SPEC.md 68.6): the vacated pixels are the bottom sdpx of
    ; the band, and the rows that own them are found from the PRE-shift
    ; banks - the first retained row whose old glyph crossed the bottom
    ; edge (it was clipped then, it is drawable now), else the first row
    ; past the glass. In ROW terms that is where redrawing must start;
    ; everything below it is redrawn to the view's end.
    mov bx, [sc_rowsn]
    cmp bx, [sc_vrows]
    jbe .fdl
    mov bx, [sc_vrows]
.fdl:
    mov cx, ax                      ; CX = the candidate pre-shift row, from d
.fds:
    cmp cx, bx
    jae .fdnone
    mov di, cx
    shl di, 1
    mov di, [di+sc_ryb]
    add di, 7
    cmp di, [sc_bot]
    ja .fdhit
    inc cx
    jmp short .fds
.fdhit:
.fdnone:
    sub cx, ax                      ; ...as a post-shift visible row
    jns .fd0
    xor cx, cx
.fd0:
    mov [sc_bd0], cx
    mov cx, [sc_vrows]
    dec cx
    mov [sc_bd1], cx
    jmp short .band
.exdn8:
    mov bx, [sc_vrows]              ; view moved DOWN: they are at the bottom
    sub bx, ax
    mov [sc_bd0], bx
    mov bx, [sc_vrows]
    dec bx
    mov [sc_bd1], bx
    jmp short .band
.exup:
    mov word [sc_bd0], 0            ; ...and UP: at the top
    neg ax
    dec ax
    mov [sc_bd1], ax
.band:
    ; ...plus whatever the caller already knew was dirty, which it counted in
    ; the OLD frame. A caret that moved off a row leaves that row needing a
    ; redraw even though the blit carried it faithfully - so an Up that
    ; scrolls has TWO dirty rows, the one it arrived on and the one it left.
    mov ax, [sc_dr0]
    cmp ax, 0xFFFF
    je .shift
    sub ax, [sc_sdlt]
    jns .d0ok
    xor ax, ax                      ; it half scrolled off the top
.d0ok:
    mov bx, [sc_dr1]
    sub bx, [sc_sdlt]
    js .shift                       ; ...or all of it did
    cmp bx, [sc_vrows]
    jb .d1ok
    mov bx, [sc_vrows]
    dec bx
.d1ok:
    cmp ax, bx
    ja .shift
    cmp ax, [sc_bd0]
    jae .hi
    mov [sc_bd0], ax
.hi:
    cmp bx, [sc_bd1]
    jbe .shift
    mov [sc_bd1], bx
.shift:
    call sc_shiftrows

    cmp byte [sc_hasfmt], 0         ; erase the band: OSAPI_GFX_SCROLL leaves
    je .eruni                       ; the vacated rows holding a copy of what
    mov bx, [sc_bot]                ; was next to them. Formatted: the blit
    sub bx, [sc_sdpx]               ; moved [ty..bot] up by sdpx, so EXACTLY
    inc bx                          ; the bottom sdpx pixels are vacated -
    cmp bx, [sc_ty]                 ; derived from the delta itself, never
    jae .erf                        ; from a blank row's banked y (a garbage
    mov bx, [sc_ty]                 ; bank once erased the window's own
.erf:                               ; chrome through this fill)
    mov dx, [sc_bot]
    jmp short .erhave
.eruni:
    mov ax, [sc_bd0]
    push cx                         ; the vacated rows holding a copy of what
    mov cl, 3                       ; was next to them, and a row of the new
    shl ax, cl                      ; text may be shorter than that or empty
    pop cx
    add ax, [sc_ty]
    mov bx, ax                      ; BX = y1
    mov ax, [sc_bd1]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]
    add ax, 7
    cmp ax, [sc_bot]
    jbe .yok
    mov ax, [sc_bot]
.yok:
    mov dx, ax                      ; DX = y2
.erhave:
    mov ax, [sc_tx]
    sub ax, SC_MARGIN               ; AX = x1, the content's own left edge
    mov cx, [sc_rgt]                ; CX = x2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [sc_prowi], 0xFFFF     ; the fill erased what the delta cache knew

    mov word [sc_hity], 0xFFFF      ; one pass, drawing AND re-signing: the
    mov word [sc_wanty], 0x7FFF     ; band was just filled, so sc_clean, and
    mov ax, [sc_bd0]                ; sc_clip confines it to the band by ROW
    mov [sc_dr0], ax                ; rather than by signature - an exposed
    mov ax, [sc_bd1]                ; row's old signature is the row that
    mov [sc_dr1], ax                ; scrolled away and could match by luck
    mov byte [sc_draw], 1
    mov byte [sc_sigup], 1
    mov byte [sc_clip], 1
    mov byte [sc_clean], 1
    cmp byte [sc_hasfmt], 0         ; the formatted band includes retained
    je .clok                        ; rows ABOVE the erased pixels: their
    mov byte [sc_clean], 0          ; runs must pad, or an old caret bar past
.clok:                              ; the text survives (SPEC.md 68.6)
    mov byte [sc_resume], 0
    cmp byte [sc_rowsok], 0
    je .xseed
    mov ax, [sc_bd0]
    or ax, ax
    jz .xseed                       ; the band starts at the top of the view:
    dec ax                          ; there is no earlier row to start from
    mov bx, ax                      ; sc_rows is valid up to here and no
    inc bx                          ; further, the entries above the band
    mov [sc_rowsn], bx              ; having just been shifted out of range
    mov dx, [sc_vrows]
    call sc_seedrow
    cmp byte [sc_resume], 0
    jne .noseed
.xseed:
    mov ax, [sc_top]                ; ...and the ROW INDEX has one (SPEC.md
    mov dx, [sc_vrows]              ; 27.13). This is every scroll UPWARD: the
    call sc_xseed                   ; exposed row is row 0 of the view, so
                                    ; there is nothing above it in sc_rows and
                                    ; the walk went back to index 0 to letter
                                    ; ONE row - 335 ms of a 640 ms Up
.noseed:
    mov ax, [sc_bd1]                ; STOP AT THE BAND, not at the bottom of
    mov [sc_lastrow], ax            ; the view (SPEC.md 27.13). Nothing below
                                    ; sc_bd1 is drawn - sc_clip says so - and
                                    ; nothing below it needs LAYING OUT
                                    ; either: OSAPI_GFX_SCROLL moved those
                                    ; pixels and sc_shiftrows moved sc_sig and
                                    ; sc_rows by the same d, so their
                                    ; descriptions already match the glass.
                                    ; Walking to the view's bottom to draw one
                                    ; exposed row was 20 rows for 1 - 333 ms of
                                    ; a 520 ms Up
    call sc_walk
    mov byte [sc_clip], 0
    mov byte [sc_clean], 0

    mov ax, [sc_top]
    mov [sc_ptop], ax               ; the screen shows this view now
    call sc_sbar                    ; unconditional: the thumb moved, and the
                                    ; blit reached into the bar's columns
    clc
    jmp short .out
.nope:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ctcalc - the live chrome top: menu bar + the strips View has switched on
; out: [sc_ctop] refreshed; preserves all registers (SPEC.md 68.2)
; -----------------------------------------------------------------------------
sc_ctcalc:
    push ax
    mov ax, SC_MENU_H
    cmp byte [sc_vrib], 0
    je .norib
    add ax, SC_RIBBON_H
.norib:
    cmp byte [sc_vrul], 0
    je .norul
    add ax, SC_RULER_H
.norul:
    mov [sc_ctop], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_bounds - the content rectangle and the text origin, from the live record
; in:  SI = window ptr, ES = KERNEL_SEG (as every callback is entered)
; out: [sc_tx] = the wrap column and left margin, [sc_ty] = the first text
;      row, [sc_rgt]/[sc_bot] = the content's inclusive right and bottom;
;      preserves all registers
;
; A resizable window lays out from the record every time (SPEC.md 11.1), and
; BOTH passes of sc_walk need the same four numbers, so they are read once
; here rather than twice in slightly different words.
; -----------------------------------------------------------------------------
sc_bounds:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [sc_cl], ax                 ; the content box, banked for every chrome
    mov [sc_ct], dx                 ; painter and hit test (SPEC.md 68.2) -
                                    ; read here once, exactly like sc_tx/sc_ty
    push ax
    push dx
    add ax, SC_MARGIN
    mov [sc_tx], ax
    call sc_ctcalc                  ; [sc_ctop] = the LIVE chrome top from the
    add dx, [sc_ctop]               ; View toggles (SPEC.md 68.2): menu bar,
                                    ; then ribbon and ruler if shown. (The
                                    ; find panel that used to dock here is
                                    ; gone - Search is a dialog, SPEC.md 68.7)
    add dx, SC_MARGIN               ; sc_bounds's own geometry test below
    mov [sc_ty], dx                 ; catches a chrome-top change and
                                    ; sc_sigsame turns it into a full repaint
    call OSAPI_WM_GEOM              ; CX/DX = content w/h (BX still the window)
    mov [sc_cw], cx                 ; banked beside sc_cl/sc_ct
    mov [sc_ch], dx
    pop ax                          ; content top
    add ax, dx
    dec ax                          ; the content's own last row...
    cmp byte [sc_vsta], 0           ; ...less the status strip when it is
    je .nosta                       ; shown (SPEC.md 68.2): the text band ends
    sub ax, SC_STATUS_H             ; above it, and the kernel's grow box
.nosta:                             ; lives in the strip's right corner where
    mov [sc_bot], ax                ; the bar already stopped clear of it
    pop ax                          ; content left
    add ax, cx
    dec ax
    mov [sc_sbr], ax                ; ...and the last drawable column.
    sub ax, SC_SB_W                 ; The scroll bar owns the rightmost
                                    ; SC_SB_W of it (SPEC.md 27.7). This was
    call sc_pgcol                   ; Page view narrows the column to the SHEET
                                    ; before anything derives from it
    mov [sc_rgt], ax                ; W_X+W_W-2 read off the record through ES;
                                    ; origin + size - 1 is the same pixel and
                                    ; needs no kernel pointer of our own - and
                                    ; it stays right under WF_FULL, where the
                                    ; frame IS the content (SPEC.md 11.2)

    mov ax, [sc_rgt]                ; whole CELLS between the pen and the right
    sub ax, [sc_tx]                 ; edge: the width of one opaque run, and
    inc ax                          ; what the row buffer is padded to
    jns .cok
    xor ax, ax
.cok:
    mov cl, 3
    cmp byte [sc_pxon], 0           ; ...and a CELL is 8 pixels in the kernel's
    je .cshr                        ; cell and as little as TY_MINADV in a
    mov cl, 2                       ; chosen face (SPEC.md 6.4), so a row of
                                    ; narrow glyphs holds more cells than it
                                    ; holds byte columns. SC_MAXCOL was sized
                                    ; from that floor and not from the face -
                                    ; the face comes off a floppy while the
                                    ; constant is in the tree (SPEC.md 68.13)
.cshr:
    shr ax, cl
    cmp ax, SC_MAXCOL - 1
    jbe .csave
    mov ax, SC_MAXCOL - 1
.csave:
    mov [sc_rcols], ax

    mov ax, [sc_bot]                ; ...and how many whole 8px rows that is,
    sub ax, [sc_ty]                 ; which is what the signature array is
    jc .norows                      ; indexed by (SPEC.md 27.2)
    cmp ax, 7
    jb .norows
    sub ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    inc ax
    cmp ax, SC_MAXROWS
    jbe .vok
    mov ax, SC_MAXROWS
.vok:
    mov [sc_vrows], ax
    jmp short .sbb
.norows:
    mov word [sc_vrows], 0
.sbb:
    mov ax, [sc_vrows]              ; [sc_vfit]: rows GUARANTEED to fit the
    cmp byte [sc_hasfmt], 0         ; band. Uniform rows: all of them. With
    je .vfok                        ; formats: band/24, the tallest row being
    mov ax, [sc_bot]                ; double spacing + open space (SPEC.md
    sub ax, [sc_ty]                 ; 65.6) - conservative, so sc_seecaret's
    jc .vf0                         ; target row is visible by construction
    inc ax
    push dx
    push cx
    xor dx, dx
    mov cx, 24
    div cx
    pop cx
    pop dx
    or ax, ax
    jnz .vfok
.vf0:
    mov ax, 1
.vfok:
    mov [sc_vfit], ax
    mov ax, [sc_bot]                ; the bar's own bottom, clear of the corner
    sub ax, SC_GROW                 ; the kernel draws the grow box in
    mov [sc_sbb], ax
    call sc_hguess                  ; ...and now the geometry is known, what the
                                    ; note's LENGTH already says about its
                                    ; height (SPEC.md 27.7.3)
.geom:
    ; The checkpoint and sc_rows are ROW INDICES, so they mean nothing under a
    ; different geometry - and unlike the signatures, nothing else was going to
    ; notice. sc_sigsame guards sc_redraw; this guards everything else, which
    ; is every caret key and every click.
    ; ...and what "a different geometry" means here is the WRAP WIDTH and the
    ; view HEIGHT, never the origin. These four words are the content box in
    ; SCREEN coordinates, so dragging the window changes sc_tx and sc_ty while
    ; the note wraps identically - and comparing them absolutely made every
    ; MOVE set [sc_gchg], which sc_paint pays with sc_measure: an unbounded
    ; walk of the whole note. On a 16KB file that is seconds of a window that
    ; has been dropped and is not yet drawing, reported as exactly that. The
    ; comment below is a RESIZE argument and always was.
    mov ax, [sc_rgt]
    sub ax, [sc_tx]                 ; the wrap width now...
    mov dx, [sc_srgt]
    sub dx, [sc_stx]                ; ...against the one the screen was laid
    cmp ax, dx                      ; out under
    jne .stale
    mov ax, [sc_bot]
    sub ax, [sc_ty]                 ; ...and the view height, which decides how
    mov dx, [sc_sbot]               ; many of those rows fit and so whether the
    sub dx, [sc_sty]                ; view is looking past the end
    cmp ax, dx
    je .out
.stale:
    mov byte [sc_ckok], 0
    mov byte [sc_rowsok], 0
    mov byte [sc_gchg], 1           ; ...and the note is a different NUMBER of
                                    ; rows under a different wrap width, so
                                    ; the view can be looking past the end of
                                    ; it. Only a walk knows how many, so this
                                    ; says "one is owed" and sc_paint pays it
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Paragraph layout state (SPEC.md 68.3/68.6)
;
; The walk carries the GOVERNING paragraph's decoded format beside the pen:
; the format lives on the ¶ that ENDS the paragraph, so entering a paragraph
; scans forward to its mark once (each byte of the walked span is therefore
; read at most twice, which is the price of Word's own arrangement). While
; [sc_hasfmt] is clear - every document until a format is applied - none of
; this runs and the walk is byte-for-byte the uniform engine it always was.
;
; Heights: a row's advance is 8/12/16px from the line spacing, +8 before an
; open paragraph's first row. BP stays the GLYPH y (the 8px band every
; drawing routine already reads); the row's BAND is [sc_rbandt .. BP+7], and
; sc_ryb banks each visible row's glyph y so pass 1 can see a row that moved
; without its text changing, and a seeded walk can resume at an exact y.
; Rows above the view all park at the ty-8 sentinel - sc_rflush's y test
; already refuses them, and their exact y answers no query.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_papat - the governing PAP index at character index AX
; out: AL = it (AH preserved); preserves every other register
; The scan is one repne scasb to the paragraph's own ¶ - O(paragraph), and
; only when [sc_hasfmt] says a non-Normal format exists at all.
; -----------------------------------------------------------------------------
sc_papat:
    push bx
    push cx
    push di
    push es
    cmp byte [sc_hasfmt], 0
    je .zero
    mov cx, [sc_len]
    sub cx, ax
    jbe .tail                       ; at/past the end: the tail paragraph
    mov es, [sc_dseg]
    mov di, ax
    push ax
    mov al, 13
    cld
    repne scasb
    pop ax
    jne .tail
    mov es, [sc_cseg]               ; the ¶'s CHP byte IS the index
    mov bx, di
    dec bx
    mov al, [es:bx]
    jmp short .out
.tail:
    mov al, [sc_pap_tail]
    jmp short .out
.zero:
    xor al, al
.out:
    pop es
    pop di
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_pback - the start of the paragraph containing index AX
; out: AX = the index after the previous ¶ (or 0); preserves all others
; -----------------------------------------------------------------------------
sc_pback:
    push bx
    push es
    mov es, [sc_dseg]
    mov bx, ax
.l:
    or bx, bx
    jz .have
    cmp byte [es:bx-1], 13
    je .have
    dec bx
    jmp short .l
.have:
    mov ax, bx
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_papload - decode dictionary entry AL into the walk's sc_w* state
; preserves all registers. Clamped against the LIVE geometry: at least one
; cell survives every combination of indents, and the clamped pens stay on
; byte columns so the single-store paths survive (SPEC.md 68.1).
; -----------------------------------------------------------------------------
sc_papload:
    push ax
    push bx
    push cx
    push dx
    push es
    mov [sc_wpap], al
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov es, [sc_pseg]
    mov dl, [es:bx]                 ; DL = the packed byte
    mov al, dl
    and al, SCPA_ALIGN
    mov [sc_walign], al
    mov al, dl                      ; advance = 8 + spacing*4, and the packed
    and al, SCPA_SPACE              ; field is already spacing << 2
    add al, [sc_ghb]                ; ...over the FACE's own row advance, which
    mov [sc_wadv], al               ; is 8 for the kernel's cell (SPEC.md 68.13)
    xor al, al
    test dl, SCPA_SB
    jz .nosb
    inc al
.nosb:
    mov [sc_wsb], al
    mov al, [es:bx+3]               ; right indent -> the effective right edge
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mov dx, [sc_rgt]
    sub dx, ax
    mov ax, [sc_tx]
    add ax, 7
    cmp dx, ax
    jge .rok
    mov dx, ax                      ; degenerate window: one cell stands
.rok:
    mov [sc_wrgt], dx
    mov al, [es:bx+1]               ; left indent px, clamped to keep a cell
    xor ah, ah
    shl ax, cl
    mov dx, [sc_wrgt]
    sub dx, [sc_tx]
    sub dx, 7
    jns .lc
    xor dx, dx
.lc:
    cmp ax, dx
    jbe .lok
    mov ax, dx
    and ax, 0xFFF8
.lok:
    mov [sc_wleftpx], ax
    mov dx, [sc_wrgt]               ; the fresh-row capacity, cells
    inc dx
    sub dx, [sc_tx]
    sub dx, ax
    jns .cc
    xor dx, dx
.cc:
    shr dx, cl
    or dx, dx
    jnz .c1
    inc dx
.c1:
    cmp dx, [sc_rcols]
    jbe .cok
    mov dx, [sc_rcols]
.cok:
    mov [sc_wcols], dx
    cmp byte [sc_pxon], 0           ; ...and the same capacity in PIXELS, which
    jne .wpx                        ; is what sc_wordfit's second threshold
    shl dx, cl                      ; measures now (SPEC.md 68.13). The fixed
    jmp short .wpxs                 ; face takes exactly 8 x the cells, so its
.wpx:                               ; wrap is the one it always had
    mov dx, [sc_wrgt]
    inc dx
    sub dx, [sc_tx]
    sub dx, [sc_wleftpx]
    jns .wpxs
    xor dx, dx
.wpxs:
    mov [sc_wcpx], dx
    mov al, [es:bx+2]               ; first-line, SIGNED, clamped so the pen
    cbw                             ; lands in [tx .. wrgt-7]
    shl ax, cl
    mov dx, [sc_wleftpx]
    add dx, ax
    jns .f0
    xor dx, dx
.f0:
    mov ax, [sc_wrgt]
    sub ax, [sc_tx]
    sub ax, 7
    jns .f1
    xor ax, ax
.f1:
    cmp dx, ax
    jbe .f2
    mov dx, ax
    and dx, 0xFFF8
.f2:
    sub dx, [sc_wleftpx]
    mov [sc_wfirstpx], dx
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_papload0 - Normal, without touching the dictionary: the uniform path's
; whole cost. Preserves all registers.
sc_papload0:
    push ax
    xor al, al
    mov [sc_wpap], al
    mov [sc_walign], al
    mov [sc_wsb], al
    mov al, [sc_ghb]
    mov [sc_wadv], al
    xor ax, ax
    mov [sc_wleftpx], ax
    mov [sc_wfirstpx], ax
    mov ax, [sc_rgt]
    mov [sc_wrgt], ax
    mov ax, [sc_rcols]
    mov [sc_wcols], ax
    cmp byte [sc_pxon], 0           ; the fresh-row capacity in pixels, exactly
    jne .wpx                        ; as sc_papload derives it
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    jmp short .wpxs
.wpx:
    mov ax, [sc_rgt]
    inc ax
    sub ax, [sc_tx]
    jns .wpxs
    xor ax, ax
.wpxs:
    mov [sc_wcpx], ax
    pop ax
    ret

; sc_papini - the walk is starting at [sc_i]: load the governing format and
; decide whether this row is its paragraph's first. ES = the document.
; Preserves all registers.
sc_papini:
    push ax
    push bx
    mov byte [sc_parafirst], 1
    mov bx, [sc_i]
    or bx, bx
    jz .first
    cmp byte [es:bx-1], 13
    je .first
    mov byte [sc_parafirst], 0
.first:
    cmp byte [sc_hasfmt], 0
    jne .fmt
    call sc_papload0
    jmp short .out
.fmt:
    mov ax, [sc_i]
    call sc_papat
    call sc_papload
.out:
    pop bx
    pop ax
    ret

; sc_rowhc - [sc_rowhv] = the entered row's height. Preserves all registers.
sc_rowhc:
    push ax
    push bx
    mov word [sc_rowpic], 0xFFFF    ; "this row is not a picture"
    mov bx, [sc_i]                  ; the row's FIRST character - sc_rowhc runs
    cmp bx, [sc_len]                ; at row entry, so sc_i is it
    jae .text
    cmp byte [es:bx], SC_PICCH      ; ES is the document throughout the walk
    jne .text                       ; (SPEC.md 27.6)
    call sc_picidx                  ; -> AX = the index from its CHP byte
    cmp ax, [sc_npic]
    jae .text                       ; an index this document has no picture
    mov [sc_rowpic], ax             ; for: draw it as ordinary text rather
    call sc_pich                    ; than off the end of the table
    jmp short .have
.text:
    mov al, [sc_wadv]
    xor ah, ah
    cmp byte [sc_parafirst], 0
    je .have
    cmp byte [sc_wsb], 0
    je .have
    add ax, 8                       ; the open paragraph's blank line
.have:
    mov [sc_rowhv], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The three little readers the two above share. A picture's identity is the
; CHP byte of its SC_PICCH, exactly as a paragraph's format is the CHP byte of
; its mark (SPEC.md 68.3) - so reading one means reaching into the PARALLEL
; attribute claim at the same offset, which is what sc_picidx is for.
; -----------------------------------------------------------------------------
; sc_picidx - AX = the picture index of the SC_PICCH at [sc_i]. Preserves all
; but AX. 0xFFFF if there is no character there at all.
sc_picidx:
    push bx
    push es
    mov bx, [sc_i]
    cmp bx, [sc_len]
    jae .none
    mov es, [sc_cseg]
    mov al, [es:bx]
    xor ah, ah
    jmp short .out
.none:
    mov ax, 0xFFFF
.out:
    pop es
    pop bx
    ret

; sc_picrec - BX = the table slot for picture AX, or CF=1. Preserves the rest.
sc_picrec:
    cmp ax, [sc_npic]
    jae .no
    push cx
    push ax
    mov cl, 3
    shl ax, cl                      ; * SC_PICREC
    add ax, sc_pictab
    mov bx, ax
    pop ax
    pop cx
    clc
    ret
.no:
    stc
    ret

; sc_picw / sc_pich - AX = the width (height) of the picture the SC_PICCH at
; [sc_i] names, or 8 if it names none. Preserves all but AX.
sc_picw:
    push bx
    call sc_picidx
    call sc_picrec
    jc .no
    mov ax, [bx]
    jmp short .out
.no:
    mov ax, 8
.out:
    pop bx
    ret

sc_pich:
    push bx
    call sc_picidx
    call sc_picrec
    jc .no
    mov ax, [bx+2]
    jmp short .out
.no:
    mov ax, 8
.out:
    pop bx
    ret

; sc_advy - the pen enters row [sc_row]+1: move the glyph y by the ENTERED
; row's height ([sc_rowhv]). Rows above the view park at the ty-8 sentinel.
; Preserves all registers but BP (its whole job).
sc_advy:
    push ax
    mov ax, [sc_row]
    inc ax
    js .neg
    jz .zero
    add bp, [sc_rowhv]
    jmp short .band
.zero:
    mov bp, [sc_ty]                 ; band top of row 0 is sc_ty exactly
    add bp, [sc_rowhv]
    sub bp, [sc_gh]
    jmp short .band
.neg:
    mov bp, [sc_ty]
    sub bp, [sc_gh]
.band:
    mov ax, bp
    sub ax, [sc_rowhv]
    add ax, [sc_gh]
    mov [sc_rbandt], ax
    pop ax
    ret

; the three row-advance shapes: a wrap stays in the paragraph, a newline
; enters a fresh one (whose format is scanned then), a blank row is plain
sc_advwrap:
    mov byte [sc_parafirst], 0
    call sc_rowhc
    jmp sc_advy

sc_advnl:
    push ax
    mov byte [sc_parafirst], 1
    cmp byte [sc_hasfmt], 0
    je .have
    mov ax, [sc_i]
    call sc_papat
    call sc_papload
.have:
    call sc_rowhc
    call sc_advy
    pop ax
    ret

sc_advblank:
    mov word [sc_rowhv], 8
    jmp sc_advy

; -----------------------------------------------------------------------------
; sc_tabw - the tab's width in px: to the next SC_TABSTOP-cell default stop,
;           anchored at the row's own start pen [sc_rowx0]
; in:  DI = the pen; out: AX = pixels (8..SC_TABSTOP*8); preserves all others
; -----------------------------------------------------------------------------
sc_tabw:
    push cx
    push dx
    mov ax, di
    sub ax, [sc_rowx0]              ; PIXELS consumed on this row, not cells:
    xor dx, dx                      ; a proportional row's pen is not a
                                    ; multiple of 8 and flooring it to one
    mov cx, SC_TABSTOP * 8          ; loses the remainder - the tab then lands
    div cx                          ; short of its stop by up to 7px and the
    inc ax                          ; column walks. Identical to the cell
    mul cx                          ; arithmetic this replaces whenever the pen
    sub ax, di                      ; IS a multiple of 8, which is every row of
    add ax, [sc_rowx0]              ; the fixed face
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; sc_rowmeasure - how many cells will the row starting at this pen hold?
; in:  ES:SI = the document at [sc_i], BX = chars left, DI = the row's start
;      pen (indents applied, NO alignment offset), pap state loaded
; out: AX = the cells; preserves every other register AND [sc_wstart]
;
; The centre/right offset needs the row's width BEFORE the row is laid out,
; and the walk is one pass - so the width comes from this dry run, made
; through the SAME two helpers (sc_wordfit / sc_hangsp) as the live loop so
; the two cannot drift. The offset the caller then adds cannot overflow the
; row: off <= free, and every word that fitted unoffset still fits offset
; (u + len <= cells and off + cells <= wcols, both by construction).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sc_fontscan - build the Font combo from what the MACHINE has (SPEC.md 19.8)
; in:  -
; out: the dropdown's items and its count set; preserves all registers
;
; ONCE, and lazily. ty_scan is four remounts and two listings - a couple of
; seconds on the target - so it runs the first time somebody opens this combo
; and never again. A person who never opens it pays nothing, which is the same
; bargain SPEC.md 6.2 strikes with a directory of faces nobody picks from.
;
; The dropdown is a STATIC table with room reserved (sc_it_fontc), and this
; fills the reserved records and writes the count byte in sc_mtab. A menu whose
; length is data rather than assembly is a menu that can grow when a disk
; carries more faces, without this program knowing their names.
; -----------------------------------------------------------------------------
sc_fontscan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sc_fscan], 0
    jne .out
    mov byte [sc_fscan], 1          ; once, whatever the answer - a disk with
                                    ; no FONTS/ must not be re-walked on every
                                    ; press
    call ty_scan
    mov al, [ty_nfam]
    or al, al
    jz .out
    cmp al, SC_MAXFONT
    jbe .n
    mov al, SC_MAXFONT
.n:
    mov [sc_nfont], al
    xor ch, ch
    mov cl, al
    xor bx, bx                      ; BX = the family index
.item:
    mov ax, bx
    mov si, SC_MI_SZ                ; the record for item 1 + BX: item 0 is
    mul si                          ; Pica and is assembled, not filled in
    mov di, sc_it_fontc + SC_MI_SZ
    add di, ax
    mov byte [di+0], 0              ; flags: live
    mov byte [di+1], 0              ; no mnemonic index
    mov byte [di+2], SCA_CSEL
    mov byte [di+3], 0
    mov al, bl
    call ty_famname                 ; SI = the display name the scan built
    mov [di+4], si
    mov word [di+6], 0              ; no caption
    inc bx
    loop .item

    mov al, [sc_nfont]              ; ...and the dropdown is that many items
    inc al                          ; longer than the one Pica it had
    mov [sc_mtab + SC_M_FONTC * SC_MT_SZ + 3], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_facemetrics - the three numbers the layout reads, from the current face
; in:  [sc_prop] and the library's current face
; out: [sc_gh]/[sc_gh1]/[sc_ghb]; preserves all registers
;
; AND IT FORCES [sc_hasfmt], which is the whole reason this change is small.
; That flag already means "rows are not all 8 pixels and do not land at
; sc_ty + 8*row" - it is what line spacing set - and twenty-three sites in
; this program already branch on it to a path that handles arbitrary row ys:
; the scroll band, the erase band, the row-fit count, the caret's visibility
; test. A taller face needs exactly those paths, so it raises exactly that
; flag rather than growing a second predicate beside it that every one of
; them would have had to learn.
; -----------------------------------------------------------------------------
sc_facemetrics:
    push ax
    mov ax, 8                       ; the kernel's cell, and the default
    mov [sc_gh], ax
    dec ax
    mov [sc_gh1], ax
    mov word [sc_ghb], 8
    mov word [sc_spadv], 8          ; ...and a space is 8 wide in it
    mov al, [sc_prop]               ; ...and the METRICS follow the face too
    mov [sc_pxon], al               ; (SPEC.md 68.13): one flag, read by sc_cx
    cmp byte [sc_prop], 0           ; and sc_xc, and by the two fast paths that
    je .out                         ; cannot be expressed without an 8px column
    mov al, ' '                     ; the padding cell's own width, banked: the
    call ty_advof                   ; row buffer pads with spaces and sc_pxcell
    xor ah, ah                      ; extrapolates the caret's parking cell by
    mov [sc_spadv], ax              ; exactly this
    call ty_getrows
    xor ah, ah
    mov [sc_gh], ax
    dec ax
    mov [sc_gh1], ax
    call ty_getrows
    xor ah, ah
    mov [sc_ghb], ax
    call ty_getlead
    xor ah, ah
    add [sc_ghb], ax
    cmp word [sc_ghb], 8            ; ...and only a face whose row advance is
    je .out                         ; not 8 needs the non-uniform-ROW paths. An
    mov byte [sc_hasfmt], 1         ; 8-row face still lands every row where it
.out:                               ; always did - what its glyphs no longer do
                                    ; is land on the 8-pixel COLUMN, and that
                                    ; is [sc_pxon]'s business above and not
                                    ; this flag's (SPEC.md 68.13)
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_facedrop - give back the face this document had open, if any
; in:  [sc_face]; out: it is 0; preserves all registers
; -----------------------------------------------------------------------------
sc_facedrop:
    push ax
    mov al, [sc_face]
    or al, al
    jz .out
    call ty_close
    mov byte [sc_face], 0
    xor ax, ax                      ; ...and the CURRENT face goes back to the
    call ty_use                     ; kernel's 8x8, so nothing is left pointing
.out:                               ; at a slot that has just been freed
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_a_csel - a combo entry was chosen (SPEC.md 68.13)
; in:  [sc_pickm] = which combo, [sc_picki] = which entry
; out: the Font combo's caption follows the choice; the others are cosmetic
;
; ITEM 0 IS PICA - the kernel's 8x8 cell, which is what this program has
; always set text in and is a perfectly good answer. Items 1.. are the faces
; FONTS/ was carrying. Choosing one opens it and names it in the ribbon.
;
; WHAT IT DOES NOT DO YET is set [sc_prop]: the document still draws through
; the 8x8 cell until sc_drawrun grows its band arm (SPEC.md 68.13), and a
; half-converted Word - drawing proportionally while measuring on the 8-pixel
; grid - would put the caret in the wrong place on every line. So the choice
; is recorded and the face is opened, and the pixels follow when the rest of
; 65.13 lands.
; -----------------------------------------------------------------------------
sc_a_csel:
    push ax
    push bx
    push si                         ; SI IS THE WINDOW POINTER AND MUST SURVIVE
    push di                         ; (sc_mfire's contract: "SI survives every
                                    ; action"). ty_famname ANSWERS in SI, so
                                    ; without this the caller is handed a name
                                    ; string where a window record belongs and
                                    ; the machine follows it into the weeds -
                                    ; observed as a hang with CS:IP parked on
                                    ; this package's own entry point
    cmp byte [sc_pickm], SC_M_FONTC
    jne .out
    mov al, [sc_picki]
    or al, al
    jnz .face
    call sc_facedrop                ; back to the built-in cell
    mov word [sc_fcap], sc_s_pica
    mov byte [sc_fsel], 0
    mov byte [sc_prop], 0
    jmp short .reflow               ; ...and the note is drawn again in it:
                                    ; going BACK to the cell is as much a
                                    ; change as leaving it
.face:
    cmp al, [sc_nfont]
    ja .out
    dec al
    push ax
    call sc_facedrop                ; ...and a face slot is not left behind by
                                    ; every visit to this menu: there are four
                                    ; and face 0 is one of them, so without
                                    ; this the THIRD pick answers TYE_NOSLOT
                                    ; and the combo quietly stops working
    pop ax
    push ax
    call ty_openfam                 ; opened NOW rather than at draw time: a
    pop bx                          ; face that will not read should say so
    jc .out                         ; while the person is still looking at the
    mov [sc_face], al               ; menu they picked it from - and the BOX
    xor ah, ah                      ; is not renamed until it has, so the name
    call ty_use                     ; in the ribbon is EVIDENCE that the face
    call ty_cache                   ; is open and not just that it was asked
    mov al, bl                      ; for
    mov [sc_fsel], al
    call ty_famname
    mov [sc_fcap], si
%if SC_PROPDRAW
    mov byte [sc_prop], 1           ; ...and the document is SET in it - which
%endif                              ; is off until the three things named at
                                    ; SC_PROPDRAW are done
.reflow:
    call sc_facemetrics             ; the glyph band and the row advance follow
                                    ; the face, so every cached row signature
                                    ; describes a layout that may no longer
                                    ; exist - all four caches go
    mov byte [sc_sigok], 0
    mov word [sc_prowi], 0xFFFF
    mov byte [sc_ckok], 0
    mov byte [sc_rowsok], 0
    mov byte [sc_redrw], 1
.paint:
    cmp byte [sc_vrib], 0           ; ...and the BOX has to be lettered again.
    je .out                         ; sc_mfire runs AFTER sc_mclose has put the
    call sc_ribbon                  ; rows the dropdown covered back, so the
                                    ; caption this just changed was drawn a
                                    ; moment before it changed. One strip,
                                    ; priced in calls, not a window
.out:
    pop di
    pop si
    pop bx
    pop ax
    cmp byte [sc_redrw], 0
    je .ret
    mov byte [sc_redrw], 0
    jmp sc_redraw                   ; A TAIL JUMP, which is how every other
                                    ; action in this program reaches the
                                    ; redraw - sc_a_new, sc_a_undo, sc_a_cut
                                    ; and the rest all `jmp sc_redraw` rather
                                    ; than call it. Calling it from inside the
                                    ; handler left the chrome drawn at two
                                    ; geometries at once
.ret:
    ret

; -----------------------------------------------------------------------------
; sc_cx - the absolute screen x of a cell (SPEC.md 68.13)
; in:  AX = a cell index, 0..[sc_rcols]
; out: AX = its x; preserves every other register
;
; In the 8x8 cell the k-th glyph is at [sc_tx] + 8k and there is nothing to
; look up. In a proportional face it is [sc_tx] + sc_px[k], which the
; accumulate walk recorded as it stored the cell. EVERY cell-to-pixel site in
; this program goes through here, so the two faces differ in one routine
; instead of in thirty - and with [sc_prop] clear this is the same three
; shifts those sites used to do inline.
; -----------------------------------------------------------------------------
sc_cx:
    push bx
    cmp byte [sc_pxon], 0           ; the METRICS flag, not [sc_prop]: a face
    jne .prop                       ; is drawn on the 8-pixel grid first and
                                    ; its own advances are honoured after
                                    ; (SPEC.md 68.13), and only the second of
                                    ; those two makes sc_px[] the truth
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, bx
    jmp short .add
.prop:
    cmp ax, SC_MAXCOL + 1           ; the map has one slot past the last cell,
    jbe .in                         ; so a span's right edge is a lookup
    mov ax, SC_MAXCOL + 1
.in:
    mov bx, ax
    shl bx, 1
    mov ax, [sc_px + bx]
.add:
    add ax, [sc_tx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_xc - the cell an x falls in - sc_cx's inverse (SPEC.md 68.13)
; in:  AX = an absolute x
; out: AX = the cell index; preserves every other register
;
; FLOOR, not nearest: this answers "which cell holds this pixel", which is
; what the fixed-face `(x - tx) >> 3` it replaces answered, and every caller
; is written against that. A caret that should land on the nearer EDGE is
; ty_hit's question and not this one.
; -----------------------------------------------------------------------------
sc_xc:
    push bx
    push cx
    sub ax, [sc_tx]
    jns .in
    xor ax, ax                      ; left of the row's first cell
    jmp short .out
.in:
    cmp byte [sc_pxon], 0
    jne .prop
    mov cl, 3
    shr ax, cl
    jmp short .out
.prop:
    xor bx, bx
.w:
    cmp bx, [sc_rcn]                ; ...the cells the WALK stored, not the
    jae .have                       ; buffer's capacity: sc_px past the row's
    push bx                         ; own content is the last row's pens and
    shl bx, 1                       ; describes nothing on this one
    mov cx, [sc_px + bx + 2]        ; ...the NEXT cell's pen: x belongs to
    pop bx                          ; cell k while it is short of cell k+1
    cmp ax, cx
    jb .have
    inc bx
    jmp short .w
.have:
    mov ax, bx
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_advof - the advance of one character, in pixels (SPEC.md 68.13)
; in:  AL = the character AS DRAWN (small caps already mapped)
; out: AX = its advance; preserves every other register
;
; 8 in the kernel's cell, the face's own otherwise - and the face's own is the
; number ty_putn will step by when the band composes this very character, which
; is the whole of why sc_px[] and the drawn glyphs agree.
; -----------------------------------------------------------------------------
sc_advof:
    cmp byte [sc_pxon], 0
    jne .prop
    mov ax, 8
    ret
.prop:
    call ty_advof                   ; AL = the advance, AH untouched
    xor ah, ah
    ret

; -----------------------------------------------------------------------------
; sc_scapof - small caps: the character as it is DRAWN (SPEC.md 68.1)
; in:  AL = the character, AH = its CHP byte; out: AL mapped; preserves the rest
;
; The map used to live inside sc_walk's drawing gate, because the only thing it
; changed was which glyph went into the row buffer. It changed the WIDTH too the
; moment a face arrived, and a width the gate can skip is a pen that disagrees
; with the pixels - so it is a helper now, called before the advance is taken
; and by the dry run as well.
; -----------------------------------------------------------------------------
sc_scapof:
    test ah, SCAT_SCAP
    jz .out
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; -----------------------------------------------------------------------------
; sc_penadv - the advance of the character the walk is ABOUT to take
; in:  ES:SI -> it, BX = characters left; out: AX = pixels; preserves the rest
;
; The wrap rule and the click's half-cell both need the width of the cell this
; index WILL occupy, before it has been consumed. A newline or a tab answers 8:
; neither is measured here (a tab goes to sc_tabw and a newline ends the row),
; and 8 is what the fixed face asked for both.
; -----------------------------------------------------------------------------
sc_penadv:
    or bx, bx                       ; the picture test comes FIRST, and before
    jz .noch                        ; the face test: a picture is its own width
    cmp byte [es:si], SC_PICCH      ; in the kernel's 8x8 cell as much as in a
    je .pictw                       ; proportional face (88.9)
.noch:
    cmp byte [sc_pxon], 0
    jne .prop
.eight:
    mov ax, 8
    ret
.pictw:
    push word [sc_i]                ; SC_PICCH IS AT SI, WHICH IS NOT ALWAYS
    mov [sc_i], si                  ; [sc_i]: sc_wordfit and sc_rowmeasure walk
    call sc_picw                    ; SI ahead with [sc_i] frozen at the word's
    pop word [sc_i]                 ; or the row's first character, so sc_picidx
    ret                             ; read THAT character's CHP byte - an
.prop:
    or bx, bx
    jz .eight
    mov al, [es:si]                 ; THE DOCUMENT IS ES:SI (SPEC.md 27.6)
    cmp al, SC_PICCH                ; a picture's cell is as wide as the
    je .pict                        ; picture, so the ordinary wrap rule ends
    cmp al, 13                      ; the row after it with no special case
    je .eight
    cmp al, 9
    je .eight
    call ty_advof                   ; preserves BX, and AH with it
    xor ah, ah
    ret
.pict:
    push word [sc_i]                ; ...attribute set (SCAT_BOLD = 1, SCAT_UL
    mov [sc_i], si                  ; = 4) taken as a picture index, so a bold
    call sc_picw                    ; character measured picture 1 and an
    pop word [sc_i]                 ; underlined one picture 4. AX = the width,
    ret                             ; or 8 if the index is not one this
                                    ; document has

; -----------------------------------------------------------------------------
; sc_cellat - the cell index the pen is standing on (SPEC.md 68.13)
; out: BX = it; preserves every other register
;
; COUNTED in a proportional face and DERIVED in the fixed one, which is the one
; place the accumulate walk had to change: (DI - [sc_tx]) >> 3 needs a divisor
; the face does not have. The count is [sc_rcn], kept by sc_pxcell below.
; -----------------------------------------------------------------------------
sc_cellat:
    cmp byte [sc_pxon], 0
    jne .prop
    push cx
    mov bx, di
    sub bx, [sc_tx]
    mov cl, 3
    shr bx, cl
    pop cx
    ret
.prop:
    mov bx, [sc_rcn]
    ret

; -----------------------------------------------------------------------------
; sc_pxcell - record the cell at the pen and count it (SPEC.md 68.13)
; in:  BX = the cell (sc_cellat), DI = the pen, AX = the cell's advance
; out: sc_px[BX..BX+2] written, [sc_rcn] = BX+1; preserves every register
;
; THREE slots for one cell, and the third is not spare. The slot past the cell
; makes a span's right edge a lookup rather than a special case, and the next
; cell overwrites it with the same value, so the two can never disagree. The
; slot past THAT is the caret's parking cell - the one index past the row's
; last character, which sc_rflush letters a space into when a caret leaves it -
; and it is a space's own advance wide, which is what the band will step by
; when it composes that very space.
; -----------------------------------------------------------------------------
sc_pxcell:
    cmp byte [sc_pxon], 0
    je .out
    cmp bx, SC_MAXCOL               ; the map is SC_MAXCOL + 3 slots, so a cell
    ja .out                         ; at SC_MAXCOL still has two past it
    push ax
    push bx
    push dx
    mov dx, di
    sub dx, [sc_tx]                 ; the pen, RELATIVE to the content left -
    shl bx, 1                       ; a window that MOVES does not reflow
    mov [sc_px + bx], dx
    add dx, ax
    mov [sc_px + bx + 2], dx
    add dx, [sc_spadv]
    mov [sc_px + bx + 4], dx
    shr bx, 1
    inc bx
    mov [sc_rcn], bx
    pop dx
    pop bx
    pop ax
.out:
    ret

sc_rowmeasure:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov al, [sc_wstart]
    push ax                         ; the live word state, restored whole
    mov bp, di                      ; BP = the start pen (borrowed: no y here)
.loop:
    or bx, bx
    jz .done
    mov al, [es:si]
    cmp al, 13
    je .done
    call sc_penadv                  ; the cell this index will occupy, which is
    mov cx, di                      ; 8 wide in the kernel's cell and the
    add cx, ax                      ; face's own otherwise (SPEC.md 68.13)
    dec cx
    cmp cx, [sc_wrgt]
    ja .over
    call sc_wordfit
    jc .done
    jmp short .take
.over:
    call sc_hangsp
    jnc .done
.take:
    cmp al, 9
    je .tab
    xor ah, ah                      ; the cell's dress. Fetched when the note
    cmp byte [sc_hashid], 0         ; carries hidden text - which is what this
    jne .chp                        ; dry run always needed it for - and in a
    cmp byte [sc_pxon], 0           ; chosen face as well, because small caps
    je .vis                         ; measures as the CAPITAL there and the
.chp:                               ; live walk draws it as one
    push es
    push bx
    mov bx, si
    mov es, [sc_cseg]
    mov ah, [es:bx]
    pop bx
    pop es
    test ah, SCAT_HID               ; hidden occupies no cell (SPEC.md 68.1)
    jnz .adv0
.vis:
    call sc_scapof
    call sc_advof
    add di, ax
    mov al, [es:si]                 ; ...and the word state below reads the
.adv0:                              ; character as TYPED, not as drawn
    mov byte [sc_wstart], 0
    cmp al, ' '
    jne .ws
    mov byte [sc_wstart], 1
.ws:
    inc si
    dec bx
    jmp short .loop
.tab:
    call sc_tabw                    ; anchored at [sc_rowx0], which the caller
                                    ; set to THIS start pen before calling
    add di, ax
    mov byte [sc_wstart], 1
    inc si
    dec bx
    jmp short .loop
.done:
    mov ax, di                      ; PIXELS (SPEC.md 68.13), which is what the
    sub ax, bp                      ; centre/right offset wants in either face
    pop cx                          ; the saved word state
    mov [sc_wstart], cl
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_rowsetup - the pen for the row just entered: indents, then the centre/
;               right offset (SPEC.md 68.1: rounded DOWN to whole cells)
; in:  ES:SI/BX = the walk at the row's first char, pap state loaded
; out: DI = the pen, [sc_rowx0] = it (the tab anchor); preserves all others
; -----------------------------------------------------------------------------
sc_rowsetup:
    push ax
    push cx
    push dx
    mov di, [sc_tx]
    cmp byte [sc_hasfmt], 0
    je .anchor
    add di, [sc_wleftpx]
    cmp byte [sc_parafirst], 0
    je .nof
    add di, [sc_wfirstpx]
    cmp di, [sc_tx]
    jge .nof
    mov di, [sc_tx]                 ; belt: sc_papload clamped this already
.nof:
    mov [sc_rowx0], di
    mov al, [sc_walign]
    cmp al, 1
    je .meas                        ; centred and right earn the dry run;
    cmp al, 2                       ; justified renders as left (65.1)
    jne .anchor
.meas:
    call sc_rowmeasure              ; AX = the row's width in PIXELS
    mov dx, [sc_wrgt]
    inc dx
    sub dx, di                      ; pixels available from this pen
    sub dx, ax
    jle .anchor                     ; full (or hanging): no offset
    cmp byte [sc_pxon], 0
    jne .poff
    mov cl, 3                       ; the kernel's cell: whole cells, exactly
    shr dx, cl                      ; the rounding this always did (SPEC.md
    cmp byte [sc_walign], 2         ; 65.1) - the row is 8k wide and the offset
    je .croff                       ; is 8k too, so the pen stays on the grid
    shr dx, 1
.croff:
    shl dx, cl
    jmp short .roff
.poff:
    cmp byte [sc_walign], 2         ; a chosen face: PIXELS, because there is no
    je .roff                        ; grid left to round to
    shr dx, 1
.roff:
    add di, dx
.anchor:
    mov [sc_rowx0], di
    cmp byte [sc_pxon], 0           ; cell 0's pen, before a character has been
    je .nopx0                       ; stored: an EMPTY row still carries a
    push ax                         ; caret, and sc_cx(0) has to answer where
    mov ax, di                      ; it stands (SPEC.md 68.13)
    sub ax, [sc_tx]
    mov [sc_px], ax
    add ax, [sc_spadv]              ; ...and cell 0 IS the parking cell there,
    mov [sc_px+2], ax               ; so it is a space wide, as sc_pxcell has it
    pop ax
.nopx0:
    cmp byte [sc_hasfmt], 0
    je .nofold
    push ax                         ; fold the pen and the height into the
    mov ax, di                      ; row's signature: alignment, indents and
    xor ax, 0x3C3C                  ; spacing move pixels without changing a
    call sc_fold                    ; character, and a stale row is exactly a
    mov ax, [sc_rowhv]              ; row whose fold did not change (the xor
    call sc_fold                    ; keeps an x from aliasing a char code)
    pop ax
.nofold:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_yrow - which visible row owns pixel y = DX?
; out: CF=0 and AX = the row; CF=1 = cannot say, and the caller falls back to
;      the unseeded path - slow and never wrong. Uniform documents answer by
;      arithmetic; formatted ones scan sc_ryb, which describes the glass.
; -----------------------------------------------------------------------------
sc_yrow:
    push bx
    push cx
    mov ax, dx
    sub ax, [sc_ty]
    jc .no
    cmp byte [sc_hasfmt], 0
    jne .scan
    mov cl, 3
    shr ax, cl
    jmp short .yes
.scan:
    cmp byte [sc_rowsok], 0
    je .no
    mov cx, [sc_rowsn]
    cmp cx, [sc_vrows]
    jbe .lim
    mov cx, [sc_vrows]
.lim:
    jcxz .no
    xor ax, ax
.l:
    mov bx, ax
    shl bx, 1
    mov bx, [bx+sc_ryb]
    add bx, [sc_gh1]
    cmp dx, bx
    jbe .yes                        ; rows are met in order, so the first row
                                    ; whose glyph bottom is at/below y owns it
                                    ; (its band reaches up to the row above)
    inc ax
    cmp ax, cx
    jb .l
.no:
    pop cx
    pop bx
    stc
    ret
.yes:
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; sc_walk - THE layout pass: one loop, two jobs (SPEC.md 27)
; in:  [sc_bounds] already run; [sc_draw] = 1 to paint, 0 to measure only;
;      [sc_cur]; the two optional queries below
; out: [sc_curx]/[sc_cury] = where the caret sits, in pixels
;      [sc_hiti]  = the character index the point [sc_hitx],[sc_hity] falls
;                   on ([sc_hity] = 0xFFFF disables the test)
;      [sc_wanti] = the index at column [sc_wantx] of row [sc_wanty]
;                   (0xFFFF disables it, and is also the "no such row" answer)
;      preserves all registers
;
; **One walk, because two would drift.** Painting the text, finding the pixel
; a caret index sits at, turning a mouse click into an index and moving the
; caret a row up or down are the same traversal asked four questions, and the
; wrap rule they share is subtle enough (an 8px cell that would cross the
; right edge moves to the next row, and a row that would cross the bottom is
; skipped while the pen keeps advancing) that a second copy of it would be
; wrong within one edit of this file.
;
; Every index 0..[sc_len] is visited, including the one PAST the last
; character - that is where the caret lives in a note that ends in text, so
; it has to be a position the queries can return.
;
; The caret occupies a cell and therefore wraps like one, which is what keeps
; it in front of the character it precedes rather than stranded at the end of
; the row above.
; -----------------------------------------------------------------------------
sc_walk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov word [sc_curx], 0
    mov word [sc_cury], 0
    mov word [sc_currow], 0
    mov byte [sc_curseen], 0        ; ...and 0 is a REAL pen y on a fullscreen
                                    ; window, so "did this walk find it" needs
                                    ; a flag of its own now that a walk may
                                    ; stop before the caret (SPEC.md 27.7)
    mov ax, [sc_len]
    mov [sc_hiti], ax               ; a click past the end lands at the end
    mov word [sc_wanti], 0xFFFF     ; ...but a row that does not exist has no
    mov byte [sc_hitset], 0         ; answer, and the caller keeps its caret
    mov byte [sc_wantset], 0

    mov di, [sc_tx]                 ; DI = pen x
    mov word [sc_i], 0
    mov ax, [sc_top]                ; sc_row is the VISIBLE row, so the note's
    neg ax                          ; first row sits sc_top rows ABOVE the top
    mov [sc_row], ax                ; of the view and its index is NEGATIVE
    mov word [sc_rowh], 0           ; (SPEC.md 27.7). Everything that indexes
                                    ; an array by it already tests UNSIGNED
                                    ; against a limit, and a negative word
                                    ; read as unsigned is past all of them
    mov byte [sc_wstart], 1         ; whatever this walk starts on begins a
                                    ; word as far as it can tell (SPEC.md
                                    ; 27.11). A seeded walk may in fact resume
                                    ; inside one, and cannot be wrong for it:
                                    ; a seed is always a row START, where the
                                    ; word test never breaks anyway
    mov bx, [sc_len]                ; BX = characters remaining
    xor si, si                      ; ES:SI = the document (SPEC.md 27.6), and
    mov es, [sc_dseg]               ; ES survives every callee below: sc_rstart
                                    ; and sc_rflush push it around their own
    cmp byte [sc_resume], 0         ; ...or start part-way in: at the caret's
    je .seeded                      ; own row for a keystroke (SPEC.md 27.4),
    mov ax, [sc_sdi]                ; or at any row sc_rows named (SPEC.md
    cmp ax, [sc_len]                ; 27.5). Everything before the seed laid
    ja .seeded                      ; out identically last time and cannot have
                                    ; moved, so its signatures still stand and
                                    ; its pixels are on screen
    cmp byte [sc_hasfmt], 0         ; ...but a FORMATTED resume also trusts
    je .rok                         ; the banked y of the row above the seed
    mov dx, [sc_sdr]                ; (SPEC.md 68.6), and a stale bank would
    or dx, dx                       ; draw every row at a lie. Verify it is a
    jle .rok                        ; y the band could hold; when it is not,
    cmp dx, [sc_vrows]              ; fall back to the unseeded walk - slow
    jae .rok                        ; and never wrong. (sdr <= 0 rides the
    dec dx                          ; sentinel and sdr >= vrows the measure
    shl dx, 1                       ; path: neither reads a bank)
    push bx
    mov bx, dx
    mov dx, [bx+sc_ryb]
    pop bx
    cmp dx, [sc_ty]
    jb .seeded
    cmp dx, [sc_bot]
    ja .seeded
.rok:
    mov [sc_i], ax
    add si, ax
    sub bx, ax
    mov ax, [sc_sdr]
    mov [sc_row], ax
.seeded:
    call sc_papini                  ; the governing paragraph format and the
    call sc_rowhc                   ; row's first-of-paragraph-ness, then its
                                    ; height (SPEC.md 68.3/68.6)
    mov ax, [sc_row]
    or ax, ax
    js .ynegs                       ; above the view: rows park at the ty-8
    jz .yzero                       ; sentinel (sc_rflush refuses y < ty)
    cmp byte [sc_hasfmt], 0
    jne .yfmt
    mov cl, 3                       ; uniform: sc_ty + 8*row, exactly as ever
    shl ax, cl
    add ax, [sc_ty]
    mov bp, ax
    jmp short .yhave
.yfmt:
    cmp ax, [sc_vrows]
    jae .ydeep
    dec ax                          ; formatted: glyph y = ryb[row-1] + h -
    shl ax, 1                       ; the rows above the seed are exactly the
    push bx                         ; ones the seed's licence says stood still
    mov bx, ax                      ; (SPEC.md 27.4/68.6), so their banked ys
    mov bp, [bx+sc_ryb]             ; still describe the glass
    pop bx
    add bp, [sc_rowhv]
    jmp short .yhave
.ydeep:
    mov bp, [sc_bot]                ; a resume below the view (the chunked
    inc bp                          ; count): drawn by nobody, no query armed
    jmp short .yhave
.yzero:
    mov bp, [sc_ty]                 ; row 0's band top is sc_ty exactly
    add bp, [sc_rowhv]
    sub bp, 8
    jmp short .yhave
.ynegs:
    mov bp, [sc_ty]
    sub bp, 8
.yhave:
    mov ax, bp
    sub ax, [sc_rowhv]
    add ax, 8
    mov [sc_rbandt], ax
    call sc_rstart                  ; BP is this row's y; the buffer starts blank
    call sc_rowsetup                ; DI = the pen: indents + alignment offset

.loop:
    ; --- the wrap rule, applied to the cell this index will occupy ---------
    call sc_penadv                  ; ...and the cell is as wide as the FACE
    mov cx, di                      ; says, which is 8 in the kernel's cell and
    add cx, ax                      ; the character's own otherwise (SPEC.md
    dec cx                          ; 65.13)
    cmp cx, [sc_wrgt]               ; the paragraph's own right edge (its
    ja .over                        ; right indent; [sc_rgt] when Normal)
    call sc_wordfit                 ; ...or the WORD that begins here would run
    jnc .fits                       ; off the row (SPEC.md 27.11)
    jmp short .wrap
.over:
    call sc_hangsp                  ; a trailing SPACE hangs past the margin
    jc .fits                        ; rather than indent the row below it
.wrap:
    call sc_rflush                  ; the row that is ENDING, before sc_nextrow
    call sc_advwrap                 ; same paragraph: h = its line spacing,
                                    ; BP moves by the ENTERED row's height
    call sc_nextrow                 ; the pen changed rows, so the signature
    call sc_bpush                   ; being accumulated belongs to the old one
    call sc_rstart                  ; ...and in break mode the rows below have
    call sc_rowsetup                ; to be pushed down before it is drawn
    mov ax, [sc_row]
    cmp ax, [sc_lastrow]            ; SIGNED (SPEC.md 27.7): sc_row is a
    jle .fits                       ; VISIBLE row and is negative above the
    jmp .stop                       ; view, which unsigned reads as past every
                                    ; limit - so the walk would stop before it
                                    ; had drawn anything at all
.fits:
    call sc_ask                     ; the queries, at the settled pen
    cmp byte [sc_draw], 0
    je .body
    call sc_carets                  ; ...and the caret, if this is its index
.body:
    cmp byte [sc_bstop], 0          ; the visual break (SPEC.md 27.3): the
    je .nostop                      ; walk ENDS at the caret, because the
    mov ax, [sc_i]                  ; whole point is that the note below it
    cmp ax, [sc_cur]                ; is not being laid out at all
    jne .nostop
    mov byte [sc_rowsok], 0         ; ...which leaves sc_rows stale below the
    jmp .donebrk                    ; caret: the note MOVED and this walk did
.nostop:                            ; not rewrite where. Near: .done is past a
                                    ; short jump's reach
    test bx, bx
    jz .done                        ; the index past the last character: the
                                    ; queries have seen it, and there is no
                                    ; character to draw
    es lodsb                        ; DF=0 per SPEC.md 1; the override is what
    dec bx                          ; makes the note a heap claim and not bss
    inc word [sc_i]
    cmp al, 13
    je .nl13
    cmp al, 9                       ; a tab (SPEC.md 68.3): one selectable
    jne .glyph                      ; character occupying the cells to the
    jmp .tab                        ; next default stop
.nl13:
    mov byte [sc_wstart], 1         ; a line break is a break opportunity like
                                    ; a space (SPEC.md 27.11)
    cmp byte [sc_showall], 0        ; Show-all (SPEC.md 68.1): the mark takes a
    je .nlplain                     ; cell of its own, stamped as a pilcrow by
    push ax                         ; sc_rflush. Folded - character and marker
    mov ax, 13                      ; attribute both - so toggling Show-all
    call sc_fold                    ; dirties exactly the rows that carry one
    mov ax, SCAT_PIL
    call sc_fold
    pop ax
    push ax
    push bx
    push cx
    call sc_cellat                  ; the mark's own cell, and its pen - taken
    mov ax, 8                       ; OUTSIDE the drawing gates below, because
    call sc_pxcell                  ; [sc_rcn] is the cell INDEX in a chosen
                                    ; face and a count a pass may skip is a
                                    ; count that describes no row. 8 wide: the
                                    ; pilcrow is a stamp, not a glyph of the
                                    ; face (SPEC.md 68.1)
    cmp byte [sc_draw], 0
    je .nlpop
    mov cx, bp                      ; the same three gates the glyph store has:
    add cx, [sc_gh1]                ; the row fits, this pass redraws it, and
    cmp cx, [sc_bot]                ; the cell is inside the band
    ja .nlpop
    call sc_rowdirty
    jc .nlpop
    cmp bx, [sc_rcols]
    jae .nlpop
    mov byte [sc_rbuf+bx], ' '
    mov byte [sc_abuf+bx], SCAT_PIL
.nlpop:
    pop cx
    pop bx
    pop ax
.nlplain:
    call sc_rflush                  ; same as the wrap above: flush before
    call sc_advnl                   ; a NEW paragraph begins at [sc_i]: its
                                    ; format is scanned once and its first
                                    ; row's height includes the open space
    call sc_nextrow                 ; the mark occupies no cell - so it is not
    call sc_rstart                  ; folded into either row's signature, and
    call sc_rowsetup                ; the pixels of the row it ends are the
    mov ax, [sc_row]                ; same with it and without it. Signed, for
    cmp ax, [sc_lastrow]            ; the reason at the wrap above (near jumps:
    jg .nlstop                      ; the Show-all block above pushed .loop
    jmp .loop                       ; past a short jump's reach)
.nlstop:
    jmp .stop
.glyph:
    mov byte [sc_wstart], 0         ; the next index is mid-word...
    cmp al, ' '
    jne .wsdone
    mov byte [sc_wstart], 1         ; ...unless this is the space that ended
.wsdone:                            ; one (SPEC.md 27.11)
    push ax                         ; the character's CHP byte (SPEC.md 68.3):
    push bx                         ; the same index in the CHP claim. Fetched
    mov bx, si                      ; through ES with the document segment put
    dec bx                          ; back after - ES belongs to the text for
    mov es, [sc_cseg]               ; the rest of the walk
    mov al, [es:bx]
    mov es, [sc_dseg]
    mov [sc_cattr], al
    pop bx
    pop ax
    push ax                         ; fold it in whatever this pass is for:
    xor ah, ah                      ; the pass that COMPUTES the signatures is
    push ax                         ; a measure pass, so this cannot hang off
    mov ax, [sc_i]                  ; sc_draw.
    dec ax                          ; ...and a SELECTED cell folds differently
    call sc_selq                    ; (SPEC.md 27.8), because the inversion is
    pop ax                          ; pixels like the glyph is and moving the
    jnc .nfsel                      ; selection has to dirty the row it left
    mov ah, 0x80                    ; as well as the one it arrived at. POP
.nfsel:                             ; touches no flags, so the answer survives
    call sc_fold
    push ax                         ; ...and the CHP byte folds BESIDE the
    mov al, [sc_cattr]              ; character (SPEC.md 68.1), so a formatting
    xor ah, ah                      ; change dirties exactly the rows it
    call sc_fold                    ; touched and nothing else
    pop ax
    pop ax
    test byte [sc_cattr], SCAT_HID  ; hidden: dropped at accumulate time - no
    jz .visible                     ; cell, no pen advance (SPEC.md 68.1). The
    jmp .loop                       ; fold above still saw it, so toggling
                                    ; hidden dirties the row
.visible:
    ; The character AS DRAWN and its ADVANCE, both taken before the drawing
    ; gates below (SPEC.md 68.13). The small-caps map used to live inside them,
    ; because the only thing it changed was which glyph reached the row buffer;
    ; it changes the WIDTH too the moment a face arrives, and a pen that moves
    ; by one character while the band composes another is a row that drifts a
    ; pixel a glyph. The pen record is outside them for the same reason: in a
    ; chosen face [sc_rcn] IS the cell index, and a count a pass may skip is a
    ; count that describes no row.
    push dx
    mov ah, [sc_cattr]
    call sc_scapof
    mov dl, al                      ; DL = the character as drawn...
    call sc_advof
    mov dh, al                      ; ...DH = its advance (a byte: SPEC.md 6.4)
    push bx
    call sc_cellat                  ; BX = the cell this pen occupies
    mov al, dh
    xor ah, ah
    call sc_pxcell
    cmp byte [sc_draw], 0
    je .nocell
    mov cx, bp                      ; vertical clip: drop rows that overflow,
    add cx, [sc_gh1]                ; but keep advancing the pen so every
    cmp cx, [sc_bot]                ; position below stays true
    ja .nocell
    call sc_rowdirty                ; ...and drop the rows whose pixels this
    jc .nocell                      ; redraw already knows are right
    cmp bx, [sc_rcols]
    jae .nocell                     ; past the band: the wrap rule above means
                                    ; this cannot normally happen, and a
                                    ; clamped sc_rcols is the case where it can
    mov al, dl                      ; into the row buffer at this pen's CELL -
    mov [sc_rbuf+bx], al            ; sc_rflush draws the whole row at once
    mov al, [sc_cattr]              ; ...and the cell's attribute beside it,
    mov [sc_abuf+bx], al            ; which is what sc_rflush splits into runs
    push ax
    push dx
    mov ax, [sc_i]
    dec ax
    call sc_selq                    ; selected NOW...
    mov dl, 0
    jnc .n1
    mov dl, 1
.n1:
    call sc_selqo                   ; ...and selected ON SCREEN (SPEC.md 27.8.2)
    mov dh, 0
    jnc .n2
    mov dh, 1
.n2:
    or dl, dl
    jz .nosel
    mov ax, bx                      ; inside the selection: widen the span
    call sc_selfold                 ; sc_rflush inverts (SPEC.md 27.8)
.nosel:
    cmp dl, dh
    je .noxf
    mov ax, bx                      ; ...and its inversion has to CHANGE, which
    call sc_xfold                   ; is the only thing a drag actually owes
.noxf:
    pop dx
    pop ax
.nocell:
    pop bx
    mov al, dh                      ; the pen moves by the advance the band
    xor ah, ah                      ; will step by, and by nothing else
    add di, ax
    pop dx
    jmp .loop                       ; near: the cell-buffer store above pushed
                                    ; the loop body past a short jump's reach

.tab:
    ; A TAB (SPEC.md 68.3): it advances the pen to the next default stop -
    ; every SC_TABSTOP cells from the row's start pen - and is ONE selectable
    ; character. Its cells go into the row buffer as spaces wearing the tab's
    ; own dress, so an underlined tab draws its rule and a selected one
    ; inverts whole; the fold sees the 9 and its CHP, and the expansion is a
    ; function of the folded content before it, so the signature stays honest.
    mov byte [sc_wstart], 1         ; a tab ends a word, like a space
    push ax
    push bx
    mov bx, si
    dec bx
    mov es, [sc_cseg]               ; its CHP byte, the glyph path's fetch
    mov al, [es:bx]
    mov es, [sc_dseg]
    mov [sc_cattr], al
    pop bx
    pop ax
    push ax
    xor ah, ah
    push ax
    mov ax, [sc_i]
    dec ax
    call sc_selq
    pop ax
    jnc .tnfs
    mov ah, 0x80
.tnfs:
    call sc_fold
    push ax
    mov al, [sc_cattr]
    xor ah, ah
    call sc_fold
    pop ax
    pop ax
    test byte [sc_cattr], SCAT_HID  ; a hidden tab occupies nothing
    jz .tvis
    jmp .loop
.tvis:
    call sc_tabw                    ; AX = the gap in px...
    mov cx, [sc_wrgt]               ; ...clamped to the row's edge, whole
    inc cx                          ; cells, at least one - so the pen cannot
    sub cx, di                      ; jam and cannot paint past the margin
    cmp ax, cx
    jbe .twok
    mov ax, cx
    and ax, 0xFFF8
    or ax, ax
    jnz .twok
    mov ax, 8
.twok:
    mov cx, ax                      ; CX = the width, held to the advance
    push ax
    push bx
    push dx
    call sc_cellat                  ; BX = the tab's first cell, taken outside
    mov dx, cx                      ; the gates for .visible's reason. DX = the
    cmp byte [sc_pxon], 0           ; px the store loop still owes cells
    je .tgate
    mov ax, cx                      ; A CHOSEN FACE GIVES A TAB ONE CELL, as
    call sc_pxcell                  ; wide as the whole gap (SPEC.md 68.13) -
    mov dx, 8                       ; against the kernel's cell, where it is a
.tgate:                             ; run of space cells one per 8 pixels. That
                                    ; is the row buffer holding characters
                                    ; rather than screen columns, which is what
                                    ; the delta diff wanted all along
    cmp byte [sc_draw], 0
    je .tcdone
    mov ax, bp                      ; the glyph-store gates, cell test aside
    add ax, [sc_gh1]
    cmp ax, [sc_bot]
    ja .tcdone
    call sc_rowdirty
    jc .tcdone
.tcell:
    cmp bx, [sc_rcols]
    jae .tcdone
    mov byte [sc_rbuf+bx], ' '
    mov al, [sc_cattr]
    mov [sc_abuf+bx], al
    push dx
    mov ax, [sc_i]
    dec ax
    call sc_selq
    mov dl, 0
    jnc .ts1
    mov dl, 1
.ts1:
    call sc_selqo
    mov dh, 0
    jnc .ts2
    mov dh, 1
.ts2:
    or dl, dl
    jz .tnosel
    mov ax, bx
    call sc_selfold
.tnosel:
    cmp dl, dh
    je .tnoxf
    mov ax, bx
    call sc_xfold
.tnoxf:
    pop dx
    inc bx
    sub dx, 8
    ja .tcell
.tcdone:
    pop dx
    pop bx
    pop ax
.tadv:
    add di, cx
    jmp .loop

.done:
    mov ax, [sc_row]                ; how tall the NOTE is, which is what the
    add ax, [sc_top]                ; scroll bar's thumb is a fraction of
    inc ax                          ; (SPEC.md 27.7). HERE, not below: .blank
    mov [sc_drows], ax              ; walks sc_row on past the last row the note
    mov byte [sc_hdirty], 0         ; actually has, and .donebrk is the visual
                                    ; break's end - a walk that stopped at the
                                    ; caret has not seen the note's height
.donebrk:
    call sc_rflush                  ; the last row the walk was accumulating

    ; ...and then every row BELOW it that this redraw still owns. A note that
    ; shrank - a backspace that pulled a wrapped line back up, a deleted
    ; newline - leaves rows the walk no longer reaches, and their old pixels
    ; are still on screen. The band fill used to erase them for free, because
    ; it covered dr0..dr1 whether or not the walk got there; drawing row by row
    ; does not, so they are blanked explicitly. Without this a deletion left
    ; the row's last state behind, caret included, which is exactly what the
    ; first test of this rewrite showed.
    cmp byte [sc_draw], 0
    je .sigpad
.blank:
    mov ax, [sc_row]
    cmp ax, [sc_dr1]
    jae .sigpad                     ; past what this redraw was asked for
    cmp ax, [sc_vrows]
    jae .sigpad                     ; ...or past the content
    call sc_advblank                ; blank rows step 8px, contiguously from
    call sc_nextrow                 ; the last real row's band
    call sc_rstart                  ; an empty row at this y: sc_rflush's own
    call sc_rflush                  ; dirty and fits tests still gate it
    jmp short .blank

.sigpad:
    mov ax, [sc_row]                ; a walk that ran to its natural end is
    inc ax                          ; what sc_rows describes, rows 0..sc_row
    js .norows2                     ; (SPEC.md 27.5) - a STOPPED one leaves
    mov [sc_rowsn], ax              ; the tail of the table stale, and one that
    mov byte [sc_rowsok], 1         ; ended ABOVE the view described none of it
    jmp short .padchk
.norows2:
    mov word [sc_rowsn], 0
    mov byte [sc_rowsok], 0
.padchk:
    cmp byte [sc_sigup], 0
    je .fin
.pad:
    call sc_nextrow                 ; flush the row the walk ended on, and then
    mov ax, [sc_row]                ; every visible row after it: a note that
    cmp ax, [sc_vrows]              ; SHRANK leaves rows behind that are no
    jb .pad                         ; longer reached, and their old signature
                                    ; is exactly what says they must be erased
    jmp short .fin                  ; ...and NOT into .stop: that path pads
                                    ; sc_row past the note's last row without
                                    ; sc_rstart, so the entries it would claim
                                    ; below were never written
.stop:                              ; the sc_lastrow stop leaves sc_rows ALONE:
                                    ; a walk that ends early is one whose
                                    ; caller knows nothing below it moved, so
                                    ; the entries past it are still what the
                                    ; last full pass wrote
    ; It cannot know the note's HEIGHT either - but it does know a LOWER BOUND,
    ; and raising [sc_drows] to it is what keeps sc_scrollmax from clamping the
    ; view short of a caret that has just moved past the old bottom. Never
    ; LOWERED here: a note that shrank keeps a slightly generous scroll range
    ; until something walks it whole, and the cost of that is one blank row at
    ; the end rather than a caret nobody can see (SPEC.md 27.7)
    push ax
    mov ax, [sc_row]                ; the ABSOLUTE row this walk stopped on,
    add ax, [sc_top]                ; and the index that row begins at: the
    mov [sc_stoprow], ax            ; pair a resumable walk picks up from
    push ax                         ; (SPEC.md 27.7.3). sc_rstart has already
    mov ax, [sc_i]                  ; run for this row, so [sc_i] is its FIRST
    mov [sc_stopi], ax              ; character and not the last of the row
    pop ax                          ; above. Published on EVERY bounded stop
                                    ; and kept only by sc_height, on the walk
                                    ; it issued itself - which is safe because
                                    ; it reads them under the lock with
                                    ; nothing between the call and the read
    inc ax
    cmp ax, [sc_drows]
    jbe .nolb
    mov [sc_drows], ax
.nolb:
    ; ...and sc_rows DOES describe what it passed, as long as this walk started
    ; at the top of the view rather than at a seed part-way down. Every row it
    ; stood on went through sc_rstart, so the table is good up to sc_row - and
    ; without saying so, a bounded sc_paint left [sc_rowsok] clear and every
    ; caret key after it fell back to walking from index 0 (SPEC.md 27.5).
    cmp byte [sc_resume], 0
    jne .norn                       ; a RESUMED walk skipped the rows above its
    mov ax, [sc_row]                ; seed, and theirs are the last full pass's
    cmp ax, [sc_vrows]
    jbe .rncap
    mov ax, [sc_vrows]
.rncap:
    cmp ax, SC_MAXROWS
    jbe .rnok
    mov ax, SC_MAXROWS
.rnok:
    or ax, ax
    jle .norn                       ; it stopped above the view: it described
    mov [sc_rowsn], ax              ; none of the table
    mov byte [sc_rowsok], 1
.norn:
    pop ax
.fin:
    mov word [sc_lastrow], 0x7FFF   ; ONE-SHOT: a caller that forgets to set it
                                    ; gets the whole note, which is slow and
                                    ; never wrong. 0x7FFF and not 0xFFFF now
                                    ; that the comparison is signed - 0xFFFF
                                    ; is row minus one, and would stop the
                                    ; walk on its first row
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_hangsp - may the cell at this pen hang past the right edge?
; in:  ES:SI = the document at [sc_i], BX = characters left, DI = pen x
; out: CF = 1 = do not wrap, let it hang; preserves every register
;
; Only a SPACE, and only one cell's worth. Word wrap ends a row after the last
; word that fits, and the space that follows that word then has nowhere to go:
; the cell rule sends it to the next row, where it is an indent nobody typed -
; one row in [sc_rcols], often enough to look like a mistake in a narrow
; window. Every text editor hangs it past the margin instead, and here that
; costs nothing, because the cell is beyond [sc_rcols] and so is dropped by
; the row buffer's own bound rather than drawn: a space paints nothing anyway.
;
; The overshoot is capped at that one cell, which is what keeps a run of
; spaces from walking the pen out of the window - and out of a 16-bit DI.
; -----------------------------------------------------------------------------
sc_hangsp:
    or bx, bx
    jz .no                          ; the end of the note: nothing to hang
    push ax
    mov ax, [sc_wrgt]
    inc ax
    cmp di, ax
    ja .popno                       ; already hanging: the next one wraps
    mov al, [es:si]
    cmp al, ' '
    jne .popno
    pop ax
    stc
    ret
.popno:
    pop ax
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; sc_wordfit - would the word beginning at this index run off the row?
; in:  ES:SI = the document at [sc_i], BX = characters left, DI = pen x,
;      [sc_wstart] = 1 if this index begins a word
; out: CF = 1 = break the row BEFORE this index; preserves every register
;
; The whole of SPEC.md 27.11's word wrap, and the only lookahead in this
; module. It is asked at a word's FIRST character and nowhere else, because
; the answer cannot change inside one: what is left of a word only gets
; shorter as the pen advances it, so a word that fitted at its first cell
; still fits at its second. That is what keeps the cost one scan per word
; rather than one per character.
;
; TWO thresholds, not one, and the second is what stops it spinning. R is what
; is left of THIS row, and a word ending inside it needs no break at all.
; [sc_rcols] is a whole row, and a word longer than THAT can never be helped
; by breaking - it has to be split by the cell rule wherever it stands, and
; forcing a wrap for it would put the pen at the left margin and ask the same
; question again, forever. Between the two thresholds is the only case there
; is.
;
; A word already AT the left margin is the same guard doing second duty and
; needs no test of its own: there R equals [sc_rcols], so "longer than R" and
; "longer than a row" are one question and the answer is always "do not
; break".
; -----------------------------------------------------------------------------
sc_wordfit:
    cmp byte [sc_wstart], 0
    je .no                          ; mid-word: asked and answered at its first
    or bx, bx                       ; character
    jz .no                          ; nothing left to measure
    push ax
    push bx
    push cx
    push dx
    push si

    mov ax, [sc_wrgt]               ; PIXELS throughout, in both faces (SPEC.md
    inc ax                          ; 65.13). The fixed face's every advance is
    sub ax, di                      ; 8 and its R was these pixels divided by
    jle .pop_no                     ; it, so the arithmetic below is the same
    mov dx, ax                      ; question scaled - and a chosen face has no
    cmp byte [sc_pxon], 0           ; divisor to ask it any other way.
    jne .rok                        ; DX = R, the pixels left on this row -
    and dx, 0xFFF8                  ; floored to whole cells in the fixed face,
.rok:                               ; where R WAS a cell count and [sc_wcpx] is
                                    ; 8 x [sc_wcols]: without that the ragged
                                    ; remainder of a window whose width is not
                                    ; a multiple of 8 would move a break that
                                    ; has never moved
    mov cx, dx                      ; --- does the word END inside them? -----
.p1:                                ; THE BREAK TEST COMES FIRST, and the width
    or bx, bx                       ; second: a word that exactly fills the
    jz .pop_no                      ; space left ends on the cell after the last
    mov al, [es:si]                 ; one it occupies, and testing the width
    cmp al, ' '                     ; first calls that an overflow. It is
    je .pop_no                      ; invisible at 29 columns and constant at 9,
    cmp al, 13                      ; which is what a narrow window showed
    je .pop_no
    cmp al, 9                       ; a tab ends a word too (SPEC.md 68.3)
    je .pop_no
    call sc_penadv                  ; measured as TYPED: small caps would make
    cmp cx, ax                      ; this word WIDER, so a word it lets by is
    jb .p2                          ; split by the cell rule instead of broken
    sub cx, ax                      ; in front of - the degrade, not a wrong
    inc si                          ; break
    dec bx
    jmp short .p1

.p2:                                ; --- no. Would a whole row hold it? -----
    mov cx, [sc_wcpx]               ; a fresh row of THIS paragraph, in pixels
    sub cx, dx                      ; the pixels a FRESH row would add
    jbe .pop_no                     ; the pen is at the left margin already
.p2l:
    or bx, bx                       ; same order, for the same reason
    jz .pop_yes                     ; the note ends: a fresh row would hold it
    mov al, [es:si]
    cmp al, ' '
    je .pop_yes
    cmp al, 13
    je .pop_yes
    cmp al, 9
    je .pop_yes
    call sc_penadv
    cmp cx, ax
    jb .pop_no                      ; longer than a row: the cell rule owns it
    sub cx, ax
    inc si
    dec bx
    jmp short .p2l

.pop_yes:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.pop_no:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; sc_ask - answer the walk's queries at the settled pen (module-internal)
; in:  DI/BP = the pen, [sc_i] = this index, SI -> its character, BX = the
;      characters left (0 = we are past the end)
; out: the sc_curx/sc_cury/sc_hiti/sc_wanti fields updated; preserves all
;
; The "+4" is the half-cell rule every text editor uses: a click in the left
; half of a character puts the caret before it, one in the right half after.
; A NEWLINE is excluded from the "after" half, and that is what makes End
; land before the line break instead of at the start of the next line - the
; character occupies no cell, so there is no right half of it to click in.
; -----------------------------------------------------------------------------
sc_ask:
    push ax
    push cx
    mov ax, [sc_i]
    cmp ax, [sc_cur]
    jne .hit
    mov [sc_curx], di
    mov [sc_cury], bp
    push ax
    mov ax, [sc_row]                ; ...and its VISIBLE row, signed - what
    mov [sc_currow], ax             ; sc_seecaret scrolls by now that a row's
    pop ax                          ; y is no longer 8*row (SPEC.md 68.6)
    mov byte [sc_curseen], 1
    push ax                         ; the row the caret is on starts HERE, and
    mov ax, [sc_ckpc]               ; that is the only state the next keystroke
    mov [sc_ckpi], ax               ; needs to skip everything above it
    mov ax, [sc_ckpcr]              ; (SPEC.md 27.4)
    mov [sc_ckpr], ax
    mov byte [sc_ckok], 1
    pop ax
    push ax                         ; AX is [sc_i] and .hit below still wants
    mov ax, di                      ; it. The caret is pixels on this row too,
    xor ax, 0x5A5A                  ; and folding it in HERE - between the
    call sc_fold                    ; glyph before it and the one after - is
    pop ax                          ; what makes moving it dirty both rows.
                                    ; The xor keeps a column from folding the
                                    ; way a character code would
.hit:
    mov cx, [sc_hity]
    cmp cx, 0xFFFF
    je .want
    cmp cx, [sc_rbandt]             ; the click row is this pen row? The BAND
    jb .want                        ; reaches from the row's own top - a click
    mov cx, bp                      ; in a spaced row's leading gap belongs to
    add cx, [sc_gh1]                       ; the row, not to nothing (SPEC.md 68.6)
    cmp cx, [sc_hity]
    jb .want
    cmp byte [sc_hitset], 0
    jne .hit2
    mov [sc_hiti], ax               ; the first index on the row, until a
    mov byte [sc_hitset], 1         ; later cell claims it
.hit2:
    call sc_halfadv                 ; CX = the middle of THIS character, which
    cmp [sc_hitx], cx               ; in a chosen face is half its own advance
    jb .want                        ; and not half a cell: the left half puts
                                    ; the caret before it
    call sc_isnl
    jc .want                        ; a newline has no right half
    inc ax
    mov [sc_hiti], ax
    dec ax
.want:
    cmp word [sc_wanty], 0x7FFF     ; [sc_wanty] is a VISIBLE ROW now, not a
    je .out                         ; pixel (SPEC.md 68.6): a row's y is no
    mov cx, [sc_wanty]              ; longer derivable outside the walk, but
    cmp cx, [sc_row]                ; its NUMBER is what the callers had
    jne .out                        ; anyway. 0x7FFF disables (0xFFFF is a
                                    ; real row: one above the view)
    cmp byte [sc_wantset], 0
    jne .want2
    mov [sc_wanti], ax
    mov byte [sc_wantset], 1
.want2:
    call sc_halfadv
    cmp [sc_wantx], cx
    jb .out
    call sc_isnl
    jc .out
    inc ax
    mov [sc_wanti], ax
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_halfadv - the pixel that splits the character at the pen down the middle
; in:  DI = the pen, ES:SI -> the character, BX = characters left
; out: CX = it; preserves every other register
;
; The half-cell rule of SPEC.md 27.6, made a half-ADVANCE: a click in the left
; half of a character puts the caret before it and one in the right half after.
; In the kernel's cell that is DI + 4 and always was; in a chosen face it is
; half of what THIS character is worth, so a 4px `i` and an 8px `m` each split
; where the reader sees their middle.
; -----------------------------------------------------------------------------
sc_halfadv:
    push ax
    call sc_penadv
    shr ax, 1
    mov cx, di
    add cx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_isnl - is the character at this index a newline (or past the end)?
; in:  BX = characters remaining, SI -> the character
; out: CF=1 = yes, or there is no character here; preserves all registers
; -----------------------------------------------------------------------------
sc_isnl:
    push ax
    test bx, bx
    jz .yes
    mov al, [es:si]                 ; THE DOCUMENT IS ES:SI (SPEC.md 27.6). A
    cmp al, 13                      ; bare [si] read the package IMAGE at the
    je .yes                         ; document's offset - almost never 13, so
    clc                             ; End and a right-half click stepped PAST
    jmp short .out                  ; the line break onto the next row's
.yes:                               ; start. Inherited from Note Pad, where
    stc                             ; np_isnl still has it (worth an upstream
.out:                               ; fix)
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_carets - draw the caret when the walk is standing on its index
; in:  DI/BP = the pen, [sc_i], [sc_cur]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_carets:
    push ax
    push bx
    push cx
    push dx
    cmp byte [sc_selon], 0
    jne .out                        ; a selection REPLACES the caret, which is
                                    ; the Macintosh rule and is also the only
                                    ; honest one here: a 1px black bar inside
                                    ; an inverted band is invisible, so drawing
                                    ; it would cost a line and show nothing
    mov ax, [sc_i]
    cmp ax, [sc_cur]
    jne .out
    mov cx, bp
    add cx, [sc_gh1]
    cmp cx, [sc_bot]
    ja .out                         ; its row does not fit: no caret
    call sc_rowdirty                ; ...nor does a row this pass is not
    jc .out                         ; redrawing (SPEC.md 27.2)
    mov [sc_rcx], di                ; BANKED, not drawn: the row's font_run has
                                    ; not happened yet and would paint over it,
                                    ; so sc_rflush puts it back afterwards
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Row signatures - why a keystroke does not repaint the note (SPEC.md 27.2)
;
; Typing one character used to cost a white fill of the whole content and a
; font_char per character in the note, twice over on Up/Down. Nearly all of
; that redraws pixels that did not move: an edit at the caret cannot change a
; row above it, and it cannot change a row below the newline that ends the
; caret's paragraph either, because a newline resets the pen.
;
; So each visible row carries a one-word signature - a rotate-then-add fold of
; the characters drawn on it, plus the caret's column when the caret is on it.
; Two layouts that fold to the same word put the same glyphs at the same
; pixels, because on any row the k-th glyph is always at [sc_tx] + 8k. It is a
; hash and not a proof, the same trade the Task Manager's rows make (SPEC.md
; 28): a collision leaves one row stale until its content moves again.
;
; The caret is part of the signature and has to be. Moving it off a row has to
; dirty that row, or it stays drawn there.
;
; A redraw is then two walks. The first measures, folds, compares against the
; stored signatures and widens [sc_dr0]..[sc_dr1] - a RANGE, not a bitmap,
; because the interesting cases are all contiguous and a range needs no
; indexing and turns the erase into ONE fill. The second draws, and skips
; every row outside it. If the range comes back empty nothing is drawn at all.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_fold - fold AX into the row being accumulated
; in:  AX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_fold:
    push bx
    mov bx, [sc_rowh]
    rol bx, 1                       ; rotate then add, so a transposition is
    add bx, ax                      ; not invisible
    mov [sc_rowh], bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_rstart - begin accumulating a row: BP is its y, the buffer goes to spaces
; preserves all registers
;
; SPACES and not zeros. font_run paints a space as background on its fast path
; - the glyph's rows are all clear, so the mask leaves the background byte -
; which is what makes one run erase the whole band as well as letter it. That
; is the entire reason this rewrite needs no fill: the padding IS the erase.
; -----------------------------------------------------------------------------
sc_rstart:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov [sc_rby], bp
    mov word [sc_rcx], 0xFFFF
    mov word [sc_rcn], 0            ; no cells stored: in a chosen face this IS
                                    ; the next cell's index, and sc_rowsetup
                                    ; writes cell 0's pen a moment from now
    mov word [sc_rs0], 0xFFFF       ; ...and no inverted cells yet either
    mov word [sc_rs1], 0xFFFF
    mov word [sc_xs0], 0xFFFF       ; ...nor any whose inversion must change
    mov word [sc_xs1], 0xFFFF
    mov ax, [sc_i]                  ; where this row STARTS, banked as the
    mov [sc_ckpc], ax               ; checkpoint candidate: sc_ask promotes it
    mov ax, [sc_row]                ; the moment the walk stands on the caret
    mov [sc_ckpcr], ax              ; (SPEC.md 27.4)
    call sc_xnote                   ; ...and offered to the row index, which is
                                    ; the same fact again for a row OUTSIDE the
                                    ; view - one compare unless it is wanted
                                    ; (SPEC.md 27.13)
    cmp ax, SC_MAXROWS              ; ...and into sc_rows, which is the same
    jae .norow                      ; fact for every row rather than for the
    shl ax, 1                       ; caret's (SPEC.md 27.5)
    mov di, ax
    mov ax, [sc_i]
    mov [di+sc_rows], ax
.norow:
    ; --- the row's GLYPH y, banked and compared (SPEC.md 68.6) --------------
    ; sc_ryb is the heights array in prefix form: what a seeded walk resumes
    ; its y from, and what pass 1 compares so a row whose pixels MOVED is
    ; dirty even when its text did not. A pure measure walk must not touch
    ; it - it describes the GLASS.
    cmp byte [sc_draw], 0
    jne .ry
    cmp byte [sc_sigup], 0
    je .rydone
.ry:
    mov ax, [sc_row]
    cmp ax, SC_MAXROWS
    jae .rydone                     ; unsigned: rows above the view fail too
    cmp ax, [sc_vrows]
    jae .rydone
    shl ax, 1
    mov di, ax
    mov ax, bp
    cmp byte [sc_hasfmt], 0
    je .rystore                     ; uniform rows cannot move; the store
    cmp byte [sc_sigup], 0          ; keeps the bank warm across the 0->1
    je .rystore                     ; transition
    cmp ax, [di+sc_ryb]
    je .rystore
    push ax
    push bx
    mov bx, [di+sc_ryb]
    cmp bx, [sc_ty]                 ; a bank OUTSIDE the band never described
    jb .rynew                       ; a glass row: this row is ENTERING the
    cmp bx, [sc_bot]                ; view (the document grew, or the slot is
    ja .rynew                       ; virgin) - it is dirty, but it did not
                                    ; MOVE, and calling it moved dragged the
                                    ; erase up to rows nobody was redrawing
    mov byte [sc_ymoved], 1         ; THE ROW MOVED: remember the highest
    cmp bx, ax                      ; glyph pixel it owned or owns - the band
    jbe .rya                        ; repaint erases from there to the content
    mov bx, ax                      ; bottom and redraws every row below
.rya:                               ; (SPEC.md 68.6). NEVER higher: the first
    cmp bx, [sc_ymv0]               ; moved row's old and new glyphs both sit
    jae .ryd                        ; strictly below the unmoved row above it,
    mov [sc_ymv0], bx               ; so the erase cannot eat a row nobody
    jmp short .ryd                  ; redraws
.rynew:                             ; entering: an ordinary dirty row - its
                                    ; ground below the content is blank and
                                    ; its opaque run self-erases
.ryd:
    mov bx, [sc_row]
    cmp bx, [sc_dr0]
    jae .rye
    mov [sc_dr0], bx
.rye:
    cmp bx, [sc_dr1]
    jbe .ryf
    mov [sc_dr1], bx
.ryf:
    pop bx
    pop ax
.rystore:
    mov [di+sc_ryb], ax
.rydone:
    mov di, sc_rbuf
    mov cx, [sc_rcols]
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov di, sc_abuf                 ; ...and the attribute cells to PLAIN
    mov cx, [sc_rcols]              ; (SPEC.md 68.1): the padding's erase is a
    xor al, al                      ; plain run, and the run splitter reads
    rep stosb                       ; this beside every cell
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_picdraw - blit the row's picture. in: [sc_rowpic] is its index, [sc_rby]
; the row's glyph y and [sc_rowx0] its start pen. Preserves all registers.
;
; OSAPI_GFX_BLIT4 and not OSAPI_GFX_BLITP, for SPEC.md 93's reason: BLITP
; refuses an armed clip region, and a picture in a document that scrolls is
; always inside one.
;
; BP IS PUSHED HERE and nowhere else in this file's drawing path, because BP is
; the WALK's pen y - and BLIT4 takes the source stride in it.
; -----------------------------------------------------------------------------
sc_picdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, [sc_rowpic]
    call sc_picrec
    jc .out
    mov cx, [bx]                    ; width
    mov dx, [bx+2]                  ; height
    mov bp, [bx+4]                  ; stride - BLIT4's own argument
    mov ax, [bx+6]
    or ax, ax
    jz .out
    mov es, ax
    xor si, si                      ; the picture is at offset 0 of its claim
    mov ax, [sc_rowx0]              ; the row's own start pen: a picture obeys
                                    ; the paragraph's indent like any row does
    mov bx, [sc_rby]
    add bx, [sc_gh]
    sub bx, dx                      ; ...and sits ON the row, so its BOTTOM is
    call OSAPI_GFX_BLIT4            ; where the glyphs' bottom would have been
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
; sc_rflush - draw the accumulated row: ONE opaque font_run, then its caret
; preserves all registers
;
; This replaced a GFX_FILL of the whole dirty band followed by a FONT_CHAR per
; character, and the reason is not only that it is faster (SPEC.md 11.94: 30.1
; ms against 33.3 for a forty-cell line on a 4.77MHz 8088). It is that the
; pair leaves the line BLANK between the fill and the last glyph, and at 33 ms
; a keystroke that gap is several display frames - it flickers, visibly, on
; every keypress. A run writes each cell from its old content straight to its
; final content, so there is never a moment when the line is empty (SPEC.md
; 6.1). Measured and then watched: the benchmark's two erase-and-letter rows
; flash on the XT and its font_run row does not.
;
; The caret is drawn AFTER the run and not during the walk, because the run
; would paint over it. sc_carets banks its x instead of drawing.
;
; Three things this must not draw: a row of a measure pass, a row this redraw
; already knows is right (sc_rowdirty, SPEC.md 27.2), and a row whose pixels
; fall past the content bottom - all three the same tests the per-character
; draw used to make, moved up to the row.
; -----------------------------------------------------------------------------
sc_rflush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sc_draw], 0
    je .out
    call sc_rowdirty
    jc .out                         ; a row this redraw already knows is right
    mov ax, [sc_rby]
    cmp ax, [sc_ty]
    jb .out                         ; ABOVE the view: scrolled off the top
    add ax, [sc_gh1]                       ; (SPEC.md 27.7), and the unsigned tests
    cmp ax, [sc_bot]                ; elsewhere cannot see this one, because a
    ja .out                         ; row a little above sc_ty has an ordinary
                                    ; small y. Below: it does not fit, the pen
                                    ; still
                                    ; advanced, so every position below is true
    cmp word [sc_rowpic], 0xFFFF    ; a PICTURE row: one blit, and none of the
    je .notpic                      ; lettering below - the row buffer holds no
    call sc_picdraw                 ; glyphs for it (SPEC.md 94.9)
    jmp .caret
.notpic:
    cmp word [sc_rcols], 0
    je .caret

    ; --- ONLY THE INVERSION MOVED (SPEC.md 27.8.2) -------------------------
    ; A drag changes no character anywhere. What it changes is which cells are
    ; inverted, and XOR is exactly the operation for that - so flip the cells
    ; whose selected-ness differs from the screen's and letter NOTHING. The
    ; row's glyphs are already correct and re-drawing them to invert them was
    ; costing a full row of font_run per dirty row per pass.
    ;
    ; Gated on a selection existing at BOTH ends, which is what guarantees no
    ; caret is drawn either before or after (sc_carets returns early while one
    ; is up): a caret bar sits on top of a glyph, so erasing one needs that
    ; cell lettered again and this path draws no cells.
    cmp byte [sc_selonly], 0
    je .normal
    cmp byte [sc_selon], 0
    je .normal
    cmp byte [sc_oselon], 0
    je .normal
    mov ax, [sc_xs0]
    cmp ax, 0xFFFF
    je .cache                       ; this row's inversion is already right
    mov cx, [sc_xs1]
    call sc_cx                      ; x1 - through sc_cx, which is the same
    push ax                         ; three shifts in the kernel's cell and a
    mov ax, cx                      ; sc_px[] lookup in a chosen face
    inc ax                          ; (SPEC.md 68.13)
    call sc_cx                      ; ...and the slot PAST the span's last cell
    dec ax                          ; is why sc_px[] carries one
    mov cx, ax                      ; x2
    pop ax
    mov bx, [sc_rby]
    mov dx, bx
    add dx, [sc_gh1]
    call OSAPI_GFX_XOR_FILL
    jmp .cache                      ; sc_prow still describes the screen -
                                    ; not one character moved - but .cache
                                    ; owes sc_prs0/sc_prs1 the new span
.normal:

    mov word [sc_fcc], 0xFFFF       ; this row's caret column, if it has one
    mov ax, [sc_rcx]
    cmp ax, 0xFFFF
    je .span
    call sc_xc
    mov [sc_fcc], ax
.span:
    mov word [sc_flo], 0            ; the default span is the whole row - which
    mov ax, [sc_rcols]              ; in a chosen face is the cells the WALK
    dec ax                          ; stored plus the one the caret parks in,
    cmp byte [sc_pxon], 0           ; because a cell past those has no pen and
    je .spanhi                      ; nothing to draw at (SPEC.md 68.13). The
    mov ax, [sc_rcn]                ; tail past it is erased in pixels below
.spanhi:
    mov [sc_fhi], ax
    mov ax, [sc_row]
    cmp ax, [sc_prowi]
    je .delta                       ; the cached row diffs cell by cell
    ; a WHOLE-row redraw of a spaced row also erases its leading GAP - the
    ; band between the row's top and its glyphs (SPEC.md 68.6). Glyph runs
    ; never touch it, so ink parked there by an older layout (a caret bar, a
    ; taller row's letters) would survive every redraw without this. One fill,
    ; only for a row taller than its glyphs, only when the band was not
    ; already filled.
    cmp byte [sc_hasfmt], 0
    je .draw
    cmp byte [sc_clean], 0
    jne .draw
    mov ax, [sc_rbandt]
    cmp ax, [sc_ty]
    jae .gt
    mov ax, [sc_ty]
.gt:
    cmp ax, [sc_rby]
    jae .draw                       ; an 8px row: no gap
    push bx
    push cx
    push dx
    mov bx, ax                      ; y1 = the band top
    mov dx, [sc_rby]
    dec dx                          ; y2 = just above the glyphs
    mov ax, [sc_tx]
    sub ax, SC_MARGIN
    mov cx, [sc_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    jmp .draw
.delta:

    mov word [sc_flo], 0xFFFF       ; --- the delta, cell by cell -----------
    mov word [sc_fhi], 0xFFFF
    xor bx, bx
.dl:
    cmp bx, [sc_rcols]
    jae .dfold
    mov al, [sc_rbuf+bx]
    cmp al, [sc_prow+bx]
    jne .dmk
    mov al, [sc_abuf+bx]            ; same character, different dress: a
    cmp al, [sc_pattr+bx]           ; formatting change moves pixels too
    je .dn                          ; (SPEC.md 68.1)
.dmk:
    cmp word [sc_flo], 0xFFFF
    jne .dhi
    mov [sc_flo], bx
.dhi:
    mov [sc_fhi], bx
.dn:
    inc bx
    jmp short .dl
.dfold:
    mov ax, [sc_prcc]               ; the caret's cells count as changed at
    call sc_fold1                   ; both ends: the one it left has to lose
    mov ax, [sc_fcc]                ; its bar, and the one it arrived at has
    call sc_fold1                   ; to get one
    mov ax, [sc_rs0]                ; ...and so does the SELECTION, for exactly
    cmp ax, [sc_prs0]               ; the same reason and only when it MOVED:
    jne .selchg                     ; a row whose inverted span is unchanged
    mov ax, [sc_rs1]                ; has the right pixels already, and folding
    cmp ax, [sc_prs1]               ; it in unconditionally would redraw every
    je .seldone                     ; selected row on every pass
.selchg:
    mov ax, [sc_prs0]               ; the union of the two spans, which
    call sc_fold1                   ; contains every cell whose inverted-ness
    mov ax, [sc_prs1]               ; changed. Wider than the difference and
    call sc_fold1                   ; very much simpler; a selection is a
    mov ax, [sc_rs0]                ; static thing, so this runs once as it
    call sc_fold1                   ; arrives and once as it leaves
    mov ax, [sc_rs1]
    call sc_fold1
.seldone:
    cmp word [sc_flo], 0xFFFF
    je .cache                       ; nothing moved: draw NOTHING
    push bx                         ; a bold/italic strike bleeds one pixel
    mov bx, [sc_fhi]                ; into the cell to its RIGHT (SPEC.md
    test byte [sc_pattr+bx], SCAT_BOLD | SCAT_ITAL
    jz .nobled                      ; 65.1): when the span's last cell WAS
    inc bx                          ; drawn with one of those, the neighbour
    cmp bx, [sc_rcols]              ; may be carrying its bleed, so it joins
    jae .nobled                     ; the span and is re-lettered opaque
    mov ax, bx
    call sc_fold1
.nobled:
    pop bx

.draw:
    cmp byte [sc_clean], 0          ; the band is known blank (a full repaint
    je .draw2                       ; white-filled it, SPEC.md 27.2), so the
    mov cx, [sc_flo]                ; padding has nothing to erase and the run
    mov ax, [sc_rs1]                ; stops at the last real character - but
    cmp ax, 0xFFFF                  ; never short of the last SELECTED one: a
    je .tf                          ; selected trailing space is drawn to be
    cmp ax, cx                      ; inverted, and trimming it away would
    jbe .tf                         ; leave a gap in the highlight
    mov cx, ax
.tf:
    mov bx, [sc_fhi]                ; A fullscreen window is 90 cells wide and
.tl:                                ; a note is rarely that long: without this
    cmp byte [sc_rbuf+bx], ' '      ; a repaint costs rows x width instead of
    jne .tdone                      ; characters, and on a 4.77MHz 8088 that
    cmp byte [sc_abuf+bx], 0        ; is the difference between half a second
    jne .tdone                      ; and five. A DRESSED space is not blank -
    cmp bx, cx                      ; an underlined space is a rule, a pilcrow
    jbe .tstop                      ; cell is a stamp - so the trim stops at
    dec bx                          ; any styled cell (SPEC.md 68.1)
    jmp short .tl
.tstop:
    cmp word [sc_rs1], 0xFFFF       ; all blank from the floor up: nothing to
    je .cache                       ; do, unless a selection reaches here
.tdone:
    mov [sc_fhi], bx
.draw2:
    cmp byte [sc_pxon], 0           ; --- the TAIL, in pixels (SPEC.md 68.13) -
    je .runs                        ; A fixed row erases itself: the buffer is
                                    ; space-padded to [sc_rcols] and one opaque
                                    ; run covers the band. A proportional row
                                    ; cannot pad, because a cell past its
                                    ; content has no pen - so the span stops at
                                    ; the cells the walk stored and ONE white
                                    ; fill covers the ground the row's glyphs
                                    ; used to reach and no longer do.
    mov ax, [sc_rcn]
    cmp ax, [sc_fhi]
    ja .tailhi
    mov [sc_fhi], ax                ; ...the caret's parking cell included: a
.tailhi:                            ; caret that moved off the end has to be
                                    ; lettered over, and the cell it stood in
                                    ; is the one past the last character - so
                                    ; sc_px[] carries a slot past THAT one too

    ; --- and GROWN to byte columns at both ends (SPEC.md 5.4.2) -------------
    ; The band is blitted by whole byte columns and is paper where nothing was
    ; composed, so a span whose first cell starts mid-byte whitens up to 7
    ; pixels of the glyph to its LEFT, and one whose last cell ends mid-byte
    ; does the same on its right. Growing the span outwards until both edges
    ; land on a multiple of 8 redraws a neighbour or two and rubs out none.
    ; In the kernel's cell every cell edge IS a multiple of 8 and neither loop
    ; runs past its first test.
    mov ax, [sc_flo]
.blo:
    or ax, ax
    jz .blodone                     ; the row's own first cell: what is left of
    push ax                         ; it is the margin, and blank
    call sc_cx
    test ax, 7
    pop ax
    jz .blodone
    dec ax
    jmp short .blo
.blodone:
    mov [sc_flo], ax
    mov ax, [sc_fhi]
.bhi:
    cmp ax, [sc_rcn]
    jae .bhidone                    ; past the row's content: blank ground
    push ax
    inc ax
    call sc_cx
    test ax, 7
    pop ax
    jz .bhidone
    inc ax
    jmp short .bhi
.bhidone:
    mov [sc_fhi], ax

    mov ax, [sc_fhi]
    inc ax
    call sc_cx
    mov cx, ax                      ; CX = where this row's ink now ends
    mov ax, [sc_prowrx]             ; ...against where it ended when last drawn
    cmp byte [sc_clean], 0
    jne .tailno                     ; a blank band owes nothing
    push bx
    mov bx, [sc_row]
    cmp bx, [sc_prowi]
    pop bx                          ; POP touches no flags
    je .tailcmp
    mov ax, [sc_rgt]                ; the cache is some other row's: erase to
    inc ax                          ; the margin, which is what the padding
.tailcmp:                           ; run did for the fixed face
    cmp ax, cx
    jbe .tailno
    push ax
    push bx
    push cx
    push dx
    xchg ax, cx                     ; AX = x1, the new right edge; CX = x2, the
    dec cx                          ; last column the old row still owns
    mov bx, [sc_rby]
    mov dx, bx
    add dx, [sc_gh1]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
.tailno:
    mov [sc_prowrx], cx             ; ...and this is the edge the next pass
.runs:                              ; measures against
    ; --- the span, split into runs of equal CHP (SPEC.md 68.1) --------------
    ; Adjacent plain cells are one run by construction, so unstyled text is
    ; exactly the one opaque font_run it always was; each styled run draws by
    ; its dress in sc_drawrun. Left to right, so a bold strike's 1px bleed
    ; into the next run is repainted by that run's own opaque pass.
    mov bx, [sc_flo]
.runl:
    cmp bx, [sc_fhi]
    ja .rundone
    mov al, [sc_abuf+bx]
    mov [sc_runa], al
    mov si, bx
.runx:
    inc si
    cmp si, [sc_fhi]
    ja .runend
    cmp al, [sc_abuf+si]
    je .runx
.runend:
    mov [sc_r0], bx                 ; the run: cells [sc_r0]..[sc_r1]
    mov ax, si
    dec ax
    mov [sc_r1], ax
    call sc_drawrun
    mov bx, si
    jmp short .runl
.rundone:
    call sc_selxor                  ; the runs drew the cells upright; invert
                                    ; the selected ones they covered (SPEC.md
                                    ; 27.8). AFTER the runs, for the reason the
                                    ; caret is drawn after them: a run would
                                    ; paint over an inversion made first

.cache:
    push es                         ; the span was drawn, so the screen now
    push ds                         ; shows sc_rbuf: remember it, and remember
    pop es                          ; which row and where its caret is
    cld
    mov si, sc_rbuf
    mov di, sc_prow
    mov cx, [sc_rcols]
    rep movsb
    mov si, sc_abuf                 ; ...and the attributes it was drawn with,
    mov di, sc_pattr                ; or the diff above could not see a
    mov cx, [sc_rcols]              ; formatting change on the cached row
    rep movsb
    pop es
    mov ax, [sc_row]
    mov [sc_prowi], ax
    mov ax, [sc_fcc]
    mov [sc_prcc], ax
    mov ax, [sc_rs0]                ; ...and which of its cells came out
    mov [sc_prs0], ax               ; inverted, or the next pass has no way to
    mov ax, [sc_rs1]                ; tell that the highlight moved off a row
    mov [sc_prs1], ax               ; whose characters did not

.caret:
    mov ax, [sc_rcx]
    cmp ax, 0xFFFF
    je .out
    mov bx, [sc_rby]                ; 1px black caret, 8 rows tall, on top of
    mov dx, bx                      ; the run that would otherwise have eaten it
    add dx, [sc_gh1]
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_fold1 - fold column AX into [sc_flo]..[sc_fhi]; 0xFFFF folds nothing.
; Preserves everything.
sc_fold1:
    cmp ax, 0xFFFF
    je .out
    cmp word [sc_flo], 0xFFFF
    jne .lo
    mov [sc_flo], ax
    mov [sc_fhi], ax
    ret
.lo:
    cmp ax, [sc_flo]
    jae .hi
    mov [sc_flo], ax
.hi:
    cmp ax, [sc_fhi]
    jbe .out
    mov [sc_fhi], ax
.out:
    ret

; =============================================================================
; The styled run drawers (SPEC.md 68.1)
;
; sc_rflush splits the dirty span into runs of equal CHP and hands each one
; here. A plain run is exactly the one opaque font_run the engine always drew;
; bold adds a transparent second strike one pixel right; an italic run is
; staged from the pre-sheared 4bpp glyph table and lands as ONE gfx_blit4;
; the underlines are rules drawn after the glyphs; a pilcrow cell (Show-all's
; paragraph mark) is stamped from an 8x8 image. Priced in calls: a plain row
; is one call as before, a styled run costs one or two more.
; =============================================================================

; the pilcrow, 8x8 at 4bpp, black (0) on white (0xF): bowl over two stems
sc_pimg:
    db 0xF0, 0x00, 0x0F, 0x0F       ; .####.#.
    db 0xF0, 0x00, 0x0F, 0x0F
    db 0xF0, 0x00, 0x0F, 0x0F
    db 0xFF, 0x00, 0x0F, 0x0F       ; ..###.#.
    db 0xFF, 0xFF, 0x0F, 0x0F       ; ....#.#.
    db 0xFF, 0xFF, 0x0F, 0x0F
    db 0xFF, 0xFF, 0x0F, 0x0F
    db 0xFF, 0xFF, 0xFF, 0xFF

; nibble -> two 4bpp bytes (four pixels), a set bit = BLACK (0 nibble),
; a clear bit = WHITE (0xF): the 1bpp -> 4bpp expansion, sixteen entries
sc_x4tab:
%assign SCX4N 0
%rep 16
  %assign SCX4B0 0
  %if (SCX4N & 8) == 0
    %assign SCX4B0 SCX4B0 | 0xF0
  %endif
  %if (SCX4N & 4) == 0
    %assign SCX4B0 SCX4B0 | 0x0F
  %endif
  %assign SCX4B1 0
  %if (SCX4N & 2) == 0
    %assign SCX4B1 SCX4B1 | 0xF0
  %endif
  %if (SCX4N & 1) == 0
    %assign SCX4B1 SCX4B1 | 0x0F
  %endif
    db SCX4B0, SCX4B1
  %assign SCX4N SCX4N + 1
%endrep

; -----------------------------------------------------------------------------
; sc_itinit - make sure the sheared italic glyph table exists (SPEC.md 68.1)
; out: CF=0 with [sc_iseg] the claim holding 95 glyphs x 8 rows x 4 bytes;
;      CF=1 the heap refused (italic degrades to the plain glyph).
;      Preserves all registers.
; Built ONCE, at the first italic run drawn: OSAPI_FONT_GLYPHS is read, rows
; 0..3 sheared one pixel right, and every row expanded 1bpp -> 4bpp
; black-on-white through sc_x4tab, so staging a run is a straight copy.
; -----------------------------------------------------------------------------
sc_itinit:
    cmp word [sc_iseg], 0
    jne .ok
    cmp byte [sc_inwk], 0           ; on the worker's draw pass: a worker may
    jne .nowk                       ; not claim memory (SPEC.md 20.6 rule 7).
                                    ; Letter this run plain; the next UI-task
                                    ; redraw builds the table
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, SC_ITKB                 ; the table AND the staging area
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sc_iseg], dx
    call OSAPI_FONT_GLYPHS          ; DX:SI = the kernel's table (NOT in
    mov [sc_itsg], dx               ; KERNEL_SEG - read through ES = DX)
    mov [sc_itso], si
    mov word [sc_itch], 0
.g:
    mov si, [sc_itch]               ; the glyph's 8 source bytes, banked so
    push cx                         ; the two segments never overlap in one
    mov cl, 3                       ; loop
    shl si, cl
    pop cx
    add si, [sc_itso]
    mov es, [sc_itsg]
    xor bx, bx
.f:
    mov al, [es:si+bx]
    mov [sc_itmp+bx], al
    inc bx
    cmp bx, 8
    jb .f
    mov di, [sc_itch]
    push cx
    mov cl, 5
    shl di, cl                      ; x32: the glyph's slot in the claim
    pop cx
    mov es, [sc_iseg]
    xor bx, bx                      ; BX = row
.r:
    mov al, [sc_itmp+bx]
    cmp bx, 4
    jae .nosh
    shr al, 1                       ; THE SHEAR: rows 0..3 one pixel right
.nosh:
    mov ah, al
    push cx
    mov cl, 4
    shr ah, cl
    pop cx
    push ax
    mov al, ah                      ; high nibble -> the first two bytes
    xor ah, ah
    shl ax, 1
    mov si, ax
    mov ax, [sc_x4tab+si]
    mov [es:di], ax
    pop ax
    and al, 0x0F                    ; low nibble -> the second two
    xor ah, ah
    shl ax, 1
    mov si, ax
    mov ax, [sc_x4tab+si]
    mov [es:di+2], ax
    add di, 4
    inc bx
    cmp bx, 8
    jb .r
    inc word [sc_itch]
    cmp word [sc_itch], 95
    jb .g
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ok:
    clc
    ret
.fail:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.nowk:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_itrun - draw the run [sc_r0]..[sc_r1] in italics: stage, then ONE blit
; in:  [sc_r0]/[sc_r1]/[sc_runa]/[sc_rby], sc_rbuf holds the characters,
;      gfx lock held
; out: CF=0 drawn; CF=1 no table (the caller letters it plain instead)
;      Preserves all registers.
; Bold-italic ANDs the staged image with itself shifted one pixel right -
; ink is the 0 nibble, so AND of bytes IS the union of ink.
; -----------------------------------------------------------------------------
sc_itrun:
    call sc_itinit
    jc .no
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, [sc_r1]
    sub cx, [sc_r0]
    inc cx                          ; CX = cells in the run
    mov ax, cx
    shl ax, 1
    shl ax, 1
    mov [sc_itstr], ax              ; the staged image's stride in bytes
    xor dx, dx                      ; DX = the cell being staged
.cell:
    cmp dx, cx
    jae .staged
    mov bx, [sc_r0]
    add bx, dx
    mov al, [sc_rbuf+bx]            ; already case-mapped by the walk
    sub al, 32
    cmp al, 95
    jb .cok
    xor al, al                      ; outside the table: the space glyph
.cok:
    xor ah, ah
    push cx
    mov cl, 5
    shl ax, cl
    pop cx
    mov si, ax                      ; SI = the glyph in the claim
    mov di, dx
    shl di, 1
    shl di, 1
    add di, SC_STG4                 ; DI = its column in the staging area
    push cx
    push dx
    push ds
    mov ds, [cs:sc_iseg]            ; DS:SI = the table and ES:DI = the
    push ds                         ; staging area - the SAME claim now, so
    pop es                          ; the copy never leaves it
    cld
    mov dx, 8
.row:
    movsw
    movsw
    add di, [cs:sc_itstr]           ; CS: - DS is the table's segment here
    sub di, 4
    dec dx
    jnz .row
    pop ds
    pop dx
    pop cx
    inc dx
    jmp short .cell
.staged:
    test byte [sc_runa], SCAT_BOLD
    jz .noem
    mov es, [sc_iseg]               ; the staged image lives in the italic
    mov si, SC_STG4                 ; claim (SPEC.md 68.1), so every touch of
    mov dx, 8                       ; it below carries an ES override
.em:                                ; bold-italic: AND in the 1px-right copy,
                                    ; row by pixel row, right to left so the
                                    ; source byte is still the original
    mov di, [sc_itstr]
    dec di
.emb:
    or di, di
    jz .emb0
    mov bx, si
    add bx, di                      ; BX = the byte (SI+DI is no 8086 mode)
    mov al, [es:bx-1]
    mov ah, [es:bx]
    push cx
    mov cl, 4
    shl al, cl
    shr ah, cl
    pop cx
    or al, ah
    and [es:bx], al
    dec di
    jmp short .emb
.emb0:
    mov al, [es:si]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    or al, 0xF0                     ; white shifts in at the left edge
    and [es:si], al
    add si, [sc_itstr]
    dec dx
    jnz .em
.noem:
    push bp                         ; BP is the walk's pen y - the blit wants
    mov es, [sc_iseg]               ; the stride there (SPEC.md 5.7)
    mov si, SC_STG4
    mov bp, [sc_itstr]
    mov ax, [sc_r0]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_tx]                 ; AX = the run's x
    mov bx, [sc_rby]                ; BX = its row's y
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = width in pixels
    mov dx, 8
    call OSAPI_GFX_BLIT4
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_drawrun - draw one equal-CHP run of the row being flushed
; in:  [sc_r0]/[sc_r1] = the run's cells, [sc_runa] = their attribute,
;      [sc_rby] = the row's y, sc_rbuf accumulated; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; The band, opened once a ROW and not once a run (SPEC.md 6.3/68.13)
;
; SPEC.md 6.3's method, and the whole of what a proportional face costs at draw
; time: ty_band, a ty_putn a run, ty_flush. It replaces the row's OSAPI_FONT_RUN
; with one OSAPI_GFX_BLIT1, so the call count a row costs does not move - what
; moves is that the glyphs are the face's instead of the cell's.
;
; THE BACK-UP RULE (SPEC.md 5.4.2) is why the band belongs to the ROW. The blit
; wants an x on a multiple of 8, and a run does not start on one - so the band
; starts at the byte column to its LEFT, the 0..7 remainder becomes the pen
; inside the band, and the width is rounded up to the next byte column. The band
; is PAPER where nothing was composed, so those few pixels are whitened: at the
; span's outer edges that is exactly what FONT_RUN's background would have done,
; and in its MIDDLE it would rub out the tail of the run before. One band a row
; has no middle. (sc_rflush grows the span outwards to byte columns for the two
; outer edges, so no neighbour is rubbed out either.)
;
; sc_bandopen  - paper, and the byte column the band starts at
; sc_bandrun   - compose one equal-CHP run into it; NOTHING is drawn
; sc_bandclose - one blit, and the row is on the glass
; All three preserve every register.
; -----------------------------------------------------------------------------
sc_bandrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov dx, [sc_gh]
    call ty_band                    ; paper, as tall as the face

    mov ax, [sc_r0]                 ; the run's first cell, in screen x
    call sc_cx
    mov bx, ax
    and ax, 0xFFF8                  ; ...backed up to its byte column
    mov [sc_bx0], ax
    sub bx, ax                      ; BX = the pen inside the band, 0..7

    push ds
    pop es                          ; ES:SI = the text, which is ours here
    mov si, [sc_r0]
    add si, sc_rbuf
    mov cx, [sc_r1]
    sub cx, [sc_r0]
    inc cx
    mov ax, bx
    call ty_putn                    ; AX = the pen after the run
    test byte [sc_runa], SCAT_BOLD
    jz .nbold
    push ax                         ; bold: the same glyphs again, one pixel
    mov ax, bx                      ; right (SPEC.md 61.5/68.1). In the BAND
    inc ax                          ; this is a second compose rather than a
    push si                         ; second drawing call - it costs no floor
    call ty_putn                    ; at all, where the cell arm pays a whole
    pop si                          ; OSAPI_FONT_STR_XPARENT for it
    pop ax
    inc ax                          ; ...and the run is one pixel wider
.nbold:

%if SC_BANDDBG
    ; SC_BANDDBG: every band goes down as a 50% dither instead of as text, so
    ; "is this path running, and over which pixels" is a question a screendump
    ; answers. It is kept because it is what FOUND the tail-jump bug (SPEC.md
    ; 65.13.1): the bars landed at exactly the right x, width, y and height on
    ; every row, which proved the whole rendering path correct in one look and
    ; left the caller as the only suspect.
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, ty_bandbuf
    mov cx, TY_BANDSZ
    mov al, 0xAA
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
%endif
    add ax, 7                       ; ...and the width, rounded up to a whole
    and ax, 0xFFF8                  ; byte column
    mov cx, ax
    mov ax, [sc_bx0]
    mov bx, [sc_rby]
    mov dx, [sc_gh]
    call ty_flush                   ; CF=1 = the kernel has no band blit, and
                                    ; the row is simply not drawn in the face.
                                    ; Not worth a fallback here: the machine
                                    ; that answers so cannot load this program
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sc_drawrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [sc_runa], SCAT_PIL
    jz .nopil
    mov bx, [sc_r0]                 ; Show-all's paragraph mark: one 8x8 stamp
.pl:                                ; per cell (there is one cell per mark)
    cmp bx, [sc_r1]
    ja .done
    mov ax, bx
    call sc_cx
    push bx
    push bp
    push es
    push ds
    pop es
    mov si, sc_pimg
    mov bp, 4
    mov bx, [sc_rby]
    mov cx, 8
    mov dx, 8
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    pop bx
    inc bx
    jmp short .pl
.nopil:
    test byte [sc_runa], SCAT_ITAL
    jz .plain
    cmp byte [sc_prop], 0           ; sc_itrun shears the KERNEL's glyphs
    jne .plain                      ; (SPEC.md 68.1), so in a chosen face it
                                    ; would letter one run in a DIFFERENT
                                    ; TYPEFACE from the words either side of
                                    ; it - which reads as a bug rather than as
                                    ; italic. Upright in the right face is the
                                    ; better wrong answer until a face carries
                                    ; a drawn italic (SPEC.md 6.4's style 2)
    call sc_itrun                   ; staged and blitted - or CF=1 with no
    jnc .rules                      ; table, and the plain letters below are
                                    ; the degrade (SPEC.md 68.1)
.plain:
    cmp byte [sc_prop], 0
    je .cell
    call sc_bandrun                 ; SPEC.md 6.3: composed once, put down once
    jmp .rules
.cell:
    mov bx, [sc_r1]                 ; NUL-cap the run inside the row string,
    inc bx                          ; letter it, put the byte back
    mov al, [sc_rbuf+bx]
    push ax
    mov byte [sc_rbuf+bx], 0
    push bx
    mov si, [sc_r0]
    mov ax, si
    call sc_cx
    mov cx, ax                      ; CX = x of the run's first cell
    add si, sc_rbuf
    mov dx, [sc_rby]
    mov al, CBLACK                  ; ink and background in one call: the erase
    mov ah, CWHITE                  ; and the letters are one decision per cell
    call OSAPI_FONT_RUN
    test byte [sc_runa], SCAT_BOLD
    jz .nbold
    inc cx                          ; bold: the same glyphs again, transparent,
    push ax                         ; one pixel right (SPEC.md 61.5/68.1) -
    mov al, CBLACK                  ; FONT_STR ORs ink and erases nothing
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_FONT_STR_XPARENT
.nbold:
    pop bx
    pop ax
    mov [sc_rbuf+bx], al
.rules:
    mov al, [sc_runa]
    test al, SCAT_UL | SCAT_WUL | SCAT_DUL
    jz .done
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    mov ax, [sc_r0]
    call sc_cx                      ; AX = x1
    push ax
    mov ax, [sc_r1]
    inc ax
    call sc_cx
    dec ax
    mov bx, ax                      ; BX = x2
    pop ax
    mov dx, [sc_rby]
    add dx, [sc_gh1]                       ; the cell's last row
    test byte [sc_runa], SCAT_UL
    jz .nul
    call OSAPI_GFX_HLINE
.nul:
    test byte [sc_runa], SCAT_DUL
    jz .ndul
    call OSAPI_GFX_HLINE            ; double underline: rows 7 and 6
    dec dx
    call OSAPI_GFX_HLINE
    inc dx
.ndul:
    test byte [sc_runa], SCAT_WUL
    jz .done
    mov si, [sc_r0]                 ; word underline: a rule under each
.ws:                                ; non-space span of the run
    cmp si, [sc_r1]
    ja .done
    cmp byte [sc_rbuf+si], ' '
    je .wnext
    mov di, si
.wx:
    inc si
    cmp si, [sc_r1]
    ja .wdraw
    cmp byte [sc_rbuf+si], ' '
    jne .wx
.wdraw:
    mov ax, si
    call sc_cx
    dec ax
    mov bx, ax                      ; BX = x2, the span's last column
    mov ax, di
    call sc_cx                      ; AX = x1
    call OSAPI_GFX_HLINE
.wnext:
    inc si
    jmp short .ws
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The visual break - typing in FRONT of text without reflowing it (SPEC.md 27.3)
;
; Inserting a character at the front of a note moves every character after it,
; and there is no cheaper way to draw that than to draw it: forty rows of
; forty cells is 1,600 cells, and on a 4.77MHz 8088 a cell is about a
; millisecond (SPEC.md 6.1.1). One keystroke, most of a second. The delta
; cache of 27.2 does not help - the cells really did all change.
;
; So they are not drawn. The rows below the caret are SCROLLED down by one,
; and what the screen then shows is the note with a line break at the caret
; that the note does not contain - the text after the caret hangs on the next
; row at the column it already occupied, and everything below it has moved
; down a row. The caret keeps the rest of its own row to type on, at 27.2's
; two cells a keystroke, and when it runs out of row the rows below are
; pushed down again.
;
; Four things hold this up, and each is a rule rather than a tuning:
;
;  1. It is a LIE, so it is temporary and it says so by settling. The
;     reconcile runs half a second after the last keystroke, when the window
;     stops being frontmost, and before anything that is not typing (a click,
;     an arrow, Enter, a menu command, a save, a resize). A user's normal
;     rhythm is type-then-read, and a note that stayed broken while being
;     read would be read as the note.
;  2. It is gated on the machine, not on the adapter: OSAPI_CPU_INFO must say
;     CPU_8086. Anywhere faster the reflow is already invisible and the lie
;     buys nothing.
;  3. The trigger is CELLS, not rows. This window is resizable and a row is
;     30 cells or 90 depending how wide it was dragged, so a row count is two
;     different amounts of work wearing one number.
;  4. It needs [sc_tx] on a multiple of 8, because OSAPI_GFX_SCROLL is
;     byte-column granular on every adapter. OSAPI_WM_SNAP guarantees that on
;     EVERY adapter now (SPEC.md 11.94 - it was mono-only, and VGA turned out
;     to gain more from alignment than mono does), so the coin flip this note
;     used to describe on VGA is gone and rule 2's CPU test is the only gate
;     left. On a window too wide to snap the alignment still fails, the break
;     does not engage and the reflow is what happens. That is a FACT the code
;     can test, not a guess (SPEC.md 47 rule 3).
; =============================================================================

; -----------------------------------------------------------------------------
; sc_scroll - move row AX and everything below it down one row
; in:  AX = the first row index to move, sc_bounds already run, gfx lock held
; out: CF = OSAPI_GFX_SCROLL's answer; preserves all registers
;
; The x span is the whole content width rounded IN to byte columns. [sc_tx] is
; a multiple of 8 (the caller checked) and the left margin is SC_MARGIN = 8,
; so x1 is the content's own left edge; x2+1 rounds the content's right edge
; down, which can only ever drop part of the <8px tail past the last cell -
; the band no glyph reaches.
; -----------------------------------------------------------------------------
sc_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]
    mov bx, ax                      ; BX = y1
    mov dx, [sc_bot]                ; DX = y2
    mov ax, [sc_tx]
    sub ax, SC_MARGIN               ; AX = x1
    mov cx, [sc_rgt]
    inc cx
    and cx, 0xFFF8
    dec cx                          ; CX = x2, x2+1 a multiple of 8
    mov si, -8                      ; down one row
    call OSAPI_GFX_SCROLL
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; POP does not touch flags: CF is still
                                    ; the scroll's answer

; -----------------------------------------------------------------------------
; sc_bpush - sc_walk reached a new row while the break is up: push the note
;            below it down to make room
; in:  [sc_row] = the row the pen just moved ONTO; gfx lock held
; out: nothing; preserves all registers
;
; Called from sc_walk's wrap site, AFTER the row that was ending has been
; flushed and BEFORE the new one starts - the only moment at which the row
; above is finished and the row below has not been touched.
;
; A refusal is the band below the caret being shorter than the row we are
; asking to insert, which is what happens when the caret reaches the bottom
; of the window. Nothing was moved, so the screen is still consistent with
; the note as far as this row; the caller settles.
; -----------------------------------------------------------------------------
sc_bpush:
    cmp byte [sc_bmode], 0
    je .out
    cmp byte [sc_draw], 0           ; a measure pass moves no pixels
    je .out
    push ax
    mov ax, [sc_row]
    cmp ax, [sc_bcrow]
    jbe .pop                        ; still on the break's own row
    call sc_scroll
    jc .fail
    mov ax, [sc_row]
    mov [sc_bcrow], ax
    mov word [sc_prowi], 0xFFFF     ; the vacated row holds a copy of the one
    mov byte [sc_didpush], 1        ; above it: nothing the delta cache knows
    jmp short .pop
.fail:
    mov byte [sc_bfail], 1
.pop:
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_brkdraw - one keystroke while the break is up
; in:  SI = window ptr, gfx lock held, [sc_ckok] set
; out: nothing; clobbers what a callback may
;
; ONE walk, from the checkpoint to the caret and no further - so a keystroke
; costs the caret's own row and nothing else, whatever the note weighs. No
; signature pass: the rows below the caret are not being laid out, so there
; is nothing to compare them against.
; -----------------------------------------------------------------------------
sc_brkdraw:
    push ax
    push bx
    mov word [sc_hity], 0xFFFF
    mov word [sc_wanty], 0x7FFF
    mov word [sc_dr1], 0            ; sc_walk's blank loop must not run: it
                                    ; erases rows this pass has no opinion on
    mov byte [sc_draw], 1
    mov byte [sc_sigup], 0
    mov byte [sc_clip], 0
    call sc_seedck
    mov byte [sc_bstop], 1
    mov byte [sc_didpush], 0
    mov byte [sc_bfail], 0
    call sc_walk
    mov byte [sc_resume], 0
    mov byte [sc_bstop], 0
    cmp byte [sc_bfail], 0
    jne .settle
    cmp byte [sc_didpush], 0
    je .out
    mov bx, si                      ; a push scrolled the whole band, grow box
    call OSAPI_WM_GROW              ; included (SPEC.md 11.1)
    jmp short .out
.settle:
    call sc_reconcile               ; no room left below: show the note
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_brktry - would this keystroke be cheaper as a break? then take it
; in:  SI = window ptr, pass 1 has run ([sc_dr1], [sc_curx]/[sc_cury] valid),
;      the caller has already checked [sc_brkok] and that this is a plain
;      keystroke at the caret; gfx lock held
; out: CF = 0 the break took over AND drew - the caller is done; CF = 1 it
;      did not, and the caller reflows as before. Clobbers AX/BX/CX/DX/DI
; -----------------------------------------------------------------------------
sc_brktry:
    push di
    test word [sc_tx], 7
    jnz .no                         ; rule 4: the scroll is byte-column granular
    cmp byte [sc_hashid], 0
    jne .no                         ; hidden characters break sc_ecol's
                                    ; column arithmetic (SPEC.md 68.1)
    cmp byte [sc_hastab], 0
    jne .no                         ; ...and so does a tab (SPEC.md 68.3)
    cmp byte [sc_hasfmt], 0
    jne .no                         ; formatted rows are not 8px and do not
                                    ; start at [sc_tx]: the break's scroll
                                    ; and column arithmetic both die (65.6)
    cmp byte [sc_pxon], 0
    jne .no                         ; ...and a chosen face has no COLUMN for
                                    ; the arithmetic below to count in
                                    ; (SPEC.md 68.13)
    mov di, [sc_currow]             ; DI = the caret's row (banked by the
                                    ; pass-1 walk that just stood on it)
    mov ax, [sc_dr1]
    sub ax, di
    jbe .no                         ; nothing below the caret's row moved
    mul word [sc_rcols]             ; DX:AX = the cells this reflow would cost
    or dx, dx                       ; below the caret. It cannot overflow -
    jnz .yes                        ; 60 rows by 90 cells is 5,400 - but a
    cmp ax, SC_BRK_CELLS            ; multiply writes DX and saying so is
    jb .no                          ; cheaper than remembering it cannot
.yes:
    mov ax, di
    inc ax
    cmp ax, [sc_vrows]
    jae .no                         ; no row below to push the note into
    mov ax, [sc_curx]
    sub ax, [sc_tx]
    mov cl, 3
    shr ax, cl
    or ax, ax
    jz .no                          ; the caret ended at column 0, which for an
                                    ; insert means the keystroke WRAPPED it
                                    ; onto a fresh row - there is no prefix to
                                    ; keep and no tail to push. The next
                                    ; keystroke will ask again

    mov bx, [sc_ecol]               ; the caret's column BEFORE the edit, which
    xor ah, ah                      ; is exactly the prefix the scrolled copy
    mov al, [sc_eext]               ; below will duplicate - plus whatever the
    add bx, ax                      ; edit took off that row (Delete: one cell)
    cmp bx, [sc_rcols]
    ja .no                          ; a stale sc_ecol cannot reach past the band

    push bx                         ; the caret bar is about to be scrolled
    push dx                         ; down with everything else, and it would
    mov ax, [sc_ecol]               ; land in the middle of the tail. Erase it
    push cx                         ; where it STANDS, which is the column it
    mov cl, 3                       ; was at before this edit and not the one
    shl ax, cl                      ; it is at now - this row is redrawn whole
    pop cx                          ; a moment from now anyway
    add ax, [sc_tx]
    mov bx, [sc_cury]
    mov dx, bx
    add dx, [sc_gh1]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    pop dx
    pop bx

    mov ax, di
    call sc_scroll                  ; the caret's row and everything below it
    jc .no                          ; go down one; the caret's row is redrawn
                                    ; from the note a moment later
    or bx, bx
    jz .nodup
    push bx
    mov ax, di
    inc ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]
    mov bx, ax                      ; BX = y1
    mov dx, ax
    add dx, 7                       ; DX = y2
    pop ax                          ; AX = cells to blank
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_tx]
    dec ax
    mov cx, ax                      ; CX = x2
    mov ax, [sc_tx]                 ; AX = x1
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.nodup:
    mov byte [sc_bmode], 1
    mov [sc_bcrow], di
    mov [sc_borig], di              ; the first row the reconcile owes a repaint
    mov word [sc_prowi], 0xFFFF     ; the caret's row was just scrolled away
    call sc_brkdraw
    mov bx, si                      ; the scroll above dragged the grow box
    call OSAPI_WM_GROW              ; down with everything else
    call sc_hire                    ; nothing settles the break but the worker
    clc
    pop di                          ; POP does not touch flags
    ret
.no:
    stc
    pop di
    ret

; -----------------------------------------------------------------------------
; sc_reconcile - take the break down and show the note
; in:  SI = window ptr, gfx lock held (UI task or the worker)
; out: nothing; clobbers what a callback may
;
; INCREMENTAL, and it can be: the break only ever scrolled rows [sc_borig] and
; below, so everything above it is still the note and still has the signature
; that says so. The band from sc_borig down is filled white and redrawn whole,
; and sc_clean is what keeps that from costing rows x width - with the band
; known blank a row's run stops at its last real character instead of padding
; to the edge to erase with (SPEC.md 27.2).
; -----------------------------------------------------------------------------
sc_reconcile:
    push ax
    push bx
    push cx
    push dx
    call sc_bounds
    mov byte [sc_bmode], 0
    mov byte [sc_resume], 0
    mov word [sc_hity], 0xFFFF      ; pass 1: the signatures, over the whole
    mov word [sc_wanty], 0x7FFF     ; note, because they have been standing
    mov word [sc_dr0], 0xFFFF       ; still since the break went up
    mov word [sc_dr1], 0
    mov byte [sc_draw], 0
    mov byte [sc_sigup], 1
    mov byte [sc_clip], 0
    call sc_walk
    mov ax, [sc_vrows]
    or ax, ax
    jz .done
    dec ax
    mov [sc_dr1], ax
    mov ax, [sc_borig]
    cmp ax, [sc_dr1]
    ja .done
    mov [sc_dr0], ax                ; the band is everything the break moved,
                                    ; whatever the signatures think: no
                                    ; signature describes a fiction
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]
    mov bx, ax                      ; BX = y1
    mov dx, [sc_bot]                ; DX = y2
    mov ax, [sc_tx]
    sub ax, SC_MARGIN               ; AX = x1, the content's own left edge
    mov cx, [sc_rgt]                ; CX = x2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [sc_prowi], 0xFFFF     ; the fill erased whatever the cache knew
    mov byte [sc_clean], 1
    mov byte [sc_draw], 1
    mov byte [sc_sigup], 0
    mov byte [sc_clip], 1
    call sc_walk
    mov byte [sc_clip], 0
    mov byte [sc_clean], 0
    mov bx, si
    call OSAPI_WM_GROW              ; the fill reached it (SPEC.md 11.1/27)
.done:
    call sc_sigmark
    push ax
    mov ax, [sc_top]                ; the reconcile draws the note as it now
    mov [sc_ptop], ax               ; is, in the view it now has
    pop ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_hire - spawn the worker, once
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
;
; Lazy on purpose: a Note Pad that never breaks never costs a task slot or a
; 512-byte stack, which on a 12-slot table is worth the byte of state. A
; refusal is normal and transient (the table can be full), so nothing is
; latched and the next break asks again.
; -----------------------------------------------------------------------------
sc_hire:
    push ax
    push bx
    cmp byte [sc_hired], 0
    jne .out
    mov ax, sc_worker
    mov bx, [sc_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [sc_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_worker - THE background task (SPEC.md 20.6): it settles the break
; in:  DX = our instance index, DS = ES = CS = our segment, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own - the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; It exists for one reason: a break that is never taken down is not a
; temporary state, it is what the user believes the note says. Two things
; take it down - half a second of not typing, and the window ceasing to be
; frontmost. The second needs OSAPI_WM_TOP because a package is told when it
; GAINS the front and never when it loses it.
;
; A covered window is skipped rather than drawn: the clip region cuts a fill
; per pixel and a run per cell (SPEC.md 11.3), and this reconcile is a fill
; followed by runs. Skipping costs nothing - an uncover repaints through
; W_PAINT, and sc_paint draws the note and clears the break.
; -----------------------------------------------------------------------------
sc_worker:
.loop:
    mov bx, [sc_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4)
    mov ax, SC_WTICKS
    call OSAPI_TASK_SLEEP
    cmp byte [sc_quit], 0           ; File > Close / Exit (SPEC.md 68.2): the
    jne .quit                       ; UI hid the window and left the teardown
                                    ; to us, because only a task can end an
                                    ; instance cleanly - see .quit below
    cmp byte [sc_bmode], 0
    jne .idle
    cmp byte [sc_hdirty], 0         ; ...or a height to recount, which is the
    jne .idle                       ; other thing worth waking up for
    cmp byte [sc_uopen], 0          ; ...or an edit group whose half-second is
    jne .idle                       ; nearly up (SPEC.md 27.9)
    call sc_stkval                  ; ...or a CAPS/NUM lamp that no longer
    cmp al, [sc_stkf]               ; matches the lock state - CapsLock alone
    je .loop                        ; emits no key EVENT, so without this poll
                                    ; the lamp waits for the next keystroke
                                    ; (the flag byte is a plain memory read,
                                    ; legal without the lock)
.idle:
    call OSAPI_WM_TOP               ; BX = frontmost visible, 0 = none
    cmp bx, [sc_win]
    jne .go
    call OSAPI_GET_TICKS
    sub ax, [sc_ktick]              ; modular, so a wrapping tick counter is
    cmp ax, SC_IDLE                 ; not a special case (SPEC.md 8)
    jb .loop
.go:
    call OSAPI_GFX_LOCK
    mov byte [sc_inwk], 1           ; sc_itinit must not claim on this task
                                    ; (SPEC.md 20.6 rule 7) - raised for the
                                    ; draw burst, cleared before the unlock
    cmp byte [sc_mopen], SC_M_NONE  ; a dropdown, the About box or a dialog is
    jne .unlock                     ; over the content (SPEC.md 68.2): every
    cmp byte [sc_about], 0          ; draw below would letter text straight
    jne .unlock                     ; through it. The debts stay raised and
    cmp word [sc_dlg], 0            ; are paid on the pass after it closes
    jne .unlock
    cmp byte [sc_sowed], 0          ; a scroll whose repaint was dropped because
    je .nosowed                     ; another click was right behind it
    mov byte [sc_sowed], 0          ; (SPEC.md 27.7.8). Cleared FIRST: a repaint
                                    ; that faults must not leave the debt to be
                                    ; paid again forever
    mov bx, [sc_win]                ; ...and ASKED, like the three draws below
    call OSAPI_WM_OBSCURED          ; it. This one drew unconditionally: covered,
    jc .nosowed                     ; it painted over the window on top of it
                                    ; (SPEC.md 11.3), and it was the one path
    mov si, [sc_win]                ; that changed this window's pixels without
    call sc_redraw                  ; telling the kernel - which is what the
                                    ; raise cache's promise rests on (11.96)
.nosowed:
    call sc_uclose                  ; half a second without an edit is what a
                                    ; user means by ONE edit, and this is the
                                    ; clock that measures it (SPEC.md 27.9).
                                    ; UNDER the lock, because every recorder
                                    ; runs inside a callback that holds it
    mov si, [sc_win]
    cmp byte [sc_bmode], 0          ; re-read UNDER the lock: the UI task may
    je .height                      ; have settled it while we waited
    mov bx, [sc_win]
    call OSAPI_WM_OBSCURED
    jc .height
    call sc_reconcile
.height:
    ; Count the note's rows, which no other walk does any more (SPEC.md 27.7),
    ; and move the thumb if that changed it. THE COUNT is not gated on the
    ; window being visible - it is arithmetic, and a covered or hidden window's
    ; height is owed the moment it comes back - and THE DRAW is gated by the
    ; call below it, like the other three in this routine.
    ;
    ; That distinction was written here as one sentence and the drawing half
    ; was wrong (SPEC.md 11.3.1): "sc_sbcheck draws only when a number moved"
    ; was offered as the reason it was safe, and a number moving is exactly
    ; what a chunked count does on a window nobody can see - SC_HCHUNK rows a
    ; pass, each raising [sc_drows]. wm_obscured answered only "is anything on
    ; TOP of me", so after the close box hid this window the bar was drawn onto
    ; the bare desktop. It answers about a hidden window now, which is what the
    ; four calls here have always meant by it.
    cmp byte [sc_hdirty], 0
    je .unlock
    call sc_bounds                  ; the walk reads [sc_ty]/[sc_rgt], and the
    call sc_hchunk                  ; window may have been resized since.
                                    ; A CHUNK of the count and not the whole of
                                    ; it (SPEC.md 27.7.3): the lock is held
                                    ; across this, so the bound on the walk is
                                    ; the bound on how long a UI action behind
                                    ; it has to wait
    mov bx, [sc_win]
    call OSAPI_WM_OBSCURED
    jc .unlock
    call sc_sbcheck
.unlock:
    mov byte [sc_inwk], 0
    call sc_stkchk                  ; the CAPS/NUM lamps (SPEC.md 68.2): the
                                    ; lock flags byte can change with no key
                                    ; EVENT arriving (CapsLock alone emits
                                    ; none), so the worker is the only thing
                                    ; that can notice. Two glyph runs, only
                                    ; on a change, gated on wm_obscured inside
    call OSAPI_GFX_UNLOCK
    jmp .loop

.quit:
    ; File > Close / Exit (SPEC.md 68.2). There is no self-close API slot: the
    ; instance normally dies through the close box (app_close_win) or through
    ; OSAPI_TASK_ALIVE noticing. So the UI half hid the window for instant
    ; feedback and set [sc_quit]; here the window record is destroyed and the
    ; very next ALIVE finds it unowned and tears the instance down through the
    ; kernel's own path - task, region, claims and record all freed inside
    ; that call. The destroy and the ALIVE are back to back on THIS task, so
    ; nothing can reuse the record slot in between.
    call OSAPI_GFX_LOCK
    mov bx, [sc_win]
    call OSAPI_WM_DESTROY
    call OSAPI_GFX_UNLOCK
    mov bx, [sc_win]
    call OSAPI_TASK_ALIVE           ; never returns: the record is gone
    jmp .quit                       ; unreachable belt-and-braces

; -----------------------------------------------------------------------------
; sc_nextrow - the pen moved to the next row: bank the signature it just
;              finished, and start the next one
; in:  [sc_row], [sc_rowh], [sc_sigup]
; out: [sc_row] advanced, [sc_rowh] = 0; [sc_dr0]/[sc_dr1] widened if the row
;      changed; preserves all registers
;
; Rows past [sc_vrows] are off the bottom of the content. The walk still
; visits them - every position below has to stay true - but they have no
; signature slot and no pixels, so they are counted and otherwise ignored.
; -----------------------------------------------------------------------------
sc_nextrow:
    push ax
    push bx
    cmp byte [sc_sigup], 0
    je .adv
    mov ax, [sc_row]
    cmp ax, [sc_vrows]
    jae .adv
    shl ax, 1
    mov bx, ax
    mov ax, [sc_rowh]
    cmp ax, [bx+sc_sig]
    je .adv                         ; same word, same pixels: leave it alone
    mov [bx+sc_sig], ax
    mov ax, [sc_row]
    cmp ax, [sc_dr0]
    jae .hi
    mov [sc_dr0], ax
.hi:
    cmp ax, [sc_dr1]
    jbe .adv
    mov [sc_dr1], ax
.adv:
    inc word [sc_row]
    mov word [sc_rowh], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rowdirty - is the row the pen is on one this pass is redrawing?
; in:  [sc_row], [sc_clip], [sc_dr0]/[sc_dr1]
; out: CF = 1 if it must NOT be drawn; preserves all registers
; -----------------------------------------------------------------------------
sc_rowdirty:
    cmp byte [sc_clip], 0
    je .yes                         ; not clipping: this is a full paint
    push ax
    mov ax, [sc_row]
    cmp ax, [sc_dr0]
    jb .no
    cmp ax, [sc_dr1]
    ja .no
    pop ax
.yes:
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_sigmark - record the geometry (and the toast) the signatures describe
; in:  sc_bounds already run
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_sigmark:
    push ax
    call sc_selmark
    mov ax, [sc_tx]
    mov [sc_stx], ax
    mov ax, [sc_ty]
    mov [sc_sty], ax
    mov ax, [sc_rgt]
    mov [sc_srgt], ax
    mov ax, [sc_bot]
    mov [sc_sbot], ax
    mov ax, [sc_ctop]               ; ...and the chrome top the band was drawn
    mov [sc_sctop], ax              ; under, for sc_panmove: a View toggle
                                    ; moves the band's own top edge, and the
                                    ; blit must span from the SMALLER of the
                                    ; two tops or the moving rows fall outside
                                    ; its rect (SPEC.md 68.2)
    mov byte [sc_sigok], 1
    mov byte [sc_gchg], 0           ; this paint laid the note out under the
    pop ax                          ; geometry just recorded, so the view has
    ret                             ; been re-clamped against it

; -----------------------------------------------------------------------------
; sc_selmark - the screen now shows THIS selection (SPEC.md 27.8.2)
; out: nothing; preserves all registers
;
; The one fact sc_selqo reads. Every path that finishes a redraw sets it,
; including the ones that drew nothing: a row whose selection did not change
; is not in the dirty band, so "what the screen shows" is the live selection
; either way.
; -----------------------------------------------------------------------------
sc_selmark:
    push ax
    mov ax, [sc_sel0]
    mov [sc_osel0], ax
    mov ax, [sc_sel1]
    mov [sc_osel1], ax
    mov al, [sc_selon]
    mov [sc_oselon], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_sigsame - do the stored signatures still describe this window?
; in:  sc_bounds already run
; out: CF = 1 if they do not and the caller must repaint whole; preserves all
;
; All four tests are the layout: a resized window wraps differently, and the
; kernel white-filled its content on the way here anyway. There used to be a
; fifth and a sixth, on [sc_msg] and its generation - the toast was drawn OVER
; the text and was in no row's signature, so the keystroke that retired one
; had to erase it the only way this module could, by painting the whole
; content again. The toast is the kernel's now and is in the menu bar
; (SPEC.md 59), so it is in nothing this routine describes.
; -----------------------------------------------------------------------------
sc_sigsame:
    push ax
    cmp byte [sc_sigok], 0
    je .no
    mov ax, [sc_tx]
    cmp ax, [sc_stx]
    jne .no
    mov ax, [sc_ty]
    cmp ax, [sc_sty]
    jne .no
    mov ax, [sc_rgt]
    cmp ax, [sc_srgt]
    jne .no
    mov ax, [sc_bot]
    cmp ax, [sc_sbot]
    jne .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_height - walk the whole note for [sc_drows], if the note has changed
; in:  SI = window ptr, sc_bounds run; out: nothing; preserves all registers
;
; The one walk that exists to answer "how many rows", which is the one
; question a bounded walk cannot answer (SPEC.md 27.7). Every OTHER walk now
; stops at the bottom of the view, because rows below it are drawn by nobody
; and the thumb is the only thing that was ever asking - so this is where the
; note's tail is paid for, and it is paid half a second after the typing
; stops rather than on every keystroke.
;
; It preserves the two query fields because sc_onclick sets them BEFORE it
; gets here, and a walk consumes them.
;
; It comes in two sizes (SPEC.md 27.7.3). sc_height finishes the count in one
; hold, for the one caller that needs the answer exact - a click on the bar.
; sc_hchunk does SC_HCHUNK rows of it and hands the lock back, which is what
; the worker calls: the count of a 16KB note is seconds of walking, and doing
; it in one hold freezes the machine behind it with nothing on the disk and
; nothing on the glass.
;
; Chunking is legal because the gfx lock here is a mutex over the walk's
; SCRATCH and not a drawing lock - sc_height writes no framebuffer, and nine of
; sc_walk's ten call sites are UI callbacks that hold the lock already. So the
; hold is needed for a CHUNK and never for the COUNT, and "give up the machine
; if somebody else wants it" falls out of the release rather than needing the
; worker to ask anyone.
;
; The resume pair survives the release because WRAPPING IS DETERMINISTIC: row R
; begins at index I whoever computed it. An interleaved W_PAINT scribbles over
; the walk's in-flight scratch and cannot touch those two words. Only the note
; CHANGING invalidates them, and that goes through sc_hmark, which resets them.
; -----------------------------------------------------------------------------
sc_height:
    push ax
    mov ax, 0x7FFF                  ; no bound: to the last character, however
    call sc_hwalk                   ; many rows that is
    pop ax
    ret

sc_hchunk:
    push ax
    mov ax, [sc_hi]                 ; a stale seed can only mean the note shrank
    cmp ax, [sc_len]                ; without going through sc_hmark; sc_walk
    jbe .seedok                     ; would fall back to a full walk, and the
    mov word [sc_hrow], 0           ; bound below would then be a chunk's worth
    mov word [sc_hi], 0             ; of rows past where it actually starts -
.seedok:                            ; one unbounded hold, the thing being fixed
    mov ax, [sc_hrow]               ; [sc_lastrow] is a VISIBLE row and
    sub ax, [sc_top]                ; [sc_hrow] is absolute, so the bound is
    add ax, SC_HCHUNK               ; this chunk's own start plus its length
    call sc_hwalk
    pop ax
    ret

; sc_hwalk - the count itself, stopping after visible row AX
; in:  AX = the last visible row this pass may stand on, SI = window ptr
sc_hwalk:
    cmp byte [sc_hdirty], 0
    jne .go
    ret
.go:
    push ax
    push si
    mov [sc_lastrow], ax
    mov ax, [sc_hity]
    push ax
    mov ax, [sc_wanty]
    push ax
    mov word [sc_hity], 0xFFFF
    mov word [sc_wanty], 0x7FFF
    mov byte [sc_draw], 0
    mov byte [sc_sigup], 0
    mov byte [sc_clip], 0

    mov ax, [sc_hi]                 ; resume where the last chunk stopped, which
    mov [sc_sdi], ax                ; for the first one is index 0 of row 0 -
    mov ax, [sc_hrow]               ; identical to the unseeded start sc_walk
    sub ax, [sc_top]                ; would make, so the top of the note needs
    mov [sc_sdr], ax                ; no case of its own. sc_sdr is a VISIBLE
    mov byte [sc_resume], 1         ; row, so it is derived from [sc_top] HERE
    call sc_walk                    ; and not banked - the view may have moved
    mov byte [sc_resume], 0         ; between one chunk and the next

    cmp byte [sc_hdirty], 0         ; .done clears it and .stop leaves it up, so
    jne .more                       ; the flag already says which exit was taken
    mov word [sc_hrow], 0           ; finished: the next count starts at the top
    mov word [sc_hi], 0
    jmp short .fin
.more:
    mov ax, [sc_stoprow]            ; stopped: pick this up next pass
    mov [sc_hrow], ax
    mov ax, [sc_stopi]
    mov [sc_hi], ax
.fin:
    pop ax
    mov [sc_wanty], ax
    pop ax
    mov [sc_hity], ax
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_hmark - the note changed: the height is owed, and owed from the TOP
; preserves every register AND the flags (it only stores to memory), so it is a
; drop-in for the `mov byte [sc_hdirty], 1` it replaces at each of its callers
;
; The reset is the whole reason this is a routine. A chunked count that is
; part-way down a note holds a (row, index) pair that describes the note it
; started on; an edit makes that pair name a row that may no longer begin
; there, so the count has to start over. Raising the flag and forgetting the
; pair are the same event and must not be separable.
; -----------------------------------------------------------------------------
sc_hmark:
    mov byte [sc_hdirty], 1
    mov word [sc_hrow], 0
    mov word [sc_hi], 0
    jmp sc_xdrop                    ; ...and the row index with them (SPEC.md
                                    ; 27.13): it is the same event - a table of
                                    ; where rows BEGIN means nothing under a
                                    ; layout where they begin somewhere else.
                                    ; A tail call, because sc_xdrop preserves
                                    ; the flags too and this routine's whole
                                    ; contract is that it is a drop-in for a
                                    ; `mov byte [sc_hdirty], 1`

; -----------------------------------------------------------------------------
; sc_measure - run the walk without drawing
; in:  SI = window ptr; the query fields already set
; out: as sc_walk; preserves all registers
;
; A QUERY pass: it answers where the caret is, or what a click landed on, and
; it must not touch the signatures - the caller has not drawn anything.
;
; It is also where the visual break comes down (SPEC.md 27.3), and that is the
; right place for it rather than a call in each of the four handlers: this is
; exactly the call that means "I need to know where things really are", and
; the answer would be a lie against what the user is looking at. A click has
; to land on the character under the pointer, so the note has to be showing
; the note before the pointer is resolved.
; -----------------------------------------------------------------------------
sc_measure:
    call sc_settle
    call sc_bounds
    mov byte [sc_draw], 0
    mov byte [sc_sigup], 0
    mov byte [sc_clip], 0
    call sc_walk
    ret

; -----------------------------------------------------------------------------
; sc_settle - if the visual break is up, take it down
; in:  SI = window ptr, gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_settle:
    cmp byte [sc_bmode], 0
    je .out
    jmp sc_reconcile                ; a tail call: it preserves what we do
.out:
    ret

; -----------------------------------------------------------------------------
; sc_seedck - seed the next walk at the caret's row (SPEC.md 27.4)
; sc_seedrow - seed it at row AX instead, and stop after row DX (SPEC.md 27.5)
; out: [sc_resume] set if the seed is usable, left 0 if the walk must start at
;      index 0 after all; preserves all registers
;
; sc_rows only describes rows the last completed walk reached, so a query about
; a row past [sc_rowsn] - a click on the blank space below the text - has to
; fall back. That fallback is the ONLY thing keeping a stale table from
; answering with a plausible wrong index.
; -----------------------------------------------------------------------------
; sc_seedck seeds one row EARLIER than the caret's, and walks back further
; still through a long word. SPEC.md 27.4's licence to resume at the caret's
; own row was "wrapping is an automaton with no lookahead", and SPEC.md 27.11
; took that away: the break in FRONT of a row is decided by the length of the
; word BEHIND it, so an edit inside the caret's row can move the break that
; put the row where it is. Redoing the row above is what re-decides it.
;
; The walk back covers the other half. A row that begins mid-word was split by
; the cell rule, and the word-fit test that let it get that far was taken at
; the word's first character, which may be several rows up; only from there is
; the layout genuinely independent of the edit. It is bounded by the note and
; runs one iteration in every ordinary case, because an ordinary row begins
; after a space.
;
; Failing back to a full walk when the table runs out is not a fallback but
; the correct answer: [sc_rows] describes visible rows only, so row 0 of a
; SCROLLED view was placed by a break above the view, and nothing here can
; redo it.
sc_seedck:
    push ax
    push bx
    push cx
    push es
    mov byte [sc_resume], 0
    cmp byte [sc_ckok], 0
    je .out
    mov es, [sc_dseg]
    mov ax, [sc_ckpr]               ; the caret's row...
    call sc_rowstart                ; ...and the index it begins at. Asked
    jc .back                        ; TWICE on the slow path, which is a shift
                                    ; and a load: sc_rowstart preserves AX
    or bx, bx
    jz .seed
    mov cl, [es:bx-1]
    cmp cl, 13
    je .seed                        ; A HARD NEWLINE IS NOT A WRAP DECISION.
                                    ; The row above ended because the note said
                                    ; so, and no word can move that break - so
                                    ; this row's start is fixed and there is
                                    ; nothing above it to lay out again
    cmp cl, ' '
    je .ckw
    cmp cl, 9                       ; a tab is a break opportunity like a
    jne .back                       ; space (SPEC.md 68.3)
.ckw:
    call sc_ckword                  ; ...and a wrapped row is safe too as long
    jnc .seed                       ; as the edit is past its first word
.back:
    call sc_rowstart                ; ...and the index it begins at
    jc .out
    or bx, bx
    jz .seed                        ; index 0 begins the NOTE: there is no
                                    ; earlier break to be redecided
    mov cl, [es:bx-1]
    cmp cl, ' '
    je .prev
    cmp cl, 13
    je .prev
    cmp cl, 9
    je .prev
    call sc_cellrun                 ; mid-word: this row was split by the cell
    jnc .seed                       ; rule, and the word began further back -
                                    ; UNLESS the word is longer than a row, in
                                    ; which case sc_wordfit never decided
                                    ; anything about it and there is nothing
                                    ; further back to redo (SPEC.md 27.4.2)
    or ax, ax
    jz .out
    dec ax
    jmp short .back
.prev:
    or ax, ax                       ; a word start, so the break in front of it
    jz .out                         ; was taken while the row ABOVE was laid
    dec ax                          ; out - so redo that one too
    call sc_rowstart
    jc .out
.seed:
    mov [sc_sdi], bx
    mov [sc_sdr], ax
    mov byte [sc_resume], 1
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ckword - may this row be seeded WITHOUT laying out the one above it?
; in:  BX = the row's start index, ES = the document segment
; out: CF = 0 yes, seed here; CF = 1 back up as before.
;      Preserves every register.
;
; SPEC.md 27.11.1. sc_wordfit is the whole of why sc_seedck backs up: standing
; at the first character of a word it measures whether that word ENDS inside
; the cells left on the row, and breaks in front of it if it does not. So the
; break that decides where this row starts is a function of the length of this
; row's FIRST WORD and of nothing else in this row - which means an edit past
; the end of that word cannot have moved it.
;
; The caret is the edit, near enough and always on the safe side: an insert
; leaves it one PAST the character it added, a backspace and a Delete leave it
; ON the edit, so the earliest index this keystroke can have touched is
; [sc_cur] - 1. THE BOUND IS THAT INDEX, NOT THE CARET, and the difference is
; the whole of the case it has to catch: the character an insert added may
; ITSELF be the space that now ends the word, and then the word was LONGER when
; the break in front of it was taken - which is precisely the decision the
; back-up exists to redo. Accepting a terminator AT [sc_cur] - 1 seeded the row
; at a start the insert had just moved. Backspace and Delete touch nothing
; below [sc_cur] and pay one extra row for the shared bound, which is the
; conservative direction. It needs no extra state kept in step.
;
; The scan is at most a row's width and stops at the caret, so it is a handful
; of byte compares against a row of layout at ~6 ms.
; -----------------------------------------------------------------------------
sc_ckword:
    push ax
    push bx
    push cx
    mov cx, [sc_cur]
    jcxz .no                        ; a caret at 0 would decrement to 0xFFFF
    dec cx                          ; and accept every terminator on the row
    mov al, [es:bx]
    cmp al, ' '                     ; a row that begins on a space or a newline
    je .no                          ; is not a word start, and the reasoning
    cmp al, 13                      ; above does not describe it
    je .no
    cmp al, 9
    je .no
.scan:
    cmp bx, cx
    jae .no                         ; the edit arrived first: it is inside the
                                    ; first word, or IS the terminator that now
                                    ; ends it - the case the back-up exists for
    mov al, [es:bx]
    cmp al, ' '
    je .yes
    cmp al, 13
    je .yes
    cmp al, 9
    je .yes
    inc bx
    jmp short .scan
.yes:
    clc                             ; the word ends before the caret, so its
    jmp short .out                  ; length is what it was
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_cellrun - is the break in front of this row owned by the CELL RULE alone?
; in:  BX = the row's start index, known mid-word ([es:bx-1] is neither a space
;      nor a CR), ES = the document segment
; out: CF = 0 seed here, the walk need go no further back; CF = 1 back up as
;      before. Preserves every register.
;
; SPEC.md 27.4.2, and it is sc_ckword's other half: that one asks whether the
; edit is past the row's first word, this one asks whether there is a word-fit
; decision in front of this row AT ALL.
;
; sc_wordfit's .p2l answers "longer than a row: the cell rule owns it" and
; breaks NOTHING - a word that cannot fit a whole row is laid out left to
; right and wrapped at the margin like the pre-27.11 automaton. So inside such
; a word every row start is the one before it plus sc_rcols, anchored at the
; word's first character, and the position of THAT was settled by text the
; edit cannot reach. Backing up through it re-decides a break nobody ever
; decided: on a note that is one 249-character run, the caret's row backed up
; NINE rows to index 0 and pass 1 then laid out the whole view from the top of
; the note, on every keystroke (docs/NOTEPAD-NOTES.md 5.6).
;
; THE MARGIN IS WHY THIS COUNTS TO sc_rcols + 2 AND NOT sc_rcols + 1. The
; threshold sc_wordfit actually applies is `length > sc_rcols`, and this runs
; AFTER the edit has been applied to the buffer - so a backspace at the caret
; may already have taken one character out of the run being measured. Two
; spare characters is what makes the answer the same before and after: the run
; measured here is at least sc_rcols + 2, so the word was at least sc_rcols + 1
; before the keystroke and is at least sc_rcols + 1 after it, and both are past
; the threshold. One spare would let a word of exactly sc_rcols + 1 fall back
; to sc_rcols under a backspace, where sc_wordfit DOES have an opinion and the
; break in front of the word can move.
;
; The scan is at most a row's width of byte compares against a row of layout at
; ~6 ms, which is sc_ckword's bargain and the same one.
; -----------------------------------------------------------------------------
sc_cellrun:
    push ax
    push bx
    push cx
    mov cx, [sc_rcols]
    inc cx
    inc cx
.scan:
    or bx, bx
    jz .no                          ; the note begins inside the run, so it is
                                    ; shorter than the margin needs - and the
                                    ; caller's own `or bx, bx` seeds at row 0
                                    ; on the next iteration anyway
    dec bx
    mov al, [es:bx]
    cmp al, ' '
    je .no
    cmp al, 13
    je .no
    cmp al, 9
    je .no
    dec cx
    jnz .scan
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_rowstart - the index visible row AX begins at (SPEC.md 27.5)
; in:  AX = the row
; out: CF = 0 and BX = the index; CF = 1 = sc_rows does not describe that row
; clobbers: BX and CF; every other register preserved
; -----------------------------------------------------------------------------
sc_rowstart:
    push ax
    cmp byte [sc_rowsok], 0
    je .no
    cmp ax, [sc_rowsn]
    jae .no                         ; unsigned, so a row ABOVE the view - a
                                    ; negative index - fails here too
    shl ax, 1
    mov bx, ax
    mov bx, [bx+sc_rows]
    cmp bx, [sc_len]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_netseed - seed the caret-follow safety net FORWARD (SPEC.md 27.7.7)
; out: [sc_resume] set if a row start at or before the caret was found
; preserves all registers
;
; The net exists because a bounded walk can stop short of the caret, and it
; used to answer that by walking the whole note from index 0. Its comment
; justified that with "the seed is what let the walk miss the caret" - which
; is true of a seed AFTER the caret and false of one before it. The case that
; fires this constantly is Down on the bottom visible row: the caret lands one
; row below the view, and finding it cost a walk of the entire note on the
; most-used key in the editor.
;
; So resume at the deepest row [sc_rows] describes whose start index is at or
; before [sc_cur], and walk forward from there. Everything before that row laid
; out identically - the edit, if there was one, is AT the caret and so at or
; after the seed, which is SPEC.md 27.4's argument unchanged, and 27.11's
; lookahead cannot reach back past it either. A caret ABOVE the table walks
; back to row 0, finds nothing that qualifies and leaves [sc_resume] clear,
; which is the old behaviour and still the right answer.
; -----------------------------------------------------------------------------
sc_netseed:
    push ax
    push bx
    mov byte [sc_resume], 0
    cmp byte [sc_rowsok], 0
    je .out
    mov ax, [sc_rowsn]
    or ax, ax
    jz .out
    dec ax                          ; the deepest row the table describes
.try:
    call sc_rowstart                ; BX = where it begins, CF=1 = it does not
    jc .out
    cmp bx, [sc_cur]
    jbe .seed                       ; at or before the caret: safe to resume
    or ax, ax
    jz .out
    dec ax                          ; ...past it, so try the row above
    jmp short .try
.seed:
    mov [sc_sdi], bx
    mov [sc_sdr], ax
    mov byte [sc_resume], 1
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE ROW INDEX (SPEC.md 27.13) - random access to a row without walking to it
;
; sc_rows describes the VIEW and nothing else, so every question about a row
; outside it fell back to laying the note out from index 0. That is Up out of
; the top of the view, and it measured 5.2 s a press.
;
; This is a sparse table of the character index at which every Kth ABSOLUTE
; row begins: entry n describes row n << [sc_xksh]. It costs no walking at
; all, because SPEC.md 27.7.3's background count already visits every row in
; order and already computes exactly this - sc_xnote just keeps what was
; being thrown away.
;
; BOUNDED BY DECIMATION, not by growing. A note of nothing but newlines is one
; row per character, so at SC_MAXKB that is 16,384 rows - too many to reserve
; and awkward to claim. When the table fills, sc_xhalve keeps every second
; entry and doubles the stride: it then always spans the whole note, always
; costs SC_XN entries, and the walk from the nearest checkpoint is at most K
; rows, which grows only logarithmically in the note's length.
;
; The stride is a POWER OF TWO and is kept as its log, because every lookup
; would otherwise be a `div` - 150 clocks against a shift's 10, once per row
; of every walk in the module.
; =============================================================================
; ENTRIES ARE THE WHOLE COST MODEL, and the first version got this wrong by
; being frugal with them. A lookup lands on the checkpoint at or BEFORE the row
; wanted, so the walk that follows is up to K rows - and one keystroke runs
; FOUR walks (sc_vmove's, sc_move's, the redraw's pass 1 and sc_scrollpaint's),
; each paying that K. At 64 entries a 781-row note halves down to K = 16 and an
; Up cost 68 rows of walking, which measured 644 ms and is where 27.13's first
; cut stopped. 256 entries hold README at K = 4 with no halving at all.
;
; 512 bytes to do it, which is the right trade here twice over: a package's bss
; ships inside its image (SPEC.md 51/20.2) and Note Pad's document is a heap
; claim of its own (27.6), so this is half a kilobyte against a 16KB note and
; nothing against KERN_BUDGET, which a package does not touch.
SC_XN     equ 256               ; entries: 512 bytes, and 256 x 1 row means a
                                ; note under 256 rows is answered EXACTLY
SC_XKSH0  equ 0                 ; ...so the stride starts at 1 and doubles only
                                ; when the note proves it has to

; -----------------------------------------------------------------------------
; sc_xnote - offer the row just started to the index
; in:  [sc_row] = the visible row, [sc_i] = where it begins, sc_bounds run
; out: nothing; preserves all registers and the flags
;
; Called from sc_rstart, so it runs once per row of EVERY walk and its cost in
; the ordinary case has to be nothing: one compare against the row the table
; wants next.
;
; ONLY THE NEXT ENTRY OWED IS TAKEN, and that is what keeps the table honest.
; A seeded walk skips the rows above its seed, so a table that recorded
; whatever it happened to pass would have holes - and a lookup landing in one
; answers for a row it never saw, which is docs/FIELD-NOTES.md 4's shape (an
; index resolved against a snapshot that had shifted). Contiguous or nothing.
; -----------------------------------------------------------------------------
sc_xnote:
    pushf
    push ax
    mov ax, [sc_row]
    add ax, [sc_top]                ; ABSOLUTE: the table outlives the view,
    cmp ax, [sc_xnext]              ; and [sc_top] moves under it
    jne .out
    cmp word [sc_xn], SC_XN
    jae .out                        ; full and not yet halved: cannot happen,
                                    ; .store halves on the way out, but a
                                    ; table that could not be halved must not
                                    ; be written past its end
    push bx
    mov bx, [sc_xn]
    shl bx, 1
    mov ax, [sc_i]
    mov [bx+sc_xi], ax
    inc word [sc_xn]
    mov ax, [sc_xnext]              ; ...and where the next one goes
    push cx
    mov cl, [sc_xksh]
    mov bx, 1
    shl bx, cl
    pop cx
    add ax, bx
    mov [sc_xnext], ax
    pop bx
    cmp word [sc_xn], SC_XN
    jb .out
    call sc_xhalve
.out:
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; sc_xhalve - keep every second entry and double the stride
; out: nothing; preserves all registers
;
; [sc_xnext] is deliberately NOT recomputed: it is xn * K, and halving xn while
; doubling K leaves that product exactly where it was. The next row the table
; wants is the next row it wanted.
; -----------------------------------------------------------------------------
sc_xhalve:
    push ax
    push cx
    push si
    push di
    xor si, si
    xor di, di
    mov cx, SC_XN / 2
.lp:
    mov ax, [si+sc_xi]
    mov [di+sc_xi], ax
    add si, 4                       ; every SECOND entry...
    add di, 2                       ; ...into consecutive slots
    loop .lp
    inc byte [sc_xksh]              ; ...at twice the stride
    mov word [sc_xn], SC_XN / 2
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_xdrop - the layout moved: every entry describes a note that is not this one
; out: nothing; preserves all registers and the flags
;
; Called by sc_hmark, which is already "the height is owed and owed from the
; top" - the same event, because both are invalidated by exactly the thing
; that changes where a row begins. Keeping them one call is what stops a
; future edit raising one and forgetting the other.
; -----------------------------------------------------------------------------
sc_xdrop:
    mov word [sc_xn], 0
    mov word [sc_xnext], 0
    mov byte [sc_xksh], SC_XKSH0
    ret

; -----------------------------------------------------------------------------
; sc_xrow - the checkpoint at or before ABSOLUTE row AX
; out: CF=1 none; else CF=0, AX = the checkpoint's absolute row, BX = the index
;      it begins at, CX = 1 if a LATER checkpoint exists (so the row wanted is
;      known to be within one stride) and 0 if this is the deepest
; -----------------------------------------------------------------------------
sc_xrow:
    push dx
    mov cx, [sc_xn]
    jcxz .no
    or ax, ax
    js .no                          ; above row 0: nothing describes it
    push cx
    mov cl, [sc_xksh]
    shr ax, cl                      ; which entry - a shift, not a divide
    pop cx
    cmp ax, cx
    jb .have
    mov ax, cx                      ; past the high-water mark: the deepest
    dec ax                          ; one we actually have
.have:
    mov dx, ax
    inc dx
    cmp dx, cx                      ; is there one BELOW it as well?
    mov cx, 1
    jb .later
    xor cx, cx
.later:
    mov bx, ax
    shl bx, 1
    mov bx, [bx+sc_xi]
    cmp bx, [sc_len]
    ja .nopop                       ; a table that outlived its note
    push cx
    mov cl, [sc_xksh]
    shl ax, cl                      ; the entry's absolute row
    pop cx
    pop dx
    clc
    ret
.nopop:
    pop dx
    stc
    ret
.no:
    pop dx
    stc
    ret

; -----------------------------------------------------------------------------
; sc_xseed - seed the next walk at the checkpoint at or before ABSOLUTE row AX
; in:  AX = the absolute row wanted, DX = the VISIBLE row to stop after
; out: CF=0 seeded; CF=1 the table cannot answer and the caller is unchanged.
;      Preserves all registers.
; -----------------------------------------------------------------------------
sc_xseed:
    push ax
    push bx
    push cx
    call sc_xrow
    jc .no
    sub ax, [sc_top]                ; sc_sdr is a VISIBLE row (sc_hwalk's rule)
    mov [sc_sdr], ax
    mov [sc_sdi], bx
    mov [sc_lastrow], dx
    mov byte [sc_resume], 1
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_xseedi - seed at the checkpoint at or before the character index AX, and
;             BOUND the walk when the table can prove where that row ends
; in:  AX = a character index (in practice [sc_cur])
; out: CF=0 seeded, and [sc_lastrow] set when a later checkpoint exists;
;      CF=1 not seeded and [sc_lastrow] untouched. Preserves all registers.
;
; The inverse lookup, and the one that matters most: the caret-follow net has
; the caret's INDEX and wants its ROW, which is the question that used to cost
; a walk of the whole note. sc_xi rises with the row, so a binary search finds
; it in six compares.
;
; THE BOUND IS THE HALF WORTH HAVING. If checkpoint n is the last one at or
; before the caret and checkpoint n+1 exists, then the caret's row is below
; n*K and above (n+1)*K, so the walk may stop at (n+1)*K and SPEC.md 27.7.7's
; "the walk is still unbounded, and has to be" stops being true. At the
; deepest checkpoint there is no n+1 and it is unbounded again, which is the
; old behaviour and still correct.
; -----------------------------------------------------------------------------
sc_xseedi:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                      ; the index wanted
    mov cx, [sc_xn]
    jcxz .no
    xor bx, bx                      ; lo
    mov dx, cx
    dec dx                          ; hi
.bs:
    cmp bx, dx
    jae .found
    mov ax, bx
    add ax, dx
    inc ax
    shr ax, 1                       ; mid, rounded up
    mov di, ax
    shl di, 1
    mov di, [di+sc_xi]
    cmp di, si
    ja .lower
    mov bx, ax                      ; sc_xi[mid] <= index: mid is a candidate
    jmp short .bs
.lower:
    mov dx, ax
    dec dx
    jmp short .bs
.found:
    mov ax, bx                      ; BX = the entry
    mov di, ax
    shl di, 1
    mov di, [di+sc_xi]
    cmp di, si
    ja .no                          ; even entry 0 is past it: impossible while
                                    ; entry 0 is index 0, and cheap to refuse
    cmp di, [sc_len]
    ja .no
    push cx
    mov cl, [sc_xksh]
    mov dx, ax
    shl dx, cl                      ; DX = the checkpoint's absolute row
    pop cx
    inc ax
    cmp ax, cx                      ; a checkpoint BELOW it as well?
    mov ax, 0                       ; (0 = no, so no bound)
    jae .nolim
    mov ax, 1
.nolim:
    push ax
    mov ax, dx
    sub ax, [sc_top]                ; visible
    mov [sc_sdr], ax
    mov [sc_sdi], di
    mov byte [sc_resume], 1
    pop ax
    or ax, ax
    jz .okno
    push cx                         ; a later checkpoint exists, so the caret's
    mov cl, [sc_xksh]               ; row is inside this stride: stop there
    mov ax, 1
    shl ax, cl
    pop cx
    add ax, [sc_sdr]
    mov [sc_lastrow], ax
.okno:
    clc
    jmp short .out
.no:
    mov byte [sc_resume], 0         ; a REFUSAL is not a licence to resume at
    stc                             ; whatever the last caller seeded, which is
                                    ; what sc_seedck and sc_seedrow both say by
                                    ; clearing on entry
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_seedtail - seed at the DEEPEST row [sc_rows] describes, stopping after DX
; in:  DX = the visible row the walk has to reach
; out: [sc_resume] set if a seed was had; preserves all registers
;
; sc_seedrow's refusal is silent and its caller is then walking from index 0,
; so this is the fallback both of its callers want: the table stops somewhere,
; and the row BELOW where it stops is reached by walking forward a row or two
; rather than by laying the note out again from the top.
;
; It is sc_netseed's argument (SPEC.md 27.7.7) for a ROW instead of for the
; caret, and a weaker case than sc_netseed's: both of these callers are moving
; the caret, which reflows nothing, so every row above the seed laid out
; identically by inspection rather than by 27.4's reasoning.
; -----------------------------------------------------------------------------
sc_seedtail:
    push ax
    cmp byte [sc_rowsok], 0
    je .out                         ; no table at all
    mov ax, [sc_rowsn]
    or ax, ax
    jz .out                         ; it describes nothing: index 0 it is
    cmp ax, SC_MAXROWS              ; ...and [sc_rowsn] IS NOT CAPPED TO THE
    jbe .have                       ; ARRAY on the walk's natural-end path -
    mov ax, SC_MAXROWS              ; it is sc_row+1 there, so a 781-row note
.have:                              ; leaves it at 771 against 60 slots. Every
                                    ; sc_seedrow caller before this one passed
                                    ; a VISIBLE row, which sc_bounds caps at
                                    ; SC_MAXROWS, so nothing ever indexed past
                                    ; the table and the miss went unseen; this
                                    ; caller takes its row FROM [sc_rowsn] and
                                    ; would have been the first to read out of
                                    ; it
    cmp dx, ax
    jb .out                         ; THE GUARD, and the whole reason this is a
                                    ; routine rather than six lines at each
                                    ; call site: the deepest row is only a seed
                                    ; for a row BELOW it. sc_seedrow and
                                    ; sc_seedck each refuse for four different
                                    ; reasons and "the row is past the table"
                                    ; is only one of them - a caret on row 0
                                    ; asks sc_seedck for row -1 and is refused
                                    ; too, and seeding THAT at row 13 starts
                                    ; the walk below the row it is looking for,
                                    ; which then never finds it. Measured: the
                                    ; caret stopped moving on Up and the view
                                    ; stopped following it
    dec ax
    call sc_seedrow                 ; ...which also carries the bound in DX
.out:
    pop ax
    ret

sc_seedrow:
    push ax
    push bx
    mov byte [sc_resume], 0
    cmp byte [sc_rowsok], 0
    je .out
    cmp ax, [sc_rowsn]
    jae .out                        ; a row the table never described
    mov [sc_sdr], ax
    shl ax, 1
    mov bx, ax
    mov ax, [bx+sc_rows]
    cmp ax, [sc_len]
    ja .out
    mov [sc_sdi], ax
    mov [sc_lastrow], dx
    mov byte [sc_resume], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_chrome - the whole in-window chrome (SPEC.md 68.2): the nine-title menu
;             bar, then whichever of ribbon / ruler / status bar the View
;             toggles have on. Each strip painter fills its own band, so this
;             is correct over a fresh white fill AND as a repair after a
;             dropdown - the fill is one call per strip and the price of
;             being callable from both.
; in:  SI = window ptr, gfx lock held, sc_bounds run
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_chrome:
    call sc_mbar
    cmp byte [sc_vrib], 0
    je .norib
    call sc_ribbon
.norib:
    cmp byte [sc_vrul], 0
    je .norul
    call sc_ruler
.norul:
    cmp byte [sc_vsta], 0
    je .out
    call sc_status
.out:
    ret

; -----------------------------------------------------------------------------
; sc_paint - W_PAINT: draw the buffer and the caret
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_paint:
    push ax
    mov byte [sc_mopen], SC_M_NONE  ; a kernel repaint painted the content
    mov byte [sc_mhi], 0xFF         ; clean, so any dropdown, About box or
    mov byte [sc_about], 0          ; dialog is GONE from the pixels: drop the
    mov word [sc_dlg], 0            ; state with them rather than redraw a
    mov byte [sc_pend], 0           ; (...and the dirty prompt's pending
                                    ; action and the confirm session end with
                                    ; their dialogs, SPEC.md 68.4/68.7)
                                    ; menu the user has visibly lost
                                    ; (SPEC.md 68.2)
    call sc_bounds
    mov byte [sc_bmode], 0          ; whatever the break was showing, this
    mov word [sc_prowi], 0xFFFF     ; draws the NOTE over a filled content
    mov byte [sc_resume], 0
    mov word [sc_hity], 0xFFFF      ; no queries: this pass is here to draw
    mov word [sc_wanty], 0x7FFF
    cmp byte [sc_gchg], 0           ; a resize got here through W_PAINT, which
    je .laidout                     ; is not sc_redraw and so has clamped
                                    ; nothing: measure the note under the new
                                    ; wrap width and put the view back inside
                                    ; it. sc_bmode is 0 above, so sc_settle
                                    ; inside sc_measure is a no-op and this is
                                    ; just the walk
    call sc_hmark                   ; the wrap width moved, so every row start
                                    ; moved with it: the height is owed, and
                                    ; owed from the TOP (SPEC.md 27.7.5)
    mov ax, [sc_vrows]              ; ...and this walk is BOUNDED to the view
    mov [sc_lastrow], ax            ; like every other one (SPEC.md 27.7.1).
    call sc_measure                 ; It used to run to the last character for
                                    ; one number - the total - and drew not a
                                    ; pixel while it did, so a resize was the
                                    ; whole note walked INVISIBLY before the
                                    ; first row appeared
    mov ax, [sc_top]                ; The bound is also what answers the clamp,
    call sc_scrollto                ; exactly, without the total: a walk that
    mov byte [sc_resume], 0         ; STOPPED proved the note reaches past the
.laidout:                           ; bottom of the view, so [sc_top] is still
                                    ; good and sc_scrollmax cannot bite; one
                                    ; that ENDED set [sc_drows] to the truth on
                                    ; its way out and cleared the debt this
                                    ; block raised, so the clamp is right. The
                                    ; walk's own exit decides which, which is
                                    ; why nothing here tests for it
    mov byte [sc_draw], 1
    mov byte [sc_sigup], 1          ; the content was white-filled on the way
    mov byte [sc_clip], 0           ; here, so this pass draws every row AND is
    mov byte [sc_clean], 1          ; the baseline every later incremental
    mov ax, [sc_vrows]              ; ...and it stops at the bottom of the view
    mov [sc_lastrow], ax            ; like every other walk, because a full
                                    ; repaint of a 16KB note otherwise walks
                                    ; 16KB of it to draw one screenful - and
                                    ; every scroll step is a full repaint
    mov ax, [sc_top]                ; ...and it STARTS at the top of the view
    mov dx, [sc_vrows]              ; rather than at index 0 (SPEC.md 27.13).
    call sc_xseed                   ; The bound above stopped it walking past
                                    ; the view; this stops it walking TO the
                                    ; view, which a raise of a window scrolled
                                    ; halfway down a long note paid in full
    call sc_walk                    ; (SPEC.md 27.7/27.2)
    mov byte [sc_resume], 0         ; SPENT HERE, like every other sc_walk site
                                    ; (sc_hwalk, sc_brkdraw, sc_move, sc_vmove,
                                    ; sc_onclick, sc_dragsel, sc_redraw's
                                    ; .done). sc_xseed sets it and sc_paint
                                    ; cleared it only on the way IN, so from
                                    ; here it stayed set with [sc_sdr]/[sc_sdi]
                                    ; still loaded - and the next walk that
                                    ; seeds nothing of its own resumes at the
                                    ; top of THIS view. sc_measure is that
                                    ; walk, and sc_hmove reaches it bare
                                    ; whenever [sc_ckok] is 0
    mov byte [sc_clean], 0          ; ...and because it WAS filled, a row's run
    call sc_sigmark                 ; stops at its last character instead of
    mov ax, [sc_top]                ; padding to the band's edge to erase with
    mov [sc_ptop], ax               ; ...and the screen now shows THIS view
    pop ax
    call sc_sbar                    ; the fill took the bar with it
    call sc_chrome                  ; ...and the chrome strips' rules, which
                                    ; only a full fill can have erased
    call sc_sheet                   ; ...and the sheet's edges (SPEC.md 68.11)
    call sc_hire                    ; the worker exists from the first paint
                                    ; now (SPEC.md 68.2, Frotz's precedent):
                                    ; the CAPS/NUM lamps need its poll -
                                    ; CapsLock alone emits no key event - and
                                    ; File > Close needs a task to die on.
                                    ; Idempotent, and a refusal (task table
                                    ; full) is retried on every paint
    call sc_hirechk                 ; this walk stopped at the bottom of the
    ret                             ; view, so the height is still owed

; -----------------------------------------------------------------------------
; sc_hguess - what the note's LENGTH alone says about its height (SPEC.md 27.7.3)
; in:  [sc_rcols] valid (sc_bounds has run); out: [sc_drows] raised to it
; preserves all registers
;
; The chunked count takes seconds on a long note, and until it lands the only
; thing [sc_drows] holds is what the first screenful's bounded walk reached -
; so a 781-row file claimed to be 18 rows tall and the bar was drawn for a
; document that does not exist. This is the arithmetic answer available for
; nothing: the characters, divided by the cells a row holds.
;
; IT CAN ONLY EVER BE TOO SMALL, which is what makes it safe to publish. A row
; holds at most [sc_rcols] cells, so a note of L characters needs at least
; L/cols rows - and every newline ends a row EARLY, which can only push the
; real number up. That is exactly [sc_drows]'s existing direction of error
; (SPEC.md 27.7: a lower bound, never lowered), so nothing downstream needs a
; new rule and the walk goes on correcting it upward as it always did.
;
; The font is fixed-width, so "cells a row holds" is not an average of
; anything - it is [sc_rcols], which sc_bounds has just computed for the row
; buffer. One DIV, in a routine that has already made two far calls into the
; kernel at PERFORMANCE.md's ~756us apiece: under 2% of what it is riding on.
; -----------------------------------------------------------------------------
sc_hguess:
    push ax
    push cx
    push dx
    mov cx, [sc_rcols]
    jcxz .out                       ; a window too narrow to hold a cell: no
    mov ax, [sc_len]                ; geometry to divide by, and nothing to say
    xor dx, dx                      ; [sc_len] is capped at SC_MAXKB, so the
    div cx                          ; dividend never needs DX and cannot overflow
    cmp ax, [sc_drows]
    jbe .out                        ; never LOWERED, the one rule this shares
    mov [sc_drows], ax              ; with every other writer of it
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_hirechk - a height debt outlived this paint: make sure a worker exists
; preserves all registers
;
; The predicate in ONE place, called from both ends of the drawing: every exit
; of sc_redraw, and sc_paint - which is W_PAINT, and W_PAINT is the only thing
; that draws a window opened by DOUBLE-CLICKING A DOCUMENT. That launch loads
; the file in the entry proc and never calls sc_redraw at all, so the note
; whose height most needs counting - a whole file, arriving at once - was the
; one note that never got a worker to count it. It sat at the first
; screenful's lower bound until some later edit happened to hire one, and
; before then the first click paid the entire count in a single hold.
; -----------------------------------------------------------------------------
sc_hirechk:
    push ax
    cmp byte [sc_hdirty], 0
    je .out
    mov ax, [sc_drows]              ; a note that FITS needs no worker: the
    cmp ax, [sc_vrows]              ; walk that drew it ran to the note's end
    jbe .out                        ; and cleared the debt on the way
    call sc_hire
.out:
    pop ax
    ret

; =============================================================================
; The document claim (SPEC.md 27.6/50.3)
; =============================================================================

; -----------------------------------------------------------------------------
; sc_resize - make the document claim AX kilobytes
; in:  AX = the wanted size in KB (clamped to SC_KB0..SC_MAXKB)
; out: CF=0 with [sc_dseg]/[sc_capkb]/[sc_cap] updated, or CF=1 and all three
;      unchanged; preserves every register
;
; ALWAYS through OSAPI_MEM_REGROW, never claim-copy-free: a regrow extends in
; place when the paragraphs above it are free, so it needs the DIFFERENCE
; rather than old + new at once, and when it does have to move it brings the
; bytes with it (SPEC.md 50.3.1). Shrinking always succeeds in place, which
; is what makes File > New's give-back free.
; -----------------------------------------------------------------------------
sc_resize:
    push ax
    push dx
    cmp ax, SC_KB0
    jae .lo
    mov ax, SC_KB0
.lo:
    cmp ax, SC_MAXKB
    jbe .hi
    mov ax, SC_MAXKB
.hi:
    cmp ax, [sc_capkb]
    je .same                        ; already that size: nothing to ask for
    push ax
    mov dx, [sc_dseg]
    call OSAPI_MEM_REGROW           ; out CF=0 and DX = the base NOW
    pop ax
    jc .out                         ; refused: the old claim stands untouched
    mov [sc_dseg], dx               ; ...and a grow that MOVED reports a new
                                    ; base, which is the whole reason DX is
                                    ; the answer (SPEC.md 50.3.1)
    push ax                         ; --- the CHP claim, in lockstep (SPEC.md
    mov dx, [sc_cseg]               ; 65.3): one attribute byte per character,
    call OSAPI_MEM_REGROW           ; so the two claims are always the same
    pop ax                          ; size. If the mirror is refused, the text
    jnc .cok                        ; claim is put back - a shrink (or a
    push ax                         ; same-size ask) always succeeds in place,
    mov ax, [sc_capkb]              ; so the pair stays consistent and the
    mov dx, [sc_dseg]               ; caller sees one refusal
    call OSAPI_MEM_REGROW
    mov [sc_dseg], dx
    pop ax
    stc
    jmp short .out
.cok:
    mov [sc_cseg], dx
    mov [sc_capkb], ax
    push cx                         ; CL is borrowed for the shift and PUT
    mov cl, 10                      ; BACK: sc_load banks its text length in
    shl ax, cl                      ; CX across this call, and a public
    pop cx                          ; routine preserves what it does not
                                    ; answer with (SPEC.md 1)
    mov [sc_cap], ax                ; SC_MAXKB * 1024 fits a word by design
.same:
    clc
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_fitclaim - size the claim to the note plus one kilobyte to type into
; out: nothing (all registers and the flags preserved)
;
; What a load ends with, on the way out of both its paths. sc_load opens the
; claim to SC_MAXKB before the read because nothing knows the file's size
; until the read reports it; this is the other half of that, and it runs even
; when the read failed - a refused load must not leave eight kilobytes of heap
; held for a note that did not change.
; -----------------------------------------------------------------------------
sc_fitclaim:
    pushf
    push ax
    push cx
    mov ax, [sc_len]
    add ax, 1023                ; the note's own whole kilobytes...
    mov cl, 10
    shr ax, cl
    add ax, SC_GROWKB           ; ...plus one to type into
    call sc_resize              ; a shrink always succeeds in place
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; sc_room - make sure one more character fits
; out: CF=0 there is room at [sc_len], CF=1 the note is as big as it can get
; clobbers: flags
;
; The growth point, and the only one. A keystroke that would fill the claim
; asks for another kilobyte first; a refusal - the heap's or SC_MAXKB's - is
; the keystroke being dropped, which is what a full note did before it could
; grow at all.
; -----------------------------------------------------------------------------
sc_room:
    push ax
    mov ax, [sc_len]
    cmp ax, [sc_cap]
    jb .yes
    mov ax, [sc_capkb]
    add ax, SC_GROWKB
    call sc_resize
    jc .no
    mov ax, [sc_len]
    cmp ax, [sc_cap]
    jb .yes
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stghold - claim the save's .DOC staging buffer (SPEC.md 68.4)
; in:  [sc_len]
; out: CF=0 with [sc_stgseg] set and ES = it, or CF=1 (the toast is already
;      set); preserves every other register
;
; Sized from the note, not fixed: the image is the FIB, the text, its CHP
; twin and the PAP table. It is a SECOND claim and it is transient - held
; only across the write - because the expansion grows and the document claim
; is sized for the document. A refusal is an ordinary path: the note is still
; there and still editable, it just cannot reach the disk until something
; gives memory back.
; -----------------------------------------------------------------------------
sc_stghold:
    push ax
    push cx                         ; CL is the shift count below, and the
    push dx                         ; header promises every other register
    mov ax, SC_DOCCAP               ; the WHOLE ceiling, not a size computed
                                    ; from the document: a real Word file is
                                    ; the text plus FKP pages, and how many
                                    ; pages it needs is what the writer finds
                                    ; out as it goes (SPEC.md 68.4). The claim
                                    ; is transient - held only across the
                                    ; write - so taking it whole costs the
                                    ; machine nothing between saves
.kb:
    call OSAPI_MEM_CLAIM            ; out CF=0 and DX = the base segment
    jc .no
    mov [sc_stgseg], dx
    mov es, dx
    clc
    jmp short .out
.no:
    mov word [sc_stgseg], 0
    mov ax, sc_e_nomem
    call sc_saymsg
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stgdrop - hand the staging buffer straight back
; out: nothing (all registers and the flags preserved)
; -----------------------------------------------------------------------------
sc_stgdrop:
    pushf
    push ax
    push dx
    mov dx, [sc_stgseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [sc_stgseg], 0
.out:
    pop dx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; sc_save - write the document as a native .DOC (SPEC.md 68.4)
; in:  nothing (the buffers and their lengths)
; out: nothing; the outcome is said as a toast; preserves all registers
;
; The whole image - the 16-byte FIB, the text, the CHP bytes and the PAP
; table - is assembled into the staging claim and written with ONE
; OSAPI_FILE_WRITE. Always the native format, whatever the name's extension,
; as the real product's Save was. A success clears [sc_dirty].
; -----------------------------------------------------------------------------
sc_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp                         ; BP TOO: the overlay verb goes in it, and
                                    ; sc_modc shifts it, so a caller banking a
                                    ; pointer there got twice the verb back
    call sc_stghold             ; ES = the staging claim, or a toast and out
    jc .out
    call sc_goto                ; the folder this document belongs to, if the
                                ; volume has been moved since (SPEC.md 19.2)
    call sc_isrtf               ; a .RTF name is the user naming a FORMAT,
    jnc .rtf                    ; and it is the one extension Save honours
    mov bp, SCM_DOCIMG          ; the whole Word file - FIB, text, FKPs,
    call sc_ovcall
    jc .big                     ; STSH, plcfsed, bin tables, DOP (65.4)
    jmp short .write
.rtf:
    mov bp, SCM_RTFIMG          ; ...or the RTF text (SPEC.md 68.8)
    call sc_ovcall
    jc .big
.write:
    mov cx, [sc_dend]           ; ES:BX = the image, DX:CX its byte count
    xor bx, bx
    xor dx, dx
    mov si, sc_name
    call OSAPI_FILE_WRITE
    jc .err
    mov byte [sc_dirty], 0      ; the disk matches the document again (65.4)
    mov si, sc_m_saved
    call sc_setmsg
    jmp short .done
.big:
    mov ax, sc_m_toobig         ; more FKP pages than the staging claim holds
    call sc_saymsg              ; - refused whole, the document still open
    jmp short .done             ; and still editable (SPEC.md 68.4)
.err:
    call sc_errmsg              ; AX = FERR_* -> the toast
.done:
    call sc_stgdrop
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_isrtf - does the document's name end '.RTF'?
; out: CF=0 = yes; preserves every register
;
; The one extension that decides a format (SPEC.md 68.8). Everything else -
; including a name with no extension at all - writes the Word file, which is
; what the real product's Save did.
; -----------------------------------------------------------------------------
sc_isrtf:
    push ax
    push bx
    mov bx, sc_name
.find:
    mov al, [bx]
    or al, al
    jz .no
    inc bx
    cmp al, '.'
    jne .find
    mov al, [bx]
    call sc_upc
    cmp al, 'R'
    jne .no
    mov al, [bx+1]
    call sc_upc
    cmp al, 'T'
    jne .no
    mov al, [bx+2]
    call sc_upc
    cmp al, 'F'
    jne .no
    cmp byte [bx+3], 0
    jne .no
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; sc_upc - AL to upper case. Preserves all others.
sc_upc:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; -----------------------------------------------------------------------------
; sc_load - read a file into the document (SPEC.md 68.4)
; in:  nothing (sc_name)
; out: nothing; the outcome is said as a toast; preserves all registers
;
; The whole file lands in a transient staging claim first, and the magic is
; sniffed there: a native .DOC has every FIB field validated against the file
; size - a corrupt file is refused with a toast and the document untouched -
; then text, CHP and PAP copy into their claims. Anything else imports as
; plain text: CR LF and a lone LF fold to 13, tabs are kept, other controls
; drop, the CHP zeroes and every paragraph is Normal. A file bigger than the
; staging ceiling is FERR_BIG from the directory entry, before any I/O.
; -----------------------------------------------------------------------------
sc_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp                         ; BP TOO: the overlay verb goes in it, and
                                    ; sc_modc shifts it, so a caller banking a
                                    ; pointer there got twice the verb back
    mov ax, SC_LSTGKB           ; the staging claim, transient across the read
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [sc_stgseg], dx
    call sc_goto                ; the folder dance on the way in
    mov es, [sc_stgseg]
    xor bx, bx                  ; ES:BX = staging, DX:CX its capacity
    mov cx, SC_LSTGKB * 1024
    xor dx, dx
    mov si, sc_name
    call OSAPI_FILE_READ        ; DX:AX = the file's size; bigger than the
    jc .err                     ; capacity is FERR_BIG decided from the
                                ; directory entry, buffer untouched (18.4)
    push ax                     ; the byte count, kept across the sniffs
    mov bp, SCM_ISRTF           ; '{\rtf' -> the RTF reader (SPEC.md 68.8)
    call sc_ovcall
    jc .nortf
    pop ax
    mov bp, SCM_RTFPARSE        ; frees the OLD pictures ITSELF and may then
    call sc_ovcall
    jc .bad                     ; register new ones (SPEC.md 94.6), so it
    jmp short .loaded2          ; must not meet sc_pictfree on the way out
.nortf:
    pop ax
    cmp ax, SC_HDRPAGE
    jb .plain
    mov es, [sc_stgseg]
    cmp word [es:SCF_IDENT], SC_DOCMAGIC
    jne .plain

    ; --- a real Word file: FIB, pieces, FKPs, the lot (SPEC.md 68.4) ------
    mov bp, SCM_DOCPARSE        ; CF=1 = refused whole, document untouched
    call sc_ovcall
    jc .bad
.loaded:
                                ; NO sc_pictfree HERE ANY MORE. Both readers
                                ; BUILD pictures now (88.6/88.7), and a free
                                ; after them hands back the ones the file just
                                ; supplied - the text arrives and the table
                                ; comes back empty. So each parser frees the
                                ; old ones itself, at the point past its own
                                ; last refusal: sc_rtfparse on its first line,
                                ; sc_docparse after sc_dcompact. The rule this
                                ; used to enforce is unchanged - a refusal
                                ; still must not cost the pictures it was not
                                ; replacing - it is enforced one level down
.loaded2:
    call sc_clamp               ; caret to the top; clears the has* flags,
                                ; the tail and the typing attrs (65.3)
    mov bp, SCM_LDPOST          ; ...then the loaded facts go back
    call sc_ovcall
    mov byte [sc_dirty], 0
    mov si, sc_m_loaded
    call sc_setmsg
    cmp byte [sc_dtrunc], 0
    je .done0
    mov ax, sc_m_trunc
    call sc_saymsg
.done0:
    jmp .done

.plain:
    ; --- plain-text import (65.4): fold from staging into the document -----
    push ax                     ; the byte count
    mov ax, SC_MAXKB            ; open the claim to its ceiling; the fold
    call sc_resize              ; only ever SHRINKS what arrived
    pop ax
    jc .nomem3
    mov cx, ax
    push ds
    mov ds, [cs:sc_stgseg]      ; DS:SI walks what arrived...
    mov es, [cs:sc_dseg]        ; ...and ES:DI writes the kept characters
    xor si, si
    xor di, di
    xor dx, dx                  ; DL = previous byte, DH = truncation flag
    cld
.fold:
    jcxz .folded
    lodsb
    dec cx
    cmp al, 10
    jne .notlf
    cmp dl, 13
    je .skip                    ; CR LF: the 13 already went in
    mov al, 13                  ; a lone LF is a line break too
.notlf:
    cmp al, 13
    je .store
    cmp al, 9                   ; tabs import (SPEC.md 68.3); sc_ldscan
    je .store                   ; re-raises the flag after the clamp
    cmp al, 32
    jb .skip
    cmp al, 126
    ja .skip
.store:
    cmp di, [cs:sc_cap]
    jb .room
    mov dh, 1                   ; a 62KB text file can outgrow the 30KB
    jmp short .folded           ; document: keep what fits and say so
.room:
    stosb
.skip:
    mov dl, al
    jmp short .fold
.folded:
    pop ds
    mov [sc_len], di
    push ax                     ; a plain-text load arrives undressed: its
    push cx                     ; CHP bytes are all zero (SPEC.md 68.3)
    push di
    mov es, [sc_cseg]
    mov cx, di
    jcxz .nochp
    xor di, di
    xor al, al
    rep stosb
.nochp:
    pop di
    pop cx
    pop ax
    call sc_pictfree            ; ...and the same on the plain-text path, whose
                                ; commit point is its own
    call sc_clamp               ; a shorter file must not leave the caret
                                ; past the end of it
    call sc_ldscan              ; any tab that survived re-raises the flag
    mov byte [sc_dirty], 0
    mov si, sc_m_loaded
    call sc_setmsg
    test dh, dh
    jz .done
    mov ax, sc_m_trunc
    call sc_saymsg
    jmp short .done

.bad:
    mov ax, sc_m_baddoc         ; refused whole: the document is untouched
    call sc_saymsg              ; (SPEC.md 68.4)
    jmp short .done
.err:
    call sc_errmsg
    jmp short .done
.nomem3:
    mov ax, sc_e_nomem
    call sc_saymsg
.done:
    call sc_fitclaim            ; give back what the file did not need
    call sc_stgdrop             ; ...and the staging claim with it
    jmp short .out
.nomem:
    mov ax, sc_e_nomem
    call sc_saymsg
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ldscan - what a load must rediscover: tabs and hidden text (SPEC.md 68.4)
; in:  the document and CHP claims loaded, [sc_len] set (sc_clamp has run)
; out: [sc_hastab] / [sc_hashid] raised as found; preserves all registers
; A ¶ mark's CHP byte is a PAP index (65.3), so the hidden scan steps over
; marks - an index with bit 6 set is not hidden text.
; -----------------------------------------------------------------------------
sc_ldscan:
    push ax
    push bx
    push cx
    push di
    push es
    mov cx, [sc_len]
    jcxz .out
    mov es, [sc_dseg]           ; --- tabs: one repne scasb ---
    xor di, di
    mov al, 9
    cld
    repne scasb
    jne .notabs
    mov byte [sc_hastab], 1
.notabs:
    mov cx, [sc_len]            ; --- hidden: text and CHP in step ---
    xor bx, bx
    push ds
    mov ds, [cs:sc_cseg]        ; DS = the CHP, ES = the text
.h:
    mov al, [es:bx]
    cmp al, 13
    je .hn                      ; a mark's byte is an index, not attrs
    test byte [bx], SCAT_HID
    jz .hn
    mov byte [cs:sc_hashid], 1
    jmp short .hdone
.hn:
    inc bx
    loop .h
.hdone:
    pop ds
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_errmsg - turn a FERR_* code into the toast string
; in:  AX = FERR_* (SPEC.md 18.4)
; out: nothing - the string is said as a toast; preserves all registers
; -----------------------------------------------------------------------------
sc_errmsg:
    push ax
    push bx
    cmp ax, FERR_BIG
    jbe .known
    mov ax, FERR_IO             ; an unknown code is still a disk problem
.known:
    mov bx, ax
    shl bx, 1
    mov ax, [bx+sc_errtab]
    call sc_saymsg
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ins - insert AL at the caret, which then sits after it
; in:  AL = the character; out: nothing; preserves all registers
;
; The gap is opened right to left because source and destination overlap, and
; by hand rather than with `rep movsb` for a reason that is easy to forget: a
; callback is entered with ES = KERNEL_SEG (SPEC.md 20.1), so a string move
; would write the gap into the KERNEL's memory at our offsets.
; -----------------------------------------------------------------------------
sc_ins:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dl, al
    cmp al, 9                       ; a tab entered: the column-arithmetic
    jne .nt9                        ; fast paths stand down (SPEC.md 68.3;
    mov byte [sc_hastab], 1         ; set-only, conservative, like sc_hashid)
.nt9:
    call sc_room                    ; grow by a kilobyte if this is the
    jc .out                         ; keystroke that fills the claim; a
                                    ; refusal drops it, as a full note always
    call sc_hmark                   ; the note is a different length, so it
                                    ; may be a different number of rows, and
                                    ; [sc_drows] is a lower bound until
                                    ; something walks the whole of it
    mov es, [sc_dseg]               ; did (SPEC.md 27.6)
    mov bx, [sc_len]
    mov cx, bx
    sub cx, [sc_cur]                ; CX = the bytes to the right of the caret
    jcxz .place
    mov si, bx
    dec si                          ; SI = the last live byte...
    mov di, bx                      ; ...and DI one past it: the runs overlap
    push ds                         ; and the gap opens UPWARD, so backwards
    mov ds, [sc_dseg]               ; (SPEC.md 27.12). movsb is DS:SI -> ES:DI
    std                             ; and both ends are the note
    rep movsb
    cld                             ; SPEC.md 1: never leave DF set
    pop ds
.place:
    mov bx, [sc_cur]
    mov [es:bx], dl
    ; --- the same gap on the CHP claim (SPEC.md 68.3), in lockstep: open it
    ; backwards exactly as above, and the new cell takes the typing attrs
    mov es, [sc_cseg]
    mov cx, [sc_len]
    sub cx, bx
    jcxz .cput
    mov si, [sc_len]
    dec si
    mov di, [sc_len]
    push ds
    mov ds, [sc_cseg]               ; the operand reads through the OLD DS
    std
    rep movsb
    cld
    pop ds
.cput:
    mov dl, [sc_chp]
    mov [es:bx], dl
    test dl, SCAT_HID               ; a hidden character just entered: the
    jz .nohid                       ; cheap column paths stand down (65.1)
    mov byte [sc_hashid], 1
.nohid:
    inc word [sc_len]
    inc word [sc_cur]
    mov byte [sc_dirty], 1          ; the document differs from the disk (65.4)
    mov ax, bx                      ; ...and remember it, which is what makes
    mov cx, 1                       ; a burst of typing one undoable edit
    call sc_urec_ins                ; (SPEC.md 27.9)
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
; sc_del - delete the character the caret sits in front of
; out: nothing; preserves all registers. A caret at the end deletes nothing.
; -----------------------------------------------------------------------------
sc_del:
    push bx
    push cx
    mov bx, [sc_cur]
    cmp bx, [sc_len]
    jae .out                        ; a caret at the end deletes nothing
    mov cx, 1
    call sc_delspan                 ; the run is the primitive now (SPEC.md
                                    ; 27.8), and it is what records the undo
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_move - put the caret on the row [sc_wanty] at column [sc_wantx]
; in:  SI = window ptr, the two query fields set
; out: [sc_cur] moved if that row exists; preserves all registers
; -----------------------------------------------------------------------------
sc_move:
    push ax
    push dx
    mov word [sc_hity], 0xFFFF      ; one query at a time
    mov ax, [sc_wanty]              ; the VISIBLE ROW it names (SPEC.md 68.6)
    mov dx, ax                      ; is the ONLY row this walk has to visit
                                    ; (SPEC.md 27.5); negative = above the
                                    ; view, which is not in [sc_rows]...
    or ax, ax
    js .above
    call sc_seedrow
    cmp byte [sc_resume], 0
    jne .bound                      ; the table describes it: one row walked
    call sc_seedtail                ; ...and when it does NOT - which is every
                                    ; Down on the bottom visible row, the row
                                    ; below the view being one past the table -
                                    ; resume at the deepest row it DOES hold
    jmp short .bound
.above:
    push ax                         ; ...but the ROW INDEX describes rows the
    mov ax, dx                      ; view has never contained (SPEC.md 27.13),
    add ax, [sc_top]                ; which is what Up out of the top wants: it
    call sc_xseed                   ; takes an ABSOLUTE row, and DX is visible
    pop ax                          ; (sc_xseed sets the bound itself, and
                                    ; .bound below setting it again is the same
                                    ; value - one place, whichever path ran)
.bound:
    ; THE BOUND IS SET ON EVERY PATH, and that is the whole fix (SPEC.md
    ; 27.7.9). [sc_lastrow] is a one-shot that sc_walk resets to 0x7FFF, so a
    ; caller which does not set it walks the WHOLE NOTE - "slow and never
    ; wrong", which sc_seedrow's silent refusal turned into the common case:
    ; Down on the bottom visible row asks for a row one past the table, got no
    ; seed AND no bound, and re-laid out all 781 rows of README.TXT to find the
    ; row directly under the caret. Measured on a 4.77MHz 8088: 4,663 ms of a
    ; 4,866 ms keystroke, on the most used key in the editor.
    mov [sc_lastrow], dx
    call sc_measure
    mov byte [sc_resume], 0
    mov ax, [sc_wanti]
    cmp ax, 0xFFFF
    je .out                         ; no such row: the caret stays put, which
    mov [sc_cur], ax                ; is what Up on the first line should do

    ; The caret moved but nothing else did, so the repaint may resume too - at
    ; whichever of the two rows comes FIRST. Up lands on the row above the
    ; checkpoint, and seeding at the checkpoint would then walk straight past
    ; the caret without ever finding it, which draws the bar at (0,0).
    mov ax, [sc_wanty]              ; already a row (SPEC.md 68.6)
    or ax, ax
    js .out

    ; THE TWO ROWS ARE THE WHOLE OF IT (SPEC.md 27.4.1). sc_ask folds the caret
    ; into a row's signature and a caret move changes nothing else, so the only
    ; rows whose signatures can differ are the one it left and the one it
    ; arrived on. The seed below already puts the walk at the FIRST of them;
    ; this records the SECOND, so sc_redraw's pass 1 can stop there instead of
    ; laying out the rest of the view to be told nothing moved.
    ; [sc_ckpr] is still the row the caret CAME FROM at this point - the branch
    ; below is what moves it back - so the two are in hand together here and
    ; nowhere else.
    mov [sc_mvbot], ax
    cmp ax, [sc_ckpr]
    jae .arm                        ; already at or after the checkpoint
    push ax                         ; ...moving BACKWARDS, so the deeper of the
    mov ax, [sc_ckpr]               ; two is the row being left
    mov [sc_mvbot], ax
    pop ax
    cmp byte [sc_rowsok], 0
    je .out
    cmp ax, [sc_rowsn]
    jae .out
    push bx
    mov [sc_ckpr], ax
    shl ax, 1
    mov bx, ax
    mov ax, [bx+sc_rows]
    mov [sc_ckpi], ax
    pop bx
.arm:
    mov byte [sc_fast], 4           ; A CARET MOVE, and it used to say 3.
                                    ; sc_fastok*'s contract numbers the kinds
                                    ; 1 insert, 2 backspace, 3 forward Delete,
                                    ; 4 a caret move, and they carry different
                                    ; PERMISSIONS: 1..4 may resume the walk,
                                    ; but only 1..3 may enter the visual break.
                                    ; sc_move wants the first and never the
                                    ; second - "a caret move reflowed nothing",
                                    ; as sc_redraw's own comment says - and 3
                                    ; granted it both. It was invisible in a
                                    ; 29-column window only because sc_brktry
                                    ; needs SC_BRK_CELLS = 60 cells below the
                                    ; caret and one row there is 29; widen the
                                    ; window past 60 columns and Up entered the
                                    ; break, on a stale [sc_ecol] left by some
                                    ; earlier edit, which is a phantom line
                                    ; break for as long as the settle takes
.out:
    mov byte [sc_resume], 0
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_vmove - Up / Down: the same column, one row away
; in:  SI = window ptr, DX = -1 (up) or +1 (down), in ROWS (SPEC.md 68.6)
; out: nothing; preserves all registers
;
; It measures twice: once to find the pixel the caret is at, then again for
; the index at that column on the neighbouring row. Two walks of at most 512
; characters, once per keystroke.
; -----------------------------------------------------------------------------
sc_vmove:
    push ax
    push dx
    call sc_settle                  ; before the seed, not inside sc_measure:
                                    ; a reconcile runs walks of its own and
                                    ; would spend the seed we are about to set
    mov word [sc_hity], 0xFFFF
    mov word [sc_wanty], 0x7FFF
    push dx                         ; the caret is on its checkpoint's row by
    mov dx, [sc_ckpr]               ; definition, so this walk is that row and
    call sc_seedck                  ; no more (SPEC.md 27.4/27.5)
    cmp byte [sc_resume], 0
    jne .lim
    cmp byte [sc_ckok], 0           ; THE SEED FAILED, and the bound used to go
    je .nolim                       ; with it (SPEC.md 27.7.9). sc_seedck asks
                                    ; for the row BEFORE the caret's, so a
                                    ; caret on a row past what [sc_rows]
                                    ; describes is refused - and [sc_lastrow]
                                    ; is a one-shot sc_walk resets to 0x7FFF,
                                    ; so the walk then laid out the WHOLE NOTE
                                    ; to find the row the caret is already on.
                                    ; With no checkpoint there is no row to
                                    ; bound to either, and that case is the
                                    ; old one unchanged
    call sc_seedtail                ; ...but with one, the table still reaches
                                    ; SOMEWHERE: resume there and walk forward
    cmp byte [sc_resume], 0
    jne .lim
    push ax                         ; ...and when even that is refused - the
    mov ax, dx                      ; caret's row is ABOVE what sc_rows
    add ax, [sc_top]                ; describes, which is every Up out of the
    call sc_xseed                   ; top of the view - the row index has it
    pop ax                          ; (SPEC.md 27.13)
.lim:
    or dx, dx                       ; ...unless that row is ABOVE the view,
    js .nolim                       ; where the signed limit would stop the
    mov [sc_lastrow], dx            ; walk before it had found anything
.nolim:
    pop dx
    call sc_measure                 ; [sc_curx]/[sc_currow]
    mov byte [sc_resume], 0
    mov ax, [sc_currow]             ; the neighbouring ROW - a row's y is the
    add ax, dx                      ; walk's business now (SPEC.md 68.6)
    mov [sc_wanty], ax
    mov ax, [sc_curx]
    mov [sc_wantx], ax
    call sc_move
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_hmove - Home / End: the same row, the far left or the far right
; in:  SI = window ptr, DX = the column to aim at (0 or 0x7FFF)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_hmove:
    push ax
    call sc_settle                  ; before the seed - see sc_vmove
    mov word [sc_hity], 0xFFFF
    mov word [sc_wanty], 0x7FFF
    cmp byte [sc_ckok], 0           ; the caret's row IS the checkpoint's, so
    je .measure                     ; Home and End need no walk at all to find
    mov ax, [sc_ckpr]               ; the row they are aiming at - a ROW,
    jmp short .have                 ; straight through (SPEC.md 68.6)
.measure:
    call sc_measure                 ; [sc_currow] = the row we are on
    mov ax, [sc_currow]
.have:
    mov [sc_wanty], ax
    mov [sc_wantx], dx
    call sc_move
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_onclick - W_ONCLICK: put the caret where the user pointed
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; clobbers what any window callback may
;
; The kernel only sends content clicks on the front window (SPEC.md 13), so
; there is no rect to test: every click that arrives here is ours, and the
; walk answers with the nearest character boundary - or with the end of the
; note for a click below the last line.
; -----------------------------------------------------------------------------
sc_onclick:
    push ax
    push dx
%ifdef OS88UI_SBDRAG
    call os88ui_sbdragging      ; A PRESS CANNOT ARRIVE DURING A LIVE DRAG, so
    jc .nostale                 ; one that does means the release never came
    push cx                     ; (SPEC.md 13.10.5.7). BEFORE the routing, as
    push dx                     ; np_onclick's is: the chrome consumes most of
    call sc_bounds              ; this window, and a press it eats is still a
    call sc_sbset               ; press that proves the drag is over
    call os88ui_sbdrop
    pop dx
    pop cx
.nostale:
%endif
    call sc_mroute                  ; the in-window chrome first (SPEC.md
    jnc .live                       ; 65.2): an open dropdown or the About box
    pop dx                          ; owns EVERY click, and the menu bar,
    jmp .out                        ; ribbon, ruler and status strips own the
.live:                              ; clicks that land on them. CF=1 = it was
                                    ; one of those and has been fully handled
    mov [sc_hitx], cx
    mov [sc_hity], dx
    mov word [sc_wanty], 0x7FFF
    call sc_settle                  ; the pointer has to be over the NOTE
    call sc_bounds                  ; before it can be resolved (SPEC.md 27.3)
    call sc_uclose                  ; ...and a click is not typing, so whatever
                                    ; was being typed is one finished edit
                                    ; NOTHING here finishes the count any more.
                                    ; A click in the TEXT wants no total at
                                    ; all, and one on the BAR wants it only if
                                    ; it asks to go PAST what has been counted
                                    ; - which sc_sbclick tests for itself, at
                                    ; the one place that knows which row is
                                    ; being asked for (SPEC.md 27.7.6).
                                    ; Finishing it here froze the machine on
                                    ; the first bar click after opening a file,
                                    ; which is exactly when the count has got
                                    ; least far and the freeze is longest
    call sc_sbclick                 ; ...and the scroll bar is not the note
    jc .text
    pop dx
    ; **A CLICK THE BAR TOOK IS NOT A CLICK THAT SCROLLED** (SPEC.md 27.7.10).
    ; Note Pad's defect, in the wider window: a press on the THUMB, or an arrow
    ; already at an end stop, used to reach sc_redraw and pay its whole measure
    ; walk to conclude that no row changed.
    cmp byte [sc_sbmoved], 0
    je .out                         ; not one pixel changed
    call OSAPI_EVQ_PENDING          ; is another click right behind this one?
    or ax, ax                       ; then this scroll position is already
    jz .drawscroll                  ; superseded and drawing it is work the
    mov byte [sc_sowed], 1          ; user will never see (SPEC.md 27.7.8).
    jmp short .out                  ; The WORKER owes the last one, because the
.drawscroll:                        ; queue may not be ours to finish
    call sc_redraw                  ; sc_scrollto dropped sc_sigok, so this is
    jmp short .out                  ; the full path (SPEC.md 27.7)
.text:
    cmp dx, [sc_ty]                 ; the backstop under sc_mroute (SPEC.md
    jb .chrome                      ; 65.2): what reaches here above [sc_ty]
    cmp dx, [sc_bot]                ; is the margin band, and below [sc_bot]
    ja .chrome                      ; nothing (sc_mroute consumed the status
                                    ; strip) - both inert. Without this a
                                    ; margin press matched no row and the hit
                                    ; query's default sent the caret to the
                                    ; END of the document
    call sc_yrow                    ; the row the click landed on is the only
    jc .full                        ; one that can answer it (SPEC.md 27.5) -
    mov dx, ax                      ; found from the banked ys under formats,
    call sc_seedrow                 ; arithmetic while uniform; a refusal is
.full:                              ; the unseeded walk, slow and never wrong
    pop dx
    call sc_measure
    mov byte [sc_resume], 0
    mov byte [sc_ext], 0            ; a click re-anchors: extend mode is over
    mov ax, [sc_hiti]
    call sc_selq                    ; a press INSIDE the selection is a drag of
    jc .move                        ; the text, not a new selection (27.8.1)
    mov [sc_anchor], ax
    mov [sc_cur], ax
    push ax
    call sc_selclr
    pop ax
    jc .nosel                       ; there was no selection to erase, so the
    mov byte [sc_ckok], 0           ; band the walk resumes at would have left
.nosel:                             ; its inversion on screen
    call sc_chpsync                 ; the caret landed: typing attrs follow it
    call sc_redraw
    call sc_dragsel                 ; ...and then follow the pointer until the
    jmp short .out                  ; button comes up
.move:
    call sc_dragmove
    jmp short .out
.chrome:
    pop dx                          ; the prologue's DX; a blanked strip has
                                    ; nothing to do yet
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_clamp - hold the caret inside the buffer after a load or a New
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_clamp:
    push ax
    mov byte [sc_chp], 0            ; a new document types plain until told
    mov byte [sc_hashid], 0         ; otherwise, and carries no hidden text
                                    ; (SPEC.md 68.3; a load zeroes the CHP)
    mov byte [sc_hastab], 0         ; ...and no tabs (a load that kept some
                                    ; re-raises this after the clamp)
    mov byte [sc_hasfmt], 0         ; every paragraph is Normal again: a load
    mov byte [sc_pap_tail], 0       ; zeroes the CHP, so every ¶ is index 0,
                                    ; and the tail follows suit (SPEC.md 68.3).
                                    ; The dictionary itself is session-lived
                                    ; and stands - indices in dead text point
                                    ; nowhere
    call sc_selclr                  ; a whole new buffer: the selection and
    call sc_uclear                  ; the undo stack are about the note that
                                    ; just went away, and an undo record
                                    ; applied to a different note corrupts it
                                    ; (SPEC.md 27.9)
    mov byte [sc_ext], 0            ; ...and extend mode ends with it
    call sc_hmark                   ; a whole new note is a whole new height
    mov word [sc_top], 0            ; a NOTE row, so it names nothing once the
                                    ; note is replaced - and the top of a file
                                    ; just opened is where a reader starts
    mov byte [sc_sigok], 0          ; ...and the SCREEN's signatures describe
                                    ; the note that went away. Found the hard
                                    ; way (SPEC.md 68.4): File > New over a
                                    ; FORMATTED document cleared [sc_hasfmt]
                                    ; here, and the uniform walk that followed
                                    ; redraws rows at 8px pitch - but the old
                                    ; layout's extra leading had pushed its
                                    ; last rows BELOW the last uniform row's
                                    ; band, where no uniform row could ever
                                    ; erase them. A whole-buffer swap owes the
                                    ; full repaint anyway
    mov byte [sc_ckok], 0           ; the whole buffer just changed underneath
    mov byte [sc_rowsok], 0         ; them, and both are indices INTO it
                                    ; (SPEC.md 27.4/27.5). sc_bounds catches a
                                    ; geometry change and nothing caught this;
                                    ; every path here does in fact reach
                                    ; sc_redraw with [sc_fast] clear and walk
                                    ; in full, but that is a fact about the
                                    ; callers rather than about the data
    mov ax, [sc_len]
    cmp [sc_cur], ax
    jbe .out
    mov [sc_cur], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_fastok* - five doors onto one answer: may sc_redraw take the cheap paths
;              for this keystroke, and what does the visual break need to know
;              about it? (SPEC.md 27.4/27.3)
; in:  [sc_cur] and the note BEFORE the edit
; out: [sc_fast] = the KIND, 0 if none: 1 insert, 2 backspace, 3 forward
;      Delete, 4 a caret move. [sc_ecol] = the caret's column on its row,
;      before the edit; [sc_eext] = columns of the row below that go stale
;      BEYOND that one. All three left alone when the answer is no.
;      Preserves all registers
;
; The kind carries two different permissions and they are NOT the same set:
;   walk may resume        kinds 1..4  - nothing ahead of the caret moved
;   break may be ENTERED   kinds 1..3  - an edit reflowed something worth not
;                                        drawing; a caret move reflowed nothing
;   break may CONTINUE     kinds 1..2  - while the break is up the TAIL is not
;                                        redrawn, so anything that would move
;                                        the break point or eat the tail's
;                                        first character has to settle first.
;                                        Right would draw a character twice
;                                        and Left would lose one; Delete eats
;                                        exactly the tail's first character
;
; The resume test is two questions: the checkpoint has to describe this layout
; at all, and the edit has to fall at or after it. The second is what "inside
; the caret's own row" means - a backspace at column 0 eats the last character
; of the row ABOVE, which is before the checkpoint, and that is the one
; deletion the resumed walk could not see.
;
; THE EDIT COLUMN IS REPORTED, NOT DERIVED, and that is the whole reason
; backspace can enter the break at all. The break scrolls the caret's row down
; and redraws it, so the copy left below duplicates the row's prefix and has to
; be blanked - and the prefix is C cells for an insert AND for a backspace,
; but the caret ends at C+1 in one case and C-1 in the other. Deriving C from
; where the caret ENDED therefore runs the opposite way for each, which is a
; direction test in a place with no business knowing about directions; getting
; it wrong left two stale characters on the row below. Here the caret's column
; is [sc_cur] - [sc_ckpi] outright, because a row start is a character index
; and every character on a row occupies exactly one cell (a newline ends a row,
; so there cannot be one in between). Forward Delete is then the same fact plus
; one: the character it removes was ON that row, so the copy is stale one cell
; further.
;
; It is deliberately NOT a test of what the redraw will cost: pass 1 answers
; that, and it can only answer it after this has let it run cheaply.
; -----------------------------------------------------------------------------
sc_fastok:                          ; a printable at the caret
    push ax
    push bx
    push cx
    mov bx, 1
    xor cx, cx
    mov ax, [sc_cur]
    jmp short sc_fastcm
sc_fastokb:                         ; Backspace: the character that goes is one
    push ax                         ; index earlier, so that is the edit
    push bx
    push cx
    mov bx, 2
    xor cx, cx
    mov ax, [sc_cur]
    dec ax
    jmp short sc_fastcm
sc_fastokd:                         ; forward Delete: the edit is AT the caret,
    push ax                         ; and it takes a cell off the row below too
    push bx
    push cx
    mov bx, 3
    mov cx, 1
    mov ax, [sc_cur]
    jmp short sc_fastcm
sc_fastokm:                         ; Left: the caret lands one index back, so
    push ax                         ; that is the earliest cell that changes
    push bx
    push cx
    mov bx, 4                       ; a caret move is not an edit, and the
    xor cx, cx                      ; break is a thing you do to an EDIT
    mov ax, [sc_cur]
    dec ax
    jmp short sc_fastcm
sc_fastokr:                         ; Right: it lands one FORWARD, and the cell
    push ax                         ; it leaves is the one it is on now
    push bx
    push cx
    mov bx, 4
    xor cx, cx
    mov ax, [sc_cur]
sc_fastcm:
    ; STKBALANCE-OK: a shared CONTINUATION, not a routine - reached only by
    ; `jmp short` from the four cases above, each of which pushed what the
    ; tail here pops.
    mov word [sc_mvbot], 0x7FFF ; kind 4 arrives here as well (Left and Right),
                                ; and neither measures the row it came from -
                                ; so park the bound at "no idea" and let
                                ; sc_move be the only thing that ever sets it
    cmp byte [sc_ckok], 0
    je .out
    cmp ax, [sc_ckpi]
    jb .out
    mov [sc_fast], bl
    mov [sc_eext], cl
    mov ax, [sc_cur]
    sub ax, [sc_ckpi]
    mov [sc_ecol], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_onkey - W_ONKEY: edit the buffer, then repaint our own content
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing; preserves all registers
; Unhandled keys touch nothing; a full buffer drops the keystroke silently.
; -----------------------------------------------------------------------------
sc_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    call sc_mkey                    ; the menu system first (SPEC.md 68.2): an
    jc .out                         ; open dropdown or the About box is MODAL
                                    ; and consumes every key (Esc, arrows,
                                    ; Enter, mnemonics); with nothing open
                                    ; this claims only Alt+mnemonic, which
                                    ; opens a menu from the keyboard

%ifdef SCBENCH
    cmp al, 0x02                ; Ctrl-B - the walk bench; first, so nothing
    jne .nobench                ; below can shadow it in a -DWDBENCH build
    call wdb_run
    jmp .redraw
.nobench:
%endif
    or al, al
    jz .noctl                   ; an extended key carries no ascii, so none of
                                ; the control characters below can be one
    cmp al, SC_C_FIND
    je .kfind
    cmp al, SC_C_ESC
    je .kesc
    cmp al, SC_C_TAB
    je .ktab
    cmp al, SC_C_UNDO
    je .kundo
    cmp al, SC_C_SELALL
    je .kselall
    cmp al, SC_C_SAVE
    je .ksave
    cmp al, SC_C_BOLD               ; the formatting toggles (SPEC.md 68.3):
    je .kbold                       ; keys.cmd's Ctrl letters, less the three
    cmp al, SC_C_DUL                ; int 16h eats (Ctrl-H/I/M are BS/Tab/CR
    je .kdul                        ; - italic and hidden ride the Format
    cmp al, SC_C_SCAP               ; Character dialog instead)
    je .kscap
    cmp al, SC_C_UL
    je .kul
    cmp al, SC_C_WUL
    je .kwul
    ; --- the paragraph keys, keys.cmd's own letters (SPEC.md 68.3):
    ; C-L/C/R/J alignment, C-2 spacing (below, a NUL key), C-O/E open/close
    ; space, C-N indent, C-T hanging, C-G unhang, C-X reset
    cmp al, SC_C_LEFT
    je .kleft
    cmp al, SC_C_CENTER
    je .kcenter
    cmp al, SC_C_RIGHT
    je .kright
    cmp al, SC_C_JUST
    je .kjust
    cmp al, SC_C_OPENSP
    je .kopensp
    cmp al, SC_C_CLOSESP
    je .kclosesp
    cmp al, SC_C_INDENT
    je .kindent
    cmp al, SC_C_HANG
    je .khang
    cmp al, SC_C_UNHANG
    je .kunhang
    cmp al, SC_C_RSTP
    je .krstpara
.noctl:
    ; --- moving the caret: no edit, but the screen changes ------------------
    ; An EXTENDED key has AL = 0, and the gate matters: the numeric keypad
    ; sends '4' '6' '8' '2' '7' '1' '.' with exactly these scan codes, so
    ; without it NumLock would turn typing a digit into moving the caret.
    or al, al
    jnz .typing
    cmp ah, SC_K_F1
    je .kf1                         ; F1 - the About box (the help there is)
    cmp ah, SC_K_F4
    je .kf4                         ; F4 / Shift-F4 - repeat the search down
    cmp ah, SC_K_SF4                ; / up (SPEC.md 68.7)
    je .ksf4
    cmp ah, SC_K_F5
    je .kf5                         ; F5 - Go To... (keys.cmd EditGoTo)
    cmp ah, SC_K_F8
    je .kf8                         ; F8 - extend selection (status EXT)
    cmp ah, SC_K_LEFT
    je .left
    cmp ah, SC_K_RIGHT
    je .right
    cmp ah, SC_K_UP
    je .up
    cmp ah, SC_K_DOWN
    je .down
    cmp ah, SC_K_HOME
    je .home
    cmp ah, SC_K_END
    je .end
    cmp ah, SC_K_DEL
    je .del
    cmp ah, SC_K_INS
    je .eins                        ; Ctrl+Ins = Copy / Shift+Ins = Paste
                                    ; (keys.cmd; SPEC.md 68.3)
    cmp ah, SC_K_PGUP
    je .pgup
    cmp ah, SC_K_PGDN
    je .pgdn
    cmp ah, 0x0E
    je .kundo2                      ; Alt+BkSp arriving as a bare 0x0E scan
    cmp ah, 0x03
    je .kspc2                       ; Ctrl-2 is the NUL key: DoubleSpace
    cmp ah, 0x86
    je .kf12                        ; F12 = Save As (keys.cmd), when the
    cmp ah, 0x88                    ; keyboard path delivers the F11/F12
    je .ksave2                      ; scans at all
    cmp ah, 0x8A
    je .kf12o                       ; Ctrl+F12 = Open
    jmp .out                        ; an extended key nobody claims
.kundo2:
    jmp .kundo
.ksave2:
    jmp .ksave
.typing:
    cmp ah, SC_K_INS                ; NumLock hands '0'/'.' over with the
    je .insk                        ; Ins/Del scans: the CHORDS are told from
    cmp ah, SC_K_DEL                ; the typing by the live shift flags
    je .delk
.typ2:
    mov byte [sc_ext], 0            ; any edit disarms extend mode (65.2)
    cmp al, 8
    je .bksp
    cmp al, 13
    je .append
    cmp al, ' '
    jne .notsp
    push ax                         ; Ctrl-Space is ResetChar (keys.cmd), but
    push es                         ; int 16h hands it over as a plain Space -
    mov ax, 0x40                    ; the BIOS shift flags say whether a Ctrl
    mov es, ax                      ; is down (0040:0017 bit 2, the same byte
    mov al, [es:0x17]               ; the CAPS/NUM lamps read; SPEC.md 68.3)
    test al, 4
    pop es
    pop ax                          ; POP touches no flags
    jnz .krst
.notsp:
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    call sc_selkill                 ; typing REPLACES a selection (SPEC.md
    jnc .insplain                   ; 27.8) - in overtype too - and the two
                                    ; halves land in one undo group
    cmp byte [sc_ovr], 0            ; OVERTYPE (Ins, SPEC.md 68.2): the char
    je .insplain                    ; under the caret goes first - except a ¶
    push bx                         ; or the end, where overtype inserts
    push es
    mov bx, [sc_cur]
    cmp bx, [sc_len]
    jae .ovno
    mov es, [sc_dseg]
    cmp byte [es:bx], 13
    je .ovno
    pop es
    pop bx
    call sc_del                     ; recorded delete + recorded insert: the
    call sc_ins                     ; replace undoes (no fast path - the row
    jmp .edited                     ; changed in two places)
.ovno:
    pop es
    pop bx
.insplain:
    call sc_fastok                  ; a printable at the caret is THE case the
    jmp short .doins                ; incremental paths exist for (SPEC.md
                                    ; 27.3/27.4); Enter is not, and
.append:                            ; jumps in below sc_fastok
    call sc_selkill
    push ax                         ; the new ¶ inherits the paragraph it
    mov ax, [sc_cur]                ; SPLITS (Word's rule, SPEC.md 68.3): its
    call sc_papat                   ; CHP byte is a PAP INDEX, never the
    mov [sc_entpap], al             ; typing attributes
    pop ax
    mov dx, [sc_len]
    call sc_ins
    cmp dx, [sc_len]
    je .edited                      ; a full note dropped the keystroke
    push ax
    push bx
    push es
    mov bx, [sc_cur]
    dec bx
    mov es, [sc_cseg]
    mov al, [sc_entpap]
    mov [es:bx], al
    pop es
    pop bx
    pop ax
    jmp .edited
.doins:
    call sc_ins                     ; at the caret, which follows it
    jmp .edited

.bksp:
    push ax                         ; Alt+BkSp is EditUndo (keys.cmd) - the
    call sc_kbflags                 ; alt flag is the only thing that tells
    test al, 8                      ; the two apart
    pop ax
    jnz .kundo3
    call sc_selkill
    jnc .edited                     ; the selection WAS the deletion
    cmp word [sc_cur], 0
    je .out                         ; nothing to the left of the caret
    call sc_fastokb                 ; ...and so is a backspace, as long as the
    dec word [sc_cur]               ; character it eats is on the caret's own
    call sc_del                     ; row
    jmp .edited
.kundo3:
    jmp .kundo

.del:
    push ax                         ; Shift+Del is EditCut (keys.cmd)
    call sc_kbflags
    test al, 3
    pop ax
    jnz .kcut2
    mov byte [sc_ext], 0            ; an edit: extend mode ends
    call sc_selkill
    jnc .edited
    mov ax, [sc_cur]                ; forward delete: the caret stays put
    cmp ax, [sc_len]
    jae .out
    call sc_fastokd
    call sc_del
    jmp short .edited
.kcut2:
    jmp .kcut

.left:
    call sc_movpre
    cmp word [sc_cur], 0
    je .out
    call sc_fastokm                 ; a caret move is not an edit, but the row
    dec word [sc_cur]               ; above it still cannot have changed
    call sc_chpsync                 ; the typing attrs follow the caret
    jmp short .moved                ; (SPEC.md 68.3), on every plain move
.right:
    call sc_movpre
    mov ax, [sc_cur]
    cmp ax, [sc_len]
    jae .out
    call sc_fastokr
    inc word [sc_cur]
    call sc_chpsync
    jmp short .moved
.up:
    call sc_movpre
    mov dx, -1                      ; in ROWS now (SPEC.md 68.6)
    call sc_vmove
    call sc_chpsync
    jmp short .moved
.down:
    call sc_movpre
    mov dx, 1
    call sc_vmove
    call sc_chpsync
    jmp short .moved
.home:
    call sc_movpre
    xor dx, dx
    call sc_hmove
    call sc_chpsync
    jmp short .moved
.end:
    call sc_movpre
    mov dx, 0x7FFF
    call sc_hmove
    call sc_chpsync

.moved:
    call sc_extpost                 ; extend mode: the selection follows the
                                    ; caret from the banked anchor (65.2)
.edited:                        ; the toast is the kernel's and expires on
                                ; its own (SPEC.md 59), so a keystroke owes it
                                ; nothing at all - this used to be a store on
                                ; the hot path AND the reason sc_sigsame threw
                                ; the fast path away on the key after a save
    call OSAPI_GET_TICKS        ; ...and restarts the settle clock, which is
    mov [sc_ktick], ax          ; what the worker measures (SPEC.md 27.3)

.redraw:
    mov byte [sc_follow], 1         ; a KEY got us here, so wherever the caret
                                    ; ended up the view owes it a place on
                                    ; screen (SPEC.md 27.7). Set at the one
                                    ; label every handled key reaches, rather
                                    ; than in each of the twelve above it
    call sc_redraw                  ; SI still = window ptr

.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    ; --- the shortcuts (SPEC.md 27.8/68.7) ---------------------------------
    ; Reached only by the ladder at the top of this proc, which is why they
    ; sit past its `ret`: every one of them ends by jumping back into it.
.kfind:
    call sc_a_search                ; Ctrl-F: the modal Search dialog
    jmp .out                        ; (SPEC.md 68.7); no repaint owed - the
                                    ; dialog is on top of us
.kesc:
    mov byte [sc_ext], 0            ; Esc disarms extend mode (SPEC.md 68.2)
    call sc_selclr
    jc .redraw                      ; nothing selected: the tail still
                                    ; relights the EXT lamp
    mov byte [sc_ckok], 0
    call sc_chpsync                 ; the collapse leaves a bare caret: its
    jmp .redraw                     ; attrs are its neighbour's (SPEC.md 68.3)
.ktab:
    mov byte [sc_ext], 0
    mov al, 9                       ; a TAB is a CHARACTER (SPEC.md 68.3) -
    call sc_selkill                 ; it advances to the next default stop;
    call sc_ins                     ; sc_ins raises [sc_hastab] so the fast
    jmp .edited                     ; paths stand down before the redraw runs
.kcopy:
    call sc_uclose
    call sc_copy
    jmp .redraw
.kcut:
    call sc_uclose
    call sc_cut
    jmp short .kstamp
.kpaste:
    call sc_paste
    jmp short .kstamp
.kundo:
    call sc_undo
    jc .knoun
    call sc_chpsync                 ; the caret landed somewhere new
    jmp .kstamp
.knoun:
    mov ax, sc_m_noundo
    call sc_saymsg
    jmp .redraw
.kselall:
    call sc_uclose
    xor ax, ax
    mov dx, [sc_len]
    call sc_selset
    mov [sc_cur], dx
    mov byte [sc_ckok], 0
    jmp .redraw
.ksave:
    call sc_uclose
    call sc_save
    jmp .redraw
.kopen:
    call sc_a_open                  ; Ctrl+F12 = Open, through the dirty
    jmp .out                        ; prompt like the menu item (SPEC.md 68.4)
.kf1:
    call sc_habout                  ; F1: the About box (bounds+settle inside)
    jmp .out
.kf4:
    call sc_donext                  ; F4: repeat search down (SPEC.md 68.7)
    jmp .redraw
.ksf4:
    call sc_doprev                  ; Shift-F4: ...and up
    jmp .redraw
.kf5:
    call sc_a_goto                  ; F5: the Go To dialog (SPEC.md 68.7)
    jmp .out
.kf8:
    xor byte [sc_ext], 1            ; F8 toggles extend mode (SPEC.md 68.2);
    cmp byte [sc_ext], 0            ; arming banks the anchor so the first
    je .kf8off                      ; move has one to extend from
    cmp byte [sc_selon], 0
    jne .kf8off
    push ax
    mov ax, [sc_cur]
    mov [sc_anchor], ax
    pop ax
.kf8off:
    jmp .redraw                     ; nothing dirty: the tail relights EXT
.kstamp:
    mov byte [sc_ext], 0            ; cut/paste/undo are edits: extend ends
    call OSAPI_GET_TICKS            ; an EDIT, so the settle clock restarts -
    mov [sc_ktick], ax              ; but the toast it may have set stands,
    jmp .redraw                     ; which is why this is not .edited

    ; --- the formatting toggles (SPEC.md 68.3): five doors onto one routine,
    ; exactly as the ribbon cells and the dialog are
.kbold:
    mov al, SCAT_BOLD
    jmp short .kattr
.kdul:
    mov al, SCAT_DUL
    jmp short .kattr
.kscap:
    mov al, SCAT_SCAP
    jmp short .kattr
.kul:
    mov al, SCAT_UL
    jmp short .kattr
.kwul:
    mov al, SCAT_WUL
.kattr:
    call sc_applyattr               ; toggles the selection or the typing
    jmp .redraw                     ; attrs; the redraw's pass 1 finds the
                                    ; dirtied rows and its tail re-inverts
                                    ; the ribbon cell
.krst:
    xor al, al                      ; Ctrl-Space: ResetChar - everything back
    call sc_applyattr               ; to plain (SPEC.md 68.3)
    jmp .redraw

    ; --- the paragraph formats (SPEC.md 68.3): keys.cmd's letters, twin
    ; doors with the ruler cells and the Format Paragraph dialog onto
    ; sc_modpap - the one span modifier
.kleft:
    mov al, SCPO_ALIGN
    xor ah, ah
    jmp short .kpap
.kcenter:
    mov al, SCPO_ALIGN
    mov ah, 1
    jmp short .kpap
.kright:
    mov al, SCPO_ALIGN
    mov ah, 2
    jmp short .kpap
.kjust:
    mov al, SCPO_ALIGN
    mov ah, 3
    jmp short .kpap
.kspc2:
    mov al, SCPO_SPACE
    mov ah, 2
    jmp short .kpap
.kopensp:
    mov al, SCPO_SB
    mov ah, 1
    jmp short .kpap
.kclosesp:
    mov al, SCPO_SB
    xor ah, ah
    jmp short .kpap
.kindent:
    mov al, SCPO_ADDLEFT
    mov ah, 5                       ; +half an inch. UnIndent is Ctrl-M, which
    jmp short .kpap                 ; int 16h folds into Enter: it rides the
                                    ; dialog and the ruler markers instead
.khang:
    mov al, SCPO_HANG
    xor ah, ah
    jmp short .kpap
.kunhang:
    mov al, SCPO_UNHANG
    xor ah, ah
    jmp short .kpap
.krstpara:
    mov al, SCPO_RESET
    xor ah, ah
.kpap:
    call sc_uclose
    call sc_modpap
    jmp .redraw

    ; --- the Ins/Del chords and the pages -----------------------------------
.eins:                              ; extended Ins (AL = 0)
    push ax
    call sc_kbflags
    test al, 4
    jnz .einsc
    test al, 3
    jnz .einss
    pop ax
    xor byte [sc_ovr], 1            ; bare Ins: OVERTYPE toggles (SPEC.md
    jmp .redraw                     ; 65.2); nothing dirty, the tail's
                                    ; sc_stat relights the OVR lamp
.einsc:
    pop ax
    jmp .kcopy
.einss:
    pop ax
    jmp .kpaste
.insk:                              ; Ins scan WITH an ascii: NumLock's '0'
    push ax
    call sc_kbflags
    test al, 4
    jnz .einsc
    test al, 3
    jnz .einss
    pop ax
    jmp .typ2                       ; a plain numpad digit: type it
.delk:                              ; Del scan with an ascii: NumLock's '.'
    push ax
    call sc_kbflags
    test al, 3
    jnz .delkc
    pop ax
    jmp .typ2
.delkc:
    pop ax
    jmp .kcut
.pgup:
    call sc_caretpre
    mov ax, [sc_top]
    sub ax, [sc_vfit]
    call sc_scrollto
    call sc_redraw                  ; follow stays 0: a page turn is a view
    jmp .out                        ; move, not a caret move
.pgdn:
    call sc_caretpre
    mov ax, [sc_top]
    add ax, [sc_vfit]
    call sc_scrollto
    call sc_redraw
    jmp .out
.kf12:
    call sc_uclose                  ; F12 = Save As (keys.cmd)
    mov al, FDLG_SAVE
    call sc_dlgopen
    jmp .out
.kf12o:
    jmp .kopen                      ; Ctrl+F12 = Open

; -----------------------------------------------------------------------------
; sc_selkill - an edit is about to happen: if a selection is up, it goes
; out: CF = 1 there was none; preserves all registers
; -----------------------------------------------------------------------------
sc_selkill:
    call sc_seldel
    jc .out
    call sc_editinv
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; sc_caretpre - a caret key is about to run: end the edit group and drop the
;               selection
; out: nothing; preserves all registers
;
; Clearing [sc_ckok] when a selection actually went is the load-bearing half.
; The checkpoint lets sc_redraw resume its walk at the caret's own row, and a
; selection reaches rows ABOVE that - a resumed walk never re-signs them, so
; the inversion would stay drawn on a row nothing intends to redraw again.
; -----------------------------------------------------------------------------
sc_caretpre:
    call sc_uclose
    call sc_selclr
    jc .out
    mov byte [sc_ckok], 0
.out:
    ret

; -----------------------------------------------------------------------------
; sc_movpre - what a caret-motion key does before moving (SPEC.md 68.2)
; out: nothing; preserves all registers
; Plain motion collapses the selection (sc_caretpre); with F8's extend mode
; armed it KEEPS it, banking the anchor if none exists yet, so the move that
; follows has an end that does not move.
; -----------------------------------------------------------------------------
sc_movpre:
    cmp byte [sc_ext], 0
    je sc_caretpre
    call sc_uclose
    cmp byte [sc_selon], 0
    jne .out
    push ax
    mov ax, [sc_cur]
    mov [sc_anchor], ax
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_extpost - after the caret moved: extend the selection to it (SPEC.md 68.2)
; out: nothing; preserves all registers
; The anchored end is [sc_anchor]; the selection is the ordered pair, and a
; caret back ON the anchor is an empty selection, cleared. The signatures
; fold the selection bits, so the redraw finds every row the span crossed.
; -----------------------------------------------------------------------------
sc_extpost:
    cmp byte [sc_ext], 0
    je .out
    push ax
    push dx
    mov ax, [sc_anchor]
    mov dx, [sc_cur]
    cmp ax, dx
    je .clr
    jb .set
    xchg ax, dx
.set:
    call sc_selset
    mov byte [sc_ckok], 0
    jmp short .done
.clr:
    call sc_selclr
    jc .done
    mov byte [sc_ckok], 0
.done:
    pop dx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_redraw - repaint our own content from the buffer, redrawing only the rows
;             that actually moved (SPEC.md 27.2)
; in:  SI = window ptr (gfx lock held by the caller)
; out: nothing; preserves all registers
;
; The self-repaint every dispatch site shares. It exists as a routine rather
; than a tail of sc_onkey because the menu handler needs exactly the same
; steps - the kernel does not repaint after a command returns (SPEC.md 12.2),
; so every command that changes the buffer has to end here.
;
; Two walks: one to measure and compare, one to draw the band the first found.
; Typing one character usually dirties exactly one row, and the erase is then
; a fill 8 pixels tall instead of the whole content. When nothing on screen
; changed - an arrow key that hit the end of the note, a keystroke a full
; buffer dropped - it draws nothing at all and returns.
;
; sc_sigsame decides whether that is legal: a resize or a toast coming and
; going means the stored signatures no longer describe what is on screen, and
; then this is the old routine unchanged - fill the content whole, sc_paint
; over it, put the grow box back because the fill just erased it.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sc_append - a printable typed at the end of the note, drawn as ONE GLYPH
; in:  SI = window ptr; gfx lock held; sc_bounds and sc_sigsame have run and
;      agreed; [sc_ekind] is the kind sc_redraw consumed
; out: CF = 0 it drew and sc_redraw owes nothing else; CF = 1 not this case.
;      Preserves every register.
;
; SPEC.md 27.14. Every optimisation before this one made the WALK shorter -
; 27.4 from the note to the caret's row, 27.11.1 by the row word wrap's
; lookahead was forcing on top. This one does not walk at all, because for the
; commonest keystroke in the editor there is nothing to discover: the caret is
; at the end of the note, so nothing follows it to reflow, and nothing above
; its row can be touched by a character typed below them.
;
; THE SIGNATURE IS WHAT MAKES IT POSSIBLE, and it is the part that looks like
; it should not work. sc_fold is a rolling `rol 1, add` over the row's cells in
; order, and sc_ask folds the CARET in at its own position - so with the caret
; at the end of the row the last two things folded are the caret and nothing
; else. That is invertible in four operations:
;
;     rol(B,1) = h - caret(C)                 undo the caret at column C
;     h' = rol(rol(B,1) + ch, 1) + caret(C+1) fold the character where it was,
;                                             then the caret after it
;
; so sc_sig stays exact and the NEXT keystroke's sc_sigsame still agrees. A
; fast path that left the signature stale would win once and pay for it on the
; keystroke after, which is how this was nearly built wrong.
;
; THE WRAP IS DEFERRED, DELIBERATELY. sc_wordfit is asked at a word's first
; character and nowhere else, so appending to a word already committed to this
; row cannot move it - but a fresh layout WOULD ask again with the longer word
; and might break earlier. So the screen can be a wrap behind the note while
; the keys are still coming, exactly as 27.3's visual break is a line break
; ahead of it, and [sc_sowed] is the debt: the worker already spends it with a
; full sc_redraw, and only after SC_IDLE ticks without a keystroke. Nothing new
; had to be hired - sc_ins raises sc_hmark, which is what wakes the worker at
; all, and 27.7.3's height recount and this reconcile are the same settle.
; -----------------------------------------------------------------------------
sc_append:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [sc_ekind], 1          ; a printable insert at the caret and
    jne .no                         ; nothing else: a Delete moves the tail, a
    cmp byte [sc_bmode], 0          ; caret move draws two rows, and the break
    jne .no                         ; owns the screen while it is up
    cmp byte [sc_ckok], 0
    je .no                          ; no checkpoint: we do not know the row
    cmp byte [sc_selon], 0
    jne .no                         ; a selection folds into the row's cells
    cmp byte [sc_chp], 0
    jne .no                         ; only a PLAIN character (SPEC.md 68.1):
                                    ; a styled one needs the run drawers, and
                                    ; the signature patch below assumes the
                                    ; attribute fold added nothing
    cmp byte [sc_hashid], 0
    jne .no                         ; hidden characters break the column
                                    ; arithmetic below (a hidden char is an
                                    ; index with no cell) - walk properly
    cmp byte [sc_hastab], 0
    jne .no                         ; a tab is an index with SEVERAL cells:
                                    ; same refusal (SPEC.md 68.3)
    cmp byte [sc_hasfmt], 0
    jne .no                         ; formatted rows have indents, offsets and
                                    ; heights this path's x/y arithmetic does
                                    ; not model - walk properly (SPEC.md 68.6)
    cmp byte [sc_pxon], 0
    jne .no                         ; ...AND A CHOSEN FACE (SPEC.md 68.13.1).
                                    ; This path does not walk, so it does not
                                    ; compose a band: it stamps the glyph with
                                    ; OSAPI_FONT_RUN, which is the KERNEL's 8x8
                                    ; cell and nothing else. Every guard above
                                    ; is about geometry, and a face changes the
                                    ; GLYPHS - tallx is 8 rows at an advance of
                                    ; 8 and raises none of them, so a character
                                    ; typed at the end of a line came out in
                                    ; Pica while every character the walk
                                    ; redrew came out in the face. That is the
                                    ; bug this section was reported as

    ; NOTHING AFTER THE CARET ON THIS ROW, which is the whole precondition and
    ; has exactly two shapes (SPEC.md 27.14.1). The end of the NOTE, where
    ; there is no tail at all; or a HARD NEWLINE at the caret, where the tail
    ; exists but is on other rows and none of its characters or its layout
    ; moves. A wrapped row's end is NOT one of them and cannot be: sc_ask fires
    ; at the settled pen, so the index after the last character of a wrapped
    ; row is reported at column 0 of the row BELOW - that caret is on the next
    ; row, and a character typed there joins that row's first word, which is
    ; the one thing 27.11.1 says can move a break.
    mov byte [sc_aprow], 0
    mov ax, [sc_cur]
    cmp ax, [sc_len]
    je .tailok
    mov es, [sc_dseg]
    mov di, ax
    cmp byte [es:di], 13
    jne .no
    mov byte [sc_aprow], 1          ; ...and rows BELOW start one index later
.tailok:
    or ax, ax
    jz .no
    dec ax                          ; AX = where the character landed
    mov bx, ax
    sub bx, [sc_ckpi]
    js .no                          ; before the row start: not our row at all
    mov cx, [sc_rcols]
    dec cx
    cmp bx, cx
    jae .no                         ; the cell rule is about to wrap this row,
                                    ; or the caret would land off the end of
                                    ; it - either way, walk properly. BX = the
                                    ; column the character occupies
    mov dx, [sc_ckpr]
    or dx, dx
    js .no
    cmp dx, [sc_vrows]
    jae .no                         ; off the view: nothing to draw
    cmp dx, [sc_prowi]
    jne .no                         ; THE EXISTING GATE: sc_prow describes some
                                    ; other row, so patching it would make the
                                    ; delta cache describe a row that is not on
                                    ; screen

    mov es, [sc_dseg]
    mov di, ax
    mov al, [es:di]                 ; the character itself
    cmp al, ' '
    jb .no                          ; a newline ends a row and is not a glyph
    mov [sc_ap1], al
    mov byte [sc_ap1+1], 0
    mov [sc_apch], al

    mov ax, bx                      ; --- x of the cell, y of the row ---------
    mov cl, 3
    shl ax, cl
    add ax, [sc_tx]
    mov [sc_apx], ax                ; x of column C
    mov ax, dx
    mov cl, 3
    shl ax, cl
    add ax, [sc_ty]
    mov [sc_apy], ax

    mov cx, [sc_apx]                ; --- the glyph, opaque, one cell ---------
    mov dx, [sc_apy]                ; opaque is what erases the caret bar that
    mov si, sc_ap1                  ; is standing on this very cell
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov ax, [sc_apx]                ; --- and the caret, one cell along -------
    add ax, 8
    mov [sc_apx2], ax
    mov bx, [sc_apy]
    mov dx, bx
    add dx, 7
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE

    ; --- and now the state the next keystroke reads --------------------------
    mov bx, [sc_cur]                ; the row cache: cell C was a space, and
    dec bx                          ; the screen shows the character now
    sub bx, [sc_ckpi]
    mov al, [sc_apch]
    mov [sc_prow+bx], al
    mov byte [sc_pattr+bx], 0       ; the cell is plain by the gate above, and
                                    ; the attr cache must say what the diff
                                    ; will re-ask (SPEC.md 68.1)
    inc bx
    mov [sc_prcc], bx               ; ...and its caret is one along

    mov bx, [sc_ckpr]               ; the signature, patched rather than walked
    shl bx, 1                       ; (see the header)
    mov ax, [sc_apx]
    xor ax, 0x5A5A
    mov dx, [bx+sc_sig]
    sub dx, ax                      ; undo the caret that was folded at C
    xor ah, ah
    mov al, [sc_apch]
    add dx, ax                      ; the character folds where it stood
    rol dx, 1                       ; ...then its CHP byte - zero by the gate
    rol dx, 1                       ; above, so its fold is the rotate alone
    mov ax, [sc_apx2]               ; (SPEC.md 68.1)
    xor ax, 0x5A5A
    add dx, ax                      ; ...and the caret after it
    mov [bx+sc_sig], dx

    mov ax, [sc_apx2]               ; sc_seecaret and every hit test read these
    mov [sc_curx], ax
    mov ax, [sc_apy]
    mov [sc_cury], ax
    mov byte [sc_curseen], 1

    ; EVERY ROW BELOW STARTS ONE INDEX LATER (SPEC.md 27.14.1). Their
    ; characters and their layout are untouched - the pixels below the caret's
    ; row are still right, which is what makes this case legal at all - but
    ; sc_rows is a table of INDICES and a character was inserted in front of
    ; all of them. Sixteen words at worst, against the walk this exists to
    ; skip. The end-of-note case has no rows below and does none of it.
    cmp byte [sc_aprow], 0
    je .norows
    cmp byte [sc_rowsok], 0
    je .norows
    mov cx, [sc_rowsn]
    cmp cx, SC_MAXROWS              ; [sc_rowsn] is not capped to the array it
    jbe .rok                        ; indexes (docs/NOTEPAD-NOTES.md 5.3.1), so
    mov cx, SC_MAXROWS              ; this caller clamps like sc_seedtail does
.rok:
    mov bx, [sc_ckpr]
    inc bx
.rsh:
    cmp bx, cx
    jae .norows
    push bx
    shl bx, 1
    inc word [bx+sc_rows]
    pop bx
    inc bx
    jmp short .rsh
.norows:

    mov byte [sc_sowed], 1          ; THE RECONCILE: the worker spends this with
                                    ; a full sc_redraw, and only once the keys
                                    ; have stopped for SC_IDLE ticks
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sc_redraw:
    push ax
    push bx
    push cx
    push dx
    call sc_bounds
    mov al, [sc_fast]               ; ONE-SHOT: whoever set it meant this
    mov byte [sc_fast], 0           ; redraw and no other
    mov [sc_ekind], al
    mov byte [sc_resume], 0
    cmp byte [sc_bmode], 0
    je .normal
    or al, al
    jz .settle                      ; the break survives an insert and a
    cmp al, 3                       ; backspace and NOTHING else (SPEC.md
    jae .settle                     ; 27.3): the tail is not redrawn while it
                                    ; is up, so Right would draw a character
                                    ; twice, Left would lose one, and Delete
                                    ; eats exactly the tail's first character
    call sc_sigsame                 ; ...and a resize or a toast is not typing
    jc .settle                      ; either
    call sc_brkdraw
    jmp .out
.settle:
    call sc_reconcile
    jmp .out

.normal:
    push ax
    call sc_sigsame
    pop ax
    jc .full
    push ax                         ; the screen shows [sc_ptop] and the view
    mov ax, [sc_top]                ; may already have moved - a scroll bar
    cmp ax, [sc_ptop]               ; click scrolls and THEN redraws. Reconcile
    pop ax                          ; the pixels first: everything below this
    jne .scrolled0                  ; indexes an array by a VISIBLE row, and
                                    ; those still count in the old view

    call sc_append                  ; ONE GLYPH AND NO WALK (SPEC.md 27.14),
    jnc .done                       ; for a printable typed at the end of the
                                    ; note - which is most of typing. Here
                                    ; rather than earlier because it needs
                                    ; sc_sigsame to have agreed and [sc_ptop]
                                    ; to be [sc_top]: it patches the row cache
                                    ; and the signature, and both describe a
                                    ; screen drawn for THIS geometry and THIS
                                    ; view. AL still holds [sc_ekind] below -
                                    ; sc_append preserves every register
    or al, al
    jz .noseed
    call sc_seedck                  ; only an edit at the caret may skip the
.noseed:                            ; rows above it (SPEC.md 27.4)
    cmp byte [sc_resume], 0         ; ...and failing that, the top of the VIEW
    jne .seeded1                    ; is a seed too: sc_rows[0] is the index
    cmp byte [sc_rowsok], 0         ; row 0 of the content starts at, rows
    je .xseed1                      ; above it have neither pixels nor
    xor ax, ax                      ; signatures, and nothing above the caret
    mov dx, [sc_vrows]              ; can have reflowed anyway - which is the
    call sc_seedrow                 ; same claim 27.4 already makes, applied
    cmp byte [sc_resume], 0
    jne .seeded1
.xseed1:
    mov ax, [sc_top]                ; ...and when sc_rows cannot - which is
    mov dx, [sc_vrows]              ; after EVERY scroll, because sc_scrollto
    call sc_xseed                   ; drops it - the row index still describes
                                    ; the view's top row (SPEC.md 27.13).
                                    ; Without this, pass 1 of the redraw after
                                    ; a scroll laid the note out from index 0
                                    ; to reach the row the view starts on: 240
                                    ; ms of a 640 ms Up at [sc_top] = 5, and
                                    ; growing with the depth of the view
.seeded1:                           ; from a HIGHER row and so a weaker one.
                                    ; Both die together: sc_scrollto,
                                    ; sc_bounds and sc_clamp clear [sc_ckok]
                                    ; and [sc_rowsok] side by side

    mov word [sc_hity], 0xFFFF      ; pass 1: no queries, no drawing - just
    mov word [sc_wanty], 0x7FFF     ; which rows stopped matching
    mov word [sc_dr0], 0xFFFF
    mov word [sc_dr1], 0
    mov byte [sc_ymoved], 0         ; ...and which rows MOVED (SPEC.md 68.6):
    mov word [sc_ymv0], 0x7FFF      ; a height change shifts every row below
    mov word [sc_ymv1], 0           ; it without changing a character
    mov byte [sc_draw], 0
    mov byte [sc_sigup], 1
    mov byte [sc_clip], 0
    mov ax, [sc_vrows]              ; STOP at the bottom of the view, plus the
                                    ; one row past it a caret can wrap onto
                                    ; (SPEC.md 27.7). Below that a row has no
                                    ; signature, cannot be dirty and is drawn
                                    ; by nobody - the only thing that ever
                                    ; wanted it was the note's total height,
                                    ; and sc_height owns that now. Typing in
                                    ; the middle of a long note used to walk
                                    ; every row beneath the window on every
                                    ; keystroke: 72% of the work, for a thumb

    ; ...AND A CARET MOVE STOPS SOONER STILL (SPEC.md 27.4.1). Nothing reflowed,
    ; so the only rows whose signatures can differ are the one the caret left
    ; and the one it arrived on, and sc_move recorded the deeper of them. The
    ; rest of the view is laid out to be told it did not move: (vrows - row) x
    ; ~6 ms, which is ~96 ms of the budget with the caret near the top.
    ;
    ; Gated on the walk actually RESUMING, and that is not caution about the
    ; bound - it is about [sc_rowsn]. sc_walk's bounded stop leaves the table
    ; alone for a resumed walk and SHRINKS it to where it stopped for one that
    ; started at the top of the view, which would hand rows this walk skipped
    ; back to SPEC.md 27.13's index for no reason. Resumed is the normal case
    ; here anyway: sc_seedck seeds at the earlier of the two rows.
    cmp byte [sc_ekind], 4
    jne .p1bound
    cmp byte [sc_resume], 0
    je .p1bound
    mov dx, [sc_mvbot]
    or dx, dx
    js .p1bound                     ; above the view: let the caret-follow net
    cmp dx, ax                      ; have it, exactly as before
    jae .p1bound                    ; never DEEPER than the view: the sentinel
    mov ax, dx                      ; lands here, and so does a caret one row
.p1bound:                           ; below the last visible one
    mov [sc_lastrow], ax
    call sc_walk
    cmp byte [sc_follow], 0         ; the caret has to be somewhere the user
    je .noflw                       ; can see it (SPEC.md 27.7) - but only
    cmp byte [sc_curseen], 0        ; ...and the walk above may have stopped
    jne .haveit                     ; short of the caret, in which case
                                    ; [sc_cury] is still the initial 0 and
                                    ; following it would scroll somewhere
                                    ; arbitrary. Walk again FROM INDEX 0 and
                                    ; UNBOUNDED, which is the whole point of
                                    ; this net: the seed is what let the walk
                                    ; miss the caret and the bound is what
                                    ; made it missable, so a net carrying
                                    ; either finds nothing too. sc_measure
                                    ; clears neither - sc_vmove and sc_onclick
                                    ; set them on purpose - so this does.
                                    ; The case is real and not theoretical:
                                    ; page the view away with the bar and then
                                    ; press a key, and the caret is a whole
                                    ; screenful below the last row walked
    mov word [sc_lastrow], 0x7FFF   ; describes, when that row begins at or
    mov ax, [sc_cur]                ; before the caret. THE ROW INDEX ANSWERS
    call sc_xseedi                  ; THIS DIRECTLY (SPEC.md 27.13) and it is
    jnc .netok                      ; the case that mattered: a caret ABOVE the
                                    ; view qualifies no row sc_rows describes,
                                    ; so sc_netseed walked back to row 0, found
                                    ; nothing and left the walk to lay out the
                                    ; whole note - 5.2 s on every Up out of the
                                    ; top of the view. sc_xseedi seeds within a
                                    ; stride of the caret AND sets the bound,
                                    ; because a later checkpoint proves which
                                    ; row the caret's row is above
    call sc_netseed             ; ...FORWARD from the deepest row the table
.netok:
    call sc_measure             ; before the caret. Unbounded still - the
                                ; caret's row is not known, which is the whole
                                ; problem - but not from INDEX 0: Down on the
                                ; bottom visible row puts the caret one row
                                ; below the view and re-walked the entire note
                                ; to find it, which is seconds on the most
                                ; used key there is (docs/NOTEPAD-NOTES.md 1.4)
.haveit:
    call sc_seecaret                ; when it MOVED. A scroll bar click also
    jnc .scrolled                   ; ends here, and following the caret then
.noflw:                             ; would drag the view straight back to it
                                    ; and make the bar look broken. Moving
                                    ; the view renames every row the band and
                                    ; the signatures are counted in - so that
                                    ; is a full repaint, not a band
    mov ax, [sc_dr0]
    cmp ax, 0xFFFF
    je .done                        ; not one pixel of the text moved

    mov al, [sc_ekind]              ; would this reflow cost more than pushing
    or al, al                       ; the note down a row? (SPEC.md 27.3)
    jz .band                        ; Every EDIT at the caret qualifies -
    cmp al, 4                       ; insert, Backspace and Delete alike -
    jae .band                       ; because sc_fastok* REPORTED the caret's
                                    ; column rather than leaving this to work
                                    ; it out from where the caret ended up. A
                                    ; caret move does not: nothing reflowed,
                                    ; so there is nothing to avoid drawing
    cmp byte [sc_brkok], 0
    je .band
    call sc_brktry
    jnc .done                       ; it took over, and it drew
.band:
    mov ax, [sc_dr0]                ; reloaded: sc_brktry is free with AX

    ; the band's pixel edges: arithmetic while uniform; from the banked ys -
    ; which pass 1 has just rewritten - under formats (SPEC.md 68.6)
    cmp byte [sc_hasfmt], 0
    je .bxy8
    or ax, ax
    jz .bty
    mov bx, ax                      ; y1 = bandtop(dr0) = ryb[dr0-1] + 8
    dec bx
    shl bx, 1
    mov bx, [bx+sc_ryb]
    add bx, 8
    jmp short .by1
.bty:
    mov bx, [sc_ty]
.by1:
    mov dx, [sc_dr1]                ; y2 = dr1's glyph bottom, or the content
    cmp dx, [sc_vrows]              ; bottom when the range runs past the view
    jae .bybot
    shl dx, 1
    push bx
    mov bx, dx
    mov dx, [bx+sc_ryb]
    pop bx
    add dx, 7
    cmp dx, [sc_bot]
    jbe .bhave
.bybot:
    mov dx, [sc_bot]
    jmp short .bhave
.bxy8:
    mov bx, ax                      ; y1 = sc_ty + 8*dr0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [sc_ty]
    mov dx, [sc_dr1]                ; y2 = sc_ty + 8*dr1 + 7
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [sc_ty]
    add dx, 7
.bhave:
    ; rows MOVED: erase from the highest pixel a moved row owned or owns
    ; down to the content bottom, and redraw EVERY row from there - a glyph
    ; run only self-erases at its own y, and a row that shifted leaves its
    ; old pixels standing (SPEC.md 68.6). The fill makes the band clean, so
    ; the runs draw trimmed, exactly the full-repaint discipline; the
    ; to-the-bottom sweep is the honest cost of pixels that all moved.
    cmp byte [sc_ymoved], 0
    je .noymv
    mov ax, [sc_ymv0]
    cmp ax, [sc_ty]
    jae .ym0
    mov ax, [sc_ty]
.ym0:
    cmp ax, bx
    jae .ym1
    mov bx, ax
.ym1:
    mov dx, [sc_bot]
    mov ax, [sc_vrows]              ; ...and pass 2 walks to the view's last
    dec ax                          ; row: rows below the moved ones need
    cmp ax, [sc_dr1]                ; their (unchanged) pixels REDRAWN at
    jbe .ym3                        ; their (unchanged) ys inside the sweep
    mov [sc_dr1], ax
.ym3:
    push bx
    push dx
    mov ax, [sc_tx]
    sub ax, SC_MARGIN
    mov cx, [sc_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop dx
    pop bx
    mov byte [sc_clean], 1
    mov word [sc_prowi], 0xFFFF
.noymv:
    ; The band fill is GONE. It used to erase dr0..dr1 whole and pass 2 then
    ; lettered it, which is the erase-and-letter pair - and on a 4.77MHz 8088
    ; that leaves the line blank for several display frames, so every keystroke
    ; flickered (SPEC.md 6.1). sc_rflush draws each row as one opaque font_run
    ; instead: the padding erases and the glyphs land in the same write, and no
    ; cell is ever momentarily blank.
    ;
    ; What the run does NOT reach is the two margins - the inset left of the
    ; pen, and whatever is left of the band right of the last whole cell. They
    ; are still fills, and they carry no glyphs, so they cannot flicker and
    ; cannot disagree with anything at a clip edge.
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    push bx
    push dx
    mov ax, [sc_tx]                 ; left: the margin, if there is one
    sub ax, SC_MARGIN
    mov cx, [sc_tx]
    dec cx
    cmp ax, cx
    jg .mr
    call OSAPI_GFX_FILL
.mr:
    cmp byte [sc_pxon], 0           ; right: the <8px tail past the last cell.
    jne .mdone                      ; A CHOSEN FACE HAS NO SUCH TAIL - its rows
                                    ; end where their glyphs do and each one
                                    ; erases its own in sc_rflush (SPEC.md
                                    ; 65.13), and [sc_rcols] there is sized from
                                    ; TY_MINADV, so 8 x it is not a pixel count
                                    ; at all. It happened to fall past [sc_rgt]
                                    ; and be skipped, which is not a thing to
                                    ; leave standing on
    mov ax, [sc_rcols]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_tx]
    mov cx, [sc_rgt]
    cmp ax, cx
    jg .mdone
    call OSAPI_GFX_FILL
.mdone:
    pop dx
    pop bx

    mov byte [sc_draw], 1           ; pass 2: draw, and only inside it - and
    mov byte [sc_sigup], 0          ; STOP at it, because pass 1 already knows
    mov byte [sc_clip], 1           ; no row below sc_dr1 changed. An arrow key
    mov ax, [sc_dr1]                ; dirties two rows and used to lay out the
    mov [sc_lastrow], ax            ; whole note behind them to draw them
    call sc_walk
    mov byte [sc_clip], 0
    mov byte [sc_clean], 0          ; the moved-rows erase may have set it

    ; THE GROW BOX IS NOT REDRAWN HERE AT ALL, because nothing on this path
    ; can reach it (SPEC.md 27.2.1). It was unconditional once, and it HAD to
    ; be: the band fill spanned the full content width, so any dirty row level
    ; with the box erased it. Then it became a row test - [sc_bandb] against
    ; sc_bot-12 - and that was still wrong in the expensive direction, because
    ; typing on the BOTTOM VISIBLE ROW satisfies it on every keystroke, which
    ; is exactly where the caret sits while a page is being filled. Measured:
    ; wm_grow_paint plus three gfx_frames and eighteen fills and lines, ~12 ms
    ; of a 52 ms keystroke - and the flicker in the corner that making it
    ; conditional was supposed to stop.
    ;
    ; The box is 13x13 at (sc_sbr-12, sc_bot-12) and sc_bounds reserves that
    ; corner in BOTH dimensions: [sc_rgt] is sc_sbr-SC_SB_W, two pixels short
    ; of its left edge, and [sc_sbb] is sc_bot-SC_GROW, one row above its top.
    ; So the text runs, the two margin fills and the scroll bar all stop clear
    ; of it. The row test was reading the band's rows and never asked about its
    ; COLUMNS. Every other OSAPI_WM_GROW in this module follows something that
    ; genuinely reaches the corner - a full-content fill, or a band scroll that
    ; drags it - and those all stay.
    call sc_sbcheck                 ; a note that gained or lost a row moves
                                    ; the thumb, and nothing else redraws it
                                    ; on this path
.done:
    mov byte [sc_resume], 0
    jmp short .out

.scrolled0:
    mov word [sc_dr0], 0xFFFF       ; no walk has run this redraw, so nothing
    mov word [sc_dr1], 0            ; is known dirty beyond the exposed rows
.scrolled:
    ; The view moved. Move the PIXELS to match instead of drawing them again
    ; (SPEC.md 27.7.2) - and if that is refused, the full repaint below is
    ; exactly what used to happen every time.
    call sc_scrollpaint
    jnc .done
    jmp short .fullpaint

.full:
    ; Reached when sc_sigsame REFUSED - a resize, a toast arriving or leaving,
    ; an uncover - so nothing above has measured anything, and both numbers
    ; the view is clamped by may have changed: a wider window wraps into fewer
    ; rows. Measure, put the view back inside a note that may now be shorter
    ; than where it was looking, and only then follow the caret.
    call sc_measure
    mov ax, [sc_top]
    call sc_scrollto
    cmp byte [sc_follow], 0
    je .fullpaint
    call sc_measure                 ; measured AGAIN because sc_scrollto
    call sc_seecaret                ; renames every row [sc_cury] was counted
                                    ; in. Same gate as the band path: a bar
                                    ; click must not have its scroll undone
.fullpaint:
    ; ...and reached DIRECTLY from the scroll above, which is the common case
    ; and was paying for this block having no way to know that. A caret that
    ; has just been followed is in view by construction - sc_seecaret's target
    ; is the exact row, not a step towards it - so re-measuring in order to
    ; ask the same question again cost two full walks per keystroke and could
    ; never answer differently. Every Up that scrolled, and every character
    ; typed with the view already trailing the caret, paid it.
    mov word [sc_prowi], 0xFFFF     ; the delta cache describes the SCREEN, and
                                    ; the screen is about to be filled over.
                                    ; Every path that disturbs it other than
                                    ; our own row draws lands here - a resize,
                                    ; a toast arriving or leaving, an uncover -
                                    ; because that is what sc_sigsame is for
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = x1, DX = y1
    push ax
    push dx
    call OSAPI_WM_GEOM              ; CX/DX = content w/h
    pop ax                          ; y1
    add dx, ax
    dec dx                          ; DX = y2
    mov bx, ax                      ; BX = y1
    pop ax                          ; x1
    add cx, ax
    dec cx                          ; CX = x2
    push ax                         ; the pen is a register here, not a
    mov al, CWHITE                  ; variable - keep x1 across the call
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL             ; white-fill the content
    call sc_paint                   ; SI still = window ptr
    mov bx, si                      ; the white fill erased the grow box;
    call OSAPI_WM_GROW              ; restore it (SPEC.md 11.1/27)
.out:
    mov byte [sc_ymoved], 0         ; spent: it described THIS redraw's pass 1
                                    ; (sc_scrollpaint tests it, and a stale 1
                                    ; would refuse a later good blit)
    call sc_hirechk                 ; a debt left by ANY of this routine's
                                    ; exits, not just the .done path it used to
                                    ; hang off - .fullpaint fell straight past
                                    ; that one (SPEC.md 27.7.3)
    call sc_selmark                 ; the screen shows this selection now
    mov byte [sc_selonly], 0        ; ONE-SHOT: whoever set it meant THIS
                                    ; redraw, and the next one may well be a
                                    ; keystroke that moves characters
    mov byte [sc_follow], 0         ; ONE-SHOT, like [sc_fast]: whoever set it
                                    ; meant this redraw and no other, and the
                                    ; next one may well be a scroll bar click
    call sc_stat                    ; the status line follows the caret
                                    ; (SPEC.md 68.2): delta-cached, so a
                                    ; keystroke that moved neither Ln nor Col
                                    ; draws not a cell of it
    call sc_rbstat                  ; ...and the ribbon's pressed cells follow
                                    ; the caret's attributes, same delta rule
                                    ; (SPEC.md 68.3)
    call sc_rlstat                  ; ...and the ruler's cells and markers
                                    ; follow the caret's PARAGRAPH, same rule
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_new - empty the note (File > New)
; in:  nothing
; out: nothing; preserves all registers
;
; The length, the toast and the claim. sc_paint reads exactly [sc_len] bytes,
; so the stale tail is unreachable and wiping it would buy nothing - but the
; claim it sat in is real memory, and a note that grew to SC_MAXKB has no
; business holding eight kilobytes of heap after the user emptied it. The
; toast goes because "Loaded DOCUMENT.DOC" over an empty note is a lie - the same
; reason an ordinary keystroke retires it.
; -----------------------------------------------------------------------------
sc_new:
    call sc_pictfree                ; the pictures go with the document. Their
                                    ; pixels are in claims of their own, and a
                                    ; table that outlives its text describes
                                    ; characters that are not there (88.9)
    mov word [sc_len], 0
    mov word [sc_cur], 0
    mov ax, sc_s_nul            ; retire the toast: 'Loaded DOCUMENT.DOC' over an
    call sc_saymsg              ; empty note is a lie. An EMPTY string is how
    call sc_clamp               ; SPEC.md 59.3 spells that, so no flag of ours
               ; a no-op on the caret, which is already 0 -
                                ; it is here for the invalidation sc_clamp
                                ; carries (SPEC.md 27.4/27.5)
    push ax                     ; ...and give the heap back what the old note
    mov ax, SC_KB0              ; had grown into. A shrink always succeeds in
    call sc_resize              ; place, so this cannot fail (SPEC.md 50.3.1)
    pop ax
    mov byte [sc_dirty], 0      ; a fresh document matches nothing on disk
                                ; and owes no prompt (SPEC.md 68.4)
    call sc_defname             ; a new note is a new document: leaving the
                                ; old name would make the next Ctrl-S overwrite
                                ; the file the user just walked away from
    jmp sc_settitle             ; ...and the caption follows it (65.2)

; -----------------------------------------------------------------------------
; sc_dlgopen - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The current document is handed over as the default, so Save As on a note
; loaded from LETTER.TXT opens with LETTER.TXT already in the box. A refusal
; (CF=1: one is already up) is silently nothing - the dialog the user
; already has IS the answer to the command they just picked.
; -----------------------------------------------------------------------------
sc_dlgopen:
    push bx
    push si
    push di
    mov bx, si                      ; the window we want to hear back about
    mov di, sc_ondlg
    mov si, sc_name
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_ondlg - the file dialog's completion callback (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, DI = the chosen name;
;      UI task, gfx lock HELD, the dialog window already destroyed
; out: nothing; no register need be preserved (the kernel saved its own)
;
; One proc for both commands, because the only difference between them is
; which way the bytes move afterwards. It must repaint: the kernel does not
; repaint after a callback returns, and the window under the dialog has just
; been uncovered by wm_destroy.
; -----------------------------------------------------------------------------
sc_ondlg:
    cmp byte [sc_pictwant], 0       ; INSERT > PICTURE BORROWS THIS DIALOG,
    je .nobank                      ; AND THE DIALOG RENAMES THE DOCUMENT.
    push si                         ; The copy below puts the chosen name in
    push di                         ; sc_name unconditionally, so picking a
    push cx                         ; picture renamed the document to it -
    mov si, sc_name                 ; and nothing recomposed the title, so the
    mov di, sc_namebank             ; bar went on showing the OLD name while
    mov cx, SC_NAMEMAX + 1          ; Save wrote to the new one. Choosing a
.bank:                              ; .BMP therefore overwrote that .BMP with
    mov al, [si]                    ; the document, in whatever format the
    mov [di], al                    ; PICTURE's extension implied. It is
    inc si                          ; banked here and put back below.
    inc di
    dec cx
    jnz .bank
    pop cx
    pop di
    pop si
.nobank:
    mov [sc_fsz], cx                ; the file's size from the LISTING (38.6),
    mov [sc_fszh], dx               ; banked FIRST - the open gate reads it
                                    ; BEFORE any disk I/O (SPEC.md 68.4)
    mov bl, al                      ; BL = the mode; AL becomes a name byte
    mov dx, si                      ; DX = our window: SI is about to be the
                                    ; kernel's buffer, and sc_redraw wants
                                    ; the window back in SI
    mov si, di
    mov di, sc_name
    mov cx, SC_NAMEMAX
.copy:
    mov al, [es:si]                 ; the name buffer is the KERNEL's, and ES
    mov [di], al                    ; points there on entry (SPEC.md 38.6).
    or al, al                       ; Bounded even though 38.6 promises <= 12:
    jz .copied                      ; a package that trusts a promise is a
    inc si                          ; package with an overrun in it
    inc di
    loop .copy
    mov byte [di], 0
.copied:
    or bl, bl                       ; Save As: an extensionless typed name
    jz .noext                       ; gets .DOC appended (SPEC.md 68.4)
    push bx
    push di
    mov di, sc_name
    xor bx, bx                      ; BX = characters, BH != 0 = a dot seen
.scan:
    mov al, [di]
    or al, al
    jz .scanned
    cmp al, '.'
    jne .nodot
    mov bh, 1
.nodot:
    inc di
    inc bl
    jmp short .scan
.scanned:
    or bh, bh
    jnz .extok
    cmp bl, 8                       ; room for '.DOC' inside 8.3
    ja .extok
    mov word [di], 'D' * 256 + '.'  ; little-endian: '.', 'D'
    mov word [di+2], 'C' * 256 + 'O'
    mov byte [di+4], 0
.extok:
    pop di
    pop bx
.noext:
    cmp byte [sc_pictwant], 0       ; Insert > Picture borrowed the dialog
    je .notpict                     ; (88.9). Answered HERE, before any of
    mov byte [sc_pictwant], 0       ; the bookkeeping below: a picture is not
    mov si, dx                      ; the document, so choosing one must not
    call sc_pictload                ; rename it, retitle the window, or move
                                    ; which folder it belongs to. sc_pictload
                                    ; reads [sc_name], so the restore comes
                                    ; AFTER it and not before
    push si
    push di
    push cx
    mov si, sc_namebank             ; the document's own name, back
    mov di, sc_name
    mov cx, SC_NAMEMAX + 1
.unbank:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .unbank
    pop cx
    pop di
    pop si
    jmp .draw
.notpict:
    push dx                         ; the window: FILE_HERE answers in DX
    push bx                         ; ...and the mode is in BL
    call OSAPI_FILE_HERE            ; where the dialog left the volume IS the
    mov [sc_dir], dx                ; folder the user chose, and it is the one
    mov [sc_drv], bl                ; this document belongs to from here on
    mov byte [sc_dirok], 1
    pop bx
    pop dx
    mov si, dx                      ; SI = our window again
    call sc_settitle                ; the caption follows the name (65.2)
    or bl, bl
    jz .load
    call sc_save
    jmp short .draw
.load:
    cmp word [sc_fszh], 0           ; the refusal gate, BEFORE any read
    jne .toobig                     ; (SPEC.md 68.4): a file the staging
    cmp word [sc_fsz], SC_STGCAP    ; claim cannot hold is refused from the
    ja .toobig                      ; directory entry's own size
    call sc_load
    jmp short .draw
.toobig:
    mov ax, sc_e_big
    call sc_saymsg
.draw:
    jmp sc_redraw                   ; tail call; SI is the window ptr

; -----------------------------------------------------------------------------
; sc_goto - put the volume back in this document's folder (SPEC.md 19.2)
; out: nothing; preserves all registers
;
; **THE KERNEL DOES THIS NOW, and this routine is kept as a no-op that costs
; two compares** (SPEC.md 19.2.1). A file name used to resolve in the ONE
; global current directory shared by every Disk window and by the file
; dialog: right after Save As it still named the folder the user picked -
; which is why saving into a folder worked - but by the next Save anything
; that navigated had moved it, and the write landed in the root. Four
; packages each carried their own copy of the six lines below, which is what
; eventually said the kernel owed the feature rather than the SDK owing an
; example. An instance owns its directory now, so OSAPI_FILE_HERE answers
; this document's folder and the OSAPI_FILE_GOTO below never fires.
;
; It stays because the slots keep their contract (SPEC.md 20.8 rule 4) and
; because a remount was always skipped when the volume was already there -
; which is now every time. Deleting it would be correct and would also delete
; the record of why it was ever needed.
; -----------------------------------------------------------------------------
sc_goto:
    push ax
    push bx
    push dx
    cmp byte [sc_dirok], 0
    je .out                     ; never saved anywhere in particular
    call OSAPI_FILE_HERE
    cmp dx, [sc_dir]
    jne .move
    cmp bl, [sc_drv]
    je .out
.move:
    mov dx, [sc_dir]
    mov bl, [sc_drv]
    call OSAPI_FILE_GOTO        ; CF = it could not be listed; the file call
.out:                           ; that follows will say so in its own words
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_arg - open the document we were launched to open (SPEC.md 54.5)
; in:  nothing; called from sc_entry once the window and the claim exist
; out: nothing; preserves all registers AND the flags, because the CF this
;      package owes the loader is still riding in them
;
; The kernel hands over a name and a (cluster, volume) pair rather than
; putting us in the right folder, because it cannot: the loader read our own
; image out of OUR directory and far-called this entry as one unit. So the
; whole of accepting a document is to copy the name, record the folder the
; way Save As already records one, and let sc_load do what Ctrl-O does.
;
; The name lives in the KERNEL segment, so ES is loaded explicitly rather
; than trusted: it happens to still be KERNEL_SEG here, and a later edit that
; left a package segment in ES would read this package's own image as a file
; name and fail in a way that looks like a kernel bug.
; -----------------------------------------------------------------------------
sc_arg:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the usual case
    jc .out
    mov [sc_dir], dx                ; the folder it lives in, recorded the
    mov [sc_drv], bl                ; way Save As records one - sc_goto then
    mov byte [sc_dirok], 1          ; takes sc_load there
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, sc_name
    mov cx, 13
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.named:
    push ds
    pop es                          ; ES = DS again, the callback default
    call sc_compttl                 ; the title follows the name - compose
                                    ; only, the window is not shown yet and
                                    ; the loader draws the caption (65.2)
    call sc_load                    ; ...and this is Ctrl-O, unchanged
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; sc_defname - seed the current document name (internal)
; in:  nothing
; out: sc_name = 'DOCUMENT.DOC'; preserves all registers
; The loader zeroes our bss (SPEC.md 21 step 5), and an empty name would
; make Ctrl-S fail with FERR_NAME on a brand-new note - so a fresh Note Pad
; still has the document the fixed-name version always had.
; -----------------------------------------------------------------------------
sc_defname:
    push ax
    push si
    push di
    mov si, sc_s_default
    mov di, sc_name
.copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copy
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_compttl - compose the window title: 'Scribe - ' + sc_name
; out: sc_wttl (bss - the window record points at it for its whole life);
;      preserves all registers. Compose-only: sc_entry runs it before the
;      window exists (SPEC.md 68.2).
; sc_settitle - ...and tell the kernel the bytes changed (caption redraw).
;      Callback context (lock held).
; -----------------------------------------------------------------------------
sc_compttl:
    push si
    push di
    mov si, sc_s_tpre
    mov di, sc_wttl
    call sc_stcat
    mov si, sc_name
    call sc_stcat
    push ax
    xor al, al
    mov [di], al
    pop ax
    pop di
    pop si
    ret

sc_settitle:
    push ax
    push bx
    call sc_compttl
    mov bx, [sc_win]
    or bx, bx
    jz .out
    xor ax, ax                      ; 0 = "the bytes W_TITLE names changed
    call OSAPI_WM_TITLE             ; underneath" (SPEC.md 11.92)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_setmsg - compose a toast around the live document name (internal)
; in:  SI = a NUL prefix ('Saved ' / 'Loaded ')
; out: sc_tbuf holds prefix + sc_name and it has been said; preserves all
;      registers
; -----------------------------------------------------------------------------
sc_setmsg:
    push ax
    push si
    push di
    mov di, sc_tbuf
.pre:
    mov al, [si]
    or al, al
    jz .name
    mov [di], al
    inc si
    inc di
    jmp short .pre
.name:
    mov si, sc_name
.copy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    jmp short .copy
.done:
    mov ax, sc_tbuf
    call sc_saymsg              ; the kernel copies it, so sc_tbuf is free to
    pop di                      ; be recomposed the moment this returns
    pop si
    pop ax
    ret

; =============================================================================
; THE SELECTION (SPEC.md 27.8)
;
; A range of character indices, [sc_sel0], [sc_sel1), and an inversion drawn
; over the cells that fall inside it. Two things make it nearly free.
;
; It is an XOR FILL, per row, applied by sc_rflush right after the run that
; drew that row - so it costs one primitive call per selected row and needs no
; second colour, no second font pass and no change to how a row is measured.
; On the two 1bpp adapters an inversion is what a Macintosh selection IS.
;
; And it rides the row signatures (SPEC.md 27.2) rather than sitting beside
; them: a cell inside the selection folds with bit 15 of its character set, so
; a selection that moves dirties exactly the rows it left and the rows it
; arrived at, and a redraw that changed nothing still draws nothing. The one
; thing signatures cannot carry is which CELLS of a redrawn row are inverted -
; a row can be redrawn for a reason that has nothing to do with the selection
; - so sc_rflush keeps [sc_prs0]/[sc_prs1] the way it keeps [sc_prcc], and
; folds the union of the old span and the new one into the cells it redraws.
;
; XOR is its own inverse, and that is the sharp edge here. A cell the run did
; NOT redraw still carries the inversion the last pass gave it, so inverting
; it a second time would take it away - which is why sc_selxor intersects the
; row's selected span with [sc_flo]..[sc_fhi], the cells actually written.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_selq - is character index AX inside the selection?
; in:  AX = a character index; out: CF = 1 if it is; preserves all registers
; -----------------------------------------------------------------------------
sc_selq:
    cmp byte [sc_selon], 0
    je .no
    cmp ax, [sc_sel0]
    jb .no
    cmp ax, [sc_sel1]
    jae .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; sc_selqo - was character index AX selected in the selection ON SCREEN?
; in:  AX; out: CF = 1 if it was; preserves all registers
; -----------------------------------------------------------------------------
sc_selqo:
    cmp byte [sc_oselon], 0
    je .no
    cmp ax, [sc_osel0]
    jb .no
    cmp ax, [sc_osel1]
    jae .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; sc_xfold - widen the row's CHANGED-inversion span to include column AX
; in:  AX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_xfold:
    cmp word [sc_xs0], 0xFFFF
    jne .lo
    mov [sc_xs0], ax
    mov [sc_xs1], ax
    ret
.lo:
    cmp ax, [sc_xs0]
    jae .hi
    mov [sc_xs0], ax
.hi:
    cmp ax, [sc_xs1]
    jbe .out
    mov [sc_xs1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_selfold - widen the row's inverted span to include cell column AX
; in:  AX = a column; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_selfold:
    cmp word [sc_rs0], 0xFFFF
    jne .lo
    mov [sc_rs0], ax
    mov [sc_rs1], ax
    ret
.lo:
    cmp ax, [sc_rs0]
    jae .hi
    mov [sc_rs0], ax
.hi:
    cmp ax, [sc_rs1]
    jbe .out
    mov [sc_rs1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_selxor - invert the selected cells of the row just drawn
; in:  [sc_rs0]/[sc_rs1] = the row's selected span (0xFFFF = none),
;      [sc_flo]/[sc_fhi] = the cells the run actually wrote, [sc_rby],
;      [sc_tx]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_selxor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [sc_rs0]
    cmp ax, 0xFFFF
    je .out
    mov cx, [sc_rs1]
    cmp ax, [sc_flo]            ; the intersection, and nothing wider: a cell
    jae .l0                     ; the run did not touch is still carrying the
    mov ax, [sc_flo]            ; inversion the last pass gave it, and a
.l0:                            ; second XOR would take it back off
    cmp cx, [sc_fhi]
    jbe .l1
    mov cx, [sc_fhi]
.l1:
    cmp ax, cx
    ja .out
    call sc_cx                  ; AX = x1 (SPEC.md 68.13: the same three shifts
    push ax                     ; in the kernel's cell, a sc_px[] lookup in a
    mov ax, cx                  ; chosen face)
    inc ax
    call sc_cx
    dec ax
    mov cx, ax                  ; CX = x2, the last column of the last cell
    pop ax
    mov bx, [sc_rby]
    mov dx, bx
    add dx, [sc_gh1]
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_selclr - there is no selection any more
; out: CF = 1 there was none to clear (so nothing needs redrawing);
;      preserves all registers
; -----------------------------------------------------------------------------
sc_selclr:
    cmp byte [sc_selon], 0
    je .none
    mov byte [sc_selon], 0
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_selset - select [AX, DX), in either order
; in:  AX, DX = two character indices; out: nothing; preserves all registers
; An empty range is no selection at all, which is what makes "click" and
; "drag back to where you started" the same thing.
; -----------------------------------------------------------------------------
sc_selset:
    push ax
    push dx
    cmp ax, dx
    jbe .ord
    xchg ax, dx
.ord:
    cmp ax, dx
    je .none
    mov [sc_sel0], ax
    mov [sc_sel1], dx
    mov byte [sc_selon], 1
    jmp short .out
.none:
    mov byte [sc_selon], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_selget - the live selection
; out: CF = 1 there is none; else CF = 0 with AX = its start and CX = its
;      length; preserves all other registers
; -----------------------------------------------------------------------------
sc_selget:
    cmp byte [sc_selon], 0
    je .no
    mov ax, [sc_sel0]
    cmp ax, [sc_len]
    jae .no                     ; the note is SHORTER than the selection now
    mov cx, [sc_sel1]
    cmp cx, [sc_len]            ; ...and the far end is clamped rather than
    jbe .end                    ; refused, so a shrink leaves the part of the
    mov cx, [sc_len]            ; selection that still exists selected
.end:
    cmp cx, ax
    jbe .no                     ; BELOW, not just equal: an inverted pair here
                                ; would make `sub` answer ~65,000 and hand
                                ; that to whoever asked - clip_put refuses it,
                                ; but sc_rev would swap bytes clean off the
                                ; end of the document claim
    sub cx, ax
    clc
    ret
.no:
    xor ax, ax
    xor cx, cx
    stc
    ret

; -----------------------------------------------------------------------------
; sc_seldel - delete the selection; the caret lands where it began
; out: CF = 1 there was none; preserves all registers
; -----------------------------------------------------------------------------
sc_seldel:
    push ax
    push bx
    push cx
    call sc_selget
    jc .no
    mov bx, ax
    call sc_delspan
    mov [sc_cur], bx
    call sc_selclr
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Editing a RANGE (SPEC.md 27.8)
;
; sc_ins and sc_del were the whole edit surface, and both were "one character
; at the caret". Cut, Paste, Replace and Undo all move runs, so the run is the
; primitive now and the two old routines are cases of it.
;
; Both record undo BEFORE they move anything (SPEC.md 27.9) - a deletion has
; to reach the undo blob while its bytes are still in the note - and both
; leave [sc_cur] alone. Where the caret goes afterwards is the caller's
; decision and differs for every one of them.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_capfor - make the document claim hold AX bytes
; in:  AX = the bytes wanted
; out: CF = 0 and [sc_cap] >= AX; CF = 1 refused. Preserves all registers.
; -----------------------------------------------------------------------------
sc_capfor:
    push ax
    push bx
    push cx
    mov bx, ax
    cmp bx, [sc_cap]
    jbe .yes
    mov ax, bx
    add ax, 1023
    jc .no                      ; past 64KB, which SC_MAXKB refuses anyway
    mov cl, 10
    shr ax, cl
    call sc_resize              ; clamps to SC_MAXKB and may be refused
    jc .no
    cmp bx, [sc_cap]
    ja .no                      ; the clamp bit: the note is as big as it gets
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_delspan - remove CX bytes at index BX
; in:  BX = the first index to go, CX = how many (both clamped to the note)
; out: nothing; preserves all registers. [sc_len] shrinks; [sc_cur] is the
;      caller's business.
; -----------------------------------------------------------------------------
sc_delspan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [sc_len]
    cmp bx, ax
    jae .out
    sub ax, bx
    cmp cx, ax
    jbe .have
    mov cx, ax
.have:
    jcxz .out
    call sc_hmark
    mov byte [sc_dirty], 1      ; bytes are leaving: dirty (SPEC.md 68.4)
    mov ax, bx
    call sc_urec_del            ; while the bytes are still here to be copied
    mov es, [sc_dseg]
    mov si, bx
    add si, cx                  ; the first byte that survives
    mov di, bx
    mov dx, [sc_len]
    sub dx, si                  ; ...and how many of them there are
    push cx                     ; the SPAN, which .close still needs
    mov cx, dx
    jcxz .nomv
    push ds                     ; forwards here: the gap closes DOWNWARD, so
    mov ds, [sc_dseg]           ; DI trails SI (SPEC.md 27.12)
    cld
    rep movsb
    pop ds
.nomv:
    pop cx
    ; --- the same close on the CHP claim (SPEC.md 68.3); the undo blob
    ; above already banked both halves while the bytes were still here
    push cx
    mov es, [sc_cseg]
    mov si, bx
    add si, cx
    mov di, bx
    mov dx, [sc_len]
    sub dx, si
    mov cx, dx
    jcxz .cnomv
    push ds
    mov ds, [sc_cseg]
    cld
    rep movsb
    pop ds
.cnomv:
    pop cx
.close:
    mov ax, [sc_len]
    sub ax, cx
    mov [sc_len], ax
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
; sc_gaproom - open a CX-byte gap at index BX for the caller to fill
; in:  BX = where, CX = how many
; out: CF = 0 and [sc_len] already counts the gap; CF = 1 refused and nothing
;      moved. Preserves all registers.
;
; It does NOT record undo: the caller knows what it is about to put there and
; how much of it survives, and a paste's filter can shorten it afterwards.
; -----------------------------------------------------------------------------
sc_gaproom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    jcxz .ok
    mov ax, [sc_len]
    add ax, cx
    jc .no                      ; a 16-bit note cannot pass 65535
    call sc_capfor
    jc .no
    mov byte [sc_dirty], 1      ; a gap is opening: dirty (SPEC.md 68.4)
    mov es, [sc_dseg]
    mov si, [sc_len]
    dec si                      ; the last live byte
    mov di, si
    add di, cx
    mov dx, [sc_len]
    sub dx, bx                  ; the bytes to the right of the gap
    push cx                     ; the GAP width, which .done still needs
    mov cx, dx
    jcxz .nomv
    push ds                     ; backwards: sc_ins's case with a gap wider
    mov ds, [sc_dseg]           ; than one byte (SPEC.md 27.12)
    std
    rep movsb
    cld
    pop ds
.nomv:
    pop cx
    ; --- the same gap on the CHP claim, FILLED with the typing attrs
    ; (SPEC.md 68.3): a paste's characters arrive dressed as the caret is;
    ; the undo restore overwrites the fill with the blob's own bytes
    push cx
    push ax
    mov es, [sc_cseg]
    mov si, [sc_len]
    dec si
    mov di, si
    add di, cx
    mov dx, [sc_len]
    sub dx, bx
    push cx
    mov cx, dx
    jcxz .cnomv
    push ds
    mov ds, [sc_cseg]
    std
    rep movsb
    cld
    pop ds
.cnomv:
    pop cx
    mov di, bx
    mov al, [sc_chp]
    cld
    rep stosb
    pop ax
    pop cx
.done:
    mov ax, [sc_len]
    add ax, cx
    mov [sc_len], ax
    call sc_hmark
.ok:
    clc
    jmp short .out
.no:
    stc
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
; sc_editinv - the layout state is all indices into a buffer that just moved
; out: nothing; preserves all registers
;
; The lighter half of sc_clamp: it does NOT put the view back at the top,
; because a paste, a replace and an undo all happen where the user is looking
; and scrolling away from that would be the opposite of helpful. What it must
; do is drop the checkpoint and sc_rows - sc_redraw seeds the next walk from
; one of them, and both are character indices whose row a bulk edit above the
; view has just renamed.
; -----------------------------------------------------------------------------
sc_editinv:
    push ax
    call sc_hmark
    mov byte [sc_ckok], 0
    mov byte [sc_rowsok], 0
    mov ax, [sc_len]
    cmp [sc_cur], ax
    jbe .cur
    mov [sc_cur], ax
.cur:
    cmp byte [sc_selon], 0      ; the SELECTION is a pair of indices into the
    je .out                     ; same buffer and it was clamped nowhere: a
    cmp [sc_sel1], ax           ; Replace All that shortens the note, or an
    jbe .out                    ; undo of a paste, leaves it pointing past the
    mov [sc_sel1], ax           ; end. sc_selget clamps too - this is the
    mov ax, [sc_sel0]           ; other half, so the stored pair is never a
    cmp ax, [sc_sel1]           ; lie in the first place
    jb .out
    mov byte [sc_selon], 0      ; nothing of it survives
.out:
    pop ax
    ret

; =============================================================================
; Applying character formatting (SPEC.md 68.3)
; =============================================================================

; -----------------------------------------------------------------------------
; sc_chpsync - the caret moved without a selection: the typing attributes are
;              those of the character left of it (Word's rule; at index 0 the
;              character right of it). Preserves all registers.
; -----------------------------------------------------------------------------
sc_chpsync:
    push ax
    push bx
    push es
    cmp byte [sc_selon], 0
    jne .out                    ; a selection carries its own answer
    cmp word [sc_len], 0
    je .out                     ; an empty document keeps what it has
    mov bx, [sc_cur]
    or bx, bx
    jz .at0
    dec bx
.at0:
    ; a ¶ mark's byte is a PAP INDEX, not character attrs (SPEC.md 68.3):
    ; look left past marks to the last real character; a run of marks back
    ; to the start answers plain
    mov es, [sc_dseg]
.skip:
    cmp byte [es:bx], 13
    jne .have
    or bx, bx
    jz .plain
    dec bx
    jmp short .skip
.have:
    mov es, [sc_cseg]
    mov al, [es:bx]
    and al, 0x7F
    mov [sc_chp], al
    jmp short .out
.plain:
    mov byte [sc_chp], 0
.out:
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dispattr - the attr byte the ribbon and the dialog reflect
; out: AL = it; preserves everything else
; The selection's FIRST character while one is up (the honest 8086 answer -
; an AND over a 30KB span per drag pass is not), the typing attrs otherwise.
; -----------------------------------------------------------------------------
sc_dispattr:
    cmp byte [sc_selon], 0
    jne .sel
    mov al, [sc_chp]
    and al, 0x7F
    ret
.sel:
    push bx
    push es
    mov bx, [sc_sel0]
.skip:
    cmp bx, [sc_len]
    jae .fall
    cmp bx, [sc_sel1]
    jae .fall
    mov es, [sc_dseg]               ; a ¶'s byte is a PAP index (SPEC.md
    cmp byte [es:bx], 13            ; 65.3): the first REAL character answers
    jne .have
    inc bx
    jmp short .skip
.have:
    mov es, [sc_cseg]
    mov al, [es:bx]
    and al, 0x7F
    pop es
    pop bx
    ret
.fall:
    mov al, [sc_chp]
    and al, 0x7F
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_applyattr - toggle attribute AL on the selection or the typing attrs
; in:  AL = ONE attribute bit, or 0 = reset to plain; SI = window ptr
; out: nothing; preserves all registers. The caller repaints (sc_redraw's
;      pass 1 finds the dirtied rows through the signatures).
;
; Word's span semantics (SPEC.md 68.3): if ANY character in the span lacks
; the attribute, set it on all; else clear it on all. ONE undo group - the
; bulk record, whose blob banks text AND CHP; the text half comes back
; unchanged and the CHP half is the change.
; -----------------------------------------------------------------------------
sc_applyattr:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov dl, al                  ; DL = the mask
    cmp byte [sc_selon], 0
    je .typing
    call sc_selget              ; AX = start, CX = length
    jc .typing
    mov byte [sc_dirty], 1      ; the span's dress changes (SPEC.md 68.4)
    push ax
    push cx
    call sc_urec_bulk           ; the span, text and CHP, into the arena -
    pop cx                      ; refusal drops the stack ("swept but not
    pop ax                      ; undoable"), and the sweep still runs
    mov es, [sc_cseg]
    mov bx, ax
    mov di, cx                  ; DI = count
    push ds
    mov ds, [sc_dseg]           ; [bx] = the text: a ¶ mark's CHP byte is a
                                ; PAP INDEX (SPEC.md 68.3) and every loop
                                ; below must step over it untouched. sc_*
                                ; is unreachable until the pop - the loops
                                ; touch only registers
    or dl, dl
    jz .clr                     ; mask 0: reset the whole span to plain
    push bx                     ; --- any character lacking it? ---------------
    push di
.scan:
    cmp byte [bx], 13
    je .scnx
    mov al, [es:bx]
    and al, dl
    cmp al, dl
    jne .lacks
.scnx:
    inc bx
    dec di
    jnz .scan
    pop di                      ; all of them have it: CLEAR on all
    pop bx
    not dl                      ; the complement, computed once
.clr2:
    cmp byte [bx], 13
    je .clnx
    mov al, [es:bx]
    and al, dl
    mov [es:bx], al
.clnx:
    inc bx
    dec di
    jnz .clr2
    not dl                      ; back to the mask for the hidden test below
    jmp short .swept
.lacks:
    pop di                      ; someone lacks it: SET on all
    pop bx
.set:
    cmp byte [bx], 13
    je .setnx
    mov al, [es:bx]
    or al, dl
    mov [es:bx], al
.setnx:
    inc bx
    dec di
    jnz .set
    jmp short .swept
.clr:
.clrl:
    cmp byte [bx], 13
    je .crnx
    mov byte [es:bx], 0
.crnx:
    inc bx
    dec di
    jnz .clrl
.swept:
    pop ds
    call sc_urec_bulkend        ; CX is still the span's length
    or dl, dl                   ; hidden changes the LAYOUT (a hidden cell
    jz .relay                   ; occupies none), so the checkpoint and the
    cmp dl, SCAT_HID            ; row table describe a document that just
    jne .done                   ; re-wrapped
.relay:
    cmp dl, SCAT_HID
    jne .norec
    mov byte [sc_hashid], 1         ; hidden entered the document: the cheap
.norec:                             ; column paths stand down (SPEC.md 68.1)
    mov byte [sc_ckok], 0
    mov byte [sc_rowsok], 0
    call sc_hmark
    jmp short .done
.typing:
    or dl, dl
    jz .rst
    xor byte [sc_chp], dl       ; no selection: the toggle rides the NEXT
    jmp short .done             ; character typed
.rst:
    mov byte [sc_chp], 0
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Applying paragraph formatting (SPEC.md 68.3)
;
; Every door - the Ctrl keys, the ruler's cells, the marker drags, the Format
; Paragraph dialog - funnels through sc_modpap, one span modifier: it walks
; the ¶ marks the selection (or the caret's paragraph) touches and rewrites
; each mark's CHP byte to the dictionary index of its MODIFIED format. The
; indices ride the ordinary text+CHP undo machinery; the tail paragraph's
; index lives in [sc_pap_tail] outside the buffers, and a change to it is
; the one paragraph fact undo cannot restore (SPEC.md 68.3).
; =============================================================================

; -----------------------------------------------------------------------------
; sc_papfind - the dictionary index for the candidate at [sc_pfb]
; out: CF=0 and AX = the index (AH=0); CF=1 refused - 256 unique formats
;      exist and this is a 257th (the toast says so). Preserves all others.
; Deduplicated by linear scan: 256 entries of 4 bytes is nothing beside one
; gfx call, and an append never moves or renumbers an entry - which is what
; lets an undo blob's old index still mean the old format.
; -----------------------------------------------------------------------------
sc_papfind0:
    push bx
    push cx
    push si
    push es
    mov es, [sc_pseg]
    mov cx, [sc_papn]
    xor si, si
    xor bx, bx                      ; BX = the entry index
.scan:
    jcxz .new
    mov ax, [sc_pfb]
    cmp ax, [es:si]
    jne .next
    mov ax, [sc_pfb+2]
    cmp ax, [es:si+2]
    je .have
.next:
    inc bx
    add si, 4
    dec cx
    jmp short .scan
.have:
    mov ax, bx
    clc
    jmp short .out
.new:
    cmp word [sc_papn], SC_PAPMAX
    jae .full
    mov ax, [sc_pfb]
    mov [es:si], ax
    mov ax, [sc_pfb+2]
    mov [es:si+2], ax
    mov ax, [sc_papn]
    inc word [sc_papn]
    clc
    jmp short .out
.full:
    mov ax, sc_m_papfull
    mov [sc_ovmsg], ax              ; NOT sc_saymsg. This runs inside
    stc                             ; SCRIBE.OVL now (88.8.1) and the module
.out:                               ; never speaks - it leaves the reason and
    pop es                          ; sc_ovcall says it on the way out
    pop si
    pop cx
    pop bx
    ret

; sc_papfind - the dictionary lookup with its refusal said. Resident callers
; use this; the module reaches sc_papfind0 through sc_v_papfind and lets
; sc_ovcall speak, so the toast is raised on the resident side either way.
sc_papfind:
    call sc_papfind0
    jnc .out
    push ax
    mov ax, [sc_ovmsg]
    or ax, ax
    jz .none
    mov word [sc_ovmsg], 0
    call sc_saymsg
.none:
    pop ax
    stc
.out:
    ret

; sc_cl0max - clamp signed AL into 0..SC_INDMAX; preserves all others
sc_cl0max:
    or al, al
    jns .pos
    xor al, al
    ret
.pos:
    cmp al, SC_INDMAX
    jle .out
    mov al, SC_INDMAX
.out:
    ret

; sc_clfirst - clamp signed AL (a first-line indent) into -left..SC_INDMAX:
; a first line may hang LEFT of the left indent by at most the indent itself,
; so the pen never crosses the margin. Preserves all others.
sc_clfirst:
    push bx
    cmp al, SC_INDMAX
    jle .hiok
    mov al, SC_INDMAX
.hiok:
    mov bl, [sc_pfb+1]
    neg bl
    cmp al, bl
    jge .out
    mov al, bl
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_modone - apply [sc_ppop]/[sc_pparg] to the format at dictionary index AL
; out: CF=0 and AX = the resulting index; CF=1 the dictionary refused
; preserves everything else; every indent clamp lives here
; -----------------------------------------------------------------------------
sc_modone:
    push bx
    push cx
    push es
    cmp byte [sc_ppop], SCPO_RESET
    jne .setall
    xor ax, ax                      ; Normal: index 0, no dictionary walk
    clc
    jmp .out
.setall:
    cmp byte [sc_ppop], SCPO_SETALL ; the dialog filled [sc_pfb] whole: the
    jne .load                       ; entry copy below would clobber it
    jmp .find
.load:
    xor ah, ah                      ; the entry, copied to the candidate
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov es, [sc_pseg]
    mov ax, [es:bx]
    mov [sc_pfb], ax
    mov ax, [es:bx+2]
    mov [sc_pfb+2], ax
    mov al, [sc_ppop]
    mov ah, [sc_pparg]
    cmp al, SCPO_ALIGN
    je .align
    cmp al, SCPO_SPACE
    je .space
    cmp al, SCPO_SB
    je .sb
    cmp al, SCPO_ADDLEFT
    je .addl
    cmp al, SCPO_HANG
    je .hang
    cmp al, SCPO_UNHANG
    je .unhang
    cmp al, SCPO_SETLEFT
    je .setl
    cmp al, SCPO_SETFIRST
    je .setf
    cmp al, SCPO_SETRIGHT
    je .setr
    jmp .find                       ; SCPO_SETALL: the dialog filled the whole
                                    ; candidate before calling
.align:
    mov al, [sc_pfb]
    and al, 0xFF - SCPA_ALIGN
    or al, ah
    mov [sc_pfb], al
    jmp .find
.space:
    mov al, [sc_pfb]
    and al, 0xFF - SCPA_SPACE
    mov cl, 2
    shl ah, cl
    or al, ah
    mov [sc_pfb], al
    jmp .find
.sb:
    mov al, [sc_pfb]
    and al, 0xFF - SCPA_SB
    or ah, ah
    jz .sbs
    or al, SCPA_SB
.sbs:
    mov [sc_pfb], al
    jmp .find
.addl:
    mov al, [sc_pfb+1]
    add al, ah
    call sc_cl0max
    mov [sc_pfb+1], al
    jmp short .fixf
.hang:
    mov al, [sc_pfb+1]              ; Ctrl-T: the body indents half an inch
    add al, 5                       ; and the first line stays - Word's
    call sc_cl0max                  ; HangingIndent exactly
    mov [sc_pfb+1], al
    mov al, [sc_pfb+2]
    sub al, 5
    call sc_clfirst
    mov [sc_pfb+2], al
    jmp short .fixf
.unhang:
    mov al, [sc_pfb+1]
    sub al, 5
    call sc_cl0max
    mov [sc_pfb+1], al
    mov al, [sc_pfb+2]
    add al, 5
    call sc_clfirst
    mov [sc_pfb+2], al
    jmp short .fixf
.setl:
    mov al, ah
    call sc_cl0max
    mov [sc_pfb+1], al
    jmp short .fixf
.setf:
    mov al, ah
    call sc_clfirst
    mov [sc_pfb+2], al
    jmp short .fixf
.setr:
    mov al, ah
    call sc_cl0max
    mov [sc_pfb+3], al
    jmp short .find
.fixf:
    mov al, [sc_pfb+2]              ; the left indent moved: the first-line
    call sc_clfirst                 ; clamp is relative to it
    mov [sc_pfb+2], al
.find:
    call sc_papfind
.out:
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_modpap - apply operation AL (argument AH) to every paragraph the
;             selection touches, or the caret's (SPEC.md 68.3)
; in:  AL = SCPO_*, AH = argument (signed where the op says), SI = window ptr
; out: nothing; preserves all registers. ONE bulk undo group over
;      [paragraph start .. last affected ¶]; the caller redraws.
; -----------------------------------------------------------------------------
sc_modpap:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov [sc_ppop], al
    mov [sc_pparg], ah
    mov byte [sc_ppstop], 0
    mov byte [sc_dirty], 1          ; a paragraph format is changing (65.4)
    cmp byte [sc_selon], 0
    je .caret
    call sc_selget                  ; AX = start, CX = length
    jnc .selok
.caret:
    mov ax, [sc_cur]
    mov dx, ax
    jmp short .spango
.selok:
    mov dx, ax
    add dx, cx
    dec dx                          ; DX = the LAST selected character
.spango:
    call sc_pback                   ; AX = the first paragraph's start
    mov di, ax                      ; DI = the span's start
    mov bx, dx                      ; scan forward for the LAST char's ¶
    mov cx, [sc_len]
    sub cx, bx
    jbe .tail1
    mov es, [sc_dseg]
.fs:
    cmp byte [es:bx], 13
    je .have13
    inc bx
    dec cx
    jnz .fs
.tail1:
    mov dx, [sc_len]                ; no ¶ behind it: the TAIL paragraph
    mov byte [sc_pptl], 1
    jmp short .spanok
.have13:
    mov dx, bx
    inc dx                          ; the span INCLUDES the ¶ byte
    mov byte [sc_pptl], 0
.spanok:
    mov cx, dx
    sub cx, di
    jcxz .sweep                     ; an empty document: the tail alone
    mov ax, di
    call sc_urec_bulk               ; text AND CHP into the arena (65.3); a
                                    ; refusal drops the stack and the sweep
                                    ; still runs - degrade, not refuse
.sweep:
    mov bx, di
    mov cx, dx
    sub cx, bx
    jcxz .swept
    mov es, [sc_dseg]
.sw:
    cmp byte [es:bx], 13
    jne .swn
    push es
    mov es, [sc_cseg]
    mov al, [es:bx]                 ; the ¶'s index -> its modified format's
    call sc_modone
    jc .swfail
    mov [es:bx], al
    pop es
    or al, al
    jz .swn
    mov byte [sc_hasfmt], 1
    jmp short .swn
.swfail:
    pop es
    mov byte [sc_ppstop], 1
    jmp short .swept
.swn:
    inc bx
    dec cx
    jnz .sw
.swept:
    mov cx, dx
    sub cx, di
    jcxz .notail
    call sc_urec_bulkend            ; CX = the span's length: the text half
                                    ; comes back unchanged, the ¶ bytes are
                                    ; the change
.notail:
    cmp byte [sc_pptl], 0
    je .invd
    cmp byte [sc_ppstop], 0
    jne .invd
    mov al, [sc_pap_tail]           ; the tail paragraph rides its own byte
    call sc_modone
    jc .invd
    mov [sc_pap_tail], al
    or al, al
    jz .invd
    mov byte [sc_hasfmt], 1
.invd:
    mov byte [sc_ckok], 0           ; row starts and heights may all have
    mov byte [sc_rowsok], 0         ; moved (SPEC.md 68.6): the next pass 1
    call sc_hmark                   ; re-lays the view and the moved-rows
    mov byte [sc_follow], 1         ; erase repaints what shifted; the caret's
                                    ; row may have left the view
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_copy - put the selection on the clipboard
; out: CF = 1 nothing was selected, or the clipboard refused (and then the
;      toast says so); preserves all registers
; -----------------------------------------------------------------------------
sc_copy:
    push ax
    push bx
    push cx
    push si
    push es
    call sc_selget              ; AX = start, CX = length
    jc .no
    mov si, ax
    mov es, [sc_dseg]           ; the note is a claim of its own, which is
    call OSAPI_CLIP_PUT         ; exactly why the slot takes a far pointer
    jc .full
    clc
    jmp short .out
.full:
    mov ax, sc_e_cbig
    call sc_saymsg
    stc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_cut - copy the selection, then take it out
; out: CF = 1 nothing happened; preserves all registers
; The copy comes first and its refusal is final: a cut that lost the text
; because the clipboard would not hold it is the one outcome nobody can undo
; from the keyboard.
; -----------------------------------------------------------------------------
sc_cut:
    call sc_copy
    jc .out
    call sc_seldel
    call sc_editinv
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; sc_paste - replace the selection with the clipboard's text
; out: CF = 1 nothing happened; preserves all registers
;
; No staging buffer, and that is the point of doing it in this order: the gap
; is opened in the document first and OSAPI_CLIP_GET writes straight into it,
; so a 4KB paste needs 4KB of document and not 8KB of anything.
;
; What arrives is then FILTERED in place, because the clipboard is shared and
; the bytes in it were put there by another program: a stray control code
; would draw as a random glyph and a lone LF would show as a missing line
; break. CR LF folds to one 13, a lone LF likewise, and anything else outside
; 32..126 is dropped - exactly sc_load's rule, for exactly sc_load's reason.
; -----------------------------------------------------------------------------
sc_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_CLIP_SIZE        ; AX = the whole length; CF = 1 = it is empty
    jc .none
    mov dx, ax                  ; DX = n, held all the way through
    call sc_seldel              ; a paste REPLACES the selection - and the two
                                ; halves land in ONE undo group, because the
                                ; insert starts exactly where the delete left
    mov bx, [sc_cur]
    mov cx, dx
    call sc_gaproom
    jc .nomem
    mov es, [sc_dseg]
    mov di, bx
    mov cx, dx
    call OSAPI_CLIP_GET         ; ES:DI = the gap; it holds the text now
    mov si, bx                  ; read...
    mov di, bx                  ; ...and write, both inside the gap
    mov cx, dx
    xor ax, ax                  ; AL = the byte before, AH = this one
.f:
    jcxz .fdone
    mov ah, [es:si]
    inc si
    dec cx
    cmp ah, 10
    jne .fnotlf
    cmp al, 13
    je .fskip                   ; CR LF: the 13 already went in
    mov ah, 13                  ; a lone LF is a line break too
.fnotlf:
    cmp ah, 13
    je .fkeep
    cmp ah, 9                   ; a tab survives the paste now (SPEC.md 68.3)
    jne .fno9
    mov byte [sc_hastab], 1
    jmp short .fkeep
.fno9:
    cmp ah, 32
    jb .fskip
    cmp ah, 126
    ja .fskip
.fkeep:
    mov [es:di], ah
    inc di
.fskip:
    mov al, ah
    jmp short .f
.fdone:
    mov cx, di
    sub cx, bx                  ; CX = the bytes that survived
    push cx
    mov si, bx
    add si, dx                  ; the first byte past the gap
    mov cx, [sc_len]
    sub cx, si                  ; ...and the tail behind it
.t:
    jcxz .tdone
    mov ah, [es:si]
    mov [es:di], ah
    inc si
    inc di
    dec cx
    jmp short .t
.tdone:
    pop cx
    ; --- mirror the tail compaction on the CHP claim (SPEC.md 68.3): the
    ; filter kept CX of the DX-byte gap, so the CHP tail slides down the
    ; difference; the kept cells already hold [sc_chp] from sc_gaproom
    push cx
    mov es, [sc_cseg]
    mov si, bx
    add si, dx
    mov di, bx
    add di, cx
    mov cx, [sc_len]
    sub cx, si
    jcxz .cnot
    push ds
    mov ds, [sc_cseg]
    cld
    rep movsb
    pop ds
.cnot:
    pop cx
    mov ax, [sc_len]
    sub ax, dx
    add ax, cx
    mov [sc_len], ax            ; the gap was n; only CX of it is text
    ; pasted ¶ marks: their CHP byte is a PAP INDEX, not the typing attrs
    ; sc_gaproom filled (SPEC.md 68.3) - every pasted paragraph joins the
    ; paragraph it was pasted INTO
    push ax
    push cx
    push di
    jcxz .nopfx
    mov ax, bx
    add ax, cx
    call sc_papat               ; AL = the governing index past the paste
    mov ah, al
    mov di, bx
.pfx:
    mov es, [sc_dseg]
    cmp byte [es:di], 13
    jne .pfn
    mov es, [sc_cseg]
    mov [es:di], ah
.pfn:
    inc di
    dec cx
    jnz .pfx
.nopfx:
    pop di
    pop cx
    pop ax
    mov ax, bx
    call sc_urec_ins            ; AX = where, CX = how many
    add bx, cx
    mov [sc_cur], bx
    call sc_editinv
    clc
    jmp short .out
.nomem:
    mov ax, sc_e_nomem
    call sc_saymsg
    stc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; UNDO, five edits deep (SPEC.md 27.9)
;
; An EDIT is a burst of keystrokes with no half-second gap in it - which is
; what the user means by one - and the clock that measures it is the worker
; that was already there for the visual break (SPEC.md 27.3). A group also
; closes on anything that is not an edit (a click, a caret key, a save, a
; menu command) and on an edit that does not touch the group's own span,
; because the record can only describe a contiguous change.
;
; A record is three numbers and a blob: at [sc_upos], this group INSERTED
; [sc_uins] bytes and REMOVED the [sc_udel] bytes now sitting at [sc_uoff] in
; the arena. Undoing it is "delete uins at upos, put the blob back at upos",
; and that is the whole of it - which is also why there is no redo: the
; inverse record would need the bytes that are being taken back out, and
; nobody asked for one.
;
; The blobs live in a HEAP CLAIM sized on demand: nothing at all until the
; first edit worth remembering, a kilobyte at a time after that, and up to
; SC_UMAXKB. When it will not grow, the OLDEST record is evicted - a stack
; four deep is still an undo, and a refusal is not - and only when there is
; nothing left to evict is the whole stack dropped. It has to be the whole
; stack: an edit that went unrecorded makes every record under it describe a
; note that no longer exists, and applying one would corrupt the document.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_uclear - forget every record and give the arena back
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_uclear:
    push ax
    push bx
    push dx
    mov byte [sc_un], 0
    mov byte [sc_uopen], 0
    mov word [sc_utop], 0
    mov dx, [sc_useg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE         ; the owner is our segment, which the slot's
    mov word [sc_useg], 0       ; X stub supplies (SPEC.md 50.3)
    mov word [sc_ukb], 0
    mov dx, [sc_cuseg]          ; ...and the CHP arena beside it (SPEC.md
    or dx, dx                   ; 65.3): the two live and die together
    jz .out
    call OSAPI_MEM_FREE
    mov word [sc_cuseg], 0
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_uclose - the open group is finished; the next edit starts a new one
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_uclose:
    mov byte [sc_uopen], 0
    ret

; -----------------------------------------------------------------------------
; sc_utop_rec - the newest record, if one is open
; out: CF = 1 there is none; else CF = 0 and SI = its word offset (every
;      array below is indexed by it). Clobbers BX and SI.
; -----------------------------------------------------------------------------
sc_utop_rec:
    cmp byte [sc_uopen], 0
    je .no
    mov bl, [sc_un]
    xor bh, bh
    or bx, bx
    jz .no
    dec bx
    shl bx, 1
    mov si, bx
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_ugrow - make the arena AX kilobytes
; in:  AX = KB; out: CF = 1 refused; preserves all registers
; -----------------------------------------------------------------------------
sc_ugrow:
    push ax
    push bx
    push dx
    cmp ax, [sc_ukb]
    jbe .yes
    cmp word [sc_useg], 0
    jne .re
    call OSAPI_MEM_CLAIM        ; out CF = 0, DX = the base
    jc .no
    mov [sc_useg], dx
    push ax                     ; ...and the CHP arena, same size (SPEC.md
    call OSAPI_MEM_CLAIM        ; 65.3): every blob offset means the same
    pop ax                      ; thing in both, so nothing forks
    jc .cfail0
    mov [sc_cuseg], dx
    mov [sc_ukb], ax
    jmp short .yes
.cfail0:
    mov dx, [sc_useg]           ; the half-claimed pair is no arena at all
    call OSAPI_MEM_FREE
    mov word [sc_useg], 0
    jmp short .no
.re:
    mov dx, [sc_useg]
    call OSAPI_MEM_REGROW       ; out CF = 0, DX = the base NOW - a grow that
    jc .no                      ; had to move reports a new one (50.3.1)
    mov [sc_useg], dx
    push ax
    mov dx, [sc_cuseg]
    call OSAPI_MEM_REGROW
    pop ax
    jc .cfail1
    mov [sc_cuseg], dx
    mov [sc_ukb], ax
    jmp short .yes
.cfail1:
    push ax                     ; the mirror was refused: shrink the text
    mov ax, [sc_ukb]            ; arena back (a shrink succeeds in place)
    mov dx, [sc_useg]           ; and report one refusal
    call OSAPI_MEM_REGROW
    mov [sc_useg], dx
    pop ax
    jmp short .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_udrop0 - forget the OLDEST record, sliding the arena down under it
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_udrop0:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp byte [sc_un], 0
    je .out
    mov cx, [sc_udel]           ; record 0's blob, which starts at offset 0
    or cx, cx
    jz .arrays
    mov es, [sc_useg]
    mov si, cx
    xor di, di
    mov bx, [sc_utop]
    sub bx, cx
.mv:
    or bx, bx
    jz .shrunk
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    dec bx
    jmp short .mv
.shrunk:
    mov es, [sc_cuseg]          ; the CHP arena slides identically (SPEC.md
    mov si, cx                  ; 65.3): same offsets, same count
    xor di, di
    mov bx, [sc_utop]
    sub bx, cx
.cmv:
    or bx, bx
    jz .cshrunk
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    dec bx
    jmp short .cmv
.cshrunk:
    mov ax, [sc_utop]
    sub ax, cx
    mov [sc_utop], ax
.arrays:
    xor bx, bx                  ; shift the four arrays down one, and every
.sh:                            ; surviving blob offset with them
    mov al, [sc_un]
    xor ah, ah
    dec ax
    cmp bx, ax
    jae .last
    mov si, bx
    shl si, 1
    mov ax, [si+sc_upos+2]
    mov [si+sc_upos], ax
    mov ax, [si+sc_uins+2]
    mov [si+sc_uins], ax
    mov ax, [si+sc_udel+2]
    mov [si+sc_udel], ax
    mov ax, [si+sc_uoff+2]
    sub ax, cx
    mov [si+sc_uoff], ax
    inc bx
    jmp short .sh
.last:
    dec byte [sc_un]
    cmp byte [sc_un], 0
    jne .out
    mov byte [sc_uopen], 0      ; the record that went WAS the open one, and
                                ; sc_utop_rec has to say so - every blob
                                ; writer re-asks after making room
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_uroom - make sure CX more blob bytes fit
; in:  CX = the bytes wanted
; out: CF = 0 there is room; CF = 1 there is not and the whole stack has been
;      dropped. Preserves all registers.
; -----------------------------------------------------------------------------
sc_uroom:
    push ax
    push bx
    push cx
    push dx
.try:
    mov ax, [sc_utop]
    add ax, cx
    jc .evict
    mov bx, [sc_ukb]
    push cx
    mov cl, 10
    shl bx, cl
    pop cx
    cmp ax, bx
    jbe .yes
    push cx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    pop cx
    cmp ax, SC_UMAXKB
    ja .evict
    call sc_ugrow
    jnc .yes
.evict:
    cmp byte [sc_un], 0
    je .no
    call sc_udrop0
    jmp short .try
.no:
    call sc_uclear
    stc
    jmp short .out
.yes:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ublob_copy - CX note bytes at SI into the arena at ES:DI
; in:  SI = document offset, DI = arena offset, CX = count, ES = [sc_useg]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_ublob_copy:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    push cx
    push si
    push di
    mov ax, [sc_useg]           ; the text half: document -> arena
    mov es, ax
    mov ax, [sc_dseg]           ; read it BEFORE DS stops being ours
    mov ds, ax
    cld
    rep movsb
    pop di
    pop si
    pop cx
    mov ax, [cs:sc_cuseg]       ; ...and the CHP half at the SAME offsets
    mov es, ax                  ; (SPEC.md 68.3). CS: because DS is the
    mov ax, [cs:sc_cseg]        ; document's segment right now
    mov ds, ax
    rep movsb
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ublob_app - append the CX note bytes at AX to the open record's blob
; in:  AX = document position, CX = count
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_ublob_app:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call sc_uroom               ; may evict, may drop the lot
    jc .out
    call sc_utop_rec            ; re-asked, because an eviction renumbers the
    jc .out                     ; records and may have taken this one
    push si
    mov di, [sc_utop]
    mov si, ax
    mov es, [sc_useg]
    call sc_ublob_copy
    pop si
    mov ax, [sc_utop]
    add ax, cx
    mov [sc_utop], ax
    mov ax, [si+sc_udel]
    add ax, cx
    mov [si+sc_udel], ax
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ublob_pre - put the CX note bytes at AX at the FRONT of the open blob,
;                and start the group CX bytes lower
; in:  AX = document position, CX = count
; out: nothing; preserves all registers
;
; The open record's blob is always the LAST one in the arena, so sliding it up
; disturbs nothing else - which is the whole reason the arena is a stack.
; -----------------------------------------------------------------------------
sc_ublob_pre:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call sc_uroom
    jc .out
    call sc_utop_rec
    jc .out
    mov dx, [si+sc_udel]        ; the blob's current length...
    mov bx, [si+sc_uoff]        ; ...and where it starts
    add [si+sc_udel], cx
    sub [si+sc_upos], cx        ; the group's span starts CX lower now
    mov es, [sc_useg]
    push ax
    mov ax, [sc_utop]
    push dx                     ; the slide's count, run twice (SPEC.md 68.3)
    mov di, ax
    add di, cx
    dec di                      ; the new last byte...
    mov si, ax
    dec si                      ; ...and the old one
.sl:
    or dx, dx
    jz .slid
    mov al, [es:si]
    mov [es:di], al
    dec si
    dec di
    dec dx
    jmp short .sl
.slid:
    pop dx                      ; ...and the CHP arena's copy of the same
    mov es, [sc_cuseg]          ; slide, byte for byte
    mov di, ax
    add di, cx
    dec di
    mov si, ax
    dec si
.csl:
    or dx, dx
    jz .cslid
    mov al, [es:si]
    mov [es:di], al
    dec si
    dec di
    dec dx
    jmp short .csl
.cslid:
    pop ax
    mov si, ax
    mov di, bx
    call sc_ublob_copy
    mov ax, [sc_utop]
    add ax, cx
    mov [sc_utop], ax
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
; sc_ubegin - close whatever is open and start a record at position AX
; in:  AX = the group's start; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_ubegin:
    push ax
    push bx
    push si
    cmp byte [sc_un], SC_UNDO
    jb .have
    call sc_udrop0              ; five deep means the sixth costs the first
.have:
    mov bl, [sc_un]
    xor bh, bh
    mov si, bx
    shl si, 1
    mov [si+sc_upos], ax
    mov word [si+sc_uins], 0
    mov word [si+sc_udel], 0
    mov ax, [sc_utop]
    mov [si+sc_uoff], ax
    inc byte [sc_un]
    mov byte [sc_uopen], 1
    call sc_hire                ; the group's half-second is measured by the
                                ; worker (SPEC.md 27.9), and until this it was
                                ; hired only by the visual break and by a note
                                ; that outgrew its window - so on a machine
                                ; with neither, nothing ever closed a group
                                ; and the whole session was one undo
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_urec_ins - record an insertion of CX bytes at AX
; in:  AX = where, CX = how many; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_urec_ins:
    push ax
    push bx
    push si
    cmp byte [sc_unolog], 0
    jne .out
    jcxz .out
    call sc_utop_rec
    jc .new
    mov bx, [si+sc_upos]
    add bx, [si+sc_uins]
    cmp bx, ax
    jne .new                    ; not where this group left off: a new one
    add [si+sc_uins], cx
    jmp short .out
.new:
    call sc_ubegin
    call sc_utop_rec
    jc .out
    mov [si+sc_uins], cx
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_urec_del - record the removal of CX bytes at AX, copying them into the
;               arena while they are still in the note
; in:  AX = where, CX = how many; out: nothing; preserves all registers
;
; Four cases, tested in this order, and the order matters:
;   1. the bytes are ones THIS group inserted - they were never in the note
;      before it, so nothing reaches the blob and [sc_uins] simply shrinks.
;      This is a backspace walking back over what was just typed;
;   2. they sit immediately AFTER the group's span - original text, so they
;      belong at the END of the blob. This is forward Delete while typing;
;   3. immediately BEFORE it - a backspace walking left out of the group - so
;      they belong at the FRONT of it and the span starts lower;
;   4. anywhere else: a new group.
; Case 1 has to come first because when [sc_uins] is 0 cases 2 and 3 can both
; look true, and taking 1 when it does not apply would forget a deletion.
; -----------------------------------------------------------------------------
sc_urec_del:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [sc_unolog], 0
    jne .out
    jcxz .out
    call sc_utop_rec
    jc .new
    mov dx, [si+sc_upos]
    add dx, [si+sc_uins]        ; DX = one past the group's span
    mov bx, ax
    add bx, cx                  ; BX = one past the deletion
    cmp bx, dx
    jne .try2
    cmp cx, [si+sc_uins]
    ja .try2
    sub [si+sc_uins], cx        ; 1.
    jmp short .out
.try2:
    cmp ax, dx
    jne .try3
    call sc_ublob_app           ; 2.
    jmp short .out
.try3:
    cmp bx, [si+sc_upos]
    jne .new
    call sc_ublob_pre           ; 3.
    jmp short .out
.new:
    call sc_ubegin              ; 4. AX is still the position
    call sc_ublob_app
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_urec_bulk - record a whole-span replacement, for the operations that
;                rewrite more than one place at once
; in:  AX = where the change begins, CX = the bytes there NOW that are about
;      to be replaced (Replace All passes the whole tail of the note)
; out: CF = 1 = it could not be recorded, and the stack has been dropped;
;      preserves all registers
;
; Drag-and-drop and Replace All both move text at two positions at once, which
; the (pos, inserted, removed) record cannot say. Saying it as ONE replacement
; of everything between them can, exactly, at the price of a bigger blob - and
; SC_UMAXKB is 16 so that a Replace All over a full note still fits.
; sc_urec_bulkend closes it with the new length.
; -----------------------------------------------------------------------------
sc_urec_bulk:
    push ax
    push bx
    push cx
    push si
    call sc_uclose              ; a bulk change is never part of a typing run
    call sc_ubegin
    call sc_ublob_app
    call sc_utop_rec
    jc .no
    cmp [si+sc_udel], cx        ; sc_ublob_app is allowed to give up
    jne .no
    clc
    jmp short .out
.no:
    call sc_uclear
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sc_urec_bulkend - in: CX = the bytes that replaced them. Preserves all.
sc_urec_bulkend:
    push bx
    push si
    call sc_utop_rec
    jc .out
    mov [si+sc_uins], cx
    call sc_uclose
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_undo - take the newest group back (SPEC.md 27.9)
; out: CF = 1 there was nothing to undo; preserves all registers
; -----------------------------------------------------------------------------
sc_undo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call sc_uclose
    cmp byte [sc_un], 0
    je .no
    mov byte [sc_dirty], 1      ; undone is still different from the disk
    mov bl, [sc_un]
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, bx
    mov byte [sc_unolog], 1     ; an undo is not an edit to be remembered
    mov ax, [si+sc_upos]
    mov cx, [si+sc_uins]
    mov di, [si+sc_uoff]
    mov dx, [si+sc_udel]
    push dx                     ; the blob's length, wanted twice below
    push di                     ; ...and where it is
    push ax                     ; ...and where all of this happens
    jcxz .noins
    mov bx, ax
    call sc_delspan             ; the group's insertion comes back out
.noins:
    pop ax
    pop di
    pop dx
    push ax
    mov cx, dx
    jcxz .nodel
    mov bx, ax
    call sc_gaproom             ; ...and the bytes it removed go back in
    jc .fail
    mov si, di
    mov di, bx
    push ds
    push es
    push cx                     ; the copy runs twice - text arena into the
    push si                     ; document, CHP arena into the CHP claim,
    push di                     ; same offsets both times (SPEC.md 68.3)
    mov es, [sc_dseg]           ; both segments loaded while DS is still ours
    mov ax, [sc_useg]
    mov ds, ax
    cld
    rep movsb
    pop di
    pop si
    pop cx
    mov ax, [cs:sc_cseg]        ; CS: - DS is the arena's segment here
    mov es, ax
    mov ax, [cs:sc_cuseg]
    mov ds, ax
    rep movsb
    pop es
    pop ds
.nodel:
    pop ax
    add ax, dx                  ; the caret lands at the end of what came back
    mov [sc_cur], ax
    call sc_selclr
    mov ax, [sc_utop]
    sub ax, dx
    mov [sc_utop], ax           ; the blob is the arena's last, so this is all
    dec byte [sc_un]            ; there is to giving it back
    mov byte [sc_uopen], 0
    mov byte [sc_unolog], 0
    call sc_editinv
    clc
    jmp short .out
.fail:
    pop ax                      ; the gap was refused: the record stays where
    mov byte [sc_unolog], 0     ; it is and the note is short of its insertion,
    call sc_editinv             ; which is the honest half of an undo rather
    clc                         ; than a hang
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The search engine (SPEC.md 68.7)
;
; A LITERAL scan over the note with the authentic search.des options - Whole
; Word and Match Upper/Lowercase - and one wrap. Word 1.1 had no regex; the
; engine Note Pad carried (and its docked panel) is deleted here, the bytes
; reclaimed, and Edit > Search... / Replace... / Go To... are modal dialogs
; on the SPEC.md 68.3 framework.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_docb - AL = the note's byte at index DI
; sc_docb2 - ...and at index AX
; both preserve every other register
; -----------------------------------------------------------------------------
sc_docb:
    push bx
    push es
    mov es, [sc_dseg]
    mov bx, di
    mov al, [es:bx]
    pop es
    pop bx
    ret
sc_docb2:
    push bx
    push es
    mov es, [sc_dseg]
    mov bx, ax
    mov al, [es:bx]
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_foldc - AL folded for a caseless compare: a-z -> A-Z (SPEC.md 68.7)
; preserves everything else
; -----------------------------------------------------------------------------
sc_foldc:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; -----------------------------------------------------------------------------
; sc_iswordc - CF = 1 when AL is a word character: A-Z a-z 0-9 (SPEC.md 68.7)
; preserves all registers
; -----------------------------------------------------------------------------
sc_iswordc:
    cmp al, '0'
    jb .no
    cmp al, '9'
    jbe .yes
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    jbe .yes
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
.yes:
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; sc_matchat - does the search text match the note at index AX? (SPEC.md 68.7)
; in:  AX = the index; the text in sc_fpat, options in [sc_fopt]
; out: CF = 0 with [sc_rxend] = one past the match; CF = 1 = no match HERE
;      (this asks about one position - sc_findfrom is what walks).
;      Preserves all registers.
;
; A LITERAL scan - Word 1.1 had no regex and neither does this port (the
; engine Note Pad carried is gone with its panel, SPEC.md 68.7). Match
; Upper/Lowercase clear folds BOTH sides through sc_foldc; Whole Word demands
; a non-word character (or the document's edge) on each side of the match.
; -----------------------------------------------------------------------------
sc_matchat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si                  ; SI = the compiled pattern's index
    mov di, ax
    mov dx, ax                  ; DX = the start, for the left boundary test
.ll:
    cmp si, [sc_fpwn]
    jae .tail
    mov bx, si
    shl bx, 1
    mov bx, [bx+sc_fpw]         ; BX = the compiled element
    cmp bx, SCP_WHITE
    je .white
    mov ax, di
    cmp ax, [sc_len]
    jae .fail
    call sc_docb                ; AL = the note at DI
    cmp bx, SCP_ANY
    je .step                    ; '?' matches any one character
    test byte [sc_fopt], SCFO_CASE
    jnz .exact
    call sc_foldc               ; fold the note's byte...
    xchg al, bl
    call sc_foldc               ; ...and the pattern's
    xchg al, bl
.exact:
    cmp al, bl
    jne .fail
.step:
    inc si
    inc di
    jmp short .ll
.white:
    mov ax, di                  ; '^w' matches one or MORE white characters,
    cmp ax, [sc_len]            ; greedily, and counts as one element
    jae .fail
    call sc_docb
    call sc_iswhite
    jc .fail
.wmore:
    inc di
    mov ax, di
    cmp ax, [sc_len]
    jae .wdone
    call sc_docb
    call sc_iswhite
    jnc .wmore
.wdone:
    inc si
    jmp short .ll
.tail:
    test byte [sc_fopt], SCFO_WORD
    jz .match
    or dx, dx                   ; Whole Word: the character left of the
    jz .rgt                     ; match (the edge counts as non-word)...
    mov ax, dx
    dec ax
    call sc_docb2
    call sc_iswordc
    jc .fail
.rgt:
    mov ax, di                  ; ...and the one after it
    cmp ax, [sc_len]
    jae .match
    call sc_docb2
    call sc_iswordc
    jc .fail
.match:
    mov [sc_rxend], di
    clc
    jmp short .out
.fail:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_iswhite - CF=0 if AL is white space: FMatchWhiteSpace's own set, less
; the non-breaking space this character set has no room for (SPEC.md 68.7).
; Preserves all registers.
sc_iswhite:
    cmp al, ' '
    je .yes
    cmp al, 9
    je .yes
    stc
    ret
.yes:
    clc
    ret

; =============================================================================
; Finding, and replacing (SPEC.md 27.10)
; =============================================================================

; -----------------------------------------------------------------------------
; sc_findfrom - the first match at or after index AX, wrapping to the top
; in:  AX = where to start looking
; out: CF = 0 with AX = the match's start and DX = one past its end;
;      CF = 1 = the text is not in the note at all. Preserves all others.
;
; The wrap is unconditional and is the whole of what "loops once" means: the
; second pass stops where the first began, so a text that occurs once is
; found from anywhere and one that occurs nowhere is refused after exactly
; one traversal (SPEC.md 68.7).
; -----------------------------------------------------------------------------
sc_findfrom:
    push bx
    push cx
    push si
    push di
    cmp word [sc_fpatn], 0
    je .no
    mov bx, ax
.p1:
    cmp ax, [sc_len]
    ja .wrap
    call sc_matchat
    jnc .hit
    inc ax
    jmp short .p1
.wrap:
    xor ax, ax                  ; past the end and back to the top
.p2:
    cmp ax, bx
    jae .no
    call sc_matchat
    jnc .hit
    inc ax
    jmp short .p2
.hit:
    mov dx, [sc_rxend]
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_findfwd - the first match at or after index AX, NO wrap (SPEC.md 68.7)
; in:  AX = where to start; out: as sc_findfrom (AX = start, DX = end)
; The replace sweeps' walker: a sweep that wrapped would re-visit its own
; replacements.
; -----------------------------------------------------------------------------
sc_findfwd:
    cmp word [sc_fpatn], 0
    je .no
.l:
    cmp ax, [sc_len]
    ja .no
    call sc_matchat
    jnc .hit
    inc ax
    jmp short .l
.hit:
    mov dx, [sc_rxend]
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_findprev - the last match that STARTS before index AX, wrapping to the
;               bottom of the note
; in:  AX = the limit
; out: as sc_findfrom
;
; One forward walk, remembering the last match seen before the limit and the
; last one seen at all. It steps match-to-match rather than character-to-
; character, so the pass is bounded by the number of matches.
; -----------------------------------------------------------------------------
sc_findprev:
    push bx
    push cx
    push si
    push di
    cmp word [sc_fpatn], 0
    je .no
    mov bx, ax                  ; BX = the limit
    mov cx, 0xFFFF              ; CX = the best answer before it...
    mov si, 0xFFFF              ; SI = ...and the last one anywhere
    xor ax, ax
.l:
    cmp ax, [sc_len]
    ja .done
    call sc_matchat
    jc .step1
    mov si, ax
    cmp ax, bx
    jae .adv
    mov cx, ax
.adv:
    mov di, [sc_rxend]          ; matches are counted without overlapping
    cmp di, ax
    ja .setax
    mov di, ax
    inc di
.setax:
    mov ax, di
    jmp short .l
.step1:
    inc ax
    jmp short .l
.done:
    cmp cx, 0xFFFF
    jne .have
    mov cx, si                  ; nothing before the limit: wrap to the last
    cmp cx, 0xFFFF              ; match in the note
    je .no
.have:
    mov ax, cx
    call sc_matchat             ; re-run it for its end, which is one match's
    jc .no                      ; worth of work rather than a second array
    mov dx, [sc_rxend]
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_showmatch - select [AX, DX) and make sure it is on screen
; in:  AX = start, DX = end, SI = window ptr
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_showmatch:
    push ax
    push dx
    mov [sc_fmst], ax
    mov [sc_fmen], dx
    call sc_selset
    mov [sc_cur], dx            ; the caret sits at the END of it, so F4 twice
                                ; walks forwards rather than finding the same
                                ; match again
    mov byte [sc_follow], 1     ; sc_redraw scrolls it into view for us
    mov byte [sc_ckok], 0       ; the checkpoint is the CARET's row start and
    pop dx                      ; the caret just jumped somewhere else
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_donext - F4: the next match after the caret (SPEC.md 68.7)
; sc_doprev - Shift-F4: the last one before it
; in:  SI = window ptr; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
sc_donext:
    call sc_uclose
    mov ax, [sc_cur]
    call sc_findfrom
    jc sc_fmiss
    call sc_showmatch
    ret

sc_doprev:
    call sc_uclose
    mov ax, [sc_sel0]
    cmp byte [sc_selon], 0
    jne .have
    mov ax, [sc_cur]
.have:
    call sc_findprev
    jc sc_fmiss
    call sc_showmatch
    ret

; sc_fmiss - say why nothing happened (SPEC.md 68.7's authentic string).
; Preserves all registers.
sc_fmiss:
    push ax
    mov ax, sc_m_nfound
    cmp word [sc_fpatn], 0
    jne .say
    mov ax, sc_m_nopat
.say:
    call sc_saymsg
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_replat - swap the CX bytes at AX for the replacement text
; in:  AX = where, CX = how many bytes go
; out: CF = 0 with DX = one past the text that replaced them; CF = 1 refused
;      and nothing changed. Preserves all other registers.
; -----------------------------------------------------------------------------
sc_replat:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, ax
    push ax                     ; ^m and ^c are resolved against THIS match
    push cx                     ; (SPEC.md 68.7), so the bytes spliced in are
    mov [sc_fmst], ax           ; built here and not taken from the dialog
    add ax, cx
    mov [sc_fmen], ax
    call sc_rxpand
    pop cx
    pop ax
    jc .nox
                                ; THE ROOM COMES FIRST, and the order is the
                                ; whole of this routine's promise. sc_delspan
                                ; used to run before sc_gaproom, so a refused
                                ; claim returned CF=1 with the match ALREADY
                                ; GONE - which the header calls "nothing
                                ; changed". Reachable without a claim failure
                                ; at all: a note loaded at SC_MAXKB has
                                ; sc_len == sc_cap, so every replacement longer
                                ; than its match is refused, and the caller's
                                ; `jc` then skips sc_editinv with [sc_cur] left
                                ; at the match END - past [sc_len] for a match
                                ; at the end of the note, and the next
                                ; keystroke walks 65,535 bytes backwards
                                ; through the document claim.
    mov ax, [sc_len]
    sub ax, cx                  ; what the note becomes: the span goes...
    add ax, [sc_frxn]           ; ...and the replacement arrives
    jc .no                      ; a 16-bit note cannot pass 65,535
    push bx
    push cx
    call sc_capfor              ; non-destructive, and preserves everything
    pop cx
    pop bx
    jc .no                      ; refused with the match still there
    push bx
    call sc_delspan             ; out with the old...
    pop bx
    mov cx, [sc_frxn]
    push cx
    call sc_gaproom             ; ...and in with the new, which can no longer
    pop cx                      ; be refused for want of room
    jc .no
    mov es, [sc_dseg]
    mov di, bx
    mov si, sc_frx
    jcxz .filled
.f:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .f
.filled:
    mov ax, bx
    mov cx, [sc_frxn]
    call sc_urec_ins
    mov dx, bx
    add dx, [sc_frxn]
    clc
    jmp short .out
.nox:
    mov bx, ax                  ; the expansion refused and said why
.no:
    mov dx, bx
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret


; -----------------------------------------------------------------------------
; sc_dorepall - replace every match, from the top of the note
; in:  SI = window ptr; out: nothing; clobbers what a callback may
;
; ONE undo record for the whole sweep, and it has to be one: the operation
; nobody wants to retype by hand is exactly the one worth being able to take
; back, and five separate records would only take back the last five. It is
; recorded as a replacement of everything from the first match to the end of
; the note (SPEC.md 27.9's bulk form), because that is the smallest span the
; three-number record can describe honestly.
; -----------------------------------------------------------------------------
sc_dorepall:
    push ax
    push bx
    push cx
    push dx
    push di
    call sc_uclose
    xor ax, ax
    call sc_findfrom            ; is there anything to do at all?
    jc .miss
    push ax
    mov cx, [sc_len]
    sub cx, ax
    call sc_urec_bulk           ; the tail as it stands, into the blob
    pop ax
    mov byte [sc_unolog], 1     ; every edit below is inside that one record
    xor bx, bx                  ; BX = how many were replaced
    mov di, ax                  ; DI = where the caret ends up
.l:
    cmp ax, [sc_len]
    ja .done
    call sc_matchat
    jc .step
    mov cx, [sc_rxend]
    sub cx, ax
    call sc_replat              ; AX survives; DX = one past the new text
    jc .done                    ; the note cannot take the growth: stop here,
                                ; with everything up to now replaced
    inc bx
    mov di, dx
    cmp dx, ax
    ja .setax
    mov dx, ax                  ; an empty match replaced by nothing would
    inc dx                      ; stand still forever
.setax:
    mov ax, dx
    jmp short .l
.step:
    inc ax
    jmp short .l
.done:
    mov byte [sc_unolog], 0
    call sc_urec_bulkend_at
    mov [sc_cur], di
    call sc_selclr
    call sc_editinv
    mov byte [sc_follow], 1
    mov ax, bx
    call sc_saycnt
    jmp short .out
.miss:
    call sc_fmiss
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_urec_bulkend_at - close the bulk record: what replaced its span is
; everything from the record's own start to the end of the note. This is the
; Replace All shape, where the sweep runs to the end by construction.
; Preserves all registers.
sc_urec_bulkend_at:
    push bx
    push cx
    push si
    call sc_utop_rec
    jc .out
    mov cx, [sc_len]
    sub cx, [si+sc_upos]
    mov [si+sc_uins], cx
    call sc_uclose
.out:
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; Selecting with the pointer, and dropping what was selected (SPEC.md 27.8.1)
;
; ui_drag's shape (SPEC.md 13) written against the API, the way sol_drag is:
; the gfx lock is held for the whole of a pass and released only between them,
; so nothing else can draw over a half-finished frame and the cursor still
; moves. What a pass does depends on where the press landed - outside the
; selection it EXTENDS one, inside it MOVES the text - and the second is why
; the dead zone exists: a plain click inside a selection is still a click, and
; what a click does is put the caret there.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_selpace - drop the lock, wait for the tick, take it back
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_selpace:
    push ax
    push bx
    call OSAPI_GFX_UNLOCK
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .spin
    call OSAPI_GFX_LOCK
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_hitpt - the character index under the pointer, scrolling the view when
;            the pointer has left it
; in:  CX = x, DX = y (absolute), SI = window ptr, sc_bounds run, lock held
; out: AX = the index; CF = 1 if [sc_top] moved as well. Preserves all others.
;
; The scroll is what makes a selection longer than the window possible at all,
; and it is one row a tick because that is the rate the drag loop runs at.
; -----------------------------------------------------------------------------
sc_hitpt:
    push bx
    push cx
    push dx
    xor bx, bx                  ; BX = "the view moved"
    mov ax, [sc_vrows]
    or ax, ax
    jz .hit
    cmp dx, [sc_ty]
    jb .up
    dec ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]             ; AX = the last visible row's top
    cmp dx, ax
    jbe .hit
    mov dx, ax                  ; below the view: clamp, and page down one
    mov ax, [sc_top]
    inc ax
    call sc_scrollto
    jc .hit                     ; CF = 1 from sc_scrollto means it did NOT
    mov bx, 1                   ; move, which is the bottom of the note
    jmp short .hit
.up:
    mov dx, [sc_ty]
    mov ax, [sc_top]
    dec ax
    call sc_scrollto
    jc .hit
    mov bx, 1
.hit:
    mov [sc_hitx], cx
    mov [sc_hity], dx
    mov word [sc_wanty], 0x7FFF
    push ax                     ; the pointer names ONE row, and sc_rows knows
    push dx                     ; where it starts (SPEC.md 27.5) - so seed
    call sc_yrow                ; there and stop after it, which is what
    jc .nseed                   ; sc_onclick has always done for a click; the
    mov dx, ax                  ; ys are banked, so formats answer too, and a
    call sc_seedrow             ; refusal is the unseeded walk (SPEC.md 68.6)
.nseed:
    pop dx
    pop ax
    call sc_measure
    mov byte [sc_resume], 0
    mov ax, [sc_hiti]
    or bx, bx
    jz .noscr
    stc
    jmp short .out
.noscr:
    clc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_dragsel - follow the pointer, extending the selection from [sc_anchor]
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
sc_dragsel:
    push ax
    push bx
    push cx
    push dx
    mov bx, [sc_anchor]         ; what the last pass resolved
    mov word [sc_lmx], 0xFFFF   ; ...and where the pointer was when it did
    mov word [sc_lmy], 0xFFFF
.pass:
    call sc_selpace
    call OSAPI_MOUSE            ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .up
    call sc_bounds
    cmp cx, [sc_lmx]            ; A POINTER THAT HAS NOT MOVED HAS NOTHING TO
    jne .moved                  ; SAY. The loop runs at a tick whether the
    cmp dx, [sc_lmy]            ; mouse reports anything or not, and at 1200
    jne .moved                  ; baud it usually does not
    cmp dx, [sc_ty]
    jb .moved                   ; ...unless it is parked outside the view,
    push ax                     ; where every tick owes another row of scroll
    mov ax, [sc_vrows]
    or ax, ax
    jz .still
    dec ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]             ; the last visible row's top - sc_hitpt's own
    cmp dx, ax                  ; threshold for scrolling, so the two cannot
    ja .scrolling               ; disagree about which passes matter
.still:
    pop ax
    jmp short .pass
.scrolling:
    pop ax
.moved:
    mov [sc_lmx], cx
    mov [sc_lmy], dx
    call sc_hitpt
    jc .draw                    ; the view scrolled: owed a redraw either way
    cmp ax, bx
    je .pass
.draw:
    mov bx, ax
    mov [sc_cur], ax
    mov dx, [sc_anchor]
    call sc_selset
    mov byte [sc_ckok], 0       ; the checkpoint is the CARET's row start and
    mov byte [sc_selonly], 1    ; the caret has just jumped - and a drag moves
                                ; no character, so every dirty row owes an
                                ; inversion and no glyphs (SPEC.md 27.8.2)
    call sc_redraw
    jmp short .pass
.up:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rev - reverse the note's bytes in [AX, BX], inclusive
; in:  AX, BX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_rev:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [sc_dseg]
    call sc_rev1                ; the text...
    mov es, [sc_cseg]           ; ...and its CHP bytes, identically (SPEC.md
    call sc_rev1                ; 65.3): the rotate moves dress with glyphs
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sc_rev1 - reverse ES:[AX..BX] inclusive; preserves AX/BX, clobbers CX/SI/DI
sc_rev1:
    mov si, ax
    mov di, bx
.l:
    cmp si, di
    jae .out
    mov cl, [es:si]
    mov ch, [es:di]
    mov [es:si], ch
    mov [es:di], cl
    inc si
    dec di
    jmp short .l
.out:
    ret

; -----------------------------------------------------------------------------
; sc_dmark_on / sc_dmark_off - the drop point's insertion bar
; in:  [sc_dpos] (0xFFFF = none), SI = window ptr, lock held
; out: nothing; preserves all registers
;
; XOR, so the erase is the draw again - and the pixels it was drawn at are
; BANKED rather than recomputed, because a scroll between the two would move
; where the bar belongs and leave the old one behind for good. That is
; SPEC.md 48.11's rule in miniature.
; -----------------------------------------------------------------------------
sc_dmark_on:
    push ax
    push bx
    push cx
    push dx
    mov word [sc_dmark], 0xFFFF
    mov ax, [sc_dpos]
    cmp ax, 0xFFFF
    je .out
    mov bx, [sc_cur]
    push bx
    mov [sc_cur], ax            ; sc_walk answers where the CARET is, so the
    mov word [sc_hity], 0xFFFF  ; drop point borrows it for one measure pass
    mov word [sc_wanty], 0x7FFF
    call sc_measure
    pop bx
    mov [sc_cur], bx
    mov byte [sc_ckok], 0       ; ...and the checkpoint that pass left behind
                                ; describes the drop point's row, not ours
    cmp byte [sc_curseen], 0
    je .out
    mov ax, [sc_curx]
    mov bx, [sc_cury]
    cmp bx, [sc_ty]
    jb .out
    mov dx, bx
    add dx, [sc_gh1]
    cmp dx, [sc_bot]
    ja .out
    mov cx, ax
    call OSAPI_GFX_XOR_FILL
    mov [sc_dmark], ax
    mov [sc_dmy], bx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sc_dmark_off:
    push ax
    push bx
    push cx
    push dx
    mov ax, [sc_dmark]
    cmp ax, 0xFFFF
    je .out
    mov bx, [sc_dmy]
    mov cx, ax
    mov dx, bx
    add dx, [sc_gh1]
    call OSAPI_GFX_XOR_FILL
    mov word [sc_dmark], 0xFFFF
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_movesel - move the selected text to [sc_dpos]
; in:  SI = window ptr; out: nothing; clobbers as a callback
;
; THREE IN-PLACE REVERSALS AND NO BUFFER. Reversing [s,e), then the run beside
; it, then the two together, rotates the block to the far end of the span -
; which is exactly what a move is. The alternative was a staging claim the
; size of the selection, and a drag that fails because the heap is busy is a
; drag the user has no way to understand.
;
; The undo record is the bulk form (SPEC.md 27.9): one replacement of the
; whole span between the old place and the new, because a (pos, in, out)
; record cannot say "these bytes went from here to there".
; -----------------------------------------------------------------------------
sc_movesel:
    push ax
    push bx
    push cx
    push dx
    call sc_selget              ; AX = s, CX = n
    jc .out
    mov [sc_mvs], ax
    mov [sc_mvn], cx
    add ax, cx
    mov [sc_mve], ax
    mov ax, [sc_dpos]
    mov [sc_mvp], ax
    cmp ax, [sc_mvs]
    jb .left
    cmp ax, [sc_mve]
    jbe .out                    ; dropped inside itself: nothing to do
    mov ax, [sc_mvs]            ; --- rightwards: [s,e)[e,p) -> [e,p)[s,e) ---
    mov cx, [sc_mvp]
    sub cx, ax
    call sc_urec_bulk           ; the whole span [s, p), as one replacement
    mov byte [sc_unolog], 1
    mov ax, [sc_mvs]
    mov bx, [sc_mve]
    dec bx
    call sc_rev
    mov ax, [sc_mve]
    mov bx, [sc_mvp]
    dec bx
    call sc_rev
    mov ax, [sc_mvs]
    mov bx, [sc_mvp]
    dec bx
    call sc_rev
    mov ax, [sc_mvp]
    sub ax, [sc_mvn]            ; the block ends where it was dropped
    jmp short .fin
.left:                          ; --- leftwards: [p,s)[s,e) -> [s,e)[p,s) ---
    mov ax, [sc_mvp]
    mov cx, [sc_mve]
    sub cx, ax
    call sc_urec_bulk           ; the whole span [p, e)
    mov byte [sc_unolog], 1
    mov ax, [sc_mvp]
    mov bx, [sc_mvs]
    dec bx
    call sc_rev
    mov ax, [sc_mvs]
    mov bx, [sc_mve]
    dec bx
    call sc_rev
    mov ax, [sc_mvp]
    mov bx, [sc_mve]
    dec bx
    call sc_rev
    mov ax, [sc_mvp]            ; ...and here it begins where it was dropped
.fin:
    mov byte [sc_unolog], 0
    mov dx, ax
    add dx, [sc_mvn]
    call sc_selset              ; it stays selected, which is what the user
    mov [sc_cur], dx            ; just spent a drag pointing at
    call sc_urec_bulkend_span
    call sc_editinv
    mov byte [sc_follow], 1
    mov byte [sc_ckok], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_urec_bulkend_span - close a bulk record whose span did not change length
; (a move rearranges bytes, it does not add or remove any). Preserves all.
sc_urec_bulkend_span:
    push bx
    push cx
    push si
    call sc_utop_rec
    jc .out
    mov cx, [si+sc_udel]
    mov [si+sc_uins], cx
    call sc_uclose
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_dragmove - a press that landed INSIDE the selection
; in:  AX = the index pressed, SI = window ptr, lock held
; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
sc_dragmove:
    push ax
    push bx
    push cx
    push dx
    mov [sc_anchor], ax         ; where the press was, for the click case
    mov word [sc_dpos], 0xFFFF
    mov word [sc_dmark], 0xFFFF
    call OSAPI_MOUSE
    mov [sc_dpx], cx
    mov [sc_dpy], dx
    xor bx, bx                  ; BX = the pointer has left the dead zone
.pass:
    call sc_selpace
    call OSAPI_MOUSE
    test al, 1
    jz .up
    or bx, bx
    jnz .track
    mov ax, cx
    sub ax, [sc_dpx]
    call sc_absw
    cmp ax, SC_SELDRAG
    ja .moved
    mov ax, dx
    sub ax, [sc_dpy]
    call sc_absw
    cmp ax, SC_SELDRAG
    jbe .pass
.moved:
    mov bx, 1
.track:
    call sc_bounds
    call sc_hitpt
    jnc .same
    mov word [sc_dmark], 0xFFFF ; the view scrolled out from under the bar
    mov word [sc_dpos], 0xFFFF
    call sc_redraw
.same:
    cmp ax, [sc_dpos]
    je .pass
    call sc_dmark_off
    mov [sc_dpos], ax
    call sc_dmark_on
    jmp short .pass
.up:
    call sc_dmark_off
    or bx, bx
    jz .click
    cmp word [sc_dpos], 0xFFFF
    je .click
    call sc_movesel
    call sc_redraw
    jmp short .out
.click:
    mov ax, [sc_anchor]         ; never left the dead zone: it was a click,
    mov [sc_cur], ax            ; and a click puts the caret where it landed
    call sc_selclr
    mov byte [sc_ckok], 0
    call sc_chpsync             ; ...and the typing attrs with it (65.3)
    call sc_redraw
.out:
    mov word [sc_dpos], 0xFFFF
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_absw - AX = |AX|, treating it as signed. Preserves all other registers.
sc_absw:
    or ax, ax
    jns .out
    neg ax
.out:
    ret

; =============================================================================
; A View toggle MOVES the text band (SPEC.md 68.2; this blit once served the
; find panel, SPEC.md 27.10.2 - the panel is gone, the mechanism stays)
;
; A ribbon/ruler toggle changes exactly one number - the chrome top sc_bounds
; adds to [sc_ty] - and changes NOTHING about the wrap: [sc_tx] and [sc_rgt]
; are the content's own edges. So every row holds exactly the same characters
; before and after; they are simply H pixels lower or higher, and the view
; keeps the same [sc_top]. That makes the whole repaint a BLIT: one
; OSAPI_GFX_SCROLL plus, on a close, the rows the text moving up EXPOSES at
; the bottom - the only part that was never on screen.

;
; Three things make it safe, and all three are refusals rather than
; corrections, because a wrong blit shows text that was never in the note:
;
;  - **Only [sc_ty] may have moved.** [sc_tx], [sc_rgt] and [sc_bot] are
;    compared against what sc_sigmark recorded; a resize that happens to
;    coincide falls back.
;  - **[sc_top] must not be clamped.** Closing GROWS [sc_vrows], which shrinks
;    sc_scrollmax, and a view that has to move renames every row. Opening
;    shrinks vrows and so can never need it.
;  - **A toast, the visual break and stale signatures all refuse.** The toast
;    is drawn over the text at a y the panel moves, so the blit would carry it
;    to the wrong place and nothing would put it back - sc_scrollpaint refuses
;    for the same reason (SPEC.md 27.7.2).
;
; The panel's OWN pixels are inside the band that moves, which is what makes
; closing leave nothing behind: they blit off the top of the content and are
; clipped. What the band cannot reach is the <8px left margin the x-rounding
; gives up and the scroll bar's columns; both are repainted afterwards, which
; sc_sbar was going to do anyway because the thumb has moved.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_panmove - move the text to where the panel now leaves room for it
; in:  SI = window ptr, sc_bounds ALREADY run for the NEW geometry, gfx lock
;      held
; out: CF = 0 the screen is correct and the signatures describe it; CF = 1
;      nothing was drawn and the caller must repaint in full.
;      Clobbers what a window callback may.
; -----------------------------------------------------------------------------
sc_panmove:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [sc_sigok], 0
    je .no                      ; the arrays do not describe the screen
    cmp byte [sc_bmode], 0
    jne .no
    cmp byte [sc_hasfmt], 0     ; formatted rows land at arbitrary ys and the
    jne .no                     ; exposed-row arithmetic below is 8px: the
                                ; full repaint is the honest path (SPEC.md
                                ; 65.6) - a panel or View toggle is rare
    mov ax, [sc_tx]             ; only [sc_ty] may have moved
    cmp ax, [sc_stx]
    jne .no
    mov ax, [sc_rgt]
    cmp ax, [sc_srgt]
    jne .no
    mov ax, [sc_bot]
    cmp ax, [sc_sbot]
    jne .no
    cmp word [sc_vrows], 0
    je .no
    mov di, [sc_ty]
    sub di, [sc_sty]            ; DI = how far the text is moving, + = down
    jz .no
    jns .open
    call sc_scrollmax           ; closing: the view has more rows to show, so
    cmp [sc_top], ax            ; [sc_top] may be past the new maximum - and a
    ja .no                      ; view that moves renames every row
.open:

    mov bx, si                  ; the band is everything below the chrome
    call OSAPI_WM_CONTENT       ; strips, so the panel's own pixels move with
    push ax                     ; the text and the menu bar never rides a
    mov ax, [sc_sctop]          ; blit (SPEC.md 68.2). The band's top is the
    cmp ax, [sc_ctop]           ; SMALLER of the chrome top the screen was
    jbe .stop                   ; drawn under and the live one: for a panel
    mov ax, [sc_ctop]           ; open/close the two are equal (the old
.stop:                          ; behaviour exactly), and for a View toggle
    add dx, ax                  ; the smaller top is what keeps every moving
    pop ax                      ; source row inside the blit's rect - the
                                ; strip rows it vacates are chrome in the new
                                ; layout and sc_vtoggle repaints them.
    push ax                     ; AX = left, DX = the band's top
    push dx
    mov ax, [sc_tx]
    and ax, 0xFFF8              ; x1 down to a byte column - [sc_tx] and not
                                ; the content's own left edge, because
                                ; rounding THAT down would leave the content
    mov cx, [sc_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                      ; x2, with x2+1 up to a byte column
    pop bx                      ; y1 = content top
    push bx
    mov dx, [sc_bot]            ; y2 = content bottom
    push si
    mov si, di
    neg si                      ; OSAPI_GFX_SCROLL's positive is text UP
    call OSAPI_GFX_SCROLL
    pop si
    pop dx                      ; content top
    pop ax                      ; content left
    jc .no                      ; refused, and having drawn nothing

    push ax                     ; --- the left margin the rounding gave up ---
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop dx
    pop ax
    mov bx, dx                  ; y1 = the band's top
    mov cx, [sc_tx]
    and cx, 0xFFF8
    dec cx                      ; x2 = just left of the band
    mov dx, [sc_bot]
    cmp ax, cx
    jg .noleft
    call OSAPI_GFX_FILL
.noleft:

    ; --- ...and the bar columns the rounding could not reach ----------------
    ; x2+1 rounds up at most seven columns into the scroll bar, so the
    ; RIGHTMOST bar columns between the band's top and the bar's new top
    ; keep whatever the old layout drew there - the panel's close box on a
    ; close, the old bar's top frame on an open: an 8-row fragment either
    ; way, parked just above the up arrow. White the whole bar width above
    ; [sc_ty]; sc_sbar redraws the bar from [sc_ty] down at .done anyway.
    mov ax, [sc_rgt]
    inc ax                      ; x1 = the first bar column
    mov cx, [sc_sbr]            ; x2 = the content's right edge
    mov dx, [sc_ty]
    dec dx                      ; y2 = just above the bar's new top
    cmp bx, dx                  ; y1 = the band's top, still in BX
    jg .nobar
    call OSAPI_GFX_FILL
.nobar:

    or di, di
    js .close

    ; --- the band moved DOWN (a chrome strip came back): the strip the text
    ; left behind is chrome in the new layout and sc_vtoggle repaints it. The
    ; rows pushed past the bottom are simply gone, which is what a smaller
    ; view means. All that is owed below is the sliver under the last whole
    ; row, where the blit left a slice of the row that used to be there.
    jmp .tail

.close:
    ; --- CLOSING: the text moved UP, so the bottom of the view is stale ----
    ; The first row that needs lettering is the one whose pixels reach the
    ; band the blit could not fill: (bot - |d| + 1 - ty) >> 3.
    mov ax, di
    neg ax
    mov bx, [sc_bot]
    sub bx, ax
    inc bx
    sub bx, [sc_ty]
    jns .r0
    xor bx, bx
.r0:
    mov cl, 3
    shr bx, cl                  ; BX = the first exposed row
    cmp bx, [sc_vrows]
    jae .tail                   ; nothing was exposed
    mov [sc_dr0], bx
    mov ax, [sc_vrows]
    dec ax
    mov [sc_dr1], ax

    push cx                     ; erase the band: OSAPI_GFX_SCROLL leaves what
    mov ax, bx                  ; it vacates holding a copy of its neighbour
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]
    mov bx, ax                  ; y1
    mov dx, [sc_bot]            ; y2 - to the very bottom, so the sliver under
                                ; the last whole row goes too
    mov ax, [sc_tx]
    sub ax, SC_MARGIN           ; x1 = the content's own left edge
    mov cx, [sc_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [sc_prowi], 0xFFFF ; the fill erased what the delta cache knew

    mov word [sc_hity], 0xFFFF  ; one pass, drawing AND re-signing: the band
    mov word [sc_wanty], 0x7FFF ; was just filled, so sc_clean
    mov byte [sc_draw], 1
    mov byte [sc_sigup], 1
    mov byte [sc_clip], 1
    mov byte [sc_clean], 1
    mov byte [sc_resume], 0
    mov ax, [sc_vrows]
    mov [sc_lastrow], ax
    call sc_walk
    mov byte [sc_clip], 0
    mov byte [sc_clean], 0
    jmp short .done

.tail:
    ; the sliver under the last whole row, which after a move DOWN holds a
    ; slice of the row that used to be there
    push cx
    mov ax, [sc_vrows]
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_ty]
    cmp ax, [sc_bot]
    ja .done
    mov bx, ax                  ; y1
    mov dx, [sc_bot]            ; y2
    mov ax, [sc_tx]
    sub ax, SC_MARGIN
    mov cx, [sc_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL

.done:
    push cx                     ; the blit moved every row's pixels by DI:
    push si                     ; keep the banked ys truthful (SPEC.md 68.6) -
    mov ax, di                  ; unread while uniform, but the bank must not
    mov cx, [sc_vrows]          ; go stale across a later 0 -> 1 transition
    jcxz .noadj
    mov si, sc_ryb
.adj:
    add [si], ax
    add si, 2
    loop .adj
.noadj:
    pop si
    pop cx
    call sc_sbar                ; unconditional: the track changed height, and
                                ; the blit reached into the bar's columns
    mov bx, si
    call OSAPI_WM_GROW          ; ...and the corner it sits in
    call sc_sigmark             ; the signatures describe THIS geometry now,
    mov ax, [sc_top]            ; and [sc_gchg] is spent with them
    mov [sc_ptop], ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sc_redrawall:
    call sc_bounds              ; the NEW geometry, so sc_panmove can compare
    call sc_panmove             ; it against what the signatures were taken at
    jnc .out                    ; (SPEC.md 27.10.2): the panel only moves the
                                ; text, so MOVE it rather than draw it again
    mov byte [sc_sigok], 0
    mov word [sc_prowi], 0xFFFF
    mov byte [sc_ckok], 0
    mov byte [sc_rowsok], 0
    call sc_redraw
.out:
    ret

; =============================================================================
; Small change (SPEC.md 27.8/27.10)
; =============================================================================

; sc_saymsg - AX = a NUL string -> the system toast (SPEC.md 59).
; Preserves all registers AND the flags: two callers are error paths that
; carry their answer in CF.
;
; This was six words of state and a drawing routine of its own - [sc_msg],
; [sc_msgn]'s generation counter, four box coordinates, sc_toast, the
; sc_smsg/sc_smsgn shadow and sc_sigsame's two tests of it. The toast is in
; the MENU BAR now, so it is in none of this app's pixels: it survives a
; repaint without help, it cannot be carried off its frame by a scroll blit,
; and it cannot leave the incremental path disagreeing with W_PAINT - which
; is what forced a FULL content repaint on the first keystroke after every
; save and every load (SPEC.md 59.1).
sc_saymsg:
    push ax
    push cx
    push si
    push es
    pushf
    mov si, ax
    push ds
    pop es                      ; the kernel COPIES the string, so sc_tbuf may
    xor cx, cx                  ; be reused freely and this app may close
    call OSAPI_TOAST            ; while the message is still up. CX = 0: the
    popf                        ; default lifetime, about three seconds
    pop es
    pop si
    pop cx
    pop ax
    ret

; sc_utoa - AX as decimal at DI, no leading zeros; DI advances past it.
; Preserves every other register.
sc_utoa:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .emit
    mov byte [di], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_saycnt - "<AX> replaced" as the toast. Preserves all registers.
sc_saycnt:
    push ax
    push si
    push di
    mov di, sc_tbuf
    call sc_utoa
    mov si, sc_m_repld
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .cp
    mov ax, sc_tbuf
    call sc_saymsg
    pop di
    pop si
    pop ax
    ret

; =============================================================================
; The Word chrome (SPEC.md 68.2): the nine-title menu bar and its dropdowns,
; the ribbon, the ruler and the status line, all drawn IN the window.
;
; MENU_APPMAX is five and Word's bar is nine menus, so the bar is ours: the
; kernel bar carries only the app-name pull-down (About/Close, SPEC.md 12.2).
; Every menu string below is verbatim from Opus's menus.cmd; the ribbon and
; ruler layout follow ibdefs.h; the shortcut captions are the keys.cmd
; bindings SetBcmMenuKeys appended at runtime. Items whose feature does not
; exist on this port are PRESENT AND DISABLED with the disabled pen
; (OSAPI_GFX_PEN CF=1, SPEC.md 47) - grey a fact, and dither it at 1bpp.
;
; One geometry, banked: sc_mgeo computes the open dropdown's rectangle once
; into sc_mrect, and the painter (sc_mdraw), the hit test (sc_mfind), the
; highlight (sc_mhl) and the close repaint (sc_mrepair) all read those four
; words - the fm_hit discipline (SPEC.md 22). Closing a dropdown repaints
; exactly what it covered: the strips it overlapped, the find-panel band, the
; text ROWS under it (one fill + one clipped walk), the scroll bar and the
; status line - never the whole window. The ribbon/ruler combos reuse the
; whole machinery as one-entry pseudo-menus.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_mgeti - AL = menu index -> BX = its sc_mtab descriptor
; preserves everything else
; -----------------------------------------------------------------------------
sc_mgeti:
    push ax
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov bx, ax
    add bx, sc_mtab
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mitemp - AL = item index, BX = descriptor -> SI = the 8-byte item record
; preserves everything else
; -----------------------------------------------------------------------------
sc_mitemp:
    push ax
    mov si, [bx+4]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add si, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mact - AL = item index of the OPEN menu -> DL = its action byte
; preserves everything else
; -----------------------------------------------------------------------------
sc_mact:
    push ax
    push bx
    push si
    mov [sc_picki], al              ; WHICH item, and out of WHICH menu: the
    mov ah, [sc_mopen]              ; action byte alone cannot tell a combo's
    mov [sc_pickm], ah              ; third entry from its first, and the Font
    mov ah, al                      ; combo is the first menu here that cares
    mov al, [sc_mopen]
    call sc_mgeti
    mov al, ah
    call sc_mitemp
    mov dl, [si+2]
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_wfit - does a chrome element ending at content-relative x AX fit the
;           window? out: CF=1 it does not; preserves all registers
; The strips guard every group with this so a narrow window drops trailing
; groups instead of drawing past its own frame onto the desktop.
; -----------------------------------------------------------------------------
sc_wfit:
    push ax
    add ax, 2
    cmp ax, [sc_cw]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_mbar - the menu bar strip: nine titles, ONE opaque run (SPEC.md 68.2)
; in:  sc_bounds run, gfx lock held; out: nothing; preserves all registers
;
; The titles are one 56-cell string drawn with one font_run at cl+8 - the
; content origin is snapped (SPEC.md 11.94), so the run rides the single-
; store path - plus a 1px mnemonic underline per fully visible title and the
; rule under the strip. A window too narrow for the bar draws a truncated
; copy rather than lettering past its own edge.
; -----------------------------------------------------------------------------
sc_mbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, [sc_ct]
    mov cx, ax
    add cx, [sc_cw]
    dec cx
    mov dx, bx
    add dx, SC_MENU_H-2
    call OSAPI_GFX_FILL             ; the strip's ground
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, cx
    mov dx, [sc_ct]
    add dx, SC_MENU_H-1
    call OSAPI_GFX_HLINE            ; the rule under the bar
    mov si, sc_s_mbar
    mov ax, [sc_cw]
    cmp ax, 8 + 56*8
    jae .whole
    sub ax, 16                      ; too narrow: truncate at the cells that
    js .under                       ; fit. Mid-title cuts are the honest
    mov cl, 3                       ; degrade for a 96px window
    shr ax, cl
    jz .under
    cmp ax, 56
    jbe .tr
    mov ax, 56
.tr:
    mov cx, ax
    mov di, sc_mbbuf
    cld
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    jz .cpd
    loop .cp
    mov byte [di], 0
.cpd:
    mov si, sc_mbbuf
.whole:
    mov cx, [sc_cl]
    add cx, 8
    mov dx, [sc_ct]
    add dx, 3
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.under:
    mov si, sc_mtab                 ; the mnemonic underlines
    mov di, SC_M_N
.mn:
    mov al, [si]
    add al, [si+1]
    inc al
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp ax, [sc_cw]                 ; (start+len+1)*8 <= cw = fully visible
    ja .mnnext
    mov al, [si]
    add al, [si+2]                  ; start + the title's mnemonic index
    xor ah, ah
    shl ax, cl
    add ax, [sc_cl]
    add ax, 8
    mov bx, ax
    add bx, 6
    mov dx, [sc_ct]
    add dx, 11
    call OSAPI_GFX_HLINE
.mnnext:
    add si, 8
    dec di
    jnz .mn
    mov al, [sc_mopen]              ; a bar repaint under an open dropdown
    cmp al, SC_M_N                  ; keeps the title inverted
    jae .out
    call sc_mtxor
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mtxor - invert a bar title's band (XOR: calling it again un-inverts)
; in:  AL = menu index 0..8, sc_bounds run; preserves all registers
; -----------------------------------------------------------------------------
sc_mtxor:
    push ax
    push bx
    push cx
    push dx
    call sc_mgeti
    mov al, [bx]
    add al, [bx+1]
    inc al
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp ax, [sc_cw]
    ja .out                         ; off a narrow window's bar: nothing shown
    mov al, [bx]
    xor ah, ah
    shl ax, cl
    add ax, [sc_cl]
    add ax, 8-2                     ; 2px into the leading space cell
    mov dl, [bx+1]
    xor dh, dh
    shl dx, cl
    mov cx, ax
    add cx, dx
    add cx, 3                       ; ...and 2px into the trailing one
    mov bx, [sc_ct]
    inc bx
    mov dx, [sc_ct]
    add dx, 12
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mtitler - AL = menu 0..8: bank its bar band as the gesture anchor
; out: [sc_mabox] = {x1,y1,x2,y2}; preserves all registers
; The anchor is what a release/click is tested against for the "stay open"
; and "toggle closed" answers - one rect for titles and combo boxes alike.
; -----------------------------------------------------------------------------
sc_mtitler:
    push ax
    push bx
    push cx
    push dx
    call sc_mgeti
    mov al, [bx]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [sc_cl]
    add ax, 8-2
    mov [sc_mabox], ax
    mov dl, [bx+1]
    xor dh, dh
    shl dx, cl
    add ax, dx
    add ax, 3
    mov [sc_mabox+4], ax
    mov ax, [sc_ct]
    mov [sc_mabox+2], ax
    add ax, SC_MENU_H-1
    mov [sc_mabox+6], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mchk - is the checkable item with action AL currently checked?
; out: CF=1 checked; preserves all registers
; Draft and the one document window are always checked (they are the mode and
; the window); the three View strips answer from their toggles.
; -----------------------------------------------------------------------------
sc_mchk:
    cmp al, SCA_DRAFT               ; Draft and Page are the two views and
    jne .npg                        ; exactly one of them is checked
    cmp byte [sc_vpage], 0
    je .yes
    jmp short .no
.npg:
    cmp al, SCA_PAGE
    jne .nwin
    cmp byte [sc_vpage], 0
    jne .yes
    jmp short .no
.nwin:
    cmp al, SCA_WIN1
    je .yes
    cmp al, SCA_VRIB
    jne .n1
    cmp byte [sc_vrib], 0
    jne .yes
    jmp short .no
.n1:
    cmp al, SCA_VRUL
    jne .n2
    cmp byte [sc_vrul], 0
    jne .yes
    jmp short .no
.n2:
    cmp al, SCA_VSTA
    jne .no
    cmp byte [sc_vsta], 0
    jne .yes
.no:
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_mgeo - compute the open dropdown's rectangle into sc_mrect
; in:  AL = menu index; bar menus anchor under their title, pseudo-menus
;      (the combos) at [sc_max]/[sc_may] which the click site set
; out: sc_mrect = {x1,y1,x2,y2}; preserves all registers
; The height is summed from the items (10px bands, 5px separators, 2px pad
; top and bottom inside the frame); the rect is clamped INSIDE the content -
; gfx primitives draw wherever they are pointed, and a dropdown must never
; scribble the window frame or the desktop below it.
; -----------------------------------------------------------------------------
sc_mgeo:
    push ax
    push bx
    push cx
    push dx
    push si
    call sc_mgeti
    cmp al, SC_M_N
    jae .combo
    mov dl, [bx]
    xor dh, dh
    mov cl, 3
    shl dx, cl
    add dx, [sc_cl]
    add dx, 8-4                     ; the panel starts 4px left of the title
    mov [sc_mrx1], dx
    mov dx, [sc_ct]
    add dx, SC_MENU_H
    mov [sc_mry1], dx
    jmp short .size
.combo:
    mov dx, [sc_max]
    mov [sc_mrx1], dx
    mov dx, [sc_may]
    mov [sc_mry1], dx
.size:
    push ax                         ; the menu index - the height loop below
                                    ; eats AL and the flip needs it back
    mov cl, [bx+3]                  ; height: 4 + 10/5 per item
    xor ch, ch
    mov si, [bx+4]
    mov ax, 4
.hh:
    jcxz .hd
    test byte [si], SCMF_SEP
    jz .h10
    add ax, SC_MS_HGT
    jmp short .hn
.h10:
    add ax, SC_MI_HGT
.hn:
    add si, 8
    dec cx
    jmp short .hh
.hd:
    pop cx                          ; CL = the menu index again
    mov si, ax                      ; SI = the panel's height in pixels
    mov dx, [sc_ct]
    add dx, [sc_ch]
    sub dx, 2                       ; leave the shadow's pixel inside too
    add ax, [sc_mry1]
    dec ax
    cmp ax, dx
    jbe .yok
    cmp cl, SC_M_N
    jb .clip                        ; a BAR menu hangs under its own title and
                                    ; nowhere else - moving one would put it
                                    ; over the menu bar it came from
    ; --- the SLIDE (SPEC.md 6.4.1). A combo's list is anchored at its box, and
    ; with ten faces in FONTS/ eleven 10px items stand 114 rows tall - past the
    ; bottom of a 200-row CGA window by a few pixels. Clipping is what used to
    ; happen and it is SILENT: the trailing faces are drawn nowhere and
    ; sc_mfind refuses to find them, so a disk carrying more faces than the
    ; anchor has room below it simply loses the last ones, which is how
    ; SC_MAXFONT came to be 6. Sliding the panel up until its bottom sits on
    ; the limit keeps every item on the screen and reachable; it costs one
    ; subtraction, and it is only ever taken when the alternative was to lose
    ; an item.
    ;
    ; THE ARITHMETIC IS UNSIGNED AND THE ROOM IS MEASURED, NOT THE TOP. The
    ; first version computed the new top and compared it against the content
    ; edge, which is a NEGATIVE number the moment the panel is taller than the
    ; space above the anchor - and `jb` read it as 65,490 and slid the panel
    ; off the top of the screen, over the menu bar and the desktop. Subtracting
    ; two rows that are both on the screen cannot go negative.
    mov cx, dx
    sub cx, [sc_ct]                 ; CX = the rows the content has for it
    cmp cx, si
    jb .clip                        ; taller than the whole content: nothing
                                    ; to slide into, so clip as before
    mov ax, dx
    sub ax, si
    inc ax                          ; the top that puts its bottom on DX
    mov [sc_mry1], ax
    mov ax, dx                      ; ...and that bottom is the limit itself
    jmp short .yok
.clip:
    mov ax, dx                      ; clipped: trailing items are not drawn
.yok:                               ; and sc_mfind stops at the same edge
    mov [sc_mry2], ax
    mov ax, [bx+6]                  ; the width the table precomputed
    mov dx, [sc_mrx1]
    add dx, ax
    dec dx
    mov cx, [sc_cl]
    add cx, [sc_cw]
    sub cx, 2
    cmp dx, cx
    jbe .xok
    mov dx, cx                      ; ride the right edge (Help lives there)
    push dx
    sub dx, ax
    inc dx
    mov cx, [sc_cl]
    inc cx
    cmp dx, cx
    jae .x1ok
    mov dx, cx
.x1ok:
    mov [sc_mrx1], dx
    pop dx
.xok:
    mov [sc_mrx2], dx
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mdraw - draw the open dropdown from sc_mrect (SPEC.md 68.2)
; in:  [sc_mopen], sc_mrect computed, gfx lock held; preserves all registers
;
; White panel, 1px black frame, 1px grey drop shadow right and bottom
; (GFX_FILL_GRAY: a 50% dither, so it reads grey on every adapter). Items
; 10px apart with the text at x1+8; separators are hlines; captions right-
; justified; disabled items drawn whole - label and caption - under the
; disabled pen so they dither at 1bpp (SPEC.md 47); checked items get a two-
; line check at x1+2; enabled mnemonics a 1px underline.
; -----------------------------------------------------------------------------
sc_mdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sc_mrx1]
    mov bx, [sc_mry1]
    mov cx, [sc_mrx2]
    mov dx, [sc_mry2]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN              ; live pen: CBLACK, dither flag clear
    call OSAPI_GFX_FRAME
    mov ax, [sc_mrx2]               ; the drop shadow: right edge...
    inc ax
    mov bx, [sc_mry1]
    inc bx
    mov cx, ax
    mov dx, [sc_mry2]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [sc_mrx1]               ; ...and bottom edge
    inc ax
    mov bx, [sc_mry2]
    inc bx
    mov cx, [sc_mrx2]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    ; the items
    mov al, [sc_mopen]
    call sc_mgeti
    mov cl, [bx+3]
    xor ch, ch
    mov si, [bx+4]
    mov di, [sc_mry1]
    add di, 2
.item:
    or cx, cx
    jnz .live
    jmp .done                       ; out of the short branch's reach
.live:
    test byte [si], SCMF_SEP
    jz .norm
    mov ax, di                      ; a separator: one hline mid-band
    add ax, SC_MS_HGT-1
    cmp ax, [sc_mry2]
    jae .done
    mov ax, [sc_mrx1]
    inc ax
    mov bx, [sc_mrx2]
    dec bx
    mov dx, di
    add dx, 2
    call OSAPI_GFX_HLINE
    add di, SC_MS_HGT
    jmp .next
.norm:
    mov ax, di
    add ax, SC_MI_HGT-1
    cmp ax, [sc_mry2]
    jae .done                       ; clipped by a short window: stop
    push cx
    mov byte [sc_mink], CBLACK
    test byte [si], SCMF_DIS
    jnz .dis
    clc
    jmp short .pen
.dis:
    mov byte [sc_mink], CDGRAY      ; ...and the run's ink is the same answer
    stc                             ; (SPEC.md 68.14): a package cannot read
.pen:                               ; the pen back, so it is decided here
    call OSAPI_GFX_PEN              ; CF IS the argument (SPEC.md 47)
    ; the check column
    test byte [si], SCMF_CHK
    jz .nochk
    mov al, [si+2]
    call sc_mchk
    jnc .nochk
    mov ax, [sc_mrx1]
    add ax, 2
    mov bx, di
    add bx, 5
    mov cx, [sc_mrx1]
    add cx, 3
    mov dx, di
    add dx, 7
    push si
    xor si, si
    call OSAPI_GFX_LINE             ; the check's short down-stroke...
    mov ax, cx
    mov bx, dx
    mov cx, [sc_mrx1]
    add cx, 7
    mov dx, di
    add dx, 3
    call OSAPI_GFX_LINE             ; ...and its long up-stroke
    pop si
.nochk:
    ; the label
    mov cx, [sc_mrx1]
    add cx, 8
    mov dx, di
    inc dx
    push si
    mov si, [si+4]
    mov ah, CWHITE                  ; OPAQUE (SPEC.md 68.14): the panel's white
    mov al, [sc_mink]               ; is a constant and the item's own cells
    call OSAPI_FONT_RUN             ; are one decision each. [gfx_dis] is
    pop si                          ; already set, so 6.1.12 folds 47's
                                    ; checkerboard into the run's own mask
    ; the mnemonic underline (enabled items only: a grey line rounds to
    ; solid black at 1bpp and a greyed mnemonic answers no key anyway)
    test byte [si], SCMF_DIS
    jnz .nomn
    cmp byte [si+3], 0
    je .nomn
    mov al, [si+1]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [sc_mrx1]
    add ax, 8
    mov bx, ax
    add bx, 6
    mov dx, di
    add dx, 9
    call OSAPI_GFX_HLINE
.nomn:
    ; the caption, right-justified in the panel
    mov bx, [si+6]
    or bx, bx
    jz .nocap
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX is the WIDTH here, so the pair goes
    mov cx, [sc_mrx2]               ; in after it and not before
    sub cx, 6
    sub cx, ax
    mov dx, di
    inc dx
    mov ah, CWHITE
    mov al, [sc_mink]
    call OSAPI_FONT_RUN
    pop si
.nocap:
    clc
    call OSAPI_GFX_PEN              ; pen back live before the next item
    pop cx
    add di, SC_MI_HGT
.next:
    add si, 8
    dec cx
    jz .done
    jmp .item
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mfind - which ENABLED item is the point on?
; in:  CX = x, DX = y (abs), the dropdown open
; out: AL = item index, or 0xFF (outside, a separator, a disabled item, or a
;      clipped one); preserves everything else
; -----------------------------------------------------------------------------
sc_mfind:
    push bx
    push cx
    push dx
    push si
    push di
    cmp cx, [sc_mrx1]
    jb .miss
    cmp cx, [sc_mrx2]
    ja .miss
    cmp dx, [sc_mry1]
    jb .miss
    cmp dx, [sc_mry2]
    ja .miss
    mov al, [sc_mopen]
    call sc_mgeti
    mov cl, [bx+3]
    xor ch, ch
    mov si, [bx+4]
    mov di, [sc_mry1]
    add di, 2
    cmp dx, di
    jb .miss                        ; in the top pad
    xor bx, bx                      ; BL = index
.scan:
    jcxz .miss
    test byte [si], SCMF_SEP
    jnz .sep
    mov ax, di
    add ax, SC_MI_HGT-1
    cmp ax, [sc_mry2]
    jae .miss                       ; this item is clipped: so is the rest
    cmp dx, ax
    ja .below
    test byte [si], SCMF_DIS        ; found the band
    jnz .miss
    mov al, bl
    jmp short .out
.below:
    add di, SC_MI_HGT
    jmp short .adv
.sep:
    mov ax, di
    add ax, SC_MS_HGT-1
    cmp dx, ax
    jbe .miss                       ; a separator answers nothing
    add di, SC_MS_HGT
.adv:
    add si, 8
    inc bx
    dec cx
    jmp short .scan
.miss:
    mov al, 0xFF
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_mhl - XOR the highlight band of item AL (0xFF = nothing to do)
; XOR is its own inverse, so on and off are the same call - the banked-
; coordinate idiom the drag markers already use (SPEC.md 27.8).
; -----------------------------------------------------------------------------
sc_mhl:
    cmp al, 0xFF
    je .no
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, al                      ; DL = target index
    mov al, [sc_mopen]
    call sc_mgeti
    mov si, [bx+4]
    mov di, [sc_mry1]
    add di, 2
    xor cx, cx
.w:
    cmp cl, dl
    je .found
    test byte [si], SCMF_SEP
    jz .i10
    add di, SC_MS_HGT
    jmp short .n
.i10:
    add di, SC_MI_HGT
.n:
    add si, 8
    inc cx
    jmp short .w
.found:
    mov ax, di
    add ax, SC_MI_HGT-1
    cmp ax, [sc_mry2]
    jae .done                       ; clipped: it was not drawn either
    mov dx, ax
    mov ax, [sc_mrx1]
    inc ax
    mov bx, di
    mov cx, [sc_mrx2]
    dec cx
    call OSAPI_GFX_XOR_FILL
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.no:
    ret

; -----------------------------------------------------------------------------
; sc_mbarhit - CX/DX = a point: which bar title is it on?
; out: AL = 0..8 or 0xFF; preserves everything else
; -----------------------------------------------------------------------------
sc_mbarhit:
    push bx
    push cx
    push dx
    push si
    mov ax, [sc_ct]
    cmp dx, ax
    jb .no
    add ax, SC_MENU_H-1
    cmp dx, ax
    ja .no
    mov ax, cx
    sub ax, [sc_cl]
    sub ax, 8
    js .no
    mov cl, 3
    shr ax, cl                      ; AL = the cell the click is in (AH = 0)
    mov si, sc_mtab
    xor bx, bx
.s:
    mov dl, [si]
    cmp al, dl
    jb .next
    mov dh, dl
    add dh, [si+1]
    cmp al, dh
    jae .next
    mov cl, [si]                    ; visible? (start+len+1)*8 <= cw
    add cl, [si+1]
    inc cl
    xor ch, ch
    push ax
    mov ax, cx
    mov cl, 3
    shl ax, cl
    cmp ax, [sc_cw]
    pop ax
    ja .next
    mov al, bl
    jmp short .out
.next:
    add si, 8
    inc bx
    cmp bl, SC_M_N
    jb .s
.no:
    mov al, 0xFF
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_mopenm - open menu AL: bank state, invert the title, draw the dropdown
; in:  AL = menu index, sc_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
sc_mopenm:
    push ax
    push cx
    push si
    push di
    mov [sc_mopen], al
    mov byte [sc_mhi], 0xFF
    cmp al, 7                       ; the Window menu shows the live document:
    jne .now                        ; compose '1 ' + sc_name into its item
    mov word [sc_win1], '1 '
    mov si, sc_name
    mov di, sc_win1+2
    mov cx, 13
.cp:
    mov ah, [si]
    mov [di], ah
    inc si
    inc di
    or ah, ah
    loopnz .cp
    mov byte [sc_win1+15], 0
.now:
    call sc_mgeo
    cmp al, SC_M_N
    jae .noxor
    call sc_mtxor
.noxor:
    call sc_mdraw
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mclose - take the open dropdown down and repaint what it covered
; in:  SI = window ptr, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
sc_mclose:
    push ax
    mov al, [sc_mopen]
    cmp al, SC_M_NONE
    je .out
    cmp al, SC_M_N
    jae .noxor
    call sc_mtxor                   ; the title back to normal video
.noxor:
    mov byte [sc_mopen], SC_M_NONE
    mov byte [sc_mhi], 0xFF
    call sc_mrepair
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mrepair - repaint exactly what a dropdown (or the About box) covered
; in:  SI = window ptr, sc_mrect = the covered rect, sc_bounds run, lock held
; out: nothing; preserves all registers; sc_mrect is consumed (grown by the
;      shadow and clamped)
;
; Piecewise, priced by what the rect touches: the ribbon and ruler strips are
; repainted whole (bounded call counts); the margin band above the text is
; refilled; the text rows are erased FULL WIDTH and re-lettered by one
; clipped walk seeded at the first covered row - row-granular, the band-blit
; idiom (SPEC.md 68.2); the bar, status line and grow box only when reached.
; -----------------------------------------------------------------------------
sc_mrepair:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [sc_mrx2]               ; the shadow is part of what was covered
    inc ax
    mov dx, [sc_cl]
    add dx, [sc_cw]
    dec dx
    cmp ax, dx
    jbe .x2ok
    mov ax, dx
.x2ok:
    mov [sc_mrx2], ax
    mov ax, [sc_mry2]
    inc ax
    mov dx, [sc_ct]
    add dx, [sc_ch]
    dec dx
    cmp ax, dx
    jbe .y2ok
    mov ax, dx
.y2ok:
    mov [sc_mry2], ax
    ; the menu bar strip (a modal dialog may cover it - SPEC.md 68.3)
    mov ax, [sc_ct]
    mov dx, ax
    add dx, SC_MENU_H-1
    cmp [sc_mry1], dx
    ja .nombar
    cmp [sc_mry2], ax
    jb .nombar
    call sc_mbar
.nombar:
    ; the ribbon strip
    cmp byte [sc_vrib], 0
    je .norib
    mov ax, [sc_ct]
    add ax, SC_MENU_H
    mov dx, ax
    add dx, SC_RIBBON_H-1
    cmp [sc_mry1], dx
    ja .norib
    cmp [sc_mry2], ax
    jb .norib
    call sc_ribbon
.norib:
    ; the ruler strip
    cmp byte [sc_vrul], 0
    je .norul
    call sc_ruly
    mov dx, ax
    add dx, SC_RULER_H-1
    cmp [sc_mry1], dx
    ja .norul
    cmp [sc_mry2], ax
    jb .norul
    call sc_ruler
.norul:
    ; the band between the chrome and the text top: the margin gap
    mov ax, [sc_ct]
    add ax, [sc_ctop]
    mov dx, [sc_ty]
    dec dx
    cmp [sc_mry1], dx
    ja .noband
    cmp [sc_mry2], ax
    jb .noband
    mov bx, [sc_mry1]
    cmp bx, ax
    jae .b1
    mov bx, ax
.b1:
    cmp dx, [sc_mry2]
    jbe .b2
    mov dx, [sc_mry2]
.b2:
    push bx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop dx
    pop bx
    mov ax, [sc_mrx1]
    mov cx, [sc_mrx2]
    call OSAPI_GFX_FILL
.noband:
    ; the text rows
    mov ax, [sc_ty]
    cmp [sc_mry2], ax
    jb .notext
    mov dx, [sc_bot]
    cmp [sc_mry1], dx
    ja .notext
    mov dx, [sc_mry1]
    cmp dx, ax
    jae .r0a
    mov dx, ax
.r0a:
    call sc_yrow                    ; DI = r0, the first covered row, found
    jnc .r0b                        ; from the banked ys (SPEC.md 68.6) -
    xor ax, ax                      ; refused, it is the whole band: slow and
.r0b:                               ; never wrong
    mov di, ax
    ; y1 = r0's band top: sc_ty for row 0, else ryb[r0-1] + 8
    or ax, ax
    jz .y1ty
    cmp byte [sc_hasfmt], 0
    jne .y1fmt
    mov cl, 3
    shl ax, cl
    add ax, [sc_ty]
    mov bx, ax
    jmp short .y1have
.y1fmt:
    mov bx, ax
    dec bx
    shl bx, 1
    mov bx, [bx+sc_ryb]
    add bx, 8
    jmp short .y1have
.y1ty:
    mov bx, [sc_ty]
.y1have:
    mov dx, [sc_mry2]
    cmp dx, [sc_bot]
    jbe .yla
    mov dx, [sc_bot]
.yla:
    push dx                         ; the erase's bottom edge
    ; erase the covered rows FULL WIDTH: the walk's opaque runs then reletter
    ; them; below the last document row the fill alone is the repaint
    mov ax, [sc_tx]
    sub ax, SC_MARGIN
    mov cx, [sc_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop dx
    ; r1 = the last covered row that exists on screen
    push di
    call sc_yrow
    mov di, ax
    jnc .r1have
    mov di, [sc_vrows]              ; refused: to the view's last row
    dec di
.r1have:
    mov dx, di
    pop di
    mov ax, [sc_vrows]
    or ax, ax
    jz .notext
    dec ax
    cmp dx, ax
    jbe .r1ok
    mov dx, ax
.r1ok:
    cmp di, dx
    ja .notext                      ; covered only the blank space below
    mov word [sc_prowi], 0xFFFF     ; the fill erased the delta cache's row
    mov [sc_dr0], di
    mov [sc_dr1], dx
    mov word [sc_hity], 0xFFFF
    mov word [sc_wanty], 0x7FFF
    mov byte [sc_draw], 1
    mov byte [sc_sigup], 1
    mov byte [sc_clip], 1
    mov byte [sc_clean], 1
    mov byte [sc_resume], 0
    mov ax, di
    call sc_seedrow                 ; seed at r0, bound at r1 (DX)
    cmp byte [sc_resume], 0
    jne .seeded
    mov ax, [sc_top]                ; sc_rows stale: the row index still
    push dx                         ; describes the view (SPEC.md 27.13)
    mov dx, [sc_vrows]
    call sc_xseed
    pop dx
.seeded:
    mov [sc_lastrow], dx
    call sc_walk
    mov byte [sc_clip], 0
    mov byte [sc_clean], 0
    mov byte [sc_resume], 0
.notext:
    ; the scroll bar's columns
    mov ax, [sc_rgt]
    cmp [sc_mrx2], ax
    jbe .nobar
    mov ax, [sc_ty]
    cmp [sc_mry2], ax
    jb .nobar
    call sc_sbar
.nobar:
    ; the status strip
    cmp byte [sc_vsta], 0
    je .nosta
    mov ax, [sc_bot]
    cmp [sc_mry2], ax
    jbe .nosta
    call sc_status
.nosta:
    ; the grow box corner
    mov ax, [sc_sbb]
    cmp [sc_mry2], ax
    jbe .nogrow
    mov ax, [sc_rgt]
    cmp [sc_mrx2], ax
    jbe .nogrow
    mov bx, si
    call OSAPI_WM_GROW              ; no-op unless frontmost+sizable: safe
.nogrow:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ruly - AX = the ruler strip's top row (valid when the ruler is shown)
; preserves everything else
; -----------------------------------------------------------------------------
sc_ruly:
    mov ax, [sc_ct]
    add ax, SC_MENU_H
    cmp byte [sc_vrib], 0
    je .out
    add ax, SC_RIBBON_H
.out:
    ret

; -----------------------------------------------------------------------------
; sc_combo - one combo box: frame, shown text, divider, drop-down arrow
; in:  AX = x1 (abs), BX = y1 (abs), CX = width, SI = the shown text
;      gfx lock held; out: nothing; preserves all registers
; The text sits at x1+8, which every caller keeps on a byte column.
; -----------------------------------------------------------------------------
sc_combo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax                      ; DI = x1
    add cx, ax
    dec cx                          ; CX = x2
    push bx                         ; y1, reloaded after the arrow loop
    push si                         ; the text, ditto
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    mov dx, bx
    add dx, 11
    call OSAPI_GFX_FRAME
    mov ax, cx
    sub ax, 12
    call OSAPI_GFX_VLINE            ; the divider in front of the arrow cell
    mov dx, bx
    add dx, 4
    mov si, 3                       ; the arrow: four shrinking hlines
.ar:
    mov ax, cx
    sub ax, 6
    sub ax, si
    mov bx, cx
    sub bx, 6
    add bx, si
    call OSAPI_GFX_HLINE
    inc dx
    dec si
    jns .ar
    pop si
    pop bx
    mov cx, di
    add cx, 8
    mov dx, bx
    add dx, 2
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: the strip's fill is what is under
    call OSAPI_FONT_RUN             ; this box, and it is white (SPEC.md 68.14)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_btn12 - one 12x12 bordered toggle cell, optionally lettered
; in:  AX = x, BX = y (abs), CL = the letter (0 = none); preserves all
; -----------------------------------------------------------------------------
sc_btn12:
    push ax
    push bx
    push cx
    push dx
    push cx
    mov cx, ax
    add cx, SC_BTN_W-1
    mov dx, bx
    add dx, SC_BTN_W-1
    call OSAPI_GFX_FRAME
    pop cx
    or cl, cl
    jz .out
    push si
    mov [sc_cbuf], cl               ; a one-character STRING: there is no
    mov byte [sc_cbuf+1], 0         ; opaque font_char and a run of one cell
    mov si, sc_cbuf                 ; is the same call (SPEC.md 6.6.5)
    mov cx, ax                      ; CX = x, which arrived in AX
    add cx, 2
    mov dx, bx
    add dx, 2
    mov ax, (CWHITE << 8) | CBLACK  ; the strip's fill is what is under this
    call OSAPI_FONT_RUN             ; box. EVERY LETTERED CALLER IS LIVE - the
                                    ; two that grey the pen (super/subscript,
                                    ; the tab gallery) pass CL = 0 - so the
                                    ; ink is a constant here
    pop si
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ribbon - the ribbon strip (SPEC.md 68.2), per ibdefs.h's order:
;             Font:/Pts: combos, B I K | U W D | super/sub pair | pilcrow.
; in:  sc_bounds run, gfx lock held; preserves all registers
; The buttons are drawn and hit-tested but inert this stage - pressed-state
; rendering arrives with character formatting.
; -----------------------------------------------------------------------------
sc_ribbon:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sc_vrib], 0
    je .out
    mov di, [sc_ct]
    add di, SC_MENU_H               ; DI = the strip's top
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, di
    mov cx, ax
    add cx, [sc_cw]
    dec cx
    mov dx, di
    add dx, SC_RIBBON_H-2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, cx
    mov dx, di
    add dx, SC_RIBBON_H-1
    call OSAPI_GFX_HLINE            ; the rule under the ribbon
    ; Font: and its combo
    mov ax, SC_RB_FBX + SC_RB_FBW - 1
    call sc_wfit
    jc .btns
    mov cx, [sc_cl]
    add cx, SC_RB_FLBL
    mov dx, di
    add dx, 4
    mov si, sc_s_font
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: the ribbon's fill is white (SPEC.md 68.14)
    call OSAPI_FONT_RUN
    mov ax, [sc_cl]
    add ax, SC_RB_FBX
    mov bx, di
    add bx, 2
    mov cx, SC_RB_FBW
    mov si, [sc_fcap]               ; the face the document is set in, which
    call sc_combo                   ; is sc_s_pica until one is chosen
    ; Pts: and its combo
    mov ax, SC_RB_PBX + SC_RB_PBW - 1
    call sc_wfit
    jc .btns
    mov cx, [sc_cl]
    add cx, SC_RB_PLBL
    mov dx, di
    add dx, 4
    mov si, sc_s_pts
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: ...and so is the rest of the strip
    call OSAPI_FONT_RUN
    mov ax, [sc_cl]
    add ax, SC_RB_PBX
    mov bx, di
    add bx, 2
    mov cx, SC_RB_PBW
    mov si, sc_s_10
    call sc_combo
.btns:
    mov ax, SC_RB_B1 + 2*SC_BTN_P + SC_BTN_W - 1
    call sc_wfit
    jc .pilc
    mov bx, di
    add bx, 2
    mov ax, [sc_cl]
    add ax, SC_RB_B1
    mov cl, 'B'
    call sc_btn12
    add ax, SC_BTN_P
    mov cl, 'I'
    call sc_btn12
    add ax, SC_BTN_P
    mov cl, 'K'                     ; Small Kaps - the ribbon bitmap's letter,
    call sc_btn12                   ; and char.des's literal '&Kaps' spelling
    mov ax, SC_RB_B2 + 2*SC_BTN_P + SC_BTN_W - 1
    call sc_wfit
    jc .pilc
    mov ax, [sc_cl]
    add ax, SC_RB_B2
    mov cl, 'U'
    call sc_btn12
    add ax, SC_BTN_P
    mov cl, 'W'
    call sc_btn12
    add ax, SC_BTN_P
    mov cl, 'D'
    call sc_btn12
    ; the stacked superscript/subscript pair (ScrptPos): a raised and a
    ; dropped mark in one cell's two halves - GREYED whole (SPEC.md 47/68.3):
    ; super/subscript does not exist in draft view's one cell height
    mov ax, SC_RB_SS + SC_BTN_W - 1
    call sc_wfit
    jc .pilc
    stc
    call OSAPI_GFX_PEN
    mov ax, [sc_cl]
    add ax, SC_RB_SS
    mov bx, di
    add bx, 2
    xor cl, cl
    call sc_btn12
    mov si, ax                      ; SI = the cell's x
    mov ax, si
    add ax, 2
    mov bx, si
    add bx, 6
    mov dx, di
    add dx, 6
    call OSAPI_GFX_HLINE            ; superscript baseline...
    mov ax, si
    add ax, 7
    mov bx, si
    add bx, 9
    mov dx, di
    add dx, 4
    call OSAPI_GFX_HLINE            ; ...and its raised exponent
    mov ax, si
    add ax, 2
    mov bx, si
    add bx, 6
    mov dx, di
    add dx, 9
    call OSAPI_GFX_HLINE            ; subscript baseline...
    mov ax, si
    add ax, 7
    mov bx, si
    add bx, 9
    mov dx, di
    add dx, 11
    call OSAPI_GFX_HLINE            ; ...and its dropped index
    clc
    call OSAPI_GFX_PEN              ; the pen back live for the pilcrow
.pilc:
    ; the show-all pilcrow, anchored at the content's right edge (ibdefs.h
    ; puts bcmShowAll at the ribbon's far right)
    mov ax, [sc_cw]
    cmp ax, SC_RB_SS + SC_BTN_W + 20
    jb .sync
    mov ax, [sc_cl]
    add ax, [sc_cw]
    sub ax, 4 + SC_BTN_W
    mov bx, di
    add bx, 2
    xor cl, cl
    call sc_btn12
    mov si, ax
    mov ax, si                      ; the pilcrow: bowl + two stems (the font
    add ax, 3                       ; has no glyph above 126, so it is drawn)
    mov bx, di
    add bx, 5
    mov cx, si
    add cx, 5
    mov dx, di
    add dx, 8
    call OSAPI_GFX_FILL
    mov ax, si
    add ax, 6
    mov bx, di
    add bx, 5
    mov dx, di
    add dx, 11
    call OSAPI_GFX_VLINE
    mov ax, si
    add ax, 8
    mov bx, di
    add bx, 5
    mov dx, di
    add dx, 11
    call OSAPI_GFX_VLINE
.sync:
    mov byte [sc_rbold], 0          ; every cell was just drawn UNPRESSED:
    mov byte [sc_rbok], 1           ; re-invert the ones the caret's attrs
    call sc_rbstat                  ; say are on (SPEC.md 68.3)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rbxy - where ribbon toggle cell AL sits (0..5 = B I K U W D, 6 = the
;           pilcrow); out: CF=1 the window is too narrow and the cell is NOT
;           drawn, else AX = its x, BX = its y. Preserves everything else.
; One geometry for the painter's XOR, the delta update and the hit test -
; the fm_hit discipline (SPEC.md 22).
; -----------------------------------------------------------------------------
sc_rbxy:
    push cx
    push dx
    mov dl, al
    cmp dl, 6
    je .pil
    cmp dl, 3
    jae .g2
    mov ax, SC_RB_B1 + 2*SC_BTN_P + SC_BTN_W - 1
    call sc_wfit
    jc .no
    mov al, dl
    xor ah, ah
    mov cx, SC_BTN_P
    mul cx
    add ax, SC_RB_B1
    jmp short .have
.g2:
    mov ax, SC_RB_B2 + 2*SC_BTN_P + SC_BTN_W - 1
    call sc_wfit
    jc .no
    mov al, dl
    sub al, 3
    xor ah, ah
    mov cx, SC_BTN_P
    mul cx
    add ax, SC_RB_B2
    jmp short .have
.pil:
    mov ax, [sc_cw]
    cmp ax, SC_RB_SS + SC_BTN_W + 20
    jb .no
    mov ax, [sc_cw]
    sub ax, 4 + SC_BTN_W
.have:
    add ax, [sc_cl]
    mov bx, [sc_ct]
    add bx, SC_MENU_H + 2
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    ret

; the six toggle cells' attribute bits, B I K U W D order, then the pilcrow
sc_rbmask: db SCAT_BOLD, SCAT_ITAL, SCAT_SCAP, SCAT_UL, SCAT_WUL, SCAT_DUL
           db 0x80

; -----------------------------------------------------------------------------
; sc_rbstat - the ribbon's pressed states, delta-cached (SPEC.md 68.3)
; in:  sc_bounds run, gfx lock held; preserves all registers
; Reads the display attrs (caret's, or the selection's first character) plus
; the Show-all toggle in bit 7, compares against what the cells show, and
; XOR-inverts ONLY the cells whose state changed - a caret move that changes
; nothing draws nothing.
; -----------------------------------------------------------------------------
sc_rbstat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [sc_vrib], 0
    je .out
    cmp byte [sc_rbok], 0
    je .out
    cmp byte [sc_mopen], SC_M_NONE
    jne .out                        ; a dropdown may cover the strip
    cmp byte [sc_about], 0
    jne .out
    cmp word [sc_dlg], 0
    jne .out
    call sc_dispattr
    cmp byte [sc_showall], 0
    je .nosa
    or al, 0x80
.nosa:
    mov ah, [sc_rbold]
    cmp al, ah
    je .out
    mov [sc_rbold], al
    xor ah, al                      ; AH = the bits that changed
    xor cx, cx                      ; CL = the cell index
.cell:
    mov bx, cx
    test ah, [sc_rbmask+bx]
    jz .next
    push ax
    push cx
    mov al, cl
    call sc_rbxy                    ; AX/BX = the cell, or CF on a narrow
    jc .undrawn                     ; window where it is not on screen
    push ax
    mov cx, ax
    add cx, SC_BTN_W-2              ; the interior: the frame stays upright
    mov dx, bx
    add dx, SC_BTN_W-2
    inc bx
    pop ax
    inc ax
    call OSAPI_GFX_XOR_FILL
.undrawn:
    pop cx
    pop ax
.next:
    inc cx
    cmp cx, 7
    jb .cell
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rmkd / sc_rmku - a small down/up-pointing indent marker
; in:  AX = centre x, DX = top row (3 rows tall); preserves all registers
; -----------------------------------------------------------------------------
sc_rmkd:
    push ax
    push bx
    push dx
    mov bx, ax
    sub ax, 2
    add bx, 2
    call OSAPI_GFX_HLINE
    inc dx
    inc ax
    dec bx
    call OSAPI_GFX_HLINE
    inc dx
    inc ax
    dec bx
    call OSAPI_GFX_HLINE
    pop dx
    pop bx
    pop ax
    ret
sc_rmku:
    push ax
    push bx
    push dx
    mov bx, ax
    call OSAPI_GFX_HLINE
    inc dx
    dec ax
    inc bx
    call OSAPI_GFX_HLINE
    inc dx
    dec ax
    inc bx
    call OSAPI_GFX_HLINE
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_pgcol - in Page view, narrow the text column to the sheet
; in:  AX = the column's right edge as the window would have it, [sc_tx] = its
;      left
; out: AX and [sc_tx] moved to the sheet's; [sc_shl]/[sc_shr] = its pixel
;      edges. Preserves every other register.
;
; This runs where the RIGHT EDGE is decided and before anything derives from
; it, which is the whole trick: [sc_rcols], [sc_wrgt], the wrap decision, the
; row buffer's padding and the caret's hit test all fall out of (sc_tx,
; sc_rgt) and every one of them is then right for the sheet without knowing
; a sheet exists. Narrowing [sc_rcols] alone - which is where this started -
; left the WRAP at the window's width and merely truncated each row at the
; sheet's edge, dropping the tail of every line.
;
; A window too narrow to hold the sheet keeps its own width: Page view never
; hides text that Draft would have shown.
; -----------------------------------------------------------------------------
sc_pgcol:
    cmp byte [sc_vpage], 0
    je .draft
    push bx
    push cx
    push dx
    mov bx, ax                      ; the cells the CONTENT could hold - the
    sub bx, [sc_cl]                 ; content and not the text area, so the
    inc bx                          ; slack either side of the sheet comes out
    mov cl, 3                       ; equal instead of one margin heavier on
    shr bx, cl                      ; the left
    cmp bx, SC_PGCOLS + 1
    jbe .narrow                     ; too narrow for a sheet: leave it alone
    sub bx, SC_PGCOLS               ; the slack, halved, centres it
    shr bx, 1
    shl bx, cl
    mov ax, [sc_cl]
    add ax, bx
    mov [sc_tx], ax                 ; ...and the right edge follows the width
    add ax, SC_PGCOLS * 8
    dec ax
.narrow:
    pop dx
    pop cx
    pop bx
.draft:
    push ax                         ; the sheet's edges, for sc_sheet. In
    mov ax, [sc_tx]                 ; Draft they are the column's too, and
    sub ax, SC_MARGIN               ; sc_sheet draws nothing there anyway
    mov [sc_shl], ax
    pop ax
    push ax
    add ax, SC_MARGIN
    mov [sc_shr], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_sheet - the sheet's two edges and its page marks (SPEC.md 68.11)
; in:  the geometry sc_bounds banked; gfx lock held
; out: nothing; preserves all registers. A no-op in Draft view, which is what
;      keeps this off every draft-view redraw's bill.
;
; The edges are drawn OUTSIDE the text column, in the margin either side, so a
; row flush never touches them and they survive every partial repaint - only a
; full fill or a band blit can take them, and both call this again. Two
; gfx calls in the common case: at 756us each (PERFORMANCE.md) that is ~1.5ms
; on the target, paid once per full repaint and never per keystroke.
; -----------------------------------------------------------------------------
sc_sheet:
    cmp byte [sc_vpage], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov bx, [sc_ct]                 ; the band's top and bottom
    add bx, [sc_ctop]
    mov dx, [sc_bot]
    mov ax, [sc_shl]
    call OSAPI_GFX_VLINE
    mov ax, [sc_shr]
    call OSAPI_GFX_VLINE
    call sc_pgmarks
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; sc_pgmarks - a tick in each margin where a page begins
; Preserves all registers.
;
; The mark sits in the MARGIN, not across the sheet: a rule drawn inside the
; column would be erased by the next flush of the row under it, and putting it
; back would mean the row flush - the hottest path in the app (SPEC.md 68.6) -
; learning about pages. The tick says the same thing for two gfx calls a page
; and no cost at all to a keystroke. The inter-page WHITESPACE a real Page
; view shows is not drawn: it would have to come out of the layout walk's row
; advance, and that is the change SPEC.md 68.11 defers.
; -----------------------------------------------------------------------------
sc_pgmarks:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si                      ; SI = the visible row
.row:
    cmp si, [sc_vrows]
    jae .out
    mov ax, [sc_top]                ; its absolute row number
    add ax, si
    or ax, ax
    jz .next                        ; row 0 is not a page BREAK
    xor dx, dx
    mov cx, SC_PGLINES
    div cx
    or dx, dx
    jnz .next                       ; not the first row of a page
    mov bx, si                      ; its y, from the height bank
    shl bx, 1
    mov dx, [bx+sc_ryb]             ; DX = y, which is what HLINE takes
    mov ax, [sc_shl]                ; the left margin's tick
    sub ax, 4
    mov bx, [sc_shl]
    dec bx
    call OSAPI_GFX_HLINE
    mov ax, [sc_shr]                ; ...and the right margin's
    inc ax
    mov bx, ax
    add bx, 3
    call OSAPI_GFX_HLINE
.next:
    inc si
    jmp short .row
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ruler - the ruler strip (SPEC.md 68.2), per ibdefs.h's order: Style:
;            combo, alignment x4, spacing x3, closed/open space, tab type x4,
;            then the inch scale with the indent markers.
; in:  sc_bounds run, gfx lock held; preserves all registers
;
; The scale's minor ticks are ONE gfx_fill_pat: every pattern byte is 0x7F,
; so the leftmost pixel of every screen byte is black - a tick every 8px (one
; ruler cell = 1/10 inch at pica pitch), screen-aligned exactly like the
; snapped glyph cells above it. Digits land every 80px = every inch.
; -----------------------------------------------------------------------------
sc_ruler:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sc_vrul], 0
    je .out
    call sc_ruly
    mov di, ax                      ; DI = the strip's top
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, di
    mov cx, ax
    add cx, [sc_cw]
    dec cx
    mov dx, di
    add dx, SC_RULER_H-2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, cx
    mov dx, di
    add dx, SC_RULER_H-1
    call OSAPI_GFX_HLINE            ; the rule under the ruler
    ; Style: and its combo
    mov ax, SC_RL_SBX + SC_RL_SBW - 1
    call sc_wfit
    jc .scale
    mov cx, [sc_cl]
    add cx, SC_RL_SLBL
    mov dx, di
    add dx, 4
    mov si, sc_s_style
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: the ruler's fill, likewise
    call OSAPI_FONT_RUN
    mov ax, [sc_cl]
    add ax, SC_RL_SBX
    mov bx, di
    add bx, 2
    mov cx, SC_RL_SBW
    mov si, sc_s_normal
    call sc_combo
    ; the four alignment cells, each a 7px four-line glyph
    mov ax, SC_RL_AL + 3*SC_BTN_P + SC_BTN_W - 1
    call sc_wfit
    jc .scale
    mov si, sc_agl
    mov ax, [sc_cl]
    add ax, SC_RL_AL
    mov cx, 4
.al:
    push cx
    mov bx, di
    add bx, 2
    xor cl, cl
    call sc_btn12
    mov cx, 4
    mov dx, di
    add dx, 5
.alr:
    push ax
    push bx
    mov bl, [si+1]
    xor bh, bh
    add bx, ax
    push bx
    mov bl, [si]
    xor bh, bh
    add ax, bx
    pop bx
    call OSAPI_GFX_HLINE
    pop bx
    pop ax
    add dx, 2
    add si, 2
    loop .alr
    pop cx
    add ax, SC_BTN_P
    loop .al
    ; spacing: 1 / 1.5 / 2
    mov ax, SC_RL_SP2 + SC_BTN_W - 1
    call sc_wfit
    jc .scale
    mov ax, [sc_cl]
    add ax, SC_RL_SP1
    mov bx, di
    add bx, 2
    mov cl, '1'
    call sc_btn12
    mov ax, [sc_cl]
    add ax, SC_RL_SP15
    mov bx, di
    add bx, 2
    mov cx, ax
    add cx, SC_RL_SP15W-1
    mov dx, di
    add dx, 13
    call OSAPI_GFX_FRAME
    mov cx, ax
    inc cx
    mov dx, di
    add dx, 4
    mov si, sc_s_15
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: ...and the box this sits in is drawn on it
    call OSAPI_FONT_RUN
    mov ax, [sc_cl]
    add ax, SC_RL_SP2
    mov bx, di
    add bx, 2
    mov cl, '2'
    call sc_btn12
    ; closed / open paragraph spacing: two rules tight, two rules apart
    mov ax, [sc_cl]
    add ax, SC_RL_OC
    mov bx, di
    add bx, 2
    xor cl, cl
    call sc_btn12
    mov si, ax
    mov ax, si
    add ax, 2
    mov bx, si
    add bx, 9
    mov dx, di
    add dx, 7
    call OSAPI_GFX_HLINE
    mov dx, di
    add dx, 9
    call OSAPI_GFX_HLINE
    mov ax, si
    add ax, SC_BTN_P
    mov bx, di
    add bx, 2
    xor cl, cl
    call sc_btn12
    mov si, ax
    mov ax, si
    add ax, 2
    mov bx, si
    add bx, 9
    mov dx, di
    add dx, 5
    call OSAPI_GFX_HLINE
    mov dx, di
    add dx, 11
    call OSAPI_GFX_HLINE
    ; the four tab-type cells: L C R D - GREYED whole (SPEC.md 47/68.3):
    ; custom tab stops are out of scope, every tab lands on the default
    ; stops, so a cell that picks the NEXT stop's type would be a lie
    stc
    call OSAPI_GFX_PEN
    mov si, sc_tgl
    mov ax, [sc_cl]
    add ax, SC_RL_TAB
    mov cx, 4
.tb:
    push cx
    mov bx, di
    add bx, 2
    xor cl, cl
    call sc_btn12
    push ax
    mov dl, [si]                    ; the stem
    xor dh, dh
    add ax, dx
    mov bx, di
    add bx, 5
    mov dx, di
    add dx, 10
    call OSAPI_GFX_VLINE
    pop ax
    push ax
    mov bl, [si+2]                  ; the foot
    xor bh, bh
    add bx, ax
    push bx
    mov bl, [si+1]
    xor bh, bh
    add ax, bx
    pop bx
    mov dx, di
    add dx, 10
    call OSAPI_GFX_HLINE
    pop ax
    pop cx
    cmp cx, 1                       ; the last cell is the decimal tab:
    jne .tbn                        ; it gains its point
    push cx
    mov cx, ax
    add cx, 8
    mov dx, di
    add dx, 5
    call OSAPI_GFX_PIXEL
    pop cx
.tbn:
    add si, 3
    add ax, SC_BTN_P
    loop .tb
    clc
    call OSAPI_GFX_PEN              ; the pen back live for the scale
.scale:
    ; the inch scale, with the LIVE indent markers: its own routine, because
    ; a caret move into a differently-indented paragraph redraws only it
    call sc_rlscale
    mov word [sc_rlold], 0          ; every cell was just drawn UNPRESSED:
    mov byte [sc_rlok], 1           ; re-invert the ones the caret's
    call sc_rlstat                  ; paragraph says are on (SPEC.md 68.3)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rlcalc - the ruler's state for the CARET's paragraph (SPEC.md 68.3)
; out: AX = pressed-cell bits (0..3 alignment, 4..6 spacing 1/1.5/2,
;      7 closed, 8 open); [sc_rll]/[sc_rlf]/[sc_rlr] = its left / first
;      (signed) / right indents in cells. Preserves everything else.
; -----------------------------------------------------------------------------
sc_rlcalc:
    push bx
    push cx
    push dx
    push es
    mov ax, [sc_cur]
    call sc_papat
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov es, [sc_pseg]
    mov dl, [es:bx]                 ; the packed byte
    mov al, [es:bx+1]
    mov [sc_rll], al
    mov al, [es:bx+2]
    mov [sc_rlf], al
    mov al, [es:bx+3]
    mov [sc_rlr], al
    mov cl, dl
    and cl, 3
    mov ax, 1
    shl ax, cl                      ; the alignment's bit
    mov cl, dl
    and cl, SCPA_SPACE
    shr cl, 1
    shr cl, 1
    add cl, 4                       ; the spacing's, bits 4..6
    push ax
    mov ax, 1
    shl ax, cl
    mov cx, ax
    pop ax
    or ax, cx
    test dl, SCPA_SB
    jnz .open
    or ax, 0x0080                   ; closed pressed
    jmp short .out
.open:
    or ax, 0x0100                   ; open pressed
.out:
    pop es
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_rlxy - where ruler cell AL sits (0..3 align, 4/5/6 spacing 1/1.5/2,
;           7 closed, 8 open)
; out: CF=1 not drawn (narrow window); else AX = x, BX = y, CX = width.
; One geometry for painter, delta update and hit test (SPEC.md 22).
; -----------------------------------------------------------------------------
sc_rlxy:
    push dx
    mov dl, al
    cmp dl, 4
    jae .grp2
    mov ax, SC_RL_AL + 3*SC_BTN_P + SC_BTN_W - 1
    call sc_wfit
    jc .no
    mov al, dl
    xor ah, ah
    push cx
    mov cx, SC_BTN_P
    mul cx
    pop cx
    add ax, SC_RL_AL
    mov cx, SC_BTN_W
    jmp short .have
.grp2:
    mov ax, SC_RL_SP2 + SC_BTN_W - 1
    call sc_wfit
    jc .no
    mov cx, SC_BTN_W
    cmp dl, 4
    je .sp1
    cmp dl, 5
    je .sp15
    cmp dl, 6
    je .sp2
    cmp dl, 7
    je .oc0
    mov ax, SC_RL_OC + SC_BTN_P
    jmp short .have
.oc0:
    mov ax, SC_RL_OC
    jmp short .have
.sp1:
    mov ax, SC_RL_SP1
    jmp short .have
.sp15:
    mov ax, SC_RL_SP15
    mov cx, SC_RL_SP15W
    jmp short .have
.sp2:
    mov ax, SC_RL_SP2
.have:
    add ax, [sc_cl]
    push ax
    call sc_ruly
    mov bx, ax
    add bx, 2
    pop ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    ret

; sc_rlmclamp - keep marker centre AX inside the scale [SI-2 .. rlxe-2]
; (SI = the scale's zero, the caller's register); preserves all others
sc_rlmclamp:
    push bx
    mov bx, si
    sub bx, 2
    cmp ax, bx
    jge .lo
    mov ax, bx
.lo:
    mov bx, [sc_rlxe]
    sub bx, 2
    cmp ax, bx
    jle .out
    mov ax, bx
.out:
    pop bx
    ret

; sc_rlend - CX = the x of the text column's last pixel, which is where the
; scale stops. Preserves all other registers.
;
; The column and not the window: the scale measures the DOCUMENT, so it ends
; where the text does. In Page view that is the sheet's right edge, and the
; ruler spans the sheet exactly (SPEC.md 68.11).
sc_rlend:
    push ax
    mov cx, [sc_rcols]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [sc_tx]
    dec cx
    mov ax, [sc_cl]
    add ax, [sc_cw]
    sub ax, 5
    cmp cx, ax                      ; never past the strip itself
    jbe .ok
    mov cx, ax
.ok:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rlscale - the inch scale band whole: erase, ticks (ONE fill_pat),
;              digits, and the LIVE indent markers at the caret paragraph's
;              positions (SPEC.md 68.3). ~12 calls, drawn by sc_ruler and by
;              sc_rlstat when the caret enters a differently-indented
;              paragraph. Banks the marker caches.
; -----------------------------------------------------------------------------
sc_rlscale:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sc_vrul], 0
    je .out
    mov ax, SC_RL_SCALE + 24
    call sc_wfit
    jc .out
    call sc_ruly
    add ax, SC_RL_ROW2              ; the scale has a row to itself now
    mov di, ax                      ; DI = that row's top
    mov si, [sc_tx]                 ; SI = the scale's zero: where the TEXT
                                    ; starts, so an indent marker at n cells
                                    ; sits exactly over the column the text
                                    ; will indent to, and inch 1 is one inch
                                    ; of document (SPEC.md 68.2)
    call sc_rlend                   ; CX = the text column's right edge
    mov [sc_rlxe], cx
    mov al, CWHITE                  ; erase the band whole: digits, ticks and
    call OSAPI_SET_COLOR            ; markers all live in it and the markers
    mov ax, si                      ; have MOVED (CX is already the right end)
    sub ax, 5
    mov bx, di
    add bx, 2
    mov dx, di
    add dx, 16
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, si
    mov bx, di
    add bx, 11
    mov dx, di
    add dx, 13
    push si
    mov si, sc_ptick
    call OSAPI_GFX_FILL_PAT         ; every minor tick in ONE call
    pop si
    mov bx, si
    add bx, 77                      ; the first digit sits over its inch tick
    mov al, '1'
.dg:
    mov dx, bx
    add dx, 7
    cmp dx, [sc_rlxe]
    ja .marks
    mov cx, bx
    mov dx, di
    add dx, 2
    call OSAPI_FONT_CHAR_XPARENT
    add bx, 80
    inc al
    cmp al, '9'
    jbe .dg
.marks:
    call sc_rlcalc                  ; the live indents (cells)
    mov al, [sc_rll]                ; first-line: the down marker on top, at
    add al, [sc_rlf]                ; left + first
    cbw
    mov cl, 3
    shl ax, cl
    add ax, si
    call sc_rlmclamp
    mov dx, di
    add dx, 3
    call sc_rmkd
    mov al, [sc_rll]                ; left: the up marker below the ticks
    cbw
    shl ax, cl
    add ax, si
    call sc_rlmclamp
    mov dx, di
    add dx, 14
    call sc_rmku
    mov al, [sc_rlr]                ; right: in from the scale's far end
    cbw
    shl ax, cl
    mov dx, [sc_rlxe]
    sub dx, 2
    sub dx, ax
    mov ax, dx
    call sc_rlmclamp
    mov dx, di
    add dx, 14
    call sc_rmku
    mov al, [sc_rll]                ; the strip shows THESE indents now
    mov [sc_rmleft], al
    mov al, [sc_rlf]
    mov [sc_rmfirst], al
    mov al, [sc_rlr]
    mov [sc_rmright], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rlstat - the ruler's pressed cells and markers, delta-cached exactly as
;             the ribbon's cells are (SPEC.md 68.3): a caret move that stays
;             inside one paragraph format draws not a call
; in:  sc_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
sc_rlstat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [sc_vrul], 0
    je .out
    cmp byte [sc_rlok], 0
    je .out
    cmp byte [sc_mopen], SC_M_NONE
    jne .out                        ; a dropdown may cover the strip
    cmp byte [sc_about], 0
    jne .out
    cmp word [sc_dlg], 0
    jne .out
    call sc_rlcalc
    mov bl, [sc_rll]
    cmp bl, [sc_rmleft]
    jne .scale
    mov bl, [sc_rlf]
    cmp bl, [sc_rmfirst]
    jne .scale
    mov bl, [sc_rlr]
    cmp bl, [sc_rmright]
    je .cells
.scale:
    push ax
    call sc_rlscale                 ; a marker moved: the scale band redraws
    pop ax
.cells:
    mov dx, [sc_rlold]
    cmp ax, dx
    je .out
    mov [sc_rlold], ax
    xor dx, ax                      ; DX = the cells whose state changed
    xor cx, cx                      ; CL = the cell being tested
.cell:
    mov ax, 1
    push cx
    shl ax, cl
    pop cx
    test dx, ax
    jz .next
    push cx
    push dx
    mov ax, cx
    call sc_rlxy                    ; AX = x, BX = y, CX = width
    jc .undrawn
    push ax
    add ax, cx
    sub ax, 2                       ; the interior: the frame stays upright
    mov cx, ax
    pop ax
    inc ax
    mov dx, bx
    add dx, SC_BTN_W-2
    inc bx
    call OSAPI_GFX_XOR_FILL
.undrawn:
    pop dx
    pop cx
.next:
    inc cx
    cmp cx, 9
    jb .cell
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rldrag - a press on the ruler's scale: drag the nearest indent marker
; in:  CX = x, DX = y (abs), SI = window ptr, gfx lock held; preserves all
; An XOR guide line tracks over the text band (banked x, so the erase is the
; draw again - SPEC.md 48.11's rule); the release snaps to whole cells and
; applies through sc_modpap, the same door as the Ctrl keys and the dialog.
; -----------------------------------------------------------------------------
sc_rldrag:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, SC_RL_SCALE + 24
    call sc_wfit
    jc .done
    call sc_rlcalc                  ; the live indents in sc_rll/f/r
    mov ax, [sc_tx]
    mov [sc_rdz], ax                ; the zero, for the release's arithmetic
    mov al, [sc_rll]
    mov [sc_rdl], al                ; ...and the left, for the first marker's
                                    ; relative answer
    push cx                         ; the press x
    call sc_ruly
    add ax, SC_RL_ROW2 + 9          ; upper half of the SCALE row = the
                                    ; first-line marker
    cmp dx, ax
    pop cx
    jb .first
    ; lower half: left or right, whichever marker is nearer the press
    mov al, [sc_rll]
    cbw
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [sc_rdz]                ; AX = the left marker's x
    mov bx, ax
    sub bx, cx
    jns .la
    neg bx
.la:                                ; BX = its distance
    mov al, [sc_rlr]
    cbw
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    mov di, [sc_rlxe]
    sub di, 2
    sub di, ax                      ; DI = the right marker's x
    mov ax, di
    sub ax, cx
    jns .ra
    neg ax
.ra:
    cmp bx, ax
    jbe .left
    mov byte [sc_ppop], SCPO_SETRIGHT
    jmp short .track
.left:
    mov byte [sc_ppop], SCPO_SETLEFT
    jmp short .track
.first:
    mov byte [sc_ppop], SCPO_SETFIRST
.track:
    mov [sc_rgx], cx                ; the guide, XOR-drawn at the banked x
    call sc_rgxor
.pass:
    call sc_selpace                 ; unlock - yield a tick - relock
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .drop
    call sc_rgclamp                 ; CX clamped onto the scale
    cmp cx, [sc_rgx]
    je .pass
    call sc_rgxor                   ; old off...
    mov [sc_rgx], cx
    call sc_rgxor                   ; ...new on
    jmp short .pass
.drop:
    call sc_rgxor                   ; the guide comes down
    mov ax, [sc_rgx]                ; the landing, snapped to cells
    sub ax, [sc_rdz]
    add ax, 4
    cmp byte [sc_ppop], SCPO_SETRIGHT
    jne .cells
    mov ax, [sc_rlxe]               ; the right marker counts from the far end
    sub ax, 2
    sub ax, [sc_rgx]
    add ax, 4
.cells:
    push cx
    mov cl, 3
    sar ax, cl
    pop cx
    cmp byte [sc_ppop], SCPO_SETFIRST
    jne .arg
    sub al, [sc_rdl]                ; first is RELATIVE to the left indent
.arg:
    mov ah, al                      ; sc_modpap wants op in AL, arg in AH
    mov al, [sc_ppop]               ; (sc_modone clamps the cells)
    call sc_uclose
    call sc_modpap
    call sc_redraw
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rgxor - the drag guide: one XOR column over the text band at [sc_rgx]
sc_rgxor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [sc_rgx]
    mov cx, ax
    mov bx, [sc_ty]
    mov dx, [sc_bot]
    call OSAPI_GFX_XOR_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rgclamp - CX clamped onto the scale [sc_rdz-8 .. sc_rlxe]
sc_rgclamp:
    cmp cx, [sc_rlxe]
    jbe .hi
    mov cx, [sc_rlxe]
.hi:
    push ax
    mov ax, [sc_rdz]
    sub ax, 8
    cmp cx, ax
    jae .out
    mov cx, ax
.out:
    pop ax
    ret

; =============================================================================
; The status line (SPEC.md 68.2): 'Pg n  Sec 1  At n  Ln n  Col n' live from
; the caret, CAPS/NUM lamps from the BIOS shift flags at the right end. Page
; = SC_PGLINES lines; Ln restarts per page; Col is 1-based; At is the line on
; the page (draft view's honest unit is the line). The composed text is
; delta-cached like Note Pad's row diff: sc_stdiff letters only the cells
; that changed since the last draw, usually two or three.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_stsrc - refresh [sc_lnv]/[sc_colv] (absolute line, 1-based column) from
;            whatever the engine currently knows; keeps the old values when
;            nothing does (a bounded walk may not have stood on the caret)
; preserves all registers
; -----------------------------------------------------------------------------
sc_stsrc:
    push ax
    push cx
    cmp byte [sc_ckok], 0
    je .try2
    mov ax, [sc_ckpr]               ; the checkpoint: the caret's row start,
    add ax, [sc_top]                ; as a VISIBLE row (SPEC.md 27.4)
    js .try2
    mov [sc_lnv], ax
    mov ax, [sc_cur]
    sub ax, [sc_ckpi]
    inc ax
    mov [sc_colv], ax
    jmp short .out
.try2:
    cmp byte [sc_curseen], 0
    je .out
    mov ax, [sc_cury]
    sub ax, [sc_ty]
    js .out
    mov cl, 3
    shr ax, cl
    add ax, [sc_top]
    mov [sc_lnv], ax
    mov ax, [sc_curx]
    sub ax, [sc_tx]
    jns .cok
    xor ax, ax
.cok:
    shr ax, cl
    inc ax
    mov [sc_colv], ax
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stcat - append the NUL string SI at DI (the NUL is not copied)
; out: DI past the text; preserves everything else
; -----------------------------------------------------------------------------
sc_stcat:
    push ax
    push si
.cp:
    mov al, [si]
    or al, al
    jz .done
    mov [di], al
    inc si
    inc di
    jmp short .cp
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stcomp - compose the status text into sc_stbuf, space-padded to
;             SC_ST_CELLS and NUL-terminated; preserves all registers
; -----------------------------------------------------------------------------
sc_stcomp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, sc_stbuf
    mov si, sc_s_pg
    call sc_stcat
    mov ax, [sc_lnv]
    xor dx, dx
    mov bx, SC_PGLINES
    div bx                          ; AX = page-1, DX = line on the page - 1
    mov cx, dx
    inc ax
    push ax                         ; the page again, for the p/t field
    call sc_utoa
    mov si, sc_s_sec
    call sc_stcat
    pop ax                          ; --- p/t: the page over the total pages
    call sc_utoa                    ; (status.h's pageInDoc/totalPages field,
    mov byte [di], '/'              ; SPEC.md 68.2) - the total from
    inc di                          ; [sc_drows], a lower bound the height
    mov ax, [sc_drows]              ; count only ever raises
    add ax, SC_PGLINES - 1
    xor dx, dx
    div bx
    or ax, ax
    jnz .t1
    inc ax                          ; an empty document is page 1 of 1
.t1:
    call sc_utoa
    ; --- graceful field dropping (SPEC.md 68.2): a window too narrow to
    ; show every cell drops the At field FIRST - it duplicates Ln by
    ; construction (a page is 54 lines), and losing it keeps Ln and Col,
    ; which sc_stdiff's right-edge clamp would otherwise cut off
    push ax
    mov ax, [sc_cw]
    sub ax, 16
    js .noat
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    cmp ax, SC_ST_CELLS
    jb .noat
    pop ax
    mov si, sc_s_at
    call sc_stcat
    mov ax, cx
    inc ax
    call sc_utoa                    ; At = the line on the page, in li - the
    mov si, sc_s_li                 ; honest draft-view unit (SPEC.md 68.2)
    call sc_stcat
    jmp short .atdone
.noat:
    pop ax
.atdone:
    mov si, sc_s_ln
    call sc_stcat
    mov ax, cx
    inc ax
    call sc_utoa
    mov si, sc_s_col
    call sc_stcat
    mov ax, [sc_colv]
    or ax, ax
    jnz .c1
    inc ax
.c1:
    call sc_utoa
.pad:
    cmp di, sc_stbuf + SC_ST_CELLS
    jae .fin
    mov byte [di], ' '
    inc di
    jmp short .pad
.fin:
    mov byte [sc_stbuf + SC_ST_CELLS], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stdiff - letter the cells of sc_stbuf that differ from what the strip
;             shows (sc_stold), as ONE font_run of the changed span; then
;             bank the buffer. [sc_stok] = 0 forces the whole line.
; in:  sc_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
sc_stdiff:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor bx, bx                      ; BX = first differing cell
    mov dx, SC_ST_CELLS-1           ; DX = last
    cmp byte [sc_stok], 0
    je .have                        ; no old copy: the whole line differs
.f:
    mov al, [bx+sc_stbuf]
    cmp al, [bx+sc_stold]
    jne .l
    inc bx
    cmp bx, SC_ST_CELLS
    jb .f
    jmp .bank                       ; not a cell changed - nothing drawn
.l:
    mov dx, SC_ST_CELLS-1           ; ...and scan back for the last (stops at
.l2:                                ; BX by construction: BX itself differs)
    mov si, dx
    mov al, [si+sc_stbuf]
    cmp al, [si+sc_stold]
    jne .have
    dec dx
    jmp short .l2
.have:
    ; clamp the span to the cells a narrow window can show
    mov ax, [sc_cw]
    sub ax, 16
    js .bank
    mov cl, 3
    shr ax, cl
    or ax, ax
    jz .bank
    cmp bx, ax
    jae .bank
    dec ax
    cmp dx, ax
    jbe .span
    mov dx, ax
.span:
    push dx                         ; the span's last cell index
    mov si, dx
    mov ah, [si+sc_stbuf+1]         ; NUL-cap the span in place for the run,
    push ax                         ; then put the byte back
    mov byte [si+sc_stbuf+1], 0
    mov ax, bx
    mov cl, 3
    shl ax, cl
    add ax, [sc_cl]
    add ax, 8
    mov cx, ax
    mov dx, [sc_ct]
    add dx, [sc_ch]
    sub dx, 10                      ; the strip's text row
    mov si, bx
    add si, sc_stbuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    pop si
    mov [si+sc_stbuf+1], ah
.bank:
    push es
    push ds
    pop es
    cld
    mov si, sc_stbuf
    mov di, sc_stold
    mov cx, SC_ST_CELLS+1
    rep movsb
    pop es
    mov byte [sc_stok], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stkval - AL = the CAPS/NUM bits of the BIOS keyboard flag byte
; preserves everything else
;
; 0040:0017 is the BIOS keyboard flag byte - bit 6 CAPS lock, bit 5 NUM lock.
; The keyboard driver keeps BIOS int 16h alive, so the byte is maintained;
; reading it is a plain memory read through a scratch segment register, not a
; BIOS call, so any task and any lock state may do it (SPEC.md 68.2).
; -----------------------------------------------------------------------------
sc_stkval:
    push bx
    push es
    mov bx, 0x40
    mov es, bx
    mov al, [es:0x17]
    and al, 0x60
    pop es
    pop bx
    cmp byte [sc_ovr], 0            ; ...and our own two lamps fold into the
    je .noov                        ; unused low bits (SPEC.md 68.2), so the
    or al, 0x01                     ; worker's delta poll and sc_stat's both
.noov:                              ; notice an Ins or an F8 for free
    cmp byte [sc_ext], 0
    je .noex
    or al, 0x02
.noex:
    ret

; -----------------------------------------------------------------------------
; sc_kbflags - AL = the whole BIOS shift-flag byte at 0040:0017 (bits 0/1
; shift, 2 ctrl, 3 alt) - what tells Shift+Del from NumLock's '.', and
; Alt+BkSp from a backspace (SPEC.md 68.3). Preserves everything else.
; -----------------------------------------------------------------------------
sc_kbflags:
    push bx
    push es
    mov bx, 0x40
    mov es, bx
    mov al, [es:0x17]
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_stkdraw - draw both lamps from the flag bits in AL, and bank them
; in:  AL = CAPS/NUM bits, sc_bounds run, gfx lock held; preserves all
; A lit lamp is inverse video (font_run white-on-black); an unlit one is
; blank - status.h's highlighted-indicator convention, in the one font there
; is. Skipped whole on a window too narrow to keep them clear of the text.
; -----------------------------------------------------------------------------
sc_stkdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sc_stkf], al
    mov bx, [sc_sbr]
    sub bx, 13 + 128                ; EXT CAPS NUM OVR right-aligned, clear
                                    ; of the grow box: 16 cells with the gaps
                                    ; (status.h's indicator order, 65.2)
    mov dx, [sc_cl]
    add dx, 8 + SC_ST_CELLS*8
    cmp bx, dx
    jb .out                         ; the lamps would collide with the text
    mov dx, [sc_ct]
    add dx, [sc_ch]
    sub dx, 10
    mov cx, bx
    mov si, sc_s_ext                ; EXT (F8's extend mode)
    mov ah, CBLACK                  ; lit: white on black
    test al, 0x02
    jnz .eon
    mov si, sc_s_sp3                ; dark: erased to the strip's white
    mov ah, CWHITE
.eon:
    push ax
    mov al, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    add cx, 32                      ; CAPS, one cell of gap after EXT
    mov si, sc_s_caps
    mov ah, CBLACK
    test al, 0x40
    jnz .con
    mov si, sc_s_sp4
    mov ah, CWHITE
.con:
    push ax
    mov al, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    add cx, 40                      ; NUM
    mov si, sc_s_num
    mov ah, CBLACK
    test al, 0x20
    jnz .non
    mov si, sc_s_sp3
    mov ah, CWHITE
.non:
    push ax
    mov al, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    add cx, 32                      ; OVR (Ins's overtype)
    mov si, sc_s_ovr
    mov ah, CBLACK
    test al, 0x01
    jnz .oon
    mov si, sc_s_sp3
    mov ah, CWHITE
.oon:
    mov al, CWHITE
    call OSAPI_FONT_RUN
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stkchk - the worker's lamp poll: redraw the lamps ONLY on a change
; in:  gfx lock held (worker context); preserves all registers
; -----------------------------------------------------------------------------
sc_stkchk:
    push ax
    push bx
    cmp byte [sc_vsta], 0
    je .out
    cmp byte [sc_sigok], 0
    je .out                         ; no geometry has been banked yet
    cmp byte [sc_mopen], SC_M_NONE
    jne .out                        ; a dropdown may be over the strip
    cmp byte [sc_about], 0
    jne .out
    cmp word [sc_dlg], 0
    jne .out
    call sc_stkval
    cmp al, [sc_stkf]
    je .out
    mov bx, [sc_win]
    call OSAPI_WM_OBSCURED
    jc .out
    call sc_stkdraw
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_status - the status strip, whole: ground, rule, text, lamps
; in:  SI = window ptr, sc_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
sc_status:
    push ax
    push bx
    push cx
    push dx
    cmp byte [sc_vsta], 0
    je .out
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sc_cl]
    mov bx, ax
    add bx, [sc_cw]
    dec bx
    mov dx, [sc_ct]
    add dx, [sc_ch]
    sub dx, SC_STATUS_H
    call OSAPI_GFX_HLINE            ; the rule over the strip
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    mov bx, dx
    inc bx
    mov cx, ax
    add cx, [sc_cw]
    dec cx
    mov dx, [sc_ct]
    add dx, [sc_ch]
    dec dx
    call OSAPI_GFX_FILL             ; the strip's ground
    call sc_stsrc
    call sc_stcomp
    mov byte [sc_stok], 0           ; force the whole line over the fresh
    call sc_stdiff                  ; ground
    call sc_stkval
    call sc_stkdraw
    mov bx, si                      ; the strip's ground fill erased the grow
    call OSAPI_WM_GROW              ; box in its right corner (SPEC.md 11.1):
                                    ; put it back. No-op unless frontmost and
                                    ; sizable, so always safe
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_stat - the incremental status update every redraw tail makes
; in:  gfx lock held; preserves all registers
; Guards itself: strip hidden, geometry unbanked, or a dropdown/About box up
; (the modal close repaints the strip whole anyway) - then draws only the
; cells that changed, and the lamps only on a change.
; -----------------------------------------------------------------------------
sc_stat:
    push ax
    cmp byte [sc_vsta], 0
    je .out
    cmp byte [sc_sigok], 0
    je .out
    cmp byte [sc_mopen], SC_M_NONE
    jne .out
    cmp byte [sc_about], 0
    jne .out
    cmp word [sc_dlg], 0
    jne .out
    call sc_stsrc
    call sc_stcomp
    call sc_stdiff
    call sc_stkval
    cmp al, [sc_stkf]
    je .out
    call sc_stkdraw
.out:
    pop ax
    ret

; =============================================================================
; Menu interaction (SPEC.md 68.2). Both period styles at once:
;  - press-drag-release (Macintosh): the press opens the dropdown, dragging
;    moves an XOR highlight (and slides across the bar between menus), the
;    release fires the item under the pointer;
;  - click-open (Windows): press and release on the same title leaves the
;    menu OPEN ("sticky"), the next click fires or dismisses, and the
;    keyboard - Esc, arrows, Enter, mnemonic letters - drives it.
; Alt+mnemonic opens a menu from the keyboard with nothing else held.
; The tracking loop is sc_dragsel's shape exactly: unlock, yield to the next
; tick, relock, poll OSAPI_MOUSE (SPEC.md 27.8.1) - and never OSAPI_WM_ONMOUSEUP,
; which must not be mixed with a polling loop.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_mtrack - the press-drag-release gesture, from a press that opened AL
; in:  AL = menu to open, [sc_mabox] = the anchor band (title or combo box),
;      SI = window ptr, gfx lock held
; out: nothing (the menu is left OPEN only for a press-and-release on the
;      anchor - the sticky case); preserves all registers
; -----------------------------------------------------------------------------
sc_mtrack:
    push ax
    push bx
    push cx
    push dx
    call sc_mopenm
.loop:
    call sc_selpace                 ; unlock - yield to the tick - relock
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .release
    cmp byte [sc_mopen], SC_M_N     ; dragging across the bar slides between
    jae .items                      ; menus (titles only; combos have no bar)
    call sc_mbarhit
    cmp al, 0xFF
    je .items
    cmp al, [sc_mopen]
    je .items
    push ax
    call sc_mclose
    pop ax
    call sc_mtitler
    call sc_mopenm
    jmp short .loop
.items:
    call sc_mfind                   ; the XOR highlight follows the pointer
    cmp al, [sc_mhi]
    je .loop
    push ax
    mov al, [sc_mhi]
    call sc_mhl                     ; old band off (0xFF-safe)...
    pop ax
    mov [sc_mhi], al
    call sc_mhl                     ; ...new band on
    jmp short .loop
.release:
    call sc_mfind
    cmp al, 0xFF
    jne .fire
    push bx
    mov bx, sc_mabox
    call os88ui_bhit                ; released back on the anchor?
    pop bx
    jc .away
    jmp short .out                  ; yes: STICKY - the menu stays open
.away:
    call sc_mclose                  ; released elsewhere: dismissed
    jmp short .out
.fire:
    call sc_mact                    ; DL = the item's action...
    call sc_mclose                  ; ...the covered rows come back first...
    mov al, dl
    call sc_mfire                   ; ...and then it runs
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mclick_open - a click arriving while a dropdown is open (sticky mode)
; in:  CX = x, DX = y, SI = window ptr, gfx lock held; preserves all registers
; Fires an enabled item, toggles closed on the anchor, slides to another bar
; title, stays put for a dead spot inside the panel, and swallows the
; dismissing click anywhere else - a menu's click never leaks to the text.
; -----------------------------------------------------------------------------
sc_mclick_open:
    push ax
    push dx
    call sc_mfind
    cmp al, 0xFF
    jne .fire
    push bx
    mov bx, sc_mabox
    call os88ui_bhit
    pop bx
    jnc .toggle
    call sc_minrect                 ; a separator or disabled item: stay open
    jnc .out
    cmp byte [sc_mopen], SC_M_N
    jae .dismiss
    call sc_mbarhit                 ; a click on another title slides there
    cmp al, 0xFF
    je .dismiss
    cmp al, [sc_mopen]
    je .toggle
    push ax
    call sc_mclose
    pop ax
    call sc_mtitler
    call sc_mtrack                  ; ...and the new press tracks as a press
    jmp short .out
.toggle:
.dismiss:
    call sc_mclose
    jmp short .out
.fire:
    call sc_mact
    call sc_mclose
    mov al, dl
    call sc_mfire
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_minrect - is the point CX/DX inside the open dropdown's rectangle?
; out: CF=0 inside; preserves all registers
; -----------------------------------------------------------------------------
sc_minrect:
    cmp cx, [sc_mrx1]
    jb .no
    cmp cx, [sc_mrx2]
    ja .no
    cmp dx, [sc_mry1]
    jb .no
    cmp dx, [sc_mry2]
    ja .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_mroute - route a content click through the chrome (SPEC.md 68.2)
; in:  CX = x, DX = y, SI = window ptr, gfx lock held
; out: CF=1 the click was chrome's and is fully handled; CF=0 not ours.
;      Preserves CX/DX/SI (and everything else).
; -----------------------------------------------------------------------------
sc_mroute:
    push ax
    push bx
    push di
    call sc_bounds
    cmp word [sc_dlg], 0            ; an open dialog is modal before anything
    je .nodlg                       ; else is (SPEC.md 68.3)
    call sc_dgclick
    jmp .cons
.nodlg:
    cmp byte [sc_about], 0
    je .noab
    push bx                         ; the About box is modal: only its OK
    mov bx, sc_abok                 ; button answers, and every click is
    call os88ui_bhit                ; consumed
    pop bx
    jc .cons
    call sc_abclose
    jmp .cons
.noab:
    cmp byte [sc_mopen], SC_M_NONE
    je .closed
    call sc_mclick_open
    jmp .cons
.closed:
    ; the menu bar strip
    mov ax, [sc_ct]
    cmp dx, ax
    jb .pass
    add ax, SC_MENU_H-1
    cmp dx, ax
    ja .notbar
    call sc_settle                  ; opening a menu is not typing: the break
    call sc_uclose                  ; settles and the edit group closes, as
                                    ; for any command (SPEC.md 27.3/27.9)
    call sc_mbarhit
    cmp al, 0xFF
    je .cons                        ; the bar's blank tail
    call sc_mtitler
    call sc_mtrack
    jmp .cons
.notbar:
    ; the ribbon strip: the two combos open; the toggles are hit-tested and
    ; inert this stage (SPEC.md 68.2)
    cmp byte [sc_vrib], 0
    je .norib
    mov ax, [sc_ct]
    add ax, SC_MENU_H
    mov di, ax
    add di, SC_RIBBON_H-1
    cmp dx, ax
    jb .norib
    cmp dx, di
    ja .norib
    call sc_settle
    call sc_uclose
    mov bx, [sc_cl]
    add bx, SC_RB_FBX
    cmp cx, bx
    jb .cons
    mov di, bx
    add di, SC_RB_FBW-1
    cmp cx, di
    ja .rbpts
    mov al, SC_M_FONTC
    call sc_fontscan                ; the machine's faces, listed the first
                                    ; time this combo is opened and never
                                    ; again (SPEC.md 19.8): the scan is real
                                    ; floppy I/O, and a menu nobody opens
                                    ; should cost nothing
    mov al, SC_M_FONTC
    jmp short .combo1
.rbpts:
    mov bx, [sc_cl]
    add bx, SC_RB_PBX
    cmp cx, bx
    jb .cons
    mov di, bx
    add di, SC_RB_PBW-1
    cmp cx, di
    ja .rbtns
    mov al, SC_M_PTSC
.combo1:
    mov [sc_max], bx                ; the dropdown hangs off the box, and the
    mov [sc_mabox], bx              ; box IS the gesture anchor
    mov [sc_mabox+4], di
    mov bx, [sc_ct]
    add bx, SC_MENU_H + SC_RIBBON_H
    mov [sc_may], bx
    sub bx, SC_RIBBON_H
    add bx, 2
    mov [sc_mabox+2], bx
    add bx, 11
    mov [sc_mabox+6], bx
    call sc_mtrack
    jmp .cons
.rbtns:
    ; the toggle cells (SPEC.md 68.3): B I K | U W D fire sc_applyattr, the
    ; pilcrow toggles Show-all, the greyed super/sub pair is inert. The hit
    ; geometry is sc_rbxy's - the same rows the painter and the delta update
    ; read, so a cell cannot be drawn one place and clicked another
    xor ax, ax                      ; AL = the cell being tested
.rbt:
    push ax
    call sc_rbxy                    ; AX = x, BX = y (CF: not drawn)
    jc .rbtn
    cmp cx, ax
    jb .rbtn
    add ax, SC_BTN_W-1
    cmp cx, ax
    ja .rbtn
    pop ax                          ; the hit: AL = the cell
    cmp al, 6
    je .rbsa
    xor bh, bh
    mov bl, al
    mov al, [sc_rbmask+bx]
    call sc_applyattr
    call sc_redraw                  ; rows the toggle dirtied + the cell's
    jmp .cons                       ; inversion (sc_redraw's tail)
.rbsa:
    xor byte [sc_showall], 1        ; Show-all: the pilcrow cells appear in
    call sc_redraw                  ; every row that carries a mark - the
    jmp .cons                       ; signatures find them (SPEC.md 68.1)
.rbtn:
    pop ax
    inc ax
    cmp al, 7
    jb .rbt
    jmp .cons                       ; the strip's blank ground, and the greyed
                                    ; super/sub cell: inert
.norib:
    ; the ruler strip (SPEC.md 68.3): the Style combo opens; the alignment,
    ; spacing and open/close cells FIRE (sc_modpap, the same door as the
    ; keys); the scale drags its indent markers; the greyed tab cells are
    ; inert
    cmp byte [sc_vrul], 0
    je .norul
    call sc_ruly
    mov di, ax
    add di, SC_RULER_H-1
    cmp dx, ax
    jb .norul
    cmp dx, di
    ja .norul
    call sc_settle
    call sc_uclose
    call sc_ruly                    ; row 2 is the scale, whatever the x: it
    add ax, SC_RL_ROW2              ; is a row of its own now and the cells
    cmp dx, ax                      ; cannot be under it
    jb .rlrow1
    call sc_rldrag
    jmp .cons
.rlrow1:
    mov bx, [sc_cl]
    add bx, SC_RL_SBX
    cmp cx, bx
    jb .cons
    mov di, bx
    add di, SC_RL_SBW-1
    cmp cx, di
    ja .rlcells
    mov [sc_max], bx
    mov [sc_mabox], bx
    mov [sc_mabox+4], di
    call sc_ruly
    add ax, SC_RULER_H
    mov [sc_may], ax
    sub ax, SC_RULER_H
    add ax, 2
    mov [sc_mabox+2], ax
    add ax, 11
    mov [sc_mabox+6], ax
    mov al, SC_M_STYLEC
    call sc_mtrack
    jmp .cons
.rlcells:
    mov di, cx                      ; the click x, banked: sc_rlxy answers its
    xor ax, ax                      ; width in CX. AL = the cell being tested;
.rlc:                               ; the geometry is the painter's (SPEC.md 22)
    push ax
    call sc_rlxy                    ; AX = x, BX = y, CX = width
    jc .rlcn
    cmp di, ax
    jb .rlcn
    add ax, cx
    dec ax
    cmp di, ax
    jbe .rlhit
.rlcn:
    pop ax
    inc ax
    cmp al, 9
    jb .rlc
    mov cx, di                      ; the click x back (mroute preserves it)
    jmp short .rlscl
.rlhit:
    pop ax                          ; AL = the cell
    mov cx, di
    cmp al, 4
    jae .rlsp
    mov ah, al                      ; 0..3: alignment
    mov al, SCPO_ALIGN
    jmp short .rlgo
.rlsp:
    cmp al, 7
    jae .rloc
    mov ah, al                      ; 4..6: line spacing 1 / 1.5 / 2
    sub ah, 4
    mov al, SCPO_SPACE
    jmp short .rlgo
.rloc:
    mov ah, al                      ; 7 = closed (arg 0), 8 = open (arg 1)
    sub ah, 7
    mov al, SCPO_SB
.rlgo:
    call sc_modpap
    call sc_redraw                  ; the rows that moved; the tail re-inverts
    jmp .cons                       ; the cells (sc_rlstat)
.rlscl:                             ; row 1 past the last cell: dead ground
    jmp .cons
.norul:
    ; the status strip is inert; the grow box never reaches W_ONCLICK
    cmp byte [sc_vsta], 0
    je .pass
    mov ax, [sc_bot]
    cmp dx, ax
    ja .cons
.pass:
    clc
    jmp short .done
.cons:
    stc
.done:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mkey - route a key through the menu system (SPEC.md 68.2)
; in:  AL = ascii, AH = scan, SI = window ptr, gfx lock held
; out: CF=1 consumed. An open dropdown (and the About box) is MODAL and
;      consumes everything: Esc closes, Left/Right slide along the bar,
;      Up/Down move the highlight over enabled items, Enter fires the
;      highlighted one, a mnemonic letter fires its item. With nothing open,
;      only Alt+<title mnemonic> is claimed (int 16h hands Alt+letter over
;      as ascii 0 + the letter's scan code).
; -----------------------------------------------------------------------------
sc_mkey:
    push bx
    push cx
    push dx
    push di
    cmp word [sc_dlg], 0            ; the dialog is modal (SPEC.md 68.3):
    je .nodlg                       ; Enter fires OK, Esc cancels, a letter
    call sc_dgkey                   ; toggles its box, and nothing leaks
    jmp .cons
.nodlg:
    cmp byte [sc_about], 0
    je .noab
    cmp al, 13                      ; the About box: OK is Enter, Esc or Space
    je .abx
    cmp al, 27
    je .abx
    cmp al, ' '
    je .abx
    jmp .cons                       ; everything else bounces off the modal
.abx:
    call sc_abclose
    jmp .cons
.noab:
    cmp byte [sc_mopen], SC_M_NONE
    jne .open
    or al, al
    jnz .pass
    push si
    mov si, sc_alttab               ; Alt+mnemonic: match the scan code
    xor bx, bx
.at:
    cmp ah, [si+bx]
    je .atgo
    inc bx
    cmp bx, SC_M_N
    jb .at
    pop si
    jmp .pass
.atgo:
    pop si
    call sc_bounds
    call sc_settle
    call sc_uclose
    mov ax, bx
    call sc_mtitler
    call sc_mopenm                  ; opened sticky, no highlight yet
    jmp .cons
.open:
    cmp al, 27
    je .close
    cmp al, 13
    je .enter
    or al, al
    jz .ext
    cmp al, 'a'                     ; a mnemonic letter fires its item
    jb .upok
    cmp al, 'z'
    ja .upok
    sub al, 32
.upok:
    mov dl, al
    mov al, [sc_mopen]
    call sc_mgeti
    mov cl, [bx+3]
    xor ch, ch
    push si
    mov si, [bx+4]
.mn:
    jcxz .mnno
    cmp dl, [si+3]
    je .mnhit
    add si, 8
    dec cx
    jmp short .mn
.mnhit:
    test byte [si], SCMF_DIS
    jnz .mnno                       ; a greyed mnemonic answers nothing
    mov dl, [si+2]
    pop si
    call sc_mclose
    mov al, dl
    call sc_mfire
    jmp .cons
.mnno:
    pop si
    jmp .cons
.close:
    call sc_mclose
    jmp .cons
.enter:
    mov al, [sc_mhi]
    cmp al, 0xFF
    je .close                       ; Enter with nothing highlighted = close
    call sc_mact
    call sc_mclose
    mov al, dl
    call sc_mfire
    jmp .cons
.ext:
    cmp ah, SC_K_LEFT
    je .prevm
    cmp ah, SC_K_RIGHT
    je .nextm
    cmp ah, SC_K_UP
    je .up
    cmp ah, SC_K_DOWN
    je .down
    jmp .cons                       ; the modal swallows the rest
.prevm:
    mov al, [sc_mopen]
    cmp al, SC_M_N
    jae .cons                       ; a combo has no neighbours
    dec al
    cmp al, 0xFF
    jne .switch
    mov al, SC_M_N-1
    jmp short .switch
.nextm:
    mov al, [sc_mopen]
    cmp al, SC_M_N
    jae .cons
    inc al
    cmp al, SC_M_N
    jb .switch
    xor al, al
.switch:
    push ax
    call sc_mclose
    pop ax
    call sc_mtitler
    call sc_mopenm
    jmp .cons
.up:
    mov dl, -1
    call sc_mstep
    jmp .cons
.down:
    mov dl, 1
    call sc_mstep
    jmp .cons
.pass:
    clc
    jmp short .done
.cons:
    stc
.done:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_mstep - move the keyboard highlight one enabled item in direction DL
;            (+1 down / -1 up), wrapping; preserves all registers
; -----------------------------------------------------------------------------
sc_mstep:
    push ax
    push bx
    push cx
    push si
    mov al, [sc_mopen]
    call sc_mgeti
    mov cl, [bx+3]                  ; CL = count, CH = tries left
    or cl, cl
    jz .out
    mov ch, cl
    mov al, [sc_mhi]
    cmp al, 0xFF
    jne .step
    or dl, dl                       ; nothing highlighted: step onto an end
    jns .step                       ; (0xFF + 1 wraps to 0 by itself; going
    mov al, cl                      ; up starts one past the last)
.step:
    add al, dl
    cmp al, 0xFF
    jne .w1
    mov al, cl
    dec al
.w1:
    cmp al, cl
    jb .w2
    xor al, al
.w2:
    push ax
    call sc_mitemp
    test byte [si], SCMF_SEP | SCMF_DIS
    pop ax
    jz .found
    dec ch
    jnz .step
    jmp short .out                  ; nothing selectable at all
.found:
    mov ah, al
    mov al, [sc_mhi]
    call sc_mhl                     ; old off...
    mov [sc_mhi], ah
    mov al, ah
    call sc_mhl                     ; ...new on
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_mfire - dispatch an action byte (SPEC.md 68.2)
; in:  AL = SCA_*, SI = window ptr; gfx lock held, the menu already closed
;      and its cover repainted
; out: clobbers registers like any callback (callers bank what they keep);
;      SI survives every action
; The actions are the SAME routines the shortcut keys call - menu items and
; keys are twin doors that cannot drift apart (SPEC.md 27.8).
; -----------------------------------------------------------------------------
sc_mfire:
    cmp al, SCA_MAX
    ja sc_mf_ret
    push bx
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [bx+sc_ftab]
    pop bx
    jmp ax

sc_ftab:
    dw sc_mf_ret                    ; SCA_NONE
    dw sc_a_new
    dw sc_a_open
    dw sc_a_close                   ; File > Close...
    dw sc_a_save
    dw sc_a_saveas
    dw sc_a_close                   ; ...and Exit: one document, one window
    dw sc_a_undo
    dw sc_a_cut
    dw sc_a_copy
    dw sc_a_paste
    dw sc_a_search
    dw sc_a_repl
    dw sc_a_draft                   ; View > Draft (SPEC.md 68.11)
    dw sc_a_vrib
    dw sc_a_vrul
    dw sc_a_vsta
    dw sc_abopen                    ; Help > About...
    dw sc_mf_ret                    ; Window > 1: the one window, checked
    dw sc_a_csel                    ; a combo entry: cosmetic, EXCEPT the
                                    ; Font one (SPEC.md 68.13)
    dw sc_a_char                    ; Format > Character... (SPEC.md 68.3)
    dw sc_a_para                    ; Format > Paragraph... (SPEC.md 68.3)
    dw sc_a_goto                    ; Edit > Go To... (SPEC.md 68.7)
    dw sc_a_sort                    ; Utilities > Sort... (SPEC.md 68.9)
    dw sc_a_renum                   ; Utilities > Renumber...
    dw sc_a_toc                     ; Insert > Table of Contents...
    dw sc_a_page                    ; View > Page (SPEC.md 68.11)
    dw sc_a_pict                    ; Insert > Picture... (SPEC.md 94.9)

sc_mf_ret:
    ret

; -----------------------------------------------------------------------------
; sc_a_pict - Insert > Picture... (SPEC.md 94.9)
;
; Ask for a file, read it, and decode it through SCRIBE.OVL. The picture is
; measured and reported and NOT yet put in the document: the model, the
; layout and the file format are 88.9's remaining three parts, and the
; message says which stage this is rather than implying more.
;
; It routes through the ordinary file dialog with [sc_pictwant] raised, so
; sc_ondlg sends the answer here instead of to open-or-save. One flag rather
; than a third FDLG mode, because the kernel's two modes are its contract and
; a package's own reason for opening the dialog is the package's business.
; -----------------------------------------------------------------------------
sc_a_pict:
    call sc_uclose
    mov byte [sc_pictwant], 1
    mov al, FDLG_OPEN
    jmp sc_dlgopen                  ; no repaint: the dialog is over us

; -----------------------------------------------------------------------------
; sc_pictload - read [sc_name] and decode it. Reports either way.
;
; TWO TRANSIENT CLAIMS, both handed straight back: the file's bytes and the
; decoded picture. Transient because 50.3 is about a package SIZING itself at
; entry, and neither of these is part of how big WORD is - they are the shape
; of one command. A refusal is an ordinary path (47): the document is
; untouched and still editable.
; -----------------------------------------------------------------------------
sc_pictload:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [sc_picseg], 0
    call sc_stghold                 ; the file's bytes (the toast is set on
    jc .out                         ; failure, and it is the right one)
    mov es, [sc_stgseg]
    xor bx, bx
    mov cx, 0xFFFF                  ; DX:CX is the capacity, and 64K does not
    xor dx, dx                      ; fit the word CX is
    mov si, sc_name
    call OSAPI_FILE_READ            ; out DX:AX = the size read
    jc .ioerr
    or dx, dx
    jnz .toobig                     ; a picture file past 64KB is past what one
    mov si, sc_imgblk               ; segment addresses, which is os88img's own
    mov [si+IMG_SRCLEN], ax         ; limit too
    mov ax, [sc_stgseg]
    mov [si+IMG_SRCSEG], ax
    mov ax, SC_PICKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [sc_picseg], dx
    mov si, sc_imgblk
    mov [si+IMG_DSTSEG], dx
    mov word [si+IMG_DSTMAX], SC_PICKB * 1024 - 1
    mov word [si+IMG_ROWBUF], sc_imgrow
    mov word [si+IMG_PICNO], 0      ; a .PIX is an ARCHIVE: 0 = whichever is
                                    ; first, since picture numbers are not
                                    ; contiguous and a document does not know
                                    ; what they are (87.1)
    mov bp, SCM_IMGLOAD
    call sc_ovcall                  ; ...out to SCRIBE.OVL, and back
    jc .decerr
    call sc_pictkeep                ; the scratch claim's contents into one
    jc .full                        ; sized for them, and a table slot
    call sc_pictput                 ; ...and SC_PICCH into the text
    mov si, sc_imgblk
    mov ax, [si+IMG_W]
    mov bx, [si+IMG_H]
    call sc_pictsay
    jmp short .done
.full:
    mov ax, sc_e_pictfull
    call sc_saymsg
    jmp short .done
.decerr:
    mov si, sc_imgblk
    mov ax, [si+IMG_ERR]
    call sc_picterr
    jmp short .done
.ioerr:
    mov ax, sc_e_pictio
    call sc_saymsg
    jmp short .done
.toobig:
    mov ax, sc_e_pictbig
    call sc_saymsg
    jmp short .done
.nomem:
    mov ax, sc_e_nomem
    call sc_saymsg
.done:
    mov dx, [sc_picseg]             ; both claims go straight back - nothing
    or dx, dx                       ; holds the picture yet
    jz .nofree
    call OSAPI_MEM_FREE
    mov word [sc_picseg], 0
.nofree:
    call sc_stgdrop
.out:
    pop es                          ; the pushes are ax bx cx dx si di es, so
    pop di                          ; these unwind es di si dx cx bx ax. SI
    pop si                          ; was missing here and `ret` took its
    pop dx                          ; saved value for the return address: the
    pop cx                          ; whole machine, segments and all, ended
    pop bx                          ; up at 000E:00DF inside the vector table
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_pictkeep - the decoded picture is in the SC_PICKB scratch claim; move it
; into a claim SIZED FOR IT and take a table slot. out: CF=1 = no slot or no
; memory, and nothing was taken.
;
; Two claims and a copy rather than keeping the scratch, because the scratch is
; 40KB whatever the picture is and eight of those is 320KB of a 640KB machine
; for eight small drawings. The decoder cannot size its own destination - it
; has to be given one before it knows the dimensions - so the sizing happens
; here, afterwards, where they are known.
; -----------------------------------------------------------------------------
sc_pictkeep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    mov ax, [sc_npic]
    cmp ax, SC_PICMAX
    jae .no
    mov si, sc_imgblk
    mov ax, [si+IMG_STRIDE]
    mul word [si+IMG_H]             ; DX:AX = the bytes it actually occupies.
    or dx, dx                       ; img_setgeom already proved this fits a
    jnz .no                         ; word, so a high half means something is
    mov cx, ax                      ; wrong rather than merely large
    add ax, 1023
    jc .no
    mov cl, 10
    shr ax, cl                      ; ...in kilobytes, rounded up
    or ax, ax
    jnz .kbok
    mov ax, 1
.kbok:
    call OSAPI_MEM_CLAIM
    jc .no
    mov [sc_picnew], dx
    mov si, sc_imgblk               ; scratch -> its own claim, a word at a
    mov ax, [si+IMG_STRIDE]         ; time. rep movsw would need DS pointed at
    mul word [si+IMG_H]             ; the source, and DS is the package here
    mov cx, ax
    inc cx
    shr cx, 1
    mov ax, [si+IMG_DSTSEG]
    mov ds, ax
    mov es, [cs:sc_picnew]
    xor si, si
    xor di, di
.copy:
    mov ax, [si]
    mov [es:di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .copy
    pop ds
    push ds
    mov bx, [sc_npic]               ; the slot: w, h, stride, segment
    mov ax, bx
    mov cl, 3
    shl ax, cl                      ; * SC_PICREC
    add ax, sc_pictab
    mov di, ax
    mov si, sc_imgblk
    mov ax, [si+IMG_W]
    mov [di], ax
    mov ax, [si+IMG_H]
    mov [di+2], ax
    mov ax, [si+IMG_STRIDE]
    mov [di+4], ax
    mov ax, [sc_picnew]
    mov [di+6], ax
    inc word [sc_npic]
    clc
    jmp short .out
.no:
    stc
.out:
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_pictput - put SC_PICCH in the text at the caret, its CHP byte the index
; of the picture just kept. Preserves all registers.
;
; [sc_chp] is banked around the insert because sc_ins writes the TYPING
; attributes as the character's CHP, and for this one character that byte is
; not attributes at all.
sc_pictput:
    push ax
    push bx
    mov al, [sc_chp]
    mov [sc_chpbank], al
    mov ax, [sc_npic]
    dec ax
    mov [sc_chp], al
    mov al, SC_PICCH
    call sc_ins
    mov al, [sc_chpbank]
    mov [sc_chp], al
    pop bx
    pop ax
    ret

; sc_pictfree - hand every picture claim back and empty the table. Called
; where the document is replaced (New, and each reader), for the reason
; sh_nnames is cleared there: state that outlives its document goes on
; describing one that is gone.
sc_pictfree:
    push ax
    push bx
    push cx
    push dx
    push si                         ; SI is used as the slot pointer below and
    mov cx, [sc_npic]               ; every caller is a document-replacing path
                                    ; with its own state in it
    jcxz .done
    xor bx, bx
.each:
    mov ax, bx
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, sc_pictab
    mov si, ax
    mov dx, [si+6]
    or dx, dx
    jz .next
    call OSAPI_MEM_FREE
.next:
    inc bx
    loop .each
.done:
    mov word [sc_npic], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_pictsay - AX = width, BX = height: "Picture 240x160 read - placing it
; is not built yet". The last clause is there on purpose. A command that
; reports a success it did not have is worse than one that refuses (47), and
; this stage genuinely reads the file and genuinely does not insert it.
sc_pictsay:
    push ax
    push bx
    push si
    push di
    mov di, sc_pbuf
    call sc_utoa                    ; AX = the width
    mov si, sc_s_pictx
    call sc_pcopy
    mov ax, bx
    call sc_utoa
    mov si, sc_s_pict2
    call sc_pcopy
    mov byte [di], 0
    mov ax, sc_pbuf
    call sc_saymsg
    pop di
    pop si
    pop bx
    pop ax
    ret

; sc_picterr - AX = an IMG_E_* code, said in THIS package's words. The include
; returns a number and never a string, because a string out in SCRIBE.OVL is at
; a module-relative offset a resident reader takes for something else (87.2).
sc_picterr:
    push ax
    push bx
    push si
    mov bx, ax
    cmp bx, SC_PICTERRN
    jbe .known
    xor bx, bx
.known:
    shl bx, 1
    mov ax, [bx+sc_picterrs]
    call sc_saymsg
    pop si
    pop bx
    pop ax
    ret

; sc_pcopy - the NUL string SI to DI, DI advancing past it. sc_utoa's own
; shape, so the two compose.
sc_pcopy:
    push ax
.l:
    mov al, [si]
    or al, al
    jz .done
    mov [di], al
    inc si
    inc di
    jmp short .l
.done:
    pop ax
    ret

; File > New / Open... ask the dirty question first (SPEC.md 68.4); the
; sc_do* halves are what Yes/No proceed to, and what a clean document runs
; directly.
sc_a_new:
    push ax
    mov al, SCP_NEW
    call sc_askdirty
    pop ax
    jc sc_mf_ret
    ; falls through
sc_donew:
    call sc_uclose
    call sc_new
    mov byte [sc_follow], 1
    jmp sc_redraw

sc_a_open:
    push ax
    mov al, SCP_OPEN
    call sc_askdirty
    pop ax
    jc sc_mf_ret
    ; falls through
sc_doopen:
    call sc_uclose
    mov al, FDLG_OPEN
    jmp sc_dlgopen                  ; no repaint: the dialog is on top of us

sc_a_saveas:
    call sc_uclose
    mov al, FDLG_SAVE
    jmp sc_dlgopen

sc_a_save:
    call sc_uclose
    jmp sc_save                     ; the toast is the whole visible change

sc_a_undo:
    call sc_undo
    jnc .ok
    mov ax, sc_m_noundo
    call sc_saymsg
    jmp short .rd
.ok:
    call sc_chpsync                 ; the caret landed somewhere new (65.3)
.rd:
    mov byte [sc_follow], 1
    jmp sc_redraw

sc_a_cut:
    call sc_cut
    mov byte [sc_follow], 1
    jmp sc_redraw

sc_a_copy:
    jmp sc_copy                     ; nothing on screen moves: the selection
                                    ; stays and the clipboard is invisible

sc_a_paste:
    call sc_paste
    mov byte [sc_follow], 1
    jmp sc_redraw

; Edit > Search... / Replace... open the legacy find panel this stage - the
; real Search/Replace DIALOGS are a later stage; the panel is the same
; feature behind the authentic menu item (SPEC.md 68.2).
; Edit > Search... / Replace... / Go To... - the authentic modal dialogs
; (SPEC.md 68.7). Each presets [sc_dck] (the check bits) and its radios
; before sc_dgopen paints, then focuses its first edit box.
sc_a_search:
    push ax
    push bx
    push di
    mov al, [sc_fopt]
    and al, SCFO_WORD | SCFO_CASE
    mov [sc_dck], al
    call sc_dirset                  ; the Direction radios from [sc_fdir]
    mov bx, sc_dlgsrch
    call sc_dgopen
    cmp [sc_dlg], bx
    jne .nofoc
    mov di, sc_ds_edit
    call sc_dgfocus
.nofoc:                 ; the Search For box takes the keys
    pop di
    pop bx
    pop ax
    ret

sc_a_repl:
    push ax
    push bx
    push di
    mov al, [sc_fopt]               ; all three bits: Confirm Changes rides
    mov [sc_dck], al                ; along (SPEC.md 68.7)
    mov bx, sc_dlgrepl
    call sc_dgopen
    cmp [sc_dlg], bx
    jne .nofoc
    mov di, sc_dr_edit
    call sc_dgfocus
.nofoc:
    pop di
    pop bx
    pop ax
    ret

sc_a_goto:
    push ax
    push bx
    push di
    mov byte [sc_de_goto], 0        ; the box opens empty
    mov byte [sc_dck], 0
    mov bx, sc_dlggoto
    call sc_dgopen
    cmp [sc_dlg], bx
    jne .nofoc
    mov di, sc_dg_edit
    call sc_dgfocus
.nofoc:
    pop di
    pop bx
    pop ax
    ret

; --- the three Utilities dialogs (SPEC.md 68.9) ------------------------------
; Each opens with its radios and check boxes showing the state it last ran
; with, so a second Sort remembers the first one's key - which is what makes
; sorting on two fields two commands rather than a re-typed dialog.
sc_a_sort:
    push ax
    push bx
    push di
    mov al, [sc_sopt]
    and al, SCSO_CASE
    shr al, 1
    shr al, 1
    mov [sc_dck], al                ; the Case Sensitive box is bit 1
    mov bx, sc_so_asc               ; Ascending / Descending
    mov al, [sc_sopt]
    and al, SCSO_DESC
    call sc_radset
    mov bx, sc_so_alp               ; Alphanumeric / Numeric
    mov al, [sc_sopt]
    and al, SCSO_NUM
    call sc_radset
    mov bx, sc_so_com               ; Comma / Tab
    mov al, [sc_sopt]
    and al, SCSO_TAB
    call sc_radset
    mov ax, [sc_sofld]
    or ax, ax                       ; the key is SOME field, and the first
    jnz .fld                        ; one is the default
    mov ax, 1
.fld:
    mov [sc_sofld], ax
    mov bx, sc_de_fld
    call sc_unum
    mov bx, sc_dlgsort
    call sc_dgopen
    pop di
    pop bx
    pop ax
    ret

sc_a_renum:
    push ax
    push bx
    push di
    mov byte [sc_dck], 0
    mov bx, sc_rn_all               ; a THREE-way radio: the first is
    mov al, [sc_rnact]              ; selected when the value is 0, and
    call sc_rad3                    ; sc_rad3 says which of three
    mov bx, sc_rn_auto
    mov al, [sc_rnman]
    call sc_radset
    mov ax, [sc_rnfrom]
    mov bx, sc_de_start
    call sc_unum
    cmp byte [sc_rnfmt], 0          ; the format the real product starts with
    jne .havef
    mov byte [sc_rnfmt], '1'
    mov byte [sc_rnfmt+1], '.'
    mov byte [sc_rnfmt+2], 0
.havef:
    mov bx, sc_dlgrenum
    call sc_dgopen
    pop di
    pop bx
    pop ax
    ret

sc_a_toc:
    push ax
    push bx
    push di
    mov byte [sc_dck], 0
    mov bx, sc_tc_head
    mov al, [sc_tcsrc]
    call sc_radset
    mov bx, sc_tc_all               ; All / From..To
    mov al, [sc_tclvl]
    call sc_radset
    mov ax, [sc_tcfrom]
    or ax, ax
    jnz .haver
    mov ax, 1
    mov [sc_tcfrom], ax
    mov word [sc_tcto], 9
.haver:
    mov bx, sc_de_from
    call sc_unum
    mov ax, [sc_tcto]
    mov bx, sc_de_to
    call sc_unum
    mov bx, sc_dlgtoc
    call sc_dgopen
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The three Utilities dialogs' OK verbs (SPEC.md 68.9). Each banks its radios
; and edits into the command's state, closes, runs, and repaints - the same
; shape sc_dsapply has, so the search dialogs and these cannot drift.
; -----------------------------------------------------------------------------
sc_dsoapply:
    push ax
    push bx
    call sc_dgclose
    xor al, al
    cmp word [sc_so_des+8], 0
    je .n1
    or al, SCSO_DESC
.n1:
    cmp word [sc_so_num+8], 0
    je .n2
    or al, SCSO_NUM
.n2:
    cmp word [sc_so_tab+8], 0
    je .n3
    or al, SCSO_TAB
.n3:
    test byte [sc_dck], 2
    jz .n4
    or al, SCSO_CASE
.n4:
    mov [sc_sopt], al
    mov bx, sc_de_fld
    call sc_uatoi
    or ax, ax                       ; field 0 is field 1: a key has to be
    jnz .fld                        ; SOME field
    mov ax, 1
.fld:
    mov [sc_sofld], ax
    push si                         ; the command uses SI as scratch, and
    call sc_dosort                     ; sc_redraw wants the WINDOW there
    pop si
    call sc_redraw
    pop bx
    pop ax
    ret

sc_drnapply:
    push ax
    push bx
    call sc_dgclose
    mov al, SCRN_ALL
    cmp word [sc_rn_numd+8], 0
    je .n1
    mov al, SCRN_NUMD
.n1:
    cmp word [sc_rn_rem+8], 0
    je .n2
    mov al, SCRN_REMOVE
.n2:
    mov [sc_rnact], al
    xor al, al
    cmp word [sc_rn_man+8], 0
    je .n3
    mov al, 1
.n3:
    mov [sc_rnman], al
    mov ax, 1                       ; Automatic numbers from 1; Manual from
    or al, al                       ; the Start at value
    jz .start
    mov bx, sc_de_start
    call sc_uatoi
    or ax, ax
    jnz .start
    mov ax, 1
.start:
    mov [sc_rnfrom], ax
    push si                         ; the command uses SI as scratch, and
    call sc_dorenum                     ; sc_redraw wants the WINDOW there
    pop si
    call sc_redraw
    pop bx
    pop ax
    ret

sc_dtcapply:
    push ax
    push bx
    call sc_dgclose
    xor al, al
    cmp word [sc_tc_fld+8], 0
    je .n1
    mov al, 1
.n1:
    mov [sc_tcsrc], al
    xor al, al
    cmp word [sc_tc_rng+8], 0
    je .n2
    mov al, 1
.n2:
    mov [sc_tclvl], al
    mov word [sc_tcfrom], 1         ; All is levels 1..9
    mov word [sc_tcto], 9
    or al, al
    jz .go
    mov bx, sc_de_from
    call sc_uatoi
    or ax, ax
    jnz .f
    mov ax, 1
.f:
    mov [sc_tcfrom], ax
    mov bx, sc_de_to
    call sc_uatoi
    or ax, ax
    jnz .t
    mov ax, 9
.t:
    mov [sc_tcto], ax
.go:
    push si                         ; the command uses SI as scratch, and
    call sc_dotoc                     ; sc_redraw wants the WINDOW there
    pop si
    call sc_redraw
    pop bx
    pop ax
    ret

; sc_radset - a two-way radio pair at BX: AL = 0 selects the first, non-zero
; the second. Preserves all registers.
sc_radset:
    push ax
    push bx
    or al, al
    mov word [bx+8], 1
    mov word [bx+12+8], 0
    jz .out
    mov word [bx+8], 0
    mov word [bx+12+8], 1
.out:
    pop bx
    pop ax
    ret

; sc_rad3 - a three-way radio row at BX: AL = 0, 1 or 2. Preserves all.
sc_rad3:
    push ax
    push bx
    mov word [bx+8], 0
    mov word [bx+12+8], 0
    mov word [bx+24+8], 0
    xor ah, ah
    cmp al, 2
    jbe .ok
    xor al, al
.ok:
    mov ah, 12
    mul ah
    add bx, ax
    mov word [bx+8], 1
    pop bx
    pop ax
    ret

; sc_unum - write unsigned AX into the NUL buffer at BX. Preserves all.
sc_unum:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
.d:
    xor dx, dx
    push bx
    mov bx, 10
    div bx
    pop bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop ax
    add al, '0'
    mov [bx], al
    inc bx
    loop .e
    mov byte [bx], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_uatoi - AX = the unsigned number in the NUL buffer at BX (0 if none).
; Preserves all registers but AX.
sc_uatoi:
    push bx
    push cx
    push dx
    xor cx, cx
.l:
    mov dl, [bx]
    cmp dl, '0'
    jb .done
    cmp dl, '9'
    ja .done
    cmp cx, 6000
    jae .skip
    sub dl, '0'                     ; the digit is taken out of DX BEFORE the
    xor dh, dh                      ; multiply: a 16-bit mul writes DX:AX and
    push dx                         ; would otherwise overwrite it with the
    mov ax, cx                      ; high half of the product
    push bx
    mov bx, 10
    mul bx
    pop bx
    pop dx
    add ax, dx
    mov cx, ax
.skip:
    inc bx
    jmp short .l
.done:
    mov ax, cx
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; View > Draft / View > Page (SPEC.md 68.11)
; in:  SI = window ptr; out: nothing; clobbers as a callback
;
; The view is one byte, and everything that differs falls out of it in
; sc_bounds: the sheet's width and where it sits. Switching re-lays the whole
; document, so both go through the full redraw rather than a damage range -
; this is the one action in the app that legitimately repaints everything,
; because everything moved.
; -----------------------------------------------------------------------------
sc_a_draft:
    cmp byte [sc_vpage], 0
    je sc_vwsame
    mov byte [sc_vpage], 0
    jmp short sc_vwset

sc_a_page:
    cmp byte [sc_vpage], 0
    jne sc_vwsame
    mov byte [sc_vpage], 1
sc_vwset:
    call sc_bounds                  ; the sheet moved, so the wrap width and
    mov byte [sc_gchg], 1           ; the text origin did
    mov byte [sc_hdirty], 1
    mov byte [sc_ckok], 0
    mov byte [sc_rowsok], 0
    mov byte [sc_follow], 1
    call sc_redraw
sc_vwsame:
    ret

; sc_dirset - the Search dialog's Up/Down radio states from [sc_fdir]
sc_dirset:
    push ax
    xor ax, ax
    mov word [sc_ds_up+8], ax
    mov word [sc_ds_dn+8], ax
    cmp byte [sc_fdir], 0
    jne .up
    mov word [sc_ds_dn+8], 1
    jmp short .out
.up:
    mov word [sc_ds_up+8], 1
.out:
    pop ax
    ret

sc_a_vrib:
    xor byte [sc_vrib], 1
    jmp short sc_vtoggle
sc_a_vrul:
    xor byte [sc_vrul], 1
    jmp short sc_vtoggle
sc_a_vsta:
    xor byte [sc_vsta], 1
    ; falls through

; -----------------------------------------------------------------------------
; sc_vtoggle - a View strip toggled: re-lay the text band, repaint the strips
; in:  SI = window ptr, gfx lock held; preserves all registers
;
; sc_redrawall recomputes the geometry (sc_bounds reads the toggles) and then
; MOVES the text band with the panel-open/close blit when it can - a ribbon
; or ruler toggle changes only [sc_ty], which is exactly the move sc_panmove
; exists for; a status toggle changes [sc_bot], the blit refuses, and the
; full path repaints everything including the chrome. After the cheap path
; the strips between the bar and the text are stale, so they are repainted
; here - a bounded strip each, priced in calls, not a full window.
; -----------------------------------------------------------------------------
sc_vtoggle:
    push ax
    call sc_redrawall
    call sc_mbar                    ; cheap insurance either way: the bar's
                                    ; mnemonics survive a full repaint too
    cmp byte [sc_vrib], 0
    je .n1
    call sc_ribbon
.n1:
    cmp byte [sc_vrul], 0
    je .n2
    call sc_ruler
.n2:
    cmp byte [sc_vsta], 0
    je .n3
    call sc_status
.n3:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_a_close - File > Close and File > Exit (SPEC.md 68.2/68.4)
; in:  SI = window ptr, gfx lock held; preserves all registers
;
; A dirty document raises the save-changes prompt first; sc_doclose is the
; close itself. There is no self-close API slot, so the split is: hide the
; window here for instant feedback and raise [sc_quit]; the WORKER - the only
; context whose OSAPI_TASK_ALIVE can end the instance - destroys the record
; and dies inside its next ALIVE (see sc_worker's .quit). The worker is hired
; first if the lazy hire has not run yet; if the task table is full
; (transient), the close is refused with the reason on screen (SPEC.md 47).
;
; THE KERNEL CLOSE BOX CANNOT PROMPT: clicking it tears the instance down
; inside the kernel's own path (task, region, claims and record freed) with
; no callback in between - a documented limitation (SPEC.md 68.4). The File
; menu's Close and Exit, and their keys, all come through here.
; -----------------------------------------------------------------------------
sc_a_close:
    push ax
    mov al, SCP_CLOSE
    call sc_askdirty
    pop ax
    jc sc_mf_ret
    ; falls through
sc_doclose:
    push ax
    push bx
    call sc_hire
    cmp byte [sc_hired], 0
    je .refuse
    mov byte [sc_quit], 1
    mov bx, [sc_win]
    call OSAPI_WM_HIDE
    jmp short .out
.refuse:
    mov ax, sc_m_noclose
    call sc_saymsg
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; The About box (SPEC.md 68.2): 'Scribe 1.1a for os8088' + OK,
; centred, modal exactly like a dropdown - sc_mkey and sc_mroute consume
; everything while [sc_about] is up, and closing repaints through the same
; sc_mrepair. Reached from Help > About... and from the kernel bar's 'About
; Scribe' (OSAPI_ABOUT_SET, registered in sc_entry).
; =============================================================================

; -----------------------------------------------------------------------------
; sc_habout - the OSAPI_ABOUT_SET handler: SI = our window, lock held
; -----------------------------------------------------------------------------
sc_habout:
    call sc_bounds                  ; the kernel bar path arrives without a
    call sc_settle                  ; click of ours having run sc_bounds
    ; falls through into sc_abopen

; -----------------------------------------------------------------------------
; sc_abopen - open the About box (sc_bounds run, gfx lock held)
; preserves all registers
; -----------------------------------------------------------------------------
sc_abopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sc_mopen], SC_M_NONE  ; it replaces any open dropdown
    je .nodrop
    call sc_mclose
.nodrop:
    cmp word [sc_dlg], 0            ; ...and any open dialog (the kernel-bar
    je .clear                       ; About can arrive over one)
    call sc_dgclose
.clear:
    mov ax, [sc_cw]                 ; a window too small for the box gets the
    cmp ax, 360                     ; name line as a toast instead - refusal
    jb .toast                       ; with the reason (SPEC.md 47)
    mov ax, [sc_ch]
    cmp ax, 96
    jb .toast
    mov ax, [sc_cw]
    sub ax, 344
    shr ax, 1
    and ax, 0xFFF8                  ; the box's text starts on a byte column
    add ax, [sc_cl]
    mov [sc_abrect], ax
    add ax, 343
    mov [sc_abrect+4], ax
    mov ax, [sc_ch]
    sub ax, 72
    shr ax, 1
    add ax, [sc_ct]
    mov dx, [sc_ct]
    add dx, SC_MENU_H
    cmp ax, dx
    jae .yok
    mov ax, dx
.yok:
    mov [sc_abrect+2], ax
    add ax, 71
    mov [sc_abrect+6], ax
    ; panel, frame, shadow - the dropdown's dress
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sc_abrect]
    mov bx, [sc_abrect+2]
    mov cx, [sc_abrect+4]
    mov dx, [sc_abrect+6]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN
    call OSAPI_GFX_FRAME
    mov ax, cx
    inc ax
    mov bx, [sc_abrect+2]
    inc bx
    mov cx, ax
    mov dx, [sc_abrect+6]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [sc_abrect]
    inc ax
    mov bx, [sc_abrect+6]
    inc bx
    mov cx, [sc_abrect+4]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    ; the three lines (SPEC.md 68.2): the name, the version, and where the
    ; authentic UI came from - the Computer History Museum's Opus release
    mov cx, [sc_abrect]
    add cx, 8
    mov dx, [sc_abrect+2]
    add dx, 8
    mov ax, (CWHITE << 8) | CBLACK  ; the panel this routine filled, eleven
    mov si, sc_s_about              ; calls up (SPEC.md 6.6.5)
    call OSAPI_FONT_RUN
    add dx, 12
    mov si, sc_s_abou2
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add dx, 12
    mov si, sc_s_abou3
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    ; the OK button
    mov ax, [sc_abrect]
    add ax, 148
    mov [sc_abok], ax
    add ax, 47
    mov [sc_abok+4], ax
    mov ax, [sc_abrect+2]
    add ax, 50
    mov [sc_abok+2], ax
    add ax, 13
    mov [sc_abok+6], ax
    push si
    mov bx, sc_abok
    mov si, sc_s_ok
    mov di, OS88UI_FILL | OS88UI_DEF
    call os88ui_btn
    pop si
    mov byte [sc_about], 1
    jmp short .out
.toast:
    mov ax, sc_s_abou2              ; the version line carries the identity
    call sc_saymsg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_abclose - take the About box down and repaint what it covered
; in:  SI = window ptr, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
sc_abclose:
    push ax
    mov byte [sc_about], 0
    mov ax, [sc_abrect]
    mov [sc_mrx1], ax
    mov ax, [sc_abrect+2]
    mov [sc_mry1], ax
    mov ax, [sc_abrect+4]
    mov [sc_mrx2], ax
    mov ax, [sc_abrect+6]
    mov [sc_mry2], ax
    call sc_mrepair
    pop ax
    ret

; =============================================================================
; The modal dialog framework (SPEC.md 68.3), and Format > Character...
;
; A dialog is data - the SCD_* records up top - drawn centred over the
; content in the dropdown's dress (white panel, black frame, grey shadow).
; While one is open sc_mkey and sc_mroute hand it every key and click: Enter
; is OK, Esc is Cancel, a mnemonic letter toggles its check box, clicks hit
; the buttons and boxes through the same records the painter drew. Closing
; repaints the covered band through sc_mrepair, exactly as a dropdown does.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_dgopen - open dialog BX (a descriptor) centred over the content
; in:  SI = window ptr, BX = descriptor, gfx lock held; preserves all
; A window too small for it gets the title as a toast - refusal with the
; reason (SPEC.md 47).
; -----------------------------------------------------------------------------
sc_dgopen:
    push ax
    push cx
    push dx
    call sc_bounds
    call sc_settle                  ; a dialog is not typing
    call sc_uclose
    mov ax, [bx]
    add ax, 8
    cmp ax, [sc_cw]
    ja .toast
    mov ax, [bx+2]
    add ax, 2                       ; the shadow's pixel; a modal dialog may
    cmp ax, [sc_ch]                 ; cover the in-window menu bar (the close
    ja .toast                       ; repaint restores it) - it need only fit
                                    ; the content box (SPEC.md 68.3)
    mov ax, [sc_cw]                 ; centred, the x snapped to a byte column
    sub ax, [bx]                    ; (the content origin is snapped, so the
    shr ax, 1                       ; sum stays on one)
    and ax, 0xFFF8
    add ax, [sc_cl]
    mov [sc_dlgx], ax
    mov [sc_dlrect], ax
    add ax, [bx]
    dec ax
    mov [sc_dlrect+4], ax
    mov ax, [sc_ch]
    sub ax, [bx+2]
    cmp bx, sc_dlgconf              ; the confirm strip PINS to the bottom of
    je .bot                         ; the content (SPEC.md 68.7): the match it
    shr ax, 1                       ; asks about is selected in the text above
.bot:
    add ax, [sc_ct]
    mov dx, [sc_ct]                 ; ...its bottom (the shadow adds one) must
    add dx, [sc_ch]                 ; not spill past the content onto the
    sub dx, [bx+2]                  ; window's border (SPEC.md 68.3): a short
    dec dx                          ; CGA content centres the box too low, so
    cmp ax, dx                      ; clamp the bottom - the height check above
    jbe .ybot                       ; guarantees such a y exists
    mov ax, dx
.ybot:
    mov dx, [sc_ct]
    cmp ax, dx
    jae .yok
    mov ax, dx                      ; ...and never above the content top
.yok:
    mov [sc_dlgy], ax
    mov [sc_dlrect+2], ax
    add ax, [bx+2]
    dec ax
    mov [sc_dlrect+6], ax
    cmp bx, sc_dlgchar              ; only Format Character reads the caret's
    jne .ckset                      ; attributes; every other dialog's opener
    call sc_dispattr                ; preset [sc_dck] itself (SPEC.md 68.7)
    mov [sc_dck], al
.ckset:
    mov word [sc_dgfoc], 0          ; no edit is focused yet
    mov [sc_dlg], bx
    call sc_dgpaint
    jmp short .out
.toast:
    mov ax, [bx+4]
    call sc_saymsg
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dpen - OSAPI_GFX_PEN from a SCDF_DIS mask, and the INK with it
; in:  AL = the record's SCDF_DIS bit, isolated (0 live, non-0 disabled)
; out: the pen set, [sc_dink] banked; AL clobbered, everything else preserved
;
; `add al, 0xFF` is the flag: it carries out of a non-zero AL and not out of a
; zero one, which is CF = 1 for disabled. That much is unchanged. What is new
; is that a run takes its ink as an ARGUMENT and there is no OSAPI_GET_COLOR
; (SPEC.md 6.6.5), so the same branch writes the answer down - CDGRAY where
; the menu's [sc_mink] uses CDGRAY, for the same reason and to the same value.
; -----------------------------------------------------------------------------
sc_dpen:
    or al, al
    jz .live
    mov byte [sc_dink], CDGRAY
    stc
    jmp short .set
.live:
    mov byte [sc_dink], CBLACK
    clc
.set:
    call OSAPI_GFX_PEN
    ret

; -----------------------------------------------------------------------------
; sc_dgpaint - draw the open dialog whole: dress, title, every control
; in:  [sc_dlg]/[sc_dlgx]/[sc_dlgy]/[sc_dlrect], gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sc_dlrect]
    mov bx, [sc_dlrect+2]
    mov cx, [sc_dlrect+4]
    mov dx, [sc_dlrect+6]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN
    call OSAPI_GFX_FRAME
    mov ax, cx                      ; the drop shadow, the dropdown's
    inc ax
    mov bx, [sc_dlrect+2]
    inc bx
    mov cx, ax
    mov dx, [sc_dlrect+6]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [sc_dlrect]
    inc ax
    mov bx, [sc_dlrect+6]
    inc bx
    mov cx, [sc_dlrect+4]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    mov bx, [sc_dlg]
    mov cx, [sc_dlgx]
    add cx, 8
    mov dx, [sc_dlgy]
    add dx, 6
    mov si, [bx+4]                  ; the title
    mov ax, (CWHITE << 8) | CBLACK  ; the panel sc_dgpaint filled above
    call OSAPI_FONT_RUN
    lea di, [bx+6]
.ctl:
    cmp byte [di], 0
    je .done
    call sc_dgctl
    add di, 12
    jmp short .ctl
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgctl - draw ONE control record
; in:  DI = the record, [sc_dlgx]/[sc_dlgy], gfx lock held; preserves all
; Disabled controls draw whole under the disabled pen (SPEC.md 47), so they
; dither at 1bpp; os88ui_glyph takes its disabled flag in AH.
; -----------------------------------------------------------------------------
sc_dgctl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [di+2]
    add cx, [sc_dlgx]               ; CX = the control's x
    mov dx, [di+4]
    add dx, [sc_dlgy]               ; DX = its y
    mov al, [di+1]
    and al, SCDF_DIS
    add al, 0xFF                    ; CF = 1 exactly when disabled
    call OSAPI_GFX_PEN
    mov al, [di]
    cmp al, SCD_LBL
    je .lbl
    cmp al, SCD_CHK
    je .chk
    cmp al, SCD_RAD
    je .rad
    cmp al, SCD_GRP
    je .grp
    cmp al, SCD_BOX
    je .box
    cmp al, SCD_BTN
    je .btn
    cmp al, SCD_EDIT
    je .edit
    jmp .done
.lbl:
    mov si, [di+6]
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    jmp .done
.chk:
    mov al, OS88UI_GCHECK
    mov bl, [di+8]                  ; the attr mask this box edits
    test [sc_dck], bl
    jz .coff
    or al, OS88UI_GON
.coff:
    mov ah, [di+1]
    and ah, SCDF_DIS                ; AH non-zero = disabled (the one routine
    call os88ui_glyph               ; whose flag rides there)
    add cx, 16
    add dx, 2
    mov al, [di+1]                  ; the glyph left the pen LIVE: back to the
    and al, SCDF_DIS                ; record's own state for the label
    call sc_dpen                    ; ...which banks the run's INK with the
    mov si, [di+6]                  ; flag: a package cannot read the pen back
    mov al, [sc_dink]               ; (SPEC.md 6.6.5). The pen is still set,
    mov ah, CWHITE                  ; because the mnemonic rule below is a
    call OSAPI_FONT_RUN             ; GFX_HLINE and does read it
    test byte [di+1], SCDF_DIS
    jnz .done                       ; no mnemonic on a greyed box (65.2's rule)
    mov ax, [di+10]                 ; the mnemonic underline: index * 8
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, cx
    mov bx, ax
    add bx, 6
    add dx, 8
    call OSAPI_GFX_HLINE
    jmp .done
.rad:
    mov al, OS88UI_GRADIO
    cmp word [di+8], 0
    je .roff
    or al, OS88UI_GON
.roff:
    mov ah, [di+1]
    and ah, SCDF_DIS
    call os88ui_glyph
    add cx, 16
    add dx, 2
    mov al, [di+1]
    and al, SCDF_DIS
    call sc_dpen
    mov si, [di+6]
    mov al, [sc_dink]
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    jmp .done
.grp:
    mov ax, cx
    mov bx, dx
    add cx, [di+8]
    dec cx
    add dx, [di+10]
    dec dx
    push ax
    push bx
    call OSAPI_GFX_FRAME
    pop dx                          ; the title, inside the frame's top-left
    pop cx
    add cx, 6
    add dx, 2
    mov si, [di+6]
    mov ax, (CWHITE << 8) | CBLACK  ; INSIDE the frame: its top edge is at
    call OSAPI_FONT_RUN             ; dx-2 and the cells run dx..dx+7
    jmp .done
.box:
    mov ax, cx
    mov bx, dx
    add cx, [di+8]
    dec cx
    add dx, 12
    push ax
    push bx
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    add cx, 4
    add dx, 3
    mov si, [di+6]
    mov ax, (CWHITE << 8) | CBLACK  ; the box is 13 rows and the cells are
    call OSAPI_FONT_RUN             ; rows 3..10 of it: clear of both edges
    jmp .done
.edit:
    mov ax, cx                      ; the live edit box: interior erased (it
    mov bx, dx                      ; redraws in place as it is typed at),
    add cx, [di+8]                  ; frame, text, and the focus caret
    dec cx
    add dx, 12
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    push ax
    push bx
    push cx
    push dx
    inc ax
    inc bx
    dec cx
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    mov cx, ax
    add cx, 4
    mov dx, bx
    add dx, 3
    mov si, [di+6]
    mov ax, (CWHITE << 8) | CBLACK  ; the interior this arm erased six calls
    call OSAPI_FONT_RUN             ; ago, clear of both frame edges
    cmp di, [sc_dgfoc]
    jne .edone
    call OSAPI_FONT_WIDTH           ; SI = the text: AX = its width
    add cx, ax
    mov ax, cx
    mov bx, dx
    dec bx
    mov dx, bx
    add dx, 9
    call OSAPI_GFX_VLINE            ; the caret bar after the text
.edone:
    jmp .done
.btn:
    mov [sc_dgr], cx
    mov [sc_dgr+2], dx
    add cx, [di+8]
    dec cx
    mov [sc_dgr+4], cx
    add dx, 13
    mov [sc_dgr+6], dx
    mov si, [di+6]
    push di
    mov ax, OS88UI_FILL
    cmp word [di+10], 1             ; OK wears the default ring
    jne .ndef
    or ax, OS88UI_DEF
.ndef:
    test byte [di+1], SCDF_DIS      ; a greyed button greys WHOLE through
    jz .nbdis                       ; os88ui's own flag (SPEC.md 47)
    or ax, OS88UI_DIS
.nbdis:
    mov di, ax
    mov bx, sc_dgr
    call os88ui_btn
    pop di
.done:
    clc
    call OSAPI_GFX_PEN              ; the pen back live on every path
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dghit - which LIVE control is the point on?
; in:  CX = x, DX = y (abs), [sc_dlg] open
; out: CF=0 with AL = its type and DI = its record; CF=1 none
; -----------------------------------------------------------------------------
sc_dghit:
    push ax
    push bx
    push si
    mov di, [sc_dlg]
    add di, 6
.h:
    mov al, [di]
    or al, al
    jz .none
    test byte [di+1], SCDF_DIS
    jnz .next
    cmp al, SCD_CHK
    je .rect
    cmp al, SCD_BTN
    je .rect
    cmp al, SCD_EDIT
    je .rect
    cmp al, SCD_RAD
    jne .next
    cmp word [di+10], 0             ; a LIVE radio has a group; the char
    je .next                        ; dialog's are decorative (SPEC.md 68.3)
.rect:
    mov bx, [di+2]
    add bx, [sc_dlgx]               ; BX = x1
    cmp cx, bx
    jb .next
    push cx
    cmp al, SCD_BTN
    jne .edw
    mov cx, [di+8]                  ; a button is p2 wide, 14 tall
    mov si, 13
    jmp short .havew
.edw:
    cmp al, SCD_EDIT
    jne .chkw
    mov cx, [di+8]                  ; an edit is p2 wide, 13 tall
    mov si, 12
    jmp short .havew
.chkw:
    mov si, [di+6]                  ; a check box is glyph + pad + label wide,
    call OSAPI_FONT_WIDTH           ; 12 tall
    mov cx, ax
    add cx, 16
    mov si, 11
.havew:
    add bx, cx
    dec bx                          ; BX = x2
    pop cx
    cmp cx, bx
    ja .next
    mov bx, [di+4]
    add bx, [sc_dlgy]               ; BX = y1
    cmp dx, bx
    jb .next
    add bx, si                      ; BX = y2
    cmp dx, bx
    ja .next
    pop si
    pop bx
    pop ax
    mov al, [di]
    clc
    ret
.next:
    add di, 12
    jmp short .h
.none:
    pop si
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_dgtoggle - flip the check box DI points at and redraw it
; in:  DI = a SCD_CHK record, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgtoggle:
    push ax
    mov al, [di+8]
    xor [sc_dck], al
    call sc_dgctl                   ; the glyph white-boxes itself; the label
    pop ax                          ; redraws the same pixels transparently
    ret

; -----------------------------------------------------------------------------
; sc_dgclick - a click while the dialog is up (always consumed: it is modal)
; in:  CX = x, DX = y, SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgclick:
    push ax
    push di
    call sc_dghit
    jc .out
    cmp al, SCD_BTN
    je .btn
    cmp al, SCD_RAD
    je .rad
    cmp al, SCD_EDIT
    je .edit
    call sc_dgtoggle
    jmp short .out
.rad:
    call sc_dgradio
    jmp short .out
.edit:
    call sc_dgfocus
    jmp short .out
.btn:
    cmp word [di+10], 1
    je .ok
    cmp word [di+10], 3             ; the prompt's third verb (SPEC.md 68.4)
    je .no
    call sc_dgcancel                ; Cancel: discard
    jmp short .out
.no:
    call sc_dgno
    jmp short .out
.ok:
    call sc_dgok
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgradio - select the live radio DI points at, deselecting its group
; in:  DI = a SCD_RAD record with a group; gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgradio:
    push ax
    push bx
    push di
    mov bx, [di+10]                 ; the group
    mov ax, di
    mov di, [sc_dlg]
    add di, 6
.r:
    cmp byte [di], 0
    je .done
    cmp byte [di], SCD_RAD
    jne .n
    cmp [di+10], bx
    jne .n
    cmp di, ax
    je .on
    cmp word [di+8], 0
    je .n
    mov word [di+8], 0              ; off, and redrawn in place - the glyph
    call sc_dgctl                   ; white-boxes itself
    jmp short .n
.on:
    cmp word [di+8], 1
    je .n
    mov word [di+8], 1
    call sc_dgctl
.n:
    add di, 12
    jmp short .r
.done:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgfocus - move the edit focus to record DI; both boxes redraw
; -----------------------------------------------------------------------------
sc_dgfocus:
    push ax
    push di
    mov ax, di
    cmp ax, [sc_dgfoc]
    je .out
    mov di, [sc_dgfoc]
    mov [sc_dgfoc], ax
    or di, di
    jz .new
    call sc_dgctl                   ; the old box loses its caret
.new:
    mov di, ax
    call sc_dgctl
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgkey - a key while the dialog is up (always consumed)
; in:  AL = ascii, SI = window ptr, gfx lock held; preserves all
; Enter = OK, Esc = Cancel, a mnemonic letter toggles its check box.
; -----------------------------------------------------------------------------
sc_dgkey:
    push ax
    push bx
    push di
    cmp al, 27
    je .esc
    cmp al, 13
    je .enter
    cmp al, 9
    je .tab                         ; Tab cycles the edit fields (SPEC.md 68.3)
    cmp word [sc_dlg], sc_dlgask    ; the two Yes/No/Cancel prompts answer Y
    je .yn                          ; and N from the keyboard (SPEC.md 68.4)
    cmp word [sc_dlg], sc_dlgconf
    jne .noyn
.yn:
    mov ah, al
    cmp ah, 'a'
    jb .ynu
    sub ah, 32
.ynu:
    cmp ah, 'Y'
    je .enter                       ; Yes is the default verb
    cmp ah, 'N'
    jne .out
    call sc_dgno
    jmp .out
.noyn:
    cmp word [sc_dgfoc], 0
    je .fold
    mov di, [sc_dgfoc]              ; a FREE-TEXT edit consumes every
    test byte [di+1], SCDF_TXT      ; printable (SPEC.md 68.7) - the check
    jz .num                         ; boxes' mnemonics answer only while no
    cmp al, 8                       ; text edit is focused
    je .ebs
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    jmp .eins
.num:
    cmp al, 8                       ; a NUMERIC edit's own keys: digits,
    je .ebs                         ; the point, a minus, the inch mark and
    cmp al, '"'                     ; backspace; letters still reach the
    je .eins                        ; mnemonics below
    cmp al, '-'
    je .eins
    cmp al, '.'
    je .eins
    cmp al, '0'
    jb .fold
    cmp al, '9'
    jbe .eins
.fold:
    cmp al, 'a'
    jb .letter
    cmp al, 'z'
    ja .letter
    sub al, 32
.letter:
    or al, al
    jz .out
    mov di, [sc_dlg]
    add di, 6
.m:
    cmp byte [di], 0
    je .out
    cmp byte [di], SCD_CHK
    jne .mn
    test byte [di+1], SCDF_DIS
    jnz .mn
    mov bx, [di+6]                  ; the label's mnemonic letter, uppercased
    add bx, [di+10]
    mov ah, [bx]
    cmp ah, 'a'
    jb .mup
    cmp ah, 'z'
    ja .mup
    sub ah, 32
.mup:
    cmp ah, al
    jne .mn
    call sc_dgtoggle
    jmp .out
.mn:
    add di, 12
    jmp short .m
.esc:
    call sc_dgcancel
    jmp .out
.enter:
    call sc_dgok
    jmp .out
.eins:
    push di                         ; append to the focused buffer, up to its
    mov di, [sc_dgfoc]              ; record's p3 capacity (0 = the numeric
    mov bx, [di+6]                  ; edits' classic 6)
.ef:
    cmp byte [bx], 0
    je .efend
    inc bx
    jmp short .ef
.efend:
    push ax
    push cx
    mov cx, [di+10]
    jcxz .cap6
    jmp short .capok
.cap6:
    mov cx, 6
.capok:
    mov ax, bx
    sub ax, [di+6]
    cmp ax, cx
    pop cx
    pop ax
    jae .efull
    mov [bx], al
    mov byte [bx+1], 0
    call sc_dgctl                   ; the box redraws in place
.efull:
    pop di
    jmp short .out
.ebs:
    push di
    mov di, [sc_dgfoc]
    mov bx, [di+6]
    cmp byte [bx], 0
    je .ebd
.eb:
    cmp byte [bx+1], 0
    je .ebe
    inc bx
    jmp short .eb
.ebe:
    mov byte [bx], 0
    call sc_dgctl
.ebd:
    pop di
    jmp short .out
.tab:
    push cx                         ; the NEXT edit after the focused one,
    push di                         ; wrapping; bounded, because a dialog
    mov bx, [sc_dgfoc]              ; with no edits must fall out clean
    or bx, bx
    jnz .t0
    mov bx, [sc_dlg]
    add bx, 6
    sub bx, 12
.t0:
    mov di, bx
    mov cx, 64
.tn:
    add di, 12
    dec cx
    jz .tdone
    cmp byte [di], 0
    jne .tt
    mov di, [sc_dlg]
    add di, 6
.tt:
    cmp di, bx
    je .tdone
    cmp byte [di], SCD_EDIT
    jne .tn
    call sc_dgfocus
.tdone:
    pop di
    pop cx
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgok - OK (or Yes): apply the open dialog's answer, close, repaint
; in:  SI = window ptr, gfx lock held; preserves all
; Which dialog is up decides which apply runs (SPEC.md 68.3/68.4/68.7).
; -----------------------------------------------------------------------------
sc_dgok:
    push ax
    push bx
    mov bx, [sc_dlg]
    cmp bx, sc_dlgpara
    je .para
    cmp bx, sc_dlgchar
    je .char
    cmp bx, sc_dlgsrch
    je .srch
    cmp bx, sc_dlgrepl
    je .repl
    cmp bx, sc_dlggoto
    je .goto
    cmp bx, sc_dlgask
    je .yes
    cmp bx, sc_dlgconf
    je .cyes
    cmp bx, sc_dlgsort
    je .sort
    cmp bx, sc_dlgrenum
    je .renum
    cmp bx, sc_dlgtoc
    je .toc
    call sc_dgclose                 ; unknown: just take it down
    jmp short .out
.sort:
    call sc_dsoapply                ; the three Utilities commands (65.9)
    jmp short .out
.renum:
    call sc_drnapply
    jmp short .out
.toc:
    call sc_dtcapply
    jmp short .out
.char:
    call sc_dgapply
    call sc_dgclose
    call sc_redraw                  ; rows the new dress dirtied, outside what
    jmp short .out                  ; the close already repainted; the tail
                                    ; re-inverts the ribbon cells
.para:
    call sc_dpapply
    call sc_dgclose
    call sc_redraw
    jmp short .out
.srch:
    call sc_dsapply                 ; closes, searches, redraws (SPEC.md 68.7)
    jmp short .out
.repl:
    call sc_drapply
    jmp short .out
.goto:
    call sc_dgoto
    jmp short .out
.yes:
    call sc_dgyes                   ; the dirty prompt's Yes (SPEC.md 68.4)
    jmp short .out
.cyes:
    call sc_dcyes                   ; the confirm strip's Yes (SPEC.md 68.7)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgno - the prompts' No verb (SPEC.md 68.4/68.7)
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgno:
    push ax
    push bx
    mov bx, [sc_dlg]
    cmp bx, sc_dlgask
    je .askno
    cmp bx, sc_dlgconf
    je .cno
    call sc_dgclose
    jmp short .out
.askno:
    call sc_dgclose                 ; No: proceed without saving
    call sc_runpend
    jmp short .out
.cno:
    call sc_dcno                    ; skip this match, find the next
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgcancel - Cancel/Esc: discard; the confirm session ends with its count
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgcancel:
    push ax
    push bx
    mov bx, [sc_dlg]
    call sc_dgclose
    mov byte [sc_pend], 0           ; nothing pending any more
    cmp bx, sc_dlgconf
    jne .out
    mov ax, [sc_cfn]                ; the sweep so far is what happened: say
    call sc_saycnt                  ; how much (SPEC.md 68.7)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgapply - the check boxes' byte onto the selection or the typing attrs
; preserves all registers
; With a selection every character in the span takes the byte WHOLE (the
; boxes are authoritative - that is what OK on this dialog means), as ONE
; bulk undo group; hidden may have changed either way, so the layout tables
; drop (SPEC.md 68.3).
; -----------------------------------------------------------------------------
sc_dgapply:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov dl, [sc_dck]
    and dl, 0x7F
    cmp byte [sc_selon], 0
    je .typing
    call sc_selget                  ; AX = start, CX = length
    jc .typing
    push ax
    push cx
    call sc_urec_bulk               ; text and CHP into the arena (65.3)
    pop cx
    pop ax
    mov es, [sc_cseg]
    mov bx, ax
    mov di, cx
    push ds
    mov ds, [sc_dseg]               ; step over ¶ marks: their byte is a PAP
.f:                                 ; index (SPEC.md 68.3)
    cmp byte [bx], 13
    je .fnx
    mov [es:bx], dl
.fnx:
    inc bx
    dec di
    jnz .f
    pop ds
    call sc_urec_bulkend            ; CX is still the span's length
    mov byte [sc_dirty], 1          ; a dressed span differs from the disk
    test dl, SCAT_HID
    jz .nohid
    mov byte [sc_hashid], 1
.nohid:
    mov byte [sc_ckok], 0           ; hidden may have appeared or gone: the
    mov byte [sc_rowsok], 0         ; layout tables describe a re-wrapped
    call sc_hmark                   ; document
    jmp short .done
.typing:
    mov [sc_chp], dl
    test dl, SCAT_HID               ; hidden TYPING attrs put hidden chars in
    jz .done                        ; on the next keystroke
    mov byte [sc_hashid], 1
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgclose - take the dialog down and repaint what it covered
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dgclose:
    push ax
    mov word [sc_dlg], 0
    mov ax, [sc_dlrect]
    mov [sc_mrx1], ax
    mov ax, [sc_dlrect+2]
    mov [sc_mry1], ax
    mov ax, [sc_dlrect+4]
    mov [sc_mrx2], ax
    mov ax, [sc_dlrect+6]
    mov [sc_mry2], ax
    call sc_mrepair
    pop ax
    ret

; =============================================================================
; Search / Replace / Go To (SPEC.md 68.7) and the dirty prompt (SPEC.md 68.4)
; - the appliers behind sc_dgok's dispatcher
; =============================================================================

; sc_fplen / sc_frlen - the search text's / replacement's live length
sc_fplen:
    push ax
    push bx
    mov bx, sc_fpat
    xor ax, ax
.l:
    cmp byte [bx], 0
    je .done
    inc bx
    inc ax
    jmp short .l
.done:
    mov [sc_fpatn], ax
    call sc_pcomp               ; the raw text is what the dialog edits; the
                                ; COMPILED pattern is what the engine walks
                                ; (SPEC.md 68.7), and this is the one door
                                ; the raw text changes through
    pop bx
    pop ax
    ret
sc_frlen:
    push ax
    push bx
    mov bx, sc_frep
    xor ax, ax
.l:
    cmp byte [bx], 0
    je .done
    inc bx
    inc ax
    jmp short .l
.done:
    mov [sc_frepn], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dsapply - the Search dialog's OK: bank the options, run the search
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_dsapply:
    push ax
    mov al, [sc_dck]                ; Whole Word + Match Upper/Lowercase from
    and al, SCFO_WORD | SCFO_CASE   ; the boxes; Confirm keeps its old answer
    mov ah, [sc_fopt]               ; (it is the Replace dialog's box)
    and ah, SCFO_CONF
    or al, ah
    mov [sc_fopt], al
    xor al, al                      ; ...and Direction from the radios
    cmp word [sc_ds_up+8], 0
    je .dn
    mov al, 1
.dn:
    mov [sc_fdir], al
    call sc_fplen
    call sc_dgclose
    cmp byte [sc_fdir], 0
    jne .up
    call sc_donext                  ; selects + follows, or toasts the miss
    jmp short .draw
.up:
    call sc_doprev
.draw:
    call sc_redraw
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_drapply - the Replace dialog's OK: sweep, or start the confirm session
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
sc_drapply:
    push ax
    mov al, [sc_dck]                ; all three bits are this dialog's
    mov [sc_fopt], al
    call sc_fplen
    call sc_frlen
    call sc_dgclose
    cmp word [sc_fpatn], 0
    je .miss
    test byte [sc_fopt], SCFO_CONF
    jnz .conf
    call sc_dorepall                ; ONE bulk undo record + 'n changes'
    call sc_redraw
    jmp short .out
.conf:
    call sc_uclose
    mov word [sc_cfn], 0            ; the confirm sweep starts at the top,
    xor ax, ax                      ; like Replace All (SPEC.md 68.7)
    call sc_cfstep
    jmp short .out
.miss:
    call sc_fmiss
    call sc_redraw
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_cfstep - the confirm session's step: select the next match at/after AX
;             under the Yes/No/Cancel strip, or finish with the count
; in:  AX = where to look from, SI = window ptr, gfx lock held; preserves all
; The strip closes before each replacement and reopens after (SPEC.md 68.7):
; a replacement reflows rows, and lettering them under a modal's pixels would
; corrupt it - the close repair and reopen are the price of a per-match answer.
; -----------------------------------------------------------------------------
sc_cfstep:
    push ax
    push bx
    push dx
    call sc_findfwd
    jc .done
    call sc_showmatch               ; selects it, follow = 1...
    call sc_redraw                  ; ...and this scrolls it visible BEFORE
    mov bx, sc_dlgconf              ; the strip opens over the bottom band
    call sc_dgopen
    jmp short .out
.done:
    mov ax, [sc_cfn]
    call sc_saycnt                  ; 'n changes'
    call sc_redraw
.out:
    pop dx
    pop bx
    pop ax
    ret

; sc_dcyes - confirm strip: replace the selected match, step on
sc_dcyes:
    push ax
    push cx
    push dx
    call sc_dgclose                 ; the strip's pixels go before the rows
                                    ; under them reflow
    mov ax, [sc_fmst]
    mov cx, [sc_fmen]
    sub cx, ax
    call sc_replat                  ; CF = 1 = no room: the session stops with
    jc .stop                        ; everything up to here replaced
    inc word [sc_cfn]
    mov byte [sc_dirty], 1
    mov ax, dx                      ; one past the new text
    push ax
    call sc_selclr
    pop ax
    mov [sc_cur], ax
    call sc_editinv
    call sc_cfstep
    jmp short .out
.stop:
    mov ax, [sc_cfn]
    call sc_saycnt
    call sc_redraw
.out:
    pop dx
    pop cx
    pop ax
    ret

; sc_dcno - confirm strip: keep this one, step past it
sc_dcno:
    push ax
    call sc_dgclose
    mov ax, [sc_fmen]
    call sc_cfstep
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_dgoto - the Go To dialog's OK: page N -> the caret at line (N-1)*54
; in:  SI = window ptr, gfx lock held; preserves all (SPEC.md 68.7)
; -----------------------------------------------------------------------------
sc_dgoto:
    push ax
    push bx
    push cx
    push dx
    call sc_dgclose
    mov bx, sc_de_goto              ; parse the unsigned page number
    xor ax, ax
    xor cx, cx                      ; CL = a digit was seen
.p:
    mov dl, [bx]
    cmp dl, '0'
    jb .pd
    cmp dl, '9'
    ja .pd
    inc bx
    mov cl, 1
    cmp ax, 999                     ; clamped: page 999 is far past SC_MAXKB
    jae .p
    push bx
    mov bx, ax
    shl ax, 1
    shl ax, 1
    add ax, bx
    shl ax, 1                       ; AX = 10 * old
    pop bx
    sub dl, '0'
    xor dh, dh
    add ax, dx
    jmp short .p
.pd:
    or cl, cl
    jz .one
    or ax, ax
    jnz .have
.one:
    mov ax, 1                       ; an empty or zero answer is page 1
.have:
    dec ax
    mov cx, SC_PGLINES
    mul cx                          ; AX = the page's first absolute line
                                    ; (<= 999*54: DX = 0)
    mov cx, [sc_drows]              ; clamp to the known height - a lower
    jcxz .rok                       ; bound, so a deep page may land short
    cmp ax, cx                      ; and honestly so (SPEC.md 68.7)
    jb .rok
    mov ax, cx
    dec ax
.rok:
    call sc_settle
    call sc_uclose
    call sc_selclr
    mov byte [sc_ext], 0
    push ax
    call sc_scrollto                ; the target row to the top of the view
    pop ax
    mov byte [sc_rowsok], 0         ; the tables describe the OLD view
    sub ax, [sc_top]                ; the target as a VISIBLE row (>= 0)
    mov [sc_wanty], ax
    mov word [sc_wantx], 0
    mov word [sc_hity], 0xFFFF
    mov dx, ax                      ; ...and the walk's bound
    mov ax, [sc_top]
    call sc_xseed                   ; seed at the view top (SPEC.md 27.13)
    jnc .seeded
    mov ax, [sc_wanty]
    mov [sc_lastrow], ax            ; unseeded: still bounded to the target
.seeded:
    call sc_measure
    mov byte [sc_resume], 0
    mov ax, [sc_wanti]
    cmp ax, 0xFFFF
    jne .cur
    mov ax, [sc_len]                ; past the tail: the end of the document
.cur:
    mov [sc_cur], ax
    mov byte [sc_ckok], 0
    call sc_chpsync
    mov byte [sc_follow], 1
    call sc_redraw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_askdirty - the save-changes gate every destructive door asks (SPEC.md 68.4)
; in:  AL = the pending action (SCP_*), SI = window ptr, gfx lock held
; out: CF = 0 proceed now (not dirty); CF = 1 the prompt is up and the action
;      is deferred to its answer (or the prompt could not fit: deferred to
;      nothing, which is Cancel). Preserves all registers.
; -----------------------------------------------------------------------------
sc_askdirty:
    cmp byte [sc_dirty], 0
    je .go
    push ax
    push bx
    mov [sc_pend], al
    call sc_askmsg                  ; 'Do you want to save changes to <X>?'
    mov bx, sc_dlgask
    call sc_dgopen
    cmp [sc_dlg], bx
    je .up
    mov byte [sc_pend], 0           ; refused (window too small): Cancel
.up:
    pop bx
    pop ax
    stc
    ret
.go:
    clc
    ret

; sc_askmsg - compose the prompt's line around the live name
sc_askmsg:
    push ax
    push si
    push di
    mov si, sc_s_askpre
    mov di, sc_pmsg
    call sc_stcat
    mov si, sc_name
    call sc_stcat
    mov si, sc_s_askpost
    call sc_stcat
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; sc_dgyes - the prompt's Yes: save, and proceed only if it stuck
sc_dgyes:
    call sc_dgclose
    call sc_save                    ; toasts its own outcome
    cmp byte [sc_dirty], 0          ; still dirty = the save failed: the
    jne .stay                       ; action aborts with the reason on screen
    jmp sc_runpend
.stay:
    mov byte [sc_pend], 0
    ret

; sc_runpend - the deferred action, after Yes-saved or No
sc_runpend:
    push ax
    mov al, [sc_pend]
    mov byte [sc_pend], 0
    cmp al, SCP_NEW
    je .new
    cmp al, SCP_OPEN
    je .open
    cmp al, SCP_CLOSE
    je .close
    jmp short .out
.new:
    call sc_donew
    jmp short .out
.open:
    call sc_doopen
    jmp short .out
.close:
    call sc_doclose
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_a_char - Format > Character... (SPEC.md 68.3)
; in:  SI = window ptr, gfx lock held
; -----------------------------------------------------------------------------
sc_a_char:
    push bx
    mov bx, sc_dlgchar
    call sc_dgopen
    pop bx
    ret

; --- the Format Character dialog (char.des, dltCharacter): the seven check
; boxes live, Font/Points/Color and the Position/Character Spacing groups
; present and greyed - facts of the platform (SPEC.md 47/68.3) ----------------
%macro SCDC 7                       ; type, flags, x, y, p1, p2, p3
    db %1, %2
    dw %3, %4, %5, %6, %7
%endmacro

sc_dlgchar:
    dw 440, 128, sc_s_dgchar
    SCDC SCD_CHK, 0,          8,  26, sc_s_dbold,  SCAT_BOLD, 0
    SCDC SCD_CHK, 0,          8,  40, sc_s_dital,  SCAT_ITAL, 0
    SCDC SCD_CHK, 0,          8,  54, sc_s_dkaps,  SCAT_SCAP, 6
    SCDC SCD_CHK, 0,          8,  68, sc_s_dhid,   SCAT_HID,  0
    SCDC SCD_CHK, 0,          8,  82, sc_s_dul,    SCAT_UL,   0
    SCDC SCD_CHK, 0,          8,  96, sc_s_dwul,   SCAT_WUL,  0
    SCDC SCD_CHK, 0,          8, 110, sc_s_ddul,   SCAT_DUL,  0
    SCDC SCD_LBL, SCDF_DIS, 152,  28, sc_s_font,   0, 0
    SCDC SCD_BOX, SCDF_DIS, 210,  26, sc_s_pica,   96, 0
    SCDC SCD_LBL, SCDF_DIS, 152,  46, sc_s_dpts,   0, 0
    SCDC SCD_BOX, SCDF_DIS, 210,  44, sc_s_10,     56, 0
    SCDC SCD_LBL, SCDF_DIS, 152,  64, sc_s_dcol,   0, 0
    SCDC SCD_BOX, SCDF_DIS, 210,  62, sc_s_dauto,  80, 0
    SCDC SCD_GRP, SCDF_DIS, 152,  76, sc_s_dpos,   132, 50
    SCDC SCD_RAD, SCDF_DIS, 158,  88, sc_s_normal, 1, 0
    SCDC SCD_RAD, SCDF_DIS, 158, 100, sc_s_dsup,   0, 0
    SCDC SCD_RAD, SCDF_DIS, 158, 112, sc_s_dsub,   0, 0
    SCDC SCD_GRP, SCDF_DIS, 292,  76, sc_s_dcsp,   140, 50
    SCDC SCD_RAD, SCDF_DIS, 298,  88, sc_s_normal, 1, 0
    SCDC SCD_RAD, SCDF_DIS, 298, 100, sc_s_dexp,   0, 0
    SCDC SCD_RAD, SCDF_DIS, 298, 112, sc_s_dcond,  0, 0
    SCDC SCD_BTN, 0,        328,   4, sc_s_ok,     48, 1
    SCDC SCD_BTN, 0,        384,   4, sc_s_dcanc,  48, 2
    db 0

; -----------------------------------------------------------------------------
; sc_a_para - Format > Paragraph... (SPEC.md 68.3), per para.des: Alignment
; radios in a row, Indents as inch edits (cells = tenths), Spacing (Line as
; the three draft spacings; Before as 0/1 li), OK/Cancel and a greyed Tabs...
; The authentic Keep Paragraph / Border / Pattern / Style groups are OMITTED,
; deliberately: with them the dialog cannot fit a CGA content box, and a
; Format command that refuses on one adapter of three is worse than a
; shortened dialog (SPEC.md 39/47).
; -----------------------------------------------------------------------------
sc_a_para:
    push ax
    push bx
    call sc_dpinit                  ; radios and edits from the caret's
    mov bx, sc_dlgpara              ; paragraph
    call sc_dgopen
    pop bx
    pop ax
    ret

; sc_dpinit - the dialog's controls from the caret paragraph's format
sc_dpinit:
    push ax
    push bx
    push cx
    push dx
    push di
    call sc_rlcalc                  ; AX = state bits; indents in sc_rll/f/r
    mov bx, sc_dp_al0
    call .bit                       ; bits 0..3: the alignment radios
    mov bx, sc_dp_al1
    call .bit
    mov bx, sc_dp_al2
    call .bit
    mov bx, sc_dp_al3
    call .bit
    mov bx, sc_dp_sp0
    call .bit                       ; bits 4..6: the spacing radios
    mov bx, sc_dp_sp1
    call .bit
    mov bx, sc_dp_sp2
    call .bit
    mov bx, sc_dp_sb0
    call .bit                       ; bit 7 closed, bit 8 open
    mov bx, sc_dp_sb1
    call .bit
    mov al, [sc_rll]
    mov di, sc_de_left
    call sc_dfmt
    mov al, [sc_rlr]
    mov di, sc_de_right
    call sc_dfmt
    mov al, [sc_rlf]
    mov di, sc_de_first
    call sc_dfmt
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.bit:
    xor dx, dx                      ; the next state bit -> the record's p2
    shr ax, 1
    adc dx, 0
    mov [bx+8], dx
    ret

; sc_dfmt - signed cells AL -> inches text at DI ('0.5"', '-1.0"')
; preserves all registers
sc_dfmt:
    push ax
    push bx
    push di
    or al, al
    jns .pos
    mov byte [di], '-'
    inc di
    neg al
.pos:
    xor ah, ah
    mov bl, 10
    div bl                          ; AL = whole inches, AH = the tenth
    mov bh, ah
    xor ah, ah
    call sc_utoa
    mov byte [di], '.'
    inc di
    mov al, bh
    add al, '0'
    mov [di], al
    inc di
    mov byte [di], '"'
    inc di
    mov byte [di], 0
    pop di
    pop bx
    pop ax
    ret

; sc_dparse - the inches text at BX -> signed cells in AL ('0.5' = 5;
; '-1' = -10; a bare '"' or junk reads as what preceded it). Clamps ride
; sc_cl0max/sc_clfirst at the caller. Preserves everything else.
sc_dparse:
    push bx
    push cx
    push dx
    xor cx, cx                      ; CL = the cells, CH = the sign
.sk:
    mov al, [bx]
    cmp al, ' '
    jne .s0
    inc bx
    jmp short .sk
.s0:
    cmp al, '-'
    jne .whole
    mov ch, 1
    inc bx
.whole:
    xor dx, dx                      ; DL = whole inches
.d:
    mov al, [bx]
    cmp al, '0'
    jb .dot
    cmp al, '9'
    ja .dot
    inc bx
    cmp dl, 10
    jae .d                          ; clamped: 10" is already SC_INDMAX
    mov ah, dl
    shl dl, 1
    shl dl, 1
    shl dl, 1
    add dl, ah
    add dl, ah                      ; *10
    mov al, [bx-1]
    sub al, '0'
    add dl, al
    jmp short .d
.dot:
    cmp dl, 10
    jbe .wok
    mov dl, 10
.wok:
    mov al, dl
    mov ah, 10
    mul ah                          ; AX = whole * 10 <= 100
    mov cl, al
    cmp byte [bx], '.'
    jne .fin
    inc bx
    mov al, [bx]
    cmp al, '0'
    jb .fin
    cmp al, '9'
    ja .fin
    sub al, '0'
    add cl, al                      ; ...+ the tenth
.fin:
    mov al, cl
    or ch, ch
    jz .out
    neg al
.out:
    pop dx
    pop cx
    pop bx
    ret

; sc_dpapply - OK: the radios and parsed edits -> ONE candidate, applied to
; the selection's paragraphs through sc_modpap (SPEC.md 68.3)
sc_dpapply:
    push ax
    push bx
    push dx
    xor dl, dl
    cmp word [sc_dp_al1+8], 0
    je .a2
    or dl, 1
.a2:
    cmp word [sc_dp_al2+8], 0
    je .a3
    or dl, 2
.a3:
    cmp word [sc_dp_al3+8], 0
    je .sp
    or dl, 3
.sp:
    cmp word [sc_dp_sp1+8], 0
    je .sp2c
    or dl, 4                        ; 1.5: spacing value 1 << 2
.sp2c:
    cmp word [sc_dp_sp2+8], 0
    je .sb
    or dl, 8                        ; double: value 2 << 2
.sb:
    cmp word [sc_dp_sb1+8], 0
    je .have
    or dl, SCPA_SB
.have:
    mov [sc_pfb], dl
    mov bx, sc_de_left
    call sc_dparse
    call sc_cl0max
    mov [sc_pfb+1], al
    mov bx, sc_de_right
    call sc_dparse
    call sc_cl0max
    mov [sc_pfb+3], al
    mov bx, sc_de_first
    call sc_dparse
    call sc_clfirst                 ; relative to the left just stored
    mov [sc_pfb+2], al
    mov al, SCPO_SETALL
    xor ah, ah
    call sc_modpap
    pop dx
    pop bx
    pop ax
    ret

; --- the Format Paragraph dialog (para.des, dltParaLooks), compacted -----------
sc_dlgpara:
    dw 448, 134, sc_s_dgpara
    SCDC SCD_GRP, 0,          8,  18, sc_s_dalign, 296, 30
sc_dp_al0:
    SCDC SCD_RAD, 0,         16,  30, sc_s_dleft,  0, 1
sc_dp_al1:
    SCDC SCD_RAD, 0,         76,  30, sc_s_dcent,  0, 1
sc_dp_al2:
    SCDC SCD_RAD, 0,        148,  30, sc_s_dright, 0, 1
sc_dp_al3:
    SCDC SCD_RAD, 0,        212,  30, sc_s_djust,  0, 1
    SCDC SCD_GRP, 0,          8,  52, sc_s_dind,   184, 76
    SCDC SCD_LBL, 0,         16,  65, sc_s_dfleft, 0, 0
sc_dp_el:
    SCDC SCD_EDIT, 0,       112,  62, sc_de_left,  64, 0
    SCDC SCD_LBL, 0,         16,  83, sc_s_dfrgt,  0, 0
sc_dp_er:
    SCDC SCD_EDIT, 0,       112,  80, sc_de_right, 64, 0
    SCDC SCD_LBL, 0,         16, 101, sc_s_dflin,  0, 0
sc_dp_ef:
    SCDC SCD_EDIT, 0,       112,  98, sc_de_first, 64, 0
    SCDC SCD_GRP, 0,        196,  52, sc_s_dspac,  148, 76
    SCDC SCD_LBL, 0,        204,  65, sc_s_dline,  0, 0
sc_dp_sp0:
    SCDC SCD_RAD, 0,        248,  62, sc_s_d1,     0, 2
sc_dp_sp1:
    SCDC SCD_RAD, 0,        280,  62, sc_s_15,     0, 2
sc_dp_sp2:
    SCDC SCD_RAD, 0,        320,  62, sc_s_d2,     0, 2
    SCDC SCD_LBL, 0,        204,  90, sc_s_dbef,   0, 0
sc_dp_sb0:
    SCDC SCD_RAD, 0,        204, 102, sc_s_d0li,   0, 3
sc_dp_sb1:
    SCDC SCD_RAD, 0,        268, 102, sc_s_d1li,   0, 3
    SCDC SCD_BTN, 0,        376,   6, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        376,  26, sc_s_dcanc,  56, 2
    SCDC SCD_BTN, SCDF_DIS, 376,  46, sc_s_dtabs,  56, 0
    db 0

sc_s_dgpara: db 'Paragraph', 0
sc_s_dalign: db 'Alignment', 0
sc_s_dleft:  db 'Left', 0
sc_s_dcent:  db 'Center', 0
sc_s_dright: db 'Right', 0
sc_s_djust:  db 'Justified', 0
sc_s_dind:   db 'Indents', 0
sc_s_dfleft: db 'From Left:', 0
sc_s_dfrgt:  db 'From Right:', 0
sc_s_dflin:  db 'First Line:', 0
sc_s_dspac:  db 'Spacing', 0
sc_s_dline:  db 'Line:', 0
sc_s_d1:     db '1', 0
sc_s_d2:     db '2', 0
sc_s_dbef:   db 'Before:', 0
sc_s_d0li:   db '0 li', 0
sc_s_d1li:   db '1 li', 0
sc_s_dtabs:  db 'Tabs...', 0

sc_s_dgchar: db 'Character', 0
sc_s_dbold:  db 'Bold', 0
sc_s_dital:  db 'Italic', 0
sc_s_dkaps:  db 'Small Kaps', 0     ; sic - char.des's literal spelling
sc_s_dhid:   db 'Hidden', 0
sc_s_dul:    db 'Underline', 0
sc_s_dwul:   db 'Word underline', 0
sc_s_ddul:   db 'Double underline', 0
sc_s_dpts:   db 'Points:', 0
sc_s_dcol:   db 'Color:', 0
sc_s_dauto:  db 'Auto', 0
sc_s_dpos:   db 'Position', 0
sc_s_dsup:   db 'Superscript', 0
sc_s_dsub:   db 'Subscript', 0
sc_s_dcsp:   db 'Character Spacing', 0
sc_s_dexp:   db 'Expanded', 0
sc_s_dcond:  db 'Condensed', 0
sc_s_dcanc:  db 'Cancel', 0
sc_s_dsort:  db 'Sort', 0            ; sort.des
sc_s_dorder: db 'Sort Order', 0
sc_s_dasc:   db 'Ascending', 0
sc_s_ddes:   db 'Descending', 0
sc_s_dkey:   db 'Key Field', 0
sc_s_dktype: db 'Key Type:', 0
sc_s_dalpha: db 'Alphanumeric', 0
sc_s_dnum:   db 'Numeric', 0
sc_s_ddate:  db 'Date', 0            ; greyed: no date parser (SPEC.md 68.9)
sc_s_dsep:   db 'Separator:', 0
sc_s_dcomma: db 'Comma', 0
sc_s_dtab:   db 'Tab', 0
sc_s_dfldn:  db 'Field number:', 0
sc_s_dscol:  db 'Sort Column Only', 0    ; greyed: draft view has no column
sc_s_dcase:  db 'Case Sensitive', 0      ; selection to restrict to
sc_s_drenum: db 'Renumber', 0        ; renum.des
sc_s_drpara: db 'Renumber Paragraphs', 0
sc_s_dall:   db 'All', 0
sc_s_dnumd:  db 'Numbered Only', 0
sc_s_drem:   db 'Remove', 0
sc_s_dauto2: db 'Automatic', 0
sc_s_dman:   db 'Manual:', 0
sc_s_dstart: db 'Start at:', 0
sc_s_dlevels: db 'Show all levels', 0    ; greyed: levels come from the style
sc_s_dfmt:   db 'Format:', 0             ; sheet, and there is none
sc_s_dtoc:   db 'Table of Contents', 0   ; toc.des
sc_s_dtocg:  db 'Table Of Contents', 0
sc_s_dhead:  db 'Use Heading Paragraphs', 0
sc_s_dtcfld: db 'Use Table Entry Fields', 0
sc_s_dfrom:  db 'From:', 0
sc_s_dto:    db 'To:', 0

; --- the Search / Replace / Go To dialogs (search.des / replace.des / the
;     goto dialog, SPEC.md 68.7) and the two Yes/No/Cancel prompts (65.4) ----
sc_dlgsrch:
    dw 448, 96, sc_s_dsrch
    SCDC SCD_LBL, 0,          8,  26, sc_s_dsfor,  0, 0
sc_ds_edit:
    SCDC SCD_EDIT, SCDF_TXT, 100,  24, sc_fpat,    268, SC_EDCAP
    SCDC SCD_CHK, 0,          8,  48, sc_s_dwword, SCFO_WORD, 0
    SCDC SCD_CHK, 0,          8,  64, sc_s_dmcase, SCFO_CASE, 0
    SCDC SCD_GRP, 0,        200,  44, sc_s_ddir,   168, 40
sc_ds_up:
    SCDC SCD_RAD, 0,        210,  58, sc_s_dup,    0, 1
sc_ds_dn:
    SCDC SCD_RAD, 0,        280,  58, sc_s_ddown,  0, 1
    SCDC SCD_BTN, 0,        384,   4, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        384,  24, sc_s_dcanc,  56, 2
    db 0

sc_dlgrepl:
    dw 448, 116, sc_s_drepl
    SCDC SCD_LBL, 0,          8,  26, sc_s_dsfor,  0, 0
sc_dr_edit:
    SCDC SCD_EDIT, SCDF_TXT, 116,  24, sc_fpat,    252, SC_EDCAP
    SCDC SCD_LBL, 0,          8,  44, sc_s_drwith, 0, 0
sc_dr_redit:
    SCDC SCD_EDIT, SCDF_TXT, 116,  42, sc_frep,    252, SC_EDCAP
    SCDC SCD_CHK, 0,          8,  64, sc_s_dwword, SCFO_WORD, 0
    SCDC SCD_CHK, 0,          8,  80, sc_s_dmcase, SCFO_CASE, 0
    SCDC SCD_CHK, 0,          8,  96, sc_s_dconf,  SCFO_CONF, 0
    SCDC SCD_BTN, 0,        384,   4, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        384,  24, sc_s_dcanc,  56, 2
    db 0

sc_dlggoto:
    dw 320, 64, sc_s_dgo
    SCDC SCD_LBL, 0,          8,  30, sc_s_dpgnum, 0, 0
sc_dg_edit:
    SCDC SCD_EDIT, 0,       112,  28, sc_de_goto,  56, 6
    SCDC SCD_BTN, 0,        184,  28, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        248,  28, sc_s_dcanc,  56, 2
    db 0

; --- the Utilities dialogs (sort.des / renum.des / toc.des, SPEC.md 68.9) ---
; The .des files' own strings and grouping. Their drop-down lists are RADIOS
; here - this framework has no live list box (SPEC.md 68.3), and a radio row
; says the same thing about a choice of three; the greyed controls are the
; ones whose fact this port does not have (a column selection, a style sheet).
sc_dlgsort:
    dw 448, 168, sc_s_dsort
    SCDC SCD_GRP, 0,          8,  24, sc_s_dorder, 360, 34
sc_so_asc:
    SCDC SCD_RAD, 0,         18,  40, sc_s_dasc,   1, 1
sc_so_des:
    SCDC SCD_RAD, 0,        140,  40, sc_s_ddes,   0, 1
    SCDC SCD_GRP, 0,          8,  64, sc_s_dkey,   360, 76
    SCDC SCD_LBL, 0,         16,  84, sc_s_dktype, 0, 0
sc_so_alp:
    SCDC SCD_RAD, 0,         92,  82, sc_s_dalpha, 1, 2
sc_so_num:
    SCDC SCD_RAD, 0,         92,  96, sc_s_dnum,   0, 2
    SCDC SCD_RAD, SCDF_DIS,  92, 110, sc_s_ddate,  0, 2
    SCDC SCD_LBL, 0,        216,  84, sc_s_dsep,   0, 0
sc_so_com:
    SCDC SCD_RAD, 0,        288,  82, sc_s_dcomma, 1, 3
sc_so_tab:
    SCDC SCD_RAD, 0,        288,  96, sc_s_dtab,   0, 3
    SCDC SCD_LBL, 0,        216, 112, sc_s_dfldn,  0, 0
sc_so_fld:
    SCDC SCD_EDIT, 0,       320, 110, sc_de_fld,   40, 4
    SCDC SCD_CHK, SCDF_DIS,   8, 146, sc_s_dscol,  1, 0
    SCDC SCD_CHK, 0,        216, 146, sc_s_dcase,  2, 0
    SCDC SCD_BTN, 0,        380,   6, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        380,  26, sc_s_dcanc,  56, 2
    db 0

sc_dlgrenum:
    dw 448, 148, sc_s_drenum
    SCDC SCD_GRP, 0,          8,  24, sc_s_drpara, 360, 34
sc_rn_all:
    SCDC SCD_RAD, 0,         18,  40, sc_s_dall,   1, 1
sc_rn_numd:
    SCDC SCD_RAD, 0,         80,  40, sc_s_dnumd,  0, 1
sc_rn_rem:
    SCDC SCD_RAD, 0,        216,  40, sc_s_drem,   0, 1
sc_rn_auto:
    SCDC SCD_RAD, 0,         18,  70, sc_s_dauto2, 1, 2
sc_rn_man:
    SCDC SCD_RAD, 0,        140,  70, sc_s_dman,   0, 2
    SCDC SCD_LBL, 0,         18,  96, sc_s_dstart, 0, 0
sc_rn_st:
    SCDC SCD_EDIT, 0,        96,  94, sc_de_start, 48, 5
    SCDC SCD_CHK, SCDF_DIS,  18, 120, sc_s_dlevels, 1, 0
    SCDC SCD_LBL, 0,        216,  96, sc_s_dfmt,   0, 0
sc_rn_fmt:
    SCDC SCD_EDIT, SCDF_TXT, 280,  94, sc_rnfmt,    80, 10
    SCDC SCD_BTN, 0,        380,   6, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        380,  26, sc_s_dcanc,  56, 2
    db 0

sc_dlgtoc:
    dw 448, 124, sc_s_dtoc
    SCDC SCD_GRP, 0,          8,  24, sc_s_dtocg,  360, 84
sc_tc_head:                         ; the two SOURCE radios stay adjacent in
    SCDC SCD_RAD, 0,         18,  40, sc_s_dhead,  1, 1
sc_tc_fld:                          ; the descriptor - sc_radset addresses a
    SCDC SCD_RAD, 0,         18,  88, sc_s_dtcfld, 0, 1
sc_tc_all:                          ; PAIR by its first record, and where a
    SCDC SCD_RAD, 0,         34,  62, sc_s_dall,   1, 2
sc_tc_rng:                          ; control PAINTS is its x/y, not its
    SCDC SCD_RAD, 0,         96,  62, sc_s_dfrom,  0, 2
sc_tc_f:                            ; place in the list
    SCDC SCD_EDIT, 0,       168,  60, sc_de_from,  32, 2
    SCDC SCD_LBL, 0,        216,  62, sc_s_dto,    0, 0
sc_tc_t:
    SCDC SCD_EDIT, 0,       248,  60, sc_de_to,    32, 2
    SCDC SCD_BTN, 0,        380,   6, sc_s_ok,     56, 1
    SCDC SCD_BTN, 0,        380,  26, sc_s_dcanc,  56, 2
    db 0

sc_dlgask:                          ; 'Do you want to save changes to X?' -
    dw 384, 68, sc_ttl              ; titled with the product name, as the
    SCDC SCD_LBL, 0,          8,  24, sc_pmsg,     0, 0
    SCDC SCD_BTN, 0,         80,  44, sc_s_dyes,   64, 1
    SCDC SCD_BTN, 0,        160,  44, sc_s_dno,    64, 3
    SCDC SCD_BTN, 0,        240,  44, sc_s_dcanc,  64, 2
    db 0                            ; real message box was (SPEC.md 68.4)

sc_dlgconf:                         ; the confirm strip, pinned to the bottom
    dw 384, 44, sc_s_drepl          ; of the content (SPEC.md 68.7)
    SCDC SCD_LBL, 0,          8,  22, sc_s_dconfq, 0, 0
    SCDC SCD_BTN, 0,        208,  18, sc_s_dyes,   48, 1
    SCDC SCD_BTN, 0,        264,  18, sc_s_dno,    48, 3
    SCDC SCD_BTN, 0,        320,  18, sc_s_dcanc,  56, 2
    db 0

sc_s_dsrch:  db 'Search', 0
sc_s_drepl:  db 'Replace', 0
sc_s_dgo:    db 'Go To', 0
sc_s_dsfor:  db 'Search For:', 0
sc_s_drwith: db 'Replace With:', 0
sc_s_dwword: db 'Whole Word', 0
sc_s_dmcase: db 'Match Upper/Lowercase', 0
sc_s_dconf:  db 'Confirm Changes', 0
sc_s_ddir:   db 'Direction', 0
sc_s_dup:    db 'Up', 0
sc_s_ddown:  db 'Down', 0
sc_s_dpgnum: db 'Page Number:', 0
sc_s_dyes:   db 'Yes', 0
sc_s_dno:    db 'No', 0
sc_s_dconfq: db 'Replace this occurrence?', 0
sc_s_askpre: db 'Do you want to save changes to ', 0
sc_s_askpost: db '?', 0
sc_m_nfound: db 'Search text not found', 0
sc_m_baddoc: db 'Bad .DOC file', 0

; =============================================================================
; The menu data (SPEC.md 68.2) - verbatim from Opus resource/menus.cmd
; (MW_MENU, the full-menus bar). '&' marked the mnemonic in the source; here
; the display string is plain, the mnemonic's cell index and its letter ride
; the item record, and the shortcut captions are keys.cmd's bindings for
; exactly the commands these items invoke - no caption is invented.
;
; Descriptor row (8 bytes): bar start cell, bar cell count, title mnemonic
; index, item count; dw items, panel width px. Item record (8 bytes): flags,
; mnemonic index, action, mnemonic letter; dw label, caption (0 = none).
; =============================================================================
%macro SCMI 6                       ; flags, mn index, action, mn letter,
    db %1, %2, %3, %4               ; label, caption
    dw %5, %6
%endmacro
%macro SCMS 0                       ; a separator
    db SCMF_SEP, 0, SCA_NONE, 0
    dw 0, 0
%endmacro

sc_mtab:
    db 0,  4, 0, 9
    dw sc_it_file, 208
    db 5,  4, 0, 15
    dw sc_it_edit, 144
    db 10, 4, 0, 13
    dw sc_it_view, 128
    db 15, 6, 0, 13
    dw sc_it_ins, 176
    db 22, 6, 5, 12
    dw sc_it_fmt, 144
    db 29, 9, 0, 11
    dw sc_it_util, 168
    db 39, 5, 0, 5
    dw sc_it_mac, 152
    db 45, 6, 0, 4
    dw sc_it_win, 128
    db 52, 4, 0, 9
    dw sc_it_help, 120
    db 0xFF, 0, 0, 1                ; the ribbon's Font combo (SC_M_FONTC)
    dw sc_it_fontc, SC_RB_FBW
    db 0xFF, 0, 0, 1                ; ...its Pts combo
    dw sc_it_ptsc, SC_RB_PBW
    db 0xFF, 0, 0, 1                ; ...and the ruler's Style combo
    dw sc_it_stylec, SC_RL_SBW

; the bar itself: one string, one opaque run; cells 0..55
sc_s_mbar: db 'File Edit View Insert Format Utilities Macro Window Help', 0

; Alt+letter scan codes, File..Help order (int 16h: Alt+letter = ascii 0 +
; the letter key's scan code)
sc_alttab: db 0x21, 0x12, 0x2F, 0x17, 0x14, 0x16, 0x32, 0x11, 0x23

sc_it_file:                         ; &File
    SCMI 0,        0, SCA_NEW,    'N', sc_L_new,    0
    SCMI 0,        0, SCA_OPEN,   'O', sc_L_open,   sc_C_cf12
    SCMI 0,        0, SCA_CLOSE,  'C', sc_L_close,  0
    SCMI 0,        0, SCA_SAVE,   'S', sc_L_save,   sc_C_sf12
    SCMI 0,        5, SCA_SAVEAS, 'A', sc_L_saveas, sc_C_f12
    SCMI SCMF_DIS, 3, SCA_NONE,   'E', sc_L_saveall, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'F', sc_L_find,   0
    SCMS                            ; no Print/Preview/Merge/Setup: OS8088 has
                                    ; no print backend, so they are absent
                                    ; rather than greyed (decided 2026-09-04)
    SCMI 0,        1, SCA_EXIT,   'X', sc_L_exit,   sc_C_af4

sc_it_edit:                         ; &Edit
    SCMI 0,        0, SCA_UNDO,   'U', sc_L_undo,   sc_C_abksp
    SCMI SCMF_DIS, 0, SCA_NONE,   'R', sc_L_repeat, sc_C_f4
    SCMI 0,        2, SCA_CUT,    'T', sc_L_cut,    sc_C_sdel
    SCMI 0,        0, SCA_COPY,   'C', sc_L_copy,   sc_C_cins
    SCMI 0,        0, SCA_PASTE,  'P', sc_L_paste,  sc_C_sins
    SCMI SCMF_DIS, 6, SCA_NONE,   'L', sc_L_plink,  0
    SCMS
    SCMI 0,        0, SCA_SEARCH, 'S', sc_L_search, 0
    SCMI 0,        1, SCA_REPL,   'E', sc_L_replace, 0
    SCMI 0,        0, SCA_GOTO,   'G', sc_L_goto,   sc_C_f5
    SCMS
    SCMI SCMF_DIS, 0, SCA_NONE,   'H', sc_L_hdrftr, 0
    SCMI SCMF_DIS, 8, SCA_NONE,   'I', sc_L_suminfo, 0
    SCMI SCMF_DIS, 2, SCA_NONE,   'O', sc_L_glossary, 0
    SCMI SCMF_DIS, 1, SCA_NONE,   'A', sc_L_table,  0

sc_it_view:                         ; &View
    SCMI SCMF_DIS, 0, SCA_NONE,   'O', sc_L_outline, 0
    SCMI SCMF_CHK, 0, SCA_DRAFT,  'D', sc_L_draft,  0
    SCMI SCMF_CHK, 0, SCA_PAGE,   'P', sc_L_page,   0
    SCMS
    SCMI SCMF_CHK, 2, SCA_VRIB,   'B', sc_L_ribbon, 0
    SCMI SCMF_CHK, 0, SCA_VRUL,   'R', sc_L_ruler,  0
    SCMI SCMF_CHK, 0, SCA_VSTA,   'S', sc_L_stabar, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'F', sc_L_footns, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'A', sc_L_annots, 0
    SCMS
    SCMI SCMF_DIS, 6, SCA_NONE,   'C', sc_L_fldcode, 0
    SCMI SCMF_DIS, 2, SCA_NONE,   'E', sc_L_prefs,  0
    SCMI SCMF_DIS, 6, SCA_NONE,   'M', sc_L_shortm, 0

sc_it_ins:                          ; &Insert
    SCMI SCMF_DIS, 0, SCA_NONE,   'B', sc_L_break,  0
    SCMI SCMF_DIS, 4, SCA_NONE,   'N', sc_L_footnote, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'F', sc_L_file,   0
    SCMI SCMF_DIS, 4, SCA_NONE,   'M', sc_L_bookmark, 0
    SCMI SCMF_DIS, 6, SCA_NONE,   'U', sc_L_pagenum, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'T', sc_L_table,  0
    SCMS
    SCMI SCMF_DIS, 0, SCA_NONE,   'A', sc_L_annot,  0
    SCMI 0,        0, SCA_PICT,   'P', sc_L_picture, 0
    SCMI SCMF_DIS, 4, SCA_NONE,   'D', sc_L_field,  0
    SCMI SCMF_DIS, 6, SCA_NONE,   'E', sc_L_ixentry, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'I', sc_L_index,  0
    SCMI 0,        9, SCA_TOC,    'C', sc_L_toc,    0

sc_it_fmt:                          ; Forma&t
    SCMI 0,        0, SCA_CHAR,   'C', sc_L_charctr, 0
    SCMI 0,        0, SCA_PARA,   'P', sc_L_para,   0
    SCMI SCMF_DIS, 0, SCA_NONE,   'S', sc_L_section, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'D', sc_L_documnt, 0
    SCMS
    SCMI SCMF_DIS, 0, SCA_NONE,   'T', sc_L_tabs,   0
    SCMI SCMF_DIS, 2, SCA_NONE,   'Y', sc_L_styles, 0
    SCMI SCMF_DIS, 1, SCA_NONE,   'O', sc_L_positn, 0
    SCMS
    SCMI SCMF_DIS, 2, SCA_NONE,   'F', sc_L_defsty, 0
    SCMI SCMF_DIS, 5, SCA_NONE,   'R', sc_L_picture, 0
    SCMI SCMF_DIS, 1, SCA_NONE,   'A', sc_L_table,  0

sc_it_util:                         ; &Utilities
    SCMI SCMF_DIS, 0, SCA_NONE,   'S', sc_L_spell,  0
    SCMI SCMF_DIS, 0, SCA_NONE,   'T', sc_L_thesaur, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'H', sc_L_hyphen, 0
    SCMS
    SCMI 0,        0, SCA_RENUM,  'R', sc_L_renum,  0
    SCMI SCMF_DIS, 9, SCA_NONE,   'M', sc_L_revmark, 0
    SCMI SCMF_DIS, 8, SCA_NONE,   'V', sc_L_compver, 0
    SCMI 0,        1, SCA_SORT,   'O', sc_L_sort,   0
    SCMI SCMF_DIS, 0, SCA_NONE,   'C', sc_L_calc,   0
    SCMI SCMF_DIS, 2, SCA_NONE,   'P', sc_L_repag,  0
    SCMI SCMF_DIS, 1, SCA_NONE,   'U', sc_L_custom, 0

sc_it_mac:                          ; &Macro
    SCMI SCMF_DIS, 2, SCA_NONE,   'C', sc_L_record, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'R', sc_L_run,    0
    SCMI SCMF_DIS, 0, SCA_NONE,   'E', sc_L_edit,   0
    SCMI SCMF_DIS, 10, SCA_NONE,  'K', sc_L_asgkey, 0
    SCMI SCMF_DIS, 10, SCA_NONE,  'M', sc_L_asgmenu, 0

sc_it_win:                          ; &Window
    SCMI SCMF_DIS, 0, SCA_NONE,   'N', sc_L_newwin, 0
    SCMI SCMF_DIS, 0, SCA_NONE,   'A', sc_L_arrange, 0
    SCMS
    SCMI SCMF_CHK, 0, SCA_WIN1,   '1', sc_win1,     0

sc_it_help:                         ; &Help
    SCMI SCMF_DIS, 0, SCA_NONE,   'I', sc_L_index2, 0
                                    ; Index stays greyed: no help files exist
                                    ; on this platform - F1 and About... are
                                    ; the help there is (SPEC.md 47/68.2)
    SCMS
    SCMI SCMF_DIS, 0, SCA_NONE,   'K', sc_L_keybrd, 0
    SCMI SCMF_DIS, 7, SCA_NONE,   'W', sc_L_actwin, 0
    SCMS
    SCMI SCMF_DIS, 0, SCA_NONE,   'T', sc_L_tutor,  0
    SCMI SCMF_DIS, 6, SCA_NONE,   'H', sc_L_usehelp, 0
    SCMS
    SCMI 0,        0, SCA_ABOUT,  'A', sc_L_about,  0

sc_it_fontc:                        ; Pica, and room for what FONTS/ carries
    SCMI 0, 0, SCA_CSEL, 0, sc_s_pica, 0
%rep SC_MAXFONT                     ; RESERVED, and filled by sc_fontscan
    SCMI SCMF_DIS, 0, SCA_NONE, 0, sc_s_pica, 0
%endrep
sc_it_ptsc:                         ; ...at the one size
    SCMI 0, 0, SCA_CSEL, 0, sc_s_10, 0
sc_it_stylec:                       ; ...in the one style
    SCMI 0, 0, SCA_CSEL, 0, sc_s_normal, 0

; --- the labels (menus.cmd, '&' removed) -------------------------------------
sc_L_new:      db 'New...', 0
sc_L_open:     db 'Open...', 0
sc_L_close:    db 'Close', 0
sc_L_save:     db 'Save', 0
sc_L_saveas:   db 'Save As...', 0
sc_L_saveall:  db 'Save All', 0
sc_L_find:     db 'Find...', 0
sc_L_exit:     db 'Exit', 0
sc_L_undo:     db 'Undo', 0
sc_L_repeat:   db 'Repeat', 0
sc_L_cut:      db 'Cut', 0
sc_L_copy:     db 'Copy', 0
sc_L_paste:    db 'Paste', 0
sc_L_plink:    db 'Paste Link...', 0
sc_L_search:   db 'Search...', 0
sc_L_replace:  db 'Replace...', 0
sc_L_goto:     db 'Go To...', 0
sc_L_hdrftr:   db 'Header/Footer...', 0
sc_L_suminfo:  db 'Summary Info...', 0
sc_L_glossary: db 'Glossary...', 0
sc_L_table:    db 'Table...', 0
sc_L_outline:  db 'Outline', 0
sc_L_draft:    db 'Draft', 0
sc_L_page:     db 'Page', 0
sc_L_ribbon:   db 'Ribbon', 0
sc_L_ruler:    db 'Ruler', 0
sc_L_stabar:   db 'Status Bar', 0
sc_L_footns:   db 'Footnotes', 0
sc_L_annots:   db 'Annotations', 0
sc_L_fldcode:  db 'Field Codes', 0
sc_L_prefs:    db 'Preferences...', 0
sc_L_shortm:   db 'Short Menus', 0
sc_L_break:    db 'Break...', 0
sc_L_footnote: db 'Footnote...', 0
sc_L_file:     db 'File...', 0
sc_L_bookmark: db 'Bookmark...', 0
sc_L_pagenum:  db 'Page Numbers...', 0
sc_L_annot:    db 'Annotation', 0
sc_L_picture:  db 'Picture...', 0
sc_L_field:    db 'Field...', 0
sc_L_ixentry:  db 'Index Entry...', 0
sc_L_index:    db 'Index...', 0
sc_L_toc:      db 'Table of Contents...', 0
sc_L_charctr:  db 'Character...', 0
sc_L_para:     db 'Paragraph...', 0
sc_L_section:  db 'Section...', 0
sc_L_documnt:  db 'Document...', 0
sc_L_tabs:     db 'Tabs...', 0
sc_L_styles:   db 'Styles...', 0
sc_L_positn:   db 'Position...', 0
sc_L_defsty:   db 'Define Styles...', 0
sc_L_spell:    db 'Spelling...', 0
sc_L_thesaur:  db 'Thesaurus...', 0
sc_L_hyphen:   db 'Hyphenate...', 0
sc_L_renum:    db 'Renumber...', 0
sc_L_revmark:  db 'Revision Marks...', 0
sc_L_compver:  db 'Compare Versions...', 0
sc_L_sort:     db 'Sort...', 0
sc_L_calc:     db 'Calculate', 0
sc_L_repag:    db 'Repaginate Now', 0
sc_L_custom:   db 'Customize...', 0
sc_L_record:   db 'Record...', 0
sc_L_run:      db 'Run...', 0
sc_L_edit:     db 'Edit...', 0
sc_L_asgkey:   db 'Assign to Key...', 0
sc_L_asgmenu:  db 'Assign to Menu...', 0
sc_L_newwin:   db 'New Window', 0
sc_L_arrange:  db 'Arrange All', 0
sc_L_index2:   db 'Index', 0
sc_L_keybrd:   db 'Keyboard', 0
sc_L_actwin:   db 'Active Window', 0
sc_L_tutor:    db 'Tutorial', 0
sc_L_usehelp:  db 'Using Help', 0
sc_L_about:    db 'About...', 0

; --- the shortcut captions (keys.cmd via SetBcmMenuKeys's Alt+/Ctrl+/Shift+
;     spelling; only commands the keymap actually binds carry one) -----------
sc_C_cf12:  db 'Ctrl+F12', 0
sc_C_sf12:  db 'Shift+F12', 0
sc_C_f12:   db 'F12', 0
sc_C_csf12: db 'Ctrl+Shift+F12', 0
sc_C_af4:   db 'Alt+F4', 0
sc_C_abksp: db 'Alt+BkSp', 0
sc_C_f4:    db 'F4', 0
sc_C_sdel:  db 'Shift+Del', 0
sc_C_cins:  db 'Ctrl+Ins', 0
sc_C_sins:  db 'Shift+Ins', 0
sc_C_f5:    db 'F5', 0

; --- ribbon / ruler / status strings and glyph tables ------------------------
sc_s_font:   db 'Font:', 0
sc_s_pica:   db 'Pica', 0           ; the 8x8 face at fixed pitch: pica is
                                    ; the honest name for 10 cpi
sc_s_pts:    db 'Pts:', 0
sc_s_10:     db '10', 0
sc_s_style:  db 'Style:', 0
sc_s_normal: db 'Normal', 0
sc_s_15:     db '1.5', 0
sc_agl:                             ; alignment glyphs: 4 rows of (x1,x2)
    db 2,9, 2,6, 2,9, 2,5           ; flush left
    db 2,9, 3,8, 2,9, 4,7           ; centered
    db 2,9, 5,9, 2,9, 6,9           ; flush right
    db 2,9, 2,9, 2,9, 2,9           ; justified
sc_tgl:                             ; tab glyphs: stem x, foot x1, foot x2
    db 3, 3, 8                      ; left tab
    db 5, 2, 8                      ; center tab
    db 7, 2, 7                      ; right tab
    db 5, 2, 8                      ; decimal tab (its point is drawn on top)
sc_ptick: times 8 db 0x7F           ; gfx_fill_pat rows: leftmost pixel of
                                    ; every byte black = a tick every 8px
sc_s_pg:    db 'Pg ', 0
sc_s_sec:   db '  Sec 1  ', 0
sc_s_at:    db '  At ', 0
sc_s_li:    db 'li', 0
sc_s_ln:    db '  Ln ', 0
sc_s_col:   db '  Col ', 0
sc_s_ext:   db 'EXT', 0
sc_s_caps:  db 'CAPS', 0
sc_s_num:   db 'NUM', 0
sc_s_ovr:   db 'OVR', 0
sc_s_sp4:   db '    ', 0
sc_s_sp3:   db '   ', 0
sc_s_about: db 'Scribe', 0
sc_s_abou2: db 'A word processor for os8088', 0
sc_s_abou3: db 'Forked from WORD (SPEC.md 86)', 0
sc_s_ok:    db 'OK', 0
sc_m_noclose: db 'Close refused - try again', 0

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; A word processor's frame, not a note pad's: 600x440 at the 640x480
; reference, clamped onto smaller adapters by wm_create (SPEC.md 11). The
; chrome strips cost 60px of content, so a small frame would leave the
; document a letterbox.
; What this window wants on each adapter (SPEC.md 11.100.1). VGA and CGA are
; PAIRS OF ZEROS - "nothing to say" - so the template below stands and wm_fit
; clamps it as it always did: 600x435 on a VGA and 600x155 on a CGA, where the
; desktop band is the whole of what there is.
;
; The HERCULES entry is the decision. 720 columns against a template that has
; never asked for more than 600, and a page is the one piece of content always
; glad of them: 712 is the widest frame that still leaves this window its
; 8-aligned content origin (wm_snap_ax rounds x DOWN to 7 there, and 7 + 712 =
; 719). The height is the template's and the band clamps it to 303, which is
; 11.100.1's "a real width and a generous height".
    OS88_PREFER sc_pref, 0, 0,  712, 440,  0, 0

sc_tpl:
    dw 20, 24, 600, 440
    dw sc_wttl, sc_paint, sc_onkey, sc_onclick
                                    ; the TITLE is the bss buffer sc_compttl
                                    ; composes - 'Scribe - <NAME>' -
                                    ; before wm_create banks the pointer, and
                                    ; sc_settitle recomposes it on every name
                                    ; change (SPEC.md 68.2)

; The EMPTY kernel menu set (SPEC.md 12.2): zero menus, but its AM_NAME puts
; 'Scribe' in the kernel bar instead of the 15-char header name. The
; command handler can never be called (there is nothing to pick); sc_mf_ret
; satisfies the layout.
    OS88_MENUSET sc_menus0, sc_ttl, sc_mf_ret
    OS88_MENUSET_END sc_menus0

sc_ttl: db 'Scribe', 0              ; the kernel bar's AM_NAME (6 chars,
                                    ; inside the 15); the TITLE BAR shows
                                    ; sc_wttl's 'Scribe - <NAME>'.
                                    ;
                                    ; SHORTER than Word's, which is the safe
                                    ; direction: sc_compttl builds the title
                                    ; into a fixed bss buffer sized for
                                    ; 'Microsoft Word - ' plus a name, so a
                                    ; shorter prefix cannot overrun it
sc_s_tpre: db 'Scribe - ', 0

%ifdef SCBENCH
; The walk bench (`make wdbench`), and the ONLY thing that reaches it in this
; file is the Ctrl-B in sc_onkey and the four SCVAR words at the foot - both
; inside %ifdef SCBENCH, so WORD.O88 carries not a byte of it.
%include "scbench.inc"
%endif

; --- the file and what the toast can say (SPEC.md 27.1) ------------------------
; The name is per-instance state now (sc_name in bss), seeded from this at
; launch and replaced by whatever the file dialog returns. The two verbs are
; PREFIXES: sc_setmsg composes them with the live name into sc_tbuf, because
; a toast that still said DOCUMENT.DOC after a Save As would be worse than no
; toast at all.
sc_s_default: db 'DOCUMENT.RTF', 0  ; Word's Document1, folded to 8.3: a 9-char
                                    ; stem is FERR_NAME on a FAT volume
                                    ; (SPEC.md 24), and a default that cannot
                                    ; save is worse than a shorter one.
                                    ;
                                    ; .RTF AND NOT .DOC IS SCRIBE'S ONE
                                    ; BEHAVIOURAL DIVERGENCE FROM WORD (88.4).
                                    ; sc_isrtf already decides the format from
                                    ; the extension, so changing the default
                                    ; NAME changes the default FORMAT and
                                    ; nothing else has to know. The reason is
                                    ; pictures: RTF can carry one losslessly
                                    ; at 4bpp and .DOC cannot (88.5), so the
                                    ; format that keeps the document whole is
                                    ; the one a plain Save should reach for.
                                    ; Save As onto a .DOC name still writes a
                                    ; real Word file - it just converts the
                                    ; pictures down on the way
sc_m_saved:   db 'Saved ', 0
sc_m_loaded:  db 'Loaded ', 0
sc_m_trunc:   db 'Truncated', 0
sc_s_nul:     db 0              ; an EMPTY string retires whatever is up
                                ; (SPEC.md 59.3) - one call, and this app
                                ; never has to know whether one was

; FERR_* (SPEC.md 18.4) -> string, indexed by the code itself
sc_errtab:
    dw sc_e_ok, sc_e_nodisk, sc_e_io, sc_e_name, sc_e_noent, sc_e_exist
    dw sc_e_full, sc_e_dirfull, sc_e_prot, sc_e_wprot, sc_e_big
sc_e_ok:      db 'Done', 0
sc_e_nodisk:  db 'No disk', 0
sc_e_io:      db 'Disk error', 0
sc_e_name:    db 'Bad name', 0
sc_e_noent:   db 'Not found', 0
sc_e_exist:   db 'Name exists', 0
sc_e_full:    db 'Disk full', 0
sc_e_dirfull: db 'Dir full', 0
sc_e_prot:    db 'Protected', 0
sc_e_wprot:   db 'Write protected', 0
sc_e_big:     db 'Too big', 0
sc_e_nomem:   db 'No memory', 0      ; the staging claim was refused (50.3)

; Insert > Picture (SPEC.md 94.9). One string per IMG_E_* code, indexed by it
; - the include hands back a NUMBER (87.2) and each package says what it means
; in its own voice.
; EVERY ONE OF THESE IS 24 CHARACTERS OR FEWER, because TOAST_MAX is 24 and
; kernel/toast.inc calls it "the tight one". The first draft ran to 58 and the
; strip simply stopped mid-word at the right edge of the screen - no error, no
; ellipsis, just a sentence with its end missing. The success line is built
; from a width and a height, so it is the longest possible one that has to
; fit: '1280x65535 - not placed' is 23.
sc_s_pictx:   db 'x', 0
sc_s_pict2:   db ' - not placed', 0
sc_e_pictio:  db 'Cannot read that file', 0
sc_e_pictbig: db 'That file is too big', 0
sc_e_pict0:   db 'Not a picture file', 0
sc_e_pictw:   db 'Not a picture file', 0
sc_e_picts:   db 'File is too short', 0
sc_e_pictd:   db 'Bad picture size', 0
sc_e_pictm:   db 'Picture too big', 0
sc_e_pictt:   db 'Picture data ends early', 0
sc_e_pictb:   db 'Convert to 4-bit first', 0
sc_e_pictc:   db 'Compressed BMP not read', 0
sc_e_pictn:   db 'No such picture', 0
sc_e_pictv:   db 'Unknown file version', 0
sc_e_pictfull: db 'No room for a picture', 0
sc_picterrs:
    dw sc_e_pict0                   ; 0 - never used: CF=0 means it worked
    dw sc_e_pictw, sc_e_picts, sc_e_pictd, sc_e_pictm, sc_e_pictt
    dw sc_e_pictb, sc_e_pictc, sc_e_pictn, sc_e_pictv
SC_PICTERRN equ IMG_E_VER

; --- search and the clipboard (SPEC.md 68.7/55) ------------------------------
sc_m_nopat:   db 'No search text', 0
sc_m_repld:   db ' changes', 0       ; sc_saycnt's suffix: 'n changes' is the
                                     ; sweep's answer (SPEC.md 68.7)
sc_m_noundo:  db 'Nothing to undo', 0
sc_m_papfull: db 'Too many paragraph formats', 0
sc_m_toobig:  db 'Document too complex to save', 0
sc_m_noclip:  db 'The clipboard is empty', 0        ; ^c with nothing in it
sc_m_replong: db 'Replacement too long', 0          ; ...and ^m/^c past
                                     ; SC_FRXMAX (SPEC.md 68.7)
sc_m_loaded2: db 'Module loaded from SCRIBE.OVL', 0
sc_m_toomanyp: db 'Too many paragraphs', 0          ; past SC_UMAXP (65.9)
sc_m_noundo2: db 'Too large to undo', 0             ; the span is bigger than
                                     ; the undo blob, so the command refuses
                                     ; rather than do it unundoably (65.9)
sc_m_notoc:   db 'No table of contents entries', 0
sc_m_sorted:  db 'Sorted', 0
sc_m_renumd:  db 'Renumbered', 0   ; more FKP pages than
                                     ; the staging claim holds (SPEC.md 68.4)
sc_e_cbig:    db 'Too big to copy', 0   ; over CLIP_MAXKB, or the heap could
                                        ; not fund the clipboard (SPEC.md 55)

; =============================================================================
; REACHING THE PACKAGE'S OWN VARIABLES FROM INSIDE THE MODULE (SPEC.md 94.8.3)
;
; The file-format engines read sc_dseg, sc_cseg, sc_len and the rest at moments
; when DS *and* ES are both pointed at the document, CHP or staging claims.
; Resident, they wrote `[cs:sc_len]`, because CS was the package. In the module
; CS is the module, and there is no third segment register to use instead:
; SS is LOW_SEG (SPEC.md 20.1), not the package.
;
; So the package's segment is stamped into the MODULE's own image at load time
; (sc_ovneed above), where CS does reach it, and these macros borrow DS around
; the access. Each one costs four to seven instructions where the resident
; version cost one, and each CLOBBERS NOTHING but its own destination and - for
; the two segment-loading forms - nothing at all: AX is banked and restored.
; `pop` does not touch the flags, which is what lets PKG_CMP end with one.
;
; They are macros and not a helper routine on purpose: a helper would need its
; arguments in registers, and the whole problem at these sites is that there is
; no register to spare.
%macro PKG_LD 2                     ; %1 = the package's word/byte at %2
    push ds
    mov ds, [cs:sc_pkgseg]
    mov %1, [%2]
    pop ds
%endmacro
%macro PKG_LDS 1                    ; DS = the package's word at %1. The 22
    push ax                         ; commonest sites are this one, and it
    push ds                         ; cannot use PKG_LD because the `pop ds`
    mov ds, [cs:sc_pkgseg]          ; would undo the load
    mov ax, [%1]
    pop ds
    mov ds, ax
    pop ax
%endmacro
%macro PKG_LDE 1                    ; ES = the package's word at %1
    push ax
    push ds
    mov ds, [cs:sc_pkgseg]
    mov ax, [%1]
    pop ds
    mov es, ax
    pop ax
%endmacro
%macro PKG_CMP 2                    ; cmp %1, [the package's %2]
    push ds
    mov ds, [cs:sc_pkgseg]
    cmp %1, [%2]
    pop ds
%endmacro
%macro PKG_ST 2                     ; [the package's %1] = %2
    push ds
    mov ds, [cs:sc_pkgseg]
    mov [%1], %2
    pop ds
%endmacro
%macro PKG_STB 2                    ; ...and the byte-sized store
    push ds
    mov ds, [cs:sc_pkgseg]
    mov byte [%1], %2
    pop ds
%endmacro
; =============================================================================

; --- the two file formats now live in SCRIBE.OVL (SPEC.md 94.8) --------------
; scdoc.inc and scrtf.inc used to be %included here, in .text. They are
; %included from inside `section .modc` further down instead, which is the
; whole of what moving a subsystem out costs: the includes moved, and every
; label in them was reclassified by the assembler and by tools/os88ovlchk.py
; without either being told twice.
;
; They are the right tenants. Between them they are the largest thing in this
; package and they run on exactly two commands - Open and Save - so the
; kilobytes they cost are kilobytes the REDRAW path was paying for a file
; dialog it sees twice a session.

; --- the search pattern and the Utilities commands (SPEC.md 68.7/68.9) -------
%include "scutil.inc"

; =============================================================================
; THE ON-DEMAND MODULE (SPEC.md 68.10)
;
; Code that ships as SCRIBE.OVL beside WORD.O88 instead of inside it, is read
; into a heap claim the first time one of its features is asked for, and is
; far-called through a dispatcher at its offset 0.
;
; The ceiling this exists to get under is not a budget: a package links at
; org 0 and addresses itself with 16-bit offsets (SPEC.md 33), so image + bss
; can never reach 64KB whatever the heap has free. A module has a segment of
; its own, so it does not spend the package's.
;
; It is kernel SPEC.md 2.8's shape, not SPEC.md 52.11's: the module keeps
; DS = THE PACKAGE'S SEGMENT and reaches the document, the claims and every
; sc_* variable through it exactly as resident code does. That is what makes
; moving a subsystem out a matter of moving its text, rather than rewriting
; every data reference in it. What it costs instead is the two rules below,
; and tools/os88ovlchk.py refuses a build that breaks the first.
;
;   1. A call from the module to resident code CANNOT be near - the module has
;      a CS of its own. It goes through a VECTOR: `call far [sc_v_*]`, a
;      dword of (offset, segment) filled in at load. Resident code calling
;      INTO the module goes the other way, through sc_ovcall.
;   2. Nothing in the module may assume CS. Its data lives in .text with
;      everything else, which is also what keeps rule 1 to code alone.
;
; Loading is the shape drivers/hdd/hdtool.inc uses (SPEC.md 52.11) and it
; needs no kernel byte: OSAPI_FILE_HERE / _GOTO to reach the package's own
; folder, OSAPI_MEM_CLAIM for the image, OSAPI_FILE_READ to fill it. A
; refusal - no heap, or the disk swapped for one without SCRIBE.OVL - is an
; ordinary path: the feature says what is missing and the document is
; untouched (SPEC.md 47).
; =============================================================================

SC_OVKB      equ 12             ; the claim SCRIBE.OVL is read into, KB. It
                                ; was 8 when the module held only the picture
                                ; decoder; the two file formats took it past
                                ; 9KB (SPEC.md 94.8). THE MAKEFILE CHECKS THIS
                                ; against the cut module and fails the build if
                                ; the module outgrows it - the number is read
                                ; out of this line, so there is one and not two
SCM_PING     equ 0              ; verbs, the module's dispatch indices
SCM_IMGLOAD  equ 1              ; SI = an OS88IMG_SZ block; see os88img.inc
SCM_DOCIMG   equ 2              ; the six file-format entry points (88.8).
SCM_DOCPARSE equ 3              ; Each takes and returns exactly what the
SCM_RTFIMG   equ 4              ; routine behind it always did - the far call
SCM_RTFPARSE equ 5              ; passes every register through and `retf`
SCM_ISRTF    equ 6              ; does not touch the flags, so CF comes back
SCM_LDPOST   equ 7              ; on its own
SCM_MAX      equ 7              ; ...and the highest of them

; -----------------------------------------------------------------------------
; sc_ovneed - make sure SCRIBE.OVL is loaded
; out: CF=0 with [sc_ovseg] the claim holding it; CF=1 and the toast already
;      says why. Preserves all registers.
;
; UI TASK ONLY: it claims and it reads a floppy, and SPEC.md 20.6 rule 7
; forbids both on a worker. [sc_inwk] is the same gate sc_itinit uses.
; -----------------------------------------------------------------------------
sc_ovneed:
    cmp word [sc_ovseg], 0
    jne .ok
    cmp byte [sc_inwk], 0
    jne .nowk
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE            ; where the USER is, to be put back
    mov [sc_ovwas], dx
    mov [sc_ovwdr], bl
    mov dx, [sc_ovdir]              ; ...and off to where we were launched
    mov bl, [sc_ovdrv]
    call OSAPI_FILE_GOTO
    mov ax, SC_OVKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [sc_ovseg], dx
    mov word [sc_ovfar], 0          ; the far pointer sc_ovcall goes through:
    mov [sc_ovfar+2], dx            ; the dispatcher is at the claim's 0
    mov es, dx
    xor bx, bx
    mov cx, SC_OVKB * 1024
    xor dx, dx
    mov si, sc_ovname
    call OSAPI_FILE_READ
    jc .noread
    mov ax, cs                      ; THE PACKAGE'S SEGMENT, STAMPED INTO THE
    mov [es:sc_pkgseg], ax          ; MODULE ITSELF (SPEC.md 94.8.3). ES is
                                    ; still the claim from the read above, and
                                    ; CS is the package because this routine
                                    ; is resident. It is the module's ONLY way
                                    ; to name the package: 20.1 says SS is
                                    ; LOW_SEG and not the package, and DS and
                                    ; ES are both busy at the sites that need
                                    ; it
    call sc_ovbind                  ; fill the vectors with our own segment
    call sc_ovback
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ok:
    clc
    ret
.noread:
    mov dx, [sc_ovseg]              ; the claim goes back: a half-loaded
    call OSAPI_MEM_FREE             ; module is worse than none
    mov word [sc_ovseg], 0
    mov ax, sc_m_noovl
    jmp short .say
.nomem:
    mov ax, sc_e_nomem
.say:
    call sc_saymsg
    call sc_ovback
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.nowk:
    stc
    ret

; sc_ovback - put the volume back where the user left it. Preserves all.
sc_ovback:
    push bx
    push dx
    mov dx, [sc_ovwas]
    mov bl, [sc_ovwdr]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    ret

; sc_ovbind - stamp this package's segment into every vector. Preserves all.
; The offsets are assembled in; only the segment is a runtime fact, and it is
; the same one for all of them.
sc_ovbind:
    push ax
    push bx
    mov ax, cs
    mov bx, sc_v_first
.l:
    mov [bx+2], ax
    add bx, 4
    cmp bx, sc_v_end
    jb .l
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_ovcall - far-call the module
; in:  BP = the verb (SCM_*), everything else as the verb documents
; out: as the verb; CF=1 if the module could not be loaded at all
; -----------------------------------------------------------------------------
sc_ovcall:
    call sc_ovneed
    jc .no                          ; CF=1 already, and it has said why
    mov word [sc_ovmsg], 0
    call far [sc_ovfar]             ; the dispatcher is at the claim's offset
                                    ; 0, so the far pointer is (0, the claim)
                                    ; and the verb's own CF comes back through
    push ax                         ; THE MODULE NEVER SPEAKS; IT LEAVES A
    pushf                           ; REASON AND THIS SAYS IT (SPEC.md 94.8.1).
    mov ax, [sc_ovmsg]              ; The one shim whose resident routine
    or ax, ax                       ; touched the UI - sc_papfind's refusal
    jz .quiet                       ; toast - is the shape 82.16 records a
    mov word [sc_ovmsg], 0          ; freeze against, and this is not a way
    call sc_saymsg                  ; round that so much as the rule
.quiet:                             ; os88img.inc already follows: IMG_ERR is
    popf                            ; a NUMBER and the caller words the
    pop ax                          ; message (85). Generalised, and put in
.no:                                ; the ONE place every future verb passes
    ret                             ; through

sc_ovname:  db 'SCRIBE.OVL', 0
sc_m_noovl: db 'SCRIBE.OVL is not on this disk', 0

; --- the vectors: resident routines the module far-calls (rule 1) ------------
; Offset assembled in, segment stamped by sc_ovbind. They are contiguous and
; sc_ovbind walks them, so adding one is two lines and no other change.
;
; Each points at a SHIM, never at the routine itself. Every resident routine
; in this package is a near proc ending in `ret` (SPEC.md 20.1 - a package
; author never writes `retf`), so far-calling one directly pops the offset,
; leaves the segment on the stack and returns into nothing. The shim is the
; one place that difference lives: a near call to the real routine, then the
; far return the module's call is waiting for. Registers and flags pass
; through both ways untouched, so a shimmed routine keeps its own contract.
sc_s_saymsg:
    call sc_saymsg
    retf
sc_s_resize:
    call sc_resize
    retf
sc_s_pictfree:
    call sc_pictfree
    retf
sc_s_papfind:
    call sc_papfind0                ; the UI-FREE half - see sc_papfind
    retf
sc_s_ldscan:
    call sc_ldscan
    retf
sc_s_picrec:
    call sc_picrec
    retf

sc_v_first:
sc_v_saymsg:   dw sc_s_saymsg
               dw 0
sc_v_resize:   dw sc_s_resize
               dw 0
sc_v_pictfree: dw sc_s_pictfree
               dw 0
sc_v_papfind:  dw sc_s_papfind
               dw 0
sc_v_ldscan:   dw sc_s_ldscan
               dw 0
sc_v_picrec:   dw sc_s_picrec
               dw 0
sc_v_end:

; --- the module itself -------------------------------------------------------
; vstart=0: it is loaded at offset 0 of its claim, so its own labels must be
; numbered from there. NASM coalesces every .text fragment before it, so this
; lands at the END of the image whatever order the source is in - which is
; what lets tools/os88ovl.py cut at the image size the header already carries.
;
; align=1 is load-bearing, not tidiness. A bin section aligns to 4 by default,
; and the pad NASM inserts to reach it lands BETWEEN the image size the header
; records and where this section really starts - so the cut would take the pad
; with it and the dispatcher would not be at the module's offset 0. It was one
; byte, and the module ran off the end of its own jump table.
section .modc vstart=0 align=1

sc_modc:                            ; +0: the dispatcher, and the only offset
    cmp bp, SCM_MAX                 ; the resident half knows
    ja .out
    shl bp, 1
    jmp word [cs:bp+sc_mverb]       ; INDEX THROUGH BP, NOT SI. This used to
.out:                               ; stage the doubled verb in SI, and SI is
    retf                            ; an ARGUMENT to a verb that takes one -
                                    ; img_load's whole contract is SI = the
                                    ; caller's block, and it would have been
                                    ; handed a 0 or a 2. It lay dormant only
                                    ; because SCM_PING takes nothing. The same
                                    ; line was live in CHART's dispatcher and
                                    ; cost an afternoon there

; The package's segment, stamped here by sc_ovneed the moment the module is
; read (SPEC.md 94.8.3). It is IN THE MODULE and not in the package's bss,
; because the whole point is that module code can reach it with CS alone.
sc_pkgseg: dw 0

sc_mverb:
    dw sc_m_ping, sc_m_imgload
    dw sc_m_docimg, sc_m_docparse, sc_m_rtfimg, sc_m_rtfparse
    dw sc_m_isrtf, sc_m_ldpost

; sc_m_ping - what the mechanism has actually been PROVEN to do, and it is
; deliberately not more than that. Measured on the emulator, in this order:
; SCRIBE.OVL is found beside WORD.O88 and read into a claim; the far call lands
; on this dispatcher at the module's offset 0; the verb table dispatches; and
; the retf comes back to sc_ovcall with the app healthy afterwards. A far call
; OUT through sc_v_* to a shim that only returns was proven the same way.
;
; WHAT IS NOT PROVEN, and must be before any feature moves out here: a shim
; whose resident routine touches the UI. Calling sc_saymsg through sc_s_saymsg
; froze the app, and every static explanation was checked and cleared - the
; vector holds the shim's address, sc_ovbind stamps the right segment, the
; string offset AX carries is the string, and the shim's near call lands
; exactly on sc_saymsg at 0x46E5 (the displacement wraps 64K, which is legal
; and correct). So the cause is dynamic and still unknown, and the honest
; place to record that is here rather than in a commit message.
sc_m_ping:
    retf

; -----------------------------------------------------------------------------
; sc_m_imgload - decode a picture. SI = the caller's block, as img_load takes
; it; everything else is that routine's contract (SPEC.md 93).
;
; THE DECODER IS THIS MODULE'S FIRST REAL TENANT, and it is a deliberate
; choice of first tenant rather than the most useful thing that happened to be
; movable. The note above says what is unproven out here: a shim whose
; resident routine touches the UI. os88img.inc HAS NO SHIMS. It owns no state,
; it calls no OSAPI, it reads two segments the caller names and writes a third
; - one far call in, one retf out, and nothing in between that needs the
; package. So it exercises exactly the path that IS proven and none of the
; path that is not, which is why the freeze is still an open question below
; and this still works.
;
; It is also 1,600-odd bytes that WORD, with 5,270 free of 61,440, could not
; have spent resident.
; -----------------------------------------------------------------------------
sc_m_imgload:
    call img_load
    retf

; --- the file formats, and the six ways in (SPEC.md 94.8) --------------------
; Each is a near proc ending in `ret`, as every routine in this package is
; (SPEC.md 20.1), so the verb table cannot point at one directly - the
; dispatcher's `jmp` arrives with a far return address on the stack. Each gets
; the same two-line wrapper the shims use in the other direction, and for the
; same reason. Registers pass through untouched and `retf` does not alter the
; flags, so CF comes back exactly as the routine set it.
sc_m_docimg:    call sc_docimg
                retf
sc_m_docparse:  call sc_docparse
                retf
sc_m_rtfimg:    call sc_rtfimg
                retf
sc_m_rtfparse:  call sc_rtfparse
                retf
sc_m_isrtf:     call sc_isrtfimg
                retf
sc_m_ldpost:    call sc_ldpost
                retf

%include "scdoc.inc"
%include "scrtf.inc"
%include "os88img.inc"

section .text

; --- the state SPEC.md 27.8/27.9/27.10 added ---------------------------------
; The block below the image (further down this file) is ~120 hand-computed
; `equ os88_image_end + N` lines, and that was survivable while it grew a word
; at a time. Selection, undo, the clipboard glue and the find panel add forty
; fields at once, so these are laid out by a PREPROCESSOR COUNTER instead:
; SCVAR emits the same equ and moves the counter on, and SC_BSS_TOTAL falls
; out of where it stops. Nothing about the older fields changed - they are
; still where they were - and the two blocks cannot collide, because this one
; starts at the byte the other one ends on.
;
; It is here, ABOVE OS88_BSS, because OS88_BSS needs the total and the counter
; is the total. os88_image_end is still a forward reference at this point,
; which is exactly what every line of code referencing these fields already
; relies on.
%assign SCB 508 + SC_MAXROWS*2      ; where the original block ends
%macro SCVAR 2                      ; name, size in bytes
    %1 equ os88_image_end + SCB
    %assign SCB SCB + %2
%endmacro

; --- the selection (SPEC.md 27.8) --------------------------------------------
    SCVAR sc_sel0,  2       ; word: the first selected character index...
    SCVAR sc_sel1,  2       ; word: ...and one past the last. sel1 > sel0
                            ; whenever [sc_selon] is set, and the pair is
                            ; ORDERED here so nothing downstream has to ask
    SCVAR sc_anchor, 2      ; word: where a drag started, which is the end
                            ; that does NOT move while the pointer does
    SCVAR sc_selon, 1       ; byte: a selection exists
    SCVAR sc_rs0,   2       ; word } the CELLS of the row being accumulated
    SCVAR sc_rs1,   2       ; word } that fall inside it, 0xFFFF = none. The
                            ; inversion is per row, like the caret's column,
                            ; because that is the unit sc_rflush draws in
    SCVAR sc_osel0, 2       ; word } the selection the SCREEN is showing, as
    SCVAR sc_osel1, 2       ; word } character indices, and whether it has one
    SCVAR sc_oselon, 1      ; byte }
    SCVAR sc_selonly, 1     ; byte: this redraw cannot have changed a
                            ; character - only the selection moved - so a row
                            ; owes an INVERSION and no glyphs at all. One-shot
    SCVAR sc_xs0,   2       ; word } the cells of the row being accumulated
    SCVAR sc_xs1,   2       ; word } whose selected-ness DIFFERS from what the
                            ; screen shows: the symmetric difference, which is
                            ; exactly what one XOR has to flip
    SCVAR sc_prs0,  2       ; word } and what the delta cache's row was last
    SCVAR sc_prs1,  2       ; word } DRAWN with, so a selection that moved
                            ; over unchanged text still redraws (SPEC.md 27.8)
    SCVAR sc_dpos,  2       ; word: a drag-and-drop's insertion point, or
                            ; 0xFFFF when no marker is on screen
    SCVAR sc_dmark, 2       ; word } the marker's x and y, banked so the XOR
    SCVAR sc_dmy,   2       ; word } that erases it cannot disagree with the
                            ; XOR that drew it - a scroll between the two
                            ; would leave the old bar on screen for good
    SCVAR sc_dpx,   2       ; word } where the press landed, for the dead-zone
    SCVAR sc_dpy,   2       ; word } test that keeps a click a click
    SCVAR sc_mvs,   2       ; word } sc_movesel's four numbers. In bss rather
    SCVAR sc_mve,   2       ; word } than in registers because the three
    SCVAR sc_mvp,   2       ; word } reversals want all four at once and the
    SCVAR sc_mvn,   2       ; word } 8086 has nowhere to put them

; --- undo (SPEC.md 27.9) -----------------------------------------------------
; Five records, oldest first, and a heap arena holding their blobs packed in
; the same order. A record says: at [sc_upos], this group INSERTED [sc_uins]
; bytes and REMOVED the [sc_udel] bytes at [sc_uoff] in the arena. Undoing it
; is therefore "delete uins at upos, insert the blob at upos" and nothing else
; - which is why there is no redo and no second representation.
    SCVAR sc_upos, SC_UNDO*2
    SCVAR sc_uins, SC_UNDO*2
    SCVAR sc_udel, SC_UNDO*2
    SCVAR sc_uoff, SC_UNDO*2
                            ; there is no "caret before the group" word, and
                            ; there was one until it turned out to be derived:
                            ; an undo puts the caret at upos + udel, the end
                            ; of what it just put back, and that is right for
                            ; all four shapes a group can have - a typing run
                            ; (udel 0, so upos: where you started), a
                            ; backspace run (the position you backspaced
                            ; from), a cut and a paste
    SCVAR sc_useg, 2        ; word: the arena claim, 0 = none held. Claimed on
                            ; the first edit worth remembering and given back
                            ; by File > New, a load and sc_uclear
    SCVAR sc_ukb,  2        ; word: its size in KB
    SCVAR sc_utop, 2        ; word: bytes of it in use - also the top record's
                            ; blob end, which is what makes an append free
    SCVAR sc_un,   1        ; byte: records live, 0..SC_UNDO
    SCVAR sc_uopen, 1       ; byte: the newest is still ACCUMULATING. Closed
                            ; by SC_IDLE ticks of not editing (the worker), by
                            ; anything that is not an edit, and by an edit
                            ; that does not touch the group's own span
    SCVAR sc_unolog, 1      ; byte: suppress recording - set while undo itself
                            ; is editing the buffer, or the undo of an undo
                            ; would be recorded as an edit
    SCVAR sc_upad, 1        ; byte: keeps the words below even

; --- search / replace / go to (SPEC.md 68.7) ---------------------------------
    SCVAR sc_lmx, 2         ; word } the drag loop's last pointer position
    SCVAR sc_lmy, 2         ; word } (sc_dragsel's has-it-moved filter)
    SCVAR sc_fopt, 1        ; byte: the search options, SCFO_* bits - Whole
                            ; Word, Match Upper/Lowercase, Confirm Changes.
                            ; Persistent: the dialogs re-open with the last
                            ; answers, and F4 repeats with them
    SCVAR sc_fdir, 1        ; byte: Direction - 0 down, 1 up
    SCVAR sc_fpatn, 2       ; word: characters in the search text...
    SCVAR sc_frepn, 2       ; word: ...and in the replacement
    SCVAR sc_fmst,  2       ; word } the match the selection is showing - the
    SCVAR sc_fmen,  2       ; word } confirm session replaces exactly this
    SCVAR sc_rxend, 2       ; word: one past the match sc_matchat found
    SCVAR sc_cfn,   2       ; word: replacements made this sweep (the toast)
    SCVAR sc_fpat,  SC_PATMAX+1
    SCVAR sc_frep,  SC_PATMAX+1

; --- the file, the dirty prompt and the title (SPEC.md 68.4) -----------------
    SCVAR sc_dirty, 1       ; byte: the document differs from the disk. Set
                            ; by every buffer/format mutation, cleared by a
                            ; successful save, a load and File > New
    SCVAR sc_ovr,   1       ; byte: overtype (Ins; the status OVR lamp)
    SCVAR sc_ext,   1       ; byte: extend-selection mode (F8; the EXT lamp)
    SCVAR sc_pend,  1       ; byte: what the dirty prompt's Yes/No proceed to
                            ; (SCP_*), 0 = nothing pending
    SCVAR sc_wttl,  32      ; 'Scribe - ' + sc_name + NUL: the title
                            ; the window record points at, so it must live
                            ; for the window's whole life (SPEC.md 68.2)
    SCVAR sc_pmsg,  46      ; 'Do you want to save changes to <name>?'
    SCVAR sc_de_goto, 8     ; the Go To dialog's page-number edit
    SCVAR sc_fsz,   2       ; word } the dialog listing's file size, banked
    SCVAR sc_fszh,  2       ; word } by sc_ondlg for the open gate (65.4)
; --- word wrap (SPEC.md 27.11) -----------------------------------------------
    SCVAR sc_wstart, 1      ; byte: the index the walk is standing on begins a
                            ; word, so sc_wordfit has a question to answer.
                            ; Maintained by the walk itself - set at its start,
                            ; by a space and by a line break, cleared by every
                            ; other character - because the alternative is
                            ; reading the character BEFORE the one in hand,
                            ; which a seeded walk cannot always do

; --- the chunked height count (SPEC.md 27.7.3) -------------------------------
; Where the count has got to, and where a bounded walk stopped. The two are
; separate because sc_walk's .stop is shared by every bounded walk in the
; module - a paint, a caret key - and only sc_height may keep what it reports.
    SCVAR sc_hrow,  2       ; word: the ABSOLUTE row the next chunk resumes at,
                            ; 0 = from the top. Absolute and not visible,
                            ; because [sc_top] may move between chunks and the
                            ; seed sc_walk wants is relative to wherever the
                            ; view is when the chunk actually runs
    SCVAR sc_hi,    2       ; word: the character index that row begins at.
                            ; sc_seedrow cannot supply this - sc_rows is
                            ; SC_MAXROWS long, one slot per VISIBLE row, so it
                            ; describes the view and can never name row 300 of
                            ; a 500-row note (SPEC.md 27.5)
    SCVAR sc_sowed, 1       ; byte: a scroll moved [sc_top] and its repaint was
                            ; dropped, because OSAPI_EVQ_PENDING said another
                            ; event was right behind it. The worker owes it
                            ; (SPEC.md 27.7.8) - and it must be the WORKER
                            ; rather than the next click, because the next
                            ; event in the queue may belong to another window
                            ; and this one would then never be drawn at all
    SCVAR sc_stoprow, 2     ; word } what .stop reached, published every time
    SCVAR sc_stopi, 2       ; word } and read only by sc_height, on the walk it
                            ; issued itself - under the lock, with nothing
                            ; between the call and the read

; --- the row index (SPEC.md 27.13) -------------------------------------------
; 134 bytes, and it is what makes a row OUTSIDE the view reachable without
; laying out the note to get to it. Entry n is the character index at which
; absolute row n << [sc_xksh] begins; the stride doubles rather than the table
; growing, so this is the whole cost however long the note is.
    SCVAR sc_xi, SC_XN * 2  ; the indices, one per checkpoint
    SCVAR sc_xn, 2          ; word: how many are valid, CONTIGUOUSLY from 0 -
                            ; a hole would answer for a row nobody walked
    SCVAR sc_xnext, 2       ; word: the absolute row the next entry wants, kept
                            ; rather than derived so sc_xnote is one compare
    SCVAR sc_xksh, 1        ; byte: log2 of the stride. A log, because a lookup
                            ; is then a shift and not a 150-clock `div`
    SCVAR sc_xpad, 1        ; byte: keep the words that follow even

; --- the append fast path (SPEC.md 27.14) ------------------------------------
    SCVAR sc_ap1, 2         ; the one character sc_append letters, NUL-capped:
                            ; OSAPI_FONT_RUN takes a string and this is the
                            ; whole of it. Not sc_rbuf, which is the row cache
                            ; sc_rflush diffs against and must not be disturbed
    SCVAR sc_apch, 1        ; ...the character on its own, because AL is spent
    SCVAR sc_aprow, 1       ; the tail is a hard NEWLINE, not the end of the
                            ; note - so rows below have shifted an index
    SCVAR sc_apx,  2        ; word } the cell's x, the caret's x one along, and
    SCVAR sc_apx2, 2        ; word } the row's y - banked because the far calls
    SCVAR sc_apy,  2        ; word } between them return nothing of their own

; --- how deep a caret move can dirty (SPEC.md 27.4.1) ------------------------
    SCVAR sc_mvbot, 2       ; word: the DEEPER of the two visible rows a caret
                            ; move touched - the one it left and the one it
                            ; arrived on - or 0x7FFF for "not a caret move, or
                            ; nowhere in particular". Written by sc_move, which
                            ; is the only caller that knows both; sc_fastcm
                            ; parks it at the sentinel, so Left and Right (kind
                            ; 4 as well, and adjacent rows they do not measure)
                            ; can never inherit an Up's bound

; --- the proportional face, SPEC.md 68.13 ------------------------------------
    SCVAR sc_px, (SC_MAXCOL + 3) * 2  ; word per cell: the pen the k-th cell
                                  ; starts at, RELATIVE to [sc_tx], plus TWO
                                  ; past the end - one so a span's right edge
                                  ; is a lookup rather than a special case, and
                                  ; one for the caret's parking cell, which is
                                  ; a cell the walk never stored and sc_rflush
                                  ; still letters a space into. This is the
                                  ; whole of what a proportional row costs over
                                  ; a fixed one - in a fixed face the k-th cell
                                  ; is at 8k and nothing needs storing, which
                                  ; is why sc_cx branches rather than always
                                  ; reading this
    SCVAR sc_rcn, 2               ; word: cells stored in the row so far. In a
                                  ; fixed face the cell index comes from the
                                  ; PEN - (di - tx) >> 3 - and a tab therefore
                                  ; leaves empty cells behind it; proportional,
                                  ; there is no such division, so the index is
                                  ; counted here instead and a tab leaves none
    SCVAR sc_prop, 1              ; byte: 1 = set the document in [sc_face],
                                  ; 0 = the kernel's 8x8 cell. THE DEFAULT IS
                                  ; 0 and bss arrives zeroed, so a build with
                                  ; no face file, or a machine whose kernel
                                  ; refuses the band blit, behaves exactly as
                                  ; this program did before any of this
    SCVAR sc_face, 1              ; byte: the ty_open handle, 0 = none opened
    SCVAR sc_fname, 16            ; the family name for the Font combo
    SCVAR sc_fcap, 2              ; word: what the ribbon's Font box SHOWS -
                                  ; sc_s_pica until a face is chosen
    SCVAR sc_fsel, 1              ; byte: the chosen family, 0-based
    SCVAR sc_nfont, 1             ; byte: families the scan listed, clamped
    SCVAR sc_fscan, 1             ; byte: the scan has run (once, lazily)
    SCVAR sc_pickm, 1             ; byte } which menu a chosen item came out
    SCVAR sc_picki, 1             ; byte } of, and which item it was
    SCVAR sc_redrw, 1             ; byte: this action owes the note a redraw,
                                  ; taken on the way out as a tail jump
    SCVAR sc_pxon, 1              ; byte: sc_px[] is the truth about where a
                                  ; cell sits. 0 = the 8-pixel grid, which is
                                  ; what sc_cx computes without reading it.
                                  ; sc_facemetrics sets it from [sc_prop], so
                                  ; the metrics and the glyphs can never be two
                                  ; different faces (SPEC.md 68.13.1)
    SCVAR sc_spadv, 2             ; word: what a SPACE is worth in the current
                                  ; face. The row buffer pads with spaces and
                                  ; sc_pxcell extrapolates the parking cell by
                                  ; exactly this, so the pen it predicts is the
                                  ; one ty_putn will take
    SCVAR sc_prowrx, 2            ; word: the absolute x the cached row's ink
                                  ; ended at when it was drawn. A row that
                                  ; SHRANK is erased from its new right edge to
                                  ; this, in one fill - a proportional row
                                  ; cannot pad itself blank the way a fixed one
                                  ; does (SPEC.md 68.13)
    SCVAR sc_bx0,  2              ; word: the band's aligned origin
    SCVAR sc_gh,   2              ; word: the GLYPH BAND's height in pixels -
                                  ; 8 for the kernel's cell, the face's `rows`
                                  ; for a chosen one. Every site that used to
                                  ; add 7 to a row's y to reach its last row
                                  ; reads sc_gh1 instead
    SCVAR sc_gh1,  2              ; word: ...that, less one
    SCVAR sc_ghb,  2              ; word: and the ROW ADVANCE's base - the band
                                  ; plus the face's leading, which line spacing
                                  ; is then added to (sc_papload)

    SCVAR sc_rbuf, SC_MAXCOL + 1  ; the row being accumulated, space-filled
    SCVAR sc_prow, SC_MAXCOL      ; ...and what was last DRAWN on the cached
                                  ; row, so the next keystroke draws the delta.
                                  ; THE ONLY TWO FIELDS SIZED BY SC_MAXCOL,
                                  ; which is why they are here rather than in
                                  ; the hand-numbered block above (see the hole
                                  ; at +238 there)

; --- the chrome's state (SPEC.md 68.2) ---------------------------------------
    SCVAR sc_cl,   2        ; word } the content box, banked by sc_bounds so
    SCVAR sc_ct,   2        ; word } every strip painter and hit test reads
    SCVAR sc_cw,   2        ; word } the same four numbers
    SCVAR sc_ch,   2        ; word }
    SCVAR sc_ctop, 2        ; word: the LIVE chrome top (sc_ctcalc)
    SCVAR sc_sctop, 2       ; word: ...and the one the screen was drawn under
                            ; (banked by sc_sigmark, read by sc_panmove)
    SCVAR sc_vrib, 1        ; byte } the View toggles: ribbon, ruler, status
    SCVAR sc_vrul, 1        ; byte } bar shown. Set to 1 in sc_entry - bss
    SCVAR sc_vsta, 1        ; byte } zero would mean all three hidden
    SCVAR sc_mopen, 1       ; byte: the open dropdown, SC_M_NONE = none.
                            ; 0..8 the bar, 9..11 the strip combos
    SCVAR sc_mhi,  1        ; byte: the XOR-highlighted item, 0xFF = none
    SCVAR sc_about, 1       ; byte: the About box is up (modal)
    SCVAR sc_quit, 1        ; byte: File > Close/Exit ran - the worker
                            ; finishes the teardown (sc_worker .quit)
    SCVAR sc_stok, 1        ; byte: sc_stold describes the strip
    SCVAR sc_stkf, 1        ; byte: the CAPS/NUM bits the lamps show
    SCVAR sc_mink, 1        ; byte: the ink a dropped menu's runs letter in -
                            ; CBLACK, or CDGRAY when the item is dead
                            ; (SPEC.md 68.14). Decided at the same branch that
                            ; decides OSAPI_GFX_PEN's CF, because a package
                            ; cannot read the pen back, and it took the pad
                            ; byte that used to keep the words below even -
                            ; which sc_dink then needed back (sc_mpad)
    SCVAR sc_dink, 1        ; byte: the same answer for a DIALOG's controls,
                            ; banked by sc_dpen (SPEC.md 6.6.5)
    SCVAR sc_mpad, 1        ; byte: ...and the pad BACK, because two ink bytes
                            ; landed where one did and the words below have to
                            ; stay even again
    SCVAR sc_cbuf, 2        ; sc_btn12's one character and its NUL
    SCVAR sc_mrx1, 2        ; word } the open dropdown's rectangle, computed
    SCVAR sc_mry1, 2        ; word } once by sc_mgeo and read by painter, hit
    SCVAR sc_mrx2, 2        ; word } test, highlight and close repaint alike
    SCVAR sc_mry2, 2        ; word } (the fm_hit discipline)
    SCVAR sc_mabox, 8       ; 4 words: the gesture anchor - the bar title's
                            ; band or the combo's box (os88ui rect order)
    SCVAR sc_max,  2        ; word } where a combo's dropdown hangs: its
    SCVAR sc_may,  2        ; word } box's left edge and the strip's bottom
    SCVAR sc_abrect, 8      ; 4 words: the About box...
    SCVAR sc_abok, 8        ; 4 words: ...and its OK button
    SCVAR sc_lnv,  2        ; word } the status line's source: the caret's
    SCVAR sc_colv, 2        ; word } absolute line and 1-based column, kept
                            ; across redraws whose walk never stood on it
    SCVAR sc_rlxe, 2        ; word: the ruler scale's right end (digit loop)
    SCVAR sc_stbuf, SC_ST_CELLS+2 ; the status text being composed...
    SCVAR sc_stold, SC_ST_CELLS+2 ; ...and what the strip SHOWS - sc_stdiff
                            ; letters only the cells that differ
    SCVAR sc_win1, 16       ; the Window menu's live item: '1 ' + sc_name
    SCVAR sc_mbbuf, 58      ; the menu bar's truncated copy, narrow windows

; --- character formatting (SPEC.md 68.1/68.3) --------------------------------
    SCVAR sc_cseg,  2       ; word: the CHP claim's segment - one attribute
                            ; byte per character at [sc_cseg]:index, sized in
                            ; lockstep with [sc_dseg] by sc_resize. Claimed in
                            ; sc_entry, so it is never 0 while we live
    SCVAR sc_chp,   1       ; byte: the TYPING attributes - what the next
                            ; character wears. Re-read from the character
                            ; left of the caret on every caret move without
                            ; a selection (Word's rule, sc_chpsync)
    SCVAR sc_cattr, 1       ; byte: the walk's per-character attr scratch
    SCVAR sc_showall, 1     ; byte: Show-all - the pilcrow toggle
    SCVAR sc_rbold, 1       ; byte: the attr byte the ribbon's cells SHOW
                            ; (bit 7 = the pilcrow cell), delta-cached
    SCVAR sc_rbok,  1       ; byte: sc_rbold describes drawn cells at all
    SCVAR sc_runa,  1       ; byte: the run being drawn - its attribute
    SCVAR sc_hashid, 1      ; byte: HIDDEN has been applied somewhere this
                            ; document's life. Conservative and set-only
                            ; (cleared by sc_clamp): a hidden character
                            ; occupies no cell, so "column = index - row
                            ; start" stops holding, and the two paths that
                            ; lean on it - sc_append and the visual break -
                            ; refuse while this is up (SPEC.md 68.1)
    SCVAR sc_r0,    2       ; word } the run's first and last cell, for
    SCVAR sc_r1,    2       ; word } sc_drawrun and the italic stager
    SCVAR sc_iseg,  2       ; word: the sheared italic glyph table's claim,
                            ; 0 = not built yet (sc_itinit, first use)
    SCVAR sc_inwk,  1       ; byte: the WORKER is inside its lock-held draw
                            ; burst. sc_itinit reads it and refuses to claim
                            ; (SPEC.md 20.6 rule 7: no OSAPI_MEM_* from a
                            ; worker): a worker pass that meets italic text
                            ; before any UI draw built the table letters it
                            ; plain, and the next UI redraw builds it. Set
                            ; and cleared under the lock, so the UI's own
                            ; draw paths never see it raised
    SCVAR sc_itsg,  2       ; word } OSAPI_FONT_GLYPHS' table, banked across
    SCVAR sc_itso,  2       ; word } the build loop
    SCVAR sc_itch,  2       ; word: the glyph being built
    SCVAR sc_itstr, 2       ; word: the staged run's stride in bytes
    SCVAR sc_itmp,  8       ; one source glyph's rows, banked so the font
                            ; table and the claim are never open at once
    SCVAR sc_cuseg, 2       ; word: the undo arena's CHP twin (SPEC.md 68.3)
                            ; - same size, same offsets as [sc_useg]
    SCVAR sc_abuf, SC_MAXCOL + 1  ; the accumulated row's attributes, one per
                            ; cell beside sc_rbuf
    SCVAR sc_pattr, SC_MAXCOL     ; ...and the attributes the cached row was
                            ; last DRAWN with, diffed beside sc_prow
                            ; (the italic run's staged 4bpp image used to be
                            ; here: (SC_MAXCOL-1)*4*8 = 5,440 bytes of bss.
                            ; It lives at SC_STG4 inside the italic claim
                            ; now - see the constant for why)
    SCVAR sc_pseg,  2       ; word: the PAP dictionary's claim (256 x 4 bytes,
                            ; zeroed at entry so entry 0 IS Normal)
    SCVAR sc_papn,  2       ; word: entries live, 1..SC_PAPMAX
    SCVAR sc_pap_tail, 1    ; byte: the LAST paragraph's index (it has no ¶)
    SCVAR sc_hasfmt, 1      ; byte: some paragraph wears a non-Normal format.
                            ; Set-only until sc_clamp, like sc_hashid: while
                            ; clear, every walk runs the old uniform-8px path
                            ; with not one added memory reference
    SCVAR sc_hastab, 1      ; byte: a tab lives in the document - the two
                            ; column-arithmetic fast paths (sc_append, the
                            ; visual break) stand down, like sc_hashid
    SCVAR sc_parafirst, 1   ; byte: the row being entered is its paragraph's
                            ; FIRST row (walk state)
    SCVAR sc_wpap,  1       ; byte: the governing PAP index at the walk's pen
    SCVAR sc_walign, 1      ; byte } the decoded entry, clamped against the
    SCVAR sc_wadv,  1       ; byte } live geometry by sc_papload: alignment,
    SCVAR sc_wsb,   1       ; byte } row advance px (8/12/16), space-before
    SCVAR sc_wleftpx, 2     ; word } left indent px, first-line px (signed),
    SCVAR sc_wfirstpx, 2    ; word } and the effective right edge and fresh-
    SCVAR sc_wrgt,  2       ; word } row cell capacity the wrap rule reads
    SCVAR sc_wcols, 2       ; word } instead of [sc_rgt]/[sc_rcols]
    SCVAR sc_wcpx,  2       ; word: ...and that capacity in PIXELS, which is
                            ; what sc_wordfit's second threshold measures now.
                            ; Exactly 8 x [sc_wcols] in the kernel's cell, so
                            ; the wrap it computes there is the one it always
                            ; computed (SPEC.md 68.13)
    SCVAR sc_rowhv, 2       ; word: the entered row's height in px
    SCVAR sc_rowx0, 2       ; word: the row's start pen x (indent + alignment
                            ; offset) - the tab stops' anchor
    SCVAR sc_rbandt, 2      ; word: the row's BAND top (glyph y - (h-8)), the
                            ; hit query's lower edge - a click in the leading
                            ; gap of a spaced row belongs to that row
    SCVAR sc_currow, 2      ; word: the caret's VISIBLE row, signed, banked by
                            ; sc_ask beside sc_curx/sc_cury
    SCVAR sc_vfit,  2       ; word: rows guaranteed to fit the band - vrows
                            ; while uniform, band/24 when formatted (24px is
                            ; the tallest row: double spacing + open)
    SCVAR sc_ryb, SC_MAXROWS*2  ; each visible row's GLYPH y as drawn - the
                            ; heights array in prefix form: band top of row r
                            ; is ryb[r-1]+8 (r=0: sc_ty). Written beside
                            ; sc_rows/sc_sig, shifted with them by
                            ; sc_shiftrows, compared by pass 1 so a row whose
                            ; pixels MOVED is dirty even when its text did not
    SCVAR sc_sdpx,  2       ; word: sc_scrollpaint's pixel delta (signed),
                            ; sc_shiftrows adjusts the shifted ryb by it
    SCVAR sc_ymoved, 1      ; byte: pass 1 saw a row change its y - the band
                            ; repaint must ERASE [sc_ymv0..sc_ymv1] first
                            ; (glyph runs only self-erase at their own y)
    SCVAR sc_ympad, 1
    SCVAR sc_ymv0,  2       ; word } the union of old and new extents of the
    SCVAR sc_ymv1,  2       ; word } rows that moved
    SCVAR sc_pfb,   4       ; the 4-byte PAP candidate sc_papfind takes
    SCVAR sc_ppop,  1       ; byte } sc_modpap's operation and argument
    SCVAR sc_pparg, 1       ; byte } (arg is signed where the op says so)
    SCVAR sc_ppstop, 1      ; byte: the dictionary refused mid-span - stop
    SCVAR sc_pptl,  1       ; byte: the span reaches the TAIL paragraph
    SCVAR sc_entpap, 1      ; byte: the pap an Enter's new ¶ inherits
    SCVAR sc_rlold, 2       ; word: the ruler cells' pressed bits as drawn
                            ; (bit 0..3 align, 4..6 spacing, 7 close, 8 open)
    SCVAR sc_rlok,  1       ; byte: sc_rlold/marker caches describe the strip
    SCVAR sc_rmleft, 1      ; byte } the marker positions the scale shows:
    SCVAR sc_rmfirst, 1     ; byte } left, first (signed), right - in cells,
    SCVAR sc_rmright, 1     ; byte } delta-cached like the cells
    SCVAR sc_rgx,   2       ; word: the marker drag's XOR guide x, 0xFFFF none
    SCVAR sc_rll,   1       ; byte } sc_rlcalc's live answer: the caret
    SCVAR sc_rlf,   1       ; byte } paragraph's left / first (signed) /
    SCVAR sc_rlr,   1       ; byte } right indents, in cells
    SCVAR sc_rdl,   1       ; byte: the drag's banked left indent
    SCVAR sc_rdz,   2       ; word: ...and the scale's zero x
    SCVAR sc_dgfoc, 2       ; word: the focused SCD_EDIT record, 0 = none
    SCVAR sc_de_left, 8     ; the three edit buffers: 'From Left:' etc.,
    SCVAR sc_de_right, 8    ; NUL-terminated, 7 chars max ('-10.0"')
    SCVAR sc_de_first, 8
    SCVAR sc_de_bef, 8      ; 'Before:' - 0 or 1 (li)

; --- the modal dialog (SPEC.md 68.3): Format > Character... ------------------
    SCVAR sc_dlg,   2       ; word: the open dialog's descriptor, 0 = none
    SCVAR sc_dlgx,  2       ; word } its top-left corner on screen, banked
    SCVAR sc_dlgy,  2       ; word } at open - painter and hit test share it
    SCVAR sc_dlrect, 8      ; 4 words: the covered rect, for the close repair
    SCVAR sc_dck,   1       ; byte: the attr byte the check boxes are editing
    SCVAR sc_dpad,  1       ; byte: keeps the words below even
    SCVAR sc_dgr,   8       ; 4 words: a button rect being drawn/hit

; --- the real .DOC format (scdoc.inc, SPEC.md 68.4) --------------------------
    SCVAR sc_dgrp,  24      ; a grpprl under construction. Six paragraph
                            ; sprms at three bytes is 18; SC_GRPMAX is the
                            ; same number stated where the builder is
    SCVAR sc_dprop, 32      ; ...and the FKP property record built from it
                            ; (a PAPX adds a cw byte, an stc and a 6-byte PHE)
    SCVAR sc_dfkp,  512     ; the 512-byte FKP page being built. Built HERE
                            ; and copied into the image whole, because in
                            ; place it needs two pointers growing towards
                            ; each other and that arithmetic is the entire
                            ; risk of the structure (SPEC.md 68.4)
    SCVAR sc_dfkn,  2       ; word: its crun
    SCVAR sc_dfkhi, 2       ; word: how far DOWN its properties have got
    SCVAR sc_dfkb,  128     ; its rgb offset bytes, BANKED: rgb[i] lives at
                            ; 4*(crun+1) + i and crun is not final until the
                            ; page is sealed, so storing one as it is made
                            ; stores it where the next run moves it from
    SCVAR sc_dbtfc, 128     ; SC_MAXBTE words: each FKP page's first fc, for
                            ; the bin table written at the end
    SCVAR sc_dbtn,  2       ; word: how many of them
    SCVAR sc_dbn0,  2       ; word } the CHP and PAP page counts, kept apart
    SCVAR sc_dbn1,  2       ; word } because the second pass reuses the array
    SCVAR sc_dpn0,  2       ; word } ...and the page number each run starts at
    SCVAR sc_dpn1,  2       ; word }
    SCVAR sc_dend,  2       ; word: one past the run/paragraph just measured
    SCVAR sc_dvlen, 2       ; word: [sc_len] plus the trailing ¶ the FILE has
                            ; and this document model does not (SPEC.md 68.4)
    SCVAR sc_dfsize, 2      ; word: the size of the file being read
    SCVAR sc_dtrunc, 1      ; byte: it was bigger than this port can hold
    SCVAR sc_dtail, 1       ; byte: the tail paragraph's PAP index, banked
                            ; across sc_clamp - which clears the live one
    SCVAR sc_dpcn,  2       ; word: how many pieces the text is in (1 for a
                            ; simple file - SPEC.md 68.4.1)
    SCVAR sc_dpcp,  2       ; word } the plcpcd's CP array and PCD array,
    SCVAR sc_dpcd,  2       ; word } as offsets INTO the staging image
    SCVAR sc_dp1fc, 2       ; word: a simple file's one piece's fc
    SCVAR sc_dccp,  2       ; word: the document's total length in characters
    SCVAR sc_dprem, 2       ; word: characters left in the piece being painted
    SCVAR sc_dfclim, 2      ; word: the limit fc of the run sc_dfkfind found
    SCVAR sc_dprop2, 2      ; word: ...and its property record's file offset

; --- RTF in and out (scrtf.inc, SPEC.md 68.8) --------------------------------
    SCVAR sc_rfull, 1       ; byte: the writer ran out of staging claim
    SCVAR sc_rat,   1       ; byte: the attributes the writer has OPEN, or
                            ; the ones the reader is applying
    SCVAR sc_rnew,  1       ; byte: a paragraph is about to start
    SCVAR sc_rpad,  1       ; byte: keeps the words below even
    SCVAR sc_rdep,  2       ; word: the reader's group depth
    SCVAR sc_rskip, 2       ; word: the depth a skipped destination began at,
                            ; 0 = not skipping (SPEC.md 68.8)
    SCVAR sc_rstk,  32      ; SC_RTFDEPTH bytes of saved attributes, one per
                            ; open group, so a \b's scope ends with its group
                            ; (padded to a word each, which keeps the indexing
                            ; a single shift)
    SCVAR sc_rcw,   20      ; the control word being read, NUL-terminated
    SCVAR sc_fpw,   94      ; the COMPILED search pattern (SPEC.md 68.7),
                            ; one word per element: a value 0..255 is that
                            ; character, SCP_ANY and SCP_WHITE the wildcards
    SCVAR sc_fpwn,  2       ; word: how many elements it has
    SCVAR sc_frx,   SC_FRXMAX   ; the EXPANDED replacement: ^m and ^c are
                            ; resolved per match, so the bytes spliced in are
                            ; never the ones the dialog holds
    SCVAR sc_frxn,  2       ; word: how many of them
; --- the Utilities commands (scutil.inc, SPEC.md 68.9) -----------------------
    SCVAR sc_us0,   2       ; word } the span Sort / Renumber / Table of
    SCVAR sc_us1,   2       ; word } Contents operate on, snapped out to
                            ; whole paragraphs
    SCVAR sc_upst,  258     ; SC_UMAXP+1 words: each paragraph's start, plus
                            ; a sentinel at [sc_us1] - so a paragraph's
                            ; length is the NEXT entry and no second array
                            ; exists to disagree with this one
    SCVAR sc_upn,   2       ; word: how many paragraphs are in it
    SCVAR sc_uord,  256     ; SC_UMAXP words: the sort's order array
    SCVAR sc_scseg, 2       ; word: the rebuild scratch claim, 0 = none
    SCVAR sc_scn,   2       ; word: how many characters are in it
    SCVAR sc_vpage, 1       ; byte: 0 = Draft, 1 = Page (SPEC.md 68.11)
    SCVAR sc_vppad, 1       ; byte: keeps the words below even
    SCVAR sc_shl,   2       ; word } the sheet's left and right pixel edges,
    SCVAR sc_shr,   2       ; word } banked by sc_bounds for the painters
    SCVAR sc_ovseg, 2       ; word: SCRIBE.OVL's claim, 0 = not loaded yet
    SCVAR sc_ovmsg, 2       ; word: a message the MODULE wants said, 0 = none.
                            ; The module never touches the UI; sc_ovcall says
                            ; this on the way back out (SPEC.md 94.8.1)
    SCVAR sc_ovfar, 4       ; the (offset, segment) sc_ovcall far-calls: an
                            ; 8086 has no `call far reg:reg`, so the pointer
                            ; lives in memory and DS reaches it
    SCVAR sc_ovdir, 2       ; word } the folder this package was LAUNCHED
    SCVAR sc_ovdrv, 1       ; byte } from, banked at entry (SPEC.md 68.10)
    SCVAR sc_ovwdr, 1       ; byte } ...and where the USER had the volume,
    SCVAR sc_ovwas, 2       ; word } banked across the load and put back
    SCVAR sc_utl,   1       ; byte: the span runs to the document's end with
                            ; no closing ¶, so the rebuilt one must not have
                            ; one either (SPEC.md 68.9)
    SCVAR sc_sopt,  1       ; byte: the Sort dialog's options, SCSO_*
    SCVAR sc_sofld, 2       ; word: its Field number, 1-based
    SCVAR sc_sokb,  2       ; word } the second key of a comparison, banked
    SCVAR sc_soke,  2       ; word } because sc_sonum walks SI..DI
    SCVAR sc_rnact, 1       ; byte: Renumber's All / Numbered Only / Remove
    SCVAR sc_rnpad, 1       ; byte: keeps the words below even
    SCVAR sc_rnfrom, 2      ; word: the Start at value
    SCVAR sc_rnnow, 2       ; word: ...and the running number
    SCVAR sc_rnfmt, 12      ; the Format edit's text, NUL-terminated
    SCVAR sc_tcsrc, 1       ; byte: TOC source, 0 = headings / 1 = fields
    SCVAR sc_tcpst, 1       ; byte: the walk is standing on a paragraph start
    SCVAR sc_tcfrom, 2      ; word } the level range From..To
    SCVAR sc_tcto,  2       ; word }
    SCVAR sc_tcn,   2       ; word: entries collected
    SCVAR sc_tcline, 2      ; word } the collector's own row counter and the
    SCVAR sc_tccol, 2       ; word } column inside the row it is on
    SCVAR sc_de_fld, 8      ; the Utilities dialogs' numeric edit buffers
    SCVAR sc_de_start, 8
    SCVAR sc_de_from, 8
    SCVAR sc_de_to,  8
    SCVAR sc_rnman, 1       ; byte: Renumber's Automatic / Manual
    SCVAR sc_tclvl, 1       ; byte: the TOC's All / From..To

    SCVAR sc_rpp,   4       ; the PAP entry the writer is emitting, banked
                            ; because SI is the string pointer every emit
                            ; needs and the entry has to outlive them

%ifdef SCBENCH
; --- the walk bench (tests/scbench.inc), in the -DWDBENCH build only ---------
    SCVAR wdb_buf, 640      ; the report, composed here and then copied into
                            ; the document claim: sc_utoa writes through DS
    SCVAR wdb_ls,  2        ; word: where the line being composed began
    SCVAR wdb_t,   2        ; word } the row being emitted: ticks over
    SCVAR wdb_i,   2        ; word } iterations

%endif

; --- Insert > Picture (SPEC.md 94.9) ----------------------------------------
; The block and the row buffer os88img.inc works through. They are HERE and
; not in a claim because that include reaches both through DS, which stays the
; package's segment even while the decoder itself is running out in SCRIBE.OVL
; (68.10) - so they must be in the package, and bss is where the package's
; memory is.
    SCVAR sc_namebank, SC_NAMEMAX + 1
                            ; the document's name across an Insert > Picture,
                            ; which borrows the file dialog and would
                            ; otherwise be renamed by it
    SCVAR sc_pictwant, 1    ; byte: the file dialog is being opened FOR a
                            ; picture, so sc_ondlg routes its answer here
                            ; instead of to open-or-save
    SCVAR sc_imgblk, OS88IMG_SZ
    SCVAR sc_imgrow, OS88IMG_ROW
    SCVAR sc_picseg, 2      ; word: the decode scratch claim, transient
    SCVAR sc_npic,   2      ; word: pictures this document holds
    SCVAR sc_pictab, SC_PICMAX * SC_PICREC
    SCVAR sc_picnew, 2      ; word: the claim sc_pictkeep is filling
    SCVAR sc_chpbank, 1     ; byte: [sc_chp] across the picture insert
    SCVAR sc_rowpic, 2      ; word: the picture the row being built IS, or
                            ; 0xFFFF. Set at row entry by sc_rowhc and read at
                            ; row exit by sc_rflush - the same lifetime
                            ; [sc_rby] and [sc_rowx0] already have
    SCVAR sc_rpw,   2       ; word } the picture being emitted into RTF:
    SCVAR sc_rph,   2       ; word } width, height, stride and the segment
    SCVAR sc_rps,   2       ; word } its pixels are in. Banked out of the
    SCVAR sc_rpg,   2       ; word } table because ES belongs to the staging
                            ; claim for the whole of sc_rpict and the picture
                            ; lives in a third segment (88.5.1)
    SCVAR sc_rpcol, 2       ; word: hex bytes on the line so far, so the
                            ; output wraps instead of being one enormous line
    SCVAR sc_dpicrun, 1     ; byte: the attribute run sc_dattr just found is
                            ; a picture, so its CHPX is the fixed structure
                            ; and not a sprm grpprl (SPEC.md 94.7)
    SCVAR sc_dpicfc, SC_PICMAX * 2
                            ; word each: where each picture's PICF landed,
                            ; filled before the CHPX that names it is built
    SCVAR sc_dpicn,  2      ; word } the picture being written, and the four
    SCVAR sc_dpicwd, 2      ; word } table fields banked out of sc_pictab so
    SCVAR sc_dpicht, 2      ; word } nothing has to hold a register on it
    SCVAR sc_dpicst, 2      ; word } while DS is the picture's own segment
    SCVAR sc_dpicsg, 2      ; word }
    SCVAR sc_dpicbw, 2      ; word: bmWidthBytes, the 1bpp destination row
    SCVAR sc_dpiclcb, 2     ; word: the whole record, 46 + bw * height
    SCVAR sc_dpicx,  2      ; word } where the reduction has got to
    SCVAR sc_dpicy,  2      ; word }
    SCVAR sc_dpicbc, 2      ; word: bytes emitted on this row, so it pads
    SCVAR sc_dpicac, 1      ; byte: the part-built output byte...
    SCVAR sc_dpicnb, 1      ; byte: ...and how many bits are in it
    SCVAR sc_dpicp,  2      ; word: the READER's cursor through the PICF
                            ; records, which are in document order (88.7)
    SCVAR sc_rpin,  2       ; word: the depth a \pict opened at, 0 = none.
                            ; Shaped exactly like sc_rskip, which is the
                            ; state it replaced for this one destination
    SCVAR sc_rpseg, 2       ; word } the picture being COLLECTED: its claim,
    SCVAR sc_rpoff, 2       ; word } how far in, and how big the header said
    SCVAR sc_rpcap, 2       ; word } it would be
    SCVAR sc_rpnib, 1       ; byte: a high nibble is held, waiting for its
    SCVAR sc_rpacc, 1       ; byte: ...and this is it
    SCVAR sc_rppl,  2       ; word } \wbmplanes, \wbmbitspixel,
    SCVAR sc_rpbp,  2       ; word } \wbmwidthbytes, \picw and \pich, as the
    SCVAR sc_rpwb,  2       ; word } group declares them. RTF's own defaults
    SCVAR sc_rpiw,  2       ; word } are 1 plane of 1-bit pixels, so a group
    SCVAR sc_rpih,  2       ; word } that says nothing is refused, not guessed
    SCVAR sc_pbuf,  32      ; the report. NOT sc_tbuf, which is 26 bytes and
                            ; sized for 'Loaded ' plus an 8.3 name. 32 is
                            ; TOAST_MAX (24) with room to compose past it and
                            ; let the kernel do the cutting

%assign SC_BSS_TOTAL SCB

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_SCROLL           ; SPEC.md 13.10: the shared scroll bar. This
                                ; app had the SEVENTH private implementation
                                ; of it (13.10.6), and its own header said so
%include "os88ui.inc"
%include "os88type.inc"         ; SPEC.md 6.5: proportional type, and the band
                                ; it is composed into. AFTER os88ui.inc for no
                                ; reason but tidiness - it depends on nothing
                                ; here and nothing here depends on it until
                                ; [sc_prop] is set

    OS88_BSS SC_BSS_TOTAL + TY_BSS_SIZE
    OS88_IMAGE_END

    TY_BSS os88_image_end + SC_BSS_TOTAL

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = a fresh empty note with the caret at the origin and no toast.
sc_len      equ os88_image_end + 0     ; word: characters used. The TEXT is
                                       ; not here any more - it is [sc_dseg]
                                       ; below, a heap claim (SPEC.md 27.6)
sc_tx       equ os88_image_end + 2   ; word: paint scratch, text origin x
sc_rgt      equ os88_image_end + 4   ; word: content right, inclusive
sc_bot      equ os88_image_end + 6   ; word: content bottom, inclusive
                                       ; +8..+17 FREE: [sc_msg] and the four
                                       ; toast-box words. The toast is the
                                       ; kernel's now (SPEC.md 59) and is in
                                       ; the menu bar, so this app holds no
                                       ; state about it at all. The offsets
                                       ; below are unchanged deliberately -
                                       ; renumbering 200 hand-written equs to
                                       ; reclaim ten bytes of bss is a large
                                       ; risk for no measurable gain
sc_name     equ os88_image_end + 18   ; 14: the current document, 8.3 + NUL
                                       ; (SPEC.md 27.1) - per INSTANCE, so
                                       ; two Note Pads hold two documents
sc_tbuf     equ os88_image_end + 32   ; 26: 'Saved ' / 'Loaded ' + sc_name
sc_cur      equ os88_image_end + 62    ; word: THE CARET - the
                                       ; character index it sits in front of,
                                       ; 0..[sc_len]. Everything below exists
                                       ; to move it or to answer where it is
sc_ty       equ os88_image_end + 64    ; word: the first text row
sc_i        equ os88_image_end + 66    ; word: sc_walk's index
sc_curx     equ os88_image_end + 68    ; word: the caret in pixels
sc_cury     equ os88_image_end + 70
sc_hitx     equ os88_image_end + 72    ; word: a click to resolve,
sc_hity     equ os88_image_end + 74    ; 0xFFFF in y = no query
sc_hiti     equ os88_image_end + 76    ; word: ...and its answer
sc_wantx    equ os88_image_end + 78    ; word: a row and column to
sc_wanty    equ os88_image_end + 80    ; find, 0xFFFF = no query
sc_wanti    equ os88_image_end + 82    ; word: ...and its answer,
                                       ; 0xFFFF = there is no such row
sc_draw     equ os88_image_end + 84    ; byte: sc_walk paints
sc_hitset   equ os88_image_end + 85    ; byte: the click row was
sc_wantset  equ os88_image_end + 86    ; byte: ...the target row
sc_pad2     equ os88_image_end + 87    ; byte: keeps the total even
sc_dir      equ os88_image_end + 58    ; word: the folder the
sc_drv      equ os88_image_end + 60    ; document lives in, byte:
sc_dirok    equ os88_image_end + 61    ; its drive, byte: whether
                                       ; the pair has been recorded at all.
                                       ; A file name resolves in the VOLUME's
                                       ; current directory - one global every
                                       ; Disk window and the file dialog
                                       ; share - so 'Save' has to put the
                                       ; volume back where 'Save As' left it,
                                       ; or it writes into whatever folder
                                       ; something else navigated to since

; --- the row signatures (SPEC.md 27.2) ---------------------------------------
; All zero is a note whose every visible row is empty, which is what a fresh
; instance has - but nothing reads them until sc_paint has written them,
; because sc_sigok below is 0 until it does.
sc_row      equ os88_image_end + 88    ; word: sc_walk's visible row
sc_rowh     equ os88_image_end + 90    ; word: its running fold
sc_vrows    equ os88_image_end + 92    ; word: rows the content
                                       ; shows, capped at SC_MAXROWS
sc_dr0      equ os88_image_end + 94    ; word: first dirty row
sc_dr1      equ os88_image_end + 96    ; word: ...and the last.
                                       ; sc_dr0 = 0xFFFF means none at all
sc_stx      equ os88_image_end + 98    ; word } the geometry the
sc_sty      equ os88_image_end + 100    ; word } signatures were
sc_srgt     equ os88_image_end + 102    ; word } taken at, and the
sc_sbot     equ os88_image_end + 104    ; word } taken at (sc_sigsame)
                                       ; +106 FREE: sc_smsg
sc_sigup    equ os88_image_end + 108    ; byte: sc_walk folds and
                                       ; compares
sc_clip     equ os88_image_end + 109    ; byte: ...and draws only
                                       ; the dirty band
sc_sigok    equ os88_image_end + 110    ; byte: sc_sig has been
                                       ; written at least once
sc_pad3     equ os88_image_end + 111    ; byte: keeps sc_sig even
sc_sig      equ os88_image_end + 112    ; SC_MAXROWS words: one
                                       ; per row of the content
sc_rcols    equ os88_image_end + 232    ; word: cells the band holds
sc_rby      equ os88_image_end + 234    ; word: y of the row being
                                       ; accumulated - BP has moved on by the
                                       ; time it is flushed
sc_rcx      equ os88_image_end + 236    ; word: the caret's x on that
                                       ; row, 0xFFFF = it is not on this one
                                       ; sc_rbuf and sc_prow USED TO BE HERE,
                                       ; at +238 and +330, and they are in the
                                       ; SCVAR block at the foot of this file
                                       ; now - because they are the only two
                                       ; fields sized by SC_MAXCOL, and that
                                       ; constant had to grow 91 -> 171 for a
                                       ; window straddling a display seam.
                                       ; Every offset in THIS block is written
                                       ; down by hand, so growing a field in
                                       ; the middle of it means renumbering
                                       ; thirty-five of them and getting all
                                       ; thirty-five right; the counter block
                                       ; sizes itself. The 183 bytes they left
                                       ; are a hole, and reclaimable by any
                                       ; field that wants them.
sc_prowi    equ os88_image_end + 421    ; word: which row that is,
                                       ; 0xFFFF = the cache holds nothing
sc_prcc     equ os88_image_end + 423    ; word: and where its caret
                                       ; was, so the cell it vacates is redrawn
sc_flo      equ os88_image_end + 425    ; word } sc_rflush's span,
sc_fhi      equ os88_image_end + 427    ; word } 0xFFFF = empty
sc_fcc      equ os88_image_end + 429    ; word: the caret's column
                                       ; on the row being flushed

; --- the document, and the heap it lives in (SPEC.md 27.6/50.3) ----------------
; The text itself is NOT in this package's region. sc_entry claims SC_KB0 for
; it before it creates the window, sc_room grows it a kilobyte at a time as
; the note fills, a load sizes it to the file and File > New gives it back.
; That is what an editor's buffer is: data whose size only the user knows,
; and a region is image + bss capped at APP_MAX_SIZE (SPEC.md 20.1).
sc_dseg     equ os88_image_end + 433    ; word: the document claim's segment.
                                       ; The text is [sc_dseg]:0000, and it
                                       ; is NEVER 0 while this instance lives:
                                       ; sc_entry aborts the launch rather
                                       ; than open a window with nowhere to
                                       ; put the text, so nothing below has
                                       ; to test it
sc_capkb    equ os88_image_end + 435    ; word: its size in KB...
sc_cap      equ os88_image_end + 437    ; word: ...and in bytes, kept in step
                                       ; by sc_resize. SC_MAXKB * 1024 fits a
                                       ; word, which is what bounds the note
                                       ; +441..+444 FREE: sc_msgn and
                                       ; sc_smsgn, the toast's GENERATION and
                                       ; the one the signatures were taken
                                       ; over. Both existed because every
                                       ; toast was composed into the same
                                       ; sc_tbuf, so the POINTER could not
                                       ; tell "Saved X" from "Loaded X" -
                                       ; a distinction the kernel's copy
                                       ; makes for itself (SPEC.md 59.3)
sc_stgseg   equ os88_image_end + 439    ; word: the file staging claim, 0 =
                                       ; not held. A SECOND claim, transient:
                                       ; a save assembles the .DOC image in
                                       ; it (FIB + text + CHP + PAP, SPEC.md
                                       ; 65.4) and a load reads the whole
                                       ; file into it before the magic is
                                       ; sniffed - the document is untouched
                                       ; until the file proves valid
; --- the layout checkpoint (SPEC.md 27.4) ------------------------------------
; sc_walk is O(the note), and it runs TWICE per keystroke - which is what a
; user found by filling a fullscreen window and watching each keystroke get
; slower while the delta cache above kept the drawing at two cells. Wrapping
; is a left-to-right automaton with no lookahead, so the pen state at index k
; depends only on the characters before it: an edit at the caret cannot
; change the layout of anything ahead of the caret. So the walk may RESUME at
; the start of the caret's row instead of starting at index 0, and the start
; of a row is (index, row) alone - the pen's x is always [sc_tx] there and
; its y is always [sc_ty] + 8*row.
sc_ckpi     equ os88_image_end + 445    ; word } the checkpoint: the
sc_ckpr     equ os88_image_end + 447    ; word } caret's row start
sc_ckpc     equ os88_image_end + 449    ; word } and the candidate
sc_ckpcr    equ os88_image_end + 451    ; word } sc_rstart banks
sc_win      equ os88_image_end + 453    ; word: our window, which a
                                       ; callback is handed but the worker
                                       ; has to remember
sc_bcrow    equ os88_image_end + 455    ; word: the row the visual
                                       ; break sits on (SPEC.md 27.3)
sc_borig    equ os88_image_end + 457    ; word: ...and the row it
                                       ; started on, which is the first row
                                       ; the reconcile has to repaint
sc_ktick    equ os88_image_end + 459    ; word: the tick of the last
                                       ; keystroke, for the idle settle
sc_ckok     equ os88_image_end + 461    ; byte: the checkpoint
                                       ; describes THIS layout
sc_fast     equ os88_image_end + 462    ; byte: this redraw is a
                                       ; plain insert or backspace at the
                                       ; caret, inside the caret's own row.
                                       ; One-shot: sc_redraw consumes it
sc_resume   equ os88_image_end + 463    ; byte: sc_walk starts at
                                       ; the checkpoint, not at index 0
sc_bmode    equ os88_image_end + 464    ; byte: the visual break is
                                       ; up, so the screen is NOT the note
sc_clean    equ os88_image_end + 465    ; byte: the band is known
                                       ; blank, so a row's run needs no
                                       ; trailing padding to erase with
sc_brkok    equ os88_image_end + 466    ; byte: this machine is slow
                                       ; enough to want the break at all
sc_hired    equ os88_image_end + 467    ; byte: the worker exists
sc_didpush  equ os88_image_end + 468    ; byte: a push scrolled the
                                       ; band, so the grow box needs redrawing
sc_bfail    equ os88_image_end + 469    ; byte: a push was REFUSED -
                                       ; the band left below the caret is
                                       ; shorter than a row - so the break
                                       ; cannot continue and must settle
sc_bstop    equ os88_image_end + 470    ; byte: THIS walk ends at
                                       ; the caret. Its own flag and not
                                       ; "[sc_bmode] is set", because
                                       ; sc_measure answers clicks and arrow
                                       ; keys and has to see the whole note
                                       ; even while the break is up
sc_rowsok   equ os88_image_end + 471    ; byte: sc_rows describes
                                       ; this layout

; --- where each row starts (SPEC.md 27.5) ------------------------------------
; The checkpoint above is the caret's row start; this is EVERY row's, which is
; what turns a query about an arbitrary row into a bounded walk. A click, an
; Up and a Home all name a row and want the index at a column of it, and
; without this each of them re-derived the whole layout from index 0 to answer
; a question about thirty characters.
sc_rows     equ os88_image_end + 472    ; SC_MAXROWS words
sc_rowsn    equ os88_image_end + 472 + SC_MAXROWS*2   ; word: how
                                       ; many of them the last full walk wrote
sc_sdi      equ os88_image_end + 474 + SC_MAXROWS*2   ; word } the
sc_sdr      equ os88_image_end + 476 + SC_MAXROWS*2   ; word } seed
sc_lastrow  equ os88_image_end + 478 + SC_MAXROWS*2   ; word: the
                                       ; last row this walk cares about. ONE-
                                       ; SHOT and reset to 0xFFFF by sc_walk,
                                       ; so a caller that forgets gets the
                                       ; whole note - slow, never wrong
sc_ecol     equ os88_image_end + 480 + SC_MAXROWS*2   ; word: the
                                       ; caret's column on its row BEFORE this
                                       ; edit - reported by the key handler,
                                       ; never derived (SPEC.md 27.3)
sc_ekind    equ os88_image_end + 482 + SC_MAXROWS*2   ; byte: what
                                       ; THIS redraw is for - sc_redraw's
                                       ; one-shot copy of [sc_fast]'s kind
sc_eext     equ os88_image_end + 483 + SC_MAXROWS*2   ; byte: cells
                                       ; of the row below that go stale beyond
                                       ; the caret's column (Delete: 1)

; --- scrolling (SPEC.md 27.7) ------------------------------------------------
sc_top      equ os88_image_end + 484 + SC_MAXROWS*2   ; word: the note row
                                       ; drawn at the top of the view. sc_row
                                       ; is the VISIBLE row and starts at
                                       ; MINUS this
sc_drows    equ os88_image_end + 486 + SC_MAXROWS*2   ; word: how many rows
                                       ; the note occupies, from the last walk
                                       ; that ran to its natural end
sc_sbr      equ os88_image_end + 488 + SC_MAXROWS*2   ; word: the content's
                                       ; own right edge - sc_rgt stops
                                       ; SC_SB_W short of it now
sc_sbb      equ os88_image_end + 490 + SC_MAXROWS*2   ; word: and the bar's own
                                       ; bottom, sc_bot less the grow box
sc_sbtop    equ os88_image_end + 492 + SC_MAXROWS*2   ; word } what the bar on
sc_sbrows   equ os88_image_end + 494 + SC_MAXROWS*2   ; word } screen was drawn
                                       ; from, so a redraw that moved neither
                                       ; leaves it alone
sc_follow   equ os88_image_end + 496 + SC_MAXROWS*2   ; byte: this redraw is
                                       ; one the CARET moved in, so the view
                                       ; owes it a place on screen. Its own
                                       ; flag and not "[sc_ekind] is set":
                                       ; that one says which cheap redraw path
                                       ; is allowed, and Enter, Up, Down, Home
                                       ; and End are all 0 there while all
                                       ; five move the caret. One-shot, so a
                                       ; scroll bar click - which reaches the
                                       ; same sc_redraw - cannot be dragged
                                       ; straight back to the caret
sc_ptop     equ os88_image_end + 500 + SC_MAXROWS*2   ; word: the [sc_top]
                                       ; the PIXELS on screen were drawn for.
                                       ; sc_sig and sc_rows are indexed by a
                                       ; visible row, so this is what says
                                       ; which view they describe - and a
                                       ; scroll is reconciled by shifting all
                                       ; three together (SPEC.md 27.7.2)
sc_sdlt     equ os88_image_end + 502 + SC_MAXROWS*2   ; word } sc_scrollpaint
sc_bd0      equ os88_image_end + 504 + SC_MAXROWS*2   ; word } scratch: the
sc_bd1      equ os88_image_end + 506 + SC_MAXROWS*2   ; word } delta, the band
sc_hdirty   equ os88_image_end + 498 + SC_MAXROWS*2   ; byte: the note
                                       ; changed, so [sc_drows] is a lower
                                       ; bound rather than the height. Set by
                                       ; sc_ins/sc_del/sc_clamp, cleared by
                                       ; any walk that reached the note's end
sc_curseen  equ os88_image_end + 499 + SC_MAXROWS*2   ; byte: THIS walk stood
                                       ; on the caret, so [sc_cury] is its
                                       ; position and not the initial 0. A
                                       ; bounded walk can stop above it
sc_gchg     equ os88_image_end + 497 + SC_MAXROWS*2   ; byte: the geometry
                                       ; changed since the last paint, so the
                                       ; row count did too and [sc_top] has
                                       ; not been clamped against it yet. Set
                                       ; by sc_bounds, spent by sc_paint,
                                       ; cleared by sc_sigmark
                                       ; total 974 = SC_BSS_TOTAL
