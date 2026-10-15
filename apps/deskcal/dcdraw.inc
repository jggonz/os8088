; =============================================================================
; apps/deskcal/dcdraw.inc - the Desk Calendar's three layouts, its three
; cards' composers, the partial redraws, the hit tests and the entry field
; (SPEC.md 107.3, v2)
;
; EVERY CARD PIXEL IS COMPOSED, THEN ONE OSAPI_GFX_BLIT1 (SPEC.md 107.11):
; dc_compose is os88dia.inc's DIA_COMPOSE - AL = the card, BX = the slab's
; first card row, CX = its rows - and writes the card's picture into the
; 176 x 32 band with GFXE_TEXT / GFXE_BITS, black ink on the white paper
; dia_slabs (or dc_rrect) laid. Nothing is a font_run, nothing lies under a
; glyph, nothing is written twice. Each element is tested against the slab's
; rows FIRST, so a slab costs only the glyphs it holds (SPEC.md 5.12.8: a
; reject is 0.08 ms, a glyph 0.49).
;
; A change outside W_PAINT is dc_rrect: rows and BYTES of one card, composed
; a band at a time and put down as a sub-rect BLIT1 - a day's box is 16 rows
; and 3-4 bytes, a task's box 11 rows and 2 bytes - after dc_arm has armed
; OSAPI_WM_CLIP_SET once for the gesture.
; =============================================================================

; --- the layouts (os88dia.inc's DIA_LAYOUT: band, face y, tag, card,gap ...)
; and the calendar's own geometry for each, in [dia_li]'s order. Every row is
; CARD-LOCAL; every x is face-relative
DCG_BIGS    equ 0               ; db the day number's style (X3 / X2, bold)
DCG_BIGY    equ 1               ; db ...its row
DCG_DOWX    equ 2               ; db today's weekday, bold capitals: x
DCG_DOWY    equ 3               ; db ...row
DCG_MONY    equ 4               ; db today's month (`AUG 2026`): row, 0FFh none
DCG_NAVY    equ 5               ; db the nav's row
DCG_NAVL    equ 6               ; db `<`'s x
DCG_NAVR    equ 7               ; db `>`'s x
DCG_NAVM    equ 8               ; db 0: the long name centred; else the short
                                ; name's x (COMPACT)
DCG_HDRB    equ 9               ; db the header block's last row
DCG_WKY     equ 10              ; db the weekday letters' row
DCG_W0      equ 11              ; db the first week row
DCG_WP      equ 12              ; db ...the pitch
DCG_DOTY    equ 13              ; db the dot: rows below the number
DCG_DOTH    equ 14              ; db ...its rows
DCG_BOXT    equ 15              ; db the day's box: rows above the number
DCG_BOXH    equ 16              ; db ...its rows
DCG_HY      equ 17              ; db card 2's heading row
DCG_A0      equ 18              ; db agenda line 0's row
DCG_AP      equ 19              ; db ...the pitch
DCG_AN      equ 20              ; db ...lines
DCG_RULE    equ 21              ; db the rule's row (0 none)
DCG_T0      equ 22              ; db task line 0's box top
DCG_TP      equ 23              ; db ...the pitch
DCG_TN      equ 24              ; db ...lines
DCG_TX      equ 25              ; db a task's text x
DCG_TTY     equ 26              ; db ...its row below the box's top
DCG_BOX     equ 27              ; db the box's size
DCG_BAR     equ 28              ; db the selection bar: rows above the text
DCG_BARH    equ 29              ; db ...its rows
DCG_ATN     equ 30              ; db an agenda line's text, characters
DCG_TTN     equ 31              ; db a task's text, characters
DCG_BELLY   equ 32              ; db card 3: the bell's row
DCG_RX      equ 33              ; db ...the time's x
DCG_R1Y     equ 34              ; db ...the time's row
DCG_R2X     equ 35              ; db ...the text's x
DCG_R2Y     equ 36              ; db ...its row
DCG_R2N     equ 37              ; db ...its characters
DCG_LKY     equ 38              ; db ...the links' row
DCG_LKX     equ 39              ; db ...Snooze's x (the dot +56, Dismiss +72)
DCG_NONEX   equ 40              ; db ...`No reminders today`'s x
DCG_BELL    equ 41              ; dw the bell's bit record
DCG_BOXR    equ 43              ; dw the box's record (the ticked one follows)
DCG_BELLX   equ 45              ; db card 3: the bell's x
DCG_SZ      equ 46

DC_ATX      equ 56              ; an agenda line's text x
DC_PLUSX    equ 160             ; `+`
DC_LINKS    equ 72              ; Dismiss after Snooze
DC_FLD0     equ 4               ; the first day field's x
DC_FLDW     equ 24              ; a day field's width
DC_BOXW     equ 20              ; today's box / the selection's outline
DC_FX1      equ 4               ; the entry field: face x 4..155, so the `+`
DC_FX2      equ 155             ; is not under it

ROLE_MON    equ 0               ; a card's ROLE: the month,
ROLE_DAY    equ 1               ; the day (TALL / MID card 2),
ROLE_REM    equ 2               ; the reminder (card 3),
ROLE_SLOT   equ 3               ; COMPACT's paging slot

dc_lays:
    DIA_LAYOUT 436, 42, 12, 178,6, 91,6, 57,5   ; TALL, VGA: 176 x 355
    DIA_LAYOUT 304, 24, 11, 122,4, 67,4, 30,4   ; MID, Hercules and EGA: 242
    DIA_LAYOUT 156, 22, 11, 85,3, 44,3          ; COMPACT, CGA: 146

dc_geos:
    ;  BIGS                  BIGY DOWX DOWY MONY NAVY NAVL NAVR NAVM HDRB
    db GFXE_X3 | GFXE_BOLD,   7,  64,   9,  22,  44,   8, 160,   0,  40
    ;  WKY W0 WP DOTY DOTH BOXT BOXH
    db  60, 78, 16, 10,   2,   4,  16
    ;  HY A0 AP AN RULE T0 TP TN TX TTY BOX BAR BARH ATN TTN
    db  6, 22, 14, 2,  50, 57, 16, 2, 32, 2,  11,  2,  12,  14, 17
    ;  BELLY RX R1Y R2X R2Y R2N LKY LKX NONEX
    db   5,  32,  8,  32, 25,  17, 41,  32, 28
    dw dc_bell16, dc_box11
    db   8                      ; BELLX
.mid:
    db GFXE_X2 | GFXE_BOLD,   4,  48,   4,  13,  26,   8, 160,   0,  22
    db  38, 50, 12,  9,   1,   2,  12
    db  4, 16, 11, 2,  39, 42, 12, 2, 24, 1,   9,  1,  10,  14, 18
    db   3,  22,  4,  68,  4,  13, 18,  22, 26
    dw dc_bell12, dc_box9
    db   4                      ; BELLX: `21:00 Back up files` on one line
.comp:
    db GFXE_X2 | GFXE_BOLD,   2,  48,   2, 0FFh, 10,  48, 136,  64,  19
    db  20, 30,  9,  8,   1,   0,   9  ; the box r..r+8: at r-1 it took
                                        ; the dot row of the day above
    db  3, 14, 10, 3,   0, 14, 10, 3, 24, 0,   8,  1,  10,  14, 18
    db  14,  24, 14,  24, 24,  18, 34,  24, 24
    dw dc_bell8, dc_box8
    db   8
%if dc_geos.mid - dc_geos != DCG_SZ || dc_geos.comp - dc_geos.mid != DCG_SZ
  %error "deskcal: a geometry entry is DCG_SZ bytes"
%endif
; the rows each layout's elements reach, against its cards (dc_lays): card 1's
; last week box, card 2's last task box, card 3's links, the slot's third line
%if 78 + 5 * 16 + 16 - 4 > 178 || 57 + 16 + 11 > 91 || 41 + 8 > 57
  %error "deskcal: TALL's elements overrun their cards"
%endif
%if 50 + 5 * 12 + 12 - 2 > 122 || 42 + 12 + 9 > 67 || 18 + 8 > 30
  %error "deskcal: MID's elements overrun their cards"
%endif
%if 30 + 5 * 9 + 9 > 85 || 34 + 8 > 44
  %error "deskcal: COMPACT's elements overrun their cards"
%endif

; -----------------------------------------------------------------------------
; dc_org - the face's origin now (os88dia.inc's dia_org, from our window).
; Every register kept
; -----------------------------------------------------------------------------
dc_org:
    push ax
    push dx
    push si
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [dc_win]
    call dia_org
    pop es
    pop si
    pop dx
    pop ax
    ret

