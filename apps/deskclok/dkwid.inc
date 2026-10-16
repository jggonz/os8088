; =============================================================================
; apps/deskclok/dkwid.inc - THE CLOCK ON THE DESK: the Sticky App's own window
; (SPEC.md 110.14.2), one card in os88stik.inc's widget treatment with
; STIK_VARFACE - its width and height are the settings', not a constant.
;
; THE CARD is the face, centred, and - unless `Clock face only` - the INFO
; PANEL under it: the time as text under an analog face, the date, a rule,
; and the alarm's row with its bell and the settings gear (a ringing alarm
; adds `Snooze . Dismiss`, and shows the panel even face-only). Under an
; analog face the dial overlaps the panel's top, as the mockup has it.
;
; EVERYTHING THAT IS NOT THE CLOCK IS THE DESKTOP: a round dial's corners,
; a narrow face's sides, the gap over a digital face's panel. The include's
; ground columns are laid by OSAPI_GFX_DESK before the card, so the card
; SAMPLES two of their pixels off the glass (dk_gsample) and composes the
; desktop's own pattern into the band - one blit a slab, nothing drawn
; twice, and the dither runs on with no seam (SPEC.md 110.14.4).
; =============================================================================

; the layouts the include indexes before STIK_SIZEUP points it at dk_lay: the
; largest card each screen class can be asked for
dk_lays:
    STIK_LAYOUT 255, 60, 12, 220, 4       ; TALL
    STIK_LAYOUT 255, 40, 11, 200, 3       ; MID
    STIK_LAYOUT 255, 26, 11, 140, 3       ; COMPACT

DK_SZW:     db 80, 112, 152     ; an analog face's width: Small, Medium, Large
            db 96, 128, 176     ; ...a digital one's
DK_PANW     equ 128             ; the info panel: 16 cells
DK_TITMIN   equ 96              ; the titled fallback's frame floor
DK_TITHMIN  equ 64 - TITLE_H - 1

; -----------------------------------------------------------------------------
; dk_sizeup - STIK_SIZEUP: the face, the panel and the card for the payload as
; it stands; [stik_fw], [stik_np] and [stik_lay] = dk_lay. Keeps everything
; -----------------------------------------------------------------------------
dk_sizeup:
    push ax
    push bx
    push cx
    push dx
    push si
    call dk_check               ; a payload this build did not write: defaults
    call dk_geom
    mov ax, [dk_cw]
    mov [stik_fw], ax
    mov al, [DKP + DKP_FLAGS]
    and al, DKF_ONLY
    jz .p
    mov al, 1
.p:
    mov [stik_np], al           ; face only: no name plate at rest
    mov byte [dk_lay + STIKL_N], 1
    mov bl, [stik_li]
    xor bh, bh
    mov al, [dk_ltag + bx]
    mov [dk_lay + STIKL_TAG], al
    shl bx, 1
    mov ax, [dk_lfy + bx]
    mov [dk_lay + STIKL_FY], ax
    mov ax, [dk_ch]
    mov [dk_lay + STIKL_CARDS], al
    mov bl, [stik_li]
    xor bh, bh
    mov al, [dk_lgap + bx]
    mov [dk_lay + STIKL_CARDS + 1], al
    mov word [stik_lay], dk_lay
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
dk_ltag:    db 12, 11, 11       ; TALL, MID, COMPACT: the tag row
dk_lgap:    db 4, 3, 3          ; ...the gap over it
dk_lfy:     dw 60, 40, 26       ; ...and the face's default y

; -----------------------------------------------------------------------------
; dk_geom - the card's geometry for the payload: the face (dk_faw x dk_fah at
; dk_fox), whether there is a panel and where its rows are, the card's width
; [dk_cw] and height [dk_ch]. Keeps everything
; -----------------------------------------------------------------------------
dk_geom:
    push ax
    push bx
    push cx
    push dx
    mov al, [DKP + DKP_FACE]
    mov byte [dk_kind], 0
    cmp al, DKF_ANALOG
    jb .k
    mov byte [dk_kind], 3
.k:
    mov bl, [DKP + DKP_SIZE]
    add bl, [dk_kind]
    xor bh, bh
    mov al, [DK_SZW + bx]
    xor ah, ah
    mov [dk_faw], ax
    cmp byte [dk_kind], 0
    jne .dig
    call dk_sy                  ; a dial: as tall as it is wide, to the eye
    jmp short .ev
