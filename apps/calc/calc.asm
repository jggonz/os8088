; =============================================================================
; os8088 - apps/calc/calc.asm
;
; Calculator, the sixteenth package (SPEC.md 65). A four-function desk
; calculator with a foldaway history of the last CAL_HIST_N calculations; a
; history row is CLICKABLE and loads its answer back into the entry, so the
; result of one sum is the start of the next.
;
; Three things about it are worth reading before changing anything.
;
; THE NUMBER IS SIGN-MAGNITUDE DECIMAL, NOT FIXED POINT (SPEC.md 65.2). A
; value is a 32-bit magnitude of at most 9 digits, a decimal-place count
; 0..8, and a sign byte - so the point FLOATS and 1/3 is 0.33333333 rather
; than whatever a chosen scale would have rounded it to. A 32-bit fixed point
; at 10^4 was the first design and its range is +/-214748.3647, which cannot
; multiply 1000 by 1000; this can, and refuses at the point a 9-digit machine
; genuinely runs out rather than at an arbitrary scale boundary. The
; intermediates are 64-bit (cal_q/cal_q2/cal_q3, four words each) because a
; 9-digit product is 18 digits and there is no other way to round it right.
; Sign-magnitude rather than two's complement because every 64-bit
; intermediate is then UNSIGNED: there is no signed shift-subtract divide or
; rounding rule to get wrong, and the sign of a sum is four lines of case
; analysis in ONE place (cal_addsub).
;
; THE LAYOUT IS DERIVED FROM THE LIVE CONTENT BOX (SPEC.md 65.3). The keypad
; is fixed - a button is 14 rows because an 8x8 glyph plus padding is - and
; everything above the history is a constant, so the ONE number that moves is
; how many history rows fit: `(ch - CAL_BASE_H) / CAL_ROW_H`, clamped. That
; is what makes a CGA correct rather than merely small: its desktop band is
; 155 frame rows = 136 of content (SPEC.md 11.98), so folding the history
; open there yields TWO rows where a VGA yields eight, and no build knows
; anything about an adapter. OSAPI_WM_ONRESIZE (SPEC.md 11.98) covers the box
; moving with nobody asking - an Activate Mode switch, or this window dragged
; across the seam of an extended desktop onto the shorter display (SPEC.md
; 39.16.3) - and the repaint the kernel owes follows it, so the first frame
; after the move is already right.
;
; NOTHING IS REDRAWN THAT DID NOT CHANGE (SPEC.md 65.4). Both display fields
; are fixed-width space-padded runs compared against a shadow of what is on
; the glass, so a keystroke emits ONE OSAPI_FONT_RUN over the cells that
; actually differ - the padding IS the erase, so there is no fill in the path
; at all and the field is never momentarily blank (SPEC.md 6.1). The pens are
; multiples of 8 from a content origin OSAPI_WM_SNAP keeps on a byte
; boundary, so every one of those runs takes the single-store path on all
; three adapters (SPEC.md 11.94). A completed sum SCROLLS the history band
; down by one row and letters only the new row 0, which is one blit and one
; run instead of eight (SPEC.md 65.4.1). And a button is drawn once per full
; repaint: a press XOR-inverts it, and the release draws it back properly
; rather than XOR-ing again, so a repaint arriving between the two cannot
; leave one inverted for the session.
; =============================================================================

%include "os88api.inc"

; -----------------------------------------------------------------------------
; APP_SMALL - THE SMALL-APPS BUILD OF THIS PACKAGE (SPEC.md 65.10)
;
; Note Pad's SPEC.md 27.16 is the pattern and its rules hold unchanged: one
; source assembled twice, `make smallapps` passes -DAPP_SMALL, the result goes
; on build/smallapps*.img, and it is NEVER a second ABI - the small build runs
; on kern_big exactly as it runs on kern_small.
;
; WHY A CALCULATOR IS WORTH GATING AT ALL, when it already claims 7,644 bytes
; against the 34.0KB kern_small leaves on a 128KB machine: because the point
; of the small disk is a HANDFUL of programs that run, not one large one that
; barely does. Every byte off a small package is another package beside it.
;
; **AT THE TOP** for the reason apps/notepad/notepad.asm's own note gives -
; these are preprocessor tests answered in FILE ORDER, and the blocks they
; guard start ~900 lines below.
;
;   CALF_HIST   the foldaway history pane (SPEC.md 65), its eight rows, the
;               disclosure triangle, the View menu and the ring that feeds
;               them. The calculator KEEPS its four functions, sqrt, 1/x,
;               percent, sign, the clipboard and the keyboard
;   CALF_ABOUT  the standard About card (SPEC.md 12.2)
;
; What a small Calculator is, then: the display, the keypad, the Edit and Math
; menus, and no pane. It opens at CAL_BASE_H and never resizes, which is what
; the CGA already got (65.3) - so the small build is the shape this program
; takes on the smallest screen it supports, made the only shape.
; -----------------------------------------------------------------------------
%ifndef APP_SMALL
%define CALF_HIST
%define CALF_ABOUT
%endif

    OS88_HEADER 'CALCULATOR', cal_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A pocket calculator: a body outlined at x 2..13, a display well across its
; top and three rows of three keys under it. The mask is a plain white plate
; (x 1..14, rows 0..14) rather than a dilated silhouette - the body IS a
; rectangle, so the two are the same picture and the rectangle is readable.
;
;   data                          mask
;   ................              .##############.
;   ..############..              .##############.
;   ..#..........#..              ...(rows 0..14)
;   ..#.########.#..
;   ..#.#......#.#..
;   ..#.########.#..
;   ..#..........#..
;   ..#.##.##.##.#..
;   ..#..........#..
;   ..#.##.##.##.#..
;   ..#..........#..
;   ..#.##.##.##.#..
;   ..#..........#..
;   ..############..
;   ................
;   ................
    OS88_ICON16
    dw 0x7FFE                       ; 16 mask rows (white underlay)
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
    dw 0x7FFE
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x3FFC                       ; body, top edge
    dw 0x2004
    dw 0x2FF4                       ; display well, top
    dw 0x2814
    dw 0x2FF4                       ; display well, bottom
    dw 0x2004
    dw 0x2DB4                       ; three rows of three keys
    dw 0x2004
    dw 0x2DB4
    dw 0x2004
    dw 0x2DB4
    dw 0x2004
    dw 0x3FFC                       ; body, bottom edge
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

; =============================================================================
; Geometry (SPEC.md 65.3). Everything here is CONTENT-RELATIVE; the origin
; comes from OSAPI_WM_CONTENT on every callback, because the window moves.
;
; Every text pen is a multiple of 8, so that OSAPI_WM_SNAP's byte-aligned
; content origin buys the single-store OSAPI_FONT_RUN path (SPEC.md 6.1/
; 11.94). Change one of them to a non-multiple and the runs silently fall
; back to fill-plus-letter - correct, and about 12% dearer on VGA.
; =============================================================================
CAL_CW       equ 224                ; content width (the frame is CAL_CW + 2)

; --- the display well ----------------------------------------------------------
CAL_DISP_X1  equ 2                  ; the sunken box, inset from the content
CAL_DISP_X2  equ CAL_CW - 3
CAL_DISP_Y1  equ 2
CAL_DISP_Y2  equ 19                 ; 18 rows; interior rows 3..18
CAL_TXT_Y    equ 7                  ; the one text line, rows 7..14

CAL_CTX_X    equ 8                  ; the pending expression, right-aligned
CAL_CTX_N    equ 13                 ; 13 cells: 11 of number + ' ' + the op
CAL_NUM_X    equ 112                ; the entry / result, right-aligned
CAL_NUM_N    equ 13                 ; 13 cells: '-9.99999999' is 11 of them

; --- the keypad ----------------------------------------------------------------
CAL_KEY_X0   equ 4                  ; four columns of CAL_BTN_W with CAL_BTN_HG
CAL_BTN_W    equ 51                 ; between them, spanning x 4..219
CAL_BTN_HG   equ 4
CAL_KEY_Y0   equ 24
CAL_BTN_H    equ 14                 ; an 8-row glyph plus 3 of padding each side
CAL_BTN_VG   equ 2
CAL_KEY_COLS equ 4
CAL_KEY_ROWS equ 5
CAL_NKEY     equ CAL_KEY_COLS * CAL_KEY_ROWS         ; 20
CAL_KEY_PIT  equ CAL_BTN_H + CAL_BTN_VG              ; 16: rows at 24..88

; --- the disclosure strip ------------------------------------------------------
CAL_HDR_RULE equ 105                ; a 1px dithered rule across the content
CAL_HDR_Y1   equ 106                ; ...and the clickable strip under it
CAL_HDR_TY   equ 108                ; the label's cell rows, 108..115
CAL_HDR_TX   equ 24                 ; 'History', clear of the triangle
CAL_TRI_X    equ 8                  ; the 7x7 disclosure triangle
CAL_HDR_Y2   equ 117

; --- and the history rows, which are the only elastic thing here ---------------
CAL_BASE_H   equ 118                ; content height with the history folded up
CAL_ROW_H    equ 9                  ; an 8-row cell and one row of leading
CAL_HIST_Y0  equ CAL_BASE_H         ; row i's cells at + i * CAL_ROW_H
CAL_HROW_X   equ 8
CAL_HROW_N   equ 26                 ; 26 cells: x 8..215, inside CAL_CW
CAL_HIST_N   equ 8                  ; kept, and the most rows ever shown
%ifdef CALF_HIST
CAL_OPEN_H   equ CAL_BASE_H + CAL_HIST_N * CAL_ROW_H ; the height we ASK for
%else
CAL_OPEN_H   equ CAL_BASE_H         ; ...which with no pane is the folded
                                    ; height, and the window never resizes -
                                    ; the shape a CGA already gets (65.3)
%endif

; --- rect table indices (one table, drawn from and hit-tested from) ------------
CAL_FLASH    equ 3                  ; ticks a TYPED key stays pressed (SPEC.md
                                    ; 65.9): 165ms, long enough to see on a
                                    ; 4.77MHz machine and short enough not to
                                    ; lag a fast typist. A constant and not a
                                    ; setting - 31.8 makes a Control Panel page
                                    ; a SYSTEM.CFG write on close, and a flash
                                    ; duration is not worth one
CAL_R_HDR    equ CAL_NKEY           ; 20: the disclosure strip
CAL_R_HIST   equ CAL_NKEY + 1       ; 21..28: the history rows
%ifdef CALF_HIST
CAL_NRECT    equ CAL_R_HIST + CAL_HIST_N
%else
CAL_NRECT    equ CAL_NKEY           ; no strip and no rows: the keypad IS the
                                    ; set of controls, and cal_rect knows it
%endif

; =============================================================================
; The value (SPEC.md 65.2): sign-magnitude decimal, 8 bytes.
; =============================================================================
CV_M         equ 0                  ; dd: magnitude, 0 .. CAL_MAXM
CV_D         equ 4                  ; db: decimal places, 0 .. CAL_MAXD
CV_SGN       equ 5                  ; db: 0 = positive, 1 = negative
CV_SZ        equ 8                  ; (2 of pad: a value is indexed by <<3)

CAL_MAXM     equ 999999999          ; nine digits, and 0x3B9AC9FF fits a dword
CAL_MAXD     equ 8
CAL_ENTMAX   equ 99999999           ; ...so this is the last magnitude a digit
                                    ; may be appended to

; --- key codes -----------------------------------------------------------------
; 0..9 are the digits BY VALUE, which is what makes the digit branch a range
; test and a store rather than a table.
CK_DOT       equ 10
CK_ADD       equ 11                 ; 11..14 are also [cal_op]'s values, so the
CK_SUB       equ 12                 ; pending-operator ladder and the key codes
CK_MUL       equ 13                 ; cannot drift apart
CK_DIV       equ 14
CK_EQ        equ 15
CK_CLR       equ 16
CK_CE        equ 17
CK_BS        equ 18
CK_NEG       equ 19
CK_SQRT      equ 20
CK_PCT       equ 21
CK_RECIP     equ 22
CK_HIST      equ 23                 ; ...and the highest, which bounds cal_jtab
; ...so these two are ABOVE the table on purpose. They are not keys of the
; ENGINE - nothing in cal_dispatch can act on a clipboard - and its own
; `cmp al, CK_HIST / ja` already refuses anything past the table, so putting
; them here means the ladder in cal_onkey is the only thing that can reach
; them and a stray code cannot index cal_jtab off its end.
CK_COPY      equ 24                 ; Ctrl-C
CK_PASTE     equ 25                 ; Ctrl-V
CK_NONE      equ 255

CAL_OP_NONE  equ 0                  ; [cal_op]; otherwise CK_ADD..CK_DIV

CAL_PASTE_MAX equ 24                ; what a paste may read, into cal_pbuf
CAL_CUT       equ '~'               ; a history line too long for the field
                                    ; keeps its HEAD and ends in this
                                    ; CAL_BSS_TOTAL is DERIVED from the counter
                                    ; beside the fields themselves, further down

