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
; at the content width with 6px margins, rows that would overflow the
; content bottom are dropped rather than scrolled, and a 1px caret follows
; the text when its own row fits.
;
; What is new is SPEC.md 27.1: F2 saves the note to NOTES.TXT on the mounted
; data disk and F3 loads it back, over the file API of SPEC.md 18.4 - which
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
; existing routines: "Open" is np_load, "Save" is np_save, F3 and F2 still
; call exactly the same two, and both doors end at np_redraw. "New" is the
; one thing here that is genuinely new rather than a second door - emptying
; the buffer had no key and no button before - and it is menu-only for the
; same reason it was missing: there was no spare key worth spending on it.
;
; The state lookup is the one thing that got simpler. The built-in reached
; its state through inst_of_win -> I_SPTR because all instances shared one
; pool; a package addresses its own bss directly.
; =============================================================================

%include "os88api.inc"

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
NP_MAXCOL    equ 91             ; cells a row can hold: 720/8 is the widest
                                ; screen this runs on, plus one for the NUL.
                                ; A row is accumulated into a buffer and drawn
                                ; as ONE opaque font_run (SPEC.md 6.1/27.2)
NP_MAXROWS   equ 60             ; signature slots, one per row the content can
                                ; show (SPEC.md 27.2). The tallest this window
                                ; can be is a fullscreen VGA frame, where the
                                ; frame IS the content (SPEC.md 11.2): 480 rows
                                ; less the 6px top margin and the 7px a row's
                                ; own band needs is 59. np_bounds clamps to
                                ; this, so a taller screen degrades to "the
                                ; rows past 60 are always redrawn" rather than
                                ; writing past the array
NP_BSS_TOTAL equ 508 + NP_MAXROWS*2  ; see the bss layout below
NP_BRK_CELLS equ 60             ; the visual break's trigger (SPEC.md 27.3):
                                ; the CELLS a keystroke would repaint BELOW
                                ; the caret's row. Not rows - this window is
                                ; resizable and a row is 30 cells or 90
                                ; depending how wide the user dragged it, so
                                ; a row count means two different amounts of
                                ; work. 60 cells is about 60ms on a 4.77MHz
                                ; 8088 (SPEC.md 6.1.1), which is the point
                                ; where a keystroke stops keeping up
NP_IDLE      equ 9              ; ticks of no typing before the break is
                                ; reconciled: ~500ms at 18.2Hz
NP_WTICKS    equ 3              ; ...and how often the worker looks, ~165ms.
                                ; Finer than NP_IDLE so the settle lands
                                ; near the deadline rather than a tick late
NP_SB_W      equ 14             ; scroll bar width, the Disk window's
                                ; (SPEC.md 22) so the two look like one OS.
                                ; It is reserved ALWAYS, present or not:
                                ; whether a bar is NEEDED depends on the row
                                ; count, which depends on the wrap width,
                                ; which would depend on the bar
NP_SB_ARR    equ 11             ; ...and the arrow cells at each end of it
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
NP_KEY_SAVE  equ 0x3C           ; F2 scan code (DOS Editor's keys)
NP_KEY_LOAD  equ 0x3D           ; F3
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
    mov ax, NP_KB0                  ; the document, before anything else: an
    call OSAPI_MEM_CLAIM            ; editor with nowhere to put the text is
    jc .nomem                       ; not a window worth opening, and the
    mov [np_dseg], dx               ; loader's LD_EABORT says so for us
    mov word [np_capkb], NP_KB0
    mov word [np_cap], NP_KB0 * 1024
    push si
    mov si, np_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): np_paint
    call OSAPI_WM_SIZABLE           ; already lays out from the live record,
    mov al, 1                       ; so the next repaint re-wraps for free
    call OSAPI_WM_SNAP              ; ...and snapped (SPEC.md 11.94), because
    pop ax                          ; every keystroke redraws a row of text and
                                    ; an aligned cell writes ONE framebuffer
                                    ; byte where an unaligned one writes two.
                                    ; A no-op on VGA, so it is unconditional
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
    pushf                           ; ...and so must this: the bss arrives
    call np_defname                 ; zeroed and an empty name is not a file
    popf                            ; (SPEC.md 27.1), but np_defname is an
                                    ; ordinary routine and the CF we owe the
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
; np_thumb - the thumb's geometry, shared by the painter and the hit test
; in:  np_bounds run
; out: CF=1 no thumb (it all fits, or the track is too short); else CF=0,
;      AX = thumb top (absolute y), DX = thumb height
; clobbers: nothing else
;
; The Disk window's arithmetic (SPEC.md 22): height = fit*track/total with an
; 8px floor, top = scroll*track/total, clamped so the bottom of the thumb
; cannot leave the track.
; -----------------------------------------------------------------------------
np_thumb:
    push bx
    push cx
    mov ax, [np_drows]
    cmp ax, [np_vrows]
    jbe .none
    call np_trackh                  ; BX = track height
    cmp bx, 8
    jl .none                        ; degenerate track: bare, no thumb
    mov ax, [np_vrows]
    mul bx
    div word [np_drows]             ; AX = proportional height
    cmp ax, 8
    jge .hok
    mov ax, 8
