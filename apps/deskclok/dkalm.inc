; =============================================================================
; apps/deskclok/dkalm.inc - THE ALARM window (SPEC.md 110.14.7): a titled
; second window, opened by the bell's row on the clock, `A`, or Clock >
; Alarm. It edits a COPY - on or off, the hour and the minute (and AM/PM
; under the 12-hour setting), a label of up to 16 characters - which OK
; writes into the payload (owing the save) and Cancel, Esc and the close box
; drop.
;
;   [x] Alarm on
;   Time:  [07] : [30] [AM]   [-] [+]
;   Label: [Wake up_         ]
;                      [Cancel] [ OK ]
;
; The keys: Tab and Left/Right move between the hour, the minute, the AM/PM
; and the label; Up/Down and - / + step the hour or the minute; digits type
; them; Space ticks the box (outside the label); Backspace edits the label;
; Enter is OK and Esc Cancel. It is a FORM of dkset.inc's: its layouts are
; data here, and the painting, the presses and the window are the settings'.
; =============================================================================

DKA_W       equ 232             ; the content's width
DKAF_HOUR   equ 0               ; DKST_FLD's param: the fields
DKAF_MIN    equ 1
DKAF_AMPM   equ 2
DKAF_LABEL  equ 3

; the alarm's ONE layout, on every screen class - dkset.inc's form machine
; (DKSE, DKST_*, DKB_*): no gallery, so no preview widths. It is 92 rows, so
; CGA's 200 hold it as well as VGA's 480 (SPEC.md 110.14.7)
dk_ageos:   dw dka_tall, dka_tall, dka_tall
dka_tall:
    dw 92
    db 0, 0
    dw DKA_W
    DKSE DKST_CHK, DKF_ALARM, 8, 6, 120, 12, dkas_on
    DKSE DKST_TEXT, 0, 8, 28, 0, 0, dkas_time
    DKSE DKST_FLD, DKAF_HOUR, 64, 24, 26, 14, 0
    DKSE DKST_TEXT, 0, 94, 28, 0, 0, dkas_colon
    DKSE DKST_FLD, DKAF_MIN, 102, 24, 26, 14, 0
    DKSE DKST_FLD, DKAF_AMPM, 134, 24, 26, 14, 0
    DKSE DKST_BTN, DKB_MINUS, 170, 24, 24, 14, dkas_minus
    DKSE DKST_BTN, DKB_PLUS, 200, 24, 24, 14, dkas_plus
    DKSE DKST_TEXT, 0, 8, 50, 0, 0, dkas_label
    DKSE DKST_FLD, DKAF_LABEL, 64, 46, 160, 14, 0
    DKSE DKST_BTN, DKB_CANCEL, 96, 70, 60, 16, dkss_cancel
    DKSE DKST_BTN, DKB_OK, 164, 70, 60, 16, dkss_ok
    db DKST_END
dkas_on:    db 'Alarm on', 0
dkas_time:  db 'Time:', 0
dkas_colon: db ':', 0
dkas_label: db 'Label:', 0
dkas_minus: db '-', 0
dkas_plus:  db '+', 0
dkas_title: db 'Alarm', 0

dk_atpl:    dw 0, MBAR_H + 24, DKA_W + 2, 0
            dw dkas_title, dk_apaint, dk_akey, dk_aclick
            dw dk_falm, dk_ageos, dk_aclose, dk_amup, dk_ainit

