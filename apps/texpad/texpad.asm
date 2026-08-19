; =============================================================================
; TeXPad - black-and-white TeX pad for os8088
;
; Source on the left, typeset preview on the right. Letter default, other
; sizes on the Page menu. Facing/opposing, gutter, units, page numbers.
; System 8x8 monofont, text + tables. Export is PDF 1.4 / PostScript Level 1.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'TEXPAD', tp_entry, 3

    OS88_ICON16
    ; mask: portrait page
    dw 0x3FFC
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x3FFC
    ; data: page + T + three text rules
    dw 0x0000
    dw 0x3FFC
    dw 0x2004
    dw 0x27E4
    dw 0x2124
    dw 0x2124
    dw 0x2004
    dw 0x27E4
    dw 0x2004
    dw 0x27E4
    dw 0x2004
    dw 0x27E4
    dw 0x2004
    dw 0x2004
    dw 0x3FFC
    dw 0x0000
    OS88_ICON16_END

    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'TEX'
    OS88_ASSOC16_END

TP_WIN_W      equ 628
TP_WIN_H      equ 400
TP_BAR_H      equ 22
TP_STAT_H     equ 14
TP_SPLIT0     equ 200
TP_SB_W       equ 10
TP_SRC_KB     equ 8
TP_SRC_MAX    equ 8190
TP_EXP_KB     equ 24
TP_EXP_MAX    equ 24576
TP_MAX_RUNS   equ 480
TP_MAX_BOXES  equ 64
TP_MAX_PAGES  equ 16
TP_TEXT_MAX   equ 5120
TP_RUN_SZ     equ 10
TP_BOX_SZ     equ 10
TP_ARG_MAX    equ 180
TP_LINE_MAX   equ 148
TP_CLINE_MAX  equ 120
TP_STY_MAX    equ 8
TP_ENV_MAX    equ 6
TP_COL_MAX    equ 6

; -----------------------------------------------------------------------------
tp_entry:
    cld
    push si
    mov si, tp_tpl
    call OSAPI_WM_CREATE
    jc .out
    push si
    mov si, tp_menus
    call OSAPI_MENU_SET
    pop si
    mov [tp_win], bx
    push ax                     ; SPEC.md 13.7/13.8.1: the bar's seven fire on
    mov ax, tp_onup             ; the RELEASE and follow the pointer between
    call OSAPI_WM_ONMOUSEUP     ; the edges. Not template words, so they are
    mov ax, tp_ondrag           ; set after wm_create like MENU_SET above
    call OSAPI_WM_ONDRAG
    pop ax
    mov byte [tp_bdown], 0
    mov al, 1
    call OSAPI_WM_SIZABLE
    mov al, 1
    call OSAPI_WM_SNAP
    push si
    mov si, tp_onabout
    call OSAPI_ABOUT_SET
    pop si
    mov byte [tp_needld], 1
    mov byte [tp_margin], 1
    mov byte [tp_pad], 1
    mov byte [tp_pstyle], 1     ; footer numbers
    mov byte [tp_psize], 0      ; Letter
    mov byte [tp_bind], 0       ; single
    mov byte [tp_gutter], 1     ; 18pt
    mov byte [tp_units], 0      ; inches
    mov byte [tp_fsize], 12
    mov word [tp_split], TP_SPLIT0
    call tp_claim_src
    jc .out
    ; RAM seed only when not opened on a .TEX. No floppy, no typeset
    ; under the loader lock (that froze the desktop).
    mov byte [tp_needld], 0
    call tp_note_arg
    cmp byte [tp_needld], 0
    jne .pend
    call tp_seed
    jmp .ready
.pend:
    mov word [tp_srclen], 0
    mov word [tp_cur], 0
.ready:
    mov byte [tp_needttl], 1
    mov byte [tp_needset], 1
    mov byte [tp_setok], 0
    clc                         ; entry CF=1 means launch failed
.out:
    pop si
    ret

; Copy ARG_FILE name only. Do not READ or typeset here.
tp_note_arg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_ARG_FILE
    jc .o
    mov [tp_dir], dx
    mov [tp_drv], bl
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, tp_fname
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.named:
    push ds
    pop es
    call tp_is_tex
    jc .o
    mov byte [tp_needld], 1
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; Load a pending ARG_FILE. Call from a key/click, not paint.
tp_deferred_ld:
    cmp byte [tp_needld], 0
    je .o
    mov byte [tp_needld], 0
    push ax
    push bx
    push dx
    mov dx, [tp_dir]
    mov bl, [tp_drv]
    call OSAPI_FILE_GOTO
    call tp_load
    pop dx
    pop bx
    pop ax
    jnc .set
    jmp .ttl
.set:
    call tp_typeset
.ttl:
    mov byte [tp_needttl], 1
    call tp_retitle
    ; A load changes the WHOLE window - the byte count in the status strip and
    ; the page count in the top bar as much as the text - and the key handler
    ; that got us here is on its way to repainting the source pane alone. Say
    ; so, and let it repaint everything ONCE instead: launched on a .TEX, the
    ; first keypress otherwise left "0/8190" under a document plainly not
    ; empty, until something unrelated forced a full repaint.
    mov byte [tp_ldfull], 1
.o:
    ret

tp_claim_src:
    cmp word [tp_srcseg], 0
    jne .ok
    mov ax, TP_SRC_KB
    call OSAPI_MEM_CLAIM
    jc .bad
    mov [tp_srcseg], dx
    mov word [tp_srclen], 0
.ok:
    clc
    ret
.bad:
    stc
    ret

tp_is_tex:
    push ax
    push si
    mov si, tp_fname
.l:
    mov al, [si]
    or al, al
    jz .no
    cmp al, '.'
    je .dot
    inc si
    jmp .l
.dot:
    inc si
    mov al, [si]
    or al, 0x20
    cmp al, 't'
    jne .no
    mov al, [si+1]
    or al, 0x20
    cmp al, 'e'
    jne .no
    mov al, [si+2]
    or al, 0x20
    cmp al, 'x'
    jne .no
    pop si
    pop ax
    clc
    ret
.no:
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
tp_paint:
    cmp byte [tp_needttl], 0
    je .go
    mov byte [tp_needttl], 0
    call tp_retitle
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [tp_ox], ax
    mov [tp_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM
    mov [tp_cw], cx
    mov [tp_ch], dx
    call tp_clamp_split
    call tp_fill
    cmp byte [tp_abouton], 0
    je .ui
    call tp_draw_about
    jmp .grow
.ui:
    call tp_layout
    call tp_draw_bar
    call tp_draw_source
    call tp_draw_preview
    call tp_draw_stat
.grow:
    mov bx, [tp_win]
    or bx, bx
    jz .out
    call OSAPI_WM_GROW
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_fill:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]
    mov bx, [tp_oy]
    mov cx, ax
    add cx, [tp_cw]
    dec cx
    mov dx, bx
    add dx, [tp_ch]
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_clamp_split:
    push ax
    push bx
    mov ax, [tp_split]
    cmp ax, 120
    jae .lo
    mov ax, 120
.lo:
    mov bx, [tp_cw]
    sub bx, 140
    cmp bx, 120
    jge .hi
    mov bx, 120
.hi:
    cmp ax, bx
    jbe .ok
    mov ax, bx
.ok:
    mov [tp_split], ax
    pop bx
    pop ax
    ret

tp_layout:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [tp_ox]
    add ax, 4
    mov bx, [tp_oy]
    add bx, 2
    mov dx, bx
    add dx, 16
    mov di, tp_r_set
    mov cx, ax
    add cx, 32
    call tp_setrect
    mov ax, cx
    add ax, 4
    mov di, tp_r_cls
    mov cx, ax
    add cx, 32
    call tp_setrect
    mov ax, cx
    add ax, 4
    mov di, tp_r_mar
    mov cx, ax
    add cx, 32
    call tp_setrect
    mov ax, cx
    add ax, 4
    mov di, tp_r_gut
    mov cx, ax
    add cx, 32
    call tp_setrect
    mov ax, cx
    add ax, 4
    mov di, tp_r_pad
    mov cx, ax
    add cx, 32
    call tp_setrect
    mov ax, cx
    add ax, 4
    mov di, tp_r_prev
    mov cx, ax
    add cx, 24
    call tp_setrect
    mov ax, cx
    add ax, 4
    mov di, tp_r_next
    mov cx, ax
    add cx, 24
    call tp_setrect
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_setrect:
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
    ret

; Paper size in points from tp_psize: Letter / Legal / A4 / A5.
tp_pagesz:
    push ax
    mov ax, 612
    mov word [tp_ph], 792
    cmp byte [tp_psize], 1
    je .legal
    cmp byte [tp_psize], 2
    je .a4
    cmp byte [tp_psize], 3
    je .a5
    jmp .ok
.legal:
    mov word [tp_ph], 1008
    jmp .ok
.a4:
    mov ax, 595
    mov word [tp_ph], 842
    jmp .ok
.a5:
    mov ax, 420
    mov word [tp_ph], 595
.ok:
    mov [tp_pw], ax
    pop ax
    ret

; Left / right / top / bot / text width for [tp_page].
; Facing: odd (recto) gutter on the inner left; even (verso) inner right.
; Opposing flips that. Single keeps equal outer margins.
tp_geom:
    push ax
    push bx
    push cx
    push dx
    call tp_pagesz
    mov ax, 72
    cmp byte [tp_margin], 0
    jne .m1
    mov ax, 54
    jmp .ms
.m1:
    cmp byte [tp_margin], 2
    jne .ms
    mov ax, 108
.ms:
    mov cx, ax                  ; outer margin
    xor bx, bx
    cmp byte [tp_gutter], 1
    jne .g2
    mov bx, 18
    jmp .gs
.g2:
    cmp byte [tp_gutter], 2
    jne .gs
    mov bx, 36
.gs:
    cmp byte [tp_bind], 0
    je .single
    mov dx, [tp_page]
    and dx, 1
    cmp byte [tp_bind], 2
    jne .side
    xor dx, 1
.side:
    or dx, dx
    jnz .verso
    mov ax, cx
    add ax, bx
    mov [tp_left], ax
    mov [tp_tleft], ax
    mov ax, [tp_pw]
    sub ax, cx
    mov [tp_right], ax
    jmp .tw
.verso:
    mov [tp_left], cx
    mov [tp_tleft], cx
    mov ax, [tp_pw]
    sub ax, cx
    sub ax, bx
    mov [tp_right], ax
    jmp .tw
.single:
    mov [tp_left], cx
    mov [tp_tleft], cx
    mov ax, [tp_pw]
    sub ax, cx
    mov [tp_right], ax
.tw:
    mov ax, [tp_right]
    sub ax, [tp_left]
    mov [tp_tw], ax
    mov ax, [tp_ph]
    sub ax, cx
    cmp byte [tp_pstyle], 2
    jne .th
    sub ax, 14
.th:
    mov [tp_ttop], ax
    mov ax, cx
    add ax, 18
    cmp byte [tp_pstyle], 1
    je .botx
    cmp byte [tp_pstyle], 3
    je .botx
    jmp .tb
.botx:
    add ax, 8
.tb:
    mov [tp_tbot], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Fit the sheet so 8 screen pixels = [tp_fsize] points (12pt default).
; Shrinking the whole Letter page into the pane left 8px glyphs looking
; twice as big as printed 12pt and only ~20 characters on a line.
tp_fit_sheet:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tp_ppage]
    mov [tp_page], ax
    call tp_geom
    mov bl, [tp_fsize]
    or bl, bl
    jnz .fs
    mov bl, 12
.fs:
    xor bh, bh
    mov cx, bx                  ; CX = pt size
    mov ax, [tp_pw]
    mov dx, ax
    shl ax, 1
    shl ax, 1
    shl ax, 1                   ; *8
    xor dx, dx
    div cx
    mov bx, ax                  ; BX = sheet w
    mov ax, [tp_ph]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    xor dx, dx
    div cx
    mov cx, ax                  ; CX = sheet h
    ; horizontal: center if it fits, else flush left
    mov ax, [tp_px2]
    sub ax, [tp_px1]
    sub ax, 8
    cmp bx, ax
    jae .hfull
    sub ax, bx
    sar ax, 1
    add ax, [tp_px1]
    jmp .hx
