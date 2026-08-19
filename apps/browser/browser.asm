; =============================================================================
; os8088 - apps/browser/browser.asm
;
; BROWSER, the text-and-table HTTP viewer (docs/BROWSER-PLAN.md). This is
; step 1 of that document's 10: the RENDERER, with no network in the machine
; at all - it opens a .HTM off a floppy through the Standard File dialog and
; draws it. tools/htmsim.py is the reference implementation and the cost
; model; tests/htm/ is what both are checked against.
;
; THE ONE NUMBER THIS FILE EXISTS TO ANSWER: a full window of text costs
; 1.24 s on CGA and 2.50 s on Hercules (PERFORMANCE.md Part 2). So the whole
; design is "never draw a page twice, and never draw a row that did not
; change", and the three mechanisms are the ones this tree already measured
; somewhere else - the blit tier (SPEC.md 27.7.2, 22.11), bounded work, and
; input coalescing through OSAPI_EVQ_PENDING.
;
; THE DOCUMENT IS ONE LINEAR BYTE STREAM, and that falls out of the fold
; rather than being designed: every character is folded to ASCII 0x20..0x7E
; (SPEC.md 6.1 - the cell font has 95 glyphs and font_char indexes past the
; end above that), so ANY BYTE BELOW 0x20 IS A MARKER and needs no escape.
; A paragraph break, a rule, a heading and a list bullet are one byte each,
; text is itself, and the whole of layout is a byte scan with no pointer
; chasing and no records. It also makes the stream SELF-CONTAINED, which is
; why the source claim is freed the moment the parse finishes: nothing above
; the parse ever looks at the file again.
;
; A LINE IS ONE OPAQUE font_run PADDED TO THE BAND (SPEC.md 6.1, and
; BROWSER-PLAN 2.4). Not because it is quicker - it is - but because the
; pair it replaces leaves the row BLANK between the fill and the last glyph,
; which on a 4.77MHz machine is tens of milliseconds of visible white per
; line. The padding IS the erase, so there is no gfx_fill in the paint path
; at all, and SPEC.md 11.3's granularity trap has no pair left to break.
;
; WHAT IS NOT HERE YET, deliberately: tables lay out as linearised cells
; rather than columns (BROWSER-PLAN 3.2's own degradation path, so the
; output is honest rather than wrong), links are drawn but not followable,
; and there are no forms. Each is a named step in that document.
; =============================================================================

%include "os88api.inc"
%include "netpkg.inc"          ; the SOCKET ABI (SPEC.md 62.11) - the
                                ; same file drivers/net/net.asm
                                ; includes, so the two ends of it
                                ; cannot drift (SPEC.md 20.11)

    OS88_HEADER 'BROWSER', br_entry, 3  ; bit0 = icon, bit1 = association

    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay): a page
    dw 0x3FF0                       ; with a folded corner
    dw 0x3FF8
    dw 0x3FFC
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x3FFE
    dw 0x0000
    dw 0x0000                       ; 16 image rows (black ink)
    dw 0x3FF0                       ; outline, corner, and four text rules
    dw 0x2018
    dw 0x2014
    dw 0x2002
    dw 0x2002
    dw 0x2FA2
    dw 0x2002
    dw 0x2FFA
    dw 0x2002
    dw 0x2FFA
    dw 0x2002
    dw 0x2FA2
    dw 0x2002
    dw 0x3FFE
    dw 0x0000
    OS88_ICON16_END

; --- the extension we claim (SPEC.md 54.6) -----------------------------------
; Just HTM: a FAT12 name is 8.3, so `.html` arrives as `.HTM` anyway, and
; claiming an extension somebody else has is a repoint rather than an error -
; so claiming TXT here would quietly take documents off Note Pad.
    OS88_ASSOC16
        db 1
        OS88_ASSOC_EXT 'HTM'
    OS88_ASSOC16_END

; --- limits (BROWSER-PLAN 2.2: bounded at the parse, not at the buffer) -------
; Every offset into the source, the document and the line table is a WORD, so
; 32KB is an ARITHMETIC limit and not a memory one - the same reason Note Pad
; caps at 16 (SPEC.md 27). A page over it is refused with its size in the
; message (SPEC.md 47 rule 3), never truncated silently.
BR_MAXKB     equ 32                 ; source ceiling
BR_DOCMUL    equ 2                  ; doc claim = source KB * this + slack; a
                                    ; fold can EXPAND (one 0xBD byte becomes
                                    ; one '?', but an entity like &#9749; is
                                    ; eight bytes becoming one) - shrinking is
                                    ; the common case and the multiplier is
                                    ; the safety, with the emit bounded anyway
BR_DOCSLK    equ 4                  ; ...plus this many KB
BR_DOCMAX    equ 63                 ; ...and the document claim is capped here:
                                    ; [br_doclen] and every LN_OFS is a word,
                                    ; so 64 KB is unreachable by construction
                                    ; and 68 << 10 is 4,096 rather than 69,632
BR_LINEKB    equ 8                  ; line table: 6 bytes a line, so 1,365
                                    ; display lines - about seven CGA pages of
                                    ; solid prose, and the layout stops there

; --- the document stream ------------------------------------------------------
; Markers are BELOW 0x20; text is 0x20..0x7E and is itself.
D_END        equ 0                  ; end of document
D_PARA       equ 1                  ; paragraph break (a blank line)
D_BR         equ 2                  ; line break (no blank line)
D_HR         equ 3                  ; a rule across the band
D_LI         equ 4                  ; list item - the bullet is drawn, not stored
D_HEAD       equ 5                  ; + 1 byte level 1..6
D_CEN1       equ 6                  ; centre on
D_CEN0       equ 7                  ; centre off
D_PRE1       equ 8                  ; preformatted on
D_PRE0       equ 9                  ; preformatted off
D_TAB1       equ 10                 ; table start / end, and one marker per
D_TAB0       equ 11                 ; row and cell inside it (BROWSER-PLAN 3.2)
D_ROW        equ 12
D_CELL       equ 13
D_LNK1       equ 14                 ; a link opens, and the TWO BYTES after it
                                    ; are its index: 0x10|(i&15) then
                                    ; 0x10|(i>>4), so both land in 16..31.
                                    ; **THAT RANGE IS THE WHOLE TRICK.** Every
                                    ; walker in this file already skips a byte
                                    ; below 0x20 as "a marker, not a cell", so
                                    ; the payload costs not one of them a line
                                    ; of change; and no real marker is above
                                    ; 15, so a BACKWARD scan can tell a payload
                                    ; byte from a marker and answer "which link
                                    ; is this offset inside" without a table
D_LNK0       equ 15                 ; ...and closes

D_NBSP       equ 0x7F               ; A NO-BREAK SPACE, and it is 0x7F rather
                                    ; than a marker because it is TEXT: it
                                    ; occupies a cell, counts toward the wrap
                                    ; width and is copied like any other byte -
                                    ; it simply is not the ' ' that br_layout
                                    ; breaks at. br_build renders it as a
                                    ; space. 0x7F is past the last ROM glyph
                                    ; (0x20..0x7E), so it can never reach
                                    ; font_char even if a walk here is wrong

BR_LNKMAX    equ 200                ; links a page may carry. Past it the anchor
                                    ; is ordinary text - a page does not fail,
                                    ; it just stops being clickable at the tail
BR_LINKKB    equ 6                  ; ...and the bytes their hrefs share
BR_LNKSCAN   equ 2048               ; how far back a click looks for its D_LNK1.
                                    ; A bound rather than a document walk: it is
                                    ; only reached on a page that HAS links, and
                                    ; an anchor whose text runs past this reads
                                    ; as not-a-link rather than as a wrong one

BR_TCOLS     equ 8                  ; columns a table may have. Past it the
                                    ; table degrades to a block rather than
                                    ; being refused - 3.2's rule
BR_TFLRMAX   equ 14
BR_FMAX      equ 4                  ; text inputs on a page. A search box is
                                    ; one; past this they are ignored
BR_FLDW      equ 20                 ; a field's default width in cells
BR_FLDWMAX   equ 40
BR_NAMEMAX   equ 12                 ; a field's name, for the query string
BR_ACTMAX    equ 64                 ; the form's action
BR_HIDMAX    equ 96                 ; hidden pairs, already encoded
BR_URLMAX    equ 160                ; the composed query
BR_FLDSZ     equ 18                 ; one field record:
FLD_OFS      equ 0                  ;   word: its first EDITABLE document byte
FLD_W        equ 2                  ;   byte: cells
FLD_LEN      equ 3                  ;   byte: characters typed so far
FLD_NAME     equ 4                  ;   BR_NAMEMAX+2: its name
KSC_TAB      equ 0x0F                 ; a column's floor is its longest WORD,
                                    ; capped here: one very long word does not
                                    ; get to reserve the whole band

; --- the line table (BROWSER-PLAN 2.3) ----------------------------------------
LN_OFS       equ 0                  ; word: doc offset this line starts at
LN_END       equ 2                  ; word: one past its last byte
LN_X         equ 4                  ; byte: starting cell column (indent/centre)
LN_FL        equ 5                  ; byte: LNF_*
LN_SIZE      equ 6

LNF_RULE     equ 1                  ; draw a rule, not text
LNF_PRE      equ 2                  ; preformatted: do not wrap (already split)
LNF_LNK      equ 4                  ; this line BEGINS inside a link, so its
                                    ; first cells are underlined even though no
                                    ; D_LNK1 appears in its own span. Set by
                                    ; br_emitline from the layout's running
                                    ; state, which is O(1) - the alternative is
                                    ; a backward scan per line per paint

; --- geometry -----------------------------------------------------------------
BR_SBW       equ 14                 ; scroll bar width, the Disk window's
BR_GROWH     equ 13                 ; the GROW BOX is a 13x13 square at the
                                    ; window's bottom-right corner
                                    ; (wm_grow_rect, SPEC.md 11.1.1) and it
                                    ; lands squarely on the bar's down-arrow
                                    ; cell. The bar stops above it, which is
                                    ; what the Disk window's does for its
                                    ; status line. Reserved unconditionally:
                                    ; the box is only drawn on the FRONTMOST
                                    ; sizable window, and a bar that changed
                                    ; length as the window gained and lost
                                    ; focus would be worse than a gap
BR_SBSTEP    equ 4                  ; LINES ONE ARROW CLICK MOVES. The Disk
                                    ; window steps ONE because its rows are
                                    ; 16px file entries and one is a real unit
                                    ; of what the user is looking at; a line of
                                    ; prose is 8px and is not, so this is Note
                                    ; Pad's NP_SB_STEP (SPEC.md 27.7.2) and its
                                    ; reasoning rather than the Disk window's.
                                    ; It stays under [br_rows], so the blit
                                    ; path in br_flush still takes it
BR_CELL      equ 8                  ; the cell font is 8x8
BR_MAXCOL    equ 96                 ; widest band we will ever letter: 720/8 is
                                    ; 90 on Hercules, and lbuf is sized to this

; =============================================================================
; br_entry - package entry (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; Nothing is claimed here: a browser with no page has nothing to hold, and
; every claim below is taken on the UI task in the dialog's completion proc,
; which is where BROWSER-PLAN 3.4's rule puts them (a worker may not call
; OSAPI_MEM_*, SPEC.md 20.6 rule 7 - and this app has no worker yet, but the
; discipline is what makes adding one later a non-event).
; =============================================================================
br_entry:
    push si
    call br_size                    ; the template's HEIGHT is the machine's
    mov si, br_tpl
    call OSAPI_WM_CREATE
    jc .out
    call br_keeph                   ; ...and on a CGA it is allowed over the dock
    mov si, br_menus
    call OSAPI_MENU_SET
    mov [br_win], bx
    mov word [br_loc + LN_BUF], br_ubuf     ; **THE BLOCK'S BUFFER WORDS, and
    mov word [br_loc + LN_MAX], BR_UBUF     ; they are not optional**: bss
                                            ; arrives ZEROED (SPEC.md 21 step
                                            ; 5), so an unset LN_BUF points at
                                            ; offset 0 - the package's own
                                            ; header - and the first keystroke
                                            ; writes into it
    push si                         ; **THE BAR OPENS HOLDING `http://`,
    mov si, br_loc                  ; FOCUSED, WITH THE CARET AFTER IT.** The
    mov di, br_s_scheme             ; scheme is not optional (br_split refuses
    call os88line_set               ; anything else - SPEC.md 71.2) and nothing
    mov byte [si+LN_FOCUS], 1       ; on screen used to say so, so the first
    pop si                          ; thing a new user typed was a bare host
                                    ; and the first thing they got was `Bad
                                    ; URL`. os88line_set lands the caret at the
                                    ; END, which is the whole of requirement 3.
                                    ; It is before br_arg deliberately: a
                                    ; browser launched ON a document has a real
                                    ; name to show and may overwrite this
    mov si, br_about
    call OSAPI_ABOUT_SET            ; 'About Browser' under our name in the bar
                                    ; (SPEC.md 12.2). It preserves the flags,
                                    ; which matters here: wm_create's CF above
                                    ; is the loader's answer and still has to
                                    ; ride out of this proc
    mov al, 1
    call OSAPI_WM_SIZABLE           ; br_measure re-reads the content box on
                                    ; every paint and relayouts on a WIDTH
                                    ; change (BROWSER-PLAN 3.5), so the window
                                    ; can be resized without anything else
    call br_arg                     ; launched ON a document? then open it
                                    ; WF_SNAP is the DEFAULT now (SPEC.md
                                    ; 11.94), so the content origin is already
                                    ; 8-aligned and every font_run in here gets
                                    ; the single-store path for nothing
    clc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; br_size - the default window's height, from the screen this machine has
; in:  entry-proc context, before OSAPI_WM_CREATE
; out: br_tpl's y and h rewritten; all registers preserved
;
; A browser is short of ROWS and nothing else, so the template's 150 was a
; number for the smallest adapter that everything larger then inherited. It is
; derived now:
;
;   VGA and Hercules   90% of the desktop band, centred in it - leaving a
;                      margin so the window still reads as a window and can
;                      be grabbed by an edge
;   CGA                the WHOLE band AND the dock's strip, because 640x200
;                      gives the band 155 rows and this app spends 33 of them
;                      on chrome. The dock is still reachable: a window over
;                      it is wm_dock_under's ordinary case (SPEC.md 11.90),
;                      and the user can move or shrink this one like any other
;
; Written into the TEMPLATE rather than set after create, because wm_create
; runs wm_fit on the size it is given and asking afterwards would fit twice.
; -----------------------------------------------------------------------------
br_size:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_VIDEO                ; AX = w, BX = h, CX = the dock's first row
    cmp dl, VID_CGA
    je .cga
    sub cx, MBAR_H                  ; CX = the desktop band
    mov ax, cx
    mov bx, 9
    mul bx                          ; **MUL WRITES DX** (SPEC.md 1) - nothing is
    mov bx, 10                      ; banked there across this
    xor dx, dx
    div bx                          ; AX = 90% of the band
    mov [br_tpl+6], ax
    sub cx, ax                      ; ...centred in what is left
    shr cx, 1
    add cx, MBAR_H
    mov [br_tpl+2], cx
    jmp short .out
.cga:
    sub bx, MBAR_H                  ; the whole screen below the bar, less the
    dec bx                          ; row the drop shadow lives on (wm_fit's
    mov [br_tpl+6], bx              ; own reasoning, and its WF_KEEPH clamp)
    mov word [br_tpl+2], MBAR_H
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_keeph - a CGA window may hang over the dock (SPEC.md 11.93) ----------
; in:  BX = the window; out: nothing, FLAGS PRESERVED - the CF wm_create left
;      is the loader's answer and still has to ride out of br_entry
br_keeph:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx                      ; **BX IS THE WINDOW AND OSAPI_VIDEO
    call OSAPI_VIDEO                ; ANSWERS THE SCREEN HEIGHT IN IT**, so
    mov bx, si                      ; asking the adapter first and passing BX
                                    ; afterwards hands WM_KEEPH the number 200
                                    ; as a window pointer. It cost one run: the
                                    ; window came back at the band's 155 rows,
                                    ; which is exactly what a KEEPH that never
                                    ; happened looks like
    cmp dl, VID_CGA
    jne .out
    mov al, 1
    call OSAPI_WM_KEEPH             ; without it wm_fit SHORTENS us back to the
                                    ; band and the whole point of the CGA case
.out:                               ; is lost - silently, since a shortened
    pop si                          ; window looks like a window
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; br_arg - open the document we were launched on (SPEC.md 54.5)
; in:  entry-proc context: UI task, no lock, window created but not shown
; out: nothing; a failure just leaves an empty window
;
; Double-clicking a .HTM is the ordinary way in, and it is also what makes a
; scripted session simple: no dialog to drive. The name lives in the KERNEL
; segment and is valid until the next call, so it is copied through ES before
; anything else happens - and ES is KERNEL_SEG here by contract (SPEC.md 20.2).
;
; The SIZE is not known on this path, where the dialog hands one over
; (SPEC.md 38.6), so the source claim is the ceiling rather than the file.
; It is freed the moment the parse ends, so the over-claim is transient -
; but it is why a 128KB machine may open a page from a Disk window and refuse
; the same page from File > Open, which is worth knowing before it is called a
; bug.
; -----------------------------------------------------------------------------
br_arg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the ordinary case
    jc .out
    push dx                         ; the folder it lives in...
    push bx                         ; ...and BL, its volume
    mov di, br_name                 ; DS:DI is ours; the NAME is the KERNEL's,
    mov ax, KERNEL_SEG              ; and ES is NOT reliably KERNEL_SEG here -
    mov es, ax                      ; the X stubs hand a callee our DS in ES,
    xor cx, cx                      ; so name the segment rather than assume it
.copy:
    cmp cx, 13
    jae .done
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    inc cx
    or al, al
    jnz .copy
.done:
    pop bx
    pop dx
    call OSAPI_FILE_GOTO            ; stand in the document's folder
    jc .out
    mov word [br_srclen], 0         ; unknown: br_load claims the ceiling
    call br_load
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
; PAINT
; =============================================================================

; -----------------------------------------------------------------------------
; br_paint - W_PAINT: the whole content
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; Re-measures the content box on every paint, which is the Calculator's rule
; (SPEC.md 65): the box moves without anybody asking - the Control Panel's
; Display page switching adapters, a window dragged across an extended
; desktop's seam - and a layout derived from a constant is wrong there. A
; WIDTH change is the only thing that invalidates the line table
; (BROWSER-PLAN 2), and br_measure is where that is noticed.
; -----------------------------------------------------------------------------
br_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [br_win], si
    call br_hire
    call br_measure                 ; CF=1 if the width moved (relayout done)
    call br_toolbar                 ; ...the strip draws the state itself
    push si
    mov si, br_loc
    call os88line_draw
    pop si
    call br_paint_band
    call br_sbar
    mov bx, si
    call OSAPI_WM_GROW              ; the grow box is ours to put back
    cmp byte [br_abon], 0           ; ...and the credit card LAST, because it
    je .noab                        ; is on top of all of it. A repaint the
    call br_abdraw                  ; KERNEL ordered - a raise, a drag, another