.hok:
    mov cx, ax
    mov ax, [np_top]
    mul bx
    div word [np_drows]             ; AX = offset down the track
    add ax, [np_ty]
    add ax, NP_SB_ARR               ; ...from the track's top
    mov dx, [np_ty]                 ; clamp: top <= track top + track - height
    add dx, NP_SB_ARR
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

; np_trackh - BX = the grey track's height, between the two arrow cells
np_trackh:
    push ax
    mov bx, [np_sbb]
    sub bx, [np_ty]
    inc bx
    sub bx, NP_SB_ARR*2
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_sbar - draw the scroll bar
; in:  SI = window ptr, np_bounds run, gfx lock held
; out: nothing; clobbers what a callback may
; -----------------------------------------------------------------------------
np_sbar:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov di, [np_sbr]
    sub di, NP_SB_W-1               ; DI = the bar's left column, absolute

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di                      ; the box
    mov bx, [np_ty]
    mov cx, [np_sbr]
    mov dx, [np_sbb]
    call OSAPI_GFX_FRAME

    mov ax, di                      ; the two arrow-cell rules
    mov bx, [np_sbr]
    mov dx, [np_ty]
    add dx, NP_SB_ARR-1
    call OSAPI_GFX_HLINE
    mov ax, di
    mov bx, [np_sbr]
    mov dx, [np_sbb]
    sub dx, NP_SB_ARR-1
    call OSAPI_GFX_HLINE

    xor bp, bp                      ; up glyph: 5 rows, widths 1..9
    mov dx, [np_ty]
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
    mov dx, [np_sbb]
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
    mov bx, [np_ty]
    add bx, NP_SB_ARR
    mov cx, [np_sbr]
    dec cx
    mov dx, [np_sbb]
    sub dx, NP_SB_ARR
    cmp bx, dx
    jg .done                        ; degenerate track: leave it framed
    call OSAPI_GFX_FILL_GRAY

    call np_thumb                   ; CF=1: it all fits, no thumb
    jc .done
    mov bx, ax                      ; y1 = thumb top
    add dx, ax
    dec dx                          ; y2 = top + height - 1
    mov ax, di
    add ax, 2
    mov cx, [np_sbr]
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
    mov cx, [np_sbr]
    sub cx, 2
    call OSAPI_GFX_FRAME
.done:
    mov ax, [np_top]                ; remember what is on screen, so a redraw
    mov [np_sbtop], ax              ; that moved neither number draws nothing
    mov ax, [np_drows]
    mov [np_sbrows], ax
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_sbcheck - redraw the bar only if it would look different
; in:  SI = window ptr, gfx lock held; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
np_sbcheck:
    push ax
    mov ax, [np_top]
    cmp ax, [np_sbtop]
    jne .draw
    mov ax, [np_drows]
    cmp ax, [np_sbrows]
    je .out
.draw:
    call np_sbar
