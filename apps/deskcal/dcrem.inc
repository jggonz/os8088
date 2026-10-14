; =============================================================================
; apps/deskcal/dcrem.inc - the Desk Calendar's REMINDERS (SPEC.md 107.3)
;
; The DIA beat (dc_tick, every 36 ticks) reads the clock; when the MINUTE has
; changed it looks for the first of today's armed, unfired, timed
; appointments whose moment - max(0, time - lead) - is at or before now, and
; fires it: FIRED set, a chirp, a toast, an OK alert, its agenda line. One a
; minute, and never while one of our alerts is up or the entry field is open,
; so alerts never stack and never take the keyboard from a typed line. A beat
; inside a minute draws nothing; nor does a minute with nothing due.
;
; At launch, today's that came due while the widget was not running are
; marked FIRED and summed up in one toast; with no hardware clock and a
; reminder still to come, the toast says the time may be wrong instead.
; =============================================================================

; dc_tpack - BX = TODAY, FAT-packed (dc_pack's shape for [dc_ty..dc_td]).
; Every other register kept
dc_tpack:
    push ax
    push cx
    mov ax, [dc_ty]
    sub ax, DC_Y0
    mov cl, 4
    shl ax, cl
    or al, [dc_tm]
    inc cx                      ; CL = 5
    shl ax, cl
    or al, [dc_td]
    mov bx, ax
    pop cx
    pop ax
    ret

; dc_moment - the appointment at SI: CX = its moment, max(0, time - lead), in
; minutes of the day - clamped at midnight rather than reaching into
; yesterday. CF = 1 it is not timed. Every other register kept
dc_moment:
    mov cx, [si + DCA_TIME]
    cmp cx, 24 * 60
    jae .no                     ; all day, or not a time at all
    push ax
    mov al, [si + DCA_LEAD]
    xor ah, ah
    sub cx, ax
    jnc .ok
    xor cx, cx
.ok:
    pop ax
    clc
    ret
.no:
    stc
    ret

; dc_due - from appointment AL on, the first that is TODAY's, ARMED (REMIND,
; not FIRED), timed, with its moment at or before DX (a SIGNED minute: -1 is
; "before midnight", which nothing is). out: CF = 0 AL = it, AH = 0; CF = 1
; none. The table is in date order, so the scan stops at the first later day.
; Every other register kept
dc_due:
    push bx
    push cx
    push si
    push di
    call dc_tpack               ; BX = today
    mov di, [DIA_PAY + DCP_NAPPT]
    and di, 0FFh                ; DI = how many
.l:
    xor ah, ah
    cmp ax, di
    jae .none
    call dc_aptr
    cmp [si + DCA_DATE], bx
    ja .none                    ; a later day: none after it is today's
    jb .n
    mov cl, [si + DCA_FLAGS]
    and cl, DCA_REMIND | DCA_FIRED
    cmp cl, DCA_REMIND
    jne .n
    call dc_moment              ; CX = its moment
    jc .n
    cmp cx, dx
    jle .yes
.n:
    inc ax
    jmp short .l
.none:
    stc
    jmp short .x
.yes:
    clc
.x:
    pop di
    pop si
    pop cx
    pop bx
    ret

