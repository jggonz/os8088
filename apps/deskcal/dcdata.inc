; =============================================================================
; apps/deskcal/dcdata.inc - the Desk Calendar's payload (SPEC.md 110.3)
;
; The appointments and the tasks, as the state file holds them: checked at
; load, an appointment inserted IN ORDER, a record taken out by closing the
; gap, the done tasks cleared out, and the one-line entry's time parser.
; Nothing here draws and nothing here touches a file - an edit says
; stik_touch and os88stik.inc's next tick writes the whole file.
;
; THE ORDER is (date, time + 1), each a word compare: the date is FAT-packed
; so it orders as a word, and time + 1 makes ALL DAY (0FFFFh) wrap to 0, so an
; all-day appointment comes first in its day. tools/os88stik.py writes the same
; order, and dc_check refuses a payload that is not in it.
; =============================================================================

; -----------------------------------------------------------------------------
; dc_check - the payload as it was read: both counts inside their tables and
; the appointments in order. CF = 1 it is not ours to trust (a DAMAGED file,
; SPEC.md 110.3). Every register kept
; -----------------------------------------------------------------------------
dc_check:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [STIK_PAY + DCP_NAPPT], DCP_MAXAPPT
    ja .bad
    cmp byte [STIK_PAY + DCP_NTASK], DCP_MAXTASK
    ja .bad
    mov cl, [STIK_PAY + DCP_NAPPT]
    xor ch, ch
    mov si, STIK_PAY + DCP_APPT0
    xor dx, dx                  ; the last key: the date...
    xor bx, bx                  ; ...and its time + 1
    jcxz .ok
.l:
    mov ax, [si + DCA_DATE]
    cmp ax, dx
    jb .bad
    ja .up
    mov ax, [si + DCA_TIME]
    inc ax
    cmp ax, bx
    jb .bad
    mov bx, ax
    jmp short .n
.up:
    mov dx, ax
    mov bx, [si + DCA_TIME]
    inc bx
.n:
    add si, DCP_APPTSZ
    loop .l
.ok:
    clc
    jmp short .x
.bad:
    stc
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_pack - AX = the selected day, FAT-packed: (y - 1980) << 9 | m << 5 | d.
; Every other register kept
dc_pack:
    push cx
    mov ax, [dc_y]
    sub ax, DC_Y0
    mov cl, 4
    shl ax, cl
    or al, [dc_m]               ; a month is four bits
    inc cx                      ; CL = 5
    shl ax, cl
    or al, [dc_sel]
    pop cx
    ret

; dc_aptr - SI = appointment AL's record. dc_tptr - task AL's. The rest kept
dc_aptr:
    push cx
    mov si, ax
    and si, 0FFh
    mov cl, 5
    shl si, cl
    add si, STIK_PAY + DCP_APPT0
    pop cx
    ret

dc_tptr:
    push ax
    push dx
    xor ah, ah
    mov dl, DCP_TASKSZ
    mul dl
    add ax, STIK_PAY + DCP_TASK0
    mov si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_arange - the selected day's appointments: [dc_afirst] the first one's
; index (or where one would go), [dc_acnt] how many. The table is in order
; (dc_check), so they are one run. Every register kept
; -----------------------------------------------------------------------------
dc_arange:
    push ax
    push cx
    push dx
    push si
    call dc_pack
    mov dx, ax
    mov cl, [STIK_PAY + DCP_NAPPT]
    xor ch, ch
    mov si, STIK_PAY + DCP_APPT0
    xor ax, ax                  ; AL = the first, AH = how many
    jcxz .x
.l:
    cmp [si + DCA_DATE], dx
    ja .x
    je .on
    inc ax                      ; an earlier day (AL never reaches 256)
    jmp short .n
.on:
    inc ah
.n:
    add si, DCP_APPTSZ
    loop .l
.x:
    mov [dc_afirst], al
    mov [dc_acnt], ah
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_ains - dc_rec into the appointments, IN ORDER (SPEC.md 110.3)
; out: AL = its index; CF = 1 the table is full and nothing changed.
; Every other register kept
; -----------------------------------------------------------------------------
dc_ains:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cl, [STIK_PAY + DCP_NAPPT]
    cmp cl, DCP_MAXAPPT
    jae .full
    mov dx, [dc_rec + DCA_DATE]
    mov bx, [dc_rec + DCA_TIME]
    inc bx                      ; the key: all day first in its day
    xor ch, ch                  ; CH = the place
    mov si, STIK_PAY + DCP_APPT0