.hfull:
    mov ax, [tp_px1]
    add ax, 4
.hx:
    mov [tp_sx1], ax
    add ax, bx
    mov [tp_sx2], ax
    ; vertical: top of pane, then scroll
    mov ax, [tp_py1]
    add ax, 4
    sub ax, [tp_pscroll]
    mov [tp_sy1], ax
    add ax, cx
    mov [tp_sy2], ax
    mov ax, [tp_left]
    call tp_mapx
    mov [tp_tx1], ax
    mov ax, [tp_right]
    call tp_mapx
    mov [tp_tx2], ax
    mov ax, [tp_ttop]
    call tp_mapy
    mov [tp_ty1], ax
    mov ax, [tp_tbot]
    call tp_mapy
    mov [tp_ty2], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = PDF x -> AX = screen x
tp_mapx:
    push bx
    push cx
    push dx
    mov cx, [tp_sx2]
    sub cx, [tp_sx1]
    mul cx
    mov bx, [tp_pw]
    or bx, bx
    jnz .d
    mov bx, 612
.d:
    div bx
    add ax, [tp_sx1]
    pop dx
    pop cx
    pop bx
    ret

; AX = PDF y (from bottom) -> AX = screen y
tp_mapy:
    push bx
    push cx
    push dx
    mov bx, [tp_ph]
    sub bx, ax
    mov ax, bx
    mov cx, [tp_sy2]
    sub cx, [tp_sy1]
    mul cx
    mov bx, [tp_ph]
    or bx, bx
    jnz .d
    mov bx, 792
.d:
    div bx
    add ax, [tp_sy1]
    pop dx
    pop cx
    pop bx
    ret

; Page, gutter band, text-area frame.
tp_draw_sheet:
    push ax
    push bx
    push cx
    push dx
    ; desk
    mov ax, [tp_px1]
    inc ax
    mov bx, [tp_py1]
    inc bx
    mov cx, [tp_px2]
    dec cx
    mov dx, [tp_py2]
    dec dx
    call OSAPI_GFX_FILL_GRAY
    ; paper, clipped to the preview pane
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_sx1]
    mov bx, [tp_sy1]
    mov cx, [tp_sx2]
    mov dx, [tp_sy2]
    call tp_clip_pane
    jc .gutter
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_sx1]
    mov bx, [tp_sy1]
    mov cx, [tp_sx2]
    mov dx, [tp_sy2]
    call tp_clip_pane
    jc .gutter
    call OSAPI_GFX_FRAME
.gutter:
    ; gutter
    cmp byte [tp_bind], 0
    je .txt
    xor ax, ax
    cmp byte [tp_gutter], 1
    jne .g2
    mov ax, 18
    jmp .gw
.g2:
    cmp byte [tp_gutter], 2
    jne .txt
    mov ax, 36
.gw:
    mov bx, [tp_sx2]
    sub bx, [tp_sx1]
    mul bx
    mov bx, [tp_pw]
    or bx, bx
    jnz .gd
    mov bx, 612
.gd:
    div bx
    cmp ax, 4
    jae .gok
    mov ax, 4
.gok:
    mov cx, ax                  ; gutter px
    mov al, [tp_ppage]
    and al, 1
    cmp byte [tp_bind], 2
    jne .sd
    xor al, 1
.sd:
    or al, al
    jnz .rg
    ; left (recto / inner)
    mov ax, [tp_sx1]
    inc ax
    mov bx, [tp_sy1]
    inc bx
    push cx
    add cx, ax
    dec cx
    mov dx, [tp_sy2]
    dec dx
    call OSAPI_GFX_FILL_GRAY
    pop cx
    jmp .txt
.rg:
    mov ax, [tp_sx2]
    sub ax, cx
    push ax
    mov bx, [tp_sy1]
    inc bx
    mov cx, [tp_sx2]
    dec cx
    mov dx, [tp_sy2]
    dec dx
    call OSAPI_GFX_FILL_GRAY
    pop ax
.txt:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Clip AX/BX/CX/DX to the preview pane. CF=1 if the rect is empty.
tp_clip_pane:
    push si
    mov si, [tp_px1]
    inc si
    cmp ax, si
    jge .x1
    mov ax, si
.x1:
    mov si, [tp_px2]
    dec si
    cmp cx, si
    jle .x2
    mov cx, si
.x2:
    cmp cx, ax
    jl .empty
    mov si, [tp_py1]
    inc si
    cmp bx, si
    jge .y1
    mov bx, si
.y1:
    mov si, [tp_py2]
    dec si
    cmp dx, si
    jle .y2
    mov dx, si
.y2:
    cmp dx, bx
    jl .empty
    pop si
    clc
    ret
.empty:
    pop si
    stc
    ret

; Status: size, margin, bind, gutter, numbers, body size, source used.
tp_fmt_stat:
    push ax
    push si
    push es
    push ds
    pop es
    mov si, tp_s_letter
    cmp byte [tp_psize], 0
    je .nm
    mov si, tp_s_legal
    cmp byte [tp_psize], 1
    je .nm
    mov si, tp_s_a4s
    cmp byte [tp_psize], 2
    je .nm
    mov si, tp_s_a5s
.nm:
    call tp_cpat
    mov al, ' '
    stosb
    cmp byte [tp_units], 0
    jne .mm
    mov si, tp_s_u075
    cmp byte [tp_margin], 0
    je .mu
    mov si, tp_s_u10
    cmp byte [tp_margin], 1
    je .mu
    mov si, tp_s_u15
    jmp .mu
.mm:
    mov si, tp_s_m19
    cmp byte [tp_margin], 0
    je .mu
    mov si, tp_s_m25
    cmp byte [tp_margin], 1
    je .mu
    mov si, tp_s_m38
.mu:
    call tp_cpat
    mov al, ' '
    stosb
    mov si, tp_s_sgl
    cmp byte [tp_bind], 0
    je .bd
    mov si, tp_s_fac
    cmp byte [tp_bind], 1
    je .bd
    mov si, tp_s_opp
.bd:
    call tp_cpat
    mov al, ' '
    stosb
    cmp byte [tp_units], 0
    jne .gm
    mov si, tp_s_gi0
    cmp byte [tp_gutter], 0
    je .gu
    mov si, tp_s_gi1
    cmp byte [tp_gutter], 1
    je .gu
    mov si, tp_s_gi2
    jmp .gu
.gm:
    mov si, tp_s_gm0
    cmp byte [tp_gutter], 0
    je .gu
    mov si, tp_s_gm1
    cmp byte [tp_gutter], 1
    je .gu
    mov si, tp_s_gm2
.gu:
    call tp_cpat
    mov al, ' '
    stosb
    mov si, tp_s_n0
    cmp byte [tp_pstyle], 0
    je .ns
    mov si, tp_s_n1
    cmp byte [tp_pstyle], 1
    je .ns
    mov si, tp_s_n2
    cmp byte [tp_pstyle], 2
    je .ns
    mov si, tp_s_n3
.ns:
    call tp_cpat
    mov al, ' '
    stosb
    mov al, [tp_fsize]
    xor ah, ah
    call tp_u16_di
    mov si, tp_s_pt
    call tp_cpat
    mov al, ' '
    stosb
    mov ax, [tp_srclen]
    call tp_u16_di
    mov al, '/'
    stosb
    mov ax, TP_SRC_MAX
    call tp_u16_di
    mov byte [di], 0
    pop es
    pop si
    pop ax
    ret

; --- SPEC.md 13.8: the top row acts on the RELEASE and draws itself down ----
; The seven rects were already one description shared by the drawing and
; os88ui_bhit; what was missing is that the press ACTED. None of these has a
; safe prefix action - Setup opens a window, the four cyclers change a
; document's page setup, and the arrows move a page - so all seven want the
; release (SPEC.md 13.6).
;
; tp_btn1 is the per-control painter the down state needs: redrawing the whole
; bar on every press and every slide would re-letter the status line beside it
; and is PERFORMANCE.md's double-draw flash.
tp_bdown:   db 0                ; which of the seven is DRAWN pressed, 0 = none
tp_btab:    dw tp_r_set, tp_r_cls, tp_r_mar, tp_r_gut, tp_r_pad
            dw tp_r_prev, tp_r_next
TP_NBTN     equ 7

tp_btn1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, OS88UI_FILL
    cmp al, [tp_bdown]
    jne .nodn
    or di, OS88UI_DOWN
.nodn:
    dec al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov bx, [tp_btab+bx]        ; BX = this button's rect
    or al, al
    jnz .b1
    mov si, tp_s_set
    jmp .draw
.b1:
    cmp al, 1
    jne .b2
    mov si, tp_s_ltr
    cmp byte [tp_psize], 0
    je .draw
    mov si, tp_s_leg
    cmp byte [tp_psize], 1
    je .draw
    mov si, tp_s_a4s
    cmp byte [tp_psize], 2
    je .draw
    mov si, tp_s_a5s
    jmp .draw
.b2:
    cmp al, 2
    jne .b3
    mov si, tp_s_sgl
    cmp byte [tp_bind], 0
    je .draw
    mov si, tp_s_fac
    cmp byte [tp_bind], 1
    je .draw
    mov si, tp_s_opp
    jmp .draw
.b3:
    cmp al, 3
    jne .b4
    mov si, tp_s_g0
    cmp byte [tp_gutter], 0
    je .draw
    mov si, tp_s_g1
    cmp byte [tp_gutter], 1
    je .draw
    mov si, tp_s_g2
    jmp .draw
.b4:
    cmp al, 4
    jne .b5
    mov si, tp_s_pc
    cmp byte [tp_pad], 0
    je .draw
    mov si, tp_s_pn
    cmp byte [tp_pad], 1
    je .draw
    mov si, tp_s_pl
    jmp .draw
.b5:
    mov si, tp_s_lt
    cmp al, 5
    je .draw
    mov si, tp_s_gt
.draw:
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tp_bhit - which of the seven is this SCREEN point in? (0 = none)
; -----------------------------------------------------------------------------
tp_bhit:
    push bx
    push si
    xor ah, ah                      ; the FULL AX indexes tp_btab below, and
    mov al, 1                       ; tp_onclick arrives with the content-left
                                    ; X still in it (SPEC.md 13.8)
.next:
    mov si, ax
    dec si
    shl si, 1
    mov bx, [tp_btab+si]
    call os88ui_bhit
    jnc .out
    inc al
    cmp al, TP_NBTN
    jbe .next
    xor al, al
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; tp_setdown - AL becomes the button drawn pressed (0 = none), and it DRAWS
; -----------------------------------------------------------------------------
tp_setdown:
    push ax
    cmp al, [tp_bdown]
    je .out
    push ax
    mov al, [tp_bdown]
    or al, al
    jz .take
    mov byte [tp_bdown], 0
    call tp_btn1
.take:
    pop ax
    mov [tp_bdown], al
    or al, al
    jz .out
    call tp_btn1
.out:
    pop ax
    ret

tp_draw_bar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, 1
.each:
    call tp_btn1
    inc al
    cmp al, TP_NBTN
    jbe .each
    call tp_fmt_bar
    mov cx, [tp_r_next+4]
    add cx, 8
    mov dx, [tp_oy]
    add dx, 6
    mov si, tp_status
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]
    mov bx, [tp_oy]
    add bx, TP_BAR_H
    dec bx
    mov cx, [tp_ox]
    add cx, [tp_cw]
    dec cx
    mov dx, bx
    call OSAPI_GFX_HLINE
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_fmt_bar:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov di, tp_status
    mov si, tp_fname
    cmp byte [si], 0
    jne .n
    mov si, tp_s_noload
.n:
    call tp_cpat
    cmp byte [tp_dirty], 0
    je .pg
    mov al, '*'
    stosb
.pg:
    mov al, ' '
    stosb
    mov ax, [tp_ppage]
    inc ax
    call tp_u16_di
    mov al, '/'
    stosb
    mov ax, [tp_npages]
    or ax, ax
    jnz .np
    inc ax
.np:
    call tp_u16_di
    cmp byte [tp_needset], 0
    je .z
    mov al, ' '
    stosb
    mov si, tp_s_stale
    call tp_cpat
.z:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop ax
    ret

