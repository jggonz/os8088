; wasetup.inc - the opening window: the MATCH SETUP (docs/plans/
; completed/WIREARENA-NET-PLAN.md 2 and 9.4). Every control on it is the system's own:
;
;   - five DROP-DOWNS (apps/os88ui.inc, SPEC.md 13.14): Arena, Columns,
;     Bots, Skill, Play - a joined player's Arena, Bots and Skill greyed,
;     the host's to choose (and showing the host's);
;   - one TEXT FIELD (apps/os88line.inc, SPEC.md 83) for the address,
;     beside Play and only while Play is internet;
;   - a second for the player's NICK, right of Mode: up to eight
;     characters, upper case, sent in every HELLO (walink.inc) and shown on
;     the lobby's rows, the scoreboard and the HUD's kill message;
;   - two BUTTONS (os88ui): Play alone / Ready linked, and Keys - which
;     puts the key list up as the standard ABOUT CARD (SPEC.md 12.2), the
;     same card the system menu's About shows.
;
; The window's text rows (walink.inc's wa_lkrows) are row 0, what the link
; is doing, and rows 1 to 5, the players once linked. THE LAYOUT (the owner,
; before release): Nick, Play and Keys on the bottom row, 4 from the
; content's bottom; alone, the four rows of drop-downs centred between row
; 0's text and it; linked, the drop-downs under row 0 and the players' rows
; between them and the bottom row, as many as fit (three on a CGA, whose
; window is fitted shorter). wa_slayout switches, repainting once. Each control is drawn
; when it CHANGED (its value, its greying, the focus), never all of them for
; a click or a key: the owner saw the whole window flash on every one,
; which was every row and both buttons redrawn per input (9.4).
;
; THE PAINTER IS OPAQUE BY CONSTRUCTION, WireFleet's shape (SPEC.md 114):
; every element draws its own ground, and wa_sground fills only the gaps
; between them - so a repaint of our own (the About card going, a list
; that could not bank) writes each pixel once, with no white first. A
; kernel PAINT skips the ground: the kernel filled the content white.

WS_W        equ 408             ; the content box (the template: 410 x 174 -
                                ; wide for Win at's "10 Captures",
                                ; MODES-PLAN 2, and a cell more for Mode's
                                ; "Capture the Flag" and "King of the Hill",
                                ; sixteen cells: the owner, 2026-11-15)
