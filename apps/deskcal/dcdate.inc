; =============================================================================
; apps/deskcal/dcdate.inc - the Desk Calendar's date maths (SPEC.md 110.3)
;
; All integer, all 8086, and exact over the range the payload can store:
; January 1980 to December 2099, the FAT date's own span. Nothing here draws
; and nothing here touches a file.
;
;   dc_dim_of    days in a month       clock.inc's clk_mlen: the mask 0x15AA
;                                      holds the 31-day months, February is
;                                      28 + (year AND 3 = 0) - exact 1901-2099
;   dc_dow_of    day of the week       Sakamoto's, as apps/dos's dos_dow
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
    push dx
    mov dx, 0x15AA              ; bit m set: month m has 31 days
    mov cl, bl
    shr dx, cl
    mov cl, al                  ; the year's low byte: AND 3 is all it needs
    mov ax, 31
    test dl, 1
    jnz .x
    dec ax                      ; 30
    cmp bl, 2
    jne .x
    mov al, 28
    test cl, 3
    jnz .x
    inc ax                      ; a leap year (every fourth, 1901-2099)
.x:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; dc_dow_of - the day of the week, 0 = Sunday (Sakamoto)
; in:  AX = year, BL = month 1..12, BH = day
; out: AL = 0..6, AH = 0; every other register kept
; -----------------------------------------------------------------------------
dc_dow_of:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                  ; DI: the month low, the day high
    cmp bl, 3
    sbb ax, 0                   ; January and February count as the year before
    mov si, ax                  ; SI = the sum, from y
    mov cx, ax
    shr cx, 1
    shr cx, 1
    add si, cx                  ; + y/4
    xor dx, dx
    mov cx, 100
    div cx                      ; AX = y/100
    sub si, ax
    shr ax, 1
    shr ax, 1
    add si, ax                  ; + y/400 = (y/100)/4
    mov bx, di
    xor bh, bh
    mov al, [dc_sakt + bx - 1]
    xor ah, ah
    add si, ax                  ; + t[m-1]
    mov ax, di
    mov al, ah
    xor ah, ah
    add ax, si                  ; + d
    xor dx, dx
    mov cx, 7
    div cx
    mov ax, dx                  ; mod 7
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

dc_sakt:    db 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4

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
    push bp
    push es
    mov ax, [dc_y]
    mov bl, [dc_m]
    call dc_dim_of
    mov [dc_dim], al
    mov bh, 1
    mov ax, [dc_y]
    call dc_dow_of              ; 0 = Sunday...
    add al, 6                   ; ...and the grid is MONDAY-FIRST: the 1st's
    cmp al, 7                   ; column is (dow + 6) % 7 (SPEC.md 110.3, v2;
    jb .mon                     ; display only - the payload holds dates)
    sub al, 7
.mon:
    mov [dc_dow1], al
    push ds
    pop es
    cld
    mov di, dc_grid
    xor cx, cx
    mov cl, al                  ; the leading blanks
    xor al, al
    rep stosb
    mov cl, [dc_dim]
    mov al, 1
.d:
    stosb
    inc ax
    loop .d
    mov cx, dc_grid + DC_GRIDN
    sub cx, di                  ; the trailing blanks
    xor al, al
    rep stosb
    mov di, dc_mark             ; the marks: one bit a day
    xor ax, ax
    stosw
    stosw
    mov cl, [STIK_PAY + DCP_NAPPT]
    cmp cl, DCP_MAXAPPT
    ja .done                    ; not ours to trust: mark nothing
    xor ch, ch
    jcxz .done
    mov ax, [dc_y]              ; (y - 1980) << 4 | m: the packed date's
    sub ax, DC_Y0               ; bits 5.. - a word compare against it
    mov dx, ax
    mov ax, 16
    mul dx                      ; AX = (y - 1980) * 16
    or al, [dc_m]
    mov dx, ax
    mov si, STIK_PAY + DCP_APPT0
.a:
    mov ax, [si + DCA_DATE]
    mov bx, ax
    and bx, 31                  ; the day
    push cx
    mov cl, 5
    shr ax, cl
    pop cx
    cmp ax, dx
    jne .n
    mov ax, bx                  ; set bit `day` of the 32-bit mask
    push cx
    mov cl, al
    and cl, 7
    mov al, 1
    shl al, cl
    shr bx, 1
    shr bx, 1
    shr bx, 1
    or [dc_mark + bx], al
    pop cx
.n:
    add si, DCP_APPTSZ
    loop .a
.done:
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
; dc_marked - does day AL of the month on show carry a mark? ZF = 0 yes.
; Every register kept
; -----------------------------------------------------------------------------
dc_marked:
    push ax
    push bx
    push cx
    mov bl, al
    xor bh, bh
    mov cl, bl
    and cl, 7
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov al, 1
    shl al, cl
    test [dc_mark + bx], al
    pop cx
    pop bx
    pop ax
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
    jae .lo
    mov cx, DC_Y0
    mov dx, 0x0101
.lo:
    cmp cx, DC_Y1
    jbe .hi
    mov cx, DC_Y1
    mov dx, 0x0C1F
.hi:
    cmp dh, 1                   ; a clock field out of range is a clock
    jb .bad                     ; nobody set: the first day of the range
    cmp dh, 12
    ja .bad
    cmp dl, 1
    jb .bad
    mov ax, cx
    mov bl, dh
    call dc_dim_of
    cmp dl, al
    jbe .ok
.bad:
    mov cx, DC_Y0
    mov dx, 0x0101
.ok:
    cmp cx, [dc_ty]
    jne .new
    cmp dh, [dc_tm]
    jne .new
    cmp dl, [dc_td]
    jne .new
    clc
    jmp short .x
.new:
    mov [dc_ty], cx
    mov [dc_tm], dh
    mov [dc_td], dl
    stc
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_tidx - today's grid index in the month on show, or 0FFh when today is in
; another month. dc_sidx - the selected day's. out AL; every other register kept
; -----------------------------------------------------------------------------
dc_tidx:
    mov al, 0FFh
    push bx
    mov bx, [dc_ty]
    cmp bx, [dc_y]
    jne .x
    mov bl, [dc_tm]
    cmp bl, [dc_m]
    jne .x
    mov al, [dc_td]
    add al, [dc_dow1]
    dec ax
.x:
    pop bx
    ret

dc_sidx:
    mov al, [dc_sel]
    add al, [dc_dow1]
    dec ax
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