tp_draw_stat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]
    mov bx, [tp_oy]
    add bx, [tp_ch]
    sub bx, TP_STAT_H
    mov cx, [tp_ox]
    add cx, [tp_cw]
    dec cx
    mov dx, bx
    call OSAPI_GFX_HLINE
    mov di, tp_nbuf
    push di
    call tp_fmt_stat
    pop si
    mov cx, [tp_ox]
    add cx, 6
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, 11
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_draw_about:
    push ax
    push cx
    push dx
    push si
    mov cx, [tp_ox]
    add cx, 16
    mov dx, [tp_oy]
    add dx, 16
    mov si, tp_s_a1
    call tp_about_line
    mov si, tp_s_a2
    call tp_about_line
    mov si, tp_s_a3
    call tp_about_line
    mov si, tp_s_a4
    call tp_about_line
    mov si, tp_s_a5
    call tp_about_line
    mov si, tp_s_a6
    call tp_about_line
    mov si, tp_s_a7
    call tp_about_line
    mov si, tp_s_a8
    call tp_about_line
    mov si, tp_s_a9
    call tp_about_line
    mov si, tp_s_a10
    call tp_about_line
    mov si, tp_s_a11
    call tp_about_line
    pop si
    pop dx
    pop cx
    pop ax
    ret

tp_about_line:
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    add dx, 14
    ret

; The internal "paint the whole window" entry, as against tp_paint, which is
; the WM's callback and takes the window in SI the way the WM passes it.
;
; SI IS RELOADED FROM [tp_win] HERE, and that is the whole reason this exists
; as more than a jmp: an event handler reaches a redraw through the editing
; routines, and tp_go_left / tp_up / tp_down / tp_sol / tp_home / tp_end all
; RETURN a source offset in SI. So SI at the point of the call is a byte
; offset into the document, not a window - and tp_paint asks WM_CONTENT where
; that "window" is and paints the answer, which put a second set of chrome
; over the menu bar. Every caller was correct about its own registers; the
; contract was the thing that could not survive being called from two places.
tp_redraw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [tp_win]
    or si, si
    jz .o                       ; before WM_CREATE there is nothing to paint
    call tp_paint
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_onabout:
    xor byte [tp_abouton], 1
    call tp_redraw
    ret

; ---- panes -----------------------------------------------------------------
tp_edit_box:
    ; out: tp_ex1/ey1/ex2/ey2 inclusive
    push ax
    mov ax, [tp_ox]
    inc ax
    mov [tp_ex1], ax
    add ax, [tp_split]
    dec ax
    mov [tp_r_ssb+4], ax        ; sbar x2
    sub ax, TP_SB_W
    mov [tp_r_ssb], ax          ; sbar x1
    dec ax
    mov [tp_ex2], ax            ; text right of sbar
    mov ax, [tp_oy]
    add ax, TP_BAR_H
    inc ax
    mov [tp_ey1], ax
    mov [tp_r_ssb+2], ax
    mov ax, [tp_oy]
    add ax, [tp_ch]
    sub ax, TP_STAT_H
    dec ax
    mov [tp_ey2], ax
    mov [tp_r_ssb+6], ax
    pop ax
    ret

tp_prev_box:
    push ax
    mov ax, [tp_ox]
    add ax, [tp_split]
    add ax, 5
    mov [tp_px1], ax
    mov ax, [tp_ox]
    add ax, [tp_cw]
    dec ax
    dec ax
    mov [tp_r_psb+4], ax        ; preview sbar x2
    sub ax, TP_SB_W
    mov [tp_r_psb], ax
    dec ax
    mov [tp_px2], ax
    mov ax, [tp_oy]
    add ax, TP_BAR_H
    inc ax
    mov [tp_py1], ax
    mov [tp_r_psb+2], ax
    mov ax, [tp_oy]
    add ax, [tp_ch]
    sub ax, TP_STAT_H
    dec ax
    mov [tp_py2], ax
    mov [tp_r_psb+6], ax
    pop ax
    ret

tp_draw_source:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_edit_box
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    mov bx, [tp_ey1]
    mov cx, [tp_r_ssb+4]
    mov dx, [tp_ey2]
    call OSAPI_GFX_FRAME
    cmp byte [tp_focus], 0
    jne .nf
    dec ax
    dec bx
    inc cx
    inc dx
    call OSAPI_GFX_FRAME
.nf:
    call tp_draw_ssb
    mov ax, [tp_srcseg]
    or ax, ax
    jz .out
    mov es, ax
    mov ax, [tp_ey2]
    sub ax, [tp_ey1]
    sub ax, 4
    js .out
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_erows], ax
    mov ax, [tp_ex2]
    sub ax, [tp_ex1]
    sub ax, 6
    js .out
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_ecols], ax
    xor di, di                  ; visible row
    mov ax, [tp_vscroll]
    mov [tp_tline], ax
.row:
    mov ax, di
    cmp ax, [tp_erows]
    jae .caret
    mov ax, [tp_tline]
    call tp_line_at
    jc .caret
    ; SI = start, CX = length
    push di
    mov ax, [tp_hscroll]
    cmp cx, ax
    ja .clip
    xor cx, cx
    jmp .copy
.clip:
    add si, ax
    sub cx, ax
.copy:
    cmp cx, [tp_ecols]
    jbe .okc
    mov cx, [tp_ecols]
.okc:
    push es
    push si
    push cx
    push ds
    pop es
    mov di, tp_linebuf
    pop cx
    pop si
    push cx
    push ds
    mov ax, [tp_srcseg]
    mov ds, ax
    jcxz .nul
    rep movsb
.nul:
    pop ds
    pop cx
    pop es
    mov bx, cx
    mov byte [tp_linebuf+bx], 0
    pop di                      ; visible row
    mov cx, [tp_ex1]
    add cx, 3
    mov dx, [tp_ey1]
    add dx, 3
    mov ax, di
    mov bl, 8
    mul bl
    add dx, ax
    mov si, tp_linebuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    inc word [tp_tline]
    inc di
    jmp .row
.caret:
    cmp byte [tp_focus], 0
    jne .out
    call tp_caret_xy
    jc .out
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ; invert caret cell
    mov ax, [tp_cxp]
    mov bx, [tp_cyp]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_XOR_FILL
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = line index. CF=1 past end. SI=start, CX=len (no NL)
tp_line_at:
    push ax
    push bx
    push dx
    push es
    mov es, [tp_srcseg]
    xor si, si
    mov dx, ax
    mov bx, [tp_srclen]
.walk:
    or dx, dx
    jz .found
    cmp si, bx
    jae .miss
    mov al, [es:si]
    inc si
    cmp al, 10
    je .nl
    cmp al, 13
    jne .walk
    cmp si, bx
    jae .nl
    cmp byte [es:si], 10
    jne .nl
    inc si
.nl:
    dec dx
    jmp .walk
.found:
    cmp si, bx
    ja .miss
    push si
.len:
    cmp si, bx
    jae .e
    mov al, [es:si]
    cmp al, 10
    je .e
    cmp al, 13
    je .e
    inc si
    jmp .len
.e:
    mov cx, si
    pop si
    sub cx, si
    clc
    jmp .o
.miss:
    stc
.o:
    pop es
    pop dx
    pop bx
    pop ax
    ret

tp_count_lines:
    push bx
    push cx
    push si
    push es
    mov es, [tp_srcseg]
    xor ax, ax
    xor si, si
    mov bx, [tp_srclen]
    or bx, bx
    jz .o
    inc ax
.w:
    cmp si, bx
    jae .o
    mov cl, [es:si]
    inc si
    cmp cl, 10
    je .nl
    cmp cl, 13
    jne .w
    cmp si, bx
    jae .nl
    cmp byte [es:si], 10
    jne .nl
    inc si
.nl:
    cmp si, bx
    jae .o
    inc ax
    jmp .w
.o:
    pop es
    pop si
    pop cx
    pop bx
    ret

; caret -> tp_cxp/tp_cyp. CF=1 off screen
tp_caret_xy:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call tp_cur_lc
    ; AX=line BX=col
    mov dx, ax
    sub dx, [tp_vscroll]
    js .off
    cmp dx, [tp_erows]
    jae .off
    mov cx, bx
    sub cx, [tp_hscroll]
    js .off
    cmp cx, [tp_ecols]
    ja .off
    mov al, 8
    mul dl
    mov dx, ax
    add dx, [tp_ey1]
    add dx, 3
    mov [tp_cyp], dx
    mov ax, cx
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3
    mov [tp_cxp], ax
    clc
    jmp .o
.off:
    stc
.o:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; out AX=line BX=col of cursor
tp_cur_lc:
    push cx
    push dx
    push si
    push es
    mov es, [tp_srcseg]
    xor ax, ax
    xor bx, bx
    xor si, si
    mov dx, [tp_cur]
    cmp dx, [tp_srclen]
    jbe .w
    mov dx, [tp_srclen]
.w:
    cmp si, dx
    jae .o
    mov cl, [es:si]
    inc si
    cmp cl, 10
    je .nl
    cmp cl, 13
    jne .ch
    cmp si, dx
    jae .nl
    cmp si, [tp_srclen]
    jae .nl
    cmp byte [es:si], 10
    jne .nl
    inc si
.nl:
    inc ax
    xor bx, bx
    jmp .w
.ch:
    inc bx
    jmp .w
.o:
    pop es
    pop si
    pop dx
    pop cx
    ret

; White-fill the source pane and redraw it. Used for caret motion so
; the preview walker is not re-entered on every arrow key.
tp_redraw_src:
    push ax
    push bx
    push cx
    push dx
    call tp_edit_box
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    mov bx, [tp_ey1]
    mov cx, [tp_ex2]
    mov dx, [tp_ey2]
    call OSAPI_GFX_FILL
    call tp_draw_source
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_draw_preview:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_prev_box
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_px1]
    mov bx, [tp_py1]
    mov cx, [tp_px2]
    mov dx, [tp_py2]
    call OSAPI_GFX_FRAME
    cmp byte [tp_focus], 1
    jne .nf
    dec ax
    dec bx
    inc cx
    inc dx
    call OSAPI_GFX_FRAME
.nf:
    ; splitter
    mov ax, [tp_ox]
    add ax, [tp_split]
    inc ax
    mov bx, [tp_py1]
    mov dx, [tp_py2]
    call OSAPI_GFX_VLINE
    call tp_fit_sheet
    call tp_draw_sheet
    call tp_draw_psb
    cmp byte [tp_setok], 0
    je .empty
    call tp_paint_runs
    call tp_paint_boxes
    call tp_draw_folio
    jmp .out
.empty:
    mov cx, [tp_tx1]
    add cx, 4
    mov dx, [tp_ty1]
    add dx, 8
    mov si, tp_s_noset
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    call tp_draw_folio
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Linear preview: walk the source once, always advance SI, wrap in the pane.
; Never calls the typesetter. Safe under the paint lock.
; Page number on the sheet. Default (style 1) is bottom center.
tp_draw_folio:
    cmp byte [tp_pstyle], 0
    je .o
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    pop es
    mov di, tp_nbuf
    mov ax, [tp_ppage]
    inc ax
    call tp_u16_di
    mov byte [di], 0
    mov si, tp_nbuf
    xor cx, cx
.ln:
    cmp byte [si], 0
    je .ld
    inc cx
    inc si
    jmp .ln
.ld:
    mov ax, cx
    mov cl, 3
    shl ax, cl                  ; *8 screen
    mov bx, ax                  ; number width
    ; Y: bottom center (default), or header
    mov dx, [tp_sy2]
    sub dx, 18
    cmp byte [tp_pstyle], 2
    jne .yok
    mov dx, [tp_sy1]
    add dx, 4
.yok:
    cmp byte [tp_pstyle], 3
    je .outer
    ; center on the sheet
    mov ax, [tp_sx1]
    add ax, [tp_sx2]
    sub ax, bx
    sar ax, 1
    jmp .put
.outer:
    mov ax, [tp_ppage]
    and ax, 1
    jnz .ol
    mov ax, [tp_sx2]
    sub ax, bx
    sub ax, 6
    jmp .put
.ol:
    mov ax, [tp_sx1]
    add ax, 6
