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

; dc_wkday - ` Su Mo Tu We Th Fr Sa` over the day fields
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
; first cell. Keeps the rest
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
    mov ah, 3
    mul ah
    add al, DC_FLD0
    mov cl, al                  ; the field's first cell
    mov di, ax
    add di, dc_line
    mov al, [dc_grid + bx]
    mov byte [di], ' '
    or al, al
    jz .blank
    call dc_marked
    jz .nm
    mov byte [di], DC_MARK
.nm:
    inc di
    call dc_put2
    jmp short .x
.blank:
    mov word [di + 1], '  '
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
    mov bl, al
    mov al, 3
    mul bl
    add al, DC_FLD0             ; today's first cell
    mov bl, al
    mov cl, 0
    mov ch, bl
    mov ax, DC_INK
    call dc_part
    mov cl, bl
    mov ch, bl
    add ch, 3
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

; dc_day - the selected day's heading, ` Tue 13 October 2026`
dc_day:
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

; dc_frame - a frame round field AL (a grid index) in colour AH: the field's
; own outer columns and the gap rows above and below it (SPEC.md 107.3), so
; it never touches a glyph pixel. Every register kept
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
    mov bl, ah
    mov al, 3
    mul bl
    add al, DC_FLD0
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [dc_ox]             ; AX = the field's left
    mov cx, ax
    add cx, 3 * 8 - 1
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

; dc_field - re-run field AL alone, inverse if it is today
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
    mov ch, cl
    add ch, 3
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
    mov bx, [di + DCL_RULE]
    inc bx
    mov dx, [di + DCL_HEAD]
    dec dx
    call dc_fill
    call dc_day
    mov bx, [di + DCL_HEAD]     ; the pane: blank until wave 3 fills it
    add bx, 8
    mov dx, [dc_ch]
    dec dx
    call dc_fill
    cmp word [dc_cw], DC_CW
    jbe .w
    mov al, CWHITE              ; a content box wider than the 26 cells -
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
    call dc_arm
    jc .x
    cmp byte [dc_redo], 0
    jne dc_redraw
    call dc_head
    call dc_weeks
    call dc_selon
    call dc_day
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
    call dc_arm
    jc .x
    cmp byte [dc_redo], 0
    jne dc_redraw               ; the About card was down by a menu pick
    push ax
    mov ah, CWHITE
    call dc_frame
    mov ah, al
    call dc_tidx
    cmp al, ah
    jne .nt
    call dc_field
.nt:
    pop ax
    call dc_selon
    call dc_day
.x:
    ret

; --- the two layouts (SPEC.md 107.3): the y of each row, content-relative ----
DCL_HDR     equ 0
DCL_WK      equ 2
DCL_W0      equ 4
DCL_PITCH   equ 6
DCL_RULE    equ 8
DCL_HEAD    equ 10
DCL_PANE    equ 12

dc_ltall:   dw 0, 12, 24, 10, 88, 92, 104
dc_lcomp:   dw 0, 9, 18, 9, 72, 75, 84

dc_s_wk:    db '   Su Mo Tu We Th Fr Sa   ', 0
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