.out:
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
np_sbclick:
    push ax
    push bx
    push dx
    mov bx, dx                      ; BX = the click's y; np_thumb wants DX
    mov ax, [np_sbr]
    sub ax, NP_SB_W-1
    cmp cx, ax
    jb .no
    cmp bx, [np_ty]
    jb .no
    cmp bx, [np_sbb]
    ja .no
    mov ax, [np_ty]
    add ax, NP_SB_ARR
    cmp bx, ax
    jb .lineup
    mov ax, [np_sbb]
    sub ax, NP_SB_ARR
    cmp bx, ax
    ja .linedn
    call np_thumb                   ; AX = thumb top, DX = its height
    jc .yes                         ; it all fits: the bar is inert, but ours
    cmp bx, ax
    jb .pageup
    add ax, dx
    cmp bx, ax
    jae .pagedn
    jmp short .yes                  ; on the thumb itself
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
    call np_scrollto
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
    cmp word [np_msg], 0
    jne .nope                       ; a toast sits OVER the text and is in no
                                    ; row's signature: the blit would carry it
                                    ; off its own frame and nothing would put
                                    ; it back
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
    je .noseed
    mov ax, [np_bd0]
    or ax, ax
    jz .noseed                      ; the band starts at the top of the view:
    dec ax                          ; there is no earlier row to start from
    mov bx, ax                      ; np_rows is valid up to here and no
    inc bx                          ; further, the entries above the band
    mov [np_rowsn], bx              ; having just been shifted out of range
    mov dx, [np_vrows]
    call np_seedrow
.noseed:
    mov ax, [np_vrows]
    mov [np_lastrow], ax
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
    add dx, NP_MARGIN
    mov [np_ty], dx
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
.geom:
    ; The checkpoint and np_rows are ROW INDICES, so they mean nothing under a
    ; different geometry - and unlike the signatures, nothing else was going to
    ; notice. np_sigsame guards np_redraw; this guards everything else, which
    ; is every caret key and every click.
    mov ax, [np_tx]
    cmp ax, [np_stx]
    jne .stale
    mov ax, [np_ty]
    cmp ax, [np_sty]
    jne .stale
    mov ax, [np_rgt]
    cmp ax, [np_srgt]
    jne .stale
    mov ax, [np_bot]
    cmp ax, [np_sbot]
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
    jbe .fits
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
    push ax                         ; fold it in whatever this pass is for:
    xor ah, ah                      ; the pass that COMPUTES the signatures is
    call np_fold                    ; a measure pass, so this cannot hang off
    pop ax                          ; np_draw
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
.nocell:                            ; clamped np_rcols is the case where it can
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
    mov ax, [np_row]
    add ax, [np_top]
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
    mov ax, [np_i]                  ; where this row STARTS, banked as the
    mov [np_ckpc], ax               ; checkpoint candidate: np_ask promotes it
    mov ax, [np_row]                ; the moment the walk stands on the caret
    mov [np_ckpcr], ax              ; (SPEC.md 27.4)
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
    cmp word [np_flo], 0xFFFF
    je .cache                       ; nothing moved: draw NOTHING

.draw:
    cmp byte [np_clean], 0          ; the band is known blank (a full repaint
    je .draw2                       ; white-filled it, SPEC.md 27.2), so the
    mov bx, [np_fhi]                ; padding has nothing to erase and the run
.tl:                                ; stops at the last real character. A
    cmp byte [np_rbuf+bx], ' '      ; fullscreen window is 90 cells wide and a
    jne .tdone                      ; note is rarely that long: without this a
    cmp bx, [np_flo]                ; repaint costs rows x width instead of
    jbe .cache                      ; characters, and on a 4.77MHz 8088 that
    dec bx                          ; is the difference between half a second
    jmp short .tl                   ; and five
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
;     the two mono adapters (SPEC.md 11.94) - which are the ones a 4.77MHz
;     machine has - and on VGA it is a coin flip, so there the break simply
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
    call np_toast
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
    je .loop                        ; other thing worth waking up for
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
    mov si, [np_win]
    cmp byte [np_bmode], 0          ; re-read UNDER the lock: the UI task may
    je .height                      ; have settled it while we waited
    mov bx, [np_win]
    call OSAPI_WM_OBSCURED
    jc .height
    call np_reconcile
