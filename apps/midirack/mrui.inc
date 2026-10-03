; =============================================================================
; os8088 - apps/midirack/mrui.inc
;
; THE WINDOW (SPEC.md 105.9). Prefix mru_. Four panes - Playlist, Now Playing,
; Channel Rack and Status - and a row of buttons, LAID OUT FROM THE LIVE
; CONTENT BOX every paint (SPEC.md 39: three adapters, one binary):
;
;   tall     (content >= MRU_TALLH): the mock-up whole - row pitch 10, the
;            Status pane with its three lines and three check boxes, and
;            Settings / MIDI Info / About / Quit along the bottom
;   compact  (a CGA): pitch 8, the rack in two columns of eight, no Status
;            pane and no bottom row - every one of those is in a menu - and
;            the status message as one line under the panes
;
; NOTHING REPAINTS MORE THAN IT CHANGED (PERFORMANCE.md rule 1). A full paint
; is the kernel's W_PAINT; the worker's redraw (mru_dyn) touches only fields
; whose value moved, and a level bar moves by ONE fill - the strip between
; its old length and its new one, black growing and white shrinking - so no
; pixel of it is written twice (rule 2).
;
; Every string is an opaque font_run, ink 0 on paper 15; every line and box
; is a horizontal or vertical run, so a 1bpp adapter draws exactly what a VGA
; does (SPEC.md 39.4).
; =============================================================================

MRU_TALLH   equ 262                 ; content height for the tall layout
MRU_NB      equ 14                  ; buttons in the group
MRU_NBC     equ 10                  ; ...of which compact shows the first

; button ids, in the group's order
MRB_ADD     equ 0
MRB_REM     equ 1
MRB_DIR     equ 2
MRB_PREV    equ 3
MRB_PLAY    equ 4
MRB_PAUSE   equ 5
MRB_STOP    equ 6
MRB_NEXT    equ 7
MRB_EJECT   equ 8
MRB_LOOP    equ 9
MRB_SET     equ 10
MRB_INFO    equ 11
MRB_ABOUT   equ 12
MRB_QUIT    equ 13

    MRW mru_ox                      ; the content's origin...
    MRW mru_oy
    MRW mru_cw                      ; ...and size
    MRW mru_ch
    MRB mru_cmp                     ; 1 = the compact layout
    MRW mru_rp                      ; the row pitch
    MRW mru_bh                      ; a text button's height
    MRBUF mru_p1, 8                 ; the panes (content-relative x1 y1 x2 y2)
    MRBUF mru_p2, 8
    MRBUF mru_p3, 8
    MRBUF mru_p4, 8
    MRBUF mru_lb, 8                 ; the playlist's box
    MRW mru_lrows                   ; ...rows it shows
    MRBUF mru_rel, MRU_NB * 8       ; the buttons, content-relative
    MRBUF mru_rect, MRU_NB * 8      ; ...and on the screen (the group's)
    MRBUF mru_bflg, MRU_NB * 2
    MRW mru_npx                     ; Now Playing: the info column's x
    MRW mru_npy                     ; ...its first line's y
    MRW mru_nplines                 ; ...how many lines
    MRBUF mru_bar, 8                ; ...the progress bar
    MRW mru_tmy                     ; ...the times under it
    MRW mru_capy                    ; ...the transport's captions (0: none)
    MRW mru_rkx                     ; the rack: its left
    MRW mru_rky                     ; ...its first row's y
    MRW mru_rkcols                  ; ...columns (1 or 2)
    MRW mru_rkrows                  ; ...rows a column
    MRW mru_rkcw                    ; ...a column's width
    MRW mru_sty                     ; status: the first line's y (0: none)
    MRW mru_stx
    MRW mru_stw                     ; ...its text width in cells
    MRBUF mru_sb, 14                ; the playlist's scroll bar block
    MRBUF mru_ck1, 12              ;   ; the three check boxes
    MRBUF mru_ck2, 12
    MRBUF mru_ck3, 12
    MRBUF mru_lw, MR_NCH            ; each bar's drawn length
    MRW mru_lsec                    ; the dynamic fields as last drawn
    MRW mru_lbar
    MRW mru_lmeas
    MRW mru_ltempo
    MRBUF mru_s, 48                 ; formatting scratch
    MRBUF mru_s2, 48
    MRBUF mru_dev, 64               ; status line 1
    MRBUF mru_file, 72              ; status line 2

; =============================================================================
; mru_layout - every rect from the live content box
; =============================================================================
mru_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [mr_win]
    call OSAPI_WM_GEOM              ; CX = content w, DX = h
    jc .keep
    or cx, cx
    jz .keep
    mov [mru_cw], cx
    mov [mru_ch], dx
    jmp short .have
.keep:
    cmp word [mru_cw], 0
    jne .have
    mov word [mru_cw], 616
    mov word [mru_ch], 280
.have:
    mov cx, [mru_cw]
    mov dx, [mru_ch]
    mov byte [mru_cmp], 0
    mov word [mru_rp], 10
    mov word [mru_bh], 14
    cmp dx, MRU_TALLH
    jae .tall
    mov byte [mru_cmp], 1
    mov word [mru_rp], 8
    mov word [mru_bh], 12
.tall:
    ; --- the bottom: the button row and the Status pane (tall only) --------
    mov ax, dx                      ; AX = the panes' bottom
    sub ax, 4
    cmp byte [mru_cmp], 0
    jne .cbot
    sub ax, [mru_bh]                ; the button row
    mov si, ax                      ; SI = its top
    sub ax, 4
    mov [mru_p4+6], ax              ; Status: 3 lines and its title
    sub ax, 14 + 30 + 6
    mov [mru_p4+2], ax
    mov word [mru_p4], 4
    mov bx, cx
    sub bx, 5
    mov [mru_p4+4], bx
    sub ax, 4
    jmp short .bot
.cbot:
    sub ax, 9                       ; compact: one status line under the panes
    mov [mru_sty], ax
    add word [mru_sty], 2
    mov word [mru_p4+6], 0          ; (no pane)
    sub ax, 2
.bot:
    mov [mru_p1+6], ax              ; the three panes' bottom
    mov [mru_p2+6], ax
    mov [mru_p3+6], ax
    mov word [mru_p1+2], 4
    mov word [mru_p2+2], 4
    mov word [mru_p3+2], 4
    ; --- the columns: 23% / 36% / the rest --------------------------------
    mov ax, cx
    sub ax, 16
    mov bx, 23
    mul bx
    mov bx, 100
    div bx                          ; AX = the playlist's width
    mov word [mru_p1], 4
    add ax, 4
    mov [mru_p1+4], ax
    add ax, 4
    mov [mru_p2], ax
    push ax
    mov ax, cx
    sub ax, 16
    mov bx, 36
    mul bx
    mov bx, 100
    div bx
    pop bx
    add ax, bx
    mov [mru_p2+4], ax
    add ax, 4
    mov [mru_p3], ax
    mov ax, cx
    sub ax, 5
    mov [mru_p3+4], ax
    call mru_lay_list
    call mru_lay_np
    call mru_lay_rack
    cmp byte [mru_cmp], 0
    jne .nost
    call mru_lay_status
    ; the bottom row: Settings, MIDI Info, About ... Quit
    mov ax, [mru_ch]
    sub ax, 4
    mov dx, ax                      ; DX = y2
    sub ax, [mru_bh]
    inc ax
    mov bx, ax                      ; BX = y1
    mov di, MRB_SET
    mov ax, 4
    mov cx, 92
    call mru_brect
    mov di, MRB_INFO
    mov ax, 100
    mov cx, 196
    call mru_brect
    mov di, MRB_ABOUT
    mov ax, 204
    mov cx, 300
    call mru_brect
    mov di, MRB_QUIT
    mov cx, [mru_cw]
    sub cx, 5
    mov ax, cx
    sub ax, 92
    call mru_brect
.nost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_brect - button DI's content-relative rect = AX, BX, CX, DX
mru_brect:
    push si
    mov si, di
    shl si, 1
    shl si, 1
    shl si, 1
    mov [mru_rel+si], ax
    mov [mru_rel+si+2], bx
    mov [mru_rel+si+4], cx
    mov [mru_rel+si+6], dx
    pop si
    ret

; --- the Playlist pane ---------------------------------------------------------
mru_lay_list:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [mru_p1]                ; the box: under the title, over the
    add ax, 4                       ; buttons (two rows of them)
    mov [mru_lb], ax
    mov ax, [mru_p1+2]
    add ax, 14
    mov [mru_lb+2], ax
    mov ax, [mru_p1+4]
    sub ax, 4
    mov [mru_lb+4], ax
    mov ax, [mru_p1+6]
    sub ax, 4
    mov dx, ax                      ; DX = the lower button row's y2
    sub ax, [mru_bh]
    inc ax
    mov bx, ax
    push bx
    mov di, MRB_DIR
    mov ax, [mru_p1]
    add ax, 4
    mov cx, [mru_p1+4]
    sub cx, 4
    call mru_brect
    pop dx
    sub dx, 4                       ; the upper row
    mov bx, dx
    sub bx, [mru_bh]
    inc bx
    push bx
    mov ax, [mru_p1]
    add ax, 4
    mov cx, [mru_p1]
    add cx, [mru_p1+4]
    shr cx, 1
    sub cx, 2
    mov di, MRB_ADD
    call mru_brect
    mov ax, cx
    add ax, 4
    mov cx, [mru_p1+4]
    sub cx, 4
    mov di, MRB_REM
    call mru_brect
    pop ax
    sub ax, 5
    mov [mru_lb+6], ax
    sub ax, [mru_lb+2]              ; the rows that fit
    sub ax, 3
    xor dx, dx
    div word [mru_rp]
    mov [mru_lrows], ax
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the Now Playing pane --------------------------------------------------------
mru_lay_np:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mru_p2]
    add ax, 8
    mov [mru_npx], ax
    mov ax, [mru_p2+2]
    add ax, 30
    cmp byte [mru_cmp], 0
    je .y
    sub ax, 4
