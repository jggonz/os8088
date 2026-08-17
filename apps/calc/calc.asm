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
CAL_OPEN_H   equ CAL_BASE_H + CAL_HIST_N * CAL_ROW_H ; the height we ASK for

; --- rect table indices (one table, drawn from and hit-tested from) ------------
CAL_R_HDR    equ CAL_NKEY           ; 20: the disclosure strip
CAL_R_HIST   equ CAL_NKEY + 1       ; 21..28: the history rows
CAL_NRECT    equ CAL_R_HIST + CAL_HIST_N

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
CAL_BSS_TOTAL equ 1136              ; see the bss layout after OS88_IMAGE_END

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
    mov si, cal_menus
    call OSAPI_MENU_SET             ; the bar already says 'Calc' on frame one
    mov si, cal_about
    call OSAPI_ABOUT_SET            ; ...and 'About Calc' above its Close
                                    ; (SPEC.md 12.2/12.7)
    mov ax, cal_onup
    call OSAPI_WM_ONMOUSEUP         ; SPEC.md 13.7: a key fires on the RELEASE,
                                    ; over the key the press landed on, so a
                                    ; mis-aimed press can be slid off
    mov ax, cal_onresize
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
.out:
    pop si
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; =============================================================================
; Geometry and layout
; =============================================================================

; -----------------------------------------------------------------------------
; cal_geom - read the live content box and derive everything that moves
; in:  BX = window ptr; any context
; out: [cal_ox]/[cal_oy] the content origin, [cal_ch] its height,
;      [cal_nvis] the history rows that FIT; all registers preserved
;
; The one derived number is [cal_nvis], and deriving it rather than assuming
; it is the whole of what makes this app correct on a CGA (SPEC.md 65.3): the
; window ASKS for CAL_OPEN_H and takes whatever the kernel's clamp granted.
; The WIDTH is a constant because the frame is 226px and the narrowest screen
; this OS drives is 640 (SPEC.md 39), so wm_fit can only ever move it.
; -----------------------------------------------------------------------------
cal_geom:
    push ax
    push cx
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [cal_ox], ax
    mov [cal_oy], dx
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    mov [cal_ch], dx
    xor ax, ax                      ; folded up: no rows, and no arithmetic
    cmp byte [cal_open], 0
    je .set
    sub dx, CAL_BASE_H              ; ...else as many whole rows as are left
    jbe .set                        ; over (a machine that granted us nothing
    mov ax, dx                      ; extra is the CGA case, not an error)
    xor dx, dx
    mov cx, CAL_ROW_H
    div cx
    cmp ax, CAL_HIST_N
    jbe .set
    mov ax, CAL_HIST_N
.set:
    mov [cal_nvis], ax
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_layout - rebuild the rect table from the live content origin
; in:  [cal_ox]/[cal_oy]
; out: nothing; all registers preserved
;
; ONE table, read by the drawing (os88ui_btn) and by the hit test
; (os88ui_bfind) alike, so the drawn control and the clickable control cannot
; drift - SPEC.md 22's fm_hit discipline, and recorder's rc_layout is the
; precedent. All CAL_HIST_N row rects are built whether or not they are on
; screen: os88ui_bfind is given a COUNT (cal_nrect), so a row that is not
; shown is simply not reachable, and there is no second rule about which
; rects are valid.
; -----------------------------------------------------------------------------
cal_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, cal_rects
    xor si, si                      ; SI = the key index 0..19
    xor cx, cx                      ; CX = the column
    mov bx, [cal_oy]
    add bx, CAL_KEY_Y0              ; BX = this row's y1, and it is in BX
                                    ; rather than DX because `mul` WRITES DX:
                                    ; the row was held there for one commit
                                    ; and every key came out at y = 0, twenty
                                    ; buttons stacked over the MENU BAR with
                                    ; nothing at all in the window. It
                                    ; assembles, boots, draws, and every
                                    ; keyboard test passes
.key:
    mov ax, cx
    mov dx, CAL_BTN_W + CAL_BTN_HG
    mul dx                          ; AX = column * the pitch (DX = 0, dead)
    add ax, CAL_KEY_X0
    add ax, [cal_ox]
    mov [di+0], ax                  ; x1
    add ax, CAL_BTN_W - 1
    mov [di+4], ax                  ; x2
    mov [di+2], bx                  ; y1
    mov ax, bx
    add ax, CAL_BTN_H - 1
    mov [di+6], ax                  ; y2
    add di, 8
    inc cx
    cmp cx, CAL_KEY_COLS
    jb .same
    xor cx, cx                      ; ...and on to the next row
    add bx, CAL_KEY_PIT
.same:
    inc si
    cmp si, CAL_NKEY
    jb .key

    mov ax, [cal_ox]                ; the disclosure strip: the keypad's full
    add ax, CAL_KEY_X0              ; width, so the label and the triangle are
    mov cx, [cal_ox]                ; both inside one target
    add cx, CAL_CW - 1 - CAL_KEY_X0
    mov [di+0], ax
    mov [di+4], cx
    mov bx, [cal_oy]
    add bx, CAL_HDR_Y1
    mov [di+2], bx
    mov bx, [cal_oy]
    add bx, CAL_HDR_Y2
    mov [di+6], bx
    add di, 8

    mov ax, [cal_ox]                ; The history rows, and their rect is
    add ax, CAL_HROW_X              ; EXACTLY the run that draws them - 26
    mov cx, ax                      ; cells wide and eight rows tall, not the
    add cx, CAL_HROW_N * 8 - 1      ; nine-row pitch and not the window's full
    mov bx, [cal_oy]                ; width. A rect wider than what puts the
    add bx, CAL_HIST_Y0             ; row back is a press that INVERTS pixels
    xor si, si                      ; the release cannot restore, and they stay
.hrow:                              ; inverted until something else repaints:
    mov [di+0], ax                  ; measured at 276 of them, in the margins
    mov [di+4], cx                  ; either side and the row of leading below
    mov [di+2], bx
    mov dx, bx
    add dx, 7
    mov [di+6], dx
    add bx, CAL_ROW_H
    add di, 8
    inc si
    cmp si, CAL_HIST_N
    jb .hrow

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_nrect - how many rects are LIVE right now
; out: AX = CAL_R_HIST + [cal_nvis]; all other registers preserved
; -----------------------------------------------------------------------------
cal_nrect:
    mov ax, [cal_nvis]
    add ax, CAL_R_HIST
    ret

; =============================================================================
; Callbacks
; =============================================================================

; -----------------------------------------------------------------------------
; cal_paint - W_PAINT: full content repaint
; in:  SI = window ptr; caller holds the gfx lock, content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call cal_geom
    call cal_layout
    call cal_drawall
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_onresize - OSAPI_WM_ONRESIZE (SPEC.md 11.98): the box moved and we did
; not ask - an Activate Mode switch, or a drag across an extended desktop's
; seam onto the shorter display (SPEC.md 39.16.3)
; in:  SI = window, CX = new content width, DX = new content height; gfx lock
;      held, and DRAWING IS FORBIDDEN - the kernel's repaint follows
; out: nothing; preserves all registers
;
; [cal_nvis] is the one thing here derived once and kept, so it is the one
; thing this has to put right. Re-deriving through cal_geom rather than from
; CX/DX costs one call and cannot disagree with what cal_paint reads a moment
; later.
; -----------------------------------------------------------------------------
cal_onresize:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call cal_geom
    call cal_layout
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_onclick - W_ONCLICK: arm the control the press landed on, and show it
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; A press ACTS ON NOTHING (SPEC.md 13.7). All it does is arm the control and
; invert it, so sliding off cancels - and the inversion is one XOR fill,
; which is its own inverse and so costs the release no fill of its own.
; -----------------------------------------------------------------------------
cal_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    push cx
    push dx
    call cal_geom                   ; the window may have moved since the last
    call cal_layout                 ; paint; CX/DX are already SCREEN coords,
    pop dx                          ; which is what os88ui_bfind wants
    pop cx
    call cal_abdismiss              ; the credits are up: this press is spent
    jc .out                         ; taking them down and arms nothing, so
                                    ; the release that follows fires nothing
    call cal_nrect
    mov bx, cal_rects
    call os88ui_bfind               ; AX = index + 1, 0 = none
    call os88ui_arm
    or ax, ax
    jz .out
    call cal_invert