.height:
    ; Count the note's rows, which no other walk does any more (SPEC.md 27.7),
    ; and move the thumb if that changed it. NOT gated on the window being
    ; visible: this is arithmetic, np_sbcheck draws only when a number moved,
    ; and a covered window's bar is redrawn by W_PAINT anyway.
    cmp byte [np_hdirty], 0
    je .unlock
    call np_bounds                  ; the walk reads [np_ty]/[np_rgt], and the
    call np_height                  ; window may have been resized since
    mov bx, [np_win]
    call OSAPI_WM_OBSCURED
    jc .unlock
    call np_sbcheck
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
    mov ax, [np_tx]
    mov [np_stx], ax
    mov ax, [np_ty]
    mov [np_sty], ax
    mov ax, [np_rgt]
    mov [np_srgt], ax
    mov ax, [np_bot]
    mov [np_sbot], ax
    mov ax, [np_msg]
    mov [np_smsg], ax
    mov ax, [np_msgn]
    mov [np_smsgn], ax
    mov byte [np_sigok], 1
    mov byte [np_gchg], 0           ; this paint laid the note out under the
    pop ax                          ; geometry just recorded, so the view has
    ret                             ; been re-clamped against it

; -----------------------------------------------------------------------------
; np_sigsame - do the stored signatures still describe this window?
; in:  np_bounds already run
; out: CF = 1 if they do not and the caller must repaint whole; preserves all
;
; Four of the five tests are the layout: a resized window wraps differently,
; and the kernel white-filled its content on the way here anyway. The fifth is
; the toast, which is drawn OVER the text by np_toast and is in no row's
; signature - so the keystroke that retires one has to erase it the only way
; this module can, by painting the content again.
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
    mov ax, [np_msg]
    cmp ax, [np_smsg]
    jne .no
    mov ax, [np_msgn]           ; ...and its GENERATION, not just the pointer.
    cmp ax, [np_smsgn]          ; "Saved X" and "Loaded X" are both composed
    jne .no                     ; into np_tbuf, so [np_msg] is the same word
    pop ax                      ; for both and a save straight after a load
    clc                         ; left the window still saying "Loaded"
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
; -----------------------------------------------------------------------------
np_height:
    cmp byte [np_hdirty], 0
    jne .go
    ret
.go:
    push ax
    push si
    mov ax, [np_hity]
    push ax
    mov ax, [np_wanty]
    push ax
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    mov byte [np_draw], 0
    mov byte [np_sigup], 0
    mov byte [np_clip], 0
    mov byte [np_resume], 0         ; from index 0, and to the last character:
    call np_walk                    ; anything less is what we already have
    pop ax
    mov [np_wanty], ax
    pop ax
    mov [np_hity], ax
    pop si
    pop ax
    ret

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
np_seedck:
    push ax
    mov byte [np_resume], 0
    cmp byte [np_ckok], 0
    je .out
    mov ax, [np_ckpi]
    mov [np_sdi], ax
    mov ax, [np_ckpr]
    mov [np_sdr], ax
    mov byte [np_resume], 1
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
    call np_measure                 ; nothing: measure the note under the new
    mov ax, [np_top]                ; wrap width and put the view back inside
    call np_scrollto                ; it. np_bmode is 0 above, so np_settle
    mov byte [np_resume], 0         ; inside np_measure is a no-op and this is
.laidout:                           ; just the walk
    mov byte [np_draw], 1
    mov byte [np_sigup], 1          ; the content was white-filled on the way
    mov byte [np_clip], 0           ; here, so this pass draws every row AND is
    mov byte [np_clean], 1          ; the baseline every later incremental
    mov ax, [np_vrows]              ; ...and it stops at the bottom of the view
    mov [np_lastrow], ax            ; like every other walk, because a full
                                    ; repaint of a 16KB note otherwise walks
                                    ; 16KB of it to draw one screenful - and
                                    ; every scroll step is a full repaint
    call np_walk                    ; (SPEC.md 27.7/27.2)
    mov byte [np_clean], 0          ; ...and because it WAS filled, a row's run
    call np_sigmark                 ; stops at its last character instead of
    mov ax, [np_top]                ; padding to the band's edge to erase with
    mov [np_ptop], ax               ; ...and the screen now shows THIS view
    pop ax
    call np_sbar                    ; the fill took the bar with it
    call np_toast                   ; last, so it sits above the text
    ret