.f:
    cmp ch, cl
    je .at
    cmp [si + DCA_DATE], dx
    ja .at
    jb .nx
    mov ax, [si + DCA_TIME]
    inc ax
    cmp ax, bx
    ja .at                      ; equal keys keep their order: after them
.nx:
    add si, DCP_APPTSZ
    inc ch
    jmp short .f
.at:
    push ds
    pop es
    push cx
    mov al, cl
    sub al, ch                  ; the records at and after the place...
    xor ah, ah
    mov cl, 4
    shl ax, cl                  ; ...in words (16 a record)
    mov cx, ax
    jcxz .put
    shl ax, 1
    add si, ax                  ; one past the last of them
    dec si
    dec si
    lea di, [si + DCP_APPTSZ]
    std                         ; up one record, from the top down
    rep movsw
    cld
.put:
    pop cx
    mov al, ch
    call dc_aptr
    mov di, si
    mov si, dc_rec
    mov cx, DCP_APPTSZ / 2
    cld
    rep movsw
    inc byte [STIK_PAY + DCP_NAPPT]
    clc
    jmp short .x
.full:
    stc
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dc_adel - appointment AL out, the ones after it down one, the freed record
; zeroed. dc_tdel - the same for task AL. Every register kept
; -----------------------------------------------------------------------------
dc_adel:
    push ax
    push cx
    push si
    push di
    push es
    mov cl, [STIK_PAY + DCP_NAPPT]
    cmp al, cl
    jae .x
    push ds
    pop es
    cld
    call dc_aptr
    mov di, si
    add si, DCP_APPTSZ
    sub cl, al
    dec cl                      ; the records after it
    mov al, cl
    xor ah, ah
    mov cl, 4
    shl ax, cl
    mov cx, ax
    rep movsw                   ; DI ends on the old last record...
    mov cx, DCP_APPTSZ / 2
    xor ax, ax
    rep stosw                   ; ...which is zeroed
    dec byte [STIK_PAY + DCP_NAPPT]
.x:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

dc_tdel:
    push ax
    push cx
    push si
    push di
    push es
    mov cl, [STIK_PAY + DCP_NTASK]
    cmp al, cl
    jae .x
    push ds
    pop es
    cld
    call dc_tptr
    mov di, si
    add si, DCP_TASKSZ
    sub cl, al
    dec cl
    mov al, cl
    mov ah, DCP_TASKSZ / 2
    mul ah
    mov cx, ax
    rep movsw
    mov cx, DCP_TASKSZ / 2
    xor ax, ax
    rep stosw
    dec byte [STIK_PAY + DCP_NTASK]
.x:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_tset - task AL's text := the CX characters at SI (CX <= DCT_TMAX), the
; rest of its field zeroed. Its flags and date are left. Every register kept
; -----------------------------------------------------------------------------
dc_tset:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    push si
    call dc_tptr
    lea di, [si + DCT_TEXT]
    pop si
    push cx
    push di
    mov cx, DCT_TMAX + 1
    xor al, al
    rep stosb
    pop di
    pop cx
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_tclear - every DONE task out, the rest closed up in order and the tail
; zeroed. out AL = how many went. Every other register kept
; -----------------------------------------------------------------------------
dc_tclear:
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov cl, [STIK_PAY + DCP_NTASK]
    xor ch, ch
    mov si, STIK_PAY + DCP_TASK0
    mov di, si
    xor bl, bl                  ; BL = the tasks kept
    jcxz .tail
.l:
    test byte [si + DCT_FLAGS], DCT_DONE
    jnz .skip
    push cx
    mov cx, DCP_TASKSZ / 2
    rep movsw                   ; onto itself until the first gap: harmless
    pop cx
    inc bl
    jmp short .n
.skip:
    add si, DCP_TASKSZ
.n:
    loop .l