.dig:
    mov bl, [DKP + DKP_FACE]    ; a digital face: its own share of its width
    xor bh, bh
    mov cl, [dk_fhr + bx]
    xor ch, ch
    mul cx
    mov al, ah
    mov ah, dl
    call dk_sy
.ev:
    inc ax
    and al, 0FEh                ; even: the centre lies between two rows
    mov [dk_fah], ax
    mov al, 2                   ; the big time's scale down
    xor ah, ah
    call dk_sy
    mov [dk_bsy], al
    ; --- the panel: always, unless face-only; and while ringing
    mov byte [dk_pan], 1
    test byte [DKP + DKP_FLAGS], DKF_ONLY
    jz .wid
    mov byte [dk_pan], 0
    cmp byte [dk_ring], 0
    je .wid
    mov byte [dk_pan], 2        ; 2: the alarm's rows alone
.wid:
    mov ax, [dk_faw]
    cmp byte [dk_pan], 0
    je .w1
    cmp ax, DK_PANW
    jae .w1
    mov ax, DK_PANW
.w1:
    test byte [stik_wf], STIK_W_DESK
    jnz .w2
    cmp ax, DK_TITMIN
    jae .w2
    mov ax, DK_TITMIN
.w2:
    mov [dk_cw], ax
    sub ax, [dk_faw]
    shr ax, 1
    mov [dk_fox], ax
    mov ax, [dk_fah]
    cmp byte [dk_pan], 0
    jne .pan
    mov [dk_ch], ax
    jmp .tit
.pan:
    mov ax, [dk_cw]
    sub ax, DK_PANW
    shr ax, 1
    mov [dk_pix], ax
    mov ax, 4
    call dk_sy
    mov cx, ax                  ; CX = a 4-px gap, down
    mov ax, [dk_fah]
    mov bx, ax                  ; BX = where the panel's rows start
    cmp byte [dk_kind], 0
    jne .pg
    cmp byte [dk_pan], 1
    jne .pg
    push ax                     ; under a dial in the full view: the panel's
    mov dl, 6                   ; top is a sixth of the dial up, under it
    div dl
    xor ah, ah
    mov dx, ax
    pop ax
    sub ax, dx
    mov [dk_pt], ax
    add bx, 3
    jmp short .rows
.pg:
    add ax, cx                  ; a gap of desktop over it
    mov [dk_pt], ax
    mov bx, ax
    add bx, 3
.rows:
    cmp byte [dk_pan], 2
    je .alm
    cmp byte [dk_kind], 0
    jne .dt
    mov [dk_tyT], bx            ; the time, big, under a dial
    mov al, [dk_bsy]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add bx, ax
    add bx, 3
.dt:
    mov [dk_tyD], bx            ; the date
    add bx, 8 + 3
    mov [dk_tyR], bx            ; the rule
    add bx, 1 + 3
.alm:
    mov [dk_tyA], bx            ; the alarm's row
    add bx, 9
    cmp byte [dk_ring], 0
    je .end
    add bx, 3
    mov [dk_tyS], bx            ; Snooze . Dismiss
    add bx, 8
.end:
    add bx, 3
    mov [dk_ch], bx
    sub bx, [dk_pt]
    mov [dk_ph], bx
.tit:
    test byte [stik_wf], STIK_W_DESK
    jnz .x
    cmp word [dk_ch], DK_TITHMIN
    jae .x
    mov word [dk_ch], DK_TITHMIN
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dk_pcard - STIK_PAINTCARD: AL = the card (0), BX..DX its damaged rows
; (card-local); [stik_fx], [stik_cy] the card on the glass, the clip armed.
; Each slab of the rows is composed - the ground, the panel, the face - and
; put down in ONE blit. Every register kept
; -----------------------------------------------------------------------------
dk_pcard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [dk_r0], bx
    mov [dk_r1], dx
    mov ax, KERNEL_SEG
    mov es, ax
    call dk_mode                ; [ck_np]: four planes or one
    test byte [stik_wf], STIK_W_DESK
    jz .white
    call dk_gsample             ; the desktop's pattern, off the glass
    jmp short .gnd