.noab:                              ; window closing - would otherwise erase a
                                    ; card that is still up as far as the next
                                    ; click is concerned, and that click would
                                    ; then be swallowed by nothing visible
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_measure - read the live content box; relayout if the width changed
; in:  SI = window ptr
; out: CF=1 if a relayout happened; [br_cx/cy/cw/ch/cols/rows] updated
; -----------------------------------------------------------------------------
br_measure:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [br_cx], ax
    mov [br_cy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h
    mov [br_cw], cx
    mov [br_ch], dx
    call br_locrect                 ; THE BAR OWNS THE TOP OF THE CONTENT, and
                                    ; it takes it by moving br_cy/br_ch - so
                                    ; the band, the scroll bar, the hit test
                                    ; and the blit all follow with no second
                                    ; opinion about where the text starts
    call br_sbrect                  ; ...and the scroll bar's rect with them
    mov ax, [br_ch]
    mov cl, 3
    shr ax, cl
    cmp ax, 1
    jae .rok
    mov ax, 1
.rok:
    mov [br_rows], ax               ; whole 8px rows that fit
    mov ax, [br_cw]
    sub ax, BR_SBW                  ; the bar owns the right edge
    jns .wok
    xor ax, ax
.wok:
    shr ax, cl                      ; ...and the band is whole cells
    cmp ax, BR_MAXCOL
    jbe .cok
    mov ax, BR_MAXCOL
.cok:
    cmp ax, 8
    jae .cok2
    mov ax, 8
.cok2:
    cmp ax, [br_cols]
    je .same
    mov [br_cols], ax
    call br_layout                  ; the ONE thing a width change invalidates
    xor ax, ax
    call br_advance                 ; ...and the view may now be past the end
    stc
    jmp .out
.same:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_locrect - the location bar's rect, and the band pushed down under it
; in:  [br_cx]/[br_cy]/[br_cw]/[br_ch] = the LIVE content box
; out: the line block's four words set; br_cy/br_ch are the BAND's now
;
; Derived on every measure rather than stored, because a window can be
; resized, moved across an extended desktop's seam or find itself on an
; adapter of a different size (SPEC.md 39.7/11.98) - and a control drawn from
; a remembered rect is one the hit-test cannot find.
; -----------------------------------------------------------------------------
br_locrect:
    push ax
    push bx
    push cx
    push dx
                                    ; --- the toolbar row, first ---
    mov ax, [br_cy]
    add ax, BR_LPAD
    mov [br_tby], ax                ; its top
    mov bx, [br_cx]
    add bx, BR_LPAD                 ; BX = the next button's left edge
    mov cx, BR_BTNW
    mov [br_r1], bx                 ; ...stored BEFORE the call, which is what
    call br_brect                   ; advances it
    mov [br_r1+4], dx
    mov [br_r2], bx
    call br_brect
    mov [br_r2+4], dx
    mov cx, BR_BTNW2
    mov [br_r3], bx
    call br_brect
    mov [br_r3+4], dx
    mov ax, [br_tby]                ; ...all three share the row's y span
    mov [br_r1+2], ax
    mov [br_r2+2], ax
    mov [br_r3+2], ax
    add ax, BR_TBH - 1
    mov [br_r1+6], ax
    mov [br_r2+6], ax
    mov [br_r3+6], ax
    call br_srect                   ; ...and the state, right-aligned
                                    ; --- then the bar under it ---
    mov ax, [br_cx]
    add ax, BR_LPAD
    mov [br_loc + LN_X1], ax
    mov ax, [br_cx]
    add ax, [br_cw]
    sub ax, BR_LPAD + 1
    mov [br_loc + LN_X2], ax
    mov ax, [br_tby]
    add ax, BR_TBH + BR_TBG
    mov [br_loc + LN_Y1], ax
    add ax, BR_LBAR - 1
    mov [br_loc + LN_Y2], ax
    mov ax, BR_LTOT                 ; the strip, the bar and their gaps
    add [br_cy], ax
    sub [br_ch], ax
    jns .out
    mov word [br_ch], 0             ; a window too short for the chrome shows
.out:                               ; no band at all rather than a negative one
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_brect - one button's right edge, and the next one's left -------------
; in:  BX = this button's left edge, CX = its width
; out: DX = its right edge (inclusive); BX = the NEXT button's left edge
br_brect:
    mov dx, bx
    add dx, cx
    dec dx                          ; DX = right edge, inclusive
    add bx, cx
    add bx, BR_BTNG                 ; BX = where the next one starts
    ret

; -----------------------------------------------------------------------------
; br_srect - the state's pen and how many cells it may use
; out: [br_spen] (8-ALIGNED) and [br_swid]
;
; Right-aligned, and the pen is floored to a multiple of 8 for SPEC.md 6.1's
; single-store path - the same reason os88line's is, and the same flicker if
; it is not: this field is rewritten on every state change.
;
; The width SHRINKS rather than the strip overflowing. A narrow window is a
; real case (wm_fit clamps the template on a CGA and the user can grow it
; smaller still), and a state that ran under the Reload button would be drawn
; over it - font_run clips to the SCREEN, not to anything smaller.
; -----------------------------------------------------------------------------
br_srect:
    push ax
    push bx
    push cx
    push dx
    mov dx, [br_r3+4]               ; DX = the first x the state may use
    add dx, BR_BTNG + 1
    mov bx, [br_cx]
    add bx, [br_cw]
    sub bx, BR_LPAD                 ; BX = one past its right edge
    mov ax, bx
    sub ax, dx
    js .none                        ; the buttons already fill the strip
    mov cl, 3
    shr ax, cl                      ; AX = cells there is room for at all
    jz .none
    cmp ax, BR_SCELLS
    jbe .have
    mov ax, BR_SCELLS               ; ...never more than the field needs
.have:
    mov [br_swid], ax
    shl ax, cl
    sub bx, ax                      ; BX = the right-aligned pen...
    sub bx, [br_cx]                 ; ...floored to a multiple of 8 RELATIVE to
    and bx, 0FFF8h                  ; the content origin, which WF_SNAP has
    add bx, [br_cx]                 ; already put on one (SPEC.md 6.1: the
    mov [br_spen], bx               ; single-store path needs the pen aligned,
    jmp short .out                  ; and this field is rewritten on every
.none:                              ; state change)
    mov word [br_swid], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_paint_band - letter every visible row
; in:  nothing (br_* geometry current); lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
br_paint_band:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx                      ; BX = row within the band
.row:
    cmp bx, [br_rows]
    jae .done
    mov ax, [br_top]
    add ax, bx
    call br_paint_line              ; AX = line index, BX = row
    inc bx
    jmp .row
.done:
    mov ax, [br_top]                ; the glass now shows this
    mov [br_ptop], ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_paint_line - one display line, as ONE opaque font_run padded to the band
; in:  AX = line index (may be past the end: the band is blanked), BX = row
; out: nothing; clobbers nothing the caller keeps
;
; This is BROWSER-PLAN 2.4 and it is the whole paint path. There is no
; gfx_fill anywhere in it: a space paints background on font_run's fast path,
; so the PADDING is the erase and the row is never momentarily blank.
; -----------------------------------------------------------------------------
br_paint_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bx                         ; keep the row
    call br_build                   ; br_lbuf = the padded line, AL = flags
    pop bx
    mov cx, bx
    mov ax, BR_CELL
    mul cx                          ; DX:AX = row * 8  (mul writes DX - see
                                    ; SPEC.md 65's bug; nothing is held there)
    add ax, [br_cy]
    mov dx, ax                      ; DX = pen y
    mov cx, [br_cx]                 ; CX = pen x, 8-aligned by WF_SNAP
    mov si, br_lbuf
    push cx
    push dx
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    call br_underline               ; ...and the link runs under it, from the
                                    ; marks br_build left. CX/DX are still the
                                    ; pen, which is what it wants
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_underline - a 1px rule under each run of link cells on the row just drawn
; in:  CX = the row's pen x, DX = its pen y; br_lmark current; lock held
; out: nothing; preserves all registers
;
; ONE FILL PER RUN, not per cell: a whole anchor is one call however many words
; it spans, and a line with no link on it costs the scan and nothing else.
; PERFORMANCE.md prices a drawing call at ~756us, so a link-dense line is the
; case to keep honest - FrogFind's results are ten links in ten lines, which is
; ten calls on top of ten font_runs, about 7% of the band.
;
; The rule sits on the cell's LAST pixel row, which is blank in every glyph of
; the ROM font bar the descenders - and those touch it rather than being cut
; by it, exactly as an underline should.
; -----------------------------------------------------------------------------
br_underline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                         ; ...BP TOO: it is used as a VALUE below
                                    ; and never as a base, but a background
                                    ; task holds it for its whole life
                                    ; (SPEC.md 7.1.4.1) and this runs under
                                    ; the same lock they draw through
    cmp word [br_lnkn], 0
    je .out                         ; no links on this page: nothing to scan
    mov si, cx                      ; SI = the row's pen x, DI = its pen y,
    mov bp, dx                      ; banked - CX and DX are the fill's
    mov bx, br_lmark                ; **BX AND NOT SI**: only BX or BP may be
    xor di, di                      ; the base of a [base+index] on an 8086
                                    ; (SPEC.md 1), and BP addresses SS here
.scan:
    cmp di, [br_cols]
    jae .out
    cmp byte [bx+di], 0
    je .step
    mov cx, di                      ; CX = the run's first column
.run:
    inc di
    cmp di, [br_cols]
    jae .emit
    cmp byte [bx+di], 0
    jne .run
.emit:
    push bx
    push di
    mov ax, cx                      ; x1 = pen + first*8
    mov cl, 3
    shl ax, cl
    mov dx, di                      ; x2 = pen + last*8 + 7, and DI is one
    shl dx, cl                      ; PAST the last, so that is di*8 - 1
    dec dx
    add ax, si
    add dx, si
    mov cx, dx                      ; CX = x2
    mov bx, bp
    add bx, BR_CELL - 1             ; BX = y1, the cell's last pixel row: blank
    mov dx, bx                      ; in every ROM glyph but the descenders,
                                    ; which touch it rather than being cut
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop di
    pop bx
    jmp .scan
.step:
    inc di
    jmp .scan
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
; br_build - render one line index into br_lbuf, space-padded to the band
; in:  AX = line index
; out: br_lbuf holds [cols] chars + NUL
; -----------------------------------------------------------------------------
br_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ax                         ; THE LINE INDEX. `mov al, ' '` below is a
                                    ; write to the bottom half of it, so
                                    ; without this every row draws line
                                    ; (AH<<8)|0x20 - which on a 16-row band is
                                    ; line 32, sixteen times
    mov di, br_lbuf
    mov cx, [br_cols]
    push cx
    mov al, ' '
    push ds                         ; stos writes through ES, and ES is
    pop es                          ; KERNEL_SEG on entry to a callback -
    cld                             ; SPEC.md 56's bug, in the place a
    rep stosb                       ; buffer fill will always find it
    pop cx
    push cx                         ; ...and the COLUMN MAP beside it, all
    mov di, br_lmap                 ; 0FFFFh = "no document byte in this cell"
    mov ax, 0FFFFh
    rep stosw
    pop cx
    push cx                         ; ...and the LINK MARKS, all clear
    mov di, br_lmark
    xor al, al
    rep stosb
    pop cx
    mov si, br_lbuf
    add si, cx
    mov byte [si], 0                ; NUL at the band's end
    pop ax                          ; ...the line index back

    cmp ax, [br_nlines]
    jae .out                        ; past the end: a blank band row
    mov bx, LN_SIZE
    mul bx                          ; DX:AX = index * 6
    mov si, ax
    mov es, [br_lseg]
    mov di, [es:si+LN_OFS]
    mov cx, [es:si+LN_END]
    mov al, [es:si+LN_X]
    mov ah, [es:si+LN_FL]
    mov bh, ah                      ; BH = flags
    and ah, LNF_LNK                 ; ...and the LINK STATE this line inherits
    mov [br_lnkcell], ah            ; (LNF_LNK, set by br_emitline)
    xor ah, ah
    xor ah, ah
    mov dx, ax                      ; the start column. NOT in BP: that
                                    ; addresses SS here (SPEC.md 1) and the
                                    ; kernel's frame is under it
    test bh, LNF_RULE
    jz .text
                                    ; a rule: the band, filled with '-'
    mov di, br_lbuf
    mov cx, [br_cols]
    mov al, '-'
    push ds
    pop es
    cld
    rep stosb
    jmp .out
.text:
    mov es, [br_docseg]
    mov si, di                      ; SI = doc offset
    mov di, br_lbuf
    add di, dx                      ; ...plus the line's start column
    mov bx, [br_cols]
.copy:
    cmp si, cx
    jae .out
    mov al, [es:si]
    inc si
    cmp al, 0x20
    jae .cell
    cmp al, D_LNK1                  ; the two inline markers are the only ones
    jne .nl1                        ; this walk has to UNDERSTAND rather than
    mov byte [br_lnkcell], 1        ; skip: they say which cells are a link
    add si, 2                       ; ...past the two payload bytes
    jmp .copy
.nl1:
    cmp al, D_LNK0
    jne .copy
    mov byte [br_lnkcell], 0
    jmp .copy                       ; every other marker: not a cell
.cell:
    cmp al, D_NBSP                  ; ...a no-break space is a SPACE on screen
    jne .cell2                      ; and only the layout knows the difference
    mov al, ' '
.cell2:
    mov dx, di
    sub dx, br_lbuf
    cmp dx, bx
    jae .out                        ; never write past the band
    mov [di], al
    push bx                         ; **AND WHERE THAT CELL CAME FROM**: the
    mov bx, dx                      ; hit test is this same walk asked
    shl bx, 1                       ; backwards, and running it twice from two
    mov [br_lmap+bx], si            ; descriptions is exactly the drift
    dec word [br_lmap+bx]           ; SPEC.md 22's fm_hit discipline forbids.
    shr bx, 1                       ; SI has already been advanced past the
    push ax                         ; byte, hence the dec
    mov al, [br_lnkcell]
    mov [br_lmark+bx], al           ; ...and whether it is inside a link, which
    pop ax                          ; is what the underline is drawn from
    pop bx
    inc di
    jmp .copy
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
; br_sbrect - the scroll bar's rect, derived (br_locrect's rule, one control on)
; in:  [br_cx]/[br_cy]/[br_cw]/[br_ch] = the live BAND box
; out: br_sbx/br_sbx2/br_sby/br_sby2
;
; **THIS USED TO LIVE INSIDE THE PAINTER**, which made the drawn bar and the
; clickable bar two descriptions of one thing - SPEC.md 22's fm_hit discipline
; broken in the one place this app had a control the kernel does not draw. They
; agreed for as long as nothing moved the window between a paint and a click,
; and the moment something did - dragging it by the title bar - the arrows and
; the track went on answering about where the window USED to be.
; -----------------------------------------------------------------------------
br_sbrect:
    push ax
    mov ax, [br_cx]
    add ax, [br_cw]
    sub ax, BR_SBW
    mov [br_sbx], ax                ; x1
    mov ax, [br_cx]
    add ax, [br_cw]
    dec ax
    mov [br_sbx2], ax               ; x2
    mov ax, [br_cy]
    mov [br_sby], ax                ; y1
    add ax, [br_ch]
    dec ax
    sub ax, BR_GROWH                ; ...stopping clear of the grow box
    mov [br_sby2], ax               ; y2
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_sbfill - the shared scroll-bar block (SPEC.md 13.10), from the live rect
; in:  the br_* geometry current
; out: br_sb filled; BX = the block. All other registers preserved.
;
; **THE BAR IS os88ui_sbar's NOW.** This app had the sixth private
; implementation of one widget - a frame, two arrow cells with their rules and
; glyphs, a dithered track and a proportional thumb, about 190 lines of it -
; and every one of the six drew a slightly different picture. What is left here
; is the seven words the shared element cannot know: where the bar is, and the
; three numbers that are this app's own idea of a view.
;
; It also gains what os88ui has and this did not: a thumb that TRANSLATES on a
; scroll instead of the bar being repainted (os88ui_sbmove, three drawing calls
; against sixteen), and a hit test that reports the THUMB as a part of its own
; so a click on it can do nothing rather than paging.
; -----------------------------------------------------------------------------
br_sbfill:
    push ax
    mov ax, [br_sbx]
    mov [br_sb+0], ax
    mov ax, [br_sby]
    mov [br_sb+2], ax
    mov ax, [br_sbx2]
    mov [br_sb+4], ax
    mov ax, [br_sby2]
    mov [br_sb+6], ax
    mov ax, [br_nlines]
    mov [br_sb+8], ax               ; total
    mov ax, [br_rows]
    mov [br_sb+10], ax              ; fit
    mov ax, [br_top]
    mov [br_sb+12], ax              ; pos
    mov bx, br_sb
    pop ax
    ret

br_sbar:
    push ax
    push bx
    call br_sbfill
    call os88ui_sbar
    pop bx
    pop ax
    ret

; =============================================================================
; SCROLLING (BROWSER-PLAN 4)
; =============================================================================

; -----------------------------------------------------------------------------
; br_scroll_by - move the view and draw it, unless another one is right behind
; in:  AX = signed line delta; lock held
;
; **THE STATE MOVES EVERY TIME AND THE PIXELS WAIT.** A scrolled line costs
; ~90 ms here, so a burst of scrolls - a held arrow, a spammed scroll-bar
; cell, a second click before the first has finished drawing - queues repaints
; that play out long after the clicking stopped, and every one of them draws a
; position the reader has already left. That is INPUT OVERRUN, which
; PERFORMANCE.md names as one of the three defects this container cannot show
; at all: it looks fine here and fails on the 5150.
;
; This was br_onkey's alone, and the CLICK paths - both scroll-bar cells, both
; halves of the track, and Go > Top/Bottom - went straight to br_flush. The
; coalescing lives at the one routine every scroll in the app already went
; through, so the two cannot drift and the keyboard's copy is gone.
; -----------------------------------------------------------------------------
br_scroll_by:
    call br_advance                 ; the view always moves...
    call br_coalesce
    jc .out                         ; ...and the last of a burst draws it
    call br_flush
.out:
    ret

; -----------------------------------------------------------------------------
; br_coalesce - CF=1 if this redraw may be skipped
; in:  nothing; out: CF, and [br_skips] maintained. Preserves all registers
;
; OSAPI_EVQ_PENDING's own contract warns that it answers about the QUEUE and
; not about whose event is next, so a handler that skips a redraw MUST still
; guarantee the redraw happens - the SDK's answer is to owe it to a worker.
; This app HAS one, and deliberately does not use it here: a second routine
; that draws is a second routine that can draw at the wrong moment, and the
; bound below costs nothing and cannot fail. After BR_MAXSKIP consecutive
; skips it draws anyway, so the worst case is a view four scrolls behind and
; there is no case at all where it lags for ever - which is what a stale
; window would be if the next queued event belonged to somebody else.
; -----------------------------------------------------------------------------
br_coalesce:
    push ax
    call OSAPI_EVQ_PENDING
    or ax, ax
    jz .draw
    mov ax, [br_skips]
    inc ax
    mov [br_skips], ax
    cmp ax, BR_MAXSKIP
    jb .skip
.draw:
    mov word [br_skips], 0
    pop ax
    clc
    ret
.skip:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; br_advance - move the view only. Draws NOTHING.
; in:  AX = signed line delta
; out: [br_top] clamped
; -----------------------------------------------------------------------------
br_advance:
    push ax
    push bx
    push cx
    mov bx, [br_top]
    add bx, ax
    jns .lo
    xor bx, bx
.lo:
    mov cx, [br_nlines]
    sub cx, [br_rows]
    jns .hi
    xor cx, cx
.hi:
    cmp bx, cx
    jbe .set
    mov bx, cx
.set:
    mov [br_top], bx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_flush - make the SCREEN agree with [br_top], by the cheapest tier
; in:  lock held
; out: [br_ptop] = [br_top]
;
; [br_ptop] is what the glass shows and [br_top] is where the view is; the
; two part while a burst of scroll events is being coalesced (br_onkey), and
; this is what reconciles them. Note Pad keeps exactly this pair for exactly
; this reason (SPEC.md 27.7.2's [np_ptop]).
;
; An end stop draws NOTHING - the old code repainted the window to show the
; same pixels, which is SPEC.md 22.11's own finding.
; -----------------------------------------------------------------------------
br_flush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [br_top]
    sub ax, [br_ptop]
    jz .out
    mov [br_dy], ax
    mov ax, [br_ptop]
    mov [br_sbold], ax              ; ...WHERE THE THUMB IS NOW, banked before
                                    ; br_ptop moves: os88ui_sbmove translates
                                    ; the thumb from the old pos to the new
                                    ; instead of the bar being repainted, and
                                    ; the old pos is the only thing it cannot
                                    ; derive from the block
    mov bx, [br_dy]                 ; **THE DELTA, OUT OF THE VARIABLE.** This
                                    ; was `mov bx, ax` back when AX still held
                                    ; it, and the two instructions above - the
                                    ; thumb's old position, added later - land
                                    ; between the last write to AX and this
                                    ; read of it. So the test below compared
                                    ; the OLD SCROLL POSITION against the band
                                    ; height: every scroll taken more than one
                                    ; windowful into a page went to .full, on
                                    ; every page, for the rest of the document.
                                    ; Measured on a cycle-accurate 5150/CGA,
                                    ; one Down key: 1 font_run and 1 gfx_scroll
                                    ; for the first 15 presses and 15 font_runs
                                    ; and no blit at all from the 16th
    or bx, bx
    jns .abs
    neg bx
.abs:
    cmp bx, [br_rows]
    jae .full                       ; a jump of a windowful or more: the band
    call br_scroll_blit
    mov ax, [br_top]
    mov [br_ptop], ax
    jmp .bar
.full:
    mov ax, [br_top]
    mov [br_ptop], ax
    call br_paint_band
.bar:
    call br_sbfill                  ; BX = the block, with the NEW pos in it
    mov ax, [br_sbold]
    call os88ui_sbmove              ; three drawing calls against the bar's
                                    ; sixteen: a scroll moves neither total nor
                                    ; fit, so the thumb's HEIGHT is unchanged
                                    ; and the frame, both rules, both arrows
                                    ; and the track it did not cover are all
                                    ; exactly where they were (SPEC.md 13.10)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_scroll_blit - move the pixels we already have, letter only what appeared
; in:  [br_dy] = the clamped signed line delta, |dy| < rows
;
; OSAPI_GFX_SCROLL answers CF=1 and moves nothing when it refuses - a clip
; region that does not wholly contain the rect, because a blit cannot be cut
; per pixel (SPEC.md 11.3) - so the fallback is a branch and not a hazard.
; The rows it vacated are OURS to repaint: that is the contract, and
; forgetting it leaves the previous content's rows on screen.
; -----------------------------------------------------------------------------
br_scroll_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [br_cx]                 ; x1 - 8-aligned by WF_SNAP, which is what
    mov bx, [br_cy]                 ; gfx_scroll requires of x1 and x2+1
    push ax
    mov ax, [br_cols]
    mov cx, BR_CELL
    mul cx
    mov cx, ax
    pop ax
    add cx, ax
    dec cx                          ; x2 = x1 + cols*8 - 1
    push ax
    push cx
    mov ax, [br_rows]
    mov cx, BR_CELL
    mul cx
    mov dx, ax
    pop cx
    pop ax
    add dx, bx
    dec dx                          ; y2 = y1 + rows*8 - 1
    push ax
    push cx
    push dx
    mov ax, [br_dy]
    mov cx, BR_CELL
    imul cx                         ; AX = dy in PIXELS, signed
    mov si, ax
    pop dx
    pop cx
    pop ax
    call OSAPI_GFX_SCROLL
    jc .full

    mov ax, [br_dy]
    or ax, ax
    js .up
                                    ; content went UP: the bottom |d| rows are
    mov bx, [br_rows]               ; the new ones
    sub bx, ax
    jns .dn_loop
    xor bx, bx
.dn_loop:
    cmp bx, [br_rows]
    jae .out
    mov ax, [br_top]
    add ax, bx
    call br_paint_line
    inc bx
    jmp .dn_loop
.up:
    neg ax                          ; content went DOWN: the top |d| rows
    xor bx, bx
.up_loop:
    cmp bx, ax
    jae .out
    push ax
    mov ax, [br_top]
    add ax, bx
    call br_paint_line
    pop ax
    inc bx
    jmp .up_loop
.full:
    call br_paint_band
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; INPUT
; =============================================================================
; The scancodes the SDK does not name. KSC_UP/DOWN it does.
KSC_PGUP equ 0x49
KSC_PGDN equ 0x51
KSC_HOME equ 0x47
KSC_END  equ 0x4F

BR_MAXSKIP equ 4                    ; see br_coalesce

; -----------------------------------------------------------------------------
; br_onkey - W_ONKEY
; in:  AL = ascii, AH = scan, SI = window ptr; lock held
;
; BROWSER-PLAN 4.2. Every scroll here goes through br_scroll_by, which is
; where the input coalescing is - see its header, and br_coalesce's.
; -----------------------------------------------------------------------------
br_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    call br_abdismiss               ; ...the card first: any key takes it down
    jc .out                         ; and is swallowed, so a reader cannot type
                                    ; into a bar they cannot see
    push si                         ; ...THE LOCATION BAR FIRST, and only when
    mov si, br_loc                  ; it has focus - os88line_key refuses every
    call os88line_key               ; key on an unfocused field, which is what
    pop si                          ; keeps the page's own keys working
    jc .notbar
    push si
    mov si, br_loc
    call os88line_draw
    pop si
    jmp .out
.notbar:
    cmp byte [br_loc + LN_FOCUS], 0
    je .page
    cmp ah, KSC_ENTER
    jne .esc
    mov byte [br_loc + LN_FOCUS], 0 ; RETURN IN THE BAR IS `GO`, which is the
    mov si, br_ubuf                 ; whole reason os88line_key hands Enter
    call br_go                      ; back rather than deciding what it means -
    jmp .out                        ; and br_go draws the bar, so the redraw
                                    ; that used to be here would be the second
                                    ; of two
.esc:
    cmp al, 27
    jne .out                        ; anything else with the bar focused is the
    mov byte [br_loc + LN_FOCUS], 0 ; bar's, and swallowed: a page that
    push si                         ; scrolled under a caret would be reading
    mov si, br_loc                  ; one control and typing into another
    call os88line_draw
    pop si
    jmp .out
.page:
    cmp ah, KSC_TAB
    je .tab
    cmp ah, KSC_ENTER
    je .enter
    call br_fkey                    ; a focused field takes printable keys and
    jnc .out                        ; backspace; the arrows still scroll, which
                                    ; 7.3 leaves as a v1 simplification
    call br_keydelta                ; CX = delta, CF=1 not ours
    jc .out
    jmp .scroll
.tab:
    call br_ftab
    jmp .out
.enter:
    cmp byte [br_fcur], 0FFh
    je .out
    call br_submit
    jmp .out
.scroll:
    mov ax, cx
    call br_scroll_by               ; ...which coalesces: the copy that used to
.out:                               ; be here is br_coalesce now
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_keydelta - one keystroke -> a signed line delta
; in:  AL = ascii, AH = scan
; out: CX = delta, CF=1 if the key is not ours
; -----------------------------------------------------------------------------
br_keydelta:
    cmp ah, KSC_UP
    jne .dn
    mov cx, -1
    clc
    ret
.dn:
    cmp ah, KSC_DOWN
    jne .pgup
    mov cx, 1
    clc
    ret
.pgup:
    cmp ah, KSC_PGUP
    jne .pgdn
    mov cx, [br_rows]
    dec cx
    neg cx
    clc
    ret
.pgdn:
    cmp ah, KSC_PGDN
    je .page
    cmp al, ' '
    je .page
    cmp ah, KSC_HOME
    jne .end
    mov cx, [br_top]
    neg cx
    clc
    ret
.end:
    cmp ah, KSC_END
    jne .no
    mov cx, [br_nlines]
    clc
    ret
.page:
    mov cx, [br_rows]
    dec cx
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; br_onclick - W_ONCLICK: the toolbar, the location bar, the scroll bar, the page
; in:  CX = x, DX = y, SI = window ptr; lock held
;
; The scroll bar's parts come from os88ui_sbhit reading the same block
; os88ui_sbar drew from (SPEC.md 13.10), so the drawn control and the clickable
; control cannot drift - SPEC.md 22's rule, now kept by construction rather
; than by two routines agreeing. What an arrow or a page DOES is still this
; app's: the shared element reports the part and no more.
; -----------------------------------------------------------------------------
br_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    call br_abdismiss               ; the credit card is dismissed by the click
    jc .out                         ; that dismisses it, and that click does
                                    ; nothing else (SPEC.md 12.2's shape, and
                                    ; every other app here)
    push cx                         ; **THE GEOMETRY FIRST, AND IT IS THE CLICK'S
    push dx                         ; TO TAKE.** Every rect this routine tests
    call br_measure                 ; against is derived from the content box,
    pop dx                          ; and the content box moves whenever the
    pop cx                          ; window does - a drag, a resize, an adapter
                                    ; switch, a slide across an extended
                                    ; desktop's seam (SPEC.md 39.7/11.98). A
                                    ; painter runs on some of those and not all,
                                    ; so a hit test that trusts the last paint's
                                    ; numbers is answering about where the
                                    ; window WAS. It clobbers CX/DX, which are
                                    ; the point, and it may relayout - which is
                                    ; safe here and nowhere near free, so it is
                                    ; a click and not a mouse-move
    push si                         ; ...THE LOCATION BAR FIRST, because it sits
    mov si, br_loc                  ; above the band and a click in it is not a
    call os88line_click             ; click in the page
    pop si
    jc .nobar
    push si
    mov si, br_loc
    call os88line_draw
    pop si
    jmp .out
.nobar:
    cmp byte [br_loc + LN_FOCUS], 0 ; **A CLICK ANYWHERE ELSE BLURS THE BAR**,
    je .nofoc                       ; because LN_FOCUS is what draws the caret
    mov byte [br_loc + LN_FOCUS], 0 ; (os88line_draw) - so a bar left focused
    push si                         ; while the reader is clicking links keeps
    mov si, br_loc                  ; a caret in it, pointing at a control the
    call os88line_draw              ; keyboard is no longer going to. The
    pop si                          ; include deliberately does NOT do this
.nofoc:                             ; itself: an outside click means something
                                    ; different in every host, and the Setup
                                    ; window's four fields hand focus to each
                                    ; other rather than dropping it
    mov ax, [br_tby]                ; --- the toolbar strip ---
    cmp dx, ax
    jb .out
    add ax, BR_TBH - 1
    cmp dx, ax
    ja .nostrip
    mov bx, br_r1
    call br_inrect
    jc .t2
    call br_okback                  ; ONE predicate for the greying and the
    jc .out                         ; refusal, so they cannot disagree
    mov bx, [br_histi]              ; (SPEC.md 47 rule 5) - and a greyed
    dec bx                          ; control explains itself, so a refused
    mov [br_histi], bx              ; click says nothing more (rule 6)
    call br_hgo
    jmp .out
.t2:
    mov bx, br_r2
    call br_inrect
    jc .t3
    call br_okfwd
    jc .out
    mov bx, [br_histi]
    inc bx
    mov [br_histi], bx
    call br_hgo
    jmp .out
.t3:
    mov bx, br_r3
    call br_inrect
    jc .out
    call br_okrel
    jc .out
    call br_hgo                     ; Reload is Back to where we already are
    jmp .out
.nostrip:
    cmp cx, [br_sbx]
    jb .page                        ; not in the scroll bar: the PAGE's
    cmp cx, [br_sbx2]
    ja .page
    call br_sbfill                  ; BX = the block; CX/DX are still the point
    call os88ui_sbhit               ; AL = which PART (SPEC.md 13.10)
    cmp al, OS88UI_SBUP
    je .lineup
    cmp al, OS88UI_SBDOWN
    je .linedn
    cmp al, OS88UI_SBPGUP
    je .pageup
    cmp al, OS88UI_SBPGDN
    je .pagedn
    jmp .out                        ; the thumb, or nowhere: this app pages
                                    ; from the TRACK only, which the shared
                                    ; element leaves to the caller because
                                    ; files.inc and fdlg.inc disagree and both
                                    ; are right about their own window
.page:
    call br_page_click              ; not in the bar: it is the PAGE's
    jmp short .out
.lineup:
    mov ax, -BR_SBSTEP
    jmp .go
.linedn:
    mov ax, BR_SBSTEP
    jmp .go
.pageup:
    mov ax, [br_rows]
    dec ax
    neg ax
    jmp .go
.pagedn:
    mov ax, [br_rows]
    dec ax
.go:
    call br_scroll_by
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE TOOLBAR (BROWSER-PLAN 5)
; =============================================================================

br_s_back:   db 'Back', 0
br_s_fwd:    db 'Fwd', 0
br_s_rel:    db 'Reload', 0

; -----------------------------------------------------------------------------
; br_hslot - DI = the history slot BX (0-based)
; -----------------------------------------------------------------------------
br_hslot:
    push ax
    mov ax, BR_UBUF
    mul bx                          ; **MUL WRITES DX** (SPEC.md 1), and this
                                    ; is called from paths that hold nothing
                                    ; there
    mov di, ax
    add di, br_hist
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_htslot - DI = the History LABEL of slot BX
; -----------------------------------------------------------------------------
br_htslot:
    push ax
    mov ax, BR_TITMAX
    mul bx                          ; MUL WRITES DX (SPEC.md 1)
    mov di, ax
    add di, br_htit
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_hlabel - the NUL string at DS:SI becomes slot BX's History label
; in:  BX = the slot, SI = the text; out: nothing, all registers preserved
;
; Truncated at BR_TITMAX-1 rather than refused: this is a menu caption, and
; the alternative to a shortened one is none at all.
; -----------------------------------------------------------------------------
br_hlabel:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    call br_htslot
    mov cx, BR_TITMAX - 1
.c:
    lodsb
    or al, al
    jz .z
    stosb
    loop .c
.z:
    xor al, al
    stosb
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_hsync - the History menu's item array and its count, from the Back stack
; in:  nothing; out: nothing, all registers preserved
;
; Called from br_toolbar and nowhere else, which is the point: the strip's
; greying and this menu answer the SAME question - what can this reader go
; back and forward to - and br_toolbar is already called at every place the
; history moves. Two callers' worth of "remember to also rebuild the menu" is
; how the bar and the pull-down come to disagree.
;
; It only fills POINTERS. The labels themselves are written when the entry is
; made (br_hpush, with the URL) and again when the page arrives with a <title>,
; so there is never a slot with no caption and nothing here has to decide.
; -----------------------------------------------------------------------------
br_hsync:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cx, [br_histn]
    or cx, cx
    jz .none
    cmp cx, BR_HISTN
    jbe .n
    mov cx, BR_HISTN
.n:
    xor bx, bx
.slot:
    call br_htslot                  ; DI = slot BX's label...
    mov ax, bx
    shl ax, 1
    xchg ax, di                     ; ...and DI is the array index it goes at
    mov [di+br_hitems], ax
    inc bx
    cmp bx, cx
    jb .slot
    mov [BR_HNITEM], cx
    jmp short .out
.none:
    mov word [br_hitems], br_it_none
    mov word [BR_HNITEM], 1
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_hpush - record the URL in br_ubuf as the newest history entry
; in:  br_ubuf holds the URL br_go just accepted; gfx lock held
; out: nothing; preserves all registers
;
; Everything AFTER the current position is dropped, which is what makes Back
; and Forward mean anything: navigating from the middle of the stack forks it,
; and the branch not taken is gone. Full, the oldest entry goes rather than the
; newest being refused - a browser that stopped remembering after eight pages
; would be remembering the wrong eight.
; -----------------------------------------------------------------------------
br_hpush:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov ax, [br_histn]
    or ax, ax
    jz .append                      ; nothing yet
    mov bx, [br_histi]
    inc bx
    mov [br_histn], bx              ; drop the forward branch
.append:
    cmp word [br_histn], BR_HISTN
    jb .room
                                    ; full: slide everything down one
    mov si, br_hist + BR_UBUF
    mov di, br_hist
    mov cx, (BR_HISTN - 1) * BR_UBUF
    rep movsb
    mov si, br_htit + BR_TITMAX     ; ...AND THE LABELS WITH THEM, or the menu
    mov di, br_htit                 ; keeps naming the page each slot used to
    mov cx, (BR_HISTN - 1) * BR_TITMAX      ; hold
    rep movsb
    mov word [br_histn], BR_HISTN - 1
.room:
    mov bx, [br_histn]
    call br_hslot
    mov si, br_ubuf
    mov cx, BR_UBUF
    rep movsb
    mov si, br_ubuf                 ; the URL is the label until the page
    call br_hlabel                  ; arrives with a <title> of its own, so a
                                    ; slot never has an empty caption and
                                    ; br_hsync never has to choose
    mov ax, [br_histn]
    mov [br_histi], ax
    inc word [br_histn]
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_hgo - load history entry BX without recording it
; in:  BX = the slot; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
br_hgo:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov bx, [br_histi]              ; **THE SLOT IS [br_histi] AND NOT BX.**
                                    ; It took BX undocumented while every
                                    ; caller ALSO stored [br_histi] a line
                                    ; earlier - two descriptions of one thing,
                                    ; and the History menu is the caller that
                                    ; found out: it set the word, left BX
                                    ; holding the kernel's menu-set pointer,
                                    ; and br_hslot resolved a slot several
                                    ; kilobytes past the stack
    call br_hslot
    mov si, di
    mov di, br_ubuf                 ; br_go reads the URL from br_ubuf and puts
    mov cx, BR_UBUF                 ; it in the bar, so that is where it goes
    rep movsb
    mov byte [br_nopush], 1         ; ...and this trip is not a new place
    mov si, br_ubuf
    call br_go
    mov byte [br_nopush], 0
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- the three predicates, each answering CF=0 LIVE (gfx_pen_cf's shape) -----
br_okback:
    cmp word [br_histn], 0
    je .no
    cmp word [br_histi], 0
    je .no
    clc
    ret
.no:
    stc
    ret

br_okfwd:
    push ax
    mov ax, [br_histi]
    inc ax
    cmp ax, [br_histn]
    pop ax
    jb .yes
    stc
    ret
.yes:
    clc
    ret

br_okrel:
    cmp byte [br_ubuf], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; br_toolbar - the three buttons and the state, in one thin strip
; in:  the br_* geometry current; gfx lock held
; out: nothing; preserves all registers
;
; The buttons are os88ui_btn's (SPEC.md 20.5.1), so the frame, the centred
; label and - the half that matters - the DISABLED treatment are the system's
; rather than three more copies of it. Greying is a FLAG and not a colour
; (SPEC.md 47 rule 1): OS88UI_DIS carries [gfx_dis] as well as CDGRAY, which is
; what makes a dead Back readable on a Hercules instead of pixel-identical to a
; live one.
;
; Each is greyed on a FACT - is there anywhere to go - rather than a guess,
; which is rule 3, and the predicates below are also what the CLICK refuses on,
; so the drawing and the refusal cannot disagree (rule 5).
; -----------------------------------------------------------------------------
br_toolbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_hsync                   ; the History menu answers the same
                                    ; question these two buttons do
    mov bx, br_r1
    mov si, br_s_back
    call br_okback
    call br_btn1
    mov bx, br_r2
    mov si, br_s_fwd
    call br_okfwd
    call br_btn1
    mov bx, br_r3
    mov si, br_s_rel
    call br_okrel
    call br_btn1
    call br_status
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_inrect - is (CX,DX) inside the rect at BX? CF=0 yes ------------------
br_inrect:
    cmp cx, [bx]
    jb .no
    cmp cx, [bx+4]
    ja .no
    cmp dx, [bx+2]
    jb .no
    cmp dx, [bx+6]
    ja .no
    clc
    ret
.no:
    stc
    ret

; --- br_btn1 - one toolbar button; CF on entry = 0 live, 1 disabled ----------
br_btn1:
    push di
    mov di, OS88UI_FILL
    jnc .live
    or di, OS88UI_DIS
.live:
    call os88ui_btn
    pop di
    ret

; -----------------------------------------------------------------------------
; br_hitofs - a click in the band -> the DOCUMENT OFFSET under it
; in:  CX = x, DX = y (SCREEN); the br_c* geometry current
; out: CF=0 and AX = the document offset, BX = the display line; CF=1 = the
;      point is outside the band, past the last line, or on a cell the
;      padding owns. Every other register preserved.
;
; **IT ANSWERS BY RE-RUNNING THE PAINTER'S OWN WALK.** br_build fills br_lmap
; beside br_lbuf, so this positions a row and a column, rebuilds that one line
; and reads the map - which means the hit test cannot disagree with what was
; drawn, however the line got its shape. Markers, table cells, centred lines,
; a field's spaces and an indent are all handled because none of them is
; handled HERE (SPEC.md 22's fm_hit discipline, one layer down: the shared
; thing is a walk rather than a rect).
;
; Rebuilding a line costs what painting one costs less the font_run - a few
; hundred instructions - and it happens once per click.
; -----------------------------------------------------------------------------
br_hitofs:
    push cx
    push dx
    push si
    push di
    cmp dx, [br_cy]
    jb .no
    cmp cx, [br_cx]
    jb .no
    mov si, cx                      ; SI = x and DI = y, banked before CL is
    mov di, dx                      ; borrowed as a shift count (SPEC.md 1:
                                    ; the 8086 shifts by 1 or by CL, so a
                                    ; value held in CX eats its own count)
    mov cl, 3
    sub di, [br_cy]
    mov ax, di
    shr ax, cl
    cmp ax, [br_rows]
    jae .no                         ; below the last band row
    add ax, [br_top]
    cmp ax, [br_nlines]
    jae .no                         ; past the last line of the document
    mov bx, ax                      ; BX = the display line
    sub si, [br_cx]
    mov ax, si
    shr ax, cl
    cmp ax, [br_cols]
    jae .no                         ; in the scroll bar's column, or past it
    mov di, ax                      ; DI = the column
    mov ax, bx
    push bx
    call br_build                   ; ...THE PAINTER'S WALK, and it fills the
    pop bx                          ; map this reads
    shl di, 1
    mov ax, [br_lmap+di]
    cmp ax, 0FFFFh
    je .no                          ; padding: no document byte here
    pop di
    pop si
    pop dx
    pop cx
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; br_linkat - which link is document offset AX inside? (BROWSER-PLAN 6)
; in:  AX = a document offset
; out: CF=0 and AX = the link index; CF=1 = not in a link. Everything else
;      preserved.
;
; **A BACKWARD SCAN, AND IT IS UNAMBIGUOUS BY CONSTRUCTION.** Every real marker
; is 0..15 and D_LNK1's two payload bytes are 16..31, so walking back from the
; click the FIRST byte below 16 answers the question: D_LNK1 means we are
; inside that link, anything else means the last thing to happen was not an
; anchor opening. Without that split a payload byte of 15 would read as D_LNK0
; and a link would go dead from its sixteenth onward.
;
; Bounded at BR_LNKSCAN rather than walking to offset 0: on a page with no
; links the count test below costs nothing at all, and on one with links an
; anchor whose text is longer than the bound reads as not-a-link, which is a
; click that does nothing rather than a click that goes somewhere wrong.
; -----------------------------------------------------------------------------
br_linkat:
    push bx
    push cx
    push dx
    push si
    push es
    cmp word [br_lnkn], 0
    je .no
    mov es, [br_docseg]
    mov si, ax
    mov cx, BR_LNKSCAN
.back:
    or si, si
    jz .no
    dec si
    mov al, [es:si]
    cmp al, 0x10
    jae .step                       ; 16..31: a payload byte, keep going
    cmp al, D_LNK1
    je .in
    cmp al, D_LNK0
    je .no                          ; the last anchor CLOSED before this byte
    cmp al, 0x20
    jae .step                       ; ordinary text
.step:
    loop .back
    jmp short .no
.in:
    mov al, [es:si+1]               ; the two payload bytes, low nibble first
    mov ah, [es:si+2]
    and al, 15
    and ah, 15
    mov cl, 4
    shl ah, cl
    or al, ah
    xor ah, ah
    cmp ax, [br_lnkn]
    jae .no                         ; a page that shrank under a stale offset
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; br_follow - go to link AX (BROWSER-PLAN 6)
; in:  AX = the link index; gfx lock held
; out: nothing; preserves all registers
;
; The href is resolved against the page we are ON - which is what br_host and
; br_path already hold, br_split having filled them for the fetch that brought
; us here - and composed into br_ubuf, because br_go reads the URL from there
; and shows it in the bar.
; -----------------------------------------------------------------------------
br_follow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, ax
    shl bx, 1
    mov si, [br_lnkoff+bx]          ; ES:SI = the href, in the link arena
    mov es, [br_lnkseg]
    call br_resolve
    jc .out                         ; refused, and it said why
    mov si, br_ubuf
    call br_go
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
; br_resolve - an href or a form action -> an absolute URL in br_ubuf
; in:  ES:SI = the source, NUL-terminated. **ES IS THE CALLER'S** because there
;      are two of them and they live in different segments: a link's href is in
;      the link arena and a form's composed action is in this package's own
;      br_url. The source may not BE br_ubuf, which this writes as it reads.
; out: CF=0 and br_ubuf holds it; CF=1 = refused, with the reason already in
;      the status line
;
; Four shapes, and the third is the one a real site needs most:
;   http://...   absolute, taken as it stands
;   https://...  REFUSED OUT LOUD (BROWSER-PLAN 8.3): there is no TLS here and
;                there will not be, and a link that silently did nothing reads
;                as a broken browser
;   #...         an anchor within this page: nothing to fetch, so nothing
;                happens - deliberately silent, because it IS this page
;   /path        the host we are on, plus it
;   path         ...plus the DIRECTORY part of the path we are on
; -----------------------------------------------------------------------------
br_resolve:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, br_ubuf
    mov word [br_upos], 0
    cmp byte [es:si], '#'
    je .anchor
    mov bx, br_s_scheme             ; "http://"
    call br_hrefpfx
    jnc .abs
    mov bx, br_s_schemes
    call br_hrefpfx
    jnc .https
    cmp byte [es:si], '/'
    je .root
                                    ; --- relative to this page's directory ---
    call br_haveh
    jc .nohost
    call br_ubase                   ; scheme + host + port
    mov bx, br_path                 ; ...and the path up to its last '/'
    xor cx, cx
    xor dx, dx
.dirscan:
    mov al, [bx]
    or al, al
    jz .dirgot
    inc bx
    inc cx
    cmp al, '/'
    jne .dirscan
    mov dx, cx                      ; DX = one past the last '/'
    jmp .dirscan
.dirgot:
    mov bx, br_path
    mov cx, dx
    call br_uputn                   ; that many bytes of it
    call br_uputh                   ; ...then the href
    jmp .done
.root:
    call br_haveh
    jc .nohost
    call br_ubase
    call br_uputh
    jmp .done
.nohost:
    push si                         ; **A PAGE OPENED FROM A FILE HAS NO
    mov si, br_s_nohost             ; SERVER**, so a relative link has nothing
    mov [br_nmsg], si               ; to be relative TO - br_host is whatever
    mov byte [br_nstate], BN_ERR    ; the last fetch left, or empty on a
    call br_status                  ; browser that has not fetched at all.
    pop si                          ; Composing `http:///torture.htm` and
    stc                             ; letting br_split refuse it would report
    jmp .out                        ; a bad ADDRESS about a good link
.abs:
    call br_uputh                   ; already absolute
    jmp .done
.anchor:
    stc                             ; the page we are already on
    jmp .out
.https:
    push si
    mov si, br_s_nohttps
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
    call br_status
    pop si
    stc
    jmp .out
.done:
    mov bx, [br_upos]
    mov byte [br_ubuf+bx], 0
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_haveh - is there a host to be relative to? CF=1 = no ----------------
br_haveh:
    cmp byte [br_host], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; --- br_hrefpfx - does the href at ES:SI start with the NUL string DS:BX? ----
; out: CF=0 yes, CF=1 no; SI and BX preserved
br_hrefpfx:
    push ax
    push bx
    push si
.loop:
    mov al, [bx]
    or al, al
    jz .yes
    mov ah, [es:si]
    cmp ah, 'A'
    jb .c
    cmp ah, 'Z'
    ja .c
    or ah, 0x20                     ; a scheme is case-insensitive
.c:
    cmp al, ah
    jne .no
    inc bx
    inc si
    jmp .loop
.yes:
    pop si
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop bx
    pop ax
    stc
    ret

; --- br_ubase - "http://host" (+ ":port" when it is not 80) into br_ubuf -----
br_ubase:
    push ax
    push bx
    push cx
    mov bx, br_s_scheme
    call br_uputz
    mov bx, br_host
    call br_uputz
    cmp word [br_port], 80
    je .out
    mov bx, br_s_colon
    call br_uputz
    mov ax, [br_port]
    call br_uputdec
.out:
    pop cx
    pop bx
    pop ax
    ret

; --- br_uputz - the NUL string at DS:BX into br_ubuf -------------------------
br_uputz:
    push ax
    push bx
    push di
.loop:
    mov al, [bx]
    or al, al
    jz .out
    call br_uputc
    inc bx
    jmp .loop
.out:
    pop di
    pop bx
    pop ax
    ret

; --- br_uputn - CX bytes from DS:BX ------------------------------------------
br_uputn:
    push ax
    push bx
    push cx
.loop:
    jcxz .out
    mov al, [bx]
    or al, al
    jz .out
    call br_uputc
    inc bx
    dec cx
    jmp .loop
.out:
    pop cx
    pop bx
    pop ax
    ret

; --- br_uputh - the href at ES:SI --------------------------------------------
br_uputh:
    push ax
    push si
.loop:
    mov al, [es:si]
    or al, al
    jz .out
    call br_uputc
    inc si
    jmp .loop
.out:
    pop si
    pop ax
    ret

; --- br_uputdec - AX as decimal ----------------------------------------------
br_uputdec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
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
    call br_uputc
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_uputc - AL into br_ubuf, truncating at BR_UBUF-1 ---------------------
; TRUNCATES rather than refusing: the alternative is a link that does nothing
; at all, and a truncated URL fails at the server with a message the user can
; read (br_split refuses a malformed one before any of it reaches the wire).
br_uputc:
    push bx
    mov bx, [br_upos]
    cmp bx, BR_UBUF - 1
    jae .out
    mov [br_ubuf+bx], al
    inc word [br_upos]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; br_page_click - a click that landed in the band (BROWSER-PLAN 7.4)
; in:  CX = x, DX = y (SCREEN); gfx lock held
; out: nothing; preserves all registers
;
; A field's cells and the submit button are both ORDINARY DOCUMENT TEXT - that
; is what makes them wrap, centre and scroll with no painter of their own - so
; both are found by document OFFSET rather than by a rect, and neither needs a
; layout pass to have remembered anything.
;
; Focus was reachable by Tab alone until this, and typing worked perfectly
; once you got there. On a real page nobody presses Tab first: they click the
; box, type into a field that does not have focus, and the keystrokes scroll
; the page instead.
; -----------------------------------------------------------------------------
br_page_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_hitofs
    jc .out
    mov si, ax                      ; SI = the document offset clicked
    call br_linkat                  ; --- a link, first: it is the commonest
    jc .notlink                     ; thing on a page and the cheapest to ask
    call br_follow
    jmp .out
.notlink:
    cmp word [br_subend], 0         ; --- the submit button ---
    je .fields
    cmp si, [br_subofs]
    jb .fields
    cmp si, [br_subend]
    jae .fields
    cmp byte [br_fn], 0
    je .out                         ; a button with no field is not a query
    call br_submit
    jmp .out
.fields:                            ; --- a text input ---
    xor bx, bx                      ; BL = field index
.next:
    cmp bl, [br_fn]
    jae .out
    mov al, bl
    push bx
    call br_fld                     ; BX = the record
    mov ax, [bx+FLD_OFS]
    mov cl, [bx+FLD_W]
    pop bx
    cmp si, ax
    jb .step                        ; before the first editable cell...
    xor ch, ch
    add ax, cx
    cmp si, ax
    jae .step                       ; ...or past the last: the '[' and ']' are
                                    ; the frame and not the field
    mov al, bl
    call br_ffocus                  ; ...and the caret goes to the END of what
    jmp .out                        ; is typed, which is what every one-line
.step:                              ; field in every system does
    inc bl
    jmp .next
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE ABOUT PANEL (SPEC.md 12.2)
;
; STATE, not a modal loop: [br_abon] goes up, the card is drawn over the band,
; and the next click or key takes it down. A loop here would hold the gfx lock
; against the fetch worker for as long as the reader left the credits up -
; which on this app is not an abstract cost, because a page can be arriving.
; =============================================================================

BR_ABLH equ 11                      ; line pitch, px (8px glyphs + 3 of air)
br_ablines:
    dw br_ab1, br_ab2, br_ab3, br_ab4, 0
br_ab1:      db 'Browser for os8088', 0
br_ab2:      db 'HTTP, HTML and tables', 0
br_ab3:      db 0                   ; a blank line is a line with no glyphs
br_ab4:      db 'Contributed by Elendilon', 0

; -----------------------------------------------------------------------------
; br_about - the OSAPI_ABOUT_SET handler (slot 0x01E0)
; in:  SI = our window ptr; UI task, gfx lock HELD, far-called at our segment
; out: nothing; preserves all registers
;
; Only the CARD is drawn: nothing under it has changed and the card is opaque
; over its own rect, so repainting the page first would be PERFORMANCE.md's
; double-draw with the credits as the second layer - and on this app the first
; layer is a bandful of glyphs, which is the expensive one.
; -----------------------------------------------------------------------------
br_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_measure                 ; the window may have moved since the last
    mov byte [br_abon], 1           ; paint; the card is placed from the LIVE
    call br_abdraw                  ; band, br_sbrect's own reasoning
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; br_abdismiss - take the card down, if it is up
; in:  [br_win] set; gfx lock held
; out: CF = 1 if it WAS up (and the window has been repainted), so the caller
;      swallows the click or key that dismissed it; CF = 0 otherwise.
;      Preserves every register.
;
; The way back is a full repaint and not the card's inverse: it covered the
; band, the scroll bar and the status line, and those come back whole rather
; than in the shape of the thing that covered them.
; -----------------------------------------------------------------------------
br_abdismiss:
    cmp byte [br_abon], 0
    je .none
    push si
    mov byte [br_abon], 0
    mov si, [br_win]
    call br_paint
    pop si
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; br_abmeas - size and place the card, in SCREEN coords
; out: [br_abw]/[br_abh]/[br_abl]/[br_abt]; clobbers nothing (flags)
;
; Measured, never pinned: the widest line sets the width and the count sets the
; height, so a line of the credit can change without re-deriving anything.
; Clamped to the BAND, which is what wm_fit and the location bar between them
; left - on a CGA that is 150px of window less the chrome.
; -----------------------------------------------------------------------------
br_abmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, br_ablines
.next:
    mov bx, [si]
    or bx, bx
    jz .done
    inc di
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    pop si
    cmp ax, cx
    jbe .next
    mov cx, ax
    jmp .next
.done:
    add cx, 24                      ; 12px of margin either side
    cmp cx, [br_cw]
    jbe .wok
    mov cx, [br_cw]
.wok:
    mov [br_abw], cx
    mov ax, di                      ; height = lines * BR_ABLH + margins
    mov bx, BR_ABLH
    mul bx                          ; **MUL WRITES DX** (SPEC.md 1), which is
                                    ; why nothing is banked there across it
    add ax, 16
    cmp ax, [br_ch]
    jbe .hok
    mov ax, [br_ch]
.hok:
    mov [br_abh], ax
    mov ax, [br_cw]                 ; centred on the band, in SCREEN coords
    sub ax, [br_abw]
    shr ax, 1
    add ax, [br_cx]
    mov [br_abl], ax
    mov ax, [br_ch]
    sub ax, [br_abh]
    shr ax, 1
    add ax, [br_cy]
    mov [br_abt], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_abdraw - white fill, black frame, the credit centred in it
; in:  the geometry is current; gfx lock held
; out: nothing; preserves all registers
;
; Every line is centred on the CARD rather than on the band, so the block still
; reads as one card when br_abmeas has clamped it narrower than the text wanted.
; -----------------------------------------------------------------------------
br_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_abmeas
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call .rect
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call .rect
    call OSAPI_GFX_FRAME

    mov si, br_ablines
    mov di, [br_abt]
    add di, 8                       ; the first baseline, inside the frame
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = this line's width
    mov cx, [br_abw]
    sub cx, ax
    shr cx, 1
    add cx, [br_abl]
    mov dx, di
    call OSAPI_FONT_STR
    pop si
    add di, BR_ABLH
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.rect:                              ; the card as (x1,y1)-(x2,y2), inclusive
    mov ax, [br_abl]
    mov bx, [br_abt]
    mov cx, ax
    add cx, [br_abw]
    dec cx
    mov dx, bx
    add dx, [br_abh]
    dec dx
    ret

; =============================================================================
; MENUS
; =============================================================================
br_oncmd:
    push ax
    push bx
    push si
    or ah, ah
    jnz .m1
    or al, al
    jnz .f1
    call br_open                    ; File > Open...
    jmp .out
.f1:
    cmp al, 2
    je .save
    jmp .loc
.save:
    call br_saveas                  ; File > Save As...
    jmp .out
.loc:
    mov byte [br_loc + LN_FOCUS], 1 ; File > Open Location... is FOCUS AND
    mov si, [br_win]                ; NOTHING ELSE: there is no modal dialog
    push si                         ; here, so the menu item's whole job is to
    mov si, br_loc                  ; put the caret where the user can type
    call os88line_draw
    pop si
    jmp .out
.m1:
    cmp ah, 2
    je .hist
.go:
    cmp al, 2                       ; **AL, AND IT WAS BL** - which the kernel
    je .reload                      ; hands over as the MENU SET's pointer
                                    ; (SPEC.md 12.2), not as anything about the
                                    ; item. Go > Reload therefore fired or did
                                    ; not according to the low byte of a label,
                                    ; and it happened to be 2 in the build the
                                    ; gate ran on - so Reload passed, Top and
                                    ; Bottom were never reached, and the next
                                    ; edit anywhere in the image would have
                                    ; silently swapped which of the three
                                    ; worked. The `mov bx, ax` below is what
                                    ; makes the SECOND test (or bl, bl) right
    mov bx, ax                      ; Go > Top / Bottom
    mov ax, [br_top]
    neg ax
    or bl, bl
    jz .move
    mov ax, [br_nlines]
.move:
    call br_scroll_by
    jmp short .out
.hist:
    cmp word [br_histn], 0          ; History > a page: jump straight to it.
    je .out                         ; The `(empty)` item is a label and not a
    xor ah, ah                      ; destination
    cmp ax, [br_histn]
    jae .out
    mov [br_histi], ax
    call br_hgo
    jmp .out
.reload:
    mov si, br_ubuf                 ; Go > Reload: the bar's own text, which is
    cmp byte [si], 0                ; what br_go last put there
    je .out
    call br_go
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_open - put the Standard File dialog up
; in:  lock held (a menu command's environment)
; -----------------------------------------------------------------------------
br_open:
    push ax
    push bx
    push si
    push di
    mov al, FDLG_OPEN
    mov bx, [br_win]
    mov di, br_opened
    xor si, si
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_lnkclaim - the link arena, for whichever load path is claiming
; out: [br_lnkseg], or 0; CF is not meaningful. All registers preserved.
;
; **A ROUTINE BECAUSE THERE ARE TWO CLAIM PATHS AND ONE FORGOT.** br_load
; claims for a file and brnet's br_claim claims for a fetch - the same three
; buffers at different sizes, because a file's size is known and a server's
; Content-Length is a claim by a stranger - and the arena was added to the
; file one only. Links therefore worked perfectly on a page opened from a
; floppy and were plain text on every page off the wire, which is the half
; that matters and the half no local test could see. One routine now, so a
; fourth buffer cannot be added to one path and not the other.
;
; **A REFUSAL IS NOT A FAILURE**: br_anchor tests [br_lnkseg] and renders
; anchors as ordinary text when there is none, so a machine too small for 6KB
; reads the page and cannot follow it - a great deal better than refusing it.
; -----------------------------------------------------------------------------
br_lnkclaim:
    push ax
    push dx
    mov word [br_lnkseg], 0
    mov ax, BR_LINKKB
    call OSAPI_MEM_CLAIM
    jc .out
    mov [br_lnkseg], dx
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_getname - copy the dialog's chosen name into br_name
; in:  DI = the name's offset, ES = KERNEL_SEG (SPEC.md 38.6's contract)
; out: br_name holds it, NUL-terminated; all registers preserved
;
; **THROUGH ES, ONE BYTE AT A TIME, AND NOT `rep movsb`.** The buffer is the
; KERNEL's and `movs` reads DS:SI - which in a package is our own image, so a
; movsb copy takes 13 bytes of BROWSER from that offset and calls them a file
; name. Both completion procs here did exactly that; Open survived it only
; because nothing has ever driven Open through the DIALOG - a document is
; opened by double-clicking it, which is br_arg's path and copies the name
; correctly. Save As has no other way in, so it failed on the first try.
;
; Bounded at 12 + the NUL even though 38.6 promises no more: a package that
; trusts a promise is a package with an overrun in it.
; -----------------------------------------------------------------------------
br_getname:
    push ax
    push cx
    push si
    push di
    mov si, di
    mov di, br_name
    mov cx, 12
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_saveas - put the page's SOURCE on a disk (BROWSER-PLAN 5)
; in:  gfx lock held (a menu command's environment)
; out: nothing; preserves all registers
;
; The bytes the server sent, not a re-render of the document stream: the point
; of this is to carry a real page off the machine, and anything this browser
; reconstructed would be a report about its own parser rather than evidence
; about the page.
;
; No default name is offered. The URL's last component is usually a script
; path with a query on it (`search.php?q=os8088`), which is not an 8.3 name and
; would have to be mangled into one - and a mangled default is worse than an
; empty box, because it looks like a considered suggestion.
; -----------------------------------------------------------------------------
br_saveas:
    push ax
    push bx
    push si
    push di
    cmp word [br_srcseg], 0
    je .none                        ; nothing loaded: say so rather than
    cmp word [br_srclen], 0         ; raising a dialog that cannot succeed
    je .none
    mov al, FDLG_SAVE
    mov bx, [br_win]
    mov di, br_saved
    xor si, si
    call OSAPI_FILE_DLG
    jmp short .out
.none:
    mov si, br_s_nopage
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
    call br_status
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_saved - Save As's completion proc
; in:  AL = mode, SI = our window, DI = the chosen name; UI task, gfx lock
;      held, the dialog window already gone
; out: nothing
;
; The name is COPIED before anything else happens: the kernel's buffer is
; valid for this call only (SPEC.md 38.6), and OSAPI_FILE_WRITE is a mount, a
; chain walk and a directory commit between here and reading it.
; -----------------------------------------------------------------------------
br_saved:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [br_win], si
    cmp byte [es:di], 0
    je .out                         ; cancelled
    call br_getname
    cmp word [br_srcseg], 0
    je .out                         ; the page went while the dialog was up
    mov si, br_name
    mov es, [br_srcseg]
    xor bx, bx
    mov cx, [br_srclen]
    xor dx, dx                      ; **DX IS AN ARGUMENT** and the count is
    call OSAPI_FILE_WRITE           ; 32-bit: a 16-bit one here writes whatever
    jc .failed                      ; was in DX as the high word
    mov si, br_s_saved
    mov al, BN_DONE                 ; ...and it FINISHED, which is not the same
    jmp short .say                  ; as it failed
.failed:
    mov si, br_s_wfail
    mov al, BN_ERR
.say:
    mov [br_nmsg], si
    mov [br_nstate], al             ; br_status shows the message on either,
                                    ; because both are finished states; the
                                    ; next navigation clears it
    call br_status
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
; br_opened - the dialog's completion proc
; in:  AL = mode, SI = our window, DI = the chosen name, DX:CX = its SIZE
;      UI task, gfx lock held, dialog window already gone
; out: nothing
;
; The size is checked BEFORE the disk is touched, which the SDK is explicit
; about: refusing after a read is ten seconds of motor on the target machine
; and the user cannot tell that from a load that works.
; -----------------------------------------------------------------------------
br_opened:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [br_win], si
    or dx, dx
    jnz .toobig                     ; over 64KB before we even look at KB
    cmp cx, BR_MAXKB*1024
    ja .toobig
    or cx, cx
    jz .out                         ; a folder, or a name with no file
    mov [br_srclen], cx

    call br_getname                 ; the kernel's buffer is valid for this
                                    ; call only - and it is ES:DI, not DS:DI
    call br_load
    jc .failed
    mov si, [br_win]
    call br_measure
    mov word [br_top], 0
    mov word [br_ptop], 0
    call br_layout
    call br_paint_band
    call br_sbar
    jmp .out
.toobig:
    mov si, br_s_big
    jmp .say
.failed:
    mov si, br_s_bad
.say:
    mov cx, 90
    push es
    push ds
    pop es
    call OSAPI_TOAST                ; ES:SI, CX ticks
    pop es
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; LOADING - claim, read, parse, free the source
; =============================================================================
br_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [br_nstate]             ; **A FETCH IN FLIGHT IS DROPPED HERE TOO**,
    cmp al, BN_OPEN                 ; on br_go's range test and through br_go's
    jb .nofetch                     ; own br_abort: the free below hands the
    cmp al, BN_DONE                 ; source claim back and the file's SIZE is
    jae .nofetch                    ; claimed in its place, while a worker that
    call br_abort                   ; nothing had told still drained the socket
.nofetch:                           ; into it - so File > Open on a small page
                                    ; while a large one arrived wrote the rest
                                    ; of the reply past the end of the new
                                    ; claim, and into segment 0 if it landed
                                    ; between the free and the claim. It is
                                    ; here and not in br_opened because br_load
                                    ; is what frees the buffers, and a third
                                    ; caller cannot forget a guard it does not
                                    ; have to write (br_arg's launch path
                                    ; reaches it at BN_IDLE, where it is a
                                    ; no-op)
    call br_free_all

    mov ax, [br_srclen]             ; source claim: exactly the file when the
    or ax, ax                       ; dialog told us its size, the ceiling when
    jnz .known                      ; we were launched on it and nobody did
    mov ax, BR_MAXKB
    jmp .kb
.known:
    mov cl, 10
    shr ax, cl
    inc ax
.kb:
    mov [br_srckb], ax
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [br_srcseg], dx

    mov ax, [br_srckb]              ; document claim
    mov bx, BR_DOCMUL
    mul bx
    add ax, BR_DOCSLK
    cmp ax, BR_DOCMAX               ; ...clamped, because every offset into it
    jbe .dok                        ; is a WORD: 64 KB is not a 16-bit byte
    mov ax, BR_DOCMAX               ; count, and the limit arithmetic below
.dok:                               ; would wrap rather than refuse
    mov [br_dockb], ax
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [br_docseg], dx

    mov ax, BR_LINEKB               ; line table
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [br_lseg], dx

    call br_lnkclaim                ; ...and the hrefs (BROWSER-PLAN 6)

    mov si, br_name                 ; read the whole file
    mov es, [br_srcseg]
    xor bx, bx
    mov cx, [br_srckb]
    mov ax, 1024
    mul cx
    mov cx, ax                      ; capacity = what we actually claimed
    xor dx, dx
    call OSAPI_FILE_READ
    jc .fail
    or dx, dx
    jnz .fail                       ; over 64KB: FERR_BIG should have caught it
    mov [br_srclen], ax             ; the file's REAL size, from the read

    call br_parse                   ; source -> the document stream

                                    ; **THE SOURCE IS KEPT**, and it used to be
                                    ; freed here on the reasoning that the
                                    ; stream is self-contained and nothing
                                    ; above the parse reads the file again.
                                    ; File > Save As does (BROWSER-PLAN 5) -
                                    ; the whole point of it is to put the bytes
                                    ; the SERVER sent on a floppy, and a
                                    ; re-render of the document stream would be
                                    ; a different file. It costs the heap the
                                    ; page's own size until the next
                                    ; navigation, and NOT the peak: br_free_all
                                    ; runs before br_claim, so a load still
                                    ; holds one page's worth at a time
    clc
    jmp .out
.fail:
    call br_free_all
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

br_free_all:
    push ax
    push dx
    mov dx, [br_srcseg]
    or dx, dx
    jz .a
    call OSAPI_MEM_FREE
    mov word [br_srcseg], 0
.a:
    mov dx, [br_docseg]
    or dx, dx
    jz .b
    call OSAPI_MEM_FREE
    mov word [br_docseg], 0
.b:
    mov dx, [br_lseg]
    or dx, dx
    jz .c
    call OSAPI_MEM_FREE
    mov word [br_lseg], 0
.c:
    mov dx, [br_lnkseg]
    or dx, dx
    jz .d
    call OSAPI_MEM_FREE
    mov word [br_lnkseg], 0
.d:
    mov word [br_doclen], 0
    mov word [br_nlines], 0
    mov word [br_top], 0
    mov word [br_ptop], 0
    mov word [br_cols], 0           ; force a relayout on the next paint
    pop dx
    pop ax
    ret

; =============================================================================
; THE PARSE (BROWSER-PLAN 2.2)
; =============================================================================
; Reads the source through ES:SI and accumulates output in a NEAR staging
; buffer, flushed to the document claim when it fills. That shape exists
; because there is one ES: reading the source and writing the document are
; two segments, and switching ES per byte is 13 clocks a character on a
; machine where the whole parse is supposed to be ~100 ms.
; -----------------------------------------------------------------------------
br_parse:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [br_doclen], 0
    mov word [br_onw], 0
    mov word [br_lnkn], 0           ; the link table is the PAGE's
    mov word [br_lnkw], 0
    mov word [br_lnkopen], 0FFFFh
    mov word [br_lnkhit], 0FFFFh
    mov byte [br_fn], 0
    mov word [br_subofs], 0         ; ...and the submit button's span, or a
    mov word [br_subend], 0         ; page with no form inherits the last
                                    ; page's button at an offset that is now
                                    ; somebody else's text
    mov byte [br_hn], 0
    mov word [br_hnw], 0
    mov byte [br_hid], 0
    mov byte [br_faction], 0
    mov byte [br_ptitle], 0         ; the title is the PAGE's, like the links
    mov byte [br_fcur], 0FFh        ; 0 is a real field, so "none" is 0FFh
    mov byte [br_on], 0
    mov byte [br_wsp], 0
    mov byte [br_any], 0
    mov byte [br_pre], 0
    mov byte [br_trunc], 0
    call br_sniff_utf8
    mov es, [br_srcseg]
    xor si, si
.loop:
    cmp si, [br_srclen]
    jae .done
    mov al, [es:si]
    inc si
    cmp al, '<'
    je .tag
    cmp al, '&'
    je .ent
    call br_char
    jmp .loop
.tag:
    call br_tag
    jmp .loop
.ent:
    call br_entity
    jmp .loop
.done:
    mov al, D_END
    call br_put
    call br_flushbuf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_sniff_utf8 - does the page declare UTF-8? (BROWSER-PLAN 1.1.2) --------
; FrogFind declares ISO-8859-1 and a modern proxy will emit UTF-8, so the
; fold has to know which. Scans the first 2KB for "utf-8" after "charset".
br_sniff_utf8:
    push ax
    push cx
    push si
    push es
    mov byte [br_utf8], 0
    mov es, [br_srcseg]
    xor si, si
    mov cx, [br_srclen]
    cmp cx, 2048
    jbe .lim
    mov cx, 2048
.lim:
    sub cx, 5
    jbe .out
.scan:
    mov al, [es:si]
    or al, 0x20
    cmp al, 'u'
    jne .next
    mov al, [es:si+1]
    or al, 0x20
    cmp al, 't'
    jne .next
    mov al, [es:si+2]
    or al, 0x20
    cmp al, 'f'
    jne .next
    cmp byte [es:si+3], '-'
    jne .next
    cmp byte [es:si+4], '8'
    jne .next
    mov byte [br_utf8], 1
    jmp .out
.next:
    inc si
    loop .scan
.out:
    pop es
    pop si
    pop cx
    pop ax
    ret

; --- br_wflush - spend the pending space NOW, if one is owed -----------------
; in:  nothing; out: nothing, all registers preserved
;
; Whitespace collapses to a PENDING space (br_wsp) spent at the next ink
; character - and that is one instruction too late for a link. `</a> <a>` puts
; the space after D_LNK1, so the cell is inside the anchor's span, br_lmark
; says 1 for it, and br_underline - which rules one run of marked cells - draws
; straight through the gap: two links read as one with an underscore between
; them. Spending it BEFORE the marker is the whole fix, and it belongs here
; rather than in br_atag because the next inline marker will want it too.
; -----------------------------------------------------------------------------
br_wflush:
    push ax
    cmp byte [br_pre], 0
    jne .out                        ; PRE keeps its own spaces verbatim
    cmp byte [br_wsp], 0
    je .out
    mov byte [br_wsp], 0
    cmp byte [br_any], 0
    je .out                         ; ...but a space never LEADS a block
    mov al, ' '
    call br_put
.out:
    pop ax
    ret

; --- br_char - one source byte of text ---------------------------------------
; in: AL = the byte, ES:SI = just past it; out: SI may advance (UTF-8)
br_char:
    push bx
    cmp byte [br_pre], 0
    jne .pre
    cmp al, ' '
    ja .ink
    mov byte [br_wsp], 1            ; whitespace COLLAPSES to a pending space
    jmp .out
.ink:
    call br_wflush                  ; the pending space, if one is owed
.now:
    call br_fold
    jmp .out
.pre:
    cmp al, 10
    jne .pre2
    push ax
    mov al, D_BR
    call br_put
    pop ax
    jmp .out
.pre2:
    cmp al, 13
    je .out
    cmp al, 9
    jne .pre3
    mov al, ' '
.pre3:
    call br_fold
.out:
    pop bx
    ret

; --- br_fold - one character -> what the cell font can draw -------------------
; SPEC.md 6.1: the cell font is ASCII 0x20..0x7E and font_char indexes past
; the end above that. BROWSER-PLAN 2.2's third bug was folding only ENTITY
; expansions - most of the German web carries the accent as a raw byte.
br_fold:
    push bx
    cmp al, 0x20
    jb .drop
    cmp al, 0x7E
    jbe .emit
    cmp byte [br_utf8], 0
    je .latin
    call br_utf8cp                  ; AX = codepoint, SI advanced
    cmp ax, 0x100
    jb .cp8
    call br_foldwide
    jmp .emit
.cp8:
    cmp al, 0x20
    jb .drop                        ; a sequence decoding to a control byte
    cmp al, 0x7E                    ; would otherwise be emitted as a MARKER
    jbe .emit
.latin:
    mov bl, al
    sub bl, 0x80
    xor bh, bh
    mov al, [bx+br_l1tab]
    or al, al
    jz .drop
.emit:
    call br_put
    mov byte [br_any], 1
.drop:
    pop bx
    ret

; --- br_utf8cp - decode a UTF-8 sequence ES:SI is just past the lead of -------
; in: AL = lead byte; out: AX = codepoint (0x3F on anything malformed)
br_utf8cp:
    push cx
    mov ah, al
    and ah, 0xE0
    cmp ah, 0xC0
    je .two
    mov ah, al
    and ah, 0xF0
    cmp ah, 0xE0
    je .three
    mov al, '?'                     ; 4-byte, or a stray continuation
    xor ah, ah
    jmp .out
.two:
    and al, 0x1F
    xor ah, ah
    mov cl, 6
    shl ax, cl
    call br_u8next
    or al, cl
    jmp .out
.three:
    and al, 0x0F
    xor ah, ah
    mov cl, 6
    shl ax, cl
    push ax
    call br_u8next
    pop ax
    or al, cl
    mov cl, 6
    shl ax, cl
    call br_u8next
    or al, cl
.out:
    pop cx
    ret

; --- br_u8next - take one continuation byte -> CL = its 6 payload bits --------
br_u8next:
    xor cl, cl
    cmp si, [br_srclen]
    jae .out                        ; a truncated sequence at the end of the
    mov cl, [es:si]                 ; buffer must not run off it
    cmp cl, 0x80
    jb .bad
    cmp cl, 0xC0
    jae .bad
    inc si
    and cl, 0x3F
    ret
.bad:
    xor cl, cl
.out:
    ret

; --- br_foldwide - a codepoint above Latin-1 -> one ASCII char ----------------
br_foldwide:
    push bx
    mov bx, br_wtab
.scan:
    cmp word [bx], 0
    je .none
    cmp ax, [bx]
    je .hit
    add bx, 3
    jmp .scan
.hit:
    mov al, [bx+2]
    jmp .out
.none:
    mov al, '?'
.out:
    xor ah, ah
    pop bx
    ret

; --- br_entity - &...; -> folded text ----------------------------------------
; A malformed entity is LITERAL TEXT, never an error and never a swallowed
; line (tests/htm/torture.htm section 6).
br_entity:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, si                      ; remember where the body starts
    xor cx, cx
    mov bx, br_ebuf
.gather:
    cmp cx, 10
    jae .literal
    cmp si, [br_srclen]
    jae .literal
    mov al, [es:si]
    inc si
    cmp al, ';'
    je .have
    cmp al, '0'
    jb .chk
.ok:
    mov [bx], al
    inc bx
    inc cx
    jmp .gather
.chk:
    cmp al, '#'
    je .ok
    jmp .literal
.have:
    mov byte [bx], 0
    or cx, cx
    jz .literal
    cmp byte [br_ebuf], '#'
    je .num
    call br_named                   ; AL = the char, CF=1 unknown
    jc .literal
    call br_fold
    jmp .out
.num:
    call br_ednum                   ; AX = codepoint, CF=1 malformed
    jc .literal
    cmp ax, 0x100
    jb .n8
    call br_foldwide
    jmp .nemit
.n8:
    or ah, ah
    jnz .literal
.nemit:
    call br_fold
    jmp .out
.literal:
    mov si, di                      ; rewind: the '&' was just an ampersand
    mov al, '&'
    call br_fold
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_ednum - "#123" or "#xAB" in br_ebuf -> AX ----------------------------
br_ednum:
    push bx
    push cx
    push dx
    mov bx, br_ebuf+1
    xor ax, ax
    mov cl, [bx]
    or cl, 0x20
    cmp cl, 'x'
    je .hex
.dec:
    mov cl, [bx]
    or cl, cl
    jz .done
    cmp cl, '0'
    jb .bad
    cmp cl, '9'
    ja .bad
    mov dx, 10
    mul dx
    sub cl, '0'
    xor ch, ch
    add ax, cx
    inc bx
    jmp .dec
.hex:
    inc bx
    mov cl, [bx]
    or cl, cl
    jz .bad
.hloop:
    mov cl, [bx]
    or cl, cl
    jz .done
    or cl, 0x20
    cmp cl, '0'
    jb .bad
    cmp cl, '9'
    jbe .hd
    cmp cl, 'a'
    jb .bad
    cmp cl, 'f'
    ja .bad
    sub cl, 'a'-10
    jmp .hacc
.hd:
    sub cl, '0'
.hacc:
    mov dx, 16
    mul dx
    xor ch, ch
    add ax, cx
    inc bx
    jmp .hloop
.done:
    or ax, ax
    jz .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; --- br_named - br_ebuf against the named table -> AL ------------------------
br_named:
    push bx
    push si
    push di
    mov bx, br_enttab
.next:
    cmp byte [bx], 0
    je .none
    mov si, bx
    mov di, br_ebuf
.cmp:
    mov al, [si]
    or al, al
    jz .end
    cmp al, [di]
    jne .skip
    inc si
    inc di
    jmp .cmp
.end:
    cmp byte [di], 0
    jne .skip
    mov al, [si+1]
    clc
    jmp .out
.skip:
    mov bx, si
.fwd:
    cmp byte [bx], 0
    je .fwd2
    inc bx
    jmp .fwd
.fwd2:
    add bx, 2
    jmp .next
.none:
    stc
.out:
    pop di
    pop si
    pop bx
    ret

; --- br_tag - '<' has been consumed ------------------------------------------
br_tag:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp si, [br_srclen]
    jae .out
    mov al, [es:si]
    cmp al, '!'
    je .bang
    mov byte [br_close], 0
    cmp al, '/'
    jne .name
    inc si
    mov byte [br_close], 1
.name:
    mov di, br_tname
    xor cx, cx
.nloop:
    cmp si, [br_srclen]
    jae .skip
    mov al, [es:si]
    cmp al, '0'                     ; DIGITS FIRST. They sort BELOW 'A', so a
    jb .ndone                       ; letter test in front of them ends every
    cmp al, '9'                     ; name at its first digit - which read
    jbe .keep                       ; <h1> as "h" and cost every heading and
    cmp al, 'A'                     ; every paragraph break on the page
    jb .ndone
    cmp al, 'Z'
    jbe .lc
    cmp al, 'a'
    jb .ndone
    cmp al, 'z'
    jbe .keep
    jmp .ndone
.lc:
    or al, 0x20
.keep:
    cmp cx, 14
    jae .adv
    mov [di], al
    inc di
    inc cx
.adv:
    inc si
    jmp .nloop
.ndone:
    mov byte [di], 0
    mov [br_astart], si             ; the attributes are what is between the
    call br_skiptag                 ; name and the '>' - forms are the first
    mov ax, si                      ; thing here that reads one
    dec ax
    mov [br_aend], ax
    call br_act
    jmp .out
.skip:
    mov byte [di], 0
    jmp .out
.bang:
    inc si
    cmp si, [br_srclen]
    jae .out
    cmp byte [es:si], '-'
    jne .decl
    call br_skipcomment
    jmp .out
.decl:
    call br_skiptag
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_skiptag - advance past the '>' , respecting quoted attributes --------
br_skiptag:
    push ax
    push bx
    xor bh, bh                      ; BH = the quote we are inside, 0 = none
.loop:
    cmp si, [br_srclen]
    jae .out
    mov al, [es:si]
    inc si
    or bh, bh
    jz .free
    cmp al, bh
    jne .loop
    xor bh, bh
    jmp .loop
.free:
    cmp al, '>'
    je .out
    cmp al, '"'
    je .q
    cmp al, 0x27
    je .q
    jmp .loop
.q:
    mov bh, al
    jmp .loop
.out:
    pop bx
    pop ax
    ret

br_skipcomment:
    push ax
.loop:
    cmp si, [br_srclen]
    jae .out
    mov al, [es:si]
    inc si
    cmp al, '>'
    jne .loop
    cmp si, 3
    jb .loop
    cmp byte [es:si-2], '-'
    jne .loop
    cmp byte [es:si-3], '-'
    jne .loop
.out:
    pop ax
    ret

; --- br_act - what the tag in br_tname does ----------------------------------
br_act:
    push ax
    push bx
    push cx
    push di
    ; SI IS NOT TOUCHED IN HERE, and that is load-bearing rather than tidy:
    ; SI is the parse's position in the SOURCE, and br_do's TA_DROP path calls
    ; br_skipelem, which advances it past a whole <script>/<style>/<title>
    ; element. The first version used SI as its cursor into br_tagtab and
    ; push/popped it, so the skip ran from a garbage offset and its advance
    ; was thrown away - which put the contents of <title> on the page.
    cmp byte [br_tname], 'h'        ; h1..h6 carry a level
    jne .table
    mov al, [br_tname+1]
    cmp al, '1'
    jb .table
    cmp al, '6'
    ja .table
    cmp byte [br_tname+2], 0
    jne .table
    cmp byte [br_close], 0
    jne .hclose
    call br_break
    push ax
    mov al, D_HEAD
    call br_put
    pop ax
    sub al, '0'
    call br_put
    jmp .out
.hclose:
    call br_break
    jmp .out
.table:
    mov bx, br_tagtab
.next:
    cmp byte [bx], 0
    je .out
    mov di, br_tname
.cmp:
    mov al, [bx]
    or al, al
    jz .end
    cmp al, [di]
    jne .skip
    inc bx
    inc di
    jmp .cmp
.end:
    cmp byte [di], 0
    jne .skip
    mov al, [bx+1]
    call br_do
    jmp .out
.skip:
    cmp byte [bx], 0                ; walk to this entry's NUL...
    je .fwd2
    inc bx
    jmp .skip
.fwd2:
    add bx, 2                       ; ...then past it and its action byte
    jmp .next
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

TA_PARA equ 1
TA_BR   equ 2
TA_HR   equ 3
TA_LI   equ 4
TA_PRE  equ 5
TA_CEN  equ 6
TA_DROP equ 7
TA_TAB  equ 8
TA_ROW  equ 9
TA_CELL equ 10
TA_FORM equ 11
TA_INPUT equ 12
TA_LINK  equ 13
TA_TITLE equ 14                 ; <title>: dropped from the PAGE like TA_DROP,
                                ; but read on the way past - it is what the
                                ; History menu labels this entry with

br_do:
    cmp al, TA_LINK
    jne .para
    call br_anchor
    ret
.para:
    cmp al, TA_PARA
    jne .br
    call br_break
    ret
.br:
    cmp al, TA_BR
    jne .hr
    mov al, D_BR
    call br_put
    mov byte [br_wsp], 0
    ret
.hr:
    cmp al, TA_HR
    jne .li
    call br_break
    mov al, D_HR
    call br_put
    ret
.li:
    cmp al, TA_LI
    jne .pre
    cmp byte [br_close], 0
    jne .no
    call br_break
    mov al, D_LI
    call br_put
    ret
.pre:
    cmp al, TA_PRE
    jne .cen
    call br_break
    mov al, D_PRE1
    cmp byte [br_close], 0
    je .p1
    mov al, D_PRE0
.p1:
    mov ah, al
    sub ah, D_PRE1
    mov byte [br_pre], 1
    or ah, ah
    jz .p2
    mov byte [br_pre], 0
.p2:
    call br_put
    ret
.cen:
    cmp al, TA_CEN
    jne .drop
    call br_break
    mov al, D_CEN1
    cmp byte [br_close], 0
    je .c1
    mov al, D_CEN0
.c1:
    call br_put
    ret
.drop:
    cmp al, TA_TITLE
    jne .drop2
    cmp byte [br_close], 0
    jne .no
    call br_captitle                ; ...read it BEFORE throwing it away
    call br_skipelem
    ret
.drop2:
    cmp al, TA_DROP
    jne .tab
    cmp byte [br_close], 0
    jne .no
    call br_skipelem                ; the CONTENT goes too, and it contains
    ret                             ; markup on purpose (torture.htm)
.tab:
    cmp al, TA_TAB
    jne .row
    call br_break
    mov al, D_TAB1
    cmp byte [br_close], 0
    je .t1
    mov al, D_TAB0
.t1:
    call br_put
    ret
.row:
    cmp al, TA_ROW
    jne .cell
    cmp byte [br_close], 0
    jne .no
    call br_break
    mov al, D_ROW
    call br_put
    ret
.cell:
    cmp al, TA_CELL
    jne .form
    cmp byte [br_close], 0
    jne .no
    call br_break
    mov al, D_CELL
    call br_put
    ret
.form:
    cmp al, TA_FORM
    jne .input
    call br_break
    call br_form
    ret
.input:
    cmp al, TA_INPUT
    jne .no
    call br_input
.no:
    ret

; --- br_skipelem - swallow to the matching close tag -------------------------
br_skipelem:
    push ax
    push bx
    push di
.loop:
    cmp si, [br_srclen]
    jae .out
    mov al, [es:si]
    inc si
    cmp al, '<'
    jne .loop
    cmp si, [br_srclen]
    jae .out
    cmp byte [es:si], '/'
    jne .loop
    inc si
    mov di, br_tname
.cmp:
    mov al, [di]
    or al, al
    jz .end
    cmp si, [br_srclen]
    jae .out
    mov ah, [es:si]
    or ah, 0x20
    cmp ah, al
    jne .loop
    inc si
    inc di
    jmp .cmp
.end:
    call br_skiptag
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_captitle - the text of <title> into br_ptitle
; in:  ES:SI = the source, just past the opening tag
; out: br_ptitle holds up to BR_TITMAX-1 characters, NUL-terminated;
;      all registers preserved, SI included - br_skipelem does the walking
;
; Whitespace is COLLAPSED and the ends trimmed, because a title is very often
; laid out across three lines of source and the History menu has room for
; about thirty characters. It stops at the first '<' rather than parsing:
; anything else inside <title> is not markup a browser should be rendering,
; and br_skipelem is still what decides where the element ends.
; -----------------------------------------------------------------------------
br_captitle:
    push ax
    push cx
    push si
    push di
    xor cx, cx
    mov di, br_ptitle
    mov ah, 1                       ; 1 = the last thing emitted was a space,
.loop:                              ; so the next run of them is swallowed
    cmp si, [br_srclen]
    jae .done
    mov al, [es:si]
    cmp al, '<'
    je .done
    inc si
    cmp al, ' '
    ja .put
    mov al, ' '                     ; a newline, a tab and every other control
    or ah, ah                       ; byte is one space, and a run is one
    jnz .loop
    mov ah, 1
    jmp short .store
.put:
    xor ah, ah
.store:
    cmp cx, BR_TITMAX - 1
    jae .done
    mov [di], al
    inc di
    inc cx
    jmp .loop
.done:
    or cx, cx
    jz .nul
    or ah, ah                       ; ...and one trailing space, which the
    jz .nul                         ; collapse above cannot know is trailing
    dec di                          ; until it gets here
.nul:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- br_break - end the current block ----------------------------------------
br_break:
    push ax
    cmp byte [br_any], 0
    je .out                         ; nothing in this block: no empty paragraph
    mov al, D_PARA
    call br_put
    mov byte [br_any], 0
    mov byte [br_wsp], 0
.out:
    pop ax
    ret

; --- br_put / br_flushbuf - append one byte to the document ------------------
br_put:
    push bx
    mov bl, [br_on]
    xor bh, bh
    cmp bl, 250
    jb .room
    push ax
    call br_flushbuf
    pop ax
    xor bx, bx
.room:
    mov [bx+br_obuf], al
    inc byte [br_on]
    pop bx
    ret

br_flushbuf:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov cl, [br_on]
    xor ch, ch
    jcxz .out
    mov ax, [br_doclen]
    add ax, cx
    mov bx, [br_dockb]
    push cx
    mov cl, 10
    shl bx, cl
    pop cx
    cmp ax, bx
    jae .full                       ; BOUNDED AT THE COPY (SPEC.md 69.7)
    mov es, [br_docseg]
    mov di, [br_doclen]
    add [br_doclen], cx             ; BEFORE the rep, not after: `rep movsb`
    mov si, br_obuf                 ; leaves CX at ZERO, so accounting for the
    cld                             ; copy afterwards adds nothing and every
    rep movsb                       ; flush rewrites offset 0
.out:
    mov byte [br_on], 0
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
.full:
    mov byte [br_trunc], 1
    jmp .out

; =============================================================================
; THE LAYOUT (BROWSER-PLAN 2.3)
; =============================================================================
; One pass over the document stream producing the line table. It is NOT on
; the hot path and does not need Note Pad's bounded walk: a browser lays out
; once per load and once per width change, where Note Pad re-walks on every
; keystroke. Measured by tools/htmsim.py, a 10KB page is ~200 lines, and the
; scan is a byte compare each - tens of milliseconds, once.
; -----------------------------------------------------------------------------
br_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [br_nlines], 0
    cmp word [br_docseg], 0
    je .out
    mov word [br_lstart], 0
    mov word [br_nch], 0
    mov word [br_bpos], 0
    mov word [br_bch], 0
    mov byte [br_lcen], 0
    mov byte [br_lpre], 0
    mov byte [br_lind], 0
    mov word [br_lnkopen], 0FFFFh   ; the LAYOUT's copy of the link state, which
    mov word [br_lnkst], 0FFFFh     ; is a different walk from the parse's
    mov ax, [br_doclen]             ; the composed-row arena starts past the
    inc ax                          ; document and is rebuilt with the layout
    mov [br_comp], ax
    mov es, [br_docseg]
    xor si, si
.loop:
    cmp si, [br_doclen]
    jae .fin
    mov al, [es:si]
    cmp al, 0x20
    jb .marker
    inc si
    cmp word [br_nch], 0
    jne .count
    cmp al, ' '
    jne .count
    mov [br_lstart], si             ; a leading space never starts a line
    jmp .loop
.count:
    inc word [br_nch]
    cmp al, ' '
    jne .over
    cmp byte [br_lpre], 0
    jne .over
    mov [br_bpos], si               ; a breakpoint: just past the space
    mov ax, [br_nch]
    mov [br_bch], ax
.over:
    mov ax, [br_nch]
    cmp ax, [br_cols]
    jbe .loop
    cmp byte [br_lpre], 0
    jne .loop                       ; PRE never wraps: it runs off the band
    mov ax, [br_bpos]
    cmp ax, [br_lstart]
    jbe .hard
                                    ; break at the last space
    mov cx, [br_bch]
    dec cx                          ; cells, excluding the space itself
    mov bx, ax
    dec bx                          ; end = just before the space
    call br_emitline
    mov ax, [br_bpos]
    mov [br_lstart], ax
    mov cx, [br_nch]
    sub cx, [br_bch]
    mov [br_nch], cx
    mov word [br_bpos], 0
    jmp .loop
.hard:
    mov bx, si                      ; a word longer than the band: split it
    dec bx
    mov cx, [br_cols]
    call br_emitline
    mov [br_lstart], bx
    mov word [br_nch], 1
    mov word [br_bpos], 0
    jmp .loop
.marker:
    cmp al, D_LNK1                  ; **THE TWO INLINE MARKERS COME FIRST AND
    je .mlnk1                       ; END NO LINE.** Every other marker in this
    cmp al, D_LNK0                  ; format is a break and the emitline below
    je .mlnk0                       ; is why; a link is a run of ordinary text
    mov bx, si                      ; with a bracket either side, so putting
    mov cx, [br_nch]                ; these through that path would break the
    call br_emitline                ; sentence at every anchor
    inc si
    cmp al, D_END
    je .fin
    cmp al, D_PARA
    je .mpara
    cmp al, D_BR
    je .mrst
    cmp al, D_HR
    je .mhr
    cmp al, D_LI
    je .mli
    cmp al, D_HEAD
    je .mhead
    cmp al, D_CEN1
    je .mcen1
    cmp al, D_CEN0
    je .mcen0
    cmp al, D_PRE1
    je .mpre1
    cmp al, D_PRE0
    je .mpre0
    cmp al, D_TAB1
    je .mtab
    jmp .mrst                       ; D_ROW / D_CELL / D_TAB0 reached OUTSIDE
                                    ; br_table means the table was inlined as
                                    ; a box (3.2.1): they are line breaks
.mlnk1:
    add si, 3                       ; the marker and its two payload bytes
    mov word [br_lnkopen], 1        ; WHICH link is read at hit time; all the
    jmp .loop                       ; layout needs is "open". And .loop, NOT
                                    ; .mrst: nothing was emitted and the
                                    ; pending line is still pending
.mlnk0:
    inc si
    mov word [br_lnkopen], 0FFFFh
    jmp .loop
.mtab:
    call br_table                   ; SI = the body. CF=1 = inline it, and
    jmp .mrst                       ; either way the walk resumes at SI
.mpara:
    call br_blank
    jmp .mrst
.mhead:
    inc si                          ; the level byte
    call br_blank
    jmp .mrst
.mhr:
    call br_emitrule
    jmp .mrst
.mli:
    mov byte [br_lind], 2
    jmp .mrst
.mcen1:
    mov byte [br_lcen], 1
    jmp .mrst
.mcen0:
    mov byte [br_lcen], 0
    jmp .mrst
.mpre1:
    mov byte [br_lpre], 1
    jmp .mrst
.mpre0:
    mov byte [br_lpre], 0
    jmp .mrst
.mrst:
    mov [br_lstart], si
    mov word [br_nch], 0
    mov word [br_bpos], 0
    jmp .loop
.fin:
    mov bx, si
    mov cx, [br_nch]
    call br_emitline
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_emitline - record [br_lstart, BX) as one display line ----------------
; in: BX = end offset, CX = cells of text on it
br_emitline:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    or cx, cx
    jz .out                         ; an empty line is not a line
    call br_lslot                   ; ES:DI = the slot, CF=1 if full
    jc .out
    mov ax, [br_lstart]
    mov [es:di+LN_OFS], ax
    mov [es:di+LN_END], bx
    xor al, al
    cmp byte [br_lcen], 0
    je .ind
    mov ax, [br_cols]               ; centred: pad to the middle
    sub ax, cx
    jns .half
    xor ax, ax
.half:
    shr ax, 1
    jmp .setx
.ind:
    mov al, [br_lind]
    xor ah, ah
.setx:
    mov [es:di+LN_X], al
    mov byte [es:di+LN_FL], 0
    cmp word [br_lnkst], 0FFFFh     ; ...DID THIS LINE BEGIN INSIDE A LINK?
    je .nolnk                       ; The underline needs it and the line's own
    or byte [es:di+LN_FL], LNF_LNK  ; span may hold no D_LNK1 at all, the
.nolnk:                             ; anchor having opened lines above
    inc word [br_nlines]
    mov byte [br_lind], 0           ; an indent belongs to ONE line: the
.out:                               ; bullet's, not the whole item's
    mov ax, [br_lnkopen]            ; ...and the NEXT line starts wherever this
    mov [br_lnkst], ax              ; one left the link state. One assignment,
                                    ; in the one routine every line comes
                                    ; through, rather than a copy at each of
                                    ; the four places br_lstart moves
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_blank / br_emitrule - the two lines with no text in them -------------
br_blank:
    push ax
    push di
    push es
    cmp word [br_nlines], 0
    je .out                         ; never lead the document with a blank
    call br_lslot
    jc .out
    mov word [es:di+LN_OFS], 0
    mov word [es:di+LN_END], 0
    mov byte [es:di+LN_X], 0
    mov byte [es:di+LN_FL], 0
    inc word [br_nlines]
.out:
    pop es
    pop di
    pop ax
    ret

br_emitrule:
    push ax
    push di
    push es
    call br_lslot
    jc .out
    mov word [es:di+LN_OFS], 0
    mov word [es:di+LN_END], 0
    mov byte [es:di+LN_X], 0
    mov byte [es:di+LN_FL], LNF_RULE
    inc word [br_nlines]
.out:
    pop es
    pop di
    pop ax
    ret

; --- br_lslot - ES:DI = the next free line slot; CF=1 when the table is full --
br_lslot:
    push ax
    push dx
    mov ax, [br_nlines]
    mov dx, LN_SIZE
    mul dx
    cmp ax, BR_LINEKB*1024 - LN_SIZE
    jae .full
    mov di, ax
    mov es, [br_lseg]
    clc
    jmp .out
.full:
    stc
.out:
    pop dx
    pop ax
    ret

; =============================================================================
; TABLES (BROWSER-PLAN 3.2)
; =============================================================================
; A table is the ATOMIC unit of layout: column widths depend on every row, so
; it cannot be laid out incrementally top-down the way prose can. One
; MEASURING pass over the parsed cells - integer arithmetic over lengths, not
; one glyph - then the rows.
;
; A row draws from SEVERAL cells at once, which the line table cannot describe:
; an entry is one contiguous span of the document. So a table's rows are
; COMPOSED into bytes appended past D_END in the document claim, and the line
; entries point at those. No new painter path, no third claim, and the whole
; thing is rebuilt by br_layout on a width change like everything else.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; br_table - lay out the table whose body starts at SI
; in:  SI = just past D_TAB1, ES = docseg
; out: CF=0 the table was emitted and SI is past its D_TAB0
;      CF=1 INLINE IT - 3.2.1 says this is a box, not a table; SI unchanged
;           and the caller lays the body out as ordinary content
; -----------------------------------------------------------------------------
br_table:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    call br_tmeas                   ; widths, ncols, cells, any-text
    pop si
    push si
    cmp byte [br_tany], 0
    je .decor                       ; no text in any cell: decoration
    cmp word [br_tcells], 1
    jbe .box                        ; 1x1: a box, not a table
    cmp byte [br_tn], 2
    jb .box
    call br_tfit                    ; squeeze the columns into the band
    call br_temit                   ; ...and put the rows out
    pop si
    call br_tskip                   ; SI past the matching D_TAB0
    clc
    jmp .out
.decor:
    pop si
    call br_tskip
    clc
    jmp .out
.box:
    pop si
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_tmeas - one measuring pass: how wide does each column want to be?
; in:  SI = table body start
; out: [br_tn] = columns (capped), [br_tw] = wanted widths, [br_tflr] = the
;      longest WORD in each (a column squeezed below it splits words - the
;      model found that one, BROWSER-PLAN 2.5), [br_tcells], [br_tany]
; -----------------------------------------------------------------------------
br_tmeas:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    mov cx, BR_TCOLS
    mov di, br_tw
    push es
    push ds
    pop es
    xor al, al
    cld
    rep stosb
    mov cx, BR_TCOLS
    mov di, br_tflr
    rep stosb
    pop es
    mov byte [br_tn], 0
    mov byte [br_tany], 0
    mov word [br_tcells], 0
    mov byte [br_tcol], 0FFh        ; no cell open yet
    xor bx, bx                      ; BX = this cell's running length
    xor dx, dx                      ; DX = this cell's running WORD length
    xor cx, cx                      ; CX = nested-table depth
.loop:
    cmp si, [br_doclen]
    jae .done
    mov al, [es:si]
    inc si
    cmp al, 0x20
    jae .text
    cmp al, D_TAB1
    jne .t0
    inc cx                          ; a nested table FLATTENS into this cell
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .row
    jcxz .done
    dec cx
    jmp .loop
.row:
    or cx, cx
    jnz .loop                       ; inside a nested table: not our structure
    cmp al, D_ROW
    jne .cell
    call br_tcellend
    mov byte [br_tcol], 0FFh
    xor bx, bx
    xor dx, dx
    jmp .loop
.cell:
    cmp al, D_CELL
    jne .loop
    call br_tcellend
    inc word [br_tcells]
    inc byte [br_tcol]
    xor bx, bx
    xor dx, dx
    jmp .loop
.text:
    or cx, cx
    jnz .txt2
.txt2:
    cmp byte [br_tcol], 0FFh
    je .loop                        ; text outside any cell: not ours
    inc bx
    cmp al, ' '
    je .wsp
    inc dx
    mov byte [br_tany], 1
    jmp .loop
.wsp:
    call br_twordend
    xor dx, dx
    jmp .loop
.done:
    call br_tcellend
    mov al, [br_tn]
    cmp al, BR_TCOLS
    jbe .cap
    mov byte [br_tn], BR_TCOLS
.cap:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tcellend - fold BX (length) and DX (word) into this column -----------
br_tcellend:
    push ax
    push di
    call br_twordend
    mov al, [br_tcol]
    cmp al, 0FFh
    je .out
    cmp al, BR_TCOLS
    jae .out
    xor ah, ah
    mov di, ax
    inc al
    cmp al, [br_tn]
    jbe .w
    mov [br_tn], al                 ; the widest ROW decides the count
.w:
    cmp bx, 255
    jbe .w2
    mov bx, 255
.w2:
    cmp bl, [di+br_tw]
    jbe .out
    mov [di+br_tw], bl
.out:
    pop di
    pop ax
    ret

; --- br_twordend - fold DX (a word's length) into this column's floor --------
br_twordend:
    push ax
    push di
    or dx, dx
    jz .out
    mov al, [br_tcol]
    cmp al, 0FFh
    je .out
    cmp al, BR_TCOLS
    jae .out
    xor ah, ah
    mov di, ax
    cmp dx, BR_TFLRMAX
    jbe .f
    mov dx, BR_TFLRMAX              ; a very long word splits; it does not get
.f:                                 ; to reserve the whole band for itself
    cmp dl, [di+br_tflr]
    jbe .out
    mov [di+br_tflr], dl
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_tfit - squeeze the wanted widths into the band
; The drawn width is sum(w) + 2*ncols + 1 - a '|' and a space either side of
; every column, and a '+' at each edge - so that is what has to fit, not the
; sum alone.
; -----------------------------------------------------------------------------
br_tfit:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cl, [br_tn]
    xor ch, ch
    or cx, cx                       ; jcxz is short-only on an 8086 and .out
    jnz .go                         ; is past its reach
    jmp .out
.go:
    mov ax, [br_cols]               ; avail = cols - 2n - 1
    mov bx, cx
    shl bx, 1
    inc bx
    sub ax, bx
    jns .a
    mov ax, 1
.a:
    mov [br_tavail], ax
    xor bx, bx                      ; BX = sum of wanted
    xor di, di
.sum:
    mov al, [di+br_tw]
    xor ah, ah
    add bx, ax
    inc di
    cmp di, cx
    jb .sum
    cmp bx, [br_tavail]
    jbe .out                        ; it already fits: nothing to squeeze
    or bx, bx
    jz .out
                                    ; proportional, then lifted back to each
                                    ; column's longest WORD where there is room
    xor di, di
.sq:
    mov al, [di+br_tw]
    xor ah, ah
    mul word [br_tavail]
    div bx                          ; w * avail / sum
    or ax, ax
    jnz .s1
    mov ax, 1
.s1:
    cmp al, [di+br_tflr]
    jae .s2
    mov al, [di+br_tflr]            ; never squeeze THROUGH a word
.s2:
    mov [di+br_tw], al
    inc di
    cmp di, cx
    jb .sq
                                    ; give any excess back from the widest
.trim:
    xor bx, bx
    xor di, di
    mov dx, 0                       ; DX = index of the widest
.t1:
    mov al, [di+br_tw]
    xor ah, ah
    add bx, ax
    inc di
    cmp di, cx
    jb .t1
    cmp bx, [br_tavail]
    jbe .out
    xor di, di
    xor dx, dx
    mov ah, 0
.t2:
    mov al, [di+br_tw]
    cmp al, ah
    jbe .t3
    mov ah, al
    mov dx, di
.t3:
    inc di
    cmp di, cx
    jb .t2
    cmp ah, 2
    jbe .out                        ; cannot shrink further: let it overflow
    mov di, dx
    dec byte [di+br_tw]
    jmp .trim
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_temit - compose and emit the table's lines
; in:  SI = table body start
; -----------------------------------------------------------------------------
br_temit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_trule                   ; the top edge
.rows:
    call br_tfindrow                ; SI -> just past the next D_ROW, CF=1 none
    jc .out
    call br_tcells_of               ; fill br_cs/br_ce from this row
    call br_trowout                 ; ...and put its wrapped lines out
    call br_trule
    jmp .rows
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tfindrow - advance SI to just past the next D_ROW at depth 0 ---------
br_tfindrow:
    push ax
    push cx
    xor cx, cx
.loop:
    cmp si, [br_doclen]
    jae .none
    mov al, [es:si]
    inc si
    cmp al, D_TAB1
    jne .t0
    inc cx
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .r
    jcxz .none
    dec cx
    jmp .loop
.r:
    cmp al, D_ROW
    jne .loop
    or cx, cx
    jnz .loop
    clc
    jmp .out
.none:
    stc
.out:
    pop cx
    pop ax
    ret

; --- br_tcells_of - record this row's cell spans into br_cs / br_ce ----------
; in:  SI just past a D_ROW; out: SI at the row's end, [br_tnc] = cells found
br_tcells_of:
    push ax
    push bx
    push cx
    push di
    mov byte [br_tnc], 0
    mov di, 0FFFFh                  ; DI = the open cell's index, FFFF = none
    xor cx, cx
.loop:
    cmp si, [br_doclen]
    jae .done
    mov al, [es:si]
    cmp al, D_TAB1
    jne .t0
    inc cx
    inc si
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .row
    jcxz .done
    dec cx
    inc si
    jmp .loop
.row:
    or cx, cx
    jnz .adv
    cmp al, D_ROW
    je .done
    cmp al, D_CELL
    jne .adv
    call br_tclose
    mov al, [br_tnc]
    cmp al, BR_TCOLS
    jae .adv2
    xor ah, ah
    mov di, ax
    shl di, 1
    inc si
    mov [di+br_cs], si
    mov [di+br_ce], si
    inc byte [br_tnc]
    jmp .loop
.adv2:
    mov di, 0FFFFh
.adv:
    inc si
    jmp .loop
.done:
    call br_tclose
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- br_tclose - the open cell (if any) ends at SI --------------------------
br_tclose:
    push ax
    push bx
    cmp di, 0FFFFh
    je .out
    mov bx, di
    mov [bx+br_ce], si
.out:
    pop bx
    pop ax
    ret

; --- br_trowout - wrap every cell and emit the row's display lines -----------
br_trowout:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cl, [br_tnc]
    xor ch, ch
    or cx, cx
    jnz .go
    jmp .out
.go:
    xor di, di                      ; copy the spans into the wrap cursors
.init:
    mov bx, di
    shl bx, 1
    mov ax, [bx+br_cs]
    mov [bx+br_tcur], ax
    inc di
    cmp di, cx
    jb .init
.line:
    call br_tcompose                ; CF=1 when every cell is exhausted
    jc .out
    jmp .line
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tcompose - one display line of the current row -----------------------
; out: CF=1 if there was nothing left to draw
br_tcompose:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    call br_cstart                  ; DI = the composed line's start
    mov al, '|'
    call br_cput
    xor cx, cx                      ; CX = column index
    xor dx, dx                      ; DX = did any cell contribute?
.col:
    cmp cl, [br_tn]
    jae .end
    mov al, ' '
    call br_cput
    call br_tcell1                  ; one cell's next fragment, padded
    or dx, ax
    mov al, '|'
    call br_cput
    inc cx
    jmp .col
.end:
    or dx, dx
    jz .none
    call br_cend                    ; emit the line entry
    clc
    jmp .out
.none:
    call br_cdrop                   ; nothing on it: give the bytes back
    stc
.out:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tcell1 - the next fragment of column CX, padded to its width --------
; out: AX = 1 if this cell contributed any character
br_tcell1:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, cx
    mov al, [bx+br_tw]
    xor ah, ah
    mov di, ax                      ; DI = the column's width
    xor dx, dx                      ; DX = characters placed
    cmp cl, [br_tnc]
    jae .pad                        ; a short row: this column is just padding
    shl bx, 1
    mov si, [bx+br_tcur]
    mov ax, [bx+br_ce]
.skip:
    cmp si, ax                      ; leading spaces never start a fragment
    jae .store
    push ax
    mov al, [es:si]
    cmp al, ' '
    pop ax
    ja .go
    inc si
    jmp .skip
.go:
    push bx
    call br_twrap                   ; SI advanced, CX = placed
    mov dx, cx
    pop bx
.store:
    mov [bx+br_tcur], si
.pad:
    mov cx, dx                      ; pad from the REAL count up to the width,
.padloop:                           ; counting separately - DX must stay the
    cmp cx, di                      ; number of characters this cell actually
    jae .done                       ; contributed, or an exhausted row of empty
    mov al, ' '                     ; cells reports itself as non-empty and
    call br_cput                    ; br_trowout composes blank rows for ever
    inc cx
    jmp .padloop
.done:
    mov ax, 0
    or dx, dx
    jz .out
    mov ax, 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- br_twrap - take up to DI characters of ES:SI, breaking on a space -------
; in:  SI = cursor, AX = end, DI = width; out: SI advanced, CX = placed
;
; TWO PASSES on purpose. The first decides how many characters this fragment
; gets and emits nothing; only then are they put out. A single pass cannot
; back up: by the time it knows the word will not fit it has already composed
; half of it, and there is no un-emitting from an append-only arena.
br_twrap:
    push ax
    push bx
    push dx
                                    ; SI IS AN OUTPUT - the advanced cursor -
                                    ; so it is deliberately not saved. Pushing
                                    ; it here rewinds the cell on every call,
                                    ; so the cell never empties, always reports
                                    ; that it contributed, and the row composes
                                    ; until the arena is full
    xor cx, cx                      ; CX = characters scanned
    xor bx, bx                      ; BX = count at the last space, 0 = none
    mov dx, si
.scan:
    cmp si, ax
    jae .take
    cmp cx, di
    jae .full
    push ax
    mov al, [es:si]
    cmp al, 0x20
    jb .adv                         ; a marker inside a cell is not a cell
    inc cx
    cmp al, ' '
    jne .adv
    mov bx, cx                      ; ...a place we could break
.adv:
    pop ax
    inc si
    jmp .scan
.full:
    or bx, bx
    jz .take                        ; one long word: hard split at the width
    mov cx, bx
    dec cx                          ; ...excluding the space itself
.take:
    mov si, dx                      ; rewind and emit exactly CX characters
    xor dx, dx
.emit:
    cmp dx, cx
    jae .skiptail
    cmp si, ax
    jae .skiptail
    push ax
    mov al, [es:si]
    cmp al, 0x20
    jb .e2
    call br_cput
    inc dx
.e2:
    pop ax
    inc si
    jmp .emit
.skiptail:
    cmp si, ax                      ; step over the space we broke at
    jae .out
    push ax
    mov al, [es:si]
    cmp al, ' '
    pop ax
    jne .out
    inc si
.out:
    mov cx, dx
    pop dx
    pop bx
    pop ax
    ret

; --- br_trule - the +---+---+ edge ------------------------------------------
br_trule:
    push ax
    push bx
    push cx
    push dx
    call br_cstart
    mov al, '+'
    call br_cput
    xor cx, cx
.col:
    cmp cl, [br_tn]
    jae .end
    mov bx, cx
    mov dl, [bx+br_tw]
    inc dl                          ; the leading space belongs to the cell
    xor dh, dh
.bar:
    or dx, dx
    jz .plus
    mov al, '-'
    call br_cput
    dec dx
    jmp .bar
.plus:
    mov al, '+'
    call br_cput
    inc cx
    jmp .col
.end:
    call br_cend
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the composed-text arena - bytes appended past D_END in the document claim
; -----------------------------------------------------------------------------
br_cstart:
    push ax
    mov ax, [br_comp]
    mov [br_cline], ax
    pop ax
    ret

br_cput:
    push bx
    push cx
    push di
    mov di, [br_comp]
    mov bx, [br_dockb]
    mov cl, 10
    shl bx, cl
    cmp di, bx
    jae .full                       ; BOUNDED AT THE COPY (SPEC.md 69.7)
    mov [es:di], al
    inc word [br_comp]
.full:
    pop di
    pop cx
    pop bx
    ret

; --- br_cend - turn the composed bytes into a line entry ---------------------
; Writes the record itself rather than going through br_emitline: a composed
; line owes nothing to [br_lstart], [br_lcen] or [br_lind], and borrowing that
; routine would mean saving and restoring three pieces of the prose walk's
; state around every table row.
br_cend:
    push ax
    push bx
    push di
    push es
    mov ax, [br_cline]
    mov bx, [br_comp]
    cmp bx, ax
    jbe .out
    call br_lslot                   ; ES:DI = the slot (ES becomes lseg)
    jc .out
    mov [es:di+LN_OFS], ax
    mov [es:di+LN_END], bx
    mov byte [es:di+LN_X], 0
    mov byte [es:di+LN_FL], 0
    inc word [br_nlines]
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret

br_cdrop:
    push ax
    mov ax, [br_cline]
    mov [br_comp], ax
    pop ax
    ret

; --- br_tskip - SI past this table's matching D_TAB0 ------------------------
br_tskip:
    push ax
    push cx
    xor cx, cx
.loop:
    cmp si, [br_doclen]
    jae .out
    mov al, [es:si]
    inc si
    cmp al, D_TAB1
    jne .t0
    inc cx
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .loop
    jcxz .out
    dec cx
    jmp .loop
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; FORMS (BROWSER-PLAN 7)
; =============================================================================
; One text input and one submit button is what a search box is, and that is
; the whole of what this implements. FrogFind is the first real site and it is
; exactly that shape (1.1).
;
; A FIELD IS A FIXED SPAN OF THE DOCUMENT. The parse emits '[', W spaces and
; ']' as ordinary text, and typing rewrites those bytes in place - so the
; field wraps with the prose, scrolls with it, and the painter needs no new
; path at all. It is the same trick the composed table rows use, from the
; other direction.
;
; Focus is shown by the CARET being present in the field rather than by an
; inversion. An XOR overlay would have to be taken off before every blit and
; put back after (SPEC.md 48.11's crosshair), which is real machinery for a
; v1 that has one field on the page.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; br_anchor - <a href=...> and </a>  (BROWSER-PLAN 6)
; in:  [br_close] says which; ES = srcseg, the tag's attribute span current
; out: nothing; all registers preserved
;
; The markers go into the document INLINE and end no line: a link is a run of
; ordinary text with a bracket either side of it, which is what lets it wrap,
; centre, sit in a table cell and scroll with everything else. br_layout's
; .marker path had to learn that, because every other marker in this format
; ends the pending line and these two must not.
;
; An anchor with no href is TEXT: <a name="top"> is a bookmark, and drawing it
; underlined would promise a click that goes nowhere (SPEC.md 47 rule 4's
; shape - a control that looks live and is not).
; -----------------------------------------------------------------------------
br_anchor:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [br_close], 0
    je .open
    cmp word [br_lnkopen], 0FFFFh
    je .out                         ; a stray </a>: nothing to close
    mov word [br_lnkopen], 0FFFFh
    mov al, D_LNK0
    call br_put
    jmp .out
.open:
    cmp word [br_lnkopen], 0FFFFh
    jne .out                        ; nested <a>: the outer run keeps the link
    cmp word [br_lnkseg], 0
    je .out
    mov ax, [br_lnkn]
    cmp ax, BR_LNKMAX
    jae .out                        ; full: ordinary text from here on
    mov di, br_a_href
    call br_getattr                 ; CF=1 = no href, so <a name=...> is TEXT
    jc .out                         ; and promises no click
    mov bx, cx
    sub bx, si                      ; BX = the href's length
    jz .out
    mov ax, [br_lnkw]
    add ax, bx
    inc ax
    cmp ax, BR_LINKKB*1024
    jae .out                        ; the arena is full: text, not a link
    mov di, [br_lnkw]               ; DI = where this href lands...
    mov ax, [br_lnkn]
    mov dx, ax
    shl dx, 1
    push bx
    mov bx, dx
    mov [br_lnkoff+bx], di          ; ...RECORDED BEFORE THE COPY, because the
    pop bx                          ; copy is what moves the cursor
    mov cx, bx                      ; CX = the count rep movsb wants
    mov dx, [br_lnkseg]
    mov ax, [br_srcseg]             ; ...read while DS is still ours
    push ds
    mov es, dx                      ; **BOTH ENDS ARE FOREIGN SEGMENTS**, so
    mov ds, ax                      ; this is the one place in the file that
    cld                             ; moves DS: a movs needs DS:SI and ES:DI at
    rep movsb                       ; once and neither of them is ours. Nothing
    xor al, al                      ; between here and the pop may name a
    stosb                           ; [br_*] - it would read the SOURCE
    pop ds
    mov [br_lnkw], di
    mov es, [br_srcseg]             ; ...and ES back to what br_act passes round
    mov ax, [br_lnkn]
    mov [br_lnkopen], ax
    inc word [br_lnkn]
    call br_wflush                  ; **BEFORE THE MARKER**: a space spent
                                    ; after it lands inside this link's span
                                    ; and br_underline rules through the gap
    mov al, D_LNK1                  ; the marker and its two payload bytes
    call br_put
    mov ax, [br_lnkopen]            ; the index as two bytes in 16..31 - see
    and al, 15                      ; D_LNK1's own comment for why that range
    or al, 0x10
    call br_put
    mov ax, [br_lnkopen]
    mov cl, 4
    shr ax, cl
    and al, 15
    or al, 0x10
    call br_put
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_getattr - the value of attribute DS:DI within the current tag --------
; in:  DI = a NUL, lowercase attribute name; [br_astart]/[br_aend] = the tag's
;      attribute span in the SOURCE; ES = srcseg
; out: CF=0 and SI/CX = the value's span in the source, CF=1 not present
; -----------------------------------------------------------------------------
br_getattr:
    push ax
    push bx
    push dx
    mov si, [br_astart]
.next:
    cmp si, [br_aend]
    jae .none
    mov al, [es:si]
    cmp al, ' '                     ; attributes are separated by whitespace
    ja .try
    inc si
    jmp .next
.try:
    mov bx, si
    mov dx, di                      ; DX = the wanted name
.cmp:
    mov al, [es:si]
    cmp al, 'A'
    jb .c2
    cmp al, 'Z'
    ja .c2
    or al, 0x20                     ; attribute names are case-insensitive
.c2:
    cmp al, [di]
    jne .skip
    or al, al
    jz .skip
    inc si
    inc di
    cmp si, [br_aend]
    jb .cmp
.skip:
    cmp byte [di], 0                ; the whole name matched?
    jne .adv
    mov al, [es:si]
    cmp al, '='
    jne .adv
    inc si
    mov di, dx
    jmp .value
.adv:
    mov di, dx
    mov si, bx
.eat:
    cmp si, [br_aend]
    jae .none
    mov al, [es:si]
    inc si
    cmp al, ' '
    ja .eat
    jmp .next
.value:
    mov cx, si                      ; CX will be the END of the value
    cmp si, [br_aend]
    jae .empty
    mov al, [es:si]
    cmp al, '"'
    je .quoted
    cmp al, 0x27
    je .quoted
.bare:
    mov bx, si                      ; BX, not CX: only BX/BP/SI/DI address
.b2:                                ; memory on an 8086
    cmp bx, [br_aend]
    jae .dn
    mov al, [es:bx]
    cmp al, ' '
    jbe .dn
    cmp al, '>'
    je .dn
    inc bx
    jmp .b2
.quoted:
    mov ah, al
    inc si
    mov bx, si
.q2:
    cmp bx, [br_aend]
    jae .dn
    mov al, [es:bx]
    cmp al, ah
    je .dn
    inc bx
    jmp .q2
.dn:
    mov cx, bx
    clc
    jmp .out
.empty:
    mov cx, si
    clc
    jmp .out
.none:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; --- br_attreq - is attribute DI equal to the literal at BX? ------------------
; out: CF=0 equal
br_attreq:
    push ax
    push cx
    push si
    call br_getattr
    jc .no
.cmp:
    cmp si, cx
    jae .end
    mov al, [es:si]
    cmp al, 'A'
    jb .c2
    cmp al, 'Z'
    ja .c2
    or al, 0x20
.c2:
    cmp al, [bx]
    jne .no
    inc si
    inc bx
    jmp .cmp
.end:
    cmp byte [bx], 0
    jne .no
    clc
    jmp .out
.no:
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; --- br_form - <form ...> ----------------------------------------------------
br_form:
    push ax
    push bx
    push cx
    push di
    push si
    cmp byte [br_close], 0
    jne .out
    mov byte [br_fn], 0
    mov byte [br_hn], 0
    mov byte [br_post], 0
    mov di, br_a_action
    call br_getattr
    jc .noact
    mov di, br_faction
    mov bx, BR_ACTMAX-1
    call br_copyspan
.noact:
    mov di, br_a_method
    mov bx, br_s_post
    call br_attreq
    jc .out
    mov byte [br_post], 1           ; recorded, and 7.5 says why it is not
.out:                               ; acted on: the request builder's body
    pop si                          ; path is the one thing v1 leaves out
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- br_copyspan - the source span SI..CX -> DS:DI, at most BX bytes ---------
br_copyspan:
    push ax
    push cx
    push di
    push si
.loop:
    or bx, bx
    jz .end
    cmp si, cx
    jae .end
    mov al, [es:si]
    cmp al, 0x20                    ; never let a control byte into a URL
    jb .end
    mov [di], al
    inc di
    inc si
    dec bx
    jmp .loop
.end:
    mov byte [di], 0
    pop si
    pop di
    pop cx
    pop ax
    ret

; --- br_input - <input ...> ---------------------------------------------------
br_input:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    cmp byte [br_close], 0
    jne .out
    mov di, br_a_type
    mov bx, br_s_submit
    call br_attreq
    jnc .submit
    mov di, br_a_type
    mov bx, br_s_hidden
    call br_attreq
    jnc .hidden
    mov di, br_a_type
    mov bx, br_s_image              ; anything we do not draw is refused
    call br_attreq                  ; VISIBLY (SPEC.md 47 rule 3) rather than
    jnc .unsup                      ; vanishing
    mov di, br_a_type
    mov bx, br_s_check
    call br_attreq
    jnc .unsup
    jmp .text
.submit:
    mov ax, [br_doclen]             ; ...ITS SPAN, so a click can find it: the
    add ax, [br_onw]                ; button is ordinary document text and has
    cmp word [br_subend], 0         ; no record of its own otherwise. The FIRST
    jne .subdraw                    ; one on the page wins - v1 implements one
    mov [br_subofs], ax             ; form, and two would need a span each and
.subdraw:                           ; a form index on every field
    mov si, br_s_subbtn             ; '< Submit >' as ordinary document text
    call br_puts
    mov ax, [br_doclen]
    add ax, [br_onw]
    cmp word [br_subend], 0
    jne .out
    mov [br_subend], ax
    jmp .out
.unsup:
    mov si, br_s_unsup
    call br_puts
    jmp .out
.hidden:
    call br_hidden
    jmp .out
.text:
    mov al, [br_fn]
    cmp al, BR_FMAX
    jae .out                        ; more fields than we can hold: ignored
    xor ah, ah
    mov bx, BR_FLDSZ
    mul bl
    mov bx, ax                      ; BX = this field's record
    add bx, br_flds
    mov di, br_a_name               ; its name, for the query string
    call br_getattr
    jc .noname
    push bx
    add bx, FLD_NAME
    mov di, bx
    pop bx
    push bx
    mov bx, BR_NAMEMAX-1
    call br_copyspan
    pop bx
    jmp .size
.noname:
    mov byte [bx+FLD_NAME], 0
.size:
    mov dx, BR_FLDW                 ; a width, from `size` when it gives one
    mov di, br_a_size
    call br_getattr
    jc .w
    call br_span2num                ; AX = the number, CF=1 if it was not one
    jc .w
    or ax, ax
    jz .w
    cmp ax, BR_FLDWMAX
    jbe .w2
    mov ax, BR_FLDWMAX
.w2:
    mov dx, ax
.w:
    mov [bx+FLD_W], dl
    mov byte [bx+FLD_LEN], 0
                                    ; ...and now the visible span: '[', W
                                    ; spaces, ']' as literal document text
    push bx
    mov al, '['
    call br_fput
    mov ax, [br_doclen]             ; where the editable cells begin
    add ax, [br_onw]
    pop bx
    mov [bx+FLD_OFS], ax
    push bx
    mov cx, dx
    xor ch, ch
.sp:
    or cx, cx
    jz .close
    mov al, D_NBSP                  ; ...the same reasoning as the button's: a
                                    ; 30-cell field wrapped at its own spaces
                                    ; would put half a box on each line, and
                                    ; typing into it would look broken
    call br_fput
    dec cx
    jmp .sp
.close:
    mov al, ']'
    call br_fput
    pop bx
    inc byte [br_fn]
.out:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_fput - put one byte, and keep the running offset exact ---------------
; br_put buffers into br_obuf, so [br_doclen] lags by [br_on]. A field's
; recorded offset has to be where the byte LANDS, not where the flush last
; got to - that is what [br_onw] carries.
br_fput:
    call br_put
    push ax
    mov al, [br_on]
    xor ah, ah
    mov [br_onw], ax
    pop ax
    ret

; --- br_puts - a NUL string in our own segment, as document text ------------
br_puts:
    push ax
    push si
.loop:
    mov al, [si]
    or al, al
    jz .out
    call br_fput
    mov byte [br_any], 1
    inc si
    jmp .loop
.out:
    pop si
    pop ax
    ret

; --- br_hidden - a hidden pair, straight into the query string ---------------
br_hidden:
    push ax
    push bx
    push cx
    push di
    push si
    mov di, br_a_name
    call br_getattr
    jc .out
    mov al, [br_hn]
    or al, al
    jz .n
    mov al, '&'
    call br_hput
.n:
    call br_henc                    ; the name, percent-encoded
    mov al, '='
    call br_hput
    mov di, br_a_value
    call br_getattr
    jc .done
    call br_henc
.done:
    mov byte [br_hn], 1
.out:
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- br_henc - percent-encode the source span SI..CX into br_hid -------------
br_henc:
    push ax
    push si
.loop:
    cmp si, cx
    jae .out
    mov al, [es:si]
    inc si
    call br_encb
    jmp .loop
.out:
    pop si
    pop ax
    ret

; --- br_span2num - the source span SI..CX as a number ------------------------
br_span2num:
    push bx
    push si
    xor ax, ax
    mov bx, si
    cmp bx, cx
    jae .bad
.loop:
    cmp bx, cx
    jae .ok
    push cx
    mov cl, [es:bx]
    cmp cl, '0'
    jb .bad2
    cmp cl, '9'
    ja .bad2
    push dx
    mov dx, 10
    mul dx
    pop dx
    sub cl, '0'
    xor ch, ch
    add ax, cx
    pop cx
    inc bx
    jmp .loop
.bad2:
    pop cx
.bad:
    stc
    jmp .out
.ok:
    clc
.out:
    pop si
    pop bx
    ret

; --- the two append sinks, chosen by [br_dest] --------------------------------
; The percent-encoder is shared between the hidden pairs (built at parse time)
; and the query string (built at submit), so the ONE encoder has two
; destinations rather than there being two encoders that can disagree about
; what needs escaping.
br_hput:
    push bx
    mov bx, [br_hnw]
    cmp bx, BR_HIDMAX-1
    jae .out
    mov [bx+br_hid], al
    inc bx
    mov [br_hnw], bx
    mov byte [bx+br_hid], 0
.out:
    pop bx
    ret

br_uput:
    push bx
    mov bx, [br_un]
    cmp bx, BR_URLMAX-1
    jae .out
    mov [bx+br_url], al
    inc bx
    mov byte [bx+br_url], 0
    mov [br_un], bx
.out:
    pop bx
    ret

br_oput:
    cmp byte [br_dest], 0
    jne .u
    call br_hput
    ret
.u:
    call br_uput
    ret

; --- br_encb - one byte, percent-encoded (BROWSER-PLAN 7.4) ------------------
; An ALLOWLIST, never a denylist: everything outside A-Za-z0-9-_.~ becomes
; %XX and a space becomes '+'. A denylist misses a byte, and the byte it
; misses is the one that splits the request line.
br_encb:
    push ax
    push bx
    cmp al, ' '
    jne .safe
    mov al, '+'
    call br_oput
    jmp .out
.safe:
    cmp al, '0'
    jb .chk
    cmp al, '9'
    jbe .lit
    cmp al, 'A'
    jb .pct
    cmp al, 'Z'
    jbe .lit
    cmp al, 'a'
    jb .chk2
    cmp al, 'z'
    jbe .lit
    jmp .pct
.chk:
    cmp al, '-'
    je .lit
    cmp al, '.'
    je .lit
    jmp .pct
.chk2:
    cmp al, '_'
    je .lit
    jmp .pct
.lit:
    call br_oput
    jmp .out
.pct:
    mov bl, al
    mov al, '%'
    call br_oput
    mov al, bl
    mov cl, 4
    shr al, cl
    call br_hex
    call br_oput
    mov al, bl
    and al, 0x0F
    call br_hex
    call br_oput
.out:
    pop bx
    pop ax
    ret

br_hex:
    and al, 0x0F
    cmp al, 10
    jb .d
    add al, 'A'-10
    ret
.d:
    add al, '0'
    ret

; =============================================================================
; the fields, on screen
; =============================================================================

; --- br_fld - BX = the record of field AL ------------------------------------
br_fld:
    push ax
    xor ah, ah
    mov bx, BR_FLDSZ
    mul bl
    mov bx, ax
    add bx, br_flds
    pop ax
    ret

; --- br_fcaret - put AL at the caret cell of field BX ------------------------
; The field's cells ARE document bytes, so showing focus is one store. The
; CALLER chooses the character ('_' for the focused field, ' ' for any other):
; only it knows which is which, and deciding in here needs a second copy of
; that knowledge.
br_fcaret:
    push ax
    push bx
    push di
    push es
    mov es, [br_docseg]
    mov di, [bx+FLD_OFS]
    mov ah, [bx+FLD_LEN]
    cmp ah, [bx+FLD_W]
    jae .out                        ; full: there is no caret cell
    xor bh, bh
    mov bl, ah
    add di, bx
    mov [es:di], al
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret

; --- br_frepaint - redraw the display line field BX sits on -------------------
; Finding the line is a walk of the line table for the entry whose span holds
; the field's offset. A keystroke must not cost a band repaint: that is 1.24 s
; on this machine (BROWSER-PLAN 0).
br_frepaint:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    push es
    mov dx, [bx+FLD_OFS]
    mov es, [br_lseg]
    xor si, si
    xor cx, cx
.scan:
    cmp cx, [br_nlines]
    jae .out
    mov ax, [es:si+LN_OFS]
    cmp dx, ax
    jb .next
    mov ax, [es:si+LN_END]
    cmp dx, ax
    jb .hit
.next:
    add si, LN_SIZE
    inc cx
    jmp .scan
.hit:
    mov ax, cx
    sub ax, [br_top]
    js .out                         ; above the view
    cmp ax, [br_rows]
    jae .out                        ; below it
    mov bx, ax
    mov ax, cx
    call br_paint_line              ; AX = line index, BX = row
.out:
    pop es
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_ftab - move focus to the next field ----------------------------------
br_ftab:
    push ax
    push bx
    cmp byte [br_fn], 0
    je .out
    mov al, [br_fcur]
    cmp al, 0FFh
    je .first
    push ax
    call br_fld                     ; take the caret off the old one
    mov al, D_NBSP                  ; ...to a no-break space, NOT to ' ': the
                                    ; caret cell is inside the field and a real
                                    ; space there is a wrap opportunity that
                                    ; appears and moves as focus does
    call br_fcaret
    call br_frepaint
    pop ax
    inc al
    cmp al, [br_fn]
    jb .set
.first:
    xor al, al
.set:
    call br_ffocus
.out:
    pop bx
    pop ax
    ret

; --- br_ffocus - give field AL the caret (the old one loses it) --------------
; in:  AL = field index; gfx lock held
; out: nothing; preserves all registers
;
; Lifted out of br_ftab so that Tab and a CLICK reach one body: the two would
; otherwise be two descriptions of "what focus looks like", and the visible
; half of it is a byte written into the DOCUMENT (br_fcaret).
br_ffocus:
    push ax
    push bx
    push cx
    mov cl, al
    mov al, [br_fcur]
    cmp al, 0FFh
    je .set
    cmp al, cl
    je .same                        ; already focused: draw nothing at all
    call br_fld
    mov al, D_NBSP                  ; ...see br_ftab: not a real space
    call br_fcaret
    call br_frepaint
.set:
    mov [br_fcur], cl
    mov al, cl
    call br_fld
    mov al, '_'
    call br_fcaret
    call br_frepaint
.same:
    pop cx
    pop bx
    pop ax
    ret

; --- br_fkey - a keystroke into the focused field ----------------------------
; in:  AL = ascii, AH = scan; out: CF=0 if it was consumed
br_fkey:
    push bx
    push dx
    push di
    push es
    cmp byte [br_fcur], 0FFh
    je .no
    push ax
    mov al, [br_fcur]
    call br_fld
    pop ax
    cmp al, 8
    je .bs
    cmp al, ' '
    jb .no
    cmp al, 0x7E
    ja .no
    mov dl, [bx+FLD_LEN]
    cmp dl, [bx+FLD_W]
    jae .yes                        ; full: swallow it rather than overflow
    mov es, [br_docseg]
    mov di, [bx+FLD_OFS]
    xor dh, dh
    add di, dx
    mov [es:di], al
    inc byte [bx+FLD_LEN]
    jmp .draw
.bs:
    cmp byte [bx+FLD_LEN], 0
    je .yes
    mov al, D_NBSP                  ; **THE OLD CARET CELL FIRST.** The caret is
    call br_fcaret                  ; a DOCUMENT BYTE (br_fcaret), so moving it
                                    ; left means blanking where it was - and
                                    ; nothing did, so every backspace left its
                                    ; `_` behind and the field filled up with
                                    ; them, one per key. Insert never met this:
                                    ; the caret moves RIGHT and the character
                                    ; being typed lands on the cell it vacated.
                                    ; br_fcaret's own `full: there is no caret
                                    ; cell` test is what makes this safe at
                                    ; FLD_W, where the old caret is off the end
    dec byte [bx+FLD_LEN]
    mov es, [br_docseg]
    mov di, [bx+FLD_OFS]
    mov dl, [bx+FLD_LEN]
    xor dh, dh
    add di, dx
    mov byte [es:di], D_NBSP        ; ...back to a NO-break space, or a
                                    ; backspace punches a wrap opportunity into
                                    ; the middle of the field
.draw:
    mov al, '_'
    call br_fcaret
    call br_frepaint
.yes:
    clc
    jmp .out
.no:
    stc
.out:
    pop es
    pop di
    pop dx
    pop bx
    ret

; --- br_submit - compose the query and say what it would fetch ---------------
; BROWSER-PLAN 7.4. With no transport yet the URL is BUILT and REPORTED rather
; than fetched, which is exactly what 10 step 4 asks for: the encoder is
; checkable by printing the URL a submit would produce.
br_submit:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    push es
    mov word [br_un], 0
    mov byte [br_url], 0
    mov byte [br_dest], 1
    mov si, br_faction              ; the action, verbatim
.act:
    mov al, [si]
    or al, al
    jz .q
    call br_uput
    inc si
    jmp .act
.q:
    mov al, '?'
    call br_uput
    xor cx, cx                      ; CX = field index
    xor dx, dx                      ; DX = have we written a pair yet
.fld:
    cmp cl, [br_fn]
    jae .hid
    mov al, cl
    call br_fld
    cmp byte [bx+FLD_NAME], 0
    je .nextf
    or dx, dx
    jz .amp0
    mov al, '&'
    call br_uput
.amp0:
    mov dx, 1
    push cx
    mov si, bx
    add si, FLD_NAME
.nm:
    mov al, [si]
    or al, al
    jz .eq
    call br_encb
    inc si
    jmp .nm
.eq:
    mov al, '='
    call br_uput
    mov es, [br_docseg]
    mov si, [bx+FLD_OFS]
    mov cl, [bx+FLD_LEN]
    xor ch, ch
.val:
    or cx, cx
    jz .doneval
    mov al, [es:si]
    call br_encb
    inc si
    dec cx
    jmp .val
.doneval:
    pop cx
.nextf:
    inc cl
    jmp .fld
.hid:
    cmp byte [br_hn], 0
    je .say
    or dx, dx
    jz .h2
    mov al, '&'
    call br_uput
.h2:
    mov si, br_hid
.hloop:
    mov al, [si]
    or al, al
    jz .say
    call br_uput                    ; already encoded at parse time
    inc si
    jmp .hloop
.say:
    mov byte [br_dest], 0
    mov si, br_url                  ; the bar is 25 cells; the whole URL lives
    mov cx, 120                     ; in br_url for the harness to read
    push ds
    pop es
    call OSAPI_TOAST
    mov si, br_url                  ; ...AND THEN FETCH IT. This is the whole
    call br_resolve                 ; of what a search box is: the encoder was
    pop es                          ; built at BROWSER-PLAN 7.4 and reported a
    jc .out                         ; URL because there was no transport, and
                                    ; into a page (NET-STACK-PLAN stage D').
                                    ;
                                    ; **RESOLVED FIRST, AND THAT IS NOT
                                    ; OPTIONAL**: a form's action is a URL like
                                    ; any other and is usually RELATIVE -
                                    ; FrogFind's is `/`, so the composed thing
                                    ; is `/?q=...`, which br_split rightly
                                    ; refuses as not-http. The search box could
                                    ; be typed into and never submitted
                                    ; anywhere. One call, the link resolver's,
                                    ; so an action and an href cannot come to
                                    ; disagree about what `/` means
    mov si, br_ubuf
    call br_go
.out:                               ; br_resolve refused and has already said
    pop si                          ; why on the status line
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- Latin-1 / CP1252 -> one ASCII char, 0 = drop (BROWSER-PLAN 2.2) ------
; GENERATED - do not edit by hand:
;     python3 tools/htmsim.py --emit-l1tab
; The model and this table are one definition, which is what makes htmsim a
; reference rather than a second opinion: two hand-maintained 128-entry tables
; would have drifted on the first accented letter nobody checked. Index is the
; byte minus 0x80; 0 means DROP. Folds are SINGLE-BYTE by design - a
; multi-character fold ('1/2', '(c)') would make the emit variable-length for
; a handful of glyphs nobody misses.
br_l1tab:
    db  '?',   0, ',', 'f', '"', '.', '+', '+', '^', '%', 'S', '<', 'O',   0, 'Z',   0   ; 80
    db    0,0x27,0x27, '"', '"', '*', '-', '-', '~', '?', 's', '>', 'o',   0, 'z', 'Y'   ; 90
    db  ' ', '!', 'c', '?', '?', '?', '|', '?', '"', 'c', 'a', '<', '?',   0, 'R', '-'   ; A0
    db  'o', '?', '2', '3',0x27, 'u', '?', '.', ',', '1', 'o', '>', '?', '?', '?', '?'   ; B0
    db  'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I'   ; C0
    db  'D', 'N', 'O', 'O', 'O', 'O', 'O', 'x', 'O', 'U', 'U', 'U', 'U', 'Y', 'T', 's'   ; D0
    db  'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i'   ; E0
    db  'd', 'n', 'o', 'o', 'o', 'o', 'o', '/', 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y'   ; F0

; --- codepoints above Latin-1 that a proxy will really emit -------------------
; word codepoint, byte replacement; word 0 ends it. Anything not here is '?',
; which is honest: the cell font has 95 glyphs and no amount of table buys a
; Cyrillic one.
br_wtab:
    dw 0x2018
    db 0x27
    dw 0x2019
    db 0x27
    dw 0x201C
    db '"'
    dw 0x201D
    db '"'
    dw 0x2013
    db '-'
    dw 0x2014
    db '-'
    dw 0x2026
    db '.'
    dw 0x2022
    db '*'
    dw 0x2039
    db '<'
    dw 0x203A
    db '>'
    dw 0x00A0
    db ' '
    dw 0

; --- named entities -----------------------------------------------------------
; name, NUL, replacement. A name not here falls through to LITERAL TEXT, which
; is what stops a malformed entity eating the rest of the line.
br_enttab:
    db 'amp',0,'&'
    db 'lt',0,'<'
    db 'gt',0,'>'
    db 'quot',0,'"'
    db 'apos',0,0x27
    db 'nbsp',0,' '
    db 'copy',0,'c'
    db 'reg',0,'R'
    db 'trade',0,'?'
    db 'mdash',0,'-'
    db 'ndash',0,'-'
    db 'hellip',0,'.'
    db 'laquo',0,'<'
    db 'raquo',0,'>'
    db 'lsquo',0,0x27
    db 'rsquo',0,0x27
    db 'ldquo',0,'"'
    db 'rdquo',0,'"'
    db 'bull',0,'*'
    db 'middot',0,'.'
    db 'times',0,'x'
    db 'deg',0,'o'
    db 'micro',0,'u'
    db 'plusmn',0,'?'
    db 'frac12',0,'?'
    db 'sup2',0,'2'
    db 'sup3',0,'3'
    db 'dagger',0,'+'
    db 'permil',0,'%'
    db 'sect',0,'?'
    db 'para',0,'?'
    db 'curren',0,'?'
    db 'yen',0,'?'
    db 'cent',0,'c'
    db 'iexcl',0,'!'
    db 'iquest',0,'?'
    db 'aacute',0,'a'
    db 'eacute',0,'e'
    db 'iacute',0,'i'
    db 'oacute',0,'o'
    db 'uacute',0,'u'
    db 'agrave',0,'a'
    db 'egrave',0,'e'
    db 'ccedil',0,'c'
    db 'ntilde',0,'n'
    db 'aring',0,'a'
    db 'oslash',0,'o'
    db 'aelig',0,'a'
    db 'pound',0,'?'
    db 'euro',0,'?'
    db 'szlig',0,'s'
    db 'auml',0,'a'
    db 'ouml',0,'o'
    db 'uuml',0,'u'
    db 0

; --- tags -----------------------------------------------------------------------
; name, NUL, TA_*. Everything not here is IGNORED and its text kept, which is
; what makes an unknown tag harmless: <font>, <span>, <tt>, <small>, <b>, <i>
; and every colour attribute in the file are dropped exactly this way
; (BROWSER-PLAN 2.2.1 - colour goes in BOTH directions or text goes invisible).
br_tagtab:
    db 'p',0,TA_PARA
    db 'div',0,TA_PARA
    db 'ul',0,TA_PARA
    db 'ol',0,TA_PARA
    db 'dl',0,TA_PARA
    db 'dt',0,TA_PARA
    db 'dd',0,TA_PARA
    db 'table',0,TA_TAB
    db 'tr',0,TA_ROW
    db 'td',0,TA_CELL
    db 'th',0,TA_CELL
    db 'caption',0,TA_PARA
    db 'blockquote',0,TA_PARA
    db 'a',0,TA_LINK
    db 'form',0,TA_FORM
    db 'input',0,TA_INPUT
    db 'br',0,TA_BR
    db 'hr',0,TA_HR
    db 'li',0,TA_LI
    db 'pre',0,TA_PRE
    db 'center',0,TA_CEN
    db 'script',0,TA_DROP
    db 'style',0,TA_DROP
    db 'title',0,TA_TITLE       ; ...CAPTURED and then dropped (br_captitle)
    db 'select',0,TA_DROP
    db 'textarea',0,TA_DROP
    db 0

; --- window template (SPEC.md 11) ----------------------------------------------
br_tpl:
    dw 40, 30, 496, 150
    dw br_ttl, br_paint, br_onkey, br_onclick

; --- the app menu set (SPEC.md 12.2) -------------------------------------------
; No Close item: SPEC.md 12.7 puts one in the app-NAME cell for every
; application, so a File > Close here would be a second door onto the same
; kernel routine.
    OS88_MENUSET br_menus, br_name_s, br_oncmd
        OS88_MENU br_m_file, br_i_file, 3   ; ...and the COUNT is here, not in
                                            ; br_i_file's list: adding a
                                            ; pointer without it leaves the
                                            ; item unreachable and invisible,
                                            ; with everything else working
        OS88_MENU br_m_go,   br_i_go,   3
        OS88_MENU br_m_hist, br_hitems, 1   ; ...and the COUNT here is a
                                            ; STARTING value: br_hsync
                                            ; rewrites it, through BR_HNITEM
                                            ; below, every time the history
                                            ; moves. It is 1 and not 0 because
                                            ; an empty pull-down is a box with
                                            ; nothing in it - the one item says
                                            ; so instead
    OS88_MENUSET_END br_menus

; The item-count word of the History cell, addressed through the macro's own
; constants so it follows if either changes. Patching a menu set in place is
; legal and needs no OSAPI_MENU_SET afterwards: the bar's layout is a function
; of the TITLES, which do not move, and the kernel reads the items when it
; drops the menu (SPEC.md 12.2).
BR_HNITEM equ br_menus_list + 2 * AMENU_ENTSZ + AMENU_NITEM

br_name_s:  db 'Browser', 0
br_m_file:  db 'File', 0
br_i_file:  dw br_it_open, br_it_loc, br_it_save
br_it_open: db 'Open...', 0
br_it_loc:  db 'Open Location...', 0
br_it_save: db 'Save As...', 0
br_m_hist:  db 'History', 0
br_it_none: db '(empty)', 0
br_m_go:    db 'Go', 0
br_i_go:    dw br_it_top, br_it_bot, br_it_rel
br_it_top:  db 'Top', 0
br_it_bot:  db 'Bottom', 0
br_it_rel:  db 'Reload', 0

br_a_href:    db 'href', 0
br_a_action:  db 'action', 0
br_a_method:  db 'method', 0
br_a_type:    db 'type', 0
br_a_name:    db 'name', 0
br_a_value:   db 'value', 0
br_a_size:    db 'size', 0
br_s_post:    db 'post', 0
br_s_submit:  db 'submit', 0
br_s_hidden:  db 'hidden', 0
br_s_image:   db 'image', 0
br_s_check:   db 'checkbox', 0
br_s_subbtn:  db '<', D_NBSP, 'Submit', D_NBSP, '>', 0   ; ONE WORD, so the
                                    ; wrap moves the whole button to the next
                                    ; line rather than splitting it. FrogFind's
                                    ; is at the end of a line and came out as
                                    ; `< Submit` with the `>` alone below
br_s_scheme:  db 'http://', 0
br_s_schemes: db 'https://', 0
br_s_colon:   db ':', 0
br_s_nohttps: db 'Https is not supported', 0
br_s_nohost:  db 'Relative link: this page has no server', 0
br_s_nopage:  db 'Nothing loaded to save', 0
br_s_saved:   db 'Saved', 0
br_s_wfail:   db 'Save failed', 0
br_s_unsup:   db '[unsupported]', 0

br_ttl:     db 'Browser', 0
br_s_big:   db 'Page too large', 0
br_s_bad:   db 'Cannot open page', 0

%define OS88UI_SCROLL           ; SPEC.md 13.10: the shared scroll bar - OPT IN,
                                ; so a package that draws no bar pays none of
                                ; its bytes
%include "os88ui.inc"           ; the standard button (SPEC.md 20.5.1)...
%include "os88line.inc"         ; ...and the one-line field the bar is
%include "brnet.inc"            ; ...and the fetch (NET-STACK-PLAN stage D)
%include "os88sock.inc"         ; ...whose driver net_find locates (SPEC.md 72)

; The loader zeroes exactly this much (SPEC.md 21 step 5), and every equ below
; indexes into it - declare it short and the tail of the last field is
; somebody else's heap. BR_BSS is DERIVED from the last field now rather than
; being a literal, because the network half added eight of them and a hand-kept
; total is one more thing to get wrong silently.
    OS88_BSS BR_BSS
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
br_win      equ os88_image_end + 0    ; word: our window
br_cx       equ os88_image_end + 2    ; the live content box, re-read every paint
br_cy       equ os88_image_end + 4
br_cw       equ os88_image_end + 6
br_ch       equ os88_image_end + 8
br_cols     equ os88_image_end + 10   ; band width in CELLS; 0 forces a relayout
br_rows     equ os88_image_end + 12
br_top      equ os88_image_end + 14   ; the line at the top of the view
br_ptop     equ os88_image_end + 16   ; ...and the one the SCREEN shows
br_dy       equ os88_image_end + 18   ; the CLAMPED delta the drawing uses
br_sbx      equ os88_image_end + 20   ; the scroll bar's rect, shared by the
br_skips    equ os88_image_end + 22   ; consecutive coalesced draws, bounded

br_srcseg   equ os88_image_end + 24
br_docseg   equ os88_image_end + 26
br_lseg     equ os88_image_end + 28
br_srclen   equ os88_image_end + 30
br_srckb    equ os88_image_end + 32
br_dockb    equ os88_image_end + 34
br_doclen   equ os88_image_end + 36
br_nlines   equ os88_image_end + 38

br_lstart   equ os88_image_end + 40   ; the layout walk's state
br_nch      equ os88_image_end + 42
br_bpos     equ os88_image_end + 44
br_bch      equ os88_image_end + 46

br_on       equ os88_image_end + 48   ; byte: staging buffer fill
br_wsp      equ os88_image_end + 49   ; byte: a space is pending
br_any      equ os88_image_end + 50   ; byte: this block has content
br_pre      equ os88_image_end + 51   ; byte: inside <pre>
br_utf8     equ os88_image_end + 52   ; byte: the page declares UTF-8
br_trunc    equ os88_image_end + 53   ; byte: the document was cut at the copy
br_close    equ os88_image_end + 54   ; byte: this tag is a closing one
br_lcen     equ os88_image_end + 55   ; byte: centring
br_lpre     equ os88_image_end + 56   ; byte: layout's own PRE state
br_lind     equ os88_image_end + 57   ; byte: indent owed to the next line

br_name     equ os88_image_end + 58   ; 14: the 8.3 name we were given
br_tname    equ os88_image_end + 72   ; 16: the tag being read, lowercased
br_ebuf     equ os88_image_end + 88   ; 12: an entity body
br_lbuf     equ os88_image_end + 100  ; BR_MAXCOL+2: one padded display line
br_obuf     equ os88_image_end + 200  ; 256: the parse's staging buffer

; --- the table engine's scratch (BROWSER-PLAN 3.2) ----------------------------
br_comp     equ os88_image_end + 456  ; word: the composed-row arena's cursor,
                                      ; which lives past D_END in the DOCUMENT
                                      ; claim - so a composed row is a line
                                      ; entry like any other and the painter
                                      ; needs no second path
br_cline    equ os88_image_end + 458  ; word: where the line being composed began
br_tn       equ os88_image_end + 460  ; byte: columns this table has
br_tany     equ os88_image_end + 461  ; byte: any cell has text (3.2.1)
br_tcol     equ os88_image_end + 462  ; byte: the column being measured
br_tnc      equ os88_image_end + 463  ; byte: cells in the row being emitted
br_tcells   equ os88_image_end + 464  ; word: cells in the whole table (3.2.1)
br_tavail   equ os88_image_end + 466  ; word: cells the columns may share
br_tw       equ os88_image_end + 468  ; 8 bytes: each column's width
br_tflr     equ os88_image_end + 476  ; 8 bytes: ...and its longest word
br_cs       equ os88_image_end + 484  ; 8 words: this row's cell starts
br_ce       equ os88_image_end + 500  ; 8 words: ...and their ends
br_tcur     equ os88_image_end + 516  ; 8 words: ...and the wrap cursor in each

; --- forms (BROWSER-PLAN 7) ---------------------------------------------------
br_astart   equ os88_image_end + 532  ; word: the current tag's attribute span
br_aend     equ os88_image_end + 534  ; word: ...in the SOURCE
br_onw      equ os88_image_end + 536  ; word: br_put's buffered count, so a
                                      ; field's recorded offset is where the
                                      ; byte LANDS and not where the last
                                      ; flush got to
br_fn       equ os88_image_end + 538  ; byte: text fields on the page
br_fcur     equ os88_image_end + 539  ; byte: which has focus, 0FFh = none
br_post     equ os88_image_end + 540  ; byte: method=post (recorded, not acted
                                      ; on - 7.5)
br_hn       equ os88_image_end + 541  ; byte: any hidden pairs
br_dest     equ os88_image_end + 542  ; byte: which sink br_encb appends to
br_hnw      equ os88_image_end + 544  ; word: bytes of hidden pairs
br_un       equ os88_image_end + 546  ; word: bytes of composed URL
br_flds     equ os88_image_end + 548  ; BR_FMAX * BR_FLDSZ = 72
br_faction  equ os88_image_end + 620  ; BR_ACTMAX
br_hid      equ os88_image_end + 684  ; BR_HIDMAX
br_sbx2     equ os88_image_end + 940  ; painter and the hit-tester so the two
br_sby      equ os88_image_end + 942  ; cannot drift (SPEC.md 22)
br_sby2     equ os88_image_end + 944
br_url      equ os88_image_end + 780  ; BR_URLMAX - and the harness reads this
                                      ; to check the encoder (7.4)

; --- the network half (NET-STACK-PLAN stage D, apps/browser/brnet.inc) -------
BR_LPAD     equ 3                     ; the bar's inset from the content edge
BR_LBAR     equ 15                    ; ...its height

; --- the toolbar row, ABOVE the bar ------------------------------------------
; Back / Forward / Reload on the left and the state on the right, in ONE thin
; strip. It costs the page three rows net rather than thirteen, because the
; state used to have a line of its own UNDER the bar and now shares this one -
; and rows of band are what a browser on a 200-line screen is short of.
BR_TBH      equ 13                    ; 8px of label + the frame either side,
                                      ; which is the shortest a standard button
                                      ; can be drawn at (os88ui_btn centres the
                                      ; label, so (13-8)/2 = 2 rows of air)
BR_TBG      equ 1                     ; the hairline between the strip and the
                                      ; bar, and under the bar
BR_BTNW     equ 40                    ; Back and Fwd: 4 glyphs plus air
BR_BTNW2    equ 56                    ; ...and Reload, which is six
BR_BTNG     equ 3                     ; between them
BR_SCELLS   equ 48                    ; the state's field, in cells - a CAP and
                                      ; not a size: br_srect gives it whatever
                                      ; is left between the last button and the
                                      ; content's right edge, and only clamps
                                      ; here. It was 22, which cut
                                      ; `Relative link: this page has no
                                      ; server` in half on the one path that
                                      ; produces it. The longest message this
                                      ; app can say is 38 cells; the rest is
                                      ; slack for the next one
BR_TITMAX   equ 32                    ; a History label, NUL included. The
                                      ; pull-down is as wide as its widest
                                      ; item, and 31 characters is about as
                                      ; much as a 640px bar can carry without
                                      ; the menu reaching the clock
BR_HISTN    equ 8                     ; URLs the Back stack holds. **URL ONLY**
                                      ; (BROWSER-PLAN 5): keeping parsed
                                      ; documents costs the heap and a re-fetch
                                      ; is cheap, and the scroll position that
                                      ; would actually be worth keeping is a
                                      ; later two words. Full, the OLDEST goes
BR_LTOT     equ BR_LPAD + BR_TBH + BR_TBG + BR_LBAR + BR_TBG
BR_UBUF     equ 160                   ; what the bar holds: BR_URLMAX

br_nstate   equ os88_image_end + 946  ; byte: the fetch's state
br_spawn    equ os88_image_end + 947  ; byte: we own a worker
br_hnd      equ os88_image_end + 948  ; byte: the socket
br_hstate   equ os88_image_end + 949  ; byte: how much of CR LF CR LF
br_port     equ os88_image_end + 950
br_nlen     equ os88_image_end + 952  ; bytes of BODY taken so far
br_code     equ os88_image_end + 954  ; the status line's number
br_hcol     equ os88_image_end + 956  ; ...how far into the first header line
br_nmsg     equ os88_image_end + 958  ; -> the failure text, for BN_ERR
br_sent     equ os88_image_end + 960
br_reqn     equ os88_image_end + 962
br_loc      equ os88_image_end + 964  ; OS88LINE_SZ: the location bar
br_ubuf     equ br_loc + OS88LINE_SZ  ; BR_UBUF: ...and its text
br_host     equ br_ubuf + BR_UBUF     ; BR_HOSTMAX
br_path     equ br_host + BR_HOSTMAX  ; BR_PATHMAX
br_reqb     equ br_path + BR_PATHMAX  ; BR_REQMAX
br_nsline   equ br_reqb + BR_REQMAX   ; BR_SCELLS+1: the padded state field.
                                      ; **SIZED FROM THE CONSTANT**, because it
                                      ; was a literal 24 against a field of 22
                                      ; and raising one without the other is a
                                      ; run that writes past its buffer
br_rxb      equ br_nsline + BR_SCELLS + 1  ; BR_CHUNK

; --- the column map (BROWSER-PLAN 7.4 / the link hit test) -------------------
; br_build fills this beside br_lbuf: one word per band cell, the DOCUMENT
; OFFSET whose byte was lettered there, or 0FFFFh for a cell the padding owns.
; It is what turns a click into a document position without a second walk.
br_lmap     equ br_rxb + BR_CHUNK     ; BR_MAXCOL words

; --- the toolbar and the history (BROWSER-PLAN 5) ----------------------------
br_sb       equ br_lmap + BR_MAXCOL*2 ; the shared scroll bar's 7-word block
br_sbold    equ br_sb + 14           ; word: the pos the thumb is drawn at
br_tby      equ br_sbold + 2         ; word: the strip's top, derived
br_r1       equ br_tby + 2            ; the three button rects {x1,y1,x2,y2}
br_r2       equ br_r1 + 8
br_r3       equ br_r2 + 8
br_spen     equ br_r3 + 8             ; word: the state's pen, 8-aligned
br_swid     equ br_spen + 2           ; word: ...and the cells it may use
br_histn    equ br_swid + 2           ; word: entries in the stack
br_histi    equ br_histn + 2          ; word: where we are in it
br_nopush   equ br_histi + 2          ; byte: this br_go is a Back/Fwd/Reload
                                      ; and must not push
br_hist     equ br_nopush + 2         ; BR_HISTN slots of BR_UBUF

; --- links (BROWSER-PLAN 6) --------------------------------------------------
br_lnkseg   equ br_hist + BR_HISTN*BR_UBUF  ; the hrefs, packed NUL-terminated
br_lnkn     equ br_lnkseg + 2         ; word: how many
br_lnkw     equ br_lnkseg + 4         ; word: the write cursor into that claim
br_lnkopen  equ br_lnkseg + 6         ; word: the link the PARSE is inside,
                                      ; 0FFFFh = none
br_lnkst    equ br_lnkseg + 8         ; word: ...and what it was when the line
                                      ; being laid out began (br_emitline)
br_lnkhit   equ br_lnkseg + 10        ; word: the link a click resolved to
br_lnkoff   equ br_lnkseg + 12        ; BR_LNKMAX words: each href's offset
br_upos     equ br_lnkoff + BR_LNKMAX*2  ; word: br_resolve's write cursor
br_lnkcell  equ br_upos + 2           ; byte: br_build's running state
br_lmark    equ br_lnkcell + 1        ; BR_MAXCOL bytes: 1 = this band cell
                                      ; is inside a link, for the underline

; --- the submit button's span (BROWSER-PLAN 7.4) -----------------------------
; '< Submit >' is ordinary document text, which is what makes it wrap and
; scroll for free - and what leaves it with no record a click can find. These
; two are that record. Cleared by br_parse, so a page without one cannot
; inherit the last page's button.
br_subofs   equ br_lmark + BR_MAXCOL
br_subend   equ br_subofs + 2

; --- the About panel (SPEC.md 12.2) ------------------------------------------
; --- the History menu (SPEC.md 71.7) -----------------------------------------
; br_hitems is what the menu set POINTS AT, so the kernel reads it through our
; segment like any other item array - bss is as good as the image for that.
; The labels are a slot each rather than packed, because the menu holds NEAR
; POINTERS and a packed arena would have to be re-walked to produce them.
br_ptitle   equ br_subend + 2         ; BR_TITMAX: the CURRENT page's <title>
br_htit     equ br_ptitle + BR_TITMAX ; BR_HISTN labels of BR_TITMAX
br_hitems   equ br_htit + BR_HISTN * BR_TITMAX   ; BR_HISTN words of pointer

; --- the fetch generation (brnet.inc's br_abort) -----------------------------
br_gen      equ br_hitems + BR_HISTN * 2   ; byte: bumped by every br_go that drops
                                      ; a fetch in flight
br_gen0     equ br_gen + 1            ; byte: what the running worker pass
                                      ; banked at its entry

br_abon     equ br_gen + 2            ; byte: 1 = the credit card is up
br_abw      equ br_abon + 2           ; its measured rect, SCREEN coords
br_abh      equ br_abon + 4
br_abl      equ br_abon + 6
br_abt      equ br_abon + 8
BR_BSS      equ (br_abon - os88_image_end) + 10