.tail:
    mov al, [STIK_PAY + DCP_NTASK]
    sub al, bl                  ; the ones that went
    push ax
    mov ah, DCP_TASKSZ / 2
    mul ah
    mov cx, ax
    xor ax, ax
    rep stosw
    mov [STIK_PAY + DCP_NTASK], bl
    pop ax
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; dc_anydone - CF = 1 no task is checked. Every register kept
dc_anydone:
    push cx
    push si
    mov cl, [STIK_PAY + DCP_NTASK]
    xor ch, ch
    mov si, STIK_PAY + DCP_TASK0
    jcxz .no
.l:
    test byte [si + DCT_FLAGS], DCT_DONE
    jnz .yes
    add si, DCP_TASKSZ
    loop .l
.no:
    stc
    jmp short .x
.yes:
    clc
.x:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dc_trim - the line at SI without its leading and trailing spaces: out SI =
; its first character, CX = its length, and a NUL written after its last.
; Every other register kept
; -----------------------------------------------------------------------------
dc_trim:
    push di
.sk:
    cmp byte [si], ' '
    jne .e0
    inc si
    jmp short .sk
.e0:
    mov di, si
.e:
    cmp byte [di], 0
    je .t
    inc di
    jmp short .e
.t:
    cmp di, si
    je .len
    cmp byte [di - 1], ' '
    jne .len
    dec di
    mov byte [di], 0
    jmp short .t
.len:
    mov cx, di
    sub cx, si
    pop di
    ret

; -----------------------------------------------------------------------------
; dc_parse - what an appointment's line MEANS (SPEC.md 110.3): a first word
; that is H:MM, HH:MM, HMM or HHMM - an hour 0..23, a minute 0..59, followed
; by a space or the end - is the time, and the rest is the text; anything
; else is an ALL-DAY line, the whole of it the text.
; in:  SI = the line (NUL), written: dc_trim's NUL
; out: BX = the minute of the day, or DC_ALLDAY; SI = the text; CX = its
;      length. Every other register kept
; -----------------------------------------------------------------------------
dc_parse:
    push ax
    push dx
    push di
    call dc_trim                ; SI = the first word, CX = the whole length
    mov di, si                  ; DI = the text if this is not a time
    push cx
    xor bx, bx                  ; BX = the number so far
    xor dx, dx                  ; DL = its digits, DH = the hour's + 1 once
.dg:                            ; a colon has been seen
    mov al, [si]
    cmp al, ':'
    je .colon
    sub al, '0'
    cmp al, 9
    ja .end
    cmp dl, 4
    jae .no
    shl bx, 1                   ; BX * 10, without MUL's DX
    mov cx, bx
    shl bx, 1
    shl bx, 1
    add bx, cx
    xor ah, ah
    add bx, ax
    inc dl
    inc si
    jmp short .dg
.colon:
    or dh, dh
    jnz .no                     ; a second colon
    or dl, dl
    jz .no
    cmp dl, 2
    ja .no
    mov dh, dl
    inc dh
    mov [dc_ph], bl             ; the hour
    xor bx, bx
    xor dl, dl
    inc si
    jmp short .dg
.end:
    mov al, [si]                ; the word must END here
    or al, al
    jz .fin
    cmp al, ' '
    jne .no
.fin:
    or dh, dh
    jz .bare
    cmp dl, 2                   ; H:MM / HH:MM - two minute digits
    jne .no
    mov al, [dc_ph]
    jmp short .hm
.bare:
    cmp dl, 3                   ; HMM / HHMM
    jb .no
    mov ax, bx
    mov bl, 100
    div bl                      ; AL = the hour, AH = the minute
    mov bl, ah
.hm:
    cmp al, 23
    ja .no
    cmp bl, 59
    ja .no
    mov ah, 60
    mul ah
    xor bh, bh
    add bx, ax                  ; BX = the minute of the day
.sp:
    cmp byte [si], ' '
    jne .got
    inc si
    jmp short .sp
.got:
    pop cx                      ; the length less the time and its spaces
    mov ax, si
    sub ax, di
    sub cx, ax
    jmp short .x
.no:
    pop cx
    mov si, di
    mov bx, DC_ALLDAY
.x:
    pop di
    pop dx
    pop ax
    ret