.white:
    call ck_gwhite
.gnd:
    call ck_font
    mov bx, [dk_cw]
    call ck_setband
    mov ax, [stik_dmg]          ; the columns the damage reaches, so a hand's
    mov cx, [stik_dmg+4]        ; box composes its own columns and no more -
    mov di, [stik_fx]           ; and puts down no more: the clip is the
    mov bx, [dk_cw]             ; window's whole visible region, not the
    call dk_cols                ; damage (dk_cols)
    mov ax, [dk_r0]
.slab:
    cmp ax, [dk_r1]
    jg .x
    mov [ck_y0], ax
    call dk_ccard               ; compose the slab
    mov dx, [dk_r1]             ; its rows that are damaged
    sub dx, ax
    inc dx
    cmp dx, [ck_sh]
    jbe .n
    mov dx, [ck_sh]
.n:
    mov bx, [stik_cy]
    add bx, ax
    mov ax, [stik_fx]
    call dk_put
    mov ax, [ck_y0]
    add ax, [ck_sh]
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

; dk_mode - [ck_np] for the window's display: four planes where BLITP will
; take the card (a colour display, kern_big, not straddling two), else one.
; ES = KERNEL_SEG. Keeps everything
dk_mode:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [ck_np], 1
    mov bx, [stik_win]
    call OSAPI_WM_DISPLAY       ; DH = its bits a pixel
    mov [dk_dbpp], dh
    cmp dh, 1
    jbe .x
    mov si, [stik_win]
    call stik_org
    mov bx, dx                  ; the PROBE (DI bit 15): every refusal decided
    mov cx, [dk_cw]             ; on the rect, nothing drawn; bit 14 - an armed
    mov dx, [dk_ch]             ; clip is walked, not refused
    mov di, 0C000h
    call OSAPI_GFX_BLITP
    jc .x
    mov byte [ck_np], 4
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_cols - AX .. CX = the damage's columns ON THE GLASS, DI = the canvas's x
; there, BX = its width: [ck_cx0] .. [ck_cx1], widened to whole bytes and cut
; to the canvas - what is composed, and all that dk_put puts down. The
; damage may be stik_redraw's 8000h .. 7FFFh, so it is cut BEFORE the
; subtraction, which would overflow. Keeps everything
dk_cols:
    push ax
    push bx
    push cx
    cmp ax, di
    jge .a0
    mov ax, di
.a0:
    sub ax, di
    cmp cx, di
    jge .c0
    mov cx, di
    dec cx                      ; wholly left of it: nothing
.c0:
    sub cx, di
.a:
    and ax, 0FFF8h
    mov [ck_cx0], ax
    dec bx
    or cx, 7
    cmp cx, bx
    jle .b
    mov cx, bx
.b:
    mov [ck_cx1], cx
    pop cx
    pop bx
    pop ax
    ret

; dk_put - the slab onto the glass: AX = the canvas's x on the glass, BX = the
; slab's y, DX = its rows; the columns [ck_cx0] .. [ck_cx1] alone (bytes:
; dk_cols). BLITP with four planes (walking the clip), BLIT1 with one. Keeps
; everything
dk_put:
    push ax
    push cx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov cx, [ck_cx1]
    sub cx, [ck_cx0]
    jl .none
    inc cx                      ; CX = the width, whole bytes
    mov si, [ck_cx0]
    add ax, si
    shr si, 1
    shr si, 1
    shr si, 1
    add si, dk_band
    mov bp, [ck_st]
    cmp byte [ck_np], 1
    je .one
    mov di, [ck_ps]
    or di, 4000h
    call OSAPI_GFX_BLITP
    jmp short .x
.one:
    call OSAPI_GFX_BLIT1
.x:
.none:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dk_gsample - the desktop's ground, read off the glass: two pixels of one of
; the include's ground columns (face x - 1, or face x + width), two rows,
; where the clip says they are ours to read. The ground is a solid colour or
; SPEC.md 26's 50% dither, so two pixels a row apart say both of its colours
; and its phase. None readable this paint: the last sample, or white.
; [stik_fx]/[stik_fy] as the paint found them; ES = KERNEL_SEG. Keeps all
; -----------------------------------------------------------------------------
dk_gsample:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dk_gcand
.c:
    cmp si, dk_gcand + 8
    jae .none
    mov ax, [stik_fx]
    cmp word [si], 0
    jne .rx
    dec ax                      ; the left column
    jmp short .y
