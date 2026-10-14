; =============================================================================
; apps/deskcal/dcdraw.inc - the Desk Calendar's two layouts and every draw
; (SPEC.md 107.3)
;
; THE RULES, all three of them PERFORMANCE.md's:
;   * text is ONE OPAQUE RUN a row (OSAPI_FONT_RUN, SPEC.md 6.1): ground and
;     glyph in one pass, never a fill with letters dropped on it;
;   * every pixel of the content is written once in a full paint - a run per
;     text row, a fill per GAP between rows, one hline for the rule - because
;     the window is OWNBG and nothing else lays its ground (SPEC.md 11.90.1);
;   * a change redraws what changed: a new selection is two frames and the
;     heading, a new month is the header, the weeks and the heading.
;
; Every draw OUTSIDE W_PAINT goes through dc_arm first: nothing has armed a
; clip region for a key, a click, a menu pick or a tick (SPEC.md 11.3), and
; arming it is also what drops the raise cache, which is the half of
; OSAPI_WM_SAVEU's promise a covered window keeps by not drawing (11.96).
; =============================================================================

DC_INK      equ 0x0F00          ; AL = ink 0, AH = ground 15: the face
DC_INV      equ 0x000F          ; ...and today, inverse

; -----------------------------------------------------------------------------
; dc_layout - choose TALL or COMPACT from the content height in DX, and keep
; CX/DX as the box. Every register kept
; -----------------------------------------------------------------------------
dc_layout:
    mov [dc_cw], cx
    mov [dc_ch], dx
    mov word [dc_lay], dc_ltall
    cmp dx, DC_TALL_CH
    jae .x
    mov word [dc_lay], dc_lcomp
.x:
    ret

; dc_org - the content origin now, into [dc_ox]/[dc_oy]. Every register kept
dc_org:
    push ax
    push bx
    push dx
    mov bx, [dc_win]
    call OSAPI_WM_CONTENT
    mov [dc_ox], ax
    mov [dc_oy], dx
    pop dx
    pop bx
    pop ax
    ret

