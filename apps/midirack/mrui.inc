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
MRU_COLW    equ 600                 ; ...and the box the colour face needs: an
MRU_COLH    equ 270                 ; EGA's is ~624 x 279, a VGA's 624 x 311
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
    MRB mru_col                     ; 1 = the COLOUR face (SPEC.md 105.9.5):
                                    ; a 4bpp display, decided at each full
                                    ; paint, and tall
    MRW mra_seg                     ; MIDIRACK.GFX's claim, or 0
    MRW mru_hdr                     ; a pane's title strip
    MRW mru_lbt                     ; the list box's top, under the title
    MRW mru_nrp                     ; Now Playing's line pitch
    MRW mru_nmx                     ; ...the name's x, y and cells
    MRW mru_nmy
    MRW mru_nmw
    MRBUF mru_lcd, 8                ; the colour face's display
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
    cmp byte [mru_cmp], 0           ; the colour face is tall, and wide
    jne .mono
    cmp cx, MRU_COLW
    jb .mono
    cmp dx, MRU_COLH
    jb .mono
    cmp byte [mru_col], 0
    je .mono0
    call mru_lay_col
    jmp .nost
.mono:
    mov byte [mru_col], 0
.mono0:
    mov word [mru_hdr], 14
    mov word [mru_lbt], 14
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
    add ax, [mru_lbt]
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
    cmp ax, MRU_LMAX
    jbe .lr
    mov ax, MRU_LMAX
.lr:
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
    mov [mru_nrp], ax
    mov ax, [mru_p2]                ; the name, in the black bar
    add ax, 8
    mov [mru_nmx], ax
    mov bx, [mru_p2+4]
    sub bx, 4
    sub bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov [mru_nmw], bx
    mov ax, [mru_p2+2]
    add ax, 15
    cmp byte [mru_cmp], 0
    je .nmy
    dec ax
.nmy:
    mov [mru_nmy], ax
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
    mov ax, [mru_rp]                ; the mono face's rows: the window's
    mov [mru_rkp], ax               ; pitch, a black bar in a black frame
    mov word [mru_rbx], 22
    mov ax, MRU_BARW
    cmp word [mru_rkcols], 1
    je .bw
    mov ax, MRU_BARW2
.bw:
    mov [mru_rbw], ax
    add ax, 26 - 22 + 22
    mov [mru_rpx], ax
    mov [mru_rz1], ax               ; (every zone black: past any length)
    mov [mru_rz2], ax
    mov byte [mru_rc1], CBLACK
    mov byte [mru_rc2], CBLACK
    mov byte [mru_rc3], CBLACK
    mov byte [mru_rc0], CWHITE
    mov byte [mru_rcf], CBLACK
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
    mov ax, [mru_p4+4]              ; the text runs to the divider, 176 px
    sub ax, 186                     ; from the right
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

;  =============================================================================
; mru_lay_col - THE COLOUR FACE'S LAYOUT (SPEC.md 105.9.5), from the live box.
; Fixed columns where the mono face's are shares - the playlist 140 (an 8.3
; name whole), Now Playing 248, the rack the rest (221 on a VGA: a 16-letter
; instrument whole) - because the transport's faces are BLITP pictures and
; sit on the card's 8-pixel grid: Now Playing's inside starts at 152, five
; faces at a pitch of 48. The rows are anchored BOTTOM-UP so the panes' last
; rows line up across the window: Load Directory with Eject and Loop, Add and
; Remove with the transport's bottom edge
; =============================================================================
MRU_CP1     equ 4                   ; the playlist: 4..143
MRU_CP2     equ 148                 ; Now Playing: 148..395 - its inside at
MRU_CP3     equ 399                 ; 152, on the grid; the rack from 399
MRU_FACEP   equ 48                  ; the transport's pitch (a face's width)

mru_lay_col:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [mru_rp], 11
    mov word [mru_bh], 18
    mov word [mru_hdr], 14
    mov word [mru_lbt], 18
    mov word [mru_capy], 0          ; the captions are IN the faces
    mov word [mru_nplines], 6
    ; --- the bottom row: Settings, MIDI Info, About ... Quit --------------
    mov dx, [mru_ch]
    sub dx, 5                       ; DX = its y2
    mov bx, dx
    sub bx, 17                      ; BX = its y1
    mov di, MRB_SET
    mov ax, 4
    mov cx, 99
    call mru_brect
    mov di, MRB_INFO
    mov ax, 104
    mov cx, 199
    call mru_brect
    mov di, MRB_ABOUT
    mov ax, 204
    mov cx, 299
    call mru_brect
    mov di, MRB_QUIT
    mov cx, [mru_cw]
    sub cx, 5
    mov ax, cx
    sub ax, 95
    call mru_brect
    ; --- the Status pane: a title, three lines, three boxes ---------------
    mov ax, bx
    sub ax, 5
    mov [mru_p4+6], ax
    sub ax, 55
    mov [mru_p4+2], ax
    mov word [mru_p4], 4
    mov cx, [mru_cw]
    sub cx, 5
    mov [mru_p4+4], cx
    mov ax, [mru_p4+2]
    add ax, 18
    mov [mru_sty], ax
    mov word [mru_stx], 4 + 8
    mov ax, cx                      ; the text runs to the divider
    sub ax, 186 + 4 + 8
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [mru_stw], ax
    mov word [mru_ck1+OS88UI_CK_LABEL], mru_l_lev
    mov word [mru_ck2+OS88UI_CK_LABEL], mru_l_loop
    mov word [mru_ck3+OS88UI_CK_LABEL], mru_l_bg
    ; --- the three panes --------------------------------------------------
    mov ax, [mru_p4+2]
    sub ax, 4
    mov [mru_p1+6], ax
    mov [mru_p2+6], ax
    mov [mru_p3+6], ax
    mov word [mru_p1+2], 4
    mov word [mru_p2+2], 4
    mov word [mru_p3+2], 4
    mov word [mru_p1], MRU_CP1
    mov word [mru_p1+4], MRU_CP2 - 5
    mov word [mru_p2], MRU_CP2
    mov word [mru_p2+4], MRU_CP3 - 4
    mov word [mru_p3], MRU_CP3
    mov ax, [mru_cw]
    sub ax, 5
    mov [mru_p3+4], ax
    call mru_lay_list               ; the playlist's shape is the mono one's
    ; --- Now Playing: Eject / Loop at the bottom, the faces above, the
    ;     display filling the rest ------------------------------------------
    mov dx, [mru_p2+6]
    sub dx, 4                       ; DX = Eject's y2
    mov bx, dx
    sub bx, 17
    mov ax, MRU_CP2 + 4
    mov cx, (MRU_CP2 + MRU_CP3 - 4) / 2 - 2
    mov di, MRB_EJECT
    call mru_brect
    mov ax, cx
    add ax, 4
    mov cx, MRU_CP3 - 4 - 4
    mov di, MRB_LOOP
    call mru_brect
    mov dx, bx                      ; the faces: 30 rows, 5 above Eject
    sub dx, 5
    mov bx, dx
    sub bx, 29
    mov ax, MRU_CP2 + 4
    mov di, MRB_PREV
.t:
    mov cx, ax
    add cx, 43                      ; the button is 44 of the face's 48
    call mru_brect
    add ax, MRU_FACEP
    inc di
    cmp di, MRB_NEXT
    jbe .t
    mov word [mru_lcd], MRU_CP2 + 4 ; the display
    mov word [mru_lcd+4], MRU_CP3 - 4 - 4
    mov ax, [mru_p2+2]
    add ax, [mru_hdr]
    add ax, 4
    mov [mru_lcd+2], ax
    mov cx, bx
    sub cx, 6
    mov [mru_lcd+6], cx
    sub cx, ax                      ; CX = its height - 1
    ; the pitch of its six lines: what is left of the height after the
    ; name, the bar, the times and their gaps (45 + 5 pitches), 10..14
    mov ax, cx
    sub ax, 45 + 4
    jns .p0
    xor ax, ax
