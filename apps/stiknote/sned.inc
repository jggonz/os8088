; =============================================================================
; apps/stiknote/sned.inc - THE EDITOR: Sticky Note's second window (SPEC.md
; 110.13)
;
;   +------------------------------------------------+
;   | [ New ] [ Delete ]  [x] On desktop             |  the bar, TBH rows
;   |------------------------------------------------|
;   | Notes          | XT project                 |^|
;   | [=] XT project | -------------------------- | |  the list, LW wide,
;   | [ ] Shopping   | Next build                 |#|  the page, and the
;   |                |  [ ] Fix mouse driver      |#|  scroll bar, 16
;   |                |  [x] Back up source        | |
;   |                |                            |v|
;   |------------------------------------------------|
;   | 3 notes                              Saved   //|  the status, SBH
;   +------------------------------------------------+
;
; Every pixel of the content is composed into the band a slab of rows at a
; time (sne_compose) and put down with OSAPI_GFX_BLIT1 - the page in the
; note's paper, the rest in white (sne_put): one blit a slab, three where a
; coloured page sits between the white list and the white bar. A keystroke
; recomposes the lines it changed and nothing else (sne_after).
;
; It is UNBOUND (SPEC.md 42.1): no dock tile, no Task Manager row, and the
; kernel asks OSAPI_WM_ONCLOSE before its close box hides it - sne_onclose,
; which hides it, or closes the whole program when the pad is off the desk.
; Its minimize box is a plain hide; sn_tick notices.
; =============================================================================

SNE_LW      equ 144             ; the list's width: the page starts here
SNE_SBW     equ 16              ; the scroll bar's
SNE_W0      equ 384             ; the content's width, opened
SNE_WMIN    equ 344             ; ...at least: `Click New to add one.` fits
SNE_WMAX    equ GFXE_BAND_W     ; ...at most: the band's
SNE_TX      equ SNE_LW + 8      ; the page's text x
SNE_BX      equ SNE_LW + 10     ; ...a box's
SNE_BTX     equ SNE_LW + 26     ; ...a box line's text
SNE_LIX     equ 10              ; the list's icon x
SNE_LTX     equ 28              ; ...its text x
SNE_LTN     equ 14              ; ...its cells
SNE_NEWX    equ 8               ; the bar: New,
SNE_NEWW    equ 64
SNE_DELX    equ 80              ; ...Delete,
SNE_DELW    equ 72
SNE_CBX     equ 168             ; ...the box,
SNE_CBTX    equ 184             ; ...and its label

SNEG_TBH    equ 0               ; db the bar's rows (its rule the last)
SNEG_BY     equ 1               ; db a button's top
SNEG_BH     equ 2               ; db ...its rows
SNEG_BTY    equ 3               ; db ...its text row, from its top
SNEG_BBW    equ 4               ; db ...its edge
SNEG_CBY    equ 5               ; db the check box's row
SNEG_LHY    equ 6               ; db `Notes`, from the bar's foot
SNEG_L0     equ 7               ; db list row 0's text row, from there
SNEG_LP     equ 8               ; db ...the pitch
SNEG_ICY    equ 9               ; db ...the icon's row against the text's
SNEG_TY     equ 10              ; db the title's text row, from the bar's foot
SNEG_RY     equ 11              ; db ...the rule's
SNEG_RH     equ 12              ; db ...its rows
SNEG_B0     equ 13              ; db body line 0's text row, from there
SNEG_P      equ 14              ; db ...the pitch
SNEG_LOFF   equ 15              ; db a line's rows start this far above it
SNEG_BOXY   equ 16              ; db a box's row against its text's
SNEG_SBH    equ 17              ; db the status's rows (its rule the first)
SNEG_AH     equ 18              ; db a scroll arrow's box
SNEG_AX     equ 19              ; db ...the arrow's x in it
SNEG_AY     equ 20              ; db ...and its row
SNEG_HMIN   equ 21              ; db the content's least height
SNEG_H0     equ 22              ; dw the content's height, opened
SNEG_ICON   equ 24              ; dw the list's page icon
SNEG_BOX    equ 26              ; dw the box
SNEG_BOXX   equ 28              ; dw ...ticked
SNEG_UP     equ 30              ; dw the arrows
SNEG_DN     equ 32
SNEG_BART   equ 34              ; db the list's bar: rows above the text
SNEG_BARH   equ 35              ; db ...its rows
SNEG_SZ     equ 36

sne_geos:
    ;  TBH BY BH BTY BBW CBY LHY L0 LP ICY TY RY RH B0  P LOFF BOXY SBH AH AX AY HMIN
    db 28,  6, 16, 4,  2,  9,  6, 22, 15, -2, 6, 17, 2, 25, 12, 2, -1, 16, 16, 2, 2, 110
    dw 248
    dw sn_doc12, sn_box10, sn_box10x, sn_up11, sn_dn11
    db 3, 14
.mid:
    db 24,  4, 15, 4,  1,  7,  4, 17, 12, -1, 4, 14, 1, 20, 10, 1, -1, 14, 12, 3, 2, 96
    dw 212
    dw sn_doc9, sn_box9, sn_box9x, sn_up8, sn_dn8
    db 2, 11
.comp:
    db 20,  3, 13, 3,  1,  6,  3, 14, 10, -1, 3, 13, 1, 17,  9, 1,  0, 12, 11, 3, 2, 84
    dw 140
    dw sn_doc9, sn_box8, sn_box8x, sn_up8, sn_dn8
    db 1, 10
%if sne_geos.mid - sne_geos != SNEG_SZ || sne_geos.comp - sne_geos.mid != SNEG_SZ
  %error "sned: a geometry entry is SNEG_SZ bytes"
%endif

SNE_H_NONE  equ 0               ; sne_press: what a press went to
SNE_H_NEW   equ 1
SNE_H_DEL   equ 2
SNE_H_DESK  equ 3
SNE_H_OTHER equ 4               ; ...acted on at the press already

; sne_geo - DI = this layout's geometry. Every other register kept
sne_geo:
    push ax
    mov al, [stik_li]
    mov ah, SNEG_SZ
    mul ah
    add ax, sne_geos
    mov di, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sne_lay - the window as it is now: its content's size and origin and what
; follows from them. CF = 1 there is no editor on the glass - draw nothing.
; Every register kept
; -----------------------------------------------------------------------------
sne_lay:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, [sne_win]
    or bx, bx
    jz .no
    call OSAPI_WM_GEOM          ; CX, DX = the content; CF = not visible
    jc .no
    mov [sne_cw], cx
    mov [sne_ch], dx
    call OSAPI_WM_CONTENT       ; AX, DX = its origin
    mov [sne_ox], ax
    mov [sne_oy], dx
    call sne_derive
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; sne_derive - the layout's numbers from [sne_cw] / [sne_ch]. Every register
; kept
sne_derive:
    push ax
    push bx
    push dx
    push di
    call sne_geo
    mov ax, [sne_cw]
    sub ax, SNE_SBW
    mov [sne_sbx], ax
    sub ax, 8 + SNE_TX
    jns .t
    xor ax, ax
.t:
    shr ax, 1
    shr ax, 1
    shr ax, 1
    or al, al
    jnz .t1
    inc ax