.put:
    cmp dx, [tp_py1]
    jb .done
    cmp dx, [tp_py2]
    jae .done
    add ax, 7
    and ax, 0xFFF8
    mov cx, ax
    mov si, tp_nbuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.o:
    ret

tp_scr_cw:
    ; body char width used for screen mapping
    push bx
    mov al, [tp_tsize]
    xor ah, ah
    or ax, ax
    jnz .s
    mov al, 12
.s:
    mov bl, 6
    mul bl
    mov bl, 10
    div bl
    xor ah, ah
    cmp al, 4
    jae .o
    mov al, 4
.o:
    pop bx
    ret

tp_map_xy_i:
    ; AX=x BX=y -> CX DX. uses tp_tsize, tp_tlead, tp_tleft, tp_ttop
    push ax
    push bx
    push si
    push di
    mov di, ax                  ; di = pdf x
    call tp_scr_cw
    mov si, ax                  ; si = cw
    or si, si
    jnz .cw
    mov si, 7
.cw:
    mov ax, di
    sub ax, [tp_tleft]
    jns .xok
    xor ax, ax
.xok:
    mov cx, 8
    mul cx                      ; DX:AX = dx*8
    xor dx, dx
    div si                      ; AX = cols*8/cw? wait AX = (x-left)*8/cw
    add ax, [tp_px1]
    add ax, 6
    mov cx, ax                  ; sx
    ; y: (ttop - y) * 8 / lead
    mov ax, [tp_ttop]
    sub ax, bx
    jns .yok
    xor ax, ax
.yok:
    mov si, [tp_tlead]
    or si, si
    jnz .ld
    mov si, 14
.ld:
    mov bx, 8
    mul bx
    xor dx, dx
    div si
    sub ax, [tp_pscroll]
    add ax, [tp_py1]
    add ax, 6
    mov dx, ax
    pop di
    pop si
    pop bx
    pop ax
    ret

tp_paint_runs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; Place each run at its typeset (x,y). The sheet is already scaled
    ; so 8px = body point size; packing into a fitted pane made 12pt
    ; look huge and crushed the measure to ~20 characters.
    xor si, si
.lp:
    cmp si, [tp_nruns]
    jae .o
    mov ax, si
    mov cx, TP_RUN_SZ
    mul cx
    mov bx, ax
    add bx, tp_runs
    mov al, [bx]
    xor ah, ah
    cmp ax, [tp_ppage]
    jne .n
    mov al, [bx+7]
    or al, al
    jz .n
    cmp word [bx+4], 40
    jb .n
    mov ax, [bx+2]
    call tp_mapx
    mov cx, ax
    mov ax, [bx+4]
    call tp_mapy
    mov dx, ax
    cmp dx, [tp_py1]
    jb .n
    cmp dx, [tp_py2]
    jae .n
    cmp cx, [tp_px2]
    jae .n
    cmp cx, [tp_px1]
    jae .xok
    mov cx, [tp_px1]
    add cx, 2
.xok:
    mov [tp_tmp], cx
    push si
    push bx
    push ds
    pop es
    mov si, [bx+8]
    add si, tp_text
    mov di, tp_linebuf
    xor ch, ch
    mov cl, [bx+7]
    cmp cx, 80
    jbe .cp
    mov cx, 80
.cp:
    jcxz .z
    push cx
    rep movsb
    pop cx
.z:
    mov di, tp_linebuf
    add di, cx
    mov byte [di], 0
    mov si, tp_linebuf
    mov cx, [tp_tmp]
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop bx
    test byte [bx+1], 1
    jnz .bd
    cmp byte [bx+6], 14
    jb .un
.bd:
    inc cx
    mov si, tp_linebuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_STR
.un:
    pop si
.n:
    inc si
    jmp .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_paint_boxes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    xor si, si
    mov di, [tp_nboxes]
.lp:
    cmp si, di
    jae .o
    mov ax, si
    mov cx, TP_BOX_SZ
    mul cx
    mov bx, ax
    add bx, tp_boxes
    mov al, [bx]
    xor ah, ah
    cmp ax, [tp_ppage]
    jne .n
    push si
    push di
    ; top-left: (x, y+h)
    mov ax, [bx+2]
    call tp_mapx
    mov [tp_tmp], ax
    mov ax, [bx+4]
    add ax, [bx+8]
    call tp_mapy
    mov [tp_tmp2], ax
    ; bottom-right: (x+w, y)
    mov ax, [bx+2]
    add ax, [bx+6]
    call tp_mapx
    mov cx, ax
    mov ax, [bx+4]
    call tp_mapy
    mov dx, ax
    mov ax, [tp_tmp]
    mov bx, [tp_tmp2]
    cmp ax, cx
    jle .xok
    xchg ax, cx
.xok:
    cmp bx, dx
    jle .yok
    xchg bx, dx
.yok:
    cmp ax, [tp_px2]
    jg .sk
    cmp cx, [tp_px1]
    jl .sk
    cmp bx, [tp_py2]
    jg .sk
    cmp dx, [tp_py1]
    jl .sk
    cmp ax, [tp_px1]
    jge .a1
    mov ax, [tp_px1]
.a1:
    cmp bx, [tp_py1]
    jge .b1
    mov bx, [tp_py1]
.b1:
    cmp cx, [tp_px2]
    jle .c1
    mov cx, [tp_px2]
.c1:
    cmp dx, [tp_py2]
    jle .d1
    mov dx, [tp_py2]
.d1:
    cmp bx, dx
    jne .notht
    ; 1-row box: a horizontal rule
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    mov bx, cx
    call OSAPI_GFX_HLINE
    jmp .sk
.notht:
    cmp ax, cx
    jne .rect
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    jmp .sk
.rect:
    call OSAPI_GFX_FRAME
.sk:
    pop di
    pop si
.n:
    inc si
    jmp .lp
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
tp_onkey:
    call tp_deferred_ld
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    mov bh, ah
    cmp byte [tp_abouton], 0
    je .k
    mov byte [tp_abouton], 0
    jmp .draw
.k:
    cmp bh, 0x3F
    je .kset
    cmp bl, 27
    je .kesc
    cmp bl, 19                  ; Ctrl+S
    je .ksave
    cmp bl, 15                  ; Ctrl+O
    je .kopen
    cmp bl, 14                  ; Ctrl+N
    je .knew
    cmp bl, 3                   ; Ctrl+C
    je .kcopy
    cmp bl, 22                  ; Ctrl+V
    je .kpaste
    cmp bl, 24                  ; Ctrl+X
    je .kcut
    cmp bl, '['
    je .kprev
    cmp bl, ']'
    je .knext
    cmp byte [tp_focus], 0
    je .ed
    jmp .pv
.kset:
    call tp_typeset
    jmp .draw
.kesc:
    call tp_typeset
    mov byte [tp_focus], 1
    jmp .draw
.ksave:
    call tp_save
    jmp .draw
.kopen:
    mov byte [tp_dlgmode], 0
    mov al, FDLG_OPEN
    call tp_dlg
    jmp .out
.knew:
    call tp_seed
    jmp .draw
.kcopy:
    call tp_copy_all
    jmp .draw
.kpaste:
    call tp_paste
    jmp .draw
.kcut:
    call tp_cut_line
    jmp .draw
.kprev:
    call tp_prev_page
    jmp .draw
.knext:
    call tp_next_page
    jmp .draw
.ed:
    cmp bl, 8
    je .kbs
    cmp bl, 127
    je .kdel
    cmp bh, 0x53
    je .kdel
    cmp bl, 13
    je .knl
    cmp bl, 9
    je .ktab
    cmp bh, 0x4B
    je .kleft
    cmp bh, 0x4D
    je .kright
    cmp bh, 0x48
    je .kup
    cmp bh, 0x50
    je .kdn
    cmp bh, 0x47
    je .khome
    cmp bh, 0x4F
    je .kend
    cmp bh, 0x49
    je .kpgup
    cmp bh, 0x51
    je .kpgdn
    cmp bl, 32
    jb .out
    cmp bl, 126
    ja .out
    mov al, bl
    call tp_ins
    jmp .edraw
.kbs:
    call tp_backsp
    jmp .edraw
.kdel:
    call tp_delete
    jmp .edraw
.knl:
    mov al, 10
    call tp_ins
    jmp .edraw
.ktab:
    mov al, ' '
    call tp_ins
    mov al, ' '
    call tp_ins
    jmp .edraw
.kleft:
    call tp_go_left
    jmp .edraw
.kright:
    call tp_go_right
    jmp .edraw
.kup:
    call tp_up
    jmp .edraw
.kdn:
    call tp_down
    jmp .edraw
.khome:
    call tp_home
    jmp .edraw
.kend:
    call tp_end
    jmp .edraw
.kpgup:
    call tp_pgup
    jmp .edraw
.kpgdn:
    call tp_pgdn
    jmp .edraw
.edraw:
    call tp_keep_caret
    ; the source pane alone, UNLESS a deferred load just landed - see
    ; tp_deferred_ld. Promoting is what keeps this to one draw: repainting the
    ; pane and then the window would put the source through twice, which is
    ; PERFORMANCE.md's double-draw and shows on an XT and on nothing else.
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_redraw_src
    jmp .out
.pv:
    cmp bh, 0x48
    je .psu
    cmp bh, 0x50
    je .psd
    cmp bh, 0x49
    je .kprev
    cmp bh, 0x51
    je .knext
    cmp bh, 0x4B
    je .kprev
    cmp bh, 0x4D
    je .knext
    jmp .out
.psu:
    cmp word [tp_pscroll], 0
    je .draw
    sub word [tp_pscroll], 24
    jnc .draw
    mov word [tp_pscroll], 0
    jmp .draw
.psd:
    add word [tp_pscroll], 24
    jmp .draw
.draw:
    mov byte [tp_ldfull], 0
    call tp_keep_caret
    call tp_redraw
    jmp .done
.out:
    ; a key that changed nothing still owes a repaint if a load came in under
    ; it - an unhandled key is the one path here that draws nothing at all.
    cmp byte [tp_ldfull], 0
    jne .draw
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tp_bact - do what bar button AL says (SPEC.md 13.8)
; in:  AL = 1..7; the gfx lock is held
; out: CF = 1 if the caller owes a full redraw
;
; ONE body, lifted out of tp_onclick's ladder so the press and the release
; cannot come to hold different opinions about what a button means.
; -----------------------------------------------------------------------------
tp_bact:
    cmp al, 1
    je .set
    cmp al, 2
    je .sz
    cmp al, 3
    je .bd
    cmp al, 4
    je .gut
    cmp al, 5
    je .pad
    cmp al, 6
    je .prev
    call tp_next_page
    stc
    ret
.set:
    call tp_typeset
    stc
    ret
.sz:
    mov al, [tp_psize]
    inc al
    cmp al, 4
    jb .szok
    xor al, al
.szok:
    mov [tp_psize], al
    jmp short .rely
.bd:
    mov al, [tp_bind]
    inc al
    cmp al, 3
    jb .bdok
    xor al, al
.bdok:
    mov [tp_bind], al
    jmp short .rely
.gut:
    mov al, [tp_gutter]
    inc al
    cmp al, 3
    jb .gok
    xor al, al
.gok:
    mov [tp_gutter], al
    jmp short .rely
.pad:
    mov al, [tp_pad]
    inc al
    cmp al, 3
    jb .ps
    xor al, al
.ps:
    mov [tp_pad], al
.rely:
    mov byte [tp_needset], 1
    stc
    ret
.prev:
    call tp_prev_page
    stc
    ret
.next:
    call tp_next_page
    stc
    ret
; -----------------------------------------------------------------------------
; tp_onup - W_ONMOUSEUP (SPEC.md 13.7): a bar button fires HERE
; in:  CX = x, DX = y (SCREEN), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
tp_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov di, ax
    call tp_bhit                ; still on it?
    xor ah, ah
    cmp ax, di
    je .fire
    xor al, al                  ; no: thought better of, and nothing fires
    call tp_setdown
    jmp short .out
.fire:
    mov byte [tp_bdown], 0      ; NOT tp_setdown: tp_redraw below draws the
                                ; whole window, this button upright with it,
                                ; and drawing it upright first is
                                ; PERFORMANCE.md's double-draw
    call tp_bact
    jnc .out
    mov byte [tp_ldfull], 0
    call tp_redraw
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tp_ondrag - W_ONDRAG (SPEC.md 13.8.1): the pointer moved, press still down
; -----------------------------------------------------------------------------
tp_ondrag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov di, ax
    call tp_bhit
    xor ah, ah
    cmp ax, di
    je .set
    xor al, al                  ; off it: nothing is down
