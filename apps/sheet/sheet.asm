; =============================================================================
; os8088 - apps/sheet/sheet.asm
;
; SHEET - a spreadsheet to complement Word. This file is stage 1.2 of the
; project roadmap, built in three pieces within this same revision:
;   (a) the 256x16384 grid and the sparse cell storage it requires (THIS
;       PASS), plus visible gridlines;
;   (b) formulas - arithmetic, cell references, SUM/AVERAGE/MIN/MAX/COUNT
;       over ranges (NEXT PASS, not in this file yet);
;   (c) an expanded file format alongside SYLK (AFTER THAT).
; Prefix sh_.
;
; WHY STORAGE HAD TO CHANGE. Stage 1.0's grid was 64x64 - 4096 cells - kept
; as a flat 512-byte occupancy bitmap plus a 4096-word value array, both
; package bss. 256x16384 is 4,194,304 possible cells: a dense bitmap alone
; would be 512KB, dwarfing the package's entire 60KB image+bss budget
; (SPEC.md 20) before a single value is stored. Real sheets are sparse -
; a handful to a few hundred occupied cells out of millions possible - so
; storage becomes an array of (row, col, value) records, kept SORTED by
; (row, col) and searched with a binary search, living in memory the
; package's own segment cannot hold: a claim from the heap (SPEC.md 50.3,
; OSAPI_MEM_CLAIM), taken once from the entry proc before the window
; exists, exactly as the SDK describes it - "a canvas, a sound clip, a
; decoder's tables". The claim is a full 64KB-addressable segment of its
; own; a 12-byte record and a 16KB claim cap the sheet at 1365 distinct
; cells, comfortably past anything hand-entered in this environment. Two
; more claims hold the formula text (arena, append-only) and the file I/O
; staging buffer - both would have blown the package's own budget too.
;
; sh_findcell binary-searches the sorted array; sh_addcell/sh_removecell
; keep it sorted by shifting the tail up or down one record's worth of
; bytes around the insertion or removal point. This trades an O(n) insert
; for an O(log n) lookup, which is the right trade for a grid that is
; painted far more often than it is edited.
;
; LAYOUT, SELECTION, EDITING, DRAWING MODEL: unchanged from stage 1.0
; (see git history / the stage 1.0 header) except:
;   - the selected cell's row is now a WORD (0..16383) everywhere, since it
;     no longer fits a byte;
;   - the row header is wider (5 digits) and the default window is a
;     little roomier;
;   - the grid now draws visible cell-boundary lines (OSAPI_GFX_FILL
;     degenerate 1px rectangles) OVER the cell text, because
;     OSAPI_FONT_RUN's opaque erase is exactly one cell wide and would
;     otherwise paint over a line drawn first;
;   - entering a number now requires the WHOLE edit buffer to parse as one
;     signed integer (stage 1.0 silently accepted a typed '.' that its
;     parser then ignored - a latent bug this stage removes along with
;     the character itself, since nothing here does fractional values).
;
; STORAGE MODEL CAVEATS, STATED RATHER THAN HIDDEN: no bounds check against
; 16-bit signed overflow on entry (a value or SUM large enough to wrap does
; so silently, exactly as plain 8086 ADD/MUL would); the formula text arena
; (once formulas land) is append-only and never reclaims a superseded
; formula's old bytes; SYLK is still this project's own honest subset, not
; certified Microsoft interchange - both stage 1.0 tradeoffs, both still
; true here.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SHEET', sh_entry, 3   ; bit 0 = icon, bit 1 = the
                                        ; association block below

; --- embedded 16x16 icon: a blank page with a 3x3 grid on it -------------------
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0000
    dw 0x3FFC                       ; top border
    dw 0x2224                       ; sides + two internal verticals
    dw 0x2224
    dw 0x2224
    dw 0x3FFC                       ; internal horizontal divider
    dw 0x2224
    dw 0x2224
    dw 0x2224
    dw 0x3FFC                       ; internal horizontal divider
    dw 0x2224
    dw 0x2224
    dw 0x3FFC                       ; bottom border
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

; The three sheet formats this app reads and writes, claimed so a document
; opens on a DOUBLE-CLICK rather than only through File > Open (SPEC.md 54.6).
; Declaring costs nothing at runtime - the mount's icon harvest already reads
; this sector - and it works before Sheet has ever been run, which a runtime
; OSAPI_ASSOC_SET claim does not.
;
; CHART.O88 reads the same three formats and deliberately does NOT claim them:
; there is no ownership model (54.5), so a second declaration would simply
; take the extension, and a spreadsheet file belongs to the spreadsheet. Chart
; opens one through its own File > Open.
    OS88_ASSOC16
    db 3
    OS88_ASSOC_EXT 'SLK'
    OS88_ASSOC_EXT 'DIF'
    OS88_ASSOC_EXT 'BIF'
    OS88_ASSOC16_END

; =============================================================================
; Geometry / grid / storage constants
; =============================================================================
SH_COLS      equ 256                ; the roadmap's stage 1.2 ceiling
SH_ROWS      equ 16384
; stage 2.x: Format > Column Width.../Row Height... make these RUNTIME
; values (sh_cellw/sh_cellh/sh_cellch bss words, set from one of these
; presets) rather than compile-time constants - every site below that used
; to read the equ now reads the bss word instead. The three presets below
; are what Column Width.../Row Height... offered while this app had no
; text-input widget of its own; stage 3.0c gave it one (sh_idlg_*, over
; os88line.inc), so both dialogs are REAL NUMERIC ENTRY now and these are
; only the startup defaults. Widths
; must stay multiples of 8 - sh_blank and every OSAPI_FONT_RUN cell text
; is built one glyph (8px) at a time, so a non-multiple would leave a
; fractional glyph column with nothing sensible to draw there.
SH_CW_NARROW equ 40                 ; 5 chars
SH_CW_NORMAL equ 56                 ; 7 chars - the original fixed default
SH_CW_WIDE   equ 80                 ; 10 chars
SH_RH_SHORT  equ 11
SH_RH_NORMAL equ 14                 ; the original fixed default
SH_RH_TALL   equ 18
; stage 3.0c: the bounds real numeric entry has to enforce, now that Row
; Height.../Column Width... take a typed number instead of a 3-way radio.
; Width is in CHARACTERS (Excel's own unit); height is in pixels.
SH_CW_MINCH  equ 1
SH_NUMBUF_MAX equ 40                ; stage 4.5: sh_numbuf's usable length,
                                    ; which is SH_CW_MAXCH because a label is
                                    ; clipped to its column and nothing wider
                                    ; can ever reach a justifier
SH_CW_MAXCH  equ 40                 ; 320px - wider than the window, but the
                                    ; renderer clips and the user asked
SH_RH_MIN    equ 8                  ; one glyph cell: below this no text fits
SH_RH_MAX    equ 48
SH_RH_W      equ 40                 ; row-header column, 5 digits at 8px
SH_CH_H      equ 14
SH_FB_H      equ 16
SH_REF_W     equ 64                 ; stage 2.x: the formula bar's own
                                     ; reference box width - wide enough
                                     ; for the longest possible reference
                                     ; text (a 2-letter column + a 5-digit
                                     ; row, SH_COLS=256/SH_ROWS=16384's own
                                     ; worst case) plus a little padding
; Mirrors of os88ui.inc's own scroll-bar constants. Duplicated here for the
; same reason the CH_* chart constants are (see their comment below): that file
; is %included at the END of this one, so its equs are FORWARD references, and
; a forward-referenced value used as an IMMEDIATE makes NASM size the
; instruction differently on each pass - `cmp cx, imm8` vs `cmp cx, imm16` -
; which fails the assembly outright with "label changed during code
; generation". Values must track os88ui.inc's; they are part of the block
; contract sh_hsb_* is written to be promoted into.
SH_SB_NONE   equ 0
SH_SB_UP     equ 1                  ; the LEFT arrow on a horizontal bar
SH_SB_DOWN   equ 2                  ; ...and the RIGHT one
SH_SB_PGUP   equ 3
SH_SB_PGDN   equ 4
SH_SB_THUMB  equ 5
SH_SB_MINH   equ 8                  ; the shortest thumb that is still a thumb
SH_SB_CELL   equ 10                 ; the arrow cell's depth

SH_VSB_W     equ 14                 ; stage 3.0a+: the vertical scroll bar's
                                     ; width. 14 is what both kernel callers
                                     ; use and what os88ui.inc's arrow glyph
                                     ; is drawn for (5 rows, widths 1..9)
SH_HSB_H     equ 14                 ; ...and the horizontal bar's height, the
                                     ; same cell so the two agree at the
                                     ; corner where they meet
SH_SB_H      equ 16                 ; stage 2.x: the status bar strip at
                                     ; the very bottom of the window,
                                     ; same height as the formula bar for
                                     ; visual symmetry
SH_EDITMAX   equ 63                 ; room for a formula, not just a number
SH_NAMEMAX   equ 12
SH_RW_CAP    equ 80                  ; stage 2.x: sh_formula_reidx's own
                                     ; output cap - a shifted reference can
                                     ; grow by a digit or two (row 9->10,
                                     ; col Z->AA), so a little more than
                                     ; SH_EDITMAX+1

SH_CLAIM_CELLS_KB equ 32            ; -> SH_CELL_CAP records of SH_C_SZ.
                                    ; Doubled with the widening: 20 bytes in
                                    ; 16KB would have DROPPED capacity to 819,
                                    ; and 32KB takes it up to 1638 instead
SH_CLAIM_TXT_KB   equ 8             ; formula text arena (used from the next
                                     ; pass on; claimed now so entry needs no
                                     ; second edit)
SH_CLAIM_STG_KB   equ 32            ; file I/O staging
; sh_docmd_sortcol's own layout within sh_stgseg (stage 2.x: formula cells
; now participate in the sort too, so alongside the original rows[]/
; values[] arrays it also needs a source-index permutation, an
; is-this-a-formula flag, and staged formula text for each one - see the
; section comment above sh_docmd_sortcol for the full design)
; STAGE 4.5 RELAID THIS OUT because values[] had to grow. It was a WORD per
; entry - the truncated integer - which was right while every cell held one and
; became silently wrong the moment cells held doubles: 1.2, 1.5 and 1.9 all
; truncate to 1, so a column of decimals sorted into whatever order the
; insertion sort's stability happened to leave them in. Nothing reported it,
; because a sorted-looking column IS what you get.
;
; Entries are capped at SH_SORT_CAP now as well. There was no cap before, and
; nothing stopped a long column walking off the end of one array into the next.
SH_MENU_CHK       equ 2              ; a leading byte meaning "checked", the
                                     ; companion to the kernel's MENU_DIS
SH_SORT_CAP       equ 512            ; entries one sort can carry
SH_SORT_ROWS_OFF  equ 0              ; word/entry
SH_SORT_VALS_OFF  equ 1024           ; EIGHT bytes/entry: a whole double
SH_SORT_ORIG_OFF  equ 5120           ; word/entry: origidx[] (which
                                     ; pre-sort entry ended up here)
SH_SORT_ISF_OFF   equ 6144           ; byte/entry: 1 if that entry is a
                                     ; formula cell
SH_SORT_FIDX_OFF  equ 6656           ; word/entry: which SH_SORT_FTXT_OFF
                                     ; slot holds that formula's own text
                                     ; (only meaningful when ISF is set)
SH_SORT_FTXT_OFF  equ 7680           ; SH_SORT_FCAP slots of 64 bytes each,
                                     ; ending at 7680+180*64=19200, safely
                                     ; inside the 32KB claim
SH_SORT_FCAP      equ 180           ; max formula cells one sort can carry
SH_SORT_SNAP_OFF  equ 19200          ; SH_SORT_SNAPCAP slots of 64 bytes: ONE
                                     ; other column's cells as text, while the
                                     ; permutation is applied to it. It starts
                                     ; where the formula slots end (7680 +
                                     ; 180*64) and fits inside SH_STAGE_MAX
SH_SORT_SNAPCAP   equ 180            ; rows a multi-column sort can carry
                                     ; through - far more than any real
                                     ; column needs; a cell beyond this cap
                                     ; is simply excluded from the sort
                                     ; entirely (same "clip, don't crash"
                                     ; policy used throughout this file)
SH_CLAIM_NOTE_KB  equ 4             ; stage 3.0b: the note table - SH_NOTE_CAP
                                    ; records of SH_NOTE_REC. The note TEXT is
                                    ; not in here; it goes in the formula
                                    ; arena, for the reason sh_nt_findcell's
                                    ; header gives.
SH_CLAIM_BORD_KB  equ 4             ; stage 2.x: the border table (below) -
                                     ; a separate claim rather than growing
                                     ; every cell record, since almost no
                                     ; cell ever has a border and this app
                                     ; already has 3 claims plus its own
                                     ; region (MEM_OWNER_MAX=8, room to spare)
SH_CHART_S2  equ 512                ; where a chart's SECOND series lands in
                                    ; sh_stgseg - the first sits at 0 and needs
                                    ; CH_MAXBARS words, so 512 is clear of it
                                    ; with room to spare
SH_CHART_D1  equ 1024               ; stage 4.6: and where the DOUBLES the scan
SH_CHART_D2  equ 1536               ; collects sit, before ch_scale turns them
                                    ; into the two word arrays above. Same 512
                                    ; spacing; CH_MAXBARS doubles is 320
SH_CLAIM_CHART_KB equ 19            ; stage 2.x: the live Chart Column window's
                                     ; offscreen 4bpp canvas - 240x160px, 120
                                     ; bytes/row (already a multiple of 4, so
                                     ; the BMP export below needs no row
                                     ; padding logic) = 19200 bytes -> 19KB.
                                     ; This is Sheet's 5th claim (own region +
                                     ; cellseg/txtseg/stgseg/bordseg), so 6/8
                                     ; of MEM_OWNER_MAX - still room to spare.
                                     ; No pixel-readback API exists anywhere in
                                     ; this OS (checked every OSAPI_GFX_*), so
                                     ; this buffer - not the screen - is the
                                     ; one thing both the on-screen chart (one
                                     ; OSAPI_GFX_BLIT4 of it) and the exported
                                     ; .BMP (one OSAPI_FILE_WRITE of it, same
                                     ; bytes) are drawn from.
; =============================================================================
; THE CELL RECORD (stage 4.0). Every offset below is named, and every stride
; goes through SH_C_SZ, because this layout has now moved once and the plan's
; own risk list puts "a missed stride site" first: it reads a MISALIGNED
; record and hands back a plausible wrong number, with no crash to notice.
; Naming them makes the next move a four-line edit instead of an 87-site
; audit.
;
; +0 and +2 and +4 and +5 are shared in shape with the border and note tables
; (sh_bt_* / sh_nt_*), which is why those four are deliberately NOT renamed
; here - a rename would have had to reach into two other tables to stay
; honest, and they have their own strides.
; =============================================================================
SH_C_ROW     equ 0                  ; word: packed row | sheet
SH_C_COL     equ 2                  ; word
SH_C_FLAGS   equ 4                  ; byte: bit0 HASFORMULA, bit1 EVALUATING
SH_C_FMT     equ 5                  ; byte: SH_FMT_*, and the BIFF XF index
SH_C_TYPE    equ 6                  ; byte: SH_T_* - reserved by stage 4.0's
SH_C_AUX     equ 7                  ; byte: ...error code, likewise reserved.
                                    ; THE TAG IS ITS OWN BYTE AND NOT SPARE
                                    ; BITS OF SH_C_FMT: that byte's numeric
                                    ; value IS the XF index the BIFF writer
                                    ; emits, so borrowing bits 6-7 would
                                    ; silently change every XF in every file
                                    ; this app has ever written.
SH_C_VAL     equ 8                  ; 8 bytes: an IEEE-754 double. Still
                                    ; written and read as a WORD in the low
                                    ; half for now - the widening and the
                                    ; switch to real doubles are separate
                                    ; steps on purpose, so that a fault in
                                    ; either one is unambiguous.
SH_C_FOFF    equ 16                 ; word: formula text offset in sh_txtseg
SH_C_PASS    equ 18                 ; word: the repaint pass that cached VAL
; The value tags stage 4.0 reserves. Numbered so that BLANK is 0 and a
; zeroed record is therefore a blank one.
SH_T_BLANK   equ 0
SH_T_NUM     equ 1
SH_T_TEXT    equ 2
SH_T_BOOL    equ 3
SH_T_ERR     equ 4

; Error codes, in SH_C_AUX. These are EXCEL'S OWN ERROR.TYPE numbers, so
; ERROR.TYPE and ISERR become a table lookup if they are ever added, and the
; BIFF BOOLERR record can carry them without a translation step.
SH_ERR_NULL  equ 1                  ; #NULL!
SH_ERR_DIV0  equ 2                  ; #DIV/0!   - the only one produced today
SH_ERR_VALUE equ 3                  ; #VALUE!
SH_ERR_REF   equ 4                  ; #REF!
SH_ERR_NAME  equ 5                  ; #NAME?
SH_ERR_NUM   equ 6                  ; #NUM!
SH_ERR_NA    equ 7                  ; #N/A

SH_C_SZ      equ 20                 ; ...and an EVEN stride, so the array
                                    ; shuffle can move words rather than bytes

; sh_rowcol_op stages every record through sh_stgseg while it shifts a row or
; column. It is a transient copy, not storage - but it CARRIES the whole
; cell across the shift, so it had to grow with the cell record: the value
; at stage 4.0, the type tag and error code at stage 4.5. Named for exactly
; the reason above: the two layouts look alike and one was silently edited
; into the other.
SH_S_SHEET   equ 0
SH_S_ROW     equ 2
SH_S_COL     equ 4
SH_S_FLAGS   equ 6
SH_S_FMT     equ 7
SH_S_VAL     equ 8                  ; 8 bytes since stage 4.0: this record
                                    ; CARRIES a cell's value across a row or
                                    ; column shift, so it had to grow with the
                                    ; cell record or every decimal in the
                                    ; sheet would have been truncated to the
                                    ; low half of its own double - silently,
                                    ; on an Insert Row
SH_S_FML     equ 16
SH_S_TYPE    equ 18                 ; byte: SH_C_TYPE, carried for the same
SH_S_AUX     equ 19                 ; byte: ...reason - sh_addcell retags a
                                    ; fresh record SH_T_NUM, so a label whose
                                    ; tag was not carried came back a number.
                                    ; Free bytes: the staging stride in the
                                    ; code is SH_C_SZ, so 18..19 already exist
SH_S_SZ      equ 20

SH_CELL_CAP  equ 1638               ; floor(SH_CLAIM_CELLS_KB*1024 / SH_C_SZ)
SH_TXT_CAP   equ 8192               ; SH_CLAIM_TXT_KB in bytes
SH_STAGE_MAX equ 32768
SH_BORD_CAP  equ 819                ; floor(4096 / 5)
SH_NOTE_REC  equ 6                  ; stage 3.0b: the note table's record -
                                    ; packed row/sheet, col, and the note
                                    ; text's offset in the SHARED formula
                                    ; arena (see sh_nt_findcell's header)
SH_NOTE_CAP  equ 682                ; floor(4096 / SH_NOTE_REC)
SH_NOTEMAX   equ 240                ; the longest note the dialog will take,
                                    ; INCLUDING its NUL - 6 lines of 39 in the
                                    ; box below, which is what fits
; CH_* is the offscreen-chart-canvas geometry apps/os88chart.inc's own
; routines (ch_bars_draw/ch_bmp_write, %included near the end of this
; file) are written against. These equ lines are duplicated verbatim in
; apps/chart/chart.asm rather than shared - NASM's equ can't be forward-
; referenced, and os88chart.inc's CODE has to live at the end of the file
; (same fixed-offset reason os88ui.inc's own header states), so anything
; used by code earlier than that has to already exist. Same idea as
; os88api.inc itself being "code-free on purpose" so it can sit at the top
; - these are the constant half of that split, just declared per-package
; instead of in a %include, since equ lines are too early-needed to live
; where the shared CODE has to live.
CH_W       equ 240
CH_H       equ 160
CH_STRIDE  equ 120                  ; CH_W / 2 (4bpp, 2px/byte)
CH_HDRSZ   equ 118                  ; 54-byte BMP header + 64-byte palette
CH_PXOFF   equ CH_HDRSZ             ; pixel data starts right after
CH_MAXBARS equ 40                   ; how many values the caller's arrays
                                     ; hold - NOT a drawing limit: ch_band
                                     ; divides the axis among however many
                                     ; there are, so any count up to this one
                                     ; fits the canvas
CH_T_COLUMN equ 0                   ; stage 3.0f: the gallery. Excel calls the
CH_T_BAR    equ 1                   ; vertical one Column and the horizontal
CH_T_LINE   equ 2                   ; one Bar, and this follows that naming
CH_T_AREA   equ 3                   ; rather than the intuitive-but-wrong one
CH_T_PIE    equ 4                   ; stage 3.0f, and the last of the four
CH_T_SCATTER equ 5                  ; ...and stage 3.0f's own last two, which
CH_T_COMBO   equ 6                  ; needed a SECOND series (SPEC.md 82.8)
                                    ; Excel types this app can draw: Scatter
                                    ; and Combination need TWO series, which
                                    ; is a data-model problem rather than a
                                    ; drawing one
SH_CHARTWIN_W equ 260                ; a little margin around the CH_W x
SH_CHARTWIN_H equ 200                ; CH_H canvas - real size comes back
                                      ; from OSAPI_WM_CONTENT either way
SH_EVAL_MAXDEPTH equ 6               ; a formula referencing a formula
                                      ; referencing a formula...; each level
                                      ; gets its own text buffer (below) so a
                                      ; nested evaluation cannot overwrite
                                      ; the text an outer one is still
                                      ; parsing. Beyond this many levels a
                                      ; reference just reads as 0 - the same
                                      ; honest simplification as every other
                                      ; unbounded case here.
SH_PNEST_MAX equ 12                  ; the parser's own nesting budget (81.3):
                                      ; live recursion points plus cell depth,
                                      ; charged by sh_pnest_enter. 12 is what
                                      ; the deepest SH_EVAL_MAXDEPTH chain of
                                      ; folds needs (one call + one depth per
                                      ; level); each level holds tens of bytes
                                      ; of task 0's 1,024-byte stack, so the
                                      ; cap is sized to that stack, not to the
                                      ; grammar

; --- stage 1.6: per-cell text formatting -----------------------------------
; Packed into the cell record's byte at +5 (previously unused padding, see
; the record layout comment above sh_findcell): bit0 bold, bit1 underline,
; bits3-2 alignment, bits5-4 number format. Bits6-7 are unused. This exact
; 6-bit space is also, not coincidentally, this app's BIFF XF index on disk
; (sh_dowrite_biff) - see the comment there for why that pairing is safe.
SH_FMT_BOLD          equ 0x01
SH_FMT_UNDER         equ 0x02
SH_FMT_BU_CLR        equ 0xFC        ; ~(SH_FMT_BOLD|SH_FMT_UNDER) & 0xFF -
                                      ; stage 1.8's Font dialog clears bits
                                      ; 0-1 in one mask, not two XORs
SH_FMT_ALIGN_MASK    equ 0x0C
SH_FMT_ALIGN_CLR     equ 0xF3        ; ~SH_FMT_ALIGN_MASK & 0xFF
SH_FMT_ALIGN_SHIFT   equ 2
SH_FMT_ALIGN_GENERAL equ 0           ; General: right, same as this app's
                                      ; only-ever-numeric default
SH_FMT_ALIGN_LEFT    equ 1
SH_FMT_ALIGN_CENTER  equ 2
SH_FMT_ALIGN_RIGHT   equ 3
SH_FMT_NUM_MASK      equ 0x30
SH_FMT_NUM_CLR       equ 0xCF        ; ~SH_FMT_NUM_MASK & 0xFF
SH_FMT_NUM_SHIFT     equ 4
SH_FMT_NUM_GENERAL   equ 0
SH_FMT_NUM_CURRENCY  equ 1
SH_FMT_NUM_COMMA     equ 2
SH_FMT_NUM_PERCENT   equ 3

; --- stage 2.x: cell borders (Format > Border..., its own sh_bordseg claim
; and sh_bt_* table - see the SH_CLAIM_BORD_KB comment above for why this
; isn't just more bits in the format byte) ----------------------------------
SH_BORD_LEFT   equ 0x01
SH_BORD_RIGHT  equ 0x02
SH_BORD_TOP    equ 0x04
SH_BORD_BOTTOM equ 0x08
SH_BORD_SHADE  equ 0x10
SH_BORD_EDGES  equ 0x0F             ; Left|Right|Top|Bottom together

; sh_doread_biff's FONT/XF tracking tables (a real file might reference more
; than this app itself ever writes - beyond the cap, a cell just reads back
; as unformatted rather than growing these tables without bound)
SH_BIFF_FONT_CAP equ 32
SH_BIFF_XF_CAP   equ 64

; --- stage 2.0: multiple sheets in one instance ----------------------------
; No OS8088 mechanism lets one running instance find or address another's
; memory (there is no window-enumeration or IPC primitive at all - see the
; claim/task model in SPEC.md 29/50.2), and every app including this one is
; strictly one-instance-one-document. Real Excel's separate-file-per-sheet
; model is therefore not implementable without inventing new OS capability,
; so "sheets" here are multiple grids living inside this ONE instance's
; existing three claims, distinguished by a sheet index folded into the
; cell record's own row field rather than by claiming more segments (the
; kernel caps any one owner at MEM_OWNER_MAX=8 claims, and this package's
; region already counts as one of them - three fresh claims per extra sheet
; would run out fast). SH_ROWS needs exactly 14 bits (0..16383), leaving
; exactly 2 spare bits in that word for a sheet index - hence exactly
; SH_SHEETS=4, not a rounder number chosen for its own sake.
SH_SHEETS    equ 4
SH_ROW_BITS  equ 14                  ; row occupies bits 0-13
SH_ROW_MASK  equ 0x3FFF

; --- stage 2.x: Sheet's own in-window menu bar -----------------------------
; MENU_APPMAX is five (apps/os88api.inc) and real Excel 2.1's bar is eight
; real menus (File/Edit/Format/Data/Options/Macro/Help, plus this app's own
; Sheets switcher, which has no real-Excel equivalent since Excel used
; separate windows per sheet rather than one packed instance - see the
; stage 2.0 comment above). Word.O88 hit the exact same ceiling and answered
; it the same way (see apps/word/word.asm's "Word chrome" section, SPEC.md
; 68.2): draw the bar and its dropdowns IN THE WINDOW instead of asking the
; kernel for one, and register only the kernel's minimum single-item
; placeholder (sh_mf_ret below) so the bar still gets an app-name pulldown.
; Word's own version adds a ribbon, a ruler, combos and a sliding-panel edge
; case none of which Sheet needs - this is a deliberately smaller subset of
; the same mechanism: plain titles, plain dropdowns, one interaction style
; (press-drag-release, matching what every OS88_MENUSET app - including
; Sheet's own menus before this stage - already trained users on).
;
; The gesture itself is Word's wd_mtrack pattern, not W_ONDRAG: a tight
; OSAPI_MOUSE poll with a gfx-unlock/yield/relock between reads (SPEC.md
; 13.7 forbids mixing W_ONDRAG with a polling loop in the same app, and
; W_ONDRAG/W_ONTIMER are missing entirely on one of the two kernel variants
; anyway - see the earlier note on why range selection was scoped out).
; This works on both kernel variants because it never touches the optional
; drag/timer slots at all.
SH_MBAR_H    equ 14                  ; the in-window menu bar strip
SH_MI_H      equ 12                  ; a dropdown item's row height
SH_MPAD      equ 8                   ; left/right pixel pad per title/item
SH_MCHKW     equ 8                   ; stage 3.0c: the DROPDOWN's extra left
                                     ; gutter, where a checked item's mark
                                     ; goes. Not folded into SH_MPAD because
                                     ; that one also sets the spacing of the
                                     ; BAR's own titles, which have no marks
                                     ; and would just drift apart
SH_MENU_N    equ 9                   ; File,Edit,Formula,Format,Data,Options,
                                      ; Macro,Sheets,Help - Excel 2.1d's own
                                      ; bar order (see sh_mtab). NOTE this
                                      ; also sizes sh_mw in the bss chain, so
                                      ; changing it moves OS88_BSS too.
SH_M_NONE    equ 0xFF

; =============================================================================
; sh_entry - package entry point (SPEC.md 20.2). Claims run here, and only
; here (SPEC.md 50.3): this is the one place a package has no window yet
; and is sizing itself. A claim failure aborts the launch (CF=1) rather
; than opening a sheet that cannot hold anything - the kernel tears down
; whatever we did claim either way (no teardown hook owed).
; =============================================================================
sh_entry:
    push ax
    push dx
    push si
    push di
    call fp_init                      ; before the first claim, because every
                                      ; other thing here can fail and be
                                      ; recovered from and this one decides
                                      ; which arithmetic the session gets
    mov ax, SH_CLAIM_CELLS_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sh_cellseg], dx
    mov ax, SH_CLAIM_TXT_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sh_txtseg], dx
    mov ax, SH_CLAIM_STG_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sh_stgseg], dx
    mov ax, SH_CLAIM_BORD_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sh_bordseg], dx
    mov word [sh_nbord], 0
    mov ax, SH_CLAIM_NOTE_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sh_noteseg], dx
    mov word [sh_nnote], 0
    mov ax, SH_CLAIM_CHART_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [sh_chartseg], dx
    mov word [sh_chartwin], 0
    mov word [sh_chart_cnt], 0
    mov word [ch_type], CH_T_COLUMN
    push si
    push di
    push cx
    push es
    mov es, dx                          ; copy the constant 118-byte BMP
    mov si, ch_hdrtpl                   ; header+palette into the buffer once
    xor di, di                          ; here - ch_bmp_write only ever stages
    mov cx, CH_HDRSZ                    ; whatever is already sitting there,
    cld                                 ; it never rebuilds it (see
    rep movsb                           ; os88chart.inc's own header comment)
    pop es
    pop cx
    pop di
    pop si
    mov word [sh_ncells], 0
    mov word [sh_txtlen], 0
    mov si, sh_tpl
    call OSAPI_WM_CREATE
    jc .fail
    mov [sh_ownwin], bx               ; stage 2.0: os88ui_ask needs our own
                                       ; window ptr, and it's asked for from
                                       ; sh_macro_eval, which has no window
                                       ; ptr of its own to hand it - Sheet
                                       ; only ever has the one window, so
                                       ; capturing it once here is safe
    mov byte [sh_mopen], SH_M_NONE    ; stage 2.x: Sheet's own menu bar -
    mov byte [sh_mhi], SH_M_NONE      ; see the SH_MBAR_H section comment
    mov byte [sh_gridlines], 1
    mov byte [sh_showformulas], 0
    mov word [sh_i_options], sh_it_grid_on   ; match sh_i_options's own
                                              ; label to the actual default
                                              ; (sh_it_form_off already does,
                                              ; since Formulas defaults off)
    mov word [sh_cellw], SH_CW_NORMAL        ; stage 2.x: runtime cell size
    mov word [sh_cellh], SH_RH_NORMAL        ; defaults - see the SH_CW_*/
    mov word [sh_cellch], SH_CW_NORMAL / 8   ; SH_RH_* section comment
    call sh_mkblank
    call sh_mtab_calc
    call sh_sheetmark

    ; stage 3.0a: drag-to-select. BX is still the window OSAPI_WM_CREATE just
    ; answered. CF=1 means kern_small, which carries the slot and not the body
    ; (os88api.inc: "TEST CF AND HAVE A SECOND PATH") - there is simply no
    ; tracking on that machine, and shift+click and shift+arrows, which need
    ; no kernel support at all, remain the way to build a range there.
    mov ax, sh_ondrag
    call OSAPI_WM_ONDRAG

    ; The RELEASE edge, which a thumb drag needs to let go on (13.10.5). Same
    ; kern_small caveat as the drag edge above: refused there, and a bar that
    ; cannot be dragged never needs dropping.
    mov ax, sh_onmouseup
    call OSAPI_WM_ONMOUSEUP

    ; stage 3.0b: the formula bar's content box. Only the buffer binding is
    ; set once - the rect is refreshed per draw by sh_flrect, since the window
    ; moves and resizes and a stale rect would draw and hit-test in the wrong
    ; place.
    mov word [sh_fline + LN_BUF], sh_editbuf
    mov word [sh_fline + LN_MAX], SH_EDITMAX + 1

    ; Arm the key-state map now rather than on the user's first shift+click.
    ; kbd_down arms itself on the first ASK and its first answer is always
    ; "up" (kernel/mouse.inc's own note), so without this the very first
    ; shift+click of a session would read as an unshifted one.
    mov al, 0x2A
    call OSAPI_KEY_DOWN

    mov si, sh_menus
    call OSAPI_MENU_SET
    mov bx, [sh_ownwin]               ; ...and 'About Sheet' above its Close,
    mov si, sh_about                  ; which is the OS's own convention and
    call OSAPI_ABOUT_SET              ; not a Help menu of one's own devising
                                       ; (SPEC.md 12.2). Seventeen packages
                                       ; already did this; Sheet had a Help >
                                       ; About... item instead, which put the
                                       ; same text somewhere nobody looks for
                                       ; it on this system.
    mov si, sh_defname
    mov di, sh_name
    call sh_strcpy
    call sh_note_arg                  ; a document double-clicked in the Disk
                                       ; window. NOTED here, READ at the first
                                       ; paint - see sh_note_arg's header
    clc
    jmp .out
.fail:
    stc
.out:
    pop di
    pop si
    pop dx
    pop ax
    ret

; =============================================================================
; Geometry
; =============================================================================

; -----------------------------------------------------------------------------
; sh_geom - in: BX = window ptr
; out: [sh_ox]/[sh_oy] content origin, [sh_cw]/[sh_ch] content size,
;      [sh_vcols]/[sh_vrows] grid cells that fit given the current scroll;
;      all registers preserved
; -----------------------------------------------------------------------------
sh_geom:
    push ax
    push cx
    push dx
    call OSAPI_WM_CONTENT
    mov [sh_ox], ax
    mov [sh_oy], dx
    add dx, SH_MBAR_H                  ; sh_goy: where the formula bar and
    mov [sh_goy], dx                   ; everything below it actually starts,
                                        ; now that the menu bar (SH_MBAR_H
                                        ; section comment) sits above them -
                                        ; sh_oy itself stays the RAW content
                                        ; origin, since sh_mbar_draw needs
                                        ; that one, not the shifted one
    call OSAPI_WM_GEOM
    mov [sh_cw], cx
    mov [sh_ch], dx

    mov ax, cx
    sub ax, SH_RH_W + SH_VSB_W         ; the vertical bar owns a strip at the
    jns .cw_ok                          ; right, so the grid is that much
    xor ax, ax                          ; narrower
.cw_ok:
    xor dx, dx
    mov cx, [sh_cellw]
    div cx
    mov cx, SH_COLS
    sub cx, [sh_scrollcol]
    cmp ax, cx
    jbe .cset
    mov ax, cx
.cset:
    mov [sh_vcols], ax

    mov ax, [sh_ch]
    sub ax, SH_MBAR_H + SH_FB_H + SH_CH_H + SH_SB_H + SH_HSB_H
    jns .chh_ok                         ; ...and the horizontal bar a strip
    xor ax, ax                          ; above the status bar
.chh_ok:
    xor dx, dx
    mov cx, [sh_cellh]
    div cx
    mov cx, SH_ROWS
    sub cx, [sh_scrollrow]
    cmp ax, cx
    jbe .rset
    mov ax, cx
.rset:
    mov [sh_vrows], ax

    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; Callbacks
; =============================================================================

; -----------------------------------------------------------------------------
; sh_note_arg - take the document this instance was launched with, if any.
; OSAPI_ARG_FILE is READ-AND-CLEAR (SPEC.md 54.5), so asking once here spends
; it and a later instance can never inherit it.
;
; THIS COPIES A NAME AND TOUCHES NO DISK, and that is the whole point of
; splitting it from sh_deferred_ld. A floppy read inside the entry proc runs
; under the LOADER LOCK and freezes the desktop (SPEC.md 69.6) - texpad's own
; ARG_FILE note carries the same warning, having paid for it.
; -----------------------------------------------------------------------------
sh_note_arg:
    push ax
    push bx                           ; ARG_FILE answers in BL, and the entry
    push cx                           ; proc that calls this has not banked BX
    push dx
    push si
    push di
    push es
    call OSAPI_ARG_FILE
    jc .none                          ; CF=1 = launched empty, the usual case
    mov [sh_argdir], dx
    mov [sh_argdrv], bl
    mov ax, KERNEL_SEG                ; the name lives in the KERNEL's segment,
    mov es, ax                        ; not ours
    mov di, sh_name
    mov cx, SH_NAMEMAX
.cp:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.named:
    mov byte [sh_needld], 1
.none:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_deferred_ld - and NOW the disk read, from the first paint, which happens
; after the window is up and the loader lock is long gone. Clears the flag
; first, so a read that fails is not retried on every repaint for the rest of
; the session.
; -----------------------------------------------------------------------------
sh_deferred_ld:
    push ax
    push bx
    push cx
    push dx
    push si                           ; sh_paint takes SI as its window ptr
    push di                           ; the instruction after this returns -
    push es                           ; OSAPI_FILE_GOTO documents no output
    mov byte [sh_needld], 0           ; but promises nothing about SI either

    mov dx, [sh_argdir]
    mov bl, [sh_argdrv]
    call OSAPI_FILE_GOTO
    jc .out                           ; could not list it: the volume is back
    call sh_doread                    ; at the root and sh_name still names a
.out:                                 ; file that is not here - leave the
    pop es                            ; sheet empty rather than half-read
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sh_paint:
    push bx
    cmp byte [sh_needld], 0           ; the associated document lands HERE, so
    je .nold                          ; it is on screen at the first paint
    call sh_deferred_ld               ; instead of waiting for a key or click
.nold:
    mov bx, si
    call sh_geom
    call sh_drawall
    pop bx
    ret

sh_repaint:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call sh_geom
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sh_ox]
    mov bx, [sh_oy]
    mov cx, [sh_ox]
    add cx, [sh_cw]
    dec cx
    mov dx, [sh_oy]
    add dx, [sh_ch]
    dec dx
    call OSAPI_GFX_FILL
    call sh_drawall
    cmp word [sh_chartwin], 0           ; stage 2.x: keep the live Chart Column
    je .nochart                         ; window in sync with every data-
    cmp byte [sh_chartdirty], 0         ; changing command that already routes
    je .nochart                         ; through sh_repaint - and ONLY those:
    push bx                            ; sh_addcell/sh_removecell set the dirty
    mov bx, [sh_chartwin]              ; byte, so a selection move, an edit
    call OSAPI_WM_OBSCURED              ; keystroke or a scroll (which change no
    jc .chartobscured                   ; cell) skips the re-scan and the whole
    call sh_chart_scan                  ; 240x160 re-raster. Skipped too while
    call sh_chart_render                ; nobody can see it (also covers
    mov si, [sh_chartwin]               ; "hidden via its own close box", which
    call sh_chart_paint                 ; only hides it - see sh_docmd_chart's
    mov byte [sh_chartdirty], 0         ; window-lifecycle comment); the byte
                                         ; then STAYS set, so the first repaint
                                         ; with the chart visible resyncs it.
                                         ; sh_chart_scan is what re-reads the
                                         ; charted column's current values;
                                         ; sh_chart_render re-rasterizes them.
.chartobscured:
    pop bx
.nochart:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_onclick - W_ONCLICK: CX=x, DX=y (screen), SI=window; gfx lock held
; -----------------------------------------------------------------------------
sh_onclick:
    push ax
    push bx
    push cx
    push dx
    cmp word [sh_fdlg_win], 0
    je .nofdlg
    call sh_fdlg_close                 ; stage 1.8: a Format dialog isn't
                                        ; kernel-modal (no fdlg_grab/fdlg_top
                                        ; machinery outside the kernel - see
                                        ; the section comment above
                                        ; sh_fdlg_open), so a click that
                                        ; reaches the main grid at all means
                                        ; the dialog visually lost focus;
                                        ; treat it as Cancel rather than
                                        ; leave sh_fdlg_win stuck non-zero,
                                        ; which would gate every future
                                        ; Format menu command shut for good
.nofdlg:
    cmp word [sh_bdlg_win], 0          ; same non-modal gate-lock risk, same
    je .nobdlg                         ; recovery, for the Border dialog
    call sh_bdlg_close
.nobdlg:
    mov word [sh_msg], 0
    mov byte [sh_dragging], 0          ; stage 3.0a: a gesture is only a grid
                                        ; drag if it STARTS on the grid - the
                                        ; menu-bar path below never arms it
    mov bx, si
    call sh_geom
    call sh_mbar_hit                   ; stage 2.x: Sheet's own in-window
    cmp al, SH_M_NONE                  ; menu bar (see the SH_MBAR_H section
    je .notmenu                        ; comment) claims a click on its strip
    call sh_mtrack                     ; before anything below ever sees it -
    jmp .out                           ; AL=menu index (from sh_mbar_hit),
                                        ; SI=window (still this callback's own
                                        ; untouched SI)
.notmenu:
    call sh_sbclick                    ; stage 3.0a+: the two scroll bars get
    jc .out                            ; the click before the grid does
    call sh_gridhit                    ; CX=x, DX=y -> CF=1 + AX=col, BX=row
    jnc .out
    call sh_shiftdown                  ; stage 3.0a: shift+click extends the
    jc .extend                         ; range from the existing anchor
    call sh_select                     ; plain click: collapse and move
    mov byte [sh_dragging], 1          ; ...and arm the drag from here
    push ax
    mov ax, [sh_selcol]
    mov [sh_drag_col], ax
    mov ax, [sh_selrow]
    mov [sh_drag_row], ax
    pop ax
    jmp .out
.extend:
    call sh_select_to
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_select - commit any pending edit, move the selection, scroll to show
; it, and repaint. AX = new column, BX = new row. SI must be the window
; ptr; not touched here so it stays that way for sh_repaint.
; -----------------------------------------------------------------------------
sh_select:
    push ax
    push bx
    mov word [sh_tabanchor], 0       ; ANY other move ends a Tab run - only the
    call sh_selbank                  ; Tab arm puts the anchor back afterwards
    call sh_commit
    mov [sh_selcol], ax
    mov [sh_selrow], bx
    mov [sh_selcol2], ax               ; stage 3.0a: a plain select COLLAPSES
    mov [sh_selrow2], bx               ; the range - anchor and extent become
                                        ; the same cell, which is exactly the
                                        ; old single-cell behaviour every
                                        ; existing caller still expects
    call sh_scrollto
    call sh_selpaint                   ; only what the move dirtied - the full
                                        ; repaint costs ~1s on a 4.77MHz 8088
                                        ; and this path runs per arrow key
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_select_to - stage 3.0a: move only the EXTENT of the range, leaving the
; anchor where it is. AX = column, BX = row. Used by shift+click, shift+arrows
; and the drag handler. SI must be the window ptr (sh_repaint's contract).
;
; Deliberately does NOT call sh_commit: extending a selection is not a
; different-cell move, and committing here would end an in-progress edit
; halfway through a drag.
; -----------------------------------------------------------------------------
sh_select_to:
    push ax
    push bx
    call sh_selbank
    cmp ax, SH_COLS
    jb .colok
    mov ax, SH_COLS - 1
.colok:
    cmp bx, SH_ROWS
    jb .rowok
    mov bx, SH_ROWS - 1
.rowok:
    mov [sh_selcol2], ax
    mov [sh_selrow2], bx
    call sh_scrollto2
    call sh_selpaint
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_selbank - bank the rect and the scroll origin a selection move starts
; from, so sh_selpaint can price the damage afterwards. Preserves everything.
; -----------------------------------------------------------------------------
sh_selbank:
    push ax
    call sh_selrect
    mov ax, [sh_selc1]
    mov [sh_oldc1], ax
    mov ax, [sh_selc2]
    mov [sh_oldc2], ax
    mov ax, [sh_selr1]
    mov [sh_oldr1], ax
    mov ax, [sh_selr2]
    mov [sh_oldr2], ax
    mov ax, [sh_scrollcol]
    mov [sh_oldscol], ax
    mov ax, [sh_scrollrow]
    mov [sh_oldsrow], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_selpaint - repaint after a selection move: as little of the window as
; the move actually dirtied. SI = the window (sh_repaint's own contract).
;
; The full repaint is ~1s on the target (PERFORMANCE.md's own table: one
; OSAPI_FONT_RUN per visible cell, plus all the chrome, plus a recalc pass),
; and this path runs once per keystroke-repeat and per drag packet - so it
; pays that price only when it must:
;   * sh_commit stored something -> full, because a dependent formula
;     anywhere on screen may now show a new value and only sh_drawall's
;     pass advance re-evaluates them;
;   * the view scrolled by rows only -> blit the surviving rows
;     (sh_scrollrow_blit) and letter just the vacated ones;
;   * by columns only -> the grid and its column half, nothing else
;     (sh_scrollcol_part - OSAPI_GFX_SCROLL is vertical-only, SPEC.md 5.5);
;   * no scroll at all -> the old cells and the new ones (sh_updsel).
; -----------------------------------------------------------------------------
sh_selpaint:
    push ax
    push cx
    cmp byte [sh_commitdirty], 0
    jne .full
    mov ax, [sh_oldscol]
    cmp ax, [sh_scrollcol]
    jne .cols
    mov cx, [sh_oldsrow]
    cmp cx, [sh_scrollrow]
    jne .rows
    call sh_updsel
    jmp .out
.rows:
    call sh_scrollrow_blit             ; CX = the row the view is leaving
    jc .full                           ; refused: pay the full price
    call sh_updsel                     ; old cells + new cells + the bars
    jmp .out
.cols:
    mov cx, [sh_oldsrow]
    cmp cx, [sh_scrollrow]
    jne .full                          ; both axes moved: a Goto, not a walk
    call sh_scrollcol_part
    call sh_drawbar                    ; the reference box changed too
    call sh_drawstatus
    jmp .out
.full:
    mov byte [sh_commitdirty], 0
    call sh_repaint
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_updsel - the no-scroll damage path: redraw the union of the old and the
; new selection rects (which covers both frames), then the two bars whose
; text names the selection. SI = the window.
; -----------------------------------------------------------------------------
sh_updsel:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call sh_geom
    cmp word [sh_vcols], 0
    je .chrome
    cmp word [sh_vrows], 0
    je .chrome
    call sh_selrect                    ; the NEW rect, ordered
    mov ax, [sh_selc1]                 ; the union's columns...
    cmp ax, [sh_oldc1]
    jbe .c1
    mov ax, [sh_oldc1]
.c1:
    mov cx, [sh_selc2]
    cmp cx, [sh_oldc2]
    jae .c2
    mov cx, [sh_oldc2]
.c2:
    mov dx, [sh_scrollcol]             ; ...clamped to the viewport, in
    cmp cx, dx                         ; window cells
    jb .chrome                         ; wholly left of the view
    sub cx, dx
    cmp cx, [sh_vcols]
    jb .c2ok
    mov cx, [sh_vcols]
    dec cx
.c2ok:
    sub ax, dx
    jns .c1ok
    xor ax, ax
.c1ok:
    cmp ax, [sh_vcols]
    jae .chrome                        ; wholly right of it
    mov [sh_dmgc1], ax
    mov [sh_dmgc2], cx
    mov ax, [sh_selr1]                 ; the union's rows, the same
    cmp ax, [sh_oldr1]
    jbe .r1
    mov ax, [sh_oldr1]
.r1:
    mov cx, [sh_selr2]
    cmp cx, [sh_oldr2]
    jae .r2
    mov cx, [sh_oldr2]
.r2:
    mov dx, [sh_scrollrow]
    cmp cx, dx
    jb .chrome
    sub cx, dx
    cmp cx, [sh_vrows]
    jb .r2ok
    mov cx, [sh_vrows]
    dec cx
.r2ok:
    sub ax, dx
    jns .r1ok
    xor ax, ax
.r1ok:
    cmp ax, [sh_vrows]
    jae .chrome
    mov [sh_dmgr1], ax
    mov [sh_dmgr2], cx
    call sh_dmgdraw
    call sh_drawsel
.chrome:
    call sh_drawbar
    call sh_drawstatus
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_gridhit - stage 3.0a: which cell is this screen point on?
; in:  CX = x, DX = y (screen coords, W_ONCLICK/W_ONDRAG's own)
; out: CF=1 and AX = column, BX = row (both absolute, scroll-adjusted);
;      CF=0 if the point is not over a grid cell. CX/DX restored.
;
; Lifted verbatim out of sh_onclick so the drag handler hit-tests exactly the
; same way a click does - two copies of this arithmetic would drift the first
; time the header or menu-bar height changed.
; -----------------------------------------------------------------------------
sh_gridhit:
    push cx
    push dx
    mov ax, cx
    sub ax, [sh_ox]
    sub ax, SH_RH_W
    js .no
    mov bx, dx
    sub bx, [sh_goy]                   ; grid origin, NOT raw content origin -
    sub bx, SH_FB_H + SH_CH_H          ; the menu bar strip sits above it
    js .no
    xor dx, dx
    mov cx, [sh_cellw]
    div cx
    cmp ax, [sh_vcols]
    jae .no
    add ax, [sh_scrollcol]
    mov [sh_wcol], ax
    mov ax, bx
    xor dx, dx
    mov cx, [sh_cellh]
    div cx
    cmp ax, [sh_vrows]
    jae .no
    add ax, [sh_scrollrow]
    mov bx, ax
    mov ax, [sh_wcol]
    pop dx
    pop cx
    stc
    ret
.no:
    pop dx
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; sh_flkey - stage 3.0b: hand one keystroke to the formula bar's field, then
; resync this app's own sh_editlen from the field's LN_LEN so sh_commit and
; every other existing reader keeps working unchanged.
; in: AL = ascii, AH = scan.
;
; REDRAWS ONLY THE FIELD. An editing keystroke changes no cell, so the full
; sh_repaint this used to end with - every visible cell re-lettered plus a
; recalc pass, ~1s per keystroke on a 4.77MHz 8088 - repainted identical
; pixels and dropped keys on the target. os88line_draw is one opaque run
; plus the strip past the text; sh_flmarg covers the one span it does not.
; -----------------------------------------------------------------------------
sh_flkey:
    push ax
    push si
    call sh_flrect                     ; the box may have moved since the last
                                        ; draw - os88line hit-tests and draws
                                        ; from the same four words
    mov si, sh_fline
    call os88line_key
    mov ax, [si + LN_LEN]
    mov [sh_editlen], al               ; LN_LEN is a word and SH_EDITMAX is
                                        ; 63, so the low byte is the whole of
                                        ; it - but keep them in step, because
                                        ; sh_commit still reads sh_editlen
    call sh_flmarg
    call os88line_draw
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_flmarg - white the strip between the content box's frame and os88line's
; 8-aligned pen. The field's own draw covers its run and the strip PAST the
; text (os88line_draw's header), so this margin is the one span neither
; touches - and on the first keystroke of an edit it still holds the leftmost
; pixels of the static text sh_drawbar drew there. SI = sh_fline, whose rect
; sh_flrect has already refreshed. Preserves everything.
; -----------------------------------------------------------------------------
sh_flmarg:
    push ax
    push bx
    push cx
    push dx
    call os88line_pen
    mov cx, ax
    dec cx                             ; the margin's right edge...
    mov ax, [si + LN_X1]
    inc ax                             ; ...and its left, inside the frame
    cmp ax, cx
    jg .none
    mov bx, [si + LN_Y1]
    inc bx
    mov dx, [si + LN_Y2]
    dec dx
    cmp bx, dx
    jg .none
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.none:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_flsync - stage 3.0b: the buffer was filled by someone other than the
; field (F2's seed-from-cell, or a Paste). Recompute the field's own length
; from the NUL and park the caret at the end, which is where a just-loaded
; value should leave it. Preserves everything.
; -----------------------------------------------------------------------------
sh_flsync:
    push ax
    push cx
    push si
    xor cx, cx
    mov si, sh_editbuf
.cnt:
    cmp byte [si], 0
    je .done
    cmp cx, SH_EDITMAX                 ; never trust an unterminated buffer
    jae .done
    inc si
    inc cx
    jmp .cnt
.done:
    mov [sh_editlen], cl
    mov si, sh_fline
    mov [si + LN_LEN], cx
    mov [si + LN_CAR], cx              ; caret at the end
    mov word [si + LN_VIEW], 0
    mov byte [si + LN_FOCUS], 1
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_editstart - stage 3.0b: begin a fresh, EMPTY edit (the first character
; typed into a cell). Resets both this app's own edit state and the field's.
; -----------------------------------------------------------------------------
sh_editstart:
    push si
    mov byte [sh_editing], 1
    mov byte [sh_editlen], 0
    mov byte [sh_editbuf], 0
    mov si, sh_fline
    mov word [si + LN_LEN], 0
    mov word [si + LN_CAR], 0
    mov word [si + LN_VIEW], 0
    mov byte [si + LN_FOCUS], 1
    pop si
    ret

; -----------------------------------------------------------------------------
; sh_arrowsrc - stage 3.0a: where does an arrow key start counting from?
; out: AX = column, BX = row - the ANCHOR normally, the EXTENT while shift is
; held, which is what makes shift+arrow grow the block from the end the user
; last moved rather than snapping it back to the anchor.
; -----------------------------------------------------------------------------
sh_arrowsrc:
    call sh_shiftdown
    jc .ext
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    ret
.ext:
    mov ax, [sh_selcol2]
    mov bx, [sh_selrow2]
    ret

; -----------------------------------------------------------------------------
; sh_shiftdown - out: CF=1 if either shift key is held. Preserves everything.
; 0x2A/0x36 are the set-1 make codes; kbd_down's map is 128 bits wide, one per
; make code, so it answers for any key and not just the named KSC_* few.
; -----------------------------------------------------------------------------
sh_shiftdown:
    push ax
    mov al, 0x2A                       ; left shift
    call OSAPI_KEY_DOWN
    jc .yes
    mov al, 0x36                       ; right shift
    call OSAPI_KEY_DOWN
    jc .yes
    pop ax                             ; pop leaves the flags alone
    clc
    ret
.yes:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sh_ondrag - W_ONDRAG (SPEC.md 13.8.2): the pointer moved while our press was
; armed. CX = x, DX = y, SI = window; UI task, gfx lock held.
;
; REDRAWS ONLY ON A CHANGE, which the slot's own doc insists on: it fires per
; mouse packet, and a repaint per packet is tens of milliseconds each on a
; 4.77MHz machine. [sh_drag_col]/[sh_drag_row] hold the cell we last extended
; to, so sliding within one cell costs a hit-test and nothing else.
; -----------------------------------------------------------------------------
sh_ondrag:
    push ax
    push bx
    push cx
    push dx
    push si
    ; stage 3.0a+: a live scroll-thumb drag owns the gesture before the grid
    ; selection does.
    call sh_sbsync
    call os88ui_sbdragging
    jc .novthumb
    mov bx, sh_vsb
    call os88ui_sbtrack                ; DX = the pointer's y
    jc .out                            ; nothing owed (no move, or the rate)
    call sh_setscrollrow
    jmp .out
.novthumb:
    cmp byte [sh_hsb_dragon], 0
    je .nohthumb
    mov bx, sh_hsb
    call sh_hsb_track                  ; CX = the pointer's x
    jc .out
    call sh_setscrollcol
    jmp .out
.nohthumb:
    cmp byte [sh_dragging], 0
    je .out                            ; this gesture did not start on the grid
    call sh_gridhit
    jnc .out                           ; slid off the grid: leave the range as
                                        ; it was rather than clamping wildly
    cmp ax, [sh_drag_col]
    jne .moved
    cmp bx, [sh_drag_row]
    je .out                            ; same cell as last packet - nothing
.moved:
    mov [sh_drag_col], ax
    mov [sh_drag_row], bx
    mov si, [sh_ownwin]                ; sh_repaint's SI contract
    call sh_select_to
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_selrect - stage 3.0a: normalize the anchor/extent pair into an ordered
; rect. Out: [sh_selc1] <= [sh_selc2], [sh_selr1] <= [sh_selr2]. Every range
; consumer reads these rather than comparing the raw pair itself, so "which
; corner did the user start from" is answered in exactly one place.
; -----------------------------------------------------------------------------
sh_selrect:
    push ax
    push bx
    mov ax, [sh_selcol]
    mov bx, [sh_selcol2]
    cmp ax, bx
    jbe .cols_ok
    xchg ax, bx
.cols_ok:
    mov [sh_selc1], ax
    mov [sh_selc2], bx
    mov ax, [sh_selrow]
    mov bx, [sh_selrow2]
    cmp ax, bx
    jbe .rows_ok
    xchg ax, bx
.rows_ok:
    mov [sh_selr1], ax
    mov [sh_selr2], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_selsingle - out: CF=1 if the selection is a single cell (anchor==extent).
; The gate every command that has no range semantics yet uses.
; -----------------------------------------------------------------------------
sh_selsingle:
    push ax
    mov ax, [sh_selcol]
    cmp ax, [sh_selcol2]
    jne .no
    mov ax, [sh_selrow]
    cmp ax, [sh_selrow2]
    jne .no
    stc
    jmp .out
.no:
    clc
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_scrollto - move the scroll origin the least amount that brings the
; current selection into the viewport described by [sh_vcols]/[sh_vrows]
; -----------------------------------------------------------------------------
sh_scrollto:
    push ax
    mov ax, [sh_selcol]
    mov [sh_sc_tcol], ax
    mov ax, [sh_selrow]
    mov [sh_sc_trow], ax
    pop ax
    jmp sh_scrollto_t

; stage 3.0a: the same scroll, aimed at the range's moving END instead of its
; anchor - what a drag or a shift+arrow needs, since it is the extent that
; walks off-screen, not the anchor.
sh_scrollto2:
    push ax
    mov ax, [sh_selcol2]
    mov [sh_sc_tcol], ax
    mov ax, [sh_selrow2]
    mov [sh_sc_trow], ax
    pop ax
    jmp sh_scrollto_t

; the core: bring [sh_sc_tcol]/[sh_sc_trow] into the viewport, moving the
; scroll origin the least amount that does it
sh_scrollto_t:
    push ax
    push bx
    mov ax, [sh_sc_tcol]
    mov bx, [sh_scrollcol]
    cmp ax, bx
    jae .cfwd
    mov [sh_scrollcol], ax
    jmp short .rows
.cfwd:
    add bx, [sh_vcols]
    cmp bx, 0
    je .rows
    dec bx
    cmp ax, bx
    jbe .rows
    sub ax, [sh_vcols]
    inc ax
    mov [sh_scrollcol], ax
.rows:
    mov ax, [sh_sc_trow]
    mov bx, [sh_scrollrow]
    cmp ax, bx
    jae .rfwd
    mov [sh_scrollrow], ax
    jmp short .out
.rfwd:
    add bx, [sh_vrows]
    cmp bx, 0
    je .out
    dec bx
    cmp ax, bx
    jbe .out
    sub ax, [sh_vrows]
    inc ax
    mov [sh_scrollrow], ax
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_onkey - W_ONKEY: AL=ascii (0 for a navigation key), AH=scan, SI=window
; -----------------------------------------------------------------------------
sh_onkey:
    push ax
    push bx
    push cx
    push dx
    cmp word [sh_fdlg_win], 0
    je .nofdlg
    call sh_fdlg_close                 ; see sh_onclick's own copy of this
                                        ; guard for why
.nofdlg:
    cmp word [sh_bdlg_win], 0
    je .nobdlg
    call sh_bdlg_close
.nobdlg:
    mov word [sh_msg], 0
    mov bx, si
    call sh_geom
    or al, al
    jz .navkey
    ; stage 3.0a: a shift+arrow arrives WITH an ASCII byte. The arrow and the
    ; keypad digit share one scancode - 0x4D is both Right and KP-6, the E0
    ; prefix naming no key of its own (kernel/mouse.inc's own note) - so the
    ; kernel's shifted translation hands us '6'. Without this test, holding
    ; shift and pressing an arrow would TYPE A DIGIT into the cell instead of
    ; extending the selection, which is exactly what it did before this check.
    call sh_shiftdown
    jnc .typing
    cmp ah, 0x4B
    je .navkey
    cmp ah, 0x4D
    je .navkey
    cmp ah, 0x48
    je .navkey
    cmp ah, 0x50
    je .navkey
    jmp .typing
.navkey:
    ; stage 3.0b: while an edit is in progress, the keys that move a CARET
    ; belong to the field, not to the grid - Left/Right/Home/End and Delete.
    ; Up/Down deliberately still commit and move the selection, which is what
    ; Excel does during cell entry.
    cmp byte [sh_editing], 0
    je .navgrid
    cmp ah, 0x4B                     ; Left
    je .navfield
    cmp ah, 0x4D                     ; Right
    je .navfield
    cmp ah, 0x47                     ; Home
    je .navfield
    cmp ah, 0x4F                     ; End
    je .navfield
    cmp ah, 0x53                     ; Delete
    je .navfield
    jmp .navgrid
.navfield:
    call sh_flkey
    jmp .out
.navgrid:
    cmp ah, 0x4B                     ; Left
    je .left
    cmp ah, 0x4D                     ; Right
    je .right
    cmp ah, 0x48                     ; Up
    je .up
    cmp ah, 0x50                     ; Down
    je .down
    cmp ah, 0x49                     ; Page Up
    je .pgup
    cmp ah, 0x51                     ; Page Down
    je .pgdn
    cmp ah, 0x47                     ; Home: back to column A
    je .home
    cmp ah, 0x53                     ; Delete: clear the selected cell
    je .delcell
    cmp ah, 0x3C                     ; F2: edit the cell in place
    je .f2
    jmp .out
.typing:
    cmp al, 27                       ; Escape: cancel the edit
    jne .notesc
    cmp byte [sh_editing], 0
    je .out
    mov byte [sh_editing], 0
    call sh_repaint
    jmp .out
.notesc:
    cmp al, 13                       ; Enter: commit, move down - and back to
    jne .nottab                      ; the column this ROW's entry started in,
    call sh_commit                   ; which is what Excel does after a run of
    mov ax, [sh_tabanchor]           ; Tabs. sh_select clears the anchor, so
    or ax, ax                        ; Enter consuming it needs no extra step
    jz .noanchor
    dec ax                           ; stored as col+1, see the Tab arm below
    jmp .enterrow
.noanchor:
    mov ax, [sh_selcol]
.enterrow:
    mov bx, [sh_selrow]
    inc bx
    cmp bx, SH_ROWS
    jb .entergo
    mov bx, SH_ROWS - 1
.entergo:
    call sh_select
    jmp .out
.nottab:
    cmp al, 9                        ; Tab: commit, move right
    jne .notbs
    call sh_commit
    mov ax, [sh_tabanchor]           ; the first Tab of a run records where it
    or ax, ax                        ; started; later ones keep that. Stored as
    jnz .haveanchor                  ; col+1, so a ZEROED bss reads as "none"
    mov ax, [sh_selcol]              ; and no init pass is needed
    inc ax
.haveanchor:
    push ax                          ; sh_select clears it, so it is put back
    mov ax, [sh_selcol]              ; afterwards rather than before
    mov bx, [sh_selrow]
    inc ax
    cmp ax, SH_COLS
    jb .tabgo
    mov ax, SH_COLS - 1
.tabgo:
    call sh_select
    pop ax
    mov [sh_tabanchor], ax
    jmp .out
.notbs:
    cmp al, 8                        ; Backspace: the field owns it now, so it
    jne .notdigit                    ; deletes AT THE CARET rather than only
    cmp byte [sh_editing], 0         ; ever chopping the last character
    je .out
    call sh_flkey
    jmp .out
.notdigit:
    ; STAGE 4.5 REPLACED AN ALLOW-LIST WITH A RANGE, and the reason is that
    ; the list had stopped describing anything. It grew one character at a
    ; time as the formula language did - '=' then the operators, then <> for
    ; comparisons, then ! and " for cross-sheet refs and ALERT's string
    ; literal, then '.' for SET.VALUE, then '$' for absolute references, then
    ; '^' for the power operator - and each addition was found the same way:
    ; the parser handled the character perfectly and the character never
    ; reached it, because THIS gate dropped it first.
    ;
    ; A cell that can hold a LABEL ends the argument. A label is arbitrary
    ; text; there is no subset of printable ASCII a column heading is not
    ; allowed to contain, and an apostrophe or a percent sign being rejected
    ; is a bug with no upside. So the gate now asks the only question it can
    ; actually answer - is this a printable character - and leaves deciding
    ; what the characters MEAN to sh_commit, which is where that decision
    ; belongs and where it already lives.
    cmp al, ' '
    jb .out                            ; control characters are handled above
    cmp al, 0x7E                       ; (Enter, Escape, Backspace, arrows)
    ja .out                            ; and are not text
.accept:
    cmp byte [sh_editing], 0
    jnz .append
    call sh_editstart                ; first character into an empty cell
.append:
    call sh_flkey                    ; the field inserts AT THE CARET and
    jmp .out                         ; bounds itself against LN_MAX
; stage 3.0a: an arrow moves the ANCHOR (collapsing the range) normally, or
; walks the EXTENT when shift is held. Both halves share one source-load and
; one dispatch rather than four near-copies of each.
.left:
    call sh_arrowsrc
    or ax, ax
    jz .out
    dec ax
    jmp .arrowgo
.right:
    call sh_arrowsrc
    cmp ax, SH_COLS - 1
    jae .out
    inc ax
    jmp .arrowgo
.up:
    call sh_arrowsrc
    or bx, bx
    jz .out
    dec bx
    jmp .arrowgo
.down:
    call sh_arrowsrc
    cmp bx, SH_ROWS - 1
    jae .out
    inc bx
.arrowgo:
    call sh_shiftdown
    jc .arrowext
    call sh_select
    jmp .out
.arrowext:
    call sh_select_to
    jmp .out
.pgup:
    mov bx, [sh_selrow]
    mov ax, [sh_vrows]
    cmp bx, ax
    jae .pgup_sub
    xor bx, bx
    jmp .pgup_go
.pgup_sub:
    sub bx, ax
.pgup_go:
    mov ax, [sh_selcol]
    call sh_select
    jmp .out
.pgdn:
    mov bx, [sh_selrow]
    add bx, [sh_vrows]
    cmp bx, SH_ROWS - 1
    jbe .pgdn_go
    mov bx, SH_ROWS - 1
.pgdn_go:
    mov ax, [sh_selcol]
    call sh_select
    jmp .out
.home:
    xor ax, ax
    mov bx, [sh_selrow]
    call sh_select
    jmp .out
.f2:
    call sh_beginedit
    jmp .out
.delcell:
    mov byte [sh_editing], 0
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_clearcell
    call sh_repaint
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_beginedit - F2: seed the edit buffer from the selected cell's current
; value (blank if the cell is empty) and enter edit mode. SI must be the
; window ptr for sh_repaint.
; -----------------------------------------------------------------------------
sh_beginedit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, si                        ; DX = window ptr, stashed (SI is used
                                       ; as scratch throughout this function)
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_findcell
    jnc .blank
    push es
    mov es, [sh_cellseg]
    test byte [es:di+4], 1
    jz .plainval
    mov ax, [es:di+SH_C_FOFF]                 ; formula_off
    pop es
    mov byte [sh_editbuf], '='
    mov di, sh_editbuf + 1
    mov si, ax
    push es
    mov es, [sh_txtseg]
.copyf:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyf
    pop es
    jmp .havelen
.plainval:
    call sh_cellnum                   ; the value as decimal text
    pop es
    mov si, sh_numbuf
    mov di, sh_editbuf
    call sh_strcpy
.havelen:
    xor cx, cx
    mov si, sh_editbuf
.cnt:
    cmp byte [si], 0
    je .setlen
    inc si
    inc cx
    jmp .cnt
.setlen:
    mov [sh_editlen], cl
    jmp .go
.blank:
    mov byte [sh_editbuf], 0
    mov byte [sh_editlen], 0
.go:
    mov byte [sh_editing], 1
    call sh_flsync                    ; stage 3.0b: the field's own length,
                                       ; caret and scroll must match the
                                       ; buffer we just seeded, or the caret
                                       ; draws somewhere the text is not
    mov si, dx                        ; SI = window ptr, restored
    call sh_repaint
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_paren_ok - do the parentheses in [sh_editbuf] balance? out: CF=1 = no.
;
; Lexical only, and deliberately so: it runs at COMMIT, where re-running the
; evaluator would mean recursion, cycle marks and memoisation stamps as a side
; effect of typing. It catches the bracket a person actually drops; it does not
; claim to be a syntax check, and something like `=1+` still commits and reads
; as 0. A real answer needs an error VALUE - SH_T_ERR is reserved and nothing
; produces one yet - and that is a feature, not this.
;
; A quoted string is skipped whole, so a bracket inside a label literal does
; not count toward the balance.
; -----------------------------------------------------------------------------
sh_paren_ok:
    push ax
    push cx
    push si
    mov si, sh_editbuf
    xor cx, cx                        ; cx = how many are still open
.scan:
    mov al, [si]
    or al, al
    jz .done
    inc si
    cmp al, '"'
    je .instr
    cmp al, '('
    je .open
    cmp al, ')'
    jne .scan
    or cx, cx
    jz .bad                           ; a ')' with nothing open
    dec cx
    jmp .scan
.open:
    inc cx
    jmp .scan
.instr:
    mov al, [si]
    or al, al
    jz .done
    inc si
    cmp al, '"'
    jne .instr
    jmp .scan
.done:
    or cx, cx
    jnz .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_commit - if a cell is being edited, parse the buffer and store it (an
; empty buffer, or one that doesn't parse as a single signed integer,
; clears the cell instead); either way stop editing. SI is not touched.
; Out: CF=1 when the store was REFUSED (text arena or cell table full) and
; the cell keeps what it had - sh_sort_permcol stops on it; every older
; caller ignores it, which is what the silence always was.
; -----------------------------------------------------------------------------
sh_commit:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    cmp byte [sh_editing], 0
    je .out
    mov byte [sh_editing], 0
    mov byte [sh_commitdirty], 1      ; cell data changes below (even an empty
                                      ; buffer clears the cell) - sh_selpaint
                                      ; reads this and pays the full repaint,
                                      ; whose pass advance is what re-shows
                                      ; every dependent formula
    cmp byte [sh_editlen], 0
    jne .have
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_clearcell
    clc                               ; clearing is never refused
    jmp .out
.have:
    cmp byte [sh_editbuf], '='
    jne .numeric
    call sh_paren_ok                  ; ...and a formula must be well formed,
    jc .badformula                    ; for the same reason "3.5kg" is not 3.5
    mov si, sh_editbuf
    inc si                            ; past the '='
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_setformula
    jmp .out
.badformula:
    ; `=SUM(E2:E5` - one missing bracket - used to be STORED AS A FORMULA and
    ; quietly evaluated to 0, which is the worst answer available: a plausible
    ; number, in the right place, that nobody has reason to doubt. It is kept
    ; as a LABEL instead, so the cell shows the text that was typed, and the
    ; status bar says why. That is this app's existing rule for input that
    ; cannot be what it looks like, applied to the one type that was exempt.
    mov word [sh_msg], sh_s_badparen
    jmp .astext
.numeric:
    mov si, sh_editbuf                ; stage 4.0: a full decimal, not a signed
    call fp_atof                      ; integer. "3.5", "-0.25" and "1e3" are
    jc .astext                        ; all values now; anything fp_atof does
    mov al, [si]                      ; not consume ENTIRELY is not a number,
    or al, al                         ; which is what keeps "3.5kg" from
    jnz .astext                       ; silently becoming 3.5
    call sh_acc_store
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_setvald
    jmp .out
.astext:
    ; stage 4.5: what used to happen here was sh_clearcell - anything that
    ; would not parse as a number was DISCARDED, and typing a column heading
    ; left the cell empty. Content decides the type, exactly as Excel does it:
    ; '=' is a formula, a complete number is a number, and everything else is
    ; a label. There is no forcing prefix because Excel 2.1 has none either
    ; (the leading ' " ^ \ are Lotus's, not Excel's) - a cell that must hold
    ; "1990" as text is a Format problem, not an entry one.
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    mov si, sh_editbuf
    call sh_settext
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing
; =============================================================================

sh_drawall:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sh_calcmanual], 0       ; stage 3.0c Options > Calculation. NOT
    jne .nocalc                       ; advancing the pass stamp is the whole
    inc word [sh_pass]                ; mechanism: sh_eval_cell's memoization
.nocalc:                              ; keys off it, so every formula reads as
                                       ; a cache hit and nothing re-evaluates.
                                       ; One recalculation pass per full
                                       ; repaint, when it is automatic.
    call sh_mbar_draw
    call sh_drawbar
    call sh_drawstatus
    call sh_sbsync                    ; stage 3.0a+: both scroll bars, from
    mov bx, sh_vsb                    ; the live geometry and scroll position
    call os88ui_sbar
    mov bx, sh_hsb
    call sh_hsb_draw
    call sh_drawcolhdrs
    call sh_drawrowhdrs
    call sh_dmgfull                   ; the three grid painters below are
    call sh_drawgrid                  ; RANGED now (sh_dmgc1..sh_dmgr2); a
    call sh_drawlines                 ; full draw is the whole viewport
    call sh_drawborders
    call sh_drawsel
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_dmgfull - point the damage range at the whole viewport. The ranged grid
; painters guard [sh_vcols]/[sh_vrows] == 0 themselves, so the wrapped-around
; bounds an empty viewport produces here are never read. Preserves everything.
; -----------------------------------------------------------------------------
sh_dmgfull:
    push ax
    xor ax, ax
    mov [sh_dmgc1], ax
    mov [sh_dmgr1], ax
    mov ax, [sh_vcols]
    dec ax
    mov [sh_dmgc2], ax
    mov ax, [sh_vrows]
    dec ax
    mov [sh_dmgr2], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_dmgdraw - redraw ONLY the cells in [sh_dmgc1..c2] x [sh_dmgr1..r2]
; (window-relative, inclusive, already clamped to the viewport), plus the
; gridline segments and border edges over them. This is the partial-repaint
; core: OSAPI_FONT_RUN owns each cell's 8 glyph rows, so when a cell is
; taller the band below them is filled here - the full repaint's window-wide
; white fill does not run on this path, and the mover owns its stale pixels
; (the old selection frame's edges land in exactly that band).
; -----------------------------------------------------------------------------
sh_dmgdraw:
    push ax
    push bx
    push cx
    push dx
    cmp word [sh_vcols], 0
    je .out
    cmp word [sh_vrows], 0
    je .out
    cmp word [sh_cellh], 8
    jbe .nobands                       ; 8px cells: the runs cover everything
    mov ax, [sh_dmgc1]                 ; the damaged columns' pixel span
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov [sh_blitx1], ax
    mov ax, [sh_dmgc2]
    inc ax
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    dec ax
    mov [sh_blitx2], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov cx, [sh_dmgr1]
.band:
    cmp cx, [sh_dmgr2]
    ja .nobands
    mov ax, cx
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov bx, ax
    add bx, 8                          ; below the glyphs...
    mov dx, ax
    add dx, [sh_cellh]
    dec dx                             ; ...down to the row's last pixel line
    mov ax, [sh_blitx1]
    push cx
    mov cx, [sh_blitx2]
    call OSAPI_GFX_FILL
    pop cx
    inc cx
    jmp .band
.nobands:
    call sh_drawgrid
    call sh_drawlines
    call sh_drawborders
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawbar - the formula bar (stage 2.x: real Excel's own two-box look -
; a fixed-width reference box on the left, a boxed content area on the
; right showing the selected cell's current value/formula, or the live
; edit buffer while typing). Status messages have their own bar now
; (sh_drawstatus) - this one only ever shows the reference and the
; content, matching real Excel's own division of labor between the two.
; -----------------------------------------------------------------------------
; =============================================================================
; The two scroll bars (stage 3.0a+)
;
; The VERTICAL one is os88ui.inc's shared element, used exactly as files.inc
; and fdlg.inc use it. The HORIZONTAL one is sh_hsb_* below - private to this
; app for now, but written to os88ui.inc's own conventions (same seven-word
; block, same OS88UI_SB* part codes, same "geometry not policy" split) so that
; promoting it into the shared file after Sheet 2.0 is a rename rather than a
; redesign. os88ui.inc has no horizontal bar today: its arrow cells are
; derived as y1+10/y2-10 and os88ui_sbtrack deliberately takes DX and not CX
; (SPEC.md 13.10.5.2, "x is never read"), so the axis is structural.
;
; SCROLL EXTENT. `total` is not SH_ROWS/SH_COLS - a bar over 16384 rows would
; have a one-pixel thumb that says nothing. It is the USED extent plus one
; screen, so the thumb is proportional to the sheet a person actually has, and
; it collapses to "no thumb" when everything already fits (os88ui_sbthumb
; answers CF=1 for that case on its own).
; =============================================================================

; -----------------------------------------------------------------------------
; sh_sbsync - refill both blocks from the live geometry and scroll position.
; Called before every draw and every hit-test, for sh_flrect's reason: the
; window moves and resizes, and a painter and a hit-tester reading different
; rects is the one bug this element is designed to make impossible.
; -----------------------------------------------------------------------------
sh_sbsync:
    push ax
    push bx
    push cx
    push dx
    call sh_difbbox                    ; -> [sh_bbcol]/[sh_bbrow], the used
                                        ; bounding box (walks only OCCUPIED
                                        ; cells, not the whole grid)

    ; --- vertical: the strip at the right of the grid area
    mov ax, [sh_ox]
    add ax, [sh_cw]
    sub ax, SH_VSB_W
    mov [sh_vsb + 0], ax               ; x1
    mov ax, [sh_ox]
    add ax, [sh_cw]
    dec ax
    mov [sh_vsb + 4], ax               ; x2
    mov ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov [sh_vsb + 2], ax               ; y1 - the top of the grid proper
    mov ax, [sh_oy]
    add ax, [sh_ch]
    sub ax, SH_SB_H + SH_HSB_H
    dec ax
    mov [sh_vsb + 6], ax               ; y2 - just above the horizontal bar
    mov ax, [sh_bbrow]
    inc ax                             ; the USED extent, not SH_ROWS - a bar
    cmp ax, [sh_vrows]                 ; over 16384 rows has a one-pixel thumb
    jae .vtot                          ; that says nothing. Floored at `fit`,
    mov ax, [sh_vrows]                 ; so an empty sheet has total == fit and
.vtot:                                 ; correctly shows no thumb at all.
    mov [sh_vsb + 8], ax               ; total
    mov ax, [sh_vrows]
    mov [sh_vsb + 10], ax              ; fit
    mov ax, [sh_scrollrow]
    mov dx, [sh_vsb + 8]               ; pos, CLAMPED to total - fit: keyboard
    sub dx, [sh_vsb + 10]              ; navigation and Goto move the origin
    cmp ax, dx                         ; without consulting the bars, and both
    jbe .vpos                          ; thumb routines divide pos * track by
    mov ax, dx                         ; total - unclamped, the quotient can
.vpos:                                 ; overflow 16 bits and the DIV raises
    mov [sh_vsb + 12], ax              ; INT 0 (a crash on real hardware)

    ; --- horizontal: the strip below the grid, left of the vertical bar
    mov ax, [sh_ox]
    add ax, SH_RH_W
    mov [sh_hsb + 0], ax               ; x1
    mov ax, [sh_ox]
    add ax, [sh_cw]
    sub ax, SH_VSB_W
    dec ax
    mov [sh_hsb + 4], ax               ; x2 - stops at the vertical bar
    mov ax, [sh_oy]
    add ax, [sh_ch]
    sub ax, SH_SB_H + SH_HSB_H
    mov [sh_hsb + 2], ax               ; y1
    mov ax, [sh_oy]
    add ax, [sh_ch]
    sub ax, SH_SB_H
    dec ax
    mov [sh_hsb + 6], ax               ; y2
    mov ax, [sh_bbcol]
    inc ax
    cmp ax, [sh_vcols]
    jae .htot
    mov ax, [sh_vcols]
.htot:
    mov [sh_hsb + 8], ax               ; total
    mov ax, [sh_vcols]
    mov [sh_hsb + 10], ax              ; fit
    mov ax, [sh_scrollcol]
    mov dx, [sh_hsb + 8]               ; pos, clamped to total - fit, for the
    sub dx, [sh_hsb + 10]              ; vertical bar's reason above
    cmp ax, dx
    jbe .hpos
    mov ax, dx
.hpos:
    mov [sh_hsb + 12], ax

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; sh_hsb_* - A HORIZONTAL SCROLL BAR, staged for os88ui.inc
;
; os88ui.inc's bar is structurally vertical and says so: its arrow cells are
; y1+10 and y2-10, and os88ui_sbtrack takes DX and refuses CX on purpose
; (SPEC.md 13.10.5.2). This is that element transposed, and NOTHING about it
; is Sheet-specific:
;
;   * the same seven-word block (x1,y1,x2,y2,total,fit,pos), so a promoted
;     version needs no caller to change its .bss;
;   * the same part codes - SH_SB_UP/SBDOWN mean LEFT/RIGHT here, which is
;     what the vertical file would also do rather than inventing two more;
;   * the same split: this answers where the parts are and draws them, and
;     what an arrow DOES to a view stays the caller's (13.10.1);
;   * the same refusal: no thumb when everything fits or the track is too
;     short to hold one.
;
; When it moves into os88ui.inc after Sheet 2.0, the intended shape is one
; axis flag in the block (or a paired entry point) rather than two copies -
; the arithmetic below is deliberately written so that swapping x for y and
; width for height is the whole of the difference.
; =============================================================================

; =============================================================================
; sh_hsb_* - A HORIZONTAL SCROLL BAR, staged for os88ui.inc
;
; os88ui.inc's bar is structurally vertical and says so: its arrow cells are
; y1+10 and y2-10, and os88ui_sbtrack takes DX and refuses CX on purpose
; (SPEC.md 13.10.5.2, "x is never read"). This is that element transposed, and
; nothing about it is Sheet-specific:
;
;   * the same seven-word block (x1,y1,x2,y2,total,fit,pos), so a promoted
;     version needs no caller to change its .bss;
;   * the same part codes - SH_SB_UP/SBDOWN read as LEFT/RIGHT here, which
;     is what a shared two-axis file would do rather than invent two more;
;   * the same split - this answers where the parts are and draws them; what
;     an arrow DOES to a view stays the caller's (13.10.1);
;   * the same refusal - no thumb when everything fits, or when the track is
;     too short to hold one.
;
; When it moves into os88ui.inc after Sheet 2.0, the intended shape is one
; axis flag in the block rather than two copies: the arithmetic below is
; written so that swapping x for y, and width for height, is the whole of the
; difference.
; =============================================================================

; -----------------------------------------------------------------------------
; sh_hsb_load - copy the block's rect into scratch. in: BX = the block.
; Preserves everything. Every drawing routine calls this FIRST and then never
; dereferences BX again, which is what keeps the block pointer and the gfx
; rect from fighting over the same register.
; -----------------------------------------------------------------------------
sh_hsb_load:
    push ax
    mov ax, [bx + 0]
    mov [sh_hsb_x1], ax
    mov ax, [bx + 2]
    mov [sh_hsb_y1], ax
    mov ax, [bx + 4]
    mov [sh_hsb_x2], ax
    mov ax, [bx + 6]
    mov [sh_hsb_y2], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_hsb_thumb - the thumb's geometry (os88ui_sbthumb, transposed)
; in:  BX = the block
; out: CF=1 = there is no thumb; else CF=0 and [sh_hsb_tl]/[sh_hsb_tw] hold
;      its left and width, absolute. Every register preserved.
; -----------------------------------------------------------------------------
sh_hsb_thumb:
    push ax
    push cx
    push dx
    push si
    mov cx, [bx + 4]
    sub cx, [bx + 0]
    sub cx, (SH_SB_CELL + 1) * 2    ; cx = the track's width
    cmp cx, SH_SB_MINH
    jb .none
    mov ax, [bx + 10]                  ; fit
    or ax, ax
    jz .none
    cmp ax, [bx + 8]                   ; fit >= total: everything fits
    jae .none
    xor dx, dx
    mul cx                             ; dx:ax = fit * track
    div word [bx + 8]                  ; / total
    cmp ax, SH_SB_MINH
    jae .wok
    mov ax, SH_SB_MINH
.wok:
    mov si, ax                         ; si = the thumb's width
    mov ax, [bx + 12]                  ; pos
    xor dx, dx
    mul cx                             ; dx:ax = pos * track
    div word [bx + 8]                  ; / total
    add ax, [bx + 0]
    add ax, SH_SB_CELL + 1          ; ax = the thumb's left
    ; Clamp the tail inside the track: pos == total-fit can overshoot by a
    ; pixel once both divisions have truncated.
    mov dx, [bx + 4]
    sub dx, SH_SB_CELL + 1          ; dx = the track's last column
    push ax
    add ax, si
    dec ax                             ; ax = the thumb's right
    cmp ax, dx
    pop ax
    jbe .fits
    mov ax, dx
    sub ax, si
    inc ax
.fits:
    mov [sh_hsb_tl], ax
    mov [sh_hsb_tw], si
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.none:
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sh_hsb_draw - the whole bar. in: BX = the block; gfx lock held.
; Preserves everything; leaves the pen BLACK, as os88ui_sbar does.
; -----------------------------------------------------------------------------
sh_hsb_draw:
    push ax
    push bx
    push cx
    push dx
    call sh_hsb_load

    mov al, CWHITE                     ; the arrow cells are plain white...
    call OSAPI_SET_COLOR
    mov ax, [sh_hsb_x1]
    mov bx, [sh_hsb_y1]
    mov cx, [sh_hsb_x2]
    mov dx, [sh_hsb_y2]
    call OSAPI_GFX_FILL
    mov ax, [sh_hsb_x1]                ; ...and the TRACK between them is the
    add ax, SH_SB_CELL + 1             ; grey dither, which is what the thumb
    mov cx, [sh_hsb_x2]                ; reads as a knob against
    sub cx, SH_SB_CELL + 1
    mov bx, [sh_hsb_y1]
    inc bx
    mov dx, [sh_hsb_y2]
    dec dx
    cmp ax, cx
    jg .notrack
    call OSAPI_GFX_FILL_GRAY
.notrack:

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_hsb_x1]                ; the outline
    mov bx, [sh_hsb_y1]
    mov cx, [sh_hsb_x2]
    mov dx, [sh_hsb_y2]
    call OSAPI_GFX_FRAME

    mov ax, [sh_hsb_x1]                ; the two arrow-cell rules
    add ax, SH_SB_CELL
    mov bx, [sh_hsb_y1]
    mov dx, [sh_hsb_y2]
    call OSAPI_GFX_VLINE
    mov ax, [sh_hsb_x2]
    sub ax, SH_SB_CELL
    mov bx, [sh_hsb_y1]
    mov dx, [sh_hsb_y2]
    call OSAPI_GFX_VLINE

    pop dx
    pop cx
    pop bx
    pop ax
    call sh_hsb_arrows
    call sh_hsb_thdraw
    ret

; -----------------------------------------------------------------------------
; sh_hsb_arrows - the two triangles. os88ui.inc's vertical arrow is 5 rows of
; widths 1..9; this is that rotated, so 5 columns of growing height.
; in: BX = the block (already loaded into scratch by the caller).
; -----------------------------------------------------------------------------
sh_hsb_arrows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [sh_hsb_y1]
    add si, [sh_hsb_y2]
    shr si, 1                          ; si = the cells' vertical centre

    ; The tips point OUTWARD - `<` on the left cell and `>` on the right, not
    ; `>` and `<`. Each arrow starts one pixel in from its OUTER edge, where
    ; the tip belongs, and widens INWARD.
    mov di, [sh_hsb_x1]                ; LEFT arrow: tip at the outer edge...
    add di, 3
    mov cx, 5
    xor bx, bx
.la:
    mov ax, di
    push bx
    push cx
    mov cx, si
    sub cx, bx
    mov dx, si
    add dx, bx
    mov bx, cx
    call OSAPI_GFX_VLINE
    pop cx
    pop bx
    inc di                             ; ...widening inward
    inc bx
    loop .la

    mov di, [sh_hsb_x2]                ; RIGHT arrow: tip at ITS outer edge,
    sub di, 3                          ; widening inward the other way
    mov cx, 5
    xor bx, bx
.ra:
    mov ax, di
    push bx
    push cx
    mov cx, si
    sub cx, bx
    mov dx, si
    add dx, bx
    mov bx, cx
    call OSAPI_GFX_VLINE
    pop cx
    pop bx
    dec di
    inc bx
    loop .ra

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_hsb_thdraw - just the thumb. in: BX = the block; gfx lock held.
; -----------------------------------------------------------------------------
sh_hsb_thdraw:
    push ax
    push bx
    push cx
    push dx
    call sh_hsb_thumb
    jc .out
    ; The same two-part thumb os88ui_sbthdraw draws, transposed: a BLACK
    ; frame with a WHITE interior inside it - not a solid block, which is what
    ; makes it read as a knob against the dithered track rather than as a bar.
    mov ax, [sh_hsb_tl]
    mov cx, ax
    add cx, [sh_hsb_tw]
    dec cx
    mov bx, [sh_hsb_y1]
    add bx, 2
    mov dx, [sh_hsb_y2]
    sub dx, 2
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    inc ax                             ; the interior, INSIDE the border
    dec cx
    inc bx
    dec dx
    cmp ax, cx
    jg .black
    cmp bx, dx
    jg .black
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.black:
    mov al, CBLACK                     ; the header's promise: pen left BLACK
    call OSAPI_SET_COLOR
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_hsb_hit - which part is this point on? (os88ui_sbhit, transposed)
; in:  BX = the block, CX = x, DX = y (both ABSOLUTE)
; out: AL = OS88UI_SB*; AH clobbered, everything else preserved.
; -----------------------------------------------------------------------------
sh_hsb_hit:
    push cx
    push dx
    cmp dx, [bx + 2]
    jb .none
    cmp dx, [bx + 6]
    ja .none
    cmp cx, [bx + 0]
    jb .none
    cmp cx, [bx + 4]
    ja .none
    mov ax, [bx + 0]
    add ax, SH_SB_CELL
    cmp cx, ax
    jbe .up                            ; the LEFT arrow cell
    mov ax, [bx + 4]
    sub ax, SH_SB_CELL
    cmp cx, ax
    jae .down                          ; the RIGHT arrow cell
    call sh_hsb_thumb
    jc .pgdn                           ; no thumb: the track is all page-fwd
    mov ax, [sh_hsb_tl]
    cmp cx, ax
    jb .pgup
    add ax, [sh_hsb_tw]
    cmp cx, ax
    jae .pgdn
    mov al, SH_SB_THUMB
    jmp .out
.up:
    mov al, SH_SB_UP
    jmp .out
.down:
    mov al, SH_SB_DOWN
    jmp .out
.pgup:
    mov al, SH_SB_PGUP
    jmp .out
.pgdn:
    mov al, SH_SB_PGDN
    jmp .out
.none:
    mov al, SH_SB_NONE
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; sh_hsb_grab / sh_hsb_track / sh_hsb_drop - the thumb drag, the same three
; edges os88ui.inc's own uses (13.10.5), with the anchor banked as
; press_x - thumb_left so the thumb does not jump under the hand.
; -----------------------------------------------------------------------------
sh_hsb_grab:
    push ax
    call sh_hsb_thumb
    jc .no
    mov ax, cx
    sub ax, [sh_hsb_tl]
    mov [sh_hsb_dragoff], ax
    mov byte [sh_hsb_dragon], 1
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; in: BX = the block, CX = the pointer's x (ABSOLUTE). y is never read, which
; is 13.10.5.2's rule with the axes swapped.
; out: CF=0 and AX = the pos the view is owed; CF=1 = nothing is owed.
sh_hsb_track:
    cmp byte [sh_hsb_dragon], 0
    je .no
    push cx
    push dx
    push si
    mov ax, cx
    sub ax, [sh_hsb_dragoff]           ; ax = where the thumb's left wants to be
    mov si, [bx + 0]
    add si, SH_SB_CELL + 1          ; si = the track's left
    sub ax, si
    jns .pos
    xor ax, ax                         ; clamped at the near end
.pos:
    mov cx, [bx + 4]
    sub cx, [bx + 0]
    sub cx, (SH_SB_CELL + 1) * 2    ; cx = the track's width
    or cx, cx
    jz .nopop
    xor dx, dx
    mul word [bx + 8]                  ; offset * total
    div cx                             ; / track -> the pos it maps to
    mov cx, [bx + 8]
    sub cx, [bx + 10]                  ; the last legal pos = total - fit
    jbe .zero
    cmp ax, cx
    jbe .done
    mov ax, cx
    jmp .done
.zero:
    xor ax, ax
.done:
    cmp ax, [bx + 12]                  ; 13.10.5.3's quantisation: a move too
    je .nopop                          ; small to change a row owes nothing
    pop si
    pop dx
    pop cx
    clc
    ret
.nopop:
    pop si
    pop dx
    pop cx
.no:
    stc
    ret

sh_hsb_drop:
    mov byte [sh_hsb_dragon], 0
    ret

; -----------------------------------------------------------------------------
; sh_onmouseup - W_ONMOUSEUP: the press was released. Ends a thumb drag and,
; for the vertical bar's rate-0 grab, commits the pos the hand ended on -
; which is what "the view follows only on release" means (13.10.5.4).
; -----------------------------------------------------------------------------
sh_onmouseup:
    push ax
    push bx
    push si
    call os88ui_sbdragging
    jc .noV
    call os88ui_sbdrop                 ; the view already followed during the
    jmp .out                           ; drag (the rate above), so releasing
.noV:                                  ; only has to let go
    cmp byte [sh_hsb_dragon], 0
    je .out
    call sh_hsb_drop
.out:
    mov byte [sh_dragging], 0
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_sbclick - stage 3.0a+: a press landed somewhere. If it was on either
; scroll bar, act on it and answer CF=1 ("mine"); otherwise CF=0 and the grid
; gets it. in: CX = x, DX = y (absolute), SI = the window.
;
; This is the POLICY half that os88ui.inc deliberately leaves to the caller
; (13.10.1): the element says which part was hit, and what a part MEANS to a
; sheet - one row, one screen, or take the thumb - is decided here.
; -----------------------------------------------------------------------------
sh_sbclick:
    push ax
    push bx
    push di
    call sh_sbsync

    mov bx, sh_vsb                     ; --- the vertical bar
    call os88ui_sbhit
    cmp al, SH_SB_NONE
    je .tryh
    xor ah, ah                         ; stash the part in DI: the very next
    mov di, ax                         ; instruction writes the whole of AX,
    mov ax, [sh_scrollrow]             ; so stashing it in AH (as this did)
    mov [sh_sb_oldpos], ax             ; destroyed it and every compare below
    cmp di, SH_SB_UP                   ; fell through to the thumb branch
                                        ; - which is why an arrow click did
                                        ; nothing at all
    je .vup
    cmp di, SH_SB_DOWN
    je .vdn
    cmp di, SH_SB_PGUP
    je .vpgup
    cmp di, SH_SB_PGDN
    je .vpgdn
    mov al, 2                          ; SB_THUMB. A rate of 2 ticks (~110ms)
    call os88ui_sbgrab                 ; rather than 0: the view FOLLOWS the
                                        ; thumb as it moves, throttled, which
                                        ; is 13.10.5.4's purpose - rate 0 means
                                        ; nothing moves until release, and then
                                        ; the final pos has to be recovered
                                        ; from os88ui_sbpos, an INTERNAL that
                                        ; answers in DI and wants the pointer's
                                        ; y that a release has but a drop does
                                        ; not naturally carry
    jmp .mine
.vup:
    mov ax, [sh_scrollrow]
    or ax, ax
    jz .mine
    dec ax
    jmp .vset
.vdn:
    mov ax, [sh_scrollrow]
    inc ax
    jmp .vset
.vpgup:
    mov ax, [sh_scrollrow]
    sub ax, [sh_vrows]
    jns .vset
    xor ax, ax
    jmp .vset
.vpgdn:
    mov ax, [sh_scrollrow]
    add ax, [sh_vrows]
.vset:
    call sh_setscrollrow
    jmp .mine

.tryh:
    mov bx, sh_hsb                     ; --- the horizontal bar
    call sh_hsb_hit
    cmp al, SH_SB_NONE
    je .notmine
    xor ah, ah                         ; same AX-clobber trap as the vertical
    mov di, ax                         ; branch above
    mov ax, [sh_scrollcol]
    mov [sh_sb_oldpos], ax
    cmp di, SH_SB_UP
    je .hlf
    cmp di, SH_SB_DOWN
    je .hrt
    cmp di, SH_SB_PGUP
    je .hpgup
    cmp di, SH_SB_PGDN
    je .hpgdn
    call sh_hsb_grab                   ; SB_THUMB
    jmp .mine
.hlf:
    mov ax, [sh_scrollcol]
    or ax, ax
    jz .mine
    dec ax
    jmp .hset
.hrt:
    mov ax, [sh_scrollcol]
    inc ax
    jmp .hset
.hpgup:
    mov ax, [sh_scrollcol]
    sub ax, [sh_vcols]
    jns .hset
    xor ax, ax
    jmp .hset
.hpgdn:
    mov ax, [sh_scrollcol]
    add ax, [sh_vcols]
.hset:
    call sh_setscrollcol
.mine:
    pop di
    pop bx
    pop ax
    stc
    ret
.notmine:
    pop di
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; sh_setscrollrow / sh_setscrollcol - move the view to AX, clamped to the
; scrollable extent, and paint the move if it actually happened - the
; surviving rows blitted and only the vacated ones lettered (vertical), or
; the grid's own strip repainted (horizontal; OSAPI_GFX_SCROLL is
; vertical-only, SPEC.md 5.5). SI = the window.
; -----------------------------------------------------------------------------
sh_setscrollrow:
    push ax
    push bx
    push cx
    mov cx, [sh_vsb + 8]               ; total
    sub cx, [sh_vsb + 10]              ; ...minus fit = the last legal pos
    jns .rok
    xor cx, cx
.rok:
    cmp ax, cx
    jbe .rset
    mov ax, cx
.rset:
    cmp ax, [sh_scrollrow]
    je .rout                           ; no movement: draw nothing
    mov cx, [sh_scrollrow]             ; the row the view is leaving
    mov [sh_scrollrow], ax
    call sh_scrollrow_blit
    jnc .rout
    call sh_repaint                    ; the blit refused: pay the full price
.rout:
    pop cx
    pop bx
    pop ax
    ret

sh_setscrollcol:
    push ax
    push bx
    push cx
    mov cx, [sh_hsb + 8]
    sub cx, [sh_hsb + 10]
    jns .cok
    xor cx, cx
.cok:
    cmp ax, cx
    jbe .cset
    mov ax, cx
.cset:
    cmp ax, [sh_scrollcol]
    je .cout
    mov [sh_scrollcol], ax
    call sh_scrollcol_part
.cout:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_scrollrow_blit - move the grid by whole rows with OSAPI_GFX_SCROLL
; instead of repainting every visible cell: the surviving rows are one blit,
; and only the |delta| vacated ones are lettered (~7 runs for an arrow click
; instead of ~119).
; in:  CX = the scroll row the view is leaving, SI = the window;
;      [sh_scrollrow] already holds the new one.
; out: CF=0 the view is painted; CF=1 nothing was drawn and the caller owes
;      the full repaint - the blit refused (the clip does not wholly contain
;      the rect, SPEC.md 5.5), the byte-alignment round-up would reach the
;      vertical bar, or the delta leaves no surviving band worth keeping.
; -----------------------------------------------------------------------------
sh_scrollrow_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call sh_geom                       ; fresh geometry: the drag path arrives
                                        ; without sh_onclick's own sh_geom
    cmp word [sh_vcols], 0
    je .no
    cmp word [sh_vrows], 0
    je .no
    mov ax, [sh_scrollrow]
    sub ax, cx                         ; ax = the delta, in rows (signed)
    mov [sh_blitdel], ax
    mov di, ax
    or di, di
    jns .abs
    neg di                             ; di = |delta|
.abs:
    cmp di, [sh_vrows]
    jae .no                            ; nothing survives: repaint instead

    ; The rect. x1 and x2+1 must be multiples of 8 (the blit is byte-column
    ; granular, SPEC.md 5.5): x1 rounds DOWN into the row-header strip,
    ; which is redrawn whole below anyway; x2+1 rounds UP into the dead
    ; space right of the last gridline - refused if that would reach the
    ; vertical bar, whose pixels must not move.
    mov ax, [sh_ox]
    add ax, SH_RH_W
    and ax, 0xFFF8
    mov [sh_blitx1], ax
    mov ax, [sh_vcols]
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W                    ; ax = one past the grid's right edge
    add ax, 7
    and ax, 0xFFF8                     ; ...rounded up to the byte column
    mov dx, [sh_ox]
    add dx, [sh_cw]
    sub dx, SH_VSB_W                   ; dx = the vertical bar's x1
    cmp ax, dx
    ja .no
    dec ax
    mov [sh_blitx2], ax
    mov ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov [sh_blity1], ax
    mov bx, ax
    mov ax, [sh_vrows]
    mov dx, [sh_cellh]
    mul dx
    add ax, bx
    dec ax
    mov [sh_blity2], ax

    mov ax, [sh_blitdel]
    mov dx, [sh_cellh]
    imul dx                            ; the delta is under vrows, so AX is
    mov si, ax                         ; the whole of it: SI = signed dy
    mov ax, [sh_blitx1]
    mov bx, [sh_blity1]
    mov cx, [sh_blitx2]
    mov dx, [sh_blity2]
    call OSAPI_GFX_SCROLL              ; positive dy = content UP = view DOWN
    jc .no                             ; refused: nothing moved, fall back

    xor ax, ax                         ; the vacated rows, and only them:
    cmp word [sh_blitdel], 0           ; scrolled up = new rows on top,
    jl .vac                            ; scrolled down = at the bottom
    mov ax, [sh_vrows]
    sub ax, di
.vac:
    mov [sh_dmgr1], ax
    add ax, di
    dec ax
    mov [sh_dmgr2], ax
    xor ax, ax
    mov [sh_dmgc1], ax
    mov ax, [sh_vcols]
    dec ax
    mov [sh_dmgc2], ax
    call sh_dmgdraw
    call sh_drawsel                    ; the frame's share of the vacated
                                        ; band - its surviving part moved
                                        ; WITH the blit, to exactly where the
                                        ; frame now belongs

    mov al, CWHITE                     ; the row headers: every number
    call OSAPI_SET_COLOR               ; changed places, and their text is
    mov ax, [sh_ox]                    ; transparent, so the strip is erased
    mov bx, [sh_blity1]                ; first
    mov cx, [sh_ox]
    add cx, SH_RH_W - 1
    mov dx, [sh_blity2]
    call OSAPI_GFX_FILL
    call sh_drawrowhdrs

    call sh_sbsync                     ; ...and the thumb moved
    mov bx, sh_vsb
    call os88ui_sbar
    clc
    jmp .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_scrollcol_part - a horizontal scroll has no blit primitive to lean on,
; but it still owes nothing to the menu bar, the formula bar, the status bar
; or the vertical scroll bar: the grid, the column letters and the
; horizontal thumb are the whole of what moved - and no recalc pass, because
; no cell changed. SI = the window.
; -----------------------------------------------------------------------------
sh_scrollcol_part:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call sh_geom                       ; sh_scrollrow_blit's reason
    call sh_dmgfull
    call sh_dmgdraw
    call sh_drawsel
    cmp word [sh_vcols], 0
    je .nohdr
    mov al, CWHITE                     ; the column letters all changed
    call OSAPI_SET_COLOR               ; places; their text is transparent,
    mov ax, [sh_ox]                    ; so the strip is erased first
    add ax, SH_RH_W
    mov bx, [sh_goy]
    add bx, SH_FB_H
    push ax
    mov ax, [sh_vcols]
    mov dx, [sh_cellw]
    mul dx
    mov cx, ax
    pop ax
    add cx, ax
    dec cx
    mov dx, [sh_goy]
    add dx, SH_FB_H + SH_CH_H - 1
    call OSAPI_GFX_FILL
    call sh_drawcolhdrs
.nohdr:
    call sh_sbsync                     ; the horizontal thumb moved
    mov bx, sh_hsb
    call sh_hsb_draw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_flrect - stage 3.0b: point the formula bar's line block at the content
; box's CURRENT screen rect. Called before every draw and every hit-test
; rather than once at startup, because the window moves and resizes and
; os88line reads the same four words for both drawing and clicking - a stale
; rect would put the caret somewhere the box no longer is. These are exactly
; the coordinates sh_drawbar frames the content box with, so the field's own
; frame lands on top of the same pixels.
; -----------------------------------------------------------------------------
sh_flrect:
    push ax
    mov ax, [sh_ox]
    add ax, SH_REF_W
    mov [sh_fline + LN_X1], ax
    mov ax, [sh_goy]
    mov [sh_fline + LN_Y1], ax
    mov ax, [sh_ox]
    add ax, [sh_cw]
    dec ax
    mov [sh_fline + LN_X2], ax
    mov ax, [sh_goy]
    add ax, SH_FB_H - 1
    mov [sh_fline + LN_Y2], ax
    pop ax
    ret

sh_drawbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov al, CBLACK
    call OSAPI_SET_COLOR

    ; --- reference box outline: (ox, goy) to (ox+SH_REF_W-1, goy+SH_FB_H-1) ---
    mov ax, [sh_ox]
    mov bx, ax
    add bx, SH_REF_W - 1
    mov dx, [sh_goy]
    call OSAPI_GFX_HLINE
    add dx, SH_FB_H - 1
    call OSAPI_GFX_HLINE
    mov ax, [sh_ox]
    mov bx, [sh_goy]
    mov dx, [sh_goy]
    add dx, SH_FB_H - 1
    call OSAPI_GFX_VLINE
    mov ax, [sh_ox]
    add ax, SH_REF_W - 1
    call OSAPI_GFX_VLINE               ; also the content box's own left edge

    ; --- content box outline: (ox+SH_REF_W, goy) to (ox+cw-1, goy+SH_FB_H-1) ---
    mov ax, [sh_ox]
    add ax, SH_REF_W
    mov bx, [sh_ox]
    add bx, [sh_cw]
    dec bx
    mov dx, [sh_goy]
    call OSAPI_GFX_HLINE
    add dx, SH_FB_H - 1
    call OSAPI_GFX_HLINE
    mov ax, [sh_ox]
    add ax, [sh_cw]
    dec ax
    mov bx, [sh_goy]
    mov dx, [sh_goy]
    add dx, SH_FB_H - 1
    call OSAPI_GFX_VLINE

    ; --- the reference box's interior, erased: its text is transparent and
    ; this bar repaints on every selection move WITHOUT the window-wide
    ; white fill behind it now (sh_selpaint), so it owns its own pixels ---
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sh_ox]
    inc ax
    mov bx, [sh_goy]
    inc bx
    mov cx, [sh_ox]
    add cx, SH_REF_W - 2
    mov dx, [sh_goy]
    add dx, SH_FB_H - 2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR

    ; --- reference text, into sh_tbuf ---
    ; Formula > Reference switches this between A1 and R1C1, which is the only
    ; place in the app that had an answer to show: the A1<->R1C1 converters
    ; already existed for SYLK's own ;E field (81.7.1), and this is what makes
    ; the setting visible rather than a file-format detail.
    mov di, sh_tbuf
    cmp byte [sh_a1style], 0
    jne .refrc
    mov ax, [sh_selcol]
    call sh_colname
    mov si, sh_colbuf
    call sh_strcpy_to_di
    mov ax, [sh_selrow]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_strcpy_to_di
    jmp .refdone
.refrc:
    mov byte [di], 'R'
    inc di
    mov ax, [sh_selrow]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_strcpy_to_di
    mov byte [di], 'C'
    inc di
    mov ax, [sh_selcol]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_strcpy_to_di
.refdone:
    mov cx, [sh_ox]
    add cx, 4
    mov dx, [sh_goy]
    add dx, 4
    mov si, sh_tbuf
    call OSAPI_FONT_STR_XPARENT

    ; --- while EDITING, the content box is a real text field: os88line owns
    ; the box, the text, the caret and the horizontal scroll, so this path
    ; hands it over entirely rather than drawing a string itself.
    cmp byte [sh_editing], 0
    je .static
    call sh_flrect
    mov si, sh_fline
    call sh_flmarg                     ; the span between the frame and the
    call os88line_draw                 ; field's 8-aligned pen, which the
    jmp .done                          ; field's own one-pass draw never
                                        ; touches

.static:
    ; --- not editing: the cell's current value/formula, as static text, into
    ; sh_tbuf+16 (past the reference text's own small span, so the two never
    ; overlap in the same shared buffer) ---
    mov di, sh_tbuf + 16
    push di                            ; sh_findcell's own DI output would
                                        ; otherwise clobber our cursor
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_findcell
    jnc .empty2
    push es
    mov es, [sh_cellseg]
    test byte [es:di+4], 1
    jnz .isformula
    cmp byte [es:di+SH_C_TYPE], SH_T_TEXT     ; stage 4.5: a label shows its
    jne .plainval2                     ; own text here, unprefixed - the '='
    mov ax, [es:di+SH_C_FOFF]          ; below is what makes a formula look
    pop es                             ; like one, and a label is not one
    pop di
    mov si, ax
    push es
    mov es, [sh_txtseg]
    jmp .copyfm
.isformula:
    mov ax, [es:di+SH_C_FOFF]                 ; formula_off
    pop es
    pop di                             ; DI = content cursor, restored
    mov byte [di], '='
    inc di
    mov si, ax
    push es
    mov es, [sh_txtseg]
.copyfm:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyfm
    pop es
    jmp .draw
.plainval2:
    call sh_cellnum                    ; sh_numbuf already holds the decimal
    pop es                             ; text; sh_itoa would overwrite it with
    pop di                             ; the low word's worth
    mov si, sh_numbuf
    call sh_strcpy_to_di
    jmp .draw
.empty2:
    pop di                             ; DI = content cursor, restored
    mov byte [di], 0
.draw:
    mov al, CWHITE                     ; the content box's interior, erased:
    call OSAPI_SET_COLOR               ; the text below is transparent and of
    mov ax, [sh_ox]                    ; varying length (the ref box's reason
    add ax, SH_REF_W + 1               ; above)
    mov bx, [sh_goy]
    inc bx
    mov cx, [sh_ox]
    add cx, [sh_cw]
    sub cx, 2
    mov dx, [sh_goy]
    add dx, SH_FB_H - 2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [sh_ox]
    add cx, SH_REF_W + 4
    mov dx, [sh_goy]
    add dx, 3                          ; the same row os88line's own run uses
                                        ; (LN_INSET), so the field covers this
                                        ; text exactly when an edit begins
    mov si, sh_tbuf + 16
    call OSAPI_FONT_STR_XPARENT
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawstatus - the status bar: a single divider line above a strip at
; the very bottom of the content area, showing [sh_msg] if a command just
; set one, else the idle "Ready" real Excel's own status bar shows.
; -----------------------------------------------------------------------------
sh_drawstatus:
    push ax
    push bx
    push cx
    push dx
    push si

    mov al, CWHITE                     ; the strip's interior, erased: the
    call OSAPI_SET_COLOR               ; message is transparent text of
    mov ax, [sh_ox]                    ; varying length, and this bar repaints
    mov bx, [sh_oy]                    ; on selection moves without the
    add bx, [sh_ch]                    ; window-wide white fill behind it
    sub bx, SH_SB_H                    ; (sh_selpaint)
    inc bx
    mov cx, [sh_ox]
    add cx, [sh_cw]
    dec cx
    mov dx, [sh_oy]
    add dx, [sh_ch]
    dec dx
    call OSAPI_GFX_FILL

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_ox]
    mov bx, ax
    add bx, [sh_cw]
    dec bx
    mov dx, [sh_oy]
    add dx, [sh_ch]
    sub dx, SH_SB_H
    call OSAPI_GFX_HLINE

    mov si, [sh_msg]
    or si, si
    jnz .havemsg
    mov si, sh_s_ready
.havemsg:
    mov cx, [sh_ox]
    add cx, 4
    mov dx, [sh_oy]
    add dx, [sh_ch]
    sub dx, SH_SB_H
    add dx, 4
    call OSAPI_FONT_STR_XPARENT

    ; The right-hand indicator block, which real Excel uses for NUM/CAPS/SCRL
    ; and for the word CALCULATE when Manual mode has left the sheet stale.
    ; CALCULATE takes precedence, because it is the one that means something
    ; is WRONG on screen rather than something is set on the keyboard.
    mov si, sh_s_num
    cmp byte [sh_calcmanual], 0
    je .indi
    mov si, sh_s_calcind
.indi:
    mov cx, [sh_ox]
    add cx, [sh_cw]
    sub cx, 88
    mov dx, [sh_oy]
    add dx, [sh_ch]
    sub dx, SH_SB_H
    add dx, 4
    call OSAPI_FONT_STR_XPARENT

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawcolhdrs - the column letters, centred in each visible column's band
; -----------------------------------------------------------------------------
sh_drawcolhdrs:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov word [sh_wcol], 0
.col:
    mov bx, [sh_wcol]
    cmp bx, [sh_vcols]
    jae .out
    mov ax, bx
    add ax, [sh_scrollcol]
    call sh_colname
    mov ax, bx
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov cx, ax
    mov si, sh_colbuf
    call OSAPI_FONT_WIDTH
    mov dx, [sh_cellw]
    sub dx, ax
    shr dx, 1
    add cx, dx
    mov dx, [sh_goy]
    add dx, SH_FB_H
    mov si, sh_colbuf
    call OSAPI_FONT_STR_XPARENT
    mov bx, [sh_wcol]
    inc bx
    mov [sh_wcol], bx
    jmp .col
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawrowhdrs - the row numbers, right-aligned in SH_RH_W
; -----------------------------------------------------------------------------
sh_drawrowhdrs:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov word [sh_wrow], 0
.row:
    mov bx, [sh_wrow]
    cmp bx, [sh_vrows]
    jae .out
    mov ax, bx
    add ax, [sh_scrollrow]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call OSAPI_FONT_WIDTH
    mov cx, SH_RH_W - 4
    sub cx, ax
    add cx, [sh_ox]
    mov ax, bx
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov dx, ax
    mov si, sh_numbuf
    call OSAPI_FONT_STR_XPARENT
    mov bx, [sh_wrow]
    inc bx
    mov [sh_wrow], bx
    jmp .row
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawgrid - every cell in the damage range (sh_dmgc1..sh_dmgr2, window-
; relative - sh_dmgfull for the whole viewport) as one fixed-width
; OSAPI_FONT_RUN, number-formatted and justified per its own SH_FMT_* bits
; (stage 1.6), all spaces for empty. Sparse lookup: no bitmap.
; -----------------------------------------------------------------------------
sh_drawgrid:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp word [sh_vcols], 0
    je .out
    cmp word [sh_vrows], 0
    je .out
    mov ax, [sh_dmgr1]
    mov [sh_wrow], ax
.row:
    mov ax, [sh_wrow]
    cmp ax, [sh_dmgr2]
    ja .out
    mov ax, [sh_dmgc1]
    mov [sh_wcol], ax
.col:
    mov ax, [sh_wcol]
    cmp ax, [sh_dmgc2]
    ja .rownext
    mov ax, [sh_wcol]
    add ax, [sh_scrollcol]
    mov bx, [sh_wrow]
    add bx, [sh_scrollrow]
    call sh_getcell2
    jc .have
    mov si, sh_blank
    jmp .got
.have:
    cmp byte [sh_showformulas], 0      ; stage 2.x Options > Formulas: On -
    je .valpath                        ; show the formula TEXT, not its
                                        ; value, matching real Excel's
                                        ; Display dialog's "Formulas" box.
                                        ; AX/BX are still this cell's own
                                        ; col/row (sh_getcell2 preserves
                                        ; both), so re-finding it costs
                                        ; nothing extra to set up.
    call sh_findcell
    jnc .valpath                       ; can't happen (getcell2 said
                                        ; occupied) - stay safe regardless
    push es
    mov es, [sh_cellseg]
    test byte [es:di+4], 1             ; HASFORMULA
    jz .noformula3
    mov ax, [es:di+SH_C_FOFF]                  ; formula_off
    pop es
    mov byte [sh_tbuf], '='
    mov di, sh_tbuf + 1
    mov si, ax
    push es
    mov es, [sh_txtseg]
.fcopy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .fcopy
    pop es
    mov cx, di
    sub cx, sh_tbuf
    dec cx                             ; cx = chars written, excluding NUL
    cmp cx, [sh_cellch]
    jbe .fpad
    mov bx, [sh_cellch]
    mov byte [sh_tbuf + bx], 0         ; longer than a cell: truncate
    jmp .fshow
.fpad:
    mov ax, [sh_cellch]
    sub ax, cx
    jz .fshow
    mov cx, ax
.fploop:
    mov byte [di], ' '
    inc di
    loop .fploop
    mov byte [di], 0
.fshow:
    mov si, sh_tbuf
    jmp .got
.noformula3:
    pop es
.valpath:
    cmp byte [sh_curtype], SH_T_ERR    ; an error draws its NAME - the number
    je .errpath                        ; underneath it is meaningless
    cmp byte [sh_curtype], SH_T_TEXT   ; stage 4.5: a label draws its own
    je .textpath                       ; characters, not its value
    mov ax, dx
    mov bl, [sh_curfmt]
    call sh_numfmt
    call sh_justify
    mov si, sh_tbuf
    jmp .got
.errpath:
    call sh_errname                    ; -> sh_numbuf
    mov bl, [sh_curfmt]
    call sh_justify                    ; right, like the number it replaces
    mov si, sh_tbuf
    jmp .got
.textpath:
    call sh_text_to_numbuf             ; the arena string, clipped to the cell
    mov bl, [sh_curfmt]
    call sh_justify_t                  ; General means LEFT for a label
    mov si, sh_tbuf
.got:
    mov ax, [sh_wcol]
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov cx, ax
    mov ax, [sh_wrow]
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov dx, ax
    ; stage 2.x: a Shaded cell (Format > Border..., real Excel's own Shade
    ; checkbox) needs the grey dither drawn FIRST and the text drawn
    ; TRANSPARENT over it - OSAPI_FONT_RUN's opaque erase-then-letter would
    ; otherwise wipe the dither right back out on every single repaint
    push cx
    push dx
    mov ax, [sh_wcol]
    add ax, [sh_scrollcol]
    mov bx, [sh_wrow]
    add bx, [sh_scrollrow]
    call sh_bt_get                     ; al = this cell's border byte
    pop dx
    pop cx
    test al, SH_BORD_SHADE
    jz .noshade
    push cx
    push dx
    mov ax, cx
    mov bx, dx
    add cx, [sh_cellw]
    dec cx
    add dx, [sh_cellh]
    dec dx
    call OSAPI_GFX_FILL_GRAY
    pop dx
    pop cx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call OSAPI_FONT_STR_XPARENT
    jmp .aftertext
.noshade:
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.aftertext:
    test byte [sh_curfmt], SH_FMT_BOLD
    jz .nobold
    push cx
    push dx
    inc cx
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_STR_XPARENT                ; a 1px-right overprint - the same
                                        ; double-strike trick texpad uses
                                        ; for bold on this same 8x8 font
    pop dx
    pop cx
.nobold:
    test byte [sh_curfmt], SH_FMT_UNDER
    jz .nounder
    call sh_drawunderline
.nounder:
    mov ax, [sh_wcol]
    inc ax
    mov [sh_wcol], ax
    jmp .col
.rownext:
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    jmp .row
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawlines - the cell-boundary lines over the damage range (sh_dmgc1..
; sh_dmgr2; sh_dmgfull for the whole viewport): c2-c1+2 vertical and r2-r1+2
; horizontal, each a degenerate (1px) OSAPI_GFX_FILL rectangle spanning just
; the damaged cells. Drawn AFTER sh_drawgrid: OSAPI_FONT_RUN's opaque erase
; is exactly one cell wide and would otherwise paint back over a line drawn
; first.
; -----------------------------------------------------------------------------
sh_drawlines:
    push ax
    push bx
    push cx
    push dx
    cmp byte [sh_gridlines], 0         ; stage 2.x Options > Gridlines: Off
    je .out                            ; skips this whole pass, same as real
                                        ; Excel's Display dialog
    cmp word [sh_vcols], 0
    je .out
    cmp word [sh_vrows], 0
    je .out
    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov ax, [sh_dmgr1]                 ; the damaged rows' pixel span...
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov [sh_ly1], ax
    mov ax, [sh_dmgr2]
    inc ax
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    dec ax
    mov [sh_ly2], ax

    mov ax, [sh_dmgc1]                 ; ...and the damaged columns'
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov [sh_lx1], ax
    mov ax, [sh_dmgc2]
    inc ax
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    dec ax
    mov [sh_lx2], ax

    mov ax, [sh_dmgc1]
    mov [sh_wcol], ax
.vline:
    mov ax, [sh_wcol]
    mov dx, [sh_dmgc2]
    inc dx
    cmp ax, dx
    ja .vdone
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov cx, ax
    mov bx, [sh_ly1]
    mov dx, [sh_ly2]
    call OSAPI_GFX_FILL
    mov ax, [sh_wcol]
    inc ax
    mov [sh_wcol], ax
    jmp .vline
.vdone:
    mov ax, [sh_dmgr1]
    mov [sh_wrow], ax
.hline:
    mov ax, [sh_wrow]
    mov dx, [sh_dmgr2]
    inc dx
    cmp ax, dx
    ja .hdone
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov bx, ax
    mov dx, ax
    mov ax, [sh_lx1]
    mov cx, [sh_lx2]
    call OSAPI_GFX_FILL
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    jmp .hline
.hdone:
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawborders - the four directional edges (Left/Right/Top/Bottom) of
; every bordered cell (sh_bordseg) on the current sheet, within the visible
; scroll window AND the damage range (sh_dmgc1..sh_dmgr2; sh_dmgfull for the
; whole viewport). Shade is drawn from INSIDE sh_drawgrid instead, since it
; has to happen BEFORE that cell's own opaque text run, not after (see the
; comment there) - this routine only ever draws the four edge lines.
; Sparse walk of sh_bordseg (typically tiny - almost no cell has a border)
; rather than a per-cell probe, the same style sh_docmd_sortcol/
; sh_rowcol_op already walk-and-filter the main cell array with. Drawn
; AFTER sh_drawgrid for the same reason sh_drawlines already is:
; OSAPI_FONT_RUN's opaque erase is exactly one cell wide and would
; otherwise paint back over an edge drawn first.
; -----------------------------------------------------------------------------
sh_drawborders:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov word [sh_bti], 0               ; the scan index lives in bss, not
                                        ; CX - OSAPI_GFX_FILL below takes CX
                                        ; as one of its own four params, so
                                        ; a register loop counter would get
                                        ; clobbered by the very first edge
                                        ; it draws (caught in review)
.scan:
    mov cx, [sh_bti]
    cmp cx, [sh_nbord]
    jae .done
    mov ax, cx
    mov bx, 5
    mul bx
    mov si, ax
    mov es, [sh_bordseg]
    mov ax, [es:si]                   ; packed row/sheet
    call sh_unpackrow                 ; ax=row, bx=sheet
    cmp bx, [sh_cursheet]
    jne .next
    mov dx, [es:si+2]                 ; col
    mov bx, dx
    sub bx, [sh_scrollcol]
    js .next
    cmp bx, [sh_vcols]
    jae .next
    cmp bx, [sh_dmgc1]                ; ...and inside the damage range, so a
    jb .next                          ; partial redraw (sh_dmgdraw) does not
    cmp bx, [sh_dmgc2]                ; re-edge cells it never repainted
    ja .next
    mov [sh_wcol], bx
    mov bx, ax
    sub bx, [sh_scrollrow]
    js .next
    cmp bx, [sh_vrows]
    jae .next
    cmp bx, [sh_dmgr1]
    jb .next
    cmp bx, [sh_dmgr2]
    ja .next
    mov [sh_wrow], bx
    mov al, [es:si+4]
    mov [sh_bdrawflags], al
    mov ax, [sh_wcol]
    mov bx, [sh_cellw]
    mul bx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov [sh_bx1], ax
    add ax, [sh_cellw]
    dec ax
    mov [sh_bx2], ax
    mov ax, [sh_wrow]
    mov bx, [sh_cellh]
    mul bx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov [sh_by1], ax
    add ax, [sh_cellh]
    dec ax
    mov [sh_by2], ax
    test byte [sh_bdrawflags], SH_BORD_LEFT
    jz .noleft
    mov ax, [sh_bx1]
    mov bx, [sh_by1]
    mov cx, [sh_bx1]
    mov dx, [sh_by2]
    call OSAPI_GFX_FILL
.noleft:
    test byte [sh_bdrawflags], SH_BORD_RIGHT
    jz .noright
    mov ax, [sh_bx2]
    mov bx, [sh_by1]
    mov cx, [sh_bx2]
    mov dx, [sh_by2]
    call OSAPI_GFX_FILL
.noright:
    test byte [sh_bdrawflags], SH_BORD_TOP
    jz .notop
    mov ax, [sh_bx1]
    mov bx, [sh_by1]
    mov cx, [sh_bx2]
    mov dx, [sh_by1]
    call OSAPI_GFX_FILL
.notop:
    test byte [sh_bdrawflags], SH_BORD_BOTTOM
    jz .nobottom
    mov ax, [sh_bx1]
    mov bx, [sh_by2]
    mov cx, [sh_bx2]
    mov dx, [sh_by2]
    call OSAPI_GFX_FILL
.nobottom:
.next:
    mov ax, [sh_bti]
    inc ax
    mov [sh_bti], ax
    jmp .scan
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawsel - a black frame around the selected cell, if it is on screen
; -----------------------------------------------------------------------------
sh_drawsel:
    push ax
    push bx
    push cx
    push dx
    call sh_selrect                    ; stage 3.0a: -> sh_selc1..sh_selr2,
                                        ; already ordered

    ; --- clip the block's own cell rect to the visible viewport. Each edge is
    ; clamped rather than the whole block rejected, so a selection that runs
    ; off the screen still draws the part that shows (Excel's own behaviour,
    ; and what a drag past the edge needs).
    mov ax, [sh_selc2]                 ; wholly left of the viewport?
    cmp ax, [sh_scrollcol]
    jb .out
    mov ax, [sh_selr2]                 ; wholly above it?
    cmp ax, [sh_scrollrow]
    jb .out

    mov ax, [sh_selc1]                 ; left edge, clamped to the origin
    cmp ax, [sh_scrollcol]
    jae .c1ok
    mov ax, [sh_scrollcol]
.c1ok:
    sub ax, [sh_scrollcol]
    cmp ax, [sh_vcols]
    jae .out                           ; starts past the right edge
    mov [sh_wcol], ax

    mov ax, [sh_selr1]                 ; top edge, clamped
    cmp ax, [sh_scrollrow]
    jae .r1ok
    mov ax, [sh_scrollrow]
.r1ok:
    sub ax, [sh_scrollrow]
    cmp ax, [sh_vrows]
    jae .out
    mov [sh_wrow], ax

    mov ax, [sh_selc2]                 ; right edge, clamped to the last
    sub ax, [sh_scrollcol]             ; visible column
    cmp ax, [sh_vcols]
    jb .c2ok
    mov ax, [sh_vcols]
    dec ax
.c2ok:
    mov [sh_selvc2], ax

    mov ax, [sh_selr2]                 ; bottom edge, clamped
    sub ax, [sh_scrollrow]
    cmp ax, [sh_vrows]
    jb .r2ok
    mov ax, [sh_vrows]
    dec ax
.r2ok:
    mov [sh_selvr2], ax

    ; --- cell coords -> pixels
    mov ax, [sh_wcol]
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    mov [sh_selx1], ax

    mov ax, [sh_selvc2]
    inc ax                             ; one past the last column...
    mov dx, [sh_cellw]
    mul dx
    add ax, [sh_ox]
    add ax, SH_RH_W
    dec ax                             ; ...minus a pixel = its right edge
    mov [sh_selx2], ax

    mov ax, [sh_wrow]
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    mov [sh_sely1], ax

    mov ax, [sh_selvr2]
    inc ax
    mov dx, [sh_cellh]
    mul dx
    add ax, [sh_goy]
    add ax, SH_FB_H + SH_CH_H
    dec ax
    mov [sh_sely2], ax

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_selx1]
    mov bx, [sh_sely1]
    mov cx, [sh_selx2]
    mov dx, [sh_sely2]
    call OSAPI_GFX_FRAME
    call sh_selsingle                  ; a single cell keeps the plain 1px
    jc .out                            ; frame it has always had; a real
                                        ; RANGE gets a second, inset frame so
                                        ; it reads as a block rather than as
                                        ; one very large cell (this OS has no
                                        ; wide-pen primitive, and XOR fill
                                        ; over the text would be worse - see
                                        ; os88ui_btn's own note on XOR)
    mov ax, [sh_selx1]
    inc ax
    mov bx, [sh_sely1]
    inc bx
    mov cx, [sh_selx2]
    dec cx
    mov dx, [sh_sely2]
    dec dx
    cmp ax, cx                         ; degenerate after the inset?
    jae .out
    cmp bx, dx
    jae .out
    call OSAPI_GFX_FRAME
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Sheet's own in-window menu bar (see the SH_MBAR_H section comment for why
; this exists instead of OS88_MENUSET): File > New / Open... / Save / Save
; As..., Edit > Cut/Copy/Paste/..., Format > dialogs, Data > Sort Column,
; Sheets > switch, Options > Display toggles, Macro > Run, Help > About.
; =============================================================================

; -----------------------------------------------------------------------------
; sh_mtab_calc - measure each menu title's pixel width once (sh_mw), so
; sh_mboxof never has to call OSAPI_FONT_WIDTH itself on every click/paint.
; Called once from sh_entry - the titles are fixed strings, so this never
; needs to run again.
; -----------------------------------------------------------------------------
sh_mtab_calc:
    push ax
    push bx
    push cx
    push si
    push di
    xor cx, cx
.loop:
    cmp cx, SH_MENU_N
    jae .done
    mov ax, cx
    mov bx, 6
    mul bx
    mov bx, ax
    mov si, [sh_mtab + bx]
    call OSAPI_FONT_WIDTH
    mov di, cx
    shl di, 1
    mov [sh_mw + di], ax
    inc cx
    jmp .loop
.done:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mboxof - AL = menu index -> sh_mbx1/sh_mbx2 (screen-absolute box
; bounds, using the raw [sh_ox]/[sh_oy], not the grid-shifted [sh_goy]).
; preserves everything
; -----------------------------------------------------------------------------
sh_mboxof:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cl, al
    xor ch, ch
    mov dx, [sh_ox]
    xor bx, bx
.loop:
    cmp bx, cx
    jae .found
    mov di, bx
    shl di, 1
    mov ax, [sh_mw + di]
    add ax, SH_MPAD*2
    add dx, ax
    inc bx
    jmp .loop
.found:
    mov [sh_mbx1], dx
    mov di, bx
    shl di, 1
    mov ax, [sh_mw + di]
    add ax, SH_MPAD*2
    add dx, ax
    dec dx
    mov [sh_mbx2], dx
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mbar_draw - the whole menu bar strip: white ground, black rule under
; it, every title (inverted if it is [sh_mopen]). Monochrome-safe black/
; white/invert, matching every other Sheet dialog in this app, rather than
; real Excel 2.1's cyan bar (VM_screenshots/excel_main.png) - this OS
; supports 1bpp Hercules/CGA-mono adapters Sheet's own chrome has stayed
; safe for since stage 1.8, and introducing a new color here would be the
; first thing in this app to depend on one existing at all.
; -----------------------------------------------------------------------------
sh_mbar_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sh_ox]
    mov bx, [sh_oy]
    mov cx, [sh_ox]
    add cx, [sh_cw]
    dec cx
    mov dx, [sh_oy]
    add dx, SH_MBAR_H - 1
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_ox]
    mov bx, [sh_ox]
    add bx, [sh_cw]
    dec bx
    mov dx, [sh_oy]
    add dx, SH_MBAR_H - 1
    call OSAPI_GFX_HLINE
    mov word [sh_mli], 0
    mov word [sh_mto], 0
.loop:
    mov ax, [sh_mli]
    cmp ax, SH_MENU_N
    jae .done
    mov al, [sh_mli]
    call sh_mboxof
    mov al, [sh_mli]
    cmp al, [sh_mopen]
    jne .normal
    mov ax, [sh_mbx1]
    mov bx, [sh_oy]
    mov cx, [sh_mbx2]
    mov dx, [sh_oy]
    add dx, SH_MBAR_H - 1
    call OSAPI_GFX_FILL
    mov al, CWHITE
    call OSAPI_SET_COLOR
    jmp .drawtitle
.normal:
    mov al, CBLACK
    call OSAPI_SET_COLOR
.drawtitle:
    mov bx, [sh_mto]
    mov si, [sh_mtab + bx]
    mov cx, [sh_mbx1]
    add cx, SH_MPAD
    mov dx, [sh_oy]
    add dx, 3
    call OSAPI_FONT_STR_XPARENT
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_mto]
    add ax, 6
    mov [sh_mto], ax
    mov ax, [sh_mli]
    inc ax
    mov [sh_mli], ax
    jmp .loop
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mbar_hit - CX,DX (screen-absolute) -> AL = menu index or SH_M_NONE
; -----------------------------------------------------------------------------
sh_mbar_hit:
    push bx
    push cx
    push dx
    mov ax, [sh_oy]
    cmp dx, ax
    jb .no
    add ax, SH_MBAR_H - 1
    cmp dx, ax
    ja .no
    mov word [sh_mli], 0
.loop:
    mov ax, [sh_mli]
    cmp ax, SH_MENU_N
    jae .no
    mov al, [sh_mli]
    call sh_mboxof
    cmp cx, [sh_mbx1]
    jb .next
    cmp cx, [sh_mbx2]
    ja .next
    mov ax, [sh_mli]
    jmp .out
.next:
    mov ax, [sh_mli]
    inc ax
    mov [sh_mli], ax
    jmp .loop
.no:
    mov ax, SH_M_NONE
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sh_mdrop_geo - compute the open menu's ([sh_mopen]) dropdown rect into
; sh_mrx1/mry1/mrx2/mry2, and stash its items ptr/count into sh_mip/
; sh_mcnt for sh_mdrop_draw and sh_mitem_hit to share. Width is the widest
; item label (skipping a leading MENU_DIS byte when measuring); height is
; item_count*SH_MI_H plus a little top/bottom padding. No sliding-under-
; the-screen-edge case (unlike word.asm's wd_mgeo) - Sheet's own dropdowns
; are short enough that this has never yet needed one.
; -----------------------------------------------------------------------------
sh_mdrop_geo:
    push ax
    push bx
    push cx
    push si
    mov al, [sh_mopen]
    call sh_mboxof
    mov ax, [sh_mbx1]
    mov [sh_mrx1], ax
    mov ax, [sh_oy]
    add ax, SH_MBAR_H
    mov [sh_mry1], ax

    mov bl, [sh_mopen]
    xor bh, bh
    mov ax, bx
    mov cx, 6
    mul cx
    mov bx, ax
    mov si, [sh_mtab + bx + 2]
    mov [sh_mip], si
    mov ax, [sh_mtab + bx + 4]
    mov [sh_mcnt], ax

    mov word [sh_mmaxw], 0
    mov word [sh_mli], 0
.wloop:
    mov ax, [sh_mli]
    cmp ax, [sh_mcnt]
    jae .wdone
    mov bx, [sh_mli]
    shl bx, 1
    mov si, [sh_mip]
    add si, bx
    mov si, [si]
    mov al, [si]
    cmp al, MENU_DIS
    jne .measure
    inc si
.measure:
    call OSAPI_FONT_WIDTH
    cmp ax, [sh_mmaxw]
    jbe .wnext
    mov [sh_mmaxw], ax
.wnext:
    mov ax, [sh_mli]
    inc ax
    mov [sh_mli], ax
    jmp .wloop
.wdone:
    mov ax, [sh_mmaxw]
    add ax, SH_MPAD*2 + SH_MCHKW
    mov bx, [sh_mrx1]
    add bx, ax
    dec bx
    mov [sh_mrx2], bx

    mov ax, [sh_mcnt]
    mov cx, SH_MI_H
    mul cx
    add ax, 4
    add ax, [sh_mry1]
    dec ax
    mov [sh_mry2], ax

    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mdrop_draw - paint the open dropdown from sh_mrx1/y1/x2/y2 + sh_mip/
; sh_mcnt (sh_mdrop_geo must already have run). White panel, black frame,
; one row per item at SH_MI_H apart: disabled items (MENU_DIS) drawn under
; OSAPI_GFX_PEN's disabled (grey) pen; the hot item ([sh_mhi]) drawn
; inverted. Redraws the WHOLE panel on every highlight change rather than
; word.asm's per-row XOR - Sheet's dropdowns are short lists, so this is
; cheap enough not to need that finer granularity.
; -----------------------------------------------------------------------------
sh_mdrop_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sh_mrx1]
    mov bx, [sh_mry1]
    mov cx, [sh_mrx2]
    mov dx, [sh_mry2]
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_mrx1]
    mov bx, [sh_mry1]
    mov cx, [sh_mrx2]
    mov dx, [sh_mry2]
    call OSAPI_GFX_FRAME
    mov word [sh_mli], 0
.loop:
    mov ax, [sh_mli]
    cmp ax, [sh_mcnt]
    jae .done
    mov cx, SH_MI_H
    mul cx
    add ax, [sh_mry1]
    add ax, 2
    mov [sh_mry_row], ax
    mov bx, [sh_mip]
    mov cx, [sh_mli]
    shl cx, 1
    add bx, cx
    mov si, [bx]
    mov byte [sh_mchk], 0
    mov al, [si]
    cmp al, SH_MENU_CHK               ; stage 3.0c: the same relabel-by-
    jne .notchk                       ; repointing trick MENU_DIS documents,
    inc si                            ; for a mark rather than for grey
    mov byte [sh_mchk], 1
    mov al, [si]
.notchk:
    cmp al, MENU_DIS
    jne .live
    inc si
    stc
    call OSAPI_GFX_PEN
    jmp .drawtext
.live:
    mov ax, [sh_mli]
    cmp al, [sh_mhi]
    jne .plain
    mov ax, [sh_mrx1]
    inc ax
    mov bx, [sh_mry_row]
    mov cx, [sh_mrx2]
    dec cx
    mov dx, [sh_mry_row]
    add dx, SH_MI_H - 1
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN
    mov al, CWHITE
    call OSAPI_SET_COLOR
    jmp .drawtext
.plain:
    clc
    call OSAPI_GFX_PEN
.drawtext:
    cmp byte [sh_mchk], 0
    je .nochk
    push si                           ; the check: two strokes, because the
    push ax                           ; kernel font stops at 0x7E and has no
    push bx                           ; glyph for one. The pen is already the
    push cx                           ; right colour - set by the highlight
    push dx                           ; branch above, so the mark inverts with
    mov ax, [sh_mrx1]                 ; the row exactly as the text does
    add ax, 4
    mov bx, [sh_mry_row]
    add bx, 5
    mov cx, ax
    add cx, 2
    mov dx, bx
    add dx, 2
    xor si, si
    call OSAPI_GFX_LINE
    mov ax, [sh_mrx1]
    add ax, 7
    mov bx, [sh_mry_row]
    add bx, 7
    mov cx, ax
    add cx, 3
    mov dx, bx
    sub dx, 5
    xor si, si
    call OSAPI_GFX_LINE
    pop dx
    pop cx
    pop bx
    pop ax
    pop si
.nochk:
    mov cx, [sh_mrx1]
    add cx, SH_MPAD + SH_MCHKW
    mov dx, [sh_mry_row]
    call OSAPI_FONT_STR_XPARENT
    mov ax, [sh_mli]
    inc ax
    mov [sh_mli], ax
    jmp .loop
.done:
    clc
    call OSAPI_GFX_PEN                 ; leave the pen live (its own "put it
                                        ; back" rule) for whatever draws next
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mitem_hit - CX,DX (screen-absolute) -> AL = item index, or SH_M_NONE
; if outside the panel, on a separator gap, or on a disabled (MENU_DIS)
; item - a disabled row cannot become the hot item at all, which is what
; lets sh_mdrop_draw assume a highlighted row is always live.
; -----------------------------------------------------------------------------
sh_mitem_hit:
    push bx
    push si
    cmp cx, [sh_mrx1]
    jb .no
    cmp cx, [sh_mrx2]
    ja .no
    cmp dx, [sh_mry1]
    jb .no
    cmp dx, [sh_mry2]
    ja .no
    mov ax, dx
    sub ax, [sh_mry1]
    sub ax, 2
    js .no
    push dx
    xor dx, dx
    mov bx, SH_MI_H
    div bx
    pop dx
    cmp ax, [sh_mcnt]
    jae .no
    mov bx, [sh_mip]
    push cx
    mov cx, ax
    shl cx, 1
    add bx, cx
    pop cx
    mov si, [bx]
    cmp byte [si], MENU_DIS
    je .no
    jmp .out
.no:
    mov ax, SH_M_NONE
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; sh_mclose - close the open dropdown and repaint what it covered. Always a
; full sh_repaint (menu bar included, since sh_drawall draws it first) -
; Sheet's own grid redraw is cheap, unlike word.asm's wd_mrepair, which
; repaints piecewise specifically to avoid a full-document reflow.
; -----------------------------------------------------------------------------
sh_mclose:
    push ax
    push si
    mov byte [sh_mopen], SH_M_NONE
    mov byte [sh_mhi], SH_M_NONE
    mov si, [sh_ownwin]
    call sh_repaint
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mtrack - the press-drag-release gesture (word.asm's wd_mtrack pattern:
; a tight OSAPI_MOUSE poll with an unlock/yield/relock between reads, never
; W_ONDRAG - see the SH_MBAR_H section comment for why). in: AL = menu
; index to open, SI = window ptr (this callback's own, untouched SI - see
; sh_onclick); called with the gfx lock already held, exactly the state
; the unlock/relock pair expects.
; -----------------------------------------------------------------------------
sh_mtrack:
    push ax
    push bx
    push si
    mov [sh_mopen], al
    mov byte [sh_mhi], SH_M_NONE
    call sh_mdrop_geo
    call sh_mbar_draw
    call sh_mdrop_draw
.loop:
    call OSAPI_GFX_UNLOCK
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .spin
    call OSAPI_GFX_LOCK
    call OSAPI_MOUSE                   ; cx=x, dx=y, al=buttons
    test al, 1
    jz .release
    call sh_mitem_hit
    cmp al, [sh_mhi]
    je .loop
    mov [sh_mhi], al
    call sh_mdrop_draw
    jmp .loop
.release:
    call sh_mitem_hit
    cmp al, SH_M_NONE
    je .closeonly
    mov ah, [sh_mopen]
    push ax
    call sh_mclose
    pop ax
    call sh_mfire
    jmp .out
.closeonly:
    call sh_mclose
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mfire - AH = menu index, AL = item index -> dispatch. Sets SI to
; [sh_ownwin] unconditionally before calling anything: this runs from
; sh_mtrack's own polling loop, not a kernel AM_ONCMD callback, so nothing
; here can assume SI already IS the window the way sh_oncmd's old kernel-
; supplied SI always was.
; -----------------------------------------------------------------------------
sh_mfire:
    push ax
    push si
    mov si, [sh_ownwin]
    cmp ah, 0
    je .file
    cmp ah, 1
    je .edit
    cmp ah, 2
    je .formula
    cmp ah, 3
    je .format
    cmp ah, 4
    je .data
    cmp ah, 5
    je .options
    cmp ah, 6
    je .macro
    cmp ah, 7
    je .sheets
    cmp ah, 8
    je .help
    jmp .out
.formula:
    or al, al
    jnz .fm1
    mov al, SH_LD_NAME
    call sh_ldlg_open
    jmp .out
.fm1:
    cmp al, 1
    jne .fm2
    mov al, SH_LD_FUNC
    call sh_ldlg_open
    jmp .out
.fm2:
    cmp al, 2
    jne .fm3
    xor byte [sh_a1style], 1          ; Reference: the item relabels itself,
    mov word [sh_i_formula+4], sh_it_ref_a1
    cmp byte [sh_a1style], 0
    je .fmref
    mov word [sh_i_formula+4], sh_it_ref_rc
.fmref:
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out
.fm3:
    cmp al, 3
    jne .fm4
    mov al, SH_ID_DEFN
    call sh_idlg_open
    jmp .out
.fm4:
    cmp al, 4
    jne .fm5
    call sh_ndlg_open
    jmp .out
.fm5:
    cmp al, 5
    jne .fm6
    mov al, SH_ID_GOTO
    call sh_idlg_open
    jmp .out
.fm6:
    mov al, SH_ID_FIND
    call sh_idlg_open
    jmp .out
.file:
    or al, al
    jnz .fopen
    mov al, SH_FDK_NEW
    call sh_fdlg_open
    jmp .out
.fopen:
    cmp al, 1
    jne .fsave
    mov al, FDLG_OPEN
    call sh_dlg
    jmp .out
.fsave:
    cmp al, 2
    jne .fsaveas
    call sh_dowrite
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out
.fsaveas:
    cmp al, 3                          ; 3 is Save As...; 4 is Print..., which
    jne .fprint                        ; used to be this label's fall-through
    mov al, SH_FDK_SAVEFMT             ; ASK for the format, then name it. The
    call sh_fdlg_open                  ; format used to be whatever extension
    jmp .out                           ; the typed name happened to end in
.fprint:
    mov word [sh_msg], sh_s_noprint
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out
.edit:
    call sh_docmd_edit
    jmp .out
.format:
    call sh_docmd_format
    jmp .out
.data:
    or al, al                          ; AL was ignored here before Chart
    jnz .data1                         ; Column.../Export were added - Data
                                        ; had exactly one item (Sort Column)
                                        ; so every click ran it regardless.
                                        ; Now a real dispatch, matching the
                                        ; or al,al chains above.
    mov al, SH_FDK_SORT
    call sh_fdlg_open
    jmp .out
.data1:
    cmp al, 1
    jne .data2
    call sh_docmd_chart
    jmp .out
.data2:
    cmp al, 2
    jne .data3
    mov al, SH_FDK_GAL
    call sh_fdlg_open
    jmp .out
.data3:
    call sh_docmd_chartexport
    jmp .out
.sheets:
    xor ah, ah                        ; al = item index = target sheet 0..3
    call sh_switchsheet
    jmp .out
.options:
    call sh_docmd_options
    jmp .out
.macro:
    call sh_macro_run
    jmp .out
.help:
    call sh_docmd_help
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_docmd_options - AL = 0 Gridlines / 1 Formulas: flip the flag, re-point
; the item's own string to the matching On/Off label (the same relabel-by-
; repointing idea documented above MENU_DIS in apps/os88api.inc, applied to
; sh_i_options directly rather than through the kernel), repaint.
; -----------------------------------------------------------------------------
sh_docmd_options:
    push si
    cmp al, 2
    je .calc
    or al, al
    jnz .formulas
    xor byte [sh_gridlines], 1
    cmp byte [sh_gridlines], 0
    je .goff
    mov word [sh_i_options], sh_it_grid_on
    jmp .repaint
.goff:
    mov word [sh_i_options], sh_it_grid_off
    jmp .repaint
.formulas:
    xor byte [sh_showformulas], 1
    cmp byte [sh_showformulas], 0
    je .foff
    mov word [sh_i_options+2], sh_it_form_on
    jmp .repaint
.foff:
    mov word [sh_i_options+2], sh_it_form_off
.calc:
    mov al, SH_FDK_CALC
    call sh_fdlg_open
    pop si
    ret
.repaint:
    mov si, [sh_ownwin]
    call sh_repaint
    pop si
    ret

; -----------------------------------------------------------------------------
; sh_docmd_help - the only Help item, About Sheet...
; -----------------------------------------------------------------------------
sh_docmd_help:
    call sh_about
    ret

; -----------------------------------------------------------------------------
; sh_about - the OSAPI_ABOUT_SET handler (slot 0x01E0, SPEC.md 12.2).
; in: SI = our window ptr; the UI task, gfx lock HELD, far-called at our own
; segment - a window callback in every respect that matters.
;
; Help > About Sheet... calls the SAME routine, so the two cannot say different
; things. Keeping the menu item as well as the name pull-down is deliberate:
; Excel 2.1d has a Help menu and this app follows Excel, while the pull-down is
; what os8088 users reach for.
; -----------------------------------------------------------------------------
sh_about:
    push ax
    push bx
    push si
    push di
    mov al, OS88UI_AOK
    mov bx, [sh_ownwin]
    mov si, sh_s_about
    mov di, sh_help_ack
    call os88ui_ask
    pop di
    pop si
    pop bx
    pop ax
    ret
sh_help_ack:
    ret

; -----------------------------------------------------------------------------
; sh_docmd_format - Format menu item AL opens the matching dialog (stage
; 1.8: real Excel's own Format menu is dialog-per-verb - Number.../
; Alignment.../Font... - not a flat immediate-apply list, per the reference
; screenshots at VM_screenshots/dialog_{number,alignment,font}.png; Sheet's
; menu now matches that shape, see the item table below). AL is 0 Number,
; 1 Alignment, 2 Font - the same order sh_fdlg_open expects.
; -----------------------------------------------------------------------------
; sh_docmd_format - Format menu item AL: 0 Number/1 Alignment/2 Font map
; straight onto sh_fdlg_open's own kind numbers. 3 Border opens the
; separate sh_bdlg_* checkbox dialog. 4 Row Height/5 Column Width do NOT
; map straight through - sh_fdlg_open's kinds 3/4 are already Insert/
; Delete (borrowed by the Edit menu), so they're remapped here to kinds
; 6/5 respectively.
sh_docmd_format:
    cmp al, 3
    jne .notborder
    call sh_bdlg_open
    ret
.notborder:
    cmp al, 4
    jne .notrowh
    mov al, SH_ID_ROWH                 ; stage 3.0c: a typed number now, not
    call sh_idlg_open                  ; the 3-preset radio pick this had to
    ret                                ; be while no text field existed
.notrowh:
    cmp al, 5
    jne .notcolw
    mov al, SH_ID_COLW
    call sh_idlg_open
    ret
.notcolw:
    call sh_fdlg_open
    ret

; -----------------------------------------------------------------------------
; sh_docmd_edit - Edit menu item AL. 0 is "Can't Undo" (MENU_DIS - the
; kernel never sends a click for a disabled item, so index 0 is dead here,
; not a bug). 1 Cut, 2 Copy, 3 Paste use the real system clipboard
; (OSAPI_CLIP_*). 4 Clear. 5 Delete... / 6 Insert... both open the
; Row/Column picker (sh_fdlg_* kinds 4 and 3 - see the dialog engine's own
; comment for why one engine now serves 5 kinds). 7 Fill Right / 8 Fill
; Down and 9 Sort Column are deliberately scoped down from real Excel: no
; range selection exists in this app (W_ONDRAG is missing on one of the
; two kernel variants and W_ONCLICK carries no Shift state, so a real
; rectangular selection was ruled out) - fill acts on just the one
; adjacent cell, and sort acts on the whole of the selected column.
; -----------------------------------------------------------------------------
sh_docmd_edit:
    cmp al, 1
    je .cut
    cmp al, 2
    je .copy
    cmp al, 3
    je .paste
    cmp al, 4
    je .clear
    cmp al, 5
    je .delete
    cmp al, 6
    je .insert
    cmp al, 7
    je .fillright
    cmp al, 8
    je .filldown
    cmp al, 9
    je .sort
    ret
.cut:
    call sh_docmd_cut
    ret
.copy:
    call sh_docmd_copy
    ret
.paste:
    call sh_docmd_paste
    ret
.clear:
    mov al, SH_FDK_CLEAR
    call sh_fdlg_open
    ret
.delete:
    mov al, 4
    call sh_fdlg_open
    ret
.insert:
    mov al, 3
    call sh_fdlg_open
    ret
.fillright:
    call sh_docmd_fillright
    ret
.filldown:
    call sh_docmd_filldown
    ret
.sort:
    call sh_docmd_sortcol
    ret

; -----------------------------------------------------------------------------
; sh_cell_totext - the text of the cell at (AX,BX) into sh_clipbuf: a formula's
; own source with its '=' restored, a label's characters, or a number as
; decimal. An empty cell gives an empty string. out: CX = the length.
;
; Copy used to do this inline and got two of the three wrong. It read a WORD at
; SH_C_VAL and ran sh_itoa over it - which is what everything did before stage
; 4.0, and is meaningless now that the value is an eight-byte double whose low
; word is mantissa bits (sh_cellnum exists to say exactly that). And it had no
; case for a label at all, so copying a column heading ran the numeric path
; over its text offset. One routine now, so the next thing that needs a cell as
; text cannot get a third answer.
; -----------------------------------------------------------------------------
sh_cell_totext:
    push ax
    push bx
    push dx                           ; callers loop on DX; sh_cellnum and the
    push si                           ; arena copy below both go through it
    push di
    push es
    mov byte [sh_clipbuf], 0
    call sh_findcell
    jnc .count
    mov es, [sh_cellseg]
    test byte [es:di+4], 1            ; HASFORMULA
    jz .notformula
    mov ax, [es:di+SH_C_FOFF]
    mov byte [sh_clipbuf], '='
    mov di, sh_clipbuf + 1
    jmp .arena
.notformula:
    cmp byte [es:di+SH_C_TYPE], SH_T_TEXT
    je .label
    call sh_cellnum                   ; the eight value bytes, as decimal
    mov si, sh_numbuf
    mov di, sh_clipbuf
    call sh_strcpy
    jmp .count
.label:
    mov ax, [es:di+SH_C_FOFF]         ; a label shares the formula arena
    mov di, sh_clipbuf
.arena:
    mov si, ax
    mov es, [sh_txtseg]
.acopy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .acopy
.count:
    xor cx, cx
    mov si, sh_clipbuf
.cnt:
    cmp byte [si], 0
    je .out
    inc si
    inc cx
    jmp .cnt
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_docmd_copy - builds the selected cell's text (a formula's own source
; text with its '=' restored, or a plain value's decimal text - the same
; two cases sh_beginedit already knows how to build, just targeting
; sh_clipbuf instead of sh_editbuf) and hands it to the real clipboard. An
; empty cell empties the clipboard instead (CX=0 is documented as not an
; error).
; -----------------------------------------------------------------------------
sh_docmd_copy:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    ; THE TOP-LEFT of the selection, not the anchor: a drag can start at any
    ; corner, and Paste's reference shift is measured from where the block
    ; began rather than from where the mouse went down.
    mov ax, [sh_selcol]
    mov bx, [sh_selcol2]
    cmp ax, bx
    jbe .cpc
    xchg ax, bx
.cpc:
    mov [sh_clip_col], ax
    mov cx, bx                         ; cx = last column
    mov ax, [sh_selrow]
    mov bx, [sh_selrow2]
    cmp ax, bx
    jbe .cpr
    xchg ax, bx
.cpr:
    mov [sh_clip_row], ax
    mov byte [sh_clip_valid], 1
    ; --- build the block as TAB-SEPARATED TEXT in the staging segment ------
    ; Tabs between columns, CR/LF between rows, and nothing after the last
    ; one - which is exactly what Excel puts on the clipboard, makes a 1x1
    ; block byte-identical to what this used to write, and means a copied
    ; block pastes into Word as text that lines up.
    push bp
    mov es, [sh_stgseg]
    xor di, di                         ; di = the write cursor
    mov dx, ax                         ; dx = the current row
.rowloop:
    mov bp, [sh_clip_col]              ; bp = the current column
.colloop:
    push ax
    push bx
    push cx
    push dx
    mov ax, bp
    mov bx, dx
    call sh_cell_totext                ; -> sh_clipbuf, CX = length
    mov si, sh_clipbuf
.emit:
    or cx, cx
    jz .emitted
    cmp di, SH_STAGE_MAX - 4           ; the staging area is the bound here
    jae .emitted
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jmp .emit
.emitted:
    pop dx
    pop cx
    pop bx
    pop ax
    cmp bp, cx
    jae .rowend
    cmp di, SH_STAGE_MAX - 4
    jae .rowend
    mov byte [es:di], 9                ; TAB between columns
    inc di
    inc bp
    jmp .colloop
.rowend:
    cmp dx, bx                         ; bx is still the last row
    jae .blockdone
    cmp di, SH_STAGE_MAX - 4
    jae .blockdone
    mov byte [es:di], 13               ; CR/LF between rows, none after the
    inc di                             ; last - so a single cell is exactly
    mov byte [es:di], 10               ; its own text, as before
    inc di
    inc dx
    jmp .rowloop
.blockdone:
    pop bp
    mov cx, di
    xor si, si
    call OSAPI_CLIP_PUT                ; ES is already the staging segment
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sh_docmd_cut - Copy, then Clear
sh_docmd_cut:
    push ax
    push bx
    push cx
    push si
    push di
    call sh_docmd_copy                 ; the whole block goes to the clipboard,
    mov ax, [sh_selrow]                ; so the whole block has to leave the
    mov bx, [sh_selrow2]               ; sheet - it cleared the anchor alone
    cmp ax, bx                         ; and left the rest of what it had just
    jbe .cutrows                       ; copied sitting there
    xchg ax, bx
.cutrows:
    mov cx, [sh_selcol]
    mov si, [sh_selcol2]
    cmp cx, si
    jbe .cutcols
    xchg cx, si
.cutcols:
.cutcolloop:
    mov di, ax
.cutrowloop:
    push ax
    push bx
    mov ax, cx
    mov bx, di
    call sh_clearcell
    pop bx
    pop ax
    inc di
    cmp di, bx
    jbe .cutrowloop
    inc cx
    cmp cx, si
    jbe .cutcolloop
    mov si, [sh_ownwin]
    call sh_repaint
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_docmd_paste - reads the system clipboard straight into sh_editbuf
; (capped to SH_EDITMAX, same as anything a keyboard could ever produce
; there); if this instance's own last Copy captured a formula AND a
; source cell (sh_clip_valid), and the destination differs from it,
; shifts every reference in the pasted formula by the (col, row) delta
; between them (sh_formula_copyshift) - real Excel's own default
; relative-reference behavior - before calling sh_commit to reuse its
; existing value/formula parsing exactly as if this (possibly rewritten)
; text had been typed. An empty clipboard is a no-op.
; -----------------------------------------------------------------------------
sh_docmd_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_CLIP_SIZE
    jc .out
    or ax, ax
    jz .out
    cmp ax, SH_STAGE_MAX - 2           ; the staging segment holds the block
    jbe .fits                          ; while it is taken apart
    mov ax, SH_STAGE_MAX - 2
.fits:
    mov cx, ax
    mov es, [sh_stgseg]
    xor di, di
    call OSAPI_CLIP_GET
    mov [sh_pb_len], cx
    mov ax, [sh_selcol]                ; where the block lands
    mov [sh_pb_c0], ax
    mov ax, [sh_selrow]
    mov [sh_pb_r0], ax
    mov word [sh_pb_x], 0
    mov word [sh_pb_y], 0
    mov word [sh_pb_cur], 0
.cell:
    mov ax, [sh_pb_cur]
    cmp ax, [sh_pb_len]
    jae .done
    ; --- one cell's text out of the block, up to TAB, CR, LF or the end ----
    mov es, [sh_stgseg]
    mov si, ax
    mov di, sh_editbuf
    xor cx, cx
.take:
    cmp si, [sh_pb_len]
    jae .took
    mov al, [es:si]
    cmp al, 9
    je .took
    cmp al, 13
    je .took
    cmp al, 10
    je .took
    cmp cx, SH_EDITMAX
    jae .skiptail
    mov [di], al
    inc di
    inc si
    inc cx
    jmp .take
.skiptail:
    inc si                             ; over-long cell: drop the rest, so the
    jmp .take                          ; dispatch below sees the cell's real
                                       ; terminator - its 64th character read
                                       ; as "new row" and smeared each further
                                       ; 63-byte chunk one row down
.took:
    mov byte [di], 0
    mov [sh_editlen], cl
    mov [sh_pb_cur], si                ; the terminator is consumed below
    call sh_paste_cell
    ; --- what ended it decides where the next one goes --------------------
    mov es, [sh_stgseg]
    mov si, [sh_pb_cur]
    cmp si, [sh_pb_len]
    jae .done
    mov al, [es:si]
    inc si
    mov [sh_pb_cur], si
    cmp al, 9                          ; TAB: the next column
    jne .newrow
    inc word [sh_pb_x]
    jmp .cell
.newrow:
    cmp al, 13                         ; CR, and swallow an LF behind it
    jne .lfonly
    cmp si, [sh_pb_len]
    jae .rowdone
    cmp byte [es:si], 10
    jne .rowdone
    inc si
    mov [sh_pb_cur], si
.rowdone:
.lfonly:
    mov word [sh_pb_x], 0
    inc word [sh_pb_y]
    jmp .cell
.done:
    mov ax, [sh_pb_c0]                 ; put the selection back where it was
    mov [sh_selcol], ax
    mov [sh_selcol2], ax
    mov ax, [sh_pb_r0]
    mov [sh_selrow], ax
    mov [sh_selrow2], ax
    mov si, [sh_ownwin]                ; ONE repaint for the whole block
    call sh_repaint
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
; sh_paste_cell - commit sh_editbuf into the block cell at (sh_pb_x, sh_pb_y),
; shifting a formula's relative references by the SAME delta for every cell in
; the block: where the block landed, less where it was copied from. That is
; Excel's rule, and it is what makes a copied column of =A1*B1 still line up a
; column over.
;
; It moves sh_selcol/sh_selrow and calls sh_commit rather than reimplementing
; the decision, because sh_commit is the ONE place that decides whether text
; is a formula, a number or a label - a second copy of that would be a second
; answer. The selection is restored by the caller when the block is done.
; -----------------------------------------------------------------------------
sh_paste_cell:
    push ax
    push bx
    push cx
    push si
    push di
    mov ax, [sh_pb_c0]
    add ax, [sh_pb_x]
    cmp ax, SH_COLS
    jae .out                           ; a block that runs off the edge stops
    mov [sh_selcol], ax                ; at it rather than wrapping
    mov [sh_selcol2], ax
    mov bx, [sh_pb_r0]
    add bx, [sh_pb_y]
    cmp bx, SH_ROWS
    jae .out
    mov [sh_selrow], bx
    mov [sh_selrow2], bx
    cmp byte [sh_clip_valid], 0
    je .commit
    cmp byte [sh_editbuf], '='
    jne .commit
    mov ax, [sh_pb_c0]
    sub ax, [sh_clip_col]
    mov [sh_cp_coldelta], ax
    mov bx, [sh_pb_r0]
    sub bx, [sh_clip_row]
    mov [sh_cp_rowdelta], bx
    or ax, bx
    jz .commit                         ; pasted onto the cell it came from
    mov si, sh_editbuf
    inc si
    mov di, sh_rwsrc
.copyin:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyin
    mov si, sh_rwsrc
    call sh_formula_copyshift
    mov byte [sh_editbuf], '='
    mov si, sh_rwdst
    mov di, sh_editbuf + 1
    mov cx, SH_EDITMAX - 1             ; a shifted reference can grow a digit
.copyout:                              ; or two, so clip rather than overrun
    mov al, [si]
    or al, al
    jz .copyoutdone
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .copyout
.copyoutdone:
    mov byte [di], 0
    xor cx, cx
    mov si, sh_editbuf
.relen:
    cmp byte [si], 0
    je .haverelen
    inc si
    inc cx
    jmp .relen
.haverelen:
    mov [sh_editlen], cl
.commit:
    mov byte [sh_editing], 1
    call sh_commit
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_docmd_fillright / sh_docmd_filldown - fill the SELECTED RANGE from its
; own first column/row, the way Excel does: Fill Down copies the selection's
; top row into every row under it, for every column in the selection, and
; Fill Right copies its left column across.
;
; These two used to copy exactly one cell into exactly one neighbour, because
; they were written before range selection existed (stage 3.0a) and were never
; taught about sh_selcol2/sh_selrow2. Selecting a block and choosing Fill Down
; changed a single cell and reported nothing - the selection stayed drawn over
; cells that had not been touched. A SINGLE-CELL selection still fills the one
; neighbour, which is what the old behaviour was and what collapsing the
; anchor and extent already means here.
;
; A formula source has its text copied through sh_formula_copyshift (the same
; relative-reference shift Copy/Paste uses), and the delta is the FULL offset
; from the source rather than always one - filling five rows down has to shift
; the fifth by five. A plain value is copied as its current value.
; -----------------------------------------------------------------------------
; sh_fill_copy - one cell to another. in: sh_fl_scol/srow = source,
; sh_fl_dcol/drow = destination. An empty source copies nothing. Preserves
; every register, so the loops below can keep their bounds in theirs.
; -----------------------------------------------------------------------------
sh_fill_copy:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [sh_fl_scol]
    mov bx, [sh_fl_srow]
    call sh_findcell
    jnc .out                           ; empty source: nothing to fill
    mov es, [sh_cellseg]
    test byte [es:di+4], 1             ; HASFORMULA
    jz .plain
    mov ax, [es:di+SH_C_FOFF]
    mov si, ax
    mov es, [sh_txtseg]
    mov di, sh_rwsrc
.copyin:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyin
    mov ax, [sh_fl_dcol]
    sub ax, [sh_fl_scol]
    mov [sh_cp_coldelta], ax
    mov ax, [sh_fl_drow]
    sub ax, [sh_fl_srow]
    mov [sh_cp_rowdelta], ax
    mov si, sh_rwsrc
    call sh_formula_copyshift
    mov ax, [sh_fl_dcol]
    mov bx, [sh_fl_drow]
    mov si, sh_rwdst
    call sh_setformula
    jmp .out
.plain:
    mov ax, [sh_fl_scol]
    mov bx, [sh_fl_srow]
    call sh_getcell2                   ; the full double lands in sh_acc, and
    cmp byte [sh_curtype], SH_T_TEXT   ; the tag says label or number (81.13 -
    je .text                           ; 81.18's Copy defect, closed here too)
    mov ax, [sh_fl_dcol]
    mov bx, [sh_fl_drow]
    call sh_setvald                    ; sh_setval would truncate 3.5 to 3
    jmp .out
.text:
    mov si, [sh_curtoff]               ; a LABEL: copy its text out of
    mov es, [sh_txtseg]                ; sh_txtseg into DS scratch, because
    mov di, sh_rwsrc                   ; sh_settext reads DS:SI
.tcopy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .tcopy
    mov ax, [sh_fl_dcol]
    mov bx, [sh_fl_drow]
    mov si, sh_rwsrc
    call sh_settext
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
sh_docmd_fillright:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [sh_selcol]                ; normalise: a drag can run either way
    mov bx, [sh_selcol2]
    cmp ax, bx
    jbe .colsok
    xchg ax, bx
.colsok:
    mov [sh_fl_scol], ax               ; the source is the LEFTMOST column
    cmp ax, bx
    jne .haverange
    inc bx                             ; a one-column selection fills the one
    cmp bx, SH_COLS                    ; column over, as this always did
    jae .out
.haverange:
    mov cx, [sh_selrow]                ; ...for every row of the selection
    mov si, [sh_selrow2]
    cmp cx, si
    jbe .rowsok
    xchg cx, si
.rowsok:
.rowloop:
    mov [sh_fl_srow], cx
    mov [sh_fl_drow], cx
    mov dx, [sh_fl_scol]
.colloop:
    inc dx
    cmp dx, bx
    ja .nextrow
    mov [sh_fl_dcol], dx
    call sh_fill_copy
    jmp .colloop
.nextrow:
    inc cx
    cmp cx, si
    jbe .rowloop
    mov si, [sh_ownwin]                ; ONE repaint for the whole fill, not
    call sh_repaint                    ; one per cell
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sh_docmd_filldown:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [sh_selrow]
    mov bx, [sh_selrow2]
    cmp ax, bx
    jbe .rowsok
    xchg ax, bx
.rowsok:
    mov [sh_fl_srow], ax               ; the source is the TOP row
    cmp ax, bx
    jne .haverange
    inc bx                             ; a one-row selection fills the one
    cmp bx, SH_ROWS                    ; row below, as this always did
    jae .out
.haverange:
    mov cx, [sh_selcol]                ; ...for every column of the selection
    mov si, [sh_selcol2]
    cmp cx, si
    jbe .colsok
    xchg cx, si
.colsok:
.colloop:
    mov [sh_fl_scol], cx
    mov [sh_fl_dcol], cx
    mov dx, [sh_fl_srow]
.rowloop:
    inc dx
    cmp dx, bx
    ja .nextcol
    mov [sh_fl_drow], dx
    call sh_fill_copy
    jmp .rowloop
.nextcol:
    inc cx
    cmp cx, si
    jbe .colloop
    mov si, [sh_ownwin]
    call sh_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_sort_carry - apply the key column's permutation to every OTHER column in
; the selection, so a table sorts as ROWS rather than as one column sliding
; past its neighbours.
;
; This is what "why can't we sort multiple columns" wanted. Sorting one column
; of a table and leaving the others put does not sort anything - it breaks the
; correspondence between them, silently, and the sheet looks ordinary
; afterwards. Excel sorts whole rows by a key column; so does this now.
;
; The permutation is already computed: rows[] are the target rows and
; origidx[i] says which original entry belongs at position i. Each column is
; SNAPSHOT AS TEXT first, because writing a column in place would overwrite
; cells the permutation still has to read. Text is the intermediate because it
; covers values, labels and formulas with one representation - sh_cell_totext
; on the way out and sh_commit on the way back, which is the same pair the
; block clipboard uses (81.18) and the same single decision about what a
; string means.
; -----------------------------------------------------------------------------
sh_sort_carry:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [sh_sort_cnt], 2
    jb .out
    cmp word [sh_sort_cnt], SH_SORT_SNAPCAP
    ja .out                           ; more rows than the snapshot holds: the
                                      ; key column is still sorted, the others
                                      ; are left alone rather than half moved
    mov ax, [sh_selcol]               ; THE KEY COLUMN, banked before anything
    mov [sh_cry_key], ax              ; else runs: sh_sort_permcol moves
    mov bx, [sh_selrow]               ; sh_selcol to commit into each carried
    mov [sh_cry_keyrow], bx           ; column, so reading it later names
    mov bx, [sh_selcol2]              ; whichever column was carried last and
    cmp ax, bx                        ; the key gets carried too - permuted a
    jbe .cols                         ; second time, on top of its own sort
    xchg ax, bx
.cols:
    mov [sh_cry_c1], ax
    mov [sh_cry_c2], bx
    cmp ax, bx
    je .out                           ; one column: the write-back did it all
    mov cx, ax
.colloop:
    cmp cx, [sh_cry_c2]
    ja .done
    cmp cx, [sh_cry_key]
    je .nextcol                       ; the key column is already written
    mov [sh_cry_col], cx
    call sh_sort_snapcol
    call sh_sort_permcol
    jc .done                          ; the arena refused (message already
                                      ; set): stop carrying, restore the
                                      ; selection and let the repaint say so
.nextcol:
    inc cx
    jmp .colloop
.done:
    mov ax, [sh_cry_key]              ; put the selection back where it was
    mov [sh_selcol], ax
    mov [sh_selcol2], ax
    mov ax, [sh_cry_keyrow]
    mov [sh_selrow], ax
    mov [sh_selrow2], ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_sort_snapcol - column [sh_cry_col]'s cells, for the sorted rows, as text
; in the snapshot slots. An empty cell snapshots as an empty string, which is
; what makes the permutation able to move emptiness around like anything else.
sh_sort_snapcol:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [sh_cry_i], 0            ; THE COUNTER LIVES IN BSS, not in a
.snap:                                ; register: this loop calls out to
    mov dx, [sh_cry_i]                ; sh_cell_totext and multiplies, and an
    cmp dx, [sh_sort_cnt]             ; index in DX did not survive either -
    jae .out                          ; the loop simply never ended and the
                                      ; machine stopped
    mov es, [sh_stgseg]
    mov si, dx
    shl si, 1
    mov bx, [es:si]                   ; rows[i]
    mov ax, [sh_cry_col]
    call sh_cell_totext               ; -> sh_clipbuf, CX = length
    cmp cx, 63
    jbe .fits
    mov cx, 63
.fits:
    mov ax, [sh_cry_i]                ; slot = SNAP + i*64
    mov di, 64
    mul di
    add ax, SH_SORT_SNAP_OFF
    mov di, ax
    mov es, [sh_stgseg]
    mov si, sh_clipbuf
    jcxz .term
.scopy:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .scopy
.term:
    mov byte [es:di], 0
    inc word [sh_cry_i]
    jmp .snap
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_sort_permcol - write column [sh_cry_col] back in the sorted order, each
; cell shifted by its own row delta the way the key column's own write-back
; shifts formulas. Out: CF=1 = the text arena refused a commit mid-permutation:
; [sh_msg] says so and the caller must stop carrying further columns - going
; on would keep burning the arena and keep half-applying.
sh_sort_permcol:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [sh_cry_i], 0            ; in bss for sh_sort_snapcol's reason
.perm:
    mov dx, [sh_cry_i]
    cmp dx, [sh_sort_cnt]
    jae .out
    mov es, [sh_stgseg]
    mov si, dx
    shl si, 1
    add si, SH_SORT_ORIG_OFF
    mov bx, [es:si]                   ; src = origidx[dx]
    cmp bx, dx
    je .nextperm                      ; already in place: nothing to rewrite
    mov [sh_cry_src], bx
    mov si, dx                        ; target row = rows[dx]
    shl si, 1
    mov ax, [es:si]
    mov [sh_cry_trow], ax
    mov si, bx                        ; source row = rows[src]
    shl si, 1
    mov ax, [es:si]
    mov [sh_cry_srow], ax
    ; the snapshot slot for src, into sh_editbuf
    mov ax, [sh_cry_src]
    mov di, 64
    mul di
    add ax, SH_SORT_SNAP_OFF
    mov si, ax
    mov di, sh_editbuf
    xor cx, cx
.load:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .loaded
    inc si
    inc di
    inc cx
    jmp .load
.loaded:
    mov [sh_editlen], cl
    mov ax, [sh_cry_col]               ; where it is going
    mov [sh_selcol], ax
    mov [sh_selcol2], ax
    mov ax, [sh_cry_trow]
    mov [sh_selrow], ax
    mov [sh_selrow2], ax
    cmp byte [sh_editbuf], '='
    jne .commit
    mov ax, [sh_cry_trow]              ; a formula follows its own row
    sub ax, [sh_cry_srow]
    mov [sh_cp_rowdelta], ax
    mov word [sh_cp_coldelta], 0
    or ax, ax
    jz .commit
    mov si, sh_editbuf
    inc si
    mov di, sh_rwsrc
.fcopy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .fcopy
    mov si, sh_rwsrc
    call sh_formula_copyshift
    mov byte [sh_editbuf], '='
    mov si, sh_rwdst
    mov di, sh_editbuf + 1
    mov cx, SH_EDITMAX - 1
.fout:
    mov al, [si]
    or al, al
    jz .fdone
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .fout
.fdone:
    mov byte [di], 0
    xor cx, cx
    mov si, sh_editbuf
.flen:
    cmp byte [si], 0
    je .fhave
    inc si
    inc cx
    jmp .flen
.fhave:
    mov [sh_editlen], cl
.commit:
    mov byte [sh_editing], 1
    call sh_commit
    jc .arenafull                     ; the text arena refused: entries already
.nextperm:                            ; written hold the new order, the rest
    inc word [sh_cry_i]               ; the old - stop and SAY SO rather than
    jmp .perm                         ; keep half-applying in silence
.out:
    clc
    jmp .ret
.arenafull:
    mov word [sh_msg], sh_s_sortfull
    stc
.ret:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sh_s_sortfull: db 'Sort incomplete - text area full.', 0

; -----------------------------------------------------------------------------
; sh_docmd_sortcol - sorts the selected column's occupied cells (on the
; current sheet) ascending by value; empty rows are left exactly where
; they are, so occupied cells are compacted toward the top the same way
; the original plain-values-only sort already did. Stage 2.x: a formula
; cell now sorts right alongside plain values (by its CURRENT evaluated
; value, via sh_getcell2, so staleness is never an issue) and, if the
; sort actually moves it to a different row, its own text is rewritten
; with sh_formula_copyshift (a (0, row-delta) shift, the same machinery
; Copy/Paste and Fill Down use) so its references still mean what they
; looked like they meant - matching real Excel's own behavior, where
; sorting a range that contains formulas carries their relative
; references along with them. Previously formula cells were excluded
; from the sort entirely (skipped, left in their original row) purely
; because this reference-adjustment capability did not exist yet.
;
; Method: one linear pass over the cell array collects this column's
; occupied cells into sh_stgseg - rows[] at offset 0, values[] at
; SH_SORT_VALS_OFF (both well under its 32KB claim: SH_CELL_CAP=1365
; entries needs at most 2730 bytes each), plus origidx[]/isformula[]/
; fidx[]/staged-formula-text (SH_SORT_ORIG_OFF/SH_SORT_ISF_OFF/
; SH_SORT_FIDX_OFF/SH_SORT_FTXT_OFF - see their own equ comments). Because
; the cell array is sorted by row within a sheet (the stage 2.0 comment
; above sh_findcell), rows[] comes out already ascending for free -
; sorting is really just "which ORIGINAL entry's data ends up at which
; ascending row", so values[] and origidx[] are insertion-sorted together
; (a parallel permutation, not just a value sort) and then written back:
; a plain value straight via sh_setval as before; a formula, only if it
; actually changed row, via sh_formula_copyshift + sh_setformula using
; that specific cell's own (target row - its original row) delta - each
; moved formula can have a DIFFERENT delta, since a sort is an arbitrary
; reordering, not a uniform shift like Insert/Delete Row or Copy/Paste.
; There is no range selection (see the W_ONDRAG scope note on
; sh_docmd_edit), so this always acts on the whole column.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sh_chart_scan - (re)collect [sh_chart_sheet]/[sh_chart_col]'s plain-value
; cells into sh_stgseg/sh_chart_cnt, capped at CH_MAXBARS (same shape as
; sh_docmd_sortcol's own scan). Does NOT touch sh_chart_sheet/sh_chart_col
; themselves - sh_docmd_chart freezes those from the current selection
; before calling this; the live-update hook in sh_repaint calls this
; against whatever was already frozen, so every edit to the charted column
; is reflected without retargeting the chart to wherever the selection
; happens to be at the time.
; -----------------------------------------------------------------------------
; sh_chart_scan - series ONE from [sh_chart_col], then series TWO from the
; column to its right if that column holds anything. Two passes rather than one
; because the cell array is sorted by row and then column, so a single walk
; would interleave them and both series must come out in row order.
sh_chart_scan:
    push ax
    mov ax, [sh_chart_col]
    mov [sh_scan_col], ax
    mov word [sh_scan_off], SH_CHART_D1
    call sh_chart_scan1
    mov ax, [sh_chart_cnt2]           ; sh_chart_scan1 leaves its count here
    mov [sh_chart_cnt], ax
    mov ax, [sh_chart_col]
    inc ax
    cmp ax, SH_COLS
    jae .nosecond
    mov [sh_scan_col], ax
    mov word [sh_scan_off], SH_CHART_D2
    call sh_chart_scan1
    jmp .done
.nosecond:
    mov word [sh_chart_cnt2], 0
.done:
    mov word [ch_arr2], SH_CHART_S2
    mov ax, [sh_chart_cnt2]
    mov [ch_cnt2], ax
    mov ax, [sh_stgseg]
    mov [ch_srcseg2], ax
    ; --- the doubles become the words the drawing runs on (82.13) ----------
    ; SERIES TWO FIRST, so [ch_e10] is left holding SERIES ONE's exponent:
    ; that is the series the value axis is labelled from.
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [sh_stgseg]
    mov si, SH_CHART_D2
    mov di, SH_CHART_S2
    mov cx, [sh_chart_cnt2]
    call ch_scale
    mov dx, [sh_stgseg]
    mov si, SH_CHART_D1
    xor di, di
    mov cx, [sh_chart_cnt]
    call ch_scale
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sh_chart_scan1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [sh_chart_cnt2], 0
    xor cx, cx
.scan:
    mov ax, [sh_chart_cnt2]
    cmp ax, CH_MAXBARS
    jae .scandone
    cmp cx, [sh_ncells]
    jae .scandone
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov si, ax
    mov es, [sh_cellseg]
    mov ax, [es:si]
    call sh_unpackrow                  ; ax=row, bx=sheet
    cmp bx, [sh_chart_sheet]
    jne .next
    mov dx, [es:si+2]                  ; col
    cmp dx, [sh_scan_col]
    jne .next
    test byte [es:si+4], 1             ; HASFORMULA: chart its CURRENT value
    jz .plainval                       ; (sh_getcell2 evaluates transparently
    mov bx, ax                         ; and is never stale) rather than
                                        ; skipping it - a column of formulas
                                        ; used to chart as completely empty.
                                        ; AX still holds this record's row
                                        ; from sh_unpackrow above.
    mov ax, [sh_scan_col]
    push cx                            ; CX is this scan's own index and
    push si                            ; sh_getcell2 does not preserve it -
    call sh_getcell2                   ; see the matching note in
    pop si                             ; sh_docmd_sortcol.
    pop cx
    jmp .havevalue
.plainval:
    call sh_cellval_to_acc_si          ; the WHOLE double, not the truncation
                                        ; of it: the chart plots 43.6 as 43.6
                                        ; now (82.13), and ch_scale is what
                                        ; turns the series into the words the
                                        ; drawing runs on
.havevalue:                            ; the value is in sh_acc either way
    mov ax, [sh_chart_cnt2]
    mov dx, 8
    mul dx
    add ax, [sh_scan_off]
    mov di, ax
    mov es, [sh_stgseg]
    push cx                            ; CX IS THIS SCAN'S OWN RECORD INDEX -
    push si                            ; the copy below needs a counter, and
    mov si, sh_acc                     ; borrowing it would restart the walk
    mov cx, 4
.vcopy:
    mov ax, [si]
    mov [es:di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .vcopy
    pop si
    pop cx
    inc word [sh_chart_cnt2]
.next:
    inc cx
    jmp .scan
.scandone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_docmd_chart - Data > Chart Column...: freeze the current sheet/column,
; scan its plain values via sh_chart_scan, then create-or-show the chart
; window and draw it.
;
; The window, once created, is NEVER destroyed by this app again - only
; shown/hidden. Traced directly against the kernel (kernel/instance.inc's
; app_close_win): clicking a non-primary window's own close box only calls
; wm_hide, not a real destroy - the record stays valid, just invisible,
; until the whole instance tears down (wm_destroy_seg cleans it up then,
; automatically). Re-creating a fresh window on every "Chart Column..."
; click would leak one of the system's MAX_WIN=12 window slots per click;
; treating a second click as "just show it again" does not.
; -----------------------------------------------------------------------------
sh_docmd_chart:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [sh_cursheet]
    mov [sh_chart_sheet], ax
    mov ax, [sh_selcol]
    mov [sh_chart_col], ax
    call sh_chart_scan
    cmp word [sh_chartwin], 0
    jne .haswin
    mov si, sh_chart_tpl
    call OSAPI_WM_CREATE
    jc .out                            ; refused: silently give up, same
                                        ; scope limit sh_fdlg_open's own
                                        ; "already open, stay safe" has
    mov [sh_chartwin], bx
.haswin:
    mov bx, [sh_chartwin]
    call OSAPI_WM_SHOW
    call sh_chart_render
    mov si, [sh_chartwin]
    call sh_chart_paint
    mov word [sh_msg], sh_s_charted
    mov si, [sh_ownwin]                ; repaint OUR OWN window too - this
    call sh_repaint                    ; only ever painted the chart window
                                        ; above, so the status bar's own
                                        ; "Charted." message was set but
                                        ; never actually shown until now
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_chart_render - rasterize the currently-staged values (sh_stgseg
; offset 0, count sh_chart_cnt) into the chart's own offscreen buffer
; (sh_chartseg), via apps/os88chart.inc's ch_bars_draw. DS is never
; touched - ch_bars_draw takes the array's segment as an explicit
; parameter (DX) and swaps ES internally instead of borrowing DS, so this
; caller's own bss stays reachable via the normal, unchanged DS throughout
; (see ch_bars_draw's own header comment for why that matters).
sh_chart_render:
    push ax
    push cx
    push dx
    push si
    push es
    push bx                             ; the title: "Column A", built here
    push di                             ; because only Sheet knows which column
    mov di, sh_chart_title              ; the series came from
    mov si, sh_s_coltitle
    call sh_strcpy_to_di
    mov ax, [sh_chart_col]
    call sh_colname
    mov si, sh_colbuf
    call sh_strcpy_to_di
    mov word [ch_title], sh_chart_title
    pop di
    pop bx
    mov cx, [sh_chart_cnt]
    mov es, [sh_chartseg]
    mov dx, [sh_stgseg]
    xor si, si
    call ch_draw                        ; stage 3.0f: the gallery, which Data >
                                        ; Chart Gallery... now sets. Going
                                        ; through the dispatcher rather than
                                        ; calling one drawing routine is what
                                        ; keeps this app and CHART.O88 from
                                        ; drifting into drawing the same data
                                        ; differently
    pop es
    pop si
    pop dx
    pop cx
    pop ax
    ret

; sh_chart_paint - the chart window's own W_PAINT callback (SI=window);
; one OSAPI_GFX_BLIT4 of the already-rasterized buffer, nothing else
sh_chart_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bx, si
    call OSAPI_WM_CONTENT               ; ax=content x, dx=content y
    mov [ch_bx1], ax                    ; borrow this scratch - safe here,
    mov [ch_by1], dx                    ; ch_bars_draw already finished by
                                         ; the time sh_chart_paint ever runs
    mov es, [sh_chartseg]
    mov si, CH_PXOFF
    mov bp, CH_STRIDE
    mov ax, [ch_bx1]
    mov bx, [ch_by1]
    mov cx, CH_W
    mov dx, CH_H
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sh_chart_tpl:
    dw 0, 0, SH_CHARTWIN_W, SH_CHARTWIN_H
    dw sh_s_chart_title, sh_chart_paint, 0, 0
sh_s_chart_title: db 'Chart', 0
sh_s_charted:      db 'Charted.', 0
sh_s_coltitle:     db 'Column ', 0
sh_s_onesheet:     db 'Saved - THIS SHEET ONLY; use .BIF to keep them all.', 0
sh_s_sheetnm:      db 'Sheet', 0

; sh_docmd_chartexport - Data > Export Chart as BMP...: a no-op
; informational message if there's nothing charted yet (same "still runs,
; OK is just a no-op" idiom used throughout this file), else the standard
; Save dialog, writing the chart's own offscreen buffer via
; apps/os88chart.inc's ch_bmp_write once a name is chosen.
sh_docmd_chartexport:
    push ax
    push bx
    push si
    push di
    cmp word [sh_chartwin], 0
    je .nothing
    cmp word [sh_chart_cnt], 0
    je .nothing
    mov al, FDLG_SAVE
    mov bx, [sh_ownwin]
    mov di, sh_chartexp_ondlg
    mov si, sh_s_chartbmp
    call OSAPI_FILE_DLG
    jmp .out
.nothing:
    mov word [sh_msg], sh_s_nochart
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_chartexp_ondlg - the Export Chart dialog's completion proc (SPEC.md
; 38.6, same shape as sh_ondlg but writing the chart buffer, not the sheet,
; and never reading back - Export is always a Save). In: AL=mode
; (unused), SI=our window ptr, DI=chosen name (ES=KERNEL_SEG); UI task,
; gfx lock HELD, dialog already destroyed - we owe the repaint.
; -----------------------------------------------------------------------------
sh_chartexp_ondlg:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, di
    mov di, sh_chart_name
    mov cx, SH_NAMEMAX               ; the count lives in CX - the loop
.copy:                               ; body writes AL, so AX cannot hold it
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .copied
    inc si
    inc di
    dec cx
    jnz .copy
    mov byte [di], 0
.copied:
    mov es, [sh_chartseg]
    mov bx, [sh_stgseg]
    mov si, sh_chart_name
    call ch_bmp_write
    jnc .ok
    mov word [sh_msg], sh_s_experr
    jmp .draw
.ok:
    mov word [sh_msg], sh_s_exported
.draw:
    mov si, [sh_ownwin]
    call sh_repaint
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

sh_s_chartbmp: db 'CHART.BMP', 0
sh_s_nochart:  db 'No chart to export.', 0
sh_s_experr:   db 'Chart export failed.', 0
sh_s_exported: db 'Chart exported.', 0

; -----------------------------------------------------------------------------
; sh_sort_vof / sh_sort_ldds / sh_sort_cmp - the three places the sort's value
; array is touched, so its EIGHT-BYTE stride lives in exactly one of them.
; It was a word per entry and the multiply was written inline six times; a
; widening done that way is how a missed site returns a plausible wrong number
; with no crash (the risk this file's own record-layout comment names).
;
; sh_sort_vof - in: BX = entry index, out: DI = its offset in sh_stgseg
; -----------------------------------------------------------------------------
sh_sort_vof:
    push ax
    push cx
    mov ax, bx
    mov cx, 8
    mul cx
    add ax, SH_SORT_VALS_OFF
    mov di, ax
    pop cx
    pop ax
    ret

; sh_sort_ldds - copy 8 bytes from ES:DI (the staging array) to DS:SI
sh_sort_ldds:
    push ax
    mov ax, [es:di]
    mov [si], ax
    mov ax, [es:di+2]
    mov [si+2], ax
    mov ax, [es:di+4]
    mov [si+4], ax
    mov ax, [es:di+6]
    mov [si+6], ax
    pop ax
    ret

; sh_sort_cmp - compare sh_sort_cmpv against sh_sort_keyval, leaving the flags
; a signed JL/JE/JG can read. ES is preserved because the caller is mid-walk
; through the staging segment and fp_unpack_* work in DS.
sh_sort_cmp:
    push ax
    push si
    mov si, sh_sort_cmpv
    call fp_unpack_a
    mov si, sh_sort_keyval
    call fp_unpack_b
    call fp_cmpab                     ; AX = -1/0/1 and the flags to match
    pop si
    pop ax
    ret

sh_docmd_sortcol:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; Sort the ROWS THE SELECTION COVERS, not the whole column. A single cell
    ; still means the whole column, which is what this always did and what the
    ; Data menu's one-item Sort implies.
    mov ax, [sh_selrow]
    mov bx, [sh_selrow2]
    cmp ax, bx
    jbe .sortrows
    xchg ax, bx
.sortrows:
    cmp ax, bx
    jne .haverows
    xor ax, ax
    mov bx, SH_ROWS - 1
.haverows:
    mov [sh_sort_r1], ax
    mov [sh_sort_r2], bx
    mov word [sh_sort_cnt], 0
    mov word [sh_sort_fcnt], 0
    xor cx, cx
.scan:
    cmp cx, [sh_ncells]
    jae .scandone
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov si, ax
    mov es, [sh_cellseg]
    mov ax, [es:si]
    call sh_unpackrow                 ; ax=row, bx=sheet
    cmp bx, [sh_cursheet]
    jne .next
    mov dx, [es:si+2]                 ; col
    cmp dx, [sh_selcol]
    jne .next
    cmp ax, [sh_sort_r1]              ; outside the rows asked for
    jb .next
    cmp ax, [sh_sort_r2]
    ja .next
    mov [sh_sort_row], ax             ; ax = row, stashed (0-based)
    test byte [es:si+4], 1            ; HASFORMULA
    jz .isplainval
    cmp word [sh_sort_fcnt], SH_SORT_FCAP
    jae .next                         ; too many formulas to carry through
                                       ; this sort: exclude this one
                                       ; entirely (see SH_SORT_FCAP's own
                                       ; comment)
    mov ax, dx                        ; col (== sh_selcol, just compared)
    mov bx, [sh_sort_row]
    push cx                           ; CX is this scan's own cell index and
                                       ; sh_getcell2 does NOT preserve it: for
                                       ; a formula cell it reaches sh_eval_cell
                                       ; -> sh_pcmp -> sh_pterm's own `.div`,
                                       ; which does `mov cx, ax` to hold the
                                       ; divisor. Without this save, sorting a
                                       ; column containing any formula that
                                       ; uses '/' restarts or skips the scan
                                       ; from an arbitrary index, silently
                                       ; duplicating or dropping cells.
    push si
    call sh_getcell2                  ; -> dx = its CURRENT value (never
    pop si                            ; stale - see this proc's own header)
    pop cx
    mov [sh_sort_val], dx
    mov es, [sh_cellseg]
    mov ax, [es:si+SH_C_FOFF]         ; formula_off
    mov si, ax
    mov es, [sh_txtseg]
    mov di, sh_rwsrc
.copyin:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyin
    mov ax, [sh_sort_fcnt]
    mov [sh_sort_fslot], ax
    mov bx, 64
    mul bx
    add ax, SH_SORT_FTXT_OFF
    mov di, ax
    mov es, [sh_stgseg]
    mov si, sh_rwsrc
.copyout:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    or al, al
    jnz .copyout
    inc word [sh_sort_fcnt]
    push ax                           ; a formula sorts on its RESULT, which
    push si                           ; sh_getcell2 already left in sh_acc
    push di                           ; above - re-reading it here would use
    mov si, sh_acc                    ; the SI and ES the text copy just left,
                                      ; which point into the staging segment
    mov di, sh_sort_val
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop di
    pop si
    pop ax
    mov al, 1                         ; isformula flag
    jmp .stage
.isplainval:
    mov al, [es:si+SH_C_TYPE]         ; a LABEL or an ERROR VALUE has no
    cmp al, SH_T_TEXT                 ; number to sort on, and staging one
    je .next                          ; through the value path wrote 0.0 back
    cmp al, SH_T_ERR                  ; over its text: it sits the sort out
    je .next                          ; instead - its row never enters rows[],
                                       ; the same clip-don't-crash policy
                                       ; SH_SORT_FCAP uses, which is what a
                                       ; header row over a table wants anyway
    call sh_cellval_to_acc_si         ; the WHOLE value into sh_acc, not the
    push si                           ; word at SH_S_VAL - sorting on the
    push di                           ; truncated integer made every decimal
    mov si, sh_acc                    ; in a column compare equal
    mov di, sh_sort_val
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop di
    pop si
    xor al, al                        ; isformula flag
.stage:
    mov bx, [sh_sort_cnt]
    cmp bx, SH_SORT_CAP
    jae .next                         ; the array is full: this cell sits the
                                       ; sort out, the same "clip, don't
                                       ; crash" policy SH_SORT_FCAP uses
    mov es, [sh_stgseg]
    mov di, bx
    shl di, 1
    mov dx, [sh_sort_row]
    mov [es:di], dx                   ; rows[cnt] = row
    call sh_sort_vof                  ; DI = &values[cnt]
    push ax                           ; AL IS THE ISFORMULA FLAG and the copy
    push si                           ; below moves eight bytes through AX. It
    mov si, sh_sort_val               ; was two bytes when this was a word
    mov ax, [si]                      ; array, and the flag survived by luck;
    mov [es:di], ax                   ; now the value's top word lands in AL
    mov ax, [si+2]                    ; and every ordinary number stages as a
    mov [es:di+2], ax                 ; FORMULA, whose write-back then reads a
    mov ax, [si+4]                    ; text slot that was never written
    mov [es:di+4], ax
    mov ax, [si+6]
    mov [es:di+6], ax
    pop si
    pop ax
    mov di, bx
    shl di, 1
    add di, SH_SORT_ORIG_OFF
    mov [es:di], bx                   ; origidx[cnt] = cnt (pre-sort)
    mov di, bx
    add di, SH_SORT_ISF_OFF
    mov [es:di], al                   ; isformula[cnt]
    or al, al
    jz .noformidx
    mov di, bx
    shl di, 1
    add di, SH_SORT_FIDX_OFF
    mov dx, [sh_sort_fslot]
    mov [es:di], dx                   ; fidx[cnt] = its own text slot
.noformidx:
    inc word [sh_sort_cnt]
.next:
    inc cx
    jmp .scan
.scandone:
    mov cx, [sh_sort_cnt]
    cmp cx, 2
    jb .sortdone
    mov es, [sh_stgseg]
    mov bx, 1
.outer:
    cmp bx, cx
    jae .sortdone
    push bx
    call sh_sort_vof                  ; DI = &values[bx]
    mov si, sh_sort_keyval
    call sh_sort_ldds                 ; key = values[bx], all eight bytes
    pop bx
    mov si, bx
    shl si, 1
    add si, SH_SORT_ORIG_OFF
    mov ax, [es:si]                   ; ax = key's own origidx
    mov [sh_sort_keyorig], ax
    mov di, bx
.inner:
    or di, di
    jz .insert
    push bx
    mov bx, di
    dec bx
    push di
    call sh_sort_vof                  ; DI = &values[j-1]
    mov si, sh_sort_cmpv
    call sh_sort_ldds
    pop di
    pop bx
    call sh_sort_cmp                  ; CF/ZF as a signed compare of
    je .insert                        ; values[j-1] against the key
    jl .isless
    cmp byte [sh_sort_desc], 0        ; values[j-1] > key: ascending shifts,
    jne .insert                       ; descending is already in order
    jmp .shift
.isless:
    cmp byte [sh_sort_desc], 0        ; values[j-1] < key: the other way round
    je .insert
.shift:
    ; DI IS THE LOOP INDEX j and sh_sort_vof RETURNS IN DI, so every use of it
    ; as a pointer here is bracketed - the origidx block below needs j back,
    ; and getting a byte offset instead is a sort that silently does nothing.
    push bx
    push di                           ; j, banked for the whole value copy
    mov bx, di
    dec bx
    call sh_sort_vof
    mov si, di                        ; SI = &values[j-1]
    pop bx                            ; BX = j (from the push di above)
    push bx
    push si
    call sh_sort_vof                  ; DI = &values[j]
    pop si
    mov ax, [es:si]
    mov [es:di], ax
    mov ax, [es:si+2]
    mov [es:di+2], ax
    mov ax, [es:si+4]
    mov [es:di+4], ax
    mov ax, [es:si+6]
    mov [es:di+6], ax
    pop di                            ; j, restored
    pop bx
    mov si, di
    dec si
    shl si, 1
    add si, SH_SORT_ORIG_OFF
    mov ax, [es:si]
    mov si, di
    shl si, 1
    add si, SH_SORT_ORIG_OFF
    mov [es:si], ax                   ; origidx[j] = origidx[j-1]
    dec di
    jmp .inner
.insert:
    push bx
    push di                           ; j, banked - same trap as .shift
    mov bx, di
    call sh_sort_vof
    mov si, sh_sort_keyval
    mov ax, [si]
    mov [es:di], ax
    mov ax, [si+2]
    mov [es:di+2], ax
    mov ax, [si+4]
    mov [es:di+4], ax
    mov ax, [si+6]
    mov [es:di+6], ax
    pop di                            ; j, restored
    pop bx
    mov si, di
    shl si, 1
    add si, SH_SORT_ORIG_OFF
    mov ax, [sh_sort_keyorig]
    mov [es:si], ax
    inc bx
    jmp .outer
.sortdone:
    xor cx, cx
.wb:
    cmp cx, [sh_sort_cnt]
    jae .wbdone
    mov es, [sh_stgseg]
    mov si, cx
    shl si, 1
    mov ax, [es:si]                   ; target_row = rows[cx] (unchanged -
    mov [sh_sort_trow], ax            ; the occupied rows themselves never
                                       ; move, only which VALUE/FORMULA
                                       ; sits in each one does)
    mov si, cx
    shl si, 1
    add si, SH_SORT_ORIG_OFF
    mov ax, [es:si]                   ; src = origidx[cx]
    mov [sh_sort_src], ax
    mov si, ax
    add si, SH_SORT_ISF_OFF
    mov al, [es:si]
    or al, al
    jz .wbplain
    mov si, [sh_sort_src]
    shl si, 1
    mov ax, [es:si]                   ; src_row = rows[src]
    mov bx, [sh_sort_trow]
    sub bx, ax                        ; row delta = target - src
    jz .wbnext                        ; unchanged position: already
                                       ; correct, nothing to rewrite
    mov [sh_cp_rowdelta], bx
    mov word [sh_cp_coldelta], 0
    mov si, [sh_sort_src]
    shl si, 1
    add si, SH_SORT_FIDX_OFF
    mov ax, [es:si]                   ; this formula's own text-slot index
    mov bx, 64
    mul bx
    add ax, SH_SORT_FTXT_OFF
    mov si, ax
    mov di, sh_rwsrc
.wbcopyin:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .wbcopyin
    push cx
    mov si, sh_rwsrc
    call sh_formula_copyshift
    mov ax, [sh_selcol]
    mov bx, [sh_sort_trow]
    mov si, sh_rwdst
    call sh_setformula
    pop cx
    jc .wbfull                        ; the arena refused: stop the write-back
    jmp .wbnext
.wbplain:
    push cx
    push bx
    mov bx, cx
    call sh_sort_vof                  ; DI = &values[cx]
    mov si, sh_acc                    ; straight into the accumulator
    mov ax, [es:di]
    mov [si], ax
    mov ax, [es:di+2]
    mov [si+2], ax
    mov ax, [es:di+4]
    mov [si+4], ax
    mov ax, [es:di+6]
    mov [si+6], ax
    pop bx
    mov ax, [sh_selcol]
    mov bx, [sh_sort_trow]
    call sh_setvald                   ; sh_setval would truncate it again
    pop cx
.wbnext:
    inc cx
    jmp .wb
.wbfull:
    mov word [sh_msg], sh_s_sortfull  ; ...and SKIP the carry: permuting the
    jmp .wbpaint                      ; other columns under a half-moved key
                                       ; would shear the table row from row
.wbdone:
    call sh_sort_carry                ; ...and bring the other columns with it
.wbpaint:
    mov si, [sh_ownwin]
    call sh_repaint
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Format dialogs (stage 1.8). Real Excel's Number/Alignment/Font dialogs
; each boil down to "pick one of a short list, then OK/Cancel" for what
; this app actually supports (Number's real dialog is a much longer
; scrollable list of format-code strings, VM_screenshots/dialog_number.png -
; Sheet only ever has 4 number formats, so a plain 4-item radio list stands
; in for it, same shape as the real Alignment and Font dialogs). All three
; are really the SAME dialog (a title, 4 radio rows, OK/Cancel) with
; different labels and a different 2-bit field of the format byte to read
; and write - see sh_fdlg_kind - which is why one implementation serves
; all three rather than three near-copies.
;
; Built directly on apps/os88ui.inc's primitives (os88ui_glyph for the
; radio dots, os88ui_btn for OK/Cancel) rather than os88ui_ask, which only
; ever offers a message and a button row - there is no generic
; "N controls" dialog builder in this codebase (apps/word/word.asm rolled
; its own, ~900 lines, for a dozen much bigger dialogs; three small
; identical-shaped ones don't need that). Only one can be open at a time
; (sh_fdlg_win is the gate, same single-instance idea as os88ui_awin, just
; simpler: this dialog doesn't need "refuse and raise" since the menu
; command that opens it can't fire again while it's up).
;
; Radio index 0-3 in each dialog is deliberately identical to that
; category's own SH_FMT_* encoding (SH_FMT_ALIGN_LEFT=1, SH_FMT_NUM_COMMA=2,
; etc, and Font's 0=Normal/1=Bold/2=Underline/3=Bold+Underline is just
; SH_FMT_BOLD|SH_FMT_UNDER's own bit pattern) - so applying a choice is a
; plain mask-and-OR, no translation table needed anywhere.
; =============================================================================
SH_FDLG_W      equ 170
SH_FDLG_ROWTOP equ 12
SH_FDLG_ROWH   equ 16
SH_FDLG_NITEMS equ 4
; THE BUTTON ROW AND THE WINDOW HEIGHT ARE ONE NUMBER, not two that have to be
; kept in agreement by hand. They were two, and they disagreed: the height was
; a flat 116 while the buttons were drawn at content-relative 86..102, and a
; window's content is only W_H - TITLE_H - 1 tall (wm_content: the origin is
; W_X+1, W_Y+TITLE_H). 116 - 18 - 1 = 97, so the bottom HALF of OK and Cancel
; was outside the window in every one of this engine's kinds - Number,
; Alignment, Font, Insert, Delete, Column Width and Row Height since stage 1.8,
; and the four stage 3.0c added. Deriving it means the next kind that needs a
; taller body cannot reintroduce this by changing one of the two.
SH_FDLG_MAXROWS equ 7                ; the tallest kind's row count, and the
                                     ; reason the button row is derived from
                                     ; it rather than fixed: the Gallery kind
                                     ; has five rows and the row at index 4
                                     ; landed ON the buttons, because 86 was
                                     ; chosen when four was the most any kind
                                     ; had. A new kind with more rows changes
                                     ; this one number.
SH_FDLG_BTY1   equ SH_FDLG_ROWTOP + SH_FDLG_MAXROWS * SH_FDLG_ROWH + 4
SH_FDLG_BTY2   equ SH_FDLG_BTY1 + 16
SH_DLG_BMARG   equ 8                 ; the gap every dialog leaves below its
                                     ; lowest element, and the reason all four
                                     ; heights below are DERIVED: a window's
                                     ; content is W_H - TITLE_H - 1 tall, so a
                                     ; hand-written height is a second number
                                     ; that has to agree with the first and
                                     ; silently did not
SH_FDLG_H      equ SH_FDLG_BTY2 + SH_DLG_BMARG + TITLE_H + 1

sh_fdlg_tpl:
    dw 0, 0, SH_FDLG_W, SH_FDLG_H
    dw 0, sh_fdlg_paint, 0, sh_fdlg_onclick

; Stage 2.x's Edit menu Insert.../Delete... reuse this same engine as kinds
; 3 and 4 - just a 2-item Row/Column pick instead of a 4-item format
; radio, and a different [sh_fdlg_count] (see sh_fdlg_open) since these
; two kinds don't have 4 rows to show. sh_fdlg_apply branches to
; sh_rowcol_op for these two kinds instead of writing a format bit.
; Kinds 5/6 (Column Width.../Row Height...) are a third borrowing: a
; 3-item preset pick instead of a per-cell format bit, applied to the
; whole sheet's runtime sh_cellw/sh_cellh (see the section comment above
; sh_entry for why these are presets rather than real Excel's free-text
; entry).
; Kinds 7-10 (stage 3.0c) are the last four radio dialogs Excel 2.1d has and
; this app was doing as immediate menu commands: Clear, New, Calculation and
; Sort. Each was a one-line "just do it" item, which is wrong twice - Excel
; asks, and asking is what lets Clear mean something other than "everything"
; and Sort mean something other than "ascending".
sh_fdlg_titles: dw sh_s_fd_num, sh_s_fd_align, sh_s_fd_font, sh_s_fd_insert, sh_s_fd_delete, sh_s_fd_colw, sh_s_fd_rowh, sh_s_fd_clear, sh_s_fd_new, sh_s_fd_calc, sh_s_fd_sort, sh_s_fd_gal, sh_s_fd_savefmt
sh_s_fd_savefmt: db 'File Format', 0
sh_s_fd_gal:    db 'Gallery', 0
sh_s_fd_clear:  db 'Clear', 0
sh_s_fd_new:    db 'New', 0
sh_s_fd_calc:   db 'Calculation', 0
sh_s_fd_sort:   db 'Sort', 0
sh_s_fd_num:    db 'Format Number', 0
sh_s_fd_align:  db 'Alignment', 0
sh_s_fd_font:   db 'Font', 0
sh_s_fd_insert: db 'Insert', 0
sh_s_fd_delete: db 'Delete', 0
sh_s_fd_colw:   db 'Column Width', 0
sh_s_fd_rowh:   db 'Row Height', 0

sh_fdlg_items:  dw sh_fd_i_num, sh_fd_i_align, sh_fd_i_font, sh_fd_i_rowcol, sh_fd_i_rowcol, sh_fd_i_colw, sh_fd_i_rowh, sh_fd_i_clear, sh_fd_i_new, sh_fd_i_calc, sh_fd_i_sort, sh_fd_i_gal, sh_fd_i_savefmt
; Excel's own words: the app's OWN format is "Normal", and the interchange
; formats are named after themselves. The order is Excel's too.
sh_fd_i_savefmt: dw sh_fd_sfnormal, sh_fd_sfsylk, sh_fd_sfdif
sh_fd_sfnormal: db 'Normal', 0
sh_fd_sfsylk:   db 'SYLK', 0
sh_fd_sfdif:    db 'DIF', 0
; Excel's own Gallery order, which is alphabetical and is NOT the order CH_T_*
; happens to be in - sh_gal_map translates, the same way chart.asm's own
; ct_gal_map does, rather than either side renumbering to suit the other.
sh_fd_i_gal:    dw sh_fd_garea, sh_fd_gbar, sh_fd_gcol, sh_fd_gline, sh_fd_gpie, sh_fd_gsca, sh_fd_gcmb
sh_fd_garea:    db 'Area', 0
sh_fd_gbar:     db 'Bar', 0
sh_fd_gcol:     db 'Column', 0
sh_fd_gline:    db 'Line', 0
sh_fd_gpie:     db 'Pie', 0
sh_fd_gsca:     db 'Scatter', 0
sh_fd_gcmb:     db 'Combination', 0
sh_gal_map:     dw CH_T_AREA, CH_T_BAR, CH_T_COLUMN, CH_T_LINE, CH_T_PIE, CH_T_SCATTER, CH_T_COMBO
sh_fd_i_clear:  dw sh_fd_clall, sh_fd_clform, sh_fd_clfmt
sh_fd_clall:    db 'All', 0
sh_fd_clform:   db 'Formulas', 0       ; Excel's own order and its own words:
sh_fd_clfmt:    db 'Formats', 0        ; "Formulas" means the CONTENTS
sh_fd_i_new:    dw sh_fd_nwsheet, sh_fd_nwchart, sh_fd_nwmacro
sh_fd_nwsheet:  db 'Worksheet', 0
sh_fd_nwchart:  db 'Chart', 0
sh_fd_nwmacro:  db 'Macro Sheet', 0
sh_fd_i_calc:   dw sh_fd_cauto, sh_fd_cmanual, sh_fd_cnow
sh_fd_cauto:    db 'Automatic', 0
sh_fd_cmanual:  db 'Manual', 0
sh_fd_cnow:     db 'Calculate Now', 0
sh_fd_i_sort:   dw sh_fd_sasc, sh_fd_sdesc
sh_fd_sasc:     db 'Ascending', 0
sh_fd_sdesc:    db 'Descending', 0
sh_fd_i_num:    dw sh_fd_numgen, sh_fd_numcur, sh_fd_numcomma, sh_fd_numpct
sh_fd_numgen:   db 'General', 0
sh_fd_numcur:   db 'Currency', 0
sh_fd_numcomma: db 'Comma', 0
sh_fd_numpct:   db 'Percent', 0
sh_fd_i_align:  dw sh_fd_agen, sh_fd_aleft, sh_fd_acenter, sh_fd_aright
sh_fd_agen:     db 'General', 0
sh_fd_aleft:    db 'Left', 0
sh_fd_acenter:  db 'Center', 0
sh_fd_aright:   db 'Right', 0
sh_fd_i_font:   dw sh_fd_fnorm, sh_fd_fbold, sh_fd_funder, sh_fd_fboth
sh_fd_fnorm:    db 'Normal', 0
sh_fd_fbold:    db 'Bold', 0
sh_fd_funder:   db 'Underline', 0
sh_fd_fboth:    db 'Bold, Underline', 0
sh_fd_i_rowcol: dw sh_fd_rcrow, sh_fd_rccol
sh_fd_rcrow:    db 'Row', 0
sh_fd_rccol:    db 'Column', 0
sh_fd_i_colw:   dw sh_fd_cwnarrow, sh_fd_cwnormal, sh_fd_cwwide
sh_fd_cwnarrow: db 'Narrow', 0
sh_fd_cwnormal: db 'Normal', 0
sh_fd_cwwide:   db 'Wide', 0
sh_fd_i_rowh:   dw sh_fd_rhshort, sh_fd_rhnormal, sh_fd_rhtall
sh_fd_rhshort:  db 'Short', 0
sh_fd_rhnormal: db 'Normal', 0
sh_fd_rhtall:   db 'Tall', 0

sh_s_fd_ok:     db 'OK', 0
sh_s_fd_cancel: db 'Cancel', 0

; per-kind row count (0 Number/1 Align/2 Font = 4 rows, 3 Insert/4 Delete
; = 2 rows, 5 Column Width/6 Row Height = 3 rows) - sh_fdlg_open copies the
; matching entry into [sh_fdlg_count], which sh_fdlg_paint/sh_fdlg_onclick
; loop and hit-test against instead of the fixed SH_FDLG_NITEMS.
sh_fdlg_counts: dw 4, 4, 4, 2, 2, 3, 3, 3, 3, 3, 2, 7, 3

SH_FDK_CLEAR equ 7
SH_FDK_NEW   equ 8
SH_FDK_CALC  equ 9
SH_FDK_SORT  equ 10
SH_FDK_GAL   equ 11
SH_FDK_SAVEFMT equ 12                 ; stage 4.6: Save As asks for the format
SH_FDK_N     equ 13                   ; instead of deriving it silently

; -----------------------------------------------------------------------------
; sh_fdlg_open - in: AL = 0 Number / 1 Alignment / 2 Font. Preselects the
; radio matching the selected cell's current format (0/General if the cell
; has no record yet - the dialog still opens; OK on a still-empty cell is a
; no-op, same scope limit the old flat menu already had).
; -----------------------------------------------------------------------------
sh_fdlg_open:
    push ax
    push bx
    push cx
    push si
    push di
    cmp word [sh_fdlg_win], 0
    jne .out                          ; already open (can't happen via the
                                       ; menu, which is inert while a dialog
                                       ; owns input focus, but stay safe)
    mov [sh_fdlg_kind], al
    mov word [sh_fdlg_sel], 0
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov cx, [sh_fdlg_counts + bx]
    mov [sh_fdlg_count], cx
    cmp al, SH_FDK_SAVEFMT
    je .prefillfmt                    ; File Format opens on the format the
    cmp al, SH_FDK_GAL                ; current NAME already implies
    je .prefillgal                    ; Gallery opens on the type in use, so
    cmp al, SH_FDK_CALC               ; OK alone cannot silently change it
    je .prefillcalc                   ; Calculation opens SHOWING the mode it
    cmp al, SH_FDK_CLEAR              ; is in, so OK alone cannot change it
    jae .noprefill                    ; Clear/New/Sort have nothing current
    cmp al, 5
    jae .prefillsize                  ; Column Width/Row Height (kinds
                                       ; 5/6): preselect from the CURRENT
                                       ; sh_cellw/sh_cellh, not a cell
    cmp al, 3
    jae .noprefill                    ; Insert/Delete (kinds 3/4): no
                                       ; "current" selection to preselect,
                                       ; just default to row 0 ("Row")
    jmp .cellpre
.prefillfmt:
    push si
    push di
    mov si, sh_name
    mov di, sh_s_ext_dif
    call sh_nameends
    pop di
    pop si
    jc .fmtdif
    push si
    push di
    mov si, sh_name
    mov di, sh_s_ext_biff
    call sh_nameends
    pop di
    pop si
    jc .fmtbiff
    mov word [sh_fdlg_sel], 1         ; anything else is SYLK, which is what
    jmp .noprefill                    ; sh_dowrite itself falls through to
.fmtdif:
    mov word [sh_fdlg_sel], 2
    jmp .noprefill
.fmtbiff:
    mov word [sh_fdlg_sel], 0
    jmp .noprefill
.prefillgal:
    xor bx, bx                        ; find [ch_type] in the map rather than
    mov cx, 7                         ; inverting it - seven entries, and an
.galpre:                              ; inverse table is one more thing to
    mov ax, [sh_gal_map + bx]         ; keep in step
    cmp ax, [ch_type]
    je .galprefound
    add bx, 2
    loop .galpre
    xor bx, bx
.galprefound:
    shr bx, 1
    mov [sh_fdlg_sel], bx
    jmp .noprefill
.prefillcalc:
    xor ah, ah
    mov al, [sh_calcmanual]
    mov [sh_fdlg_sel], ax
    jmp .noprefill
.cellpre:
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_findcell
    jnc .noprefill
    push es
    mov es, [sh_cellseg]
    mov al, [es:di+5]
    pop es
    mov ah, 0
    cmp byte [sh_fdlg_kind], 0
    je .pfnum
    cmp byte [sh_fdlg_kind], 1
    je .pfalign
    and al, 0x03                      ; Font: bits0-1 directly
    jmp .havesel
.pfnum:
    and al, SH_FMT_NUM_MASK
    mov cl, SH_FMT_NUM_SHIFT
    shr al, cl
    jmp .havesel
.pfalign:
    and al, SH_FMT_ALIGN_MASK
    mov cl, SH_FMT_ALIGN_SHIFT
    shr al, cl
.havesel:
    mov [sh_fdlg_sel], ax
    jmp .noprefill
.prefillsize:
    cmp al, 5
    jne .prefillrowh
    mov ax, [sh_cellw]
    cmp ax, SH_CW_NARROW
    jne .cwn2
    mov word [sh_fdlg_sel], 0
    jmp .noprefill
.cwn2:
    cmp ax, SH_CW_WIDE
    jne .cwn3
    mov word [sh_fdlg_sel], 2
    jmp .noprefill
.cwn3:
    mov word [sh_fdlg_sel], 1          ; Normal, or any non-preset value
    jmp .noprefill
.prefillrowh:
    mov ax, [sh_cellh]
    cmp ax, SH_RH_SHORT
    jne .rhn2
    mov word [sh_fdlg_sel], 0
    jmp .noprefill
.rhn2:
    cmp ax, SH_RH_TALL
    jne .rhn3
    mov word [sh_fdlg_sel], 2
    jmp .noprefill
.rhn3:
    mov word [sh_fdlg_sel], 1
.noprefill:
    mov bl, [sh_fdlg_kind]
    xor bh, bh
    shl bx, 1
    mov ax, [sh_fdlg_titles + bx]
    mov [sh_fdlg_tpl + WT_TITLE], ax
    call OSAPI_VIDEO                  ; centre on the LIVE screen, the same
    sub ax, SH_FDLG_W                 ; way os88ui_ask does (apps/os88ui.inc)
    sar ax, 1
    mov [sh_fdlg_tpl + WT_X], ax
    sub bx, SH_FDLG_H
    sar bx, 1
    cmp bx, MBAR_H + 8
    jge .placed
    mov bx, MBAR_H + 8                ; never under the menu bar
.placed:
    mov [sh_fdlg_tpl + WT_Y], bx
    mov si, sh_fdlg_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [sh_fdlg_win], bx
    call OSAPI_WM_SHOW
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_fdlg_paint - SI = the dialog window. Uses bss scratch (sh_fdlg_ox/oy/
; itemsptr/rowidx/rowy) rather than stack juggling to hold state across the
; os88ui_glyph/OSAPI_FONT_RUN calls, since both take CX/DX as their own
; position input - a register-only approach would need constant reshuffling
; for no real benefit here (this paints at most once per click).
; -----------------------------------------------------------------------------
sh_fdlg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si                         ; OSAPI_WM_CONTENT wants BX=window
    call OSAPI_WM_CONTENT              ; -> ax=content x, dx=content y
    mov [sh_fdlg_ox], ax
    mov [sh_fdlg_oy], dx
    mov bl, [sh_fdlg_kind]
    xor bh, bh
    shl bx, 1
    mov si, [sh_fdlg_items + bx]       ; the window's own title bar already
    mov [sh_fdlg_itemsptr], si         ; names the dialog (sh_fdlg_open set
                                        ; WT_TITLE) - no need to repeat it as
                                        ; content
    mov word [sh_fdlg_rowidx], 0
.rowloop:
    mov cx, [sh_fdlg_rowidx]
    cmp cx, [sh_fdlg_count]
    jae .rowsdone
    mov ax, cx
    mov bx, SH_FDLG_ROWH
    mul bx
    add ax, SH_FDLG_ROWTOP
    add ax, [sh_fdlg_oy]
    mov [sh_fdlg_rowy], ax
    mov ax, [sh_fdlg_sel]
    cmp ax, [sh_fdlg_rowidx]
    mov al, OS88UI_GRADIO
    jne .goff
    or al, OS88UI_GON
.goff:
    mov ah, 0
    mov cx, [sh_fdlg_ox]
    add cx, 8
    mov dx, [sh_fdlg_rowy]
    call os88ui_glyph                  ; preserves all registers (its own doc)
    mov si, [sh_fdlg_itemsptr]
    mov bx, [sh_fdlg_rowidx]
    shl bx, 1
    add si, bx
    mov si, [si]                       ; si = this row's label string
    mov cx, [sh_fdlg_ox]
    add cx, 24
    mov dx, [sh_fdlg_rowy]
    add dx, 2
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    mov ax, [sh_fdlg_rowidx]
    inc ax
    mov [sh_fdlg_rowidx], ax
    jmp .rowloop
.rowsdone:
    mov ax, [sh_fdlg_ox]
    add ax, 8
    mov [sh_fdlg_rect], ax
    mov ax, [sh_fdlg_oy]
    add ax, SH_FDLG_BTY1
    mov [sh_fdlg_rect+2], ax
    mov ax, [sh_fdlg_ox]
    add ax, 62
    mov [sh_fdlg_rect+4], ax
    mov ax, [sh_fdlg_oy]
    add ax, SH_FDLG_BTY2
    mov [sh_fdlg_rect+6], ax
    mov bx, sh_fdlg_rect
    mov si, sh_s_fd_ok
    mov di, OS88UI_DEF
    call os88ui_btn
    mov ax, [sh_fdlg_ox]
    add ax, 96
    mov [sh_fdlg_rect], ax
    mov ax, [sh_fdlg_oy]
    add ax, SH_FDLG_BTY1
    mov [sh_fdlg_rect+2], ax
    mov ax, [sh_fdlg_ox]
    add ax, 150
    mov [sh_fdlg_rect+4], ax
    mov ax, [sh_fdlg_oy]
    add ax, SH_FDLG_BTY2
    mov [sh_fdlg_rect+6], ax
    mov bx, sh_fdlg_rect
    mov si, sh_s_fd_cancel
    xor di, di
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_fdlg_onclick - in: CX=x, DX=y (screen-absolute, same convention as
; sh_onclick), SI=the dialog window
; -----------------------------------------------------------------------------
sh_fdlg_onclick:
    push ax
    push bx
    push si
    push di
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT              ; -> ax=content x, dx=content y
    pop bx
    sub bx, dx                         ; bx = click y, content-relative
    pop cx
    sub cx, ax                         ; cx = click x, content-relative
    cmp cx, 8
    jb .checkcancel
    cmp cx, 62
    ja .checkcancel
    cmp bx, SH_FDLG_BTY1
    jb .checkcancel
    cmp bx, SH_FDLG_BTY2
    ja .checkcancel
    jmp .doOK
.checkcancel:
    cmp cx, 96
    jb .checkrows
    cmp cx, 150
    ja .checkrows
    cmp bx, SH_FDLG_BTY1
    jb .checkrows
    cmp bx, SH_FDLG_BTY2
    ja .checkrows
    jmp .doCancel
.checkrows:
    cmp cx, 8
    jb .out
    cmp bx, SH_FDLG_ROWTOP
    jb .out
    mov ax, bx
    sub ax, SH_FDLG_ROWTOP
    xor dx, dx
    mov si, SH_FDLG_ROWH
    div si                             ; ax = row index
    cmp ax, [sh_fdlg_count]
    jae .out
    mov [sh_fdlg_sel], ax
    mov si, [sh_fdlg_win]
    call sh_fdlg_paint
    jmp .out
.doOK:
    call sh_fdlg_apply
    call sh_fdlg_close
    cmp byte [sh_savepend], 0         ; File Format's OK owes a Save As, and it
    je .out                           ; runs only now that the format dialog's
    mov byte [sh_savepend], 0         ; window is DESTROYED. Opening the file
    mov si, [sh_ownwin]               ; dialog from inside apply would stack a
    mov al, FDLG_SAVE                 ; second dialog on a window slot that is
    call sh_dlg                       ; still in use, which is how one gets
    jmp .out                          ; orphaned behind the other
.doCancel:
    call sh_fdlg_close
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_setext - in: SI -> a 4-byte ".XXX". Replaces [sh_name]'s extension, or
; appends one if it has none, so picking a format in the Save As dialog
; renames BUDGET.SLK to BUDGET.BIF rather than leaving the name disagreeing
; with the bytes. sh_dowrite still decides by extension - this makes the
; extension follow the CHOICE instead of the other way round.
;
; sh_name is 13 bytes and holds an 8.3 name, so the worst case (an 8-char
; stem with no dot) writes 8+4+1 = 13. Nothing longer can arrive: the file
; dialog is what fills this buffer and it enforces 8.3.
; -----------------------------------------------------------------------------
sh_setext:
    push ax
    push cx
    push si
    push di
    mov di, sh_name
    xor cx, cx                        ; cx = where the '.' is, 0 = none yet
.scan:
    mov al, [di]
    or al, al
    jz .atend
    cmp al, '.'
    jne .next
    mov cx, di
.next:
    inc di
    jmp .scan
.atend:
    or cx, cx
    jz .append                        ; no extension: write one on the end
    mov di, cx                        ; there is one: overwrite from the '.'
.append:
    mov cx, 4
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_clear_one - Clear's work for ONE cell, by [sh_fdlg_sel]: 0 All /
; 1 Formulas / 2 Formats. "Formulas" is Excel's word for the CONTENTS - a cell
; cleared that way keeps its border, its number format and its font, which is
; the whole reason the dialog exists. in: AX = col, BX = row. Preserves all.
; -----------------------------------------------------------------------------
sh_clear_one:
    push ax
    push bx
    push di
    push es
    cmp word [sh_fdlg_sel], 2
    je .fmt
    cmp word [sh_fdlg_sel], 1
    je .contents
    call sh_clearcell                 ; All: the record goes, border and all
    call sh_bt_removecell             ; (sh_clearcell preserves AX/BX)
    jmp .out
.contents:
    call sh_findcell
    jnc .out
    mov es, [sh_cellseg]
    mov byte [es:di+4], 0             ; not a formula any more...
    mov byte [es:di+SH_C_TYPE], SH_T_NUM
    mov word [es:di+SH_C_VAL], 0      ; ...and zero, but the format byte at
    mov word [es:di+SH_C_VAL+2], 0    ; +5 is deliberately untouched
    mov word [es:di+SH_C_VAL+4], 0
    mov word [es:di+SH_C_VAL+6], 0
    mov word [es:di+SH_C_PASS], 0
    jmp .out
.fmt:
    call sh_findcell
    jnc .out
    mov es, [sh_cellseg]
    mov byte [es:di+5], 0             ; Formats: only the format byte, and the
    call sh_bt_removecell             ; border table entry beside it
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_fmt_one - apply the format dialog's current choice to ONE cell.
; in: AX = col, BX = row. An empty cell has no record to carry a format and is
; skipped, which is what the single-cell version did too. Preserves all.
; -----------------------------------------------------------------------------
sh_fmt_one:
    push ax
    push bx
    push cx
    push di
    push es
    call sh_findcell
    jnc .out
    mov es, [sh_cellseg]
    mov bl, [es:di+5]
    mov al, [sh_fdlg_sel]
    cmp byte [sh_fdlg_kind], 0
    je .num
    cmp byte [sh_fdlg_kind], 1
    je .align
    and bl, SH_FMT_BU_CLR              ; Font: bits0-1 directly
    or bl, al
    jmp .put
.num:
    and bl, SH_FMT_NUM_CLR
    mov cl, SH_FMT_NUM_SHIFT
    shl al, cl
    or bl, al
    jmp .put
.align:
    and bl, SH_FMT_ALIGN_CLR
    mov cl, SH_FMT_ALIGN_SHIFT
    shl al, cl
    or bl, al
.put:
    mov [es:di+5], bl
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_fdlg_apply - kinds 0-2 (Number/Alignment/Font): write [sh_fdlg_sel]
; into the selected cell's format byte, in the field [sh_fdlg_kind] names -
; a no-op if the cell has no record (see sh_fdlg_open's own comment on
; that scope limit). Kinds 3-4 (Insert/Delete): [sh_fdlg_sel] is 0 Row / 1
; Column, so hand off to sh_rowcol_op with the selected cell's own row or
; column as the pivot index - these have no "cell must have a record"
; limit, since they act on the grid's structure, not a cell's content.
; -----------------------------------------------------------------------------
sh_fdlg_apply:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp byte [sh_fdlg_kind], SH_FDK_CLEAR
    je .doclear
    cmp byte [sh_fdlg_kind], SH_FDK_NEW
    je .donew
    cmp byte [sh_fdlg_kind], SH_FDK_CALC
    je .docalc
    cmp byte [sh_fdlg_kind], SH_FDK_SORT
    je .dosort
    cmp byte [sh_fdlg_kind], SH_FDK_GAL
    je .dogallery
    cmp byte [sh_fdlg_kind], SH_FDK_SAVEFMT
    je .dosavefmt
    cmp byte [sh_fdlg_kind], 5
    je .colwidth
    cmp byte [sh_fdlg_kind], 6
    je .rowheight
    cmp byte [sh_fdlg_kind], 3
    je .insertrc
    cmp byte [sh_fdlg_kind], 4
    je .deleterc
    ; Number/Alignment/Font apply to the WHOLE SELECTION. They used to read
    ; sh_selcol/sh_selrow and format the anchor alone, so selecting a column
    ; of figures and choosing Currency changed exactly one cell - the same
    ; thing Fill Right/Down did before 81.13, and for the same reason: written
    ; before range selection existed and never taught about sh_selcol2/
    ; sh_selrow2. A single-cell selection is a 1x1 range, so it still works.
    mov ax, [sh_selrow]
    mov bx, [sh_selrow2]
    cmp ax, bx
    jbe .fmtrows
    xchg ax, bx
.fmtrows:                              ; ax = top row, bx = bottom row
    mov cx, [sh_selcol]
    mov si, [sh_selcol2]
    cmp cx, si
    jbe .fmtcols
    xchg cx, si
.fmtcols:                              ; cx = first col, si = last col
.fmtcolloop:
    mov di, ax                         ; di = the current row, from the top
.fmtrowloop:
    push ax
    push bx
    mov ax, cx
    mov bx, di
    call sh_fmt_one
    pop bx
    pop ax
    inc di
    cmp di, bx
    jbe .fmtrowloop
    inc cx
    cmp cx, si
    jbe .fmtcolloop
    call sh_repaint                    ; ONE repaint for the whole block
    jmp .out
.colwidth:
    mov ax, [sh_fdlg_sel]
    or ax, ax
    jnz .cwnotnarrow
    mov word [sh_cellw], SH_CW_NARROW
    jmp .cwdone
.cwnotnarrow:
    cmp ax, 2
    jne .cwnormal
    mov word [sh_cellw], SH_CW_WIDE
    jmp .cwdone
.cwnormal:
    mov word [sh_cellw], SH_CW_NORMAL
.cwdone:
    mov ax, [sh_cellw]
    mov cl, 3
    shr ax, cl
    mov [sh_cellch], ax
    call sh_mkblank
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out
.rowheight:
    mov ax, [sh_fdlg_sel]
    or ax, ax
    jnz .rhnotshort
    mov word [sh_cellh], SH_RH_SHORT
    jmp .rhdone
.rhnotshort:
    cmp ax, 2
    jne .rhnormal
    mov word [sh_cellh], SH_RH_TALL
    jmp .rhdone
.rhnormal:
    mov word [sh_cellh], SH_RH_NORMAL
.rhdone:
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out
.insertrc:
    cmp word [sh_fdlg_sel], 0
    jne .inscol
    mov al, 0                          ; op 0 = insert row
    mov bx, [sh_selrow]
    jmp .rcgo
.inscol:
    mov al, 2                          ; op 2 = insert column
    mov bx, [sh_selcol]
    jmp .rcgo
.deleterc:
    cmp word [sh_fdlg_sel], 0
    jne .delcol
    mov al, 1                          ; op 1 = delete row
    mov bx, [sh_selrow]
    jmp .rcgo
.delcol:
    mov al, 3                          ; op 3 = delete column
    mov bx, [sh_selcol]
.rcgo:
    call sh_rowcol_op
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out

; --- stage 3.0c: the four that used to be immediate menu commands -----------
.doclear:
    ; Over the WHOLE SELECTION, like Excel's Clear and like the block the user
    ; has highlighted. It used to clear the anchor alone (81.17's third case,
    ; after Fill and the format dialogs).
    mov ax, [sh_selrow]
    mov bx, [sh_selrow2]
    cmp ax, bx
    jbe .clrows
    xchg ax, bx
.clrows:                              ; ax = top row, bx = bottom row
    mov cx, [sh_selcol]
    mov si, [sh_selcol2]
    cmp cx, si
    jbe .clcols
    xchg cx, si
.clcols:                              ; cx = first col, si = last col
.clcolloop:
    mov di, ax
.clrowloop:
    push ax
    push bx
    mov ax, cx
    mov bx, di
    call sh_clear_one
    pop bx
    pop ax
    inc di
    cmp di, bx
    jbe .clrowloop
    inc cx
    cmp cx, si
    jbe .clcolloop
.cldone:
    mov si, [sh_ownwin]               ; ONE repaint for the block
    call sh_repaint
    jmp .out

.donew:
    ; Excel asks which KIND of new document. This app has one grid type, so
    ; Chart and Macro Sheet do the honest thing rather than the flattering
    ; one: a new sheet, and a status line saying what was actually made.
    call sh_new
    mov word [sh_msg], sh_s_nw_sheet
    cmp word [sh_fdlg_sel], 1
    jne .nwnotchart
    mov word [sh_msg], sh_s_nw_chart
.nwnotchart:
    cmp word [sh_fdlg_sel], 2
    jne .nwdone
    mov word [sh_msg], sh_s_nw_macro
.nwdone:
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out

.docalc:
    ; 0 Automatic / 1 Manual / 2 Calculate Now. Manual is not a no-op with a
    ; label on it: sh_drawgrid re-evaluates every formula cell on every
    ; repaint, so switching it off is what a big sheet on a 4.77MHz 8088
    ; actually needs, and Calculate Now is then the only way to catch up.
    cmp word [sh_fdlg_sel], 2
    je .calcnow
    mov ax, [sh_fdlg_sel]
    mov [sh_calcmanual], al
    mov word [sh_msg], sh_s_calc_auto
    or al, al
    jz .calcrepaint
    mov word [sh_msg], sh_s_calc_man
    jmp .calcrepaint
.calcnow:
    inc word [sh_pass]                ; a pass stamp nothing has cached, which
    mov word [sh_msg], sh_s_calc_now  ; is exactly what forces the recompute
.calcrepaint:
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out

.dosort:
    mov ax, [sh_fdlg_sel]
    mov [sh_sort_desc], al
    call sh_docmd_sortcol
    jmp .out
.dogallery:
    mov bx, [sh_fdlg_sel]
    shl bx, 1
    mov ax, [sh_gal_map + bx]
    mov [ch_type], ax
    cmp word [sh_chartwin], 0         ; no chart window yet: the type is still
    je .galdone                       ; remembered, and the next Chart Column
    call sh_chart_render              ; uses it
    mov si, [sh_chartwin]
    call sh_chart_paint
.galdone:
    mov si, [sh_ownwin]
    call sh_repaint
    jmp .out
.dosavefmt:
    mov si, sh_s_ext_biff             ; 0 Normal is this app's OWN format,
    mov ax, [sh_fdlg_sel]             ; which is BIFF - the same thing
    or ax, ax                         ; Excel means by "Normal"
    jz .fmtset
    mov si, sh_s_ext_sylk
    cmp ax, 1
    je .fmtset
    mov si, sh_s_ext_dif
.fmtset:
    call sh_setext
    mov byte [sh_savepend], 1         ; the file dialog cannot open until
    jmp .out                          ; THIS one is destroyed - see .doOK

.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_fdlg_close
; -----------------------------------------------------------------------------
sh_fdlg_close:
    push ax
    push bx
    mov bx, [sh_fdlg_win]
    or bx, bx
    jz .out
    mov word [sh_fdlg_win], 0
    call OSAPI_WM_DESTROY               ; NOT OSAPI_WM_CLOSE. Close means
                                        ; "quit the instance owning this
                                        ; window" (app_close_win); a dialog
                                        ; has no owning instance, so that path
                                        ; falls through to a plain wm_hide -
                                        ; the pixels go but THE SLOT STAYS
                                        ; USED. MAX_WIN is 12, so ten dialogs
                                        ; into a session no dialog would open
                                        ; again, in this app or any other.
                                        ; os88api.inc names this exact case:
                                        ; the unowned species is "a driver's
                                        ; windows, and a package's second one".
                                        ; The gfx lock is already held - every
                                        ; callback holds it - which is what
                                        ; DESTROY wants (os88ui_adone does the
                                        ; same, gate first then destroy).
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Border dialog (stage 2.x). Real Excel 2.1's Format > Border... is a
; "Border" GROUP BOX holding six independent CHECKBOXES (Outline/Left/
; Right/Top/Bottom/Shade) with OK/Cancel standing beside it, not below it
; (VM_screenshots/dialog_border.png) - a materially different shape from
; Number/Alignment/Font's single-choice radio lists, so it gets its own
; small engine rather than being forced into sh_fdlg_*'s. "Outline" is
; UI-only: checking it sets all four edges at once and unchecking it clears
; all four, matching real Excel's own behavior - there is no stored
; "outline" bit separate from the four edges themselves, so re-opening the
; dialog on a cell that has all four set shows Outline checked too, purely
; because sh_bdlg_open recomputes it from them.
; =============================================================================
SH_BDLG_W      equ 190
SH_BDLG_GX1    equ 10                ; the "Border" group box, inset from
SH_BDLG_GY1    equ 12                ; the dialog's own content origin
SH_BDLG_GX2    equ 104
SH_BDLG_GY2    equ 132                ; the LOWEST element here, so the height
SH_BDLG_H      equ SH_BDLG_GY2 + SH_DLG_BMARG + TITLE_H + 1
                                     ; comes from it. At a flat 150 the content
                                     ; was 131 tall and this line sat at 132 -
                                     ; the group box's bottom edge was one
                                     ; pixel outside the window
SH_BDLG_ROWTOP equ 26                ; first checkbox row, and OK/Cancel
SH_BDLG_ROWH   equ 18                ; both measured from the SAME origin
SH_BDLG_NITEMS equ 6

SH_BDLG_B_OUTLINE equ 0x01           ; the dialog's own 6-bit UI state -
SH_BDLG_B_LEFT    equ 0x02           ; bits 1-4 line up with SH_BORD_LEFT..
SH_BDLG_B_RIGHT   equ 0x04           ; SH_BORD_BOTTOM shifted up by one (to
SH_BDLG_B_TOP     equ 0x08           ; make room for Outline at bit 0) and
SH_BDLG_B_BOTTOM  equ 0x10           ; bit 5 lines up with SH_BORD_SHADE the
SH_BDLG_B_SHADE   equ 0x20           ; same way - see sh_bdlg_open/_apply

sh_bdlg_tpl:
    dw 0, 0, SH_BDLG_W, SH_BDLG_H
    dw sh_s_bdlg_title, sh_bdlg_paint, 0, sh_bdlg_onclick

sh_s_bdlg_title: db 'Border', 0
sh_bdlg_items: dw sh_bdlg_i0, sh_bdlg_i1, sh_bdlg_i2, sh_bdlg_i3, sh_bdlg_i4, sh_bdlg_i5
sh_bdlg_i0:    db 'Outline', 0
sh_bdlg_i1:    db 'Left', 0
sh_bdlg_i2:    db 'Right', 0
sh_bdlg_i3:    db 'Top', 0
sh_bdlg_i4:    db 'Bottom', 0
sh_bdlg_i5:    db 'Shade', 0

; -----------------------------------------------------------------------------
; sh_bdlg_open - preselect from the selected cell's stored border byte
; (sh_bt_get); a cell with no border record at all reads back as 0, same
; "dialog still opens, OK on it is just a no-op" scope as sh_fdlg_open's.
; -----------------------------------------------------------------------------
sh_bdlg_open:
    push ax
    push bx
    push si
    cmp word [sh_bdlg_win], 0
    jne .out
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_bt_get                     ; al = stored border byte
    mov ah, al
    and ah, 0x1F
    mov bl, ah
    shl bl, 1                          ; bl = sel bits 1-5 (L,R,T,Bot,Shade)
    and ah, SH_BORD_EDGES
    cmp ah, SH_BORD_EDGES
    jne .noout
    or bl, SH_BDLG_B_OUTLINE
.noout:
    mov [sh_bdlg_sel], bl
    call OSAPI_VIDEO
    sub ax, SH_BDLG_W
    sar ax, 1
    mov [sh_bdlg_tpl + WT_X], ax
    sub bx, SH_BDLG_H
    sar bx, 1
    cmp bx, MBAR_H + 8
    jge .placed
    mov bx, MBAR_H + 8
.placed:
    mov [sh_bdlg_tpl + WT_Y], bx
    mov si, sh_bdlg_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [sh_bdlg_win], bx
    call OSAPI_WM_SHOW
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_bdlg_paint - SI = the dialog window
; -----------------------------------------------------------------------------
sh_bdlg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [sh_bdlg_ox], ax
    mov [sh_bdlg_oy], dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_bdlg_ox]
    add ax, SH_BDLG_GX1
    mov bx, [sh_bdlg_oy]
    add bx, SH_BDLG_GY1
    mov cx, [sh_bdlg_ox]
    add cx, SH_BDLG_GX2
    mov dx, [sh_bdlg_oy]
    add dx, SH_BDLG_GY2
    call OSAPI_GFX_FRAME                ; the group box itself
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sh_bdlg_ox]
    add ax, SH_BDLG_GX1 + 6
    mov bx, [sh_bdlg_oy]
    add bx, SH_BDLG_GY1 - 3
    mov cx, ax
    add cx, 40
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL                 ; erase the frame line behind the
                                         ; label, so it "breaks" the box top
                                         ; the way a real GUI group box does
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [sh_bdlg_ox]
    add cx, SH_BDLG_GX1 + 8
    mov dx, [sh_bdlg_oy]
    add dx, SH_BDLG_GY1 - 4
    mov si, sh_s_bdlg_title
    call OSAPI_FONT_STR_XPARENT
    mov word [sh_bdlg_ri], 0
.rowloop:
    mov ax, [sh_bdlg_ri]
    cmp ax, SH_BDLG_NITEMS
    jae .rowsdone
    mov bx, SH_BDLG_ROWH
    mul bx
    add ax, SH_BDLG_ROWTOP
    add ax, [sh_bdlg_oy]
    mov [sh_bdlg_ry], ax
    mov al, OS88UI_GCHECK
    mov bh, 1
    mov cl, byte [sh_bdlg_ri]
    shl bh, cl
    test bh, [sh_bdlg_sel]
    jz .off
    or al, OS88UI_GON
.off:
    mov ah, 0
    mov cx, [sh_bdlg_ox]
    add cx, SH_BDLG_GX1 + 8
    mov dx, [sh_bdlg_ry]
    call os88ui_glyph
    mov bx, [sh_bdlg_ri]
    shl bx, 1
    mov si, [sh_bdlg_items + bx]
    mov cx, [sh_bdlg_ox]
    add cx, SH_BDLG_GX1 + 24
    mov dx, [sh_bdlg_ry]
    add dx, 2
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    mov ax, [sh_bdlg_ri]
    inc ax
    mov [sh_bdlg_ri], ax
    jmp .rowloop
.rowsdone:
    mov ax, [sh_bdlg_ox]
    add ax, SH_BDLG_GX2 + 10
    mov [sh_bdlg_rect], ax
    mov ax, [sh_bdlg_oy]
    add ax, 20
    mov [sh_bdlg_rect+2], ax
    mov ax, [sh_bdlg_ox]
    add ax, SH_BDLG_W - 10
    mov [sh_bdlg_rect+4], ax
    mov ax, [sh_bdlg_oy]
    add ax, 40
    mov [sh_bdlg_rect+6], ax
    mov bx, sh_bdlg_rect
    mov si, sh_s_fd_ok
    mov di, OS88UI_DEF
    call os88ui_btn
    mov ax, [sh_bdlg_oy]
    add ax, 50
    mov [sh_bdlg_rect+2], ax
    mov ax, [sh_bdlg_oy]
    add ax, 70
    mov [sh_bdlg_rect+6], ax
    mov bx, sh_bdlg_rect
    mov si, sh_s_fd_cancel
    xor di, di
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_bdlg_onclick - in: CX=x, DX=y (screen-absolute), SI=the dialog window
; -----------------------------------------------------------------------------
sh_bdlg_onclick:
    push ax
    push bx
    push si
    push di
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT
    pop bx
    sub bx, dx                          ; bx = click y, content-relative
    pop cx
    sub cx, ax                          ; cx = click x, content-relative
    cmp cx, SH_BDLG_GX2 + 10
    jb .checkrows
    cmp cx, SH_BDLG_W - 10
    ja .checkrows
    cmp bx, 20
    jb .checkrows
    cmp bx, 40
    jle .doOK
    cmp bx, 50
    jb .checkrows
    cmp bx, 70
    jle .doCancel
.checkrows:
    cmp cx, SH_BDLG_GX1 + 8
    jb .out
    cmp bx, SH_BDLG_ROWTOP
    jb .out
    mov ax, bx
    sub ax, SH_BDLG_ROWTOP
    xor dx, dx
    mov si, SH_BDLG_ROWH
    div si
    cmp ax, SH_BDLG_NITEMS
    jae .out
    mov cl, al
    mov bh, 1
    shl bh, cl
    xor [sh_bdlg_sel], bh
    cmp al, 0
    je .wasoutline
    mov al, [sh_bdlg_sel]
    and al, 0x1E
    cmp al, 0x1E
    jne .clroutline
    or byte [sh_bdlg_sel], SH_BDLG_B_OUTLINE
    jmp .redraw
.clroutline:
    and byte [sh_bdlg_sel], ~SH_BDLG_B_OUTLINE & 0xFF
    jmp .redraw
.wasoutline:
    test byte [sh_bdlg_sel], SH_BDLG_B_OUTLINE
    jz .outoff
    or byte [sh_bdlg_sel], 0x1E
    jmp .redraw
.outoff:
    and byte [sh_bdlg_sel], ~0x1E & 0xFF
.redraw:
    mov si, [sh_bdlg_win]
    call sh_bdlg_paint
    jmp .out
.doOK:
    call sh_bdlg_apply
    call sh_bdlg_close
    jmp .out
.doCancel:
    call sh_bdlg_close
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_bdlg_apply - write sh_bdlg_sel's edges/shade (bits 1-5) into the
; border table: a record if any bit is set, no record (removed if one
; existed) if the cell ends up with no border at all.
; -----------------------------------------------------------------------------
sh_bdlg_apply:
    push ax
    push bx
    push dx
    mov al, [sh_bdlg_sel]
    shr al, 1
    and al, 0x1F
    mov dl, al
    or dl, dl
    jz .clearit
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_bt_addcell
    jc .out                             ; table full: silent no-op, same
                                         ; scope limit as the main array's
    push es
    mov es, [sh_bordseg]
    mov [es:di+4], dl
    pop es
    jmp .out
.clearit:
    mov ax, [sh_selcol]
    mov bx, [sh_selrow]
    call sh_bt_removecell
.out:
    mov si, [sh_ownwin]
    call sh_repaint
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_bdlg_close
; -----------------------------------------------------------------------------
sh_bdlg_close:
    push ax
    push bx
    mov bx, [sh_bdlg_win]
    or bx, bx
    jz .out
    mov word [sh_bdlg_win], 0
    call OSAPI_WM_DESTROY               ; see sh_fdlg_close on why not CLOSE
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; The ONE-LINE INPUT DIALOG (stage 3.0c) - a prompt, an os88line field, OK and
; Cancel. Four menu items want exactly this and differ only in their prompt and
; in what OK does with the string, so it is written once with a KIND byte and
; a dispatch on it, the same way sh_fdlg_* already serves five radio-list
; kinds rather than being copied five times.
;
; This is what the text widget was for. Row Height... and Column Width... have
; been a THREE-PRESET RADIO PICK since stage 1.8 purely because no free-text
; entry existed at the app level - sh_m_format's own comment says so. They are
; now real numeric entry, which is what Excel 2.1d has.
; =============================================================================
SH_ID_GOTO   equ 0                   ; Formula > Goto...
SH_ID_ROWH   equ 1                   ; Format > Row Height...
SH_ID_COLW   equ 2                   ; Format > Column Width...
SH_ID_DEFN   equ 3                   ; Formula > Define Name...
SH_ID_FIND   equ 4                   ; Formula > Find...
SH_ID_NKIND  equ 5

SH_IDLG_W    equ 268
SH_IDLG_FX1  equ 8                   ; the field, content-relative
SH_IDLG_FY1  equ 28
SH_IDLG_FX2  equ 176
SH_IDLG_FY2  equ 46
SH_IDLG_BTX1 equ 186                 ; OK / Cancel, 64 wide - 'Cancel' needs
SH_IDLG_BTX2 equ 250                 ; 6 glyphs at the fixed 8px cell
SH_IDLG_OKY1 equ 26
SH_IDLG_OKY2 equ 46
SH_IDLG_CAY1 equ 54
SH_IDLG_CAY2 equ 74
SH_IDLG_H    equ SH_IDLG_CAY2 + SH_DLG_BMARG + TITLE_H + 1

sh_idlg_tpl:
    dw 0, 0, SH_IDLG_W, SH_IDLG_H
    dw sh_s_id_tgoto, sh_idlg_paint, sh_idlg_onkey, sh_idlg_onclick
; The title above is only a PLACEHOLDER: sh_idlg_open overwrites
; [sh_idlg_tpl + WT_TITLE] with whichever of sh_s_id_t* the kind names, before
; OSAPI_WM_CREATE. WT_TITLE is a pointer TO the text, so the pointer has to go
; into the template itself - putting it in a cell and pointing the template at
; that cell makes the kernel letter the pointer's own two bytes and then run on
; into whatever follows, which is exactly what it did.
sh_id_titles:  dw sh_s_id_tgoto, sh_s_id_trowh, sh_s_id_tcolw, sh_s_id_tdefn, sh_s_id_tfind
sh_id_prompts: dw sh_s_id_pgoto, sh_s_id_prowh, sh_s_id_pcolw, sh_s_id_pdefn, sh_s_id_pfind
sh_s_id_tgoto: db 'Goto', 0
sh_s_id_trowh: db 'Row Height', 0
sh_s_id_tcolw: db 'Column Width', 0
sh_s_id_tdefn: db 'Define Name', 0
sh_s_id_tfind: db 'Find', 0
sh_s_id_pgoto: db 'Reference:', 0
sh_s_id_prowh: db 'Row height:', 0
sh_s_id_pcolw: db 'Column width:', 0
sh_s_id_pdefn: db 'Name:', 0
sh_s_id_pfind: db 'Find what:', 0
sh_s_id_nofit: db 'Name table full.', 0
sh_s_id_named: db 'Name defined.', 0
sh_s_id_nofnd: db 'Not found.', 0
sh_s_idlg_ok:  db 'OK', 0
sh_s_idlg_can: db 'Cancel', 0

; -----------------------------------------------------------------------------
; sh_idlg_open - in: AL = SH_ID_*. Preloads the field with the CURRENT value
; (the selection's reference, or the live row height / column width) so the
; dialog opens showing what it is about to change, and Enter alone is a no-op
; rather than a surprise.
; -----------------------------------------------------------------------------
sh_idlg_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [sh_idlg_win], 0
    jne .out
    cmp al, SH_ID_NKIND
    jae .out
    mov [sh_idlg_kind], al
    xor ah, ah
    mov bx, ax
    shl bx, 1                          ; word index into the two tables
    mov ax, [sh_id_titles + bx]
    mov [sh_idlg_tpl + WT_TITLE], ax
    mov byte [sh_idlg_buf], 0
    cmp byte [sh_idlg_kind], SH_ID_DEFN
    jae .prenone                       ; Define Name and Find open EMPTY: there
    cmp byte [sh_idlg_kind], SH_ID_GOTO ; is no current value for either, and
    je .pregoto                        ; prefilling one would be a wrong guess
    cmp byte [sh_idlg_kind], SH_ID_ROWH
    je .prerowh
    mov ax, [sh_cellch]                ; characters, matching what OK reads
    jmp .prenum
.prerowh:
    mov ax, [sh_cellh]
.prenum:
    call sh_itoa
    mov di, sh_idlg_buf
    mov si, sh_numbuf
    call sh_strcpy_to_di
    jmp .haveinit
.pregoto:
    mov di, sh_idlg_buf                ; the selection, as 'A1'
    mov ax, [sh_selcol]
    call sh_colname
    mov si, sh_colbuf
    call sh_strcpy_to_di
    mov ax, [sh_selrow]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_strcpy_to_di
.prenone:
.haveinit:
    mov si, sh_idlg_line
    mov word [si + LN_BUF], sh_idlg_buf
    mov word [si + LN_MAX], SH_EDITMAX
    mov byte [si + LN_FOCUS], 1
    mov di, sh_idlg_buf
    call os88line_set                  ; sets LEN/CAR/VIEW from the content
    call OSAPI_VIDEO
    sub ax, SH_IDLG_W
    sar ax, 1
    mov [sh_idlg_tpl + WT_X], ax
    sub bx, SH_IDLG_H
    sar bx, 1
    cmp bx, MBAR_H + 8
    jge .placed
    mov bx, MBAR_H + 8
.placed:
    mov [sh_idlg_tpl + WT_Y], bx
    mov si, sh_idlg_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [sh_idlg_win], bx
    call OSAPI_WM_SHOW
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_idlg_paint - SI = the dialog window
; -----------------------------------------------------------------------------
sh_idlg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [sh_idlg_ox], ax
    mov [sh_idlg_oy], dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov bl, [sh_idlg_kind]             ; the prompt for this kind
    xor bh, bh
    shl bx, 1
    mov si, [sh_id_prompts + bx]
    mov cx, [sh_idlg_ox]
    add cx, SH_IDLG_FX1
    mov dx, [sh_idlg_oy]
    add dx, 8
    call OSAPI_FONT_STR_XPARENT

    mov si, sh_idlg_line               ; the field's rect from the LIVE origin
    mov ax, [sh_idlg_ox]               ; every paint - the window moves
    add ax, SH_IDLG_FX1
    mov [si + LN_X1], ax
    mov ax, [sh_idlg_ox]
    add ax, SH_IDLG_FX2
    mov [si + LN_X2], ax
    mov ax, [sh_idlg_oy]
    add ax, SH_IDLG_FY1
    mov [si + LN_Y1], ax
    mov ax, [sh_idlg_oy]
    add ax, SH_IDLG_FY2
    mov [si + LN_Y2], ax
    call os88line_draw

    mov ax, [sh_idlg_ox]               ; OK
    add ax, SH_IDLG_BTX1
    mov [sh_idlg_rect], ax
    mov ax, [sh_idlg_oy]
    add ax, SH_IDLG_OKY1
    mov [sh_idlg_rect+2], ax
    mov ax, [sh_idlg_ox]
    add ax, SH_IDLG_BTX2
    mov [sh_idlg_rect+4], ax
    mov ax, [sh_idlg_oy]
    add ax, SH_IDLG_OKY2
    mov [sh_idlg_rect+6], ax
    mov bx, sh_idlg_rect
    mov si, sh_s_idlg_ok
    mov di, OS88UI_DEF
    call os88ui_btn
    mov ax, [sh_idlg_oy]               ; Cancel - same x, two new y's
    add ax, SH_IDLG_CAY1
    mov [sh_idlg_rect+2], ax
    mov ax, [sh_idlg_oy]
    add ax, SH_IDLG_CAY2
    mov [sh_idlg_rect+6], ax
    mov bx, sh_idlg_rect
    mov si, sh_s_idlg_can
    xor di, di
    call os88ui_btn

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_idlg_onkey - Enter is OK and Escape is Cancel, which is what a one-field
; dialog should do; os88line_key deliberately does NOT consume Enter (its own
; header says so) precisely so the caller can use it for this.
; -----------------------------------------------------------------------------
sh_idlg_onkey:
    push ax
    push si
    cmp al, 27
    je .cancel
    cmp al, 0x0D
    je .accept
    mov si, sh_idlg_line
    call os88line_key
    jc .out
    mov si, [sh_idlg_win]
    call sh_idlg_paint
    jmp .out
.accept:
    call sh_idlg_apply
    call sh_idlg_close
    jmp .out
.cancel:
    call sh_idlg_close
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_idlg_onclick - CX,DX = the click, screen-absolute
; -----------------------------------------------------------------------------
sh_idlg_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, sh_idlg_line               ; the field's rect is already
    call os88line_click                ; screen-absolute from the last paint
    jnc .redraw
    mov bx, [sh_idlg_win]
    push cx
    push dx
    call OSAPI_WM_CONTENT
    pop dx
    pop cx
    sub cx, ax
    sub dx, [sh_idlg_oy]
    cmp cx, SH_IDLG_BTX1
    jb .out
    cmp cx, SH_IDLG_BTX2
    ja .out
    cmp dx, SH_IDLG_OKY1
    jb .out
    cmp dx, SH_IDLG_OKY2
    jle .doOK
    cmp dx, SH_IDLG_CAY1
    jb .out
    cmp dx, SH_IDLG_CAY2
    jle .doCancel
    jmp .out
.redraw:
    mov si, [sh_idlg_win]
    call sh_idlg_paint
    jmp .out
.doOK:
    call sh_idlg_apply
    call sh_idlg_close
    jmp .out
.doCancel:
    call sh_idlg_close
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_idlg_apply - dispatch on the kind. A value this cannot make sense of is
; REFUSED SILENTLY and the old one kept, rather than being coerced to zero:
; a column of width 0 is invisible and a Goto to a reference that does not
; parse has nowhere to go, so doing nothing is the honest answer.
; -----------------------------------------------------------------------------
sh_idlg_apply:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [sh_idlg_kind], SH_ID_DEFN
    je .defname
    cmp byte [sh_idlg_kind], SH_ID_FIND
    je .find
    cmp byte [sh_idlg_kind], SH_ID_GOTO
    je .goto
    mov si, sh_idlg_buf                ; the two numeric kinds
    call sh_pnum_at
    jc .out                            ; not a number at all
    cmp byte [sh_idlg_kind], SH_ID_ROWH
    je .rowh
    cmp ax, SH_CW_MINCH                ; COLUMN WIDTH IS IN CHARACTERS, which
    jb .out                            ; is Excel's own unit for it - the
    cmp ax, SH_CW_MAXCH                ; pixel width is a consequence, not the
    ja .out                            ; thing the user types
    mov [sh_cellch], ax
    mov cl, 3
    shl ax, cl
    mov [sh_cellw], ax
    call sh_mkblank                    ; the blank-cell fill string is sized
    jmp .redraw                        ; from the width, so it must follow it
.rowh:
    cmp ax, SH_RH_MIN
    jb .out
    cmp ax, SH_RH_MAX
    ja .out
    mov [sh_cellh], ax
    jmp .redraw
.goto:
    mov si, sh_idlg_buf
    call sh_upcase_at                  ; 'a1' and 'A1' both work, as in Excel
    mov si, sh_idlg_buf
    call sh_pcellref                   ; CF=1 = AX col, BX row
    jnc .out
    cmp ax, SH_COLS
    jae .out
    cmp bx, SH_ROWS
    jae .out
    mov si, [sh_ownwin]                ; sh_select's own contract: SI must be
    call sh_select                     ; the window; it scrolls and repaints
    jmp .out                           ; itself - and a Goto DEFINES NO NAME,
                                       ; so it must not fall into .defname
.defname:
    mov si, sh_idlg_buf                ; the name binds THE SELECTION, which is
    mov ax, [sh_selcol]                ; where it was when the dialog opened -
    mov bx, [sh_selrow]                ; nothing can move it while a modal
    call sh_name_def                   ; dialog owns the input
    mov word [sh_msg], sh_s_id_named
    jnc .redraw
    mov word [sh_msg], sh_s_id_nofit
    jmp .redraw
.find:
    call sh_docmd_find
    jmp .redraw
.redraw:
    call sh_geom                       ; the cell size may have changed, so the
    mov si, [sh_ownwin]                ; visible row/column counts must be
    call sh_repaint                    ; recomputed before anything is drawn
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_idlg_close
; -----------------------------------------------------------------------------
sh_idlg_close:
    push ax
    push bx
    mov bx, [sh_idlg_win]
    or bx, bx
    jz .out
    mov word [sh_idlg_win], 0
    call OSAPI_WM_DESTROY               ; see sh_fdlg_close on why not CLOSE
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_docmd_find - Formula > Find...: move the selection to the next cell whose
; DISPLAYED TEXT contains what was typed.
;
; Displayed text, not stored value, and that is the useful definition rather
; than the easy one: it finds 3.5 in a cell holding 3.5, "Total" in a label,
; and - because a formula cell displays its result - 1003.5 in a cell holding
; =A2+A3. A search over stored bytes would have matched none of those the way
; a user expects, since a double's eight bytes look nothing like what is on
; screen.
;
; Case-insensitive, and it wraps: the walk starts at the cell AFTER the
; selection and comes back round to it, so Find repeated from the same box
; steps through every match rather than sticking on the first.
; -----------------------------------------------------------------------------
sh_docmd_find:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, sh_idlg_buf
    call sh_upcase_at
    cmp byte [sh_idlg_buf], 0
    je .none
    ; the scan order is the CELL ARRAY's, which is sorted by row then column -
    ; so "next" here means next in reading order, which is what it looks like
    mov cx, [sh_ncells]
    or cx, cx                         ; not jcxz: it is short-only and .none
    jz .none                          ; is past its reach from here
    xor bx, bx                        ; bx = index into the array
.each:
    push cx
    mov ax, bx
    mov cx, SH_C_SZ
    mul cx
    mov si, ax
    pop cx
    mov es, [sh_cellseg]
    mov ax, [es:si]
    push bx
    call sh_unpackrow                 ; ax = row, bx = sheet
    mov dx, bx
    pop bx
    cmp dx, [sh_cursheet]
    jne .next
    mov [sh_find_row], ax
    mov ax, [es:si+2]
    mov [sh_find_col], ax
    ; skip everything at or before the current selection on this pass
    mov ax, [sh_find_row]
    cmp ax, [sh_selrow]
    jb .next
    ja .test
    mov ax, [sh_find_col]
    cmp ax, [sh_selcol]
    jbe .next
.test:
    call sh_find_text                 ; builds the cell's displayed text
    call sh_find_match
    jc .found
.next:
    inc bx
    cmp bx, cx
    jb .each
    ; nothing after the selection: go round again from the top, so a repeated
    ; Find wraps rather than stopping
    xor bx, bx
.each2:
    push cx
    mov ax, bx
    mov cx, SH_C_SZ
    mul cx
    mov si, ax
    pop cx
    mov es, [sh_cellseg]
    mov ax, [es:si]
    push bx
    call sh_unpackrow
    mov dx, bx
    pop bx
    cmp dx, [sh_cursheet]
    jne .next2
    mov [sh_find_row], ax
    mov ax, [es:si+2]
    mov [sh_find_col], ax
    call sh_find_text
    call sh_find_match
    jc .found
.next2:
    inc bx
    cmp bx, cx
    jb .each2
.none:
    mov word [sh_msg], sh_s_id_nofnd
    jmp .out
.found:
    mov ax, [sh_find_col]
    mov bx, [sh_find_row]
    mov si, [sh_ownwin]
    call sh_select
    call sh_scrollto
    mov word [sh_msg], 0
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_find_text - the cell at (sh_find_col, sh_find_row) as UPPERCASE text in
; sh_find_buf. Goes through sh_getcell2 so a formula cell yields its RESULT,
; which is what the grid shows and therefore what a search should match.
sh_find_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [sh_find_buf], 0
    mov ax, [sh_find_col]
    mov bx, [sh_find_row]
    call sh_getcell2
    jnc .out
    cmp byte [sh_curtype], SH_T_TEXT
    je .istext
    call sh_acc_load_a                ; a number: the same ten significant
    mov di, sh_find_buf               ; digits the cell itself shows
    mov ax, 10
    call fp_ftoa
    jmp .up
.istext:
    push es
    mov es, [sh_txtseg]
    mov si, [sh_curtoff]
    mov di, sh_find_buf
    mov cx, SH_EDITMAX
.tc:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .tcd
    inc si
    inc di
    dec cx
    jnz .tc
    mov byte [di], 0
.tcd:
    pop es
.up:
    mov si, sh_find_buf
    call sh_upcase_at
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_find_match - CF=1 if sh_idlg_buf occurs anywhere in sh_find_buf
sh_find_match:
    push ax
    push bx
    push si
    push di
    mov si, sh_find_buf
.at:
    cmp byte [si], 0
    je .no
    mov bx, si
    mov di, sh_idlg_buf
.cmp:
    mov al, [di]
    or al, al
    jz .yes
    cmp al, [bx]
    jne .adv
    inc bx
    inc di
    jmp .cmp
.adv:
    inc si
    jmp .at
.no:
    ; an empty needle would have matched at the first character above, so
    ; reaching here means it really is absent
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.yes:
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; =============================================================================
; DEFINED NAMES (stage 3.0c) - Formula > Define Name... binds a name to the
; cell the selection is on, and a formula may then use that name anywhere a
; reference would go.
;
; A FIXED TABLE IN BSS rather than a claim: SH_NAME_CAP names at SH_NAME_REC
; bytes is under 400, which is small enough that a whole segment claim for it
; would be the wrong shape - and unlike the cell array it never grows during
; a repaint, so nothing here needs the shuffling that made the cells' claim
; worth having.
;
; Each record is a name, uppercased on entry, then its column and row. Names
; are compared uppercase because sh_pident already uppercases what it reads,
; and a spreadsheet where Total and TOTAL are different cells would be a trap
; rather than a feature.
;
; SCOPE, stated rather than discovered: a name binds ONE CELL, not a range,
; and it belongs to the whole instance rather than to a sheet. A range needs
; the reference-typed argument the value model still does not have (the same
; thing blocking VLOOKUP and the array functions), and per-sheet names need a
; sheet field here plus a rule for what an unqualified name means from another
; sheet - both are Stage 4.5 work and both would be worse guessed at.
; =============================================================================
SH_NAME_CAP  equ 16
SH_NAME_MAX  equ 12                  ; characters, not counting the NUL
SH_NAME_REC  equ SH_NAME_MAX + 1 + 4 ; text + NUL + col + row

; -----------------------------------------------------------------------------
; sh_name_find - in: SI = an uppercase NUL name
; out: CF=1 and BX = its record offset in sh_names; CF=0 = no such name
; -----------------------------------------------------------------------------
sh_name_find:
    push ax
    push cx
    push si
    push di
    xor bx, bx
    mov cx, [sh_nnames]
    jcxz .no
.each:
    push cx
    push si
    mov di, sh_names
    add di, bx
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .next
    or al, al
    jz .hit
    inc si
    inc di
    jmp .cmp
.next:
    pop si
    pop cx
    add bx, SH_NAME_REC
    loop .each
.no:
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.hit:
    pop si
    pop cx
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sh_name_def - in: SI = a NUL name (uppercased here), AX = col, BX = row.
; out: CF=1 the table is full. Redefining an existing name REBINDS it, which
; is what Excel does and what makes the dialog usable twice.
; -----------------------------------------------------------------------------
sh_name_def:
    push ax
    push bx
    push cx
    push si
    push di
    mov [sh_nm_col], ax
    mov [sh_nm_row], bx
    push si
    call sh_upcase_at
    mov di, sh_nm_buf                 ; clipped to SH_NAME_MAX, so a long name
    mov cx, SH_NAME_MAX               ; cannot run past its record
.copy:
    mov al, [si]
    or al, al
    jz .copied
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .copy
.copied:
    mov byte [di], 0
    pop si
    cmp byte [sh_nm_buf], 0
    je .full                          ; an empty name is not a name
    mov si, sh_nm_buf
    call sh_name_find
    jc .bind
    mov ax, [sh_nnames]
    cmp ax, SH_NAME_CAP
    jae .full
    mov cx, SH_NAME_REC
    mul cx
    mov bx, ax
    inc word [sh_nnames]
.bind:
    mov di, sh_names
    add di, bx
    mov si, sh_nm_buf
.wr:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .wr
    mov di, sh_names
    add di, bx
    add di, SH_NAME_MAX + 1
    mov ax, [sh_nm_col]
    mov [di], ax
    mov ax, [sh_nm_row]
    mov [di+2], ax
    clc
    jmp .out
.full:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_name_lookup - in: SI = an uppercase NUL name
; out: CF=1 and AX = col, BX = row; CF=0 = not a defined name
; -----------------------------------------------------------------------------
sh_name_lookup:
    push di
    call sh_name_find
    jnc .no
    mov di, sh_names
    add di, bx
    add di, SH_NAME_MAX + 1
    mov ax, [di]
    mov bx, [di+2]
    stc
    pop di
    ret
.no:
    pop di
    clc
    ret

; -----------------------------------------------------------------------------
; sh_name_list - build the pointer array sh_ldlg wants. out: CX = count.
; The pointers are into sh_names itself, which is fine because the list dialog
; only ever READS them and nothing can redefine a name while it is open.
; -----------------------------------------------------------------------------
sh_name_list:
    push ax
    push bx
    push di
    xor bx, bx
    xor di, di
    mov cx, [sh_nnames]
    jcxz .done
    push cx
.each:
    mov ax, sh_names
    add ax, bx
    mov [sh_nameptr + di], ax
    add di, 2
    add bx, SH_NAME_REC
    loop .each
    pop cx
.done:
    pop di
    pop bx
    pop ax
    ret

; =============================================================================
; The SCROLLING LIST dialog (stage 3.0c) - a framed list with a real scroll
; bar, OK and Cancel. Formula > Paste Function... and Formula > Paste Name...
; are both "pick one of a list too long to show at once", which is the one
; shape sh_fdlg_* cannot take: its radio rows are a fixed short array chosen
; by kind, and 25 function names is neither fixed nor short.
;
; The item source is a POINTER ARRAY plus a count, filled at open time, so the
; two kinds differ only in where that array comes from - sh_functab as it
; stands for the functions, and the name table built at run time for the names.
; That is also what makes a third kind free later.
;
; The bar is os88ui.inc's (SPEC.md 13.10), already opted into by this file for
; the grid's own two, so the dialog gets arrow cells, page regions and a
; proportional thumb without a line of its own.
; =============================================================================
SH_LD_FUNC   equ 0                   ; Formula > Paste Function...
SH_LD_NAME   equ 1                   ; Formula > Paste Name...
SH_LD_NKIND  equ 2

SH_LDLG_W    equ 222
SH_LDLG_LX1  equ 8                   ; the list box, content-relative
SH_LDLG_LY1  equ 22
SH_LDLG_LX2  equ 130
SH_LDLG_ROWH equ 12
SH_LDLG_ROWS equ 8                   ; visible at once
SH_LDLG_LY2  equ SH_LDLG_LY1 + SH_LDLG_ROWS * SH_LDLG_ROWH + 2
SH_LDLG_SBW  equ 14                  ; the bar sits just right of the list
SH_LDLG_BTX1 equ 152                 ; clear of the bar, which ends at
SH_LDLG_BTX2 equ 212                 ; SH_LDLG_LX2 + 2 + SH_LDLG_SBW
SH_LDLG_OKY1 equ 22
SH_LDLG_OKY2 equ 42
SH_LDLG_CAY1 equ 50
SH_LDLG_CAY2 equ 70
SH_LDLG_H    equ SH_LDLG_LY2 + SH_DLG_BMARG + TITLE_H + 1

sh_ldlg_tpl:
    dw 0, 0, SH_LDLG_W, SH_LDLG_H
    dw sh_s_ld_tfunc, sh_ldlg_paint, 0, sh_ldlg_onclick
sh_ld_titles:  dw sh_s_ld_tfunc, sh_s_ld_tname
sh_ld_prompts: dw sh_s_ld_pfunc, sh_s_ld_pname
sh_s_ld_tfunc: db 'Paste Function', 0
sh_s_ld_tname: db 'Paste Name', 0
sh_s_ld_pfunc: db 'Paste function:', 0
sh_s_ld_pname: db 'Paste name:', 0
sh_s_ld_none:  db '(none defined)', 0

; -----------------------------------------------------------------------------
; sh_ldlg_open - in: AL = SH_LD_*
; -----------------------------------------------------------------------------
sh_ldlg_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [sh_ldlg_win], 0
    jne .out
    cmp al, SH_LD_NKIND
    jae .out
    mov [sh_ldlg_kind], al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [sh_ld_titles + bx]
    mov [sh_ldlg_tpl + WT_TITLE], ax
    mov word [sh_ldlg_sel], 0
    mov word [sh_ldlg_top], 0
    cmp byte [sh_ldlg_kind], SH_LD_NAME
    je .names
    mov word [sh_ldlg_items], sh_functab   ; the function table IS the list -
    xor cx, cx                             ; it is already a NUL-terminated
    mov si, sh_functab                     ; pointer array, which is what this
.fcount:                                   ; dialog wants
    cmp word [si], 0
    je .fdone
    inc cx
    add si, 2
    jmp .fcount
.fdone:
    mov [sh_ldlg_count], cx
    jmp .have
.names:
    call sh_name_list                      ; -> sh_nameptr[] and CX
    mov word [sh_ldlg_items], sh_nameptr
    mov [sh_ldlg_count], cx
.have:
    call OSAPI_VIDEO                  ; centred the same way every other
    sub ax, SH_LDLG_W                 ; dialog here is
    sar ax, 1
    mov [sh_ldlg_tpl + WT_X], ax
    sub bx, SH_LDLG_H
    sar bx, 1
    cmp bx, MBAR_H + 8
    jge .placed
    mov bx, MBAR_H + 8
.placed:
    mov [sh_ldlg_tpl + WT_Y], bx
    mov si, sh_ldlg_tpl
    call OSAPI_WM_CREATE               ; the window comes back in BX, NOT SI -
    jc .out                            ; SI is still the template - and it is
    mov [sh_ldlg_win], bx              ; created HIDDEN, so the show is not
    call OSAPI_WM_SHOW                 ; optional
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ldlg_sbset - fill the scroll block from the live list state. One place,
; because the painter and the hit-tester must not disagree about the geometry
; (which is the whole reason os88ui_sbhit derives everything from the block).
; -----------------------------------------------------------------------------
sh_ldlg_sbset:
    push ax
    mov ax, [sh_ldlg_ox]
    add ax, SH_LDLG_LX2 + 2
    mov [sh_ldsb + 0], ax
    add ax, SH_LDLG_SBW - 1
    mov [sh_ldsb + 4], ax
    mov ax, [sh_ldlg_oy]
    add ax, SH_LDLG_LY1
    mov [sh_ldsb + 2], ax
    mov ax, [sh_ldlg_oy]
    add ax, SH_LDLG_LY2
    mov [sh_ldsb + 6], ax
    mov ax, [sh_ldlg_count]
    cmp ax, SH_LDLG_ROWS
    jae .tot
    mov ax, SH_LDLG_ROWS
.tot:
    mov [sh_ldsb + 8], ax
    mov word [sh_ldsb + 10], SH_LDLG_ROWS
    mov ax, [sh_ldlg_top]
    mov [sh_ldsb + 12], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ldlg_paint
; -----------------------------------------------------------------------------
sh_ldlg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [sh_ldlg_ox], ax
    mov [sh_ldlg_oy], dx

    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [sh_ldlg_ox]
    mov bx, [sh_ldlg_oy]
    mov cx, ax
    add cx, SH_LDLG_W - 3
    mov dx, bx
    add dx, SH_LDLG_H - TITLE_H - 3
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov cx, [sh_ldlg_ox]              ; the prompt
    add cx, SH_LDLG_LX1
    mov dx, [sh_ldlg_oy]
    add dx, 6
    mov bl, [sh_ldlg_kind]
    xor bh, bh
    shl bx, 1
    mov si, [sh_ld_prompts + bx]
    call OSAPI_FONT_STR_XPARENT

    mov ax, [sh_ldlg_ox]              ; the list box's own frame
    add ax, SH_LDLG_LX1
    mov bx, [sh_ldlg_oy]
    add bx, SH_LDLG_LY1
    mov cx, [sh_ldlg_ox]
    add cx, SH_LDLG_LX2
    mov dx, [sh_ldlg_oy]
    add dx, SH_LDLG_LY2
    call OSAPI_GFX_FRAME

    cmp word [sh_ldlg_count], 0
    jne .rows
    mov cx, [sh_ldlg_ox]              ; an empty list says so rather than
    add cx, SH_LDLG_LX1 + 6           ; showing a blank box
    mov dx, [sh_ldlg_oy]
    add dx, SH_LDLG_LY1 + 4
    mov si, sh_s_ld_none
    call OSAPI_FONT_STR_XPARENT
    jmp .buttons
.rows:
    mov word [sh_ldlg_i], 0
.rloop:
    mov ax, [sh_ldlg_i]
    cmp ax, SH_LDLG_ROWS
    jae .rdone
    add ax, [sh_ldlg_top]
    cmp ax, [sh_ldlg_count]
    jae .rdone
    mov [sh_ldlg_idx], ax
    mov ax, [sh_ldlg_i]
    mov cx, SH_LDLG_ROWH
    mul cx
    add ax, [sh_ldlg_oy]
    add ax, SH_LDLG_LY1 + 2
    mov [sh_ldlg_rowy], ax
    mov ax, [sh_ldlg_idx]
    cmp ax, [sh_ldlg_sel]
    jne .plain
    mov ax, [sh_ldlg_ox]              ; the selected row is inverted, the same
    add ax, SH_LDLG_LX1 + 1           ; way the menu's hot item is
    mov bx, [sh_ldlg_rowy]
    mov cx, [sh_ldlg_ox]
    add cx, SH_LDLG_LX2 - 1
    mov dx, bx
    add dx, SH_LDLG_ROWH - 1
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN
    mov al, CWHITE
    call OSAPI_SET_COLOR
    jmp .rtext
.plain:
    clc
    call OSAPI_GFX_PEN
    mov al, CBLACK
    call OSAPI_SET_COLOR
.rtext:
    mov bx, [sh_ldlg_items]
    mov ax, [sh_ldlg_idx]
    shl ax, 1
    add bx, ax
    mov si, [bx]
    mov cx, [sh_ldlg_ox]
    add cx, SH_LDLG_LX1 + 4
    mov dx, [sh_ldlg_rowy]
    add dx, 2
    call OSAPI_FONT_STR_XPARENT
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [sh_ldlg_i]
    inc ax
    mov [sh_ldlg_i], ax
    jmp .rloop
.rdone:
    call sh_ldlg_sbset
    mov bx, sh_ldsb
    call os88ui_sbar
.buttons:
    mov ax, [sh_ldlg_ox]
    add ax, SH_LDLG_BTX1
    mov [sh_ldlg_rect+0], ax
    mov ax, [sh_ldlg_oy]
    add ax, SH_LDLG_OKY1
    mov [sh_ldlg_rect+2], ax
    mov ax, [sh_ldlg_ox]
    add ax, SH_LDLG_BTX2
    mov [sh_ldlg_rect+4], ax
    mov ax, [sh_ldlg_oy]
    add ax, SH_LDLG_OKY2
    mov [sh_ldlg_rect+6], ax
    mov bx, sh_ldlg_rect
    mov si, sh_s_fd_ok
    mov di, OS88UI_DEF
    call os88ui_btn
    mov ax, [sh_ldlg_oy]
    add ax, SH_LDLG_CAY1
    mov [sh_ldlg_rect+2], ax
    mov ax, [sh_ldlg_oy]
    add ax, SH_LDLG_CAY2
    mov [sh_ldlg_rect+6], ax
    mov bx, sh_ldlg_rect
    mov si, sh_s_fd_cancel
    xor di, di
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ldlg_onclick - in: CX/DX = absolute click point
; -----------------------------------------------------------------------------
sh_ldlg_onclick:
    push ax
    push bx
    push cx
    push dx
    mov bx, [sh_ldlg_win]
    or bx, bx
    jz .out
    push cx
    push dx
    call OSAPI_WM_CONTENT
    mov [sh_ldlg_ox], ax
    mov [sh_ldlg_oy], dx
    pop dx
    pop cx

    call sh_ldlg_sbset                ; the bar first: it owns a strip of its
    mov ax, cx                        ; own and a click there is never a row
    mov bx, dx
    mov cx, ax
    mov dx, bx
    mov bx, sh_ldsb
    call os88ui_sbhit
    or al, al
    jz .notbar
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .down
    cmp al, OS88UI_SBPGUP
    je .pgup
    cmp al, OS88UI_SBPGDN
    je .pgdn
    jmp .out
.up:
    cmp word [sh_ldlg_top], 0
    je .out
    dec word [sh_ldlg_top]
    jmp .redraw
.down:
    call sh_ldlg_maxtop
    cmp [sh_ldlg_top], ax
    jae .out
    inc word [sh_ldlg_top]
    jmp .redraw
.pgup:
    mov ax, [sh_ldlg_top]
    cmp ax, SH_LDLG_ROWS
    jae .pgu2
    xor ax, ax
    jmp .pgset
.pgu2:
    sub ax, SH_LDLG_ROWS
    jmp .pgset
.pgdn:
    mov ax, [sh_ldlg_top]
    add ax, SH_LDLG_ROWS
    push ax
    call sh_ldlg_maxtop
    mov bx, ax
    pop ax
    cmp ax, bx
    jbe .pgset
    mov ax, bx
.pgset:
    mov [sh_ldlg_top], ax
    jmp .redraw
.notbar:
    mov ax, cx                        ; --- the buttons ---
    sub ax, [sh_ldlg_ox]
    mov bx, dx
    sub bx, [sh_ldlg_oy]
    cmp ax, SH_LDLG_BTX1
    jb .list
    cmp ax, SH_LDLG_BTX2
    ja .list
    cmp bx, SH_LDLG_OKY1
    jb .notok
    cmp bx, SH_LDLG_OKY2
    ja .notok
    call sh_ldlg_apply
    call sh_ldlg_close
    jmp .out
.notok:
    cmp bx, SH_LDLG_CAY1
    jb .out
    cmp bx, SH_LDLG_CAY2
    ja .out
    call sh_ldlg_close
    jmp .out
.list:
    cmp ax, SH_LDLG_LX1
    jb .out
    cmp ax, SH_LDLG_LX2
    ja .out
    sub bx, SH_LDLG_LY1 + 2
    jb .out
    mov ax, bx
    xor dx, dx
    mov bx, SH_LDLG_ROWH
    div bx
    cmp ax, SH_LDLG_ROWS
    jae .out
    add ax, [sh_ldlg_top]
    cmp ax, [sh_ldlg_count]
    jae .out
    mov [sh_ldlg_sel], ax
.redraw:
    mov si, [sh_ldlg_win]             ; sh_ldlg_paint, NOT sh_repaint: that one
    call sh_ldlg_paint                ; repaints THE SHEET into whatever window
                                       ; SI names, so calling it here drew the
                                       ; grid, the menu bar and the status line
                                       ; inside this dialog's own rect. Every
                                       ; other dialog here calls its own
                                       ; painter for exactly this reason.
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_ldlg_maxtop - AX = the largest legal sh_ldlg_top
sh_ldlg_maxtop:
    mov ax, [sh_ldlg_count]
    cmp ax, SH_LDLG_ROWS
    ja .some
    xor ax, ax
    ret
.some:
    sub ax, SH_LDLG_ROWS
    ret

; -----------------------------------------------------------------------------
; sh_ldlg_apply - paste the chosen item into the cell being edited.
;
; A FUNCTION arrives with its opening parenthesis, because that is what the
; user would type next and Excel's own Paste Function does the same; a NAME
; arrives bare. Both go through sh_editstart/sh_flkey so the field's caret and
; bounds behave exactly as they do for typing - there is no second path into
; the edit buffer to keep in step.
; -----------------------------------------------------------------------------
sh_ldlg_apply:
    push ax
    push bx
    push si
    cmp word [sh_ldlg_count], 0
    je .out
    mov bx, [sh_ldlg_items]
    mov ax, [sh_ldlg_sel]
    shl ax, 1
    add bx, ax
    mov ax, [bx]
    mov [sh_ldlg_src], ax             ; the chosen string, in BSS rather than
                                       ; in SI - see sh_ldlg_putc
    cmp byte [sh_editing], 0
    jne .append
    call sh_editstart                 ; nothing being edited: start a formula,
    mov al, '='                       ; since a bare function name is not one
    call sh_ldlg_putc
.append:
    call sh_ldlg_puts
    cmp byte [sh_ldlg_kind], SH_LD_FUNC
    jne .done
    mov al, '('                       ; a function arrives with its opening
    call sh_ldlg_putc                 ; parenthesis, as Excel's own does
.done:
    mov si, [sh_ownwin]
    call sh_repaint
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ldlg_putc - one character into the edit field, in: AL.
;
; TWO REGISTER CONTRACTS MEET HERE and they want different things in the same
; register. os88line_key reads AH as a SCAN CODE, so AH must be cleared or a
; leftover one makes the field do something else with a perfectly good
; character. And SI must not be the string being copied out, as it was:
; sh_flkey points SI at the field's own block. Both are set here, once,
; instead of at each call site.
; -----------------------------------------------------------------------------
sh_ldlg_putc:
    push ax
    push si
    xor ah, ah
    mov si, [sh_ownwin]
    call sh_flkey
    pop si
    pop ax
    ret

; sh_ldlg_puts - every character of the string at sh_ldlg_src
sh_ldlg_puts:
    push ax
    push si
    mov si, [sh_ldlg_src]
.lp:
    mov al, [si]
    or al, al
    jz .done
    inc si
    push si
    call sh_ldlg_putc
    pop si
    jmp .lp
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ldlg_close
; -----------------------------------------------------------------------------
sh_ldlg_close:
    push ax
    push bx
    mov bx, [sh_ldlg_win]
    or bx, bx
    jz .out
    mov word [sh_ldlg_win], 0
    call OSAPI_WM_DESTROY               ; see sh_fdlg_close on why not CLOSE
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_upcase_at - uppercase the NUL string at SI in place. Preserves all.
; -----------------------------------------------------------------------------
sh_upcase_at:
    push ax
    push si
.loop:
    mov al, [si]
    or al, al
    jz .done
    cmp al, 'a'
    jb .next
    cmp al, 'z'
    ja .next
    sub al, 32
    mov [si], al
.next:
    inc si
    jmp .loop
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_pnum_at - read an unsigned decimal from the NUL string at SI.
; out: CF=0 and AX = the value; CF=1 if there was no digit at all or it ran
; past 65535. Leading blanks are skipped; anything after the digits is
; ignored, so '12 wide' reads as 12.
; -----------------------------------------------------------------------------
sh_pnum_at:
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    xor cx, cx                         ; cx = how many digits were seen
.skip:
    cmp byte [si], ' '
    jne .loop
    inc si
    jmp .skip
.loop:
    mov bl, [si]
    cmp bl, '0'
    jb .done
    cmp bl, '9'
    ja .done
    cmp ax, 6553                       ; 6553*10 is the last product that fits,
    ja .over                           ; checked BEFORE the shifts rather than
    mov dx, ax                         ; from the carry of one of them - the
    shl ax, 1                          ; first two can overflow silently
    shl ax, 1
    add ax, dx
    shl ax, 1                          ; ax = ax*10 (8086: shift by 1 or CL)
    sub bl, '0'
    xor bh, bh
    add ax, bx
    jc .over
    inc cx
    inc si
    jmp .loop
.done:
    or cx, cx
    jz .none
    clc
    jmp .out
.over:
.none:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; Formula > Note... (stage 3.0b) - Excel 2.1's cell notes, and the FIRST
; consumer of apps/os88text.inc. Everything above this point that takes typed
; input takes it one character at a time into a fixed field; this is the first
; place in Sheet where a user can type a paragraph.
;
; It edits sh_notetext, a bss COPY, and only writes through to the note table
; on OK - so Cancel is free and a commit refused for want of arena space leaves
; the old note exactly as it was, rather than half-replacing it.
;
; It also remembers the cell it was opened on (sh_notecol/sh_noterow) instead
; of reading the live selection at OK time. This dialog is NON-MODAL like every
; other one here, so the user can move the selection while it is open; writing
; to whatever happens to be selected on OK would attach the note to the wrong
; cell, which is exactly the kind of quiet wrongness that is hard to notice.
; =============================================================================
SH_NDLG_W    equ 300
SH_NDLG_BX1  equ 8                   ; the text box, content-relative
SH_NDLG_BY1  equ 24
SH_NDLG_BX2  equ 214
SH_NDLG_BY2  equ 112                 ; -> 24 columns x 10 rows = 240 cells,
                                     ; which is what SH_NOTEMAX is sized from
SH_NDLG_BTX1 equ 222                 ; OK / Cancel, both 64 wide -
                                     ; 'Cancel' is 6 glyphs at the fixed 8px
                                     ; cell, so a narrower button clips its
                                     ; own label (it did, at 34)
SH_NDLG_BTX2 equ 286
SH_NDLG_OKY1 equ 24
SH_NDLG_OKY2 equ 44
SH_NDLG_CAY1 equ 52
SH_NDLG_CAY2 equ 72
SH_NDLG_H    equ SH_NDLG_BY2 + SH_DLG_BMARG + TITLE_H + 1   ; the text box is
                                     ; the lowest element, not the buttons

sh_ndlg_tpl:
    dw 0, 0, SH_NDLG_W, SH_NDLG_H
    dw sh_s_ndlg_title, sh_ndlg_paint, sh_ndlg_onkey, sh_ndlg_onclick

sh_s_ndlg_title: db 'Note', 0
sh_s_ndlg_cell:  db 'Cell:', 0
sh_s_ndlg_ok:    db 'OK', 0
sh_s_ndlg_can:   db 'Cancel', 0

; -----------------------------------------------------------------------------
; sh_ndlg_open - load the selected cell's note into the edit buffer and put
; the dialog up. Same single-instance gate as sh_bdlg_open.
; -----------------------------------------------------------------------------
sh_ndlg_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [sh_noteopen], 0
    jne .out
    mov ax, [sh_selcol]                ; pin the cell NOW - see this section's
    mov [sh_notecol], ax               ; header on why not at OK time
    mov bx, [sh_selrow]
    mov [sh_noterow], bx
    mov byte [sh_notetext], 0          ; no note = an empty box, not stale text
    call sh_nt_get
    jnc .nonote
    mov si, ax                         ; ax = the text's offset in the arena
    call sh_note_load
.nonote:
    mov si, sh_notebox                 ; the field, over the buffer
    mov word [si + TX_BUF], sh_notetext
    mov word [si + TX_MAX], SH_NOTEMAX
    mov word [si + TX_TOP], 0
    mov byte [si + TX_FOCUS], 1
    mov di, sh_notetext
    call os88text_set                  ; sets LEN/CAR from the buffer's content
    call OSAPI_VIDEO
    sub ax, SH_NDLG_W
    sar ax, 1
    mov [sh_ndlg_tpl + WT_X], ax
    sub bx, SH_NDLG_H
    sar bx, 1
    cmp bx, MBAR_H + 8
    jge .placed
    mov bx, MBAR_H + 8
.placed:
    mov [sh_ndlg_tpl + WT_Y], bx
    mov si, sh_ndlg_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [sh_ndlg_win], bx
    mov byte [sh_noteopen], 1
    call OSAPI_WM_SHOW
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
; sh_note_load - copy the NUL string at [sh_txtseg]:SI into sh_notetext,
; clipped to SH_NOTEMAX-1. in: SI = the arena offset. Preserves everything.
;
; A byte-at-a-time copy through ES rather than a rep movsb, so DS is never
; changed at all - the alternative wants DS pointing at the claim, and every
; sh_* symbol in this file is DS-relative.
; -----------------------------------------------------------------------------
sh_note_load:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [sh_txtseg]
    mov di, sh_notetext
    mov cx, SH_NOTEMAX - 1
.copy:
    jcxz .done
    mov al, [es:si]
    or al, al
    jz .done
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .copy
.done:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ndlg_paint - SI = the dialog window
; -----------------------------------------------------------------------------
sh_ndlg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT               ; ax,dx = the content origin
    mov [sh_ndlg_ox], ax
    mov [sh_ndlg_oy], dx

    mov cx, ax                          ; the 'Cell:' label and the reference
    add cx, SH_NDLG_BX1
    mov dx, [sh_ndlg_oy]
    add dx, 6
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, sh_s_ndlg_cell
    call OSAPI_FONT_STR_XPARENT
    mov di, sh_tbuf                     ; the reference, built the same way the
    mov ax, [sh_notecol]                ; formula bar's own name box builds it
    call sh_colname
    mov si, sh_colbuf
    call sh_strcpy_to_di
    mov ax, [sh_noterow]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_strcpy_to_di
    mov cx, [sh_ndlg_ox]
    add cx, SH_NDLG_BX1 + 48
    mov dx, [sh_ndlg_oy]
    add dx, 6
    mov si, sh_tbuf
    call OSAPI_FONT_STR_XPARENT

    mov si, sh_notebox                  ; the field's rect is refreshed from
    mov ax, [sh_ndlg_ox]                ; the LIVE content origin every paint,
    add ax, SH_NDLG_BX1                 ; because the window moves - the same
    mov [si + TX_X1], ax                ; painter/hit-tester drift the scroll
    mov ax, [sh_ndlg_ox]                ; bars already had to solve
    add ax, SH_NDLG_BX2
    mov [si + TX_X2], ax
    mov ax, [sh_ndlg_oy]
    add ax, SH_NDLG_BY1
    mov [si + TX_Y1], ax
    mov ax, [sh_ndlg_oy]
    add ax, SH_NDLG_BY2
    mov [si + TX_Y2], ax
    call os88text_draw

    mov ax, [sh_ndlg_ox]                ; OK - os88ui_btn takes BX = a POINTER
    add ax, SH_NDLG_BTX1                ; to the rect, not the rect in
    mov [sh_ndlg_rect], ax              ; AX/BX/CX/DX
    mov ax, [sh_ndlg_oy]
    add ax, SH_NDLG_OKY1
    mov [sh_ndlg_rect+2], ax
    mov ax, [sh_ndlg_ox]
    add ax, SH_NDLG_BTX2
    mov [sh_ndlg_rect+4], ax
    mov ax, [sh_ndlg_oy]
    add ax, SH_NDLG_OKY2
    mov [sh_ndlg_rect+6], ax
    mov bx, sh_ndlg_rect
    mov si, sh_s_ndlg_ok
    mov di, OS88UI_DEF
    call os88ui_btn
    mov ax, [sh_ndlg_oy]                ; Cancel - same x, two new y's
    add ax, SH_NDLG_CAY1
    mov [sh_ndlg_rect+2], ax
    mov ax, [sh_ndlg_oy]
    add ax, SH_NDLG_CAY2
    mov [sh_ndlg_rect+6], ax
    mov bx, sh_ndlg_rect
    mov si, sh_s_ndlg_can
    xor di, di
    call os88ui_btn

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ndlg_onkey - AL = ascii, AH = scan code. The field gets first refusal;
; Escape is the only key this dialog claims for itself.
; -----------------------------------------------------------------------------
sh_ndlg_onkey:
    push ax
    push si
    cmp al, 27
    je .cancel
    mov si, sh_notebox
    call os88text_key
    jc .out                             ; the field did not want it
    call os88text_draw                  ; the BOX, not the whole dialog: the
    jmp .out                            ; labels and both buttons did not
                                        ; change, and os88ui_btn's own erase
                                        ; would flash them on every keystroke.
                                        ; The block's rect is refreshed by
                                        ; every real paint and the window
                                        ; cannot move mid-callback (the gfx
                                        ; lock is held) - the same trust the
                                        ; onclick hit-test below already
                                        ; places in it
.cancel:
    call sh_ndlg_close
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ndlg_onclick - CX,DX = the click, screen-absolute
; -----------------------------------------------------------------------------
sh_ndlg_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, sh_notebox                  ; the field first: its own rect is
    call os88text_click                 ; already screen-absolute from the
    jnc .redraw                         ; last paint, so no conversion here
    mov bx, [sh_ndlg_win]
    push cx
    push dx
    call OSAPI_WM_CONTENT
    pop dx
    pop cx
    sub cx, ax                          ; cx,dx = content-relative
    sub dx, [sh_ndlg_oy]
    cmp cx, SH_NDLG_BTX1
    jb .out
    cmp cx, SH_NDLG_BTX2
    ja .out
    cmp dx, SH_NDLG_OKY1
    jb .out
    cmp dx, SH_NDLG_OKY2
    jle .doOK
    cmp dx, SH_NDLG_CAY1
    jb .out
    cmp dx, SH_NDLG_CAY2
    jle .doCancel
    jmp .out
.redraw:
    mov si, sh_notebox                  ; only the caret moved: redraw the
    call os88text_draw                  ; box, not the dialog's chrome
    jmp .out
.doOK:
    call sh_ndlg_apply
    call sh_ndlg_close
    jmp .out
.doCancel:
    call sh_ndlg_close
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ndlg_apply - commit the edit buffer to the cell the dialog was opened on.
; An empty buffer removes the note (sh_nt_set's own rule), which is how this
; dialog clears one - Excel 2.1's Note dialog has no separate Delete either.
; -----------------------------------------------------------------------------
sh_ndlg_apply:
    push ax
    push bx
    push si
    mov ax, [sh_notecol]
    mov bx, [sh_noterow]
    mov si, sh_notetext
    call sh_nt_set                      ; CF=1 = table or arena full. Silent,
                                        ; the same scope limit sh_bdlg_apply
                                        ; documents for a full border table.
    mov si, [sh_ownwin]
    call sh_repaint
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ndlg_close
; -----------------------------------------------------------------------------
sh_ndlg_close:
    push ax
    push bx
    mov bx, [sh_ndlg_win]
    or bx, bx
    jz .out
    mov word [sh_ndlg_win], 0
    mov byte [sh_noteopen], 0
    call OSAPI_WM_DESTROY               ; see sh_fdlg_close on why not CLOSE
.out:
    pop bx
    pop ax
    ret

sh_dlg:
    push bx
    push si
    push di
    mov bx, si
    mov di, sh_ondlg
    mov si, sh_name
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; sh_ondlg - the file dialog's completion proc (SPEC.md 38.6)
; in:  AL=mode, SI=our window ptr, DI=chosen name (ES=KERNEL_SEG); UI task,
;      gfx lock HELD, dialog already destroyed - we owe the repaint
; -----------------------------------------------------------------------------
sh_ondlg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, al
    mov cx, si                       ; CX = our window ptr (SI about to move)
    mov si, di
    mov di, sh_name
    mov dx, SH_NAMEMAX               ; the count lives in DX - the loop body
.copy:                               ; writes AL, so AX cannot hold it, and
    mov al, [es:si]                  ; CX holds the window
    mov [di], al
    or al, al
    jz .copied
    inc si
    inc di
    dec dx
    jnz .copy
    mov byte [di], 0
.copied:
    mov si, cx                       ; SI = our window again
    or bl, bl
    jz .load
    call sh_dowrite
    jmp short .draw
.load:
    call sh_doread
.draw:
    call sh_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_new - File > New: clear the sheet and the name, reselect A1
; -----------------------------------------------------------------------------
sh_new:
    push ax
    push cx
    push dx
    push si
    push di
    mov dx, si                       ; DX = window ptr, stashed
    mov si, sh_defname
    mov di, sh_name
    call sh_strcpy
    mov si, dx                       ; SI = window ptr, restored
    mov word [sh_ncells], 0
    mov word [sh_txtlen], 0
    mov word [sh_nbord], 0           ; the discarded document's borders and
    mov word [sh_nnote], 0           ; notes go with it - a note record holds
                                     ; an OFFSET into the arena reset above,
                                     ; and would read new text through it
    mov word [sh_cursheet], 0
    mov cx, SH_SHEETS * 4            ; 4 words per sheet: sel/row/scl/scr
    mov di, sh_selsave
    xor ax, ax
.clrsave:
    mov [di], ax
    add di, 2
    loop .clrsave
    mov word [sh_selcol], 0
    mov word [sh_selrow], 0
    mov word [sh_scrollcol], 0
    mov word [sh_scrollrow], 0
    mov byte [sh_editing], 0
    mov word [sh_msg], 0
    call sh_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_switchsheet - in: AX = target sheet index (0..SH_SHEETS-1); saves the
; outgoing sheet's selection/scroll into its slot and restores the
; incoming sheet's own (all-zero the first time it's ever visited)
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sh_sheetmark - point every Sheets item at its plain string, then the CURRENT
; one at its marked twin. Called at startup and after every switch, so the mark
; is derived from sh_cursheet rather than tracked alongside it.
; -----------------------------------------------------------------------------
sh_sheetmark:
    push ax
    push bx
    push cx
    xor bx, bx
    mov cx, SH_SHEETS
.lp:
    mov ax, [sh_sheet_plain + bx]
    mov [sh_i_sheet + bx], ax
    add bx, 2
    loop .lp
    mov bx, [sh_cursheet]
    shl bx, 1
    mov ax, [sh_sheet_chk + bx]
    mov [sh_i_sheet + bx], ax
    pop cx
    pop bx
    pop ax
    ret

sh_switchsheet:
    push ax
    push bx
    push cx
    mov cx, ax                       ; cx = target sheet, preserved across
                                      ; the save step below
    cmp cx, [sh_cursheet]
    je .out
    mov bx, [sh_cursheet]
    shl bx, 1
    mov ax, [sh_selcol]
    mov [sh_selsave+bx], ax
    mov ax, [sh_selrow]
    mov [sh_rowsave+bx], ax
    mov ax, [sh_scrollcol]
    mov [sh_sclsave+bx], ax
    mov ax, [sh_scrollrow]
    mov [sh_scrsave+bx], ax
    mov [sh_cursheet], cx
    call sh_sheetmark
    mov bx, cx
    shl bx, 1
    mov ax, [sh_selsave+bx]
    mov [sh_selcol], ax
    mov ax, [sh_rowsave+bx]
    mov [sh_selrow], ax
    mov ax, [sh_sclsave+bx]
    mov [sh_scrollcol], ax
    mov ax, [sh_scrsave+bx]
    mov [sh_scrollrow], ax
    call sh_repaint
.out:
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; File I/O: SYLK write and read, over the sparse array directly
; =============================================================================

; -----------------------------------------------------------------------------
; sh_dowrite - write the sheet to [sh_name], format chosen by its extension
; (SPEC-free scope decision, this project's own: ".DIF" writes DIF,
; everything else writes SYLK, matching stage 1.0's default).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sh_sheets_used - out: AX = how many of the SH_SHEETS grids hold at least one
; cell, and BX = a bitmap of which. One walk of the array, not four.
; -----------------------------------------------------------------------------
sh_sheets_used:
    push cx
    push dx
    push si
    push es
    xor bx, bx
    xor cx, cx
    mov es, [sh_cellseg]
.each:
    cmp cx, [sh_ncells]
    jae .counted
    mov ax, cx
    push bx
    mov bx, SH_C_SZ
    mul bx
    pop bx
    mov si, ax
    mov ax, [es:si]
    push bx
    call sh_unpackrow                 ; BX = this record's sheet
    mov dx, bx
    pop bx
    mov ax, 1
    push cx
    mov cx, dx
    jcxz .noshift
.shift:
    shl ax, 1
    loop .shift
.noshift:
    pop cx
    or bx, ax
    inc cx
    jmp .each
.counted:
    mov ax, bx                        ; popcount of the bitmap
    xor dx, dx
    mov cx, SH_SHEETS
.pop1:
    shr ax, 1
    jnc .pop2
    inc dx
.pop2:
    loop .pop1
    mov ax, dx
    pop es
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; sh_dowrite - pick the writer from the file name's extension.
;
; AND SAY SO WHEN A SAVE CANNOT CARRY EVERYTHING. SYLK and DIF have no
; multi-sheet concept at all - SYLK has no notion of a sheet and DIF is one
; table - so a workbook with data on more than one of them loses the rest, and
; used to lose it in silence. It says so in the status bar now. BIFF is the one
; format here that CAN carry them, and does (81.10.5).
; -----------------------------------------------------------------------------
sh_dowrite:
    push si
    push di
    mov si, sh_name
    mov di, sh_s_ext_dif
    call sh_nameends
    pop di
    pop si
    jc .dif
    push si
    push di
    mov si, sh_name
    mov di, sh_s_ext_biff
    call sh_nameends
    pop di
    pop si
    jc .biff
    call sh_dowrite_sylk
    jmp .warn
.dif:
    call sh_dowrite_dif
    jmp .warn
.biff:
    jmp sh_dowrite_biff
.warn:
    push ax
    push bx
    cmp word [sh_msg], sh_m_saved     ; only upgrade a plain success: a failed
    jne .warned                       ; write's "Err N" (or a truncated save's
                                      ; message) must not be replaced by a
                                      ; string that begins with "Saved"
    call sh_sheets_used
    cmp ax, 2
    jb .warned
    mov word [sh_msg], sh_s_onesheet
.warned:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_dowrite_sylk - write the sheet to [sh_name] as SYLK. Walks the sorted
; cell array directly (already row-major), so no grid loop is needed at
; all. A formatted cell's C (value) record is followed by a real SYLK F
; (formatting) record - "F;X<col>;Y<row>;F<c1><n><c2>[;K]" - using
; MultiPlan-era SYLK's actual format codes (stage 1.6): c1 is '$' for
; Currency or 'G' for everything else (General/Comma/Percent all share
; 'G' - real SYLK has no comma or percent code of its own; Comma instead
; sets the separate ;K "commas are set" flag, and Percent has no real
; equivalent at all so it degrades to General on disk), c2 is the real
; alignment code (G/L/C/R) matching this app's own alignment 1:1. Real
; SYLK, per the era's own documentation, has no bold/underline concept
; whatsoever - MultiPlan predates that - so neither persists here; this is
; the same "only persist what the real format actually has" rule DIF
; follows below, not an oversight.
; -----------------------------------------------------------------------------
sh_dowrite_sylk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov es, [sh_stgseg]
    xor di, di
    mov si, sh_s_id
    call sh_stgput

    mov byte [sh_trunc], 0
    mov word [sh_wrow], 0            ; reused here as the record index
.rec:
    mov bx, [sh_wrow]
    cmp bx, [sh_ncells]
    jae .footer
    cmp byte [sh_trunc], 0
    jne .footer
    mov ax, di
    add ax, 200                      ; worst case a LABEL's C line: the head
                                      ; ("C;X256;Y16384;K"), the quoted text
                                      ; with every embedded quote DOUBLED
                                      ; (2 + 2*SH_EDITMAX), CRLF, plus its
                                      ; F line ("F;X256;Y16384;F$0R;K\r\n");
                                      ; the ;E formula case is smaller
    cmp ax, SH_STAGE_MAX
    jbe .room
    mov byte [sh_trunc], 1
    jmp .footer
.room:
    mov ax, bx
    mov cx, SH_C_SZ
    mul cx
    mov si, ax                        ; SI = this record's offset in cellseg
    push es
    mov es, [sh_cellseg]
    mov ax, [es:si]
    call sh_unpackrow                 ; -> ax=real row, bx=this record's
                                       ; sheet (see the stage 2.0 comment
                                       ; above the cell record layout)
    cmp bx, [sh_cursheet]
    jne .recskip                      ; a save only ever writes the CURRENT
                                       ; sheet - the array may hold other
                                       ; sheets' records too, interleaved
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]
    mov [sh_wrec_col], ax
    mov word [sh_wrec_foff], 0xFFFF   ; ...and this cell's formula, if it has
    test byte [es:si+4], 1            ; one: SYLK carries the EXPRESSION in a
    jz .noformula_w                   ; ;E field beside the cached ;K value,
    mov ax, [es:si+SH_C_FOFF]         ; which is what makes a saved sheet a
    mov [sh_wrec_foff], ax            ; spreadsheet rather than a table of
.noformula_w:                         ; numbers
    call sh_cellval_to_acc_si         ; bank the whole value: the row and
    push si                           ; column are formatted through sh_numbuf
    push di                           ; before it is wanted, so it cannot be
    mov si, sh_acc                    ; turned into text here
    mov di, sh_wrec_dval
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop di
    pop si
    mov ax, [es:si+SH_C_VAL]
    mov [sh_wrec_val], ax
    mov al, [es:si+5]
    mov [sh_wrec_fmt], al
    mov al, [es:si+SH_C_AUX]
    mov [sh_wrec_aux], al
    mov al, [es:si+SH_C_TYPE]         ; stage 4.5: and the tag, because a LABEL
    mov [sh_wrec_type], al            ; goes out as a QUOTED K field rather
    mov ax, [es:si+SH_C_FOFF]         ; than as a number, and its characters
    mov [sh_wrec_toff], ax            ; come from the same arena a formula's do
    pop es                            ; ES = stgseg again

    mov si, sh_s_c
    call sh_stgput
    mov ax, [sh_wrec_col]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_stgput
    mov si, sh_s_y
    call sh_stgput
    mov ax, [sh_wrec_row]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_stgput
    cmp word [sh_wrec_foff], 0xFFFF   ; ";E<expr>" comes BEFORE ";K", the
    je .noexpr                        ; order a real file from the period uses
    push si
    push di
    mov ax, [sh_wrec_col]             ; every relative offset is measured from
    mov [sh_rc_ccol], ax              ; the cell being written
    mov ax, [sh_wrec_row]
    mov [sh_rc_crow], ax
    push es
    mov es, [sh_txtseg]               ; copy the formula text out of the arena
    mov si, [sh_wrec_foff]            ; into DS, where the converter reads
    mov di, sh_rwsrc
    mov cx, SH_EDITMAX
.ecopy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .ecopied
    inc si
    inc di
    dec cx
    jnz .ecopy
    mov byte [di], 0
.ecopied:
    pop es
    mov si, sh_rwsrc
    call sh_formula_to_r1c1
    pop di
    pop si
    mov si, sh_s_e
    call sh_stgput
    mov si, sh_rwdst
    call sh_stgput
.noexpr:
    mov si, sh_s_k
    call sh_stgput
    cmp byte [sh_wrec_type], SH_T_TEXT
    je .ktext
    cmp byte [sh_wrec_type], SH_T_ERR ; ...and an ERROR is its NAME, bare. The
    je .kerr                          ; leading '#' is what tells it from a
    push si                           ; SYLK's K field IS a decimal literal,
    push di                           ; so the full value goes out, not a
    mov si, sh_wrec_dval              ; truncation of it
    call fp_unpack_a
    mov di, sh_numbuf
    mov ax, 10
    call fp_ftoa
    pop di
    pop si
    mov si, sh_numbuf
    call sh_stgput
    jmp .kdone
.kerr:
    ; number, on both sides - no number starts with one, and SYLK has no type
    ; field to consult. Writing the value UNDERNEATH an error instead (a zero)
    ; is what this did, and it turned #DIV/0! into a perfectly ordinary 0 on
    ; the next load.
    push ax
    mov al, [sh_curaux]               ; sh_errname names the CURRENT cell, and
    push ax                           ; a save is not a paint - bank what the
    mov al, [sh_wrec_aux]             ; painter left there
    mov [sh_curaux], al
    call sh_errname                   ; -> sh_numbuf
    pop ax
    mov [sh_curaux], al
    pop ax
    mov si, sh_numbuf
    call sh_stgput
    jmp .kdone
.ktext:
    ; A LABEL'S K FIELD IS QUOTED, and that is the whole of how SYLK tells text
    ; from a number - there is no type field to consult, on either side.
    ; An embedded quote is DOUBLED, because the charset gate admits one now and
    ; a bare one would end the field early and leave the rest of the label
    ; looking like malformed SYLK.
    mov al, 34
    call sh_stgputb
    mov si, [sh_wrec_toff]
.kt:
    push es
    mov es, [sh_txtseg]
    mov al, [es:si]
    pop es
    or al, al
    jz .ktend
    inc si
    cmp al, 34
    jne .kt1
    call sh_stgputb                   ; doubled: a quote inside a label
.kt1:
    call sh_stgputb
    jmp .kt
.ktend:
    mov al, 34
    call sh_stgputb
.kdone:
    mov si, sh_s_crlf
    call sh_stgput

    mov al, [sh_wrec_fmt]
    and al, (SH_FMT_ALIGN_MASK | SH_FMT_NUM_MASK)
    jz .noformat                      ; bold/underline alone don't get an F
                                       ; record - real SYLK has no code for
                                       ; either, see sh_parsefrec's comment
    mov si, sh_s_sylk_fx               ; "F;X"
    call sh_stgput
    mov ax, [sh_wrec_col]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_stgput
    mov si, sh_s_y
    call sh_stgput
    mov ax, [sh_wrec_row]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_stgput
    mov si, sh_s_sylk_ff                ; ";F"
    call sh_stgput
    mov bl, [sh_wrec_fmt]
    and bl, SH_FMT_NUM_MASK
    mov cl, SH_FMT_NUM_SHIFT
    shr bl, cl
    mov al, '$'
    cmp bl, SH_FMT_NUM_CURRENCY
    je .c1ok
    mov al, 'G'
.c1ok:
    call sh_stgputb                      ; c1
    mov al, '0'
    call sh_stgputb                      ; n (digit count - always 0, this
                                         ; app's values are whole numbers)
    mov al, [sh_wrec_fmt]
    and al, SH_FMT_ALIGN_MASK
    mov cl, SH_FMT_ALIGN_SHIFT
    shr al, cl
    cmp al, SH_FMT_ALIGN_LEFT
    je .c2l
    cmp al, SH_FMT_ALIGN_CENTER
    je .c2c
    cmp al, SH_FMT_ALIGN_RIGHT
    je .c2r
    mov al, 'G'
    jmp .c2ok
.c2l:
    mov al, 'L'
    jmp .c2ok
.c2c:
    mov al, 'C'
    jmp .c2ok
.c2r:
    mov al, 'R'
.c2ok:
    call sh_stgputb                      ; c2
    cmp bl, SH_FMT_NUM_COMMA
    jne .nok
    mov si, sh_s_k
    call sh_stgput                      ; ";K"
.nok:
    mov si, sh_s_crlf
    call sh_stgput
.noformat:
    jmp .recnext
.recskip:
    pop es
.recnext:
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    jmp .rec
.footer:
    mov si, sh_s_end
    call sh_stgput
    mov [sh_stagelen], di

    mov ax, [sh_stgseg]
    mov es, ax
    xor bx, bx
    mov cx, [sh_stagelen]
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_WRITE
    jc .werr
    mov word [sh_msg], sh_m_saved
    cmp byte [sh_trunc], 0            ; cells dropped for room must not be
    je .wdone                         ; reported as a plain success
    mov word [sh_msg], sh_m_trunc
    jmp .wdone
.werr:
    call sh_setferr
.wdone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_stgput - append DS:SI (NUL-terminated) to ES:DI, advancing DI. ES must
; already be the staging segment (the caller's job); no NUL is written to
; the destination, since the staging buffer is a raw byte stream whose
; total length is tracked separately, not a re-readable C string.
; -----------------------------------------------------------------------------
sh_stgput:
    push ax
.loop:
    mov al, [si]
    or al, al
    jz .done
    mov [es:di], al
    inc si
    inc di
    jmp .loop
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_doread - read [sh_name], format chosen by its extension (see sh_dowrite)
; -----------------------------------------------------------------------------
sh_doread:
    push si
    push di
    mov si, sh_name
    mov di, sh_s_ext_dif
    call sh_nameends
    pop di
    pop si
    jc .dif
    push si
    push di
    mov si, sh_name
    mov di, sh_s_ext_biff
    call sh_nameends
    pop di
    pop si
    jc .biff
    jmp sh_doread_sylk
.dif:
    jmp sh_doread_dif
.biff:
    jmp sh_doread_biff

; -----------------------------------------------------------------------------
; sh_doread_sylk - read [sh_name] as SYLK, replacing the sheet
; -----------------------------------------------------------------------------
sh_doread_sylk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov es, [sh_stgseg]
    xor bx, bx
    mov cx, SH_STAGE_MAX
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_READ              ; out: DX:AX = bytes read, or CF=1
    jc .rerr

    mov word [sh_ncells], 0
    mov word [sh_txtlen], 0           ; "replacing the sheet" means the old
    mov word [sh_nbord], 0            ; document's arena text, borders and
    mov word [sh_nnote], 0            ; notes too, not just its cells
    mov cx, ax                        ; a file this small never exceeds 64KB
    xor si, si
    call sh_parseslk
    mov word [sh_msg], sh_m_loaded
    jmp .out
.rerr:
    call sh_setferr
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
; File I/O: DIF write and read - the "expanded file format" alongside SYLK
; (SPEC-free, this project's own subset - round-trips against itself, like
; the SYLK subset already does, not certified interchange with a specific
; external product, but DOES follow the real per-cell data-line grammar so
; a genuine DIF-reading program can open it). DIF is fundamentally DENSE -
; it declares a column and row count up front and must then supply a value
; for every cell in that rectangle - so unlike SYLK's sparse C-records, the
; write walks the sheet's USED BOUNDING BOX (sh_difbbox), not the full
; 256x16384 grid: a handful of cells clustered near the origin, which is
; what this stage's sheets actually look like, stays a small file; the
; roadmap's full grid size is a ceiling on what a cell address can BE, not
; a promise that every format scales to a dense encoding of all of it.
;
; Each occupied cell is written as real DIF's numeric data item: type 0
; (NUMERIC), the value, then the literal value-indicator keyword V (valid)
; on its own line - NOT a comment string. An earlier version of this
; writer got this wrong (it emitted an empty quoted comment string, "",
; where V belongs, since a type-0 item has no comment-string line at all
; in the real format) and marked a gap in the bounding box as type 1
; (STRING) with the bare word NA where a quoted string was required; both
; are fixed now - a gap is type 0 with the NA indicator instead, per the
; real spec's own "0 - numeric type ... indicator: V/NA/ERROR/TRUE/FALSE"
; rule. On read, an indicator other than V (a foreign file's NA, ERROR,
; TRUE, or FALSE) just means "leave this cell blank", the same as this
; app's own concept of empty. Like SYLK, only the cached VALUE is carried -
; a formula's source text is not, and per the user's explicit direction
; this stage does NOT extend DIF with any per-cell formatting: real DIF
; has no such concept (unlike real SYLK, which has actual P/font records -
; see the SYLK section below), so this format only ever carries values.
; =============================================================================

; -----------------------------------------------------------------------------
; sh_nameends - in: SI=name (NUL-terminated), DI=suffix (NUL-terminated);
; out: CF=1 if name ends with suffix (case-sensitive: 8.3 names arrive
; already uppercase from the kernel, and so do the suffixes this file
; compares against)
; -----------------------------------------------------------------------------
sh_nameends:
    push ax
    push bx
    push cx
    push si
    push di
    xor cx, cx
    mov bx, si
.namelen:
    cmp byte [bx], 0
    je .havenamelen
    inc bx
    inc cx
    jmp .namelen
.havenamelen:
    push cx                          ; CX = strlen(name)
    xor cx, cx
    mov bx, di
.suflen:
    cmp byte [bx], 0
    je .havesuflen
    inc bx
    inc cx
    jmp .suflen
.havesuflen:
    pop bx                           ; BX = strlen(name), CX = strlen(suffix)
    cmp cx, bx
    ja .no                           ; suffix longer than the whole name
    mov ax, si
    add ax, bx
    sub ax, cx                       ; AX = name + (namelen - suflen)
    mov si, ax
.cmp:
    or cx, cx
    jz .yes
    mov al, [si]
    cmp al, [di]
    jne .no
    inc si
    inc di
    dec cx
    jmp .cmp
.yes:
    stc
    jmp .out
.no:
    clc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_difbbox - the sheet's used bounding box (0,0)-(sh_bbcol,sh_bbrow),
; both 0 for an empty sheet. sh_bbrow is free (the array is row-sorted, so
; it is just the last record's row); sh_bbcol needs a scan.
; -----------------------------------------------------------------------------
sh_difbbox:
    push ax
    push bx
    push cx
    push si
    push es
    mov word [sh_bbrow], 0
    mov word [sh_bbcol], 0
    cmp word [sh_ncells], 0
    je .out
    mov es, [sh_cellseg]
    xor cx, cx
.scan:
    cmp cx, [sh_ncells]
    jae .out
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov si, ax                        ; si = this record's byte offset
    mov ax, [es:si]                   ; packed row/sheet (stage 2.0)
    call sh_unpackrow                 ; -> ax=real row, bx=sheet
    cmp bx, [sh_cursheet]
    jne .next                         ; a sheet's records aren't
                                       ; necessarily contiguous from index 0,
                                       ; so this scans every record rather
                                       ; than assuming the last one is ours
    cmp ax, [sh_bbrow]
    jbe .rowok
    mov [sh_bbrow], ax
.rowok:
    mov ax, [es:si+2]                 ; this record's col
    cmp ax, [sh_bbcol]
    jbe .next
    mov [sh_bbcol], ax
.next:
    inc cx
    jmp .scan
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_dowrite_dif - write the sheet to [sh_name] as DIF
; -----------------------------------------------------------------------------
sh_dowrite_dif:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call sh_difbbox
    mov byte [sh_trunc], 0
    mov es, [sh_stgseg]
    xor di, di
    mov si, sh_s_dif_hdr1
    call sh_stgput
    mov ax, [sh_bbcol]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_stgput
    mov si, sh_s_dif_hdr2
    call sh_stgput
    mov ax, [sh_bbrow]
    inc ax
    call sh_itoa
    mov si, sh_numbuf
    call sh_stgput
    mov si, sh_s_dif_hdr3
    call sh_stgput

    mov word [sh_wrow], 0
.rloop:
    mov ax, [sh_wrow]
    cmp ax, [sh_bbrow]
    ja .footer
    mov ax, di
    add ax, 16
    cmp ax, SH_STAGE_MAX
    ja .truncf                        ; truncate - documented, same spirit
                                       ; as the SYLK writer's own room check
    mov si, sh_s_dif_bot
    call sh_stgput
    mov word [sh_wcol], 0
.cloop:
    mov ax, [sh_wcol]
    cmp ax, [sh_bbcol]
    ja .rnext
    mov ax, di
    add ax, SH_EDITMAX + 24           ; worst case is now a LABEL and its
    cmp ax, SH_STAGE_MAX              ; quotes, not "0,-32768\r\nV\r\n"
    ja .truncf
    mov ax, [sh_wcol]
    mov bx, [sh_wrow]
    call sh_getcell2
    jnc .na
    cmp byte [sh_curtype], SH_T_TEXT   ; stage 4.5: DIF's type 1 is STRING -
    je .dtext                          ; "1,0" then the quoted text on the
    cmp byte [sh_curtype], SH_T_ERR    ; ...and an ERROR takes DIF's own ERROR
    je .derr                           ; indicator. DIF cannot say WHICH error,
    mov si, sh_s_dif_zc                ; following line, which is the real
    call sh_stgput                     ; format's own grammar
    push si                            ; ...and the value goes out as a FULL
    push di                            ; DECIMAL. This wrote `mov ax, dx` -
    mov si, sh_acc                     ; the TRUNCATED integer - so a sheet
    call fp_unpack_a                   ; holding 3.5 saved to DIF as 3, in
    mov di, sh_numbuf                  ; silence, and reloaded as 3. Stage 4.0
    mov ax, 10                         ; converted SYLK's K field and left this
    call fp_ftoa                       ; one behind; DIF's numeric item has
    pop di                             ; never been restricted to integers.
    pop si
    mov si, sh_numbuf
    call sh_stgput
    mov si, sh_s_crlf
    call sh_stgput
    mov si, sh_s_dif_v
    call sh_stgput
    jmp .cnext
.dtext:
    mov si, sh_s_dif_1c
    call sh_stgput                     ; "1,0" CRLF, then the quoted string
    mov al, 34
    call sh_stgputb
    mov si, [sh_curtoff]
.dt:
    push es
    mov es, [sh_txtseg]
    mov al, [es:si]
    pop es
    or al, al
    jz .dtend
    inc si
    cmp al, 34                         ; DIF has no escape for an embedded
    je .dt                             ; quote, so one is DROPPED rather than
    cmp al, 13                         ; written out to break the line - the
    je .dt                             ; label loses a character, the file
    cmp al, 10                         ; stays parseable
    je .dt
    call sh_stgputb
    jmp .dt
.dtend:
    mov al, 34
    call sh_stgputb
    mov si, sh_s_crlf
    call sh_stgput
    jmp .cnext
.derr:
    mov si, sh_s_dif_err0              ; but a file that says ERROR is honest,
    call sh_stgput                     ; where the 0 underneath one is a lie
    jmp .cnext
.na:
    mov si, sh_s_dif_na0
    call sh_stgput
.cnext:
    mov ax, [sh_wcol]
    inc ax
    mov [sh_wcol], ax
    jmp .cloop
.rnext:
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    jmp .rloop
.truncf:
    mov byte [sh_trunc], 1            ; the buffer filled before the bounding
                                       ; box was covered - the header already
                                       ; promised the full box, so say so
.footer:
    mov si, sh_s_dif_eod
    call sh_stgput
    mov [sh_stagelen], di

    mov ax, [sh_stgseg]
    mov es, ax
    xor bx, bx
    mov cx, [sh_stagelen]
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_WRITE
    jc .werr
    mov word [sh_msg], sh_m_saved
    cmp byte [sh_trunc], 0            ; cells dropped for room must not be
    je .wdone                         ; reported as a plain success
    mov word [sh_msg], sh_m_trunc
    jmp .wdone
.werr:
    call sh_setferr
.wdone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_difskipline - in: ES:SI, DI=end (exclusive); out: SI advanced past the
; next CR/LF run (any mix of the two, so both conventions read correctly)
; -----------------------------------------------------------------------------
sh_difskipline:
    push ax
.scan:
    cmp si, di
    jae .out
    mov al, [es:si]
    inc si
    cmp al, 13
    je .eat
    cmp al, 10
    je .eat
    jmp .scan
.eat:
    cmp si, di
    jae .out
    mov al, [es:si]
    cmp al, 13
    je .eat2
    cmp al, 10
    je .eat2
    jmp .out
.eat2:
    inc si
    jmp .eat
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_doread_dif - read [sh_name] as DIF, replacing the sheet. Skips the
; fixed 12-line header (this project's own writer always emits exactly
; that many), then reads row markers ("-1,0"/"BOT" or the closing "EOD")
; and, within a row, cell records ("0,<value>" or "1,0"/"NA").
; -----------------------------------------------------------------------------
sh_doread_dif:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov es, [sh_stgseg]
    xor bx, bx
    mov cx, SH_STAGE_MAX
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_READ               ; out: DX:AX = bytes read, or CF=1
    jc .rerr

    mov word [sh_ncells], 0
    mov word [sh_txtlen], 0            ; "replacing the sheet" - see
    mov word [sh_nbord], 0             ; sh_doread_sylk's same three
    mov word [sh_nnote], 0
    mov es, [sh_stgseg]
    mov di, ax                         ; DI = end (bytes read)
    xor si, si
    mov cx, 12                         ; the header is always 12 lines
.skiphdr:
    call sh_difskipline
    loop .skiphdr
    mov word [sh_wrow], 0xFFFF         ; becomes 0 at the first BOT
.rowloop:
    cmp si, di
    jae .done
    call sh_difskipline                ; the "-1,0" line
    cmp si, di
    jae .done
    mov al, [es:si]
    cmp al, 'E'
    je .done                           ; EOD
    call sh_difskipline                ; the "BOT" line
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    mov word [sh_wcol], 0
.cellloop:
    cmp si, di
    jae .done
    mov al, [es:si]
    cmp al, '-'
    je .rowloop                        ; the next row's "-1,0"
    cmp al, '1'
    je .dstring                        ; stage 4.5: type 1 IS a string item,
    cmp al, '0'                        ; and this app writes them now - the
    jne .skipunknown                   ; comment that used to sit here said it
                                        ; never would, and skipping them was
                                        ; the right defence at the time
    add si, 2                          ; past "0,"
    mov bx, di
    call sh_esatof                     ; a full decimal into sh_acc, not
    call sh_difskipline                ; sh_pint's integer: the writer emits
    cmp si, di                         ; 3.5 and this read it back as 3
    jae .cellnext
    cmp word [sh_wcol], SH_COLS        ; a file with more data items per row
    jae .notvalid                      ; than the grid has columns walks
                                       ; sh_wcol off it - drop the surplus,
                                       ; but still consume its indicator line
    cmp byte [es:si], 'V'              ; the real DIF value-indicator: V
    je .isvalid                        ; (valid) is the ordinary one; ERROR is
    cmp byte [es:si], 'E'              ; the other one this app writes, and
    jne .notvalid                      ; NA/TRUE/FALSE from a foreign file
    mov dl, SH_ERR_NA                  ; still just mean "leave this cell
    mov ax, [sh_wcol]                  ; blank" here.
    mov bx, [sh_wrow]                  ; #N/A is what an ERROR comes back as:
    call sh_seterr                     ; the file said a value was not
    jmp .notvalid                      ; available and could not say more, and
.isvalid:                              ; #N/A is the one error that means
    mov ax, [sh_wcol]                  ; exactly that. Guessing a specific one
    mov bx, [sh_wrow]                  ; would be inventing what the file does
    call sh_setvald                    ; not contain
.notvalid:
    call sh_difskipline                ; the indicator line
    jmp .cellnext
.dstring:
    call sh_difskipline                ; past the "1,0" line; the quoted text
    cmp si, di                         ; is the line after it
    jae .cellnext
    cmp word [sh_wcol], SH_COLS        ; off-grid column: skip the text line
    jae .notvalid                      ; too (see the numeric item above)
    cmp byte [es:si], 34
    jne .notvalid                      ; not quoted: not a string item this
    inc si                             ; app can use - skip the line
    push di                            ; DI is the buffer END and is needed
    push di                            ; again below, so it is banked twice:
    mov di, SH_TEXPR                   ; once for the loop bound in CX terms
    mov cx, SH_EDITMAX                 ; and once as the cursor here
    pop bx                             ; BX = the end offset
.ds:
    jcxz .dsend
    cmp si, bx
    jae .dsend
    mov al, [es:si]
    cmp al, 34
    je .dsend
    cmp al, 13
    je .dsend
    cmp al, 10
    je .dsend
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .ds
.dsend:
    mov byte [di], 0
    pop di
    push si
    mov ax, [sh_wcol]
    mov bx, [sh_wrow]
    mov si, SH_TEXPR
    call sh_settext
    pop si
    call sh_difskipline
    jmp .cellnext
.skipunknown:
    call sh_difskipline
.cellnext:
    mov ax, [sh_wcol]
    inc ax
    mov [sh_wcol], ax
    jmp .cellloop
.done:
    mov word [sh_msg], sh_m_loaded
    jmp .out
.rerr:
    call sh_setferr
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
; File I/O: BIFF write and read - stage 1.4's "BIFF3/4 support". Frames
; records the real way (BOF/EOF opcodes, a real per-cell record type), but
; the BOF payload's exact byte-level convention beyond the opcode and the
; dt field is genuine best-effort (documented, not certified) - the same
; honesty this project already applies to SYLK/DIF. The one point that IS
; deliberately spec-correct, because it is load-bearing for round-tripping
; negative numbers through a real reader: cell values are written as RK
; records (opcode 0x027E, real BIFF3+), not the older INTEGER record
; (0x0202) - INTEGER's 16-bit value field is UNSIGNED (0..65535 only), so
; it cannot hold this app's negative cells at all, whereas RK's 4-byte
; packed value has a signed-30-bit-integer subtype that fits this app's
; signed 16-bit cells with room to spare and needs no IEEE-754 float
; encoding. On read, only that same subtype is understood - a foreign RK
; using the "multiplied by 100" or plain-float subtype, or a NUMBER
; (0x0203) float record, is out of this subset's scope and is skipped,
; leaving that cell blank, rather than guessed at.
; Like SYLK, this is sparse (one record per occupied cell, walking the
; sorted array directly), not dense like DIF, since a binary record already
; carries its own row/col and needs no bounding box. Like both SYLK and
; DIF, only the cached VALUE survives a round trip - a formula's source
; text is not persisted.
;
; Stage 1.6's bold/underline/alignment/number-format DOES persist here,
; and unlike SYLK/DIF's own invented extensions, this one uses real BIFF3/4
; structure: 4 FONT records (opcode 0x0231) for the 4 bold/underline
; combinations, 64 XF records (opcode 0x0443) - one per possible SH_FMT_*
; byte value - and each cell's RK record points at its XF by index. That
; 1:1 pairing between our format byte and the BIFF ixfe is deliberate: it
; means neither side needs a lookup table to go from "this cell's 6
; format bits" to "this cell's XF index" or back, at the cost of always
; writing all 64 XFs whether or not the sheet uses every combination (at
; most 64*16 + 4*15 bytes - a fixed, small overhead). No FORMAT records are
; written at all: General/Currency/Comma/Percent all land on real BIFF
; built-in number-format ids (0/5/3/9), each already a 0-decimal-place
; form - the only kind this app's whole-number cells ever need.
; =============================================================================

; -----------------------------------------------------------------------------
; sh_biffw - append raw word AX to ES:DI (little-endian, matching BIFF and
; this CPU), advancing DI by 2. ES must already be the staging segment.
; -----------------------------------------------------------------------------
sh_biffw:
    mov [es:di], ax
    add di, 2
    ret

; -----------------------------------------------------------------------------
; sh_stgputb - append raw byte AL to ES:DI, advancing DI by 1. ES must
; already be the staging segment (the caller's job, as with sh_stgput).
; Used for a BIFF length-prefix byte (sh_biffw only writes whole words) and
; for a SYLK F record's single-character format codes.
; -----------------------------------------------------------------------------
sh_stgputb:
    mov [es:di], al
    inc di
    ret

; -----------------------------------------------------------------------------
; sh_rkenc - in: AX = signed 16-bit cell value; out: DX:AX = that value
; packed as an RK "signed 30-bit integer, not multiplied by 100" (bit1=1,
; bit0=0 of the low word) - AX is the low word of the 4-byte RK value, DX
; the high word, matching write order (low word first, then high).
; -----------------------------------------------------------------------------
sh_rkenc:
    push cx
    cwd                    ; DX:AX = AX sign-extended to 32 bits
    mov cx, 2
.shl:
    shl ax, 1
    rcl dx, 1
    loop .shl
    or ax, 0x0002
    pop cx
    ret

; -----------------------------------------------------------------------------
; sh_rkdec_d - in: DX:AX = a packed RK value; out: sh_acc = it, as a double.
;
; ALL FOUR SUBTYPES, where sh_rkdec below handles only the one an integer
; could represent. The other three - divided by 100, and the float form that
; is the TOP 32 BITS of an IEEE-754 double with the low 32 zero - are exactly
; the ones a 16-bit integer had to refuse, and refusing them meant silently
; dropping cells from any file Excel itself had written.
;
;   bit0 = 1: the value is 100x what was meant
;   bit1 = 1: integer form, a signed 30-bit value in the top 30 bits
;   bit1 = 0: float form, the top 32 bits of a double
; -----------------------------------------------------------------------------
sh_rkdec_d:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                        ; bl bit0/bit1 = the subtype flags
    test al, 0x02
    jz .float
    mov cx, 2                         ; integer form: an arithmetic shift, so
.shr:                                 ; a negative value stays negative
    sar dx, 1
    rcr ax, 1
    loop .shr
    push bx
    call fp_i32_to_a                  ; DX:AX (signed 32) -> A
    pop bx
    jmp .div100
.float:
    and ax, 0xFFFC                    ; the two flag bits are not mantissa
    mov word [sh_acc], 0              ; the low 32 bits of the double are zero
    mov word [sh_acc+2], 0            ; by construction in this form
    mov [sh_acc+4], ax
    mov [sh_acc+6], dx
    push bx
    call sh_acc_load_a
    pop bx
.div100:
    test bl, 0x01
    jz .out
    mov ax, 100                       ; bit0: it was scaled up by a hundred
    call fp_i2b
    call fp_div
.out:
    call sh_acc_store
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_i32_to_a - DX:AX, a SIGNED 32-bit integer, becomes A.
fp_i32_to_a:
    push ax
    push bx
    push cx
    push di
    mov byte [fp_as], 0
    or dx, dx
    jns .abs
    mov byte [fp_as], 1
    neg dx                            ; the 32-bit negate idiom
    neg ax
    sbb dx, 0
.abs:
    mov [fp_am0], ax
    mov [fp_am1], dx
    mov word [fp_am2], 0
    mov word [fp_am3], 0
    mov word [fp_ae], 0
    mov bx, fp_am0
    mov di, fp_ae
    call fp_norm
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rkdec - in: DX:AX = a packed RK value (AX low word, DX high word); out:
; CF=0 and AX=the signed 16-bit value if it's the "integer, not multiplied"
; subtype this project writes, else CF=1 (out of this subset's scope - the
; caller should skip the cell rather than guess at a float or *100 value)
; -----------------------------------------------------------------------------
sh_rkdec:
    test al, 0x01
    jnz .unsupported        ; multiplied by 100
    test al, 0x02
    jz .unsupported         ; plain IEEE-754 float form, top 32 bits only
    push cx
    mov cx, 2
.shr:
    sar dx, 1
    rcr ax, 1
    loop .shr
    pop cx
    clc
    ret
.unsupported:
    stc
    ret

; -----------------------------------------------------------------------------
; sh_biff_numfmt_from_id - in: AL = a real BIFF built-in number-format id;
; out: AL = this app's SH_FMT_NUM_* code (General for anything that isn't
; one of the four ids sh_biff_numfmt_tab itself ever writes - a custom
; FORMAT record's id, or a built-in this app doesn't have an equivalent
; for, both just degrade to General rather than guessed at)
; -----------------------------------------------------------------------------
sh_biff_numfmt_from_id:
    cmp al, 0x05
    je .cur
    cmp al, 0x03
    je .comma
    cmp al, 0x09
    je .pct
    xor al, al
    ret
.cur:
    mov al, SH_FMT_NUM_CURRENCY
    ret
.comma:
    mov al, SH_FMT_NUM_COMMA
    ret
.pct:
    mov al, SH_FMT_NUM_PERCENT
    ret

; -----------------------------------------------------------------------------
; sh_biff_applyfmt - in: sh_wrec_col/sh_wrec_row (the cell just written by
; sh_setval), sh_wrec_xf (its BIFF ixfe); combines sh_xf_fmt/sh_xf_font (if
; the xf index is one this reader tracked) with sh_font_tab (if that xf's
; font index is one it tracked) into this app's own format byte, and
; writes it to the cell. A cell whose xf or font fell outside the tracked
; caps is left at format 0 (General, unformatted) rather than guessed at.
; -----------------------------------------------------------------------------
sh_biff_applyfmt:
    push ax
    push bx
    push cx
    push di
    push es
    mov bx, [sh_wrec_xf]
    cmp bx, SH_BIFF_XF_CAP
    jae .out
    mov di, sh_xf_fmt
    add di, bx
    mov al, [di]                       ; al = align|numfmt packed byte
    mov di, sh_xf_font
    add di, bx
    mov cl, [di]                       ; cl = this xf's font index
    xor ch, ch
    cmp cx, SH_BIFF_FONT_CAP
    jae .noboldunder
    mov di, sh_font_tab
    add di, cx
    or al, [di]                        ; fold in bold/underline
.noboldunder:
    mov cl, al                         ; stash the finished byte in cl
                                        ; across the cell lookup below
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_findcell
    jnc .out
    mov es, [sh_cellseg]
    mov [es:di+5], cl
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_dowrite_biff - write the sheet to [sh_name] as BIFF. Walks the sorted
; cell array directly, same shape as sh_dowrite_sylk.
; -----------------------------------------------------------------------------
sh_dowrite_biff:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call sh_sheets_used               ; ONE sheet keeps the BIFF3 stream this
    cmp ax, 2                         ; writer has always produced, byte for
    jb .single                        ; byte. More than one needs a WORKBOOK,
    call sh_biff_workbook             ; and BIFF3 has no such thing (81.10.5)
    jmp .wdone
.single:
    mov es, [sh_stgseg]
    xor di, di

    mov ax, 0x0209                   ; BOF (BIFF3). NOT BIFF4, deliberately:
                                      ; a reader is BACKWARD compatible and
                                      ; not forward compatible, so Excel 4 and
                                      ; everything after it read a BIFF3 file
                                      ; happily, while a program that knows
                                      ; only BIFF3 cannot read a BIFF4 one -
                                      ; it does not even recognise the BOF.
                                      ; Emitting the older stream is therefore
                                      ; strictly the wider audience, and costs
                                      ; nothing: every record this writer uses
                                      ; exists in BIFF3.
    call sh_biffw
    mov ax, 6
    call sh_biffw
    mov ax, 0x0300                   ; vers = BIFF3, matching the BOF above
    call sh_biffw
    mov ax, 0x0010                   ; dt = worksheet
    call sh_biffw
    xor ax, ax                       ; BIFF3's BOF carries two more bytes,
    call sh_biffw                    ; documented as "not used" - BIFF2's
                                      ; record is the four-byte one, and a
                                      ; reader that trusts the length would
                                      ; desynchronise on the short form

    call sh_biff_fontsxfs

    mov byte [sh_trunc], 0
    mov ax, [sh_cursheet]            ; the BIFF3 stream is ONE sheet, and says
    mov [sh_wsheet], ax              ; so in the status bar when there are more
    call sh_biff_cells
.footer:
    mov ax, 0x000A                    ; EOF
    call sh_biffw
    xor ax, ax
    call sh_biffw
    mov [sh_stagelen], di

    mov ax, [sh_stgseg]
    mov es, ax
    xor bx, bx
    mov cx, [sh_stagelen]
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_WRITE
    jc .werr
    mov word [sh_msg], sh_m_saved
    cmp byte [sh_trunc], 0            ; cells dropped for room must not be
    je .wdone                         ; reported as a plain success
    mov word [sh_msg], sh_m_trunc
    jmp .wdone
.werr:
    call sh_setferr
.wdone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_doread_biff - read [sh_name] as BIFF, replacing the sheet. Skips the
; BOF record (and every other record type this subset doesn't know, by its
; length - so a real file's extra records, e.g. a workbook-globals BOF,
; FONT, or FORMAT record, are tolerated rather than misparsed), then
; applies every RK cell record found whose value is this subset's
; understood subtype, until EOF or the buffer end (a truncated/foreign
; file).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sh_biff_formula - a FORMULA record (0206H in BIFF3) for the cell being
; written. out: CF=0 it was written; CF=1 = not expressible, and the caller
; falls through to RK/NUMBER exactly as before.
;
; The result field carries the CACHED VALUE as an IEEE double, which is what
; the record is for: a reader that does not recalculate still shows the right
; number, and one that does gets the same answer from the tokens.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sh_biff_fontsxfs - the four FONT records and the sixty-four XF records, at
; ES:DI. [sh_wb_xf4] picks BIFF4's XF opcode and body layout over BIFF3's.
;
; Extracted so the BIFF4 workbook's globals section and the BIFF3 stream share
; one copy: they differ in two version-dependent details and in nothing else,
; and two copies would have drifted on the first change to a font.
; -----------------------------------------------------------------------------
sh_biff_fontsxfs:
    ; 4 FONT records (indices 0-3: normal, bold, underline, bold+underline)
    ; and 64 XF records (indices 0-63) - one per possible SH_FMT_* byte
    ; value, so a cell's own format byte IS its BIFF ixfe with no lookup
    ; table needed on either side. See the section comment above for why
    ; this pairing is safe and the honesty scope around it.
    mov word [sh_wrow], 0            ; reused as the font index, 0..3
.ffontloop:
    mov ax, [sh_wrow]
    cmp ax, 4
    jae .ffontsdone
    mov ax, 0x0231                   ; FONT (BIFF3/4)
    call sh_biffw
    mov ax, 11                       ; height+options+palette(6) + len(1) +
    call sh_biffw                    ; "Helv"(4)
    mov ax, 200                      ; height: 10pt in twips
    call sh_biffw
    mov bx, [sh_wrow]
    xor ax, ax
    test bl, 1
    jz .fnobold
    or ax, 0x0001                    ; bit0: bold
.fnobold:
    test bl, 2
    jz .fnounder
    or ax, 0x0004                    ; bit2: underlined
.fnounder:
    call sh_biffw
    xor ax, ax                       ; palette index (default)
    call sh_biffw
    mov al, 4
    call sh_stgputb                   ; name length prefix
    mov si, sh_s_biff_fontname
    call sh_stgput                   ; the raw bytes, no NUL (BIFF strings
                                      ; are length-prefixed, not C strings)
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    jmp .ffontloop
.ffontsdone:
    mov word [sh_wrow], 0            ; reused as the XF index, 0..63
.fxfloop:
    mov si, [sh_wrow]
    cmp si, 64
    jae .fxfsdone
    mov ax, 0x0243                   ; XF - or BIFF4's own number and body
    cmp byte [sh_wb_xf4], 0          ; layout, which differ (docs/BIFF-NOTES.md
    je .xfop3                        ; tabulates the two side by side)
    mov ax, 0x0443
.xfop3:                              ; XF (BIFF3). Same twelve bytes as the
                                      ; BIFF4 record in the fields this uses -
                                      ; font index, format index, attributes,
                                      ; alignment, area, border - which is why
                                      ; the body below did not have to change
                                      ; with the opcode. BIFF4's additions to
                                      ; that record (orientation, notably) sit
                                      ; in bits this never sets.
    call sh_biffw
    mov ax, 12
    call sh_biffw
    mov ax, si
    and ax, 3                        ; al = font index (bits0-1 of the
                                      ; format byte: bold, underline)
    mov bx, si
    mov cl, 4
    shr bx, cl
    and bx, 3                        ; bx = our number-format code
    mov ah, [sh_biff_numfmt_tab + bx] ; ah = real BIFF built-in format id
    call sh_biffw                    ; offset0-1: font idx, format idx
    ; THE TWO MIDDLE WORDS ARE SWAPPED BETWEEN THE VERSIONS, and that is the
    ; whole of the layout difference this record's used fields feel:
    ;
    ;   BIFF3 (0243H)                    BIFF4 (0443H)
    ;     +2  XF_TYPE_PROT      (1)        +2  type/prot + parent   (2)
    ;     +3  XF_USED_ATTRIB    (1)        +4  align/vert/orient    (1)
    ;     +4  align + parent    (2)        +5  XF_USED_ATTRIB       (1)
    ;
    ; So BIFF3 writes FC00H then align|FFF0H, and BIFF4 writes FFF0H then
    ; align|FC00H. FCH means "override every inherited attribute", which is the
    ; honest value when no style XF is written, and FFFH is the documented
    ; "no parent" for the same reason.
    mov ax, si                       ; the alignment code, bits 2-3 of the
    mov cl, 2                        ; format byte - it matches XF_HOR_ALIGN
    shr ax, cl                       ; 0-3 (General/Left/Center/Right)
    and ax, 3                        ; directly, with no translation
    mov [sh_wb_align], ax
    cmp byte [sh_wb_xf4], 0
    jne .xf4body
    mov ax, 0xFC00                   ; BIFF3: type/prot, then used-attrib
    call sh_biffw
    mov ax, [sh_wb_align]
    or ax, 0xFFF0
    call sh_biffw
    jmp .xfmid
.xf4body:
    mov ax, 0xFFF0                   ; BIFF4: type/prot + parent, then the
    call sh_biffw                    ; align byte with used-attrib above it
    mov ax, [sh_wb_align]
    or ax, 0xFC00
    call sh_biffw
.xfmid:
    xor ax, ax                       ; XF_AREA_34: no fill
    call sh_biffw
    xor ax, ax                       ; XF_BORDER_34 low word: no borders
    call sh_biffw
    xor ax, ax                       ; XF_BORDER_34 high word
    call sh_biffw
    mov ax, si
    inc ax
    mov [sh_wrow], ax
    jmp .fxfloop
.fxfsdone:

    ret

; =============================================================================
; THE BIFF4 WORKBOOK - the only way this app can save more than one sheet.
;
; SYLK has no notion of a sheet and DIF is a single table, so for those two the
; loss is the format's and all this app can do is say so. BIFF3 is single-sheet
; too: its stream is one BOF...EOF pair. BIFF4 is the first version with a
; workbook, and its shape is (excelfileformat 4.1.2):
;
;     BOF          dt = 0100H, workbook globals
;     FONT x4, XF x64
;     SHEETSOFFSET stream position of the first SHEETHDR
;     SHEETHDR     byte length of the substream, and the sheet's name
;       BOF        dt = 0010H, worksheet
;       cells
;       EOF
;     SHEETHDR ... BOF ... EOF        (once per sheet)
;     EOF          the outer one
;
; WRITTEN ONLY WHEN IT IS NEEDED. A reader is backward compatible and not
; forward compatible, so a BIFF3 file reaches strictly more programs - which is
; why the single-sheet case still emits one and this path exists beside it
; rather than replacing it. A workbook that will not open in a BIFF3-only
; reader is still better than three sheets that were never written at all.
;
; THE SUBSTREAM LENGTH IS BACKPATCHED. SHEETHDR carries the byte length of the
; substream that FOLLOWS it, which is not known until that substream is built.
; The whole file is assembled in the staging segment before a single byte
; reaches the disk, so the header's offset is remembered and the length written
; into it afterwards - which is why this is easy here and would not be if the
; writer streamed.
;
; XF IS 0443H HERE, NOT 0243H, AND ITS BODY IS LAID OUT DIFFERENTLY - see
; docs/BIFF-NOTES.md, which tabulates both. FONT keeps 0231H in both versions.
; =============================================================================
sh_biff_workbook:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [sh_stgseg]
    xor di, di
    mov byte [sh_trunc], 0
    call sh_sheets_used
    mov [sh_wb_map], bx               ; which sheets to write

    mov ax, 0x0409                    ; BOF, BIFF4
    call sh_biffw
    mov ax, 6
    call sh_biffw
    mov ax, 0x0400                    ; vers = BIFF4
    call sh_biffw
    mov ax, 0x0100                    ; dt = workbook globals
    call sh_biffw
    xor ax, ax
    call sh_biffw

    mov byte [sh_wb_xf4], 1           ; the globals' FONT/XF block, in BIFF4
    call sh_biff_fontsxfs             ; opcodes and layout

    mov ax, 0x008E                    ; SHEETSOFFSET
    call sh_biffw
    mov ax, 4
    call sh_biffw
    mov ax, di                        ; the first SHEETHDR begins right after
    add ax, 4                         ; this record's own four bytes
    call sh_biffw
    xor ax, ax
    call sh_biffw

    mov word [sh_wb_i], 0
.sheet:
    mov ax, [sh_wb_i]
    cmp ax, SH_SHEETS
    jae .alldone
    mov cx, ax                        ; is this one used?
    mov ax, 1
    jcxz .nosh
.shl1:
    shl ax, 1
    loop .shl1
.nosh:
    test [sh_wb_map], ax
    jz .next
    cmp byte [sh_trunc], 0
    jne .next

    mov ax, 0x008F                    ; SHEETHDR
    call sh_biffw
    mov ax, 11                        ; 4 length + 1 count + "SheetN"(6)
    call sh_biffw
    mov [sh_wb_lenat], di             ; ...backpatched once the substream is
    xor ax, ax                        ; built, because SHEETHDR carries the
    call sh_biffw                     ; size of what FOLLOWS it
    call sh_biffw
    mov al, 6
    call sh_stgputb
    mov si, sh_s_sheetnm              ; "Sheet"
.nm:
    mov al, [si]
    or al, al
    jz .nmdone
    call sh_stgputb
    inc si
    jmp .nm
.nmdone:
    mov ax, [sh_wb_i]
    add al, '1'
    call sh_stgputb

    mov [sh_wb_subat], di             ; the substream starts here
    mov ax, 0x0409                    ; BOF, worksheet
    call sh_biffw
    mov ax, 6
    call sh_biffw
    mov ax, 0x0400
    call sh_biffw
    mov ax, 0x0010                    ; dt = worksheet
    call sh_biffw
    xor ax, ax
    call sh_biffw

    mov ax, [sh_wb_i]                 ; ...its cells...
    mov [sh_wsheet], ax
    call sh_biff_cells

    mov ax, 0x000A                    ; ...and its EOF
    call sh_biffw
    xor ax, ax
    call sh_biffw

    mov ax, di                        ; backpatch the substream length
    sub ax, [sh_wb_subat]
    mov bx, [sh_wb_lenat]
    mov [es:bx], ax
    mov word [es:bx+2], 0
.next:
    inc word [sh_wb_i]
    jmp .sheet
.alldone:
    mov ax, 0x000A                    ; the workbook's own EOF
    call sh_biffw
    xor ax, ax
    call sh_biffw
    mov [sh_stagelen], di

    mov ax, [sh_stgseg]
    mov es, ax
    xor bx, bx
    mov cx, [sh_stagelen]
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_WRITE
    jc .err
    mov word [sh_msg], sh_m_saved
    cmp byte [sh_trunc], 0            ; cells dropped for room must not be
    je .out                           ; reported as a plain success
    mov word [sh_msg], sh_m_trunc
    jmp .out
.err:
    call sh_setferr
.out:
    mov byte [sh_wb_xf4], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_biff_cells - every cell record for ONE sheet, appended at ES:DI.
; in: [sh_wsheet] = which of the SH_SHEETS grids; ES = staging, DI = cursor
; out: DI advanced past what was written; [sh_trunc] set if the buffer filled
;
; Extracted from sh_dowrite_biff so a BIFF4 WORKBOOK can call it once per
; sheet (81.10.5). The BIFF3 path calls it exactly once, with sh_wsheet set to
; sh_cursheet, and emits the same bytes it always did.
; -----------------------------------------------------------------------------
sh_biff_cells:
    mov byte [sh_trunc], 0
    mov word [sh_wrow], 0            ; reused here as the record index
.rec:
    mov bx, [sh_wrow]
    cmp bx, [sh_ncells]
    jae .cdone
    cmp byte [sh_trunc], 0
    jne .cdone
    mov ax, di
    add ax, 18                       ; the NUMBER record - opcode+len(4) +
                                      ; row+col+xf(6) + double(8), the largest
                                      ; fixed-size cell record (RK is 14,
                                      ; BOOLERR 12; LABEL and FORMULA re-check
                                      ; with their own exact sizes)
    cmp ax, SH_STAGE_MAX
    jbe .room
    mov byte [sh_trunc], 1
    jmp .cdone
.room:
    mov ax, bx
    mov cx, SH_C_SZ
    mul cx
    mov si, ax                        ; SI = this record's offset in cellseg
    push es
    mov es, [sh_cellseg]
    mov ax, [es:si]
    call sh_unpackrow                 ; -> ax=real row, bx=this record's
                                       ; sheet (stage 2.0)
    cmp bx, [sh_wsheet]
    jne .recskip                      ; a save only ever writes the CURRENT
                                       ; sheet - see sh_dowrite_sylk's own
                                       ; copy of this same filter
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]
    mov [sh_wrec_col], ax
    call sh_cellval_to_acc_si         ; the whole value, banked - the choice
    push si                           ; of record below needs all eight bytes
    push di
    mov si, sh_acc
    mov di, sh_wrec_dval
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop di
    pop si
    mov al, [es:si+5]
    mov [sh_wrec_fmt], al
    mov al, [es:si+SH_C_TYPE]
    mov [sh_wrec_type], al            ; stage 4.5: a LABEL takes neither RK nor
    mov al, [es:si+SH_C_AUX]          ; NUMBER, and an ERROR takes neither
    mov [sh_wrec_aux], al             ; either - see .aserr
    mov ax, [es:si+SH_C_FOFF]
    mov [sh_wrec_toff], ax
    mov al, [es:si+4]                 ; ...and a FORMULA cell may take a real
    and al, 1                         ; FORMULA record, if its text is one this
    mov [sh_wrec_hasf], al            ; writer can tokenise
    pop es                            ; ES = stgseg again

    cmp byte [sh_wrec_type], SH_T_TEXT
    je .aslabel
    cmp byte [sh_wrec_hasf], 0
    je .notformula
    call sh_biff_formula              ; CF=0 = it wrote the record
    jnc .recnext
.notformula:
    cmp byte [sh_wrec_type], SH_T_ERR  ; an error whose formula this writer
    je .aserr                          ; could not tokenise still has to go out
                                        ; AS AN ERROR: the number underneath one
                                        ; is zero, and a file saying 0 where the
                                        ; sheet said #DIV/0! is worse than a
                                        ; file that lost the formula

    ; --- which record? -------------------------------------------------------
    ; AN EXACT IN-RANGE INTEGER STILL GOES OUT AS RK, byte for byte as before,
    ; so every file this app has already written is unchanged and a reader that
    ; only knows the old subtype still reads those cells. Anything else - a
    ; fraction, or a magnitude past a signed word - needs the NUMBER record,
    ; which carries the IEEE-754 double verbatim.
    push si
    mov si, sh_wrec_dval
    call fp_unpack_a
    pop si
    call fp_a2i                       ; CF=1: no signed word can hold it
    jc .asnumber
    mov [sh_wrec_val], ax
    call fp_i2a                       ; round-trip it and see if anything was
    push si                           ; lost - 3.5 truncates to 3, and 3 is
    mov si, sh_wrec_dval              ; not the value we were asked to write
    call fp_unpack_b
    pop si
    call fp_cmpab
    jne .asnumber

    mov ax, 0x027E                    ; RK cell record
    call sh_biffw
    mov ax, 10
    call sh_biffw
    mov ax, [sh_wrec_row]
    call sh_biffw
    mov ax, [sh_wrec_col]
    call sh_biffw
    xor ah, ah
    mov al, [sh_wrec_fmt]              ; xf = the format byte itself
    call sh_biffw
    mov ax, [sh_wrec_val]
    call sh_rkenc                     ; -> DX:AX = packed RK value
    call sh_biffw                     ; low word
    mov ax, dx
    call sh_biffw                     ; high word
    jmp .recnext
.asnumber:
    mov ax, 0x0203                    ; NUMBER: row, col, xf, then an 8-byte
    call sh_biffw                     ; IEEE-754 double, little-endian - the
    mov ax, 14                        ; same layout the working form packs to,
    call sh_biffw                     ; so it goes out with no conversion
    mov ax, [sh_wrec_row]
    call sh_biffw
    mov ax, [sh_wrec_col]
    call sh_biffw
    xor ah, ah
    mov al, [sh_wrec_fmt]
    call sh_biffw
    mov ax, [sh_wrec_dval]
    call sh_biffw
    mov ax, [sh_wrec_dval+2]
    call sh_biffw
    mov ax, [sh_wrec_dval+4]
    call sh_biffw
    mov ax, [sh_wrec_dval+6]
    call sh_biffw
    jmp .recnext
.aserr:
    ; BOOLERR, 0205H: the row/col/xf head every cell record shares, then the
    ; value byte and a flag saying whether it is a BOOLEAN or an ERROR. The
    ; code stored is Excel's own ERROR.TYPE number, which is what SH_C_AUX
    ; holds, so it travels with no translation.
    mov ax, 0x0205
    call sh_biffw
    mov ax, 8
    call sh_biffw
    mov ax, [sh_wrec_row]
    call sh_biffw
    mov ax, [sh_wrec_col]
    call sh_biffw
    xor ah, ah
    mov al, [sh_wrec_fmt]
    call sh_biffw
    mov al, [sh_wrec_aux]
    call sh_stgputb
    mov al, 1                          ; fError
    call sh_stgputb
    jmp .recnext
.aslabel:
    ; LABEL, 0204H in BIFF3 - and NOT 0004H, which is BIFF2's. The body is the
    ; same row/col/xf head RK and NUMBER use, then a SIXTEEN-BIT length and the
    ; bytes. BIFF2's LABEL has a one-byte length and a three-byte cell
    ; attribute where the xf index goes, so a file mixing the two conventions
    ; desynchronises the moment a reader trusts the length field.
    push si                            ; measure it first: the length goes in
    push es                            ; the record BEFORE the bytes do
    mov es, [sh_txtseg]
    mov si, [sh_wrec_toff]
    xor cx, cx
.llen:
    cmp byte [es:si], 0
    je .lhavelen
    inc si
    inc cx
    cmp cx, 255                        ; BIFF3 allows 255; the arena string
    jb .llen                           ; cannot be longer than SH_EDITMAX
                                       ; anyway, and this is the format's own
                                       ; ceiling rather than ours
.lhavelen:
    pop es
    pop si
    mov [sh_wrec_len], cx
    mov ax, di                         ; and only now check for room, because
    add ax, cx                         ; the length is what decides how much
    add ax, 12
    cmp ax, SH_STAGE_MAX
    jbe .lroom
    mov byte [sh_trunc], 1
    jmp .recnext
.lroom:
    mov ax, 0x0204                     ; LABEL
    call sh_biffw
    mov ax, [sh_wrec_len]
    add ax, 8                          ; row+col+xf+cch = 8, then the bytes
    call sh_biffw
    mov ax, [sh_wrec_row]
    call sh_biffw
    mov ax, [sh_wrec_col]
    call sh_biffw
    xor ah, ah
    mov al, [sh_wrec_fmt]
    call sh_biffw
    mov ax, [sh_wrec_len]
    call sh_biffw
    mov si, [sh_wrec_toff]
    mov cx, [sh_wrec_len]
    jcxz .recnext
.lput:
    push es
    mov es, [sh_txtseg]
    mov al, [es:si]
    pop es
    inc si
    call sh_stgputb
    dec cx
    jnz .lput
    jmp .recnext
.recskip:
    pop es
.recnext:
    mov ax, [sh_wrow]
    inc ax
    mov [sh_wrow], ax
    jmp .rec
.cdone:
    ret

sh_biff_formula:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    ; DI IS NOT SAVED, AND MUST NOT BE. It is the staging write cursor this
    ; whole writer advances through the file, and sh_biffw both reads and
    ; advances it - so a record emitted here has to leave it moved on, exactly
    ; as the RK and NUMBER paths do. The first version pushed it and then used
    ; it as the text-copy destination, which put every FORMULA record into the
    ; staging segment at sh_rwsrc's offset - written, past the cursor, and
    ; invisible to the file the writer then saved. BX carries the copy instead.
    mov es, [sh_txtseg]               ; the formula text out of the arena into
    mov si, [sh_wrec_toff]            ; DS, where the emitter reads
    mov bx, sh_rwsrc
    mov cx, SH_EDITMAX
.copy:
    mov al, [es:si]
    mov [bx], al
    or al, al
    jz .copied
    inc si
    inc bx
    dec cx
    jnz .copy
    mov byte [bx], 0
.copied:
    mov es, [sh_stgseg]               ; AND BACK TO THE STAGING SEGMENT, which
                                      ; is where sh_biffw writes. The first
                                      ; version left ES pointing at DS through
                                      ; the whole record emit, so every word of
                                      ; every FORMULA record went into Sheet's
                                      ; own data segment instead of the file -
                                      ; no crash, no record, and a corrupted
                                      ; variable wherever DI happened to be.
    mov si, sh_rwsrc
    call sh_rpn_emit                  ; CX = token bytes, or CF=1
    jc .no
    mov [sh_wrec_len], cx
    mov ax, di                        ; room? header + body + the tokens
    add ax, cx
    add ax, 22
    cmp ax, SH_STAGE_MAX
    ja .no
    mov ax, 0x0206                    ; FORMULA - 0206H in BIFF3 and 0406H in
    cmp byte [sh_wb_xf4], 0           ; BIFF4, and a BIFF4 workbook must carry
    je .op3                           ; the BIFF4 number or its own reader
    mov ax, 0x0406                    ; skips the record by length and loses
.op3:                                 ; the cell
    call sh_biffw
    mov ax, [sh_wrec_len]
    add ax, 18                        ; row+col+xf+result(8)+flags+cce = 18
    call sh_biffw
    mov ax, [sh_wrec_row]
    call sh_biffw
    mov ax, [sh_wrec_col]
    call sh_biffw
    xor ah, ah
    mov al, [sh_wrec_fmt]
    call sh_biffw
    cmp byte [sh_wrec_type], SH_T_ERR ; BIFF's own encoding for a cached result
    je .errresult                     ; that is not a number: the top word all
    mov ax, [sh_wrec_dval]            ; ones, byte 0 naming the kind and byte 2
    call sh_biffw                     ; carrying the value. Without this the
    mov ax, [sh_wrec_dval+2]          ; result field says 0.0, and a reader
    call sh_biffw                     ; that trusts it - including this app's,
    mov ax, [sh_wrec_dval+4]          ; which reads the result and skips the
    call sh_biffw                     ; tokens - turns #DIV/0! back into a
    mov ax, [sh_wrec_dval+6]          ; perfectly ordinary zero
    call sh_biffw
    jmp .resdone
.errresult:
    mov ax, 2                         ; byte 0 = 2: an error code
    call sh_biffw
    mov al, [sh_wrec_aux]             ; byte 2 = which one
    xor ah, ah
    call sh_biffw
    xor ax, ax
    call sh_biffw
    mov ax, 0xFFFF
    call sh_biffw
.resdone:
    xor ax, ax                        ; option flags: neither recalc bit
    call sh_biffw
    mov ax, [sh_wrec_len]             ; cce, the token array's own length
    call sh_biffw
    mov si, sh_rpn_buf
    mov cx, [sh_wrec_len]
    jcxz .done
.put:
    mov al, [si]
    call sh_stgputb
    inc si
    dec cx
    jnz .put
.done:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret


sh_doread_biff:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov es, [sh_stgseg]
    xor bx, bx
    mov cx, SH_STAGE_MAX
    xor dx, dx
    mov si, sh_name
    call OSAPI_FILE_READ               ; out: AX = bytes read, or CF=1
    jc .rerr

    mov word [sh_ncells], 0
    mov word [sh_txtlen], 0            ; "replacing the sheet" - see
    mov word [sh_nbord], 0             ; sh_doread_sylk's same three
    mov word [sh_nnote], 0
    mov word [sh_biff_nfont], 0
    mov word [sh_biff_nxf], 0
    mov cx, ax                         ; CX = end offset (bytes read)
    mov [sh_biff_end], ax              ; ...and banked, because .islabel needs
    xor si, si                         ; a counter and CX is the only one free
    ; AX STILL HELD THE BYTE COUNT two lines ago, which is why this bank sits
    ; below and not above: reading sh_cursheet into AX before those two lines
    ; made the file end ZERO, and the record walk finished before its first
    ; record while still reporting "Loaded".
    mov ax, [sh_cursheet]              ; a workbook read MOVES sh_cursheet as
    mov [sh_rd_home], ax               ; it walks the substreams, so where the
    mov word [sh_rd_sheet], -1         ; user actually was is banked and put
    mov byte [sh_rd_wb], 0             ; back at the end
.rechdr:
    mov ax, si
    add ax, 4                          ; a record header is 4 bytes; EOF's
    cmp ax, cx                         ; is the shortest whole record, so
    ja .done                           ; anything less can't be one
    mov ax, [es:si]                    ; opcode
    mov dx, [es:si+2]                  ; length
    add si, 4                          ; SI = this record's data start
    mov bx, si                         ; the record must END inside the file:
    add bx, dx                         ; a crafted length near 0xFFFF wraps
    jc .done                           ; the 16-bit add and walks the stream
    cmp bx, cx                         ; BACKWARD - onto this same header,
    ja .done                           ; forever - so the wrap (CF) and the
                                       ; file end are both refused here, which
                                       ; also bounds every unknown-opcode
                                       ; .skip (every byte off a disk is
                                       ; hostile)
    cmp ax, 0x000A                     ; EOF - but a WORKBOOK has one per sheet
    je .iseof                          ; substream plus its own, so the first
                                       ; is not the end of anything (81.10.5)
    cmp ax, 0x0231                     ; FONT
    je .isfont
    cmp ax, 0x0243                     ; XF (BIFF3) - what this app writes
    je .isxf                           ; now, and what a real Excel 3 file
    cmp ax, 0x0443                     ; carries. BIFF4's is accepted too, so
    je .isxf                           ; files written before the switch still
                                        ; read back with their formats, as do
                                        ; real Excel 4 files. BIFF5-8's XF
                                        ; (0x00E0) has a different layout and
                                        ; is skipped generically below, so
                                        ; those cells read back unformatted
                                        ; rather than misformatted.
    cmp ax, 0x027E                     ; RK cell record
    je .isrk
    cmp ax, 0x0203                     ; NUMBER: a whole IEEE-754 double, and
    je .isnum                          ; the only way a fraction can travel
    cmp ax, 0x0204                     ; LABEL (BIFF3/4). 0004H is BIFF2's,
    je .islabel                        ; with a one-byte length, and is NOT
    cmp ax, 0x0205                     ; BOOLERR: a boolean or an ERROR VALUE
    je .isboolerr
    cmp ax, 0x0206                     ; accepted here for that reason
    je .isformula                      ; FORMULA: its CACHED RESULT is read,
    cmp ax, 0x0406                     ; the token array skipped - see below.
    je .isformula                      ; 0406H is BIFF4's own number for it
    cmp ax, 0x008F                     ; SHEETHDR: the substream that follows
    je .issheethdr                     ; belongs to the NEXT sheet
    jmp .skip
.isfont:
    mov bx, [sh_biff_nfont]
    cmp bx, SH_BIFF_FONT_CAP
    jae .fontcounted                   ; too many fonts to track: this one
                                        ; (and any cell using it) just
                                        ; reads back as not bold/underlined
    mov ax, si
    add ax, 4                          ; need at least height+options here
    cmp ax, cx
    ja .fontcounted
    mov al, [es:si+2]                  ; BIFF options byte: bit0 bold,
    mov ah, 0                          ; bit2 underline (BIFF3/4 layout)
    test al, 0x01
    jz .fnb
    or ah, SH_FMT_BOLD
.fnb:
    test al, 0x04
    jz .fnu
    or ah, SH_FMT_UNDER
.fnu:
    mov di, sh_font_tab
    add di, bx
    mov [di], ah
.fontcounted:
    inc word [sh_biff_nfont]
    jmp .skip
.isxf:
    mov bx, [sh_biff_nxf]
    cmp bx, SH_BIFF_XF_CAP
    jae .xfcounted                     ; too many XFs to track: a cell
                                        ; using one just reads back General
    mov ax, si
    add ax, 12                         ; need the full BIFF4 XF body
    cmp ax, cx
    ja .xfcounted
    mov al, [es:si]                    ; font index (offset0, low byte of
    mov di, sh_xf_font                 ; the font/format word)
    add di, bx
    mov [di], al
    mov al, [es:si+4]                  ; align/wrap/vertalign/orient byte
    and al, 0x07                       ; the full 3-bit XF_HOR_ALIGN field
    cmp al, 3
    jbe .alignok
    xor al, al                         ; Filled/Justified/CenterAcrossSel:
                                        ; out of this subset's scope
.alignok:
    push cx                            ; CX IS THE FILE'S END OFFSET here, and
    mov cl, SH_FMT_ALIGN_SHIFT         ; `mov cl` destroys its low byte. With
    shl al, cl                         ; 64 XF records to read, the walk then
    pop cx                             ; ran off a length of ~1028 instead of
    mov ah, al                         ; 1160 and stopped BEFORE the first cell
    mov al, [es:si+1]                  ; record - so every BIFF file this app
    call sh_biff_numfmt_from_id        ; wrote read back as "Loaded" and empty.
    push cx
    mov cl, SH_FMT_NUM_SHIFT
    shl al, cl
    pop cx
    or al, ah                          ; al = align|numfmt packed byte
    mov di, sh_xf_fmt
    add di, bx
    mov [di], al
.xfcounted:
    inc word [sh_biff_nxf]
    jmp .skip
.isrk:
    push dx                            ; length, saved across sh_setval
                                        ; (which itself preserves DX, but
                                        ; only around the value it was
                                        ; passed - not a second use)
    mov ax, si
    add ax, dx
    cmp ax, cx
    ja .toolong                        ; truncated record: stop, don't read
    mov ax, [es:si]                    ; row
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]                  ; col
    mov [sh_wrec_col], ax
    mov ax, [es:si+4]                  ; xf index
    mov [sh_wrec_xf], ax
    call sh_biff_rcok                  ; off-grid row/col: skip the record
    jc .rkdone                         ; rather than cross onto another sheet
    mov ax, [es:si+6]                  ; rk value low word
    mov dx, [es:si+8]                  ; rk value high word
    push es                            ; sh_rkdec_d works in sh_acc, which is
    call sh_rkdec_d                    ; ours, not the staging segment's
    pop es
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_setvald
    call sh_biff_applyfmt              ; uses sh_wrec_col/row/xf; looks up
                                        ; the format and writes it to the
                                        ; cell record sh_setval just made
.rkdone:
    pop dx
    jmp .skip
.isnum:
    push dx
    mov ax, si
    add ax, dx
    cmp ax, cx
    ja .toolong
    mov ax, [es:si]                    ; row
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]                  ; col
    mov [sh_wrec_col], ax
    mov ax, [es:si+4]                  ; xf index
    mov [sh_wrec_xf], ax
    call sh_biff_rcok                  ; off-grid row/col: skip the record
    jc .rkdone
    mov ax, [es:si+6]                  ; the eight value bytes, verbatim
    mov [sh_acc], ax
    mov ax, [es:si+8]
    mov [sh_acc+2], ax
    mov ax, [es:si+10]
    mov [sh_acc+4], ax
    mov ax, [es:si+12]
    mov [sh_acc+6], ax
    push es
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_setvald
    call sh_biff_applyfmt
    pop es
    pop dx
    jmp .skip
.iseof:
    cmp byte [sh_rd_wb], 0             ; a plain single-sheet stream ends here,
    je .done                           ; as it always did. In a workbook this
    jmp .skip                          ; only closes one substream, and the
                                       ; walk stops when the bytes run out
.issheethdr:
    mov byte [sh_rd_wb], 1             ; from here on, an EOF closes a sheet
    ; A BIFF4 WORKBOOK (81.10.5). Every SHEETHDR starts another sheet's
    ; substream, so the reader simply counts them: the first is sheet 0, the
    ; next sheet 1, and the cell records in between land on whichever is
    ; current. The name in the record is IGNORED - this app's four sheets are
    ; positional and named by position (81.2), so honouring a name would mean
    ; inventing a mapping the rest of the app has no way to express.
    inc word [sh_rd_sheet]
    mov ax, [sh_rd_sheet]
    cmp ax, SH_SHEETS
    jb .rdsheetok
    mov ax, SH_SHEETS - 1              ; a workbook with more sheets than this
.rdsheetok:                            ; app has: the surplus piles onto the
    mov [sh_cursheet], ax              ; last one rather than being dropped
    jmp .skip
.isformula:
    ; ONLY THE RESULT IS READ, and the token array is stepped over. Sheet keeps
    ; formulas as TEXT (81.3) and re-parses them; turning RPN back into text is
    ; a decompiler, and one built against a spec section (3.12) that is marked
    ; *2do* would guess at exactly the function names it could not verify. The
    ; value is right either way, which is the same trade this app's SYLK reader
    ; makes when a file has no ;E field.
    push dx
    mov ax, si
    add ax, dx
    cmp ax, cx
    ja .toolong
    mov ax, [es:si]                    ; row
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]                  ; col
    mov [sh_wrec_col], ax
    mov ax, [es:si+4]                  ; xf index
    mov [sh_wrec_xf], ax
    call sh_biff_rcok                  ; off-grid row/col: skip the record
    jc .rkdone
    mov ax, [es:si+6]                  ; the eight result bytes
    mov [sh_acc], ax
    mov ax, [es:si+8]
    mov [sh_acc+2], ax
    mov ax, [es:si+10]
    mov [sh_acc+4], ax
    mov ax, [es:si+12]
    mov [sh_acc+6], ax
    cmp word [sh_acc+6], 0xFFFF        ; not a number at all: see .errresult in
    jne .fresnum                       ; the writer for the encoding
    cmp byte [sh_acc], 2
    jne .fresnum
    push es
    mov dl, [sh_acc+2]
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_seterr
    call sh_biff_applyfmt
    pop es
    pop dx
    jmp .skip
.fresnum:
    push es
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_setvald
    call sh_biff_applyfmt
    pop es
    pop dx
    jmp .skip
.isboolerr:
    push dx
    mov ax, si
    add ax, dx
    cmp ax, cx
    ja .toolong
    mov ax, [es:si]                    ; row
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]                  ; col
    mov [sh_wrec_col], ax
    mov ax, [es:si+4]                  ; xf index
    mov [sh_wrec_xf], ax
    call sh_biff_rcok                  ; off-grid row/col: skip the record
    jc .rkdone
    mov al, [es:si+6]                  ; the value...
    mov dl, al
    mov al, [es:si+7]                  ; ...and what kind of value it is
    push es
    or al, al
    jz .isbool
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_seterr
    jmp .bedone
.isbool:
    mov al, dl                         ; TRUE/FALSE reads back as 1/0: this app
    xor ah, ah                         ; has no BOOL type of its own yet, and a
    call sh_acc_int                    ; number is what its formulas expect
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    call sh_setvald
.bedone:
    call sh_biff_applyfmt
    pop es
    pop dx
    jmp .skip
.islabel:
    push dx
    mov ax, si
    add ax, dx
    cmp ax, cx
    ja .toolong
    mov ax, [es:si]                    ; row
    mov [sh_wrec_row], ax
    mov ax, [es:si+2]                  ; col
    mov [sh_wrec_col], ax
    mov ax, [es:si+4]                  ; xf index
    mov [sh_wrec_xf], ax
    call sh_biff_rcok                  ; off-grid row/col: skip the record
    jc .rkdone
    mov ax, [es:si+6]                  ; cch, sixteen bits in BIFF3
    cmp ax, SH_EDITMAX                 ; a longer label is TRUNCATED, not
    jbe .lcap                          ; refused: the record's own length
    mov ax, SH_EDITMAX                 ; field still governs the skip below,
.lcap:                                 ; so the stream stays in step
    mov [sh_wrec_len], ax
    push si
    push di
    add si, 8                          ; past row/col/xf/cch
    mov di, SH_TEXPR
    mov cx, [sh_wrec_len]              ; CX HOLDS THE FILE END for this whole
    jcxz .lrdend                       ; routine and is the only register free
.lrd:                                  ; to count with, so it is reloaded from
    mov al, [es:si]                    ; sh_biff_end after the copy - every
    mov [di], al                       ; record after this one is bounded by it
    inc si
    inc di
    dec cx
    jnz .lrd
.lrdend:
    mov byte [di], 0
    pop di
    pop si
    push es
    mov ax, [sh_wrec_col]
    mov bx, [sh_wrec_row]
    push si
    mov si, SH_TEXPR
    call sh_settext
    pop si
    call sh_biff_applyfmt
    pop es
    mov cx, [sh_biff_end]              ; CX restored: the loop bound, banked at
    pop dx                             ; the top of this routine, because the
    jmp .skip                          ; copy above used CX as its counter
.toolong:
    pop dx
    jmp .done
.skip:
    add si, dx
    jmp .rechdr
.done:
    mov ax, [sh_rd_home]               ; the user's own sheet, back where it
    cmp word [sh_rd_sheet], 0          ; was - unless the file was a plain
    jl .nowb                           ; single-sheet stream, which never
    xor ax, ax                         ; touched sh_cursheet and whose data
.nowb:                                 ; landed on sheet 0
    mov [sh_cursheet], ax
    mov word [sh_msg], sh_m_loaded
    jmp .out
.rerr:
    call sh_setferr
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
; sh_biff_rcok - out: CF=0 if [sh_wrec_row] < SH_ROWS and [sh_wrec_col] <
; SH_COLS, CF=1 otherwise. A file's row with bit 14 set would OR into the
; SHEET bits of the packed key (see sh_findcell) and land the cell on a
; DIFFERENT sheet - the same clamp the SYLK reader's .apply performs on X/Y.
; Preserves all registers.
; -----------------------------------------------------------------------------
sh_biff_rcok:
    cmp word [sh_wrec_row], SH_ROWS
    jae .bad
    cmp word [sh_wrec_col], SH_COLS
    jae .bad
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; sh_parseslk - walk every line of a buffer, applying each 'C' record found
; in: SI = buffer start (offset in ES), CX = length; ES = the buffer's
; segment (the caller's job, e.g. sh_doread sets it to sh_stgseg)
; -----------------------------------------------------------------------------
sh_parseslk:
    push ax
    push bx
    push dx
    push si
    push di
    mov di, si
    add di, cx
.lineloop:
    cmp si, di
    jae .donelines
    mov bx, si
.findeol:
    cmp bx, di
    jae .goteol
    mov al, [es:bx]
    cmp al, 13
    je .goteol
    cmp al, 10
    je .goteol
    inc bx
    jmp .findeol
.goteol:
    mov ax, bx
    sub ax, si
    cmp ax, 2
    jb .advance
    cmp byte [es:si+1], ';'
    jne .advance
    cmp byte [es:si], 'C'
    jne .notc
    push si
    add si, 2
    call sh_parsecrec                ; in: SI=tokens start, BX=line end
    pop si
    jmp .advance
.notc:
    cmp byte [es:si], 'F'
    jne .advance
    push si
    add si, 2
    call sh_parsefrec                ; in: SI=tokens start, BX=line end
    pop si
.advance:
    mov si, bx
.skipterm:
    cmp si, di
    jae .lineloop
    mov al, [es:si]
    cmp al, 13
    je .isterm
    cmp al, 10
    je .isterm
    jmp .lineloop
.isterm:
    inc si
    jmp .skipterm
.donelines:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_parsecrec - the fields of one 'C' record, order-independent
; in: SI = start of tokens (right after "C;"), BX = line end (exclusive);
; ES = the buffer's segment, same as sh_parseslk's caller set
; -----------------------------------------------------------------------------
sh_parsecrec:
    push ax
    push bx
    push si
    mov word [SH_TCOL], 0
    mov word [SH_TROW], 0
    mov word [SH_TVAL], 0
    mov byte [SH_THASE], 0
    mov byte [SH_TISTXT], 0
    mov byte [SH_TISERR], 0
    mov word [SH_TDVAL], 0
    mov word [SH_TDVAL+2], 0
    mov word [SH_TDVAL+4], 0
    mov word [SH_TDVAL+6], 0
    mov byte [SH_THAVE], 0
.tok:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    cmp al, ';'
    je .skipsemi
    cmp al, 'X'
    je .isx
    cmp al, 'Y'
    je .isy
    cmp al, 'K'
    je .isk
    cmp al, 'E'
    je .ise
.scan:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    inc si
    cmp al, ';'
    jne .scan
    jmp .tok
.skipsemi:
    inc si
    jmp .tok
.isx:
    inc si
    call sh_pint
    mov [SH_TCOL], ax
    jmp .tok
.isy:
    inc si
    call sh_pint
    mov [SH_TROW], ax
    jmp .tok
.isk:
    inc si
    cmp si, bx                        ; stage 4.5: a QUOTED K field is a label.
    jae .knum                         ; SYLK has no type field - the quotes are
    cmp byte [es:si], '#'             ; the entire signal, on both sides, and
    je .kerr                          ; a leading '#' is an ERROR VALUE for
    cmp byte [es:si], 34              ; the same reason
    jne .knum
    inc si                            ; past the opening quote
    push di
    mov di, SH_TEXPR                  ; the same buffer ;E uses, and never at
    mov cx, SH_EDITMAX                ; the same time: a label has no formula
.kt:
    jcxz .ktend
    cmp si, bx
    jae .ktend
    mov al, [es:si]
    cmp al, 13
    je .ktend
    cmp al, 10
    je .ktend
    cmp al, 34
    jne .ktkeep
    inc si                            ; a quote: doubled means one literal
    cmp si, bx                        ; quote, single means end of field
    jae .ktend
    cmp byte [es:si], 34
    jne .ktend
.ktkeep:
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .kt
.ktend:
    mov byte [di], 0
    pop di
    mov byte [SH_TISTXT], 1
    mov byte [SH_THAVE], 1
    jmp .tok
.kerr:
    push di                           ; the name goes into sh_rwsrc, NOT into
    mov di, sh_rwsrc                  ; SH_TEXPR: a FORMULA cell writes both
    mov cx, SH_EDITMAX                ; ;E and ;K, ;E comes first, and parsing
                                       ; the ;K into ;E's buffer overwrote the
                                       ; formula with the error's name - which
                                       ; then went in as the cell's formula,
                                       ; `=#DIV/0!`, and evaluated to #VALUE!
.ke:
    jcxz .keend
    cmp si, bx
    jae .keend
    mov al, [es:si]
    cmp al, ';'
    je .keend
    cmp al, 13
    je .keend
    cmp al, 10
    je .keend
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .ke
.keend:
    mov byte [di], 0
    pop di
    push si
    mov si, sh_rwsrc
    call sh_errcode                   ; -> AL, 0 for a spelling we do not know
    pop si
    mov [SH_TISERR], al
    mov byte [SH_THAVE], 1
    jmp .tok
.knum:
    call sh_esatof                    ; a full decimal, not an integer
    push ax
    push si
    push di
    mov si, sh_acc
    mov di, SH_TDVAL
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop di
    pop si
    pop ax
    mov byte [SH_THAVE], 1
    jmp .tok
.ise:
    inc si
    push di                           ; the expression, copied out of the
    mov di, SH_TEXPR                  ; staging segment into DS so the R1C1
    mov cx, SH_EDITMAX                ; converter can read it
.ecpy:
    jcxz .ecpyd
    cmp si, bx
    jae .ecpyd
    mov al, [es:si]
    cmp al, ';'
    je .ecpyd
    cmp al, 13
    je .ecpyd
    cmp al, 10
    je .ecpyd
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .ecpy
.ecpyd:
    mov byte [di], 0
    pop di
    mov byte [SH_THASE], 1
    jmp .tok
.apply:
    cmp byte [SH_THAVE], 0
    je .out
    mov ax, [SH_TCOL]
    cmp ax, 1
    jb .out
    cmp ax, SH_COLS
    ja .out
    mov cx, [SH_TROW]
    cmp cx, 1
    jb .out
    cmp cx, SH_ROWS
    ja .out
    dec ax
    dec cx
    mov bx, cx
    cmp byte [SH_THASE], 0            ; a ;E field wins over ;K: the value is
    je .notformula_c                  ; only the cached result of it, and
    push ax                           ; storing that instead would flatten the
    push bx                           ; formula exactly as this used to
    push si
    push di
    mov [sh_rc_ccol], ax
    mov [sh_rc_crow], bx
    mov si, SH_TEXPR
    call sh_formula_from_r1c1
    pop di
    pop si
    pop bx
    pop ax
    mov si, sh_rwdst                  ; AFTER the pops: setting SI before them
    call sh_setformula                ; put the saved value straight back over
                                      ; it, and sh_setformula stored whatever
                                      ; the staging pointer happened to be
    jmp .out
.notformula_c:
    cmp byte [SH_TISERR], 0
    je .noterr_c
    mov dl, [SH_TISERR]
    call sh_seterr
    jmp .out
.noterr_c:
    cmp byte [SH_TISTXT], 0
    je .plainval_c
    push si
    mov si, SH_TEXPR
    call sh_settext
    pop si
    jmp .out
.plainval_c:
    push si
    push di
    mov si, SH_TDVAL
    mov di, sh_acc
    push ax
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop ax
    pop di
    pop si
    call sh_setvald
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_parsefrec - the fields of an "F" (formatting) record, stage 1.6's real
; SYLK support. In: SI = start of tokens (right after "F;"), BX = line end
; (exclusive); ES = the buffer's segment. Real SYLK's F record predates
; MultiPlan-era bold/underline entirely (this book's own field list never
; mentions either), so only alignment (;F's c2 code) and number format
; (;F's c1 code, plus the separate ;K comma flag) round-trip through SYLK -
; matching the same "only what the real format actually has" principle
; DIF's fix above just established. The cell itself must already exist
; (from an earlier C record) for this to do anything, per SYLK's own
; documented convention that referenced things are defined before use -
; this app's own writer always emits a formatted cell's C record first for
; exactly that reason.
; -----------------------------------------------------------------------------
sh_parsefrec:
    push ax
    push bx
    push cx
    push si
    push di                           ; sh_findcell below is called for its
                                       ; side effect on DI, but sh_parseslk's
                                       ; caller keeps its own buffer-end in
                                       ; DI live across this whole call - it
                                       ; must come back unchanged
    mov word [SH_TCOL], 0
    mov word [SH_TROW], 0
    mov byte [SH_TALIGN], 0
    mov byte [SH_TNUMFMT], 0
    mov byte [SH_TCOMMA], 0
.tok:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    cmp al, ';'
    je .skipsemi
    cmp al, 'X'
    je .isx
    cmp al, 'Y'
    je .isy
    cmp al, 'F'
    je .isf
    cmp al, 'K'
    je .isk
.scan:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    inc si
    cmp al, ';'
    jne .scan
    jmp .tok
.skipsemi:
    inc si
    jmp .tok
.isx:
    inc si
    call sh_pint
    mov [SH_TCOL], ax
    jmp .tok
.isy:
    inc si
    call sh_pint
    mov [SH_TROW], ax
    jmp .tok
.isk:
    inc si
    mov byte [SH_TCOMMA], 1
    jmp .tok
.isf:                                  ; ;F<c1>[space]<digits>[space]<c2> -
                                        ; one field, not semicolon-delimited
                                        ; internally, so it's parsed as its
                                        ; own little grammar before control
                                        ; returns to the outer ;-scan loop
    inc si
    cmp si, bx
    jae .tok
    mov al, [es:si]
    call sh_sylk_numfmt_from_c1
    mov [SH_TNUMFMT], al
    inc si
    cmp si, bx
    jae .tok
    cmp byte [es:si], ' '
    jne .fdigits
    inc si
.fdigits:
    cmp si, bx
    jae .tok
    mov al, [es:si]
    cmp al, '0'
    jb .fspace2
    cmp al, '9'
    ja .fspace2
    inc si
    jmp .fdigits
.fspace2:
    cmp si, bx
    jae .tok
    cmp byte [es:si], ' '
    jne .fc2
    inc si
.fc2:
    cmp si, bx
    jae .tok
    mov al, [es:si]
    call sh_sylk_align_from_c2
    mov [SH_TALIGN], al
    inc si
    jmp .tok
.apply:
    mov ax, [SH_TCOL]
    cmp ax, 1
    jb .out
    cmp ax, SH_COLS
    ja .out
    mov cx, [SH_TROW]
    cmp cx, 1
    jb .out
    cmp cx, SH_ROWS
    ja .out
    dec ax
    dec cx
    mov bx, cx
    call sh_findcell
    jnc .out                          ; no prior C record for this cell:
                                       ; nothing to attach the format to
    mov al, [SH_TNUMFMT]
    cmp byte [SH_TCOMMA], 0
    je .noupgrade
    or al, al
    jnz .noupgrade                    ; ;K only promotes a still-General
                                       ; code to Comma - an explicit c1 of
                                       ; '$' (Currency) wins if both appear
    mov al, SH_FMT_NUM_COMMA
.noupgrade:
    mov cl, SH_FMT_NUM_SHIFT
    shl al, cl
    mov ah, [SH_TALIGN]
    mov cl, SH_FMT_ALIGN_SHIFT
    shl ah, cl
    or al, ah
    push es
    mov es, [sh_cellseg]
    mov [es:di+5], al
    pop es
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_sylk_numfmt_from_c1 - in: AL = an F record's c1 formatting-code char;
; out: AL = this app's SH_FMT_NUM_* code. Real SYLK's c1 codes are
; 0/C/E/F/G/$/* (default/continuous/scientific/fixed/general/currency/
; bargraph) - only '$' has an equivalent here; everything else (including
; the codes this app never writes, like scientific or bargraph) falls back
; to General, which is the honest answer since this app has no comparable
; format for them either.
; -----------------------------------------------------------------------------
sh_sylk_numfmt_from_c1:
    cmp al, '$'
    je .cur
    xor al, al
    ret
.cur:
    mov al, SH_FMT_NUM_CURRENCY
    ret

; -----------------------------------------------------------------------------
; sh_sylk_align_from_c2 - in: AL = an F record's c2 alignment-code char
; ('0' default, 'C' center, 'G' general, 'L' left, 'R' right); out: AL =
; this app's SH_FMT_ALIGN_* code
; -----------------------------------------------------------------------------
sh_sylk_align_from_c2:
    cmp al, 'L'
    je .left
    cmp al, 'C'
    je .center
    cmp al, 'R'
    je .right
    xor al, al
    ret
.left:
    mov al, SH_FMT_ALIGN_LEFT
    ret
.center:
    mov al, SH_FMT_ALIGN_CENTER
    ret
.right:
    mov al, SH_FMT_ALIGN_RIGHT
    ret

; -----------------------------------------------------------------------------
; sh_pint - parse a signed decimal integer
; in: ES:SI=ptr, BX=limit (exclusive, an offset); also stops at NUL
; out: AX=value, SI=advanced; BX preserved; ES must be set by the caller
; -----------------------------------------------------------------------------
sh_pint:
    push bx
    push cx
    push dx
    xor cx, cx
    xor ax, ax
    cmp si, bx
    jae .fin
    cmp byte [es:si], '-'
    jne .digits
    mov cx, 1
    inc si
.digits:
    cmp si, bx
    jae .fin
    mov dl, [es:si]
    or dl, dl
    jz .fin
    cmp dl, '0'
    jb .fin
    cmp dl, '9'
    ja .fin
    sub dl, '0'
    xor dh, dh
    push dx
    push bx
    mov bx, 10
    mul bx
    pop bx
    pop dx
    add ax, dx
    inc si
    jmp .digits
.fin:
    or cx, cx
    jz .nosign
    neg ax
.nosign:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sh_setferr - build "Err N" from a FERR_* code and point sh_msg at it
; in: AX = FERR_* (CF was set on the API call that produced it)
; -----------------------------------------------------------------------------
sh_setferr:
    push di
    push si
    mov di, sh_errbuf
    mov si, sh_s_errpfx
    call sh_strcpy_to_di              ; DI advances past "Err " to the new NUL
    call sh_itoa                      ; AX (the FERR_* code) -> sh_numbuf;
                                       ; preserves DI
    mov si, sh_numbuf
    call sh_strcpy_to_di
    mov word [sh_msg], sh_errbuf
    pop si
    pop di
    ret

; =============================================================================
; Cell storage - a sorted array of (row, col, flags, format, value,
; formula_off, pass) records in the claimed sh_cellseg, searched with a
; binary search and kept sorted by shifting on insert/remove. 12 bytes/rec:
;   +0 row (word) - stage 2.0: PACKED, not a plain row. Bits 0-13 are the
;   real row (0..16383, SH_ROW_MASK); bits 14-15 are the sheet index
;   (0..SH_SHEETS-1). Sorting and searching a plain 16-bit compare on this
;   word therefore sorts every sheet's records into one contiguous run,
;   ordered first by sheet and then by row within it, with NO change to the
;   comparison logic itself - only the few places that construct or take
;   apart the word (sh_findcell packing it from [sh_cursheet], sh_unpackrow
;   splitting it back out for the SYLK/DIF/BIFF writers, which must skip
;   every sheet but the one being saved) know this isn't just a row.
;   +2 col (word)  +4 flags (byte)  +5 format (byte, the
;   SH_FMT_* bits, stage 1.6 - this byte was unused padding before)
;   +6 value (word)  +8 formula_off (word, 0xFFFF=none)  +10 pass (word)
; Only three routines (sh_findcell/sh_addcell/sh_removecell) know this
; layout and the shifting; everything else goes through sh_getcell2/
; sh_setval/sh_clearcell.
; =============================================================================

; -----------------------------------------------------------------------------
; sh_unpackrow - in: AX = a cell record's packed row/sheet word; out: AX =
; the real row (0..16383), BX = the sheet index it belongs to
; -----------------------------------------------------------------------------
sh_unpackrow:
    push cx
    mov bx, ax
    mov cl, SH_ROW_BITS
    shr bx, cl
    and ax, SH_ROW_MASK
    pop cx
    ret

; -----------------------------------------------------------------------------
; sh_findcell - binary search for (col, row)
; in: AX=col, BX=row
; out: CF=1 found, DI=byte offset of the record
;      CF=0 not found, DI=byte offset where it would be inserted
; -----------------------------------------------------------------------------
sh_findcell:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sh_fcol], ax
    mov ax, [sh_cursheet]              ; the stored "row" word is really a
    mov cl, SH_ROW_BITS                ; packed (sheet<<SH_ROW_BITS | row) -
    shl ax, cl                         ; see the stage 2.0 comment above the
    or ax, bx                          ; cell record layout - so every
    mov [sh_frow], ax                  ; existing caller of this proc (all
                                        ; of them pass a plain 0..16383 row)
                                        ; keeps working unchanged, searching
                                        ; only the CURRENT sheet's records
    xor cx, cx                        ; CX = lo
    mov dx, [sh_ncells]                ; DX = hi
.loop:
    cmp cx, dx
    jae .notfound
    mov si, dx
    sub si, cx
    shr si, 1
    add si, cx                        ; SI = mid
    mov ax, si
    mov bx, SH_C_SZ
    push dx                           ; MUL clobbers DX (the high word of
    mul bx                            ; the product) - DX is also this
    pop dx                            ; loop's search bound, so it must
    mov di, ax                        ; survive every iteration, not just
                                       ; the one that happens to find a match
                                       ; on its first probe
    push es
    mov es, [sh_cellseg]
    mov ax, [es:di]                   ; candidate row
    mov bx, [es:di+2]                 ; candidate col
    pop es
    cmp ax, [sh_frow]
    jl .lower
    jg .higher
    cmp bx, [sh_fcol]
    jl .lower
    jg .higher
    stc
    jmp .out
.lower:
    mov cx, si
    inc cx
    jmp .loop
.higher:
    mov dx, si
    jmp .loop
.notfound:
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov di, ax
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_addcell - find or create the record for (col, row)
; in: AX=col, BX=row
; out: CF=0, DI=byte offset of the record (existing or new, zeroed if new)
;      CF=1 the table is full - no new record could be created
; -----------------------------------------------------------------------------
sh_addcell:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [sh_chartdirty], 1        ; whoever asked is about to write this
                                        ; record - sh_repaint's chart tail
                                        ; reads the byte instead of rescanning
                                        ; the column on every repaint
    call sh_findcell
    jc .found
    cmp word [sh_ncells], SH_CELL_CAP
    jae .full
    push di                            ; insertion offset, kept across the
                                        ; shift below
    mov ax, [sh_ncells]
    mov bx, SH_C_SZ
    mul bx                             ; AX = current end-of-array offset
    mov cx, ax
    sub cx, di                         ; CX = bytes to shift up (may be 0)
    push ds
    push es
    mov dx, [sh_cellseg]
    mov ds, dx
    mov es, dx
    jcxz .noshift
    mov si, ax
    dec si
    mov di, si
    add di, SH_C_SZ
    std
    rep movsb
    cld
.noshift:
    pop es
    pop ds
    pop di                             ; DI = insertion offset, restored
    inc word [sh_ncells]
    push es
    mov es, [sh_cellseg]
    mov ax, [sh_frow]
    mov [es:di], ax
    mov ax, [sh_fcol]
    mov [es:di+2], ax
    mov byte [es:di+4], 0
    mov byte [es:di+5], 0
    mov byte [es:di+SH_C_TYPE], SH_T_NUM
    mov byte [es:di+SH_C_AUX], 0
    mov word [es:di+SH_C_VAL], 0      ; ALL EIGHT value bytes, not just the low
    mov word [es:di+SH_C_VAL+2], 0    ; word the integer model uses today. The
    mov word [es:di+SH_C_VAL+4], 0    ; array is shuffled with a byte move, so
    mov word [es:di+SH_C_VAL+6], 0    ; a "new" record inherits whatever the
                                      ; record above it left here - harmless
                                      ; while only the low word is read, and a
                                      ; genuinely nasty surprise the moment the
                                      ; full double goes live
    mov word [es:di+SH_C_FOFF], 0xFFFF
    mov word [es:di+SH_C_PASS], 0
    pop es
    clc
    jmp .out
.found:
    clc
    jmp .out
.full:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_removecell - in: AX=col, BX=row; removes the record if present
; -----------------------------------------------------------------------------
sh_removecell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [sh_chartdirty], 1        ; sh_addcell's reason
    call sh_findcell
    jnc .out
    mov ax, [sh_ncells]
    mov bx, SH_C_SZ
    mul bx                             ; AX = end offset (before shrink)
    mov cx, ax
    sub cx, di
    sub cx, SH_C_SZ                    ; CX = bytes after this record
    push ds
    push es
    mov dx, [sh_cellseg]
    mov ds, dx
    mov es, dx
    jcxz .noshift
    mov si, di
    add si, SH_C_SZ
    cld
    rep movsb
.noshift:
    pop es
    pop ds
    dec word [sh_ncells]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Border table (stage 2.x, sh_bordseg claim) - a SEPARATE sparse sorted
; array, same shape and packing convention as the main cell array above
; (sh_findcell's own stage 2.0 comment on the packed row/sheet word applies
; here unchanged) but only 5 bytes/record: +0 packed row/sheet (word)
; +2 col (word) +4 border byte (SH_BORD_* bits). Almost no cell ever has a
; border, so a cell simply has NO record here at all until Format >
; Border... sets one of its bits, and loses its record again the moment
; every bit clears (sh_bt_removecell) - the same "no record = default"
; philosophy the main array already uses for value 0 vs formatted-and-0.
; =============================================================================

; sh_bt_findcell - binary search for (col,row); in AX=col,BX=row;
; out CF=1 found DI=offset, CF=0 not found DI=insertion offset
sh_bt_findcell:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sh_fcol], ax
    mov ax, [sh_cursheet]
    mov cl, SH_ROW_BITS
    shl ax, cl
    or ax, bx
    mov [sh_frow], ax
    xor cx, cx
    mov dx, [sh_nbord]
.loop:
    cmp cx, dx
    jae .notfound
    mov si, dx
    sub si, cx
    shr si, 1
    add si, cx
    mov ax, si
    mov bx, 5
    push dx
    mul bx
    pop dx
    mov di, ax
    push es
    mov es, [sh_bordseg]
    mov ax, [es:di]
    mov bx, [es:di+2]
    pop es
    cmp ax, [sh_frow]
    jl .lower
    jg .higher
    cmp bx, [sh_fcol]
    jl .lower
    jg .higher
    stc
    jmp .out
.lower:
    mov cx, si
    inc cx
    jmp .loop
.higher:
    mov dx, si
    jmp .loop
.notfound:
    mov ax, cx
    mov bx, 5
    mul bx
    mov di, ax
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_bt_addcell - find or create (col,row); in AX=col,BX=row;
; out CF=0 DI=offset (zeroed border byte if new), CF=1 table full
sh_bt_addcell:
    push ax
    push bx
    push cx
    push dx
    push si
    call sh_bt_findcell
    jc .found
    cmp word [sh_nbord], SH_BORD_CAP
    jae .full
    push di
    mov ax, [sh_nbord]
    mov bx, 5
    mul bx
    mov cx, ax
    sub cx, di
    push ds
    push es
    mov dx, [sh_bordseg]
    mov ds, dx
    mov es, dx
    jcxz .noshift
    mov si, ax
    dec si
    mov di, si
    add di, 5
    std
    rep movsb
    cld
.noshift:
    pop es
    pop ds
    pop di
    inc word [sh_nbord]
    push es
    mov es, [sh_bordseg]
    mov ax, [sh_frow]
    mov [es:di], ax
    mov ax, [sh_fcol]
    mov [es:di+2], ax
    mov byte [es:di+4], 0
    pop es
    clc
    jmp .out
.found:
    clc
    jmp .out
.full:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_bt_removecell - in: AX=col, BX=row
sh_bt_removecell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sh_bt_findcell
    jnc .out
    mov ax, [sh_nbord]
    mov bx, 5
    mul bx
    mov cx, ax
    sub cx, di
    sub cx, 5
    push ds
    push es
    mov dx, [sh_bordseg]
    mov ds, dx
    mov es, dx
    jcxz .noshift
    mov si, di
    add si, 5
    cld
    rep movsb
.noshift:
    pop es
    pop ds
    dec word [sh_nbord]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_bt_get - in: AX=col, BX=row; out: AL = border byte (0 if no record)
sh_bt_get:
    push bx
    push di
    push es
    call sh_bt_findcell
    jnc .none
    mov es, [sh_bordseg]
    mov al, [es:di+4]
    jmp .out
.none:
    xor al, al
.out:
    pop es
    pop di
    pop bx
    ret

; =============================================================================
; Note table (stage 3.0b, sh_noteseg claim) - Excel 2.1's cell notes, reached
; from Formula > Note... A THIRD sparse sorted array, the same shape as the
; border table above and searched the same way, but 6 bytes/record: +0 packed
; row/sheet (word) +2 col (word) +4 the note text's offset in sh_txtseg
; (word).
;
; THE TEXT LIVES IN THE EXISTING FORMULA ARENA, not in this claim. A note is
; text of unknown length and sh_txt_append already appends exactly that, so this
; table stores an offset into it just as a cell record stores formula_off.
; That arena is APPEND-ONLY and never compacted, so re-editing a note leaks
; its old copy - which is precisely what re-editing a formula has always done,
; so the behaviour is at least consistent, and 8KB is a lot of notes. When the
; arena fills, sh_txt_append returns CF=1 and the edit is refused rather than
; half-applied.
;
; This is Sheet's 7th claim of MEM_OWNER_MAX's 8 (own region + cellseg/txtseg/
; stgseg/bordseg/chartseg/noteseg), so there is exactly one left.
; =============================================================================

; sh_nt_findcell - binary search for (col,row); in AX=col,BX=row;
; out CF=1 found DI=offset, CF=0 not found DI=insertion offset
sh_nt_findcell:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [sh_fcol], ax
    mov ax, [sh_cursheet]
    mov cl, SH_ROW_BITS
    shl ax, cl
    or ax, bx
    mov [sh_frow], ax
    xor cx, cx
    mov dx, [sh_nnote]
.loop:
    cmp cx, dx
    jae .notfound
    mov si, dx
    sub si, cx
    shr si, 1
    add si, cx
    mov ax, si
    mov bx, SH_NOTE_REC
    push dx
    mul bx
    pop dx
    mov di, ax
    push es
    mov es, [sh_noteseg]
    mov ax, [es:di]
    mov bx, [es:di+2]
    pop es
    cmp ax, [sh_frow]
    jl .lower
    jg .higher
    cmp bx, [sh_fcol]
    jl .lower
    jg .higher
    stc
    jmp .out
.lower:
    mov cx, si
    inc cx
    jmp .loop
.higher:
    mov dx, si
    jmp .loop
.notfound:
    mov ax, cx
    mov bx, SH_NOTE_REC
    mul bx
    mov di, ax
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_nt_addcell - find or create (col,row); in AX=col,BX=row;
; out CF=0 DI=offset (zeroed text offset if new), CF=1 table full
sh_nt_addcell:
    push ax
    push bx
    push cx
    push dx
    push si
    call sh_nt_findcell
    jc .found
    cmp word [sh_nnote], SH_NOTE_CAP
    jae .full
    push di
    mov ax, [sh_nnote]
    mov bx, SH_NOTE_REC
    mul bx
    mov cx, ax
    sub cx, di
    push ds
    push es
    mov dx, [sh_noteseg]
    mov ds, dx
    mov es, dx
    jcxz .noshift
    mov si, ax
    dec si
    mov di, si
    add di, SH_NOTE_REC
    std
    rep movsb
    cld
.noshift:
    pop es
    pop ds
    pop di
    inc word [sh_nnote]
    push es
    mov es, [sh_noteseg]
    mov ax, [sh_frow]
    mov [es:di], ax
    mov ax, [sh_fcol]
    mov [es:di+2], ax
    mov word [es:di+4], 0
    pop es
    clc
    jmp .out
.found:
    clc
    jmp .out
.full:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_nt_removecell - in: AX=col, BX=row. Deleting a note orphans its text in
; the arena; see this table's header on why that is the existing behaviour.
sh_nt_removecell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sh_nt_findcell
    jnc .out
    mov ax, [sh_nnote]
    mov bx, SH_NOTE_REC
    mul bx
    mov cx, ax
    sub cx, di
    sub cx, SH_NOTE_REC
    push ds
    push es
    mov dx, [sh_noteseg]
    mov ds, dx
    mov es, dx
    jcxz .noshift
    mov si, di
    add si, SH_NOTE_REC
    cld
    rep movsb
.noshift:
    pop es
    pop ds
    dec word [sh_nnote]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_nt_get - in: AX=col, BX=row; out: CF=1 and AX = the note text's offset
; in sh_txtseg; CF=0 and AX undefined if this cell has no note.
sh_nt_get:
    push bx
    push di
    push es
    call sh_nt_findcell
    jnc .none
    mov es, [sh_noteseg]
    mov ax, [es:di+4]
    pop es
    pop di
    pop bx
    stc
    ret
.none:
    pop es
    pop di
    pop bx
    clc
    ret

; sh_nt_set - attach the NUL string at DS:SI to (col,row).
; in: AX=col, BX=row, SI=the text. out: CF=1 = refused (table or arena full).
; An EMPTY string removes the note instead, which is how the dialog's OK
; button clears one - Excel's own Note dialog has no separate Delete.
sh_nt_set:
    push ax
    push bx
    push dx
    push di
    cmp byte [si], 0
    je .clear
    push ax
    push bx
    call sh_txt_append                  ; arena first: if IT has no room the
    jc .fail2                           ; table must not gain a record
    mov dx, ax                          ; dx = the text's arena offset
    pop bx
    pop ax
    call sh_nt_addcell
    jc .fail
    push es
    mov es, [sh_noteseg]
    mov [es:di+4], dx
    pop es
    clc
    jmp .out
.clear:
    call sh_nt_removecell
    clc
    jmp .out
.fail2:
    pop bx
    pop ax
.fail:
    stc
.out:
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rowcol_op - Insert or delete a whole row or column on the CURRENT
; sheet only. in: AL = 0 insert row / 1 delete row / 2 insert column /
; 3 delete column; BX = the row or column index the operation pivots on
; (the selected cell's own row/col - Edit menu Insert.../Delete... has no
; other way to name one, since there is no range selection - see the
; W_ONDRAG scope note above sh_docmd_edit).
;
; The cell array is sorted by (sheet, row) then col (see the stage 2.0
; comment above sh_findcell) - shifting a COLUMN can reorder cells WITHIN
; a row relative to their row-mates, which the sorted array's own binary
; search depends on getting right. Rather than hand-roll an in-place
; resort, this stages every record's (sheet, row, col, flags, format,
; value, formula_off) into sh_stgseg with the shift already applied (or
; marked dropped, if inserting pushes a row/col past the edge of the
; grid, or if it sits exactly on a deleted row/col), empties the whole
; array, then re-inserts every staged record through sh_addcell (which
; already keeps the array sorted on every insert, so re-insertion order
; doesn't matter). Formula TEXT is untouched - only the cell record's own
; formula_off is carried over as-is, so an existing formula's cell
; references are NOT relatively adjusted by this operation (same scope
; reasoning as sh_docmd_fillright/sh_docmd_sortcol); its cached value is
; simply left to go stale, since sh_addcell's own default pass=0 on the
; fresh record forces a re-evaluation on the next paint regardless.
; -----------------------------------------------------------------------------
sh_rowcol_op:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [sh_rc_op], al
    mov [sh_rc_idx], bx
    mov word [sh_rc_stgcnt], 0
    mov ax, [sh_cursheet]
    mov [sh_rc_savedsheet], ax
    xor cx, cx
.scan:
    cmp cx, [sh_ncells]
    jae .scandone
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov si, ax
    mov es, [sh_cellseg]
    mov ax, [es:si]
    call sh_unpackrow                 ; ax=row, bx=sheet
    mov [sh_rc_trow], ax
    mov [sh_rc_tsheet], bx
    mov ax, [es:si+2]
    mov [sh_rc_tcol], ax
    mov al, [es:si+4]
    mov [sh_rc_tflags], al
    mov al, [es:si+5]
    mov [sh_rc_tfmt], al
    mov al, [es:si+SH_C_TYPE]
    mov [sh_rc_ttype], al
    mov al, [es:si+SH_C_AUX]
    mov [sh_rc_taux], al
    push di
    push cx
    mov di, sh_rc_tval
    mov cx, 4
.rcget:
    mov ax, [es:si+SH_C_VAL]
    mov [di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .rcget
    sub si, 8
    pop cx
    pop di
    mov ax, [es:si+SH_C_FOFF]
    mov [sh_rc_tfml], ax
    mov ax, [sh_rc_tsheet]
    cmp ax, [sh_rc_savedsheet]
    jne .stage                        ; a different sheet: carried unchanged
    mov al, [sh_rc_op]
    cmp al, 0
    je .insrow
    cmp al, 1
    je .delrow
    cmp al, 2
    je .inscol
    jmp .delcol
.insrow:
    mov ax, [sh_rc_trow]
    cmp ax, [sh_rc_idx]
    jb .stage
    inc ax
    cmp ax, SH_ROWS
    jae .next                         ; pushed off the bottom: dropped
    mov [sh_rc_trow], ax
    jmp .stage
.delrow:
    mov ax, [sh_rc_trow]
    cmp ax, [sh_rc_idx]
    jb .stage
    je .next                          ; exactly the deleted row: dropped
    dec ax
    mov [sh_rc_trow], ax
    jmp .stage
.inscol:
    mov ax, [sh_rc_tcol]
    cmp ax, [sh_rc_idx]
    jb .stage
    inc ax
    cmp ax, SH_COLS
    jae .next
    mov [sh_rc_tcol], ax
    jmp .stage
.delcol:
    mov ax, [sh_rc_tcol]
    cmp ax, [sh_rc_idx]
    jb .stage
    je .next
    dec ax
    mov [sh_rc_tcol], ax
.stage:
    mov ax, [sh_rc_stgcnt]
    mov bx, SH_C_SZ
    mul bx
    mov di, ax
    mov es, [sh_stgseg]
    mov ax, [sh_rc_tsheet]
    mov [es:di], ax
    mov ax, [sh_rc_trow]
    mov [es:di+2], ax
    mov ax, [sh_rc_tcol]
    mov [es:di+4], ax
    mov al, [sh_rc_tflags]
    mov [es:di+SH_S_FLAGS], al        ; THE STAGING RECORD IS NOT THE CELL
    mov al, [sh_rc_tfmt]              ; RECORD. It is its own SH_S_* layout in
    mov [es:di+SH_S_FMT], al          ; sh_stgseg - which is precisely why
    push si                           ; both are named now: converting this
    push cx                           ; block to the cell offsets by mistake
    mov si, sh_rc_tval                ; was silent, and staged garbage
    mov cx, 4
.stval:
    mov ax, [si]                      ; ALL FOUR value words: .rstval reads
    mov [es:di+SH_S_VAL], ax          ; four back, so staging only one left
    add si, 2                         ; six bytes of whatever sh_stgseg last
    add di, 2                         ; held (file text, a sort) inside every
    dec cx                            ; plain number, on every Insert/Delete
    jnz .stval
    sub di, 8
    pop cx
    pop si
    mov ax, [sh_rc_tfml]
    mov [es:di+SH_S_FML], ax
    mov al, [sh_rc_ttype]             ; the tag and the error code ride too -
    mov [es:di+SH_S_TYPE], al         ; see the SH_S_TYPE comment in the
    mov al, [sh_rc_taux]              ; layout block above
    mov [es:di+SH_S_AUX], al
    inc word [sh_rc_stgcnt]
.next:
    inc cx
    jmp .scan
.scandone:
    mov word [sh_ncells], 0
    xor cx, cx
.reins:
    cmp cx, [sh_rc_stgcnt]
    jae .reinsdone
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov si, ax
    mov es, [sh_stgseg]
    mov ax, [es:si]
    mov [sh_cursheet], ax             ; impersonate this record's own sheet
                                       ; so sh_addcell's sh_findcell packs
                                       ; it correctly (stage 2.0 comment
                                       ; above sh_findcell)
    mov ax, [es:si+2]
    mov [sh_rc_trow], ax
    mov ax, [es:si+4]
    mov [sh_rc_tcol], ax
    mov al, [es:si+SH_S_FLAGS]
    mov [sh_rc_tflags], al
    mov al, [es:si+SH_S_FMT]
    mov [sh_rc_tfmt], al
    push di
    push cx
    mov di, sh_rc_tval
    mov cx, 4
.rstval:
    mov ax, [es:si+SH_S_VAL]
    mov [di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .rstval
    sub si, 8
    pop cx
    pop di
    mov ax, [es:si+SH_S_FML]
    mov [sh_rc_tfml], ax
    mov al, [es:si+SH_S_TYPE]
    mov [sh_rc_ttype], al
    mov al, [es:si+SH_S_AUX]
    mov [sh_rc_taux], al
    mov ax, [sh_rc_tcol]
    mov bx, [sh_rc_trow]
    call sh_addcell
    jc .reinsnext                     ; array full - can't happen (we only
                                       ; ever re-insert as many records as
                                       ; we removed minus drops), stay safe
    mov es, [sh_cellseg]
    mov al, [sh_rc_tflags]
    mov [es:di+4], al
    mov al, [sh_rc_tfmt]
    mov [es:di+5], al
    mov al, [sh_rc_ttype]             ; over sh_addcell's SH_T_NUM default -
    mov [es:di+SH_C_TYPE], al         ; a label keeps being a label, an error
    mov al, [sh_rc_taux]              ; keeps its code
    mov [es:di+SH_C_AUX], al
    push si
    push cx
    mov si, sh_rc_tval
    mov cx, 4
.rcput:
    mov ax, [si]
    mov [es:di+SH_C_VAL], ax
    add si, 2
    add di, 2
    dec cx
    jnz .rcput
    sub di, 8
    pop cx
    pop si
    mov ax, [sh_rc_tfml]
    mov [es:di+SH_C_FOFF], ax
.reinsnext:
    inc cx
    jmp .reins
.reinsdone:
    mov ax, [sh_rc_savedsheet]
    mov [sh_cursheet], ax
    call sh_rowcol_reidx               ; stage 2.x: fix up every formula's
                                        ; own cell references for this same
                                        ; shift - see its own header comment
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; sh_rowcol_reidx and its helpers - stage 2.x: after sh_rowcol_op has
; shifted every cell record's own position, this second pass fixes up the
; TEXT of every formula whose cell references point at or past the pivot,
; so a formula still means what it looked like it meant before the
; insert/delete. Previously (and still true of Fill Right/Down and Sort
; Column, a deliberate scope cut documented at their own call sites)
; formula TEXT was left completely untouched by a row/col shift - only
; the referenced CELLS moved, silently breaking any formula that pointed
; at or past the pivot (a formula "=A1" one row below an inserted row
; kept saying A1 even though the data it meant is now at A2). This pass
; closes that gap for Insert/Delete Row/Column specifically, per direct
; user report.
;
; Method: walk the cell array once (this is now in its POST-shift,
; correct positions), and for every formula cell, copy its text out,
; run it through sh_formula_reidx (a character scanner - not a full
; parse/reserialize - that recognizes exactly the same token shapes the
; real formula grammar does: an optional "SheetN!" prefix, then 1-7
; letters immediately followed by digits is a cell reference; anything
; inside a double-quoted string, per ALERT's own argument, is copied
; byte-for-byte and never scanned), and if the text actually changed,
; appends the new text to the pool (formula text is append-only - see
; sh_setformula's own header comment - so an edit here is "append new,
; abandon old" exactly like every other formula edit already is) and
; repoints the cell record's formula_off at it.
;
; A reference is only ever touched if it is KNOWN to name the sheet this
; whole operation is acting on: either a bare, unprefixed reference
; inside a formula that itself lives on that sheet (the overwhelmingly
; common case - editing your own sheet's own formulas), or an explicit
; "SheetN!" reference naming that sheet from ANYWHERE else. A bare
; reference inside a formula that lives on a DIFFERENT sheet is left
; alone - it means that OTHER sheet's own same cell, never the one being
; shifted.
;
; A reference at exactly the pivot on a DELETE is clamped to stay at the
; pivot (the row/col that used to be one further along now occupies that
; slot) rather than invented as some error value - this project has no
; error-value concept anywhere else either (RK's unsupported subtype and
; a division by zero both degrade the same "closest sane fallback, never
; crash" way).
; =============================================================================

; sh_isletter_at - in: SI; out: CF=1 if [SI] is A-Z or a-z (SI untouched)
sh_isletter_at:
    push ax
    mov al, [si]
    cmp al, '$'                       ; stage 3.0e: '$A$1' starts a reference
    je .yes                           ; just as 'A1' does - the rewriters'
    cmp al, 'A'                       ; scanners enter on this test, so an
    jb .no                            ; absolute ref is invisible to them
                                      ; without it
    cmp al, 'Z'
    jbe .yes
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
.yes:
    stc
    jmp .out
.no:
    clc
.out:
    pop ax
    ret

; sh_rw_emit - in: AL = one byte; appends it to sh_rwdst at [sh_rw_di],
; clipping (silently dropping the byte) rather than overrunning the
; buffer - same "clip, don't refuse" policy ALERT's own message copy uses.
;
; The clip is at SH_EDITMAX, NOT at sh_rwdst's own SH_RW_CAP size: every
; consumer of a rewritten formula assumes formula text fits the same
; SH_EDITMAX+1 = 64 bytes a typed formula does - sh_eval_cell's own
; per-recursion-level sh_fbuf slot, sh_beginedit's sh_editbuf,
; sh_docmd_copy's sh_clipbuf, and sh_drawbar's sh_tbuf+16 span are all
; exactly that size. A shift CAN legitimately grow text (row 9 -> 10,
; column Z -> AA), so the extra SH_RW_CAP slack is real working room; but
; letting the RESULT exceed SH_EDITMAX would overrun all four of those
; downstream buffers (sh_setformula/sh_txt_append only bound against the
; whole pool, not against 64), so growth past the cap is dropped here at
; the single choke point every rewriter shares rather than re-checked at
; each of the five call sites.
sh_rw_emit:
    push bx
    push di
    mov bx, [sh_rw_di]
    cmp bx, SH_EDITMAX
    jae .full
    mov di, sh_rwdst
    add di, bx
    mov [di], al
    inc bx
    mov [sh_rw_di], bx
.full:
    pop di
    pop bx
    ret

; sh_txt_append - in: DS:SI = NUL-terminated text (no leading '=');
; out: CF=0 and AX = its new offset in the text pool, or CF=1 if there is
; no room (the pool is left unchanged either way)
sh_txt_append:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, si
    xor cx, cx
.len:
    cmp byte [bx], 0
    je .havelen
    inc bx
    inc cx
    jmp .len
.havelen:
    mov ax, [sh_txtlen]
    add ax, cx
    inc ax
    cmp ax, SH_TXT_CAP
    ja .noroom
    mov es, [sh_txtseg]
    mov di, [sh_txtlen]
    mov ax, di
    push ax
.copy:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    or al, al
    jnz .copy
    mov [sh_txtlen], di
    pop ax
    clc
    jmp .out
.noroom:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; sh_reidx_shift - in: AX = a reference's original 0-based row or col,
; BX = the pivot, [sh_rw_op] = sh_rowcol_op's own AL (0 ins-row/1 del-row/
; 2 ins-col/3 del-col); out: AX = the adjusted index
sh_reidx_shift:
    push cx
    push dx
    mov dl, [sh_rw_op]
    test dl, 1
    jnz .delete
    cmp ax, bx
    jb .out
    inc ax
    mov cx, SH_ROWS                    ; clamp at the grid edge rather than
    cmp dl, 2                          ; letting an insert push a reference
    jb .havecap                        ; past it - a row 16384 reference
    mov cx, SH_COLS                    ; shifted down would otherwise be
.havecap:                              ; written as "A16385", which
    cmp ax, cx                         ; sh_pident then resolves wrongly.
    jb .out                            ; (The cell it names is genuinely
    mov ax, cx                         ; gone; naming the last real row is
    dec ax                             ; the same "closest sane fallback"
    jmp .out                           ; the delete branch below already
.delete:                               ; uses for a reference ON the pivot.)
    cmp ax, bx
    jbe .out                           ; < pivot: untouched; == pivot:
                                        ; clamped (see the header comment)
    dec ax
.out:
    pop dx
    pop cx
    ret

; sh_reidx_apply - in: [sh_rw_refcol]/[sh_rw_refrow] = the reference as
; parsed, [sh_rw_ostart]/[sh_rw_lettersend]/[sh_rw_refend] = its own text
; spans, [sh_rw_op]/[sh_rw_pivot] = the shift; emits the adjusted
; reference (only the axis [sh_rw_op] actually operates on is
; recomputed - the other axis's ORIGINAL text is copied verbatim, so a
; row-only shift never touches a column's own case/spelling)
sh_reidx_apply:
    push ax
    push bx
    mov al, [sh_rw_op]
    cmp al, 2
    jae .colop
    mov ax, [sh_rw_refrow]             ; INSERT/DELETE SHIFTS AN ABSOLUTE
    mov bx, [sh_rw_pivot]              ; REFERENCE TOO, and that is not an
    call sh_reidx_shift                ; oversight. '$' means "do not adjust
    mov [sh_rw_refrow], ax             ; when this formula is COPIED"; it does
.rowletcopy:                           ; not mean "keep pointing at row 1 no
                                       ; matter what". Inserting a row above
                                       ; physically moves the referenced cell
                                       ; down, so every reference to it must
                                       ; follow or it silently starts naming
                                       ; different data - '$A$1' becomes
                                       ; '$A$2', exactly as Excel does. The
                                       ; markers are preserved below; only the
                                       ; index moves.
    mov bx, [sh_rw_ostart]             ; ostart is before any '$', so this
.rowletloop:                           ; copy carries the column's marker
    cmp bx, [sh_rw_lettersend]
    jae .rowdigits
    mov al, [bx]
    call sh_rw_emit
    inc bx
    jmp .rowletloop
.rowdigits:
    cmp byte [sh_rw_absr], 0           ; put the row's own '$' back
    je .rownodollar
    mov al, '$'
    call sh_rw_emit
.rownodollar:
    mov ax, [sh_rw_refrow]
    inc ax                             ; back to 1-based display text
    call sh_itoa
    mov bx, sh_numbuf
.rowdigemit:
    mov al, [bx]
    or al, al
    jz .out
    call sh_rw_emit
    inc bx
    jmp .rowdigemit
.colop:
    mov ax, [sh_rw_refcol]             ; same rule for a column insert/delete
    mov bx, [sh_rw_pivot]              ; as for a row - see .rowletcopy above
    call sh_reidx_shift
    mov [sh_rw_refcol], ax
.colemitstart:
    cmp byte [sh_rw_absc], 0           ; the letters are REGENERATED here, so
    je .colnodollar                    ; the marker has to be re-emitted
    mov al, '$'
    call sh_rw_emit
.colnodollar:
    mov ax, [sh_rw_refcol]
    call sh_colname
    mov bx, sh_colbuf
.colemit:
    mov al, [bx]
    or al, al
    jz .coldigits
    call sh_rw_emit
    inc bx
    jmp .colemit
.coldigits:
    mov bx, [sh_rw_lettersend]
.coldigcopy:
    cmp bx, [sh_rw_refend]
    jae .out
    mov al, [bx]
    call sh_rw_emit
    inc bx
    jmp .coldigcopy
.out:
    pop bx
    pop ax
    ret

; sh_reidx_cellpart - in: SI at a cell reference's first letter (the
; caller has already confirmed one is there via sh_isletter_at), DL = 1
; adjust this reference (it is known to name the sheet being shifted) or
; 0 leave it exactly as written; out: SI advanced past the whole
; reference (letters and, if any followed, digits) and the reference (or
; the bare word, if a letter run here turns out NOT to be followed by a
; digit - a function name, not a cell reference) emitted to sh_rwdst
; either verbatim or adjusted
sh_reidx_cellpart:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [sh_rw_adj], dl
    mov [sh_rw_ostart], si
    mov byte [sh_rw_absc], 0           ; stage 3.0e: '$' before the letters
    mov byte [sh_rw_absr], 0           ; pins the COLUMN, '$' before the
    cmp byte [si], '$'                 ; digits pins the ROW
    jne .nocoldollar
    mov byte [sh_rw_absc], 1
    inc si
.nocoldollar:
    mov di, sh_ident
    xor cx, cx
.letters:
    mov al, [si]
    cmp al, 'A'
    jb .doneletters
    cmp al, 'Z'
    jbe .isletter
    cmp al, 'a'
    jb .doneletters
    cmp al, 'z'
    ja .doneletters
.isletter:
    cmp cx, 7
    jae .doneletters
    mov ah, al
    and ah, 0xDF
    mov [di], ah
    inc di
    inc cx
    inc si
    jmp .letters
.doneletters:
    mov byte [di], 0
    mov [sh_rw_lettersend], si
    cmp byte [si], '$'
    jne .norowdollar
    mov byte [sh_rw_absr], 1
    inc si
.norowdollar:
    mov al, [si]
    cmp al, '0'
    jb .notref
    cmp al, '9'
    ja .notref
    call sh_identcol                   ; ax = 0-based col (from sh_ident)
    mov [sh_rw_refcol], ax
    mov bx, si
    add bx, SH_EDITMAX + 1
    push es
    mov ax, ds
    mov es, ax
    call sh_pint                       ; ax = 1-based row text; si advances
    pop es
    dec ax                             ; ax = 0-based row
    mov [sh_rw_refrow], ax
    mov [sh_rw_refend], si
    cmp byte [sh_rw_adj], 0
    je .verbatim
    call sh_reidx_apply
    jmp .out
.verbatim:
    mov bx, [sh_rw_ostart]
.vcopy:
    cmp bx, si
    jae .out
    mov al, [bx]
    call sh_rw_emit
    inc bx
    jmp .vcopy
.notref:
    mov bx, [sh_rw_ostart]
.wcopy:
    cmp bx, si
    jae .out
    mov al, [bx]
    call sh_rw_emit
    inc bx
    jmp .wcopy
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; A1 <-> R1C1, for SYLK's ;E field (stage 4.x)
;
; SYLK CARRIES FORMULAS IN R1C1 RELATIVE FORM, not in the A1 form this app
; stores and shows. That is not a preference - it is what the format is, and a
; real file from the period reads
;
;     C;X3;E+R[-6]C[-1]-RC[-1];K100.73
;
; where R[-6]C[-1] is "six rows up, one column left" of the cell being defined.
; An ABSOLUTE reference has no brackets: R6C3 means row 6, column 3 outright,
; which is exactly what '$' means in A1 form - so the two notations carry the
; same distinction and it survives the trip.
;
; Both directions reuse sh_formula_reidx's scanner shape: walk the text, copy
; everything that is not a reference verbatim, and transform the references.
; A quoted string is passed through untouched, as it is there.
;
; THE CROSS-SHEET PREFIX IS AN EXTENSION. SYLK has no notion of a second sheet
; - it is a single-grid format - so "Sheet2!" is written through verbatim. It
; round-trips within this app and means nothing to anything else, which is the
; honest position: the alternative is silently dropping the reference.
; =============================================================================

; sh_emit_num - AX as signed decimal, into sh_rwdst via sh_rw_emit
sh_emit_num:
    push ax
    push bx
    call sh_itoa
    mov bx, sh_numbuf
.e:
    mov al, [bx]
    or al, al
    jz .o
    call sh_rw_emit
    inc bx
    jmp .e
.o:
    pop bx
    pop ax
    ret

; sh_emit_rc - one R or C part. in: AL = 'R' or 'C', BX = the value,
; CL = 0 relative (bracketed offset, omitted entirely when zero) or 1 absolute
; (a bare 1-based index).
sh_emit_rc:
    push ax
    push bx
    call sh_rw_emit                   ; the letter itself
    or cl, cl
    jnz .abs
    or bx, bx
    jz .out                           ; a zero offset is written as nothing:
    mov al, '['                       ; "RC" means "this row, this column"
    call sh_rw_emit
    mov ax, bx
    call sh_emit_num
    mov al, ']'
    call sh_rw_emit
    jmp .out
.abs:
    mov ax, bx
    inc ax                            ; absolute parts are 1-based
    call sh_emit_num
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_formula_to_r1c1 - in: SI = A1-form formula text (no leading '='),
; [sh_rc_ccol]/[sh_rc_crow] = the cell that owns it.
; out: sh_rwdst holds the R1C1 form, [sh_rw_di] its length.
; -----------------------------------------------------------------------------
sh_formula_to_r1c1:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [sh_rw_di], 0
.loop:
    mov al, [si]
    or al, al
    jz .done
    cmp al, '"'
    jne .tryref
    call sh_rw_emit
    inc si
.instr:
    mov al, [si]
    or al, al
    jz .done
    call sh_rw_emit
    inc si
    cmp al, '"'
    jne .instr
    jmp .loop
.tryref:
    call sh_isletter_at
    jnc .literal
    mov [sh_rw_ostart], si
    call sh_psheetpfx
    jnc .noxsheet
    mov si, [sh_rw_ostart]            ; the prefix goes through verbatim
    mov bx, 7
.pfx:
    mov al, [si]
    call sh_rw_emit
    inc si
    dec bx
    jnz .pfx
    mov [sh_rw_ostart], si
.noxsheet:
    call sh_reidx_cellpart_probe      ; is this really a reference?
    jc .isref
    mov si, [sh_rw_ostart]            ; no: a function name or a bare word
.word:
    call sh_isletter_at
    jnc .loop
    mov al, [si]
    call sh_rw_emit
    inc si
    jmp .word
.isref:
    mov al, 'R'                       ; ...the row part
    mov bx, [sh_rw_refrow]
    mov cl, [sh_rw_absr]
    or cl, cl
    jnz .rowabs
    sub bx, [sh_rc_crow]              ; relative: an offset from this cell
.rowabs:
    call sh_emit_rc
    mov al, 'C'                       ; ...and the column part
    mov bx, [sh_rw_refcol]
    mov cl, [sh_rw_absc]
    or cl, cl
    jnz .colabs
    sub bx, [sh_rc_ccol]
.colabs:
    call sh_emit_rc
    jmp .loop
.literal:
    mov al, [si]
    call sh_rw_emit
    inc si
    jmp .loop
.done:
    mov bx, [sh_rw_di]
    mov byte [sh_rwdst + bx], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_reidx_cellpart_probe - SI at a possible reference. Fills sh_rw_refcol/
; refrow/absc/absr and returns CF=1 with SI past it. CF=0 means the letters
; were NOT followed by a row number (a function name, say) - SI is left where
; the scan stopped, and the caller rewinds it from sh_rw_ostart, which is the
; same contract sh_reidx_cellpart works to.
sh_reidx_cellpart_probe:
    push ax
    push cx
    push di
    mov di, sh_ident
    xor cx, cx
    mov byte [sh_rw_absc], 0
    mov byte [sh_rw_absr], 0
    cmp byte [si], '$'
    jne .nc
    mov byte [sh_rw_absc], 1
    inc si
.nc:
.letters:
    mov al, [si]
    cmp al, 'A'
    jb .doneletters
    cmp al, 'Z'
    jbe .isl
    cmp al, 'a'
    jb .doneletters
    cmp al, 'z'
    ja .doneletters
.isl:
    cmp cx, 2
    jae .doneletters
    and al, 0xDF
    mov [di], al
    inc di
    inc cx
    inc si
    jmp .letters
.doneletters:
    mov byte [di], 0
    or cx, cx
    jz .no
    cmp byte [si], '$'
    jne .nr
    mov byte [sh_rw_absr], 1
    inc si
.nr:
    mov al, [si]
    cmp al, '0'
    jb .no
    cmp al, '9'
    ja .no
    call sh_identcol
    mov [sh_rw_refcol], ax
    push bx
    mov bx, si
    add bx, SH_EDITMAX + 1
    push es
    mov ax, ds
    mov es, ax
    call sh_pint
    pop es
    pop bx
    dec ax
    mov [sh_rw_refrow], ax
    pop di
    pop cx
    pop ax
    stc
    ret
.no:
    pop di
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; sh_formula_from_r1c1 - in: SI = R1C1-form text, [sh_rc_ccol]/[sh_rc_crow] =
; the cell that owns it. out: sh_rwdst holds the A1 form, NUL-terminated.
;
; The inverse of the above. A reference starts at an 'R' that is followed by
; '[', a digit, '-' or 'C' - which is what tells "R[-1]C" apart from a function
; name beginning with R, and the reason this looks one character further ahead
; than the A1 scanner needs to.
; -----------------------------------------------------------------------------
sh_formula_from_r1c1:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [sh_rw_di], 0
.loop:
    mov al, [si]
    or al, al
    jz .done
    cmp al, '"'
    jne .tryref
    call sh_rw_emit
    inc si
.instr:
    mov al, [si]
    or al, al
    jz .done
    call sh_rw_emit
    inc si
    cmp al, '"'
    jne .instr
    jmp .loop
.tryref:
    mov al, [si]
    and al, 0xDF
    cmp al, 'R'
    jne .literal
    mov [sh_rw_ostart], si
    inc si
    call sh_read_rc                   ; -> BX = value, CL = 1 if absolute
    jc .notref
    mov [sh_rw_refrow], bx
    mov [sh_rw_absr], cl
    mov al, [si]
    and al, 0xDF
    cmp al, 'C'
    jne .notref
    inc si
    call sh_read_rc
    jc .notref
    mov [sh_rw_refcol], bx
    mov [sh_rw_absc], cl
    ; --- emit it as A1 ---
    cmp byte [sh_rw_absc], 0
    je .colrel
    mov al, '$'
    call sh_rw_emit
    jmp .colemit
.colrel:
    mov ax, [sh_rw_refcol]
    add ax, [sh_rc_ccol]
    mov [sh_rw_refcol], ax
.colemit:
    mov ax, [sh_rw_refcol]
    call sh_colname
    mov bx, sh_colbuf
.cl:
    mov al, [bx]
    or al, al
    jz .rowpart
    call sh_rw_emit
    inc bx
    jmp .cl
.rowpart:
    cmp byte [sh_rw_absr], 0
    je .rowrel
    mov al, '$'
    call sh_rw_emit
    jmp .rowemit
.rowrel:
    mov ax, [sh_rw_refrow]
    add ax, [sh_rc_crow]
    mov [sh_rw_refrow], ax
.rowemit:
    mov ax, [sh_rw_refrow]
    inc ax                            ; back to the 1-based display row
    call sh_emit_num
    jmp .loop
.notref:
    mov si, [sh_rw_ostart]            ; not a reference after all: the 'R' and
    mov al, [si]                      ; whatever follows go through as text
    call sh_rw_emit
    inc si
    jmp .loop
.literal:
    mov al, [si]
    call sh_rw_emit
    inc si
    jmp .loop
.done:
    mov bx, [sh_rw_di]
    mov byte [sh_rwdst + bx], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_read_rc - SI just past an 'R' or 'C'. out: BX = the value (a signed offset
; when relative, a 0-based index when absolute), CL = 1 if absolute, SI
; advanced. CF=1 if what follows is neither a bracket nor a digit.
sh_read_rc:
    push ax
    push dx
    xor bx, bx
    xor cl, cl
    cmp byte [si], '['
    je .rel
    mov al, [si]                      ; a bare digit means absolute
    cmp al, '0'
    jb .zero                          ; neither: "RC" - a zero offset
    cmp al, '9'
    ja .zero
    mov cl, 1
    call sh_read_int
    dec bx                            ; absolute parts are 1-based on the wire
    jmp .ok
.rel:
    inc si
    call sh_read_int                  ; the bracketed offset, sign and all
    cmp byte [si], ']'
    jne .bad
    inc si
    jmp .ok
.zero:
    cmp byte [si], 'C'                ; "RC..." - this part is simply zero
    je .ok
    cmp byte [si], 'c'
    je .ok
    or bx, bx                         ; end of the reference is fine too
    jmp .ok
.ok:
    pop dx
    pop ax
    clc
    ret
.bad:
    pop dx
    pop ax
    stc
    ret

; sh_read_int - a signed decimal at SI into BX; SI advanced. Used only by the
; R1C1 reader, where the number is known to be short.
sh_read_int:
    push ax
    push cx
    push dx
    xor bx, bx
    xor cx, cx                        ; cx = 1 when negative
    cmp byte [si], '-'
    jne .d
    mov cx, 1
    inc si
.d:
    mov al, [si]
    cmp al, '0'
    jb .fin
    cmp al, '9'
    ja .fin
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, bx
    mov dx, 10
    imul dx
    mov bx, ax
    pop ax
    add bx, ax
    inc si
    jmp .d
.fin:
    or cx, cx
    jz .o
    neg bx
.o:
    pop dx
    pop cx
    pop ax
    ret

; sh_formula_reidx - in: SI = source formula text (DS-resident, NUL-
; terminated, no leading '='); [sh_rw_op]/[sh_rw_pivot]/[sh_rw_tsheet]/
; [sh_rw_home] already set by the caller (sh_rowcol_reidx). Out:
; sh_rwdst holds the rewritten, NUL-terminated text, [sh_rw_di] = its
; length. See the section header comment above for the token rules.
sh_formula_reidx:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [sh_rw_di], 0
.loop:
    mov al, [si]
    or al, al
    jz .done
    cmp al, '"'
    jne .tryref
    call sh_rw_emit
    inc si
.instr:
    mov al, [si]
    or al, al
    jz .done
    call sh_rw_emit
    inc si
    cmp al, '"'
    jne .instr
    jmp .loop
.tryref:
    call sh_isletter_at
    jnc .literal
    mov [sh_rw_ostart], si
    call sh_psheetpfx
    jnc .noxsheet
    mov cx, ax                         ; cx = the sheet the prefix names
    call sh_isletter_at
    jc .pfxisref
    mov si, [sh_rw_ostart]             ; "SheetN!" not actually followed by
    mov bx, 7                          ; a reference: emit the 7 prefix
.pfxverb:                              ; bytes VERBATIM and carry on.
    mov al, [si]                       ; sh_psheetpfx has already advanced
    call sh_rw_emit                    ; SI past them, so just jumping back
    inc si                             ; to .loop (as this did before) threw
    dec bx                             ; them away - silently deleting the
    jnz .pfxverb                       ; "SHEET2!" from the rewritten text
    jmp .loop
.pfxisref:
    mov si, [sh_rw_ostart]
    mov bx, 7                          ; "SHEET" + one digit + "!" always
.copypfx:
    mov al, [si]
    call sh_rw_emit
    inc si
    dec bx
    jnz .copypfx
    cmp cx, [sh_rw_tsheet]
    jne .pfxnoadj
    mov dl, 1
    jmp .pfxgo
.pfxnoadj:
    mov dl, 0
.pfxgo:
    call sh_reidx_cellpart
    jmp .loop
.noxsheet:
    mov dl, [sh_rw_home]
    call sh_reidx_cellpart
    jmp .loop
.literal:
    mov al, [si]
    call sh_rw_emit
    inc si
    jmp .loop
.done:
    mov bx, [sh_rw_di]
    mov byte [sh_rwdst + bx], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_rowcol_reidx - the driver: walk every cell, and for each formula
; cell, run its text through sh_formula_reidx and repoint its
; formula_off if the text actually changed. [sh_rc_op]/[sh_rc_idx] are
; still exactly what sh_rowcol_op's caller passed (untouched since
; entry); [sh_cursheet] has just been restored to the sheet this whole
; operation acted on.
sh_rowcol_reidx:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [sh_cursheet]
    mov [sh_rw_tsheet], ax
    mov al, [sh_rc_op]
    mov [sh_rw_op], al
    mov ax, [sh_rc_idx]
    mov [sh_rw_pivot], ax
    xor cx, cx
.scan:
    cmp cx, [sh_ncells]
    jae .done
    mov ax, cx
    mov bx, SH_C_SZ
    mul bx
    mov [sh_rw_recdi], ax
    mov si, ax
    mov es, [sh_cellseg]
    test byte [es:si+4], 1             ; HASFORMULA
    jz .next
    mov ax, [es:si]
    call sh_unpackrow                  ; bx = this record's own sheet
    mov byte [sh_rw_home], 0
    cmp bx, [sh_rw_tsheet]
    jne .gothome
    mov byte [sh_rw_home], 1
.gothome:
    mov si, [sh_rw_recdi]
    mov ax, [es:si+SH_C_FOFF]          ; formula_off
    mov si, ax
    mov es, [sh_txtseg]
    mov di, sh_rwsrc
.copyin:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyin
    mov si, sh_rwsrc
    call sh_formula_reidx
    mov si, sh_rwsrc
    mov di, sh_rwdst
    call sh_streq                      ; CF=1 if identical
    jc .next                           ; unchanged: nothing to do
    mov si, sh_rwdst
    call sh_txt_append
    jc .next                           ; no room left: leave the stale
                                        ; (still valid, just unshifted)
                                        ; text in place rather than losing
                                        ; the formula entirely
    mov es, [sh_cellseg]
    mov di, [sh_rw_recdi]
    mov [es:di+SH_C_FOFF], ax
    mov word [es:di+SH_C_PASS], 0xFFFF        ; force re-evaluation
.next:
    inc cx
    jmp .scan
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Copy/Paste relative-reference adjustment (stage 2.x, per direct user
; report). sh_docmd_copy already remembers WHERE it copied from
; (sh_clip_col/sh_clip_row/sh_clip_valid, set below); sh_docmd_paste uses
; that plus its own destination (sh_selcol/sh_selrow) to compute a
; constant (col, row) delta and runs the copied formula's text through
; sh_formula_copyshift before handing it to sh_commit - the same "copy a
; formula, keep the cell it landed in" behavior every other spreadsheet's
; own default (non-absolute) reference already has.
;
; This reuses sh_rowcol_reidx's own low-level pieces (sh_isletter_at,
; sh_rw_emit, sh_rwsrc/sh_rwdst/sh_rw_di, sh_psheetpfx/sh_identcol/
; sh_colname/sh_pint/sh_itoa) but is otherwise a SEPARATE top-level scan,
; not a generalization of sh_formula_reidx: an Insert/Delete Row/Column
; shift only ever touches ONE axis (row XOR column) and only for
; references at or past a pivot; a copy/paste shift touches BOTH axes
; unconditionally by a fixed delta (pasting diagonally moves a reference
; diagonally too) and has no pivot or target-sheet concept at all - every
; reference in the formula, bare or "SheetN!"-prefixed alike, shifts by
; the exact same delta, matching real Excel's own relative-reference
; behavor when copying between sheets (the sheet name itself never
; changes, only the cell part does).
;
; sh_clip_valid is this instance's own memory of "the last thing *I*
; copied, and from where" - the real clipboard (OSAPI_CLIP_PUT/GET) is
; plain bytes with no provenance, so if something else overwrites it
; between the Copy and the Paste (a different app, or a different Sheet
; instance), a resulting paste here would only misfire if that unrelated
; text ALSO happens to start with '=' - accepted as a known, low-
; probability edge case rather than something worth adding real
; clipboard versioning for.
; =============================================================================

; sh_copy_shift - in: AX = a reference's original 0-based index, BX =
; the signed delta (sh_cp_coldelta or sh_cp_rowdelta), CX = that axis's
; cap (SH_COLS or SH_ROWS); out: AX = adjusted index, clamped to
; [0, CX-1] rather than allowed to go negative or off the grid - this
; project has no error-value concept anywhere (RK's unsupported subtype
; and division by zero both degrade the same "closest sane fallback,
; never crash" way)
sh_copy_shift:
    add ax, bx
    jns .nonneg
    xor ax, ax
    jmp .out
.nonneg:
    cmp ax, cx
    jb .out
    mov ax, cx
    dec ax
.out:
    ret

; sh_copy_cellpart - in: SI at a cell reference's first letter (the
; caller has already confirmed one is there via sh_isletter_at); out: SI
; advanced past the whole reference (letters and, if any followed,
; digits), and the reference emitted to sh_rwdst with BOTH its column
; and row shifted by [sh_cp_coldelta]/[sh_cp_rowdelta] - or, if this
; letter run turns out not to be followed by a digit (a function name,
; not a cell reference), the bare word emitted verbatim instead
sh_copy_cellpart:
    push ax
    push bx
    push cx
    push di
    mov [sh_cp_ostart], si
    mov byte [sh_cp_absc], 0           ; stage 3.0e: see sh_rw_absc
    mov byte [sh_cp_absr], 0
    cmp byte [si], '$'
    jne .nocoldollar
    mov byte [sh_cp_absc], 1
    inc si
.nocoldollar:
    mov di, sh_ident
    xor cx, cx
.letters:
    mov al, [si]
    cmp al, 'A'
    jb .doneletters
    cmp al, 'Z'
    jbe .isletter
    cmp al, 'a'
    jb .doneletters
    cmp al, 'z'
    ja .doneletters
.isletter:
    cmp cx, 7
    jae .doneletters
    mov ah, al
    and ah, 0xDF
    mov [di], ah
    inc di
    inc cx
    inc si
    jmp .letters
.doneletters:
    mov byte [di], 0
    mov [sh_cp_lettersend], si
    cmp byte [si], '$'
    jne .norowdollar
    mov byte [sh_cp_absr], 1
    inc si
.norowdollar:
    mov al, [si]
    cmp al, '0'
    jb .notref
    cmp al, '9'
    ja .notref
    call sh_identcol                   ; ax = 0-based col (from sh_ident)
    cmp byte [sh_cp_absc], 0           ; a pinned column does not follow the
    jne .colpinned                     ; paste's own displacement
    mov bx, [sh_cp_coldelta]
    mov cx, SH_COLS
    call sh_copy_shift
.colpinned:
    mov [sh_cp_refcol], ax
    mov bx, si
    add bx, SH_EDITMAX + 1
    push es
    mov ax, ds
    mov es, ax
    call sh_pint                       ; ax = 1-based row text; si advances
    pop es
    dec ax                             ; ax = 0-based row
    cmp byte [sh_cp_absr], 0
    jne .rowpinned
    mov bx, [sh_cp_rowdelta]
    mov cx, SH_ROWS
    call sh_copy_shift
.rowpinned:
    mov [sh_cp_refrow], ax
    mov [sh_cp_refend], si
    cmp byte [sh_cp_absc], 0           ; both halves are REGENERATED below, so
    je .cpnocoldollar                  ; both markers have to be re-emitted
    mov al, '$'
    call sh_rw_emit
.cpnocoldollar:
    mov ax, [sh_cp_refcol]
    call sh_colname
    mov bx, sh_colbuf
.colemit:
    mov al, [bx]
    or al, al
    jz .rowdigits
    call sh_rw_emit
    inc bx
    jmp .colemit
.rowdigits:
    cmp byte [sh_cp_absr], 0
    je .cpnorowdollar
    mov al, '$'
    call sh_rw_emit
.cpnorowdollar:
    mov ax, [sh_cp_refrow]
    inc ax                             ; back to 1-based display text
    call sh_itoa
    mov bx, sh_numbuf
.rowdigemit:
    mov al, [bx]
    or al, al
    jz .out
    call sh_rw_emit
    inc bx
    jmp .rowdigemit
.notref:
    mov bx, [sh_cp_ostart]
.wcopy:
    cmp bx, si
    jae .out
    mov al, [bx]
    call sh_rw_emit
    inc bx
    jmp .wcopy
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; sh_formula_copyshift - in: SI = source formula text (DS-resident, NUL-
; terminated, no leading '='); [sh_cp_coldelta]/[sh_cp_rowdelta] already
; set by the caller (sh_docmd_paste). Out: sh_rwdst holds the shifted,
; NUL-terminated text, [sh_rw_di] = its length. Same token-recognition
; and quoted-string-is-verbatim rules as sh_formula_reidx (see that
; proc's own header comment for why a character scan, not a re-parse, is
; both sufficient and safe here).
sh_formula_copyshift:
    push ax
    push bx
    push si
    mov word [sh_rw_di], 0
.loop:
    mov al, [si]
    or al, al
    jz .done
    cmp al, '"'
    jne .tryref
    call sh_rw_emit
    inc si
.instr:
    mov al, [si]
    or al, al
    jz .done
    call sh_rw_emit
    inc si
    cmp al, '"'
    jne .instr
    jmp .loop
.tryref:
    call sh_isletter_at
    jnc .literal
    mov [sh_cp_ostart], si
    call sh_psheetpfx
    jnc .noxsheet
    call sh_isletter_at
    jc .pfxisref
    mov si, [sh_cp_ostart]             ; "SheetN!" not actually followed by
    mov bx, 7                          ; a reference: emit the 7 prefix
.pfxverb:                              ; bytes VERBATIM rather than letting
    mov al, [si]                       ; them be silently deleted (see the
    call sh_rw_emit                    ; matching fix in sh_formula_reidx)
    inc si
    dec bx
    jnz .pfxverb
    jmp .loop
.pfxisref:
    mov si, [sh_cp_ostart]
    mov bx, 7                          ; "SHEET" + one digit + "!" always
.copypfx:
    mov al, [si]
    call sh_rw_emit
    inc si
    dec bx
    jnz .copypfx
    call sh_copy_cellpart              ; the sheet name itself never
    jmp .loop                          ; shifts - only the cell part does
.noxsheet:
    call sh_copy_cellpart
    jmp .loop
.literal:
    mov al, [si]
    call sh_rw_emit
    inc si
    jmp .loop
.done:
    mov bx, [sh_rw_di]
    mov byte [sh_rwdst + bx], 0
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_getcell2 - in: AX=col, BX=row; out: CF=1 occupied + DX=value, CF=0 empty.
; Also always sets [sh_curfmt] to the cell's format byte (0 if empty) -
; a side channel the drawing code reads, since none of this proc's other
; callers (SYLK/DIF/BIFF export, range folding) care about it.
; -----------------------------------------------------------------------------
sh_getcell2:
    push ax
    push bx
    push di
    call sh_findcell
    jnc .empty
    push es
    mov es, [sh_cellseg]
    mov al, [es:di+5]
    mov [sh_curfmt], al
    mov al, [es:di+SH_C_TYPE]         ; stage 4.5: and the tag, so a caller can
    mov [sh_curtype], al              ; tell a label from the zero it would
    mov al, [es:di+SH_C_AUX]          ; the error code travels with the tag,
    mov [sh_curaux], al               ; so the painter can name it
    mov ax, [es:di+SH_C_FOFF]         ; otherwise read as this cell's value
    mov [sh_curtoff], ax
    test byte [es:di+4], 1            ; HASFORMULA. An ERROR tag raises
    jz .plain                         ; sh_evalerr only AFTER this split
                                      ; (81.20): a formula cell's stored tag
                                      ; may predate the fix that unbreaks it,
                                      ; and raising from it here survived the
                                      ; clean re-evaluation below and wrote
                                      ; the old error back for the whole pass
    pop es
    ; BANK THIS CELL'S OWN IDENTITY ACROSS THE EVALUATION. sh_eval_cell
    ; recurses back through sh_getcell2 for every cell the formula names, and
    ; each of those overwrites every one of these - so a formula rendered with
    ; the FORMAT of the last cell it referenced, and, worse, with that cell's
    ; TYPE and text offset: `=A2+0` where A2 holds a label drew the label.
    ; The cell showed something that was not its value, and said nothing.
    ; Only the two the EVALUATION cannot change are banked here; the type and
    ; the error code come back from sh_eval_cell's own writeback instead.
    mov al, [sh_curfmt]
    mov bx, [sh_curtoff]
    push ax
    push bx
    call sh_eval_cell                 ; leaves the full result in sh_acc, and
    pop bx                            ; DX as its truncated form
    pop ax
    mov [sh_curfmt], al
    mov [sh_curtoff], bx               ; sh_curtype/sh_curaux are NOT banked:
                                       ; the evaluation is what decides them,
                                       ; and it publishes them at its writeback
    cmp byte [sh_curtype], SH_T_ERR   ; ...and an ERROR spreads: anything built
    jne .fresh                        ; on a broken cell is broken too - raised
    mov al, [sh_curaux]               ; from what the writeback (or a cache
    mov [sh_evalerr], al              ; hit's current tag) JUST published,
.fresh:                               ; never from the pre-evaluation one
    stc
    jmp .out
.plain:
    cmp byte [sh_curtype], SH_T_ERR   ; a formula-less error cell (sh_seterr,
    jne .pnum                         ; the BIFF reader) has no evaluation to
    mov al, [sh_curaux]               ; republish its tag, so the stored one
    mov [sh_evalerr], al              ; is current and spreads as before
.pnum:
    push si                           ; stage 4.0: the stored value is a full
    push cx                           ; double, so it comes out into sh_acc.
    mov si, sh_acc                    ; DX stays the truncated integer for the
    mov cx, 4                         ; callers that still want one.
.pcopy:
    mov ax, [es:di+SH_C_VAL]
    mov [si], ax
    add di, 2
    add si, 2
    dec cx
    jnz .pcopy
    pop cx
    pop si
    pop es
    call sh_acc_toint
    mov dx, ax
    stc
    jmp .out
.empty:
    mov byte [sh_curfmt], 0
    mov byte [sh_curtype], SH_T_BLANK
    push ax                           ; an empty cell is a zero value, and
    xor ax, ax                        ; sh_acc must say so rather than keeping
    call sh_acc_int                   ; whatever the last cell left there
    pop ax
    clc
.out:
    pop di
    pop bx
    pop ax
    ret

; =============================================================================
; The value accumulator (stage 4.0). The evaluator's working value is a double
; in sh_acc, not an integer in AX - a double does not fit a register, so it
; lives in memory and the machine stack carries a binary operator's left
; operand across the parse of its right.
;
; The INTEGER entry points below are kept as converting wrappers rather than
; being deleted. Roughly forty callers pass values as words - file readers,
; the chart scan, sort, fill, the macro engine - and converting them all in
; one change would have made a fault impossible to localise. They convert at
; the boundary and are correct for any value an integer can hold.
; =============================================================================

; sh_acc_store - pack the fp A accumulator into sh_acc
sh_acc_store:
    push di
    mov di, sh_acc
    call fp_pack_a
    pop di
    ret

; sh_acc_load_a - unpack sh_acc into fp A
sh_acc_load_a:
    push si
    mov si, sh_acc
    call fp_unpack_a
    pop si
    ret

; sh_acc_load_b - unpack sh_acc into fp B
sh_acc_load_b:
    push si
    mov si, sh_acc
    call fp_unpack_b
    pop si
    ret

; sh_acc_int - AX (signed) -> sh_acc
sh_acc_int:
    call fp_i2a
    call sh_acc_store
    ret

; sh_acc_toint - sh_acc -> AX (signed, truncated); CF=1 if it did not fit
sh_acc_toint:
    call sh_acc_load_a
    call fp_a2i
    ret

; sh_vpush - bank sh_acc on the machine stack. CLOBBERS AX (the return address
; goes through it), which is safe because the evaluator's value now lives in
; sh_acc rather than in a register.
sh_vpush:
    ; STKBALANCE-NET: +4 - banks sh_acc on the CALLER's stack for a binary operator; sh_binop_pre takes it off
    pop ax
    push word [sh_acc+6]
    push word [sh_acc+4]
    push word [sh_acc+2]
    push word [sh_acc]
    push ax
    ret

; sh_binop_pre - recover a banked left operand into fp A and load sh_acc, the
; right operand, into fp B. Pairs with exactly one sh_vpush.
sh_binop_pre:
    ; STKBALANCE-NET: -4 - the other half of sh_vpush - one call each, always paired
    pop ax
    pop word [sh_lhs]
    pop word [sh_lhs+2]
    pop word [sh_lhs+4]
    pop word [sh_lhs+6]
    push ax
    push si
    mov si, sh_lhs
    call fp_unpack_a
    pop si
    call sh_acc_load_b
    ret

; -----------------------------------------------------------------------------
; sh_setvald - in: AX=col, BX=row; the value is sh_acc. Out: CF=1 when
; refused (cell table full) - the cell keeps what it had.
; -----------------------------------------------------------------------------
sh_setvald:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    call sh_addcell
    jc .dfull
    push es
    mov es, [sh_cellseg]
    mov byte [es:di+4], 0             ; a plain value has no formula
    mov byte [es:di+SH_C_TYPE], SH_T_NUM
    mov si, sh_acc                    ; all EIGHT bytes of it
    mov cx, 4
.dcopy:
    mov ax, [si]
    mov [es:di+SH_C_VAL], ax
    add si, 2
    add di, 2
    dec cx
    jnz .dcopy
    pop es
    clc                               ; stored
    jmp .ddone
.dfull:
    stc                               ; refused - see the header
.ddone:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_seterr - in: AX=col, BX=row, DL=an Excel error code (1..7). The cell
; becomes an ERROR VALUE with a zero underneath it, which is exactly what a
; file carrying one means. Used by the BIFF reader; the SYLK and DIF readers
; do not need it, because those formats carry the FORMULA and the error is
; regenerated by evaluating it.
; -----------------------------------------------------------------------------
sh_seterr:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    mov cl, dl                        ; the code, across sh_setvald
    push cx
    push ax                           ; the column, across sh_acc_int
    xor ax, ax
    call sh_acc_int
    pop ax
    call sh_setvald                   ; creates the record, tagged SH_T_NUM
    call sh_findcell                  ; ...and now say what it really is
    pop cx
    jnc .out
    push es
    mov es, [sh_cellseg]
    mov byte [es:di+SH_C_TYPE], SH_T_ERR
    mov [es:di+SH_C_AUX], cl
    pop es
.out:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_setval - in: AX=col, BX=row, DX=value. The integer wrapper (see above).
; -----------------------------------------------------------------------------
sh_setval:
    push ax
    push bx
    push dx
    push di
    push ax                           ; the column, across the conversion -
    mov ax, dx                        ; fp_i2a takes its integer in AX
    call sh_acc_int
    pop ax
    call sh_setvald
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_setformula - in: AX=col, BX=row, SI=formula text (DS-resident,
; NUL-terminated, NOT including the leading '='). Out: CF=1 when refused
; (arena or cell table full) - the cell keeps what it had (sh_settext's
; contract, which this is a near-twin of).
; -----------------------------------------------------------------------------
sh_setformula:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [sh_fcol], ax
    mov [sh_frow], bx
    mov dx, si                        ; DX = start of the text, for the
                                       ; length count below
    xor cx, cx
.len:
    cmp byte [si], 0
    je .havelen
    inc si
    inc cx
    jmp .len
.havelen:
    mov si, dx                        ; SI = start of the text again
    mov ax, [sh_txtlen]
    add ax, cx
    inc ax                            ; +1 for the NUL this stores too
    cmp ax, SH_TXT_CAP
    ja .noroom
    mov es, [sh_txtseg]
    mov di, [sh_txtlen]
    mov [sh_newoff], di               ; where THIS formula starts
.copy:
    lodsb
    stosb
    or al, al
    jnz .copy
    mov [sh_txtlen], di
    mov ax, [sh_fcol]
    mov bx, [sh_frow]
    call sh_addcell
    jc .noroom
    push es
    mov es, [sh_cellseg]
    mov byte [es:di+4], 1             ; HASFORMULA
    mov byte [es:di+SH_C_TYPE], SH_T_NUM   ; stage 4.5: and RETAG it. Typing a
                                      ; formula over a label reuses that
                                      ; label's record, and without this the
                                      ; TEXT tag survived and the cell drew
                                      ; its old text forever while quietly
                                      ; computing the right answer underneath
    mov ax, [sh_newoff]
    mov [es:di+SH_C_FOFF], ax
    mov word [es:di+SH_C_PASS], 0xFFFF       ; a pass stamp sh_pass can never equal,
                                       ; forcing at least one real evaluation
    pop es
    clc                               ; stored
    jmp .done
.noroom:
    stc                               ; refused - see the header
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; RPN TOKENS - a formula that reaches BIFF as a FORMULA record instead of a
; flattened number.
;
; WHAT THIS DELIBERATELY DOES NOT DO, first, because the boundary is the whole
; design. It refuses any formula containing a FUNCTION CALL, and falls back to
; writing the cached value as NUMBER or RK exactly as before. The reason is not
; effort: docs/excelfileformat.pdf's section 3.12, "Built-in Sheet Functions",
; is marked *2do* - the index table is not written in that revision. A guessed
; index does not produce a broken file, it produces a file Excel opens happily
; and computes SOMETHING ELSE from, silently. That is strictly worse than
; carrying the value, which is at least right.
;
; BIFF3's tFunc and tFuncVar also take a ONE-BYTE index, so several of Sheet's
; own functions could not be expressed even with the table - POWER is 337.
;
; So: numbers, cell references, ranges, the six comparisons, + - * / ^, unary
; minus and parentheses. That is most of what a sheet actually holds, and every
; one of them is verifiable against a spec section that IS written.
;
; THE PARSER HERE IS A SECOND ONE, not the evaluator with a mode bolted on.
; sh_pexpr and friends compute; this walks the same grammar and emits. Two
; parsers can drift - but this one only ever has to answer "can I express
; this", and when it cannot the writer falls back to a path that was already
; correct. A shared parser with an emit flag would have put a second set of
; states inside the routine every cell value already depends on.
; =============================================================================
SH_PTG_ADD     equ 0x03                 ; the operator tokens (excelfileformat 3.5.7)
SH_PTG_SUB     equ 0x04
SH_PTG_MUL     equ 0x05
SH_PTG_DIV     equ 0x06
SH_PTG_POWER   equ 0x07
SH_PTG_LT      equ 0x09
SH_PTG_LE      equ 0x0A
SH_PTG_EQ      equ 0x0B
SH_PTG_GE      equ 0x0C
SH_PTG_GT      equ 0x0D
SH_PTG_NE      equ 0x0E
SH_PTG_UMINUS  equ 0x13
SH_PTG_PAREN   equ 0x15
SH_PTG_INT     equ 0x1E                 ; + a 16-bit unsigned
SH_PTG_NUM     equ 0x1F                 ; + an IEEE double
SH_PTG_FUNCV   equ 0x41                 ; tFuncV / tFuncVarV (3.7.1, 3.7.2).
SH_PTG_FUNCVARV equ 0x42                ; VALUE class throughout, like the refs
                                         ; below: a cell formula's result is a
                                         ; value, whatever the function's own
                                         ; default return class is
SH_PTG_REFV    equ 0x44                 ; value class: 3.3.4's transformation
SH_PTG_AREAV   equ 0x45                 ; turns the default R class into V inside
                                      ; an ordinary cell formula
SH_RPN_MAX   equ 96                   ; a token array longer than this is
                                      ; refused rather than truncated

; The BIFF function index for each of Sheet's own functions, INDEXED BY
; sh_functab's order - so the id sh_funcid already returns indexes straight
; into these, and adding a function to one table without the other is a
; visible hole rather than a silent mismatch.
;
; The numbers are OpenOffice.org's Documentation of the Microsoft Excel File
; Format, revision 1.42, section 3.11 (Built-In Sheet Functions). SPEC.md
; 81.10.2 was written against an EARLIER revision of the same document, in
; which that section reads only "2do" - which is why every function used to
; fall back to a cached value.
;
; 0xFF = cannot be written at the BIFF version this app emits. POWER is the
; only one: it is index 337, new in BIFF5, and past the byte BIFF3 allows.
sh_rpn_fid:
    db 4, 5, 6, 7, 0                  ; SUM AVERAGE MIN MAX COUNT
    db 1, 38, 24, 36, 37              ; IF NOT ABS AND OR
    db 183, 169, 39, 25, 197          ; PRODUCT COUNTA MOD INT TRUNC
    db 26, 184, 20, 0xFF, 27          ; SIGN FACT SQRT POWER(no) ROUND
    db 34, 35, 8, 9, 100              ; TRUE FALSE ROW COLUMN CHOOSE

; 1 = the function takes a variable number of arguments and so is written as
; tFuncVar (which carries a count byte); 0 = fixed, written as tFunc. This is
; "min par != max par" in that same table, NOT a guess about how it is used -
; TRUNC is 1..2 in BIFF3 even though this app only ever passes it one.
sh_rpn_fvar:
    db 1, 1, 1, 1, 1                  ; SUM AVERAGE MIN MAX COUNT
    db 1, 0, 0, 1, 1                  ; IF NOT ABS AND OR
    db 1, 1, 0, 0, 1                  ; PRODUCT COUNTA MOD INT TRUNC
    db 0, 0, 0, 0, 0                  ; SIGN FACT SQRT POWER ROUND
    db 0, 0, 1, 1, 1                  ; TRUE FALSE ROW COLUMN CHOOSE

; sh_rpn_isfunc - is the name at sh_rpn_p followed by a '('? out: CF=0 yes.
; Looks ahead and RESTORES nothing because it consumes nothing: sh_rpn_p is
; untouched either way, so whichever path runs next reads the name itself.
sh_rpn_isfunc:
    push ax
    push si
    mov si, [sh_rpn_p]
.skipname:
    mov al, [si]
    cmp al, 'A'
    jb .past
    cmp al, 'Z'
    jbe .next
    cmp al, 'a'
    jb .past
    cmp al, 'z'
    ja .past
.next:
    inc si
    jmp .skipname
.past:
    cmp al, ' '                       ; "SUM (" is still a call
    jne .test
.spaces:
    inc si
    mov al, [si]
    cmp al, ' '
    je .spaces
.test:
    cmp al, '('
    je .yes
    stc
    jmp .out
.yes:
    clc
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rpn_func - a built-in function call at sh_rpn_p: NAME ( arg , arg ... )
; Emits the arguments in order, then the function token. Re-entrant, because
; an argument can be another call - the function's own id and its argument
; count are kept in registers banked across the recursion rather than in bss,
; which a nested call would overwrite.
; -----------------------------------------------------------------------------
sh_rpn_func:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [sh_rpn_p]
    mov di, sh_ident
    xor cx, cx
.gather:
    mov al, [si]
    cmp al, 'a'
    jb .nolower
    cmp al, 'z'
    ja .nolower
    sub al, 32                        ; sh_funcid matches uppercase
.nolower:
    cmp al, 'A'
    jb .gathered
    cmp al, 'Z'
    ja .gathered
    cmp cx, SH_NAME_MAX
    jae .bad
    mov [di], al
    inc di
    inc si
    inc cx
    jmp .gather
.gathered:
    mov byte [di], 0
    or cx, cx
    jz .bad
    mov [sh_rpn_p], si
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], '('
    jne .bad
    inc si
    mov [sh_rpn_p], si
    call sh_funcid                    ; AL = this app's own id, 0xFF unknown
    cmp al, 0xFF
    je .bad
    xor ah, ah
    mov bx, ax                        ; BX = that id, kept across the arguments
    mov al, [sh_rpn_fid + bx]
    cmp al, 0xFF
    je .bad                           ; no index at this BIFF version
    xor cx, cx                        ; CX = how many arguments were seen
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], ')'
    je .closed                        ; NAME() - legal for TRUE, ROW, COUNT...
.arg:
    push bx                           ; sh_rpn_cmp banks only AX and SI
    push cx
    call sh_rpn_cmp
    pop cx
    pop bx
    cmp byte [sh_rpn_bad], 0
    jne .out
    inc cx
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], ','
    jne .closed
    inc si
    mov [sh_rpn_p], si
    jmp .arg
.closed:
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], ')'
    jne .bad
    inc si
    mov [sh_rpn_p], si
    cmp byte [sh_rpn_fvar + bx], 0
    jne .variable
    mov al, SH_PTG_FUNCV
    call sh_rpn_put
    jmp .index
.variable:
    mov al, SH_PTG_FUNCVARV
    call sh_rpn_put
    mov al, cl                        ; the count comes before the index
    call sh_rpn_put
.index:
    mov al, [sh_rpn_fid + bx]
    call sh_rpn_put
    cmp byte [sh_wb_xf4], 0           ; BIFF2-3 carry a ONE-byte index and
    je .out                           ; BIFF4-8 a word (3.7.1/3.7.2) - the
    xor al, al                        ; workbook writer emits BIFF4, so the
    call sh_rpn_put                   ; high byte goes out there and not here
    jmp .out
.bad:
    mov byte [sh_rpn_bad], 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rpn_emit - in: SI = the formula text (no leading '=')
; out: CF=0 and CX = bytes in sh_rpn_buf; CF=1 = cannot be expressed
; -----------------------------------------------------------------------------
sh_rpn_emit:
    push ax
    push bx
    push dx
    push si
    push di
    mov [sh_rpn_p], si
    mov word [sh_rpn_len], 0
    mov byte [sh_rpn_bad], 0
    call sh_rpn_cmp
    cmp byte [sh_rpn_bad], 0
    jne .no
    call sh_rpn_skip
    mov si, [sh_rpn_p]                ; anything left over means the grammar
    cmp byte [si], 0                  ; above did not account for all of it
    jne .no
    cmp word [sh_rpn_len], 0
    je .no
    mov cx, [sh_rpn_len]
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    stc
    ret

; sh_rpn_put - AL into the buffer; sets sh_rpn_bad if it would overflow
sh_rpn_put:
    push bx
    mov bx, [sh_rpn_len]
    cmp bx, SH_RPN_MAX
    jae .full
    mov [sh_rpn_buf + bx], al
    inc word [sh_rpn_len]
    pop bx
    ret
.full:
    mov byte [sh_rpn_bad], 1
    pop bx
    ret

sh_rpn_putw:                          ; AX, little-endian
    push ax
    call sh_rpn_put
    pop ax
    push ax
    mov al, ah
    call sh_rpn_put
    pop ax
    ret

sh_rpn_skip:                          ; past spaces
    push si
    mov si, [sh_rpn_p]
.lp:
    cmp byte [si], ' '
    jne .done
    inc si
    jmp .lp
.done:
    mov [sh_rpn_p], si
    pop si
    ret

; --- the grammar, one level per routine, mirroring sh_pcmp/sh_pexpr/... ------
; sh_rpn_cmp - the comparison level, lowest precedence
sh_rpn_cmp:
    push ax
    push si
    call sh_rpn_add
    cmp byte [sh_rpn_bad], 0
    jne .out
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    mov al, [si]
    cmp al, '='
    je .eq
    cmp al, '<'
    je .lt
    cmp al, '>'
    je .gt
    jmp .out
.eq:
    inc si
    mov [sh_rpn_p], si
    mov ah, SH_PTG_EQ
    jmp .rhs
.lt:
    inc si
    mov ah, SH_PTG_LT
    cmp byte [si], '='
    jne .lt2
    inc si
    mov ah, SH_PTG_LE
    jmp .ltdone
.lt2:
    cmp byte [si], '>'
    jne .ltdone
    inc si
    mov ah, SH_PTG_NE
.ltdone:
    mov [sh_rpn_p], si
    jmp .rhs
.gt:
    inc si
    mov ah, SH_PTG_GT
    cmp byte [si], '='
    jne .gtdone
    inc si
    mov ah, SH_PTG_GE
.gtdone:
    mov [sh_rpn_p], si
.rhs:
    push ax
    call sh_rpn_add                   ; ONE comparison only, which is what the
    pop ax                            ; evaluator does too - a<b<c is not a
    cmp byte [sh_rpn_bad], 0          ; thing either of them accepts
    jne .out
    mov al, ah
    call sh_rpn_put
.out:
    pop si
    pop ax
    ret

; sh_rpn_add - additive level, left-associative
sh_rpn_add:
    push ax
    push si
    call sh_rpn_term
.more:
    cmp byte [sh_rpn_bad], 0
    jne .out
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    mov al, [si]
    cmp al, '+'
    je .add
    cmp al, '-'
    je .sub
    jmp .out
.add:
    mov ah, SH_PTG_ADD
    jmp .go
.sub:
    mov ah, SH_PTG_SUB
.go:
    inc si
    mov [sh_rpn_p], si
    push ax
    call sh_rpn_term
    pop ax
    cmp byte [sh_rpn_bad], 0
    jne .out
    mov al, ah
    call sh_rpn_put
    jmp .more
.out:
    pop si
    pop ax
    ret

; sh_rpn_term - multiplicative level
sh_rpn_term:
    push ax
    push si
    call sh_rpn_pow
.more:
    cmp byte [sh_rpn_bad], 0
    jne .out
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    mov al, [si]
    cmp al, '*'
    je .mul
    cmp al, '/'
    je .div
    jmp .out
.mul:
    mov ah, SH_PTG_MUL
    jmp .go
.div:
    mov ah, SH_PTG_DIV
.go:
    inc si
    mov [sh_rpn_p], si
    push ax
    call sh_rpn_pow
    pop ax
    cmp byte [sh_rpn_bad], 0
    jne .out
    mov al, ah
    call sh_rpn_put
    jmp .more
.out:
    pop si
    pop ax
    ret

; sh_rpn_pow - '^', RIGHT-associative, exactly as sh_ppow is
sh_rpn_pow:
    push ax
    push si
    call sh_rpn_unary
    cmp byte [sh_rpn_bad], 0
    jne .out
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], '^'
    jne .out
    inc si
    mov [sh_rpn_p], si
    call sh_rpn_pow                   ; recursion is what makes it right-assoc
    cmp byte [sh_rpn_bad], 0
    jne .out
    mov al, SH_PTG_POWER
    call sh_rpn_put
.out:
    pop si
    pop ax
    ret

; sh_rpn_unary - a leading minus becomes tUminus AFTER its operand
sh_rpn_unary:
    push ax
    push si
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], '-'
    jne .plain
    inc si
    mov [sh_rpn_p], si
    call sh_rpn_unary
    cmp byte [sh_rpn_bad], 0
    jne .out
    mov al, SH_PTG_UMINUS
    call sh_rpn_put
    jmp .out
.plain:
    cmp byte [si], '+'                ; a leading plus is a no-op, and tUplus
    jne .factor                       ; would only add a byte
    inc si
    mov [sh_rpn_p], si
.factor:
    call sh_rpn_factor
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rpn_factor - a number, a cell reference, a range, or a parenthesised
; expression. ANYTHING ELSE REFUSES, which is where a function call lands and
; is the whole safety property of this emitter.
; -----------------------------------------------------------------------------
sh_rpn_factor:
    push ax
    push bx
    push cx
    push dx
    push si
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    mov al, [si]
    cmp al, '('
    je .paren
    cmp al, '$'
    je .ref
    cmp al, '0'
    jb .notdigit
    cmp al, '9'
    jbe .num
.notdigit:
    cmp al, '.'                       ; a leading decimal point is a number
    je .num
    cmp al, 'A'                       ; A letter starts either a reference or a
    jb .bad                           ; FUNCTION CALL, and only the character
    cmp al, 'Z'                       ; after the name tells them apart: a '('
    jbe .word                         ; means a call, anything else a reference
    cmp al, 'a'
    jb .bad
    cmp al, 'z'
    jbe .word
    jmp .bad
.word:
    call sh_rpn_isfunc                ; LOOKS AHEAD without consuming, because
    jc .ref                           ; sh_rpn_ref must re-read from the name
    call sh_rpn_func
    jmp .out
.paren:
    inc si
    mov [sh_rpn_p], si
    call sh_rpn_cmp
    cmp byte [sh_rpn_bad], 0
    jne .out
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], ')'
    jne .bad
    inc si
    mov [sh_rpn_p], si
    mov al, SH_PTG_PAREN                ; kept, because Excel round-trips it into
    call sh_rpn_put                   ; the formula it shows the user
    jmp .out
.num:
    call sh_rpn_number
    jmp .out
.ref:
    call sh_rpn_ref
    jmp .out
.bad:
    mov byte [sh_rpn_bad], 1
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rpn_number - tInt for a whole number that fits 0..65535, tNum otherwise.
; The value comes from fp_atof, so what BIFF carries and what the cell computes
; are parsed by ONE routine rather than two that could disagree about "1e3".
; -----------------------------------------------------------------------------
sh_rpn_number:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, [sh_rpn_p]
    call fp_atof                      ; SI advances past what it consumed
    jc .bad
    mov [sh_rpn_p], si
    call sh_acc_store                 ; the packed double, in sh_acc
    call sh_acc_load_a
    call fp_a2i                       ; CF=1: no signed word holds it
    jc .asnum
    or ax, ax
    js .asnum                         ; tInt is UNSIGNED; a negative literal
    mov bx, ax                        ; arrives as tUminus over a positive one
    call fp_i2a                       ; and only an exact integer may use it
    push si
    mov si, sh_acc
    call fp_unpack_b
    pop si
    call fp_cmpab
    jne .asnum
    mov al, SH_PTG_INT
    call sh_rpn_put
    mov ax, bx
    call sh_rpn_putw
    jmp .out
.asnum:
    mov al, SH_PTG_NUM
    call sh_rpn_put
    mov si, sh_acc                    ; the eight bytes, verbatim - BIFF wants
    mov cx, 8                         ; exactly the IEEE double this file
.nbyte:                               ; already stores
    mov al, [si]
    call sh_rpn_put
    inc si
    dec cx
    jnz .nbyte
    jmp .out
.bad:
    mov byte [sh_rpn_bad], 1
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_rpn_ref - a cell reference, or a range if a ':' follows.
;
; The row word carries BOTH relative flags (excelfileformat 3.4.1): bit 15 is
; the ROW's, bit 14 the COLUMN's, and set means RELATIVE. So $A$1 is 0x0000 and
; A1 is 0xC000 - the opposite polarity from how the '$' reads, which is the one
; thing here worth checking twice.
;
; A "SheetN!" prefix refuses: a cross-sheet reference is tRef3d and needs an
; EXTERNSHEET table this writer does not build.
; -----------------------------------------------------------------------------
sh_rpn_ref:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sh_rpn_one                   ; -> BX = row word, DL = col
    cmp byte [sh_rpn_bad], 0
    jne .out
    mov [sh_rpn_r1], bx
    mov [sh_rpn_c1], dl
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    cmp byte [si], ':'
    je .range
    mov al, SH_PTG_REFV
    call sh_rpn_put
    mov ax, [sh_rpn_r1]
    call sh_rpn_putw
    mov al, [sh_rpn_c1]
    call sh_rpn_put
    jmp .out
.range:
    inc si
    mov [sh_rpn_p], si
    call sh_rpn_one
    cmp byte [sh_rpn_bad], 0
    jne .out
    mov al, SH_PTG_AREAV
    call sh_rpn_put
    mov ax, [sh_rpn_r1]               ; first row, last row, first col, last col
    call sh_rpn_putw
    mov ax, bx
    call sh_rpn_putw
    mov al, [sh_rpn_c1]
    call sh_rpn_put
    mov al, dl
    call sh_rpn_put
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_rpn_one - one [$]COL[$]ROW at sh_rpn_p. out: BX = the row word with its
; two relative flags, DL = the column. Sets sh_rpn_bad and nothing else on a
; token that is not a reference.
sh_rpn_one:
    push ax
    push cx
    push si
    push di
    call sh_rpn_skip
    mov si, [sh_rpn_p]
    mov word [sh_rpn_rel], 0xC000     ; relative until a '$' says otherwise
    cmp byte [si], '$'
    jne .colletters
    inc si
    and word [sh_rpn_rel], 0x8000     ; absolute COLUMN clears bit 14
.colletters:
    mov di, sh_ident
    xor cx, cx
.gather:
    mov al, [si]
    cmp al, 'A'
    jb .gathered
    cmp al, 'Z'
    jbe .keep
    cmp al, 'a'
    jb .gathered
    cmp al, 'z'
    ja .gathered
.keep:
    cmp cx, 2                         ; two letters is the whole of 256 columns
    jae .bad
    and al, 0xDF
    mov [di], al
    inc di
    inc si
    inc cx
    jmp .gather
.gathered:
    jcxz .bad
    mov byte [di], 0
    cmp byte [si], '$'
    jne .rowdigits
    inc si
    and word [sh_rpn_rel], 0x4000     ; absolute ROW clears bit 15
.rowdigits:
    mov al, [si]
    cmp al, '0'
    jb .bad
    cmp al, '9'
    ja .bad
    xor bx, bx
.digit:
    mov al, [si]
    cmp al, '0'
    jb .haverow
    cmp al, '9'
    ja .haverow
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, bx
    mov cx, 10
    mul cx
    mov bx, ax
    pop ax
    add bx, ax
    cmp bx, SH_ROWS
    ja .bad
    inc si
    jmp .digit
.haverow:
    or bx, bx
    jz .bad                           ; row 0 does not exist; A0 is not a cell
    dec bx                            ; BIFF rows are zero-based
    call sh_identcol                  ; AX = the 0-based column
    cmp ax, SH_COLS
    jae .bad
    mov dl, al
    or bx, [sh_rpn_rel]
    mov [sh_rpn_p], si
    pop di
    pop si
    pop cx
    pop ax
    ret
.bad:
    mov byte [sh_rpn_bad], 1
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_settext - stage 4.5: store TEXT in a cell.
; in: AX = col, BX = row, SI = the NUL-terminated text
;
; Deliberately a near-twin of sh_setformula above rather than a shared routine
; the two both call. They agree on the arena copy and disagree on every flag
; that follows it, and a merged version would have been a copy of the first
; half wrapped in a parameter deciding the second - which is the same amount
; of code with a branch through the middle of it.
;
; THE TEXT LIVES IN THE FORMULA ARENA, at SH_C_FOFF, and the two never collide
; because a cell is one thing or the other: HASFORMULA clear plus a TEXT tag
; is the whole discrimination. Notes share this arena too (stage 3.0b) and are
; keyed separately, in their own table.
;
; Like a formula, retyping a label APPENDS and abandons the old bytes - the
; arena has no free list and never compacts. 8 KB is a lot of labels and this
; matches what formulas have always done, but it is a real ceiling rather than
; an oversight, and it is why .noroom below is a no-op rather than a wrong
; value written. Out: CF=1 when refused (arena or cell table full) - the cell
; keeps what it had, and a caller mid-permutation must STOP (see
; sh_sort_permcol).
; -----------------------------------------------------------------------------
sh_settext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [sh_fcol], ax
    mov [sh_frow], bx
    mov dx, si
    xor cx, cx
.len:
    cmp byte [si], 0
    je .havelen
    inc si
    inc cx
    jmp .len
.havelen:
    mov si, dx
    mov ax, [sh_txtlen]
    add ax, cx
    inc ax                            ; +1 for the NUL
    cmp ax, SH_TXT_CAP
    ja .noroom
    mov es, [sh_txtseg]
    mov di, [sh_txtlen]
    mov [sh_newoff], di
.copy:
    lodsb
    stosb
    or al, al
    jnz .copy
    mov [sh_txtlen], di
    mov ax, [sh_fcol]
    mov bx, [sh_frow]
    call sh_addcell
    jc .noroom
    push es
    mov es, [sh_cellseg]
    mov byte [es:di+4], 0             ; NOT a formula: the tag is what says
    mov byte [es:di+SH_C_TYPE], SH_T_TEXT
    mov byte [es:di+SH_C_AUX], 0
    mov ax, [sh_newoff]
    mov [es:di+SH_C_FOFF], ax
    mov word [es:di+SH_C_PASS], 0
    xor ax, ax                        ; and a numeric value of zero, so every
    mov [es:di+SH_C_VAL], ax          ; reader that has never heard of text
    mov [es:di+SH_C_VAL+2], ax        ; still gets a defined number out of it
    mov [es:di+SH_C_VAL+4], ax
    mov [es:di+SH_C_VAL+6], ax
    pop es
    clc                               ; stored
    jmp .done
.noroom:
    stc                               ; refused - see the header
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_eval_cell - evaluate a formula cell, with cycle detection and
; per-repaint memoization
; in: DI = record offset (a HASFORMULA record); ES = sh_cellseg
; out: DX = value; the record's cached value and pass stamp are updated
; -----------------------------------------------------------------------------
sh_eval_cell:
    push ax
    push bx
    push si
    push di
    push es
    mov es, [sh_cellseg]
    mov ax, [es:di+SH_C_PASS]                ; this cell's last-computed pass
    cmp ax, [sh_pass]
    jne .stale
    test byte [es:di+4], 2            ; EVALUATING - already mid-computation
    jnz .cycle                        ; means a cycle, not a cache hit
    call sh_cellval_to_acc            ; a cache hit is a full double now, not
    call sh_acc_toint                 ; a word; DX stays the truncated form
    mov dx, ax                        ; for the callers that still want one
    jmp .out
.stale:
    test byte [es:di+4], 2
    jnz .cycle
    or byte [es:di+4], 2              ; EVALUATING = 1
    push di                           ; this cell's record offset, kept on
                                       ; the stack (NOT a global) because
                                       ; evaluating this formula may recurse
                                       ; into sh_eval_cell again for a cell
                                       ; it references, and call/ret through
                                       ; sh_pexpr is stack-neutral either way
    push word [sh_evrow]              ; stage 3.0d: ROW()/COLUMN()'s context,
    push word [sh_evcol]              ; banked for the SAME reason and popped
    mov ax, [es:di]                   ; at .writeback. A referenced cell's own
    and ax, SH_ROW_MASK               ; formula must answer for ITSELF, so this
    mov [sh_evrow], ax                ; is per-frame, not set once
    mov ax, [es:di+2]
    mov [sh_evcol], ax
    mov ax, [es:di+SH_C_FOFF]                 ; formula_off
    mov si, ax
    mov es, [sh_txtseg]
    cmp word [sh_evaldepth], SH_EVAL_MAXDEPTH
    jae .toodeep
    cmp word [sh_evaldepth], 0        ; a FRESH evaluation starts clean; a
    jne .depthok                      ; nested one must not, or a referenced
    mov byte [sh_evalerr], 0          ; cell's error would be wiped on the way
.depthok:                             ; back up
    mov bx, [sh_evaldepth]
    inc word [sh_evaldepth]
    push bx                           ; this recursion level's buffer slot
    mov ax, SH_EDITMAX + 1
    mul bx
    add ax, sh_fbuf
    mov di, ax                        ; DI = this level's OWN copy of the
                                       ; formula text - a nested evaluation
                                       ; (of a cell THIS formula references)
                                       ; gets a DIFFERENT slot, so it cannot
                                       ; overwrite the text we are still
                                       ; parsing
    mov bx, di
.copyin:
    mov al, [es:si]
    mov [di], al                      ; DS-relative: our own scratch buffer
    inc si
    inc di
    or al, al
    jnz .copyin
    mov si, bx
    call sh_pcmp                      ; the result lands in sh_acc, and may
    pop bx                            ; have recursed to get there
    dec word [sh_evaldepth]
    jmp .writeback
.toodeep:
    xor dx, dx
    push ax
    xor ax, ax
    call sh_acc_int                   ; too deep is a zero, in both forms
    pop ax
.writeback:
    pop word [sh_evcol]               ; stage 3.0d: ROW()/COLUMN() context,
    pop word [sh_evrow]               ; restored in the order it was pushed
    pop di                            ; this cell's record offset, restored
    mov es, [sh_cellseg]
    and byte [es:di+4], 0xFD          ; EVALUATING = 0 (HASFORMULA untouched)
    call sh_acc_to_cellval            ; cache the whole double
    call sh_acc_toint
    mov dx, ax
    mov al, [sh_evalerr]              ; the cell is stored by what it IS: an
    or al, al                         ; error, or a number again once whatever
    jz .notanerr                      ; broke it has been fixed
    mov byte [es:di+SH_C_TYPE], SH_T_ERR
    mov [es:di+SH_C_AUX], al
    mov byte [sh_curtype], SH_T_ERR   ; and PUBLISH it: the caller banked these
    mov [sh_curaux], al               ; two before the evaluation ran, so its
    jmp .errdone                      ; copy names the cell as it USED to be -
.notanerr:                            ; which paints #DIV/0! on a cell whose
    mov byte [es:di+SH_C_TYPE], SH_T_NUM  ; divisor has since been fixed, and
    mov byte [es:di+SH_C_AUX], 0      ; #ERR on the one that is still broken
    mov byte [sh_curtype], SH_T_NUM   ; (its code having been overwritten by
    mov byte [sh_curaux], 0           ; the last cell the formula referenced)
.errdone:
    mov ax, [sh_pass]
    mov [es:di+SH_C_PASS], ax
    jmp .out
.cycle:
    xor dx, dx
    push ax
    xor ax, ax
    call sh_acc_int                   ; a cycle is a zero, in both forms
    pop ax
.out:
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; Formula parser/evaluator - recursive descent over sh_fbuf (a DS-resident
; copy of the formula text; see sh_eval_cell). Grammar:
;   expr   := term (('+'|'-') term)*
;   term   := pow (('*'|'/') pow)*
;   pow    := factor ('^' pow)?            ; right-associative (stage 3.0d)
;   factor := '-' factor | '(' expr ')' | NUMBER | CELLREF | NAME '(' args ')'
;   args   := arg (',' arg)*
;   arg    := CELLREF ':' CELLREF | expr
; No spaces (the editor never lets one through), so no whitespace skipping.
; Every value is a 16-bit signed integer; division truncates toward zero
; (IDIV) and division by zero yields 0 rather than faulting - a stated
; simplification, not an oversight, matching this project's "no formulas,
; no formatting" -> "formulas, still no formatting" progression: nothing
; here produces or accepts a fraction. SUM/AVERAGE/MIN/MAX/COUNT are "the
; most common formulas" the roadmap asks for first; comparisons, IF() and
; the rest are later-stage work.
; =============================================================================

; sh_pcmp / sh_pcmpcont - comparison level, the actual top of the grammar
; (sh_eval_cell enters here, not at sh_pexpr): one optional
; '=' '<' '>' '<=' '>=' '<>' against an additive expression, producing 1
; (true) or 0 (false). Not chained - "A1<B1<C1" parses the same as most
; spreadsheets treat it, as one comparison ("A1<B1") followed by a second
; expression ("<C1") that the caller's own grammar level decides what to
; do with, which in practice just stops parsing there - matching this
; project's general rule of degrading a malformed tail rather than
; raising an error nothing here has a channel to report through.
sh_pcmp:
    call sh_pexpr
sh_pcmpcont:
    cmp byte [si], '='
    je .eq
    cmp byte [si], '<'
    je .lt_le_ne
    cmp byte [si], '>'
    je .gt_ge
    ret
.eq:
    inc si
    call sh_vpush
    call sh_pexpr
    call sh_binop_pre
    call fp_cmpab                     ; AX = -1/0/1 with the flags to match,
    je .true                          ; so the six tests below read exactly as
    jmp .false                        ; the integer CMPs they replace
.lt_le_ne:
    inc si
    cmp byte [si], '='
    je .le
    cmp byte [si], '>'
    je .ne
    call sh_vpush
    call sh_pexpr
    call sh_binop_pre
    call fp_cmpab
    jl .true
    jmp .false
.le:
    inc si
    call sh_vpush
    call sh_pexpr
    call sh_binop_pre
    call fp_cmpab
    jle .true
    jmp .false
.ne:
    inc si
    call sh_vpush
    call sh_pexpr
    call sh_binop_pre
    call fp_cmpab
    jne .true
    jmp .false
.gt_ge:
    inc si
    cmp byte [si], '='
    je .ge
    call sh_vpush
    call sh_pexpr
    call sh_binop_pre
    call fp_cmpab
    jg .true
    jmp .false
.ge:
    inc si
    call sh_vpush
    call sh_pexpr
    call sh_binop_pre
    call fp_cmpab
    jge .true
    jmp .false
.true:
    mov ax, 1
    call sh_acc_int
    ret
.false:
    xor ax, ax
    call sh_acc_int
    ret

; sh_pexpr / sh_pexprcont - additive level. sh_pexprcont is a real entry
; point of its own: sh_prange calls it to resume the +/- loop after folding
; a lone cell reference that turned out not to start a range.
sh_pexpr:
    call sh_pterm
sh_pexprcont:
    cmp byte [si], '+'
    je .add
    cmp byte [si], '-'
    je .sub
    ret
.add:
    call sh_chktext                   ; the LEFT operand, which sh_curtype
    inc si                            ; still describes
    call sh_vpush                     ; the left operand goes on the machine
    call sh_pterm                     ; stack: a double does not fit a register
    call sh_chktext                   ; ...and now the right
    call sh_binop_pre                 ; and the parse of the right may recurse
    call fp_add
    call sh_acc_store
    jmp sh_pexprcont
.sub:
    call sh_chktext
    inc si
    call sh_vpush
    call sh_pterm
    call sh_chktext
    call sh_binop_pre
    call fp_sub
    call sh_acc_store
    jmp sh_pexprcont

; sh_pterm / sh_ptermcont - multiplicative level, same reasoning as above.
sh_pterm:
    call sh_ppow
sh_ptermcont:
    cmp byte [si], '*'
    je .mul
    cmp byte [si], '/'
    je .div
    ret
.mul:
    call sh_chktext
    inc si
    call sh_vpush
    call sh_ppow
    call sh_chktext
    call sh_binop_pre
    call fp_mul
    call sh_acc_store
    jmp sh_ptermcont
.div:
    call sh_chktext
    inc si
    call sh_vpush
    call sh_ppow
    call sh_chktext
    call sh_binop_pre
    call fp_div                       ; CF=1 means the divisor was zero
    jnc .divok
    mov byte [sh_evalerr], SH_ERR_DIV0
    xor ax, ax                        ; the value is still zero underneath -
    call sh_acc_int                   ; the flag is what the cell is stored by
    jmp sh_ptermcont
.divok:
    call sh_acc_store
    jmp sh_ptermcont

; sh_ppow (stage 3.0d) - the '^' level, between multiplication and the
; factors. RIGHT-associative, so 2^3^2 is 2^(3^2) = 512, which is what every
; spreadsheet does; the recursion below is what makes it so, where a loop like
; sh_ptermcont's would have made it left-associative.
;
; It binds TIGHTER than '*' and looser than unary minus, so -2^2 is -(2^2).
; That is Excel's own precedence and it surprises people, but matching it is
; the point.
;
; A negative exponent is a fraction and there is no fraction here, so it
; yields 0 - the same answer this evaluator already gives for division by
; zero, and for the same stated reason.
; sh_ppowcont is a real entry point of its own, like sh_ptermcont's: sh_prange
; calls it FIRST to resume a lone cell reference that turned out not to start
; a range - without it, `=SUM(A1^2)` left the '^' for sh_pfunc's argument loop,
; which can only stop at it.
sh_ppow:
    call sh_pfactor
sh_ppowcont:
    cmp byte [si], '^'
    jne .out
    call sh_chktext
    inc si
    call sh_pnest_enter               ; '^' recurses too (81.3)
    jc .nestfull
    call sh_vpush                     ; the BASE, banked
    call sh_ppow                      ; recurse: right-associative
    call sh_pnest_leave
    call sh_chktext
    call sh_acc_toint                 ; the exponent is still a whole number -
    mov cx, ax                        ; a fractional power needs logarithms,
    pop word [sh_lhs]                 ; which this file does not have
    pop word [sh_lhs+2]
    pop word [sh_lhs+4]
    pop word [sh_lhs+6]
    mov ax, 1                         ; the running product starts at one
    call sh_acc_int
    or cx, cx
    js .zero                          ; a negative exponent is a fraction
    jz .out                           ; anything^0 = 1
.loop:
    push cx
    push si
    mov si, sh_lhs                    ; B = the base, reloaded each time -
    call fp_unpack_b                  ; fp_mul consumes it
    pop si
    call sh_acc_load_a
    call fp_mul
    call sh_acc_store
    pop cx
    dec cx
    jnz .loop
    jmp .out
.zero:
    xor ax, ax
    call sh_acc_int
.out:
    ret
.nestfull:
    push ax                           ; over the budget: the base is dropped,
    xor ax, ax                        ; and the #VALUE! sh_pnest_enter raised
    call sh_acc_int                   ; stands
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_pnest_enter / sh_pnest_leave - the parser's shared nesting budget (81.3).
; SH_EVAL_MAXDEPTH bounds cell-to-cell recursion, but nothing bounded a
; formula's OWN nesting: every '(', unary '-', '^' and nested call recurses
; the parser and banks bytes on task 0's 1,024-byte stack, and six
; memoization-cold cells chained that way could run SP off the stack's floor
; into .lowbss - silent corruption that fails later, somewhere unrelated.
; One counter charges every recursion point, with the cell depth folded in;
; over SH_PNEST_MAX the parse refuses with #VALUE! - the same refusal shape
; sh_eval_cell's .toodeep already has.
; out: CF=1 refused (sh_evalerr raised, NOTHING charged - do not leave),
;      CF=0 charged - pair with exactly one sh_pnest_leave
; -----------------------------------------------------------------------------
sh_pnest_enter:
    push ax
    mov ax, [sh_pnest]
    inc ax
    add ax, [sh_evaldepth]
    cmp ax, SH_PNEST_MAX
    ja .full
    inc word [sh_pnest]
    pop ax
    clc
    ret
.full:
    pop ax
    mov byte [sh_evalerr], SH_ERR_VALUE
    stc
    ret

sh_pnest_leave:
    dec word [sh_pnest]
    ret

; sh_pfactor - unary minus, parens, a number, or an identifier (cell
; reference or function call, sh_pident tells them apart)
sh_pfactor:
    cmp byte [si], '-'
    jne .notneg
    inc si
    call sh_pnest_enter               ; unary minus recurses (81.3)
    jc .nestfull
    call sh_pfactor
    call sh_pnest_leave
    call sh_chktext                   ; -"text" is arithmetic too
    xor byte [sh_acc+7], 0x80         ; negate by flipping the sign BIT of the
    ret                               ; packed double - cheaper than unpacking
                                      ; and, unlike `neg`, exact for every
                                      ; value including zero
.notneg:
    cmp byte [si], '('
    jne .notparen
    inc si
    call sh_pnest_enter               ; ...and so does a parenthesis
    jc .nestfull
    call sh_pcmp
    call sh_pnest_leave
    cmp byte [si], ')'
    jne .out                          ; malformed; return whatever we have
    inc si
    ret
.nestfull:
    push ax                           ; over the budget: sh_pnest_enter has
    xor ax, ax                        ; raised #VALUE!, and the refusal
    call sh_acc_int                   ; answers zero underneath it
    pop ax
    ret
.notparen:
    mov al, [si]
    cmp al, '$'                       ; stage 3.0e: '$A$1' is an IDENTIFIER,
    je .ident                         ; and this router decides that on the
    cmp al, 'A'                       ; FIRST character - without this line a
    jb .maybenum                      ; leading '$' falls through to the
    cmp al, 'Z'                       ; number path and the whole reference
    jbe .ident                        ; evaluates to 0. sh_pident tolerating
    cmp al, 'a'                       ; '$' is necessary but not sufficient.
    jb .maybenum
    cmp al, 'z'
    ja .maybenum
.ident:
    call sh_pident
    ret
.maybenum:
    mov byte [sh_curtype], SH_T_NUM   ; a LITERAL is a number - say so, or the
    call fp_atof                      ; tag left by the last cell referenced
    jnc .numok                        ; still stands and `=B4+1` is judged by
    mov byte [sh_evalerr], SH_ERR_VALUE ; B4's type twice over
    xor ax, ax                        ; nothing parseable at all: `=1+` used to
    call sh_acc_int                   ; read as 1, an answer to a formula the
    ret                               ; user never finished writing
.numok:
    call sh_acc_store
.out:
    ret

; -----------------------------------------------------------------------------
; sh_psheetpfx - stage 2.0: does SI start a "SheetN!" cross-sheet prefix?
; Sheet names are the fixed "Sheet1".."SheetN" strings (see the Sheets menu
; comment), so this is a literal, case-insensitive match against "SHEET"
; plus a digit '1'..SH_SHEETS - not a general name lookup.
; in: SI; out: CF=1 and AX=0-based sheet index, SI advanced past the '!';
; CF=0 and SI unchanged otherwise
; -----------------------------------------------------------------------------
sh_psheetpfx:
    push bx
    push cx
    mov bx, si
    mov al, [bx]
    and al, 0xDF
    cmp al, 'S'
    jne .no
    inc bx
    mov al, [bx]
    and al, 0xDF
    cmp al, 'H'
    jne .no
    inc bx
    mov al, [bx]
    and al, 0xDF
    cmp al, 'E'
    jne .no
    inc bx
    mov al, [bx]
    and al, 0xDF
    cmp al, 'E'
    jne .no
    inc bx
    mov al, [bx]
    and al, 0xDF
    cmp al, 'T'
    jne .no
    inc bx
    mov al, [bx]
    cmp al, '1'
    jb .no
    cmp al, '0' + SH_SHEETS
    ja .no
    sub al, '1'
    xor ah, ah
    mov cx, ax                        ; cx = sheet index 0..SH_SHEETS-1
    inc bx
    cmp byte [bx], '!'
    jne .no
    inc bx
    mov si, bx
    mov ax, cx
    stc
    jmp .out
.no:
    clc
.out:
    pop cx
    pop bx
    ret

; sh_pident - in: SI at an identifier's first letter
; out: AX = value (a cell's value, or a function call's result), SI advanced
sh_pident:
    push bx
    push cx
    push dx
    push di
    mov byte [sh_pxsheet], 0xFF
    call sh_psheetpfx
    jnc .noxsheet
    mov [sh_pxsheet], al
.noxsheet:
    cmp byte [si], '$'                ; stage 3.0e: skip an absolute marker
    jne .nocoldollar                  ; before the column letters
    inc si
.nocoldollar:
    mov di, sh_ident
    xor cx, cx
.collect:
    mov al, [si]
    cmp al, 'A'
    jb .doneletters
    cmp al, 'Z'
    jbe .isletter
    cmp al, 'a'
    jb .doneletters
    cmp al, 'z'
    ja .doneletters
.isletter:
    cmp cx, SH_NAME_MAX               ; a DEFINED NAME can be this long, so the
    jae .doneletters                  ; cap is its length rather than a column
                                       ; pair's - a function name is shorter
                                       ; than either
    and al, 0xDF                      ; normalize to uppercase
    mov [di], al
    inc di
    inc cx
    inc si
    jmp .collect
.doneletters:
    mov byte [di], 0
    cmp byte [si], '$'                ; ...and before the row digits
    jne .norowdollar
    inc si
.norowdollar:
    mov al, [si]
    cmp al, '0'
    jb .isfunc
    cmp al, '9'
    ja .isfunc
    call sh_identcol                  ; sh_ident -> AX = 0-based column
    mov [sh_pcol], ax
    mov bx, si
    add bx, SH_EDITMAX + 1
    push es
    mov ax, ds
    mov es, ax
    call sh_pint                      ; SI advances past the digits
    pop es
    dec ax                            ; AX = 0-based row
    mov bx, ax
    mov ax, [sh_pcol]
    cmp byte [sh_pxsheet], 0xFF
    je .samesheet
    mov cx, [sh_cursheet]              ; stage 2.0: a "SheetN!" prefix -
    push cx                            ; temporarily point sh_findcell (via
    mov cl, [sh_pxsheet]               ; sh_cursheet) at the target sheet
    xor ch, ch                         ; for this one lookup, then put it
    mov [sh_cursheet], cx              ; back - every OTHER caller of
    call sh_getcell2                   ; sh_getcell2/sh_findcell is none the
    pop cx                             ; wiser
    mov [sh_cursheet], cx
    jmp .havecell
.samesheet:
    call sh_getcell2
.havecell:
    jmp .out                          ; nothing to do: sh_getcell2 leaves the
                                      ; value in sh_acc, and leaves a ZERO
                                      ; there for a cell that does not exist -
                                      ; which is what both branches here used
                                      ; to arrange by hand
.isfunc:
    cmp byte [si], '('                ; stage 3.0c: a DEFINED NAME is an
    je .reallyfunc                    ; identifier that is not a call. Excel
    push si                           ; resolves it the same way, and the
    mov si, sh_ident                  ; parenthesis is the only thing that
    call sh_name_lookup               ; separates SUM from a cell called SUM
    pop si
    jnc .reallyfunc
    ; AX = col, BX = row: the same shape sh_pcellref hands on, so the name
    ; reaches the evaluator as an ordinary reference and everything that
    ; already works for one - the cycle check, the memoization - works for it
    call sh_getcell2                  ; which leaves the value in sh_acc, and
    jmp .out                          ; a zero there for a cell that does not
                                       ; exist yet - exactly as a reference to
                                       ; an empty cell already behaves
.reallyfunc:
    call sh_pfunc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; sh_identcol - in: sh_ident = NUL-terminated uppercase letters (1-2 chars)
; out: AX = 0-based column index (the bijective base-26 sh_colname inverts)
sh_identcol:
    push bx
    push cx
    push si
    mov si, sh_ident
    xor ax, ax
.loop:
    mov cl, [si]
    or cl, cl
    jz .done
    sub cl, 'A'
    inc cl
    xor ch, ch
    mov bx, 26
    mul bx
    add ax, cx
    inc si
    jmp .loop
.done:
    dec ax
    pop si
    pop cx
    pop bx
    ret

; sh_pcellref - a backtracking probe: does SI start a bare cell reference?
; in: SI; out: CF=1 yes (AX=col, BX=row, SI advanced past it),
;             CF=0 no (SI UNCHANGED - the caller falls back to sh_pexpr)
; Used only to tell a range's "A1:B5" apart from a plain expression that
; merely starts with a cell reference, like "A1+5".
sh_pcellref:
    push cx
    push di
    push si                           ; the only way back out on failure
    cmp byte [si], '$'                ; stage 3.0e: '$' is PURELY TEXTUAL -
    jne .nocoldollar                  ; it changes what the rewriters do, not
    inc si                            ; what this evaluates to, so the parser
.nocoldollar:                         ; only has to skip it
    mov al, [si]
    cmp al, 'A'
    jb .fail
    cmp al, 'Z'
    jbe .ok
    cmp al, 'a'
    jb .fail
    cmp al, 'z'
    ja .fail
.ok:
    mov di, sh_ident
    xor cx, cx
.collect:
    mov al, [si]
    cmp al, 'A'
    jb .doneletters
    cmp al, 'Z'
    jbe .isletter
    cmp al, 'a'
    jb .doneletters
    cmp al, 'z'
    ja .doneletters
.isletter:
    cmp cx, 2
    jae .doneletters                  ; 3+ letters: a NAME, not a column
    and al, 0xDF
    mov [di], al
    inc di
    inc cx
    inc si
    jmp .collect
.doneletters:
    mov byte [di], 0
    or cx, cx
    jz .fail
    cmp byte [si], '$'                ; ...and again before the row digits
    jne .norowdollar
    inc si
.norowdollar:
    mov al, [si]
    cmp al, '0'
    jb .fail
    cmp al, '9'
    ja .fail
    call sh_identcol
    mov cx, ax                        ; CX = col, held across sh_pint
    mov bx, si
    add bx, SH_EDITMAX + 1
    push es
    mov ax, ds
    mov es, ax
    call sh_pint
    pop es
    dec ax                            ; AX = 0-based row
    mov bx, ax
    mov ax, cx                        ; AX = col
    add sp, 2                         ; discard the saved SI - keep advancing
    stc
    jmp .out
.fail:
    pop si                            ; restore SI - this was not a cellref
    clc
.out:
    pop di
    pop cx
    ret

; sh_prange - one comma-separated function argument: a range, or a single
; expression (which may itself start with, but not be, a cell reference -
; "A1" alone IS the range-shorthand for a single cell; "A1+5" is not a
; range at all). Folds into sh_pacc/sh_pcnt/sh_phave per sh_pfid.
sh_prange:
    push ax
    push bx
    call sh_pcellref
    jnc .plainexpr
    cmp byte [si], ':'
    jne .singlecell
    inc si
    mov [sh_r1col], ax
    mov [sh_r1row], bx
    call sh_pcellref
    jnc .out                          ; malformed range; contributes nothing
    mov [sh_r2col], ax
    mov [sh_r2row], bx
    call sh_foldrange
    jmp .out
.singlecell:
    call sh_getcell2                  ; the value lands in sh_acc either way -
.havev:                               ; getcell2 puts a zero there for a cell
    call sh_ppowcont                  ; that does not exist. '^' binds tighter
    call sh_ptermcont                 ; than the levels below, so its
    call sh_pexprcont                 ; continuation must run first
    call sh_pcmpcont
    call sh_foldvalue
    jmp .out
.plainexpr:
    call sh_pcmp
    call sh_foldvalue
.out:
    pop bx
    pop ax
    ret

; sh_foldrange - in: sh_r1col/row, sh_r2col/row (either corner order);
; folds every OCCUPIED cell in the rectangle via sh_foldvalue.
;
; It walks the RECORD ARRAY, not the rectangle (81.3): records are sorted by
; (packed row, col) - the stage 2.0 comment above sh_findcell - so ONE binary
; search finds the first corner and a forward scan visits exactly the records
; in the row span. Walking every coordinate was O(area x log n): the ordinary
; =SUM(A1:A16384) idiom was 16,384 searches inside the paint callback, seconds
; per repaint on the 8088 and invisible in an emulator (PERFORMANCE.md rule 6).
; The bound rides in SI and the cursor in DI because sh_getcell2 preserves
; both across the evaluation a formula cell runs; the end offset is a
; function of [sh_ncells] alone, the same for a nested fold, so sh_rrow can
; hold it.
sh_foldrange:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [sh_r1col]
    mov bx, [sh_r2col]
    cmp ax, bx
    jle .colok
    xchg ax, bx
.colok:
    mov [sh_r1col], ax
    mov [sh_r2col], bx
    mov ax, [sh_r1row]
    mov bx, [sh_r2row]
    cmp ax, bx
    jle .rowok
    xchg ax, bx
.rowok:
    mov [sh_r1row], ax
    mov [sh_r2row], bx

    mov ax, [sh_ncells]
    mov bx, SH_C_SZ
    mul bx
    mov [sh_rrow], ax                  ; end-of-array offset
    mov ax, [sh_cursheet]              ; the far corner, PACKED the way the
    mov cl, SH_ROW_BITS                ; records store a row (sh_findcell)
    shl ax, cl
    or ax, [sh_r2row]
    mov si, ax                         ; SI = the packed bound
    mov ax, [sh_r1col]
    mov bx, [sh_r1row]
    call sh_findcell                   ; found or not, DI = the first record
                                        ; at or after the near corner
.scan:
    cmp di, [sh_rrow]
    jae .done                          ; past the last record
    mov es, [sh_cellseg]               ; reloaded every pass: an evaluation
    mov ax, [es:di]                    ; below moves ES. AX = packed row
    cmp ax, si
    jg .done                           ; sorted (signed, as sh_findcell
    mov bx, [es:di+2]                  ; compares): past the last row is done
    cmp bx, [sh_r1col]
    jb .next
    cmp bx, [sh_r2col]
    ja .next
    and ax, SH_ROW_MASK                ; a hit: unpack the row and fold it
    xchg ax, bx                        ; AX = col, BX = row
    call sh_getcell2                   ; tag, format and sh_acc, exactly as
    jnc .next                          ; an operand's read loads them
    call sh_foldvalue                  ; sh_acc is the value; see sh_foldvalue
.next:
    add di, SH_C_SZ
    jmp .scan
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_foldvalue - fold the value in sh_acc into the running sh_pacc, per the
; function being parsed (sh_pfid).
;
; sh_pacc is EIGHT BYTES now, not a word: SUM over a column of decimals has to
; keep them. The incoming value arrives in sh_acc rather than in AX for the
; same reason, and both are packed doubles - fp A and B are scratch here and
; are reloaded on every fold, because the range walker between calls uses them
; itself.
; -----------------------------------------------------------------------------
sh_foldvalue:
    push ax
    push bx
    cmp byte [sh_curtype], SH_T_TEXT   ; stage 4.5: a LABEL is not a number and
    jne .counted                       ; every numeric fold steps over it -
    cmp word [sh_pfid], 11             ; SUM, MIN, MAX and PRODUCT because a
    jne .out                           ; label has no value, and AVERAGE for a
    inc word [sh_pcnt]                 ; second reason on top of that: it
    jmp .out                           ; divides by sh_pcnt, so counting a
                                       ; label would drag the mean toward zero
                                       ; without ever adding to the total.
                                       ; COUNTA (11) is the one that WANTS it,
                                       ; and this is the first release in
                                       ; which COUNT and COUNTA can disagree
                                       ; about anything at all.
.counted:
    inc word [sh_pcnt]
    mov bx, [sh_pfid]
    cmp bx, 0
    je .sum
    cmp bx, 1
    je .sum                           ; AVERAGE sums here; sh_funcfinish
                                       ; divides once the count is final
    cmp bx, 2
    je .min
    cmp bx, 3
    je .max
    cmp bx, 8
    je .and
    cmp bx, 9
    je .or
    cmp bx, 10
    je .product
    jmp .out                          ; COUNT (4), COUNTA (11) or unknown:
                                       ; pcnt alone is enough
.sum:
    call sh_pacc_to_a
    call sh_acc_load_b
    call fp_add
    call sh_pacc_from_a
    jmp .out
.product:
    call sh_pacc_to_a
    call sh_acc_load_b
    call fp_mul
    call sh_pacc_from_a
    jmp .out
.min:
    cmp word [sh_phave], 0
    jnz .mincmp
    call sh_acc_to_pacc
    mov word [sh_phave], 1
    jmp .out
.mincmp:
    call sh_acc_load_a                ; is the new value below the running one?
    call sh_pacc_to_b
    call fp_cmpab
    jge .out
    call sh_acc_to_pacc
    jmp .out
.max:
    cmp word [sh_phave], 0
    jnz .maxcmp
    call sh_acc_to_pacc
    mov word [sh_phave], 1
    jmp .out
.maxcmp:
    call sh_acc_load_a
    call sh_pacc_to_b
    call fp_cmpab
    jle .out
    call sh_acc_to_pacc
    jmp .out
.and:
    call sh_acc_iszero
    jnc .out                          ; nonzero folds in as true: no-op
    xor ax, ax                        ; any false value forces AND to false
    call sh_int_to_pacc
    jmp .out
.or:
    call sh_acc_iszero
    jc .out                           ; zero folds in as false: no-op
    mov ax, 1                         ; any true value forces OR to true
    call sh_int_to_pacc
.out:
    pop bx
    pop ax
    ret

; --- the small movers the fold above is written in terms of ------------------
sh_pacc_to_a:
    push si
    mov si, sh_pacc
    call fp_unpack_a
    pop si
    ret

sh_pacc_to_b:
    push si
    mov si, sh_pacc
    call fp_unpack_b
    pop si
    ret

sh_pacc_from_a:
    push di
    mov di, sh_pacc
    call fp_pack_a
    pop di
    ret

sh_acc_to_pacc:
    push ax
    push si
    push di
    mov si, sh_acc
    mov di, sh_pacc
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop di
    pop si
    pop ax
    ret

; sh_int_to_pacc - AX (signed) -> sh_pacc
sh_int_to_pacc:
    call fp_i2a
    call sh_pacc_from_a
    ret

; sh_esatof - parse a decimal number from ES:SI into sh_acc, advancing SI past
; it. fp_atof reads DS:SI and the file staging buffer is in ES, so the token is
; copied across first - up to a ';' or the record's end. Without this, a SYLK
; K field would still be read by the integer parser and "3.5" would come back
; as 3, which is what the round trip actually did before this existed.
sh_esatof:
    push ax
    push cx
    push di
    mov di, sh_numbuf
    mov cx, 24
.copy:
    jcxz .done
    cmp si, bx
    jae .done
    mov al, [es:si]
    cmp al, ';'
    je .done
    cmp al, 13
    je .done
    cmp al, 10
    je .done
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .copy
.done:
    mov byte [di], 0
    push si
    mov si, sh_numbuf
    call fp_atof
    pop si
    call sh_acc_store
    pop di
    pop cx
    pop ax
    ret

; The same three, for a record addressed through SI - the file writers, the
; chart scan and sort all walk the array with SI rather than DI.
sh_cellval_to_acc_si:
    push ax
    push cx
    push si
    push di
    mov di, sh_acc
    mov cx, 4
.s2a:
    mov ax, [es:si+SH_C_VAL]
    mov [di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .s2a
    pop di
    pop si
    pop cx
    pop ax
    ret

; sh_cellnum_si - ...formatted into sh_numbuf
sh_cellnum_si:
    push ax
    push di
    call sh_cellval_to_acc_si
    call sh_acc_load_a
    mov di, sh_numbuf
    mov ax, 10
    call fp_ftoa
    pop di
    pop ax
    ret

; sh_cellint_si - ...truncated to a signed word in AX
sh_cellint_si:
    call sh_cellval_to_acc_si
    call sh_acc_toint
    ret

; sh_cellnum - the value of the record at ES:DI, formatted into sh_numbuf as
; a decimal. What "read the word and sh_itoa it" used to do, except that the
; value is eight bytes now and its low word on its own is meaningless.
sh_cellnum:
    push ax
    push di
    call sh_cellval_to_acc
    call sh_acc_load_a
    mov di, sh_numbuf
    mov ax, 10
    call fp_ftoa
    pop di
    pop ax
    ret

; sh_cellval_to_acc / sh_acc_to_cellval - the eight value bytes of the record
; at ES:DI. DI is left where it started, which matters: every caller is still
; using it as the record's offset.
sh_cellval_to_acc:
    push ax
    push cx
    push si
    push di
    mov si, sh_acc
    mov cx, 4
.c2a:
    mov ax, [es:di+SH_C_VAL]
    mov [si], ax
    add di, 2
    add si, 2
    dec cx
    jnz .c2a
    pop di
    pop si
    pop cx
    pop ax
    ret

sh_acc_to_cellval:
    push ax
    push cx
    push si
    push di
    mov si, sh_acc
    mov cx, 4
.a2c:
    mov ax, [si]
    mov [es:di+SH_C_VAL], ax
    add di, 2
    add si, 2
    dec cx
    jnz .a2c
    pop di
    pop si
    pop cx
    pop ax
    ret

; sh_acc_iszero - CF=1 if sh_acc is zero. The exponent and mantissa are all
; that matter; a negative zero is still zero, so the sign byte is masked off.
sh_acc_iszero:
    push ax
    push bx
    mov ax, [sh_acc]
    or ax, [sh_acc+2]
    or ax, [sh_acc+4]
    mov bx, [sh_acc+6]
    and bx, 0x7FFF
    or ax, bx
    pop bx
    pop ax
    jnz .no
    stc
    ret
.no:
    clc
    ret

; sh_funcfinish - the accumulated sh_pacc/sh_pcnt -> the function's result
sh_funcfinish:
    push bx
    mov bx, [sh_pfid]
    cmp bx, 1
    je .average
    cmp bx, 4
    je .count
    cmp bx, 11
    je .count                         ; COUNTA answers from the COUNT of things
                                       ; folded, not from the accumulator - it
                                       ; is identical to COUNT while every
                                       ; value in this model is a number, and
                                       ; separating them is Stage 4.0's job,
                                       ; when text and blanks become tellable
                                       ; apart. Without this it returned
                                       ; sh_pacc, which for a non-summing fold
                                       ; is always 0.
    call sh_pacc_to_a                 ; SUM/MIN/MAX/PRODUCT (and unknown):
    call sh_acc_store                 ; whatever was folded, zero if nothing
    jmp .fout
.average:
    cmp word [sh_pcnt], 0
    jne .avgok
    xor ax, ax
    call sh_acc_int
    jmp .fout
.avgok:
    call sh_pacc_to_a                 ; A REAL MEAN NOW, not a truncated one:
    mov ax, [sh_pcnt]                 ; AVERAGE(1,2) is 1.5 where the integer
    call fp_i2b                       ; evaluator gave 1
    call fp_div
    call sh_acc_store
    jmp .fout
.count:
    mov ax, [sh_pcnt]
    call sh_acc_int
    jmp .fout
.fout:
.out:
    pop bx
    ret

; sh_pfunc - in: SI right after a function NAME (sh_ident holds it),
; expecting '(' next; out: AX=value, SI advanced past the closing ')'.
; Saves/restores sh_pfid/sh_pacc/sh_pcnt/sh_phave around itself, so a
; function call nested inside another's argument list (SUM(A1,MAX(B1:B9)))
; cannot corrupt the outer accumulator.
sh_pfunc:
    push bx
    push cx
    push dx
    push word [sh_pfid]
    push word [sh_pacc+6]             ; ALL EIGHT bytes: sh_pacc is a packed
    push word [sh_pacc+4]             ; double (stage 4.0), and banking only
    push word [sh_pacc+2]             ; its low word handed the outer fold the
    push word [sh_pacc]               ; inner call's accumulator back
    push word [sh_pcnt]
    push word [sh_phave]
    xor dx, dx                        ; DX = result; 0 covers every bad exit
    call sh_pnest_enter               ; a nested call is a recursion point too
    jc .popout                        ; (81.3); too deep answers 0 + #VALUE!
    cmp byte [si], '('
    jne .noparen                      ; a bare word that resolved to no defined
    inc si                            ; name is #NAME?, exactly as in Excel -
    call sh_funcid                    ; sh_pident only routes one here once
    xor ah, ah                        ; sh_name_lookup has already declined it
    cmp ax, 0xFF                      ; ...and so is a CALL to a function this
    je .noname                        ; app does not have. Reading either as a
    cmp ax, 5
    je .doif
    cmp ax, 6
    je .donot
    cmp ax, 7
    je .doabs
    cmp ax, 12                         ; 12+ are stage 3.0d's special forms:
    jae .dospecial                     ; fixed arity, parsed by sh_pspecial,
                                       ; not folded over ranges
    mov [sh_pfid], ax
    push ax
    xor ax, ax
    cmp word [sh_pfid], 8              ; AND folds by ANDing in each value, so
    je .accone                         ; it must start true (1), not the false
    cmp word [sh_pfid], 10             ; (0) every other fold starts at.
    jne .accset                        ; PRODUCT starts at 1 for the same
.accone:                               ; reason - a running product seeded with
    mov ax, 1                          ; 0 can only ever be 0
.accset:
    call sh_int_to_pacc
    pop ax
    mov word [sh_pcnt], 0
    mov word [sh_phave], 0
.args:
    call sh_prange
    cmp byte [si], ','
    jne .argsdone
    inc si
    jmp .args
.argsdone:
    cmp byte [si], ')'
    jne .badtail
    inc si
    call sh_funcfinish
    mov dx, ax
    jmp .done
.badtail:
    mov byte [sh_evalerr], SH_ERR_VALUE ; an argument tail this grammar cannot
    jmp .done                          ; parse (=SUM(A1:A9^2)) must ERR, not
                                       ; answer with a partial fold (81.20)
.doif:
    call sh_pif
    mov dx, ax
    jmp .done
.donot:
    call sh_pnot
    mov dx, ax
    jmp .done
.doabs:
    call sh_pabs
    mov dx, ax
    jmp .done
.dospecial:
    call sh_pspecial
    mov dx, ax
    jmp .typed
.noname:                              ; zero is how a typo silently becomes an
    call sh_skipargs                  ; answer, and the whole point of an error
.noparen:                             ; value is that it cannot be mistaken for
    mov byte [sh_evalerr], SH_ERR_NAME  ; one. Only the CALL form has arguments
.typed:                               ; to step over - `=FOO+1` has none, and
                                       ; skipping there would eat the `+1`                               ; mistaken for one
    mov byte [sh_curtype], SH_T_NUM   ; a call's RESULT is a number whatever it
                                       ; folded over: without this the TEXT tag
                                       ; left by the last cell a range touched
                                       ; would make `=SUM(A1:A9)*2` a #VALUE!
.done:
    call sh_pnest_leave
.popout:
    pop word [sh_phave]
    pop word [sh_pcnt]
    pop word [sh_pacc]
    pop word [sh_pacc+2]
    pop word [sh_pacc+4]
    pop word [sh_pacc+6]
    pop word [sh_pfid]
    mov ax, dx
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; sh_pspecial (stage 3.0d) - the fixed-arity functions, ids 12 and up. These
; do not fold over a range the way SUM does; each parses exactly the arguments
; it takes and computes a value.
;
; WHAT IS DELIBERATELY ABSENT, and why. This list used to say "all of these
; need the value model Stage 4.0 brings", and that reason EXPIRED when Stage
; 4.0 landed - values are IEEE-754 doubles now and cells carry an SH_T_* tag,
; so SQRT really does return 1.41421 and the fraction-dependent families are
; no longer blocked on arithmetic. What still blocks these three is a
; different thing:
;   ISBLANK  an argument is FOLDED TO A VALUE before the function sees it, so
;   ISNUMBER by the time either of these is called there is no reference left
;            to ask about - an empty cell and a cell holding 0 both arrive as
;            0, and a label arrives as its numeric value. Answering them needs
;            reference-typed arguments in the evaluator, not a wider number.
;   ISNA/NA  SH_T_BOOL and SH_T_ERR are reserved, but nothing in the evaluator
;            ever produces one, so there is still no error value to return or
;            to test for.
; A version of any of them that returned a plausible constant would be worse
; than its absence.
;
; in: AX = the id, SI just past '('. out: AX = the value, SI past ')'.
; =============================================================================
; sh_parg - one argument, as an integer. The special forms below are integer
; functions by nature; a fractional MOD or FACT is not a thing they mean.
; Truncation is the same rule TRUNC itself uses, so INT(3.7) is 3.
sh_parg:
    call sh_pcmp
    call sh_acc_toint
    ret

sh_pspecial:
    push bx
    push cx
    push dx
    push di
    mov di, ax                        ; DI holds the id: every sh_pcmp below
                                      ; clobbers AX/BX/CX/DX
    cmp di, 13                        ; the four that are REAL functions of a
    je .dfloor                        ; real number now that cells hold one -
    cmp di, 14                        ; everything else here is a function of
    je .dtrunc                        ; whole numbers by nature and stays
    cmp di, 17                        ; integer (see .close)
    je .dsqrt
    cmp di, 19
    je .dround
    cmp di, 20
    jb .arg1                          ; 12..18 take one or two arguments
    cmp di, 23
    jbe .noargs                       ; 20..23 take none
    jmp .choose                       ; 24 CHOOSE takes a list

; ---- INT / TRUNC / SQRT / ROUND, on doubles ---------------------------------
; INT FLOORS and TRUNC cuts toward zero, which differ for negatives: Excel's
; INT(-3.7) is -4 and TRUNC(-3.7) is -3. While every value was an integer the
; two were indistinguishable and both were the identity; they are not any more.
.dfloor:
    call sh_pcmp
    call sh_acc_load_a
    call fp_floor
    jmp .dstore
.dtrunc:
    call sh_pcmp
    call sh_acc_load_a
    call fp_trunc
    jmp .dstore
.dsqrt:
    call sh_pcmp
    test byte [sh_acc+7], 0x80        ; the sign bit of the packed double: a
    jz .sqrtok                        ; negative has no real square root, and
    mov byte [sh_evalerr], SH_ERR_NUM ; Excel says #NUM! rather than 0
.sqrtok:
    call sh_acc_load_a
    call fp_sqrt                      ; a REAL root: SQRT(2) is 1.414213562,
    jmp .dstore                       ; where the integer version gave 1
.dround:
    call sh_pcmp                      ; the value, banked across the second
    call sh_vpush                     ; argument's parse
    xor cx, cx
    cmp byte [si], ','
    jne .dround1                      ; ROUND(x) with no count means 0 places
    inc si
    call sh_parg                      ; the digit count IS a whole number
    mov cx, ax
.dround1:
    call sh_binop_pre                 ; A = the value again
    call fp_round
.dstore:
    call sh_acc_store
    cmp byte [si], ')'
    jne .dout
    inc si
.dout:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; ---- TRUE() FALSE() ROW() COLUMN() ------------------------------------------
.noargs:
    xor ax, ax
    cmp di, 20                        ; TRUE
    jne .nf
    mov ax, 1
    jmp .close
.nf:
    cmp di, 21                        ; FALSE - AX is already 0
    je .close
    mov ax, [sh_evrow]                ; ROW / COLUMN answer for the cell being
    cmp di, 22                        ; EVALUATED, not the one selected - a
    je .ctx1                          ; formula's own position is what Excel
    mov ax, [sh_evcol]                ; means by these
.ctx1:
    inc ax                            ; 1-based, as displayed
    jmp .close

; ---- the one- and two-argument forms ----------------------------------------
.arg1:
    call sh_parg                      ; every id from here takes a first value
    mov bx, ax                        ; BX = first argument
    cmp di, 12
    je .two
    cmp di, 18
    je .two
    cmp di, 19
    je .two
    ; --- single argument: INT TRUNC SIGN FACT SQRT ---
    mov ax, bx
    cmp di, 13                        ; INT - truncation toward zero on a whole
    je .close                         ; number is the identity. Present for
    cmp di, 14                        ; formula compatibility, not effect; it
    je .close                         ; becomes real work in Stage 4.0. TRUNC
                                      ; likewise.
    cmp di, 15
    je .sign
    cmp di, 16
    je .fact
    call sh_isqrt                     ; 17 SQRT
    jmp .close
.sign:
    or ax, ax
    jz .close
    jns .signpos
    mov ax, -1
    jmp .close
.signpos:
    mov ax, 1
    jmp .close
.fact:
    or ax, ax
    js .factnum                       ; negative has no factorial here
    cmp ax, 7
    ja .factnum                       ; 8! = 40320 does not fit a signed word,
    mov cx, ax                        ; so refuse rather than hand back a
    mov ax, 1                         ; wrapped number that looks like an answer
    or cx, cx
    jz .close                         ; 0! = 1
.factloop:
    imul cx
    dec cx
    jnz .factloop
    jmp .close
.factnum:                             ; out of FACT's domain, or out of the
    mov byte [sh_evalerr], SH_ERR_NUM ; range a word can hold: #NUM! either
    jmp .zeroout                      ; way, which is what Excel reports

.two:
    cmp byte [si], ','
    jne .zeroout
    inc si
    push bx                           ; first argument, across the second parse
    call sh_parg
    mov cx, ax                        ; CX = second argument
    pop bx
    cmp di, 12
    je .mod
    cmp di, 18
    je .power
    ; --- 19 ROUND(x, digits) ---
    mov ax, bx
    or cx, cx
    jns .close                        ; digits >= 0 leaves a whole number
    neg cx                            ; alone; only rounding to tens and up
    cmp cx, 4                         ; can do anything here
    ja .zeroout                       ; 10^5 exceeds the value range entirely
    mov bx, 1
.p10:
    or cx, cx
    jz .havep10
    push ax
    mov ax, bx
    mov dx, 10
    imul dx
    mov bx, ax
    pop ax
    dec cx
    jmp .p10
.havep10:                             ; BX = the power of ten
    cwd
    idiv bx                           ; AX = quotient, DX = remainder
    push ax
    mov ax, dx
    or ax, ax                         ; |remainder| * 2 vs the divisor decides
    jns .roundabs                     ; the direction; away from zero on a tie,
    neg ax                            ; which is Excel's own rule
.roundabs:
    shl ax, 1
    cmp ax, bx
    pop ax
    jb .scaleback
    or dx, dx                         ; step away from zero, following the
    js .rounddown                     ; remainder's own sign
    inc ax
    jmp .scaleback
.rounddown:
    dec ax
.scaleback:
    imul bx
    jmp .close
.mod:
    mov ax, bx
    or cx, cx
    jz .zeroout                       ; MOD by zero -> 0, this evaluator's
    cwd                               ; standing divide-by-zero policy
    idiv cx
    mov ax, dx                        ; IDIV's remainder takes the DIVIDEND's
    or ax, ax                         ; sign; Excel's MOD takes the DIVISOR's,
    jz .close                         ; so a mismatch needs one correction
    mov bx, ax
    xor bx, cx
    jns .close                        ; signs already agree
    add ax, cx
    jmp .close
.power:
    mov ax, 1
    or cx, cx
    js .zeroout                       ; a negative exponent is a fraction
    jz .close                         ; anything^0 = 1, including 0^0 here
.powloop:
    imul bx
    dec cx
    jnz .powloop
    jmp .close

; ---- CHOOSE(index, v1, v2, ...) ---------------------------------------------
; Every argument is parsed whether or not it is the chosen one - stopping
; early would leave SI mid-expression with no way to find the closing paren -
; but only the CHOSEN one's raise of the sticky sh_evalerr may stand (81.20).
; Same reasoning as sh_pif's, and the bank rides in DI because sh_pcmp
; preserves it (see the comment at sh_pspecial's entry) where it clobbers
; AX/BX/CX/DX; the id DI held is not needed once .choose is reached.
.choose:
    call sh_parg                      ; the 1-based index
    mov bx, ax
    xor cx, cx                        ; CX = how many values seen
    xor dx, dx                        ; DX = the one that matched
.chloop:
    cmp byte [si], ','
    jne .chdone
    inc si
    mov al, [sh_evalerr]              ; banked across this value's parse...
    xor ah, ah
    mov di, ax
    call sh_parg
    inc cx
    cmp cx, bx
    jne .chskip
    mov dx, ax                        ; the chosen one - its raise stands
    jmp .chloop
.chskip:
    mov ax, di                        ; ...and restored: not the chosen one
    mov [sh_evalerr], al
    jmp .chloop
.chdone:
    mov ax, dx
    jmp .close

.zeroout:
    xor ax, ax
.close:
    call sh_acc_int                   ; these thirteen are integer functions by
                                      ; nature - MOD, FACT, ROW, CHOOSE - so
                                      ; they take integers and give one back,
                                      ; converting only at this boundary
    cmp byte [si], ')'
    jne .out
    inc si
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; sh_isqrt - in: AX = n; out: AX = floor(sqrt(n)), 0 for n < 0.
; Successive odd numbers: 1+3+5+... = k^2, so subtracting them until AX runs
; out counts the root. At most 181 iterations for a signed word, and it needs
; no division at all.
sh_isqrt:
    push bx
    push cx
    or ax, ax
    js .zero
    xor cx, cx
    mov bx, 1
.loop:
    cmp ax, bx
    jb .done
    sub ax, bx
    add bx, 2
    inc cx
    jmp .loop
.zero:
    xor cx, cx
.done:
    mov ax, cx
    pop cx
    pop bx
    ret

; sh_pif - IF(cond,then,else): the one function that does not fold - its
; branches are not even both evaluated the way a real spreadsheet expects
; only ONE side effect-free path to matter, but here both sides just get
; parsed unconditionally (the parse is what advances SI) and the condition
; alone picks which value survives. Parsing IS evaluation here, and it has
; one side effect since errors landed: a raise of the sticky sh_evalerr - so
; the raise of the branch the condition did NOT pick is banked and unraised
; (81.20), or =IF(B1=0,0,A1/B1) answered #DIV/0! for the case it guards.
; in: SI right after "IF("; out: AX=result, SI advanced past ')' if found
sh_pif:
    push bx
    push cx
    call sh_pcmp                      ; the condition, kept as a truth value
    call sh_acc_iszero                ; rather than as a number
    mov bx, 0
    jc .condfalse
    mov bx, 1
.condfalse:
    cmp byte [si], ','
    jne .bad
    inc si
    mov al, [sh_evalerr]              ; banked across the then-parse, and
    push ax                           ; restored if then was NOT chosen
    call sh_pcmp                      ; the then-value, banked whole
    pop ax
    or bx, bx
    jnz .thenkept
    mov [sh_evalerr], al
.thenkept:
    call sh_vpush
    cmp byte [si], ','
    jne .badpop
    inc si
    mov al, [sh_evalerr]              ; ...and the same for the else-parse
    push ax
    call sh_pcmp                      ; the else-value, left in sh_acc
    pop ax
    or bx, bx
    jz .elsekept
    mov [sh_evalerr], al
.elsekept:
    or bx, bx
    jz .dropthen                      ; false: sh_acc already holds the else
    call sh_binop_pre                 ; true: recover the then-value from the
    call sh_acc_store                 ; stack (it lands in fp A) and keep it
    jmp .out
.dropthen:
    add sp, 8                         ; the banked then-value is not wanted
    jmp .out
.badpop:
    add sp, 8
.bad:
    xor ax, ax
    call sh_acc_int
.out:
    cmp byte [si], ')'
    jne .noclose
    inc si
.noclose:
    pop cx
    pop bx
    ret

; sh_pnot - NOT(x): logical negation
; in: SI right after "NOT("; out: AX=1 or 0, SI advanced past ')' if found
sh_pnot:
    call sh_pcmp
    call sh_acc_iszero
    jc .true
    xor ax, ax
    call sh_acc_int
    jmp .close
.true:
    mov ax, 1
    call sh_acc_int
.close:
    cmp byte [si], ')'
    jne .out
    inc si
.out:
    ret

; sh_pabs - ABS(x): absolute value
; in: SI right after "ABS("; out: AX=|x|, SI advanced past ')' if found
sh_pabs:
    call sh_pcmp
    and byte [sh_acc+7], 0x7F         ; clear the sign bit: |x| for a packed
.close:                               ; double is one AND, and it is exact
    cmp byte [si], ')'
    jne .out
    inc si
.out:
    ret

; =============================================================================
; Macro engine (stage 2.0). A macro is an ordinary column of formula cells,
; on whatever sheet is active when Macro > Run is chosen, starting at the
; currently selected cell - real Excel lets you type or pick a starting
; reference in a Run dialog, but this OS has no generic text-prompt
; primitive (only a FILE picker, OSAPI_FILE_DLG) and building one is its
; own project, so "select the cell, then Run" is this stage's honest
; substitute. Execution proceeds down the column exactly like real Excel's
; own macro sheets, one cell at a time, until RETURN(), an empty cell, or
; the step cap below.
;
; Five function names are recognized as ACTIONS, not value-returning
; formulas, when they appear as the WHOLE of a macro cell's formula (never
; nested inside a larger expression - each cell is one instruction, again
; matching real Excel's macro-sheet model):
;   RETURN()              stop the macro
;   GOTO(ref)             jump execution to another cell on the SAME sheet
;   SET.VALUE(ref, expr)  write expr's value into another cell
;   SELECT(ref)           move the selection (and repaint, so it's visible)
;   ALERT("text")         show a real message box (SPEC.md 75.3's
;                         os88ui_ask, %included at the end of this file) and
;                         PAUSE until it's dismissed - os88ui_ask answers
;                         through a callback, not a return value, so the
;                         macro's "next step" is recorded before raising it
;                         and execution resumes from sh_macro_onalert
; A cell whose formula is anything else is evaluated normally (the full
; expression grammar, unchanged) and its value is discarded - a true no-op
; step, exactly as it would be on a real Excel macro sheet.
;
; A macro command's cell-reference arguments (GOTO/SET.VALUE/SELECT) are a
; bare "A1"-style reference only - no cross-sheet "SheetN!" prefix, unlike
; ordinary formulas (see sh_psheetpfx above). Keeping macro execution
; entirely on one sheet avoids the added complexity of switching sh_cursheet
; (and everything that implies for mid-macro repaints) partway through a
; run.
;
; SH_MACRO_MAXSTEPS exists because this is a COOPERATIVE system (SPEC.md's
; own repeated point) - sh_macro_step's GOTO loop below runs flat out with
; nothing to yield to, so a macro that GOTOs in a cycle would otherwise
; freeze the whole UI task forever, not just this window. Hitting the cap
; stops the macro and reports it, the same honest-failure posture as every
; other bound in this file, rather than let the machine hang.
; =============================================================================
SH_MACRO_MAXSTEPS equ 5000

sh_macro_kw_goto:     db 'GOTO', 0
sh_macro_kw_return:   db 'RETURN', 0
sh_macro_kw_setvalue: db 'SET.VALUE', 0
sh_macro_kw_select:   db 'SELECT', 0
sh_macro_kw_alert:    db 'ALERT', 0
sh_s_macrolimit: db 'Err: macro step limit', 0
sh_s_macrodone:  db 'Macro done', 0

; -----------------------------------------------------------------------------
; sh_pmacroref - a bare "A1"-style cell reference (no function names, no
; sheet prefix - see the section header above)
; in: SI; out: CF=1, AX=col, BX=row, SI advanced past it; CF=0 malformed
; -----------------------------------------------------------------------------
sh_pmacroref:
    push cx
    push dx
    push di
    mov di, sh_ident
    xor cx, cx
.collect:
    mov al, [si]
    cmp al, 'A'
    jb .doneletters
    cmp al, 'Z'
    jbe .isletter
    cmp al, 'a'
    jb .doneletters
    cmp al, 'z'
    ja .doneletters
.isletter:
    cmp cx, 2                          ; SH_COLS=256 never needs a 3rd letter
    jae .doneletters
    and al, 0xDF
    mov [di], al
    inc di
    inc cx
    inc si
    jmp .collect
.doneletters:
    mov byte [di], 0
    or cx, cx
    jz .bad
    mov al, [si]
    cmp al, '0'
    jb .bad
    cmp al, '9'
    ja .bad
    call sh_identcol
    push ax                            ; col
    mov bx, si
    add bx, SH_EDITMAX + 1
    push es
    mov ax, ds
    mov es, ax
    call sh_pint
    pop es
    dec ax                             ; row
    mov bx, ax
    pop ax                             ; col
    cmp ax, SH_COLS
    jae .bad
    cmp bx, SH_ROWS
    jae .bad
    stc
    jmp .out
.bad:
    clc
.out:
    pop di
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; sh_macro_kwtest - in: SI=text, CX=ptr to a NUL-terminated uppercase
; keyword; out: CF=1 and SI advanced past the keyword AND a following '(' -
; the '(' is required, so "GOTOX(" or "GOTO" alone don't match; CF=0
; otherwise (SI may be left partway advanced - callers always reset it)
; -----------------------------------------------------------------------------
sh_macro_kwtest:
    push ax
    push bx
    push di
    mov di, cx
.cmp:
    mov al, [di]
    or al, al
    jz .kwend
    mov bl, [si]
    cmp bl, 'a'
    jb .noupper
    cmp bl, 'z'
    ja .noupper
    and bl, 0xDF                       ; only a-z gets case-folded - a
                                        ; keyword like SET.VALUE has a '.'
                                        ; that this mask would corrupt
.noupper:
    cmp al, bl
    jne .no
    inc si
    inc di
    jmp .cmp
.kwend:
    cmp byte [si], '('
    jne .no
    inc si
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
; sh_macro_ident - in: SI; out: AX = 0 GOTO / 1 RETURN / 2 SET.VALUE /
; 3 SELECT / 4 ALERT / 0xFF none of these (SI restored); on a match SI is
; advanced past the keyword and its opening '('
; -----------------------------------------------------------------------------
sh_macro_ident:
    push bx
    push cx
    mov bx, si
    mov cx, sh_macro_kw_goto
    call sh_macro_kwtest
    jc .m0
    mov si, bx
    mov cx, sh_macro_kw_return
    call sh_macro_kwtest
    jc .m1
    mov si, bx
    mov cx, sh_macro_kw_setvalue
    call sh_macro_kwtest
    jc .m2
    mov si, bx
    mov cx, sh_macro_kw_select
    call sh_macro_kwtest
    jc .m3
    mov si, bx
    mov cx, sh_macro_kw_alert
    call sh_macro_kwtest
    jc .m4
    mov si, bx
    mov ax, 0xFF
    jmp .out
.m0:
    mov ax, 0
    jmp .out
.m1:
    mov ax, 1
    jmp .out
.m2:
    mov ax, 2
    jmp .out
.m3:
    mov ax, 3
    jmp .out
.m4:
    mov ax, 4
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sh_macro_eval - execute ONE macro cell's formula as an instruction
; in: DI = the cell's record offset (caller has already checked HASFORMULA)
; out: AX = 0 advance / 1 goto (BX=col, CX=row) / 2 return / 3 alert raised
;      (the caller must stop stepping - sh_macro_onalert resumes it later)
; -----------------------------------------------------------------------------
sh_macro_eval:
    push si
    push dx
    push es
    mov es, [sh_cellseg]
    mov si, [es:di+SH_C_FOFF]                  ; formula text offset, in sh_txtseg
    mov es, [sh_txtseg]
    mov di, sh_macrobuf
.copyin:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copyin
    pop es
    mov si, sh_macrobuf
    call sh_macro_ident
    cmp ax, 0
    je .doGOTO
    cmp ax, 1
    je .doRETURN
    cmp ax, 2
    je .doSETVALUE
    cmp ax, 3
    je .doSELECT
    cmp ax, 4
    je .doALERT
    mov si, sh_macrobuf                ; not a macro keyword: a plain
    call sh_pcmp                       ; formula step, evaluated for its
    xor ax, ax                         ; (nonexistent) side effect only
    jmp .out
.doGOTO:
    call sh_pmacroref
    jnc .noop
    mov cx, bx                         ; cx = row
    mov bx, ax                         ; bx = col
    mov ax, 1
    jmp .out
.doRETURN:
    mov ax, 2
    jmp .out
.doSETVALUE:
    call sh_pmacroref
    jnc .noop
    mov [sh_macro_tcol], ax
    mov [sh_macro_trow], bx
    cmp byte [si], ','
    jne .noop
    inc si
    call sh_pcmp                       ; the result lives in sh_acc, NOT in AX
    mov ax, [sh_macro_tcol]            ; (stage 4.0) - sh_setvald reads it from
    mov bx, [sh_macro_trow]            ; there, decimals intact, where sh_setval
    call sh_setvald                    ; with AX stored parser scratch
    xor ax, ax
    jmp .out
.doSELECT:
    call sh_pmacroref
    jnc .noop
    mov si, [sh_ownwin]                ; SI was the parse cursor, and
    call sh_select                     ; sh_select's contract needs the WINDOW:
    xor ax, ax                         ; it collapses the range, scrolls the
    jmp .out                           ; cell into view and repaints, exactly
                                       ; as a click does (.out restores SI)
.doALERT:
    cmp byte [si], '"'
    jne .noop
    inc si
    mov di, sh_macro_msg
.alertcopy:
    mov al, [si]
    or al, al
    jz .alertdone                      ; unterminated string: stop at NUL
    cmp al, '"'
    je .alertdone
    mov dx, di
    sub dx, sh_macro_msg
    cmp dx, OS88UI_AMAX
    jae .alertdone                     ; clip, matching os88ui_ask's own
    mov [di], al                       ; clip-not-refuse policy
    inc di
    inc si
    jmp .alertcopy
.alertdone:
    mov byte [di], 0
    mov ax, [sh_macro_row]             ; advance to the next step BEFORE
    inc ax                             ; raising the alert, so its callback
    mov [sh_macro_row], ax             ; can just re-enter sh_macro_step
    mov al, OS88UI_AOK
    mov bx, [sh_ownwin]
    mov si, sh_macro_msg
    mov di, sh_macro_onalert
    call os88ui_ask
    mov ax, 3
    jmp .out
.noop:
    xor ax, ax
.out:
    pop dx
    pop si
    ret

; -----------------------------------------------------------------------------
; sh_macro_step - run macro steps starting at [sh_macro_col]/[sh_macro_row]
; until RETURN, an empty cell, an ALERT (which returns here having already
; arranged its own resumption), or the step cap
; -----------------------------------------------------------------------------
sh_macro_step:
    push ax
    push bx
    push cx
    push di
    push es
.next:
    mov ax, [sh_macro_steps]
    cmp ax, SH_MACRO_MAXSTEPS
    jae .limit
    inc ax
    mov [sh_macro_steps], ax
    mov ax, [sh_macro_col]
    mov bx, [sh_macro_row]
    call sh_findcell
    jnc .stop                          ; an empty cell: implicit RETURN
    mov es, [sh_cellseg]
    test byte [es:di+4], 1             ; HASFORMULA - a plain value cell is
    jz .advance                        ; a no-op step, just like a bare
                                        ; formula with no side effect
    call sh_macro_eval
    cmp ax, 0
    je .advance
    cmp ax, 1
    je .goto
    cmp ax, 2
    je .stop
    jmp .out                           ; 3: alert raised, stop stepping -
                                        ; sh_macro_onalert resumes us later
.goto:
    mov [sh_macro_col], bx
    mov [sh_macro_row], cx
    jmp .next
.advance:
    mov ax, [sh_macro_row]
    inc ax
    mov [sh_macro_row], ax
    jmp .next
.limit:
    mov word [sh_msg], sh_s_macrolimit
    jmp .stopdraw
.stop:
    mov word [sh_msg], sh_s_macrodone
.stopdraw:
    mov byte [sh_macro_running], 0
    call sh_repaint
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_macro_onalert - os88ui_ask's completion proc (SPEC.md 75.3): AL=button
; or OS88UI_ACANCEL, SI=our window, gfx lock held, the alert already
; destroyed. Either way, resume - an OK and a Cancel mean the same thing
; here, since ALERT only ever offers the one OS88UI_AOK button.
; -----------------------------------------------------------------------------
sh_macro_onalert:
    call sh_macro_step
    ret

; -----------------------------------------------------------------------------
; sh_macro_run - Macro > Run: start executing at the currently selected
; cell, on the currently active sheet
; -----------------------------------------------------------------------------
sh_macro_run:
    cmp byte [sh_macro_running], 0
    jne .out                           ; already running (shouldn't happen -
                                        ; the menu command can't fire while
                                        ; an alert has this window's own
                                        ; event handling otherwise occupied,
                                        ; but a stray re-entry is a silent
                                        ; no-op rather than two interleaved
                                        ; macros stepping on each other)
    mov byte [sh_macro_running], 1
    mov word [sh_macro_steps], 0
    mov ax, [sh_selcol]
    mov [sh_macro_col], ax
    mov ax, [sh_selrow]
    mov [sh_macro_row], ax
    call sh_macro_step
.out:
    ret

; sh_funcid - in: sh_ident; out: AL = the function's id, or 0xFF unknown.
; TABLE-DRIVEN as of stage 3.0d: the id IS the entry's index in sh_functab, so
; adding a function is one string and one table word. It was an unrolled
; compare chain of five lines per function, which at ten functions was merely
; verbose and at twenty-five would have been a hundred lines of boilerplate
; with a hand-written id on each - exactly the shape that drifts.
sh_funcid:
    push bx
    push cx
    push si
    push di
    xor cx, cx
    mov bx, sh_functab
.loop:
    mov di, [bx]
    or di, di
    jz .unknown                       ; the table's 0 terminator
    mov si, sh_ident
    call sh_streq
    jc .found
    inc cx
    add bx, 2
    jmp .loop
.found:
    mov ax, cx
    jmp .out
.unknown:
    mov ax, 0xFF
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sh_chktext - the operand sh_curtype describes is TEXT, and something
; arithmetic is about to happen to it: raise #VALUE!. Excel's own answer, and
; the reason it is raised HERE rather than in sh_getcell2 is that a bare `=B4`
; must still SHOW the label, and SUM must still skip it - only an operator
; makes a label a mistake.
; -----------------------------------------------------------------------------
sh_chktext:
    cmp byte [sh_curtype], SH_T_TEXT
    jne .out
    mov byte [sh_evalerr], SH_ERR_VALUE
.out:
    ret

; -----------------------------------------------------------------------------
; sh_skipargs - SI is just past an unknown function's '('; leave it just past
; the matching ')'. Counting depth rather than scanning for the first ')' is
; what keeps `=FOO(SUM(A1:A2))` from leaving a stray parenthesis behind for
; the rest of the parse to trip over.
; -----------------------------------------------------------------------------
sh_skipargs:
    push cx
    mov cx, 1
.loop:
    mov al, [si]
    or al, al
    jz .out                           ; end of the formula: unbalanced, and
    inc si                            ; sh_paren_ok already refuses those at
    cmp al, '('                       ; entry - this is belt and braces
    jne .notopen
    inc cx
    jmp .loop
.notopen:
    cmp al, ')'
    jne .loop
    dec cx
    jnz .loop
.out:
    pop cx
    ret

; sh_streq - in: SI, DI (two NUL-terminated strings); out: CF=1 equal
sh_streq:
    push ax
    push si
    push di
.loop:
    mov al, [si]
    cmp al, [di]
    jne .neq
    or al, al
    jz .eq
    inc si
    inc di
    jmp .loop
.eq:
    stc
    jmp .out
.neq:
    clc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_clearcell - in: AX=col, BX=row
; -----------------------------------------------------------------------------
sh_clearcell:
    call sh_removecell
    ret

; =============================================================================
; String / number utilities
; =============================================================================

; sh_colname - bijective base-26 column letters (0-based index in AX)
; out: sh_colbuf = NUL-terminated letters (up to 2 for a 256-column grid)
sh_colname:
    push ax
    push bx
    push cx
    push dx
    inc ax
    xor cx, cx
.divloop:
    or ax, ax
    jz .popall
    dec ax
    xor dx, dx
    mov bx, 26
    div bx
    push dx
    inc cx
    jmp .divloop
.popall:
    mov bx, sh_colbuf
.popone:
    or cx, cx
    jz .term
    pop dx
    add dl, 'A'
    mov [bx], dl
    inc bx
    dec cx
    jmp .popone
.term:
    mov byte [bx], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_itoa - signed AX to a NUL-terminated decimal string in sh_numbuf
sh_itoa:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, sh_numbuf
    or ax, ax
    jns .pos
    mov byte [di], '-'
    inc di
    neg ax
.pos:
    xor cx, cx
    or ax, ax
    jnz .divloop
    mov byte [di], '0'
    inc di
    jmp .term
.divloop:
    or ax, ax
    jz .emit
    xor dx, dx
    mov bx, 10
    div bx
    push dx
    inc cx
    jmp .divloop
.emit:
    or cx, cx
    jz .term
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    dec cx
    jmp .emit
.term:
    mov byte [di], 0
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_mkblank - rebuild sh_blank (every empty cell's display text) as exactly
; [sh_cellch] spaces + NUL. Called once at startup and again whenever
; Format > Column Width... changes the preset - sh_blank can't be a fixed
; string once the cell width is a runtime value.
; -----------------------------------------------------------------------------
sh_mkblank:
    push ax
    push cx
    push di
    mov cx, [sh_cellch]
    mov di, sh_blank
.fill:
    jcxz .term
    mov byte [di], ' '
    inc di
    loop .fill
.term:
    mov byte [di], 0
    pop di
    pop cx
    pop ax
    ret

; sh_rjust - right-justify sh_numbuf into a fixed SH_CELL_CH-wide sh_tbuf
sh_rjust:
    push ax
    push cx
    push si
    push di
    mov si, sh_numbuf
    xor cx, cx
.len:
    cmp byte [si], 0
    je .havelen
    inc si
    inc cx
    jmp .len
.havelen:
    mov di, sh_tbuf
    mov ax, [sh_cellch]
    sub ax, cx
    jbe .nopad
    push cx
    mov cx, ax
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
    pop cx
.nopad:
    mov si, sh_numbuf
.copy:
    jcxz .term
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .copy
.term:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_ljust - left-justify sh_numbuf into a fixed SH_CELL_CH-wide sh_tbuf
; (padding trails, mirroring sh_rjust which pads first)
; -----------------------------------------------------------------------------
sh_ljust:
    push ax
    push cx
    push si
    push di
    mov si, sh_numbuf
    xor cx, cx
.len:
    cmp byte [si], 0
    je .havelen
    inc si
    inc cx
    jmp .len
.havelen:
    mov [sh_jlen], cx
    mov di, sh_tbuf
    mov si, sh_numbuf
.copy:
    jcxz .copydone
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .copy
.copydone:
    mov ax, [sh_cellch]
    sub ax, [sh_jlen]
    jbe .term
    mov cx, ax
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.term:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_cjust - center-justify sh_numbuf into a fixed SH_CELL_CH-wide sh_tbuf
; (the odd leftover space, if any, goes on the right)
; -----------------------------------------------------------------------------
sh_cjust:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, sh_numbuf
    xor cx, cx
.len:
    cmp byte [si], 0
    je .havelen
    inc si
    inc cx
    jmp .len
.havelen:
    mov [sh_jlen], cx
    mov di, sh_tbuf
    mov ax, [sh_cellch]
    sub ax, cx
    jle .nopad
    mov bx, ax                        ; bx = total pad
    shr ax, 1                         ; ax = left pad (floor)
    mov cx, ax
    jcxz .lpdone
.lp:
    mov byte [di], ' '
    inc di
    loop .lp
.lpdone:
    sub bx, ax                        ; bx = right pad = total - left
    mov si, sh_numbuf
    mov cx, [sh_jlen]
.cp:
    jcxz .cpdone
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .cp
.cpdone:
    mov cx, bx
    jcxz .term
.rp:
    mov byte [di], ' '
    inc di
    loop .rp
    jmp .term
.nopad:
    mov si, sh_numbuf
    mov cx, [sh_jlen]
.cp2:
    jcxz .term
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .cp2
.term:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_justify - in: BL=format byte; dispatches to sh_ljust/sh_cjust/sh_rjust
; by the alignment bits (General and explicit Right both right-justify,
; since this app's cells are only ever numeric - matching how real Excel's
; own "General" alignment right-justifies a number)
; -----------------------------------------------------------------------------
sh_justify:
    push ax
    push cx
    mov al, bl
    and al, SH_FMT_ALIGN_MASK
    mov cl, SH_FMT_ALIGN_SHIFT
    shr al, cl
    cmp al, SH_FMT_ALIGN_LEFT
    je .left
    cmp al, SH_FMT_ALIGN_CENTER
    je .center
    jmp .right
.left:
    call sh_ljust
    jmp .out
.center:
    call sh_cjust
    jmp .out
.right:
    call sh_rjust
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_justify_t - sh_justify for a LABEL rather than a number.
;
; The one difference, and it is Excel's: General aligns a number RIGHT and a
; label LEFT. An EXPLICIT alignment means the same thing for both, so this
; only intercepts General and hands everything else straight over.
; in: BL = the format byte
; -----------------------------------------------------------------------------
sh_justify_t:
    push ax
    push cx
    mov al, bl
    and al, SH_FMT_ALIGN_MASK
    mov cl, SH_FMT_ALIGN_SHIFT
    shr al, cl
    cmp al, SH_FMT_ALIGN_GENERAL
    jne .explicit
    call sh_ljust
    jmp .out
.explicit:
    call sh_justify
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_text_to_numbuf - copy the label at sh_curtoff (in sh_txtseg) into
; sh_numbuf, clipped to what the cell can show, so that the justifiers - which
; all read sh_numbuf and write sh_tbuf - need to know nothing about text.
;
; Clipped rather than scrolled or spilled: real Excel lets a label OVERFLOW
; into the empty cells to its right, which needs the neighbours' occupancy
; before this cell is drawn and a draw order that respects it. That is a
; drawing-order change, not a storage one, and it is deliberately not in this
; step - a clipped label is honest about being clipped.
; -----------------------------------------------------------------------------
sh_text_to_numbuf:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [sh_txtseg]
    mov si, [sh_curtoff]
    mov di, sh_numbuf
    mov cx, [sh_cellch]
    cmp cx, SH_NUMBUF_MAX             ; sh_numbuf is a fixed buffer and the
    jbe .cap                          ; cell width is a RUNTIME value now
    mov cx, SH_NUMBUF_MAX             ; (stage 3.0c's Column Width), so the
.cap:                                 ; clip is against both
    jcxz .term
.copy:
    mov al, [es:si]
    or al, al
    jz .term
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .copy
.term:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_strlen - in: SI=NUL-terminated string; out: AX=length (SI preserved)
; -----------------------------------------------------------------------------
sh_strlen:
    push si
    xor ax, ax
.lp:
    cmp byte [si], 0
    je .done
    inc si
    inc ax
    jmp .lp
.done:
    pop si
    ret

; -----------------------------------------------------------------------------
; sh_curr_ins - insert '$' at the front of sh_numbuf, shifting the existing
; text (and its NUL) right by one byte
; -----------------------------------------------------------------------------
sh_curr_ins:
    push ax
    push si
    push di
    mov si, sh_numbuf
    xor ax, ax
.len:
    cmp byte [si], 0
    je .havelen
    inc si
    inc ax
    jmp .len
.havelen:                             ; si -> the NUL, ax = strlen (unused)
    mov di, si
    inc di
.shift:
    mov al, [si]
    mov [di], al
    cmp si, sh_numbuf
    je .done
    dec si
    dec di
    jmp .shift
.done:
    mov byte [sh_numbuf], '$'
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_comma_ins - insert a thousands separator into sh_numbuf's digit run
; (a 16-bit value never needs more than one - max 5 digits), skipping any
; leading sign
; -----------------------------------------------------------------------------
sh_comma_ins:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, sh_numbuf
    cmp byte [si], '-'
    jne .nosign
    inc si
.nosign:
    push si                           ; start of the digit run
    xor cx, cx
.dlen:
    cmp byte [si], 0
    je .havedlen
    inc si
    inc cx
    jmp .dlen
.havedlen:                            ; cx = digit count
    pop si                            ; si = start of digit run again
    cmp cx, 4
    jb .nocomma                       ; <=3 digits: no comma needed
    mov ax, cx
    sub ax, 3
    add ax, si
    mov di, ax                        ; di = insertion point (fixed)
    mov bx, si
.elen:
    cmp byte [bx], 0
    je .haveend
    inc bx
    jmp .elen
.haveend:                             ; bx -> the NUL
    mov si, bx
    inc bx                            ; bx = shift destination (one past)
.shift:
    mov al, [si]
    mov [bx], al
    cmp si, di
    je .placecomma
    dec si
    dec bx
    jmp .shift
.placecomma:
    mov byte [di], ','
.nocomma:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_pct_app - append '%' to sh_numbuf
; -----------------------------------------------------------------------------
sh_pct_app:
    push ax
    push si
    mov si, sh_numbuf
.f:
    cmp byte [si], 0
    je .got
    inc si
    jmp .f
.got:
    mov byte [si], '%'
    inc si
    mov byte [si], 0
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_errname - the current cell's error, by name, into sh_numbuf. Excel's own
; spellings, because they are what a person recognises and what every book
; about spreadsheets prints. An unknown code cannot arise from this app's own
; evaluator, but a file could carry one, so it reads as #ERR rather than
; running off the end of the table.
; -----------------------------------------------------------------------------
sh_errname:
    push ax
    push bx
    push si
    push di
    mov al, [sh_curaux]
    or al, al
    jz .unknown
    cmp al, 7
    ja .unknown
    xor ah, ah
    dec ax
    shl ax, 1
    mov bx, ax
    mov si, [sh_errtab + bx]
    jmp .copy
.unknown:
    mov si, sh_s_err_unk
.copy:
    mov di, sh_numbuf
    call sh_strcpy
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_errcode - in: SI = a NUL-terminated error name; out: AL = its Excel
; ERROR.TYPE number, or 0 if this is not a spelling we write. The inverse of
; sh_errname, and it reads the SAME table, so the two cannot drift apart.
; -----------------------------------------------------------------------------
sh_errcode:
    push bx
    push cx
    push si
    push di
    xor cx, cx
.loop:
    cmp cx, 7
    jae .unknown
    mov bx, cx
    shl bx, 1
    mov di, [sh_errtab + bx]
    call sh_streq
    jc .found
    inc cx
    jmp .loop
.found:
    mov ax, cx
    inc ax
    jmp .out
.unknown:
    xor ax, ax
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

sh_errtab:  dw sh_s_err_null, sh_s_err_div0, sh_s_err_value, sh_s_err_ref
            dw sh_s_err_name, sh_s_err_num, sh_s_err_na
sh_s_err_null:  db '#NULL!', 0
sh_s_err_div0:  db '#DIV/0!', 0
sh_s_err_value: db '#VALUE!', 0
sh_s_err_ref:   db '#REF!', 0
sh_s_err_name:  db '#NAME?', 0
sh_s_err_num:   db '#NUM!', 0
sh_s_err_na:    db '#N/A', 0
sh_s_err_unk:   db '#ERR', 0

; -----------------------------------------------------------------------------
; sh_numfmt - in: AX=value, BL=format byte; writes the decorated display
; text into sh_numbuf (General is exactly sh_itoa's plain decimal; Currency/
; Comma/Percent decorate it further). BL survives sh_itoa (which preserves
; the whole of BX across its own body) so the format nibble is still there
; to dispatch on afterward.
; -----------------------------------------------------------------------------
; stage 4.0: the value being formatted is the DOUBLE in sh_acc, not the
; integer in AX. Its one caller is sh_drawgrid, immediately after
; sh_getcell2, which leaves sh_acc set - so the grid shows 3.5 as "3.5"
; rather than as the 3 an integer cell could hold. The currency, comma and
; percent decorations below are unchanged: they work on the digit string,
; whatever produced it.
;
; Ten significant digits, which is what fits a cell and what Excel shows in a
; General column before it starts rounding to fit.
sh_numfmt:
    push ax
    push bx
    push cx
    push di
    mov bh, bl
    mov di, sh_numbuf
    call sh_acc_load_a                ; fp_ftoa formats the A accumulator, so
    mov ax, 10                        ; sh_acc has to be put there first
    call fp_ftoa
    pop di
    mov bl, bh
    and bl, SH_FMT_NUM_MASK
    mov cl, SH_FMT_NUM_SHIFT
    shr bl, cl
    cmp bl, SH_FMT_NUM_CURRENCY
    je .currency
    cmp bl, SH_FMT_NUM_COMMA
    je .comma
    cmp bl, SH_FMT_NUM_PERCENT
    je .percent
    jmp .out
.currency:
    call sh_curr_ins
    jmp .out
.comma:
    call sh_comma_ins
    jmp .out
.percent:
    call sh_pct_app
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sh_drawunderline - in: CX=cell text x (left edge, as passed to
; OSAPI_FONT_RUN), DX=cell text y (top); reads sh_numbuf (the UNPADDED
; decorated text - not sh_tbuf, which carries alignment padding) and
; [sh_curfmt] to underline exactly the text's own extent, not the whole
; cell, positioned by the same alignment the text itself used.
; -----------------------------------------------------------------------------
sh_drawunderline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sh_ulx], cx
    mov [sh_uly], dx
    mov si, sh_numbuf
    call sh_strlen                    ; ax = text length (chars)
    mov bx, ax                        ; bx = length (chars)
    mov ax, [sh_cellch]
    sub ax, bx
    jns .padok
    xor ax, ax
.padok:                                ; ax = total pad chars
    mov dl, [sh_curfmt]
    and dl, SH_FMT_ALIGN_MASK
    mov cl, SH_FMT_ALIGN_SHIFT
    shr dl, cl                         ; dl = align code
    cmp dl, SH_FMT_ALIGN_LEFT
    je .lp0
    cmp dl, SH_FMT_ALIGN_CENTER
    je .lphalf
    mov di, ax                         ; General/Right: full pad on the left
    jmp .havelp
.lp0:
    xor di, di
    jmp .havelp
.lphalf:
    shr ax, 1
    mov di, ax
.havelp:                               ; di = left-pad chars
    shl di, 1
    shl di, 1
    shl di, 1                          ; di = left-pad pixels (*8)
    shl bx, 1
    shl bx, 1
    shl bx, 1                          ; bx = text width pixels (*8)
    mov ax, [sh_ulx]
    add ax, di                         ; ax = underline x1
    mov cx, ax
    add cx, bx
    dec cx                             ; cx = underline x2
    mov bx, [sh_uly]
    add bx, 9                          ; a couple px below the 8px glyph row
    mov dx, bx
    call OSAPI_GFX_FILL                ; AX=x1, BX=y1, CX=x2, DX=y2
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sh_strcpy - copy a NUL-terminated string, SI->DI, including the NUL
sh_strcpy:
    push ax
.loop:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .loop
    pop ax
    ret

; sh_strcpy_to_di - append a NUL-terminated string at the DI cursor,
; advancing DI to the new NUL (so successive calls concatenate)
sh_strcpy_to_di:
    push ax
.loop:
    mov al, [si]
    or al, al
    jz .term
    mov [di], al
    inc si
    inc di
    jmp .loop
.term:
    mov byte [di], 0
    pop ax
    ret

; =============================================================================
; Window template, menu, strings
; =============================================================================
sh_tpl:
    dw 60, 40, 560, 380
    dw sh_ttl, sh_paint, sh_onkey, sh_onclick

; The EMPTY kernel menu set (SPEC.md 12.2), same idea as apps/word/word.asm's
; wd_menus0: zero real menus, but its AM_NAME still puts 'Sheet' in the
; kernel bar. sh_mf_ret can never actually be called (there is nothing to
; pick) - it only satisfies the macro's layout.
    OS88_MENUSET sh_menus, sh_s_appname, sh_mf_ret
    OS88_MENUSET_END sh_menus
sh_mf_ret:
    ret

; sh_mtab - Sheet's own in-window menu bar (see the SH_MBAR_H section
; comment). Each entry: title string ptr, item string-ptr array, item
; count (word each, so 6 bytes/entry) - menu index 0..SH_MENU_N-1 is this
; array's own order, which sh_mfire's dispatch and sh_docmd_sortcol/
; sh_docmd_options/sh_docmd_help all key off directly.
; Stage 3.0b puts FORMULA at index 2, which is where Excel 2.1d has it -
; File Edit Formula Format Data Options Macro Window Help. Sheet's own
; multi-sheet menu stands in for Window and now sits where Window does, after
; Macro. Every index below 2 is unchanged and everything above it shifted, so
; sh_mfire's dispatch chain moved with it; nothing else in this file keys off a
; menu index (the macro language names commands, not menu positions), which is
; what made the renumber safe to do at all.
sh_mtab:
    dw sh_m_file,    sh_i_file,    5
    dw sh_m_edit,    sh_i_edit,    9
    dw sh_m_formula, sh_i_formula, 7
    dw sh_m_format,  sh_i_format,  6
    dw sh_m_data,    sh_i_data,    4
    dw sh_m_options, sh_i_options, 3
    dw sh_m_macro,   sh_i_macro,   1
    dw sh_m_sheet,   sh_i_sheet,   SH_SHEETS
    dw sh_m_help,    sh_i_help,    1

; Excel 2.1d's Formula menu, in its own order: Paste Name.../Paste Function.../
; Reference/Define Name.../Note.../Goto.../Find... - all seven now. The note
; this replaced said the rest would arrive with the features behind them rather
; than as items that open nothing, and that is what happened: the list dialog
; is what Paste Name and Paste Function were waiting on, the name table is what
; Define Name needed, and Reference had nowhere to show its answer until the
; reference box existed.
sh_m_formula:    db 'Formula', 0
sh_i_formula:    dw sh_it_pname, sh_it_pfunc, sh_it_ref_a1, sh_it_defname, sh_it_note, sh_it_goto, sh_it_find
sh_it_pname:     db 'Paste Name...', 0
sh_it_pfunc:     db 'Paste Function...', 0
sh_it_ref_a1:    db 'Reference: A1', 0     ; the same relabel-by-repointing
sh_it_ref_rc:    db 'Reference: R1C1', 0   ; the Options toggles use
sh_it_defname:   db 'Define Name...', 0
sh_it_note:      db 'Note...', 0
sh_it_goto:      db 'Goto...', 0
sh_it_find:      db 'Find...', 0

sh_ttl:        db 'Sheet', 0
sh_s_appname:  db 'Sheet', 0
sh_m_file:     db 'File', 0
sh_i_file:     dw sh_it_new, sh_it_open, sh_it_save, sh_it_saveas, sh_it_print
sh_it_new:     db 'New...', 0
sh_it_open:    db 'Open...', 0
sh_it_save:    db 'Save', 0
sh_it_saveas:  db 'Save As...', 0
; Print... is DELIBERATELY A STUB: there is no print backend anywhere in this
; OS, so the item exists for menu fidelity and says so in the status bar
; rather than opening a dialog that could not do anything. Exit is absent on
; purpose too - the OS menu owns it.
sh_it_print:   db 'Print...', 0
sh_s_noprint:  db 'Printing is not supported.', 0

; Stage 1.8/2.x: matches real Excel 2.0/2.1's own Format menu shape
; (VM_screenshots/menu_format.png) - Number.../Alignment.../Font... open
; dialogs (sh_docmd_format's AL 0/1/2 is sh_fdlg_open's own kind number, so
; this array's first 3 entries must stay in that order). Border... is real
; (sh_bdlg_*). Row Height.../Column Width... are real too, as a 3-preset
; radio pick (sh_fdlg_open kinds 6/5 - see sh_docmd_format's own remap
; comment for why those two aren't 4/5 straight through) rather than real
; Excel's free-text numeric entry, since this app has no text-input widget
; at the app level - applies to the WHOLE sheet's runtime sh_cellw/
; sh_cellh, not per-row/per-column (the real per-row/per-column version
; would need every fixed-grid assumption in the renderer/hit-tester turned
; into a lookup, which stayed out of scope here).
sh_m_format:    db 'Format', 0
sh_i_format:    dw sh_it_fnum, sh_it_falign, sh_it_ffont, sh_it_fborder, sh_it_frowh, sh_it_fcolw
sh_it_fnum:      db 'Number...', 0
sh_it_falign:    db 'Alignment...', 0
sh_it_ffont:     db 'Font...', 0
sh_it_fborder:   db 'Border...', 0
sh_it_frowh:     db 'Row Height...', 0
sh_it_fcolw:     db 'Column Width...', 0

; Stage 2.0: the Sheet menu switches which of this instance's SH_SHEETS
; grids is active (see the multi-sheet cell-record comment above
; sh_findcell for why this lives in one instance rather than several).
; Sheet names are the fixed strings below, not user-renameable in this
; stage - simpler, and a macro's "SheetN!" reference (see sh_pident) needs
; a name it can recognize regardless of what the user might have typed.
sh_m_sheet:    db 'Sheets', 0
sh_i_sheet:    dw sh_it_sheet1, sh_it_sheet2, sh_it_sheet3, sh_it_sheet4
sh_it_sheet1:  db 'Sheet1', 0
sh_it_sheet2:  db 'Sheet2', 0
sh_it_sheet3:  db 'Sheet3', 0
sh_it_sheet4:  db 'Sheet4', 0
; ...and the same four with the mark, which sh_sheetmark repoints between.
sh_it_sheet1c: db SH_MENU_CHK, 'Sheet1', 0
sh_it_sheet2c: db SH_MENU_CHK, 'Sheet2', 0
sh_it_sheet3c: db SH_MENU_CHK, 'Sheet3', 0
sh_it_sheet4c: db SH_MENU_CHK, 'Sheet4', 0
sh_sheet_plain: dw sh_it_sheet1, sh_it_sheet2, sh_it_sheet3, sh_it_sheet4
sh_sheet_chk:   dw sh_it_sheet1c, sh_it_sheet2c, sh_it_sheet3c, sh_it_sheet4c

; Stage 2.0: no generic text-prompt dialog exists in this OS (only a FILE
; picker), so "Run" starts a macro at whatever cell is CURRENTLY SELECTED,
; rather than asking for a typed/picked starting reference - see the
; Macro engine section comment for the full reasoning.
sh_m_macro:    db 'Macro', 0
sh_i_macro:    dw sh_it_run
sh_it_run:     db 'Run', 0

; Edit - "Can't Undo" is a real Excel item with no real implementation
; behind it (no undo system exists) - shown disabled (MENU_DIS) rather than
; omitted, same honesty as Format's Border/Row Height/Column Width
; placeholders. Sort Column now lives in its own Data menu (below) - it
; only had to share Edit's list while the bar was the kernel's own
; MENU_APPMAX=5 one; Sheet's own in-window bar (sh_mtab) has no such cap.
; SH_MENU_CHK is Sheet's own leading-byte convention beside the kernel's
; MENU_DIS: the item is drawn with a check in the left margin. It is 2 rather
; than 1 so the two can never be confused, and sh_mdrop_draw handles both.
sh_m_edit:     db 'Edit', 0
sh_i_edit:     dw sh_it_undo, sh_it_cut, sh_it_copy, sh_it_paste, sh_it_clear, sh_it_delete, sh_it_insert, sh_it_fillright, sh_it_filldown
sh_it_undo:    db MENU_DIS, "Can't Undo", 0
sh_it_cut:     db 'Cut', 0
sh_it_copy:    db 'Copy', 0
sh_it_paste:   db 'Paste', 0
sh_it_clear:   db 'Clear...', 0
sh_it_delete:  db 'Delete...', 0
sh_it_insert:  db 'Insert...', 0
sh_it_fillright: db 'Fill Right', 0
sh_it_filldown:  db 'Fill Down', 0

; Data - real Excel 2.1 keeps Sort here, not in Edit. Chart Column.../Export
; Chart as BMP... are stage 2.x's own addition (no real-Excel Data menu
; equivalent - Excel's own charting is a whole separate document type) -
; see sh_docmd_chart's header comment for the design.
sh_m_data:     db 'Data', 0
sh_i_data:     dw sh_it_sort, sh_it_chart, sh_it_gallery, sh_it_chartexp
sh_it_sort:    db 'Sort...', 0
sh_it_chart:   db 'Chart Column...', 0
sh_it_gallery: db 'Chart Gallery...', 0
sh_it_chartexp: db 'Export Chart as BMP...', 0

; Options - Display toggles (stage 2.x). Each item's own string SWAPS
; between an On/Off pair (same relabel-by-repointing idea MENU_DIS's own
; doc shows) rather than drawing a separate checkmark glyph.
sh_m_options:  db 'Options', 0
sh_i_options:  dw sh_it_grid_off, sh_it_form_off, sh_it_calc
sh_it_grid_on:  db 'Gridlines: On', 0
sh_it_grid_off: db 'Gridlines: Off', 0
sh_it_form_on:  db 'Formulas: On', 0
sh_it_form_off: db 'Formulas: Off', 0
sh_it_calc:     db 'Calculation...', 0

; Help
sh_m_help:     db 'Help', 0
sh_i_help:     dw sh_it_about
sh_it_about:   db 'About Sheet...', 0
sh_s_about:    db 'Sheet - a spreadsheet for os8088', 0

sh_defname:    db 'SHEET1.SLK', 0
sh_s_ready:    db 'Ready', 0
sh_s_badparen: db 'Unbalanced ( ) - kept as text.', 0
sh_s_num:      db 'NUM', 0
sh_s_calcind:  db 'CALCULATE', 0
sh_s_nw_sheet: db 'New worksheet.', 0
sh_s_nw_chart: db 'New sheet - use Data > Chart Column to chart it.', 0
sh_s_nw_macro: db 'New sheet - Macro > Run reads commands from cells.', 0
sh_s_calc_auto: db 'Calculation: Automatic', 0
sh_s_calc_man:  db 'Calculation: Manual - Calculate Now to recompute.', 0
sh_s_calc_now:  db 'Recalculated.', 0
sh_s_id:       db 'ID;PWXL;N;E', 13, 10, 0
sh_s_c:        db 'C;X', 0
sh_s_y:        db ';Y', 0
sh_s_e:        db ';E', 0                  ; the expression field (stage 4.x)
sh_s_k:        db ';K', 0                  ; also the "commas are set" flag
                                            ; on an F record (stage 1.6)
sh_s_sylk_fx:  db 'F;X', 0                 ; an F (formatting) record -
sh_s_sylk_ff:  db ';F', 0                  ; stage 1.6's real SYLK support
sh_s_crlf:     db 13, 10, 0
sh_s_end:      db 'E', 13, 10, 0
sh_m_saved:    db 'Saved', 0
sh_m_trunc:    db 'Saved - TRUNCATED; sheet too large for this format.', 0
sh_m_loaded:   db 'Loaded', 0
sh_f_sum:      db 'SUM', 0
sh_f_average:  db 'AVERAGE', 0
sh_f_min:      db 'MIN', 0
sh_f_max:      db 'MAX', 0
sh_f_count:    db 'COUNT', 0
sh_f_if:       db 'IF', 0
sh_f_not:      db 'NOT', 0
sh_f_abs:      db 'ABS', 0
sh_f_and:      db 'AND', 0
sh_f_or:       db 'OR', 0
; stage 3.0d. ORDER IS THE ID - sh_functab below indexes by position and
; sh_pfunc/sh_foldvalue/sh_pspecial switch on that number, so entries may be
; APPENDED but never reordered or removed.
sh_f_product:  db 'PRODUCT', 0
sh_f_counta:   db 'COUNTA', 0
sh_f_mod:      db 'MOD', 0
sh_f_int:      db 'INT', 0
sh_f_trunc:    db 'TRUNC', 0
sh_f_sign:     db 'SIGN', 0
sh_f_fact:     db 'FACT', 0
sh_f_sqrt:     db 'SQRT', 0
sh_f_power:    db 'POWER', 0
sh_f_round:    db 'ROUND', 0
sh_f_true:     db 'TRUE', 0
sh_f_false:    db 'FALSE', 0
sh_f_row:      db 'ROW', 0
sh_f_column:   db 'COLUMN', 0
sh_f_choose:   db 'CHOOSE', 0

; sh_functab - the id is the INDEX. 0 terminates.
sh_functab:
    dw sh_f_sum, sh_f_average, sh_f_min, sh_f_max, sh_f_count
    dw sh_f_if, sh_f_not, sh_f_abs, sh_f_and, sh_f_or
    dw sh_f_product, sh_f_counta, sh_f_mod, sh_f_int, sh_f_trunc
    dw sh_f_sign, sh_f_fact, sh_f_sqrt, sh_f_power, sh_f_round
    dw sh_f_true, sh_f_false, sh_f_row, sh_f_column, sh_f_choose
    dw 0
sh_s_errpfx:   db 'Err ', 0
sh_s_ext_sylk: db '.SLK', 0
sh_s_ext_dif:  db '.DIF', 0
sh_s_ext_biff: db '.BIF', 0
sh_s_biff_fontname: db 'Helv', 0     ; Excel's own historical default face
; our number-format code (General/Currency/Comma/Percent) -> the real BIFF
; built-in format id, per the OpenOffice BIFF reference: 0=General,
; 5="$"#,##0 (currency, 0dp), 3=#,##0 (comma, 0dp), 9=0% (percent, 0dp) -
; all four are exactly the 0-decimal-place forms, matching this app's
; values always being whole numbers
sh_biff_numfmt_tab: db 0x00, 0x05, 0x03, 0x09
sh_s_dif_hdr1: db 'TABLE', 13, 10, '0,1', 13, 10, '""', 13, 10, 'VECTORS', 13, 10, '0,', 0
sh_s_dif_hdr2: db 13, 10, '""', 13, 10, 'TUPLES', 13, 10, '0,', 0
sh_s_dif_hdr3: db 13, 10, '""', 13, 10, 'DATA', 13, 10, '0,0', 13, 10, '""', 13, 10, 0
sh_s_dif_bot:  db '-1,0', 13, 10, 'BOT', 13, 10, 0
sh_s_dif_zc:   db '0,', 0
sh_s_dif_1c:   db '1,0', 13, 10, 0        ; stage 4.5: a STRING data item
sh_s_dif_v:    db 'V', 13, 10, 0           ; the real DIF value-indicator
                                            ; for "this numeric data is
                                            ; valid" - NOT a comment string;
                                            ; a type-0 (numeric) data item
                                            ; has no third line at all
sh_s_dif_err0: db '0,0', 13, 10, 'ERROR', 13, 10, 0 ; the ERROR indicator, on
                                                    ; a numeric item, which is
                                                    ; DIF's whole vocabulary
                                                    ; for one
sh_s_dif_na0:  db '0,0', 13, 10, 'NA', 13, 10, 0  ; a numeric item (type 0)
                                            ; whose indicator is NA - NOT
                                            ; type 1 (that's DIF's STRING
                                            ; type, whose second line must
                                            ; be a quoted string, not a
                                            ; bare keyword)
sh_s_dif_eod:  db '-1,0', 13, 10, 'EOD', 13, 10, 0

; Stage 2.0's ALERT() needs a real message box; SPEC.md 75.3's os88ui_ask is
; the project's own answer to that (a kernel-resident version was tried and
; measured too costly for every app to pay for - see the section comment
; above sh_macro_kw_goto). Included here, above OS88_BSS, because the
; sh_macro_msg bss field below sizes itself from OS88UI_AMAX, which this
; needs to have already defined.
%define OS88UI_ALERT
%define OS88UI_SCROLL               ; stage 3.0a+: SPEC.md 13.10's shared
                                     ; scroll bar - OPT IN, and without it
                                     ; os88ui_sbar is simply not assembled
%define OS88UI_SBDRAG               ; ...and the thumb-drag half of it
                                     ; shared scroll bar (SPEC.md 13.10.5),
                                     ; which needs W_ONCLICK/W_ONDRAG/
                                     ; W_ONMOUSEUP - Sheet already has the
                                     ; first two for range selection
%include "os88ui.inc"

; stage 3.0b: the one-line text field, the shared control browser.asm and
; telnet.asm already use. It gives the formula bar's content box a real caret
; and mid-string editing, replacing the append-only in-cell editor this app
; had before. MUST come after os88ui.inc (it uses its UI_* macros) and before
; OS88_BSS, which is os88ui.inc's own placement rule for the same reason.
%include "os88line.inc"

; stage 3.0b: its multi-line sibling, new in this stage and written to the same
; conventions (caller owns the block, passed in SI; no storage of its own).
; First consumer: Formula > Note..., which is Excel 2.1's cell notes and the
; first place in this app where free text can be typed at all.
%include "os88text.inc"

; stage 2.x: Data > Chart Column.../Export Chart as BMP...'s shared
; rasterizer + BMP writer - see that file's own header comment for the
; CH_* constants and ch_* bss words it requires, both declared above
; stage 4.0: the software IEEE-754 double. Included before os88chart.inc for
; no reason other than tidiness - it depends on nothing but the caller's own
; scratch, declared in the bss chain below.
%include "os88fp.inc"

%include "os88chart.inc"

; =============================================================================
; bss (loader-zeroed, SPEC.md 21 step 5) - small now: the grid itself lives
; in claimed heap segments, not here.
; =============================================================================
    OS88_BSS 3137
    OS88_IMAGE_END

sh_selcol     equ os88_image_end + 0
sh_selrow     equ sh_selcol + 2
sh_scrollcol  equ sh_selrow + 2
sh_scrollrow  equ sh_scrollcol + 2
sh_editing    equ sh_scrollrow + 2
sh_editlen    equ sh_editing + 1
sh_editbuf    equ sh_editlen + 1            ; 64: SH_EDITMAX + NUL
sh_name       equ sh_editbuf + 64           ; 13: 8.3 name + NUL
sh_ox         equ sh_name + 13
sh_oy         equ sh_ox + 2
sh_cw         equ sh_oy + 2
sh_ch         equ sh_cw + 2
sh_vcols      equ sh_ch + 2
sh_vrows      equ sh_vcols + 2
sh_wcol       equ sh_vrows + 2
sh_wrow       equ sh_wcol + 2
sh_selx1      equ sh_wrow + 2
sh_selx2      equ sh_selx1 + 2
sh_sely1      equ sh_selx2 + 2
sh_sely2      equ sh_sely1 + 2
sh_lx1        equ sh_sely2 + 2
sh_lx2        equ sh_lx1 + 2
sh_ly1        equ sh_lx2 + 2
sh_ly2        equ sh_ly1 + 2
sh_trunc      equ sh_ly2 + 2
SH_TCOL       equ sh_trunc + 1
SH_TROW       equ SH_TCOL + 2
SH_TVAL       equ SH_TROW + 2               ; the integer form, still used by
                                             ; the DIF and BIFF readers
SH_TDVAL      equ SH_TVAL + 2               ; 8: SYLK's, as a real double
SH_THASE      equ SH_TDVAL + 8              ; byte: this record had a ;E field
SH_TEXPR      equ SH_THASE + 1              ; SH_EDITMAX+1: its text
SH_TISTXT     equ SH_TEXPR + SH_EDITMAX + 1 ; byte: the ;K field was QUOTED,
                                             ; so SH_TEXPR holds a label
SH_TISERR     equ SH_TISTXT + 1             ; byte: ...or was an ERROR NAME,
SH_THAVE      equ SH_TISERR + 1             ; and which one
SH_TALIGN     equ SH_THAVE + 1             ; sh_parsefrec's own scratch -
SH_TNUMFMT    equ SH_TALIGN + 1            ; an "F" record's parsed
SH_TCOMMA     equ SH_TNUMFMT + 1           ; alignment/number-format/;K
sh_stagelen   equ SH_TCOMMA + 1
sh_tbuf       equ sh_stagelen + 2           ; 96: formula bar text (a formula
                                             ; can run to SH_EDITMAX chars)
sh_colbuf     equ sh_tbuf + 96              ; 4: up to 2 letters + NUL
sh_numbuf     equ sh_colbuf + 4             ; SH_NUMBUF_MAX+1: what all three
                                             ; justifiers read. Ten bytes held
                                             ; the widest DECORATED number
                                             ; ("$-32768", stage 1.6) and that
                                             ; was its whole job until stage
                                             ; 4.5 put LABELS through the same
                                             ; three routines - a label is as
                                             ; wide as the column, and a
                                             ; column runs to SH_CW_MAXCH
sh_msg        equ sh_numbuf + SH_NUMBUF_MAX + 1  ; 2: pointer to a status string
sh_errbuf     equ sh_msg + 2                ; 8: "Err " + up to 2 digits + NUL
sh_cellseg    equ sh_errbuf + 8
sh_txtseg     equ sh_cellseg + 2
sh_stgseg     equ sh_txtseg + 2
sh_bordseg    equ sh_stgseg + 2            ; stage 2.x: the border table's
                                             ; own claim, see SH_CLAIM_BORD_KB
sh_nbord      equ sh_bordseg + 2            ; word: records in sh_bordseg
sh_ncells     equ sh_nbord + 2
sh_txtlen     equ sh_ncells + 2
sh_fcol       equ sh_txtlen + 2             ; sh_findcell's search key stash
sh_frow       equ sh_fcol + 2
sh_wrec_row   equ sh_frow + 2               ; sh_dowrite's per-record stash
sh_wrec_col   equ sh_wrec_row + 2
sh_wrec_val   equ sh_wrec_col + 2
sh_wrec_fmt   equ sh_wrec_val + 2           ; SYLK's and BIFF's writers'
                                             ; stash of the record's format
                                             ; byte (DIF carries no format
                                             ; at all, see sh_dowrite_dif)
sh_newoff     equ sh_wrec_fmt + 1           ; sh_setformula's new text offset
sh_curaux     equ sh_newoff + 2             ; sh_getcell2's error-code output
sh_evalerr    equ sh_curaux + 2             ; byte: the error this evaluation
                                             ; ran into, 0 = none. STICKY for
                                             ; the whole of one top-level
                                             ; evaluation, which is what makes
                                             ; propagation free: no operator
                                             ; has to test it
sh_evaldepth  equ sh_evalerr + 2             ; sh_eval_cell's recursion depth
sh_pnest      equ sh_evaldepth + 2           ; live parser recursion points -
                                             ; sh_pnest_enter's counter (81.3),
                                             ; balanced so it needs no reset
sh_fbuf       equ sh_pnest + 2              ; SH_EVAL_MAXDEPTH * 64: one
                                             ; formula-text copy per
                                             ; recursion level (see
                                             ; sh_eval_cell), copied out of
                                             ; sh_txtseg so the parser never
                                             ; needs a segment override
sh_ident      equ sh_fbuf + (SH_EVAL_MAXDEPTH * 64) ; SH_NAME_MAX+1: a collected name/column
sh_pxsheet    equ sh_ident + SH_NAME_MAX + 1              ; stage 2.0: a "SheetN!" prefix
                                             ; sh_pident just consumed,
                                             ; 0xFF = none (see sh_psheetpfx)
sh_pcol       equ sh_pxsheet + 1              ; sh_pident's cell-ref column
sh_pfid       equ sh_pcol + 2               ; the function currently parsing:
                                             ; 0 SUM 1 AVERAGE 2 MIN 3 MAX
                                             ; 4 COUNT 0xFF unknown
sh_pacc       equ sh_pfid + 2               ; 8: the running sum / min / max /
                                             ; product, a packed double since
                                             ; stage 4.0 - SUM over a column of
                                             ; decimals has to keep them
sh_pcnt       equ sh_pacc + 8               ; cells folded so far
sh_phave      equ sh_pcnt + 2               ; MIN/MAX has a candidate yet
sh_r1col      equ sh_phave + 2              ; a range's two corners...
sh_r1row      equ sh_r1col + 2
sh_r2col      equ sh_r1row + 2
sh_r2row      equ sh_r2col + 2
sh_rrow       equ sh_r2row + 2              ; ...and sh_foldrange's end-of-
sh_rcol       equ sh_rrow + 2               ; array bound (sh_rcol is spare
                                             ; since the record-array walk)
sh_pass       equ sh_rcol + 2               ; recalculation pass counter
sh_bbrow      equ sh_pass + 2               ; sh_difbbox's used bounding box
sh_bbcol      equ sh_bbrow + 2
sh_curfmt     equ sh_bbcol + 2              ; sh_getcell2's format-byte output
sh_curtype    equ sh_curfmt + 1             ; ...and its SH_T_* tag, and where
sh_curtoff    equ sh_curtype + 1            ; a TEXT cell's characters live
sh_jlen       equ sh_curtoff + 2             ; sh_cjust's stashed text length
sh_ulx        equ sh_jlen + 2               ; sh_drawunderline's stashed
sh_uly        equ sh_ulx + 2                ; cell text origin (x, y)
sh_wrec_xf    equ sh_uly + 2                ; sh_doread_biff's per-record
                                             ; xf index stash
sh_biff_nfont equ sh_wrec_xf + 2            ; sh_doread_biff's FONT/XF
sh_biff_nxf   equ sh_biff_nfont + 2         ; record counters (also each
                                             ; new record's own index)
sh_font_tab   equ sh_biff_nxf + 2           ; SH_BIFF_FONT_CAP bytes: each
                                             ; tracked font's bold/underline
                                             ; bits
sh_xf_fmt     equ sh_font_tab + SH_BIFF_FONT_CAP  ; SH_BIFF_XF_CAP bytes:
                                             ; each tracked XF's align|
                                             ; numfmt packed byte
sh_xf_font    equ sh_xf_fmt + SH_BIFF_XF_CAP      ; SH_BIFF_XF_CAP bytes:
                                             ; each tracked XF's font index

sh_cursheet   equ sh_xf_font + SH_BIFF_XF_CAP      ; the sheet sh_findcell
                                             ; packs into every search (see
                                             ; the stage 2.0 cell-record
                                             ; comment above sh_findcell)
sh_selsave    equ sh_cursheet + 2           ; SH_SHEETS words each: the
sh_rowsave    equ sh_selsave + (SH_SHEETS*2) ; other 3 sheets' own
sh_sclsave    equ sh_rowsave + (SH_SHEETS*2) ; selection/scroll, saved and
sh_scrsave    equ sh_sclsave + (SH_SHEETS*2) ; restored by sh_switchsheet

sh_ownwin     equ sh_scrsave + (SH_SHEETS*2) ; our own window ptr, stashed
                                             ; once in sh_entry for
                                             ; os88ui_ask's sake
sh_macro_col  equ sh_ownwin + 2             ; the macro engine's current
sh_macro_row  equ sh_macro_col + 2          ; execution position
sh_macro_running equ sh_macro_row + 2       ; byte: a run is in progress
sh_macro_steps equ sh_macro_running + 1     ; word: this run's step count,
                                             ; against SH_MACRO_MAXSTEPS
sh_macro_tcol equ sh_macro_steps + 2        ; SET.VALUE's target cell,
sh_macro_trow equ sh_macro_tcol + 2         ; stashed across its sh_pcmp
sh_macrobuf   equ sh_macro_trow + 2         ; SH_EDITMAX+1: a macro step's
                                             ; formula text, copied out of
                                             ; sh_txtseg the same way
                                             ; sh_eval_cell's sh_fbuf is
sh_macro_msg  equ sh_macrobuf + SH_EDITMAX + 1 ; OS88UI_AMAX+1: ALERT's
                                             ; string-literal argument

sh_fdlg_win    equ sh_macro_msg + OS88UI_AMAX + 1 ; stage 1.8's Format
                                             ; dialogs: 0 = none, the gate
sh_fdlg_kind   equ sh_fdlg_win + 2          ; byte: 0 Number/1 Align/2 Font
sh_fdlg_sel    equ sh_fdlg_kind + 1         ; word: the selected radio 0-3
sh_fdlg_ox     equ sh_fdlg_sel + 2          ; this paint's content origin,
sh_fdlg_oy     equ sh_fdlg_ox + 2           ; stashed across widget calls
sh_fdlg_itemsptr equ sh_fdlg_oy + 2         ; this dialog's 4-item label
                                             ; array, for the row loop
sh_fdlg_rowidx equ sh_fdlg_itemsptr + 2     ; the row loop's own index
sh_fdlg_rowy   equ sh_fdlg_rowidx + 2       ; ...and that row's y
sh_fdlg_rect   equ sh_fdlg_rowy + 2         ; 4 words: one button rect,
                                             ; reused for OK then Cancel
sh_fdlg_count  equ sh_fdlg_rect + 8         ; word: this kind's row count
                                             ; (4 for Number/Align/Font, 2
                                             ; for Insert/Delete's Row/
                                             ; Column pick) - see
                                             ; sh_fdlg_counts

; Edit menu (stage 2.x)
sh_clipbuf    equ sh_fdlg_count + 2         ; SH_EDITMAX+1: Copy/Cut build
                                             ; their clipboard text here;
                                             ; Paste goes straight into
                                             ; sh_editbuf instead (see
                                             ; sh_docmd_paste)
sh_rc_op      equ sh_clipbuf + SH_EDITMAX + 1 ; sh_rowcol_op's own working
sh_rc_idx     equ sh_rc_op + 1              ; state - see its header
sh_rc_stgcnt  equ sh_rc_idx + 2             ; comment for what each field
sh_rc_savedsheet equ sh_rc_stgcnt + 2       ; holds; kept here rather than
sh_rc_tsheet  equ sh_rc_savedsheet + 2      ; on the stack purely because
sh_rc_trow    equ sh_rc_tsheet + 2          ; there are enough of them
sh_rc_tcol    equ sh_rc_trow + 2            ; that stack-relative addressing
sh_rc_tflags  equ sh_rc_tcol + 2            ; would be more error-prone
sh_rc_tfmt    equ sh_rc_tflags + 1          ; than a few named bytes
sh_wrec_foff  equ sh_rc_tfmt + 1            ; word: the formula text offset of
                                             ; the cell being written, or FFFF
sh_wrec_dval  equ sh_wrec_foff + 2          ; 8: the SYLK writer's banked value
sh_wrec_type  equ sh_wrec_dval + 8          ; byte: SH_T_* of the cell being
sh_wrec_toff  equ sh_wrec_type + 1          ; written, where its label is, and
sh_wrec_len   equ sh_wrec_toff + 2          ; how long that label is
sh_wrec_hasf  equ sh_wrec_len + 2      ; byte: this cell is a formula
sh_wrec_aux   equ sh_wrec_hasf + 1      ; byte: and, if it is an ERROR, which
sh_wsheet     equ sh_wrec_aux + 1        ; which sheet a BIFF write is on
sh_wb_map     equ sh_wsheet + 2         ; --- the BIFF4 workbook writer ---
sh_wb_i       equ sh_wb_map + 2
sh_wb_lenat   equ sh_wb_i + 2           ; where a SHEETHDR's length goes...
sh_wb_subat   equ sh_wb_lenat + 2       ; ...and where its substream began
sh_wb_xf4     equ sh_wb_subat + 2       ; byte: emit BIFF4 XFs, not BIFF3
sh_wb_align   equ sh_wb_xf4 + 1         ; this XF's alignment code
sh_rd_sheet   equ sh_wb_align + 2       ; the BIFF reader's substream counter
sh_rd_home    equ sh_rd_sheet + 2       ; ...and the sheet the user was on
sh_rd_wb      equ sh_rd_home + 2        ; byte: this file is a BIFF4 workbook
sh_biff_end   equ sh_rd_wb + 1           ; the BIFF reader's banked file end
sh_rc_tval    equ sh_biff_end + 2           ; 8: a whole double, not a word
sh_rc_tfml    equ sh_rc_tval + 8
sh_rc_ttype   equ sh_rc_tfml + 2            ; byte: SH_C_TYPE in transit
sh_rc_taux    equ sh_rc_ttype + 1           ; byte: ...and SH_C_AUX

sh_sort_cnt   equ sh_rc_taux + 1            ; word: sh_docmd_sortcol's own
                                             ; staged-pair count
sh_sort_fcnt  equ sh_sort_cnt + 2           ; word: how many formula text
                                             ; slots are staged so far
sh_sort_row   equ sh_sort_fcnt + 2          ; word: the scan's own current
                                             ; row, stashed across the
                                             ; sh_getcell2 call below it
sh_sort_val   equ sh_sort_row + 2           ; 8: that same cell's value, a
                                             ; whole double since stage 4.5
sh_sort_fslot equ sh_sort_val + 8           ; word: which text slot a
                                             ; formula cell just staged into
sh_sort_keyval  equ sh_sort_fslot + 2       ; 8: the insertion sort's key...
sh_sort_cmpv    equ sh_sort_keyval + 8      ; 8: ...and what it is compared
                                             ; against, both in DS because
                                             ; fp_unpack_* read DS:SI and the
                                             ; array lives in sh_stgseg
sh_sort_keyorig equ sh_sort_cmpv + 8        ; word: the key's own origidx
sh_sort_desc    equ sh_sort_keyorig + 2     ; byte: 0 ascending, 1 descending
sh_calcmanual   equ sh_sort_desc + 1        ; byte: Options > Calculation
sh_mchk         equ sh_calcmanual + 1       ; byte: this dropdown row is the
                                             ; checked one
sh_a1style      equ sh_mchk + 1             ; byte: 0 = A1, 1 = R1C1 - what
                                             ; the reference box and Goto show
; --- stage 3.0c: the list dialog ---
sh_ldlg_win     equ sh_a1style + 1
sh_ldlg_kind    equ sh_ldlg_win + 2
sh_ldlg_sel     equ sh_ldlg_kind + 1
sh_ldlg_top     equ sh_ldlg_sel + 2          ; first visible row
sh_ldlg_count   equ sh_ldlg_top + 2
sh_ldlg_items   equ sh_ldlg_count + 2        ; -> the pointer array in use
sh_ldlg_ox      equ sh_ldlg_items + 2
sh_ldlg_oy      equ sh_ldlg_ox + 2
sh_ldlg_i       equ sh_ldlg_oy + 2           ; the paint loop's row counter
sh_ldlg_idx     equ sh_ldlg_i + 2            ; ...and the item it maps to
sh_ldlg_rowy    equ sh_ldlg_idx + 2
sh_ldlg_rect    equ sh_ldlg_rowy + 2         ; 8: os88ui_btn takes a POINTER
sh_ldsb         equ sh_ldlg_rect + 8         ; 14: os88ui_sbar's seven words
sh_ldlg_src     equ sh_ldsb + 14             ; -> the string being pasted
; --- stage 3.0c: defined names ---
sh_nnames       equ sh_ldlg_src + 2
sh_names        equ sh_nnames + 2            ; SH_NAME_CAP * SH_NAME_REC
sh_nameptr      equ sh_names + SH_NAME_CAP * SH_NAME_REC   ; SH_NAME_CAP words
sh_nm_buf       equ sh_nameptr + SH_NAME_CAP * 2           ; SH_NAME_MAX+1
sh_nm_col       equ sh_nm_buf + SH_NAME_MAX + 1
sh_nm_row       equ sh_nm_col + 2
sh_find_col     equ sh_nm_row + 2            ; the walk's current cell...
sh_find_row     equ sh_find_col + 2
sh_find_buf     equ sh_find_row + 2          ; SH_EDITMAX+1: ...as displayed
sh_sort_trow  equ sh_find_buf + SH_EDITMAX + 1   ; word: the write-back loop's
sh_sort_src   equ sh_sort_trow + 2          ; own (target row, source idx)

; Sheet's own in-window menu bar (stage 2.x, see the SH_MBAR_H section
; comment) - sh_goy is the grid's own origin (raw [sh_oy] + SH_MBAR_H);
; everything from sh_mopen down is sh_mtrack/sh_mbar_*/sh_mdrop_*/
; sh_mitem_hit's shared working state.
sh_goy        equ sh_sort_src + 2
sh_mopen      equ sh_goy + 2               ; byte: open menu index, SH_M_NONE
sh_mhi        equ sh_mopen + 1             ; byte: hot item in the open
                                             ; dropdown, SH_M_NONE
sh_mrx1       equ sh_mhi + 1               ; the open dropdown's own rect
sh_mry1       equ sh_mrx1 + 2
sh_mrx2       equ sh_mry1 + 2
sh_mry2       equ sh_mrx2 + 2
sh_mbx1       equ sh_mry2 + 2              ; sh_mboxof's own output: one
sh_mbx2       equ sh_mbx1 + 2              ; menu title's screen box
sh_mw         equ sh_mbx2 + 2              ; SH_MENU_N words: each title's
                                             ; pixel width (sh_mtab_calc)
sh_mli        equ sh_mw + (SH_MENU_N*2)    ; generic loop-index scratch,
                                             ; shared by every sh_m* routine
                                             ; above (none of them nest)
sh_mto        equ sh_mli + 2               ; generic sh_mtab byte-offset
                                             ; scratch, same sharing rule
sh_mip        equ sh_mto + 2               ; the open menu's items array ptr
sh_mcnt       equ sh_mip + 2               ; the open menu's item count
sh_mmaxw      equ sh_mcnt + 2              ; sh_mdrop_geo's running max
                                             ; item-label width
sh_mry_row    equ sh_mmaxw + 2             ; sh_mdrop_draw's current row y

sh_gridlines     equ sh_mry_row + 2        ; byte: Options > Gridlines, 1=on
sh_showformulas  equ sh_gridlines + 1      ; byte: Options > Formulas, 1=on

; Border dialog (stage 2.x, sh_bdlg_*) - same "own scratch, not stack
; juggling" shape as sh_fdlg_*'s own bss block above
sh_bdlg_win    equ sh_showformulas + 1     ; word: 0 = none, the gate
sh_bdlg_sel    equ sh_bdlg_win + 2         ; byte: the 6 checkboxes' state,
                                             ; SH_BDLG_B_* bits
sh_bdlg_ox     equ sh_bdlg_sel + 1
sh_bdlg_oy     equ sh_bdlg_ox + 2
sh_bdlg_ri     equ sh_bdlg_oy + 2          ; the row loop's own index
sh_bdlg_ry     equ sh_bdlg_ri + 2          ; ...and that row's y
sh_bdlg_rect   equ sh_bdlg_ry + 2          ; 4 words: one button rect,
                                             ; reused for OK then Cancel

; sh_drawborders' own scratch (stage 2.x) - the four edges' screen rect for
; whichever bordered cell it is currently drawing
sh_bdrawflags  equ sh_bdlg_rect + 8        ; byte: that cell's border byte
sh_bx1         equ sh_bdrawflags + 1
sh_by1         equ sh_bx1 + 2
sh_bx2         equ sh_by1 + 2
sh_by2         equ sh_bx2 + 2
sh_bti         equ sh_by2 + 2              ; word: the scan loop's own index
                                             ; (not CX - see sh_drawborders)

; stage 2.x: runtime cell dimensions (Format > Column Width.../Row
; Height...) - see the SH_CW_*/SH_RH_* section comment above sh_entry
sh_cellw       equ sh_bti + 2              ; word: current column width, px
sh_cellh       equ sh_cellw + 2            ; word: current row height, px
sh_cellch      equ sh_cellh + 2            ; word: sh_cellw / 8, in chars
sh_blank       equ sh_cellch + 2           ; SH_CW_MAXCH+1: as many spaces as
                                             ; the WIDEST column the Column
                                             ; Width dialog will accept, plus
                                             ; the NUL (sh_mkblank). It was 11
                                             ; - SH_CW_WIDE/8 plus a NUL, right
                                             ; for the three presets it was
                                             ; written for and wrong the moment
                                             ; a numeric width could be typed:
                                             ; a width of 12 wrote 13 bytes and
                                             ; the two that fell off the end
                                             ; landed on sh_chartseg, one word
                                             ; further down. See 81.21

; Data > Chart Column... (stage 2.x) - a live second window; see the
; SH_CLAIM_CHART_KB comment above sh_entry for why it exists and the
; window-lifecycle note above sh_docmd_chart for why sh_chartwin, once
; set, is never zeroed again this session (only shown/hidden)
sh_chartseg    equ sh_blank + SH_CW_MAXCH + 1  ; word: the offscreen canvas claim
sh_chartwin    equ sh_chartseg + 2         ; word: 0 = never created; else its
                                             ; window ptr, permanently valid
sh_chart_sheet equ sh_chartwin + 2         ; word: which sheet the open chart
                                             ; is pinned to (frozen at open)
sh_chart_col   equ sh_chart_sheet + 2      ; word: which column is pinned
                                             ; (frozen at open - re-run the
                                             ; menu item to retarget)
sh_chart_cnt   equ sh_chart_col + 2        ; word: values currently plotted,
                                             ; 0 = nothing yet (Export checks
                                             ; this)
sh_chart_name  equ sh_chart_cnt + 2        ; 13: the exported .BMP's own 8.3
                                             ; name buffer (separate from
                                             ; sh_name, which is Sheet's own
                                             ; load/save filename)

; apps/os88chart.inc's own required scratch (see that file's header comment)
ch_max         equ sh_chart_name + 13
ch_base        equ ch_max + 2
ch_arr         equ ch_base + 2
ch_cnt         equ ch_arr + 2
ch_idx         equ ch_cnt + 2
ch_bx1         equ ch_idx + 2
ch_by1         equ ch_bx1 + 2
ch_bx2         equ ch_by1 + 2
ch_by2         equ ch_bx2 + 2
ch_srcseg      equ ch_by2 + 2
ch_stgseg      equ ch_srcseg + 2
ch_neg         equ ch_stgseg + 2     ; stage 3.0f: 1 = some value is
                                       ; negative. Its own word now: the axis
                                       ; row is type-dependent, so ch_base
                                       ; cannot carry this as well.
ch_type        equ ch_neg + 2       ; CH_T_* - which chart to draw
ch_lx0         equ ch_type + 2      ; the current segment's endpoints and
ch_ly0         equ ch_lx0 + 2       ; the column being interpolated -
ch_lx1         equ ch_ly0 + 2       ; CALLER bss like every other ch_*
ch_ly1         equ ch_lx1 + 2       ; word, for the same DS reason
ch_lcx         equ ch_ly1 + 2

; sh_rowcol_reidx and friends (stage 2.x) - see the section comment above
; sh_rowcol_reidx itself for what each of these holds
ch_pie_px      equ ch_lcx + 2       ; --- stage 3.0f: the pie ---
ch_pie_py      equ ch_pie_px + 2
ch_pie_ex      equ ch_pie_py + 2    ; ch_ray's endpoint and its Bresenham
ch_pie_ey      equ ch_pie_ex + 2    ; state - in bss for the same DS reason
ch_pie_x       equ ch_pie_ey + 2    ; every other ch_* word is
ch_pie_y       equ ch_pie_x + 2
ch_pie_dx      equ ch_pie_y + 2
ch_pie_dy      equ ch_pie_dx + 2
ch_pie_sx      equ ch_pie_dy + 2
ch_pie_sy      equ ch_pie_sx + 2
ch_pie_err     equ ch_pie_sy + 2
ch_pie_e2      equ ch_pie_err + 2
ch_pie_tlo     equ ch_pie_e2 + 2    ; the 32-bit total and how far it was
ch_pie_thi     equ ch_pie_tlo + 2   ; shifted to fit a word
ch_pie_shift   equ ch_pie_thi + 2
ch_pie_a0      equ ch_pie_shift + 2 ; this slice's first half-degree...
ch_pie_span    equ ch_pie_a0 + 2    ; ...how many it covers...
ch_pie_a       equ ch_pie_span + 2  ; ...and the sweep's current one
ch_pie_col     equ ch_pie_a + 2
ch_pie_thick   equ ch_pie_col + 2    ; byte: this ray fills, so it is 3px
ch_pie_pen     equ ch_pie_thick + 1  ; byte: the colour ch_setpixel keeps
ch_pie_pat     equ ch_pie_pen + 1    ; byte: this slice's hatch, FF = solid
ch_tx          equ ch_pie_pat + 1   ; --- stage 3.0f: text into the canvas ---
ch_ty          equ ch_tx + 2
ch_tpen        equ ch_ty + 2
ch_tsrc        equ ch_tpen + 2        ; the string cursor, across ch_glyph
ch_tseg        equ ch_tsrc + 2        ; the GLYPH TABLE's segment, not KERNEL_SEG
ch_ttab        equ ch_tseg + 2
ch_tfirst      equ ch_ttab + 2        ; the character range the table covers
ch_tlast       equ ch_tfirst + 2
ch_tglyph      equ ch_tlast + 2       ; -> the current character's 8 rows
ch_trow        equ ch_tglyph + 2
ch_tcol        equ ch_trow + 2
ch_tpy         equ ch_tcol + 2
ch_tbits       equ ch_tpy + 2
ch_tnum        equ ch_tbits + 2       ; 16: ch_itoa_t's/ch_num_t's output -
                                      ; eight held "-32768" and nothing more,
                                      ; and a scaled label can carry a point
                                      ; and four digits, or nine trailing
                                      ; zeros (see ch_scale)
ch_e10         equ ch_tnum + 16     ; the series' decimal exponent (82.13)
ch_sc_seg      equ ch_e10 + 2       ; ch_scale's own scratch
ch_sc_src      equ ch_sc_seg + 2
ch_sc_dst      equ ch_sc_src + 2
ch_sc_cnt      equ ch_sc_dst + 2
ch_dbl         equ ch_sc_cnt + 2    ; 8: the value being converted...
ch_dmax        equ ch_dbl + 8       ; 8: ...and the largest seen
ch_title       equ ch_dmax + 8      ; -> the chart's title, or 0 for none
ch_legy        equ ch_title + 2     ; the legend row being drawn...
ch_legr        equ ch_legy + 2      ; ...and the swatch row inside it
ch_arr2        equ ch_legr + 2       ; --- the SECOND series (82.8) ---
ch_cnt2        equ ch_arr2 + 2      ; 0 = there is no second series
ch_srcseg2     equ ch_cnt2 + 2
ch_max2        equ ch_srcseg2 + 2   ; its own scale, independent of the first
ch_mkx         equ ch_max2 + 2      ; ch_mark's centre
ch_mky         equ ch_mkx + 2
ch_scx         equ ch_mky + 2       ; a scatter point's x, across the y maths
ch_cbx         equ ch_scx + 2       ; a combination point...
ch_cby         equ ch_cbx + 2
ch_lcy         equ ch_cby + 2       ; ...and the previous one's y
ch_l2x         equ ch_lcy + 2       ; ch_line2's Bresenham state
ch_l2y         equ ch_l2x + 2
ch_l2ex        equ ch_l2y + 2
ch_l2ey        equ ch_l2ex + 2
ch_l2dx        equ ch_l2ey + 2
ch_l2dy        equ ch_l2dx + 2
ch_l2sx        equ ch_l2dy + 2
ch_l2sy        equ ch_l2sx + 2
ch_l2err       equ ch_l2sy + 2
ch_l2e2        equ ch_l2err + 2
sh_chart_title equ ch_l2e2 + 2      ; 16: "Column A"
sh_scan_col    equ sh_chart_title + 16  ; which column a scan pass reads...
sh_scan_off    equ sh_scan_col + 2      ; ...and where in sh_stgseg it lands
sh_chart_cnt2  equ sh_scan_off + 2      ; the second series' own count
sh_rpn_p       equ sh_chart_cnt2 + 2  ; --- stage 4.5: the RPN emitter ---
sh_rpn_len     equ sh_rpn_p + 2
sh_rpn_bad     equ sh_rpn_len + 2    ; byte: this formula cannot be expressed
sh_rpn_rel     equ sh_rpn_bad + 1    ; the two relative-reference flags
sh_rpn_r1      equ sh_rpn_rel + 2    ; a range's first cell, held across the
sh_rpn_c1      equ sh_rpn_r1 + 2     ; second one's parse
sh_rpn_buf     equ sh_rpn_c1 + 2     ; SH_RPN_MAX: the token array
sh_rwsrc          equ sh_rpn_buf + SH_RPN_MAX             ; SH_EDITMAX+1: the formula
                                              ; text copied out for rewriting
sh_rwdst          equ sh_rwsrc + SH_EDITMAX + 1  ; SH_RW_CAP: the rewritten
                                              ; text being built
sh_rw_di          equ sh_rwdst + SH_RW_CAP   ; word: sh_rw_emit's own cursor
sh_rw_op          equ sh_rw_di + 2           ; byte: sh_rc_op, copied in
sh_rw_pivot       equ sh_rw_op + 1           ; word: sh_rc_idx, copied in
sh_rw_tsheet      equ sh_rw_pivot + 2        ; word: the sheet this whole
                                              ; operation is acting on
sh_rw_home        equ sh_rw_tsheet + 2       ; byte: 1 if the formula being
                                              ; rewritten right now lives on
                                              ; sh_rw_tsheet itself
sh_rw_adj         equ sh_rw_home + 1         ; byte: sh_reidx_cellpart's own
                                              ; "adjust this one" flag
sh_rw_ostart      equ sh_rw_adj + 1          ; word: the reference's own
                                              ; text start, for a verbatim copy
sh_rw_lettersend  equ sh_rw_ostart + 2       ; word: where its letters end
                                              ; (and its digits, if any, start)
sh_rw_refcol      equ sh_rw_lettersend + 2   ; word: the reference as parsed
sh_rw_refrow      equ sh_rw_refcol + 2
sh_rw_refend      equ sh_rw_refrow + 2       ; word: just past its digits
sh_rw_recdi       equ sh_rw_refend + 2       ; word: sh_rowcol_reidx's own
                                              ; current record offset

; Copy/Paste relative-reference adjustment (stage 2.x) - see the section
; comment above sh_copy_shift for what each of these holds
sh_clip_col       equ sh_rw_recdi + 2        ; word: sh_docmd_copy's own
sh_clip_row       equ sh_clip_col + 2        ; source cell
sh_clip_valid     equ sh_clip_row + 2        ; byte: 1 once any Copy has
                                              ; run this session
sh_cp_coldelta    equ sh_clip_valid + 1      ; word: sh_docmd_paste's own
sh_cp_rowdelta    equ sh_cp_coldelta + 2     ; (dest - source) delta
sh_cp_ostart      equ sh_cp_rowdelta + 2     ; word: sh_copy_cellpart's own
                                              ; scratch - same shape as
                                              ; sh_rw_ostart/lettersend/
                                              ; refcol/refrow/refend above,
                                              ; just a separate copy since
                                              ; a row/col insert and a
                                              ; paste never run at once but
                                              ; sharing the same words
                                              ; would still be confusing
sh_cp_lettersend  equ sh_cp_ostart + 2
sh_cp_refcol      equ sh_cp_lettersend + 2
sh_cp_refrow      equ sh_cp_refcol + 2
sh_cp_refend      equ sh_cp_refrow + 2

; Stage 3.0a: multi-cell range selection. sh_selcol/sh_selrow keep their
; existing meaning as the ANCHOR (and, for every single-cell operation, still
; simply "the selected cell"); these two are the moving end of the block. A
; collapsed selection has extent == anchor, which is what sh_select sets, so
; every existing single-cell caller keeps working untouched.
sh_selcol2        equ sh_cp_refend + 2
sh_selrow2        equ sh_selcol2 + 2
sh_selc1          equ sh_selrow2 + 2   ; sh_selrect's normalized output -
sh_selc2          equ sh_selc1 + 2     ; c1<=c2, r1<=r2, so no consumer has
sh_selr1          equ sh_selc2 + 2     ; to care which corner was dragged
sh_selr2          equ sh_selr1 + 2     ; from
sh_sc_tcol        equ sh_selr2 + 2     ; sh_scrollto_t's target cell
sh_sc_trow        equ sh_sc_tcol + 2
sh_drag_col       equ sh_sc_trow + 2   ; the cell the drag handler last
sh_drag_row       equ sh_drag_col + 2  ; landed on - "redraw only on a
                                        ; change", per OSAPI_WM_ONDRAG's own
                                        ; warning that it fires per mouse
                                        ; packet and a repaint per packet is
                                        ; tens of ms on a 4.77MHz machine
sh_dragging       equ sh_drag_row + 2  ; byte: a press is armed on the grid
sh_selvc2         equ sh_dragging + 1  ; sh_drawsel's viewport-clamped
sh_selvr2         equ sh_selvc2 + 2    ; bottom-right, in window cells

; stage 3.0b: the formula bar's content box, as a real os88line field. Its
; rect is refreshed from the live geometry on every draw (the window moves and
; resizes), so only LN_BUF/LN_MAX are set once at entry.
sh_fline          equ sh_selvr2 + 2    ; OS88LINE_SZ bytes

; stage 3.0a+: the two scroll bars. Both use os88ui.inc's OWN seven-word block
; layout - x1,y1,x2,y2 (absolute, inclusive), total, fit, pos - so the vertical
; one is passed straight to os88ui_sbar/sbhit/sbgrab/sbtrack, and the private
; horizontal one below is a transposition of the same words rather than a
; different structure (see sh_hsb_* for why it is private and what it is
; staged to become).
sh_vsb            equ sh_fline + 20    ; 7 words
sh_hsb            equ sh_vsb + 14      ; 7 words
sh_sb_oldpos      equ sh_hsb + 14      ; word: the pos a scroll started from,
                                        ; for os88ui_sbmove's cheap redraw
sh_hsb_dragon     equ sh_sb_oldpos + 2 ; byte: 1 = a horizontal thumb drag is
                                        ; live (the vertical one's state is
                                        ; os88ui.inc's own static)
sh_hsb_dragoff    equ sh_hsb_dragon + 1 ; word: press x - thumb left
; sh_hsb_*'s own scratch. The rect is copied out of the block before ANY
; drawing, because the gfx primitives take AX/BX/CX/DX as their rect and BX is
; also the block pointer - holding both in BX is the clobber this codebase has
; hit three times already.
sh_hsb_x1         equ sh_hsb_dragoff + 2
sh_hsb_y1         equ sh_hsb_x1 + 2
sh_hsb_x2         equ sh_hsb_y1 + 2
sh_hsb_y2         equ sh_hsb_x2 + 2
sh_hsb_tl         equ sh_hsb_y2 + 2    ; the thumb's left
sh_hsb_tw         equ sh_hsb_tl + 2    ; ...and its width

; stage 3.0b: the note table's claim, and the Note... dialog's state. The
; EDIT BUFFER IS REAL BSS rather than a pointer into the arena, because the
; arena is append-only: the dialog edits a copy and only commits it on OK, so
; Cancel costs nothing and a refused commit leaves the old note intact.
sh_noteseg        equ sh_hsb_tw + 2    ; word: the note table's segment
sh_nnote          equ sh_noteseg + 2   ; word: records in it
sh_notetext       equ sh_nnote + 2     ; SH_NOTEMAX bytes: the edit buffer
sh_notebox        equ sh_notetext + SH_NOTEMAX  ; OS88TEXT_SZ bytes: the field
sh_noteopen       equ sh_notebox + 20  ; byte: 1 = the dialog is up
sh_notecol        equ sh_noteopen + 1  ; word: the cell it was opened on -
sh_noterow        equ sh_notecol + 2   ; NOT the live selection, which the
                                       ; user can still move behind a
                                       ; non-modal dialog
sh_ndlg_win       equ sh_noterow + 2   ; word: 0 = none, the same gate shape
sh_ndlg_ox        equ sh_ndlg_win + 2  ; as sh_bdlg_win
sh_ndlg_oy        equ sh_ndlg_ox + 2
sh_ndlg_rect      equ sh_ndlg_oy + 2   ; 4 words: one button rect, refilled
                                       ; per button (os88ui_btn takes a
                                       ; POINTER to it)

; stage 3.0c: the generic one-line input dialog, shared by Goto..., Row
; Height... and Column Width... (see SH_ID_* for why one dialog serves three).
sh_idlg_win       equ sh_ndlg_rect + 8 ; word: 0 = none, the single-instance
sh_idlg_kind      equ sh_idlg_win + 2  ; byte: SH_ID_*                   gate
sh_idlg_buf       equ sh_idlg_kind + 1 ; SH_EDITMAX bytes: what is typed
sh_idlg_line      equ sh_idlg_buf + SH_EDITMAX   ; OS88LINE_SZ bytes
sh_idlg_ox        equ sh_idlg_line + 20
sh_idlg_oy        equ sh_idlg_ox + 2
sh_idlg_rect      equ sh_idlg_oy + 2   ; 4 words: one button rect

; stage 3.0e: absolute references. Each scanner records whether the reference
; it is looking at pinned its column and/or its row with '$', and its adjuster
; then declines to move the pinned half - that refusal is the whole feature.
sh_rw_absc        equ sh_idlg_rect + 8 ; byte: Insert/Delete's scanner
sh_rw_absr        equ sh_rw_absc + 1
sh_cp_absc        equ sh_rw_absr + 1   ; byte: Copy/Paste + Fill's scanner
sh_cp_absr        equ sh_cp_absc + 1

; stage 3.0d: which cell the evaluator is CURRENTLY inside, for ROW()/COLUMN().
; Saved and restored around each sh_eval_cell so a formula reached through
; another cell's reference still answers for itself, not for whoever asked.
sh_rc_ccol        equ sh_cp_absr + 1   ; the cell that OWNS the formula being
sh_rc_crow        equ sh_rc_ccol + 2   ; converted to or from R1C1 - every
                                       ; relative offset is measured from it
sh_evrow          equ sh_rc_crow + 2   ; word: 0-based
sh_evcol          equ sh_evrow + 2     ; word: 0-based
; stage 4.0: the value accumulator the evaluator now carries, and every
; scratch word apps/os88fp.inc's header says the caller owes it.
sh_acc            equ sh_evcol + 2     ; 8: the expression's current value
sh_lhs            equ sh_acc + 8       ; 8: a binary operator's left operand,
                                       ; recovered from the stack
fp_as             equ sh_lhs + 8
fp_bs             equ fp_as + 1
fp_ae             equ fp_bs + 1
fp_be             equ fp_ae + 2
fp_am0            equ fp_be + 2
fp_am1            equ fp_am0 + 2
fp_am2            equ fp_am1 + 2
fp_am3            equ fp_am2 + 2
fp_bm0            equ fp_am3 + 2
fp_bm1            equ fp_bm0 + 2
fp_bm2            equ fp_bm1 + 2
fp_bm3            equ fp_bm2 + 2
fp_t0             equ fp_bm3 + 2
fp_t1             equ fp_t0 + 2
fp_t2             equ fp_t1 + 2
fp_t3             equ fp_t2 + 2
fp_p0             equ fp_t3 + 2        ; 8 words: the 128-bit product
fp_sticky         equ fp_p0 + 16
fp_tmp            equ fp_sticky + 2
fp_dig            equ fp_tmp + 2       ; 24: fp_ftoa's digit string
fp_d10            equ fp_dig + 24
fp_nd             equ fp_d10 + 2
fp_sgn            equ fp_nd + 2
fp_sq             equ fp_sgn + 2       ; 8: fp_sqrt's input, across iterations
fp_g              equ fp_sq + 8        ; 8: its running guess
fp_tv             equ fp_g + 8         ; 8: fp_floor's general temporary
fp_hw             equ fp_tv + 8        ; --- the coprocessor path ---
fp_x1             equ fp_hw + 1        ; 10: A in 80-bit form
fp_x2             equ fp_x1 + 10       ; 10: B
fp_sw             equ fp_x2 + 10       ; where the status word lands
sh_cry_key         equ fp_sw + 2        ; the key column and cell, banked
sh_cry_keyrow      equ sh_cry_key + 2   ; before any carry moves them
sh_cry_i           equ sh_cry_keyrow + 2 ; the carry loops' index
sh_cry_c1          equ sh_cry_i + 2        ; sh_sort_carry's column span...
sh_cry_c2          equ sh_cry_c1 + 2
sh_cry_col         equ sh_cry_c2 + 2     ; ...the one being carried...
sh_cry_src         equ sh_cry_col + 2    ; ...and the entry it is taking from
sh_cry_trow       equ sh_cry_src + 2
sh_cry_srow       equ sh_cry_trow + 2
sh_sort_r1        equ sh_cry_srow + 2   ; the rows Sort was asked for
sh_sort_r2        equ sh_sort_r1 + 2
sh_pb_c0          equ sh_sort_r2 + 2        ; the paste block's landing
sh_pb_r0          equ sh_pb_c0 + 2     ; corner...
sh_pb_x           equ sh_pb_r0 + 2     ; ...the cell being written
sh_pb_y           equ sh_pb_x + 2
sh_pb_cur         equ sh_pb_y + 2      ; ...and where the reader is
sh_pb_len         equ sh_pb_cur + 2
sh_tabanchor      equ sh_pb_len + 2        ; word: 0 = no Tab run in progress,
                                       ; else the run's start column PLUS ONE
sh_fl_scol        equ sh_tabanchor + 2 ; sh_fill_copy's source cell...
sh_fl_srow        equ sh_fl_scol + 2
sh_fl_dcol        equ sh_fl_srow + 2   ; ...and its destination
sh_fl_drow        equ sh_fl_dcol + 2
sh_needld         equ sh_fl_drow + 2   ; byte: an ARG_FILE document is
                                       ; noted and not yet read
sh_argdir         equ sh_needld + 1    ; word: the directory it is in
sh_argdrv         equ sh_argdir + 2    ; byte: ...and that volume
sh_savepend       equ sh_argdrv + 1    ; byte: File Format's OK owes a Save As

; The damage-rect machinery (perf review): what a selection move or a scroll
; actually dirtied, so the hot paths stop paying the ~1s full repaint.
sh_commitdirty    equ sh_savepend + 1  ; byte: sh_commit stored something -
                                       ; sh_selpaint consumes it and pays the
                                       ; full repaint (dependent formulas)
sh_chartdirty     equ sh_commitdirty + 1 ; byte: a cell record changed since
                                       ; the chart last resynced (sh_repaint's
                                       ; tail reads it, sh_addcell/
                                       ; sh_removecell set it)
sh_oldc1          equ sh_chartdirty + 1 ; sh_selbank's bank of the ordered
sh_oldc2          equ sh_oldc1 + 2     ; rect a selection move started from...
sh_oldr1          equ sh_oldc2 + 2
sh_oldr2          equ sh_oldr1 + 2
sh_oldscol        equ sh_oldr2 + 2     ; ...and the scroll origin
sh_oldsrow        equ sh_oldscol + 2
sh_dmgc1          equ sh_oldsrow + 2   ; the range the ranged grid painters
sh_dmgc2          equ sh_dmgc1 + 2     ; draw (window-relative cells,
sh_dmgr1          equ sh_dmgc2 + 2     ; inclusive; sh_dmgfull = the whole
sh_dmgr2          equ sh_dmgr1 + 2     ; viewport)
sh_blitx1         equ sh_dmgr2 + 2     ; sh_scrollrow_blit's rect (also
sh_blitx2         equ sh_blitx1 + 2    ; sh_dmgdraw's band-fill x span)...
sh_blity1         equ sh_blitx2 + 2
sh_blity2         equ sh_blity1 + 2
sh_blitdel        equ sh_blity2 + 2    ; ...and its signed row delta
sh_bss_end        equ sh_blitdel + 2

; -----------------------------------------------------------------------------
; The bss size above is a PLAIN LITERAL and nothing in the toolchain checks it
; against the equ chain - setting it low is silent corruption of whatever the
; loader placed next, not a build error. It cannot simply be written as
; `OS88_BSS sh_bss_end - os88_image_end`: OS88_BSS_SIZE goes into the package
; header's dw at a FIXED OFFSET near the top of the image, so it has to be
; known on pass 1, and a forward reference to a label defined down here makes
; NASM size instructions differently per pass - the "changed during code
; generation" failure this file has already hit twice.
;
; So it stays a literal, and this asserts it instead. A mismatch drives one of
; the two TIMES counts negative, which -w+error turns into a build failure that
; prints the exact shortfall. Both are zero when the literal is right, so
; nothing is emitted.
;
; READ THE LINE NUMBER, not just the sign: the two TIMES lines report the same
; shortfall with opposite signs, so "which one fired" is what says whether the
; literal is too small or too large. Mistaking one for the other sends you
; chasing a discrepancy that is not there.
; -----------------------------------------------------------------------------
%define SH_BSS_NEED (sh_bss_end - os88_image_end)
    times (SH_BSS_NEED - OS88_BSS_SIZE) db 0
    times (OS88_BSS_SIZE - SH_BSS_NEED) db 0