.rx:
    add ax, [dk_cw]             ; the right
.y:
    or ax, ax
    js .n
    mov bx, [stik_fy]
    cmp word [si+2], 0
    je .t
    add bx, [dk_ch]
    sub bx, 2
.t:
    mov cx, ax
    mov dx, bx
    inc dx
    call OSAPI_WM_CLIP_TEST     ; CF = 0 every pixel of it is ours
    jnc .take
.n:
    add si, 4
    jmp short .c
.none:
    cmp byte [dk_gok], 0
    jne .keep
    call ck_gwhite
.keep:
    mov al, [stik_fy]           ; a kept sample's parity is absolute: canvas
    and al, 1                   ; row 0 is the face's top
    mov [ck_gpar], al
    jmp .x
.take:
    mov [dk_gsx], ax
    mov [dk_gsy], bx
    push es
    push ds
    pop es
    mov di, dk_gsv
    call OSAPI_GFX_SAVE         ; plane-major: plane 0's two rows, then 1..
    pop es
    jc .none
    mov al, [dk_dbpp]           ; its planes: the display's
    mov ah, 1
    cmp al, 1
    jbe .pn
    mov ah, 4
.pn:
    cmp ah, [ck_np]
    je .ok
    call ck_gwhite              ; a colour screen drawn in one plane (a
    jmp short .keep             ; straddle): no ground to speak of
.ok:
    mov cx, [dk_gsx]            ; CL = the pixel's shift in its byte
    not cl
    and cl, 7
    mov ax, [dk_gsx]
    add ax, [dk_gsy]
    and al, 1
    mov [dk_gq], al             ; the sample's top pixel's parity
    mov si, dk_gsv
    mov di, ck_gnd
    mov dl, [ck_np]
.pl:
    mov al, [si]                ; v0 = the top pixel, v1 = the one under it
    shr al, cl
    and al, 1
    mov ah, [si+1]
    shr ah, cl
    and ah, 1
    xor bx, bx                  ; BL = parity 0's byte, BH = parity 1's
    ; an even pixel of a row of parity r has (x + y) & 1 = r: v0 when that is
    ; the sample's parity, v1 when not; an odd pixel the other one
    push dx
    mov dh, [dk_gq]
    or dh, dh
    jz .q0
    xchg al, ah                 ; parity 1 is the sample's: swap the roles
.q0:
    ; AL = the colour where (x + y) & 1 = 0, AH where it is 1
    or al, al
    jz .a0
    or bx, 055AAh               ; even x on an even row, odd x on an odd one
.a0:
    or ah, ah
    jz .a1
    or bx, 0AA55h