.set:
    call tp_setdown             ; ...which draws only if that CHANGED
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
tp_onclick:
    call tp_deferred_ld
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [tp_ox], ax
    mov [tp_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM
    mov [tp_cw], cx
    mov [tp_ch], dx
    call tp_clamp_split
    call tp_layout
    call tp_edit_box
    call tp_prev_box
    pop dx
    pop cx
    mov byte [tp_abouton], 0
    ; SPEC.md 13.6/13.8: the seven bar buttons only ARM and DRAW here, and
    ; tp_onup acts. The two scroll-bar rects below keep the press - a scroll
    ; is repeatable and reversible, which is the definition of a safe prefix
    ; action, and the arrow cells are the part that would want a down state.
    call tp_bhit
    or al, al
    jz .sbars
    xor ah, ah
    call os88ui_arm
    call tp_setdown
    jmp .out
.sbars:
    mov bx, tp_r_ssb
    call os88ui_bhit
    jnc .ssb
    mov bx, tp_r_psb
    call os88ui_bhit
    jnc .psb
    ; panes
    cmp dx, [tp_ey1]
    jb .out
    cmp dx, [tp_ey2]
    ja .out
    cmp cx, [tp_ex2]
    jbe .src
    cmp cx, [tp_px1]
    jae .prv
    jmp .out
.src:
    mov byte [tp_focus], 0
    call tp_click_caret
    jmp .draw
.prv:
    mov byte [tp_focus], 1
    jmp .draw
.ssb:
    mov byte [tp_focus], 0
    call tp_ssb_click
    jmp .draw
.psb:
    mov byte [tp_focus], 1
    call tp_psb_click
.draw:
    mov byte [tp_ldfull], 0     ; this IS the full repaint the flag asks for
    call tp_redraw
    jmp .done
.out:
    cmp byte [tp_ldfull], 0     ; a click on nothing, with a load under it
    jne .draw
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_click_caret:
    push ax
    push bx
    push cx
    push dx
    ; row
    mov ax, dx
    sub ax, [tp_ey1]
    sub ax, 3
    jns .r
    xor ax, ax
.r:
    mov bl, 8
    div bl
    xor ah, ah
    add ax, [tp_vscroll]
    mov [tp_tline], ax
    ; col
    mov ax, cx
    sub ax, [tp_ex1]
    sub ax, 3
    jns .c
    xor ax, ax
.c:
    mov bl, 8
    div bl
    xor ah, ah
    add ax, [tp_hscroll]
    mov [tp_wantcol], ax
    mov ax, [tp_tline]
    call tp_goto_lc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX=line, tp_wantcol=col
tp_goto_lc:
    push ax
    push bx
    push cx
    push si
    call tp_line_at
    jc .eof
    ; SI start CX len
    mov ax, [tp_wantcol]
    cmp ax, cx
    jbe .ok
    mov ax, cx
.ok:
    add si, ax
    mov [tp_cur], si
    jmp .o
.eof:
    mov ax, [tp_srclen]
    mov [tp_cur], ax
.o:
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_prev_page:
    cmp word [tp_ppage], 0
    je .o
    dec word [tp_ppage]
    mov word [tp_pscroll], 0
.o:
    ret

tp_next_page:
    mov ax, [tp_npages]
    or ax, ax
    jz .o
    dec ax
    cmp [tp_ppage], ax
    jae .o
    inc word [tp_ppage]
    mov word [tp_pscroll], 0
.o:
    ret

; -----------------------------------------------------------------------------
tp_oncmd:
    call tp_deferred_ld
    cmp ah, 0
    je .file
    cmp ah, 1
    je .edit
    cmp ah, 2
    je .lay
    cmp ah, 3
    je .page
    cmp ah, 4
    je .view
    ret
.file:
    cmp al, 0
    je .new
    cmp al, 1
    je .open
    cmp al, 2
    je .save
    cmp al, 3
    je .saveas
    cmp al, 4
    je .epdf
    cmp al, 5
    je .eps
    ret
.new:
    call tp_seed
    call tp_redraw
    ret
.open:
    mov byte [tp_dlgmode], 0
    mov al, FDLG_OPEN
    call tp_dlg
    ret
.save:
    call tp_save
    call tp_redraw
    ret
.saveas:
    mov byte [tp_dlgmode], 1
    mov al, FDLG_SAVE
    call tp_dlg
    ret
.epdf:
    mov byte [tp_dlgmode], 2
    mov al, FDLG_SAVE
    call tp_dlg
    ret
.eps:
    mov byte [tp_dlgmode], 3
    mov al, FDLG_SAVE
    call tp_dlg
    ret
.edit:
    cmp al, 0
    je .copy
    cmp al, 1
    je .paste
    cmp al, 2
    je .cut
    ret
.copy:
    call tp_copy_all
    call tp_redraw
    ret
.paste:
    call tp_paste
    call tp_redraw
    ret
.cut:
    call tp_cut_line
    call tp_redraw
    ret
.lay:
    cmp al, 0
    je .art
    cmp al, 1
    je .book
    cmp al, 2
    je .mn
    cmp al, 3
    je .m1
    cmp al, 4
    je .mw
    cmp al, 5
    je .pc
    cmp al, 6
    je .pn
    cmp al, 7
    je .pl
    cmp al, 8
    je .sgl
    cmp al, 9
    je .fac
    cmp al, 10
    je .opp
    ret
.art:
    mov byte [tp_class], 0
    mov byte [tp_bind], 0
    jmp .rely
.book:
    mov byte [tp_class], 1
    cmp byte [tp_bind], 2
    je .rely
    mov byte [tp_bind], 1
    jmp .rely
.mn:
    mov byte [tp_margin], 0
    jmp .rely
.m1:
    mov byte [tp_margin], 1
    jmp .rely
.mw:
    mov byte [tp_margin], 2
    jmp .rely
.pc:
    mov byte [tp_pad], 0
    jmp .rely
.pn:
    mov byte [tp_pad], 1
    jmp .rely
.pl:
    mov byte [tp_pad], 2
    jmp .rely
.sgl:
    mov byte [tp_bind], 0
    jmp .rely
.fac:
    mov byte [tp_bind], 1
    jmp .rely
.opp:
    mov byte [tp_bind], 2
    jmp .rely
.page:
    cmp al, 0
    je .ltr
    cmp al, 1
    je .leg
    cmp al, 2
    je .a4
    cmp al, 3
    je .a5
    cmp al, 4
    je .g0
    cmp al, 5
    je .g1
    cmp al, 6
    je .g2
    cmp al, 7
    je .uin
    cmp al, 8
    je .umm
    cmp al, 9
    je .n0
    cmp al, 10
    je .n1
    cmp al, 11
    je .n2
    cmp al, 12
    je .n3
    ret
.ltr:
    mov byte [tp_psize], 0
    jmp .rely
.leg:
    mov byte [tp_psize], 1
    jmp .rely
.a4:
    mov byte [tp_psize], 2
    jmp .rely
.a5:
    mov byte [tp_psize], 3
    jmp .rely
.g0:
    mov byte [tp_gutter], 0
    jmp .rely
.g1:
    mov byte [tp_gutter], 1
    jmp .rely
.g2:
    mov byte [tp_gutter], 2
    jmp .rely
.uin:
    mov byte [tp_units], 0
    jmp .rely
.umm:
    mov byte [tp_units], 1
    jmp .rely
.n0:
    mov byte [tp_pstyle], 0
    jmp .rely
.n1:
    mov byte [tp_pstyle], 1
    jmp .rely
.n2:
    mov byte [tp_pstyle], 2
    jmp .rely
.n3:
    mov byte [tp_pstyle], 3
.rely:
    mov byte [tp_needset], 1
    call tp_redraw
    ret
.view:
    cmp al, 0
    je .set
    cmp al, 1
    je .vp
    cmp al, 2
    je .vn
    cmp al, 3
    je .ws
    cmp al, 4
    je .wp
    ret
.set:
    call tp_typeset
    call tp_redraw
    ret
.vp:
    call tp_prev_page
    call tp_redraw
    ret
.vn:
    call tp_next_page
    call tp_redraw
    ret
.ws:
    add word [tp_split], 24
    call tp_clamp_split
    call tp_redraw
    ret
.wp:
    sub word [tp_split], 24
    call tp_clamp_split
    call tp_redraw
    ret

tp_dlg:
    push bx
    push si
    push di
    mov bx, [tp_win]
    or bx, bx
    jnz .w
    mov bx, si
.w:
    mov di, tp_ondlg
    mov si, tp_fname
    cmp byte [si], 0
    jne .nm
    mov si, tp_s_hello
.nm:
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

tp_ondlg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, di
    mov di, tp_dlgname
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.named:
    push ds
    pop es
    call OSAPI_FILE_HERE
    mov [tp_dir], dx
    mov [tp_drv], bl
    mov al, [tp_dlgmode]
    or al, al
    je .open
    cmp al, 1
    je .save
    cmp al, 2
    je .pdf
    cmp al, 3
    je .ps
    jmp .fin
.open:
    mov si, tp_dlgname
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
    call tp_load
    jc .fin
    call tp_typeset
    jmp .fin
.save:
    mov si, tp_dlgname
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
    call tp_save
    jmp .fin
.pdf:
    mov si, tp_dlgname
    call tp_export_pdf
    jmp .fin
.ps:
    mov si, tp_dlgname
    call tp_export_ps
.fin:
    call tp_retitle
    call tp_redraw              ; SI is tp_redraw's own business now
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
tp_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call tp_claim_src
    jc .nomem
    mov es, [tp_srcseg]
    xor bx, bx
    mov cx, TP_SRC_MAX
    xor dx, dx
    mov si, tp_fname
    call OSAPI_FILE_READ
    jc .err
    cmp ax, TP_SRC_MAX
    jbe .ok
    mov ax, TP_SRC_MAX
.ok:
    mov [tp_srclen], ax
    call tp_crlf_fix
    mov word [tp_cur], 0
    mov word [tp_vscroll], 0
    mov word [tp_hscroll], 0
    mov byte [tp_dirty], 0
    mov byte [tp_needset], 1
    mov si, tp_s_loaded
    call tp_toast
    clc
    jmp .out
.nomem:
    mov si, tp_s_nomem
    call tp_toast
    stc
    jmp .out
.err:
    mov si, tp_s_err
    call tp_toast
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_crlf_fix:
    ; collapse CR LF -> LF, lone CR -> LF
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    xor si, si
    xor di, di
    mov cx, [tp_srclen]
    jcxz .z
.lp:
    mov al, [es:si]
    inc si
    cmp al, 13
    jne .st
    mov al, 10
    cmp si, [tp_srclen]
    jae .st
    cmp byte [es:si], 10
    jne .st
    inc si
.st:
    mov [es:di], al
    inc di
    cmp si, [tp_srclen]
    jb .lp
    mov [tp_srclen], di
.z:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    cmp byte [tp_fname], 0
    jne .nm
    mov si, tp_s_paper
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
.nm:
    call tp_claim_src
    jc .err
    mov es, [tp_srcseg]
    xor bx, bx
    mov cx, [tp_srclen]
    cmp cx, TP_SRC_MAX
    jbe .len
    mov cx, TP_SRC_MAX
.len:
    xor dx, dx
    mov si, tp_fname
    call OSAPI_FILE_WRITE
    jc .err
    mov byte [tp_dirty], 0
    call tp_retitle
    mov si, tp_s_saved
    call tp_toast
    jmp .out
.err:
    mov si, tp_s_werr
    call tp_toast
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_seed:
    push ax
    push cx
    push si
    push di
    push es
    call tp_claim_src
    jc .o
    mov es, [tp_srcseg]
    mov si, tp_seed_s
    xor di, di
.cp:
    lodsb
    mov [es:di], al
    inc di
    or al, al
    jnz .cp
    dec di
    mov [tp_srclen], di
    mov word [tp_cur], 0
    mov word [tp_vscroll], 0
    mov word [tp_hscroll], 0
    mov si, tp_s_untitled
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
    mov byte [tp_dirty], 0
    mov byte [tp_needset], 1
.o:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

tp_copy_all:
    push ax
    push cx
    push si
    push es
    mov ax, [tp_srcseg]
    or ax, ax
    jz .o
    mov es, ax
    xor si, si
    mov cx, [tp_srclen]
    call OSAPI_CLIP_PUT
    mov si, tp_s_copied
    call tp_toast
.o:
    pop es
    pop si
    pop cx
    pop ax
    ret

tp_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_clamp_cur
    call OSAPI_CLIP_SIZE
    jc .o
    or ax, ax
    jz .o
    mov cx, ax
    mov ax, [tp_srclen]
    add ax, cx
    cmp ax, TP_SRC_MAX
    ja .big
    ; insert CX bytes at cursor: grow then get into a hole
    ; paste at end of a gap we open
    mov bx, [tp_cur]
    mov dx, cx                  ; paste len
    ; move tail
    call tp_open_gap            ; BX=cur DX=len
    jc .o
    mov es, [tp_srcseg]
    mov di, [tp_cur]
    mov cx, dx
    call OSAPI_CLIP_GET
    ; CX is what CLIP_GET actually COPIED, not what CLIP_SIZE promised. The
    ; clipboard belongs to the desktop and this app is pre-empted between the
    ; two calls (SPEC.md 20.6), so another program may have replaced it with a
    ; shorter one in between. Advancing by DX would splice that many bytes of
    ; whatever was already in the claim into the document.
    mov dx, cx
    add [tp_cur], dx
    add [tp_srclen], dx
    call tp_crlf_fix
    ; crlf_fix SHRINKS srclen by one byte per CR LF pair it collapses and knows
    ; nothing about the caret, so a paste of CRLF text at the end of the buffer
    ; leaves tp_cur PAST tp_srclen. The next tp_ins then hands tp_open_gap an
    ; offset beyond the end - see the guard there for what that used to do.
    call tp_clamp_cur
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
    jmp .o
.big:
    mov si, tp_s_full
    call tp_toast
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The caret is an offset into the document and every path that shortens the
; document has to bring it back inside, or the next edit works from an offset
; that is past the end. Cheap enough to call unconditionally.
tp_clamp_cur:
    push ax
    mov ax, [tp_srclen]
    cmp [tp_cur], ax
    jbe .o
    mov [tp_cur], ax
.o:
    pop ax
    ret

; BX = offset, DX = bytes to open. CF=1 no room
tp_open_gap:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax, [tp_srclen]
    add ax, dx
    cmp ax, TP_SRC_MAX
    ja .bad
    ; BX PAST the end is the caller's bug, and refusing is the only safe answer
    ; to it: the tail length below is srclen - BX, which UNDERFLOWS to ~64KB
    ; when BX is bigger, and the std/rep movsb under it then walks backwards
    ; over the whole 64KB segment the 8KB claim sits in - the claim heap above
    ; it included (SPEC.md 50.3). Every caller is checked, but this is the one
    ; place the damage is unbounded, so the check lives here too.
    cmp bx, [tp_srclen]
    ja .bad
    mov cx, [tp_srclen]
    sub cx, bx
    jz .ok
    mov si, [tp_srclen]
    dec si
    mov di, si
    add di, dx
    mov ax, [tp_srcseg]
    mov ds, ax
    mov es, ax
    std
    rep movsb
    cld
.ok:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.bad:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

tp_cut_line:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call tp_cur_lc
    ; AX=line
    call tp_line_at
    jc .o
    ; SI start CX len; include following NL
    mov bx, si
    add si, cx
    mov es, [tp_srcseg]
    cmp si, [tp_srclen]
    jae .cut
    cmp byte [es:si], 13
    jne .lf
    inc si
    inc cx
    cmp si, [tp_srclen]
    jae .cut
.lf:
    cmp byte [es:si], 10
    jne .cut
    inc cx
.cut:
    ; clipboard the line
    mov es, [tp_srcseg]
    push cx
    mov cx, cx
    pop cx
    push bx
    mov si, bx
    call OSAPI_CLIP_PUT
    pop bx
    mov [tp_tmp3], cx
    call tp_del_range2
    mov [tp_cur], bx
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
.o:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_del_range2:
    ; BX=off, [tp_tmp3]=count
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov cx, [tp_srclen]
    cmp bx, cx
    jae .none
    mov ax, bx
    add ax, [tp_tmp3]
    jnc .sum
    mov ax, cx
.sum:
    cmp ax, cx
    jbe .ok
    mov ax, cx
.ok:
    sub ax, bx
    mov [tp_tmp3], ax           ; clamped count
    or ax, ax
    jz .none
    mov si, bx
    add si, ax                  ; first kept byte
    mov di, bx
    mov cx, [tp_srclen]
    sub cx, si                  ; tail
    jnc .tail
    xor cx, cx
.tail:
    mov ax, [tp_srcseg]
    mov ds, ax
    mov es, ax
    cld
    jcxz .z
    rep movsb
.z:
    pop es
    pop ds
    mov ax, [tp_srclen]
    sub ax, [tp_tmp3]
    mov [tp_srclen], ax
    pop di
    pop si
    pop cx
    pop ax
    ret
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; AL = char to insert
tp_ins:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    call tp_claim_src
    jc .o
    call tp_clamp_cur
    mov bx, [tp_srclen]
    cmp bx, TP_SRC_MAX
    jae .full
    mov dx, 1
    mov bx, [tp_cur]
    mov [tp_tmp5], al
    call tp_open_gap
    jc .full
    mov es, [tp_srcseg]
    mov di, [tp_cur]
    mov al, [tp_tmp5]
    mov [es:di], al
    inc word [tp_cur]
    inc word [tp_srclen]
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
    jmp .o
.full:
    mov si, tp_s_full
    call tp_toast
.o:
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_backsp:
    cmp word [tp_cur], 0
    je .o
    dec word [tp_cur]
    call tp_delete
.o:
    ret

tp_delete:
    push ax
    push bx
    push cx
    push si
    push es
    mov ax, [tp_cur]
    cmp ax, [tp_srclen]
    jae .o
    mov bx, ax
    mov word [tp_tmp3], 1
    mov es, [tp_srcseg]
    mov si, bx
    mov al, [es:si]
    cmp al, 13
    jne .one
    inc si
    cmp si, [tp_srclen]
    jae .one
    cmp byte [es:si], 10
    jne .one
    mov word [tp_tmp3], 2
.one:
    call tp_del_range2
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
.o:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_go_left:
    cmp word [tp_cur], 0
    je .o
    dec word [tp_cur]
    push es
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    cmp byte [es:si], 10
    jne .d
    or si, si
    jz .d
    dec si
    cmp byte [es:si], 13
    jne .d
    mov [tp_cur], si
.d:
    pop es
    call tp_cur_lc
    mov [tp_wantcol], bx
.o:
    ret

tp_go_right:
    mov ax, [tp_cur]
    cmp ax, [tp_srclen]
    jae .o
    push es
    mov es, [tp_srcseg]
    mov si, ax
    mov al, [es:si]
    inc si
    cmp al, 13
    jne .d
    cmp si, [tp_srclen]
    jae .d
    cmp byte [es:si], 10
    jne .d
    inc si
.d:
    mov [tp_cur], si
    pop es
    call tp_cur_lc
    mov [tp_wantcol], bx
.o:
    ret

; SI = start of the line that contains tp_cur
tp_sol:
    push ax
    push bx
    push es
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    mov bx, [tp_srclen]
    cmp si, bx
    jbe .ok
    mov si, bx
.ok:
    or si, si
    jz .d
.lp:
    dec si
    mov al, [es:si]
    cmp al, 10
    je .aft
    cmp al, 13
    je .aft
    or si, si
    jnz .lp
    jmp .d
.aft:
    inc si
.d:
    pop es
    pop bx
    pop ax
    ret

tp_up:
    push ax
    push bx
    push cx
    push si
    push es
    call tp_cur_lc
    mov [tp_wantcol], bx
    call tp_sol
    or si, si
    jz .o
    dec si
    ; land on previous line; skip CR of CRLF
    mov es, [tp_srcseg]
    cmp byte [es:si], 13
    jne .back
    or si, si
    jz .back
    dec si
    cmp byte [es:si], 10
    je .back
    inc si
.back:
    ; walk back to start of previous line
    or si, si
    jz .at
.w:
    dec si
    mov al, [es:si]
    cmp al, 10
    je .aft
    cmp al, 13
    je .aft
    or si, si
    jnz .w
    jmp .at
.aft:
    inc si
.at:
    mov cx, [tp_wantcol]
    mov bx, [tp_srclen]
.fwd:
    or cx, cx
    jz .set
    cmp si, bx
    jae .set
    mov al, [es:si]
    cmp al, 10
    je .set
    cmp al, 13
    je .set
    inc si
    dec cx
    jmp .fwd
.set:
    mov [tp_cur], si
.o:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_down:
    push ax
    push bx
    push cx
    push si
    push es
    call tp_cur_lc
    mov [tp_wantcol], bx
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    mov bx, [tp_srclen]
.sk:
    cmp si, bx
    jae .o
    mov al, [es:si]
    inc si
    cmp al, 10
    je .at
    cmp al, 13
    jne .sk
    cmp si, bx
    jae .at
    cmp byte [es:si], 10
    jne .at
    inc si
.at:
    mov cx, [tp_wantcol]
.fwd:
    or cx, cx
    jz .set
    cmp si, bx
    jae .set
    mov al, [es:si]
    cmp al, 10
    je .set
    cmp al, 13
    je .set
    inc si
    dec cx
    jmp .fwd
.set:
    mov [tp_cur], si
.o:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_home:
    call tp_sol
    mov [tp_cur], si
    mov word [tp_wantcol], 0
    ret

tp_end:
    push ax
    push bx
    push si
    push es
    call tp_sol
    mov es, [tp_srcseg]
    mov bx, [tp_srclen]
.lp:
    cmp si, bx
    jae .s
    mov al, [es:si]
    cmp al, 10
    je .s
    cmp al, 13
    je .s
    inc si
    jmp .lp
.s:
    mov [tp_cur], si
    mov word [tp_wantcol], 255
    pop es
    pop si
    pop bx
    pop ax
    ret

tp_clamp_vs:
    push ax
    push bx
    call tp_count_lines
    mov bx, [tp_erows]
    or bx, bx
    jnz .e
    mov bx, 8
.e:
    sub ax, bx
    jnc .ok
    xor ax, ax
.ok:
    cmp [tp_vscroll], ax
    jbe .o
    mov [tp_vscroll], ax
.o:
    pop bx
    pop ax
    ret

tp_pgup:
    mov ax, [tp_erows]
    or ax, ax
    jnz .s
    mov ax, 8
.s:
    sub [tp_vscroll], ax
    jnc .g
    mov word [tp_vscroll], 0
.g:
    call tp_clamp_vs
    call tp_cur_lc
    mov ax, [tp_vscroll]
    call tp_goto_lc
    ret

tp_pgdn:
    mov ax, [tp_erows]
    or ax, ax
    jnz .s
    mov ax, 8
.s:
    add [tp_vscroll], ax
    call tp_clamp_vs
    call tp_cur_lc
    mov ax, [tp_vscroll]
    add ax, [tp_erows]
    dec ax
    jns .g
    xor ax, ax
.g:
    call tp_goto_lc
    ret

tp_draw_ssb:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tp_r_ssb]
    mov bx, [tp_r_ssb+2]
    mov cx, [tp_r_ssb+4]
    mov dx, [tp_r_ssb+6]
    cmp cx, ax
    jbe .o
    cmp dx, bx
    jbe .o
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    mov ax, [tp_r_ssb]
    mov bx, [tp_r_ssb+2]
    mov cx, [tp_r_ssb+4]
    mov dx, [tp_r_ssb+6]
    call OSAPI_GFX_FILL
    call tp_count_lines
    mov si, ax                  ; lines
    mov bx, [tp_erows]
    or bx, bx
    jnz .er
    mov bx, 8