; -----------------------------------------------------------------------------
; np_toast - draw the save/load result over the content's top-right corner
; in:  SI = window ptr, [np_msg] = the string (0 = nothing to say)
; out: nothing; preserves all registers
;
; A framed white box, so it reads over whatever text is under it, clamped to
; the content's left edge on a narrow window. It is drawn from np_paint and
; cleared by the next keystroke, so it can never become stale furniture.
; -----------------------------------------------------------------------------
np_toast:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [np_msg], 0
    je .out
    mov bx, si                  ; BX = window ptr
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov di, ax                  ; DI = content left (the clamp)
    add dx, 2
    mov [np_by1], dx
    add dx, 11
    mov [np_by2], dx
    call OSAPI_WM_GEOM          ; CX = content width (BX is still the window;
    mov ax, di                  ; DX is dead here, both strip rows are stored)
    add ax, cx
    sub ax, 3                   ; 2px frame + a 2px gap from the edge
    mov [np_bx2], ax
    push ax
    mov si, [np_msg]
    call OSAPI_FONT_WIDTH       ; AX = the string's pixel width
    add ax, 7                   ; 4px left pad, 3px right
    mov cx, ax
    pop ax
    sub ax, cx
    cmp ax, di
    jae .xok
    mov ax, di                  ; a narrow window: start at the content left
.xok:
    mov [np_bx1], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [np_bx1]
    mov bx, [np_by1]
    mov cx, [np_bx2]
    mov dx, [np_by2]
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [np_bx1]
    mov bx, [np_by1]
    mov cx, [np_bx2]
    mov dx, [np_by2]
    call OSAPI_GFX_FRAME
    mov cx, [np_bx1]
    add cx, 4
    mov dx, [np_by1]
    add dx, 2
    mov si, [np_msg]
    call OSAPI_FONT_STR
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    mov word [np_msg], np_e_nomem
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
; out: nothing; [np_msg] reports the outcome; preserves all registers
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
    jmp short .done
.err:
    call np_errmsg              ; AX = FERR_* -> the toast
.done:
    call np_stgdrop
.out:
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
; out: nothing; [np_msg] reports the outcome; preserves all registers
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
    mov es, [np_dseg]
    xor bx, bx                  ; ES:BX = the document, DX:CX its capacity
    mov cx, [np_cap]
    xor dx, dx
    mov si, np_name
    call OSAPI_FILE_READ        ; DX:AX = bytes read, and DX is 0 - a longer
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
    test dh, dh
    jz .done
    mov word [np_msg], np_m_trunc
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
; out: [np_msg]; preserves all registers
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
    mov bx, [bx+np_errtab]
    mov [np_msg], bx
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
    mov byte [np_hdirty], 1         ; the note is a different length, so it
                                    ; may be a different number of rows, and
                                    ; [np_drows] is a lower bound until
                                    ; something walks the whole of it
    mov es, [np_dseg]               ; did (SPEC.md 27.6)
    mov bx, [np_len]
    mov cx, bx
    sub cx, [np_cur]                ; CX = the bytes to the right of the caret
    mov si, bx
    dec si                          ; SI = the last live byte
    mov di, si
    inc di
    jcxz .place
.mv:
    mov al, [es:si]
    mov [es:di], al
    dec si
    dec di
    loop .mv
.place:
    mov bx, [np_cur]
    mov [es:bx], dl
    inc word [np_len]
    inc word [np_cur]
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
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, [np_cur]
    cmp bx, [np_len]
    jae .out
    mov byte [np_hdirty], 1         ; ...and shrinking counts too, though this
                                    ; is the direction the lower bound cannot
                                    ; follow (SPEC.md 27.7)
    mov es, [np_dseg]
    mov cx, [np_len]
    sub cx, bx
    dec cx                          ; CX = the bytes that move down
    mov di, bx
    mov si, di
    inc si
    jcxz .close