.a1:
    pop dx
    mov [di], bx
    add si, 2
    add di, 2
    dec dl
    jnz .pl
    mov byte [dk_gok], 1
    jmp .keep
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
dk_gcand:   dw 0, 0, 1, 0       ; left top, right top...
            ; (and the bottoms: dk_gcand + 8 ends the walk early on purpose -
            ; the two tops are always on the face's own rows)

; -----------------------------------------------------------------------------
; dk_ccard - compose slab [ck_y0] of the card: the ground, the panel, the
; face. Keeps everything
; -----------------------------------------------------------------------------
dk_ccard:
    push ax
    push bx
    push cx
    push dx
    push si
    call ck_ground
    call ck_clear
    xor ax, ax
    mov [ck_ox], ax
    mov [ck_oy], ax
    cmp byte [dk_pan], 0
    je .face
    call dk_cpanel
.face:
    mov ax, [dk_fox]
    mov [ck_ox], ax
    mov word [ck_oy], 0
    mov ax, [dk_faw]
    mov [dk_fw], ax
    mov ax, [dk_fah]
    mov [dk_fh], ax
    mov bl, [DKP + DKP_FACE]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov si, [dk_faces + bx + 2]
    call dk_face
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_cpanel - the info panel, in canvas coordinates (origin 0)
dk_cpanel:
    mov al, LC_WHT
    call ck_ink
    mov ax, [dk_pix]
    mov bx, [dk_pt]
    mov cx, DK_PANW
    mov dx, [dk_ph]
    call ck_rect
    mov al, LC_BLK
    call ck_ink
    cmp byte [dk_pan], 2
    je .alarm
    cmp byte [dk_kind], 0
    jne .date
    call dk_cbig                ; the time, big, under a dial
.date:
    call ck_sc1
    call dk_sdate
    mov si, dk_line
    mov cx, [dk_pix]
    add cx, DK_PANW / 2
    mov dx, [dk_tyD]
    call ck_textc
    mov ax, [dk_pix]            ; the rule
    add ax, 4
    mov cx, [dk_pix]
    add cx, DK_PANW - 5
    mov dx, [dk_tyR]
    call ck_span
.alarm:
    call ck_sc1
    mov si, dk_bell
    mov cx, [dk_pix]
    add cx, 4
    mov dx, [dk_tyA]
    call ck_bits
    call dk_salarm              ; dk_line = what the row says
    mov si, dk_line
    mov cx, [dk_pix]
    add cx, 18
    mov dx, [dk_tyA]
    inc dx
    call ck_text
    cmp byte [dk_ring], 0
    jne .ring
    mov si, dk_gear             ; the settings gear, at the right
    mov cx, [dk_pix]
    add cx, DK_PANW - 13
    mov dx, [dk_tyA]
    call ck_bits
    ret
.ring:
    mov si, dks_snz             ; Snooze . Dismiss
    mov cx, [dk_pix]
    add cx, DK_SNZX
    mov dx, [dk_tyS]
    call ck_text
    mov ax, [dk_pix]
    add ax, DK_DOTX
    mov bx, [dk_tyS]
    add bx, 3
    mov cx, 2
    mov dx, 2
    call ck_rect
    mov si, dks_dis
    mov cx, [dk_pix]
    add cx, DK_DISX
    mov dx, [dk_tyS]
    jmp ck_text
DK_SNZX     equ 10              ; `Snooze` at the panel's x + 10...
DK_DOTX     equ DK_SNZX + 6 * 8 + 6
DK_DISX     equ DK_DOTX + 8     ; ...the dot, `Dismiss`
dks_snz:    db 'Snooze', 0
dks_dis:    db 'Dismiss', 0

; dk_cbig - the time as text, big, centred on the panel: HH:MM at x2 and -
; small after it - the seconds and the AM/PM
dk_cbig:
    call dk_stime
    mov byte [ck_gsx], 2
    mov al, [dk_bsy]
    mov [ck_gsy], al
    mov byte [ck_gbold], 1
    mov si, dk_line
    call ck_tw                  ; AX = the big part's width
    mov bx, ax
    call dk_ssmall              ; dk_small = the small part
    push bx
    call ck_sc1
    mov si, dk_small
    call ck_tw
    pop bx
    add ax, bx                  ; the whole width
    mov cx, [dk_pix]
    add cx, DK_PANW / 2
    shr ax, 1
    sub cx, ax
    mov byte [ck_gsx], 2
    mov al, [dk_bsy]
    mov [ck_gsy], al
    mov byte [ck_gbold], 1
    mov si, dk_line
    mov dx, [dk_tyT]
    call ck_text
    call ck_sc1
    mov al, [dk_bsy]            ; the small part on the big one's baseline
    dec al
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add dx, ax
    mov si, dk_small
    jmp ck_text

; dk_ssmall - dk_small = `:07`, ` PM`, both, or nothing. Keeps everything
dk_ssmall:
    push ax
    push di
    push es
    push ds
    pop es
    mov di, dk_small
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jz .am
    mov al, ':'
    stosb
    mov al, [dk_ss]
    call dk_put2
.am:
    call dk_hour
    or ah, ah
    jz .x
    mov al, ' '
    stosb
    mov al, 'A'
    cmp ah, 1
    je .a
    mov al, 'P'
.a:
    stosb
    mov al, 'M'
    stosb
.x:
    xor al, al
    stosb
    pop es
    pop di
    pop ax
    ret

; dk_salarm - dk_line = the alarm's row: `Alarm off`, `Alarm 07:30` or, while
; it rings or is snoozed, `07:30 Wake up`. Keeps everything
dk_salarm:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, dk_line
    cmp byte [dk_ring], 0
    jne .lab
    mov si, dks_alm
    call dk_cat
    test byte [DKP + DKP_FLAGS], DKF_ALARM
    jnz .t
    mov si, dks_off
    call dk_cat
    jmp short .x
.t:
    mov al, [DKP + DKP_AH]
    mov ah, [DKP + DKP_AM]
    cmp byte [dk_snz], 0
    je .at
    mov di, dk_line             ; snoozed: `Zz` and when it comes back
    mov si, dks_zz
    call dk_cat
    mov al, [dk_snh]
    mov ah, [dk_snm]
.at:
    call dk_atime
    jmp short .x
.lab:
    mov al, [DKP + DKP_AH]      ; ringing: the time and the label, as much
    mov ah, [DKP + DKP_AM]      ; of it as 13 cells hold
    call dk_atime
    mov al, ' '
    stosb
    mov si, DKP + DKP_LABEL
    mov cx, dk_line + 13
    sub cx, di
    jle .x
.l:
    lodsb
    or al, al
    jz .x
    stosb
    loop .l
.x:
    xor al, al
    stosb
    pop es
    pop di
    pop si
    pop ax
    ret
dks_alm:    db 'Alarm ', 0
dks_off:    db 'off', 0
dks_zz:     db 'Zz ', 0

; dk_atime - AL:AH = an hour and minute at DI: `07:30` (24-hour) or `7:30p`
dk_atime:
    push ax
    push bx
    mov bl, ah                  ; BL = the minute
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .h
    mov ah, 'a'
    cmp al, 12
    jb .am
    mov ah, 'p'
    sub al, 12
.am:
    or al, al
    jnz .h12
    mov al, 12
.h12:
    call dk_put1
    mov al, ':'
    stosb
    mov al, bl
    call dk_put2
    mov al, ah
    stosb
    pop bx
    pop ax
    ret
.h:
    call dk_put2
    mov al, ':'
    stosb
    mov al, bl
    call dk_put2
    pop bx
    pop ax
    ret

; dk_cat - append SI's NUL string at DI (no NUL). Keeps AX
dk_cat:
    push ax
.c:
    lodsb
    or al, al
    jz .x
    stosb
    jmp short .c
.x:
    pop ax
    ret

; ck_bits - SI = a record (db rows, then a word a row, bit 15 the leftmost)
; at CX, DX in the current ink. Keeps everything
ck_bits:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, cx
    lodsb
    xor ah, ah
    mov di, ax                  ; DI = the rows
.row:
    or di, di
    jz .x
    lodsw
    mov bx, ax
    xor cx, cx                  ; CX = the bit
.b:
    cmp cx, 16
    jae .nr
    test bx, 8000h
    jnz .run
    shl bx, 1
    inc cx
    jmp short .b
.run:
    mov ax, cx
.rr:
    shl bx, 1
    inc cx
    cmp cx, 16
    jae .re
    test bx, 8000h
    jnz .rr
.re:
    push bx
    push cx
    push dx
    add ax, bp
    add cx, bp
    dec cx
    call ck_span
    pop dx
    pop cx
    pop bx
    jmp short .b
.nr:
    inc dx
    dec di
    jmp short .row
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dk_bell:    db 9
            dw 0400h, 1F00h, 3F80h, 3F80h, 3F80h, 7FC0h, 0FFE0h, 0000h, 0E00h
dk_gear:    db 9
            dw 1C00h, 5D00h, 3E00h, 0F780h, 0E380h, 0F780h, 3E00h, 5D00h, 1C00h

; -----------------------------------------------------------------------------
; THE REDRAWS - a part of the card again, outside a paint (the lock held):
; os88stik.inc's stik_redrawr over it, which arms the clip and reaches
; dk_pcard for just its rows
; -----------------------------------------------------------------------------
; dk_redraw - the card whole
dk_redraw:
    push ax
    push bx
    push cx
    push dx
    xor ax, ax
    xor bx, bx
    mov cx, [dk_cw]
    dec cx
    mov dx, [dk_ch]
    dec dx
    call dk_rrect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_rrect - card rect AX, BX .. CX, DX (inclusive) again. Nothing when the
; clock is not on the glass. Keeps everything
dk_rrect:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ax
    mov ax, KERNEL_SEG
    mov es, ax
    pop ax
    mov si, [stik_win]
    or si, si
    jz .x
    test byte [es:si + W_FLAGS], 2
    jz .x
    test byte [stik_fl], STIK_F_QUIT
    jnz .x
    or ax, ax                   ; onto the card
    jge .a
    xor ax, ax
.a:
    or bx, bx
    jge .b
    xor bx, bx
.b:
    push ax
    mov ax, [dk_cw]
    dec ax
    cmp cx, ax
    jle .c
    mov cx, ax
.c:
    mov ax, [dk_ch]
    dec ax
    cmp dx, ax
    jle .d
    mov dx, ax
.d:
    pop ax
    cmp ax, cx
    jg .x
    cmp bx, dx
    jg .x
    push ax
    push dx
    call stik_org               ; AX, DX = the face on the glass
    mov [dk_t0], ax
    mov [dk_t1], dx
    pop dx
    pop ax
    add ax, [dk_t0]
    add cx, [dk_t0]
    add bx, [dk_t1]
    add dx, [dk_t1]
    call stik_redrawr
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_hbox - AL = a hand's angle, AH = its length f, CX = its stroke width:
; AX, BX .. CX, DX = the box it covers on the card. For a hand drawn from
; the dial's centre that box lies inside the dial, so its redraw composes
; the dial and never the ground
dk_hbox:
    push si
    push di
    push cx
    call dk_polf                ; the tip
    mov si, ax
    mov di, bx
    mov cx, [dk_cx]
    mov dx, [dk_cy]
    cmp si, cx                  ; AX..CX the columns, BX..DX the rows
    jle .x1
    xchg si, cx
.x1:
    cmp di, dx
    jle .y1
    xchg di, dx
.y1:
    mov ax, si
    mov bx, di
    pop si                      ; the stroke: its half, and one more
    shr si, 1
    inc si
    inc si
    sub ax, si
    add cx, si
    push ax
    mov ax, si
    call dk_sy
    mov si, ax
    pop ax
    sub bx, si
    add dx, si
    add ax, [dk_fox]
    add cx, [dk_fox]
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dk_hands - the three hands' boxes for the time now, into dk_hnew (the
; hour's, the minute's, the second's - 0FFFFh x1 when it is not drawn) and
; their angles into dk_hang. Keeps everything
; -----------------------------------------------------------------------------
dk_hands:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [dk_faw]
    mov [dk_fw], ax
    mov ax, [dk_fah]
    mov [dk_fh], ax
    call dk_fgeo
    mov al, [dk_hh]
    xor ah, ah
    mov bl, 12
    div bl
    mov al, ah
    mov bl, 10
    mul bl
    push ax
    mov al, [dk_mm]
    xor ah, ah
    mov bl, 6
    div bl
    pop bx
    add al, bl
    mov [dk_hang], al
    mov al, [dk_mm]
    shl al, 1
    cmp byte [dk_ss], 30
    jb .m
    inc ax
.m:
    mov [dk_hang+1], al
    mov al, [dk_ss]
    shl al, 1
    mov [dk_hang+2], al
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jnz .s
    mov byte [dk_hang+2], 0FFh
.s:
    mov di, dk_hnew
    xor si, si
.h:
    mov al, [dk_hang+si]
    cmp al, 0FFh
    jne .b
    mov word [di], 0FFFFh
    jmp short .n
.b:
    mov ah, 210                 ; the longest any face's hand reaches, the
    mov cx, [dk_R]              ; widest stroke: one box serves every face
    shr cx, 1
    shr cx, 1
    shr cx, 1
    call dk_hbox
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
.n:
    add di, 8
    inc si
    cmp si, 3
    jb .h
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_hmove - the hands that moved since the glass was drawn: their old box and
; their new one again, each. dk_hnew -> dk_hold. Keeps everything
dk_hmove:
    push ax
    push bx
    push cx
    push dx
    push si
    call dk_hands
    xor si, si
.h:
    mov al, [dk_hang + si]
    cmp al, [dk_hshown + si]
    je .n
    push si
    mov cl, 3
    shl si, cl                  ; x8
    mov ax, [dk_hold + si]
    cmp ax, 0FFFFh
    je .new
    mov bx, [dk_hold + si + 2]
    mov cx, [dk_hold + si + 4]
    mov dx, [dk_hold + si + 6]
    call dk_rrect               ; where it was...
.new:
    mov ax, [dk_hnew + si]
    cmp ax, 0FFFFh
    je .p
    mov bx, [dk_hnew + si + 2]
    mov cx, [dk_hnew + si + 4]
    mov dx, [dk_hnew + si + 6]
    call dk_rrect               ; ...and where it is
.p:
    pop si
.n:
    inc si
    cmp si, 3
    jb .h
    call dk_hkeep
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_hkeep - what is on the glass now: the new boxes and angles as the old
dk_hkeep:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, dk_hnew
    mov di, dk_hold
    mov cx, 12
    rep movsw
    mov si, dk_hang
    mov di, dk_hshown
    movsw
    movsb
    pop es
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dk_oncard - STIK_ONCARD (AL = the card, CX, DX the press card-relative, SI
; the window; lock held): the panel's controls; a DOUBLE click anywhere else
; opens the settings (on a face-only clock too); and any other press DRAGS
; the clock, as the tag and the gaps do
; -----------------------------------------------------------------------------
dk_oncard:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [dk_press], 0
    cmp byte [dk_pan], 0
    je .dbl
    call dk_panhit              ; AL = DKH_*
    or al, al
    jz .dbl
    mov [dk_press], al
    jmp short .x
.dbl:
    call OSAPI_GET_TICKS        ; the second press within 9 ticks (the
    mov bx, ax                  ; kernel's own UI_TDBLT) near the first
    sub ax, [dk_clkt]
    cmp ax, 9
    ja .first
    mov ax, cx
    sub ax, [dk_clkx]
    jns .p1
    neg ax
.p1:
    cmp ax, 4
    ja .first
    mov ax, dx
    sub ax, [dk_clky]
    jns .p2
    neg ax
.p2:
    cmp ax, 4
    ja .first
    mov word [dk_clkt], 8000h   ; a third press is a new first one
    call dk_setopen
    jmp short .x
.first:
    mov [dk_clkt], bx
    mov [dk_clkx], cx
    mov [dk_clky], dx
    test byte [stik_wf], STIK_W_DESK
    jz .x
    mov bx, si                  ; the kernel's drag, once we return
    mov al, 2
    call OSAPI_WM_DESK
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

DKH_ALARM   equ 1               ; the bell and its row: the alarm window
DKH_GEAR    equ 2               ; the gear: the settings
DKH_SNZ     equ 3
DKH_DIS     equ 4

; dk_panhit - CX, DX on the card: AL = the panel control under it, 0 none
dk_panhit:
    push bx
    push cx
    push dx
    xor al, al
    sub cx, [dk_pix]
    jl .x
    cmp cx, DK_PANW
    jge .x
    mov bx, dx
    sub bx, [dk_tyA]
    cmp bx, -2
    jl .s
    cmp bx, 10
    jg .s
    mov al, DKH_GEAR
    cmp byte [dk_ring], 0
    jne .al
    cmp cx, DK_PANW - 16
    jge .x
.al:
    mov al, DKH_ALARM
    jmp short .x
.s:
    cmp byte [dk_ring], 0
    je .x
    mov bx, dx
    sub bx, [dk_tyS]
    cmp bx, -2
    jl .x
    cmp bx, 10
    jg .x
    cmp cx, DK_SNZX - 2
    jl .x
    mov al, DKH_SNZ
    cmp cx, DK_DOTX + 3
    jl .x
    mov al, DKH_DIS
    cmp cx, DK_DISX + 7 * 8 + 2
    jl .x
    xor al, al
.x:
    pop dx
    pop cx
    pop bx
    ret

; dk_oncardup - STIK_ONCARDUP: the control the press went to, if the release
; is on it too (SPEC.md 13.5)
dk_oncardup:
    push ax
    mov ah, [dk_press]
    mov byte [dk_press], 0
    or ah, ah
    jz .x
    call dk_panhit
    cmp al, ah
    jne .x
    cmp al, DKH_ALARM
    jne .g
    call dk_almopen
    jmp short .x
.g:
    cmp al, DKH_GEAR
    jne .s
    call dk_setopen
    jmp short .x
.s:
    cmp al, DKH_SNZ
    jne .d
    call dk_snooze
    jmp short .x
.d:
    call dk_dismiss
.x:
    pop ax
    ret
