; =============================================================================
; apps/deskclok/dkset.inc - DESK CLOCK SETTINGS (SPEC.md 110.14.6): a titled
; second window, unbound (no dock tile), opened by the gear, a double click on
; the clock, Enter, or Clock > Settings.
;
;   [x] Clock face only            (Hide date, alarms and desktop label.)
;   [ ] Seconds   [x] 24-hour      Size: [ Medium ]
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

; THE FORMS: this window and the alarm's (dkalm.inc) are one machine over
; two kinds of LAYOUT - a table of entries, each a control at a rect - and
; two bss records (deskclok.asm's DKW_*: the window, the layout, a button
; pressed). [dk_fm] names the form the routines below work on: a form's own
; callbacks set it, and a paint puts back the one it found (a paint can come
; in the middle of the other form's gesture)
DKST_END    equ 0               ; the entry types: the clickable ones...
DKST_CHK    equ 1               ; param = the flag (DKF_*; DKF_ALARM is the
                                ; alarm's edit, dk_aon); ptr = the label -
                                ; bold for `Clock face only`
DKST_SIZE   equ 2               ; the size: a button that cycles
                                ; Small, Medium, Large on each press -
                                ; param = DKB_SIZE, so it is never drawn
                                ; pressed (dk_sbtnd), ptr = 0: its label is
                                ; the size's name
DKST_TILE   equ 3               ; param = the face (made by dk_tileent)
DKST_BTN    equ 4               ; param = DKB_*; ptr = the label
DKST_FLD    equ 5               ; the alarm's: param = DKAF_*
DKST_TEXT   equ 6               ; ...then the ones a press passes through:
DKST_RULE   equ 7               ; ptr = the text
DKST_STAT   equ 8               ; `Selected: ...`
DKST_TILES  equ 9               ; the gallery: the first tile's x, y, w, h,
                                ; and the rows' pitch at ptr - its nine
                                ; tiles are made from it (dk_tileent)
DKS_ESZ     equ 12              ; an entry: db type, param; dw x, y, w, h, ptr
DKB_CANCEL  equ 0               ; the buttons, both forms'
DKB_APPLY   equ 1
DKB_OK      equ 2               ; the default: a ring
DKB_MINUS   equ 3
DKB_PLUS    equ 4
DKB_SIZE    equ 5               ; (the size's: never a DKST_BTN)
DKSG_CH     equ 0               ; a layout: dw the content's height,
DKSG_PWA    equ 2               ; db an analog preview's width (0: no gallery
DKSG_PWD    equ 3               ; - the alarm's), db a digital one's,
DKSG_W      equ 4               ; dw the content's width,
DKSG_E      equ 6               ; then its entries
DKT_FORM    equ WT_SIZE         ; a form's window template, past WT_*: its
DKT_GEOS    equ WT_SIZE + 2     ; record, its layouts by screen class, its
DKT_CLOSE   equ WT_SIZE + 4     ; close and release callbacks, and the proc
DKT_MUP     equ WT_SIZE + 6     ; that makes its state before it opens
DKT_INIT    equ WT_SIZE + 8

%macro DKSE 7
    db %1, %2
    dw %3, %4, %5, %6, %7
%endmacro
%macro DKS_TILES 3              ; y0, the row step, a tile's height
    DKSE DKST_TILES, 0, 8, %1, 112, %3, %2
%endmacro
DKS_TPITCH  equ 120             ; the gallery's columns: 112 px and a gap

dk_sgeos:   dw dks_tall, dks_mid, dks_comp
dks_tall:
    dw 386
    db 64, 96
    dw DK_SETW
    DKSE DKST_CHK, DKF_ONLY, 8, 6, 200, 12, dkss_only
    DKSE DKST_TEXT, 0, 26, 22, 0, 0, dkss_sub
    DKSE DKST_CHK, DKF_SECS, 8, 40, 100, 12, dkss_secs
    DKSE DKST_CHK, DKF_H24, 120, 40, 100, 12, dkss_h24
    DKSE DKST_TEXT, 0, 228, 42, 0, 0, dkss_size
    DKSE DKST_SIZE, DKB_SIZE, 276, 38, 84, 16, 0
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
    dw DK_SETW
    DKSE DKST_CHK, DKF_ONLY, 8, 4, 200, 12, dkss_only
    DKSE DKST_TEXT, 0, 26, 18, 0, 0, dkss_sub
    DKSE DKST_CHK, DKF_SECS, 8, 32, 100, 12, dkss_secs
    DKSE DKST_CHK, DKF_H24, 120, 32, 100, 12, dkss_h24
    DKSE DKST_TEXT, 0, 228, 34, 0, 0, dkss_size
    DKSE DKST_SIZE, DKB_SIZE, 276, 30, 84, 14, 0
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
    dw DK_SETW
    DKSE DKST_CHK, DKF_ONLY, 8, 2, 140, 12, dkss_only
    DKSE DKST_CHK, DKF_SECS, 160, 2, 90, 12, dkss_secs
    DKSE DKST_CHK, DKF_H24, 264, 2, 90, 12, dkss_h24
    DKSE DKST_TEXT, 0, 8, 17, 0, 0, dkss_size
    DKSE DKST_SIZE, DKB_SIZE, 56, 14, 84, 12, 0
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
dkss_sizes:  dw dk_t_s0 + 2, dk_t_s1 + 2, dk_t_s2 + 2   ; the Size menu's

dk_tick12:  db 8                 ; inside a 12-px box, from its third row
            dw 00C0h, 01C0h, 0380h, 3700h, 3E00h, 1C00h, 0800h, 0000h

dk_stpl:    dw 0, MBAR_H + 4, DK_SETW + 2, 0
            dw dkss_title, dk_spaint, dk_skey, dk_sclick
            dw dk_fset, dk_sgeos, dk_sclose, dk_smup, dk_sapplied

; -----------------------------------------------------------------------------
; dk_setopen / dk_almopen - a form's window: in front if it is up, else made
; (the lock held: a callback's) beside the clock, so its preview shows - its
; state made first by its DKT_INIT. Keep everything
; -----------------------------------------------------------------------------
dk_setopen:
    push si
    mov si, dk_stpl
    jmp short dk_fopen
dk_almopen:
    push si
    mov si, dk_atpl
dk_fopen:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov di, [si + DKT_FORM]
    mov bx, [di + DKW_WIN]
    or bx, bx
    jz .make
    call OSAPI_WM_FRONT
    jmp .x
.make:
    call [si + DKT_INIT]
    mov byte [di + DKW_BTN], 0
    mov bl, [stik_li]           ; the layout for the screen class
    xor bh, bh
    shl bx, 1
    add bx, [si + DKT_GEOS]
    mov bx, [bx]
    mov [di + DKW_GEO], bx
    mov ax, [bx + DKSG_CH]
    add ax, TITLE_H + 1
    mov [si + WT_H], ax
    ; BESIDE THE CLOCK: the side with the more room - wholly clear of it
    ; where the screen allows (W_X = 7 mod 8, the content on the byte grid)
    call OSAPI_VIDEO            ; AX = the screen's width
    mov dx, ax
    mov cx, [si + WT_W]
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [stik_win]
    mov ax, dx
    sub ax, [es:bx + W_X]
    sub ax, [es:bx + W_W]       ; AX = the room right of it
    cmp ax, [es:bx + W_X]
    jg .right
    mov ax, [es:bx + W_X]       ; LEFT: its right edge at the clock's left,
    sub ax, cx                  ; or the screen's edge
    jmp short .snap
.right:
    mov ax, [es:bx + W_X]
    add ax, [es:bx + W_W]
    add ax, 8                   ; clear of it once snapped down
    sub dx, cx
    cmp ax, dx
    jle .snap
    mov ax, dx                  ; ...no further than the screen's right
.snap:
    or ax, ax
    jns .p
    xor ax, ax
.p:
    and ax, 0FFF8h
    dec ax
    jns .px
    mov ax, 7
.px:
    mov [si + WT_X], ax
    call OSAPI_WM_CREATE
    jc .x                       ; the window table is full: nothing opens
    mov [di + DKW_WIN], bx
    mov al, 1
    call OSAPI_WM_OWNBG         ; every pixel is ours
    mov al, 1
    call OSAPI_WM_KEEPH
    call OSAPI_WM_NOANIM        ; no zoom: the paint is the opening
    mov ax, [si + DKT_CLOSE]
    call OSAPI_WM_ONCLOSE
    mov ax, [si + DKT_MUP]
    call OSAPI_WM_ONMOUSEUP
    mov si, dk_menus
    call OSAPI_MENU_SET
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW
.x:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    pop si
    ret

; dk_sapplied / dk_srestore - the payload to dk_sav, and back. Keep all.
; dk_sapplied is the settings' DKT_INIT too: what Cancel goes back to
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

; dk_sshut / dk_ashut - a form's window down; the menus. Keep everything
dk_sshut:
    push si
    mov si, dk_fset
    jmp short dk_fshut
dk_ashut:
    push si
    mov si, dk_falm
dk_fshut:
    push bx
    mov bx, [si + DKW_WIN]
    or bx, bx
    jz .x
    mov word [si + DKW_WIN], 0
    call OSAPI_WM_DESTROY
    call dk_refocus
.x:
    call dk_menulive
    pop bx
    pop si
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
; PAINTING A FORM: its content composed (dkdraw.inc) and put down a slab at a
; time - every control whose rows a slab reaches
; -----------------------------------------------------------------------------
; dk_forg - [dk_wox], [dk_woy] = the form's content's origin. ES not assumed
dk_forg:
    push ax
    push bx
    push dx
    mov bx, [dk_fm]
    mov bx, [bx + DKW_WIN]
    call OSAPI_WM_CONTENT
    mov [dk_wox], ax
    mov [dk_woy], dx
    pop dx
    pop bx
    pop ax
    ret

; dk_spaint / dk_apaint - W_PAINT (SI = the window, the lock held, the clip
; armed): the damage, on that form - and [dk_fm] as it was, after
dk_spaint:
    push ax
    mov ax, dk_fset
dk_fpaint:
    push word [dk_fm]
    mov [dk_fm], ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_DAMAGE        ; AX..DX, the whole of it on CF = 1
    call dk_fbody
    pop dx
    pop cx
    pop bx
    pop word [dk_fm]
    pop ax
    ret

; dk_wrect - the form's content rect AX, BX .. CX, DX (inclusive) again,
; outside a paint (the lock held). Keeps everything
dk_wrect:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [dk_fm]
    cmp word [si + DKW_WIN], 0
    je .x
    call dk_forg
    add ax, [dk_wox]
    add cx, [dk_wox]
    add bx, [dk_woy]
    add dx, [dk_woy]
    push bx
    mov bx, [si + DKW_WIN]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x                       ; none of it showing
    call dk_fbody
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_sredraw - the settings' content whole, outside a paint, from any of our
; windows (the clock's keys and menus too). Keeps everything
dk_sredraw:
    push word [dk_fm]
    mov word [dk_fm], dk_fset
    call dk_fall
    pop word [dk_fm]
    ret

; dk_fall - the form's content whole, outside a paint. Keeps everything
dk_fall:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [dk_fm]
    mov si, [si + DKW_GEO]
    xor ax, ax
    xor bx, bx
    mov cx, [si + DKSG_W]
    dec cx
    mov dx, [si + DKSG_CH]
    dec dx
    call dk_wrect
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_fbody - the damage AX, BX .. CX, DX (screen): its rows composed and put
; down a slab at a time. Every register kept
dk_fbody:
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
    call dk_forg
    mov si, [dk_fm]
    mov si, [si + DKW_GEO]      ; SI = the layout, the paint through
    push bx
    push di
    mov di, [dk_wox]            ; the content's columns
    mov bx, [si + DKSG_W]
    call dk_cols
    pop di
    pop bx
    sub bx, [dk_woy]
    jge .r0
    xor bx, bx
.r0:
    mov [dk_r0], bx
    mov ax, [si + DKSG_CH]
    dec ax
    sub dx, [dk_woy]
    cmp dx, ax
    jle .r1
    mov dx, ax
.r1:
    mov [dk_r1], dx
    mov ax, [ck_cx0]
    cmp ax, [ck_cx1]
    jg .x
    ; the mode: the settings take four planes where BLITP takes the content;
    ; the alarm is black and white, and one plane is all of it
    mov byte [ck_np], 1
    cmp byte [si + DKSG_PWA], 0
    je .m
    push si
    mov bx, [dk_swin]
    call OSAPI_WM_DISPLAY       ; (SI spent)
    mov [dk_dbpp], dh
    cmp dh, 1
    jbe .m1
    pop si
    push si
    mov ax, [dk_wox]
    mov bx, [dk_woy]
    mov cx, [si + DKSG_W]
    mov dx, [si + DKSG_CH]
    mov di, 0C000h
    call OSAPI_GFX_BLITP
    jc .m1
    mov byte [ck_np], 4
.m1:
    pop si
.m:
    call ck_gwhite
    call ck_font
    mov bx, [si + DKSG_W]
    call ck_setband
    call dk_stall               ; a band a tile row tall, if the heap has one
    mov ax, [dk_r0]
.slab:
    cmp ax, [dk_r1]
    jg .x
    mov [ck_y0], ax
    call dk_sslab               ; DX = its rows: to a tile row's edge at most
    call dk_fcomp
    mov bx, [dk_woy]
    add bx, ax
    mov ax, [dk_wox]
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

; dk_stall - the settings on four planes: a heap claim a TILE ROW tall for
; the paint's band, so each preview is composed once, where the 25-row band
; composed each one four times over (4.8 s a full paint on a 4.77 MHz 8088).
; Refused, the paint keeps dk_band. The alarm's slabs meet no tile row
; (dk_st0 = 7FFFh). Keeps everything
dk_stall:
    push ax
    push bx
    push cx
    push dx
    push di
    mov word [dk_sbseg], 0
    mov word [dk_st0], 7FFFh
    cmp word [dk_fm], dk_fset
    jne .x
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
    push di
    mov ax, DKST_TILES
    call dk_ffind
    mov ax, [di + 4]
    mov bx, [di + 10]
    pop di
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
; THE LAYOUT: its entries walked, found, made
; -----------------------------------------------------------------------------
; dk_fwalk - BP = a near proc, called with DI = each entry of the form's
; layout in turn - the gallery's row as its nine tiles (dk_tileent's) - and
; CF = 1 from it stops the walk there. CF out = stopped, DI = that entry.
; Keeps the rest, BP too
dk_fwalk:
    push si
    mov di, [dk_fm]
    mov di, [di + DKW_GEO]
    add di, DKSG_E
.e:
    cmp byte [di], DKST_TILES
    je .t
    cmp byte [di], DKST_END
    je .x                       ; (CF = 0: equal)
    push di
    call bp
    pop di
    jc .x
.n:
    add di, DKS_ESZ
    jmp short .e
.t:
    xor si, si
.tl:
    call dk_tileent             ; DI = tile SI
    push si
    call bp
    pop si
    jc .x
    inc si
    cmp si, DKF_N
    jb .tl
    push ax
    mov ax, DKST_TILES
    call dk_ffind               ; DI = the gallery's row again
    pop ax
    jmp short .n
.x:
    pop si
    ret

; dk_ffind - AL = a type, AH = its param: DI = the form's entry of them (one
; that is there - not a tile). Keeps the rest
dk_ffind:
    mov di, [dk_fm]
    mov di, [di + DKW_GEO]
    add di, DKSG_E - DKS_ESZ
.e:
    add di, DKS_ESZ
    cmp [di], ax
    jne .e
    ret

; dk_ssizeent - DI = the settings' size button. AX spent
dk_ssizeent:
    mov ax, DKST_SIZE | (DKB_SIZE << 8)
    jmp short dk_ffind

; dk_tileent - SI = a face: DI = its tile's entry, made at dk_tent from the
; gallery's row - column face mod 3, row face / 3. Keeps the rest
dk_tileent:
    push ax
    push cx
    mov ax, DKST_TILES
    call dk_ffind
    mov ax, si
    mov cl, 3
    div cl                      ; AL = the row, AH = the column
    mov cx, ax
    mov al, DKS_TPITCH
    mul ch
    add ax, [di + 2]
    mov [dk_tent + 2], ax
    mov al, [di + 10]
    mul cl
    add ax, [di + 4]
    mov [dk_tent + 4], ax
    mov ax, [di + 6]
    mov [dk_tent + 6], ax
    mov ax, [di + 8]
    mov [dk_tent + 8], ax
    mov ax, si
    mov ah, al
    mov al, DKST_TILE
    mov [dk_tent], ax
    mov di, dk_tent
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dk_fcomp - slab [ck_y0] of the form: white, and every control whose rows
; it reaches. Keeps all
; -----------------------------------------------------------------------------
dk_fcomp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call ck_ground
    call ck_clear
    mov word [ck_ox], 0
    mov word [ck_oy], 0
    mov bp, .one
    call dk_fwalk
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; the entry at DI, when the slab reaches its rows (and a button's ring)
.one:
    mov al, [di]
    mov bx, [di+4]              ; its rows
    mov dx, [di+8]
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
    clc
    ret
dk_sdraw:   dw dk_schk, dk_sbtnd, dk_stile, dk_sbtnd, dk_afld, dk_stext
            dw dk_srule, dk_sstat

; the entry at DI: AX, BX, CX, DX = its rect
dk_sent:
    mov ax, [di+2]
    mov bx, [di+4]
    mov cx, [di+6]
    mov dx, [di+8]
    ret

; dk_iblk / dk_iwht - the ink black, or white (ck_ink). Keep everything
dk_iwht:
    push ax
    mov al, LC_WHT
    jmp short dk_iset
dk_iblk:
    push ax
    mov al, LC_BLK
dk_iset:
    call ck_ink
    pop ax
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

; a check box: a 12-px box, the tick when its flag is set, the label - the
; settings' and the alarm's
dk_schk:
    call dk_iblk
    call dk_sent
    mov cx, 12
    mov dx, 12
    call dk_frame
    mov cl, [di+1]
    mov si, DKP + DKP_FLAGS
    cmp cl, DKF_ALARM           ; the alarm's box: its edit's copy
    jne .f
    mov si, dk_aon
.f:
    test [si], cl
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
    cmp byte [di+1], DKF_ONLY   ; `Clock face only`, bold
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
    call dk_iblk
    call ck_sc1
    mov cx, [di+2]
    mov dx, [di+4]
    mov si, [di+10]
    jmp ck_text

dk_srule:
    call dk_iblk
    call dk_sent
    jmp ck_rect

; a tile: the face's preview, its name; the selected one in a double border
; with its name on a black bar behind a ticked box
dk_stile:
    call dk_iblk
    call dk_sent
    call dk_frame
    mov si, [di+1]
    and si, 0FFh                ; SI = the face
    call dk_tsel
    jne .pv
    push ax
    push bx
    push cx
    push dx
    add ax, 2                   ; the second border
    add bx, 2
    sub cx, 4
    sub dx, 4
    call dk_frame
    pop dx
    pop cx
    pop bx
    pop ax
.pv:
    push ax                     ; the preview: the face program at the
    push bx                     ; layout's preview size...
    mov bx, [dk_sgeo]
    mov al, [bx + DKSG_PWA]
    cmp si, DKF_ANALOG
    jb .w
    mov al, [bx + DKSG_PWD]
.w:
    xor ah, ah
    mov [dk_fw], ax
    mov bx, si
    call dk_fhgt                ; AX = its height
    mov [dk_fh], ax
    pop bx
    mov cx, [di+8]              ; ...in the rows over its name...
    sub cx, 14
    sub cx, ax
    sar cx, 1
    add cx, 3
    add cx, bx
    mov [ck_oy], cx
    pop ax
    mov cx, 112                 ; ...and centred across the tile
    sub cx, [dk_fw]
    sar cx, 1
    add cx, ax
    mov [ck_ox], cx
    push si
    shl si, 1
    shl si, 1
    mov si, [dk_faces + si + 2]
    call dk_face
    pop si
    xor cx, cx
    mov [ck_ox], cx
    mov [ck_oy], cx
    ; the name
    mov dx, bx
    add dx, [di+8]
    sub dx, 11                  ; DX = the name's row
    mov cx, ax
    call dk_tsel
    jne .nm
    call dk_iblk                ; selected: a black bar and a ticked box
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
    call dk_iwht
    push ax
    push bx
    push dx
    push si
    add ax, 5                   ; the box, white, its tick black
    mov bx, dx
    mov cx, 8
    mov dx, 8
    call ck_rect
    call dk_iblk
    mov cx, ax
    mov dx, bx
    inc dx
    mov si, dk_tick8
    call ck_bits
    call dk_iwht
    pop si
    pop dx
    pop bx
    pop ax
    mov cx, ax                  ; the name right of the box
    add cx, 60 - 56
    jmp short .tx
.nm:
    call dk_iblk
.tx:
    add cx, 56
    call ck_sc1
    mov byte [ck_gbold], 1
    shl si, 1
    shl si, 1
    mov si, [dk_faces + si]
    call ck_textc
    mov byte [ck_gbold], 0
    ret

; dk_tsel - ZF = 1: the tile at DI is the face the payload has. Keeps all
dk_tsel:
    push ax
    mov al, [di+1]
    cmp al, [DKP + DKP_FACE]
    pop ax
    ret

dk_tick8:   db 5                  ; inside an 8-px box, from its second row
            dw 0600h, 0C00h, 5800h, 7000h, 2000h

; the status line: `Selected: Red LED`
dk_sstat:
    call dk_iblk
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

; a button, either form's: its edge, its label centred; OK the default (a
; second edge, two px thick, a px out); a pressed one inverse. The settings'
; size is one too, its label the size's name
dk_sbtnd:
    call dk_iblk
    call dk_sent
    push ax
    mov si, [dk_fm]
    mov al, [di+1]
    inc al
    cmp al, [si + DKW_BTN]
    pop ax
    jne .up
    call ck_rect                ; pressed: black, the label white
    call dk_iwht
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
    call dk_iblk
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
    or si, si
    jnz .t
    mov bl, [DKP + DKP_SIZE]    ; the size's button: its name
    xor bh, bh
    shl bx, 1
    mov si, [dkss_sizes + bx]
.t:
    call ck_textc
    mov byte [ck_gbold], 0
    ret

; -----------------------------------------------------------------------------
; THE GESTURES
; -----------------------------------------------------------------------------
; dk_fhit - CX, DX on the form's content: DI = the control there (a check
; box, the size, a tile, a button, a field), CF = 1 none. Keeps the rest
dk_fhit:
    push ax
    push bp
    mov bp, .one
    call dk_fwalk               ; CF = 1: stopped on it
    cmc
    pop bp
    pop ax
    ret
.one:
    cmp byte [di], DKST_TEXT    ; text, a rule, the status: passed through
    jae .n
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
    stc
    ret
.n:
    clc
    ret

; dk_sclick / dk_aclick - W_ONCLICK (CX, DX the press; lock held): the
; control under it, on either form
dk_sclick:
    push si
    mov si, dk_fset
    jmp short dk_fclick
dk_aclick:
    push si
    mov si, dk_falm
dk_fclick:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [dk_fm], si
    call dk_forg
    sub cx, [dk_wox]
    sub dx, [dk_woy]
    call dk_fhit
    jc .x
    mov ax, [di]                ; AL = the type, AH = its param
    cmp al, DKST_BTN
    jne .c
    inc ah                      ; a button: down, until the release
    mov [si + DKW_BTN], ah
    call dk_sentredraw
    jmp short .x
.c:
    xchg al, ah                 ; AL = the param, AH = the type
    cmp ah, DKST_CHK
    jne .d
    cmp al, DKF_ALARM
    je .aon
    call dk_stoggle
    jmp short .x
.aon:
    call dk_kaon                ; the alarm's own box: its edit
    jmp short .x
.d:
    cmp ah, DKST_SIZE           ; the size: the next one, on the press
    jne .t
    call dk_kzsize
    jmp short .x
.t:
    cmp ah, DKST_TILE
    jne .f
    call dk_sface
    jmp short .x
.f:
    cmp al, DKAF_AMPM           ; a field of the alarm's
    jne .fo
    call dk_aampm
    jmp short .x
.fo:
    call dk_afocus
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    pop si
    ret

; dk_smup / dk_amup - W_ONMOUSEUP: a pressed button fires if the release is
; on it
dk_smup:
    push si
    mov si, dk_fset
    jmp short dk_fmup
dk_amup:
    push si
    mov si, dk_falm
dk_fmup:
    push ax
    push cx
    push dx
    push di
    mov [dk_fm], si
    mov ah, [si + DKW_BTN]
    or ah, ah
    jz .x
    mov byte [si + DKW_BTN], 0
    call dk_forg
    sub cx, [dk_wox]
    sub dx, [dk_woy]
    dec ah
    mov al, DKST_BTN
    call dk_ffind               ; DI = that button
    call dk_sentredraw          ; up again
    push di
    call dk_fhit
    pop ax                      ; AX = the pressed button's entry
    jc .x
    cmp di, ax
    jne .x
    mov al, [di+1]
    cmp si, dk_fset
    jne .a
    call dk_sfire
    jmp short .x
.a:
    call dk_afire
.x:
    pop di
    pop dx
    pop cx
    pop ax
    pop si
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

; dk_sentredraw - the entry at DI again, with its ring (the form's)
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
    call dk_wrect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_ssize - AL = a size, previewed; its button again
dk_ssize:
    cmp al, [DKP + DKP_SIZE]
    je .r
    mov [DKP + DKP_SIZE], al
    call dk_spreview
    push ax
    push di
    call dk_ssizeent
    call dk_sentredraw
    pop di
    pop ax
.r:
    ret

; dk_stoggle - AL = a flag, the other way, previewed: its box again, and the
; gallery when the previews show it (the seconds, the hours)
dk_stoggle:
    push ax
    push di
    xor [DKP + DKP_FLAGS], al
    call dk_spreview
    test al, DKF_SECS | DKF_H24
    jz .box
    call dk_sredraw
    jmp short .x
.box:
    mov ah, al
    mov al, DKST_CHK
    call dk_ffind
    call dk_sentredraw
.x:
    pop di
    pop ax
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
    mov ax, DKST_STAT           ; the status
    call dk_ffind
    call dk_sentredraw
.x:
    pop di
    pop si
    ret

; dk_skey - W_ONKEY for the window: Enter OK, Esc Cancel, the arrows walk
; the gallery, F S H the boxes, Z the size, A Apply
dk_skey:
    push ax
    push bx
    push si
    mov word [dk_fm], dk_fset
    mov si, dk_kset
    call dk_keys
    pop si
    pop bx
    pop ax
    ret
dk_kset:    DKKEY 80h + KSC_LEFT, -1, dk_kgal     ; the gallery, round it
            DKKEY 80h + KSC_RIGHT, 1, dk_kgal
            DKKEY 80h + KSC_UP, -3, dk_kgal
            DKKEY 80h + KSC_DOWN, 3, dk_kgal
            DKKEY 13, DKB_OK, dk_sfire
            DKKEY 27, 0, dk_scancel
            DKKEY 'a', 0, dk_sapply
            DKKEY 'f', DKF_ONLY, dk_stoggle
            DKKEY 's', DKF_SECS, dk_stoggle
            DKKEY 'h', DKF_H24, dk_stoggle
            DKKEY 'z', 0, dk_kzsize
            db 0

; dk_kgal - AL = a step: the gallery's face that far on, previewed
dk_kgal:
    call dk_fstep
    jmp dk_sface

; dk_kzsize - Z, or a press on the size's button: the next size, round the
; three
dk_kzsize:
    mov al, [DKP + DKP_SIZE]
    inc ax
    cmp al, 3
    jb .s
    xor al, al
.s:
    jmp dk_ssize
