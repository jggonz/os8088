; =============================================================================
; apps/deskcal/dcdate.inc - the Desk Calendar's date maths (SPEC.md 110.3)
;
; All integer, all 8086, and exact over the range the payload can store:
; January 1980 to December 2099, the FAT date's own span. Nothing here draws
; and nothing here touches a file.
;
;   dc_dim_of    days in a month       clock.inc's clk_mlen's mask, one bit
;                                      down: 0x0AD5 holds the 31-day months,
;                                      February is 28 + (year AND 3 = 0) -
;                                      exact 1901-2099
;   dc_dow_of    day of the week       Sakamoto's, as apps/dos's dos_dow, cut
;                                      to the range so it fits a byte
;   dc_build     the 42-byte grid of the month on show, MONDAY-FIRST
;                (v2), and its marks
;   dc_today     OSAPI_CLK_GET's date, clamped into the range
; =============================================================================

DC_Y0       equ 1980            ; the first month navigation reaches...
DC_Y1       equ 2099            ; ...and the last year (SPEC.md 110.3)

; -----------------------------------------------------------------------------
; dc_dim_of - days in a month
; in:  AX = year, BL = month 1..12
; out: AL = 28..31; every other register kept (AH zero)
; -----------------------------------------------------------------------------
dc_dim_of:
    push cx
    mov cl, bl
    mov ch, al                  ; the year's low byte: AND 3 is all it needs
    mov ax, 0x0AD5              ; bit m-1 set: month m has 31 days...
    shr ax, cl                  ; ...and the shift leaves it in CF
    mov ax, 30
    adc al, 0
    cmp bl, 2
    jne .x
    mov al, 28
    test ch, 3
    jnz .x
    inc ax                      ; a leap year (every fourth, 1901-2099)
.x:
    pop cx
    ret

; -----------------------------------------------------------------------------
; dc_dow_of - the day of the week, 0 = Sunday (Sakamoto)
; in:  AX = year, BL = month 1..12, BH = day
; out: AL = 0..6, AH = 0; every other register kept
;
; Sakamoto's y + y/4 - y/100 + y/400 + t[m-1] + d, cut to the range: from
; March 1900 to 2099 the two century terms are -15 together, and counting
; the year from 1976 (a leap year, 2470 = 6 mod 7) folds the rest into
; t[] + 5 - so the whole sum fits a byte (at most 200 for 1980-2107) and
; one 8-bit DIV takes it mod 7. Exact 1980-2099; host-checked against the
; full formula for every day of it.
; -----------------------------------------------------------------------------
dc_dow_of:
    push bx
    cmp bl, 3
    sbb ax, DC_Y0 - 4           ; AL = y - 1976, Jan/Feb the year before's
    mov ah, al
    shr ah, 1
    shr ah, 1
    add al, ah                  ; + its quarter
    add al, bh                  ; + d
    mov bh, 0
    add al, [dc_sakt + bx - 1]  ; + t[m-1] + 5, mod 7
    mov ah, bh
    mov bl, 7
    div bl
    mov al, ah                  ; mod 7
    cbw
    pop bx
    ret

dc_sakt:    db 5, 1, 0, 3, 5, 1, 3, 6, 2, 4, 0, 2

; -----------------------------------------------------------------------------
; dc_build - the month on show, [dc_y]/[dc_m], into the grid and the marks
; out: [dc_grid] 42 bytes, byte i = the day in week row i/7, column i%7
;      counted from MONDAY, or 0; [dc_dow1] the 1st's column; [dc_dim] the
;      month's length; [dc_mark]
;      bit d set when the payload holds an appointment on day d. A payload
;      whose count is past the table marks nothing. Every register kept
; -----------------------------------------------------------------------------
dc_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [dc_y]
    mov bl, [dc_m]
    push ax
    call dc_dim_of
    mov [dc_dim], al
    pop ax
    mov bh, 7                   ; the 7th's day of the week (Sunday 0) is
    call dc_dow_of              ; the 1st's column counted from MONDAY
    mov [dc_dow1], al           ; (SPEC.md 110.3, v2; display only - the
    xchg ax, bx                 ; payload holds dates). BX = it
    push ds
    pop es
    cld
    mov di, dc_grid
    mov cx, DC_GRIDN
    xor ax, ax
    rep stosb                   ; every cell blank...
    lea di, [bx + dc_grid]      ; ...then the days, from the 1st's column
    mov cl, [dc_dim]