.p0:
    mov si, 5
    xor dx, dx
    div si
    cmp ax, 10
    jae .p1
    mov ax, 10
.p1:
    cmp ax, 14
    jbe .p2
    mov ax, 14
.p2:
    mov [mru_nrp], ax
    mov si, ax                      ; ...and the air above and below it
    mov dx, 5
    mul dx
    add ax, 45
    neg ax
    add ax, cx
    sar ax, 1
    jns .p3
    xor ax, ax
.p3:
    add ax, [mru_lcd+2]             ; AX = the name's y
    mov [mru_nmy], ax
    mov word [mru_nmx], MRU_CP2 + 4 + 6
    mov word [mru_npx], MRU_CP2 + 4 + 6
    mov word [mru_nmw], (MRU_CP3 - 4 - 4 - 4 - (MRU_CP2 + 4 + 6)) / 8
    add ax, 8 + 6
    mov [mru_npy], ax               ; the first line
    mov dx, si
    shl dx, 1
    shl dx, 1
    add dx, si
    add ax, dx                      ; + 5 pitches: the last line's y
    add ax, 8 + 4
    mov word [mru_bar], MRU_CP2 + 4 + 6
    mov word [mru_bar+4], MRU_CP3 - 4 - 4 - 6
    mov [mru_bar+2], ax
    add ax, 7
    mov [mru_bar+6], ax
    add ax, 4
    mov [mru_tmy], ax
    ; --- the rack ---------------------------------------------------------
    mov ax, [mru_p3]
    add ax, 4
    mov [mru_rkx], ax
    mov ax, [mru_p3+2]
    add ax, [mru_hdr]
    add ax, 16
    mov [mru_rky], ax
    mov bx, [mru_p3+6]              ; sixteen rows, 12 apart if they fit
    sub bx, 2
    sub bx, ax
    mov ax, bx
    mov cl, 4
    shr ax, cl
    cmp ax, 12
    jbe .rp
    mov ax, 12
