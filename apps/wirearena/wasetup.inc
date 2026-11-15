; wasetup.inc - the opening window: the MATCH SETUP (docs/plans/
; WIREARENA-NET-PLAN.md 2 and 9.4). Every control on it is the system's own:
;
;   - five DROP-DOWNS (apps/os88ui.inc, SPEC.md 13.14): Arena, Columns,
;     Bots, Skill, Play - a joined player's Arena, Bots and Skill greyed,
;     the host's to choose (and showing the host's);
;   - one TEXT FIELD (apps/os88line.inc, SPEC.md 83) for the address,
;     beside Play and only while Play is internet;
;   - two BUTTONS (os88ui): Play alone / Ready linked, and Keys - which
;     puts the key list up as the standard ABOUT CARD (SPEC.md 12.2), the
;     same card the system menu's About shows.
;
; The window's text rows (walink.inc's wa_lkrows) are row 0, what the link
; is doing, and rows 1 to 5, the players once linked. Each control is drawn
; when it CHANGED (its value, its greying, the focus), never all of them for
; a click or a key: the owner saw the whole window flash on every one,
; which was every row and both buttons redrawn per input (9.4).
;
; THE PAINTER IS OPAQUE BY CONSTRUCTION, WireFleet's shape (SPEC.md 114):
; every element draws its own ground, and wa_sground fills only the gaps
; between them - so a repaint of our own (the About card going, a list
; that could not bank) writes each pixel once, with no white first. A
; kernel PAINT skips the ground: the kernel filled the content white.