.d:
    inc ax
    stosb
    loop .d
    mov di, dc_mark             ; the marks: one bit a day
    xchg ax, cx                 ; AX = 0, CX = the month's length (CH = 0)
    stosw
    stosw
    call dc_aall
    cmp cl, DCP_MAXAPPT
    ja .done                    ; not ours to trust: mark nothing
    jcxz .done
    call dc_pack                ; the month on show: the packed date's bits
    and al, 0E0h                ; 5.. - a word compare against it
    xchg ax, dx
.a:
    mov ax, [si + DCA_DATE]
    xor ax, dx                  ; this month's: only the day is left
    cmp ax, 32
    jae .n
    call dc_mbit
    or [dc_mark + bx], al
.n:
    add si, DCP_APPTSZ
    loop .a
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
; dc_marked - does day AL of the month on show carry a mark? ZF = 0 yes.
; Every register kept
; -----------------------------------------------------------------------------
dc_marked:
    push ax
    push bx
    call dc_mbit
    test [dc_mark + bx], al
    pop bx
    pop ax
    ret

; dc_mbit - day AL (0..31): BX = its byte of [dc_mark], AL = its bit. AH
; clobbered; every other register kept
dc_mbit:
    push cx
    cbw
    mov cl, al
    and cl, 7
    xchg ax, bx
    mov al, 1
    shl al, cl
    mov cl, 3
    shr bx, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; dc_today - today, from OSAPI_CLK_GET, clamped into 1980-01-01..2099-12-31
; out: [dc_ty] [dc_tm] [dc_td]; CF = 1 it differs from what they held. Also
;      [dc_now] = the minute of the day and [dc_rtc] = the cell's flags, for
;      the reminders (dcrem.inc). Every register kept
; -----------------------------------------------------------------------------
dc_today:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_CLK_GET          ; CX = year, DH = month, DL = day, BH:BL =
    mov [dc_rtc], al            ; hour:minute, AL bit 0 = an RTC
    mov al, 60
    mul bh                      ; AX = the hour's minutes (DX is not mul r8's)
    xor bh, bh
    add ax, bx
    mov [dc_now], ax
    cmp cx, DC_Y0
    jb .bad                     ; before the range: its first day
    cmp cx, DC_Y1
    jbe .hi
    mov cx, DC_Y1               ; past it: its last
    mov dx, 0x0C1F
.hi:
    mov al, dh                  ; a clock field out of range is a clock
    dec ax                      ; nobody set: the first day of the range
    cmp al, 12
    jae .bad
    test dl, dl
    jz .bad
    mov ax, cx
    mov bl, dh
    call dc_dim_of
    cmp dl, al
    jbe .ok
.bad:
    mov cx, DC_Y0
    mov dx, 0x0101
.ok:
    xchg dh, dl                 ; DL = the month, DH = the day: [dc_tm] and
    xchg cx, [dc_ty]            ; [dc_td] as one word. Stored, and the old
    sub cx, [dc_ty]             ; less the new...
    xchg dx, [dc_tm]
    sub dx, [dc_tm]
    or cx, dx
    neg cx                      ; ...CF = 1 not zero
    jmp dc_pop4

; -----------------------------------------------------------------------------
; dc_tidx - today's grid index in the month on show, or 0FFh when today is in
; another month. dc_sidx - the selected day's. out AL; every other register kept
; -----------------------------------------------------------------------------
dc_tidx:
    push bx
    mov bx, [dc_ty]
    cmp bx, [dc_y]
    jne .no
    mov bl, [dc_tm]
    cmp bl, [dc_m]
.no:
    pop bx
    mov al, 0FFh
    jne dc_sidx.r
    mov al, [dc_td]
    jmp short dc_sidx.add

dc_sidx:
    mov al, [dc_sel]
.add:
    add al, [dc_dow1]
    dec ax
.r:
    ret

; -----------------------------------------------------------------------------
; dc_atfirst / dc_atlast - is the month on show the first / last there is?
; ZF = 1 yes. Every register kept
; -----------------------------------------------------------------------------
dc_atfirst:
    cmp word [dc_y], DC_Y0
    jne .x
    cmp byte [dc_m], 1
.x:
    ret

dc_atlast:
    cmp word [dc_y], DC_Y1
    jne .x
    cmp byte [dc_m], 12
.x:
    ret