.mv:
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    loop .mv
.close:
    dec word [np_len]
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
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
    jc .full
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    mov dx, ax
    call np_seedrow
.full:
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
    cmp ax, [np_ckpr]
    jae .arm                        ; already at or after the checkpoint
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
    mov byte [np_fast], 3
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
    je .nolim
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
    mov [np_hitx], cx
    mov [np_hity], dx
    mov word [np_wanty], 0xFFFF
    call np_settle                  ; the pointer has to be over the NOTE
    call np_bounds                  ; before it can be resolved (SPEC.md 27.3)
    call np_height                  ; ...and a click on the BAR is the one
                                    ; place the note's height has to be exact
                                    ; rather than a lower bound, because it is
                                    ; what the thumb and the paging are a
                                    ; fraction of. One walk, on a click, and
                                    ; only if something was typed since the
                                    ; last one (SPEC.md 27.7)
    call np_sbclick                 ; ...and the scroll bar is not the note
    jc .text
    pop dx
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
    cmp ax, [np_cur]
    je .out                         ; the caret did not move: no repaint
    mov [np_cur], ax
    mov word [np_msg], 0
    call np_redraw
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_clamp - hold the caret inside the buffer after a load or a New
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_clamp:
    push ax
    mov byte [np_hdirty], 1         ; a whole new note is a whole new height
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

    cmp ah, NP_KEY_SAVE
    jne .nosave
    call np_save
    jmp .redraw                 ; near: the key ladder below outruns a short
.nosave:                        ; jump
    cmp ah, NP_KEY_LOAD
    jne .noload
    mov al, FDLG_OPEN           ; F3 ASKS now (SPEC.md 27.1): a load with no
    call np_dlgopen             ; way to say what was never the useful half.
    jmp .out                    ; No repaint - the dialog is on top of us
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
    call np_fastok                  ; a printable at the caret is THE case the
                                    ; incremental paths exist for (SPEC.md
                                    ; 27.3/27.4); Enter is not, and jumps in
                                    ; below this
.append:
    call np_ins                     ; at the caret, which follows it
    jmp short .edited

.bksp:
    cmp word [np_cur], 0
    je .out                         ; nothing to the left of the caret
    call np_fastokb                 ; ...and so is a backspace, as long as the
    dec word [np_cur]               ; character it eats is on the caret's own
    call np_del                     ; row
    jmp short .edited

.del:
    mov ax, [np_cur]                ; forward delete: the caret stays put
    cmp ax, [np_len]
    jae .out
    call np_fastokd
    call np_del
    jmp short .edited

.left:
    cmp word [np_cur], 0
    je .out
    call np_fastokm                 ; a caret move is not an edit, but the row
    dec word [np_cur]               ; above it still cannot have changed
    jmp short .edited
.right:
    mov ax, [np_cur]
    cmp ax, [np_len]
    jae .out
    call np_fastokr
    inc word [np_cur]
    jmp short .edited
.up:
    mov dx, -8
    call np_vmove
    jmp short .edited
.down:
    mov dx, 8
    call np_vmove
    jmp short .edited
.home:
    xor dx, dx
    call np_hmove
    jmp short .edited
.end:
    mov dx, 0x7FFF
    call np_hmove

.edited:                        ; an edit retires the toast; an unhandled
    mov word [np_msg], 0        ; key leaves both it and the screen alone
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
    or al, al
    jz .noseed
    call np_seedck                  ; only an edit at the caret may skip the