.out:
    pop di
    pop si
    pop dx
    pop cx
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
    push cx
    push dx
    push si
    push di
    mov bx, si
    push cx
    push dx
    call cal_geom
    call cal_layout
    pop dx
    pop cx
    call os88ui_fire                ; AX = what the press armed, and it is
    or ax, ax                       ; CLEARED - one release per press, so
    jz .out                         ; there is no stale arm to guard against
    mov di, ax
    call cal_restore                ; ...upright again, whatever happens next
    push di
    call cal_nrect
    mov bx, cal_rects
    call os88ui_bfind               ; AX = the control under the RELEASE
    pop di
    cmp ax, di
    jne .out                        ; a different one, or none: cancelled
    dec di
    cmp di, CAL_NKEY
    jb .keypad
    je .hdr
    sub di, CAL_R_HIST              ; DI = the history row clicked
    mov ax, di
    call cal_hist_load
    jmp short .out
.hdr:
    call cal_hist_toggle            ; resizes AND repaints: nothing after it
    jmp short .out
.keypad:
    mov bx, di
    add bx, bx
    add bx, bx                      ; four bytes per key: a label and a code
    mov al, [cal_keytab+bx+2]
    call cal_key
.out:
    pop di
    pop si
    pop dx
    pop cx
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
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, al                      ; keep the key: cal_geom answers in AX/DX
    mov bx, si
    call cal_geom
    call cal_layout
    call cal_abdismiss              ; any key takes the credits down, and is
    jc .out                         ; itself swallowed doing it
    mov al, cl
    call cal_asckey                 ; AL = CK_*, or CK_NONE
    cmp al, CK_NONE
    je .out
    cmp al, CK_HIST
    jne .clip
    call cal_hist_toggle
    jmp short .out
.clip:
    cmp al, CK_COPY                 ; the clipboard verbs are the menu's own
    jne .cpaste                     ; routines: one implementation each, the
    call cal_copy                   ; key and the item differing only in how
    jmp short .out                  ; they arrive (SPEC.md 65.5)
.cpaste:
    cmp al, CK_PASTE
    jne .plain
    call cal_paste
    jmp short .out
.plain:
    call cal_key
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_asckey - one ASCII byte to a key code
; in:  AL = ascii
; out: AL = CK_*, or CK_NONE; preserves all other registers
; -----------------------------------------------------------------------------
cal_asckey:
    push bx
    push si
    cmp al, '0'
    jb .table
    cmp al, '9'
    ja .table
    sub al, '0'                     ; digits ARE their codes (the CK_* block)
    jmp short .out
.table:
    mov si, cal_asctab
.scan:
    mov bl, [si]
    or bl, bl
    jz .none
    cmp bl, al
    je .hit
    add si, 2
    jmp short .scan
.hit:
    mov al, [si+1]
    jmp short .out
.none:
    mov al, CK_NONE
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; cal_oncmd - AM_ONCMD: a menu command (SPEC.md 12.2)
; in:  AL = item index, AH = menu index, SI = our window, BX = the set;
;      UI task, gfx lock held
; out: nothing; clobbers AX, BX, CX, DX like any window callback
;
; Called exactly like W_ONCLICK, so it draws directly and must repaint
; itself - the kernel does not repaint after a menu command returns. Every
; item here is also a key, so all but the two clipboard verbs end up in
; cal_key and there is one implementation of each.
; -----------------------------------------------------------------------------
cal_oncmd:
    push si
    push di
    mov cx, ax                      ; CL = the item, CH = the menu
    mov bx, si
    call cal_geom
    call cal_layout
    call cal_abdismiss              ; a menu pick takes the credits down first
    or ch, ch                       ; and then does what it was asked - a
    jz .edit                        ; deliberate command is not a dismissal
    dec ch
    jz .math
    ; --- View -----------------------------------------------------------------
    or cl, cl
    jnz .clrhist
    call cal_hist_toggle
    jmp short .out
.clrhist:
    mov byte [cal_hn], 0
    mov byte [cal_hhead], 0
    call cal_repaint                ; a rare command, and the band has to be
    jmp short .out                  ; erased rather than merely re-lettered
    ; --- Edit -----------------------------------------------------------------
.edit:
    or cl, cl
    jnz .paste
    call cal_copy
    jmp short .out
.paste:
    call cal_paste
    jmp short .out
    ; --- Math -----------------------------------------------------------------
.math:
    mov al, CK_SQRT
    or cl, cl
    jz .mgo
    mov al, CK_RECIP
    dec cl
    jz .mgo
    mov al, CK_PCT
    dec cl
    jz .mgo
    mov al, CK_NEG
.mgo:
    call cal_key
.out:
    pop di
    pop si
    ret