; -----------------------------------------------------------------------------
; cal_entry - package entry point (SPEC.md 20.2)
; in:  CS = DS = ES = our own segment, IF = 1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
;
; The loader shows the window, so nothing here may draw. Everything installed
; below preserves the FLAGS as well as the registers (SPEC.md 20.3), which is
; why the CF this proc owes the loader survives all six calls.
; -----------------------------------------------------------------------------
cal_entry:
    push ax
    push si
    mov si, cal_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    mov [cal_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE_NOKEEP      ; 6 bytes short: AX is ours to pop, DX
                                    ; and the flags nobody reads (the CF the
                                    ; loader is owed is the `clc` below)
    mov si, cal_menus
    call OSAPI_MENU_SET             ; the bar already says 'Calc' on frame one
%ifdef CALF_ABOUT
    mov si, cal_about
    call OSAPI_ABOUT_SET            ; ...and 'About Calc' above its Close
%endif                              ; (SPEC.md 12.2/12.7)
    mov ax, cal_onup
    call OSAPI_WM_ONMOUSEUP         ; SPEC.md 13.7: a key fires on the RELEASE,
                                    ; over the key the press landed on, so a
                                    ; mis-aimed press can be slid off
    mov ax, cal_ondrag
    call OSAPI_WM_ONDRAG            ; ...and SPEC.md 13.8.2: the key it landed
                                    ; on FOLLOWS the pointer between the two
                                    ; edges, so sliding off says the gesture is
                                    ; cancelled before the button comes up
    mov ax, cal_ontimer
    call OSAPI_WM_ONTIMER           ; ...and SPEC.md 13.9's one-shot, which is
                                    ; how a key pressed from the KEYBOARD gets
                                    ; back up: it has no release edge, and a
                                    ; callback cannot sleep
    mov ax, cal_sync
    call OSAPI_WM_ONRESIZE          ; SPEC.md 11.98: [cal_nvis] is derived from
                                    ; the content box and KEPT, so the box
                                    ; moving under us - an adapter switch, a
                                    ; drag across an extended desktop's seam -
                                    ; has to re-derive it BEFORE the repaint
    mov al, 1
    call OSAPI_WM_SNAP              ; every pen here is content + a multiple of
                                    ; 8, so this is what buys the single-store
                                    ; font_run path (SPEC.md 6.1/11.94)
    mov al, 1
    call OSAPI_WM_SAVEU             ; ...and our content genuinely does not
                                    ; change while we are not drawing (SPEC.md
                                    ; 11.96.1): no worker, no clock, nothing
                                    ; here moves but on a click or a key
    clc                             ; THE ANSWER IS STATED, not carried: the
                                    ; CF wm_create gave does NOT survive the
                                    ; calls above on kern_small, where
                                    ; OSAPI_WM_ONDRAG and _ONTIMER refuse with
                                    ; CF = 1 (SPEC.md 13.8.2/13.9) - so the
                                    ; refusal of a feature we do without came
                                    ; back here as LD_EABORT, and the small
                                    ; build of Calculator never opened at all
.out:
    pop si
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; =============================================================================
; Geometry and layout
; =============================================================================

; -----------------------------------------------------------------------------
; cal_sync - read the live content box and derive everything that moves
; in:  any context (the window is [cal_win], not an argument)
; out: [cal_ox]/[cal_oy] the content origin, [cal_ch] its height,
;      [cal_nvis] the history rows that FIT; all registers preserved
;
; ALSO THE OSAPI_WM_ONRESIZE HANDLER (SPEC.md 11.98), as it stands: the box
; moved and we did not ask - an Activate Mode switch, or a drag across an
; extended desktop's seam onto the shorter display (SPEC.md 39.16.3) - and
; [cal_nvis] is the one thing here derived once and kept, so it is the one
; thing that has to be put right before the kernel's repaint. Drawing is
; forbidden there and this draws nothing.
;
; The window is read out of [cal_win] rather than handed in, because it is
; the only window there is - and every callback spelled `mov bx, si` to hand
; it (apps size pass 1).
;
; The one derived number is [cal_nvis], and deriving it rather than assuming
; it is the whole of what makes this app correct on a CGA (SPEC.md 65.3): the
; window ASKS for CAL_OPEN_H and takes whatever the kernel's clamp granted.
; The WIDTH is a constant because the frame is 226px and the narrowest screen
; this OS drives is 640 (SPEC.md 39), so wm_fit can only ever move it.
;
; THIS RUNS ON EVERY CALLBACK - every key, every press, every W_ONDRAG of a
; pointer crossing the pad. It used to rebuild a 29-rect table each time as
; well (cal_layout, a 16x16 MUL per key: ~2,500 cycles of a 4.77MHz 8088 that
; every keystroke paid before anything was drawn); the rects are computed now
; when one is wanted (cal_rect).
; -----------------------------------------------------------------------------
cal_sync:
    push ax
    push bx
    push cx
    push dx
    mov bx, [cal_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [cal_ox], ax
    mov [cal_oy], dx
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    mov [cal_ch], dx
%ifdef CALF_HIST
    xor ax, ax                      ; folded up: no rows, and no arithmetic
    cmp byte [cal_open], 0
    je .set
    sub dx, CAL_BASE_H              ; ...else as many whole rows as are left
    jbe .set                        ; over (a machine that granted us nothing
    xchg ax, dx                     ; extra is the CGA case, not an error);
    mov cl, CAL_ROW_H               ; under 512 rows, so a byte divide holds
    div cl                          ; the quotient
    mov ah, 0
    cmp al, CAL_HIST_N
    jbe .set
    mov al, CAL_HIST_N
.set:
    mov [cal_nvis], ax
%endif                              ; with no pane the count is a constant 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_rect - control AX's rect, in SCREEN coordinates
; in:  AX = the control's index, 0..CAL_NRECT-1; [cal_ox]/[cal_oy] current
; out: BX = cal_rc, holding {x1,y1,x2,y2} inclusive; all other registers
;      preserved
;
; THE ONE DESCRIPTION OF WHERE A CONTROL IS, read by the drawing (cal_btn,
; whose os88ui_btn record points at cal_rc) and by the hit test (cal_hit,
; which only ever ASKS this routine whether its candidate holds the point) -
; so the drawn control and the clickable control cannot drift: SPEC.md 22's
; fm_hit discipline. It was a table of all 29 rects, rebuilt on every
; callback: 232 bytes of every instance, and a rebuild every keystroke paid
; (apps size pass 1).
;
; A history row's rect is EXACTLY the run that draws it - 26 cells wide and
; eight rows tall, not the nine-row pitch and not the window's full width. A
; rect wider than what puts the row back is a press that INVERTS pixels the
; release cannot restore, and they stay inverted until something else
; repaints: measured at 276 of them, in the margins either side and the row
; of leading below. The strip is the keypad's full width, so the label and
; the triangle are both inside one target.
; -----------------------------------------------------------------------------
cal_rect:
    push ax
    push cx
    push dx
%ifdef CALF_HIST
    cmp al, CAL_NKEY
    jae .pane
%endif
    mov cl, al                      ; a KEY: column and row out of the index
    and cl, CAL_KEY_COLS - 1
    shr al, 1
    shr al, 1
    mov ah, CAL_KEY_PIT
    mul ah
    add ax, CAL_KEY_Y0
    xchg dx, ax                     ; DX = y1, content-relative
    mov al, CAL_BTN_W + CAL_BTN_HG
    mul cl
    add ax, CAL_KEY_X0              ; AX = x1
    mov cx, (CAL_BTN_W - 1) | ((CAL_BTN_H - 1) << 8)
%ifdef CALF_HIST
    jmp short .put
.pane:
    mov dx, CAL_HDR_Y1              ; the STRIP...
    mov cx, (CAL_CW - 1 - 2 * CAL_KEY_X0) | ((CAL_HDR_Y2 - CAL_HDR_Y1) << 8)
    sub al, CAL_R_HIST
    jb .strip
    mov ah, CAL_ROW_H               ; ...or a history ROW
    mul ah
    add ax, CAL_HIST_Y0
    xchg dx, ax
    mov ax, CAL_HROW_X
    mov cx, (CAL_HROW_N * 8 - 1) | (7 << 8)
    jmp short .put
.strip:
    mov ax, CAL_KEY_X0
%endif
.put:                               ; AX = x1, DX = y1, CL = width - 1, CH =
    mov bx, cal_rc                  ; height - 1
    add ax, [cal_ox]
    add dx, [cal_oy]
    mov [bx+0], ax
    mov [bx+2], dx
    push cx
    mov ch, 0
    add ax, cx
    mov [bx+4], ax
    pop cx
    mov cl, ch
    mov ch, 0
    add dx, cx
    mov [bx+6], dx
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_hit - which control is the point on?
; in:  CX = x, DX = y (SCREEN, and possibly outside the window); [cal_ox]/
;      [cal_oy]/[cal_nvis] current
; out: AX = the control's index + 1 (os88ui_bfind's sentinel), or 0 for none;
;      clobbers BX
;
; ARITHMETIC NOMINATES, cal_rect DECIDES. The row and column the point is in
; give at most ONE candidate - a key, the strip, or a history row on show -
; and os88ui_bhit against that candidate's own rect is the answer, so a gap
; between keys, the rule above the strip and a row folded out of sight are
; all refused by the same rect the painter draws. It was a walk of all 29
; rects in the table (os88ui_bfind).
; -----------------------------------------------------------------------------
cal_hit:
    mov ax, dx
    sub ax, [cal_oy]
    sub ax, CAL_KEY_Y0              ; y within the keypad, signed
    jl .none
    cmp ax, CAL_KEY_ROWS * CAL_KEY_PIT
    jae .pane
    mov bl, CAL_KEY_PIT
    div bl
    mov bh, al                      ; BH = the row
    mov ax, cx
    sub ax, [cal_ox]
    sub ax, CAL_KEY_X0
    jl .none
    cmp ax, CAL_KEY_COLS * (CAL_BTN_W + CAL_BTN_HG)
    jae .none
    mov bl, CAL_BTN_W + CAL_BTN_HG
    div bl                          ; AL = the column
    shl bh, 1
    shl bh, 1
    add al, bh
    jmp short .try
.pane:
%ifdef CALF_HIST
    sub ax, CAL_HIST_Y0 - CAL_KEY_Y0
    jl .strip                       ; above the rows: the strip, if anything
    mov bl, CAL_ROW_H
    div bl                          ; AL = the row (under 60 on any screen)
    mov ah, 0
    cmp ax, [cal_nvis]
    jae .none                       ; not on show: not there
    add al, CAL_R_HIST
    jmp short .try
.strip:
    mov al, CAL_R_HDR
%else
    jmp short .none                 ; no pane: nothing below the keypad
%endif
.try:
    mov ah, 0
    push ax
    call cal_rect                   ; BX = the candidate's rect
    call os88ui_bhit                ; CF = 0: inside it
    pop ax
    jc .none
    inc ax
    ret
.none:
    xor ax, ax
    ret

; =============================================================================
; Callbacks
; =============================================================================

; -----------------------------------------------------------------------------
; cal_onclick - W_ONCLICK: arm the control the press landed on, and show it
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; A press ACTS ON NOTHING (SPEC.md 13.7). All it does is arm the control and
; draw it down, so sliding off cancels.
; -----------------------------------------------------------------------------
cal_onclick:
    push ax
    push bx
    call cal_sync                   ; the window may have moved since the last
                                    ; paint; CX/DX are already SCREEN coords,
                                    ; which is what cal_hit wants
    call cal_abdismiss              ; the credits are up: this press is spent
    jc .out                         ; taking them down and arms nothing, so
                                    ; the release that follows fires nothing
    call cal_hit                    ; AX = index + 1, 0 = none
    call os88ui_arm
    call cal_setdown                ; ...and the pressed look, through the one
                                    ; writer - 0 is a legal argument, so the
.out:                               ; press that lands on nothing needs no test
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_onup - W_ONMOUSEUP (SPEC.md 13.7): the control fires HERE
; in:  CX = x, DX = y (SCREEN, and possibly outside the window), SI = window;
;      UI task, gfx lock held
; out: nothing; preserves all registers
;
; The armed control is put back FIRST and unconditionally, and it is redrawn
; properly rather than XOR-ed a second time. That is the whole reason this is
; not symmetric with the press: a repaint arriving between the two - the
; window dragged, another window closing over it - takes the inversion off
; the glass, and a second XOR would then invert a control that was already
; upright and leave it that way for the session.
; -----------------------------------------------------------------------------
cal_onup:
    push ax
    push bx
    push di
    call cal_sync
    call os88ui_fire                ; AX = what the press armed, and it is
    or ax, ax                       ; CLEARED - one release per press, so
    jz cal_cbret                    ; there is no stale arm to guard against
    xchg di, ax
    xor ax, ax
    call cal_setdown                ; ...upright again, FIRST and whatever
                                    ; happens next (SPEC.md 13.8.2)
    call cal_hit                    ; AX = the control under the RELEASE
    cmp ax, di
    jne cal_cbret                   ; a different one, or none: cancelled
    dec ax
%ifdef CALF_HIST
    cmp ax, CAL_NKEY
    jb .keypad
    je .hdr
    sub ax, CAL_R_HIST              ; AX = the history row clicked
    call cal_hist_load
    jmp short cal_cbret
.hdr:
    call cal_hist_toggle            ; resizes AND repaints: nothing after it
    jmp short cal_cbret
.keypad:
%endif                              ; ...and with no pane there is no rect
    xchg bx, ax                     ; above the keypad that could have been hit
    mov al, [cal_kcodes+bx]
    call cal_key
    jmp short cal_cbret

; -----------------------------------------------------------------------------
; cal_ondrag - W_ONDRAG (SPEC.md 13.8.2): the pointer moved, press still down
; in:  CX = x, DX = y (SCREEN, and possibly outside the window), SI = window;
;      UI task, gfx lock held
; out: nothing; preserves all registers
;
; THE PRESSED CONTROL FOLLOWS THE POINTER: down while it is over the control
; the press landed on, up the moment it is not, down again on the way back.
; That is the half of the down state that is worth having - it says the
; gesture is CANCELLED before the user commits to it, which is the whole
; reason press-and-slide-off is a usable way to change your mind.
;
; It is the same question cal_onup asks at the release, one pass early, and
; it is deliberately the same two calls in the same order - cal_hit
; against the armed control - so what is lit is exactly what would fire. The
; kernel decides none of that: it says only "the pointer is here and your
; press is live" (13.8.2), because there is no widget layer for it to ask.
;
; os88ui_armed rather than os88ui_fire: the arm has to survive this and be
; there for the release. Spending it here would leave the release with
; nothing armed and the calculator would take no input at all.
; -----------------------------------------------------------------------------
cal_ondrag:
    push ax
    push bx
    push di
    call cal_sync                   ; the window can have been dragged since
                                    ; the press; CX/DX are already SCREEN
                                    ; coordinates, which is what bfind wants
    call os88ui_armed               ; AX = what the press armed, still armed
    or ax, ax
    jz cal_cbret                    ; the press landed on nothing: nothing to
    xchg di, ax                     ; track, and no repaint owed
    call cal_hit                    ; AX = what is under the pointer NOW
    cmp ax, di
    je .set                         ; back on it (or never left)
    xor ax, ax                      ; off it: nothing down
.set:
    call cal_setdown                ; ...which draws only if that CHANGED
cal_cbret:                          ; (cal_onup's way out too)
    pop di
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_onkey - W_ONKEY: the whole keypad from the keyboard
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The arrow keys are deliberately unbound: on a machine with no mouse they
; ARE the mouse (SPEC.md 9.6), and a calculator has nothing to do with them.
; -----------------------------------------------------------------------------
cal_onkey:
    push ax
    call cal_sync
    call cal_abdismiss              ; any key takes the credits down, and is
    jc .out                         ; itself swallowed doing it
    call cal_asckey                 ; AL = CK_*, or CK_NONE
    cmp al, CK_NONE
    je .out
%ifdef CALF_HIST
    cmp al, CK_HIST
    je .hist
%endif
    cmp al, CK_COPY                 ; the clipboard verbs are the menu's own
    je .copy                        ; routines: one implementation each, the
    cmp al, CK_PASTE                ; key and the item differing only in how
    je .paste                       ; they arrive (SPEC.md 65.5)
    call cal_flash                  ; the key on the pad goes DOWN (SPEC.md
                                    ; 65.9) - BEFORE the action, so a verb that
                                    ; repaints puts the pressed key back up
                                    ; itself rather than over the top of it
    call cal_key
.out:
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)
%ifdef CALF_HIST                    ; ...and the three that are not engine
.hist:                              ; keys go straight on to a routine that
    pop ax                          ; saves its own registers
    jmp cal_hist_toggle
%endif
.copy:
    pop ax
    jmp cal_copy
.paste:
    pop ax
    jmp cal_paste

; -----------------------------------------------------------------------------
; cal_flash - press key AL on the pad and arm the timer to let it go
; in:  AL = CK_*; the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; A typed key had nothing to show for it before this: the display changed and
; the key that produced it did not move (SPEC.md 65.9). The kernel's one-shot
; timer (SPEC.md 13.9) is what makes it possible at all - a callback cannot
; sleep, holding the gfx lock as it does, and the alternative was a worker
; task for a 165ms flash.
;
; A code with no key on the pad - Q, R, % are Math-menu verbs - flashes
; NOTHING rather than the nearest thing, which is what cal_keyrect's 0 means.
; -----------------------------------------------------------------------------
cal_flash:
    push ax
    push bx
    call cal_keyrect                ; AX = index + 1, 0 = not on the pad
    or ax, ax
    jz .out
    push ax
    mov bx, [cal_win]
    mov ax, CAL_FLASH
    call OSAPI_WM_TIMER             ; a REPEAT re-arms this rather than
    pop ax                          ; stacking, the timer being one-shot and
    jc .out                         ; per-window: a held key stays lit and goes
                                    ; up CAL_FLASH ticks after the last repeat.
                                    ; ARMED FIRST, because kern_small has no
                                    ; timer (CF = 1, SPEC.md 13.9): a key put
                                    ; down with nothing to bring it back up
                                    ; would stay drawn pressed, so there the
                                    ; keyboard flashes nothing
    call cal_setdown                ; ...through the ONE writer, as every mouse
.out:                               ; edge does (SPEC.md 65.8)
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_ontimer - W_ONTIMER (SPEC.md 13.9): the flash is over
; in:  CX = DX = 0, SI = window; UI task, gfx lock held
; out: nothing; preserves all registers
;
; IT DEFERS TO THE MOUSE, and that is the whole of it. Type a key and then
; press a mouse button on the pad inside the flash window, and an
; unconditional clear here springs the HELD key up under a finger that is
; still down - SPEC.md 13.8.2's defect arriving from the clock instead of from
; a repaint. os88ui_armed answers whether a gesture is still running, and a
; timed un-draw does not outrank one that is.
; -----------------------------------------------------------------------------
cal_ontimer:
    push ax
    call os88ui_armed
    or ax, ax
    jnz .out                        ; a press is live: the release owns the
    call cal_sync                   ; down state and will put it up itself
    call cal_setdown                ; (AX = 0: nothing down) - the window may
.out:                               ; have moved since the key
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_keyrect - which keypad key produces code AL?
; in:  AL = CK_*
; out: AX = the key index + 1 (os88ui_bfind's sentinel), or 0 for none;
;      preserves all other registers
;
; The reverse of cal_kcodes, WALKED rather than tabulated: the table is twenty
; bytes and this runs once per keystroke, on a machine where the keystroke
; itself costs milliseconds. A second table would be a second description of
; the same pairing, which is exactly what cal_kcodes exists to prevent. Walked
; from the END, so the counter IS the answer: no code is on two keys.
; -----------------------------------------------------------------------------
cal_keyrect:
    push bx
    mov bx, CAL_NKEY
.next:
    cmp al, [cal_kcodes-1+bx]
    je .hit
    dec bx
    jnz .next
.hit:
    xchg ax, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cal_asckey - one ASCII byte to a key code
; in:  AL = ascii
; out: AL = CK_*, or CK_NONE; preserves all other registers
; -----------------------------------------------------------------------------
cal_asckey:
    cmp al, '0'
    jb .table
    cmp al, '9'
    ja .table
    sub al, '0'                     ; digits ARE their codes (the CK_* block)
    ret
.table:
    push si
    mov si, cal_asctab - 2
.scan:
    inc si
    inc si
    cmp byte [si], 0                ; the end, whose pair is {0, CK_NONE}
    je .hit
    cmp [si], al
    jne .scan
.hit:
    mov al, [si+1]
    pop si
    ret

; -----------------------------------------------------------------------------
; cal_oncmd - AM_ONCMD: a menu command (SPEC.md 12.2)
; in:  AL = item index, AH = menu index, SI = our window, BX = the set;
;      UI task, gfx lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI like any window callback
;
; Called exactly like W_ONCLICK, so it draws directly and must repaint
; itself - the kernel does not repaint after a menu command returns. Every
; item here is also a key, so all but the two clipboard verbs end up in
; cal_key and there is one implementation of each. Each arm is the routine's
; last call, so each is a jump.
; -----------------------------------------------------------------------------
cal_oncmd:
    xchg cx, ax                     ; CL = the item, CH = the menu
    call cal_sync
    call cal_abdismiss              ; a menu pick takes the credits down first
    or ch, ch                       ; and then does what it was asked - a
    jz .edit                        ; deliberate command is not a dismissal
    dec ch                          ; (from here CH = 0, so CX IS the item)
    jz .math
    ; --- View -----------------------------------------------------------------
%ifdef CALF_HIST
    jcxz .hist
    mov word [cal_hn], 0            ; Clear History: [cal_hn] and [cal_hhead]
    jmp cal_repaint                 ; in one store. A rare command, and the
.hist:                              ; band has to be erased rather than merely
    jmp cal_hist_toggle             ; re-lettered
%endif   ; CALF_HIST - the View menu is the pane's, both items
    ; --- Edit -----------------------------------------------------------------
.edit:
    jcxz .copy
    jmp cal_paste
.copy:
    jmp cal_copy
    ; --- Math -----------------------------------------------------------------
.math:
    mov bx, cx
    mov al, [cal_mathk+bx]
    jmp cal_key

; =============================================================================
; 'About Calc' - the credit panel (SPEC.md 12.2/65)
; =============================================================================
; The STANDARD card, os88ui_about (SPEC.md 20.5.1.1): this package was one of
; the seventeen private implementations that section names, and its own
; measure-and-draw pair was ~250 bytes for a white fill, a black frame and a
; column of centred lines - the card the library draws, a pixel or two of
; spacing apart (apps size pass 1).
;
; It is STATE and not a modal loop. [cal_abon] goes up, the card is drawn on
; top, and the next click, key or menu pick takes it down again - a loop here
; would hold the gfx lock against every background task for as long as the
; reader left the credits up. The library draws; this package remembers, which
; is that section's own division of the work.

; -----------------------------------------------------------------------------
; cal_about - the OSAPI_ABOUT_SET handler (slot 0x018A)
; in:  SI = our window ptr; the UI task, gfx lock HELD, far-called at our
;      segment - a window callback in every respect that matters, so it draws
;      directly
; out: nothing; preserves all registers
;
; Only the CARD is drawn: nothing underneath it has changed, and the card is
; opaque over its own rect, so repainting the content first would be
; PERFORMANCE.md's double-draw flash with the credits as the second layer.
; os88ui_about and not _d: nothing has armed a clip in an About handler, and
; the library arms one itself.
; -----------------------------------------------------------------------------
%ifndef CALF_ABOUT
; No About card (APP_SMALL). cal_abdismiss is called by the click, key and
; menu paths before they do anything else and answers CF = 1 when it SPENT the
; event taking the card down - so with no card it must answer "not spent".
cal_abdismiss:
    clc
    ret
%endif

%ifdef CALF_ABOUT
cal_about:
    mov byte [cal_abon], 1
    push bx
    push si
    mov bx, [cal_win]
    mov si, cal_ablines
    call os88ui_about               ; CF = 1: not one pixel of us is visible,
    pop si                          ; and the next W_PAINT puts it up instead
    pop bx
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_abdismiss - take the card down, if it is up
; in:  the layout is current; gfx lock held
; out: CF = 1 if it WAS up (and the content has been repainted), so the caller
;      swallows the click or key that dismissed it; CF = 0 otherwise. Every
;      register preserved.
;
; The way back is cal_repaint and not the card's inverse: the card covered
; the display well, whose shadows (cal_shadkill) and whose keypad have to be
; put back whole rather than in the shape of the thing that covered them.
; -----------------------------------------------------------------------------
cal_abdismiss:
    cmp byte [cal_abon], 0
    je .none
    mov byte [cal_abon], 0
    call cal_repaint
    stc
    ret
.none:
    clc
    ret
%endif   ; CALF_ABOUT

; =============================================================================
; Drawing (SPEC.md 65.4.2)
; =============================================================================

; -----------------------------------------------------------------------------
; cal_paint - W_PAINT: full content repaint
; in:  SI = window ptr; caller holds the gfx lock, content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_paint:
    call cal_sync
    jmp short cal_drawall           ; ...which preserves everything itself

; -----------------------------------------------------------------------------
; cal_repaint - erase the content and draw the lot
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; W_PAINT does NOT come through here: the kernel has already whitened the
; content in front of it (SPEC.md 11.90.1), and filling it a second time is
; PERFORMANCE.md's double-draw flash with our own picture as the second
; layer. This entry is for the paths that repaint with no kernel fill in
; front of them - a menu command, and the About card coming down.
; -----------------------------------------------------------------------------
cal_repaint:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [cal_ox]
    mov bx, [cal_oy]
    mov cx, ax
    add cx, CAL_CW - 1
    mov dx, bx
    add dx, [cal_ch]
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax                          ; ...and on into the drawing

; -----------------------------------------------------------------------------
; cal_drawall - every pixel of the content, over ground that is already white
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_drawall:
    push ax
    push bx
    push cx
    push dx
    push si

    call cal_shadkill               ; the glass is about to disagree with both
                                    ; shadows, so the next emit must be whole
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [cal_ox]                ; the display well
    mov bx, [cal_oy]
    mov cx, ax
    mov dx, bx
    add ax, CAL_DISP_X1
    add bx, CAL_DISP_Y1
    add cx, CAL_DISP_X2
    add dx, CAL_DISP_Y2
    call OSAPI_GFX_FRAME
    call cal_show                   ; ...and both fields inside it

    xor ax, ax                      ; the twenty keys
.key:
    clc                             ; upright, and no erase owed: the kernel
    call cal_btn                    ; whited this content before W_PAINT
    inc ax
    cmp ax, CAL_NKEY
    jb .key

%ifdef CALF_HIST
    call cal_draw_hdr
    call cal_draw_rows
%endif

%ifdef CALF_ABOUT
    cmp byte [cal_abon], 0          ; ...and the credit card over the lot, or
    je .noab                        ; a W_PAINT under it would lose it. The
    mov bx, [cal_win]               ; PAINTER's entry: a W_PAINT has its clip
    mov si, cal_ablines             ; armed already, and re-arming would throw
    call os88ui_about_d             ; the damage away (SPEC.md 20.5.1.1).
.noab:                              ; cal_repaint, the other way here, only
%endif                              ; runs with the card already down
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_btn - draw keypad button AX in the state [cal_down] says it is in
; in:  AX = the key index 0..19, CF = 1 to erase its interior first; the
;      layout is current; gfx lock held
; out: nothing; preserves all registers
;
; OS88UI_FILL rides in on CF because the flag's own question is "can this be
; drawn a second time without the ground being repainted first" (os88ui.inc),
; and the two callers give different answers: cal_drawall's ground is white
; already, cal_ctl's is a button that was drawn the other way up.
;
; The DOWN state comes from [cal_down] rather than from the caller, so that
; every painter agrees with it for free - a W_PAINT mid-gesture included
; (SPEC.md 13.8.2) - and so a press and a release are the SAME call.
; -----------------------------------------------------------------------------

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; One button at a time: this package's rects are not one contiguous group,
; so the record is pointed at whichever rect the caller staged.
cal_btlbl: dw 0
cal_btflg: dw 0
    OS88UI_BTNREC cal_btrec, cal_rc, cal_btlbl, cal_btflg, 1

cal_btn:
    push ax
    push bx
    push di
    sbb di, di                      ; DI = FFFFh on CF, 0 without
    and di, OS88UI_FILL
    inc ax
    cmp ax, [cal_down]
    jne .up
    or di, OS88UI_DOWN              ; DOWN implies the erase by itself, so the
.up:                                ; fill flag only ever ADDS to it
    dec ax
    mov [cal_btflg], di             ; THE ONE CONTROL (SPEC.md 20.5.1.3): the
    call cal_rect                   ; record takes the flags, the rect (it
    mov bx, ax                      ; points at cal_rc) and the cap, and the
    shl bx, 1                       ; picture is identical
    shl bx, 1
    add bx, cal_klbl                ; the cap, a four-byte slot a key
    mov [cal_btlbl], bx
    mov bx, cal_btrec
    mov al, 1
    call os88ui_btn
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_ctl - draw control AX-1 as [cal_down] says it is: pressed if it is the
;           one down, upright otherwise
; in:  AX = index + 1 (os88ui_bfind's answer, never 0); the layout is current;
;      gfx lock held
; out: nothing; preserves all registers
;
; ONE ROUTINE FOR THE PRESS AND THE RELEASE. They were two, cal_invert and
; cal_restore, and every arm of them already drew from the variable or could:
; a key is os88ui_btn with or without OS88UI_DOWN, a history row is its run
; with the two colours swapped, and both of those READ [cal_down] - so the
; only thing the pair still decided was which way round to draw the strip
; (apps size pass 1).
;
; NO BUTTON IS XOR-ED (SPEC.md 13.8): the same primitive over the same rect
; draws it down and draws it up, which is what cal_draw_row had always been
; doing for a history row and for the reason SPEC.md 65.4 records - an XOR and
; a redraw are two descriptions of one control that have to agree by hand.
;
; The header strip keeps its XOR, and that is not an oversight. It is a
; disclosure rule with a triangle and a label in it, not a framed button, so
; os88ui_btn has nothing to say about it - and its way back up is the
; erase-and-redraw pair rather than a second XOR.
;
; NOT CALLED DIRECTLY: cal_setdown owns it, because the down state is a
; VARIABLE (SPEC.md 13.8.2) and two callers moving it independently is how it
; parts from [cal_down].
; -----------------------------------------------------------------------------
cal_ctl:
    push ax
    dec ax
    cmp ax, CAL_NKEY
    jb .key
%ifdef CALF_HIST
    je .hdr
    sub ax, CAL_R_HIST              ; a history ROW: the same opaque pass over
    call cal_draw_row               ; the same cells either way up
    jmp short .out
.hdr:
    push bx
    push cx
    push dx
    mov al, CAL_R_HDR               ; (AH is 0: AX was an index + 1)
    call cal_rect
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov bx, [bx+2]
    cmp word [cal_down], CAL_R_HDR + 1
    jne .hup
    call OSAPI_GFX_XOR_FILL         ; down: the strip inverted
    jmp short .hdone
.hup:                               ; up: the strip is a generous target and
    push ax                         ; what is DRAWN in it is a triangle and a
    mov al, CWHITE                  ; label, so unlike a row it does not cover
    call OSAPI_SET_COLOR            ; its own rect - the inversion has to be
    pop ax                          ; erased before cal_draw_hdr puts the two
    call OSAPI_GFX_FILL             ; back. One fill, on the release path only:
    call cal_draw_hdr               ; the full-paint path draws over ground the
.hdone:                             ; kernel has already whitened (11.90.1)
    pop dx
    pop cx
    pop bx
    jmp short .out
%endif
.key:
    stc                             ; the erase is not optional: the ground
    call cal_btn                    ; under it is the button the other way up
.out:                               ; (os88ui.inc's OS88UI_FILL note)
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_setdown - MOVE the pressed state to control AX (0 = none)
; in:  AX = index + 1, or 0 for "nothing is down"; the layout is current;
;      gfx lock held
; out: nothing; preserves all registers
;
; THE ONE PLACE THE DOWN STATE MOVES, and the one writer of [cal_down]. All
; three edges come through it - the press, the tracking edge and the release -
; so "which control is drawn down" and "which control [cal_down] says is down"
; cannot part.
;
; It draws NOTHING when the state has not changed, and that is not an
; optimisation here, it is the thing that makes W_ONDRAG affordable at all: the
; edge arrives per pointer movement, and a pointer crossing a keypad delivers
; one every few milliseconds. Redrawing two buttons per movement would be tens
; of milliseconds of repaint per mouse packet on the field machine (SPEC.md
; 13.8.2's third rule); comparing first makes the common case - the pointer
; moving INSIDE the control it pressed - cost one compare.
; -----------------------------------------------------------------------------
cal_setdown:
    cmp ax, [cal_down]
    je .out
    push ax
    xchg ax, [cal_down]             ; AX = the OLD one, and the variable is
    or ax, ax                       ; already the new one - so the old one is
    jz .new                         ; drawn up, and a repaint racing this reads
    call cal_ctl                    ; the new state whichever side of the draw
.new:                               ; it lands on
    mov ax, [cal_down]
    or ax, ax
    jz .done
    call cal_ctl                    ; ...and the new one drawn down
.done:
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; cal_draw_hdr - the disclosure rule, triangle and label
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; The triangle is a picture rather than a glyph, because the kernel's font is
; ASCII 32..126 and has no arrow in it (font.inc) - an 8x8 band put down with
; ONE OSAPI_GFX_BLIT1 (cal_tri_shut's note). The blit takes the RESTING PEN,
; white for a set bit, which every callback starts with (SPEC.md 5.4.2.2.2)
; and which nothing here moves. The label is static - the triangle's
; DIRECTION is the state - so nothing here changes on a fold and the strip is
; drawn once per repaint.
; -----------------------------------------------------------------------------
%ifdef CALF_HIST
cal_draw_hdr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov al, CLGRAY                  ; the separator: a 50% dither, which is the
    call OSAPI_SET_COLOR            ; dotted rule this OS's model draws (39.4)
    mov si, [cal_ox]
    mov di, [cal_oy]
    lea ax, [si+CAL_DISP_X1]
    lea bx, [di+CAL_HDR_RULE]
    lea cx, [si+CAL_DISP_X2]
    mov dx, bx
    call OSAPI_GFX_FILL

    lea cx, [si+CAL_HDR_TX]         ; the label
    lea dx, [di+CAL_HDR_TY]
    push si
    mov si, cal_s_hist
    mov ax, CBLACK + CWHITE * 256
    call OSAPI_FONT_RUN
    pop si

    lea ax, [si+CAL_TRI_X]          ; ...and the triangle, on the label's row
    mov bx, dx
    mov si, cal_tri_shut
    cmp byte [cal_open], 0
    je .tri
    mov si, cal_tri_open
.tri:
    push ds
    pop es                          ; ES:SI, the band in our own segment
    mov cx, 8
    mov dx, cx
    mov bp, 1                       ; a byte a row
    call OSAPI_GFX_BLIT1

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
; cal_draw_rows - every visible history row
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_draw_rows:
    push ax
    xor ax, ax
.row:
    cmp ax, [cal_nvis]
    jae .out
    call cal_draw_row
    inc ax
    jmp short .row
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_draw_row - one history row, as ONE opaque run, in the state [cal_down]
;                says it is in
; in:  AX = the screen row; gfx lock held
; out: nothing; preserves all registers
;
; Row 0 is the newest. The text was space-padded to CAL_HROW_N at the moment
; the sum was done (cal_hist_push), so the padding is the erase and there is
; no fill here at all - the row is never momentarily blank, and a row past
; the end of the ring draws as CAL_HROW_N spaces, which is exactly the erase
; a shortened list needs (SPEC.md 6.1).
;
; **THE PRESSED STATE IS THIS RUN WITH THE TWO COLOURS SWAPPED, NOT AN XOR OF
; THE ROW'S RECT**, and that is a correctness fix rather than a tidy-up
; (SPEC.md 65.4). `font_run` already takes an ink AND a background
; (SPEC.md 27.8.2 makes the same point), so inverting is the SAME primitive
; over the SAME cells - which is what makes the press and the release
; symmetrical by construction instead of by two routines agreeing about a
; rect. With an XOR they did not: measured on a CGA, a row clicked with the
; pointer resting on it came back with THE ARROW'S TOP THREE ROWS MISSING -
; and those three were exactly the arrow rows that fell inside the XOR'd
; rect, the rest of it being below the row's last line. A keypad button,
; whose release redraws through gfx_fill, showed nothing; so did a font_run
; landing under the arrow with no XOR anywhere near it.
;
; WHICH WAY UP IS READ HERE, off [cal_down], and not handed in: it was a CF
; argument, and the loop in cal_draw_rows reached it through a `cmp` / `jae`
; that leaves CF SET on the branch that falls through - so every row of a
; full repaint drew inverted until every call site said `clc` or `stc`. A
; variable every painter reads cannot be inherited from the wrong compare.
; -----------------------------------------------------------------------------
cal_draw_row:
    push ax
    push bx
    push cx
    push dx
    push si
    call cal_hslot                  ; SI = its text, or 0 for an empty row
    or si, si
    jnz .have
    mov si, cal_blankrow
.have:
    push ax
    mov dl, CAL_ROW_H
    mul dl
    add ax, CAL_HIST_Y0
    add ax, [cal_oy]
    xchg dx, ax                     ; DX = the row's y
    pop ax
    mov cx, [cal_ox]
    add cx, CAL_HROW_X
    add ax, CAL_R_HIST + 1          ; the HELD row is drawn held, so a repaint
    cmp ax, [cal_down]              ; mid-gesture agrees with [cal_down] the
    mov ax, CBLACK + CWHITE * 256   ; way cal_btn does: ink black on white...
    jne .pen
    xchg al, ah                     ; ...or the pressed look, same pass
.pen:
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_hslot - the ring slot behind screen row AX
; in:  AX = the screen row (0 = newest)
; out: SI = its 27-byte text and BX = its slot index, or SI = 0 when that row
;      is past the end of the ring; all other registers preserved
;
; A ring, so a new sum moves ONE byte ([cal_hhead]) rather than shuffling
; eight lines of text - the pixels are the thing that has to move, and
; cal_hist_push moves those with one blit.
; -----------------------------------------------------------------------------
cal_hslot:
    cmp al, [cal_hn]
    jae .none
    push ax
    mov bl, [cal_hhead]             ; the newest is at [cal_hhead]; row i is i
    sub bl, al                      ; slots back from it, around the ring
    and bl, CAL_HIST_N - 1
    mov bh, 0
    mov al, CAL_HROW_N + 1
    mul bl
    add ax, cal_htext
    xchg si, ax
    pop ax
    ret
.none:
    xor si, si
    ret

%endif   ; CALF_HIST
; -----------------------------------------------------------------------------
; cal_show - both display fields, emitting only the cells that changed
; in:  gfx lock held
; out: nothing; preserves all registers
;
; THE KEYSTROKE PATH: every key ends here. The save is this routine's and
; nothing under it keeps a register (cal_compose, cal_emit), which is both
; fewer bytes and fewer cycles than each of them banking the lot.
; -----------------------------------------------------------------------------
cal_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cal_compose                ; the two fields, as they SHOULD read, and
    mov ax, CAL_CTX_X               ; CX = their width
    mov si, cal_cbuf
    mov di, cal_cshad
    call cal_emit
    mov ax, CAL_NUM_X
    mov si, cal_nbuf
    mov di, cal_nshad
    call cal_emit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%if CAL_CTX_N != CAL_NUM_N
 %error "cal_show hands both fields one width, and the bss gives them one"
%endif

; -----------------------------------------------------------------------------
; cal_emit - draw the cells of a fixed-width field that differ from the glass
; in:  SI = the composed field, DI = the shadow of what is DRAWN, CX = cells,
;      AX = the field's content-relative pen x; gfx lock held
; out: nothing; preserves CX, SI and DI; clobbers AX, BX, DX
;
; The shadow is the record of the GLASS, so composing IS the diff and no
; second structure can fall out of step (SPEC.md 12.9's menu_bcell is the
; same argument one layer up). BOTH ends are found rather than just the
; first, so backspacing a digit off a right-aligned number redraws the two
; cells that moved and not the eleven behind them.
;
; The span's two ends, the banked byte and the pen live in registers and on
; the stack, where they were four words of bss.
; -----------------------------------------------------------------------------
cal_emit:
    push cx
    xor bx, bx                      ; BX indexes BOTH buffers throughout: an
.first:                             ; 8086 effective address is base + index
    cmp bx, cx                      ; and SI/DI are indexes, so [si+dx] and
    jae .out                        ; [di+si] are not addresses at all
    mov dl, [si+bx]
    cmp dl, [di+bx]
    jne .gotf
    inc bx
    jmp short .first
.gotf:
    mov dx, bx                      ; DX = the FIRST differing cell
    mov bx, cx                      ; ...and now the LAST
.lscan:
    dec bx
    mov cl, [si+bx]
    cmp cl, [di+bx]
    je .lscan
    inc bx                          ; BX = ONE PAST the last differing cell
    mov cl, [si+bx]                 ; bank the byte the terminator replaces -
    mov byte [si+bx], 0             ; the field's own NUL is at its far end,
    push bx                         ; and the span usually stops short of it
    push cx

    mov bx, dx
    mov cl, 3
    shl dx, cl                      ; the pen: the field's, plus 8 per cell
    add ax, dx
    add ax, [cal_ox]
    xchg cx, ax
    mov dx, [cal_oy]
    add dx, CAL_TXT_Y
    add si, bx
    mov ax, CBLACK + CWHITE * 256
    call OSAPI_FONT_RUN
    sub si, bx

    pop cx                          ; CL = the banked byte
    pop ax                          ; AX = one past the last
    xchg ax, bx
    mov [si+bx], cl                 ; ...and the byte back
    xchg ax, bx                     ; BX = the first again
.copy:                              ; the glass now says what we composed
    mov cl, [si+bx]
    mov [di+bx], cl
    inc bx
    cmp bx, ax
    jb .copy
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; cal_shadkill - forget what the glass says, so the next emit draws it whole
; out: nothing; preserves all registers
;
; Only each shadow's FIRST and LAST cells: cal_emit's span runs from the first
; cell that differs to the last, so two cells that cannot match make it the
; whole field - and the copy behind the draw puts every cell between them
; right. 0xFF is not a character either field can hold.
; -----------------------------------------------------------------------------
cal_shadkill:
    push ax
    mov al, 0xFF
    mov [cal_cshad], al
    mov [cal_cshad + CAL_NUM_N - 1], al
    mov [cal_nshad], al
    mov [cal_nshad + CAL_NUM_N - 1], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_compose - build both display fields from the live state
; out: cal_cbuf / cal_nbuf, space-padded and NUL-terminated, and CX =
;      CAL_NUM_N, their width; clobbers AX, BX, DX, SI, DI
; -----------------------------------------------------------------------------
cal_compose:
    mov si, cal_s_error             ; --- the number field ---------------------
    cmp byte [cal_err], 0
    jne .nput
    mov si, cal_blankrow            ; an operator was the last key: the field
    cmp byte [cal_wait], 0          ; is empty, waiting for the next number
    jne .nput                       ; (SPEC.md 65.2.1)
    mov bx, cal_cur
    mov di, cal_scratch
    mov al, [cal_dot]
    call cal_fmtv
    mov byte [di], 0                ; cal_fmtv does not terminate, and
    mov si, cal_scratch             ; cal_rpad's own length walk needs the NUL
.nput:
    mov di, cal_nbuf
    mov cx, CAL_NUM_N
    call cal_rpad

    mov si, cal_blankrow            ; --- the context field --------------------
    cmp byte [cal_err], 0
    jne .cput
    cmp byte [cal_op], CAL_OP_NONE
    je .cput
    mov bx, cal_acc
    mov di, cal_scratch
    xor al, al
    call cal_fmtv
    mov al, [cal_op]
    mov bx, cal_opchar - CK_ADD
    xlat
    mov ah, al
    mov al, ' '
    mov [di], ax                    ; ' ' and the operator...
    mov byte [di+2], 0              ; ...and the end
    mov si, cal_scratch
.cput:
    mov di, cal_cbuf
    mov cx, CAL_CTX_N               ; again: cal_fmtv answers its length in CX
    jmp short cal_rpad

; -----------------------------------------------------------------------------
; cal_rpad - copy a NUL string into a fixed field, right-aligned
; in:  SI = the source, DI = the destination, CX = cells
; out: the field, space-padded and NUL-terminated at [DI+CX]; preserves CX;
;      clobbers AX, BX, DX, SI, DI
;
; A string LONGER than the field keeps its TAIL, because the low-order digits
; of a number are the ones a truncation may not eat.
; -----------------------------------------------------------------------------
cal_rpad:
    push cx
    xor bx, bx                      ; BX = the source's length
.len:
    cmp byte [si+bx], 0
    je .have
    inc bx
    jmp short .len
.have:
    mov dx, cx                      ; DX = the pad cells, 0 if it overflows
    sub dx, bx
    jns .pad
    add si, bx                      ; keep the tail
    sub si, cx
    mov bx, cx
    xor dx, dx
.pad:
    mov cx, dx
    jcxz .body
    mov al, ' '
.sp:
    mov [di], al
    inc di
    loop .sp
.body:
    mov cx, bx
    jcxz .term
.cp:
    lodsb
    mov [di], al
    inc di
    loop .cp
.term:
    mov byte [di], 0
    pop cx
    ret

; =============================================================================
; The keys (SPEC.md 65.5)
;
; cal_key banks the code, runs one handler out of cal_jtab and then draws -
; a TABLE rather than a compare ladder, because the ladder was 24 rungs and
; an 8086's conditional jumps are all short: a body added in the middle put
; the arms at the bottom out of reach, which NASM reports as a range error
; nowhere near the change that caused it.
; =============================================================================

; -----------------------------------------------------------------------------
; cal_key - run one key code
; in:  AL = CK_*; the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; Every path ends in cal_show, which draws the cells that changed and no
; others - so a digit costs one OSAPI_FONT_RUN over two or three cells, and
; nothing else on the window is touched.
;
; THIS IS THE ONE SAVE FOR THE WHOLE ENGINE. Every handler below cal_dispatch
; - the arithmetic, the history line, the clears - is reached from here and
; nowhere else, so none of them keeps a register of anybody's: each says what
; it clobbers rather than banking and restoring the lot on the way through
; (apps size pass 1, where that banking was ~200 bytes of push and pop).
; -----------------------------------------------------------------------------
cal_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cal_dispatch
    call cal_show
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cal_dispatch:
    mov [cal_kcode], al             ; the handlers that need it read it here
    ; ANY KEY THAT REACHES THE ENGINE UN-BLANKS THE FIELD, and cal_kop is the
    ; one thing that blanks it again (SPEC.md 65.2.1). One choke point rather
    ; than a clear in each of the ten handlers that put a number on show.
    ; BACKSPACE IS THE EXCEPTION: it does nothing to a result (cal_kbs), so
    ; clearing here would make it resurrect the operand the field is waiting
    ; to replace - press it after `12 +` and 12 comes back, un-editable.
    cmp al, CK_BS
    je .nowait
    mov byte [cal_wait], 0
.nowait:
    cmp byte [cal_err], 0
    je .live
    cmp al, CK_CLR                  ; refused: only the two clears act
    jb .no
    cmp al, CK_CE
    ja .no
.live:
    cmp al, 9
    jbe cal_kdigit                  ; a digit IS its code
    cmp al, CK_RECIP
    ja .no                          ; CK_HIST and the clipboard: the callers'
    cbw
    xchg bx, ax
    add bx, bx
    jmp word [cal_jtab - 2 * CK_DOT + bx]   ; every arm is a near proc ending
.no:                                        ; in ret
    ret

; -----------------------------------------------------------------------------
; cal_kdigit - append the banked digit to the entry
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (as every handler may)
;
; Both refusals are silent and deliberate: a tenth digit, or a ninth decimal,
; simply does not arrive. Wrapping the magnitude instead would turn a
; too-long entry into a plausible WRONG number, which is the one outcome a
; calculator may not produce.
; -----------------------------------------------------------------------------
cal_kdigit:
    call cal_fresh
    cmp byte [cal_dot], 0
    je .mag
    cmp byte [cal_cur+CV_D], CAL_MAXD
    jae .out                        ; eight decimals is the whole field
.mag:
    mov ax, [cal_cur+CV_M+0]
    mov dx, [cal_cur+CV_M+2]
    cmp dx, (CAL_ENTMAX >> 16)
    ja .out
    jb .fits
    cmp ax, (CAL_ENTMAX & 0xFFFF)
    ja .out
.fits:
    mov bx, 10                      ; m = m * 10 + digit, in 32 bits; the high
    mov cx, dx                      ; half cannot carry out, m being at most
    mul bx                          ; CAL_ENTMAX on the way in
    mov si, dx                      ; SI = the carry into the high half
    mov di, ax                      ; DI = the low half
    mov ax, cx
    mul bx
    add ax, si
    mov dx, ax
    mov ax, di
    mov bl, [cal_kcode]
    xor bh, bh
    add ax, bx
    adc dx, 0
    mov [cal_cur+CV_M+0], ax
    mov [cal_cur+CV_M+2], dx
    cmp byte [cal_dot], 0
    je .out
    inc byte [cal_cur+CV_D]
.out:
    ret

; -----------------------------------------------------------------------------
; cal_kdot - the decimal point
; out: nothing
;
; A point on a number that already has one is simply ignored: CV_D only ever
; grows, and [cal_dot] is already set.
; -----------------------------------------------------------------------------
cal_kdot:
    call cal_fresh
    mov byte [cal_dot], 1
    ret

; -----------------------------------------------------------------------------
; cal_kbs - backspace
; out: nothing
;
; A RESULT is not an entry to edit, so this does nothing at all when the
; display is holding one - the alternative is silently turning an answer into
; a number the machine never computed. The magnitude loses its last digit by
; a 32-bit divide in two halves, the high half's remainder carried down.
; -----------------------------------------------------------------------------
cal_kbs:
    cmp byte [cal_new], 0
    jne .out
    mov cx, 10
    xor dx, dx
    mov ax, [cal_cur+CV_M+2]
    div cx
    mov [cal_cur+CV_M+2], ax
    mov ax, [cal_cur+CV_M+0]
    div cx
    mov [cal_cur+CV_M+0], ax
    cmp byte [cal_cur+CV_D], 0
    je .dot
    dec byte [cal_cur+CV_D]
    ret
.dot:
    mov byte [cal_dot], 0           ; past the point, the point goes too
.out:
    ret

; -----------------------------------------------------------------------------
; cal_kneg - the sign key
; out: nothing
; -----------------------------------------------------------------------------
cal_kneg:
    mov ax, [cal_cur+CV_M+0]        ; -0 is 0, and showing '-0' reads as a bug
    or ax, [cal_cur+CV_M+2]
    jz .out
    xor byte [cal_cur+CV_SGN], 1
.out:
    ret

; -----------------------------------------------------------------------------
; cal_kop - a pending operator was pressed
; out: nothing
;
; Two operators running - `2 + *` - means the second one wins and nothing is
; computed, which is what a desk calculator does and what stops a mis-hit
; from committing a sum. Otherwise the pending one is reduced first, so a
; chain like `2 + 3 + 4` shows 5 when the second + is pressed.
; -----------------------------------------------------------------------------
cal_kop:
    cmp byte [cal_op], CAL_OP_NONE
    je .set
    cmp byte [cal_new], 0
    jne .set
    call cal_reduce                 ; cur = acc <op> cur...
    cmp byte [cal_err], 0
    jne .out
.set:
    mov si, cal_cur                 ; ...and the operand is whatever is on show
    mov di, cal_acc
    call cq_copy
    mov al, [cal_kcode]
    mov [cal_op], al
    mov byte [cal_new], 1
    mov word [cal_dot], 0x0100      ; no point, and [cal_wait] = 1: the
                                    ; operand has moved to the left of the
                                    ; display, so the right goes BLANK - the
                                    ; machine is ready for a new number and
                                    ; says so (SPEC.md 65.2.1). The value is
                                    ; still in [cal_cur] and `=` still uses
                                    ; it, so `12 + =` is 24 as it always was
.out:
    ret

; -----------------------------------------------------------------------------
; cal_keq - equals
; out: nothing
;
; cal_hist_prep runs BEFORE cal_reduce because cal_reduce overwrites the
; operand with the answer, and cal_hist_push after it (in cal_unary_end)
; because the answer is the rest of the line. Splitting the composition in
; two is the whole of what that ordering costs.
; -----------------------------------------------------------------------------
cal_keq:
    cmp byte [cal_op], CAL_OP_NONE
    je .out                         ; nothing pending: '=' does not repeat here
    call cal_hist_prep
    call cal_reduce
    mov byte [cal_op], CAL_OP_NONE
    jmp short cal_unary_end         ; a RESULT, and the row goes up with it
.out:
    ret

; -----------------------------------------------------------------------------
; cal_ksqrt / cal_krecip / cal_kpct - the three unary keys
; out: nothing
;
; All three leave a RESULT on show, so the next digit starts a fresh entry
; rather than extending the answer.
; -----------------------------------------------------------------------------
cal_ksqrt:
    mov si, cal_s_sqrt              ; 'sqrt(' <operand> ')' - a root IS a
    mov di, cal_s_rparen            ; calculation, so it earns a history row
    call cal_hist_prep_un           ; exactly as '=' does
    call cal_do_sqrt
    jmp short cal_unary_end

cal_krecip:
    mov si, cal_s_recip             ; '1/' <operand>, and no suffix: the NUL
    mov di, cal_s_rparen + 1        ; that ends ')' is an empty string
    call cal_hist_prep_un
    mov si, cal_one                 ; 1 / x is cal_calc's divide with a
    mov di, cal_cur                 ; constant left operand
    mov al, CK_DIV
    call cal_calc
    jmp short cal_unary_end

; The four-function convention rather than a bare divide by a hundred: with
; + or - pending, `200 + 10 %` is 200 + 20, because that is what the key is
; FOR. With x or / pending, or nothing pending, it is the plain hundredth.
cal_kpct:
    mov al, [cal_op]
    sub al, CK_ADD                  ; 0 or 1 for + and -, anything else above
    cmp al, CK_SUB - CK_ADD
    ja .plain
    mov si, cal_acc
    mov di, cal_cur
    mov al, CK_MUL
    call cal_calc
    jc cal_unary_end                ; refused: [cal_err] is already up
.plain:
    mov si, cal_cur
    mov di, cal_hundred
    mov al, CK_DIV
    call cal_calc

cal_unary_end:
    mov word [cal_new], 1           ; a RESULT, past no point
    cmp byte [cal_err], 0
    jne .out                        ; a refusal is not a calculation, and the
    cmp word [cal_linep], 0         ; line staged for it is dropped unspent
    je .out                         ; (% stages none: it is a change to the
    call cal_hist_push              ; PENDING entry rather than a sum of its
.out:                               ; own, and a row saying '10 = 20' would be
    mov word [cal_linep], 0         ; a lie about what the user did)
    ret

; -----------------------------------------------------------------------------
; cal_fresh - start a new entry if the display is holding a result
; out: nothing; clobbers AX, DI
; -----------------------------------------------------------------------------
cal_fresh:
    cmp byte [cal_new], 0
    je .out
    mov di, cal_cur
    call cal_vzero
    mov word [cal_new], 0           ; an entry, past no point
.out:
    ret

; -----------------------------------------------------------------------------
; cal_clearall / cal_clearent - the two clears
; out: nothing; clobbers AX, DI
;
; C falls through into CE: clearing everything is clearing the entry plus
; forgetting the pending operation, so there is one body for the part they
; share and no way for the two to disagree about what a cleared entry is.
; -----------------------------------------------------------------------------
cal_clearall:
    mov di, cal_acc
    call cal_vzero
    mov byte [cal_op], CAL_OP_NONE
cal_clearent:
    mov di, cal_cur
    call cal_vzero
    mov word [cal_err], 0x0100      ; no error, and a RESULT (the 0) on show,
    mov byte [cal_dot], 0           ; past no point
    ret

; -----------------------------------------------------------------------------
; cal_vzero - the value at DI = 0
; out: nothing; clobbers AX
;
; Six bytes - magnitude, decimals, sign - and not the eight a copy would move:
; it was a copy from an eight-byte constant zero.
; -----------------------------------------------------------------------------
cal_vzero:
    xor ax, ax
    mov [di+CV_M+0], ax
    mov [di+CV_M+2], ax
    mov [di+CV_D], ax               ; ...and CV_SGN
    ret

; -----------------------------------------------------------------------------
; cal_reduce - cur = acc <cal_op> cur
; out: [cal_err] set on a refusal; clobbers as cal_calc
;
; The answer lands on show and nowhere else: both callers are done with the
; accumulator - cal_kop overwrites it with the operand next, and cal_keq
; clears the operator that would read it.
; -----------------------------------------------------------------------------
cal_reduce:
    mov si, cal_acc
    mov di, cal_cur
    mov al, [cal_op]
    jmp cal_calc

; =============================================================================
; History (SPEC.md 65.4.1)
; =============================================================================

; -----------------------------------------------------------------------------
; cal_hist_prep - compose the line for the sum about to be done
; cal_hist_prep_un - the same, for a one-operand key: SI = the prefix
;                    ('sqrt(' / '1/'), DI = the suffix (')' / '')
; out: cal_line holds '<acc> <op> <cur> = ' or '<pfx><cur><sfx> = ', and
;      [cal_linep] points past it; clobbers AX, BX, CX, SI, DI
;
; Both end in the same body - the operand, the suffix, ' = ' - so a binary
; line is a unary one whose prefix is '<acc> <op> ' and whose suffix is empty.
; -----------------------------------------------------------------------------
%ifndef CALF_HIST
; No history (APP_SMALL). These three are called from the ARITHMETIC paths -
; every `=`, every sqrt and every 1/x - so gating the call sites would have
; meant six %ifdefs through code that is not the history's. Three entry points
; on one `ret` instead, which is apps/notepad/notepad.asm's undo treatment.
;
; [cal_linep] STAYS in both builds and simply reads 0 here, which is what
; cal_unary_end's `cmp word [cal_linep], 0 / je .out` already tests for - so
; that path needs no gate either.
cal_hist_prep:
cal_hist_prep_un:
cal_hist_push:
    ret
%endif

%ifdef CALF_HIST
cal_hist_prep:
    mov di, cal_line
    mov bx, cal_acc
    xor al, al
    call cal_fmtv
    mov al, [cal_op]
    mov bx, cal_opchar - CK_ADD
    xlat
    call cal_spc                    ; ' + '
    mov si, cal_s_rparen + 1        ; ...and no suffix
    jmp short cal_hist_cur

cal_hist_prep_un:
    push di                         ; the suffix, for after the operand
    mov di, cal_line
    call cal_strcat
    pop si
cal_hist_cur:
    mov bx, cal_cur
    xor al, al
    call cal_fmtv
    call cal_strcat
    mov al, '='
    call cal_spc
    mov [cal_linep], di
    ret

; -----------------------------------------------------------------------------
; cal_spc - ' ', AL, ' ' at DI, and DI past them
; out: clobbers AH
; -----------------------------------------------------------------------------
cal_spc:
    mov ah, ' '
    mov [di], ah
    mov [di+1], ax                  ; AL, then AH's space
    add di, 3
    ret

; -----------------------------------------------------------------------------
; cal_strcat - copy the NUL string at SI to DI, leaving DI past it
; in:  SI = source, DI = destination
; out: DI advanced past the bytes copied, NOT terminated; SI preserved;
;      clobbers AL
; -----------------------------------------------------------------------------
cal_strcat:
    push si
.b:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short .b
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; cal_hist_push - finish the line, ring it in, and move the pixels
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (cal_unary_end is its caller)
;
; The band SCROLLS and only row 0 is lettered. Re-lettering the rows instead
; would be eight runs of CAL_HROW_N cells, which PERFORMANCE.md Part 2 prices
; at about a millisecond a cell on the field machine - a fifth of a second of
; visible redraw on every `=`. One blit plus one run is about 27ms.
;
; The blit is licensed by W_ONCLICK firing on the FRONTMOST window alone
; (SPEC.md 13), so nothing is over our content and OSAPI_GFX_SCROLL - which
; is deliberately off the clipped-primitives list (SPEC.md 11.3) - has
; nothing it would have to cut against. A refusal (CF = 1) letters them all
; instead, so nothing here depends on the blit succeeding.
; -----------------------------------------------------------------------------
cal_hist_push:
    mov di, [cal_linep]             ; the answer completes the line
    mov bx, cal_cur
    xor al, al
    call cal_fmtv
    mov byte [di], 0

    mov al, [cal_hhead]             ; the ring advances by one byte
    cmp byte [cal_hn], 0
    je .first
    inc ax
    and al, CAL_HIST_N - 1
.first:
    mov [cal_hhead], al
    cmp byte [cal_hn], CAL_HIST_N
    jae .full
    inc byte [cal_hn]
.full:
    cbw
    mov bx, ax                      ; BX = the slot
    mov dl, CAL_HROW_N + 1
    mul dl
    add ax, cal_htext               ; ...padded to the field, so that drawing
    xchg di, ax                     ; the row IS the erase (SPEC.md 6.1)
    mov si, cal_line
    mov cx, CAL_HROW_N
    call cal_lpad

    mov cl, 3                       ; and the ANSWER beside it, because a
    shl bx, cl                      ; click loads the value rather than
    lea di, [bx+cal_hval]           ; re-reading a line the field may have
    mov si, cal_cur                 ; truncated
    call cq_copy

    mov ax, [cal_nvis]
    or ax, ax
    jz .out                         ; folded up: the ring moved, no pixels did
%ifdef CALC_NOSCROLL
    jmp short .allrows              ; make CALCSCROLL=0: the reference build,
%endif                              ; which letters every row instead. It is
                                    ; the A/B for the paragraph above rather
                                    ; than a fallback - the claim is that one
                                    ; blit plus one run beats eight runs, and
                                    ; a claim about drawing is worth what it
                                    ; is measured at (tests/calcflick.py)
    cmp ax, 1
    jbe .row0                       ; one row: there is nothing to scroll

    mov dl, CAL_ROW_H               ; the band, x-aligned by construction -
    mul dl                          ; OSAPI_WM_SNAP keeps the content origin on
    mov bx, [cal_oy]                ; a byte and both edges are multiples of 8
    add bx, CAL_HIST_Y0
    mov dx, bx
    add dx, ax
    dec dx
    mov ax, [cal_ox]
    add ax, CAL_HROW_X
    mov cx, ax
    add cx, CAL_HROW_N * 8 - 1
    mov si, -CAL_ROW_H              ; negative scrolls DOWN (SPEC.md 5.5)
    call OSAPI_GFX_SCROLL
    jc .allrows                     ; refused: nothing moved, letter them all
.row0:
    xor ax, ax
    jmp cal_draw_row
.allrows:
    jmp cal_draw_rows
.out:
    ret

; -----------------------------------------------------------------------------
; cal_lpad - copy a NUL string into a fixed field, left-aligned
; in:  SI = the source, DI = the destination, CX = cells
; out: the field, space-padded and NUL-terminated at [DI+CX]; clobbers AX,
;      CX, SI, DI
;
; A line too long keeps its HEAD and marks the cut with CAL_CUT in the last
; cell. It kept the TAIL at first, on the reasoning that the expression can be
; given up and the answer cannot - which is true about the DATA and wrong
; about the READING: a row cut from the left opens mid-number, so what the eye
; lands on is a run of low-order digits with nothing to say what they belong
; to. `123456789 * 98765432~` is a calculation you can recognise;
; `~ = 121932631112635269` is a number you cannot place. Nothing is lost by
; it either, because a click loads the ANSWER out of the stored value and
; never off the line (SPEC.md 65.4.1).
; -----------------------------------------------------------------------------
cal_lpad:
    push bx
    xor bx, bx
.len:
    cmp byte [si+bx], 0
    je .have
    inc bx
    jmp short .len
.have:
    cmp bx, cx
    jbe .copy
    mov bx, cx                      ; keep the head, and one cell for the mark
    dec bx
.copy:
    sub cx, bx                      ; CX = the cells left over
    xchg cx, bx
    jcxz .pad
.cp:
    lodsb
    mov [di], al
    inc di
    loop .cp
.pad:
    mov cx, bx
    jcxz .term
    cmp cx, 1                       ; exactly one cell short means the line was
    jne .sp                         ; cut: say so rather than pad it to look
    cmp byte [si], 0                ; complete. A line that happens to end one
    je .sp                          ; cell early is NOT cut - the source's own
    mov byte [di], CAL_CUT          ; NUL is what tells the two apart
    inc di
    jmp short .term
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
.term:
    mov byte [di], 0
    pop bx
    ret

; -----------------------------------------------------------------------------
; cal_hist_load - a history row was clicked: its ANSWER becomes the entry
; in:  AX = the screen row
; out: nothing; preserves all registers
;
; This is what the pane is FOR: the result of one sum starts the next. It
; loads the value that was stored rather than re-reading the text, so nothing
; is lost to the row's own truncation.
; -----------------------------------------------------------------------------
cal_hist_load:
    push bx
    push si
    push di
    call cal_hslot                  ; BX = the slot, SI = 0 for an empty row
    or si, si
    jz .out
    shl bx, 1
    shl bx, 1
    shl bx, 1
    lea si, [bx+cal_hval]
    mov di, cal_cur
    call cq_copy
    mov word [cal_err], 0x0100      ; no error, and it is a RESULT, so a digit
    mov word [cal_dot], 0           ; replaces it; no point - and it is a
                                    ; number to LOOK at, so the field is not
                                    ; waiting any more: this one does not come
                                    ; through cal_dispatch
    call cal_show
.out:
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; cal_hist_toggle - fold the history pane down, or back up
; out: nothing; preserves all registers
;
; OSAPI_WM_RESIZE clamps to the live screen and REPAINTS (SPEC.md 11.1), so
; there is nothing to draw after it and nothing here needs to know what a CGA
; can spare: it asks for CAL_OPEN_H, and cal_sync - which the repaint runs -
; derives [cal_nvis] from whatever was actually granted.
; -----------------------------------------------------------------------------
cal_hist_toggle:
    push ax
    push bx
    push cx
    push dx
    push si
    xor byte [cal_open], 1
    mov si, cal_mi_hide             ; the menu item says what the click WILL do
    mov dx, CAL_OPEN_H + TITLE_H + 1
    jnz .word
    mov si, cal_mi_show
    mov dx, CAL_BASE_H + TITLE_H + 1
.word:
    mov ax, [si+0]
    mov [cal_mi_hist+0], ax
    mov ax, [si+2]
    mov [cal_mi_hist+2], ax
    mov cx, CAL_CW + 2
    mov bx, [cal_win]
    call OSAPI_WM_RESIZE
    call cal_sync                   ; ...and our own idea of the box follows
    pop si                          ; whatever the clamp gave us
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif   ; CALF_HIST
; =============================================================================
; The clipboard (SPEC.md 55)
; =============================================================================

; -----------------------------------------------------------------------------
; cal_copy - the displayed number onto the system clipboard
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_copy:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                          ; ES:SI, not our DS (the slot's contract)
    mov bx, cal_cur
    mov di, cal_scratch
    xor al, al
    call cal_fmtv                   ; CX = the length
    mov si, cal_scratch
    call OSAPI_CLIP_PUT
    mov si, cal_s_copied
    jnc .say
    mov si, cal_s_nocopy
.say:
    xor cx, cx                      ; the default ~3s, on the MENU BAR, so it
    call OSAPI_TOAST                ; costs our window no pixels at all and
    pop es                          ; cannot leave our incremental drawing
    pop di                          ; disagreeing with our W_PAINT (SPEC.md 59)
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_paste - a number off the system clipboard into the entry
; out: nothing; preserves all registers
;
; It is fed through cal_key one character at a time rather than parsed, so
; the clipboard cannot express a number the keypad cannot: the same range
; refusals, the same decimal rule, and no second parser to disagree with the
; first. Anything it does not recognise simply ends the paste.
; -----------------------------------------------------------------------------
cal_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, cal_pbuf                ; ITS OWN BUFFER, clear of the composition:
    mov cx, CAL_PASTE_MAX           ; the loop below drives cal_key once per
    call OSAPI_CLIP_GET             ; character, cal_key ends in cal_show, and
    jc .out                         ; cal_show composes the display through
    jcxz .out                       ; cal_scratch - so pasting INTO the bytes
    mov word [cal_err], 0x0100      ; it composes had every keystroke
    mov byte [cal_dot], 0           ; overwrite the characters not yet read.
    mov si, cal_pbuf                ; '88' came back as '87' or '8': the second
    mov bx, cx                      ; byte was whatever the first digit's own
    xor dx, dx                      ; display text had just put there. DL =
.ch:                                ; a leading '-' was seen
    or bx, bx
    jz .done
    mov al, [si]
    cmp al, '-'                     ; a leading sign, and only a leading one
    jne .code
    cmp cx, bx
    jne .done
    inc dx
    jmp short .next
.code:
    call cal_asckey
    cmp al, CK_DOT
    ja .done                        ; digits and the point drive the entry;
    call cal_key                    ; nothing else may come off a clipboard
.next:
    inc si
    dec bx
    jmp short .ch
.done:
    or dx, dx
    jz .show
    mov al, CK_NEG
    call cal_key
.show:
    call cal_show
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
; The number (SPEC.md 65.2)
;
; Sixty-four-bit intermediates in cal_q / cal_q2 / cal_q3, four words each,
; least word first. Every routine below takes DI (and SI, and BX) as a
; POINTER to one of those, so there is no register in this file holding half
; of a number.
; =============================================================================

; -----------------------------------------------------------------------------
; cq_set32 - [DI] = DX:AX
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cq_set32:
    mov [di+0], ax
    mov [di+2], dx
    mov word [di+4], 0
    mov word [di+6], 0
    ret

; -----------------------------------------------------------------------------
; cq_mul10 - [DI] *= 10
; out: CF = 1 if it carried out of 64 bits; preserves all registers
; -----------------------------------------------------------------------------
cq_mul10:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, di                      ; BX = the pointer: an 8086 index register
    mov di, 10                      ; needs a BASE, so [di+si] is not an
    xor cx, cx                      ; address and [bx+si] is
    xor si, si
.w:
    mov ax, [bx+si]
    mul di
    add ax, cx                      ; CX = the carry into the next word
    adc dx, 0
    mov [bx+si], ax
    mov cx, dx
    add si, 2
    cmp si, 8
    jb .w
    neg cx                          ; CF = 1 exactly when a carry is left
    pop di                          ; pops do not touch the flags
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_div10 - [DI] /= 10
; out: AX = the remainder 0..9; preserves all other registers
; -----------------------------------------------------------------------------
cq_div10:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, di                      ; BX = the pointer, for cq_mul10's reason
    mov di, 10
    xor dx, dx
    mov si, 6
.w:
    mov ax, [bx+si]                 ; DX:AX < 10 * 65536, so the quotient fits
    div di
    mov [bx+si], ax
    sub si, 2
    jnc .w                          ; 6, 4, 2, 0, then the borrow ends it
    mov ax, dx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cq_add16 - [DI] += AX (unsigned 16-bit)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cq_add16:
    add [di+0], ax
    adc word [di+2], 0
    adc word [di+4], 0
    adc word [di+6], 0
    ret

; -----------------------------------------------------------------------------
; cq_div10r - [DI] = round([DI] / 10)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cq_div10r:
    push ax
    call cq_div10
    cmp ax, 5
    jb .out
    mov ax, 1
    call cq_add16
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_add / cq_sub - [DI] += [SI], [DI] -= [SI] (64-bit)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cq_add:
    push ax
    mov ax, [si+0]
    add [di+0], ax
    mov ax, [si+2]
    adc [di+2], ax
    mov ax, [si+4]
    adc [di+4], ax
    mov ax, [si+6]
    adc [di+6], ax
    pop ax
    ret

cq_sub:
    push ax
    mov ax, [si+0]
    sub [di+0], ax
    mov ax, [si+2]
    sbb [di+2], ax
    mov ax, [si+4]
    sbb [di+4], ax
    mov ax, [si+6]
    sbb [di+6], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_cmp - compare [SI] against [DI], unsigned
; out: the flags of an unsigned compare; every register preserved
; -----------------------------------------------------------------------------
cq_cmp:
    push ax
    mov ax, [si+6]
    cmp ax, [di+6]
    jne .out
    mov ax, [si+4]
    cmp ax, [di+4]
    jne .out
    mov ax, [si+2]
    cmp ax, [di+2]
    jne .out
    mov ax, [si+0]
    cmp ax, [di+0]
.out:
    pop ax                          ; pops do not touch the flags
    ret

; -----------------------------------------------------------------------------
; cq_zero - is [DI] zero?
; out: ZF = 1 if it is; every register preserved
; -----------------------------------------------------------------------------
cq_zero:
    push ax
    mov ax, [di+0]
    or ax, [di+2]
    or ax, [di+4]
    or ax, [di+6]                   ; ...which is the ZF
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_copy - [DI] = [SI]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cq_copy:
    push ax
    mov ax, [si+0]
    mov [di+0], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_mul32 - [DI] = the 64-bit product of the dwords at [SI] and [BX]
; out: nothing; preserves all registers
;
; Four 16x16 partial products. DI may not alias either input.
; -----------------------------------------------------------------------------
cq_mul32:
    push ax
    push dx
    mov word [di+4], 0
    mov word [di+6], 0
    mov ax, [si+0]                  ; a0 * b0, into words 0..1
    mul word [bx+0]
    mov [di+0], ax
    mov [di+2], dx
    mov ax, [si+0]                  ; a0 * b1, into words 1..2
    mul word [bx+2]
    add [di+2], ax
    adc word [di+4], dx
    adc word [di+6], 0
    mov ax, [si+2]                  ; a1 * b0, likewise
    mul word [bx+0]
    add [di+2], ax
    adc word [di+4], dx
    adc word [di+6], 0
    mov ax, [si+2]                  ; a1 * b1, into words 2..3
    mul word [bx+2]
    add [di+4], ax
    adc word [di+6], dx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_divmod - [DI] = [DI] / [cal_dv], the remainder to [cal_rem]
; in:  DI = the dividend, [cal_dv] a NON-ZERO dword divisor
; out: nothing; preserves all registers
;
; Restoring shift-subtract, one bit a pass, the quotient shifted in at the
; bottom as the dividend leaves the top - so there is one 96-bit shift and no
; second buffer. The remainder needs 33 bits mid-pass and the carry out of
; the last `rcl` IS that bit, which is why `jc .sub` comes before the
; compare: R < D always, so 2R+1 < 2D and 2R+1-D < D fits 32 bits again.
; -----------------------------------------------------------------------------
cq_divmod:
    push ax
    push cx
    mov word [cal_rem+0], 0
    mov word [cal_rem+2], 0
    mov cx, 64
.pass:
    shl word [di+0], 1
    rcl word [di+2], 1
    rcl word [di+4], 1
    rcl word [di+6], 1
    rcl word [cal_rem+0], 1
    rcl word [cal_rem+2], 1
    jc .sub                         ; the 33rd bit: certainly >= the divisor
    mov ax, [cal_rem+2]
    cmp ax, [cal_dv+2]
    ja .sub
    jb .next
    mov ax, [cal_rem+0]
    cmp ax, [cal_dv+0]
    jb .next
.sub:
    mov ax, [cal_dv+0]
    sub [cal_rem+0], ax
    mov ax, [cal_dv+2]
    sbb [cal_rem+2], ax
    or word [di+0], 1
.next:
    dec cx
    jnz .pass
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cq_big - is [DI] more than nine digits?
; out: CF = 1 if [DI] > CAL_MAXM; every register preserved
; -----------------------------------------------------------------------------
cq_big:
    push ax
    xor ax, ax                      ; each compare is the LIMIT against the
    cmp ax, [di+6]                  ; word, so its borrow IS the answer: CF = 1
    jb .out                         ; when the word is the larger - and a
    cmp ax, [di+4]                  ; word that differs from the limit's
    jb .out                         ; decides it either way
    mov ax, CAL_MAXM >> 16
    cmp ax, [di+2]
    jne .out
    mov ax, CAL_MAXM & 0xFFFF
    cmp ax, [di+0]
.out:
    pop ax                          ; pops do not touch the flags
    ret

; -----------------------------------------------------------------------------
; cq_scale - [DI] *= 10^CL
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cq_scale:
    push cx
    or cl, cl
    jz .out
    xor ch, ch
.mul:
    call cq_mul10
    loop .mul
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; cal_pack - turn cal_q / [cal_wd] / [cal_wsgn] into the value at cal_cur
; in:  cal_q the magnitude, [cal_wd] its decimal places (0..16), [cal_wsgn]
;      its sign
; out: CF = 1 and cal_cur untouched on overflow
; clobbers: AX, DI
;
; EVERY ANSWER LANDS IN cal_cur, so that is where this writes: the four
; callers each named it (or cal_res, which three callers then copied into
; cal_cur) - apps size pass 1. Writing the answer over an operand is safe
; because every operation has read both operands into cal_q/cal_q2/cal_dv
; before it gets here.
;
; Three passes, in this order and no other: bring the DECIMALS inside the
; field, bring the MAGNITUDE inside it - which is where an overflow is
; genuinely an overflow, there being no decimals left to give - then strip
; trailing zeros so that 2.50 shows as 2.5. The strip divides and puts the
; digit back when it was not a zero, which is cheaper than a copy and cannot
; drift from the divide it is testing.
; -----------------------------------------------------------------------------
cal_pack:
    mov di, cal_q
.decs:
    cmp byte [cal_wd], CAL_MAXD
    jbe .mag
    call cq_div10r
    dec byte [cal_wd]
    jmp short .decs
.mag:
    call cq_big
    jnc .strip
    cmp byte [cal_wd], 0
    je .over
    call cq_div10r
    dec byte [cal_wd]
    jmp short .mag
.strip:
    cmp byte [cal_wd], 0
    je .zero
    call cq_div10                   ; AX = the digit it took off
    or ax, ax
    jz .took
    call cq_mul10                   ; ...not a zero: put it straight back
    call cq_add16                   ; (both keep AX)
    jmp short .zero
.took:
    dec byte [cal_wd]
    jmp short .strip
.zero:
    call cq_zero
    jnz .store
    mov word [cal_wd], 0            ; a zero has no decimals and no sign, or
.store:                             ; the display reads '-0.00' ([cal_wsgn]
    mov ax, [cal_q+0]               ; follows [cal_wd], and CV_SGN CV_D)
    mov [cal_cur+CV_M+0], ax
    mov ax, [cal_q+2]
    mov [cal_cur+CV_M+2], ax
    mov ax, [cal_wd]
    mov [cal_cur+CV_D], ax
    clc
    ret
.over:
    stc
    ret
%if CV_SGN != CV_D + 1
 %error "cal_pack stores CV_D and CV_SGN as one word"
%endif

; -----------------------------------------------------------------------------
; cal_calc - cal_cur = [SI] <AL> [DI]
; in:  SI = the left operand, DI = the right (either may BE cal_cur), AL =
;      CK_ADD..CK_DIV
; out: CF = 1 on an overflow or a division by zero, and then [cal_err] = 1 and
;      cal_cur is untouched
; clobbers: AX, BX, CX, DX, SI, DI
;
; Called only from the key handlers, under cal_key's own save, so it and the
; three operations below it keep no register of anybody's - which is why the
; operands travel in SI and BX rather than through two words of bss, and why
; it is this routine that raises [cal_err]: all four callers did, after it.
; -----------------------------------------------------------------------------
cal_calc:
    mov bx, di                      ; BX = the right operand from here down
    call .op
    jnc .ok
    mov byte [cal_err], 1
.ok:
    ret
.op:
    sub al, CK_MUL                  ; SIGNED: add and subtract are -2 and -1
    jz cal_mul
    jg cal_div
    add al, CK_MUL - CK_ADD         ; 0 = add, 1 = subtract
    mov [cal_subf], al
    jmp cal_addsub

; -----------------------------------------------------------------------------
; cal_mul - cal_cur = [SI] * [BX]
; out: CF = 1 on overflow; clobbers as cal_calc
; -----------------------------------------------------------------------------
cal_mul:
    mov ax, [si+CV_D]               ; AL = A's decimals, AH = A's sign...
    mov dx, [bx+CV_D]
    add al, dl                      ; ...the decimals ADD: 1.5 * 1.5 has two
    xor ah, dh
    mov [cal_wd], ax                ; [cal_wd] and [cal_wsgn] in one store
    mov di, cal_q                   ; CV_M is 0, so the value IS its magnitude
    call cq_mul32                   ; at most 10^18, and a 64-bit pair holds it
    jmp cal_pack
%if CV_M != 0
 %error "cal_mul hands cq_mul32 the value pointers as the magnitudes"
%endif

; -----------------------------------------------------------------------------
; cal_div - cal_cur = [SI] / [BX]
; out: CF = 1 on a zero divisor or an overflow; clobbers as cal_calc
;
; The dividend is raised by ten until either the answer would carry the eight
; decimals the field allows or the quotient has reached nine digits, and only
; then divided - so 1/3 comes out 0.33333333 rather than 0. The FIRST loop is
; not an optimisation: a negative decimal count is not representable, so a
; divisor with more decimals than the dividend MUST be scaled past.
; -----------------------------------------------------------------------------
cal_div:
    mov ax, [bx+CV_M+0]
    mov dx, [bx+CV_M+2]
    mov [cal_dv+0], ax
    mov [cal_dv+2], dx
    or ax, dx
    stc                             ; a zero divisor: a refusal, not an answer
    jz .ret                         ; (stc leaves ZF alone)
    mov ax, [si+CV_D]               ; the answer's decimals so far: da - db,
    mov dx, [bx+CV_D]               ; and it may well be negative - and the
    sub al, dl                      ; sign, in the same word
    xor ah, dh
    mov [cal_wd], ax

    mov di, cal_q                   ; the dividend
    mov ax, [si+CV_M+0]
    mov dx, [si+CV_M+2]
    call cq_set32

    mov di, cal_q2                  ; the precision target: divisor * 10^8, so
    mov si, cal_dv                  ; that "the quotient has nine digits" is
    mov bx, cal_e8                  ; one 64-bit compare and no division
    call cq_mul32

.raise:
    cmp byte [cal_wd], 0            ; mandatory: a negative count first
    jl .up
    cmp byte [cal_wd], CAL_MAXD     ; then optional, while precision is short
    jge .quot
    mov si, cal_q
    mov di, cal_q2
    call cq_cmp
    jae .quot
.up:
    mov di, cal_q
    call cq_mul10
    inc byte [cal_wd]
    jmp short .raise

.quot:
    mov ax, [cal_dv+0]              ; round to nearest: + divisor/2 before the
    mov dx, [cal_dv+2]              ; divide, which is exact - the divisor is
    shr dx, 1                       ; at most nine digits, so its halving
    rcr ax, 1                       ; carries no further than this
    mov di, cal_q2
    call cq_set32
    mov si, cal_q2
    mov di, cal_q
    call cq_add
    call cq_divmod
    jmp cal_pack
.ret:
    ret

; -----------------------------------------------------------------------------
; cal_addsub - cal_cur = [SI] +/- [BX], per [cal_subf]
; out: CF = 1 on overflow; clobbers as cal_calc
;
; Both magnitudes are raised to the LARGER decimal count first, so the sum is
; exact before anything is rounded - a 9-digit number raised by at most eight
; places is 10^17, which is why the intermediates are 64-bit and why nothing
; here can overflow before cal_pack gets to decide.
; -----------------------------------------------------------------------------
cal_addsub:
    mov al, [si+CV_D]               ; dt = max(da, db)
    mov ah, [bx+CV_D]
    cmp al, ah
    jae .dt
    mov al, ah
.dt:
    mov [cal_wd], al

    mov di, cal_q                   ; A, raised to dt
    call .raise
    xchg si, bx
    mov di, cal_q2                  ; B, likewise
    call .raise
    xchg si, bx

    mov al, [si+CV_SGN]             ; AL = A's sign, AH = B's, subtraction
    mov ah, [bx+CV_SGN]             ; being just B with its sign flipped (a
    xor ah, [cal_subf]              ; sign is 0 or 1, and so is the flag)

    cmp al, ah
    jne .diff
    mov si, cal_q2                  ; like signs: the magnitudes add
    mov di, cal_q
    call cq_add
    mov [cal_wsgn], al
    jmp cal_pack
.diff:
    mov si, cal_q                   ; unlike: the larger magnitude keeps its
    mov di, cal_q2                  ; sign and the smaller comes off it
    call cq_cmp                     ; compares [cal_q] against [cal_q2]
    jb .bwins
    xchg si, di
    call cq_sub                     ; cal_q -= cal_q2
    mov [cal_wsgn], al
    jmp cal_pack
.bwins:
    call cq_sub                     ; cal_q2 -= cal_q...
    xchg si, di
    call cq_copy                    ; ...and it is the answer
    mov [cal_wsgn], ah
    jmp cal_pack

.raise:                             ; [DI] = the magnitude at SI, raised to
    mov ax, [si+CV_M+0]             ; [cal_wd] decimals
    mov dx, [si+CV_M+2]
    call cq_set32
    mov cl, [cal_wd]
    sub cl, [si+CV_D]
    jmp cq_scale

; -----------------------------------------------------------------------------
; cal_do_sqrt - cur = sqrt(cur)
; out: [cal_err] set on a refusal; clobbers AX, BX, CX, DX, SI, DI (a key
;      handler, under cal_key's save)
;
; The magnitude is scaled by an EVEN number of decimal places before the
; root, because sqrt(m * 10^e) = sqrt(m) * 10^(e/2) only divides cleanly when
; e is even - and it is scaled as far as 2^53 allows, because every factor of
; a hundred put in buys one more digit of answer. The exponent rides in AL
; and on the stack across the root, where it was a byte of bss.
; -----------------------------------------------------------------------------
cal_do_sqrt:
    cmp byte [cal_cur+CV_SGN], 0
    je .ok
    mov ax, [cal_cur+CV_M+0]        ; the root of a negative number is refused;
    or ax, [cal_cur+CV_M+2]         ; a negative ZERO is not negative
    jnz .fail
.ok:
    mov di, cal_q
    mov ax, [cal_cur+CV_M+0]
    mov dx, [cal_cur+CV_M+2]
    call cq_set32
    mov al, [cal_cur+CV_D]          ; AL = the exponent
    test al, 1
    jz .grow
    call cq_mul10                   ; an odd count: one more place makes it even
    inc ax                          ; (AL is at most 7: no carry into AH)
.grow:
    cmp al, 14
    ja .root
    cmp word [cal_q+6], 0x0100      ; under 2^56, so a factor of a hundred is
    jae .root                       ; still under 2^63. It is the TOP word that
                                    ; has to be tested and the first version
                                    ; tested word 2 against 0x0020 - a bound of
                                    ; 2^37, not 2^53 - so the scaling stopped
                                    ; four decimal places early and sqrt(2) came
                                    ; out 1.414213 instead of 1.41421356: right
                                    ; to every digit it printed, and three short
    call cq_mul10
    call cq_mul10
    add al, 2
    jmp short .grow
.root:
    push ax
    call cal_isqrt                  ; cal_q = floor(sqrt(cal_q))
    pop ax
    shr al, 1
    mov ah, 0                       ; [cal_wd] = e / 2 and [cal_wsgn] = 0, a
    mov [cal_wd], ax                ; root being positive
    call cal_pack
    jnc .out
.fail:
    mov byte [cal_err], 1
.out:
    ret

; -----------------------------------------------------------------------------
; cal_isqrt - cal_q = floor(sqrt(cal_q))
; clobbers: AX, BX, CX, DX, SI, DI (cal_do_sqrt is its one caller)
;
; Newton from a seed one bit above the true root, so the sequence falls
; monotonically and "it stopped falling" IS the answer - no epsilon, and no
; iteration count to tune. Six or seven divides, which at about 4ms each is
; well under the threshold at which a key press stops feeling immediate.
;
; The running root lives in cal_dv, which is exactly where cq_divmod wants
; it as the divisor - it was a dword of its own, copied in every pass.
; -----------------------------------------------------------------------------
cal_isqrt:
    mov di, cal_q
    call cq_zero
    jz .out                         ; sqrt(0) = 0, and 0 is no seed at all

    mov si, cal_q                   ; bank the radicand: cq_divmod eats it
    mov di, cal_q3
    call cq_copy

    mov cx, 64                      ; the bit length, from the top word down
    mov bx, 6
.word:
    mov ax, [cal_q3+bx]
    or ax, ax
    jnz .bit
    sub cx, 16
    dec bx
    dec bx
    jmp short .word
.bit:
    shl ax, 1
    jc .seed
    dec cx
    jmp short .bit
.seed:
    shr cx, 1                       ; the seed is 1 << ceil(L/2), which is
    adc cx, 0                       ; above sqrt for every L (CF = L's low bit)
    mov ax, 1
    xor dx, dx
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh                        ; (loop leaves the flags: CF is the bit
    sbb ax, 0                       ; shifted out.) L = 63 asks for 2^32, which
    sbb dx, 0                       ; wraps DX:AX to 0 with CF=1 - a ZERO
                                    ; divisor, and sqrt(720575940) showed 0.
                                    ; Borrowing it makes FFFF:FFFF, which is
                                    ; still above sqrt(2^63) = 2^31.5, so the
                                    ; fall stays monotonic (SPEC.md 65.2)

.iter:                              ; DX:AX = x
    mov [cal_dv+0], ax
    mov [cal_dv+2], dx
    mov si, cal_q3                  ; q = radicand / x, which fits a dword
    mov di, cal_q                   ; because x is never below the true root
    call cq_copy
    call cq_divmod
    mov ax, [cal_q+0]               ; nx = (x + q) / 2, and the carry out of
    add ax, [cal_dv+0]              ; the add is the 33rd bit, so it shifts
    mov dx, [cal_q+2]               ; back in through the rcr pair
    adc dx, [cal_dv+2]
    rcr dx, 1
    rcr ax, 1
    cmp dx, [cal_dv+2]
    ja .done
    jb .iter                        ; still falling: nx is the next x
    cmp ax, [cal_dv+0]
    jb .iter
.done:
    mov ax, [cal_dv+0]              ; it stopped falling: x is the root
    mov dx, [cal_dv+2]
    call cq_set32                   ; (DI is still cal_q)
.out:
    ret

; -----------------------------------------------------------------------------
; cal_fmtv - the value at BX as decimal text at DI
; in:  BX = the value, DI = the destination, AL non-zero = show a trailing
;      point (the '12.' a half-typed entry is showing)
; out: DI = ONE PAST the text, which is NOT terminated, and CX = how many
;      characters that is; preserves all other registers
;
; DI comes back ADVANCED because every caller but one went on to write after
; the number - a NUL, an operator, ' = ' - and each spelled `add di, cx`
; first (apps size pass 1).
;
; The digits come out backwards into cal_dig and are then walked forwards
; with the point put in, which is the only way round that can pad the
; INTEGER side: 0.05 is a magnitude of 5 with two decimals, and its leading
; zero exists nowhere in the magnitude. ONE walk for both sides: the point
; goes in front of the digit whose index is the decimal count, which no
; integer digit can have.
;
; THIS IS ON THE KEYSTROKE PATH - cal_compose formats the entry on every key -
; and the digit loop is the cost of it: two DIVs a digit. Its high half and
; low half live in BP and AX across them rather than being pushed and popped
; round each one, which is both shorter and faster than it was.
; -----------------------------------------------------------------------------
cal_fmtv:
    push ax
    push bx
    push dx
    push si
    push bp
    push di                         ; the start, for the count
    mov [cal_fdot], al

    mov ax, [bx+CV_M+0]             ; the magnitude, least digit first
    mov dx, [bx+CV_M+2]
    mov cx, 10
    xor si, si                      ; SI = how many
.dig:
    mov bp, ax                      ; BP = the low word
    xchg ax, dx                     ; the high half first, so its remainder
    xor dx, dx                      ; carries down into the low half
    div cx
    xchg ax, bp                     ; AX = the low word, BP = the high quotient
    div cx                          ; AX = the low quotient, DX = the digit
    mov [cal_dig+si], dl
    inc si
    mov dx, bp                      ; DX:AX = the quotient
    or bp, ax
    jnz .dig
    mov dl, [bx+CV_D]               ; DX = the decimals owed (DH is 0: the
.pad:                               ; quotient's high word, which ended 0)
    cmp si, dx                      ; at least one digit in FRONT of the point
    ja .sign
    mov byte [cal_dig+si], 0
    inc si
    jmp short .pad

.sign:
    cmp byte [bx+CV_SGN], 0
    je .walk
    mov byte [di], '-'
    inc di
.walk:
    cmp si, dx                      ; the point, in front of digit [dx-1] - and
    jne .digit                      ; only ever with decimals: SI is not 0 here
    mov byte [di], '.'
    inc di
.digit:
    dec si
    mov al, [cal_dig+si]
    add al, '0'
    mov [di], al
    inc di
    or si, si
    jnz .walk
    or dx, dx                       ; the trailing point a half-typed '12.'
    jnz .end                        ; shows, which only a value with no
    cmp byte [cal_fdot], 0          ; decimals can owe
    je .end
    mov byte [di], '.'
    inc di
.end:
    mov cx, di
    pop si                          ; SI = the start
    sub cx, si
    pop bp
    pop si
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; Authored for a 640x480 VGA; wm_fit clamps the origin onto whatever screen
; this machine has (SPEC.md 39.7), and the frame HEIGHT is inside every
; adapter's desktop band with the history folded up - which is the whole
; point of CAL_BASE_H being what it is.
cal_tpl:
    dw 180, 48, CAL_CW + 2, CAL_BASE_H + TITLE_H + 1
    dw cal_ttl, cal_paint, cal_onkey, cal_onclick

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
; Three menus, and every item is also a key - the menu is the discoverable
; half of the keyboard rather than a second set of verbs. The app-name cell
; and its Close are the kernel's (SPEC.md 12.7).
    OS88_MENUSET cal_menus, cal_m_name, cal_oncmd
        OS88_MENU cal_m_edit, cal_i_edit, 2
        OS88_MENU cal_m_math, cal_i_math, 4
%ifdef CALF_HIST
        OS88_MENU cal_m_view, cal_i_view, 2
%endif                              ; the View menu is the pane's, both items -
                                    ; and it is LAST, so Edit and Math keep
                                    ; their indices when it goes
    OS88_MENUSET_END cal_menus

cal_m_name: db 'Calc', 0
cal_m_edit: db 'Edit', 0
cal_m_math: db 'Math', 0
cal_m_view: db 'View', 0

cal_i_edit: dw cal_mi_copy, cal_mi_paste
cal_i_math: dw cal_mi_sqrt, cal_mi_recip, cal_mi_pct, cal_mi_neg
cal_i_view: dw cal_mi_hist, cal_mi_clr

cal_mi_copy:  db 'Copy   ^C', 0
cal_mi_paste: db 'Paste  ^V', 0
cal_mi_sqrt:  db 'Square Root  Q', 0
cal_mi_recip: db 'Reciprocal   R', 0
cal_mi_pct:   db 'Percent      %', 0
cal_mi_neg:   db 'Negate       N', 0
; The one mutable string in the set: a menu item's bytes are read when the
; menu DROPS, so saying what the click will do NEXT is four bytes rewritten
; by cal_hist_toggle rather than two items with one of them greyed.
cal_mi_hist:  db 'Show History H', 0
cal_mi_clr:   db 'Clear History', 0
cal_mi_show:  db 'Show'
cal_mi_hide:  db 'Hide'

cal_ttl:      db 'Calculator', 0

; --- the About card's credit (SPEC.md 12.2/65) --------------------------------
; A NUL-terminated array of line pointers - os88ui_about's own format - that
; the card measures rather than pins: the widest line sets the width and the
; count sets the height. Keep every line inside 25 glyphs - the content is
; CAL_CW = 224px on every adapter, which is 28 cells less the card's own 12px
; margins. An empty string is a blank line.
%ifdef CALF_ABOUT
cal_ablines:
    dw cal_ab1, cal_ab2, cal_ab3, cal_ab4, 0
cal_ab1:      db 'Calc for os8088', 0
cal_ab2:      db 'A desk calculator', 0
cal_ab3:      db 0                  ; a blank line is a line with no glyphs
cal_ab4:      db 'Contributed by Elendilon', 0
%endif

cal_s_hist:   db 'History', 0
cal_s_error:  db 'Error', 0
cal_s_copied: db 'Copied', 0
cal_s_sqrt:   db 'sqrt(', 0
cal_s_rparen: db ')', 0
cal_s_recip:  db '1/', 0
cal_s_nocopy: db 'Clipboard full', 0

; Twenty-six spaces and a NUL: the blank a history row past the end of the
; ring draws, and the blank the context field falls back to. One string for
; both, because CAL_HROW_N is the wider of the two.
cal_blankrow: db '                          ', 0

; The operator characters, indexed by [cal_op] - CK_ADD. They are ASCII '*'
; and '/' rather than the multiplication and division signs because the
; kernel's font is glyphs 32..126 (font.inc) - and the KEY CAP then says the
; same thing the display does, which is worth more than a prettier symbol on
; one of the two.
cal_opchar:   db '+', '-', '*', '/'

; The key dispatch (cal_dispatch), indexed by key code CK_DOT..CK_RECIP. The
; ten digits are not in it: they ARE their codes, so cal_dispatch sends 0..9
; to cal_kdigit with one compare, and the body reads the code back out of
; [cal_kcode]. Ten slots of the same pointer were 20 bytes of table; the
; compare is 4 (apps size pass 1). CK_HIST and the two clipboard codes are
; past its end and cal_dispatch refuses them - the callers take those.
cal_jtab:
    dw cal_kdot                     ; 10 CK_DOT
    dw cal_kop, cal_kop, cal_kop, cal_kop   ; 11..14 CK_ADD..CK_DIV
    dw cal_keq                      ; 15 CK_EQ
    dw cal_clearall                 ; 16 CK_CLR  (the two clears are the only
    dw cal_clearent                 ; 17 CK_CE    keys an Error lets through)
    dw cal_kbs                      ; 18 CK_BS
    dw cal_kneg                     ; 19 CK_NEG
    dw cal_ksqrt                    ; 20 CK_SQRT
    dw cal_kpct                     ; 21 CK_PCT
    dw cal_krecip                   ; 22 CK_RECIP

; The Math menu's four items, as the key each one IS (SPEC.md 65.5): the item
; index into this table, and cal_key does the rest.
cal_mathk:    db CK_SQRT, CK_RECIP, CK_PCT, CK_NEG

; The keypad, row-major, and it is the table cal_sync walks - so the cap,
; the rect and the action are one description and cannot drift. Two parallel
; tables indexed by the key: the CODE, a byte, and the CAP, a NUL string in
; a fixed four-byte slot (the longest cap is '+/-'). It was a word of label
; pointer and a word of code per key over twenty separate strings - 124 bytes
; where these are 100, and the cap is now an address computed from the index
; rather than a load (apps size pass 1).
cal_kcodes:
    db CK_CLR, CK_CE,  CK_BS,  CK_DIV
    db 7,      8,      9,      CK_MUL
    db 4,      5,      6,      CK_SUB
    db 1,      2,      3,      CK_ADD
    db CK_NEG, 0,      CK_DOT, CK_EQ
cal_klbl:
    db 'C',0,0,0,   'CE',0,0,   '<-',0,0,  '/',0,0,0
    db '7',0,0,0,   '8',0,0,0,  '9',0,0,0, '*',0,0,0
    db '4',0,0,0,   '5',0,0,0,  '6',0,0,0, '-',0,0,0
    db '1',0,0,0,   '2',0,0,0,  '3',0,0,0, '+',0,0,0
    db '+/-',0,     '0',0,0,0,  '.',0,0,0, '=',0,0,0

; ASCII to key code, ended by a 0 byte and CK_NONE. The digits are not here: they ARE
; their codes (the CK_* block).
cal_asctab:
    db '.', CK_DOT
    db '+', CK_ADD
    db '-', CK_SUB
    db '*', CK_MUL
    db 'x', CK_MUL
    db 'X', CK_MUL
    db '/', CK_DIV
    db '=', CK_EQ
    db 13,  CK_EQ
    db 8,   CK_BS
    db 27,  CK_CLR
    db 'c', CK_CLR
    db 'C', CK_CLR
    db 'e', CK_CE
    db 'E', CK_CE
    db 'n', CK_NEG
    db 'N', CK_NEG
    db 'q', CK_SQRT
    db 'Q', CK_SQRT
    db '%', CK_PCT
    db 'r', CK_RECIP
    db 'R', CK_RECIP
    db 'h', CK_HIST
    db 'H', CK_HIST
    db 3,   CK_COPY                 ; Ctrl-C and Ctrl-V, Note Pad's bindings
    db 22,  CK_PASTE                ; (SPEC.md 27.8) - and they cannot collide
    db 0, CK_NONE                   ; with 'c' or 'v', which are bytes of their
                                    ; own: a control key is not its letter.
                                    ; The end is a pair too, and its code is
                                    ; the answer to a byte that is not here


; The disclosure triangle, as a 1bpp BAND for OSAPI_GFX_BLIT1: eight rows of
; one byte, bit 7 the leftmost pixel and a SET bit WHITE - the band arrives in
; final screen polarity and white is the lit pixel (SPEC.md 5.4.2). The cell
; is 8x8 at (CAL_TRI_X, CAL_HDR_TY), a multiple of 8 from the snapped content
; origin, so the blit takes its byte-grid path on every adapter and kernel.
;
; It was four black fills out of a {x1,y1,x2,y2} band table over a white fill
; of the cell - FIVE drawing calls a fold, and 130 bytes of code and table
; (apps size pass 1). The picture is the same pixels: the eighth column, which
; the fill never touched, is white here and was white there, because nothing
; else is drawn in it and its ground is whitened before the strip is drawn.
%ifdef CALF_HIST
cal_tri_shut:                       ; folded up: pointing right
    db 0x7F, 0x3F, 0x1F, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
cal_tri_open:                       ; folded down: pointing down
    db 0xFF, 0x01, 0x83, 0xC7, 0xEF, 0xFF, 0xFF, 0xFF
%endif   ; CALF_HIST

; --- constants, as values ------------------------------------------------------
; Only CV_M, CV_D and CV_SGN are READ - a value's last two bytes are pad, kept
; so a value is indexed by <<3 (SPEC.md 65.2) - so each constant stops at its
; sign byte and the next one's first two bytes are the previous one's pad:
; cq_copy moves eight and that is harmless. 1 and 100 are cal_calc's constant
; left and right operands; 10^8 is cal_div's precision target, read as a dword.
cal_one:     dd 1
             db 0, 0
cal_hundred: dd 100
             db 0, 0
cal_e8:      dd 100000000

%ifdef CALF_ABOUT
%define OS88UI_ABOUT            ; the standard About card (SPEC.md 20.5.1.1)
%endif
%define OS88UI_NOGLYPH          ; no check box or radio drawn here (SPEC.md 13.8.9)
%define OS88UI_NOGEST           ; presses routed by hand: no gesture install (SPEC.md 20.5.1.3.4)
%define OS88UI_NOBFIND          ; ...and hit-tested by hand: cal_hit asks os88ui_bhit
%include "os88ui.inc"

%assign CAL_BSS 0
%macro CALVAR 2                     ; name, bytes
%1 equ os88_image_end + CAL_BSS
%assign CAL_BSS CAL_BSS + (%2)
%endmacro

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; Laid out by a PREPROCESSOR COUNTER, not by hand. It was ~52 lines of
; `equ os88_image_end + N` with the offsets typed out, and two things came of
; that: the note above cal_down says renumbering is expensive enough that a
; variable was put in the IMAGE to avoid it, and the total drifted - the
; fields end at 843 and `CAL_BSS_TOTAL equ 1136` had been sitting above them
; since the package landed, so **293 bytes of every instance were reserved,
; zeroed by the loader and never addressed by anything**.
;
; CAL_BSS_TOTAL now FALLS OUT of the counter, so it cannot drift again, and
; the 293 bytes come back on both builds. The counter is also what makes the
; history gateable at all: dropping a field from a hand-numbered table means
; renumbering every field under it (SPEC.md 65.10).
;
; All zero is a valid starting state: a zero entry, no pending operator, no
; history and the pane folded up. [cal_new] being 0 rather than 1 is right
; too - the display shows a plain 0 and the first digit extends it, which is
; what typing into a fresh calculator does.
;
; tests/dispcalc.py and tests/dispcalcx.py read some of these at FIXED offsets
; past the image (the header's +8), so moving a field moves those numbers too.
    CALVAR cal_win,     2    ; word: our window
    CALVAR cal_ox,      2    ; word: content left  (per callback)
    CALVAR cal_oy,      2    ; word: content top
    CALVAR cal_ch,      2    ; word: content height
%ifdef CALF_HIST
    CALVAR cal_nvis,    2    ; word: history rows that FIT
%endif
    CALVAR cal_linep,   2    ; word: where the answer joins cal_line
    ; WHICH CONTROL IS DRAWN DOWN (SPEC.md 13.8/13.8.2), as os88ui_bfind's
    ; index + 1 so that 0 can mean "none". It was DATA, for os88ui_armw's
    ; reason - the bss was hand-numbered then, and two bytes of image were
    ; cheaper than renumbering it; the counter above made that free.
    ;
    ; IT IS READ BY THE PAINTERS, which is the point of it being a variable
    ; at all rather than something the press hands to a drawing routine:
    ; cal_btn, cal_ctl and cal_draw_row all consult it, so a W_PAINT arriving
    ; in the middle of a gesture draws the held key held. Without that the
    ; kernel repaints the window - a drag over it, a window above it closing -
    ; and the key springs up under a finger that is still down, after which
    ; the release "restores" a control that is already upright.
    CALVAR cal_down,    2

%ifdef CALF_HIST
    CALVAR cal_open,    1    ; byte: the history pane is folded down
%endif
    ; THESE FOUR ARE ONE GROUP, IN THIS ORDER, because they are set together:
    ; a word store covers two of them (`mov word [cal_new], 1` is "a RESULT is
    ; on show, past no point"), which is 6 bytes where two byte stores are 10.
    CALVAR cal_err,     1    ; byte: the refused state
    CALVAR cal_new,     1    ; byte: the display holds a RESULT
    CALVAR cal_dot,     1    ; byte: the entry is past its point
    CALVAR cal_wait,    1    ; byte: an operator was the last key, so the
                             ; number field is blank and waiting
    CALVAR cal_op,      1    ; byte: CAL_OP_NONE or CK_ADD..CK_DIV
%ifdef CALF_HIST
    CALVAR cal_hn,      1    ; byte: history entries kept, 0..8 - and
    CALVAR cal_hhead,   1    ; byte: the newest one's ring slot, AFTER it:
%endif                       ; Clear History is one word store
    CALVAR cal_wd,      1    ; byte: cal_pack's decimal count
    CALVAR cal_wsgn,    1    ; byte: ...and its sign
    CALVAR cal_subf,    1    ; byte: cal_addsub's subtract flag
    CALVAR cal_fdot,    1    ; byte: cal_fmtv's trailing-point flag
    CALVAR cal_kcode,   1    ; byte: the key cal_dispatch is running
%ifdef CALF_ABOUT
    CALVAR cal_abon,    1    ; byte: 1 = the credits are up
%endif

%if CAL_BSS & 1
    CALVAR cal_pad,     1    ; the values below stay on a word: odd costs a
%endif                       ; 16-bit bus a second access per word
    CALVAR cal_acc,     8    ; the accumulator
    CALVAR cal_cur,     8    ; ...and what is on show, which is also where
                             ; every answer lands (cal_pack writes it)

    CALVAR cal_q,       8    ; 64-bit scratch, four words each
    CALVAR cal_q2,      8
    CALVAR cal_q3,      8
    CALVAR cal_dv,      4    ; dword: cq_divmod's divisor
    CALVAR cal_rem,     4    ; dword: ...and its remainder

    ; The two fields and their shadows, each CAL_NUM_N cells and a NUL. In
    ; this order and at this stride: cal_show steps from the first pair to
    ; the second.
    CALVAR cal_cbuf,    CAL_NUM_N + 1    ; the context field
    CALVAR cal_cshad,   CAL_NUM_N + 1    ; ...and what is on the GLASS
    CALVAR cal_nbuf,    CAL_NUM_N + 1    ; the number field, likewise
    CALVAR cal_nshad,   CAL_NUM_N + 1

    CALVAR cal_dig,     10   ; cal_fmtv's digits, least first: nine at most
                             ; (a nine-digit magnitude, or 0.0000000x padded
                             ; to CAL_MAXD + 1)
    ; SHARED SCRATCH, three tenants that are never live at once:
    ;   cal_scratch  the display's composition and the clipboard's staging -
    ;                fourteen bytes at most ('-9.99999999' + ' ' + op + NUL);
    ;   cal_line     a history line before it is padded, from cal_hist_prep to
    ;                cal_hist_push - 40 bytes at most (11 + ' op ' + 11 +
    ;                ' = ' + 11 + NUL), and nothing in between composes the
    ;                display: cal_key draws only after its handler returns;
    ;   cal_pbuf     a paste, at +16, CLEAR OF THE COMPOSITION - the paste
    ;                feeds cal_key one byte at a time and every one recomposes
    ;                the display through cal_scratch while the rest of the
    ;                paste is still unread ('88' came back '87' when it shared
    ;                the first bytes). Nothing a paste reaches stages a line.
    ; They were 48 + 64 + 26 bytes of their own (apps size pass 1).
    CALVAR cal_scratch, 48
cal_pbuf     equ cal_scratch + 16
%if CAL_PASTE_MAX + 16 > 48
 %error "cal_pbuf no longer fits cal_scratch above the composition"
%endif
%ifdef CALF_HIST
cal_line     equ cal_scratch
    CALVAR cal_htext,   216  ; 8 x 27: the rows, padded to the field
    CALVAR cal_hval,    64   ; 8 x 8: the answers they load back
%endif

    CALVAR cal_rc,      8    ; {x1,y1,x2,y2}, screen: cal_rect's answer -
                             ; ONE rect, where it was a table of all 29 (a
                             ; typed 232, then CAL_NRECT * 8)

%assign CAL_BSS_TOTAL CAL_BSS

    OS88_BSS CAL_BSS_TOTAL
    OS88_IMAGE_END