.noseed:                            ; rows above it (SPEC.md 27.4)
    cmp byte [np_resume], 0         ; ...and failing that, the top of the VIEW
    jne .seeded1                    ; is a seed too: np_rows[0] is the index
    cmp byte [np_rowsok], 0         ; row 0 of the content starts at, rows
    je .seeded1                     ; above it have neither pixels nor
    xor ax, ax                      ; signatures, and nothing above the caret
    mov dx, [np_vrows]              ; can have reflowed anyway - which is the
    call np_seedrow                 ; same claim 27.4 already makes, applied
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
    mov [np_lastrow], ax            ; one row past it a caret can wrap onto
                                    ; (SPEC.md 27.7). Below that a row has no
                                    ; signature, cannot be dirty and is drawn
                                    ; by nobody - the only thing that ever
                                    ; wanted it was the note's total height,
                                    ; and np_height owns that now. Typing in
                                    ; the middle of a long note used to walk
                                    ; every row beneath the window on every
                                    ; keystroke: 72% of the work, for a thumb
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
    mov byte [np_resume], 0
    mov word [np_lastrow], 0x7FFF
    call np_measure
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
    mov [np_bandb], dx              ; ...banked for the grow-box test below
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

    ; The grow box is redrawn only if this redraw could have touched it, and
    ; that test is new. It used to be unconditional, and it HAD to be: the band
    ; fill spanned the full content width, so any dirty row level with the box
    ; erased it - and there was no cheap way to know which. It is 13x13 at
    ; (np_rgt-12, np_bot-12), the bottom-right corner of the content, and the
    ; only things that reach it now are the right-margin fill and the last text
    ; row, both bounded by the dirty band's rows. So one comparison answers it.
    ;
    ; Unconditional, it redrew the box on EVERY KEYSTROKE - and wm_grow_paint
    ; fills the square before it frames it, which is the erase-and-letter flash
    ; all over again in one 13x13 corner. Typing anywhere in the note made the
    ; resize handle flicker, which is exactly what a user saw once the rows
    ; themselves had stopped.
    mov ax, [np_bot]
    sub ax, 12
    cmp [np_bandb], ax
    jb .nogrow
    mov bx, si
    call OSAPI_WM_GROW
.nogrow:
    call np_sbcheck                 ; a note that gained or lost a row moves
    call np_toast                   ; the thumb, and nothing else redraws it
                                    ; on this path
.done:
    cmp byte [np_hdirty], 0         ; the height is a lower bound and only the
    je .nohire                      ; worker puts it right, so a note that has
    mov ax, [np_drows]              ; outgrown its window needs one hired -
    cmp ax, [np_vrows]              ; same lazy rule as the visual break, and
    jbe .nohire                     ; for the same reason: a note that fits
    call np_hire                    ; needs neither
.nohire:
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
    mov word [np_msg], 0
    call np_clamp               ; a no-op on the caret, which is already 0 -
                                ; it is here for the invalidation np_clamp
                                ; carries (SPEC.md 27.4/27.5)
    push ax                     ; ...and give the heap back what the old note
    mov ax, NP_KB0              ; had grown into. A shrink always succeeds in
    call np_resize              ; place, so this cannot fail (SPEC.md 50.3.1)
    pop ax
    jmp np_defname              ; a new note is a new document: leaving the
                                ; old name would make the next F2 overwrite
                                ; the file the user just walked away from

; -----------------------------------------------------------------------------
; np_oncmd - AM_ONCMD: run a menu command (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0 = File), SI = window ptr,
;      BX = our menu set ptr; gfx lock held by the caller, UI task
; out: nothing; clobbers AX/BX/CX/DX/DI/ES like any window callback
;
; Open and Save are menu-driven twins of F3 and F2 - the same np_load /
; np_save the keyboard path calls, so the two doors can never drift apart;
; New is menu-only, and empties the buffer. Every item changes
; what the window shows - the text, the toast, or both - and the kernel does
; not repaint for us, so all three tails run through np_redraw. The menu
; index is tested even though we register only one menu: the argument is
; kernel input, and an unknown pair must do nothing rather than fall into
; the first case.
; -----------------------------------------------------------------------------
np_oncmd:
    test ah, ah
    jnz .out                        ; not File: nothing of ours
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

; -----------------------------------------------------------------------------
; np_dlgopen - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: nothing; preserves all registers
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
    jmp short .draw
.load:
    call np_load
.draw:
    jmp np_redraw                   ; tail call; SI is the window ptr