.er:
    cmp si, bx
    jbe .o
    mov ax, [tp_r_ssb+6]
    sub ax, [tp_r_ssb+2]
    sub ax, 4
    cmp ax, 8
    jl .o
    ; thumb_h = erows * track / lines, min 8
    push ax
    mul bx
    xor dx, dx
    div si
    cmp ax, 8
    jae .th
    mov ax, 8
.th:
    pop dx                      ; track
    cmp ax, dx
    jbe .th2
    mov ax, dx
.th2:
    mov cx, ax                  ; thumb_h
    sub dx, cx                  ; slack
    mov ax, si
    sub ax, bx                  ; max scroll
    or ax, ax
    jz .top
    push cx
    mov cx, ax
    mov ax, [tp_vscroll]
    mul dx
    xor dx, dx
    div cx
    pop cx
    jmp .got
.top:
    xor ax, ax
.got:
    add ax, [tp_r_ssb+2]
    add ax, 2
    mov bx, ax
    add ax, cx
    dec ax
    mov dx, ax
    mov ax, [tp_r_ssb]
    inc ax
    mov cx, [tp_r_ssb+4]
    dec cx
    cmp dx, bx
    jae .okt
    mov dx, bx
.okt:
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = distinct body lines on the current typeset page.
tp_page_nlines:
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    mov dx, 0xFFFF
    xor si, si
