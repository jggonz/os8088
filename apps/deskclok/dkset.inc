; =============================================================================
; apps/deskclok/dkset.inc - DESK CLOCK SETTINGS (SPEC.md 110.14.6): a titled
; second window, unbound (no dock tile), opened by the gear, a double click on
; the clock, Enter, or Clock > Settings.
;
;   [x] Clock face only            (Hide date, alarms and desktop label.)
;   [ ] Seconds   [x] 24-hour      Size: [Medium   v]
;   Choose a face                  three rows of three previews
;   Selected: Red LED              [Cancel] [Apply] [ OK ]
;
; A change PREVIEWS on the desk at once - the payload itself is edited, and
; the clock laid out again (dk_relay) - while dk_sav keeps what was last
; applied. Apply makes the preview the applied state and owes the save; OK
; is Apply and close; Cancel, Esc and the close box put dk_sav back. The
; clock's tick holds any save while this window is up (dk_tick's CF).
;
; Every pixel of the content is composed (dkdraw.inc) and blitted a slab at a
; time - the previews ARE the faces, drawn by dk_face at the tile's size -
; and a change redraws its control alone: a box, two tiles and the status
; line, or the gallery when the seconds or the 12/24 hours change what every
; preview shows.
; =============================================================================

DKST_END    equ 0
DKST_CHK    equ 1               ; param = the flag; ptr = the label
DKST_CHKB   equ 2               ; ...its label bold
DKST_TEXT   equ 3               ; ptr = the text
DKST_RULE   equ 4
DKST_DROP   equ 5               ; the size
DKST_TILE   equ 6               ; param = the face
DKST_STAT   equ 7               ; `Selected: ...`
DKST_BTN    equ 8               ; param = DKB_*; ptr = the label
DKS_ESZ     equ 12              ; an entry: db type, param; dw x, y, w, h, ptr
DKB_CANCEL  equ 0
DKB_APPLY   equ 1
DKB_OK      equ 2
DKSG_CH     equ 0               ; a layout: dw the content's height,
DKSG_PWA    equ 2               ; db an analog preview's width,
DKSG_PWD    equ 3               ; db a digital one's,
DKSG_E      equ 4               ; then its entries

%macro DKSE 7
    db %1, %2
    dw %3, %4, %5, %6, %7
%endmacro
%macro DKS_TROW 3                ; y, a tile's height, the row
    DKSE DKST_TILE, %3 * 3 + 0, 8, %1, 112, %2, 0
    DKSE DKST_TILE, %3 * 3 + 1, 128, %1, 112, %2, 0
    DKSE DKST_TILE, %3 * 3 + 2, 248, %1, 112, %2, 0
%endmacro
%macro DKS_TILES 3              ; y0, the row step, a tile's height
    DKS_TROW %1, %3, 0
    DKS_TROW %1 + %2, %3, 1
    DKS_TROW %1 + 2 * %2, %3, 2
%endmacro

dk_sgeos:   dw dks_tall, dks_mid, dks_comp
dks_tall:
    dw 386
    db 64, 96
    DKSE DKST_CHKB, DKF_ONLY, 8, 6, 200, 12, dkss_only
    DKSE DKST_TEXT, 0, 26, 22, 0, 0, dkss_sub
    DKSE DKST_CHK, DKF_SECS, 8, 40, 100, 12, dkss_secs
    DKSE DKST_CHK, DKF_H24, 120, 40, 100, 12, dkss_h24
    DKSE DKST_TEXT, 0, 228, 42, 0, 0, dkss_size
    DKSE DKST_DROP, 0, 276, 38, 84, 16, 0
    DKSE DKST_RULE, 0, 8, 60, 352, 1, 0
    DKSE DKST_TEXT, 0, 8, 66, 0, 0, dkss_choose
    DKS_TILES 80, 92, 88
    DKSE DKST_RULE, 0, 8, 358, 352, 1, 0
    DKSE DKST_STAT, 0, 8, 369, 176, 8, 0
    DKSE DKST_BTN, DKB_CANCEL, 192, 364, 52, 18, dkss_cancel
    DKSE DKST_BTN, DKB_APPLY, 250, 364, 52, 18, dkss_apply
    DKSE DKST_BTN, DKB_OK, 308, 364, 52, 18, dkss_ok
    db DKST_END
