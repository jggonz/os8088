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

; --- the document lives in the HEAP, not in this package's bss (SPEC.md 27.3)
; A note is data, and data of a size only the user knows; a package's region
; is image + bss capped at APP_MAX_SIZE, so anything sized by the user belongs
; in a claim (SPEC.md 50.3). The claim starts at NP_KB0, grows a kilobyte at a
; time as the note fills it, is sized to the file on a load, and shrinks back
; on File > New. [np_dseg]:0000 is the text and [np_cap] its capacity.
NP_KB0       equ 1              ; the document claim at launch, KB
NP_GROWKB    equ 1              ; ...and the quantum it grows by
NP_MAXKB     equ 8              ; ...and its ceiling. NOT a memory limit - it
                                ; is what the WINDOW can show. Note Pad does
                                ; not scroll: np_walk lays out from [np_ty],
                                ; which np_bounds always sets to the content's
                                ; top, so text past the last visible row can
                                ; be typed and can never be read back. The
                                ; most a window can display is NP_MAXROWS
                                ; rows of NP_MAXCOL cells - 60 x 91, about
                                ; 5,400 characters on a fullscreen VGA frame,
                                ; and fewer on any smaller one - so 8,192 is
                                ; already past the point of diminishing
                                ; returns, and every byte past it also costs
                                ; a np_walk pass that visits it on EVERY
                                ; paint. Lifting this wants scrolling first
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
NP_BSS_TOTAL equ 445 ; see the bss layout after OS88_IMAGE_END
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
    dec ax                          ; ...and the last drawable column. This was
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
    jmp short .out
.norows:
    mov word [np_vrows], 0
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
    mov ax, [np_len]
    mov [np_hiti], ax               ; a click past the end lands at the end
    mov word [np_wanti], 0xFFFF     ; ...but a row that does not exist has no
    mov byte [np_hitset], 0         ; answer, and the caller keeps its caret
    mov byte [np_wantset], 0

    mov di, [np_tx]                 ; DI = pen x
    mov bp, [np_ty]                 ; BP = pen y
    mov word [np_i], 0
    mov word [np_row], 0            ; ...and row 0 of the signature array,
    mov word [np_rowh], 0           ; with nothing folded into it yet
    mov bx, [np_len]                ; BX = characters remaining
    xor si, si                      ; ES:SI = the document (SPEC.md 27.3), and
    mov es, [np_dseg]               ; ES survives every callee below: np_rstart
                                    ; and np_rflush push it around their own
    call np_rstart                  ; BP is row 0's y; the buffer starts blank

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
    call np_rstart                  ; being accumulated belongs to the old one
.fits:                              ; being accumulated belongs to the old one
    call np_ask                     ; the queries, at the settled pen
    cmp byte [np_draw], 0
    je .body
    call np_carets                  ; ...and the caret, if this is its index
.body:
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
    jmp .loop                       ; the pixels of the row it ends are the
                                    ; same with it and without it
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
    cmp byte [np_sigup], 0
    je .fin
.pad:
    call np_nextrow                 ; flush the row the walk ended on, and then
    mov ax, [np_row]                ; every visible row after it: a note that
    cmp ax, [np_vrows]              ; SHRANK leaves rows behind that are no
    jb .pad                         ; longer reached, and their old signature
.fin:                               ; is exactly what says they must be erased

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
    add ax, 7
    cmp ax, [np_bot]
    ja .out                         ; the row does not fit: the pen still
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
    pop ax
    ret

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
; np_measure - run the walk without drawing
; in:  SI = window ptr; the query fields already set
; out: as np_walk; preserves all registers
;
; A QUERY pass: it answers where the caret is, or what a click landed on, and
; it must not touch the signatures - the caller has not drawn anything.
; -----------------------------------------------------------------------------
np_measure:
    call np_bounds
    mov byte [np_draw], 0
    mov byte [np_sigup], 0
    mov byte [np_clip], 0
    call np_walk
    ret

; -----------------------------------------------------------------------------
; np_paint - W_PAINT: draw the buffer and the caret
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_paint:
    push ax
    call np_bounds
    mov word [np_hity], 0xFFFF      ; no queries: this pass is here to draw
    mov word [np_wanty], 0xFFFF
    mov byte [np_draw], 1
    mov byte [np_sigup], 1          ; the content was white-filled on the way
    mov byte [np_clip], 0           ; here, so this pass draws every row AND is
    call np_walk                    ; the baseline every later incremental
    call np_sigmark                 ; redraw is measured against (SPEC.md 27.2)
    pop ax
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
; The document claim (SPEC.md 27.3/50.3)
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
    add ax, 1023                    ; bounded by NP_MAXKB * 1024
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
    mov es, [np_dseg]               ; did (SPEC.md 27.3)
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
    mov word [np_hity], 0xFFFF      ; one query at a time
    call np_measure
    mov ax, [np_wanti]
    cmp ax, 0xFFFF
    je .out                         ; no such row: the caret stays put, which
    mov [np_cur], ax                ; is what Up on the first line should do
.out:
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
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    call np_measure                 ; [np_curx]/[np_cury]
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
    mov word [np_hity], 0xFFFF
    mov word [np_wanty], 0xFFFF
    call np_measure                 ; [np_cury] = the row we are on
    mov ax, [np_cury]
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
    mov [np_hitx], cx
    mov [np_hity], dx
    mov word [np_wanty], 0xFFFF
    call np_measure
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
    mov ax, [np_len]
    cmp [np_cur], ax
    jbe .out
    mov [np_cur], ax
.out:
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

.append:
    call np_ins                     ; at the caret, which follows it
    jmp short .edited

.bksp:
    cmp word [np_cur], 0
    je .out                         ; nothing to the left of the caret
    dec word [np_cur]
    call np_del
    jmp short .edited

.del:
    mov ax, [np_cur]                ; forward delete: the caret stays put
    cmp ax, [np_len]
    jae .out
    call np_del
    jmp short .edited

.left:
    cmp word [np_cur], 0
    je .out
    dec word [np_cur]
    jmp short .edited
.right:
    mov ax, [np_cur]
    cmp ax, [np_len]
    jae .out
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

.redraw:
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
    call np_sigsame
    jc .full

    mov word [np_hity], 0xFFFF      ; pass 1: no queries, no drawing - just
    mov word [np_wanty], 0xFFFF     ; which rows stopped matching
    mov word [np_dr0], 0xFFFF
    mov word [np_dr1], 0
    mov byte [np_draw], 0
    mov byte [np_sigup], 1
    mov byte [np_clip], 0
    call np_walk
    mov ax, [np_dr0]
    cmp ax, 0xFFFF
    je .out                         ; not one pixel of the text moved

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

    mov byte [np_draw], 1           ; pass 2: draw, and only inside it
    mov byte [np_sigup], 0
    mov byte [np_clip], 1
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
    call np_toast                   ; a no-op with no message, which is every
    jmp short .out                  ; keystroke

.full:
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
                                       ; below, a heap claim (SPEC.md 27.3)
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

; --- the document, and the heap it lives in (SPEC.md 27.3/50.3) ----------------
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
                                       ; total 445 = NP_BSS_TOTAL