; dc_arm - the clip for a draw outside W_PAINT (lock held): the origin, and
; CF = 1 when not one pixel of the window is visible - draw nothing then; the
; next paint draws the state as it is. Every register kept
dc_arm:
    push bx
    mov bx, [dc_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x
    call dc_org
.x:
    ret

; dc_fill - white over content rows BX..DX (relative, inclusive), the whole
; width; nothing when BX > DX. Every register kept
dc_fill:
    cmp bx, dx
    jg .x
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [dc_ox]
    mov cx, ax
    add cx, [dc_cw]
    dec cx
    add bx, [dc_oy]
    add dx, [dc_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
.x:
    ret

; dc_run - one opaque run: SI = the string, CL = its first cell, DX = the
; content row, AX = the colours (DC_INK / DC_INV). Every register kept
dc_run:
    push cx
    push dx
    push ax
    mov al, cl
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [dc_ox]
    mov cx, ax
    add dx, [dc_oy]
    pop ax
    push ax
    call OSAPI_FONT_RUN
    pop ax
    pop dx
    pop cx
    ret

; dc_part - the cells [CL, CH) of dc_line at row DX in colours AX
dc_part:
    push bx
    push si
    mov bl, ch
    xor bh, bh
    push word [dc_line + bx]    ; NUL the run's end for the call, then back
    mov byte [dc_line + bx], 0
    mov bl, cl
    lea si, [dc_line + bx]
    call dc_run
    mov bl, ch
    pop word [dc_line + bx]
    pop si
    pop bx
    ret

; dc_blank - dc_line := 26 spaces and its NUL; DI = dc_line. Keeps the rest
dc_blank:
    push ax
    push cx
    push es
    push ds
    pop es
    cld
    mov di, dc_line
    mov cx, DC_CELLS
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov di, dc_line
    pop es
    pop cx
    pop ax
    ret

; dc_put2 - AL (0..99) as two characters at DI, a leading space below 10;
; dc_put2z - the same with a leading zero. DI advances by 2, the rest kept
dc_put2:
    push ax
    call dc_dec2
    cmp al, '0'
    jne .t
    mov al, ' '
.t:
    mov [di], ax
    add di, 2
    pop ax
    ret

dc_put2z:
    push ax
    call dc_dec2
    mov [di], ax
    add di, 2
    pop ax
    ret

; dc_dec2 - AL (0..99) to AL = the tens digit, AH = the ones digit (ASCII)
dc_dec2:
    push bx
    xor ah, ah
    mov bl, 10
    div bl
    add ax, '00'
    pop bx
    ret

; dc_puts - the NUL string SI at DI, without its NUL; DI advances
dc_puts:
    push ax
    push si
.l:
    lodsb
    or al, al
    jz .x
    mov [di], al
    inc di
    jmp short .l
.x:
    pop si
    pop ax
    ret

; dc_year - [dc_y] as four digits at DI; DI advances by 4, the rest kept
dc_year:
    push ax
    push cx
    push dx
    mov ax, [dc_y]
    xor dx, dx
    mov cx, 100
    div cx                      ; AX = the century, DX = the rest
    call dc_put2z
    mov ax, dx
    call dc_put2z
    pop dx
    pop cx
    pop ax
    ret

; dc_slen - AX = the length of the NUL string SI
dc_slen:
    push si
    xor ax, ax
.l:
    cmp byte [si], 0
    je .x
    inc si
    inc ax
    jmp short .l
.x:
    pop si
    ret

; -----------------------------------------------------------------------------
; the rows. Each builds dc_line and draws it; DI = the layout throughout
; -----------------------------------------------------------------------------

; dc_head - `<   October 2026   >`, inverse, the arrows spaces at either end of
; the range (SPEC.md 107.3)
dc_head:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dc_blank
    call dc_atfirst
    je .nl
    mov byte [dc_line], '<'
.nl:
    call dc_atlast
    je .nr
    mov byte [dc_line + DC_CELLS - 1], '>'
.nr:
    mov bl, [dc_m]
    xor bh, bh
    shl bx, 1
    mov si, [dc_mlong + bx - 2]
    call dc_slen
    add ax, 5                   ; ' yyyy'
    mov di, DC_CELLS
    sub di, ax
    shr di, 1
    add di, dc_line
    call dc_puts
    inc di
    call dc_year
    mov si, dc_line
    mov di, [dc_lay]
    mov dx, [di + DCL_HDR]
    xor cl, cl
    mov ax, DC_INV
    call dc_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_wkday - ` Su  Mo ... Sa` over the day fields' numbers
dc_wkday:
    push ax
    push cx
    push dx
    push si
    push di
    mov di, [dc_lay]
    mov dx, [di + DCL_WK]
    mov si, dc_s_wk
    xor cl, cl
    mov ax, DC_INK
    call dc_run
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; dc_fld - field BL (a grid index) into dc_line at its own cells, and CL = its
; first cell. The field is `[ ][d][d][m]`: the blank that parts it from the
; day before, the number, and the mark HARD AGAINST the number - ` 16*`, `  2*`
; - so a mark can only be read as its own day's (SPEC.md 107.3). Keeps the rest
dc_fld:
    push ax
    push bx
    push di
    xor bh, bh
    mov al, bl
    xor ah, ah
    mov ah, 7
    xchg ah, bh                 ; BH = 7, AH = 0
    div bh                      ; AL = the row, AH = the column
    xor bh, bh
    mov al, ah
    xor ah, ah
    shl al, 1
    shl al, 1                   ; DC_FLDW = 4 cells a field
    add al, DC_FLD0
    mov cl, al                  ; the field's first cell
    mov di, ax
    add di, dc_line + 1         ; ...and DI its number's: the blank is dc_blank's
    mov al, [dc_grid + bx]
    or al, al
    jz .x                       ; no day: dc_blank's four spaces
    call dc_put2
    call dc_marked
    jz .x
    mov byte [di], DC_MARK
.x:
    pop di
    pop bx
    pop ax
    ret

; dc_rowy - DX = the content row of week row AL (0..5). Keeps the rest
dc_rowy:
    push ax
    push di
    mov di, [dc_lay]
    xor ah, ah
    mul byte [di + DCL_PITCH]
    add ax, [di + DCL_W0]
    mov dx, ax
    pop di
    pop ax
    ret

; dc_week - week row AL: one run, or three when today is in it
dc_week:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dc_rowy
    call dc_blank
    mov bl, al
    mov bh, 7
    mov al, bl
    mul bh                      ; AL = the row's first grid index
    mov bl, al
    mov bh, 7
.f:
    push bx
    call dc_fld
    pop bx
    inc bl
    dec bh
    jnz .f
    sub bl, 7
    call dc_tidx                ; AL = today's index, or 0FFh
    sub al, bl
    cmp al, 7
    jae .one                    ; not in this row
    shl al, 1
    shl al, 1                   ; DC_FLDW
    add al, DC_FLD0 + 1         ; today's first INVERSE cell: its number - the
    mov bl, al                  ; blank before it stays ground, so the block
    mov cl, 0                   ; is the number and its mark, `13*`
    mov ch, bl
    mov ax, DC_INK
    call dc_part
    mov cl, bl
    mov ch, bl
    add ch, DC_FLDW - 1
    mov ax, DC_INV
    call dc_part
    mov cl, ch
    mov ch, DC_CELLS
    mov ax, DC_INK
    call dc_part
    jmp short .x
.one:
    mov si, dc_line
    xor cl, cl
    mov ax, DC_INK
    call dc_run
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_weeks - the gap above the first week, then each week and the gap below
; it, down to the rule. The gaps are rewritten too, which is what erases a
; selection frame's top and bottom on a month change
dc_weeks:
    push ax
    push bx
    push dx
    push di
    mov di, [dc_lay]
    mov bx, [di + DCL_WK]
    add bx, 8
    mov dx, [di + DCL_W0]
    dec dx
    call dc_fill
    xor al, al
.r:
    call dc_week
    push dx
    call dc_rowy
    mov bx, dx
    add bx, 8                   ; the gap: under the row...
    add dx, [di + DCL_PITCH]    ; ...to the next one's top
    cmp al, 5
    jb .g
    mov dx, [di + DCL_RULE]     ; ...or the last's, to the rule
.g:
    dec dx
    call dc_fill
    pop dx
    inc al
    cmp al, 6
    jb .r
    pop di
    pop dx
    pop bx
    pop ax
    ret

; dc_day - the heading under the rule as it is now: the selected day's, or on
; COMPACT with the tasks on show, ` Tasks`. dc_dayq - the day's, only when it
; is the one on show (a new day changes nothing else there)
dc_day:
    call dc_tsolo
    jc dc_dayh
    push dx
    push di
    mov di, [dc_lay]
    mov dx, [di + DCL_HEAD]
    call dc_tasksh
    pop di
    pop dx
    ret

dc_dayq:
    call dc_tsolo
    jc dc_dayh
    ret

; dc_dayh - the selected day's heading, ` Tue 13 October 2026`
dc_dayh:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dc_blank
    inc di
    call dc_sidx
    xor ah, ah
    mov bl, 7
    div bl                      ; AH = the column: the day of the week
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov si, [dc_dshort + bx]
    call dc_puts
    inc di
    mov al, [dc_sel]
    call dc_put2
    cmp byte [di - 2], ' '
    jne .two
    dec di                      ; one digit: no gap of two
    mov al, [di]
    mov [di - 1], al
    mov byte [di], ' '
.two:
    inc di
    mov bl, [dc_m]
    xor bh, bh
    shl bx, 1
    mov si, [dc_mlong + bx - 2]
    call dc_puts
    inc di
    call dc_year
    mov si, dc_line
    mov di, [dc_lay]
    mov dx, [di + DCL_HEAD]
    xor cl, cl
    mov ax, DC_INK
    call dc_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_frame - a frame round field AL (a grid index) in colour AH: column 1 of
; the field's leading blank, column 7 of its mark cell, and the gap rows
; above and below it (SPEC.md 107.3). Column 1 and not 0 keeps it a pixel off
; the day before's mark - and off the window's own frame on a Sunday. Every
; register kept
dc_frame:
    push ax
    push bx
    push cx
    push dx
    push di
    push ax
    mov al, ah
    call OSAPI_SET_COLOR
    pop ax
    xor ah, ah
    mov bl, 7
    div bl                      ; AL = the row, AH = the column
    call dc_rowy                ; DX = the row's top
    mov al, ah
    xor ah, ah
    add al, al
    add al, al                  ; DC_FLDW
    add al, DC_FLD0
    mov cl, 3
    shl ax, cl
    inc ax
    add ax, [dc_ox]             ; AX = the field's left, plus one
    mov cx, ax
    add cx, DC_FLDW * 8 - 2
    add dx, [dc_oy]
    mov bx, dx
    dec bx
    add dx, 8
    call OSAPI_GFX_FRAME
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_selon - the selection's frame, black
dc_selon:
    push ax
    call dc_sidx
    mov ah, CBLACK
    call dc_frame
    pop ax
    ret

; dc_field - re-run field AL's number and mark alone (the frame never cuts its
; leading blank), inverse if it is today
dc_field:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    xor ah, ah
    mov bh, 7
    div bh
    call dc_rowy                ; DX = its row
    call dc_blank
    call dc_fld                 ; CL = its first cell
    inc cx                      ; ...past its blank
    mov ch, cl
    add ch, DC_FLDW - 1
    call dc_tidx
    cmp al, bl
    mov ax, DC_INK
    jne .p
    mov ax, DC_INV
.p:
    call dc_part
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_all - the whole content, every pixel once (W_PAINT, or after dc_arm)
; -----------------------------------------------------------------------------
dc_all:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, [dc_lay]
    call dc_head
    mov bx, [di + DCL_HDR]
    add bx, 8
    mov dx, [di + DCL_WK]
    dec dx
    call dc_fill
    call dc_wkday
    call dc_weeks
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [dc_ox]
    mov bx, ax
    add bx, [dc_cw]
    dec bx
    mov dx, [di + DCL_RULE]
    add dx, [dc_oy]
    call OSAPI_GFX_HLINE
    call dc_panes               ; the heading under the rule, and the lists
    cmp word [dc_cw], DC_CW
    jbe .w
    mov al, CWHITE              ; a content box wider than the 29 cells -
    call OSAPI_SET_COLOR        ; never asked for, but the ground is ours
    mov ax, [dc_ox]             ; (OWNBG)
    add ax, DC_CW
    mov bx, [dc_oy]
    mov cx, [dc_ox]
    add cx, [dc_cw]
    dec cx
    mov dx, [dc_oy]
    add dx, [dc_ch]
    dec dx
    call OSAPI_GFX_FILL
.w:
    call dc_selon
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_regrid - a new month: header, weeks, frame, heading (lock held) - or
; the whole content when a menu pick has just taken the About card down
; (dc_redo), whose ground the four rows would not cover
dc_regrid:
    call dc_build
    call dc_newday
    call dc_arm
    jc .x
    cmp byte [dc_redo], 0
    jne dc_redraw
    call dc_head
    call dc_weeks
    call dc_selon
    call dc_dayq
    call dc_agnew
.x:
    ret

; dc_redraw - dc_redo's whole paint, once (the clip armed)
dc_redraw:
    mov byte [dc_redo], 0
    jmp dc_all

; dc_reselect - the selection moved from grid index AL, inside one month:
; the old frame off, the old field re-run when it was today (the white frame
; cut its inverse ground), the new frame on, the heading (lock held)
dc_reselect:
    call dc_newday
    call dc_arm
    jc .x
    cmp byte [dc_redo], 0
    jne dc_redraw               ; the About card was down by a menu pick
    push ax
    push bx
    mov ah, CWHITE
    call dc_frame
    mov ah, al                  ; AH = the old field
    call dc_tidx
    cmp al, ah
    je .rr                      ; today: the white frame cut its ground
    mov bl, ah
    xor bh, bh
    mov al, [dc_grid + bx]
    call dc_marked              ; marked: the frame cut its star's middle
    jz .nt
.rr:
    mov al, ah
    call dc_field
.nt:
    pop bx
    pop ax
    call dc_selon
    call dc_dayq
    call dc_agnew
.x:
    ret

; =============================================================================
; THE LISTS (SPEC.md 107.3): the day's agenda, the tasks, and the entry field
; that takes a heading's place. Every line is one 29-cell run; a task row is
; the check box's glyph and a 27-cell run from cell 2 (8-aligned, so font_run
; takes its one-pass path - os88ui_chk's own label would sit 18 px in)
; =============================================================================

; dc_tsolo - CF = 0: COMPACT with the TASKS on show, so the row under the
; rule is the task list's heading. CF = 1 otherwise. Every register kept
dc_tsolo:
    push di
    mov di, [dc_lay]
    cmp word [di + DCL_SPLIT], 0
    jne .no
    cmp byte [dc_pane], 0
    je .no
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

; dc_aon / dc_ton - CF = 0 the agenda / the tasks are on show. Keep all
dc_aon:
    push di
    mov di, [dc_lay]
    cmp word [di + DCL_SPLIT], 0
    jne .yes
    cmp byte [dc_pane], 0
    jne .no
.yes:
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

dc_ton:
    push di
    mov di, [dc_lay]
    cmp word [di + DCL_SPLIT], 0
    jne .yes
    cmp byte [dc_pane], 0
    je .no
.yes:
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

; dc_apage / dc_tpage - BL = items a page, BH = 1 when there is more than
; one page (and the last line is the pager). Every other register kept
dc_apage:
    push di
    mov di, [dc_lay]
    mov bl, [di + DCL_AN]
    xor bh, bh
    cmp [dc_acnt], bl
    jbe .x
    dec bl
    inc bh
.x:
    pop di
    ret

dc_tpage:
    push di
    mov di, [dc_lay]
    mov bl, [di + DCL_TN]
    xor bh, bh
    cmp [DIA_PAY + DCP_NTASK], bl
    jbe .x
    dec bl
    inc bh
.x:
    pop di
    ret

; dc_ashown / dc_tshown - CL = the lines (rows) that are not blank now: the
; items from the top, or - when the list pages - every row to the PAGER, which
; is the last row whatever a short last page holds (SPEC.md 107.3)
dc_ashown:
    push ax
    push bx
    call dc_apage
    mov al, [dc_acnt]
    sub al, [dc_atop]
    jmp short dc_shown

dc_tshown:
    push ax
    push bx
    call dc_tpage
    mov al, [DIA_PAY + DCP_NTASK]
    sub al, [dc_ttop]
dc_shown:                       ; AL = items from the top, BL/BH the page
    cmp al, bl
    jbe .k
    mov al, bl
.k:
    or bh, bh
    jz .n
    mov al, bl                  ; paging: the rows above the pager, all of them
.n:
    add al, bh
    mov cl, al
    pop bx
    pop ax
    ret

; dc_putn - AL (0..99) at DI, one digit or two; DI advances. Keeps the rest
dc_putn:
    cmp al, 10
    jae .two
    push ax
    add al, '0'
    mov [di], al
    inc di
    pop ax
    ret
.two:
    jmp dc_put2z

; dc_pager - `  more...   page p of q` at dc_line + cell AL: CL = the first
; item on show, BL = items a page, CH = items. Keeps every register
dc_pager:
    push ax
    push cx
    push si
    push di
    xor ah, ah
    mov di, dc_line
    add di, ax
    mov si, dc_s_more
    call dc_puts
    mov al, cl
    xor ah, ah
    div bl
    inc ax                      ; this page, from 1
    call dc_putn
    mov si, dc_s_of
    call dc_puts
    mov al, ch
    add al, bl
    xor ah, ah
    dec ax
    div bl                      ; the pages
    call dc_putn
    pop di
    pop si
    pop cx
    pop ax
    ret

; dc_tfmt - the minute of the day AX as `HH:MM` at DI; DI advances by 5
dc_tfmt:
    push ax
    push bx
    mov bl, 60
    div bl                      ; AL = the hour, AH = the minute
    mov bl, ah
    call dc_put2z
    mov byte [di], ':'
    inc di
    mov al, bl
    call dc_put2z
    pop bx
    pop ax
    ret

; dc_arowy - DX = agenda line AL's row, content-relative. Keeps the rest
dc_arowy:
    push ax
    push di
    mov di, [dc_lay]
    mul byte [di + DCL_APITCH]
    add ax, [di + DCL_PANE]
    mov dx, ax
    pop di
    pop ax
    ret

; dc_aend - BX = the row under the agenda's LAST run: where the ground a full
; paint lays under the list begins. Keeps the rest
dc_aend:
    push ax
    push dx
    push di
    mov di, [dc_lay]
    mov al, [di + DCL_AN]
    dec ax
    call dc_arowy
    mov bx, dx
    add bx, 8
    pop di
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_aline - agenda line AL (0..AN-1) as it is now: an appointment, the
; pager, or blank; inverse when it is the selected appointment. One run.
; Every register kept
; -----------------------------------------------------------------------------
dc_aline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al                  ; AH = the line
    call dc_arowy               ; DX = its row
    push dx
    call dc_blank
    mov dx, DC_INK              ; DX = the colours, until the run
    call dc_apage
    cmp ah, bl
    jb .item
    or bh, bh
    jz .run                     ; past the page and no pager: blank
    mov cl, [dc_atop]
    mov ch, [dc_acnt]
    mov al, 2
    call dc_pager
    jmp short .run
.item:
    mov al, ah
    add al, [dc_atop]
    cmp al, [dc_acnt]
    jae .run
    add al, [dc_afirst]         ; AL = its index in the table
    cmp byte [dc_selk], DCS_APPT
    jne .f
    cmp al, [dc_seli]
    jne .f
    mov dx, DC_INV
.f:
    call dc_afmt
.run:
    mov ax, dx
    pop dx
    mov si, dc_line
    xor cl, cl
    call dc_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_afmt - appointment AL into dc_line: ` HH:MM! text`, `--:--` all day, the
; mark `!` armed, `-` fired; 18 cells of its text. Keeps every register
dc_afmt:
    push ax
    push cx
    push si
    push di
    call dc_aptr
    mov di, dc_line + 1
    mov ax, [si + DCA_TIME]
    cmp ax, DC_ALLDAY
    jne .t
    mov word [di], '--'
    mov byte [di + 2], ':'
    mov word [di + 3], '--'
    jmp short .tx
.t:
    cmp ax, 24 * 60
    jae .tx                     ; not a time: nothing in the column
    call dc_tfmt
    mov al, ' '
    test byte [si + DCA_FLAGS], DCA_REMIND
    jz .mk
    mov al, '!'
    test byte [si + DCA_FLAGS], DCA_FIRED
    jz .mk
    mov al, '-'
.mk:
    mov [dc_line + DC_AMARK], al
.tx:
    add si, DCA_TEXT
    mov di, dc_line + 8
    mov cx, DC_CELLS - 8
    cld
.c:
    lodsb
    or al, al
    jz .x
    mov [di], al
    inc di
    loop .c
.x:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_agenda - agenda lines from AL down, when the agenda is on show (the clip
; armed): AH = 0 only as far as a line that is or was occupied - a day with
; nothing on it, after another with nothing, costs nothing - and AH = 1 every
; line (a full paint). [dc_ashow] follows. Every register kept
; -----------------------------------------------------------------------------
dc_agenda:
    call dc_aon
    jc .r
    push ax
    push cx
    push di
    mov di, [dc_lay]
    mov ch, [di + DCL_AN]
    call dc_ashown              ; CL = occupied now
    or ah, ah
    jnz .set
    mov ch, cl
    cmp ch, [dc_ashow]
    jae .set
    mov ch, [dc_ashow]
.set:
    mov [dc_ashow], cl
.l:
    cmp al, ch
    jae .x
    call dc_aline
    or ah, ah
    jz .n                       ; a line's own redraw: its gap is ground already
    inc ax
    cmp al, ch
    je .x                       ; the last's gap is the caller's (dc_aend)
    dec ax
    push bx
    push dx
    call dc_arowy
    mov bx, dx
    add bx, 8                   ; THE GAP under the line, every pixel once
    add dl, [di + DCL_APITCH]   ; (the run lays the line's own 8 rows)
    adc dh, 0
    dec dx                      ; ...to the next line's top
    call dc_fill
    pop dx
    pop bx
.n:
    inc ax
    jmp short .l
.x:
    pop di
    pop cx
    pop ax
.r:
    ret

; dc_agnew - the agenda from its first line, the occupied ones (lock, clip)
dc_agnew:
    push ax
    xor ax, ax
    call dc_agenda
    pop ax
    ret

; dc_newday - the selected day changed: its appointments, from their first
; page, and no appointment selected (it was the other day's). State only
dc_newday:
    cmp byte [dc_selk], DCS_APPT
    jne .k
    mov byte [dc_selk], 0
.k:
    mov byte [dc_atop], 0
    call dc_arange
    jmp dc_menulive

; -----------------------------------------------------------------------------
; the task rows. Row k: the box (os88ui's check glyph) at x 4..15, rows
; y..y+11 - INSET off the window's frame, and a pitch of 13 so stacked boxes
; never touch; the text a run of cells 2..28 at y + 2, its words from cell 3;
; the 4 columns left of the box, the row under each box and the strips
; between the runs belong to the full paint (dc_tpaint)
; -----------------------------------------------------------------------------

; dc_trowy - DX = task row AL's top, content-relative. Keeps the rest
dc_trowy:
    push ax
    push di
    mov di, [dc_lay]
    mov dl, DC_TPITCH
    mul dl
    add ax, [di + DCL_T0]
    mov dx, ax
    pop di
    pop ax
    ret

; dc_titem - CF = 0: task row AL shows a task, AL = its index; CF = 1 it is
; the pager or blank, and BH = 1 the pager. BL = tasks a page either way
dc_titem:
    call dc_tpage
    cmp al, bl
    jae .not
    add al, [dc_ttop]
    cmp al, [DIA_PAY + DCP_NTASK]
    jae .blank
    clc
    ret
.blank:
    xor bh, bh
.not:
    stc
    ret

; dc_ckaim - dc_ck at task row AL's box, its mark AH (0 / 1). Keeps all
dc_ckaim:
    push ax
    push dx
    mov [dc_ck + OS88UI_CK_ON], ah
    call dc_trowy
    add dx, [dc_oy]
    mov [dc_ck + 2], dx
    add dx, OS88UI_CKBOX - 1
    mov [dc_ck + 6], dx
    mov ax, [dc_ox]
    add ax, DC_TBOX
    mov [dc_ck + 0], ax
    add ax, OS88UI_CKBOX - 1
    mov [dc_ck + 4], ax
    pop dx
    pop ax
    ret

; dc_tmark - task row AL's MARK alone, AH = 1 checked: ONE fill, the 6x6
; square os88ui_glyph puts inside the frame (box + 3 .. box + 8), black or
; white (SPEC.md 107.3). Not os88ui_chkmark: that is the glyph whole and it
; clears the box first, so the frame - known to be on the glass here - would
; be written twice. The clip armed. Keeps every register
dc_tmark:
    push ax
    push bx
    push cx
    push dx
    call dc_trowy               ; DX = the row's top: the box's
    push ax
    mov al, CWHITE
    or ah, ah
    jz .c
    mov al, CBLACK
.c:
    call OSAPI_SET_COLOR
    pop ax
    add dx, [dc_oy]
    add dx, 3
    mov bx, dx
    add dx, OS88UI_CKBOX - 7
    mov ax, [dc_ox]
    add ax, DC_TBOX + 3
    mov cx, ax
    add cx, OS88UI_CKBOX - 7
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_tleft - task row AL's box: the glyph with its mark, or white. Keep all
dc_tleft:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ah, al
    call dc_titem
    jc .white
    push si
    call dc_tptr
    mov bl, [si + DCT_FLAGS]
    pop si
    and bl, DCT_DONE
    mov al, ah
    mov ah, bl
    call dc_ckaim
    mov bx, dc_ck
    xor di, di
    call os88ui_chkdraw         ; the shared control's own box (13.15.2)
    jmp short .x
.white:
    mov al, ah
    call dc_trowy
    mov bx, dx
    add dx, OS88UI_CKBOX - 1    ; the box's own square: the row under it is
    mov ax, DC_TBOX             ; ground for good (dc_tpaint)
    mov cx, DC_TBOX + OS88UI_CKBOX - 1
    call dc_fillr
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_ttext - task row AL's text: one run of cells 2..28, inverse for the
; selected task, the pager's words, or blank. Keeps every register
dc_ttext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al
    call dc_trowy
    add dx, 2
    push dx
    call dc_blank
    mov dx, DC_INK
    mov al, ah
    call dc_titem
    jnc .item
    or bh, bh
    jz .run
    mov cl, [dc_ttop]
    mov ch, [DIA_PAY + DCP_NTASK]
    mov al, 4
    call dc_pager
    jmp short .run
.item:
    cmp byte [dc_selk], DCS_TASK
    jne .f
    cmp al, [dc_seli]
    jne .f
    mov dx, DC_INV
.f:
    call dc_tptr
    add si, DCT_TEXT
    mov di, dc_line + 3
    mov cx, DCT_TMAX
    cld
.c:
    lodsb
    or al, al
    jz .run
    mov [di], al
    inc di
    loop .c
.run:
    mov ax, dx
    pop dx
    mov cx, (DC_CELLS << 8) | 2
    call dc_part
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_trow - task row AL whole: the box and the text
dc_trow:
    call dc_tleft
    jmp dc_ttext

; dc_tasks - task rows from AL down when the tasks are on show (the clip
; armed), as far as a row that is or was occupied; [dc_tshow] follows
dc_tasks:
    call dc_ton
    jc .r
    push ax
    push cx
    call dc_tshown
    mov ch, cl
    cmp ch, [dc_tshow]
    jae .set
    mov ch, [dc_tshow]
.set:
    mov [dc_tshow], cl
.l:
    cmp al, ch
    jae .x
    call dc_trow
    inc ax
    jmp short .l
.x:
    pop cx
    pop ax
.r:
    ret

; dc_tpaint - the task list whole (a full paint, the clip armed): the inset
; left of the boxes, the strip above the first row, and each row with the
; strip under its text and the row under its box. Every pixel once
dc_tpaint:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, [dc_lay]
    mov bx, [di + DCL_T0]
    mov dx, [dc_ch]
    dec dx
    xor ax, ax
    mov cx, DC_TBOX - 1
    call dc_fillr               ; x 0..3, every row: the box's inset
    mov dx, bx
    inc dx
    mov ax, 16
    mov cx, DC_CW - 1
    call dc_fillr               ; above the first row's text
    xor al, al
.r:
    call dc_trow
    push ax
    call dc_trowy
    mov bx, dx
    add bx, OS88UI_CKBOX        ; the row(s) under the box...
    add dx, DC_TPITCH - 1       ; ...to the next box's top (y + 12)...
    cmp dx, [dc_ch]
    jb .b
    mov dx, [dc_ch]             ; ...or the content's bottom
    dec dx
.b:
    mov ax, DC_TBOX
    mov cx, 15
    call dc_fillr
    mov dx, bx
    sub bx, OS88UI_CKBOX - 10   ; the strip under the text, y + 10...
    add dx, DC_TPITCH + 2 - OS88UI_CKBOX - 1    ; ...to the next text's top
    cmp dx, [dc_ch]             ; (y + 14)...
    jb .s
    mov dx, [dc_ch]             ; ...or the content's bottom
    dec dx
.s:
    mov ax, 16
    mov cx, DC_CW - 1
    call dc_fillr
    pop ax
    inc al
    cmp al, [di + DCL_TN]
    jb .r
    call dc_tshown
    mov [dc_tshow], cl
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_tasksh - ` Tasks` at content row DX
dc_tasksh:
    push ax
    push cx
    push si
    push di
    call dc_blank
    inc di
    mov si, dc_s_tasks
    call dc_puts
    mov si, dc_line
    xor cl, cl
    mov ax, DC_INK
    call dc_run
    pop di
    pop si
    pop cx
    pop ax
    ret

; dc_ttaskh - the TALL task heading, at its own row
dc_ttaskh:
    push dx
    push di
    mov di, [dc_lay]
    mov dx, [di + DCL_THEAD]
    call dc_tasksh
    pop di
    pop dx
    ret

; dc_fillr - white over content columns AX..CX, rows BX..DX (relative,
; inclusive). Every register kept
dc_fillr:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    add ax, [dc_ox]
    add cx, [dc_ox]
    add bx, [dc_oy]
    add dx, [dc_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the entry field (os88line.inc) - in the place of a HEADING: the day's for an
; appointment, the task list's for a task (COMPACT: the one row, both)
; -----------------------------------------------------------------------------

; dc_fhead - AX = the content row of the heading the field takes. Keeps rest
dc_fhead:
    push di
    mov di, [dc_lay]
    mov ax, [di + DCL_HEAD]
    cmp byte [dc_ed], DCE_NEWT
    jb .x
    cmp word [di + DCL_SPLIT], 0
    je .x
    mov ax, [di + DCL_THEAD]
.x:
    pop di
    ret

; dc_frect - the field's rect on the glass now (the window may have moved):
; the content's width, from three rows above the heading to eight below
dc_frect:
    push ax
    call dc_fhead
    sub ax, 3
    add ax, [dc_oy]
    mov [dc_ln + LN_Y1], ax
    add ax, 11
    mov [dc_ln + LN_Y2], ax
    mov ax, [dc_ox]
    mov [dc_ln + LN_X1], ax
    add ax, DC_CW - 1
    mov [dc_ln + LN_X2], ax
    pop ax
    ret

; dc_fdraw - the field whole (the clip armed)
dc_fdraw:
    push si
    call dc_frect
    mov si, dc_ln
    call os88line_draw
    pop si
    ret

; dc_band - content rows BX..DX round the heading at row AX, drawn by the
; proc at SI: the gaps and the heading - or, when the field is in this
; heading's place, one fill and the field (SPEC.md 107.3: the one double
; write, a full paint's while the field is open). Every register kept
dc_band:
    push ax
    push bx
    push dx
    cmp byte [dc_ed], 0
    je .head
    push ax
    push dx
    mov dx, ax
    call dc_fhead
    cmp ax, dx
    pop dx
    pop ax
    jne .head
    call dc_fill
    call dc_fdraw
    jmp short .x
.head:
    push dx
    mov dx, ax
    dec dx
    call dc_fill                ; above it
    mov dx, ax
    call si                     ; the heading's run (DX = its row)
    mov bx, ax
    add bx, 8
    pop dx
    call dc_fill                ; below it
.x:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_panes - everything under the rule (a full paint, the clip armed): the
; heading's band, then TALL's agenda, task band and tasks, or COMPACT's one
; list. Every register kept
; -----------------------------------------------------------------------------
dc_panes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, [dc_lay]
    mov bx, [di + DCL_RULE]
    inc bx
    mov dx, [di + DCL_PANE]
    dec dx
    mov ax, [di + DCL_HEAD]
    mov si, dc_dayat
    call dc_band
    call dc_tsolo
    jnc .tasks
    mov ax, 0x0100
    call dc_agenda              ; every line, and the gaps between them
    call dc_aend                ; BX = the agenda's end
    cmp word [di + DCL_SPLIT], 0
    jne .split
    mov dx, [dc_ch]             ; COMPACT: white to the bottom
    dec dx
    call dc_fill
    jmp short .x
.split:
    mov dx, [di + DCL_T0]
    dec dx
    mov ax, [di + DCL_THEAD]
    mov si, dc_tasksh
    call dc_band
.tasks:
    call dc_tpaint
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_dayat - dc_band's proc for the row under the rule (DX = its row, which
; dc_day knows already)
dc_dayat:
    jmp dc_day

; --- the two layouts (SPEC.md 107.3): the y of each row, content-relative ----
DCL_HDR     equ 0
DCL_WK      equ 2
DCL_W0      equ 4
DCL_PITCH   equ 6
DCL_RULE    equ 8
DCL_HEAD    equ 10
DCL_PANE    equ 12              ; the agenda's first line
DCL_AN      equ 14              ; agenda lines (pitch DCL_APITCH)
DCL_THEAD   equ 16              ; the task heading's row
DCL_T0      equ 18              ; the first task row (pitch DC_TPITCH)
DCL_TN      equ 20              ; task rows
DCL_SPLIT   equ 22              ; 1: both lists on show; 0: one at a time
DCL_APITCH  equ 24              ; db the agenda's pitch: the 8-row run and a
                                ; GAP under it, so a descender never meets the
                                ; next line's caps - 10 on TALL, and 9 on
                                ; COMPACT, which keeps its four lines inside
                                ; the 123 rows its three task rows take

dc_ltall:   dw 0, 12, 24, 10, 88, 92, 104, 7, 176, 188, 5, 1
            db 10
dc_lcomp:   dw 0, 9, 18, 9, 72, 75, 84, 4, 75, 84, 3, 0
            db 9
%if 188 + 5 * DC_TPITCH != DC_TALL_CH || 104 + 7 * 10 + 2 != 176 || 84 + 3 * DC_TPITCH != DC_COMP_CH || 84 + 4 * 9 > DC_COMP_CH
  %error "deskcal: a layout's rows no longer fill its content box"
%endif

dc_s_wk:    db ' Su  Mo  Tu  We  Th  Fr  Sa  ', 0  ; over each field's number
%if ($ - dc_s_wk - 1) != DC_CELLS
  %error "deskcal: the weekday row is one run of DC_CELLS cells"
%endif
dc_s_more:  db 'more...  page ', 0
dc_s_of:    db ' of ', 0
dc_s_tasks: db 'Tasks', 0
dc_dshort:  dw dc_d0, dc_d1, dc_d2, dc_d3, dc_d4, dc_d5, dc_d6
dc_d0:      db 'Sun', 0
dc_d1:      db 'Mon', 0
dc_d2:      db 'Tue', 0
dc_d3:      db 'Wed', 0
dc_d4:      db 'Thu', 0
dc_d5:      db 'Fri', 0
dc_d6:      db 'Sat', 0
dc_mlong:   dw dc_m1, dc_m2, dc_m3, dc_m4, dc_m5, dc_m6
            dw dc_m7, dc_m8, dc_m9, dc_m10, dc_m11, dc_m12
dc_m1:      db 'January', 0
dc_m2:      db 'February', 0
dc_m3:      db 'March', 0
dc_m4:      db 'April', 0
dc_m5:      db 'May', 0
dc_m6:      db 'June', 0
dc_m7:      db 'July', 0
dc_m8:      db 'August', 0
dc_m9:      db 'September', 0
dc_m10:     db 'October', 0
dc_m11:     db 'November', 0
dc_m12:     db 'December', 0