dks_mid:
    dw 274
    db 56, 96
    DKSE DKST_CHKB, DKF_ONLY, 8, 4, 200, 12, dkss_only
    DKSE DKST_TEXT, 0, 26, 18, 0, 0, dkss_sub
    DKSE DKST_CHK, DKF_SECS, 8, 32, 100, 12, dkss_secs
    DKSE DKST_CHK, DKF_H24, 120, 32, 100, 12, dkss_h24
    DKSE DKST_TEXT, 0, 228, 34, 0, 0, dkss_size
    DKSE DKST_DROP, 0, 276, 30, 84, 14, 0
    DKSE DKST_RULE, 0, 8, 48, 352, 1, 0
    DKSE DKST_TEXT, 0, 8, 52, 0, 0, dkss_choose
    DKS_TILES 64, 62, 60
    DKSE DKST_RULE, 0, 8, 251, 352, 1, 0
    DKSE DKST_STAT, 0, 8, 260, 176, 8, 0
    DKSE DKST_BTN, DKB_CANCEL, 192, 256, 52, 16, dkss_cancel
    DKSE DKST_BTN, DKB_APPLY, 250, 256, 52, 16, dkss_apply
    DKSE DKST_BTN, DKB_OK, 308, 256, 52, 16, dkss_ok
    db DKST_END
dks_comp:
    dw 137
    db 36, 56
    DKSE DKST_CHKB, DKF_ONLY, 8, 2, 140, 12, dkss_only
    DKSE DKST_CHK, DKF_SECS, 160, 2, 90, 12, dkss_secs
    DKSE DKST_CHK, DKF_H24, 264, 2, 90, 12, dkss_h24
    DKSE DKST_TEXT, 0, 8, 17, 0, 0, dkss_size
    DKSE DKST_DROP, 0, 56, 14, 84, 12, 0
    DKSE DKST_STAT, 0, 160, 17, 200, 8, 0
    DKS_TILES 29, 31, 30
    DKSE DKST_BTN, DKB_CANCEL, 152, 124, 64, 12, dkss_cancel
    DKSE DKST_BTN, DKB_APPLY, 224, 124, 64, 12, dkss_apply
    DKSE DKST_BTN, DKB_OK, 296, 124, 64, 12, dkss_ok
    db DKST_END

dkss_only:   db 'Clock face only', 0
dkss_sub:    db 'Hide date, alarms and desktop label.', 0
dkss_secs:   db 'Seconds', 0
dkss_h24:    db '24-hour', 0
dkss_size:   db 'Size:', 0
dkss_choose: db 'Choose a face', 0
dkss_sel:    db 'Selected: ', 0
dkss_cancel: db 'Cancel', 0
dkss_apply:  db 'Apply', 0
dkss_ok:     db 'OK', 0
dkss_title:  db 'Desk Clock Settings', 0
dkss_sizes:  dw dkss_s0, dkss_s1, dkss_s2
dkss_s0:     db 'Small', 0
dkss_s1:     db 'Medium', 0
dkss_s2:     db 'Large', 0
DKS_LROW    equ 12              ; the size list's row

dk_tick12:  db 8                 ; inside a 12-px box, from its third row
            dw 00C0h, 01C0h, 0380h, 3700h, 3E00h, 1C00h, 0800h, 0000h

dk_stpl:    dw 0, 0, DK_SETW + 2, 0
            dw dkss_title, dk_spaint, dk_skey, dk_sclick