.t1:
    mov [it_w], al              ; (the walk's; set again by sne_widths)
    mov [sne_tw], al
    sub al, 2                   ; a box line: 18 px further in, two cells
    jg .b
    mov al, 1
.b:
    mov [sne_bw], al
    mov ax, [sne_ch]
    xor bx, bx
    mov bl, [di + SNEG_SBH]
    sub ax, bx
    mov [sne_pb], ax            ; the status's first row
    mov bl, [di + SNEG_TBH]
    sub ax, bx                  ; the panes' rows
    push ax
    mov bl, [di + SNEG_B0]
    sub ax, bx
    mov bl, [di + SNEG_LOFF]
    add ax, bx
    jns .v
    xor ax, ax
.v:
    mov bl, [di + SNEG_P]
    xor dx, dx
    div bx
    or ax, ax
    jnz .v1
    inc ax
.v1:
    mov [sne_nv], ax            ; the body lines that fit
    pop ax
    mov bl, [di + SNEG_L0]
    sub ax, bx
    add ax, 3
    jns .l
    xor ax, ax
.l:
    mov bl, [di + SNEG_LP]
    xor dx, dx
    div bx
    or al, al
    jnz .l1
    inc ax
.l1:
    mov [sne_nl], al            ; the list rows that fit
    pop di
    pop dx
    pop bx
    pop ax
    ret

; sne_widths - the walk's widths for the editor's page
sne_widths:
    push ax
    mov al, [sne_tw]
    mov [it_w], al
    mov al, [sne_bw]
    mov [it_wb], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; sne_make - the window, the first time it is wanted: sized for the layout and
; the screen, placed at the desk's top left, its callbacks installed. CF = 1
; the window table is full (and nothing else happened)
; -----------------------------------------------------------------------------
sne_make:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sne_geo
    call OSAPI_VIDEO            ; AX, BX = the screen, CX = the dock's row
    mov word [sne_tpl + WT_X], 23
    mov word [sne_tpl + WT_Y], MBAR_H + 8
    sub cx, MBAR_H + 8 + TITLE_H + 1 + 4
    mov ax, [di + SNEG_H0]
    cmp ax, cx
    jbe .h
    mov ax, cx                  ; the desk band's height, at most
.h:
    xor dx, dx
    mov dl, [di + SNEG_HMIN]
    cmp ax, dx
    jge .h1
    mov ax, dx
.h1:
    add ax, TITLE_H + 1
    mov [sne_tpl + WT_H], ax
    mov word [sne_tpl + WT_W], SNE_W0 + 2
    mov si, sne_tpl
    call OSAPI_WM_CREATE        ; BX = it, CF = the table is full
    jc .x
    mov [sne_win], bx
    mov al, 1
    call OSAPI_WM_SIZABLE
    call OSAPI_WM_OWNBG         ; every pixel is ours
    mov ax, sne_onsize
    call OSAPI_WM_ONSIZE
    mov ax, sne_onclose
    call OSAPI_WM_ONCLOSE
    mov ax, sne_onmup
    call OSAPI_WM_ONMOUSEUP
    mov si, sn_menus
    call OSAPI_MENU_SET
    mov si, sn_about
    call OSAPI_ABOUT_SET
    call sne_newcur
    clc
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sne_tpl:
    dw 23, 28, SNE_W0 + 2, 267
    dw sn_title, sne_paint, sne_onkey, sne_onclick

; sne_show - the editor up, in front - made the first time (lock held)
sne_show:
    push ax
    push bx
    cmp word [sne_win], 0
    jne .have
    call sne_make
    jc .x                       ; no window to be had: the pad remains
.have:
    mov bx, [sne_win]
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW          ; un-hide and raise, and paint
    mov byte [sne_open], 1
    call sn_menulive
.x:
    pop bx
    pop ax
    ret

; sne_onsize - the resize negotiator (SI = the window, CX, DX the proposed
; FRAME): the content kept between SNE_WMIN and SNE_WMAX wide and at least
; the layout's least height
sne_onsize:
    push di
    cmp cx, SNE_WMIN + 2
    jge .w1
    mov cx, SNE_WMIN + 2
.w1:
    cmp cx, SNE_WMAX + 2
    jle .w2
    mov cx, SNE_WMAX + 2
.w2:
    call sne_geo
    push ax
    xor ax, ax
    mov al, [di + SNEG_HMIN]
    add ax, TITLE_H + 1
    cmp dx, ax
    jge .h
    mov dx, ax
.h:
    pop ax
    pop di
    ret

; sne_onclose - the close negotiator (SI = the editor, lock held): with the
; pad on the desk the editor hides (CF = 0, the kernel's hide); with it off,
; the editor was the program's only window, so the PROGRAM closes - through
; the pad, whose negotiator removes the Sticky App row and saves - and this
; close is refused (CF = 1): the teardown takes the editor with it
sne_onclose:
    test byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    jz .quit
    mov byte [sne_open], 0
    call sn_menulive
    clc
    ret
.quit:
    push si
    mov si, [stik_win]
    call stik_quit              ; CF = 1 declined (the disk): stay up
    pop si
    stc
    ret

; sne_newcur - a new current note: the caret at its title's end, the page
; from the top, the list scrolled to show it
sne_newcur:
    push ax
    call sn_tend
    mov [sne_car], ax
    mov [sne_goal], al
    mov word [sne_top], 0
    mov byte [sne_jb], 0
    call sne_lfit
    pop ax
    ret

; sne_lfit - [sne_ltop] so the current note's row is on show. Every register
; kept
sne_lfit:
    cmp byte [sne_nl], 0
    je .r                       ; never laid out: sne_fix does it at the paint
    push ax
    mov al, [STIK_PAY + SNP_CUR]
    cmp al, [sne_ltop]
    jae .lo
    mov [sne_ltop], al
    jmp short .x
.lo:
    mov ah, [sne_ltop]
    add ah, [sne_nl]
    cmp al, ah
    jb .x
    sub al, [sne_nl]
    inc ax
    mov [sne_ltop], al
.x:
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; sne_paint - W_PAINT (SI = the editor, lock held, the kernel's clip armed):
; what OSAPI_WM_DAMAGE asks for, then the About card over it
; -----------------------------------------------------------------------------
sne_paint:
    call sne_lay
    jc .r
    call sne_clamp              ; a resize may have moved the page's range
    mov bx, si
    call OSAPI_WM_DAMAGE        ; AX..DX, screen, inclusive
    sub ax, [sne_ox]
    jns .a
    xor ax, ax
.a:
    sub cx, [sne_ox]
    inc cx                      ; exclusive
    cmp cx, [sne_cw]
    jle .c
    mov cx, [sne_cw]
.c:
    sub bx, [sne_oy]
    jns .b
    xor bx, bx
.b:
    sub dx, [sne_oy]
    cmp dx, [sne_ch]
    jl .d
    mov dx, [sne_ch]
    dec dx
.d:
    cmp ax, cx
    jge .ab
    cmp bx, dx
    jg .ab
    call sne_put
.ab:
    call sn_abdraw
.r:
    ret

; sne_rect - content columns AX .. CX - 1, rows BX .. DX, drawn again outside
; a paint (lock held): the clip armed for the editor first. Nothing when it
; is not on the glass. Every register kept
sne_rect:
    push bx
    call sne_lay
    jc .x
    push bx
    mov bx, [sne_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x
    pop bx
    push bx
    call sne_put
.x:
    pop bx
    ret

; sne_rall - the whole content again (the grow box after it, the kernel's)
sne_rall:
    push ax
    push bx
    push cx
    push dx
    call sne_lay
    jc .x
    call sne_clamp
    xor ax, ax
    xor bx, bx
    mov cx, [sne_cw]
    mov dx, [sne_ch]
    dec dx
    call sne_rect
    mov bx, [sne_win]
    call OSAPI_WM_GROW
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rbar - the bar (the box went the other way)
sne_rbar:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    call sne_geo
    xor ax, ax
    xor bx, bx
    mov cx, [sne_cw]
    mov dl, [di + SNEG_TBH]
    xor dh, dh
    dec dx
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rpane - the page and the scroll bar whole
sne_rpane:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    call sne_clamp
    call sne_geo
    mov ax, SNE_LW
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    mov cx, [sne_cw]
    mov dx, [sne_pb]
    dec dx
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rlist - the list whole
sne_rlist:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    call sne_geo
    xor ax, ax
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    mov cx, SNE_LW
    mov dx, [sne_pb]
    dec dx
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rlrow - list row for note AL (if it is on show)
sne_rlrow:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    sub al, [sne_ltop]
    jb .x
    cmp al, [sne_nl]
    jae .x
    call sne_geo
    mul byte [di + SNEG_LP]
    add al, [di + SNEG_TBH]
    adc ah, 0
    add al, [di + SNEG_L0]
    adc ah, 0
    sub al, [di + SNEG_BART]
    sbb ah, 0
    mov bx, ax
    mov dl, [di + SNEG_LP]
    xor dh, dh
    add dx, bx
    dec dx
    xor ax, ax
    mov cx, SNE_LW - 8          ; the divider is not the row's
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rsb - the scroll bar
sne_rsb:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    call sne_geo
    mov ax, [sne_sbx]
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    mov cx, [sne_cw]
    mov dx, [sne_pb]
    dec dx
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rtitle - the page's title line
sne_rtitle:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    call sne_geo
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    xor ax, ax
    mov al, [di + SNEG_TY]
    add bx, ax
    sub bx, 2
    mov dx, bx
    add dx, 11
    mov ax, SNE_LW
    mov cx, [sne_sbx]
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rlines - body lines AX .. DX (indices) again, the part on show
sne_rlines:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    cmp ax, [sne_top]
    jge .a
    mov ax, [sne_top]
.a:
    mov bx, [sne_top]
    add bx, [sne_nv]
    dec bx                      ; the last on show
    cmp dx, bx
    jle .b
    mov dx, bx
.b:
    cmp ax, dx
    jg .x
    push dx
    call sne_lrow               ; BX = line AX's first row
    pop ax
    push bx
    call sne_lrow               ; ...and the last line's
    call sne_geo
    mov dl, [di + SNEG_P]
    xor dh, dh
    add dx, bx
    dec dx
    pop bx
    mov ax, SNE_LW
    mov cx, [sne_sbx]
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_lrow - body line AX (on show): BX = its first content row. The rest
; kept
sne_lrow:
    push ax
    push dx
    push di
    call sne_geo
    sub ax, [sne_top]
    mov bl, [di + SNEG_P]
    xor bh, bh
    mul bx
    mov bl, [di + SNEG_TBH]
    add ax, bx
    mov bl, [di + SNEG_B0]
    add ax, bx
    mov bl, [di + SNEG_LOFF]
    sub ax, bx
    mov bx, ax
    pop di
    pop dx
    pop ax
    ret

; sne_rbody - every body line on show, and the scroll bar
sne_rbody:
    push ax
    push bx
    push cx
    push dx
    push di
    call sne_lay
    jc .x
    call sne_geo
    mov ax, [sne_top]
    call sne_lrow
    mov ax, SNE_LW
    mov cx, [sne_cw]
    mov dx, [sne_pb]
    dec dx
    call sne_rect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rstat - the status row, left of the grow box
sne_rstat:
    push ax
    push bx
    push cx
    push dx
    call sne_lay
    jc .x
    xor ax, ax
    mov bx, [sne_pb]
    mov cx, [sne_cw]
    sub cx, SNE_SBW
    mov dx, [sne_ch]
    dec dx
    call sne_rect
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_rptr - the editor's body line holding pointer SI again (a box the pad
; ticked), if the editor is up
sne_rptr:
    push ax
    push dx
    call sne_lay
    jc .x
    call sne_widths
    call sn_vfind               ; AX = the line
    mov dx, ax
    call sne_rlines
.x:
    pop dx
    pop ax
    ret

; sne_rswitch - the current note changed from AH, the list's scroll from BL:
; the two list rows (or the list, scrolled), the page and the bar
sne_rswitch:
    push ax
    push bx
    call sne_lay
    jc .x
    call sne_lfit
    cmp bl, [sne_ltop]
    je .rows
    call sne_rlist
    jmp short .p
.rows:
    mov al, ah
    call sne_rlrow
    mov al, [STIK_PAY + SNP_CUR]
    call sne_rlrow
.p:
    call sne_rpane
.x:
    pop bx
    pop ax
    ret

; sne_status - the status word to what the save says, drawn on a change
sne_status:
    push ax
    mov al, SNS_NOSAVE
    test byte [stik_st], STIK_S_NOHOME | STIK_S_RDONLY
    jnz .s
    mov al, SNS_EDITED
    test byte [stik_st], STIK_S_DIRTY
    jnz .s
    mov al, SNS_SAVED
.s:
    cmp al, [sne_stat]
    je .x
    mov [sne_stat], al
    call sne_rstat
.x:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sne_put - content columns AX .. CX - 1 (rounded out to the byte grid), rows
; BX .. DX: composed GFXE_BAND_H rows at a time and put down. The clip is
; armed. Every register kept
; -----------------------------------------------------------------------------
sne_put:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov word [gfxe_fseg], 0     ; the glyph table asked for again: a resume
                                ; may have MOVED it (SPEC.md 5.12.8)
    and ax, 0FFF8h
    add cx, 7
    and cx, 0FFF8h
    mov [sne_x0], ax
    mov [sne_x1], cx
    mov [sne_cx0], ax
    mov [sne_cx1], cx
    mov [sne_r0], bx
    mov [sne_r1], dx
    call sne_widths
.slab:
    mov cx, [sne_r1]
    sub cx, [sne_r0]
    inc cx
    cmp cx, GFXE_BAND_H
    jbe .n
    mov cx, GFXE_BAND_H
.n:
    mov [gfxe_bh], cx
    mov [sne_bn], cx
    mov ax, [sne_r0]
    mov [sne_ba], ax
    mov ax, 0FFFFh
    call gfxe_bclear
    call sne_compose
    call sne_blits
    mov ax, [sne_r0]
    add ax, [sne_bn]
    mov [sne_r0], ax
    cmp ax, [sne_r1]
    jle .slab
    mov al, CWHITE
    call sn_setpen
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_blits - the slab at [sne_ba] ([sne_bn] rows), columns [sne_x0] ..
; [sne_x1]: the page's rows in the note's paper, the rest in white
sne_blits:
    call sne_geo
    mov bx, [sne_ba]            ; the bar's rows: white
    mov dl, [di + SNEG_TBH]
    xor dh, dh
    mov [sn_t0], dx             ; the panes' first row...
    mov ax, [sne_pb]
    mov [sn_t1], ax             ; ...and the status's
    mov dx, bx
    add dx, [sne_bn]
    dec dx                      ; the slab's last row
    cmp bx, [sn_t0]
    jge .mid
    push dx
    mov ax, [sn_t0]
    dec ax
    cmp dx, ax
    jle .t
    mov dx, ax
.t:
    call sne_bwhite
    pop dx
    mov bx, [sn_t0]
.mid:
    cmp bx, dx
    jg .r
    cmp bx, [sn_t1]
    jge .bot
    push dx
    mov ax, [sn_t1]
    dec ax
    cmp dx, ax
    jle .m
    mov dx, ax
.m:
    call sne_bpanes
    pop dx
    mov bx, [sn_t1]
    cmp bx, dx
    jg .r
.bot:
    call sne_bwhite
.r:
    ret

; sne_bwhite - rows BX..DX, every column asked, in white
sne_bwhite:
    mov al, CWHITE
    call sn_setpen
    mov ax, [sne_x0]
    mov cx, [sne_x1]
    jmp sne_b

; sne_bpanes - rows BX..DX of the panes: the list and the bar white, the page
; in the note's paper - one blit when the paper is white
sne_bpanes:
    call sn_pencur
    cmp al, CWHITE
    jne .three
    cmp byte [sn_mono], 0
    jne .one
    jmp short .one
.three:
    mov ax, [sne_x0]            ; the list
    mov cx, [sne_x1]
    cmp cx, SNE_LW
    jle .l
    mov cx, SNE_LW
.l:
    cmp ax, cx
    jge .p
    push ax
    mov al, CWHITE
    call sn_setpen
    pop ax
    call sne_b
.p:
    mov ax, [sne_x0]            ; the page
    cmp ax, SNE_LW
    jge .p1
    mov ax, SNE_LW
.p1:
    mov cx, [sne_x1]
    cmp cx, [sne_sbx]
    jle .p2
    mov cx, [sne_sbx]
.p2:
    cmp ax, cx
    jge .s
    push ax
    call sn_pencur
    call sn_setpen
    pop ax
    call sne_b
.s:
    mov ax, [sne_x0]            ; the bar
    cmp ax, [sne_sbx]
    jge .s1
    mov ax, [sne_sbx]
.s1:
    mov cx, [sne_x1]
    cmp ax, cx
    jge .r
    push ax
    mov al, CWHITE
    call sn_setpen
    pop ax
    jmp sne_b
.one:
    mov al, CWHITE
    call sn_setpen
    mov ax, [sne_x0]
    mov cx, [sne_x1]
    jmp sne_b
.r:
    ret

; sne_b - band columns AX .. CX - 1 (byte grid), content rows BX .. DX of the
; slab at [sne_ba], onto the glass. AX, CX, SI spent
sne_b:
    push bx
    push dx
    sub cx, ax                  ; the width
    jle .x
    sub dx, bx
    inc dx                      ; the rows
    jle .x
    push ax
    push dx
    mov ax, bx
    sub ax, [sne_ba]
    mov si, GFXE_BAND_ST
    mul si
    mov si, ax                  ; the band row
    pop dx
    pop ax
    push ax
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add si, ax                  ; ...its byte
    pop ax
    add ax, [sne_ox]
    add bx, [sne_oy]
    call sn_b1
.x:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sne_compose - content rows [sne_ba] .. + [sne_bn] - 1 into the band (row -
; [sne_ba]), the regions [sne_cx0] .. [sne_cx1] reach. Every register kept
; -----------------------------------------------------------------------------
sne_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov byte [gfxe_tsty], 0
    call sne_geo
    mov al, [di + SNEG_TBH]
    xor ah, ah
    cmp [sne_ba], ax
    jge .panes
    call sne_cbar
.panes:
    call sne_geo
    mov ax, [sne_ba]
    add ax, [sne_bn]            ; past the slab
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    cmp ax, bx
    jle .stat
    mov ax, [sne_pb]
    cmp [sne_ba], ax
    jge .stat
    cmp word [sne_cx0], SNE_LW
    jge .pg
    call sne_clist
.pg:
    mov ax, [sne_cx1]
    cmp ax, SNE_LW
    jle .sb
    mov ax, [sne_cx0]
    cmp ax, [sne_sbx]
    jge .sb
    call sne_cpage
.sb:
    mov ax, [sne_cx1]
    cmp ax, [sne_sbx]
    jle .stat
    call sne_csb
.stat:
    mov ax, [sne_ba]
    add ax, [sne_bn]
    cmp ax, [sne_pb]
    jle .x
    call sne_cstat
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_fill - ink over content x AX, row BX, CX wide, DX rows (GFXE_INV:
; paper); AX..BP spent
sne_fill:
    sub bx, [sne_ba]
    jmp gfxe_bfill

; sne_rec - the bit record SI at content x CX, row DX; AX..BP spent
sne_rec:
    sub dx, [sne_ba]
    jmp gfxe_bits

; sne_vis - content rows DX - 3 .. DX + 10 (a text line's, its box and its
; bar) against the slab: CF = 1 it misses. Every register kept
sne_vis:
    push ax
    mov ax, dx
    add ax, 10
    cmp ax, [sne_ba]
    jl .no
    mov ax, [sne_ba]
    add ax, [sne_bn]
    push dx
    sub dx, 3
    cmp dx, ax
    pop dx
    jge .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; sne_str - the NUL string SI at content x CX, row DX (if it reaches the
; slab); AX..BP spent
sne_str:
    call sne_vis
    jc .r
    sub dx, [sne_ba]
    jmp gfxe_text
.r:
    ret

; --- the bar ------------------------------------------------------------------
sne_cbar:
    mov ax, SNE_NEWX            ; New
    mov cx, SNE_NEWW
    mov si, sne_s_new
    mov bl, SNE_H_NEW
    call sne_cbtn
    mov ax, SNE_DELX            ; Delete
    mov cx, SNE_DELW
    mov si, sne_s_del
    mov bl, SNE_H_DEL
    call sne_cbtn
    cmp byte [STIK_PAY + SNP_N], 0
    jne .cb
    mov ax, SNE_DELX            ; ...greyed with no note to delete: the
    mov cx, SNE_DELW            ; caption a checkerboard (SPEC.md 47)
    call sne_grey
.cb:
    call sne_geo                ; the box: On desktop
    mov dl, [di + SNEG_CBY]
    xor dh, dh
    mov si, [di + SNEG_BOX]
    test byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    jz .b
    mov si, [di + SNEG_BOXX]
.b:
    mov cx, SNE_CBX
    call sne_rec
    call sne_geo
    mov dl, [di + SNEG_BY]
    add dl, [di + SNEG_BTY]
    xor dh, dh
    mov si, sne_s_desk
    mov cx, SNE_CBTX
    call sne_str
    call sne_geo                ; the bar's rule
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    dec bx
    xor ax, ax
    mov cx, [sne_cw]
    mov dx, 1
    jmp sne_fill

; sne_cbtn - a button at x AX, CX wide, captioned SI, drawn DOWN when BL is
; [sne_lit]
sne_cbtn:
    mov [sn_t0], ax
    mov [sn_t1], cx
    push si
    push bx
    call sne_geo
    mov bl, [di + SNEG_BY]
    xor bh, bh
    mov dl, [di + SNEG_BH]
    xor dh, dh
    call sne_fill               ; its face in ink
    pop bx
    cmp bl, [sne_lit]
    je .down
    call sne_geo                ; up: paper inside its edge
    mov al, [di + SNEG_BBW]
    xor ah, ah
    mov si, ax
    mov ax, [sn_t0]
    add ax, si
    mov cx, [sn_t1]
    sub cx, si
    sub cx, si
    mov bl, [di + SNEG_BY]
    xor bh, bh
    add bx, si
    mov dl, [di + SNEG_BH]
    xor dh, dh
    sub dx, si
    sub dx, si
    mov byte [gfxe_tsty], GFXE_INV
    call sne_fill
    mov byte [gfxe_tsty], 0
    jmp short .t
.down:
    mov byte [gfxe_tsty], GFXE_INV  ; down: the caption white on the ink
.t:
    pop si
    call sn_strlen              ; BX = the caption's length
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov cx, [sn_t1]
    sub cx, bx
    shr cx, 1
    add cx, [sn_t0]             ; centred
    call sne_geo
    mov dl, [di + SNEG_BY]
    add dl, [di + SNEG_BTY]
    xor dh, dh
    call sne_str
    mov byte [gfxe_tsty], 0
    ret

; sn_strlen - BX = the length of the NUL string SI. Every other register kept
sn_strlen:
    push si
    xor bx, bx
.l:
    cmp byte [si], 0
    je .x
    inc si
    inc bx
    jmp short .l
.x:
    pop si
    ret

; sne_grey - the inside of the button at x AX, CX wide: half its ink taken
; away, the checkerboard a 1bpp screen greys with (SPEC.md 47)
sne_grey:
    push ax
    push bx
    push cx
    push dx
    push di
    shr ax, 1
    shr ax, 1
    shr ax, 1
    inc ax                      ; the first byte wholly inside
    add cx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    sub cx, 2                   ; ...and how many
    jle .x
    call sne_geo
    mov bl, [di + SNEG_BY]
    xor bh, bh
    mov dl, [di + SNEG_BH]
    xor dh, dh
    add dx, bx                  ; past its last row
.row:
    mov di, bx
    sub di, [sne_ba]
    jl .next
    cmp di, [sne_bn]
    jge .next
    push ax
    push cx
    push dx
    push ax
    mov ax, GFXE_BAND_ST
    mul di                      ; (DX spent: the pattern comes after)
    mov di, ax
    pop ax
    add di, ax
    mov dl, 0AAh
    test bl, 1
    jz .b
    mov dl, 055h
.b:
    or [GFXE_BAND_BUF + di], dl
    inc di
    loop .b
    pop dx
    pop cx
    pop ax
.next:
    inc bx
    cmp bx, dx
    jb .row
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the list -----------------------------------------------------------------
sne_clist:
    call sne_geo                ; `Notes`
    mov dl, [di + SNEG_TBH]
    add dl, [di + SNEG_LHY]
    xor dh, dh
    mov si, sne_s_notes
    mov cx, 8
    mov byte [gfxe_tsty], GFXE_BOLD
    call sne_str
    mov byte [gfxe_tsty], 0
    call sne_geo                ; the divider
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    mov dx, [sne_pb]
    sub dx, bx
    mov ax, SNE_LW - 1
    mov cx, 1
    call sne_fill
    mov byte [sn_t2], 0         ; the row on show
.row:
    mov al, [sn_t2]
    cmp al, [sne_nl]
    jae .r
    add al, [sne_ltop]
    cmp al, [STIK_PAY + SNP_N]
    jae .r
    mov [sn_t2 + 1], al         ; the note
    call sne_geo
    mov al, [sn_t2]
    mul byte [di + SNEG_LP]
    add al, [di + SNEG_TBH]
    adc ah, 0
    add al, [di + SNEG_L0]
    adc ah, 0
    mov dx, ax                  ; its text row
    call sne_vis
    jc .next
    mov [sn_t3], dx
    mov byte [gfxe_tsty], 0
    mov al, [sn_t2 + 1]
    cmp al, [STIK_PAY + SNP_CUR]
    jne .ic
    call sne_geo                ; the current one: an ink bar, the rest white
    mov bx, dx
    mov al, [di + SNEG_BART]
    xor ah, ah
    sub bx, ax
    mov dl, [di + SNEG_BARH]
    xor dh, dh
    mov ax, 4
    mov cx, SNE_LW - 12
    call sne_fill
    mov byte [gfxe_tsty], GFXE_INV
.ic:
    call sne_geo
    mov dx, [sn_t3]
    mov al, [di + SNEG_ICY]
    cbw
    add dx, ax
    mov si, [di + SNEG_ICON]
    mov cx, SNE_LIX
    call sne_rec
    mov al, [sn_t2 + 1]         ; its title, cut to the column
    call sn_nptr                ; SI = it, CX = its length
    mov bx, si
    add cx, si
.t:
    cmp bx, cx
    jae .te
    cmp byte [bx], 10
    je .te
    inc bx
    jmp short .t
.te:
    sub bx, si                  ; BX = the title's length
    jnz .tn
    mov si, sn_s_untit
    call sn_strlen
.tn:
    cmp bx, SNE_LTN
    jbe .tc
    mov bx, SNE_LTN
.tc:
    mov dx, [sn_t3]
    sub dx, [sne_ba]
    mov cx, SNE_LTX
    call sn_glyphs
    mov byte [gfxe_tsty], 0
.next:
    inc byte [sn_t2]
    jmp .row
.r:
    ret

; --- the page -----------------------------------------------------------------
sne_cpage:
    cmp byte [STIK_PAY + SNP_N], 0
    jne .note
    call sne_geo                ; no notes: what to do about it
    mov dl, [di + SNEG_TBH]
    add dl, [di + SNEG_B0]
    xor dh, dh
    push dx
    mov si, sn_s_none
    mov cx, SNE_TX
    call sne_str
    pop dx
    call sne_geo
    add dl, [di + SNEG_P]
    adc dh, 0
    mov si, sne_s_click
    mov cx, SNE_TX
    jmp sne_str
.note:
    call sne_geo                ; the title
    mov dl, [di + SNEG_TBH]
    add dl, [di + SNEG_TY]
    xor dh, dh
    call sne_vis
    jc .rule
    push dx
    call sn_tend
    mov bl, [sne_tw]
    xor bh, bh
    cmp ax, bx
    jae .tc
    mov bx, ax
.tc:
    call sn_cur
    pop dx
    push dx
    sub dx, [sne_ba]
    mov cx, SNE_TX
    call sn_glyphs
    pop dx
    call sne_loc                ; AX = the caret's line, -1 the title
    cmp ax, -1
    jne .rule
    call sne_ccar               ; ...on the title: its bar
.rule:
    call sne_geo
    mov bl, [di + SNEG_TBH]
    add bl, [di + SNEG_RY]
    xor bh, bh
    mov dl, [di + SNEG_RH]
    xor dh, dh
    mov ax, SNE_TX
    mov cx, [sne_sbx]
    sub cx, SNE_TX + 8
    call sne_fill
    ; --- the body: the lines on show
    call sne_widths
    mov ax, [sne_top]
    call sn_vseek
    jc .r
    mov ax, [sne_top]
    mov [sn_t0], ax             ; the line
.l:
    mov ax, [sn_t0]
    sub ax, [sne_top]
    cmp ax, [sne_nv]
    jae .r
    call sne_geo
    mov bl, [di + SNEG_P]
    xor bh, bh
    mul bx
    mov bl, [di + SNEG_TBH]
    add ax, bx
    mov bl, [di + SNEG_B0]
    add ax, bx
    mov dx, ax                  ; its text row
    call sne_vis
    jc .n
    mov [sn_t3], dx
    call sne_cline
.n:
    inc word [sn_t0]
    call sn_vadv
    jnc .l
.r:
    ret

; sne_cline - the walk's line at text row [sn_t3]: its box, its text, and the
; caret when it is the caret's
sne_cline:
    mov dx, [sn_t3]
    mov cx, SNE_TX
    cmp byte [it_k], 0
    je .t
    mov cx, SNE_BTX
    cmp byte [it_f], 0
    je .t
    call sne_geo
    mov al, [di + SNEG_BOXY]
    cbw
    add dx, ax
    mov si, [di + SNEG_BOX]
    cmp byte [it_k], SN_BOX1
    jne .b
    mov si, [di + SNEG_BOXX]
.b:
    mov cx, SNE_BX
    call sne_rec
    mov dx, [sn_t3]
    mov cx, SNE_BTX
.t:
    sub dx, [sne_ba]
    mov si, [it_p]
    mov bx, [it_le]
    sub bx, si
    call sn_glyphs
    ; the caret: on this line when it lies between its start and its next's
    call sn_cur                 ; SI = the note
    add si, [sne_car]
    cmp si, [it_p]
    jb .r
    cmp si, [it_n]
    jb .on
    cmp byte [it_end], 0
    je .r
    cmp si, [it_n]
    jne .r
.on:
    sub si, [it_p]
    mov ax, si
    mov cx, SNE_TX
    cmp byte [it_k], 0
    je .c
    mov cx, SNE_BTX
.c:
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add cx, ax
    mov dx, [sn_t3]
    jmp sne_cbarat
.r:
    ret

; sne_ccar - the caret on the title, at its column
sne_ccar:
    mov ax, [sne_car]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov cx, SNE_TX
    add cx, ax
    call sne_geo
    mov dl, [di + SNEG_TBH]
    add dl, [di + SNEG_TY]
    xor dh, dh
; sne_cbarat - the caret's bar left of the cell at content x CX, text row DX
sne_cbarat:
    dec cx
    cmp cx, [sne_sbx]
    jge .r
    call sne_geo
    mov bl, [di + SNEG_LOFF]
    xor bh, bh
    neg bx
    add bx, dx
    inc bx                      ; its first row
    mov dl, [di + SNEG_P]
    xor dh, dh
    sub dx, 2
    mov ax, cx
    mov cx, 1
    jmp sne_fill
.r:
    ret

; --- the scroll bar -------------------------------------------------------------
sne_csb:
    call sne_geo
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    mov [sn_t0], bx             ; its first row
    mov dx, [sne_pb]
    sub dx, bx
    mov ax, [sne_sbx]
    mov cx, 1
    call sne_fill               ; its left edge
    call sne_geo
    mov bl, [di + SNEG_AH]
    xor bh, bh
    add bx, [sn_t0]
    dec bx
    mov ax, [sne_sbx]
    mov cx, SNE_SBW
    mov dx, 1
    call sne_fill               ; the up box's foot
    call sne_geo
    mov bx, [sne_pb]
    mov al, [di + SNEG_AH]
    xor ah, ah
    sub bx, ax
    mov ax, [sne_sbx]
    mov cx, SNE_SBW
    mov dx, 1
    call sne_fill               ; the down box's top
    call sne_geo
    mov dl, [di + SNEG_AY]
    xor dh, dh
    add dx, [sn_t0]
    mov si, [di + SNEG_UP]
    mov cl, [di + SNEG_AX]
    xor ch, ch
    add cx, [sne_sbx]
    call sne_rec                ; the arrows
    call sne_geo
    mov dx, [sne_pb]
    mov al, [di + SNEG_AH]
    xor ah, ah
    sub dx, ax
    mov al, [di + SNEG_AY]
    add dx, ax
    mov si, [di + SNEG_DN]
    mov cl, [di + SNEG_AX]
    xor ch, ch
    add cx, [sne_sbx]
    call sne_rec
    call sne_thumb              ; AX = the track's first row, DX past its last,
    jc .r                       ; BX, CX = the thumb's: nothing to scroll
    push bx
    push cx
    call sne_dither             ; the track grey...
    pop cx
    pop bx
    push cx                     ; ...the thumb white, edged
    mov dx, cx
    sub dx, bx
    inc dx
    mov ax, [sne_sbx]
    inc ax
    mov cx, SNE_SBW - 1
    push ax
    push bx
    mov byte [gfxe_tsty], GFXE_INV
    call sne_fill
    mov byte [gfxe_tsty], 0
    pop bx
    pop ax
    push ax
    mov cx, SNE_SBW - 1
    mov dx, 1
    call sne_fill
    pop ax
    pop bx
    mov cx, SNE_SBW - 1
    mov dx, 1
    jmp sne_fill
.r:
    ret

; sne_thumb - the scroll bar's track: AX = its first row, DX past its last;
; CF = 0 and BX, CX = the thumb's first and last rows - CF = 1 every line is
; on show (no thumb, a white track). Every other register kept
sne_thumb:
    push si
    push di
    call sne_geo
    mov al, [di + SNEG_AH]
    xor ah, ah
    mov si, ax
    mov bl, [di + SNEG_TBH]
    xor bh, bh
    add ax, bx                  ; AX = the track's first row
    mov dx, [sne_pb]
    sub dx, si                  ; DX = past its last
    push ax
    push dx
    call sne_nt                 ; AX = the body's lines
    mov si, ax
    pop dx
    pop ax
    cmp si, [sne_nv]
    jbe .none
    mov di, dx
    sub di, ax                  ; DI = the track's rows
    cmp di, 12
    jl .none
    push ax
    push dx
    mov ax, di
    mul word [sne_nv]
    div si                      ; the thumb's rows, in proportion
    cmp ax, 8
    jge .h
    mov ax, 8
.h:
    mov cx, ax                  ; CX = its rows
    mov ax, di
    sub ax, cx                  ; the travel
    mul word [sne_top]
    sub si, [sne_nv]            ; the scroll's range
    div si
    pop dx
    pop bx                      ; BX = the track's first row
    push bx
    add bx, ax                  ; the thumb's first row
    add cx, bx
    dec cx                      ; ...and its last
    cmp cx, dx
    jl .ok
    mov cx, dx
    dec cx
.ok:
    pop ax
    pop di
    pop si
    clc
    ret
.none:
    pop di
    pop si
    stc
    ret

; sne_dither - the track's rows AX .. DX - 1 grey: alternate bits cleared,
; phased on the content row; the bar's left edge stays ink
sne_dither:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, ax
.row:
    cmp bx, dx
    jge .x
    mov di, bx
    sub di, [sne_ba]
    jl .n
    cmp di, [sne_bn]
    jge .n
    push dx
    mov ax, GFXE_BAND_ST
    mul di
    mov di, ax
    mov ax, [sne_sbx]
    mov cl, 3
    shr ax, cl
    add di, ax
    mov al, 0AAh
    test bl, 1
    jz .p
    mov al, 055h
.p:
    mov ah, al
    and al, 07Fh
    and [GFXE_BAND_BUF + di], al
    and [GFXE_BAND_BUF + di + 1], ah
    pop dx
.n:
    inc bx
    jmp short .row
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the status -----------------------------------------------------------------
sne_cstat:
    mov bx, [sne_pb]            ; its rule
    xor ax, ax
    mov cx, [sne_cw]
    mov dx, 1
    call sne_fill
    call sne_geo
    mov dl, [di + SNEG_SBH]
    shr dl, 1
    xor dh, dh
    add dx, [sne_pb]
    sub dx, 3
    mov [sn_t3], dx
    mov di, sn_line             ; `3 notes`
    mov al, [STIK_PAY + SNP_N]
    or al, al
    jnz .n
    mov si, sne_s_nonotes
    jmp short .c
.n:
    call sn_dec
    mov si, sne_s_notes1
    cmp byte [STIK_PAY + SNP_N], 1
    je .cp
    mov si, sne_s_notesn
.cp:
    lodsb
    stosb
    or al, al
    jnz .cp
    mov si, sn_line
.c:
    mov cx, 8
    mov dx, [sn_t3]
    call sne_str
    mov al, [sne_stat]          ; `Saved`, at the right
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov si, [sne_stats + bx]
    call sn_strlen
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov cx, [sne_cw]
    sub cx, 24
    sub cx, bx
    mov dx, [sn_t3]
    jmp sne_str

sne_stats:  dw sne_s_saved, sne_s_edited, sne_s_nosave

; -----------------------------------------------------------------------------
; the caret's place, and the page's scroll
; -----------------------------------------------------------------------------

; sne_loc - AX = the caret's body line, or -1 on the title. Every other
; register kept (the walk is left on the line)
sne_loc:
    push si
    push di
    push cx
    call sn_tend
    cmp [sne_car], ax
    ja .body
    mov ax, -1
    jmp short .x
.body:
    call sne_widths
    call sn_cur
    add si, [sne_car]
    call sn_vfind
.x:
    pop cx
    pop di
    pop si
    ret

; sne_nt - AX = the body's lines at the editor's widths. The rest kept
sne_nt:
    call sne_widths
    jmp sn_vcount

; sne_clamp - the list's scroll and the page's IN RANGE, and no more: what
; a paint may do, so a scroll-bar scroll away from the caret survives a
; repaint and a partial one draws one scroll position. Every register kept
sne_clamp:
    push ax
    call sne_lfit
    call sne_nt
    sub ax, [sne_nv]
    jge .m
    xor ax, ax
.m:
    cmp [sne_top], ax           ; no further than the last screenful
    jle .x
    mov [sne_top], ax
.x:
    pop ax
    ret

; sne_fix - sne_clamp, and the caret's line on show: after a move or an
; edit. CF = 1 the page's scroll moved. Every register kept
sne_fix:
    push ax
    push bx
    push dx
    mov bx, [sne_top]
    call sne_clamp
    call sne_loc
    cmp ax, -1
    je .y                       ; on the title, which is always on show
    cmp ax, [sne_top]
    jge .lo
    mov [sne_top], ax
    jmp short .y
.lo:
    mov dx, [sne_top]
    add dx, [sne_nv]
    cmp ax, dx
    jl .y
    sub ax, [sne_nv]
    inc ax
    mov [sne_top], ax
.y:
    cmp bx, [sne_top]
    pop dx
    pop bx
    pop ax
    je .same
    stc
    ret
.same:
    clc
    ret

; sne_lpos - the caret onto body line AX at column [sne_goal] (or its end). A
; line broken HARD keeps the caret off its end, which is the next line's start
sne_lpos:
    push ax
    push bx
    push si
    push di
    push cx
    call sne_widths
    call sn_vseek
    mov si, [it_p]
    mov al, [sne_goal]
    xor ah, ah
    add si, ax
    cmp si, [it_le]
    jbe .k
    mov si, [it_le]
.k:
    cmp si, [it_le]
    jne .set
    cmp byte [it_end], 0
    jne .set
    mov ax, [it_le]
    cmp ax, [it_n]
    jne .set                    ; it ends at a space or a LF
    cmp si, [it_p]
    je .set
    dec si
.set:
    mov bx, si
    call sn_cur
    sub bx, si
    mov [sne_car], bx
    pop cx
    pop di
    pop si
    pop bx
    pop ax
    ret

; sne_col - AL = the caret's column on its line (the title's, or a body
; line's). Every other register kept
sne_col:
    push bx
    push cx
    push si
    push di
    mov bx, ax
    call sne_loc
    cmp ax, -1
    jne .b
    mov ax, [sne_car]
    jmp short .x
.b:
    call sn_cur
    add si, [sne_car]
    sub si, [it_p]
    mov ax, si
.x:
    mov ah, bh
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; the keys (SI = the editor, lock held)
; -----------------------------------------------------------------------------
sne_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    call sn_abdismiss
    jc .x
    call sne_lay
    jc .x
    cmp al, SN_C_NEW
    jne .k0
    call sn_new
    jmp .x
.k0:
    cmp byte [STIK_PAY + SNP_N], 0
    je .x                       ; nothing to edit
    cmp ah, KSC_DEL             ; Del - and not the keypad's `.`
    jne .k1
    or al, al
    jz .del
    cmp al, 0E0h
    jne .k1
.del:
    call sne_kdel
    jmp .x
.k1:
    mov bx, sne_moves
.mv:
    cmp byte [bx], 0
    je .ch
    cmp ah, [bx]
    je .move
    add bx, 3
    jmp short .mv
.move:
    or al, al                   ; the arrows: an ASCII 0 or E0 with them,
    jz .m1                      ; else it is the keypad's digit
    cmp al, 0E0h
    jne .ch
.m1:
    call sne_premove
    call [bx + 1]
    call sne_postmove
    jmp short .x
.ch:
    cmp al, 8
    jne .k2
    call sne_kbs
    jmp short .x
.k2:
    cmp al, 13
    jne .k3
    call sne_kenter
    jmp short .x
.k3:
    cmp al, SN_C_BOX
    jne .k4
    call sne_cbox
    jmp short .x
.k4:
    cmp al, ' '
    jb .x
    cmp al, 127
    jae .x
    call sne_ktype
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sne_moves:  db KSC_LEFT
            dw sne_kleft
            db KSC_RIGHT
            dw sne_kright
            db KSC_UP
            dw sne_kup
            db KSC_DOWN
            dw sne_kdown
            db KSC_HOME
            dw sne_khome
            db KSC_END
            dw sne_kend
            db KSC_PGUP
            dw sne_kpgup
            db KSC_PGDN
            dw sne_kpgdn
            db 0

; sne_premove / sne_postmove - around a caret move: the old caret's line
; banked, then the new one's drawn - or the page, when it scrolled. A move
; that is not Up or Down sets the goal column
sne_premove:
    mov byte [sne_jb], 0
    push ax
    call sne_loc
    mov [sne_l0], ax
    pop ax
    cmp ah, KSC_UP
    je .v
    cmp ah, KSC_DOWN
    je .v
    cmp ah, KSC_PGUP
    je .v
    cmp ah, KSC_PGDN
    je .v
    mov byte [sne_vert], 0
    ret
.v:
    cmp byte [sne_vert], 0
    jne .r
    mov byte [sne_vert], 1
    push ax
    call sne_col
    mov [sne_goal], al
    pop ax
.r:
    ret

sne_postmove:
    call sn_norm
    cmp byte [sne_vert], 0
    jne .g
    push ax
    call sne_col
    mov [sne_goal], al
    pop ax
.g:
    call sne_fix
    jnc .lines
    jmp sne_rpane               ; scrolled: the page, its title (an old
.lines:                         ; caret there) and the bar
    jmp sne_rcar2

; sne_rcar2 - the caret's old line ([sne_l0]) and its new one drawn
sne_rcar2:
    push ax
    push dx
    mov ax, [sne_l0]
    call sne_rcl
    call sne_loc
    cmp ax, [sne_l0]
    je .x
    call sne_rcl
.x:
    pop dx
    pop ax
    ret

; sne_rcl - line AX (-1 the title) drawn again
sne_rcl:
    cmp ax, -1
    jne .b
    jmp sne_rtitle
.b:
    push dx
    mov dx, ax
    call sne_rlines
    pop dx
    ret

sne_kleft:
    cmp word [sne_car], 0
    je .r
    dec word [sne_car]
    push si
    push di
    push cx
    call sn_cur
    add si, [sne_car]
    call sn_ismark
    jc .x
    dec word [sne_car]          ; off the marker: the line before's end
.x:
    pop cx
    pop di
    pop si
.r:
    ret

sne_kright:
    push si
    push di
    push cx
    call sn_cur
    cmp [sne_car], cx
    jae .x
    inc word [sne_car]          ; (sn_norm steps a marker it lands on)
.x:
    pop cx
    pop di
    pop si
    ret

sne_kup:
    push ax
    call sne_loc
    cmp ax, -1
    je .x
    or ax, ax
    jnz .b
    call sn_tend                ; line 0: up to the title
    mov bl, [sne_goal]
    xor bh, bh
    cmp bx, ax
    jbe .t
    mov bx, ax
.t:
    mov [sne_car], bx
    jmp short .x
.b:
    dec ax
    call sne_lpos
.x:
    pop ax
    ret

sne_kdown:
    push ax
    push bx
    call sne_loc
    inc ax                      ; the title's -1 is line 0
    mov bx, ax
    call sne_nt
    cmp bx, ax
    jae .x
    mov ax, bx
    call sne_lpos
.x:
    pop bx
    pop ax
    ret

sne_khome:
    push ax
    push si
    push di
    push cx
    call sne_loc
    cmp ax, -1
    jne .b
    mov word [sne_car], 0
    jmp short .x
.b:
    mov ax, [it_p]
    call sn_cur
    sub ax, si
    mov [sne_car], ax
.x:
    pop cx
    pop di
    pop si
    pop ax
    ret

sne_kend:
    push ax
    push si
    push di
    push cx
    call sne_loc
    cmp ax, -1
    jne .b
    call sn_tend
    mov [sne_car], ax
    jmp short .x
.b:
    mov byte [sne_goal], 0FFh
    call sne_lpos
.x:
    pop cx
    pop di
    pop si
    pop ax
    ret

sne_kpgup:
    push ax
    call sne_loc
    cmp ax, -1
    je .x
    sub ax, [sne_nv]
    jge .p
    mov word [sne_car], 0       ; past the top: the title's start
    jmp short .x
.p:
    push ax
    mov ax, [sne_top]
    sub ax, [sne_nv]
    jge .t
    xor ax, ax
.t:
    mov [sne_top], ax
    pop ax
    call sne_lpos
.x:
    pop ax
    ret

sne_kpgdn:
    push ax
    push bx
    call sne_loc
    add ax, [sne_nv]
    mov bx, ax
    call sne_nt
    or ax, ax
    jz .x
    cmp bx, ax
    jb .p
    mov bx, ax
    dec bx
.p:
    mov ax, [sne_top]
    add ax, [sne_nv]
    mov [sne_top], ax           ; sne_fix brings it back in range
    mov ax, bx
    call sne_lpos
.x:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the edits: each banks the state first (sne_pre) and redraws what it changed
; after (sne_after)
; -----------------------------------------------------------------------------

; sne_pre - before an edit: the caret's line and the body's lines banked; AL
; = 0 a body edit, 1 the title's alone, 2 one that crosses the title's end
sne_pre:
    mov byte [sne_jb], 0        ; any edit spends the swallow
    mov [sne_tk], al
    push ax
    call sne_loc
    mov [sne_l0], ax
    call sne_nt
    mov [sne_nt0], ax
    pop ax
    mov byte [sne_vert], 0
    ret

; sne_after - after an edit: the caret normalized and its goal taken, then the
; lines it changed drawn - its own and the one before it (a wrap can pull a
; word back) to its paragraph's end when the body kept its line count, else
; to the page's foot - and the scroll bar if the count moved; the title and
; its list row for a title edit; then the save owed
sne_after:
    push ax
    push bx
    push dx
    call sn_norm
    call sne_col
    mov [sne_goal], al
    call sn_edit
    call sne_fix
    jc .all                     ; scrolled: the page whole
    cmp byte [sne_tk], 1
    jb .body
    ja .all
    call sne_nt                 ; the title's alone
    cmp ax, [sne_nt0]
    jne .all
    call sne_rtitle
    jmp short .row
.body:
    call sne_nt
    mov bx, ax
    mov ax, [sne_l0]
    dec ax
    jge .f
    xor ax, ax
.f:
    cmp bx, [sne_nt0]
    jne .foot
    push ax
    call sne_loc                ; the walk on the caret's line: on to its
    mov dx, ax                  ; paragraph's last
.p:
    cmp byte [it_lf], 0
    jne .pe
    cmp byte [it_end], 0
    jne .pe
    inc dx
    call sn_vadv
    jnc .p
.pe:
    pop ax
    cmp dx, [sne_l0]
    jge .d
    mov dx, [sne_l0]
.d:
    call sne_rlines
    jmp short .x
.foot:
    mov dx, 7FFFh
    call sne_rlines
    call sne_rsb
    jmp short .x
.all:
    call sne_rpane
.row:
    mov al, [STIK_PAY + SNP_CUR]
    call sne_rlrow
.x:
    pop dx
    pop bx
    pop ax
    ret

; sne_ktype - a printable AL at the caret
sne_ktype:
    cmp byte [sne_jb], 0
    je .go
    mov byte [sne_jb], 0
    cmp al, ' '                 ; the space after a box just made is the box's
    jne .go
    ret
.go:
    cmp al, ']'
    jne .ins
    call sne_mkbox              ; `[ ]`, `[]`, `[x]`: CF = 0 it was one
    jnc .r
.ins:
    push ax
    call sn_tend
    cmp [sne_car], ax
    pop ax
    ja .body
    push ax                     ; the title: one line, as wide as the page
    push bx
    call sn_tend
    mov bl, [sne_tw]
    xor bh, bh
    cmp ax, bx
    pop bx
    pop ax
    jae .r
    push ax
    mov al, 1
    call sne_pre
    pop ax
    jmp short .put
.body:
    push ax
    mov al, 0
    call sne_pre
    pop ax
.put:
    mov bx, [sne_car]
    call sn_ins
    jc .r
    inc word [sne_car]
    call sne_after
.r:
    ret

; sne_pstart - AX = the offset of the caret's paragraph's first byte (a body
; caret). Every other register kept
sne_pstart:
    push cx
    push si
    push di
    call sn_cur
    mov ax, si
    add si, [sne_car]
.l:
    cmp si, ax
    je .x
    cmp byte [si - 1], 10
    je .x
    dec si
    jmp short .l
.x:
    sub si, ax
    mov ax, si
    pop di
    pop si
    pop cx
    ret

; sne_mkbox - `]` typed: if the caret's paragraph is `[`, `[ ` or `[x` up to
; it, that becomes a box. CF = 1 it was not one (type the `]`)
sne_mkbox:
    push ax
    push bx
    push cx
    push si
    push di
    call sn_tend
    cmp [sne_car], ax
    jbe .no                     ; the title has no boxes
    call sne_pstart             ; AX = the paragraph
    mov bx, ax
    call sn_cur
    add si, bx
    call sn_ismark
    jnc .no                     ; a box already
    mov cx, [sne_car]
    sub cx, bx                  ; what is typed before the caret
    cmp byte [si], '['
    jne .no
    mov dl, SN_BOX0
    cmp cx, 1
    je .yes
    cmp cx, 2
    jne .no
    mov al, [si + 1]
    cmp al, ' '
    je .yes
    mov dl, SN_BOX1
    or al, 20h
    cmp al, 'x'
    jne .no
.yes:
    push dx
    mov al, 0
    call sne_pre
    call sn_del                 ; the `[`... (BX, CX)
    pop dx
    mov al, dl
    call sn_ins                 ; ...and the marker in its place - the pool
    mov [sne_car], bx           ; had room for it a moment ago
    inc word [sne_car]
    mov byte [sne_jb], 1
    call sne_after
    clc
    jmp short .x
.no:
    stc
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sne_cbox - Ctrl+T, the Note menu's Check Box: the caret's paragraph a box,
; or a box no more
sne_cbox:
    cmp byte [STIK_PAY + SNP_N], 0
    je .r
    push ax
    push bx
    push cx
    push si
    push di
    call sne_lay
    jc .x
    call sn_tend
    cmp [sne_car], ax
    jbe .x
    mov al, 0
    call sne_pre
    call sne_pstart
    mov bx, ax
    call sn_cur
    add si, bx
    push ax                     ; the whole paragraph re-wraps at the other
    call sne_widths             ; width and its first line gains or loses
    call sn_vfind               ; the box: redraw from that line, not the
    mov [sne_l0], ax            ; caret's
    pop ax
    call sn_ismark
    jc .add
    mov cx, 1                   ; a box: the marker out
    call sn_del
    dec word [sne_car]
    jmp short .a
.add:
    mov al, SN_BOX0
    call sn_ins
    jc .x
    inc word [sne_car]
.a:
    call sne_after
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.r:
    ret

; sne_kenter - a new line: in a box paragraph a new box - or, on an EMPTY
; box, the box away (the list ends); in the title the title splits
sne_kenter:
    push ax
    push bx
    push cx
    push si
    push di
    call sn_tend
    cmp [sne_car], ax
    ja .body
    mov al, 2
    call sne_pre
    jmp short .lf
.body:
    mov al, 0
    call sne_pre
    call sne_pstart
    mov bx, ax
    call sn_cur
    add si, bx
    call sn_ismark
    jc .lf
    mov ax, [sne_car]           ; a box: empty?
    dec ax
    cmp ax, bx
    jne .nb
    call sn_cur
    add si, [sne_car]
    cmp si, di
    je .end
    cmp byte [si], 10
    jne .nb
.end:
    mov cx, 1                   ; the empty box goes: the list is over
    call sn_del
    dec word [sne_car]
    jmp short .a
.nb:
    call sn_used                ; a new box: room for two
    cmp ax, SN_POOL - 1
    jb .two
    mov si, sn_s_pool
    call sn_toast
    jmp short .x
.two:
    mov bx, [sne_car]
    mov al, 10
    call sn_ins
    inc bx
    mov al, SN_BOX0
    call sn_ins
    add word [sne_car], 2
    jmp short .a
.lf:
    mov bx, [sne_car]
    mov al, 10
    call sn_ins
    jc .x
    inc word [sne_car]
.a:
    call sne_after
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sne_kbs - Backspace: the box away after its marker, a line joined to the one
; before at its start, else the character before the caret
sne_kbs:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, [sne_car]
    or bx, bx
    jz .x
    call sn_cur
    add si, bx
    dec si                      ; the byte before the caret
    call sn_ismark
    jc .nm
    mov al, 0                   ; the box away
    call sne_pre
    jmp short .one
.nm:
    cmp byte [si], 10
    jne .ch
    dec bx                      ; a join: is it onto the title?
    call sn_tend
    cmp ax, bx
    jne .jb
    call sne_joinok             ; ...which must still fit its line
    jc .x
    mov al, 2
    call sne_pre
    jmp short .del
.jb:
    mov al, 0
    call sne_pre
    jmp short .del
.ch:
    dec bx
    call sn_tend
    cmp ax, bx
    mov al, 0
    jbe .p
    mov al, 1                   ; inside the title
.p:
    call sne_pre
    jmp short .del
.one:
    dec bx
.del:
    mov cx, 1
    call sn_del
    mov [sne_car], bx
    call sne_after
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sne_kdel - Delete: the character at the caret - a LF with the next line's
; marker, if it has one
sne_kdel:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, [sne_car]
    call sn_cur
    cmp bx, cx
    jae .x
    add si, bx
    mov cx, 1
    cmp byte [si], 10
    jne .ch
    inc si
    call sn_ismark
    jc .lf
    inc cx                      ; ...and the marker after it
.lf:
    call sn_tend
    cmp ax, bx
    jne .jb
    push cx
    call sne_joinok
    pop cx
    jc .x
    mov al, 2
    call sne_pre
    jmp short .del
.jb:
    mov al, 0
    call sne_pre
    jmp short .del
.ch:
    call sn_tend
    cmp ax, bx
    mov al, 0
    jbe .p
    mov al, 1
.p:
    call sne_pre
.del:
    call sn_del
    call sne_after
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sne_joinok - may line 1 join the title? CF = 1 no (the toast says why): the
; two together must fit the title's one line
sne_joinok:
    push ax
    push bx
    push cx
    push si
    push di
    call sn_tend
    mov bx, ax                  ; the title's length
    call sn_cur
    add si, ax
    inc si                      ; line 1
    cmp si, di
    jae .ok
    cmp byte [si], SN_BOX1
    ja .s
    cmp byte [si], 0
    je .s
    inc si                      ; its marker would go
.s:
    cmp si, di
    jae .ok
    cmp byte [si], 10
    je .ok
    inc si
    inc bx
    jmp short .s
.ok:
    mov al, [sne_tw]
    xor ah, ah
    cmp bx, ax                  ; a WORD: the two lines may be 4 KB together
    ja .no
    clc
    jmp short .x
.no:
    mov si, sne_s_tlong
    call sn_toast
    stc
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the presses (SI = the editor, CX, DX the press, lock held) and the release
; -----------------------------------------------------------------------------
sne_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [sne_press], SNE_H_NONE
    call sn_abdismiss
    jc .x
    call sne_lay
    jc .x
    sub cx, [sne_ox]
    sub dx, [sne_oy]
    call sne_geo
    mov al, [di + SNEG_TBH]
    xor ah, ah
    cmp dx, ax
    jl .bar
    cmp dx, [sne_pb]
    jge .x                      ; the status: nothing
    cmp cx, SNE_LW
    jl .list
    cmp cx, [sne_sbx]
    jl .page
    call sne_sbclick
    jmp short .x
.page:
    call sne_pgclick
    jmp short .x
.list:
    call sne_lclick
    jmp short .x
.bar:
    call sne_bhit               ; AL = the button, SNE_H_*
    or al, al
    jz .x
    mov [sne_press], al
    cmp al, SNE_H_DESK
    je .x                       ; the box acts at the release, unlit
    cmp al, SNE_H_DEL
    jne .lit
    cmp byte [STIK_PAY + SNP_N], 0
    jne .lit
    mov byte [sne_press], SNE_H_NONE   ; greyed
    jmp short .x
.lit:
    mov [sne_lit], al
    call sne_rbar
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_onmup - W_ONMOUSEUP (CX, DX the release, screen): a button pressed acts
; when the release is still on it
sne_onmup:
    push ax
    push bx
    push cx
    push dx
    mov al, [sne_press]
    mov byte [sne_press], SNE_H_NONE
    or al, al
    jz .x
    cmp byte [sne_lit], 0
    je .h
    mov byte [sne_lit], 0
    call sne_rbar
.h:
    call sne_lay
    jc .x
    push ax
    sub cx, [sne_ox]
    sub dx, [sne_oy]
    call sne_bhit
    pop bx
    cmp al, bl
    jne .x                      ; slid off
    cmp al, SNE_H_NEW
    jne .d
    call sn_new
    jmp short .x
.d:
    cmp al, SNE_H_DEL
    jne .k
    call sn_askdel
    jmp short .x
.k:
    mov al, [STIK_PAY + SNP_FLAGS]
    and al, SNF_DESK
    xor al, SNF_DESK
    call sn_setdesk
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sne_bhit - content CX, DX in the bar: AL = the button there, SNE_H_*
sne_bhit:
    push bx
    push di
    call sne_geo
    xor ax, ax
    mov bl, [di + SNEG_BY]
    xor bh, bh
    cmp dx, bx
    jl .x
    add bl, [di + SNEG_BH]
    cmp dx, bx
    jge .x
    mov al, SNE_H_NEW
    cmp cx, SNE_NEWX
    jl .no
    cmp cx, SNE_NEWX + SNE_NEWW
    jl .x
    mov al, SNE_H_DEL
    cmp cx, SNE_DELX
    jl .no
    cmp cx, SNE_DELX + SNE_DELW
    jl .x
    mov al, SNE_H_DESK
    cmp cx, SNE_CBX - 2
    jl .no
    cmp cx, SNE_CBTX + 10 * 8
    jl .x
.no:
    xor ax, ax
.x:
    pop di
    pop bx
    ret

; sne_lclick - a press on the list (content CX, DX): that note, current
sne_lclick:
    call sne_geo
    mov al, [di + SNEG_TBH]
    add al, [di + SNEG_L0]
    xor ah, ah
    sub al, [di + SNEG_BART]
    sbb ah, 0
    sub dx, ax
    jl .r
    mov ax, dx
    div byte [di + SNEG_LP]
    cmp al, [sne_nl]
    jae .r
    add al, [sne_ltop]
    jmp sn_goto
.r:
    ret

; sne_pgclick - a press on the page (content CX, DX): a box ticked, or the
; caret placed
sne_pgclick:
    cmp byte [STIK_PAY + SNP_N], 0
    je .r
    call sne_geo
    mov al, [di + SNEG_TBH]
    add al, [di + SNEG_B0]
    sub al, [di + SNEG_LOFF]
    xor ah, ah
    cmp dx, ax
    jge .body
    mov ax, -1                  ; above the body: the title
    call sne_xcol
    push ax
    call sne_loc
    mov [sne_l0], ax
    call sn_tend
    pop bx
    cmp bx, ax
    jbe .t
    mov bx, ax
.t:
    mov [sne_car], bx
    jmp short .moved
.body:
    sub dx, ax
    mov ax, dx
    div byte [di + SNEG_P]
    xor ah, ah
    cmp ax, [sne_nv]
    jb .on
    jmp .r                      ; the rows below the last line on show
.on:
    add ax, [sne_top]
    push ax
    call sne_nt
    pop bx
    or ax, ax
    jz .r
    cmp bx, ax
    jb .ln
    mov bx, ax
    dec bx
.ln:
    mov ax, bx
    call sne_widths
    call sn_vseek
    cmp byte [it_k], 0          ; the box?
    je .car
    cmp byte [it_f], 0
    je .car
    cmp cx, SNE_BX - 2
    jl .car
    cmp cx, SNE_BTX - 2
    jge .car
    push ax
    call sn_vtick               ; SI = the marker
    call sn_edit
    pop ax
    mov dx, ax
    call sne_rlines
    mov byte [snp_dirty], 1     ; the pad at the next beat
    jmp short .r
.car:
    push ax
    call sne_xcol               ; AL = the column under the press (the walk
    mov [sne_goal], al          ; is on the line)
    call sne_loc
    mov [sne_l0], ax
    pop ax
    call sne_lpos
.moved:
    mov byte [sne_vert], 0
    mov byte [sne_jb], 0
    call sn_norm
    call sne_fix
    jnc .two
    jmp sne_rpane
.two:
    jmp sne_rcar2
.r:
    ret

; sne_xcol - AL = the column content x CX lands on (the nearer cell edge) on
; the walk's line - or, AX = -1, on the title. The rest kept
sne_xcol:
    push bx
    push cx
    mov bx, SNE_TX
    cmp ax, -1
    je .t
    cmp byte [it_k], 0
    je .t
    mov bx, SNE_BTX
.t:
    sub cx, bx
    add cx, 4
    jns .p
    xor cx, cx
.p:
    shr cx, 1
    shr cx, 1
    shr cx, 1
    cmp cx, 255
    jbe .c
    mov cx, 255
.c:
    mov ax, cx
    pop cx
    pop bx
    ret

; sne_sbclick - a press on the scroll bar (content CX, DX): a line by the
; arrows, a page by the track
sne_sbclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, dx                  ; the press's row
    call sne_thumb              ; AX = the track's first row, DX the down
    jc .x                       ; box's, BX..CX the thumb: nothing to scroll
    mov si, dx
    mov dx, [sne_top]
    cmp di, ax
    jl .up
    cmp di, si
    jge .dn
    cmp di, bx
    jl .pgup
    cmp di, cx
    jg .pgdn
    jmp short .x                ; on the thumb (no drag here)
.up:
    dec dx
    jmp short .set
.dn:
    inc dx
    jmp short .set
.pgup:
    sub dx, [sne_nv]
    jmp short .set
.pgdn:
    add dx, [sne_nv]
.set:
    or dx, dx
    jns .s1
    xor dx, dx
.s1:
    push dx
    call sne_nt
    sub ax, [sne_nv]
    pop dx
    cmp dx, ax
    jle .s2
    mov dx, ax
.s2:
    cmp dx, [sne_top]
    je .x
    mov [sne_top], dx
    call sne_rbody
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sne_s_new:  db 'New', 0
sne_s_del:  db 'Delete', 0
sne_s_desk: db 'On desktop', 0
sne_s_notes: db 'Notes', 0
sne_s_click: db 'Click New to add one.', 0
sne_s_nonotes: db 'No notes', 0
sne_s_notes1: db ' note', 0
sne_s_notesn: db ' notes', 0
sne_s_saved: db 'Saved', 0
sne_s_edited: db 'Edited', 0
sne_s_nosave: db 'Not saved', 0
sne_s_tlong: db 'The title would not fit', 0

; --- the editor's bit records ---------------------------------------------------
sn_doc12:   db 12               ; a page with its corner turned
            dw 0FC00h, 08600h, 08500h, 08780h, 08080h, 08080h
            dw 08080h, 08080h, 08080h, 08080h, 08080h, 0FF80h
sn_doc9:    db 9
            dw 0F800h, 08C00h, 08E00h, 08200h, 08200h
            dw 08200h, 08200h, 08200h, 0FE00h
sn_up11:    db 11
            dw 00600h, 00900h, 01080h, 02040h, 04020h, 0F0F0h
            dw 01080h, 01080h, 01080h, 01080h, 01F80h
sn_dn11:    db 11
            dw 01F80h, 01080h, 01080h, 01080h, 01080h, 0F0F0h
            dw 04020h, 02040h, 01080h, 00900h, 00600h
sn_up8:     db 8
            dw 00C00h, 01200h, 02100h, 04080h, 0E1C0h, 02100h, 02100h, 03F00h
sn_dn8:     db 8
            dw 03F00h, 02100h, 02100h, 0E1C0h, 04080h, 02100h, 01200h, 00C00h