; -----------------------------------------------------------------------------
; np_goto - put the volume back in this document's folder (SPEC.md 19.2)
; out: nothing; preserves all registers
;
; A file name resolves in the volume's CURRENT directory, and that is one
; global word shared by every Disk window and by the file dialog. Right after
; Save As it still names the folder the user picked - which is why saving
; into a folder worked - but by the next Save anything that navigated has
; moved it, and the write landed in the root. The pair OSAPI_FILE_HERE
; recorded is what says otherwise.
;
; A remount is real floppy I/O, so it is skipped when the volume is already
; there, which is the common case.
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
; np_defname - seed the current document name (internal)
; in:  nothing
; out: np_name = 'NOTES.TXT'; preserves all registers
; The loader zeroes our bss (SPEC.md 21 step 5), and an empty name would
; make F2 fail with FERR_NAME on a brand-new note - so a fresh Note Pad
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
; out: np_tbuf holds prefix + np_name and [np_msg] points at it; preserves
;      all registers
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
    inc word [np_msgn]          ; a new toast, even at the same address
    mov word [np_msg], np_tbuf
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
; buffer, Open is F3, Save is F2. AM_NAME reuses np_ttl so the bar label and
; the window title are the same eight characters by construction. The bar
; runs 38 + 64 ('Note Pad') + 16 = 118 to the File cell's left edge, and
; 32 + 12 more to its right edge at 162 - nowhere near the clock at 434.
    OS88_MENUSET np_menus, np_ttl, np_oncmd
        OS88_MENU np_m_file, np_items_file, 4
    OS88_MENUSET_END np_menus

np_m_file:     db 'File', 0
np_items_file: dw np_i_new, np_i_open, np_i_save, np_i_saveas  ; = NP_MI_*
np_i_new:      db 'New', 0
np_i_open:     db 'Open...', 0      ; the ellipsis is the convention and it
np_i_save:     db 'Save', 0         ; is honest: these two ask a question
np_i_saveas:   db 'Save As...', 0   ; first (SPEC.md 38), Save never does

; --- the file and what the toast can say (SPEC.md 27.1) ------------------------
; The name is per-instance state now (np_name in bss), seeded from this at
; launch and replaced by whatever the file dialog returns. The two verbs are
; PREFIXES: np_setmsg composes them with the live name into np_tbuf, because
; a toast that still said NOTES.TXT after a Save As would be worse than no
; toast at all.
np_s_default: db 'NOTES.TXT', 0
np_m_saved:   db 'Saved ', 0
np_m_loaded:  db 'Loaded ', 0
np_m_trunc:   db 'Truncated', 0

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
np_msg      equ os88_image_end + 8   ; word: toast string, 0 = none
np_bx1      equ os88_image_end + 10   ; word: the toast box, computed in
np_by1      equ os88_image_end + 12   ; np_toast and used by three calls
np_bx2      equ os88_image_end + 14
np_by2      equ os88_image_end + 16
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
np_sbot     equ os88_image_end + 104    ; word } toast that was over
np_smsg     equ os88_image_end + 106    ; word } them (np_sigsame)
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
np_rbuf     equ os88_image_end + 238    ; NP_MAXCOL+1 bytes: the row
                                       ; being accumulated, space-filled
np_prow     equ os88_image_end + 330    ; NP_MAXCOL bytes: what was
                                       ; last DRAWN on the cached row, so the
                                       ; next keystroke can draw the delta
np_prowi    equ os88_image_end + 421    ; word: which row that is,
                                       ; 0xFFFF = the cache holds nothing
np_prcc     equ os88_image_end + 423    ; word: and where its caret
                                       ; was, so the cell it vacates is redrawn
np_flo      equ os88_image_end + 425    ; word } np_rflush's span,
np_fhi      equ os88_image_end + 427    ; word } 0xFFFF = empty
np_fcc      equ os88_image_end + 429    ; word: the caret's column
                                       ; on the row being flushed
np_bandb    equ os88_image_end + 431    ; word: the dirty band's last
                                       ; row, for the grow-box test

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
np_msgn     equ os88_image_end + 441    ; word: the toast's GENERATION, bumped
np_smsgn    equ os88_image_end + 443    ; word: ...and the one the signatures
                                       ; were taken over. np_setmsg composes
                                       ; every toast into the same np_tbuf, so
                                       ; the POINTER cannot tell "Saved X"
                                       ; from "Loaded X" and np_sigsame used
                                       ; to skip the repaint between them
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