.rp:
    mov [mru_rkp], ax
    mov word [mru_rkcols], 1
    mov word [mru_rkrows], 16
    mov ax, [mru_p3+4]
    sub ax, [mru_p3]
    sub ax, 8
    mov [mru_rkcw], ax
    mov word [mru_rbx], 20
    mov word [mru_rbw], MRU_CBARW
    mov word [mru_rpx], 20 + MRU_CBARW + 4
    mov word [mru_rz1], (MRU_CBARW - 2) * 5 / 8
    mov word [mru_rz2], (MRU_CBARW - 2) * 7 / 8
    mov byte [mru_rc1], CLGREEN
    mov byte [mru_rc2], CYELLOW
    mov byte [mru_rc3], CLRED
    mov byte [mru_rc0], CBLACK
    mov byte [mru_rcf], CDGRAY
    pop di
    pop si
    pop dx
    pop cx
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
    call mru_recmain                ; the rack's buttons, live...
    cmp byte [mr_card], 0
    je .nc
    mov word [mru_btrec+OS88UI_BT_N], 0 ; ...or none under a card - and under
    cmp byte [mr_card], MR_CARD_SET ; the Settings dialog, ITS pair: one
    jne .nc                         ; record, paged (os88ui.inc's BT_N note)
    call mru_screct
    mov word [mru_btrec+OS88UI_BT_RECTS], mru_crect
    mov word [mru_btrec+OS88UI_BT_LABELS], mru_clabs
    mov word [mru_btrec+OS88UI_BT_FLAGS], mru_cflg
    mov word [mru_btrec+OS88UI_BT_N], 2
.nc:
    call mru_flags
    ; --- the check boxes: rows of the Status pane's right side -------------
    cmp byte [mru_cmp], 0
    jne .nock
    mov ax, [mru_p4+4]              ; (8 clear of the pane's right edge)
    sub ax, 168
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

; mru_recmain - the record pointed at the rack's own buttons, its live count
; the layout's: all fourteen, or compact's ten
mru_recmain:
    push ax
    mov word [mru_btrec+OS88UI_BT_RECTS], mru_rect
    mov ax, mru_blabs
    cmp byte [mru_col], 0
    je .lab
    mov ax, mru_blabc               ; colour: every one is OUR painter
.lab:
    mov [mru_btrec+OS88UI_BT_LABELS], ax
    mov word [mru_btrec+OS88UI_BT_FLAGS], mru_bflg
    mov ax, MRU_NB
    cmp byte [mru_cmp], 0
    je .n
    mov ax, MRU_NBC
.n:
    mov [mru_btrec+OS88UI_BT_N], ax
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
    mov ax, OS88UI_FILL             ; colour: every button the package's own
    cmp byte [mru_col], 0           ; (SPEC.md 13.8.10)
    je .base
    mov ax, OS88UI_FILL | OS88UI_OWN
.base:
    xor bx, bx
.z:
    mov [mru_bflg+bx], ax
    add bx, 2
    cmp bx, MRU_NB * 2
    jb .z
    cmp byte [mru_col], 0           ; mono: the transport's pictures
    jne .tr
    mov ax, OS88UI_IMG | OS88UI_FILL
.tr:
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
mru_l_add:   db 'Add', 0
mru_l_rem:   db 'Remove', 0
mru_l_dir:   db 'Load Directory', 0
mru_l_eject: db 'Eject', 0
mru_l_loopb: db 'Loop', 0
mru_l_set:   db 'Settings', 0
mru_l_info:  db 'MIDI Info', 0
mru_l_about: db 'About', 0
mru_l_quit:  db 'Quit', 0
mru_caps:    dw mru_c_prev, mru_c_play, mru_c_pause, mru_c_stop, mru_c_next
; the colour face's labels: every entry the painter (OS88UI_OWN), which
; reads the caption below by the button's index
mru_blabc:  times MRU_NB dw mru_bpaint
mru_bcap:   dw mru_l_add, mru_l_rem, mru_l_dir
            dw mru_c_prev, mru_c_play, mru_c_pause, mru_c_stop, mru_c_next
            dw mru_l_eject, mru_l_loopb
            dw mru_l_set, mru_l_info, mru_l_about, mru_l_quit

; -----------------------------------------------------------------------------
; mru_bpaint - the colour face's button (SPEC.md 105.9.5, os88ui.inc's
; OS88UI_OWN): AX = the index, BX = the rect (screen), DI = the flags with
; the pressed look resolved. The transport's five are MIDIRACK.GFX's
; pictures, one OSAPI_GFX_BLITP each - a plane a row, no decode; every other
; button, and those five when the file is not here or the card refuses the
; block (a straddle, an origin off the grid), is a bevelled grey button with
; its caption. Every register preserved
; -----------------------------------------------------------------------------
mru_bpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [mru_pbx], bx
    mov [mru_pdi], di
    mov [mru_pax], ax
    cmp ax, MRB_PREV
    jb .bevel
    cmp ax, MRB_NEXT
    ja .bevel
    cmp word [mra_seg], 0
    je .bevel
    sub ax, MRB_PREV                ; the face: button x 3 + the state
    mov cx, ax
    shl ax, 1
    add ax, cx
    test di, OS88UI_DIS
    jz .nd
    add ax, 2
    jmp short .st
.nd:
    test di, OS88UI_DOWN
    jz .st
    inc ax
.st:
    mov cx, MRA_FACE
    mul cx
    mov si, ax                      ; ES:SI = its plane 0
    mov es, [mra_seg]
    mov ax, [bx]
    mov bx, [bx+2]
    mov cx, MRA_W
    mov dx, MRA_H
    mov di, (MRA_H * MRA_W / 8) | 0x4000    ; the plane step, walking a clip
    mov bp, MRA_W / 8
    call OSAPI_GFX_BLITP
    jnc .out
.bevel:
    mov ax, [mru_pax]
    mov bx, [mru_pbx]
    mov di, [mru_pdi]
    call mru_bevel
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRW mru_pax                     ; mru_bpaint's arguments, for the
    MRW mru_pbx                     ; bevel a refused blit falls back to
    MRW mru_pdi
    MRBUF mru_bv, 8                 ; mru_bevel's rect...
    MRBUF mru_bf, 8                 ; ...its face...
    MRW mru_btx                     ; ...and its caption's pen and width
    MRW mru_bty
    MRW mru_btw

; mru_bevel - button AX's bevelled face at rect BX, flags DI: a black frame,
; a light top-left and a grey bottom-right (or one grey line in at the top
; and the left, pressed, with the caption a pixel down and right), and the
; caption opaque on the grey with the grey ringed round it - each pixel once
mru_bevel:
    push ax
    push bx
    push cx
    push dx
    push si
    shl ax, 1
    mov si, ax
    mov si, [mru_bcap+si]           ; SI = the caption
    mov ax, [bx]
    mov [mru_bv], ax
    mov ax, [bx+2]
    mov [mru_bv+2], ax
    mov ax, [bx+4]
    mov [mru_bv+4], ax
    mov ax, [bx+6]
    mov [mru_bv+6], ax
    xor al, al                      ; the frame
    call OSAPI_SET_COLOR
    mov ax, [mru_bv]
    mov bx, [mru_bv+2]
    mov cx, [mru_bv+4]
    mov dx, [mru_bv+6]
    call OSAPI_GFX_FRAME
    inc ax                          ; the face, before the bevel takes its
    inc bx                          ; lines out of it
    dec cx
    dec dx
    mov [mru_bf], ax
    mov [mru_bf+2], bx
    mov [mru_bf+4], cx
    mov [mru_bf+6], dx
    xor cx, cx                      ; CX = the caption's shift
    test di, OS88UI_DIS
    jnz .up
    test di, OS88UI_DOWN
    jz .up
    mov al, CDGRAY                  ; PRESSED: one grey line in at the top
    mov ah, CDGRAY                  ; and the left
    call mru_bvtl
    inc cx
    jmp short .face
.up:
    mov al, CWHITE                  ; UP: light top-left...
    mov ah, CWHITE
    call mru_bvtl
    mov al, CDGRAY                  ; ...grey bottom-right
    call mru_bvbr
.face:
    push cx
    push si
    call OSAPI_FONT_WIDTH           ; AX = the caption's width
    pop si
    pop cx
    mov [mru_btw], ax
    mov dx, [mru_bv+4]              ; centred in the button, + the shift
    sub dx, [mru_bv]
    inc dx
    sub dx, ax
    sar dx, 1
    add dx, [mru_bv]
    add dx, cx
    cmp dx, [mru_bf]
    jge .x1
    mov dx, [mru_bf]
.x1:
    mov [mru_btx], dx
    mov dx, [mru_bv+6]
    sub dx, [mru_bv+2]
    inc dx
    sub dx, 8
    sar dx, 1
    add dx, [mru_bv+2]
    add dx, cx
    mov [mru_bty], dx
    mov byte [mru_ink], CLGRAY      ; the ring: above, below, left, right
    mov ax, [mru_bf]
    mov bx, [mru_bf+2]
    mov cx, [mru_bf+4]
    mov dx, [mru_bty]
    dec dx
    call mru_afill
    mov bx, [mru_bty]
    add bx, 8
    mov dx, [mru_bf+6]
    call mru_afill
    mov bx, [mru_bty]
    mov dx, bx
    add dx, 7
    mov cx, [mru_btx]
    dec cx
    call mru_afill
    mov ax, [mru_btx]
    add ax, [mru_btw]
    mov cx, [mru_bf+4]
    call mru_afill
    mov cx, [mru_btx]               ; the caption
    mov dx, [mru_bty]
    mov ax, CBLACK | (CLGRAY << 8)
    test di, OS88UI_DIS
    jz .ink
    mov al, CDGRAY
.ink:
    call OSAPI_FONT_RUN
    xor al, al
    call OSAPI_SET_COLOR
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_bvtl - the face's top row in AL and its left column in AH, taken out
; of [mru_bf]
mru_bvtl:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov [mru_ink], al
    mov ax, [mru_bf]
    mov bx, [mru_bf+2]
    mov cx, [mru_bf+4]
    mov dx, bx
    call mru_afill
    pop ax
    mov [mru_ink], ah
    mov ax, [mru_bf]
    mov bx, [mru_bf+2]
    inc bx
    mov cx, ax
    mov dx, [mru_bf+6]
    call mru_afill
    inc word [mru_bf]
    inc word [mru_bf+2]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_bvbr - the face's bottom row and right column in AL, taken out of it
mru_bvbr:
    push ax
    push bx
    push cx
    push dx
    mov [mru_ink], al
    mov ax, [mru_bf]
    mov bx, [mru_bf+6]
    mov cx, [mru_bf+4]
    mov dx, bx
    call mru_afill
    mov ax, cx
    mov bx, [mru_bf+2]
    mov dx, [mru_bf+6]
    dec dx
    call mru_afill
    dec word [mru_bf+4]
    dec word [mru_bf+6]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_afill - AX..DX (SCREEN) in [mru_ink], or nothing when it is empty
mru_afill:
    cmp cx, ax
    jl .r
    cmp dx, bx
    jl .r
    push ax
    mov al, [mru_ink]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.r:
    ret

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
    push si
    call mru_pad
    mov si, mru_s2
    call mru_text
    pop si
    pop ax
    ret

; mru_pad - SI cut or padded with spaces to AX cells (at most MRU_FMAX) into
; mru_s2; AX = the cells, clamped
mru_pad:
    push bx
    push cx
    push si
    push di
    cmp ax, MRU_FMAX
    jbe .w
    mov ax, MRU_FMAX
.w:
    mov cx, ax
    mov di, mru_s2
.c:
    jcxz .end
    mov bl, [si]
    or bl, bl
    jz .pad
    inc si
    mov [di], bl
    inc di
    dec cx
    jmp short .c
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.end:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; THE FIELD CACHE (SPEC.md 105.9.4). Every string the window shows that can
; change - a Now Playing value, a status line, a channel's program - is drawn
; through a SLOT that holds the cells as they are on the glass, and a redraw
; draws only the run between the first cell that differs and the last: one
; font_run, of "4" when 01:13 becomes 01:14, and of nothing at all when the
; value did not move. A slot is OWED (0xFF) after a full paint, so the first
; draw is whole. The pair is [mru_pair]: AL ink, AH paper, font_run's own
; -----------------------------------------------------------------------------
MRU_FMAX    equ 46                  ; the cells a field may take
MRU_FW      equ 48                  ; a slot: the cells drawn, then the cells
MRF_TITLE   equ 0                   ; the playlist's title
MRF_NAME    equ 1                   ; Now Playing: the name bar
MRF_LEN     equ 2                   ; ...Length .. Output (0..5 + MRF_LEN)
MRF_TL      equ 8                   ; ...the times under the bar, left
MRF_TR      equ 9                   ; ...and right
MRF_ST1     equ 10                  ; the Status pane's three lines
MRF_MSG     equ 13                  ; compact's one message line
MRF_PROG    equ 14                  ; the rack: sixteen programs
MRU_NF      equ MRF_PROG + MR_NCH
    MRBUF mru_fc, MRU_NF * MRU_FW
    MRW mru_pair                    ; the pair a field draws in
    MRW mru_fn                      ; mru_field's: the field's cells...
    MRW mru_fi                      ; ...and the first that differs

; mru_fowe - every slot owed (a full paint has white-filled them)
mru_fowe:
    push cx
    push di
    mov di, mru_fc
    mov cx, MRU_NF
.o:
    mov byte [di], 0xFF
    add di, MRU_FW
    loop .o
    pop di
    pop cx
    ret

; mru_field - SI at (CX, DX) in AX cells, through slot BX: only the cells that
; differ from the slot's, in one run
mru_field:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_pad                    ; mru_s2 = the field, AX = its cells
    mov [mru_fn], ax
    mov di, bx                      ; DI = the slot: BX x 48
    shl di, 1
    shl di, 1
    shl di, 1
    shl di, 1
    mov bx, di
    shl di, 1
    add di, bx
    add di, mru_fc
    xor bx, bx                      ; BX = the first cell to draw
    cmp [di], al
    jne .all                        ; owed, or another width: all of it
.f:
    cmp bx, [mru_fn]
    jae .done                       ; not one cell moved
    mov al, [mru_s2+bx]
    cmp al, [bx+di+1]
    jne .fd
    inc bx
    jmp short .f
.fd:
    mov [mru_fi], bx
    mov bx, [mru_fn]
.l:
    dec bx                          ; the last that differs (the first does,
    mov al, [mru_s2+bx]             ; so this stops by it)
    cmp al, [bx+di+1]
    je .l
    jmp short .draw
.all:
    mov word [mru_fi], 0
    mov bx, [mru_fn]
    or bx, bx
    jz .copy
    dec bx
.draw:                              ; cells [mru_fi] .. BX
    mov al, [mru_s2+bx+1]
    push ax
    mov byte [mru_s2+bx+1], 0
    push bx
    mov bx, [mru_fi]
    lea si, [mru_s2+bx]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add cx, bx
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov ax, [mru_pair]
    call OSAPI_FONT_RUN
    pop bx
    pop ax
    mov [mru_s2+bx+1], al
.copy:
    mov bx, [mru_fn]
    mov [di], bl
.cp:
    or bx, bx
    jz .done
    dec bx
    mov al, [mru_s2+bx]
    mov [bx+di+1], al
    jmp short .cp
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_frame - the rect at SI (relative), black; mru_framec in colour AL
mru_frame:
    push ax
    xor al, al
    call mru_framec
    pop ax
    ret
mru_framec:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_SET_COLOR
    call mru_absr
    call OSAPI_GFX_FRAME
    xor al, al
    call OSAPI_SET_COLOR
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

; mru_pane - the pane at SI, framed, its title BX at its top-left. On the
; colour face a pane is a grey frame with a title STRIP: a light grey band
; ruled off under it, the title black on the band
mru_pane:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [mru_col], 0
    jne .col
    call mru_frame
    mov cx, [si]
    add cx, 6
    mov dx, [si+2]
    add dx, 3
    mov si, bx
    call mru_text
    jmp short .out
.col:
    push bx
    mov al, CDGRAY
    call OSAPI_SET_COLOR
    call mru_absr
    call OSAPI_GFX_FRAME            ; the frame...
    add bx, [mru_hdr]
    mov dx, bx
    call OSAPI_GFX_FILL             ; ...the rule under the strip...
    push ax
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    pop ax
    inc ax
    dec cx
    sub bx, [mru_hdr]
    inc bx
    mov dx, bx
    add dx, [mru_hdr]
    sub dx, 2
    call OSAPI_GFX_FILL             ; ...the strip...
    xor al, al
    call OSAPI_SET_COLOR
    pop bx
    mov cx, [si]                    ; ...and the title
    add cx, 6
    mov dx, [si+2]
    add dx, 4
    mov si, bx
    or si, si
    jz .out                         ; (the playlist's is a field: see below)
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov ax, CBLACK | (CLGRAY << 8)
    call OSAPI_FONT_RUN
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    call mru_mode
    call mru_layout
    call mru_origin
    call mru_aim
    call mru_fowe                   ; every field, bar and box owed: the
    mov word [mru_pair], CBLACK | (CWHITE << 8) ; kernel has white-filled them
    mov byte [mru_ckd], 0xFF
    mov byte [mru_ckd+1], 0xFF
    mov byte [mru_ckd+2], 0xFF
    mov si, mru_p1
    mov bx, mru_t_list
    cmp byte [mru_col], 0
    je .pt
    xor bx, bx                      ; colour: the strip alone, the field after
.pt:
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

; mru_mode - the face this paint draws: colour on a 4bpp display (the one
; this window is ON, SPEC.md 39.16.4 - which answers the more restrictive of
; two for a straddle), mono otherwise; the layout takes it back to mono on a
; box too small for it
mru_mode:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [mru_col], 0
    mov bx, [mr_win]
    call OSAPI_WM_DISPLAY           ; DH = bits per pixel
    cmp dh, 1
    jbe .m
    mov byte [mru_col], 1
.m:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mra_load - the colour face's pictures, MIDIRACK.GFX beside us, into a claim
; of their own (SPEC.md 105.9.5). Only on a COLOUR PRIMARY: a Hercules or a
; CGA machine never reads it. Silent when the file is not there, the heap is
; short or the file is not exactly the size the four numbers say - the
; transport is then code-drawn, which is a complete face and not a broken
; one. UI task, at entry (a read and a claim, SPEC.md 20.6 rule 7)
; -----------------------------------------------------------------------------
mra_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call OSAPI_VIDEO                ; DH = the primary's bits per pixel
    cmp dh, 1
    jbe .out
    mov ax, (MRA_SIZE + 1023) / 1024
    call OSAPI_MEM_CLAIM            ; DX = the segment
    jc .out
    push dx
    mov es, dx
    xor bx, bx
    mov cx, ((MRA_SIZE + 1023) / 1024) * 1024
    xor dx, dx
    mov si, mra_name
    call OSAPI_FILE_READ            ; DX:AX = its size (unwrapped, 20.14)
    pop si
    jc .free
    or dx, dx
    jnz .free
    cmp ax, MRA_SIZE
    jne .free
    mov [mra_seg], si
    jmp short .out
.free:
    mov dx, si
    call OSAPI_MEM_FREE
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mra_free - the pictures' claim back (the window is closing)
mra_free:
    push dx
    mov dx, [mra_seg]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
    mov word [mra_seg], 0
.r:
    pop dx
    ret

mra_name:   db 'MIDIRACK.GFX', 0

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
    cmp byte [mru_col], 0
    je .ty
    inc dx                          ; (on the colour face's strip)
.ty:
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
    push bx
    push word [mru_pair]
    cmp byte [mru_col], 0
    je .tp
    mov word [mru_pair], CBLACK | (CLGRAY << 8)
.tp:
    mov bx, MRF_TITLE
    call mru_field
    pop word [mru_pair]
    pop bx
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
    push si
    mov si, mru_lb
    mov al, CBLACK
    cmp byte [mru_col], 0
    je .f
    mov al, CDGRAY
.f:
    call mru_framec
    pop si
    pop ax
    call mru_list_owe
    ; fall through

; mru_list_sync - the rows whose entry, highlight or mark moved since they
; were drawn, and the bar if its three numbers did
mru_list_sync:
    push ax
    push bx
    push cx
    push di
    xor di, di                      ; DI = the row
.r:
    cmp di, [mru_lrows]
    jae .sb
    mov ax, di                      ; AX = what the row shows: the entry,
    add al, [mrl_top]               ; with the highlight (bit 8) and the
    xor ah, ah                      ; loaded mark (bit 9) - or 0xFE, blank
    cmp al, [mrl_n]
    jb .ent
    mov ax, 0x00FE
    jmp short .key
.ent:
    cmp al, [mrl_sel]
    jne .ns
    or ah, 1
.ns:
    cmp byte [mr_loaded], 0
    je .key
    cmp al, [mr_lidx]
    jne .key
    or ah, 2
.key:
    mov bx, di
    shl bx, 1
    cmp ax, [mru_rowk+bx]
    je .nx
    mov [mru_rowk+bx], ax
    call mru_draw_row
.nx:
    inc di
    jmp short .r
.sb:
    mov bx, 8                       ; the bar: total, fit, pos
.sc:
    mov ax, [mru_sb+bx]
    cmp ax, [mru_sbk+bx-8]
    jne .sd
    add bx, 2
    cmp bx, 14
    jb .sc
    jmp short .out
.sd:
    mov ax, [mru_sb+8]
    mov [mru_sbk], ax
    mov ax, [mru_sb+10]
    mov [mru_sbk+2], ax
    mov ax, [mru_sb+12]
    mov [mru_sbk+4], ax
    mov bx, mru_sb
    call os88ui_sbar
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; mru_list_owe - every row and the bar owed (a full paint, or the list's
; entries moved under them: an add, a remove, a clear)
mru_list_owe:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, mru_rowk
    mov cx, MRU_LMAX
    mov ax, 0xFFFF
    cld
    rep stosw
    mov [mru_sbk], ax
    pop es
    pop di
    pop cx
    pop ax
    ret

MRU_LMAX    equ 40                  ; the most rows the list box shows
    MRBUF mru_rowk, MRU_LMAX * 2    ; each row's key as drawn (0xFFFF owed)
    MRBUF mru_sbk, 6                ; the bar's numbers as drawn

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
    ; the row's ground (one fill: the highlight, or white) - black in
    ; mono, the colour face's blue
    mov byte [mru_ink], CWHITE
    mov byte [mru_rowhl], 0
    cmp al, [mrl_n]
    jae .gr
    cmp al, [mrl_sel]
    jne .gr
    mov byte [mru_rowhl], 1
    mov byte [mru_ink], CBLACK
    cmp byte [mru_col], 0
    je .gr
    mov byte [mru_ink], CBLUE
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
    mov ah, CBLACK                  ; ground, ink - inverted on the highlight
    cmp byte [mru_col], 0
    je .ink
    mov ah, CDGRAY                  ; colour: a grey page, a GREEN mark on the
    cmp si, mru_i_cur               ; song that is loaded
    jne .ink
    mov ah, CGREEN
.ink:
    mov al, CWHITE
    cmp byte [mru_rowhl], 0
    je .pen
    mov al, [mru_ink]               ; on the highlight: white on its ground
    mov ah, CWHITE
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
    cmp byte [mru_rowhl], 0
    jne .inv
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
    push ax
    push cx
    push dx
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov al, CWHITE
    mov ah, [mru_ink]
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRB mru_rowi
    MRB mru_rowhl                   ; the row being drawn is the highlight
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
    push word [mru_pair]
    cmp byte [mru_col], 0
    jne .lcd
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
    mov word [mru_pair], CBLACK | (CWHITE << 8)
    jmp short .named
.lcd:                               ; THE DISPLAY: black, a grey rule under
    mov si, mru_lcd                 ; the name, the lines in green
    mov al, CBLACK
    call mru_fillr
    mov al, CDGRAY
    call mru_framec
    mov ax, [mru_lcd]
    add ax, 4
    mov cx, [mru_lcd+4]
    sub cx, 4
    mov bx, [mru_nmy]
    add bx, 8 + 3
    mov dx, bx
    mov byte [mru_ink], CDGRAY
    call mru_fill
    mov word [mru_pair], CGREEN | (CBLACK << 8)
.named:
    call mru_np_name
    ; the labels
    xor di, di
.lb:
    cmp di, [mru_nplines]
    jae .lbd
    mov ax, di
    mul word [mru_nrp]
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
    push ax
    push cx
    push dx
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov ax, [mru_pair]
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    inc di
    jmp short .lb
.lbd:
    pop word [mru_pair]
    mov si, mru_bar                 ; the bar's frame
    mov al, CBLACK
    cmp byte [mru_col], 0
    je .bf
    mov al, CDGRAY
.bf:
    call mru_framec
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

; mru_vpair - [mru_pair] for a Now Playing VALUE: black on white, or the
; display's bright green on black
mru_vpair:
    mov word [mru_pair], CBLACK | (CWHITE << 8)
    cmp byte [mru_col], 0
    je .r
    mov word [mru_pair], CLGREEN | (CBLACK << 8)
.r:
    ret

; mru_np_name - the song's name in the bar, white on black, as a field
mru_np_name:
    push ax
    push bx
    push cx
    push dx
    push si
    push word [mru_pair]
    call mru_name                   ; SI = the song's name
    mov cx, [mru_nmx]
    mov dx, [mru_nmy]
    mov ax, [mru_nmw]
    mov word [mru_pair], CWHITE | (CBLACK << 8)
    cmp byte [mru_col], 0
    je .nn
    mov word [mru_pair], CYELLOW | (CBLACK << 8)
.nn:
    mov bx, MRF_NAME
    call mru_field
    pop word [mru_pair]
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
    mul word [mru_nrp]
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
    cmp byte [mru_col], 0
    je .e
    mov ax, [mru_lcd+4]
.e:
    sub ax, [mru_npx]
    sub ax, MRU_VCOL * 8 + 4
    mov cl, 3
    shr ax, cl
    pop cx
    ret

; mru_np_static - Length, Mode and Output
mru_np_static:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push word [mru_pair]
    call mru_vpair
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
    mov bx, MRF_LEN
    call mru_field
    mov cx, [mru_bar+4]             ; ...and under the bar's right end
    sub cx, 39
    mov dx, [mru_tmy]
    mov ax, 5
    mov bx, MRF_TR
    call mru_field
    mov si, mru_m_norm              ; Mode
    cmp byte [mr_loop], 0
    je .m
    mov si, mru_m_loop
.m:
    mov ax, 4
    call mru_vline
    jc .o
    call mru_vcells
    mov bx, MRF_LEN + 4
    call mru_field
.o:
    call mru_outname                ; Output
    mov ax, 5
    call mru_vline
    call mru_vcells
    mov bx, MRF_LEN + 5
    call mru_field
    pop word [mru_pair]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    push word [mru_pair]
    call mru_vpair
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
    mov bx, MRF_LEN + 1
    call mru_field
    mov cx, [mru_bar]               ; and under the bar's left end
    mov dx, [mru_tmy]
    mov ax, 5
    mov bx, MRF_TL
    call mru_field
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
    mov bx, MRF_LEN + 2
    call mru_field
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
    mov bx, MRF_LEN + 3
    call mru_field
.notempo:
    pop word [mru_pair]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mru_sl:     db ' / ', 0

; mru_bar0 / mru_bar1 - [mru_ink] for the progress bar's unlit and lit parts:
; white and black, or black and the display's green
mru_bar0:
    mov byte [mru_ink], CWHITE
    cmp byte [mru_col], 0
    je .r
    mov byte [mru_ink], CBLACK
.r:
    ret
mru_bar1:
    mov byte [mru_ink], CBLACK
    cmp byte [mru_col], 0
    je .r
    mov byte [mru_ink], CLGREEN
.r:
    ret
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
    call mru_bar0
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
    jb .shrink
    call mru_bar1                   ; growing: lit from the old end
    jmp short .go
.shrink:
    call mru_bar0                   ; shrinking (a new song): unlit back
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
    cmp byte [mru_col], 0
    jne .hc
    call mru_textn
    jmp short .rows
.hc:
    push cx                         ; colour: three headings in grey, each
    push dx                         ; over its own column
    add cx, [mru_ox]
    add dx, [mru_oy]
    mov ax, CDGRAY | (CWHITE << 8)
    mov si, mru_rkc1
    call OSAPI_FONT_RUN
    push cx
    add cx, [mru_rbx]
    mov si, mru_rkc2
    call OSAPI_FONT_RUN
    pop cx
    add cx, [mru_rpx]
    mov si, mru_rkc3
    call OSAPI_FONT_RUN
    pop dx
    pop cx
.rows:
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

mru_rkh:    db 'Ch  Lev   Program / Instrument', 0
mru_rkh2:   db 'Ch Lev  Program', 0
mru_rkc1:   db 'Ch', 0
mru_rkc2:   db 'Lev', 0
mru_rkc3:   db 'Program / Instrument', 0
MRU_BARW    equ 32                  ; a level bar's width
MRU_BARW2   equ 20                  ; ...in two columns
MRU_CBARW   equ 28                  ; ...and on the colour face

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
    mul word [mru_rkp]
    add ax, [mru_rky]
    mov dx, ax
    pop bx
    pop ax
    ret

; the rack row's geometry and colours, the layout's (mono / colour):
    MRW mru_rkp                     ; the rows' pitch
    MRW mru_rbx                     ; the bar's x in the row
    MRW mru_rbw                     ; ...its width, frame and all
    MRW mru_rpx                     ; the program's x in the row
    MRW mru_rz1                     ; the bar's zones: lit pixels below this
    MRW mru_rz2                     ; are the first colour, below this the
    MRB mru_rc1                     ; second, and the rest the third
    MRB mru_rc2
    MRB mru_rc3
    MRB mru_rc0                     ; ...and the unlit part
    MRB mru_rcf                     ; the frame

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
    push bx
    mov ax, cx                      ; the bar's frame
    add ax, [mru_rbx]
    mov bx, dx
    mov cx, ax
    add cx, [mru_rbw]
    dec cx
    add dx, 6
    add ax, [mru_ox]
    add bx, [mru_oy]
    add cx, [mru_ox]
    add dx, [mru_oy]
    push ax
    mov al, [mru_rcf]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    xor al, al
    call OSAPI_SET_COLOR
    pop bx
    call mru_rack_bar               ; the fill, from nothing
    call mru_rack_prog
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mru_rack_bar - channel BX's bar to its level: the CHANGE alone - growing,
; the lit run from the old end to the new in each zone it crosses (one fill
; in mono, where every zone is black); shrinking, one fill of the unlit
; colour back
mru_rack_bar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_rkpos
    add cx, [mru_rbx]               ; CX = the bar's inside, DX its top row
    inc cx
    inc dx
    mov si, [mru_rbw]
    sub si, 2                       ; SI = the inside's width
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
    xor ah, ah                      ; a full paint: the unlit part first,
    push ax                         ; then grow from nothing
    mov di, si
    call mru_rbfill0                ; [AL, SI) unlit
    pop ax
.delta:
    cmp al, ah
    je .out
    ja .grow
    xchg al, ah                     ; shrinking: [new, old) unlit
    mov di, ax
    and di, 0x00FF
    mov al, ah
    call mru_rbfill0
    jmp short .out
.grow:                              ; [AH, AL) lit, zone by zone
    mov di, ax
    and di, 0x00FF                  ; DI = the new end
    mov al, ah
    xor ah, ah                      ; AX = the old end
    mov bl, [mru_rc1]
    mov si, [mru_rz1]
    call mru_rbzone
    mov bl, [mru_rc2]
    mov si, [mru_rz2]
    call mru_rbzone
    mov bl, [mru_rc3]
    mov si, di
    call mru_rbzone
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_rbzone - the lit run from AX to min(DI, SI) in colour BL, AX advanced
; to its end (CX, DX: the bar's inside)
mru_rbzone:
    cmp si, di
    jbe .e
    mov si, di
.e:
    cmp ax, si
    jae .r
    push bx
    push cx
    push dx
    mov [mru_ink], bl
    mov bx, dx                      ; [AX, SI) at the bar's rows
    add dx, 4
    add ax, cx
    add cx, si
    dec cx
    call mru_fill
    pop dx
    pop cx
    pop bx
    mov ax, si
.r:
    ret

; mru_rbfill0 - the unlit run [AL, DI) (CX, DX: the bar's inside)
mru_rbfill0:
    push ax
    push bx
    push cx
    push dx
    xor ah, ah
    cmp ax, di
    jae .r
    mov bl, [mru_rc0]
    mov [mru_ink], bl
    mov bx, dx
    add dx, 4
    add ax, cx
    add cx, di
    dec cx
    call mru_fill
.r:
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
    add cx, [mru_rpx]
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
    push bx
    add bx, MRF_PROG
    call mru_field
    pop bx
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
    sub ax, 176
    add ax, [mru_ox]
    mov bx, [mru_p4+2]
    add bx, [mru_hdr]
    add bx, [mru_oy]
    mov cx, ax
    mov dx, [mru_p4+6]
    sub dx, 4
    add dx, [mru_oy]
    push ax
    xor al, al
    cmp byte [mru_col], 0
    je .dv
    mov al, CDGRAY
    inc bx                          ; (clear of the strip's rule)
.dv:
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    xor al, al
    call OSAPI_SET_COLOR
    call mru_ck_sync
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_ck_sync - the three check boxes whose state moved since they were
; drawn (os88ui_chkhit redraws the one it toggles, and mr_onclick tells
; mru_ckd so)
mru_ck_sync:
    push ax
    push bx
    push di
    cmp byte [mru_cmp], 0
    jne .out                        ; compact has none
    mov al, [mru_ck1+OS88UI_CK_ON]
    cmp al, [mru_ckd]
    je .k2
    mov [mru_ckd], al
    xor di, di
    mov bx, mru_ck1
    call os88ui_chk
.k2:
    mov al, [mru_ck2+OS88UI_CK_ON]
    cmp al, [mru_ckd+1]
    je .k3
    mov [mru_ckd+1], al
    xor di, di
    mov bx, mru_ck2
    call os88ui_chk
.k3:
    xor di, di                      ; Play in Background means something only
    mov al, [mru_ck3+OS88UI_CK_ON]  ; where the speaker is the output: bit 7
    call mr_isspk                   ; of what was drawn is its greying
    jc .bg
    mov di, OS88UI_DIS
    or al, 0x80
.bg:
    cmp al, [mru_ckd+2]
    je .out
    mov [mru_ckd+2], al
    mov bx, mru_ck3
    call os88ui_chk
.out:
    pop di
    pop bx
    pop ax
    ret

    MRBUF mru_ckd, 3                ; the boxes as drawn (0xFF owed)

; mru_st_lines - the Status pane's three lines
mru_st_lines:
    push ax
    push cx
    push dx
    push si
    call mru_mkdev
    call mru_mkfile
    push bx
    mov cx, [mru_stx]
    mov dx, [mru_sty]
    mov ax, [mru_stw]
    mov si, mru_dev
    mov bx, MRF_ST1
    call mru_field
    add dx, [mru_rp]
    mov si, mru_file
    inc bx
    call mru_field
    add dx, [mru_rp]
    mov si, [mr_msg]
    inc bx
    call mru_field
    pop bx
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
    push bx
    mov bx, MRF_MSG
    call mru_field
    pop bx
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
    mov ax, [mrb_rate]              ; the open stream's, or what a Play would
    cmp byte [mr_out], MRO_SB       ; ask for now
    je .sr
    call mrb_rate_for
.sr:
    call mru_decc
    mov si, mru_d_hz
    call mru_cat
    jmp short .out
.n2:
    mov si, mru_d_tone
    cmp al, MRO_TONE
    je .sp
    mov si, mru_d_spk
    call mru_cat
    mov ax, [mrk_rate]
    cmp byte [mr_out], MRO_SPK
    je .kr
    call mrk_rate_for
    call mrk_real
.kr:
    call mru_decc
    mov si, mru_d_hz
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
mru_d_spk:  db 'PC speaker - pulse-width synth at ', 0
mru_d_tone: db 'PC speaker - melody line, in the background', 0

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
; mru_draw_buttons - every live button of the group (a full paint)
; mru_btn_sync - ...or only those whose flags moved since they were drawn:
; Pause greys one button and ungreys none, so it draws ONE, not fourteen
; -----------------------------------------------------------------------------
mru_draw_buttons:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, mru_bdr
    mov cx, MRU_NB
    mov ax, 0xFFFF                  ; every one owed
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ; fall through
mru_btn_sync:
    push ax
    push bx
    push cx
    push si
    push word [mru_btrec+OS88UI_BT_RECTS]   ; EVERY button the layout has,
    push word [mru_btrec+OS88UI_BT_LABELS]  ; whatever the record is live
    push word [mru_btrec+OS88UI_BT_FLAGS]   ; on: under a card it is none or
    push word [mru_btrec+OS88UI_BT_N]       ; the dialog's pair, and the row
    call mru_recmain                ; would go unpainted. Put back after - only
    mov cx, [mru_btrec+OS88UI_BT_N] ; the UI task draws buttons, and nothing
    xor ax, ax                      ; presses in between
    xor si, si
.b:
    cmp ax, cx
    jae .out
    inc ax
    mov bx, [mru_bflg+si]
    cmp bx, [mru_bdr+si]
    je .nx
    mov [mru_bdr+si], bx
    mov bx, mru_btrec
    call os88ui_btn
.nx:
    add si, 2
    jmp short .b
.out:
    pop word [mru_btrec+OS88UI_BT_N]
    pop word [mru_btrec+OS88UI_BT_FLAGS]
    pop word [mru_btrec+OS88UI_BT_LABELS]
    pop word [mru_btrec+OS88UI_BT_RECTS]
    pop si
    pop cx
    pop bx
    pop ax
    ret

    MRBUF mru_bdr, MRU_NB * 2       ; each button's flags as drawn

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
    mov word [mru_pair], CBLACK | (CWHITE << 8)
    call mru_np_dyn
    call mru_rack_sync
.out:
    pop bx
    pop ax
    ret

; mru_rack_sync - the rack's rows whose bar or program moved (mrc_dirty)
mru_rack_sync:
    push ax
    push bx
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

; =============================================================================
; mru_update - WHAT A COMMAND CHANGED, AND NOTHING ELSE (SPEC.md 105.9.4).
; Every part of the window is walked and every part is drawn only through its
; cache - the field slots, the list's row keys and bar, the check boxes and
; the buttons' flags - so a Pause draws one button and one status run, and a
; Next draws two rows, the name, the values that moved and the programs that
; changed. A full paint is the kernel's W_PAINT and a card's, nobody else's.
; Under a card it draws nothing: the card is on top, and its going down is a
; repaint
; =============================================================================
mru_update:
    push ax
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_layout
    call mru_origin
    call mru_aim
    cmp byte [mr_card], 0
    jne .cl
    mov word [mru_pair], CBLACK | (CWHITE << 8)
    call mru_list_title
    call mru_list_sync
    call mru_np_name
    call mru_np_static
    mov word [mru_lsec], 0xFFFF     ; (formatted again; the slots decide what
    mov word [mru_lmeas], 0xFFFF    ; is drawn)
    mov word [mru_ltempo], 0xFFFF
    call mru_np_dyn
    call mru_rack_sync
    cmp byte [mru_cmp], 0
    jne .msg
    call mru_st_lines
    call mru_ck_sync
    jmp short .bt
.msg:
    call mru_draw_msg
.bt:
    call mru_btn_sync
.cl:
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    pop ax
    ret

; mru_card_up - card AL goes up: the card alone, over what is there. The
; content under it is unchanged and stays drawn; the record goes to the
; card's controls (mru_aim). A card already up is under it and would show
; round the edges of a smaller one, so that case is a repaint
mru_card_up:
    cmp byte [mr_card], 0
    je .first
    mov [mr_card], al
    jmp mru_repaint
.first:
    mov [mr_card], al
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_layout
    call mru_origin
    call mru_aim
    call mru_draw_card
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    ret

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
mr_ab5:     db 'FM bank and song arrangements: CC0', 0

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

; =============================================================================
; THE SETTINGS DIALOG (SPEC.md 105.9.2): the output, the synth's sample rate
; and Play in Background, edited as COPIES (mrs_*) and committed only by OK -
; Cancel, Esc or a second Settings leave the player exactly as it was. While
; it is up the window's one button record points at the dialog's own pair
; (mru_aim), the paged-window device os88ui.inc's record was built for, so a
; press on a rack button under the card cannot reach the rack.
; =============================================================================
    MRBUF mru_sc, 8                 ; the dialog's rect (screen)
    MRBUF mru_rad, 18               ; the output group
    MRBUF mru_rad2, 18              ; the rate group
    MRBUF mru_ck4, 12               ; Play in Background
    MRBUF mru_crect, 16             ; OK and Cancel (screen)
    MRBUF mru_cflg, 4
    MRB mrs_out                     ; the dialog's copies
    MRB mrs_rate
    MRB mrs_bg
    MRW mru_scp                     ; its row pitch
    MRW mru_sch                     ; ...and its height
    MRW mru_shy                     ; the groups' heading row
    MRW mru_scky                    ; the check box's row
mru_clabs:  dw mru_l_ok, mru_l_cancel
mru_l_ok:     db 'OK', 0
mru_l_cancel: db 'Cancel', 0
mru_radit:  dw mru_r0, mru_r1, mru_r2, mru_r3, mru_r4
mru_r0:     db 'Automatic', 0
mru_r1:     db 'OPL3 FM, 17 voices', 0
mru_r2:     db 'OPL2 FM (AdLib), 8', 0
mru_r3:     db 'Sound Blaster synth', 0
mru_r4:     db 'PC speaker synth', 0
mru_ratit:  dw mru_q0, mru_q1, mru_q2, mru_q3, mru_q4, mru_q5, mru_q6
mru_q0:     db 'Automatic', 0
mru_q1:     db '8,000 Hz', 0
mru_q2:     db '11,025 Hz', 0
mru_q3:     db '16,000 Hz', 0
mru_q4:     db '22,050 Hz', 0
mru_q5:     db '32,000 Hz', 0
mru_q6:     db '44,100 Hz', 0
mru_sct:    db 'Settings', 0
mru_sho:    db 'Output device', 0
mru_shr:    db 'Synth sample rate', 0
mru_l_bgd:  db 'Speaker: play the melody in the background', 0
mru_snote:  db 'Above 22,050 Hz needs an SB Pro or SB16. The speaker', 0
mru_snote2: db 'reaches 24.8 kHz (8 kHz on an 8088); FM has no rate.', 0
mru_sfnd:   db 'Found: ', 0
mru_f_opl2: db 'OPL2  ', 0
mru_f_opl3: db 'OPL3  ', 0
mru_f_dsp:  db 'SB DSP  ', 0
mru_f_hi:   db '(to 44.1 kHz)  ', 0
mru_f_spk:  db 'PC speaker', 0
MRU_SCW     equ 520

; mru_screct - the dialog's rect and every record in it, from the layout
mru_screct:
    push ax
    push bx
    push cx
    push dx
    mov word [mru_scp], 16
    cmp byte [mru_cmp], 0
    je .p
    mov word [mru_scp], 12          ; compact: the rows close up
.p:
    mov ax, [mru_scp]               ; the height: the rate group's seven rows
    mov cx, 7                       ; and the rows around it
    mul cx
    add ax, 6 + 11 + 12 + 2 + 14 + 6
    cmp byte [mru_cmp], 0
    jne .h                          ; compact: OK and Cancel ride beside the
    add ax, 17 + 22 + 6             ; rate group (below); tall: the Found
    add ax, [mru_bh]                ; line, the note and their own row
.h:
    mov [mru_sch], ax
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
    sub ax, [mru_sch]
    jns .y
    xor ax, ax
.y:
    shr ax, 1
    add ax, [mru_oy]
    mov [mru_sc+2], ax
    add ax, [mru_sch]
    dec ax
    mov [mru_sc+6], ax
    mov ax, [mru_sc+2]              ; the groups' headings
    add ax, 6 + 11
    cmp byte [mru_cmp], 0
    jne .hy
    add ax, 17                      ; (under the Found line, with air)
.hy:
    mov [mru_shy], ax
    add ax, 12                      ; the two groups
    mov dx, ax
    mov ax, [mru_sc]
    add ax, 12
    mov [mru_rad+OS88UI_RD_RECT], ax
    mov [mru_rad+OS88UI_RD_RECT+2], dx
    mov ax, [mru_sc]
    add ax, MRU_SCW / 2 - 8
    mov [mru_rad+OS88UI_RD_RECT+4], ax
    mov ax, [mru_scp]
    mov cx, 5
    push dx
    mul cx
    pop dx
    add ax, dx
    dec ax
    mov [mru_rad+OS88UI_RD_RECT+6], ax
    mov ax, [mru_sc]
    add ax, MRU_SCW / 2 + 4
    mov [mru_rad2+OS88UI_RD_RECT], ax
    mov [mru_rad2+OS88UI_RD_RECT+2], dx
    mov ax, [mru_sc+4]
    sub ax, 10
    mov [mru_rad2+OS88UI_RD_RECT+4], ax
    mov ax, [mru_scp]
    mov cx, 7
    push dx
    mul cx
    pop dx
    add ax, dx
    mov cx, ax                      ; CX = under the groups
    dec ax
    mov [mru_rad2+OS88UI_RD_RECT+6], ax
    add cx, 2
    mov [mru_scky], cx              ; the check box
    mov ax, [mru_sc]
    add ax, 12
    mov [mru_ck4+OS88UI_CK_RECT], ax
    mov [mru_ck4+OS88UI_CK_RECT+2], cx
    add ax, 360
    mov [mru_ck4+OS88UI_CK_RECT+4], ax
    mov ax, cx
    add ax, 11
    mov [mru_ck4+OS88UI_CK_RECT+6], ax
    mov word [mru_ck4+OS88UI_CK_LABEL], mru_l_bgd
    mov al, [mrs_bg]
    mov [mru_ck4+OS88UI_CK_ON], al
    mov ax, [mru_sc+6]              ; OK and Cancel, bottom right
    sub ax, 5
    mov dx, ax                      ; DX = y2
    sub ax, [mru_bh]
    inc ax
    mov bx, ax                      ; BX = y1
    mov cx, [mru_sc+4]
    sub cx, 10
    mov [mru_crect+12], cx          ; Cancel
    mov [mru_crect+14], dx
    mov [mru_crect+10], bx
    sub cx, 75
    mov [mru_crect+8], cx
    sub cx, 8                       ; OK, to its left
    mov [mru_crect+4], cx
    mov [mru_crect+6], dx
    mov [mru_crect+2], bx
    sub cx, 75
    mov [mru_crect], cx
    cmp byte [mru_cmp], 0           ; COMPACT (a CGA): the row is in the space
    je .cdone                       ; the five-row output group leaves under
    mov bx, [mru_rad+OS88UI_RD_RECT+6]  ; it beside the seven-row rate group,
    add bx, 5                       ; which is what makes the dialog fit 200
    mov dx, bx                      ; lines
    add dx, [mru_bh]
    dec dx
    mov cx, [mru_sc]
    add cx, 12
    mov [mru_crect], cx
    mov [mru_crect+2], bx
    add cx, 74
    mov [mru_crect+4], cx
    mov [mru_crect+6], dx
    add cx, 8
    mov [mru_crect+8], cx
    mov [mru_crect+10], bx
    add cx, 74
    mov [mru_crect+12], cx
    mov [mru_crect+14], dx
.cdone:
    mov word [mru_cflg], OS88UI_FILL | OS88UI_DEF   ; Enter is OK
    mov word [mru_cflg+2], OS88UI_FILL
    call mru_radset
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_radset - the two groups' records, from the dialog's copies
mru_radset:
    push ax
    mov word [mru_rad+OS88UI_RD_ITEMS], mru_radit
    mov word [mru_rad+OS88UI_RD_N], 5
    mov al, [mrs_out]
    xor ah, ah
    mov [mru_rad+OS88UI_RD_SEL], ax
    mov ax, [mru_scp]
    mov [mru_rad+OS88UI_RD_PITCH], ax
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
    mov word [mru_rad2+OS88UI_RD_ITEMS], mru_ratit
    mov word [mru_rad2+OS88UI_RD_N], MR_NRATE
    mov al, [mrs_rate]
    xor ah, ah
    mov [mru_rad2+OS88UI_RD_SEL], ax
    mov ax, [mru_scp]
    mov [mru_rad2+OS88UI_RD_PITCH], ax
    xor ax, ax
    cmp byte [mr_sbhi], 0           ; past 22,050 Hz: a DSP that can, or
    jne .hi                         ; nothing - the speaker stops at 24.8k
    mov al, 0x60
.hi:
    mov [mru_rad2+OS88UI_RD_DIS], ax
    pop ax
    ret

; mru_setcard - the dialog, drawn whole (last, over the content)
mru_setcard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_screct
    mov al, CWHITE                  ; the ground, then a double frame: a
    call OSAPI_SET_COLOR            ; dialog, not a pane
    mov ax, [mru_sc]
    mov bx, [mru_sc+2]
    mov cx, [mru_sc+4]
    mov dx, [mru_sc+6]
    call OSAPI_GFX_FILL
    xor al, al
    call OSAPI_SET_COLOR
    call OSAPI_GFX_FRAME
    add ax, 2
    add bx, 2
    sub cx, 2
    sub dx, 2
    call OSAPI_GFX_FRAME
    mov cx, [mru_sc]
    add cx, 10
    mov dx, [mru_sc+2]
    add dx, 7
    mov si, mru_sct
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    cmp byte [mru_cmp], 0
    jne .nofound
    call mru_found                  ; "Found: OPL2  SB DSP  PC speaker"
    add dx, 12
    mov si, mru_s
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
.nofound:
    mov dx, [mru_shy]
    mov si, mru_sho
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    mov cx, [mru_rad2+OS88UI_RD_RECT]
    mov si, mru_shr
    call OSAPI_FONT_RUN
    xor di, di
    mov bx, mru_rad
    call os88ui_rad
    mov bx, mru_rad2
    call os88ui_rad
    mov bx, mru_ck4
    call os88ui_chk
    cmp byte [mru_cmp], 0
    jne .nonote
    mov cx, [mru_sc]
    add cx, 12
    mov dx, [mru_scky]
    add dx, 16
    mov si, mru_snote
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    add dx, 10
    mov si, mru_snote2
    call OSAPI_FONT_RUN
.nonote:
    mov bx, mru_btrec               ; OK and Cancel: the record is the
    mov al, 1                       ; dialog's while it is up (mru_aim)
    call os88ui_btn
    mov al, 2
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mru_found - "Found: OPL3  SB DSP (to 44.1 kHz)  PC speaker" into mru_s
mru_found:
    push si
    push di
    mov di, mru_s
    mov si, mru_sfnd
    call mru_cat
    mov si, mru_f_opl2
    cmp byte [mr_fmkind], SND_OPL2
    je .fm
    mov si, mru_f_opl3
    cmp byte [mr_fmkind], SND_OPL3
    jne .nofm
.fm:
    call mru_cat
.nofm:
    cmp byte [mr_hassb], 0
    je .nosb
    mov si, mru_f_dsp
    call mru_cat
    cmp byte [mr_sbhi], 0
    je .nosb
    mov si, mru_f_hi
    call mru_cat
.nosb:
    mov si, mru_f_spk
    call mru_cat
    pop di
    pop si
    ret