; -----------------------------------------------------------------------------
; dk_setopen - the settings window: in front if it is up, else made (the lock
; held: a callback's). Keeps everything
; -----------------------------------------------------------------------------
dk_setopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov bx, [dk_swin]
    or bx, bx
    jz .make
    call OSAPI_WM_FRONT
    jmp .x
.make:
    call dk_sapplied            ; what Cancel goes back to
    mov byte [dk_sdrop], 0
    mov byte [dk_sbtn], 0
    mov bl, [stik_li]
    xor bh, bh
    shl bx, 1
    mov si, [dk_sgeos + bx]
    mov [dk_sgeo], si
    mov ax, [si + DKSG_CH]
    add ax, TITLE_H + 1
    mov [dk_stpl + WT_H], ax
    mov cx, DK_SETW + 2         ; beside the clock, so its preview shows
    call dk_beside
    mov [dk_stpl + WT_X], ax
    mov word [dk_stpl + WT_Y], MBAR_H + 4
    mov si, dk_stpl
    call OSAPI_WM_CREATE
    jc .x                       ; the window table is full: nothing opens
    mov [dk_swin], bx
    mov al, 1
    call OSAPI_WM_OWNBG         ; every pixel is ours
    mov al, 1
    call OSAPI_WM_KEEPH
    call OSAPI_WM_NOANIM        ; no zoom: the paint is the opening
    mov ax, dk_sclose
    call OSAPI_WM_ONCLOSE
    mov ax, dk_smup
    call OSAPI_WM_ONMOUSEUP
    mov si, dk_menus
    call OSAPI_MENU_SET
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_beside - CX = a window's width: AX = its x, the side of the clock with
; the more room - wholly clear of it where the screen allows (W_X = 7 mod 8,
; the content on the byte grid). Keeps the rest
dk_beside:
    push bx
    push dx
    push si
    push es
    push cx
    call OSAPI_VIDEO            ; AX = the screen's width
    pop cx
    mov dx, ax
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [stik_win]
    mov bx, [es:si + W_X]       ; BX = the room left of the clock
    mov ax, dx
    sub ax, bx
    sub ax, [es:si + W_W]       ; AX = the room right of it
    cmp ax, bx
    jg .right
    mov ax, bx                  ; LEFT: its right edge at the clock's left,
    sub ax, cx                  ; or the screen's edge
    jmp short .snap
.right:
    mov ax, [es:si + W_X]
    add ax, [es:si + W_W]
    add ax, 8                   ; clear of it once snapped down
    mov bx, dx
    sub bx, cx
    cmp ax, bx
    jle .snap
    mov ax, bx                  ; ...no further than the screen's right
.snap:
    or ax, ax
    jns .p
    xor ax, ax
.p:
    and ax, 0FFF8h
    dec ax
    jns .x
    mov ax, 7
.x:
    pop es
    pop si
    pop dx
    pop bx
    ret

; dk_sapplied / dk_srestore - the payload to dk_sav, and back. Keep all
dk_sapplied:
    push si
    push di
    mov si, DKP
    mov di, dk_sav
    jmp short dk_scopy
dk_srestore:
    push si
    push di
    mov si, dk_sav
    mov di, DKP
dk_scopy:
    push cx
    push es
    push ds
    pop es
    mov cx, DKP_LEN
    cld
    rep movsb
    pop es
    pop cx
    pop di
    pop si
    ret

; dk_sshut - the window down; the menus. Keeps everything
dk_sshut:
    push bx
    mov bx, [dk_swin]
    or bx, bx
    jz .x
    mov word [dk_swin], 0
    call OSAPI_WM_DESTROY
    call dk_refocus
.x:
    call dk_menulive
    pop bx
    ret

; dk_scancel - the preview withdrawn: what was applied, on the desk again
dk_scancel:
    call dk_srestore
    call dk_relay
    jmp dk_sshut

; dk_sapply - the preview is the clock now: kept, and saved
dk_sapply:
    call dk_sapplied
    jmp stik_touch

; dk_sclose - W_ONCLOSE: the close box is Cancel; the window is gone by our
; own hand, so CF = 1 (os88ui_aclose's answer)
dk_sclose:
    call dk_scancel
    stc
    ret

; dk_spreview - the payload changed in the window: on the desk at once
dk_spreview:
    call dk_relay
    jmp dk_menulive

; -----------------------------------------------------------------------------
; the window's geometry: its content's origin, the rows and columns
; -----------------------------------------------------------------------------
; dk_sorg - [dk_sox], [dk_soy] = the content's origin. ES not assumed
dk_sorg:
    push ax
    push bx
    push dx
    mov bx, [dk_swin]
    call OSAPI_WM_CONTENT
    mov [dk_sox], ax
    mov [dk_soy], dx
    pop dx
    pop bx
    pop ax
    ret

; dk_spaint - W_PAINT (SI = the window, the lock held, the clip armed)
dk_spaint:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_DAMAGE        ; AX..DX, the whole of it on CF = 1
    call dk_sbody
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_srect - content rect AX, BX .. CX, DX (inclusive) again, outside a paint
; (the lock held). Keeps everything
dk_srect:
    push ax
    push bx
    push cx
    push dx
    cmp word [dk_swin], 0
    je .x
    call dk_sorg
    add ax, [dk_sox]
    add cx, [dk_sox]
    add bx, [dk_soy]
    add dx, [dk_soy]
    push bx
    mov bx, [dk_swin]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x                       ; none of it showing
    call dk_sbody
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_sredraw - the content whole, outside a paint. Keeps everything
dk_sredraw:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp word [dk_swin], 0
    je .x
    xor ax, ax
    xor bx, bx
    mov cx, DK_SETW - 1
    mov si, [dk_sgeo]
    mov dx, [si + DKSG_CH]
    dec dx
    call dk_srect
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_sbody - the damage AX, BX .. CX, DX (screen): its rows composed and put
; down a slab at a time. Every register kept
dk_sbody:
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
    call dk_sorg
    push bx
    push di
    mov di, [dk_sox]                ; the content's columns
    mov bx, DK_SETW
    call dk_cols
    pop di
    pop bx
    sub bx, [dk_soy]
    jge .r0
    xor bx, bx
.r0:
    mov [dk_r0], bx
    mov si, [dk_sgeo]
    mov ax, [si + DKSG_CH]
    dec ax
    sub dx, [dk_soy]
    cmp dx, ax
    jle .r1
    mov dx, ax
.r1:
    mov [dk_r1], dx
    mov ax, [ck_cx0]
    cmp ax, [ck_cx1]
    jg .x
    ; the mode: four planes where BLITP takes the content
    mov byte [ck_np], 1
    mov bx, [dk_swin]
    call OSAPI_WM_DISPLAY
    mov [dk_dbpp], dh
    cmp dh, 1
    jbe .m
    mov ax, [dk_sox]
    mov bx, [dk_soy]
    mov cx, DK_SETW
    mov si, [dk_sgeo]
    mov dx, [si + DKSG_CH]
    mov di, 0C000h
    call OSAPI_GFX_BLITP
    jc .m
    mov byte [ck_np], 4
.m:
    call ck_gwhite
    call ck_font
    mov bx, DK_SETW
    call ck_setband
    call dk_stall               ; a band a tile row tall, if the heap has one
    mov ax, [dk_r0]
.slab:
    cmp ax, [dk_r1]
    jg .x
    mov [ck_y0], ax
    call dk_sslab               ; DX = its rows: to a tile row's edge at most
    call dk_scomp
    mov bx, [dk_soy]
    add bx, ax
    mov ax, [dk_sox]
    call dk_put
    mov ax, [ck_y0]
    add ax, dx
    jmp short .slab
.x:
    call dk_sfree
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_stall - on four planes, a heap claim a TILE ROW tall for the paint's
; band: each preview is then composed once, where the 25-row band composed
; each one four times over (4.8 s a full paint on a 4.77 MHz 8088). Refused,
; the paint keeps dk_band. Keeps everything
dk_stall:
    push ax
    push bx
    push cx
    push dx
    push di
    mov word [dk_sbseg], 0
    call dk_stiles              ; AX = the first tile row's y, BX = the pitch
    mov [dk_st0], ax
    mov [dk_spit], bx
    cmp byte [ck_np], 4
    jne .x
    mov ax, DK_SETW / 8 * 4
    mul bx
    mov cx, ax                  ; CX = the bytes
    add ax, 1023
    mov al, ah
    shr al, 1
    shr al, 1
    xor ah, ah                  ; AX = the KB
    call OSAPI_MEM_CLAIM
    jc .x
    mov [dk_sbseg], dx
    mov [ck_bseg], dx
    mov word [ck_buf], 0
    mov word [ck_lrow], 8000h
    mov [ck_sh], bx
    mov [dk_shf], bx
    mov ax, DK_SETW / 8
    mul bx
    mov [ck_ps], ax
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_sfree - the paint's claim back, and dk_band the composer's again
dk_sfree:
    push dx
    mov dx, [dk_sbseg]
    or dx, dx
    jz .x
    mov word [dk_sbseg], 0
    call OSAPI_MEM_FREE
.x:
    call ck_ourband
    pop dx
    ret

; dk_stiles - AX = the first tile row's y, BX = the rows from one to the
; next. Keeps the rest
dk_stiles:
    push si
    push di
    mov si, 0
    call dk_tileent             ; DI = face 0's tile: the first row
    mov ax, [di+4]
    mov si, 3
    call dk_tileent             ; ...face 3's: the second
    mov bx, [di+4]
    sub bx, ax
    pop di
    pop si
    ret

; dk_sslab - AX = a slab's first row: DX = its rows - the band's, the
; damage's, and never across the edge of a tile row - in [ck_sh] and
; [ck_crow] while it is composed. Keeps everything else
dk_sslab:
    push ax
    push bx
    push cx
    mov dx, [dk_r1]
    sub dx, ax
    inc dx
    cmp dx, [dk_shf]
    jbe .a
    mov dx, [dk_shf]
.a:
    mov bx, [dk_st0]            ; the next tile row's edge past AX
    mov cx, 4
.e:
    cmp bx, ax
    jg .b
    add bx, [dk_spit]
    loop .e
    jmp short .s
.b:
    sub bx, ax                  ; BX = the rows to it
    cmp dx, bx
    jbe .s
    mov dx, bx
.s:
    mov [ck_sh], dx
    mov [ck_crow], dl
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dk_scomp - slab [ck_y0] of the content: white, every control whose rows it
; reaches, and the size list over them when it is open. Keeps everything
; -----------------------------------------------------------------------------
dk_scomp:
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
    mov di, [dk_sgeo]
    add di, DKSG_E
.e:
    mov al, [di]
    cmp al, DKST_END
    je .list
    mov bx, [di+4]              ; its rows
    mov dx, [di+8]
    cmp al, DKST_TEXT
    jne .h
    mov dx, 8
.h:
    or dx, dx
    jnz .r
    mov dx, 8
.r:
    sub bx, 3                   ; the default button's ring
    add dx, 6
    call ck_rows
    jc .n
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [dk_sdraw + bx - 2]
.n:
    add di, DKS_ESZ
    jmp short .e
.list:
    cmp byte [dk_sdrop], 0
    je .x
    call dk_slist
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
dk_sdraw:   dw dk_schk, dk_schk, dk_stext, dk_srule, dk_sdropd, dk_stile
            dw dk_sstat, dk_sbtnd

; the entry at DI: AX, BX, CX, DX = its rect
dk_sent:
    mov ax, [di+2]
    mov bx, [di+4]
    mov cx, [di+6]
    mov dx, [di+8]
    ret

; dk_frame - AX, BX, CX, DX a rect: its 1-px edge in the current ink
dk_frame:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cx
    mov di, dx
    mov dx, 1
    call ck_rect                ; the top
    push bx
    add bx, di
    dec bx
    call ck_rect                ; the bottom
    pop bx
    mov cx, 1
    mov dx, di
    call ck_rect                ; the left
    add ax, si
    dec ax
    call ck_rect                ; the right
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; a check box: a 12-px box, the tick when its flag is set, the label
dk_schk:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    mov cx, 12
    mov dx, 12
    call dk_frame
    mov cl, [di+1]
    test [DKP + DKP_FLAGS], cl
    jz .l
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
    cmp byte [di], DKST_CHKB
    jne .p
    mov byte [ck_gbold], 1
.p:
    mov cx, ax
    add cx, 18
    mov dx, bx
    add dx, 2
    mov si, [di+10]
    call ck_text
    mov byte [ck_gbold], 0
    ret

dk_stext:
    mov al, LC_BLK
    call ck_ink
    call ck_sc1
    mov cx, [di+2]
    mov dx, [di+4]
    mov si, [di+10]
    jmp ck_text

dk_srule:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    jmp ck_rect

; the size: a box, the size's name, a cell with the arrow
dk_sdropd:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    call dk_frame
    push ax
    push bx
    add ax, cx                  ; the arrow's cell: its left edge...
    sub ax, 14
    mov cx, 1
    call ck_rect
    add ax, 4                   ; ...and the arrow, 7 wide, 4 deep
    sub dx, 4
    shr dx, 1
    add bx, dx
    mov cx, 7
    mov dx, 1
.ar:
    call ck_rect
    inc ax
    inc bx
    sub cx, 2
    jg .ar
    pop bx
    pop ax
    call ck_sc1
    mov cx, ax
    add cx, 5
    mov dx, [di+8]
    sub dx, 8
    shr dx, 1
    add dx, bx
    mov bl, [DKP + DKP_SIZE]
    xor bh, bh
    shl bx, 1
    mov si, [dkss_sizes + bx]
    jmp ck_text

; dk_slist - the size list, open under its box: three rows, the size
; inverse. Over the gallery, in the same slab - nothing drawn twice
dk_slist:
    call dk_sdropent            ; DI = the drop's entry
    call dk_sent
    add bx, dx
    dec bx                      ; under the box, sharing its bottom edge
    mov dx, 3 * DKS_LROW + 2
    push ax
    mov al, LC_WHT
    call ck_ink
    pop ax
    call ck_rect
    push ax
    mov al, LC_BLK
    call ck_ink
    pop ax
    call dk_frame
    inc bx
    xor si, si
.r:
    cmp si, 3
    jae .x
    push ax
    push bx
    push cx
    mov dx, si
    cmp dl, [DKP + DKP_SIZE]
    jne .t
    push ax                     ; the current one inverse
    mov al, LC_BLK
    call ck_ink
    pop ax
    inc ax
    sub cx, 2
    mov dx, DKS_LROW
    call ck_rect
    push ax
    mov al, LC_WHT
    call ck_ink
    pop ax
    dec ax
.t:
    push si
    call ck_sc1
    mov cx, ax
    add cx, 5
    mov dx, bx
    add dx, 2
    shl si, 1
    mov si, [dkss_sizes + si]
    call ck_text
    pop si
    mov al, LC_BLK
    call ck_ink
    pop cx
    pop bx
    pop ax
    add bx, DKS_LROW
    inc si
    jmp short .r
.x:
    ret

; dk_sdropent - DI = the layout's drop entry
dk_sdropent:
    mov di, [dk_sgeo]
    add di, DKSG_E
.e:
    cmp byte [di], DKST_DROP
    je .r
    add di, DKS_ESZ
    jmp short .e
.r:
    ret

; a tile: the face's preview, its name; the selected one in a double border
; with its name on a black bar behind a ticked box
dk_stile:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    call dk_frame
    mov si, [di+1]
    and si, 0FFh                ; SI = the face
    mov cl, [DKP + DKP_FACE]
    xor ch, ch
    cmp si, cx
    jne .pv
    push ax
    push bx
    push cx
    push dx
    add ax, 2                   ; the second border
    add bx, 2
    sub cx, 4
    sub dx, 4
    mov cx, [di+6]
    sub cx, 4
    call dk_frame
    pop dx
    pop cx
    pop bx
    pop ax
.pv:
    push ax
    push bx
    push si
    ; the preview: the face program at the layout's preview size
    mov di, [dk_sgeo]
    mov cl, [di + DKSG_PWA]
    cmp si, DKF_ANALOG
    jb .w
    mov cl, [di + DKSG_PWD]
.w:
    xor ch, ch
    mov [dk_fw], cx
    mov ax, cx
    cmp si, DKF_ANALOG
    jb .ah
    mov dl, [dk_fhr + si]
    xor dh, dh
    mul dx
    mov al, ah
    mov ah, dl
.ah:
    call dk_sy
    inc ax
    and al, 0FEh
    mov [dk_fh], ax
    pop si
    pop bx
    pop ax
    push ax
    push bx
    push si
    mov di, 112                 ; centred across the tile...
    sub di, [dk_fw]
    sar di, 1
    add ax, di
    mov [ck_ox], ax
    call dk_tileent             ; DI = the tile's entry again
    mov cx, [di+8]              ; ...and in the rows over its name
    sub cx, 14
    sub cx, [dk_fh]
    sar cx, 1
    add cx, 3
    add bx, cx
    mov [ck_oy], bx
    shl si, 1
    shl si, 1
    mov si, [dk_faces + si + 2]
    call dk_face
    mov word [ck_ox], 0
    mov word [ck_oy], 0
    pop si
    pop bx
    pop ax
    ; the name
    call dk_tileent
    mov dx, bx
    add dx, [di+8]
    sub dx, 11                  ; DX = the name's row
    mov cl, [DKP + DKP_FACE]
    xor ch, ch
    cmp si, cx
    jne .nm
    push ax                     ; selected: a black bar and a ticked box
    push dx
    mov al, LC_BLK
    call ck_ink
    pop dx
    pop ax
    push ax
    push bx
    push dx
    add ax, 3
    mov bx, dx
    dec bx
    mov cx, 112 - 6
    mov dx, 10
    call ck_rect
    pop dx
    pop bx
    pop ax
    push ax
    push dx
    mov al, LC_WHT
    call ck_ink
    pop dx
    pop ax
    push ax
    push bx
    push dx
    add ax, 5                   ; the box, white, its tick black
    mov bx, dx
    mov cx, 8
    mov dx, 8
    call ck_rect
    push ax
    mov al, LC_BLK
    call ck_ink
    pop ax
    mov cx, ax
    mov dx, bx
    inc dx
    mov si, dk_tick8
    call ck_bits
    mov al, LC_WHT
    call ck_ink
    pop dx
    pop bx
    pop ax
    mov si, [di+1]
    and si, 0FFh
    mov cx, ax                  ; right of the box
    add cx, 60
    jmp short .tx
.nm:
    push ax
    mov al, LC_BLK
    call ck_ink
    pop ax
    mov cx, ax
    add cx, 56
.tx:
    call ck_sc1
    mov byte [ck_gbold], 1
    shl si, 1
    shl si, 1
    mov si, [dk_faces + si]
    call ck_textc
    mov byte [ck_gbold], 0
    ret
dk_tick8:   db 5                  ; inside an 8-px box, from its second row
            dw 0600h, 0C00h, 5800h, 7000h, 2000h

; dk_tileent - SI = a face: DI = its tile's entry. Keeps the rest
dk_tileent:
    push ax
    mov di, [dk_sgeo]
    add di, DKSG_E
    mov ax, si
.e:
    cmp byte [di], DKST_TILE
    jne .n
    cmp [di+1], al
    je .r
.n:
    add di, DKS_ESZ
    jmp short .e
.r:
    pop ax
    ret

; the status line: `Selected: Red LED`
dk_sstat:
    mov al, LC_BLK
    call ck_ink
    call ck_sc1
    mov cx, [di+2]
    mov dx, [di+4]
    mov si, dkss_sel
    call ck_text
    mov bl, [DKP + DKP_FACE]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov si, [dk_faces + bx]
    mov byte [ck_gbold], 1
    call ck_text
    mov byte [ck_gbold], 0
    ret

; a button: its edge, its label centred; OK the default (a second edge, two
; px thick, a px out); a pressed one inverse
dk_sbtnd:
    mov al, LC_BLK
    call ck_ink
    call dk_sent
    push ax
    mov al, [di+1]
    inc al
    cmp al, [dk_sbtn]
    pop ax
    jne .up
    call ck_rect                ; pressed: black, the label white
    push ax
    mov al, LC_WHT
    call ck_ink
    pop ax
    jmp short .lb
.up:
    call dk_frame
.lb:
    cmp byte [di+1], DKB_OK
    jne .nd
    push ax
    push bx
    push cx
    push dx
    mov si, [ck_pp]             ; the ring is black whatever the label is
    push word [si]
    push ax
    mov al, LC_BLK
    call ck_ink
    pop ax
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
    pop dx                      ; (the ink back: white on a pressed OK)
    mov al, LC_BLK
    cmp dl, 0
    je .ri
    mov al, LC_WHT
.ri:
    call ck_ink
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
; THE GESTURES
; -----------------------------------------------------------------------------
; dk_shit - CX, DX on the content: DI = the control there (a check box, the
; drop, a tile, a button), CF = 1 none. Keeps the rest
dk_shit:
    push ax
    mov di, [dk_sgeo]
    add di, DKSG_E
.e:
    mov al, [di]
    cmp al, DKST_END
    je .no
    cmp al, DKST_TEXT
    je .n
    cmp al, DKST_RULE
    je .n
    cmp al, DKST_STAT
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

; dk_sclick - W_ONCLICK (SI = the window, CX, DX the press; lock held)
dk_sclick:
    push ax
    push bx
    push cx
    push dx
    push di
    call dk_sorg
    sub cx, [dk_sox]
    sub dx, [dk_soy]
    cmp byte [dk_sdrop], 0
    je .hit
    call dk_slisthit            ; the list is up: a pick, or it goes
    jmp short .x
.hit:
    call dk_shit
    jc .x
    mov al, [di]
    cmp al, DKST_CHK
    je .chk
    cmp al, DKST_CHKB
    je .chk
    cmp al, DKST_DROP
    je .drop
    cmp al, DKST_TILE
    je .tile
    mov al, [di+1]              ; a button: down, until the release
    inc al
    mov [dk_sbtn], al
    call dk_sentredraw
    jmp short .x
.chk:
    mov al, [di+1]
    call dk_stoggle
    jmp short .x
.drop:
    mov byte [dk_sdrop], 1
    call dk_slistrect
    call dk_srect
    jmp short .x
.tile:
    mov al, [di+1]
    call dk_sface
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_smup - W_ONMOUSEUP: a pressed button fires if the release is on it
dk_smup:
    push ax
    push cx
    push dx
    push di
    mov al, [dk_sbtn]
    or al, al
    jz .x
    mov byte [dk_sbtn], 0
    push ax
    call dk_sorg
    sub cx, [dk_sox]
    sub dx, [dk_soy]
    call dk_sbtnent             ; DI = that button (AL = its id + 1)
    call dk_sentredraw          ; up again
    pop ax
    push di
    call dk_shit
    pop ax                      ; AX = the pressed button's entry
    jc .x
    cmp di, ax
    jne .x
    mov al, [di+1]
    call dk_sfire
.x:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; dk_sbtnent - AL = a button's id + 1: DI = its entry. Keeps the rest
dk_sbtnent:
    push ax
    dec al
    mov di, [dk_sgeo]
    add di, DKSG_E
.e:
    cmp byte [di], DKST_BTN
    jne .n
    cmp [di+1], al
    je .r
.n:
    add di, DKS_ESZ
    jmp short .e
.r:
    pop ax
    ret

; dk_sfire - AL = DKB_*: Cancel, Apply, OK
dk_sfire:
    cmp al, DKB_APPLY
    je dk_sapply
    cmp al, DKB_OK
    je .ok
    jmp dk_scancel
.ok:
    call dk_sapply
    jmp dk_sshut

; dk_sentredraw - the entry at DI again, with its ring
dk_sentredraw:
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
    call dk_srect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_slistrect - AX, BX .. CX, DX = the size list's rect, open
dk_slistrect:
    push di
    call dk_sdropent
    call dk_sent
    add bx, dx
    dec bx
    add cx, ax
    dec cx
    mov dx, bx
    add dx, 3 * DKS_LROW + 1
    pop di
    ret

; dk_slisthit - CX, DX: a row of the open list picks its size; anywhere else
; shuts the list. Either way it is drawn shut
dk_slisthit:
    push ax
    push bx
    push cx
    push dx
    push di
    call dk_sdropent
    sub cx, [di+2]
    jl .shut
    cmp cx, [di+6]
    jge .shut
    mov ax, dx
    sub ax, [di+4]
    sub ax, [di+8]
    jl .shut
    xor dx, dx
    mov bx, DKS_LROW
    div bx
    cmp ax, 3
    jae .shut
    mov byte [dk_sdrop], 0
    call dk_slistrect
    call dk_srect
    call dk_ssize
    jmp short .x
.shut:
    mov byte [dk_sdrop], 0
    call dk_slistrect
    call dk_srect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_ssize - AL = a size, previewed; the drop again
dk_ssize:
    cmp al, [DKP + DKP_SIZE]
    je .r
    mov [DKP + DKP_SIZE], al
    call dk_spreview
    push di
    call dk_sdropent
    call dk_sentredraw
    pop di
.r:
    ret

; dk_stoggle - AL = a flag, the other way, previewed: its box again, and the
; gallery when the previews show it (the seconds, the hours)
dk_stoggle:
    push di
    xor [DKP + DKP_FLAGS], al
    call dk_spreview
    test al, DKF_SECS | DKF_H24
    jz .box
    call dk_sredraw
    jmp short .x
.box:
    call dk_sflagent
    call dk_sentredraw
.x:
    pop di
    ret

; dk_sflagent - AL = a flag: DI = its check box's entry
dk_sflagent:
    mov di, [dk_sgeo]
    add di, DKSG_E
.e:
    cmp byte [di], DKST_CHK
    je .c
    cmp byte [di], DKST_CHKB
    jne .n
.c:
    cmp [di+1], al
    je .r
.n:
    add di, DKS_ESZ
    jmp short .e
.r:
    ret

; dk_sface - AL = a face, previewed: the old tile, the new one, the status
dk_sface:
    push si
    push di
    mov ah, [DKP + DKP_FACE]
    cmp al, ah
    je .x
    mov [DKP + DKP_FACE], al
    call dk_spreview
    push ax
    mov al, ah
    xor ah, ah
    mov si, ax
    call dk_tileent
    call dk_sentredraw
    pop ax
    xor ah, ah
    mov si, ax
    call dk_tileent
    call dk_sentredraw
    mov di, [dk_sgeo]           ; the status
    add di, DKSG_E
.e:
    cmp byte [di], DKST_STAT
    je .s
    add di, DKS_ESZ
    jmp short .e
.s:
    call dk_sentredraw
.x:
    pop di
    pop si
    ret

; dk_skey - W_ONKEY for the window: Enter OK, Esc Cancel (or the list shut),
; the arrows walk the gallery, F S H the boxes, Z the size, A Apply
dk_skey:
    push ax
    push bx
    cmp byte [dk_sdrop], 0
    je .k
    mov byte [dk_sdrop], 0      ; a key shuts the list first
    push ax
    call dk_slistrect
    call dk_srect
    pop ax
    cmp al, 27
    je .x
.k:
    mov bl, [DKP + DKP_FACE]
    cmp ah, KSC_LEFT
    je .l
    cmp ah, KSC_RIGHT
    je .r
    cmp ah, KSC_UP
    je .u
    cmp ah, KSC_DOWN
    je .d
    cmp al, 13
    je .ok
    cmp al, 27
    je .can
    or al, 20h
    cmp al, 'a'
    je .app
    cmp al, 'f'
    je .only
    cmp al, 's'
    je .secs
    cmp al, 'h'
    je .h24
    cmp al, 'z'
    je .size
    jmp short .x
.l:
    dec bl
    jns .f
    mov bl, DKF_N - 1
    jmp short .f
.r:
    inc bl
    cmp bl, DKF_N
    jb .f
    xor bl, bl
    jmp short .f
.u:
    sub bl, 3
    jns .f
    add bl, DKF_N
    jmp short .f
.d:
    add bl, 3
    cmp bl, DKF_N
    jb .f
    sub bl, DKF_N
.f:
    mov al, bl
    call dk_sface
    jmp short .x
.ok:
    mov al, DKB_OK
    call dk_sfire
    jmp short .x
.can:
    call dk_scancel
    jmp short .x
.app:
    call dk_sapply
    jmp short .x
.only:
    mov al, DKF_ONLY
    jmp short .tg
.secs:
    mov al, DKF_SECS
    jmp short .tg
.h24:
    mov al, DKF_H24
.tg:
    call dk_stoggle
    jmp short .x
.size:
    mov al, [DKP + DKP_SIZE]
    inc al
    cmp al, 3
    jb .sz
    xor al, al
.sz:
    call dk_ssize
.x:
    pop bx
    pop ax
    ret
