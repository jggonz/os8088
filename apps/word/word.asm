; =============================================================================
; os8088 - apps/word/word.asm
;
; MICROSOFT WORD (SPEC.md 65) - a faithful native reimplementation of Word
; for Windows 1.1a ("Opus") as an os8088 package. The reasoning and feature
; inventory are docs/WORD-PLAN.md; SPEC.md 65 is the binding contract.
;
; The text engine is Note Pad's (SPEC.md 27), transplanted wholesale with
; prefix wd_: the one-walk/many-queries layout pass, row signatures and
; damage ranges, the space-padded row buffer flushed as one opaque font_run,
; blit scrolling with the row description shifted alongside, the decimating
; checkpoint index, the append fast path, XOR selection, drag-and-drop, the
; 5-deep undo, find/replace with its docked panel, and the lazy worker that
; pays the wrap/height debts. The engine comments below still cite SPEC.md
; 27 - deliberately: they describe the transplanted machinery, and 27 is
; where that machinery is specified. SPEC.md 65.6 keeps the architecture
; whole by reference.
;
; On top of the transplant, the AUTHENTIC in-window chrome (SPEC.md 65.2):
; the nine-title menu bar with Opus's verbatim menus (dropdowns, separators,
; right-justified shortcut captions, check marks, mnemonic underlines,
; disabled-pen greying, press-drag-release AND click-open interaction,
; Alt+mnemonic and arrow/Enter keyboard driving), the ribbon (Font:/Pts:
; combos + the B I K | U W D | script | pilcrow toggles, inert until the
; formatting stage), the ruler (Style: combo, alignment/spacing/tab cells,
; the inch scale with indent markers), and the status line (Pg/Sec/At/Ln/Col
; live from the caret, CAPS/NUM lamps from the BIOS lock flags), all four
; strips toggling from the View menu. wd_bounds reserves the strips inside
; the content; the find panel docks BELOW the ruler (wd_bounds and wd_fpgeom
; both add the live [wd_ctop]). No kernel menus - the kernel bar carries only
; the app-name pull-down with About. The buffer ceiling is WD_MAXKB = 30
; (the CRLF staging arithmetic stays 16-bit exact - see the constant).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'WORD', wd_entry, 3    ; bit 0 icon, bit 1 the DOC
                                       ; association block (SPEC.md 65.4)

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
;   ..#.#.....#.#...   .#############..
;   ..#.#.....#.#...   .#############..
;   ..#.#.....#.#...   .#############..
;   ..#.#..#..#.#...   .#############..
;   ..#.#..#..#.#...   .#############..
;   ..#.#.#.#.#.#...   .#############..
;   ..#.##...##.#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..###########...   .#############..
;   ................   .#############..
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
    dw 0x3FF8
    dw 0x2008
    dw 0x2008
    dw 0x2828
    dw 0x2828
    dw 0x2828
    dw 0x2928
    dw 0x2928
    dw 0x2AA8
    dw 0x2C68
    dw 0x2008
    dw 0x2008
    dw 0x2008
    dw 0x3FF8
    dw 0x0000
    OS88_ICON16_END

; --- the association block (SPEC.md 54/65.4): double-clicking a .DOC on the
; desktop launches Word with the document in OSAPI_ARG_FILE. Declared in the
; header (flags bit 1) so the mount's icon harvest reads it for free.
    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'DOC'
    OS88_ASSOC16_END

; --- the document lives in the HEAP, not in this package's bss (SPEC.md 27.6)
; A note is data, and data of a size only the user knows; a package's region
; is image + bss capped at APP_MAX_SIZE, so anything sized by the user belongs
; in a claim (SPEC.md 50.3). The claim starts at WD_KB0, grows a kilobyte at a
; time as the note fills it, is sized to the file on a load, and shrinks back
; on File > New. [wd_dseg]:0000 is the text and [wd_cap] its capacity.
WD_KB0       equ 1              ; the document claim at launch, KB
WD_GROWKB    equ 1              ; ...and the quantum it grows by
WD_MAXKB     equ 30             ; ...and its ceiling (SPEC.md 65.3). Note
                                ; Pad's was 16, and what bounds it is the
                                ; SAVE arithmetic, not memory: a save expands
                                ; every 13 to CR LF, and its staging pass
                                ; walks that with a 16-bit DI and counts it
                                ; with a 16-bit BX. At 30KB the worst case is
                                ; 2 x 30,720 = 61,440 and both hold exactly
                                ; (so does wd_stghold's 2 x [wd_len] + 1023 =
                                ; 62,463, and WD_MAXKB * 1024 = 30,720 still
                                ; fits the [wd_cap] word); at 32KB all three
                                ; wrap to zero. So 30 is the last whole
                                ; kilobyte the 16-bit staging walk can carry
                                ; - going further means teaching that loop to
                                ; cross a segment and making its count 32-bit
WD_STGMIN    equ 1              ; the save's transient staging claim, KB
WD_MAXCOL    equ 341            ; cells a row can hold, plus one for the NUL.
                                ; A row is accumulated into a buffer and drawn
                                ; as ONE opaque font_run (SPEC.md 6.1/27.2) -
                                ; or, in a proportional face, as one composed
                                ; band (SPEC.md 6.3/65.13)
                                ;
                                ; 1360/4 AND NOT 1360/8 since SPEC.md 65.13:
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
                                ; clamp wd_rflush DROPS the cell - the guard
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
%define WD_MAXROWS 60           ; signature slots, one per row the content can
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
                                ; own band needs is 59. wd_bounds clamps to
                                ; this, so a taller screen degrades to "the
                                ; rows past 60 are always redrawn" rather than
                                ; writing past the array
                                ; (WD_BSS_TOTAL lives at the foot of this file
                                ; now, with the fields it counts)
WD_BRK_CELLS equ 60             ; the visual break's trigger (SPEC.md 27.3):
                                ; the CELLS a keystroke would repaint BELOW
                                ; the caret's row. Not rows - this window is
                                ; resizable and a row is 30 cells or 90
                                ; depending how wide the user dragged it, so
                                ; a row count means two different amounts of
                                ; work. A cell is ~0.9ms on a 4.77MHz 8088 -
                                ; measured four ways, PERFORMANCE.md Part 2 -
                                ; so 60 cells is ~54ms, which is the point
                                ; where a keystroke stops keeping up
WD_IDLE      equ 9              ; ticks of no typing before the break is
                                ; reconciled: ~500ms at 18.2Hz
WD_WTICKS    equ 3              ; ...and how often the worker looks, ~165ms.
                                ; Finer than WD_IDLE so the settle lands
                                ; near the deadline rather than a tick late
WD_HCHUNK    equ 4              ; rows of the height count per worker pass
                                ; (SPEC.md 27.7.3). The count is the one walk
                                ; that cannot be bounded by the view, so it is
                                ; bounded by TIME instead: this many rows, then
                                ; the lock goes back.
                                ;
                                ; It sizes TWO things and the second is what
                                ; set it. The lock HOLD is what a UI action
                                ; waits behind, and the DUTY CYCLE is what the
                                ; count costs the rest of the machine: the
                                ; worker sleeps WD_WTICKS between passes, so
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
WD_SB_W      equ 14             ; scroll bar width, the Disk window's
                                ; (SPEC.md 22) so the two look like one OS.
                                ; It is reserved ALWAYS, present or not:
                                ; whether a bar is NEEDED depends on the row
                                ; count, which depends on the wrap width,
                                ; which would depend on the bar
WD_SB_ARR    equ 11             ; ...and the arrow cells at each end of it
WD_SB_STEP   equ 4              ; rows an arrow cell steps. The Disk window
                                ; steps one, but its rows are 16px list
                                ; entries and these are 8px lines of prose:
                                ; four of them is about the same travel, and
                                ; a blit-scrolled band costs the same whether
                                ; it moves one row or four (SPEC.md 27.7.2)
WD_GROW      equ 13             ; the grow box the kernel draws in the
                                ; content's bottom-right corner (SPEC.md
                                ; 11.1). The bar stops above it, exactly as
                                ; the Disk window's stops above its status
                                ; line - drawn into, the two overlap and the
                                ; down arrow comes out as a square
; --- the chrome strips (SPEC.md 65.2) ----------------------------------------
; Word's chrome is the WINDOW's, not the kernel's: MENU_APPMAX is five and
; Word's bar is nine menus, so wd_bounds reserves four strips inside the
; content and the text band is what remains. THIS STAGE paints each strip as
; a plain white band with a 1px black rule at its inner edge - the strip
; heights are the real ones, so the next stage draws chrome into geometry
; that is already load-bearing. The find panel docks BELOW the ruler:
; wd_bounds and wd_fpgeom both offset by WD_CHROME_TOP, and wd_panmove's
; open/close blit starts there too, so the chrome never rides a text blit.
;
; WD_CHROME_TOP is 48, A MULTIPLE OF 8 ON PURPOSE: the content origin is
; snapped to a byte column AND a byte-friendly y (OSAPI_WM_SNAP), and every
; blit delta and glyph y downstream is chrome top + a multiple of 4 or 8 -
; keeping the sum aligned is what keeps gfx_scroll's banked fast path and
; font_run's single-store path exactly as reachable as they were in Note Pad.
WD_MENU_H    equ 14             ; the in-window menu bar
WD_RIBBON_H  equ 16             ; the ribbon (View > Ribbon)
WD_RULER_H   equ 34             ; the ruler (View > Ruler): TWO rows, as
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
WD_RL_ROW2   equ 16             ; ...where that second row begins in the strip
WD_STATUS_H  equ 12             ; the status bar (View > Status Bar)
WD_CHROME_TOP equ WD_MENU_H + WD_RIBBON_H + WD_RULER_H   ; 48, all strips ON.
                                ; The LIVE chrome top is [wd_ctop], computed by
                                ; wd_ctcalc from the View toggles - ribbon and
                                ; ruler can each be hidden (SPEC.md 65.2), and
                                ; then the sum is 30 or 32 or 14. The align-
                                ; ment note above holds for the default stack;
                                ; a toggled top costs the blit its banked fast
                                ; path for that one transient move, never
                                ; correctness.
%if (WD_CHROME_TOP & 7) != 0
  %error "WD_CHROME_TOP must be a multiple of 8 - see the note above"
%endif

; --- the in-window menu system (SPEC.md 65.2) --------------------------------
; Word's nine menus, verbatim from Opus's menus.cmd. A menu (or a ribbon/ruler
; combo's one-entry list, which reuses all of this) is described by an 8-byte
; row of wd_mtab and an array of 8-byte item records; the open dropdown's
; rectangle is computed once at open (wd_mgeo) and banked in wd_mrect, so the
; painter, the hit test and the close repaint all read the same four words -
; the fm_hit discipline (SPEC.md 22).
WD_M_N       equ 9              ; menus on the bar
; WD_PROPDRAW - is a chosen face DRAWN, or only opened and named?
;
; 0 while SPEC.md 65.13's conversion is unfinished, and this is the switch it
; finishes at. What works: the face opens, the ribbon names it, wd_bandrun
; composes and emits a run through SPEC.md 6.3's band, and the row advance and
; glyph band follow the face's own rows via [wd_gh]/[wd_ghb]. What does NOT,
; measured by turning this on and looking:
;
;   1. the chrome doubles - the content geometry moves under the bar and the
;      strips, and repainting them the way a View toggle does is not enough;
;      wd_ctcalc/wd_bounds have to be understood first
;   2. the rows redraw but the glyphs stay the 8x8 cell's, so ty_flush is
;      refusing or the band is arriving empty - unproven either way
;   3. the metrics are still the 8-pixel grid ([wd_pxon] is 0): the wrap rule,
;      the accumulate walk's cell index and wd_px[] are not converted
;
; A half-converted Word is worse than an unconverted one - it draws in one
; face and measures in another - so this stays 0 until all three are done, and
; the code above it stays in the tree because it assembles and is most of the
; way there.
;
; %assign AND NOT equ, and that distinction cost a whole debugging round: `%if`
; is a PREPROCESSOR directive and an `equ` is an ASSEMBLER symbol, so `%if
; WD_PROPDRAW` against an equ sees an undefined name, evaluates it as 0, and
; silently assembles neither arm. The build is clean, the flag reads 1 in the
; source, and nothing happens at run time.
%assign WD_PROPDRAW 1
%assign WD_BANDDBG 0            ; the band probe, off - see wd_bandrun

WD_MAXFONT   equ 10             ; faces the Font combo will list beside Pica.
                                ; Ten, which is os88type's TY_MAXFAM and what
                                ; FONTS/ carries (SPEC.md 6.4.1). It used to
                                ; be six, because eleven 10px items hang off a
                                ; ribbon box 60 rows down a 200-row CGA screen
                                ; ran off the bottom of the window; wd_mgeo
                                ; now SLIDES a combo's dropdown up until it
                                ; fits instead of clipping its tail off, which
                                ; is what makes the tenth face reachable there
WD_MI_SZ     equ 8              ; bytes in one WDMI record - four bytes then
                                ; two words, and wd_mitemp indexes by it. The
                                ; dropdown is filled at run time now (SPEC.md
                                ; 65.13), so the size the macro emits needs a
                                ; name rather than being counted by hand
WD_MT_SZ     equ 8              ; ...and one wd_mtab row, which wd_mgeti
                                ; reaches with three shifts
WD_M_FONTC   equ 9              ; the ribbon's Font combo, as a pseudo-menu
WD_M_PTSC    equ 10             ; ...its Pts combo
WD_M_STYLEC  equ 11             ; ...and the ruler's Style combo
WD_M_NONE    equ 0xFF           ; [wd_mopen]: nothing open
WD_MI_HGT    equ 10             ; an item band: 8px of glyph + 1 above + 1 under
WD_MS_HGT    equ 5              ; a separator band

WDMF_SEP     equ 1              ; item flags: a separator hline
WDMF_DIS     equ 2              ; disabled - drawn with the disabled pen
                                ; (OSAPI_GFX_PEN CF=1, SPEC.md 47), never fired
WDMF_CHK     equ 4              ; carries a check mark; wd_mchk answers it live

; Menu actions - what an ENABLED item does when it fires. One byte per item,
; dispatched by wd_mfire's table; menu items and shortcut keys are twin doors
; onto the same routines, exactly as the kernel-menu stage had it.
WDA_NONE     equ 0
WDA_NEW      equ 1
WDA_OPEN     equ 2
WDA_CLOSE    equ 3
WDA_SAVE     equ 4
WDA_SAVEAS   equ 5
WDA_EXIT     equ 6
WDA_UNDO     equ 7
WDA_CUT      equ 8
WDA_COPY     equ 9
WDA_PASTE    equ 10
WDA_SEARCH   equ 11
WDA_REPL     equ 12
WDA_DRAFT    equ 13             ; View > Draft (SPEC.md 65.11)
WDA_VRIB     equ 14
WDA_VRUL     equ 15
WDA_VSTA     equ 16
WDA_ABOUT    equ 17
WDA_WIN1     equ 18             ; Window > 1 <doc>: the one window; checked
WDA_CSEL     equ 19             ; a combo's entry - cosmetic select
WDA_CHAR     equ 20             ; Format > Character... - the modal dialog
WDA_PARA     equ 21             ; Format > Paragraph... (SPEC.md 65.3)
WDA_GOTO     equ 22             ; Edit > Go To... (SPEC.md 65.7)
WDA_SORT     equ 23             ; Utilities > Sort... (SPEC.md 65.9)
WDA_RENUM    equ 24             ; Utilities > Renumber...
WDA_TOC      equ 25             ; Insert > Table of Contents...
WDA_PAGE     equ 26             ; View > Page (SPEC.md 65.11)
WDA_MAX      equ 26

; --- the CHP attribute byte (SPEC.md 65.3) -----------------------------------
; One byte per character, in a claim that mirrors every gap operation the text
; claim makes. Bit 7 is reserved in the FILE format; in the ROW's attribute
; buffer it marks a paragraph-mark cell (Show-all's pilcrow), which never
; reaches the CHP claim.
WDAT_BOLD    equ 0x01
WDAT_ITAL    equ 0x02
WDAT_UL      equ 0x04
WDAT_WUL     equ 0x08
WDAT_DUL     equ 0x10
WDAT_SCAP    equ 0x20
WDAT_HID     equ 0x40
WDAT_PIL     equ 0x80           ; row-buffer only: this cell is a pilcrow

; --- the PAP dictionary (SPEC.md 65.3) ---------------------------------------
; Paragraph formatting lives ON THE PARAGRAPH MARK: a 13's CHP byte is an
; index into a 256-entry dictionary of unique 4-byte formats, exactly Word's
; arrangement shrunk to fit - so undo, cut/paste and the drag-move's rotate
; carry paragraph formats with the ordinary text+CHP machinery and no third
; structure exists to keep synchronized. The LAST paragraph has no mark, so
; its index lives in [wd_pap_tail]. Entry layout:
;   byte 0  packed: bits 0-1 alignment (left/center/right/justified),
;                   bits 2-3 line spacing (0/1/2 = 8/12/16px row advance),
;                   bit 4 space before (an open paragraph: +8px on the
;                   paragraph's first row)
;   byte 1  left indent, in cells (1 cell = 8px = 1/10" at pica pitch)
;   byte 2  first-line indent, SIGNED cells relative to the left indent
;   byte 3  right indent, in cells
; Entry 0 is Normal (all zero) and is pre-seeded; wd_papfind deduplicates,
; entries live for the session, and a 257th unique format is refused with a
; toast - the dictionary never moves or reuses an index, which is what lets
; an undo blob's old index still mean the old format.
WD_PAPMAX    equ 256            ; entries (4 bytes each: a 1KB claim)
WD_INDMAX    equ 100            ; indent clamp, cells (10 inches)
WD_TABSTOP   equ 5              ; default tab stops: every 5 cells (half inch)
                                ; from the row's own start pen - custom stops
                                ; are out of scope (the ruler's tab-type cells
                                ; are greyed; a real stop table is a later
                                ; stage's structure)
WDPA_ALIGN   equ 0x03           ; the packed byte's fields
WDPA_SPACE   equ 0x0C
WDPA_SB      equ 0x10

; wd_modpap operations - every door (Ctrl keys, ruler cells, marker drags,
; the Format Paragraph dialog) funnels through one span-modifier
WDPO_ALIGN   equ 0              ; arg = 0..3
WDPO_SPACE   equ 1              ; arg = 0..2
WDPO_SB      equ 2              ; arg = 0/1
WDPO_ADDLEFT equ 3              ; arg = signed cells (Ctrl-N: +5)
WDPO_HANG    equ 4              ; left +5, first -5 (Ctrl-T)
WDPO_UNHANG  equ 5              ; left -5, first +5 (Ctrl-G)
WDPO_RESET   equ 6              ; back to Normal, index 0 (Ctrl-X)
WDPO_SETLEFT equ 7              ; arg = cells (the ruler's left marker)
WDPO_SETFIRST equ 8             ; arg = SIGNED cells (the first-line marker)
WDPO_SETRIGHT equ 9             ; arg = cells (the right marker)
WDPO_SETALL  equ 10             ; the whole candidate [wd_pfb] (the dialog)

; --- the modal dialog framework (SPEC.md 65.3) -------------------------------
; A dialog is a descriptor: dw width, height, title; then 12-byte control
; records (db type, flags; dw x, y, p1, p2, p3 - x/y relative to the dialog's
; top-left), terminated by a type-0 byte. Painter and hit test walk the SAME
; records - the fm_hit discipline - so a control cannot be drawn one place
; and clicked another. Reused by the later dialog stages.
WDD_LBL      equ 1              ; label:      p1 = string
WDD_CHK      equ 2              ; check box:  p1 = string, p2 = attr mask,
                                ;             p3 = mnemonic index in p1
WDD_RAD      equ 3              ; radio:      p1 = string, p2 = selected
WDD_GRP      equ 4              ; group box:  p1 = title, p2 = w, p3 = h
WDD_BOX      equ 5              ; combo/edit mock: p1 = shown text, p2 = w
WDD_BTN      equ 6              ; button:     p1 = label, p2 = w,
                                ;             p3 = 1 OK (default ring) /
                                ;             2 Cancel / 3 No (the dirty
                                ;             prompt's third verb, 65.4)
WDD_EDIT     equ 7              ; live edit:  p1 = its buffer (NUL), p2 = w,
                                ;             p3 = capacity in chars (0 = 6).
                                ;             Click or Tab focuses; digits
                                ;             . - " type; BkSp deletes
                                ;             (SPEC.md 65.3). With WDDF_TXT
                                ;             in the flags it takes every
                                ;             printable 32..126 instead - the
                                ;             search dialogs' boxes (65.7)
                                ; ...and a WDD_RAD whose p3 is NON-ZERO is a
                                ; LIVE radio: p3 is its group, a click selects
                                ; it and deselects its group-mates. p3 = 0 is
                                ; the char dialog's decorative (greyed) kind
WDDF_DIS     equ 1              ; control flags bit 0: disabled (SPEC.md 47)
WDDF_TXT     equ 2              ; bit 1, WDD_EDIT only: free text (SPEC.md
                                ; 65.7). While one is FOCUSED it consumes
                                ; letters, so check-box mnemonics answer
                                ; only when no text edit has the focus

; --- ribbon and ruler layout (SPEC.md 65.2), content-relative x --------------
; Grouping and order per Opus ibdefs.h: Font/Pts combos, then Bold Italic
; SmallKaps | Underline Word Double | the stacked super/subscript pair | the
; show-all pilcrow at the far right; ruler: Style combo, align x4, spacing x3,
; open/close space, tab type x4, then the inch scale. The x offsets keep every
; combo's TEXT on a multiple of 8 from the (snapped) content origin, so the
; strings ride font_run's single-store path.
WD_BTN_W     equ 12             ; a toggle cell, 12x12
WD_BTN_P     equ 13             ; ...and the pitch inside a group
WD_RB_FLBL   equ 8              ; 'Font:' label
WD_RB_FBX    equ 56             ; Font combo box (text at +8)
WD_RB_FBW    equ 96
WD_RB_PLBL   equ 168            ; 'Pts:' label
WD_RB_PBX    equ 208            ; Pts combo box
WD_RB_PBW    equ 56
WD_RB_B1     equ 272            ; B I K
WD_RB_B2     equ 319            ; U W D
WD_RB_SS     equ 366            ; the super/subscript pair
WD_RL_SLBL   equ 8              ; 'Style:' label
WD_RL_SBX    equ 64             ; Style combo box
WD_RL_SBW    equ 96
WD_RL_AL     equ 168            ; align left/center/right/justified
WD_RL_SP1    equ 224            ; spacing '1'
WD_RL_SP15   equ 239            ; spacing '1.5' (26 wide: three glyph cells)
WD_RL_SP15W  equ 26
WD_RL_SP2    equ 268            ; spacing '2'
WD_RL_OC     equ 284            ; closed/open paragraph spacing
WD_RL_TAB    equ 314            ; tab type L C R D
WD_RL_SCALE  equ 376            ; the inch scale's zero
WD_ST_CELLS  equ 46             ; status line text cells (delta-cached):
                                ; 'Pg 15  Sec 1  15/15  At 54li  Ln 54  Col
                                ; 171' is 44 at the worst (SPEC.md 65.2)
WD_PGCOLS    equ 60             ; the sheet's text column in Page view: 60
                                ; cells = 480px = 6 inches at this port's
                                ; scale (1 cell = 1/10"), which is US Letter
                                ; less Word's own 1.25" margins. The COLUMN
                                ; and not the full 8.5" sheet, on every
                                ; adapter: the sheet is 680px wide and only
                                ; Hercules is wider than 640 (SPEC.md 39), so
                                ; a full sheet would fit one adapter of three
                                ; and CLAUDE.md's rule is to look at a 1bpp
                                ; adapter before calling a layout done
WD_PGLINES   equ 54             ; lines to a page (SPEC.md 65): Pg/Ln derive
                                ; from the caret's absolute line by this

; --- the italic claim (SPEC.md 65.1) -----------------------------------------
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
; exactly the same lifetime and the same refusal: wd_itinit already degrades
; italic to the plain glyph when the heap says no (SPEC.md 47), and that one
; path now covers both.
WD_ITTAB     equ 95*8*4         ; the table: 95 glyphs x 8 rows x 4 bytes
WD_STG4      equ WD_ITTAB       ; ...and the staging area starts after it
WD_ITKB      equ 9              ; 3,040 + 5,440 = 8,480, so nine whole KB

WD_MARGIN    equ 8              ; left/top text margin inside the content. It
                                ; was 6, and 8 is what puts every glyph cell
                                ; on a multiple of 8 once OSAPI_WM_SNAP has
                                ; put the content origin on one (SPEC.md
                                ; 11.94): wd_tx is content left + this, and
                                ; wd_walk advances the pen by 8 from there.
                                ; A glyph at an unaligned x spills into a
                                ; SECOND framebuffer byte whenever the shift
                                ; carries ink into it, and this window redraws
                                ; text on every keystroke
                                ; The two F-keys are WD_KEY_NEXT and
                                ; WD_KEY_PREV below, and they are the only
                                ; two: F2 was Save and F3 was Load, the DOS
                                ; Editor's pair, and both are Ctrl-letters now
                                ; (SPEC.md 27.1/27.10). What made the DOS keys
                                ; worth keeping was that they were the ones a
                                ; user of that machine already knew, and this
                                ; window has a Macintosh menu bar over it
WD_K_F1      equ 0x3B           ; F1 - Help: opens the About box (the only
                                ; help there is; Help > Index is greyed)
WD_K_F4      equ 0x3E           ; F4 - repeat search down (SPEC.md 65.7)
WD_K_F5      equ 0x3F           ; F5 - Go To... (keys.cmd EditGoTo)
WD_K_F8      equ 0x42           ; F8 - extend selection (status EXT)
WD_K_SF4     equ 0x57           ; Shift-F4 - repeat search up (Shift-F1..F10
                                ; are scans 0x54..0x5D)
WD_K_HOME    equ 0x47           ; the caret keys, int 16h scan codes
WD_K_UP      equ 0x48
WD_K_LEFT    equ 0x4B
WD_K_RIGHT   equ 0x4D
WD_K_END     equ 0x4F
WD_K_DOWN    equ 0x50
WD_K_DEL     equ 0x53
WD_NAMEMAX   equ 12             ; 8 + '.' + 3, as SPEC.md 38.6 hands it over

; --- keys (SPEC.md 27.8/65.3) ------------------------------------------------
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
; BIOS shift flags in wd_onkey. Deviations, each deliberate: Ctrl-A stays
; Select All (the authentic C-NumPad5 does not arrive as anything usable),
; Ctrl-S stays Save and Ctrl-F Search (their authentic meanings - style/font
; box focus - have no home yet, and a Save with no key is a document lost),
; Ctrl-Z stays undo beside Alt+BkSp (unbound in keys.cmd, harmless).
WD_C_SELALL  equ 0x01           ; Ctrl-A
WD_C_BOLD    equ 0x02           ; Ctrl-B - bold (keys.cmd; SPEC.md 65.3)
WD_C_CENTER  equ 0x03           ; Ctrl-C - CenterPara (keys.cmd!)
WD_C_DUL     equ 0x04           ; Ctrl-D - double underline (keys.cmd)
WD_C_CLOSESP equ 0x05           ; Ctrl-E - CloseUpPara
WD_C_FIND    equ 0x06           ; Ctrl-F
WD_C_UNHANG  equ 0x07           ; Ctrl-G - UnHang
WD_C_JUST    equ 0x0A           ; Ctrl-J - JustifyPara
WD_C_SCAP    equ 0x0B           ; Ctrl-K - small caps (keys.cmd)
WD_C_LEFT    equ 0x0C           ; Ctrl-L - LeftPara
WD_C_INDENT  equ 0x0E           ; Ctrl-N - Indent (+half inch)
WD_C_OPENSP  equ 0x0F           ; Ctrl-O - OpenUpPara (Open is Ctrl+F12)
WD_C_RIGHT   equ 0x12           ; Ctrl-R - RightPara
WD_C_SAVE    equ 0x13           ; Ctrl-S (deviation, kept - see above)
WD_C_HANG    equ 0x14           ; Ctrl-T - HangingIndent
WD_C_UL      equ 0x15           ; Ctrl-U - underline (keys.cmd)
WD_C_WUL     equ 0x17           ; Ctrl-W - word underline (keys.cmd)
WD_C_RSTP    equ 0x18           ; Ctrl-X - ResetPara (keys.cmd!)
WD_C_UNDO    equ 0x1A           ; Ctrl-Z
WD_C_ESC     equ 0x1B
WD_C_TAB     equ 0x09
WD_K_INS     equ 0x52           ; the Ins scan: + shift = Paste, + ctrl =
                                ; Copy (keys.cmd); Del + shift = Cut
WD_K_PGUP    equ 0x49
WD_K_PGDN    equ 0x51
WD_KEY_NEXT  equ 0x3D           ; F3 - Find Next. It WAS Load, and Load has a
                                ; menu item and Ctrl-O now: a find with no
                                ; key for the next match is not a find
WD_KEY_PREV  equ 0x56           ; Shift-F3 (Shift-F1..F10 are 0x54..0x5D)

; --- the selection (SPEC.md 27.8) --------------------------------------------
WD_SELDRAG   equ 3              ; pixels the pointer must travel before a
                                ; press inside a selection becomes a MOVE
                                ; rather than a click that collapses it

; --- undo (SPEC.md 27.9) -----------------------------------------------------
WD_UNDO      equ 5              ; edits deep, as asked
WD_UKB0      equ 1              ; the arena's first claim, KB...
WD_UMAXKB    equ 16             ; ...and its ceiling. Note Pad's 16, KEPT at
                                ; 16 while WD_MAXKB rose to 30: a Replace All
                                ; over a document whose rewritten tail is
                                ; past 16KB now falls back to "swept but not
                                ; undoable" (wd_uroom evicts, then drops the
                                ; stack - the sweep itself still runs), which
                                ; is the degrade-not-refuse path. Worth its
                                ; own decision when the undo arena is looked
                                ; at again, not a silent 14KB of heap here

; --- search and replace (SPEC.md 65.7) ---------------------------------------
; Word 1.1 had no regex, and neither does this: the engine is a literal scan
; honouring the authentic search.des options. Note Pad's regex matcher and
; its docked panel are gone - Edit > Search... / Replace... / Go To... are
; modal dialogs on the SPEC.md 65.3 framework now.
WD_FRXMAX    equ 96             ; the expanded replacement's ceiling
                                ; (SPEC.md 65.7): ^m can be as long as the
                                ; match and ^c as long as the clipboard, so
                                ; the splice buffer is bounded here and a
                                ; replacement that overflows it is REFUSED
                                ; with a toast rather than truncated
WD_PATMAX    equ 47             ; characters a pattern or a replacement holds
WD_EDCAP     equ 32             ; ...and what the dialogs' edit boxes cap at
                                ; (the box shows its whole text - no scroll)
WDFO_WORD    equ 1              ; [wd_fopt] bits, and the search/replace
WDFO_CASE    equ 2              ; dialogs' check-box masks: Whole Word,
WDFO_CONF    equ 4              ; Match Upper/Lowercase, Confirm Changes

; --- the native .DOC file (SPEC.md 65.4) -------------------------------------
; 16-byte FIB, then text, CHP, PAP (papn x 4). The flags word's LOW BYTE is
; the tail paragraph's PAP index - the one paragraph fact with no ¶ to ride.
WD_DOCMAGIC  equ 0xA59B         ; Opus's real wIdent
WD_DOCVER    equ 1
WD_FIB       equ 16             ; header bytes
; The load staging ceiling: the largest native file is 16 + 2*30,720 + 1,024
; = 62,480 bytes; 62KB (63,488) covers it and still fits a 16-bit count.
WD_LSTGKB    equ 62
WD_STGCAP    equ WD_FIB + 2*(WD_MAXKB*1024) + WD_PAPMAX*4

; --- the dirty prompt's pending action (SPEC.md 65.4) ------------------------
WDP_NEW      equ 1              ; what 'Yes'/'No' proceed to once the
WDP_OPEN     equ 2              ; save-changes question is answered
WDP_CLOSE    equ 3

; -----------------------------------------------------------------------------
; wd_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here. The
; bss arrives zeroed, which is already a fresh empty note - the built-in's
; KD_INIT proc (app_note_kinit) had nothing else to do either.
;
; The menu set is registered here rather than later because the loader's
; wm_show is what draws the first bar (SPEC.md 12.2): by the time the window
; appears, the bar already says "Microsoft Word  File".
; -----------------------------------------------------------------------------
wd_entry:
    call OSAPI_FILE_HERE            ; where this package was LAUNCHED from,
    mov [wd_ovdir], dx              ; banked before anything can navigate away
    mov [wd_ovdrv], bl              ; - it is where WORD.OVL lives, and the
                                    ; user is free to move the volume to DOCS\
                                    ; a moment later (SPEC.md 65.10)
    call ty_init                    ; face 0 (the kernel's 8x8) before anything
                                    ; can ask for a metric - SPEC.md 6.5, and
                                    ; the reason nothing below has to test
                                    ; whether a face was found
    call wd_facemetrics             ; the glyph band and the row advance, which
                                    ; are 8 and 7 for the kernel's cell - and
                                    ; bss arrives ZEROED, so without this every
                                    ; row-band test in the program compares
                                    ; against y+0 instead of y+7 and drops most
                                    ; of the note
    mov word [wd_fcap], wd_s_pica   ; ...and the ribbon's Font box names it.
                                    ; bss arrives zeroed, so without this the
                                    ; first ribbon paint draws a string at
                                    ; offset 0 - which is this package's own
                                    ; header, and looks like a corrupt heap

    call wd_xdrop                   ; the row index starts EMPTY and at its
                                    ; initial stride (SPEC.md 27.13). bss
                                    ; arrives zeroed, and a zero [wd_xksh] is
                                    ; a stride of ONE - not wrong, but 64
                                    ; entries covering 64 rows and halving four
                                    ; times to catch up on the first count
    mov ax, WD_KB0                  ; the document, before anything else: an
    call OSAPI_MEM_CLAIM            ; editor with nowhere to put the text is
    jc .nomem                       ; not a window worth opening, and the
    mov [wd_dseg], dx               ; loader's LD_EABORT says so for us
    mov ax, WD_KB0                  ; ...and the CHP claim, one attribute byte
    call OSAPI_MEM_CLAIM            ; per character, grown/shrunk in lockstep
    jc .nomem                       ; by wd_resize (SPEC.md 65.3). Claimed here
    mov [wd_cseg], dx               ; so wd_arg's load can zero it; a refusal
                                    ; aborts the launch and ld_unreserve frees
                                    ; the text claim with the region
    mov ax, 1                       ; ...and the PAP dictionary (SPEC.md 65.3):
    call OSAPI_MEM_CLAIM            ; 256 x 4 bytes, session-lived. Zeroed by
    jc .nomem                       ; hand - a claim arrives dirty and entry 0
    mov [wd_pseg], dx               ; must BE Normal, all zero
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
    mov word [wd_papn], 1
    mov word [wd_capkb], WD_KB0
    mov word [wd_cap], WD_KB0 * 1024
    call wd_defname                 ; the name and the TITLE the template
    call wd_compttl                 ; points at (SPEC.md 65.2: 'Microsoft
                                    ; Word - DOCUMENT.DOC') must exist before
                                    ; wm_create banks the pointer - the
                                    ; buffer is bss and arrives zeroed
    push si
    mov si, wd_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): wd_paint
    call OSAPI_WM_SIZABLE           ; already lays out from the live record,
    mov al, 1                       ; so the next repaint re-wraps for free
    mov al, 1                       ; ...and it PROMISES its content stands
    call OSAPI_WM_SAVEU             ; still while it is not drawing, so a
                                    ; raise puts the old pixels back instead
                                    ; of lettering 464 cells (SPEC.md 11.96.1).
                                    ; True of this app: everything that draws
                                    ; goes through wd_redraw or wd_paint, and
                                    ; the worker's two background drawers ask
                                    ; OSAPI_WM_OBSCURED first
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
    mov si, wd_habout               ; NO kernel menus (SPEC.md 65.2): the
    call OSAPI_ABOUT_SET            ; nine-menu bar is drawn in the window.
    mov si, wd_menus0               ; The kernel bar carries only the app
    call OSAPI_MENU_SET             ; name pull-down - an EMPTY set (count 0)
    pop si                          ; whose AM_NAME makes the bar read
                                    ; 'Microsoft Word' rather than the
                                    ; header's 'WORD', with About (the same
                                    ; box Help > About... opens) and Close
                                    ; under it. CF is still wm_create's: the
                                    ; branch above consumed it and both slots
                                    ; preserve flags (SPEC.md 12.2)
    mov byte [wd_vrib], 1           ; every strip starts SHOWN (View > Ribbon /
    mov byte [wd_vrul], 1           ; Ruler / Status Bar are checked toggles,
    mov byte [wd_vsta], 1           ; SPEC.md 65.2)
    mov word [wd_ctop], WD_CHROME_TOP
    mov byte [wd_mopen], WD_M_NONE  ; no dropdown, no highlight - bss zero is
    mov byte [wd_mhi], 0xFF         ; a REAL menu index and a real item
    mov [wd_win], bx                ; the worker (SPEC.md 27.3) has no callback
                                    ; to be handed this in SI
    pushf                           ; the visual break exists for the machine
    push ax                         ; that cannot repaint a screenful between
    call OSAPI_CPU_INFO             ; keystrokes, and nowhere else: on anything
    cmp al, CPU_8086                ; faster the reflow is already invisible
    jne .nobrk                      ; and a temporary layout would be a lie
    mov byte [wd_brkok], 1          ; told for no gain. The flags are saved
.nobrk:                             ; around it because the CF this proc owes
    pop ax                          ; the loader is still riding in them and
    popf                            ; the compare above would eat it
    mov word [wd_prowi], 0xFFFF     ; .bss arrives zeroed and 0 is a REAL row
                                    ; index, so the delta cache has to be told
                                    ; it holds nothing (SPEC.md 27.2)
    mov word [wd_dpos], 0xFFFF      ; no drop marker, and 0 is a real index
    mov word [wd_dmark], 0xFFFF
    pushf                           ; the name was seeded above (before the
    call wd_arg                     ; template was banked); wd_arg opens a
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
; [wd_top] is the note row drawn at the top of the view, and wd_walk's wd_row
; - the index into wd_sig, wd_rows and the dirty band - starts at MINUS it.
; Nothing downstream of that had to change: every array index is already an
; unsigned test against a limit, and a negative row read as unsigned is past
; all of them. The one place that could not see it is wd_rflush, because a row
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
; wd_scrollmax - the largest [wd_top] that still shows text
; out: AX = max(wd_drows - wd_vrows, 0); preserves all other registers
; -----------------------------------------------------------------------------
wd_scrollmax:
    mov ax, [wd_drows]
    cmp byte [wd_hasfmt], 0
    je .uni
    sub ax, [wd_vfit]               ; formatted: how many rows fit depends on
    jns .out                        ; their heights, so the clamp loosens to
    xor ax, ax                      ; the guaranteed fit - a mostly-plain
    ret                             ; document can overscroll a little, and
.uni:                               ; the tail row is always reachable
    sub ax, [wd_vrows]
    jns .out
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_scrollto - put row AX at the top of the view
; in:  AX = the wanted row (signed; clamped here), SI = window ptr
; out: CF=0 if [wd_top] moved, CF=1 if it did not; preserves all registers
;
; Everything indexed by the visible row means something different afterwards -
; the signatures, the checkpoint and wd_rows all shift by the same amount as
; the view - so all three are dropped rather than adjusted. Adjusting them
; would be a second place that has to agree about what a row is.
; -----------------------------------------------------------------------------
wd_scrollto:
    push ax
    push bx
    or ax, ax
    jns .lo
    xor ax, ax
.lo:
    push ax
    call wd_scrollmax
    mov bx, ax
    pop ax
    cmp ax, bx
    jbe .hi
    mov ax, bx
.hi:
    cmp ax, [wd_top]
    je .same
    mov [wd_top], ax                ; wd_sig and wd_rows are NOT dropped here
                                    ; any more: they still describe the pixels
                                    ; that are still on screen, and
                                    ; wd_scrollpaint shifts all three together
                                    ; (SPEC.md 27.7.2). [wd_ptop] is what says
                                    ; the two have parted, and every path out
                                    ; of wd_redraw puts them back in step
    mov byte [wd_ckok], 0           ; the checkpoint is one row rather than an
                                    ; array, so it is cheaper to re-find than
                                    ; to carry: the view seed replaces it
    mov byte [wd_resume], 0         ; ...including one ALREADY LOADED: wd_walk
                                    ; takes [wd_sdr] as a visible row and
                                    ; wd_measure does not clear the flag, so a
                                    ; scroll between wd_seedck and the walk it
                                    ; seeded resumes at the wrong row and every
                                    ; number that walk produces - [wd_cury] and
                                    ; the note's height - is out by the scroll
    mov byte [wd_bmode], 0          ; and the visual break's fiction is over
    clc
    jmp short .out
.same:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_seecaret - scroll so the caret is inside the view
; in:  [wd_cury] from a walk, SI = window ptr
; out: CF=0 if the view moved; preserves all registers
; -----------------------------------------------------------------------------
wd_seecaret:
    push ax
    push cx
    push dx
    mov ax, [wd_currow]             ; the caret's VISIBLE row, banked by the
    or ax, ax                       ; walk (SPEC.md 65.6): its y is no longer
    js .up                          ; 8*row, but its NUMBER still is the row
    mov cx, [wd_cury]               ; visible = the caret's whole glyph band
    add cx, [wd_gh1]                       ; fits above the content bottom
    cmp cx, [wd_bot]
    jbe .same
    ; below the view. Formatted, the EXACT scroll is knowable: the caret is
    ; (cury+7 - bot) pixels past the bottom, and the pixels freed by
    ; scrolling k rows off the top are band-top(k) - ty, which the banked ys
    ; answer - so take the smallest k that frees enough (SPEC.md 65.6). The
    ; conservative [wd_vfit] target is the fallback, and the uniform case's
    ; exact answer as it always was.
    cmp byte [wd_hasfmt], 0
    je .dcons
    cmp byte [wd_rowsok], 0
    je .dcons
    sub cx, [wd_bot]                ; CX = cury+7 - bot, the overshoot
    xor dx, dx                      ; DX = k-1
.dk:
    cmp dx, [wd_vrows]
    jae .dcons                      ; the glass cannot free enough
    push bx
    mov bx, dx
    shl bx, 1
    mov bx, [bx+wd_ryb]
    add bx, 8
    sub bx, [wd_ty]                 ; freed by scrolling k = dx+1 rows
    cmp bx, cx
    pop bx
    jge .dhave
    inc dx
    jmp short .dk
.dhave:
    inc dx
    mov ax, dx
    add ax, [wd_top]
    jmp short .go
.dcons:
    mov ax, [wd_currow]
    sub ax, [wd_vfit]               ; ...bring it to the last row GUARANTEED
    inc ax                          ; to fit - conservative under formats,
    add ax, [wd_top]                ; exact while uniform (vfit = vrows)
    jmp short .go
.up:
    add ax, [wd_top]                ; ...above it: bring it to the first
.go:
    call wd_scrollto
    jmp short .out
.same:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_thumb - the thumb's geometry, shared by the painter and the hit test
; in:  wd_bounds run
; out: CF=1 no thumb (it all fits, or the track is too short); else CF=0,
;      AX = thumb top (absolute y), DX = thumb height
; clobbers: nothing else
;
; The Disk window's arithmetic (SPEC.md 22): height = fit*track/total with an
; 8px floor, top = scroll*track/total, clamped so the bottom of the thumb
; cannot leave the track.
; -----------------------------------------------------------------------------
wd_thumb:
    push bx
    push cx
    mov ax, [wd_drows]
    cmp ax, [wd_vrows]
    jbe .none
    call wd_trackh                  ; BX = track height
    cmp bx, 8
    jl .none                        ; degenerate track: bare, no thumb
    mov ax, [wd_vrows]
    mul bx
    div word [wd_drows]             ; AX = proportional height
    cmp ax, 8
    jge .hok
    mov ax, 8
.hok:
    mov cx, ax
    mov ax, [wd_top]
    mul bx
    div word [wd_drows]             ; AX = offset down the track
    add ax, [wd_ty]
    add ax, WD_SB_ARR               ; ...from the track's top
    mov dx, [wd_ty]                 ; clamp: top <= track top + track - height
    add dx, WD_SB_ARR
    add dx, bx
    sub dx, cx
    cmp ax, dx
    jle .tok
    mov ax, dx
.tok:
    mov dx, cx
    clc
    jmp short .out
.none:
    stc
.out:
    pop cx
    pop bx
    ret

; wd_trackh - BX = the grey track's height, between the two arrow cells
wd_trackh:
    push ax
    mov bx, [wd_sbb]
    sub bx, [wd_ty]
    inc bx
    sub bx, WD_SB_ARR*2
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_sbar - draw the scroll bar
; in:  SI = window ptr, wd_bounds run, gfx lock held
; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
wd_sbar:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov di, [wd_sbr]
    sub di, WD_SB_W-1               ; DI = the bar's left column, absolute

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di                      ; the box
    mov bx, [wd_ty]
    mov cx, [wd_sbr]
    mov dx, [wd_sbb]
    call OSAPI_GFX_FRAME

    mov ax, di                      ; the two arrow-cell rules
    mov bx, [wd_sbr]
    mov dx, [wd_ty]
    add dx, WD_SB_ARR-1
    call OSAPI_GFX_HLINE
    mov ax, di
    mov bx, [wd_sbr]
    mov dx, [wd_sbb]
    sub dx, WD_SB_ARR-1
    call OSAPI_GFX_HLINE

    xor bp, bp                      ; up glyph: 5 rows, widths 1..9
    mov dx, [wd_ty]
    add dx, 3
.up:
    mov ax, di
    add ax, 6
    sub ax, bp
    mov bx, di
    add bx, 6
    add bx, bp
    call OSAPI_GFX_HLINE
    inc dx
    inc bp
    cmp bp, 5
    jb .up

    mov bp, 4                       ; down glyph: 5 rows, widths 9..1
    mov dx, [wd_sbb]
    sub dx, 7
.dn:
    mov ax, di
    add ax, 6
    sub ax, bp
    mov bx, di
    add bx, 6
    add bx, bp
    call OSAPI_GFX_HLINE
    inc dx
    dec bp
    jns .dn

    mov ax, di                      ; the track, grey between the rules
    inc ax
    mov bx, [wd_ty]
    add bx, WD_SB_ARR
    mov cx, [wd_sbr]
    dec cx
    mov dx, [wd_sbb]
    sub dx, WD_SB_ARR
    cmp bx, dx
    jg .done                        ; degenerate track: leave it framed
    call OSAPI_GFX_FILL_GRAY

    call wd_thumb                   ; CF=1: it all fits, no thumb
    jc .done
    mov bx, ax                      ; y1 = thumb top
    add dx, ax
    dec dx                          ; y2 = top + height - 1
    mov ax, di
    add ax, 2
    mov cx, [wd_sbr]
    sub cx, 2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    add ax, 2
    mov cx, [wd_sbr]
    sub cx, 2
    call OSAPI_GFX_FRAME
.done:
    mov ax, [wd_top]                ; remember what is on screen, so a redraw
    mov [wd_sbtop], ax              ; that moved neither number draws nothing
    mov ax, [wd_drows]
    mov [wd_sbrows], ax
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_sbcheck - redraw the bar only if it would look different
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
wd_sbcheck:
    push ax
    mov ax, [wd_top]
    cmp ax, [wd_sbtop]
    jne .draw
    mov ax, [wd_drows]
    cmp ax, [wd_sbrows]
    je .out
.draw:
    call wd_sbar
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_sbclick - a press inside the scroll bar
; in:  CX = x, DX = y (absolute), wd_bounds run, gfx lock held
; out: CF=0 the bar took it (and may have scrolled), CF=1 it was not ours;
;      preserves all registers
;
; Zones are the Disk window's (SPEC.md 22): the two arrow cells step a row,
; the track pages against the thumb, and the thumb itself does nothing -
; there is no drag, here or there.
; -----------------------------------------------------------------------------
; wd_sbhit - is the click at (CX, DX) inside the scroll bar's rect?
; out: CF = 0 = yes; preserves every register
;
; Its own routine because wd_onclick asks the same question for a different
; reason - whether this click needs the note's height to be EXACT - and two
; copies of a rect are two things that have to agree about where the bar is.
wd_sbhit:
    push ax
    mov ax, [wd_sbr]
    sub ax, WD_SB_W-1
    cmp cx, ax
    jb .no
    cmp dx, [wd_ty]
    jb .no
    cmp dx, [wd_sbb]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

wd_sbclick:
    push ax
    push bx
    push dx
    call wd_sbhit
    jc .no
    mov bx, dx                      ; BX = the click's y; wd_thumb wants DX
    mov ax, [wd_ty]
    add ax, WD_SB_ARR
    cmp bx, ax
    jb .lineup
    mov ax, [wd_sbb]
    sub ax, WD_SB_ARR
    cmp bx, ax
    ja .linedn
    call wd_thumb                   ; AX = thumb top, DX = its height
    jc .yes                         ; it all fits: the bar is inert, but ours
    cmp bx, ax
    jb .pageup
    add ax, dx
    cmp bx, ax
    jae .pagedn
    jmp short .yes                  ; on the thumb itself
.lineup:
    mov ax, [wd_top]
    sub ax, WD_SB_STEP
    jmp short .set
.linedn:
    mov ax, [wd_top]
    add ax, WD_SB_STEP
    jmp short .set
.pageup:
    mov ax, [wd_top]
    sub ax, [wd_vfit]               ; a page is the rows GUARANTEED visible
    jmp short .set                  ; (= vrows while uniform, SPEC.md 65.6)
.pagedn:
    mov ax, [wd_top]
    add ax, [wd_vfit]
.set:
    ; [wd_drows] is a LOWER BOUND while the count is unfinished (SPEC.md
    ; 27.7.4), so wd_scrollmax would clamp this short of a row that really
    ; exists. Only a request that reaches past the counted extent needs the
    ; exact total - every other one is answered by what is already known, and
    ; every one of them used to pay for a full walk (SPEC.md 27.7.6).
    cmp byte [wd_hdirty], 0
    je .doset
    push ax                         ; the row being asked for...
    call wd_scrollmax               ; ...against the furthest one counted so far
    mov bx, ax                      ; (BX is this routine's own, saved above)
    pop ax
    cmp ax, bx
    jbe .doset
    push si                         ; SI is the CALLER's - wd_onclick has more
    mov si, [wd_win]                ; to do with it after this returns
    call wd_height
    pop si
.doset:
    call wd_scrollto
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
; What makes it safe is that the blit shifts the PIXELS and wd_shiftrows
; shifts their DESCRIPTION - wd_sig and wd_rows - by the same d, in the same
; operation. SPEC.md 27.7 says those two arrays are dropped rather than
; adjusted on a scroll, and that was right while the pixels were being
; redrawn wholesale: adjusting would have been a second place that has to
; agree about what a row is. Here there is no second place. The pixels and
; the arrays move together or not at all, and [wd_ptop] - the [wd_top] the
; screen was drawn for - is the one fact that says which.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_vshift - move the whole text band DI pixels (signed; positive = text up)
; in:  DI = the signed pixel distance, wd_bounds run, gfx lock held
; out: CF = OSAPI_GFX_SCROLL's answer; preserves all registers
;
; The x span is rounded OUTWARD to byte columns, and that is what lets this
; work on every adapter rather than only where OSAPI_WM_SNAP aligns the
; content (SPEC.md 11.94). Rounding x1 DOWN stays inside the content because
; WD_MARGIN is 8 and the rounding moves it at most 7; rounding x2+1 UP reaches
; at most seven columns into the scroll bar, which wd_sbar redraws
; immediately afterwards because the thumb has moved anyway. The band
; therefore CONTAINS every glyph pixel, which the break's wd_scroll - rounding
; inward, and needing [wd_tx] aligned for it - does not have to.
; -----------------------------------------------------------------------------
wd_vshift:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [wd_tx]
    and ax, 0xFFF8                  ; x1, down to a byte column
    mov bx, [wd_ty]                 ; y1
    mov cx, [wd_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                          ; x2, with x2+1 up to a byte column
    cmp byte [wd_hasfmt], 0         ; formatted rows land at arbitrary ys, so
    je .yuni                        ; the band is the whole [wd_ty..wd_bot] -
    mov dx, [wd_bot]                ; legal because the formatted scroll path
    jmp short .yok                  ; erases its exposed band to [wd_bot] too
.yuni:
    mov dx, [wd_vrows]              ; y2 stops at the bottom of the last WHOLE
    push cx                         ; row, not at [wd_bot]: a content height
    mov cl, 3                       ; that is not a multiple of 8 leaves a
    shl dx, cl                      ; sliver below it, wd_rflush refuses to
    pop cx                          ; draw a row that would cross it, and so
    add dx, [wd_ty]                 ; nothing would ever erase what the blit
    dec dx                          ; pushed into it. Scrolled to [wd_bot] it
    cmp dx, [wd_bot]                ; showed as a one-pixel band of the row
    jbe .yok                        ; above's descenders, left behind for good
    mov dx, [wd_bot]                ; - and only on a window whose content
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
; wd_shiftrows - move wd_sig and wd_rows by [wd_sdlt] rows
; in:  [wd_sdlt] = the signed row delta, |d| < [wd_vrows]
; out: nothing; preserves all registers
;
; Entry r must end up holding what entry r+d held, because the pixels of row
; r+d are now at row r. The slots with no source are the exposed rows, and the
; pass that letters them rewrites both arrays for exactly those.
; -----------------------------------------------------------------------------
wd_shiftrows:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                          ; both arrays are ours (SPEC.md 20.1)
    mov ax, [wd_sdlt]
    or ax, ax
    js .up
    mov cx, [wd_vrows]              ; DOWN: dst r, src r+d, ascending
    sub cx, ax
    jbe .out
    mov bx, ax
    shl bx, 1                       ; BX = d in bytes
    push cx
    mov di, wd_sig
    mov si, di
    add si, bx
    cld
    rep movsw
    pop cx
    push cx
    mov di, wd_rows
    mov si, di
    add si, bx
    rep movsw
    pop cx
    mov di, wd_ryb                  ; ...and the ys, which ride the same shift
    mov si, di                      ; (SPEC.md 65.6)
    add si, bx
    rep movsw
    jmp short .adj
.up:
    neg ax                          ; UP: dst r+|d|, src r, DESCENDING, or the
    mov cx, [wd_vrows]              ; copy would overwrite its own source
    sub cx, ax
    jbe .out
    mov bx, [wd_vrows]
    dec bx
    shl bx, 1                       ; BX = the last row's byte offset
    mov di, bx
    sub bx, ax
    sub bx, ax                      ; ...and BX = that minus |d| words
    mov si, bx
    push cx
    push si
    push di
    add si, wd_sig
    add di, wd_sig
    std
    rep movsw
    cld
    pop di
    pop si
    pop cx
    push cx
    push si
    push di
    add si, wd_rows
    add di, wd_rows
    std
    rep movsw
    cld
    pop di
    pop si
    pop cx
    add si, wd_ryb
    add di, wd_ryb
    std
    rep movsw
    cld
.adj:
    ; the blit moved every surviving row's pixels by [wd_sdpx], so their
    ; banked ys move with them - or the next pass 1 would call every row
    ; "moved" and repaint the view it just blitted
    mov ax, [wd_sdpx]
    mov cx, [wd_vrows]
    jcxz .out
    mov si, wd_ryb
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
; wd_scrollpaint - move the view with a blit instead of a repaint
; in:  SI = window ptr, wd_bounds run, gfx lock held, [wd_top] ALREADY moved,
;      [wd_dr0]/[wd_dr1] = whatever rows the caller found dirty in the frame
;      the screen is still showing (0xFFFF/0 = none)
; out: CF=0 the screen is correct and both arrays describe it; CF=1 nothing
;      was drawn and the caller must repaint in full. Preserves all registers.
; -----------------------------------------------------------------------------
wd_scrollpaint:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [wd_sigok], 0
    je .nope                        ; the arrays do not describe the screen,
                                    ; so there is nothing to shift
    cmp word [wd_vrows], 2
    jb .nope
    mov ax, [wd_top]
    sub ax, [wd_ptop]               ; AX = d, signed
    jz .nope                        ; the view did not actually move
    mov [wd_sdlt], ax
    mov bx, ax
    or bx, bx
    jns .abs
    neg bx
.abs:
    cmp bx, [wd_vrows]
    jae .nope                       ; nothing is retained: a repaint is the
                                    ; same work without the blit
    cmp byte [wd_hasfmt], 0
    je .pxuni
    or ax, ax                       ; formatted (SPEC.md 65.6): an UP scroll's
    js .nope                        ; entering rows have unknown heights -
                                    ; the full repaint is the honest path
    cmp byte [wd_ymoved], 0         ; ...and so is a scroll riding an edit
    jne .nope                       ; that MOVED rows: pass 1 has already
                                    ; rewritten the banked ys to the new
                                    ; layout, so they no longer describe the
                                    ; glass the blit would move
    cmp byte [wd_rowsok], 0
    je .nope
    cmp ax, [wd_rowsn]
    ja .nope                        ; ryb[d-1] must be a row the glass shows
    mov di, ax
    dec di
    shl di, 1
    mov di, [di+wd_ryb]
    add di, 8                       ; band top of row d - band top of row 0:
    sub di, [wd_ty]                 ; exactly the pixels the retained rows
    jle .nope                       ; move up by
    jmp short .pxhave
.pxuni:
    mov di, ax
    push cx
    mov cl, 3
    shl di, cl                      ; DI = d*8, and a positive d moves the
    pop cx                          ; view down, which moves the text UP
.pxhave:
    mov [wd_sdpx], di               ; wd_shiftrows adjusts the shifted ys by
                                    ; this (SPEC.md 65.6)
    call wd_vshift
    jc .nope                        ; refused, and having drawn nothing

    ; Rounding x2+1 outward carried up to seven columns of furniture with the
    ; text: the scroll bar's left frame at wd_rgt+1, and the left edge of the
    ; grow box below it. Blank that strip and let the two things that own it
    ; put themselves back - wd_sbar at the end of this routine, and the grow
    ; box here, because wd_sbar stops short of the corner it sits in.
    mov ax, [wd_rgt]
    inc ax                          ; x1, the first column past the text
    mov cx, [wd_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                          ; x2, the same one wd_vshift moved
    cmp ax, cx
    ja .nostrip
    mov bx, [wd_ty]
    mov dx, [wd_bot]
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

    mov ax, [wd_sdlt]               ; the rows the blit did not fill in
    or ax, ax
    js .exup
    cmp byte [wd_hasfmt], 0
    je .exdn8
    ; formatted (SPEC.md 65.6): the vacated pixels are the bottom sdpx of
    ; the band, and the rows that own them are found from the PRE-shift
    ; banks - the first retained row whose old glyph crossed the bottom
    ; edge (it was clipped then, it is drawable now), else the first row
    ; past the glass. In ROW terms that is where redrawing must start;
    ; everything below it is redrawn to the view's end.
    mov bx, [wd_rowsn]
    cmp bx, [wd_vrows]
    jbe .fdl
    mov bx, [wd_vrows]
.fdl:
    mov cx, ax                      ; CX = the candidate pre-shift row, from d
.fds:
    cmp cx, bx
    jae .fdnone
    mov di, cx
    shl di, 1
    mov di, [di+wd_ryb]
    add di, 7
    cmp di, [wd_bot]
    ja .fdhit
    inc cx
    jmp short .fds
.fdhit:
.fdnone:
    sub cx, ax                      ; ...as a post-shift visible row
    jns .fd0
    xor cx, cx
.fd0:
    mov [wd_bd0], cx
    mov cx, [wd_vrows]
    dec cx
    mov [wd_bd1], cx
    jmp short .band
.exdn8:
    mov bx, [wd_vrows]              ; view moved DOWN: they are at the bottom
    sub bx, ax
    mov [wd_bd0], bx
    mov bx, [wd_vrows]
    dec bx
    mov [wd_bd1], bx
    jmp short .band
.exup:
    mov word [wd_bd0], 0            ; ...and UP: at the top
    neg ax
    dec ax
    mov [wd_bd1], ax
.band:
    ; ...plus whatever the caller already knew was dirty, which it counted in
    ; the OLD frame. A caret that moved off a row leaves that row needing a
    ; redraw even though the blit carried it faithfully - so an Up that
    ; scrolls has TWO dirty rows, the one it arrived on and the one it left.
    mov ax, [wd_dr0]
    cmp ax, 0xFFFF
    je .shift
    sub ax, [wd_sdlt]
    jns .d0ok
    xor ax, ax                      ; it half scrolled off the top
.d0ok:
    mov bx, [wd_dr1]
    sub bx, [wd_sdlt]
    js .shift                       ; ...or all of it did
    cmp bx, [wd_vrows]
    jb .d1ok
    mov bx, [wd_vrows]
    dec bx
.d1ok:
    cmp ax, bx
    ja .shift
    cmp ax, [wd_bd0]
    jae .hi
    mov [wd_bd0], ax
.hi:
    cmp bx, [wd_bd1]
    jbe .shift
    mov [wd_bd1], bx
.shift:
    call wd_shiftrows

    cmp byte [wd_hasfmt], 0         ; erase the band: OSAPI_GFX_SCROLL leaves
    je .eruni                       ; the vacated rows holding a copy of what
    mov bx, [wd_bot]                ; was next to them. Formatted: the blit
    sub bx, [wd_sdpx]               ; moved [ty..bot] up by sdpx, so EXACTLY
    inc bx                          ; the bottom sdpx pixels are vacated -
    cmp bx, [wd_ty]                 ; derived from the delta itself, never
    jae .erf                        ; from a blank row's banked y (a garbage
    mov bx, [wd_ty]                 ; bank once erased the window's own
.erf:                               ; chrome through this fill)
    mov dx, [wd_bot]
    jmp short .erhave
.eruni:
    mov ax, [wd_bd0]
    push cx                         ; the vacated rows holding a copy of what
    mov cl, 3                       ; was next to them, and a row of the new
    shl ax, cl                      ; text may be shorter than that or empty
    pop cx
    add ax, [wd_ty]
    mov bx, ax                      ; BX = y1
    mov ax, [wd_bd1]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]
    add ax, 7
    cmp ax, [wd_bot]
    jbe .yok
    mov ax, [wd_bot]
.yok:
    mov dx, ax                      ; DX = y2
.erhave:
    mov ax, [wd_tx]
    sub ax, WD_MARGIN               ; AX = x1, the content's own left edge
    mov cx, [wd_rgt]                ; CX = x2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [wd_prowi], 0xFFFF     ; the fill erased what the delta cache knew

    mov word [wd_hity], 0xFFFF      ; one pass, drawing AND re-signing: the
    mov word [wd_wanty], 0x7FFF     ; band was just filled, so wd_clean, and
    mov ax, [wd_bd0]                ; wd_clip confines it to the band by ROW
    mov [wd_dr0], ax                ; rather than by signature - an exposed
    mov ax, [wd_bd1]                ; row's old signature is the row that
    mov [wd_dr1], ax                ; scrolled away and could match by luck
    mov byte [wd_draw], 1
    mov byte [wd_sigup], 1
    mov byte [wd_clip], 1
    mov byte [wd_clean], 1
    cmp byte [wd_hasfmt], 0         ; the formatted band includes retained
    je .clok                        ; rows ABOVE the erased pixels: their
    mov byte [wd_clean], 0          ; runs must pad, or an old caret bar past
.clok:                              ; the text survives (SPEC.md 65.6)
    mov byte [wd_resume], 0
    cmp byte [wd_rowsok], 0
    je .xseed
    mov ax, [wd_bd0]
    or ax, ax
    jz .xseed                       ; the band starts at the top of the view:
    dec ax                          ; there is no earlier row to start from
    mov bx, ax                      ; wd_rows is valid up to here and no
    inc bx                          ; further, the entries above the band
    mov [wd_rowsn], bx              ; having just been shifted out of range
    mov dx, [wd_vrows]
    call wd_seedrow
    cmp byte [wd_resume], 0
    jne .noseed
.xseed:
    mov ax, [wd_top]                ; ...and the ROW INDEX has one (SPEC.md
    mov dx, [wd_vrows]              ; 27.13). This is every scroll UPWARD: the
    call wd_xseed                   ; exposed row is row 0 of the view, so
                                    ; there is nothing above it in wd_rows and
                                    ; the walk went back to index 0 to letter
                                    ; ONE row - 335 ms of a 640 ms Up
.noseed:
    mov ax, [wd_bd1]                ; STOP AT THE BAND, not at the bottom of
    mov [wd_lastrow], ax            ; the view (SPEC.md 27.13). Nothing below
                                    ; wd_bd1 is drawn - wd_clip says so - and
                                    ; nothing below it needs LAYING OUT
                                    ; either: OSAPI_GFX_SCROLL moved those
                                    ; pixels and wd_shiftrows moved wd_sig and
                                    ; wd_rows by the same d, so their
                                    ; descriptions already match the glass.
                                    ; Walking to the view's bottom to draw one
                                    ; exposed row was 20 rows for 1 - 333 ms of
                                    ; a 520 ms Up
    call wd_walk
    mov byte [wd_clip], 0
    mov byte [wd_clean], 0

    mov ax, [wd_top]
    mov [wd_ptop], ax               ; the screen shows this view now
    call wd_sbar                    ; unconditional: the thumb moved, and the
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
; wd_ctcalc - the live chrome top: menu bar + the strips View has switched on
; out: [wd_ctop] refreshed; preserves all registers (SPEC.md 65.2)
; -----------------------------------------------------------------------------
wd_ctcalc:
    push ax
    mov ax, WD_MENU_H
    cmp byte [wd_vrib], 0
    je .norib
    add ax, WD_RIBBON_H
.norib:
    cmp byte [wd_vrul], 0
    je .norul
    add ax, WD_RULER_H
.norul:
    mov [wd_ctop], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_bounds - the content rectangle and the text origin, from the live record
; in:  SI = window ptr, ES = KERNEL_SEG (as every callback is entered)
; out: [wd_tx] = the wrap column and left margin, [wd_ty] = the first text
;      row, [wd_rgt]/[wd_bot] = the content's inclusive right and bottom;
;      preserves all registers
;
; A resizable window lays out from the record every time (SPEC.md 11.1), and
; BOTH passes of wd_walk need the same four numbers, so they are read once
; here rather than twice in slightly different words.
; -----------------------------------------------------------------------------
wd_bounds:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [wd_cl], ax                 ; the content box, banked for every chrome
    mov [wd_ct], dx                 ; painter and hit test (SPEC.md 65.2) -
                                    ; read here once, exactly like wd_tx/wd_ty
    push ax
    push dx
    add ax, WD_MARGIN
    mov [wd_tx], ax
    call wd_ctcalc                  ; [wd_ctop] = the LIVE chrome top from the
    add dx, [wd_ctop]               ; View toggles (SPEC.md 65.2): menu bar,
                                    ; then ribbon and ruler if shown. (The
                                    ; find panel that used to dock here is
                                    ; gone - Search is a dialog, SPEC.md 65.7)
    add dx, WD_MARGIN               ; wd_bounds's own geometry test below
    mov [wd_ty], dx                 ; catches a chrome-top change and
                                    ; wd_sigsame turns it into a full repaint
    call OSAPI_WM_GEOM              ; CX/DX = content w/h (BX still the window)
    mov [wd_cw], cx                 ; banked beside wd_cl/wd_ct
    mov [wd_ch], dx
    pop ax                          ; content top
    add ax, dx
    dec ax                          ; the content's own last row...
    cmp byte [wd_vsta], 0           ; ...less the status strip when it is
    je .nosta                       ; shown (SPEC.md 65.2): the text band ends
    sub ax, WD_STATUS_H             ; above it, and the kernel's grow box
.nosta:                             ; lives in the strip's right corner where
    mov [wd_bot], ax                ; the bar already stopped clear of it
    pop ax                          ; content left
    add ax, cx
    dec ax
    mov [wd_sbr], ax                ; ...and the last drawable column.
    sub ax, WD_SB_W                 ; The scroll bar owns the rightmost
                                    ; WD_SB_W of it (SPEC.md 27.7). This was
    call wd_pgcol                   ; Page view narrows the column to the SHEET
                                    ; before anything derives from it
    mov [wd_rgt], ax                ; W_X+W_W-2 read off the record through ES;
                                    ; origin + size - 1 is the same pixel and
                                    ; needs no kernel pointer of our own - and
                                    ; it stays right under WF_FULL, where the
                                    ; frame IS the content (SPEC.md 11.2)

    mov ax, [wd_rgt]                ; whole 8px CELLS between the pen and the
    sub ax, [wd_tx]                 ; right edge: the width of one opaque run,
    inc ax                          ; and what the row buffer is padded to
    jns .cok
    xor ax, ax
.cok:
    mov cl, 3
    shr ax, cl
    cmp ax, WD_MAXCOL - 1
    jbe .csave
    mov ax, WD_MAXCOL - 1
.csave:
    mov [wd_rcols], ax

    mov ax, [wd_bot]                ; ...and how many whole 8px rows that is,
    sub ax, [wd_ty]                 ; which is what the signature array is
    jc .norows                      ; indexed by (SPEC.md 27.2)
    cmp ax, 7
    jb .norows
    sub ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    inc ax
    cmp ax, WD_MAXROWS
    jbe .vok
    mov ax, WD_MAXROWS
.vok:
    mov [wd_vrows], ax
    jmp short .sbb
.norows:
    mov word [wd_vrows], 0
.sbb:
    mov ax, [wd_vrows]              ; [wd_vfit]: rows GUARANTEED to fit the
    cmp byte [wd_hasfmt], 0         ; band. Uniform rows: all of them. With
    je .vfok                        ; formats: band/24, the tallest row being
    mov ax, [wd_bot]                ; double spacing + open space (SPEC.md
    sub ax, [wd_ty]                 ; 65.6) - conservative, so wd_seecaret's
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
    mov [wd_vfit], ax
    mov ax, [wd_bot]                ; the bar's own bottom, clear of the corner
    sub ax, WD_GROW                 ; the kernel draws the grow box in
    mov [wd_sbb], ax
    call wd_hguess                  ; ...and now the geometry is known, what the
                                    ; note's LENGTH already says about its
                                    ; height (SPEC.md 27.7.3)
.geom:
    ; The checkpoint and wd_rows are ROW INDICES, so they mean nothing under a
    ; different geometry - and unlike the signatures, nothing else was going to
    ; notice. wd_sigsame guards wd_redraw; this guards everything else, which
    ; is every caret key and every click.
    ; ...and what "a different geometry" means here is the WRAP WIDTH and the
    ; view HEIGHT, never the origin. These four words are the content box in
    ; SCREEN coordinates, so dragging the window changes wd_tx and wd_ty while
    ; the note wraps identically - and comparing them absolutely made every
    ; MOVE set [wd_gchg], which wd_paint pays with wd_measure: an unbounded
    ; walk of the whole note. On a 16KB file that is seconds of a window that
    ; has been dropped and is not yet drawing, reported as exactly that. The
    ; comment below is a RESIZE argument and always was.
    mov ax, [wd_rgt]
    sub ax, [wd_tx]                 ; the wrap width now...
    mov dx, [wd_srgt]
    sub dx, [wd_stx]                ; ...against the one the screen was laid
    cmp ax, dx                      ; out under
    jne .stale
    mov ax, [wd_bot]
    sub ax, [wd_ty]                 ; ...and the view height, which decides how
    mov dx, [wd_sbot]               ; many of those rows fit and so whether the
    sub dx, [wd_sty]                ; view is looking past the end
    cmp ax, dx
    je .out
.stale:
    mov byte [wd_ckok], 0
    mov byte [wd_rowsok], 0
    mov byte [wd_gchg], 1           ; ...and the note is a different NUMBER of
                                    ; rows under a different wrap width, so
                                    ; the view can be looking past the end of
                                    ; it. Only a walk knows how many, so this
                                    ; says "one is owed" and wd_paint pays it
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Paragraph layout state (SPEC.md 65.3/65.6)
;
; The walk carries the GOVERNING paragraph's decoded format beside the pen:
; the format lives on the ¶ that ENDS the paragraph, so entering a paragraph
; scans forward to its mark once (each byte of the walked span is therefore
; read at most twice, which is the price of Word's own arrangement). While
; [wd_hasfmt] is clear - every document until a format is applied - none of
; this runs and the walk is byte-for-byte the uniform engine it always was.
;
; Heights: a row's advance is 8/12/16px from the line spacing, +8 before an
; open paragraph's first row. BP stays the GLYPH y (the 8px band every
; drawing routine already reads); the row's BAND is [wd_rbandt .. BP+7], and
; wd_ryb banks each visible row's glyph y so pass 1 can see a row that moved
; without its text changing, and a seeded walk can resume at an exact y.
; Rows above the view all park at the ty-8 sentinel - wd_rflush's y test
; already refuses them, and their exact y answers no query.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_papat - the governing PAP index at character index AX
; out: AL = it (AH preserved); preserves every other register
; The scan is one repne scasb to the paragraph's own ¶ - O(paragraph), and
; only when [wd_hasfmt] says a non-Normal format exists at all.
; -----------------------------------------------------------------------------
wd_papat:
    push bx
    push cx
    push di
    push es
    cmp byte [wd_hasfmt], 0
    je .zero
    mov cx, [wd_len]
    sub cx, ax
    jbe .tail                       ; at/past the end: the tail paragraph
    mov es, [wd_dseg]
    mov di, ax
    push ax
    mov al, 13
    cld
    repne scasb
    pop ax
    jne .tail
    mov es, [wd_cseg]               ; the ¶'s CHP byte IS the index
    mov bx, di
    dec bx
    mov al, [es:bx]
    jmp short .out
.tail:
    mov al, [wd_pap_tail]
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
; wd_pback - the start of the paragraph containing index AX
; out: AX = the index after the previous ¶ (or 0); preserves all others
; -----------------------------------------------------------------------------
wd_pback:
    push bx
    push es
    mov es, [wd_dseg]
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
; wd_papload - decode dictionary entry AL into the walk's wd_w* state
; preserves all registers. Clamped against the LIVE geometry: at least one
; cell survives every combination of indents, and the clamped pens stay on
; byte columns so the single-store paths survive (SPEC.md 65.1).
; -----------------------------------------------------------------------------
wd_papload:
    push ax
    push bx
    push cx
    push dx
    push es
    mov [wd_wpap], al
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov es, [wd_pseg]
    mov dl, [es:bx]                 ; DL = the packed byte
    mov al, dl
    and al, WDPA_ALIGN
    mov [wd_walign], al
    mov al, dl                      ; advance = 8 + spacing*4, and the packed
    and al, WDPA_SPACE              ; field is already spacing << 2
    add al, [wd_ghb]                ; ...over the FACE's own row advance, which
    mov [wd_wadv], al               ; is 8 for the kernel's cell (SPEC.md 65.13)
    xor al, al
    test dl, WDPA_SB
    jz .nosb
    inc al
.nosb:
    mov [wd_wsb], al
    mov al, [es:bx+3]               ; right indent -> the effective right edge
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mov dx, [wd_rgt]
    sub dx, ax
    mov ax, [wd_tx]
    add ax, 7
    cmp dx, ax
    jge .rok
    mov dx, ax                      ; degenerate window: one cell stands
.rok:
    mov [wd_wrgt], dx
    mov al, [es:bx+1]               ; left indent px, clamped to keep a cell
    xor ah, ah
    shl ax, cl
    mov dx, [wd_wrgt]
    sub dx, [wd_tx]
    sub dx, 7
    jns .lc
    xor dx, dx
.lc:
    cmp ax, dx
    jbe .lok
    mov ax, dx
    and ax, 0xFFF8
.lok:
    mov [wd_wleftpx], ax
    mov dx, [wd_wrgt]               ; the fresh-row capacity, cells
    inc dx
    sub dx, [wd_tx]
    sub dx, ax
    jns .cc
    xor dx, dx
.cc:
    shr dx, cl
    or dx, dx
    jnz .c1
    inc dx
.c1:
    cmp dx, [wd_rcols]
    jbe .cok
    mov dx, [wd_rcols]
.cok:
    mov [wd_wcols], dx
    mov al, [es:bx+2]               ; first-line, SIGNED, clamped so the pen
    cbw                             ; lands in [tx .. wrgt-7]
    shl ax, cl
    mov dx, [wd_wleftpx]
    add dx, ax
    jns .f0
    xor dx, dx
.f0:
    mov ax, [wd_wrgt]
    sub ax, [wd_tx]
    sub ax, 7
    jns .f1
    xor ax, ax
.f1:
    cmp dx, ax
    jbe .f2
    mov dx, ax
    and dx, 0xFFF8
.f2:
    sub dx, [wd_wleftpx]
    mov [wd_wfirstpx], dx
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_papload0 - Normal, without touching the dictionary: the uniform path's
; whole cost. Preserves all registers.
wd_papload0:
    push ax
    xor al, al
    mov [wd_wpap], al
    mov [wd_walign], al
    mov [wd_wsb], al
    mov al, [wd_ghb]
    mov [wd_wadv], al
    xor ax, ax
    mov [wd_wleftpx], ax
    mov [wd_wfirstpx], ax
    mov ax, [wd_rgt]
    mov [wd_wrgt], ax
    mov ax, [wd_rcols]
    mov [wd_wcols], ax
    pop ax
    ret

; wd_papini - the walk is starting at [wd_i]: load the governing format and
; decide whether this row is its paragraph's first. ES = the document.
; Preserves all registers.
wd_papini:
    push ax
    push bx
    mov byte [wd_parafirst], 1
    mov bx, [wd_i]
    or bx, bx
    jz .first
    cmp byte [es:bx-1], 13
    je .first
    mov byte [wd_parafirst], 0
.first:
    cmp byte [wd_hasfmt], 0
    jne .fmt
    call wd_papload0
    jmp short .out
.fmt:
    mov ax, [wd_i]
    call wd_papat
    call wd_papload
.out:
    pop bx
    pop ax
    ret

; wd_rowhc - [wd_rowhv] = the entered row's height. Preserves all registers.
wd_rowhc:
    push ax
    mov al, [wd_wadv]
    xor ah, ah
    cmp byte [wd_parafirst], 0
    je .have
    cmp byte [wd_wsb], 0
    je .have
    add ax, 8                       ; the open paragraph's blank line
.have:
    mov [wd_rowhv], ax
    pop ax
    ret

; wd_advy - the pen enters row [wd_row]+1: move the glyph y by the ENTERED
; row's height ([wd_rowhv]). Rows above the view park at the ty-8 sentinel.
; Preserves all registers but BP (its whole job).
wd_advy:
    push ax
    mov ax, [wd_row]
    inc ax
    js .neg
    jz .zero
    add bp, [wd_rowhv]
    jmp short .band
.zero:
    mov bp, [wd_ty]                 ; band top of row 0 is wd_ty exactly
    add bp, [wd_rowhv]
    sub bp, [wd_gh]
    jmp short .band
.neg:
    mov bp, [wd_ty]
    sub bp, [wd_gh]
.band:
    mov ax, bp
    sub ax, [wd_rowhv]
    add ax, [wd_gh]
    mov [wd_rbandt], ax
    pop ax
    ret

; the three row-advance shapes: a wrap stays in the paragraph, a newline
; enters a fresh one (whose format is scanned then), a blank row is plain
wd_advwrap:
    mov byte [wd_parafirst], 0
    call wd_rowhc
    jmp wd_advy

wd_advnl:
    push ax
    mov byte [wd_parafirst], 1
    cmp byte [wd_hasfmt], 0
    je .have
    mov ax, [wd_i]
    call wd_papat
    call wd_papload
.have:
    call wd_rowhc
    call wd_advy
    pop ax
    ret

wd_advblank:
    mov word [wd_rowhv], 8
    jmp wd_advy

; -----------------------------------------------------------------------------
; wd_tabw - the tab's width in px: to the next WD_TABSTOP-cell default stop,
;           anchored at the row's own start pen [wd_rowx0]
; in:  DI = the pen; out: AX = pixels (8..WD_TABSTOP*8); preserves all others
; -----------------------------------------------------------------------------
wd_tabw:
    push cx
    push dx
    mov ax, di
    sub ax, [wd_rowx0]
    mov cl, 3
    shr ax, cl                      ; cells consumed on this row
    mov cx, ax
    xor dx, dx
    mov ax, cx
    push cx
    mov cx, WD_TABSTOP
    div cx
    inc ax
    mul cx                          ; the next stop, cells
    pop cx
    sub ax, cx
    mov cl, 3
    shl ax, cl
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; wd_rowmeasure - how many cells will the row starting at this pen hold?
; in:  ES:SI = the document at [wd_i], BX = chars left, DI = the row's start
;      pen (indents applied, NO alignment offset), pap state loaded
; out: AX = the cells; preserves every other register AND [wd_wstart]
;
; The centre/right offset needs the row's width BEFORE the row is laid out,
; and the walk is one pass - so the width comes from this dry run, made
; through the SAME two helpers (wd_wordfit / wd_hangsp) as the live loop so
; the two cannot drift. The offset the caller then adds cannot overflow the
; row: off <= free, and every word that fitted unoffset still fits offset
; (u + len <= cells and off + cells <= wcols, both by construction).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wd_fontscan - build the Font combo from what the MACHINE has (SPEC.md 19.8)
; in:  -
; out: the dropdown's items and its count set; preserves all registers
;
; ONCE, and lazily. ty_scan is four remounts and two listings - a couple of
; seconds on the target - so it runs the first time somebody opens this combo
; and never again. A person who never opens it pays nothing, which is the same
; bargain SPEC.md 6.2 strikes with a directory of faces nobody picks from.
;
; The dropdown is a STATIC table with room reserved (wd_it_fontc), and this
; fills the reserved records and writes the count byte in wd_mtab. A menu whose
; length is data rather than assembly is a menu that can grow when a disk
; carries more faces, without this program knowing their names.
; -----------------------------------------------------------------------------
wd_fontscan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [wd_fscan], 0
    jne .out
    mov byte [wd_fscan], 1          ; once, whatever the answer - a disk with
                                    ; no FONTS/ must not be re-walked on every
                                    ; press
    call ty_scan
    mov al, [ty_nfam]
    or al, al
    jz .out
    cmp al, WD_MAXFONT
    jbe .n
    mov al, WD_MAXFONT
.n:
    mov [wd_nfont], al
    xor ch, ch
    mov cl, al
    xor bx, bx                      ; BX = the family index
.item:
    mov ax, bx
    mov si, WD_MI_SZ                ; the record for item 1 + BX: item 0 is
    mul si                          ; Pica and is assembled, not filled in
    mov di, wd_it_fontc + WD_MI_SZ
    add di, ax
    mov byte [di+0], 0              ; flags: live
    mov byte [di+1], 0              ; no mnemonic index
    mov byte [di+2], WDA_CSEL
    mov byte [di+3], 0
    mov al, bl
    call ty_famname                 ; SI = the display name the scan built
    mov [di+4], si
    mov word [di+6], 0              ; no caption
    inc bx
    loop .item

    mov al, [wd_nfont]              ; ...and the dropdown is that many items
    inc al                          ; longer than the one Pica it had
    mov [wd_mtab + WD_M_FONTC * WD_MT_SZ + 3], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_facemetrics - the three numbers the layout reads, from the current face
; in:  [wd_prop] and the library's current face
; out: [wd_gh]/[wd_gh1]/[wd_ghb]; preserves all registers
;
; AND IT FORCES [wd_hasfmt], which is the whole reason this change is small.
; That flag already means "rows are not all 8 pixels and do not land at
; wd_ty + 8*row" - it is what line spacing set - and twenty-three sites in
; this program already branch on it to a path that handles arbitrary row ys:
; the scroll band, the erase band, the row-fit count, the caret's visibility
; test. A taller face needs exactly those paths, so it raises exactly that
; flag rather than growing a second predicate beside it that every one of
; them would have had to learn.
; -----------------------------------------------------------------------------
wd_facemetrics:
    push ax
    mov ax, 8                       ; the kernel's cell, and the default
    mov [wd_gh], ax
    dec ax
    mov [wd_gh1], ax
    mov word [wd_ghb], 8
    cmp byte [wd_prop], 0
    je .out
    call ty_getrows
    xor ah, ah
    mov [wd_gh], ax
    dec ax
    mov [wd_gh1], ax
    call ty_getrows
    xor ah, ah
    mov [wd_ghb], ax
    call ty_getlead
    xor ah, ah
    add [wd_ghb], ax
    cmp word [wd_ghb], 8            ; ...and ONLY a face whose row advance is
    je .out                         ; not 8 needs the non-uniform paths. An
    mov byte [wd_hasfmt], 1         ; 8-row face changes no geometry at all -
.out:                               ; same pitch, same caret, same clicks, and
                                    ; only the glyph shapes move
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_facedrop - give back the face this document had open, if any
; in:  [wd_face]; out: it is 0; preserves all registers
; -----------------------------------------------------------------------------
wd_facedrop:
    push ax
    mov al, [wd_face]
    or al, al
    jz .out
    call ty_close
    mov byte [wd_face], 0
    xor ax, ax                      ; ...and the CURRENT face goes back to the
    call ty_use                     ; kernel's 8x8, so nothing is left pointing
.out:                               ; at a slot that has just been freed
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_a_csel - a combo entry was chosen (SPEC.md 65.13)
; in:  [wd_pickm] = which combo, [wd_picki] = which entry
; out: the Font combo's caption follows the choice; the others are cosmetic
;
; ITEM 0 IS PICA - the kernel's 8x8 cell, which is what this program has
; always set text in and is a perfectly good answer. Items 1.. are the faces
; FONTS/ was carrying. Choosing one opens it and names it in the ribbon.
;
; WHAT IT DOES NOT DO YET is set [wd_prop]: the document still draws through
; the 8x8 cell until wd_drawrun grows its band arm (SPEC.md 65.13), and a
; half-converted Word - drawing proportionally while measuring on the 8-pixel
; grid - would put the caret in the wrong place on every line. So the choice
; is recorded and the face is opened, and the pixels follow when the rest of
; 65.13 lands.
; -----------------------------------------------------------------------------
wd_a_csel:
    push ax
    push bx
    push si                         ; SI IS THE WINDOW POINTER AND MUST SURVIVE
    push di                         ; (wd_mfire's contract: "SI survives every
                                    ; action"). ty_famname ANSWERS in SI, so
                                    ; without this the caller is handed a name
                                    ; string where a window record belongs and
                                    ; the machine follows it into the weeds -
                                    ; observed as a hang with CS:IP parked on
                                    ; this package's own entry point
    cmp byte [wd_pickm], WD_M_FONTC
    jne .out
    mov al, [wd_picki]
    or al, al
    jnz .face
    call wd_facedrop                ; back to the built-in cell
    mov word [wd_fcap], wd_s_pica
    mov byte [wd_fsel], 0
    mov byte [wd_prop], 0
    jmp short .reflow               ; ...and the note is drawn again in it:
                                    ; going BACK to the cell is as much a
                                    ; change as leaving it
.face:
    cmp al, [wd_nfont]
    ja .out
    dec al
    push ax
    call wd_facedrop                ; ...and a face slot is not left behind by
                                    ; every visit to this menu: there are four
                                    ; and face 0 is one of them, so without
                                    ; this the THIRD pick answers TYE_NOSLOT
                                    ; and the combo quietly stops working
    pop ax
    push ax
    call ty_openfam                 ; opened NOW rather than at draw time: a
    pop bx                          ; face that will not read should say so
    jc .out                         ; while the person is still looking at the
    mov [wd_face], al               ; menu they picked it from - and the BOX
    xor ah, ah                      ; is not renamed until it has, so the name
    call ty_use                     ; in the ribbon is EVIDENCE that the face
    call ty_cache                   ; is open and not just that it was asked
    mov al, bl                      ; for
    mov [wd_fsel], al
    call ty_famname
    mov [wd_fcap], si
%if WD_PROPDRAW
    mov byte [wd_prop], 1           ; ...and the document is SET in it - which
%endif                              ; is off until the three things named at
                                    ; WD_PROPDRAW are done
.reflow:
    call wd_facemetrics             ; the glyph band and the row advance follow
                                    ; the face, so every cached row signature
                                    ; describes a layout that may no longer
                                    ; exist - all four caches go
    mov byte [wd_sigok], 0
    mov word [wd_prowi], 0xFFFF
    mov byte [wd_ckok], 0
    mov byte [wd_rowsok], 0
    mov byte [wd_redrw], 1
.paint:
    cmp byte [wd_vrib], 0           ; ...and the BOX has to be lettered again.
    je .out                         ; wd_mfire runs AFTER wd_mclose has put the
    call wd_ribbon                  ; rows the dropdown covered back, so the
                                    ; caption this just changed was drawn a
                                    ; moment before it changed. One strip,
                                    ; priced in calls, not a window
.out:
    pop di
    pop si
    pop bx
    pop ax
    cmp byte [wd_redrw], 0
    je .ret
    mov byte [wd_redrw], 0
    jmp wd_redraw                   ; A TAIL JUMP, which is how every other
                                    ; action in this program reaches the
                                    ; redraw - wd_a_new, wd_a_undo, wd_a_cut
                                    ; and the rest all `jmp wd_redraw` rather
                                    ; than call it. Calling it from inside the
                                    ; handler left the chrome drawn at two
                                    ; geometries at once
.ret:
    ret

; -----------------------------------------------------------------------------
; wd_cx - the absolute screen x of a cell (SPEC.md 65.13)
; in:  AX = a cell index, 0..[wd_rcols]
; out: AX = its x; preserves every other register
;
; In the 8x8 cell the k-th glyph is at [wd_tx] + 8k and there is nothing to
; look up. In a proportional face it is [wd_tx] + wd_px[k], which the
; accumulate walk recorded as it stored the cell. EVERY cell-to-pixel site in
; this program goes through here, so the two faces differ in one routine
; instead of in thirty - and with [wd_prop] clear this is the same three
; shifts those sites used to do inline.
; -----------------------------------------------------------------------------
wd_cx:
    push bx
    cmp byte [wd_pxon], 0           ; the METRICS flag, not [wd_prop]: a face
    jne .prop                       ; is drawn on the 8-pixel grid first and
                                    ; its own advances are honoured after
                                    ; (SPEC.md 65.13), and only the second of
                                    ; those two makes wd_px[] the truth
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, bx
    jmp short .add
.prop:
    cmp ax, WD_MAXCOL + 1           ; the map has one slot past the last cell,
    jbe .in                         ; so a span's right edge is a lookup
    mov ax, WD_MAXCOL + 1
.in:
    mov bx, ax
    shl bx, 1
    mov ax, [wd_px + bx]
.add:
    add ax, [wd_tx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_xc - the cell an x falls in - wd_cx's inverse (SPEC.md 65.13)
; in:  AX = an absolute x
; out: AX = the cell index; preserves every other register
;
; FLOOR, not nearest: this answers "which cell holds this pixel", which is
; what the fixed-face `(x - tx) >> 3` it replaces answered, and every caller
; is written against that. A caret that should land on the nearer EDGE is
; ty_hit's question and not this one.
; -----------------------------------------------------------------------------
wd_xc:
    push bx
    push cx
    sub ax, [wd_tx]
    jns .in
    xor ax, ax                      ; left of the row's first cell
    jmp short .out
.in:
    cmp byte [wd_pxon], 0
    jne .prop
    mov cl, 3
    shr ax, cl
    jmp short .out
.prop:
    xor bx, bx
.w:
    cmp bx, [wd_rcols]
    jae .have
    push bx
    shl bx, 1
    mov cx, [wd_px + bx + 2]        ; ...the NEXT cell's pen: x belongs to
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

wd_rowmeasure:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov al, [wd_wstart]
    push ax                         ; the live word state, restored whole
    mov bp, di                      ; BP = the start pen (borrowed: no y here)
.loop:
    or bx, bx
    jz .done
    mov al, [es:si]
    cmp al, 13
    je .done
    mov cx, di
    add cx, 7
    cmp cx, [wd_wrgt]
    ja .over
    call wd_wordfit
    jc .done
    jmp short .take
.over:
    call wd_hangsp
    jnc .done
.take:
    cmp al, 9
    je .tab
    cmp byte [wd_hashid], 0         ; hidden occupies no cell (SPEC.md 65.1)
    je .vis
    push es
    push bx
    mov bx, si
    mov es, [wd_cseg]
    mov ah, [es:bx]
    pop bx
    pop es
    test ah, WDAT_HID
    jnz .adv0
.vis:
    add di, 8
.adv0:
    mov byte [wd_wstart], 0
    cmp al, ' '
    jne .ws
    mov byte [wd_wstart], 1
.ws:
    inc si
    dec bx
    jmp short .loop
.tab:
    call wd_tabw                    ; anchored at [wd_rowx0], which the caller
                                    ; set to THIS start pen before calling
    add di, ax
    mov byte [wd_wstart], 1
    inc si
    dec bx
    jmp short .loop
.done:
    mov ax, di
    sub ax, bp
    mov cl, 3
    shr ax, cl
    pop cx                          ; the saved word state
    mov [wd_wstart], cl
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_rowsetup - the pen for the row just entered: indents, then the centre/
;               right offset (SPEC.md 65.1: rounded DOWN to whole cells)
; in:  ES:SI/BX = the walk at the row's first char, pap state loaded
; out: DI = the pen, [wd_rowx0] = it (the tab anchor); preserves all others
; -----------------------------------------------------------------------------
wd_rowsetup:
    push ax
    push cx
    push dx
    mov di, [wd_tx]
    cmp byte [wd_hasfmt], 0
    je .anchor
    add di, [wd_wleftpx]
    cmp byte [wd_parafirst], 0
    je .nof
    add di, [wd_wfirstpx]
    cmp di, [wd_tx]
    jge .nof
    mov di, [wd_tx]                 ; belt: wd_papload clamped this already
.nof:
    mov [wd_rowx0], di
    mov al, [wd_walign]
    cmp al, 1
    je .meas                        ; centred and right earn the dry run;
    cmp al, 2                       ; justified renders as left (65.1)
    jne .anchor
.meas:
    call wd_rowmeasure              ; AX = the row's cells
    mov dx, [wd_wrgt]
    inc dx
    sub dx, di
    mov cl, 3
    shr dx, cl                      ; cells available from this pen
    sub dx, ax
    jle .anchor                     ; full (or hanging): no offset
    cmp byte [wd_walign], 2
    je .roff
    shr dx, 1                       ; centred: half the free cells
.roff:
    mov cl, 3
    shl dx, cl
    add di, dx
.anchor:
    mov [wd_rowx0], di
    cmp byte [wd_hasfmt], 0
    je .nofold
    push ax                         ; fold the pen and the height into the
    mov ax, di                      ; row's signature: alignment, indents and
    xor ax, 0x3C3C                  ; spacing move pixels without changing a
    call wd_fold                    ; character, and a stale row is exactly a
    mov ax, [wd_rowhv]              ; row whose fold did not change (the xor
    call wd_fold                    ; keeps an x from aliasing a char code)
    pop ax
.nofold:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_yrow - which visible row owns pixel y = DX?
; out: CF=0 and AX = the row; CF=1 = cannot say, and the caller falls back to
;      the unseeded path - slow and never wrong. Uniform documents answer by
;      arithmetic; formatted ones scan wd_ryb, which describes the glass.
; -----------------------------------------------------------------------------
wd_yrow:
    push bx
    push cx
    mov ax, dx
    sub ax, [wd_ty]
    jc .no
    cmp byte [wd_hasfmt], 0
    jne .scan
    mov cl, 3
    shr ax, cl
    jmp short .yes
.scan:
    cmp byte [wd_rowsok], 0
    je .no
    mov cx, [wd_rowsn]
    cmp cx, [wd_vrows]
    jbe .lim
    mov cx, [wd_vrows]
.lim:
    jcxz .no
    xor ax, ax
.l:
    mov bx, ax
    shl bx, 1
    mov bx, [bx+wd_ryb]
    add bx, [wd_gh1]
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
; wd_walk - THE layout pass: one loop, two jobs (SPEC.md 27)
; in:  [wd_bounds] already run; [wd_draw] = 1 to paint, 0 to measure only;
;      [wd_cur]; the two optional queries below
; out: [wd_curx]/[wd_cury] = where the caret sits, in pixels
;      [wd_hiti]  = the character index the point [wd_hitx],[wd_hity] falls
;                   on ([wd_hity] = 0xFFFF disables the test)
;      [wd_wanti] = the index at column [wd_wantx] of row [wd_wanty]
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
; Every index 0..[wd_len] is visited, including the one PAST the last
; character - that is where the caret lives in a note that ends in text, so
; it has to be a position the queries can return.
;
; The caret occupies a cell and therefore wraps like one, which is what keeps
; it in front of the character it precedes rather than stranded at the end of
; the row above.
; -----------------------------------------------------------------------------
wd_walk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov word [wd_curx], 0
    mov word [wd_cury], 0
    mov word [wd_currow], 0
    mov byte [wd_curseen], 0        ; ...and 0 is a REAL pen y on a fullscreen
                                    ; window, so "did this walk find it" needs
                                    ; a flag of its own now that a walk may
                                    ; stop before the caret (SPEC.md 27.7)
    mov ax, [wd_len]
    mov [wd_hiti], ax               ; a click past the end lands at the end
    mov word [wd_wanti], 0xFFFF     ; ...but a row that does not exist has no
    mov byte [wd_hitset], 0         ; answer, and the caller keeps its caret
    mov byte [wd_wantset], 0

    mov di, [wd_tx]                 ; DI = pen x
    mov word [wd_i], 0
    mov ax, [wd_top]                ; wd_row is the VISIBLE row, so the note's
    neg ax                          ; first row sits wd_top rows ABOVE the top
    mov [wd_row], ax                ; of the view and its index is NEGATIVE
    mov word [wd_rowh], 0           ; (SPEC.md 27.7). Everything that indexes
                                    ; an array by it already tests UNSIGNED
                                    ; against a limit, and a negative word
                                    ; read as unsigned is past all of them
    mov byte [wd_wstart], 1         ; whatever this walk starts on begins a
                                    ; word as far as it can tell (SPEC.md
                                    ; 27.11). A seeded walk may in fact resume
                                    ; inside one, and cannot be wrong for it:
                                    ; a seed is always a row START, where the
                                    ; word test never breaks anyway
    mov bx, [wd_len]                ; BX = characters remaining
    xor si, si                      ; ES:SI = the document (SPEC.md 27.6), and
    mov es, [wd_dseg]               ; ES survives every callee below: wd_rstart
                                    ; and wd_rflush push it around their own
    cmp byte [wd_resume], 0         ; ...or start part-way in: at the caret's
    je .seeded                      ; own row for a keystroke (SPEC.md 27.4),
    mov ax, [wd_sdi]                ; or at any row wd_rows named (SPEC.md
    cmp ax, [wd_len]                ; 27.5). Everything before the seed laid
    ja .seeded                      ; out identically last time and cannot have
                                    ; moved, so its signatures still stand and
                                    ; its pixels are on screen
    cmp byte [wd_hasfmt], 0         ; ...but a FORMATTED resume also trusts
    je .rok                         ; the banked y of the row above the seed
    mov dx, [wd_sdr]                ; (SPEC.md 65.6), and a stale bank would
    or dx, dx                       ; draw every row at a lie. Verify it is a
    jle .rok                        ; y the band could hold; when it is not,
    cmp dx, [wd_vrows]              ; fall back to the unseeded walk - slow
    jae .rok                        ; and never wrong. (sdr <= 0 rides the
    dec dx                          ; sentinel and sdr >= vrows the measure
    shl dx, 1                       ; path: neither reads a bank)
    push bx
    mov bx, dx
    mov dx, [bx+wd_ryb]
    pop bx
    cmp dx, [wd_ty]
    jb .seeded
    cmp dx, [wd_bot]
    ja .seeded
.rok:
    mov [wd_i], ax
    add si, ax
    sub bx, ax
    mov ax, [wd_sdr]
    mov [wd_row], ax
.seeded:
    call wd_papini                  ; the governing paragraph format and the
    call wd_rowhc                   ; row's first-of-paragraph-ness, then its
                                    ; height (SPEC.md 65.3/65.6)
    mov ax, [wd_row]
    or ax, ax
    js .ynegs                       ; above the view: rows park at the ty-8
    jz .yzero                       ; sentinel (wd_rflush refuses y < ty)
    cmp byte [wd_hasfmt], 0
    jne .yfmt
    mov cl, 3                       ; uniform: wd_ty + 8*row, exactly as ever
    shl ax, cl
    add ax, [wd_ty]
    mov bp, ax
    jmp short .yhave
.yfmt:
    cmp ax, [wd_vrows]
    jae .ydeep
    dec ax                          ; formatted: glyph y = ryb[row-1] + h -
    shl ax, 1                       ; the rows above the seed are exactly the
    push bx                         ; ones the seed's licence says stood still
    mov bx, ax                      ; (SPEC.md 27.4/65.6), so their banked ys
    mov bp, [bx+wd_ryb]             ; still describe the glass
    pop bx
    add bp, [wd_rowhv]
    jmp short .yhave
.ydeep:
    mov bp, [wd_bot]                ; a resume below the view (the chunked
    inc bp                          ; count): drawn by nobody, no query armed
    jmp short .yhave
.yzero:
    mov bp, [wd_ty]                 ; row 0's band top is wd_ty exactly
    add bp, [wd_rowhv]
    sub bp, 8
    jmp short .yhave
.ynegs:
    mov bp, [wd_ty]
    sub bp, 8
.yhave:
    mov ax, bp
    sub ax, [wd_rowhv]
    add ax, 8
    mov [wd_rbandt], ax
    call wd_rstart                  ; BP is this row's y; the buffer starts blank
    call wd_rowsetup                ; DI = the pen: indents + alignment offset

.loop:
    ; --- the wrap rule, applied to the cell this index will occupy ---------
    mov cx, di
    add cx, 7
    cmp cx, [wd_wrgt]               ; the paragraph's own right edge (its
    ja .over                        ; right indent; [wd_rgt] when Normal)
    call wd_wordfit                 ; ...or the WORD that begins here would run
    jnc .fits                       ; off the row (SPEC.md 27.11)
    jmp short .wrap
.over:
    call wd_hangsp                  ; a trailing SPACE hangs past the margin
    jc .fits                        ; rather than indent the row below it
.wrap:
    call wd_rflush                  ; the row that is ENDING, before wd_nextrow
    call wd_advwrap                 ; same paragraph: h = its line spacing,
                                    ; BP moves by the ENTERED row's height
    call wd_nextrow                 ; the pen changed rows, so the signature
    call wd_bpush                   ; being accumulated belongs to the old one
    call wd_rstart                  ; ...and in break mode the rows below have
    call wd_rowsetup                ; to be pushed down before it is drawn
    mov ax, [wd_row]
    cmp ax, [wd_lastrow]            ; SIGNED (SPEC.md 27.7): wd_row is a
    jle .fits                       ; VISIBLE row and is negative above the
    jmp .stop                       ; view, which unsigned reads as past every
                                    ; limit - so the walk would stop before it
                                    ; had drawn anything at all
.fits:
    call wd_ask                     ; the queries, at the settled pen
    cmp byte [wd_draw], 0
    je .body
    call wd_carets                  ; ...and the caret, if this is its index
.body:
    cmp byte [wd_bstop], 0          ; the visual break (SPEC.md 27.3): the
    je .nostop                      ; walk ENDS at the caret, because the
    mov ax, [wd_i]                  ; whole point is that the note below it
    cmp ax, [wd_cur]                ; is not being laid out at all
    jne .nostop
    mov byte [wd_rowsok], 0         ; ...which leaves wd_rows stale below the
    jmp .donebrk                    ; caret: the note MOVED and this walk did
.nostop:                            ; not rewrite where. Near: .done is past a
                                    ; short jump's reach
    test bx, bx
    jz .done                        ; the index past the last character: the
                                    ; queries have seen it, and there is no
                                    ; character to draw
    es lodsb                        ; DF=0 per SPEC.md 1; the override is what
    dec bx                          ; makes the note a heap claim and not bss
    inc word [wd_i]
    cmp al, 13
    je .nl13
    cmp al, 9                       ; a tab (SPEC.md 65.3): one selectable
    jne .glyph                      ; character occupying the cells to the
    jmp .tab                        ; next default stop
.nl13:
    mov byte [wd_wstart], 1         ; a line break is a break opportunity like
                                    ; a space (SPEC.md 27.11)
    cmp byte [wd_showall], 0        ; Show-all (SPEC.md 65.1): the mark takes a
    je .nlplain                     ; cell of its own, stamped as a pilcrow by
    push ax                         ; wd_rflush. Folded - character and marker
    mov ax, 13                      ; attribute both - so toggling Show-all
    call wd_fold                    ; dirties exactly the rows that carry one
    mov ax, WDAT_PIL
    call wd_fold
    pop ax
    cmp byte [wd_draw], 0
    je .nlplain
    push ax
    push bx
    push cx
    mov cx, bp                      ; the same three gates the glyph store has:
    add cx, [wd_gh1]                       ; the row fits, this pass redraws it, and
    cmp cx, [wd_bot]                ; the cell is inside the band
    ja .nlpop
    call wd_rowdirty
    jc .nlpop
    mov bx, di
    sub bx, [wd_tx]
    mov cl, 3
    shr bx, cl
    cmp bx, [wd_rcols]
    jae .nlpop
    mov byte [wd_rbuf+bx], ' '
    mov byte [wd_abuf+bx], WDAT_PIL
.nlpop:
    pop cx
    pop bx
    pop ax
.nlplain:
    call wd_rflush                  ; same as the wrap above: flush before
    call wd_advnl                   ; a NEW paragraph begins at [wd_i]: its
                                    ; format is scanned once and its first
                                    ; row's height includes the open space
    call wd_nextrow                 ; the mark occupies no cell - so it is not
    call wd_rstart                  ; folded into either row's signature, and
    call wd_rowsetup                ; the pixels of the row it ends are the
    mov ax, [wd_row]                ; same with it and without it. Signed, for
    cmp ax, [wd_lastrow]            ; the reason at the wrap above (near jumps:
    jg .nlstop                      ; the Show-all block above pushed .loop
    jmp .loop                       ; past a short jump's reach)
.nlstop:
    jmp .stop
.glyph:
    mov byte [wd_wstart], 0         ; the next index is mid-word...
    cmp al, ' '
    jne .wsdone
    mov byte [wd_wstart], 1         ; ...unless this is the space that ended
.wsdone:                            ; one (SPEC.md 27.11)
    push ax                         ; the character's CHP byte (SPEC.md 65.3):
    push bx                         ; the same index in the CHP claim. Fetched
    mov bx, si                      ; through ES with the document segment put
    dec bx                          ; back after - ES belongs to the text for
    mov es, [wd_cseg]               ; the rest of the walk
    mov al, [es:bx]
    mov es, [wd_dseg]
    mov [wd_cattr], al
    pop bx
    pop ax
    push ax                         ; fold it in whatever this pass is for:
    xor ah, ah                      ; the pass that COMPUTES the signatures is
    push ax                         ; a measure pass, so this cannot hang off
    mov ax, [wd_i]                  ; wd_draw.
    dec ax                          ; ...and a SELECTED cell folds differently
    call wd_selq                    ; (SPEC.md 27.8), because the inversion is
    pop ax                          ; pixels like the glyph is and moving the
    jnc .nfsel                      ; selection has to dirty the row it left
    mov ah, 0x80                    ; as well as the one it arrived at. POP
.nfsel:                             ; touches no flags, so the answer survives
    call wd_fold
    push ax                         ; ...and the CHP byte folds BESIDE the
    mov al, [wd_cattr]              ; character (SPEC.md 65.1), so a formatting
    xor ah, ah                      ; change dirties exactly the rows it
    call wd_fold                    ; touched and nothing else
    pop ax
    pop ax
    test byte [wd_cattr], WDAT_HID  ; hidden: dropped at accumulate time - no
    jz .visible                     ; cell, no pen advance (SPEC.md 65.1). The
    jmp .loop                       ; fold above still saw it, so toggling
                                    ; hidden dirties the row
.visible:
    cmp byte [wd_draw], 0
    je .advance
    mov cx, bp                      ; vertical clip: drop rows that overflow,
    add cx, [wd_gh1]                       ; but keep advancing the pen so every
    cmp cx, [wd_bot]                ; position below stays true
    ja .advance
    call wd_rowdirty                ; ...and drop the rows whose pixels this
    jc .advance                     ; redraw already knows are right
    push bx                         ; into the row buffer at this pen's CELL -
    mov bx, di                      ; wd_rflush draws the whole row at once
    sub bx, [wd_tx]
    push cx
    mov cl, 3
    shr bx, cl
    pop cx
    cmp bx, [wd_rcols]
    jae .nocell                     ; past the band: the wrap rule above means
    push ax                         ; this cannot normally happen, and a
                                    ; clamped wd_rcols is the case where it can
    mov ah, [wd_cattr]              ; small caps case-map at accumulate time
    test ah, WDAT_SCAP              ; (SPEC.md 65.1): the row buffer holds the
    jz .nomap                       ; capital, so the delta diff and the run
    cmp al, 'a'                     ; draw both see what the screen shows
    jb .nomap
    cmp al, 'z'
    ja .nomap
    sub al, 32
.nomap:
    mov [wd_rbuf+bx], al
    mov al, ah                      ; ...and the cell's attribute beside it,
    mov [wd_abuf+bx], al            ; which is what wd_rflush splits into runs
    pop ax
    push ax
    push dx
    mov ax, [wd_i]
    dec ax
    call wd_selq                    ; selected NOW...
    mov dl, 0
    jnc .n1
    mov dl, 1
.n1:
    call wd_selqo                   ; ...and selected ON SCREEN (SPEC.md 27.8.2)
    mov dh, 0
    jnc .n2
    mov dh, 1
.n2:
    or dl, dl
    jz .nosel
    mov ax, bx                      ; inside the selection: widen the span
    call wd_selfold                 ; wd_rflush inverts (SPEC.md 27.8)
.nosel:
    cmp dl, dh
    je .noxf
    mov ax, bx                      ; ...and its inversion has to CHANGE, which
    call wd_xfold                   ; is the only thing a drag actually owes
.noxf:
    pop dx
    pop ax
.nocell:
    pop bx
.advance:
    add di, 8
    jmp .loop                       ; near: the cell-buffer store above pushed
                                    ; the loop body past a short jump's reach

.tab:
    ; A TAB (SPEC.md 65.3): it advances the pen to the next default stop -
    ; every WD_TABSTOP cells from the row's start pen - and is ONE selectable
    ; character. Its cells go into the row buffer as spaces wearing the tab's
    ; own dress, so an underlined tab draws its rule and a selected one
    ; inverts whole; the fold sees the 9 and its CHP, and the expansion is a
    ; function of the folded content before it, so the signature stays honest.
    mov byte [wd_wstart], 1         ; a tab ends a word, like a space
    push ax
    push bx
    mov bx, si
    dec bx
    mov es, [wd_cseg]               ; its CHP byte, the glyph path's fetch
    mov al, [es:bx]
    mov es, [wd_dseg]
    mov [wd_cattr], al
    pop bx
    pop ax
    push ax
    xor ah, ah
    push ax
    mov ax, [wd_i]
    dec ax
    call wd_selq
    pop ax
    jnc .tnfs
    mov ah, 0x80
.tnfs:
    call wd_fold
    push ax
    mov al, [wd_cattr]
    xor ah, ah
    call wd_fold
    pop ax
    pop ax
    test byte [wd_cattr], WDAT_HID  ; a hidden tab occupies nothing
    jz .tvis
    jmp .loop
.tvis:
    call wd_tabw                    ; AX = the gap in px...
    mov cx, [wd_wrgt]               ; ...clamped to the row's edge, whole
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
    cmp byte [wd_draw], 0
    je .tadv
    mov ax, bp                      ; the glyph-store gates, cell test aside
    add ax, [wd_gh1]
    cmp ax, [wd_bot]
    ja .tadv
    call wd_rowdirty
    jc .tadv
    push ax
    push bx
    push dx
    mov dx, cx                      ; DX = px still to write
    mov bx, di
    sub bx, [wd_tx]
    push cx
    mov cl, 3
    shr bx, cl
    pop cx                          ; BX = the first cell
.tcell:
    cmp bx, [wd_rcols]
    jae .tcdone
    mov byte [wd_rbuf+bx], ' '
    mov al, [wd_cattr]
    mov [wd_abuf+bx], al
    push dx
    mov ax, [wd_i]
    dec ax
    call wd_selq
    mov dl, 0
    jnc .ts1
    mov dl, 1
.ts1:
    call wd_selqo
    mov dh, 0
    jnc .ts2
    mov dh, 1
.ts2:
    or dl, dl
    jz .tnosel
    mov ax, bx
    call wd_selfold
.tnosel:
    cmp dl, dh
    je .tnoxf
    mov ax, bx
    call wd_xfold
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
    mov ax, [wd_row]                ; how tall the NOTE is, which is what the
    add ax, [wd_top]                ; scroll bar's thumb is a fraction of
    inc ax                          ; (SPEC.md 27.7). HERE, not below: .blank
    mov [wd_drows], ax              ; walks wd_row on past the last row the note
    mov byte [wd_hdirty], 0         ; actually has, and .donebrk is the visual
                                    ; break's end - a walk that stopped at the
                                    ; caret has not seen the note's height
.donebrk:
    call wd_rflush                  ; the last row the walk was accumulating

    ; ...and then every row BELOW it that this redraw still owns. A note that
    ; shrank - a backspace that pulled a wrapped line back up, a deleted
    ; newline - leaves rows the walk no longer reaches, and their old pixels
    ; are still on screen. The band fill used to erase them for free, because
    ; it covered dr0..dr1 whether or not the walk got there; drawing row by row
    ; does not, so they are blanked explicitly. Without this a deletion left
    ; the row's last state behind, caret included, which is exactly what the
    ; first test of this rewrite showed.
    cmp byte [wd_draw], 0
    je .sigpad
.blank:
    mov ax, [wd_row]
    cmp ax, [wd_dr1]
    jae .sigpad                     ; past what this redraw was asked for
    cmp ax, [wd_vrows]
    jae .sigpad                     ; ...or past the content
    call wd_advblank                ; blank rows step 8px, contiguously from
    call wd_nextrow                 ; the last real row's band
    call wd_rstart                  ; an empty row at this y: wd_rflush's own
    call wd_rflush                  ; dirty and fits tests still gate it
    jmp short .blank

.sigpad:
    mov ax, [wd_row]                ; a walk that ran to its natural end is
    inc ax                          ; what wd_rows describes, rows 0..wd_row
    js .norows2                     ; (SPEC.md 27.5) - a STOPPED one leaves
    mov [wd_rowsn], ax              ; the tail of the table stale, and one that
    mov byte [wd_rowsok], 1         ; ended ABOVE the view described none of it
    jmp short .padchk
.norows2:
    mov word [wd_rowsn], 0
    mov byte [wd_rowsok], 0
.padchk:
    cmp byte [wd_sigup], 0
    je .fin
.pad:
    call wd_nextrow                 ; flush the row the walk ended on, and then
    mov ax, [wd_row]                ; every visible row after it: a note that
    cmp ax, [wd_vrows]              ; SHRANK leaves rows behind that are no
    jb .pad                         ; longer reached, and their old signature
                                    ; is exactly what says they must be erased
    jmp short .fin                  ; ...and NOT into .stop: that path pads
                                    ; wd_row past the note's last row without
                                    ; wd_rstart, so the entries it would claim
                                    ; below were never written
.stop:                              ; the wd_lastrow stop leaves wd_rows ALONE:
                                    ; a walk that ends early is one whose
                                    ; caller knows nothing below it moved, so
                                    ; the entries past it are still what the
                                    ; last full pass wrote
    ; It cannot know the note's HEIGHT either - but it does know a LOWER BOUND,
    ; and raising [wd_drows] to it is what keeps wd_scrollmax from clamping the
    ; view short of a caret that has just moved past the old bottom. Never
    ; LOWERED here: a note that shrank keeps a slightly generous scroll range
    ; until something walks it whole, and the cost of that is one blank row at
    ; the end rather than a caret nobody can see (SPEC.md 27.7)
    push ax
    mov ax, [wd_row]                ; the ABSOLUTE row this walk stopped on,
    add ax, [wd_top]                ; and the index that row begins at: the
    mov [wd_stoprow], ax            ; pair a resumable walk picks up from
    push ax                         ; (SPEC.md 27.7.3). wd_rstart has already
    mov ax, [wd_i]                  ; run for this row, so [wd_i] is its FIRST
    mov [wd_stopi], ax              ; character and not the last of the row
    pop ax                          ; above. Published on EVERY bounded stop
                                    ; and kept only by wd_height, on the walk
                                    ; it issued itself - which is safe because
                                    ; it reads them under the lock with
                                    ; nothing between the call and the read
    inc ax
    cmp ax, [wd_drows]
    jbe .nolb
    mov [wd_drows], ax
.nolb:
    ; ...and wd_rows DOES describe what it passed, as long as this walk started
    ; at the top of the view rather than at a seed part-way down. Every row it
    ; stood on went through wd_rstart, so the table is good up to wd_row - and
    ; without saying so, a bounded wd_paint left [wd_rowsok] clear and every
    ; caret key after it fell back to walking from index 0 (SPEC.md 27.5).
    cmp byte [wd_resume], 0
    jne .norn                       ; a RESUMED walk skipped the rows above its
    mov ax, [wd_row]                ; seed, and theirs are the last full pass's
    cmp ax, [wd_vrows]
    jbe .rncap
    mov ax, [wd_vrows]
.rncap:
    cmp ax, WD_MAXROWS
    jbe .rnok
    mov ax, WD_MAXROWS
.rnok:
    or ax, ax
    jle .norn                       ; it stopped above the view: it described
    mov [wd_rowsn], ax              ; none of the table
    mov byte [wd_rowsok], 1
.norn:
    pop ax
.fin:
    mov word [wd_lastrow], 0x7FFF   ; ONE-SHOT: a caller that forgets to set it
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
; wd_hangsp - may the cell at this pen hang past the right edge?
; in:  ES:SI = the document at [wd_i], BX = characters left, DI = pen x
; out: CF = 1 = do not wrap, let it hang; preserves every register
;
; Only a SPACE, and only one cell's worth. Word wrap ends a row after the last
; word that fits, and the space that follows that word then has nowhere to go:
; the cell rule sends it to the next row, where it is an indent nobody typed -
; one row in [wd_rcols], often enough to look like a mistake in a narrow
; window. Every text editor hangs it past the margin instead, and here that
; costs nothing, because the cell is beyond [wd_rcols] and so is dropped by
; the row buffer's own bound rather than drawn: a space paints nothing anyway.
;
; The overshoot is capped at that one cell, which is what keeps a run of
; spaces from walking the pen out of the window - and out of a 16-bit DI.
; -----------------------------------------------------------------------------
wd_hangsp:
    or bx, bx
    jz .no                          ; the end of the note: nothing to hang
    push ax
    mov ax, [wd_wrgt]
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
; wd_wordfit - would the word beginning at this index run off the row?
; in:  ES:SI = the document at [wd_i], BX = characters left, DI = pen x,
;      [wd_wstart] = 1 if this index begins a word
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
; [wd_rcols] is a whole row, and a word longer than THAT can never be helped
; by breaking - it has to be split by the cell rule wherever it stands, and
; forcing a wrap for it would put the pen at the left margin and ask the same
; question again, forever. Between the two thresholds is the only case there
; is.
;
; A word already AT the left margin is the same guard doing second duty and
; needs no test of its own: there R equals [wd_rcols], so "longer than R" and
; "longer than a row" are one question and the answer is always "do not
; break".
; -----------------------------------------------------------------------------
wd_wordfit:
    cmp byte [wd_wstart], 0
    je .no                          ; mid-word: asked and answered at its first
    or bx, bx                       ; character
    jz .no                          ; nothing left to measure
    push ax
    push bx
    push cx
    push dx
    push si

    mov ax, [wd_wrgt]
    inc ax
    sub ax, di                      ; pixels left on this row, this cell first
    jle .pop_no                     ; the caller has already tested the cell,
    mov cl, 3                       ; so this cannot fire - but a shift of a
    shr ax, cl                      ; negative width would answer nonsense
    mov dx, ax                      ; DX = R, whole cells left on this row

    mov cx, dx                      ; --- does the word END inside them? -----
.p1:                                ; THE BREAK TEST COMES FIRST, and the cell
    or bx, bx                       ; count second: a word that exactly fills
    jz .pop_no                      ; the space left ends on the cell after the
    mov al, [es:si]                 ; last one it occupies, and testing the
    cmp al, ' '                     ; count first calls that an overflow. It
    je .pop_no                      ; is invisible at 29 columns and constant
    cmp al, 13                      ; at 9, which is what a narrow window
    je .pop_no                      ; showed
    cmp al, 9                       ; a tab ends a word too (SPEC.md 65.3)
    je .pop_no
    jcxz .p2
    inc si
    dec bx
    dec cx
    jmp short .p1

.p2:                                ; --- no. Would a whole row hold it? -----
    mov cx, [wd_wcols]              ; a fresh row of THIS paragraph
    sub cx, dx                      ; the cells a FRESH row would add
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
    jcxz .pop_no                    ; longer than a row: the cell rule owns it
    inc si
    dec bx
    dec cx
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
; wd_ask - answer the walk's queries at the settled pen (module-internal)
; in:  DI/BP = the pen, [wd_i] = this index, SI -> its character, BX = the
;      characters left (0 = we are past the end)
; out: the wd_curx/wd_cury/wd_hiti/wd_wanti fields updated; preserves all
;
; The "+4" is the half-cell rule every text editor uses: a click in the left
; half of a character puts the caret before it, one in the right half after.
; A NEWLINE is excluded from the "after" half, and that is what makes End
; land before the line break instead of at the start of the next line - the
; character occupies no cell, so there is no right half of it to click in.
; -----------------------------------------------------------------------------
wd_ask:
    push ax
    push cx
    mov ax, [wd_i]
    cmp ax, [wd_cur]
    jne .hit
    mov [wd_curx], di
    mov [wd_cury], bp
    push ax
    mov ax, [wd_row]                ; ...and its VISIBLE row, signed - what
    mov [wd_currow], ax             ; wd_seecaret scrolls by now that a row's
    pop ax                          ; y is no longer 8*row (SPEC.md 65.6)
    mov byte [wd_curseen], 1
    push ax                         ; the row the caret is on starts HERE, and
    mov ax, [wd_ckpc]               ; that is the only state the next keystroke
    mov [wd_ckpi], ax               ; needs to skip everything above it
    mov ax, [wd_ckpcr]              ; (SPEC.md 27.4)
    mov [wd_ckpr], ax
    mov byte [wd_ckok], 1
    pop ax
    push ax                         ; AX is [wd_i] and .hit below still wants
    mov ax, di                      ; it. The caret is pixels on this row too,
    xor ax, 0x5A5A                  ; and folding it in HERE - between the
    call wd_fold                    ; glyph before it and the one after - is
    pop ax                          ; what makes moving it dirty both rows.
                                    ; The xor keeps a column from folding the
                                    ; way a character code would
.hit:
    mov cx, [wd_hity]
    cmp cx, 0xFFFF
    je .want
    cmp cx, [wd_rbandt]             ; the click row is this pen row? The BAND
    jb .want                        ; reaches from the row's own top - a click
    mov cx, bp                      ; in a spaced row's leading gap belongs to
    add cx, [wd_gh1]                       ; the row, not to nothing (SPEC.md 65.6)
    cmp cx, [wd_hity]
    jb .want
    cmp byte [wd_hitset], 0
    jne .hit2
    mov [wd_hiti], ax               ; the first index on the row, until a
    mov byte [wd_hitset], 1         ; later cell claims it
.hit2:
    mov cx, di
    add cx, 4
    cmp [wd_hitx], cx
    jb .want                        ; the left half: the caret goes before it
    call wd_isnl
    jc .want                        ; a newline has no right half
    inc ax
    mov [wd_hiti], ax
    dec ax
.want:
    cmp word [wd_wanty], 0x7FFF     ; [wd_wanty] is a VISIBLE ROW now, not a
    je .out                         ; pixel (SPEC.md 65.6): a row's y is no
    mov cx, [wd_wanty]              ; longer derivable outside the walk, but
    cmp cx, [wd_row]                ; its NUMBER is what the callers had
    jne .out                        ; anyway. 0x7FFF disables (0xFFFF is a
                                    ; real row: one above the view)
    cmp byte [wd_wantset], 0
    jne .want2
    mov [wd_wanti], ax
    mov byte [wd_wantset], 1
.want2:
    mov cx, di
    add cx, 4
    cmp [wd_wantx], cx
    jb .out
    call wd_isnl
    jc .out
    inc ax
    mov [wd_wanti], ax
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_isnl - is the character at this index a newline (or past the end)?
; in:  BX = characters remaining, SI -> the character
; out: CF=1 = yes, or there is no character here; preserves all registers
; -----------------------------------------------------------------------------
wd_isnl:
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
; wd_carets - draw the caret when the walk is standing on its index
; in:  DI/BP = the pen, [wd_i], [wd_cur]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_carets:
    push ax
    push bx
    push cx
    push dx
    cmp byte [wd_selon], 0
    jne .out                        ; a selection REPLACES the caret, which is
                                    ; the Macintosh rule and is also the only
                                    ; honest one here: a 1px black bar inside
                                    ; an inverted band is invisible, so drawing
                                    ; it would cost a line and show nothing
    mov ax, [wd_i]
    cmp ax, [wd_cur]
    jne .out
    mov cx, bp
    add cx, [wd_gh1]
    cmp cx, [wd_bot]
    ja .out                         ; its row does not fit: no caret
    call wd_rowdirty                ; ...nor does a row this pass is not
    jc .out                         ; redrawing (SPEC.md 27.2)
    mov [wd_rcx], di                ; BANKED, not drawn: the row's font_run has
                                    ; not happened yet and would paint over it,
                                    ; so wd_rflush puts it back afterwards
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
; pixels, because on any row the k-th glyph is always at [wd_tx] + 8k. It is a
; hash and not a proof, the same trade the Task Manager's rows make (SPEC.md
; 28): a collision leaves one row stale until its content moves again.
;
; The caret is part of the signature and has to be. Moving it off a row has to
; dirty that row, or it stays drawn there.
;
; A redraw is then two walks. The first measures, folds, compares against the
; stored signatures and widens [wd_dr0]..[wd_dr1] - a RANGE, not a bitmap,
; because the interesting cases are all contiguous and a range needs no
; indexing and turns the erase into ONE fill. The second draws, and skips
; every row outside it. If the range comes back empty nothing is drawn at all.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_fold - fold AX into the row being accumulated
; in:  AX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_fold:
    push bx
    mov bx, [wd_rowh]
    rol bx, 1                       ; rotate then add, so a transposition is
    add bx, ax                      ; not invisible
    mov [wd_rowh], bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_rstart - begin accumulating a row: BP is its y, the buffer goes to spaces
; preserves all registers
;
; SPACES and not zeros. font_run paints a space as background on its fast path
; - the glyph's rows are all clear, so the mask leaves the background byte -
; which is what makes one run erase the whole band as well as letter it. That
; is the entire reason this rewrite needs no fill: the padding IS the erase.
; -----------------------------------------------------------------------------
wd_rstart:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov [wd_rby], bp
    mov word [wd_rcx], 0xFFFF
    mov word [wd_rs0], 0xFFFF       ; ...and no inverted cells yet either
    mov word [wd_rs1], 0xFFFF
    mov word [wd_xs0], 0xFFFF       ; ...nor any whose inversion must change
    mov word [wd_xs1], 0xFFFF
    mov ax, [wd_i]                  ; where this row STARTS, banked as the
    mov [wd_ckpc], ax               ; checkpoint candidate: wd_ask promotes it
    mov ax, [wd_row]                ; the moment the walk stands on the caret
    mov [wd_ckpcr], ax              ; (SPEC.md 27.4)
    call wd_xnote                   ; ...and offered to the row index, which is
                                    ; the same fact again for a row OUTSIDE the
                                    ; view - one compare unless it is wanted
                                    ; (SPEC.md 27.13)
    cmp ax, WD_MAXROWS              ; ...and into wd_rows, which is the same
    jae .norow                      ; fact for every row rather than for the
    shl ax, 1                       ; caret's (SPEC.md 27.5)
    mov di, ax
    mov ax, [wd_i]
    mov [di+wd_rows], ax
.norow:
    ; --- the row's GLYPH y, banked and compared (SPEC.md 65.6) --------------
    ; wd_ryb is the heights array in prefix form: what a seeded walk resumes
    ; its y from, and what pass 1 compares so a row whose pixels MOVED is
    ; dirty even when its text did not. A pure measure walk must not touch
    ; it - it describes the GLASS.
    cmp byte [wd_draw], 0
    jne .ry
    cmp byte [wd_sigup], 0
    je .rydone
.ry:
    mov ax, [wd_row]
    cmp ax, WD_MAXROWS
    jae .rydone                     ; unsigned: rows above the view fail too
    cmp ax, [wd_vrows]
    jae .rydone
    shl ax, 1
    mov di, ax
    mov ax, bp
    cmp byte [wd_hasfmt], 0
    je .rystore                     ; uniform rows cannot move; the store
    cmp byte [wd_sigup], 0          ; keeps the bank warm across the 0->1
    je .rystore                     ; transition
    cmp ax, [di+wd_ryb]
    je .rystore
    push ax
    push bx
    mov bx, [di+wd_ryb]
    cmp bx, [wd_ty]                 ; a bank OUTSIDE the band never described
    jb .rynew                       ; a glass row: this row is ENTERING the
    cmp bx, [wd_bot]                ; view (the document grew, or the slot is
    ja .rynew                       ; virgin) - it is dirty, but it did not
                                    ; MOVE, and calling it moved dragged the
                                    ; erase up to rows nobody was redrawing
    mov byte [wd_ymoved], 1         ; THE ROW MOVED: remember the highest
    cmp bx, ax                      ; glyph pixel it owned or owns - the band
    jbe .rya                        ; repaint erases from there to the content
    mov bx, ax                      ; bottom and redraws every row below
.rya:                               ; (SPEC.md 65.6). NEVER higher: the first
    cmp bx, [wd_ymv0]               ; moved row's old and new glyphs both sit
    jae .ryd                        ; strictly below the unmoved row above it,
    mov [wd_ymv0], bx               ; so the erase cannot eat a row nobody
    jmp short .ryd                  ; redraws
.rynew:                             ; entering: an ordinary dirty row - its
                                    ; ground below the content is blank and
                                    ; its opaque run self-erases
.ryd:
    mov bx, [wd_row]
    cmp bx, [wd_dr0]
    jae .rye
    mov [wd_dr0], bx
.rye:
    cmp bx, [wd_dr1]
    jbe .ryf
    mov [wd_dr1], bx
.ryf:
    pop bx
    pop ax
.rystore:
    mov [di+wd_ryb], ax
.rydone:
    mov di, wd_rbuf
    mov cx, [wd_rcols]
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov di, wd_abuf                 ; ...and the attribute cells to PLAIN
    mov cx, [wd_rcols]              ; (SPEC.md 65.1): the padding's erase is a
    xor al, al                      ; plain run, and the run splitter reads
    rep stosb                       ; this beside every cell
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rflush - draw the accumulated row: ONE opaque font_run, then its caret
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
; would paint over it. wd_carets banks its x instead of drawing.
;
; Three things this must not draw: a row of a measure pass, a row this redraw
; already knows is right (wd_rowdirty, SPEC.md 27.2), and a row whose pixels
; fall past the content bottom - all three the same tests the per-character
; draw used to make, moved up to the row.
; -----------------------------------------------------------------------------
wd_rflush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [wd_draw], 0
    je .out
    call wd_rowdirty
    jc .out                         ; a row this redraw already knows is right
    mov ax, [wd_rby]
    cmp ax, [wd_ty]
    jb .out                         ; ABOVE the view: scrolled off the top
    add ax, [wd_gh1]                       ; (SPEC.md 27.7), and the unsigned tests
    cmp ax, [wd_bot]                ; elsewhere cannot see this one, because a
    ja .out                         ; row a little above wd_ty has an ordinary
                                    ; small y. Below: it does not fit, the pen
                                    ; still
                                    ; advanced, so every position below is true
    cmp word [wd_rcols], 0
    je .caret

    ; --- ONLY THE INVERSION MOVED (SPEC.md 27.8.2) -------------------------
    ; A drag changes no character anywhere. What it changes is which cells are
    ; inverted, and XOR is exactly the operation for that - so flip the cells
    ; whose selected-ness differs from the screen's and letter NOTHING. The
    ; row's glyphs are already correct and re-drawing them to invert them was
    ; costing a full row of font_run per dirty row per pass.
    ;
    ; Gated on a selection existing at BOTH ends, which is what guarantees no
    ; caret is drawn either before or after (wd_carets returns early while one
    ; is up): a caret bar sits on top of a glyph, so erasing one needs that
    ; cell lettered again and this path draws no cells.
    cmp byte [wd_selonly], 0
    je .normal
    cmp byte [wd_selon], 0
    je .normal
    cmp byte [wd_oselon], 0
    je .normal
    mov ax, [wd_xs0]
    cmp ax, 0xFFFF
    je .cache                       ; this row's inversion is already right
    mov cx, [wd_xs1]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]                 ; x1
    push ax
    mov ax, cx
    inc ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]
    dec ax
    mov cx, ax                      ; x2
    pop ax
    mov bx, [wd_rby]
    mov dx, bx
    add dx, [wd_gh1]
    call OSAPI_GFX_XOR_FILL
    jmp .cache                      ; wd_prow still describes the screen -
                                    ; not one character moved - but .cache
                                    ; owes wd_prs0/wd_prs1 the new span
.normal:

    mov word [wd_fcc], 0xFFFF       ; this row's caret column, if it has one
    mov ax, [wd_rcx]
    cmp ax, 0xFFFF
    je .span
    sub ax, [wd_tx]
    mov cl, 3
    shr ax, cl
    mov [wd_fcc], ax
.span:
    mov word [wd_flo], 0            ; the default span is the whole row
    mov ax, [wd_rcols]
    dec ax
    mov [wd_fhi], ax
    mov ax, [wd_row]
    cmp ax, [wd_prowi]
    je .delta                       ; the cached row diffs cell by cell
    ; a WHOLE-row redraw of a spaced row also erases its leading GAP - the
    ; band between the row's top and its glyphs (SPEC.md 65.6). Glyph runs
    ; never touch it, so ink parked there by an older layout (a caret bar, a
    ; taller row's letters) would survive every redraw without this. One fill,
    ; only for a row taller than its glyphs, only when the band was not
    ; already filled.
    cmp byte [wd_hasfmt], 0
    je .draw
    cmp byte [wd_clean], 0
    jne .draw
    mov ax, [wd_rbandt]
    cmp ax, [wd_ty]
    jae .gt
    mov ax, [wd_ty]
.gt:
    cmp ax, [wd_rby]
    jae .draw                       ; an 8px row: no gap
    push bx
    push cx
    push dx
    mov bx, ax                      ; y1 = the band top
    mov dx, [wd_rby]
    dec dx                          ; y2 = just above the glyphs
    mov ax, [wd_tx]
    sub ax, WD_MARGIN
    mov cx, [wd_rgt]
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

    mov word [wd_flo], 0xFFFF       ; --- the delta, cell by cell -----------
    mov word [wd_fhi], 0xFFFF
    xor bx, bx
.dl:
    cmp bx, [wd_rcols]
    jae .dfold
    mov al, [wd_rbuf+bx]
    cmp al, [wd_prow+bx]
    jne .dmk
    mov al, [wd_abuf+bx]            ; same character, different dress: a
    cmp al, [wd_pattr+bx]           ; formatting change moves pixels too
    je .dn                          ; (SPEC.md 65.1)
.dmk:
    cmp word [wd_flo], 0xFFFF
    jne .dhi
    mov [wd_flo], bx
.dhi:
    mov [wd_fhi], bx
.dn:
    inc bx
    jmp short .dl
.dfold:
    mov ax, [wd_prcc]               ; the caret's cells count as changed at
    call wd_fold1                   ; both ends: the one it left has to lose
    mov ax, [wd_fcc]                ; its bar, and the one it arrived at has
    call wd_fold1                   ; to get one
    mov ax, [wd_rs0]                ; ...and so does the SELECTION, for exactly
    cmp ax, [wd_prs0]               ; the same reason and only when it MOVED:
    jne .selchg                     ; a row whose inverted span is unchanged
    mov ax, [wd_rs1]                ; has the right pixels already, and folding
    cmp ax, [wd_prs1]               ; it in unconditionally would redraw every
    je .seldone                     ; selected row on every pass
.selchg:
    mov ax, [wd_prs0]               ; the union of the two spans, which
    call wd_fold1                   ; contains every cell whose inverted-ness
    mov ax, [wd_prs1]               ; changed. Wider than the difference and
    call wd_fold1                   ; very much simpler; a selection is a
    mov ax, [wd_rs0]                ; static thing, so this runs once as it
    call wd_fold1                   ; arrives and once as it leaves
    mov ax, [wd_rs1]
    call wd_fold1
.seldone:
    cmp word [wd_flo], 0xFFFF
    je .cache                       ; nothing moved: draw NOTHING
    push bx                         ; a bold/italic strike bleeds one pixel
    mov bx, [wd_fhi]                ; into the cell to its RIGHT (SPEC.md
    test byte [wd_pattr+bx], WDAT_BOLD | WDAT_ITAL
    jz .nobled                      ; 65.1): when the span's last cell WAS
    inc bx                          ; drawn with one of those, the neighbour
    cmp bx, [wd_rcols]              ; may be carrying its bleed, so it joins
    jae .nobled                     ; the span and is re-lettered opaque
    mov ax, bx
    call wd_fold1
.nobled:
    pop bx

.draw:
    cmp byte [wd_clean], 0          ; the band is known blank (a full repaint
    je .draw2                       ; white-filled it, SPEC.md 27.2), so the
    mov cx, [wd_flo]                ; padding has nothing to erase and the run
    mov ax, [wd_rs1]                ; stops at the last real character - but
    cmp ax, 0xFFFF                  ; never short of the last SELECTED one: a
    je .tf                          ; selected trailing space is drawn to be
    cmp ax, cx                      ; inverted, and trimming it away would
    jbe .tf                         ; leave a gap in the highlight
    mov cx, ax
.tf:
    mov bx, [wd_fhi]                ; A fullscreen window is 90 cells wide and
.tl:                                ; a note is rarely that long: without this
    cmp byte [wd_rbuf+bx], ' '      ; a repaint costs rows x width instead of
    jne .tdone                      ; characters, and on a 4.77MHz 8088 that
    cmp byte [wd_abuf+bx], 0        ; is the difference between half a second
    jne .tdone                      ; and five. A DRESSED space is not blank -
    cmp bx, cx                      ; an underlined space is a rule, a pilcrow
    jbe .tstop                      ; cell is a stamp - so the trim stops at
    dec bx                          ; any styled cell (SPEC.md 65.1)
    jmp short .tl
.tstop:
    cmp word [wd_rs1], 0xFFFF       ; all blank from the floor up: nothing to
    je .cache                       ; do, unless a selection reaches here
.tdone:
    mov [wd_fhi], bx
.draw2:
    ; --- the span, split into runs of equal CHP (SPEC.md 65.1) --------------
    ; Adjacent plain cells are one run by construction, so unstyled text is
    ; exactly the one opaque font_run it always was; each styled run draws by
    ; its dress in wd_drawrun. Left to right, so a bold strike's 1px bleed
    ; into the next run is repainted by that run's own opaque pass.
    mov bx, [wd_flo]
.runl:
    cmp bx, [wd_fhi]
    ja .rundone
    mov al, [wd_abuf+bx]
    mov [wd_runa], al
    mov si, bx
.runx:
    inc si
    cmp si, [wd_fhi]
    ja .runend
    cmp al, [wd_abuf+si]
    je .runx
.runend:
    mov [wd_r0], bx                 ; the run: cells [wd_r0]..[wd_r1]
    mov ax, si
    dec ax
    mov [wd_r1], ax
    call wd_drawrun
    mov bx, si
    jmp short .runl
.rundone:
    call wd_selxor                  ; the runs drew the cells upright; invert
                                    ; the selected ones they covered (SPEC.md
                                    ; 27.8). AFTER the runs, for the reason the
                                    ; caret is drawn after them: a run would
                                    ; paint over an inversion made first

.cache:
    push es                         ; the span was drawn, so the screen now
    push ds                         ; shows wd_rbuf: remember it, and remember
    pop es                          ; which row and where its caret is
    cld
    mov si, wd_rbuf
    mov di, wd_prow
    mov cx, [wd_rcols]
    rep movsb
    mov si, wd_abuf                 ; ...and the attributes it was drawn with,
    mov di, wd_pattr                ; or the diff above could not see a
    mov cx, [wd_rcols]              ; formatting change on the cached row
    rep movsb
    pop es
    mov ax, [wd_row]
    mov [wd_prowi], ax
    mov ax, [wd_fcc]
    mov [wd_prcc], ax
    mov ax, [wd_rs0]                ; ...and which of its cells came out
    mov [wd_prs0], ax               ; inverted, or the next pass has no way to
    mov ax, [wd_rs1]                ; tell that the highlight moved off a row
    mov [wd_prs1], ax               ; whose characters did not

.caret:
    mov ax, [wd_rcx]
    cmp ax, 0xFFFF
    je .out
    mov bx, [wd_rby]                ; 1px black caret, 8 rows tall, on top of
    mov dx, bx                      ; the run that would otherwise have eaten it
    add dx, [wd_gh1]
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

; wd_fold1 - fold column AX into [wd_flo]..[wd_fhi]; 0xFFFF folds nothing.
; Preserves everything.
wd_fold1:
    cmp ax, 0xFFFF
    je .out
    cmp word [wd_flo], 0xFFFF
    jne .lo
    mov [wd_flo], ax
    mov [wd_fhi], ax
    ret
.lo:
    cmp ax, [wd_flo]
    jae .hi
    mov [wd_flo], ax
.hi:
    cmp ax, [wd_fhi]
    jbe .out
    mov [wd_fhi], ax
.out:
    ret

; =============================================================================
; The styled run drawers (SPEC.md 65.1)
;
; wd_rflush splits the dirty span into runs of equal CHP and hands each one
; here. A plain run is exactly the one opaque font_run the engine always drew;
; bold adds a transparent second strike one pixel right; an italic run is
; staged from the pre-sheared 4bpp glyph table and lands as ONE gfx_blit4;
; the underlines are rules drawn after the glyphs; a pilcrow cell (Show-all's
; paragraph mark) is stamped from an 8x8 image. Priced in calls: a plain row
; is one call as before, a styled run costs one or two more.
; =============================================================================

; the pilcrow, 8x8 at 4bpp, black (0) on white (0xF): bowl over two stems
wd_pimg:
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
wd_x4tab:
%assign WDX4N 0
%rep 16
  %assign WDX4B0 0
  %if (WDX4N & 8) == 0
    %assign WDX4B0 WDX4B0 | 0xF0
  %endif
  %if (WDX4N & 4) == 0
    %assign WDX4B0 WDX4B0 | 0x0F
  %endif
  %assign WDX4B1 0
  %if (WDX4N & 2) == 0
    %assign WDX4B1 WDX4B1 | 0xF0
  %endif
  %if (WDX4N & 1) == 0
    %assign WDX4B1 WDX4B1 | 0x0F
  %endif
    db WDX4B0, WDX4B1
  %assign WDX4N WDX4N + 1
%endrep

; -----------------------------------------------------------------------------
; wd_itinit - make sure the sheared italic glyph table exists (SPEC.md 65.1)
; out: CF=0 with [wd_iseg] the claim holding 95 glyphs x 8 rows x 4 bytes;
;      CF=1 the heap refused (italic degrades to the plain glyph).
;      Preserves all registers.
; Built ONCE, at the first italic run drawn: OSAPI_FONT_GLYPHS is read, rows
; 0..3 sheared one pixel right, and every row expanded 1bpp -> 4bpp
; black-on-white through wd_x4tab, so staging a run is a straight copy.
; -----------------------------------------------------------------------------
wd_itinit:
    cmp word [wd_iseg], 0
    jne .ok
    cmp byte [wd_inwk], 0           ; on the worker's draw pass: a worker may
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
    mov ax, WD_ITKB                 ; the table AND the staging area
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [wd_iseg], dx
    call OSAPI_FONT_GLYPHS          ; DX:SI = the kernel's table (NOT in
    mov [wd_itsg], dx               ; KERNEL_SEG - read through ES = DX)
    mov [wd_itso], si
    mov word [wd_itch], 0
.g:
    mov si, [wd_itch]               ; the glyph's 8 source bytes, banked so
    push cx                         ; the two segments never overlap in one
    mov cl, 3                       ; loop
    shl si, cl
    pop cx
    add si, [wd_itso]
    mov es, [wd_itsg]
    xor bx, bx
.f:
    mov al, [es:si+bx]
    mov [wd_itmp+bx], al
    inc bx
    cmp bx, 8
    jb .f
    mov di, [wd_itch]
    push cx
    mov cl, 5
    shl di, cl                      ; x32: the glyph's slot in the claim
    pop cx
    mov es, [wd_iseg]
    xor bx, bx                      ; BX = row
.r:
    mov al, [wd_itmp+bx]
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
    mov ax, [wd_x4tab+si]
    mov [es:di], ax
    pop ax
    and al, 0x0F                    ; low nibble -> the second two
    xor ah, ah
    shl ax, 1
    mov si, ax
    mov ax, [wd_x4tab+si]
    mov [es:di+2], ax
    add di, 4
    inc bx
    cmp bx, 8
    jb .r
    inc word [wd_itch]
    cmp word [wd_itch], 95
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
; wd_itrun - draw the run [wd_r0]..[wd_r1] in italics: stage, then ONE blit
; in:  [wd_r0]/[wd_r1]/[wd_runa]/[wd_rby], wd_rbuf holds the characters,
;      gfx lock held
; out: CF=0 drawn; CF=1 no table (the caller letters it plain instead)
;      Preserves all registers.
; Bold-italic ANDs the staged image with itself shifted one pixel right -
; ink is the 0 nibble, so AND of bytes IS the union of ink.
; -----------------------------------------------------------------------------
wd_itrun:
    call wd_itinit
    jc .no
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, [wd_r1]
    sub cx, [wd_r0]
    inc cx                          ; CX = cells in the run
    mov ax, cx
    shl ax, 1
    shl ax, 1
    mov [wd_itstr], ax              ; the staged image's stride in bytes
    xor dx, dx                      ; DX = the cell being staged
.cell:
    cmp dx, cx
    jae .staged
    mov bx, [wd_r0]
    add bx, dx
    mov al, [wd_rbuf+bx]            ; already case-mapped by the walk
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
    add di, WD_STG4                 ; DI = its column in the staging area
    push cx
    push dx
    push ds
    mov ds, [cs:wd_iseg]            ; DS:SI = the table and ES:DI = the
    push ds                         ; staging area - the SAME claim now, so
    pop es                          ; the copy never leaves it
    cld
    mov dx, 8
.row:
    movsw
    movsw
    add di, [cs:wd_itstr]           ; CS: - DS is the table's segment here
    sub di, 4
    dec dx
    jnz .row
    pop ds
    pop dx
    pop cx
    inc dx
    jmp short .cell
.staged:
    test byte [wd_runa], WDAT_BOLD
    jz .noem
    mov es, [wd_iseg]               ; the staged image lives in the italic
    mov si, WD_STG4                 ; claim (SPEC.md 65.1), so every touch of
    mov dx, 8                       ; it below carries an ES override
.em:                                ; bold-italic: AND in the 1px-right copy,
                                    ; row by pixel row, right to left so the
                                    ; source byte is still the original
    mov di, [wd_itstr]
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
    add si, [wd_itstr]
    dec dx
    jnz .em
.noem:
    push bp                         ; BP is the walk's pen y - the blit wants
    mov es, [wd_iseg]               ; the stride there (SPEC.md 5.7)
    mov si, WD_STG4
    mov bp, [wd_itstr]
    mov ax, [wd_r0]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]                 ; AX = the run's x
    mov bx, [wd_rby]                ; BX = its row's y
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
; wd_drawrun - draw one equal-CHP run of the row being flushed
; in:  [wd_r0]/[wd_r1] = the run's cells, [wd_runa] = their attribute,
;      [wd_rby] = the row's y, wd_rbuf accumulated; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wd_bandrun - one styled run, composed into the band and put down in ONE call
; in:  [wd_r0]/[wd_r1] = the run's cells, [wd_rby] = its glyph y, wd_rbuf holds
;      the characters; gfx lock held
; out: nothing; preserves all registers
;
; SPEC.md 6.3's method, and the whole of what a proportional face costs at draw
; time: ty_band, ty_putn, ty_flush. It replaces ONE OSAPI_FONT_RUN with one
; OSAPI_GFX_BLIT1, so the call count a row costs does not move - what moves is
; that the glyphs are the face's instead of the cell's.
;
; THE BACK-UP RULE (SPEC.md 5.4.2). The blit wants an x on a multiple of 8, and
; a run does not start on one - so the band starts at the byte column to its
; LEFT, the 0..7 remainder becomes the pen inside the band, and the width is
; rounded up to the next byte column. The band is paper where the run is not,
; so this erases exactly what FONT_RUN's background would have.
; -----------------------------------------------------------------------------
wd_bandrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov dx, [wd_gh]
    call ty_band                    ; paper, as tall as the face

    mov ax, [wd_r0]                 ; the run's first cell, in screen x
    call wd_cx
    mov bx, ax
    and ax, 0xFFF8                  ; ...backed up to its byte column
    mov [wd_bx0], ax
    sub bx, ax                      ; BX = the pen inside the band, 0..7

    push ds
    pop es                          ; ES:SI = the text, which is ours here
    mov si, [wd_r0]
    add si, wd_rbuf
    mov cx, [wd_r1]
    sub cx, [wd_r0]
    inc cx
    mov ax, bx
    call ty_putn                    ; AX = the pen after the run
    test byte [wd_runa], WDAT_BOLD
    jz .nbold
    push ax                         ; bold: the same glyphs again, one pixel
    mov ax, bx                      ; right (SPEC.md 61.5/65.1). In the BAND
    inc ax                          ; this is a second compose rather than a
    push si                         ; second drawing call - it costs no floor
    call ty_putn                    ; at all, where the cell arm pays a whole
    pop si                          ; OSAPI_FONT_STR for it
    pop ax
    inc ax                          ; ...and the run is one pixel wider
.nbold:

%if WD_BANDDBG
    ; WD_BANDDBG: every band goes down as a 50% dither instead of as text, so
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
    mov ax, [wd_bx0]
    mov bx, [wd_rby]
    mov dx, [wd_gh]
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

wd_drawrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [wd_runa], WDAT_PIL
    jz .nopil
    mov bx, [wd_r0]                 ; Show-all's paragraph mark: one 8x8 stamp
.pl:                                ; per cell (there is one cell per mark)
    cmp bx, [wd_r1]
    ja .done
    mov ax, bx
    mov cl, 3
    shl ax, cl
    add ax, [wd_tx]
    push bx
    push bp
    push es
    push ds
    pop es
    mov si, wd_pimg
    mov bp, 4
    mov bx, [wd_rby]
    mov cx, 8
    mov dx, 8
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    pop bx
    inc bx
    jmp short .pl
.nopil:
    test byte [wd_runa], WDAT_ITAL
    jz .plain
    cmp byte [wd_prop], 0           ; wd_itrun shears the KERNEL's glyphs
    jne .plain                      ; (SPEC.md 65.1), so in a chosen face it
                                    ; would letter one run in a DIFFERENT
                                    ; TYPEFACE from the words either side of
                                    ; it - which reads as a bug rather than as
                                    ; italic. Upright in the right face is the
                                    ; better wrong answer until a face carries
                                    ; a drawn italic (SPEC.md 6.4's style 2)
    call wd_itrun                   ; staged and blitted - or CF=1 with no
    jnc .rules                      ; table, and the plain letters below are
                                    ; the degrade (SPEC.md 65.1)
.plain:
    cmp byte [wd_prop], 0
    je .cell
    call wd_bandrun                 ; SPEC.md 6.3: composed once, put down once
    jmp .rules
.cell:
    mov bx, [wd_r1]                 ; NUL-cap the run inside the row string,
    inc bx                          ; letter it, put the byte back
    mov al, [wd_rbuf+bx]
    push ax
    mov byte [wd_rbuf+bx], 0
    push bx
    mov si, [wd_r0]
    mov ax, si
    mov cl, 3
    shl ax, cl
    add ax, [wd_tx]
    mov cx, ax                      ; CX = x of the run's first cell
    add si, wd_rbuf
    mov dx, [wd_rby]
    mov al, CBLACK                  ; ink and background in one call: the erase
    mov ah, CWHITE                  ; and the letters are one decision per cell
    call OSAPI_FONT_RUN
    test byte [wd_runa], WDAT_BOLD
    jz .nbold
    inc cx                          ; bold: the same glyphs again, transparent,
    push ax                         ; one pixel right (SPEC.md 61.5/65.1) -
    mov al, CBLACK                  ; FONT_STR ORs ink and erases nothing
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_FONT_STR
.nbold:
    pop bx
    pop ax
    mov [wd_rbuf+bx], al
.rules:
    mov al, [wd_runa]
    test al, WDAT_UL | WDAT_WUL | WDAT_DUL
    jz .done
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    mov ax, [wd_r0]
    mov cl, 3
    shl ax, cl
    add ax, [wd_tx]                 ; AX = x1
    mov bx, [wd_r1]
    inc bx
    shl bx, cl
    add bx, [wd_tx]
    dec bx                          ; BX = x2
    mov dx, [wd_rby]
    add dx, [wd_gh1]                       ; the cell's last row
    test byte [wd_runa], WDAT_UL
    jz .nul
    call OSAPI_GFX_HLINE
.nul:
    test byte [wd_runa], WDAT_DUL
    jz .ndul
    call OSAPI_GFX_HLINE            ; double underline: rows 7 and 6
    dec dx
    call OSAPI_GFX_HLINE
    inc dx
.ndul:
    test byte [wd_runa], WDAT_WUL
    jz .done
    mov si, [wd_r0]                 ; word underline: a rule under each
.ws:                                ; non-space span of the run
    cmp si, [wd_r1]
    ja .done
    cmp byte [wd_rbuf+si], ' '
    je .wnext
    mov di, si
.wx:
    inc si
    cmp si, [wd_r1]
    ja .wdraw
    cmp byte [wd_rbuf+si], ' '
    jne .wx
.wdraw:
    mov ax, di
    push cx
    mov cl, 3
    shl ax, cl
    add ax, [wd_tx]
    mov bx, si
    shl bx, cl
    add bx, [wd_tx]
    dec bx
    pop cx
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
;  4. It needs [wd_tx] on a multiple of 8, because OSAPI_GFX_SCROLL is
;     byte-column granular on every adapter. OSAPI_WM_SNAP guarantees that on
;     EVERY adapter now (SPEC.md 11.94 - it was mono-only, and VGA turned out
;     to gain more from alignment than mono does), so the coin flip this note
;     used to describe on VGA is gone and rule 2's CPU test is the only gate
;     left. On a window too wide to snap the alignment still fails, the break
;     does not engage and the reflow is what happens. That is a FACT the code
;     can test, not a guess (SPEC.md 47 rule 3).
; =============================================================================

; -----------------------------------------------------------------------------
; wd_scroll - move row AX and everything below it down one row
; in:  AX = the first row index to move, wd_bounds already run, gfx lock held
; out: CF = OSAPI_GFX_SCROLL's answer; preserves all registers
;
; The x span is the whole content width rounded IN to byte columns. [wd_tx] is
; a multiple of 8 (the caller checked) and the left margin is WD_MARGIN = 8,
; so x1 is the content's own left edge; x2+1 rounds the content's right edge
; down, which can only ever drop part of the <8px tail past the last cell -
; the band no glyph reaches.
; -----------------------------------------------------------------------------
wd_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]
    mov bx, ax                      ; BX = y1
    mov dx, [wd_bot]                ; DX = y2
    mov ax, [wd_tx]
    sub ax, WD_MARGIN               ; AX = x1
    mov cx, [wd_rgt]
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
; wd_bpush - wd_walk reached a new row while the break is up: push the note
;            below it down to make room
; in:  [wd_row] = the row the pen just moved ONTO; gfx lock held
; out: nothing; preserves all registers
;
; Called from wd_walk's wrap site, AFTER the row that was ending has been
; flushed and BEFORE the new one starts - the only moment at which the row
; above is finished and the row below has not been touched.
;
; A refusal is the band below the caret being shorter than the row we are
; asking to insert, which is what happens when the caret reaches the bottom
; of the window. Nothing was moved, so the screen is still consistent with
; the note as far as this row; the caller settles.
; -----------------------------------------------------------------------------
wd_bpush:
    cmp byte [wd_bmode], 0
    je .out
    cmp byte [wd_draw], 0           ; a measure pass moves no pixels
    je .out
    push ax
    mov ax, [wd_row]
    cmp ax, [wd_bcrow]
    jbe .pop                        ; still on the break's own row
    call wd_scroll
    jc .fail
    mov ax, [wd_row]
    mov [wd_bcrow], ax
    mov word [wd_prowi], 0xFFFF     ; the vacated row holds a copy of the one
    mov byte [wd_didpush], 1        ; above it: nothing the delta cache knows
    jmp short .pop
.fail:
    mov byte [wd_bfail], 1
.pop:
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_brkdraw - one keystroke while the break is up
; in:  SI = window ptr, gfx lock held, [wd_ckok] set
; out: nothing; clobbers what a callback may
;
; ONE walk, from the checkpoint to the caret and no further - so a keystroke
; costs the caret's own row and nothing else, whatever the note weighs. No
; signature pass: the rows below the caret are not being laid out, so there
; is nothing to compare them against.
; -----------------------------------------------------------------------------
wd_brkdraw:
    push ax
    push bx
    mov word [wd_hity], 0xFFFF
    mov word [wd_wanty], 0x7FFF
    mov word [wd_dr1], 0            ; wd_walk's blank loop must not run: it
                                    ; erases rows this pass has no opinion on
    mov byte [wd_draw], 1
    mov byte [wd_sigup], 0
    mov byte [wd_clip], 0
    call wd_seedck
    mov byte [wd_bstop], 1
    mov byte [wd_didpush], 0
    mov byte [wd_bfail], 0
    call wd_walk
    mov byte [wd_resume], 0
    mov byte [wd_bstop], 0
    cmp byte [wd_bfail], 0
    jne .settle
    cmp byte [wd_didpush], 0
    je .out
    mov bx, si                      ; a push scrolled the whole band, grow box
    call OSAPI_WM_GROW              ; included (SPEC.md 11.1)
    jmp short .out
.settle:
    call wd_reconcile               ; no room left below: show the note
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_brktry - would this keystroke be cheaper as a break? then take it
; in:  SI = window ptr, pass 1 has run ([wd_dr1], [wd_curx]/[wd_cury] valid),
;      the caller has already checked [wd_brkok] and that this is a plain
;      keystroke at the caret; gfx lock held
; out: CF = 0 the break took over AND drew - the caller is done; CF = 1 it
;      did not, and the caller reflows as before. Clobbers AX/BX/CX/DX/DI
; -----------------------------------------------------------------------------
wd_brktry:
    push di
    test word [wd_tx], 7
    jnz .no                         ; rule 4: the scroll is byte-column granular
    cmp byte [wd_hashid], 0
    jne .no                         ; hidden characters break wd_ecol's
                                    ; column arithmetic (SPEC.md 65.1)
    cmp byte [wd_hastab], 0
    jne .no                         ; ...and so does a tab (SPEC.md 65.3)
    cmp byte [wd_hasfmt], 0
    jne .no                         ; formatted rows are not 8px and do not
                                    ; start at [wd_tx]: the break's scroll
                                    ; and column arithmetic both die (65.6)
    mov di, [wd_currow]             ; DI = the caret's row (banked by the
                                    ; pass-1 walk that just stood on it)
    mov ax, [wd_dr1]
    sub ax, di
    jbe .no                         ; nothing below the caret's row moved
    mul word [wd_rcols]             ; DX:AX = the cells this reflow would cost
    or dx, dx                       ; below the caret. It cannot overflow -
    jnz .yes                        ; 60 rows by 90 cells is 5,400 - but a
    cmp ax, WD_BRK_CELLS            ; multiply writes DX and saying so is
    jb .no                          ; cheaper than remembering it cannot
.yes:
    mov ax, di
    inc ax
    cmp ax, [wd_vrows]
    jae .no                         ; no row below to push the note into
    mov ax, [wd_curx]
    sub ax, [wd_tx]
    mov cl, 3
    shr ax, cl
    or ax, ax
    jz .no                          ; the caret ended at column 0, which for an
                                    ; insert means the keystroke WRAPPED it
                                    ; onto a fresh row - there is no prefix to
                                    ; keep and no tail to push. The next
                                    ; keystroke will ask again

    mov bx, [wd_ecol]               ; the caret's column BEFORE the edit, which
    xor ah, ah                      ; is exactly the prefix the scrolled copy
    mov al, [wd_eext]               ; below will duplicate - plus whatever the
    add bx, ax                      ; edit took off that row (Delete: one cell)
    cmp bx, [wd_rcols]
    ja .no                          ; a stale wd_ecol cannot reach past the band

    push bx                         ; the caret bar is about to be scrolled
    push dx                         ; down with everything else, and it would
    mov ax, [wd_ecol]               ; land in the middle of the tail. Erase it
    push cx                         ; where it STANDS, which is the column it
    mov cl, 3                       ; was at before this edit and not the one
    shl ax, cl                      ; it is at now - this row is redrawn whole
    pop cx                          ; a moment from now anyway
    add ax, [wd_tx]
    mov bx, [wd_cury]
    mov dx, bx
    add dx, [wd_gh1]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    pop dx
    pop bx

    mov ax, di
    call wd_scroll                  ; the caret's row and everything below it
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
    add ax, [wd_ty]
    mov bx, ax                      ; BX = y1
    mov dx, ax
    add dx, 7                       ; DX = y2
    pop ax                          ; AX = cells to blank
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]
    dec ax
    mov cx, ax                      ; CX = x2
    mov ax, [wd_tx]                 ; AX = x1
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.nodup:
    mov byte [wd_bmode], 1
    mov [wd_bcrow], di
    mov [wd_borig], di              ; the first row the reconcile owes a repaint
    mov word [wd_prowi], 0xFFFF     ; the caret's row was just scrolled away
    call wd_brkdraw
    mov bx, si                      ; the scroll above dragged the grow box
    call OSAPI_WM_GROW              ; down with everything else
    call wd_hire                    ; nothing settles the break but the worker
    clc
    pop di                          ; POP does not touch flags
    ret
.no:
    stc
    pop di
    ret

; -----------------------------------------------------------------------------
; wd_reconcile - take the break down and show the note
; in:  SI = window ptr, gfx lock held (UI task or the worker)
; out: nothing; clobbers what a callback may
;
; INCREMENTAL, and it can be: the break only ever scrolled rows [wd_borig] and
; below, so everything above it is still the note and still has the signature
; that says so. The band from wd_borig down is filled white and redrawn whole,
; and wd_clean is what keeps that from costing rows x width - with the band
; known blank a row's run stops at its last real character instead of padding
; to the edge to erase with (SPEC.md 27.2).
; -----------------------------------------------------------------------------
wd_reconcile:
    push ax
    push bx
    push cx
    push dx
    call wd_bounds
    mov byte [wd_bmode], 0
    mov byte [wd_resume], 0
    mov word [wd_hity], 0xFFFF      ; pass 1: the signatures, over the whole
    mov word [wd_wanty], 0x7FFF     ; note, because they have been standing
    mov word [wd_dr0], 0xFFFF       ; still since the break went up
    mov word [wd_dr1], 0
    mov byte [wd_draw], 0
    mov byte [wd_sigup], 1
    mov byte [wd_clip], 0
    call wd_walk
    mov ax, [wd_vrows]
    or ax, ax
    jz .done
    dec ax
    mov [wd_dr1], ax
    mov ax, [wd_borig]
    cmp ax, [wd_dr1]
    ja .done
    mov [wd_dr0], ax                ; the band is everything the break moved,
                                    ; whatever the signatures think: no
                                    ; signature describes a fiction
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]
    mov bx, ax                      ; BX = y1
    mov dx, [wd_bot]                ; DX = y2
    mov ax, [wd_tx]
    sub ax, WD_MARGIN               ; AX = x1, the content's own left edge
    mov cx, [wd_rgt]                ; CX = x2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [wd_prowi], 0xFFFF     ; the fill erased whatever the cache knew
    mov byte [wd_clean], 1
    mov byte [wd_draw], 1
    mov byte [wd_sigup], 0
    mov byte [wd_clip], 1
    call wd_walk
    mov byte [wd_clip], 0
    mov byte [wd_clean], 0
    mov bx, si
    call OSAPI_WM_GROW              ; the fill reached it (SPEC.md 11.1/27)
.done:
    call wd_sigmark
    push ax
    mov ax, [wd_top]                ; the reconcile draws the note as it now
    mov [wd_ptop], ax               ; is, in the view it now has
    pop ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_hire - spawn the worker, once
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
;
; Lazy on purpose: a Note Pad that never breaks never costs a task slot or a
; 512-byte stack, which on a 12-slot table is worth the byte of state. A
; refusal is normal and transient (the table can be full), so nothing is
; latched and the next break asks again.
; -----------------------------------------------------------------------------
wd_hire:
    push ax
    push bx
    cmp byte [wd_hired], 0
    jne .out
    mov ax, wd_worker
    mov bx, [wd_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [wd_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_worker - THE background task (SPEC.md 20.6): it settles the break
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
; W_PAINT, and wd_paint draws the note and clears the break.
; -----------------------------------------------------------------------------
wd_worker:
.loop:
    mov bx, [wd_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4)
    mov ax, WD_WTICKS
    call OSAPI_TASK_SLEEP
    cmp byte [wd_quit], 0           ; File > Close / Exit (SPEC.md 65.2): the
    jne .quit                       ; UI hid the window and left the teardown
                                    ; to us, because only a task can end an
                                    ; instance cleanly - see .quit below
    cmp byte [wd_bmode], 0
    jne .idle
    cmp byte [wd_hdirty], 0         ; ...or a height to recount, which is the
    jne .idle                       ; other thing worth waking up for
    cmp byte [wd_uopen], 0          ; ...or an edit group whose half-second is
    jne .idle                       ; nearly up (SPEC.md 27.9)
    call wd_stkval                  ; ...or a CAPS/NUM lamp that no longer
    cmp al, [wd_stkf]               ; matches the lock state - CapsLock alone
    je .loop                        ; emits no key EVENT, so without this poll
                                    ; the lamp waits for the next keystroke
                                    ; (the flag byte is a plain memory read,
                                    ; legal without the lock)
.idle:
    call OSAPI_WM_TOP               ; BX = frontmost visible, 0 = none
    cmp bx, [wd_win]
    jne .go
    call OSAPI_GET_TICKS
    sub ax, [wd_ktick]              ; modular, so a wrapping tick counter is
    cmp ax, WD_IDLE                 ; not a special case (SPEC.md 8)
    jb .loop
.go:
    call OSAPI_GFX_LOCK
    mov byte [wd_inwk], 1           ; wd_itinit must not claim on this task
                                    ; (SPEC.md 20.6 rule 7) - raised for the
                                    ; draw burst, cleared before the unlock
    cmp byte [wd_mopen], WD_M_NONE  ; a dropdown, the About box or a dialog is
    jne .unlock                     ; over the content (SPEC.md 65.2): every
    cmp byte [wd_about], 0          ; draw below would letter text straight
    jne .unlock                     ; through it. The debts stay raised and
    cmp word [wd_dlg], 0            ; are paid on the pass after it closes
    jne .unlock
    cmp byte [wd_sowed], 0          ; a scroll whose repaint was dropped because
    je .nosowed                     ; another click was right behind it
    mov byte [wd_sowed], 0          ; (SPEC.md 27.7.8). Cleared FIRST: a repaint
                                    ; that faults must not leave the debt to be
                                    ; paid again forever
    mov bx, [wd_win]                ; ...and ASKED, like the three draws below
    call OSAPI_WM_OBSCURED          ; it. This one drew unconditionally: covered,
    jc .nosowed                     ; it painted over the window on top of it
                                    ; (SPEC.md 11.3), and it was the one path
    mov si, [wd_win]                ; that changed this window's pixels without
    call wd_redraw                  ; telling the kernel - which is what the
                                    ; raise cache's promise rests on (11.96)
.nosowed:
    call wd_uclose                  ; half a second without an edit is what a
                                    ; user means by ONE edit, and this is the
                                    ; clock that measures it (SPEC.md 27.9).
                                    ; UNDER the lock, because every recorder
                                    ; runs inside a callback that holds it
    mov si, [wd_win]
    cmp byte [wd_bmode], 0          ; re-read UNDER the lock: the UI task may
    je .height                      ; have settled it while we waited
    mov bx, [wd_win]
    call OSAPI_WM_OBSCURED
    jc .height
    call wd_reconcile
.height:
    ; Count the note's rows, which no other walk does any more (SPEC.md 27.7),
    ; and move the thumb if that changed it. THE COUNT is not gated on the
    ; window being visible - it is arithmetic, and a covered or hidden window's
    ; height is owed the moment it comes back - and THE DRAW is gated by the
    ; call below it, like the other three in this routine.
    ;
    ; That distinction was written here as one sentence and the drawing half
    ; was wrong (SPEC.md 11.3.1): "wd_sbcheck draws only when a number moved"
    ; was offered as the reason it was safe, and a number moving is exactly
    ; what a chunked count does on a window nobody can see - WD_HCHUNK rows a
    ; pass, each raising [wd_drows]. wm_obscured answered only "is anything on
    ; TOP of me", so after the close box hid this window the bar was drawn onto
    ; the bare desktop. It answers about a hidden window now, which is what the
    ; four calls here have always meant by it.
    cmp byte [wd_hdirty], 0
    je .unlock
    call wd_bounds                  ; the walk reads [wd_ty]/[wd_rgt], and the
    call wd_hchunk                  ; window may have been resized since.
                                    ; A CHUNK of the count and not the whole of
                                    ; it (SPEC.md 27.7.3): the lock is held
                                    ; across this, so the bound on the walk is
                                    ; the bound on how long a UI action behind
                                    ; it has to wait
    mov bx, [wd_win]
    call OSAPI_WM_OBSCURED
    jc .unlock
    call wd_sbcheck
.unlock:
    mov byte [wd_inwk], 0
    call wd_stkchk                  ; the CAPS/NUM lamps (SPEC.md 65.2): the
                                    ; lock flags byte can change with no key
                                    ; EVENT arriving (CapsLock alone emits
                                    ; none), so the worker is the only thing
                                    ; that can notice. Two glyph runs, only
                                    ; on a change, gated on wm_obscured inside
    call OSAPI_GFX_UNLOCK
    jmp .loop

.quit:
    ; File > Close / Exit (SPEC.md 65.2). There is no self-close API slot: the
    ; instance normally dies through the close box (app_close_win) or through
    ; OSAPI_TASK_ALIVE noticing. So the UI half hid the window for instant
    ; feedback and set [wd_quit]; here the window record is destroyed and the
    ; very next ALIVE finds it unowned and tears the instance down through the
    ; kernel's own path - task, region, claims and record all freed inside
    ; that call. The destroy and the ALIVE are back to back on THIS task, so
    ; nothing can reuse the record slot in between.
    call OSAPI_GFX_LOCK
    mov bx, [wd_win]
    call OSAPI_WM_DESTROY
    call OSAPI_GFX_UNLOCK
    mov bx, [wd_win]
    call OSAPI_TASK_ALIVE           ; never returns: the record is gone
    jmp .quit                       ; unreachable belt-and-braces

; -----------------------------------------------------------------------------
; wd_nextrow - the pen moved to the next row: bank the signature it just
;              finished, and start the next one
; in:  [wd_row], [wd_rowh], [wd_sigup]
; out: [wd_row] advanced, [wd_rowh] = 0; [wd_dr0]/[wd_dr1] widened if the row
;      changed; preserves all registers
;
; Rows past [wd_vrows] are off the bottom of the content. The walk still
; visits them - every position below has to stay true - but they have no
; signature slot and no pixels, so they are counted and otherwise ignored.
; -----------------------------------------------------------------------------
wd_nextrow:
    push ax
    push bx
    cmp byte [wd_sigup], 0
    je .adv
    mov ax, [wd_row]
    cmp ax, [wd_vrows]
    jae .adv
    shl ax, 1
    mov bx, ax
    mov ax, [wd_rowh]
    cmp ax, [bx+wd_sig]
    je .adv                         ; same word, same pixels: leave it alone
    mov [bx+wd_sig], ax
    mov ax, [wd_row]
    cmp ax, [wd_dr0]
    jae .hi
    mov [wd_dr0], ax
.hi:
    cmp ax, [wd_dr1]
    jbe .adv
    mov [wd_dr1], ax
.adv:
    inc word [wd_row]
    mov word [wd_rowh], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rowdirty - is the row the pen is on one this pass is redrawing?
; in:  [wd_row], [wd_clip], [wd_dr0]/[wd_dr1]
; out: CF = 1 if it must NOT be drawn; preserves all registers
; -----------------------------------------------------------------------------
wd_rowdirty:
    cmp byte [wd_clip], 0
    je .yes                         ; not clipping: this is a full paint
    push ax
    mov ax, [wd_row]
    cmp ax, [wd_dr0]
    jb .no
    cmp ax, [wd_dr1]
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
; wd_sigmark - record the geometry (and the toast) the signatures describe
; in:  wd_bounds already run
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_sigmark:
    push ax
    call wd_selmark
    mov ax, [wd_tx]
    mov [wd_stx], ax
    mov ax, [wd_ty]
    mov [wd_sty], ax
    mov ax, [wd_rgt]
    mov [wd_srgt], ax
    mov ax, [wd_bot]
    mov [wd_sbot], ax
    mov ax, [wd_ctop]               ; ...and the chrome top the band was drawn
    mov [wd_sctop], ax              ; under, for wd_panmove: a View toggle
                                    ; moves the band's own top edge, and the
                                    ; blit must span from the SMALLER of the
                                    ; two tops or the moving rows fall outside
                                    ; its rect (SPEC.md 65.2)
    mov byte [wd_sigok], 1
    mov byte [wd_gchg], 0           ; this paint laid the note out under the
    pop ax                          ; geometry just recorded, so the view has
    ret                             ; been re-clamped against it

; -----------------------------------------------------------------------------
; wd_selmark - the screen now shows THIS selection (SPEC.md 27.8.2)
; out: nothing; preserves all registers
;
; The one fact wd_selqo reads. Every path that finishes a redraw sets it,
; including the ones that drew nothing: a row whose selection did not change
; is not in the dirty band, so "what the screen shows" is the live selection
; either way.
; -----------------------------------------------------------------------------
wd_selmark:
    push ax
    mov ax, [wd_sel0]
    mov [wd_osel0], ax
    mov ax, [wd_sel1]
    mov [wd_osel1], ax
    mov al, [wd_selon]
    mov [wd_oselon], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_sigsame - do the stored signatures still describe this window?
; in:  wd_bounds already run
; out: CF = 1 if they do not and the caller must repaint whole; preserves all
;
; All four tests are the layout: a resized window wraps differently, and the
; kernel white-filled its content on the way here anyway. There used to be a
; fifth and a sixth, on [wd_msg] and its generation - the toast was drawn OVER
; the text and was in no row's signature, so the keystroke that retired one
; had to erase it the only way this module could, by painting the whole
; content again. The toast is the kernel's now and is in the menu bar
; (SPEC.md 59), so it is in nothing this routine describes.
; -----------------------------------------------------------------------------
wd_sigsame:
    push ax
    cmp byte [wd_sigok], 0
    je .no
    mov ax, [wd_tx]
    cmp ax, [wd_stx]
    jne .no
    mov ax, [wd_ty]
    cmp ax, [wd_sty]
    jne .no
    mov ax, [wd_rgt]
    cmp ax, [wd_srgt]
    jne .no
    mov ax, [wd_bot]
    cmp ax, [wd_sbot]
    jne .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wd_height - walk the whole note for [wd_drows], if the note has changed
; in:  SI = window ptr, wd_bounds run; out: nothing; preserves all registers
;
; The one walk that exists to answer "how many rows", which is the one
; question a bounded walk cannot answer (SPEC.md 27.7). Every OTHER walk now
; stops at the bottom of the view, because rows below it are drawn by nobody
; and the thumb is the only thing that was ever asking - so this is where the
; note's tail is paid for, and it is paid half a second after the typing
; stops rather than on every keystroke.
;
; It preserves the two query fields because wd_onclick sets them BEFORE it
; gets here, and a walk consumes them.
;
; It comes in two sizes (SPEC.md 27.7.3). wd_height finishes the count in one
; hold, for the one caller that needs the answer exact - a click on the bar.
; wd_hchunk does WD_HCHUNK rows of it and hands the lock back, which is what
; the worker calls: the count of a 16KB note is seconds of walking, and doing
; it in one hold freezes the machine behind it with nothing on the disk and
; nothing on the glass.
;
; Chunking is legal because the gfx lock here is a mutex over the walk's
; SCRATCH and not a drawing lock - wd_height writes no framebuffer, and nine of
; wd_walk's ten call sites are UI callbacks that hold the lock already. So the
; hold is needed for a CHUNK and never for the COUNT, and "give up the machine
; if somebody else wants it" falls out of the release rather than needing the
; worker to ask anyone.
;
; The resume pair survives the release because WRAPPING IS DETERMINISTIC: row R
; begins at index I whoever computed it. An interleaved W_PAINT scribbles over
; the walk's in-flight scratch and cannot touch those two words. Only the note
; CHANGING invalidates them, and that goes through wd_hmark, which resets them.
; -----------------------------------------------------------------------------
wd_height:
    push ax
    mov ax, 0x7FFF                  ; no bound: to the last character, however
    call wd_hwalk                   ; many rows that is
    pop ax
    ret

wd_hchunk:
    push ax
    mov ax, [wd_hi]                 ; a stale seed can only mean the note shrank
    cmp ax, [wd_len]                ; without going through wd_hmark; wd_walk
    jbe .seedok                     ; would fall back to a full walk, and the
    mov word [wd_hrow], 0           ; bound below would then be a chunk's worth
    mov word [wd_hi], 0             ; of rows past where it actually starts -
.seedok:                            ; one unbounded hold, the thing being fixed
    mov ax, [wd_hrow]               ; [wd_lastrow] is a VISIBLE row and
    sub ax, [wd_top]                ; [wd_hrow] is absolute, so the bound is
    add ax, WD_HCHUNK               ; this chunk's own start plus its length
    call wd_hwalk
    pop ax
    ret

; wd_hwalk - the count itself, stopping after visible row AX
; in:  AX = the last visible row this pass may stand on, SI = window ptr
wd_hwalk:
    cmp byte [wd_hdirty], 0
    jne .go
    ret
.go:
    push ax
    push si
    mov [wd_lastrow], ax
    mov ax, [wd_hity]
    push ax
    mov ax, [wd_wanty]
    push ax
    mov word [wd_hity], 0xFFFF
    mov word [wd_wanty], 0x7FFF
    mov byte [wd_draw], 0
    mov byte [wd_sigup], 0
    mov byte [wd_clip], 0

    mov ax, [wd_hi]                 ; resume where the last chunk stopped, which
    mov [wd_sdi], ax                ; for the first one is index 0 of row 0 -
    mov ax, [wd_hrow]               ; identical to the unseeded start wd_walk
    sub ax, [wd_top]                ; would make, so the top of the note needs
    mov [wd_sdr], ax                ; no case of its own. wd_sdr is a VISIBLE
    mov byte [wd_resume], 1         ; row, so it is derived from [wd_top] HERE
    call wd_walk                    ; and not banked - the view may have moved
    mov byte [wd_resume], 0         ; between one chunk and the next

    cmp byte [wd_hdirty], 0         ; .done clears it and .stop leaves it up, so
    jne .more                       ; the flag already says which exit was taken
    mov word [wd_hrow], 0           ; finished: the next count starts at the top
    mov word [wd_hi], 0
    jmp short .fin
.more:
    mov ax, [wd_stoprow]            ; stopped: pick this up next pass
    mov [wd_hrow], ax
    mov ax, [wd_stopi]
    mov [wd_hi], ax
.fin:
    pop ax
    mov [wd_wanty], ax
    pop ax
    mov [wd_hity], ax
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_hmark - the note changed: the height is owed, and owed from the TOP
; preserves every register AND the flags (it only stores to memory), so it is a
; drop-in for the `mov byte [wd_hdirty], 1` it replaces at each of its callers
;
; The reset is the whole reason this is a routine. A chunked count that is
; part-way down a note holds a (row, index) pair that describes the note it
; started on; an edit makes that pair name a row that may no longer begin
; there, so the count has to start over. Raising the flag and forgetting the
; pair are the same event and must not be separable.
; -----------------------------------------------------------------------------
wd_hmark:
    mov byte [wd_hdirty], 1
    mov word [wd_hrow], 0
    mov word [wd_hi], 0
    jmp wd_xdrop                    ; ...and the row index with them (SPEC.md
                                    ; 27.13): it is the same event - a table of
                                    ; where rows BEGIN means nothing under a
                                    ; layout where they begin somewhere else.
                                    ; A tail call, because wd_xdrop preserves
                                    ; the flags too and this routine's whole
                                    ; contract is that it is a drop-in for a
                                    ; `mov byte [wd_hdirty], 1`

; -----------------------------------------------------------------------------
; wd_measure - run the walk without drawing
; in:  SI = window ptr; the query fields already set
; out: as wd_walk; preserves all registers
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
wd_measure:
    call wd_settle
    call wd_bounds
    mov byte [wd_draw], 0
    mov byte [wd_sigup], 0
    mov byte [wd_clip], 0
    call wd_walk
    ret

; -----------------------------------------------------------------------------
; wd_settle - if the visual break is up, take it down
; in:  SI = window ptr, gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_settle:
    cmp byte [wd_bmode], 0
    je .out
    jmp wd_reconcile                ; a tail call: it preserves what we do
.out:
    ret

; -----------------------------------------------------------------------------
; wd_seedck - seed the next walk at the caret's row (SPEC.md 27.4)
; wd_seedrow - seed it at row AX instead, and stop after row DX (SPEC.md 27.5)
; out: [wd_resume] set if the seed is usable, left 0 if the walk must start at
;      index 0 after all; preserves all registers
;
; wd_rows only describes rows the last completed walk reached, so a query about
; a row past [wd_rowsn] - a click on the blank space below the text - has to
; fall back. That fallback is the ONLY thing keeping a stale table from
; answering with a plausible wrong index.
; -----------------------------------------------------------------------------
; wd_seedck seeds one row EARLIER than the caret's, and walks back further
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
; the correct answer: [wd_rows] describes visible rows only, so row 0 of a
; SCROLLED view was placed by a break above the view, and nothing here can
; redo it.
wd_seedck:
    push ax
    push bx
    push cx
    push es
    mov byte [wd_resume], 0
    cmp byte [wd_ckok], 0
    je .out
    mov es, [wd_dseg]
    mov ax, [wd_ckpr]               ; the caret's row...
    call wd_rowstart                ; ...and the index it begins at. Asked
    jc .back                        ; TWICE on the slow path, which is a shift
                                    ; and a load: wd_rowstart preserves AX
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
    jne .back                       ; space (SPEC.md 65.3)
.ckw:
    call wd_ckword                  ; ...and a wrapped row is safe too as long
    jnc .seed                       ; as the edit is past its first word
.back:
    call wd_rowstart                ; ...and the index it begins at
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
    call wd_cellrun                 ; mid-word: this row was split by the cell
    jnc .seed                       ; rule, and the word began further back -
                                    ; UNLESS the word is longer than a row, in
                                    ; which case wd_wordfit never decided
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
    call wd_rowstart
    jc .out
.seed:
    mov [wd_sdi], bx
    mov [wd_sdr], ax
    mov byte [wd_resume], 1
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_ckword - may this row be seeded WITHOUT laying out the one above it?
; in:  BX = the row's start index, ES = the document segment
; out: CF = 0 yes, seed here; CF = 1 back up as before.
;      Preserves every register.
;
; SPEC.md 27.11.1. wd_wordfit is the whole of why wd_seedck backs up: standing
; at the first character of a word it measures whether that word ENDS inside
; the cells left on the row, and breaks in front of it if it does not. So the
; break that decides where this row starts is a function of the length of this
; row's FIRST WORD and of nothing else in this row - which means an edit past
; the end of that word cannot have moved it.
;
; The caret is the edit, near enough and always on the safe side: an insert
; leaves it one PAST the character it added, a backspace and a Delete leave it
; ON the edit, so the earliest index this keystroke can have touched is
; [wd_cur] - 1. THE BOUND IS THAT INDEX, NOT THE CARET, and the difference is
; the whole of the case it has to catch: the character an insert added may
; ITSELF be the space that now ends the word, and then the word was LONGER when
; the break in front of it was taken - which is precisely the decision the
; back-up exists to redo. Accepting a terminator AT [wd_cur] - 1 seeded the row
; at a start the insert had just moved. Backspace and Delete touch nothing
; below [wd_cur] and pay one extra row for the shared bound, which is the
; conservative direction. It needs no extra state kept in step.
;
; The scan is at most a row's width and stops at the caret, so it is a handful
; of byte compares against a row of layout at ~6 ms.
; -----------------------------------------------------------------------------
wd_ckword:
    push ax
    push bx
    push cx
    mov cx, [wd_cur]
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
; wd_cellrun - is the break in front of this row owned by the CELL RULE alone?
; in:  BX = the row's start index, known mid-word ([es:bx-1] is neither a space
;      nor a CR), ES = the document segment
; out: CF = 0 seed here, the walk need go no further back; CF = 1 back up as
;      before. Preserves every register.
;
; SPEC.md 27.4.2, and it is wd_ckword's other half: that one asks whether the
; edit is past the row's first word, this one asks whether there is a word-fit
; decision in front of this row AT ALL.
;
; wd_wordfit's .p2l answers "longer than a row: the cell rule owns it" and
; breaks NOTHING - a word that cannot fit a whole row is laid out left to
; right and wrapped at the margin like the pre-27.11 automaton. So inside such
; a word every row start is the one before it plus wd_rcols, anchored at the
; word's first character, and the position of THAT was settled by text the
; edit cannot reach. Backing up through it re-decides a break nobody ever
; decided: on a note that is one 249-character run, the caret's row backed up
; NINE rows to index 0 and pass 1 then laid out the whole view from the top of
; the note, on every keystroke (docs/NOTEPAD-NOTES.md 5.6).
;
; THE MARGIN IS WHY THIS COUNTS TO wd_rcols + 2 AND NOT wd_rcols + 1. The
; threshold wd_wordfit actually applies is `length > wd_rcols`, and this runs
; AFTER the edit has been applied to the buffer - so a backspace at the caret
; may already have taken one character out of the run being measured. Two
; spare characters is what makes the answer the same before and after: the run
; measured here is at least wd_rcols + 2, so the word was at least wd_rcols + 1
; before the keystroke and is at least wd_rcols + 1 after it, and both are past
; the threshold. One spare would let a word of exactly wd_rcols + 1 fall back
; to wd_rcols under a backspace, where wd_wordfit DOES have an opinion and the
; break in front of the word can move.
;
; The scan is at most a row's width of byte compares against a row of layout at
; ~6 ms, which is wd_ckword's bargain and the same one.
; -----------------------------------------------------------------------------
wd_cellrun:
    push ax
    push bx
    push cx
    mov cx, [wd_rcols]
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
; wd_rowstart - the index visible row AX begins at (SPEC.md 27.5)
; in:  AX = the row
; out: CF = 0 and BX = the index; CF = 1 = wd_rows does not describe that row
; clobbers: BX and CF; every other register preserved
; -----------------------------------------------------------------------------
wd_rowstart:
    push ax
    cmp byte [wd_rowsok], 0
    je .no
    cmp ax, [wd_rowsn]
    jae .no                         ; unsigned, so a row ABOVE the view - a
                                    ; negative index - fails here too
    shl ax, 1
    mov bx, ax
    mov bx, [bx+wd_rows]
    cmp bx, [wd_len]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wd_netseed - seed the caret-follow safety net FORWARD (SPEC.md 27.7.7)
; out: [wd_resume] set if a row start at or before the caret was found
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
; So resume at the deepest row [wd_rows] describes whose start index is at or
; before [wd_cur], and walk forward from there. Everything before that row laid
; out identically - the edit, if there was one, is AT the caret and so at or
; after the seed, which is SPEC.md 27.4's argument unchanged, and 27.11's
; lookahead cannot reach back past it either. A caret ABOVE the table walks
; back to row 0, finds nothing that qualifies and leaves [wd_resume] clear,
; which is the old behaviour and still the right answer.
; -----------------------------------------------------------------------------
wd_netseed:
    push ax
    push bx
    mov byte [wd_resume], 0
    cmp byte [wd_rowsok], 0
    je .out
    mov ax, [wd_rowsn]
    or ax, ax
    jz .out
    dec ax                          ; the deepest row the table describes
.try:
    call wd_rowstart                ; BX = where it begins, CF=1 = it does not
    jc .out
    cmp bx, [wd_cur]
    jbe .seed                       ; at or before the caret: safe to resume
    or ax, ax
    jz .out
    dec ax                          ; ...past it, so try the row above
    jmp short .try
.seed:
    mov [wd_sdi], bx
    mov [wd_sdr], ax
    mov byte [wd_resume], 1
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE ROW INDEX (SPEC.md 27.13) - random access to a row without walking to it
;
; wd_rows describes the VIEW and nothing else, so every question about a row
; outside it fell back to laying the note out from index 0. That is Up out of
; the top of the view, and it measured 5.2 s a press.
;
; This is a sparse table of the character index at which every Kth ABSOLUTE
; row begins: entry n describes row n << [wd_xksh]. It costs no walking at
; all, because SPEC.md 27.7.3's background count already visits every row in
; order and already computes exactly this - wd_xnote just keeps what was
; being thrown away.
;
; BOUNDED BY DECIMATION, not by growing. A note of nothing but newlines is one
; row per character, so at WD_MAXKB that is 16,384 rows - too many to reserve
; and awkward to claim. When the table fills, wd_xhalve keeps every second
; entry and doubles the stride: it then always spans the whole note, always
; costs WD_XN entries, and the walk from the nearest checkpoint is at most K
; rows, which grows only logarithmically in the note's length.
;
; The stride is a POWER OF TWO and is kept as its log, because every lookup
; would otherwise be a `div` - 150 clocks against a shift's 10, once per row
; of every walk in the module.
; =============================================================================
; ENTRIES ARE THE WHOLE COST MODEL, and the first version got this wrong by
; being frugal with them. A lookup lands on the checkpoint at or BEFORE the row
; wanted, so the walk that follows is up to K rows - and one keystroke runs
; FOUR walks (wd_vmove's, wd_move's, the redraw's pass 1 and wd_scrollpaint's),
; each paying that K. At 64 entries a 781-row note halves down to K = 16 and an
; Up cost 68 rows of walking, which measured 644 ms and is where 27.13's first
; cut stopped. 256 entries hold README at K = 4 with no halving at all.
;
; 512 bytes to do it, which is the right trade here twice over: a package's bss
; ships inside its image (SPEC.md 51/20.2) and Note Pad's document is a heap
; claim of its own (27.6), so this is half a kilobyte against a 16KB note and
; nothing against KERN_BUDGET, which a package does not touch.
WD_XN     equ 256               ; entries: 512 bytes, and 256 x 1 row means a
                                ; note under 256 rows is answered EXACTLY
WD_XKSH0  equ 0                 ; ...so the stride starts at 1 and doubles only
                                ; when the note proves it has to

; -----------------------------------------------------------------------------
; wd_xnote - offer the row just started to the index
; in:  [wd_row] = the visible row, [wd_i] = where it begins, wd_bounds run
; out: nothing; preserves all registers and the flags
;
; Called from wd_rstart, so it runs once per row of EVERY walk and its cost in
; the ordinary case has to be nothing: one compare against the row the table
; wants next.
;
; ONLY THE NEXT ENTRY OWED IS TAKEN, and that is what keeps the table honest.
; A seeded walk skips the rows above its seed, so a table that recorded
; whatever it happened to pass would have holes - and a lookup landing in one
; answers for a row it never saw, which is docs/FIELD-NOTES.md 4's shape (an
; index resolved against a snapshot that had shifted). Contiguous or nothing.
; -----------------------------------------------------------------------------
wd_xnote:
    pushf
    push ax
    mov ax, [wd_row]
    add ax, [wd_top]                ; ABSOLUTE: the table outlives the view,
    cmp ax, [wd_xnext]              ; and [wd_top] moves under it
    jne .out
    cmp word [wd_xn], WD_XN
    jae .out                        ; full and not yet halved: cannot happen,
                                    ; .store halves on the way out, but a
                                    ; table that could not be halved must not
                                    ; be written past its end
    push bx
    mov bx, [wd_xn]
    shl bx, 1
    mov ax, [wd_i]
    mov [bx+wd_xi], ax
    inc word [wd_xn]
    mov ax, [wd_xnext]              ; ...and where the next one goes
    push cx
    mov cl, [wd_xksh]
    mov bx, 1
    shl bx, cl
    pop cx
    add ax, bx
    mov [wd_xnext], ax
    pop bx
    cmp word [wd_xn], WD_XN
    jb .out
    call wd_xhalve
.out:
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wd_xhalve - keep every second entry and double the stride
; out: nothing; preserves all registers
;
; [wd_xnext] is deliberately NOT recomputed: it is xn * K, and halving xn while
; doubling K leaves that product exactly where it was. The next row the table
; wants is the next row it wanted.
; -----------------------------------------------------------------------------
wd_xhalve:
    push ax
    push cx
    push si
    push di
    xor si, si
    xor di, di
    mov cx, WD_XN / 2
.lp:
    mov ax, [si+wd_xi]
    mov [di+wd_xi], ax
    add si, 4                       ; every SECOND entry...
    add di, 2                       ; ...into consecutive slots
    loop .lp
    inc byte [wd_xksh]              ; ...at twice the stride
    mov word [wd_xn], WD_XN / 2
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_xdrop - the layout moved: every entry describes a note that is not this one
; out: nothing; preserves all registers and the flags
;
; Called by wd_hmark, which is already "the height is owed and owed from the
; top" - the same event, because both are invalidated by exactly the thing
; that changes where a row begins. Keeping them one call is what stops a
; future edit raising one and forgetting the other.
; -----------------------------------------------------------------------------
wd_xdrop:
    mov word [wd_xn], 0
    mov word [wd_xnext], 0
    mov byte [wd_xksh], WD_XKSH0
    ret

; -----------------------------------------------------------------------------
; wd_xrow - the checkpoint at or before ABSOLUTE row AX
; out: CF=1 none; else CF=0, AX = the checkpoint's absolute row, BX = the index
;      it begins at, CX = 1 if a LATER checkpoint exists (so the row wanted is
;      known to be within one stride) and 0 if this is the deepest
; -----------------------------------------------------------------------------
wd_xrow:
    push dx
    mov cx, [wd_xn]
    jcxz .no
    or ax, ax
    js .no                          ; above row 0: nothing describes it
    push cx
    mov cl, [wd_xksh]
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
    mov bx, [bx+wd_xi]
    cmp bx, [wd_len]
    ja .nopop                       ; a table that outlived its note
    push cx
    mov cl, [wd_xksh]
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
; wd_xseed - seed the next walk at the checkpoint at or before ABSOLUTE row AX
; in:  AX = the absolute row wanted, DX = the VISIBLE row to stop after
; out: CF=0 seeded; CF=1 the table cannot answer and the caller is unchanged.
;      Preserves all registers.
; -----------------------------------------------------------------------------
wd_xseed:
    push ax
    push bx
    push cx
    call wd_xrow
    jc .no
    sub ax, [wd_top]                ; wd_sdr is a VISIBLE row (wd_hwalk's rule)
    mov [wd_sdr], ax
    mov [wd_sdi], bx
    mov [wd_lastrow], dx
    mov byte [wd_resume], 1
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
; wd_xseedi - seed at the checkpoint at or before the character index AX, and
;             BOUND the walk when the table can prove where that row ends
; in:  AX = a character index (in practice [wd_cur])
; out: CF=0 seeded, and [wd_lastrow] set when a later checkpoint exists;
;      CF=1 not seeded and [wd_lastrow] untouched. Preserves all registers.
;
; The inverse lookup, and the one that matters most: the caret-follow net has
; the caret's INDEX and wants its ROW, which is the question that used to cost
; a walk of the whole note. wd_xi rises with the row, so a binary search finds
; it in six compares.
;
; THE BOUND IS THE HALF WORTH HAVING. If checkpoint n is the last one at or
; before the caret and checkpoint n+1 exists, then the caret's row is below
; n*K and above (n+1)*K, so the walk may stop at (n+1)*K and SPEC.md 27.7.7's
; "the walk is still unbounded, and has to be" stops being true. At the
; deepest checkpoint there is no n+1 and it is unbounded again, which is the
; old behaviour and still correct.
; -----------------------------------------------------------------------------
wd_xseedi:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                      ; the index wanted
    mov cx, [wd_xn]
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
    mov di, [di+wd_xi]
    cmp di, si
    ja .lower
    mov bx, ax                      ; wd_xi[mid] <= index: mid is a candidate
    jmp short .bs
.lower:
    mov dx, ax
    dec dx
    jmp short .bs
.found:
    mov ax, bx                      ; BX = the entry
    mov di, ax
    shl di, 1
    mov di, [di+wd_xi]
    cmp di, si
    ja .no                          ; even entry 0 is past it: impossible while
                                    ; entry 0 is index 0, and cheap to refuse
    cmp di, [wd_len]
    ja .no
    push cx
    mov cl, [wd_xksh]
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
    sub ax, [wd_top]                ; visible
    mov [wd_sdr], ax
    mov [wd_sdi], di
    mov byte [wd_resume], 1
    pop ax
    or ax, ax
    jz .okno
    push cx                         ; a later checkpoint exists, so the caret's
    mov cl, [wd_xksh]               ; row is inside this stride: stop there
    mov ax, 1
    shl ax, cl
    pop cx
    add ax, [wd_sdr]
    mov [wd_lastrow], ax
.okno:
    clc
    jmp short .out
.no:
    mov byte [wd_resume], 0         ; a REFUSAL is not a licence to resume at
    stc                             ; whatever the last caller seeded, which is
                                    ; what wd_seedck and wd_seedrow both say by
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
; wd_seedtail - seed at the DEEPEST row [wd_rows] describes, stopping after DX
; in:  DX = the visible row the walk has to reach
; out: [wd_resume] set if a seed was had; preserves all registers
;
; wd_seedrow's refusal is silent and its caller is then walking from index 0,
; so this is the fallback both of its callers want: the table stops somewhere,
; and the row BELOW where it stops is reached by walking forward a row or two
; rather than by laying the note out again from the top.
;
; It is wd_netseed's argument (SPEC.md 27.7.7) for a ROW instead of for the
; caret, and a weaker case than wd_netseed's: both of these callers are moving
; the caret, which reflows nothing, so every row above the seed laid out
; identically by inspection rather than by 27.4's reasoning.
; -----------------------------------------------------------------------------
wd_seedtail:
    push ax
    cmp byte [wd_rowsok], 0
    je .out                         ; no table at all
    mov ax, [wd_rowsn]
    or ax, ax
    jz .out                         ; it describes nothing: index 0 it is
    cmp ax, WD_MAXROWS              ; ...and [wd_rowsn] IS NOT CAPPED TO THE
    jbe .have                       ; ARRAY on the walk's natural-end path -
    mov ax, WD_MAXROWS              ; it is wd_row+1 there, so a 781-row note
.have:                              ; leaves it at 771 against 60 slots. Every
                                    ; wd_seedrow caller before this one passed
                                    ; a VISIBLE row, which wd_bounds caps at
                                    ; WD_MAXROWS, so nothing ever indexed past
                                    ; the table and the miss went unseen; this
                                    ; caller takes its row FROM [wd_rowsn] and
                                    ; would have been the first to read out of
                                    ; it
    cmp dx, ax
    jb .out                         ; THE GUARD, and the whole reason this is a
                                    ; routine rather than six lines at each
                                    ; call site: the deepest row is only a seed
                                    ; for a row BELOW it. wd_seedrow and
                                    ; wd_seedck each refuse for four different
                                    ; reasons and "the row is past the table"
                                    ; is only one of them - a caret on row 0
                                    ; asks wd_seedck for row -1 and is refused
                                    ; too, and seeding THAT at row 13 starts
                                    ; the walk below the row it is looking for,
                                    ; which then never finds it. Measured: the
                                    ; caret stopped moving on Up and the view
                                    ; stopped following it
    dec ax
    call wd_seedrow                 ; ...which also carries the bound in DX
.out:
    pop ax
    ret

wd_seedrow:
    push ax
    push bx
    mov byte [wd_resume], 0
    cmp byte [wd_rowsok], 0
    je .out
    cmp ax, [wd_rowsn]
    jae .out                        ; a row the table never described
    mov [wd_sdr], ax
    shl ax, 1
    mov bx, ax
    mov ax, [bx+wd_rows]
    cmp ax, [wd_len]
    ja .out
    mov [wd_sdi], ax
    mov [wd_lastrow], dx
    mov byte [wd_resume], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_chrome - the whole in-window chrome (SPEC.md 65.2): the nine-title menu
;             bar, then whichever of ribbon / ruler / status bar the View
;             toggles have on. Each strip painter fills its own band, so this
;             is correct over a fresh white fill AND as a repair after a
;             dropdown - the fill is one call per strip and the price of
;             being callable from both.
; in:  SI = window ptr, gfx lock held, wd_bounds run
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_chrome:
    call wd_mbar
    cmp byte [wd_vrib], 0
    je .norib
    call wd_ribbon
.norib:
    cmp byte [wd_vrul], 0
    je .norul
    call wd_ruler
.norul:
    cmp byte [wd_vsta], 0
    je .out
    call wd_status
.out:
    ret

; -----------------------------------------------------------------------------
; wd_paint - W_PAINT: draw the buffer and the caret
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_paint:
    push ax
    mov byte [wd_mopen], WD_M_NONE  ; a kernel repaint painted the content
    mov byte [wd_mhi], 0xFF         ; clean, so any dropdown, About box or
    mov byte [wd_about], 0          ; dialog is GONE from the pixels: drop the
    mov word [wd_dlg], 0            ; state with them rather than redraw a
    mov byte [wd_pend], 0           ; (...and the dirty prompt's pending
                                    ; action and the confirm session end with
                                    ; their dialogs, SPEC.md 65.4/65.7)
                                    ; menu the user has visibly lost
                                    ; (SPEC.md 65.2)
    call wd_bounds
    mov byte [wd_bmode], 0          ; whatever the break was showing, this
    mov word [wd_prowi], 0xFFFF     ; draws the NOTE over a filled content
    mov byte [wd_resume], 0
    mov word [wd_hity], 0xFFFF      ; no queries: this pass is here to draw
    mov word [wd_wanty], 0x7FFF
    cmp byte [wd_gchg], 0           ; a resize got here through W_PAINT, which
    je .laidout                     ; is not wd_redraw and so has clamped
                                    ; nothing: measure the note under the new
                                    ; wrap width and put the view back inside
                                    ; it. wd_bmode is 0 above, so wd_settle
                                    ; inside wd_measure is a no-op and this is
                                    ; just the walk
    call wd_hmark                   ; the wrap width moved, so every row start
                                    ; moved with it: the height is owed, and
                                    ; owed from the TOP (SPEC.md 27.7.5)
    mov ax, [wd_vrows]              ; ...and this walk is BOUNDED to the view
    mov [wd_lastrow], ax            ; like every other one (SPEC.md 27.7.1).
    call wd_measure                 ; It used to run to the last character for
                                    ; one number - the total - and drew not a
                                    ; pixel while it did, so a resize was the
                                    ; whole note walked INVISIBLY before the
                                    ; first row appeared
    mov ax, [wd_top]                ; The bound is also what answers the clamp,
    call wd_scrollto                ; exactly, without the total: a walk that
    mov byte [wd_resume], 0         ; STOPPED proved the note reaches past the
.laidout:                           ; bottom of the view, so [wd_top] is still
                                    ; good and wd_scrollmax cannot bite; one
                                    ; that ENDED set [wd_drows] to the truth on
                                    ; its way out and cleared the debt this
                                    ; block raised, so the clamp is right. The
                                    ; walk's own exit decides which, which is
                                    ; why nothing here tests for it
    mov byte [wd_draw], 1
    mov byte [wd_sigup], 1          ; the content was white-filled on the way
    mov byte [wd_clip], 0           ; here, so this pass draws every row AND is
    mov byte [wd_clean], 1          ; the baseline every later incremental
    mov ax, [wd_vrows]              ; ...and it stops at the bottom of the view
    mov [wd_lastrow], ax            ; like every other walk, because a full
                                    ; repaint of a 16KB note otherwise walks
                                    ; 16KB of it to draw one screenful - and
                                    ; every scroll step is a full repaint
    mov ax, [wd_top]                ; ...and it STARTS at the top of the view
    mov dx, [wd_vrows]              ; rather than at index 0 (SPEC.md 27.13).
    call wd_xseed                   ; The bound above stopped it walking past
                                    ; the view; this stops it walking TO the
                                    ; view, which a raise of a window scrolled
                                    ; halfway down a long note paid in full
    call wd_walk                    ; (SPEC.md 27.7/27.2)
    mov byte [wd_resume], 0         ; SPENT HERE, like every other wd_walk site
                                    ; (wd_hwalk, wd_brkdraw, wd_move, wd_vmove,
                                    ; wd_onclick, wd_dragsel, wd_redraw's
                                    ; .done). wd_xseed sets it and wd_paint
                                    ; cleared it only on the way IN, so from
                                    ; here it stayed set with [wd_sdr]/[wd_sdi]
                                    ; still loaded - and the next walk that
                                    ; seeds nothing of its own resumes at the
                                    ; top of THIS view. wd_measure is that
                                    ; walk, and wd_hmove reaches it bare
                                    ; whenever [wd_ckok] is 0
    mov byte [wd_clean], 0          ; ...and because it WAS filled, a row's run
    call wd_sigmark                 ; stops at its last character instead of
    mov ax, [wd_top]                ; padding to the band's edge to erase with
    mov [wd_ptop], ax               ; ...and the screen now shows THIS view
    pop ax
    call wd_sbar                    ; the fill took the bar with it
    call wd_chrome                  ; ...and the chrome strips' rules, which
                                    ; only a full fill can have erased
    call wd_sheet                   ; ...and the sheet's edges (SPEC.md 65.11)
    call wd_hire                    ; the worker exists from the first paint
                                    ; now (SPEC.md 65.2, Frotz's precedent):
                                    ; the CAPS/NUM lamps need its poll -
                                    ; CapsLock alone emits no key event - and
                                    ; File > Close needs a task to die on.
                                    ; Idempotent, and a refusal (task table
                                    ; full) is retried on every paint
    call wd_hirechk                 ; this walk stopped at the bottom of the
    ret                             ; view, so the height is still owed

; -----------------------------------------------------------------------------
; wd_hguess - what the note's LENGTH alone says about its height (SPEC.md 27.7.3)
; in:  [wd_rcols] valid (wd_bounds has run); out: [wd_drows] raised to it
; preserves all registers
;
; The chunked count takes seconds on a long note, and until it lands the only
; thing [wd_drows] holds is what the first screenful's bounded walk reached -
; so a 781-row file claimed to be 18 rows tall and the bar was drawn for a
; document that does not exist. This is the arithmetic answer available for
; nothing: the characters, divided by the cells a row holds.
;
; IT CAN ONLY EVER BE TOO SMALL, which is what makes it safe to publish. A row
; holds at most [wd_rcols] cells, so a note of L characters needs at least
; L/cols rows - and every newline ends a row EARLY, which can only push the
; real number up. That is exactly [wd_drows]'s existing direction of error
; (SPEC.md 27.7: a lower bound, never lowered), so nothing downstream needs a
; new rule and the walk goes on correcting it upward as it always did.
;
; The font is fixed-width, so "cells a row holds" is not an average of
; anything - it is [wd_rcols], which wd_bounds has just computed for the row
; buffer. One DIV, in a routine that has already made two far calls into the
; kernel at PERFORMANCE.md's ~756us apiece: under 2% of what it is riding on.
; -----------------------------------------------------------------------------
wd_hguess:
    push ax
    push cx
    push dx
    mov cx, [wd_rcols]
    jcxz .out                       ; a window too narrow to hold a cell: no
    mov ax, [wd_len]                ; geometry to divide by, and nothing to say
    xor dx, dx                      ; [wd_len] is capped at WD_MAXKB, so the
    div cx                          ; dividend never needs DX and cannot overflow
    cmp ax, [wd_drows]
    jbe .out                        ; never LOWERED, the one rule this shares
    mov [wd_drows], ax              ; with every other writer of it
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_hirechk - a height debt outlived this paint: make sure a worker exists
; preserves all registers
;
; The predicate in ONE place, called from both ends of the drawing: every exit
; of wd_redraw, and wd_paint - which is W_PAINT, and W_PAINT is the only thing
; that draws a window opened by DOUBLE-CLICKING A DOCUMENT. That launch loads
; the file in the entry proc and never calls wd_redraw at all, so the note
; whose height most needs counting - a whole file, arriving at once - was the
; one note that never got a worker to count it. It sat at the first
; screenful's lower bound until some later edit happened to hire one, and
; before then the first click paid the entire count in a single hold.
; -----------------------------------------------------------------------------
wd_hirechk:
    push ax
    cmp byte [wd_hdirty], 0
    je .out
    mov ax, [wd_drows]              ; a note that FITS needs no worker: the
    cmp ax, [wd_vrows]              ; walk that drew it ran to the note's end
    jbe .out                        ; and cleared the debt on the way
    call wd_hire
.out:
    pop ax
    ret

; =============================================================================
; The document claim (SPEC.md 27.6/50.3)
; =============================================================================

; -----------------------------------------------------------------------------
; wd_resize - make the document claim AX kilobytes
; in:  AX = the wanted size in KB (clamped to WD_KB0..WD_MAXKB)
; out: CF=0 with [wd_dseg]/[wd_capkb]/[wd_cap] updated, or CF=1 and all three
;      unchanged; preserves every register
;
; ALWAYS through OSAPI_MEM_REGROW, never claim-copy-free: a regrow extends in
; place when the paragraphs above it are free, so it needs the DIFFERENCE
; rather than old + new at once, and when it does have to move it brings the
; bytes with it (SPEC.md 50.3.1). Shrinking always succeeds in place, which
; is what makes File > New's give-back free.
; -----------------------------------------------------------------------------
wd_resize:
    push ax
    push dx
    cmp ax, WD_KB0
    jae .lo
    mov ax, WD_KB0
.lo:
    cmp ax, WD_MAXKB
    jbe .hi
    mov ax, WD_MAXKB
.hi:
    cmp ax, [wd_capkb]
    je .same                        ; already that size: nothing to ask for
    push ax
    mov dx, [wd_dseg]
    call OSAPI_MEM_REGROW           ; out CF=0 and DX = the base NOW
    pop ax
    jc .out                         ; refused: the old claim stands untouched
    mov [wd_dseg], dx               ; ...and a grow that MOVED reports a new
                                    ; base, which is the whole reason DX is
                                    ; the answer (SPEC.md 50.3.1)
    push ax                         ; --- the CHP claim, in lockstep (SPEC.md
    mov dx, [wd_cseg]               ; 65.3): one attribute byte per character,
    call OSAPI_MEM_REGROW           ; so the two claims are always the same
    pop ax                          ; size. If the mirror is refused, the text
    jnc .cok                        ; claim is put back - a shrink (or a
    push ax                         ; same-size ask) always succeeds in place,
    mov ax, [wd_capkb]              ; so the pair stays consistent and the
    mov dx, [wd_dseg]               ; caller sees one refusal
    call OSAPI_MEM_REGROW
    mov [wd_dseg], dx
    pop ax
    stc
    jmp short .out
.cok:
    mov [wd_cseg], dx
    mov [wd_capkb], ax
    push cx                         ; CL is borrowed for the shift and PUT
    mov cl, 10                      ; BACK: wd_load banks its text length in
    shl ax, cl                      ; CX across this call, and a public
    pop cx                          ; routine preserves what it does not
                                    ; answer with (SPEC.md 1)
    mov [wd_cap], ax                ; WD_MAXKB * 1024 fits a word by design
.same:
    clc
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_fitclaim - size the claim to the note plus one kilobyte to type into
; out: nothing (all registers and the flags preserved)
;
; What a load ends with, on the way out of both its paths. wd_load opens the
; claim to WD_MAXKB before the read because nothing knows the file's size
; until the read reports it; this is the other half of that, and it runs even
; when the read failed - a refused load must not leave eight kilobytes of heap
; held for a note that did not change.
; -----------------------------------------------------------------------------
wd_fitclaim:
    pushf
    push ax
    push cx
    mov ax, [wd_len]
    add ax, 1023                ; the note's own whole kilobytes...
    mov cl, 10
    shr ax, cl
    add ax, WD_GROWKB           ; ...plus one to type into
    call wd_resize              ; a shrink always succeeds in place
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wd_room - make sure one more character fits
; out: CF=0 there is room at [wd_len], CF=1 the note is as big as it can get
; clobbers: flags
;
; The growth point, and the only one. A keystroke that would fill the claim
; asks for another kilobyte first; a refusal - the heap's or WD_MAXKB's - is
; the keystroke being dropped, which is what a full note did before it could
; grow at all.
; -----------------------------------------------------------------------------
wd_room:
    push ax
    mov ax, [wd_len]
    cmp ax, [wd_cap]
    jb .yes
    mov ax, [wd_capkb]
    add ax, WD_GROWKB
    call wd_resize
    jc .no
    mov ax, [wd_len]
    cmp ax, [wd_cap]
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
; wd_stghold - claim the save's .DOC staging buffer (SPEC.md 65.4)
; in:  [wd_len]
; out: CF=0 with [wd_stgseg] set and ES = it, or CF=1 (the toast is already
;      set); preserves every other register
;
; Sized from the note, not fixed: the image is the FIB, the text, its CHP
; twin and the PAP table. It is a SECOND claim and it is transient - held
; only across the write - because the expansion grows and the document claim
; is sized for the document. A refusal is an ordinary path: the note is still
; there and still editable, it just cannot reach the disk until something
; gives memory back.
; -----------------------------------------------------------------------------
wd_stghold:
    push ax
    push cx                         ; CL is the shift count below, and the
    push dx                         ; header promises every other register
    mov ax, WD_DOCCAP               ; the WHOLE ceiling, not a size computed
                                    ; from the document: a real Word file is
                                    ; the text plus FKP pages, and how many
                                    ; pages it needs is what the writer finds
                                    ; out as it goes (SPEC.md 65.4). The claim
                                    ; is transient - held only across the
                                    ; write - so taking it whole costs the
                                    ; machine nothing between saves
.kb:
    call OSAPI_MEM_CLAIM            ; out CF=0 and DX = the base segment
    jc .no
    mov [wd_stgseg], dx
    mov es, dx
    clc
    jmp short .out
.no:
    mov word [wd_stgseg], 0
    mov ax, wd_e_nomem
    call wd_saymsg
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_stgdrop - hand the staging buffer straight back
; out: nothing (all registers and the flags preserved)
; -----------------------------------------------------------------------------
wd_stgdrop:
    pushf
    push ax
    push dx
    mov dx, [wd_stgseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [wd_stgseg], 0
.out:
    pop dx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wd_save - write the document as a native .DOC (SPEC.md 65.4)
; in:  nothing (the buffers and their lengths)
; out: nothing; the outcome is said as a toast; preserves all registers
;
; The whole image - the 16-byte FIB, the text, the CHP bytes and the PAP
; table - is assembled into the staging claim and written with ONE
; OSAPI_FILE_WRITE. Always the native format, whatever the name's extension,
; as the real product's Save was. A success clears [wd_dirty].
; -----------------------------------------------------------------------------
wd_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wd_stghold             ; ES = the staging claim, or a toast and out
    jc .out
    call wd_goto                ; the folder this document belongs to, if the
                                ; volume has been moved since (SPEC.md 19.2)
    call wd_isrtf               ; a .RTF name is the user naming a FORMAT,
    jnc .rtf                    ; and it is the one extension Save honours
    call wd_docimg              ; the whole Word file - FIB, text, FKPs,
    jc .big                     ; STSH, plcfsed, bin tables, DOP (65.4)
    jmp short .write
.rtf:
    call wd_rtfimg              ; ...or the RTF text (SPEC.md 65.8)
    jc .big
.write:
    mov cx, [wd_dend]           ; ES:BX = the image, DX:CX its byte count
    xor bx, bx
    xor dx, dx
    mov si, wd_name
    call OSAPI_FILE_WRITE
    jc .err
    mov byte [wd_dirty], 0      ; the disk matches the document again (65.4)
    mov si, wd_m_saved
    call wd_setmsg
    jmp short .done
.big:
    mov ax, wd_m_toobig         ; more FKP pages than the staging claim holds
    call wd_saymsg              ; - refused whole, the document still open
    jmp short .done             ; and still editable (SPEC.md 65.4)
.err:
    call wd_errmsg              ; AX = FERR_* -> the toast
.done:
    call wd_stgdrop
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
; wd_isrtf - does the document's name end '.RTF'?
; out: CF=0 = yes; preserves every register
;
; The one extension that decides a format (SPEC.md 65.8). Everything else -
; including a name with no extension at all - writes the Word file, which is
; what the real product's Save did.
; -----------------------------------------------------------------------------
wd_isrtf:
    push ax
    push bx
    mov bx, wd_name
.find:
    mov al, [bx]
    or al, al
    jz .no
    inc bx
    cmp al, '.'
    jne .find
    mov al, [bx]
    call wd_upc
    cmp al, 'R'
    jne .no
    mov al, [bx+1]
    call wd_upc
    cmp al, 'T'
    jne .no
    mov al, [bx+2]
    call wd_upc
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

; wd_upc - AL to upper case. Preserves all others.
wd_upc:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; -----------------------------------------------------------------------------
; wd_load - read a file into the document (SPEC.md 65.4)
; in:  nothing (wd_name)
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
wd_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, WD_LSTGKB           ; the staging claim, transient across the read
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [wd_stgseg], dx
    call wd_goto                ; the folder dance on the way in
    mov es, [wd_stgseg]
    xor bx, bx                  ; ES:BX = staging, DX:CX its capacity
    mov cx, WD_LSTGKB * 1024
    xor dx, dx
    mov si, wd_name
    call OSAPI_FILE_READ        ; DX:AX = the file's size; bigger than the
    jc .err                     ; capacity is FERR_BIG decided from the
                                ; directory entry, buffer untouched (18.4)
    push ax                     ; the byte count, kept across the sniffs
    call wd_isrtfimg            ; '{\rtf' -> the RTF reader (SPEC.md 65.8)
    jc .nortf
    pop ax
    call wd_rtfparse
    jc .bad
    jmp short .loaded
.nortf:
    pop ax
    cmp ax, WD_HDRPAGE
    jb .plain
    mov es, [wd_stgseg]
    cmp word [es:WDF_IDENT], WD_DOCMAGIC
    jne .plain

    ; --- a real Word file: FIB, pieces, FKPs, the lot (SPEC.md 65.4) ------
    call wd_docparse            ; CF=1 = refused whole, document untouched
    jc .bad
.loaded:
    call wd_clamp               ; caret to the top; clears the has* flags,
                                ; the tail and the typing attrs (65.3)
    call wd_ldpost              ; ...then the loaded facts go back
    mov byte [wd_dirty], 0
    mov si, wd_m_loaded
    call wd_setmsg
    cmp byte [wd_dtrunc], 0
    je .done0
    mov ax, wd_m_trunc
    call wd_saymsg
.done0:
    jmp .done

.plain:
    ; --- plain-text import (65.4): fold from staging into the document -----
    push ax                     ; the byte count
    mov ax, WD_MAXKB            ; open the claim to its ceiling; the fold
    call wd_resize              ; only ever SHRINKS what arrived
    pop ax
    jc .nomem3
    mov cx, ax
    push ds
    mov ds, [cs:wd_stgseg]      ; DS:SI walks what arrived...
    mov es, [cs:wd_dseg]        ; ...and ES:DI writes the kept characters
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
    cmp al, 9                   ; tabs import (SPEC.md 65.3); wd_ldscan
    je .store                   ; re-raises the flag after the clamp
    cmp al, 32
    jb .skip
    cmp al, 126
    ja .skip
.store:
    cmp di, [cs:wd_cap]
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
    mov [wd_len], di
    push ax                     ; a plain-text load arrives undressed: its
    push cx                     ; CHP bytes are all zero (SPEC.md 65.3)
    push di
    mov es, [wd_cseg]
    mov cx, di
    jcxz .nochp
    xor di, di
    xor al, al
    rep stosb
.nochp:
    pop di
    pop cx
    pop ax
    call wd_clamp               ; a shorter file must not leave the caret
                                ; past the end of it
    call wd_ldscan              ; any tab that survived re-raises the flag
    mov byte [wd_dirty], 0
    mov si, wd_m_loaded
    call wd_setmsg
    test dh, dh
    jz .done
    mov ax, wd_m_trunc
    call wd_saymsg
    jmp short .done

.bad:
    mov ax, wd_m_baddoc         ; refused whole: the document is untouched
    call wd_saymsg              ; (SPEC.md 65.4)
    jmp short .done
.err:
    call wd_errmsg
    jmp short .done
.nomem3:
    mov ax, wd_e_nomem
    call wd_saymsg
.done:
    call wd_fitclaim            ; give back what the file did not need
    call wd_stgdrop             ; ...and the staging claim with it
    jmp short .out
.nomem:
    mov ax, wd_e_nomem
    call wd_saymsg
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
; wd_ldscan - what a load must rediscover: tabs and hidden text (SPEC.md 65.4)
; in:  the document and CHP claims loaded, [wd_len] set (wd_clamp has run)
; out: [wd_hastab] / [wd_hashid] raised as found; preserves all registers
; A ¶ mark's CHP byte is a PAP index (65.3), so the hidden scan steps over
; marks - an index with bit 6 set is not hidden text.
; -----------------------------------------------------------------------------
wd_ldscan:
    push ax
    push bx
    push cx
    push di
    push es
    mov cx, [wd_len]
    jcxz .out
    mov es, [wd_dseg]           ; --- tabs: one repne scasb ---
    xor di, di
    mov al, 9
    cld
    repne scasb
    jne .notabs
    mov byte [wd_hastab], 1
.notabs:
    mov cx, [wd_len]            ; --- hidden: text and CHP in step ---
    xor bx, bx
    push ds
    mov ds, [cs:wd_cseg]        ; DS = the CHP, ES = the text
.h:
    mov al, [es:bx]
    cmp al, 13
    je .hn                      ; a mark's byte is an index, not attrs
    test byte [bx], WDAT_HID
    jz .hn
    mov byte [cs:wd_hashid], 1
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
; wd_errmsg - turn a FERR_* code into the toast string
; in:  AX = FERR_* (SPEC.md 18.4)
; out: nothing - the string is said as a toast; preserves all registers
; -----------------------------------------------------------------------------
wd_errmsg:
    push ax
    push bx
    cmp ax, FERR_BIG
    jbe .known
    mov ax, FERR_IO             ; an unknown code is still a disk problem
.known:
    mov bx, ax
    shl bx, 1
    mov ax, [bx+wd_errtab]
    call wd_saymsg
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_ins - insert AL at the caret, which then sits after it
; in:  AL = the character; out: nothing; preserves all registers
;
; The gap is opened right to left because source and destination overlap, and
; by hand rather than with `rep movsb` for a reason that is easy to forget: a
; callback is entered with ES = KERNEL_SEG (SPEC.md 20.1), so a string move
; would write the gap into the KERNEL's memory at our offsets.
; -----------------------------------------------------------------------------
wd_ins:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dl, al
    cmp al, 9                       ; a tab entered: the column-arithmetic
    jne .nt9                        ; fast paths stand down (SPEC.md 65.3;
    mov byte [wd_hastab], 1         ; set-only, conservative, like wd_hashid)
.nt9:
    call wd_room                    ; grow by a kilobyte if this is the
    jc .out                         ; keystroke that fills the claim; a
                                    ; refusal drops it, as a full note always
    call wd_hmark                   ; the note is a different length, so it
                                    ; may be a different number of rows, and
                                    ; [wd_drows] is a lower bound until
                                    ; something walks the whole of it
    mov es, [wd_dseg]               ; did (SPEC.md 27.6)
    mov bx, [wd_len]
    mov cx, bx
    sub cx, [wd_cur]                ; CX = the bytes to the right of the caret
    jcxz .place
    mov si, bx
    dec si                          ; SI = the last live byte...
    mov di, bx                      ; ...and DI one past it: the runs overlap
    push ds                         ; and the gap opens UPWARD, so backwards
    mov ds, [wd_dseg]               ; (SPEC.md 27.12). movsb is DS:SI -> ES:DI
    std                             ; and both ends are the note
    rep movsb
    cld                             ; SPEC.md 1: never leave DF set
    pop ds
.place:
    mov bx, [wd_cur]
    mov [es:bx], dl
    ; --- the same gap on the CHP claim (SPEC.md 65.3), in lockstep: open it
    ; backwards exactly as above, and the new cell takes the typing attrs
    mov es, [wd_cseg]
    mov cx, [wd_len]
    sub cx, bx
    jcxz .cput
    mov si, [wd_len]
    dec si
    mov di, [wd_len]
    push ds
    mov ds, [wd_cseg]               ; the operand reads through the OLD DS
    std
    rep movsb
    cld
    pop ds
.cput:
    mov dl, [wd_chp]
    mov [es:bx], dl
    test dl, WDAT_HID               ; a hidden character just entered: the
    jz .nohid                       ; cheap column paths stand down (65.1)
    mov byte [wd_hashid], 1
.nohid:
    inc word [wd_len]
    inc word [wd_cur]
    mov byte [wd_dirty], 1          ; the document differs from the disk (65.4)
    mov ax, bx                      ; ...and remember it, which is what makes
    mov cx, 1                       ; a burst of typing one undoable edit
    call wd_urec_ins                ; (SPEC.md 27.9)
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
; wd_del - delete the character the caret sits in front of
; out: nothing; preserves all registers. A caret at the end deletes nothing.
; -----------------------------------------------------------------------------
wd_del:
    push bx
    push cx
    mov bx, [wd_cur]
    cmp bx, [wd_len]
    jae .out                        ; a caret at the end deletes nothing
    mov cx, 1
    call wd_delspan                 ; the run is the primitive now (SPEC.md
                                    ; 27.8), and it is what records the undo
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_move - put the caret on the row [wd_wanty] at column [wd_wantx]
; in:  SI = window ptr, the two query fields set
; out: [wd_cur] moved if that row exists; preserves all registers
; -----------------------------------------------------------------------------
wd_move:
    push ax
    push dx
    mov word [wd_hity], 0xFFFF      ; one query at a time
    mov ax, [wd_wanty]              ; the VISIBLE ROW it names (SPEC.md 65.6)
    mov dx, ax                      ; is the ONLY row this walk has to visit
                                    ; (SPEC.md 27.5); negative = above the
                                    ; view, which is not in [wd_rows]...
    or ax, ax
    js .above
    call wd_seedrow
    cmp byte [wd_resume], 0
    jne .bound                      ; the table describes it: one row walked
    call wd_seedtail                ; ...and when it does NOT - which is every
                                    ; Down on the bottom visible row, the row
                                    ; below the view being one past the table -
                                    ; resume at the deepest row it DOES hold
    jmp short .bound
.above:
    push ax                         ; ...but the ROW INDEX describes rows the
    mov ax, dx                      ; view has never contained (SPEC.md 27.13),
    add ax, [wd_top]                ; which is what Up out of the top wants: it
    call wd_xseed                   ; takes an ABSOLUTE row, and DX is visible
    pop ax                          ; (wd_xseed sets the bound itself, and
                                    ; .bound below setting it again is the same
                                    ; value - one place, whichever path ran)
.bound:
    ; THE BOUND IS SET ON EVERY PATH, and that is the whole fix (SPEC.md
    ; 27.7.9). [wd_lastrow] is a one-shot that wd_walk resets to 0x7FFF, so a
    ; caller which does not set it walks the WHOLE NOTE - "slow and never
    ; wrong", which wd_seedrow's silent refusal turned into the common case:
    ; Down on the bottom visible row asks for a row one past the table, got no
    ; seed AND no bound, and re-laid out all 781 rows of README.TXT to find the
    ; row directly under the caret. Measured on a 4.77MHz 8088: 4,663 ms of a
    ; 4,866 ms keystroke, on the most used key in the editor.
    mov [wd_lastrow], dx
    call wd_measure
    mov byte [wd_resume], 0
    mov ax, [wd_wanti]
    cmp ax, 0xFFFF
    je .out                         ; no such row: the caret stays put, which
    mov [wd_cur], ax                ; is what Up on the first line should do

    ; The caret moved but nothing else did, so the repaint may resume too - at
    ; whichever of the two rows comes FIRST. Up lands on the row above the
    ; checkpoint, and seeding at the checkpoint would then walk straight past
    ; the caret without ever finding it, which draws the bar at (0,0).
    mov ax, [wd_wanty]              ; already a row (SPEC.md 65.6)
    or ax, ax
    js .out

    ; THE TWO ROWS ARE THE WHOLE OF IT (SPEC.md 27.4.1). wd_ask folds the caret
    ; into a row's signature and a caret move changes nothing else, so the only
    ; rows whose signatures can differ are the one it left and the one it
    ; arrived on. The seed below already puts the walk at the FIRST of them;
    ; this records the SECOND, so wd_redraw's pass 1 can stop there instead of
    ; laying out the rest of the view to be told nothing moved.
    ; [wd_ckpr] is still the row the caret CAME FROM at this point - the branch
    ; below is what moves it back - so the two are in hand together here and
    ; nowhere else.
    mov [wd_mvbot], ax
    cmp ax, [wd_ckpr]
    jae .arm                        ; already at or after the checkpoint
    push ax                         ; ...moving BACKWARDS, so the deeper of the
    mov ax, [wd_ckpr]               ; two is the row being left
    mov [wd_mvbot], ax
    pop ax
    cmp byte [wd_rowsok], 0
    je .out
    cmp ax, [wd_rowsn]
    jae .out
    push bx
    mov [wd_ckpr], ax
    shl ax, 1
    mov bx, ax
    mov ax, [bx+wd_rows]
    mov [wd_ckpi], ax
    pop bx
.arm:
    mov byte [wd_fast], 4           ; A CARET MOVE, and it used to say 3.
                                    ; wd_fastok*'s contract numbers the kinds
                                    ; 1 insert, 2 backspace, 3 forward Delete,
                                    ; 4 a caret move, and they carry different
                                    ; PERMISSIONS: 1..4 may resume the walk,
                                    ; but only 1..3 may enter the visual break.
                                    ; wd_move wants the first and never the
                                    ; second - "a caret move reflowed nothing",
                                    ; as wd_redraw's own comment says - and 3
                                    ; granted it both. It was invisible in a
                                    ; 29-column window only because wd_brktry
                                    ; needs WD_BRK_CELLS = 60 cells below the
                                    ; caret and one row there is 29; widen the
                                    ; window past 60 columns and Up entered the
                                    ; break, on a stale [wd_ecol] left by some
                                    ; earlier edit, which is a phantom line
                                    ; break for as long as the settle takes
.out:
    mov byte [wd_resume], 0
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_vmove - Up / Down: the same column, one row away
; in:  SI = window ptr, DX = -1 (up) or +1 (down), in ROWS (SPEC.md 65.6)
; out: nothing; preserves all registers
;
; It measures twice: once to find the pixel the caret is at, then again for
; the index at that column on the neighbouring row. Two walks of at most 512
; characters, once per keystroke.
; -----------------------------------------------------------------------------
wd_vmove:
    push ax
    push dx
    call wd_settle                  ; before the seed, not inside wd_measure:
                                    ; a reconcile runs walks of its own and
                                    ; would spend the seed we are about to set
    mov word [wd_hity], 0xFFFF
    mov word [wd_wanty], 0x7FFF
    push dx                         ; the caret is on its checkpoint's row by
    mov dx, [wd_ckpr]               ; definition, so this walk is that row and
    call wd_seedck                  ; no more (SPEC.md 27.4/27.5)
    cmp byte [wd_resume], 0
    jne .lim
    cmp byte [wd_ckok], 0           ; THE SEED FAILED, and the bound used to go
    je .nolim                       ; with it (SPEC.md 27.7.9). wd_seedck asks
                                    ; for the row BEFORE the caret's, so a
                                    ; caret on a row past what [wd_rows]
                                    ; describes is refused - and [wd_lastrow]
                                    ; is a one-shot wd_walk resets to 0x7FFF,
                                    ; so the walk then laid out the WHOLE NOTE
                                    ; to find the row the caret is already on.
                                    ; With no checkpoint there is no row to
                                    ; bound to either, and that case is the
                                    ; old one unchanged
    call wd_seedtail                ; ...but with one, the table still reaches
                                    ; SOMEWHERE: resume there and walk forward
    cmp byte [wd_resume], 0
    jne .lim
    push ax                         ; ...and when even that is refused - the
    mov ax, dx                      ; caret's row is ABOVE what wd_rows
    add ax, [wd_top]                ; describes, which is every Up out of the
    call wd_xseed                   ; top of the view - the row index has it
    pop ax                          ; (SPEC.md 27.13)
.lim:
    or dx, dx                       ; ...unless that row is ABOVE the view,
    js .nolim                       ; where the signed limit would stop the
    mov [wd_lastrow], dx            ; walk before it had found anything
.nolim:
    pop dx
    call wd_measure                 ; [wd_curx]/[wd_currow]
    mov byte [wd_resume], 0
    mov ax, [wd_currow]             ; the neighbouring ROW - a row's y is the
    add ax, dx                      ; walk's business now (SPEC.md 65.6)
    mov [wd_wanty], ax
    mov ax, [wd_curx]
    mov [wd_wantx], ax
    call wd_move
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_hmove - Home / End: the same row, the far left or the far right
; in:  SI = window ptr, DX = the column to aim at (0 or 0x7FFF)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_hmove:
    push ax
    call wd_settle                  ; before the seed - see wd_vmove
    mov word [wd_hity], 0xFFFF
    mov word [wd_wanty], 0x7FFF
    cmp byte [wd_ckok], 0           ; the caret's row IS the checkpoint's, so
    je .measure                     ; Home and End need no walk at all to find
    mov ax, [wd_ckpr]               ; the row they are aiming at - a ROW,
    jmp short .have                 ; straight through (SPEC.md 65.6)
.measure:
    call wd_measure                 ; [wd_currow] = the row we are on
    mov ax, [wd_currow]
.have:
    mov [wd_wanty], ax
    mov [wd_wantx], dx
    call wd_move
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_onclick - W_ONCLICK: put the caret where the user pointed
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; clobbers what any window callback may
;
; The kernel only sends content clicks on the front window (SPEC.md 13), so
; there is no rect to test: every click that arrives here is ours, and the
; walk answers with the nearest character boundary - or with the end of the
; note for a click below the last line.
; -----------------------------------------------------------------------------
wd_onclick:
    push ax
    push dx
    call wd_mroute                  ; the in-window chrome first (SPEC.md
    jnc .live                       ; 65.2): an open dropdown or the About box
    pop dx                          ; owns EVERY click, and the menu bar,
    jmp .out                        ; ribbon, ruler and status strips own the
.live:                              ; clicks that land on them. CF=1 = it was
                                    ; one of those and has been fully handled
    mov [wd_hitx], cx
    mov [wd_hity], dx
    mov word [wd_wanty], 0x7FFF
    call wd_settle                  ; the pointer has to be over the NOTE
    call wd_bounds                  ; before it can be resolved (SPEC.md 27.3)
    call wd_uclose                  ; ...and a click is not typing, so whatever
                                    ; was being typed is one finished edit
                                    ; NOTHING here finishes the count any more.
                                    ; A click in the TEXT wants no total at
                                    ; all, and one on the BAR wants it only if
                                    ; it asks to go PAST what has been counted
                                    ; - which wd_sbclick tests for itself, at
                                    ; the one place that knows which row is
                                    ; being asked for (SPEC.md 27.7.6).
                                    ; Finishing it here froze the machine on
                                    ; the first bar click after opening a file,
                                    ; which is exactly when the count has got
                                    ; least far and the freeze is longest
    call wd_sbclick                 ; ...and the scroll bar is not the note
    jc .text
    pop dx
    call OSAPI_EVQ_PENDING          ; is another click right behind this one?
    or ax, ax                       ; then this scroll position is already
    jz .drawscroll                  ; superseded and drawing it is work the
    mov byte [wd_sowed], 1          ; user will never see (SPEC.md 27.7.8).
    jmp short .out                  ; The WORKER owes the last one, because the
.drawscroll:                        ; queue may not be ours to finish
    call wd_redraw                  ; wd_scrollto dropped wd_sigok, so this is
    jmp short .out                  ; the full path (SPEC.md 27.7)
.text:
    cmp dx, [wd_ty]                 ; the backstop under wd_mroute (SPEC.md
    jb .chrome                      ; 65.2): what reaches here above [wd_ty]
    cmp dx, [wd_bot]                ; is the margin band, and below [wd_bot]
    ja .chrome                      ; nothing (wd_mroute consumed the status
                                    ; strip) - both inert. Without this a
                                    ; margin press matched no row and the hit
                                    ; query's default sent the caret to the
                                    ; END of the document
    call wd_yrow                    ; the row the click landed on is the only
    jc .full                        ; one that can answer it (SPEC.md 27.5) -
    mov dx, ax                      ; found from the banked ys under formats,
    call wd_seedrow                 ; arithmetic while uniform; a refusal is
.full:                              ; the unseeded walk, slow and never wrong
    pop dx
    call wd_measure
    mov byte [wd_resume], 0
    mov byte [wd_ext], 0            ; a click re-anchors: extend mode is over
    mov ax, [wd_hiti]
    call wd_selq                    ; a press INSIDE the selection is a drag of
    jc .move                        ; the text, not a new selection (27.8.1)
    mov [wd_anchor], ax
    mov [wd_cur], ax
    push ax
    call wd_selclr
    pop ax
    jc .nosel                       ; there was no selection to erase, so the
    mov byte [wd_ckok], 0           ; band the walk resumes at would have left
.nosel:                             ; its inversion on screen
    call wd_chpsync                 ; the caret landed: typing attrs follow it
    call wd_redraw
    call wd_dragsel                 ; ...and then follow the pointer until the
    jmp short .out                  ; button comes up
.move:
    call wd_dragmove
    jmp short .out
.chrome:
    pop dx                          ; the prologue's DX; a blanked strip has
                                    ; nothing to do yet
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_clamp - hold the caret inside the buffer after a load or a New
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_clamp:
    push ax
    mov byte [wd_chp], 0            ; a new document types plain until told
    mov byte [wd_hashid], 0         ; otherwise, and carries no hidden text
                                    ; (SPEC.md 65.3; a load zeroes the CHP)
    mov byte [wd_hastab], 0         ; ...and no tabs (a load that kept some
                                    ; re-raises this after the clamp)
    mov byte [wd_hasfmt], 0         ; every paragraph is Normal again: a load
    mov byte [wd_pap_tail], 0       ; zeroes the CHP, so every ¶ is index 0,
                                    ; and the tail follows suit (SPEC.md 65.3).
                                    ; The dictionary itself is session-lived
                                    ; and stands - indices in dead text point
                                    ; nowhere
    call wd_selclr                  ; a whole new buffer: the selection and
    call wd_uclear                  ; the undo stack are about the note that
                                    ; just went away, and an undo record
                                    ; applied to a different note corrupts it
                                    ; (SPEC.md 27.9)
    mov byte [wd_ext], 0            ; ...and extend mode ends with it
    call wd_hmark                   ; a whole new note is a whole new height
    mov word [wd_top], 0            ; a NOTE row, so it names nothing once the
                                    ; note is replaced - and the top of a file
                                    ; just opened is where a reader starts
    mov byte [wd_sigok], 0          ; ...and the SCREEN's signatures describe
                                    ; the note that went away. Found the hard
                                    ; way (SPEC.md 65.4): File > New over a
                                    ; FORMATTED document cleared [wd_hasfmt]
                                    ; here, and the uniform walk that followed
                                    ; redraws rows at 8px pitch - but the old
                                    ; layout's extra leading had pushed its
                                    ; last rows BELOW the last uniform row's
                                    ; band, where no uniform row could ever
                                    ; erase them. A whole-buffer swap owes the
                                    ; full repaint anyway
    mov byte [wd_ckok], 0           ; the whole buffer just changed underneath
    mov byte [wd_rowsok], 0         ; them, and both are indices INTO it
                                    ; (SPEC.md 27.4/27.5). wd_bounds catches a
                                    ; geometry change and nothing caught this;
                                    ; every path here does in fact reach
                                    ; wd_redraw with [wd_fast] clear and walk
                                    ; in full, but that is a fact about the
                                    ; callers rather than about the data
    mov ax, [wd_len]
    cmp [wd_cur], ax
    jbe .out
    mov [wd_cur], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_fastok* - five doors onto one answer: may wd_redraw take the cheap paths
;              for this keystroke, and what does the visual break need to know
;              about it? (SPEC.md 27.4/27.3)
; in:  [wd_cur] and the note BEFORE the edit
; out: [wd_fast] = the KIND, 0 if none: 1 insert, 2 backspace, 3 forward
;      Delete, 4 a caret move. [wd_ecol] = the caret's column on its row,
;      before the edit; [wd_eext] = columns of the row below that go stale
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
; is [wd_cur] - [wd_ckpi] outright, because a row start is a character index
; and every character on a row occupies exactly one cell (a newline ends a row,
; so there cannot be one in between). Forward Delete is then the same fact plus
; one: the character it removes was ON that row, so the copy is stale one cell
; further.
;
; It is deliberately NOT a test of what the redraw will cost: pass 1 answers
; that, and it can only answer it after this has let it run cheaply.
; -----------------------------------------------------------------------------
wd_fastok:                          ; a printable at the caret
    push ax
    push bx
    push cx
    mov bx, 1
    xor cx, cx
    mov ax, [wd_cur]
    jmp short wd_fastcm
wd_fastokb:                         ; Backspace: the character that goes is one
    push ax                         ; index earlier, so that is the edit
    push bx
    push cx
    mov bx, 2
    xor cx, cx
    mov ax, [wd_cur]
    dec ax
    jmp short wd_fastcm
wd_fastokd:                         ; forward Delete: the edit is AT the caret,
    push ax                         ; and it takes a cell off the row below too
    push bx
    push cx
    mov bx, 3
    mov cx, 1
    mov ax, [wd_cur]
    jmp short wd_fastcm
wd_fastokm:                         ; Left: the caret lands one index back, so
    push ax                         ; that is the earliest cell that changes
    push bx
    push cx
    mov bx, 4                       ; a caret move is not an edit, and the
    xor cx, cx                      ; break is a thing you do to an EDIT
    mov ax, [wd_cur]
    dec ax
    jmp short wd_fastcm
wd_fastokr:                         ; Right: it lands one FORWARD, and the cell
    push ax                         ; it leaves is the one it is on now
    push bx
    push cx
    mov bx, 4
    xor cx, cx
    mov ax, [wd_cur]
wd_fastcm:
    mov word [wd_mvbot], 0x7FFF ; kind 4 arrives here as well (Left and Right),
                                ; and neither measures the row it came from -
                                ; so park the bound at "no idea" and let
                                ; wd_move be the only thing that ever sets it
    cmp byte [wd_ckok], 0
    je .out
    cmp ax, [wd_ckpi]
    jb .out
    mov [wd_fast], bl
    mov [wd_eext], cl
    mov ax, [wd_cur]
    sub ax, [wd_ckpi]
    mov [wd_ecol], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_onkey - W_ONKEY: edit the buffer, then repaint our own content
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing; preserves all registers
; Unhandled keys touch nothing; a full buffer drops the keystroke silently.
; -----------------------------------------------------------------------------
wd_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    call wd_mkey                    ; the menu system first (SPEC.md 65.2): an
    jc .out                         ; open dropdown or the About box is MODAL
                                    ; and consumes every key (Esc, arrows,
                                    ; Enter, mnemonics); with nothing open
                                    ; this claims only Alt+mnemonic, which
                                    ; opens a menu from the keyboard

%ifdef WDBENCH
    cmp al, 0x02                ; Ctrl-B - the walk bench; first, so nothing
    jne .nobench                ; below can shadow it in a -DWDBENCH build
    call wdb_run
    jmp .redraw
.nobench:
%endif
    or al, al
    jz .noctl                   ; an extended key carries no ascii, so none of
                                ; the control characters below can be one
    cmp al, WD_C_FIND
    je .kfind
    cmp al, WD_C_ESC
    je .kesc
    cmp al, WD_C_TAB
    je .ktab
    cmp al, WD_C_UNDO
    je .kundo
    cmp al, WD_C_SELALL
    je .kselall
    cmp al, WD_C_SAVE
    je .ksave
    cmp al, WD_C_BOLD               ; the formatting toggles (SPEC.md 65.3):
    je .kbold                       ; keys.cmd's Ctrl letters, less the three
    cmp al, WD_C_DUL                ; int 16h eats (Ctrl-H/I/M are BS/Tab/CR
    je .kdul                        ; - italic and hidden ride the Format
    cmp al, WD_C_SCAP               ; Character dialog instead)
    je .kscap
    cmp al, WD_C_UL
    je .kul
    cmp al, WD_C_WUL
    je .kwul
    ; --- the paragraph keys, keys.cmd's own letters (SPEC.md 65.3):
    ; C-L/C/R/J alignment, C-2 spacing (below, a NUL key), C-O/E open/close
    ; space, C-N indent, C-T hanging, C-G unhang, C-X reset
    cmp al, WD_C_LEFT
    je .kleft
    cmp al, WD_C_CENTER
    je .kcenter
    cmp al, WD_C_RIGHT
    je .kright
    cmp al, WD_C_JUST
    je .kjust
    cmp al, WD_C_OPENSP
    je .kopensp
    cmp al, WD_C_CLOSESP
    je .kclosesp
    cmp al, WD_C_INDENT
    je .kindent
    cmp al, WD_C_HANG
    je .khang
    cmp al, WD_C_UNHANG
    je .kunhang
    cmp al, WD_C_RSTP
    je .krstpara
.noctl:
    ; --- moving the caret: no edit, but the screen changes ------------------
    ; An EXTENDED key has AL = 0, and the gate matters: the numeric keypad
    ; sends '4' '6' '8' '2' '7' '1' '.' with exactly these scan codes, so
    ; without it NumLock would turn typing a digit into moving the caret.
    or al, al
    jnz .typing
    cmp ah, WD_K_F1
    je .kf1                         ; F1 - the About box (the help there is)
    cmp ah, WD_K_F4
    je .kf4                         ; F4 / Shift-F4 - repeat the search down
    cmp ah, WD_K_SF4                ; / up (SPEC.md 65.7)
    je .ksf4
    cmp ah, WD_K_F5
    je .kf5                         ; F5 - Go To... (keys.cmd EditGoTo)
    cmp ah, WD_K_F8
    je .kf8                         ; F8 - extend selection (status EXT)
    cmp ah, WD_K_LEFT
    je .left
    cmp ah, WD_K_RIGHT
    je .right
    cmp ah, WD_K_UP
    je .up
    cmp ah, WD_K_DOWN
    je .down
    cmp ah, WD_K_HOME
    je .home
    cmp ah, WD_K_END
    je .end
    cmp ah, WD_K_DEL
    je .del
    cmp ah, WD_K_INS
    je .eins                        ; Ctrl+Ins = Copy / Shift+Ins = Paste
                                    ; (keys.cmd; SPEC.md 65.3)
    cmp ah, WD_K_PGUP
    je .pgup
    cmp ah, WD_K_PGDN
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
    cmp ah, WD_K_INS                ; NumLock hands '0'/'.' over with the
    je .insk                        ; Ins/Del scans: the CHORDS are told from
    cmp ah, WD_K_DEL                ; the typing by the live shift flags
    je .delk
.typ2:
    mov byte [wd_ext], 0            ; any edit disarms extend mode (65.2)
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
    mov al, [es:0x17]               ; the CAPS/NUM lamps read; SPEC.md 65.3)
    test al, 4
    pop es
    pop ax                          ; POP touches no flags
    jnz .krst
.notsp:
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    call wd_selkill                 ; typing REPLACES a selection (SPEC.md
    jnc .insplain                   ; 27.8) - in overtype too - and the two
                                    ; halves land in one undo group
    cmp byte [wd_ovr], 0            ; OVERTYPE (Ins, SPEC.md 65.2): the char
    je .insplain                    ; under the caret goes first - except a ¶
    push bx                         ; or the end, where overtype inserts
    push es
    mov bx, [wd_cur]
    cmp bx, [wd_len]
    jae .ovno
    mov es, [wd_dseg]
    cmp byte [es:bx], 13
    je .ovno
    pop es
    pop bx
    call wd_del                     ; recorded delete + recorded insert: the
    call wd_ins                     ; replace undoes (no fast path - the row
    jmp .edited                     ; changed in two places)
.ovno:
    pop es
    pop bx
.insplain:
    call wd_fastok                  ; a printable at the caret is THE case the
    jmp short .doins                ; incremental paths exist for (SPEC.md
                                    ; 27.3/27.4); Enter is not, and
.append:                            ; jumps in below wd_fastok
    call wd_selkill
    push ax                         ; the new ¶ inherits the paragraph it
    mov ax, [wd_cur]                ; SPLITS (Word's rule, SPEC.md 65.3): its
    call wd_papat                   ; CHP byte is a PAP INDEX, never the
    mov [wd_entpap], al             ; typing attributes
    pop ax
    mov dx, [wd_len]
    call wd_ins
    cmp dx, [wd_len]
    je .edited                      ; a full note dropped the keystroke
    push ax
    push bx
    push es
    mov bx, [wd_cur]
    dec bx
    mov es, [wd_cseg]
    mov al, [wd_entpap]
    mov [es:bx], al
    pop es
    pop bx
    pop ax
    jmp .edited
.doins:
    call wd_ins                     ; at the caret, which follows it
    jmp .edited

.bksp:
    push ax                         ; Alt+BkSp is EditUndo (keys.cmd) - the
    call wd_kbflags                 ; alt flag is the only thing that tells
    test al, 8                      ; the two apart
    pop ax
    jnz .kundo3
    call wd_selkill
    jnc .edited                     ; the selection WAS the deletion
    cmp word [wd_cur], 0
    je .out                         ; nothing to the left of the caret
    call wd_fastokb                 ; ...and so is a backspace, as long as the
    dec word [wd_cur]               ; character it eats is on the caret's own
    call wd_del                     ; row
    jmp .edited
.kundo3:
    jmp .kundo

.del:
    push ax                         ; Shift+Del is EditCut (keys.cmd)
    call wd_kbflags
    test al, 3
    pop ax
    jnz .kcut2
    mov byte [wd_ext], 0            ; an edit: extend mode ends
    call wd_selkill
    jnc .edited
    mov ax, [wd_cur]                ; forward delete: the caret stays put
    cmp ax, [wd_len]
    jae .out
    call wd_fastokd
    call wd_del
    jmp short .edited
.kcut2:
    jmp .kcut

.left:
    call wd_movpre
    cmp word [wd_cur], 0
    je .out
    call wd_fastokm                 ; a caret move is not an edit, but the row
    dec word [wd_cur]               ; above it still cannot have changed
    call wd_chpsync                 ; the typing attrs follow the caret
    jmp short .moved                ; (SPEC.md 65.3), on every plain move
.right:
    call wd_movpre
    mov ax, [wd_cur]
    cmp ax, [wd_len]
    jae .out
    call wd_fastokr
    inc word [wd_cur]
    call wd_chpsync
    jmp short .moved
.up:
    call wd_movpre
    mov dx, -1                      ; in ROWS now (SPEC.md 65.6)
    call wd_vmove
    call wd_chpsync
    jmp short .moved
.down:
    call wd_movpre
    mov dx, 1
    call wd_vmove
    call wd_chpsync
    jmp short .moved
.home:
    call wd_movpre
    xor dx, dx
    call wd_hmove
    call wd_chpsync
    jmp short .moved
.end:
    call wd_movpre
    mov dx, 0x7FFF
    call wd_hmove
    call wd_chpsync

.moved:
    call wd_extpost                 ; extend mode: the selection follows the
                                    ; caret from the banked anchor (65.2)
.edited:                        ; the toast is the kernel's and expires on
                                ; its own (SPEC.md 59), so a keystroke owes it
                                ; nothing at all - this used to be a store on
                                ; the hot path AND the reason wd_sigsame threw
                                ; the fast path away on the key after a save
    call OSAPI_GET_TICKS        ; ...and restarts the settle clock, which is
    mov [wd_ktick], ax          ; what the worker measures (SPEC.md 27.3)

.redraw:
    mov byte [wd_follow], 1         ; a KEY got us here, so wherever the caret
                                    ; ended up the view owes it a place on
                                    ; screen (SPEC.md 27.7). Set at the one
                                    ; label every handled key reaches, rather
                                    ; than in each of the twelve above it
    call wd_redraw                  ; SI still = window ptr

.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    ; --- the shortcuts (SPEC.md 27.8/65.7) ---------------------------------
    ; Reached only by the ladder at the top of this proc, which is why they
    ; sit past its `ret`: every one of them ends by jumping back into it.
.kfind:
    call wd_a_search                ; Ctrl-F: the modal Search dialog
    jmp .out                        ; (SPEC.md 65.7); no repaint owed - the
                                    ; dialog is on top of us
.kesc:
    mov byte [wd_ext], 0            ; Esc disarms extend mode (SPEC.md 65.2)
    call wd_selclr
    jc .redraw                      ; nothing selected: the tail still
                                    ; relights the EXT lamp
    mov byte [wd_ckok], 0
    call wd_chpsync                 ; the collapse leaves a bare caret: its
    jmp .redraw                     ; attrs are its neighbour's (SPEC.md 65.3)
.ktab:
    mov byte [wd_ext], 0
    mov al, 9                       ; a TAB is a CHARACTER (SPEC.md 65.3) -
    call wd_selkill                 ; it advances to the next default stop;
    call wd_ins                     ; wd_ins raises [wd_hastab] so the fast
    jmp .edited                     ; paths stand down before the redraw runs
.kcopy:
    call wd_uclose
    call wd_copy
    jmp .redraw
.kcut:
    call wd_uclose
    call wd_cut
    jmp short .kstamp
.kpaste:
    call wd_paste
    jmp short .kstamp
.kundo:
    call wd_undo
    jc .knoun
    call wd_chpsync                 ; the caret landed somewhere new
    jmp .kstamp
.knoun:
    mov ax, wd_m_noundo
    call wd_saymsg
    jmp .redraw
.kselall:
    call wd_uclose
    xor ax, ax
    mov dx, [wd_len]
    call wd_selset
    mov [wd_cur], dx
    mov byte [wd_ckok], 0
    jmp .redraw
.ksave:
    call wd_uclose
    call wd_save
    jmp .redraw
.kopen:
    call wd_a_open                  ; Ctrl+F12 = Open, through the dirty
    jmp .out                        ; prompt like the menu item (SPEC.md 65.4)
.kf1:
    call wd_habout                  ; F1: the About box (bounds+settle inside)
    jmp .out
.kf4:
    call wd_donext                  ; F4: repeat search down (SPEC.md 65.7)
    jmp .redraw
.ksf4:
    call wd_doprev                  ; Shift-F4: ...and up
    jmp .redraw
.kf5:
    call wd_a_goto                  ; F5: the Go To dialog (SPEC.md 65.7)
    jmp .out
.kf8:
    xor byte [wd_ext], 1            ; F8 toggles extend mode (SPEC.md 65.2);
    cmp byte [wd_ext], 0            ; arming banks the anchor so the first
    je .kf8off                      ; move has one to extend from
    cmp byte [wd_selon], 0
    jne .kf8off
    push ax
    mov ax, [wd_cur]
    mov [wd_anchor], ax
    pop ax
.kf8off:
    jmp .redraw                     ; nothing dirty: the tail relights EXT
.kstamp:
    mov byte [wd_ext], 0            ; cut/paste/undo are edits: extend ends
    call OSAPI_GET_TICKS            ; an EDIT, so the settle clock restarts -
    mov [wd_ktick], ax              ; but the toast it may have set stands,
    jmp .redraw                     ; which is why this is not .edited

    ; --- the formatting toggles (SPEC.md 65.3): five doors onto one routine,
    ; exactly as the ribbon cells and the dialog are
.kbold:
    mov al, WDAT_BOLD
    jmp short .kattr
.kdul:
    mov al, WDAT_DUL
    jmp short .kattr
.kscap:
    mov al, WDAT_SCAP
    jmp short .kattr
.kul:
    mov al, WDAT_UL
    jmp short .kattr
.kwul:
    mov al, WDAT_WUL
.kattr:
    call wd_applyattr               ; toggles the selection or the typing
    jmp .redraw                     ; attrs; the redraw's pass 1 finds the
                                    ; dirtied rows and its tail re-inverts
                                    ; the ribbon cell
.krst:
    xor al, al                      ; Ctrl-Space: ResetChar - everything back
    call wd_applyattr               ; to plain (SPEC.md 65.3)
    jmp .redraw

    ; --- the paragraph formats (SPEC.md 65.3): keys.cmd's letters, twin
    ; doors with the ruler cells and the Format Paragraph dialog onto
    ; wd_modpap - the one span modifier
.kleft:
    mov al, WDPO_ALIGN
    xor ah, ah
    jmp short .kpap
.kcenter:
    mov al, WDPO_ALIGN
    mov ah, 1
    jmp short .kpap
.kright:
    mov al, WDPO_ALIGN
    mov ah, 2
    jmp short .kpap
.kjust:
    mov al, WDPO_ALIGN
    mov ah, 3
    jmp short .kpap
.kspc2:
    mov al, WDPO_SPACE
    mov ah, 2
    jmp short .kpap
.kopensp:
    mov al, WDPO_SB
    mov ah, 1
    jmp short .kpap
.kclosesp:
    mov al, WDPO_SB
    xor ah, ah
    jmp short .kpap
.kindent:
    mov al, WDPO_ADDLEFT
    mov ah, 5                       ; +half an inch. UnIndent is Ctrl-M, which
    jmp short .kpap                 ; int 16h folds into Enter: it rides the
                                    ; dialog and the ruler markers instead
.khang:
    mov al, WDPO_HANG
    xor ah, ah
    jmp short .kpap
.kunhang:
    mov al, WDPO_UNHANG
    xor ah, ah
    jmp short .kpap
.krstpara:
    mov al, WDPO_RESET
    xor ah, ah
.kpap:
    call wd_uclose
    call wd_modpap
    jmp .redraw

    ; --- the Ins/Del chords and the pages -----------------------------------
.eins:                              ; extended Ins (AL = 0)
    push ax
    call wd_kbflags
    test al, 4
    jnz .einsc
    test al, 3
    jnz .einss
    pop ax
    xor byte [wd_ovr], 1            ; bare Ins: OVERTYPE toggles (SPEC.md
    jmp .redraw                     ; 65.2); nothing dirty, the tail's
                                    ; wd_stat relights the OVR lamp
.einsc:
    pop ax
    jmp .kcopy
.einss:
    pop ax
    jmp .kpaste
.insk:                              ; Ins scan WITH an ascii: NumLock's '0'
    push ax
    call wd_kbflags
    test al, 4
    jnz .einsc
    test al, 3
    jnz .einss
    pop ax
    jmp .typ2                       ; a plain numpad digit: type it
.delk:                              ; Del scan with an ascii: NumLock's '.'
    push ax
    call wd_kbflags
    test al, 3
    jnz .delkc
    pop ax
    jmp .typ2
.delkc:
    pop ax
    jmp .kcut
.pgup:
    call wd_caretpre
    mov ax, [wd_top]
    sub ax, [wd_vfit]
    call wd_scrollto
    call wd_redraw                  ; follow stays 0: a page turn is a view
    jmp .out                        ; move, not a caret move
.pgdn:
    call wd_caretpre
    mov ax, [wd_top]
    add ax, [wd_vfit]
    call wd_scrollto
    call wd_redraw
    jmp .out
.kf12:
    call wd_uclose                  ; F12 = Save As (keys.cmd)
    mov al, FDLG_SAVE
    call wd_dlgopen
    jmp .out
.kf12o:
    jmp .kopen                      ; Ctrl+F12 = Open

; -----------------------------------------------------------------------------
; wd_selkill - an edit is about to happen: if a selection is up, it goes
; out: CF = 1 there was none; preserves all registers
; -----------------------------------------------------------------------------
wd_selkill:
    call wd_seldel
    jc .out
    call wd_editinv
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; wd_caretpre - a caret key is about to run: end the edit group and drop the
;               selection
; out: nothing; preserves all registers
;
; Clearing [wd_ckok] when a selection actually went is the load-bearing half.
; The checkpoint lets wd_redraw resume its walk at the caret's own row, and a
; selection reaches rows ABOVE that - a resumed walk never re-signs them, so
; the inversion would stay drawn on a row nothing intends to redraw again.
; -----------------------------------------------------------------------------
wd_caretpre:
    call wd_uclose
    call wd_selclr
    jc .out
    mov byte [wd_ckok], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wd_movpre - what a caret-motion key does before moving (SPEC.md 65.2)
; out: nothing; preserves all registers
; Plain motion collapses the selection (wd_caretpre); with F8's extend mode
; armed it KEEPS it, banking the anchor if none exists yet, so the move that
; follows has an end that does not move.
; -----------------------------------------------------------------------------
wd_movpre:
    cmp byte [wd_ext], 0
    je wd_caretpre
    call wd_uclose
    cmp byte [wd_selon], 0
    jne .out
    push ax
    mov ax, [wd_cur]
    mov [wd_anchor], ax
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_extpost - after the caret moved: extend the selection to it (SPEC.md 65.2)
; out: nothing; preserves all registers
; The anchored end is [wd_anchor]; the selection is the ordered pair, and a
; caret back ON the anchor is an empty selection, cleared. The signatures
; fold the selection bits, so the redraw finds every row the span crossed.
; -----------------------------------------------------------------------------
wd_extpost:
    cmp byte [wd_ext], 0
    je .out
    push ax
    push dx
    mov ax, [wd_anchor]
    mov dx, [wd_cur]
    cmp ax, dx
    je .clr
    jb .set
    xchg ax, dx
.set:
    call wd_selset
    mov byte [wd_ckok], 0
    jmp short .done
.clr:
    call wd_selclr
    jc .done
    mov byte [wd_ckok], 0
.done:
    pop dx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_redraw - repaint our own content from the buffer, redrawing only the rows
;             that actually moved (SPEC.md 27.2)
; in:  SI = window ptr (gfx lock held by the caller)
; out: nothing; preserves all registers
;
; The self-repaint every dispatch site shares. It exists as a routine rather
; than a tail of wd_onkey because the menu handler needs exactly the same
; steps - the kernel does not repaint after a command returns (SPEC.md 12.2),
; so every command that changes the buffer has to end here.
;
; Two walks: one to measure and compare, one to draw the band the first found.
; Typing one character usually dirties exactly one row, and the erase is then
; a fill 8 pixels tall instead of the whole content. When nothing on screen
; changed - an arrow key that hit the end of the note, a keystroke a full
; buffer dropped - it draws nothing at all and returns.
;
; wd_sigsame decides whether that is legal: a resize or a toast coming and
; going means the stored signatures no longer describe what is on screen, and
; then this is the old routine unchanged - fill the content whole, wd_paint
; over it, put the grow box back because the fill just erased it.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wd_append - a printable typed at the end of the note, drawn as ONE GLYPH
; in:  SI = window ptr; gfx lock held; wd_bounds and wd_sigsame have run and
;      agreed; [wd_ekind] is the kind wd_redraw consumed
; out: CF = 0 it drew and wd_redraw owes nothing else; CF = 1 not this case.
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
; it should not work. wd_fold is a rolling `rol 1, add` over the row's cells in
; order, and wd_ask folds the CARET in at its own position - so with the caret
; at the end of the row the last two things folded are the caret and nothing
; else. That is invertible in four operations:
;
;     rol(B,1) = h - caret(C)                 undo the caret at column C
;     h' = rol(rol(B,1) + ch, 1) + caret(C+1) fold the character where it was,
;                                             then the caret after it
;
; so wd_sig stays exact and the NEXT keystroke's wd_sigsame still agrees. A
; fast path that left the signature stale would win once and pay for it on the
; keystroke after, which is how this was nearly built wrong.
;
; THE WRAP IS DEFERRED, DELIBERATELY. wd_wordfit is asked at a word's first
; character and nowhere else, so appending to a word already committed to this
; row cannot move it - but a fresh layout WOULD ask again with the longer word
; and might break earlier. So the screen can be a wrap behind the note while
; the keys are still coming, exactly as 27.3's visual break is a line break
; ahead of it, and [wd_sowed] is the debt: the worker already spends it with a
; full wd_redraw, and only after WD_IDLE ticks without a keystroke. Nothing new
; had to be hired - wd_ins raises wd_hmark, which is what wakes the worker at
; all, and 27.7.3's height recount and this reconcile are the same settle.
; -----------------------------------------------------------------------------
wd_append:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [wd_ekind], 1          ; a printable insert at the caret and
    jne .no                         ; nothing else: a Delete moves the tail, a
    cmp byte [wd_bmode], 0          ; caret move draws two rows, and the break
    jne .no                         ; owns the screen while it is up
    cmp byte [wd_ckok], 0
    je .no                          ; no checkpoint: we do not know the row
    cmp byte [wd_selon], 0
    jne .no                         ; a selection folds into the row's cells
    cmp byte [wd_chp], 0
    jne .no                         ; only a PLAIN character (SPEC.md 65.1):
                                    ; a styled one needs the run drawers, and
                                    ; the signature patch below assumes the
                                    ; attribute fold added nothing
    cmp byte [wd_hashid], 0
    jne .no                         ; hidden characters break the column
                                    ; arithmetic below (a hidden char is an
                                    ; index with no cell) - walk properly
    cmp byte [wd_hastab], 0
    jne .no                         ; a tab is an index with SEVERAL cells:
                                    ; same refusal (SPEC.md 65.3)
    cmp byte [wd_hasfmt], 0
    jne .no                         ; formatted rows have indents, offsets and
                                    ; heights this path's x/y arithmetic does
                                    ; not model - walk properly (SPEC.md 65.6)

    ; NOTHING AFTER THE CARET ON THIS ROW, which is the whole precondition and
    ; has exactly two shapes (SPEC.md 27.14.1). The end of the NOTE, where
    ; there is no tail at all; or a HARD NEWLINE at the caret, where the tail
    ; exists but is on other rows and none of its characters or its layout
    ; moves. A wrapped row's end is NOT one of them and cannot be: wd_ask fires
    ; at the settled pen, so the index after the last character of a wrapped
    ; row is reported at column 0 of the row BELOW - that caret is on the next
    ; row, and a character typed there joins that row's first word, which is
    ; the one thing 27.11.1 says can move a break.
    mov byte [wd_aprow], 0
    mov ax, [wd_cur]
    cmp ax, [wd_len]
    je .tailok
    mov es, [wd_dseg]
    mov di, ax
    cmp byte [es:di], 13
    jne .no
    mov byte [wd_aprow], 1          ; ...and rows BELOW start one index later
.tailok:
    or ax, ax
    jz .no
    dec ax                          ; AX = where the character landed
    mov bx, ax
    sub bx, [wd_ckpi]
    js .no                          ; before the row start: not our row at all
    mov cx, [wd_rcols]
    dec cx
    cmp bx, cx
    jae .no                         ; the cell rule is about to wrap this row,
                                    ; or the caret would land off the end of
                                    ; it - either way, walk properly. BX = the
                                    ; column the character occupies
    mov dx, [wd_ckpr]
    or dx, dx
    js .no
    cmp dx, [wd_vrows]
    jae .no                         ; off the view: nothing to draw
    cmp dx, [wd_prowi]
    jne .no                         ; THE EXISTING GATE: wd_prow describes some
                                    ; other row, so patching it would make the
                                    ; delta cache describe a row that is not on
                                    ; screen

    mov es, [wd_dseg]
    mov di, ax
    mov al, [es:di]                 ; the character itself
    cmp al, ' '
    jb .no                          ; a newline ends a row and is not a glyph
    mov [wd_ap1], al
    mov byte [wd_ap1+1], 0
    mov [wd_apch], al

    mov ax, bx                      ; --- x of the cell, y of the row ---------
    mov cl, 3
    shl ax, cl
    add ax, [wd_tx]
    mov [wd_apx], ax                ; x of column C
    mov ax, dx
    mov cl, 3
    shl ax, cl
    add ax, [wd_ty]
    mov [wd_apy], ax

    mov cx, [wd_apx]                ; --- the glyph, opaque, one cell ---------
    mov dx, [wd_apy]                ; opaque is what erases the caret bar that
    mov si, wd_ap1                  ; is standing on this very cell
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov ax, [wd_apx]                ; --- and the caret, one cell along -------
    add ax, 8
    mov [wd_apx2], ax
    mov bx, [wd_apy]
    mov dx, bx
    add dx, 7
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE

    ; --- and now the state the next keystroke reads --------------------------
    mov bx, [wd_cur]                ; the row cache: cell C was a space, and
    dec bx                          ; the screen shows the character now
    sub bx, [wd_ckpi]
    mov al, [wd_apch]
    mov [wd_prow+bx], al
    mov byte [wd_pattr+bx], 0       ; the cell is plain by the gate above, and
                                    ; the attr cache must say what the diff
                                    ; will re-ask (SPEC.md 65.1)
    inc bx
    mov [wd_prcc], bx               ; ...and its caret is one along

    mov bx, [wd_ckpr]               ; the signature, patched rather than walked
    shl bx, 1                       ; (see the header)
    mov ax, [wd_apx]
    xor ax, 0x5A5A
    mov dx, [bx+wd_sig]
    sub dx, ax                      ; undo the caret that was folded at C
    xor ah, ah
    mov al, [wd_apch]
    add dx, ax                      ; the character folds where it stood
    rol dx, 1                       ; ...then its CHP byte - zero by the gate
    rol dx, 1                       ; above, so its fold is the rotate alone
    mov ax, [wd_apx2]               ; (SPEC.md 65.1)
    xor ax, 0x5A5A
    add dx, ax                      ; ...and the caret after it
    mov [bx+wd_sig], dx

    mov ax, [wd_apx2]               ; wd_seecaret and every hit test read these
    mov [wd_curx], ax
    mov ax, [wd_apy]
    mov [wd_cury], ax
    mov byte [wd_curseen], 1

    ; EVERY ROW BELOW STARTS ONE INDEX LATER (SPEC.md 27.14.1). Their
    ; characters and their layout are untouched - the pixels below the caret's
    ; row are still right, which is what makes this case legal at all - but
    ; wd_rows is a table of INDICES and a character was inserted in front of
    ; all of them. Sixteen words at worst, against the walk this exists to
    ; skip. The end-of-note case has no rows below and does none of it.
    cmp byte [wd_aprow], 0
    je .norows
    cmp byte [wd_rowsok], 0
    je .norows
    mov cx, [wd_rowsn]
    cmp cx, WD_MAXROWS              ; [wd_rowsn] is not capped to the array it
    jbe .rok                        ; indexes (docs/NOTEPAD-NOTES.md 5.3.1), so
    mov cx, WD_MAXROWS              ; this caller clamps like wd_seedtail does
.rok:
    mov bx, [wd_ckpr]
    inc bx
.rsh:
    cmp bx, cx
    jae .norows
    push bx
    shl bx, 1
    inc word [bx+wd_rows]
    pop bx
    inc bx
    jmp short .rsh
.norows:

    mov byte [wd_sowed], 1          ; THE RECONCILE: the worker spends this with
                                    ; a full wd_redraw, and only once the keys
                                    ; have stopped for WD_IDLE ticks
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

wd_redraw:
    push ax
    push bx
    push cx
    push dx
    call wd_bounds
    mov al, [wd_fast]               ; ONE-SHOT: whoever set it meant this
    mov byte [wd_fast], 0           ; redraw and no other
    mov [wd_ekind], al
    mov byte [wd_resume], 0
    cmp byte [wd_bmode], 0
    je .normal
    or al, al
    jz .settle                      ; the break survives an insert and a
    cmp al, 3                       ; backspace and NOTHING else (SPEC.md
    jae .settle                     ; 27.3): the tail is not redrawn while it
                                    ; is up, so Right would draw a character
                                    ; twice, Left would lose one, and Delete
                                    ; eats exactly the tail's first character
    call wd_sigsame                 ; ...and a resize or a toast is not typing
    jc .settle                      ; either
    call wd_brkdraw
    jmp .out
.settle:
    call wd_reconcile
    jmp .out

.normal:
    push ax
    call wd_sigsame
    pop ax
    jc .full
    push ax                         ; the screen shows [wd_ptop] and the view
    mov ax, [wd_top]                ; may already have moved - a scroll bar
    cmp ax, [wd_ptop]               ; click scrolls and THEN redraws. Reconcile
    pop ax                          ; the pixels first: everything below this
    jne .scrolled0                  ; indexes an array by a VISIBLE row, and
                                    ; those still count in the old view

    call wd_append                  ; ONE GLYPH AND NO WALK (SPEC.md 27.14),
    jnc .done                       ; for a printable typed at the end of the
                                    ; note - which is most of typing. Here
                                    ; rather than earlier because it needs
                                    ; wd_sigsame to have agreed and [wd_ptop]
                                    ; to be [wd_top]: it patches the row cache
                                    ; and the signature, and both describe a
                                    ; screen drawn for THIS geometry and THIS
                                    ; view. AL still holds [wd_ekind] below -
                                    ; wd_append preserves every register
    or al, al
    jz .noseed
    call wd_seedck                  ; only an edit at the caret may skip the
.noseed:                            ; rows above it (SPEC.md 27.4)
    cmp byte [wd_resume], 0         ; ...and failing that, the top of the VIEW
    jne .seeded1                    ; is a seed too: wd_rows[0] is the index
    cmp byte [wd_rowsok], 0         ; row 0 of the content starts at, rows
    je .xseed1                      ; above it have neither pixels nor
    xor ax, ax                      ; signatures, and nothing above the caret
    mov dx, [wd_vrows]              ; can have reflowed anyway - which is the
    call wd_seedrow                 ; same claim 27.4 already makes, applied
    cmp byte [wd_resume], 0
    jne .seeded1
.xseed1:
    mov ax, [wd_top]                ; ...and when wd_rows cannot - which is
    mov dx, [wd_vrows]              ; after EVERY scroll, because wd_scrollto
    call wd_xseed                   ; drops it - the row index still describes
                                    ; the view's top row (SPEC.md 27.13).
                                    ; Without this, pass 1 of the redraw after
                                    ; a scroll laid the note out from index 0
                                    ; to reach the row the view starts on: 240
                                    ; ms of a 640 ms Up at [wd_top] = 5, and
                                    ; growing with the depth of the view
.seeded1:                           ; from a HIGHER row and so a weaker one.
                                    ; Both die together: wd_scrollto,
                                    ; wd_bounds and wd_clamp clear [wd_ckok]
                                    ; and [wd_rowsok] side by side

    mov word [wd_hity], 0xFFFF      ; pass 1: no queries, no drawing - just
    mov word [wd_wanty], 0x7FFF     ; which rows stopped matching
    mov word [wd_dr0], 0xFFFF
    mov word [wd_dr1], 0
    mov byte [wd_ymoved], 0         ; ...and which rows MOVED (SPEC.md 65.6):
    mov word [wd_ymv0], 0x7FFF      ; a height change shifts every row below
    mov word [wd_ymv1], 0           ; it without changing a character
    mov byte [wd_draw], 0
    mov byte [wd_sigup], 1
    mov byte [wd_clip], 0
    mov ax, [wd_vrows]              ; STOP at the bottom of the view, plus the
                                    ; one row past it a caret can wrap onto
                                    ; (SPEC.md 27.7). Below that a row has no
                                    ; signature, cannot be dirty and is drawn
                                    ; by nobody - the only thing that ever
                                    ; wanted it was the note's total height,
                                    ; and wd_height owns that now. Typing in
                                    ; the middle of a long note used to walk
                                    ; every row beneath the window on every
                                    ; keystroke: 72% of the work, for a thumb

    ; ...AND A CARET MOVE STOPS SOONER STILL (SPEC.md 27.4.1). Nothing reflowed,
    ; so the only rows whose signatures can differ are the one the caret left
    ; and the one it arrived on, and wd_move recorded the deeper of them. The
    ; rest of the view is laid out to be told it did not move: (vrows - row) x
    ; ~6 ms, which is ~96 ms of the budget with the caret near the top.
    ;
    ; Gated on the walk actually RESUMING, and that is not caution about the
    ; bound - it is about [wd_rowsn]. wd_walk's bounded stop leaves the table
    ; alone for a resumed walk and SHRINKS it to where it stopped for one that
    ; started at the top of the view, which would hand rows this walk skipped
    ; back to SPEC.md 27.13's index for no reason. Resumed is the normal case
    ; here anyway: wd_seedck seeds at the earlier of the two rows.
    cmp byte [wd_ekind], 4
    jne .p1bound
    cmp byte [wd_resume], 0
    je .p1bound
    mov dx, [wd_mvbot]
    or dx, dx
    js .p1bound                     ; above the view: let the caret-follow net
    cmp dx, ax                      ; have it, exactly as before
    jae .p1bound                    ; never DEEPER than the view: the sentinel
    mov ax, dx                      ; lands here, and so does a caret one row
.p1bound:                           ; below the last visible one
    mov [wd_lastrow], ax
    call wd_walk
    cmp byte [wd_follow], 0         ; the caret has to be somewhere the user
    je .noflw                       ; can see it (SPEC.md 27.7) - but only
    cmp byte [wd_curseen], 0        ; ...and the walk above may have stopped
    jne .haveit                     ; short of the caret, in which case
                                    ; [wd_cury] is still the initial 0 and
                                    ; following it would scroll somewhere
                                    ; arbitrary. Walk again FROM INDEX 0 and
                                    ; UNBOUNDED, which is the whole point of
                                    ; this net: the seed is what let the walk
                                    ; miss the caret and the bound is what
                                    ; made it missable, so a net carrying
                                    ; either finds nothing too. wd_measure
                                    ; clears neither - wd_vmove and wd_onclick
                                    ; set them on purpose - so this does.
                                    ; The case is real and not theoretical:
                                    ; page the view away with the bar and then
                                    ; press a key, and the caret is a whole
                                    ; screenful below the last row walked
    mov word [wd_lastrow], 0x7FFF   ; describes, when that row begins at or
    mov ax, [wd_cur]                ; before the caret. THE ROW INDEX ANSWERS
    call wd_xseedi                  ; THIS DIRECTLY (SPEC.md 27.13) and it is
    jnc .netok                      ; the case that mattered: a caret ABOVE the
                                    ; view qualifies no row wd_rows describes,
                                    ; so wd_netseed walked back to row 0, found
                                    ; nothing and left the walk to lay out the
                                    ; whole note - 5.2 s on every Up out of the
                                    ; top of the view. wd_xseedi seeds within a
                                    ; stride of the caret AND sets the bound,
                                    ; because a later checkpoint proves which
                                    ; row the caret's row is above
    call wd_netseed             ; ...FORWARD from the deepest row the table
.netok:
    call wd_measure             ; before the caret. Unbounded still - the
                                ; caret's row is not known, which is the whole
                                ; problem - but not from INDEX 0: Down on the
                                ; bottom visible row puts the caret one row
                                ; below the view and re-walked the entire note
                                ; to find it, which is seconds on the most
                                ; used key there is (docs/NOTEPAD-NOTES.md 1.4)
.haveit:
    call wd_seecaret                ; when it MOVED. A scroll bar click also
    jnc .scrolled                   ; ends here, and following the caret then
.noflw:                             ; would drag the view straight back to it
                                    ; and make the bar look broken. Moving
                                    ; the view renames every row the band and
                                    ; the signatures are counted in - so that
                                    ; is a full repaint, not a band
    mov ax, [wd_dr0]
    cmp ax, 0xFFFF
    je .done                        ; not one pixel of the text moved

    mov al, [wd_ekind]              ; would this reflow cost more than pushing
    or al, al                       ; the note down a row? (SPEC.md 27.3)
    jz .band                        ; Every EDIT at the caret qualifies -
    cmp al, 4                       ; insert, Backspace and Delete alike -
    jae .band                       ; because wd_fastok* REPORTED the caret's
                                    ; column rather than leaving this to work
                                    ; it out from where the caret ended up. A
                                    ; caret move does not: nothing reflowed,
                                    ; so there is nothing to avoid drawing
    cmp byte [wd_brkok], 0
    je .band
    call wd_brktry
    jnc .done                       ; it took over, and it drew
.band:
    mov ax, [wd_dr0]                ; reloaded: wd_brktry is free with AX

    ; the band's pixel edges: arithmetic while uniform; from the banked ys -
    ; which pass 1 has just rewritten - under formats (SPEC.md 65.6)
    cmp byte [wd_hasfmt], 0
    je .bxy8
    or ax, ax
    jz .bty
    mov bx, ax                      ; y1 = bandtop(dr0) = ryb[dr0-1] + 8
    dec bx
    shl bx, 1
    mov bx, [bx+wd_ryb]
    add bx, 8
    jmp short .by1
.bty:
    mov bx, [wd_ty]
.by1:
    mov dx, [wd_dr1]                ; y2 = dr1's glyph bottom, or the content
    cmp dx, [wd_vrows]              ; bottom when the range runs past the view
    jae .bybot
    shl dx, 1
    push bx
    mov bx, dx
    mov dx, [bx+wd_ryb]
    pop bx
    add dx, 7
    cmp dx, [wd_bot]
    jbe .bhave
.bybot:
    mov dx, [wd_bot]
    jmp short .bhave
.bxy8:
    mov bx, ax                      ; y1 = wd_ty + 8*dr0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [wd_ty]
    mov dx, [wd_dr1]                ; y2 = wd_ty + 8*dr1 + 7
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [wd_ty]
    add dx, 7
.bhave:
    ; rows MOVED: erase from the highest pixel a moved row owned or owns
    ; down to the content bottom, and redraw EVERY row from there - a glyph
    ; run only self-erases at its own y, and a row that shifted leaves its
    ; old pixels standing (SPEC.md 65.6). The fill makes the band clean, so
    ; the runs draw trimmed, exactly the full-repaint discipline; the
    ; to-the-bottom sweep is the honest cost of pixels that all moved.
    cmp byte [wd_ymoved], 0
    je .noymv
    mov ax, [wd_ymv0]
    cmp ax, [wd_ty]
    jae .ym0
    mov ax, [wd_ty]
.ym0:
    cmp ax, bx
    jae .ym1
    mov bx, ax
.ym1:
    mov dx, [wd_bot]
    mov ax, [wd_vrows]              ; ...and pass 2 walks to the view's last
    dec ax                          ; row: rows below the moved ones need
    cmp ax, [wd_dr1]                ; their (unchanged) pixels REDRAWN at
    jbe .ym3                        ; their (unchanged) ys inside the sweep
    mov [wd_dr1], ax
.ym3:
    push bx
    push dx
    mov ax, [wd_tx]
    sub ax, WD_MARGIN
    mov cx, [wd_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop dx
    pop bx
    mov byte [wd_clean], 1
    mov word [wd_prowi], 0xFFFF
.noymv:
    ; The band fill is GONE. It used to erase dr0..dr1 whole and pass 2 then
    ; lettered it, which is the erase-and-letter pair - and on a 4.77MHz 8088
    ; that leaves the line blank for several display frames, so every keystroke
    ; flickered (SPEC.md 6.1). wd_rflush draws each row as one opaque font_run
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
    mov ax, [wd_tx]                 ; left: the margin, if there is one
    sub ax, WD_MARGIN
    mov cx, [wd_tx]
    dec cx
    cmp ax, cx
    jg .mr
    call OSAPI_GFX_FILL
.mr:
    mov ax, [wd_rcols]              ; right: the <8px tail past the last cell
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]
    mov cx, [wd_rgt]
    cmp ax, cx
    jg .mdone
    call OSAPI_GFX_FILL
.mdone:
    pop dx
    pop bx

    mov byte [wd_draw], 1           ; pass 2: draw, and only inside it - and
    mov byte [wd_sigup], 0          ; STOP at it, because pass 1 already knows
    mov byte [wd_clip], 1           ; no row below wd_dr1 changed. An arrow key
    mov ax, [wd_dr1]                ; dirties two rows and used to lay out the
    mov [wd_lastrow], ax            ; whole note behind them to draw them
    call wd_walk
    mov byte [wd_clip], 0
    mov byte [wd_clean], 0          ; the moved-rows erase may have set it

    ; THE GROW BOX IS NOT REDRAWN HERE AT ALL, because nothing on this path
    ; can reach it (SPEC.md 27.2.1). It was unconditional once, and it HAD to
    ; be: the band fill spanned the full content width, so any dirty row level
    ; with the box erased it. Then it became a row test - [wd_bandb] against
    ; wd_bot-12 - and that was still wrong in the expensive direction, because
    ; typing on the BOTTOM VISIBLE ROW satisfies it on every keystroke, which
    ; is exactly where the caret sits while a page is being filled. Measured:
    ; wm_grow_paint plus three gfx_frames and eighteen fills and lines, ~12 ms
    ; of a 52 ms keystroke - and the flicker in the corner that making it
    ; conditional was supposed to stop.
    ;
    ; The box is 13x13 at (wd_sbr-12, wd_bot-12) and wd_bounds reserves that
    ; corner in BOTH dimensions: [wd_rgt] is wd_sbr-WD_SB_W, two pixels short
    ; of its left edge, and [wd_sbb] is wd_bot-WD_GROW, one row above its top.
    ; So the text runs, the two margin fills and the scroll bar all stop clear
    ; of it. The row test was reading the band's rows and never asked about its
    ; COLUMNS. Every other OSAPI_WM_GROW in this module follows something that
    ; genuinely reaches the corner - a full-content fill, or a band scroll that
    ; drags it - and those all stay.
    call wd_sbcheck                 ; a note that gained or lost a row moves
                                    ; the thumb, and nothing else redraws it
                                    ; on this path
.done:
    mov byte [wd_resume], 0
    jmp short .out

.scrolled0:
    mov word [wd_dr0], 0xFFFF       ; no walk has run this redraw, so nothing
    mov word [wd_dr1], 0            ; is known dirty beyond the exposed rows
.scrolled:
    ; The view moved. Move the PIXELS to match instead of drawing them again
    ; (SPEC.md 27.7.2) - and if that is refused, the full repaint below is
    ; exactly what used to happen every time.
    call wd_scrollpaint
    jnc .done
    jmp short .fullpaint

.full:
    ; Reached when wd_sigsame REFUSED - a resize, a toast arriving or leaving,
    ; an uncover - so nothing above has measured anything, and both numbers
    ; the view is clamped by may have changed: a wider window wraps into fewer
    ; rows. Measure, put the view back inside a note that may now be shorter
    ; than where it was looking, and only then follow the caret.
    call wd_measure
    mov ax, [wd_top]
    call wd_scrollto
    cmp byte [wd_follow], 0
    je .fullpaint
    call wd_measure                 ; measured AGAIN because wd_scrollto
    call wd_seecaret                ; renames every row [wd_cury] was counted
                                    ; in. Same gate as the band path: a bar
                                    ; click must not have its scroll undone
.fullpaint:
    ; ...and reached DIRECTLY from the scroll above, which is the common case
    ; and was paying for this block having no way to know that. A caret that
    ; has just been followed is in view by construction - wd_seecaret's target
    ; is the exact row, not a step towards it - so re-measuring in order to
    ; ask the same question again cost two full walks per keystroke and could
    ; never answer differently. Every Up that scrolled, and every character
    ; typed with the view already trailing the caret, paid it.
    mov word [wd_prowi], 0xFFFF     ; the delta cache describes the SCREEN, and
                                    ; the screen is about to be filled over.
                                    ; Every path that disturbs it other than
                                    ; our own row draws lands here - a resize,
                                    ; a toast arriving or leaving, an uncover -
                                    ; because that is what wd_sigsame is for
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
    call wd_paint                   ; SI still = window ptr
    mov bx, si                      ; the white fill erased the grow box;
    call OSAPI_WM_GROW              ; restore it (SPEC.md 11.1/27)
.out:
    mov byte [wd_ymoved], 0         ; spent: it described THIS redraw's pass 1
                                    ; (wd_scrollpaint tests it, and a stale 1
                                    ; would refuse a later good blit)
    call wd_hirechk                 ; a debt left by ANY of this routine's
                                    ; exits, not just the .done path it used to
                                    ; hang off - .fullpaint fell straight past
                                    ; that one (SPEC.md 27.7.3)
    call wd_selmark                 ; the screen shows this selection now
    mov byte [wd_selonly], 0        ; ONE-SHOT: whoever set it meant THIS
                                    ; redraw, and the next one may well be a
                                    ; keystroke that moves characters
    mov byte [wd_follow], 0         ; ONE-SHOT, like [wd_fast]: whoever set it
                                    ; meant this redraw and no other, and the
                                    ; next one may well be a scroll bar click
    call wd_stat                    ; the status line follows the caret
                                    ; (SPEC.md 65.2): delta-cached, so a
                                    ; keystroke that moved neither Ln nor Col
                                    ; draws not a cell of it
    call wd_rbstat                  ; ...and the ribbon's pressed cells follow
                                    ; the caret's attributes, same delta rule
                                    ; (SPEC.md 65.3)
    call wd_rlstat                  ; ...and the ruler's cells and markers
                                    ; follow the caret's PARAGRAPH, same rule
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_new - empty the note (File > New)
; in:  nothing
; out: nothing; preserves all registers
;
; The length, the toast and the claim. wd_paint reads exactly [wd_len] bytes,
; so the stale tail is unreachable and wiping it would buy nothing - but the
; claim it sat in is real memory, and a note that grew to WD_MAXKB has no
; business holding eight kilobytes of heap after the user emptied it. The
; toast goes because "Loaded DOCUMENT.DOC" over an empty note is a lie - the same
; reason an ordinary keystroke retires it.
; -----------------------------------------------------------------------------
wd_new:
    mov word [wd_len], 0
    mov word [wd_cur], 0
    mov ax, wd_s_nul            ; retire the toast: 'Loaded DOCUMENT.DOC' over an
    call wd_saymsg              ; empty note is a lie. An EMPTY string is how
    call wd_clamp               ; SPEC.md 59.3 spells that, so no flag of ours
               ; a no-op on the caret, which is already 0 -
                                ; it is here for the invalidation wd_clamp
                                ; carries (SPEC.md 27.4/27.5)
    push ax                     ; ...and give the heap back what the old note
    mov ax, WD_KB0              ; had grown into. A shrink always succeeds in
    call wd_resize              ; place, so this cannot fail (SPEC.md 50.3.1)
    pop ax
    mov byte [wd_dirty], 0      ; a fresh document matches nothing on disk
                                ; and owes no prompt (SPEC.md 65.4)
    call wd_defname             ; a new note is a new document: leaving the
                                ; old name would make the next Ctrl-S overwrite
                                ; the file the user just walked away from
    jmp wd_settitle             ; ...and the caption follows it (65.2)

; -----------------------------------------------------------------------------
; wd_dlgopen - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The current document is handed over as the default, so Save As on a note
; loaded from LETTER.TXT opens with LETTER.TXT already in the box. A refusal
; (CF=1: one is already up) is silently nothing - the dialog the user
; already has IS the answer to the command they just picked.
; -----------------------------------------------------------------------------
wd_dlgopen:
    push bx
    push si
    push di
    mov bx, si                      ; the window we want to hear back about
    mov di, wd_ondlg
    mov si, wd_name
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_ondlg - the file dialog's completion callback (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, DI = the chosen name;
;      UI task, gfx lock HELD, the dialog window already destroyed
; out: nothing; no register need be preserved (the kernel saved its own)
;
; One proc for both commands, because the only difference between them is
; which way the bytes move afterwards. It must repaint: the kernel does not
; repaint after a callback returns, and the window under the dialog has just
; been uncovered by wm_destroy.
; -----------------------------------------------------------------------------
wd_ondlg:
    mov [wd_fsz], cx                ; the file's size from the LISTING (38.6),
    mov [wd_fszh], dx               ; banked FIRST - the open gate reads it
                                    ; BEFORE any disk I/O (SPEC.md 65.4)
    mov bl, al                      ; BL = the mode; AL becomes a name byte
    mov dx, si                      ; DX = our window: SI is about to be the
                                    ; kernel's buffer, and wd_redraw wants
                                    ; the window back in SI
    mov si, di
    mov di, wd_name
    mov cx, WD_NAMEMAX
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
    jz .noext                       ; gets .DOC appended (SPEC.md 65.4)
    push bx
    push di
    mov di, wd_name
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
    push dx                         ; the window: FILE_HERE answers in DX
    push bx                         ; ...and the mode is in BL
    call OSAPI_FILE_HERE            ; where the dialog left the volume IS the
    mov [wd_dir], dx                ; folder the user chose, and it is the one
    mov [wd_drv], bl                ; this document belongs to from here on
    mov byte [wd_dirok], 1
    pop bx
    pop dx
    mov si, dx                      ; SI = our window again
    call wd_settitle                ; the caption follows the name (65.2)
    or bl, bl
    jz .load
    call wd_save
    jmp short .draw
.load:
    cmp word [wd_fszh], 0           ; the refusal gate, BEFORE any read
    jne .toobig                     ; (SPEC.md 65.4): a file the staging
    cmp word [wd_fsz], WD_STGCAP    ; claim cannot hold is refused from the
    ja .toobig                      ; directory entry's own size
    call wd_load
    jmp short .draw
.toobig:
    mov ax, wd_e_big
    call wd_saymsg
.draw:
    jmp wd_redraw                   ; tail call; SI is the window ptr

; -----------------------------------------------------------------------------
; wd_goto - put the volume back in this document's folder (SPEC.md 19.2)
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
wd_goto:
    push ax
    push bx
    push dx
    cmp byte [wd_dirok], 0
    je .out                     ; never saved anywhere in particular
    call OSAPI_FILE_HERE
    cmp dx, [wd_dir]
    jne .move
    cmp bl, [wd_drv]
    je .out
.move:
    mov dx, [wd_dir]
    mov bl, [wd_drv]
    call OSAPI_FILE_GOTO        ; CF = it could not be listed; the file call
.out:                           ; that follows will say so in its own words
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_arg - open the document we were launched to open (SPEC.md 54.5)
; in:  nothing; called from wd_entry once the window and the claim exist
; out: nothing; preserves all registers AND the flags, because the CF this
;      package owes the loader is still riding in them
;
; The kernel hands over a name and a (cluster, volume) pair rather than
; putting us in the right folder, because it cannot: the loader read our own
; image out of OUR directory and far-called this entry as one unit. So the
; whole of accepting a document is to copy the name, record the folder the
; way Save As already records one, and let wd_load do what Ctrl-O does.
;
; The name lives in the KERNEL segment, so ES is loaded explicitly rather
; than trusted: it happens to still be KERNEL_SEG here, and a later edit that
; left a package segment in ES would read this package's own image as a file
; name and fail in a way that looks like a kernel bug.
; -----------------------------------------------------------------------------
wd_arg:
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
    mov [wd_dir], dx                ; the folder it lives in, recorded the
    mov [wd_drv], bl                ; way Save As records one - wd_goto then
    mov byte [wd_dirok], 1          ; takes wd_load there
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, wd_name
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
    call wd_compttl                 ; the title follows the name - compose
                                    ; only, the window is not shown yet and
                                    ; the loader draws the caption (65.2)
    call wd_load                    ; ...and this is Ctrl-O, unchanged
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
; wd_defname - seed the current document name (internal)
; in:  nothing
; out: wd_name = 'DOCUMENT.DOC'; preserves all registers
; The loader zeroes our bss (SPEC.md 21 step 5), and an empty name would
; make Ctrl-S fail with FERR_NAME on a brand-new note - so a fresh Note Pad
; still has the document the fixed-name version always had.
; -----------------------------------------------------------------------------
wd_defname:
    push ax
    push si
    push di
    mov si, wd_s_default
    mov di, wd_name
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
; wd_compttl - compose the window title: 'Microsoft Word - ' + wd_name
; out: wd_wttl (bss - the window record points at it for its whole life);
;      preserves all registers. Compose-only: wd_entry runs it before the
;      window exists (SPEC.md 65.2).
; wd_settitle - ...and tell the kernel the bytes changed (caption redraw).
;      Callback context (lock held).
; -----------------------------------------------------------------------------
wd_compttl:
    push si
    push di
    mov si, wd_s_tpre
    mov di, wd_wttl
    call wd_stcat
    mov si, wd_name
    call wd_stcat
    push ax
    xor al, al
    mov [di], al
    pop ax
    pop di
    pop si
    ret

wd_settitle:
    push ax
    push bx
    call wd_compttl
    mov bx, [wd_win]
    or bx, bx
    jz .out
    xor ax, ax                      ; 0 = "the bytes W_TITLE names changed
    call OSAPI_WM_TITLE             ; underneath" (SPEC.md 11.92)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_setmsg - compose a toast around the live document name (internal)
; in:  SI = a NUL prefix ('Saved ' / 'Loaded ')
; out: wd_tbuf holds prefix + wd_name and it has been said; preserves all
;      registers
; -----------------------------------------------------------------------------
wd_setmsg:
    push ax
    push si
    push di
    mov di, wd_tbuf
.pre:
    mov al, [si]
    or al, al
    jz .name
    mov [di], al
    inc si
    inc di
    jmp short .pre
.name:
    mov si, wd_name
.copy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    jmp short .copy
.done:
    mov ax, wd_tbuf
    call wd_saymsg              ; the kernel copies it, so wd_tbuf is free to
    pop di                      ; be recomposed the moment this returns
    pop si
    pop ax
    ret

; =============================================================================
; THE SELECTION (SPEC.md 27.8)
;
; A range of character indices, [wd_sel0], [wd_sel1), and an inversion drawn
; over the cells that fall inside it. Two things make it nearly free.
;
; It is an XOR FILL, per row, applied by wd_rflush right after the run that
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
; - so wd_rflush keeps [wd_prs0]/[wd_prs1] the way it keeps [wd_prcc], and
; folds the union of the old span and the new one into the cells it redraws.
;
; XOR is its own inverse, and that is the sharp edge here. A cell the run did
; NOT redraw still carries the inversion the last pass gave it, so inverting
; it a second time would take it away - which is why wd_selxor intersects the
; row's selected span with [wd_flo]..[wd_fhi], the cells actually written.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_selq - is character index AX inside the selection?
; in:  AX = a character index; out: CF = 1 if it is; preserves all registers
; -----------------------------------------------------------------------------
wd_selq:
    cmp byte [wd_selon], 0
    je .no
    cmp ax, [wd_sel0]
    jb .no
    cmp ax, [wd_sel1]
    jae .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wd_selqo - was character index AX selected in the selection ON SCREEN?
; in:  AX; out: CF = 1 if it was; preserves all registers
; -----------------------------------------------------------------------------
wd_selqo:
    cmp byte [wd_oselon], 0
    je .no
    cmp ax, [wd_osel0]
    jb .no
    cmp ax, [wd_osel1]
    jae .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wd_xfold - widen the row's CHANGED-inversion span to include column AX
; in:  AX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_xfold:
    cmp word [wd_xs0], 0xFFFF
    jne .lo
    mov [wd_xs0], ax
    mov [wd_xs1], ax
    ret
.lo:
    cmp ax, [wd_xs0]
    jae .hi
    mov [wd_xs0], ax
.hi:
    cmp ax, [wd_xs1]
    jbe .out
    mov [wd_xs1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_selfold - widen the row's inverted span to include cell column AX
; in:  AX = a column; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_selfold:
    cmp word [wd_rs0], 0xFFFF
    jne .lo
    mov [wd_rs0], ax
    mov [wd_rs1], ax
    ret
.lo:
    cmp ax, [wd_rs0]
    jae .hi
    mov [wd_rs0], ax
.hi:
    cmp ax, [wd_rs1]
    jbe .out
    mov [wd_rs1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_selxor - invert the selected cells of the row just drawn
; in:  [wd_rs0]/[wd_rs1] = the row's selected span (0xFFFF = none),
;      [wd_flo]/[wd_fhi] = the cells the run actually wrote, [wd_rby],
;      [wd_tx]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_selxor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [wd_rs0]
    cmp ax, 0xFFFF
    je .out
    mov cx, [wd_rs1]
    cmp ax, [wd_flo]            ; the intersection, and nothing wider: a cell
    jae .l0                     ; the run did not touch is still carrying the
    mov ax, [wd_flo]            ; inversion the last pass gave it, and a
.l0:                            ; second XOR would take it back off
    cmp cx, [wd_fhi]
    jbe .l1
    mov cx, [wd_fhi]
.l1:
    cmp ax, cx
    ja .out
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]             ; AX = x1
    push ax
    mov ax, cx
    inc ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_tx]
    dec ax
    mov cx, ax                  ; CX = x2, the last column of the last cell
    pop ax
    mov bx, [wd_rby]
    mov dx, bx
    add dx, [wd_gh1]
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_selclr - there is no selection any more
; out: CF = 1 there was none to clear (so nothing needs redrawing);
;      preserves all registers
; -----------------------------------------------------------------------------
wd_selclr:
    cmp byte [wd_selon], 0
    je .none
    mov byte [wd_selon], 0
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; wd_selset - select [AX, DX), in either order
; in:  AX, DX = two character indices; out: nothing; preserves all registers
; An empty range is no selection at all, which is what makes "click" and
; "drag back to where you started" the same thing.
; -----------------------------------------------------------------------------
wd_selset:
    push ax
    push dx
    cmp ax, dx
    jbe .ord
    xchg ax, dx
.ord:
    cmp ax, dx
    je .none
    mov [wd_sel0], ax
    mov [wd_sel1], dx
    mov byte [wd_selon], 1
    jmp short .out
.none:
    mov byte [wd_selon], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_selget - the live selection
; out: CF = 1 there is none; else CF = 0 with AX = its start and CX = its
;      length; preserves all other registers
; -----------------------------------------------------------------------------
wd_selget:
    cmp byte [wd_selon], 0
    je .no
    mov ax, [wd_sel0]
    cmp ax, [wd_len]
    jae .no                     ; the note is SHORTER than the selection now
    mov cx, [wd_sel1]
    cmp cx, [wd_len]            ; ...and the far end is clamped rather than
    jbe .end                    ; refused, so a shrink leaves the part of the
    mov cx, [wd_len]            ; selection that still exists selected
.end:
    cmp cx, ax
    jbe .no                     ; BELOW, not just equal: an inverted pair here
                                ; would make `sub` answer ~65,000 and hand
                                ; that to whoever asked - clip_put refuses it,
                                ; but wd_rev would swap bytes clean off the
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
; wd_seldel - delete the selection; the caret lands where it began
; out: CF = 1 there was none; preserves all registers
; -----------------------------------------------------------------------------
wd_seldel:
    push ax
    push bx
    push cx
    call wd_selget
    jc .no
    mov bx, ax
    call wd_delspan
    mov [wd_cur], bx
    call wd_selclr
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
; wd_ins and wd_del were the whole edit surface, and both were "one character
; at the caret". Cut, Paste, Replace and Undo all move runs, so the run is the
; primitive now and the two old routines are cases of it.
;
; Both record undo BEFORE they move anything (SPEC.md 27.9) - a deletion has
; to reach the undo blob while its bytes are still in the note - and both
; leave [wd_cur] alone. Where the caret goes afterwards is the caller's
; decision and differs for every one of them.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_capfor - make the document claim hold AX bytes
; in:  AX = the bytes wanted
; out: CF = 0 and [wd_cap] >= AX; CF = 1 refused. Preserves all registers.
; -----------------------------------------------------------------------------
wd_capfor:
    push ax
    push bx
    push cx
    mov bx, ax
    cmp bx, [wd_cap]
    jbe .yes
    mov ax, bx
    add ax, 1023
    jc .no                      ; past 64KB, which WD_MAXKB refuses anyway
    mov cl, 10
    shr ax, cl
    call wd_resize              ; clamps to WD_MAXKB and may be refused
    jc .no
    cmp bx, [wd_cap]
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
; wd_delspan - remove CX bytes at index BX
; in:  BX = the first index to go, CX = how many (both clamped to the note)
; out: nothing; preserves all registers. [wd_len] shrinks; [wd_cur] is the
;      caller's business.
; -----------------------------------------------------------------------------
wd_delspan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [wd_len]
    cmp bx, ax
    jae .out
    sub ax, bx
    cmp cx, ax
    jbe .have
    mov cx, ax
.have:
    jcxz .out
    call wd_hmark
    mov byte [wd_dirty], 1      ; bytes are leaving: dirty (SPEC.md 65.4)
    mov ax, bx
    call wd_urec_del            ; while the bytes are still here to be copied
    mov es, [wd_dseg]
    mov si, bx
    add si, cx                  ; the first byte that survives
    mov di, bx
    mov dx, [wd_len]
    sub dx, si                  ; ...and how many of them there are
    push cx                     ; the SPAN, which .close still needs
    mov cx, dx
    jcxz .nomv
    push ds                     ; forwards here: the gap closes DOWNWARD, so
    mov ds, [wd_dseg]           ; DI trails SI (SPEC.md 27.12)
    cld
    rep movsb
    pop ds
.nomv:
    pop cx
    ; --- the same close on the CHP claim (SPEC.md 65.3); the undo blob
    ; above already banked both halves while the bytes were still here
    push cx
    mov es, [wd_cseg]
    mov si, bx
    add si, cx
    mov di, bx
    mov dx, [wd_len]
    sub dx, si
    mov cx, dx
    jcxz .cnomv
    push ds
    mov ds, [wd_cseg]
    cld
    rep movsb
    pop ds
.cnomv:
    pop cx
.close:
    mov ax, [wd_len]
    sub ax, cx
    mov [wd_len], ax
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
; wd_gaproom - open a CX-byte gap at index BX for the caller to fill
; in:  BX = where, CX = how many
; out: CF = 0 and [wd_len] already counts the gap; CF = 1 refused and nothing
;      moved. Preserves all registers.
;
; It does NOT record undo: the caller knows what it is about to put there and
; how much of it survives, and a paste's filter can shorten it afterwards.
; -----------------------------------------------------------------------------
wd_gaproom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    jcxz .ok
    mov ax, [wd_len]
    add ax, cx
    jc .no                      ; a 16-bit note cannot pass 65535
    call wd_capfor
    jc .no
    mov byte [wd_dirty], 1      ; a gap is opening: dirty (SPEC.md 65.4)
    mov es, [wd_dseg]
    mov si, [wd_len]
    dec si                      ; the last live byte
    mov di, si
    add di, cx
    mov dx, [wd_len]
    sub dx, bx                  ; the bytes to the right of the gap
    push cx                     ; the GAP width, which .done still needs
    mov cx, dx
    jcxz .nomv
    push ds                     ; backwards: wd_ins's case with a gap wider
    mov ds, [wd_dseg]           ; than one byte (SPEC.md 27.12)
    std
    rep movsb
    cld
    pop ds
.nomv:
    pop cx
    ; --- the same gap on the CHP claim, FILLED with the typing attrs
    ; (SPEC.md 65.3): a paste's characters arrive dressed as the caret is;
    ; the undo restore overwrites the fill with the blob's own bytes
    push cx
    push ax
    mov es, [wd_cseg]
    mov si, [wd_len]
    dec si
    mov di, si
    add di, cx
    mov dx, [wd_len]
    sub dx, bx
    push cx
    mov cx, dx
    jcxz .cnomv
    push ds
    mov ds, [wd_cseg]
    std
    rep movsb
    cld
    pop ds
.cnomv:
    pop cx
    mov di, bx
    mov al, [wd_chp]
    cld
    rep stosb
    pop ax
    pop cx
.done:
    mov ax, [wd_len]
    add ax, cx
    mov [wd_len], ax
    call wd_hmark
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
; wd_editinv - the layout state is all indices into a buffer that just moved
; out: nothing; preserves all registers
;
; The lighter half of wd_clamp: it does NOT put the view back at the top,
; because a paste, a replace and an undo all happen where the user is looking
; and scrolling away from that would be the opposite of helpful. What it must
; do is drop the checkpoint and wd_rows - wd_redraw seeds the next walk from
; one of them, and both are character indices whose row a bulk edit above the
; view has just renamed.
; -----------------------------------------------------------------------------
wd_editinv:
    push ax
    call wd_hmark
    mov byte [wd_ckok], 0
    mov byte [wd_rowsok], 0
    mov ax, [wd_len]
    cmp [wd_cur], ax
    jbe .cur
    mov [wd_cur], ax
.cur:
    cmp byte [wd_selon], 0      ; the SELECTION is a pair of indices into the
    je .out                     ; same buffer and it was clamped nowhere: a
    cmp [wd_sel1], ax           ; Replace All that shortens the note, or an
    jbe .out                    ; undo of a paste, leaves it pointing past the
    mov [wd_sel1], ax           ; end. wd_selget clamps too - this is the
    mov ax, [wd_sel0]           ; other half, so the stored pair is never a
    cmp ax, [wd_sel1]           ; lie in the first place
    jb .out
    mov byte [wd_selon], 0      ; nothing of it survives
.out:
    pop ax
    ret

; =============================================================================
; Applying character formatting (SPEC.md 65.3)
; =============================================================================

; -----------------------------------------------------------------------------
; wd_chpsync - the caret moved without a selection: the typing attributes are
;              those of the character left of it (Word's rule; at index 0 the
;              character right of it). Preserves all registers.
; -----------------------------------------------------------------------------
wd_chpsync:
    push ax
    push bx
    push es
    cmp byte [wd_selon], 0
    jne .out                    ; a selection carries its own answer
    cmp word [wd_len], 0
    je .out                     ; an empty document keeps what it has
    mov bx, [wd_cur]
    or bx, bx
    jz .at0
    dec bx
.at0:
    ; a ¶ mark's byte is a PAP INDEX, not character attrs (SPEC.md 65.3):
    ; look left past marks to the last real character; a run of marks back
    ; to the start answers plain
    mov es, [wd_dseg]
.skip:
    cmp byte [es:bx], 13
    jne .have
    or bx, bx
    jz .plain
    dec bx
    jmp short .skip
.have:
    mov es, [wd_cseg]
    mov al, [es:bx]
    and al, 0x7F
    mov [wd_chp], al
    jmp short .out
.plain:
    mov byte [wd_chp], 0
.out:
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dispattr - the attr byte the ribbon and the dialog reflect
; out: AL = it; preserves everything else
; The selection's FIRST character while one is up (the honest 8086 answer -
; an AND over a 30KB span per drag pass is not), the typing attrs otherwise.
; -----------------------------------------------------------------------------
wd_dispattr:
    cmp byte [wd_selon], 0
    jne .sel
    mov al, [wd_chp]
    and al, 0x7F
    ret
.sel:
    push bx
    push es
    mov bx, [wd_sel0]
.skip:
    cmp bx, [wd_len]
    jae .fall
    cmp bx, [wd_sel1]
    jae .fall
    mov es, [wd_dseg]               ; a ¶'s byte is a PAP index (SPEC.md
    cmp byte [es:bx], 13            ; 65.3): the first REAL character answers
    jne .have
    inc bx
    jmp short .skip
.have:
    mov es, [wd_cseg]
    mov al, [es:bx]
    and al, 0x7F
    pop es
    pop bx
    ret
.fall:
    mov al, [wd_chp]
    and al, 0x7F
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_applyattr - toggle attribute AL on the selection or the typing attrs
; in:  AL = ONE attribute bit, or 0 = reset to plain; SI = window ptr
; out: nothing; preserves all registers. The caller repaints (wd_redraw's
;      pass 1 finds the dirtied rows through the signatures).
;
; Word's span semantics (SPEC.md 65.3): if ANY character in the span lacks
; the attribute, set it on all; else clear it on all. ONE undo group - the
; bulk record, whose blob banks text AND CHP; the text half comes back
; unchanged and the CHP half is the change.
; -----------------------------------------------------------------------------
wd_applyattr:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov dl, al                  ; DL = the mask
    cmp byte [wd_selon], 0
    je .typing
    call wd_selget              ; AX = start, CX = length
    jc .typing
    mov byte [wd_dirty], 1      ; the span's dress changes (SPEC.md 65.4)
    push ax
    push cx
    call wd_urec_bulk           ; the span, text and CHP, into the arena -
    pop cx                      ; refusal drops the stack ("swept but not
    pop ax                      ; undoable"), and the sweep still runs
    mov es, [wd_cseg]
    mov bx, ax
    mov di, cx                  ; DI = count
    push ds
    mov ds, [wd_dseg]           ; [bx] = the text: a ¶ mark's CHP byte is a
                                ; PAP INDEX (SPEC.md 65.3) and every loop
                                ; below must step over it untouched. wd_*
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
    call wd_urec_bulkend        ; CX is still the span's length
    or dl, dl                   ; hidden changes the LAYOUT (a hidden cell
    jz .relay                   ; occupies none), so the checkpoint and the
    cmp dl, WDAT_HID            ; row table describe a document that just
    jne .done                   ; re-wrapped
.relay:
    cmp dl, WDAT_HID
    jne .norec
    mov byte [wd_hashid], 1         ; hidden entered the document: the cheap
.norec:                             ; column paths stand down (SPEC.md 65.1)
    mov byte [wd_ckok], 0
    mov byte [wd_rowsok], 0
    call wd_hmark
    jmp short .done
.typing:
    or dl, dl
    jz .rst
    xor byte [wd_chp], dl       ; no selection: the toggle rides the NEXT
    jmp short .done             ; character typed
.rst:
    mov byte [wd_chp], 0
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Applying paragraph formatting (SPEC.md 65.3)
;
; Every door - the Ctrl keys, the ruler's cells, the marker drags, the Format
; Paragraph dialog - funnels through wd_modpap, one span modifier: it walks
; the ¶ marks the selection (or the caret's paragraph) touches and rewrites
; each mark's CHP byte to the dictionary index of its MODIFIED format. The
; indices ride the ordinary text+CHP undo machinery; the tail paragraph's
; index lives in [wd_pap_tail] outside the buffers, and a change to it is
; the one paragraph fact undo cannot restore (SPEC.md 65.3).
; =============================================================================

; -----------------------------------------------------------------------------
; wd_papfind - the dictionary index for the candidate at [wd_pfb]
; out: CF=0 and AX = the index (AH=0); CF=1 refused - 256 unique formats
;      exist and this is a 257th (the toast says so). Preserves all others.
; Deduplicated by linear scan: 256 entries of 4 bytes is nothing beside one
; gfx call, and an append never moves or renumbers an entry - which is what
; lets an undo blob's old index still mean the old format.
; -----------------------------------------------------------------------------
wd_papfind:
    push bx
    push cx
    push si
    push es
    mov es, [wd_pseg]
    mov cx, [wd_papn]
    xor si, si
    xor bx, bx                      ; BX = the entry index
.scan:
    jcxz .new
    mov ax, [wd_pfb]
    cmp ax, [es:si]
    jne .next
    mov ax, [wd_pfb+2]
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
    cmp word [wd_papn], WD_PAPMAX
    jae .full
    mov ax, [wd_pfb]
    mov [es:si], ax
    mov ax, [wd_pfb+2]
    mov [es:si+2], ax
    mov ax, [wd_papn]
    inc word [wd_papn]
    clc
    jmp short .out
.full:
    mov ax, wd_m_papfull
    call wd_saymsg
    stc
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; wd_cl0max - clamp signed AL into 0..WD_INDMAX; preserves all others
wd_cl0max:
    or al, al
    jns .pos
    xor al, al
    ret
.pos:
    cmp al, WD_INDMAX
    jle .out
    mov al, WD_INDMAX
.out:
    ret

; wd_clfirst - clamp signed AL (a first-line indent) into -left..WD_INDMAX:
; a first line may hang LEFT of the left indent by at most the indent itself,
; so the pen never crosses the margin. Preserves all others.
wd_clfirst:
    push bx
    cmp al, WD_INDMAX
    jle .hiok
    mov al, WD_INDMAX
.hiok:
    mov bl, [wd_pfb+1]
    neg bl
    cmp al, bl
    jge .out
    mov al, bl
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_modone - apply [wd_ppop]/[wd_pparg] to the format at dictionary index AL
; out: CF=0 and AX = the resulting index; CF=1 the dictionary refused
; preserves everything else; every indent clamp lives here
; -----------------------------------------------------------------------------
wd_modone:
    push bx
    push cx
    push es
    cmp byte [wd_ppop], WDPO_RESET
    jne .setall
    xor ax, ax                      ; Normal: index 0, no dictionary walk
    clc
    jmp .out
.setall:
    cmp byte [wd_ppop], WDPO_SETALL ; the dialog filled [wd_pfb] whole: the
    jne .load                       ; entry copy below would clobber it
    jmp .find
.load:
    xor ah, ah                      ; the entry, copied to the candidate
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov es, [wd_pseg]
    mov ax, [es:bx]
    mov [wd_pfb], ax
    mov ax, [es:bx+2]
    mov [wd_pfb+2], ax
    mov al, [wd_ppop]
    mov ah, [wd_pparg]
    cmp al, WDPO_ALIGN
    je .align
    cmp al, WDPO_SPACE
    je .space
    cmp al, WDPO_SB
    je .sb
    cmp al, WDPO_ADDLEFT
    je .addl
    cmp al, WDPO_HANG
    je .hang
    cmp al, WDPO_UNHANG
    je .unhang
    cmp al, WDPO_SETLEFT
    je .setl
    cmp al, WDPO_SETFIRST
    je .setf
    cmp al, WDPO_SETRIGHT
    je .setr
    jmp .find                       ; WDPO_SETALL: the dialog filled the whole
                                    ; candidate before calling
.align:
    mov al, [wd_pfb]
    and al, 0xFF - WDPA_ALIGN
    or al, ah
    mov [wd_pfb], al
    jmp .find
.space:
    mov al, [wd_pfb]
    and al, 0xFF - WDPA_SPACE
    mov cl, 2
    shl ah, cl
    or al, ah
    mov [wd_pfb], al
    jmp .find
.sb:
    mov al, [wd_pfb]
    and al, 0xFF - WDPA_SB
    or ah, ah
    jz .sbs
    or al, WDPA_SB
.sbs:
    mov [wd_pfb], al
    jmp .find
.addl:
    mov al, [wd_pfb+1]
    add al, ah
    call wd_cl0max
    mov [wd_pfb+1], al
    jmp short .fixf
.hang:
    mov al, [wd_pfb+1]              ; Ctrl-T: the body indents half an inch
    add al, 5                       ; and the first line stays - Word's
    call wd_cl0max                  ; HangingIndent exactly
    mov [wd_pfb+1], al
    mov al, [wd_pfb+2]
    sub al, 5
    call wd_clfirst
    mov [wd_pfb+2], al
    jmp short .fixf
.unhang:
    mov al, [wd_pfb+1]
    sub al, 5
    call wd_cl0max
    mov [wd_pfb+1], al
    mov al, [wd_pfb+2]
    add al, 5
    call wd_clfirst
    mov [wd_pfb+2], al
    jmp short .fixf
.setl:
    mov al, ah
    call wd_cl0max
    mov [wd_pfb+1], al
    jmp short .fixf
.setf:
    mov al, ah
    call wd_clfirst
    mov [wd_pfb+2], al
    jmp short .fixf
.setr:
    mov al, ah
    call wd_cl0max
    mov [wd_pfb+3], al
    jmp short .find
.fixf:
    mov al, [wd_pfb+2]              ; the left indent moved: the first-line
    call wd_clfirst                 ; clamp is relative to it
    mov [wd_pfb+2], al
.find:
    call wd_papfind
.out:
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_modpap - apply operation AL (argument AH) to every paragraph the
;             selection touches, or the caret's (SPEC.md 65.3)
; in:  AL = WDPO_*, AH = argument (signed where the op says), SI = window ptr
; out: nothing; preserves all registers. ONE bulk undo group over
;      [paragraph start .. last affected ¶]; the caller redraws.
; -----------------------------------------------------------------------------
wd_modpap:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov [wd_ppop], al
    mov [wd_pparg], ah
    mov byte [wd_ppstop], 0
    mov byte [wd_dirty], 1          ; a paragraph format is changing (65.4)
    cmp byte [wd_selon], 0
    je .caret
    call wd_selget                  ; AX = start, CX = length
    jnc .selok
.caret:
    mov ax, [wd_cur]
    mov dx, ax
    jmp short .spango
.selok:
    mov dx, ax
    add dx, cx
    dec dx                          ; DX = the LAST selected character
.spango:
    call wd_pback                   ; AX = the first paragraph's start
    mov di, ax                      ; DI = the span's start
    mov bx, dx                      ; scan forward for the LAST char's ¶
    mov cx, [wd_len]
    sub cx, bx
    jbe .tail1
    mov es, [wd_dseg]
.fs:
    cmp byte [es:bx], 13
    je .have13
    inc bx
    dec cx
    jnz .fs
.tail1:
    mov dx, [wd_len]                ; no ¶ behind it: the TAIL paragraph
    mov byte [wd_pptl], 1
    jmp short .spanok
.have13:
    mov dx, bx
    inc dx                          ; the span INCLUDES the ¶ byte
    mov byte [wd_pptl], 0
.spanok:
    mov cx, dx
    sub cx, di
    jcxz .sweep                     ; an empty document: the tail alone
    mov ax, di
    call wd_urec_bulk               ; text AND CHP into the arena (65.3); a
                                    ; refusal drops the stack and the sweep
                                    ; still runs - degrade, not refuse
.sweep:
    mov bx, di
    mov cx, dx
    sub cx, bx
    jcxz .swept
    mov es, [wd_dseg]
.sw:
    cmp byte [es:bx], 13
    jne .swn
    push es
    mov es, [wd_cseg]
    mov al, [es:bx]                 ; the ¶'s index -> its modified format's
    call wd_modone
    jc .swfail
    mov [es:bx], al
    pop es
    or al, al
    jz .swn
    mov byte [wd_hasfmt], 1
    jmp short .swn
.swfail:
    pop es
    mov byte [wd_ppstop], 1
    jmp short .swept
.swn:
    inc bx
    dec cx
    jnz .sw
.swept:
    mov cx, dx
    sub cx, di
    jcxz .notail
    call wd_urec_bulkend            ; CX = the span's length: the text half
                                    ; comes back unchanged, the ¶ bytes are
                                    ; the change
.notail:
    cmp byte [wd_pptl], 0
    je .invd
    cmp byte [wd_ppstop], 0
    jne .invd
    mov al, [wd_pap_tail]           ; the tail paragraph rides its own byte
    call wd_modone
    jc .invd
    mov [wd_pap_tail], al
    or al, al
    jz .invd
    mov byte [wd_hasfmt], 1
.invd:
    mov byte [wd_ckok], 0           ; row starts and heights may all have
    mov byte [wd_rowsok], 0         ; moved (SPEC.md 65.6): the next pass 1
    call wd_hmark                   ; re-lays the view and the moved-rows
    mov byte [wd_follow], 1         ; erase repaints what shifted; the caret's
                                    ; row may have left the view
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_copy - put the selection on the clipboard
; out: CF = 1 nothing was selected, or the clipboard refused (and then the
;      toast says so); preserves all registers
; -----------------------------------------------------------------------------
wd_copy:
    push ax
    push bx
    push cx
    push si
    push es
    call wd_selget              ; AX = start, CX = length
    jc .no
    mov si, ax
    mov es, [wd_dseg]           ; the note is a claim of its own, which is
    call OSAPI_CLIP_PUT         ; exactly why the slot takes a far pointer
    jc .full
    clc
    jmp short .out
.full:
    mov ax, wd_e_cbig
    call wd_saymsg
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
; wd_cut - copy the selection, then take it out
; out: CF = 1 nothing happened; preserves all registers
; The copy comes first and its refusal is final: a cut that lost the text
; because the clipboard would not hold it is the one outcome nobody can undo
; from the keyboard.
; -----------------------------------------------------------------------------
wd_cut:
    call wd_copy
    jc .out
    call wd_seldel
    call wd_editinv
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; wd_paste - replace the selection with the clipboard's text
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
; 32..126 is dropped - exactly wd_load's rule, for exactly wd_load's reason.
; -----------------------------------------------------------------------------
wd_paste:
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
    call wd_seldel              ; a paste REPLACES the selection - and the two
                                ; halves land in ONE undo group, because the
                                ; insert starts exactly where the delete left
    mov bx, [wd_cur]
    mov cx, dx
    call wd_gaproom
    jc .nomem
    mov es, [wd_dseg]
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
    cmp ah, 9                   ; a tab survives the paste now (SPEC.md 65.3)
    jne .fno9
    mov byte [wd_hastab], 1
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
    mov cx, [wd_len]
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
    ; --- mirror the tail compaction on the CHP claim (SPEC.md 65.3): the
    ; filter kept CX of the DX-byte gap, so the CHP tail slides down the
    ; difference; the kept cells already hold [wd_chp] from wd_gaproom
    push cx
    mov es, [wd_cseg]
    mov si, bx
    add si, dx
    mov di, bx
    add di, cx
    mov cx, [wd_len]
    sub cx, si
    jcxz .cnot
    push ds
    mov ds, [wd_cseg]
    cld
    rep movsb
    pop ds
.cnot:
    pop cx
    mov ax, [wd_len]
    sub ax, dx
    add ax, cx
    mov [wd_len], ax            ; the gap was n; only CX of it is text
    ; pasted ¶ marks: their CHP byte is a PAP INDEX, not the typing attrs
    ; wd_gaproom filled (SPEC.md 65.3) - every pasted paragraph joins the
    ; paragraph it was pasted INTO
    push ax
    push cx
    push di
    jcxz .nopfx
    mov ax, bx
    add ax, cx
    call wd_papat               ; AL = the governing index past the paste
    mov ah, al
    mov di, bx
.pfx:
    mov es, [wd_dseg]
    cmp byte [es:di], 13
    jne .pfn
    mov es, [wd_cseg]
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
    call wd_urec_ins            ; AX = where, CX = how many
    add bx, cx
    mov [wd_cur], bx
    call wd_editinv
    clc
    jmp short .out
.nomem:
    mov ax, wd_e_nomem
    call wd_saymsg
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
; A record is three numbers and a blob: at [wd_upos], this group INSERTED
; [wd_uins] bytes and REMOVED the [wd_udel] bytes now sitting at [wd_uoff] in
; the arena. Undoing it is "delete uins at upos, put the blob back at upos",
; and that is the whole of it - which is also why there is no redo: the
; inverse record would need the bytes that are being taken back out, and
; nobody asked for one.
;
; The blobs live in a HEAP CLAIM sized on demand: nothing at all until the
; first edit worth remembering, a kilobyte at a time after that, and up to
; WD_UMAXKB. When it will not grow, the OLDEST record is evicted - a stack
; four deep is still an undo, and a refusal is not - and only when there is
; nothing left to evict is the whole stack dropped. It has to be the whole
; stack: an edit that went unrecorded makes every record under it describe a
; note that no longer exists, and applying one would corrupt the document.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_uclear - forget every record and give the arena back
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_uclear:
    push ax
    push bx
    push dx
    mov byte [wd_un], 0
    mov byte [wd_uopen], 0
    mov word [wd_utop], 0
    mov dx, [wd_useg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE         ; the owner is our segment, which the slot's
    mov word [wd_useg], 0       ; X stub supplies (SPEC.md 50.3)
    mov word [wd_ukb], 0
    mov dx, [wd_cuseg]          ; ...and the CHP arena beside it (SPEC.md
    or dx, dx                   ; 65.3): the two live and die together
    jz .out
    call OSAPI_MEM_FREE
    mov word [wd_cuseg], 0
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_uclose - the open group is finished; the next edit starts a new one
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_uclose:
    mov byte [wd_uopen], 0
    ret

; -----------------------------------------------------------------------------
; wd_utop_rec - the newest record, if one is open
; out: CF = 1 there is none; else CF = 0 and SI = its word offset (every
;      array below is indexed by it). Clobbers BX and SI.
; -----------------------------------------------------------------------------
wd_utop_rec:
    cmp byte [wd_uopen], 0
    je .no
    mov bl, [wd_un]
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
; wd_ugrow - make the arena AX kilobytes
; in:  AX = KB; out: CF = 1 refused; preserves all registers
; -----------------------------------------------------------------------------
wd_ugrow:
    push ax
    push bx
    push dx
    cmp ax, [wd_ukb]
    jbe .yes
    cmp word [wd_useg], 0
    jne .re
    call OSAPI_MEM_CLAIM        ; out CF = 0, DX = the base
    jc .no
    mov [wd_useg], dx
    push ax                     ; ...and the CHP arena, same size (SPEC.md
    call OSAPI_MEM_CLAIM        ; 65.3): every blob offset means the same
    pop ax                      ; thing in both, so nothing forks
    jc .cfail0
    mov [wd_cuseg], dx
    mov [wd_ukb], ax
    jmp short .yes
.cfail0:
    mov dx, [wd_useg]           ; the half-claimed pair is no arena at all
    call OSAPI_MEM_FREE
    mov word [wd_useg], 0
    jmp short .no
.re:
    mov dx, [wd_useg]
    call OSAPI_MEM_REGROW       ; out CF = 0, DX = the base NOW - a grow that
    jc .no                      ; had to move reports a new one (50.3.1)
    mov [wd_useg], dx
    push ax
    mov dx, [wd_cuseg]
    call OSAPI_MEM_REGROW
    pop ax
    jc .cfail1
    mov [wd_cuseg], dx
    mov [wd_ukb], ax
    jmp short .yes
.cfail1:
    push ax                     ; the mirror was refused: shrink the text
    mov ax, [wd_ukb]            ; arena back (a shrink succeeds in place)
    mov dx, [wd_useg]           ; and report one refusal
    call OSAPI_MEM_REGROW
    mov [wd_useg], dx
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
; wd_udrop0 - forget the OLDEST record, sliding the arena down under it
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_udrop0:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp byte [wd_un], 0
    je .out
    mov cx, [wd_udel]           ; record 0's blob, which starts at offset 0
    or cx, cx
    jz .arrays
    mov es, [wd_useg]
    mov si, cx
    xor di, di
    mov bx, [wd_utop]
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
    mov es, [wd_cuseg]          ; the CHP arena slides identically (SPEC.md
    mov si, cx                  ; 65.3): same offsets, same count
    xor di, di
    mov bx, [wd_utop]
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
    mov ax, [wd_utop]
    sub ax, cx
    mov [wd_utop], ax
.arrays:
    xor bx, bx                  ; shift the four arrays down one, and every
.sh:                            ; surviving blob offset with them
    mov al, [wd_un]
    xor ah, ah
    dec ax
    cmp bx, ax
    jae .last
    mov si, bx
    shl si, 1
    mov ax, [si+wd_upos+2]
    mov [si+wd_upos], ax
    mov ax, [si+wd_uins+2]
    mov [si+wd_uins], ax
    mov ax, [si+wd_udel+2]
    mov [si+wd_udel], ax
    mov ax, [si+wd_uoff+2]
    sub ax, cx
    mov [si+wd_uoff], ax
    inc bx
    jmp short .sh
.last:
    dec byte [wd_un]
    cmp byte [wd_un], 0
    jne .out
    mov byte [wd_uopen], 0      ; the record that went WAS the open one, and
                                ; wd_utop_rec has to say so - every blob
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
; wd_uroom - make sure CX more blob bytes fit
; in:  CX = the bytes wanted
; out: CF = 0 there is room; CF = 1 there is not and the whole stack has been
;      dropped. Preserves all registers.
; -----------------------------------------------------------------------------
wd_uroom:
    push ax
    push bx
    push cx
    push dx
.try:
    mov ax, [wd_utop]
    add ax, cx
    jc .evict
    mov bx, [wd_ukb]
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
    cmp ax, WD_UMAXKB
    ja .evict
    call wd_ugrow
    jnc .yes
.evict:
    cmp byte [wd_un], 0
    je .no
    call wd_udrop0
    jmp short .try
.no:
    call wd_uclear
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
; wd_ublob_copy - CX note bytes at SI into the arena at ES:DI
; in:  SI = document offset, DI = arena offset, CX = count, ES = [wd_useg]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_ublob_copy:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    push cx
    push si
    push di
    mov ax, [wd_useg]           ; the text half: document -> arena
    mov es, ax
    mov ax, [wd_dseg]           ; read it BEFORE DS stops being ours
    mov ds, ax
    cld
    rep movsb
    pop di
    pop si
    pop cx
    mov ax, [cs:wd_cuseg]       ; ...and the CHP half at the SAME offsets
    mov es, ax                  ; (SPEC.md 65.3). CS: because DS is the
    mov ax, [cs:wd_cseg]        ; document's segment right now
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
; wd_ublob_app - append the CX note bytes at AX to the open record's blob
; in:  AX = document position, CX = count
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_ublob_app:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call wd_uroom               ; may evict, may drop the lot
    jc .out
    call wd_utop_rec            ; re-asked, because an eviction renumbers the
    jc .out                     ; records and may have taken this one
    push si
    mov di, [wd_utop]
    mov si, ax
    mov es, [wd_useg]
    call wd_ublob_copy
    pop si
    mov ax, [wd_utop]
    add ax, cx
    mov [wd_utop], ax
    mov ax, [si+wd_udel]
    add ax, cx
    mov [si+wd_udel], ax
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_ublob_pre - put the CX note bytes at AX at the FRONT of the open blob,
;                and start the group CX bytes lower
; in:  AX = document position, CX = count
; out: nothing; preserves all registers
;
; The open record's blob is always the LAST one in the arena, so sliding it up
; disturbs nothing else - which is the whole reason the arena is a stack.
; -----------------------------------------------------------------------------
wd_ublob_pre:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wd_uroom
    jc .out
    call wd_utop_rec
    jc .out
    mov dx, [si+wd_udel]        ; the blob's current length...
    mov bx, [si+wd_uoff]        ; ...and where it starts
    add [si+wd_udel], cx
    sub [si+wd_upos], cx        ; the group's span starts CX lower now
    mov es, [wd_useg]
    push ax
    mov ax, [wd_utop]
    push dx                     ; the slide's count, run twice (SPEC.md 65.3)
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
    mov es, [wd_cuseg]          ; slide, byte for byte
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
    call wd_ublob_copy
    mov ax, [wd_utop]
    add ax, cx
    mov [wd_utop], ax
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
; wd_ubegin - close whatever is open and start a record at position AX
; in:  AX = the group's start; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_ubegin:
    push ax
    push bx
    push si
    cmp byte [wd_un], WD_UNDO
    jb .have
    call wd_udrop0              ; five deep means the sixth costs the first
.have:
    mov bl, [wd_un]
    xor bh, bh
    mov si, bx
    shl si, 1
    mov [si+wd_upos], ax
    mov word [si+wd_uins], 0
    mov word [si+wd_udel], 0
    mov ax, [wd_utop]
    mov [si+wd_uoff], ax
    inc byte [wd_un]
    mov byte [wd_uopen], 1
    call wd_hire                ; the group's half-second is measured by the
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
; wd_urec_ins - record an insertion of CX bytes at AX
; in:  AX = where, CX = how many; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_urec_ins:
    push ax
    push bx
    push si
    cmp byte [wd_unolog], 0
    jne .out
    jcxz .out
    call wd_utop_rec
    jc .new
    mov bx, [si+wd_upos]
    add bx, [si+wd_uins]
    cmp bx, ax
    jne .new                    ; not where this group left off: a new one
    add [si+wd_uins], cx
    jmp short .out
.new:
    call wd_ubegin
    call wd_utop_rec
    jc .out
    mov [si+wd_uins], cx
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_urec_del - record the removal of CX bytes at AX, copying them into the
;               arena while they are still in the note
; in:  AX = where, CX = how many; out: nothing; preserves all registers
;
; Four cases, tested in this order, and the order matters:
;   1. the bytes are ones THIS group inserted - they were never in the note
;      before it, so nothing reaches the blob and [wd_uins] simply shrinks.
;      This is a backspace walking back over what was just typed;
;   2. they sit immediately AFTER the group's span - original text, so they
;      belong at the END of the blob. This is forward Delete while typing;
;   3. immediately BEFORE it - a backspace walking left out of the group - so
;      they belong at the FRONT of it and the span starts lower;
;   4. anywhere else: a new group.
; Case 1 has to come first because when [wd_uins] is 0 cases 2 and 3 can both
; look true, and taking 1 when it does not apply would forget a deletion.
; -----------------------------------------------------------------------------
wd_urec_del:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [wd_unolog], 0
    jne .out
    jcxz .out
    call wd_utop_rec
    jc .new
    mov dx, [si+wd_upos]
    add dx, [si+wd_uins]        ; DX = one past the group's span
    mov bx, ax
    add bx, cx                  ; BX = one past the deletion
    cmp bx, dx
    jne .try2
    cmp cx, [si+wd_uins]
    ja .try2
    sub [si+wd_uins], cx        ; 1.
    jmp short .out
.try2:
    cmp ax, dx
    jne .try3
    call wd_ublob_app           ; 2.
    jmp short .out
.try3:
    cmp bx, [si+wd_upos]
    jne .new
    call wd_ublob_pre           ; 3.
    jmp short .out
.new:
    call wd_ubegin              ; 4. AX is still the position
    call wd_ublob_app
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_urec_bulk - record a whole-span replacement, for the operations that
;                rewrite more than one place at once
; in:  AX = where the change begins, CX = the bytes there NOW that are about
;      to be replaced (Replace All passes the whole tail of the note)
; out: CF = 1 = it could not be recorded, and the stack has been dropped;
;      preserves all registers
;
; Drag-and-drop and Replace All both move text at two positions at once, which
; the (pos, inserted, removed) record cannot say. Saying it as ONE replacement
; of everything between them can, exactly, at the price of a bigger blob - and
; WD_UMAXKB is 16 so that a Replace All over a full note still fits.
; wd_urec_bulkend closes it with the new length.
; -----------------------------------------------------------------------------
wd_urec_bulk:
    push ax
    push bx
    push cx
    push si
    call wd_uclose              ; a bulk change is never part of a typing run
    call wd_ubegin
    call wd_ublob_app
    call wd_utop_rec
    jc .no
    cmp [si+wd_udel], cx        ; wd_ublob_app is allowed to give up
    jne .no
    clc
    jmp short .out
.no:
    call wd_uclear
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; wd_urec_bulkend - in: CX = the bytes that replaced them. Preserves all.
wd_urec_bulkend:
    push bx
    push si
    call wd_utop_rec
    jc .out
    mov [si+wd_uins], cx
    call wd_uclose
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_undo - take the newest group back (SPEC.md 27.9)
; out: CF = 1 there was nothing to undo; preserves all registers
; -----------------------------------------------------------------------------
wd_undo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wd_uclose
    cmp byte [wd_un], 0
    je .no
    mov byte [wd_dirty], 1      ; undone is still different from the disk
    mov bl, [wd_un]
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, bx
    mov byte [wd_unolog], 1     ; an undo is not an edit to be remembered
    mov ax, [si+wd_upos]
    mov cx, [si+wd_uins]
    mov di, [si+wd_uoff]
    mov dx, [si+wd_udel]
    push dx                     ; the blob's length, wanted twice below
    push di                     ; ...and where it is
    push ax                     ; ...and where all of this happens
    jcxz .noins
    mov bx, ax
    call wd_delspan             ; the group's insertion comes back out
.noins:
    pop ax
    pop di
    pop dx
    push ax
    mov cx, dx
    jcxz .nodel
    mov bx, ax
    call wd_gaproom             ; ...and the bytes it removed go back in
    jc .fail
    mov si, di
    mov di, bx
    push ds
    push es
    push cx                     ; the copy runs twice - text arena into the
    push si                     ; document, CHP arena into the CHP claim,
    push di                     ; same offsets both times (SPEC.md 65.3)
    mov es, [wd_dseg]           ; both segments loaded while DS is still ours
    mov ax, [wd_useg]
    mov ds, ax
    cld
    rep movsb
    pop di
    pop si
    pop cx
    mov ax, [cs:wd_cseg]        ; CS: - DS is the arena's segment here
    mov es, ax
    mov ax, [cs:wd_cuseg]
    mov ds, ax
    rep movsb
    pop es
    pop ds
.nodel:
    pop ax
    add ax, dx                  ; the caret lands at the end of what came back
    mov [wd_cur], ax
    call wd_selclr
    mov ax, [wd_utop]
    sub ax, dx
    mov [wd_utop], ax           ; the blob is the arena's last, so this is all
    dec byte [wd_un]            ; there is to giving it back
    mov byte [wd_uopen], 0
    mov byte [wd_unolog], 0
    call wd_editinv
    clc
    jmp short .out
.fail:
    pop ax                      ; the gap was refused: the record stays where
    mov byte [wd_unolog], 0     ; it is and the note is short of its insertion,
    call wd_editinv             ; which is the honest half of an undo rather
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
; The search engine (SPEC.md 65.7)
;
; A LITERAL scan over the note with the authentic search.des options - Whole
; Word and Match Upper/Lowercase - and one wrap. Word 1.1 had no regex; the
; engine Note Pad carried (and its docked panel) is deleted here, the bytes
; reclaimed, and Edit > Search... / Replace... / Go To... are modal dialogs
; on the SPEC.md 65.3 framework.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_docb - AL = the note's byte at index DI
; wd_docb2 - ...and at index AX
; both preserve every other register
; -----------------------------------------------------------------------------
wd_docb:
    push bx
    push es
    mov es, [wd_dseg]
    mov bx, di
    mov al, [es:bx]
    pop es
    pop bx
    ret
wd_docb2:
    push bx
    push es
    mov es, [wd_dseg]
    mov bx, ax
    mov al, [es:bx]
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_foldc - AL folded for a caseless compare: a-z -> A-Z (SPEC.md 65.7)
; preserves everything else
; -----------------------------------------------------------------------------
wd_foldc:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; -----------------------------------------------------------------------------
; wd_iswordc - CF = 1 when AL is a word character: A-Z a-z 0-9 (SPEC.md 65.7)
; preserves all registers
; -----------------------------------------------------------------------------
wd_iswordc:
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
; wd_matchat - does the search text match the note at index AX? (SPEC.md 65.7)
; in:  AX = the index; the text in wd_fpat, options in [wd_fopt]
; out: CF = 0 with [wd_rxend] = one past the match; CF = 1 = no match HERE
;      (this asks about one position - wd_findfrom is what walks).
;      Preserves all registers.
;
; A LITERAL scan - Word 1.1 had no regex and neither does this port (the
; engine Note Pad carried is gone with its panel, SPEC.md 65.7). Match
; Upper/Lowercase clear folds BOTH sides through wd_foldc; Whole Word demands
; a non-word character (or the document's edge) on each side of the match.
; -----------------------------------------------------------------------------
wd_matchat:
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
    cmp si, [wd_fpwn]
    jae .tail
    mov bx, si
    shl bx, 1
    mov bx, [bx+wd_fpw]         ; BX = the compiled element
    cmp bx, WDP_WHITE
    je .white
    mov ax, di
    cmp ax, [wd_len]
    jae .fail
    call wd_docb                ; AL = the note at DI
    cmp bx, WDP_ANY
    je .step                    ; '?' matches any one character
    test byte [wd_fopt], WDFO_CASE
    jnz .exact
    call wd_foldc               ; fold the note's byte...
    xchg al, bl
    call wd_foldc               ; ...and the pattern's
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
    cmp ax, [wd_len]            ; greedily, and counts as one element
    jae .fail
    call wd_docb
    call wd_iswhite
    jc .fail
.wmore:
    inc di
    mov ax, di
    cmp ax, [wd_len]
    jae .wdone
    call wd_docb
    call wd_iswhite
    jnc .wmore
.wdone:
    inc si
    jmp short .ll
.tail:
    test byte [wd_fopt], WDFO_WORD
    jz .match
    or dx, dx                   ; Whole Word: the character left of the
    jz .rgt                     ; match (the edge counts as non-word)...
    mov ax, dx
    dec ax
    call wd_docb2
    call wd_iswordc
    jc .fail
.rgt:
    mov ax, di                  ; ...and the one after it
    cmp ax, [wd_len]
    jae .match
    call wd_docb2
    call wd_iswordc
    jc .fail
.match:
    mov [wd_rxend], di
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

; wd_iswhite - CF=0 if AL is white space: FMatchWhiteSpace's own set, less
; the non-breaking space this character set has no room for (SPEC.md 65.7).
; Preserves all registers.
wd_iswhite:
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
; wd_findfrom - the first match at or after index AX, wrapping to the top
; in:  AX = where to start looking
; out: CF = 0 with AX = the match's start and DX = one past its end;
;      CF = 1 = the text is not in the note at all. Preserves all others.
;
; The wrap is unconditional and is the whole of what "loops once" means: the
; second pass stops where the first began, so a text that occurs once is
; found from anywhere and one that occurs nowhere is refused after exactly
; one traversal (SPEC.md 65.7).
; -----------------------------------------------------------------------------
wd_findfrom:
    push bx
    push cx
    push si
    push di
    cmp word [wd_fpatn], 0
    je .no
    mov bx, ax
.p1:
    cmp ax, [wd_len]
    ja .wrap
    call wd_matchat
    jnc .hit
    inc ax
    jmp short .p1
.wrap:
    xor ax, ax                  ; past the end and back to the top
.p2:
    cmp ax, bx
    jae .no
    call wd_matchat
    jnc .hit
    inc ax
    jmp short .p2
.hit:
    mov dx, [wd_rxend]
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
; wd_findfwd - the first match at or after index AX, NO wrap (SPEC.md 65.7)
; in:  AX = where to start; out: as wd_findfrom (AX = start, DX = end)
; The replace sweeps' walker: a sweep that wrapped would re-visit its own
; replacements.
; -----------------------------------------------------------------------------
wd_findfwd:
    cmp word [wd_fpatn], 0
    je .no
.l:
    cmp ax, [wd_len]
    ja .no
    call wd_matchat
    jnc .hit
    inc ax
    jmp short .l
.hit:
    mov dx, [wd_rxend]
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wd_findprev - the last match that STARTS before index AX, wrapping to the
;               bottom of the note
; in:  AX = the limit
; out: as wd_findfrom
;
; One forward walk, remembering the last match seen before the limit and the
; last one seen at all. It steps match-to-match rather than character-to-
; character, so the pass is bounded by the number of matches.
; -----------------------------------------------------------------------------
wd_findprev:
    push bx
    push cx
    push si
    push di
    cmp word [wd_fpatn], 0
    je .no
    mov bx, ax                  ; BX = the limit
    mov cx, 0xFFFF              ; CX = the best answer before it...
    mov si, 0xFFFF              ; SI = ...and the last one anywhere
    xor ax, ax
.l:
    cmp ax, [wd_len]
    ja .done
    call wd_matchat
    jc .step1
    mov si, ax
    cmp ax, bx
    jae .adv
    mov cx, ax
.adv:
    mov di, [wd_rxend]          ; matches are counted without overlapping
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
    call wd_matchat             ; re-run it for its end, which is one match's
    jc .no                      ; worth of work rather than a second array
    mov dx, [wd_rxend]
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
; wd_showmatch - select [AX, DX) and make sure it is on screen
; in:  AX = start, DX = end, SI = window ptr
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_showmatch:
    push ax
    push dx
    mov [wd_fmst], ax
    mov [wd_fmen], dx
    call wd_selset
    mov [wd_cur], dx            ; the caret sits at the END of it, so F4 twice
                                ; walks forwards rather than finding the same
                                ; match again
    mov byte [wd_follow], 1     ; wd_redraw scrolls it into view for us
    mov byte [wd_ckok], 0       ; the checkpoint is the CARET's row start and
    pop dx                      ; the caret just jumped somewhere else
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_donext - F4: the next match after the caret (SPEC.md 65.7)
; wd_doprev - Shift-F4: the last one before it
; in:  SI = window ptr; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
wd_donext:
    call wd_uclose
    mov ax, [wd_cur]
    call wd_findfrom
    jc wd_fmiss
    call wd_showmatch
    ret

wd_doprev:
    call wd_uclose
    mov ax, [wd_sel0]
    cmp byte [wd_selon], 0
    jne .have
    mov ax, [wd_cur]
.have:
    call wd_findprev
    jc wd_fmiss
    call wd_showmatch
    ret

; wd_fmiss - say why nothing happened (SPEC.md 65.7's authentic string).
; Preserves all registers.
wd_fmiss:
    push ax
    mov ax, wd_m_nfound
    cmp word [wd_fpatn], 0
    jne .say
    mov ax, wd_m_nopat
.say:
    call wd_saymsg
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_replat - swap the CX bytes at AX for the replacement text
; in:  AX = where, CX = how many bytes go
; out: CF = 0 with DX = one past the text that replaced them; CF = 1 refused
;      and nothing changed. Preserves all other registers.
; -----------------------------------------------------------------------------
wd_replat:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, ax
    push ax                     ; ^m and ^c are resolved against THIS match
    push cx                     ; (SPEC.md 65.7), so the bytes spliced in are
    mov [wd_fmst], ax           ; built here and not taken from the dialog
    add ax, cx
    mov [wd_fmen], ax
    call wd_rxpand
    pop cx
    pop ax
    jc .nox
                                ; THE ROOM COMES FIRST, and the order is the
                                ; whole of this routine's promise. wd_delspan
                                ; used to run before wd_gaproom, so a refused
                                ; claim returned CF=1 with the match ALREADY
                                ; GONE - which the header calls "nothing
                                ; changed". Reachable without a claim failure
                                ; at all: a note loaded at WD_MAXKB has
                                ; wd_len == wd_cap, so every replacement longer
                                ; than its match is refused, and the caller's
                                ; `jc` then skips wd_editinv with [wd_cur] left
                                ; at the match END - past [wd_len] for a match
                                ; at the end of the note, and the next
                                ; keystroke walks 65,535 bytes backwards
                                ; through the document claim.
    mov ax, [wd_len]
    sub ax, cx                  ; what the note becomes: the span goes...
    add ax, [wd_frxn]           ; ...and the replacement arrives
    jc .no                      ; a 16-bit note cannot pass 65,535
    push bx
    push cx
    call wd_capfor              ; non-destructive, and preserves everything
    pop cx
    pop bx
    jc .no                      ; refused with the match still there
    push bx
    call wd_delspan             ; out with the old...
    pop bx
    mov cx, [wd_frxn]
    push cx
    call wd_gaproom             ; ...and in with the new, which can no longer
    pop cx                      ; be refused for want of room
    jc .no
    mov es, [wd_dseg]
    mov di, bx
    mov si, wd_frx
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
    mov cx, [wd_frxn]
    call wd_urec_ins
    mov dx, bx
    add dx, [wd_frxn]
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
; wd_dorepall - replace every match, from the top of the note
; in:  SI = window ptr; out: nothing; clobbers what a callback may
;
; ONE undo record for the whole sweep, and it has to be one: the operation
; nobody wants to retype by hand is exactly the one worth being able to take
; back, and five separate records would only take back the last five. It is
; recorded as a replacement of everything from the first match to the end of
; the note (SPEC.md 27.9's bulk form), because that is the smallest span the
; three-number record can describe honestly.
; -----------------------------------------------------------------------------
wd_dorepall:
    push ax
    push bx
    push cx
    push dx
    push di
    call wd_uclose
    xor ax, ax
    call wd_findfrom            ; is there anything to do at all?
    jc .miss
    push ax
    mov cx, [wd_len]
    sub cx, ax
    call wd_urec_bulk           ; the tail as it stands, into the blob
    pop ax
    mov byte [wd_unolog], 1     ; every edit below is inside that one record
    xor bx, bx                  ; BX = how many were replaced
    mov di, ax                  ; DI = where the caret ends up
.l:
    cmp ax, [wd_len]
    ja .done
    call wd_matchat
    jc .step
    mov cx, [wd_rxend]
    sub cx, ax
    call wd_replat              ; AX survives; DX = one past the new text
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
    mov byte [wd_unolog], 0
    call wd_urec_bulkend_at
    mov [wd_cur], di
    call wd_selclr
    call wd_editinv
    mov byte [wd_follow], 1
    mov ax, bx
    call wd_saycnt
    jmp short .out
.miss:
    call wd_fmiss
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_urec_bulkend_at - close the bulk record: what replaced its span is
; everything from the record's own start to the end of the note. This is the
; Replace All shape, where the sweep runs to the end by construction.
; Preserves all registers.
wd_urec_bulkend_at:
    push bx
    push cx
    push si
    call wd_utop_rec
    jc .out
    mov cx, [wd_len]
    sub cx, [si+wd_upos]
    mov [si+wd_uins], cx
    call wd_uclose
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
; wd_selpace - drop the lock, wait for the tick, take it back
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_selpace:
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
; wd_hitpt - the character index under the pointer, scrolling the view when
;            the pointer has left it
; in:  CX = x, DX = y (absolute), SI = window ptr, wd_bounds run, lock held
; out: AX = the index; CF = 1 if [wd_top] moved as well. Preserves all others.
;
; The scroll is what makes a selection longer than the window possible at all,
; and it is one row a tick because that is the rate the drag loop runs at.
; -----------------------------------------------------------------------------
wd_hitpt:
    push bx
    push cx
    push dx
    xor bx, bx                  ; BX = "the view moved"
    mov ax, [wd_vrows]
    or ax, ax
    jz .hit
    cmp dx, [wd_ty]
    jb .up
    dec ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]             ; AX = the last visible row's top
    cmp dx, ax
    jbe .hit
    mov dx, ax                  ; below the view: clamp, and page down one
    mov ax, [wd_top]
    inc ax
    call wd_scrollto
    jc .hit                     ; CF = 1 from wd_scrollto means it did NOT
    mov bx, 1                   ; move, which is the bottom of the note
    jmp short .hit
.up:
    mov dx, [wd_ty]
    mov ax, [wd_top]
    dec ax
    call wd_scrollto
    jc .hit
    mov bx, 1
.hit:
    mov [wd_hitx], cx
    mov [wd_hity], dx
    mov word [wd_wanty], 0x7FFF
    push ax                     ; the pointer names ONE row, and wd_rows knows
    push dx                     ; where it starts (SPEC.md 27.5) - so seed
    call wd_yrow                ; there and stop after it, which is what
    jc .nseed                   ; wd_onclick has always done for a click; the
    mov dx, ax                  ; ys are banked, so formats answer too, and a
    call wd_seedrow             ; refusal is the unseeded walk (SPEC.md 65.6)
.nseed:
    pop dx
    pop ax
    call wd_measure
    mov byte [wd_resume], 0
    mov ax, [wd_hiti]
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
; wd_dragsel - follow the pointer, extending the selection from [wd_anchor]
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
wd_dragsel:
    push ax
    push bx
    push cx
    push dx
    mov bx, [wd_anchor]         ; what the last pass resolved
    mov word [wd_lmx], 0xFFFF   ; ...and where the pointer was when it did
    mov word [wd_lmy], 0xFFFF
.pass:
    call wd_selpace
    call OSAPI_MOUSE            ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .up
    call wd_bounds
    cmp cx, [wd_lmx]            ; A POINTER THAT HAS NOT MOVED HAS NOTHING TO
    jne .moved                  ; SAY. The loop runs at a tick whether the
    cmp dx, [wd_lmy]            ; mouse reports anything or not, and at 1200
    jne .moved                  ; baud it usually does not
    cmp dx, [wd_ty]
    jb .moved                   ; ...unless it is parked outside the view,
    push ax                     ; where every tick owes another row of scroll
    mov ax, [wd_vrows]
    or ax, ax
    jz .still
    dec ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]             ; the last visible row's top - wd_hitpt's own
    cmp dx, ax                  ; threshold for scrolling, so the two cannot
    ja .scrolling               ; disagree about which passes matter
.still:
    pop ax
    jmp short .pass
.scrolling:
    pop ax
.moved:
    mov [wd_lmx], cx
    mov [wd_lmy], dx
    call wd_hitpt
    jc .draw                    ; the view scrolled: owed a redraw either way
    cmp ax, bx
    je .pass
.draw:
    mov bx, ax
    mov [wd_cur], ax
    mov dx, [wd_anchor]
    call wd_selset
    mov byte [wd_ckok], 0       ; the checkpoint is the CARET's row start and
    mov byte [wd_selonly], 1    ; the caret has just jumped - and a drag moves
                                ; no character, so every dirty row owes an
                                ; inversion and no glyphs (SPEC.md 27.8.2)
    call wd_redraw
    jmp short .pass
.up:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rev - reverse the note's bytes in [AX, BX], inclusive
; in:  AX, BX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wd_rev:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [wd_dseg]
    call wd_rev1                ; the text...
    mov es, [wd_cseg]           ; ...and its CHP bytes, identically (SPEC.md
    call wd_rev1                ; 65.3): the rotate moves dress with glyphs
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; wd_rev1 - reverse ES:[AX..BX] inclusive; preserves AX/BX, clobbers CX/SI/DI
wd_rev1:
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
; wd_dmark_on / wd_dmark_off - the drop point's insertion bar
; in:  [wd_dpos] (0xFFFF = none), SI = window ptr, lock held
; out: nothing; preserves all registers
;
; XOR, so the erase is the draw again - and the pixels it was drawn at are
; BANKED rather than recomputed, because a scroll between the two would move
; where the bar belongs and leave the old one behind for good. That is
; SPEC.md 48.11's rule in miniature.
; -----------------------------------------------------------------------------
wd_dmark_on:
    push ax
    push bx
    push cx
    push dx
    mov word [wd_dmark], 0xFFFF
    mov ax, [wd_dpos]
    cmp ax, 0xFFFF
    je .out
    mov bx, [wd_cur]
    push bx
    mov [wd_cur], ax            ; wd_walk answers where the CARET is, so the
    mov word [wd_hity], 0xFFFF  ; drop point borrows it for one measure pass
    mov word [wd_wanty], 0x7FFF
    call wd_measure
    pop bx
    mov [wd_cur], bx
    mov byte [wd_ckok], 0       ; ...and the checkpoint that pass left behind
                                ; describes the drop point's row, not ours
    cmp byte [wd_curseen], 0
    je .out
    mov ax, [wd_curx]
    mov bx, [wd_cury]
    cmp bx, [wd_ty]
    jb .out
    mov dx, bx
    add dx, [wd_gh1]
    cmp dx, [wd_bot]
    ja .out
    mov cx, ax
    call OSAPI_GFX_XOR_FILL
    mov [wd_dmark], ax
    mov [wd_dmy], bx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wd_dmark_off:
    push ax
    push bx
    push cx
    push dx
    mov ax, [wd_dmark]
    cmp ax, 0xFFFF
    je .out
    mov bx, [wd_dmy]
    mov cx, ax
    mov dx, bx
    add dx, [wd_gh1]
    call OSAPI_GFX_XOR_FILL
    mov word [wd_dmark], 0xFFFF
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_movesel - move the selected text to [wd_dpos]
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
wd_movesel:
    push ax
    push bx
    push cx
    push dx
    call wd_selget              ; AX = s, CX = n
    jc .out
    mov [wd_mvs], ax
    mov [wd_mvn], cx
    add ax, cx
    mov [wd_mve], ax
    mov ax, [wd_dpos]
    mov [wd_mvp], ax
    cmp ax, [wd_mvs]
    jb .left
    cmp ax, [wd_mve]
    jbe .out                    ; dropped inside itself: nothing to do
    mov ax, [wd_mvs]            ; --- rightwards: [s,e)[e,p) -> [e,p)[s,e) ---
    mov cx, [wd_mvp]
    sub cx, ax
    call wd_urec_bulk           ; the whole span [s, p), as one replacement
    mov byte [wd_unolog], 1
    mov ax, [wd_mvs]
    mov bx, [wd_mve]
    dec bx
    call wd_rev
    mov ax, [wd_mve]
    mov bx, [wd_mvp]
    dec bx
    call wd_rev
    mov ax, [wd_mvs]
    mov bx, [wd_mvp]
    dec bx
    call wd_rev
    mov ax, [wd_mvp]
    sub ax, [wd_mvn]            ; the block ends where it was dropped
    jmp short .fin
.left:                          ; --- leftwards: [p,s)[s,e) -> [s,e)[p,s) ---
    mov ax, [wd_mvp]
    mov cx, [wd_mve]
    sub cx, ax
    call wd_urec_bulk           ; the whole span [p, e)
    mov byte [wd_unolog], 1
    mov ax, [wd_mvp]
    mov bx, [wd_mvs]
    dec bx
    call wd_rev
    mov ax, [wd_mvs]
    mov bx, [wd_mve]
    dec bx
    call wd_rev
    mov ax, [wd_mvp]
    mov bx, [wd_mve]
    dec bx
    call wd_rev
    mov ax, [wd_mvp]            ; ...and here it begins where it was dropped
.fin:
    mov byte [wd_unolog], 0
    mov dx, ax
    add dx, [wd_mvn]
    call wd_selset              ; it stays selected, which is what the user
    mov [wd_cur], dx            ; just spent a drag pointing at
    call wd_urec_bulkend_span
    call wd_editinv
    mov byte [wd_follow], 1
    mov byte [wd_ckok], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_urec_bulkend_span - close a bulk record whose span did not change length
; (a move rearranges bytes, it does not add or remove any). Preserves all.
wd_urec_bulkend_span:
    push bx
    push cx
    push si
    call wd_utop_rec
    jc .out
    mov cx, [si+wd_udel]
    mov [si+wd_uins], cx
    call wd_uclose
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_dragmove - a press that landed INSIDE the selection
; in:  AX = the index pressed, SI = window ptr, lock held
; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
wd_dragmove:
    push ax
    push bx
    push cx
    push dx
    mov [wd_anchor], ax         ; where the press was, for the click case
    mov word [wd_dpos], 0xFFFF
    mov word [wd_dmark], 0xFFFF
    call OSAPI_MOUSE
    mov [wd_dpx], cx
    mov [wd_dpy], dx
    xor bx, bx                  ; BX = the pointer has left the dead zone
.pass:
    call wd_selpace
    call OSAPI_MOUSE
    test al, 1
    jz .up
    or bx, bx
    jnz .track
    mov ax, cx
    sub ax, [wd_dpx]
    call wd_absw
    cmp ax, WD_SELDRAG
    ja .moved
    mov ax, dx
    sub ax, [wd_dpy]
    call wd_absw
    cmp ax, WD_SELDRAG
    jbe .pass
.moved:
    mov bx, 1
.track:
    call wd_bounds
    call wd_hitpt
    jnc .same
    mov word [wd_dmark], 0xFFFF ; the view scrolled out from under the bar
    mov word [wd_dpos], 0xFFFF
    call wd_redraw
.same:
    cmp ax, [wd_dpos]
    je .pass
    call wd_dmark_off
    mov [wd_dpos], ax
    call wd_dmark_on
    jmp short .pass
.up:
    call wd_dmark_off
    or bx, bx
    jz .click
    cmp word [wd_dpos], 0xFFFF
    je .click
    call wd_movesel
    call wd_redraw
    jmp short .out
.click:
    mov ax, [wd_anchor]         ; never left the dead zone: it was a click,
    mov [wd_cur], ax            ; and a click puts the caret where it landed
    call wd_selclr
    mov byte [wd_ckok], 0
    call wd_chpsync             ; ...and the typing attrs with it (65.3)
    call wd_redraw
.out:
    mov word [wd_dpos], 0xFFFF
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_absw - AX = |AX|, treating it as signed. Preserves all other registers.
wd_absw:
    or ax, ax
    jns .out
    neg ax
.out:
    ret

; =============================================================================
; A View toggle MOVES the text band (SPEC.md 65.2; this blit once served the
; find panel, SPEC.md 27.10.2 - the panel is gone, the mechanism stays)
;
; A ribbon/ruler toggle changes exactly one number - the chrome top wd_bounds
; adds to [wd_ty] - and changes NOTHING about the wrap: [wd_tx] and [wd_rgt]
; are the content's own edges. So every row holds exactly the same characters
; before and after; they are simply H pixels lower or higher, and the view
; keeps the same [wd_top]. That makes the whole repaint a BLIT: one
; OSAPI_GFX_SCROLL plus, on a close, the rows the text moving up EXPOSES at
; the bottom - the only part that was never on screen.

;
; Three things make it safe, and all three are refusals rather than
; corrections, because a wrong blit shows text that was never in the note:
;
;  - **Only [wd_ty] may have moved.** [wd_tx], [wd_rgt] and [wd_bot] are
;    compared against what wd_sigmark recorded; a resize that happens to
;    coincide falls back.
;  - **[wd_top] must not be clamped.** Closing GROWS [wd_vrows], which shrinks
;    wd_scrollmax, and a view that has to move renames every row. Opening
;    shrinks vrows and so can never need it.
;  - **A toast, the visual break and stale signatures all refuse.** The toast
;    is drawn over the text at a y the panel moves, so the blit would carry it
;    to the wrong place and nothing would put it back - wd_scrollpaint refuses
;    for the same reason (SPEC.md 27.7.2).
;
; The panel's OWN pixels are inside the band that moves, which is what makes
; closing leave nothing behind: they blit off the top of the content and are
; clipped. What the band cannot reach is the <8px left margin the x-rounding
; gives up and the scroll bar's columns; both are repainted afterwards, which
; wd_sbar was going to do anyway because the thumb has moved.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_panmove - move the text to where the panel now leaves room for it
; in:  SI = window ptr, wd_bounds ALREADY run for the NEW geometry, gfx lock
;      held
; out: CF = 0 the screen is correct and the signatures describe it; CF = 1
;      nothing was drawn and the caller must repaint in full.
;      Clobbers what a window callback may.
; -----------------------------------------------------------------------------
wd_panmove:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [wd_sigok], 0
    je .no                      ; the arrays do not describe the screen
    cmp byte [wd_bmode], 0
    jne .no
    cmp byte [wd_hasfmt], 0     ; formatted rows land at arbitrary ys and the
    jne .no                     ; exposed-row arithmetic below is 8px: the
                                ; full repaint is the honest path (SPEC.md
                                ; 65.6) - a panel or View toggle is rare
    mov ax, [wd_tx]             ; only [wd_ty] may have moved
    cmp ax, [wd_stx]
    jne .no
    mov ax, [wd_rgt]
    cmp ax, [wd_srgt]
    jne .no
    mov ax, [wd_bot]
    cmp ax, [wd_sbot]
    jne .no
    cmp word [wd_vrows], 0
    je .no
    mov di, [wd_ty]
    sub di, [wd_sty]            ; DI = how far the text is moving, + = down
    jz .no
    jns .open
    call wd_scrollmax           ; closing: the view has more rows to show, so
    cmp [wd_top], ax            ; [wd_top] may be past the new maximum - and a
    ja .no                      ; view that moves renames every row
.open:

    mov bx, si                  ; the band is everything below the chrome
    call OSAPI_WM_CONTENT       ; strips, so the panel's own pixels move with
    push ax                     ; the text and the menu bar never rides a
    mov ax, [wd_sctop]          ; blit (SPEC.md 65.2). The band's top is the
    cmp ax, [wd_ctop]           ; SMALLER of the chrome top the screen was
    jbe .stop                   ; drawn under and the live one: for a panel
    mov ax, [wd_ctop]           ; open/close the two are equal (the old
.stop:                          ; behaviour exactly), and for a View toggle
    add dx, ax                  ; the smaller top is what keeps every moving
    pop ax                      ; source row inside the blit's rect - the
                                ; strip rows it vacates are chrome in the new
                                ; layout and wd_vtoggle repaints them.
    push ax                     ; AX = left, DX = the band's top
    push dx
    mov ax, [wd_tx]
    and ax, 0xFFF8              ; x1 down to a byte column - [wd_tx] and not
                                ; the content's own left edge, because
                                ; rounding THAT down would leave the content
    mov cx, [wd_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                      ; x2, with x2+1 up to a byte column
    pop bx                      ; y1 = content top
    push bx
    mov dx, [wd_bot]            ; y2 = content bottom
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
    mov cx, [wd_tx]
    and cx, 0xFFF8
    dec cx                      ; x2 = just left of the band
    mov dx, [wd_bot]
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
    ; [wd_ty]; wd_sbar redraws the bar from [wd_ty] down at .done anyway.
    mov ax, [wd_rgt]
    inc ax                      ; x1 = the first bar column
    mov cx, [wd_sbr]            ; x2 = the content's right edge
    mov dx, [wd_ty]
    dec dx                      ; y2 = just above the bar's new top
    cmp bx, dx                  ; y1 = the band's top, still in BX
    jg .nobar
    call OSAPI_GFX_FILL
.nobar:

    or di, di
    js .close

    ; --- the band moved DOWN (a chrome strip came back): the strip the text
    ; left behind is chrome in the new layout and wd_vtoggle repaints it. The
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
    mov bx, [wd_bot]
    sub bx, ax
    inc bx
    sub bx, [wd_ty]
    jns .r0
    xor bx, bx
.r0:
    mov cl, 3
    shr bx, cl                  ; BX = the first exposed row
    cmp bx, [wd_vrows]
    jae .tail                   ; nothing was exposed
    mov [wd_dr0], bx
    mov ax, [wd_vrows]
    dec ax
    mov [wd_dr1], ax

    push cx                     ; erase the band: OSAPI_GFX_SCROLL leaves what
    mov ax, bx                  ; it vacates holding a copy of its neighbour
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]
    mov bx, ax                  ; y1
    mov dx, [wd_bot]            ; y2 - to the very bottom, so the sliver under
                                ; the last whole row goes too
    mov ax, [wd_tx]
    sub ax, WD_MARGIN           ; x1 = the content's own left edge
    mov cx, [wd_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [wd_prowi], 0xFFFF ; the fill erased what the delta cache knew

    mov word [wd_hity], 0xFFFF  ; one pass, drawing AND re-signing: the band
    mov word [wd_wanty], 0x7FFF ; was just filled, so wd_clean
    mov byte [wd_draw], 1
    mov byte [wd_sigup], 1
    mov byte [wd_clip], 1
    mov byte [wd_clean], 1
    mov byte [wd_resume], 0
    mov ax, [wd_vrows]
    mov [wd_lastrow], ax
    call wd_walk
    mov byte [wd_clip], 0
    mov byte [wd_clean], 0
    jmp short .done

.tail:
    ; the sliver under the last whole row, which after a move DOWN holds a
    ; slice of the row that used to be there
    push cx
    mov ax, [wd_vrows]
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_ty]
    cmp ax, [wd_bot]
    ja .done
    mov bx, ax                  ; y1
    mov dx, [wd_bot]            ; y2
    mov ax, [wd_tx]
    sub ax, WD_MARGIN
    mov cx, [wd_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL

.done:
    push cx                     ; the blit moved every row's pixels by DI:
    push si                     ; keep the banked ys truthful (SPEC.md 65.6) -
    mov ax, di                  ; unread while uniform, but the bank must not
    mov cx, [wd_vrows]          ; go stale across a later 0 -> 1 transition
    jcxz .noadj
    mov si, wd_ryb
.adj:
    add [si], ax
    add si, 2
    loop .adj
.noadj:
    pop si
    pop cx
    call wd_sbar                ; unconditional: the track changed height, and
                                ; the blit reached into the bar's columns
    mov bx, si
    call OSAPI_WM_GROW          ; ...and the corner it sits in
    call wd_sigmark             ; the signatures describe THIS geometry now,
    mov ax, [wd_top]            ; and [wd_gchg] is spent with them
    mov [wd_ptop], ax
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

wd_redrawall:
    call wd_bounds              ; the NEW geometry, so wd_panmove can compare
    call wd_panmove             ; it against what the signatures were taken at
    jnc .out                    ; (SPEC.md 27.10.2): the panel only moves the
                                ; text, so MOVE it rather than draw it again
    mov byte [wd_sigok], 0
    mov word [wd_prowi], 0xFFFF
    mov byte [wd_ckok], 0
    mov byte [wd_rowsok], 0
    call wd_redraw
.out:
    ret

; =============================================================================
; Small change (SPEC.md 27.8/27.10)
; =============================================================================

; wd_saymsg - AX = a NUL string -> the system toast (SPEC.md 59).
; Preserves all registers AND the flags: two callers are error paths that
; carry their answer in CF.
;
; This was six words of state and a drawing routine of its own - [wd_msg],
; [wd_msgn]'s generation counter, four box coordinates, wd_toast, the
; wd_smsg/wd_smsgn shadow and wd_sigsame's two tests of it. The toast is in
; the MENU BAR now, so it is in none of this app's pixels: it survives a
; repaint without help, it cannot be carried off its frame by a scroll blit,
; and it cannot leave the incremental path disagreeing with W_PAINT - which
; is what forced a FULL content repaint on the first keystroke after every
; save and every load (SPEC.md 59.1).
wd_saymsg:
    push ax
    push cx
    push si
    push es
    pushf
    mov si, ax
    push ds
    pop es                      ; the kernel COPIES the string, so wd_tbuf may
    xor cx, cx                  ; be reused freely and this app may close
    call OSAPI_TOAST            ; while the message is still up. CX = 0: the
    popf                        ; default lifetime, about three seconds
    pop es
    pop si
    pop cx
    pop ax
    ret

; wd_utoa - AX as decimal at DI, no leading zeros; DI advances past it.
; Preserves every other register.
wd_utoa:
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

; wd_saycnt - "<AX> replaced" as the toast. Preserves all registers.
wd_saycnt:
    push ax
    push si
    push di
    mov di, wd_tbuf
    call wd_utoa
    mov si, wd_m_repld
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .cp
    mov ax, wd_tbuf
    call wd_saymsg
    pop di
    pop si
    pop ax
    ret

; =============================================================================
; The Word chrome (SPEC.md 65.2): the nine-title menu bar and its dropdowns,
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
; One geometry, banked: wd_mgeo computes the open dropdown's rectangle once
; into wd_mrect, and the painter (wd_mdraw), the hit test (wd_mfind), the
; highlight (wd_mhl) and the close repaint (wd_mrepair) all read those four
; words - the fm_hit discipline (SPEC.md 22). Closing a dropdown repaints
; exactly what it covered: the strips it overlapped, the find-panel band, the
; text ROWS under it (one fill + one clipped walk), the scroll bar and the
; status line - never the whole window. The ribbon/ruler combos reuse the
; whole machinery as one-entry pseudo-menus.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_mgeti - AL = menu index -> BX = its wd_mtab descriptor
; preserves everything else
; -----------------------------------------------------------------------------
wd_mgeti:
    push ax
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov bx, ax
    add bx, wd_mtab
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mitemp - AL = item index, BX = descriptor -> SI = the 8-byte item record
; preserves everything else
; -----------------------------------------------------------------------------
wd_mitemp:
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
; wd_mact - AL = item index of the OPEN menu -> DL = its action byte
; preserves everything else
; -----------------------------------------------------------------------------
wd_mact:
    push ax
    push bx
    push si
    mov [wd_picki], al              ; WHICH item, and out of WHICH menu: the
    mov ah, [wd_mopen]              ; action byte alone cannot tell a combo's
    mov [wd_pickm], ah              ; third entry from its first, and the Font
    mov ah, al                      ; combo is the first menu here that cares
    mov al, [wd_mopen]
    call wd_mgeti
    mov al, ah
    call wd_mitemp
    mov dl, [si+2]
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_wfit - does a chrome element ending at content-relative x AX fit the
;           window? out: CF=1 it does not; preserves all registers
; The strips guard every group with this so a narrow window drops trailing
; groups instead of drawing past its own frame onto the desktop.
; -----------------------------------------------------------------------------
wd_wfit:
    push ax
    add ax, 2
    cmp ax, [wd_cw]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wd_mbar - the menu bar strip: nine titles, ONE opaque run (SPEC.md 65.2)
; in:  wd_bounds run, gfx lock held; out: nothing; preserves all registers
;
; The titles are one 56-cell string drawn with one font_run at cl+8 - the
; content origin is snapped (SPEC.md 11.94), so the run rides the single-
; store path - plus a 1px mnemonic underline per fully visible title and the
; rule under the strip. A window too narrow for the bar draws a truncated
; copy rather than lettering past its own edge.
; -----------------------------------------------------------------------------
wd_mbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, [wd_ct]
    mov cx, ax
    add cx, [wd_cw]
    dec cx
    mov dx, bx
    add dx, WD_MENU_H-2
    call OSAPI_GFX_FILL             ; the strip's ground
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, cx
    mov dx, [wd_ct]
    add dx, WD_MENU_H-1
    call OSAPI_GFX_HLINE            ; the rule under the bar
    mov si, wd_s_mbar
    mov ax, [wd_cw]
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
    mov di, wd_mbbuf
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
    mov si, wd_mbbuf
.whole:
    mov cx, [wd_cl]
    add cx, 8
    mov dx, [wd_ct]
    add dx, 3
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.under:
    mov si, wd_mtab                 ; the mnemonic underlines
    mov di, WD_M_N
.mn:
    mov al, [si]
    add al, [si+1]
    inc al
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp ax, [wd_cw]                 ; (start+len+1)*8 <= cw = fully visible
    ja .mnnext
    mov al, [si]
    add al, [si+2]                  ; start + the title's mnemonic index
    xor ah, ah
    shl ax, cl
    add ax, [wd_cl]
    add ax, 8
    mov bx, ax
    add bx, 6
    mov dx, [wd_ct]
    add dx, 11
    call OSAPI_GFX_HLINE
.mnnext:
    add si, 8
    dec di
    jnz .mn
    mov al, [wd_mopen]              ; a bar repaint under an open dropdown
    cmp al, WD_M_N                  ; keeps the title inverted
    jae .out
    call wd_mtxor
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mtxor - invert a bar title's band (XOR: calling it again un-inverts)
; in:  AL = menu index 0..8, wd_bounds run; preserves all registers
; -----------------------------------------------------------------------------
wd_mtxor:
    push ax
    push bx
    push cx
    push dx
    call wd_mgeti
    mov al, [bx]
    add al, [bx+1]
    inc al
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp ax, [wd_cw]
    ja .out                         ; off a narrow window's bar: nothing shown
    mov al, [bx]
    xor ah, ah
    shl ax, cl
    add ax, [wd_cl]
    add ax, 8-2                     ; 2px into the leading space cell
    mov dl, [bx+1]
    xor dh, dh
    shl dx, cl
    mov cx, ax
    add cx, dx
    add cx, 3                       ; ...and 2px into the trailing one
    mov bx, [wd_ct]
    inc bx
    mov dx, [wd_ct]
    add dx, 12
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mtitler - AL = menu 0..8: bank its bar band as the gesture anchor
; out: [wd_mabox] = {x1,y1,x2,y2}; preserves all registers
; The anchor is what a release/click is tested against for the "stay open"
; and "toggle closed" answers - one rect for titles and combo boxes alike.
; -----------------------------------------------------------------------------
wd_mtitler:
    push ax
    push bx
    push cx
    push dx
    call wd_mgeti
    mov al, [bx]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [wd_cl]
    add ax, 8-2
    mov [wd_mabox], ax
    mov dl, [bx+1]
    xor dh, dh
    shl dx, cl
    add ax, dx
    add ax, 3
    mov [wd_mabox+4], ax
    mov ax, [wd_ct]
    mov [wd_mabox+2], ax
    add ax, WD_MENU_H-1
    mov [wd_mabox+6], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mchk - is the checkable item with action AL currently checked?
; out: CF=1 checked; preserves all registers
; Draft and the one document window are always checked (they are the mode and
; the window); the three View strips answer from their toggles.
; -----------------------------------------------------------------------------
wd_mchk:
    cmp al, WDA_DRAFT               ; Draft and Page are the two views and
    jne .npg                        ; exactly one of them is checked
    cmp byte [wd_vpage], 0
    je .yes
    jmp short .no
.npg:
    cmp al, WDA_PAGE
    jne .nwin
    cmp byte [wd_vpage], 0
    jne .yes
    jmp short .no
.nwin:
    cmp al, WDA_WIN1
    je .yes
    cmp al, WDA_VRIB
    jne .n1
    cmp byte [wd_vrib], 0
    jne .yes
    jmp short .no
.n1:
    cmp al, WDA_VRUL
    jne .n2
    cmp byte [wd_vrul], 0
    jne .yes
    jmp short .no
.n2:
    cmp al, WDA_VSTA
    jne .no
    cmp byte [wd_vsta], 0
    jne .yes
.no:
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; wd_mgeo - compute the open dropdown's rectangle into wd_mrect
; in:  AL = menu index; bar menus anchor under their title, pseudo-menus
;      (the combos) at [wd_max]/[wd_may] which the click site set
; out: wd_mrect = {x1,y1,x2,y2}; preserves all registers
; The height is summed from the items (10px bands, 5px separators, 2px pad
; top and bottom inside the frame); the rect is clamped INSIDE the content -
; gfx primitives draw wherever they are pointed, and a dropdown must never
; scribble the window frame or the desktop below it.
; -----------------------------------------------------------------------------
wd_mgeo:
    push ax
    push bx
    push cx
    push dx
    push si
    call wd_mgeti
    cmp al, WD_M_N
    jae .combo
    mov dl, [bx]
    xor dh, dh
    mov cl, 3
    shl dx, cl
    add dx, [wd_cl]
    add dx, 8-4                     ; the panel starts 4px left of the title
    mov [wd_mrx1], dx
    mov dx, [wd_ct]
    add dx, WD_MENU_H
    mov [wd_mry1], dx
    jmp short .size
.combo:
    mov dx, [wd_max]
    mov [wd_mrx1], dx
    mov dx, [wd_may]
    mov [wd_mry1], dx
.size:
    push ax                         ; the menu index - the height loop below
                                    ; eats AL and the flip needs it back
    mov cl, [bx+3]                  ; height: 4 + 10/5 per item
    xor ch, ch
    mov si, [bx+4]
    mov ax, 4
.hh:
    jcxz .hd
    test byte [si], WDMF_SEP
    jz .h10
    add ax, WD_MS_HGT
    jmp short .hn
.h10:
    add ax, WD_MI_HGT
.hn:
    add si, 8
    dec cx
    jmp short .hh
.hd:
    pop cx                          ; CL = the menu index again
    mov si, ax                      ; SI = the panel's height in pixels
    mov dx, [wd_ct]
    add dx, [wd_ch]
    sub dx, 2                       ; leave the shadow's pixel inside too
    add ax, [wd_mry1]
    dec ax
    cmp ax, dx
    jbe .yok
    cmp cl, WD_M_N
    jb .clip                        ; a BAR menu hangs under its own title and
                                    ; nowhere else - moving one would put it
                                    ; over the menu bar it came from
    ; --- the SLIDE (SPEC.md 6.4.1). A combo's list is anchored at its box, and
    ; with ten faces in FONTS/ eleven 10px items stand 114 rows tall - past the
    ; bottom of a 200-row CGA window by a few pixels. Clipping is what used to
    ; happen and it is SILENT: the trailing faces are drawn nowhere and
    ; wd_mfind refuses to find them, so a disk carrying more faces than the
    ; anchor has room below it simply loses the last ones, which is how
    ; WD_MAXFONT came to be 6. Sliding the panel up until its bottom sits on
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
    sub cx, [wd_ct]                 ; CX = the rows the content has for it
    cmp cx, si
    jb .clip                        ; taller than the whole content: nothing
                                    ; to slide into, so clip as before
    mov ax, dx
    sub ax, si
    inc ax                          ; the top that puts its bottom on DX
    mov [wd_mry1], ax
    mov ax, dx                      ; ...and that bottom is the limit itself
    jmp short .yok
.clip:
    mov ax, dx                      ; clipped: trailing items are not drawn
.yok:                               ; and wd_mfind stops at the same edge
    mov [wd_mry2], ax
    mov ax, [bx+6]                  ; the width the table precomputed
    mov dx, [wd_mrx1]
    add dx, ax
    dec dx
    mov cx, [wd_cl]
    add cx, [wd_cw]
    sub cx, 2
    cmp dx, cx
    jbe .xok
    mov dx, cx                      ; ride the right edge (Help lives there)
    push dx
    sub dx, ax
    inc dx
    mov cx, [wd_cl]
    inc cx
    cmp dx, cx
    jae .x1ok
    mov dx, cx
.x1ok:
    mov [wd_mrx1], dx
    pop dx
.xok:
    mov [wd_mrx2], dx
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mdraw - draw the open dropdown from wd_mrect (SPEC.md 65.2)
; in:  [wd_mopen], wd_mrect computed, gfx lock held; preserves all registers
;
; White panel, 1px black frame, 1px grey drop shadow right and bottom
; (GFX_FILL_GRAY: a 50% dither, so it reads grey on every adapter). Items
; 10px apart with the text at x1+8; separators are hlines; captions right-
; justified; disabled items drawn whole - label and caption - under the
; disabled pen so they dither at 1bpp (SPEC.md 47); checked items get a two-
; line check at x1+2; enabled mnemonics a 1px underline.
; -----------------------------------------------------------------------------
wd_mdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wd_mrx1]
    mov bx, [wd_mry1]
    mov cx, [wd_mrx2]
    mov dx, [wd_mry2]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN              ; live pen: CBLACK, dither flag clear
    call OSAPI_GFX_FRAME
    mov ax, [wd_mrx2]               ; the drop shadow: right edge...
    inc ax
    mov bx, [wd_mry1]
    inc bx
    mov cx, ax
    mov dx, [wd_mry2]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [wd_mrx1]               ; ...and bottom edge
    inc ax
    mov bx, [wd_mry2]
    inc bx
    mov cx, [wd_mrx2]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    ; the items
    mov al, [wd_mopen]
    call wd_mgeti
    mov cl, [bx+3]
    xor ch, ch
    mov si, [bx+4]
    mov di, [wd_mry1]
    add di, 2
.item:
    or cx, cx
    jnz .live
    jmp .done                       ; out of the short branch's reach
.live:
    test byte [si], WDMF_SEP
    jz .norm
    mov ax, di                      ; a separator: one hline mid-band
    add ax, WD_MS_HGT-1
    cmp ax, [wd_mry2]
    jae .done
    mov ax, [wd_mrx1]
    inc ax
    mov bx, [wd_mrx2]
    dec bx
    mov dx, di
    add dx, 2
    call OSAPI_GFX_HLINE
    add di, WD_MS_HGT
    jmp .next
.norm:
    mov ax, di
    add ax, WD_MI_HGT-1
    cmp ax, [wd_mry2]
    jae .done                       ; clipped by a short window: stop
    push cx
    test byte [si], WDMF_DIS
    jnz .dis
    clc
    jmp short .pen
.dis:
    stc
.pen:
    call OSAPI_GFX_PEN              ; CF IS the argument (SPEC.md 47)
    ; the check column
    test byte [si], WDMF_CHK
    jz .nochk
    mov al, [si+2]
    call wd_mchk
    jnc .nochk
    mov ax, [wd_mrx1]
    add ax, 2
    mov bx, di
    add bx, 5
    mov cx, [wd_mrx1]
    add cx, 3
    mov dx, di
    add dx, 7
    push si
    xor si, si
    call OSAPI_GFX_LINE             ; the check's short down-stroke...
    mov ax, cx
    mov bx, dx
    mov cx, [wd_mrx1]
    add cx, 7
    mov dx, di
    add dx, 3
    call OSAPI_GFX_LINE             ; ...and its long up-stroke
    pop si
.nochk:
    ; the label
    mov cx, [wd_mrx1]
    add cx, 8
    mov dx, di
    inc dx
    push si
    mov si, [si+4]
    call OSAPI_FONT_STR
    pop si
    ; the mnemonic underline (enabled items only: a grey line rounds to
    ; solid black at 1bpp and a greyed mnemonic answers no key anyway)
    test byte [si], WDMF_DIS
    jnz .nomn
    cmp byte [si+3], 0
    je .nomn
    mov al, [si+1]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [wd_mrx1]
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
    call OSAPI_FONT_WIDTH
    mov cx, [wd_mrx2]
    sub cx, 6
    sub cx, ax
    mov dx, di
    inc dx
    call OSAPI_FONT_STR
    pop si
.nocap:
    clc
    call OSAPI_GFX_PEN              ; pen back live before the next item
    pop cx
    add di, WD_MI_HGT
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
; wd_mfind - which ENABLED item is the point on?
; in:  CX = x, DX = y (abs), the dropdown open
; out: AL = item index, or 0xFF (outside, a separator, a disabled item, or a
;      clipped one); preserves everything else
; -----------------------------------------------------------------------------
wd_mfind:
    push bx
    push cx
    push dx
    push si
    push di
    cmp cx, [wd_mrx1]
    jb .miss
    cmp cx, [wd_mrx2]
    ja .miss
    cmp dx, [wd_mry1]
    jb .miss
    cmp dx, [wd_mry2]
    ja .miss
    mov al, [wd_mopen]
    call wd_mgeti
    mov cl, [bx+3]
    xor ch, ch
    mov si, [bx+4]
    mov di, [wd_mry1]
    add di, 2
    cmp dx, di
    jb .miss                        ; in the top pad
    xor bx, bx                      ; BL = index
.scan:
    jcxz .miss
    test byte [si], WDMF_SEP
    jnz .sep
    mov ax, di
    add ax, WD_MI_HGT-1
    cmp ax, [wd_mry2]
    jae .miss                       ; this item is clipped: so is the rest
    cmp dx, ax
    ja .below
    test byte [si], WDMF_DIS        ; found the band
    jnz .miss
    mov al, bl
    jmp short .out
.below:
    add di, WD_MI_HGT
    jmp short .adv
.sep:
    mov ax, di
    add ax, WD_MS_HGT-1
    cmp dx, ax
    jbe .miss                       ; a separator answers nothing
    add di, WD_MS_HGT
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
; wd_mhl - XOR the highlight band of item AL (0xFF = nothing to do)
; XOR is its own inverse, so on and off are the same call - the banked-
; coordinate idiom the drag markers already use (SPEC.md 27.8).
; -----------------------------------------------------------------------------
wd_mhl:
    cmp al, 0xFF
    je .no
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, al                      ; DL = target index
    mov al, [wd_mopen]
    call wd_mgeti
    mov si, [bx+4]
    mov di, [wd_mry1]
    add di, 2
    xor cx, cx
.w:
    cmp cl, dl
    je .found
    test byte [si], WDMF_SEP
    jz .i10
    add di, WD_MS_HGT
    jmp short .n
.i10:
    add di, WD_MI_HGT
.n:
    add si, 8
    inc cx
    jmp short .w
.found:
    mov ax, di
    add ax, WD_MI_HGT-1
    cmp ax, [wd_mry2]
    jae .done                       ; clipped: it was not drawn either
    mov dx, ax
    mov ax, [wd_mrx1]
    inc ax
    mov bx, di
    mov cx, [wd_mrx2]
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
; wd_mbarhit - CX/DX = a point: which bar title is it on?
; out: AL = 0..8 or 0xFF; preserves everything else
; -----------------------------------------------------------------------------
wd_mbarhit:
    push bx
    push cx
    push dx
    push si
    mov ax, [wd_ct]
    cmp dx, ax
    jb .no
    add ax, WD_MENU_H-1
    cmp dx, ax
    ja .no
    mov ax, cx
    sub ax, [wd_cl]
    sub ax, 8
    js .no
    mov cl, 3
    shr ax, cl                      ; AL = the cell the click is in (AH = 0)
    mov si, wd_mtab
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
    cmp ax, [wd_cw]
    pop ax
    ja .next
    mov al, bl
    jmp short .out
.next:
    add si, 8
    inc bx
    cmp bl, WD_M_N
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
; wd_mopenm - open menu AL: bank state, invert the title, draw the dropdown
; in:  AL = menu index, wd_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
wd_mopenm:
    push ax
    push cx
    push si
    push di
    mov [wd_mopen], al
    mov byte [wd_mhi], 0xFF
    cmp al, 7                       ; the Window menu shows the live document:
    jne .now                        ; compose '1 ' + wd_name into its item
    mov word [wd_win1], '1 '
    mov si, wd_name
    mov di, wd_win1+2
    mov cx, 13
.cp:
    mov ah, [si]
    mov [di], ah
    inc si
    inc di
    or ah, ah
    loopnz .cp
    mov byte [wd_win1+15], 0
.now:
    call wd_mgeo
    cmp al, WD_M_N
    jae .noxor
    call wd_mtxor
.noxor:
    call wd_mdraw
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mclose - take the open dropdown down and repaint what it covered
; in:  SI = window ptr, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
wd_mclose:
    push ax
    mov al, [wd_mopen]
    cmp al, WD_M_NONE
    je .out
    cmp al, WD_M_N
    jae .noxor
    call wd_mtxor                   ; the title back to normal video
.noxor:
    mov byte [wd_mopen], WD_M_NONE
    mov byte [wd_mhi], 0xFF
    call wd_mrepair
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mrepair - repaint exactly what a dropdown (or the About box) covered
; in:  SI = window ptr, wd_mrect = the covered rect, wd_bounds run, lock held
; out: nothing; preserves all registers; wd_mrect is consumed (grown by the
;      shadow and clamped)
;
; Piecewise, priced by what the rect touches: the ribbon and ruler strips are
; repainted whole (bounded call counts); the margin band above the text is
; refilled; the text rows are erased FULL WIDTH and re-lettered by one
; clipped walk seeded at the first covered row - row-granular, the band-blit
; idiom (SPEC.md 65.2); the bar, status line and grow box only when reached.
; -----------------------------------------------------------------------------
wd_mrepair:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [wd_mrx2]               ; the shadow is part of what was covered
    inc ax
    mov dx, [wd_cl]
    add dx, [wd_cw]
    dec dx
    cmp ax, dx
    jbe .x2ok
    mov ax, dx
.x2ok:
    mov [wd_mrx2], ax
    mov ax, [wd_mry2]
    inc ax
    mov dx, [wd_ct]
    add dx, [wd_ch]
    dec dx
    cmp ax, dx
    jbe .y2ok
    mov ax, dx
.y2ok:
    mov [wd_mry2], ax
    ; the menu bar strip (a modal dialog may cover it - SPEC.md 65.3)
    mov ax, [wd_ct]
    mov dx, ax
    add dx, WD_MENU_H-1
    cmp [wd_mry1], dx
    ja .nombar
    cmp [wd_mry2], ax
    jb .nombar
    call wd_mbar
.nombar:
    ; the ribbon strip
    cmp byte [wd_vrib], 0
    je .norib
    mov ax, [wd_ct]
    add ax, WD_MENU_H
    mov dx, ax
    add dx, WD_RIBBON_H-1
    cmp [wd_mry1], dx
    ja .norib
    cmp [wd_mry2], ax
    jb .norib
    call wd_ribbon
.norib:
    ; the ruler strip
    cmp byte [wd_vrul], 0
    je .norul
    call wd_ruly
    mov dx, ax
    add dx, WD_RULER_H-1
    cmp [wd_mry1], dx
    ja .norul
    cmp [wd_mry2], ax
    jb .norul
    call wd_ruler
.norul:
    ; the band between the chrome and the text top: the margin gap
    mov ax, [wd_ct]
    add ax, [wd_ctop]
    mov dx, [wd_ty]
    dec dx
    cmp [wd_mry1], dx
    ja .noband
    cmp [wd_mry2], ax
    jb .noband
    mov bx, [wd_mry1]
    cmp bx, ax
    jae .b1
    mov bx, ax
.b1:
    cmp dx, [wd_mry2]
    jbe .b2
    mov dx, [wd_mry2]
.b2:
    push bx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop dx
    pop bx
    mov ax, [wd_mrx1]
    mov cx, [wd_mrx2]
    call OSAPI_GFX_FILL
.noband:
    ; the text rows
    mov ax, [wd_ty]
    cmp [wd_mry2], ax
    jb .notext
    mov dx, [wd_bot]
    cmp [wd_mry1], dx
    ja .notext
    mov dx, [wd_mry1]
    cmp dx, ax
    jae .r0a
    mov dx, ax
.r0a:
    call wd_yrow                    ; DI = r0, the first covered row, found
    jnc .r0b                        ; from the banked ys (SPEC.md 65.6) -
    xor ax, ax                      ; refused, it is the whole band: slow and
.r0b:                               ; never wrong
    mov di, ax
    ; y1 = r0's band top: wd_ty for row 0, else ryb[r0-1] + 8
    or ax, ax
    jz .y1ty
    cmp byte [wd_hasfmt], 0
    jne .y1fmt
    mov cl, 3
    shl ax, cl
    add ax, [wd_ty]
    mov bx, ax
    jmp short .y1have
.y1fmt:
    mov bx, ax
    dec bx
    shl bx, 1
    mov bx, [bx+wd_ryb]
    add bx, 8
    jmp short .y1have
.y1ty:
    mov bx, [wd_ty]
.y1have:
    mov dx, [wd_mry2]
    cmp dx, [wd_bot]
    jbe .yla
    mov dx, [wd_bot]
.yla:
    push dx                         ; the erase's bottom edge
    ; erase the covered rows FULL WIDTH: the walk's opaque runs then reletter
    ; them; below the last document row the fill alone is the repaint
    mov ax, [wd_tx]
    sub ax, WD_MARGIN
    mov cx, [wd_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop dx
    ; r1 = the last covered row that exists on screen
    push di
    call wd_yrow
    mov di, ax
    jnc .r1have
    mov di, [wd_vrows]              ; refused: to the view's last row
    dec di
.r1have:
    mov dx, di
    pop di
    mov ax, [wd_vrows]
    or ax, ax
    jz .notext
    dec ax
    cmp dx, ax
    jbe .r1ok
    mov dx, ax
.r1ok:
    cmp di, dx
    ja .notext                      ; covered only the blank space below
    mov word [wd_prowi], 0xFFFF     ; the fill erased the delta cache's row
    mov [wd_dr0], di
    mov [wd_dr1], dx
    mov word [wd_hity], 0xFFFF
    mov word [wd_wanty], 0x7FFF
    mov byte [wd_draw], 1
    mov byte [wd_sigup], 1
    mov byte [wd_clip], 1
    mov byte [wd_clean], 1
    mov byte [wd_resume], 0
    mov ax, di
    call wd_seedrow                 ; seed at r0, bound at r1 (DX)
    cmp byte [wd_resume], 0
    jne .seeded
    mov ax, [wd_top]                ; wd_rows stale: the row index still
    push dx                         ; describes the view (SPEC.md 27.13)
    mov dx, [wd_vrows]
    call wd_xseed
    pop dx
.seeded:
    mov [wd_lastrow], dx
    call wd_walk
    mov byte [wd_clip], 0
    mov byte [wd_clean], 0
    mov byte [wd_resume], 0
.notext:
    ; the scroll bar's columns
    mov ax, [wd_rgt]
    cmp [wd_mrx2], ax
    jbe .nobar
    mov ax, [wd_ty]
    cmp [wd_mry2], ax
    jb .nobar
    call wd_sbar
.nobar:
    ; the status strip
    cmp byte [wd_vsta], 0
    je .nosta
    mov ax, [wd_bot]
    cmp [wd_mry2], ax
    jbe .nosta
    call wd_status
.nosta:
    ; the grow box corner
    mov ax, [wd_sbb]
    cmp [wd_mry2], ax
    jbe .nogrow
    mov ax, [wd_rgt]
    cmp [wd_mrx2], ax
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
; wd_ruly - AX = the ruler strip's top row (valid when the ruler is shown)
; preserves everything else
; -----------------------------------------------------------------------------
wd_ruly:
    mov ax, [wd_ct]
    add ax, WD_MENU_H
    cmp byte [wd_vrib], 0
    je .out
    add ax, WD_RIBBON_H
.out:
    ret

; -----------------------------------------------------------------------------
; wd_combo - one combo box: frame, shown text, divider, drop-down arrow
; in:  AX = x1 (abs), BX = y1 (abs), CX = width, SI = the shown text
;      gfx lock held; out: nothing; preserves all registers
; The text sits at x1+8, which every caller keeps on a byte column.
; -----------------------------------------------------------------------------
wd_combo:
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
    call OSAPI_FONT_STR
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_btn12 - one 12x12 bordered toggle cell, optionally lettered
; in:  AX = x, BX = y (abs), CL = the letter (0 = none); preserves all
; -----------------------------------------------------------------------------
wd_btn12:
    push ax
    push bx
    push cx
    push dx
    push cx
    mov cx, ax
    add cx, WD_BTN_W-1
    mov dx, bx
    add dx, WD_BTN_W-1
    call OSAPI_GFX_FRAME
    pop cx
    or cl, cl
    jz .out
    push ax
    mov al, cl
    pop cx
    add cx, 2
    mov dx, bx
    add dx, 2
    call OSAPI_FONT_CHAR
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_ribbon - the ribbon strip (SPEC.md 65.2), per ibdefs.h's order:
;             Font:/Pts: combos, B I K | U W D | super/sub pair | pilcrow.
; in:  wd_bounds run, gfx lock held; preserves all registers
; The buttons are drawn and hit-tested but inert this stage - pressed-state
; rendering arrives with character formatting.
; -----------------------------------------------------------------------------
wd_ribbon:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [wd_vrib], 0
    je .out
    mov di, [wd_ct]
    add di, WD_MENU_H               ; DI = the strip's top
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, di
    mov cx, ax
    add cx, [wd_cw]
    dec cx
    mov dx, di
    add dx, WD_RIBBON_H-2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, cx
    mov dx, di
    add dx, WD_RIBBON_H-1
    call OSAPI_GFX_HLINE            ; the rule under the ribbon
    ; Font: and its combo
    mov ax, WD_RB_FBX + WD_RB_FBW - 1
    call wd_wfit
    jc .btns
    mov cx, [wd_cl]
    add cx, WD_RB_FLBL
    mov dx, di
    add dx, 4
    mov si, wd_s_font
    call OSAPI_FONT_STR
    mov ax, [wd_cl]
    add ax, WD_RB_FBX
    mov bx, di
    add bx, 2
    mov cx, WD_RB_FBW
    mov si, [wd_fcap]               ; the face the document is set in, which
    call wd_combo                   ; is wd_s_pica until one is chosen
    ; Pts: and its combo
    mov ax, WD_RB_PBX + WD_RB_PBW - 1
    call wd_wfit
    jc .btns
    mov cx, [wd_cl]
    add cx, WD_RB_PLBL
    mov dx, di
    add dx, 4
    mov si, wd_s_pts
    call OSAPI_FONT_STR
    mov ax, [wd_cl]
    add ax, WD_RB_PBX
    mov bx, di
    add bx, 2
    mov cx, WD_RB_PBW
    mov si, wd_s_10
    call wd_combo
.btns:
    mov ax, WD_RB_B1 + 2*WD_BTN_P + WD_BTN_W - 1
    call wd_wfit
    jc .pilc
    mov bx, di
    add bx, 2
    mov ax, [wd_cl]
    add ax, WD_RB_B1
    mov cl, 'B'
    call wd_btn12
    add ax, WD_BTN_P
    mov cl, 'I'
    call wd_btn12
    add ax, WD_BTN_P
    mov cl, 'K'                     ; Small Kaps - the ribbon bitmap's letter,
    call wd_btn12                   ; and char.des's literal '&Kaps' spelling
    mov ax, WD_RB_B2 + 2*WD_BTN_P + WD_BTN_W - 1
    call wd_wfit
    jc .pilc
    mov ax, [wd_cl]
    add ax, WD_RB_B2
    mov cl, 'U'
    call wd_btn12
    add ax, WD_BTN_P
    mov cl, 'W'
    call wd_btn12
    add ax, WD_BTN_P
    mov cl, 'D'
    call wd_btn12
    ; the stacked superscript/subscript pair (ScrptPos): a raised and a
    ; dropped mark in one cell's two halves - GREYED whole (SPEC.md 47/65.3):
    ; super/subscript does not exist in draft view's one cell height
    mov ax, WD_RB_SS + WD_BTN_W - 1
    call wd_wfit
    jc .pilc
    stc
    call OSAPI_GFX_PEN
    mov ax, [wd_cl]
    add ax, WD_RB_SS
    mov bx, di
    add bx, 2
    xor cl, cl
    call wd_btn12
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
    mov ax, [wd_cw]
    cmp ax, WD_RB_SS + WD_BTN_W + 20
    jb .sync
    mov ax, [wd_cl]
    add ax, [wd_cw]
    sub ax, 4 + WD_BTN_W
    mov bx, di
    add bx, 2
    xor cl, cl
    call wd_btn12
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
    mov byte [wd_rbold], 0          ; every cell was just drawn UNPRESSED:
    mov byte [wd_rbok], 1           ; re-invert the ones the caret's attrs
    call wd_rbstat                  ; say are on (SPEC.md 65.3)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rbxy - where ribbon toggle cell AL sits (0..5 = B I K U W D, 6 = the
;           pilcrow); out: CF=1 the window is too narrow and the cell is NOT
;           drawn, else AX = its x, BX = its y. Preserves everything else.
; One geometry for the painter's XOR, the delta update and the hit test -
; the fm_hit discipline (SPEC.md 22).
; -----------------------------------------------------------------------------
wd_rbxy:
    push cx
    push dx
    mov dl, al
    cmp dl, 6
    je .pil
    cmp dl, 3
    jae .g2
    mov ax, WD_RB_B1 + 2*WD_BTN_P + WD_BTN_W - 1
    call wd_wfit
    jc .no
    mov al, dl
    xor ah, ah
    mov cx, WD_BTN_P
    mul cx
    add ax, WD_RB_B1
    jmp short .have
.g2:
    mov ax, WD_RB_B2 + 2*WD_BTN_P + WD_BTN_W - 1
    call wd_wfit
    jc .no
    mov al, dl
    sub al, 3
    xor ah, ah
    mov cx, WD_BTN_P
    mul cx
    add ax, WD_RB_B2
    jmp short .have
.pil:
    mov ax, [wd_cw]
    cmp ax, WD_RB_SS + WD_BTN_W + 20
    jb .no
    mov ax, [wd_cw]
    sub ax, 4 + WD_BTN_W
.have:
    add ax, [wd_cl]
    mov bx, [wd_ct]
    add bx, WD_MENU_H + 2
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    ret

; the six toggle cells' attribute bits, B I K U W D order, then the pilcrow
wd_rbmask: db WDAT_BOLD, WDAT_ITAL, WDAT_SCAP, WDAT_UL, WDAT_WUL, WDAT_DUL
           db 0x80

; -----------------------------------------------------------------------------
; wd_rbstat - the ribbon's pressed states, delta-cached (SPEC.md 65.3)
; in:  wd_bounds run, gfx lock held; preserves all registers
; Reads the display attrs (caret's, or the selection's first character) plus
; the Show-all toggle in bit 7, compares against what the cells show, and
; XOR-inverts ONLY the cells whose state changed - a caret move that changes
; nothing draws nothing.
; -----------------------------------------------------------------------------
wd_rbstat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [wd_vrib], 0
    je .out
    cmp byte [wd_rbok], 0
    je .out
    cmp byte [wd_mopen], WD_M_NONE
    jne .out                        ; a dropdown may cover the strip
    cmp byte [wd_about], 0
    jne .out
    cmp word [wd_dlg], 0
    jne .out
    call wd_dispattr
    cmp byte [wd_showall], 0
    je .nosa
    or al, 0x80
.nosa:
    mov ah, [wd_rbold]
    cmp al, ah
    je .out
    mov [wd_rbold], al
    xor ah, al                      ; AH = the bits that changed
    xor cx, cx                      ; CL = the cell index
.cell:
    mov bx, cx
    test ah, [wd_rbmask+bx]
    jz .next
    push ax
    push cx
    mov al, cl
    call wd_rbxy                    ; AX/BX = the cell, or CF on a narrow
    jc .undrawn                     ; window where it is not on screen
    push ax
    mov cx, ax
    add cx, WD_BTN_W-2              ; the interior: the frame stays upright
    mov dx, bx
    add dx, WD_BTN_W-2
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
; wd_rmkd / wd_rmku - a small down/up-pointing indent marker
; in:  AX = centre x, DX = top row (3 rows tall); preserves all registers
; -----------------------------------------------------------------------------
wd_rmkd:
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
wd_rmku:
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
; wd_pgcol - in Page view, narrow the text column to the sheet
; in:  AX = the column's right edge as the window would have it, [wd_tx] = its
;      left
; out: AX and [wd_tx] moved to the sheet's; [wd_shl]/[wd_shr] = its pixel
;      edges. Preserves every other register.
;
; This runs where the RIGHT EDGE is decided and before anything derives from
; it, which is the whole trick: [wd_rcols], [wd_wrgt], the wrap decision, the
; row buffer's padding and the caret's hit test all fall out of (wd_tx,
; wd_rgt) and every one of them is then right for the sheet without knowing
; a sheet exists. Narrowing [wd_rcols] alone - which is where this started -
; left the WRAP at the window's width and merely truncated each row at the
; sheet's edge, dropping the tail of every line.
;
; A window too narrow to hold the sheet keeps its own width: Page view never
; hides text that Draft would have shown.
; -----------------------------------------------------------------------------
wd_pgcol:
    cmp byte [wd_vpage], 0
    je .draft
    push bx
    push cx
    push dx
    mov bx, ax                      ; the cells the CONTENT could hold - the
    sub bx, [wd_cl]                 ; content and not the text area, so the
    inc bx                          ; slack either side of the sheet comes out
    mov cl, 3                       ; equal instead of one margin heavier on
    shr bx, cl                      ; the left
    cmp bx, WD_PGCOLS + 1
    jbe .narrow                     ; too narrow for a sheet: leave it alone
    sub bx, WD_PGCOLS               ; the slack, halved, centres it
    shr bx, 1
    shl bx, cl
    mov ax, [wd_cl]
    add ax, bx
    mov [wd_tx], ax                 ; ...and the right edge follows the width
    add ax, WD_PGCOLS * 8
    dec ax
.narrow:
    pop dx
    pop cx
    pop bx
.draft:
    push ax                         ; the sheet's edges, for wd_sheet. In
    mov ax, [wd_tx]                 ; Draft they are the column's too, and
    sub ax, WD_MARGIN               ; wd_sheet draws nothing there anyway
    mov [wd_shl], ax
    pop ax
    push ax
    add ax, WD_MARGIN
    mov [wd_shr], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_sheet - the sheet's two edges and its page marks (SPEC.md 65.11)
; in:  the geometry wd_bounds banked; gfx lock held
; out: nothing; preserves all registers. A no-op in Draft view, which is what
;      keeps this off every draft-view redraw's bill.
;
; The edges are drawn OUTSIDE the text column, in the margin either side, so a
; row flush never touches them and they survive every partial repaint - only a
; full fill or a band blit can take them, and both call this again. Two
; gfx calls in the common case: at 756us each (PERFORMANCE.md) that is ~1.5ms
; on the target, paid once per full repaint and never per keystroke.
; -----------------------------------------------------------------------------
wd_sheet:
    cmp byte [wd_vpage], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov bx, [wd_ct]                 ; the band's top and bottom
    add bx, [wd_ctop]
    mov dx, [wd_bot]
    mov ax, [wd_shl]
    call OSAPI_GFX_VLINE
    mov ax, [wd_shr]
    call OSAPI_GFX_VLINE
    call wd_pgmarks
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wd_pgmarks - a tick in each margin where a page begins
; Preserves all registers.
;
; The mark sits in the MARGIN, not across the sheet: a rule drawn inside the
; column would be erased by the next flush of the row under it, and putting it
; back would mean the row flush - the hottest path in the app (SPEC.md 65.6) -
; learning about pages. The tick says the same thing for two gfx calls a page
; and no cost at all to a keystroke. The inter-page WHITESPACE a real Page
; view shows is not drawn: it would have to come out of the layout walk's row
; advance, and that is the change SPEC.md 65.11 defers.
; -----------------------------------------------------------------------------
wd_pgmarks:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si                      ; SI = the visible row
.row:
    cmp si, [wd_vrows]
    jae .out
    mov ax, [wd_top]                ; its absolute row number
    add ax, si
    or ax, ax
    jz .next                        ; row 0 is not a page BREAK
    xor dx, dx
    mov cx, WD_PGLINES
    div cx
    or dx, dx
    jnz .next                       ; not the first row of a page
    mov bx, si                      ; its y, from the height bank
    shl bx, 1
    mov dx, [bx+wd_ryb]             ; DX = y, which is what HLINE takes
    mov ax, [wd_shl]                ; the left margin's tick
    sub ax, 4
    mov bx, [wd_shl]
    dec bx
    call OSAPI_GFX_HLINE
    mov ax, [wd_shr]                ; ...and the right margin's
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
; wd_ruler - the ruler strip (SPEC.md 65.2), per ibdefs.h's order: Style:
;            combo, alignment x4, spacing x3, closed/open space, tab type x4,
;            then the inch scale with the indent markers.
; in:  wd_bounds run, gfx lock held; preserves all registers
;
; The scale's minor ticks are ONE gfx_fill_pat: every pattern byte is 0x7F,
; so the leftmost pixel of every screen byte is black - a tick every 8px (one
; ruler cell = 1/10 inch at pica pitch), screen-aligned exactly like the
; snapped glyph cells above it. Digits land every 80px = every inch.
; -----------------------------------------------------------------------------
wd_ruler:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [wd_vrul], 0
    je .out
    call wd_ruly
    mov di, ax                      ; DI = the strip's top
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, di
    mov cx, ax
    add cx, [wd_cw]
    dec cx
    mov dx, di
    add dx, WD_RULER_H-2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, cx
    mov dx, di
    add dx, WD_RULER_H-1
    call OSAPI_GFX_HLINE            ; the rule under the ruler
    ; Style: and its combo
    mov ax, WD_RL_SBX + WD_RL_SBW - 1
    call wd_wfit
    jc .scale
    mov cx, [wd_cl]
    add cx, WD_RL_SLBL
    mov dx, di
    add dx, 4
    mov si, wd_s_style
    call OSAPI_FONT_STR
    mov ax, [wd_cl]
    add ax, WD_RL_SBX
    mov bx, di
    add bx, 2
    mov cx, WD_RL_SBW
    mov si, wd_s_normal
    call wd_combo
    ; the four alignment cells, each a 7px four-line glyph
    mov ax, WD_RL_AL + 3*WD_BTN_P + WD_BTN_W - 1
    call wd_wfit
    jc .scale
    mov si, wd_agl
    mov ax, [wd_cl]
    add ax, WD_RL_AL
    mov cx, 4
.al:
    push cx
    mov bx, di
    add bx, 2
    xor cl, cl
    call wd_btn12
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
    add ax, WD_BTN_P
    loop .al
    ; spacing: 1 / 1.5 / 2
    mov ax, WD_RL_SP2 + WD_BTN_W - 1
    call wd_wfit
    jc .scale
    mov ax, [wd_cl]
    add ax, WD_RL_SP1
    mov bx, di
    add bx, 2
    mov cl, '1'
    call wd_btn12
    mov ax, [wd_cl]
    add ax, WD_RL_SP15
    mov bx, di
    add bx, 2
    mov cx, ax
    add cx, WD_RL_SP15W-1
    mov dx, di
    add dx, 13
    call OSAPI_GFX_FRAME
    mov cx, ax
    inc cx
    mov dx, di
    add dx, 4
    mov si, wd_s_15
    call OSAPI_FONT_STR
    mov ax, [wd_cl]
    add ax, WD_RL_SP2
    mov bx, di
    add bx, 2
    mov cl, '2'
    call wd_btn12
    ; closed / open paragraph spacing: two rules tight, two rules apart
    mov ax, [wd_cl]
    add ax, WD_RL_OC
    mov bx, di
    add bx, 2
    xor cl, cl
    call wd_btn12
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
    add ax, WD_BTN_P
    mov bx, di
    add bx, 2
    xor cl, cl
    call wd_btn12
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
    ; the four tab-type cells: L C R D - GREYED whole (SPEC.md 47/65.3):
    ; custom tab stops are out of scope, every tab lands on the default
    ; stops, so a cell that picks the NEXT stop's type would be a lie
    stc
    call OSAPI_GFX_PEN
    mov si, wd_tgl
    mov ax, [wd_cl]
    add ax, WD_RL_TAB
    mov cx, 4
.tb:
    push cx
    mov bx, di
    add bx, 2
    xor cl, cl
    call wd_btn12
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
    add ax, WD_BTN_P
    loop .tb
    clc
    call OSAPI_GFX_PEN              ; the pen back live for the scale
.scale:
    ; the inch scale, with the LIVE indent markers: its own routine, because
    ; a caret move into a differently-indented paragraph redraws only it
    call wd_rlscale
    mov word [wd_rlold], 0          ; every cell was just drawn UNPRESSED:
    mov byte [wd_rlok], 1           ; re-invert the ones the caret's
    call wd_rlstat                  ; paragraph says are on (SPEC.md 65.3)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rlcalc - the ruler's state for the CARET's paragraph (SPEC.md 65.3)
; out: AX = pressed-cell bits (0..3 alignment, 4..6 spacing 1/1.5/2,
;      7 closed, 8 open); [wd_rll]/[wd_rlf]/[wd_rlr] = its left / first
;      (signed) / right indents in cells. Preserves everything else.
; -----------------------------------------------------------------------------
wd_rlcalc:
    push bx
    push cx
    push dx
    push es
    mov ax, [wd_cur]
    call wd_papat
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov es, [wd_pseg]
    mov dl, [es:bx]                 ; the packed byte
    mov al, [es:bx+1]
    mov [wd_rll], al
    mov al, [es:bx+2]
    mov [wd_rlf], al
    mov al, [es:bx+3]
    mov [wd_rlr], al
    mov cl, dl
    and cl, 3
    mov ax, 1
    shl ax, cl                      ; the alignment's bit
    mov cl, dl
    and cl, WDPA_SPACE
    shr cl, 1
    shr cl, 1
    add cl, 4                       ; the spacing's, bits 4..6
    push ax
    mov ax, 1
    shl ax, cl
    mov cx, ax
    pop ax
    or ax, cx
    test dl, WDPA_SB
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
; wd_rlxy - where ruler cell AL sits (0..3 align, 4/5/6 spacing 1/1.5/2,
;           7 closed, 8 open)
; out: CF=1 not drawn (narrow window); else AX = x, BX = y, CX = width.
; One geometry for painter, delta update and hit test (SPEC.md 22).
; -----------------------------------------------------------------------------
wd_rlxy:
    push dx
    mov dl, al
    cmp dl, 4
    jae .grp2
    mov ax, WD_RL_AL + 3*WD_BTN_P + WD_BTN_W - 1
    call wd_wfit
    jc .no
    mov al, dl
    xor ah, ah
    push cx
    mov cx, WD_BTN_P
    mul cx
    pop cx
    add ax, WD_RL_AL
    mov cx, WD_BTN_W
    jmp short .have
.grp2:
    mov ax, WD_RL_SP2 + WD_BTN_W - 1
    call wd_wfit
    jc .no
    mov cx, WD_BTN_W
    cmp dl, 4
    je .sp1
    cmp dl, 5
    je .sp15
    cmp dl, 6
    je .sp2
    cmp dl, 7
    je .oc0
    mov ax, WD_RL_OC + WD_BTN_P
    jmp short .have
.oc0:
    mov ax, WD_RL_OC
    jmp short .have
.sp1:
    mov ax, WD_RL_SP1
    jmp short .have
.sp15:
    mov ax, WD_RL_SP15
    mov cx, WD_RL_SP15W
    jmp short .have
.sp2:
    mov ax, WD_RL_SP2
.have:
    add ax, [wd_cl]
    push ax
    call wd_ruly
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

; wd_rlmclamp - keep marker centre AX inside the scale [SI-2 .. rlxe-2]
; (SI = the scale's zero, the caller's register); preserves all others
wd_rlmclamp:
    push bx
    mov bx, si
    sub bx, 2
    cmp ax, bx
    jge .lo
    mov ax, bx
.lo:
    mov bx, [wd_rlxe]
    sub bx, 2
    cmp ax, bx
    jle .out
    mov ax, bx
.out:
    pop bx
    ret

; wd_rlend - CX = the x of the text column's last pixel, which is where the
; scale stops. Preserves all other registers.
;
; The column and not the window: the scale measures the DOCUMENT, so it ends
; where the text does. In Page view that is the sheet's right edge, and the
; ruler spans the sheet exactly (SPEC.md 65.11).
wd_rlend:
    push ax
    mov cx, [wd_rcols]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [wd_tx]
    dec cx
    mov ax, [wd_cl]
    add ax, [wd_cw]
    sub ax, 5
    cmp cx, ax                      ; never past the strip itself
    jbe .ok
    mov cx, ax
.ok:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rlscale - the inch scale band whole: erase, ticks (ONE fill_pat),
;              digits, and the LIVE indent markers at the caret paragraph's
;              positions (SPEC.md 65.3). ~12 calls, drawn by wd_ruler and by
;              wd_rlstat when the caret enters a differently-indented
;              paragraph. Banks the marker caches.
; -----------------------------------------------------------------------------
wd_rlscale:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [wd_vrul], 0
    je .out
    mov ax, WD_RL_SCALE + 24
    call wd_wfit
    jc .out
    call wd_ruly
    add ax, WD_RL_ROW2              ; the scale has a row to itself now
    mov di, ax                      ; DI = that row's top
    mov si, [wd_tx]                 ; SI = the scale's zero: where the TEXT
                                    ; starts, so an indent marker at n cells
                                    ; sits exactly over the column the text
                                    ; will indent to, and inch 1 is one inch
                                    ; of document (SPEC.md 65.2)
    call wd_rlend                   ; CX = the text column's right edge
    mov [wd_rlxe], cx
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
    mov si, wd_ptick
    call OSAPI_GFX_FILL_PAT         ; every minor tick in ONE call
    pop si
    mov bx, si
    add bx, 77                      ; the first digit sits over its inch tick
    mov al, '1'
.dg:
    mov dx, bx
    add dx, 7
    cmp dx, [wd_rlxe]
    ja .marks
    mov cx, bx
    mov dx, di
    add dx, 2
    call OSAPI_FONT_CHAR
    add bx, 80
    inc al
    cmp al, '9'
    jbe .dg
.marks:
    call wd_rlcalc                  ; the live indents (cells)
    mov al, [wd_rll]                ; first-line: the down marker on top, at
    add al, [wd_rlf]                ; left + first
    cbw
    mov cl, 3
    shl ax, cl
    add ax, si
    call wd_rlmclamp
    mov dx, di
    add dx, 3
    call wd_rmkd
    mov al, [wd_rll]                ; left: the up marker below the ticks
    cbw
    shl ax, cl
    add ax, si
    call wd_rlmclamp
    mov dx, di
    add dx, 14
    call wd_rmku
    mov al, [wd_rlr]                ; right: in from the scale's far end
    cbw
    shl ax, cl
    mov dx, [wd_rlxe]
    sub dx, 2
    sub dx, ax
    mov ax, dx
    call wd_rlmclamp
    mov dx, di
    add dx, 14
    call wd_rmku
    mov al, [wd_rll]                ; the strip shows THESE indents now
    mov [wd_rmleft], al
    mov al, [wd_rlf]
    mov [wd_rmfirst], al
    mov al, [wd_rlr]
    mov [wd_rmright], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rlstat - the ruler's pressed cells and markers, delta-cached exactly as
;             the ribbon's cells are (SPEC.md 65.3): a caret move that stays
;             inside one paragraph format draws not a call
; in:  wd_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
wd_rlstat:
    push ax
    push bx
    push cx
    push dx
    cmp byte [wd_vrul], 0
    je .out
    cmp byte [wd_rlok], 0
    je .out
    cmp byte [wd_mopen], WD_M_NONE
    jne .out                        ; a dropdown may cover the strip
    cmp byte [wd_about], 0
    jne .out
    cmp word [wd_dlg], 0
    jne .out
    call wd_rlcalc
    mov bl, [wd_rll]
    cmp bl, [wd_rmleft]
    jne .scale
    mov bl, [wd_rlf]
    cmp bl, [wd_rmfirst]
    jne .scale
    mov bl, [wd_rlr]
    cmp bl, [wd_rmright]
    je .cells
.scale:
    push ax
    call wd_rlscale                 ; a marker moved: the scale band redraws
    pop ax
.cells:
    mov dx, [wd_rlold]
    cmp ax, dx
    je .out
    mov [wd_rlold], ax
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
    call wd_rlxy                    ; AX = x, BX = y, CX = width
    jc .undrawn
    push ax
    add ax, cx
    sub ax, 2                       ; the interior: the frame stays upright
    mov cx, ax
    pop ax
    inc ax
    mov dx, bx
    add dx, WD_BTN_W-2
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
; wd_rldrag - a press on the ruler's scale: drag the nearest indent marker
; in:  CX = x, DX = y (abs), SI = window ptr, gfx lock held; preserves all
; An XOR guide line tracks over the text band (banked x, so the erase is the
; draw again - SPEC.md 48.11's rule); the release snaps to whole cells and
; applies through wd_modpap, the same door as the Ctrl keys and the dialog.
; -----------------------------------------------------------------------------
wd_rldrag:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, WD_RL_SCALE + 24
    call wd_wfit
    jc .done
    call wd_rlcalc                  ; the live indents in wd_rll/f/r
    mov ax, [wd_tx]
    mov [wd_rdz], ax                ; the zero, for the release's arithmetic
    mov al, [wd_rll]
    mov [wd_rdl], al                ; ...and the left, for the first marker's
                                    ; relative answer
    push cx                         ; the press x
    call wd_ruly
    add ax, WD_RL_ROW2 + 9          ; upper half of the SCALE row = the
                                    ; first-line marker
    cmp dx, ax
    pop cx
    jb .first
    ; lower half: left or right, whichever marker is nearer the press
    mov al, [wd_rll]
    cbw
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [wd_rdz]                ; AX = the left marker's x
    mov bx, ax
    sub bx, cx
    jns .la
    neg bx
.la:                                ; BX = its distance
    mov al, [wd_rlr]
    cbw
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    mov di, [wd_rlxe]
    sub di, 2
    sub di, ax                      ; DI = the right marker's x
    mov ax, di
    sub ax, cx
    jns .ra
    neg ax
.ra:
    cmp bx, ax
    jbe .left
    mov byte [wd_ppop], WDPO_SETRIGHT
    jmp short .track
.left:
    mov byte [wd_ppop], WDPO_SETLEFT
    jmp short .track
.first:
    mov byte [wd_ppop], WDPO_SETFIRST
.track:
    mov [wd_rgx], cx                ; the guide, XOR-drawn at the banked x
    call wd_rgxor
.pass:
    call wd_selpace                 ; unlock - yield a tick - relock
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .drop
    call wd_rgclamp                 ; CX clamped onto the scale
    cmp cx, [wd_rgx]
    je .pass
    call wd_rgxor                   ; old off...
    mov [wd_rgx], cx
    call wd_rgxor                   ; ...new on
    jmp short .pass
.drop:
    call wd_rgxor                   ; the guide comes down
    mov ax, [wd_rgx]                ; the landing, snapped to cells
    sub ax, [wd_rdz]
    add ax, 4
    cmp byte [wd_ppop], WDPO_SETRIGHT
    jne .cells
    mov ax, [wd_rlxe]               ; the right marker counts from the far end
    sub ax, 2
    sub ax, [wd_rgx]
    add ax, 4
.cells:
    push cx
    mov cl, 3
    sar ax, cl
    pop cx
    cmp byte [wd_ppop], WDPO_SETFIRST
    jne .arg
    sub al, [wd_rdl]                ; first is RELATIVE to the left indent
.arg:
    mov ah, al                      ; wd_modpap wants op in AL, arg in AH
    mov al, [wd_ppop]               ; (wd_modone clamps the cells)
    call wd_uclose
    call wd_modpap
    call wd_redraw
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_rgxor - the drag guide: one XOR column over the text band at [wd_rgx]
wd_rgxor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [wd_rgx]
    mov cx, ax
    mov bx, [wd_ty]
    mov dx, [wd_bot]
    call OSAPI_GFX_XOR_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_rgclamp - CX clamped onto the scale [wd_rdz-8 .. wd_rlxe]
wd_rgclamp:
    cmp cx, [wd_rlxe]
    jbe .hi
    mov cx, [wd_rlxe]
.hi:
    push ax
    mov ax, [wd_rdz]
    sub ax, 8
    cmp cx, ax
    jae .out
    mov cx, ax
.out:
    pop ax
    ret

; =============================================================================
; The status line (SPEC.md 65.2): 'Pg n  Sec 1  At n  Ln n  Col n' live from
; the caret, CAPS/NUM lamps from the BIOS shift flags at the right end. Page
; = WD_PGLINES lines; Ln restarts per page; Col is 1-based; At is the line on
; the page (draft view's honest unit is the line). The composed text is
; delta-cached like Note Pad's row diff: wd_stdiff letters only the cells
; that changed since the last draw, usually two or three.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_stsrc - refresh [wd_lnv]/[wd_colv] (absolute line, 1-based column) from
;            whatever the engine currently knows; keeps the old values when
;            nothing does (a bounded walk may not have stood on the caret)
; preserves all registers
; -----------------------------------------------------------------------------
wd_stsrc:
    push ax
    push cx
    cmp byte [wd_ckok], 0
    je .try2
    mov ax, [wd_ckpr]               ; the checkpoint: the caret's row start,
    add ax, [wd_top]                ; as a VISIBLE row (SPEC.md 27.4)
    js .try2
    mov [wd_lnv], ax
    mov ax, [wd_cur]
    sub ax, [wd_ckpi]
    inc ax
    mov [wd_colv], ax
    jmp short .out
.try2:
    cmp byte [wd_curseen], 0
    je .out
    mov ax, [wd_cury]
    sub ax, [wd_ty]
    js .out
    mov cl, 3
    shr ax, cl
    add ax, [wd_top]
    mov [wd_lnv], ax
    mov ax, [wd_curx]
    sub ax, [wd_tx]
    jns .cok
    xor ax, ax
.cok:
    shr ax, cl
    inc ax
    mov [wd_colv], ax
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_stcat - append the NUL string SI at DI (the NUL is not copied)
; out: DI past the text; preserves everything else
; -----------------------------------------------------------------------------
wd_stcat:
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
; wd_stcomp - compose the status text into wd_stbuf, space-padded to
;             WD_ST_CELLS and NUL-terminated; preserves all registers
; -----------------------------------------------------------------------------
wd_stcomp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, wd_stbuf
    mov si, wd_s_pg
    call wd_stcat
    mov ax, [wd_lnv]
    xor dx, dx
    mov bx, WD_PGLINES
    div bx                          ; AX = page-1, DX = line on the page - 1
    mov cx, dx
    inc ax
    push ax                         ; the page again, for the p/t field
    call wd_utoa
    mov si, wd_s_sec
    call wd_stcat
    pop ax                          ; --- p/t: the page over the total pages
    call wd_utoa                    ; (status.h's pageInDoc/totalPages field,
    mov byte [di], '/'              ; SPEC.md 65.2) - the total from
    inc di                          ; [wd_drows], a lower bound the height
    mov ax, [wd_drows]              ; count only ever raises
    add ax, WD_PGLINES - 1
    xor dx, dx
    div bx
    or ax, ax
    jnz .t1
    inc ax                          ; an empty document is page 1 of 1
.t1:
    call wd_utoa
    ; --- graceful field dropping (SPEC.md 65.2): a window too narrow to
    ; show every cell drops the At field FIRST - it duplicates Ln by
    ; construction (a page is 54 lines), and losing it keeps Ln and Col,
    ; which wd_stdiff's right-edge clamp would otherwise cut off
    push ax
    mov ax, [wd_cw]
    sub ax, 16
    js .noat
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    cmp ax, WD_ST_CELLS
    jb .noat
    pop ax
    mov si, wd_s_at
    call wd_stcat
    mov ax, cx
    inc ax
    call wd_utoa                    ; At = the line on the page, in li - the
    mov si, wd_s_li                 ; honest draft-view unit (SPEC.md 65.2)
    call wd_stcat
    jmp short .atdone
.noat:
    pop ax
.atdone:
    mov si, wd_s_ln
    call wd_stcat
    mov ax, cx
    inc ax
    call wd_utoa
    mov si, wd_s_col
    call wd_stcat
    mov ax, [wd_colv]
    or ax, ax
    jnz .c1
    inc ax
.c1:
    call wd_utoa
.pad:
    cmp di, wd_stbuf + WD_ST_CELLS
    jae .fin
    mov byte [di], ' '
    inc di
    jmp short .pad
.fin:
    mov byte [wd_stbuf + WD_ST_CELLS], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_stdiff - letter the cells of wd_stbuf that differ from what the strip
;             shows (wd_stold), as ONE font_run of the changed span; then
;             bank the buffer. [wd_stok] = 0 forces the whole line.
; in:  wd_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
wd_stdiff:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor bx, bx                      ; BX = first differing cell
    mov dx, WD_ST_CELLS-1           ; DX = last
    cmp byte [wd_stok], 0
    je .have                        ; no old copy: the whole line differs
.f:
    mov al, [bx+wd_stbuf]
    cmp al, [bx+wd_stold]
    jne .l
    inc bx
    cmp bx, WD_ST_CELLS
    jb .f
    jmp .bank                       ; not a cell changed - nothing drawn
.l:
    mov dx, WD_ST_CELLS-1           ; ...and scan back for the last (stops at
.l2:                                ; BX by construction: BX itself differs)
    mov si, dx
    mov al, [si+wd_stbuf]
    cmp al, [si+wd_stold]
    jne .have
    dec dx
    jmp short .l2
.have:
    ; clamp the span to the cells a narrow window can show
    mov ax, [wd_cw]
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
    mov ah, [si+wd_stbuf+1]         ; NUL-cap the span in place for the run,
    push ax                         ; then put the byte back
    mov byte [si+wd_stbuf+1], 0
    mov ax, bx
    mov cl, 3
    shl ax, cl
    add ax, [wd_cl]
    add ax, 8
    mov cx, ax
    mov dx, [wd_ct]
    add dx, [wd_ch]
    sub dx, 10                      ; the strip's text row
    mov si, bx
    add si, wd_stbuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    pop si
    mov [si+wd_stbuf+1], ah
.bank:
    push es
    push ds
    pop es
    cld
    mov si, wd_stbuf
    mov di, wd_stold
    mov cx, WD_ST_CELLS+1
    rep movsb
    pop es
    mov byte [wd_stok], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_stkval - AL = the CAPS/NUM bits of the BIOS keyboard flag byte
; preserves everything else
;
; 0040:0017 is the BIOS keyboard flag byte - bit 6 CAPS lock, bit 5 NUM lock.
; The keyboard driver keeps BIOS int 16h alive, so the byte is maintained;
; reading it is a plain memory read through a scratch segment register, not a
; BIOS call, so any task and any lock state may do it (SPEC.md 65.2).
; -----------------------------------------------------------------------------
wd_stkval:
    push bx
    push es
    mov bx, 0x40
    mov es, bx
    mov al, [es:0x17]
    and al, 0x60
    pop es
    pop bx
    cmp byte [wd_ovr], 0            ; ...and our own two lamps fold into the
    je .noov                        ; unused low bits (SPEC.md 65.2), so the
    or al, 0x01                     ; worker's delta poll and wd_stat's both
.noov:                              ; notice an Ins or an F8 for free
    cmp byte [wd_ext], 0
    je .noex
    or al, 0x02
.noex:
    ret

; -----------------------------------------------------------------------------
; wd_kbflags - AL = the whole BIOS shift-flag byte at 0040:0017 (bits 0/1
; shift, 2 ctrl, 3 alt) - what tells Shift+Del from NumLock's '.', and
; Alt+BkSp from a backspace (SPEC.md 65.3). Preserves everything else.
; -----------------------------------------------------------------------------
wd_kbflags:
    push bx
    push es
    mov bx, 0x40
    mov es, bx
    mov al, [es:0x17]
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; wd_stkdraw - draw both lamps from the flag bits in AL, and bank them
; in:  AL = CAPS/NUM bits, wd_bounds run, gfx lock held; preserves all
; A lit lamp is inverse video (font_run white-on-black); an unlit one is
; blank - status.h's highlighted-indicator convention, in the one font there
; is. Skipped whole on a window too narrow to keep them clear of the text.
; -----------------------------------------------------------------------------
wd_stkdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [wd_stkf], al
    mov bx, [wd_sbr]
    sub bx, 13 + 128                ; EXT CAPS NUM OVR right-aligned, clear
                                    ; of the grow box: 16 cells with the gaps
                                    ; (status.h's indicator order, 65.2)
    mov dx, [wd_cl]
    add dx, 8 + WD_ST_CELLS*8
    cmp bx, dx
    jb .out                         ; the lamps would collide with the text
    mov dx, [wd_ct]
    add dx, [wd_ch]
    sub dx, 10
    mov cx, bx
    mov si, wd_s_ext                ; EXT (F8's extend mode)
    mov ah, CBLACK                  ; lit: white on black
    test al, 0x02
    jnz .eon
    mov si, wd_s_sp3                ; dark: erased to the strip's white
    mov ah, CWHITE
.eon:
    push ax
    mov al, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    add cx, 32                      ; CAPS, one cell of gap after EXT
    mov si, wd_s_caps
    mov ah, CBLACK
    test al, 0x40
    jnz .con
    mov si, wd_s_sp4
    mov ah, CWHITE
.con:
    push ax
    mov al, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    add cx, 40                      ; NUM
    mov si, wd_s_num
    mov ah, CBLACK
    test al, 0x20
    jnz .non
    mov si, wd_s_sp3
    mov ah, CWHITE
.non:
    push ax
    mov al, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    add cx, 32                      ; OVR (Ins's overtype)
    mov si, wd_s_ovr
    mov ah, CBLACK
    test al, 0x01
    jnz .oon
    mov si, wd_s_sp3
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
; wd_stkchk - the worker's lamp poll: redraw the lamps ONLY on a change
; in:  gfx lock held (worker context); preserves all registers
; -----------------------------------------------------------------------------
wd_stkchk:
    push ax
    push bx
    cmp byte [wd_vsta], 0
    je .out
    cmp byte [wd_sigok], 0
    je .out                         ; no geometry has been banked yet
    cmp byte [wd_mopen], WD_M_NONE
    jne .out                        ; a dropdown may be over the strip
    cmp byte [wd_about], 0
    jne .out
    cmp word [wd_dlg], 0
    jne .out
    call wd_stkval
    cmp al, [wd_stkf]
    je .out
    mov bx, [wd_win]
    call OSAPI_WM_OBSCURED
    jc .out
    call wd_stkdraw
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_status - the status strip, whole: ground, rule, text, lamps
; in:  SI = window ptr, wd_bounds run, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
wd_status:
    push ax
    push bx
    push cx
    push dx
    cmp byte [wd_vsta], 0
    je .out
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [wd_cl]
    mov bx, ax
    add bx, [wd_cw]
    dec bx
    mov dx, [wd_ct]
    add dx, [wd_ch]
    sub dx, WD_STATUS_H
    call OSAPI_GFX_HLINE            ; the rule over the strip
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    mov bx, dx
    inc bx
    mov cx, ax
    add cx, [wd_cw]
    dec cx
    mov dx, [wd_ct]
    add dx, [wd_ch]
    dec dx
    call OSAPI_GFX_FILL             ; the strip's ground
    call wd_stsrc
    call wd_stcomp
    mov byte [wd_stok], 0           ; force the whole line over the fresh
    call wd_stdiff                  ; ground
    call wd_stkval
    call wd_stkdraw
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
; wd_stat - the incremental status update every redraw tail makes
; in:  gfx lock held; preserves all registers
; Guards itself: strip hidden, geometry unbanked, or a dropdown/About box up
; (the modal close repaints the strip whole anyway) - then draws only the
; cells that changed, and the lamps only on a change.
; -----------------------------------------------------------------------------
wd_stat:
    push ax
    cmp byte [wd_vsta], 0
    je .out
    cmp byte [wd_sigok], 0
    je .out
    cmp byte [wd_mopen], WD_M_NONE
    jne .out
    cmp byte [wd_about], 0
    jne .out
    cmp word [wd_dlg], 0
    jne .out
    call wd_stsrc
    call wd_stcomp
    call wd_stdiff
    call wd_stkval
    cmp al, [wd_stkf]
    je .out
    call wd_stkdraw
.out:
    pop ax
    ret

; =============================================================================
; Menu interaction (SPEC.md 65.2). Both period styles at once:
;  - press-drag-release (Macintosh): the press opens the dropdown, dragging
;    moves an XOR highlight (and slides across the bar between menus), the
;    release fires the item under the pointer;
;  - click-open (Windows): press and release on the same title leaves the
;    menu OPEN ("sticky"), the next click fires or dismisses, and the
;    keyboard - Esc, arrows, Enter, mnemonic letters - drives it.
; Alt+mnemonic opens a menu from the keyboard with nothing else held.
; The tracking loop is wd_dragsel's shape exactly: unlock, yield to the next
; tick, relock, poll OSAPI_MOUSE (SPEC.md 27.8.1) - and never OSAPI_WM_ONMOUSEUP,
; which must not be mixed with a polling loop.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_mtrack - the press-drag-release gesture, from a press that opened AL
; in:  AL = menu to open, [wd_mabox] = the anchor band (title or combo box),
;      SI = window ptr, gfx lock held
; out: nothing (the menu is left OPEN only for a press-and-release on the
;      anchor - the sticky case); preserves all registers
; -----------------------------------------------------------------------------
wd_mtrack:
    push ax
    push bx
    push cx
    push dx
    call wd_mopenm
.loop:
    call wd_selpace                 ; unlock - yield to the tick - relock
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .release
    cmp byte [wd_mopen], WD_M_N     ; dragging across the bar slides between
    jae .items                      ; menus (titles only; combos have no bar)
    call wd_mbarhit
    cmp al, 0xFF
    je .items
    cmp al, [wd_mopen]
    je .items
    push ax
    call wd_mclose
    pop ax
    call wd_mtitler
    call wd_mopenm
    jmp short .loop
.items:
    call wd_mfind                   ; the XOR highlight follows the pointer
    cmp al, [wd_mhi]
    je .loop
    push ax
    mov al, [wd_mhi]
    call wd_mhl                     ; old band off (0xFF-safe)...
    pop ax
    mov [wd_mhi], al
    call wd_mhl                     ; ...new band on
    jmp short .loop
.release:
    call wd_mfind
    cmp al, 0xFF
    jne .fire
    push bx
    mov bx, wd_mabox
    call os88ui_bhit                ; released back on the anchor?
    pop bx
    jc .away
    jmp short .out                  ; yes: STICKY - the menu stays open
.away:
    call wd_mclose                  ; released elsewhere: dismissed
    jmp short .out
.fire:
    call wd_mact                    ; DL = the item's action...
    call wd_mclose                  ; ...the covered rows come back first...
    mov al, dl
    call wd_mfire                   ; ...and then it runs
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mclick_open - a click arriving while a dropdown is open (sticky mode)
; in:  CX = x, DX = y, SI = window ptr, gfx lock held; preserves all registers
; Fires an enabled item, toggles closed on the anchor, slides to another bar
; title, stays put for a dead spot inside the panel, and swallows the
; dismissing click anywhere else - a menu's click never leaks to the text.
; -----------------------------------------------------------------------------
wd_mclick_open:
    push ax
    push dx
    call wd_mfind
    cmp al, 0xFF
    jne .fire
    push bx
    mov bx, wd_mabox
    call os88ui_bhit
    pop bx
    jnc .toggle
    call wd_minrect                 ; a separator or disabled item: stay open
    jnc .out
    cmp byte [wd_mopen], WD_M_N
    jae .dismiss
    call wd_mbarhit                 ; a click on another title slides there
    cmp al, 0xFF
    je .dismiss
    cmp al, [wd_mopen]
    je .toggle
    push ax
    call wd_mclose
    pop ax
    call wd_mtitler
    call wd_mtrack                  ; ...and the new press tracks as a press
    jmp short .out
.toggle:
.dismiss:
    call wd_mclose
    jmp short .out
.fire:
    call wd_mact
    call wd_mclose
    mov al, dl
    call wd_mfire
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_minrect - is the point CX/DX inside the open dropdown's rectangle?
; out: CF=0 inside; preserves all registers
; -----------------------------------------------------------------------------
wd_minrect:
    cmp cx, [wd_mrx1]
    jb .no
    cmp cx, [wd_mrx2]
    ja .no
    cmp dx, [wd_mry1]
    jb .no
    cmp dx, [wd_mry2]
    ja .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wd_mroute - route a content click through the chrome (SPEC.md 65.2)
; in:  CX = x, DX = y, SI = window ptr, gfx lock held
; out: CF=1 the click was chrome's and is fully handled; CF=0 not ours.
;      Preserves CX/DX/SI (and everything else).
; -----------------------------------------------------------------------------
wd_mroute:
    push ax
    push bx
    push di
    call wd_bounds
    cmp word [wd_dlg], 0            ; an open dialog is modal before anything
    je .nodlg                       ; else is (SPEC.md 65.3)
    call wd_dgclick
    jmp .cons
.nodlg:
    cmp byte [wd_about], 0
    je .noab
    push bx                         ; the About box is modal: only its OK
    mov bx, wd_abok                 ; button answers, and every click is
    call os88ui_bhit                ; consumed
    pop bx
    jc .cons
    call wd_abclose
    jmp .cons
.noab:
    cmp byte [wd_mopen], WD_M_NONE
    je .closed
    call wd_mclick_open
    jmp .cons
.closed:
    ; the menu bar strip
    mov ax, [wd_ct]
    cmp dx, ax
    jb .pass
    add ax, WD_MENU_H-1
    cmp dx, ax
    ja .notbar
    call wd_settle                  ; opening a menu is not typing: the break
    call wd_uclose                  ; settles and the edit group closes, as
                                    ; for any command (SPEC.md 27.3/27.9)
    call wd_mbarhit
    cmp al, 0xFF
    je .cons                        ; the bar's blank tail
    call wd_mtitler
    call wd_mtrack
    jmp .cons
.notbar:
    ; the ribbon strip: the two combos open; the toggles are hit-tested and
    ; inert this stage (SPEC.md 65.2)
    cmp byte [wd_vrib], 0
    je .norib
    mov ax, [wd_ct]
    add ax, WD_MENU_H
    mov di, ax
    add di, WD_RIBBON_H-1
    cmp dx, ax
    jb .norib
    cmp dx, di
    ja .norib
    call wd_settle
    call wd_uclose
    mov bx, [wd_cl]
    add bx, WD_RB_FBX
    cmp cx, bx
    jb .cons
    mov di, bx
    add di, WD_RB_FBW-1
    cmp cx, di
    ja .rbpts
    mov al, WD_M_FONTC
    call wd_fontscan                ; the machine's faces, listed the first
                                    ; time this combo is opened and never
                                    ; again (SPEC.md 19.8): the scan is real
                                    ; floppy I/O, and a menu nobody opens
                                    ; should cost nothing
    mov al, WD_M_FONTC
    jmp short .combo1
.rbpts:
    mov bx, [wd_cl]
    add bx, WD_RB_PBX
    cmp cx, bx
    jb .cons
    mov di, bx
    add di, WD_RB_PBW-1
    cmp cx, di
    ja .rbtns
    mov al, WD_M_PTSC
.combo1:
    mov [wd_max], bx                ; the dropdown hangs off the box, and the
    mov [wd_mabox], bx              ; box IS the gesture anchor
    mov [wd_mabox+4], di
    mov bx, [wd_ct]
    add bx, WD_MENU_H + WD_RIBBON_H
    mov [wd_may], bx
    sub bx, WD_RIBBON_H
    add bx, 2
    mov [wd_mabox+2], bx
    add bx, 11
    mov [wd_mabox+6], bx
    call wd_mtrack
    jmp .cons
.rbtns:
    ; the toggle cells (SPEC.md 65.3): B I K | U W D fire wd_applyattr, the
    ; pilcrow toggles Show-all, the greyed super/sub pair is inert. The hit
    ; geometry is wd_rbxy's - the same rows the painter and the delta update
    ; read, so a cell cannot be drawn one place and clicked another
    xor ax, ax                      ; AL = the cell being tested
.rbt:
    push ax
    call wd_rbxy                    ; AX = x, BX = y (CF: not drawn)
    jc .rbtn
    cmp cx, ax
    jb .rbtn
    add ax, WD_BTN_W-1
    cmp cx, ax
    ja .rbtn
    pop ax                          ; the hit: AL = the cell
    cmp al, 6
    je .rbsa
    xor bh, bh
    mov bl, al
    mov al, [wd_rbmask+bx]
    call wd_applyattr
    call wd_redraw                  ; rows the toggle dirtied + the cell's
    jmp .cons                       ; inversion (wd_redraw's tail)
.rbsa:
    xor byte [wd_showall], 1        ; Show-all: the pilcrow cells appear in
    call wd_redraw                  ; every row that carries a mark - the
    jmp .cons                       ; signatures find them (SPEC.md 65.1)
.rbtn:
    pop ax
    inc ax
    cmp al, 7
    jb .rbt
    jmp .cons                       ; the strip's blank ground, and the greyed
                                    ; super/sub cell: inert
.norib:
    ; the ruler strip (SPEC.md 65.3): the Style combo opens; the alignment,
    ; spacing and open/close cells FIRE (wd_modpap, the same door as the
    ; keys); the scale drags its indent markers; the greyed tab cells are
    ; inert
    cmp byte [wd_vrul], 0
    je .norul
    call wd_ruly
    mov di, ax
    add di, WD_RULER_H-1
    cmp dx, ax
    jb .norul
    cmp dx, di
    ja .norul
    call wd_settle
    call wd_uclose
    call wd_ruly                    ; row 2 is the scale, whatever the x: it
    add ax, WD_RL_ROW2              ; is a row of its own now and the cells
    cmp dx, ax                      ; cannot be under it
    jb .rlrow1
    call wd_rldrag
    jmp .cons
.rlrow1:
    mov bx, [wd_cl]
    add bx, WD_RL_SBX
    cmp cx, bx
    jb .cons
    mov di, bx
    add di, WD_RL_SBW-1
    cmp cx, di
    ja .rlcells
    mov [wd_max], bx
    mov [wd_mabox], bx
    mov [wd_mabox+4], di
    call wd_ruly
    add ax, WD_RULER_H
    mov [wd_may], ax
    sub ax, WD_RULER_H
    add ax, 2
    mov [wd_mabox+2], ax
    add ax, 11
    mov [wd_mabox+6], ax
    mov al, WD_M_STYLEC
    call wd_mtrack
    jmp .cons
.rlcells:
    mov di, cx                      ; the click x, banked: wd_rlxy answers its
    xor ax, ax                      ; width in CX. AL = the cell being tested;
.rlc:                               ; the geometry is the painter's (SPEC.md 22)
    push ax
    call wd_rlxy                    ; AX = x, BX = y, CX = width
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
    mov al, WDPO_ALIGN
    jmp short .rlgo
.rlsp:
    cmp al, 7
    jae .rloc
    mov ah, al                      ; 4..6: line spacing 1 / 1.5 / 2
    sub ah, 4
    mov al, WDPO_SPACE
    jmp short .rlgo
.rloc:
    mov ah, al                      ; 7 = closed (arg 0), 8 = open (arg 1)
    sub ah, 7
    mov al, WDPO_SB
.rlgo:
    call wd_modpap
    call wd_redraw                  ; the rows that moved; the tail re-inverts
    jmp .cons                       ; the cells (wd_rlstat)
.rlscl:                             ; row 1 past the last cell: dead ground
    jmp .cons
.norul:
    ; the status strip is inert; the grow box never reaches W_ONCLICK
    cmp byte [wd_vsta], 0
    je .pass
    mov ax, [wd_bot]
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
; wd_mkey - route a key through the menu system (SPEC.md 65.2)
; in:  AL = ascii, AH = scan, SI = window ptr, gfx lock held
; out: CF=1 consumed. An open dropdown (and the About box) is MODAL and
;      consumes everything: Esc closes, Left/Right slide along the bar,
;      Up/Down move the highlight over enabled items, Enter fires the
;      highlighted one, a mnemonic letter fires its item. With nothing open,
;      only Alt+<title mnemonic> is claimed (int 16h hands Alt+letter over
;      as ascii 0 + the letter's scan code).
; -----------------------------------------------------------------------------
wd_mkey:
    push bx
    push cx
    push dx
    push di
    cmp word [wd_dlg], 0            ; the dialog is modal (SPEC.md 65.3):
    je .nodlg                       ; Enter fires OK, Esc cancels, a letter
    call wd_dgkey                   ; toggles its box, and nothing leaks
    jmp .cons
.nodlg:
    cmp byte [wd_about], 0
    je .noab
    cmp al, 13                      ; the About box: OK is Enter, Esc or Space
    je .abx
    cmp al, 27
    je .abx
    cmp al, ' '
    je .abx
    jmp .cons                       ; everything else bounces off the modal
.abx:
    call wd_abclose
    jmp .cons
.noab:
    cmp byte [wd_mopen], WD_M_NONE
    jne .open
    or al, al
    jnz .pass
    push si
    mov si, wd_alttab               ; Alt+mnemonic: match the scan code
    xor bx, bx
.at:
    cmp ah, [si+bx]
    je .atgo
    inc bx
    cmp bx, WD_M_N
    jb .at
    pop si
    jmp .pass
.atgo:
    pop si
    call wd_bounds
    call wd_settle
    call wd_uclose
    mov ax, bx
    call wd_mtitler
    call wd_mopenm                  ; opened sticky, no highlight yet
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
    mov al, [wd_mopen]
    call wd_mgeti
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
    test byte [si], WDMF_DIS
    jnz .mnno                       ; a greyed mnemonic answers nothing
    mov dl, [si+2]
    pop si
    call wd_mclose
    mov al, dl
    call wd_mfire
    jmp .cons
.mnno:
    pop si
    jmp .cons
.close:
    call wd_mclose
    jmp .cons
.enter:
    mov al, [wd_mhi]
    cmp al, 0xFF
    je .close                       ; Enter with nothing highlighted = close
    call wd_mact
    call wd_mclose
    mov al, dl
    call wd_mfire
    jmp .cons
.ext:
    cmp ah, WD_K_LEFT
    je .prevm
    cmp ah, WD_K_RIGHT
    je .nextm
    cmp ah, WD_K_UP
    je .up
    cmp ah, WD_K_DOWN
    je .down
    jmp .cons                       ; the modal swallows the rest
.prevm:
    mov al, [wd_mopen]
    cmp al, WD_M_N
    jae .cons                       ; a combo has no neighbours
    dec al
    cmp al, 0xFF
    jne .switch
    mov al, WD_M_N-1
    jmp short .switch
.nextm:
    mov al, [wd_mopen]
    cmp al, WD_M_N
    jae .cons
    inc al
    cmp al, WD_M_N
    jb .switch
    xor al, al
.switch:
    push ax
    call wd_mclose
    pop ax
    call wd_mtitler
    call wd_mopenm
    jmp .cons
.up:
    mov dl, -1
    call wd_mstep
    jmp .cons
.down:
    mov dl, 1
    call wd_mstep
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
; wd_mstep - move the keyboard highlight one enabled item in direction DL
;            (+1 down / -1 up), wrapping; preserves all registers
; -----------------------------------------------------------------------------
wd_mstep:
    push ax
    push bx
    push cx
    push si
    mov al, [wd_mopen]
    call wd_mgeti
    mov cl, [bx+3]                  ; CL = count, CH = tries left
    or cl, cl
    jz .out
    mov ch, cl
    mov al, [wd_mhi]
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
    call wd_mitemp
    test byte [si], WDMF_SEP | WDMF_DIS
    pop ax
    jz .found
    dec ch
    jnz .step
    jmp short .out                  ; nothing selectable at all
.found:
    mov ah, al
    mov al, [wd_mhi]
    call wd_mhl                     ; old off...
    mov [wd_mhi], ah
    mov al, ah
    call wd_mhl                     ; ...new on
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_mfire - dispatch an action byte (SPEC.md 65.2)
; in:  AL = WDA_*, SI = window ptr; gfx lock held, the menu already closed
;      and its cover repainted
; out: clobbers registers like any callback (callers bank what they keep);
;      SI survives every action
; The actions are the SAME routines the shortcut keys call - menu items and
; keys are twin doors that cannot drift apart (SPEC.md 27.8).
; -----------------------------------------------------------------------------
wd_mfire:
    cmp al, WDA_MAX
    ja wd_mf_ret
    push bx
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [bx+wd_ftab]
    pop bx
    jmp ax

wd_ftab:
    dw wd_mf_ret                    ; WDA_NONE
    dw wd_a_new
    dw wd_a_open
    dw wd_a_close                   ; File > Close...
    dw wd_a_save
    dw wd_a_saveas
    dw wd_a_close                   ; ...and Exit: one document, one window
    dw wd_a_undo
    dw wd_a_cut
    dw wd_a_copy
    dw wd_a_paste
    dw wd_a_search
    dw wd_a_repl
    dw wd_a_draft                   ; View > Draft (SPEC.md 65.11)
    dw wd_a_vrib
    dw wd_a_vrul
    dw wd_a_vsta
    dw wd_abopen                    ; Help > About...
    dw wd_mf_ret                    ; Window > 1: the one window, checked
    dw wd_a_csel                    ; a combo entry: cosmetic, EXCEPT the
                                    ; Font one (SPEC.md 65.13)
    dw wd_a_char                    ; Format > Character... (SPEC.md 65.3)
    dw wd_a_para                    ; Format > Paragraph... (SPEC.md 65.3)
    dw wd_a_goto                    ; Edit > Go To... (SPEC.md 65.7)
    dw wd_a_sort                    ; Utilities > Sort... (SPEC.md 65.9)
    dw wd_a_renum                   ; Utilities > Renumber...
    dw wd_a_toc                     ; Insert > Table of Contents...
    dw wd_a_page                    ; View > Page (SPEC.md 65.11)

wd_mf_ret:
    ret

; File > New / Open... ask the dirty question first (SPEC.md 65.4); the
; wd_do* halves are what Yes/No proceed to, and what a clean document runs
; directly.
wd_a_new:
    push ax
    mov al, WDP_NEW
    call wd_askdirty
    pop ax
    jc wd_mf_ret
    ; falls through
wd_donew:
    call wd_uclose
    call wd_new
    mov byte [wd_follow], 1
    jmp wd_redraw

wd_a_open:
    push ax
    mov al, WDP_OPEN
    call wd_askdirty
    pop ax
    jc wd_mf_ret
    ; falls through
wd_doopen:
    call wd_uclose
    mov al, FDLG_OPEN
    jmp wd_dlgopen                  ; no repaint: the dialog is on top of us

wd_a_saveas:
    call wd_uclose
    mov al, FDLG_SAVE
    jmp wd_dlgopen

wd_a_save:
    call wd_uclose
    jmp wd_save                     ; the toast is the whole visible change

wd_a_undo:
    call wd_undo
    jnc .ok
    mov ax, wd_m_noundo
    call wd_saymsg
    jmp short .rd
.ok:
    call wd_chpsync                 ; the caret landed somewhere new (65.3)
.rd:
    mov byte [wd_follow], 1
    jmp wd_redraw

wd_a_cut:
    call wd_cut
    mov byte [wd_follow], 1
    jmp wd_redraw

wd_a_copy:
    jmp wd_copy                     ; nothing on screen moves: the selection
                                    ; stays and the clipboard is invisible

wd_a_paste:
    call wd_paste
    mov byte [wd_follow], 1
    jmp wd_redraw

; Edit > Search... / Replace... open the legacy find panel this stage - the
; real Search/Replace DIALOGS are a later stage; the panel is the same
; feature behind the authentic menu item (SPEC.md 65.2).
; Edit > Search... / Replace... / Go To... - the authentic modal dialogs
; (SPEC.md 65.7). Each presets [wd_dck] (the check bits) and its radios
; before wd_dgopen paints, then focuses its first edit box.
wd_a_search:
    push ax
    push bx
    push di
    mov al, [wd_fopt]
    and al, WDFO_WORD | WDFO_CASE
    mov [wd_dck], al
    call wd_dirset                  ; the Direction radios from [wd_fdir]
    mov bx, wd_dlgsrch
    call wd_dgopen
    cmp [wd_dlg], bx
    jne .nofoc
    mov di, wd_ds_edit
    call wd_dgfocus
.nofoc:                 ; the Search For box takes the keys
    pop di
    pop bx
    pop ax
    ret

wd_a_repl:
    push ax
    push bx
    push di
    mov al, [wd_fopt]               ; all three bits: Confirm Changes rides
    mov [wd_dck], al                ; along (SPEC.md 65.7)
    mov bx, wd_dlgrepl
    call wd_dgopen
    cmp [wd_dlg], bx
    jne .nofoc
    mov di, wd_dr_edit
    call wd_dgfocus
.nofoc:
    pop di
    pop bx
    pop ax
    ret

wd_a_goto:
    push ax
    push bx
    push di
    mov byte [wd_de_goto], 0        ; the box opens empty
    mov byte [wd_dck], 0
    mov bx, wd_dlggoto
    call wd_dgopen
    cmp [wd_dlg], bx
    jne .nofoc
    mov di, wd_dg_edit
    call wd_dgfocus
.nofoc:
    pop di
    pop bx
    pop ax
    ret

; --- the three Utilities dialogs (SPEC.md 65.9) ------------------------------
; Each opens with its radios and check boxes showing the state it last ran
; with, so a second Sort remembers the first one's key - which is what makes
; sorting on two fields two commands rather than a re-typed dialog.
wd_a_sort:
    push ax
    push bx
    push di
    mov al, [wd_sopt]
    and al, WDSO_CASE
    shr al, 1
    shr al, 1
    mov [wd_dck], al                ; the Case Sensitive box is bit 1
    mov bx, wd_so_asc               ; Ascending / Descending
    mov al, [wd_sopt]
    and al, WDSO_DESC
    call wd_radset
    mov bx, wd_so_alp               ; Alphanumeric / Numeric
    mov al, [wd_sopt]
    and al, WDSO_NUM
    call wd_radset
    mov bx, wd_so_com               ; Comma / Tab
    mov al, [wd_sopt]
    and al, WDSO_TAB
    call wd_radset
    mov ax, [wd_sofld]
    or ax, ax                       ; the key is SOME field, and the first
    jnz .fld                        ; one is the default
    mov ax, 1
.fld:
    mov [wd_sofld], ax
    mov bx, wd_de_fld
    call wd_unum
    mov bx, wd_dlgsort
    call wd_dgopen
    pop di
    pop bx
    pop ax
    ret

wd_a_renum:
    push ax
    push bx
    push di
    mov byte [wd_dck], 0
    mov bx, wd_rn_all               ; a THREE-way radio: the first is
    mov al, [wd_rnact]              ; selected when the value is 0, and
    call wd_rad3                    ; wd_rad3 says which of three
    mov bx, wd_rn_auto
    mov al, [wd_rnman]
    call wd_radset
    mov ax, [wd_rnfrom]
    mov bx, wd_de_start
    call wd_unum
    cmp byte [wd_rnfmt], 0          ; the format the real product starts with
    jne .havef
    mov byte [wd_rnfmt], '1'
    mov byte [wd_rnfmt+1], '.'
    mov byte [wd_rnfmt+2], 0
.havef:
    mov bx, wd_dlgrenum
    call wd_dgopen
    pop di
    pop bx
    pop ax
    ret

wd_a_toc:
    push ax
    push bx
    push di
    mov byte [wd_dck], 0
    mov bx, wd_tc_head
    mov al, [wd_tcsrc]
    call wd_radset
    mov bx, wd_tc_all               ; All / From..To
    mov al, [wd_tclvl]
    call wd_radset
    mov ax, [wd_tcfrom]
    or ax, ax
    jnz .haver
    mov ax, 1
    mov [wd_tcfrom], ax
    mov word [wd_tcto], 9
.haver:
    mov bx, wd_de_from
    call wd_unum
    mov ax, [wd_tcto]
    mov bx, wd_de_to
    call wd_unum
    mov bx, wd_dlgtoc
    call wd_dgopen
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The three Utilities dialogs' OK verbs (SPEC.md 65.9). Each banks its radios
; and edits into the command's state, closes, runs, and repaints - the same
; shape wd_dsapply has, so the search dialogs and these cannot drift.
; -----------------------------------------------------------------------------
wd_dsoapply:
    push ax
    push bx
    call wd_dgclose
    xor al, al
    cmp word [wd_so_des+8], 0
    je .n1
    or al, WDSO_DESC
.n1:
    cmp word [wd_so_num+8], 0
    je .n2
    or al, WDSO_NUM
.n2:
    cmp word [wd_so_tab+8], 0
    je .n3
    or al, WDSO_TAB
.n3:
    test byte [wd_dck], 2
    jz .n4
    or al, WDSO_CASE
.n4:
    mov [wd_sopt], al
    mov bx, wd_de_fld
    call wd_uatoi
    or ax, ax                       ; field 0 is field 1: a key has to be
    jnz .fld                        ; SOME field
    mov ax, 1
.fld:
    mov [wd_sofld], ax
    push si                         ; the command uses SI as scratch, and
    call wd_dosort                     ; wd_redraw wants the WINDOW there
    pop si
    call wd_redraw
    pop bx
    pop ax
    ret

wd_drnapply:
    push ax
    push bx
    call wd_dgclose
    mov al, WDRN_ALL
    cmp word [wd_rn_numd+8], 0
    je .n1
    mov al, WDRN_NUMD
.n1:
    cmp word [wd_rn_rem+8], 0
    je .n2
    mov al, WDRN_REMOVE
.n2:
    mov [wd_rnact], al
    xor al, al
    cmp word [wd_rn_man+8], 0
    je .n3
    mov al, 1
.n3:
    mov [wd_rnman], al
    mov ax, 1                       ; Automatic numbers from 1; Manual from
    or al, al                       ; the Start at value
    jz .start
    mov bx, wd_de_start
    call wd_uatoi
    or ax, ax
    jnz .start
    mov ax, 1
.start:
    mov [wd_rnfrom], ax
    push si                         ; the command uses SI as scratch, and
    call wd_dorenum                     ; wd_redraw wants the WINDOW there
    pop si
    call wd_redraw
    pop bx
    pop ax
    ret

wd_dtcapply:
    push ax
    push bx
    call wd_dgclose
    xor al, al
    cmp word [wd_tc_fld+8], 0
    je .n1
    mov al, 1
.n1:
    mov [wd_tcsrc], al
    xor al, al
    cmp word [wd_tc_rng+8], 0
    je .n2
    mov al, 1
.n2:
    mov [wd_tclvl], al
    mov word [wd_tcfrom], 1         ; All is levels 1..9
    mov word [wd_tcto], 9
    or al, al
    jz .go
    mov bx, wd_de_from
    call wd_uatoi
    or ax, ax
    jnz .f
    mov ax, 1
.f:
    mov [wd_tcfrom], ax
    mov bx, wd_de_to
    call wd_uatoi
    or ax, ax
    jnz .t
    mov ax, 9
.t:
    mov [wd_tcto], ax
.go:
    push si                         ; the command uses SI as scratch, and
    call wd_dotoc                     ; wd_redraw wants the WINDOW there
    pop si
    call wd_redraw
    pop bx
    pop ax
    ret

; wd_radset - a two-way radio pair at BX: AL = 0 selects the first, non-zero
; the second. Preserves all registers.
wd_radset:
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

; wd_rad3 - a three-way radio row at BX: AL = 0, 1 or 2. Preserves all.
wd_rad3:
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

; wd_unum - write unsigned AX into the NUL buffer at BX. Preserves all.
wd_unum:
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

; wd_uatoi - AX = the unsigned number in the NUL buffer at BX (0 if none).
; Preserves all registers but AX.
wd_uatoi:
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
; View > Draft / View > Page (SPEC.md 65.11)
; in:  SI = window ptr; out: nothing; clobbers as a callback
;
; The view is one byte, and everything that differs falls out of it in
; wd_bounds: the sheet's width and where it sits. Switching re-lays the whole
; document, so both go through the full redraw rather than a damage range -
; this is the one action in the app that legitimately repaints everything,
; because everything moved.
; -----------------------------------------------------------------------------
wd_a_draft:
    cmp byte [wd_vpage], 0
    je wd_vwsame
    mov byte [wd_vpage], 0
    jmp short wd_vwset

wd_a_page:
    cmp byte [wd_vpage], 0
    jne wd_vwsame
    mov byte [wd_vpage], 1
wd_vwset:
    call wd_bounds                  ; the sheet moved, so the wrap width and
    mov byte [wd_gchg], 1           ; the text origin did
    mov byte [wd_hdirty], 1
    mov byte [wd_ckok], 0
    mov byte [wd_rowsok], 0
    mov byte [wd_follow], 1
    call wd_redraw
wd_vwsame:
    ret

; wd_dirset - the Search dialog's Up/Down radio states from [wd_fdir]
wd_dirset:
    push ax
    xor ax, ax
    mov word [wd_ds_up+8], ax
    mov word [wd_ds_dn+8], ax
    cmp byte [wd_fdir], 0
    jne .up
    mov word [wd_ds_dn+8], 1
    jmp short .out
.up:
    mov word [wd_ds_up+8], 1
.out:
    pop ax
    ret

wd_a_vrib:
    xor byte [wd_vrib], 1
    jmp short wd_vtoggle
wd_a_vrul:
    xor byte [wd_vrul], 1
    jmp short wd_vtoggle
wd_a_vsta:
    xor byte [wd_vsta], 1
    ; falls through

; -----------------------------------------------------------------------------
; wd_vtoggle - a View strip toggled: re-lay the text band, repaint the strips
; in:  SI = window ptr, gfx lock held; preserves all registers
;
; wd_redrawall recomputes the geometry (wd_bounds reads the toggles) and then
; MOVES the text band with the panel-open/close blit when it can - a ribbon
; or ruler toggle changes only [wd_ty], which is exactly the move wd_panmove
; exists for; a status toggle changes [wd_bot], the blit refuses, and the
; full path repaints everything including the chrome. After the cheap path
; the strips between the bar and the text are stale, so they are repainted
; here - a bounded strip each, priced in calls, not a full window.
; -----------------------------------------------------------------------------
wd_vtoggle:
    push ax
    call wd_redrawall
    call wd_mbar                    ; cheap insurance either way: the bar's
                                    ; mnemonics survive a full repaint too
    cmp byte [wd_vrib], 0
    je .n1
    call wd_ribbon
.n1:
    cmp byte [wd_vrul], 0
    je .n2
    call wd_ruler
.n2:
    cmp byte [wd_vsta], 0
    je .n3
    call wd_status
.n3:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_a_close - File > Close and File > Exit (SPEC.md 65.2/65.4)
; in:  SI = window ptr, gfx lock held; preserves all registers
;
; A dirty document raises the save-changes prompt first; wd_doclose is the
; close itself. There is no self-close API slot, so the split is: hide the
; window here for instant feedback and raise [wd_quit]; the WORKER - the only
; context whose OSAPI_TASK_ALIVE can end the instance - destroys the record
; and dies inside its next ALIVE (see wd_worker's .quit). The worker is hired
; first if the lazy hire has not run yet; if the task table is full
; (transient), the close is refused with the reason on screen (SPEC.md 47).
;
; THE KERNEL CLOSE BOX CANNOT PROMPT: clicking it tears the instance down
; inside the kernel's own path (task, region, claims and record freed) with
; no callback in between - a documented limitation (SPEC.md 65.4). The File
; menu's Close and Exit, and their keys, all come through here.
; -----------------------------------------------------------------------------
wd_a_close:
    push ax
    mov al, WDP_CLOSE
    call wd_askdirty
    pop ax
    jc wd_mf_ret
    ; falls through
wd_doclose:
    push ax
    push bx
    call wd_hire
    cmp byte [wd_hired], 0
    je .refuse
    mov byte [wd_quit], 1
    mov bx, [wd_win]
    call OSAPI_WM_HIDE
    jmp short .out
.refuse:
    mov ax, wd_m_noclose
    call wd_saymsg
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; The About box (SPEC.md 65.2): 'Microsoft Word 1.1a for os8088' + OK,
; centred, modal exactly like a dropdown - wd_mkey and wd_mroute consume
; everything while [wd_about] is up, and closing repaints through the same
; wd_mrepair. Reached from Help > About... and from the kernel bar's 'About
; Microsoft Word' (OSAPI_ABOUT_SET, registered in wd_entry).
; =============================================================================

; -----------------------------------------------------------------------------
; wd_habout - the OSAPI_ABOUT_SET handler: SI = our window, lock held
; -----------------------------------------------------------------------------
wd_habout:
    call wd_bounds                  ; the kernel bar path arrives without a
    call wd_settle                  ; click of ours having run wd_bounds
    ; falls through into wd_abopen

; -----------------------------------------------------------------------------
; wd_abopen - open the About box (wd_bounds run, gfx lock held)
; preserves all registers
; -----------------------------------------------------------------------------
wd_abopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [wd_mopen], WD_M_NONE  ; it replaces any open dropdown
    je .nodrop
    call wd_mclose
.nodrop:
    cmp word [wd_dlg], 0            ; ...and any open dialog (the kernel-bar
    je .clear                       ; About can arrive over one)
    call wd_dgclose
.clear:
    mov ax, [wd_cw]                 ; a window too small for the box gets the
    cmp ax, 360                     ; name line as a toast instead - refusal
    jb .toast                       ; with the reason (SPEC.md 47)
    mov ax, [wd_ch]
    cmp ax, 96
    jb .toast
    mov ax, [wd_cw]
    sub ax, 344
    shr ax, 1
    and ax, 0xFFF8                  ; the box's text starts on a byte column
    add ax, [wd_cl]
    mov [wd_abrect], ax
    add ax, 343
    mov [wd_abrect+4], ax
    mov ax, [wd_ch]
    sub ax, 72
    shr ax, 1
    add ax, [wd_ct]
    mov dx, [wd_ct]
    add dx, WD_MENU_H
    cmp ax, dx
    jae .yok
    mov ax, dx
.yok:
    mov [wd_abrect+2], ax
    add ax, 71
    mov [wd_abrect+6], ax
    ; panel, frame, shadow - the dropdown's dress
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wd_abrect]
    mov bx, [wd_abrect+2]
    mov cx, [wd_abrect+4]
    mov dx, [wd_abrect+6]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN
    call OSAPI_GFX_FRAME
    mov ax, cx
    inc ax
    mov bx, [wd_abrect+2]
    inc bx
    mov cx, ax
    mov dx, [wd_abrect+6]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [wd_abrect]
    inc ax
    mov bx, [wd_abrect+6]
    inc bx
    mov cx, [wd_abrect+4]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    ; the three lines (SPEC.md 65.2): the name, the version, and where the
    ; authentic UI came from - the Computer History Museum's Opus release
    mov cx, [wd_abrect]
    add cx, 8
    mov dx, [wd_abrect+2]
    add dx, 8
    mov si, wd_s_about
    call OSAPI_FONT_STR
    add dx, 12
    mov si, wd_s_abou2
    call OSAPI_FONT_STR
    add dx, 12
    mov si, wd_s_abou3
    call OSAPI_FONT_STR
    ; the OK button
    mov ax, [wd_abrect]
    add ax, 148
    mov [wd_abok], ax
    add ax, 47
    mov [wd_abok+4], ax
    mov ax, [wd_abrect+2]
    add ax, 50
    mov [wd_abok+2], ax
    add ax, 13
    mov [wd_abok+6], ax
    push si
    mov bx, wd_abok
    mov si, wd_s_ok
    mov di, OS88UI_FILL | OS88UI_DEF
    call os88ui_btn
    pop si
    mov byte [wd_about], 1
    jmp short .out
.toast:
    mov ax, wd_s_abou2              ; the version line carries the identity
    call wd_saymsg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_abclose - take the About box down and repaint what it covered
; in:  SI = window ptr, gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
wd_abclose:
    push ax
    mov byte [wd_about], 0
    mov ax, [wd_abrect]
    mov [wd_mrx1], ax
    mov ax, [wd_abrect+2]
    mov [wd_mry1], ax
    mov ax, [wd_abrect+4]
    mov [wd_mrx2], ax
    mov ax, [wd_abrect+6]
    mov [wd_mry2], ax
    call wd_mrepair
    pop ax
    ret

; =============================================================================
; The modal dialog framework (SPEC.md 65.3), and Format > Character...
;
; A dialog is data - the WDD_* records up top - drawn centred over the
; content in the dropdown's dress (white panel, black frame, grey shadow).
; While one is open wd_mkey and wd_mroute hand it every key and click: Enter
; is OK, Esc is Cancel, a mnemonic letter toggles its check box, clicks hit
; the buttons and boxes through the same records the painter drew. Closing
; repaints the covered band through wd_mrepair, exactly as a dropdown does.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_dgopen - open dialog BX (a descriptor) centred over the content
; in:  SI = window ptr, BX = descriptor, gfx lock held; preserves all
; A window too small for it gets the title as a toast - refusal with the
; reason (SPEC.md 47).
; -----------------------------------------------------------------------------
wd_dgopen:
    push ax
    push cx
    push dx
    call wd_bounds
    call wd_settle                  ; a dialog is not typing
    call wd_uclose
    mov ax, [bx]
    add ax, 8
    cmp ax, [wd_cw]
    ja .toast
    mov ax, [bx+2]
    add ax, 2                       ; the shadow's pixel; a modal dialog may
    cmp ax, [wd_ch]                 ; cover the in-window menu bar (the close
    ja .toast                       ; repaint restores it) - it need only fit
                                    ; the content box (SPEC.md 65.3)
    mov ax, [wd_cw]                 ; centred, the x snapped to a byte column
    sub ax, [bx]                    ; (the content origin is snapped, so the
    shr ax, 1                       ; sum stays on one)
    and ax, 0xFFF8
    add ax, [wd_cl]
    mov [wd_dlgx], ax
    mov [wd_dlrect], ax
    add ax, [bx]
    dec ax
    mov [wd_dlrect+4], ax
    mov ax, [wd_ch]
    sub ax, [bx+2]
    cmp bx, wd_dlgconf              ; the confirm strip PINS to the bottom of
    je .bot                         ; the content (SPEC.md 65.7): the match it
    shr ax, 1                       ; asks about is selected in the text above
.bot:
    add ax, [wd_ct]
    mov dx, [wd_ct]                 ; ...its bottom (the shadow adds one) must
    add dx, [wd_ch]                 ; not spill past the content onto the
    sub dx, [bx+2]                  ; window's border (SPEC.md 65.3): a short
    dec dx                          ; CGA content centres the box too low, so
    cmp ax, dx                      ; clamp the bottom - the height check above
    jbe .ybot                       ; guarantees such a y exists
    mov ax, dx
.ybot:
    mov dx, [wd_ct]
    cmp ax, dx
    jae .yok
    mov ax, dx                      ; ...and never above the content top
.yok:
    mov [wd_dlgy], ax
    mov [wd_dlrect+2], ax
    add ax, [bx+2]
    dec ax
    mov [wd_dlrect+6], ax
    cmp bx, wd_dlgchar              ; only Format Character reads the caret's
    jne .ckset                      ; attributes; every other dialog's opener
    call wd_dispattr                ; preset [wd_dck] itself (SPEC.md 65.7)
    mov [wd_dck], al
.ckset:
    mov word [wd_dgfoc], 0          ; no edit is focused yet
    mov [wd_dlg], bx
    call wd_dgpaint
    jmp short .out
.toast:
    mov ax, [bx+4]
    call wd_saymsg
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgpaint - draw the open dialog whole: dress, title, every control
; in:  [wd_dlg]/[wd_dlgx]/[wd_dlgy]/[wd_dlrect], gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wd_dlrect]
    mov bx, [wd_dlrect+2]
    mov cx, [wd_dlrect+4]
    mov dx, [wd_dlrect+6]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN
    call OSAPI_GFX_FRAME
    mov ax, cx                      ; the drop shadow, the dropdown's
    inc ax
    mov bx, [wd_dlrect+2]
    inc bx
    mov cx, ax
    mov dx, [wd_dlrect+6]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [wd_dlrect]
    inc ax
    mov bx, [wd_dlrect+6]
    inc bx
    mov cx, [wd_dlrect+4]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    mov bx, [wd_dlg]
    mov cx, [wd_dlgx]
    add cx, 8
    mov dx, [wd_dlgy]
    add dx, 6
    mov si, [bx+4]                  ; the title
    call OSAPI_FONT_STR
    lea di, [bx+6]
.ctl:
    cmp byte [di], 0
    je .done
    call wd_dgctl
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
; wd_dgctl - draw ONE control record
; in:  DI = the record, [wd_dlgx]/[wd_dlgy], gfx lock held; preserves all
; Disabled controls draw whole under the disabled pen (SPEC.md 47), so they
; dither at 1bpp; os88ui_glyph takes its disabled flag in AH.
; -----------------------------------------------------------------------------
wd_dgctl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [di+2]
    add cx, [wd_dlgx]               ; CX = the control's x
    mov dx, [di+4]
    add dx, [wd_dlgy]               ; DX = its y
    mov al, [di+1]
    and al, WDDF_DIS
    add al, 0xFF                    ; CF = 1 exactly when disabled
    call OSAPI_GFX_PEN
    mov al, [di]
    cmp al, WDD_LBL
    je .lbl
    cmp al, WDD_CHK
    je .chk
    cmp al, WDD_RAD
    je .rad
    cmp al, WDD_GRP
    je .grp
    cmp al, WDD_BOX
    je .box
    cmp al, WDD_BTN
    je .btn
    cmp al, WDD_EDIT
    je .edit
    jmp .done
.lbl:
    mov si, [di+6]
    call OSAPI_FONT_STR
    jmp .done
.chk:
    mov al, OS88UI_GCHECK
    mov bl, [di+8]                  ; the attr mask this box edits
    test [wd_dck], bl
    jz .coff
    or al, OS88UI_GON
.coff:
    mov ah, [di+1]
    and ah, WDDF_DIS                ; AH non-zero = disabled (the one routine
    call os88ui_glyph               ; whose flag rides there)
    add cx, 16
    add dx, 2
    mov al, [di+1]                  ; the glyph left the pen LIVE: back to the
    and al, WDDF_DIS                ; record's own state for the label
    add al, 0xFF
    call OSAPI_GFX_PEN
    mov si, [di+6]
    call OSAPI_FONT_STR
    test byte [di+1], WDDF_DIS
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
    and ah, WDDF_DIS
    call os88ui_glyph
    add cx, 16
    add dx, 2
    mov al, [di+1]
    and al, WDDF_DIS
    add al, 0xFF
    call OSAPI_GFX_PEN
    mov si, [di+6]
    call OSAPI_FONT_STR
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
    call OSAPI_FONT_STR
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
    call OSAPI_FONT_STR
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
    call OSAPI_FONT_STR
    cmp di, [wd_dgfoc]
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
    mov [wd_dgr], cx
    mov [wd_dgr+2], dx
    add cx, [di+8]
    dec cx
    mov [wd_dgr+4], cx
    add dx, 13
    mov [wd_dgr+6], dx
    mov si, [di+6]
    push di
    mov ax, OS88UI_FILL
    cmp word [di+10], 1             ; OK wears the default ring
    jne .ndef
    or ax, OS88UI_DEF
.ndef:
    test byte [di+1], WDDF_DIS      ; a greyed button greys WHOLE through
    jz .nbdis                       ; os88ui's own flag (SPEC.md 47)
    or ax, OS88UI_DIS
.nbdis:
    mov di, ax
    mov bx, wd_dgr
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
; wd_dghit - which LIVE control is the point on?
; in:  CX = x, DX = y (abs), [wd_dlg] open
; out: CF=0 with AL = its type and DI = its record; CF=1 none
; -----------------------------------------------------------------------------
wd_dghit:
    push ax
    push bx
    push si
    mov di, [wd_dlg]
    add di, 6
.h:
    mov al, [di]
    or al, al
    jz .none
    test byte [di+1], WDDF_DIS
    jnz .next
    cmp al, WDD_CHK
    je .rect
    cmp al, WDD_BTN
    je .rect
    cmp al, WDD_EDIT
    je .rect
    cmp al, WDD_RAD
    jne .next
    cmp word [di+10], 0             ; a LIVE radio has a group; the char
    je .next                        ; dialog's are decorative (SPEC.md 65.3)
.rect:
    mov bx, [di+2]
    add bx, [wd_dlgx]               ; BX = x1
    cmp cx, bx
    jb .next
    push cx
    cmp al, WDD_BTN
    jne .edw
    mov cx, [di+8]                  ; a button is p2 wide, 14 tall
    mov si, 13
    jmp short .havew
.edw:
    cmp al, WDD_EDIT
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
    add bx, [wd_dlgy]               ; BX = y1
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
; wd_dgtoggle - flip the check box DI points at and redraw it
; in:  DI = a WDD_CHK record, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgtoggle:
    push ax
    mov al, [di+8]
    xor [wd_dck], al
    call wd_dgctl                   ; the glyph white-boxes itself; the label
    pop ax                          ; redraws the same pixels transparently
    ret

; -----------------------------------------------------------------------------
; wd_dgclick - a click while the dialog is up (always consumed: it is modal)
; in:  CX = x, DX = y, SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgclick:
    push ax
    push di
    call wd_dghit
    jc .out
    cmp al, WDD_BTN
    je .btn
    cmp al, WDD_RAD
    je .rad
    cmp al, WDD_EDIT
    je .edit
    call wd_dgtoggle
    jmp short .out
.rad:
    call wd_dgradio
    jmp short .out
.edit:
    call wd_dgfocus
    jmp short .out
.btn:
    cmp word [di+10], 1
    je .ok
    cmp word [di+10], 3             ; the prompt's third verb (SPEC.md 65.4)
    je .no
    call wd_dgcancel                ; Cancel: discard
    jmp short .out
.no:
    call wd_dgno
    jmp short .out
.ok:
    call wd_dgok
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgradio - select the live radio DI points at, deselecting its group
; in:  DI = a WDD_RAD record with a group; gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgradio:
    push ax
    push bx
    push di
    mov bx, [di+10]                 ; the group
    mov ax, di
    mov di, [wd_dlg]
    add di, 6
.r:
    cmp byte [di], 0
    je .done
    cmp byte [di], WDD_RAD
    jne .n
    cmp [di+10], bx
    jne .n
    cmp di, ax
    je .on
    cmp word [di+8], 0
    je .n
    mov word [di+8], 0              ; off, and redrawn in place - the glyph
    call wd_dgctl                   ; white-boxes itself
    jmp short .n
.on:
    cmp word [di+8], 1
    je .n
    mov word [di+8], 1
    call wd_dgctl
.n:
    add di, 12
    jmp short .r
.done:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgfocus - move the edit focus to record DI; both boxes redraw
; -----------------------------------------------------------------------------
wd_dgfocus:
    push ax
    push di
    mov ax, di
    cmp ax, [wd_dgfoc]
    je .out
    mov di, [wd_dgfoc]
    mov [wd_dgfoc], ax
    or di, di
    jz .new
    call wd_dgctl                   ; the old box loses its caret
.new:
    mov di, ax
    call wd_dgctl
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgkey - a key while the dialog is up (always consumed)
; in:  AL = ascii, SI = window ptr, gfx lock held; preserves all
; Enter = OK, Esc = Cancel, a mnemonic letter toggles its check box.
; -----------------------------------------------------------------------------
wd_dgkey:
    push ax
    push bx
    push di
    cmp al, 27
    je .esc
    cmp al, 13
    je .enter
    cmp al, 9
    je .tab                         ; Tab cycles the edit fields (SPEC.md 65.3)
    cmp word [wd_dlg], wd_dlgask    ; the two Yes/No/Cancel prompts answer Y
    je .yn                          ; and N from the keyboard (SPEC.md 65.4)
    cmp word [wd_dlg], wd_dlgconf
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
    call wd_dgno
    jmp .out
.noyn:
    cmp word [wd_dgfoc], 0
    je .fold
    mov di, [wd_dgfoc]              ; a FREE-TEXT edit consumes every
    test byte [di+1], WDDF_TXT      ; printable (SPEC.md 65.7) - the check
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
    mov di, [wd_dlg]
    add di, 6
.m:
    cmp byte [di], 0
    je .out
    cmp byte [di], WDD_CHK
    jne .mn
    test byte [di+1], WDDF_DIS
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
    call wd_dgtoggle
    jmp .out
.mn:
    add di, 12
    jmp short .m
.esc:
    call wd_dgcancel
    jmp .out
.enter:
    call wd_dgok
    jmp .out
.eins:
    push di                         ; append to the focused buffer, up to its
    mov di, [wd_dgfoc]              ; record's p3 capacity (0 = the numeric
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
    call wd_dgctl                   ; the box redraws in place
.efull:
    pop di
    jmp short .out
.ebs:
    push di
    mov di, [wd_dgfoc]
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
    call wd_dgctl
.ebd:
    pop di
    jmp short .out
.tab:
    push cx                         ; the NEXT edit after the focused one,
    push di                         ; wrapping; bounded, because a dialog
    mov bx, [wd_dgfoc]              ; with no edits must fall out clean
    or bx, bx
    jnz .t0
    mov bx, [wd_dlg]
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
    mov di, [wd_dlg]
    add di, 6
.tt:
    cmp di, bx
    je .tdone
    cmp byte [di], WDD_EDIT
    jne .tn
    call wd_dgfocus
.tdone:
    pop di
    pop cx
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgok - OK (or Yes): apply the open dialog's answer, close, repaint
; in:  SI = window ptr, gfx lock held; preserves all
; Which dialog is up decides which apply runs (SPEC.md 65.3/65.4/65.7).
; -----------------------------------------------------------------------------
wd_dgok:
    push ax
    push bx
    mov bx, [wd_dlg]
    cmp bx, wd_dlgpara
    je .para
    cmp bx, wd_dlgchar
    je .char
    cmp bx, wd_dlgsrch
    je .srch
    cmp bx, wd_dlgrepl
    je .repl
    cmp bx, wd_dlggoto
    je .goto
    cmp bx, wd_dlgask
    je .yes
    cmp bx, wd_dlgconf
    je .cyes
    cmp bx, wd_dlgsort
    je .sort
    cmp bx, wd_dlgrenum
    je .renum
    cmp bx, wd_dlgtoc
    je .toc
    call wd_dgclose                 ; unknown: just take it down
    jmp short .out
.sort:
    call wd_dsoapply                ; the three Utilities commands (65.9)
    jmp short .out
.renum:
    call wd_drnapply
    jmp short .out
.toc:
    call wd_dtcapply
    jmp short .out
.char:
    call wd_dgapply
    call wd_dgclose
    call wd_redraw                  ; rows the new dress dirtied, outside what
    jmp short .out                  ; the close already repainted; the tail
                                    ; re-inverts the ribbon cells
.para:
    call wd_dpapply
    call wd_dgclose
    call wd_redraw
    jmp short .out
.srch:
    call wd_dsapply                 ; closes, searches, redraws (SPEC.md 65.7)
    jmp short .out
.repl:
    call wd_drapply
    jmp short .out
.goto:
    call wd_dgoto
    jmp short .out
.yes:
    call wd_dgyes                   ; the dirty prompt's Yes (SPEC.md 65.4)
    jmp short .out
.cyes:
    call wd_dcyes                   ; the confirm strip's Yes (SPEC.md 65.7)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgno - the prompts' No verb (SPEC.md 65.4/65.7)
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgno:
    push ax
    push bx
    mov bx, [wd_dlg]
    cmp bx, wd_dlgask
    je .askno
    cmp bx, wd_dlgconf
    je .cno
    call wd_dgclose
    jmp short .out
.askno:
    call wd_dgclose                 ; No: proceed without saving
    call wd_runpend
    jmp short .out
.cno:
    call wd_dcno                    ; skip this match, find the next
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgcancel - Cancel/Esc: discard; the confirm session ends with its count
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgcancel:
    push ax
    push bx
    mov bx, [wd_dlg]
    call wd_dgclose
    mov byte [wd_pend], 0           ; nothing pending any more
    cmp bx, wd_dlgconf
    jne .out
    mov ax, [wd_cfn]                ; the sweep so far is what happened: say
    call wd_saycnt                  ; how much (SPEC.md 65.7)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgapply - the check boxes' byte onto the selection or the typing attrs
; preserves all registers
; With a selection every character in the span takes the byte WHOLE (the
; boxes are authoritative - that is what OK on this dialog means), as ONE
; bulk undo group; hidden may have changed either way, so the layout tables
; drop (SPEC.md 65.3).
; -----------------------------------------------------------------------------
wd_dgapply:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov dl, [wd_dck]
    and dl, 0x7F
    cmp byte [wd_selon], 0
    je .typing
    call wd_selget                  ; AX = start, CX = length
    jc .typing
    push ax
    push cx
    call wd_urec_bulk               ; text and CHP into the arena (65.3)
    pop cx
    pop ax
    mov es, [wd_cseg]
    mov bx, ax
    mov di, cx
    push ds
    mov ds, [wd_dseg]               ; step over ¶ marks: their byte is a PAP
.f:                                 ; index (SPEC.md 65.3)
    cmp byte [bx], 13
    je .fnx
    mov [es:bx], dl
.fnx:
    inc bx
    dec di
    jnz .f
    pop ds
    call wd_urec_bulkend            ; CX is still the span's length
    mov byte [wd_dirty], 1          ; a dressed span differs from the disk
    test dl, WDAT_HID
    jz .nohid
    mov byte [wd_hashid], 1
.nohid:
    mov byte [wd_ckok], 0           ; hidden may have appeared or gone: the
    mov byte [wd_rowsok], 0         ; layout tables describe a re-wrapped
    call wd_hmark                   ; document
    jmp short .done
.typing:
    mov [wd_chp], dl
    test dl, WDAT_HID               ; hidden TYPING attrs put hidden chars in
    jz .done                        ; on the next keystroke
    mov byte [wd_hashid], 1
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgclose - take the dialog down and repaint what it covered
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dgclose:
    push ax
    mov word [wd_dlg], 0
    mov ax, [wd_dlrect]
    mov [wd_mrx1], ax
    mov ax, [wd_dlrect+2]
    mov [wd_mry1], ax
    mov ax, [wd_dlrect+4]
    mov [wd_mrx2], ax
    mov ax, [wd_dlrect+6]
    mov [wd_mry2], ax
    call wd_mrepair
    pop ax
    ret

; =============================================================================
; Search / Replace / Go To (SPEC.md 65.7) and the dirty prompt (SPEC.md 65.4)
; - the appliers behind wd_dgok's dispatcher
; =============================================================================

; wd_fplen / wd_frlen - the search text's / replacement's live length
wd_fplen:
    push ax
    push bx
    mov bx, wd_fpat
    xor ax, ax
.l:
    cmp byte [bx], 0
    je .done
    inc bx
    inc ax
    jmp short .l
.done:
    mov [wd_fpatn], ax
    call wd_pcomp               ; the raw text is what the dialog edits; the
                                ; COMPILED pattern is what the engine walks
                                ; (SPEC.md 65.7), and this is the one door
                                ; the raw text changes through
    pop bx
    pop ax
    ret
wd_frlen:
    push ax
    push bx
    mov bx, wd_frep
    xor ax, ax
.l:
    cmp byte [bx], 0
    je .done
    inc bx
    inc ax
    jmp short .l
.done:
    mov [wd_frepn], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dsapply - the Search dialog's OK: bank the options, run the search
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_dsapply:
    push ax
    mov al, [wd_dck]                ; Whole Word + Match Upper/Lowercase from
    and al, WDFO_WORD | WDFO_CASE   ; the boxes; Confirm keeps its old answer
    mov ah, [wd_fopt]               ; (it is the Replace dialog's box)
    and ah, WDFO_CONF
    or al, ah
    mov [wd_fopt], al
    xor al, al                      ; ...and Direction from the radios
    cmp word [wd_ds_up+8], 0
    je .dn
    mov al, 1
.dn:
    mov [wd_fdir], al
    call wd_fplen
    call wd_dgclose
    cmp byte [wd_fdir], 0
    jne .up
    call wd_donext                  ; selects + follows, or toasts the miss
    jmp short .draw
.up:
    call wd_doprev
.draw:
    call wd_redraw
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_drapply - the Replace dialog's OK: sweep, or start the confirm session
; in:  SI = window ptr, gfx lock held; preserves all
; -----------------------------------------------------------------------------
wd_drapply:
    push ax
    mov al, [wd_dck]                ; all three bits are this dialog's
    mov [wd_fopt], al
    call wd_fplen
    call wd_frlen
    call wd_dgclose
    cmp word [wd_fpatn], 0
    je .miss
    test byte [wd_fopt], WDFO_CONF
    jnz .conf
    call wd_dorepall                ; ONE bulk undo record + 'n changes'
    call wd_redraw
    jmp short .out
.conf:
    call wd_uclose
    mov word [wd_cfn], 0            ; the confirm sweep starts at the top,
    xor ax, ax                      ; like Replace All (SPEC.md 65.7)
    call wd_cfstep
    jmp short .out
.miss:
    call wd_fmiss
    call wd_redraw
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_cfstep - the confirm session's step: select the next match at/after AX
;             under the Yes/No/Cancel strip, or finish with the count
; in:  AX = where to look from, SI = window ptr, gfx lock held; preserves all
; The strip closes before each replacement and reopens after (SPEC.md 65.7):
; a replacement reflows rows, and lettering them under a modal's pixels would
; corrupt it - the close repair and reopen are the price of a per-match answer.
; -----------------------------------------------------------------------------
wd_cfstep:
    push ax
    push bx
    push dx
    call wd_findfwd
    jc .done
    call wd_showmatch               ; selects it, follow = 1...
    call wd_redraw                  ; ...and this scrolls it visible BEFORE
    mov bx, wd_dlgconf              ; the strip opens over the bottom band
    call wd_dgopen
    jmp short .out
.done:
    mov ax, [wd_cfn]
    call wd_saycnt                  ; 'n changes'
    call wd_redraw
.out:
    pop dx
    pop bx
    pop ax
    ret

; wd_dcyes - confirm strip: replace the selected match, step on
wd_dcyes:
    push ax
    push cx
    push dx
    call wd_dgclose                 ; the strip's pixels go before the rows
                                    ; under them reflow
    mov ax, [wd_fmst]
    mov cx, [wd_fmen]
    sub cx, ax
    call wd_replat                  ; CF = 1 = no room: the session stops with
    jc .stop                        ; everything up to here replaced
    inc word [wd_cfn]
    mov byte [wd_dirty], 1
    mov ax, dx                      ; one past the new text
    push ax
    call wd_selclr
    pop ax
    mov [wd_cur], ax
    call wd_editinv
    call wd_cfstep
    jmp short .out
.stop:
    mov ax, [wd_cfn]
    call wd_saycnt
    call wd_redraw
.out:
    pop dx
    pop cx
    pop ax
    ret

; wd_dcno - confirm strip: keep this one, step past it
wd_dcno:
    push ax
    call wd_dgclose
    mov ax, [wd_fmen]
    call wd_cfstep
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_dgoto - the Go To dialog's OK: page N -> the caret at line (N-1)*54
; in:  SI = window ptr, gfx lock held; preserves all (SPEC.md 65.7)
; -----------------------------------------------------------------------------
wd_dgoto:
    push ax
    push bx
    push cx
    push dx
    call wd_dgclose
    mov bx, wd_de_goto              ; parse the unsigned page number
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
    cmp ax, 999                     ; clamped: page 999 is far past WD_MAXKB
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
    mov cx, WD_PGLINES
    mul cx                          ; AX = the page's first absolute line
                                    ; (<= 999*54: DX = 0)
    mov cx, [wd_drows]              ; clamp to the known height - a lower
    jcxz .rok                       ; bound, so a deep page may land short
    cmp ax, cx                      ; and honestly so (SPEC.md 65.7)
    jb .rok
    mov ax, cx
    dec ax
.rok:
    call wd_settle
    call wd_uclose
    call wd_selclr
    mov byte [wd_ext], 0
    push ax
    call wd_scrollto                ; the target row to the top of the view
    pop ax
    mov byte [wd_rowsok], 0         ; the tables describe the OLD view
    sub ax, [wd_top]                ; the target as a VISIBLE row (>= 0)
    mov [wd_wanty], ax
    mov word [wd_wantx], 0
    mov word [wd_hity], 0xFFFF
    mov dx, ax                      ; ...and the walk's bound
    mov ax, [wd_top]
    call wd_xseed                   ; seed at the view top (SPEC.md 27.13)
    jnc .seeded
    mov ax, [wd_wanty]
    mov [wd_lastrow], ax            ; unseeded: still bounded to the target
.seeded:
    call wd_measure
    mov byte [wd_resume], 0
    mov ax, [wd_wanti]
    cmp ax, 0xFFFF
    jne .cur
    mov ax, [wd_len]                ; past the tail: the end of the document
.cur:
    mov [wd_cur], ax
    mov byte [wd_ckok], 0
    call wd_chpsync
    mov byte [wd_follow], 1
    call wd_redraw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_askdirty - the save-changes gate every destructive door asks (SPEC.md 65.4)
; in:  AL = the pending action (WDP_*), SI = window ptr, gfx lock held
; out: CF = 0 proceed now (not dirty); CF = 1 the prompt is up and the action
;      is deferred to its answer (or the prompt could not fit: deferred to
;      nothing, which is Cancel). Preserves all registers.
; -----------------------------------------------------------------------------
wd_askdirty:
    cmp byte [wd_dirty], 0
    je .go
    push ax
    push bx
    mov [wd_pend], al
    call wd_askmsg                  ; 'Do you want to save changes to <X>?'
    mov bx, wd_dlgask
    call wd_dgopen
    cmp [wd_dlg], bx
    je .up
    mov byte [wd_pend], 0           ; refused (window too small): Cancel
.up:
    pop bx
    pop ax
    stc
    ret
.go:
    clc
    ret

; wd_askmsg - compose the prompt's line around the live name
wd_askmsg:
    push ax
    push si
    push di
    mov si, wd_s_askpre
    mov di, wd_pmsg
    call wd_stcat
    mov si, wd_name
    call wd_stcat
    mov si, wd_s_askpost
    call wd_stcat
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; wd_dgyes - the prompt's Yes: save, and proceed only if it stuck
wd_dgyes:
    call wd_dgclose
    call wd_save                    ; toasts its own outcome
    cmp byte [wd_dirty], 0          ; still dirty = the save failed: the
    jne .stay                       ; action aborts with the reason on screen
    jmp wd_runpend
.stay:
    mov byte [wd_pend], 0
    ret

; wd_runpend - the deferred action, after Yes-saved or No
wd_runpend:
    push ax
    mov al, [wd_pend]
    mov byte [wd_pend], 0
    cmp al, WDP_NEW
    je .new
    cmp al, WDP_OPEN
    je .open
    cmp al, WDP_CLOSE
    je .close
    jmp short .out
.new:
    call wd_donew
    jmp short .out
.open:
    call wd_doopen
    jmp short .out
.close:
    call wd_doclose
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_a_char - Format > Character... (SPEC.md 65.3)
; in:  SI = window ptr, gfx lock held
; -----------------------------------------------------------------------------
wd_a_char:
    push bx
    mov bx, wd_dlgchar
    call wd_dgopen
    pop bx
    ret

; --- the Format Character dialog (char.des, dltCharacter): the seven check
; boxes live, Font/Points/Color and the Position/Character Spacing groups
; present and greyed - facts of the platform (SPEC.md 47/65.3) ----------------
%macro WDDC 7                       ; type, flags, x, y, p1, p2, p3
    db %1, %2
    dw %3, %4, %5, %6, %7
%endmacro

wd_dlgchar:
    dw 440, 128, wd_s_dgchar
    WDDC WDD_CHK, 0,          8,  26, wd_s_dbold,  WDAT_BOLD, 0
    WDDC WDD_CHK, 0,          8,  40, wd_s_dital,  WDAT_ITAL, 0
    WDDC WDD_CHK, 0,          8,  54, wd_s_dkaps,  WDAT_SCAP, 6
    WDDC WDD_CHK, 0,          8,  68, wd_s_dhid,   WDAT_HID,  0
    WDDC WDD_CHK, 0,          8,  82, wd_s_dul,    WDAT_UL,   0
    WDDC WDD_CHK, 0,          8,  96, wd_s_dwul,   WDAT_WUL,  0
    WDDC WDD_CHK, 0,          8, 110, wd_s_ddul,   WDAT_DUL,  0
    WDDC WDD_LBL, WDDF_DIS, 152,  28, wd_s_font,   0, 0
    WDDC WDD_BOX, WDDF_DIS, 210,  26, wd_s_pica,   96, 0
    WDDC WDD_LBL, WDDF_DIS, 152,  46, wd_s_dpts,   0, 0
    WDDC WDD_BOX, WDDF_DIS, 210,  44, wd_s_10,     56, 0
    WDDC WDD_LBL, WDDF_DIS, 152,  64, wd_s_dcol,   0, 0
    WDDC WDD_BOX, WDDF_DIS, 210,  62, wd_s_dauto,  80, 0
    WDDC WDD_GRP, WDDF_DIS, 152,  76, wd_s_dpos,   132, 50
    WDDC WDD_RAD, WDDF_DIS, 158,  88, wd_s_normal, 1, 0
    WDDC WDD_RAD, WDDF_DIS, 158, 100, wd_s_dsup,   0, 0
    WDDC WDD_RAD, WDDF_DIS, 158, 112, wd_s_dsub,   0, 0
    WDDC WDD_GRP, WDDF_DIS, 292,  76, wd_s_dcsp,   140, 50
    WDDC WDD_RAD, WDDF_DIS, 298,  88, wd_s_normal, 1, 0
    WDDC WDD_RAD, WDDF_DIS, 298, 100, wd_s_dexp,   0, 0
    WDDC WDD_RAD, WDDF_DIS, 298, 112, wd_s_dcond,  0, 0
    WDDC WDD_BTN, 0,        328,   4, wd_s_ok,     48, 1
    WDDC WDD_BTN, 0,        384,   4, wd_s_dcanc,  48, 2
    db 0

; -----------------------------------------------------------------------------
; wd_a_para - Format > Paragraph... (SPEC.md 65.3), per para.des: Alignment
; radios in a row, Indents as inch edits (cells = tenths), Spacing (Line as
; the three draft spacings; Before as 0/1 li), OK/Cancel and a greyed Tabs...
; The authentic Keep Paragraph / Border / Pattern / Style groups are OMITTED,
; deliberately: with them the dialog cannot fit a CGA content box, and a
; Format command that refuses on one adapter of three is worse than a
; shortened dialog (SPEC.md 39/47).
; -----------------------------------------------------------------------------
wd_a_para:
    push ax
    push bx
    call wd_dpinit                  ; radios and edits from the caret's
    mov bx, wd_dlgpara              ; paragraph
    call wd_dgopen
    pop bx
    pop ax
    ret

; wd_dpinit - the dialog's controls from the caret paragraph's format
wd_dpinit:
    push ax
    push bx
    push cx
    push dx
    push di
    call wd_rlcalc                  ; AX = state bits; indents in wd_rll/f/r
    mov bx, wd_dp_al0
    call .bit                       ; bits 0..3: the alignment radios
    mov bx, wd_dp_al1
    call .bit
    mov bx, wd_dp_al2
    call .bit
    mov bx, wd_dp_al3
    call .bit
    mov bx, wd_dp_sp0
    call .bit                       ; bits 4..6: the spacing radios
    mov bx, wd_dp_sp1
    call .bit
    mov bx, wd_dp_sp2
    call .bit
    mov bx, wd_dp_sb0
    call .bit                       ; bit 7 closed, bit 8 open
    mov bx, wd_dp_sb1
    call .bit
    mov al, [wd_rll]
    mov di, wd_de_left
    call wd_dfmt
    mov al, [wd_rlr]
    mov di, wd_de_right
    call wd_dfmt
    mov al, [wd_rlf]
    mov di, wd_de_first
    call wd_dfmt
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

; wd_dfmt - signed cells AL -> inches text at DI ('0.5"', '-1.0"')
; preserves all registers
wd_dfmt:
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
    call wd_utoa
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

; wd_dparse - the inches text at BX -> signed cells in AL ('0.5' = 5;
; '-1' = -10; a bare '"' or junk reads as what preceded it). Clamps ride
; wd_cl0max/wd_clfirst at the caller. Preserves everything else.
wd_dparse:
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
    jae .d                          ; clamped: 10" is already WD_INDMAX
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

; wd_dpapply - OK: the radios and parsed edits -> ONE candidate, applied to
; the selection's paragraphs through wd_modpap (SPEC.md 65.3)
wd_dpapply:
    push ax
    push bx
    push dx
    xor dl, dl
    cmp word [wd_dp_al1+8], 0
    je .a2
    or dl, 1
.a2:
    cmp word [wd_dp_al2+8], 0
    je .a3
    or dl, 2
.a3:
    cmp word [wd_dp_al3+8], 0
    je .sp
    or dl, 3
.sp:
    cmp word [wd_dp_sp1+8], 0
    je .sp2c
    or dl, 4                        ; 1.5: spacing value 1 << 2
.sp2c:
    cmp word [wd_dp_sp2+8], 0
    je .sb
    or dl, 8                        ; double: value 2 << 2
.sb:
    cmp word [wd_dp_sb1+8], 0
    je .have
    or dl, WDPA_SB
.have:
    mov [wd_pfb], dl
    mov bx, wd_de_left
    call wd_dparse
    call wd_cl0max
    mov [wd_pfb+1], al
    mov bx, wd_de_right
    call wd_dparse
    call wd_cl0max
    mov [wd_pfb+3], al
    mov bx, wd_de_first
    call wd_dparse
    call wd_clfirst                 ; relative to the left just stored
    mov [wd_pfb+2], al
    mov al, WDPO_SETALL
    xor ah, ah
    call wd_modpap
    pop dx
    pop bx
    pop ax
    ret

; --- the Format Paragraph dialog (para.des, dltParaLooks), compacted -----------
wd_dlgpara:
    dw 448, 134, wd_s_dgpara
    WDDC WDD_GRP, 0,          8,  18, wd_s_dalign, 296, 30
wd_dp_al0:
    WDDC WDD_RAD, 0,         16,  30, wd_s_dleft,  0, 1
wd_dp_al1:
    WDDC WDD_RAD, 0,         76,  30, wd_s_dcent,  0, 1
wd_dp_al2:
    WDDC WDD_RAD, 0,        148,  30, wd_s_dright, 0, 1
wd_dp_al3:
    WDDC WDD_RAD, 0,        212,  30, wd_s_djust,  0, 1
    WDDC WDD_GRP, 0,          8,  52, wd_s_dind,   184, 76
    WDDC WDD_LBL, 0,         16,  65, wd_s_dfleft, 0, 0
wd_dp_el:
    WDDC WDD_EDIT, 0,       112,  62, wd_de_left,  64, 0
    WDDC WDD_LBL, 0,         16,  83, wd_s_dfrgt,  0, 0
wd_dp_er:
    WDDC WDD_EDIT, 0,       112,  80, wd_de_right, 64, 0
    WDDC WDD_LBL, 0,         16, 101, wd_s_dflin,  0, 0
wd_dp_ef:
    WDDC WDD_EDIT, 0,       112,  98, wd_de_first, 64, 0
    WDDC WDD_GRP, 0,        196,  52, wd_s_dspac,  148, 76
    WDDC WDD_LBL, 0,        204,  65, wd_s_dline,  0, 0
wd_dp_sp0:
    WDDC WDD_RAD, 0,        248,  62, wd_s_d1,     0, 2
wd_dp_sp1:
    WDDC WDD_RAD, 0,        280,  62, wd_s_15,     0, 2
wd_dp_sp2:
    WDDC WDD_RAD, 0,        320,  62, wd_s_d2,     0, 2
    WDDC WDD_LBL, 0,        204,  90, wd_s_dbef,   0, 0
wd_dp_sb0:
    WDDC WDD_RAD, 0,        204, 102, wd_s_d0li,   0, 3
wd_dp_sb1:
    WDDC WDD_RAD, 0,        268, 102, wd_s_d1li,   0, 3
    WDDC WDD_BTN, 0,        376,   6, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        376,  26, wd_s_dcanc,  56, 2
    WDDC WDD_BTN, WDDF_DIS, 376,  46, wd_s_dtabs,  56, 0
    db 0

wd_s_dgpara: db 'Paragraph', 0
wd_s_dalign: db 'Alignment', 0
wd_s_dleft:  db 'Left', 0
wd_s_dcent:  db 'Center', 0
wd_s_dright: db 'Right', 0
wd_s_djust:  db 'Justified', 0
wd_s_dind:   db 'Indents', 0
wd_s_dfleft: db 'From Left:', 0
wd_s_dfrgt:  db 'From Right:', 0
wd_s_dflin:  db 'First Line:', 0
wd_s_dspac:  db 'Spacing', 0
wd_s_dline:  db 'Line:', 0
wd_s_d1:     db '1', 0
wd_s_d2:     db '2', 0
wd_s_dbef:   db 'Before:', 0
wd_s_d0li:   db '0 li', 0
wd_s_d1li:   db '1 li', 0
wd_s_dtabs:  db 'Tabs...', 0

wd_s_dgchar: db 'Character', 0
wd_s_dbold:  db 'Bold', 0
wd_s_dital:  db 'Italic', 0
wd_s_dkaps:  db 'Small Kaps', 0     ; sic - char.des's literal spelling
wd_s_dhid:   db 'Hidden', 0
wd_s_dul:    db 'Underline', 0
wd_s_dwul:   db 'Word underline', 0
wd_s_ddul:   db 'Double underline', 0
wd_s_dpts:   db 'Points:', 0
wd_s_dcol:   db 'Color:', 0
wd_s_dauto:  db 'Auto', 0
wd_s_dpos:   db 'Position', 0
wd_s_dsup:   db 'Superscript', 0
wd_s_dsub:   db 'Subscript', 0
wd_s_dcsp:   db 'Character Spacing', 0
wd_s_dexp:   db 'Expanded', 0
wd_s_dcond:  db 'Condensed', 0
wd_s_dcanc:  db 'Cancel', 0
wd_s_dsort:  db 'Sort', 0            ; sort.des
wd_s_dorder: db 'Sort Order', 0
wd_s_dasc:   db 'Ascending', 0
wd_s_ddes:   db 'Descending', 0
wd_s_dkey:   db 'Key Field', 0
wd_s_dktype: db 'Key Type:', 0
wd_s_dalpha: db 'Alphanumeric', 0
wd_s_dnum:   db 'Numeric', 0
wd_s_ddate:  db 'Date', 0            ; greyed: no date parser (SPEC.md 65.9)
wd_s_dsep:   db 'Separator:', 0
wd_s_dcomma: db 'Comma', 0
wd_s_dtab:   db 'Tab', 0
wd_s_dfldn:  db 'Field number:', 0
wd_s_dscol:  db 'Sort Column Only', 0    ; greyed: draft view has no column
wd_s_dcase:  db 'Case Sensitive', 0      ; selection to restrict to
wd_s_drenum: db 'Renumber', 0        ; renum.des
wd_s_drpara: db 'Renumber Paragraphs', 0
wd_s_dall:   db 'All', 0
wd_s_dnumd:  db 'Numbered Only', 0
wd_s_drem:   db 'Remove', 0
wd_s_dauto2: db 'Automatic', 0
wd_s_dman:   db 'Manual:', 0
wd_s_dstart: db 'Start at:', 0
wd_s_dlevels: db 'Show all levels', 0    ; greyed: levels come from the style
wd_s_dfmt:   db 'Format:', 0             ; sheet, and there is none
wd_s_dtoc:   db 'Table of Contents', 0   ; toc.des
wd_s_dtocg:  db 'Table Of Contents', 0
wd_s_dhead:  db 'Use Heading Paragraphs', 0
wd_s_dtcfld: db 'Use Table Entry Fields', 0
wd_s_dfrom:  db 'From:', 0
wd_s_dto:    db 'To:', 0

; --- the Search / Replace / Go To dialogs (search.des / replace.des / the
;     goto dialog, SPEC.md 65.7) and the two Yes/No/Cancel prompts (65.4) ----
wd_dlgsrch:
    dw 448, 96, wd_s_dsrch
    WDDC WDD_LBL, 0,          8,  26, wd_s_dsfor,  0, 0
wd_ds_edit:
    WDDC WDD_EDIT, WDDF_TXT, 100,  24, wd_fpat,    268, WD_EDCAP
    WDDC WDD_CHK, 0,          8,  48, wd_s_dwword, WDFO_WORD, 0
    WDDC WDD_CHK, 0,          8,  64, wd_s_dmcase, WDFO_CASE, 0
    WDDC WDD_GRP, 0,        200,  44, wd_s_ddir,   168, 40
wd_ds_up:
    WDDC WDD_RAD, 0,        210,  58, wd_s_dup,    0, 1
wd_ds_dn:
    WDDC WDD_RAD, 0,        280,  58, wd_s_ddown,  0, 1
    WDDC WDD_BTN, 0,        384,   4, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        384,  24, wd_s_dcanc,  56, 2
    db 0

wd_dlgrepl:
    dw 448, 116, wd_s_drepl
    WDDC WDD_LBL, 0,          8,  26, wd_s_dsfor,  0, 0
wd_dr_edit:
    WDDC WDD_EDIT, WDDF_TXT, 116,  24, wd_fpat,    252, WD_EDCAP
    WDDC WDD_LBL, 0,          8,  44, wd_s_drwith, 0, 0
wd_dr_redit:
    WDDC WDD_EDIT, WDDF_TXT, 116,  42, wd_frep,    252, WD_EDCAP
    WDDC WDD_CHK, 0,          8,  64, wd_s_dwword, WDFO_WORD, 0
    WDDC WDD_CHK, 0,          8,  80, wd_s_dmcase, WDFO_CASE, 0
    WDDC WDD_CHK, 0,          8,  96, wd_s_dconf,  WDFO_CONF, 0
    WDDC WDD_BTN, 0,        384,   4, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        384,  24, wd_s_dcanc,  56, 2
    db 0

wd_dlggoto:
    dw 320, 64, wd_s_dgo
    WDDC WDD_LBL, 0,          8,  30, wd_s_dpgnum, 0, 0
wd_dg_edit:
    WDDC WDD_EDIT, 0,       112,  28, wd_de_goto,  56, 6
    WDDC WDD_BTN, 0,        184,  28, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        248,  28, wd_s_dcanc,  56, 2
    db 0

; --- the Utilities dialogs (sort.des / renum.des / toc.des, SPEC.md 65.9) ---
; The .des files' own strings and grouping. Their drop-down lists are RADIOS
; here - this framework has no live list box (SPEC.md 65.3), and a radio row
; says the same thing about a choice of three; the greyed controls are the
; ones whose fact this port does not have (a column selection, a style sheet).
wd_dlgsort:
    dw 448, 168, wd_s_dsort
    WDDC WDD_GRP, 0,          8,  24, wd_s_dorder, 360, 34
wd_so_asc:
    WDDC WDD_RAD, 0,         18,  40, wd_s_dasc,   1, 1
wd_so_des:
    WDDC WDD_RAD, 0,        140,  40, wd_s_ddes,   0, 1
    WDDC WDD_GRP, 0,          8,  64, wd_s_dkey,   360, 76
    WDDC WDD_LBL, 0,         16,  84, wd_s_dktype, 0, 0
wd_so_alp:
    WDDC WDD_RAD, 0,         92,  82, wd_s_dalpha, 1, 2
wd_so_num:
    WDDC WDD_RAD, 0,         92,  96, wd_s_dnum,   0, 2
    WDDC WDD_RAD, WDDF_DIS,  92, 110, wd_s_ddate,  0, 2
    WDDC WDD_LBL, 0,        216,  84, wd_s_dsep,   0, 0
wd_so_com:
    WDDC WDD_RAD, 0,        288,  82, wd_s_dcomma, 1, 3
wd_so_tab:
    WDDC WDD_RAD, 0,        288,  96, wd_s_dtab,   0, 3
    WDDC WDD_LBL, 0,        216, 112, wd_s_dfldn,  0, 0
wd_so_fld:
    WDDC WDD_EDIT, 0,       320, 110, wd_de_fld,   40, 4
    WDDC WDD_CHK, WDDF_DIS,   8, 146, wd_s_dscol,  1, 0
    WDDC WDD_CHK, 0,        216, 146, wd_s_dcase,  2, 0
    WDDC WDD_BTN, 0,        380,   6, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        380,  26, wd_s_dcanc,  56, 2
    db 0

wd_dlgrenum:
    dw 448, 148, wd_s_drenum
    WDDC WDD_GRP, 0,          8,  24, wd_s_drpara, 360, 34
wd_rn_all:
    WDDC WDD_RAD, 0,         18,  40, wd_s_dall,   1, 1
wd_rn_numd:
    WDDC WDD_RAD, 0,         80,  40, wd_s_dnumd,  0, 1
wd_rn_rem:
    WDDC WDD_RAD, 0,        216,  40, wd_s_drem,   0, 1
wd_rn_auto:
    WDDC WDD_RAD, 0,         18,  70, wd_s_dauto2, 1, 2
wd_rn_man:
    WDDC WDD_RAD, 0,        140,  70, wd_s_dman,   0, 2
    WDDC WDD_LBL, 0,         18,  96, wd_s_dstart, 0, 0
wd_rn_st:
    WDDC WDD_EDIT, 0,        96,  94, wd_de_start, 48, 5
    WDDC WDD_CHK, WDDF_DIS,  18, 120, wd_s_dlevels, 1, 0
    WDDC WDD_LBL, 0,        216,  96, wd_s_dfmt,   0, 0
wd_rn_fmt:
    WDDC WDD_EDIT, WDDF_TXT, 280,  94, wd_rnfmt,    80, 10
    WDDC WDD_BTN, 0,        380,   6, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        380,  26, wd_s_dcanc,  56, 2
    db 0

wd_dlgtoc:
    dw 448, 124, wd_s_dtoc
    WDDC WDD_GRP, 0,          8,  24, wd_s_dtocg,  360, 84
wd_tc_head:                         ; the two SOURCE radios stay adjacent in
    WDDC WDD_RAD, 0,         18,  40, wd_s_dhead,  1, 1
wd_tc_fld:                          ; the descriptor - wd_radset addresses a
    WDDC WDD_RAD, 0,         18,  88, wd_s_dtcfld, 0, 1
wd_tc_all:                          ; PAIR by its first record, and where a
    WDDC WDD_RAD, 0,         34,  62, wd_s_dall,   1, 2
wd_tc_rng:                          ; control PAINTS is its x/y, not its
    WDDC WDD_RAD, 0,         96,  62, wd_s_dfrom,  0, 2
wd_tc_f:                            ; place in the list
    WDDC WDD_EDIT, 0,       168,  60, wd_de_from,  32, 2
    WDDC WDD_LBL, 0,        216,  62, wd_s_dto,    0, 0
wd_tc_t:
    WDDC WDD_EDIT, 0,       248,  60, wd_de_to,    32, 2
    WDDC WDD_BTN, 0,        380,   6, wd_s_ok,     56, 1
    WDDC WDD_BTN, 0,        380,  26, wd_s_dcanc,  56, 2
    db 0

wd_dlgask:                          ; 'Do you want to save changes to X?' -
    dw 384, 68, wd_ttl              ; titled with the product name, as the
    WDDC WDD_LBL, 0,          8,  24, wd_pmsg,     0, 0
    WDDC WDD_BTN, 0,         80,  44, wd_s_dyes,   64, 1
    WDDC WDD_BTN, 0,        160,  44, wd_s_dno,    64, 3
    WDDC WDD_BTN, 0,        240,  44, wd_s_dcanc,  64, 2
    db 0                            ; real message box was (SPEC.md 65.4)

wd_dlgconf:                         ; the confirm strip, pinned to the bottom
    dw 384, 44, wd_s_drepl          ; of the content (SPEC.md 65.7)
    WDDC WDD_LBL, 0,          8,  22, wd_s_dconfq, 0, 0
    WDDC WDD_BTN, 0,        208,  18, wd_s_dyes,   48, 1
    WDDC WDD_BTN, 0,        264,  18, wd_s_dno,    48, 3
    WDDC WDD_BTN, 0,        320,  18, wd_s_dcanc,  56, 2
    db 0

wd_s_dsrch:  db 'Search', 0
wd_s_drepl:  db 'Replace', 0
wd_s_dgo:    db 'Go To', 0
wd_s_dsfor:  db 'Search For:', 0
wd_s_drwith: db 'Replace With:', 0
wd_s_dwword: db 'Whole Word', 0
wd_s_dmcase: db 'Match Upper/Lowercase', 0
wd_s_dconf:  db 'Confirm Changes', 0
wd_s_ddir:   db 'Direction', 0
wd_s_dup:    db 'Up', 0
wd_s_ddown:  db 'Down', 0
wd_s_dpgnum: db 'Page Number:', 0
wd_s_dyes:   db 'Yes', 0
wd_s_dno:    db 'No', 0
wd_s_dconfq: db 'Replace this occurrence?', 0
wd_s_askpre: db 'Do you want to save changes to ', 0
wd_s_askpost: db '?', 0
wd_m_nfound: db 'Search text not found', 0
wd_m_baddoc: db 'Bad .DOC file', 0

; =============================================================================
; The menu data (SPEC.md 65.2) - verbatim from Opus resource/menus.cmd
; (MW_MENU, the full-menus bar). '&' marked the mnemonic in the source; here
; the display string is plain, the mnemonic's cell index and its letter ride
; the item record, and the shortcut captions are keys.cmd's bindings for
; exactly the commands these items invoke - no caption is invented.
;
; Descriptor row (8 bytes): bar start cell, bar cell count, title mnemonic
; index, item count; dw items, panel width px. Item record (8 bytes): flags,
; mnemonic index, action, mnemonic letter; dw label, caption (0 = none).
; =============================================================================
%macro WDMI 6                       ; flags, mn index, action, mn letter,
    db %1, %2, %3, %4               ; label, caption
    dw %5, %6
%endmacro
%macro WDMS 0                       ; a separator
    db WDMF_SEP, 0, WDA_NONE, 0
    dw 0, 0
%endmacro

wd_mtab:
    db 0,  4, 0, 14
    dw wd_it_file, 208
    db 5,  4, 0, 15
    dw wd_it_edit, 144
    db 10, 4, 0, 13
    dw wd_it_view, 128
    db 15, 6, 0, 13
    dw wd_it_ins, 176
    db 22, 6, 5, 12
    dw wd_it_fmt, 144
    db 29, 9, 0, 11
    dw wd_it_util, 168
    db 39, 5, 0, 5
    dw wd_it_mac, 152
    db 45, 6, 0, 4
    dw wd_it_win, 128
    db 52, 4, 0, 9
    dw wd_it_help, 120
    db 0xFF, 0, 0, 1                ; the ribbon's Font combo (WD_M_FONTC)
    dw wd_it_fontc, WD_RB_FBW
    db 0xFF, 0, 0, 1                ; ...its Pts combo
    dw wd_it_ptsc, WD_RB_PBW
    db 0xFF, 0, 0, 1                ; ...and the ruler's Style combo
    dw wd_it_stylec, WD_RL_SBW

; the bar itself: one string, one opaque run; cells 0..55
wd_s_mbar: db 'File Edit View Insert Format Utilities Macro Window Help', 0

; Alt+letter scan codes, File..Help order (int 16h: Alt+letter = ascii 0 +
; the letter key's scan code)
wd_alttab: db 0x21, 0x12, 0x2F, 0x17, 0x14, 0x16, 0x32, 0x11, 0x23

wd_it_file:                         ; &File
    WDMI 0,        0, WDA_NEW,    'N', wd_L_new,    0
    WDMI 0,        0, WDA_OPEN,   'O', wd_L_open,   wd_C_cf12
    WDMI 0,        0, WDA_CLOSE,  'C', wd_L_close,  0
    WDMI 0,        0, WDA_SAVE,   'S', wd_L_save,   wd_C_sf12
    WDMI 0,        5, WDA_SAVEAS, 'A', wd_L_saveas, wd_C_f12
    WDMI WDMF_DIS, 3, WDA_NONE,   'E', wd_L_saveall, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'F', wd_L_find,   0
    WDMS
    WDMI WDMF_DIS, 0, WDA_NONE,   'P', wd_L_print,  wd_C_csf12
    WDMI WDMF_DIS, 9, WDA_NONE,   'V', wd_L_prview, 0
    WDMI WDMF_DIS, 6, WDA_NONE,   'M', wd_L_prmerge, 0
    WDMI WDMF_DIS, 1, WDA_NONE,   'R', wd_L_prsetup, 0
    WDMS
    WDMI 0,        1, WDA_EXIT,   'X', wd_L_exit,   wd_C_af4

wd_it_edit:                         ; &Edit
    WDMI 0,        0, WDA_UNDO,   'U', wd_L_undo,   wd_C_abksp
    WDMI WDMF_DIS, 0, WDA_NONE,   'R', wd_L_repeat, wd_C_f4
    WDMI 0,        2, WDA_CUT,    'T', wd_L_cut,    wd_C_sdel
    WDMI 0,        0, WDA_COPY,   'C', wd_L_copy,   wd_C_cins
    WDMI 0,        0, WDA_PASTE,  'P', wd_L_paste,  wd_C_sins
    WDMI WDMF_DIS, 6, WDA_NONE,   'L', wd_L_plink,  0
    WDMS
    WDMI 0,        0, WDA_SEARCH, 'S', wd_L_search, 0
    WDMI 0,        1, WDA_REPL,   'E', wd_L_replace, 0
    WDMI 0,        0, WDA_GOTO,   'G', wd_L_goto,   wd_C_f5
    WDMS
    WDMI WDMF_DIS, 0, WDA_NONE,   'H', wd_L_hdrftr, 0
    WDMI WDMF_DIS, 8, WDA_NONE,   'I', wd_L_suminfo, 0
    WDMI WDMF_DIS, 2, WDA_NONE,   'O', wd_L_glossary, 0
    WDMI WDMF_DIS, 1, WDA_NONE,   'A', wd_L_table,  0

wd_it_view:                         ; &View
    WDMI WDMF_DIS, 0, WDA_NONE,   'O', wd_L_outline, 0
    WDMI WDMF_CHK, 0, WDA_DRAFT,  'D', wd_L_draft,  0
    WDMI WDMF_CHK, 0, WDA_PAGE,   'P', wd_L_page,   0
    WDMS
    WDMI WDMF_CHK, 2, WDA_VRIB,   'B', wd_L_ribbon, 0
    WDMI WDMF_CHK, 0, WDA_VRUL,   'R', wd_L_ruler,  0
    WDMI WDMF_CHK, 0, WDA_VSTA,   'S', wd_L_stabar, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'F', wd_L_footns, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'A', wd_L_annots, 0
    WDMS
    WDMI WDMF_DIS, 6, WDA_NONE,   'C', wd_L_fldcode, 0
    WDMI WDMF_DIS, 2, WDA_NONE,   'E', wd_L_prefs,  0
    WDMI WDMF_DIS, 6, WDA_NONE,   'M', wd_L_shortm, 0

wd_it_ins:                          ; &Insert
    WDMI WDMF_DIS, 0, WDA_NONE,   'B', wd_L_break,  0
    WDMI WDMF_DIS, 4, WDA_NONE,   'N', wd_L_footnote, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'F', wd_L_file,   0
    WDMI WDMF_DIS, 4, WDA_NONE,   'M', wd_L_bookmark, 0
    WDMI WDMF_DIS, 6, WDA_NONE,   'U', wd_L_pagenum, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'T', wd_L_table,  0
    WDMS
    WDMI WDMF_DIS, 0, WDA_NONE,   'A', wd_L_annot,  0
    WDMI WDMF_DIS, 0, WDA_NONE,   'P', wd_L_picture, 0
    WDMI WDMF_DIS, 4, WDA_NONE,   'D', wd_L_field,  0
    WDMI WDMF_DIS, 6, WDA_NONE,   'E', wd_L_ixentry, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'I', wd_L_index,  0
    WDMI 0,        9, WDA_TOC,    'C', wd_L_toc,    0

wd_it_fmt:                          ; Forma&t
    WDMI 0,        0, WDA_CHAR,   'C', wd_L_charctr, 0
    WDMI 0,        0, WDA_PARA,   'P', wd_L_para,   0
    WDMI WDMF_DIS, 0, WDA_NONE,   'S', wd_L_section, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'D', wd_L_documnt, 0
    WDMS
    WDMI WDMF_DIS, 0, WDA_NONE,   'T', wd_L_tabs,   0
    WDMI WDMF_DIS, 2, WDA_NONE,   'Y', wd_L_styles, 0
    WDMI WDMF_DIS, 1, WDA_NONE,   'O', wd_L_positn, 0
    WDMS
    WDMI WDMF_DIS, 2, WDA_NONE,   'F', wd_L_defsty, 0
    WDMI WDMF_DIS, 5, WDA_NONE,   'R', wd_L_picture, 0
    WDMI WDMF_DIS, 1, WDA_NONE,   'A', wd_L_table,  0

wd_it_util:                         ; &Utilities
    WDMI WDMF_DIS, 0, WDA_NONE,   'S', wd_L_spell,  0
    WDMI WDMF_DIS, 0, WDA_NONE,   'T', wd_L_thesaur, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'H', wd_L_hyphen, 0
    WDMS
    WDMI 0,        0, WDA_RENUM,  'R', wd_L_renum,  0
    WDMI WDMF_DIS, 9, WDA_NONE,   'M', wd_L_revmark, 0
    WDMI WDMF_DIS, 8, WDA_NONE,   'V', wd_L_compver, 0
    WDMI 0,        1, WDA_SORT,   'O', wd_L_sort,   0
    WDMI WDMF_DIS, 0, WDA_NONE,   'C', wd_L_calc,   0
    WDMI WDMF_DIS, 2, WDA_NONE,   'P', wd_L_repag,  0
    WDMI WDMF_DIS, 1, WDA_NONE,   'U', wd_L_custom, 0

wd_it_mac:                          ; &Macro
    WDMI WDMF_DIS, 2, WDA_NONE,   'C', wd_L_record, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'R', wd_L_run,    0
    WDMI WDMF_DIS, 0, WDA_NONE,   'E', wd_L_edit,   0
    WDMI WDMF_DIS, 10, WDA_NONE,  'K', wd_L_asgkey, 0
    WDMI WDMF_DIS, 10, WDA_NONE,  'M', wd_L_asgmenu, 0

wd_it_win:                          ; &Window
    WDMI WDMF_DIS, 0, WDA_NONE,   'N', wd_L_newwin, 0
    WDMI WDMF_DIS, 0, WDA_NONE,   'A', wd_L_arrange, 0
    WDMS
    WDMI WDMF_CHK, 0, WDA_WIN1,   '1', wd_win1,     0

wd_it_help:                         ; &Help
    WDMI WDMF_DIS, 0, WDA_NONE,   'I', wd_L_index2, 0
                                    ; Index stays greyed: no help files exist
                                    ; on this platform - F1 and About... are
                                    ; the help there is (SPEC.md 47/65.2)
    WDMS
    WDMI WDMF_DIS, 0, WDA_NONE,   'K', wd_L_keybrd, 0
    WDMI WDMF_DIS, 7, WDA_NONE,   'W', wd_L_actwin, 0
    WDMS
    WDMI WDMF_DIS, 0, WDA_NONE,   'T', wd_L_tutor,  0
    WDMI WDMF_DIS, 6, WDA_NONE,   'H', wd_L_usehelp, 0
    WDMS
    WDMI 0,        0, WDA_ABOUT,  'A', wd_L_about,  0

wd_it_fontc:                        ; Pica, and room for what FONTS/ carries
    WDMI 0, 0, WDA_CSEL, 0, wd_s_pica, 0
%rep WD_MAXFONT                     ; RESERVED, and filled by wd_fontscan
    WDMI WDMF_DIS, 0, WDA_NONE, 0, wd_s_pica, 0
%endrep
wd_it_ptsc:                         ; ...at the one size
    WDMI 0, 0, WDA_CSEL, 0, wd_s_10, 0
wd_it_stylec:                       ; ...in the one style
    WDMI 0, 0, WDA_CSEL, 0, wd_s_normal, 0

; --- the labels (menus.cmd, '&' removed) -------------------------------------
wd_L_new:      db 'New...', 0
wd_L_open:     db 'Open...', 0
wd_L_close:    db 'Close', 0
wd_L_save:     db 'Save', 0
wd_L_saveas:   db 'Save As...', 0
wd_L_saveall:  db 'Save All', 0
wd_L_find:     db 'Find...', 0
wd_L_print:    db 'Print...', 0
wd_L_prview:   db 'Print Preview', 0
wd_L_prmerge:  db 'Print Merge...', 0
wd_L_prsetup:  db 'Printer Setup...', 0
wd_L_exit:     db 'Exit', 0
wd_L_undo:     db 'Undo', 0
wd_L_repeat:   db 'Repeat', 0
wd_L_cut:      db 'Cut', 0
wd_L_copy:     db 'Copy', 0
wd_L_paste:    db 'Paste', 0
wd_L_plink:    db 'Paste Link...', 0
wd_L_search:   db 'Search...', 0
wd_L_replace:  db 'Replace...', 0
wd_L_goto:     db 'Go To...', 0
wd_L_hdrftr:   db 'Header/Footer...', 0
wd_L_suminfo:  db 'Summary Info...', 0
wd_L_glossary: db 'Glossary...', 0
wd_L_table:    db 'Table...', 0
wd_L_outline:  db 'Outline', 0
wd_L_draft:    db 'Draft', 0
wd_L_page:     db 'Page', 0
wd_L_ribbon:   db 'Ribbon', 0
wd_L_ruler:    db 'Ruler', 0
wd_L_stabar:   db 'Status Bar', 0
wd_L_footns:   db 'Footnotes', 0
wd_L_annots:   db 'Annotations', 0
wd_L_fldcode:  db 'Field Codes', 0
wd_L_prefs:    db 'Preferences...', 0
wd_L_shortm:   db 'Short Menus', 0
wd_L_break:    db 'Break...', 0
wd_L_footnote: db 'Footnote...', 0
wd_L_file:     db 'File...', 0
wd_L_bookmark: db 'Bookmark...', 0
wd_L_pagenum:  db 'Page Numbers...', 0
wd_L_annot:    db 'Annotation', 0
wd_L_picture:  db 'Picture...', 0
wd_L_field:    db 'Field...', 0
wd_L_ixentry:  db 'Index Entry...', 0
wd_L_index:    db 'Index...', 0
wd_L_toc:      db 'Table of Contents...', 0
wd_L_charctr:  db 'Character...', 0
wd_L_para:     db 'Paragraph...', 0
wd_L_section:  db 'Section...', 0
wd_L_documnt:  db 'Document...', 0
wd_L_tabs:     db 'Tabs...', 0
wd_L_styles:   db 'Styles...', 0
wd_L_positn:   db 'Position...', 0
wd_L_defsty:   db 'Define Styles...', 0
wd_L_spell:    db 'Spelling...', 0
wd_L_thesaur:  db 'Thesaurus...', 0
wd_L_hyphen:   db 'Hyphenate...', 0
wd_L_renum:    db 'Renumber...', 0
wd_L_revmark:  db 'Revision Marks...', 0
wd_L_compver:  db 'Compare Versions...', 0
wd_L_sort:     db 'Sort...', 0
wd_L_calc:     db 'Calculate', 0
wd_L_repag:    db 'Repaginate Now', 0
wd_L_custom:   db 'Customize...', 0
wd_L_record:   db 'Record...', 0
wd_L_run:      db 'Run...', 0
wd_L_edit:     db 'Edit...', 0
wd_L_asgkey:   db 'Assign to Key...', 0
wd_L_asgmenu:  db 'Assign to Menu...', 0
wd_L_newwin:   db 'New Window', 0
wd_L_arrange:  db 'Arrange All', 0
wd_L_index2:   db 'Index', 0
wd_L_keybrd:   db 'Keyboard', 0
wd_L_actwin:   db 'Active Window', 0
wd_L_tutor:    db 'Tutorial', 0
wd_L_usehelp:  db 'Using Help', 0
wd_L_about:    db 'About...', 0

; --- the shortcut captions (keys.cmd via SetBcmMenuKeys's Alt+/Ctrl+/Shift+
;     spelling; only commands the keymap actually binds carry one) -----------
wd_C_cf12:  db 'Ctrl+F12', 0
wd_C_sf12:  db 'Shift+F12', 0
wd_C_f12:   db 'F12', 0
wd_C_csf12: db 'Ctrl+Shift+F12', 0
wd_C_af4:   db 'Alt+F4', 0
wd_C_abksp: db 'Alt+BkSp', 0
wd_C_f4:    db 'F4', 0
wd_C_sdel:  db 'Shift+Del', 0
wd_C_cins:  db 'Ctrl+Ins', 0
wd_C_sins:  db 'Shift+Ins', 0
wd_C_f5:    db 'F5', 0

; --- ribbon / ruler / status strings and glyph tables ------------------------
wd_s_font:   db 'Font:', 0
wd_s_pica:   db 'Pica', 0           ; the 8x8 face at fixed pitch: pica is
                                    ; the honest name for 10 cpi
wd_s_pts:    db 'Pts:', 0
wd_s_10:     db '10', 0
wd_s_style:  db 'Style:', 0
wd_s_normal: db 'Normal', 0
wd_s_15:     db '1.5', 0
wd_agl:                             ; alignment glyphs: 4 rows of (x1,x2)
    db 2,9, 2,6, 2,9, 2,5           ; flush left
    db 2,9, 3,8, 2,9, 4,7           ; centered
    db 2,9, 5,9, 2,9, 6,9           ; flush right
    db 2,9, 2,9, 2,9, 2,9           ; justified
wd_tgl:                             ; tab glyphs: stem x, foot x1, foot x2
    db 3, 3, 8                      ; left tab
    db 5, 2, 8                      ; center tab
    db 7, 2, 7                      ; right tab
    db 5, 2, 8                      ; decimal tab (its point is drawn on top)
wd_ptick: times 8 db 0x7F           ; gfx_fill_pat rows: leftmost pixel of
                                    ; every byte black = a tick every 8px
wd_s_pg:    db 'Pg ', 0
wd_s_sec:   db '  Sec 1  ', 0
wd_s_at:    db '  At ', 0
wd_s_li:    db 'li', 0
wd_s_ln:    db '  Ln ', 0
wd_s_col:   db '  Col ', 0
wd_s_ext:   db 'EXT', 0
wd_s_caps:  db 'CAPS', 0
wd_s_num:   db 'NUM', 0
wd_s_ovr:   db 'OVR', 0
wd_s_sp4:   db '    ', 0
wd_s_sp3:   db '   ', 0
wd_s_about: db 'Microsoft Word', 0
wd_s_abou2: db 'Version 1.1a for os8088', 0
wd_s_abou3: db 'UI from the CHM Opus source release', 0
wd_s_ok:    db 'OK', 0
wd_m_noclose: db 'Close refused - try again', 0

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; A word processor's frame, not a note pad's: 600x440 at the 640x480
; reference, clamped onto smaller adapters by wm_create (SPEC.md 11). The
; chrome strips cost 60px of content, so a small frame would leave the
; document a letterbox.
wd_tpl:
    dw 20, 24, 600, 440
    dw wd_wttl, wd_paint, wd_onkey, wd_onclick
                                    ; the TITLE is the bss buffer wd_compttl
                                    ; composes - 'Microsoft Word - <NAME>' -
                                    ; before wm_create banks the pointer, and
                                    ; wd_settitle recomposes it on every name
                                    ; change (SPEC.md 65.2)

; The EMPTY kernel menu set (SPEC.md 12.2): zero menus, but its AM_NAME puts
; 'Microsoft Word' in the kernel bar instead of the 15-char header name. The
; command handler can never be called (there is nothing to pick); wd_mf_ret
; satisfies the layout.
    OS88_MENUSET wd_menus0, wd_ttl, wd_mf_ret
    OS88_MENUSET_END wd_menus0

wd_ttl: db 'Microsoft Word', 0      ; the kernel bar's AM_NAME (14 chars,
                                    ; inside the 15); the TITLE BAR shows
                                    ; wd_wttl's 'Microsoft Word - <NAME>'
wd_s_tpre: db 'Microsoft Word - ', 0

%ifdef WDBENCH
; The walk bench (`make wdbench`), and the ONLY thing that reaches it in this
; file is the Ctrl-B in wd_onkey and the four WDVAR words at the foot - both
; inside %ifdef WDBENCH, so WORD.O88 carries not a byte of it.
%include "wdbench.inc"
%endif

; --- the file and what the toast can say (SPEC.md 27.1) ------------------------
; The name is per-instance state now (wd_name in bss), seeded from this at
; launch and replaced by whatever the file dialog returns. The two verbs are
; PREFIXES: wd_setmsg composes them with the live name into wd_tbuf, because
; a toast that still said DOCUMENT.DOC after a Save As would be worse than no
; toast at all.
wd_s_default: db 'DOCUMENT.DOC', 0  ; Word's Document1, folded to 8.3: a 9-char
                                    ; stem is FERR_NAME on a FAT volume
                                    ; (SPEC.md 24), and a default that cannot
                                    ; save is worse than a shorter one
wd_m_saved:   db 'Saved ', 0
wd_m_loaded:  db 'Loaded ', 0
wd_m_trunc:   db 'Truncated', 0
wd_s_nul:     db 0              ; an EMPTY string retires whatever is up
                                ; (SPEC.md 59.3) - one call, and this app
                                ; never has to know whether one was

; FERR_* (SPEC.md 18.4) -> string, indexed by the code itself
wd_errtab:
    dw wd_e_ok, wd_e_nodisk, wd_e_io, wd_e_name, wd_e_noent, wd_e_exist
    dw wd_e_full, wd_e_dirfull, wd_e_prot, wd_e_wprot, wd_e_big
wd_e_ok:      db 'Done', 0
wd_e_nodisk:  db 'No disk', 0
wd_e_io:      db 'Disk error', 0
wd_e_name:    db 'Bad name', 0
wd_e_noent:   db 'Not found', 0
wd_e_exist:   db 'Name exists', 0
wd_e_full:    db 'Disk full', 0
wd_e_dirfull: db 'Dir full', 0
wd_e_prot:    db 'Protected', 0
wd_e_wprot:   db 'Write protected', 0
wd_e_big:     db 'Too big', 0
wd_e_nomem:   db 'No memory', 0      ; the staging claim was refused (50.3)

; --- search and the clipboard (SPEC.md 65.7/55) ------------------------------
wd_m_nopat:   db 'No search text', 0
wd_m_repld:   db ' changes', 0       ; wd_saycnt's suffix: 'n changes' is the
                                     ; sweep's answer (SPEC.md 65.7)
wd_m_noundo:  db 'Nothing to undo', 0
wd_m_papfull: db 'Too many paragraph formats', 0
wd_m_toobig:  db 'Document too complex to save', 0
wd_m_noclip:  db 'The clipboard is empty', 0        ; ^c with nothing in it
wd_m_replong: db 'Replacement too long', 0          ; ...and ^m/^c past
                                     ; WD_FRXMAX (SPEC.md 65.7)
wd_m_loaded2: db 'Module loaded from WORD.OVL', 0
wd_m_toomanyp: db 'Too many paragraphs', 0          ; past WD_UMAXP (65.9)
wd_m_noundo2: db 'Too large to undo', 0             ; the span is bigger than
                                     ; the undo blob, so the command refuses
                                     ; rather than do it unundoably (65.9)
wd_m_notoc:   db 'No table of contents entries', 0
wd_m_sorted:  db 'Sorted', 0
wd_m_renumd:  db 'Renumbered', 0   ; more FKP pages than
                                     ; the staging claim holds (SPEC.md 65.4)
wd_e_cbig:    db 'Too big to copy', 0   ; over CLIP_MAXKB, or the heap could
                                        ; not fund the clipboard (SPEC.md 55)

; --- the real Word file format (SPEC.md 65.4) --------------------------------
; The FIB, the FKP pages, the bin tables, the style sheet, the section table,
; the DOP and the piece table - a separate file because it is a FORMAT, laid
; out from Opus's own headers, and reads as one.
%include "wddoc.inc"

; --- RTF, the other portable format (SPEC.md 65.8) ---------------------------
%include "wdrtf.inc"

; --- the search pattern and the Utilities commands (SPEC.md 65.7/65.9) -------
%include "wdutil.inc"

; =============================================================================
; THE ON-DEMAND MODULE (SPEC.md 65.10)
;
; Code that ships as WORD.OVL beside WORD.O88 instead of inside it, is read
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
; wd_* variable through it exactly as resident code does. That is what makes
; moving a subsystem out a matter of moving its text, rather than rewriting
; every data reference in it. What it costs instead is the two rules below,
; and tools/os88ovlchk.py refuses a build that breaks the first.
;
;   1. A call from the module to resident code CANNOT be near - the module has
;      a CS of its own. It goes through a VECTOR: `call far [wd_v_*]`, a
;      dword of (offset, segment) filled in at load. Resident code calling
;      INTO the module goes the other way, through wd_ovcall.
;   2. Nothing in the module may assume CS. Its data lives in .text with
;      everything else, which is also what keeps rule 1 to code alone.
;
; Loading is the shape drivers/hdd/hdtool.inc uses (SPEC.md 52.11) and it
; needs no kernel byte: OSAPI_FILE_HERE / _GOTO to reach the package's own
; folder, OSAPI_MEM_CLAIM for the image, OSAPI_FILE_READ to fill it. A
; refusal - no heap, or the disk swapped for one without WORD.OVL - is an
; ordinary path: the feature says what is missing and the document is
; untouched (SPEC.md 47).
; =============================================================================

WD_OVKB      equ 8              ; the claim WORD.OVL is read into, KB
WDM_PING     equ 0              ; verbs, the module's dispatch indices
WDM_MAX      equ 0              ; ...and the highest of them

; -----------------------------------------------------------------------------
; wd_ovneed - make sure WORD.OVL is loaded
; out: CF=0 with [wd_ovseg] the claim holding it; CF=1 and the toast already
;      says why. Preserves all registers.
;
; UI TASK ONLY: it claims and it reads a floppy, and SPEC.md 20.6 rule 7
; forbids both on a worker. [wd_inwk] is the same gate wd_itinit uses.
; -----------------------------------------------------------------------------
wd_ovneed:
    cmp word [wd_ovseg], 0
    jne .ok
    cmp byte [wd_inwk], 0
    jne .nowk
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE            ; where the USER is, to be put back
    mov [wd_ovwas], dx
    mov [wd_ovwdr], bl
    mov dx, [wd_ovdir]              ; ...and off to where we were launched
    mov bl, [wd_ovdrv]
    call OSAPI_FILE_GOTO
    mov ax, WD_OVKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [wd_ovseg], dx
    mov word [wd_ovfar], 0          ; the far pointer wd_ovcall goes through:
    mov [wd_ovfar+2], dx            ; the dispatcher is at the claim's 0
    mov es, dx
    xor bx, bx
    mov cx, WD_OVKB * 1024
    xor dx, dx
    mov si, wd_ovname
    call OSAPI_FILE_READ
    jc .noread
    call wd_ovbind                  ; fill the vectors with our own segment
    call wd_ovback
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
    mov dx, [wd_ovseg]              ; the claim goes back: a half-loaded
    call OSAPI_MEM_FREE             ; module is worse than none
    mov word [wd_ovseg], 0
    mov ax, wd_m_noovl
    jmp short .say
.nomem:
    mov ax, wd_e_nomem
.say:
    call wd_saymsg
    call wd_ovback
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

; wd_ovback - put the volume back where the user left it. Preserves all.
wd_ovback:
    push bx
    push dx
    mov dx, [wd_ovwas]
    mov bl, [wd_ovwdr]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    ret

; wd_ovbind - stamp this package's segment into every vector. Preserves all.
; The offsets are assembled in; only the segment is a runtime fact, and it is
; the same one for all of them.
wd_ovbind:
    push ax
    push bx
    mov ax, cs
    mov bx, wd_v_first
.l:
    mov [bx+2], ax
    add bx, 4
    cmp bx, wd_v_end
    jb .l
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_ovcall - far-call the module
; in:  BP = the verb (WDM_*), everything else as the verb documents
; out: as the verb; CF=1 if the module could not be loaded at all
; -----------------------------------------------------------------------------
wd_ovcall:
    call wd_ovneed
    jc .no                          ; CF=1 already, and it has said why
    call far [wd_ovfar]             ; the dispatcher is at the claim's offset
                                    ; 0, so the far pointer is (0, the claim)
                                    ; and the verb's own CF comes back through
.no:
    ret

wd_ovname:  db 'WORD.OVL', 0
wd_m_noovl: db 'WORD.OVL is not on this disk', 0

; --- the vectors: resident routines the module far-calls (rule 1) ------------
; Offset assembled in, segment stamped by wd_ovbind. They are contiguous and
; wd_ovbind walks them, so adding one is two lines and no other change.
;
; Each points at a SHIM, never at the routine itself. Every resident routine
; in this package is a near proc ending in `ret` (SPEC.md 20.1 - a package
; author never writes `retf`), so far-calling one directly pops the offset,
; leaves the segment on the stack and returns into nothing. The shim is the
; one place that difference lives: a near call to the real routine, then the
; far return the module's call is waiting for. Registers and flags pass
; through both ways untouched, so a shimmed routine keeps its own contract.
wd_s_saymsg:
    call wd_saymsg
    retf

wd_v_first:
wd_v_saymsg:  dw wd_s_saymsg
              dw 0
wd_v_end:

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

wd_modc:                            ; +0: the dispatcher, and the only offset
    cmp bp, WDM_MAX                 ; the resident half knows
    ja .out
    shl bp, 1
    mov si, bp
    jmp word [cs:si+wd_mverb]
.out:
    retf

wd_mverb:
    dw wd_m_ping

; wd_m_ping - what the mechanism has actually been PROVEN to do, and it is
; deliberately not more than that. Measured on the emulator, in this order:
; WORD.OVL is found beside WORD.O88 and read into a claim; the far call lands
; on this dispatcher at the module's offset 0; the verb table dispatches; and
; the retf comes back to wd_ovcall with the app healthy afterwards. A far call
; OUT through wd_v_* to a shim that only returns was proven the same way.
;
; WHAT IS NOT PROVEN, and must be before any feature moves out here: a shim
; whose resident routine touches the UI. Calling wd_saymsg through wd_s_saymsg
; froze the app, and every static explanation was checked and cleared - the
; vector holds the shim's address, wd_ovbind stamps the right segment, the
; string offset AX carries is the string, and the shim's near call lands
; exactly on wd_saymsg at 0x46E5 (the displacement wraps 64K, which is legal
; and correct). So the cause is dynamic and still unknown, and the honest
; place to record that is here rather than in a commit message.
wd_m_ping:
    retf

section .text

; --- the state SPEC.md 27.8/27.9/27.10 added ---------------------------------
; The block below the image (further down this file) is ~120 hand-computed
; `equ os88_image_end + N` lines, and that was survivable while it grew a word
; at a time. Selection, undo, the clipboard glue and the find panel add forty
; fields at once, so these are laid out by a PREPROCESSOR COUNTER instead:
; WDVAR emits the same equ and moves the counter on, and WD_BSS_TOTAL falls
; out of where it stops. Nothing about the older fields changed - they are
; still where they were - and the two blocks cannot collide, because this one
; starts at the byte the other one ends on.
;
; It is here, ABOVE OS88_BSS, because OS88_BSS needs the total and the counter
; is the total. os88_image_end is still a forward reference at this point,
; which is exactly what every line of code referencing these fields already
; relies on.
%assign WDB 508 + WD_MAXROWS*2      ; where the original block ends
%macro WDVAR 2                      ; name, size in bytes
    %1 equ os88_image_end + WDB
    %assign WDB WDB + %2
%endmacro

; --- the selection (SPEC.md 27.8) --------------------------------------------
    WDVAR wd_sel0,  2       ; word: the first selected character index...
    WDVAR wd_sel1,  2       ; word: ...and one past the last. sel1 > sel0
                            ; whenever [wd_selon] is set, and the pair is
                            ; ORDERED here so nothing downstream has to ask
    WDVAR wd_anchor, 2      ; word: where a drag started, which is the end
                            ; that does NOT move while the pointer does
    WDVAR wd_selon, 1       ; byte: a selection exists
    WDVAR wd_rs0,   2       ; word } the CELLS of the row being accumulated
    WDVAR wd_rs1,   2       ; word } that fall inside it, 0xFFFF = none. The
                            ; inversion is per row, like the caret's column,
                            ; because that is the unit wd_rflush draws in
    WDVAR wd_osel0, 2       ; word } the selection the SCREEN is showing, as
    WDVAR wd_osel1, 2       ; word } character indices, and whether it has one
    WDVAR wd_oselon, 1      ; byte }
    WDVAR wd_selonly, 1     ; byte: this redraw cannot have changed a
                            ; character - only the selection moved - so a row
                            ; owes an INVERSION and no glyphs at all. One-shot
    WDVAR wd_xs0,   2       ; word } the cells of the row being accumulated
    WDVAR wd_xs1,   2       ; word } whose selected-ness DIFFERS from what the
                            ; screen shows: the symmetric difference, which is
                            ; exactly what one XOR has to flip
    WDVAR wd_prs0,  2       ; word } and what the delta cache's row was last
    WDVAR wd_prs1,  2       ; word } DRAWN with, so a selection that moved
                            ; over unchanged text still redraws (SPEC.md 27.8)
    WDVAR wd_dpos,  2       ; word: a drag-and-drop's insertion point, or
                            ; 0xFFFF when no marker is on screen
    WDVAR wd_dmark, 2       ; word } the marker's x and y, banked so the XOR
    WDVAR wd_dmy,   2       ; word } that erases it cannot disagree with the
                            ; XOR that drew it - a scroll between the two
                            ; would leave the old bar on screen for good
    WDVAR wd_dpx,   2       ; word } where the press landed, for the dead-zone
    WDVAR wd_dpy,   2       ; word } test that keeps a click a click
    WDVAR wd_mvs,   2       ; word } wd_movesel's four numbers. In bss rather
    WDVAR wd_mve,   2       ; word } than in registers because the three
    WDVAR wd_mvp,   2       ; word } reversals want all four at once and the
    WDVAR wd_mvn,   2       ; word } 8086 has nowhere to put them

; --- undo (SPEC.md 27.9) -----------------------------------------------------
; Five records, oldest first, and a heap arena holding their blobs packed in
; the same order. A record says: at [wd_upos], this group INSERTED [wd_uins]
; bytes and REMOVED the [wd_udel] bytes at [wd_uoff] in the arena. Undoing it
; is therefore "delete uins at upos, insert the blob at upos" and nothing else
; - which is why there is no redo and no second representation.
    WDVAR wd_upos, WD_UNDO*2
    WDVAR wd_uins, WD_UNDO*2
    WDVAR wd_udel, WD_UNDO*2
    WDVAR wd_uoff, WD_UNDO*2
                            ; there is no "caret before the group" word, and
                            ; there was one until it turned out to be derived:
                            ; an undo puts the caret at upos + udel, the end
                            ; of what it just put back, and that is right for
                            ; all four shapes a group can have - a typing run
                            ; (udel 0, so upos: where you started), a
                            ; backspace run (the position you backspaced
                            ; from), a cut and a paste
    WDVAR wd_useg, 2        ; word: the arena claim, 0 = none held. Claimed on
                            ; the first edit worth remembering and given back
                            ; by File > New, a load and wd_uclear
    WDVAR wd_ukb,  2        ; word: its size in KB
    WDVAR wd_utop, 2        ; word: bytes of it in use - also the top record's
                            ; blob end, which is what makes an append free
    WDVAR wd_un,   1        ; byte: records live, 0..WD_UNDO
    WDVAR wd_uopen, 1       ; byte: the newest is still ACCUMULATING. Closed
                            ; by WD_IDLE ticks of not editing (the worker), by
                            ; anything that is not an edit, and by an edit
                            ; that does not touch the group's own span
    WDVAR wd_unolog, 1      ; byte: suppress recording - set while undo itself
                            ; is editing the buffer, or the undo of an undo
                            ; would be recorded as an edit
    WDVAR wd_upad, 1        ; byte: keeps the words below even

; --- search / replace / go to (SPEC.md 65.7) ---------------------------------
    WDVAR wd_lmx, 2         ; word } the drag loop's last pointer position
    WDVAR wd_lmy, 2         ; word } (wd_dragsel's has-it-moved filter)
    WDVAR wd_fopt, 1        ; byte: the search options, WDFO_* bits - Whole
                            ; Word, Match Upper/Lowercase, Confirm Changes.
                            ; Persistent: the dialogs re-open with the last
                            ; answers, and F4 repeats with them
    WDVAR wd_fdir, 1        ; byte: Direction - 0 down, 1 up
    WDVAR wd_fpatn, 2       ; word: characters in the search text...
    WDVAR wd_frepn, 2       ; word: ...and in the replacement
    WDVAR wd_fmst,  2       ; word } the match the selection is showing - the
    WDVAR wd_fmen,  2       ; word } confirm session replaces exactly this
    WDVAR wd_rxend, 2       ; word: one past the match wd_matchat found
    WDVAR wd_cfn,   2       ; word: replacements made this sweep (the toast)
    WDVAR wd_fpat,  WD_PATMAX+1
    WDVAR wd_frep,  WD_PATMAX+1

; --- the file, the dirty prompt and the title (SPEC.md 65.4) -----------------
    WDVAR wd_dirty, 1       ; byte: the document differs from the disk. Set
                            ; by every buffer/format mutation, cleared by a
                            ; successful save, a load and File > New
    WDVAR wd_ovr,   1       ; byte: overtype (Ins; the status OVR lamp)
    WDVAR wd_ext,   1       ; byte: extend-selection mode (F8; the EXT lamp)
    WDVAR wd_pend,  1       ; byte: what the dirty prompt's Yes/No proceed to
                            ; (WDP_*), 0 = nothing pending
    WDVAR wd_wttl,  32      ; 'Microsoft Word - ' + wd_name + NUL: the title
                            ; the window record points at, so it must live
                            ; for the window's whole life (SPEC.md 65.2)
    WDVAR wd_pmsg,  46      ; 'Do you want to save changes to <name>?'
    WDVAR wd_de_goto, 8     ; the Go To dialog's page-number edit
    WDVAR wd_fsz,   2       ; word } the dialog listing's file size, banked
    WDVAR wd_fszh,  2       ; word } by wd_ondlg for the open gate (65.4)
; --- word wrap (SPEC.md 27.11) -----------------------------------------------
    WDVAR wd_wstart, 1      ; byte: the index the walk is standing on begins a
                            ; word, so wd_wordfit has a question to answer.
                            ; Maintained by the walk itself - set at its start,
                            ; by a space and by a line break, cleared by every
                            ; other character - because the alternative is
                            ; reading the character BEFORE the one in hand,
                            ; which a seeded walk cannot always do

; --- the chunked height count (SPEC.md 27.7.3) -------------------------------
; Where the count has got to, and where a bounded walk stopped. The two are
; separate because wd_walk's .stop is shared by every bounded walk in the
; module - a paint, a caret key - and only wd_height may keep what it reports.
    WDVAR wd_hrow,  2       ; word: the ABSOLUTE row the next chunk resumes at,
                            ; 0 = from the top. Absolute and not visible,
                            ; because [wd_top] may move between chunks and the
                            ; seed wd_walk wants is relative to wherever the
                            ; view is when the chunk actually runs
    WDVAR wd_hi,    2       ; word: the character index that row begins at.
                            ; wd_seedrow cannot supply this - wd_rows is
                            ; WD_MAXROWS long, one slot per VISIBLE row, so it
                            ; describes the view and can never name row 300 of
                            ; a 500-row note (SPEC.md 27.5)
    WDVAR wd_sowed, 1       ; byte: a scroll moved [wd_top] and its repaint was
                            ; dropped, because OSAPI_EVQ_PENDING said another
                            ; event was right behind it. The worker owes it
                            ; (SPEC.md 27.7.8) - and it must be the WORKER
                            ; rather than the next click, because the next
                            ; event in the queue may belong to another window
                            ; and this one would then never be drawn at all
    WDVAR wd_stoprow, 2     ; word } what .stop reached, published every time
    WDVAR wd_stopi, 2       ; word } and read only by wd_height, on the walk it
                            ; issued itself - under the lock, with nothing
                            ; between the call and the read

; --- the row index (SPEC.md 27.13) -------------------------------------------
; 134 bytes, and it is what makes a row OUTSIDE the view reachable without
; laying out the note to get to it. Entry n is the character index at which
; absolute row n << [wd_xksh] begins; the stride doubles rather than the table
; growing, so this is the whole cost however long the note is.
    WDVAR wd_xi, WD_XN * 2  ; the indices, one per checkpoint
    WDVAR wd_xn, 2          ; word: how many are valid, CONTIGUOUSLY from 0 -
                            ; a hole would answer for a row nobody walked
    WDVAR wd_xnext, 2       ; word: the absolute row the next entry wants, kept
                            ; rather than derived so wd_xnote is one compare
    WDVAR wd_xksh, 1        ; byte: log2 of the stride. A log, because a lookup
                            ; is then a shift and not a 150-clock `div`
    WDVAR wd_xpad, 1        ; byte: keep the words that follow even

; --- the append fast path (SPEC.md 27.14) ------------------------------------
    WDVAR wd_ap1, 2         ; the one character wd_append letters, NUL-capped:
                            ; OSAPI_FONT_RUN takes a string and this is the
                            ; whole of it. Not wd_rbuf, which is the row cache
                            ; wd_rflush diffs against and must not be disturbed
    WDVAR wd_apch, 1        ; ...the character on its own, because AL is spent
    WDVAR wd_aprow, 1       ; the tail is a hard NEWLINE, not the end of the
                            ; note - so rows below have shifted an index
    WDVAR wd_apx,  2        ; word } the cell's x, the caret's x one along, and
    WDVAR wd_apx2, 2        ; word } the row's y - banked because the far calls
    WDVAR wd_apy,  2        ; word } between them return nothing of their own

; --- how deep a caret move can dirty (SPEC.md 27.4.1) ------------------------
    WDVAR wd_mvbot, 2       ; word: the DEEPER of the two visible rows a caret
                            ; move touched - the one it left and the one it
                            ; arrived on - or 0x7FFF for "not a caret move, or
                            ; nowhere in particular". Written by wd_move, which
                            ; is the only caller that knows both; wd_fastcm
                            ; parks it at the sentinel, so Left and Right (kind
                            ; 4 as well, and adjacent rows they do not measure)
                            ; can never inherit an Up's bound

; --- the proportional face, SPEC.md 65.13 ------------------------------------
    WDVAR wd_px, (WD_MAXCOL + 2) * 2  ; word per cell: the pen the k-th cell
                                  ; starts at, RELATIVE to [wd_tx], plus one
                                  ; past the end so a span's right edge is a
                                  ; lookup rather than a special case. This is
                                  ; the whole of what a proportional row costs
                                  ; over a fixed one - in a fixed face the
                                  ; k-th cell is at 8k and nothing needs
                                  ; storing, which is why wd_cx branches
                                  ; rather than always reading this
    WDVAR wd_rcn, 2               ; word: cells stored in the row so far. In a
                                  ; fixed face the cell index comes from the
                                  ; PEN - (di - tx) >> 3 - and a tab therefore
                                  ; leaves empty cells behind it; proportional,
                                  ; there is no such division, so the index is
                                  ; counted here instead and a tab leaves none
    WDVAR wd_prop, 1              ; byte: 1 = set the document in [wd_face],
                                  ; 0 = the kernel's 8x8 cell. THE DEFAULT IS
                                  ; 0 and bss arrives zeroed, so a build with
                                  ; no face file, or a machine whose kernel
                                  ; refuses the band blit, behaves exactly as
                                  ; this program did before any of this
    WDVAR wd_face, 1              ; byte: the ty_open handle, 0 = none opened
    WDVAR wd_fname, 16            ; the family name for the Font combo
    WDVAR wd_fcap, 2              ; word: what the ribbon's Font box SHOWS -
                                  ; wd_s_pica until a face is chosen
    WDVAR wd_fsel, 1              ; byte: the chosen family, 0-based
    WDVAR wd_nfont, 1             ; byte: families the scan listed, clamped
    WDVAR wd_fscan, 1             ; byte: the scan has run (once, lazily)
    WDVAR wd_pickm, 1             ; byte } which menu a chosen item came out
    WDVAR wd_picki, 1             ; byte } of, and which item it was
    WDVAR wd_redrw, 1             ; byte: this action owes the note a redraw,
                                  ; taken on the way out as a tail jump
    WDVAR wd_pxon, 1              ; byte: wd_px[] is the truth about where a
                                  ; cell sits. 0 = the 8-pixel grid, which is
                                  ; what wd_cx computes without reading it
    WDVAR wd_bx0,  2              ; word: wd_bandrun's aligned band origin
    WDVAR wd_gh,   2              ; word: the GLYPH BAND's height in pixels -
                                  ; 8 for the kernel's cell, the face's `rows`
                                  ; for a chosen one. Every site that used to
                                  ; add 7 to a row's y to reach its last row
                                  ; reads wd_gh1 instead
    WDVAR wd_gh1,  2              ; word: ...that, less one
    WDVAR wd_ghb,  2              ; word: and the ROW ADVANCE's base - the band
                                  ; plus the face's leading, which line spacing
                                  ; is then added to (wd_papload)

    WDVAR wd_rbuf, WD_MAXCOL + 1  ; the row being accumulated, space-filled
    WDVAR wd_prow, WD_MAXCOL      ; ...and what was last DRAWN on the cached
                                  ; row, so the next keystroke draws the delta.
                                  ; THE ONLY TWO FIELDS SIZED BY WD_MAXCOL,
                                  ; which is why they are here rather than in
                                  ; the hand-numbered block above (see the hole
                                  ; at +238 there)

; --- the chrome's state (SPEC.md 65.2) ---------------------------------------
    WDVAR wd_cl,   2        ; word } the content box, banked by wd_bounds so
    WDVAR wd_ct,   2        ; word } every strip painter and hit test reads
    WDVAR wd_cw,   2        ; word } the same four numbers
    WDVAR wd_ch,   2        ; word }
    WDVAR wd_ctop, 2        ; word: the LIVE chrome top (wd_ctcalc)
    WDVAR wd_sctop, 2       ; word: ...and the one the screen was drawn under
                            ; (banked by wd_sigmark, read by wd_panmove)
    WDVAR wd_vrib, 1        ; byte } the View toggles: ribbon, ruler, status
    WDVAR wd_vrul, 1        ; byte } bar shown. Set to 1 in wd_entry - bss
    WDVAR wd_vsta, 1        ; byte } zero would mean all three hidden
    WDVAR wd_mopen, 1       ; byte: the open dropdown, WD_M_NONE = none.
                            ; 0..8 the bar, 9..11 the strip combos
    WDVAR wd_mhi,  1        ; byte: the XOR-highlighted item, 0xFF = none
    WDVAR wd_about, 1       ; byte: the About box is up (modal)
    WDVAR wd_quit, 1        ; byte: File > Close/Exit ran - the worker
                            ; finishes the teardown (wd_worker .quit)
    WDVAR wd_stok, 1        ; byte: wd_stold describes the strip
    WDVAR wd_stkf, 1        ; byte: the CAPS/NUM bits the lamps show
    WDVAR wd_mpad, 1        ; byte: keeps the words below even
    WDVAR wd_mrx1, 2        ; word } the open dropdown's rectangle, computed
    WDVAR wd_mry1, 2        ; word } once by wd_mgeo and read by painter, hit
    WDVAR wd_mrx2, 2        ; word } test, highlight and close repaint alike
    WDVAR wd_mry2, 2        ; word } (the fm_hit discipline)
    WDVAR wd_mabox, 8       ; 4 words: the gesture anchor - the bar title's
                            ; band or the combo's box (os88ui rect order)
    WDVAR wd_max,  2        ; word } where a combo's dropdown hangs: its
    WDVAR wd_may,  2        ; word } box's left edge and the strip's bottom
    WDVAR wd_abrect, 8      ; 4 words: the About box...
    WDVAR wd_abok, 8        ; 4 words: ...and its OK button
    WDVAR wd_lnv,  2        ; word } the status line's source: the caret's
    WDVAR wd_colv, 2        ; word } absolute line and 1-based column, kept
                            ; across redraws whose walk never stood on it
    WDVAR wd_rlxe, 2        ; word: the ruler scale's right end (digit loop)
    WDVAR wd_stbuf, WD_ST_CELLS+2 ; the status text being composed...
    WDVAR wd_stold, WD_ST_CELLS+2 ; ...and what the strip SHOWS - wd_stdiff
                            ; letters only the cells that differ
    WDVAR wd_win1, 16       ; the Window menu's live item: '1 ' + wd_name
    WDVAR wd_mbbuf, 58      ; the menu bar's truncated copy, narrow windows

; --- character formatting (SPEC.md 65.1/65.3) --------------------------------
    WDVAR wd_cseg,  2       ; word: the CHP claim's segment - one attribute
                            ; byte per character at [wd_cseg]:index, sized in
                            ; lockstep with [wd_dseg] by wd_resize. Claimed in
                            ; wd_entry, so it is never 0 while we live
    WDVAR wd_chp,   1       ; byte: the TYPING attributes - what the next
                            ; character wears. Re-read from the character
                            ; left of the caret on every caret move without
                            ; a selection (Word's rule, wd_chpsync)
    WDVAR wd_cattr, 1       ; byte: the walk's per-character attr scratch
    WDVAR wd_showall, 1     ; byte: Show-all - the pilcrow toggle
    WDVAR wd_rbold, 1       ; byte: the attr byte the ribbon's cells SHOW
                            ; (bit 7 = the pilcrow cell), delta-cached
    WDVAR wd_rbok,  1       ; byte: wd_rbold describes drawn cells at all
    WDVAR wd_runa,  1       ; byte: the run being drawn - its attribute
    WDVAR wd_hashid, 1      ; byte: HIDDEN has been applied somewhere this
                            ; document's life. Conservative and set-only
                            ; (cleared by wd_clamp): a hidden character
                            ; occupies no cell, so "column = index - row
                            ; start" stops holding, and the two paths that
                            ; lean on it - wd_append and the visual break -
                            ; refuse while this is up (SPEC.md 65.1)
    WDVAR wd_r0,    2       ; word } the run's first and last cell, for
    WDVAR wd_r1,    2       ; word } wd_drawrun and the italic stager
    WDVAR wd_iseg,  2       ; word: the sheared italic glyph table's claim,
                            ; 0 = not built yet (wd_itinit, first use)
    WDVAR wd_inwk,  1       ; byte: the WORKER is inside its lock-held draw
                            ; burst. wd_itinit reads it and refuses to claim
                            ; (SPEC.md 20.6 rule 7: no OSAPI_MEM_* from a
                            ; worker): a worker pass that meets italic text
                            ; before any UI draw built the table letters it
                            ; plain, and the next UI redraw builds it. Set
                            ; and cleared under the lock, so the UI's own
                            ; draw paths never see it raised
    WDVAR wd_itsg,  2       ; word } OSAPI_FONT_GLYPHS' table, banked across
    WDVAR wd_itso,  2       ; word } the build loop
    WDVAR wd_itch,  2       ; word: the glyph being built
    WDVAR wd_itstr, 2       ; word: the staged run's stride in bytes
    WDVAR wd_itmp,  8       ; one source glyph's rows, banked so the font
                            ; table and the claim are never open at once
    WDVAR wd_cuseg, 2       ; word: the undo arena's CHP twin (SPEC.md 65.3)
                            ; - same size, same offsets as [wd_useg]
    WDVAR wd_abuf, WD_MAXCOL + 1  ; the accumulated row's attributes, one per
                            ; cell beside wd_rbuf
    WDVAR wd_pattr, WD_MAXCOL     ; ...and the attributes the cached row was
                            ; last DRAWN with, diffed beside wd_prow
                            ; (the italic run's staged 4bpp image used to be
                            ; here: (WD_MAXCOL-1)*4*8 = 5,440 bytes of bss.
                            ; It lives at WD_STG4 inside the italic claim
                            ; now - see the constant for why)
    WDVAR wd_pseg,  2       ; word: the PAP dictionary's claim (256 x 4 bytes,
                            ; zeroed at entry so entry 0 IS Normal)
    WDVAR wd_papn,  2       ; word: entries live, 1..WD_PAPMAX
    WDVAR wd_pap_tail, 1    ; byte: the LAST paragraph's index (it has no ¶)
    WDVAR wd_hasfmt, 1      ; byte: some paragraph wears a non-Normal format.
                            ; Set-only until wd_clamp, like wd_hashid: while
                            ; clear, every walk runs the old uniform-8px path
                            ; with not one added memory reference
    WDVAR wd_hastab, 1      ; byte: a tab lives in the document - the two
                            ; column-arithmetic fast paths (wd_append, the
                            ; visual break) stand down, like wd_hashid
    WDVAR wd_parafirst, 1   ; byte: the row being entered is its paragraph's
                            ; FIRST row (walk state)
    WDVAR wd_wpap,  1       ; byte: the governing PAP index at the walk's pen
    WDVAR wd_walign, 1      ; byte } the decoded entry, clamped against the
    WDVAR wd_wadv,  1       ; byte } live geometry by wd_papload: alignment,
    WDVAR wd_wsb,   1       ; byte } row advance px (8/12/16), space-before
    WDVAR wd_wleftpx, 2     ; word } left indent px, first-line px (signed),
    WDVAR wd_wfirstpx, 2    ; word } and the effective right edge and fresh-
    WDVAR wd_wrgt,  2       ; word } row cell capacity the wrap rule reads
    WDVAR wd_wcols, 2       ; word } instead of [wd_rgt]/[wd_rcols]
    WDVAR wd_rowhv, 2       ; word: the entered row's height in px
    WDVAR wd_rowx0, 2       ; word: the row's start pen x (indent + alignment
                            ; offset) - the tab stops' anchor
    WDVAR wd_rbandt, 2      ; word: the row's BAND top (glyph y - (h-8)), the
                            ; hit query's lower edge - a click in the leading
                            ; gap of a spaced row belongs to that row
    WDVAR wd_currow, 2      ; word: the caret's VISIBLE row, signed, banked by
                            ; wd_ask beside wd_curx/wd_cury
    WDVAR wd_vfit,  2       ; word: rows guaranteed to fit the band - vrows
                            ; while uniform, band/24 when formatted (24px is
                            ; the tallest row: double spacing + open)
    WDVAR wd_ryb, WD_MAXROWS*2  ; each visible row's GLYPH y as drawn - the
                            ; heights array in prefix form: band top of row r
                            ; is ryb[r-1]+8 (r=0: wd_ty). Written beside
                            ; wd_rows/wd_sig, shifted with them by
                            ; wd_shiftrows, compared by pass 1 so a row whose
                            ; pixels MOVED is dirty even when its text did not
    WDVAR wd_sdpx,  2       ; word: wd_scrollpaint's pixel delta (signed),
                            ; wd_shiftrows adjusts the shifted ryb by it
    WDVAR wd_ymoved, 1      ; byte: pass 1 saw a row change its y - the band
                            ; repaint must ERASE [wd_ymv0..wd_ymv1] first
                            ; (glyph runs only self-erase at their own y)
    WDVAR wd_ympad, 1
    WDVAR wd_ymv0,  2       ; word } the union of old and new extents of the
    WDVAR wd_ymv1,  2       ; word } rows that moved
    WDVAR wd_pfb,   4       ; the 4-byte PAP candidate wd_papfind takes
    WDVAR wd_ppop,  1       ; byte } wd_modpap's operation and argument
    WDVAR wd_pparg, 1       ; byte } (arg is signed where the op says so)
    WDVAR wd_ppstop, 1      ; byte: the dictionary refused mid-span - stop
    WDVAR wd_pptl,  1       ; byte: the span reaches the TAIL paragraph
    WDVAR wd_entpap, 1      ; byte: the pap an Enter's new ¶ inherits
    WDVAR wd_rlold, 2       ; word: the ruler cells' pressed bits as drawn
                            ; (bit 0..3 align, 4..6 spacing, 7 close, 8 open)
    WDVAR wd_rlok,  1       ; byte: wd_rlold/marker caches describe the strip
    WDVAR wd_rmleft, 1      ; byte } the marker positions the scale shows:
    WDVAR wd_rmfirst, 1     ; byte } left, first (signed), right - in cells,
    WDVAR wd_rmright, 1     ; byte } delta-cached like the cells
    WDVAR wd_rgx,   2       ; word: the marker drag's XOR guide x, 0xFFFF none
    WDVAR wd_rll,   1       ; byte } wd_rlcalc's live answer: the caret
    WDVAR wd_rlf,   1       ; byte } paragraph's left / first (signed) /
    WDVAR wd_rlr,   1       ; byte } right indents, in cells
    WDVAR wd_rdl,   1       ; byte: the drag's banked left indent
    WDVAR wd_rdz,   2       ; word: ...and the scale's zero x
    WDVAR wd_dgfoc, 2       ; word: the focused WDD_EDIT record, 0 = none
    WDVAR wd_de_left, 8     ; the three edit buffers: 'From Left:' etc.,
    WDVAR wd_de_right, 8    ; NUL-terminated, 7 chars max ('-10.0"')
    WDVAR wd_de_first, 8
    WDVAR wd_de_bef, 8      ; 'Before:' - 0 or 1 (li)

; --- the modal dialog (SPEC.md 65.3): Format > Character... ------------------
    WDVAR wd_dlg,   2       ; word: the open dialog's descriptor, 0 = none
    WDVAR wd_dlgx,  2       ; word } its top-left corner on screen, banked
    WDVAR wd_dlgy,  2       ; word } at open - painter and hit test share it
    WDVAR wd_dlrect, 8      ; 4 words: the covered rect, for the close repair
    WDVAR wd_dck,   1       ; byte: the attr byte the check boxes are editing
    WDVAR wd_dpad,  1       ; byte: keeps the words below even
    WDVAR wd_dgr,   8       ; 4 words: a button rect being drawn/hit

; --- the real .DOC format (wddoc.inc, SPEC.md 65.4) --------------------------
    WDVAR wd_dgrp,  24      ; a grpprl under construction. Six paragraph
                            ; sprms at three bytes is 18; WD_GRPMAX is the
                            ; same number stated where the builder is
    WDVAR wd_dprop, 32      ; ...and the FKP property record built from it
                            ; (a PAPX adds a cw byte, an stc and a 6-byte PHE)
    WDVAR wd_dfkp,  512     ; the 512-byte FKP page being built. Built HERE
                            ; and copied into the image whole, because in
                            ; place it needs two pointers growing towards
                            ; each other and that arithmetic is the entire
                            ; risk of the structure (SPEC.md 65.4)
    WDVAR wd_dfkn,  2       ; word: its crun
    WDVAR wd_dfkhi, 2       ; word: how far DOWN its properties have got
    WDVAR wd_dfkb,  128     ; its rgb offset bytes, BANKED: rgb[i] lives at
                            ; 4*(crun+1) + i and crun is not final until the
                            ; page is sealed, so storing one as it is made
                            ; stores it where the next run moves it from
    WDVAR wd_dbtfc, 128     ; WD_MAXBTE words: each FKP page's first fc, for
                            ; the bin table written at the end
    WDVAR wd_dbtn,  2       ; word: how many of them
    WDVAR wd_dbn0,  2       ; word } the CHP and PAP page counts, kept apart
    WDVAR wd_dbn1,  2       ; word } because the second pass reuses the array
    WDVAR wd_dpn0,  2       ; word } ...and the page number each run starts at
    WDVAR wd_dpn1,  2       ; word }
    WDVAR wd_dend,  2       ; word: one past the run/paragraph just measured
    WDVAR wd_dvlen, 2       ; word: [wd_len] plus the trailing ¶ the FILE has
                            ; and this document model does not (SPEC.md 65.4)
    WDVAR wd_dfsize, 2      ; word: the size of the file being read
    WDVAR wd_dtrunc, 1      ; byte: it was bigger than this port can hold
    WDVAR wd_dtail, 1       ; byte: the tail paragraph's PAP index, banked
                            ; across wd_clamp - which clears the live one
    WDVAR wd_dpcn,  2       ; word: how many pieces the text is in (1 for a
                            ; simple file - SPEC.md 65.4.1)
    WDVAR wd_dpcp,  2       ; word } the plcpcd's CP array and PCD array,
    WDVAR wd_dpcd,  2       ; word } as offsets INTO the staging image
    WDVAR wd_dp1fc, 2       ; word: a simple file's one piece's fc
    WDVAR wd_dccp,  2       ; word: the document's total length in characters
    WDVAR wd_dprem, 2       ; word: characters left in the piece being painted
    WDVAR wd_dfclim, 2      ; word: the limit fc of the run wd_dfkfind found
    WDVAR wd_dprop2, 2      ; word: ...and its property record's file offset

; --- RTF in and out (wdrtf.inc, SPEC.md 65.8) --------------------------------
    WDVAR wd_rfull, 1       ; byte: the writer ran out of staging claim
    WDVAR wd_rat,   1       ; byte: the attributes the writer has OPEN, or
                            ; the ones the reader is applying
    WDVAR wd_rnew,  1       ; byte: a paragraph is about to start
    WDVAR wd_rpad,  1       ; byte: keeps the words below even
    WDVAR wd_rdep,  2       ; word: the reader's group depth
    WDVAR wd_rskip, 2       ; word: the depth a skipped destination began at,
                            ; 0 = not skipping (SPEC.md 65.8)
    WDVAR wd_rstk,  32      ; WD_RTFDEPTH bytes of saved attributes, one per
                            ; open group, so a \b's scope ends with its group
                            ; (padded to a word each, which keeps the indexing
                            ; a single shift)
    WDVAR wd_rcw,   20      ; the control word being read, NUL-terminated
    WDVAR wd_fpw,   94      ; the COMPILED search pattern (SPEC.md 65.7),
                            ; one word per element: a value 0..255 is that
                            ; character, WDP_ANY and WDP_WHITE the wildcards
    WDVAR wd_fpwn,  2       ; word: how many elements it has
    WDVAR wd_frx,   WD_FRXMAX   ; the EXPANDED replacement: ^m and ^c are
                            ; resolved per match, so the bytes spliced in are
                            ; never the ones the dialog holds
    WDVAR wd_frxn,  2       ; word: how many of them
; --- the Utilities commands (wdutil.inc, SPEC.md 65.9) -----------------------
    WDVAR wd_us0,   2       ; word } the span Sort / Renumber / Table of
    WDVAR wd_us1,   2       ; word } Contents operate on, snapped out to
                            ; whole paragraphs
    WDVAR wd_upst,  258     ; WD_UMAXP+1 words: each paragraph's start, plus
                            ; a sentinel at [wd_us1] - so a paragraph's
                            ; length is the NEXT entry and no second array
                            ; exists to disagree with this one
    WDVAR wd_upn,   2       ; word: how many paragraphs are in it
    WDVAR wd_uord,  256     ; WD_UMAXP words: the sort's order array
    WDVAR wd_scseg, 2       ; word: the rebuild scratch claim, 0 = none
    WDVAR wd_scn,   2       ; word: how many characters are in it
    WDVAR wd_vpage, 1       ; byte: 0 = Draft, 1 = Page (SPEC.md 65.11)
    WDVAR wd_vppad, 1       ; byte: keeps the words below even
    WDVAR wd_shl,   2       ; word } the sheet's left and right pixel edges,
    WDVAR wd_shr,   2       ; word } banked by wd_bounds for the painters
    WDVAR wd_ovseg, 2       ; word: WORD.OVL's claim, 0 = not loaded yet
    WDVAR wd_ovfar, 4       ; the (offset, segment) wd_ovcall far-calls: an
                            ; 8086 has no `call far reg:reg`, so the pointer
                            ; lives in memory and DS reaches it
    WDVAR wd_ovdir, 2       ; word } the folder this package was LAUNCHED
    WDVAR wd_ovdrv, 1       ; byte } from, banked at entry (SPEC.md 65.10)
    WDVAR wd_ovwdr, 1       ; byte } ...and where the USER had the volume,
    WDVAR wd_ovwas, 2       ; word } banked across the load and put back
    WDVAR wd_utl,   1       ; byte: the span runs to the document's end with
                            ; no closing ¶, so the rebuilt one must not have
                            ; one either (SPEC.md 65.9)
    WDVAR wd_sopt,  1       ; byte: the Sort dialog's options, WDSO_*
    WDVAR wd_sofld, 2       ; word: its Field number, 1-based
    WDVAR wd_sokb,  2       ; word } the second key of a comparison, banked
    WDVAR wd_soke,  2       ; word } because wd_sonum walks SI..DI
    WDVAR wd_rnact, 1       ; byte: Renumber's All / Numbered Only / Remove
    WDVAR wd_rnpad, 1       ; byte: keeps the words below even
    WDVAR wd_rnfrom, 2      ; word: the Start at value
    WDVAR wd_rnnow, 2       ; word: ...and the running number
    WDVAR wd_rnfmt, 12      ; the Format edit's text, NUL-terminated
    WDVAR wd_tcsrc, 1       ; byte: TOC source, 0 = headings / 1 = fields
    WDVAR wd_tcpst, 1       ; byte: the walk is standing on a paragraph start
    WDVAR wd_tcfrom, 2      ; word } the level range From..To
    WDVAR wd_tcto,  2       ; word }
    WDVAR wd_tcn,   2       ; word: entries collected
    WDVAR wd_tcline, 2      ; word } the collector's own row counter and the
    WDVAR wd_tccol, 2       ; word } column inside the row it is on
    WDVAR wd_de_fld, 8      ; the Utilities dialogs' numeric edit buffers
    WDVAR wd_de_start, 8
    WDVAR wd_de_from, 8
    WDVAR wd_de_to,  8
    WDVAR wd_rnman, 1       ; byte: Renumber's Automatic / Manual
    WDVAR wd_tclvl, 1       ; byte: the TOC's All / From..To

    WDVAR wd_rpp,   4       ; the PAP entry the writer is emitting, banked
                            ; because SI is the string pointer every emit
                            ; needs and the entry has to outlive them

%ifdef WDBENCH
; --- the walk bench (tests/wdbench.inc), in the -DWDBENCH build only ---------
    WDVAR wdb_buf, 640      ; the report, composed here and then copied into
                            ; the document claim: wd_utoa writes through DS
    WDVAR wdb_ls,  2        ; word: where the line being composed began
    WDVAR wdb_t,   2        ; word } the row being emitted: ticks over
    WDVAR wdb_i,   2        ; word } iterations
%endif

%assign WD_BSS_TOTAL WDB

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%include "os88ui.inc"
%include "os88type.inc"         ; SPEC.md 6.5: proportional type, and the band
                                ; it is composed into. AFTER os88ui.inc for no
                                ; reason but tidiness - it depends on nothing
                                ; here and nothing here depends on it until
                                ; [wd_prop] is set

    OS88_BSS WD_BSS_TOTAL + TY_BSS_SIZE
    OS88_IMAGE_END

    TY_BSS os88_image_end + WD_BSS_TOTAL

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = a fresh empty note with the caret at the origin and no toast.
wd_len      equ os88_image_end + 0     ; word: characters used. The TEXT is
                                       ; not here any more - it is [wd_dseg]
                                       ; below, a heap claim (SPEC.md 27.6)
wd_tx       equ os88_image_end + 2   ; word: paint scratch, text origin x
wd_rgt      equ os88_image_end + 4   ; word: content right, inclusive
wd_bot      equ os88_image_end + 6   ; word: content bottom, inclusive
                                       ; +8..+17 FREE: [wd_msg] and the four
                                       ; toast-box words. The toast is the
                                       ; kernel's now (SPEC.md 59) and is in
                                       ; the menu bar, so this app holds no
                                       ; state about it at all. The offsets
                                       ; below are unchanged deliberately -
                                       ; renumbering 200 hand-written equs to
                                       ; reclaim ten bytes of bss is a large
                                       ; risk for no measurable gain
wd_name     equ os88_image_end + 18   ; 14: the current document, 8.3 + NUL
                                       ; (SPEC.md 27.1) - per INSTANCE, so
                                       ; two Note Pads hold two documents
wd_tbuf     equ os88_image_end + 32   ; 26: 'Saved ' / 'Loaded ' + wd_name
wd_cur      equ os88_image_end + 62    ; word: THE CARET - the
                                       ; character index it sits in front of,
                                       ; 0..[wd_len]. Everything below exists
                                       ; to move it or to answer where it is
wd_ty       equ os88_image_end + 64    ; word: the first text row
wd_i        equ os88_image_end + 66    ; word: wd_walk's index
wd_curx     equ os88_image_end + 68    ; word: the caret in pixels
wd_cury     equ os88_image_end + 70
wd_hitx     equ os88_image_end + 72    ; word: a click to resolve,
wd_hity     equ os88_image_end + 74    ; 0xFFFF in y = no query
wd_hiti     equ os88_image_end + 76    ; word: ...and its answer
wd_wantx    equ os88_image_end + 78    ; word: a row and column to
wd_wanty    equ os88_image_end + 80    ; find, 0xFFFF = no query
wd_wanti    equ os88_image_end + 82    ; word: ...and its answer,
                                       ; 0xFFFF = there is no such row
wd_draw     equ os88_image_end + 84    ; byte: wd_walk paints
wd_hitset   equ os88_image_end + 85    ; byte: the click row was
wd_wantset  equ os88_image_end + 86    ; byte: ...the target row
wd_pad2     equ os88_image_end + 87    ; byte: keeps the total even
wd_dir      equ os88_image_end + 58    ; word: the folder the
wd_drv      equ os88_image_end + 60    ; document lives in, byte:
wd_dirok    equ os88_image_end + 61    ; its drive, byte: whether
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
; instance has - but nothing reads them until wd_paint has written them,
; because wd_sigok below is 0 until it does.
wd_row      equ os88_image_end + 88    ; word: wd_walk's visible row
wd_rowh     equ os88_image_end + 90    ; word: its running fold
wd_vrows    equ os88_image_end + 92    ; word: rows the content
                                       ; shows, capped at WD_MAXROWS
wd_dr0      equ os88_image_end + 94    ; word: first dirty row
wd_dr1      equ os88_image_end + 96    ; word: ...and the last.
                                       ; wd_dr0 = 0xFFFF means none at all
wd_stx      equ os88_image_end + 98    ; word } the geometry the
wd_sty      equ os88_image_end + 100    ; word } signatures were
wd_srgt     equ os88_image_end + 102    ; word } taken at, and the
wd_sbot     equ os88_image_end + 104    ; word } taken at (wd_sigsame)
                                       ; +106 FREE: wd_smsg
wd_sigup    equ os88_image_end + 108    ; byte: wd_walk folds and
                                       ; compares
wd_clip     equ os88_image_end + 109    ; byte: ...and draws only
                                       ; the dirty band
wd_sigok    equ os88_image_end + 110    ; byte: wd_sig has been
                                       ; written at least once
wd_pad3     equ os88_image_end + 111    ; byte: keeps wd_sig even
wd_sig      equ os88_image_end + 112    ; WD_MAXROWS words: one
                                       ; per row of the content
wd_rcols    equ os88_image_end + 232    ; word: cells the band holds
wd_rby      equ os88_image_end + 234    ; word: y of the row being
                                       ; accumulated - BP has moved on by the
                                       ; time it is flushed
wd_rcx      equ os88_image_end + 236    ; word: the caret's x on that
                                       ; row, 0xFFFF = it is not on this one
                                       ; wd_rbuf and wd_prow USED TO BE HERE,
                                       ; at +238 and +330, and they are in the
                                       ; WDVAR block at the foot of this file
                                       ; now - because they are the only two
                                       ; fields sized by WD_MAXCOL, and that
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
wd_prowi    equ os88_image_end + 421    ; word: which row that is,
                                       ; 0xFFFF = the cache holds nothing
wd_prcc     equ os88_image_end + 423    ; word: and where its caret
                                       ; was, so the cell it vacates is redrawn
wd_flo      equ os88_image_end + 425    ; word } wd_rflush's span,
wd_fhi      equ os88_image_end + 427    ; word } 0xFFFF = empty
wd_fcc      equ os88_image_end + 429    ; word: the caret's column
                                       ; on the row being flushed

; --- the document, and the heap it lives in (SPEC.md 27.6/50.3) ----------------
; The text itself is NOT in this package's region. wd_entry claims WD_KB0 for
; it before it creates the window, wd_room grows it a kilobyte at a time as
; the note fills, a load sizes it to the file and File > New gives it back.
; That is what an editor's buffer is: data whose size only the user knows,
; and a region is image + bss capped at APP_MAX_SIZE (SPEC.md 20.1).
wd_dseg     equ os88_image_end + 433    ; word: the document claim's segment.
                                       ; The text is [wd_dseg]:0000, and it
                                       ; is NEVER 0 while this instance lives:
                                       ; wd_entry aborts the launch rather
                                       ; than open a window with nowhere to
                                       ; put the text, so nothing below has
                                       ; to test it
wd_capkb    equ os88_image_end + 435    ; word: its size in KB...
wd_cap      equ os88_image_end + 437    ; word: ...and in bytes, kept in step
                                       ; by wd_resize. WD_MAXKB * 1024 fits a
                                       ; word, which is what bounds the note
                                       ; +441..+444 FREE: wd_msgn and
                                       ; wd_smsgn, the toast's GENERATION and
                                       ; the one the signatures were taken
                                       ; over. Both existed because every
                                       ; toast was composed into the same
                                       ; wd_tbuf, so the POINTER could not
                                       ; tell "Saved X" from "Loaded X" -
                                       ; a distinction the kernel's copy
                                       ; makes for itself (SPEC.md 59.3)
wd_stgseg   equ os88_image_end + 439    ; word: the file staging claim, 0 =
                                       ; not held. A SECOND claim, transient:
                                       ; a save assembles the .DOC image in
                                       ; it (FIB + text + CHP + PAP, SPEC.md
                                       ; 65.4) and a load reads the whole
                                       ; file into it before the magic is
                                       ; sniffed - the document is untouched
                                       ; until the file proves valid
; --- the layout checkpoint (SPEC.md 27.4) ------------------------------------
; wd_walk is O(the note), and it runs TWICE per keystroke - which is what a
; user found by filling a fullscreen window and watching each keystroke get
; slower while the delta cache above kept the drawing at two cells. Wrapping
; is a left-to-right automaton with no lookahead, so the pen state at index k
; depends only on the characters before it: an edit at the caret cannot
; change the layout of anything ahead of the caret. So the walk may RESUME at
; the start of the caret's row instead of starting at index 0, and the start
; of a row is (index, row) alone - the pen's x is always [wd_tx] there and
; its y is always [wd_ty] + 8*row.
wd_ckpi     equ os88_image_end + 445    ; word } the checkpoint: the
wd_ckpr     equ os88_image_end + 447    ; word } caret's row start
wd_ckpc     equ os88_image_end + 449    ; word } and the candidate
wd_ckpcr    equ os88_image_end + 451    ; word } wd_rstart banks
wd_win      equ os88_image_end + 453    ; word: our window, which a
                                       ; callback is handed but the worker
                                       ; has to remember
wd_bcrow    equ os88_image_end + 455    ; word: the row the visual
                                       ; break sits on (SPEC.md 27.3)
wd_borig    equ os88_image_end + 457    ; word: ...and the row it
                                       ; started on, which is the first row
                                       ; the reconcile has to repaint
wd_ktick    equ os88_image_end + 459    ; word: the tick of the last
                                       ; keystroke, for the idle settle
wd_ckok     equ os88_image_end + 461    ; byte: the checkpoint
                                       ; describes THIS layout
wd_fast     equ os88_image_end + 462    ; byte: this redraw is a
                                       ; plain insert or backspace at the
                                       ; caret, inside the caret's own row.
                                       ; One-shot: wd_redraw consumes it
wd_resume   equ os88_image_end + 463    ; byte: wd_walk starts at
                                       ; the checkpoint, not at index 0
wd_bmode    equ os88_image_end + 464    ; byte: the visual break is
                                       ; up, so the screen is NOT the note
wd_clean    equ os88_image_end + 465    ; byte: the band is known
                                       ; blank, so a row's run needs no
                                       ; trailing padding to erase with
wd_brkok    equ os88_image_end + 466    ; byte: this machine is slow
                                       ; enough to want the break at all
wd_hired    equ os88_image_end + 467    ; byte: the worker exists
wd_didpush  equ os88_image_end + 468    ; byte: a push scrolled the
                                       ; band, so the grow box needs redrawing
wd_bfail    equ os88_image_end + 469    ; byte: a push was REFUSED -
                                       ; the band left below the caret is
                                       ; shorter than a row - so the break
                                       ; cannot continue and must settle
wd_bstop    equ os88_image_end + 470    ; byte: THIS walk ends at
                                       ; the caret. Its own flag and not
                                       ; "[wd_bmode] is set", because
                                       ; wd_measure answers clicks and arrow
                                       ; keys and has to see the whole note
                                       ; even while the break is up
wd_rowsok   equ os88_image_end + 471    ; byte: wd_rows describes
                                       ; this layout

; --- where each row starts (SPEC.md 27.5) ------------------------------------
; The checkpoint above is the caret's row start; this is EVERY row's, which is
; what turns a query about an arbitrary row into a bounded walk. A click, an
; Up and a Home all name a row and want the index at a column of it, and
; without this each of them re-derived the whole layout from index 0 to answer
; a question about thirty characters.
wd_rows     equ os88_image_end + 472    ; WD_MAXROWS words
wd_rowsn    equ os88_image_end + 472 + WD_MAXROWS*2   ; word: how
                                       ; many of them the last full walk wrote
wd_sdi      equ os88_image_end + 474 + WD_MAXROWS*2   ; word } the
wd_sdr      equ os88_image_end + 476 + WD_MAXROWS*2   ; word } seed
wd_lastrow  equ os88_image_end + 478 + WD_MAXROWS*2   ; word: the
                                       ; last row this walk cares about. ONE-
                                       ; SHOT and reset to 0xFFFF by wd_walk,
                                       ; so a caller that forgets gets the
                                       ; whole note - slow, never wrong
wd_ecol     equ os88_image_end + 480 + WD_MAXROWS*2   ; word: the
                                       ; caret's column on its row BEFORE this
                                       ; edit - reported by the key handler,
                                       ; never derived (SPEC.md 27.3)
wd_ekind    equ os88_image_end + 482 + WD_MAXROWS*2   ; byte: what
                                       ; THIS redraw is for - wd_redraw's
                                       ; one-shot copy of [wd_fast]'s kind
wd_eext     equ os88_image_end + 483 + WD_MAXROWS*2   ; byte: cells
                                       ; of the row below that go stale beyond
                                       ; the caret's column (Delete: 1)

; --- scrolling (SPEC.md 27.7) ------------------------------------------------
wd_top      equ os88_image_end + 484 + WD_MAXROWS*2   ; word: the note row
                                       ; drawn at the top of the view. wd_row
                                       ; is the VISIBLE row and starts at
                                       ; MINUS this
wd_drows    equ os88_image_end + 486 + WD_MAXROWS*2   ; word: how many rows
                                       ; the note occupies, from the last walk
                                       ; that ran to its natural end
wd_sbr      equ os88_image_end + 488 + WD_MAXROWS*2   ; word: the content's
                                       ; own right edge - wd_rgt stops
                                       ; WD_SB_W short of it now
wd_sbb      equ os88_image_end + 490 + WD_MAXROWS*2   ; word: and the bar's own
                                       ; bottom, wd_bot less the grow box
wd_sbtop    equ os88_image_end + 492 + WD_MAXROWS*2   ; word } what the bar on
wd_sbrows   equ os88_image_end + 494 + WD_MAXROWS*2   ; word } screen was drawn
                                       ; from, so a redraw that moved neither
                                       ; leaves it alone
wd_follow   equ os88_image_end + 496 + WD_MAXROWS*2   ; byte: this redraw is
                                       ; one the CARET moved in, so the view
                                       ; owes it a place on screen. Its own
                                       ; flag and not "[wd_ekind] is set":
                                       ; that one says which cheap redraw path
                                       ; is allowed, and Enter, Up, Down, Home
                                       ; and End are all 0 there while all
                                       ; five move the caret. One-shot, so a
                                       ; scroll bar click - which reaches the
                                       ; same wd_redraw - cannot be dragged
                                       ; straight back to the caret
wd_ptop     equ os88_image_end + 500 + WD_MAXROWS*2   ; word: the [wd_top]
                                       ; the PIXELS on screen were drawn for.
                                       ; wd_sig and wd_rows are indexed by a
                                       ; visible row, so this is what says
                                       ; which view they describe - and a
                                       ; scroll is reconciled by shifting all
                                       ; three together (SPEC.md 27.7.2)
wd_sdlt     equ os88_image_end + 502 + WD_MAXROWS*2   ; word } wd_scrollpaint
wd_bd0      equ os88_image_end + 504 + WD_MAXROWS*2   ; word } scratch: the
wd_bd1      equ os88_image_end + 506 + WD_MAXROWS*2   ; word } delta, the band
wd_hdirty   equ os88_image_end + 498 + WD_MAXROWS*2   ; byte: the note
                                       ; changed, so [wd_drows] is a lower
                                       ; bound rather than the height. Set by
                                       ; wd_ins/wd_del/wd_clamp, cleared by
                                       ; any walk that reached the note's end
wd_curseen  equ os88_image_end + 499 + WD_MAXROWS*2   ; byte: THIS walk stood
                                       ; on the caret, so [wd_cury] is its
                                       ; position and not the initial 0. A
                                       ; bounded walk can stop above it
wd_gchg     equ os88_image_end + 497 + WD_MAXROWS*2   ; byte: the geometry
                                       ; changed since the last paint, so the
                                       ; row count did too and [wd_top] has
                                       ; not been clamped against it yet. Set
                                       ; by wd_bounds, spent by wd_paint,
                                       ; cleared by wd_sigmark
                                       ; total 974 = WD_BSS_TOTAL