; =============================================================================
; 'About Calc' - the credit panel (SPEC.md 12.2/65)
; =============================================================================
; Drawn INSIDE our own content rather than in a window of its own: a package
; cannot put up a second window it does not own, and the keypad is exactly the
; rectangle a card wants anyway (solitaire's sol_about is the precedent).
;
; It is STATE and not a modal loop. [cal_abon] goes up, the card is drawn on
; top, and the next click, key or menu pick takes it down again - a loop here
; would hold the gfx lock against every background task for as long as the
; reader left the credits up.

; -----------------------------------------------------------------------------
; cal_about - the OSAPI_ABOUT_SET handler (slot 0x01E0)
; in:  SI = our window ptr; the UI task, gfx lock HELD, far-called at our
;      segment - a window callback in every respect that matters, so it draws
;      directly and repaints itself
; out: nothing; preserves all registers
;
; Only the CARD is drawn: nothing underneath it has changed, and the card is
; opaque over its own rect, so repainting the content first would be
; PERFORMANCE.md's double-draw flash with the credits as the second layer.
; -----------------------------------------------------------------------------
cal_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call cal_geom                   ; the window may have moved since the last
    call cal_layout                 ; paint, and the card is placed from the
    mov byte [cal_abon], 1          ; live content box
    call cal_abdraw
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; cal_abdismiss - take the card down, if it is up
; in:  the layout is current; gfx lock held
; out: CF = 1 if it WAS up (and the content has been repainted), so the caller
;      swallows the click or key that dismissed it; CF = 0 otherwise. Every
;      register preserved.
;
; The way back is cal_repaint and not cal_abdraw's inverse: the card covered
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

; -----------------------------------------------------------------------------
; cal_abmeas - size and place the card
; in:  [cal_ox]/[cal_oy]/[cal_ch] current
; out: [cal_abw]/[cal_abh]/[cal_abl]/[cal_abt], content coords
; clobbers: nothing (flags)
;
; Measured, never pinned: the widest line sets the width and the count sets
; the height, so a line of the credit can be changed without re-deriving
; anything. Both are clamped to the content, because [cal_ch] is whatever
; wm_fit granted - CAL_BASE_H on a CGA, where the history pane does not fit
; at all (SPEC.md 65.3).
; -----------------------------------------------------------------------------
cal_abmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, cal_ablines
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
    cmp cx, CAL_CW
    jbe .wok
    mov cx, CAL_CW
.wok:
    mov [cal_abw], cx
    mov ax, di                      ; height = lines * CAL_ABLH + margins
    mov bx, CAL_ABLH
    mul bx
    add ax, 16
    cmp ax, [cal_ch]
    jbe .hok
    mov ax, [cal_ch]
.hok:
    mov [cal_abh], ax
    mov ax, CAL_CW
    sub ax, [cal_abw]
    shr ax, 1
    mov [cal_abl], ax
    mov ax, [cal_ch]
    sub ax, [cal_abh]
    shr ax, 1
    mov [cal_abt], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_abdraw - the card: white fill, black frame, the credit centred in it
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; Every line is centred on the CARD rather than on the content, so the block
; still reads as one card when cal_abmeas has clamped it narrower than the
; text wanted.
; -----------------------------------------------------------------------------
cal_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cal_abmeas
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call .rect
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call .rect
    call OSAPI_GFX_FRAME

    mov si, cal_ablines
    mov di, [cal_abt]
    add di, 8                       ; the first baseline, inside the frame
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = this line's width
    mov cx, [cal_abw]
    sub cx, ax
    shr cx, 1
    add cx, [cal_abl]
    add cx, [cal_ox]                ; font_str wants SCREEN coords
    mov dx, di
    add dx, [cal_oy]
    call OSAPI_FONT_STR
    pop si
    add di, CAL_ABLH
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.rect:                              ; the card as (x1,y1)-(x2,y2) SCREEN,
    mov ax, [cal_abl]               ; inclusive
    add ax, [cal_ox]
    mov bx, [cal_abt]
    add bx, [cal_oy]
    mov cx, ax
    add cx, [cal_abw]
    dec cx
    mov dx, bx
    add dx, [cal_abh]
    dec dx
    ret

; =============================================================================
; Drawing (SPEC.md 65.4.2)
; =============================================================================

; -----------------------------------------------------------------------------
; cal_repaint - erase the content and draw the lot
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; W_PAINT does NOT come through here: the kernel has already whitened the
; content in front of it (SPEC.md 11.90.1), and filling it a second time is
; PERFORMANCE.md's double-draw flash with our own picture as the second
; layer. This entry is for the paths that repaint with no kernel fill in
; front of them - a menu command.
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
    call cal_drawall
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    push di

    call cal_shadkill               ; the glass is about to disagree with both
                                    ; shadows, so the next emit must be whole
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [cal_ox]                ; the display well
    add ax, CAL_DISP_X1
    mov bx, [cal_oy]
    add bx, CAL_DISP_Y1
    mov cx, [cal_ox]
    add cx, CAL_DISP_X2
    mov dx, [cal_oy]
    add dx, CAL_DISP_Y2
    call OSAPI_GFX_FRAME
    call cal_show                   ; ...and both fields inside it

    xor si, si                      ; the twenty keys
.key:
    mov ax, si
    call cal_btn
    inc si
    cmp si, CAL_NKEY
    jb .key

    call cal_draw_hdr
    call cal_draw_rows

    cmp byte [cal_abon], 0          ; ...and the credit card over the lot, or
    je .noab                        ; a W_PAINT under it would lose it
    call cal_abdraw
.noab:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_btn - draw keypad button AX
; in:  AX = the key index 0..19, CF = 1 to erase its interior first;
;      the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; OS88UI_FILL rides in on CF because the flag's own question is "can this be
; drawn a second time without the ground being repainted first" (os88ui.inc),
; and the two callers give different answers: cal_drawall's ground is white
; already, cal_restore's is an inverted button.
; -----------------------------------------------------------------------------
cal_btn:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, OS88UI_FILL
    jc .fill
    xor di, di
.fill:
    mov si, ax
    add si, si
    add si, si
    mov si, [cal_keytab+si]         ; SI = the label
    mov cl, 3
    shl ax, cl
    mov bx, cal_rects
    add bx, ax                      ; BX = the rect
    call os88ui_btn
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_invert  - XOR-invert control AX-1's rect (the pressed look)
; cal_restore - put control AX-1 back the way it was drawn
; in:  AX = index + 1 (os88ui_bfind's answer); the layout is current; gfx
;      lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_invert:
    push ax
    push bx
    push cx
    push dx
    push si
    dec ax
    cmp ax, CAL_R_HIST
    jb .rect
    sub ax, CAL_R_HIST              ; a history ROW inverts by being drawn
    stc                             ; again with its two colours swapped -
    call cal_draw_row               ; the same opaque pass over the same
    jmp short .out                  ; cells the release will use
.rect:
    mov cl, 3
    shl ax, cl
    mov si, cal_rects
    add si, ax
    mov ax, [si+0]
    mov bx, [si+2]
    mov cx, [si+4]
    mov dx, [si+6]
    call OSAPI_GFX_XOR_FILL
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cal_restore:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    dec ax
    cmp ax, CAL_NKEY
    jb .key
    je .hdr
    sub ax, CAL_R_HIST
    clc                             ; upright, and said rather than inherited
    call cal_draw_row               ; a row is one opaque run over EXACTLY its
    jmp short .out                  ; own rect, so drawing it again IS the
                                    ; erase (SPEC.md 6.1) and no fill is owed
.hdr:
    mov si, cal_rects + CAL_R_HDR * 8   ; the strip is a generous target and
    mov ax, [si+0]                      ; what is DRAWN in it is a triangle and
    mov bx, [si+2]                      ; a label, so unlike a row it does not
    mov cx, [si+4]                      ; cover its own rect - the inversion
    mov dx, [si+6]                      ; has to be erased before cal_draw_hdr
    push ax                             ; puts the two back. One fill, on the
    mov al, CWHITE                      ; release path only: the full-paint
    call OSAPI_SET_COLOR                ; path draws over ground the kernel
    pop ax                              ; has already whitened (SPEC.md 11.90.1)
    call OSAPI_GFX_FILL
    call cal_draw_hdr
    jmp short .out
.key:
    stc                             ; the ground under it is inverted
    call cal_btn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_draw_hdr - the disclosure rule, triangle and label
; in:  the layout is current; gfx lock held
; out: nothing; preserves all registers
;
; The triangle is four fills out of a table rather than a glyph, because the
; kernel's font is ASCII 32..126 and has no arrow in it (font.inc). The label
; is static - the triangle's DIRECTION is the state - so nothing here changes
; on a fold and the strip is drawn once per repaint.
; -----------------------------------------------------------------------------
cal_draw_hdr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov al, CLGRAY                  ; the separator: a 50% dither, which is the
    call OSAPI_SET_COLOR            ; dotted rule this OS's model draws (39.4)
    mov ax, [cal_ox]
    add ax, CAL_DISP_X1
    mov bx, [cal_oy]
    add bx, CAL_HDR_RULE
    mov cx, [cal_ox]
    add cx, CAL_DISP_X2
    mov dx, bx
    call OSAPI_GFX_FILL

    mov al, CWHITE                  ; the triangle's own cell, so the two
    call OSAPI_SET_COLOR            ; states cannot overlay each other
    mov ax, [cal_ox]
    add ax, CAL_TRI_X
    mov bx, [cal_oy]
    add bx, CAL_HDR_TY
    mov cx, ax
    add cx, 6
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, cal_tri_shut
    cmp byte [cal_open], 0
    je .tri
    mov si, cal_tri_open
.tri:
    mov di, 4                       ; four bands, {x1,y1,x2,y2} relative bytes
.band:
    mov al, [si+0]
    xor ah, ah
    add ax, [cal_ox]
    add ax, CAL_TRI_X
    mov bl, [si+1]
    xor bh, bh
    add bx, [cal_oy]
    add bx, CAL_HDR_TY
    mov cl, [si+2]
    xor ch, ch
    add cx, [cal_ox]
    add cx, CAL_TRI_X
    mov dl, [si+3]
    xor dh, dh
    add dx, [cal_oy]
    add dx, CAL_HDR_TY
    call OSAPI_GFX_FILL
    add si, 4
    dec di
    jnz .band

    mov si, cal_s_hist
    mov cx, [cal_ox]
    add cx, CAL_HDR_TX
    mov dx, [cal_oy]
    add dx, CAL_HDR_TY
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

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
    clc                             ; NOT the `cmp` above: below-and-not-equal
    call cal_draw_row               ; leaves CF SET, and CF is what says
    inc ax                          ; INVERTED now
    jmp short .row
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_draw_row - one history row, as ONE opaque run
; in:  AX = the screen row, CF = 1 to draw it INVERTED (the pressed look);
;      gfx lock held
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
; -----------------------------------------------------------------------------
cal_draw_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, CBLACK + CWHITE * 256    ; ink black on white...
    jnc .pen
    mov bx, CWHITE + CBLACK * 256    ; ...or the pressed look, same pass
.pen:
    push bx
    mov di, ax                      ; DI = the screen row
    call cal_hslot                  ; SI = its text, or 0 for an empty row
    or si, si
    jnz .have
    mov si, cal_blankrow
.have:
    mov ax, di
    mov bx, CAL_ROW_H
    mul bx
    add ax, CAL_HIST_Y0
    add ax, [cal_oy]
    mov dx, ax
    mov cx, [cal_ox]
    add cx, CAL_HROW_X
    pop ax
    call OSAPI_FONT_RUN
    pop di
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
    push ax
    push dx
    cmp al, [cal_hn]
    jae .none
    mov bl, [cal_hhead]             ; the newest is at [cal_hhead]; row i is i
    sub bl, al                      ; slots back from it, around the ring
    and bl, CAL_HIST_N - 1
    xor bh, bh
    mov ax, bx
    mov dx, CAL_HROW_N + 1
    mul dx
    mov si, cal_htext
    add si, ax
    jmp short .out
.none:
    xor si, si
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_show - both display fields, emitting only the cells that changed
; in:  gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cal_compose                ; the two fields, as they SHOULD read
    mov word [cal_penx], CAL_CTX_X
    mov si, cal_cbuf
    mov di, cal_cshad
    mov cx, CAL_CTX_N
    call cal_emit
    mov word [cal_penx], CAL_NUM_X
    mov si, cal_nbuf
    mov di, cal_nshad
    mov cx, CAL_NUM_N
    call cal_emit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_emit - draw the cells of a fixed-width field that differ from the glass
; in:  SI = the composed field, DI = the shadow of what is DRAWN, CX = cells,
;      [cal_penx] = the field's content-relative pen x; gfx lock held
; out: nothing; preserves all registers
;
; The shadow is the record of the GLASS, so composing IS the diff and no
; second structure can fall out of step (SPEC.md 12.9's menu_bcell is the
; same argument one layer up). BOTH ends are found rather than just the
; first, so backspacing a digit off a right-aligned number redraws the two
; cells that moved and not the eleven behind them.
; -----------------------------------------------------------------------------
cal_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    xor bx, bx                      ; BX indexes BOTH buffers throughout: an
.first:                             ; 8086 effective address is base + index
    cmp bx, cx                      ; and SI/DI are indexes, so [si+dx] and
    jae .out                        ; [di+si] are not addresses at all
    mov al, [si+bx]
    cmp al, [di+bx]
    jne .gotf
    inc bx
    jmp short .first
.gotf:
    mov [cal_efirst], bx
    mov bx, cx                      ; ...and now the LAST differing cell
    dec bx
.lscan:
    mov al, [si+bx]
    cmp al, [di+bx]
    jne .gotl
    dec bx
    jmp short .lscan
.gotl:
    inc bx                          ; BX = ONE PAST the last differing cell
    mov [cal_eterm], bx
    mov al, [si+bx]                 ; bank the byte the terminator replaces -
    mov [cal_ebank], al             ; the field's own NUL is at its far end,
    mov byte [si+bx], 0             ; and the span usually stops short of it

    mov bx, [cal_efirst]
    mov ax, bx
    mov cl, 3
    shl ax, cl                      ; the pen: the field's, plus 8 per cell
    add ax, [cal_penx]
    add ax, [cal_ox]
    mov cx, ax
    mov dx, [cal_oy]
    add dx, CAL_TXT_Y
    add si, bx
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    sub si, bx

    mov bx, [cal_eterm]             ; ...and the byte back
    mov al, [cal_ebank]
    mov [si+bx], al

    mov bx, [cal_efirst]            ; the glass now says what we composed
    mov cx, [cal_eterm]
.copy:
    cmp bx, cx
    jae .out
    mov al, [si+bx]
    mov [di+bx], al
    inc bx
    jmp short .copy

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_shadkill - forget what the glass says, so the next emit draws it whole
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_shadkill:
    push ax
    push bx
    xor bx, bx
.z:
    mov byte [cal_cshad+bx], 0xFF   ; 0xFF is not a character either field can
    mov byte [cal_nshad+bx], 0xFF   ; hold, so every cell reads as different
    inc bx
    cmp bx, CAL_NUM_N
    jb .z
    mov byte [cal_cshad+CAL_CTX_N], 0
    mov byte [cal_nshad+CAL_NUM_N], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_compose - build both display fields from the live state
; out: cal_cbuf / cal_nbuf, space-padded and NUL-terminated; all registers
;      preserved
; -----------------------------------------------------------------------------
cal_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    cmp byte [cal_err], 0           ; --- the number field ---------------------
    je .wait
    mov si, cal_s_error
    jmp short .nput
.wait:
    cmp byte [cal_wait], 0          ; an operator was the last key: the field
    je .num                         ; is empty, waiting for the next number
    mov si, cal_blankrow            ; (SPEC.md 65.2.1)
    jmp short .nput
.num:
    mov bx, cal_cur
    mov di, cal_scratch
    mov al, [cal_dot]
    call cal_fmtv                   ; CX = the length; cal_fmtv does not
    mov bx, cx                      ; terminate, so cal_rpad's own length walk
    mov byte [cal_scratch+bx], 0    ; needs the NUL put on here
    mov si, cal_scratch
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
    mov di, cal_scratch
    add di, cx
    mov byte [di], ' '
    mov bl, [cal_op]
    xor bh, bh
    sub bx, CK_ADD
    mov al, [cal_opchar+bx]
    mov [di+1], al
    mov byte [di+2], 0
    mov si, cal_scratch
.cput:
    mov di, cal_cbuf
    mov cx, CAL_CTX_N
    call cal_rpad

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_rpad - copy a NUL string into a fixed field, right-aligned
; in:  SI = the source, DI = the destination, CX = cells
; out: the field, space-padded and NUL-terminated at [DI+CX]; all registers
;      preserved
;
; A string LONGER than the field keeps its TAIL, because the low-order digits
; of a number are the ones a truncation may not eat.
; -----------------------------------------------------------------------------
cal_rpad:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
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
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
.body:
    mov cx, bx
    jcxz .term
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp
.term:
    mov byte [di], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    cmp al, CK_CLR
    je .clr
    cmp al, CK_CE
    je .ce
    cmp byte [cal_err], 0
    jne .no                         ; refused: only the two clears act
    cmp al, CK_HIST
    ja .no
    xor ah, ah
    mov bx, ax
    add bx, bx
    jmp word [cal_jtab+bx]          ; every arm is a near proc ending in ret
.clr:
    jmp cal_clearall
.ce:
    jmp cal_clearent
.no:
    ret

; -----------------------------------------------------------------------------
; cal_kdigit - append the banked digit to the entry
; out: nothing
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
; a number the machine never computed.
; -----------------------------------------------------------------------------
cal_kbs:
    cmp byte [cal_new], 0
    jne .out
    mov di, cal_q
    mov ax, [cal_cur+CV_M+0]
    mov dx, [cal_cur+CV_M+2]
    call cq_set32
    call cq_div10
    mov ax, [cal_q+0]
    mov [cal_cur+CV_M+0], ax
    mov ax, [cal_q+2]
    mov [cal_cur+CV_M+2], ax
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
    cmp word [cal_cur+CV_M+0], 0    ; -0 is 0, and showing '-0' reads as a bug
    jne .flip
    cmp word [cal_cur+CV_M+2], 0
    je .out
.flip:
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
    call cal_reduce                 ; acc = acc <op> cur, and cur follows it
    cmp byte [cal_err], 0
    jne .out
.set:
    mov si, cal_cur                 ; the operand is whatever is on show
    mov di, cal_acc
    call cal_vcopy
    mov al, [cal_kcode]
    mov [cal_op], al
    mov byte [cal_new], 1
    mov byte [cal_dot], 0
    mov byte [cal_wait], 1          ; the operand has moved to the left of the
                                    ; display, so the right goes BLANK: the
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
; cal_hist_prep runs BEFORE cal_reduce because cal_reduce overwrites both
; operands with the answer, and cal_hist_push after it because the answer is
; the rest of the line. Splitting the composition in two is the whole of what
; that ordering costs.
; -----------------------------------------------------------------------------
cal_keq:
    cmp byte [cal_op], CAL_OP_NONE
    je .out                         ; nothing pending: '=' does not repeat here
    call cal_hist_prep
    call cal_reduce
    mov byte [cal_op], CAL_OP_NONE
    mov byte [cal_new], 1
    mov byte [cal_dot], 0
    cmp byte [cal_err], 0
    jne .out
    call cal_hist_push              ; ...and the row goes up with the answer
.out:
    mov word [cal_linep], 0         ; spent, or dropped: either way the next
    ret                             ; key must not find a stale line here

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
    mov si, cal_s_recip             ; '1/' <operand>
    mov di, cal_s_empty
    call cal_hist_prep_un
    mov si, cal_one                 ; 1 / x is cal_calc's divide with a
    mov di, cal_cur                 ; constant left operand
    mov al, CK_DIV
    call cal_calc
    jc .fail
    mov si, cal_res
    mov di, cal_cur
    call cal_vcopy
    jmp short cal_unary_end
.fail:
    mov byte [cal_err], 1
    jmp short cal_unary_end

; The four-function convention rather than a bare divide by a hundred: with
; + or - pending, `200 + 10 %` is 200 + 20, because that is what the key is
; FOR. With x or / pending, or nothing pending, it is the plain hundredth.
cal_kpct:
    cmp byte [cal_op], CK_ADD
    je .of
    cmp byte [cal_op], CK_SUB
    jne .plain
.of:
    mov si, cal_acc
    mov di, cal_cur
    mov al, CK_MUL
    call cal_calc
    jc .fail
    mov si, cal_res
    mov di, cal_cur
    call cal_vcopy
.plain:
    mov si, cal_cur
    mov di, cal_hundred
    mov al, CK_DIV
    call cal_calc
    jc .fail
    mov si, cal_res
    mov di, cal_cur
    call cal_vcopy
    jmp short cal_unary_end
.fail:
    mov byte [cal_err], 1

cal_unary_end:
    mov byte [cal_new], 1
    mov byte [cal_dot], 0
    cmp byte [cal_err], 0
    jne .out                        ; a refusal is not a calculation, and the
    cmp word [cal_linep], 0         ; line staged for it is dropped unspent
    je .out                         ; (% stages none: it is a change to the
    call cal_hist_push              ; PENDING entry rather than a sum of its
.out:                               ; own, and a row saying '10 = 20' would be
    mov word [cal_linep], 0         ; a lie about what the user did)
    ret

cal_knop:
    ret

; -----------------------------------------------------------------------------
; cal_fresh - start a new entry if the display is holding a result
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_fresh:
    cmp byte [cal_new], 0
    je .out
    push si
    push di
    mov si, cal_zero
    mov di, cal_cur
    call cal_vcopy
    pop di
    pop si
    mov byte [cal_new], 0
    mov byte [cal_dot], 0
.out:
    ret

; -----------------------------------------------------------------------------
; cal_clearall / cal_clearent - the two clears
; out: nothing; preserves all registers
;
; C falls through into CE: clearing everything is clearing the entry plus
; forgetting the pending operation, so there is one body for the part they
; share and no way for the two to disagree about what a cleared entry is.
; -----------------------------------------------------------------------------
cal_clearall:
    push si
    push di
    mov si, cal_zero
    mov di, cal_acc
    call cal_vcopy
    pop di
    pop si
    mov byte [cal_op], CAL_OP_NONE
cal_clearent:
    push si
    push di
    mov si, cal_zero
    mov di, cal_cur
    call cal_vcopy
    pop di
    pop si
    mov byte [cal_err], 0
    mov byte [cal_new], 1
    mov byte [cal_dot], 0
    ret

; -----------------------------------------------------------------------------
; cal_reduce - acc = acc <cal_op> cur, and put the answer on show
; out: [cal_err] set on a refusal; preserves all registers
; -----------------------------------------------------------------------------
cal_reduce:
    push ax
    push si
    push di
    mov si, cal_acc
    mov di, cal_cur
    mov al, [cal_op]
    call cal_calc
    jc .fail
    mov si, cal_res
    mov di, cal_acc
    call cal_vcopy
    mov si, cal_res
    mov di, cal_cur
    call cal_vcopy
    jmp short .out
.fail:
    mov byte [cal_err], 1
.out:
    pop di
    pop si
    pop ax
    ret

; =============================================================================
; History (SPEC.md 65.4.1)
; =============================================================================

; -----------------------------------------------------------------------------
; cal_hist_prep - compose the line for the sum about to be done
; out: cal_line holds '<acc> <op> <cur> = ' and [cal_linep] points past it;
;      preserves all registers
; -----------------------------------------------------------------------------
cal_hist_prep:
    push ax
    push bx
    push cx
    push di
    mov di, cal_line
    mov bx, cal_acc
    xor al, al
    call cal_fmtv
    add di, cx
    mov byte [di], ' '
    mov bl, [cal_op]
    xor bh, bh
    sub bx, CK_ADD
    mov al, [cal_opchar+bx]
    mov [di+1], al
    mov byte [di+2], ' '
    add di, 3
    mov bx, cal_cur
    xor al, al
    call cal_fmtv
    add di, cx
    mov byte [di], ' '
    mov byte [di+1], '='
    mov byte [di+2], ' '
    add di, 3
    mov [cal_linep], di
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_hist_prep_un - the same, for a one-operand key
; in:  SI = the prefix ('sqrt(' / '1/'), DI = the suffix (')' / '')
; out: cal_line holds '<pfx><cur><sfx> = '; preserves all registers
; -----------------------------------------------------------------------------
cal_hist_prep_un:
    push ax
    push bx
    push cx
    push si
    push di
    push di                         ; the suffix, for after the operand
    mov di, cal_line
    call cal_strcat
    mov bx, cal_cur
    xor al, al
    call cal_fmtv
    add di, cx
    pop si
    call cal_strcat
    mov byte [di], ' '
    mov byte [di+1], '='
    mov byte [di+2], ' '
    add di, 3
    mov [cal_linep], di
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_strcat - copy the NUL string at SI to DI, leaving DI past it
; in:  SI = source, DI = destination
; out: DI advanced past the bytes copied, NOT terminated; SI preserved
; -----------------------------------------------------------------------------
cal_strcat:
    push ax
    push si
.b:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc si
    inc di
    jmp short .b
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_hist_push - finish the line, ring it in, and move the pixels
; out: nothing; preserves all registers
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
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, [cal_linep]             ; the answer completes the line
    mov bx, cal_cur
    xor al, al
    call cal_fmtv
    add di, cx
    mov byte [di], 0

    mov al, [cal_hhead]             ; the ring advances by one byte
    cmp byte [cal_hn], 0
    je .first
    inc al
    and al, CAL_HIST_N - 1
.first:
    mov [cal_hhead], al
    cmp byte [cal_hn], CAL_HIST_N
    jae .full
    inc byte [cal_hn]
.full:
    xor ah, ah
    mov bx, ax                      ; BX = the slot
    mov dx, CAL_HROW_N + 1
    mul dx
    mov di, cal_htext
    add di, ax                      ; ...padded to the field, so that drawing
    mov si, cal_line                ; the row IS the erase (SPEC.md 6.1)
    mov cx, CAL_HROW_N
    call cal_lpad

    mov ax, bx                      ; and the ANSWER beside it, because a
    mov cl, 3                       ; click loads the value rather than
    shl ax, cl                      ; re-reading a line the field may have
    mov di, cal_hval                ; truncated
    add di, ax
    mov si, cal_cur
    call cal_vcopy

    cmp word [cal_nvis], 0
    je .out                         ; folded up: the ring moved, no pixels did
%ifdef CALC_NOSCROLL
    jmp short .allrows              ; make CALCSCROLL=0: the reference build,
%endif                              ; which letters every row instead. It is
                                    ; the A/B for the paragraph above rather
                                    ; than a fallback - the claim is that one
                                    ; blit plus one run beats eight runs, and
                                    ; a claim about drawing is worth what it
                                    ; is measured at (tests/calcflick.py)
    cmp word [cal_nvis], 1
    jbe .row0                       ; one row: there is nothing to scroll

    mov ax, [cal_ox]                ; the band, x-aligned by construction -
    add ax, CAL_HROW_X              ; OSAPI_WM_SNAP keeps the content origin on
    mov bx, [cal_oy]                ; a byte and both edges are multiples of 8
    add bx, CAL_HIST_Y0
    mov cx, ax
    add cx, CAL_HROW_N * 8 - 1
    mov ax, [cal_nvis]
    mov dx, CAL_ROW_H
    mul dx
    mov dx, bx
    add dx, ax
    dec dx
    mov ax, [cal_ox]
    add ax, CAL_HROW_X
    mov si, -CAL_ROW_H              ; negative scrolls DOWN (SPEC.md 5.5)
    call OSAPI_GFX_SCROLL
    jc .allrows                     ; refused: nothing moved, letter them all
.row0:
    xor ax, ax
    clc
    call cal_draw_row
    jmp short .out
.allrows:
    call cal_draw_rows
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_lpad - copy a NUL string into a fixed field, left-aligned
; in:  SI = the source, DI = the destination, CX = cells
; out: the field, space-padded and NUL-terminated at [DI+CX]; all registers
;      preserved
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
    push ax
    push bx
    push cx
    push si
    push di
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
    push cx
    mov cx, bx
    jcxz .pad
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp
.pad:
    pop cx
    sub cx, bx
    jcxz .term
    cmp cx, 1                       ; exactly one cell short means the line was
    jne .sp                         ; cut: say so rather than pad it to look
    cmp byte [si], 0                ; complete. A line that happens to end one
    je .sp                          ; cell early is NOT cut - the source's own
    mov byte [di], CAL_CUT          ; NUL is what tells the two apart
    inc di
    dec cx
    jcxz .term
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
.term:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop bx
    pop ax
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
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cal_hslot                  ; BX = the slot, SI = 0 for an empty row
    or si, si
    jz .out
    mov ax, bx
    mov cl, 3
    shl ax, cl
    mov si, cal_hval
    add si, ax
    mov di, cal_cur
    call cal_vcopy
    mov byte [cal_err], 0
    mov byte [cal_new], 1           ; it is a RESULT, so a digit replaces it
    mov byte [cal_dot], 0
    mov byte [cal_wait], 0          ; ...and it is a number to LOOK at, so the
                                    ; field is not waiting any more - this one
                                    ; does not come through cal_dispatch
    call cal_show
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_hist_toggle - fold the history pane down, or back up
; out: nothing; preserves all registers
;
; OSAPI_WM_RESIZE clamps to the live screen and REPAINTS (SPEC.md 11.1), so
; there is nothing to draw after it and nothing here needs to know what a CGA
; can spare: it asks for CAL_OPEN_H, and cal_geom - which the repaint runs -
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
    cmp byte [cal_open], 0
    jne .word
    mov si, cal_mi_show
.word:
    mov ax, [si+0]
    mov [cal_mi_hist+0], ax
    mov ax, [si+2]
    mov [cal_mi_hist+2], ax
    mov cx, CAL_CW + 2
    mov dx, CAL_BASE_H + TITLE_H + 1
    cmp byte [cal_open], 0
    je .go
    mov dx, CAL_OPEN_H + TITLE_H + 1
.go:
    mov bx, [cal_win]
    call OSAPI_WM_RESIZE
    mov bx, [cal_win]
    call cal_geom                   ; ...and our own idea of the box follows
    call cal_layout                 ; whatever the clamp gave us
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    mov ax, ds
    mov es, ax                      ; ES:SI, not our DS (the slot's contract)
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
    push si
    push di
    push es
    mov ax, ds
    mov es, ax
    mov di, cal_pbuf                ; ITS OWN BUFFER, and that is the whole of
    mov cx, CAL_PASTE_MAX           ; what a paste needs from this app that a
    call OSAPI_CLIP_GET             ; copy does not. The loop below drives
    jc .out                         ; cal_key once per character, cal_key ends
    jcxz .out                       ; in cal_show, and cal_show composes the
    mov byte [cal_err], 0           ; display through cal_scratch - so pasting
    mov byte [cal_new], 1           ; INTO cal_scratch had every keystroke
    mov byte [cal_dot], 0           ; overwrite the characters not yet read.
    mov si, cal_pbuf                ; '88' came back as '87' or '8': the second
    mov bx, cx                      ; byte was whatever the first digit's own
                                    ; display text had just put there
.ch:
    or bx, bx
    jz .done
    mov al, [si]
    cmp al, '-'                     ; a leading sign, and only a leading one
    jne .code
    cmp cx, bx
    jne .done
    mov byte [cal_pneg], 1
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
    cmp byte [cal_pneg], 0
    je .show
    mov byte [cal_pneg], 0
    mov al, CK_NEG
    call cal_key
.show:
    call cal_show
.out:
    pop es
    pop di
    pop si
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
    or cx, cx
    jz .ok
    stc
    jmp short .out
.ok:
    clc
.out:
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
    or ax, [di+6]
    or ax, ax
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
    cmp word [di+6], 0
    jne .big
    cmp word [di+4], 0
    jne .big
    mov ax, [di+2]
    cmp ax, (CAL_MAXM >> 16)
    ja .big
    jb .ok
    mov ax, [di+0]
    cmp ax, (CAL_MAXM & 0xFFFF)
    ja .big
.ok:
    clc
    pop ax
    ret
.big:
    stc
    pop ax
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
; cal_pack - turn cal_q / [cal_wd] / [cal_wsgn] into the value at DI
; in:  DI = the destination, cal_q the magnitude, [cal_wd] its decimal places
;      (0..16), [cal_wsgn] its sign
; out: CF = 1 and the destination untouched on overflow; preserves all
;      registers
;
; Three passes, in this order and no other: bring the DECIMALS inside the
; field, bring the MAGNITUDE inside it - which is where an overflow is
; genuinely an overflow, there being no decimals left to give - then strip
; trailing zeros so that 2.50 shows as 2.5. The strip divides and puts the
; digit back when it was not a zero, which is cheaper than a copy and cannot
; drift from the divide it is testing.
; -----------------------------------------------------------------------------
cal_pack:
    push ax
    push di
    push si
    mov si, di                      ; SI = where the answer goes
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
    push ax                         ; ...not a zero: put it straight back
    call cq_mul10
    pop ax
    call cq_add16
    jmp short .zero
.took:
    dec byte [cal_wd]
    jmp short .strip
.zero:
    call cq_zero
    jnz .store
    mov byte [cal_wd], 0            ; a zero has no decimals and no sign, or
    mov byte [cal_wsgn], 0          ; the display reads '-0.00'
.store:
    mov ax, [cal_q+0]
    mov [si+CV_M+0], ax
    mov ax, [cal_q+2]
    mov [si+CV_M+2], ax
    mov al, [cal_wd]
    mov [si+CV_D], al
    mov al, [cal_wsgn]
    mov [si+CV_SGN], al
    clc
    jmp short .out
.over:
    stc
.out:
    pop si
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_vcopy - the value at SI to the one at DI
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
cal_vcopy:
    push ax
    push cx
    push si
    push di
    mov cx, CV_SZ
.b:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .b
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_calc - cal_res = [SI] <AL> [DI]
; in:  SI = the left operand, DI = the right, AL = CK_ADD..CK_DIV
; out: cal_res holds the answer; CF = 1 on an overflow or a division by zero;
;      preserves all registers
; -----------------------------------------------------------------------------
cal_calc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [cal_a], si
    mov [cal_b], di
    cmp al, CK_MUL
    je .mul
    cmp al, CK_DIV
    je .div
    mov byte [cal_subf], 0          ; add, or subtract
    cmp al, CK_SUB
    jne .as
    mov byte [cal_subf], 1
.as:
    call cal_addsub
    jmp short .out
.mul:
    call cal_mul
    jmp short .out
.div:
    call cal_div
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_addsub - cal_res = [cal_a] +/- [cal_b], per [cal_subf]
; out: CF = 1 on overflow
;
; Both magnitudes are raised to the LARGER decimal count first, so the sum is
; exact before anything is rounded - a 9-digit number raised by at most eight
; places is 10^17, which is why the intermediates are 64-bit and why nothing
; here can overflow before cal_pack gets to decide.
; -----------------------------------------------------------------------------
cal_addsub:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov si, [cal_a]
    mov bx, [cal_b]
    mov al, [si+CV_D]               ; dt = max(da, db)
    mov ah, [bx+CV_D]
    cmp al, ah
    jae .dt
    mov al, ah
.dt:
    mov [cal_wd], al

    mov di, cal_q                   ; A, raised to dt
    mov ax, [si+CV_M+0]
    mov dx, [si+CV_M+2]
    call cq_set32
    mov cl, [cal_wd]
    sub cl, [si+CV_D]
    call cq_scale

    mov di, cal_q2                  ; B, likewise
    mov ax, [bx+CV_M+0]
    mov dx, [bx+CV_M+2]
    call cq_set32
    mov cl, [cal_wd]
    sub cl, [bx+CV_D]
    call cq_scale

    mov al, [si+CV_SGN]             ; AL = A's sign, AH = B's, subtraction
    mov ah, [bx+CV_SGN]             ; being just B with its sign flipped
    xor ah, [cal_subf]
    and ah, 1

    cmp al, ah
    jne .diff
    mov si, cal_q2                  ; like signs: the magnitudes add
    mov di, cal_q
    call cq_add
    mov [cal_wsgn], al
    jmp short .pack
.diff:
    mov si, cal_q                   ; unlike: the larger magnitude keeps its
    mov di, cal_q2                  ; sign and the smaller comes off it
    call cq_cmp                     ; compares [cal_q] against [cal_q2]
    jb .bwins
    mov si, cal_q2
    mov di, cal_q
    call cq_sub
    mov [cal_wsgn], al
    jmp short .pack
.bwins:
    mov si, cal_q
    mov di, cal_q2
    call cq_sub
    mov si, cal_q2
    mov di, cal_q
    call cq_copy
    mov [cal_wsgn], ah
.pack:
    mov di, cal_res
    call cal_pack
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_mul - cal_res = [cal_a] * [cal_b]
; out: CF = 1 on overflow
; -----------------------------------------------------------------------------
cal_mul:
    push ax
    push bx
    push si
    push di
    mov si, [cal_a]
    mov bx, [cal_b]
    mov al, [si+CV_D]               ; the decimals ADD: 1.5 * 1.5 has two
    add al, [bx+CV_D]
    mov [cal_wd], al
    mov al, [si+CV_SGN]
    xor al, [bx+CV_SGN]
    mov [cal_wsgn], al
    add si, CV_M
    add bx, CV_M
    mov di, cal_q
    call cq_mul32                   ; at most 10^18, and a 64-bit pair holds it
    mov di, cal_res
    call cal_pack
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_div - cal_res = [cal_a] / [cal_b]
; out: CF = 1 on a zero divisor or an overflow
;
; The dividend is raised by ten until either the answer would carry the eight
; decimals the field allows or the quotient has reached nine digits, and only
; then divided - so 1/3 comes out 0.33333333 rather than 0. The FIRST loop is
; not an optimisation: a negative decimal count is not representable, so a
; divisor with more decimals than the dividend MUST be scaled past.
; -----------------------------------------------------------------------------
cal_div:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov si, [cal_a]
    mov bx, [cal_b]
    cmp word [bx+CV_M+0], 0
    jne .live
    cmp word [bx+CV_M+2], 0
    je .zero                        ; a zero divisor: a refusal, not an answer
.live:
    mov al, [si+CV_SGN]
    xor al, [bx+CV_SGN]
    mov [cal_wsgn], al
    mov ax, [bx+CV_M+0]
    mov [cal_dv+0], ax
    mov ax, [bx+CV_M+2]
    mov [cal_dv+2], ax

    mov di, cal_q                   ; the dividend
    mov ax, [si+CV_M+0]
    mov dx, [si+CV_M+2]
    call cq_set32

    mov al, [si+CV_D]               ; the answer's decimals so far: da - db,
    sub al, [bx+CV_D]               ; and it may well be negative
    mov [cal_wd], al

    mov di, cal_q2                  ; the precision target: divisor * 10^8, so
    mov si, cal_dv                  ; that "the quotient has nine digits" is
    mov bx, cal_e8                  ; one 64-bit compare and no division
    call cq_mul32

    mov di, cal_q
.raise:
    cmp byte [cal_wd], 0            ; mandatory: a negative count first
    jl .up
    cmp byte [cal_wd], CAL_MAXD     ; then optional, while precision is short
    jge .quot
    mov si, cal_q
    mov di, cal_q2
    call cq_cmp
    jae .quot
    mov di, cal_q
.up:
    call cq_mul10
    inc byte [cal_wd]
    jmp short .raise

.quot:
    mov ax, [cal_dv+2]              ; round to nearest: + divisor/2 before the
    shr ax, 1                       ; divide, which is exact - the divisor is
    mov [cal_q2+2], ax              ; at most nine digits, so its halving
    mov ax, [cal_dv+0]              ; carries no further than this
    rcr ax, 1
    mov [cal_q2+0], ax
    mov word [cal_q2+4], 0
    mov word [cal_q2+6], 0
    mov si, cal_q2
    mov di, cal_q
    call cq_add
    call cq_divmod
    mov di, cal_res
    call cal_pack
    jmp short .out
.zero:
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
; cal_do_sqrt - cur = sqrt(cur)
; out: [cal_err] set on a refusal; preserves all registers
;
; The magnitude is scaled by an EVEN number of decimal places before the
; root, because sqrt(m * 10^e) = sqrt(m) * 10^(e/2) only divides cleanly when
; e is even - and it is scaled as far as 2^53 allows, because every factor of
; a hundred put in buys one more digit of answer.
; -----------------------------------------------------------------------------
cal_do_sqrt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    cmp byte [cal_cur+CV_SGN], 0
    je .ok
    cmp word [cal_cur+CV_M+0], 0    ; the root of a negative number is refused;
    jne .fail                       ; a negative ZERO is not negative
    cmp word [cal_cur+CV_M+2], 0
    jne .fail
.ok:
    mov di, cal_q
    mov ax, [cal_cur+CV_M+0]
    mov dx, [cal_cur+CV_M+2]
    call cq_set32
    mov al, [cal_cur+CV_D]
    test al, 1
    jz .even
    call cq_mul10                   ; an odd count: one more place makes it even
    inc al
.even:
    mov [cal_we], al
.grow:
    cmp byte [cal_we], 14
    ja .root
    cmp word [cal_q+6], 0x0100      ; under 2^56, so a factor of a hundred is
    jae .root                       ; still under 2^63. It is the TOP word that
                                    ; has to be tested and the first version
                                    ; tested word 2 against 0x0020 - a bound of
                                    ; 2^37, not 2^53 - so the scaling stopped
                                    ; four decimal places early and sqrt(2) came
                                    ; out 1.414213 instead of 1.41421356: right
                                    ; to every digit it printed, and three short
    mov di, cal_q
    call cq_mul10
    call cq_mul10
    add byte [cal_we], 2
    jmp short .grow
.root:
    call cal_isqrt                  ; cal_q = floor(sqrt(cal_q))
    mov al, [cal_we]
    shr al, 1
    mov [cal_wd], al
    mov byte [cal_wsgn], 0
    mov di, cal_cur
    call cal_pack
    jnc .out
.fail:
    mov byte [cal_err], 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_isqrt - cal_q = floor(sqrt(cal_q))
; out: nothing; preserves all registers
;
; Newton from a seed one bit above the true root, so the sequence falls
; monotonically and "it stopped falling" IS the answer - no epsilon, and no
; iteration count to tune. Six or seven divides, which at about 4ms each is
; well under the threshold at which a key press stops feeling immediate.
; -----------------------------------------------------------------------------
cal_isqrt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

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
    sub bx, 2
    jmp short .word
.bit:
    shl ax, 1
    jc .seed
    dec cx
    jmp short .bit
.seed:
    shr cx, 1                       ; the seed is 1 << ceil(L/2), which is
    adc cx, 0                       ; above sqrt for every L (CF = L's low bit)
    mov word [cal_x+0], 1
    mov word [cal_x+2], 0
.sh:
    shl word [cal_x+0], 1
    rcl word [cal_x+2], 1
    loop .sh

.iter:
    mov si, cal_q3                  ; q = radicand / x, which fits a dword
    mov di, cal_q                   ; because x is never below the true root
    call cq_copy
    mov ax, [cal_x+0]
    mov [cal_dv+0], ax
    mov ax, [cal_x+2]
    mov [cal_dv+2], ax
    call cq_divmod
    mov ax, [cal_q+0]               ; nx = (x + q) / 2, and the carry out of
    add ax, [cal_x+0]               ; the add is the 33rd bit, so it shifts
    mov dx, [cal_q+2]               ; back in through the rcr pair
    adc dx, [cal_x+2]
    rcr dx, 1
    rcr ax, 1
    cmp dx, [cal_x+2]
    ja .done
    jb .step
    cmp ax, [cal_x+0]
    jae .done
.step:
    mov [cal_x+0], ax
    mov [cal_x+2], dx
    jmp short .iter
.done:
    mov di, cal_q
    mov ax, [cal_x+0]
    mov dx, [cal_x+2]
    call cq_set32
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cal_fmtv - the value at BX as decimal text at DI
; in:  BX = the value, DI = the destination, AL non-zero = show a trailing
;      point (the '12.' a half-typed entry is showing)
; out: CX = the characters written, NOT terminated; preserves all other
;      registers
;
; The digits come out backwards into cal_dig and are then walked forwards
; with the point put in, which is the only way round that can pad the
; INTEGER side: 0.05 is a magnitude of 5 with two decimals, and its leading
; zero exists nowhere in the magnitude.
; -----------------------------------------------------------------------------
cal_fmtv:
    push ax
    push bx
    push dx
    push si
    push di
    mov [cal_fdot], al

    mov ax, [bx+CV_M+0]             ; the magnitude, least digit first
    mov dx, [bx+CV_M+2]
    xor si, si                      ; SI = how many
.dig:
    push bx
    mov bx, 10
    mov cx, ax                      ; CX = the low word
    mov ax, dx                      ; the high half first, so its remainder
    xor dx, dx                      ; carries down into the low half
    div bx
    push ax                         ; the high quotient
    mov ax, cx
    div bx                          ; AX = the low quotient, DX = the digit
    mov [cal_dig+si], dl
    pop dx
    pop bx
    inc si
    mov cx, ax
    or cx, dx
    jnz .dig
.pad:
    mov al, [bx+CV_D]               ; at least one digit in FRONT of the point
    xor ah, ah
    cmp si, ax
    ja .sign
    mov byte [cal_dig+si], 0
    inc si
    jmp short .pad

.sign:
    xor cx, cx                      ; CX = the characters written
    cmp byte [bx+CV_SGN], 0
    je .int
    mov byte [di], '-'
    inc di
    inc cx
.int:
    mov al, [bx+CV_D]
    xor ah, ah
    mov dx, ax                      ; DX = the decimals owed
.iloop:
    dec si
    mov al, [cal_dig+si]
    add al, '0'
    mov [di], al
    inc di
    inc cx
    cmp si, dx
    ja .iloop
    or dx, dx
    jz .trail
    mov byte [di], '.'
    inc di
    inc cx
.floop:
    dec si
    mov al, [cal_dig+si]
    add al, '0'
    mov [di], al
    inc di
    inc cx
    or si, si
    jnz .floop
    jmp short .end
.trail:
    cmp byte [cal_fdot], 0
    je .end
    mov byte [di], '.'
    inc di
    inc cx
.end:
    pop di
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
        OS88_MENU cal_m_view, cal_i_view, 2
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
; A NUL-terminated array of line pointers, measured rather than pinned
; (cal_abmeas): the widest line sets the width and the count sets the height.
; Keep every line inside 25 glyphs - the content is CAL_CW = 224px on every
; adapter, which is 28 cells less the card's own 12px margins.
CAL_ABLH equ 11                     ; line pitch, px (8px glyphs + 3 of air)
cal_ablines:
    dw cal_ab1, cal_ab2, cal_ab3, cal_ab4, 0
cal_ab1:      db 'Calc for os8088', 0
cal_ab2:      db 'A desk calculator', 0
cal_ab3:      db 0                  ; a blank line is a line with no glyphs
cal_ab4:      db 'Contributed by Elendilon', 0

cal_s_hist:   db 'History', 0
cal_s_error:  db 'Error', 0
cal_s_copied: db 'Copied', 0
cal_s_sqrt:   db 'sqrt(', 0
cal_s_rparen: db ')', 0
cal_s_recip:  db '1/', 0
cal_s_empty:  db 0
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

; The key dispatch (cal_dispatch), indexed by key code 0..CK_HIST. The ten
; digit slots all reach one body, which reads the code back out of
; [cal_kcode] - digits ARE their codes, so the body needs no argument of its
; own and the table needs no second column.
cal_jtab:
    dw cal_kdigit, cal_kdigit, cal_kdigit, cal_kdigit, cal_kdigit
    dw cal_kdigit, cal_kdigit, cal_kdigit, cal_kdigit, cal_kdigit
    dw cal_kdot                     ; 10 CK_DOT
    dw cal_kop, cal_kop, cal_kop, cal_kop   ; 11..14 CK_ADD..CK_DIV
    dw cal_keq                      ; 15 CK_EQ
    dw cal_knop                     ; 16 CK_CLR  (taken above the table)
    dw cal_knop                     ; 17 CK_CE   (likewise)
    dw cal_kbs                      ; 18 CK_BS
    dw cal_kneg                     ; 19 CK_NEG
    dw cal_ksqrt                    ; 20 CK_SQRT
    dw cal_kpct                     ; 21 CK_PCT
    dw cal_krecip                   ; 22 CK_RECIP
    dw cal_knop                     ; 23 CK_HIST (the callers take this one)

; The keypad: a label and a key code per button, row-major, and it is the
; table cal_layout walks - so the cap, the rect and the action are one
; description and cannot drift.
cal_keytab:
    dw cal_k_c,   CK_CLR
    dw cal_k_ce,  CK_CE
    dw cal_k_bs,  CK_BS
    dw cal_k_div, CK_DIV
    dw cal_k_7,   7
    dw cal_k_8,   8
    dw cal_k_9,   9
    dw cal_k_mul, CK_MUL
    dw cal_k_4,   4
    dw cal_k_5,   5
    dw cal_k_6,   6
    dw cal_k_sub, CK_SUB
    dw cal_k_1,   1
    dw cal_k_2,   2
    dw cal_k_3,   3
    dw cal_k_add, CK_ADD
    dw cal_k_neg, CK_NEG
    dw cal_k_0,   0
    dw cal_k_dot, CK_DOT
    dw cal_k_eq,  CK_EQ

cal_k_c:   db 'C', 0
cal_k_ce:  db 'CE', 0
cal_k_bs:  db '<-', 0
cal_k_div: db '/', 0
cal_k_mul: db '*', 0
cal_k_sub: db '-', 0
cal_k_add: db '+', 0
cal_k_eq:  db '=', 0
cal_k_neg: db '+/-', 0
cal_k_dot: db '.', 0
cal_k_0:   db '0', 0
cal_k_1:   db '1', 0
cal_k_2:   db '2', 0
cal_k_3:   db '3', 0
cal_k_4:   db '4', 0
cal_k_5:   db '5', 0
cal_k_6:   db '6', 0
cal_k_7:   db '7', 0
cal_k_8:   db '8', 0
cal_k_9:   db '9', 0

; ASCII to key code, ended by a 0 byte. The digits are not here: they ARE
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
    db 0                            ; with 'c' or 'v', which are bytes of their
                                    ; own: a control key is not its letter


; The disclosure triangle, four bands each, {x1,y1,x2,y2} relative to
; (CAL_TRI_X, CAL_HDR_TY). Two pictures rather than one rotated, because a
; 7x7 triangle at this size is drawn rather than computed.
cal_tri_shut:                       ; folded up: pointing right
    db 0, 0, 0, 6
    db 1, 1, 1, 5
    db 2, 2, 2, 4
    db 3, 3, 3, 3
cal_tri_open:                       ; folded down: pointing down
    db 0, 1, 6, 1
    db 1, 2, 5, 2
    db 2, 3, 4, 3
    db 3, 4, 3, 4

; --- constants, as values ------------------------------------------------------
cal_zero:    dd 0
             db 0, 0, 0, 0
cal_one:     dd 1
             db 0, 0, 0, 0
cal_hundred: dd 100
             db 0, 0, 0, 0
cal_e8:      dd 100000000           ; cal_div's precision-target multiplier

%include "os88ui.inc"

    OS88_BSS CAL_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero is a valid starting state: a zero entry, no pending operator, no
; history and the pane folded up. [cal_new] being 0 rather than 1 is right
; too - the display shows a plain 0 and the first digit extends it, which is
; what typing into a fresh calculator does.
cal_win     equ os88_image_end + 0     ; word: our window
cal_ox      equ os88_image_end + 2     ; word: content left  (per callback)
cal_oy      equ os88_image_end + 4     ; word: content top
cal_ch      equ os88_image_end + 6     ; word: content height
cal_nvis    equ os88_image_end + 8     ; word: history rows that FIT
cal_a       equ os88_image_end + 10    ; word: cal_calc's left operand ptr
cal_b       equ os88_image_end + 12    ; word: ...and its right
cal_linep   equ os88_image_end + 14    ; word: where the answer joins cal_line
cal_penx    equ os88_image_end + 16    ; word: cal_emit's field pen
cal_efirst  equ os88_image_end + 18    ; word: ...its first changed cell
cal_eterm   equ os88_image_end + 20    ; word: ...and one past its last

cal_open    equ os88_image_end + 22    ; byte: the history pane is folded down
cal_err     equ os88_image_end + 23    ; byte: the refused state
cal_op      equ os88_image_end + 24    ; byte: CAL_OP_NONE or CK_ADD..CK_DIV
cal_new     equ os88_image_end + 25    ; byte: the display holds a RESULT
cal_dot     equ os88_image_end + 26    ; byte: the entry is past its point
cal_hn      equ os88_image_end + 27    ; byte: history entries kept, 0..8
cal_hhead   equ os88_image_end + 28    ; byte: the newest one's ring slot
cal_wd      equ os88_image_end + 29    ; byte: cal_pack's decimal count
cal_wsgn    equ os88_image_end + 30    ; byte: ...and its sign
cal_we      equ os88_image_end + 31    ; byte: cal_do_sqrt's even exponent
cal_subf    equ os88_image_end + 32    ; byte: cal_addsub's subtract flag
cal_fdot    equ os88_image_end + 33    ; byte: cal_fmtv's trailing-point flag
cal_pneg    equ os88_image_end + 34    ; byte: a pasted leading '-'
cal_kcode   equ os88_image_end + 35    ; byte: the key cal_dispatch is running
cal_ebank   equ os88_image_end + 36    ; byte: cal_emit's banked terminator
cal_wait    equ os88_image_end + 37    ; byte: an operator was the last key, so
                                       ; the number field is blank and waiting
                                       ; (38..39 spare, keeping the values
                                       ;  below on an even boundary)

cal_acc     equ os88_image_end + 40    ; the accumulator
cal_cur     equ os88_image_end + 48    ; ...and what is on show
cal_res     equ os88_image_end + 56    ; cal_calc's answer

cal_q       equ os88_image_end + 64    ; 64-bit scratch, four words each
cal_q2      equ os88_image_end + 72
cal_q3      equ os88_image_end + 80
cal_dv      equ os88_image_end + 88    ; dword: cq_divmod's divisor
cal_rem     equ os88_image_end + 92    ; dword: ...and its remainder
cal_x       equ os88_image_end + 96    ; dword: cal_isqrt's running root

cal_cbuf    equ os88_image_end + 104   ; the context field, 13 cells + NUL
cal_cshad   equ os88_image_end + 120   ; ...and what is on the GLASS
cal_nbuf    equ os88_image_end + 136   ; the number field, likewise
cal_nshad   equ os88_image_end + 152

cal_dig     equ os88_image_end + 168   ; 12: cal_fmtv's digits, least first
cal_scratch equ os88_image_end + 184   ; 48: formatting and clipboard staging
cal_line    equ os88_image_end + 232   ; 64: a history line before it is padded

cal_htext   equ os88_image_end + 296   ; 8 x 27: the rows, padded to the field
cal_hval    equ os88_image_end + 512   ; 8 x 8: the answers they load back

cal_rects   equ os88_image_end + 576   ; 29 x {x1,y1,x2,y2}, screen coords
cal_pbuf    equ os88_image_end + 808   ; 25: a paste, and NOT cal_scratch -
                                       ; every keystroke it feeds cal_key
                                       ; recomposes the display through that
                                       ; one, over the bytes still unread
                                       ; 808 + 25 = 833

cal_abw     equ os88_image_end + 834   ; word: the About card's measured width,
cal_abh     equ os88_image_end + 836   ; word: ...height,
cal_abl     equ os88_image_end + 838   ; word: ...and where it sits in the
cal_abt     equ os88_image_end + 840   ; word:    content (SPEC.md 12.2/65)
cal_abon    equ os88_image_end + 842   ; byte: 1 = the credits are up
                                       ; 842 + 1 = 843, rounded up to
                                       ; CAL_BSS_TOTAL = 1136