.y:
    mov [mru_npy], ax
    mov word [mru_nplines], 6
    cmp byte [mru_cmp], 0
    je .n
    mov word [mru_nplines], 5       ; compact: no Mode line
.n:
    mov ax, [mru_rp]
    mul word [mru_nplines]
    add ax, [mru_npy]
    add ax, 2                       ; the bar
    mov bx, [mru_p2]
    add bx, 8
    mov [mru_bar], bx
    mov [mru_bar+2], ax
    mov bx, [mru_p2+4]
    sub bx, 8
    mov [mru_bar+4], bx
    add ax, 8
    mov [mru_bar+6], ax
    add ax, 3
    mov [mru_tmy], ax
    add ax, 11                      ; the transport: five across
    mov bx, ax                      ; BX = y1
    mov dx, ax
    add dx, 19                      ; 20 tall
    cmp byte [mru_cmp], 0
    je .tb
    sub dx, 4                       ; 16 tall, compact
.tb:
    mov si, [mru_p2+4]              ; the pitch: (width - 8) / 5
    sub si, [mru_p2]
    sub si, 8
    mov ax, si
    push dx
    xor dx, dx
    mov cx, 5
    div cx
    pop dx
    mov si, ax                      ; SI = the pitch
    mov ax, [mru_p2]
    add ax, 4
    mov di, MRB_PREV
.t:
    mov cx, ax
    add cx, si
    sub cx, 4
    call mru_brect
    add ax, si
    inc di
    cmp di, MRB_NEXT
    jbe .t
    mov ax, dx
    inc ax
    mov word [mru_capy], 0
    cmp byte [mru_cmp], 0
    jne .nc
    mov [mru_capy], ax              ; the captions under them
    add ax, 9
.nc:
    add ax, 3                       ; Eject and Loop, half each
    mov bx, ax
    mov dx, ax
    add dx, [mru_bh]
    dec dx
    mov ax, [mru_p2]
    add ax, 4
    mov cx, [mru_p2]
    add cx, [mru_p2+4]
    shr cx, 1
    sub cx, 2
    mov di, MRB_EJECT
    call mru_brect
    mov ax, cx
    add ax, 4
    mov cx, [mru_p2+4]
    sub cx, 4
    mov di, MRB_LOOP
    call mru_brect
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the Channel Rack ----------------------------------------------------------
mru_lay_rack:
    push ax
    push bx
    push dx
    mov ax, [mru_p3]
    add ax, 6
    mov [mru_rkx], ax
    mov ax, [mru_p3+2]
    add ax, 28
    cmp byte [mru_cmp], 0
    je .y
    sub ax, 4                       ; compact: the header just under the title
.y:
    mov [mru_rky], ax
    mov bx, [mru_p3+6]              ; rows that fit under the header
    sub bx, ax
    sub bx, 2
    mov ax, bx
    xor dx, dx
    div word [mru_rp]
    mov word [mru_rkcols], 1
    mov word [mru_rkrows], 16
    cmp ax, 16
    jae .w
    mov word [mru_rkcols], 2        ; not sixteen high: two columns of eight
    mov word [mru_rkrows], 8
.w:
    mov ax, [mru_p3+4]
    sub ax, [mru_p3]
    sub ax, 10
    xor dx, dx
    div word [mru_rkcols]
    mov [mru_rkcw], ax
    pop dx
    pop bx
    pop ax
    ret

; --- the Status pane -------------------------------------------------------------
mru_lay_status:
    push ax
    push bx
    push si
    mov ax, [mru_p4+2]
    add ax, 16
    mov [mru_sty], ax
    mov ax, [mru_p4]
    add ax, 8
    mov [mru_stx], ax
    mov ax, [mru_p4+4]              ; the text runs to the divider, 170 px
    sub ax, 178                     ; from the right
    sub ax, [mru_stx]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [mru_stw], ax
    mov word [mru_ck1+OS88UI_CK_LABEL], mru_l_lev
    mov word [mru_ck2+OS88UI_CK_LABEL], mru_l_loop
    mov word [mru_ck3+OS88UI_CK_LABEL], mru_l_bg
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mru_origin - [mru_ox]/[mru_oy] from the window as it stands
; -----------------------------------------------------------------------------
mru_origin:
    push ax
    push bx
    push dx
    mov bx, [mr_win]
    call OSAPI_WM_CONTENT
    jc .k
    mov [mru_ox], ax
    mov [mru_oy], dx