.lp:
    cmp si, [tp_nruns]
    jae .o
    push ax
    mov ax, si
    mov cx, TP_RUN_SZ
    mul cx
    mov bx, ax
    add bx, tp_runs
    pop ax
    mov cl, [bx]
    xor ch, ch
    cmp cx, [tp_ppage]
    jne .n
    cmp word [bx+4], 48
    jb .n
    mov cx, [bx+4]
    cmp cx, dx
    je .n
    mov dx, cx
    inc ax
.n:
    inc si
    jmp .lp
.o:
    pop si
    pop dx
    pop cx
    pop bx
    ret

tp_draw_psb:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tp_r_psb]
    mov bx, [tp_r_psb+2]
    mov cx, [tp_r_psb+4]
    mov dx, [tp_r_psb+6]
    cmp cx, ax
    jbe .o
    cmp dx, bx
    jbe .o
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    mov ax, [tp_r_psb]
    mov bx, [tp_r_psb+2]
    mov cx, [tp_r_psb+4]
    mov dx, [tp_r_psb+6]
    call OSAPI_GFX_FILL
    cmp byte [tp_setok], 0
    je .o
    ; Scroll the sheet, not packed lines: 8px = body point size.
    mov si, [tp_sy2]
    sub si, [tp_sy1]
    mov bx, [tp_py2]
    sub bx, [tp_py1]
    sub bx, 8
    cmp bx, 16
    jae .ph
    mov bx, 16
.ph:
    cmp si, bx
    jbe .o
    mov ax, [tp_r_psb+6]
    sub ax, [tp_r_psb+2]
    sub ax, 4
    cmp ax, 8
    jl .o
    push ax
    mul bx
    xor dx, dx
    div si
    cmp ax, 8
    jae .th
    mov ax, 8
.th:
    pop dx
    cmp ax, dx
    jbe .th2
    mov ax, dx
.th2:
    mov cx, ax                  ; thumb
    sub dx, cx                  ; slack
    mov ax, si
    sub ax, bx                  ; max pscroll
    or ax, ax
    jz .top
    push cx
    mov cx, ax
    mov ax, [tp_pscroll]
    mul dx
    xor dx, dx
    div cx
    pop cx
    jmp .got
.top:
    xor ax, ax
.got:
    add ax, [tp_r_psb+2]
    add ax, 2
    mov bx, ax
    add ax, cx
    dec ax
    mov dx, ax
    mov ax, [tp_r_psb]
    inc ax
    mov cx, [tp_r_psb+4]
    dec cx
    cmp dx, bx
    jae .okt
    mov dx, bx
.okt:
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_psb_click:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [tp_setok], 0
    je .o
    mov si, [tp_sy2]
    sub si, [tp_sy1]
    mov bx, [tp_py2]
    sub bx, [tp_py1]
    sub bx, 8
    cmp si, bx
    jbe .z
    sub si, bx                  ; max pscroll
    mov cx, dx
    sub cx, [tp_r_psb+2]
    jns .y
    xor cx, cx
.y:
    mov ax, [tp_r_psb+6]
    sub ax, [tp_r_psb+2]
    or ax, ax
    jz .o
    xchg ax, si                 ; AX=max SI=track
    mul cx
    div si
    mov [tp_pscroll], ax
    jmp .o
.z:
    mov word [tp_pscroll], 0
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; DX = click y. Set vscroll from track position.
tp_ssb_click:
    push ax
    push bx
    push cx
    push dx
    push si
    call tp_count_lines
    mov bx, [tp_erows]
    or bx, bx
    jnz .e
    mov bx, 8
.e:
    cmp ax, bx
    jbe .o
    sub ax, bx                  ; AX = max vscroll
    mov cx, dx
    sub cx, [tp_r_ssb+2]
    jns .y
    xor cx, cx
.y:
    mov si, [tp_r_ssb+6]
    sub si, [tp_r_ssb+2]
    or si, si
    jz .o
    mul cx                      ; DX:AX = max * yrel
    div si                      ; AX = vscroll
    mov [tp_vscroll], ax
    call tp_clamp_vs
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_keep_caret:
    push ax
    push bx
    call tp_cur_lc
    ; AX line BX col
    cmp ax, [tp_vscroll]
    jae .dn
    mov [tp_vscroll], ax
    jmp .h
.dn:
    mov dx, [tp_erows]
    or dx, dx
    jnz .er
    mov dx, 8
.er:
    add dx, [tp_vscroll]
    or dx, dx
    jz .h
    dec dx
    cmp ax, dx
    jbe .h
    sub ax, [tp_erows]
    inc ax
    jns .sv
    xor ax, ax
.sv:
    mov [tp_vscroll], ax
.h:
    call tp_cur_lc
    cmp bx, [tp_hscroll]
    jae .hr
    mov [tp_hscroll], bx
    jmp .o
.hr:
    mov ax, [tp_hscroll]
    add ax, [tp_ecols]
    or ax, ax
    jz .o
    dec ax
    cmp bx, ax
    jbe .o
    sub bx, [tp_ecols]
    inc bx
    jns .sh
    xor bx, bx
.sh:
    mov [tp_hscroll], bx
.o:
    call tp_clamp_vs
    pop bx
    pop ax
    ret

tp_retitle:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov di, tp_ttlbuf
    mov si, tp_s_name
    call tp_cpat
    mov al, ' '
    stosb
    mov al, '-'
    stosb
    mov al, ' '
    stosb
    mov si, tp_fname
    cmp byte [si], 0
    jne .n
    mov si, tp_s_noload
.n:
    call tp_cpat
    cmp byte [tp_dirty], 0
    je .z
    mov al, '*'
    stosb
.z:
    mov byte [di], 0
    mov bx, [tp_win]
    or bx, bx
    jz .o
    mov ax, tp_ttlbuf
    call OSAPI_WM_TITLE
.o:
    pop es
    pop di
    pop si
    pop ax
    ret

tp_toast:
    push ax
    push cx
    push es
    push ds
    pop es
    mov cx, 54
    call OSAPI_TOAST
    pop es
    pop cx
    pop ax
    ret

tp_cpat:
    push ax
    push ds
    pop es
.l:
    lodsb
    or al, al
    jz .d
    stosb
    jmp .l
.d:
    pop ax
    ret

; tp_cpat with a ceiling: CX = the most bytes to copy. Out CX = what is left of
; it, so several of these can fill one buffer between them, and DI stops where
; the copy stopped. The NUL is the caller's, as with tp_cpat.
;
; Every destination here is a fixed bss field and most of the sources are
; DOCUMENT text, which is as long as whoever wrote the file made it - so a
; plain tp_cpat into a 48-byte field is a buffer overrun with the length under
; the file's control. This is what those call sites use instead.
tp_cpatn:
    push ax
.l:
    jcxz .d
    lodsb
    or al, al
    jz .d
    stosb
    dec cx
    jmp .l
.d:
    pop ax
    ret

tp_u16_di:
    push ax
    push bx
    push cx
    push dx
    push ds
    pop es
    mov bx, 10
    xor cx, cx
    or ax, ax
    jnz .d
    mov al, '0'
    stosb
    jmp .o
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p:
    pop ax
    add al, '0'
    stosb
    loop .p
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    %include "texpad/tpparse.inc"
    %include "texpad/tpexport.inc"

; -----------------------------------------------------------------------------
tp_tpl:
    dw 6, 22, TP_WIN_W, TP_WIN_H
    dw tp_ttl, tp_paint, tp_onkey, tp_onclick

    OS88_MENUSET tp_menus, tp_s_name, tp_oncmd
        OS88_MENU tp_m_file, tp_i_file, 6
        OS88_MENU tp_m_edit, tp_i_edit, 3
        OS88_MENU tp_m_lay, tp_i_lay, 11
        OS88_MENU tp_m_page, tp_i_page, 13
        OS88_MENU tp_m_view, tp_i_view, 5
    OS88_MENUSET_END tp_menus

tp_s_name:      db 'TeXPad', 0
tp_m_file:      db 'File', 0
tp_m_edit:      db 'Edit', 0
tp_m_lay:       db 'Layout', 0
tp_m_page:      db 'Page', 0
tp_m_view:      db 'View', 0
tp_i_file:      dw tp_it_new, tp_it_open, tp_it_save, tp_it_saveas, tp_it_pdf, tp_it_ps
tp_i_edit:      dw tp_it_copy, tp_it_paste, tp_it_cut
tp_i_lay:       dw tp_it_art, tp_it_book, tp_it_mn, tp_it_m1, tp_it_mw
                dw tp_it_pc, tp_it_pn, tp_it_pl
                dw tp_it_sgl, tp_it_fac, tp_it_opp
tp_i_page:      dw tp_it_ltr, tp_it_leg, tp_it_a4, tp_it_a5
                dw tp_it_g0, tp_it_g1, tp_it_g2
                dw tp_it_uin, tp_it_umm
                dw tp_it_n0, tp_it_n1, tp_it_n2, tp_it_n3
tp_i_view:      dw tp_it_set, tp_it_vp, tp_it_vn, tp_it_ws, tp_it_wp
tp_it_new:      db 'New', 0
tp_it_open:     db 'Open...', 0
tp_it_save:     db 'Save', 0
tp_it_saveas:   db 'Save As...', 0
tp_it_pdf:      db 'Export PDF...', 0
tp_it_ps:       db 'Export PS...', 0
tp_it_copy:     db 'Copy All', 0
tp_it_paste:    db 'Paste', 0
tp_it_cut:      db 'Cut Line', 0
tp_it_art:      db 'Article', 0
tp_it_book:     db 'Book', 0
tp_it_mn:       db 'Narrow Margins', 0
tp_it_m1:       db 'Normal Margins', 0
tp_it_mw:       db 'Wide Margins', 0
tp_it_pc:       db 'Compact Padding', 0
tp_it_pn:       db 'Normal Padding', 0
tp_it_pl:       db 'Loose Padding', 0
tp_it_sgl:      db 'Single Pages', 0
tp_it_fac:      db 'Facing Pages', 0
tp_it_opp:      db 'Opposing Pages', 0
tp_it_ltr:      db 'Letter 8.5 x 11 in', 0
tp_it_leg:      db 'Legal 8.5 x 14 in', 0
tp_it_a4:       db 'A4 210 x 297 mm', 0
tp_it_a5:       db 'A5 148 x 210 mm', 0
tp_it_g0:       db 'Gutter Off', 0
tp_it_g1:       db 'Gutter Narrow', 0
tp_it_g2:       db 'Gutter Wide', 0
tp_it_uin:      db 'Inches', 0
tp_it_umm:      db 'Millimetres', 0
tp_it_n0:       db 'Numbers Off', 0
tp_it_n1:       db 'Numbers Bottom Center', 0
tp_it_n2:       db 'Numbers Header', 0
tp_it_n3:       db 'Numbers Outer', 0
tp_it_set:      db 'Typeset', 0
tp_it_vp:       db 'Prev Page', 0
tp_it_vn:       db 'Next Page', 0
tp_it_ws:       db 'Wider Source', 0
tp_it_wp:       db 'Wider Preview', 0

