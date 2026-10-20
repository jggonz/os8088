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
; THE STATE FILE IS PACKED (os88stik.inc's STIK_PACK / STIK_UNPACK, SPEC.md
; 110.5.3): the 8 bytes of counts and settings, the appointments IN USE, then
; the tasks in use straight after them - so a few entries are one cluster on
; the disk where the whole 2,440-byte table was three. Full tables pack to
; exactly the fixed layout, so a file of DCP_LEN is read as it stands -
; which is every file written before the packing, too.
;
; dc_squeeze - STIK_PACK, just before a save: the tasks down behind the last
; appointment. Out CX = the payload bytes to write. Keeps BX, DX and ES
; -----------------------------------------------------------------------------
dc_squeeze:
    push es
    push ds
    pop es
    mov al, [STIK_PAY + DCP_NAPPT]
    mov ah, DCP_APPTSZ
    mul ah                      ; (a byte MUL: DX kept)
    add ax, STIK_PAY + DCP_APPT0
    xchg ax, di                 ; DI = where the tasks go
    mov al, [STIK_PAY + DCP_NTASK]
    mov ah, DCP_TASKSZ
    mul ah
    xchg ax, cx                 ; CX = their bytes
    mov si, STIK_PAY + DCP_TASK0
    cld
    rep movsb                   ; DOWN, so forward is safe
    pop es
    lea cx, [di - STIK_PAY]     ; CX = the bytes in use
    ret

