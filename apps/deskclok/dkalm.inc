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
; Enter is OK and Esc Cancel. Composed and blitted as the settings are.
; =============================================================================

DKA_W       equ 232             ; the content's width
DKAT_END    equ 0
DKAT_CHK    equ 1
DKAT_TEXT   equ 2
DKAT_FLD    equ 3               ; param = DKAF_*
DKAT_BTN    equ 4               ; param = DKAB_*
DKAF_HOUR   equ 0
DKAF_MIN    equ 1
DKAF_AMPM   equ 2
DKAF_LABEL  equ 3
DKAB_CANCEL equ 0
DKAB_OK     equ 1
DKAB_MINUS  equ 2
DKAB_PLUS   equ 3

dk_ageos:   dw dka_tall, dka_tall, dka_comp
dka_tall:
    dw 92
    db 0, 0
    DKSE DKAT_CHK, 0, 8, 6, 120, 12, dkas_on
    DKSE DKAT_TEXT, 0, 8, 28, 0, 0, dkas_time
    DKSE DKAT_FLD, DKAF_HOUR, 64, 24, 26, 14, 0
    DKSE DKAT_TEXT, 0, 94, 28, 0, 0, dkas_colon
    DKSE DKAT_FLD, DKAF_MIN, 102, 24, 26, 14, 0
    DKSE DKAT_FLD, DKAF_AMPM, 134, 24, 26, 14, 0
    DKSE DKAT_BTN, DKAB_MINUS, 170, 24, 24, 14, dkas_minus
    DKSE DKAT_BTN, DKAB_PLUS, 200, 24, 24, 14, dkas_plus
    DKSE DKAT_TEXT, 0, 8, 50, 0, 0, dkas_label
    DKSE DKAT_FLD, DKAF_LABEL, 64, 46, 160, 14, 0
    DKSE DKAT_BTN, DKAB_CANCEL, 96, 70, 60, 16, dkss_cancel
    DKSE DKAT_BTN, DKAB_OK, 164, 70, 60, 16, dkss_ok
    db DKAT_END
dka_comp:
    dw 66
    db 0, 0
    DKSE DKAT_CHK, 0, 8, 2, 120, 12, dkas_on
    DKSE DKAT_TEXT, 0, 8, 18, 0, 0, dkas_time
    DKSE DKAT_FLD, DKAF_HOUR, 64, 16, 26, 12, 0
    DKSE DKAT_TEXT, 0, 94, 18, 0, 0, dkas_colon
    DKSE DKAT_FLD, DKAF_MIN, 102, 16, 26, 12, 0
    DKSE DKAT_FLD, DKAF_AMPM, 134, 16, 26, 12, 0
    DKSE DKAT_BTN, DKAB_MINUS, 170, 16, 24, 12, dkas_minus
    DKSE DKAT_BTN, DKAB_PLUS, 200, 16, 24, 12, dkas_plus
    DKSE DKAT_TEXT, 0, 8, 34, 0, 0, dkas_label
    DKSE DKAT_FLD, DKAF_LABEL, 64, 32, 160, 12, 0
    DKSE DKAT_BTN, DKAB_CANCEL, 96, 50, 60, 12, dkss_cancel
    DKSE DKAT_BTN, DKAB_OK, 164, 50, 60, 12, dkss_ok
    db DKAT_END
dkas_on:    db 'Alarm on', 0
dkas_time:  db 'Time:', 0
dkas_colon: db ':', 0
dkas_label: db 'Label:', 0
dkas_minus: db '-', 0
dkas_plus:  db '+', 0
dkas_title: db 'Alarm', 0

dk_atpl:    dw 0, 0, DKA_W + 2, 0
            dw dkas_title, dk_apaint, dk_akey, dk_aclick

; -----------------------------------------------------------------------------
; dk_almopen - the alarm window: in front if it is up, else made with a copy
; of the payload's alarm. Keeps everything
; -----------------------------------------------------------------------------
dk_almopen:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, [dk_awin]
    or bx, bx
    jz .make
    call OSAPI_WM_FRONT
    jmp .x
