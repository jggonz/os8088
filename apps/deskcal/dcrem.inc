; =============================================================================
; apps/deskcal/dcrem.inc - the Desk Calendar's REMINDERS (SPEC.md 110.3, v2)
;
; The Sticky App beat (dc_tick, every 36 ticks) reads the clock; when the MINUTE has
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
DC_SNOOZE   equ 5               ; minutes (STICKY-APPS-LAYER-PLAN.md 7, decision 7)

; dc_tpack - BX = TODAY, FAT-packed (dc_pack's shape for [dc_ty..dc_td]).
; Every other register kept
dc_tpack:
    push ax
    push dx
    mov ax, [dc_ty]
    mov dx, [dc_tm]             ; DL = the month, DH = the day
    call dc_fpack
    xchg ax, bx
    pop dx
    pop ax
    ret

; dc_armm - the appointment at SI: CF = 0 it is ARMED (REMIND, not FIRED) and
; timed, CX = its moment, max(0, time - lead), in minutes of the day -
; clamped at midnight rather than reaching into yesterday. CF = 1 not. CX
; clobbered; every other register kept
dc_armm:
    mov cl, [si + DCA_FLAGS]
    and cl, DCA_REMIND | DCA_FIRED
    dec cl                      ; ZF = 1: REMIND alone
    stc
    jnz .r
    mov cx, [si + DCA_TIME]
    cmp cx, 24 * 60
    cmc
    jc .r                       ; all day, or not a time at all
    sub cl, [si + DCA_LEAD]
    sbb ch, 0
    jnc .r
    xor cx, cx
.r:
    ret

; dc_due - from appointment AL on, the first that is TODAY's, ARMED and
; timed, with its moment BELOW DX (unsigned: 0 is "before midnight", which
; nothing is). out: CF = 0 AL = it, SI = its record; CF = 1 none (AL and
; SI moved on). BX = today; CX clobbered; every other register kept. The
; table is in date order, so the scan stops at the first later day
dc_due:
    call dc_tpack               ; BX = today
.l:
    call dc_anext
    jc .r
    cmp bx, [si + DCA_DATE]
    jb .r                       ; a later day: none after it is today's
    ja .n
    call dc_armm                ; CX = its moment
    jc .n
    cmp dx, cx
    ja .r                       ; CF = 0
.n:
    inc ax
    jmp short .l
.r:
    ret

; dc_flagged - the first appointment carrying flag bit(s) AH: CF = 0 AL = it,
; SI = its record; CF = 1 none. Every other register kept
dc_flagged:
    xor al, al
.l:
    call dc_anext
    jc .r
    inc ax
    test [si + DCA_FLAGS], ah   ; CF = 0
    jz .l
    dec ax
.r:
    ret

; dc_rpass - the minute's work (dc_tick: the UI task, the lock held, no clip).
; Nothing unless the minute changed; then a snoozed appointment whose slot is
; reached rings again, or else the first one due fires - FIRED and DUE and a
; save owed - one a minute. Either RINGS: the chirp, and the toast `HH:MM
; text` - the user's text LAST (SPEC.md 59.10) - and card 3 follows. Every
; register kept
dc_rpass:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [dc_now]
    cmp dx, [dc_lnow]
    je .x                       ; the same minute: a compare, no draw
    mov [dc_lnow], dx
    mov ah, DCA_SNOOZED         ; THE SNOOZE: its slot reached - a date
    call dc_flagged             ; before today, or today at or past its
    jc .due                     ; minute - and it rings again
    call dc_tpack
    cmp dx, [STIK_PAY + DCP_SNZT]   ; (today, now) less the slot, as one
    sbb bx, [STIK_PAY + DCP_SNZD]   ; 32-bit subtraction: a borrow is a slot
    jc .due                         ; still to come
.snz:
    call dc_unsnz
    jmp short .ring
.due:
    inc dx                      ; a moment at or before this minute
    xor al, al
    call dc_due
    jc .x
    or byte [si + DCA_FLAGS], DCA_FIRED | DCA_DUE
.ring:                          ; SI = it
    call dc_touch
    mov di, dc_rmsg
    mov ax, [si + DCA_TIME]
    call dc_tfmt
    mov byte [di], ' '
    inc di
    add si, DCA_TEXT
    mov cx, DCA_TMAX            ; at most the field's own 25, NUL'd: a file's
    call dc_copyz               ; record need not be
    mov ax, 880                 ; THE CHIRP: 880 Hz, then 660 on a beat four
    mov cx, 4                   ; ticks out (stik_nx) - one tone channel, so
%ifndef STIK_DESKONLY           ; the second note is a second ask.
    test byte [stik_st], STIK_S_WORKER   ; kern_small's worker has begun its
    jz .two                     ; next 36-tick sleep before this beat ran:
    mov cl, 8                   ; one longer note there
    jmp short .tone
.two:
%endif
    mov [dc_chirp], cl          ; owed (non-zero)...
    mov [stik_nx], cl           ; ...four ticks out
.tone:
    mov dl, 40h
    call OSAPI_SND_TONE         ; refused (a louder owner): no matter
    mov si, dc_rmsg
    call dc_toast
    call dc_c3sync
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_rng - CF = 0: card 3 RINGS and the field is shut, so it may be acted
; on: SI = the ringing appointment's record, AL = its index. CF = 1 not.
; AX and SI clobbered either way
dc_rng:
    call dc_busy
    jc .r
    mov ax, [dc_c3k]            ; AL = the kind, AH = the appointment
    cmp al, DC3_RING
    stc
    jne .r
    mov al, ah
    jmp dc_aptr                 ; CF = 0
.r:
    ret

; dc_unsnz - the snoozed appointment at SI rings again: SNOOZED off, DUE on.
; Every register kept
dc_unsnz:
    and byte [si + DCA_FLAGS], ~DCA_SNOOZED & 0FFh
    or byte [si + DCA_FLAGS], DCA_DUE
    ret

; dc_snooze - S / Snooze: the RINGING appointment snoozed DC_SNOOZE minutes -
; DUE off, SNOOZED on, the slot now + 5 (into tomorrow past midnight, the
; last day of the range being its own tomorrow). ONE snooze is held: a
; snoozed other goes back to DUE (SPEC.md 110.3). Every register kept
dc_snooze:
    push ax
    push bx
    push cx
    push dx
    push si
    call dc_rng                 ; SI = the ringing one
    jc dc_pop5
    push si
    mov ah, DCA_SNOOZED         ; the one held already: back to ringing
    call dc_flagged
    jc .one
    call dc_unsnz
.one:
    pop si
    and byte [si + DCA_FLAGS], ~DCA_DUE & 0FFh
    or byte [si + DCA_FLAGS], DCA_SNOOZED
    mov ax, [dc_now]
    add ax, DC_SNOOZE
    cmp ax, 24 * 60
    xchg ax, si                 ; SI = the slot's minute
    mov ax, [dc_ty]             ; the slot's day: today...
    mov dx, [dc_tm]             ; DL = the month, DH = the day
    jb .slot
    sub si, 24 * 60             ; ...or, past midnight, tomorrow
    push ax
    mov bl, dl
    call dc_dim_of              ; AL = today's month's length
    cmp dh, al                  ; CF = 1: not its last day
    pop ax
    inc dh
    jb .slot
    mov dh, 1
    inc dx                      ; the next month (DL <= 12: no carry)
    cmp dl, 12
    jbe .slot
    mov dl, 1
    inc ax
    cmp ax, DC_Y1
    jbe .slot
    dec ax                      ; past 2099: 2099-12-31 again
    mov dx, 0x1F0C
.slot:
    call dc_fpack
    mov [STIK_PAY + DCP_SNZT], si
    mov [STIK_PAY + DCP_SNZD], ax
    jmp short dc_dismiss.tail

; dc_dismiss - D / Dismiss: the RINGING appointment is quiet - DUE and
; SNOOZED off; card 3 shows the next one due, the next armed, or none.
; Every register kept
dc_dismiss:
    push ax
    push bx
    push cx
    push dx
    push si
    call dc_rng
    jc dc_pop5
    and byte [si + DCA_FLAGS], ~(DCA_DUE | DCA_SNOOZED) & 0FFh
.tail:
    call dc_touch
    call dc_c3sync
dc_pop5:                        ; THE SHARED EPILOGUES, reached by a jump:
    pop si                      ; SI DX CX BX AX pushed in that order...
dc_pop4:
    pop dx                      ; ...or DX CX BX AX, or CX BX AX
dc_pop3:
    pop cx
    pop bx
    pop ax
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
    mov dx, [si + DCA_TIME]
    mov ah, DC3_RING
    jmp short .ring
.next:
    mov byte [dc_c3k], DC3_NONE
    mov dx, 0FFFFh              ; DX = the best minute so far
    xor al, al
    call dc_due                 ; BX = today
    jc .snz
    mov dx, [si + DCA_TIME]     ; the first armed today is the earliest
    call .nx
.snz:
    mov ah, DCA_SNOOZED
    call dc_flagged
    jc .x
    cmp [STIK_PAY + DCP_SNZD], bx
    jne .x                      ; another day's: it rings at the first minute
    cmp [STIK_PAY + DCP_SNZT], dx
    jae .x
    mov dx, [STIK_PAY + DCP_SNZT]
    mov ah, DC3_NEXT
.ring:
    call .put
.x:
    jmp short dc_pop5
.nx:
    mov ah, DC3_NEXT
.put:                           ; [dc_c3k] = AH, [dc_c3a] = AL, [dc_c3t] = DX
    xchg al, ah
    mov [dc_c3k], ax
    mov [dc_c3t], dx
    ret

; dc_c3sync - card 3 recomposed when its state CHANGED - or [dc_c3f] says
; its text did (an edit) - and on COMPACT a reminder that starts ringing
; TAKES THE SLOT (and gives it back once it stops, if the user did not page
; it meanwhile); never while the field is open there - the sync is owed
; ([dc_c3f]) until it shuts - and nothing is drawn under the About card,
; whose dismissal paints the face. The lock held. Every register kept
dc_c3sync:
    push ax
    push bx
    push cx
    mov ax, [dc_c3k]            ; AL = the kind, AH = the appointment
    mov bx, [dc_c3t]
    call dc_c3calc
    sub ax, [dc_c3k]            ; 0 when the state stands...
    sub bx, [dc_c3t]
    or ax, bx
    xor cx, cx
    xchg cl, [dc_c3f]           ; ...and is not forced, once
    or ax, cx
    jz .x
    mov al, 2                   ; TALL / MID: card 3
    cmp [stik_li], al
    jne .show
    cmp [dc_ed], ch             ; (CH = 0)
    je .open
    mov [dc_c3f], al            ; the field holds the slot's heading: the
    jmp short .x                ; sync is OWED, and its shutting forces it
.open:
    cmp [dc_c3k], al            ; AL = 2 = DC3_RING
    mov ax, [dc_pane]           ; AL = the page, AH = [dc_pane0]
    jne .quiet
    cmp al, 2
    je .draw
    mov ah, al                  ; TAKEN: the page it was on, for later
    mov al, 2
    jmp short .give
.quiet:
    cmp al, 2
    jne .x                      ; not on show
    cmp ah, 0FFh
    je .draw                    ; the user paged here: it stays
    mov al, ah                  ; given back
    mov ah, 0FFh
.give:
    mov [dc_pane], ax
    call dc_menulive
.draw:
    mov al, 1
.show:
    call dc_arm                 ; card AL whole, when the window is on the
    jc .x                       ; glass
    call dc_rcardw
.x:
    jmp dc_pop3

; dc_stale - appointment AL was just ARMED (a new one, a moved time, a
; Remind pick). A moment already PAST - before this minute - is FIRED at
; once, silently (not DUE: nothing rings for it); one that IS this minute is
; the next beat's to fire, so the minute last seen is forgotten (SPEC.md
; 110.3). Every register kept
dc_stale:
    push bx
    push cx
    push si
    call dc_aptr                ; SI = its record
    call dc_armm                ; CX = its moment
    jc .x                       ; not armed, fired already, or all day
    call dc_tpack
    cmp [si + DCA_DATE], bx
    jne .x                      ; not today's: its day will come
    cmp cx, [dc_now]
    ja .x                       ; still to come
    jb .past
    or word [dc_lnow], -1       ; this minute: the next beat fires it
    jmp short .x