; dc_rpass - the minute's work (dc_tick: the UI task, the lock held, no clip).
; Nothing unless the minute changed; then the first one due fires - unless an
; alert of ours is up (the reminder's, the Delete question, the damaged-file
; question: os88ui.inc's one gate) or the field is open, when the minute
; passes and the next fires at the first minute after (SPEC.md 107.3).
; Every register kept
dc_rpass:
    push ax
    push dx
    mov ax, [dc_now]
    cmp ax, [dc_lnow]
    je .x                       ; the same minute: a compare, no draw
    mov [dc_lnow], ax
    cmp word [os88ui_awin], 0
    jne .x                      ; one alert at a time
    cmp byte [dc_ed], 0
    jne .x                      ; never under a typed line
    mov dx, ax
    xor al, al
    call dc_due
    jc .x
    call dc_fire
.x:
    pop dx
    pop ax
    ret

; dc_fire - appointment AL's reminder goes off (the lock held, no clip):
; FIRED and a save owed; the chirp; its agenda line where it is on show; the
; toast `HH:MM text`; the alert `Reminder HH:MM text`, OK alone - without the
; prefix when that would pass OS88UI_AMAX, so the user's text is never what
; is lost (SPEC.md 107.3). Every register kept
dc_fire:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call dc_aptr                ; SI = its record
    or byte [si + DCA_FLAGS], DCA_FIRED
    call dc_touch
    mov bl, al                  ; BL = it, from here on
    mov di, dc_rmsg             ; the line: `Reminder `, the time, the text
    push si
    mov si, dc_s_rpre
    call dc_puts
    pop si
    mov ax, [si + DCA_TIME]
    call dc_tfmt
    mov byte [di], ' '
    inc di
    add si, DCA_TEXT
    mov cx, DCA_TMAX            ; at most the field's own 25, NUL'd: a file's
    call dc_copyz               ; record need not be
    mov ax, 880                 ; THE CHIRP: 880 Hz, then 660 on a beat four
    mov cx, 4                   ; ticks out (dia_nx) - one tone channel, so
    test byte [dia_st], DIA_S_WORKER    ; the second note is a second ask.
    jz .two                     ; kern_small's worker has begun its next
    mov cl, 8                   ; 36-tick sleep before this beat ran: one
    jmp short .tone             ; longer note there
.two:
    mov byte [dc_chirp], 1
    mov byte [dia_nx], 4
.tone:
    mov dl, 40h
    call OSAPI_SND_TONE         ; refused (a louder owner): no matter
    cmp byte [dc_abon], 0
    jne .said                   ; nothing under the About card
    call dc_arm
    jc .said                    ; not on the glass: the next paint shows `-`
    mov bh, bl                  ; its line, `!` to `-`: one run, BEFORE the
    mov bl, DCS_APPT            ; alert exists - the clip is our window's
    call dc_selitem             ; visible region as it is now
.said:
    mov si, dc_rmsg + DC_RMSG_PRE
    call dc_toast               ; `09:30 Dentist`: the text LAST (59.10)
    mov si, dc_rmsg
    call dc_slen                ; AX = the whole line's length
    cmp ax, OS88UI_AMAX
    jbe .ask
    add si, DC_RMSG_PRE         ; too long with the prefix: the time and text
.ask:
    mov ax, KERNEL_SEG          ; os88ui_ask reads our title out of the
    mov es, ax                  ; window record
    mov al, OS88UI_AOK
    mov bx, [dc_win]
    mov di, dc_remans
    call os88ui_ask             ; CF = 1: refused (one is up - not reached,
    jc .x                       ; dc_rpass asked - or the table is full)
    mov byte [dc_alert], 1
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_remans - the reminder's alert answered or dismissed (the lock held)
dc_remans:
    mov byte [dc_alert], 0
    ret

; dc_stale - appointment AL was just ARMED (a new one, a moved time, the mark
; toggled, a Remind pick). A moment already PAST - before this minute - is
; FIRED at once, silently, rather than going off the instant it was typed;
; one that IS this minute is the next beat's to fire, so the minute last
; seen is forgotten and dc_rpass looks again (SPEC.md 107.3) - `0930` with
; a 15-minute lead typed at 09:15 is a reminder now, not a missed one.
; Every register kept
dc_stale:
    push bx
    push cx
    push si
    call dc_aptr                ; SI = its record
    mov cl, [si + DCA_FLAGS]
    and cl, DCA_REMIND | DCA_FIRED
    cmp cl, DCA_REMIND
    jne .x                      ; not armed, or fired already
    call dc_tpack
    cmp [si + DCA_DATE], bx
    jne .x                      ; not today's: its day will come
    call dc_moment              ; CX = its moment
    jc .x                       ; all day
    cmp cx, [dc_now]
    ja .x                       ; still to come
    jb .past
    mov word [dc_lnow], 0FFFFh  ; this minute: the next beat fires it
    jmp short .x
.past:
    or byte [si + DCA_FLAGS], DCA_FIRED
.x:
    pop si
    pop cx
    pop bx
    ret

; dc_armed - CF = 0: an armed, unfired, timed reminder still to come, today
; or later. Every register kept
dc_armed:
    push ax
    push bx
    push cx
    push si
    call dc_tpack
    mov cl, [DIA_PAY + DCP_NAPPT]
    xor ch, ch
    mov si, DIA_PAY + DCP_APPT0
    jcxz .no
.l:
    cmp [si + DCA_DATE], bx
    jb .n
    mov al, [si + DCA_FLAGS]
    and al, DCA_REMIND | DCA_FIRED
    cmp al, DCA_REMIND
    jne .n
    cmp word [si + DCA_TIME], 24 * 60
    jb .yes
.n:
    add si, DCP_APPTSZ
    loop .l
.no:
    stc
    jmp short .x
.yes:
    clc
.x:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; dc_missed - the entry proc's half (after dc_today): today's reminders whose
; moment came BEFORE this minute, while the widget was not running, are
; FIRED and summed up in one toast, `3 missed reminders`. Earlier days are
; not replayed. Nothing missed and no RTC, with a reminder still to come:
; `No RTC - check the time` (OSAPI_CLK_GET's AL bit 0; SPEC.md 107.3).
; Every register kept
dc_missed:
    push ax
    push cx
    push dx
    push si
    push di
    xor cx, cx
    mov dx, [dc_now]
    dec dx                      ; BEFORE this minute: one due now is the
    xor al, al                  ; first beat's to fire
.l:
    call dc_due
    jc .d
    call dc_aptr
    or byte [si + DCA_FLAGS], DCA_FIRED
    inc cx
    inc al
    jmp short .l
.d:
    jcxz .rtc
    call dc_touch               ; the FIRED marks are the file's
    mov di, dc_rmsg
    mov al, cl
    call dc_dec2                ; AL = the tens, AH = the ones (<= 48)
    cmp al, '0'
    je .one
    mov [di], al
    inc di
.one:
    mov [di], ah
    inc di
    mov si, dc_s_miss
    call dc_puts
    cmp cl, 1
    je .z
    mov byte [di], 's'
    inc di
.z:
    mov byte [di], 0
    mov si, dc_rmsg
    call dc_toast
    jmp short .x
.rtc:
    test byte [dc_rtc], 1
    jnz .x
    call dc_armed
    jc .x
    mov si, dc_s_nortc
    call dc_toast
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

dc_s_rpre:  db 'Reminder ', 0
dc_s_miss:  db ' missed reminder', 0    ; ...and an `s` past one (<= 24)
dc_s_nortc: db 'No RTC - check the time', 0
%if DC_RMSG_PRE != 9
  %error "deskcal: DC_RMSG_PRE is `Reminder `'s length"
%endif
