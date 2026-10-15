; =============================================================================
; apps/deskcal/dcrem.inc - the Desk Calendar's REMINDERS (SPEC.md 107.3, v2)
;
; The DIA beat (dc_tick, every 36 ticks) reads the clock; when the MINUTE has
; changed it re-fires a SNOOZED appointment whose slot is reached, or fires
; the first of today's armed, unfired, timed appointments whose moment -
; max(0, time - lead) - is at or before now: FIRED and DUE set, a chirp, a
; toast, and CARD 3 RINGS. One a minute. There is NO ALERT any more: nothing
; waits on the user and nothing takes the keyboard (v2). Card 3 rings the
; first DUE appointment until it is dismissed (DUE off) or snoozed (SNOOZED
; on, the payload's snooze slot +4/+6 its re-fire minute and date).
;
; A beat inside a minute draws nothing; nor does a minute with nothing due.
; At launch, today's that came due while the widget was not running are
; marked FIRED and DUE - card 3 rings the first - and summed up in one toast;
; with no hardware clock and a reminder still to come, the toast says the
; time may be wrong instead.
; =============================================================================

DC3_NONE    equ 0               ; [dc_c3k]: card 3 says `No reminders today`,
DC3_NEXT    equ 1               ; ...the next armed one (or the snoozed one),
DC3_RING    equ 2               ; ...or RINGS the first DUE one
DC_SNOOZE   equ 5               ; minutes (SPEC.md 107's open decision 7)

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

; dc_tomorrow - BX = the day after today, FAT-packed - the last day of the
; range is its own tomorrow. Every other register kept
dc_tomorrow:
    push ax
    push cx
    push dx
    mov ax, [dc_ty]
    mov dh, [dc_tm]
    mov dl, [dc_td]
    push ax
    mov bl, dh
    call dc_dim_of              ; AL = today's month's length
    mov cl, al
    pop ax
    inc dl
    cmp dl, cl
    jbe .p
    mov dl, 1
    inc dh
    cmp dh, 12
    jbe .p
    mov dh, 1
    inc ax
    cmp ax, DC_Y1
    jbe .p
    call dc_tpack               ; past 2099: today again
    jmp short .x
.p:
    sub ax, DC_Y0
    mov cl, 4
    shl ax, cl
    or al, dh
    inc cx
    shl ax, cl
    or al, dl
    mov bx, ax
.x:
    pop dx
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

; dc_flagged - the first appointment carrying flag bit(s) AH: CF = 0 AL = it,
; SI = its record; CF = 1 none. Every other register kept
dc_flagged:
    push cx
    mov cl, [DIA_PAY + DCP_NAPPT]
    xor ch, ch
    xor al, al
    jcxz .no
.l:
    call dc_aptr
    test [si + DCA_FLAGS], ah
    jnz .yes
    inc ax
    loop .l
.no:
    stc
    pop cx
    ret
.yes:
    clc
    pop cx
    ret

; dc_rpass - the minute's work (dc_tick: the UI task, the lock held, no clip).
; Nothing unless the minute changed; then a snoozed appointment whose slot is
; reached rings again, or else the first one due fires - one a minute - and
; card 3 follows. Every register kept
dc_rpass:
    push ax
    push dx
    push si
    mov ax, [dc_now]
    cmp ax, [dc_lnow]
    je .x                       ; the same minute: a compare, no draw
    mov [dc_lnow], ax
    mov ah, DCA_SNOOZED         ; THE SNOOZE: its slot reached - a date
    call dc_flagged             ; before today, or today at or past its
    jc .due                     ; minute - and it rings again
    push bx
    call dc_tpack
    cmp [DIA_PAY + DCP_SNZD], bx
    pop bx
    jb .snz
    ja .due
    mov dx, [DIA_PAY + DCP_SNZT]
    cmp dx, [dc_now]
    ja .due
.snz:
    and byte [si + DCA_FLAGS], ~DCA_SNOOZED & 0FFh
    or byte [si + DCA_FLAGS], DCA_DUE
    call dc_touch
    call dc_ring                ; the chirp and the toast again
    jmp short .sync
.due:
    mov dx, [dc_now]
    xor al, al
    call dc_due
    jc .x
    call dc_fire
.sync:
    call dc_c3sync
.x:
    pop si
    pop dx
    pop ax
    ret

; dc_fire - appointment AL's reminder goes off (the lock held): FIRED and DUE
; and a save owed, then dc_ring. Card 3 is the caller's (dc_c3sync). Every
; register kept
dc_fire:
    push si
    call dc_aptr
    or byte [si + DCA_FLAGS], DCA_FIRED | DCA_DUE
    call dc_touch
    pop si
    ; fall through

; dc_ring - appointment AL rings: the chirp, and the toast `HH:MM text` -
; the user's text LAST (SPEC.md 59.10). Every register kept
dc_ring:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dc_aptr                ; SI = its record
    mov di, dc_rmsg
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
    mov si, dc_rmsg
    call dc_toast
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_snooze - S / Snooze: the RINGING appointment snoozed DC_SNOOZE minutes -
; DUE off, SNOOZED on, the slot now + 5 (into tomorrow past midnight). ONE
; snooze is held: a snoozed other goes back to DUE (SPEC.md 107.3). Every
; register kept
dc_snooze:
    cmp byte [dc_c3k], DC3_RING
    jne .r
    call dc_busy
    jc .r
    push ax
    push bx
    push si
    mov ah, DCA_SNOOZED         ; the one held already: back to ringing
    call dc_flagged
    jc .one
    and byte [si + DCA_FLAGS], ~DCA_SNOOZED & 0FFh
    or byte [si + DCA_FLAGS], DCA_DUE
.one:
    mov al, [dc_c3a]
    call dc_aptr
    and byte [si + DCA_FLAGS], ~DCA_DUE & 0FFh
    or byte [si + DCA_FLAGS], DCA_SNOOZED
    call dc_tpack               ; BX = today
    mov ax, [dc_now]
    add ax, DC_SNOOZE
    cmp ax, 24 * 60
    jb .slot
    sub ax, 24 * 60
    call dc_tomorrow
.slot:
    mov [DIA_PAY + DCP_SNZT], ax
    mov [DIA_PAY + DCP_SNZD], bx
    call dc_touch
    call dc_c3sync
    pop si
    pop bx
    pop ax
.r:
    ret

; dc_dismiss - D / Dismiss: the RINGING appointment is quiet - DUE and
; SNOOZED off; card 3 shows the next one due, the next armed, or none
dc_dismiss:
    cmp byte [dc_c3k], DC3_RING
    jne .r
    call dc_busy
    jc .r
    push ax
    push si
    mov al, [dc_c3a]
    call dc_aptr
    and byte [si + DCA_FLAGS], ~(DCA_DUE | DCA_SNOOZED) & 0FFh
    call dc_touch
    call dc_c3sync
    pop si
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; card 3's STATE. dc_c3calc - [dc_c3k] / [dc_c3a] / [dc_c3t] as the payload
; stands: RINGING the first DUE appointment (its time); else NEXT, the
; earliest of today's first armed, unfired, timed appointment and the
; snoozed one (at its slot's minute, when the slot is today); else NONE.
; Every register kept
; -----------------------------------------------------------------------------
dc_c3calc:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ah, DCA_DUE
    call dc_flagged
    jc .next
    mov byte [dc_c3k], DC3_RING
    mov [dc_c3a], al
    mov ax, [si + DCA_TIME]
    mov [dc_c3t], ax
    jmp .x
.next:
    mov byte [dc_c3k], DC3_NONE
    mov dx, 0FFFFh              ; DX = the best minute so far
    call dc_tpack               ; BX = today
    mov cl, [DIA_PAY + DCP_NAPPT]
    xor ch, ch
    xor al, al
    jcxz .snz
.l:
    call dc_aptr
    cmp [si + DCA_DATE], bx
    ja .snz
    jb .n
    mov ah, [si + DCA_FLAGS]
    and ah, DCA_REMIND | DCA_FIRED
    cmp ah, DCA_REMIND
    jne .n
    cmp word [si + DCA_TIME], 24 * 60
    jae .n
    mov dx, [si + DCA_TIME]     ; the first armed today is the earliest
    mov byte [dc_c3k], DC3_NEXT
    mov [dc_c3a], al
    mov [dc_c3t], dx
    jmp short .snz
.n:
    inc ax
    loop .l
.snz:
    mov ah, DCA_SNOOZED
    call dc_flagged
    jc .x
    cmp [DIA_PAY + DCP_SNZD], bx
    jne .x                      ; another day's: it rings at the first minute
    mov cx, [DIA_PAY + DCP_SNZT]
    cmp cx, dx
    jae .x
    mov byte [dc_c3k], DC3_NEXT
    mov [dc_c3a], al
    mov [dc_c3t], cx
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_c3sync - card 3 recomposed when its state CHANGED - or [dc_c3f] says
; its text did (an edit) - and on COMPACT a reminder that starts ringing
; TAKES THE SLOT (and gives it back once it stops, if the user did not page
; it meanwhile); never while the field is open there. The lock held. Every
; register kept
dc_c3sync:
    push ax
    push bx
    push cx
    mov al, [dc_c3k]
    mov ah, [dc_c3a]
    mov bx, [dc_c3t]
    call dc_c3calc
    xor cl, cl
    xchg cl, [dc_c3f]           ; forced, once
    cmp al, [dc_c3k]
    jne .chg
    cmp ah, [dc_c3a]
    jne .chg
    cmp bx, [dc_c3t]
    jne .chg
    or cl, cl
    jz .x
.chg:
    cmp byte [dia_li], 2
    je .slot
    mov al, 2                   ; TALL / MID: card 3
    call dc_rshow
    jmp short .x
.slot:
    cmp byte [dc_ed], 0
    jne .x                      ; the field holds the slot's heading
    cmp byte [dc_c3k], DC3_RING
    jne .quiet
    cmp byte [dc_pane], 2
    je .draw
    mov al, [dc_pane]           ; TAKEN: the page it was on, for later
    mov [dc_pane0], al
    mov byte [dc_pane], 2
    call dc_menulive
    jmp short .draw
.quiet:
    cmp byte [dc_pane], 2
    jne .x                      ; not on show
    mov al, [dc_pane0]
    cmp al, 0FFh
    je .draw                    ; the user paged here: it stays
    mov [dc_pane], al           ; given back
    mov byte [dc_pane0], 0FFh
    call dc_menulive
.draw:
    mov al, 1
    call dc_rshow
.x:
    pop cx
    pop bx
    pop ax
    ret

; dc_rshow - card AL whole, when the window is on the glass
dc_rshow:
    call dc_arm
    jc .r
    call dc_rcardw
.r:
    ret

; dc_stale - appointment AL was just ARMED (a new one, a moved time, a
; Remind pick). A moment already PAST - before this minute - is FIRED at
; once, silently (not DUE: nothing rings for it); one that IS this minute is
; the next beat's to fire, so the minute last seen is forgotten (SPEC.md
; 107.3). Every register kept
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
; FIRED and DUE - card 3 rings the first - and summed up in one toast, `3
; missed reminders`. Earlier days are not replayed. Nothing missed and no
; RTC, with a reminder still to come: `No RTC - check the time` (OSAPI_CLK_GET's
; AL bit 0). The toast is STAGED in [dc_ltp] and said by the first beat
; (dc_tick). Every register kept
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
    or byte [si + DCA_FLAGS], DCA_FIRED | DCA_DUE
    inc cx
    inc al
    jmp short .l
.d:
    jcxz .rtc
    call dc_touch               ; the marks are the file's
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
    mov word [dc_ltp], dc_rmsg  ; said by the FIRST BEAT, after the
    jmp short .x                ; registration's own toast (dc_tick)
.rtc:
    test byte [dc_rtc], 1
    jnz .x
    call dc_armed
    jc .x
    mov word [dc_ltp], dc_s_nortc
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

dc_s_miss:  db ' missed reminder', 0    ; ...and an `s` past one (<= 24)
dc_s_nortc: db 'No RTC - check the time', 0