.make:
    mov al, [DKP + DKP_FLAGS]   ; the copy
    and al, DKF_ALARM
    mov [dk_aon], al
    mov al, [DKP + DKP_AH]
    mov [dk_ah], al
    mov al, [DKP + DKP_AM]
    mov [dk_am], al
    push ds
    pop es
    mov si, DKP + DKP_LABEL
    mov di, dk_alab
    mov cx, DK_LABMAX + 1
    cld
    rep movsb
    mov byte [dk_alab + DK_LABMAX], 0
    mov byte [dk_afoc], DKAF_HOUR
    mov byte [dk_abtn], 0
    mov bx, dk_ageos
    mov al, [stik_li]
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov si, [bx]
    mov [dk_ageo], si
    mov ax, [si]
    add ax, TITLE_H + 1
    mov [dk_atpl + WT_H], ax
    mov cx, DKA_W + 2           ; beside the clock
    call dk_beside
    mov [dk_atpl + WT_X], ax
    mov word [dk_atpl + WT_Y], MBAR_H + 24
    mov si, dk_atpl
    call OSAPI_WM_CREATE
    jc .x
    mov [dk_awin], bx
    mov al, 1
    call OSAPI_WM_OWNBG
    mov al, 1
    call OSAPI_WM_KEEPH
    call OSAPI_WM_NOANIM
    mov ax, dk_aclose
    call OSAPI_WM_ONCLOSE
    mov ax, dk_amup
    call OSAPI_WM_ONMOUSEUP
    mov si, dk_menus
    call OSAPI_MENU_SET
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW
.x:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; dk_ashut - the window down. Keeps everything
dk_ashut:
    push bx
    mov bx, [dk_awin]
    or bx, bx
    jz .x
    mov word [dk_awin], 0
    call OSAPI_WM_DESTROY
    call dk_refocus
.x:
    pop bx
    ret

; dk_aclose - W_ONCLOSE: the close box is Cancel (CF = 1: gone by our hand)
dk_aclose:
    call dk_ashut
    stc
    ret

; dk_aok - the copy into the payload: saved, a snooze dropped, the clock's
; row again
dk_aok:
    push ax
    push cx
    push si
    push di
    push es
    and byte [DKP + DKP_FLAGS], ~DKF_ALARM & 0FFh
    mov al, [dk_aon]
    or [DKP + DKP_FLAGS], al
    mov al, [dk_ah]
    mov [DKP + DKP_AH], al
    mov al, [dk_am]
    mov [DKP + DKP_AM], al
    push ds
    pop es
    mov si, dk_alab
    mov di, DKP + DKP_LABEL
    mov cx, DK_LABMAX + 1
    cld
    rep movsb
    cmp word [dk_swin], 0       ; the settings' applied copy keeps it too:
    je .s                       ; a Cancel there must not undo it
    mov si, DKP + DKP_FLAGS
    mov di, dk_sav + DKP_FLAGS
    movsb
    mov si, DKP + DKP_AH
    mov di, dk_sav + DKP_AH
    mov cx, DKP_LEN - DKP_AH
    rep movsb
    and byte [dk_sav + DKP_FLAGS], ~DKF_ALARM & 0FFh
    mov al, [dk_aon]
    or [dk_sav + DKP_FLAGS], al
.s:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    mov byte [dk_snz], 0
    call stik_touch
    call dk_relay
    call dk_menulive
    jmp dk_ashut