; dc_arm - the clip for a draw outside W_PAINT (lock held), ONCE a gesture:
; [dc_clip] says it is armed already this lock hold - every entry point
; zeroes it, because the region dies at the kernel's unlock. CF = 1 not one
; pixel of the window is visible: draw nothing, the next paint shows the
; state as it is. Every register kept
dc_arm:
    cmp byte [dc_clip], 0
    jne .ok
    push bx
    mov bx, [dc_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .r
    mov byte [dc_clip], 1
    call dc_org
.ok:
    clc
.r:
    ret

; dc_role - AL = a card: AL = its role. Every other register kept
dc_role:
    cmp byte [dia_li], 2
    jb .r                       ; TALL / MID: the card is its role
    or al, al
    jz .r
    mov al, ROLE_SLOT           ; COMPACT's second card
.r:
    ret

; -----------------------------------------------------------------------------
; dc_compose - DIA_COMPOSE (and dc_rrect's): AL = the card, BX = the band's
; first card row, CX = its rows. The card's elements on those rows, into the
; band; then a held button's light (SPEC.md 107.3: the light is part of the
; picture). Every register kept
; -----------------------------------------------------------------------------
dc_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [dc_ccard], al
    mov [dc_cr0], bx
    add cx, bx
    mov [dc_cr1], cx
    call dc_role
    mov di, [dc_geo]
    cmp al, ROLE_DAY
    je .day
    ja .rem
    call dc_cmon
    jmp short .lit
.day:
    call dc_cday
    jmp short .lit
.rem:
    cmp al, ROLE_SLOT
    je .slot
    call dc_crem
    jmp short .lit
.slot:
    call dc_cslot
.lit:
    call dc_litinv
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_vis - AX = an element's first card row, DX = past its last: CF = 1 the
; band holds none of it. Every register kept
dc_vis:
    cmp dx, [dc_cr0]
    jle .no
    cmp ax, [dc_cr1]
    jge .no
    clc
    ret
.no:
    stc
    ret

; dc_ctext - the NUL string SI at face x CX, card row DX, in style BL - only
; when the band holds a row of it. Every register kept
dc_ctext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [gfxe_tsty], bl
    mov ax, 8
    test bl, GFXE_X2
    jz .a
    mov ax, 16
.a:
    test bl, GFXE_X3
    jz .b
    mov ax, 24
.b:
    add ax, dx                  ; past its last row
    xchg ax, dx
    call dc_vis
    jc .x
    mov dx, ax
    sub dx, [dc_cr0]            ; the band row
    call gfxe_text
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_cline - dc_ctext of dc_line
dc_cline:
    push si
    mov si, dc_line
    call dc_ctext
    pop si
    ret

; dc_cfill - a rectangle of ink: AX = x, BX = card row, CX = w, DX = h, in
; [gfxe_tsty]'s sense (0 black, GFXE_INV white); gfxe_bfill clips it to the
; band. Every register kept
dc_cfill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    sub bx, [dc_cr0]
    call gfxe_bfill
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_cbits - the bit record SI at face x CX, card row DX, black. Every
; register kept
dc_cbits:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov byte [gfxe_tsty], 0
    sub dx, [dc_cr0]
    call gfxe_bits
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_litinv - the held button's rect inverted in the band, where the band
; holds rows of it and it is this card's. Every register kept
dc_litinv:
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, [dc_lit]
    or al, al
    jz .x
    dec ax
    cmp al, [dc_ccard]
    jne .x
    mov bx, [dc_ly0]
    cmp bx, [dc_cr0]
    jge .a
    mov bx, [dc_cr0]
.a:
    mov dx, [dc_ly1]
    cmp dx, [dc_cr1]
    jl .row
    mov dx, [dc_cr1]
    dec dx
.row:
    cmp bx, dx
    jg .x
    mov ax, bx
    sub ax, [dc_cr0]
    mov cx, GFXE_BAND_ST
    push dx
    mul cx
    pop dx
    mov di, ax
    mov al, [dc_lx0]
    xor ah, ah
    add di, ax
    mov cl, [dc_lx1]
    sub cl, al
    inc cl
    xor ch, ch
.b:
    xor byte [dc_band + di], 0FFh
    inc di
    loop .b
    inc bx
    jmp short .row
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the strings the composers build, in dc_line
; -----------------------------------------------------------------------------

; dc_ln0 - DI = dc_line, empty
dc_ln0:
    mov di, dc_line
    mov byte [di], 0
    ret

; dc_lnz - the NUL at DI
dc_lnz:
    mov byte [di], 0
    ret

; dc_putc - AL at DI; DI advances
dc_putc:
    mov [di], al
    inc di
    ret

; dc_puts3 - the first three characters of the NUL string SI at DI, in
; capitals when BL = 1. DI advances; the rest kept
dc_puts3:
    push ax
    push cx
    push si
    mov cx, 3
.l:
    lodsb
    or al, al
    jz .x
    or bl, bl
    jz .p
    cmp al, 'a'
    jb .p
    and al, 0DFh
.p:
    call dc_putc
    loop .l
.x:
    pop si
    pop cx
    pop ax
    ret

; dc_putsu - the NUL string SI at DI in capitals; DI advances
dc_putsu:
    push ax
    push si
.l:
    lodsb
    or al, al
    jz .x
    cmp al, 'a'
    jb .p
    and al, 0DFh
.p:
    call dc_putc
    jmp short .l
.x:
    pop si
    pop ax
    ret

; dc_putsn - at most CX characters of the NUL string SI at DI; DI advances
dc_putsn:
    push ax
    push cx
    push si
    jcxz .x
.l:
    lodsb
    or al, al
    jz .x
    call dc_putc
    loop .l
.x:
    pop si
    pop cx
    pop ax
    ret

; dc_put2z - AL (0..99) as two digits at DI, a leading zero; DI advances by
; 2, the rest kept
dc_put2z:
    push ax
    call dc_dec2
    mov [di], ax
    add di, 2
    pop ax
    ret

; dc_putn - AL (0..99) at DI, one digit or two; DI advances. Keeps the rest
dc_putn:
    cmp al, 10
    jae dc_put2z
    push ax
    add al, '0'
    mov [di], al
    inc di
    pop ax
    ret

; dc_dec2 - AL (0..99) to AL = the tens digit, AH = the ones digit (ASCII)
dc_dec2:
    push bx
    xor ah, ah
    mov bl, 10
    div bl
    add ax, '00'
    pop bx
    ret

; dc_puts - the NUL string SI at DI, without its NUL; DI advances
dc_puts:
    push ax
    push si
.l:
    lodsb
    or al, al
    jz .x
    mov [di], al
    inc di
    jmp short .l
.x:
    pop si
    pop ax
    ret

; dc_slen - AX = the length of the NUL string SI
dc_slen:
    push si
    xor ax, ax
.l:
    cmp byte [si], 0
    je .x
    inc si
    inc ax
    jmp short .l
.x:
    pop si
    ret

; dc_tfmt - the minute of the day AX as `HH:MM` at DI; DI advances by 5
dc_tfmt:
    push ax
    push bx
    mov bl, 60
    div bl                      ; AL = the hour, AH = the minute
    mov bl, ah
    call dc_put2z
    mov byte [di], ':'
    inc di
    mov al, bl
    call dc_put2z
    pop bx
    pop ax
    ret

; dc_yr4 - AX = a year, four digits at DI; DI advances. The rest kept
dc_yr4:
    push ax
    push cx
    push dx
    xor dx, dx
    mov cx, 100
    div cx
    call dc_put2z
    mov ax, dx
    call dc_put2z
    pop dx
    pop cx
    pop ax
    ret

; dc_mname - AL = a month 1..12: SI = its long name. The rest kept
dc_mname:
    push bx
    mov bl, al
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, [dc_mlong + bx]
    pop bx
    ret

; dc_dname - AL = a day of the week (0 Sunday): SI = its name. The rest kept
dc_dname:
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [dc_dlong + bx]
    pop bx
    ret

; dc_hdname - card 2's heading into dc_line: `Today` when the selected day IS
; today, else `Tue 25 Aug`; DI past it. The rest kept
dc_hdname:
    push ax
    push bx
    push si
    call dc_ln0
    call dc_istoday
    jne .other
    mov si, dc_s_today
    call dc_puts
    jmp short .x
.other:
    mov ax, [dc_y]
    mov bl, [dc_m]
    mov bh, [dc_sel]
    call dc_dow_of
    call dc_dname
    xor bl, bl
    call dc_puts3
    mov al, ' '
    call dc_putc
    mov al, [dc_sel]
    call dc_putn
    mov al, ' '
    call dc_putc
    mov al, [dc_m]
    call dc_mname
    call dc_puts3
.x:
    call dc_lnz
    pop si
    pop bx
    pop ax
    ret

; dc_istoday - ZF = 1 the selected day is today. Every register kept
dc_istoday:
    push ax
    mov ax, [dc_y]
    cmp ax, [dc_ty]
    jne .x
    mov al, [dc_m]
    cmp al, [dc_tm]
    jne .x
    mov al, [dc_sel]
    cmp al, [dc_td]
.x:
    pop ax
    ret

; -----------------------------------------------------------------------------
; card 1: the month. Today's page (the number, the weekday, the month), the
; nav for the month ON SHOW, the weekday letters, and six weeks
; -----------------------------------------------------------------------------
dc_cmon:
    mov ax, [dc_ty]             ; today's day of the week, for its name
    mov bl, [dc_tm]
    mov bh, [dc_td]
    call dc_dow_of
    mov [dc_tdow], al
    call dc_tidx
    mov [dc_tix], al
    call dc_sidx
    mov [dc_six], al
    ; --- today's number
    call dc_ln0
    mov al, [dc_td]
    call dc_putn
    call dc_lnz
    mov di, [dc_geo]            ; (dc_ln0 spent DI)
    mov bl, [di + DCG_BIGS]
    mov cx, 8
    mov dl, [di + DCG_BIGY]
    xor dh, dh
    call dc_cline
    ; --- the weekday, bold capitals
    call dc_ln0
    mov al, [dc_tdow]
    call dc_dname
    call dc_putsu
    call dc_lnz
    mov di, [dc_geo]
    mov cl, [di + DCG_DOWX]
    xor ch, ch
    mov dl, [di + DCG_DOWY]
    xor dh, dh
    mov bl, GFXE_BOLD
    call dc_cline
    ; --- today's month, `AUG 2026`
    cmp byte [di + DCG_MONY], 0FFh
    je .nav
    call dc_ln0
    mov al, [dc_tm]
    call dc_mname
    mov bl, 1
    call dc_puts3
    mov al, ' '
    call dc_putc
    mov ax, [dc_ty]
    call dc_yr4
    call dc_lnz
    mov di, [dc_geo]
    mov cl, [di + DCG_DOWX]
    xor ch, ch
    mov dl, [di + DCG_MONY]
    xor dh, dh
    xor bl, bl
    call dc_cline
.nav:
    ; --- the nav: `<`, the month on show, `>`
    mov al, [di + DCG_NAVY]
    xor ah, ah
    mov dx, ax
    add dx, 8
    call dc_vis
    jc .wk
    mov dx, ax                  ; DX = the nav's row from here
    xor bl, bl
    call dc_atfirst
    je .nl
    mov si, dc_s_lt
    mov cl, [di + DCG_NAVL]
    xor ch, ch
    call dc_ctext
.nl:
    call dc_atlast
    je .nr
    mov si, dc_s_gt
    mov cl, [di + DCG_NAVR]
    xor ch, ch
    call dc_ctext
.nr:
    push dx
    call dc_navlbl              ; dc_line = the label, CX = its x
    pop dx
    call dc_cline
.wk:
    ; --- the weekday letters, Monday first
    mov di, [dc_geo]
    mov al, [di + DCG_WKY]
    xor ah, ah
    mov dx, ax
    add dx, 8
    call dc_vis
    jc .weeks
    mov dx, ax
    mov si, dc_s_wk
    mov cx, DC_FLD0 + 8
    mov word [dc_line], 0
.wl:
    lodsb
    or al, al
    jz .weeks
    mov [dc_line], al
    call dc_cline
    add cx, DC_FLDW
    jmp short .wl
.weeks:
    ; --- six weeks
    mov byte [dc_wrow], 0
.week:
    mov di, [dc_geo]
    mov al, [dc_wrow]
    mul byte [di + DCG_WP]
    add al, [di + DCG_W0]
    adc ah, 0
    mov [dc_wr], ax             ; the week's number row
    mov dl, [di + DCG_BOXT]
    xor dh, dh
    sub ax, dx                  ; its box's first row...
    mov dl, [di + DCG_BOXH]
    add dx, ax                  ; ...and past its last
    call dc_vis
    jc .wn
    mov byte [dc_wcol], 0
.cell:
    call dc_cday1
    inc byte [dc_wcol]
    cmp byte [dc_wcol], 7
    jb .cell
.wn:
    inc byte [dc_wrow]
    cmp byte [dc_wrow], 6
    jb .week
    ret

; dc_navlbl - the nav's label into dc_line - `August 2026` centred, or
; COMPACT's `Aug 2026` at its own x - and CX = that x
dc_navlbl:
    push ax
    push bx
    push si
    push di
    call dc_ln0
    mov al, [dc_m]
    call dc_mname
    mov bx, [dc_geo]
    cmp byte [bx + DCG_NAVM], 0
    jne .short
    call dc_puts
    jmp short .y
.short:
    xor bl, bl
    call dc_puts3
.y:
    mov al, ' '
    call dc_putc
    mov ax, [dc_y]
    call dc_yr4
    call dc_lnz
    mov bx, [dc_geo]
    mov cl, [bx + DCG_NAVM]
    xor ch, ch
    or cx, cx
    jnz .x
    mov si, dc_line
    call dc_slen
    mov cl, 3
    shl ax, cl
    mov cx, DIA_FACE_W
    sub cx, ax
    shr cx, 1
.x:
    pop di
    pop si
    pop bx
    pop ax
    ret

; dc_cday1 - week [dc_wrow], column [dc_wcol]: its day's field - today's
; inverse box, the selection's outline, the number centred, the dot
dc_cday1:
    mov al, [dc_wrow]
    mov bl, 7
    mul bl
    add al, [dc_wcol]
    mov bl, al                  ; BL = the grid index
    xor bh, bh
    mov al, [dc_grid + bx]
    or al, al
    jz .r                       ; no day here
    mov [dc_wday], al
    mov al, [dc_wcol]
    mov ah, DC_FLDW
    mul ah
    add ax, DC_FLD0
    mov [dc_wfx], ax            ; the field's x
    mov di, [dc_geo]
    mov byte [dc_wink], 0       ; black on white...
    mov ax, [dc_wfx]
    add ax, 2
    mov si, bx                  ; SI = the index, for the compares
    mov bx, [dc_wr]
    mov cl, [di + DCG_BOXT]
    xor ch, ch
    sub bx, cx                  ; the box's top
    mov dl, [di + DCG_BOXH]
    xor dh, dh
    mov cx, si
    cmp cl, [dc_tix]
    jne .sel
    mov byte [gfxe_tsty], 0     ; TODAY: the box black, the number white
    mov cx, DC_BOXW
    call dc_cfill
    mov byte [dc_wink], GFXE_INV
    jmp short .num
.sel:
    cmp cl, [dc_six]
    jne .num
    mov byte [gfxe_tsty], 0     ; the SELECTED day: the box's outline
    push dx
    mov cx, DC_BOXW
    mov dx, 1
    call dc_cfill               ; top...
    pop dx
    push bx
    add bx, dx
    dec bx
    push dx
    mov dx, 1
    call dc_cfill               ; ...bottom...
    pop dx
    pop bx
    mov cx, 1
    call dc_cfill               ; ...left...
    add ax, DC_BOXW - 1
    call dc_cfill               ; ...right
.num:
    call dc_ln0
    mov al, [dc_wday]
    call dc_putn
    call dc_lnz
    mov cx, [dc_wfx]
    add cx, 8                   ; one digit at the field's middle...
    cmp byte [dc_wday], 10
    jb .one
    sub cx, 4                   ; ...two either side of it
.one:
    mov dx, [dc_wr]
    mov bl, [dc_wink]
    call dc_cline
    mov al, [dc_wday]           ; THE DOT: the day has appointments
    call dc_marked
    jz .r
    mov al, [dc_wink]
    mov [gfxe_tsty], al
    mov di, [dc_geo]
    mov ax, [dc_wfx]
    add ax, 11
    mov bx, [dc_wr]
    mov cl, [di + DCG_DOTY]
    xor ch, ch
    add bx, cx
    mov dl, [di + DCG_DOTH]
    xor dh, dh
    mov cx, 2
    call dc_cfill
.r:
    ret

; -----------------------------------------------------------------------------
; card 2: the selected day (TALL / MID) - the heading and `+`, the agenda,
; the rule, the tasks
; -----------------------------------------------------------------------------
dc_cday:
    call dc_chead
    call dc_casec
    mov di, [dc_geo]
    mov bl, [di + DCG_RULE]
    or bl, bl
    jz .t
    xor bh, bh
    mov byte [gfxe_tsty], 0
    mov ax, 8
    mov cx, 160
    mov dx, 1
    call dc_cfill
.t:
    jmp dc_ctsec

; dc_chead - the heading row: the day's name (not under an open field) and,
; on the pages that take one, `+`; COMPACT adds the slot's ` >` and names
; its page
dc_chead:
    mov di, [dc_geo]
    mov dl, [di + DCG_HY]
    xor dh, dh
    cmp byte [dc_ed], 0
    jne .plus                   ; the field is there: its own pixels
    cmp byte [dia_li], 2
    jb .day
    cmp byte [dc_pane], 1
    jb .day
    mov si, dc_s_tasksh
    je .named
    mov si, dc_s_remh
.named:
    mov di, dc_line
    call dc_puts
    jmp short .gt
.day:
    call dc_hdname
    cmp byte [dia_li], 2
    jb .put
.gt:
    mov si, dc_s_more2
    call dc_puts
    call dc_lnz
.put:
    mov cx, 8
    mov bl, GFXE_BOLD
    call dc_cline
.plus:
    cmp byte [dc_pane], 2       ; no `+` on the reminder's page
    je .r
    mov si, dc_s_plus
    mov cx, DC_PLUSX
    mov bl, GFXE_BOLD
    call dc_ctext
.r:
    ret

; dc_casec - the agenda's lines; the last one `+N more` while it pages
dc_casec:
    mov byte [dc_lsec], 0
    mov byte [dc_li], 0
.l:
    mov di, [dc_geo]
    mov al, [dc_li]
    cmp al, [di + DCG_AN]
    jae .r
    call dc_arow                ; DX = the line's row
    mov ax, dx
    mov bl, [di + DCG_BAR]
    xor bh, bh
    sub ax, bx
    push dx
    mov dl, [di + DCG_AP]
    xor dh, dh
    add dx, ax
    call dc_vis
    pop dx
    jc .n
    mov al, [dc_li]
    call dc_aslot               ; CF = 0 AL = the item; CF = 1 AH = 1 pager
    jnc .item
    or ah, ah
    jz .n
    mov cx, 8
    mov ah, [dc_acnt]
    call dc_cmore
    jmp short .n
.item:
    add al, [dc_afirst]
    call dc_caline
.n:
    inc byte [dc_li]
    jmp short .l
.r:
    ret

; dc_arow - DX = agenda line AL's row. The rest kept
dc_arow:
    push ax
    push di
    mov di, [dc_geo]
    mul byte [di + DCG_AP]
    add al, [di + DCG_A0]
    adc ah, 0
    mov dx, ax
    pop di
    pop ax
    ret

; dc_trow - DX = task line AL's box top. The rest kept
dc_trow:
    push ax
    push di
    mov di, [dc_geo]
    mul byte [di + DCG_TP]
    add al, [di + DCG_T0]
    adc ah, 0
    mov dx, ax
    pop di
    pop ax
    ret

; dc_aslot / dc_tslot - what agenda / task line AL shows: CF = 0 AL = the
; item (from the list's first); CF = 1 not an item, AH = 1 it is the PAGER.
; The pager is the last line while the list has more than its lines
dc_aslot:
    push bx
    push di
    mov di, [dc_geo]
    mov bl, [di + DCG_AN]
    mov bh, [dc_acnt]
    mov ah, [dc_atop]
    jmp short dc_slot
dc_tslot:
    push bx
    push di
    mov di, [dc_geo]
    mov bl, [di + DCG_TN]
    mov bh, [DIA_PAY + DCP_NTASK]
    mov ah, [dc_ttop]
dc_slot:                        ; BL = lines, BH = items, AH = the top
    cmp bh, bl
    jbe .item                   ; no paging
    dec bl
    cmp al, bl
    jb .item
    mov ah, 1                   ; the last line: the pager
    stc
    jmp short .x
.item:
    add al, ah
    cmp al, bh
    jae .blank
    xor ah, ah
    clc
    jmp short .x
.blank:
    xor ah, ah
    stc
.x:
    pop di
    pop bx
    ret

; dc_cmore - `+N more` at x CX, row DX: AH = the list's items, the ones not
; on show counted from [dc_li]'s page (lines - 1 items a page)
dc_cmore:
    push ax
    push bx
    push di
    call dc_pgshown             ; AL = the items this page shows
    sub ah, al
    mov al, ah
    call dc_ln0
    push ax
    mov al, '+'
    call dc_putc
    pop ax
    call dc_putn
    mov si, dc_s_more
    call dc_puts
    call dc_lnz
    xor bl, bl
    call dc_cline
    pop di
    pop bx
    pop ax
    ret

; dc_pgshown - AL = the items the page of [dc_li]'s list shows: (lines - 1)
; or what is left from its top. [dc_ccard]'s role and the slot's page say
; which list; AH = its items in, kept
dc_pgshown:
    push bx
    push di
    mov di, [dc_geo]
    mov bl, [di + DCG_AN]
    mov bh, [dc_atop]
    cmp byte [dc_lsec], 0
    je .a
    mov bl, [di + DCG_TN]
    mov bh, [dc_ttop]
.a:
    dec bl                      ; a page, the pager not counted
    mov al, ah
    sub al, bh                  ; left from the top
    cmp al, bl
    jbe .x
    mov al, bl
.x:
    pop di
    pop bx
    ret

; dc_caline - appointment AL on agenda line [dc_li] (row DX): `HH:MM text`,
; inverse when it is the selected one
dc_caline:
    call dc_aptr
    mov bl, 0
    cmp byte [dc_selk], DCS_APPT
    jne .ink
    cmp al, [dc_seli]
    jne .ink
    mov di, [dc_geo]            ; SELECTED: a black bar, white ink
    push dx
    mov bx, dx
    mov al, [di + DCG_BAR]
    xor ah, ah
    sub bx, ax
    mov dl, [di + DCG_BARH]
    xor dh, dh
    mov ax, 4
    mov cx, DIA_FACE_W - 8
    mov byte [gfxe_tsty], 0
    call dc_cfill
    pop dx
    mov bl, GFXE_INV
.ink:
    call dc_ln0
    mov ax, [si + DCA_TIME]
    cmp ax, 24 * 60
    jb .t
    push si
    mov si, dc_s_allday
    call dc_puts
    pop si
    jmp short .t1
.t:
    call dc_tfmt
.t1:
    call dc_lnz
    mov cx, 8
    call dc_cline
    call dc_ln0
    push si
    add si, DCA_TEXT
    mov di, [dc_geo]
    mov cl, [di + DCG_ATN]
    xor ch, ch
    mov di, dc_line
    call dc_putsn
    call dc_lnz
    pop si
    mov cx, DC_ATX
    jmp dc_cline

; dc_ctsec - the task lines; the last `+N more` while the list pages
dc_ctsec:
    mov byte [dc_lsec], 1
    mov byte [dc_li], 0
.l:
    mov di, [dc_geo]
    mov al, [dc_li]
    cmp al, [di + DCG_TN]
    jae .r
    call dc_trow                ; DX = the box's top
    mov ax, dx
    push dx
    mov dl, [di + DCG_TP]
    xor dh, dh
    add dx, ax
    call dc_vis
    pop dx
    jc .n
    mov al, [di + DCG_TTY]
    xor ah, ah
    mov [dc_tty], ax
    mov al, [dc_li]
    call dc_tslot
    jnc .item
    or ah, ah
    jz .n
    mov cl, [di + DCG_TX]
    xor ch, ch
    add dx, [dc_tty]
    mov ah, [DIA_PAY + DCP_NTASK]
    call dc_cmore
    jmp short .n
.item:
    call dc_ctline
.n:
    inc byte [dc_li]
    jmp short .l
.r:
    ret

; dc_ctline - task AL on its line (DX = the box's top): the box - ticked when
; DONE - and the text, inverse when it is the selected one
dc_ctline:
    call dc_tptr                ; SI = the task's record
    mov di, [dc_geo]
    push si
    mov bl, [si + DCT_FLAGS]
    mov si, [di + DCG_BOXR]     ; the box - and its TICKED twin, which
    test bl, DCT_DONE           ; follows it, for a done task
    jz .bx
    mov bl, [si]
    xor bh, bh
    shl bx, 1
    lea si, [si + bx + 1]
.bx:
    mov cx, 8
    call dc_cbits
    pop si
    add dx, [dc_tty]            ; DX = the text's row
    mov bl, 0
    cmp byte [dc_selk], DCS_TASK
    jne .ink
    cmp al, [dc_seli]
    jne .ink
    push dx                     ; SELECTED: a black bar under the text
    mov bx, dx
    mov al, [di + DCG_BAR]
    xor ah, ah
    sub bx, ax
    mov dl, [di + DCG_BARH]
    xor dh, dh
    mov al, [di + DCG_TX]
    xor ah, ah
    sub ax, 4
    mov cx, DIA_FACE_W - 4
    sub cx, ax
    mov byte [gfxe_tsty], 0
    call dc_cfill
    pop dx
    mov bl, GFXE_INV
.ink:
    call dc_ln0
    push si
    add si, DCT_TEXT
    mov di, [dc_geo]
    mov cl, [di + DCG_TTN]
    xor ch, ch
    mov di, dc_line
    call dc_putsn
    call dc_lnz
    pop si
    mov di, [dc_geo]
    mov cl, [di + DCG_TX]
    xor ch, ch
    jmp dc_cline

; -----------------------------------------------------------------------------
; card 3: the reminder - RINGING (the bell, the time, the text, `Snooze •
; Dismiss`), NEXT (no links) or NONE - as [dc_c3k] says (dcrem.inc)
; -----------------------------------------------------------------------------
dc_crem:
    mov di, [dc_geo]
    mov si, [di + DCG_BELL]
    mov cl, [di + DCG_BELLX]
    xor ch, ch
    mov dl, [di + DCG_BELLY]
    xor dh, dh
    call dc_cbits
    cmp byte [dc_c3k], DC3_NONE
    jne .some
    mov si, dc_s_norem
    mov cl, [di + DCG_NONEX]
    xor ch, ch
    mov dl, [di + DCG_R1Y]
    xor dh, dh
    xor bl, bl
    jmp dc_ctext
.some:
    call dc_ln0                 ; the time, bold
    mov ax, [dc_c3t]
    call dc_tfmt
    call dc_lnz
    mov di, [dc_geo]
    mov cl, [di + DCG_RX]
    xor ch, ch
    mov dl, [di + DCG_R1Y]
    xor dh, dh
    mov bl, GFXE_BOLD
    call dc_cline
    call dc_ln0                 ; the text
    mov al, [dc_c3a]
    call dc_aptr
    add si, DCA_TEXT
    mov di, [dc_geo]
    mov cl, [di + DCG_R2N]
    xor ch, ch
    mov di, dc_line
    call dc_putsn
    call dc_lnz
    mov di, [dc_geo]
    mov cl, [di + DCG_R2X]
    xor ch, ch
    mov dl, [di + DCG_R2Y]
    xor dh, dh
    xor bl, bl
    call dc_cline
    cmp byte [dc_c3k], DC3_RING
    jne .r
    mov dl, [di + DCG_LKY]      ; RINGING: `Snooze • Dismiss`
    xor dh, dh
    mov cl, [di + DCG_LKX]
    xor ch, ch
    mov si, dc_s_snooze
    call dc_ctext
    push cx
    mov ax, cx
    add ax, 59                  ; the dot, centred in its cell
    mov bx, dx
    add bx, 3
    push dx
    mov cx, 2
    mov dx, 2
    mov byte [gfxe_tsty], 0
    call dc_cfill
    pop dx
    pop cx
    add cx, DC_LINKS
    mov si, dc_s_dismiss
    xor bl, bl                  ; (the dot spent BL)
    call dc_ctext
.r:
    ret

; -----------------------------------------------------------------------------
; COMPACT's slot: the heading names the page; the page is the day's agenda,
; the tasks or the reminder
; -----------------------------------------------------------------------------
dc_cslot:
    call dc_chead
    cmp byte [dc_pane], 1
    jb .day
    je .tasks
    jmp dc_crem
.day:
    jmp dc_casec
.tasks:
    jmp dc_ctsec

; -----------------------------------------------------------------------------
; dc_rrect - card AL's rows BX..DX (card-local, inclusive) and bytes CL..CH
; of the face, recomposed and put down: a band of at most GFXE_BAND_H rows at
; a time, each ONE sub-rect OSAPI_GFX_BLIT1 - then the entry field over it
; when it is open and those rows reach it. dc_arm has armed the clip. A held
; button's pending un-light is settled when this covers it. Every register
; kept
; -----------------------------------------------------------------------------
dc_rrect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [dc_rcard], al
    mov [dc_rr0], bx
    mov [dc_rr1], dx
    mov [dc_rb], cx
    call dc_lpdone              ; a pending un-light this covers is settled
    call dia_ctop               ; BX = the card's face-relative top
    add bx, [dia_fy]
    mov [dc_rtop], bx
.chunk:
    mov cx, [dc_rr1]
    sub cx, [dc_rr0]
    inc cx
    cmp cx, GFXE_BAND_H
    jbe .n
    mov cx, GFXE_BAND_H
.n:
    mov [gfxe_bh], cx
    mov ax, 0FFFFh
    call gfxe_bclear            ; white paper
    mov al, [dc_rcard]
    mov bx, [dc_rr0]
    mov cx, [gfxe_bh]
    call dc_compose
    mov al, [dc_rb]             ; the bytes: x, width, the band's start
    xor ah, ah
    mov si, ax
    add si, dc_band
    mov cl, 3
    shl ax, cl
    add ax, [dia_fx]
    mov cl, [dc_rb + 1]
    sub cl, [dc_rb]
    inc cl
    xor ch, ch
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov bx, [dc_rtop]
    add bx, [dc_rr0]
    mov dx, [gfxe_bh]
    push ds
    pop es
    mov bp, GFXE_BAND_ST
    call OSAPI_GFX_BLIT1
    mov ax, [dc_rr0]
    mov dx, ax
    add dx, [gfxe_bh]
    dec dx
    mov al, [dc_rcard]
    mov bx, [dc_rr0]
    call dc_fover               ; the field over these rows, when open
    mov ax, [dc_rr0]
    add ax, [gfxe_bh]
    mov [dc_rr0], ax
    cmp ax, [dc_rr1]
    jle .chunk
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_rrows - card AL's rows BX..DX, the whole face width
dc_rrows:
    push cx
    mov cx, (DIA_FACE_W / 8 - 1) << 8
    call dc_rrect
    pop cx
    ret

; dc_rcard - card AL whole
dc_rcardw:
    push bx
    push dx
    push si
    xor bx, bx
    call dc_chgt                ; DX = its rows
    dec dx
    call dc_rrows
    pop si
    pop dx
    pop bx
    ret

; dc_chgt - DX = card AL's height. The rest kept
dc_chgt:
    push bx
    mov bx, [dia_lay]
    push ax
    xor ah, ah
    shl ax, 1
    add bx, ax
    pop ax
    mov dl, [bx + DIAL_CARDS]
    xor dh, dh
    pop bx
    ret

; dc_lpdone - a held button's PENDING un-light (dc_unlit) settled by the
; redraw dc_rrect is about to make, when it covers the button's whole rect.
; Every register kept
dc_lpdone:
    push ax
    mov al, [dc_lpend]
    or al, al
    jz .x
    dec ax
    cmp al, [dc_rcard]
    jne .x
    mov ax, [dc_rr0]
    cmp ax, [dc_ly0]
    jg .x
    mov ax, [dc_rr1]
    cmp ax, [dc_ly1]
    jl .x
    mov al, [dc_rb]
    cmp al, [dc_lx0]
    ja .x
    mov al, [dc_rb + 1]
    cmp al, [dc_lx1]
    jb .x
    mov byte [dc_lpend], 0
.x:
    pop ax
    ret

; -----------------------------------------------------------------------------
; DIA_PAINTCARD: the include's slab painter, then the field over card 2's
; heading when it is open and the damage reached it
; -----------------------------------------------------------------------------
dc_pcard:
    call dia_slabs
    push ax
    push bx
    push dx
    mov byte [dc_clip], 1       ; the kernel's clip is armed for the paint
    call dc_fover
    pop dx
    pop bx
    pop ax
    ret

; dc_fover - card AL, rows BX..DX were just drawn: the entry field over them
; when it is open there and they reach its rows. Every register kept
dc_fover:
    cmp byte [dc_ed], 0
    je .r
    cmp al, 1
    jne .r
    push ax
    push di
    mov di, [dc_geo]
    mov al, [di + DCG_HY]
    xor ah, ah
    sub ax, 3                   ; the field's first row
    cmp dx, ax
    jl .x
    add ax, 11
    cmp bx, ax
    jg .x
    call dc_fdraw
.x:
    pop di
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; the entry field (os88line.inc) in card 2's heading row: face x 4..155, the
; heading's row - 3 .. + 8
; -----------------------------------------------------------------------------

; dc_frect - the field's rect on the glass now ([dia_fx]/[dia_fy] current)
dc_frect:
    push ax
    push bx
    push di
    mov al, 1
    call dia_ctop
    add bx, [dia_fy]
    mov di, [dc_geo]
    mov al, [di + DCG_HY]
    xor ah, ah
    add ax, bx
    sub ax, 3
    mov [dc_ln + LN_Y1], ax
    add ax, 11
    mov [dc_ln + LN_Y2], ax
    mov ax, [dia_fx]
    add ax, DC_FX1
    mov [dc_ln + LN_X1], ax
    add ax, DC_FX2 - DC_FX1
    mov [dc_ln + LN_X2], ax
    pop di
    pop bx
    pop ax
    ret

; dc_fdraw - the field whole (the clip armed)
dc_fdraw:
    push si
    call dc_frect
    mov si, dc_ln
    call os88line_draw
    pop si
    ret

; dc_fhead - card 2's rows the field takes: BX..DX. The rest kept
dc_fhead:
    push ax
    push di
    mov di, [dc_geo]
    mov al, [di + DCG_HY]
    xor ah, ah
    sub ax, 3
    mov bx, ax
    add ax, 11
    mov dx, ax
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE HIT TESTS: dc_hit - card AL, CX, DX card-relative. out [dc_hk] the
; kind (DCH_*), [dc_hi] its index (a day, an item) or [dc_hb] its button
; (DCB_*), and for a button its rect - [dc_hx0]/[dc_hx1] bytes, [dc_hy0]/
; [dc_hy1] card rows - which is what lights. Every register kept
; -----------------------------------------------------------------------------
DCH_NONE    equ 0
DCH_DAY     equ 1
DCH_BTN     equ 2
DCH_ALINE   equ 3               ; an agenda line's item (an appointment)
DCH_TBOX    equ 4               ; a task's box
DCH_TTEXT   equ 5               ; a task's text
DCH_FIELD   equ 6               ; the open entry field
DCB_PREV    equ 1
DCB_NEXT    equ 2
DCB_TODAY   equ 3
DCB_PLUS    equ 4
DCB_AMORE   equ 5
DCB_TMORE   equ 6
DCB_SNOOZE  equ 7
DCB_DISMISS equ 8
DCB_PAGE    equ 9

dc_hit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [dc_hk], DCH_NONE
    mov [dc_qx], cx
    mov [dc_qy], dx
    mov di, [dc_geo]
    call dc_role
    cmp al, ROLE_DAY
    je .day
    ja .rem
    call dc_hmon
    jmp short .x
.day:
    call dc_hday
    jmp short .x
.rem:
    cmp al, ROLE_SLOT
    je .slot
    call dc_hrem
    jmp short .x
.slot:
    call dc_hday                ; the heading, `+`, the agenda or tasks page
    cmp byte [dc_pane], 2
    jne .x
    cmp byte [dc_hk], DCH_NONE
    jne .x
    call dc_hrem                ; ...or the reminder's links
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_hbtn - a button: AL = DCB_*, BL..BH its bytes, SI..DX its rows - but
; only when the press is INSIDE that rect, which is what lights: a press
; beside a button is a press on the card's margin (it activates the widget
; and does nothing else)
dc_hbtn:
    push ax
    mov ax, [dc_qy]
    cmp ax, si
    jl .no
    cmp ax, dx
    jg .no
    mov ax, [dc_qx]
    or ax, ax
    js .no
    shr ax, 1
    shr ax, 1
    shr ax, 1
    cmp al, bl
    jb .no
    cmp al, bh
    ja .no
    pop ax
    mov byte [dc_hk], DCH_BTN
    mov [dc_hb], al
    mov [dc_hx0], bl
    mov [dc_hx1], bh
    mov [dc_hy0], si
    mov [dc_hy1], dx
    ret
.no:
    pop ax
    ret

; dc_hmon - card 1 (CX, DX in it; DI the geometry)
dc_hmon:
    mov al, [di + DCG_NAVY]
    xor ah, ah
    mov si, ax
    sub si, 2                   ; the nav's light: 2 above, 1 below
    cmp byte [dia_li], 2
    jne .tall
    ; --- COMPACT: the number left, the weekday and the nav right of it
    cmp dx, 19
    jg .grid
    cmp cx, 40
    jl .today
    cmp dx, si
    jl .today                   ; the weekday's row: today
    cmp cx, 64
    jl .prev
    cmp cx, 136
    jge .next
    jmp short .label
.tall:
    cmp dx, si
    jl .today                   ; the header block: today's page
    mov ax, si
    add ax, 12
    cmp dx, ax
    jge .grid
    cmp cx, 32
    jl .prev
    cmp cx, 144
    jge .next
.label:
    call dc_navlbl              ; CX = the label's x
    mov si, dc_line
    call dc_slen
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, cx
    dec ax                      ; its last pixel...
    mov bx, cx
    mov cl, 3
    shr bx, cl                  ; ...BL its first byte...
    shr ax, cl
    mov bh, al                  ; ...BH its last
    mov al, DCB_TODAY
    jmp short .navb
.prev:
    call dc_atfirst
    je .r
    mov al, DCB_PREV
    mov bl, [di + DCG_NAVL]
    mov cl, 3
    shr bl, cl
    mov bh, bl                  ; the arrow's byte and the one before it
    dec bl
    jmp short .navb
.next:
    call dc_atlast
    je .r
    mov al, DCB_NEXT
    mov bl, [di + DCG_NAVR]
    mov cl, 3
    shr bl, cl
    mov bh, bl                  ; ...and after it
    inc bh
.navb:
    mov si, [di + DCG_NAVY]
    and si, 0FFh
    sub si, 2
    mov dx, si
    add dx, 11
    jmp dc_hbtn
.today:
    mov ah, 2                   ; the day number's own bytes light: from
    test byte [di + DCG_BIGS], GFXE_X3  ; byte 1, a digit x the scale and
    jz .t1                      ; one for the bold
    inc ah
.t1:
    mov al, ah
    cmp byte [dc_td], 10
    jb .t2
    shl al, 1
.t2:
    inc al
    mov bh, al
    mov bl, 1
    mov al, ah                  ; the rows: 8 x the scale
    mov cl, 3
    shl al, cl
    xor ah, ah
    mov si, [di + DCG_BIGY]
    and si, 0FFh
    mov dx, si
    add dx, ax
    dec dx
    mov al, DCB_TODAY
    jmp dc_hbtn
.grid:
    ; a day: the week by the box's rows, the column by the field
    mov al, [di + DCG_W0]
    sub al, [di + DCG_BOXT]
    xor ah, ah
    sub dx, ax
    jl .r
    mov ax, dx
    div byte [di + DCG_WP]
    cmp al, 6
    jae .r
    mov bl, al                  ; BL = the week
    mov ax, cx
    sub ax, DC_FLD0
    jl .r
    mov dl, DC_FLDW
    div dl
    cmp al, 7
    jae .r
    mov bh, al                  ; BH = the column
    mov al, bl
    mov bl, 7
    mul bl
    add al, bh
    mov bl, al
    xor bh, bh
    mov al, [dc_grid + bx]
    or al, al
    jz .r
    mov [dc_hi], al
    mov byte [dc_hk], DCH_DAY
.r:
    ret

; dc_hday - card 2 / the slot (CX, DX in it; DI the geometry)
dc_hday:
    cmp byte [dc_ed], 0
    je .shut
    push dx                     ; the field open: a press in it moves the
    mov ax, dx                  ; caret (deskcal.asm), and nothing else in
    call dc_fhead               ; the card acts
    cmp ax, bx
    jl .nf
    cmp ax, dx
    jg .nf
    mov byte [dc_hk], DCH_FIELD
.nf:
    pop dx
    ret
.shut:
    mov al, [di + DCG_HY]
    xor ah, ah
    sub ax, 3
    cmp dx, ax
    jl .r
    mov si, ax
    add ax, 13
    cmp dx, ax
    jge .lists
    inc si                      ; the heading row: `+` right, the page left
    mov dx, si
    add dx, 11
    cmp byte [dc_pane], 2
    je .page
    cmp cx, DC_PLUSX - 8
    jl .page
    mov al, DCB_PLUS
    mov bx, ((DIA_FACE_W / 8 - 1) << 8) | ((DC_PLUSX - 8) / 8)
    jmp dc_hbtn
.page:
    cmp byte [dia_li], 2
    jne .r                      ; TALL / MID: the heading does nothing
    mov al, DCB_PAGE
    mov bx, 0B00h               ; bytes 0..11 - `Reminder >` is 10 cells
    jmp dc_hbtn
.lists:
    cmp byte [dia_li], 2
    jne .both
    cmp byte [dc_pane], 1       ; COMPACT: the page's one list
    jb .ag
    je .tk
    ret
.both:
    call .ag
    cmp byte [dc_hk], DCH_NONE
    jne .r
.tk:
    jmp dc_htask
.ag:
    jmp dc_hagenda
.r:
    ret

; dc_hagenda - an agenda line under CX, DX
dc_hagenda:
    mov byte [dc_li], 0
.l:
    mov al, [dc_li]
    cmp al, [di + DCG_AN]
    jae .r
    call dc_arow
    mov si, dx
    mov bl, [di + DCG_BAR]
    xor bh, bh
    sub si, bx                  ; the line's rows: the bar's first...
    mov bl, [di + DCG_AP]
    add bx, si                  ; ...and the next line's
    mov ax, [dc_qy]
    cmp ax, si
    jl .n
    cmp ax, bx
    jge .n
    mov al, [dc_li]
    call dc_aslot
    jnc .item
    or ah, ah
    jz .r
    mov al, DCB_AMORE           ; the pager: its text's bytes light
    mov bx, 0A01h
    mov dx, si
    mov ah, [di + DCG_BARH]
    add dl, ah
    adc dh, 0
    dec dx
    jmp dc_hbtn
.item:
    add al, [dc_afirst]
    mov [dc_hi], al
    mov byte [dc_hk], DCH_ALINE
    ret
.n:
    inc byte [dc_li]
    jmp short .l
.r:
    ret

; dc_htask - a task line under [dc_qx], [dc_qy]
dc_htask:
    mov byte [dc_li], 0
.l:
    mov al, [dc_li]
    cmp al, [di + DCG_TN]
    jae .r
    call dc_trow                ; DX = the box's top
    mov si, dx
    mov bl, [di + DCG_TP]
    xor bh, bh
    add bx, si
    mov ax, [dc_qy]
    cmp ax, si
    jl .n
    cmp ax, bx
    jge .n
    mov al, [dc_li]
    call dc_tslot
    jnc .item
    or ah, ah
    jz .r
    mov al, DCB_TMORE
    mov bl, [di + DCG_TX]
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov bh, bl
    add bh, 9
    mov dl, [di + DCG_TTY]
    xor dh, dh
    add si, dx
    mov dl, [di + DCG_BAR]
    sub si, dx
    mov dx, si
    mov ah, [di + DCG_BARH]
    add dl, ah
    adc dh, 0
    dec dx
    jmp dc_hbtn
.item:
    mov [dc_hi], al
    mov byte [dc_hk], DCH_TTEXT
    mov ax, [dc_qx]             ; the box, a column either side
    cmp ax, 6
    jl .r
    mov bl, [di + DCG_BOX]
    xor bh, bh
    add bx, 9
    cmp ax, bx
    jg .r
    mov byte [dc_hk], DCH_TBOX
    ret
.n:
    inc byte [dc_li]
    jmp short .l
.r:
    ret

; dc_hrem - card 3 (or the slot's reminder page): the links while RINGING
dc_hrem:
    cmp byte [dc_c3k], DC3_RING
    jne .r
    mov al, [di + DCG_LKY]
    xor ah, ah
    sub ax, 2
    mov si, ax
    mov ax, [dc_qy]
    cmp ax, si
    jl .r
    mov dx, si
    add dx, 11
    cmp ax, dx
    jg .r
    mov al, [di + DCG_LKX]
    xor ah, ah
    mov bx, [dc_qx]
    sub bx, ax
    jl .r
    cmp bx, 48 + 4              ; `Snooze` and a little
    jl .snz
    sub bx, DC_LINKS
    jl .r
    cmp bx, 56 + 4
    jg .r
    mov al, DCB_DISMISS
    mov bl, [di + DCG_LKX]
    add bl, DC_LINKS
    mov cl, 3
    shr bl, cl
    mov bh, bl
    add bh, 6
    jmp dc_hbtn
.snz:
    mov al, DCB_SNOOZE
    mov bl, [di + DCG_LKX]
    mov cl, 3
    shr bl, cl
    mov bh, bl
    add bh, 5
    jmp dc_hbtn
.r:
    ret

; --- the strings and the bit records -----------------------------------------
dc_s_wk:    db 'MTWTFSS', 0     ; over each field, MONDAY first
dc_s_lt:    db '<', 0
dc_s_gt:    db '>', 0
dc_s_plus:  db '+', 0
dc_s_today: db 'Today', 0
dc_s_more:  db ' more', 0
dc_s_more2: db ' >', 0
dc_s_tasksh: db 'Tasks', 0
dc_s_remh:  db 'Reminder', 0
dc_s_allday: db '--:--', 0
dc_s_norem: db 'No reminders today', 0
dc_s_snooze: db 'Snooze', 0
dc_s_dismiss: db 'Dismiss', 0
dc_dlong:   dw dc_d0, dc_d1, dc_d2, dc_d3, dc_d4, dc_d5, dc_d6
dc_d0:      db 'Sunday', 0
dc_d1:      db 'Monday', 0
dc_d2:      db 'Tuesday', 0
dc_d3:      db 'Wednesday', 0
dc_d4:      db 'Thursday', 0
dc_d5:      db 'Friday', 0
dc_d6:      db 'Saturday', 0
dc_mlong:   dw dc_m1, dc_m2, dc_m3, dc_m4, dc_m5, dc_m6
            dw dc_m7, dc_m8, dc_m9, dc_m10, dc_m11, dc_m12
dc_m1:      db 'January', 0
dc_m2:      db 'February', 0
dc_m3:      db 'March', 0
dc_m4:      db 'April', 0
dc_m5:      db 'May', 0
dc_m6:      db 'June', 0
dc_m7:      db 'July', 0
dc_m8:      db 'August', 0
dc_m9:      db 'September', 0
dc_m10:     db 'October', 0
dc_m11:     db 'November', 0
dc_m12:     db 'December', 0

; GFXE_BITS records: db rows, then a word a row, bit 15 the leftmost pixel.
; A box is its frame; its TICKED twin follows it, the tick in 2-pixel strokes
; (SPEC.md 13.16.2.1's note: a composed tick reads on 1bpp)
dc_box11:   db 11
            dw 0FFE0h, 08020h, 08020h, 08020h, 08020h, 08020h
            dw 08020h, 08020h, 08020h, 08020h, 0FFE0h
            db 11
            dw 0FFE0h, 08020h, 080E0h, 081A0h, 0A320h, 0B620h
            dw 09C20h, 08820h, 08020h, 08020h, 0FFE0h
dc_box9:    db 9
            dw 0FF80h, 08080h, 08080h, 08080h, 08080h, 08080h
            dw 08080h, 08080h, 0FF80h
            db 9
            dw 0FF80h, 08080h, 08380h, 08680h, 0AC80h, 0B880h
            dw 09080h, 08080h, 0FF80h
dc_box8:    db 8
            dw 0FF00h, 08100h, 08100h, 08100h, 08100h, 08100h
            dw 08100h, 0FF00h
            db 8
            dw 0FF00h, 08100h, 08700h, 08D00h, 0D900h, 0B100h
            dw 08100h, 0FF00h
dc_bell16:  db 15
            dw 00180h, 007E0h, 00C30h, 01818h, 01008h, 01008h, 01008h
            dw 01008h, 0300Ch, 02004h, 06006h, 0C003h, 0FFFFh, 003C0h
            dw 00180h
dc_bell12:  db 12
            dw 00180h, 007E0h, 00C30h, 01818h, 01008h, 01008h, 0300Ch
            dw 06006h, 0C003h, 0FFFFh, 003C0h, 00180h
dc_bell8:   db 8
            dw 01800h, 03C00h, 04200h, 04200h, 04200h, 08100h, 0FF00h
            dw 01800h