; dk_ainit - DKT_INIT (dkset.inc's dk_almopen): the edit, a copy of the
; payload's alarm, the hour with the keys. Keeps everything
dk_ainit:
    push ax
    push cx
    push si
    push di
    push es
    mov al, [DKP + DKP_FLAGS]   ; the copy
    and al, DKF_ALARM
    mov [dk_aon], al
    push ds
    pop es
    mov si, DKP + DKP_AH        ; the hour, the minute, the label
    mov di, dk_ah
    mov cx, DKA_EDIT
    cld
    rep movsb
    mov byte [dk_alab + DK_LABMAX], 0
    mov byte [dk_afoc], DKAF_HOUR
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; dk_aclose - W_ONCLOSE: the close box is Cancel (CF = 1: gone by our hand)
dk_aclose:
    call dk_ashut
    stc
    ret

; dk_aok - the copy into the payload: saved, a snooze dropped, the clock's
; row again - and into the settings' applied copy when they are up, so a
; Cancel there does not undo it (the alarm's fields alone: what the settings
; preview is theirs)
dk_aok:
    push bx
    mov bx, DKP
    call dk_aput
    cmp word [dk_swin], 0
    je .s
    mov bx, dk_sav
    call dk_aput
.s:
    pop bx
    mov byte [dk_snz], 0
    call stik_touch
    call dk_relay
    call dk_menulive
    jmp dk_ashut

; dk_aput - the edit into the payload at BX (DKP, or dk_sav). Keeps all
dk_aput:
    push ax
    push cx
    push si
    push di
    push es
    and byte [bx + DKP_FLAGS], ~DKF_ALARM & 0FFh
    mov al, [dk_aon]
    or [bx + DKP_FLAGS], al
    push ds
    pop es
    mov si, dk_ah
    lea di, [bx + DKP_AH]
    mov cx, DKA_EDIT
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; dk_apaint - W_PAINT: dkset.inc's form paint, on this form
dk_apaint:
    push ax
    mov ax, dk_falm
    jmp dk_fpaint

; a field: its edge; inverse when it has the keys - except the label, which
; shows a caret after its text
dk_afld:
    call dk_iblk
    call dk_sent
    call dk_frame
    call dk_afstr               ; dk_line = its text
    push ax
    mov al, [di+1]
    cmp al, [dk_afoc]
    pop ax
    jne .t
    cmp byte [di+1], DKAF_LABEL
    je .t
    push ax                     ; focused: black, the text white
    inc ax
    inc bx
    sub cx, 2
    sub dx, 2
    call ck_rect
    call dk_iwht
    pop ax
.t:
    call ck_sc1
    mov dx, [di+8]
    sub dx, 8
    shr dx, 1
    add dx, [di+4]
    mov si, dk_line
    cmp byte [di+1], DKAF_LABEL
    je .lab
    mov cx, [di+6]
    shr cx, 1
    add cx, [di+2]
    jmp ck_textc
.lab:
    mov cx, [di+2]
    add cx, 4
    call ck_text                ; CX past it
    cmp byte [dk_afoc], DKAF_LABEL
    jne .r
    mov ax, cx                  ; the caret
    mov bx, dx
    dec bx
    mov cx, 1
    mov dx, 10
    jmp ck_rect
.r:
    ret

; dk_afstr - DI = a field's entry: dk_line = what it shows
dk_afstr:
    push ax
    push si
    push di
    push es
    mov al, [di+1]
    push ds
    pop es
    cld
    mov di, dk_line
    cmp al, DKAF_LABEL
    je .lab
    cmp al, DKAF_MIN
    je .min
    cmp al, DKAF_AMPM
    je .ap
    mov al, [dk_ah]             ; the hour
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .h
    call dk_ah12
.h:
    call dk_put2
    jmp short .z
.min:
    mov al, [dk_am]
    call dk_put2
    jmp short .z
.ap:
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .z                      ; no AM/PM under 24 hours
    mov ax, 'AM'
    cmp byte [dk_ah], 12
    jb .a
    mov ax, 'PM'
.a:
    stosw
    jmp short .z
.lab:
    mov si, dk_alab
    call dk_cat
.z:
    xor al, al
    stosb
    pop es
    pop di
    pop si
    pop ax
    ret

; dk_ah12 - AL = a 24-hour hour: AL = it on a 12-hour dial, 1..12
dk_ah12:
    cmp al, 12
    jb .a
    sub al, 12
.a:
    or al, al
    jnz .r
    mov al, 12
.r:
    ret

; -----------------------------------------------------------------------------
; the gestures and the keys
; -----------------------------------------------------------------------------
; dk_afire - AL = DKB_*: OK, Cancel, + and -
dk_afire:
    cmp al, DKB_OK
    je dk_aok
    cmp al, DKB_CANCEL
    je dk_ashut
    cmp al, DKB_PLUS
    mov al, 1
    je dk_astep
    mov al, -1
    jmp dk_astep

; dk_afocus - AL = the field with the keys now; the window again
dk_afocus:
    cmp al, DKAF_AMPM           ; no AM/PM to stand on under 24 hours
    jne .s
    test byte [DKP + DKP_FLAGS], DKF_H24
    jz .s
    mov al, DKAF_LABEL
.s:
    mov [dk_afoc], al
    jmp dk_fall

; dk_aampm - the hour the other side of noon
dk_aampm:
    push ax
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .x
    mov al, [dk_ah]
    add al, 12
    cmp al, 24
    jb .s
    sub al, 24
.s:
    mov [dk_ah], al
    call dk_fall
.x:
    pop ax
    ret

; dk_astep - AL = +1 / -1: the hour or the minute with the keys (the minute
; when the label has them) steps, round its dial
dk_astep:
    push ax
    push bx
    mov bx, dk_am
    mov ah, 60
    cmp byte [dk_afoc], DKAF_HOUR
    jne .s
    mov bx, dk_ah
    mov ah, 24
.s:
    add al, [bx]
    jns .p
    add al, ah
.p:
    cmp al, ah
    jb .k
    sub al, ah
.k:
    mov [bx], al
    call dk_fall
    pop bx
    pop ax
    ret

; dk_akey - W_ONKEY for the window: the keys of dk_kalm1, then - outside
; the label - dk_kalm2's, the AM/PM's A and P, and the digits; in the label,
; Backspace and a character
dk_akey:
    push ax
    push bx
    push si
    mov word [dk_fm], dk_falm
    mov si, dk_kalm1
    call dk_keys
    jnc .x
    cmp byte [dk_afoc], DKAF_LABEL
    je .lab
    mov si, dk_kalm2
    call dk_keys
    jnc .x
    cmp byte [dk_afoc], DKAF_AMPM
    jne .dig
    mov si, dk_kalm3
    call dk_keys
    jmp short .x
.dig:
    cmp al, '0'
    jb .x
    cmp al, '9'
    ja .x
    sub al, '0'
    call dk_adigit
    jmp short .x
.lab:
    xor bx, bx                  ; BX = the label's length
.ln:
    cmp byte [dk_alab + bx], 0
    je .le
    inc bx
    jmp short .ln
.le:
    cmp al, 8                   ; Backspace...
    jne .ch
    or bx, bx
    jz .rd
    dec bx
    jmp short .z
.ch:
    cmp al, ' '                 ; ...or a character
    jb .x
    cmp al, 126
    ja .x
    cmp bx, DK_LABMAX
    jae .rd
    mov [dk_alab + bx], al
    inc bx
.z:
    mov byte [dk_alab + bx], 0
.rd:
    call dk_fall
.x:
    pop si
    pop bx
    pop ax
    ret
dk_kalm1:   DKKEY 13, 0, dk_aok             ; OK
            DKKEY 27, 0, dk_ashut           ; Cancel
            DKKEY 9, 1, dk_ktab             ; Tab, Right: the next field
            DKKEY 80h + KSC_RIGHT, 1, dk_ktab
            DKKEY 80h + KSC_LEFT, -1, dk_ktab
            DKKEY 80h + KSC_UP, 1, dk_astep
            DKKEY 80h + KSC_DOWN, -1, dk_astep
            db 0
dk_kalm2:   DKKEY '+', 1, dk_astep
            DKKEY '=', 1, dk_astep
            DKKEY '-', -1, dk_astep
            DKKEY ' ', 0, dk_kaon           ; Space ticks the box
            db 0
dk_kalm3:   DKKEY 'a', 0, dk_aampm          ; on the AM/PM: A or P turns it
            DKKEY 'p', 0, dk_aampm
            db 0

; dk_ktab - AL = +1 / -1: the next field, or the one before, round the four
; - and backwards past the AM/PM under 24 hours (dk_afocus steps forwards
; past it)
dk_ktab:
    mov ah, al
    add al, [dk_afoc]
    jns .p
    mov al, DKAF_LABEL
.p:
    cmp al, DKAF_LABEL
    jbe .b
    xor al, al
.b:
    or ah, ah
    jns .f
    cmp al, DKAF_AMPM
    jne .f
    test byte [DKP + DKP_FLAGS], DKF_H24
    jz .f
    dec ax
.f:
    jmp dk_afocus

; dk_kaon - the box: the alarm on or off, in the edit
dk_kaon:
    xor byte [dk_aon], DKF_ALARM
    jmp dk_fall

; dk_adigit - AL = a digit typed into the hour or the minute: the field's
; last digit becomes its tens, the new one its units - and a value its dial
; has not got starts again from the digit
dk_adigit:
    push ax
    push bx
    push cx
    mov cl, al
    cmp byte [dk_afoc], DKAF_HOUR
    jne .m
    mov al, [dk_ah]
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .h24
    mov ch, 0                   ; 12-hour: typed on the 1..12 dial, the half
    cmp al, 12                  ; of the day kept
    jb .hk
    mov ch, 12
.hk:
    call dk_ah12
    call .shift
    cmp al, 12
    ja .h1
    or al, al
    jnz .h2
.h1:
    mov al, cl
    or al, al
    jz .x
.h2:
    cmp al, 12
    jne .h3
    xor al, al
.h3:
    add al, ch
    mov [dk_ah], al
    jmp short .d
.h24:
    call .shift
    cmp al, 24
    jb .hs
    mov al, cl
.hs:
    mov [dk_ah], al
    jmp short .d
.m:
    mov al, [dk_am]
    call .shift
    cmp al, 60
    jb .ms
    mov al, cl
.ms:
    mov [dk_am], al
.d:
    call dk_fall
.x:
    pop cx
    pop bx
    pop ax
    ret
; AL = the field: AL = its units x 10 + the typed digit (CL)
.shift:
    xor ah, ah
    mov bl, 10
    div bl
    mov al, ah
    mul bl
    add al, cl
    ret