WS_RM       equ 400             ; ...and its right margin, past every
                                ; control and row (the owner: the drop-downs
                                ; off the edge, as the left's 8 px are)
WS_H        equ 155
WS_LX       equ 0               ; the left column: a marker cell and a
WS_LDX1     equ 56              ; label (seven cells), then its drop-down
WS_LDX2     equ 207
WS_RX       equ 208             ; the right column: marker and label (nine
WS_RDX1     equ 280             ; cells), then its drop-down
WS_RDX2     equ 399
WS_Y1       equ 0               ; THE GROUP's four rows of drop-downs, 16
WS_Y2       equ 18              ; high, from its top - which is where the
WS_Y3       equ 36              ; layout puts it (wa_sgy): alone, CENTRED
WS_Y4       equ 54              ; between row 0's text and the bottom row
WS_GH       equ 70              ; (the owner, before release); linked,
WS_GLINK    equ 14              ; under row 0, the players' rows under it
WS_LOBYR    equ WS_GH + 2       ; (from the group's top, 10 apart)
WS_BOTM     equ 20              ; THE BOTTOM ROW, the nick and the buttons:
                                ; both layouts, 4 from the content's bottom
                                ; as row 0's text is from its top - the
                                ; content's height the window's own (a CGA's
                                ; is fitted shorter: 136, not 155)
; THE GROUND's y values (wa_sgnd): an offset from one of six bases, the
; base in bits 12-14, wa_splace's wa_sybase
%define YT(o) (o)
%define YG(o) ((o) + 1 * 4096)
%define YGM1 (2 * 4096)
%define YB(o) ((o) + 3 * 4096)
%define YBM1 (4 * 4096)
%define YEND (5 * 4096)
WS_DH       equ 16
WS_FX1      equ 216             ; the address field, right of Play
WS_FX2      equ 399
                                ; (the buttons: the bottom row, right of the
                                ; nick - wa_sby)
WS_BTNW     equ 96              ; (Play: "Not ready" fits)
WS_B1X      equ 216
WS_BTNW2    equ 80              ; (Keys)
WS_BTNH     equ 16
WS_B2X      equ 320
WS_LOBY     equ WS_GLINK + WS_LOBYR             ; the players' rows (walink.inc), 10 apart
WS_NDR      equ 8               ; the drop-downs. THE FOCUS'S STOPS, in the
WS_FLD      equ 5               ; window's reading order: the first five
WS_FMODE    equ 6               ; drop-downs, then THE SLOT right of Play -
                                ; the address field while Play is Internet,
                                ; Detail (drop-down 7) the rest of the time
                                ; (the owner, 2026-11-15) - then Mode
                                ; (drop-down 5, the row under Play: CTF-
WS_FGOAL    equ 7               ; PLAN 7), then Win at (drop-down 6, right
WS_NICK     equ 8               ; of Mode), then the nick's field, under Mode
WS_DDET     equ 7               ; (Detail's drop-down)
WS_NKX      equ WS_LDX1         ; ...its left (its label seven cells)
WS_NKMAX    equ 9               ; ...its buffer: eight and the NUL

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
    mov word [wa_snfld+LN_BUF], wa_snick
    mov word [wa_snfld+LN_MAX], WS_NKMAX
    mov bx, wa_sbrec
    mov dx, wa_sclick
    call os88ui_btninit
    call wa_sarlist
    call wa_sforget
    ret

; wa_sarlist - the mode's lists (MODES-PLAN 2, 5): the Arena drop-down's
; items the arenas that play the mode, in the setup's order (and the
; arena each item is: wa_salx), the current one moved to the first of them
; if it does not; Win at's items the mode's. Keeps all but the flags
wa_sarlist:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [wa_lkgmode]
    mov dl, 1
    shl dl, cl                  ; DL = the mode's bit
    xor bx, bx                  ; BX = an item of the whole list, in order
    xor di, di                  ; DI = the filtered list's count
    mov ch, 0FFh                ; CH = the current arena's place in it
.i:
    mov al, bl
    call wa_sarmap              ; AL = its arena
    mov si, ax
    and si, 0x00FF
    test [wa_amodes+si], dl
    jz .n
    mov [wa_salx+di], al
    cmp al, [wa_lkarena]
    jne .a
    mov ax, di
    mov ch, al
.a:
    mov si, bx
    shl si, 1
    mov ax, [wa_lt_anames+si]
    mov si, di
    shl si, 1
    mov [wa_salp+si], ax
    inc di
.n:
    inc bx
    cmp bx, WA_NARENA
    jb .i
    mov byte [wa_salx+di], 0FFh ; ...and the last: Rotation... (MODES-PLAN
    mov si, di                  ; 5), its window
    shl si, 1
    mov word [wa_salp+si], wa_s_rot
    inc di
    mov [wa_sdr_a+OS88UI_DR_N], di
    cmp ch, 0FFh
    jne .have
    mov al, [wa_salx]           ; (not this mode's: its first)
    mov [wa_lkarena], al
.have:
    mov bl, [wa_lkgmode]        ; Win at's items
    xor bh, bh
    shl bx, 1
    mov ax, [wa_sglist+bx]
    mov [wa_sdr_g+OS88UI_DR_ITEMS], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wa_sforget - nothing drawn yet: every control drawn by the next pass
wa_sforget:
    push ds
    pop es
    mov di, wa_sdsel            ; (sel, greying for each; the focus; the
    mov al, 0FFh                ; field's place)
    mov cx, WS_NDR * 2 + 3      ; (and the nick's)
    rep stosb
    ret

; --- the controls' state ----------------------------------------------------

; wa_sval - BX = a drop-down x 2: AL = the pick its setting says, AH = how
; many items it has now, DL = 1 when it is greyed (a joined player: the
; host's arena, bots and skill). Keeps all but AX, DX
wa_sval:
    push bx
    xor dl, dl
    cmp bx, 4 * 2               ; (arena 0, bots 2, skill 3 are the host's;
    je .mine                    ; Play, Columns and Detail each player's
    cmp bx, 1 * 2               ; own)
    je .mine
    cmp bx, WS_DDET * 2
    je .mine
    call wa_lklinked
    jz .mine
    call wa_lkhost
    jnz .mine
    inc dx
.mine:
    jmp [wa_svals+bx]
.arena:
    mov ah, [wa_sdr_a+OS88UI_DR_N] ; (the mode's arenas: wa_sarlist)
    xor bx, bx
    mov al, [wa_lkarena]
.ax:
    cmp [wa_salx+bx], al
    je .af
    inc bx
    cmp bl, ah
    jb .ax
    xor bx, bx
.af:
    mov al, bl
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
    mov al, [wa_lkgmode]        ; (the host's, as the arena is) - the list's
    mov bx, wa_smodes           ; order free-for-all, team deathmatch, CTF
    call .find
    mov ah, WA_NMODES
    jmp .out
.goal:
    mov bl, [wa_lkgmode]        ; (the host's too) - the mode's own pick
    xor bh, bh
    mov al, [wa_sgoal+bx]
    mov ah, WA_NGOAL
    jmp .out
.detail:
    mov al, [wa_level]          ; (the CPU's tier picked it at the start:
    mov ah, WA_NLEV             ; Medium on an 8088, High on a 286 or
    jmp .out                    ; better - F1-F5 in the match set it too)
.find:                          ; AL = a value, BX = a table: AL = its place
    push cx
    xor cx, cx
.fl:
    cmp [bx], al
    je .ff
    inc bx
    inc cx
    cmp cx, WA_NMODES
    jb .fl
    xor cx, cx
.ff:
    mov al, cl
    pop cx
    ret
.out:
    pop bx
    ret
wa_svals:   dw wa_sval.arena, wa_sval.cols, wa_sval.bots, wa_sval.skill
            dw wa_sval.play, wa_sval.mode, wa_sval.goal, wa_sval.detail

; wa_spick - BX = a drop-down x 2, AL = its new pick: the setting it names
; set, and whatever that implies
wa_spick:
    jmp [wa_spicks+bx]
.arena:
    xor bh, bh                  ; (the mode's list: wa_sarlist's)
    mov bl, al
    mov al, [wa_salx+bx]
    cmp al, 0FFh
    je .rot
    mov [wa_lkarena], al
    ret
.rot:
    mov byte [wa_sdsel], 0FFh   ; (Rotation...: its window, the arena as it
    jmp wa_ropen                ; was drawn again)
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
    xor bh, bh                  ; (the list's order to the game's numbers)
    mov bl, al
    mov al, [wa_smodes+bx]
    mov [wa_lkgmode], al
    call wa_rdone               ; (another mode's rotation: its window down)
    call wa_sarlist             ; (its arenas, its Win at)
    mov byte [wa_sdsel], 0FFh   ; (the Arena and Win at drawn again)
    mov byte [wa_sdsel+6], 0FFh
    ret
.goal:
    mov bl, [wa_lkgmode]
    xor bh, bh
    mov [wa_sgoal+bx], al
    ret
.detail:
    call wa_setlevel            ; (the match's renderer, if one is bound,
    cmp word [wa_rds], 0        ; as F1-F5's)
    je .dn
    call wa_rsync
.dn:
    ret
.ser:
    call wa_lkcable
    call lk_disconnect
    call lk_auto
    mov word [lk_blk+LK_MSG], lk_m_search
    ret
wa_spicks:  dw wa_spick.arena, wa_spick.cols, wa_spick.bots, wa_spick.skill
            dw wa_spick.play, wa_spick.mode, wa_spick.goal, wa_spick.detail

; wa_sarmap - AL = the Arena list's item: AL = its arena; wa_sitem - AL =
; an arena: AL = its item. The list is the owner's order (arenagen's
; ARENA_ORDER: open, pillars, crossing, the heights ...) and the arenas keep
; their numbers (the benches, the poses, the rows and the wire all name
; them). Keep all but AL
wa_sarmap:
    push bx
    mov bx, wa_saord
.x:
    xlatb
    pop bx
    ret
wa_sitem:
    push bx
    mov bx, wa_sitm
    jmp short wa_sarmap.x

; wa_sset - BX = a drop-down x 2, AH = -1 or +1: its pick one along, round
; (Left and Right on the focused control). A greyed one says whose it is
wa_sset:
    push ax
    call wa_sval
    or bx, bx                   ; (the Arena: round its arenas, Rotation...
    jnz .nr                     ; left out - R, or a pick, opens it)
    dec ah
.nr:
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
    push dx                     ; THE LAYOUT (the owner, before release):
    push cx                     ; the bottom row 4 from the content's
    call OSAPI_WM_GEOM          ; bottom; the group under row 0 linked,
    mov cx, dx                  ; alone centred between row 0's text
    pop dx                      ; (ends at 12) and the bottom row
    sub cx, WS_BOTM
    mov [wa_sby], cx            ; (the bottom row's top)
    mov [wa_sybase+3*2], cx
    dec cx
    mov [wa_sybase+4*2], cx
    add cx, WS_BOTM             ; (the content's last line: h - 1)
    mov [wa_sybase+5*2], cx
    mov cx, WS_GLINK
    cmp byte [wa_slay], 0
    jne .gy
    mov cx, [wa_sby]            ; (by - 12 - 70 split in two, 12 on)
    sub cx, 12 + WS_GH
    sar cx, 1
    add cx, 12
.gy:
    mov [wa_sgy], cx
    mov [wa_sybase+1*2], cx
    dec cx
    mov [wa_sybase+2*2], cx
    pop dx
    push dx                     ; (the bottom row's: the content's top)
    add dx, [wa_sgy]            ; DX = the group's top, the layout's
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
    mov cx, ax                  ; the nick's field, right of Mode
    add cx, WS_NKX
    mov [wa_snfld+LN_X1], cx
    add cx, WS_LDX2 - WS_NKX
    mov [wa_snfld+LN_X2], cx
    pop dx                      ; (the content's top again)
    mov cx, dx
    add cx, [wa_sby]
    mov [wa_snfld+LN_Y1], cx
    add cx, WS_DH - 1
    mov [wa_snfld+LN_Y2], cx
    add ax, WS_B1X              ; the buttons, right of the nick
    add dx, [wa_sby]
    mov [wa_sbrects], ax
    mov [wa_sbrects+2], dx
    add ax, WS_BTNW - 1
    mov [wa_sbrects+4], ax
    add dx, WS_BTNH - 1
    mov [wa_sbrects+6], dx
    add ax, WS_B2X - WS_B1X - WS_BTNW + 1
    mov [wa_sbrects+8], ax
    sub dx, WS_BTNH - 1
    mov [wa_sbrects+10], dx
    add ax, WS_BTNW2 - 1
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
            dw WS_RDX1, WS_Y4, WS_RDX2, WS_Y4 + WS_DH - 1
            dw WS_RDX1, WS_Y3, WS_RDX2, WS_Y3 + WS_DH - 1   ; (Detail's)
; ...each focus stop's label: its cell's x and y, its text (a marker cell
; first)
wa_slabs:   dw WS_LX, WS_Y1 + 4, wa_sl_a
            dw WS_RX, WS_Y1 + 4, wa_sl_c
            dw WS_LX, WS_Y2 + 4, wa_sl_b
            dw WS_RX, WS_Y2 + 4, wa_sl_s
            dw WS_LX, WS_Y3 + 4, wa_sl_p
            dw WS_RX, WS_Y3 + 4, wa_sl_d     ; (Detail's: WS_FLD, the slot's
                                             ; while there is no field)
            dw WS_LX, WS_Y4 + 4, wa_sl_m
            dw WS_RX, WS_Y4 + 4, wa_sl_g     ; (Win at's: WS_FGOAL)
            dw WS_LX, 4, wa_sl_n             ; (the nick's: WS_NICK - from
                                             ; the bottom row's top)
wa_sl_a:    db ' Arena ', 0
wa_sl_c:    db ' Columns ', 0
wa_sl_b:    db ' Bots  ', 0
wa_sl_s:    db ' Skill   ', 0
wa_sl_p:    db ' Play  ', 0
wa_sl_m:    db ' Mode  ', 0
wa_sl_n:    db ' Nick  ', 0
wa_sl_g:    db ' Win at  ', 0
wa_sl_d:    db ' Detail  ', 0

; THE GROUND: every rect of the content no control and no row covers
; (content coordinates, inclusive). THE SLOT right of Play is the address
; field's (Internet) or Detail's label and drop-down (the rest): the two
; rects BEFORE the table are ground only with Detail there (over and under
; its label, the field's place), the one AFTER it only with the field (the
; band of Detail's label's first cell left of the field)
%macro WS_GROUND 2              ; label, 1: the players' rows (linked)
            dw WS_FX1, YG(WS_Y3), WS_RDX1 - 1, YG(WS_Y3 + 3)
            dw WS_FX1, YG(WS_Y3 + 12), WS_RDX1 - 1, YG(WS_Y3 + 15)
%1:         dw 0, YT(0), WS_RM - 1, YT(3)
            dw 0, YT(4), 7, YT(11)
            dw 0, YT(12), WS_RM - 1, YGM1
%assign wsg_r 0
%rep 4                          ; (the group's four rows, and the gaps
 %if wsg_r == 0                 ; under them)
  %assign wsg_y WS_Y1
  %assign wsg_n WS_Y2
 %elif wsg_r == 1
  %assign wsg_y WS_Y2
  %assign wsg_n WS_Y3
 %elif wsg_r == 2
  %assign wsg_y WS_Y3
  %assign wsg_n WS_Y4
 %else
  %assign wsg_y WS_Y4
 %endif
            dw 0, YG(wsg_y), WS_LDX1 - 1, YG(wsg_y + 3)
            dw 0, YG(wsg_y + 12), WS_LDX1 - 1, YG(wsg_y + 15)
 %if wsg_r == 2                 ; (Play: the slot right of it)
            dw WS_LDX2 + 1, YG(wsg_y), WS_FX1 - 1, YG(wsg_y + 3)
            dw WS_LDX2 + 1, YG(wsg_y + 12), WS_FX1 - 1, YG(wsg_y + 15)
 %else
            dw WS_RX, YG(wsg_y), WS_RDX1 - 1, YG(wsg_y + 3)
            dw WS_RX, YG(wsg_y + 12), WS_RDX1 - 1, YG(wsg_y + 15)
 %endif
 %if wsg_r < 3
            dw 0, YG(wsg_y + 16), WS_RM - 1, YG(wsg_n - 1)
 %endif
 %assign wsg_r wsg_r + 1
%endrep
%if %2
            dw 0, YG(WS_Y4 + 16), WS_RM - 1, YG(WS_LOBYR - 1)
            dw 0, YG(WS_LOBYR), 7, YBM1     ; (the players' rows: their text
            dw 8, YG(WS_LOBYR + 8), WS_RM - 1, YG(WS_LOBYR + 9)  ; 8 high, 10
            dw 8, YG(WS_LOBYR + 18), WS_RM - 1, YG(WS_LOBYR + 19) ; apart, as
            dw 8, YG(WS_LOBYR + 28), WS_RM - 1, YG(WS_LOBYR + 29) ; many as
            dw 8, YG(WS_LOBYR + 38), WS_RM - 1, YG(WS_LOBYR + 39) ; fit over
            dw 8, YG(WS_LOBYR + 48), WS_RM - 1, YBM1  ; the bottom row)
%else
            dw 0, YG(WS_Y4 + 16), WS_RM - 1, YBM1
%endif
            dw 0, YB(0), WS_LDX1 - 1, YB(3)
            dw 0, YB(12), WS_LDX1 - 1, YB(15)
            dw WS_LDX2 + 1, YB(0), WS_B1X - 1, YB(15)
            dw WS_B1X + WS_BTNW, YB(0), WS_B2X - 1, YB(15)
            dw 0, YB(16), WS_RM - 1, YEND
            dw WS_RM, YT(0), WS_W - 1, YEND
%%end:
%1_n        equ (%%end - %1) / 8
            dw WS_LDX2 + 1, YG(WS_Y3 + 4), WS_FX1 - 1, YG(WS_Y3 + 11)
%endmacro
WS_GROUND wa_sgnd, 0            ; ALONE: the group centred
WS_GROUND wa_sgndl, 1           ; LINKED: the players' rows too

; wa_sgsel - SI = the ground table of the layout ([wa_slay]), CX = its
; rects (the slot's either side of them: WS_GROUND). Keeps all but SI, CX
wa_sgsel:
    mov si, wa_sgnd
    mov cx, wa_sgnd_n
    cmp byte [wa_slay], 0
    je .out
    mov si, wa_sgndl
    mov cx, wa_sgndl_n
.out:
    ret

; wa_slayout - the layout as the link stands (the owner, before release):
; ALONE the group centred, LINKED under row 0 with the players' rows under
; it; Nick, Play and Keys at the bottom in both. A change while no list is
; down repaints the window whole in the new one (CF = 1: wa_srepaint has
; drawn everything, rows and all). Keeps all but AX, the flags
wa_slayout:
    call wa_lklinked            ; (ZF = 0: linked)
    mov al, 0
    jz .l
    inc ax
.l:
    cmp al, [wa_slay]
    je .same
    push bx
    call wa_sopen               ; (a list down: later)
    pop bx
    jnc .same
    mov [wa_slay], al           ; (wa_splace places the group by it)
    cmp byte [wa_lkfull], 0     ; (a paint: everything is drawn anyway)
    jne .same
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call wa_srepaint
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
.same:
    clc
    ret

; wa_sground - the gaps white (a repaint of our own; the kernel's PAINT
; filled them already). The clip is armed
wa_sground:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call wa_sgsel               ; SI = the layout's table, CX its count
    inc cx                      ; (the slot's: the field's strip after the
    cmp byte [wa_splay], 3      ; table, or Detail's two before it)
    je .r
    sub si, 2 * 8
    inc cx
.r:
    push cx
    call wa_srect
    mov di, [si+2]              ; (a rect off the group's top: none of it
    and di, 0x7000              ; on the bottom row - the players' rows
    cmp di, 0x1000              ; that do not fit on a short content, a
    jne .f                      ; CGA's)
    mov cx, [wa_sby]
    add cx, [wa_sorg+2]
    dec cx
    cmp dx, cx
    jle .f0
    mov dx, cx
.f0:
    mov cx, [si+4]
    add cx, [wa_sorg]
    cmp bx, dx
    jg .fn
.f:
    call OSAPI_GFX_FILL
.fn:
    pop cx
    add si, 8
    loop .r
    mov si, wa_snfld            ; the nick's field's inside, always
    call wa_sfgnd
    cmp byte [wa_splay], 3      ; ...and the address's, while there
    jne wa_sfgnd.out
    mov si, wa_sfld
; wa_sfgnd - SI = a field: its inside that its own draw does not cover -
; the bands over and under its text, and the inset left of it
wa_sfgnd:
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
    mov bx, [si+2]              ; (the ground's y values: an offset and its
    call .y                     ; base, wa_sgnd's - the drop-downs' and
    push bx                     ; fields' rects, absolute, are not read
    mov bx, [si+6]              ; here)
    call .y
    mov dx, bx
    pop bx
    mov ax, [si]
    add ax, [wa_sorg]
    mov cx, [si+4]
    add cx, [wa_sorg]
    ret
.y:                             ; BX = an encoded y: BX = the screen's
    push ax
    push cx
    mov ax, bx
    and bx, 0x0FFF
    mov cl, 11
    shr ax, cl
    and ax, 0x0E                ; (the base x 2)
    xchg ax, bx
    add ax, [wa_sybase+bx]
    add ax, [wa_sorg+2]
    xchg ax, bx
    pop cx
    pop ax
    ret

; --- drawing --------------------------------------------------------------------

; wa_sdraw - the controls, each drawn if it changed since it was drawn (all
; of them with [wa_lkfull]); the clip is taken first through wa_lkclipon
; (a tick's) or is the paint's. CF = 1: no clip, nothing drawn
wa_sdraw:
    cmp byte [wa_lkfull], 0     ; A LIST DOWN: nothing of ours drawn while
    jne .go                     ; it is - the controls under it are its to
    push bx                     ; cover (the owner: with the focus in the
    call wa_sopen               ; nick, a click on a drop-down drew the
    pop bx                      ; field back over its list, and the list's
    jc .go                      ; bank put its caret back when it went).
    clc                         ; Their records stay as last drawn, so the
    ret                         ; first pass after it catches up on them
.go:
    call wa_splace
    xor bx, bx                  ; THE DROP-DOWNS
.d:
    call wa_sval                ; AL = the pick, AH = the items, DL greyed
    mov si, [wa_sdrs+bx]
    cmp byte [si+OS88UI_DR_OPEN], 0
    jne .dn                     ; (open: the list is the control's)
    mov di, bx
    shr di, 1
    cmp di, WS_DDET             ; (Detail: not while the field has its slot -
    jne .dv                     ; and drawn whole when it is back)
    cmp byte [wa_splay], 3
    jne .dv
    mov byte [wa_sdsel+di], 0FFh
    jmp short .dn
.dv:
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
    cmp al, WS_NICK
    jbe .la
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
    mov ah, [wa_sdfld]          ; (AH = as it was drawn)
    mov [wa_sdfld], al
    cmp byte [wa_lkfull], 0     ; (a paint: its ground is down already)
    jne .fg
    cmp al, ah                  ; THE SLOT CHANGING HANDS (the drop-down
    je .fg                      ; loop above drew Detail's box, if back):
    or al, al                   ; the ground the newcomer's own draw does
    jz .fx                      ; not cover, then its label
    or ah, ah
    jnz .fg
.fx:
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call wa_sgsel               ; SI = the table, CX its count
    pop ax
    or al, al
    jnz .fs
    sub si, 2 * 8               ; (Detail's: the two before the table)
    call wa_srect
    call OSAPI_GFX_FILL
    add si, 8
    call wa_srect
    call OSAPI_GFX_FILL
    mov al, WS_FLD
    call wa_slabel
    jmp .ok
.fs:
    shl cx, 1                   ; (the field's: the strip after it, and
    shl cx, 1                   ; inside it)
    shl cx, 1
    add si, cx
    call wa_srect
    call OSAPI_GFX_FILL
    mov si, wa_sfld
    call wa_sfgnd
    jmp short .fdr
.fg:
    or al, al
    jz .ok
.fdr:
    mov si, wa_sfld
    call os88line_draw
.ok:
    mov al, 1                   ; THE NICK's FIELD: always there, its caret
    cmp byte [wa_sfoc], WS_NICK ; while it has the focus
    jne .nf
    inc ax
.nf:
    mov byte [wa_snfld+LN_FOCUS], 0
    cmp al, 2
    jne .nfo
    mov byte [wa_snfld+LN_FOCUS], 1
.nfo:
    cmp byte [wa_lkfull], 0
    jne .nfd
    cmp al, [wa_sdnk]
    je .nok
.nfd:
    call wa_lkclipon
    jc .no
    mov [wa_sdnk], al
    mov si, wa_snfld
    call os88line_draw
.nok:
    clc
    ret
.no:
    stc
    ret

; wa_slabel - AL = a drop-down: its label, the focus's marker in its first
; cell. CF = 1 no clip. Keeps all but the flags
wa_slabel:                      ; (AL a focus STOP: wa_sfoc's)
    cmp al, WS_FLD              ; (the slot's: Detail's, not the field's)
    jne .lv
    cmp byte [wa_splay], 3
    je .out
.lv:
    cmp al, WS_NICK
    ja .out
    call wa_lkclipon
    jc .no
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    xor bh, bh
    mov ah, ' '                 ; its marker, by its stop
    cmp al, [wa_sfoc]
    jne .mk
    mov ah, '>'                 ; (the font has no triangles)
.mk:
    push ax
    mov si, bx                  ; (x 6: the label's three words)
    shl si, 1
    add si, bx
    shl si, 1
    mov cx, [wa_slabs+si]
    add cx, [wa_sorg]
    mov dx, [wa_slabs+si+2]
    add dx, [wa_sorg+2]
    cmp bl, WS_NICK             ; (the group's labels from its top; the
    jae .lb                     ; nick's from the bottom row's)
    add dx, [wa_sgy]
    jmp short .lc
.lb:
    add dx, [wa_sby]
.lc:
    mov si, [wa_slabs+si+4]
    pop ax
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
    cmp byte [wa_sfoc], WS_NICK ; THE NICK's keys: anything printable but
    jne .naddr                  ; Space, letters in upper case, and the
    call wa_snavkey             ; editing keys
    je .nav
    cmp al, ' '
    jb .nedit
    cmp al, 127
    jae .nedit
    cmp al, 'a'
    jb .nedit
    cmp al, 'z'
    ja .nedit
    sub al, 'a' - 'A'
.nedit:
    mov si, wa_snfld
    call wa_sedit
    call wa_snkset
    jmp .done
.naddr:
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
    call wa_sedit
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
    cmp ah, 'r'                 ; R: the rotation's window
    jne .nr
    call wa_ropen
    jmp .done
.nr:
    clc
    ret
.up:
    mov al, [wa_sfoc]           ; (every stop always there: the slot's the
    or al, al                   ; field's or Detail's)
    jz .done
    dec ax
    jmp short .fset
.down:
    mov al, [wa_sfoc]
    cmp al, WS_NICK
    jae .done
    inc ax
.fset:
    mov [wa_sfoc], al
    jmp .done
.left:
    mov ah, -1
    jmp .chg
.right:
    mov ah, 1
.chg:
    mov bl, [wa_sfoc]
    cmp bl, WS_NICK             ; (the fields: no pick)
    je .done
    cmp bl, WS_FLD
    jb .dd
    ja .dm
    cmp byte [wa_splay], 3
    je .done
    mov bl, WS_DDET + 1         ; (the slot's: Detail, drop-down 7)
.dm:
    dec bx                      ; (Mode's stop: drop-down 5)
.dd:
    xor bh, bh
    shl bx, 1
    call wa_sset
    jmp .done
.enter:
    call wa_sstart
    jmp .done
.keys:
    call wa_about
.done:
    stc
    ret

; wa_sedit - SI = a field with the focus, AX = a key: the edit it makes,
; drawn (os88line). Clobbers AX, BX, CX, DX, DI
wa_sedit:
    mov byte [si+LN_FOCUS], 1   ; (it has the focus NOW: the tick that
    push word [si+LN_VIEW]      ; draws its caret may not have run yet)      ; (as they were: os88line_edit's
    push word [si+LN_LEN]       ; arguments)
    push word [si+LN_CAR]
    call os88line_key
    pop dx
    pop bx
    pop ax
    jc .out                     ; (not an edit)
    push bx
    mov bx, [wa_win]
    call OSAPI_WM_CLIP_SET      ; (CF = 1: nothing of us to see)
    pop bx
    jc .out
    call os88line_edit
.out:
    ret

; wa_snavkey - AX = a key: ZF = 1 when it is the page's on a field (Up,
; Down, Tab, Enter, Space - Ready, as anywhere on the page)
wa_snavkey:
    cmp ah, 0x48
    je .out
    cmp ah, 0x50
    je .out
    cmp al, 9
    je .out
    cmp al, 13
    je .out
    cmp al, ' '
.out:
    ret

; wa_snkset - the nick typed: wa_nick, space-padded, what the HELLOs send
wa_snkset:
    push ds
    pop es
    mov si, wa_snick
    mov di, wa_nick
    mov cx, WA_NNAME
.c:
    lodsb
    or al, al
    jz .pad
    stosb
    loop .c
    ret
.pad:
    mov al, ' '
    rep stosb
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
    cmp di, WS_DDET * 2         ; (Detail: not while the field has its slot)
    jne .dp
    cmp byte [wa_splay], 3
    je .dx
.dp:
    mov bx, [wa_sdrs+di]
    call os88ui_drpress
    or ah, ah
    jnz .took
.dx:
    inc di
    inc di
    cmp di, WS_NDR * 2
    jb .d
    mov si, wa_snfld            ; the nick's field
    call os88line_click
    jc .naddr
    mov byte [wa_sfoc], WS_NICK
    mov byte [wa_sdnk], 0FFh    ; (its caret where the press put it)
    jmp .out
.naddr:
    cmp byte [wa_splay], 3      ; the address's field
    jne .out
    mov si, wa_sfld
    call os88line_click
    jc .out
    mov byte [wa_sfoc], WS_FLD
    mov byte [wa_sdfld], 0FFh   ; (its caret where the press put it)
    jmp .out
.took:
    pushf                       ; (drpress's CF - repaint - is wa_stake's to
    mov bx, di                  ; read: the stop's compare below set it for
    shr bx, 1                   ; every drop-down before the field, and the
    cmp bl, WS_FLD              ; list came down only to be painted over)
    jb .tk                      ; (the focus where the press was: Mode's
    inc bx                      ; and Win at's stops past the slot's,
    cmp bl, WS_DDET + 1         ; Detail's the slot's)
    jne .tk
    mov bl, WS_FLD
.tk:
    mov [wa_sfoc], bl
    popf
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

; --- THE ROTATION (MODES-PLAN 5) ----------------------------------------------
; A window of its own: the mode's arenas, a check box each (os88ui's, SPEC.md
; 13.15) - the ones a win moves on to, round, in the list's order (wc_next).
; Each mode keeps its own (wa_srot, a bit per arena, all by default); at
; least one stays checked. Up and Down move the marker, Space checks, Esc or
; Enter (or the close box) puts it away

WR_W        equ 168             ; its content: the mode's name, then a row
WR_Y0       equ 18              ; a box
WR_DY       equ 16
WR_BX       equ 20              ; (a box's rect from here; the marker at 4)

; wa_ropen - the window up (Rotation..., R): the mode's arenas
wa_ropen:
    mov bx, [wa_rwin]
    or bx, bx
    jz .new
    call OSAPI_WM_SHOW          ; (up already: in front)
    ret
.new:
    mov al, [wa_sdr_a+OS88UI_DR_N]
    dec ax                      ; (the list but Rotation...)
    mov [wa_rn], al
    mov byte [wa_rfoc], 0
    push cx                     ; ONE COLUMN, OR TWO WHEN ONE WOULD REACH THE
    push dx                     ; DOCK (the owner, 2026-11-15: free-for-all's
    mov [wa_rrows], al          ; eight ran into it on a CGA, whose dock
    mov word [wa_rtpl+WT_W], WR_W + 2 ; starts at row 176): the rows a
    push ax                     ; column holds, and the width
    call OSAPI_VIDEO            ; CX = the dock's first row
    pop ax
    push ax
    mov ah, WR_DY
    mul ah
    add ax, WR_Y0 + 4 + 19
    add ax, [wa_rtpl+WT_Y]      ; (its bottom, from where it opens)
    cmp ax, cx
    pop ax
    jbe .one
    inc ax                      ; two columns: half the rows, rounded up,
    shr al, 1                   ; the first the taller
    mov [wa_rrows], al
    mov word [wa_rtpl+WT_W], 2 * WR_W + 2
.one:
    pop dx
    pop cx
    mov al, [wa_rrows]
    mov ah, WR_DY
    mul ah
    add ax, WR_Y0 + 4 + 19      ; (the template's: the title bar's 19 with
    mov [wa_rtpl+WT_H], ax      ; the content, as wa_tpl's 174 is)
    mov si, wa_rtpl
    call OSAPI_WM_CREATE
    jc .out
    mov [wa_rwin], bx
    call OSAPI_WM_SHOW
.out:
    ret

; wa_rdone - the window down, if it is up. Keeps all but BX
wa_rdone:
    xor bx, bx
    xchg bx, [wa_rwin]          ; (the word first: wm_destroy's repaint
    or bx, bx                   ; must not find a dying one)
    jz .out
    call OSAPI_WM_DESTROY
.out:
    ret

; wa_rclose - ONCLOSE: down, and the close done (CF = 1: nothing left to
; close - os88ui_aclose's answer)
wa_rclose:
    call wa_rback
    stc
    ret

; wa_rback - down, and the setup in front again (its keys back)
wa_rback:
    call wa_rdone
    mov bx, [wa_win]
    call OSAPI_WM_FRONT
    ret

; wa_rplace - the boxes' records from where the window is, their marks from
; the mode's rotation. Keeps every register
wa_rplace:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [wa_rwin]
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov cx, ax
    add dx, WR_Y0
    mov bl, [wa_lkgmode]
    xor bh, bh
    mov bl, [wa_srot+bx]        ; BL = the rotation
    xor si, si
    mov di, wa_rck
.r:
    mov ax, si
    cmp al, [wa_rn]
    jae .out
    or al, al                   ; the first box of the second column: one
    jz .x                       ; column right, and back to the top
    cmp al, [wa_rrows]
    jne .x
    add cx, WR_W
    mov al, [wa_rrows]
    mov ah, WR_DY
    mul ah
    sub dx, ax
.x:
    mov ax, cx
    add ax, WR_BX
    mov [di+OS88UI_CK_RECT], ax
    mov [di+OS88UI_CK_RECT+2], dx
    add ax, WR_W - WR_BX - 9
    mov [di+OS88UI_CK_RECT+4], ax
    mov ax, dx
    add ax, 13
    mov [di+OS88UI_CK_RECT+6], ax
    push si
    shl si, 1
    mov ax, [wa_salp+si]        ; (SI, not BP: BP would read SS)
    pop si
    mov [di+OS88UI_CK_LABEL], ax
    push cx
    mov cl, [wa_salx+si]
    mov al, 1
    shl al, cl
    pop cx
    and al, bl
    jz .m
    mov al, 1
.m:
    mov [di+OS88UI_CK_ON], al
    add di, OS88UI_CK_SIZE
    add dx, WR_DY
    inc si
    jmp .r
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wa_rpaint - PAINT (the kernel set the clip, the content white): the mode,
; the boxes, the marker
wa_rpaint:
    push si                     ; W_PAINT OWES BACK SI (SPEC.md 11's W_CLASS):
    call .body                  ; wm_draw_win takes it for the window again,
    pop si                      ; for the grow box and the save-under bank.
    cld                         ; The mode's name left it a string's address:
    ret                         ; wm_ptr2idx's divide overflowed on kern_mega
.body:                          ; (FAULT 00), and on kern_big the same index
    call wa_rplace              ; zeroed and freed a stranger's wm_su_segs word
    mov bx, [wa_rwin]
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, 8
    add dx, 4
    mov al, [wa_lkgmode]        ; (its name: Mode's item)
    mov bx, wa_smodes
    call wa_sval.find
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [wa_si_mode+bx]
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov bx, wa_rck
    xor cx, cx
.b:
    cmp cl, [wa_rn]
    jae wa_rmarks
    xor di, di
    call os88ui_chk
    add bx, OS88UI_CK_SIZE
    inc cx
    jmp .b

; wa_rmarks - each row's marker cell, '>' at the focus (the clip armed)
wa_rmarks:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, wa_rck
    xor cx, cx
.l:
    cmp cl, [wa_rn]
    jae .o
    mov al, ' '
    cmp cl, [wa_rfoc]
    jne .s
    mov al, '>'
.s:
    mov [wa_rmk], al
    push cx
    mov cx, [bx+OS88UI_CK_RECT]
    sub cx, WR_BX - 4
    mov dx, [bx+OS88UI_CK_RECT+2]
    add dx, 3
    mov si, wa_rmk
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop cx
    add bx, OS88UI_CK_SIZE
    inc cx
    jmp .l
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wa_rclip - CF = 0 the window's clip armed, CF = 1 nothing of it to see;
; the records placed. Keeps all but the flags
wa_rclip:
    push bx
    call wa_rplace
    mov bx, [wa_rwin]
    call OSAPI_WM_CLIP_SET
    pop bx
    ret

; wa_rclick - ONCLICK: CX, DX = the press (screen): a box toggled
wa_rclick:
    call wa_rclip
    jc .out
    mov bx, wa_rck
    xor bp, bp
.h:
    mov ax, bp
    cmp al, [wa_rn]
    jae .out
    call os88ui_chkhit
    jnc .hit
    add bx, OS88UI_CK_SIZE
    inc bp
    jmp .h
.hit:
    mov [wa_rfoc], al
    call wa_rset
    call wa_rmarks
.out:
    ret

; wa_rkey - ONKEY: Up, Down, Space; Esc and Enter close. CF = 1 taken
wa_rkey:
    cmp al, 27
    je .close
    cmp al, 13
    je .close
    call wa_rclip
    jc .yes
    mov bl, [wa_rfoc]
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp ah, 0x4B                ; Left and Right: the other column's box on
    je .left                    ; the same row, when there are two
    cmp ah, 0x4D
    je .right
    cmp al, ' '
    jne .no
    mov al, OS88UI_CK_SIZE      ; SPACE: the focused one toggled
    mul bl
    add ax, wa_rck
    mov bx, ax
    xor byte [bx+OS88UI_CK_ON], 1
    xor di, di
    call os88ui_chkmark
    mov al, [wa_rfoc]
    call wa_rset
    jmp short .yes
.up:
    or bl, bl
    jz .yes
    dec bx
    jmp short .f
.down:
    inc bx
    cmp bl, [wa_rn]
    jae .yes
    jmp short .f
.left:
    sub bl, [wa_rrows]          ; (one column: below 0, nothing)
    jb .yes
    jmp short .f
.right:
    add bl, [wa_rrows]          ; (one column: past the last, nothing)
    cmp bl, [wa_rn]
    jae .yes
.f:
    mov [wa_rfoc], bl
    call wa_rmarks
.yes:
    stc
    ret
.no:
    clc
    ret
.close:
    call wa_rback
    stc
    ret

; wa_rset - BX = box AL, just toggled (and drawn): the rotation as it says -
; unless it was the last one checked, which goes back on (a toast says why)
wa_rset:
    push si                     ; SI KEPT: wa_rclick is an ONCLICK, which owes
    call .body                  ; it back (SPEC.md 11's W_CLASS) as W_PAINT
    pop si                      ; does, and both arms below spend it
    ret
.body:
    push bx
    mov bx, wa_rck              ; (any still checked?)
    xor cx, cx
    xor ah, ah
.c:
    cmp cl, [wa_rn]
    jae .cd
    or ah, [bx+OS88UI_CK_ON]
    add bx, OS88UI_CK_SIZE
    inc cx
    jmp .c
.cd:
    pop bx
    or ah, ah
    jnz .set
    mov byte [bx+OS88UI_CK_ON], 1
    xor di, di
    call os88ui_chkmark
    push ds
    pop es
    mov si, wa_t_rone
    xor cx, cx
    call OSAPI_TOAST
    ret
.set:
    xor ah, ah
    mov si, ax
    mov cl, [wa_salx+si]
    mov dl, 1
    shl dl, cl                  ; DL = its arena's bit
    mov al, [wa_lkgmode]
    xor ah, ah
    mov si, ax
    cmp byte [bx+OS88UI_CK_ON], 0
    je .clr
    or [wa_srot+si], dl
    ret
.clr:
    not dl
    and [wa_srot+si], dl
    ret

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
    cmp byte [wa_lkgmode], 1    ; (CTF: an arena with its bases - the
    jne .run                    ; list offers no other, MODES-PLAN 5)
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
    call wa_saidin              ; (a rotation's refused arena, said now)
    mov byte [wa_lkfull], 1
.out:
    ret

; --- data ---------------------------------------------------------------------

wa_sdrs:    dw wa_sdr_a, wa_sdr_c, wa_sdr_b, wa_sdr_s, wa_sdr_p, wa_sdr_m
            dw wa_sdr_g, wa_sdr_d
%macro WA_SDROP 3                ; a drop-down's record: its items, how many
%1:         dw 0, 0, 0, 0, %2, %3, 0, 0
            db 0, 0FFh
            dw 0, 0, 0, 0
%endmacro
WA_SDROP wa_sdr_a, wa_salp, WA_NARENA     ; (the mode's: wa_sarlist)
WA_SDROP wa_sdr_c, wa_si_cols, 2
WA_SDROP wa_sdr_b, wa_si_bots, 4
WA_SDROP wa_sdr_s, wa_lt_dnames, 3
WA_SDROP wa_sdr_p, wa_sp_names, 4
WA_SDROP wa_sdr_m, wa_si_mode, WA_NMODES
WA_SDROP wa_sdr_g, wa_sg_ffa, WA_NGOAL   ; (the mode's: wa_sarlist)
WA_SDROP wa_sdr_d, wa_hs_lev, WA_NLEV   ; (the HUD's names for them)

wa_si_cols:     dw wa_sw_40, wa_sw_80
wa_sw_40:       db '40', 0
wa_sw_80:       db '80', 0
wa_si_bots:     dw wa_sbn_0, wa_sbn_1, wa_sbn_2, wa_sbn_3
wa_sbn_0:       db 'None', 0
wa_sbn_1:       db '1', 0
wa_sbn_2:       db '2', 0
wa_sbn_3:       db '3', 0
wa_sp_names:    dw wa_sp_0, wa_sp_1, wa_sp_2, wa_sp_3
wa_sp_0:        db 'Offline', 0
wa_sp_1:        db 'Serial Cable', 0
wa_sp_2:        db 'Local Network', 0
wa_sp_3:        db 'Internet', 0
wa_si_mode:     dw wa_smd_0, wa_smd_2, wa_smd_1, wa_smd_3
                                ; (CTF-PLAN 7, MODES-PLAN 1)
wa_smd_0:       db 'Free-for-All', 0
wa_smd_1:       db 'Capture the Flag', 0
wa_smd_2:       db 'Team Deathmatch', 0
wa_smd_3:       db 'King of the Hill', 0 ; (sixteen cells: WS_LDX2's)
WA_NMODES       equ 4
wa_smodes:      db 0, 2, 1, 3   ; (the list's order to wa_gmode's numbers)
wa_amodes:                      ; each arena's modes, a bit each (arenagen)
%assign wa_i 0
%rep WA_NARENA
                db WA_AM%[wa_i]
%assign wa_i wa_i+1
%endrep
wa_saord:                        ; the list's order: each item's arena, and
%assign wa_i 0                  ; each arena's item (arenagen's WA_ORD,
%rep WA_NARENA                  ; WA_ITM)
                db WA_ORD%[wa_i]
%assign wa_i wa_i+1
%endrep
wa_sitm:
%assign wa_i 0
%rep WA_NARENA
                db WA_ITM%[wa_i]
%assign wa_i wa_i+1
%endrep
wa_salp:        times WA_NARENA+1 dw 0  ; the Arena list, the mode's
wa_salx:        times WA_NARENA+1 db 0  ; ...and each item's arena (0FFh:
                                        ; Rotation..., the last)
wa_s_rot:       db 'Rotation...', 0
; WIN AT (MODES-PLAN 2): by mode (wa_gmode's numbers), its items, the
; values they name (0: no limit) and the pick, which each mode keeps
WA_NGOAL        equ 7
wa_sglist:      dw wa_sg_ffa, wa_sg_ctf, wa_sg_tdm, wa_sg_kh
wa_sgval:       db 0, 5, 10, 15, 20, 25, 30     ; free-for-all: kills
                db 0, 1, 2, 3, 5, 7, 10         ; CTF: captures
                db 0, 10, 15, 20, 25, 30, 50    ; team deathmatch: kills
                db 0, 3, 5, 10, 15, 20, 30      ; king of the hill: points
wa_srot:        db 0FFh, 0FFh, 0FFh, 0FFh   ; THE ROTATION by mode: a bit an
                                ; arena, every one to begin with
wa_slay:        db 0            ; THE LAYOUT: 0 alone, 1 linked (wa_slayout)
wa_sgy:         dw 0            ; ...the group's top in it, the bottom row's
wa_sby:         dw 0            ; (wa_splace's, off the content's height)
wa_sybase:      dw 0, 0, 0, 0, 0, 0     ; the ground's y bases: 0, the group,
                                ; the group - 1, the bottom row, it - 1, the
                                ; content's last line
wa_rwin:        dw 0            ; its window, 0 none; how many boxes, the
wa_rn:          db 0            ; focus's, its marker's text
wa_rfoc:        db 0
wa_rrows:       db 0            ; rows a column holds (wa_rn: one column)
wa_rmk:         db ' ', 0
WR_CKSZ         equ 12          ; (os88ui's OS88UI_CK_SIZE, included after:
                                ; wirearena.asm checks the two agree)
wa_rck:         times WA_NARENA * WR_CKSZ db 0  ; the boxes
wa_rtpl:        dw 232, 22, WR_W + 2, 0 ; its template (the height: wa_ropen;
                                        ; eight rows fit a CGA from y 22)
                dw wa_rttl, wa_rcls
    OS88_CLASS wa_rcls, 0, PAINT, wa_rpaint, ONKEY, wa_rkey, \
               ONCLICK, wa_rclick, ONCLOSE, wa_rclose
wa_rttl:        db 'Rotation', 0
wa_sgoal:       db 4, 3, 5, 3   ; the picks: 20 kills, 3 captures, 30
                                ; kills, 10 points
wa_sg_ffa:      dw wa_sgn, wa_sgk5, wa_sgk10, wa_sgk15, wa_sgk20, wa_sgk25
                dw wa_sgk30
wa_sg_ctf:      dw wa_sgn, wa_sgc1, wa_sgc2, wa_sgc3, wa_sgc5, wa_sgc7
                dw wa_sgc10
wa_sg_tdm:      dw wa_sgn, wa_sgk10, wa_sgk15, wa_sgk20, wa_sgk25, wa_sgk30
                dw wa_sgk50
wa_sg_kh:       dw wa_sgn, wa_sgp3, wa_sgp5, wa_sgp10, wa_sgp15, wa_sgp20
                dw wa_sgp30
wa_sgn:         db 'No Limit', 0
wa_sgk5:        db '5 Kills', 0
wa_sgk10:       db '10 Kills', 0
wa_sgk15:       db '15 Kills', 0
wa_sgk20:       db '20 Kills', 0
wa_sgk25:       db '25 Kills', 0
wa_sgk30:       db '30 Kills', 0
wa_sgk50:       db '50 Kills', 0
wa_sgc1:        db '1 Capture', 0
wa_sgc2:        db '2 Captures', 0
wa_sgc3:        db '3 Captures', 0
wa_sgc5:        db '5 Captures', 0
wa_sgc7:        db '7 Captures', 0
wa_sgc10:       db '10 Captures', 0
wa_sgp3:        db '3 Points', 0
wa_sgp5:        db '5 Points', 0
wa_sgp10:       db '10 Points', 0
wa_sgp15:       db '15 Points', 0
wa_sgp20:       db '20 Points', 0
wa_sgp30:       db '30 Points', 0

wa_sb_play:     db 'Play', 0
wa_sb_rdy:      db 'Ready', 0
wa_sb_nrdy:     db 'Not ready', 0
wa_sb_keys:     db 'Keys', 0
wa_sbflags:     dw OS88UI_FILL, OS88UI_FILL
    OS88UI_BTNREC wa_sbrec, wa_sbrects, wa_sblab, wa_sbflags, 2, wa_splace

wa_ablines:                     ; (OS88UI_ABPACK: the lines end to end, a
            db 'Wire Arena', 0  ; lone NUL the blank one, ABEND the end)
            db 0
            db OS88UI_ABLT, 'Mouse looks; W A S D move; arrows', 0
            db OS88UI_ABLT, 'turn and pitch; Space jumps.', 0
            db OS88UI_ABLT, 'Click or Ctrl fires; R reloads.', 0
            db OS88UI_ABLT, '1-6: pistol, shotgun, chaingun,', 0
            db OS88UI_ABLT, 'grenades, rockets, railgun.', 0
            db OS88UI_ABLT, 'Q/E: weapon. [/]: mouse. P: pause.', 0
            db OS88UI_ABLT, 'Tab: scores. -/+: 40/80 columns.', 0
            db OS88UI_ABLT, 'F1-F5: detail. V: vsync.', 0
            db OS88UI_ABLT, 'F10: fps; all the stats; off.', 0
            db OS88UI_ABLT, 'Esc leaves the arena.', 0
            db 0
            db 'Contributed by Elendilon', 0
            db OS88UI_ABEND
