; =============================================================================
; os8088 - apps/notepad/notepad.asm
;
; NOTEPAD, the third software package (SPEC.md 27) - formerly the built-in
; Note Pad app (KIND_NOTE). Moved out of the kernel to reclaim the 1,317
; bytes it cost there: 281 of code and 1,036 of .bss, nearly all of the
; latter a fixed two-instance text pool. As a package that pool disappears
; entirely - every instance is its own relocated copy with its own bss
; (SPEC.md 20.1), so the buffer below is simply per-instance, and the
; instance count is bounded by the region pool instead of a hard-coded 2.
;
; Editing behaviour is unchanged from the built-in (SPEC.md 14): printable
; 32..126 append, backspace deletes, Enter stores a newline byte; text wraps
; at the content width BY WORD (SPEC.md 27.11), with 6px margins, rows that
; would overflow the
; content bottom are dropped rather than scrolled, and a 1px caret follows
; the text when its own row fits.
;
; What is new is SPEC.md 27.1: Ctrl-S saves the note to NOTES.TXT on the
; mounted data disk and Ctrl-O loads it back - they were F2 and F3, and F3 is
; Find Next now (SPEC.md 27.10) - over the file API of SPEC.md 18.4 - which
; makes this package the first caller of those slots and the proof that they
; work from an ordinary window callback. Line endings are translated in both
; directions (13 here, CR LF on the disk), because the whole point of
; writing a DOS filesystem is that the other machine can read the file.
; Results are reported as a toast in the content's top-right corner, retired
; by the next keystroke.
;
; Those two commands now also have a face. SPEC.md 12.2 gives every
; application the menu bar while its window is frontmost, so Note Pad ships
; a one-menu set - File: New, Open, Save - registered from the entry proc
; and dispatched to np_oncmd. The menu is strictly a second door onto the
; existing routines: "Open" is np_load, "Save" is np_save, Ctrl-O and Ctrl-S
; still call exactly the same two, and both doors end at np_redraw. "New" is the
; one thing here that is genuinely new rather than a second door - emptying
; the buffer had no key and no button before - and it is menu-only for the
; same reason it was missing: there was no spare key worth spending on it.
;
; The state lookup is the one thing that got simpler. The built-in reached
; its state through inst_of_win -> I_SPTR because all instances shared one
; pool; a package addresses its own bss directly.
; =============================================================================

%include "os88api.inc"

; SPEC.md 13.10.5's thumb GESTURE, and it SHIPS (13.10.7);
; `make SBDRAGOFF=1` compiles it out.
;
; **AT THE TOP, NOT BESIDE THE %include THAT PULLS os88ui.inc IN.** A %ifdef is
; a PREPROCESSOR test answered in FILE ORDER, and that include sits ~10,000
; lines below here - so a define made down there is not made yet at the window
; entry, the setter, the bar ladder or the two edge handlers, and every one of
; those blocks vanishes while the element's drag body assembles perfectly.
; The binary grows, nothing errors, and the thumb simply does not move
; (SPEC.md 13.10.7.4). kernel.asm carries the identical note for the identical
; reason.
%ifndef SBDRAGOFF
%define OS88UI_SBDRAG
%endif

    OS88_HEADER 'NOTEPAD', np_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A page with a folded top-right corner and five lines of writing, two of
; them short - the ragged right edge is what reads as text rather than as a
; grille at 16px. The mask is the page silhouette dilated 1px, so it sits on
; a clean white underlay over the desktop grey and over a selected row.
;
;   data                             mask
;   ................   .###########....
;   ..#########.....   .############...
;   ..#.......##....   .#############..
;   ..#.......#.#...   .##############.
;   ..#.#####.####..   .##############.
;   ..#..........#..   .##############.
;   ..#.########.#..   .##############.
;   ..#..........#..   .##############.
;   ..#.########.#..   .##############.
;   ..#..........#..   .##############.
;   ..#.########.#..   .##############.
;   ..#..........#..   .##############.
;   ..#.#####....#..   .##############.
;   ..#..........#..   .##############.
;   ..############..   .##############.
;   ................   .##############.
    OS88_ICON16
    dw 0x7FF0                       ; 16 mask rows
    dw 0x7FF8
    dw 0x7FFC
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
    dw 0x0000                       ; 16 data rows
    dw 0x3FE0
    dw 0x2030
    dw 0x2028
    dw 0x2FBC
    dw 0x2004
    dw 0x2FF4
    dw 0x2004
    dw 0x2FF4
    dw 0x2004
    dw 0x2FF4
    dw 0x2004
    dw 0x2F84
    dw 0x2004
    dw 0x3FFC
    dw 0x0000
    OS88_ICON16_END

; --- the document lives in the HEAP, not in this package's bss (SPEC.md 27.6)
; A note is data, and data of a size only the user knows; a package's region
; is image + bss capped at APP_MAX_SIZE, so anything sized by the user belongs
; in a claim (SPEC.md 50.3). The claim starts at NP_KB0, grows a kilobyte at a
; time as the note fills it, is sized to the file on a load, and shrinks back
; on File > New. [np_dseg]:0000 is the text and [np_cap] its capacity.
NP_KB0       equ 1              ; the document claim at launch, KB
NP_GROWKB    equ 1              ; ...and the quantum it grows by
NP_MAXKB     equ 16             ; ...and its ceiling, which is now a real
                                ; ceiling rather than a stand-in for one. It
                                ; was 8, and 8 was never about memory: it was
                                ; what the WINDOW could show, because Note Pad
                                ; did not scroll and text past the last
                                ; visible row could be typed and never read
                                ; back. It scrolls now (SPEC.md 27.7), so what
                                ; bounds the note is the arithmetic instead -
                                ; a save expands every newline to CR LF, and
                                ; its staging pass walks that with a 16-bit DI
                                ; and counts it with a 16-bit BX. At 16KB the
                                ; worst case is 32,768 and both hold; at 32KB
                                ; it is 65,536 and both wrap to zero, and so
                                ; does np_stghold's own 2 x [np_len]. So the
                                ; SAVE is what bounds the note, not the
                                ; window and not the heap - going further
                                ; means teaching that loop to cross a segment
                                ; and making its count 32-bit
NP_STGMIN    equ 1              ; the save's transient staging claim, KB
NP_MAXCOL    equ 171            ; cells a row can hold, plus one for the NUL.
                                ; A row is accumulated into a buffer and drawn
                                ; as ONE opaque font_run (SPEC.md 6.1/27.2)
                                ;
                                ; 1360/8 AND NOT 720/8. It was the widest
                                ; SCREEN, and a window on an extended desktop
                                ; is not bounded by one: straddling the seam
                                ; it may be the whole VIRTUAL desktop wide,
                                ; which is 1360 px with a 720 Hercules beside
                                ; a 640 CGA or VGA (SPEC.md 39.19.2). Past the
                                ; clamp np_rflush DROPS the cell - the guard
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
%define NP_MAXROWS 60           ; signature slots, one per row the content can
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
                                ; own band needs is 59. np_bounds clamps to
                                ; this, so a taller screen degrades to "the
                                ; rows past 60 are always redrawn" rather than
                                ; writing past the array
                                ; (NP_BSS_TOTAL lives at the foot of this file
                                ; now, with the fields it counts)
NP_BRK_CELLS equ 60             ; the visual break's trigger (SPEC.md 27.3):
                                ; the CELLS a keystroke would repaint BELOW
                                ; the caret's row. Not rows - this window is
                                ; resizable and a row is 30 cells or 90
                                ; depending how wide the user dragged it, so
                                ; a row count means two different amounts of
                                ; work. A cell is ~0.9ms on a 4.77MHz 8088 -
                                ; measured four ways, PERFORMANCE.md Part 2 -
                                ; so 60 cells is ~54ms, which is the point
                                ; where a keystroke stops keeping up
NP_IDLE      equ 9              ; ticks of no typing before the break is
                                ; reconciled: ~500ms at 18.2Hz
NP_WTICKS    equ 3              ; ...and how often the worker looks, ~165ms.
                                ; Finer than NP_IDLE so the settle lands
                                ; near the deadline rather than a tick late
NP_HCHUNK    equ 4              ; rows of the height count per worker pass
                                ; (SPEC.md 27.7.3). The count is the one walk
                                ; that cannot be bounded by the view, so it is
                                ; bounded by TIME instead: this many rows, then
                                ; the lock goes back.
                                ;
                                ; It sizes TWO things and the second is what
                                ; set it. The lock HOLD is what a UI action
                                ; waits behind, and the DUTY CYCLE is what the
                                ; count costs the rest of the machine: the
                                ; worker sleeps NP_WTICKS between passes, so
                                ; the fraction spent counting is hold/(hold +
                                ; 165ms). At 16 rows the hold measured 124ms
                                ; (`make npbench`) - two ticks, and 43% of the
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
NP_SB_W      equ 14             ; scroll bar width, the Disk window's
                                ; (SPEC.md 22) so the two look like one OS.
                                ; It is reserved ALWAYS, present or not:
                                ; whether a bar is NEEDED depends on the row
                                ; count, which depends on the wrap width,
                                ; which would depend on the bar
NP_SB_ARR    equ 11             ; ...and the arrow cells at each end of it
%ifdef OS88UI_SBDRAG
; THIS WINDOW'S THUMB-DRAG POLICY (SPEC.md 13.10.5.4). RATE 0: a scroll here
; ends in np_redraw, which re-letters every row the view moved past, and
; PERFORMANCE.md prices a row of text in tens of milliseconds on a 4.77 MHz
; 8088 - so a view that followed the hand would be seconds behind it. The
; thumb follows; the text arrives when the button comes up.
%ifndef SB_RATE
%define SB_RATE 0
%endif
NP_SBRATE   equ SB_RATE
%endif

NP_SB_STEP   equ 4              ; rows an arrow cell steps. The Disk window
                                ; steps one, but its rows are 16px list
                                ; entries and these are 8px lines of prose:
                                ; four of them is about the same travel, and
                                ; a blit-scrolled band costs the same whether
                                ; it moves one row or four (SPEC.md 27.7.2)
NP_GROW      equ 13             ; the grow box the kernel draws in the
                                ; content's bottom-right corner (SPEC.md
                                ; 11.1). The bar stops above it, exactly as
                                ; the Disk window's stops above its status
                                ; line - drawn into, the two overlap and the
                                ; down arrow comes out as a square
NP_MARGIN    equ 8              ; left/top text margin inside the content. It
                                ; was 6, and 8 is what puts every glyph cell
                                ; on a multiple of 8 once OSAPI_WM_SNAP has
                                ; put the content origin on one (SPEC.md
                                ; 11.94): np_tx is content left + this, and
                                ; np_walk advances the pen by 8 from there.
                                ; A glyph at an unaligned x spills into a
                                ; SECOND framebuffer byte whenever the shift
                                ; carries ink into it, and this window redraws
                                ; text on every keystroke
                                ; The two F-keys are NP_KEY_NEXT and
                                ; NP_KEY_PREV below, and they are the only
                                ; two: F2 was Save and F3 was Load, the DOS
                                ; Editor's pair, and both are Ctrl-letters now
                                ; (SPEC.md 27.1/27.10). What made the DOS keys
                                ; worth keeping was that they were the ones a
                                ; user of that machine already knew, and this
                                ; window has a Macintosh menu bar over it
NP_K_HOME    equ 0x47           ; the caret keys, int 16h scan codes
NP_K_UP      equ 0x48
NP_K_LEFT    equ 0x4B
NP_K_RIGHT   equ 0x4D
NP_K_END     equ 0x4F
NP_K_DOWN    equ 0x50
NP_K_DEL     equ 0x53
NP_MI_NEW    equ 0              ; File menu item indices - the order of
NP_MI_OPEN   equ 1              ; np_items_file, which is what the kernel
NP_MI_SAVE   equ 2              ; hands np_oncmd in AL (SPEC.md 12.2)
NP_MI_SAVEAS equ 3
NP_NAMEMAX   equ 12             ; 8 + '.' + 3, as SPEC.md 38.6 hands it over

; --- the Edit menu (SPEC.md 27.8) --------------------------------------------
NP_MI_UNDO   equ 0              ; menu 1's items, in np_items_edit's order.
NP_MI_CUT    equ 1              ; There is no Clear: Backspace and Delete
NP_MI_COPY   equ 2              ; already delete a selection (SPEC.md 27.8),
NP_MI_PASTE  equ 3              ; so it was a third door onto np_selkill
NP_MI_SELALL equ 4

NP_FI_FIND   equ 0              ; ...and menu 2's, in np_items_find's
NP_FI_NEXT   equ 1
NP_FI_REPL   equ 2

; --- keys (SPEC.md 27.8) -----------------------------------------------------
; The control characters int 16h already hands over in AL. Backspace (8),
; Tab (9) and Enter (13) are Ctrl-H/I/M, so those three letters are spoken
; for and none of the shortcuts below uses them.
NP_C_SELALL  equ 0x01           ; Ctrl-A
NP_C_COPY    equ 0x03           ; Ctrl-C
NP_C_FIND    equ 0x06           ; Ctrl-F
NP_C_OPEN    equ 0x0F           ; Ctrl-O
NP_C_REPL    equ 0x12           ; Ctrl-R
NP_C_SAVE    equ 0x13           ; Ctrl-S
NP_C_PASTE   equ 0x16           ; Ctrl-V
NP_C_CUT     equ 0x18           ; Ctrl-X
NP_C_UNDO    equ 0x1A           ; Ctrl-Z
NP_C_ESC     equ 0x1B
NP_C_TAB     equ 0x09
NP_KEY_NEXT  equ 0x3D           ; F3 - Find Next. It WAS Load, and Load has a
                                ; menu item and Ctrl-O now: a find with no
                                ; key for the next match is not a find
NP_KEY_PREV  equ 0x56           ; Shift-F3 (Shift-F1..F10 are 0x54..0x5D)

; --- the selection (SPEC.md 27.8) --------------------------------------------
NP_SELDRAG   equ 3              ; pixels the pointer must travel before a
                                ; press inside a selection becomes a MOVE
                                ; rather than a click that collapses it

; --- undo (SPEC.md 27.9) -----------------------------------------------------
NP_UNDO      equ 5              ; edits deep, as asked
NP_UKB0      equ 1              ; the arena's first claim, KB...
NP_UMAXKB    equ 16             ; ...and its ceiling. 16 and not 8, because a
                                ; Replace All records the whole tail of the
                                ; note it rewrote and the note itself caps at
                                ; NP_MAXKB - so this is what makes the one
                                ; operation nobody wants to retype by hand
                                ; undoable at all

; --- find and replace (SPEC.md 27.10) ----------------------------------------
NP_PATMAX    equ 47             ; characters a pattern or a replacement holds
NP_RXST      equ 12             ; backtrack frames the matcher may stack. An
                                ; EXPLICIT stack in bss, not the CPU's: a
                                ; recursive matcher would run on the worker's
                                ; 256-byte task stack (SPEC.md 8), and a
                                ; pattern is user input
NP_FP_ROW    equ 12             ; the panel's row pitch: an 8px line plus 4
NP_FP_PAD    equ 2              ; ...and the border above and below it
NP_FP_LBL    equ 44             ; the 'Find:'/'Repl:' label column
NP_FP_BTNH   equ 11             ; button height

; THE PANEL'S HEIGHT IS A MULTIPLE OF 4, and that is SPEC.md 27.10.3 rather
; than a layout preference. Opening or closing it is one OSAPI_GFX_SCROLL of
; the whole text band (27.10.2), and the delta that blit is given is exactly
; this height - so it is the height that decides which of gfx_scroll's two
; paths runs. SPEC.md 5.5.1's constant-delta path derives the destination row
; address ONCE and steps it, and on a banked adapter it is gated on
; `dy & [vid_bmask] == 0`: bmask is 3 on Hercules and 1 on the CGA, so an ODD
; height misses it on both and every row of the blit pays a gfx_rowbase walk.
;
; 2*ROW + 2*PAD + 1 is ODD for every value of ROW and PAD - 2*anything is even
; and the separating rule adds one - so no choice of those two can fix it and
; the correction has to be explicit. Rounding UP rather than down, because
; down would have to take a pixel off something that is using it.
NP_FP_RAW    equ NP_FP_ROW*2 + NP_FP_PAD*2 + 1     ; 29: two rows, the border
                                                   ; above and below, and the
                                                   ; 1px rule under it all
NP_FP_SLACK  equ (-NP_FP_RAW) & 3                  ; 3, to the next multiple
NP_FP_H      equ NP_FP_RAW + NP_FP_SLACK           ; 32 - and 44 with Replace,
                                                   ; which needs NP_FP_ROW to
                                                   ; be a multiple of 4 too:
%if (NP_FP_ROW & 3) != 0
  %error "NP_FP_ROW must be a multiple of 4, or the Replace row's +ROW breaks the alignment NP_FP_SLACK just bought"
%endif
NP_FPAN_NONE equ 0              ; [np_fpan]: closed / find / find + replace
NP_FPAN_FIND equ 1
NP_FPAN_REPL equ 2
NP_FF_FIND   equ 0              ; [np_ffield]: which box the keys go to. 2 is
NP_FF_REPL   equ 1              ; the document, which is where Tab lands last
NP_FF_DOC    equ 2

; -----------------------------------------------------------------------------
; np_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here. The
; bss arrives zeroed, which is already a fresh empty note - the built-in's
; KD_INIT proc (app_note_kinit) had nothing else to do either.
;
; The menu set is registered here rather than later because the loader's
; wm_show is what draws the first bar (SPEC.md 12.2): by the time the window
; appears, the bar already says "Note Pad  File".
; -----------------------------------------------------------------------------
np_entry:
    call np_xdrop                   ; the row index starts EMPTY and at its
                                    ; initial stride (SPEC.md 27.13). bss
                                    ; arrives zeroed, and a zero [np_xksh] is
                                    ; a stride of ONE - not wrong, but 64
                                    ; entries covering 64 rows and halving four
                                    ; times to catch up on the first count
    mov ax, NP_KB0                  ; the document, before anything else: an
    call OSAPI_MEM_CLAIM            ; editor with nowhere to put the text is
    jc .nomem                       ; not a window worth opening, and the
    mov [np_dseg], dx               ; loader's LD_EABORT says so for us
    mov word [np_capkb], NP_KB0
    mov word [np_cap], NP_KB0 * 1024
    mov ax, np_reloc                ; ...and it MOVES (SPEC.md 66.5.7), from
    call OSAPI_MEM_MOVABLE          ; here rather than later: an empty note
                                    ; has nothing pointing into it, and DX is
                                    ; still the claim
    push si
    mov si, np_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): np_paint
    call OSAPI_WM_SIZABLE           ; already lays out from the live record,
    mov al, 1                       ; so the next repaint re-wraps for free
    mov al, 1                       ; ...and it PROMISES its content stands
    call OSAPI_WM_SAVEU             ; still while it is not drawing, so a
                                    ; raise puts the old pixels back instead
                                    ; of lettering 464 cells (SPEC.md 11.96.1).
                                    ; True of this app: everything that draws
                                    ; goes through np_redraw or np_paint, and
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
%ifdef OS88UI_SBDRAG
    pushf                           ; THE ENTRY STILL OWES THE LOADER
                                    ; wm_create's CF, and OSAPI_WM_ONDRAG
                                    ; STATES a flag of its own (SPEC.md
                                    ; 13.8.2) - so the two installs go inside
                                    ; a pushf exactly as the CPU_INFO block
                                    ; below does
    mov ax, np_onup                 ; SPEC.md 13.7 / 13.8.2: the release and
    call OSAPI_WM_ONMOUSEUP         ; the tracking edge, both AFTER wm_create
    mov ax, np_ondrag               ; and neither a template word
    call OSAPI_WM_ONDRAG
    sbb al, al                      ; CF = 1 on kern_small: 0xFF into the byte
    mov [np_nodrag], al             ; the grab site tests
    popf
%endif
    mov ax, np_onclose              ; SPEC.md 75.1: the kernel asks before it
    call OSAPI_WM_ONCLOSE           ; closes us, and a note with unsaved work
                                    ; answers. A side table, so this goes here
                                    ; and not in np_tpl; it preserves the
                                    ; FLAGS, which the CF we still owe the
                                    ; loader depends on
    push si
    mov si, np_menus                ; BX is still the window: hand it our
    call OSAPI_MENU_SET             ; menus (draws nothing, takes no lock)
    pop si                          ; CF is still wm_create's: the branch
                                    ; above consumed it and OSAPI_MENU_SET
                                    ; preserves flags too (SPEC.md 20.3)
    mov [np_win], bx                ; the worker (SPEC.md 27.3) has no callback
                                    ; to be handed this in SI
    pushf                           ; the visual break exists for the machine
    push ax                         ; that cannot repaint a screenful between
    call OSAPI_CPU_INFO             ; keystrokes, and nowhere else: on anything
    cmp al, CPU_8086                ; faster the reflow is already invisible
    jne .nobrk                      ; and a temporary layout would be a lie
    mov byte [np_brkok], 1          ; told for no gain. The flags are saved
.nobrk:                             ; around it because the CF this proc owes
    pop ax                          ; the loader is still riding in them and
    popf                            ; the compare above would eat it
    mov word [np_prowi], 0xFFFF     ; .bss arrives zeroed and 0 is a REAL row
                                    ; index, so the delta cache has to be told
                                    ; it holds nothing (SPEC.md 27.2)
    mov byte [np_ffield], NP_FF_DOC  ; ...and 0 is a real FOCUS too - the find
                                    ; box - so without this a fresh Note Pad
                                    ; sends every keystroke to a panel that is
                                    ; not on screen, and typing does nothing
    mov word [np_dpos], 0xFFFF      ; no drop marker, and 0 is a real index
    mov word [np_dmark], 0xFFFF
    pushf                           ; ...and so must this: the bss arrives
    call np_defname                 ; zeroed and an empty name is not a file
    call np_arg                     ; (SPEC.md 27.1), but np_defname is an
    call np_mark                    ; ...and WHATEVER we start with is clean
                                    ; (SPEC.md 27.15): an empty note, or the
                                    ; document np_arg just loaded. After
                                    ; np_arg and not before, which is what
                                    ; makes one call cover both
    popf                            ; ordinary routine and the CF we owe the
                                    ; loader is still riding in the flags
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
; [np_top] is the note row drawn at the top of the view, and np_walk's np_row
; - the index into np_sig, np_rows and the dirty band - starts at MINUS it.
; Nothing downstream of that had to change: every array index is already an
; unsigned test against a limit, and a negative row read as unsigned is past
; all of them. The one place that could not see it is np_rflush, because a row
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
; np_scrollmax - the largest [np_top] that still shows text
; out: AX = max(np_drows - np_vrows, 0); preserves all other registers
; -----------------------------------------------------------------------------
np_scrollmax:
    mov ax, [np_drows]
    sub ax, [np_vrows]
    jns .out
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; np_scrollto - put row AX at the top of the view
; in:  AX = the wanted row (signed; clamped here), SI = window ptr
; out: CF=0 if [np_top] moved, CF=1 if it did not; preserves all registers
;
; Everything indexed by the visible row means something different afterwards -
; the signatures, the checkpoint and np_rows all shift by the same amount as
; the view - so all three are dropped rather than adjusted. Adjusting them
; would be a second place that has to agree about what a row is.
; -----------------------------------------------------------------------------
np_scrollto:
    push ax
    push bx
    or ax, ax
    jns .lo
    xor ax, ax
.lo:
    push ax
    call np_scrollmax
    mov bx, ax
    pop ax
    cmp ax, bx
    jbe .hi
    mov ax, bx
.hi:
    cmp ax, [np_top]
    je .same
    mov [np_top], ax                ; np_sig and np_rows are NOT dropped here
                                    ; any more: they still describe the pixels
                                    ; that are still on screen, and
                                    ; np_scrollpaint shifts all three together
                                    ; (SPEC.md 27.7.2). [np_ptop] is what says
                                    ; the two have parted, and every path out
                                    ; of np_redraw puts them back in step
    mov byte [np_ckok], 0           ; the checkpoint is one row rather than an
                                    ; array, so it is cheaper to re-find than
                                    ; to carry: the view seed replaces it
    mov byte [np_resume], 0         ; ...including one ALREADY LOADED: np_walk
                                    ; takes [np_sdr] as a visible row and
                                    ; np_measure does not clear the flag, so a
                                    ; scroll between np_seedck and the walk it
                                    ; seeded resumes at the wrong row and every
                                    ; number that walk produces - [np_cury] and
                                    ; the note's height - is out by the scroll
    mov byte [np_bmode], 0          ; and the visual break's fiction is over
    clc
    jmp short .out
.same:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_seecaret - scroll so the caret is inside the view
; in:  [np_cury] from a walk, SI = window ptr
; out: CF=0 if the view moved; preserves all registers
; -----------------------------------------------------------------------------
np_seecaret:
    push ax
    push cx
    push dx
    mov ax, [np_cury]               ; the caret's VISIBLE row, signed: a caret
    sub ax, [np_ty]                 ; above the view has a pen y above np_ty
    mov cl, 3
    sar ax, 1                       ; an arithmetic shift, three times: the
    sar ax, 1                       ; 8086 has no `sar ax, 3`
    sar ax, 1
    or ax, ax
    js .up
    cmp ax, [np_vrows]
    jb .same                        ; already in view
    sub ax, [np_vrows]
    inc ax
    add ax, [np_top]                ; ...below it: bring it to the last row
    jmp short .go
.up:
    add ax, [np_top]                ; ...above it: bring it to the first
.go:
    call np_scrollto
    jmp short .out
.same:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; np_sbar - draw the scroll bar
; in:  SI = window ptr, np_bounds run, gfx lock held
; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; np_sbset - fill np_sb with this window's bar (SPEC.md 13.10)
; out: BX = the block; every other register preserved
;
; Note Pad's bar was already PIXEL-IDENTICAL to the shared element - 14 wide,
; NP_SB_ARR = 11 putting the rule at ty+10 and the track at ty+11, the arrow
; centred on x1+6 - so this conversion changes nothing on the glass. It was
; simply the third copy of a picture the kernel now draws once.
; -----------------------------------------------------------------------------
np_sbset:
    push ax
    mov ax, [np_sbr]
    sub ax, NP_SB_W-1
    mov [np_sb+0], ax
    mov ax, [np_sbr]
    mov [np_sb+4], ax
    mov ax, [np_ty]
    mov [np_sb+2], ax
    mov ax, [np_sbb]
    mov [np_sb+6], ax
    mov ax, [np_drows]
    mov [np_sb+8], ax
    mov ax, [np_vrows]
    mov [np_sb+10], ax
    mov ax, [np_top]
    mov [np_sb+12], ax
    mov bx, np_sb
%ifdef OS88UI_SBDRAG
    call os88ui_sbfix           ; ...and while a thumb drag is live, word 6 is
%endif                          ; the HAND's row rather than the view's
                                ; (SPEC.md 13.10.5.6)
    pop ax
    ret

np_sb:      dw 0,0,0,0,0,0,0
np_sbmoved: db 0                ; np_sbclick's SECOND answer (SPEC.md 27.7.10):
                                ; CF says the bar took the click, this says
                                ; whether the VIEW moved because of it
%ifdef OS88UI_SBDRAG
np_nodrag:  db 0                ; 0xFF = this kernel has no tracking edge
                                ; (SPEC.md 13.8.2 - kern_small answers CF = 1),
                                ; so the thumb stays inert there rather than
                                ; taking a gesture nothing will ever feed
%endif

np_sbar:
    push ax
    push bx
    call np_sbset
    call os88ui_sbar
    mov ax, [np_top]                ; remember what is on screen, so a redraw
    mov [np_sbtop], ax              ; that moved neither number draws nothing
    mov ax, [np_drows]
    mov [np_sbrows], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_sbcheck - redraw the bar only if it would look different
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_sbcheck:
    push ax
    push bx
    mov ax, [np_drows]
    cmp ax, [np_sbrows]
    jne .full                       ; the TOTAL moved, so the thumb's HEIGHT
    mov ax, [np_top]                ; did: only a full draw can resize it
    cmp ax, [np_sbtop]
    je .out                         ; neither moved: draw nothing at all
%ifdef OS88UI_SBDRAG
    call os88ui_sbdragging          ; ...unless the hand is on the thumb, in
    jnc .full                       ; which case it is drawn where the HAND is
                                    ; and not at [np_sbtop] (SPEC.md 13.10.5.6)
                                    ; - so the "where it was" this needs is not
                                    ; the one banked here, and the full draw is
                                    ; the one that is right either way
%endif
    call np_sbset                   ; BX = the block, holding the NEW pos...
    mov ax, [np_sbtop]              ; ...and this is where the thumb is DRAWN
    call os88ui_sbmove              ; THREE drawing calls against the bar's
                                    ; sixteen (SPEC.md 13.10.3): a scroll moves
                                    ; neither total nor fit, so the frame, both
                                    ; rules, both arrow glyphs and all of the
                                    ; track the thumb did not cover are exactly
                                    ; where they were. ~10 ms a scroll on the
                                    ; target machine, and this app scrolls on
                                    ; every arrow key
    mov ax, [np_top]
    mov [np_sbtop], ax
    jmp short .out
.full:
    call np_sbar
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_sbclick - a press inside the scroll bar
; in:  CX = x, DX = y (absolute), np_bounds run, gfx lock held
; out: CF=0 the bar took it (and may have scrolled), CF=1 it was not ours;
;      preserves all registers
;
; Zones are the Disk window's (SPEC.md 22): the two arrow cells step a row,
; the track pages against the thumb, and the thumb itself does nothing -
; there is no drag, here or there.
; -----------------------------------------------------------------------------
; np_sbhit - is the click at (CX, DX) inside the scroll bar's rect?
; out: CF = 0 = yes; preserves every register
;
; Its own routine because np_onclick asks the same question for a different
; reason - whether this click needs the note's height to be EXACT - and two
; copies of a rect are two things that have to agree about where the bar is.
np_sbhit:
    push ax
    mov ax, [np_sbr]
    sub ax, NP_SB_W-1
    cmp cx, ax
    jb .no
    cmp dx, [np_ty]
    jb .no
    cmp dx, [np_sbb]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

np_sbclick:
    push ax
    push bx
    push dx
    mov byte [np_sbmoved], 0        ; ...until something actually moves
    call np_sbhit
    jc .no
    mov bx, dx                      ; BX = the click's y
    mov ax, [np_ty]
    add ax, NP_SB_ARR
    cmp bx, ax
    jb .lineup
    mov ax, [np_sbb]
    sub ax, NP_SB_ARR
    cmp bx, ax
    ja .linedn
    call np_sbset                   ; the SHARED element's parts (SPEC.md
    call os88ui_sbhit               ; 13.10) - BX = the block np_sbset just
                                    ; filled, and CX/DX are still the point,
                                    ; absolute, as it takes them
    cmp al, OS88UI_SBPGUP
    je .pageup
    cmp al, OS88UI_SBPGDN
    je .pagedn
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB          ; SPEC.md 13.10.5: the thumb is DRAGGED
    jne .yes                        ; now, where it was inert. BX is still the
    cmp byte [np_nodrag], 0         ; block and DX still the press, absolute
    jne .yes
    mov al, NP_SBRATE
    call os88ui_sbgrab              ; CF = 1 = no thumb after all; either way
%endif                              ; this click scrolls nothing
    jmp short .yes                  ; the thumb itself, or an inert track
.lineup:
    mov ax, [np_top]
    sub ax, NP_SB_STEP
    jmp short .set
.linedn:
    mov ax, [np_top]
    add ax, NP_SB_STEP
    jmp short .set
.pageup:
    mov ax, [np_top]
    sub ax, [np_vrows]
    jmp short .set
.pagedn:
    mov ax, [np_top]
    add ax, [np_vrows]
.set:
    ; [np_drows] is a LOWER BOUND while the count is unfinished (SPEC.md
    ; 27.7.4), so np_scrollmax would clamp this short of a row that really
    ; exists. Only a request that reaches past the counted extent needs the
    ; exact total - every other one is answered by what is already known, and
    ; every one of them used to pay for a full walk (SPEC.md 27.7.6).
    cmp byte [np_hdirty], 0
    je .doset
    push ax                         ; the row being asked for...
    call np_scrollmax               ; ...against the furthest one counted so far
    mov bx, ax                      ; (BX is this routine's own, saved above)
    pop ax
    cmp ax, bx
    jbe .doset
    push si                         ; SI is the CALLER's - np_onclick has more
    mov si, [np_win]                ; to do with it after this returns
    call np_height
    pop si
.doset:
    call np_scrollto                ; CF = 1: an END STOP - it did not move,
    jc .yes                         ; so there is nothing to draw
    mov byte [np_sbmoved], 1
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

%ifdef OS88UI_SBDRAG
; -----------------------------------------------------------------------------
; np_ondrag / np_onup - the thumb gesture's other two edges (SPEC.md 13.10.5)
; in:  CX = x, DX = y (ABSOLUTE), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; One body with two names and one difference - track against drop - because
; everything either of them has to do before and after that call is the same.
; The release COMMITS unconditionally (13.10.5.4): a position the rate refused
; is never lost.
;
; np_bounds first, because the block's rect and both of its counts come from
; the live window record and this app is resizable.
; -----------------------------------------------------------------------------
np_ondrag:
    push ax
    push bx
    push cx
    push dx
    call os88ui_sbdragging
    jc np_sbd_out
    call np_bounds
    call np_sbset               ; BX = the block; DX is still the pointer's y
    call os88ui_sbtrack         ; CF = 1: nothing owed - the rate, or the same
    jc np_sbd_out               ; row
    jmp short np_sbd_go
np_onup:
    push ax
    push bx
    push cx
    push dx
    call os88ui_sbdragging
    jc np_sbd_out
    call np_bounds
    call np_sbset
    call os88ui_sbdrop
    jc np_sbd_out
np_sbd_go:                      ; FLAT labels and not `.go`/`.out`: the two
                                ; entries share one tail, and a local label
                                ; belongs to whichever non-local one preceded
                                ; it - so np_ondrag's `.out` and np_onup's are
                                ; two different symbols and only one of them
                                ; exists
    call np_scrollto            ; AX = the row the hand is on, SI = the window
    jc np_sbd_out               ; an end stop: not one pixel changes
    call np_redraw
np_sbd_out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

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
; What makes it safe is that the blit shifts the PIXELS and np_shiftrows
; shifts their DESCRIPTION - np_sig and np_rows - by the same d, in the same
; operation. SPEC.md 27.7 says those two arrays are dropped rather than
; adjusted on a scroll, and that was right while the pixels were being
; redrawn wholesale: adjusting would have been a second place that has to
; agree about what a row is. Here there is no second place. The pixels and
; the arrays move together or not at all, and [np_ptop] - the [np_top] the
; screen was drawn for - is the one fact that says which.
; =============================================================================

; -----------------------------------------------------------------------------
; np_vshift - move the whole text band DI pixels (signed; positive = text up)
; in:  DI = the signed pixel distance, np_bounds run, gfx lock held
; out: CF = OSAPI_GFX_SCROLL's answer; preserves all registers
;
; The x span is rounded OUTWARD to byte columns, and that is what lets this
; work on every adapter rather than only where OSAPI_WM_SNAP aligns the
; content (SPEC.md 11.94). Rounding x1 DOWN stays inside the content because
; NP_MARGIN is 8 and the rounding moves it at most 7; rounding x2+1 UP reaches
; at most seven columns into the scroll bar, which np_sbar redraws
; immediately afterwards because the thumb has moved anyway. The band
; therefore CONTAINS every glyph pixel, which the break's np_scroll - rounding
; inward, and needing [np_tx] aligned for it - does not have to.
; -----------------------------------------------------------------------------
np_vshift:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [np_tx]
    and ax, 0xFFF8                  ; x1, down to a byte column
    mov bx, [np_ty]                 ; y1
    mov cx, [np_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                          ; x2, with x2+1 up to a byte column
    mov dx, [np_vrows]              ; y2 stops at the bottom of the last WHOLE
    push cx                         ; row, not at [np_bot]: a content height
    mov cl, 3                       ; that is not a multiple of 8 leaves a
    shl dx, cl                      ; sliver below it, np_rflush refuses to
    pop cx                          ; draw a row that would cross it, and so
    add dx, [np_ty]                 ; nothing would ever erase what the blit
    dec dx                          ; pushed into it. Scrolled to [np_bot] it
    cmp dx, [np_bot]                ; showed as a one-pixel band of the row
    jbe .yok                        ; above's descenders, left behind for good
    mov dx, [np_bot]                ; - and only on a window whose content
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
; np_shiftrows - move np_sig and np_rows by [np_sdlt] rows
; in:  [np_sdlt] = the signed row delta, |d| < [np_vrows]
; out: nothing; preserves all registers
;
; Entry r must end up holding what entry r+d held, because the pixels of row
; r+d are now at row r. The slots with no source are the exposed rows, and the
; pass that letters them rewrites both arrays for exactly those.
; -----------------------------------------------------------------------------
np_shiftrows:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                          ; both arrays are ours (SPEC.md 20.1)
    mov ax, [np_sdlt]
    or ax, ax
    js .up
    mov cx, [np_vrows]              ; DOWN: dst r, src r+d, ascending
    sub cx, ax
    jbe .out
    mov bx, ax
    shl bx, 1                       ; BX = d in bytes
    push cx
    mov di, np_sig
    mov si, di
    add si, bx
    cld
    rep movsw
    pop cx
    mov di, np_rows
    mov si, di
    add si, bx
    rep movsw
    jmp short .out
.up:
    neg ax                          ; UP: dst r+|d|, src r, DESCENDING, or the
    mov cx, [np_vrows]              ; copy would overwrite its own source
    sub cx, ax
    jbe .out
    mov bx, [np_vrows]
    dec bx
    shl bx, 1                       ; BX = the last row's byte offset
    mov di, bx
    sub bx, ax
    sub bx, ax                      ; ...and BX = that minus |d| words
    mov si, bx
    push cx
    push si
    push di
    add si, np_sig
    add di, np_sig
    std
    rep movsw
    cld
    pop di
    pop si
    pop cx
    add si, np_rows
    add di, np_rows
    std
    rep movsw
    cld
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_scrollpaint - move the view with a blit instead of a repaint
; in:  SI = window ptr, np_bounds run, gfx lock held, [np_top] ALREADY moved,
;      [np_dr0]/[np_dr1] = whatever rows the caller found dirty in the frame
;      the screen is still showing (0xFFFF/0 = none)
; out: CF=0 the screen is correct and both arrays describe it; CF=1 nothing
;      was drawn and the caller must repaint in full. Preserves all registers.
; -----------------------------------------------------------------------------
np_scrollpaint:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [np_sigok], 0
    je .nope                        ; the arrays do not describe the screen,
                                    ; so there is nothing to shift
    cmp word [np_vrows], 2
    jb .nope
    mov ax, [np_top]
    sub ax, [np_ptop]               ; AX = d, signed
    jz .nope                        ; the view did not actually move
    mov [np_sdlt], ax
    mov bx, ax
    or bx, bx
    jns .abs
    neg bx
.abs:
    cmp bx, [np_vrows]
    jae .nope                       ; nothing is retained: a repaint is the
                                    ; same work without the blit
    mov di, ax
    push cx
    mov cl, 3
    shl di, cl                      ; DI = d*8, and a positive d moves the
    pop cx                          ; view down, which moves the text UP
    call np_vshift
    jc .nope                        ; refused, and having drawn nothing

    ; Rounding x2+1 outward carried up to seven columns of furniture with the
    ; text: the scroll bar's left frame at np_rgt+1, and the left edge of the
    ; grow box below it. Blank that strip and let the two things that own it
    ; put themselves back - np_sbar at the end of this routine, and the grow
    ; box here, because np_sbar stops short of the corner it sits in.
    mov ax, [np_rgt]
    inc ax                          ; x1, the first column past the text
    mov cx, [np_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                          ; x2, the same one np_vshift moved
    cmp ax, cx
    ja .nostrip
    mov bx, [np_ty]
    mov dx, [np_bot]
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

    mov ax, [np_sdlt]               ; the rows the blit did not fill in
    or ax, ax
    js .exup
    mov bx, [np_vrows]              ; view moved DOWN: they are at the bottom
    sub bx, ax
    mov [np_bd0], bx
    mov bx, [np_vrows]
    dec bx
    mov [np_bd1], bx
    jmp short .band
.exup:
    mov word [np_bd0], 0            ; ...and UP: at the top
    neg ax
    dec ax
    mov [np_bd1], ax
.band:
    ; ...plus whatever the caller already knew was dirty, which it counted in
    ; the OLD frame. A caret that moved off a row leaves that row needing a
    ; redraw even though the blit carried it faithfully - so an Up that
    ; scrolls has TWO dirty rows, the one it arrived on and the one it left.
    mov ax, [np_dr0]
    cmp ax, 0xFFFF
    je .shift
    sub ax, [np_sdlt]
    jns .d0ok
    xor ax, ax                      ; it half scrolled off the top
.d0ok:
    mov bx, [np_dr1]
    sub bx, [np_sdlt]
    js .shift                       ; ...or all of it did
    cmp bx, [np_vrows]
    jb .d1ok
    mov bx, [np_vrows]
    dec bx
.d1ok:
    cmp ax, bx
    ja .shift
    cmp ax, [np_bd0]
    jae .hi
    mov [np_bd0], ax
.hi:
    cmp bx, [np_bd1]
    jbe .shift
    mov [np_bd1], bx
.shift:
    call np_shiftrows

    mov ax, [np_bd0]                ; erase the band: OSAPI_GFX_SCROLL leaves
    push cx                         ; the vacated rows holding a copy of what
    mov cl, 3                       ; was next to them, and a row of the new
    shl ax, cl                      ; text may be shorter than that or empty
    pop cx
    add ax, [np_ty]
    mov bx, ax                      ; BX = y1
    mov ax, [np_bd1]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]
    add ax, 7
    cmp ax, [np_bot]
    jbe .yok
    mov ax, [np_bot]
.yok:
    mov dx, ax                      ; DX = y2
    mov ax, [np_tx]
    sub ax, NP_MARGIN               ; AX = x1, the content's own left edge
    mov cx, [np_rgt]                ; CX = x2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [np_prowi], 0xFFFF     ; the fill erased what the delta cache knew

    mov word [np_hity], 0xFFFF      ; one pass, drawing AND re-signing: the
    mov word [np_wanty], 0xFFFF     ; band was just filled, so np_clean, and
    mov ax, [np_bd0]                ; np_clip confines it to the band by ROW
    mov [np_dr0], ax                ; rather than by signature - an exposed
    mov ax, [np_bd1]                ; row's old signature is the row that
    mov [np_dr1], ax                ; scrolled away and could match by luck
    mov byte [np_draw], 1
    mov byte [np_sigup], 1
    mov byte [np_clip], 1
    mov byte [np_clean], 1
    mov byte [np_resume], 0
    cmp byte [np_rowsok], 0
    je .xseed
    mov ax, [np_bd0]
    or ax, ax
    jz .xseed                       ; the band starts at the top of the view:
    dec ax                          ; there is no earlier row to start from
    mov bx, ax                      ; np_rows is valid up to here and no
    inc bx                          ; further, the entries above the band
    mov [np_rowsn], bx              ; having just been shifted out of range
    mov dx, [np_vrows]
    call np_seedrow
    cmp byte [np_resume], 0
    jne .noseed
.xseed:
    mov ax, [np_top]                ; ...and the ROW INDEX has one (SPEC.md
    mov dx, [np_vrows]              ; 27.13). This is every scroll UPWARD: the
    call np_xseed                   ; exposed row is row 0 of the view, so
                                    ; there is nothing above it in np_rows and
                                    ; the walk went back to index 0 to letter
                                    ; ONE row - 335 ms of a 640 ms Up
.noseed:
    mov ax, [np_bd1]                ; STOP AT THE BAND, not at the bottom of
    mov [np_lastrow], ax            ; the view (SPEC.md 27.13). Nothing below
                                    ; np_bd1 is drawn - np_clip says so - and
                                    ; nothing below it needs LAYING OUT
                                    ; either: OSAPI_GFX_SCROLL moved those
                                    ; pixels and np_shiftrows moved np_sig and
                                    ; np_rows by the same d, so their
                                    ; descriptions already match the glass.
                                    ; Walking to the view's bottom to draw one
                                    ; exposed row was 20 rows for 1 - 333 ms of
                                    ; a 520 ms Up
    call np_walk
    mov byte [np_clip], 0
    mov byte [np_clean], 0

    mov ax, [np_top]
    mov [np_ptop], ax               ; the screen shows this view now
    call np_sbar                    ; unconditional: the thumb moved, and the
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
; np_bounds - the content rectangle and the text origin, from the live record
; in:  SI = window ptr, ES = KERNEL_SEG (as every callback is entered)
; out: [np_tx] = the wrap column and left margin, [np_ty] = the first text
;      row, [np_rgt]/[np_bot] = the content's inclusive right and bottom;
;      preserves all registers
;
; A resizable window lays out from the record every time (SPEC.md 11.1), and
; BOTH passes of np_walk need the same four numbers, so they are read once
; here rather than twice in slightly different words.
; -----------------------------------------------------------------------------
np_bounds:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    push ax
    push dx
    add ax, NP_MARGIN
    mov [np_tx], ax
    push ax                         ; the find panel is docked at the TOP of
    call np_fph                     ; the content (SPEC.md 27.10), so the text
    add dx, ax                      ; simply starts lower - which is the whole
    pop ax                          ; of what the rest of this module has to
    add dx, NP_MARGIN               ; know about it. np_bounds's own geometry
    mov [np_ty], dx                 ; test below then catches the change and
                                    ; np_sigsame turns it into a full repaint
    call OSAPI_WM_GEOM              ; CX/DX = content w/h (BX still the window)
    pop ax                          ; content top
    add ax, dx
    dec ax                          ; the last drawable row...
    mov [np_bot], ax
    pop ax                          ; content left
    add ax, cx
    dec ax
    mov [np_sbr], ax                ; ...and the last drawable column.
    sub ax, NP_SB_W                 ; The scroll bar owns the rightmost
                                    ; NP_SB_W of it (SPEC.md 27.7). This was
    mov [np_rgt], ax                ; W_X+W_W-2 read off the record through ES;
                                    ; origin + size - 1 is the same pixel and
                                    ; needs no kernel pointer of our own - and
                                    ; it stays right under WF_FULL, where the
                                    ; frame IS the content (SPEC.md 11.2)

    mov ax, [np_rgt]                ; whole 8px CELLS between the pen and the
    sub ax, [np_tx]                 ; right edge: the width of one opaque run,
    inc ax                          ; and what the row buffer is padded to
    jns .cok
    xor ax, ax
.cok:
    mov cl, 3
    shr ax, cl
    cmp ax, NP_MAXCOL - 1
    jbe .csave
    mov ax, NP_MAXCOL - 1
.csave:
    mov [np_rcols], ax

    mov ax, [np_bot]                ; ...and how many whole 8px rows that is,
    sub ax, [np_ty]                 ; which is what the signature array is
    jc .norows                      ; indexed by (SPEC.md 27.2)
    cmp ax, 7
    jb .norows
    sub ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    inc ax
    cmp ax, NP_MAXROWS
    jbe .vok
    mov ax, NP_MAXROWS
.vok:
    mov [np_vrows], ax
    jmp short .sbb
.norows:
    mov word [np_vrows], 0
.sbb:
    mov ax, [np_bot]                ; the bar's own bottom, clear of the corner
    sub ax, NP_GROW                 ; the kernel draws the grow box in
    mov [np_sbb], ax
    call np_hguess                  ; ...and now the geometry is known, what the
                                    ; note's LENGTH already says about its
                                    ; height (SPEC.md 27.7.3)
.geom:
    ; The checkpoint and np_rows are ROW INDICES, so they mean nothing under a
    ; different geometry - and unlike the signatures, nothing else was going to
    ; notice. np_sigsame guards np_redraw; this guards everything else, which
    ; is every caret key and every click.
    ; ...and what "a different geometry" means here is the WRAP WIDTH and the
    ; view HEIGHT, never the origin. These four words are the content box in
    ; SCREEN coordinates, so dragging the window changes np_tx and np_ty while
    ; the note wraps identically - and comparing them absolutely made every
    ; MOVE set [np_gchg], which np_paint pays with np_measure: an unbounded
    ; walk of the whole note. On a 16KB file that is seconds of a window that
    ; has been dropped and is not yet drawing, reported as exactly that. The
    ; comment below is a RESIZE argument and always was.
    mov ax, [np_rgt]
    sub ax, [np_tx]                 ; the wrap width now...
    mov dx, [np_srgt]
    sub dx, [np_stx]                ; ...against the one the screen was laid
    cmp ax, dx                      ; out under
    jne .stale
    mov ax, [np_bot]
    sub ax, [np_ty]                 ; ...and the view height, which decides how
    mov dx, [np_sbot]               ; many of those rows fit and so whether the
    sub dx, [np_sty]                ; view is looking past the end
    cmp ax, dx
    je .out
.stale:
    mov byte [np_ckok], 0
    mov byte [np_rowsok], 0
    mov byte [np_gchg], 1           ; ...and the note is a different NUMBER of
                                    ; rows under a different wrap width, so
                                    ; the view can be looking past the end of
                                    ; it. Only a walk knows how many, so this
                                    ; says "one is owed" and np_paint pays it
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_walk - THE layout pass: one loop, two jobs (SPEC.md 27)
; in:  [np_bounds] already run; [np_draw] = 1 to paint, 0 to measure only;
;      [np_cur]; the two optional queries below
; out: [np_curx]/[np_cury] = where the caret sits, in pixels
;      [np_hiti]  = the character index the point [np_hitx],[np_hity] falls
;                   on ([np_hity] = 0xFFFF disables the test)
;      [np_wanti] = the index at column [np_wantx] of row [np_wanty]
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
; Every index 0..[np_len] is visited, including the one PAST the last
; character - that is where the caret lives in a note that ends in text, so
; it has to be a position the queries can return.
;
; The caret occupies a cell and therefore wraps like one, which is what keeps
; it in front of the character it precedes rather than stranded at the end of
; the row above.
; -----------------------------------------------------------------------------
np_walk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov word [np_curx], 0
    mov word [np_cury], 0
    mov byte [np_curseen], 0        ; ...and 0 is a REAL pen y on a fullscreen
                                    ; window, so "did this walk find it" needs
                                    ; a flag of its own now that a walk may
                                    ; stop before the caret (SPEC.md 27.7)
    mov ax, [np_len]
    mov [np_hiti], ax               ; a click past the end lands at the end
    mov word [np_wanti], 0xFFFF     ; ...but a row that does not exist has no
    mov byte [np_hitset], 0         ; answer, and the caller keeps its caret
    mov byte [np_wantset], 0

    mov di, [np_tx]                 ; DI = pen x
    mov word [np_i], 0
    mov ax, [np_top]                ; np_row is the VISIBLE row, so the note's
    neg ax                          ; first row sits np_top rows ABOVE the top
    mov [np_row], ax                ; of the view and its index is NEGATIVE
    mov word [np_rowh], 0           ; (SPEC.md 27.7). Everything that indexes
                                    ; an array by it already tests UNSIGNED
                                    ; against a limit, and a negative word
                                    ; read as unsigned is past all of them
    mov byte [np_wstart], 1         ; whatever this walk starts on begins a
                                    ; word as far as it can tell (SPEC.md
                                    ; 27.11). A seeded walk may in fact resume
                                    ; inside one, and cannot be wrong for it:
                                    ; a seed is always a row START, where the
                                    ; word test never breaks anyway
    mov bx, [np_len]                ; BX = characters remaining
    xor si, si                      ; ES:SI = the document (SPEC.md 27.6), and
    mov es, [np_dseg]               ; ES survives every callee below: np_rstart
                                    ; and np_rflush push it around their own
    cmp byte [np_resume], 0         ; ...or start part-way in: at the caret's
    je .seeded                      ; own row for a keystroke (SPEC.md 27.4),
    mov ax, [np_sdi]                ; or at any row np_rows named (SPEC.md
    cmp ax, [np_len]                ; 27.5). Everything before the seed laid
    ja .seeded                      ; out identically last time and cannot have
    mov [np_i], ax                  ; moved, so its signatures still stand and
    add si, ax                      ; its pixels are on screen
    sub bx, ax
    mov ax, [np_sdr]
    mov [np_row], ax
.seeded:
    mov ax, [np_row]                ; BP = np_ty + 8*row for BOTH starts, and
    mov cl, 3                       ; the shift is a signed multiply: a row
    shl ax, cl                      ; above the view gets a pen y above np_ty,
    add ax, [np_ty]                 ; which np_rflush refuses to draw at
    mov bp, ax
    call np_rstart                  ; BP is this row's y; the buffer starts blank

.loop:
    ; --- the wrap rule, applied to the cell this index will occupy ---------
    mov cx, di
    add cx, 7
    cmp cx, [np_rgt]
    ja .over                        ; the cell itself does not fit...
    call np_wordfit                 ; ...or the WORD that begins here would run
    jnc .fits                       ; off the row (SPEC.md 27.11)
    jmp short .wrap
.over:
    call np_hangsp                  ; a trailing SPACE hangs past the margin
    jc .fits                        ; rather than indent the row below it
.wrap:
    call np_rflush                  ; the row that is ENDING, before np_nextrow
    mov di, [np_tx]                 ; moves [np_row] off it
    add bp, 8
    call np_nextrow                 ; the pen changed rows, so the signature
    call np_bpush                   ; being accumulated belongs to the old one
    call np_rstart                  ; ...and in break mode the rows below have
    mov ax, [np_row]                ; to be pushed down before it is drawn
    cmp ax, [np_lastrow]            ; SIGNED (SPEC.md 27.7): np_row is a
    jle .fits                       ; VISIBLE row and is negative above the
    jmp .stop                       ; view, which unsigned reads as past every
                                    ; limit - so the walk would stop before it
                                    ; had drawn anything at all
.fits:
    call np_ask                     ; the queries, at the settled pen
    cmp byte [np_draw], 0
    je .body
    call np_carets                  ; ...and the caret, if this is its index
.body:
    cmp byte [np_bstop], 0          ; the visual break (SPEC.md 27.3): the
    je .nostop                      ; walk ENDS at the caret, because the
    mov ax, [np_i]                  ; whole point is that the note below it
    cmp ax, [np_cur]                ; is not being laid out at all
    jne .nostop
    mov byte [np_rowsok], 0         ; ...which leaves np_rows stale below the
    jmp .donebrk                    ; caret: the note MOVED and this walk did
.nostop:                            ; not rewrite where. Near: .done is past a
                                    ; short jump's reach
    test bx, bx
    jz .done                        ; the index past the last character: the
                                    ; queries have seen it, and there is no
                                    ; character to draw
    es lodsb                        ; DF=0 per SPEC.md 1; the override is what
    dec bx                          ; makes the note a heap claim and not bss
    inc word [np_i]
    cmp al, 13
    jne .glyph
    mov byte [np_wstart], 1         ; a line break is a break opportunity like
                                    ; a space (SPEC.md 27.11)
    call np_rflush                  ; same as the wrap above: flush before
    mov di, [np_tx]                 ; np_nextrow moves off this row
    add bp, 8                       ; newline: carriage return + line feed,
    call np_nextrow                 ; and it occupies no cell - so it is not
    call np_rstart                  ; folded into either row's signature, and
    mov ax, [np_row]                ; the pixels of the row it ends are the
    cmp ax, [np_lastrow]            ; same with it and without it. Signed, for
    jle .loop                       ; the reason at the wrap above
    jmp .stop
.glyph:
    mov byte [np_wstart], 0         ; the next index is mid-word...
    cmp al, ' '
    jne .wsdone
    mov byte [np_wstart], 1         ; ...unless this is the space that ended
.wsdone:                            ; one (SPEC.md 27.11)
    push ax                         ; fold it in whatever this pass is for:
    xor ah, ah                      ; the pass that COMPUTES the signatures is
    push ax                         ; a measure pass, so this cannot hang off
    mov ax, [np_i]                  ; np_draw.
    dec ax                          ; ...and a SELECTED cell folds differently
    call np_selq                    ; (SPEC.md 27.8), because the inversion is
    pop ax                          ; pixels like the glyph is and moving the
    jnc .nfsel                      ; selection has to dirty the row it left
    mov ah, 0x80                    ; as well as the one it arrived at. POP
.nfsel:                             ; touches no flags, so the answer survives
    call np_fold
    pop ax
    cmp byte [np_draw], 0
    je .advance
    mov cx, bp                      ; vertical clip: drop rows that overflow,
    add cx, 7                       ; but keep advancing the pen so every
    cmp cx, [np_bot]                ; position below stays true
    ja .advance
    call np_rowdirty                ; ...and drop the rows whose pixels this
    jc .advance                     ; redraw already knows are right
    push bx                         ; into the row buffer at this pen's CELL -
    mov bx, di                      ; np_rflush draws the whole row at once
    sub bx, [np_tx]
    push cx
    mov cl, 3
    shr bx, cl
    pop cx
    cmp bx, [np_rcols]
    jae .nocell                     ; past the band: the wrap rule above means
    mov [np_rbuf+bx], al            ; this cannot normally happen, and a
    push ax                         ; clamped np_rcols is the case where it can
    push dx
    mov ax, [np_i]
    dec ax
    call np_selq                    ; selected NOW...
    mov dl, 0
    jnc .n1
    mov dl, 1
.n1:
    call np_selqo                   ; ...and selected ON SCREEN (SPEC.md 27.8.2)
    mov dh, 0
    jnc .n2
    mov dh, 1
.n2:
    or dl, dl
    jz .nosel
    mov ax, bx                      ; inside the selection: widen the span
    call np_selfold                 ; np_rflush inverts (SPEC.md 27.8)
.nosel:
    cmp dl, dh
    je .noxf
    mov ax, bx                      ; ...and its inversion has to CHANGE, which
    call np_xfold                   ; is the only thing a drag actually owes
.noxf:
    pop dx
    pop ax
.nocell:
    pop bx
.advance:
    add di, 8
    jmp .loop                       ; near: the cell-buffer store above pushed
                                    ; the loop body past a short jump's reach

.done:
    mov ax, [np_row]                ; how tall the NOTE is, which is what the
    add ax, [np_top]                ; scroll bar's thumb is a fraction of
    inc ax                          ; (SPEC.md 27.7). HERE, not below: .blank
    mov [np_drows], ax              ; walks np_row on past the last row the note
    mov byte [np_hdirty], 0         ; actually has, and .donebrk is the visual
                                    ; break's end - a walk that stopped at the
                                    ; caret has not seen the note's height
.donebrk:
    call np_rflush                  ; the last row the walk was accumulating

    ; ...and then every row BELOW it that this redraw still owns. A note that
    ; shrank - a backspace that pulled a wrapped line back up, a deleted
    ; newline - leaves rows the walk no longer reaches, and their old pixels
    ; are still on screen. The band fill used to erase them for free, because
    ; it covered dr0..dr1 whether or not the walk got there; drawing row by row
    ; does not, so they are blanked explicitly. Without this a deletion left
    ; the row's last state behind, caret included, which is exactly what the
    ; first test of this rewrite showed.
    cmp byte [np_draw], 0
    je .sigpad
.blank:
    mov ax, [np_row]
    cmp ax, [np_dr1]
    jae .sigpad                     ; past what this redraw was asked for
    cmp ax, [np_vrows]
    jae .sigpad                     ; ...or past the content
    add bp, 8
    call np_nextrow
    call np_rstart                  ; an empty row at this y: np_rflush's own
    call np_rflush                  ; dirty and fits tests still gate it
    jmp short .blank

.sigpad:
    mov ax, [np_row]                ; a walk that ran to its natural end is
    inc ax                          ; what np_rows describes, rows 0..np_row
    js .norows2                     ; (SPEC.md 27.5) - a STOPPED one leaves
    mov [np_rowsn], ax              ; the tail of the table stale, and one that
    mov byte [np_rowsok], 1         ; ended ABOVE the view described none of it
    jmp short .padchk
.norows2:
    mov word [np_rowsn], 0
    mov byte [np_rowsok], 0
.padchk:
    cmp byte [np_sigup], 0
    je .fin
.pad:
    call np_nextrow                 ; flush the row the walk ended on, and then
    mov ax, [np_row]                ; every visible row after it: a note that
    cmp ax, [np_vrows]              ; SHRANK leaves rows behind that are no
    jb .pad                         ; longer reached, and their old signature
                                    ; is exactly what says they must be erased
    jmp short .fin                  ; ...and NOT into .stop: that path pads
                                    ; np_row past the note's last row without
                                    ; np_rstart, so the entries it would claim
                                    ; below were never written
.stop:                              ; the np_lastrow stop leaves np_rows ALONE:
                                    ; a walk that ends early is one whose
                                    ; caller knows nothing below it moved, so
                                    ; the entries past it are still what the
                                    ; last full pass wrote
    ; It cannot know the note's HEIGHT either - but it does know a LOWER BOUND,
    ; and raising [np_drows] to it is what keeps np_scrollmax from clamping the
    ; view short of a caret that has just moved past the old bottom. Never
    ; LOWERED here: a note that shrank keeps a slightly generous scroll range
    ; until something walks it whole, and the cost of that is one blank row at
    ; the end rather than a caret nobody can see (SPEC.md 27.7)
    push ax
    mov ax, [np_row]                ; the ABSOLUTE row this walk stopped on,
    add ax, [np_top]                ; and the index that row begins at: the
    mov [np_stoprow], ax            ; pair a resumable walk picks up from
    push ax                         ; (SPEC.md 27.7.3). np_rstart has already
    mov ax, [np_i]                  ; run for this row, so [np_i] is its FIRST
    mov [np_stopi], ax              ; character and not the last of the row
    pop ax                          ; above. Published on EVERY bounded stop
                                    ; and kept only by np_height, on the walk
                                    ; it issued itself - which is safe because
                                    ; it reads them under the lock with
                                    ; nothing between the call and the read
    inc ax
    cmp ax, [np_drows]
    jbe .nolb
    mov [np_drows], ax
.nolb:
    ; ...and np_rows DOES describe what it passed, as long as this walk started
    ; at the top of the view rather than at a seed part-way down. Every row it
    ; stood on went through np_rstart, so the table is good up to np_row - and
    ; without saying so, a bounded np_paint left [np_rowsok] clear and every
    ; caret key after it fell back to walking from index 0 (SPEC.md 27.5).
    cmp byte [np_resume], 0
    jne .norn                       ; a RESUMED walk skipped the rows above its
    mov ax, [np_row]                ; seed, and theirs are the last full pass's
    cmp ax, [np_vrows]
    jbe .rncap
    mov ax, [np_vrows]
.rncap:
    cmp ax, NP_MAXROWS
    jbe .rnok
    mov ax, NP_MAXROWS
.rnok:
    or ax, ax
    jle .norn                       ; it stopped above the view: it described
    mov [np_rowsn], ax              ; none of the table
    mov byte [np_rowsok], 1
.norn:
    pop ax
.fin:
    mov word [np_lastrow], 0x7FFF   ; ONE-SHOT: a caller that forgets to set it
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
; np_hangsp - may the cell at this pen hang past the right edge?
; in:  ES:SI = the document at [np_i], BX = characters left, DI = pen x
; out: CF = 1 = do not wrap, let it hang; preserves every register
;
; Only a SPACE, and only one cell's worth. Word wrap ends a row after the last
; word that fits, and the space that follows that word then has nowhere to go:
; the cell rule sends it to the next row, where it is an indent nobody typed -
; one row in [np_rcols], often enough to look like a mistake in a narrow
; window. Every text editor hangs it past the margin instead, and here that
; costs nothing, because the cell is beyond [np_rcols] and so is dropped by
; the row buffer's own bound rather than drawn: a space paints nothing anyway.
;
; The overshoot is capped at that one cell, which is what keeps a run of
; spaces from walking the pen out of the window - and out of a 16-bit DI.
; -----------------------------------------------------------------------------
np_hangsp:
    or bx, bx
    jz .no                          ; the end of the note: nothing to hang
    push ax
    mov ax, [np_rgt]
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
; np_wordfit - would the word beginning at this index run off the row?
; in:  ES:SI = the document at [np_i], BX = characters left, DI = pen x,
;      [np_wstart] = 1 if this index begins a word
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
; [np_rcols] is a whole row, and a word longer than THAT can never be helped
; by breaking - it has to be split by the cell rule wherever it stands, and
; forcing a wrap for it would put the pen at the left margin and ask the same
; question again, forever. Between the two thresholds is the only case there
; is.
;
; A word already AT the left margin is the same guard doing second duty and
; needs no test of its own: there R equals [np_rcols], so "longer than R" and
; "longer than a row" are one question and the answer is always "do not
; break".
; -----------------------------------------------------------------------------
np_wordfit:
    cmp byte [np_wstart], 0
    je .no                          ; mid-word: asked and answered at its first
    or bx, bx                       ; character
    jz .no                          ; nothing left to measure
    push ax
    push bx
    push cx
    push dx
    push si

    mov ax, [np_rgt]
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
    jcxz .p2
    inc si
    dec bx
    dec cx
    jmp short .p1

.p2:                                ; --- no. Would a whole row hold it? -----
    mov cx, [np_rcols]
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
; np_ask - answer the walk's queries at the settled pen (module-internal)
; in:  DI/BP = the pen, [np_i] = this index, SI -> its character, BX = the
;      characters left (0 = we are past the end)
; out: the np_curx/np_cury/np_hiti/np_wanti fields updated; preserves all
;
; The "+4" is the half-cell rule every text editor uses: a click in the left
; half of a character puts the caret before it, one in the right half after.
; A NEWLINE is excluded from the "after" half, and that is what makes End
; land before the line break instead of at the start of the next line - the
; character occupies no cell, so there is no right half of it to click in.
; -----------------------------------------------------------------------------
np_ask:
    push ax
    push cx
    mov ax, [np_i]
    cmp ax, [np_cur]
    jne .hit
    mov [np_curx], di
    mov [np_cury], bp
    mov byte [np_curseen], 1
    push ax                         ; the row the caret is on starts HERE, and
    mov ax, [np_ckpc]               ; that is the only state the next keystroke
    mov [np_ckpi], ax               ; needs to skip everything above it
    mov ax, [np_ckpcr]              ; (SPEC.md 27.4)
    mov [np_ckpr], ax
    mov byte [np_ckok], 1
    pop ax
    push ax                         ; AX is [np_i] and .hit below still wants
    mov ax, di                      ; it. The caret is pixels on this row too,
    xor ax, 0x5A5A                  ; and folding it in HERE - between the
    call np_fold                    ; glyph before it and the one after - is
    pop ax                          ; what makes moving it dirty both rows.
                                    ; The xor keeps a column from folding the
                                    ; way a character code would
.hit:
    mov cx, [np_hity]
    cmp cx, 0xFFFF
    je .want
    cmp cx, bp                      ; the click row is this pen row?
    jb .want
    mov cx, bp
    add cx, 7
    cmp cx, [np_hity]
    jb .want
    cmp byte [np_hitset], 0
    jne .hit2
    mov [np_hiti], ax               ; the first index on the row, until a
    mov byte [np_hitset], 1         ; later cell claims it
.hit2:
    mov cx, di
    add cx, 4
    cmp [np_hitx], cx
    jb .want                        ; the left half: the caret goes before it
    call np_isnl
    jc .want                        ; a newline has no right half
    inc ax
    mov [np_hiti], ax
    dec ax
.want:
    cmp word [np_wanty], 0xFFFF
    je .out
    mov cx, [np_wanty]
    cmp cx, bp
    jne .out
    cmp byte [np_wantset], 0
    jne .want2
    mov [np_wanti], ax
    mov byte [np_wantset], 1
.want2:
    mov cx, di
    add cx, 4
    cmp [np_wantx], cx
    jb .out
    call np_isnl
    jc .out
    inc ax
    mov [np_wanti], ax
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_isnl - is the character at this index a newline (or past the end)?
; in:  BX = characters remaining, SI -> the character
; out: CF=1 = yes, or there is no character here; preserves all registers
; -----------------------------------------------------------------------------
np_isnl:
    push ax
    test bx, bx
    jz .yes
    mov al, [si]
    cmp al, 13
    je .yes
    clc
    jmp short .out
.yes:
    stc
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_carets - draw the caret when the walk is standing on its index
; in:  DI/BP = the pen, [np_i], [np_cur]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_carets:
    push ax
    push bx
    push cx
    push dx
    cmp byte [np_selon], 0
    jne .out                        ; a selection REPLACES the caret, which is
                                    ; the Macintosh rule and is also the only
                                    ; honest one here: a 1px black bar inside
                                    ; an inverted band is invisible, so drawing
                                    ; it would cost a line and show nothing
    mov ax, [np_i]
    cmp ax, [np_cur]
    jne .out
    mov cx, bp
    add cx, 7
    cmp cx, [np_bot]
    ja .out                         ; its row does not fit: no caret
    call np_rowdirty                ; ...nor does a row this pass is not
    jc .out                         ; redrawing (SPEC.md 27.2)
    mov [np_rcx], di                ; BANKED, not drawn: the row's font_run has
                                    ; not happened yet and would paint over it,
                                    ; so np_rflush puts it back afterwards
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
; pixels, because on any row the k-th glyph is always at [np_tx] + 8k. It is a
; hash and not a proof, the same trade the Task Manager's rows make (SPEC.md
; 28): a collision leaves one row stale until its content moves again.
;
; The caret is part of the signature and has to be. Moving it off a row has to
; dirty that row, or it stays drawn there.
;
; A redraw is then two walks. The first measures, folds, compares against the
; stored signatures and widens [np_dr0]..[np_dr1] - a RANGE, not a bitmap,
; because the interesting cases are all contiguous and a range needs no
; indexing and turns the erase into ONE fill. The second draws, and skips
; every row outside it. If the range comes back empty nothing is drawn at all.
; =============================================================================

; -----------------------------------------------------------------------------
; np_fold - fold AX into the row being accumulated
; in:  AX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_fold:
    push bx
    mov bx, [np_rowh]
    rol bx, 1                       ; rotate then add, so a transposition is
    add bx, ax                      ; not invisible
    mov [np_rowh], bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_rstart - begin accumulating a row: BP is its y, the buffer goes to spaces
; preserves all registers
;
; SPACES and not zeros. font_run paints a space as background on its fast path
; - the glyph's rows are all clear, so the mask leaves the background byte -
; which is what makes one run erase the whole band as well as letter it. That
; is the entire reason this rewrite needs no fill: the padding IS the erase.
; -----------------------------------------------------------------------------
np_rstart:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov [np_rby], bp
    mov word [np_rcx], 0xFFFF
    mov word [np_rs0], 0xFFFF       ; ...and no inverted cells yet either
    mov word [np_rs1], 0xFFFF
    mov word [np_xs0], 0xFFFF       ; ...nor any whose inversion must change
    mov word [np_xs1], 0xFFFF
    mov ax, [np_i]                  ; where this row STARTS, banked as the
    mov [np_ckpc], ax               ; checkpoint candidate: np_ask promotes it
    mov ax, [np_row]                ; the moment the walk stands on the caret
    mov [np_ckpcr], ax              ; (SPEC.md 27.4)
    call np_xnote                   ; ...and offered to the row index, which is
                                    ; the same fact again for a row OUTSIDE the
                                    ; view - one compare unless it is wanted
                                    ; (SPEC.md 27.13)
    cmp ax, NP_MAXROWS              ; ...and into np_rows, which is the same
    jae .norow                      ; fact for every row rather than for the
    shl ax, 1                       ; caret's (SPEC.md 27.5)
    mov di, ax
    mov ax, [np_i]
    mov [di+np_rows], ax
.norow:
    mov di, np_rbuf
    mov cx, [np_rcols]
    mov al, ' '
    rep stosb
    mov byte [di], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_rflush - draw the accumulated row: ONE opaque font_run, then its caret
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
; would paint over it. np_carets banks its x instead of drawing.
;
; Three things this must not draw: a row of a measure pass, a row this redraw
; already knows is right (np_rowdirty, SPEC.md 27.2), and a row whose pixels
; fall past the content bottom - all three the same tests the per-character
; draw used to make, moved up to the row.
; -----------------------------------------------------------------------------
np_rflush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [np_draw], 0
    je .out
    call np_rowdirty
    jc .out                         ; a row this redraw already knows is right
    mov ax, [np_rby]
    cmp ax, [np_ty]
    jb .out                         ; ABOVE the view: scrolled off the top
    add ax, 7                       ; (SPEC.md 27.7), and the unsigned tests
    cmp ax, [np_bot]                ; elsewhere cannot see this one, because a
    ja .out                         ; row a little above np_ty has an ordinary
                                    ; small y. Below: it does not fit, the pen
                                    ; still
                                    ; advanced, so every position below is true
    cmp word [np_rcols], 0
    je .caret

    ; --- ONLY THE INVERSION MOVED (SPEC.md 27.8.2) -------------------------
    ; A drag changes no character anywhere. What it changes is which cells are
    ; inverted, and XOR is exactly the operation for that - so flip the cells
    ; whose selected-ness differs from the screen's and letter NOTHING. The
    ; row's glyphs are already correct and re-drawing them to invert them was
    ; costing a full row of font_run per dirty row per pass.
    ;
    ; Gated on a selection existing at BOTH ends, which is what guarantees no
    ; caret is drawn either before or after (np_carets returns early while one
    ; is up): a caret bar sits on top of a glyph, so erasing one needs that
    ; cell lettered again and this path draws no cells.
    cmp byte [np_selonly], 0
    je .normal
    cmp byte [np_selon], 0
    je .normal
    cmp byte [np_oselon], 0
    je .normal
    mov ax, [np_xs0]
    cmp ax, 0xFFFF
    je .cache                       ; this row's inversion is already right
    mov cx, [np_xs1]
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_tx]                 ; x1
    push ax
    mov ax, cx
    inc ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_tx]
    dec ax
    mov cx, ax                      ; x2
    pop ax
    mov bx, [np_rby]
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_XOR_FILL
    jmp .cache                      ; np_prow still describes the screen -
                                    ; not one character moved - but .cache
                                    ; owes np_prs0/np_prs1 the new span
.normal:

    mov word [np_fcc], 0xFFFF       ; this row's caret column, if it has one
    mov ax, [np_rcx]
    cmp ax, 0xFFFF
    je .span
    sub ax, [np_tx]
    mov cl, 3
    shr ax, cl
    mov [np_fcc], ax
.span:
    mov word [np_flo], 0            ; the default span is the whole row
    mov ax, [np_rcols]
    dec ax
    mov [np_fhi], ax
    mov ax, [np_row]
    cmp ax, [np_prowi]
    jne .draw                       ; not the cached row: nothing to diff

    mov word [np_flo], 0xFFFF       ; --- the delta, cell by cell -----------
    mov word [np_fhi], 0xFFFF
    xor bx, bx
.dl:
    cmp bx, [np_rcols]
    jae .dfold
    mov al, [np_rbuf+bx]
    cmp al, [np_prow+bx]
    je .dn
    cmp word [np_flo], 0xFFFF
    jne .dhi
    mov [np_flo], bx
.dhi:
    mov [np_fhi], bx
.dn:
    inc bx
    jmp short .dl
.dfold:
    mov ax, [np_prcc]               ; the caret's cells count as changed at
    call np_fold1                   ; both ends: the one it left has to lose
    mov ax, [np_fcc]                ; its bar, and the one it arrived at has
    call np_fold1                   ; to get one
    mov ax, [np_rs0]                ; ...and so does the SELECTION, for exactly
    cmp ax, [np_prs0]               ; the same reason and only when it MOVED:
    jne .selchg                     ; a row whose inverted span is unchanged
    mov ax, [np_rs1]                ; has the right pixels already, and folding
    cmp ax, [np_prs1]               ; it in unconditionally would redraw every
    je .seldone                     ; selected row on every pass
.selchg:
    mov ax, [np_prs0]               ; the union of the two spans, which
    call np_fold1                   ; contains every cell whose inverted-ness
    mov ax, [np_prs1]               ; changed. Wider than the difference and
    call np_fold1                   ; very much simpler; a selection is a
    mov ax, [np_rs0]                ; static thing, so this runs once as it
    call np_fold1                   ; arrives and once as it leaves
    mov ax, [np_rs1]
    call np_fold1
.seldone:
    cmp word [np_flo], 0xFFFF
    je .cache                       ; nothing moved: draw NOTHING

.draw:
    cmp byte [np_clean], 0          ; the band is known blank (a full repaint
    je .draw2                       ; white-filled it, SPEC.md 27.2), so the
    mov cx, [np_flo]                ; padding has nothing to erase and the run
    mov ax, [np_rs1]                ; stops at the last real character - but
    cmp ax, 0xFFFF                  ; never short of the last SELECTED one: a
    je .tf                          ; selected trailing space is drawn to be
    cmp ax, cx                      ; inverted, and trimming it away would
    jbe .tf                         ; leave a gap in the highlight
    mov cx, ax
.tf:
    mov bx, [np_fhi]                ; A fullscreen window is 90 cells wide and
.tl:                                ; a note is rarely that long: without this
    cmp byte [np_rbuf+bx], ' '      ; a repaint costs rows x width instead of
    jne .tdone                      ; characters, and on a 4.77MHz 8088 that
    cmp bx, cx                      ; is the difference between half a second
    jbe .tstop                      ; and five
    dec bx
    jmp short .tl
.tstop:
    cmp word [np_rs1], 0xFFFF       ; all blank from the floor up: nothing to
    je .cache                       ; do, unless a selection reaches here
.tdone:
    mov [np_fhi], bx
.draw2:
    mov bx, [np_fhi]                ; terminate the span and run just it: the
    inc bx                          ; row is one string, so the byte after the
    mov al, [np_rbuf+bx]            ; span has to come back afterwards
    push ax
    mov byte [np_rbuf+bx], 0
    push bx
    mov si, [np_flo]
    mov cx, si
    mov ax, cx
    mov cl, 3
    shl ax, cl
    add ax, [np_tx]
    mov cx, ax                      ; CX = x of the span's first cell
    add si, np_rbuf
    mov dx, [np_rby]
    mov al, CBLACK                  ; ink and background in one call: the erase
    mov ah, CWHITE                  ; and the letters are one decision per cell
    call OSAPI_FONT_RUN
    pop bx
    pop ax
    mov [np_rbuf+bx], al
    call np_selxor                  ; the run drew the cells upright; invert
                                    ; the selected ones it covered (SPEC.md
                                    ; 27.8). AFTER the run, for the reason the
                                    ; caret is drawn after it: the run would
                                    ; paint over an inversion made first

.cache:
    push es                         ; the span was drawn, so the screen now
    push ds                         ; shows np_rbuf: remember it, and remember
    pop es                          ; which row and where its caret is
    cld
    mov si, np_rbuf
    mov di, np_prow
    mov cx, [np_rcols]
    rep movsb
    pop es
    mov ax, [np_row]
    mov [np_prowi], ax
    mov ax, [np_fcc]
    mov [np_prcc], ax
    mov ax, [np_rs0]                ; ...and which of its cells came out
    mov [np_prs0], ax               ; inverted, or the next pass has no way to
    mov ax, [np_rs1]                ; tell that the highlight moved off a row
    mov [np_prs1], ax               ; whose characters did not

.caret:
    mov ax, [np_rcx]
    cmp ax, 0xFFFF
    je .out
    mov bx, [np_rby]                ; 1px black caret, 8 rows tall, on top of
    mov dx, bx                      ; the run that would otherwise have eaten it
    add dx, 7
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

; np_fold1 - fold column AX into [np_flo]..[np_fhi]; 0xFFFF folds nothing.
; Preserves everything.
np_fold1:
    cmp ax, 0xFFFF
    je .out
    cmp word [np_flo], 0xFFFF
    jne .lo
    mov [np_flo], ax
    mov [np_fhi], ax
    ret
.lo:
    cmp ax, [np_flo]
    jae .hi
    mov [np_flo], ax
.hi:
    cmp ax, [np_fhi]
    jbe .out
    mov [np_fhi], ax
.out:
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
;  4. It needs [np_tx] on a multiple of 8, because OSAPI_GFX_SCROLL is
;     byte-column granular on every adapter. OSAPI_WM_SNAP guarantees that on
;     EVERY adapter now (SPEC.md 11.94 - it was mono-only, and VGA turned out
;     to gain more from alignment than mono does), so the coin flip this note
;     used to describe on VGA is gone and rule 2's CPU test is the only gate
;     left. On a window too wide to snap the alignment still fails, the break
;     does not engage and the reflow is what happens. That is a FACT the code
;     can test, not a guess (SPEC.md 47 rule 3).
; =============================================================================

; -----------------------------------------------------------------------------
; np_scroll - move row AX and everything below it down one row
; in:  AX = the first row index to move, np_bounds already run, gfx lock held
; out: CF = OSAPI_GFX_SCROLL's answer; preserves all registers
;
; The x span is the whole content width rounded IN to byte columns. [np_tx] is
; a multiple of 8 (the caller checked) and the left margin is NP_MARGIN = 8,
; so x1 is the content's own left edge; x2+1 rounds the content's right edge
; down, which can only ever drop part of the <8px tail past the last cell -
; the band no glyph reaches.
; -----------------------------------------------------------------------------
np_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]
    mov bx, ax                      ; BX = y1
    mov dx, [np_bot]                ; DX = y2
    mov ax, [np_tx]
    sub ax, NP_MARGIN               ; AX = x1
    mov cx, [np_rgt]
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
; np_bpush - np_walk reached a new row while the break is up: push the note
;            below it down to make room
; in:  [np_row] = the row the pen just moved ONTO; gfx lock held
; out: nothing; preserves all registers
;
; Called from np_walk's wrap site, AFTER the row that was ending has been
; flushed and BEFORE the new one starts - the only moment at which the row
; above is finished and the row below has not been touched.
;
; A refusal is the band below the caret being shorter than the row we are
; asking to insert, which is what happens when the caret reaches the bottom
; of the window. Nothing was moved, so the screen is still consistent with
; the note as far as this row; the caller settles.
; -----------------------------------------------------------------------------
np_bpush:
    cmp byte [np_bmode], 0
    je .out
    cmp byte [np_draw], 0           ; a measure pass moves no pixels
    je .out
    push ax
    mov ax, [np_row]
    cmp ax, [np_bcrow]
    jbe .pop                        ; still on the break's own row
    call np_scroll
    jc .fail
    mov ax, [np_row]
    mov [np_bcrow], ax
    mov word [np_prowi], 0xFFFF     ; the vacated row holds a copy of the one
    mov byte [np_didpush], 1        ; above it: nothing the delta cache knows
    jmp short .pop
.fail:
    mov byte [np_bfail], 1
.pop:
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; np_brkdraw - one keystroke while the break is up
; in:  SI = window ptr, gfx lock held, [np_ckok] set
; out: nothing; clobbers what a callback may
;
; ONE walk, from the checkpoint to the caret and no further - so a keystroke
; costs the caret's own row and nothing else, whatever the note weighs. No
; signature pass: the rows below the caret are not being laid out, so there
; is nothing to compare them against.
; -----------------------------------------------------------------------------
np_brkdraw:
    push ax
    push bx
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    mov word [np_dr1], 0            ; np_walk's blank loop must not run: it
                                    ; erases rows this pass has no opinion on
    mov byte [np_draw], 1
    mov byte [np_sigup], 0
    mov byte [np_clip], 0
    call np_seedck
    mov byte [np_bstop], 1
    mov byte [np_didpush], 0
    mov byte [np_bfail], 0
    call np_walk
    mov byte [np_resume], 0
    mov byte [np_bstop], 0
    cmp byte [np_bfail], 0
    jne .settle
    cmp byte [np_didpush], 0
    je .out
    mov bx, si                      ; a push scrolled the whole band, grow box
    call OSAPI_WM_GROW              ; included (SPEC.md 11.1)
    jmp short .out
.settle:
    call np_reconcile               ; no room left below: show the note
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_brktry - would this keystroke be cheaper as a break? then take it
; in:  SI = window ptr, pass 1 has run ([np_dr1], [np_curx]/[np_cury] valid),
;      the caller has already checked [np_brkok] and that this is a plain
;      keystroke at the caret; gfx lock held
; out: CF = 0 the break took over AND drew - the caller is done; CF = 1 it
;      did not, and the caller reflows as before. Clobbers AX/BX/CX/DX/DI
; -----------------------------------------------------------------------------
np_brktry:
    push di
    test word [np_tx], 7
    jnz .no                         ; rule 4: the scroll is byte-column granular
    mov ax, [np_cury]
    sub ax, [np_ty]
    mov cl, 3
    shr ax, cl
    mov di, ax                      ; DI = the caret's row
    mov ax, [np_dr1]
    sub ax, di
    jbe .no                         ; nothing below the caret's row moved
    mul word [np_rcols]             ; DX:AX = the cells this reflow would cost
    or dx, dx                       ; below the caret. It cannot overflow -
    jnz .yes                        ; 60 rows by 90 cells is 5,400 - but a
    cmp ax, NP_BRK_CELLS            ; multiply writes DX and saying so is
    jb .no                          ; cheaper than remembering it cannot
.yes:
    mov ax, di
    inc ax
    cmp ax, [np_vrows]
    jae .no                         ; no row below to push the note into
    mov ax, [np_curx]
    sub ax, [np_tx]
    mov cl, 3
    shr ax, cl
    or ax, ax
    jz .no                          ; the caret ended at column 0, which for an
                                    ; insert means the keystroke WRAPPED it
                                    ; onto a fresh row - there is no prefix to
                                    ; keep and no tail to push. The next
                                    ; keystroke will ask again

    mov bx, [np_ecol]               ; the caret's column BEFORE the edit, which
    xor ah, ah                      ; is exactly the prefix the scrolled copy
    mov al, [np_eext]               ; below will duplicate - plus whatever the
    add bx, ax                      ; edit took off that row (Delete: one cell)
    cmp bx, [np_rcols]
    ja .no                          ; a stale np_ecol cannot reach past the band

    push bx                         ; the caret bar is about to be scrolled
    push dx                         ; down with everything else, and it would
    mov ax, [np_ecol]               ; land in the middle of the tail. Erase it
    push cx                         ; where it STANDS, which is the column it
    mov cl, 3                       ; was at before this edit and not the one
    shl ax, cl                      ; it is at now - this row is redrawn whole
    pop cx                          ; a moment from now anyway
    add ax, [np_tx]
    mov bx, [np_cury]
    mov dx, bx
    add dx, 7
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    pop dx
    pop bx

    mov ax, di
    call np_scroll                  ; the caret's row and everything below it
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
    add ax, [np_ty]
    mov bx, ax                      ; BX = y1
    mov dx, ax
    add dx, 7                       ; DX = y2
    pop ax                          ; AX = cells to blank
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_tx]
    dec ax
    mov cx, ax                      ; CX = x2
    mov ax, [np_tx]                 ; AX = x1
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.nodup:
    mov byte [np_bmode], 1
    mov [np_bcrow], di
    mov [np_borig], di              ; the first row the reconcile owes a repaint
    mov word [np_prowi], 0xFFFF     ; the caret's row was just scrolled away
    call np_brkdraw
    mov bx, si                      ; the scroll above dragged the grow box
    call OSAPI_WM_GROW              ; down with everything else
    call np_hire                    ; nothing settles the break but the worker
    clc
    pop di                          ; POP does not touch flags
    ret
.no:
    stc
    pop di
    ret

; -----------------------------------------------------------------------------
; np_reconcile - take the break down and show the note
; in:  SI = window ptr, gfx lock held (UI task or the worker)
; out: nothing; clobbers what a callback may
;
; INCREMENTAL, and it can be: the break only ever scrolled rows [np_borig] and
; below, so everything above it is still the note and still has the signature
; that says so. The band from np_borig down is filled white and redrawn whole,
; and np_clean is what keeps that from costing rows x width - with the band
; known blank a row's run stops at its last real character instead of padding
; to the edge to erase with (SPEC.md 27.2).
; -----------------------------------------------------------------------------
np_reconcile:
    push ax
    push bx
    push cx
    push dx
    call np_bounds
    mov byte [np_bmode], 0
    mov byte [np_resume], 0
    mov word [np_hity], 0xFFFF      ; pass 1: the signatures, over the whole
    mov word [np_wanty], 0xFFFF     ; note, because they have been standing
    mov word [np_dr0], 0xFFFF       ; still since the break went up
    mov word [np_dr1], 0
    mov byte [np_draw], 0
    mov byte [np_sigup], 1
    mov byte [np_clip], 0
    call np_walk
    mov ax, [np_vrows]
    or ax, ax
    jz .done
    dec ax
    mov [np_dr1], ax
    mov ax, [np_borig]
    cmp ax, [np_dr1]
    ja .done
    mov [np_dr0], ax                ; the band is everything the break moved,
                                    ; whatever the signatures think: no
                                    ; signature describes a fiction
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]
    mov bx, ax                      ; BX = y1
    mov dx, [np_bot]                ; DX = y2
    mov ax, [np_tx]
    sub ax, NP_MARGIN               ; AX = x1, the content's own left edge
    mov cx, [np_rgt]                ; CX = x2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [np_prowi], 0xFFFF     ; the fill erased whatever the cache knew
    mov byte [np_clean], 1
    mov byte [np_draw], 1
    mov byte [np_sigup], 0
    mov byte [np_clip], 1
    call np_walk
    mov byte [np_clip], 0
    mov byte [np_clean], 0
    mov bx, si
    call OSAPI_WM_GROW              ; the fill reached it (SPEC.md 11.1/27)
.done:
    call np_sigmark
    push ax
    mov ax, [np_top]                ; the reconcile draws the note as it now
    mov [np_ptop], ax               ; is, in the view it now has
    pop ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_hire - spawn the worker, once
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
;
; Lazy on purpose: a Note Pad that never breaks never costs a task slot or a
; 512-byte stack, which on a 12-slot table is worth the byte of state. A
; refusal is normal and transient (the table can be full), so nothing is
; latched and the next break asks again.
; -----------------------------------------------------------------------------
np_hire:
    push ax
    push bx
    cmp byte [np_hired], 0
    jne .out
    mov al, 1                   ; PARK-SAFE (SPEC.md 66.5.4): no register and
    call OSAPI_MEM_PARKSAFE     ; no stack slot here holds a pointer derived
                                ; from the note or the undo arena across a
                                ; call that can yield. The worker takes the
                                ; lock at .go holding only a window pointer;
                                ; np_selpace - the ONLY other lock site, and
                                ; the one both drag loops spin in - is entered
                                ; with indices alone (np_dragsel's BX is a
                                ; character index, np_dragmove's a flag). The
                                ; walk at np_measure DOES hold ES = the note
                                ; across its callees, and cannot be caught by
                                ; this: it runs under a lock the caller
                                ; already holds, so it never blocks in one.
                                ; It is a WIDENING and not what makes these
                                ; two claims movable (SPEC.md 66.5.7.2): this
                                ; worker sleeps NP_WTICKS and so reaches
                                ; OSAPI_TASK_ALIVE inside INST_PARKW on its
                                ; own, measured. Tracker's cannot, which is
                                ; the case 66.5.4 was written for
    mov ax, np_worker
    mov bx, [np_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [np_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_worker - THE background task (SPEC.md 20.6): it settles the break
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
; W_PAINT, and np_paint draws the note and clears the break.
; -----------------------------------------------------------------------------
np_worker:
.loop:
    mov bx, [np_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4)
    mov ax, NP_WTICKS
    call OSAPI_TASK_SLEEP
    cmp byte [np_bmode], 0
    jne .idle
    cmp byte [np_hdirty], 0         ; ...or a height to recount, which is the
    jne .idle                       ; other thing worth waking up for
    cmp byte [np_uopen], 0          ; ...or an edit group whose half-second is
    jne .idle                       ; nearly up (SPEC.md 27.9)
    cmp byte [np_fcdirty], 0        ; ...or a match count somebody is waiting
    je .loop                        ; to see (SPEC.md 27.10)
.idle:
    call OSAPI_WM_TOP               ; BX = frontmost visible, 0 = none
    cmp bx, [np_win]
    jne .go
    call OSAPI_GET_TICKS
    sub ax, [np_ktick]              ; modular, so a wrapping tick counter is
    cmp ax, NP_IDLE                 ; not a special case (SPEC.md 8)
    jb .loop
.go:
    call OSAPI_GFX_LOCK
    cmp byte [np_sowed], 0          ; a scroll whose repaint was dropped because
    je .nosowed                     ; another click was right behind it
    mov byte [np_sowed], 0          ; (SPEC.md 27.7.8). Cleared FIRST: a repaint
                                    ; that faults must not leave the debt to be
                                    ; paid again forever
    mov bx, [np_win]                ; ...and ASKED, like the three draws below
    call OSAPI_WM_OBSCURED          ; it. This one drew unconditionally: covered,
    jc .nosowed                     ; it painted over the window on top of it
                                    ; (SPEC.md 11.3), and it was the one path
    mov si, [np_win]                ; that changed this window's pixels without
    call np_redraw                  ; telling the kernel - which is what the
                                    ; raise cache's promise rests on (11.96)
.nosowed:
    call np_uclose                  ; half a second without an edit is what a
                                    ; user means by ONE edit, and this is the
                                    ; clock that measures it (SPEC.md 27.9).
                                    ; UNDER the lock, because every recorder
                                    ; runs inside a callback that holds it
    mov si, [np_win]
    cmp byte [np_bmode], 0          ; re-read UNDER the lock: the UI task may
    je .height                      ; have settled it while we waited
    mov bx, [np_win]
    call OSAPI_WM_OBSCURED
    jc .height
    call np_reconcile
.height:
    ; Count the note's rows, which no other walk does any more (SPEC.md 27.7),
    ; and move the thumb if that changed it. THE COUNT is not gated on the
    ; window being visible - it is arithmetic, and a covered or hidden window's
    ; height is owed the moment it comes back - and THE DRAW is gated by the
    ; call below it, like the other three in this routine.
    ;
    ; That distinction was written here as one sentence and the drawing half
    ; was wrong (SPEC.md 11.3.1): "np_sbcheck draws only when a number moved"
    ; was offered as the reason it was safe, and a number moving is exactly
    ; what a chunked count does on a window nobody can see - NP_HCHUNK rows a
    ; pass, each raising [np_drows]. wm_obscured answered only "is anything on
    ; TOP of me", so after the close box hid this window the bar was drawn onto
    ; the bare desktop. It answers about a hidden window now, which is what the
    ; four calls here have always meant by it.
    cmp byte [np_hdirty], 0
    je .count
    call np_bounds                  ; the walk reads [np_ty]/[np_rgt], and the
    call np_hchunk                  ; window may have been resized since.
                                    ; A CHUNK of the count and not the whole of
                                    ; it (SPEC.md 27.7.3): the lock is held
                                    ; across this, so the bound on the walk is
                                    ; the bound on how long a UI action behind
                                    ; it has to wait
    mov bx, [np_win]
    call OSAPI_WM_OBSCURED
    jc .count
    call np_sbcheck
.count:
    ; The match count, which walks the whole note with the matcher and so is
    ; owed rather than paid on the keystroke that changed the pattern
    ; (SPEC.md 27.10) - the same trade np_height makes above it. UNDER the
    ; lock, because the matcher's backtrack stack is one block of bss that the
    ; UI task's own finds use too.
    cmp byte [np_fcdirty], 0
    je .unlock
    cmp byte [np_fpan], NP_FPAN_NONE
    je .nocount                     ; no panel: nobody can see the answer, so
                                    ; the debt is simply cancelled
    call np_fcount_do
    mov bx, [np_win]
    call OSAPI_WM_OBSCURED
    jc .unlock
    mov si, [np_win]
    call np_pdrawn                  ; a recount changes ONE string on screen
    jmp short .unlock
.nocount:
    mov byte [np_fcdirty], 0
.unlock:
    call OSAPI_GFX_UNLOCK
    jmp .loop

; -----------------------------------------------------------------------------
; np_nextrow - the pen moved to the next row: bank the signature it just
;              finished, and start the next one
; in:  [np_row], [np_rowh], [np_sigup]
; out: [np_row] advanced, [np_rowh] = 0; [np_dr0]/[np_dr1] widened if the row
;      changed; preserves all registers
;
; Rows past [np_vrows] are off the bottom of the content. The walk still
; visits them - every position below has to stay true - but they have no
; signature slot and no pixels, so they are counted and otherwise ignored.
; -----------------------------------------------------------------------------
np_nextrow:
    push ax
    push bx
    cmp byte [np_sigup], 0
    je .adv
    mov ax, [np_row]
    cmp ax, [np_vrows]
    jae .adv
    shl ax, 1
    mov bx, ax
    mov ax, [np_rowh]
    cmp ax, [bx+np_sig]
    je .adv                         ; same word, same pixels: leave it alone
    mov [bx+np_sig], ax
    mov ax, [np_row]
    cmp ax, [np_dr0]
    jae .hi
    mov [np_dr0], ax
.hi:
    cmp ax, [np_dr1]
    jbe .adv
    mov [np_dr1], ax
.adv:
    inc word [np_row]
    mov word [np_rowh], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_rowdirty - is the row the pen is on one this pass is redrawing?
; in:  [np_row], [np_clip], [np_dr0]/[np_dr1]
; out: CF = 1 if it must NOT be drawn; preserves all registers
; -----------------------------------------------------------------------------
np_rowdirty:
    cmp byte [np_clip], 0
    je .yes                         ; not clipping: this is a full paint
    push ax
    mov ax, [np_row]
    cmp ax, [np_dr0]
    jb .no
    cmp ax, [np_dr1]
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
; np_sigmark - record the geometry (and the toast) the signatures describe
; in:  np_bounds already run
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_sigmark:
    push ax
    call np_selmark
    mov ax, [np_tx]
    mov [np_stx], ax
    mov ax, [np_ty]
    mov [np_sty], ax
    mov ax, [np_rgt]
    mov [np_srgt], ax
    mov ax, [np_bot]
    mov [np_sbot], ax
    mov byte [np_sigok], 1
    mov byte [np_gchg], 0           ; this paint laid the note out under the
    pop ax                          ; geometry just recorded, so the view has
    ret                             ; been re-clamped against it

; -----------------------------------------------------------------------------
; np_selmark - the screen now shows THIS selection (SPEC.md 27.8.2)
; out: nothing; preserves all registers
;
; The one fact np_selqo reads. Every path that finishes a redraw sets it,
; including the ones that drew nothing: a row whose selection did not change
; is not in the dirty band, so "what the screen shows" is the live selection
; either way.
; -----------------------------------------------------------------------------
np_selmark:
    push ax
    mov ax, [np_sel0]
    mov [np_osel0], ax
    mov ax, [np_sel1]
    mov [np_osel1], ax
    mov al, [np_selon]
    mov [np_oselon], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_sigsame - do the stored signatures still describe this window?
; in:  np_bounds already run
; out: CF = 1 if they do not and the caller must repaint whole; preserves all
;
; All four tests are the layout: a resized window wraps differently, and the
; kernel white-filled its content on the way here anyway. There used to be a
; fifth and a sixth, on [np_msg] and its generation - the toast was drawn OVER
; the text and was in no row's signature, so the keystroke that retired one
; had to erase it the only way this module could, by painting the whole
; content again. The toast is the kernel's now and is in the menu bar
; (SPEC.md 59), so it is in nothing this routine describes.
; -----------------------------------------------------------------------------
np_sigsame:
    push ax
    cmp byte [np_sigok], 0
    je .no
    mov ax, [np_tx]
    cmp ax, [np_stx]
    jne .no
    mov ax, [np_ty]
    cmp ax, [np_sty]
    jne .no
    mov ax, [np_rgt]
    cmp ax, [np_srgt]
    jne .no
    mov ax, [np_bot]
    cmp ax, [np_sbot]
    jne .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; np_height - walk the whole note for [np_drows], if the note has changed
; in:  SI = window ptr, np_bounds run; out: nothing; preserves all registers
;
; The one walk that exists to answer "how many rows", which is the one
; question a bounded walk cannot answer (SPEC.md 27.7). Every OTHER walk now
; stops at the bottom of the view, because rows below it are drawn by nobody
; and the thumb is the only thing that was ever asking - so this is where the
; note's tail is paid for, and it is paid half a second after the typing
; stops rather than on every keystroke.
;
; It preserves the two query fields because np_onclick sets them BEFORE it
; gets here, and a walk consumes them.
;
; It comes in two sizes (SPEC.md 27.7.3). np_height finishes the count in one
; hold, for the one caller that needs the answer exact - a click on the bar.
; np_hchunk does NP_HCHUNK rows of it and hands the lock back, which is what
; the worker calls: the count of a 16KB note is seconds of walking, and doing
; it in one hold freezes the machine behind it with nothing on the disk and
; nothing on the glass.
;
; Chunking is legal because the gfx lock here is a mutex over the walk's
; SCRATCH and not a drawing lock - np_height writes no framebuffer, and nine of
; np_walk's ten call sites are UI callbacks that hold the lock already. So the
; hold is needed for a CHUNK and never for the COUNT, and "give up the machine
; if somebody else wants it" falls out of the release rather than needing the
; worker to ask anyone.
;
; The resume pair survives the release because WRAPPING IS DETERMINISTIC: row R
; begins at index I whoever computed it. An interleaved W_PAINT scribbles over
; the walk's in-flight scratch and cannot touch those two words. Only the note
; CHANGING invalidates them, and that goes through np_hmark, which resets them.
; -----------------------------------------------------------------------------
np_height:
    push ax
    mov ax, 0x7FFF                  ; no bound: to the last character, however
    call np_hwalk                   ; many rows that is
    pop ax
    ret

np_hchunk:
    push ax
    mov ax, [np_hi]                 ; a stale seed can only mean the note shrank
    cmp ax, [np_len]                ; without going through np_hmark; np_walk
    jbe .seedok                     ; would fall back to a full walk, and the
    mov word [np_hrow], 0           ; bound below would then be a chunk's worth
    mov word [np_hi], 0             ; of rows past where it actually starts -
.seedok:                            ; one unbounded hold, the thing being fixed
    mov ax, [np_hrow]               ; [np_lastrow] is a VISIBLE row and
    sub ax, [np_top]                ; [np_hrow] is absolute, so the bound is
    add ax, NP_HCHUNK               ; this chunk's own start plus its length
    call np_hwalk
    pop ax
    ret

; np_hwalk - the count itself, stopping after visible row AX
; in:  AX = the last visible row this pass may stand on, SI = window ptr
np_hwalk:
    cmp byte [np_hdirty], 0
    jne .go
    ret
.go:
    push ax
    push si
    mov [np_lastrow], ax
    mov ax, [np_hity]
    push ax
    mov ax, [np_wanty]
    push ax
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    mov byte [np_draw], 0
    mov byte [np_sigup], 0
    mov byte [np_clip], 0

    mov ax, [np_hi]                 ; resume where the last chunk stopped, which
    mov [np_sdi], ax                ; for the first one is index 0 of row 0 -
    mov ax, [np_hrow]               ; identical to the unseeded start np_walk
    sub ax, [np_top]                ; would make, so the top of the note needs
    mov [np_sdr], ax                ; no case of its own. np_sdr is a VISIBLE
    mov byte [np_resume], 1         ; row, so it is derived from [np_top] HERE
    call np_walk                    ; and not banked - the view may have moved
    mov byte [np_resume], 0         ; between one chunk and the next

    cmp byte [np_hdirty], 0         ; .done clears it and .stop leaves it up, so
    jne .more                       ; the flag already says which exit was taken
    mov word [np_hrow], 0           ; finished: the next count starts at the top
    mov word [np_hi], 0
    jmp short .fin
.more:
    mov ax, [np_stoprow]            ; stopped: pick this up next pass
    mov [np_hrow], ax
    mov ax, [np_stopi]
    mov [np_hi], ax
.fin:
    pop ax
    mov [np_wanty], ax
    pop ax
    mov [np_hity], ax
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_hmark - the note changed: the height is owed, and owed from the TOP
; preserves every register AND the flags (it only stores to memory), so it is a
; drop-in for the `mov byte [np_hdirty], 1` it replaces at each of its callers
;
; The reset is the whole reason this is a routine. A chunked count that is
; part-way down a note holds a (row, index) pair that describes the note it
; started on; an edit makes that pair name a row that may no longer begin
; there, so the count has to start over. Raising the flag and forgetting the
; pair are the same event and must not be separable.
; -----------------------------------------------------------------------------
np_hmark:
    mov byte [np_hdirty], 1
    mov word [np_hrow], 0
    mov word [np_hi], 0
    jmp np_xdrop                    ; ...and the row index with them (SPEC.md
                                    ; 27.13): it is the same event - a table of
                                    ; where rows BEGIN means nothing under a
                                    ; layout where they begin somewhere else.
                                    ; A tail call, because np_xdrop preserves
                                    ; the flags too and this routine's whole
                                    ; contract is that it is a drop-in for a
                                    ; `mov byte [np_hdirty], 1`

; -----------------------------------------------------------------------------
; np_measure - run the walk without drawing
; in:  SI = window ptr; the query fields already set
; out: as np_walk; preserves all registers
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
np_measure:
    call np_settle
    call np_bounds
    mov byte [np_draw], 0
    mov byte [np_sigup], 0
    mov byte [np_clip], 0
    call np_walk
    ret

; -----------------------------------------------------------------------------
; np_settle - if the visual break is up, take it down
; in:  SI = window ptr, gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_settle:
    cmp byte [np_bmode], 0
    je .out
    jmp np_reconcile                ; a tail call: it preserves what we do
.out:
    ret

; -----------------------------------------------------------------------------
; np_seedck - seed the next walk at the caret's row (SPEC.md 27.4)
; np_seedrow - seed it at row AX instead, and stop after row DX (SPEC.md 27.5)
; out: [np_resume] set if the seed is usable, left 0 if the walk must start at
;      index 0 after all; preserves all registers
;
; np_rows only describes rows the last completed walk reached, so a query about
; a row past [np_rowsn] - a click on the blank space below the text - has to
; fall back. That fallback is the ONLY thing keeping a stale table from
; answering with a plausible wrong index.
; -----------------------------------------------------------------------------
; np_seedck seeds one row EARLIER than the caret's, and walks back further
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
; the correct answer: [np_rows] describes visible rows only, so row 0 of a
; SCROLLED view was placed by a break above the view, and nothing here can
; redo it.
np_seedck:
    push ax
    push bx
    push cx
    push es
    mov byte [np_resume], 0
    cmp byte [np_ckok], 0
    je .out
    mov es, [np_dseg]
    mov ax, [np_ckpr]               ; the caret's row...
    call np_rowstart                ; ...and the index it begins at. Asked
    jc .back                        ; TWICE on the slow path, which is a shift
                                    ; and a load: np_rowstart preserves AX
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
    jne .back
    call np_ckword                  ; ...and a wrapped row is safe too as long
    jnc .seed                       ; as the edit is past its first word
.back:
    call np_rowstart                ; ...and the index it begins at
    jc .out
    or bx, bx
    jz .seed                        ; index 0 begins the NOTE: there is no
                                    ; earlier break to be redecided
    mov cl, [es:bx-1]
    cmp cl, ' '
    je .prev
    cmp cl, 13
    je .prev
    call np_cellrun                 ; mid-word: this row was split by the cell
    jnc .seed                       ; rule, and the word began further back -
                                    ; UNLESS the word is longer than a row, in
                                    ; which case np_wordfit never decided
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
    call np_rowstart
    jc .out
.seed:
    mov [np_sdi], bx
    mov [np_sdr], ax
    mov byte [np_resume], 1
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_ckword - may this row be seeded WITHOUT laying out the one above it?
; in:  BX = the row's start index, ES = the document segment
; out: CF = 0 yes, seed here; CF = 1 back up as before.
;      Preserves every register.
;
; SPEC.md 27.11.1. np_wordfit is the whole of why np_seedck backs up: standing
; at the first character of a word it measures whether that word ENDS inside
; the cells left on the row, and breaks in front of it if it does not. So the
; break that decides where this row starts is a function of the length of this
; row's FIRST WORD and of nothing else in this row - which means an edit past
; the end of that word cannot have moved it.
;
; The caret is the edit, near enough and always on the safe side: an insert
; leaves it one PAST the character it added, a backspace and a Delete leave it
; ON the edit, so the earliest index this keystroke can have touched is
; [np_cur] - 1. THE BOUND IS THAT INDEX, NOT THE CARET, and the difference is
; the whole of the case it has to catch: the character an insert added may
; ITSELF be the space that now ends the word, and then the word was LONGER when
; the break in front of it was taken - which is precisely the decision the
; back-up exists to redo. Accepting a terminator AT [np_cur] - 1 seeded the row
; at a start the insert had just moved. Backspace and Delete touch nothing
; below [np_cur] and pay one extra row for the shared bound, which is the
; conservative direction. It needs no extra state kept in step.
;
; The scan is at most a row's width and stops at the caret, so it is a handful
; of byte compares against a row of layout at ~6 ms.
; -----------------------------------------------------------------------------
np_ckword:
    push ax
    push bx
    push cx
    mov cx, [np_cur]
    jcxz .no                        ; a caret at 0 would decrement to 0xFFFF
    dec cx                          ; and accept every terminator on the row
    mov al, [es:bx]
    cmp al, ' '                     ; a row that begins on a space or a newline
    je .no                          ; is not a word start, and the reasoning
    cmp al, 13                      ; above does not describe it
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
; np_cellrun - is the break in front of this row owned by the CELL RULE alone?
; in:  BX = the row's start index, known mid-word ([es:bx-1] is neither a space
;      nor a CR), ES = the document segment
; out: CF = 0 seed here, the walk need go no further back; CF = 1 back up as
;      before. Preserves every register.
;
; SPEC.md 27.4.2, and it is np_ckword's other half: that one asks whether the
; edit is past the row's first word, this one asks whether there is a word-fit
; decision in front of this row AT ALL.
;
; np_wordfit's .p2l answers "longer than a row: the cell rule owns it" and
; breaks NOTHING - a word that cannot fit a whole row is laid out left to
; right and wrapped at the margin like the pre-27.11 automaton. So inside such
; a word every row start is the one before it plus np_rcols, anchored at the
; word's first character, and the position of THAT was settled by text the
; edit cannot reach. Backing up through it re-decides a break nobody ever
; decided: on a note that is one 249-character run, the caret's row backed up
; NINE rows to index 0 and pass 1 then laid out the whole view from the top of
; the note, on every keystroke (docs/NOTEPAD-NOTES.md 5.6).
;
; THE MARGIN IS WHY THIS COUNTS TO np_rcols + 2 AND NOT np_rcols + 1. The
; threshold np_wordfit actually applies is `length > np_rcols`, and this runs
; AFTER the edit has been applied to the buffer - so a backspace at the caret
; may already have taken one character out of the run being measured. Two
; spare characters is what makes the answer the same before and after: the run
; measured here is at least np_rcols + 2, so the word was at least np_rcols + 1
; before the keystroke and is at least np_rcols + 1 after it, and both are past
; the threshold. One spare would let a word of exactly np_rcols + 1 fall back
; to np_rcols under a backspace, where np_wordfit DOES have an opinion and the
; break in front of the word can move.
;
; The scan is at most a row's width of byte compares against a row of layout at
; ~6 ms, which is np_ckword's bargain and the same one.
; -----------------------------------------------------------------------------
np_cellrun:
    push ax
    push bx
    push cx
    mov cx, [np_rcols]
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
; np_rowstart - the index visible row AX begins at (SPEC.md 27.5)
; in:  AX = the row
; out: CF = 0 and BX = the index; CF = 1 = np_rows does not describe that row
; clobbers: BX and CF; every other register preserved
; -----------------------------------------------------------------------------
np_rowstart:
    push ax
    cmp byte [np_rowsok], 0
    je .no
    cmp ax, [np_rowsn]
    jae .no                         ; unsigned, so a row ABOVE the view - a
                                    ; negative index - fails here too
    shl ax, 1
    mov bx, ax
    mov bx, [bx+np_rows]
    cmp bx, [np_len]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; np_netseed - seed the caret-follow safety net FORWARD (SPEC.md 27.7.7)
; out: [np_resume] set if a row start at or before the caret was found
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
; So resume at the deepest row [np_rows] describes whose start index is at or
; before [np_cur], and walk forward from there. Everything before that row laid
; out identically - the edit, if there was one, is AT the caret and so at or
; after the seed, which is SPEC.md 27.4's argument unchanged, and 27.11's
; lookahead cannot reach back past it either. A caret ABOVE the table walks
; back to row 0, finds nothing that qualifies and leaves [np_resume] clear,
; which is the old behaviour and still the right answer.
; -----------------------------------------------------------------------------
np_netseed:
    push ax
    push bx
    mov byte [np_resume], 0
    cmp byte [np_rowsok], 0
    je .out
    mov ax, [np_rowsn]
    or ax, ax
    jz .out
    dec ax                          ; the deepest row the table describes
.try:
    call np_rowstart                ; BX = where it begins, CF=1 = it does not
    jc .out
    cmp bx, [np_cur]
    jbe .seed                       ; at or before the caret: safe to resume
    or ax, ax
    jz .out
    dec ax                          ; ...past it, so try the row above
    jmp short .try
.seed:
    mov [np_sdi], bx
    mov [np_sdr], ax
    mov byte [np_resume], 1
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE ROW INDEX (SPEC.md 27.13) - random access to a row without walking to it
;
; np_rows describes the VIEW and nothing else, so every question about a row
; outside it fell back to laying the note out from index 0. That is Up out of
; the top of the view, and it measured 5.2 s a press.
;
; This is a sparse table of the character index at which every Kth ABSOLUTE
; row begins: entry n describes row n << [np_xksh]. It costs no walking at
; all, because SPEC.md 27.7.3's background count already visits every row in
; order and already computes exactly this - np_xnote just keeps what was
; being thrown away.
;
; BOUNDED BY DECIMATION, not by growing. A note of nothing but newlines is one
; row per character, so at NP_MAXKB that is 16,384 rows - too many to reserve
; and awkward to claim. When the table fills, np_xhalve keeps every second
; entry and doubles the stride: it then always spans the whole note, always
; costs NP_XN entries, and the walk from the nearest checkpoint is at most K
; rows, which grows only logarithmically in the note's length.
;
; The stride is a POWER OF TWO and is kept as its log, because every lookup
; would otherwise be a `div` - 150 clocks against a shift's 10, once per row
; of every walk in the module.
; =============================================================================
; ENTRIES ARE THE WHOLE COST MODEL, and the first version got this wrong by
; being frugal with them. A lookup lands on the checkpoint at or BEFORE the row
; wanted, so the walk that follows is up to K rows - and one keystroke runs
; FOUR walks (np_vmove's, np_move's, the redraw's pass 1 and np_scrollpaint's),
; each paying that K. At 64 entries a 781-row note halves down to K = 16 and an
; Up cost 68 rows of walking, which measured 644 ms and is where 27.13's first
; cut stopped. 256 entries hold README at K = 4 with no halving at all.
;
; 512 bytes to do it, which is the right trade here twice over: a package's bss
; ships inside its image (SPEC.md 51/20.2) and Note Pad's document is a heap
; claim of its own (27.6), so this is half a kilobyte against a 16KB note and
; nothing against KERN_BUDGET, which a package does not touch.
NP_XN     equ 256               ; entries: 512 bytes, and 256 x 1 row means a
                                ; note under 256 rows is answered EXACTLY
NP_XKSH0  equ 0                 ; ...so the stride starts at 1 and doubles only
                                ; when the note proves it has to

; -----------------------------------------------------------------------------
; np_xnote - offer the row just started to the index
; in:  [np_row] = the visible row, [np_i] = where it begins, np_bounds run
; out: nothing; preserves all registers and the flags
;
; Called from np_rstart, so it runs once per row of EVERY walk and its cost in
; the ordinary case has to be nothing: one compare against the row the table
; wants next.
;
; ONLY THE NEXT ENTRY OWED IS TAKEN, and that is what keeps the table honest.
; A seeded walk skips the rows above its seed, so a table that recorded
; whatever it happened to pass would have holes - and a lookup landing in one
; answers for a row it never saw, which is docs/FIELD-NOTES.md 4's shape (an
; index resolved against a snapshot that had shifted). Contiguous or nothing.
; -----------------------------------------------------------------------------
np_xnote:
    pushf
    push ax
    mov ax, [np_row]
    add ax, [np_top]                ; ABSOLUTE: the table outlives the view,
    cmp ax, [np_xnext]              ; and [np_top] moves under it
    jne .out
    cmp word [np_xn], NP_XN
    jae .out                        ; full and not yet halved: cannot happen,
                                    ; .store halves on the way out, but a
                                    ; table that could not be halved must not
                                    ; be written past its end
    push bx
    mov bx, [np_xn]
    shl bx, 1
    mov ax, [np_i]
    mov [bx+np_xi], ax
    inc word [np_xn]
    mov ax, [np_xnext]              ; ...and where the next one goes
    push cx
    mov cl, [np_xksh]
    mov bx, 1
    shl bx, cl
    pop cx
    add ax, bx
    mov [np_xnext], ax
    pop bx
    cmp word [np_xn], NP_XN
    jb .out
    call np_xhalve
.out:
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; np_xhalve - keep every second entry and double the stride
; out: nothing; preserves all registers
;
; [np_xnext] is deliberately NOT recomputed: it is xn * K, and halving xn while
; doubling K leaves that product exactly where it was. The next row the table
; wants is the next row it wanted.
; -----------------------------------------------------------------------------
np_xhalve:
    push ax
    push cx
    push si
    push di
    xor si, si
    xor di, di
    mov cx, NP_XN / 2
.lp:
    mov ax, [si+np_xi]
    mov [di+np_xi], ax
    add si, 4                       ; every SECOND entry...
    add di, 2                       ; ...into consecutive slots
    loop .lp
    inc byte [np_xksh]              ; ...at twice the stride
    mov word [np_xn], NP_XN / 2
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_xdrop - the layout moved: every entry describes a note that is not this one
; out: nothing; preserves all registers and the flags
;
; Called by np_hmark, which is already "the height is owed and owed from the
; top" - the same event, because both are invalidated by exactly the thing
; that changes where a row begins. Keeping them one call is what stops a
; future edit raising one and forgetting the other.
; -----------------------------------------------------------------------------
np_xdrop:
    mov word [np_xn], 0
    mov word [np_xnext], 0
    mov byte [np_xksh], NP_XKSH0
    ret

; -----------------------------------------------------------------------------
; np_xrow - the checkpoint at or before ABSOLUTE row AX
; out: CF=1 none; else CF=0, AX = the checkpoint's absolute row, BX = the index
;      it begins at, CX = 1 if a LATER checkpoint exists (so the row wanted is
;      known to be within one stride) and 0 if this is the deepest
; -----------------------------------------------------------------------------
np_xrow:
    push dx
    mov cx, [np_xn]
    jcxz .no
    or ax, ax
    js .no                          ; above row 0: nothing describes it
    push cx
    mov cl, [np_xksh]
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
    mov bx, [bx+np_xi]
    cmp bx, [np_len]
    ja .nopop                       ; a table that outlived its note
    push cx
    mov cl, [np_xksh]
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
; np_xseed - seed the next walk at the checkpoint at or before ABSOLUTE row AX
; in:  AX = the absolute row wanted, DX = the VISIBLE row to stop after
; out: CF=0 seeded; CF=1 the table cannot answer and the caller is unchanged.
;      Preserves all registers.
; -----------------------------------------------------------------------------
np_xseed:
    push ax
    push bx
    push cx
    call np_xrow
    jc .no
    sub ax, [np_top]                ; np_sdr is a VISIBLE row (np_hwalk's rule)
    mov [np_sdr], ax
    mov [np_sdi], bx
    mov [np_lastrow], dx
    mov byte [np_resume], 1
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
; np_xseedi - seed at the checkpoint at or before the character index AX, and
;             BOUND the walk when the table can prove where that row ends
; in:  AX = a character index (in practice [np_cur])
; out: CF=0 seeded, and [np_lastrow] set when a later checkpoint exists;
;      CF=1 not seeded and [np_lastrow] untouched. Preserves all registers.
;
; The inverse lookup, and the one that matters most: the caret-follow net has
; the caret's INDEX and wants its ROW, which is the question that used to cost
; a walk of the whole note. np_xi rises with the row, so a binary search finds
; it in six compares.
;
; THE BOUND IS THE HALF WORTH HAVING. If checkpoint n is the last one at or
; before the caret and checkpoint n+1 exists, then the caret's row is below
; n*K and above (n+1)*K, so the walk may stop at (n+1)*K and SPEC.md 27.7.7's
; "the walk is still unbounded, and has to be" stops being true. At the
; deepest checkpoint there is no n+1 and it is unbounded again, which is the
; old behaviour and still correct.
; -----------------------------------------------------------------------------
np_xseedi:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                      ; the index wanted
    mov cx, [np_xn]
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
    mov di, [di+np_xi]
    cmp di, si
    ja .lower
    mov bx, ax                      ; np_xi[mid] <= index: mid is a candidate
    jmp short .bs
.lower:
    mov dx, ax
    dec dx
    jmp short .bs
.found:
    mov ax, bx                      ; BX = the entry
    mov di, ax
    shl di, 1
    mov di, [di+np_xi]
    cmp di, si
    ja .no                          ; even entry 0 is past it: impossible while
                                    ; entry 0 is index 0, and cheap to refuse
    cmp di, [np_len]
    ja .no
    push cx
    mov cl, [np_xksh]
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
    sub ax, [np_top]                ; visible
    mov [np_sdr], ax
    mov [np_sdi], di
    mov byte [np_resume], 1
    pop ax
    or ax, ax
    jz .okno
    push cx                         ; a later checkpoint exists, so the caret's
    mov cl, [np_xksh]               ; row is inside this stride: stop there
    mov ax, 1
    shl ax, cl
    pop cx
    add ax, [np_sdr]
    mov [np_lastrow], ax
.okno:
    clc
    jmp short .out
.no:
    mov byte [np_resume], 0         ; a REFUSAL is not a licence to resume at
    stc                             ; whatever the last caller seeded, which is
                                    ; what np_seedck and np_seedrow both say by
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
; np_seedtail - seed at the DEEPEST row [np_rows] describes, stopping after DX
; in:  DX = the visible row the walk has to reach
; out: [np_resume] set if a seed was had; preserves all registers
;
; np_seedrow's refusal is silent and its caller is then walking from index 0,
; so this is the fallback both of its callers want: the table stops somewhere,
; and the row BELOW where it stops is reached by walking forward a row or two
; rather than by laying the note out again from the top.
;
; It is np_netseed's argument (SPEC.md 27.7.7) for a ROW instead of for the
; caret, and a weaker case than np_netseed's: both of these callers are moving
; the caret, which reflows nothing, so every row above the seed laid out
; identically by inspection rather than by 27.4's reasoning.
; -----------------------------------------------------------------------------
np_seedtail:
    push ax
    cmp byte [np_rowsok], 0
    je .out                         ; no table at all
    mov ax, [np_rowsn]
    or ax, ax
    jz .out                         ; it describes nothing: index 0 it is
    cmp ax, NP_MAXROWS              ; ...and [np_rowsn] IS NOT CAPPED TO THE
    jbe .have                       ; ARRAY on the walk's natural-end path -
    mov ax, NP_MAXROWS              ; it is np_row+1 there, so a 781-row note
.have:                              ; leaves it at 771 against 60 slots. Every
                                    ; np_seedrow caller before this one passed
                                    ; a VISIBLE row, which np_bounds caps at
                                    ; NP_MAXROWS, so nothing ever indexed past
                                    ; the table and the miss went unseen; this
                                    ; caller takes its row FROM [np_rowsn] and
                                    ; would have been the first to read out of
                                    ; it
    cmp dx, ax
    jb .out                         ; THE GUARD, and the whole reason this is a
                                    ; routine rather than six lines at each
                                    ; call site: the deepest row is only a seed
                                    ; for a row BELOW it. np_seedrow and
                                    ; np_seedck each refuse for four different
                                    ; reasons and "the row is past the table"
                                    ; is only one of them - a caret on row 0
                                    ; asks np_seedck for row -1 and is refused
                                    ; too, and seeding THAT at row 13 starts
                                    ; the walk below the row it is looking for,
                                    ; which then never finds it. Measured: the
                                    ; caret stopped moving on Up and the view
                                    ; stopped following it
    dec ax
    call np_seedrow                 ; ...which also carries the bound in DX
.out:
    pop ax
    ret

np_seedrow:
    push ax
    push bx
    mov byte [np_resume], 0
    cmp byte [np_rowsok], 0
    je .out
    cmp ax, [np_rowsn]
    jae .out                        ; a row the table never described
    mov [np_sdr], ax
    shl ax, 1
    mov bx, ax
    mov ax, [bx+np_rows]
    cmp ax, [np_len]
    ja .out
    mov [np_sdi], ax
    mov [np_lastrow], dx
    mov byte [np_resume], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_paint - W_PAINT: draw the buffer and the caret
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_paint:
    push ax
    call np_bounds
    mov byte [np_bmode], 0          ; whatever the break was showing, this
    mov word [np_prowi], 0xFFFF     ; draws the NOTE over a filled content
    mov byte [np_resume], 0
    mov word [np_hity], 0xFFFF      ; no queries: this pass is here to draw
    mov word [np_wanty], 0xFFFF
    cmp byte [np_gchg], 0           ; a resize got here through W_PAINT, which
    je .laidout                     ; is not np_redraw and so has clamped
                                    ; nothing: measure the note under the new
                                    ; wrap width and put the view back inside
                                    ; it. np_bmode is 0 above, so np_settle
                                    ; inside np_measure is a no-op and this is
                                    ; just the walk
    call np_hmark                   ; the wrap width moved, so every row start
                                    ; moved with it: the height is owed, and
                                    ; owed from the TOP (SPEC.md 27.7.5)
    mov ax, [np_vrows]              ; ...and this walk is BOUNDED to the view
    mov [np_lastrow], ax            ; like every other one (SPEC.md 27.7.1).
    call np_measure                 ; It used to run to the last character for
                                    ; one number - the total - and drew not a
                                    ; pixel while it did, so a resize was the
                                    ; whole note walked INVISIBLY before the
                                    ; first row appeared
    mov ax, [np_top]                ; The bound is also what answers the clamp,
    call np_scrollto                ; exactly, without the total: a walk that
    mov byte [np_resume], 0         ; STOPPED proved the note reaches past the
.laidout:                           ; bottom of the view, so [np_top] is still
                                    ; good and np_scrollmax cannot bite; one
                                    ; that ENDED set [np_drows] to the truth on
                                    ; its way out and cleared the debt this
                                    ; block raised, so the clamp is right. The
                                    ; walk's own exit decides which, which is
                                    ; why nothing here tests for it
    mov byte [np_draw], 1
    mov byte [np_sigup], 1          ; the content was white-filled on the way
    mov byte [np_clip], 0           ; here, so this pass draws every row AND is
    mov byte [np_clean], 1          ; the baseline every later incremental
    mov ax, [np_vrows]              ; ...and it stops at the bottom of the view
    mov [np_lastrow], ax            ; like every other walk, because a full
                                    ; repaint of a 16KB note otherwise walks
                                    ; 16KB of it to draw one screenful - and
                                    ; every scroll step is a full repaint
    mov ax, [np_top]                ; ...and it STARTS at the top of the view
    mov dx, [np_vrows]              ; rather than at index 0 (SPEC.md 27.13).
    call np_xseed                   ; The bound above stopped it walking past
                                    ; the view; this stops it walking TO the
                                    ; view, which a raise of a window scrolled
                                    ; halfway down a long note paid in full
    call np_walk                    ; (SPEC.md 27.7/27.2)
    mov byte [np_resume], 0         ; SPENT HERE, like every other np_walk site
                                    ; (np_hwalk, np_brkdraw, np_move, np_vmove,
                                    ; np_onclick, np_dragsel, np_redraw's
                                    ; .done). np_xseed sets it and np_paint
                                    ; cleared it only on the way IN, so from
                                    ; here it stayed set with [np_sdr]/[np_sdi]
                                    ; still loaded - and the next walk that
                                    ; seeds nothing of its own resumes at the
                                    ; top of THIS view. np_measure is that
                                    ; walk, and np_hmove reaches it bare
                                    ; whenever [np_ckok] is 0
    mov byte [np_clean], 0          ; ...and because it WAS filled, a row's run
    call np_sigmark                 ; stops at its last character instead of
    mov ax, [np_top]                ; padding to the band's edge to erase with
    mov [np_ptop], ax               ; ...and the screen now shows THIS view
    pop ax
    call np_sbar                    ; the fill took the bar with it
    call np_fpaint                  ; ...and the find panel, which lives in the
                                    ; strip np_bounds took off the top of the
                                    ; content (SPEC.md 27.10)
    call np_hirechk                 ; this walk stopped at the bottom of the
    ret                             ; view, so the height is still owed

; -----------------------------------------------------------------------------
; np_hguess - what the note's LENGTH alone says about its height (SPEC.md 27.7.3)
; in:  [np_rcols] valid (np_bounds has run); out: [np_drows] raised to it
; preserves all registers
;
; The chunked count takes seconds on a long note, and until it lands the only
; thing [np_drows] holds is what the first screenful's bounded walk reached -
; so a 781-row file claimed to be 18 rows tall and the bar was drawn for a
; document that does not exist. This is the arithmetic answer available for
; nothing: the characters, divided by the cells a row holds.
;
; IT CAN ONLY EVER BE TOO SMALL, which is what makes it safe to publish. A row
; holds at most [np_rcols] cells, so a note of L characters needs at least
; L/cols rows - and every newline ends a row EARLY, which can only push the
; real number up. That is exactly [np_drows]'s existing direction of error
; (SPEC.md 27.7: a lower bound, never lowered), so nothing downstream needs a
; new rule and the walk goes on correcting it upward as it always did.
;
; The font is fixed-width, so "cells a row holds" is not an average of
; anything - it is [np_rcols], which np_bounds has just computed for the row
; buffer. One DIV, in a routine that has already made two far calls into the
; kernel at PERFORMANCE.md's ~756us apiece: under 2% of what it is riding on.
; -----------------------------------------------------------------------------
np_hguess:
    push ax
    push cx
    push dx
    mov cx, [np_rcols]
    jcxz .out                       ; a window too narrow to hold a cell: no
    mov ax, [np_len]                ; geometry to divide by, and nothing to say
    xor dx, dx                      ; [np_len] is capped at NP_MAXKB, so the
    div cx                          ; dividend never needs DX and cannot overflow
    cmp ax, [np_drows]
    jbe .out                        ; never LOWERED, the one rule this shares
    mov [np_drows], ax              ; with every other writer of it
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_hirechk - a height debt outlived this paint: make sure a worker exists
; preserves all registers
;
; The predicate in ONE place, called from both ends of the drawing: every exit
; of np_redraw, and np_paint - which is W_PAINT, and W_PAINT is the only thing
; that draws a window opened by DOUBLE-CLICKING A DOCUMENT. That launch loads
; the file in the entry proc and never calls np_redraw at all, so the note
; whose height most needs counting - a whole file, arriving at once - was the
; one note that never got a worker to count it. It sat at the first
; screenful's lower bound until some later edit happened to hire one, and
; before then the first click paid the entire count in a single hold.
; -----------------------------------------------------------------------------
np_hirechk:
    push ax
    cmp byte [np_hdirty], 0
    je .out
    mov ax, [np_drows]              ; a note that FITS needs no worker: the
    cmp ax, [np_vrows]              ; walk that drew it ran to the note's end
    jbe .out                        ; and cleared the debt on the way
    call np_hire
.out:
    pop ax
    ret

; =============================================================================
; The document claim (SPEC.md 27.6/50.3)
; =============================================================================

; -----------------------------------------------------------------------------
; np_resize - make the document claim AX kilobytes
; in:  AX = the wanted size in KB (clamped to NP_KB0..NP_MAXKB)
; out: CF=0 with [np_dseg]/[np_capkb]/[np_cap] updated, or CF=1 and all three
;      unchanged; preserves every register
;
; ALWAYS through OSAPI_MEM_REGROW, never claim-copy-free: a regrow extends in
; place when the paragraphs above it are free, so it needs the DIFFERENCE
; rather than old + new at once, and when it does have to move it brings the
; bytes with it (SPEC.md 50.3.1). Shrinking always succeeds in place, which
; is what makes File > New's give-back free.
; -----------------------------------------------------------------------------
np_resize:
    push ax
    push dx
    cmp ax, NP_KB0
    jae .lo
    mov ax, NP_KB0
.lo:
    cmp ax, NP_MAXKB
    jbe .hi
    mov ax, NP_MAXKB
.hi:
    cmp ax, [np_capkb]
    je .same                        ; already that size: nothing to ask for
    push ax
    mov dx, [np_dseg]
    call OSAPI_MEM_REGROW           ; out CF=0 and DX = the base NOW
    pop ax
    jc .out                         ; refused: the old claim stands untouched
    mov [np_dseg], dx               ; ...and a grow that MOVED reports a new
    mov [np_capkb], ax              ; base, which is the whole reason DX is
    mov cl, 10                      ; the answer (SPEC.md 50.3.1)
    shl ax, cl
    mov [np_cap], ax                ; NP_MAXKB * 1024 fits a word by design
.same:
    clc
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_dmov - AX = np_reloc to let the note move, 0 to pin it (SPEC.md 66.5.7.1)
; in:  AX; out: nothing - every register AND the flags preserved, because the
;      caller's CF is the file operation's verdict and this runs beside it
; -----------------------------------------------------------------------------
np_dmov:
    push ax
    push dx
    pushf
    mov dx, [np_dseg]           ; re-read: np_resize above may have moved it
    call OSAPI_MEM_MOVABLE
    popf
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_reloc - the compactor moved the note or the undo arena (SPEC.md 66.5.7)
; in:  BX = the base it WAS at, DX = the base it is at NOW, DS = CS = ours
; out: nothing; preserves every register
;
; TWO claims and one proc, because the kernel calls the proc that the moved
; claim was declared with and BX says which one it was. Two words is all
; either costs: every cursor into the note ([np_len], [np_caret], the two
; selection ends, np_rows' whole table) and every cursor into the arena
; ([np_utop], each record's blob offset) is a byte OFFSET, and an offset is
; what a move does not change.
;
; The staging buffer is deliberately absent. It is claimed for one save and
; freed at the end of it, it is the OSAPI_FILE_WRITE target throughout, and
; it never outlives the call that made it - so it is pinned by having no
; declaration, which is the cheapest correct answer for a transient.
; -----------------------------------------------------------------------------
np_reloc:
    cmp bx, [np_dseg]
    jne .undo
    mov [np_dseg], dx
    ret
.undo:
    cmp bx, [np_useg]
    jne .out
    mov [np_useg], dx
.out:
    ret

; -----------------------------------------------------------------------------
; np_fitclaim - size the claim to the note plus one kilobyte to type into
; out: nothing (all registers and the flags preserved)
;
; What a load ends with, on the way out of both its paths. np_load opens the
; claim to NP_MAXKB before the read because nothing knows the file's size
; until the read reports it; this is the other half of that, and it runs even
; when the read failed - a refused load must not leave eight kilobytes of heap
; held for a note that did not change.
; -----------------------------------------------------------------------------
np_fitclaim:
    pushf
    push ax
    push cx
    mov ax, [np_len]
    add ax, 1023                ; the note's own whole kilobytes...
    mov cl, 10
    shr ax, cl
    add ax, NP_GROWKB           ; ...plus one to type into
    call np_resize              ; a shrink always succeeds in place
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; np_room - make sure one more character fits
; out: CF=0 there is room at [np_len], CF=1 the note is as big as it can get
; clobbers: flags
;
; The growth point, and the only one. A keystroke that would fill the claim
; asks for another kilobyte first; a refusal - the heap's or NP_MAXKB's - is
; the keystroke being dropped, which is what a full note did before it could
; grow at all.
; -----------------------------------------------------------------------------
np_room:
    push ax
    mov ax, [np_len]
    cmp ax, [np_cap]
    jb .yes
    mov ax, [np_capkb]
    add ax, NP_GROWKB
    call np_resize
    jc .no
    mov ax, [np_len]
    cmp ax, [np_cap]
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
; np_stghold - claim the save's CR/LF staging buffer
; in:  [np_len]
; out: CF=0 with [np_stgseg] set and ES = it, or CF=1 (the toast is already
;      set); preserves every other register
;
; Sized from the note, not fixed: every character may become CR LF, so the
; worst case is 2 x [np_len]. It is a SECOND claim and it is transient - held
; only across the write - because the expansion grows and the document claim
; is sized for the document. A refusal is an ordinary path: the note is still
; there and still editable, it just cannot reach the disk until something
; gives memory back.
; -----------------------------------------------------------------------------
np_stghold:
    push ax
    push dx
    mov ax, [np_len]
    add ax, [np_len]                ; 2 x len, which cannot carry: [np_len] is
    add ax, 1023                    ; bounded by NP_MAXKB * 1024, and NP_MAXKB
                                    ; is 16 for exactly this sum's sake
    mov cl, 10
    shr ax, cl                      ; ...as whole kilobytes, rounded up
    cmp ax, NP_STGMIN
    jae .kb
    mov ax, NP_STGMIN               ; an empty note still needs somewhere to
.kb:                                ; put its zero bytes
    call OSAPI_MEM_CLAIM            ; out CF=0 and DX = the base segment
    jc .no
    mov [np_stgseg], dx
    mov es, dx
    clc
    jmp short .out
.no:
    mov word [np_stgseg], 0
    mov ax, np_e_nomem
    call np_saymsg
    stc
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_stgdrop - hand the staging buffer straight back
; out: nothing (all registers and the flags preserved)
; -----------------------------------------------------------------------------
np_stgdrop:
    pushf
    push ax
    push dx
    mov dx, [np_stgseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [np_stgseg], 0
.out:
    pop dx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; np_save - write the note to NOTES.TXT (SPEC.md 18.4/27.1)
; in:  nothing (the buffer and its length)
; out: CF = 0 it is on the disk, CF = 1 it is not; the outcome is said as a
;      toast either way; preserves all registers
;
; The CF is SPEC.md 27.15's: the close path saves and THEN closes, and closing
; on a save that failed is the one way this feature can lose the document it
; exists to protect. Every older caller ignores it.
;
; The note's bare 13s become CR LF on the way out, through the staging claim -
; which is sized for the worst case (every character a newline), so the
; staging pass needs no bounds test of its own.
; -----------------------------------------------------------------------------
np_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    call np_stghold             ; ES = the staging claim, or a toast and out
    jc .out
    call np_goto                ; the folder this document belongs to, if the
    mov ds, [np_dseg]           ; volume has been moved since (SPEC.md 19.2)
    xor si, si                  ; DS:SI = the note, ES:DI the expansion. NO
    xor di, di                  ; kernel variable is readable while DS is the
    mov cx, [cs:np_len]         ; document - through CS is how the counts are
    xor bx, bx                  ; reached, the dsk_copy_in discipline
.stage:
    jcxz .staged
    lodsb                       ; DF=0 per SPEC.md 1
    dec cx
    mov [es:di], al
    inc di
    inc bx
    cmp al, 13
    jne .stage
    mov byte [es:di], 10        ; the DOS half of the line ending
    inc di
    inc bx
    jmp short .stage
.staged:
    push cs
    pop ds                      ; ...and back, before anything else is read
    mov cx, bx                  ; ES:BX = the staged bytes (SPEC.md 20.3),
    xor bx, bx                  ; DX:CX their count (SPEC.md 18.4.1)
    xor dx, dx
    mov si, np_name
    call OSAPI_FILE_WRITE
    jc .err
    mov si, np_m_saved
    call np_setmsg
    call np_mark                ; the disk and the document agree again
    mov byte [np_named], 1      ; ...and this note IS that file from here on
    clc                         ; (SPEC.md 27.15)
    jmp short .done
.err:
    call np_errmsg              ; AX = FERR_* -> the toast
    stc
.done:
    pushf                       ; the answer, past a call that writes flags of
    call np_stgdrop             ; its own (SPEC.md 27.15.1: the close path acts
    popf                        ; on it, so it is a contract now rather than a
.out:                           ; by-product)
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_load - read NOTES.TXT back into the note (SPEC.md 18.4/27.1)
; in:  nothing
; out: nothing; the outcome is said as a toast; preserves all registers
;
; CR LF folds back to a single 13, and so does a lone LF - a note written by
; a Unix editor loads correctly too. Anything else outside 32..126 is
; dropped rather than rendered as a stray glyph, and a file longer than the
; buffer fills it and says so.
; -----------------------------------------------------------------------------
np_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, NP_MAXKB            ; open the claim to its ceiling for the read:
    call np_resize              ; nothing here knows the file's size until the
                                ; read reports it, and the fold below only ever
                                ; SHRINKS what arrived - so the file lands in
                                ; the document buffer itself and folds in
                                ; place, and there is no load staging at all
    call np_goto                ; ...and the same folder dance on the way in
    xor ax, ax                  ; PIN it across the read (SPEC.md 66.5.7.1):
    call np_dmov                ; ES:BX below is a pointer INTO this claim and
                                ; OSAPI_FILE_READ holds it across the sector
                                ; cache's own claims (SPEC.md 18.95), which
                                ; can compact. dsk_xfer's [mem_pinseg] guard
                                ; (66.3 rule 5) covers the transfer itself and
                                ; not the walk between transfers - Tracker
                                ; escapes this by declaring only after the
                                ; read, which an editor that loads repeatedly
                                ; cannot do
    mov es, [np_dseg]
    xor bx, bx                  ; ES:BX = the document, DX:CX its capacity
    mov cx, [np_cap]
    xor dx, dx
    mov si, np_name
    call OSAPI_FILE_READ        ; DX:AX = bytes read, and DX is 0 - a longer
    push ax                     ; movable again, and on BOTH paths out: the
    mov ax, np_reloc            ; error one returns through .err
    call np_dmov
    pop ax
    jc .err                     ; file is FERR_BIG, "Too big", and the note is
                                ; left alone. That is the honest answer and it
                                ; used to be a half-loaded note that the next
                                ; save then wrote back over the whole file
    mov cx, ax
    xor si, si                  ; ES:SI walks what arrived...
    xor di, di                  ; ...and ES:DI writes the kept characters back
    xor dx, dx                  ; over it. DI can never outrun SI - the fold
                                ; only drops bytes - so in place is safe.
                                ; DL = previous byte, DH = truncation flag
.fold:
    jcxz .folded
    mov al, [es:si]
    inc si
    dec cx
    cmp al, 10
    jne .notlf
    cmp dl, 13
    je .skip                    ; CR LF: the 13 already went in
    mov al, 13                  ; a lone LF is a line break too
.notlf:
    cmp al, 13
    je .store
    cmp al, 32
    jb .skip
    cmp al, 126
    ja .skip
.store:
    cmp di, [np_cap]
    jb .room
    mov dh, 1                   ; unreachable - the fold cannot outgrow what
    jmp short .folded           ; the read fitted - but a bound is a bound
.room:
    mov [es:di], al
    inc di
.skip:
    mov dl, al
    jmp short .fold
.folded:
    mov [np_len], di
    call np_clamp               ; a shorter file must not leave the caret
                                ; past the end of it
    mov si, np_m_loaded
    call np_setmsg
    call np_mark                ; what came off the disk IS the disk's version
    mov byte [np_named], 1      ; (SPEC.md 27.15), and this note is that file
    test dh, dh
    jz .done
    mov ax, np_m_trunc
    call np_saymsg
    jmp short .done
.err:
    call np_errmsg
.done:
    call np_fitclaim            ; both paths: give back what the file did not
.out:                           ; need, including the whole of a read that
                                ; failed and left the note as it was
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_errmsg - turn a FERR_* code into the toast string
; in:  AX = FERR_* (SPEC.md 18.4)
; out: nothing - the string is said as a toast; preserves all registers
; -----------------------------------------------------------------------------
np_errmsg:
    push ax
    push bx
    cmp ax, FERR_BIG
    jbe .known
    mov ax, FERR_IO             ; an unknown code is still a disk problem
.known:
    mov bx, ax
    shl bx, 1
    mov ax, [bx+np_errtab]
    call np_saymsg
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_ins - insert AL at the caret, which then sits after it
; in:  AL = the character; out: nothing; preserves all registers
;
; The gap is opened right to left because source and destination overlap, and
; by hand rather than with `rep movsb` for a reason that is easy to forget: a
; callback is entered with ES = KERNEL_SEG (SPEC.md 20.1), so a string move
; would write the gap into the KERNEL's memory at our offsets.
; -----------------------------------------------------------------------------
np_ins:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dl, al
    call np_room                    ; grow by a kilobyte if this is the
    jc .out                         ; keystroke that fills the claim; a
                                    ; refusal drops it, as a full note always
    call np_hmark                   ; the note is a different length, so it
                                    ; may be a different number of rows, and
                                    ; [np_drows] is a lower bound until
                                    ; something walks the whole of it
    mov es, [np_dseg]               ; did (SPEC.md 27.6)
    mov bx, [np_len]
    mov cx, bx
    sub cx, [np_cur]                ; CX = the bytes to the right of the caret
    jcxz .place
    mov si, bx
    dec si                          ; SI = the last live byte...
    mov di, bx                      ; ...and DI one past it: the runs overlap
    push ds                         ; and the gap opens UPWARD, so backwards
    mov ds, [np_dseg]               ; (SPEC.md 27.12). movsb is DS:SI -> ES:DI
    std                             ; and both ends are the note
    rep movsb
    cld                             ; SPEC.md 1: never leave DF set
    pop ds
.place:
    mov bx, [np_cur]
    mov [es:bx], dl
    inc word [np_len]
    inc word [np_cur]
    mov ax, bx                      ; ...and remember it, which is what makes
    mov cx, 1                       ; a burst of typing one undoable edit
    call np_urec_ins                ; (SPEC.md 27.9)
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
; np_del - delete the character the caret sits in front of
; out: nothing; preserves all registers. A caret at the end deletes nothing.
; -----------------------------------------------------------------------------
np_del:
    push bx
    push cx
    mov bx, [np_cur]
    cmp bx, [np_len]
    jae .out                        ; a caret at the end deletes nothing
    mov cx, 1
    call np_delspan                 ; the run is the primitive now (SPEC.md
                                    ; 27.8), and it is what records the undo
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_move - put the caret on the row [np_wanty] at column [np_wantx]
; in:  SI = window ptr, the two query fields set
; out: [np_cur] moved if that row exists; preserves all registers
; -----------------------------------------------------------------------------
np_move:
    push ax
    push dx
    mov word [np_hity], 0xFFFF      ; one query at a time
    mov ax, [np_wanty]              ; the row it names is the ONLY row this
    sub ax, [np_ty]                 ; walk has to visit (SPEC.md 27.5)
    push cx
    mov cl, 3
    sar ax, cl                      ; SIGNED, and floor: a row ABOVE the view
    pop cx                          ; is negative, which the old unsigned shift
    mov dx, ax                      ; could not say, so it took the seedless
                                    ; branch below and said nothing about it
    js .above                       ; nothing above the view is in [np_rows]...
    call np_seedrow
    cmp byte [np_resume], 0
    jne .bound                      ; the table describes it: one row walked
    call np_seedtail                ; ...and when it does NOT - which is every
                                    ; Down on the bottom visible row, the row
                                    ; below the view being one past the table -
                                    ; resume at the deepest row it DOES hold
    jmp short .bound
.above:
    push ax                         ; ...but the ROW INDEX describes rows the
    mov ax, dx                      ; view has never contained (SPEC.md 27.13),
    add ax, [np_top]                ; which is what Up out of the top wants: it
    call np_xseed                   ; takes an ABSOLUTE row, and DX is visible
    pop ax                          ; (np_xseed sets the bound itself, and
                                    ; .bound below setting it again is the same
                                    ; value - one place, whichever path ran)
.bound:
    ; THE BOUND IS SET ON EVERY PATH, and that is the whole fix (SPEC.md
    ; 27.7.9). [np_lastrow] is a one-shot that np_walk resets to 0x7FFF, so a
    ; caller which does not set it walks the WHOLE NOTE - "slow and never
    ; wrong", which np_seedrow's silent refusal turned into the common case:
    ; Down on the bottom visible row asks for a row one past the table, got no
    ; seed AND no bound, and re-laid out all 781 rows of README.TXT to find the
    ; row directly under the caret. Measured on a 4.77MHz 8088: 4,663 ms of a
    ; 4,866 ms keystroke, on the most used key in the editor.
    mov [np_lastrow], dx
    call np_measure
    mov byte [np_resume], 0
    mov ax, [np_wanti]
    cmp ax, 0xFFFF
    je .out                         ; no such row: the caret stays put, which
    mov [np_cur], ax                ; is what Up on the first line should do

    ; The caret moved but nothing else did, so the repaint may resume too - at
    ; whichever of the two rows comes FIRST. Up lands on the row above the
    ; checkpoint, and seeding at the checkpoint would then walk straight past
    ; the caret without ever finding it, which draws the bar at (0,0).
    mov ax, [np_wanty]
    sub ax, [np_ty]
    jc .out
    push cx
    mov cl, 3
    shr ax, cl
    pop cx

    ; THE TWO ROWS ARE THE WHOLE OF IT (SPEC.md 27.4.1). np_ask folds the caret
    ; into a row's signature and a caret move changes nothing else, so the only
    ; rows whose signatures can differ are the one it left and the one it
    ; arrived on. The seed below already puts the walk at the FIRST of them;
    ; this records the SECOND, so np_redraw's pass 1 can stop there instead of
    ; laying out the rest of the view to be told nothing moved.
    ; [np_ckpr] is still the row the caret CAME FROM at this point - the branch
    ; below is what moves it back - so the two are in hand together here and
    ; nowhere else.
    mov [np_mvbot], ax
    cmp ax, [np_ckpr]
    jae .arm                        ; already at or after the checkpoint
    push ax                         ; ...moving BACKWARDS, so the deeper of the
    mov ax, [np_ckpr]               ; two is the row being left
    mov [np_mvbot], ax
    pop ax
    cmp byte [np_rowsok], 0
    je .out
    cmp ax, [np_rowsn]
    jae .out
    push bx
    mov [np_ckpr], ax
    shl ax, 1
    mov bx, ax
    mov ax, [bx+np_rows]
    mov [np_ckpi], ax
    pop bx
.arm:
    mov byte [np_fast], 4           ; A CARET MOVE, and it used to say 3.
                                    ; np_fastok*'s contract numbers the kinds
                                    ; 1 insert, 2 backspace, 3 forward Delete,
                                    ; 4 a caret move, and they carry different
                                    ; PERMISSIONS: 1..4 may resume the walk,
                                    ; but only 1..3 may enter the visual break.
                                    ; np_move wants the first and never the
                                    ; second - "a caret move reflowed nothing",
                                    ; as np_redraw's own comment says - and 3
                                    ; granted it both. It was invisible in a
                                    ; 29-column window only because np_brktry
                                    ; needs NP_BRK_CELLS = 60 cells below the
                                    ; caret and one row there is 29; widen the
                                    ; window past 60 columns and Up entered the
                                    ; break, on a stale [np_ecol] left by some
                                    ; earlier edit, which is a phantom line
                                    ; break for as long as the settle takes
.out:
    mov byte [np_resume], 0
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_vmove - Up / Down: the same column, one row away
; in:  SI = window ptr, DX = -8 (up) or +8 (down)
; out: nothing; preserves all registers
;
; It measures twice: once to find the pixel the caret is at, then again for
; the index at that column on the neighbouring row. Two walks of at most 512
; characters, once per keystroke.
; -----------------------------------------------------------------------------
np_vmove:
    push ax
    push dx
    call np_settle                  ; before the seed, not inside np_measure:
                                    ; a reconcile runs walks of its own and
                                    ; would spend the seed we are about to set
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    push dx                         ; the caret is on its checkpoint's row by
    mov dx, [np_ckpr]               ; definition, so this walk is that row and
    call np_seedck                  ; no more (SPEC.md 27.4/27.5)
    cmp byte [np_resume], 0
    jne .lim
    cmp byte [np_ckok], 0           ; THE SEED FAILED, and the bound used to go
    je .nolim                       ; with it (SPEC.md 27.7.9). np_seedck asks
                                    ; for the row BEFORE the caret's, so a
                                    ; caret on a row past what [np_rows]
                                    ; describes is refused - and [np_lastrow]
                                    ; is a one-shot np_walk resets to 0x7FFF,
                                    ; so the walk then laid out the WHOLE NOTE
                                    ; to find the row the caret is already on.
                                    ; With no checkpoint there is no row to
                                    ; bound to either, and that case is the
                                    ; old one unchanged
    call np_seedtail                ; ...but with one, the table still reaches
                                    ; SOMEWHERE: resume there and walk forward
    cmp byte [np_resume], 0
    jne .lim
    push ax                         ; ...and when even that is refused - the
    mov ax, dx                      ; caret's row is ABOVE what np_rows
    add ax, [np_top]                ; describes, which is every Up out of the
    call np_xseed                   ; top of the view - the row index has it
    pop ax                          ; (SPEC.md 27.13)
.lim:
    or dx, dx                       ; ...unless that row is ABOVE the view,
    js .nolim                       ; where the signed limit would stop the
    mov [np_lastrow], dx            ; walk before it had found anything
.nolim:
    pop dx
    call np_measure                 ; [np_curx]/[np_cury]
    mov byte [np_resume], 0
    mov ax, [np_cury]
    add ax, dx
    mov [np_wanty], ax
    mov ax, [np_curx]
    mov [np_wantx], ax
    call np_move
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_hmove - Home / End: the same row, the far left or the far right
; in:  SI = window ptr, DX = the column to aim at (0 or 0x7FFF)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_hmove:
    push ax
    call np_settle                  ; before the seed - see np_vmove
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    cmp byte [np_ckok], 0           ; the caret's row IS the checkpoint's, so
    je .measure                     ; Home and End need no walk at all to find
    mov ax, [np_ckpr]               ; the row they are aiming at
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]
    jmp short .have
.measure:
    call np_measure                 ; [np_cury] = the row we are on
    mov ax, [np_cury]
.have:
    mov [np_wanty], ax
    mov [np_wantx], dx
    call np_move
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_onclick - W_ONCLICK: put the caret where the user pointed
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; clobbers what any window callback may
;
; The kernel only sends content clicks on the front window (SPEC.md 13), so
; there is no rect to test: every click that arrives here is ours, and the
; walk answers with the nearest character boundary - or with the end of the
; note for a click below the last line.
; -----------------------------------------------------------------------------
np_onclick:
    push ax
    push dx
%ifdef OS88UI_SBDRAG
    call os88ui_sbdragging      ; A PRESS CANNOT ARRIVE DURING A LIVE DRAG, so
    jc .nostale                 ; one that does means the release never came
    push cx                     ; (SPEC.md 13.10.5.7)
    push dx
    call np_bounds
    call np_sbset
    call os88ui_sbdrop          ; ...which takes the overlay off with it
    pop dx
    pop cx
.nostale:
%endif
    mov [np_hitx], cx
    mov [np_hity], dx
    mov word [np_wanty], 0xFFFF
    call np_settle                  ; the pointer has to be over the NOTE
    call np_bounds                  ; before it can be resolved (SPEC.md 27.3)
    call np_uclose                  ; ...and a click is not typing, so whatever
                                    ; was being typed is one finished edit
    call np_fpclick                 ; the find panel owns the top of the
    jc .notpanel                    ; content (SPEC.md 27.10)
    pop dx
    jmp .out
.notpanel:
                                    ; NOTHING here finishes the count any more.
                                    ; A click in the TEXT wants no total at
                                    ; all, and one on the BAR wants it only if
                                    ; it asks to go PAST what has been counted
                                    ; - which np_sbclick tests for itself, at
                                    ; the one place that knows which row is
                                    ; being asked for (SPEC.md 27.7.6).
                                    ; Finishing it here froze the machine on
                                    ; the first bar click after opening a file,
                                    ; which is exactly when the count has got
                                    ; least far and the freeze is longest
    call np_sbclick                 ; ...and the scroll bar is not the note
    jc .text
    pop dx
    ; **A CLICK THE BAR TOOK IS NOT A CLICK THAT SCROLLED** (SPEC.md 27.7.10).
    ; np_sbclick answers CF = 0 for every press that landed on the bar, and
    ; this used to send all of them to np_redraw - so a press on the THUMB, or
    ; an arrow click already at an end stop, repainted the whole note to show
    ; the pixels that were already there. On a maximized window that is every
    ; visible row lettered: reported from the field as half a second to a
    ; second of dead time before a dragged thumb would move, which is exactly
    ; what it is.
    cmp byte [np_sbmoved], 0
    je .out                         ; not one pixel changed
    call OSAPI_EVQ_PENDING          ; is another click right behind this one?
    or ax, ax                       ; then this scroll position is already
    jz .drawscroll                  ; superseded and drawing it is work the
    mov byte [np_sowed], 1          ; user will never see (SPEC.md 27.7.8).
    jmp short .out                  ; The WORKER owes the last one, because the
.drawscroll:                        ; queue may not be ours to finish
    call np_redraw                  ; np_scrollto dropped np_sigok, so this is
    jmp short .out                  ; the full path (SPEC.md 27.7)
.text:
    mov ax, dx                      ; the row the click landed on is the only
    sub ax, [np_ty]                 ; one that can answer it (SPEC.md 27.5)
    jc .full
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    mov dx, ax
    call np_seedrow
.full:
    pop dx
    call np_measure
    mov byte [np_resume], 0
    mov ax, [np_hiti]
    push ax                         ; a click in the note takes the keys back
    mov al, NP_FF_DOC               ; off the find panel (SPEC.md 27.10)
    call np_ffocus
    pop ax
    call np_selq                    ; a press INSIDE the selection is a drag of
    jc .move                        ; the text, not a new selection (27.8.1)
    mov [np_anchor], ax
    mov [np_cur], ax
    push ax
    call np_selclr
    pop ax
    jc .nosel                       ; there was no selection to erase, so the
    mov byte [np_ckok], 0           ; band the walk resumes at would have left
.nosel:                             ; its inversion on screen
    call np_redraw
    call np_dragsel                 ; ...and then follow the pointer until the
    call np_pdrawn                  ; button comes up; the counter's ordinal
    jmp short .out                  ; goes with the selection it named                  ; button comes up
.move:
    call np_dragmove
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_clamp - hold the caret inside the buffer after a load or a New
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_clamp:
    push ax
    call np_selclr                  ; a whole new buffer: the selection, the
    call np_uclear                  ; undo stack and the match count are all
    mov byte [np_fcok], 0           ; about the note that just went away, and
    mov byte [np_fcdirty], 1        ; an undo record applied to a different
                                    ; note corrupts it (SPEC.md 27.9)
    call np_hmark                   ; a whole new note is a whole new height
    mov word [np_top], 0            ; a NOTE row, so it names nothing once the
                                    ; note is replaced - and the top of a file
                                    ; just opened is where a reader starts
    mov byte [np_ckok], 0           ; the whole buffer just changed underneath
    mov byte [np_rowsok], 0         ; them, and both are indices INTO it
                                    ; (SPEC.md 27.4/27.5). np_bounds catches a
                                    ; geometry change and nothing caught this;
                                    ; every path here does in fact reach
                                    ; np_redraw with [np_fast] clear and walk
                                    ; in full, but that is a fact about the
                                    ; callers rather than about the data
    mov ax, [np_len]
    cmp [np_cur], ax
    jbe .out
    mov [np_cur], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_fastok* - five doors onto one answer: may np_redraw take the cheap paths
;              for this keystroke, and what does the visual break need to know
;              about it? (SPEC.md 27.4/27.3)
; in:  [np_cur] and the note BEFORE the edit
; out: [np_fast] = the KIND, 0 if none: 1 insert, 2 backspace, 3 forward
;      Delete, 4 a caret move. [np_ecol] = the caret's column on its row,
;      before the edit; [np_eext] = columns of the row below that go stale
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
; is [np_cur] - [np_ckpi] outright, because a row start is a character index
; and every character on a row occupies exactly one cell (a newline ends a row,
; so there cannot be one in between). Forward Delete is then the same fact plus
; one: the character it removes was ON that row, so the copy is stale one cell
; further.
;
; It is deliberately NOT a test of what the redraw will cost: pass 1 answers
; that, and it can only answer it after this has let it run cheaply.
; -----------------------------------------------------------------------------
np_fastok:                          ; a printable at the caret
    push ax
    push bx
    push cx
    mov bx, 1
    xor cx, cx
    mov ax, [np_cur]
    jmp short np_fastcm
np_fastokb:                         ; Backspace: the character that goes is one
    push ax                         ; index earlier, so that is the edit
    push bx
    push cx
    mov bx, 2
    xor cx, cx
    mov ax, [np_cur]
    dec ax
    jmp short np_fastcm
np_fastokd:                         ; forward Delete: the edit is AT the caret,
    push ax                         ; and it takes a cell off the row below too
    push bx
    push cx
    mov bx, 3
    mov cx, 1
    mov ax, [np_cur]
    jmp short np_fastcm
np_fastokm:                         ; Left: the caret lands one index back, so
    push ax                         ; that is the earliest cell that changes
    push bx
    push cx
    mov bx, 4                       ; a caret move is not an edit, and the
    xor cx, cx                      ; break is a thing you do to an EDIT
    mov ax, [np_cur]
    dec ax
    jmp short np_fastcm
np_fastokr:                         ; Right: it lands one FORWARD, and the cell
    push ax                         ; it leaves is the one it is on now
    push bx
    push cx
    mov bx, 4
    xor cx, cx
    mov ax, [np_cur]
np_fastcm:
    mov word [np_mvbot], 0x7FFF ; kind 4 arrives here as well (Left and Right),
                                ; and neither measures the row it came from -
                                ; so park the bound at "no idea" and let
                                ; np_move be the only thing that ever sets it
    cmp byte [np_ckok], 0
    je .out
    cmp ax, [np_ckpi]
    jb .out
    mov [np_fast], bl
    mov [np_eext], cl
    mov ax, [np_cur]
    sub ax, [np_ckpi]
    mov [np_ecol], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_onkey - W_ONKEY: edit the buffer, then repaint our own content
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing; preserves all registers
; Unhandled keys touch nothing; a full buffer drops the keystroke silently.
; -----------------------------------------------------------------------------
np_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    ; --- the keys that mean the same thing wherever the focus is -----------
    ; They come first because the find panel must not swallow F3, and the
    ; document must not swallow Ctrl-F. Everything below this block is routed
    ; by [np_ffield] (SPEC.md 27.10).
    cmp ah, NP_KEY_NEXT
    jne .nonext
    call np_donext              ; F3 - it WAS Load, which is Ctrl-O and the
    jmp .redraw                 ; File menu now (SPEC.md 27.10). Near: the key
                                ; ladder below outruns a short jump
.nonext:
    cmp ah, NP_KEY_PREV
    jne .noprev
    call np_doprev              ; Shift-F3
    jmp .redraw
.noprev:
%ifdef NPBENCH
    cmp al, 0x02                ; Ctrl-B - the walk bench, and it belongs up
    jne .nobench                ; here with F3 for the same reason: it must
    call npb_run                ; work whatever [np_ffield] is focused on
    jmp .redraw
.nobench:
%endif
    or al, al
    jz .noctl                   ; an extended key carries no ascii, so none of
                                ; the control characters below can be one
    cmp al, NP_C_FIND
    je .kfind
    cmp al, NP_C_REPL
    je .krepl
    cmp al, NP_C_ESC
    je .kesc
    cmp al, NP_C_TAB
    je .ktab
    cmp al, NP_C_COPY
    je .kcopy
    cmp al, NP_C_CUT
    je .kcut
    cmp al, NP_C_PASTE
    je .kpaste
    cmp al, NP_C_UNDO
    je .kundo
    cmp al, NP_C_SELALL
    je .kselall
    cmp al, NP_C_SAVE
    je .ksave
    cmp al, NP_C_OPEN
    je .kopen
.noctl:
    cmp byte [np_ffield], NP_FF_DOC
    je .noload                  ; the document has the keys
    cmp al, 13
    je .kenter                  ; Enter in a box IS Find Next
    cmp byte [np_fpan], NP_FPAN_NONE
    je .noload                  ; no panel on screen can own a keystroke
    call np_fpkey               ; ...and the find panel has the rest
    jc .out                     ; not a key it wants: nothing happens
    mov si, [np_win]            ; np_fpkey is free with SI and np_pdrawf wants
    call np_pdrawf              ; the window: only the box changed, so only it
                                ; is redrawn
    jmp .out
.noload:
    ; --- moving the caret: no edit, but the screen changes ------------------
    ; An EXTENDED key has AL = 0, and the gate matters: the numeric keypad
    ; sends '4' '6' '8' '2' '7' '1' '.' with exactly these scan codes, so
    ; without it NumLock would turn typing a digit into moving the caret.
    or al, al
    jnz .typing
    cmp ah, NP_K_LEFT
    je .left
    cmp ah, NP_K_RIGHT
    je .right
    cmp ah, NP_K_UP
    je .up
    cmp ah, NP_K_DOWN
    je .down
    cmp ah, NP_K_HOME
    je .home
    cmp ah, NP_K_END
    je .end
    cmp ah, NP_K_DEL
    je .del
.typing:
    cmp al, 8
    je .bksp
    cmp al, 13
    je .append
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    call np_selkill                 ; typing REPLACES a selection (SPEC.md
    call np_fastok                  ; 27.8), and the two halves land in one
    jmp short .doins                ; undo group. A printable at the caret is
                                    ; THE case the incremental paths exist for
                                    ; (SPEC.md 27.3/27.4); Enter is not, and
.append:                            ; jumps in below np_fastok
    call np_selkill
.doins:
    call np_ins                     ; at the caret, which follows it
    jmp .edited

.bksp:
    call np_selkill
    jnc .edited                     ; the selection WAS the deletion
    cmp word [np_cur], 0
    je .out                         ; nothing to the left of the caret
    call np_fastokb                 ; ...and so is a backspace, as long as the
    dec word [np_cur]               ; character it eats is on the caret's own
    call np_del                     ; row
    jmp short .edited

.del:
    call np_selkill
    jnc .edited
    mov ax, [np_cur]                ; forward delete: the caret stays put
    cmp ax, [np_len]
    jae .out
    call np_fastokd
    call np_del
    jmp short .edited

.left:
    call np_caretpre
    cmp word [np_cur], 0
    je .out
    call np_fastokm                 ; a caret move is not an edit, but the row
    dec word [np_cur]               ; above it still cannot have changed
    jmp short .edited
.right:
    call np_caretpre
    mov ax, [np_cur]
    cmp ax, [np_len]
    jae .out
    call np_fastokr
    inc word [np_cur]
    jmp short .edited
.up:
    call np_caretpre
    mov dx, -8
    call np_vmove
    jmp short .edited
.down:
    call np_caretpre
    mov dx, 8
    call np_vmove
    jmp short .edited
.home:
    call np_caretpre
    xor dx, dx
    call np_hmove
    jmp short .edited
.end:
    call np_caretpre
    mov dx, 0x7FFF
    call np_hmove

.edited:                        ; the toast is the kernel's and expires on
                                ; its own (SPEC.md 59), so a keystroke owes it
                                ; nothing at all - this used to be a store on
                                ; the hot path AND the reason np_sigsame threw
                                ; the fast path away on the key after a save
    call OSAPI_GET_TICKS        ; ...and restarts the settle clock, which is
    mov [np_ktick], ax          ; what the worker measures (SPEC.md 27.3)

.redraw:
    mov byte [np_follow], 1         ; a KEY got us here, so wherever the caret
                                    ; ended up the view owes it a place on
                                    ; screen (SPEC.md 27.7). Set at the one
                                    ; label every handled key reaches, rather
                                    ; than in each of the twelve above it
    call np_redraw                  ; SI still = window ptr

.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    ; --- the shortcuts (SPEC.md 27.8/27.10) --------------------------------
    ; Reached only by the ladder at the top of this proc, which is why they
    ; sit past its `ret`: every one of them ends by jumping back into it.
.kfind:
    mov al, NP_FPAN_FIND
    jmp short .kpan
.krepl:
    mov al, NP_FPAN_REPL
.kpan:
    cmp [np_fpan], al
    je .kfocus                      ; already up in this mode: the shortcut
    call np_fopen                   ; means "put the cursor back in the box"
    call np_redrawall               ; the panel moved [np_ty]: everything below
    jmp .out                        ; it wraps into a different set of rows
.kfocus:
    mov al, NP_FF_FIND
    call np_ffocus
    call np_fpaint
    jmp .out
.kesc:
    cmp byte [np_fpan], NP_FPAN_NONE
    je .kescsel
    call np_fclose
    call np_redrawall
    jmp .out
.kescsel:
    call np_selclr
    jc .out
    mov byte [np_ckok], 0
    jmp .redraw
.ktab:
    cmp byte [np_fpan], NP_FPAN_NONE
    je .out
    mov al, [np_ffield]             ; Find -> Repl -> the document -> Find
    inc al
    cmp al, NP_FF_DOC
    jbe .ktset
    xor al, al
.ktset:
    cmp al, NP_FF_REPL
    jne .ktok
    cmp byte [np_fpan], NP_FPAN_REPL
    je .ktok
    inc al                          ; there is no Replace box in Find mode
.ktok:
    call np_ffocus
    call np_fpaint
    jmp .out
.kcopy:
    call np_uclose
    call np_copy
    jmp .redraw
.kcut:
    call np_uclose
    call np_cut
    jmp short .kstamp
.kpaste:
    call np_paste
    jmp short .kstamp
.kundo:
    call np_undo
    jnc .kstamp
    mov ax, np_m_noundo
    call np_saymsg
    jmp .redraw
.kselall:
    call np_uclose
    xor ax, ax
    mov dx, [np_len]
    call np_selset
    mov [np_cur], dx
    mov byte [np_ckok], 0
    jmp .redraw
.ksave:
    call np_uclose
    call np_save
    jmp .redraw
.kopen:
    call np_uclose
    mov al, FDLG_OPEN
    call np_dlgopen
    jmp .out                        ; no repaint: the dialog is on top of us
.kenter:
    call np_donext                  ; Enter in a find box is Find Next
    jmp .redraw
.kstamp:
    call OSAPI_GET_TICKS            ; an EDIT, so the settle clock restarts -
    mov [np_ktick], ax              ; but the toast it may have set stands,
    jmp .redraw                     ; which is why this is not .edited

; -----------------------------------------------------------------------------
; np_selkill - an edit is about to happen: if a selection is up, it goes
; out: CF = 1 there was none; preserves all registers
; -----------------------------------------------------------------------------
np_selkill:
    call np_seldel
    jc .out
    call np_editinv
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; np_caretpre - a caret key is about to run: end the edit group and drop the
;               selection
; out: nothing; preserves all registers
;
; Clearing [np_ckok] when a selection actually went is the load-bearing half.
; The checkpoint lets np_redraw resume its walk at the caret's own row, and a
; selection reaches rows ABOVE that - a resumed walk never re-signs them, so
; the inversion would stay drawn on a row nothing intends to redraw again.
; -----------------------------------------------------------------------------
np_caretpre:
    call np_uclose
    call np_selclr
    jc .out
    mov byte [np_ckok], 0
.out:
    ret

; -----------------------------------------------------------------------------
; np_redraw - repaint our own content from the buffer, redrawing only the rows
;             that actually moved (SPEC.md 27.2)
; in:  SI = window ptr (gfx lock held by the caller)
; out: nothing; preserves all registers
;
; The self-repaint every dispatch site shares. It exists as a routine rather
; than a tail of np_onkey because the menu handler needs exactly the same
; steps - the kernel does not repaint after a command returns (SPEC.md 12.2),
; so every command that changes the buffer has to end here.
;
; Two walks: one to measure and compare, one to draw the band the first found.
; Typing one character usually dirties exactly one row, and the erase is then
; a fill 8 pixels tall instead of the whole content. When nothing on screen
; changed - an arrow key that hit the end of the note, a keystroke a full
; buffer dropped - it draws nothing at all and returns.
;
; np_sigsame decides whether that is legal: a resize or a toast coming and
; going means the stored signatures no longer describe what is on screen, and
; then this is the old routine unchanged - fill the content whole, np_paint
; over it, put the grow box back because the fill just erased it.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; np_append - a printable typed at the end of the note, drawn as ONE GLYPH
; in:  SI = window ptr; gfx lock held; np_bounds and np_sigsame have run and
;      agreed; [np_ekind] is the kind np_redraw consumed
; out: CF = 0 it drew and np_redraw owes nothing else; CF = 1 not this case.
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
; it should not work. np_fold is a rolling `rol 1, add` over the row's cells in
; order, and np_ask folds the CARET in at its own position - so with the caret
; at the end of the row the last two things folded are the caret and nothing
; else. That is invertible in four operations:
;
;     rol(B,1) = h - caret(C)                 undo the caret at column C
;     h' = rol(rol(B,1) + ch, 1) + caret(C+1) fold the character where it was,
;                                             then the caret after it
;
; so np_sig stays exact and the NEXT keystroke's np_sigsame still agrees. A
; fast path that left the signature stale would win once and pay for it on the
; keystroke after, which is how this was nearly built wrong.
;
; THE WRAP IS DEFERRED, DELIBERATELY. np_wordfit is asked at a word's first
; character and nowhere else, so appending to a word already committed to this
; row cannot move it - but a fresh layout WOULD ask again with the longer word
; and might break earlier. So the screen can be a wrap behind the note while
; the keys are still coming, exactly as 27.3's visual break is a line break
; ahead of it, and [np_sowed] is the debt: the worker already spends it with a
; full np_redraw, and only after NP_IDLE ticks without a keystroke. Nothing new
; had to be hired - np_ins raises np_hmark, which is what wakes the worker at
; all, and 27.7.3's height recount and this reconcile are the same settle.
; -----------------------------------------------------------------------------
np_append:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [np_ekind], 1          ; a printable insert at the caret and
    jne .no                         ; nothing else: a Delete moves the tail, a
    cmp byte [np_bmode], 0          ; caret move draws two rows, and the break
    jne .no                         ; owns the screen while it is up
    cmp byte [np_ckok], 0
    je .no                          ; no checkpoint: we do not know the row
    cmp byte [np_selon], 0
    jne .no                         ; a selection folds into the row's cells

    ; NOTHING AFTER THE CARET ON THIS ROW, which is the whole precondition and
    ; has exactly two shapes (SPEC.md 27.14.1). The end of the NOTE, where
    ; there is no tail at all; or a HARD NEWLINE at the caret, where the tail
    ; exists but is on other rows and none of its characters or its layout
    ; moves. A wrapped row's end is NOT one of them and cannot be: np_ask fires
    ; at the settled pen, so the index after the last character of a wrapped
    ; row is reported at column 0 of the row BELOW - that caret is on the next
    ; row, and a character typed there joins that row's first word, which is
    ; the one thing 27.11.1 says can move a break.
    mov byte [np_aprow], 0
    mov ax, [np_cur]
    cmp ax, [np_len]
    je .tailok
    mov es, [np_dseg]
    mov di, ax
    cmp byte [es:di], 13
    jne .no
    mov byte [np_aprow], 1          ; ...and rows BELOW start one index later
.tailok:
    or ax, ax
    jz .no
    dec ax                          ; AX = where the character landed
    mov bx, ax
    sub bx, [np_ckpi]
    js .no                          ; before the row start: not our row at all
    mov cx, [np_rcols]
    dec cx
    cmp bx, cx
    jae .no                         ; the cell rule is about to wrap this row,
                                    ; or the caret would land off the end of
                                    ; it - either way, walk properly. BX = the
                                    ; column the character occupies
    mov dx, [np_ckpr]
    or dx, dx
    js .no
    cmp dx, [np_vrows]
    jae .no                         ; off the view: nothing to draw
    cmp dx, [np_prowi]
    jne .no                         ; THE EXISTING GATE: np_prow describes some
                                    ; other row, so patching it would make the
                                    ; delta cache describe a row that is not on
                                    ; screen

    mov es, [np_dseg]
    mov di, ax
    mov al, [es:di]                 ; the character itself
    cmp al, ' '
    jb .no                          ; a newline ends a row and is not a glyph
    mov [np_ap1], al
    mov byte [np_ap1+1], 0
    mov [np_apch], al

    mov ax, bx                      ; --- x of the cell, y of the row ---------
    mov cl, 3
    shl ax, cl
    add ax, [np_tx]
    mov [np_apx], ax                ; x of column C
    mov ax, dx
    mov cl, 3
    shl ax, cl
    add ax, [np_ty]
    mov [np_apy], ax

    mov cx, [np_apx]                ; --- the glyph, opaque, one cell ---------
    mov dx, [np_apy]                ; opaque is what erases the caret bar that
    mov si, np_ap1                  ; is standing on this very cell
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov ax, [np_apx]                ; --- and the caret, one cell along -------
    add ax, 8
    mov [np_apx2], ax
    mov bx, [np_apy]
    mov dx, bx
    add dx, 7
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE

    ; --- and now the state the next keystroke reads --------------------------
    mov bx, [np_cur]                ; the row cache: cell C was a space, and
    dec bx                          ; the screen shows the character now
    sub bx, [np_ckpi]
    mov al, [np_apch]
    mov [np_prow+bx], al
    inc bx
    mov [np_prcc], bx               ; ...and its caret is one along

    mov bx, [np_ckpr]               ; the signature, patched rather than walked
    shl bx, 1                       ; (see the header)
    mov ax, [np_apx]
    xor ax, 0x5A5A
    mov dx, [bx+np_sig]
    sub dx, ax                      ; undo the caret that was folded at C
    xor ah, ah
    mov al, [np_apch]
    add dx, ax                      ; the character folds where it stood
    rol dx, 1
    mov ax, [np_apx2]
    xor ax, 0x5A5A
    add dx, ax                      ; ...and the caret after it
    mov [bx+np_sig], dx

    mov ax, [np_apx2]               ; np_seecaret and every hit test read these
    mov [np_curx], ax
    mov ax, [np_apy]
    mov [np_cury], ax
    mov byte [np_curseen], 1

    ; EVERY ROW BELOW STARTS ONE INDEX LATER (SPEC.md 27.14.1). Their
    ; characters and their layout are untouched - the pixels below the caret's
    ; row are still right, which is what makes this case legal at all - but
    ; np_rows is a table of INDICES and a character was inserted in front of
    ; all of them. Sixteen words at worst, against the walk this exists to
    ; skip. The end-of-note case has no rows below and does none of it.
    cmp byte [np_aprow], 0
    je .norows
    cmp byte [np_rowsok], 0
    je .norows
    mov cx, [np_rowsn]
    cmp cx, NP_MAXROWS              ; [np_rowsn] is not capped to the array it
    jbe .rok                        ; indexes (docs/NOTEPAD-NOTES.md 5.3.1), so
    mov cx, NP_MAXROWS              ; this caller clamps like np_seedtail does
.rok:
    mov bx, [np_ckpr]
    inc bx
.rsh:
    cmp bx, cx
    jae .norows
    push bx
    shl bx, 1
    inc word [bx+np_rows]
    pop bx
    inc bx
    jmp short .rsh
.norows:

    mov byte [np_sowed], 1          ; THE RECONCILE: the worker spends this with
                                    ; a full np_redraw, and only once the keys
                                    ; have stopped for NP_IDLE ticks
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

np_redraw:
    push ax
    push bx
    push cx
    push dx
    call np_bounds
    mov al, [np_fast]               ; ONE-SHOT: whoever set it meant this
    mov byte [np_fast], 0           ; redraw and no other
    mov [np_ekind], al
    mov byte [np_resume], 0
    cmp byte [np_bmode], 0
    je .normal
    or al, al
    jz .settle                      ; the break survives an insert and a
    cmp al, 3                       ; backspace and NOTHING else (SPEC.md
    jae .settle                     ; 27.3): the tail is not redrawn while it
                                    ; is up, so Right would draw a character
                                    ; twice, Left would lose one, and Delete
                                    ; eats exactly the tail's first character
    call np_sigsame                 ; ...and a resize or a toast is not typing
    jc .settle                      ; either
    call np_brkdraw
    jmp .out
.settle:
    call np_reconcile
    jmp .out

.normal:
    push ax
    call np_sigsame
    pop ax
    jc .full
    push ax                         ; the screen shows [np_ptop] and the view
    mov ax, [np_top]                ; may already have moved - a scroll bar
    cmp ax, [np_ptop]               ; click scrolls and THEN redraws. Reconcile
    pop ax                          ; the pixels first: everything below this
    jne .scrolled0                  ; indexes an array by a VISIBLE row, and
                                    ; those still count in the old view

    call np_append                  ; ONE GLYPH AND NO WALK (SPEC.md 27.14),
    jnc .done                       ; for a printable typed at the end of the
                                    ; note - which is most of typing. Here
                                    ; rather than earlier because it needs
                                    ; np_sigsame to have agreed and [np_ptop]
                                    ; to be [np_top]: it patches the row cache
                                    ; and the signature, and both describe a
                                    ; screen drawn for THIS geometry and THIS
                                    ; view. AL still holds [np_ekind] below -
                                    ; np_append preserves every register
    or al, al
    jz .noseed
    call np_seedck                  ; only an edit at the caret may skip the
.noseed:                            ; rows above it (SPEC.md 27.4)
    cmp byte [np_resume], 0         ; ...and failing that, the top of the VIEW
    jne .seeded1                    ; is a seed too: np_rows[0] is the index
    cmp byte [np_rowsok], 0         ; row 0 of the content starts at, rows
    je .xseed1                      ; above it have neither pixels nor
    xor ax, ax                      ; signatures, and nothing above the caret
    mov dx, [np_vrows]              ; can have reflowed anyway - which is the
    call np_seedrow                 ; same claim 27.4 already makes, applied
    cmp byte [np_resume], 0
    jne .seeded1
.xseed1:
    mov ax, [np_top]                ; ...and when np_rows cannot - which is
    mov dx, [np_vrows]              ; after EVERY scroll, because np_scrollto
    call np_xseed                   ; drops it - the row index still describes
                                    ; the view's top row (SPEC.md 27.13).
                                    ; Without this, pass 1 of the redraw after
                                    ; a scroll laid the note out from index 0
                                    ; to reach the row the view starts on: 240
                                    ; ms of a 640 ms Up at [np_top] = 5, and
                                    ; growing with the depth of the view
.seeded1:                           ; from a HIGHER row and so a weaker one.
                                    ; Both die together: np_scrollto,
                                    ; np_bounds and np_clamp clear [np_ckok]
                                    ; and [np_rowsok] side by side

    mov word [np_hity], 0xFFFF      ; pass 1: no queries, no drawing - just
    mov word [np_wanty], 0xFFFF     ; which rows stopped matching
    mov word [np_dr0], 0xFFFF
    mov word [np_dr1], 0
    mov byte [np_draw], 0
    mov byte [np_sigup], 1
    mov byte [np_clip], 0
    mov ax, [np_vrows]              ; STOP at the bottom of the view, plus the
                                    ; one row past it a caret can wrap onto
                                    ; (SPEC.md 27.7). Below that a row has no
                                    ; signature, cannot be dirty and is drawn
                                    ; by nobody - the only thing that ever
                                    ; wanted it was the note's total height,
                                    ; and np_height owns that now. Typing in
                                    ; the middle of a long note used to walk
                                    ; every row beneath the window on every
                                    ; keystroke: 72% of the work, for a thumb

    ; ...AND A CARET MOVE STOPS SOONER STILL (SPEC.md 27.4.1). Nothing reflowed,
    ; so the only rows whose signatures can differ are the one the caret left
    ; and the one it arrived on, and np_move recorded the deeper of them. The
    ; rest of the view is laid out to be told it did not move: (vrows - row) x
    ; ~6 ms, which is ~96 ms of the budget with the caret near the top.
    ;
    ; Gated on the walk actually RESUMING, and that is not caution about the
    ; bound - it is about [np_rowsn]. np_walk's bounded stop leaves the table
    ; alone for a resumed walk and SHRINKS it to where it stopped for one that
    ; started at the top of the view, which would hand rows this walk skipped
    ; back to SPEC.md 27.13's index for no reason. Resumed is the normal case
    ; here anyway: np_seedck seeds at the earlier of the two rows.
    cmp byte [np_ekind], 4
    jne .p1bound
    cmp byte [np_resume], 0
    je .p1bound
    mov dx, [np_mvbot]
    or dx, dx
    js .p1bound                     ; above the view: let the caret-follow net
    cmp dx, ax                      ; have it, exactly as before
    jae .p1bound                    ; never DEEPER than the view: the sentinel
    mov ax, dx                      ; lands here, and so does a caret one row
.p1bound:                           ; below the last visible one
    mov [np_lastrow], ax
    call np_walk
    cmp byte [np_follow], 0         ; the caret has to be somewhere the user
    je .noflw                       ; can see it (SPEC.md 27.7) - but only
    cmp byte [np_curseen], 0        ; ...and the walk above may have stopped
    jne .haveit                     ; short of the caret, in which case
                                    ; [np_cury] is still the initial 0 and
                                    ; following it would scroll somewhere
                                    ; arbitrary. Walk again FROM INDEX 0 and
                                    ; UNBOUNDED, which is the whole point of
                                    ; this net: the seed is what let the walk
                                    ; miss the caret and the bound is what
                                    ; made it missable, so a net carrying
                                    ; either finds nothing too. np_measure
                                    ; clears neither - np_vmove and np_onclick
                                    ; set them on purpose - so this does.
                                    ; The case is real and not theoretical:
                                    ; page the view away with the bar and then
                                    ; press a key, and the caret is a whole
                                    ; screenful below the last row walked
    mov word [np_lastrow], 0x7FFF   ; describes, when that row begins at or
    mov ax, [np_cur]                ; before the caret. THE ROW INDEX ANSWERS
    call np_xseedi                  ; THIS DIRECTLY (SPEC.md 27.13) and it is
    jnc .netok                      ; the case that mattered: a caret ABOVE the
                                    ; view qualifies no row np_rows describes,
                                    ; so np_netseed walked back to row 0, found
                                    ; nothing and left the walk to lay out the
                                    ; whole note - 5.2 s on every Up out of the
                                    ; top of the view. np_xseedi seeds within a
                                    ; stride of the caret AND sets the bound,
                                    ; because a later checkpoint proves which
                                    ; row the caret's row is above
    call np_netseed             ; ...FORWARD from the deepest row the table
.netok:
    call np_measure             ; before the caret. Unbounded still - the
                                ; caret's row is not known, which is the whole
                                ; problem - but not from INDEX 0: Down on the
                                ; bottom visible row puts the caret one row
                                ; below the view and re-walked the entire note
                                ; to find it, which is seconds on the most
                                ; used key there is (docs/NOTEPAD-NOTES.md 1.4)
.haveit:
    call np_seecaret                ; when it MOVED. A scroll bar click also
    jnc .scrolled                   ; ends here, and following the caret then
.noflw:                             ; would drag the view straight back to it
                                    ; and make the bar look broken. Moving
                                    ; the view renames every row the band and
                                    ; the signatures are counted in - so that
                                    ; is a full repaint, not a band
    mov ax, [np_dr0]
    cmp ax, 0xFFFF
    je .done                        ; not one pixel of the text moved

    mov al, [np_ekind]              ; would this reflow cost more than pushing
    or al, al                       ; the note down a row? (SPEC.md 27.3)
    jz .band                        ; Every EDIT at the caret qualifies -
    cmp al, 4                       ; insert, Backspace and Delete alike -
    jae .band                       ; because np_fastok* REPORTED the caret's
                                    ; column rather than leaving this to work
                                    ; it out from where the caret ended up. A
                                    ; caret move does not: nothing reflowed,
                                    ; so there is nothing to avoid drawing
    cmp byte [np_brkok], 0
    je .band
    call np_brktry
    jnc .done                       ; it took over, and it drew
.band:
    mov ax, [np_dr0]                ; reloaded: np_brktry is free with AX

    mov bx, ax                      ; y1 = np_ty + 8*dr0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [np_ty]
    mov dx, [np_dr1]                ; y2 = np_ty + 8*dr1 + 7
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [np_ty]
    add dx, 7
    ; The band fill is GONE. It used to erase dr0..dr1 whole and pass 2 then
    ; lettered it, which is the erase-and-letter pair - and on a 4.77MHz 8088
    ; that leaves the line blank for several display frames, so every keystroke
    ; flickered (SPEC.md 6.1). np_rflush draws each row as one opaque font_run
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
    mov ax, [np_tx]                 ; left: the margin, if there is one
    sub ax, NP_MARGIN
    mov cx, [np_tx]
    dec cx
    cmp ax, cx
    jg .mr
    call OSAPI_GFX_FILL
.mr:
    mov ax, [np_rcols]              ; right: the <8px tail past the last cell
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_tx]
    mov cx, [np_rgt]
    cmp ax, cx
    jg .mdone
    call OSAPI_GFX_FILL
.mdone:
    pop dx
    pop bx

    mov byte [np_draw], 1           ; pass 2: draw, and only inside it - and
    mov byte [np_sigup], 0          ; STOP at it, because pass 1 already knows
    mov byte [np_clip], 1           ; no row below np_dr1 changed. An arrow key
    mov ax, [np_dr1]                ; dirties two rows and used to lay out the
    mov [np_lastrow], ax            ; whole note behind them to draw them
    call np_walk
    mov byte [np_clip], 0

    ; THE GROW BOX IS NOT REDRAWN HERE AT ALL, because nothing on this path
    ; can reach it (SPEC.md 27.2.1). It was unconditional once, and it HAD to
    ; be: the band fill spanned the full content width, so any dirty row level
    ; with the box erased it. Then it became a row test - [np_bandb] against
    ; np_bot-12 - and that was still wrong in the expensive direction, because
    ; typing on the BOTTOM VISIBLE ROW satisfies it on every keystroke, which
    ; is exactly where the caret sits while a page is being filled. Measured:
    ; wm_grow_paint plus three gfx_frames and eighteen fills and lines, ~12 ms
    ; of a 52 ms keystroke - and the flicker in the corner that making it
    ; conditional was supposed to stop.
    ;
    ; The box is 13x13 at (np_sbr-12, np_bot-12) and np_bounds reserves that
    ; corner in BOTH dimensions: [np_rgt] is np_sbr-NP_SB_W, two pixels short
    ; of its left edge, and [np_sbb] is np_bot-NP_GROW, one row above its top.
    ; So the text runs, the two margin fills and the scroll bar all stop clear
    ; of it. The row test was reading the band's rows and never asked about its
    ; COLUMNS. Every other OSAPI_WM_GROW in this module follows something that
    ; genuinely reaches the corner - a full-content fill, or a band scroll that
    ; drags it - and those all stay.
    call np_sbcheck                 ; a note that gained or lost a row moves
                                    ; the thumb, and nothing else redraws it
                                    ; on this path
.done:
    mov byte [np_resume], 0
    jmp short .out

.scrolled0:
    mov word [np_dr0], 0xFFFF       ; no walk has run this redraw, so nothing
    mov word [np_dr1], 0            ; is known dirty beyond the exposed rows
.scrolled:
    ; The view moved. Move the PIXELS to match instead of drawing them again
    ; (SPEC.md 27.7.2) - and if that is refused, the full repaint below is
    ; exactly what used to happen every time.
    call np_scrollpaint
    jnc .done
    jmp short .fullpaint

.full:
    ; Reached when np_sigsame REFUSED - a resize, a toast arriving or leaving,
    ; an uncover - so nothing above has measured anything, and both numbers
    ; the view is clamped by may have changed: a wider window wraps into fewer
    ; rows. Measure, put the view back inside a note that may now be shorter
    ; than where it was looking, and only then follow the caret.
    call np_measure
    mov ax, [np_top]
    call np_scrollto
    cmp byte [np_follow], 0
    je .fullpaint
    call np_measure                 ; measured AGAIN because np_scrollto
    call np_seecaret                ; renames every row [np_cury] was counted
                                    ; in. Same gate as the band path: a bar
                                    ; click must not have its scroll undone
.fullpaint:
    ; ...and reached DIRECTLY from the scroll above, which is the common case
    ; and was paying for this block having no way to know that. A caret that
    ; has just been followed is in view by construction - np_seecaret's target
    ; is the exact row, not a step towards it - so re-measuring in order to
    ; ask the same question again cost two full walks per keystroke and could
    ; never answer differently. Every Up that scrolled, and every character
    ; typed with the view already trailing the caret, paid it.
    mov word [np_prowi], 0xFFFF     ; the delta cache describes the SCREEN, and
                                    ; the screen is about to be filled over.
                                    ; Every path that disturbs it other than
                                    ; our own row draws lands here - a resize,
                                    ; a toast arriving or leaving, an uncover -
                                    ; because that is what np_sigsame is for
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
    call np_paint                   ; SI still = window ptr
    mov bx, si                      ; the white fill erased the grow box;
    call OSAPI_WM_GROW              ; restore it (SPEC.md 11.1/27)
.out:
    call np_hirechk                 ; a debt left by ANY of this routine's
                                    ; exits, not just the .done path it used to
                                    ; hang off - .fullpaint fell straight past
                                    ; that one (SPEC.md 27.7.3)
    call np_selmark                 ; the screen shows this selection now
    mov byte [np_selonly], 0        ; ONE-SHOT: whoever set it meant THIS
                                    ; redraw, and the next one may well be a
                                    ; keystroke that moves characters
    mov byte [np_follow], 0         ; ONE-SHOT, like [np_fast]: whoever set it
                                    ; meant this redraw and no other, and the
                                    ; next one may well be a scroll bar click
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_new - empty the note (File > New)
; in:  nothing
; out: nothing; preserves all registers
;
; The length, the toast and the claim. np_paint reads exactly [np_len] bytes,
; so the stale tail is unreachable and wiping it would buy nothing - but the
; claim it sat in is real memory, and a note that grew to NP_MAXKB has no
; business holding eight kilobytes of heap after the user emptied it. The
; toast goes because "Loaded NOTES.TXT" over an empty note is a lie - the same
; reason an ordinary keystroke retires it.
; -----------------------------------------------------------------------------
np_new:
    mov word [np_len], 0
    mov word [np_cur], 0
    mov ax, np_s_nul            ; retire the toast: 'Loaded NOTES.TXT' over an
    call np_saymsg              ; empty note is a lie. An EMPTY string is how
    call np_clamp               ; SPEC.md 59.3 spells that, so no flag of ours
               ; a no-op on the caret, which is already 0 -
                                ; it is here for the invalidation np_clamp
                                ; carries (SPEC.md 27.4/27.5)
    push ax                     ; ...and give the heap back what the old note
    mov ax, NP_KB0              ; had grown into. A shrink always succeeds in
    call np_resize              ; place, so this cannot fail (SPEC.md 50.3.1)
    pop ax
    mov byte [np_named], 0      ; ...and it is not a FILE until something
    call np_mark                ; makes it one (SPEC.md 27.15): empty and
                                ; clean, so New then Close asks nothing
    jmp np_defname              ; a new note is a new document: leaving the
                                ; old name would make the next Ctrl-S overwrite
                                ; the file the user just walked away from

; -----------------------------------------------------------------------------
; np_oncmd - AM_ONCMD: run a menu command (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0 = File), SI = window ptr,
;      BX = our menu set ptr; gfx lock held by the caller, UI task
; out: nothing; clobbers AX/BX/CX/DX/DI/ES like any window callback
;
; Open and Save are menu-driven twins of Ctrl-O and Ctrl-S - the same np_load /
; np_save the keyboard path calls, so the two doors can never drift apart;
; New is menu-only, and empties the buffer. Every item changes
; what the window shows - the text, the toast, or both - and the kernel does
; not repaint for us, so all three tails run through np_redraw. The menu
; index is tested even though we register only one menu: the argument is
; kernel input, and an unknown pair must do nothing rather than fall into
; the first case.
; -----------------------------------------------------------------------------
np_oncmd:
    call np_uclose                  ; a menu command is not typing, so the edit
                                    ; group it interrupts is finished
    test ah, ah
    jz .file
    cmp ah, 1
    je .edit
    cmp ah, 2
    je .find
    ret                             ; none of ours: do nothing rather than
                                    ; fall into the first case
.edit:
    cmp al, NP_MI_UNDO
    je .e_undo
    cmp al, NP_MI_CUT
    je .e_cut
    cmp al, NP_MI_COPY
    je .e_copy
    cmp al, NP_MI_PASTE
    je .e_paste
    cmp al, NP_MI_SELALL
    je .e_all
    ret
.find:
    cmp al, NP_FI_FIND
    je .e_find
    cmp al, NP_FI_NEXT
    je .e_next
    cmp al, NP_FI_REPL
    je .e_repl
    ret
.e_undo:
    call np_undo
    jnc .draw
    mov ax, np_m_noundo
    call np_saymsg
    jmp short .draw
.e_cut:
    call np_cut
    jmp short .draw
.e_copy:
    call np_copy
    jmp short .draw
.e_paste:
    call np_paste
    jmp short .draw
.e_all:
    xor ax, ax
    mov dx, [np_len]
    call np_selset
    mov [np_cur], dx
    mov byte [np_ckok], 0
    jmp short .draw
.e_next:
    call np_donext
    jmp short .draw
.e_find:
    mov al, NP_FPAN_FIND
    jmp short .e_pan
.e_repl:
    mov al, NP_FPAN_REPL
.e_pan:
    call np_fopen
    jmp np_redrawall                ; a tail call: the panel moved [np_ty], so
                                    ; every row below it wraps differently
.file:
    cmp al, NP_MI_NEW
    je .new
    cmp al, NP_MI_OPEN
    je .open
    cmp al, NP_MI_SAVEAS
    je .saveas
    cmp al, NP_MI_SAVE
    jne .out
    call np_save
    jmp short .draw
.new:
    call np_new
.draw:
    mov byte [np_follow], 1
    call np_redraw                  ; SI is still the window ptr
.out:
    ret
.open:
    mov al, FDLG_OPEN
    jmp short .dlg
.saveas:
    mov al, FDLG_SAVE
.dlg:
    jmp np_dlgopen                  ; tail call, and NO repaint after it:
                                    ; the dialog is on screen and on top of
                                    ; us, so the usual "commands repaint
                                    ; themselves" tail would draw straight
                                    ; over it. The repaint happens in
                                    ; np_ondlg instead, once it is gone

; =============================================================================
; Closing with unsaved work (SPEC.md 27.15, 75)
;
; The kernel asks before it closes a window now, so this is where Note Pad
; answers. Three questions, in this order: has the document changed since it
; last agreed with the disk; is there a file to write it to; and what does the
; user want done about it.
;
; THE FIRST ONE IS A CHECKSUM, NOT A FLAG, and that is the design decision
; worth reading. A dirty flag has to be set by every mutation - insert,
; backspace, delete, paste, the drag that moves a block, replace-all, and undo
; going the other way - which is nine places that must each remember, and it
; answers DIRTY for a note the user typed a character into and took straight
; back out. A shadow copy answers exactly and costs 16KB of the one claim this
; app cannot do without. Fletcher's two sums over the note, plus its length,
; cost three words of bss and one walk at the moment of closing - and the walk
; is the cheap end: the note is at most 16,384 bytes of a four-instruction
; loop, ~0.14s on the 4.77MHz machine at PERFORMANCE.md's instruction floor
; and typically far less, against the SECONDS a save costs on the same floppy.
; =============================================================================

; -----------------------------------------------------------------------------
; np_cksum - Fletcher's two sums over the document (internal)
; out: AX = s1, DX = s2; every other register preserved
;
; DS is the DOCUMENT for the length of the walk, so [cs:np_len] and
; [cs:np_dseg] are how the counts are reached - np_save's discipline, and the
; reason is the same: no kernel or package variable is readable through DS
; while it points at the note.
; -----------------------------------------------------------------------------
np_cksum:
    push bx
    push cx
    push si
    push ds
    xor ax, ax                      ; AX = s1: the sum of the bytes
    xor dx, dx                      ; DX = s2: the sum of the sums, which is
                                    ; what makes it sensitive to ORDER
    mov cx, [np_len]
    mov ds, [np_dseg]
    xor si, si
    xor bh, bh
.next:
    jcxz .done
    mov bl, [si]                    ; DS:SI = the note (SPEC.md 27.6)
    inc si
    dec cx
    add ax, bx
    add dx, ax
    jmp short .next
.done:
    pop ds
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_mark - the document as it stands IS what is on the disk (internal)
; out: nothing; preserves all registers and the FLAGS
;
; The flags because np_save calls it on its success path, between the write
; and the CF it now owes its caller (SPEC.md 27.15.1).
; -----------------------------------------------------------------------------
np_mark:
    pushf
    push ax
    push dx
    call np_cksum
    mov [np_ds1], ax
    mov [np_ds2], dx
    mov ax, [np_len]
    mov [np_dslen], ax
    pop dx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; np_dirty - has the document changed since np_mark? (internal)
; out: CF = 1 it has; preserves all registers
; -----------------------------------------------------------------------------
np_dirty:
    push ax
    push dx
    call np_cksum
    cmp ax, [np_ds1]
    jne .yes
    cmp dx, [np_ds2]
    jne .yes
    mov ax, [np_len]
    cmp ax, [np_dslen]
    jne .yes
    pop dx
    pop ax
    clc
    ret
.yes:
    pop dx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; np_qcompose - 'Save changes to NOTES.TXT?' into np_qbuf (internal)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_qcompose:
    push ax
    push si
    push di
    mov si, np_q_pre
    mov di, np_qbuf
.pre:
    lodsb
    or al, al
    jz .name
    mov [di], al
    inc di
    jmp short .pre
.name:
    mov si, np_name
.copy:
    lodsb
    or al, al
    jz .end
    mov [di], al
    inc di
    jmp short .copy
.end:
    mov word [di], '?'              ; '?' and the NUL in one store - the high
    pop di                          ; byte of the immediate is 0
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_onclose - the CLOSE NEGOTIATOR (SPEC.md 75.1, API 0x0468)
; in:  SI = our window; the UI task, gfx lock HELD
; out: CF = 0 close me, CF = 1 not yet
;
; The whole feature seen from the kernel's side: a clean note closes exactly
; as it always did, and a dirty one puts the question up and REFUSES, leaving
; itself on screen to be answered. np_onask below finishes the job.
;
; THE FALLBACK IS TO SAVE, and it is not a corner case: OSAPI_ASK refuses
; whenever an alert is already up and ALWAYS on kern_small, which does not
; carry the module (SPEC.md 75.3.2). SPEC.md 75's first sentence names both
; halves of what an application needs on the way out - ask, or take the
; action - and this is where the second one is taken. It closes even if the
; write failed, which is the deliberate half: the toast names the error, and
; a window that cannot be closed on a machine with no dialog to close it from
; is the worse outcome of the two.
; -----------------------------------------------------------------------------
np_onclose:
    call np_dirty
    jnc .go                         ; nothing unsaved: nothing to say
    push si
    push di
    push bx
    call np_qcompose
    mov bx, si                      ; BX = our window; SI becomes the message
    mov si, np_qbuf
    mov di, np_onask
    mov al, OS88UI_ASAVE
    call os88ui_ask                 ; ...and this is asked EVEN WHEN ONE IS
                                    ; ALREADY UP, which looks redundant and is
                                    ; the whole point: the refusal RAISES the
                                    ; alert that is up (SPEC.md 75.3.1), so a
                                    ; second click on the close box brings the
                                    ; question back to the front - and
                                    ; un-minimizes it - instead of doing
                                    ; nothing
    pop bx
    pop di
    pop si
    jnc .up
    cmp byte [np_asking], 0
    jne .wait                       ; refused because OURS is up - and it has
                                    ; just been raised
    jmp short .wait                 ; refused because the window table is
                                    ; full: there is nothing to ask with and
                                    ; nothing safe to do, so stay open and let
                                    ; the user close something
.up:
    mov byte [np_asking], 1
.wait:
    stc
    ret
.go:
    clc
    ret

; -----------------------------------------------------------------------------
; np_onask - the alert's completion (SPEC.md 75.3)
; in:  AL = the answer, SI = our window; the UI task, gfx lock HELD, the alert
;      already destroyed
; out: nothing
;
; OS88UI_ACANCEL arrives when the alert was DISMISSED rather than answered -
; its close box, its minimize box or Esc - and it lands on the same branch Cancel
; does, because both mean "I am not closing after all". Clearing [np_asking]
; is the one thing every branch owes: without it the next close attempt would
; take .wait for ever and the window could never be closed again.
;
; It repaints NOTHING. The alert's own teardown has already repainted what it
; covered (SPEC.md 75.3), the document is unchanged on every branch, and the
; two branches that close are about to have their window destroyed anyway.
; -----------------------------------------------------------------------------
np_onask:
    mov byte [np_asking], 0
    or al, al
    jz .save                        ; 0 = Save (the default, and Enter)
    cmp al, 1
    je .close                       ; 1 = Discard
    ret                             ; 2 = Cancel, or OS88UI_ACANCEL: stay
                                    ; open
.save:
    cmp byte [np_named], 0
    je .saveas                      ; never been a file: ASK where it goes
                                    ; rather than putting it in NOTES.TXT
    call np_save
    jc .out                         ; the write failed and said so; stay open,
                                    ; so the user still has the document and
                                    ; can pick Don't Save if they mean it
.close:
    mov bx, si
    call OSAPI_WM_CLOSE             ; DEFERRED (SPEC.md 75.2): this returns and
    ret                             ; the window goes on the next UI pass
.saveas:
    push si
    mov al, FDLG_SAVE
    call np_dlgopen                 ; SI is still our window
    pop si
    jc .out                         ; refused: stay open
    mov byte [np_qclose], 1         ; ...and its commit is a QUIT (np_ondlg)
.out:
    ret

; -----------------------------------------------------------------------------
; np_dlgopen - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: CF = the dialog's own answer - 0 it is up, 1 refused; preserves all
;      registers (the pops below write no flags)
;
; The current document is handed over as the default, so Save As on a note
; loaded from LETTER.TXT opens with LETTER.TXT already in the box. A refusal
; (CF=1: one is already up) is silently nothing - the dialog the user
; already has IS the answer to the command they just picked.
; -----------------------------------------------------------------------------
np_dlgopen:
    push bx
    push si
    push di
    mov byte [np_qclose], 0         ; whatever this dialog is for, it is not
                                    ; SPEC.md 27.15's quit until np_onask says
                                    ; so - and it is cleared HERE, at the one
                                    ; place all three callers pass through,
                                    ; because a CANCELLED dialog never reaches
                                    ; np_ondlg and so can never clear it itself
    mov bx, si                      ; the window we want to hear back about
    mov di, np_ondlg
    mov si, np_name
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_ondlg - the file dialog's completion callback (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, DI = the chosen name;
;      UI task, gfx lock HELD, the dialog window already destroyed
; out: nothing; no register need be preserved (the kernel saved its own)
;
; One proc for both commands, because the only difference between them is
; which way the bytes move afterwards. It must repaint: the kernel does not
; repaint after a callback returns, and the window under the dialog has just
; been uncovered by wm_destroy.
; -----------------------------------------------------------------------------
np_ondlg:
    mov bl, al                      ; BL = the mode; AL becomes a name byte
    mov dx, si                      ; DX = our window: SI is about to be the
                                    ; kernel's buffer, and np_redraw wants
                                    ; the window back in SI
    mov si, di
    mov di, np_name
    mov cx, NP_NAMEMAX
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
    push dx                         ; the window: FILE_HERE answers in DX
    push bx                         ; ...and the mode is in BL
    call OSAPI_FILE_HERE            ; where the dialog left the volume IS the
    mov [np_dir], dx                ; folder the user chose, and it is the one
    mov [np_drv], bl                ; this document belongs to from here on
    mov byte [np_dirok], 1
    pop bx
    pop dx
    mov si, dx                      ; SI = our window again
    or bl, bl
    jz .load
    call np_save
    pushf                           ; its answer, past the read-and-clear
    xor al, al                      ; SPEC.md 27.15: was this Save As the
    xchg al, [np_qclose]            ; alert's? Read AND CLEARED, because the
    popf                            ; intent is spent whichever way it went
    jc .draw                        ; the write failed: it said so, and a
                                    ; failed save is never a quit - stay open
                                    ; with the document still in hand
    or al, al
    jz .draw
    mov bx, si
    call OSAPI_WM_CLOSE             ; deferred; no repaint is owed after it
    ret
.load:
    call np_load
.draw:
    jmp np_redraw                   ; tail call; SI is the window ptr

; -----------------------------------------------------------------------------
; np_goto - put the volume back in this document's folder (SPEC.md 19.2)
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
np_goto:
    push ax
    push bx
    push dx
    cmp byte [np_dirok], 0
    je .out                     ; never saved anywhere in particular
    call OSAPI_FILE_HERE
    cmp dx, [np_dir]
    jne .move
    cmp bl, [np_drv]
    je .out
.move:
    mov dx, [np_dir]
    mov bl, [np_drv]
    call OSAPI_FILE_GOTO        ; CF = it could not be listed; the file call
.out:                           ; that follows will say so in its own words
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_arg - open the document we were launched to open (SPEC.md 54.5)
; in:  nothing; called from np_entry once the window and the claim exist
; out: nothing; preserves all registers AND the flags, because the CF this
;      package owes the loader is still riding in them
;
; The kernel hands over a name and a (cluster, volume) pair rather than
; putting us in the right folder, because it cannot: the loader read our own
; image out of OUR directory and far-called this entry as one unit. So the
; whole of accepting a document is to copy the name, record the folder the
; way Save As already records one, and let np_load do what Ctrl-O does.
;
; The name lives in the KERNEL segment, so ES is loaded explicitly rather
; than trusted: it happens to still be KERNEL_SEG here, and a later edit that
; left a package segment in ES would read this package's own image as a file
; name and fail in a way that looks like a kernel bug.
; -----------------------------------------------------------------------------
np_arg:
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
    mov [np_dir], dx                ; the folder it lives in, recorded the
    mov [np_drv], bl                ; way Save As records one - np_goto then
    mov byte [np_dirok], 1          ; takes np_load there
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, np_name
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
    call np_load                    ; ...and this is Ctrl-O, unchanged
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
; np_defname - seed the current document name (internal)
; in:  nothing
; out: np_name = 'NOTES.TXT'; preserves all registers
; The loader zeroes our bss (SPEC.md 21 step 5), and an empty name would
; make Ctrl-S fail with FERR_NAME on a brand-new note - so a fresh Note Pad
; still has the document the fixed-name version always had.
; -----------------------------------------------------------------------------
np_defname:
    push ax
    push si
    push di
    mov si, np_s_default
    mov di, np_name
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
; np_setmsg - compose a toast around the live document name (internal)
; in:  SI = a NUL prefix ('Saved ' / 'Loaded ')
; out: np_tbuf holds prefix + np_name and it has been said; preserves all
;      registers
; -----------------------------------------------------------------------------
np_setmsg:
    push ax
    push si
    push di
    mov di, np_tbuf
.pre:
    mov al, [si]
    or al, al
    jz .name
    mov [di], al
    inc si
    inc di
    jmp short .pre
.name:
    mov si, np_name
.copy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    jmp short .copy
.done:
    mov ax, np_tbuf
    call np_saymsg              ; the kernel copies it, so np_tbuf is free to
    pop di                      ; be recomposed the moment this returns
    pop si
    pop ax
    ret

; =============================================================================
; THE SELECTION (SPEC.md 27.8)
;
; A range of character indices, [np_sel0], [np_sel1), and an inversion drawn
; over the cells that fall inside it. Two things make it nearly free.
;
; It is an XOR FILL, per row, applied by np_rflush right after the run that
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
; - so np_rflush keeps [np_prs0]/[np_prs1] the way it keeps [np_prcc], and
; folds the union of the old span and the new one into the cells it redraws.
;
; XOR is its own inverse, and that is the sharp edge here. A cell the run did
; NOT redraw still carries the inversion the last pass gave it, so inverting
; it a second time would take it away - which is why np_selxor intersects the
; row's selected span with [np_flo]..[np_fhi], the cells actually written.
; =============================================================================

; -----------------------------------------------------------------------------
; np_selq - is character index AX inside the selection?
; in:  AX = a character index; out: CF = 1 if it is; preserves all registers
; -----------------------------------------------------------------------------
np_selq:
    cmp byte [np_selon], 0
    je .no
    cmp ax, [np_sel0]
    jb .no
    cmp ax, [np_sel1]
    jae .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; np_selqo - was character index AX selected in the selection ON SCREEN?
; in:  AX; out: CF = 1 if it was; preserves all registers
; -----------------------------------------------------------------------------
np_selqo:
    cmp byte [np_oselon], 0
    je .no
    cmp ax, [np_osel0]
    jb .no
    cmp ax, [np_osel1]
    jae .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; np_xfold - widen the row's CHANGED-inversion span to include column AX
; in:  AX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_xfold:
    cmp word [np_xs0], 0xFFFF
    jne .lo
    mov [np_xs0], ax
    mov [np_xs1], ax
    ret
.lo:
    cmp ax, [np_xs0]
    jae .hi
    mov [np_xs0], ax
.hi:
    cmp ax, [np_xs1]
    jbe .out
    mov [np_xs1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; np_selfold - widen the row's inverted span to include cell column AX
; in:  AX = a column; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_selfold:
    cmp word [np_rs0], 0xFFFF
    jne .lo
    mov [np_rs0], ax
    mov [np_rs1], ax
    ret
.lo:
    cmp ax, [np_rs0]
    jae .hi
    mov [np_rs0], ax
.hi:
    cmp ax, [np_rs1]
    jbe .out
    mov [np_rs1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; np_selxor - invert the selected cells of the row just drawn
; in:  [np_rs0]/[np_rs1] = the row's selected span (0xFFFF = none),
;      [np_flo]/[np_fhi] = the cells the run actually wrote, [np_rby],
;      [np_tx]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_selxor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [np_rs0]
    cmp ax, 0xFFFF
    je .out
    mov cx, [np_rs1]
    cmp ax, [np_flo]            ; the intersection, and nothing wider: a cell
    jae .l0                     ; the run did not touch is still carrying the
    mov ax, [np_flo]            ; inversion the last pass gave it, and a
.l0:                            ; second XOR would take it back off
    cmp cx, [np_fhi]
    jbe .l1
    mov cx, [np_fhi]
.l1:
    cmp ax, cx
    ja .out
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_tx]             ; AX = x1
    push ax
    mov ax, cx
    inc ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_tx]
    dec ax
    mov cx, ax                  ; CX = x2, the last column of the last cell
    pop ax
    mov bx, [np_rby]
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_selclr - there is no selection any more
; out: CF = 1 there was none to clear (so nothing needs redrawing);
;      preserves all registers
; -----------------------------------------------------------------------------
np_selclr:
    cmp byte [np_selon], 0
    je .none
    mov byte [np_selon], 0
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; np_selset - select [AX, DX), in either order
; in:  AX, DX = two character indices; out: nothing; preserves all registers
; An empty range is no selection at all, which is what makes "click" and
; "drag back to where you started" the same thing.
; -----------------------------------------------------------------------------
np_selset:
    push ax
    push dx
    cmp ax, dx
    jbe .ord
    xchg ax, dx
.ord:
    cmp ax, dx
    je .none
    mov [np_sel0], ax
    mov [np_sel1], dx
    mov byte [np_selon], 1
    jmp short .out
.none:
    mov byte [np_selon], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_selget - the live selection
; out: CF = 1 there is none; else CF = 0 with AX = its start and CX = its
;      length; preserves all other registers
; -----------------------------------------------------------------------------
np_selget:
    cmp byte [np_selon], 0
    je .no
    mov ax, [np_sel0]
    cmp ax, [np_len]
    jae .no                     ; the note is SHORTER than the selection now
    mov cx, [np_sel1]
    cmp cx, [np_len]            ; ...and the far end is clamped rather than
    jbe .end                    ; refused, so a shrink leaves the part of the
    mov cx, [np_len]            ; selection that still exists selected
.end:
    cmp cx, ax
    jbe .no                     ; BELOW, not just equal: an inverted pair here
                                ; would make `sub` answer ~65,000 and hand
                                ; that to whoever asked - clip_put refuses it,
                                ; but np_rev would swap bytes clean off the
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
; np_seldel - delete the selection; the caret lands where it began
; out: CF = 1 there was none; preserves all registers
; -----------------------------------------------------------------------------
np_seldel:
    push ax
    push bx
    push cx
    call np_selget
    jc .no
    mov bx, ax
    call np_delspan
    mov [np_cur], bx
    call np_selclr
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
; np_ins and np_del were the whole edit surface, and both were "one character
; at the caret". Cut, Paste, Replace and Undo all move runs, so the run is the
; primitive now and the two old routines are cases of it.
;
; Both record undo BEFORE they move anything (SPEC.md 27.9) - a deletion has
; to reach the undo blob while its bytes are still in the note - and both
; leave [np_cur] alone. Where the caret goes afterwards is the caller's
; decision and differs for every one of them.
; =============================================================================

; -----------------------------------------------------------------------------
; np_capfor - make the document claim hold AX bytes
; in:  AX = the bytes wanted
; out: CF = 0 and [np_cap] >= AX; CF = 1 refused. Preserves all registers.
; -----------------------------------------------------------------------------
np_capfor:
    push ax
    push bx
    push cx
    mov bx, ax
    cmp bx, [np_cap]
    jbe .yes
    mov ax, bx
    add ax, 1023
    jc .no                      ; past 64KB, which NP_MAXKB refuses anyway
    mov cl, 10
    shr ax, cl
    call np_resize              ; clamps to NP_MAXKB and may be refused
    jc .no
    cmp bx, [np_cap]
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
; np_delspan - remove CX bytes at index BX
; in:  BX = the first index to go, CX = how many (both clamped to the note)
; out: nothing; preserves all registers. [np_len] shrinks; [np_cur] is the
;      caller's business.
; -----------------------------------------------------------------------------
np_delspan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [np_len]
    cmp bx, ax
    jae .out
    sub ax, bx
    cmp cx, ax
    jbe .have
    mov cx, ax
.have:
    jcxz .out
    call np_hmark
    mov ax, bx
    call np_urec_del            ; while the bytes are still here to be copied
    mov es, [np_dseg]
    mov si, bx
    add si, cx                  ; the first byte that survives
    mov di, bx
    mov dx, [np_len]
    sub dx, si                  ; ...and how many of them there are
    push cx                     ; the SPAN, which .close still needs
    mov cx, dx
    jcxz .nomv
    push ds                     ; forwards here: the gap closes DOWNWARD, so
    mov ds, [np_dseg]           ; DI trails SI (SPEC.md 27.12)
    cld
    rep movsb
    pop ds
.nomv:
    pop cx
.close:
    mov ax, [np_len]
    sub ax, cx
    mov [np_len], ax
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
; np_gaproom - open a CX-byte gap at index BX for the caller to fill
; in:  BX = where, CX = how many
; out: CF = 0 and [np_len] already counts the gap; CF = 1 refused and nothing
;      moved. Preserves all registers.
;
; It does NOT record undo: the caller knows what it is about to put there and
; how much of it survives, and a paste's filter can shorten it afterwards.
; -----------------------------------------------------------------------------
np_gaproom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    jcxz .ok
    mov ax, [np_len]
    add ax, cx
    jc .no                      ; a 16-bit note cannot pass 65535
    call np_capfor
    jc .no
    mov es, [np_dseg]
    mov si, [np_len]
    dec si                      ; the last live byte
    mov di, si
    add di, cx
    mov dx, [np_len]
    sub dx, bx                  ; the bytes to the right of the gap
    push cx                     ; the GAP width, which .done still needs
    mov cx, dx
    jcxz .nomv
    push ds                     ; backwards: np_ins's case with a gap wider
    mov ds, [np_dseg]           ; than one byte (SPEC.md 27.12)
    std
    rep movsb
    cld
    pop ds
.nomv:
    pop cx
.done:
    mov ax, [np_len]
    add ax, cx
    mov [np_len], ax
    call np_hmark
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
; np_editinv - the layout state is all indices into a buffer that just moved
; out: nothing; preserves all registers
;
; The lighter half of np_clamp: it does NOT put the view back at the top,
; because a paste, a replace and an undo all happen where the user is looking
; and scrolling away from that would be the opposite of helpful. What it must
; do is drop the checkpoint and np_rows - np_redraw seeds the next walk from
; one of them, and both are character indices whose row a bulk edit above the
; view has just renamed.
; -----------------------------------------------------------------------------
np_editinv:
    push ax
    call np_hmark
    mov byte [np_ckok], 0
    mov byte [np_rowsok], 0
    mov byte [np_fcok], 0       ; ...and the match count counted the old note
    mov byte [np_fcdirty], 1
    mov word [np_fmno], 0
    mov ax, [np_len]
    cmp [np_cur], ax
    jbe .cur
    mov [np_cur], ax
.cur:
    cmp byte [np_selon], 0      ; the SELECTION is a pair of indices into the
    je .out                     ; same buffer and it was clamped nowhere: a
    cmp [np_sel1], ax           ; Replace All that shortens the note, or an
    jbe .out                    ; undo of a paste, leaves it pointing past the
    mov [np_sel1], ax           ; end. np_selget clamps too - this is the
    mov ax, [np_sel0]           ; other half, so the stored pair is never a
    cmp ax, [np_sel1]           ; lie in the first place
    jb .out
    mov byte [np_selon], 0      ; nothing of it survives
.out:
    pop ax
    ret

; =============================================================================
; Cut, Copy and Paste over the system clipboard (SPEC.md 55/27.8)
; =============================================================================

; -----------------------------------------------------------------------------
; np_copy - put the selection on the clipboard
; out: CF = 1 nothing was selected, or the clipboard refused (and then the
;      toast says so); preserves all registers
; -----------------------------------------------------------------------------
np_copy:
    push ax
    push bx
    push cx
    push si
    push es
    call np_selget              ; AX = start, CX = length
    jc .no
    mov si, ax
    mov es, [np_dseg]           ; the note is a claim of its own, which is
    call OSAPI_CLIP_PUT         ; exactly why the slot takes a far pointer
    jc .full
    clc
    jmp short .out
.full:
    mov ax, np_e_cbig
    call np_saymsg
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
; np_cut - copy the selection, then take it out
; out: CF = 1 nothing happened; preserves all registers
; The copy comes first and its refusal is final: a cut that lost the text
; because the clipboard would not hold it is the one outcome nobody can undo
; from the keyboard.
; -----------------------------------------------------------------------------
np_cut:
    call np_copy
    jc .out
    call np_seldel
    call np_editinv
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; np_paste - replace the selection with the clipboard's text
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
; 32..126 is dropped - exactly np_load's rule, for exactly np_load's reason.
; -----------------------------------------------------------------------------
np_paste:
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
    call np_seldel              ; a paste REPLACES the selection - and the two
                                ; halves land in ONE undo group, because the
                                ; insert starts exactly where the delete left
    mov bx, [np_cur]
    mov cx, dx
    call np_gaproom
    jc .nomem
    mov es, [np_dseg]
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
    mov cx, [np_len]
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
    mov ax, [np_len]
    sub ax, dx
    add ax, cx
    mov [np_len], ax            ; the gap was n; only CX of it is text
    mov ax, bx
    call np_urec_ins            ; AX = where, CX = how many
    add bx, cx
    mov [np_cur], bx
    call np_editinv
    clc
    jmp short .out
.nomem:
    mov ax, np_e_nomem
    call np_saymsg
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
; A record is three numbers and a blob: at [np_upos], this group INSERTED
; [np_uins] bytes and REMOVED the [np_udel] bytes now sitting at [np_uoff] in
; the arena. Undoing it is "delete uins at upos, put the blob back at upos",
; and that is the whole of it - which is also why there is no redo: the
; inverse record would need the bytes that are being taken back out, and
; nobody asked for one.
;
; The blobs live in a HEAP CLAIM sized on demand: nothing at all until the
; first edit worth remembering, a kilobyte at a time after that, and up to
; NP_UMAXKB. When it will not grow, the OLDEST record is evicted - a stack
; four deep is still an undo, and a refusal is not - and only when there is
; nothing left to evict is the whole stack dropped. It has to be the whole
; stack: an edit that went unrecorded makes every record under it describe a
; note that no longer exists, and applying one would corrupt the document.
; =============================================================================

; -----------------------------------------------------------------------------
; np_uclear - forget every record and give the arena back
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_uclear:
    push ax
    push bx
    push dx
    mov byte [np_un], 0
    mov byte [np_uopen], 0
    mov word [np_utop], 0
    mov dx, [np_useg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE         ; the owner is our segment, which the slot's
    mov word [np_useg], 0       ; X stub supplies (SPEC.md 50.3)
    mov word [np_ukb], 0
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_uclose - the open group is finished; the next edit starts a new one
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_uclose:
    mov byte [np_uopen], 0
    ret

; -----------------------------------------------------------------------------
; np_utop_rec - the newest record, if one is open
; out: CF = 1 there is none; else CF = 0 and SI = its word offset (every
;      array below is indexed by it). Clobbers BX and SI.
; -----------------------------------------------------------------------------
np_utop_rec:
    cmp byte [np_uopen], 0
    je .no
    mov bl, [np_un]
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
; np_ugrow - make the arena AX kilobytes
; in:  AX = KB; out: CF = 1 refused; preserves all registers
; -----------------------------------------------------------------------------
np_ugrow:
    push ax
    push bx
    push dx
    cmp ax, [np_ukb]
    jbe .yes
    cmp word [np_useg], 0
    jne .re
    call OSAPI_MEM_CLAIM        ; out CF = 0, DX = the base
    jc .no
    mov [np_useg], dx
    mov [np_ukb], ax
    push ax                     ; the arena moves too (SPEC.md 66.5.7): the
    mov ax, np_reloc            ; undo records index it by OFFSET, so np_reloc
    call OSAPI_MEM_MOVABLE      ; has one word to fix here as well
    pop ax
    jmp short .yes
.re:
    mov dx, [np_useg]
    call OSAPI_MEM_REGROW       ; out CF = 0, DX = the base NOW - a grow that
    jc .no                      ; had to move reports a new one (50.3.1)
    mov [np_useg], dx
    mov [np_ukb], ax
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
; np_udrop0 - forget the OLDEST record, sliding the arena down under it
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_udrop0:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp byte [np_un], 0
    je .out
    mov cx, [np_udel]           ; record 0's blob, which starts at offset 0
    or cx, cx
    jz .arrays
    mov es, [np_useg]
    mov si, cx
    xor di, di
    mov bx, [np_utop]
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
    mov ax, [np_utop]
    sub ax, cx
    mov [np_utop], ax
.arrays:
    xor bx, bx                  ; shift the four arrays down one, and every
.sh:                            ; surviving blob offset with them
    mov al, [np_un]
    xor ah, ah
    dec ax
    cmp bx, ax
    jae .last
    mov si, bx
    shl si, 1
    mov ax, [si+np_upos+2]
    mov [si+np_upos], ax
    mov ax, [si+np_uins+2]
    mov [si+np_uins], ax
    mov ax, [si+np_udel+2]
    mov [si+np_udel], ax
    mov ax, [si+np_uoff+2]
    sub ax, cx
    mov [si+np_uoff], ax
    inc bx
    jmp short .sh
.last:
    dec byte [np_un]
    cmp byte [np_un], 0
    jne .out
    mov byte [np_uopen], 0      ; the record that went WAS the open one, and
                                ; np_utop_rec has to say so - every blob
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
; np_uroom - make sure CX more blob bytes fit
; in:  CX = the bytes wanted
; out: CF = 0 there is room; CF = 1 there is not and the whole stack has been
;      dropped. Preserves all registers.
; -----------------------------------------------------------------------------
np_uroom:
    push ax
    push bx
    push cx
    push dx
.try:
    mov ax, [np_utop]
    add ax, cx
    jc .evict
    mov bx, [np_ukb]
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
    cmp ax, NP_UMAXKB
    ja .evict
    call np_ugrow
    jnc .yes
.evict:
    cmp byte [np_un], 0
    je .no
    call np_udrop0
    jmp short .try
.no:
    call np_uclear
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
; np_ublob_copy - CX note bytes at SI into the arena at ES:DI
; in:  SI = document offset, DI = arena offset, CX = count, ES = [np_useg]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_ublob_copy:
    push ax
    push cx
    push si
    push di
    push ds
    mov ax, [np_dseg]           ; read it BEFORE DS stops being ours
    mov ds, ax
    cld
    rep movsb
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_ublob_app - append the CX note bytes at AX to the open record's blob
; in:  AX = document position, CX = count
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_ublob_app:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call np_uroom               ; may evict, may drop the lot
    jc .out
    call np_utop_rec            ; re-asked, because an eviction renumbers the
    jc .out                     ; records and may have taken this one
    push si
    mov di, [np_utop]
    mov si, ax
    mov es, [np_useg]
    call np_ublob_copy
    pop si
    mov ax, [np_utop]
    add ax, cx
    mov [np_utop], ax
    mov ax, [si+np_udel]
    add ax, cx
    mov [si+np_udel], ax
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_ublob_pre - put the CX note bytes at AX at the FRONT of the open blob,
;                and start the group CX bytes lower
; in:  AX = document position, CX = count
; out: nothing; preserves all registers
;
; The open record's blob is always the LAST one in the arena, so sliding it up
; disturbs nothing else - which is the whole reason the arena is a stack.
; -----------------------------------------------------------------------------
np_ublob_pre:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call np_uroom
    jc .out
    call np_utop_rec
    jc .out
    mov dx, [si+np_udel]        ; the blob's current length...
    mov bx, [si+np_uoff]        ; ...and where it starts
    add [si+np_udel], cx
    sub [si+np_upos], cx        ; the group's span starts CX lower now
    mov es, [np_useg]
    push ax
    mov ax, [np_utop]
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
    pop ax
    mov si, ax
    mov di, bx
    call np_ublob_copy
    mov ax, [np_utop]
    add ax, cx
    mov [np_utop], ax
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
; np_ubegin - close whatever is open and start a record at position AX
; in:  AX = the group's start; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_ubegin:
    push ax
    push bx
    push si
    cmp byte [np_un], NP_UNDO
    jb .have
    call np_udrop0              ; five deep means the sixth costs the first
.have:
    mov bl, [np_un]
    xor bh, bh
    mov si, bx
    shl si, 1
    mov [si+np_upos], ax
    mov word [si+np_uins], 0
    mov word [si+np_udel], 0
    mov ax, [np_utop]
    mov [si+np_uoff], ax
    inc byte [np_un]
    mov byte [np_uopen], 1
    call np_hire                ; the group's half-second is measured by the
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
; np_urec_ins - record an insertion of CX bytes at AX
; in:  AX = where, CX = how many; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_urec_ins:
    push ax
    push bx
    push si
    cmp byte [np_unolog], 0
    jne .out
    jcxz .out
    call np_utop_rec
    jc .new
    mov bx, [si+np_upos]
    add bx, [si+np_uins]
    cmp bx, ax
    jne .new                    ; not where this group left off: a new one
    add [si+np_uins], cx
    jmp short .out
.new:
    call np_ubegin
    call np_utop_rec
    jc .out
    mov [si+np_uins], cx
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_urec_del - record the removal of CX bytes at AX, copying them into the
;               arena while they are still in the note
; in:  AX = where, CX = how many; out: nothing; preserves all registers
;
; Four cases, tested in this order, and the order matters:
;   1. the bytes are ones THIS group inserted - they were never in the note
;      before it, so nothing reaches the blob and [np_uins] simply shrinks.
;      This is a backspace walking back over what was just typed;
;   2. they sit immediately AFTER the group's span - original text, so they
;      belong at the END of the blob. This is forward Delete while typing;
;   3. immediately BEFORE it - a backspace walking left out of the group - so
;      they belong at the FRONT of it and the span starts lower;
;   4. anywhere else: a new group.
; Case 1 has to come first because when [np_uins] is 0 cases 2 and 3 can both
; look true, and taking 1 when it does not apply would forget a deletion.
; -----------------------------------------------------------------------------
np_urec_del:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [np_unolog], 0
    jne .out
    jcxz .out
    call np_utop_rec
    jc .new
    mov dx, [si+np_upos]
    add dx, [si+np_uins]        ; DX = one past the group's span
    mov bx, ax
    add bx, cx                  ; BX = one past the deletion
    cmp bx, dx
    jne .try2
    cmp cx, [si+np_uins]
    ja .try2
    sub [si+np_uins], cx        ; 1.
    jmp short .out
.try2:
    cmp ax, dx
    jne .try3
    call np_ublob_app           ; 2.
    jmp short .out
.try3:
    cmp bx, [si+np_upos]
    jne .new
    call np_ublob_pre           ; 3.
    jmp short .out
.new:
    call np_ubegin              ; 4. AX is still the position
    call np_ublob_app
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_urec_bulk - record a whole-span replacement, for the operations that
;                rewrite more than one place at once
; in:  AX = where the change begins, CX = the bytes there NOW that are about
;      to be replaced (Replace All passes the whole tail of the note)
; out: CF = 1 = it could not be recorded, and the stack has been dropped;
;      preserves all registers
;
; Drag-and-drop and Replace All both move text at two positions at once, which
; the (pos, inserted, removed) record cannot say. Saying it as ONE replacement
; of everything between them can, exactly, at the price of a bigger blob - and
; NP_UMAXKB is 16 so that a Replace All over a full note still fits.
; np_urec_bulkend closes it with the new length.
; -----------------------------------------------------------------------------
np_urec_bulk:
    push ax
    push bx
    push cx
    push si
    call np_uclose              ; a bulk change is never part of a typing run
    call np_ubegin
    call np_ublob_app
    call np_utop_rec
    jc .no
    cmp [si+np_udel], cx        ; np_ublob_app is allowed to give up
    jne .no
    clc
    jmp short .out
.no:
    call np_uclear
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; np_urec_bulkend - in: CX = the bytes that replaced them. Preserves all.
np_urec_bulkend:
    push bx
    push si
    call np_utop_rec
    jc .out
    mov [si+np_uins], cx
    call np_uclose
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_undo - take the newest group back (SPEC.md 27.9)
; out: CF = 1 there was nothing to undo; preserves all registers
; -----------------------------------------------------------------------------
np_undo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call np_uclose
    cmp byte [np_un], 0
    je .no
    mov bl, [np_un]
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, bx
    mov byte [np_unolog], 1     ; an undo is not an edit to be remembered
    mov ax, [si+np_upos]
    mov cx, [si+np_uins]
    mov di, [si+np_uoff]
    mov dx, [si+np_udel]
    push dx                     ; the blob's length, wanted twice below
    push di                     ; ...and where it is
    push ax                     ; ...and where all of this happens
    jcxz .noins
    mov bx, ax
    call np_delspan             ; the group's insertion comes back out
.noins:
    pop ax
    pop di
    pop dx
    push ax
    mov cx, dx
    jcxz .nodel
    mov bx, ax
    call np_gaproom             ; ...and the bytes it removed go back in
    jc .fail
    mov si, di
    mov di, bx
    push ds
    push es
    mov es, [np_dseg]           ; both segments loaded while DS is still ours
    mov ax, [np_useg]
    mov ds, ax
    cld
    rep movsb
    pop es
    pop ds
.nodel:
    pop ax
    add ax, dx                  ; the caret lands at the end of what came back
    mov [np_cur], ax
    call np_selclr
    mov ax, [np_utop]
    sub ax, dx
    mov [np_utop], ax           ; the blob is the arena's last, so this is all
    dec byte [np_un]            ; there is to giving it back
    mov byte [np_uopen], 0
    mov byte [np_unolog], 0
    call np_editinv
    clc
    jmp short .out
.fail:
    pop ax                      ; the gap was refused: the record stays where
    mov byte [np_unolog], 0     ; it is and the note is short of its insertion,
    call np_editinv             ; which is the honest half of an undo rather
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
; The regular-expression matcher (SPEC.md 27.10.1)
;
; A backtracking matcher over the note, supporting the subset a text editor
; actually uses: any character is itself, `.` is any character but a line
; break, `[abc]` / `[^abc]` / `[a-z]` are classes, `*` `+` `?` repeat the
; element before them, `^` and `$` anchor to the start and end of a LINE (this
; is a multi-line document, so anchoring to the note would be useless), and
; `\` makes the next character literal.
;
; IT IS ITERATIVE, WITH AN EXPLICIT STACK, and that is not a style choice. The
; textbook matcher recurses once per repeat element and once per repetition;
; the second is fatal on its own (`.*` over a 16KB note is 16,000 frames) and
; the first is fatal here too, because the match COUNT is recomputed by the
; worker task and a worker's stack is 256 bytes (SPEC.md 8). So repetition is
; a greedy count plus a frame that gives ground one at a time, the frames live
; in bss, and there are NP_RXST of them - a pattern needing more is refused
; whole, by np_fchk, before anything runs.
;
; Matching is CASE SENSITIVE, deliberately. Folding would have to fold class
; ranges too, and `[a-z]` quietly matching `Q` is a worse surprise than
; retyping a capital.
; =============================================================================

; -----------------------------------------------------------------------------
; np_docb - AL = the note's byte at index DI
; np_docb2 - ...and at index AX
; both preserve every other register
; -----------------------------------------------------------------------------
np_docb:
    push bx
    push es
    mov es, [np_dseg]
    mov bx, di
    mov al, [es:bx]
    pop es
    pop bx
    ret
np_docb2:
    push bx
    push es
    mov es, [np_dseg]
    mov bx, ax
    mov al, [es:bx]
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_rx_next - SI = the offset just past the element at SI
; in/out: SI; preserves every other register
; -----------------------------------------------------------------------------
np_rx_next:
    push ax
    mov al, [si]
    cmp al, '\'
    jne .nesc
    inc si
    cmp byte [si], 0
    je .out
    inc si
    jmp short .out
.nesc:
    cmp al, '['
    jne .one
    inc si
    cmp byte [si], '^'
    jne .c0
    inc si
.c0:
    cmp byte [si], ']'          ; a ']' first is a literal ']', the convention
    jne .cl
    inc si
.cl:
    mov al, [si]
    or al, al
    jz .out                     ; unterminated: the element is the rest of it
    cmp al, ']'
    je .close
    cmp al, '\'
    jne .cnext
    inc si
    cmp byte [si], 0
    je .out
.cnext:
    inc si
    jmp short .cl
.close:
    inc si
    jmp short .out
.one:
    inc si
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_rx_ch - does the element at SI match the character in AL?
; in:  SI = a pattern offset, AL = a character
; out: CF = 1 = yes; preserves all registers
; -----------------------------------------------------------------------------
np_rx_ch:
    push ax
    push bx
    push cx
    push si
    mov bl, [si]
    cmp bl, '.'
    jne .notdot
    cmp al, 13                  ; '.' stops at a line break, which is what
    je .no                      ; makes '.*' mean "the rest of this line"
    jmp .yes                    ; NOT `short`: NP_MAXCOL - 1 stopped fitting a
                                ; sign-extended imm8 at 171, so every compare
                                ; against it grew a byte and this went out of
                                ; range
.notdot:
    cmp bl, '['
    je .class
    cmp bl, '\'
    jne .lit
    inc si
    mov bl, [si]
    or bl, bl
    jz .no                      ; a trailing backslash matches nothing
.lit:
    cmp al, bl
    je .yes
    jmp short .no
.class:
    inc si
    xor ch, ch
    cmp byte [si], '^'
    jne .cl
    mov ch, 1
    inc si
.cl:
    xor cl, cl
.clp:
    mov bl, [si]
    or bl, bl
    jz .cldone
    cmp bl, ']'
    je .cldone
    cmp bl, '\'
    jne .nocesc
    inc si
    mov bl, [si]
    or bl, bl
    jz .cldone
    jmp short .clone
.nocesc:
    cmp byte [si+1], '-'        ; a range, unless the '-' is the class's last
    jne .clone
    cmp byte [si+2], ']'
    je .clone
    cmp byte [si+2], 0
    je .clone
    cmp al, bl
    jb .clskip
    cmp al, [si+2]
    ja .clskip
    mov cl, 1
.clskip:
    add si, 2
    jmp short .clnext
.clone:
    cmp al, bl
    jne .clnext
    mov cl, 1
.clnext:
    inc si
    jmp short .clp
.cldone:
    or ch, ch
    jz .clyes
    xor cl, 1                   ; a negated class
.clyes:
    or cl, cl
    jnz .yes
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_rx_push - stack a repetition frame
; in:  BX = the quantifier's offset (the pattern resumes at BX+1), DI = the
;      text index the repeat started at, DX = the fewest it may keep,
;      CX = how many it is keeping now
; out: CF = 1 = NP_RXST frames are already in use; preserves all registers
; -----------------------------------------------------------------------------
np_rx_push:
    push ax
    push cx
    push si
    mov ax, [np_rxsp]
    cmp ax, NP_RXST
    jae .no
    mov si, ax
    shl si, 1                   ; *8 by hand: CX is an ARGUMENT here, so CL
    shl si, 1                   ; is not available to shift with
    shl si, 1
    add si, np_rxs
    mov ax, bx
    inc ax
    mov [si], ax
    mov [si+2], di
    mov [si+4], dx
    mov [si+6], cx
    inc word [np_rxsp]
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_rx_top - read the newest frame
; out: SI = the frame, BX = the resume offset, AX = the text base, DX = the
;      fewest it may keep, CX = how many it is keeping. Preserves nothing but
;      DI. Only legal with [np_rxsp] nonzero.
; -----------------------------------------------------------------------------
np_rx_top:
    mov si, [np_rxsp]
    dec si
    mov cl, 3
    shl si, cl
    add si, np_rxs
    mov bx, [si]
    mov ax, [si+2]
    mov dx, [si+4]
    mov cx, [si+6]
    ret

; -----------------------------------------------------------------------------
; np_rx_at - does the pattern match the note at index AX?
; in:  AX = the index; the pattern in np_fpat, [np_frx] = regex or literal
; out: CF = 0 with [np_rxend] = one past the match; CF = 1 = no match HERE
;      (this asks about one position - np_findfrom is what walks)
;      Preserves all registers.
; -----------------------------------------------------------------------------
np_rx_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [np_frx], 0
    jne .rx

    mov si, np_fpat             ; --- literal ------------------------------
    mov di, ax
.ll:
    mov bl, [si]
    or bl, bl
    jz .match
    mov ax, di
    cmp ax, [np_len]
    jae .fail
    call np_docb
    cmp al, bl
    jne .fail
    inc si
    inc di
    jmp short .ll

.rx:                            ; --- regular expression -------------------
    mov word [np_rxsp], 0
    mov si, np_fpat
    mov di, ax
    cmp byte [si], '^'
    jne .step
    inc si                      ; anchored to the start of a LINE
    or ax, ax
    jz .step
    dec ax
    call np_docb2
    cmp al, 13
    jne .fail

.step:
    mov bl, [si]
    or bl, bl
    jz .match
    cmp bl, '$'
    jne .elem
    cmp byte [si+1], 0
    jne .elem
    mov ax, di                  ; '$' last: the end of the note, or a line
    cmp ax, [np_len]            ; break just ahead of us
    je .match
    call np_docb
    cmp al, 13
    je .match
    jmp .back

.elem:
    push si                     ; the element's own offset
    call np_rx_next
    mov bl, [si]                ; BL = what follows it
    cmp bl, '*'
    je .quant
    cmp bl, '+'
    je .quant
    cmp bl, '?'
    je .quant
    pop si                      ; a plain element: one character or nothing
    mov ax, di
    cmp ax, [np_len]
    jae .back
    call np_docb
    call np_rx_ch
    jnc .back
    inc di
    call np_rx_next
    jmp short .step

.quant:
    mov bx, si                  ; BX = the quantifier
    pop si                      ; SI = the element it applies to
    xor cx, cx                  ; greedy first: take as many as there are
.gr:
    mov ax, di
    add ax, cx
    cmp ax, [np_len]
    jae .grdone
    push cx
    call np_docb2
    call np_rx_ch
    pop cx
    jnc .grdone
    inc cx
    cmp cx, 0x7FF0
    jb .gr
.grdone:
    mov al, [bx]
    cmp al, '?'
    jne .q2
    cmp cx, 1
    jbe .q2
    mov cx, 1                   ; '?' is at most one
.q2:
    xor dx, dx
    cmp al, '+'
    jne .q3
    mov dx, 1                   ; '+' is at least one
.q3:
    cmp cx, dx
    jb .back
    call np_rx_push             ; a frame, so this can give ground later
    jc .fail                    ; NP_RXST exceeded: np_fchk should have said
    add di, cx
    mov si, bx
    inc si
    jmp .step

.back:
    cmp word [np_rxsp], 0
    je .fail
    call np_rx_top              ; SI = the frame, and nothing below moves it
    or cx, cx
    jz .pop
    dec cx
    cmp cx, dx
    jb .pop
    mov [si+6], cx              ; keep one fewer and try the rest again
    mov si, bx
    mov di, ax
    add di, cx
    jmp .step
.pop:
    dec word [np_rxsp]
    jmp short .back

.match:
    mov [np_rxend], di
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

; -----------------------------------------------------------------------------
; np_fchk - can this pattern be run at all?
; out: [np_fbad] set when it cannot; preserves all registers
;
; One reason only: more repetition elements than NP_RXST frames. Refusing here
; rather than mid-match is what lets np_rx_at treat a full stack as impossible
; and lets the panel say so once instead of finding nothing forever.
; -----------------------------------------------------------------------------
np_fchk:
    push ax
    push cx
    push si
    mov byte [np_fbad], 0
    cmp byte [np_frx], 0
    je .out
    xor cx, cx
    mov si, np_fpat
.l:
    cmp byte [si], 0
    je .done
    call np_rx_next
    mov al, [si]
    cmp al, '*'
    je .q
    cmp al, '+'
    je .q
    cmp al, '?'
    jne .l
.q:
    inc si
    inc cx
    jmp short .l
.done:
    cmp cx, NP_RXST
    jbe .out
    mov byte [np_fbad], 1
.out:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; Finding, and replacing (SPEC.md 27.10)
; =============================================================================

; -----------------------------------------------------------------------------
; np_findfrom - the first match at or after index AX, wrapping to the top
; in:  AX = where to start looking
; out: CF = 0 with AX = the match's start and DX = one past its end;
;      CF = 1 = the pattern is not in the note at all. Preserves all others.
;
; The wrap is unconditional and is the whole of what "loops to the top" means:
; the second pass stops where the first began, so a pattern that occurs once
; is found from anywhere and one that occurs nowhere is refused after exactly
; one traversal.
; -----------------------------------------------------------------------------
np_findfrom:
    push bx
    push cx
    push si
    push di
    cmp word [np_fpatn], 0
    je .no
    cmp byte [np_fbad], 0
    jne .no
    mov bx, ax
    mov cx, [np_len]
    mov byte [np_fwrap], 0
.p1:
    cmp ax, cx
    ja .wrap
    call np_rx_at
    jnc .hit
    inc ax
    jmp short .p1
.wrap:
    mov byte [np_fwrap], 1      ; past the end and back to the top: whatever
    xor ax, ax                  ; this finds is match 1 (SPEC.md 27.10)
.p2:
    cmp ax, bx
    jae .no
    call np_rx_at
    jnc .hit
    inc ax
    jmp short .p2
.hit:
    mov dx, [np_rxend]
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
; np_findprev - the last match that STARTS before index AX, wrapping to the
;               bottom of the note
; in:  AX = the limit
; out: as np_findfrom
;
; One forward walk, remembering the last match seen before the limit and the
; last one seen at all - because a backtracking matcher cannot be run
; backwards, and running it forwards twice would cost twice as much. It steps
; match-to-match rather than character-to-character, so the pass is bounded by
; the number of matches and not by the length of the note.
; -----------------------------------------------------------------------------
np_findprev:
    push bx
    push cx
    push si
    push di
    cmp word [np_fpatn], 0
    je .no
    cmp byte [np_fbad], 0
    jne .no
    mov bx, ax                  ; BX = the limit
    mov byte [np_fwrap], 0
    mov cx, 0xFFFF              ; CX = the best answer before it...
    mov si, 0xFFFF              ; SI = ...and the last one anywhere
    xor ax, ax
.l:
    cmp ax, [np_len]
    ja .done
    call np_rx_at
    jc .step1
    mov si, ax
    cmp ax, bx
    jae .adv
    mov cx, ax
.adv:
    mov di, [np_rxend]          ; matches are counted without overlapping
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
    mov byte [np_fwrap], 1      ; nothing before the limit: wrap to the last
    mov cx, si                  ; match in the note, which is match [np_fcount]
    cmp cx, 0xFFFF
    je .no
.have:
    mov ax, cx
    call np_rx_at               ; re-run it for its end, which is one match's
    jc .no                      ; worth of work rather than a second array
    mov dx, [np_rxend]
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
; np_fcount_do - count the pattern's non-overlapping matches
; out: [np_fcount] and [np_fcok]; preserves all registers
;
; Called by the WORKER, half a second after the typing stops, for exactly the
; reason np_height is (SPEC.md 27.7): this walks the whole note, and doing it
; on every keystroke in the find box is the cost that made the box unusable.
; It runs under the gfx lock because the matcher's backtrack stack is one
; block of bss shared with the UI task's own finds.
; -----------------------------------------------------------------------------
np_fcount_do:
    push ax
    push bx
    push cx
    push dx
    mov byte [np_fcdirty], 0
    mov byte [np_fcok], 1
    mov word [np_fmno], 0       ; re-derived below, from the same walk
    xor cx, cx
    cmp word [np_fpatn], 0
    je .done
    cmp byte [np_fbad], 0
    jne .done
    xor ax, ax
.l:
    cmp ax, [np_len]
    ja .done
    call np_rx_at
    jc .step1
    inc cx
    cmp byte [np_selon], 0      ; ...and this is where the ordinal comes from
    je .noord                   ; when nothing stepped it: the walk is already
    cmp ax, [np_fmst]           ; passing every match, so recognising the one
    jne .noord                  ; on screen costs a compare
    mov [np_fmno], cx
.noord:
    mov bx, [np_rxend]
    cmp bx, ax
    ja .setax
    mov bx, ax
    inc bx
.setax:
    mov ax, bx
    jmp short .l
.step1:
    inc ax
    jmp short .l
.done:
    mov [np_fcount], cx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_showmatch - select [AX, DX) and make sure it is on screen
; in:  AX = start, DX = end, SI = window ptr
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_showmatch:
    push ax
    push dx
    mov [np_fmst], ax
    mov [np_fmen], dx
    call np_selset
    mov [np_cur], dx            ; the caret sits at the END of it, so F3 twice
                                ; walks forwards rather than finding the same
                                ; match again
    mov byte [np_follow], 1     ; np_redraw scrolls it into view for us
    mov byte [np_ckok], 0       ; the checkpoint is the CARET's row start and
    pop dx                      ; the caret just jumped somewhere else
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_donext - F3: the next match after the caret
; np_doprev - Shift-F3: the last one before it
; in:  SI = window ptr; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
np_donext:
    call np_uclose
    mov ax, [np_cur]
    call np_findfrom
    jc .no
    call np_showmatch
    cmp byte [np_fwrap], 0      ; the ordinal is STEPPED, not counted: a
    je .step                    ; forward search from the end of match n lands
    mov word [np_fmno], 1       ; on n+1, and a wrap lands on 1. Counting it
    jmp short .draw             ; would walk the whole note per keypress,
.step:                          ; which is the cost 27.10 exists to avoid
    cmp word [np_fmno], 0
    je .draw                    ; not known: leave it for the worker to answer
    mov ax, [np_fmno]
    inc ax
    cmp ax, [np_fcount]
    jbe .set
    mov ax, [np_fcount]
.set:
    mov [np_fmno], ax
.draw:
    call np_pdrawn
    ret
.no:
    call np_fmiss
    call np_pdrawn
    ret

np_doprev:
    call np_uclose
    mov ax, [np_sel0]
    cmp byte [np_selon], 0
    jne .have
    mov ax, [np_cur]
.have:
    call np_findprev
    jc .no
    call np_showmatch
    cmp byte [np_fwrap], 0
    je .step
    mov ax, [np_fcount]         ; wrapped backwards: the LAST match
    mov [np_fmno], ax
    jmp short .draw
.step:
    cmp word [np_fmno], 0
    je .draw
    cmp word [np_fmno], 1
    jbe .draw
    dec word [np_fmno]
.draw:
    call np_pdrawn
    ret
.no:
    call np_fmiss
    call np_pdrawn
    ret

; np_fmiss - say why nothing happened. Preserves all registers.
np_fmiss:
    push ax
    mov ax, np_e_noent
    cmp word [np_fpatn], 0
    jne .say
    mov ax, np_m_nopat
.say:
    cmp byte [np_fbad], 0
    je .go
    mov ax, np_m_badpat
.go:
    call np_saymsg
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_dorepl - replace the match the selection is showing, then find the next
; in:  SI = window ptr; out: nothing; clobbers what a callback may
;
; It only replaces a selection that IS the current match, which is what makes
; the button honest: press Replace on a selection you made by hand and it
; finds the match instead of overwriting whatever you had highlighted.
; -----------------------------------------------------------------------------
np_dorepl:
    push ax
    push bx
    push cx
    push dx
    call np_uclose
    call np_fatmatch            ; only a selection that IS the current match
    jnc .find                   ; is replaced; one the user made by hand sends
    mov ax, [np_fmst]           ; us to find instead of overwriting it
    mov cx, [np_fmen]
    sub cx, ax
    call np_replat              ; CF = 1 = no room; DX = one past the new text
    jc .out
    mov ax, dx
    call np_selclr
    mov [np_cur], ax
    call np_editinv
    mov byte [np_fcdirty], 1
.find:
    mov ax, [np_cur]
    call np_findfrom
    jc .miss
    call np_showmatch
    jmp short .out
.miss:
    call np_fmiss
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_replat - swap the CX bytes at AX for the replacement text
; in:  AX = where, CX = how many bytes go
; out: CF = 0 with DX = one past the text that replaced them; CF = 1 refused
;      and nothing changed. Preserves all other registers.
; -----------------------------------------------------------------------------
np_replat:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, ax
                                ; THE ROOM COMES FIRST, and the order is the
                                ; whole of this routine's promise. np_delspan
                                ; used to run before np_gaproom, so a refused
                                ; claim returned CF=1 with the match ALREADY
                                ; GONE - which the header calls "nothing
                                ; changed". Reachable without a claim failure
                                ; at all: a note loaded at NP_MAXKB has
                                ; np_len == np_cap, so every replacement longer
                                ; than its match is refused, and np_dorepl's
                                ; `jc` then skips np_editinv with [np_cur] left
                                ; at the match END - past [np_len] for a match
                                ; at the end of the note, and the next
                                ; keystroke walks 65,535 bytes backwards
                                ; through the document claim.
    mov ax, [np_len]
    sub ax, cx                  ; what the note becomes: the span goes...
    add ax, [np_frepn]          ; ...and the replacement arrives
    jc .no                      ; a 16-bit note cannot pass 65,535
    push bx
    push cx
    call np_capfor              ; non-destructive, and preserves everything
    pop cx
    pop bx
    jc .no                      ; refused with the match still there
    push bx
    call np_delspan             ; out with the old...
    pop bx
    mov cx, [np_frepn]
    push cx
    call np_gaproom             ; ...and in with the new, which can no longer
    pop cx                      ; be refused for want of room
    jc .no
    mov es, [np_dseg]
    mov di, bx
    mov si, np_frep
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
    mov cx, [np_frepn]
    call np_urec_ins
    mov dx, bx
    add dx, [np_frepn]
    clc
    jmp short .out
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
; np_dorepall - replace every match, from the top of the note
; in:  SI = window ptr; out: nothing; clobbers what a callback may
;
; ONE undo record for the whole sweep, and it has to be one: the operation
; nobody wants to retype by hand is exactly the one worth being able to take
; back, and five separate records would only take back the last five. It is
; recorded as a replacement of everything from the first match to the end of
; the note (SPEC.md 27.9's bulk form), because that is the smallest span the
; three-number record can describe honestly.
; -----------------------------------------------------------------------------
np_dorepall:
    push ax
    push bx
    push cx
    push dx
    push di
    call np_uclose
    xor ax, ax
    call np_findfrom            ; is there anything to do at all?
    jc .miss
    push ax
    mov cx, [np_len]
    sub cx, ax
    call np_urec_bulk           ; the tail as it stands, into the blob
    pop ax
    mov byte [np_unolog], 1     ; every edit below is inside that one record
    xor bx, bx                  ; BX = how many were replaced
    mov di, ax                  ; DI = where the caret ends up
.l:
    cmp ax, [np_len]
    ja .done
    call np_rx_at
    jc .step
    mov cx, [np_rxend]
    sub cx, ax
    call np_replat              ; AX survives; DX = one past the new text
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
    mov byte [np_unolog], 0
    call np_urec_bulkend_at
    mov [np_cur], di
    call np_selclr
    call np_editinv
    mov byte [np_fcdirty], 1
    mov byte [np_follow], 1
    mov ax, bx
    call np_saycnt
    jmp short .out
.miss:
    call np_fmiss
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; np_urec_bulkend_at - close the bulk record: what replaced its span is
; everything from the record's own start to the end of the note. This is the
; Replace All shape, where the sweep runs to the end by construction.
; Preserves all registers.
np_urec_bulkend_at:
    push bx
    push cx
    push si
    call np_utop_rec
    jc .out
    mov cx, [np_len]
    sub cx, [si+np_upos]
    mov [si+np_uins], cx
    call np_uclose
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
; np_selpace - drop the lock, wait for the tick, take it back
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_selpace:
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
; np_hitpt - the character index under the pointer, scrolling the view when
;            the pointer has left it
; in:  CX = x, DX = y (absolute), SI = window ptr, np_bounds run, lock held
; out: AX = the index; CF = 1 if [np_top] moved as well. Preserves all others.
;
; The scroll is what makes a selection longer than the window possible at all,
; and it is one row a tick because that is the rate the drag loop runs at.
; -----------------------------------------------------------------------------
np_hitpt:
    push bx
    push cx
    push dx
    xor bx, bx                  ; BX = "the view moved"
    mov ax, [np_vrows]
    or ax, ax
    jz .hit
    cmp dx, [np_ty]
    jb .up
    dec ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]             ; AX = the last visible row's top
    cmp dx, ax
    jbe .hit
    mov dx, ax                  ; below the view: clamp, and page down one
    mov ax, [np_top]
    inc ax
    call np_scrollto
    jc .hit                     ; CF = 1 from np_scrollto means it did NOT
    mov bx, 1                   ; move, which is the bottom of the note
    jmp short .hit
.up:
    mov dx, [np_ty]
    mov ax, [np_top]
    dec ax
    call np_scrollto
    jc .hit
    mov bx, 1
.hit:
    mov [np_hitx], cx
    mov [np_hity], dx
    mov word [np_wanty], 0xFFFF
    push ax                     ; the pointer names ONE row, and np_rows knows
    push dx                     ; where it starts (SPEC.md 27.5) - so seed
    mov ax, dx                  ; there and stop after it, which is what
    sub ax, [np_ty]             ; np_onclick has always done for a click. This
    jc .nseed                   ; was walking the WHOLE note per drag pass
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    mov dx, ax
    call np_seedrow
.nseed:
    pop dx
    pop ax
    call np_measure
    mov byte [np_resume], 0
    mov ax, [np_hiti]
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
; np_dragsel - follow the pointer, extending the selection from [np_anchor]
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_dragsel:
    push ax
    push bx
    push cx
    push dx
    mov bx, [np_anchor]         ; what the last pass resolved
    mov word [np_lmx], 0xFFFF   ; ...and where the pointer was when it did
    mov word [np_lmy], 0xFFFF
.pass:
    call np_selpace
    call OSAPI_MOUSE            ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .up
    call np_bounds
    cmp cx, [np_lmx]            ; A POINTER THAT HAS NOT MOVED HAS NOTHING TO
    jne .moved                  ; SAY. The loop runs at a tick whether the
    cmp dx, [np_lmy]            ; mouse reports anything or not, and at 1200
    jne .moved                  ; baud it usually does not
    cmp dx, [np_ty]
    jb .moved                   ; ...unless it is parked outside the view,
    push ax                     ; where every tick owes another row of scroll
    mov ax, [np_vrows]
    or ax, ax
    jz .still
    dec ax
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]             ; the last visible row's top - np_hitpt's own
    cmp dx, ax                  ; threshold for scrolling, so the two cannot
    ja .scrolling               ; disagree about which passes matter
.still:
    pop ax
    jmp short .pass
.scrolling:
    pop ax
.moved:
    mov [np_lmx], cx
    mov [np_lmy], dx
    call np_hitpt
    jc .draw                    ; the view scrolled: owed a redraw either way
    cmp ax, bx
    je .pass
.draw:
    mov bx, ax
    mov [np_cur], ax
    mov dx, [np_anchor]
    call np_selset
    mov byte [np_ckok], 0       ; the checkpoint is the CARET's row start and
    mov byte [np_selonly], 1    ; the caret has just jumped - and a drag moves
                                ; no character, so every dirty row owes an
                                ; inversion and no glyphs (SPEC.md 27.8.2)
    call np_redraw
    jmp short .pass
.up:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_rev - reverse the note's bytes in [AX, BX], inclusive
; in:  AX, BX; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_rev:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [np_dseg]
    mov si, ax
    mov di, bx
.l:
    cmp si, di
    jae .out
    mov al, [es:si]
    mov cl, [es:di]
    mov [es:si], cl
    mov [es:di], al
    inc si
    dec di
    jmp short .l
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_dmark_on / np_dmark_off - the drop point's insertion bar
; in:  [np_dpos] (0xFFFF = none), SI = window ptr, lock held
; out: nothing; preserves all registers
;
; XOR, so the erase is the draw again - and the pixels it was drawn at are
; BANKED rather than recomputed, because a scroll between the two would move
; where the bar belongs and leave the old one behind for good. That is
; SPEC.md 48.11's rule in miniature.
; -----------------------------------------------------------------------------
np_dmark_on:
    push ax
    push bx
    push cx
    push dx
    mov word [np_dmark], 0xFFFF
    mov ax, [np_dpos]
    cmp ax, 0xFFFF
    je .out
    mov bx, [np_cur]
    push bx
    mov [np_cur], ax            ; np_walk answers where the CARET is, so the
    mov word [np_hity], 0xFFFF  ; drop point borrows it for one measure pass
    mov word [np_wanty], 0xFFFF
    call np_measure
    pop bx
    mov [np_cur], bx
    mov byte [np_ckok], 0       ; ...and the checkpoint that pass left behind
                                ; describes the drop point's row, not ours
    cmp byte [np_curseen], 0
    je .out
    mov ax, [np_curx]
    mov bx, [np_cury]
    cmp bx, [np_ty]
    jb .out
    mov dx, bx
    add dx, 7
    cmp dx, [np_bot]
    ja .out
    mov cx, ax
    call OSAPI_GFX_XOR_FILL
    mov [np_dmark], ax
    mov [np_dmy], bx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

np_dmark_off:
    push ax
    push bx
    push cx
    push dx
    mov ax, [np_dmark]
    cmp ax, 0xFFFF
    je .out
    mov bx, [np_dmy]
    mov cx, ax
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_XOR_FILL
    mov word [np_dmark], 0xFFFF
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_movesel - move the selected text to [np_dpos]
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
np_movesel:
    push ax
    push bx
    push cx
    push dx
    call np_selget              ; AX = s, CX = n
    jc .out
    mov [np_mvs], ax
    mov [np_mvn], cx
    add ax, cx
    mov [np_mve], ax
    mov ax, [np_dpos]
    mov [np_mvp], ax
    cmp ax, [np_mvs]
    jb .left
    cmp ax, [np_mve]
    jbe .out                    ; dropped inside itself: nothing to do
    mov ax, [np_mvs]            ; --- rightwards: [s,e)[e,p) -> [e,p)[s,e) ---
    mov cx, [np_mvp]
    sub cx, ax
    call np_urec_bulk           ; the whole span [s, p), as one replacement
    mov byte [np_unolog], 1
    mov ax, [np_mvs]
    mov bx, [np_mve]
    dec bx
    call np_rev
    mov ax, [np_mve]
    mov bx, [np_mvp]
    dec bx
    call np_rev
    mov ax, [np_mvs]
    mov bx, [np_mvp]
    dec bx
    call np_rev
    mov ax, [np_mvp]
    sub ax, [np_mvn]            ; the block ends where it was dropped
    jmp short .fin
.left:                          ; --- leftwards: [p,s)[s,e) -> [s,e)[p,s) ---
    mov ax, [np_mvp]
    mov cx, [np_mve]
    sub cx, ax
    call np_urec_bulk           ; the whole span [p, e)
    mov byte [np_unolog], 1
    mov ax, [np_mvp]
    mov bx, [np_mvs]
    dec bx
    call np_rev
    mov ax, [np_mvs]
    mov bx, [np_mve]
    dec bx
    call np_rev
    mov ax, [np_mvp]
    mov bx, [np_mve]
    dec bx
    call np_rev
    mov ax, [np_mvp]            ; ...and here it begins where it was dropped
.fin:
    mov byte [np_unolog], 0
    mov dx, ax
    add dx, [np_mvn]
    call np_selset              ; it stays selected, which is what the user
    mov [np_cur], dx            ; just spent a drag pointing at
    call np_urec_bulkend_span
    call np_editinv
    mov byte [np_follow], 1
    mov byte [np_ckok], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; np_urec_bulkend_span - close a bulk record whose span did not change length
; (a move rearranges bytes, it does not add or remove any). Preserves all.
np_urec_bulkend_span:
    push bx
    push cx
    push si
    call np_utop_rec
    jc .out
    mov cx, [si+np_udel]
    mov [si+np_uins], cx
    call np_uclose
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_dragmove - a press that landed INSIDE the selection
; in:  AX = the index pressed, SI = window ptr, lock held
; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_dragmove:
    push ax
    push bx
    push cx
    push dx
    mov [np_anchor], ax         ; where the press was, for the click case
    mov word [np_dpos], 0xFFFF
    mov word [np_dmark], 0xFFFF
    call OSAPI_MOUSE
    mov [np_dpx], cx
    mov [np_dpy], dx
    xor bx, bx                  ; BX = the pointer has left the dead zone
.pass:
    call np_selpace
    call OSAPI_MOUSE
    test al, 1
    jz .up
    or bx, bx
    jnz .track
    mov ax, cx
    sub ax, [np_dpx]
    call np_absw
    cmp ax, NP_SELDRAG
    ja .moved
    mov ax, dx
    sub ax, [np_dpy]
    call np_absw
    cmp ax, NP_SELDRAG
    jbe .pass
.moved:
    mov bx, 1
.track:
    call np_bounds
    call np_hitpt
    jnc .same
    mov word [np_dmark], 0xFFFF ; the view scrolled out from under the bar
    mov word [np_dpos], 0xFFFF
    call np_redraw
.same:
    cmp ax, [np_dpos]
    je .pass
    call np_dmark_off
    mov [np_dpos], ax
    call np_dmark_on
    jmp short .pass
.up:
    call np_dmark_off
    or bx, bx
    jz .click
    cmp word [np_dpos], 0xFFFF
    je .click
    call np_movesel
    call np_redraw
    jmp short .out
.click:
    mov ax, [np_anchor]         ; never left the dead zone: it was a click,
    mov [np_cur], ax            ; and a click puts the caret where it landed
    call np_selclr
    mov byte [np_ckok], 0
    call np_redraw
.out:
    mov word [np_dpos], 0xFFFF
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; np_absw - AX = |AX|, treating it as signed. Preserves all other registers.
np_absw:
    or ax, ax
    jns .out
    neg ax
.out:
    ret

; =============================================================================
; The find and replace panel (SPEC.md 27.10)
;
; It is docked at the TOP of the content rather than being a window of its
; own, and that is worth stating because a second window was the obvious
; shape. A window would have needed its own record, its own dispatcher entry
; and its own answer to "what happens when the user clicks the note behind
; it"; a strip of the content needs one number - the height np_bounds adds to
; [np_ty] - and every other thing in this module follows for nothing. The
; signatures notice the geometry changed and repaint once (SPEC.md 27.2), the
; scroll bar re-derives its track, the note re-wraps at the same width.
;
; The count is NOT recomputed on a keystroke. Counting walks the whole note
; with the matcher, so it is owed by [np_fcdirty] and paid by the worker half
; a second later - the same trade np_height makes (SPEC.md 27.7), for the same
; reason, and it is why typing in the box stays as cheap as typing in the note.
; =============================================================================

; -----------------------------------------------------------------------------
; np_fph - the panel's height in pixels, 0 when it is closed
; out: AX; preserves every other register
; -----------------------------------------------------------------------------
np_fph:
    cmp byte [np_fpan], NP_FPAN_NONE
    je .none
    mov ax, NP_FP_H             ; a multiple of 4 - see the constant
    cmp byte [np_fpan], NP_FPAN_REPL
    jne .out
    add ax, NP_FP_ROW
    jmp short .out
.none:
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; np_pbset - place button BX, CX pixels wide, with its right edge at DI
; in:  BX = 0..3, CX = width (0 = not shown), DI = the right-edge cursor
; out: DI moved left past it; preserves all other registers
; -----------------------------------------------------------------------------
np_pbset:
    push ax
    push si
    mov si, bx
    shl si, 1
    mov [si+np_pbw], cx
    jcxz .zero
    mov ax, di
    sub ax, cx
    inc ax
    mov [si+np_pbx], ax
    sub di, cx
    sub di, 3
    jmp short .out
.zero:
    mov word [si+np_pbx], 0
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_fpgeom - the panel's live geometry, in one place
; in:  SI = window ptr; out: the np_p* block; preserves all registers
;
; The fm_hit discipline (SPEC.md 22): the painter and the hit test read the
; same words, so a button cannot be drawn in one place and clicked in another.
; -----------------------------------------------------------------------------
np_fpgeom:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov [np_pl], ax
    mov [np_pt], dx
    push ax
    call OSAPI_WM_GEOM          ; CX = content w, DX = content h
    pop ax
    add ax, cx
    dec ax
    mov [np_pr], ax
    mov ax, [np_pl]
    add ax, 4 + NP_FP_LBL
    mov [np_pfx], ax            ; a text box's interior...
    mov ax, [np_pr]
    sub ax, 4
    mov [np_pfr], ax            ; ...left and right
    call np_fph
    mov bx, ax
    mov ax, [np_pt]
    add ax, bx
    sub ax, NP_FP_PAD + 1 + NP_FP_ROW
    mov [np_pbtny], ax          ; the button row's top
    mov ax, [np_pl]
    add ax, 4
    mov [np_pcbx], ax           ; the Regex tick box...
    add ax, 9 + 4 + 16 + 8
    mov [np_pnx], ax            ; ...then 'Rx', then the count
    mov di, [np_pr]
    sub di, 3
    xor bx, bx                  ; 0 = the close box
    mov cx, 14
    call np_pbset
    cmp byte [np_fpan], NP_FPAN_REPL
    jne .nrepl
    mov bx, 1                   ; 1 = All
    mov cx, 3*8+8
    call np_pbset
    mov bx, 2                   ; 2 = Repl
    mov cx, 4*8+8
    call np_pbset
    jmp short .next
.nrepl:
    mov bx, 1
    xor cx, cx
    call np_pbset
    mov bx, 2
    xor cx, cx
    call np_pbset
.next:
    mov bx, 3                   ; 3 = Next
    mov cx, 4*8+8
    call np_pbset
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_pfcols - AX = whole cells a text box shows; preserves everything else
; -----------------------------------------------------------------------------
np_pfcols:
    push cx
    mov ax, [np_pfr]
    sub ax, [np_pfx]
    inc ax
    jns .ok
    xor ax, ax
.ok:
    mov cl, 3
    shr ax, cl
    cmp ax, NP_PATMAX
    jbe .out
    mov ax, NP_PATMAX
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; np_pfield - draw one text box, interior and all
; in:  AL = 0 the Find box / 1 the Replace box; np_fpgeom run, lock held
; out: nothing; clobbers as a callback
;
; The interior is ONE opaque font_run over a space-padded buffer (SPEC.md 6.1)
; rather than a fill and then glyphs: this is redrawn on every keystroke in
; the box, and the pair leaves it blank in between.
; -----------------------------------------------------------------------------
np_pfield:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    xor ah, ah
    mov bp, ax                  ; BP = which box (a VALUE - SS is not DS here)
    mov cx, NP_FP_ROW
    mul cx
    add ax, [np_pt]
    add ax, NP_FP_PAD
    mov di, ax                  ; DI = this row's top

    mov al, CBLACK              ; the pen is the BOX FRAME's below, not the
    call OSAPI_SET_COLOR        ; label's - the label carries its own pair
    mov si, np_s_find
    or bp, bp
    jz .lbl
    mov si, np_s_repl
.lbl:
    mov cx, [np_pl]
    add cx, 4
    mov dx, di
    add dx, 1
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the panel's own ground
    call OSAPI_FONT_RUN             ; (SPEC.md 6.6.5)

    mov ax, [np_pfx]            ; the box
    sub ax, 2
    mov bx, di
    mov cx, [np_pfr]
    add cx, 2
    mov dx, di
    add dx, NP_FP_ROW - 3
    call OSAPI_GFX_FRAME

    push di
    call np_pfbuf               ; np_fbuf = what the box shows, padded
    pop di                      ; ...which walks DI over it
    mov si, np_fbuf
    mov cx, [np_pfx]
    mov dx, di
    inc dx                      ; di+1..di+8, INSIDE the frame at di..di+9:
                                ; the run is opaque, so a cell that reached
                                ; the frame's own row would erase it
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov al, [np_ffield]         ; the caret, when this box has the focus
    cmp al, bl                  ; BL = the box index, set by np_pfbuf
    jne .out
    mov ax, [np_fpcur]
    sub ax, [np_fview]
    js .out
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_pfx]
    cmp ax, [np_pfr]
    ja .out
    mov bx, di
    inc bx
    mov dx, bx
    add dx, 7
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
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
; np_pfbuf - fill np_fbuf with the visible part of box BP, space-padded
; in:  BP = 0 find / 1 replace
; out: BL = BP (the box index, for the caret test), [np_fview] = the first
;      character shown; clobbers AX/CX/SI/DI
;
; The window slides so the caret is always inside it, which is the whole of
; what a one-line editor owes: NP_PATMAX is 47 characters and no window this
; runs on is that wide.
; -----------------------------------------------------------------------------
np_pfbuf:
    push dx
    push es
    push ds
    pop es
    mov si, np_fpat
    mov cx, [np_fpatn]
    or bp, bp
    jz .have
    mov si, np_frep
    mov cx, [np_frepn]
.have:
    mov al, [np_ffield]         ; only the focused box scrolls to its caret
    xor ah, ah
    cmp ax, bp
    jne .top
    push cx
    call np_pfcols              ; AX = cells
    mov cx, [np_fpcur]
    sub cx, ax
    inc cx
    jns .set
    xor cx, cx
.set:
    mov [np_fview], cx
    pop cx
    jmp short .win
.top:
    mov word [np_fview], 0
.win:
    mov di, [np_fview]
    cmp di, cx
    jbe .from
    mov di, cx                  ; past the end: nothing to show
.from:
    add si, di
    sub cx, di
    push cx
    call np_pfcols
    mov di, ax                  ; DI = cells the box holds
    pop cx
    cmp cx, di
    jbe .cp
    mov cx, di
.cp:
    sub di, cx                  ; DI = the padding after it
    push di
    mov di, np_fbuf
    cld
    rep movsb
    pop cx
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov bx, bp                  ; the caret test wants the index back
    pop es
    pop dx
    ret

; -----------------------------------------------------------------------------
; np_pdrawf - redraw just the focused box (a keystroke in it changed nothing
;             else on the panel)
; in:  SI = window ptr, lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_pdrawf:
    push ax
    cmp byte [np_fpan], NP_FPAN_NONE
    je .out
    mov al, [np_ffield]
    cmp al, NP_FF_DOC
    je .out
    call np_fpgeom
    call np_pfield
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_pcount - compose what the count line says into np_fnum
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
; np_fatmatch - is the selection on screen the match [np_fmno] names?
; out: CF = 1 = yes; preserves all registers.
;
; ONE predicate with two readers - the counter, which must not name a match
; nobody can see, and Replace, which must not overwrite a selection the user
; made by hand. Keeping them the same test is what stops the panel saying
; "3/4" while Replace declines to act on it.
np_fatmatch:
    push ax
    cmp byte [np_selon], 0
    je .no
    mov ax, [np_sel0]
    cmp ax, [np_fmst]
    jne .no
    mov ax, [np_sel1]
    cmp ax, [np_fmen]
    jne .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

np_pcount:
    push ax
    push di
    push si
    mov di, np_fnum
    cmp byte [np_fbad], 0
    jne .bad
    cmp word [np_fpatn], 0
    je .blank
    cmp byte [np_fcok], 0
    je .wait
    ; --- n/total, which is both more use and SHORTER than 'n found' -------
    ; The room on this row is the tightest thing in the panel: the tick box
    ; and its label are on the left of it and four buttons on the right, and
    ; on a 260px window there are nine cells between them. '12 found' is
    ; eight of the nine; '3/12' is four, and it answers the question the
    ; count was really being asked - where am I, of how many.
    cmp word [np_fmno], 0
    je .dash
    call np_fatmatch            ; ...and the selection has to still BE it: a
    jc .num                     ; click clears the selection without touching
.dash:                          ; the ordinal, and 3/4 with nothing highlighted
    mov byte [di], '-'          ; names a match nobody can see. Honest about
    inc di                      ; which half is unknown, rather than showing a
    jmp short .slash            ; 0 that looks like an answer
.num:
    mov ax, [np_fmno]
    call np_utoa
.slash:
    mov byte [di], '/'
    inc di
    mov ax, [np_fcount]
    call np_utoa
    jmp short .out
.wait:
    mov si, np_m_wait
    jmp short .tail
.bad:
    mov si, np_m_badpat
    jmp short .tail
.blank:
    mov si, np_m_blank
.tail:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .tail
.out:
    pop si
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_pdrawn - redraw just the count, which is all a recount changes
; in:  SI = window ptr, lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_pdrawn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [np_fpan], NP_FPAN_NONE
    je .out
    call np_fpgeom
    call np_pcount
    mov bx, [np_pbx+6]          ; button 3 (Next) is the leftmost one shown,
    or bx, bx                   ; and the count may not reach it
    jnz .room
    mov bx, [np_pr]
.room:
    sub bx, 4
    sub bx, [np_pnx]
    js .out
    mov cl, 3
    shr bx, cl                  ; BX = the cells there is room for
    jz .out
    cmp bx, 13
    jbe .cap
    mov bx, 13
.cap:
    mov si, np_fnum             ; ...and the string is padded to exactly that,
    mov di, np_fnum             ; so a shorter answer erases the longer one it
.len:                           ; replaces and nothing runs into a button
    cmp byte [di], 0
    je .pad
    inc di
    dec bx
    jnz .len
    mov byte [di], 0            ; longer than the room: it is cut, not spilled
    jmp short .draw
.pad:
    or bx, bx
    jz .draw
    mov byte [di], ' '
    inc di
    dec bx
    jmp short .pad
.draw:
    mov byte [di], 0
    mov cx, [np_pnx]
    mov dx, [np_pbtny]
    add dx, 2
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_pbutton - draw button BX with label SI
; in:  BX = 0..3, SI = its NUL label; np_fpgeom run, lock held
; out: nothing; preserves all registers
;
; The drawing is os88ui_btn's (apps/os88ui.inc); this turns np_fpgeom's
; parallel x/width arrays into the 4-word rect the shared control takes. A
; zero width or a zero x still means "this button is not shown" and returns
; before anything is drawn.
;
; ONE PIXEL MOVED, and deliberately: the label's y was the literal +2 in an
; NP_FP_BTNH = 11 box, and the shared control centres at (11-8)/2 = 1, so
; every caption in the panel sits one row higher. That is the arithmetic the
; literal was standing in for.
; -----------------------------------------------------------------------------
np_pbutton:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    shl bx, 1
    mov cx, [bx+np_pbw]
    jcxz .out
    mov di, [bx+np_pbx]
    or di, di
    jz .out
    mov [np_brect+0], di
    add di, cx
    dec di                      ; ...x2 inclusive
    mov [np_brect+4], di
    mov ax, [np_pbtny]
    mov [np_brect+2], ax
    add ax, NP_FP_BTNH - 1
    mov [np_brect+6], ax
    mov bx, np_brect
    mov di, OS88UI_FILL         ; np_fpaint fills the panel band before the
                                ; first button, but a button REDRAWN in place
                                ; would or its caption onto the old one
                                ; (os88ui.inc's own note), and this is the
                                ; cheapest way for that never to become true
    call os88ui_btn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

np_brect:   dw 0, 0, 0, 0       ; the button being drawn, screen coordinates

; -----------------------------------------------------------------------------
; np_fpaint - draw the whole panel
; in:  SI = window ptr, lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_fpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [np_fpan], NP_FPAN_NONE
    je .out
    call np_fpgeom
    call np_fph
    mov di, ax                  ; DI = the panel's height
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [np_pl]
    mov bx, [np_pt]
    mov cx, [np_pr]
    mov dx, bx
    add dx, di
    sub dx, 2
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [np_pl]
    mov bx, [np_pr]
    mov dx, [np_pt]
    add dx, di
    dec dx
    call OSAPI_GFX_HLINE        ; the rule that separates it from the note

    xor ax, ax
    call np_pfield              ; Find:
    cmp byte [np_fpan], NP_FPAN_REPL
    jne .btn
    mov ax, 1
    call np_pfield              ; Repl:

.btn:
    mov ax, [np_pcbx]           ; the Regex tick box
    mov bx, [np_pbtny]
    inc bx
    mov cx, ax
    add cx, 8
    mov dx, bx
    add dx, 8
    call OSAPI_GFX_FRAME
    cmp byte [np_frx], 0
    je .norx
    mov ax, [np_pcbx]
    add ax, 2
    mov bx, [np_pbtny]
    add bx, 3
    mov cx, ax
    add cx, 4
    mov dx, bx
    add dx, 4
    call OSAPI_GFX_FILL
.norx:
    mov cx, [np_pcbx]
    add cx, 13
    mov dx, [np_pbtny]
    add dx, 2
    mov si, np_s_rx
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the panel's own ground.
    call OSAPI_FONT_RUN             ; The tick box to its left is a fill of
                                    ; its own and ends 5px short of this pen

    mov bx, 3
    mov si, np_b_next
    call np_pbutton
    mov bx, 2
    mov si, np_b_repl
    call np_pbutton
    mov bx, 1
    mov si, np_b_all
    call np_pbutton
    xor bx, bx
    mov si, np_b_x
    call np_pbutton
    mov si, [np_win]            ; np_pdrawn re-derives the geometry from the
    call np_pdrawn              ; window, and SI has been label strings since
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_fopen - put the panel up (AL = NP_FPAN_FIND or NP_FPAN_REPL)
; in:  AL, SI = window ptr; out: nothing; clobbers as a callback
;
; A selection that fits on one line SEEDS the pattern, which is the one thing
; every editor does that nobody notices until it is missing.
; -----------------------------------------------------------------------------
np_fopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [np_fpan], al
    mov byte [np_ffield], NP_FF_FIND    ; .curok below is what puts the caret
                                        ; inside this box; np_ffocus is the
                                        ; general form and cannot be used here
                                        ; because the seed sets it explicitly
    call np_selget              ; AX = start, CX = length
    jc .nosel
    cmp cx, NP_PATMAX
    ja .nosel
    jcxz .nosel
    mov es, [np_dseg]
    mov si, ax
    mov di, np_fpat
    mov dx, cx
.cp:
    mov al, [es:si]
    cmp al, 13
    je .nosel                   ; more than one line: not a pattern
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .cp
    mov byte [di], 0
    mov [np_fpatn], dx
    mov [np_fpcur], dx
.nosel:
    mov ax, [np_fpatn]
    cmp [np_fpcur], ax
    jbe .curok
    mov [np_fpcur], ax
.curok:
    call np_fchk
    mov byte [np_fcok], 0
    mov byte [np_fcdirty], 1
    call np_hire                ; the count is the worker's to pay
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; np_fclose - take the panel down. Preserves all registers.
np_fclose:
    mov byte [np_fpan], NP_FPAN_NONE
    mov byte [np_ffield], NP_FF_DOC
    ret

; -----------------------------------------------------------------------------
; np_ffocus - hand the keys to field AL, and put its caret at the end
; in:  AL = NP_FF_*; out: nothing; preserves all registers
;
; The caret is one word for both boxes, so moving the focus has to move it
; too - and the end of the text is where every dialog on every machine puts
; the caret when a box is tabbed into.
; -----------------------------------------------------------------------------
np_ffocus:
    push ax
    push bx
    mov [np_ffield], al
    cmp al, NP_FF_DOC
    je .out
    mov bx, [np_fpatn]
    cmp al, NP_FF_FIND
    je .set
    mov bx, [np_frepn]
.set:
    mov [np_fpcur], bx
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_fpclick - a press inside the panel
; in:  CX = x, DX = y (absolute), SI = window ptr, np_bounds run, lock held
; out: CF = 0 = it was ours and has been dealt with; CF = 1 = it was not.
;      Clobbers as a callback.
; -----------------------------------------------------------------------------
np_fpclick:
    push ax
    push bx
    push di
    cmp byte [np_fpan], NP_FPAN_NONE
    je .no
    call np_fpgeom
    call np_fph
    mov bx, [np_pt]
    add bx, ax
    cmp dx, bx
    jae .no                     ; below the panel: the note's press, not ours
    cmp dx, [np_pt]
    jb .no

    mov bx, [np_pbtny]          ; --- the button row ---
    cmp dx, bx
    jb .fields
    xor bx, bx
.b:
    mov di, bx
    shl di, 1
    mov ax, [di+np_pbw]
    or ax, ax
    jz .bnext
    mov ax, [di+np_pbx]
    cmp cx, ax
    jb .bnext
    add ax, [di+np_pbw]
    cmp cx, ax
    jae .bnext
    jmp short .hit
.bnext:
    inc bx
    cmp bx, 4
    jb .b
    mov ax, [np_pcbx]           ; the Regex tick box, and its label with it
    cmp cx, ax
    jb .yes
    add ax, 9 + 4 + 16
    cmp cx, ax
    jae .yes
    xor byte [np_frx], 1
    call np_fchk
    mov byte [np_fcok], 0
    mov byte [np_fcdirty], 1
    call np_fpaint
    jmp short .yes
.hit:
    mov si, [np_win]            ; every handler below draws, and SI has to be
    or bx, bx                   ; the window for all of them
    jnz .h1
    call np_fclose              ; 0 = close
    call np_redrawall
    jmp short .yes
.h1:
    cmp bx, 1
    jne .h2
    call np_dorepall            ; 1 = All
    mov si, [np_win]
    call np_redraw
    call np_pdrawn
    jmp short .yes
.h2:
    cmp bx, 2
    jne .h3
    call np_dorepl              ; 2 = Repl
    mov si, [np_win]
    call np_redraw
    call np_pdrawn
    jmp short .yes
.h3:
    call np_donext              ; 3 = Next
    mov si, [np_win]
    call np_redraw
    jmp short .yes

.fields:
    mov ax, dx                  ; --- a text box ---
    sub ax, [np_pt]
    sub ax, NP_FP_PAD
    js .yes
    mov bx, NP_FP_ROW
    xor dx, dx
    div bx                      ; AX = the row index
    cmp ax, 1
    ja .yes
    cmp al, 1
    jne .f0
    cmp byte [np_fpan], NP_FPAN_REPL
    jne .yes
.f0:
    call np_ffocus
    mov bx, ax
    call np_fpcaret             ; CX = x -> [np_fpcur], which np_ffocus has
                                ; just put at the end of THIS box
    call np_fpaint
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_fpcaret - put the box's caret at pixel CX
; in:  CX = an absolute x, BX = the box index; out: [np_fpcur]; preserves all
; -----------------------------------------------------------------------------
np_fpcaret:
    push ax
    push cx
    push dx
    mov ax, cx
    sub ax, [np_pfx]
    jns .ok
    xor ax, ax
.ok:
    add ax, 4
    mov cl, 3
    shr ax, cl
    add ax, [np_fview]
    mov dx, [np_fpatn]
    or bx, bx
    jz .lim
    mov dx, [np_frepn]
.lim:
    cmp ax, dx
    jbe .set
    mov ax, dx
.set:
    mov [np_fpcur], ax
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_fpkey - a key while a text box has the focus
; in:  AL = ascii, AH = scan
; out: CF = 0 = consumed (the box changed and wants redrawing); CF = 1 = not
;      ours. Clobbers AX/BX/CX/DX/SI/DI.
; -----------------------------------------------------------------------------
np_fpkey:
    push si
    push di
    mov si, np_fpat             ; SI = the buffer, DI = its length's address
    mov di, np_fpatn
    cmp byte [np_ffield], NP_FF_FIND
    je .have
    mov si, np_frep
    mov di, np_frepn
.have:
    mov bx, [di]                ; ONE caret word serves both boxes, so the
    cmp [np_fpcur], bx          ; first thing this must do is make sure it is
    jbe .cur                    ; inside THIS one. Without it, tabbing from a
    mov [np_fpcur], bx          ; long pattern into a short replacement gave
.cur:                           ; the insert loop a NEGATIVE count, and it
                                ; wrote 64KB across this package's own bss
    or al, al
    jnz .ascii
    cmp ah, NP_K_LEFT
    je .left
    cmp ah, NP_K_RIGHT
    je .right
    cmp ah, NP_K_HOME
    je .home
    cmp ah, NP_K_END
    je .end
    cmp ah, NP_K_DEL
    je .del
    jmp .no
.ascii:
    cmp al, 8
    je .bksp
    cmp al, 32
    jb .no
    cmp al, 126
    ja .no
    mov bx, [di]
    cmp bx, NP_PATMAX
    jae .yes                    ; full: the keystroke is dropped, as a full
                                ; note's is
    push ax                     ; open a gap at the caret
    mov cx, bx
    sub cx, [np_fpcur]
    mov bx, [di]
.ig:
    jcxz .iplace
    mov al, [si+bx-1]
    mov [si+bx], al
    dec bx
    dec cx
    jmp short .ig
.iplace:
    pop ax
    mov bx, [np_fpcur]
    mov [si+bx], al
    inc word [di]
    inc word [np_fpcur]
    mov bx, [di]
    mov byte [si+bx], 0
    jmp short .chg
.bksp:
    cmp word [np_fpcur], 0
    je .yes
    dec word [np_fpcur]
    jmp short .cut1
.del:
    mov bx, [np_fpcur]
    cmp bx, [di]
    jae .yes
.cut1:
    mov bx, [np_fpcur]
.dg:
    mov cx, [di]
    cmp bx, cx
    jae .dend
    mov al, [si+bx+1]
    mov [si+bx], al
    inc bx
    jmp short .dg
.dend:
    dec word [di]
    mov bx, [di]
    mov byte [si+bx], 0
    jmp short .chg
.left:
    cmp word [np_fpcur], 0
    je .yes
    dec word [np_fpcur]
    jmp short .yes
.right:
    mov bx, [np_fpcur]
    cmp bx, [di]
    jae .yes
    inc word [np_fpcur]
    jmp short .yes
.home:
    mov word [np_fpcur], 0
    jmp short .yes
.end:
    mov bx, [di]
    mov [np_fpcur], bx
    jmp short .yes
.chg:
    call np_fchk                ; the pattern changed: it may not compile, and
    mov byte [np_fcok], 0       ; the count it had is about somebody else
    mov byte [np_fcdirty], 1
    mov word [np_fmno], 0
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; np_redrawall - a full repaint of the content, panel included
; in:  SI = window ptr, lock held; out: nothing; clobbers as a callback
;
; What opening or closing the panel needs: [np_ty] moved, so every row
; signature describes a layout that no longer exists - which np_sigsame would
; have worked out for itself, but saying so costs nothing and cannot be got
; wrong by a later edit.
; -----------------------------------------------------------------------------
; =============================================================================
; Opening and closing the panel MOVES the text (SPEC.md 27.10.2)
;
; The panel changes exactly one number - the height np_bounds adds to
; [np_ty] - and changes NOTHING about the wrap: [np_tx] and [np_rgt] are the
; content's own edges and the panel is docked above the text, not beside it.
; So every row holds exactly the same characters before and after; they are
; simply H pixels lower or higher, and the view keeps the same [np_top].
;
; That makes the whole repaint a BLIT. Opening was ~19 rows of ~30 cells
; through np_redraw's full path - about 570 glyph cells, over half a second on
; a 4.77MHz machine (PERFORMANCE.md Part 2's ~1ms a cell) - and is now one
; OSAPI_GFX_SCROLL plus the panel. Closing is the blit plus the four rows the
; text moving up EXPOSES at the bottom, which is the only part of it that was
; never on screen.
;
; Three things make it safe, and all three are refusals rather than
; corrections, because a wrong blit shows text that was never in the note:
;
;  - **Only [np_ty] may have moved.** [np_tx], [np_rgt] and [np_bot] are
;    compared against what np_sigmark recorded; a resize that happens to
;    coincide falls back.
;  - **[np_top] must not be clamped.** Closing GROWS [np_vrows], which shrinks
;    np_scrollmax, and a view that has to move renames every row. Opening
;    shrinks vrows and so can never need it.
;  - **A toast, the visual break and stale signatures all refuse.** The toast
;    is drawn over the text at a y the panel moves, so the blit would carry it
;    to the wrong place and nothing would put it back - np_scrollpaint refuses
;    for the same reason (SPEC.md 27.7.2).
;
; The panel's OWN pixels are inside the band that moves, which is what makes
; closing leave nothing behind: they blit off the top of the content and are
; clipped. What the band cannot reach is the <8px left margin the x-rounding
; gives up and the scroll bar's columns; both are repainted afterwards, which
; np_sbar was going to do anyway because the thumb has moved.
; =============================================================================

; -----------------------------------------------------------------------------
; np_panmove - move the text to where the panel now leaves room for it
; in:  SI = window ptr, np_bounds ALREADY run for the NEW geometry, gfx lock
;      held
; out: CF = 0 the screen is correct and the signatures describe it; CF = 1
;      nothing was drawn and the caller must repaint in full.
;      Clobbers what a window callback may.
; -----------------------------------------------------------------------------
np_panmove:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [np_sigok], 0
    je .no                      ; the arrays do not describe the screen
    cmp byte [np_bmode], 0
    jne .no
    mov ax, [np_tx]             ; only [np_ty] may have moved
    cmp ax, [np_stx]
    jne .no
    mov ax, [np_rgt]
    cmp ax, [np_srgt]
    jne .no
    mov ax, [np_bot]
    cmp ax, [np_sbot]
    jne .no
    cmp word [np_vrows], 0
    je .no
    mov di, [np_ty]
    sub di, [np_sty]            ; DI = how far the text is moving, + = down
    jz .no
    jns .open
    call np_scrollmax           ; closing: the view has more rows to show, so
    cmp [np_top], ax            ; [np_top] may be past the new maximum - and a
    ja .no                      ; view that moves renames every row
.open:

    mov bx, si                  ; the band is the whole content height, so the
    call OSAPI_WM_CONTENT       ; panel's own pixels move with the text
    push ax                     ; AX = content left, DX = content top
    push dx
    mov ax, [np_tx]
    and ax, 0xFFF8              ; x1 down to a byte column - [np_tx] and not
                                ; the content's own left edge, because
                                ; rounding THAT down would leave the content
    mov cx, [np_rgt]
    add cx, 8
    and cx, 0xFFF8
    dec cx                      ; x2, with x2+1 up to a byte column
    pop bx                      ; y1 = content top
    push bx
    mov dx, [np_bot]            ; y2 = content bottom
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
    mov bx, dx                  ; y1 = content top
    mov cx, [np_tx]
    and cx, 0xFFF8
    dec cx                      ; x2 = just left of the band
    mov dx, [np_bot]
    cmp ax, cx
    jg .noleft
    call OSAPI_GFX_FILL
.noleft:

    or di, di
    js .close

    ; --- OPENING: the panel goes in the strip the text left behind --------
    ; and the rows pushed past the bottom are simply gone, which is what a
    ; smaller view means. All that is owed below is the sliver under the last
    ; whole row, where the blit left a slice of the row that used to be there.
    call np_fpaint
    jmp .tail

.close:
    ; --- CLOSING: the text moved UP, so the bottom of the view is stale ----
    ; The first row that needs lettering is the one whose pixels reach the
    ; band the blit could not fill: (bot - |d| + 1 - ty) >> 3.
    mov ax, di
    neg ax
    mov bx, [np_bot]
    sub bx, ax
    inc bx
    sub bx, [np_ty]
    jns .r0
    xor bx, bx
.r0:
    mov cl, 3
    shr bx, cl                  ; BX = the first exposed row
    cmp bx, [np_vrows]
    jae .tail                   ; nothing was exposed
    mov [np_dr0], bx
    mov ax, [np_vrows]
    dec ax
    mov [np_dr1], ax

    push cx                     ; erase the band: OSAPI_GFX_SCROLL leaves what
    mov ax, bx                  ; it vacates holding a copy of its neighbour
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]
    mov bx, ax                  ; y1
    mov dx, [np_bot]            ; y2 - to the very bottom, so the sliver under
                                ; the last whole row goes too
    mov ax, [np_tx]
    sub ax, NP_MARGIN           ; x1 = the content's own left edge
    mov cx, [np_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov word [np_prowi], 0xFFFF ; the fill erased what the delta cache knew

    mov word [np_hity], 0xFFFF  ; one pass, drawing AND re-signing: the band
    mov word [np_wanty], 0xFFFF ; was just filled, so np_clean
    mov byte [np_draw], 1
    mov byte [np_sigup], 1
    mov byte [np_clip], 1
    mov byte [np_clean], 1
    mov byte [np_resume], 0
    mov ax, [np_vrows]
    mov [np_lastrow], ax
    call np_walk
    mov byte [np_clip], 0
    mov byte [np_clean], 0
    jmp short .done

.tail:
    ; the sliver under the last whole row, which after a move DOWN holds a
    ; slice of the row that used to be there
    push cx
    mov ax, [np_vrows]
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, [np_ty]
    cmp ax, [np_bot]
    ja .done
    mov bx, ax                  ; y1
    mov dx, [np_bot]            ; y2
    mov ax, [np_tx]
    sub ax, NP_MARGIN
    mov cx, [np_rgt]
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL

.done:
    call np_sbar                ; unconditional: the track changed height, and
                                ; the blit reached into the bar's columns
    mov bx, si
    call OSAPI_WM_GROW          ; ...and the corner it sits in
    call np_sigmark             ; the signatures describe THIS geometry now,
    mov ax, [np_top]            ; and [np_gchg] is spent with them
    mov [np_ptop], ax
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

np_redrawall:
    call np_bounds              ; the NEW geometry, so np_panmove can compare
    call np_panmove             ; it against what the signatures were taken at
    jnc .out                    ; (SPEC.md 27.10.2): the panel only moves the
                                ; text, so MOVE it rather than draw it again
    mov byte [np_sigok], 0
    mov word [np_prowi], 0xFFFF
    mov byte [np_ckok], 0
    mov byte [np_rowsok], 0
    call np_redraw
.out:
    ret

; =============================================================================
; Small change (SPEC.md 27.8/27.10)
; =============================================================================

; np_saymsg - AX = a NUL string -> the system toast (SPEC.md 59).
; Preserves all registers AND the flags: two callers are error paths that
; carry their answer in CF.
;
; This was six words of state and a drawing routine of its own - [np_msg],
; [np_msgn]'s generation counter, four box coordinates, np_toast, the
; np_smsg/np_smsgn shadow and np_sigsame's two tests of it. The toast is in
; the MENU BAR now, so it is in none of this app's pixels: it survives a
; repaint without help, it cannot be carried off its frame by a scroll blit,
; and it cannot leave the incremental path disagreeing with W_PAINT - which
; is what forced a FULL content repaint on the first keystroke after every
; save and every load (SPEC.md 59.1).
np_saymsg:
    push ax
    push cx
    push si
    push es
    pushf
    mov si, ax
    push ds
    pop es                      ; the kernel COPIES the string, so np_tbuf may
    xor cx, cx                  ; be reused freely and this app may close
    call OSAPI_TOAST            ; while the message is still up. CX = 0: the
    popf                        ; default lifetime, about three seconds
    pop es
    pop si
    pop cx
    pop ax
    ret

; np_utoa - AX as decimal at DI, no leading zeros; DI advances past it.
; Preserves every other register.
np_utoa:
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

; np_saycnt - "<AX> replaced" as the toast. Preserves all registers.
np_saycnt:
    push ax
    push si
    push di
    mov di, np_tbuf
    call np_utoa
    mov si, np_m_repld
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .cp
    mov ax, np_tbuf
    call np_saymsg
    pop di
    pop si
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; Same geometry the built-in used: 260x180 outer -> 258x160 content.
np_tpl:
    dw 60, 60, 260, 180
    dw np_ttl, np_paint, np_onkey, np_onclick

np_ttl: db 'Note Pad', 0

; --- the app menu set (SPEC.md 12.2) -------------------------------------------
; One menu, three items, all of them existing behaviour: New empties the
; buffer, Open is Ctrl-O, Save is Ctrl-S. AM_NAME reuses np_ttl so the bar
; label and
; the window title are the same eight characters by construction. The bar
; runs 38 + 64 ('Note Pad') + 16 = 118 to the File cell's left edge, and
; 32 + 12 more to its right edge at 162 - nowhere near the clock at 434.
    OS88_MENUSET np_menus, np_ttl, np_oncmd
        OS88_MENU np_m_file, np_items_file, 4
        OS88_MENU np_m_edit, np_items_edit, 5
        OS88_MENU np_m_find, np_items_find, 3
    OS88_MENUSET_END np_menus

np_m_file:     db 'File', 0
np_items_file: dw np_i_new, np_i_open, np_i_save, np_i_saveas  ; = NP_MI_*
np_i_new:      db 'New', 0
np_i_open:     db 'Open...  ^O', 0  ; the ellipsis is the convention and it
np_i_save:     db 'Save  ^S', 0     ; is honest: these two ask a question
np_i_saveas:   db 'Save As...', 0   ; first (SPEC.md 38), Save never does.
                                    ; The keys are on the items because they
                                    ; are the fast path and a menu nobody can
                                    ; learn from is one that has to be opened
                                    ; forever - the Edit menu's own rule

; The Edit menu (SPEC.md 27.8), and the Find menu beside it. Every item
; carries its key, because the keys are the fast path and a menu nobody can
; learn from is a menu that has to be opened forever.
;
; There is no Clear, and searching is not in Edit. Clear was a third door onto
; np_selkill, which Backspace and Delete already open - a menu item whose only
; distinction is that it is slower than the key everybody presses anyway. And
; finding is not editing: it is the one thing here that reads the note without
; changing it, it owns a panel and three keys of its own (SPEC.md 27.10), and
; three items is a menu rather than a tail.
;
; The bar is 38 + 64 ('Note Pad') + 16 = 118 to File's left edge, and each
; cell is its name plus 12: File 118..162, Edit 162..206, Find 206..250 -
; still nowhere near the clock at 434.
np_m_edit:     db 'Edit', 0
np_items_edit: dw np_i_undo, np_i_cut, np_i_copy, np_i_paste, np_i_all ; NP_MI_*
np_i_undo:     db 'Undo  ^Z', 0
np_i_cut:      db 'Cut  ^X', 0
np_i_copy:     db 'Copy  ^C', 0
np_i_paste:    db 'Paste  ^V', 0
np_i_all:      db 'Select All  ^A', 0

np_m_find:     db 'Find', 0
np_items_find: dw np_i_find, np_i_next, np_i_rep               ; = NP_FI_*
np_i_find:     db 'Find...  ^F', 0
np_i_next:     db 'Find Next  F3', 0
np_i_rep:      db 'Replace...  ^R', 0

%ifdef NPBENCH
; The walk bench (`make npbench`), and the ONLY thing that reaches it in this
; file is the Ctrl-B in np_onkey and the four NPVAR words at the foot - both
; inside %ifdef NPBENCH, so NOTEPAD.O88 carries not a byte of it.
%include "npbench.inc"
%endif

; --- the file and what the toast can say (SPEC.md 27.1) ------------------------
; The name is per-instance state now (np_name in bss), seeded from this at
; launch and replaced by whatever the file dialog returns. The two verbs are
; PREFIXES: np_setmsg composes them with the live name into np_tbuf, because
; a toast that still said NOTES.TXT after a Save As would be worse than no
; toast at all.
np_s_default: db 'NOTES.TXT', 0
np_q_pre:     db 'Save changes to ', 0   ; SPEC.md 27.15's alert, composed
                                            ; around the document's name
np_m_saved:   db 'Saved ', 0
np_m_loaded:  db 'Loaded ', 0
np_m_trunc:   db 'Truncated', 0
np_s_nul:     db 0              ; an EMPTY string retires whatever is up
                                ; (SPEC.md 59.3) - one call, and this app
                                ; never has to know whether one was

; FERR_* (SPEC.md 18.4) -> string, indexed by the code itself
np_errtab:
    dw np_e_ok, np_e_nodisk, np_e_io, np_e_name, np_e_noent, np_e_exist
    dw np_e_full, np_e_dirfull, np_e_prot, np_e_wprot, np_e_big
np_e_ok:      db 'Done', 0
np_e_nodisk:  db 'No disk', 0
np_e_io:      db 'Disk error', 0
np_e_name:    db 'Bad name', 0
np_e_noent:   db 'Not found', 0
np_e_exist:   db 'Name exists', 0
np_e_full:    db 'Disk full', 0
np_e_dirfull: db 'Dir full', 0
np_e_prot:    db 'Protected', 0
np_e_wprot:   db 'Write protected', 0
np_e_big:     db 'Too big', 0
np_e_nomem:   db 'No memory', 0      ; the staging claim was refused (50.3)

; --- the find panel and the clipboard (SPEC.md 27.8/27.10/55) ----------------
np_s_find:    db 'Find:', 0
np_s_repl:    db 'Repl:', 0
np_s_rx:      db 'Rx', 0             ; short on purpose: the button row has to
                                     ; fit a 260px window, and 'Regex' is five
                                     ; cells that would push the count off it
np_b_next:    db 'Next', 0
np_b_repl:    db 'Repl', 0
np_b_all:     db 'All', 0
np_b_x:       db 'X', 0
np_m_wait:    db '...', 0            ; the worker owes a count (SPEC.md 27.10)
np_m_blank:   db ' ', 0
np_m_badpat:  db 'Bad pattern', 0
np_m_nopat:   db 'No pattern', 0
np_m_repld:   db ' replaced', 0
np_m_noundo:  db 'Nothing to undo', 0
np_e_cbig:    db 'Too big to copy', 0   ; over CLIP_MAXKB, or the heap could
                                        ; not fund the clipboard (SPEC.md 55)

; --- the state SPEC.md 27.8/27.9/27.10 added ---------------------------------
; The block below the image (further down this file) is ~120 hand-computed
; `equ os88_image_end + N` lines, and that was survivable while it grew a word
; at a time. Selection, undo, the clipboard glue and the find panel add forty
; fields at once, so these are laid out by a PREPROCESSOR COUNTER instead:
; NPVAR emits the same equ and moves the counter on, and NP_BSS_TOTAL falls
; out of where it stops. Nothing about the older fields changed - they are
; still where they were - and the two blocks cannot collide, because this one
; starts at the byte the other one ends on.
;
; It is here, ABOVE OS88_BSS, because OS88_BSS needs the total and the counter
; is the total. os88_image_end is still a forward reference at this point,
; which is exactly what every line of code referencing these fields already
; relies on.
; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_SCROLL           ; SPEC.md 13.10: the shared scroll bar
                                ; OS88UI_SBDRAG is NOT defined here - it is at
                                ; the TOP of this file, and SPEC.md 13.10.7.4
                                ; says why in one sentence: this line is 10,000
                                ; lines below the %ifdefs that read it
%define OS88UI_ALERT            ; ...and SPEC.md 75.3's alert, which is a
                                ; PACKAGE's and not the kernel's - a windowed
                                ; dialog has a floor of ~800 bytes wherever it
                                ; lives, and this is where that is affordable
%include "os88ui.inc"

; ...and it is ABOVE the bss counter below because that block sizes a field
; from OS88UI_AMAX: an %assign is evaluated where it stands, so a constant it
; names has to exist by then.
%assign NPB 508 + NP_MAXROWS*2      ; where the original block ends
%macro NPVAR 2                      ; name, size in bytes
    %1 equ os88_image_end + NPB
    %assign NPB NPB + %2
%endmacro

; --- the selection (SPEC.md 27.8) --------------------------------------------
    NPVAR np_sel0,  2       ; word: the first selected character index...
    NPVAR np_sel1,  2       ; word: ...and one past the last. sel1 > sel0
                            ; whenever [np_selon] is set, and the pair is
                            ; ORDERED here so nothing downstream has to ask
    NPVAR np_anchor, 2      ; word: where a drag started, which is the end
                            ; that does NOT move while the pointer does
    NPVAR np_selon, 1       ; byte: a selection exists
    NPVAR np_rs0,   2       ; word } the CELLS of the row being accumulated
    NPVAR np_rs1,   2       ; word } that fall inside it, 0xFFFF = none. The
                            ; inversion is per row, like the caret's column,
                            ; because that is the unit np_rflush draws in
    NPVAR np_osel0, 2       ; word } the selection the SCREEN is showing, as
    NPVAR np_osel1, 2       ; word } character indices, and whether it has one
    NPVAR np_oselon, 1      ; byte }
    NPVAR np_selonly, 1     ; byte: this redraw cannot have changed a
                            ; character - only the selection moved - so a row
                            ; owes an INVERSION and no glyphs at all. One-shot
    NPVAR np_xs0,   2       ; word } the cells of the row being accumulated
    NPVAR np_xs1,   2       ; word } whose selected-ness DIFFERS from what the
                            ; screen shows: the symmetric difference, which is
                            ; exactly what one XOR has to flip
    NPVAR np_prs0,  2       ; word } and what the delta cache's row was last
    NPVAR np_prs1,  2       ; word } DRAWN with, so a selection that moved
                            ; over unchanged text still redraws (SPEC.md 27.8)
    NPVAR np_dpos,  2       ; word: a drag-and-drop's insertion point, or
                            ; 0xFFFF when no marker is on screen
    NPVAR np_dmark, 2       ; word } the marker's x and y, banked so the XOR
    NPVAR np_dmy,   2       ; word } that erases it cannot disagree with the
                            ; XOR that drew it - a scroll between the two
                            ; would leave the old bar on screen for good
    NPVAR np_dpx,   2       ; word } where the press landed, for the dead-zone
    NPVAR np_dpy,   2       ; word } test that keeps a click a click
    NPVAR np_mvs,   2       ; word } np_movesel's four numbers. In bss rather
    NPVAR np_mve,   2       ; word } than in registers because the three
    NPVAR np_mvp,   2       ; word } reversals want all four at once and the
    NPVAR np_mvn,   2       ; word } 8086 has nowhere to put them

; --- undo (SPEC.md 27.9) -----------------------------------------------------
; Five records, oldest first, and a heap arena holding their blobs packed in
; the same order. A record says: at [np_upos], this group INSERTED [np_uins]
; bytes and REMOVED the [np_udel] bytes at [np_uoff] in the arena. Undoing it
; is therefore "delete uins at upos, insert the blob at upos" and nothing else
; - which is why there is no redo and no second representation.
    NPVAR np_upos, NP_UNDO*2
    NPVAR np_uins, NP_UNDO*2
    NPVAR np_udel, NP_UNDO*2
    NPVAR np_uoff, NP_UNDO*2
                            ; there is no "caret before the group" word, and
                            ; there was one until it turned out to be derived:
                            ; an undo puts the caret at upos + udel, the end
                            ; of what it just put back, and that is right for
                            ; all four shapes a group can have - a typing run
                            ; (udel 0, so upos: where you started), a
                            ; backspace run (the position you backspaced
                            ; from), a cut and a paste
    NPVAR np_useg, 2        ; word: the arena claim, 0 = none held. Claimed on
                            ; the first edit worth remembering and given back
                            ; by File > New, a load and np_uclear
    NPVAR np_ukb,  2        ; word: its size in KB
    NPVAR np_utop, 2        ; word: bytes of it in use - also the top record's
                            ; blob end, which is what makes an append free
    NPVAR np_un,   1        ; byte: records live, 0..NP_UNDO
    NPVAR np_uopen, 1       ; byte: the newest is still ACCUMULATING. Closed
                            ; by NP_IDLE ticks of not editing (the worker), by
                            ; anything that is not an edit, and by an edit
                            ; that does not touch the group's own span
    NPVAR np_unolog, 1      ; byte: suppress recording - set while undo itself
                            ; is editing the buffer, or the undo of an undo
                            ; would be recorded as an edit
    NPVAR np_upad, 1        ; byte: keeps the words below even

; --- the find/replace panel (SPEC.md 27.10) ----------------------------------
    NPVAR np_fpan,  1       ; byte: NP_FPAN_*
    NPVAR np_ffield, 1      ; byte: NP_FF_*  - which box the keys go to
    NPVAR np_frx,   1       ; byte: the Regex box is ticked
    NPVAR np_fcok,  1       ; byte: [np_fcount] describes the live note and
                            ; the live pattern
    NPVAR np_fcdirty, 1     ; byte: ...and this says one is OWED, which the
                            ; worker pays half a second after the typing stops
                            ; - counting matches walks the whole note, and
                            ; doing it per keystroke in the box is exactly the
                            ; cost np_height was moved off the keystroke to
                            ; avoid
    NPVAR np_fbad,  1       ; byte: the pattern will not compile (too many
                            ; repeats for NP_RXST, or an unclosed class)
    NPVAR np_fpad,  1       ; byte: keeps the words below even
    NPVAR np_fcount, 2      ; word: matches in the note...
    NPVAR np_fmno,  2       ; word: ...and WHICH of them the selection is
                            ; showing, 1-based. 0 = not known, which is a real
                            ; state and not a failure: an edit or a click can
                            ; move the caret off every match, and the ordinal
                            ; is then the worker's to re-derive
    NPVAR np_lmx, 2
    NPVAR np_lmy, 2
    NPVAR np_fwrap, 1       ; byte: the last search ran off the end and came
                            ; back to the top - which is what turns "the next
                            ; match" into "match 1" without counting anything
    NPVAR np_fpad2, 1       ; byte: keeps the words after it even
    NPVAR np_fpatn, 2       ; word: characters in the pattern...
    NPVAR np_frepn, 2       ; word: ...and in the replacement
    NPVAR np_fpcur, 2       ; word: the caret in the focused box
    NPVAR np_fview, 2       ; word: the first character the box SHOWS. A
                            ; pattern is 47 characters and no box this runs on
                            ; is that wide, so the window slides to keep the
                            ; caret inside it - recomputed at every paint, so
                            ; nothing has to keep it in step
    NPVAR np_fmst,  2       ; word } the match the view is showing, so Replace
    NPVAR np_fmen,  2       ; word } knows what to replace without re-finding
    NPVAR np_fpat,  NP_PATMAX+1
    NPVAR np_frep,  NP_PATMAX+1
    NPVAR np_fnum,  16      ; the match count as digits, for the panel. 16 and
                            ; not 12 because np_pdrawn space-pads it to a
                            ; fixed span so a shorter answer erases a longer
                            ; one, and the pad has to fit the NUL after it
    NPVAR np_fbuf,  NP_PATMAX+2  ; a box's interior, space-padded and drawn as
                            ; ONE opaque font_run - the same reason np_rbuf is
                            ; (SPEC.md 6.1/27.2): a fill followed by glyphs
                            ; leaves the box blank in between, and on the
                            ; machine this is for that is visible
; the panel's geometry, computed by np_fpgeom and read by the painter and the
; hit test alike - the fm_hit discipline (SPEC.md 22), so a button cannot be
; drawn in one place and clicked in another
    NPVAR np_pl,    2       ; word } content left and right, inclusive
    NPVAR np_pr,    2
    NPVAR np_pt,    2       ; word: the panel's top row
    NPVAR np_pbtny, 2       ; word: the button row's top
    NPVAR np_pbx,   8       ; 4 words: each button's left edge, right to left
    NPVAR np_pbw,   8       ; 4 words: ...and its width. 0 = not shown
    NPVAR np_pcbx,  2       ; word: the Regex tick box's left edge
    NPVAR np_pnx,   2       ; word: ...and where the match count starts
    NPVAR np_pfx,   2       ; word } a text box's interior, left and right
    NPVAR np_pfr,   2

; --- the regex matcher's explicit backtrack stack (SPEC.md 27.10.1) ----------
; Four words a frame: the pattern offset to resume at, the text index the
; repeat started from, the fewest repetitions it may keep, and how many it is
; currently keeping. A frame is pushed per * + ? element and popped when it
; runs out of ways to give ground.
    NPVAR np_rxs, NP_RXST*8
    NPVAR np_rxsp, 2        ; word: frames in use
    NPVAR np_rxend, 2       ; word: one past the match, when one is found

; --- word wrap (SPEC.md 27.11) -----------------------------------------------
    NPVAR np_wstart, 1      ; byte: the index the walk is standing on begins a
                            ; word, so np_wordfit has a question to answer.
                            ; Maintained by the walk itself - set at its start,
                            ; by a space and by a line break, cleared by every
                            ; other character - because the alternative is
                            ; reading the character BEFORE the one in hand,
                            ; which a seeded walk cannot always do

; --- the chunked height count (SPEC.md 27.7.3) -------------------------------
; Where the count has got to, and where a bounded walk stopped. The two are
; separate because np_walk's .stop is shared by every bounded walk in the
; module - a paint, a caret key - and only np_height may keep what it reports.
    NPVAR np_hrow,  2       ; word: the ABSOLUTE row the next chunk resumes at,
                            ; 0 = from the top. Absolute and not visible,
                            ; because [np_top] may move between chunks and the
                            ; seed np_walk wants is relative to wherever the
                            ; view is when the chunk actually runs
    NPVAR np_hi,    2       ; word: the character index that row begins at.
                            ; np_seedrow cannot supply this - np_rows is
                            ; NP_MAXROWS long, one slot per VISIBLE row, so it
                            ; describes the view and can never name row 300 of
                            ; a 500-row note (SPEC.md 27.5)
    NPVAR np_sowed, 1       ; byte: a scroll moved [np_top] and its repaint was
                            ; dropped, because OSAPI_EVQ_PENDING said another
                            ; event was right behind it. The worker owes it
                            ; (SPEC.md 27.7.8) - and it must be the WORKER
                            ; rather than the next click, because the next
                            ; event in the queue may belong to another window
                            ; and this one would then never be drawn at all
    NPVAR np_stoprow, 2     ; word } what .stop reached, published every time
    NPVAR np_stopi, 2       ; word } and read only by np_height, on the walk it
                            ; issued itself - under the lock, with nothing
                            ; between the call and the read

; --- the row index (SPEC.md 27.13) -------------------------------------------
; 134 bytes, and it is what makes a row OUTSIDE the view reachable without
; laying out the note to get to it. Entry n is the character index at which
; absolute row n << [np_xksh] begins; the stride doubles rather than the table
; growing, so this is the whole cost however long the note is.
    NPVAR np_xi, NP_XN * 2  ; the indices, one per checkpoint
    NPVAR np_xn, 2          ; word: how many are valid, CONTIGUOUSLY from 0 -
                            ; a hole would answer for a row nobody walked
    NPVAR np_xnext, 2       ; word: the absolute row the next entry wants, kept
                            ; rather than derived so np_xnote is one compare
    NPVAR np_xksh, 1        ; byte: log2 of the stride. A log, because a lookup
                            ; is then a shift and not a 150-clock `div`
    NPVAR np_xpad, 1        ; byte: keep the words that follow even

; --- the append fast path (SPEC.md 27.14) ------------------------------------
    NPVAR np_ap1, 2         ; the one character np_append letters, NUL-capped:
                            ; OSAPI_FONT_RUN takes a string and this is the
                            ; whole of it. Not np_rbuf, which is the row cache
                            ; np_rflush diffs against and must not be disturbed
    NPVAR np_apch, 1        ; ...the character on its own, because AL is spent
    NPVAR np_aprow, 1       ; the tail is a hard NEWLINE, not the end of the
                            ; note - so rows below have shifted an index
    NPVAR np_apx,  2        ; word } the cell's x, the caret's x one along, and
    NPVAR np_apx2, 2        ; word } the row's y - banked because the far calls
    NPVAR np_apy,  2        ; word } between them return nothing of their own

; --- how deep a caret move can dirty (SPEC.md 27.4.1) ------------------------
    NPVAR np_mvbot, 2       ; word: the DEEPER of the two visible rows a caret
                            ; move touched - the one it left and the one it
                            ; arrived on - or 0x7FFF for "not a caret move, or
                            ; nowhere in particular". Written by np_move, which
                            ; is the only caller that knows both; np_fastcm
                            ; parks it at the sentinel, so Left and Right (kind
                            ; 4 as well, and adjacent rows they do not measure)
                            ; can never inherit an Up's bound

    NPVAR np_rbuf, NP_MAXCOL + 1  ; the row being accumulated, space-filled
    NPVAR np_prow, NP_MAXCOL      ; ...and what was last DRAWN on the cached
                                  ; row, so the next keystroke draws the delta.
                                  ; THE ONLY TWO FIELDS SIZED BY NP_MAXCOL,
                                  ; which is why they are here rather than in
                                  ; the hand-numbered block above (see the hole
                                  ; at +238 there)

%ifdef NPBENCH
; --- the walk bench (tests/npbench.inc), in the -DNPBENCH build only ---------
    NPVAR npb_buf, 640      ; the report, composed here and then copied into
                            ; the document claim: np_utoa writes through DS
    NPVAR npb_ls,  2        ; word: where the line being composed began
    NPVAR npb_t,   2        ; word } the row being emitted: ticks over
    NPVAR npb_i,   2        ; word } iterations
%endif

; --- closing (SPEC.md 27.15) -------------------------------------------------
; What the document looked like the last time it agreed with the disk. A
; CHECKSUM and not a second copy: NP_MAXKB is 16 and the note already lives in
; a heap claim, so keeping a shadow of it would double the one allocation this
; app cannot do without - on the machine least able to fund it. Three words
; against 16,384 bytes, and the comparison is exact about the thing that
; matters (an edit that is undone back to the original reads CLEAN, which a
; dirty FLAG set by every mutation could never say).
    NPVAR np_ds1,   2       ; word } Fletcher's two running sums over the
    NPVAR np_ds2,   2       ; word } document as it stands on disk...
    NPVAR np_dslen, 2       ; word: ...and its length, compared with them
                            ; because a sum pair alone is blind to a note that
                            ; was truncated at a point where the sums repeat
    NPVAR np_named, 1       ; byte: this note IS a file - it has been loaded,
                            ; saved, opened from a document double-click or
                            ; named in a dialog. A fresh note is NOT: [np_name]
                            ; holds 'NOTES.TXT' from np_defname so that Ctrl-S
                            ; always has somewhere to go, and quietly writing
                            ; that on the way out - over whatever NOTES.TXT the
                            ; user already had - is not a thing to do without
                            ; asking
    NPVAR np_asking, 1      ; byte: our SPEC.md 75.3 alert is up. The kernel
                            ; keeps one alert in the whole machine, so this is
                            ; only about ours
    NPVAR np_qclose, 1      ; byte: the Save As dialog now on screen was
                            ; started by that alert, so its commit is a QUIT
    NPVAR np_qbuf, OS88UI_AMAX + 1  ; the alert's line, composed around the
                            ; document's name. Not np_tbuf, which is 30 bytes
                            ; sized for 'Loaded ' + an 8.3 name and would take
                            ; this one to the byte

%assign NP_BSS_TOTAL NPB

    OS88_BSS NP_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = a fresh empty note with the caret at the origin and no toast.
np_len      equ os88_image_end + 0     ; word: characters used. The TEXT is
                                       ; not here any more - it is [np_dseg]
                                       ; below, a heap claim (SPEC.md 27.6)
np_tx       equ os88_image_end + 2   ; word: paint scratch, text origin x
np_rgt      equ os88_image_end + 4   ; word: content right, inclusive
np_bot      equ os88_image_end + 6   ; word: content bottom, inclusive
                                       ; +8..+17 FREE: [np_msg] and the four
                                       ; toast-box words. The toast is the
                                       ; kernel's now (SPEC.md 59) and is in
                                       ; the menu bar, so this app holds no
                                       ; state about it at all. The offsets
                                       ; below are unchanged deliberately -
                                       ; renumbering 200 hand-written equs to
                                       ; reclaim ten bytes of bss is a large
                                       ; risk for no measurable gain
np_name     equ os88_image_end + 18   ; 14: the current document, 8.3 + NUL
                                       ; (SPEC.md 27.1) - per INSTANCE, so
                                       ; two Note Pads hold two documents
np_tbuf     equ os88_image_end + 32   ; 26: 'Saved ' / 'Loaded ' + np_name
np_cur      equ os88_image_end + 62    ; word: THE CARET - the
                                       ; character index it sits in front of,
                                       ; 0..[np_len]. Everything below exists
                                       ; to move it or to answer where it is
np_ty       equ os88_image_end + 64    ; word: the first text row
np_i        equ os88_image_end + 66    ; word: np_walk's index
np_curx     equ os88_image_end + 68    ; word: the caret in pixels
np_cury     equ os88_image_end + 70
np_hitx     equ os88_image_end + 72    ; word: a click to resolve,
np_hity     equ os88_image_end + 74    ; 0xFFFF in y = no query
np_hiti     equ os88_image_end + 76    ; word: ...and its answer
np_wantx    equ os88_image_end + 78    ; word: a row and column to
np_wanty    equ os88_image_end + 80    ; find, 0xFFFF = no query
np_wanti    equ os88_image_end + 82    ; word: ...and its answer,
                                       ; 0xFFFF = there is no such row
np_draw     equ os88_image_end + 84    ; byte: np_walk paints
np_hitset   equ os88_image_end + 85    ; byte: the click row was
np_wantset  equ os88_image_end + 86    ; byte: ...the target row
np_pad2     equ os88_image_end + 87    ; byte: keeps the total even
np_dir      equ os88_image_end + 58    ; word: the folder the
np_drv      equ os88_image_end + 60    ; document lives in, byte:
np_dirok    equ os88_image_end + 61    ; its drive, byte: whether
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
; instance has - but nothing reads them until np_paint has written them,
; because np_sigok below is 0 until it does.
np_row      equ os88_image_end + 88    ; word: np_walk's visible row
np_rowh     equ os88_image_end + 90    ; word: its running fold
np_vrows    equ os88_image_end + 92    ; word: rows the content
                                       ; shows, capped at NP_MAXROWS
np_dr0      equ os88_image_end + 94    ; word: first dirty row
np_dr1      equ os88_image_end + 96    ; word: ...and the last.
                                       ; np_dr0 = 0xFFFF means none at all
np_stx      equ os88_image_end + 98    ; word } the geometry the
np_sty      equ os88_image_end + 100    ; word } signatures were
np_srgt     equ os88_image_end + 102    ; word } taken at, and the
np_sbot     equ os88_image_end + 104    ; word } taken at (np_sigsame)
                                       ; +106 FREE: np_smsg
np_sigup    equ os88_image_end + 108    ; byte: np_walk folds and
                                       ; compares
np_clip     equ os88_image_end + 109    ; byte: ...and draws only
                                       ; the dirty band
np_sigok    equ os88_image_end + 110    ; byte: np_sig has been
                                       ; written at least once
np_pad3     equ os88_image_end + 111    ; byte: keeps np_sig even
np_sig      equ os88_image_end + 112    ; NP_MAXROWS words: one
                                       ; per row of the content
np_rcols    equ os88_image_end + 232    ; word: cells the band holds
np_rby      equ os88_image_end + 234    ; word: y of the row being
                                       ; accumulated - BP has moved on by the
                                       ; time it is flushed
np_rcx      equ os88_image_end + 236    ; word: the caret's x on that
                                       ; row, 0xFFFF = it is not on this one
                                       ; np_rbuf and np_prow USED TO BE HERE,
                                       ; at +238 and +330, and they are in the
                                       ; NPVAR block at the foot of this file
                                       ; now - because they are the only two
                                       ; fields sized by NP_MAXCOL, and that
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
np_prowi    equ os88_image_end + 421    ; word: which row that is,
                                       ; 0xFFFF = the cache holds nothing
np_prcc     equ os88_image_end + 423    ; word: and where its caret
                                       ; was, so the cell it vacates is redrawn
np_flo      equ os88_image_end + 425    ; word } np_rflush's span,
np_fhi      equ os88_image_end + 427    ; word } 0xFFFF = empty
np_fcc      equ os88_image_end + 429    ; word: the caret's column
                                       ; on the row being flushed

; --- the document, and the heap it lives in (SPEC.md 27.6/50.3) ----------------
; The text itself is NOT in this package's region. np_entry claims NP_KB0 for
; it before it creates the window, np_room grows it a kilobyte at a time as
; the note fills, a load sizes it to the file and File > New gives it back.
; That is what an editor's buffer is: data whose size only the user knows,
; and a region is image + bss capped at APP_MAX_SIZE (SPEC.md 20.1).
np_dseg     equ os88_image_end + 433    ; word: the document claim's segment.
                                       ; The text is [np_dseg]:0000, and it
                                       ; is NEVER 0 while this instance lives:
                                       ; np_entry aborts the launch rather
                                       ; than open a window with nowhere to
                                       ; put the text, so nothing below has
                                       ; to test it
np_capkb    equ os88_image_end + 435    ; word: its size in KB...
np_cap      equ os88_image_end + 437    ; word: ...and in bytes, kept in step
                                       ; by np_resize. NP_MAXKB * 1024 fits a
                                       ; word, which is what bounds the note
                                       ; +441..+444 FREE: np_msgn and
                                       ; np_smsgn, the toast's GENERATION and
                                       ; the one the signatures were taken
                                       ; over. Both existed because every
                                       ; toast was composed into the same
                                       ; np_tbuf, so the POINTER could not
                                       ; tell "Saved X" from "Loaded X" -
                                       ; a distinction the kernel's copy
                                       ; makes for itself (SPEC.md 59.3)
np_stgseg   equ os88_image_end + 439    ; word: the save's CR/LF staging
                                       ; claim, 0 = not held. A SECOND claim,
                                       ; sized from [np_len] and taken only
                                       ; across the write, because expanding
                                       ; CR to CR LF grows and the document
                                       ; claim is sized for the document. A
                                       ; load needs none: the file lands in
                                       ; the document buffer and folds in
                                       ; place, which only ever shrinks
; --- the layout checkpoint (SPEC.md 27.4) ------------------------------------
; np_walk is O(the note), and it runs TWICE per keystroke - which is what a
; user found by filling a fullscreen window and watching each keystroke get
; slower while the delta cache above kept the drawing at two cells. Wrapping
; is a left-to-right automaton with no lookahead, so the pen state at index k
; depends only on the characters before it: an edit at the caret cannot
; change the layout of anything ahead of the caret. So the walk may RESUME at
; the start of the caret's row instead of starting at index 0, and the start
; of a row is (index, row) alone - the pen's x is always [np_tx] there and
; its y is always [np_ty] + 8*row.
np_ckpi     equ os88_image_end + 445    ; word } the checkpoint: the
np_ckpr     equ os88_image_end + 447    ; word } caret's row start
np_ckpc     equ os88_image_end + 449    ; word } and the candidate
np_ckpcr    equ os88_image_end + 451    ; word } np_rstart banks
np_win      equ os88_image_end + 453    ; word: our window, which a
                                       ; callback is handed but the worker
                                       ; has to remember
np_bcrow    equ os88_image_end + 455    ; word: the row the visual
                                       ; break sits on (SPEC.md 27.3)
np_borig    equ os88_image_end + 457    ; word: ...and the row it
                                       ; started on, which is the first row
                                       ; the reconcile has to repaint
np_ktick    equ os88_image_end + 459    ; word: the tick of the last
                                       ; keystroke, for the idle settle
np_ckok     equ os88_image_end + 461    ; byte: the checkpoint
                                       ; describes THIS layout
np_fast     equ os88_image_end + 462    ; byte: this redraw is a
                                       ; plain insert or backspace at the
                                       ; caret, inside the caret's own row.
                                       ; One-shot: np_redraw consumes it
np_resume   equ os88_image_end + 463    ; byte: np_walk starts at
                                       ; the checkpoint, not at index 0
np_bmode    equ os88_image_end + 464    ; byte: the visual break is
                                       ; up, so the screen is NOT the note
np_clean    equ os88_image_end + 465    ; byte: the band is known
                                       ; blank, so a row's run needs no
                                       ; trailing padding to erase with
np_brkok    equ os88_image_end + 466    ; byte: this machine is slow
                                       ; enough to want the break at all
np_hired    equ os88_image_end + 467    ; byte: the worker exists
np_didpush  equ os88_image_end + 468    ; byte: a push scrolled the
                                       ; band, so the grow box needs redrawing
np_bfail    equ os88_image_end + 469    ; byte: a push was REFUSED -
                                       ; the band left below the caret is
                                       ; shorter than a row - so the break
                                       ; cannot continue and must settle
np_bstop    equ os88_image_end + 470    ; byte: THIS walk ends at
                                       ; the caret. Its own flag and not
                                       ; "[np_bmode] is set", because
                                       ; np_measure answers clicks and arrow
                                       ; keys and has to see the whole note
                                       ; even while the break is up
np_rowsok   equ os88_image_end + 471    ; byte: np_rows describes
                                       ; this layout

; --- where each row starts (SPEC.md 27.5) ------------------------------------
; The checkpoint above is the caret's row start; this is EVERY row's, which is
; what turns a query about an arbitrary row into a bounded walk. A click, an
; Up and a Home all name a row and want the index at a column of it, and
; without this each of them re-derived the whole layout from index 0 to answer
; a question about thirty characters.
np_rows     equ os88_image_end + 472    ; NP_MAXROWS words
np_rowsn    equ os88_image_end + 472 + NP_MAXROWS*2   ; word: how
                                       ; many of them the last full walk wrote
np_sdi      equ os88_image_end + 474 + NP_MAXROWS*2   ; word } the
np_sdr      equ os88_image_end + 476 + NP_MAXROWS*2   ; word } seed
np_lastrow  equ os88_image_end + 478 + NP_MAXROWS*2   ; word: the
                                       ; last row this walk cares about. ONE-
                                       ; SHOT and reset to 0xFFFF by np_walk,
                                       ; so a caller that forgets gets the
                                       ; whole note - slow, never wrong
np_ecol     equ os88_image_end + 480 + NP_MAXROWS*2   ; word: the
                                       ; caret's column on its row BEFORE this
                                       ; edit - reported by the key handler,
                                       ; never derived (SPEC.md 27.3)
np_ekind    equ os88_image_end + 482 + NP_MAXROWS*2   ; byte: what
                                       ; THIS redraw is for - np_redraw's
                                       ; one-shot copy of [np_fast]'s kind
np_eext     equ os88_image_end + 483 + NP_MAXROWS*2   ; byte: cells
                                       ; of the row below that go stale beyond
                                       ; the caret's column (Delete: 1)

; --- scrolling (SPEC.md 27.7) ------------------------------------------------
np_top      equ os88_image_end + 484 + NP_MAXROWS*2   ; word: the note row
                                       ; drawn at the top of the view. np_row
                                       ; is the VISIBLE row and starts at
                                       ; MINUS this
np_drows    equ os88_image_end + 486 + NP_MAXROWS*2   ; word: how many rows
                                       ; the note occupies, from the last walk
                                       ; that ran to its natural end
np_sbr      equ os88_image_end + 488 + NP_MAXROWS*2   ; word: the content's
                                       ; own right edge - np_rgt stops
                                       ; NP_SB_W short of it now
np_sbb      equ os88_image_end + 490 + NP_MAXROWS*2   ; word: and the bar's own
                                       ; bottom, np_bot less the grow box
np_sbtop    equ os88_image_end + 492 + NP_MAXROWS*2   ; word } what the bar on
np_sbrows   equ os88_image_end + 494 + NP_MAXROWS*2   ; word } screen was drawn
                                       ; from, so a redraw that moved neither
                                       ; leaves it alone
np_follow   equ os88_image_end + 496 + NP_MAXROWS*2   ; byte: this redraw is
                                       ; one the CARET moved in, so the view
                                       ; owes it a place on screen. Its own
                                       ; flag and not "[np_ekind] is set":
                                       ; that one says which cheap redraw path
                                       ; is allowed, and Enter, Up, Down, Home
                                       ; and End are all 0 there while all
                                       ; five move the caret. One-shot, so a
                                       ; scroll bar click - which reaches the
                                       ; same np_redraw - cannot be dragged
                                       ; straight back to the caret
np_ptop     equ os88_image_end + 500 + NP_MAXROWS*2   ; word: the [np_top]
                                       ; the PIXELS on screen were drawn for.
                                       ; np_sig and np_rows are indexed by a
                                       ; visible row, so this is what says
                                       ; which view they describe - and a
                                       ; scroll is reconciled by shifting all
                                       ; three together (SPEC.md 27.7.2)
np_sdlt     equ os88_image_end + 502 + NP_MAXROWS*2   ; word } np_scrollpaint
np_bd0      equ os88_image_end + 504 + NP_MAXROWS*2   ; word } scratch: the
np_bd1      equ os88_image_end + 506 + NP_MAXROWS*2   ; word } delta, the band
np_hdirty   equ os88_image_end + 498 + NP_MAXROWS*2   ; byte: the note
                                       ; changed, so [np_drows] is a lower
                                       ; bound rather than the height. Set by
                                       ; np_ins/np_del/np_clamp, cleared by
                                       ; any walk that reached the note's end
np_curseen  equ os88_image_end + 499 + NP_MAXROWS*2   ; byte: THIS walk stood
                                       ; on the caret, so [np_cury] is its
                                       ; position and not the initial 0. A
                                       ; bounded walk can stop above it
np_gchg     equ os88_image_end + 497 + NP_MAXROWS*2   ; byte: the geometry
                                       ; changed since the last paint, so the
                                       ; row count did too and [np_top] has
                                       ; not been clamped against it yet. Set
                                       ; by np_bounds, spent by np_paint,
                                       ; cleared by np_sigmark
                                       ; total 974 = NP_BSS_TOTAL