tp_ttl:         db 'TeXPad', 0
tp_s_hello:     db 'HELLO.TEX', 0
tp_s_paper:     db 'PAPER.TEX', 0
tp_s_untitled:  db 'UNTITLED.TEX', 0
tp_s_noload:    db '(new)', 0
tp_s_set:       db 'Set', 0
tp_s_ltr:       db 'Ltr', 0
tp_s_leg:       db 'Leg', 0
tp_s_a4s:       db 'A4', 0
tp_s_a5s:       db 'A5', 0
tp_s_sgl:       db '1pg', 0
tp_s_fac:       db 'Fac', 0
tp_s_opp:       db 'Opp', 0
tp_s_g0:        db 'G0', 0
tp_s_g1:        db 'G+', 0
tp_s_g2:        db 'G++', 0
tp_s_pc:        db 'Pad-', 0
tp_s_pn:        db 'Pad', 0
tp_s_pl:        db 'Pad+', 0
tp_s_lt:        db '<', 0
tp_s_gt:        db '>', 0
tp_s_stale:     db '(edit)', 0
tp_s_letter:    db 'Letter', 0
tp_s_legal:     db 'Legal', 0
tp_s_pt:        db 'pt', 0
tp_s_u075:      db '.75in', 0
tp_s_u10:       db '1in', 0
tp_s_u15:       db '1.5in', 0
tp_s_m19:       db '19mm', 0
tp_s_m25:       db '25mm', 0
tp_s_m38:       db '38mm', 0
tp_s_gi0:       db 'G0', 0
tp_s_gi1:       db 'G.25in', 0
tp_s_gi2:       db 'G.5in', 0
tp_s_gm0:       db 'G0', 0
tp_s_gm1:       db 'G6mm', 0
tp_s_gm2:       db 'G13mm', 0
tp_s_n0:        db 'No#', 0
tp_s_n1:        db 'Ft', 0
tp_s_n2:        db 'Hd', 0
tp_s_n3:        db 'Out', 0
tp_s_noset:     db 'F5 typesets the preview', 0
tp_s_loaded:    db 'Loaded', 0
tp_s_saved:     db 'Saved', 0
tp_s_copied:    db 'Copied', 0
tp_s_err:       db 'Read error - new document', 0
tp_s_werr:      db 'Write error', 0
tp_s_nomem:     db 'Need more RAM', 0
tp_s_full:      db 'Document full (8K)', 0
tp_s_xok:       db 'Exported', 0
tp_s_xerr:      db 'Export failed', 0
tp_s_xbig:      db 'Export too large', 0
; The About card, in the shape apps/paint and apps/modplug use it: what the
; program is, who contributed it, and one honest line about what is NOT in
; here. A reader who has met TeX should not be left thinking this is TeX.
tp_s_a1:        db 'TeXPad for os8088', 0
tp_s_a2:        db 'a TeX pad for the 8086', 0
tp_s_a3:        db 0
tp_s_a4:        db 'contributed by Jason Page', 0
tp_s_a5:        db 'amfile.org', 0
tp_s_a6:        db 0
tp_s_a7:        db 'A paper-oriented SUBSET of the markup, set in', 0
tp_s_a8:        db 'the kernel 8x8 monofont. No math engine, no', 0
tp_s_a9:        db 'figures, no colour - see SPEC.md 69.2.', 0
tp_s_a10:       db 0
tp_s_a11:       db 'F5 typesets.  File exports PDF 1.4 and PostScript.', 0

tp_seed_s:
    db '\documentclass[letterpaper,12pt]{article}', 10
    db '\title{Untitled}', 10
    db '\author{}', 10
    db '\date{}', 10
    db '\begin{document}', 10
    db '\maketitle', 10
    db '\section{Introduction}', 10
    db 10
    db 'Type on the left. F5 typesets the preview.', 10
    db 10
    db '\end{document}', 10
    db 0

    %include "os88ui.inc"

TP_BSS_TOTAL equ 12288
    OS88_BSS TP_BSS_TOTAL
    OS88_IMAGE_END

; ---- bss --------------------------------------------------------------------
tp_win      equ os88_image_end + 0
tp_ox       equ os88_image_end + 2
tp_oy       equ os88_image_end + 4
tp_cw       equ os88_image_end + 6
tp_ch       equ os88_image_end + 8
tp_srcseg   equ os88_image_end + 10
tp_srclen   equ os88_image_end + 12
tp_expseg   equ os88_image_end + 14
tp_explen   equ os88_image_end + 16
tp_dir      equ os88_image_end + 18
tp_drv      equ os88_image_end + 20
tp_cur      equ os88_image_end + 22
tp_wantcol  equ os88_image_end + 24
tp_vscroll  equ os88_image_end + 26
tp_hscroll  equ os88_image_end + 28
tp_pscroll  equ os88_image_end + 30
tp_ppage    equ os88_image_end + 32
tp_npages   equ os88_image_end + 34
tp_split    equ os88_image_end + 36
tp_focus    equ os88_image_end + 38
tp_dirty    equ os88_image_end + 39
tp_needset  equ os88_image_end + 40
tp_setok    equ os88_image_end + 41
tp_needld   equ os88_image_end + 42
tp_abouton  equ os88_image_end + 43
tp_dlgmode  equ os88_image_end + 44
tp_class    equ os88_image_end + 45
tp_margin   equ os88_image_end + 46
tp_pad      equ os88_image_end + 47
tp_pstyle   equ os88_image_end + 48
tp_fsize    equ os88_image_end + 49
tp_ex1      equ os88_image_end + 50
tp_ey1      equ os88_image_end + 52
tp_ex2      equ os88_image_end + 54
tp_ey2      equ os88_image_end + 56
tp_px1      equ os88_image_end + 58
tp_py1      equ os88_image_end + 60
tp_px2      equ os88_image_end + 62
tp_py2      equ os88_image_end + 64
tp_erows    equ os88_image_end + 66
tp_ecols    equ os88_image_end + 68
tp_tline    equ os88_image_end + 70
tp_cxp      equ os88_image_end + 72
tp_cyp      equ os88_image_end + 74
tp_tmp      equ os88_image_end + 76
tp_tmp2     equ os88_image_end + 78
tp_tmp3     equ os88_image_end + 80
tp_tmp4     equ os88_image_end + 82
tp_tmp5     equ os88_image_end + 84
; typesetter
tp_page     equ os88_image_end + 86
tp_x        equ os88_image_end + 88
tp_y        equ os88_image_end + 90
tp_left     equ os88_image_end + 92
tp_right    equ os88_image_end + 94
tp_tleft    equ os88_image_end + 96
tp_ttop     equ os88_image_end + 98
tp_tbot     equ os88_image_end + 100
tp_tlead    equ os88_image_end + 102
tp_tsize    equ os88_image_end + 104
tp_indent   equ os88_image_end + 106
tp_inpara   equ os88_image_end + 108
tp_needind  equ os88_image_end + 109
tp_bold     equ os88_image_end + 110
tp_under    equ os88_image_end + 111
tp_align    equ os88_image_end + 112
tp_csize    equ os88_image_end + 113
tp_parind   equ os88_image_end + 114
tp_parskip  equ os88_image_end + 116
tp_tabsep   equ os88_image_end + 118
tp_stretch  equ os88_image_end + 120
tp_tw       equ os88_image_end + 122
tp_chno     equ os88_image_end + 124
tp_secno    equ os88_image_end + 126
tp_subno    equ os88_image_end + 128
tp_itemn    equ os88_image_end + 130
tp_made     equ os88_image_end + 132
tp_nruns    equ os88_image_end + 134
tp_nboxes   equ os88_image_end + 136
tp_tused    equ os88_image_end + 138
tp_si       equ os88_image_end + 140
tp_se       equ os88_image_end + 142
tp_sty_n    equ os88_image_end + 144
tp_env_n    equ os88_image_end + 145
tp_clen     equ os88_image_end + 146
tp_pw       equ os88_image_end + 148
tp_ph       equ os88_image_end + 150
tp_cmdn     equ os88_image_end + 152
tp_argn     equ os88_image_end + 154
tp_exp_i    equ os88_image_end + 156
tp_nobj     equ os88_image_end + 158
tp_xkind    equ os88_image_end + 160
tp_fname    equ os88_image_end + 162          ; 14
tp_dlgname  equ os88_image_end + 176          ; 14
tp_ttlbuf   equ os88_image_end + 190          ; 32
tp_status   equ os88_image_end + 222          ; 80
tp_nbuf     equ os88_image_end + 302          ; 48
tp_cmd      equ os88_image_end + 350          ; 32
tp_arg      equ os88_image_end + 382          ; 182
tp_wbuf     equ os88_image_end + 564          ; 82
tp_linebuf  equ os88_image_end + 646          ; 150
tp_cline    equ os88_image_end + 796          ; 122
tp_title    equ os88_image_end + 918          ; 80
tp_author   equ os88_image_end + 998          ; 80
tp_date     equ os88_image_end + 1078         ; 40
tp_sty      equ os88_image_end + 1118         ; 32
tp_env      equ os88_image_end + 1150         ; 24
tp_envsv    equ os88_image_end + 1174         ; 36
tp_colw     equ os88_image_end + 1210         ; 12
tp_colk     equ os88_image_end + 1222         ; 6
tp_colbar   equ os88_image_end + 1228         ; 8
tp_objoff   equ os88_image_end + 1236         ; 80
tp_r_set    equ os88_image_end + 1316
tp_r_cls    equ os88_image_end + 1324
tp_r_mar    equ os88_image_end + 1332
tp_r_pad    equ os88_image_end + 1340
tp_r_prev   equ os88_image_end + 1348
tp_r_next   equ os88_image_end + 1356
tp_runs     equ os88_image_end + 1364         ; 4800
tp_boxes    equ os88_image_end + 6164         ; 640
tp_text     equ os88_image_end + 6804         ; 5120  -> 11924
tp_rx       equ os88_image_end + 11924
tp_ry       equ os88_image_end + 11926
tp_fs_ret   equ os88_image_end + 11928
tp_fs_need  equ os88_image_end + 11930
tp_needttl  equ os88_image_end + 11932
tp_scanat   equ os88_image_end + 11934
tp_budget   equ os88_image_end + 11936
tp_psize    equ os88_image_end + 11938   ; 0 Letter 1 Legal 2 A4 3 A5
tp_bind     equ os88_image_end + 11939   ; 0 single 1 facing 2 opposing
tp_gutter   equ os88_image_end + 11940   ; 0 off 1 18pt 2 36pt
tp_units    equ os88_image_end + 11941   ; 0 in 1 mm
tp_r_gut    equ os88_image_end + 11942   ; 8
tp_sx1      equ os88_image_end + 11950
tp_sy1      equ os88_image_end + 11952
tp_sx2      equ os88_image_end + 11954
tp_sy2      equ os88_image_end + 11956
tp_tx1      equ os88_image_end + 11958
tp_ty1      equ os88_image_end + 11960
tp_tx2      equ os88_image_end + 11962
tp_ty2      equ os88_image_end + 11964
tp_r_ssb    equ os88_image_end + 11966   ; 8
tp_onpage   equ os88_image_end + 11974
tp_r_psb    equ os88_image_end + 11976   ; 8
tp_tabtop   equ os88_image_end + 11984
tp_tabbot   equ os88_image_end + 11986
tp_tabw     equ os88_image_end + 11988
tp_tabn     equ os88_image_end + 11990
tp_rowh     equ os88_image_end + 11992
tp_ldfull   equ os88_image_end + 11994   ; a deferred load landed: the next
                                         ; handler owes a FULL repaint, not a
                                         ; source-pane one (tp_deferred_ld)
; remainder to 12288. Every name above is a hand-computed offset and nothing
; checks them against each other, so a new field goes at the END and a field
; that grows moves everything under it - the assertion below is the only
; automatic part, and it catches overflowing the block, not overlapping inside
; it. TP_BSS_LAST is the high-water mark; keep it pointing at the last field.
TP_BSS_LAST equ 11994 + 1
%if TP_BSS_LAST > TP_BSS_TOTAL
    %error "texpad bss map overflows OS88_BSS"
%endif