WS_W        equ 336             ; the content box (the template: 338 x 174)
WS_RM       equ 328             ; ...and its right margin, past every
                                ; control and row (the owner: the drop-downs
                                ; off the edge, as the left's 8 px are)
WS_H        equ 155
WS_LX       equ 0               ; the left column: a marker cell and a
WS_LDX1     equ 56              ; label (seven cells), then its drop-down
WS_LDX2     equ 183
WS_RX       equ 184             ; the right column: marker and label (nine
WS_RDX1     equ 256             ; cells), then its drop-down
WS_RDX2     equ 327
WS_Y1       equ 14              ; the three rows of controls, 16 high
WS_Y2       equ 32
WS_Y3       equ 50
WS_Y4       equ 68              ; (the fourth: Mode, CTF-PLAN 7)
WS_DH       equ 16
WS_FX1      equ 192             ; the address field, right of Play
WS_FX2      equ 327
WS_BTNY     equ 87              ; the buttons
WS_BTNW     equ 104
WS_BTNH     equ 16
WS_B2X      equ 129
WS_LOBY     equ 106             ; the players' rows (walink.inc), 10 apart
WS_NDR      equ 6               ; the drop-downs; the focus's seventh stop
WS_FLD      equ 6               ; is the field

; wa_sinit - the window is open: the controls' records
wa_sinit:
    mov ax, [wa_win]
    xor bx, bx
.d:
    mov si, [wa_sdrs+bx]
    mov [si+OS88UI_DR_WIN], ax
    inc bx
    inc bx
    cmp bx, WS_NDR * 2
    jb .d
    mov word [wa_sfld+LN_BUF], wa_saddr
    mov word [wa_sfld+LN_MAX], 16
    mov bx, wa_sbrec
    mov dx, wa_sclick
    call os88ui_btninit
    call wa_sforget
    ret

; wa_sforget - nothing drawn yet: every control drawn by the next pass
wa_sforget:
    push ds
    pop es
    mov di, wa_sdsel            ; (sel, greying for each; the focus; the
    mov al, 0FFh                ; field's place)
    mov cx, WS_NDR * 2 + 2
    rep stosb
    ret

; --- the controls' state ----------------------------------------------------

; wa_sval - BX = a drop-down x 2: AL = the pick its setting says, AH = how
; many items it has now, DL = 1 when it is greyed (a joined player: the
; host's arena, bots and skill). Keeps all but AX, DX
wa_sval:
    push bx
    xor dl, dl
    cmp bx, 4 * 2               ; (arena 0, bots 2, skill 3 are the host's)
    je .mine
    cmp bx, 1 * 2
    je .mine
    call wa_lklinked
    jz .mine
    call wa_lkhost
    jnz .mine
    inc dx
.mine:
    jmp [wa_svals+bx]
.arena:
    mov al, [wa_lkarena]
    mov ah, WA_NARENA
    jmp .out
.cols:
    mov al, [wa_want]           ; (40 is 1 there, item 0 here)
    xor al, 1
    mov ah, 2
    jmp .out
.bots:
    mov al, [wa_lkbots]
    mov ah, WA_NFIG + 1         ; alone: 0..3; linked: two at most (LINK-
    cmp byte [wa_splay], 0      ; PLAN 8: four in all, two of them players)
    je .out
    mov ah, 3
    jmp .out
.skill:
    mov al, [wa_lkdiff]
    mov ah, 3
    jmp .out
.play:
    mov al, [wa_splay]
    mov ah, 4
    jmp .out
.mode:
    mov al, [wa_lkgmode]        ; (the host's, as the arena is)
    mov ah, 2
.out:
    pop bx
    ret
wa_svals:   dw wa_sval.arena, wa_sval.cols, wa_sval.bots, wa_sval.skill
            dw wa_sval.play, wa_sval.mode

; wa_spick - BX = a drop-down x 2, AL = its new pick: the setting it names
; set, and whatever that implies
wa_spick:
    jmp [wa_spicks+bx]
.arena:
    mov [wa_lkarena], al
    ret
.cols:
    xor al, 1
    mov [wa_want], al
    ret
.bots:
    mov [wa_lkbots], al
    ret
.skill:
    mov [wa_lkdiff], al
    mov [wa_diff], al
    ret
.play:
    mov [wa_splay], al          ; ALONE: neither carrier; SERIAL: the
    or al, al                   ; cable's search; the LOCAL NETWORK or the
    jnz .linked                 ; INTERNET: the card (wa_lknetgo)
    call wa_lkcable
    call lk_disconnect
    mov word [lk_blk+LK_MSG], wa_ms_setup
    ret
.linked:
    cmp byte [wa_lkbots], 2     ; (linked: two bots at most)
    jbe .b
    mov byte [wa_lkbots], 2
.b:
    cmp al, 1
    je .ser
    jmp wa_lknetgo
.mode:
    mov [wa_lkgmode], al
    ret
.ser:
    call wa_lkcable
    call lk_disconnect
    call lk_auto
    mov word [lk_blk+LK_MSG], lk_m_search
    ret
wa_spicks:  dw wa_spick.arena, wa_spick.cols, wa_spick.bots, wa_spick.skill
            dw wa_spick.play, wa_spick.mode

; wa_sset - BX = a drop-down x 2, AH = -1 or +1: its pick one along, round
; (Left and Right on the focused control). A greyed one says whose it is
wa_sset:
    push ax
    call wa_sval
    or dl, dl
    pop dx                      ; (DH = the step)
    jnz .host
    add al, dh
    jns .lo
    mov al, ah
    dec ax
.lo:
    cmp al, ah
    jb .ok
    xor al, al
.ok:
    jmp wa_spick
.host:
    push ds
    pop es
    mov si, wa_t_host
    xor cx, cx
    call OSAPI_TOAST
    ret

; --- the layout ----------------------------------------------------------------

; wa_splace - every control's rect from where the window is now (screen
; coordinates; also os88ui's OS88UI_BT_PLACE). Draws nothing, keeps every
; register
wa_splace:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [wa_win]
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov [wa_sorg], ax
    mov [wa_sorg+2], dx
    xor bx, bx                  ; the drop-downs, off wa_srects
.d:
    mov si, [wa_sdrs+bx]
    mov di, bx
    shl di, 1
    shl di, 1
    add di, wa_srects
    mov cx, [di]
    add cx, ax
    mov [si+OS88UI_DR_RECT], cx
    mov cx, [di+2]
    add cx, dx
    mov [si+OS88UI_DR_RECT+2], cx
    mov cx, [di+4]
    add cx, ax
    mov [si+OS88UI_DR_RECT+4], cx
    mov cx, [di+6]
    add cx, dx
    mov [si+OS88UI_DR_RECT+6], cx
    inc bx
    inc bx
    cmp bx, WS_NDR * 2
    jb .d
    mov cx, ax                  ; the field
    add cx, WS_FX1
    mov [wa_sfld+LN_X1], cx
    add cx, WS_FX2 - WS_FX1
    mov [wa_sfld+LN_X2], cx
    mov cx, dx
    add cx, WS_Y3
    mov [wa_sfld+LN_Y1], cx
    add cx, WS_DH - 1
    mov [wa_sfld+LN_Y2], cx
    add ax, 8                   ; the buttons
    add dx, WS_BTNY
    mov [wa_sbrects], ax
    mov [wa_sbrects+2], dx
    add ax, WS_BTNW - 1
    mov [wa_sbrects+4], ax
    add dx, WS_BTNH - 1
    mov [wa_sbrects+6], dx
    add ax, WS_B2X - 8 - WS_BTNW + 1
    mov [wa_sbrects+8], ax
    sub dx, WS_BTNH - 1
    mov [wa_sbrects+10], dx
    add ax, WS_BTNW - 1
    mov [wa_sbrects+12], ax
    add dx, WS_BTNH - 1
    mov [wa_sbrects+14], dx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the drop-downs' rects in the content: Arena, Columns, Bots, Skill, Play
wa_srects:  dw WS_LDX1, WS_Y1, WS_LDX2, WS_Y1 + WS_DH - 1
            dw WS_RDX1, WS_Y1, WS_RDX2, WS_Y1 + WS_DH - 1
            dw WS_LDX1, WS_Y2, WS_LDX2, WS_Y2 + WS_DH - 1
            dw WS_RDX1, WS_Y2, WS_RDX2, WS_Y2 + WS_DH - 1
            dw WS_LDX1, WS_Y3, WS_LDX2, WS_Y3 + WS_DH - 1
            dw WS_LDX1, WS_Y4, WS_LDX2, WS_Y4 + WS_DH - 1
; ...each one's label: its cell's x and y, its text (a marker cell first)
wa_slabs:   dw WS_LX, WS_Y1 + 4, wa_sl_a
            dw WS_RX, WS_Y1 + 4, wa_sl_c
            dw WS_LX, WS_Y2 + 4, wa_sl_b
            dw WS_RX, WS_Y2 + 4, wa_sl_s
            dw WS_LX, WS_Y3 + 4, wa_sl_p
            dw WS_LX, WS_Y4 + 4, wa_sl_m
wa_sl_a:    db ' Arena ', 0
wa_sl_c:    db ' Columns ', 0
wa_sl_b:    db ' Bots  ', 0
wa_sl_s:    db ' Skill   ', 0
wa_sl_p:    db ' Play  ', 0
wa_sl_m:    db ' Mode  ', 0

; THE GROUND: every rect of the content no control and no row covers
; (content coordinates, inclusive). The last is the field's place, which
; is ground only while there is no field
wa_sgnd:    dw 0, 0, WS_RM - 1, 3
            dw 0, 4, 7, 11
            dw 0, 12, WS_RM - 1, WS_Y1 - 1
            dw 0, WS_Y1, WS_LDX1 - 1, WS_Y1 + 3
            dw 0, WS_Y1 + 12, WS_LDX1 - 1, WS_Y1 + 15
            dw WS_RX, WS_Y1, WS_RDX1 - 1, WS_Y1 + 3
            dw WS_RX, WS_Y1 + 12, WS_RDX1 - 1, WS_Y1 + 15
            dw 0, WS_Y1 + 16, WS_RM - 1, WS_Y2 - 1
            dw 0, WS_Y2, WS_LDX1 - 1, WS_Y2 + 3
            dw 0, WS_Y2 + 12, WS_LDX1 - 1, WS_Y2 + 15
            dw WS_RX, WS_Y2, WS_RDX1 - 1, WS_Y2 + 3
            dw WS_RX, WS_Y2 + 12, WS_RDX1 - 1, WS_Y2 + 15
            dw 0, WS_Y2 + 16, WS_RM - 1, WS_Y3 - 1
            dw 0, WS_Y3, WS_LDX1 - 1, WS_Y3 + 3
            dw 0, WS_Y3 + 12, WS_LDX1 - 1, WS_Y3 + 15
            dw WS_LDX2 + 1, WS_Y3, WS_FX1 - 1, WS_Y3 + 15
            dw 0, WS_Y3 + 16, WS_RM - 1, WS_Y4 - 1
            dw 0, WS_Y4, WS_LDX1 - 1, WS_Y4 + 3
            dw 0, WS_Y4 + 12, WS_LDX1 - 1, WS_Y4 + 15
            dw WS_LDX2 + 1, WS_Y4, WS_RM - 1, WS_Y4 + 15
            dw 0, WS_Y4 + 16, WS_RM - 1, WS_BTNY - 1
            dw 0, WS_BTNY, 7, WS_BTNY + WS_BTNH - 1
            dw 8 + WS_BTNW, WS_BTNY, WS_B2X - 1, WS_BTNY + WS_BTNH - 1
            dw WS_B2X + WS_BTNW, WS_BTNY, WS_RM - 1, WS_BTNY + WS_BTNH - 1
            dw 0, WS_BTNY + WS_BTNH, WS_RM - 1, WS_LOBY - 1
            dw 0, WS_LOBY, 7, WS_H - 1
            dw 8, WS_LOBY + 8, WS_RM - 1, WS_LOBY + 9
            dw 8, WS_LOBY + 18, WS_RM - 1, WS_LOBY + 19
            dw 8, WS_LOBY + 28, WS_RM - 1, WS_LOBY + 29
            dw 8, WS_LOBY + 38, WS_RM - 1, WS_LOBY + 39
            dw 8, WS_LOBY + 48, WS_RM - 1, WS_H - 1
            dw WS_RM, 0, WS_W - 1, WS_H - 1
WS_NGND     equ ($ - wa_sgnd) / 8
            dw WS_FX1, WS_Y3, WS_FX2, WS_Y3 + WS_DH - 1

; wa_sground - the gaps white (a repaint of our own; the kernel's PAINT
; filled them already). The clip is armed
wa_sground:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov si, wa_sgnd
    mov cx, WS_NGND
    cmp byte [wa_splay], 3      ; (no field: its place too)
    je .r
    inc cx
.r:
    push cx
    call wa_srect
    call OSAPI_GFX_FILL
    pop cx
    add si, 8
    loop .r
    cmp byte [wa_splay], 3      ; THE FIELD's inside that its own draw does
    jne .out                    ; not cover: the bands over and under its
    mov si, wa_sfld             ; text, and the inset left of it
    mov ax, [si+LN_X1]
    inc ax
    mov bx, [si+LN_Y1]
    inc bx
    mov cx, [si+LN_X2]
    dec cx
    mov dx, bx
    inc dx
    call OSAPI_GFX_FILL
    mov bx, [si+LN_Y1]
    add bx, LN_INSET + 8
    mov dx, [si+LN_Y2]
    dec dx
    call OSAPI_GFX_FILL
    call os88line_pen           ; (AX = where its text starts)
    mov cx, ax
    dec cx
    mov ax, [si+LN_X1]
    inc ax
    cmp cx, ax
    jl .out
    mov bx, [si+LN_Y1]
    add bx, LN_INSET
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL
.out:
    ret

; wa_srect - SI = a content rect: AX, BX, CX, DX that rect on the screen
wa_srect:
    mov ax, [si]
    add ax, [wa_sorg]
    mov bx, [si+2]
    add bx, [wa_sorg+2]
    mov cx, [si+4]
    add cx, [wa_sorg]
    mov dx, [si+6]
    add dx, [wa_sorg+2]
    ret

; --- drawing --------------------------------------------------------------------

; wa_sdraw - the controls, each drawn if it changed since it was drawn (all
; of them with [wa_lkfull]); the clip is taken first through wa_lkclipon
; (a tick's) or is the paint's. CF = 1: no clip, nothing drawn
wa_sdraw:
    call wa_splace
    xor bx, bx                  ; THE DROP-DOWNS
.d:
    call wa_sval                ; AL = the pick, AH = the items, DL greyed
    mov si, [wa_sdrs+bx]
    cmp byte [si+OS88UI_DR_OPEN], 0
    jne .dn                     ; (open: the list is the control's)
    mov di, bx
    shr di, 1
    cmp byte [wa_lkfull], 0
    jne .dd
    cmp al, [wa_sdsel+di]
    jne .dd
    cmp dl, [wa_sddis+di]
    je .dn
.dd:
    call wa_lkclipon
    jc .no
    mov [wa_sdsel+di], al
    mov [wa_sddis+di], dl
    mov [si+OS88UI_DR_SEL], al
    mov byte [si+OS88UI_DR_SEL+1], 0
    mov [si+OS88UI_DR_N], ah
    mov byte [si+OS88UI_DR_N+1], 0
    push bx
    mov bx, si
    xor di, di
    or dl, dl
    jz .live
    mov di, OS88UI_DIS
.live:
    call os88ui_drop
    pop bx
.dn:
    inc bx
    inc bx
    cmp bx, WS_NDR * 2
    jb .d
    mov al, [wa_sfoc]           ; THE LABELS: the focus's marker moved
    cmp byte [wa_lkfull], 0
    jne .lall
    cmp al, [wa_sdfoc]
    je .field
    push ax
    mov al, [wa_sdfoc]
    call wa_slabel              ; (the old one, then the new)
    pop ax
    jc .no
    call wa_slabel
    jc .no
    jmp .lset
.lall:
    xor al, al
.la:
    call wa_slabel
    jc .no
    inc ax
    cmp al, WS_NDR
    jb .la
.lset:
    mov al, [wa_sfoc]
    mov [wa_sdfoc], al
.field:
    xor al, al                  ; THE FIELD: there while Play is internet,
    cmp byte [wa_splay], 3      ; its caret while it has the focus
    jne .f
    inc ax
    cmp byte [wa_sfoc], WS_FLD
    jne .f
    inc ax
.f:
    mov byte [wa_sfld+LN_FOCUS], 0
    cmp al, 2
    jne .fo
    mov byte [wa_sfld+LN_FOCUS], 1
.fo:
    cmp byte [wa_lkfull], 0
    jne .fd
    cmp al, [wa_sdfld]
    je .ok
.fd:
    call wa_lkclipon
    jc .no
    mov [wa_sdfld], al
    or al, al
    jnz .on
    cmp byte [wa_lkfull], 0     ; (gone: its place white - a paint's
    jne .ok                     ; already is)
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov si, wa_sgnd + WS_NGND * 8
    call wa_srect
    call OSAPI_GFX_FILL
    jmp .ok
.on:
    mov si, wa_sfld
    call os88line_draw
.ok:
    clc
    ret
.no:
    stc
    ret

; wa_slabel - AL = a drop-down: its label, the focus's marker in its first
; cell. CF = 1 no clip. Keeps all but the flags
wa_slabel:
    cmp al, WS_NDR
    jae .out
    call wa_lkclipon
    jc .no
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    xor bh, bh
    mov si, bx                  ; (x 6: the label's three words)
    shl si, 1
    add si, bx
    shl si, 1
    mov cx, [wa_slabs+si]
    add cx, [wa_sorg]
    mov dx, [wa_slabs+si+2]
    add dx, [wa_sorg+2]
    mov si, [wa_slabs+si+4]
    mov ah, ' '
    cmp bl, [wa_sfoc]
    jne .m
    mov ah, '>'                 ; (the font has no triangles)
.m:
    mov [si], ah                ; (its own first cell: the strings are ours)
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    clc
    ret
.no:
    ret

; wa_sbtns - the buttons, their labels as things stand, drawn where one
; changed or [wa_lkfull]; none while the About card or a list is up
wa_sbtns:
    mov word [wa_sbrec+OS88UI_BT_N], 0
    cmp byte [wa_abon], 0
    jne .out
    call wa_sopen
    jnc .out
    mov word [wa_sbrec+OS88UI_BT_N], 2
    mov si, wa_sb_play          ; the first: Play alone, else Ready / Not
    cmp byte [wa_splay], 0      ; ready
    je .l1
    mov si, wa_sb_rdy
    cmp byte [wa_lkst], 0
    je .l1
    mov si, wa_sb_nrdy
.l1:
    mov di, wa_sb_keys
    cmp byte [wa_lkfull], 0     ; (a full pass, or a label changed: draw)
    jne .draw
    cmp si, [wa_sblab]
    jne .draw
    cmp di, [wa_sblab+2]
    je .out
.draw:
    call wa_lkclipon            ; (a tick's: the clip, if no row took it)
    jc .out
    mov [wa_sblab], si
    mov [wa_sblab+2], di
    call wa_splace
    mov bx, wa_sbrec
    mov al, 1
    call os88ui_btn
    mov al, 2
    call os88ui_btn
.out:
    ret

; wa_sopen - CF = 0 when a drop-down's list is open (BX = its record),
; CF = 1 none. Keeps all but BX
wa_sopen:
    push si
    xor si, si
.d:
    mov bx, [wa_sdrs+si]
    cmp byte [bx+OS88UI_DR_OPEN], 0
    jne .yes
    inc si
    inc si
    cmp si, WS_NDR * 2
    jb .d
    stc
    pop si
    ret
.yes:
    clc
    pop si
    ret

; wa_srepaint - the content again, by our own painter, ground and all (the
; About card gone; a list that could not bank). Under the lock
wa_srepaint:
    mov bx, [wa_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wa_splace
    call wa_sground
    mov byte [wa_lkclip], 1
    mov byte [wa_lkfull], 1
    mov byte [wa_lkown], 1      ; (our own: the blank rows drawn too)
    call wa_lkrows
    mov byte [wa_lkown], 0
.out:
    ret

; --- the About card: the keys (SPEC.md 12.2) -----------------------------------

; wa_about - WC_ABOUT, and the Keys button: the card up (SI = the window)
wa_about:
    push si
    call wa_sclose              ; (an open list down first)
    mov byte [wa_abon], 1
    mov word [wa_sbrec+OS88UI_BT_N], 0
    mov bx, [wa_win]
    mov si, wa_ablines
    call os88ui_about
    pop si
    ret

; wa_sdismiss - a click or a key with the card up: it goes, the page back
wa_sdismiss:
    mov byte [wa_abon], 0
    push si
    call wa_srepaint
    pop si
    ret

; wa_sclose - any open list down (Esc, a menu, the card). CF = 1 one was
wa_sclose:
    call wa_sopen
    jc .none
    call os88ui_drclose
    jnc .shut
    call wa_srepaint
.shut:
    stc
    ret
.none:
    clc
    ret

; --- input ------------------------------------------------------------------

; wa_skey - ONKEY's: Up / Down / Tab the focus, Left / Right the focused
; control's pick, Enter the first button, K the keys; on the field, its
; editing keys. AL = ASCII, AH = scan; CF = 1 taken
wa_skey:
    cmp byte [wa_abon], 0       ; (the card: any key takes it down)
    je .set
    call wa_sdismiss
    stc
    ret
.set:
    cmp al, 27                  ; Esc: an open list down
    jne .nesc
    call wa_sclose
    ret
.nesc:
    cmp byte [wa_sfoc], WS_FLD  ; THE FIELD's keys (joining by address):
    jne .nav                    ; digits and dots typed, the editing keys;
    cmp byte [wa_splay], 3      ; Up, Down, Tab and Enter are the page's
    jne .nav
    cmp ah, 0x48
    je .nav
    cmp ah, 0x50
    je .nav
    cmp al, 9
    je .nav
    cmp al, 13
    je .nav
    cmp al, ' '                 ; (Space: Ready, as anywhere on the page)
    je .nav
    jb .edit                    ; (printable: a digit or a dot, or nothing)
    cmp al, 127
    jae .edit
    cmp al, '.'
    je .edit
    cmp al, '0'
    jb .done
    cmp al, '9'
    ja .done
.edit:
    mov si, wa_sfld
    mov byte [si+LN_FOCUS], 1   ; (it has the focus NOW: the tick that
    push word [si+LN_VIEW]      ; draws its caret may not have run yet)      ; (as they were: os88line_edit's
    push word [si+LN_LEN]       ; arguments)
    push word [si+LN_CAR]
    call os88line_key
    pop dx
    pop bx
    pop ax
    jc .done                    ; (not an edit)
    push bx
    mov bx, [wa_win]
    call OSAPI_WM_CLIP_SET      ; (CF = 1: nothing of us to see)
    pop bx
    jc .parse
    call os88line_edit
.parse:
    call wa_saparse
    jmp .done
.nav:
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp al, 9
    je .down
    cmp ah, 0x4B
    je .left
    cmp ah, 0x4D
    je .right
    cmp al, 13
    je .enter
    mov ah, al
    or ah, 0x20
    cmp ah, 'k'
    je .keys
    clc
    ret
.up:
    cmp byte [wa_sfoc], 0
    je .done
    dec byte [wa_sfoc]
    jmp .done
.down:
    mov al, WS_NDR - 1          ; (the field a stop only while there)
    cmp byte [wa_splay], 3
    jne .dl
    inc ax
.dl:
    cmp [wa_sfoc], al
    jae .done
    inc byte [wa_sfoc]
    jmp .done
.left:
    mov ah, -1
    jmp .chg
.right:
    mov ah, 1
.chg:
    mov bl, [wa_sfoc]
    cmp bl, WS_NDR
    jae .done
    xor bh, bh
    shl bx, 1
    call wa_sset
    call wa_sfocfix
    jmp .done
.enter:
    call wa_sstart
    jmp .done
.keys:
    call wa_about
.done:
    stc
    ret

; wa_sfocfix - the focus off the field once there is no field
wa_sfocfix:
    cmp byte [wa_splay], 3
    je .out
    cmp byte [wa_sfoc], WS_FLD
    jb .out
    mov byte [wa_sfoc], WS_NDR - 1
.out:
    ret

; wa_saparse - the address typed: the six bytes it names (wa_saddr6: the IP
; address and our port), four numbers with dots between
wa_saparse:
    mov si, wa_saddr
    mov di, wa_saddr6
    mov cx, 4
.n:
    xor ax, ax
.d:
    mov bl, [si]
    sub bl, '0'
    jb .end
    cmp bl, 9
    ja .end
    mov bh, 10
    mul bh
    add al, bl
    inc si
    jmp .d
.end:
    mov [di], al
    inc di
    cmp byte [si], '.'
    jne .last
    inc si
.last:
    loop .n
    mov word [wa_saddr6+4], UD_PORT
    ret

; wa_sclick - a press in the content that is not a button's (os88ui
; chains it): CX = x, DX = y, SI = the window. An open list takes any
; press; else a drop-down's box drops its list, the field takes the focus
wa_sclick:
    push si
    cmp byte [wa_abon], 0
    je .live
    call wa_sdismiss
    jmp .out
.live:
    call wa_splace
    call wa_sopen               ; (an open list first: it takes the press,
    jc .boxes                   ; wherever it lands)
    call os88ui_drpress
    call wa_stake
    jmp .out
.boxes:
    xor di, di
.d:
    mov bx, [wa_sdrs+di]
    call os88ui_drpress
    or ah, ah
    jnz .took
    inc di
    inc di
    cmp di, WS_NDR * 2
    jb .d
    cmp byte [wa_splay], 3      ; the field
    jne .out
    mov si, wa_sfld
    call os88line_click
    jc .out
    mov byte [wa_sfoc], WS_FLD
    mov byte [wa_sdfld], 0FFh   ; (its caret where the press put it)
    jmp .out
.took:
    mov bx, di                  ; (the focus where the press was)
    shr bx, 1
    mov [wa_sfoc], bl
    mov bx, [wa_sdrs+di]
    call wa_stake
.out:
    pop si
    ret

; wa_stake - BX = a drop-down's record, AX/CF its answer (os88ui_drpress,
; os88ui_drup): a pick is the setting's, a list that came down without its
; bank is a repaint; the buttons out of the way while a list is up
wa_stake:
    pushf
    cmp al, 0FFh
    je .nopick
    push ax
    push bx
    xor si, si                  ; (its place in wa_sdrs)
.f:
    cmp bx, [wa_sdrs+si]
    je .got
    inc si
    inc si
    jmp .f
.got:
    mov bx, si
    shr si, 1
    mov [wa_sdsel+si], al       ; (the control drew its own box)
    call wa_spick
    call wa_sfocfix
    cmp byte [wa_splay], 3      ; (internet picked: the address next)
    jne .nf
    cmp si, 4
    jne .nf
    mov byte [wa_sfoc], WS_FLD
.nf:
    pop bx
    pop ax
.nopick:
    popf
    jnc .norep
    call wa_srepaint
.norep:
    mov word [wa_sbrec+OS88UI_BT_N], 0
    call wa_sopen               ; (a list up: no buttons under it)
    jnc .out
    mov word [wa_sbrec+OS88UI_BT_N], 2
.out:
    ret

; wa_sbtnup - ONMOUSEUP: a release over a list's item picks it; a button
; fired (os88ui_btnup)
wa_sbtnup:
    push si
    call wa_splace
    call wa_sopen
    jc .btn
    call os88ui_drup
    call wa_stake
    jmp .out
.btn:
    mov bx, wa_sbrec
    call os88ui_btnup           ; AX = the button + 1, or 0
    cmp ax, 1
    jne .k
    call wa_sstart
    jmp .out
.k:
    cmp ax, 2
    jne .out
    call wa_about
.out:
    pop si
    ret

; wa_sbtndrag - ONDRAG: a list's highlight follows, or a button's press
wa_sbtndrag:
    push bx
    call wa_sopen
    jc .btn
    call os88ui_drdrag
    pop bx
    ret
.btn:
    pop bx
    mov bx, wa_sbrec
    jmp os88ui_btndrag

; wa_sstart - Play (alone: the arena now) or Ready (linked)
wa_sstart:
    cmp byte [wa_splay], 0
    jne wa_lkready
    mov al, [wa_lkarena]
    mov [wa_arena], al
    mov al, [wa_lkdiff]
    mov [wa_diff], al
    mov al, [wa_lkbots]
    mov [wa_lknb], al
    call wa_loadarena
    jc .out
    cmp byte [wa_lkgmode], 0
    je .run
    mov si, [wa_gflp]
    cmp word [si], 2
    je .run
    mov si, wa_t_ctfa
.toast:
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    ret
.run:
    mov ax, wa_main
    mov bx, [wa_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    mov byte [wa_lkfull], 1
.out:
    ret

; --- data ---------------------------------------------------------------------

wa_sdrs:    dw wa_sdr_a, wa_sdr_c, wa_sdr_b, wa_sdr_s, wa_sdr_p, wa_sdr_m
%macro WA_SDROP 3                ; a drop-down's record: its items, how many
%1:         dw 0, 0, 0, 0, %2, %3, 0, 0
            db 0, 0FFh
            dw 0, 0, 0, 0
%endmacro
WA_SDROP wa_sdr_a, wa_lt_anames, WA_NARENA
WA_SDROP wa_sdr_c, wa_si_cols, 2
WA_SDROP wa_sdr_b, wa_si_bots, 4
WA_SDROP wa_sdr_s, wa_lt_dnames, 3
WA_SDROP wa_sdr_p, wa_sp_names, 4
WA_SDROP wa_sdr_m, wa_si_mode, 2

wa_si_cols:     dw wa_sw_40, wa_sw_80
wa_sw_40:       db '40', 0
wa_sw_80:       db '80', 0
wa_si_bots:     dw wa_sbn_0, wa_sbn_1, wa_sbn_2, wa_sbn_3
wa_sbn_0:       db 'none', 0
wa_sbn_1:       db '1', 0
wa_sbn_2:       db '2', 0
wa_sbn_3:       db '3', 0
wa_sp_names:    dw wa_sp_0, wa_sp_1, wa_sp_2, wa_sp_3
wa_sp_0:        db 'alone', 0
wa_sp_1:        db 'serial cable', 0
wa_sp_2:        db 'local network', 0
wa_sp_3:        db 'internet', 0
wa_si_mode:     dw wa_smd_0, wa_smd_1   ; (CTF-PLAN 7)
wa_smd_0:       db 'free-for-all', 0
wa_smd_1:       db 'capture flag', 0

wa_sb_play:     db 'Play', 0
wa_sb_rdy:      db 'Ready', 0
wa_sb_nrdy:     db 'Not ready', 0
wa_sb_keys:     db 'Keys', 0
wa_sbflags:     dw OS88UI_FILL, OS88UI_FILL
    OS88UI_BTNREC wa_sbrec, wa_sbrects, wa_sblab, wa_sbflags, 2, wa_splace

wa_ablines: dw wa_ab0, wa_ab1, wa_ab2, wa_ab3, wa_ab4, wa_ab5, wa_ab6
            dw wa_ab7, wa_ab8, wa_ab9, wa_ab10, wa_ab11, wa_ab12, 0
wa_ab0:     db 'WireArena', 0
wa_ab1:     db 0
wa_ab2:     db OS88UI_ABLT, 'Mouse looks; W A S D move; arrows', 0
wa_ab3:     db OS88UI_ABLT, 'turn and pitch; Space jumps.', 0
wa_ab4:     db OS88UI_ABLT, 'Click or Ctrl fires; R reloads.', 0
wa_ab5:     db OS88UI_ABLT, '1-6: pistol, shotgun, chaingun,', 0
wa_ab6:     db OS88UI_ABLT, 'grenades, rockets, railgun.', 0
wa_ab7:     db OS88UI_ABLT, 'Tab: scores. -/+: 40/80 columns.', 0
wa_ab8:     db OS88UI_ABLT, 'F1-F5 detail. F: view. V: vsync.', 0
wa_ab9:     db OS88UI_ABLT, 'G: floor grid. T: treads. L: tops.', 0
wa_ab10:    db OS88UI_ABLT, 'F10: fps; all the stats; off.', 0
wa_ab11:    db OS88UI_ABLT, 'B, P: benches (the stats on).', 0
wa_ab12:    db OS88UI_ABLT, 'Esc leaves the arena.', 0
