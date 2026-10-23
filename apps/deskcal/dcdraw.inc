; =============================================================================
; apps/deskcal/dcdraw.inc - the Desk Calendar's three layouts, its three
; cards' composers, the partial redraws, the hit tests and the entry field
; (SPEC.md 110.3, v2)
;
; EVERY CARD PIXEL IS COMPOSED, THEN ONE OSAPI_GFX_BLIT1 (SPEC.md 110.11):
; dc_compose is os88stik.inc's STIK_COMPOSE - AL = the card, BX = the slab's
; first card row, CX = its rows - and writes the card's picture into the
; 176 x 32 band with GFXE_TEXT / GFXE_BITS, black ink on the white paper
; stik_slabs (or dc_rrect) laid. Nothing is a font_run, nothing lies under a
; glyph, nothing is written twice. Each element is tested against the slab's
; rows FIRST, so a slab costs only the glyphs it holds (SPEC.md 5.12.8: a
; reject is 0.08 ms, a glyph 0.49).
;
; A change outside W_PAINT is dc_rrect: rows and BYTES of one card, composed
; a band at a time and put down as a sub-rect BLIT1 - a day's box is 16 rows
; and 3-4 bytes, a task's box 11 rows and 2 bytes - after dc_arm has armed
; OSAPI_WM_CLIP_SET once for the gesture.
; =============================================================================