; -----------------------------------------------------------------------------
; painting: as the settings (dk_sbody's shape), over this window's entries
; -----------------------------------------------------------------------------
dk_aorg:
    push ax
    push bx
    push dx
    mov bx, [dk_awin]
    call OSAPI_WM_CONTENT
    mov [dk_aox], ax
    mov [dk_aoy], dx
    pop dx
    pop bx
    pop ax
    ret

dk_apaint:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_DAMAGE
    call dk_abody
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_arect - content rect AX, BX .. CX, DX again (the lock held)
dk_arect:
    push ax
    push bx
    push cx
    push dx
    cmp word [dk_awin], 0
    je .x
    call dk_aorg
    add ax, [dk_aox]
    add cx, [dk_aox]
    add bx, [dk_aoy]
    add dx, [dk_aoy]
    push bx
    mov bx, [dk_awin]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x
    call dk_abody
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_aall - the content whole
dk_aall:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    xor bx, bx
    mov cx, DKA_W - 1
    mov si, [dk_ageo]
    mov dx, [si]
    dec dx
    call dk_arect
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dk_abody:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ax
    mov ax, KERNEL_SEG
    mov es, ax
    pop ax
    call dk_aorg
    push bx
    push di
    mov di, [dk_aox]                ; the content's columns
    mov bx, DKA_W
    call dk_cols
    pop di
    pop bx
    sub bx, [dk_aoy]
    jge .r0
    xor bx, bx
.r0:
    mov [dk_r0], bx
    mov si, [dk_ageo]
    mov ax, [si]
    dec ax
    sub dx, [dk_aoy]
    cmp dx, ax
    jle .r1
    mov dx, ax
.r1:
    mov [dk_r1], dx
    mov ax, [ck_cx0]
    cmp ax, [ck_cx1]
    jg .x
    mov byte [ck_np], 1         ; black and white: one plane is all of it
    call ck_gwhite
    call ck_font
    mov bx, DKA_W
    call ck_setband
    mov ax, [dk_r0]
.slab:
    cmp ax, [dk_r1]
    jg .x
    mov [ck_y0], ax
    call dk_crows
    call dk_acomp
    mov dx, [dk_r1]
    sub dx, ax
    inc dx
    cmp dx, [ck_sh]
    jbe .n
    mov dx, [ck_sh]
.n:
    mov bx, [dk_aoy]
    add bx, ax
    mov ax, [dk_aox]
    call dk_put
    mov ax, [ck_y0]
    add ax, [dk_shf]
    jmp short .slab
.x:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_acomp - slab [ck_y0]: white, every entry it reaches
dk_acomp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ck_ground
    call ck_clear
    mov word [ck_ox], 0
    mov word [ck_oy], 0
    mov di, [dk_ageo]
    add di, DKSG_E
.e:
    mov al, [di]
    cmp al, DKAT_END
    je .x
    mov bx, [di+4]
    mov dx, [di+8]
    or dx, dx
    jnz .r
    mov dx, 8
.r:
    sub bx, 3
    add dx, 6
    call ck_rows
    jc .n
    push di
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [dk_adraw + bx - 2]
    pop di
.n:
    add di, DKS_ESZ
    jmp short .e
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
dk_adraw:   dw dk_achk, dk_stext, dk_afld, dk_abtnd

dk_achk:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    mov cx, 12
    mov dx, 12
    call dk_frame
    cmp byte [dk_aon], 0
    je .l
    push ax
    push bx
    mov cx, ax
    mov dx, bx
    add dx, 2
    mov si, dk_tick12
    call ck_bits
    pop bx
    pop ax
.l:
    call ck_sc1
    mov cx, ax
    add cx, 18
    mov dx, bx
    add dx, 2
    mov si, [di+10]
    jmp ck_text

; a field: its edge; inverse when it has the keys - except the label, which
; shows a caret after its text
dk_afld:
    mov al, LC_BLK
    call ck_ink
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
    mov al, LC_WHT
    call ck_ink
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

; a button, as the settings' are, OK the default
dk_abtnd:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    push ax
    mov al, [di+1]
    inc al
    cmp al, [dk_abtn]
    pop ax
    jne .up
    call ck_rect
    push ax
    mov al, LC_WHT
    call ck_ink
    pop ax
    jmp short .lb
.up:
    call dk_frame
.lb:
    cmp byte [di+1], DKAB_OK
    jne .nd
    push ax
    push bx
    push cx
    push dx
    sub ax, 3
    sub bx, 3
    add cx, 6
    add dx, 6
    call dk_frame
    inc ax
    inc bx
    sub cx, 2
    sub dx, 2
    call dk_frame
    pop dx
    pop cx
    pop bx
    pop ax
.nd:
    call ck_sc1
    mov byte [ck_gbold], 1
    mov cx, [di+6]
    shr cx, 1
    add cx, ax
    mov dx, [di+8]
    sub dx, 8
    shr dx, 1
    add dx, bx
    mov si, [di+10]
    call ck_textc
    mov byte [ck_gbold], 0
    ret

; -----------------------------------------------------------------------------
; the gestures and the keys
; -----------------------------------------------------------------------------
; dk_ahit - CX, DX on the content: DI = the entry there, CF = 1 none
dk_ahit:
    push ax
    mov di, [dk_ageo]
    add di, DKSG_E
.e:
    mov al, [di]
    cmp al, DKAT_END
    je .no
    cmp al, DKAT_TEXT
    je .n
    cmp cx, [di+2]
    jl .n
    cmp dx, [di+4]
    jl .n
    mov ax, [di+2]
    add ax, [di+6]
    cmp cx, ax
    jge .n
    mov ax, [di+4]
    add ax, [di+8]
    cmp dx, ax
    jge .n
    clc
    jmp short .x
.n:
    add di, DKS_ESZ
    jmp short .e
.no:
    stc
.x:
    pop ax
    ret

dk_aclick:
    push ax
    push cx
    push dx
    push di
    call dk_aorg
    sub cx, [dk_aox]
    sub dx, [dk_aoy]
    call dk_ahit
    jc .x
    mov al, [di]
    cmp al, DKAT_CHK
    je .chk
    cmp al, DKAT_FLD
    je .fld
    mov al, [di+1]              ; a button: down until the release
    inc al
    mov [dk_abtn], al
    call dk_sentredraw_a
    jmp short .x
.chk:
    xor byte [dk_aon], DKF_ALARM
    call dk_aall
    jmp short .x
.fld:
    mov al, [di+1]
    cmp al, DKAF_AMPM
    jne .f
    call dk_aampm
    jmp short .x
.f:
    call dk_afocus
.x:
    pop di
    pop dx
    pop cx
    pop ax
    ret

dk_amup:
    push ax
    push cx
    push dx
    push di
    mov al, [dk_abtn]
    or al, al
    jz .x
    mov byte [dk_abtn], 0
    push ax
    call dk_aorg
    sub cx, [dk_aox]
    sub dx, [dk_aoy]
    call dk_aall                ; the button up again
    call dk_ahit
    pop ax
    jc .x
    cmp byte [di], DKAT_BTN
    jne .x
    dec al
    cmp al, [di+1]
    jne .x
    call dk_afire
.x:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; dk_afire - AL = DKAB_*
dk_afire:
    cmp al, DKAB_OK
    je dk_aok
    cmp al, DKAB_CANCEL
    je dk_ashut
    cmp al, DKAB_PLUS
    mov al, 1
    je dk_astep
    mov al, -1
    jmp dk_astep

; dk_sentredraw_a - the entry at DI again, with its ring
dk_sentredraw_a:
    push ax
    push bx
    push cx
    push dx
    call dk_sent
    sub ax, 3
    sub bx, 3
    add cx, ax
    add cx, 5
    add dx, bx
    add dx, 5
    call dk_arect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_afocus - AL = the field with the keys now; the window again
dk_afocus:
    cmp al, DKAF_AMPM           ; no AM/PM to stand on under 24 hours
    jne .s
    test byte [DKP + DKP_FLAGS], DKF_H24
    jz .s
    mov al, DKAF_LABEL
.s:
    mov [dk_afoc], al
    jmp dk_aall

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
    call dk_aall
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
    call dk_aall
    pop bx
    pop ax
    ret

; dk_akey - W_ONKEY for the window
dk_akey:
    push ax
    push bx
    mov bl, [dk_afoc]
    cmp al, 13
    je .ok
    cmp al, 27
    je .can
    cmp al, 9
    je .tab
    cmp ah, KSC_RIGHT
    je .tab
    cmp ah, KSC_LEFT
    je .btab
    cmp ah, KSC_UP
    je .up
    cmp ah, KSC_DOWN
    je .dn
    cmp bl, DKAF_LABEL
    je .lab
    cmp al, '+'
    je .up
    cmp al, '='
    je .up
    cmp al, '-'
    je .dn
    cmp al, ' '
    je .on
    cmp bl, DKAF_AMPM
    jne .dig
    or al, 20h
    cmp al, 'a'
    je .amp
    cmp al, 'p'
    je .amp
    jmp .x
.dig:
    cmp al, '0'
    jb .x
    cmp al, '9'
    ja .x
    sub al, '0'
    call dk_adigit
    jmp .x
.amp:
    call dk_aampm
    jmp .x
.on:
    xor byte [dk_aon], DKF_ALARM
    call dk_aall
    jmp .x
.ok:
    call dk_aok
    jmp .x
.can:
    call dk_ashut
    jmp .x
.tab:
    mov al, bl
    inc al
    cmp al, DKAF_LABEL + 1
    jb .t
    xor al, al
.t:
    call dk_afocus
    jmp .x
.btab:
    mov al, bl
    dec al
    jns .bt
    mov al, DKAF_LABEL
.bt:
    cmp al, DKAF_AMPM           ; backwards past the AM/PM under 24 hours
    jne .t
    test byte [DKP + DKP_FLAGS], DKF_H24
    jz .t
    dec al
    jmp short .t
.up:
    mov al, 1
    call dk_astep
    jmp short .x
.dn:
    mov al, -1
    call dk_astep
    jmp short .x
.lab:
    cmp al, 8                   ; the label: Backspace, or a character
    je .bs
    cmp al, ' '
    jb .x
    cmp al, 126
    ja .x
    push di
    mov di, dk_alab
    xor bx, bx
.ln:
    cmp byte [di+bx], 0
    je .le
    inc bx
    jmp short .ln
.le:
    cmp bx, DK_LABMAX
    jae .full
    mov [di+bx], al
    mov byte [di+bx+1], 0
.full:
    pop di
    call dk_aall
    jmp short .x
.bs:
    push di
    mov di, dk_alab
    xor bx, bx
.bn:
    cmp byte [di+bx], 0
    je .be
    inc bx
    jmp short .bn
.be:
    or bx, bx
    jz .bx
    mov byte [di+bx-1], 0
.bx:
    pop di
    call dk_aall
.x:
    pop bx
    pop ax
    ret

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
    call dk_aall
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