.past:
    or byte [si + DCA_FLAGS], DCA_FIRED
.x:
    pop si
    pop cx
    pop bx
    ret

; dc_missed - the entry proc's half (after dc_today): today's reminders whose
; moment came BEFORE this minute, while the widget was not running, are
; FIRED and DUE - card 3 rings the first - and summed up in one toast, `3
; missed reminders`. Earlier days are not replayed. Nothing missed and no
; RTC, with an armed, unfired, timed reminder still to come today or later:
; `No RTC - check the time` (OSAPI_CLK_GET's AL bit 0). The toast is STAGED
; in [dc_ltp] and said by the first beat (dc_tick). Every register kept
dc_missed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [dc_now]            ; BEFORE this minute: one due now is the
    xor ax, ax                  ; first beat's to fire
    xor di, di                  ; DI = how many
.l:
    call dc_due
    jc .d
    or byte [si + DCA_FLAGS], DCA_FIRED | DCA_DUE
    inc di
    inc ax
    jmp short .l
.d:
    mov cx, di
    jcxz .rtc
    call dc_touch               ; the marks are the file's
    mov di, dc_rmsg
    mov al, cl
    call dc_putn                ; one digit or two (<= 48)
    mov si, dc_s_miss
    call dc_puts
    mov word [di], 's'          ; ...an `s` and the NUL...
    loop .z
    mov [di], ch                ; ...or, for one, the NUL alone
.z:
    mov ax, dc_rmsg             ; said by the FIRST BEAT, after the
    jmp short .st               ; registration's own toast (dc_tick)
.rtc:
    test byte [dc_rtc], 1
    jnz .x
    call dc_tpack
    xor al, al
.a:
    call dc_anext
    jc .x
    inc ax
    cmp [si + DCA_DATE], bx
    jb .a
    call dc_armm
    jc .a
    mov ax, dc_s_nortc
.st:
    mov [dc_ltp], ax
.x:
    jmp dc_rpass.x

dc_s_miss:  db ' missed reminder', 0    ; ...and an `s` past one (<= 24)
dc_s_nortc: db 'No RTC - check the time', 0