; --- the layouts (os88stik.inc's STIK_LAYOUT: band, face y, tag, card,gap ...)
; and the calendar's own geometry for each, in [stik_li]'s order. Every row is
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
DCG_DOTY    equ 13              ; db the dot: rows below the number - or,
                                ; with DCG_DOTBY, BESIDE it at that row
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
%if DC_FLDW != 24 || (DC_FLD0 + 2) / 8 != 0 || (DC_FLD0 + 2 + DC_BOXW - 1) / 8 != 3 || (DC_FLD0 + 2 + DC_BOXW + 1) / 8 != 3
  %error "deskcal: dc_rbox's bytes are 3 x the column and 3 more"
%endif
DCG_DOTBY   equ 80h             ; DCG_DOTY's flag: the dot BESIDE the number,
                                ; 2 x 1 one pixel past its cell, and the box
                                ; DC_BOXW + 2 wide round both (COMPACT: a
                                ; 9-row pitch has no row under a number that
                                ; does not touch the next week's digits)
DC_FX1      equ 4               ; the entry field: face x 4..155, so the `+`
DC_FX2      equ 155             ; is not under it

ROLE_MON    equ 0               ; a card's ROLE: the month,
ROLE_DAY    equ 1               ; the day (TALL / MID card 2),
ROLE_REM    equ 2               ; the reminder (card 3),
ROLE_SLOT   equ 3               ; COMPACT's paging slot

dc_lays:
    STIK_LAYOUT 436, 42, 12, 178,6, 91,6, 57,5   ; TALL, VGA: 176 x 355
    STIK_LAYOUT 304, 24, 11, 122,4, 67,4, 30,4   ; MID, Hercules and EGA: 242
    STIK_LAYOUT 156, 22, 11, 85,3, 44,3          ; COMPACT, CGA: 146

dc_geos:
    ;  BIGS                  BIGY DOWX DOWY MONY NAVY NAVL NAVR NAVM HDRB
    db GFXE_X3 | GFXE_BOLD,   7,  64,   9,  22,  44,   8, 160,   0,  40
    ;  WKY W0 WP DOTY DOTH BOXT BOXH
    db  60, 78, 16,  9,   2,   4,  16   ; the dot r+9..10, the box to r+11
    ;  HY A0 AP AN RULE T0 TP TN TX TTY BOX BAR BARH ATN TTN
    db  6, 22, 14, 2,  50, 57, 16, 2, 32, 2,  11,  2,  12,  14, 17
    ;  BELLY RX R1Y R2X R2Y R2N LKY LKX NONEX
    db   5,  32,  8,  32, 25,  17, 41,  32, 28
    dw dc_bell16, dc_box11
    db   8                      ; BELLX
.mid:
    db GFXE_X2 | GFXE_BOLD,   4,  48,   4,  13,  26,   8, 160,   0,  22
    db  38, 50, 12,  8,   1,   2,  12   ; the dot r+8, the box to r+9
    db  4, 16, 11, 2,  39, 42, 12, 2, 24, 1,   9,  1,  10,  14, 18
    db   3,  22,  4,  68,  4,  13, 18,  22, 26
    dw dc_bell12, dc_box9
    db   4                      ; BELLX: `21:00 Back up files` on one line
.comp:
    db GFXE_X2 | GFXE_BOLD,   2,  48,   2, 0FFh, 10,  48, 136,  64,  19
    db  20, 30,  9, DCG_DOTBY | 3, 1, 1, 9 ; the dot BESIDE the number at
                                        ; r+3, so r+8 is blank and the box is
                                        ; r-1..r+7: a paper row under the
                                        ; digits above, an ink row over its
                                        ; own (SPEC.md 110.9)
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
; dc_arm - the clip for a draw outside W_PAINT (lock held), ONCE a gesture:
; [dc_clip] says it is armed already this lock hold - every entry point
; zeroes it, because the region dies at the kernel's unlock. Arming it takes
; the face's origin now too (os88stik.inc's stik_org, from our window). CF =
; 1 not one pixel of the window is visible: draw nothing, the next paint
; shows the state as it is. Every register kept
; -----------------------------------------------------------------------------
dc_arm:
    cmp byte [dc_clip], 0
    jne .r                      ; armed already (and CF = 0)
    push ax
    push bx
    push dx
    push si
    push es
    mov bx, [dc_win]
    call OSAPI_WM_CLIP_SET
    jc .x
    mov byte [dc_clip], 1
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, bx
    call stik_org               ; (AX, DX)
    clc
.x:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
.r:
    ret

; dc_rolej - AL = a card, BX = a table of four near routines in ROLE_*
; order: JUMPS to the card's role's, so its RET is the caller's. A card is
; its role on TALL / MID; COMPACT's second card is the SLOT. Clobbers AX, BX
dc_rolej:
    cmp byte [stik_li], 2
    jb .t                       ; TALL / MID: the card is its role
    or al, al
    jz .t
    mov al, ROLE_SLOT           ; COMPACT's second card
.t:
    cbw
    shl ax, 1
    add bx, ax
    jmp [bx]

dc_ctab:    dw dc_cmon, dc_cday, dc_crem, dc_cslot     ; by ROLE_*
dc_htab:    dw dc_hmon, dc_hday, dc_hrem, dc_hslot

; -----------------------------------------------------------------------------
; dc_compose - STIK_COMPOSE (and dc_rrect's): AL = the card, BX = the band's
; first card row, CX = its rows. The card's elements on those rows, into the
; band; then a held button's light (SPEC.md 110.3: the light is part of the
; picture) - its rect inverted, where the band holds rows of it and it is
; this card's. Every register kept
;
; dc_pop7 is the epilogue it shares with dc_ctext, dc_cfill, dc_cbits and
; dc_hit, reached by a jump: each pushes SI, AX, BX, CX, DX, DI, BP in that
; order
; -----------------------------------------------------------------------------
dc_compose:
    push si
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov [dc_ccard], al
    mov [dc_cr0], bx
    add cx, bx
    mov [dc_cr1], cx
    mov bx, dc_ctab
    call dc_rolej
    mov al, [dc_lit]            ; THE LIGHT
    dec ax                      ; (none: 0FFh, never a card)
    cmp al, [dc_ccard]
    jne dc_pop7
    mov bx, [dc_ly0]
    mov dx, [dc_cr0]
    cmp bx, dx
    jge .a
    mov bx, dx                  ; BX = the first row lit...
.a:
    mov cx, [dc_ly1]
    inc cx
    cmp cx, [dc_cr1]
    jle .b
    mov cx, [dc_cr1]            ; ...CX past the last
.b:
    sub cx, bx
    jle dc_pop7                 ; none of it in this band
    sub bx, dx                  ; the band row
    mov ax, GFXE_BAND_ST
    mul bx
    add ax, dc_band
    xchg ax, di
    mov al, [dc_lx0]
    cbw
    add di, ax                  ; DI = its first byte in the band...
    mov bl, [dc_lx1]
    sub bl, al
    inc bx                      ; ...BX its bytes (BH = 0: a band row)
    mov dx, cx                  ; DX = its rows
.row:
    mov cx, bx
    push di
.c:
    not byte [di]
    inc di
    loop .c
    pop di
    add di, GFXE_BAND_ST
    dec dx
    jnz .row
dc_pop7:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    pop si
    ret

; dc_cline - dc_ctext of dc_line
dc_cline:
    push si
    mov si, dc_line
    db 3Ch                      ; (cmp al, imm8: steps over dc_ctext's push si)

; dc_ctext - the NUL string SI at face x CX, card row DX, in style BL - only
; when the band holds a row of it. Every register kept
dc_ctext:
    push si
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov [gfxe_tsty], bl
    mov al, bl
    and ax, GFXE_X2 | GFXE_X3   ; 0, X2 or X3 - never both
    shl ax, 1
    add ax, 8                   ; the cell: 8, 16 or 24 a side
    xchg ax, bx                 ; BX = the cell
    mov ax, dx                  ; AX = its first row...
    add dx, bx                  ; ...DX past its last
    call dc_vis
    jc dc_pop7
    mov dx, [dc_cx1]
    or dx, dx
    jz .go                      ; dc_rrect's window: a string wholly
    cmp cx, dx                  ; outside the bytes it puts down is never
    jge dc_pop7                 ; composed (a day box's week, say)
    push ax
    call dc_slen                ; AX = the characters
    mul bx                      ; (DX)
    add ax, cx                  ; its last pixel (bold's one included) before
    cmp ax, [dc_cx0]            ; the window's first: none of it there
    pop ax
    jl dc_pop7
.go:
    sub ax, [dc_cr0]
    xchg ax, dx                 ; DX = the band row
    call gfxe_text
    jmp short dc_pop7

; dc_cfill - a rectangle of ink: AX = x, BX = card row, CX = w, DX = h, in
; [gfxe_tsty]'s sense (0 black, GFXE_INV white); gfxe_bfill clips it to the
; band. dc_cfill0: black. Every register kept
dc_cfill0:
    mov byte [gfxe_tsty], 0
dc_cfill:
    push si
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    sub bx, [dc_cr0]
    call gfxe_bfill
    jmp short dc_pop7

; dc_cbits - the bit record SI at face x CX, card row DX, black. Every
; register kept
dc_cbits:
    mov byte [gfxe_tsty], 0
    push si
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    sub dx, [dc_cr0]
    call gfxe_bits
    jmp short dc_pop7

; dc_vis8 - dc_vis of the 8 rows from AX: DX = AX + 8 out
dc_vis8:
    mov dx, ax
    add dx, 8
; dc_vis - AX = an element's first card row, DX = past its last (both >= 0):
; CF = 1 the band holds none of it. Every register kept
dc_vis:
    cmp ax, [dc_cr1]
    cmc                         ; CF = 1: it starts at or past the band's end
    jc .r
    cmp [dc_cr0], dx
    cmc                         ; CF = 1: it ends at or before its start
.r:
    ret

; -----------------------------------------------------------------------------
; the strings the composers build, in dc_line: each starts with DI = dc_line
; and ends with its NUL stored at DI
; -----------------------------------------------------------------------------

; dc_lnzg - the NUL at DI, then DI = the geometry
dc_lnzg:
    mov byte [di], 0
    mov di, [dc_geo]
    ret

; dc_puts3 - the first three characters of the NUL string SI at DI (CX = 3).
; dc_putsn - at most CL characters of it, in capitals when CH is not 0. DI
; advances, SI past what it took; clobbers AL and CL
dc_puts3:
    mov cx, 3
dc_putsn:
.l:
    sub cl, 1
    jc .x
    lodsb
    or al, al
    jz .x
    or ch, ch
    jz .p
    cmp al, 'a'
    jb .p
    and al, 0DFh
.p:
    mov [di], al
    inc di
    jmp short .l
.x:
    ret

; dc_put2z - AL (0..99) as two digits at DI, a leading zero; DI advances by
; 2. Clobbers AX
dc_put2z:
    call dc_dec2
    mov [di], ax
    inc di
    inc di
    ret

; dc_putn - AL (0..99) at DI, one digit or two; DI advances. Clobbers AX
dc_putn:
    cmp al, 10
    jae dc_put2z
    add al, '0'
; dc_putc - AL at DI; DI advances
dc_putc:
    mov [di], al
    inc di
    ret

; dc_dec2 - AL (0..99) to AL = the tens digit, AH = the ones digit (ASCII).
; Every other register kept
dc_dec2:
    aam                         ; AH = the tens, AL = the ones
    xchg al, ah
    add ax, '00'
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

; dc_slen - AX = the length of the NUL string SI. Every other register kept
dc_slen:
    push si
.l:
    lodsb
    or al, al
    jnz .l
    xchg ax, si                 ; past its NUL
    pop si
    sub ax, si
    dec ax
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

; dc_yr4 - AX = a year (< 25600), four digits at DI; DI advances. Clobbers
; AX, CL
dc_yr4:
    mov cl, 100
    div cl                      ; AL = the century, AH = the year in it
    mov cl, ah
    call dc_put2z
    mov al, cl
    jmp short dc_put2z

; dc_mname - AL = a month 1..12: SI = its long name (dc_mlong follows
; dc_dlong's seven words, so month m is entry m + 6). Clobbers AX
dc_mname:
    add al, 6
; dc_dname - AL = a day of the week (0 Sunday): SI = its name. Clobbers AX
dc_dname:
    cbw
    shl ax, 1
    xchg ax, si
    mov si, [dc_dlong + si]
    ret

; dc_hdname - card 2's heading into dc_line: `Today` when the selected day IS
; today, else `Tue 25 Aug`; DI past it, its NUL stored, and BL = its style -
; `Today` alone is bold: a date's `0` fills in at x1 bold (SPEC.md 110.3).
; Clobbers AX, BX, CX, SI; DX kept
dc_hdname:
    mov di, dc_line
    mov si, dc_s_today
    mov cx, 0FFh                ; `Today` whole...
    mov ax, [dc_y]
    mov bx, [dc_m]              ; BL = the month, BH = the selected day
    cmp ax, [dc_ty]
    jne .other
    cmp bx, [dc_tm]             ; (and [dc_td])
    jne .other
    mov bl, GFXE_BOLD           ; ...and bold
    jmp short .p
.other:
    call dc_dow_of
    call dc_dname
    call dc_puts3
    mov al, ' '
    call dc_putc
    mov al, [dc_sel]
    call dc_putn
    mov al, ' '
    call dc_putc
    mov al, [dc_m]
    call dc_mname
    mov cl, 3                   ; (CH = 0)
    xor bl, bl
.p:
    call dc_putsn
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; card 1: the month. Today's page (the number, the weekday, the month), the
; nav for the month ON SHOW, the weekday letters, and six weeks
; -----------------------------------------------------------------------------
dc_cmon:
    mov ax, [dc_ty]             ; today's day of the week, for its name
    mov bx, [dc_tm]             ; (BL = its month, BH = its day)
    call dc_dow_of
    mov [dc_tdow], al
    call dc_tidx
    mov [dc_tix], al
    call dc_sidx
    mov [dc_six], al
    ; --- today's number
    mov di, dc_line
    mov al, [dc_td]
    call dc_putn
    call dc_lnzg
    mov bl, [di + DCG_BIGS]
    mov cx, 8
    mov dl, [di + DCG_BIGY]
    xor dh, dh
    call dc_cline
    ; --- the weekday, bold capitals
    mov di, dc_line
    mov al, [dc_tdow]
    call dc_dname
    mov cx, 0FFFFh              ; whole, in capitals
    call dc_putsn
    call dc_lnzg
    mov cl, [di + DCG_DOWX]
    xor ch, ch
    mov dl, [di + DCG_DOWY]     ; (DH = 0 from here: DX is a row)
    xor bl, bl                  ; regular: x1 bold fuses MONDAY (SPEC.md 110.3)
    call dc_cline
    ; --- today's month, `AUG 2026`
    cmp byte [di + DCG_MONY], 0FFh
    je .nav
    push cx                     ; (CX = DCG_DOWX: the same x)
    mov di, dc_line
    mov al, [dc_tm]
    call dc_mname
    mov cx, 0103h               ; its first three, in capitals
    call dc_putsn
    mov al, ' '
    call dc_putc
    mov ax, [dc_ty]
    call dc_yr4
    call dc_lnzg
    pop cx
    mov dl, [di + DCG_MONY]     ; (BL still 0)
    call dc_cline
.nav:
    ; --- the nav: `<`, the month on show, `>` (DI the geometry, BL 0, CH 0)
    mov al, [di + DCG_NAVY]
    cbw
    call dc_vis8
    jc .wk
    mov dx, ax                  ; DX = the nav's row from here
    call dc_atfirst
    je .nl
    mov si, dc_s_lt
    mov cl, [di + DCG_NAVL]
    call dc_ctext
.nl:
    call dc_atlast
    je .nr
    mov si, dc_s_gt
    mov cl, [di + DCG_NAVR]
    call dc_ctext
.nr:
    call dc_navlbl              ; dc_line = the label, CX = its x
    call dc_cline
.wk:
    ; --- the weekday letters, Monday first (DI the geometry, BL 0)
    mov al, [di + DCG_WKY]
    cbw
    call dc_vis8
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
    ; --- six weeks (every row is < 256: the asserts after dc_geos)
    mov byte [dc_wrow], 0
.week:
    mov di, [dc_geo]
    mov al, [dc_wrow]
    mul byte [di + DCG_WP]
    add al, [di + DCG_W0]
    mov [dc_wr], ax             ; the week's number row
    sub al, [di + DCG_BOXT]     ; its box's first row...
    mov dx, ax
    add dl, [di + DCG_BOXH]     ; ...and past its last
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
; COMPACT's `Aug 2026` at its own x - and CX = that x. DI = the geometry in;
; BX, DX, DI kept, AX and SI clobbered
dc_navlbl:
    push bx
    push di
    mov bx, di                  ; BX = the geometry
    mov di, dc_line
    mov al, [dc_m]
    call dc_mname
    cmp byte [bx + DCG_NAVM], 0
    jne .short
    call dc_puts
    jmp short .y
.short:
    call dc_puts3
.y:
    mov al, ' '
    call dc_putc
    mov ax, [dc_y]
    call dc_yr4
    mov byte [di], 0
    mov cl, [bx + DCG_NAVM]
    xor ch, ch
    inc cx
    loop .x                     ; COMPACT: its own x
    mov ax, di
    sub ax, dc_line             ; the characters
    shl ax, 1
    shl ax, 1
    mov cx, STIK_FACE_W / 2
    sub cx, ax                  ; centred: (176 - 8 x n) / 2
.x:
    pop di
    pop bx
    ret

; dc_cday1 - week [dc_wrow], column [dc_wcol]: its day's field - today's
; inverse box, the selection's outline, the number centred, the dot.
; Clobbers everything but [dc_wrow]/[dc_wcol]
dc_cday1:
    mov al, [dc_wrow]
    mov ah, 7
    mul ah
    add al, [dc_wcol]
    xchg ax, bx                 ; BX = the grid index
    mov al, [dc_grid + bx]
    or al, al
    jnz .day
    ret                         ; no day here
.day:
    mov [dc_wday], al
    mov si, [dc_geo]            ; SI = the geometry
    mov al, [dc_wcol]
    mov ah, DC_FLDW
    mul ah
    add al, DC_FLD0
    mov [dc_wfx], ax            ; the field's x
    mov byte [dc_wink], 0       ; black on white...
    inc ax
    inc ax                      ; AX = the box's x
    mov bp, DC_BOXW             ; BP = the box's width, to the outline
    test byte [si + DCG_DOTY], DCG_DOTBY
    jz .bx
    inc bp                      ; COMPACT: 22, round the number AND its dot
    inc bp                      ; with a paper column before the edge
.bx:
    mov cx, bx                  ; CX = the index, for the compares
    mov bx, [dc_wr]
    sub bl, [si + DCG_BOXT]     ; the box's top
    mov dl, [si + DCG_BOXH]     ; (DH = 0: DX is a row on every entry)
    cmp cl, [dc_tix]
    je .today
    cmp cl, [dc_six]
    jne .num
    mov cx, bp                  ; the SELECTED day: the box's outline - the
    call dc_cfill0              ; box black, then its inside white again
    inc ax                      ; (it is paper there: nothing in the band
    inc bx                      ; reaches inside a box but its own number
    dec cx                      ; and dot, which come after)
    dec cx
    dec dx
    dec dx
    mov byte [gfxe_tsty], GFXE_INV
    call dc_cfill
    jmp short .num
.today:
    mov cx, bp                  ; TODAY: the box black, the number white
    call dc_cfill0
    mov byte [dc_wink], GFXE_INV
.num:
    mov cx, [dc_wfx]
    add cx, 8                   ; one digit at the field's middle...
    mov al, [dc_wday]
    cmp al, 10
    jb .one
    sub cx, 4                   ; ...two either side of it
.one:
    mov di, dc_line
    call dc_putn                ; (DI = dc_line + its digits from here)
    mov byte [di], 0
    mov dx, [dc_wr]
    mov bl, [dc_wink]
    call dc_cline
    mov al, [dc_wday]           ; THE DOT: the day has appointments
    call dc_marked
    jz .r
    mov al, [dc_wink]
    mov [gfxe_tsty], al
    mov ax, [dc_wfx]
    add ax, 11                  ; UNDER: centred in the field
    mov bx, dx                  ; (DX = [dc_wr])
    mov cl, [si + DCG_DOTY]     ; (CH = 0: CX is an x)
    or cl, cl
    jns .under
    and cl, ~DCG_DOTBY & 0FFh   ; BESIDE: one pixel past the number's cell,
    add ax, 16 - 11             ; whose 8th column is the font's blank one -
    cmp di, dc_line + 2         ; field + 16 past one digit...
    jb .under
    add ax, 4                   ; ...+ 20 past two
.under:
    add bx, cx
    mov dl, [si + DCG_DOTH]     ; (DH = 0: DX was a row)
    mov cx, 2
    jmp dc_cfill
.r:
    ret

; -----------------------------------------------------------------------------
; card 2: the selected day (TALL / MID) - the heading and `+`, the agenda,
; the rule, the tasks
; -----------------------------------------------------------------------------
dc_cday:
    call dc_chead
    call dc_casec               ; (DI = the geometry out)
    mov bl, [di + DCG_RULE]
    xor bh, bh
    or bl, bl
    jz dc_ctsec
    mov ax, 8
    mov cx, 160
    mov dx, 1
    call dc_cfill0

; dc_ctsec - the task lines; the last `+N more` while the list pages
dc_ctsec:
    mov word [dc_li], 0100h     ; line 0, of the tasks ([dc_lsec] = 1)
.l:
    mov di, [dc_geo]
    mov al, [dc_li]
    cmp al, [di + DCG_TN]
    jae .r
    call dc_trow                ; DX = the box's top
    mov ax, dx
    push dx
    add dl, [di + DCG_TP]       ; (rows < 256)
    call dc_vis
    pop dx
    jc .n
    mov al, [di + DCG_TTY]
    cbw
    mov [dc_tty], ax
    mov al, [dc_li]
    call dc_tslot
    jnc .item
    or ah, ah
    jz .n
    mov cl, [di + DCG_TX]
    xor ch, ch
    add dx, [dc_tty]
    mov ah, [STIK_PAY + DCP_NTASK]
    call dc_cmore
    jmp short .n
.item:
    call dc_ctline
.n:
    inc byte [dc_li]
    jmp short .l
.r:
    ret

; dc_ctline - task AL on its line (DX = the box's top, DI = the geometry):
; the box - ticked when DONE - and the text, inverse when it is the selected
; one
dc_ctline:
    call dc_tptr                ; SI = the task's record
    push si
    test byte [si + DCT_FLAGS], DCT_DONE
    mov si, [di + DCG_BOXR]     ; the box - and its TICKED twin, which
    jz .bx                      ; follows it, for a done task
    mov bl, [si]
    xor bh, bh
    shl bx, 1
    lea si, [si + bx + 1]
.bx:
    mov cx, 8
    call dc_cbits
    pop si
    add dx, [dc_tty]            ; DX = the text's row
    mov bl, [di + DCG_TX]
    xor bh, bh
    lea bp, [bx - 4]            ; the bar from 4 before the text
    mov ah, DCS_TASK
    call dc_csel                ; BL = the ink
    mov cl, [di + DCG_TTN]      ; (CH = 0)
    mov di, dc_line
    add si, DCT_TEXT
    call dc_putsn
    mov byte [di], 0
    lea cx, [bp + 4]            ; the text's x
    jmp dc_cline

; dc_csel - AL = an item of kind AH (DCS_*), DX = its text's row, BP = its
; selection bar's x, DI = the geometry: BL = 0 when it is not the selected
; item; when it is, its bar black - BP to face x 172, from DCG_BAR rows over
; the text, DCG_BARH tall - and BL = GFXE_INV. Clobbers AX, BX, CX
dc_csel:
    mov bl, 0
    cmp ah, [dc_selk]
    jne .r
    cmp al, [dc_seli]
    jne .r
    push dx
    mov bx, dx
    sub bl, [di + DCG_BAR]
    mov dl, [di + DCG_BARH]     ; (DH = 0: DX is a row)
    mov ax, bp
    mov cx, STIK_FACE_W - 4
    sub cx, ax
    call dc_cfill0
    pop dx
    mov bl, GFXE_INV
.r:
    ret

; dc_chead - the heading row: the day's name (not under an open field) and,
; on the pages that take one, `+`; COMPACT adds the slot's ` >` and names
; its page
dc_chead:
    mov di, [dc_geo]
    mov dl, [di + DCG_HY]
    xor dh, dh
    cmp byte [dc_ed], 0
    jne .plus                   ; the field is there: its own pixels
    cmp byte [stik_li], 2
    jb .day
    cmp byte [dc_pane], 1
    jb .day
    mov si, dc_s_tasksh
    je .named
    mov si, dc_s_remh
.named:
    mov di, dc_line
    call dc_puts
    xor bl, bl                  ; a page's name: regular
    jmp short .gt
.day:
    call dc_hdname              ; (BL = its style)
    cmp byte [stik_li], 2
    jb .put
.gt:
    mov si, dc_s_more2
    call dc_puts
    mov byte [di], 0
.put:
    mov cx, 8
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

; dc_casec - the agenda's lines; the last one `+N more` while it pages, and
; `No appointments` on line 0 when the day has none - card 3's `No reminders
; today` again, so an empty day does not read as a paint that stopped. DI =
; the geometry out
dc_casec:
    mov word [dc_li], 0         ; line 0, of the agenda ([dc_lsec] = 0)
.l:
    mov di, [dc_geo]
    mov al, [dc_li]
    cmp al, [di + DCG_AN]
    jae .r
    call dc_arow                ; DX = the line's row
    mov ax, dx
    sub al, [di + DCG_BAR]      ; its bar's first row... (rows < 256)
    push dx
    mov dx, ax
    add dl, [di + DCG_AP]       ; ...and the next line's
    call dc_vis
    pop dx
    jc .n
    mov al, [dc_li]
    call dc_aslot               ; CF = 0 AL = the item; CF = 1 AH = 1 pager
    jnc .item
    mov cx, 8                   ; at the times' x
    or ah, ah
    jz .none
    mov ah, [dc_acnt]
    call dc_cmore
    jmp short .n
.item:
    add al, [dc_afirst]
    call dc_caline
.n:
    inc byte [dc_li]
    jmp short .l
.none:                          ; a BLANK line: on line 0 that is no items
    cmp byte [dc_li], 0
    jne .n
    mov si, dc_s_noapp          ; regular, as card 3's is
    xor bl, bl
    call dc_ctext
    jmp short .n
.r:
    ret

; dc_arow - DX = agenda line AL's row. dc_trow - DX = task line AL's box top
; (DCG_T0 / DCG_TP sit as far past DCG_A0 / DCG_AP). The rest kept
dc_trow:
    push di
    mov di, DCG_T0 - DCG_A0
    jmp short dc_arow.g
dc_arow:
    push di
    xor di, di
.g:
    add di, [dc_geo]
    push ax
    mul byte [di + DCG_AP]
    add al, [di + DCG_A0]
    adc ah, 0
    mov dx, ax
    pop ax
    pop di
    ret
%if DCG_TP - DCG_AP != DCG_T0 - DCG_A0
  %error "deskcal: dc_trow is dc_arow at DCG_T0 - DCG_A0"
%endif

; dc_aslot / dc_tslot - what agenda / task line AL shows: CF = 0 AL = the
; item (from the list's first); CF = 1 not an item, AH = 1 it is the PAGER.
; The pager is the last line while the list has more than its lines
dc_aslot:
    push bx
    mov bx, [dc_geo]
    mov bl, [bx + DCG_AN]
    mov bh, [dc_acnt]
    mov ah, [dc_atop]
    jmp short dc_slot
dc_tslot:
    push bx
    mov bx, [dc_geo]
    mov bl, [bx + DCG_TN]
    mov bh, [STIK_PAY + DCP_NTASK]
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
    cmp al, bh                  ; CF = 1 an item...
    mov ah, 0
    cmc                         ; ...CF = 0 out
.x:
    pop bx
    ret

; dc_cmore - `+N more` at x CX, row DX: AH = the list's items, the ones not
; on show counted from the page [dc_lsec]'s list is at - lines - 1 items a
; page, or what is left from its top. Clobbers AX, BX, SI, DI
dc_cmore:
    mov di, [dc_geo]
    mov bl, [di + DCG_AN]
    mov bh, [dc_atop]
    cmp byte [dc_lsec], 0
    je .a
    mov bl, [di + DCG_TN]
    mov bh, [dc_ttop]
.a:
    dec bx                      ; a page, the pager not counted (BL >= 2)
    mov al, ah
    sub al, bh                  ; left from the top
    cmp al, bl
    jbe .b
    mov al, bl                  ; AL = the items this page shows
.b:
    sub ah, al
    mov di, dc_line
    mov al, '+'
    call dc_putc
    mov al, ah
    call dc_putn
    mov si, dc_s_more
    call dc_puts
    mov byte [di], 0
    xor bl, bl
    jmp dc_cline

; dc_caline - appointment AL on agenda line [dc_li] (row DX, DI = the
; geometry): `HH:MM text`, inverse when it is the selected one
dc_caline:
    call dc_aptr                ; SI = its record
    mov ah, DCS_APPT
    mov bp, 4                   ; the bar from face x 4
    call dc_csel                ; BL = the ink
    mov di, dc_line
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
    mov byte [di], 0
    mov cx, 8
    call dc_cline
    mov di, [dc_geo]
    mov cl, [di + DCG_ATN]      ; (CH = 0)
    mov di, dc_line
    add si, DCA_TEXT
    call dc_putsn
    mov byte [di], 0
    mov cx, DC_ATX
    jmp dc_cline

; -----------------------------------------------------------------------------
; COMPACT's slot: the heading names the page; the page is the day's agenda,
; the tasks or the reminder
; -----------------------------------------------------------------------------
dc_cslot:
    call dc_chead
    cmp byte [dc_pane], 1
    je .tasks
    ja dc_crem
    jmp dc_casec
.tasks:
    jmp dc_ctsec

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
    xor dh, dh                  ; (DH = 0 from here: DX is a row)
    call dc_cbits
    cmp byte [dc_c3k], DC3_NONE
    jne .some
    mov si, dc_s_norem
    mov cl, [di + DCG_NONEX]
    mov dl, [di + DCG_R1Y]
    xor bl, bl
    jmp dc_ctext
.some:
    mov al, [dc_c3a]            ; the time - after the day, when the one it
    call dc_aptr                ; rings for is not today's (a missed one
    push si                     ; must not read as today's going off early)
    mov di, dc_line
    mov ax, [si + DCA_DATE]
    call dc_tpack               ; BX = today
    xor bp, bp                  ; BP = the text's shift right (MID)
    cmp ax, bx
    je .tm
    cmp byte [stik_li], 1
    je .wd
    push ax                     ; TALL / COMPACT: `23 Aug 21:00`
    and al, 31
    call dc_putn
    mov al, ' '
    call dc_putc
    pop ax
    mov cl, 5
    shr ax, cl
    and al, 15
    call dc_mname
    call dc_puts3
    jmp short .sp
.wd:                            ; MID, one line: `Sun 21:00`, the text moved
    mov bx, ax                  ; over and cut to fit
    mov cl, 5
    shr bx, cl
    and bl, 15                  ; BL = the month
    mov bh, al
    and bh, 31                  ; BH = the day
    mov cl, 9
    shr ax, cl
    add ax, DC_Y0
    call dc_dow_of
    call dc_dname
    call dc_puts3
    mov bp, 32
.sp:
    mov al, ' '
    call dc_putc
.tm:
    mov ax, [dc_c3t]
    call dc_tfmt
    call dc_lnzg
    mov cl, [di + DCG_RX]       ; (CH = 0: CX is BELLX or dc_puts3's)
    mov dl, [di + DCG_R1Y]
    xor bl, bl                  ; regular: x1 bold fills a time's `0`s
    call dc_cline
    pop si                      ; the text
    add si, DCA_TEXT
    mov ax, bp
    mov cl, 3
    shr ax, cl
    mov cl, [di + DCG_R2N]      ; (CH = 0)
    sub cx, ax                  ; a character fewer for each 8 px moved
    mov di, dc_line
    call dc_putsn
    call dc_lnzg
    mov cl, [di + DCG_R2X]      ; (CH = 0)
    add cx, bp
    mov dl, [di + DCG_R2Y]
    call dc_cline
    cmp byte [dc_c3k], DC3_RING
    jne .r
    mov dl, [di + DCG_LKY]      ; RINGING: `Snooze • Dismiss`
    mov cl, [di + DCG_LKX]
    mov si, dc_s_snooze
    call dc_ctext
    add cx, DC_LINKS
    mov si, dc_s_dismiss
    call dc_ctext
    mov ax, cx                  ; the dot, centred in its cell (its bytes
    sub ax, DC_LINKS - 59       ; are neither word's, so after both is the
    mov bx, dx                  ; same picture)
    add bx, 3
    mov cx, 2
    mov dx, cx
    jmp dc_cfill0
.r:
    ret

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
    mov ah, [dc_lpend]          ; a held button's PENDING un-light
    dec ah                      ; (dc_unlit) is settled by this redraw when
    cmp ah, al                  ; it covers the button's whole rect (none:
    jne .lp                     ; 0FFh, never a card)
    cmp bx, [dc_ly0]
    jg .lp
    cmp dx, [dc_ly1]
    jl .lp
    cmp cl, [dc_lx0]
    ja .lp
    cmp ch, [dc_lx1]
    jb .lp
    mov byte [dc_lpend], 0
.lp:
    call stik_ctop              ; BX = the card's face-relative top
    add bx, [stik_fy]
    mov [dc_rtop], bx
    xchg ax, cx                 ; the x window the compose may skip text
    inc ah                      ; outside of (dc_ctext): bytes CL..CH
    mov bl, ah
    mov cl, 3
    cbw
    shl ax, cl
    mov [dc_cx0], ax
    mov al, bl
    cbw
    shl ax, cl
    mov [dc_cx1], ax
.chunk:
    mov cx, [dc_rr1]
    sub cx, [dc_rr0]
    inc cx
    cmp cx, GFXE_BAND_H
    jbe .n
    mov cx, GFXE_BAND_H
.n:
    mov [gfxe_bh], cx
    push cx
    mov ax, 0FFFFh
    call gfxe_bclear            ; white paper
    pop cx
    mov al, [dc_rcard]
    mov bx, [dc_rr0]
    call dc_compose
    mov al, [dc_rb]             ; the bytes: the band's start, x, width
    cbw
    add ax, dc_band
    xchg ax, si
    mov ax, [dc_cx0]
    mov cx, [dc_cx1]
    sub cx, ax
    add ax, [stik_fx]
    mov bx, [dc_rtop]
    add bx, [dc_rr0]
    mov dx, [gfxe_bh]
    push ds
    pop es
    mov bp, GFXE_BAND_ST
    call OSAPI_GFX_BLIT1
    mov bx, [dc_rr0]
    mov dx, [gfxe_bh]
    add dx, bx                  ; DX = past these rows
    cmp byte [dc_rb], DC_FX2 / 8
    ja .nof                     ; bytes right of the field: none of it
    dec dx
    mov al, [dc_rcard]
    call dc_fover               ; the field over these rows, when open
    inc dx
.nof:
    mov [dc_rr0], dx
    cmp dx, [dc_rr1]
    jle .chunk
    mov word [dc_cx1], 0        ; the paint's compose: the whole width
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
    mov cx, (STIK_FACE_W / 8 - 1) << 8
    call dc_rrect
    pop cx
    ret

; dc_rcard - card AL whole
dc_rcardw:
    push bx
    push dx
    xor bx, bx
    call dc_chgt                ; DX = its rows
    dec dx
    call dc_rrows
    pop dx
    pop bx
    ret

; dc_chgt - DX = card AL's height. The rest kept
dc_chgt:
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    add bx, [stik_lay]
    mov dl, [bx + STIKL_CARDS]
    xor dh, dh
    pop bx
    ret

; -----------------------------------------------------------------------------
; STIK_PAINTCARD: the include's slab painter, then the field over card 2's
; heading when it is open and the damage reached it
; -----------------------------------------------------------------------------
dc_pcard:
    call stik_slabs
    mov byte [dc_clip], 1       ; the kernel's clip is armed for the paint

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
    cbw
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

; dc_frect - the field's rect on the glass now ([stik_fx]/[stik_fy] current)
dc_frect:
    push ax
    push bx
    push dx
    mov al, 1
    call stik_ctop
    add bx, [stik_fy]
    xchg ax, bx                 ; AX = card 2's top on the glass
    call dc_fhead               ; BX = the field's first card row
    add ax, bx
    mov [dc_ln + LN_Y1], ax
    add ax, 11
    mov [dc_ln + LN_Y2], ax
    mov ax, [stik_fx]
    add ax, DC_FX1
    mov [dc_ln + LN_X1], ax
    add ax, DC_FX2 - DC_FX1
    mov [dc_ln + LN_X2], ax
    pop dx
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
    mov bx, [dc_geo]
    mov al, [bx + DCG_HY]
    cbw
    sub ax, 3
    xchg ax, bx
    mov dx, bx
    add dx, 11
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
    push si
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov byte [dc_hk], DCH_NONE
    mov [dc_qx], cx
    mov [dc_qy], dx
    mov di, [dc_geo]
    mov bx, dc_htab
    call dc_rolej
    jmp dc_pop7

; dc_hbtn - a button: AL = DCB_*, BL..BH its bytes, SI..DX its rows - but
; only when the press is INSIDE that rect, which is what lights: a press
; beside a button is a press on the card's margin (it activates the widget
; and does nothing else). CX kept
dc_hbtn:
    cmp si, [dc_qy]
    jg .r
    cmp dx, [dc_qy]
    jl .r
    push ax
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
    mov [dc_hx0], bx            ; (and [dc_hx1] = BH)
    mov [dc_hy0], si
    mov [dc_hy1], dx
    ret
.no:
    pop ax
.r:
    ret

; dc_hgrid - dc_hmon's press below the nav: a day - the week by the box's
; rows, the column by the field
dc_hgrid:
    mov al, [di + DCG_W0]
    sub al, [di + DCG_BOXT]
    cbw
    sub dx, ax
    jl .r
    xchg ax, dx
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
    xchg ax, bx                 ; BL = the column, AL = the week
    mov ah, 7
    mul ah
    add al, bl
    xchg ax, bx                 ; BX = the grid index
    mov al, [dc_grid + bx]
    or al, al
    jz .r
    mov [dc_hi], al
    mov byte [dc_hk], DCH_DAY
.r:
    ret

; dc_hmon - card 1 (CX, DX in it; DI the geometry)
dc_hmon:
    mov al, [di + DCG_NAVY]
    cbw
    xchg ax, si                 ; SI = the nav's row
    cmp byte [stik_li], 2
    jne .tall
    ; --- COMPACT: the number left, the weekday and the nav right of it -
    ; the light from the nav's own row, MONDAY's last being the one above
    cmp dx, 19
    jg dc_hgrid
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
    dec si                      ; the nav's light: 2 above, 1 below
    dec si
    cmp dx, si
    jl .today                   ; the header block: today's page
    lea ax, [si + 12]
    cmp dx, ax
    jge dc_hgrid
    cmp cx, 32
    jl .prev
    cmp cx, 144
    jge .next
.label:
    push si
    call dc_navlbl              ; CX = the label's x
    mov si, dc_line
    call dc_slen
    mov bx, cx
    mov cl, 3
    shl ax, cl
    add ax, bx
    dec ax                      ; its last pixel...
    shr bx, cl                  ; ...BL its first byte...
    shr ax, cl
    mov bh, al                  ; ...BH its last
    pop si
    mov al, DCB_TODAY
    jmp short .navb
.prev:
    call dc_atfirst
    je dc_hgrid.r
    mov al, DCB_PREV
    mov bl, [di + DCG_NAVL]
    sub bl, 8                   ; the arrow's byte and the one before it
    jmp short .arr
.next:
    call dc_atlast
    je dc_hgrid.r
    mov al, DCB_NEXT
    mov bl, [di + DCG_NAVR]     ; ...and after it
.arr:
    mov cl, 3
    shr bl, cl
    mov bh, bl
    inc bh
.navb:                          ; SI = the light's first row - 2 above the
    mov dl, [di + DCG_NAVY]     ; nav, COMPACT none (MONDAY's rows) - to 1
    add dx, 9                   ; below its 8 (DH = 0: a press on the nav)
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
    inc ax
    mov bh, al
    mov bl, 1
    mov dl, [di + DCG_BIGY]     ; the rows: 8 x the scale
    xor dh, dh
    mov si, dx
    mov cl, 3
    shl ah, cl
    add dl, ah
    dec dx
    mov al, DCB_TODAY
    jmp dc_hbtn

; dc_hday - card 2 / the slot (CX, DX in it, CX = [dc_qx] throughout; DI
; the geometry)
dc_hday:
    cmp byte [dc_ed], 0
    je .shut
    mov ax, dx                  ; the field open: a press in it moves the
    call dc_fhead               ; caret (deskcal.asm), and nothing else in
    cmp ax, bx                  ; the card acts
    jl .r
    cmp ax, dx
    jg .r
    mov byte [dc_hk], DCH_FIELD
    ret
.shut:
    mov al, [di + DCG_HY]
    cbw
    xchg ax, si                 ; SI = the heading's row
    sub dx, si                  ; (the lists read [dc_qy], not DX)
    cmp dx, -3
    jl .r
    cmp dx, 10
    jge .lists
    lea dx, [si + 9]            ; the heading row: `+` right, the page left
    dec si
    dec si
    cmp byte [dc_pane], 2
    je .page
    cmp cx, DC_PLUSX            ; bytes 20..21 - clear of the field's
    jl .page                    ; last byte (19), so its un-light cannot
    mov al, DCB_PLUS            ; reach the field it just opened
    mov bx, ((STIK_FACE_W / 8 - 1) << 8) | (DC_PLUSX / 8)
    jmp dc_hbtn
.page:
    cmp byte [stik_li], 2
    jne .r                      ; TALL / MID: the heading does nothing
    mov al, DCB_PAGE
    mov bx, 0B00h               ; bytes 0..11 - `Reminder >` is 10 cells
    jmp dc_hbtn
.lists:
    cmp byte [stik_li], 2
    jne .both
    cmp byte [dc_pane], 1       ; COMPACT: the page's one list
    jb .ag
    je .tk
    ret
.both:
    call dc_hagenda
    cmp byte [dc_hk], DCH_NONE
    jne .r
.tk:
    jmp short dc_htask
.r:
    ret
.ag:                            ; (dc_hagenda follows)

; dc_hagenda - an agenda line under CX, DX
dc_hagenda:
    mov byte [dc_li], 0
.l:
    mov al, [dc_li]
    cmp al, [di + DCG_AN]
    jae .r
    call dc_arow
    sub dl, [di + DCG_BAR]      ; the line's rows: the bar's first...
    mov si, dx
    add dl, [di + DCG_AP]       ; ...and the next line's (rows < 256)
    mov ax, [dc_qy]
    cmp ax, si
    jl .n
    cmp ax, dx
    jge .n
    mov al, [dc_li]
    call dc_aslot
    jnc .item
    or ah, ah
    jz .r
    mov al, DCB_AMORE           ; the pager: its text's bytes light
    mov bx, 0A01h
    mov dx, si
    add dl, [di + DCG_BARH]
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

; dc_htask - a task line under [dc_qx] (= CX), [dc_qy]
dc_htask:
    mov byte [dc_li], 0
.l:
    mov al, [dc_li]
    cmp al, [di + DCG_TN]
    jae .r
    call dc_trow                ; DX = the box's top
    mov si, dx
    add dl, [di + DCG_TP]       ; (rows < 256)
    mov ax, [dc_qy]
    cmp ax, si
    jl .n
    cmp ax, dx
    jge .n
    mov al, [dc_li]
    call dc_tslot
    jnc .item
    or ah, ah
    jz .r
    mov al, DCB_TMORE
    mov bl, [di + DCG_TX]
    mov cl, 3
    shr bl, cl
    mov bh, bl
    add bh, 9
    mov dx, si                  ; the pager's light: its text's bar
    add dl, [di + DCG_TTY]
    sub dl, [di + DCG_BAR]
    mov si, dx
    add dl, [di + DCG_BARH]
    dec dx
    jmp dc_hbtn
.item:
    mov [dc_hi], al
    mov byte [dc_hk], DCH_TTEXT
    cmp cx, 6                   ; the box, a column either side
    jl .r
    mov al, [di + DCG_BOX]
    cbw
    add ax, 9
    cmp cx, ax
    jg .r
    mov byte [dc_hk], DCH_TBOX
    ret
.n:
    inc byte [dc_li]
    jmp short .l
.r:
    ret

; dc_hslot - COMPACT's slot: the heading, `+`, the agenda or tasks page -
; or, on the reminder's page, its links (into dc_hrem)
dc_hslot:
    call dc_hday
    cmp byte [dc_pane], 2
    jne dc_hrem.r
    cmp byte [dc_hk], DCH_NONE
    jne dc_hrem.r

; dc_hrem - card 3 (or the slot's reminder page): the links while RINGING
dc_hrem:
    cmp byte [dc_c3k], DC3_RING
    jne .r
    mov al, [di + DCG_LKY]
    cbw
    dec ax
    dec ax
    xchg ax, si                 ; the links' light: 2 rows above them to
    lea dx, [si + 11]           ; 1 below (dc_hbtn tests the press's row)
    mov al, [di + DCG_LKX]
    cbw
    mov bx, [dc_qx]
    sub bx, ax
    jl .r
    mov cx, (DCB_SNOOZE << 8) | 5   ; CH = the link, CL = its bytes - 1
    cmp bx, 48 + 4              ; `Snooze` and a little
    jl .b
    sub bx, DC_LINKS
    jl .r
    cmp bx, 56 + 4
    jg .r
    add al, DC_LINKS
    mov cx, (DCB_DISMISS << 8) | 6
.b:
    shr al, 1
    shr al, 1
    shr al, 1
    mov bl, al                  ; BL..BH the link's bytes
    add al, cl
    mov bh, al
    mov al, ch
    jmp dc_hbtn
.r:
    ret

; --- the strings and the bit records -----------------------------------------
dc_s_wk:    db 'MTWTFSS', 0     ; over each field, MONDAY first
dc_s_lt:    db '<', 0
dc_s_plus:  db '+', 0
dc_s_today: db 'Today', 0
dc_s_more:  db ' more', 0
dc_s_more2: db ' '
dc_s_gt:    db '>', 0           ; (the tail of the slot's ` >`)
dc_s_allday: db '--:--', 0
dc_s_norem: db 'No reminders today', 0
dc_s_noapp: db 'No appointments', 0
dc_s_snooze: db 'Snooze', 0
dc_s_dismiss: db 'Dismiss', 0
dc_dlong:   dw dc_d0, dc_d1, dc_d2, dc_d3, dc_d4, dc_d5, dc_d6
dc_mlong:   dw dc_m1, dc_m2, dc_m3, dc_m4, dc_m5, dc_m6
            dw dc_m7, dc_m8, dc_m9, dc_m10, dc_m11, dc_m12
%if dc_mlong - dc_dlong != 14
  %error "deskcal: dc_mname reads dc_mlong as dc_dlong's entries 7.."
%endif
dc_d0:      db 'Sunday', 0
dc_d1:      db 'Monday', 0
dc_d2:      db 'Tuesday', 0
dc_d3:      db 'Wednesday', 0
dc_d4:      db 'Thursday', 0
dc_d5:      db 'Friday', 0
dc_d6:      db 'Saturday', 0
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