.k:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mru_aim - the group's SCREEN rects, its live count and its flags, and the
; check boxes' and the scroll bar's - from the layout and the origin. The
; painter AND the click path run it (the Audio Player's apu_baim rule)
; -----------------------------------------------------------------------------
mru_aim:
    push ax
    push bx
    push cx
    push si
    push di
    xor si, si
.r:
    mov ax, [mru_rel+si]
    add ax, [mru_ox]
    mov [mru_rect+si], ax
    mov ax, [mru_rel+si+2]
    add ax, [mru_oy]
    mov [mru_rect+si+2], ax
    mov ax, [mru_rel+si+4]
    add ax, [mru_ox]
    mov [mru_rect+si+4], ax
    mov ax, [mru_rel+si+6]
    add ax, [mru_oy]
    mov [mru_rect+si+6], ax
    add si, 8
    cmp si, MRU_NB * 8
    jb .r
    mov ax, MRU_NB                  ; the live count: compact drops the row
    cmp byte [mru_cmp], 0
    je .n
    mov ax, MRU_NBC
.n:
    cmp byte [mr_card], 0
    je .nc
    xor ax, ax                      ; a card is up: no button is live under it
.nc:
    mov [mru_btrec+OS88UI_BT_N], ax
    call mru_flags
    ; --- the check boxes: rows of the Status pane's right side -------------
    cmp byte [mru_cmp], 0
    jne .nock
    mov ax, [mru_p4+4]
    sub ax, 160
    add ax, [mru_ox]
    mov bx, [mru_sty]
    add bx, [mru_oy]
    sub bx, 2
    mov si, mru_ck1
    call mru_ckrect
    add bx, 13
    mov si, mru_ck2
    call mru_ckrect
    add bx, 13
    mov si, mru_ck3
    call mru_ckrect
    mov al, [mr_levels]
    mov [mru_ck1+OS88UI_CK_ON], al
    mov al, [mr_loop]
    mov [mru_ck2+OS88UI_CK_ON], al
    mov al, [mr_bg]
    mov [mru_ck3+OS88UI_CK_ON], al
.nock:
    ; --- the scroll bar: the list box's right 14 px ---------------------
    mov ax, [mru_lb+4]
    add ax, [mru_ox]
    mov [mru_sb+4], ax
    sub ax, 13
    mov [mru_sb], ax
    mov ax, [mru_lb+2]
    add ax, [mru_oy]
    mov [mru_sb+2], ax
    mov ax, [mru_lb+6]
    add ax, [mru_oy]
    mov [mru_sb+6], ax
    mov al, [mrl_n]
    xor ah, ah
    mov [mru_sb+8], ax
    mov ax, [mru_lrows]
    mov [mru_sb+10], ax
    mov al, [mrl_top]
    xor ah, ah
    mov [mru_sb+12], ax
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; mru_ckrect - check box SI's rect: AX, BX (screen), 150 x 12
mru_ckrect:
    push ax
    push bx
    mov [si+OS88UI_CK_RECT], ax
    mov [si+OS88UI_CK_RECT+2], bx
    add ax, 150
    mov [si+OS88UI_CK_RECT+4], ax
    add bx, 11
    mov [si+OS88UI_CK_RECT+6], bx
    pop bx
    pop ax
    ret

; mru_flags - each button's flags from the state: icons, the latched Loop,
; and what cannot act right now greyed (SPEC.md 47: a fact, not a guess)
mru_flags:
    push ax
    push bx
    xor bx, bx
.z:
    mov word [mru_bflg+bx], OS88UI_FILL
    add bx, 2
    cmp bx, MRU_NB * 2
    jb .z
    mov ax, OS88UI_IMG | OS88UI_FILL
    mov [mru_bflg+MRB_PREV*2], ax
    mov [mru_bflg+MRB_PLAY*2], ax
    mov [mru_bflg+MRB_PAUSE*2], ax
    mov [mru_bflg+MRB_STOP*2], ax
    mov [mru_bflg+MRB_NEXT*2], ax
    cmp byte [mr_loop], 0
    je .nl
    or word [mru_bflg+MRB_LOOP*2], OS88UI_LATCH
.nl:
    cmp byte [mrl_n], 0             ; an empty list: nothing to remove, play,
    jne .hl                         ; skip or eject
    or word [mru_bflg+MRB_REM*2], OS88UI_DIS
    or word [mru_bflg+MRB_PLAY*2], OS88UI_DIS
    or word [mru_bflg+MRB_PREV*2], OS88UI_DIS
    or word [mru_bflg+MRB_NEXT*2], OS88UI_DIS
.hl:
    cmp byte [mr_state], MR_ST_PLAY
    je .pl
    or word [mru_bflg+MRB_PAUSE*2], OS88UI_DIS
.pl:
    cmp byte [mr_state], MR_ST_STOP
    jne .st
    or word [mru_bflg+MRB_STOP*2], OS88UI_DIS
.st:
    cmp byte [mr_loaded], 0
    jne .ld
    or word [mru_bflg+MRB_INFO*2], OS88UI_DIS
    or word [mru_bflg+MRB_EJECT*2], OS88UI_DIS
.ld:
    pop bx
    pop ax
    ret

    OS88UI_BTNREC mru_btrec, mru_rect, mru_blabs, mru_bflg, MRU_NB
mru_blabs:
    dw mru_l_add, mru_l_rem, mru_l_dir
    dw mru_i_prev, mru_i_play, mru_i_pause, mru_i_stop, mru_i_next
    dw mru_l_eject, mru_l_loopb
    dw mru_l_set, mru_l_info, mru_l_about, mru_l_quit
mru_l_add:   db 'Add...', 0
mru_l_rem:   db 'Remove', 0
mru_l_dir:   db 'Load Directory...', 0
mru_l_eject: db 'Eject', 0
mru_l_loopb: db 'Loop', 0
mru_l_set:   db 'Settings...', 0
mru_l_info:  db 'MIDI Info...', 0
mru_l_about: db 'About...', 0
mru_l_quit:  db 'Quit', 0
mru_caps:    dw mru_c_prev, mru_c_play, mru_c_pause, mru_c_stop, mru_c_next
mru_c_prev:  db 'Prev', 0
mru_c_play:  db 'Play', 0
mru_c_pause: db 'Pause', 0
mru_c_stop:  db 'Stop', 0
mru_c_next:  db 'Next', 0
mru_l_lev:   db 'Show Levels', 0
mru_l_loop:  db 'Loop Playback', 0
mru_l_bg:    db 'Play in Background', 0

; the transport's pictures (OS88UI_IMG): 16 x 10, a full mask - opaque, one
; write (SPEC.md 13.8.9)
mru_i_prev:
    db 1, 10
    times 10 dw 0xFFFF
    dw 0x0000, 0x1888, 0x1998, 0x1BB8, 0x1FF8, 0x1FF8, 0x1BB8, 0x1998, 0x1888, 0x0000
mru_i_play:
    db 1, 10
    times 10 dw 0xFFFF
    dw 0x0200, 0x0300, 0x0380, 0x03C0, 0x03E0, 0x03E0, 0x03C0, 0x0380, 0x0300, 0x0200
mru_i_pause:
    db 1, 10
    times 10 dw 0xFFFF
    dw 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660
mru_i_stop:
    db 1, 10
    times 10 dw 0xFFFF
    dw 0x0000, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0000
mru_i_next:
    db 1, 10
    times 10 dw 0xFFFF
    dw 0x0000, 0x1118, 0x1998, 0x1DD8, 0x1FF8, 0x1FF8, 0x1DD8, 0x1998, 0x1118, 0x0000

; the playlist's row marks, 8 x 8 in the left byte: a song, and the one loaded
mru_i_doc:
    db 1, 8
    dw 0xFE00, 0xFE00, 0xFE00, 0xFE00, 0xFE00, 0xFE00, 0xFE00, 0xFE00
    dw 0xFC00, 0x8600, 0x9200, 0x9A00, 0x9200, 0xB200, 0xB200, 0xFE00
mru_i_cur:
    db 1, 8
    dw 0x1000, 0x3800, 0x7C00, 0xFE00, 0x7C00, 0x3800, 0x1000, 0x0000
    dw 0x1000, 0x2800, 0x4400, 0x9200, 0x4400, 0x2800, 0x1000, 0x0000

; =============================================================================
; drawing helpers. Coordinates are CONTENT-RELATIVE unless named otherwise
; =============================================================================

; mru_text - SI at (CX, DX), ink 0 on paper 15
mru_text:
    push ax
    push cx
    push dx
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

; mru_textinv - the same, white on black
mru_textinv:
    push ax
    push cx
    push dx
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov ax, 0x000F
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

; mru_textn - SI at (CX, DX), at most AX cells, padded with spaces to AX
; (an opaque field: what was longer before is overwritten, once)
mru_textn:
    push ax
    push bx
    push si
    push di
    mov di, mru_s2
    mov bx, ax
    cmp bx, 46
    jbe .c
    mov bx, 46
.c:
    or bx, bx
    jz .end
    mov al, [si]
    or al, al
    jz .pad
    inc si
    mov [di], al
    inc di
    dec bx
    jmp short .c
.pad:
    mov byte [di], ' '
    inc di
    dec bx
    jnz .pad
.end:
    mov byte [di], 0
    mov si, mru_s2
    call mru_text
    pop di
    pop si
    pop bx
    pop ax
    ret

; mru_frame - the rect at SI (relative), black
mru_frame:
    push ax
    push bx
    push cx
    push dx
    xor al, al
    call OSAPI_SET_COLOR
    call mru_absr
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_fillr - the rect at SI (relative), colour AL
mru_fillr:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_SET_COLOR
    call mru_absr
    call OSAPI_GFX_FILL
    xor al, al
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_absr - AX, BX, CX, DX = the rect at SI on the screen
mru_absr:
    mov ax, [si]
    add ax, [mru_ox]
    mov bx, [si+2]
    add bx, [mru_oy]
    mov cx, [si+4]
    add cx, [mru_ox]
    mov dx, [si+6]
    add dx, [mru_oy]
    ret

; mru_fill - AX..DX relative, colour [mru_ink]
mru_fill:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov al, [mru_ink]
    call OSAPI_SET_COLOR
    pop ax
    add ax, [mru_ox]
    add bx, [mru_oy]
    add cx, [mru_ox]
    add dx, [mru_oy]
    call OSAPI_GFX_FILL
    xor al, al
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRB mru_ink

; mru_pane - the pane at SI, framed, its title BX at its top-left
mru_pane:
    push cx
    push dx
    push si
    call mru_frame
    mov cx, [si]
    add cx, 6
    mov dx, [si+2]
    add dx, 3
    mov si, bx
    call mru_text
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; number formatting into DI (NUL-terminated, DI left on the NUL)
; -----------------------------------------------------------------------------
; mru_d3 - AX as three digits, zero-padded
mru_d3:
    push ax
    push bx
    push dx
    mov bx, 100
    xor dx, dx
    div bx
    cmp ax, 9
    jbe .h
    mov ax, 9
.h:
    add al, '0'
    mov [di], al
    mov ax, dx
    mov bl, 10
    div bl
    add ax, '00'
    mov [di+1], ax
    add di, 3
    mov byte [di], 0
    pop dx
    pop bx
    pop ax
    ret

; mru_d2 - AX as two digits
mru_d2:
    push ax
    push bx
    mov bl, 10
    div bl
    add ax, '00'
    mov [di], ax
    add di, 2
    mov byte [di], 0
    pop bx
    pop ax
    ret

; mru_time - AX seconds as MM:SS
mru_time:
    push ax
    push bx
    push dx
    xor dx, dx
    mov bx, 60
    div bx
    cmp ax, 99
    jbe .m
    mov ax, 99
.m:
    call mru_d2
    mov byte [di], ':'
    inc di
    mov ax, dx
    call mru_d2
    pop dx
    pop bx
    pop ax
    ret

; mru_dec - AX in decimal, no padding; mru_decc with thousands commas
mru_dec:
    ; STKBALANCE-LOOP: pushes one digit per loop turn, pops them in the second loop - an itoa
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .e
    mov byte [di], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mru_decc:
    cmp ax, 1000
    jae .k
    jmp mru_dec
.k:
    push ax
    push bx
    push dx
    xor dx, dx
    mov bx, 1000
    div bx
    call mru_dec
    mov byte [di], ','
    inc di
    mov ax, dx
    call mru_d3
    pop dx
    pop bx
    pop ax
    ret

; mru_cat - the NUL string at SI appended at DI (DI left on the NUL)
mru_cat:
    push ax
    push si
.c:
    mov al, [si]
    mov [di], al
    or al, al
    jz .e
    inc si
    inc di
    jmp short .c
.e:
    pop si
    pop ax
    ret

; mr_gmname - SI = program AL's General MIDI name
mr_gmname:
    push ax
    push cx
    mov cl, al
    xor ch, ch
    mov si, mrt_gmname
    jcxz .out
.w:
    cmp byte [si], 0
    je .n
    inc si
    jmp short .w
.n:
    inc si
    loop .w
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; mru_draw - the whole content (W_PAINT: the kernel has white-filled it and
; armed the clip)
; =============================================================================
mru_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_layout
    call mru_origin
    call mru_aim
    mov si, mru_p1
    mov bx, mru_t_list
    call mru_pane
    call mru_list_title
    call mru_draw_list
    mov si, mru_p2
    mov bx, mru_t_np
    call mru_pane
    call mru_draw_np
    mov si, mru_p3
    mov bx, mru_t_rack
    call mru_pane
    call mru_draw_rack
    cmp byte [mru_cmp], 0
    jne .cst
    mov si, mru_p4
    mov bx, mru_t_st
    call mru_pane
    call mru_draw_status
    jmp short .btn
.cst:
    call mru_draw_msg
.btn:
    call mru_draw_buttons
    call mru_draw_card              ; on top, if one is up
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mru_t_list: db 'Playlist', 0
mru_t_np:   db 'Now Playing', 0
mru_t_rack: db 'Channel Rack', 0
mru_t_st:   db 'Status / Information', 0

; mru_list_title - "Playlist (B:\MIDI\)", as much as fits
mru_list_title:
    push ax
    push cx
    push dx
    push si
    push di
    cmp byte [mrl_path], 0
    je .out
    mov di, mru_s
    mov si, mru_t_list
    call mru_cat
    mov byte [di], ' '
    mov byte [di+1], '('
    add di, 2
    mov si, mrl_path
    call mru_cat
    mov byte [di], ')'
    mov byte [di+1], 0
    mov cx, [mru_p1]
    add cx, 6
    mov dx, [mru_p1+2]
    add dx, 3
    mov ax, [mru_p1+4]
    sub ax, cx
    sub ax, 4
    shr ax, 1
    shr ax, 1
    shr ax, 1
    push ax
    add ax, mru_s
    cmp di, ax                      ; too long for the pane: "(MIDI\)", the
    pop ax                          ; last folder alone
    jbe .put
    push ax
    mov di, mru_s + 10              ; past "Playlist ("
    mov si, mrl_path                ; the last folder, '\' to '\'
.e:
    cmp byte [si], 0
    je .ee
    inc si
    jmp short .e
.ee:
    dec si                          ; the trailing '\'
.b:
    cmp si, mrl_path + 2
    jbe .bb
    dec si
    cmp byte [si], '\'
    jne .b
.bb:
    inc si                          ; (past the leading '\')
    call mru_cat
    mov byte [di], ')'
    mov byte [di+1], 0
    pop ax
.put:
    mov si, mru_s
    call mru_textn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mru_draw_list - the list box, its rows and its scroll bar
; -----------------------------------------------------------------------------
mru_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, mru_lb
    call mru_frame
    xor di, di                      ; DI = the row
.r:
    cmp di, [mru_lrows]
    jae .sb
    call mru_draw_row
    inc di
    jmp short .r
.sb:
    mov bx, mru_sb
    call os88ui_sbar
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_draw_row - row DI of the list box: its mark and its name, the
; highlighted entry white on black, an empty row blank
mru_draw_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, di
    mul word [mru_rp]
    add ax, [mru_lb+2]
    add ax, 2
    mov dx, ax                      ; DX = the row's y
    mov ax, di
    add al, [mrl_top]
    mov [mru_rowi], al
    mov cx, [mru_lb+4]              ; the row's width in cells, short of the
    sub cx, [mru_lb]                ; bar and the mark
    sub cx, 14 + 16
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [mru_roww], cx
    ; the row's ground (one fill: the highlight, or white)
    mov byte [mru_ink], CWHITE
    cmp al, [mrl_n]
    jae .gr
    cmp al, [mrl_sel]
    jne .gr
    mov byte [mru_ink], CBLACK
.gr:
    push dx
    mov ax, [mru_lb]
    inc ax
    mov bx, dx
    dec bx
    mov cx, [mru_lb+4]
    sub cx, 14
    mov dx, bx
    add dx, [mru_rp]
    dec dx
    call mru_fill
    pop dx
    mov al, [mru_rowi]
    cmp al, [mrl_n]
    jae .out
    ; the mark
    mov si, mru_i_doc
    cmp byte [mr_loaded], 0
    je .m
    cmp al, [mr_lidx]
    jne .m
    mov si, mru_i_cur
.m:
    push ax
    mov al, CWHITE                  ; ground, ink - inverted on the highlight
    mov ah, CBLACK
    cmp byte [mru_ink], CBLACK
    jne .pen
    xchg al, ah
.pen:
    call OSAPI_ICON_PEN
    pop ax
    mov cx, [mru_lb]
    add cx, 4
    add cx, [mru_ox]
    push dx
    add dx, [mru_oy]
    call OSAPI_ICON_DRAW
    pop dx
    ; the name
    mov al, [mru_rowi]
    call mrl_ptr                    ; BX = the entry
    mov si, bx
    mov cx, [mru_lb]
    add cx, 16
    mov ax, [mru_roww]
    cmp byte [mru_ink], CBLACK
    je .inv
    call mru_textn
    jmp short .out
.inv:
    push si                         ; the highlight: white on black, padded
    push di
    mov di, mru_s
    mov bx, ax
.ic:
    or bx, bx
    jz .ie
    mov al, [si]
    or al, al
    jz .ie
    mov [di], al
    inc si
    inc di
    dec bx
    jmp short .ic
.ie:
    mov byte [di], 0
    pop di
    pop si
    mov si, mru_s
    call mru_textinv
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRB mru_rowi
    MRW mru_roww

; -----------------------------------------------------------------------------
; mru_draw_np - the Now Playing pane: the name bar, the six lines, the bar,
; the times and the transport's captions
; -----------------------------------------------------------------------------
mru_draw_np:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mru_p2]                ; the name bar: black, the name white
    add ax, 4
    mov bx, [mru_p2+2]
    add bx, 13
    mov cx, [mru_p2+4]
    sub cx, 4
    mov dx, bx
    add dx, 11
    cmp byte [mru_cmp], 0
    je .nb
    sub dx, 2
.nb:
    mov byte [mru_ink], CBLACK
    call mru_fill
    call mru_name                   ; SI = the song's name
    mov cx, [mru_p2]
    add cx, 8
    mov dx, [mru_p2+2]
    add dx, 15
    cmp byte [mru_cmp], 0
    je .nn
    dec dx
.nn:
    call mru_textinv
    ; the labels
    xor di, di
.lb:
    cmp di, [mru_nplines]
    jae .lbd
    mov ax, di
    mul word [mru_rp]
    add ax, [mru_npy]
    mov dx, ax
    mov cx, [mru_npx]
    mov bx, di
    cmp byte [mru_cmp], 0
    je .full
    cmp bx, 4                       ; compact skips Mode
    jb .full
    inc bx
.full:
    shl bx, 1
    mov si, [mru_nplab+bx]
    call mru_text
    inc di
    jmp short .lb
.lbd:
    mov si, mru_bar                 ; the bar's frame
    call mru_frame
    mov word [mru_lsec], 0xFFFF     ; every field owed
    mov word [mru_lbar], 0xFFFF
    mov word [mru_lmeas], 0xFFFF
    mov word [mru_ltempo], 0xFFFF
    call mru_np_static              ; Length, Mode, Output
    call mru_np_dyn                 ; Elapsed, Measure, Tempo, the bar
    cmp word [mru_capy], 0          ; the captions under the transport
    je .out
    mov di, MRB_PREV
.cap:
    mov si, di
    shl si, 1
    shl si, 1
    shl si, 1
    mov ax, [mru_rel+si+4]          ; centred under the button
    add ax, [mru_rel+si]
    shr ax, 1
    push si
    mov si, di
    sub si, MRB_PREV
    shl si, 1
    mov si, [mru_caps+si]
    push ax
    call OSAPI_FONT_WIDTH           ; AX = its width
    mov cx, ax
    pop ax
    shr cx, 1
    sub ax, cx
    mov cx, ax
    mov dx, [mru_capy]
    call mru_text
    pop si
    inc di
    cmp di, MRB_NEXT
    jbe .cap
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mru_nplab:  dw mru_n_len, mru_n_el, mru_n_meas, mru_n_tempo, mru_n_mode, mru_n_out
mru_n_len:   db 'Length  :', 0
mru_n_el:    db 'Elapsed :', 0
mru_n_meas:  db 'Measure :', 0
mru_n_tempo: db 'Tempo   :', 0
mru_n_mode:  db 'Mode    :', 0
mru_n_out:   db 'Output  :', 0
MRU_VCOL    equ 10                  ; the values' column, in cells

; mru_name - SI = the loaded song's name, or "(no song)"
mru_name:
    mov si, mru_s_none
    cmp byte [mr_loaded], 0
    je .r
    push ax
    push bx
    mov al, [mr_lidx]
    call mrl_ptr
    mov si, bx
    pop bx
    pop ax
.r:
    ret
mru_s_none: db '(no song)', 0

; mru_vline - DX = the y of Now Playing's line AX (0 Length .. 5 Output),
; CX = its value's x; CF = 1 the line is not shown (compact Mode)
mru_vline:
    push ax
    cmp byte [mru_cmp], 0
    je .f
    cmp ax, 4
    je .no
    jb .f
    dec ax                          ; Output is the fifth compact line
.f:
    mul word [mru_rp]
    add ax, [mru_npy]
    mov dx, ax
    mov cx, [mru_npx]
    add cx, MRU_VCOL * 8
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; mru_vcells - AX = the cells a value may take on a Now Playing line
mru_vcells:
    push cx
    mov ax, [mru_p2+4]
    sub ax, [mru_npx]
    sub ax, MRU_VCOL * 8 + 4
    mov cl, 3
    shr ax, cl
    pop cx
    ret

; mru_np_static - Length, Mode and Output
mru_np_static:
    push ax
    push cx
    push dx
    push si
    push di
    mov di, mru_s                   ; Length
    mov ax, [mr_lenlo]
    mov dx, [mr_lenhi]
    mov si, 1000
    cmp dx, si
    jae .lz
    div si
    jmp short .lt
.lz:
    xor ax, ax
.lt:
    cmp byte [mr_loaded], 0
    jne .lt2
    xor ax, ax
.lt2:
    call mru_time
    mov ax, 0
    call mru_vline
    mov si, mru_s
    call mru_vcells
    call mru_textn
    mov cx, [mru_bar+4]             ; ...and under the bar's right end
    sub cx, 39
    mov dx, [mru_tmy]
    mov ax, 5
    call mru_textn
    mov si, mru_m_norm              ; Mode
    cmp byte [mr_loop], 0
    je .m
    mov si, mru_m_loop
.m:
    mov ax, 4
    call mru_vline
    jc .o
    call mru_vcells
    call mru_textn
.o:
    call mru_outname                ; Output
    mov ax, 5
    call mru_vline
    call mru_vcells
    call mru_textn
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

mru_m_norm: db 'Normal Playback', 0
mru_m_loop: db 'Loop Playback', 0

; mru_outname - SI = what the output is called: the open one, or the one a
; Play would open
mru_outname:
    push ax
    mov al, [mr_out]
    cmp al, MRO_NONE
    jne .k
    call mro_pick
.k:
    mov si, mru_o_spk
    cmp al, MRO_SPK
    je .r
    mov si, mru_o_tone
    cmp al, MRO_TONE
    je .r
    mov si, mru_o_sb
    cmp al, MRO_SB
    je .r
    mov si, mru_o_opl2              ; FM: the mode it is (or would be) in
    cmp byte [mr_out], MRO_FM
    jne .would
    cmp byte [mrf_mode], SND_OPL3
    jne .r
    mov si, mru_o_opl3
    jmp short .r
.would:
    cmp byte [mr_fmkind], SND_OPL3
    jne .r
    cmp byte [mr_want], MR_OUT_OPL2
    je .r
    mov si, mru_o_opl3
.r:
    pop ax
    ret

mru_o_opl2: db 'OPL2 (AdLib)', 0
mru_o_opl3: db 'OPL3 (Stereo FM)', 0
mru_o_sb:   db 'Sound Blaster', 0
mru_o_spk:  db 'PC Speaker', 0
mru_o_tone: db 'PC Speaker tone', 0

; -----------------------------------------------------------------------------
; mru_np_dyn - Elapsed, Measure, Tempo, the bar and the times under it, each
; only when its value moved since it was drawn
; -----------------------------------------------------------------------------
mru_np_dyn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mr_elapsed                 ; AX = seconds, DX = thousandths
    push dx
    cmp ax, [mru_lsec]
    je .nosec
    mov [mru_lsec], ax
    mov di, mru_s
    call mru_time
    mov ax, 1
    call mru_vline
    mov si, mru_s
    call mru_vcells
    call mru_textn
    mov cx, [mru_bar]               ; and under the bar's left end
    mov dx, [mru_tmy]
    mov ax, 5
    call mru_textn
.nosec:
    pop dx                          ; the bar, in pixels
    mov ax, [mru_bar+4]
    sub ax, [mru_bar]
    sub ax, 2
    mul dx
    mov bx, 1000
    div bx                          ; AX = the filled width
    cmp ax, [mru_lbar]
    je .nobar
    call mru_bar_to
.nobar:
    call mru_meas_val               ; AX = the measure (0 = none)
    cmp ax, [mru_lmeas]
    je .nomeas
    mov [mru_lmeas], ax
    mov di, mru_s
    call mru_d3
    mov si, mru_sl
    call mru_cat
    mov ax, [mr_meas]
    cmp byte [mr_loaded], 0
    jne .mt
    xor ax, ax
.mt:
    call mru_d3
    mov ax, 2
    call mru_vline
    mov si, mru_s
    call mru_vcells
    call mru_textn
.nomeas:
    call mr_bpm                     ; AX = beats a minute
    cmp ax, [mru_ltempo]
    je .notempo
    mov [mru_ltempo], ax
    mov di, mru_s
    call mru_dec
    mov si, mru_bpms
    call mru_cat
    mov ax, 3
    call mru_vline
    mov si, mru_s
    call mru_vcells
    call mru_textn
.notempo:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mru_sl:     db ' / ', 0
mru_bpms:   db ' BPM', 0

; mru_meas_val - AX = the measure playing, 1-based (0 with nothing loaded)
mru_meas_val:
    xor ax, ax
    cmp byte [mr_loaded], 0
    je .r
    call mrq_meas
    inc ax
.r:
    ret

; mr_bpm - AX = the tempo in BPM: 60,000,000 / us a quarter
mr_bpm:
    push bx
    push cx
    push dx
    mov bx, [mrq_templo]
    mov cx, [mrq_temphi]
    mov ax, 0x8700                  ; 60,000,000 = 0x0393_8700
    mov dx, 0x0393
    call mr_div32
    pop dx
    pop cx
    pop bx
    ret

; mru_bar_to - the bar's fill from [mru_lbar] to AX pixels: one fill
mru_bar_to:
    push ax
    push bx
    push cx
    push dx
    mov bx, [mru_lbar]
    mov [mru_lbar], ax
    cmp bx, 0xFFFF
    jne .delta
    xor bx, bx                      ; a full paint: white past the end too
    push ax
    mov byte [mru_ink], CWHITE
    mov cx, [mru_bar+4]
    dec cx
    add ax, [mru_bar]
    inc ax
    cmp ax, cx
    ja .nw
    push bx
    mov bx, [mru_bar+2]
    inc bx
    mov dx, [mru_bar+6]
    dec dx
    call mru_fill
    pop bx
.nw:
    pop ax
.delta:
    cmp ax, bx
    je .out
    mov byte [mru_ink], CBLACK      ; growing: black from the old end
    jb .shrink
    jmp short .go
.shrink:
    mov byte [mru_ink], CWHITE      ; shrinking (a new song): white back
    xchg ax, bx
.go:
    mov cx, ax                      ; CX = the right end, BX = the left
    add cx, [mru_bar]
    mov ax, bx
    add ax, [mru_bar]
    inc ax
    mov bx, [mru_bar+2]
    inc bx
    mov dx, [mru_bar+6]
    dec dx
    call mru_fill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mru_draw_rack - the header and sixteen rows
; -----------------------------------------------------------------------------
mru_draw_rack:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, [mru_rkx]               ; the header
    mov dx, [mru_rky]
    sub dx, 12
    cmp byte [mru_cmp], 0
    je .hy
    add dx, 2                       ; (compact: 10 under the rows' top)
.hy:
    mov si, mru_rkh
    cmp word [mru_rkcols], 1
    je .h
    mov si, mru_rkh2
.h:
    mov ax, [mru_p3+4]              ; as much as the pane holds
    sub ax, cx
    sub ax, 2
    shr ax, 1
    shr ax, 1
    shr ax, 1
    call mru_textn
    xor bx, bx
.r:
    mov byte [mru_lw+bx], 0xFF      ; every bar owed
    call mru_rack_row
    inc bx
    cmp bx, MR_NCH
    jb .r
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mru_rkh:    db 'Ch  Lev    Program / Instrument', 0
mru_rkh2:   db 'Ch Lev  Program', 0
MRU_BARW    equ 32                  ; a level bar's width
MRU_BARW2   equ 20                  ; ...in two columns

; mru_rkpos - CX, DX = channel BX's row origin
mru_rkpos:
    push ax
    push bx
    mov ax, bx
    xor dx, dx
    div word [mru_rkrows]           ; AX = the column, DX = the row
    push dx
    mul word [mru_rkcw]
    add ax, [mru_rkx]
    mov cx, ax
    pop ax
    mul word [mru_rp]
    add ax, [mru_rky]
    mov dx, ax
    pop bx
    pop ax
    ret

; mru_rack_row - channel BX's number, bar and program
mru_rack_row:
    push ax
    push cx
    push dx
    push si
    push di
    call mru_rkpos
    mov di, mru_s                   ; the number, right-aligned in two
    mov ax, bx
    inc ax
    cmp ax, 10
    jae .two
    mov byte [di], ' '
    inc di
.two:
    call mru_dec
    mov si, mru_s
    call mru_text
    push cx
    push dx
    add cx, 22                      ; the bar's frame
    mov ax, cx
    push bx
    mov bx, dx
    mov cx, ax
    add cx, MRU_BARW - 1
    cmp word [mru_rkcols], 1
    je .bw
    sub cx, MRU_BARW - MRU_BARW2
.bw:
    mov dx, bx
    add dx, 6
    add ax, [mru_ox]
    add bx, [mru_oy]
    add cx, [mru_ox]
    add dx, [mru_oy]
    push ax
    xor al, al
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    pop bx
    pop dx
    pop cx
    call mru_rack_bar               ; the fill, from nothing
    call mru_rack_prog
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mru_rack_bar - channel BX's bar to its level (one fill of the change)
mru_rack_bar:
    push ax
    push bx
    push cx
    push dx
    push si
    call mru_rkpos
    add cx, 23                      ; the bar's inside
    inc dx
    mov si, MRU_BARW - 2
    cmp word [mru_rkcols], 1
    je .w
    mov si, MRU_BARW2 - 2
.w:
    xor ax, ax
    cmp byte [mr_levels], 0
    je .lv
    mov al, [mrc_lev+bx]            ; the length: level x width / 128
    push dx
    push cx
    mul si
    mov cl, 7
    shr ax, cl
    pop cx
    pop dx
.lv:
    cmp ax, si
    jbe .lk
    mov ax, si
.lk:
    mov ah, [mru_lw+bx]             ; AH = the length drawn
    mov [mru_lw+bx], al
    cmp ah, 0xFF
    jne .delta
    push ax                         ; a full paint: the white part first
    mov byte [mru_ink], CWHITE
    xor ah, ah
    push cx
    push dx
    add ax, cx                      ; x1 = left + length
    mov bx, dx
    add cx, si
    dec cx
    add dx, 4
    cmp ax, cx
    ja .nw
    call mru_fill
.nw:
    pop dx
    pop cx
    pop ax
    xor ah, ah
.delta:
    cmp al, ah
    je .out
    mov byte [mru_ink], CBLACK
    ja .grow
    mov byte [mru_ink], CWHITE
    xchg al, ah
.grow:                              ; AH = the shorter, AL = the longer
    mov bl, ah
    xor bh, bh
    xor ah, ah
    add ax, cx                      ; x2 = left + longer - 1
    dec ax
    add bx, cx                      ; x1 = left + shorter
    push ax
    mov ax, bx
    pop cx
    mov bx, dx
    add dx, 4
    call mru_fill
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_rack_prog - channel BX's program number and name ("Drum Kit" on 10)
mru_rack_prog:
    push ax
    push cx
    push dx
    push si
    push di
    call mru_rkpos
    add cx, 26 + MRU_BARW
    cmp word [mru_rkcols], 1
    je .x
    sub cx, MRU_BARW - MRU_BARW2
.x:
    mov di, mru_s
    cmp bx, MR_DRUMCH
    jne .mel
    mov si, mru_s_kit
    call mru_cat
    jmp short .put
.mel:
    mov al, [mrc_prog+bx]
    xor ah, ah
    push ax
    inc ax                          ; programs are named 1-based
    call mru_d3
    mov byte [di], ' '
    inc di
    pop ax
    call mr_gmname
    call mru_cat
.put:
    mov ax, [mru_rkx]               ; the cells to the column's right edge
    add ax, [mru_rkcw]
    cmp word [mru_rkcols], 2
    jne .w
    push bx
    push dx
    mov ax, bx
    xor dx, dx
    div word [mru_rkrows]
    inc ax
    mul word [mru_rkcw]
    add ax, [mru_rkx]
    pop dx
    pop bx
.w:
    sub ax, cx
    sub ax, 4
    jns .wp
    xor ax, ax
.wp:
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov si, mru_s
    call mru_textn
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

mru_s_kit:  db '--- Drum Kit', 0

; -----------------------------------------------------------------------------
; mru_draw_status - the three lines, the divider and the check boxes
; -----------------------------------------------------------------------------
mru_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_st_lines
    mov ax, [mru_p4+4]              ; the divider
    sub ax, 168
    add ax, [mru_ox]
    mov bx, [mru_p4+2]
    add bx, 14
    add bx, [mru_oy]
    mov cx, ax
    mov dx, [mru_p4+6]
    sub dx, 4
    add dx, [mru_oy]
    push ax
    xor al, al
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    xor di, di
    mov bx, mru_ck1
    call os88ui_chk
    mov bx, mru_ck2
    call os88ui_chk
    mov bx, mru_ck3                 ; Play in Background means something only
    call mr_isspk                   ; where the speaker is the output
    jc .bg
    mov di, OS88UI_DIS
.bg:
    call os88ui_chk
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_st_lines - the Status pane's three lines
mru_st_lines:
    push ax
    push cx
    push dx
    push si
    call mru_mkdev
    call mru_mkfile
    mov cx, [mru_stx]
    mov dx, [mru_sty]
    mov ax, [mru_stw]
    mov si, mru_dev
    call mru_textn
    add dx, [mru_rp]
    mov si, mru_file
    call mru_textn
    add dx, [mru_rp]
    mov si, [mr_msg]
    call mru_textn
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mru_draw_msg - compact: the message alone, under the panes
mru_draw_msg:
    push ax
    push cx
    push dx
    push si
    mov cx, 6
    mov dx, [mru_sty]
    mov ax, [mru_cw]
    sub ax, 12
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov si, [mr_msg]
    call mru_textn
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mru_status_msg - the message line alone (after a command), clipped by the
; caller's region
mru_status_msg:
    cmp byte [mru_cmp], 0
    jne mru_draw_msg
    push ax
    push cx
    push dx
    push si
    mov cx, [mru_stx]
    mov dx, [mru_sty]
    add dx, [mru_rp]
    add dx, [mru_rp]
    mov ax, [mru_stw]
    mov si, [mr_msg]
    call mru_textn
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mru_mkdev - status line 1: what the machine has and what is playing it
mru_mkdev:
    push ax
    push si
    push di
    mov di, mru_dev
    mov al, [mr_out]
    cmp al, MRO_NONE
    jne .k
    call mro_pick
.k:
    cmp al, MRO_FM
    jne .n1
    mov si, mru_d_opl2
    cmp byte [mr_fmkind], SND_OPL3
    jne .f
    mov si, mru_d_opl3
.f:
    call mru_cat
    mov si, mru_d_fm
    call mru_cat
    jmp short .out
.n1:
    cmp al, MRO_SB
    jne .n2
    mov si, mru_d_sb
    call mru_cat
    mov ax, [mrb_rate]
    or ax, ax
    jnz .sr
    call mrb_rate_for
.sr:
    call mru_decc
    mov si, mru_d_hz
    call mru_cat
    jmp short .out
.n2:
    mov si, mru_d_spk
    cmp al, MRO_TONE
    jne .sp
    mov si, mru_d_tone
.sp:
    call mru_cat
.out:
    pop di
    pop si
    pop ax
    ret

mru_d_opl2: db 'OPL2 detected at 0x388', 0
mru_d_opl3: db 'OPL3 detected at 0x388', 0
mru_d_fm:   db ' - FM driver loaded', 0
mru_d_sb:   db 'Sound Blaster DSP - synth at ', 0
mru_d_hz:   db ' Hz', 0
mru_d_spk:  db 'PC speaker - pulse-width synth', 0
mru_d_tone: db 'PC speaker - melody line, in the background', 0

; mrb_rate_for - AX = the rate the card's synth would run at on this CPU
mrb_rate_for:
    mov ax, 8000
    cmp byte [mr_cpu], CPU_8086
    je .r
    mov ax, 16000
    cmp byte [mr_cpu], CPU_286
    je .r
    mov ax, 22050
.r:
    ret

; mru_mkfile - status line 2: "File: NAME (6,337 bytes) - Standard MIDI File
; (Type 1)"
mru_mkfile:
    push ax
    push si
    push di
    mov di, mru_file
    cmp byte [mr_loaded], 0
    jne .y
    mov si, mru_f_none
    call mru_cat
    jmp short .out
.y:
    mov si, mru_f_file
    call mru_cat
    call mru_name
    call mru_cat
    mov si, mru_f_op
    call mru_cat
    mov ax, [mr_fsize]
    call mru_decc
    mov si, mru_f_bytes
    call mru_cat
    mov al, [mrq_fmt]
    add al, '0'
    mov [di], al
    mov byte [di+1], 0
.out:
    pop di
    pop si
    pop ax
    ret

mru_f_none:  db 'No file loaded', 0
mru_f_file:  db 'File: ', 0
mru_f_op:    db ' (', 0
mru_f_bytes: db ' bytes) - MIDI type ', 0

; -----------------------------------------------------------------------------
; mru_draw_buttons - every live button of the group
; -----------------------------------------------------------------------------
mru_draw_buttons:
    push ax
    push bx
    push cx
    mov cx, MRU_NB                  ; EVERY button the layout has - not the
    cmp byte [mru_cmp], 0           ; group's live count, which is 0 under a
    je .n                           ; card and would leave the row unpainted.
    mov cx, MRU_NBC                 ; os88ui_btn draws nothing past the live
.n:                                 ; count, so it is raised for the paint and
    push word [mru_btrec+OS88UI_BT_N]   ; put back after: only the UI task
    mov [mru_btrec+OS88UI_BT_N], cx     ; draws buttons, and nothing presses
    xor ax, ax                          ; in between
.b:
    cmp ax, cx
    jae .out
    inc ax
    mov bx, mru_btrec
    call os88ui_btn
    jmp short .b
.out:
    pop word [mru_btrec+OS88UI_BT_N]
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mru_changed - CF = 1 when the worker has anything to draw
; =============================================================================
mru_changed:
    push ax
    push bx
    push dx
    xor bx, bx
.d:
    cmp byte [mrc_dirty+bx], 0
    jne .yes
    inc bx
    cmp bx, MR_NCH
    jb .d
    cmp byte [mr_state], MR_ST_PLAY
    jne .no
    call mr_elapsed
    cmp ax, [mru_lsec]
    jne .yes
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; mru_dyn - the cheap redraw: the clock, the bar, the measure, the tempo, and
; the rack's rows whose bar or program moved. The clip is armed by the caller
; =============================================================================
mru_dyn:
    push ax
    push bx
    call mru_layout
    call mru_origin
    cmp byte [mr_card], 0
    jne .out
    call mru_np_dyn
    xor bx, bx
.c:
    mov al, [mrc_dirty+bx]
    or al, al
    jz .n
    mov byte [mrc_dirty+bx], 0
    test al, 2
    jz .np
    call mru_rack_bar
.np:
    test al, 1
    jz .n
    call mru_rack_prog
.n:
    inc bx
    cmp bx, MR_NCH
    jb .c
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; mru_repaint - white the content and draw it all, outside W_PAINT (a command
; changed the state): the clip armed, then DISARMED (the Audio Player's
; reason, apcb.inc)
; =============================================================================
mru_repaint:
    push ax
    push bx
    push cx
    push dx
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_layout
    call mru_origin
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [mru_ox]
    mov bx, [mru_oy]
    mov cx, ax
    add cx, [mru_cw]
    dec cx
    mov dx, bx
    add dx, [mru_ch]
    dec dx
    call OSAPI_GFX_FILL
    xor al, al
    call OSAPI_SET_COLOR
    call mru_draw
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_refresh - the panes that a transport command changes, without the full
; white: the list, Now Playing's values, the buttons and the message
mru_refresh:
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_layout
    call mru_origin
    call mru_aim
    cmp byte [mr_card], 0
    jne .card
    call mru_draw_list
    call mru_np_static
    mov word [mru_lsec], 0xFFFF
    mov word [mru_lmeas], 0xFFFF
    mov word [mru_ltempo], 0xFFFF
    call mru_np_dyn
    call mru_draw_buttons
    cmp byte [mru_cmp], 0
    jne .msg
    call mru_st_lines
    jmp short .cl
.msg:
    call mru_draw_msg
.cl:
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    ret
.card:
    call OSAPI_WM_CLIP_CLEAR
    pop bx
    jmp mru_repaint

; =============================================================================
; THE CARDS (SPEC.md 105.9.3): About, MIDI Info and Settings, drawn last over
; the content. While one is up the button group has no live controls
; (mru_aim), so a press reaches mr_onclick and the card has it
; =============================================================================
MR_CARD_ABOUT equ 1
MR_CARD_INFO  equ 2
MR_CARD_SET   equ 3

mru_draw_card:
    push bx
    push si
    mov al, [mr_card]
    or al, al
    jz .out
    mov bx, [mr_win]
    mov si, mr_ablines
    cmp al, MR_CARD_ABOUT
    je .ab
    mov si, mr_inlines
    cmp al, MR_CARD_INFO
    je .info
    call mru_setcard
    jmp short .out
.info:
    call mr_info_build
.ab:
    call os88ui_about_d
.out:
    pop si
    pop bx
    ret

mr_ablines:
    dw mr_ab1, mr_ab2, mr_ab0, mr_ab3, mr_ab4, mr_ab0, mr_ab5, 0
mr_ab0:     db 0
mr_ab1:     db 'MIDIRack for os8088', 0
mr_ab2:     db 'Version 1.0', 0
mr_ab3:     db 'Standard MIDI Files on an OPL2 / OPL3,', 0
mr_ab4:     db 'a Sound Blaster, or the PC speaker', 0
mr_ab5:     db 'FM bank and demo songs: original, CC0', 0

; --- MIDI Info: its lines, built when it is drawn -----------------------------
mr_inlines:
    dw mr_in0, mr_ab0, mr_in1, mr_in2, mr_in3, mr_in4, mr_in5, mr_ab0, mr_in6, 0
mr_in0:     db 'MIDI Info', 0
    MRBUF mr_in1, 40
    MRBUF mr_in2, 40
    MRBUF mr_in3, 40
    MRBUF mr_in4, 40
    MRBUF mr_in5, 40
    MRBUF mr_in6, 48

mr_info_build:
    push ax
    push dx
    push si
    push di
    mov di, mr_in1                  ; File: NAME, 6,337 bytes
    mov si, mru_f_file
    call mru_cat
    call mru_name
    call mru_cat
    mov si, mr_in_comma
    call mru_cat
    mov ax, [mr_fsize]
    call mru_decc
    mov si, mr_in_bytes
    call mru_cat
    mov di, mr_in2                  ; Format: type 1, 9 tracks
    mov si, mr_in_fmt
    call mru_cat
    mov al, [mrq_fmt]
    xor ah, ah
    call mru_dec
    mov si, mr_in_comma
    call mru_cat
    mov al, [mrq_ntrk]
    xor ah, ah
    call mru_dec
    mov si, mr_in_trk
    call mru_cat
    mov di, mr_in3                  ; Division: 96 ticks a quarter
    mov si, mr_in_div
    call mru_cat
    mov ax, [mrq_div]
    call mru_dec
    mov si, mr_in_tpq
    call mru_cat
    mov di, mr_in4                  ; Length: 03:42, 96 measures
    mov si, mr_in_len
    call mru_cat
    mov ax, [mr_lenlo]
    mov dx, [mr_lenhi]
    mov si, 1000
    cmp dx, si
    jae .lz
    div si
    jmp short .lt
.lz:
    xor ax, ax
.lt:
    call mru_time
    mov si, mr_in_comma
    call mru_cat
    mov ax, [mr_meas]
    call mru_dec
    mov si, mr_in_meas
    call mru_cat
    mov di, mr_in5                  ; Notes: 1,234 on 9 channels
    mov si, mr_in_notes
    call mru_cat
    mov ax, [mr_nnotlo]
    cmp word [mr_nnothi], 0
    je .nn
    mov ax, 65535
.nn:
    call mru_decc
    mov si, mr_in_on
    call mru_cat
    mov ax, [mr_chused]             ; channels: the bits set
    xor dx, dx
.cb:
    or ax, ax
    jz .cbd
    shr ax, 1
    adc dx, 0
    jmp short .cb
.cbd:
    mov ax, dx
    call mru_dec
    mov si, mr_in_ch
    call mru_cat
    mov di, mr_in6                  ; the first text the song carries
    mov si, mr_text
    cmp byte [mr_text], 0
    jne .tx
    mov si, mr_in_notext
.tx:
    call mru_cat
    pop di
    pop si
    pop dx
    pop ax
    ret

mr_in_comma: db ', ', 0
mr_in_bytes: db ' bytes', 0
mr_in_fmt:   db 'Format: type ', 0
mr_in_trk:   db ' tracks', 0
mr_in_div:   db 'Division: ', 0
mr_in_tpq:   db ' ticks a quarter note', 0
mr_in_len:   db 'Length: ', 0
mr_in_meas:  db ' measures', 0
mr_in_notes: db 'Notes: ', 0
mr_in_on:    db ' on ', 0
mr_in_ch:    db ' channels', 0
mr_in_notext: db '(no text in the file)', 0

; --- Settings: the output, a radio group in a card ----------------------------
    MRBUF mru_sc, 8                 ; the card's rect (screen)
    MRBUF mru_rad, 18               ; OS88UI_RD_SIZE (asserted below)
mru_radit:  dw mru_r0, mru_r1, mru_r2, mru_r3, mru_r4
mru_r0:     db 'Automatic (the best there is)', 0
mru_r1:     db 'OPL3 FM, 17 voices, stereo', 0
mru_r2:     db 'OPL2 FM (AdLib), 8 voices', 0
mru_r3:     db 'Sound Blaster, software synth', 0
mru_r4:     db 'PC speaker, software synth', 0
mru_sct:    db 'Settings - Output device', 0
mru_sch:    db 'Takes effect at the next Play.', 0
mru_sch2:   db 'Click outside the list to close.', 0
MRU_SCW     equ 300
MRU_SCH     equ 132

mru_setcard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_screct
    jmp mru_setdraw

; mru_screct - the card's rect and its radio group's record, from the layout
mru_screct:
    push ax
    mov ax, [mru_cw]                ; centred, clamped to the content
    sub ax, MRU_SCW
    jns .x
    xor ax, ax
.x:
    shr ax, 1
    add ax, [mru_ox]
    mov [mru_sc], ax
    add ax, MRU_SCW - 1
    mov [mru_sc+4], ax
    mov ax, [mru_ch]
    sub ax, MRU_SCH
    jns .y
    xor ax, ax
.y:
    shr ax, 1
    add ax, [mru_oy]
    mov [mru_sc+2], ax
    add ax, MRU_SCH - 1
    mov [mru_sc+6], ax
    call mru_radset
    pop ax
    ret

mru_setdraw:
    mov al, CWHITE                  ; the card's ground, then its frame
    call OSAPI_SET_COLOR
    mov ax, [mru_sc]
    mov bx, [mru_sc+2]
    mov cx, [mru_sc+4]
    mov dx, [mru_sc+6]
    call OSAPI_GFX_FILL
    xor al, al
    call OSAPI_SET_COLOR
    call OSAPI_GFX_FRAME
    mov cx, [mru_sc]
    add cx, 10
    mov dx, [mru_sc+2]
    add dx, 7
    mov si, mru_sct
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    xor di, di
    mov bx, mru_rad
    call os88ui_rad
    mov cx, [mru_sc]
    add cx, 10
    mov dx, [mru_sc+6]
    sub dx, 24
    mov si, mru_sch
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    add dx, 11
    mov si, mru_sch2
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_radset - the radio group's record, from the card and the state
mru_radset:
    push ax
    mov ax, [mru_sc]
    add ax, 14
    mov [mru_rad+OS88UI_RD_RECT], ax
    mov ax, [mru_sc+2]
    add ax, 22
    mov [mru_rad+OS88UI_RD_RECT+2], ax
    add ax, 5 * 16 - 1
    mov [mru_rad+OS88UI_RD_RECT+6], ax
    mov ax, [mru_sc+4]
    sub ax, 10
    mov [mru_rad+OS88UI_RD_RECT+4], ax
    mov word [mru_rad+OS88UI_RD_ITEMS], mru_radit
    mov word [mru_rad+OS88UI_RD_N], 5
    mov al, [mr_want]
    xor ah, ah
    mov [mru_rad+OS88UI_RD_SEL], ax
    mov word [mru_rad+OS88UI_RD_PITCH], 16
    xor ax, ax                      ; what this machine does not have, greyed
    cmp byte [mr_fmkind], SND_OPL3
    je .o3
    or al, 2                        ; no OPL3
.o3:
    cmp byte [mr_fmkind], 0
    jne .o2
    or al, 4                        ; no FM at all
.o2:
    cmp byte [mr_hassb], 0
    jne .sb
    or al, 8                        ; no DSP
.sb:
    mov [mru_rad+OS88UI_RD_DIS], ax
    pop ax
    ret