; dc_spread - STIK_UNPACK, after a load and after every save: CX = the
; payload's length. DCP_LEN is the fixed layout, read as it stands; anything
; shorter must be exactly the counts' records, packed, and the tasks go back
; up to DCP_TASK0 with the gap behind them zeroed. CF = 1 it is not - a
; DAMAGED file. Keeps BX, DX and ES
dc_spread:
    cmp cx, DCP_LEN
    je .r                       ; (CF = 0)
    push es
    push ds
    pop es
    mov al, [STIK_PAY + DCP_NTASK]
    cmp al, DCP_MAXTASK
    ja .bad
    mov ah, DCP_TASKSZ
    mul ah
    xchg ax, di                 ; DI = the tasks' bytes
    mov al, [STIK_PAY + DCP_NAPPT]
    cmp al, DCP_MAXAPPT
    ja .bad
    mov ah, DCP_APPTSZ
    mul ah
    add ax, DCP_APPT0           ; AX = where the packed tasks start
    mov si, ax
    add si, di                  ; SI = where they end, which must be the
    cmp si, cx                  ; file's own length
    jne .bad
    mov cx, di
    add si, STIK_PAY - 1        ; the last packed byte...
    lea di, [di + STIK_PAY + DCP_TASK0 - 1] ; ...to the last task byte
    std
    rep movsb                   ; UP, so backwards
    cld
    mov cx, DCP_TASK0
    sub cx, ax
    xchg ax, di
    add di, STIK_PAY            ; the gap: the records not in use, zero as
    xor al, al                  ; dc_adel leaves them
    rep stosb
    pop es                      ; (CF = 0: the XOR's)
    ret
.bad:
    pop es
    stc
.r:
    ret

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
    call dc_aall
    mov al, DCP_MAXAPPT         ; (every way out below is CF = 1 bad, or
    cmp al, cl                  ; CF = 0 good)
    jb .x
    mov al, DCP_MAXTASK
    cmp al, [STIK_PAY + DCP_NTASK]
    jb .x
    xor dx, dx                  ; the last key: the date...
    xor bx, bx                  ; ...and its time + 1
    jcxz .x
.l:
    mov ax, [si + DCA_TIME]
    inc ax
    cmp ax, bx                  ; this key less the last, as one 32-bit
    mov bx, ax                  ; subtraction: a borrow is out of order
    mov ax, [si + DCA_DATE]
    sbb ax, dx
    jc .x
    mov dx, [si + DCA_DATE]
    add si, DCP_APPTSZ
    loop .l
.x:
    jmp dc_pop5

; dc_pack - AX = the selected day, FAT-packed: (y - 1980) << 9 | m << 5 | d.
; Every other register kept
dc_pack:
    push dx
    mov ax, [dc_y]
    mov dx, [dc_m]              ; DL = the month, DH = the day
    call dc_fpack
    pop dx
    ret

; dc_fpack - AX = a year, DL = a month, DH = a day: AX = them FAT-packed.
; Every other register kept
dc_fpack:
    push cx
    sub ax, DC_Y0
    mov cl, 4
    shl ax, cl
    or al, dl                   ; a month is four bits
    inc cx                      ; CL = 5
    shl ax, cl
    or al, dh
    pop cx
    ret

; dc_tall - CX = the tasks in use, SI = the first's record. dc_aall - the
; same for the appointments. The rest kept
dc_tall:
    mov si, STIK_PAY + DCP_TASK0
    mov cl, [STIK_PAY + DCP_NTASK]
    jmp short dc_all
dc_aall:
    mov si, STIK_PAY + DCP_APPT0
    mov cl, [STIK_PAY + DCP_NAPPT]
dc_all:
    xor ch, ch
    ret

; dc_aptr - SI = appointment AL's record, CF = 0. dc_tptr - task AL's.
; dc_anext - CF = 1 AL is not an appointment in use, else dc_aptr. The rest
; kept
dc_tptr:
    push ax
    mov ah, DCP_TASKSZ
    mul ah
    add ax, STIK_PAY + DCP_TASK0
    jmp short dc_ptrx

dc_anext:
    cmp al, [STIK_PAY + DCP_NAPPT]
    cmc
    jc dc_ptrx.r

dc_aptr:
    push ax
    mov ah, DCP_APPTSZ
    mul ah
    add ax, STIK_PAY + DCP_APPT0    ; (no carry: CF = 0)
dc_ptrx:
    xchg ax, si
    pop ax
.r:
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
    xchg ax, dx
    call dc_aall
    xor ax, ax                  ; AL = the earlier days', AH = those and
    jcxz .x                     ; the day's own
.l:
    cmp [si + DCA_DATE], dx
    ja .x
    adc ax, 100h                ; CF = 1 an earlier day (AL never reaches 256)
    add si, DCP_APPTSZ
    loop .l
.x:
    sub ah, al
    mov [dc_afirst], ax         ; [dc_acnt] is the byte after it
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_ains - dc_rec into the appointments, IN ORDER (SPEC.md 110.3)
; out: AL = its index; CF = 1 the table is full and nothing changed. AH
; clobbered (it always was); every other register kept
; -----------------------------------------------------------------------------
dc_ains:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call dc_aall                ; CH = the place
    cmp cl, DCP_MAXAPPT
    cmc
    jc .x                       ; full
    mov dx, [dc_rec + DCA_DATE]
    mov bx, [dc_rec + DCA_TIME]
    inc bx                      ; the key: all day first in its day
.f:
    cmp ch, cl
    je .at
    mov ax, [si + DCA_TIME]
    inc ax
    cmp bx, ax                  ; the new key less this one, as one 32-bit
    mov ax, dx                  ; subtraction: a borrow puts it here - and
    sbb ax, [si + DCA_DATE]     ; equal keys keep their order: after them
    jc .at
    add si, DCP_APPTSZ
    inc ch
    jmp short .f
.at:
    push ds
    pop es
    sub cl, ch                  ; the records at and after the place...
    mov al, DCP_APPTSZ / 2
    mul cl                      ; ...in words
    xchg ax, cx                 ; AH = the place
    add si, cx
    add si, cx                  ; one past the last record...
    dec si
    dec si                      ; ...and back a word: SI = its last word
    lea di, [si + DCP_APPTSZ]
    std                         ; up one record, from the top down - and
    rep movsw                   ; DI ends on the place's last word, moved
    mov si, dc_rec + DCP_APPTSZ - 2 ; or not, for dc_rec to go in
    mov cl, DCP_APPTSZ / 2
    rep movsw
    cld
    mov al, ah
    inc byte [STIK_PAY + DCP_NAPPT]
    clc
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
dc_tdel:
    push ax
    push bx
    push cx
    push si
    call dc_tptr
    mov ch, DCP_TASKSZ
    mov bx, STIK_PAY + DCP_NTASK
    jmp short dc_del

dc_adel:
    push ax
    push bx
    push cx
    push si
    call dc_aptr
    mov ch, DCP_APPTSZ
    mov bx, STIK_PAY + DCP_NAPPT
dc_del:                         ; SI = record AL, CH = a record's bytes, BX =
    push di                     ; the table's count; AX BX CX SI pushed
    push es
    mov cl, [bx]
    cmp al, cl
    jae .x
    push ds
    pop es
    cld
    mov di, si
    sub cl, al
    dec cx                      ; CL = the records after it (no borrow)
    mov al, ch
    cbw
    add si, ax                  ; SI = the next record
    shr al, 1
    mul cl                      ; AX = their words
    xchg ax, cx                 ; AH = a record's bytes
    rep movsw                   ; DI ends on the old last record...
    mov cl, ah
    xor ax, ax
    rep stosb                   ; ...which is zeroed
    dec byte [bx]
.x:
    pop es
    pop di
    pop si
    pop cx
    pop bx
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
    mov ax, DCT_TMAX + 1
    sub ax, cx                  ; the field's bytes past the text
    rep movsb
    xchg ax, cx                 ; AX = 0
    rep stosb
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
    call dc_tall
    mov di, si
    xor bx, bx                  ; BL = the tasks that went
    jcxz .tail
.l:
    test byte [si + DCT_FLAGS], DCT_DONE
    jnz .skip
    push cx
    mov cx, DCP_TASKSZ / 2
    rep movsw                   ; onto itself until the first gap: harmless
    pop cx
    jmp short .n
.skip:
    add si, DCP_TASKSZ
    inc bx
.n:
    loop .l
.tail:
    mov cx, si                  ; SI is past the last task, DI past the last
    sub cx, di                  ; kept: the gap between is zeroed
    mov al, 0
    rep stosb
    sub [STIK_PAY + DCP_NTASK], bl
    mov al, bl
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
    call dc_tall
    jcxz .no
.l:
    test byte [si + DCT_FLAGS], DCT_DONE
    jnz .x                      ; CF = 0 (TEST)
    add si, DCP_TASKSZ
    loop .l
.no:
    stc
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
    dec si
.sk:
    inc si
    cmp byte [si], ' '
    je .sk
    lea di, [si - 1]
.e:
    inc di
    cmp byte [di], 0
    jne .e
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
;
; The digits run on over the colon, so H:MM reads H * 100 + MM as HMM does
; and one divide splits either; DH holds how many came before the colon.
; -----------------------------------------------------------------------------
dc_parse:
    push ax
    push dx
    push di
    call dc_trim                ; SI = the first word, CX = the whole length
    mov di, si                  ; DI = the text if this is not a time
    push cx
    xor bx, bx                  ; BX = the number so far
    xor dx, dx                  ; DL = its digits, DH = those before a colon
.dg:
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
    cbw
    add bx, ax
    inc dx
.nx:
    inc si
    jmp short .dg
.colon:
    mov ax, dx                  ; one or two digits before it, and no colon
    dec ax                      ; before them: DX = 1 or 2
    cmp ax, 2
    jae .no
    mov dh, dl
    jmp short .nx
.end:
    add al, '0'                 ; the word must END here: a space or the
    test al, ~' ' & 0FFh        ; NUL, the two characters with no other bit
    jnz .no

    mov al, dl
    or dh, dh
    jz .bare
    sub al, dh                  ; H:MM / HH:MM - two minute digits...
    cmp al, 2
    jne .no
    inc ax                      ; ...which the next test passes
.bare:
    cmp al, 3                   ; HMM / HHMM
    jb .no
    mov ax, bx
    mov cl, 100
    div cl                      ; AL = the hour, AH = the minute
    cmp al, 23
    ja .no
    cmp ah, 59
    ja .no
    mov ah, 40
    mul ah
    sub bx, ax                  ; BX = H * 100 + MM - 40 * H: the minute of
                                ; the day
    dec si
.sp:
    inc si
    cmp byte [si], ' '
    je .sp
    pop cx                      ; the length less the time and its spaces
    add cx, di
    sub cx, si
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
