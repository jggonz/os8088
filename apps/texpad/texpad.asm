; =============================================================================
; TeXPad - black-and-white TeX pad for os8088
;
; Source on the left, typeset preview on the right. Letter default, other
; sizes on the Page menu. Facing/opposing, gutter, units, page numbers.
; System 8x8 monofont, text + tables. Export is PDF 1.4 / PostScript Level 1.
; =============================================================================

%include "os88api.inc"

; SPEC.md 13.10.5's thumb GESTURE, and it SHIPS (13.10.7);
; `make SBDRAGOFF=1` compiles it out.
; AT THE TOP, not beside the %include that pulls os88ui.inc in (13.10.7.4).
%ifndef SBDRAGOFF
%define OS88UI_SBDRAG
%ifndef SB_RATE
%define SB_RATE 0               ; RATE 0 (13.10.5.4): both panes repaint whole
%endif
TP_SBRATE   equ SB_RATE
%endif

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
TP_SPLIT_MIN  equ 120           ; the narrowest the SOURCE pane is worth
TP_PREV_MIN   equ 140           ; ...and the PREVIEW, which is a page
TP_TEXT_MIN   equ 48            ; six rows of source under the bar
; The smallest FRAME the two-pane layout still means anything in (SPEC.md
; 11.100.2), derived from tp_clamp_split's own two numbers so they cannot
; drift apart. Without it WMIN_W lets the grow box take this window to 96:
; measured, the content is then 94 wide, tp_clamp_split pins the split at 120
; - WIDER THAN THE WINDOW - and the preview pane comes out **-26 pixels**, so
; the source pane runs off its own right edge and the preview is a rect with a
; negative width.
TP_MIN_W      equ TP_SPLIT_MIN + TP_PREV_MIN + 2
TP_MIN_H      equ TP_BAR_H + TP_STAT_H + TP_TEXT_MIN + TITLE_H + 1
TP_SB_W       equ 14            ; SPEC.md 13.10: the shared bar is 14 wide
TP_WRAP_MARK  equ '>'           ; the last column of a row that CONTINUES
TP_SB_STEP    equ 4             ; ...and an arrow cell steps FOUR rows. One is
                                ; what the kernel's own bars do and it is four
                                ; clicks a line of TeX here, where a row is an
                                ; 8px cell rather than a file-list entry. The
                                ; preview scrolls in the same step, times the
                                ; 8px a body line occupies there (SPEC.md
                                ; 13.10.1: the policy is the caller's)
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
%ifdef OS88UI_SBDRAG
    sbb al, al                  ; CF = 1 on kern_small (SPEC.md 13.10.7.1): no
    mov [tp_nodrag], al         ; tracking edge, so no thumb gesture either
%endif
    pop ax
    mov byte [tp_bdown], 0
    mov al, 1
    call OSAPI_WM_SIZABLE
    mov al, 1
    call OSAPI_WM_SNAP
    push cx                     ; ...AND A FLOOR UNDER THE GROW BOX (SPEC.md
    push dx                     ; 11.100.2). Two panes with minimums cannot be
    mov cx, TP_MIN_W            ; expressed by WMIN_W, which is one number for
    mov dx, TP_MIN_H            ; the whole machine and is 96
    call OSAPI_WM_MINSIZE
    pop dx
    pop cx
    push si                     ; ...and the HERCULES is 720 columns wide
    mov si, tp_pref             ; (SPEC.md 11.100.1). A two-pane editor is the
    call OSAPI_WM_PREFER        ; one window in the tree with somewhere to put
    pop si                      ; them: the split stays where it is and every
                                ; extra column goes to the page
    push si
    mov si, tp_onabout
    call OSAPI_ABOUT_SET
    pop si
    call tp_rgn_all             ; the preview region is bss, and bss is ZERO -
                                ; which as a region means "nothing is
                                ; drawable". Every painter sets it, but this
                                ; is the one that costs nothing to be sure of
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
    ; ARG_FILE is READ here, not in the entry proc: a floppy read under the
    ; loader lock froze the desktop (SPEC.md 69.6). First paint is after the
    ; window is up, so the associated .TEX actually lands in the source pane
    ; instead of waiting for a key or a click.
    cmp byte [tp_needld], 0
    je .noload
    call tp_deferred_ld
    mov byte [tp_ldfull], 0     ; the load asks the NEXT handler for a full
                                ; repaint, and this IS one: the rest of this
                                ; proc draws every part of the window from the
                                ; document that just landed. Left set, the
                                ; first keystroke afterwards took the whole
                                ; window again instead of the one line it
                                ; changed
.noload:
    call tp_fill
    mov byte [tp_caret_on], 0
    mov byte [tp_dselon], 0
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
; -----------------------------------------------------------------------------
; The one thing this window has to say about an adapter (SPEC.md 11.100.1).
;
; VGA and CGA get a PAIR OF ZEROS - "nothing to say", so the template stands
; and wm_fit clamps it exactly as it always did: 628x400 on a VGA, and 628x155
; on a CGA where the desktop band is all there is. The template is already
; nearly the whole width of both.
;
; THE HERCULES IS THE ENTRY, and it is the only window in the tree with an
; obvious use for those 720 columns: the source pane keeps its split and every
; extra column goes to the page. The height is the template's and is clamped to
; that adapter's 303-row band, which is 11.100.1's "a real width and a generous
; height" - only the width here is a decision.
; -----------------------------------------------------------------------------
    OS88_PREFER tp_pref, 0, 0,  712, TP_WIN_H,  0, 0


tp_clamp_split:
    push ax
    push bx
    mov ax, [tp_split]
    cmp ax, TP_SPLIT_MIN
    jae .lo
    mov ax, TP_SPLIT_MIN
.lo:
    mov bx, [tp_cw]
    sub bx, TP_PREV_MIN
    cmp bx, TP_SPLIT_MIN
    jge .hi
    mov bx, TP_SPLIT_MIN        ; the floor OSAPI_WM_MINSIZE now makes
.hi:                            ; unreachable - kept because this routine is
                                ; also reached from tp_paint, which runs on
                                ; whatever box the record holds
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
    add cx, 60                  ; 'Typeset' is seven cells; the five cyclers
                                ; after it hold three or six and stay at 32
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
    call tp_clip_pane           ; ...to the REGION as well, so a band redraw
    jc .gutter                  ; greys the band and not the pane
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
    call tp_sheet_edges
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
    call tp_clip_pane
    jc .lg
    call OSAPI_GFX_FILL_GRAY
.lg:
    pop cx
    jmp .txt
.rg:
    mov ax, [tp_sx2]
    sub ax, cx
    mov bx, [tp_sy1]
    inc bx
    mov cx, [tp_sx2]
    dec cx
    mov dx, [tp_sy2]
    dec dx
    call tp_clip_pane
    jc .txt
    call OSAPI_GFX_FILL_GRAY
.txt:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The sheet's four edges, each clipped on its own. GFX_FRAME cannot be used
; here: clamped to a band it draws the two sides the clamp INVENTED, so a
; scrolled page came out ruled across the middle.
tp_sheet_edges:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tp_sx1]            ; top
    mov bx, [tp_sy1]
    mov cx, [tp_sx2]
    mov dx, bx
    call tp_clip_pane
    jc .bot
    mov bx, cx                  ; hline: AX=x1, BX=x2, DX=y
    call OSAPI_GFX_HLINE
.bot:
    mov ax, [tp_sx1]
    mov bx, [tp_sy2]
    mov cx, [tp_sx2]
    mov dx, bx
    call tp_clip_pane
    jc .lft
    mov bx, cx
    call OSAPI_GFX_HLINE
.lft:
    mov ax, [tp_sx1]            ; left
    mov bx, [tp_sy1]
    mov cx, ax
    mov dx, [tp_sy2]
    call tp_clip_pane
    jc .rgt
    call OSAPI_GFX_VLINE        ; AX=x, BX=y1, DX=y2, which is what the clip
                                ; already left behind
.rgt:
    mov ax, [tp_sx2]            ; right
    mov bx, [tp_sy1]
    mov cx, ax
    mov dx, [tp_sy2]
    call tp_clip_pane
    jc .o
    call OSAPI_GFX_VLINE
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE REGION - what part of the preview pane is being drawn right now
;
; The preview is painted by one set of routines whatever the reason, and a
; SCROLL needs the same picture cut to a band. There is no clip region a
; package can ask for - OSAPI_WM_CLIP_SET is the window manager's, about what
; is covering you - so this is the app's own, honoured by tp_clip_pane below
; and by the two painters' y tests. Everything that draws the preview goes
; through one of those, so a region set here reaches all of it.
;
; It is set to the whole pane by tp_rgn_all and NEVER left narrow: every
; entry that paints the preview opens with tp_rgn_all.
; -----------------------------------------------------------------------------
tp_rgn_all:
    push ax
    xor ax, ax
    mov [tp_rgx1], ax
    mov [tp_rgy1], ax
    mov ax, 0x7FFF              ; SIGNED-large, and that is the whole of it:
    mov [tp_rgx2], ax           ; tp_clip_pane compares with jge/jle because a
    mov [tp_rgy2], ax           ; mapped sheet coordinate is legitimately
    pop ax                      ; NEGATIVE (a page scrolled above the pane),
    ret                         ; so 0xFFFF as "no restriction" is -1 and
                                ; every rect clipped against it came out
                                ; EMPTY - measured as a preview with no desk,
                                ; no paper and no page edges at all, on every
                                ; full repaint

; AX..DX become the region (inclusive).
tp_rgn_set:
    mov [tp_rgx1], ax
    mov [tp_rgy1], bx
    mov [tp_rgx2], cx
    mov [tp_rgy2], dx
    ret

; Clip AX/BX/CX/DX to the preview pane AND the region. CF=1 if empty.
tp_clip_pane:
    push si
    mov si, [tp_rgx1]
    cmp ax, si
    jge .r1
    mov ax, si
.r1:
    mov si, [tp_rgy1]
    cmp bx, si
    jge .r2
    mov bx, si
.r2:
    mov si, [tp_rgx2]
    cmp cx, si
    jle .r3
    mov cx, si
.r3:
    mov si, [tp_rgy2]
    cmp dx, si
    jle .r4
    mov dx, si
.r4:
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
    mov [tp_statcnt], di        ; where the BYTE COUNT starts, which is the
                                ; only part of this line a keystroke moves
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
    je .arrow
    mov si, tp_s_gt
.arrow:
    ; The two page arrows are the only ones here that can have nothing to do,
    ; and on page 1 of 1 that is both of them. SPEC.md 47: grey a FACT - the
    ; page number and the page count are both on the bar beside them, so this
    ; is not a guess - and let os88ui_btn dither the frame and the label,
    ; which is what makes it read as disabled on the 1bpp adapters too
    ; (SPEC.md 39.4, where grey rounds to black).
    inc al                      ; back to the 1..7 tp_bdis expects
    call tp_bdis
    jnc .draw
    or di, OS88UI_DIS           ; ...and DISABLED wins over DOWN inside
                                ; os88ui_btn, so there is nothing to clear
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
; tp_bdis - is button AL (1..7) DISABLED? CF=1 yes. Preserves every register.
;
; One description, read by the painter and by the press: a button drawn grey
; that still arms and depresses is worse than one that was never greyed, and
; the two would be separate opinions about tp_ppage otherwise.
; -----------------------------------------------------------------------------
tp_bdis:
    push ax
    push bx
    cmp al, 6
    je .prev
    cmp al, 7
    je .next
.no:
    pop bx
    pop ax
    clc
    ret
.prev:
    cmp word [tp_ppage], 0
    jne .no
.yes:
    pop bx
    pop ax
    stc
    ret
.next:
    mov ax, [tp_npages]
    or ax, ax
    jz .yes                     ; nothing typeset: there is no page 2
    dec ax
    mov bx, [tp_ppage]
    cmp bx, ax
    jae .yes
    jmp short .no

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
    ; OSAPI_GFX_HLINE is AX = x1, BX = x2, DX = y - not the four corners the
    ; fills take. Written as a rect, this drew from the left edge to whatever
    ; the Y happened to be: the bar's rule stopped at x = 62 and the status
    ; strip's at x = 407, both of them a coincidence of the window's height.
    mov ax, [tp_ox]
    mov dx, [tp_oy]
    add dx, TP_BAR_H
    dec dx                      ; DX = the rule's row
    mov bx, [tp_ox]
    add bx, [tp_cw]
    dec bx                      ; BX = its right end
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
    call tp_barsig              ; what this string was composed from, for
    mov [tp_barsig_v], al       ; tp_stat_touch's comparison
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
    mov ax, [tp_ox]             ; ...and the status strip's, the same way
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, TP_STAT_H
    mov bx, [tp_ox]
    add bx, [tp_cw]
    dec bx
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
    mov si, tp_s_a12
    call tp_about_line
    mov si, tp_s_a13
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
    sub ax, TP_SB_W - 1
    mov [tp_r_ssb], ax          ; sbar x1 (inclusive width = TP_SB_W)
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

; The preview pane's frame, and its INTERIOR ON BYTE COLUMNS.
;
; A gfx_scroll is byte-column granular (SPEC.md 5.5) and what is immediately
; outside this pane on both sides is a SCROLL BAR - the source's on the left,
; its own on the right - so a blit may not round either edge outward, and
; rounding inward leaves columns it cannot move. Here that is not a strip to
; repaint but a defect: the sheet is wider than the pane at 12pt, so the
; right-hand columns carry TEXT, and repainting them means re-lettering every
; row in the pane, which is the full repaint this exists to avoid.
;
; So the pane is placed where the arithmetic works instead: x1 rounded UP to
; the next byte column and x2+1 rounded DOWN to one, which costs at most
; seven pixels of page at each side and makes [px1+1, px2-1] exactly what
; gfx_scroll can move. The gaps left over are window background and tp_fill
; paints them.
tp_prev_box:
    push ax
    mov ax, [tp_ox]
    add ax, [tp_split]
    add ax, 5
    add ax, 7                   ; px1+1 up to a byte column, so the interior
    and ax, 0xFFF8              ; starts on one...
    dec ax
    mov [tp_px1], ax
    mov ax, [tp_ox]
    add ax, [tp_cw]
    dec ax
    dec ax
    mov [tp_r_psb+4], ax        ; preview sbar x2
    sub ax, TP_SB_W - 1
    mov [tp_r_psb], ax
    dec ax
    and ax, 0xFFF8              ; ...and px2 = the interior's x2+1, down to one
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
    call tp_src_metrics
    jc .out
    call tp_ssb_draw
    mov ax, [tp_srcseg]
    or ax, ax
    jz .out
    xor di, di
.row:
    cmp di, [tp_erows]
    jae .marks
    call tp_draw_srow
    inc di
    jmp .row
.marks:
    mov byte [tp_caret_on], 0
    mov byte [tp_dselon], 0
    call tp_sel_show
    call tp_caret_show
    mov ax, [tp_vscroll]
    mov [tp_oldvs], ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE ROW ENGINE - what one line of the source pane shows (SPEC.md 69.10)
;
; THERE IS NO HORIZONTAL SCROLLING. A line longer than the pane WRAPS, broken
; at the last space that fits - hard at the width when one word is longer than
; the pane - and a row that continues carries TP_WRAP_MARK in its last column.
; What that replaces is arrowing sideways to read a document, and the reason
; is as much about drawing as about the reading: every row's text changes when
; the view slides one column, so a horizontal scroll is a whole-pane repaint
; on every keystroke and cannot be made cheap.
;
; A ROW is therefore the unit here. The caret, the selection, the scroll bar,
; the page keys and every drawing path count in rows; a LINE exists inside
; this section, in tp_cut_line, and in the file on disk.
;
; The walk starts at the top of the document, as the line walk it replaces
; already did - a row cannot know where it begins without reading everything
; before it. What makes that affordable is the one-entry CACHE below: every
; drawing path asks for rows in order, so the second row of a repaint starts
; where the first one ended and a pane repaint is ONE pass over the document
; instead of forty-four.
; =============================================================================

; AX = the wrap width in cells: the pane's columns less the one the mark sits
; in, and never less than one.
tp_row_w:
    mov ax, [tp_ecols]
    or ax, ax
    jz .one
    dec ax
    or ax, ax
    jnz .o
.one:
    mov ax, 1
.o:
    ret

; The cache is a guess about where a row starts, so the only way to be wrong
; is to be stale: any edit, and any change of width. The row COUNT is a guess
; about the same thing, so it goes at the same moments.
tp_row_inval:
    mov word [tp_rc_row], 0xFFFF
    mov byte [tp_nrowsok], 0
    ret

; SI = a row's start offset, ES = tp_srcseg. Out: CX = its length in cells,
; DI = where the NEXT row starts, [tp_rowcont] = 1 when the break is a WRAP
; rather than a newline. CF = 1 when SI is past the end of the document.
tp_row_ext:
    push ax
    push bx
    push dx
    mov byte [tp_rowcont], 0
    mov bx, [tp_srclen]
    cmp si, bx
    ja .none
    call tp_row_w               ; AX = the width
    xor cx, cx
    mov di, si
.scan:
    cmp di, bx
    jae .eol
    mov dl, [es:di]
    cmp dl, 10
    je .eol
    cmp dl, 13
    je .eol
    inc di
    inc cx
    cmp cx, ax
    jbe .scan                   ; stop one PAST the width - that cell is the
    jmp short .wrap             ; one that does not fit
.eol:
    mov cx, di
    sub cx, si                  ; the row is the rest of the line
    cmp di, bx
    jb .nl
    inc di                      ; at the END of the document the next row must
    jmp short .o                ; not start where this one did, or the walk
.nl:                            ; never terminates
    mov dl, [es:di]
    inc di
    cmp dl, 13
    jne .o
    cmp di, bx
    jae .o
    cmp byte [es:di], 10
    jne .o
    inc di
    jmp short .o
.wrap:
    mov byte [tp_rowcont], 1
    mov di, si
    add di, ax                  ; the first cell that did not fit
.back:
    cmp di, si
    jbe .hard                   ; one word, longer than the pane
    cmp byte [es:di], ' '
    je .space
    dec di
    jmp short .back
.space:
    mov cx, di
    sub cx, si                  ; the row ends BEFORE the space...
    inc di                      ; ...and the next one starts after it
    jmp short .o
.hard:
    mov cx, ax
    mov di, si
    add di, ax
.o:
    pop dx
    pop bx
    pop ax
    clc
    ret
.none:
    pop dx
    pop bx
    pop ax
    stc
    ret

; AX = a row index. Out: CF=1 past the end, else SI = its start, CX = its
; length, [tp_rowcont] set. Every other register preserved.
tp_row_at:
    push ax
    push bx
    push dx
    push di
    push es
    mov [tp_rc_want], ax
    mov es, [tp_srcseg]
    call tp_row_w
    cmp ax, [tp_rc_w]
    je .w
    mov [tp_rc_w], ax           ; a resize is a different set of rows
    call tp_row_inval
.w:
    mov ax, [tp_rc_want]
    mov bx, [tp_rc_row]
    cmp bx, 0xFFFF
    je .top
    cmp ax, bx
    jb .top                     ; the cache is BELOW us: start over
    mov si, [tp_rc_off]
    mov dx, bx
    jmp short .walk
.top:
    xor si, si
    xor dx, dx
.walk:
    cmp dx, ax
    je .here
    call tp_row_ext
    jc .miss
    mov si, di
    inc dx
    jmp short .walk
.here:
    call tp_row_ext             ; the row's own length and continuation
    jc .miss
    mov [tp_rc_row], ax
    mov [tp_rc_off], si
    pop es
    pop di
    pop dx
    pop bx
    pop ax
    clc
    ret
.miss:
    pop es
    pop di
    pop dx
    pop bx
    pop ax
    stc
    ret

; AX = the row containing offset SI. Preserves everything else.
;
; It reads the same cache tp_row_at keeps, for the same reason: this runs on
; every editing keystroke (tp_edmark) and the offset it is asked about is
; almost always inside the pane, which is where the cache already points. A
; walk from the top of an 8KB document is ~26ms on the target machine and it
; would be paid per character typed.
tp_row_of:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov dx, si
    xor si, si
    xor ax, ax
    mov bx, [tp_rc_row]
    cmp bx, 0xFFFF
    je .lp
    mov cx, [tp_rc_off]
    cmp cx, dx
    ja .lp                      ; the cache is PAST the offset: start at the top
    mov si, cx
    mov ax, bx
.lp:
    call tp_row_ext
    jc .o
    cmp dx, di
    jb .o                       ; the offset is inside this row
    inc ax
    mov si, di
    jmp short .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; AX = how many rows START in [SI, DI). Preserves everything else.
tp_rows_between:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov dx, di
    xor ax, ax
.lp:
    cmp si, dx
    jae .o
    call tp_row_ext
    jc .o
    inc ax
    mov si, di
    jmp short .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; SI = the start of the logical LINE containing SI (ES = tp_srcseg).
tp_lstart:
    push ax
    push bx
    mov bx, [tp_srclen]
    cmp si, bx
    jbe .ok
    mov si, bx
.ok:
    or si, si
    jz .o
.lp:
    dec si
    mov al, [es:si]
    cmp al, 10
    je .aft
    cmp al, 13
    je .aft
    or si, si
    jnz .lp
    jmp short .o
.aft:
    inc si
.o:
    pop bx
    pop ax
    ret

; SI = the start of the logical line AFTER the one containing SI, or the end
; of the document (ES = tp_srcseg).
tp_lnext:
    push ax
    push bx
    mov bx, [tp_srclen]
.lp:
    cmp si, bx
    jae .o
    mov al, [es:si]
    inc si
    cmp al, 10
    je .o
    cmp al, 13
    jne .lp
    cmp si, bx
    jae .o
    cmp byte [es:si], 10
    jne .o
    inc si
.o:
    pop bx
    pop ax
    ret

; AX = how many ROWS the document has - the scroll bar's `total`.
;
; The walk reads every character of the document, and tp_ssb_sync asks for it
; on every keystroke, every scroll step and every mouse report of a live thumb
; drag - ~26ms per 8KB on the target machine, per report. So the answer is
; KEPT, and thrown away where the row cache is (tp_row_inval: every edit) and
; when the wrap width moves under it, which is a different set of rows.
tp_count_rows:
    push cx
    push dx
    push si
    push di
    push es
    call tp_row_w               ; AX = the width the count would be taken at
    cmp byte [tp_nrowsok], 0
    je .walk
    cmp ax, [tp_nrowsw]
    jne .walk
    mov ax, [tp_nrows]
    jmp short .o
.walk:
    mov [tp_nrowsw], ax
    mov es, [tp_srcseg]
    xor si, si
    xor ax, ax
.lp:
    call tp_row_ext
    jc .done
    inc ax
    mov si, di
    jmp short .lp
.done:
    mov [tp_nrows], ax
    mov byte [tp_nrowsok], 1
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
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

; out AX = the caret's ROW, BX = its column in that row.
;
; A caret at the offset where one row ends and the next begins belongs to the
; EARLIER row when the break is a wrapped space - the space is at the end of
; the first row and that is where the caret is drawn - and to the LATER row
; when the break is hard, where the offset is the next row's first cell and
; the earlier row has no column left to draw it in.
tp_cur_lc:
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov dx, [tp_cur]
    cmp dx, [tp_srclen]
    jbe .ok
    mov dx, [tp_srclen]
.ok:
    xor si, si
    xor ax, ax
    xor bx, bx
    mov cx, [tp_rc_row]         ; the same cache, and this is the hottest
    cmp cx, 0xFFFF              ; reader of it: the caret's row is asked for
    je .lp                      ; on every keystroke and every repaint
    mov bx, [tp_rc_off]
    cmp bx, dx
    ja .rst
    mov si, bx
    mov ax, cx
.rst:
    xor bx, bx
.lp:
    call tp_row_ext
    jc .o
    mov bx, si
    add bx, cx                  ; BX = one past the row's last cell
    cmp dx, bx
    ja .next
    jb .in
    cmp di, bx
    je .next                    ; a hard break: this offset opens the next row
.in:
    mov bx, dx
    sub bx, si
    jmp short .o
.next:
    mov si, di
    inc ax
    xor bx, bx
    jmp short .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; White-fill the source pane and redraw it. Fallback when gfx_scroll
; refuses, or when the view jumped too far for a blit.
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
    mov byte [tp_caret_on], 0
    mov byte [tp_dselon], 0
    call tp_draw_source
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; erows/ecols from the edit box. CF=1 if the pane is too small to letter.
tp_src_metrics:
    push ax
    push bx
    call tp_edit_box
    mov ax, [tp_ey2]
    sub ax, [tp_ey1]
    sub ax, 4
    js .bad
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_erows], ax
    or ax, ax
    jz .bad
    mov ax, [tp_ex2]
    sub ax, [tp_ex1]
    sub ax, 6
    js .bad
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_ecols], ax
    pop bx
    pop ax
    clc
    ret
.bad:
    pop bx
    pop ax
    stc
    ret

; Fill the 7-word source bar block (SPEC.md 13.10).
tp_ssb_sync:
    push ax
    call tp_count_rows
    mov [tp_r_ssb+8], ax
    mov ax, [tp_erows]
    or ax, ax
    jnz .f
    mov ax, 8
.f:
    mov [tp_r_ssb+10], ax
    mov ax, [tp_vscroll]
    mov [tp_r_ssb+12], ax
%ifdef OS88UI_SBDRAG
    push bx
    mov bx, tp_r_ssb
    call os88ui_sbfix
    pop bx
%endif
    pop ax
    ret

; Draw the source bar whole, and remember what it was drawn from.
tp_ssb_draw:
    push ax
    push bx
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbar
    call tp_ssb_kept
    pop bx
    pop ax
    ret

; The thumb ALONE - three drawing calls against the sixteen a whole bar costs
; (SPEC.md 13.10.3), which is the difference on every arrow key. os88ui_sbmove
; may only be used when total and fit are what the bar on the glass was drawn
; from: it derives the old thumb by putting the old POS back, and a total that
; moved under it would leave a fragment of the old thumb behind. A line added
; or deleted since the last paint is therefore a whole bar, once.
tp_ssb_move:
    push ax
    push bx
    call tp_ssb_sync
    mov ax, [tp_r_ssb+8]
    cmp ax, [tp_ssbtot]
    jne .whole
    mov ax, [tp_r_ssb+10]
    cmp ax, [tp_ssbfit]
    jne .whole
    mov ax, [tp_ssbpos]
    mov bx, tp_r_ssb
    call os88ui_sbmove
    jmp short .kept
.whole:
    mov bx, tp_r_ssb
    call os88ui_sbar
.kept:
    call tp_ssb_kept
    pop bx
    pop ax
    ret

; What the bar on the glass is drawn from, for the comparison above.
tp_ssb_kept:
    push ax
    mov ax, [tp_r_ssb+8]
    mov [tp_ssbtot], ax
    mov ax, [tp_r_ssb+10]
    mov [tp_ssbfit], ax
    mov ax, [tp_r_ssb+12]
    mov [tp_ssbpos], ax
    pop ax
    ret

; DI = visible row. White-fill the 8px band and letter it. Preserves DI.
tp_draw_srow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, di
    mov bl, 8
    mul bl
    mov dx, ax
    add dx, [tp_ey1]
    add dx, 3
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    inc ax
    mov bx, dx
    mov cx, [tp_ex2]
    add dx, 7
    call OSAPI_GFX_FILL
    pop dx
    mov ax, [tp_srcseg]
    or ax, ax
    jz .o
    mov es, ax
    mov ax, [tp_vscroll]
    add ax, di
    call tp_row_at              ; SI = its start, CX = its length, and a row
    jc .o                       ; never needs clipping: it IS what fits
    push dx
    push di
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
    cld
    rep movsb
.nul:
    pop ds
    pop cx
    pop es
    mov bx, cx
    mov byte [tp_linebuf+bx], 0
    pop di
    pop dx
    mov cx, [tp_ex1]
    add cx, 3
    mov si, tp_linebuf
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    call tp_draw_mark           ; ...and the wrap mark, if this row continues
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; DI = a visible row, DX = its top. Letter TP_WRAP_MARK in the column kept
; for it when [tp_rowcont] says the row is continued by the next one.
;
; It is a FONT_RUN of its own rather than a padded line, and that is the whole
; point: padding the row out to the mark with spaces would letter forty cells
; to draw one, at PERFORMANCE.md's ~900us each.
tp_draw_mark:
    cmp byte [tp_rowcont], 0
    je .o
    push ax
    push bx
    push cx
    push dx
    push si
    call tp_row_w               ; AX = the width, which IS the mark's column
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3
    mov cx, ax
    mov si, tp_s_wrap
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.o:
    ret

; XOR the 8x8 cell at tp_cxp/tp_cyp.
tp_xor_caret:
    push ax
    push bx
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_cxp]
    mov bx, [tp_cyp]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_XOR_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_caret_hide:
    cmp byte [tp_caret_on], 0
    je .o
    call tp_xor_caret
    mov byte [tp_caret_on], 0
.o:
    ret

tp_caret_show:
    cmp byte [tp_focus], 0
    jne .o
    cmp byte [tp_selon], 0
    jne .o
    call tp_caret_xy
    jc .o
    call tp_xor_caret
    mov byte [tp_caret_on], 1
.o:
    ret

; AX=start DX=end exclusive, any order. Invert the selected cells on screen.
tp_xor_span:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp ax, dx
    je .out
    jbe .ord
    xchg ax, dx
.ord:
    mov [tp_tmp], ax            ; start
    mov [tp_tmp2], dx           ; end
    xor di, di
.row:
    cmp di, [tp_erows]
    jae .out
    mov ax, [tp_vscroll]
    add ax, di
    call tp_row_at
    jc .out
    mov bx, si                  ; the row's start
    add si, cx
    mov dx, si                  ; line end
    mov ax, [tp_tmp]
    cmp ax, dx
    jae .n
    mov ax, [tp_tmp2]
    cmp ax, bx
    jbe .n
    mov ax, [tp_tmp]
    cmp ax, bx
    jae .s0
    mov ax, bx
.s0:
    mov cx, [tp_tmp2]
    cmp cx, dx
    jbe .s1
    mov cx, dx
.s1:
    cmp ax, cx
    jae .n
    sub ax, bx                  ; start col
    sub cx, bx                  ; end col
    cmp ax, 0
    jge .c0
    xor ax, ax
.c0:
    cmp cx, 0
    jle .n
    cmp ax, [tp_ecols]
    jae .n
    cmp cx, [tp_ecols]
    jbe .c1
    mov cx, [tp_ecols]
.c1:
    cmp ax, cx
    jae .n
    push di
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3                   ; x1
    push ax
    mov ax, cx
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3
    dec ax
    mov cx, ax                  ; x2
    pop ax
    mov dx, di
    mov bl, 8
    xchg ax, dx
    mul bl
    add ax, [tp_ey1]
    add ax, 3
    xchg ax, dx                 ; DX=y, AX=x1
    mov bx, dx
    add dx, 7
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_XOR_FILL
    pop di
.n:
    inc di
    jmp .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_sel_hide:
    push ax                     ; the callers hold live values in AX/DX across
    push dx                     ; this - tp_ondrag its anchor and caret pair,
    cmp byte [tp_dselon], 0     ; tp_src_scroll the rows the view moved
    je .o
    mov ax, [tp_dsel0]
    mov dx, [tp_dsel1]
    call tp_xor_span
    mov byte [tp_dselon], 0
.o:
    pop dx
    pop ax
    ret

tp_sel_show:
    cmp byte [tp_selon], 0
    je .o
    mov ax, [tp_sel0]
    mov dx, [tp_sel1]
    cmp ax, dx
    je .o
    call tp_xor_span
    mov ax, [tp_sel0]
    mov [tp_dsel0], ax
    mov ax, [tp_sel1]
    mov [tp_dsel1], ax
    mov byte [tp_dselon], 1
.o:
    ret

tp_sel_clear:
    call tp_sel_hide
    mov byte [tp_selon], 0
    ret

; Delete the live selection. CF=1 there was none.
tp_sel_del:
    push ax
    push bx
    cmp byte [tp_selon], 0
    je .none
    call tp_sel_hide
    mov ax, [tp_sel0]
    mov [tp_cur], ax
    mov bx, [tp_sel1]
    sub bx, ax
    mov [tp_tmp3], bx
    mov bx, ax
    call tp_del_range2
    mov byte [tp_selon], 0
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
    mov byte [tp_edkind], 0
    pop bx
    pop ax
    clc
    ret
.none:
    pop bx
    pop ax
    stc
    ret

; AX and DX become the live selection (either order). Empty = none.
tp_sel_set:
    push ax
    push dx
    cmp ax, dx
    jbe .ord
    xchg ax, dx
.ord:
    cmp ax, dx
    je .none
    mov [tp_sel0], ax
    mov [tp_sel1], dx
    mov byte [tp_selon], 1
    jmp short .o
.none:
    mov byte [tp_selon], 0
.o:
    pop dx
    pop ax
    ret

; Caret / selection on glass, after the document positions changed and
; the view did not scroll. One XOR off, one XOR on; not a line.
tp_marks_sync:
    call tp_caret_hide
    call tp_sel_hide
    call tp_sel_show
    call tp_caret_show
    ret

; Focus rings only - selecting a pane does not redraw its contents.
tp_draw_frames:
    push ax
    push bx
    push cx
    push dx
    call tp_edit_box
    call tp_prev_box
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    mov bx, [tp_ey1]
    mov cx, [tp_r_ssb+4]
    mov dx, [tp_ey2]
    call OSAPI_GFX_FRAME
    dec ax
    dec bx
    inc cx
    inc dx
    cmp byte [tp_focus], 0
    je .srcf
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    jmp short .prv
.srcf:
    call OSAPI_GFX_FRAME
.prv:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_px1]
    mov bx, [tp_py1]
    mov cx, [tp_px2]
    mov dx, [tp_py2]
    call OSAPI_GFX_FRAME
    dec ax
    dec bx
    inc cx
    inc dx
    cmp byte [tp_focus], 1
    je .prvf
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    jmp short .o
.prvf:
    call OSAPI_GFX_FRAME
.o:
    ; A FOCUS RING IS DRAWN ONE PIXEL OUTSIDE THE PANE, so the ring that is
    ; ERASED (white, the pane that just lost focus) takes with it whatever
    ; else lives on those four lines: the SPLITTER down the right of the
    ; source pane, and the status strip's RULE along the bottom of both.
    ; Neither belongs to this routine and both have to be put back, or a
    ; click on the other pane leaves a white gap where they were.
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]
    add ax, [tp_split]
    inc ax
    mov bx, [tp_py1]
    mov dx, [tp_py2]
    call OSAPI_GFX_VLINE
    mov ax, [tp_ox]
    mov bx, ax
    add bx, [tp_cw]
    dec bx                      ; hline: AX=x1, BX=x2, DX=y
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, TP_STAT_H
    call OSAPI_GFX_HLINE
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = the FIRST visible row the blit may move, SI = signed pixel dy (positive
; scrolls the content UP). CF = gfx_scroll's answer. Preserves AX.
;
; BOTH EDGES ROUND DOWN, and they are not the same decision.
;
; gfx_scroll is byte-column granular (SPEC.md 5.5), so x1 and x2+1 have to be
; multiples of 8 and the rect can only be widened or narrowed by whole cells.
; The text pen is at [tp_ex1]+3, which is NOT on a byte boundary, so a rect
; that starts at the next boundary UP leaves the first four columns of the
; first character cell behind - measured: four stale pixel columns down every
; row a Return pushed, the whole height of the pane. So x1 goes DOWN, to the
; content origin, which SPEC.md 11.94 keeps on a multiple of 8 for us: what
; that takes in with it is the pane's own left frame and padding, ours and
; uniform, and a vertical line moved vertically is the same line.
;
; The RIGHT edge has the source scroll bar immediately beyond it, so x2+1
; goes down too - away from it. Rounding that one up instead sheared up to
; seven columns of the bar by 8px on every Return, which is what a blit is
; least for. Nothing is lost: [tp_ex2] is padding past the last whole cell.
tp_src_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, 8
    mul bl
    add ax, [tp_ey1]
    add ax, 3
    mov bx, ax                  ; y1 = that row's top
    mov ax, [tp_ex1]
    inc ax
    and ax, 0xFFF8              ; x1, down to a byte column...
    cmp ax, [tp_ox]
    jae .x1
    mov ax, [tp_ox]             ; ...but never past the content's own left
.x1:
    mov cx, [tp_ex2]
    inc cx
    and cx, 0xFFF8
    dec cx                      ; x2, x2+1 down to a byte column
    cmp cx, ax
    jb .no
    mov dx, [tp_erows]
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [tp_ey1]
    add dx, 3
    dec dx
    cmp dx, [tp_ey2]
    jb .yok
    mov dx, [tp_ey2]
    dec dx
.yok:
    cmp bx, dx
    ja .no
    call OSAPI_GFX_SCROLL
    jmp .o
.no:
    stc
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; View moved to [tp_vscroll]. Blit + new rows, or a full pane if that fails.
tp_src_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_src_metrics
    jc .full
    mov ax, [tp_vscroll]
    mov bx, [tp_oldvs]
    sub ax, bx                  ; AX = d rows (new - old)
    jz .marks
    mov dx, ax
    or dx, dx
    jns .abs
    neg dx
.abs:
    cmp dx, [tp_erows]
    jae .full
    call tp_caret_hide
    call tp_sel_hide
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    push ax
    xor ax, ax                  ; the whole pane moves: from visible row 0
    call tp_src_blit
    pop ax                      ; (pop leaves gfx_scroll's CF alone)
    jc .full
    or ax, ax
    js .up
    mov di, [tp_erows]
    sub di, ax
.dn:
    cmp di, [tp_erows]
    jae .after
    call tp_draw_srow
    inc di
    jmp .dn
.up:
    neg ax
    xor di, di
.upl:
    cmp di, ax
    jae .after
    call tp_draw_srow
    inc di
    jmp .upl
.after:
    call tp_sel_show
    call tp_caret_show
    call tp_ssb_move
    mov ax, [tp_vscroll]
    mov [tp_oldvs], ax
    jmp .o
.marks:
    call tp_marks_sync
    jmp .o
.full:
    call tp_redraw_src
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; After a caret key: scroll the pane if needed, else XOR the caret.
tp_after_caret:
    push ax
    push bx
    mov ax, [tp_vscroll]
    call tp_keep_caret
    cmp ax, [tp_vscroll]
    jne .sc
    call tp_marks_sync
    jmp .o
.sc:
    call tp_src_scroll          ; ...which owns the whole-pane fallback itself
.o:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tp_edmark - what the edit about to happen is, in ROWS
;
; Wrapping makes an edit's reach a question that cannot be answered after the
; fact: typing one character can push a word onto a new row and move every row
; below it, or pull one back and take a row away. So the shape is measured
; HERE, before the edit, as a span of the document [tp_edls, tp_edtail) and
; the number of rows it occupied; tp_after_edit measures the same span again
; and the difference is how far everything below has moved.
;
; The span starts at the caret's own LINE - or the one before it, because a
; Backspace at a line start joins the two - and ends two lines on, because a
; Delete at a line's end joins the next one in.
; -----------------------------------------------------------------------------
tp_edmark:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov byte [tp_edkind], 0
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    call tp_lstart
    cmp si, [tp_cur]
    jne .ls
    or si, si
    jz .ls
    dec si
    call tp_lstart              ; a join takes the line above with it
.ls:
    mov [tp_edls], si
    call tp_row_of
    mov [tp_edrf], ax
    mov si, [tp_cur]
    call tp_lnext
    call tp_lnext
    mov [tp_edtail], si
    mov di, si
    mov si, [tp_edls]
    call tp_rows_between
    mov [tp_edkb], ax
    mov ax, [tp_srclen]
    mov [tp_edlen], ax
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; [tp_edka] = what that span spans NOW. Every offset after the edit moved by
; the bytes the document gained, the tail included.
tp_ed_ka:
    push ax
    push si
    push di
    mov ax, [tp_srclen]
    sub ax, [tp_edlen]
    add ax, [tp_edtail]
    mov di, ax
    mov si, [tp_edls]
    call tp_rows_between
    mov [tp_edka], ax
    pop di
    pop si
    pop ax
    ret

; DI = the first visible row, CX = how many. Draw them, stopping at the pane.
tp_rows_redraw:
    push ax
    push cx
    push di
.lp:
    or cx, cx
    jz .o
    cmp di, [tp_erows]
    jae .o
    call tp_draw_srow
    inc di
    dec cx
    jmp short .lp
.o:
    pop di
    pop cx
    pop ax
    ret

; After an edit at the caret. edkind: 1=ins char 2=ins nl 3=del char 4=del nl
; 0 = something else, full pane.
tp_after_edit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_caret_hide
    call tp_sel_hide
    mov ax, [tp_vscroll]
    call tp_keep_caret
    mov bx, [tp_vscroll]
    cmp ax, bx
    je .same
    ; THE VIEW MOVED AND THE DOCUMENT CHANGED, which tp_src_scroll cannot do:
    ; it blits the old pixels by the rows the view moved and letters the band
    ; that came in, and the rows the EDIT moved are inside that blit.
    ; Measured with a two-line selection typed over: every row below the
    ; deletion stayed where it was, two lines stale.
    ;
    ; ONE SHAPE IS STILL A BLIT, and it is the one a person meets constantly:
    ; typing at the BOTTOM of the pane. The view goes down exactly one row,
    ; and an insert or a delete inside a line only changed the caret's line -
    ; which is now the last row, the one the blit vacates anyway. A Return
    ; owes the line it broke as well, one row above. Everything else falls
    ; through to the whole pane.
    sub bx, ax                  ; BX = rows the view moved
    cmp bx, 1
    jne .full
    mov al, [tp_edkind]
    or al, al
    jz .full
    call tp_ed_ka               ; ...and the edit must not have RESHAPED the
    mov ax, [tp_edka]           ; rows under it, or one blit cannot describe
    cmp ax, [tp_edkb]           ; what moved (wrapping makes that ordinary:
    jne .full                   ; one typed character can push a word down)
.sc1:
    call tp_src_metrics
    jc .full
    mov si, 8                   ; the pane's content up one row
    xor ax, ax
    call tp_src_blit
    jc .full
    mov di, [tp_edrf]
    sub di, [tp_vscroll]
    js .full
    mov cx, [tp_edka]
    call tp_rows_redraw         ; the span, wherever the scroll left it
    mov ax, [tp_vscroll]
    mov [tp_oldvs], ax
    call tp_caret_show
    call tp_ssb_move            ; the thumb moved, and a Return moved `total`
    jmp .strip
.full:
    call tp_redraw_src
    jmp .stat
.same:
    ; THE VIEW DID NOT MOVE, so what is on screen changed only where the edit
    ; reached: the rows of the span tp_edmark measured, plus - if that span
    ; now takes a different number of rows - everything below it, which moves
    ; by the difference and is a BLIT rather than a repaint.
    cmp byte [tp_edkind], 0
    je .full                    ; an edit nobody measured (a selection went)
    call tp_ed_ka
    mov di, [tp_edrf]
    sub di, [tp_vscroll]
    js .full                    ; the span starts above the pane
    cmp di, [tp_erows]
    jae .stat                   ; ...or below it: nothing on screen moved
    mov ax, [tp_edka]
    cmp ax, [tp_edkb]
    je .blk                     ; the rows below did not move
    mov ax, di
    add ax, [tp_edkb]           ; the first row below the OLD span - the
    cmp ax, [tp_erows]          ; blit's SOURCE start, and the gate: off the
    jae .blk                    ; pane, the bottom redraw below owns it whole
    mov ax, [tp_edka]           ; ...but the RECT anchors at the DESTINATION,
    cmp ax, [tp_edkb]           ; min(edka, edkb): gfx_scroll puts the dest
    jb .anc                     ; at the rect TOP going up and the BOTTOM
    mov ax, [tp_edkb]           ; coming down, so anchoring a shrink at
.anc:                           ; di+edkb left rows [di+edka, di+edkb) never
    add ax, di                  ; written - one row duplicated, one dropped,
                                ; on every mid-pane join
    mov si, [tp_edkb]
    sub si, [tp_edka]           ; rows the tail moves UP...
    shl si, 1
    shl si, 1
    shl si, 1                   ; ...times 8, which is gfx_scroll's dy
    push di
    call tp_src_blit
    pop di
    jc .full
.blk:
    mov cx, [tp_edka]
    call tp_rows_redraw
    mov ax, [tp_edkb]
    cmp ax, [tp_edka]
    jbe .caret
    sub ax, [tp_edka]           ; the span SHRANK: the rows it gave back are
    mov cx, ax                  ; stale at the bottom of the pane
    mov di, [tp_erows]
    sub di, cx
    js .caret
    call tp_rows_redraw
.caret:
    call tp_caret_show
    call tp_ssb_move
    jmp .strip
.stat:
    ; A Return or a join changed the LINE COUNT, which is the scroll bar's
    ; `total`: the thumb is a different size and in a different place, and
    ; nothing else on this path would have said so until the next scroll.
    ; tp_ssb_move draws the whole bar when total moved and three calls when
    ; it did not, so this is the cheap version of "keep the bar honest".
    mov al, [tp_edkind]
    cmp al, 2
    je .bar
    cmp al, 4
    jne .strip
.bar:
    call tp_ssb_move
.strip:
    call tp_stat_touch
    mov byte [tp_edkind], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The BYTE COUNT in the status strip, and NOTHING else on the window.
;
; Everything to the left of it - the paper, the margin, the binding, the
; gutter, the numbering, the body size - can only be changed from a menu or a
; bar button, and every one of those repaints the window. A keystroke moves
; the count and the count only.
;
; So this is ONE font_run of about eleven cells, and deliberately not a fill
; and a run: font_run is erase-and-letter in one decision per cell (SPEC.md
; 6.1), so the cells it letters need no clearing first - and the two SPACES
; appended below cover the one case that would otherwise leave a stale cell,
; a count losing a digit and pulling '/8190' left with it. What that buys is
; not the 756us of the fill: it is that nothing here touches the right-hand
; end of the strip any more, so the GROW BOX is not erased and needs no
; OSAPI_WM_GROW to put it back, and the top bar is not re-lettered.
tp_stat_touch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tp_nbuf
    call tp_fmt_stat            ; leaves DI on the terminator
    mov word [di], '  '         ; ...and a count that SHRANK pulls the rest of
    mov byte [di+2], 0          ; the field left, so letter two cells of slack
    mov ax, [tp_statcnt]
    sub ax, tp_nbuf             ; AX = the count's column in the line
    mov bl, 8
    mul bl
    add ax, [tp_ox]
    add ax, 6                   ; ...and its x, from tp_draw_stat's pen
    mov cx, [tp_ox]
    add cx, [tp_cw]
    dec cx
    cmp cx, ax
    jb .bar                     ; the strip is narrower than the count's column
    mov cx, ax
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, 11
    mov si, [tp_statcnt]
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.bar:
    ; The top bar carries the file name, the * and the (edit) marker, and a
    ; keystroke can change the last two - ONCE. Re-lettering that strip on
    ; every character typed is ~30 more cells for a picture that is already
    ; right, so this compares what the bar was drawn from against what it
    ; would be drawn from now, and draws only on the edge.
    call tp_barsig
    cmp al, [tp_barsig_v]
    je .o
    call tp_bar_status          ; ...which re-signs it through tp_fmt_bar
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL = what the top bar's TEXT depends on and a keystroke can move. The page
; number and the file name are on it too and neither can change without a
; full repaint (a page turn, a load, a typeset).
tp_barsig:
    xor al, al
    cmp byte [tp_dirty], 0
    je .ns
    or al, 1
.ns:
    cmp byte [tp_needset], 0
    je .o
    or al, 2
.o:
    ret

; Redraw the filename/page text beside the buttons, not the buttons.
tp_bar_status:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_r_next+4]
    add ax, 4
    mov bx, [tp_oy]
    inc bx
    mov cx, [tp_ox]
    add cx, [tp_cw]
    dec cx
    mov dx, [tp_oy]
    add dx, TP_BAR_H
    sub dx, 2
    call OSAPI_GFX_FILL
    call tp_fmt_bar
    mov cx, [tp_r_next+4]
    add cx, 8
    mov dx, [tp_oy]
    add dx, 6
    mov si, tp_status
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Preview: blit the pane and letter only the uncovered band.
tp_psb_sync:
    push ax
    push bx
    call tp_fit_sheet
    mov ax, [tp_sy2]
    sub ax, [tp_sy1]
    mov [tp_r_psb+8], ax
    mov ax, [tp_py2]
    sub ax, [tp_py1]
    sub ax, 8
    cmp ax, 16
    jae .f
    mov ax, 16
.f:
    mov [tp_r_psb+10], ax
    mov ax, [tp_pscroll]
    mov [tp_r_psb+12], ax
%ifdef OS88UI_SBDRAG
    mov bx, tp_r_psb            ; SPEC.md 13.10.5.6, and 13.10.5.10 decides
    call os88ui_sbfix           ; whether it is THIS bar's gesture
%endif
    pop bx
    pop ax
    ret

tp_clamp_ps:
    push ax
    push bx
    ; AN EVEN SCROLL, ALWAYS. The desk is a 50% dither and its phase is a
    ; function of the absolute (x, y) of each pixel, so a blit by an ODD
    ; number of rows moves the checkerboard onto the wrong parity and the
    ; strip beside the page comes out inverted against everything a full
    ; repaint draws. One pixel of preview scroll is meaningless at this
    ; scale; the phase is not.
    and word [tp_pscroll], 0xFFFE
    call tp_fit_sheet
    mov ax, [tp_sy2]
    sub ax, [tp_sy1]
    mov bx, [tp_py2]
    sub bx, [tp_py1]
    sub bx, 8
    cmp bx, 16
    jae .ph
    mov bx, 16
.ph:
    sub ax, bx
    jnc .ok
    xor ax, ax
.ok:
    and ax, 0xFFFE              ; the max EVENED, like the scroll itself was
                                ; at entry: sheet_height - fit is ODD for A4
                                ; at 12pt (561), and a clamp landing there
                                ; handed tp_prev_scroll an odd dy - the 50%
                                ; desk dither blitted onto inverted parity
                                ; beside the sheet until a full repaint
    cmp [tp_pscroll], ax
    jbe .o
    mov [tp_pscroll], ax
.o:
    pop bx
    pop ax
    ret

tp_redraw_prev:
    push ax
    push bx
    push cx
    push dx
    call tp_prev_box
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_px1]
    inc ax
    mov bx, [tp_py1]
    inc bx
    mov cx, [tp_px2]
    dec cx
    mov dx, [tp_py2]
    dec dx
    call OSAPI_GFX_FILL
    call tp_draw_preview
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE PREVIEW SCROLLS BY BLITTING TOO, and it is the pane where that matters
; most: a page of set text is forty-odd FONT_RUNs of fifty cells, which is
; PERFORMANCE.md's ~900us a cell - the better part of two seconds on the
; target machine for a scroll of four lines.
;
; The reason it did not, before, is written into the routine it replaces: a
; blit that letters only the vacated strip misses the gutter, the rules, the
; sheet's own edges and any run whose 8-row cell straddles the band. So the
; band is not lettered here - it is DRAWN, by the same routines that draw the
; whole pane, cut to the band by tp_rgn_set. A run whose cell straddles the
; edge is drawn WHOLE, and the rows of it that land outside the band are the
; same pixels the blit just put there.
; -----------------------------------------------------------------------------
tp_prev_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    call tp_clamp_ps
    mov ax, [tp_pscroll]
    mov si, ax
    sub si, [tp_oldps]          ; SI = dy, positive = the page moves UP
    jz .out
    mov cx, [tp_py2]
    sub cx, [tp_py1]            ; the pane's height
    mov dx, si
    or dx, dx
    jns .abs
    neg dx
.abs:
    cmp dx, cx
    jae .full                   ; further than the pane: nothing to keep
    call tp_prev_box
    call tp_fit_sheet           ; the sheet at the NEW scroll position
    mov ax, [tp_px1]
    inc ax                      ; the interior, both edges on byte columns by
    mov bx, [tp_py1]            ; construction (tp_prev_box)
    inc bx
    mov cx, [tp_px2]
    dec cx
    mov dx, [tp_py2]
    dec dx
    call OSAPI_GFX_SCROLL
    jc .full
    ; the band it vacated: the bottom |dy| rows going up, the top going down
    mov ax, [tp_px1]
    inc ax
    mov cx, [tp_px2]
    dec cx
    or si, si
    js .up
    mov dx, [tp_py2]
    dec dx
    mov bx, dx
    sub bx, si
    inc bx
    jmp short .band
.up:
    mov bx, [tp_py1]
    inc bx
    mov dx, bx
    sub dx, si                  ; SI is negative here
    dec dx
.band:
    call tp_prev_band           ; the rows the blit vacated...
    ; ...AND EIGHT ROWS AT THE OTHER END. A run whose 8-row cell straddles
    ; the pane's edge is not drawn at all (a glyph cannot be cut), so the
    ; edge holds a HALF cell that the blit then moves into full view - where
    ; a full repaint draws nothing. Repainting a cell's worth at the far edge
    ; is what makes the two agree.
    mov ax, [tp_px1]
    inc ax
    mov cx, [tp_px2]
    dec cx
    or si, si
    js .fup
    mov bx, [tp_py1]
    inc bx
    mov dx, bx
    add dx, 7
    jmp short .far
.fup:
    mov dx, [tp_py2]
    dec dx
    mov bx, dx
    sub bx, 7
.far:
    call tp_prev_band
.done:
    call tp_rgn_all
    call tp_psb_move            ; the thumb, not the bar
    mov ax, [tp_pscroll]
    mov [tp_oldps], ax
    jmp short .out
.full:
    call tp_rgn_all
    call tp_redraw_prev
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX..DX = a band of the preview pane; paint everything the pane has in it.
; The sheet first, because the desk and the paper are what the rest is drawn
; ON, then the page itself. A run whose cell straddles the band is drawn
; WHOLE and the rows of it outside are the same pixels that are already
; there, so nothing has to be cut per pixel - which is just as well, because
; a package has no clip region to do it with (OSAPI_WM_CLIP_SET is the window
; manager's, about what covers you).
tp_prev_band:
    push ax
    push bx
    push cx
    push dx
    call tp_rgn_set
    call tp_draw_sheet
    cmp byte [tp_setok], 0
    je .o
    call tp_paint_runs
    call tp_paint_boxes
    call tp_draw_folio
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The preview bar's thumb alone, on tp_ssb_move's rule: total and fit here are
; the sheet's height and the pane's, and a scroll moves neither.
tp_psb_move:
    push ax
    push bx
    call tp_psb_sync
    mov ax, [tp_r_psb+8]
    cmp ax, [tp_psbtot]
    jne .whole
    mov ax, [tp_r_psb+10]
    cmp ax, [tp_psbfit]
    jne .whole
    mov ax, [tp_psbpos]
    mov bx, tp_r_psb
    call os88ui_sbmove
    jmp short .kept
.whole:
    mov bx, tp_r_psb
    call os88ui_sbar
.kept:
    mov ax, [tp_r_psb+8]
    mov [tp_psbtot], ax
    mov ax, [tp_r_psb+10]
    mov [tp_psbfit], ax
    mov ax, [tp_r_psb+12]
    mov [tp_psbpos], ax
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
    call tp_rgn_all             ; this entry is the WHOLE pane, always
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
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbar
    mov ax, [tp_r_psb+8]
    mov [tp_psbtot], ax         ; what the bar on the glass was drawn from,
    mov ax, [tp_r_psb+10]       ; for tp_psb_move's comparison
    mov [tp_psbfit], ax
    mov ax, [tp_r_psb+12]
    mov [tp_psbpos], ax
    mov ax, [tp_pscroll]
    mov [tp_oldps], ax
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
    push ax                     ; A CELL IS EIGHT ROWS and this is its TOP: a
    mov ax, dx                  ; run one row above the pane's bottom frame
    add ax, 7                   ; letters seven rows THROUGH it, and what is
    cmp ax, [tp_py2]            ; under it is the status strip. Measured as
    jae .nope                   ; two rows of page text in the strip, which
    cmp ax, [tp_rgy1]           ; only ever showed up as a full repaint
    jb .nope                    ; disagreeing with an incremental one. The
    cmp dx, [tp_rgy2]           ; region tests are the run painter's
    ja .nope
    pop ax
    jmp short .pen
.nope:
    pop ax
    jmp .done
.pen:
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
    mov ax, dx                  ; ...the same eight rows as the folio above
    add ax, 7
    cmp ax, [tp_py2]
    jae .n
    cmp ax, [tp_rgy1]           ; does the CELL reach the region at all? A run
    jb .n                       ; that straddles its edge is drawn whole - the
    cmp dx, [tp_rgy2]           ; rows outside are the same pixels the blit
    ja .n                       ; put there
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
    cmp bx, [tp_rgy2]           ; ...and the REGION, so a band redraw draws
    jg .sk                      ; the part of a rule that is in the band
    cmp dx, [tp_rgy1]
    jl .sk
    cmp ax, [tp_px1]
    jge .a1
    mov ax, [tp_px1]
.a1:
    cmp bx, [tp_py1]
    jge .b1
    mov bx, [tp_py1]
.b1:
    cmp bx, [tp_rgy1]
    jge .b2
    mov bx, [tp_rgy1]
.b2:
    cmp cx, [tp_px2]
    jle .c1
    mov cx, [tp_px2]
.c1:
    cmp dx, [tp_py2]
    jle .d1
    mov dx, [tp_py2]
.d1:
    cmp dx, [tp_rgy2]
    jle .d2
    mov dx, [tp_rgy2]
.d2:
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
    call tp_typeset             ; as File > New above
    jmp .draw
.kcopy:
    call tp_copy_all
    jmp .draw
.kpaste:
    call tp_paste
    mov byte [tp_selon], 0      ; ...and every offset after the caret moved
    jmp .draw
.kcut:
    call tp_cut_line
    mov byte [tp_selon], 0
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
    ; A SELECTION THAT WENT IS NOT AN INSERT (edkind 0 = the whole pane).
    ; tp_sel_del leaves edkind 0 and CF=1 when there was nothing selected;
    ; claiming "one character into one line" over the top of it drew the
    ; caret's line and left every row a multi-line selection had moved.
    call tp_edmark              ; where this lands, in rows, BEFORE it does
    call tp_sel_del
    mov al, bl
    jnc .kins
    mov byte [tp_edkind], 1
.kins:
    call tp_ins
    jmp .etype
.kbs:
    call tp_edmark
    call tp_sel_del
    jc .kbs1
    mov byte [tp_edkind], 0
    jmp .etype
.kbs1:
    call tp_backsp
    jmp .etype
.kdel:
    call tp_edmark
    call tp_sel_del
    jc .kdel1
    mov byte [tp_edkind], 0
    jmp .etype
.kdel1:
    call tp_delete
    jmp .etype
.knl:
    call tp_edmark
    call tp_sel_del             ; ...and the same for a Return and a Tab
    mov al, 10
    jnc .knl1
    mov byte [tp_edkind], 2
.knl1:
    call tp_ins
    jmp .etype
.ktab:
    call tp_edmark
    call tp_sel_del
    mov al, ' '
    jnc .ktab1
    mov byte [tp_edkind], 1
.ktab1:
    call tp_ins
    mov al, ' '
    call tp_ins
    jmp .etype
.kleft:
    call tp_sel_clear
    call tp_go_left
    jmp .ecaret
.kright:
    call tp_sel_clear
    call tp_go_right
    jmp .ecaret
.kup:
    call tp_sel_clear
    call tp_up
    jmp .ecaret
.kdn:
    call tp_sel_clear
    call tp_down
    jmp .ecaret
.khome:
    call tp_sel_clear
    call tp_home
    jmp .ecaret
.kend:
    call tp_sel_clear
    call tp_end
    jmp .ecaret
.kpgup:
    call tp_sel_clear
    call tp_pgup
    jmp .escroll
.kpgdn:
    call tp_sel_clear
    call tp_pgdn
    jmp .escroll
.etype:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_after_edit
    jmp .out
.ecaret:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_after_caret
    jmp .out
.escroll:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_src_scroll
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
    je .out
    sub word [tp_pscroll], 24
    jnc .psc
    mov word [tp_pscroll], 0
    jmp short .psc
.psd:
    add word [tp_pscroll], 24
.psc:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_prev_scroll
    jmp .out
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
    mov byte [tp_seldrag], 0
%ifdef OS88UI_SBDRAG
    call tp_sbd_which           ; SPEC.md 13.10.5: the release COMMITS, and
    jc .btn                     ; unconditionally
    call os88ui_sbdrop
    jc .btn
    call tp_sbd_commit
    jmp .out
.btn:
%endif
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
    cmp byte [tp_seldrag], 0
    je .bar
    ; extend the source selection to the pointer; XOR only the cells that
    ; changed, never the pane.
    call tp_click_caret
    mov ax, [tp_anchor]
    mov dx, [tp_cur]
    cmp ax, dx
    jne .sel
    call tp_sel_hide
    mov byte [tp_selon], 0
    call tp_caret_show
    jmp .out
.sel:
    call tp_caret_hide
    call tp_sel_hide
    call tp_sel_set
    call tp_sel_show
    jmp .out
.bar:
%ifdef OS88UI_SBDRAG
    call tp_sbd_which           ; SPEC.md 13.10.5: a live thumb drag owns this
    jc .btn                     ; movement whole
    call os88ui_sbtrack         ; CF = 1: nothing owed - the rate, or the same
    jc .out                     ; step
    call tp_sbd_commit
    jmp .out
.btn:
%endif
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
%ifdef OS88UI_SBDRAG
    call tp_sbd_which           ; A PRESS CANNOT ARRIVE DURING A LIVE DRAG, so
    jc .nostale                 ; one that does means the release never came
    push cx                     ; (SPEC.md 13.10.5.7). AFTER tp_layout, so the
    push dx                     ; two rects the gesture is matched against are
    call os88ui_sbdrop          ; the ones on screen
    pop dx
    pop cx
.nostale:
%endif
    ; A CLICK DISMISSES THE ABOUT BOX AND DOES NOTHING ELSE, and it owes the
    ; window a repaint on the way out - the box is drawn OVER the two panes
    ; and the bar, so every incremental path below would leave it standing.
    ; Clearing the flag and then routing the click was the bug: a click on
    ; the source pane put a caret under a paragraph of About text, and a
    ; click on nothing left the box up for good. The key handler already
    ; treats a keystroke this way.
    cmp byte [tp_abouton], 0
    je .live
    mov byte [tp_abouton], 0
    jmp .draw
.live:
    ; SPEC.md 13.6/13.8: the seven bar buttons only ARM and DRAW here, and
    ; tp_onup acts. The two scroll-bar rects below keep the press - a scroll
    ; is repeatable and reversible, which is the definition of a safe prefix
    ; action, and the arrow cells are the part that would want a down state.
    call tp_bhit
    or al, al
    jz .sbars
    call tp_bdis                ; grey: it does not arm and it does not go down
    jc .out
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
    cmp byte [tp_ldfull], 0
    jne .draw
    mov al, [tp_focus]
    mov byte [tp_focus], 0
    or al, al
    jz .sames
    call tp_draw_frames
.sames:
    call tp_caret_hide
    call tp_sel_hide
    mov byte [tp_selon], 0
    call tp_click_caret
    mov ax, [tp_cur]
    mov [tp_anchor], ax
    mov byte [tp_seldrag], 1
    call tp_caret_show
    jmp .done
.prv:
    cmp byte [tp_ldfull], 0
    jne .draw
    cmp byte [tp_focus], 1
    je .done
    mov byte [tp_focus], 1
    call tp_caret_hide
    call tp_draw_frames
    jmp .done
.ssb:
    mov al, [tp_focus]
    mov byte [tp_focus], 0
    or al, al
    jz .ssb1
    call tp_draw_frames
.ssb1:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_caret_hide          ; the marks come off at the scroll they were
    call tp_sel_hide            ; DRAWN at - tp_xor_span walks rows from
                                ; [tp_vscroll], which tp_ssb_click is about to
                                ; move (tp_after_edit hides ahead of the move
                                ; for the same reason)
    call tp_ssb_click
    call tp_src_scroll
    jmp .done
.psb:
    cmp byte [tp_focus], 1
    je .psb1
    mov byte [tp_focus], 1
    call tp_caret_hide
    call tp_draw_frames
.psb1:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_psb_click
    call tp_prev_scroll
    jmp .done
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
    mov [tp_wantcol], ax
    mov ax, [tp_tline]
    call tp_goto_lc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = a ROW, [tp_wantcol] = a column in it -> tp_cur.
tp_goto_lc:
    push ax
    push bx
    push cx
    push si
    call tp_row_at
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
    call tp_typeset             ; a document that REPLACES this one is set on
    call tp_redraw              ; arrival - File > Open and a double-clicked
    ret                         ; .TEX already were, and a blank preview
                                ; beside a fresh document was the odd one out
                                ; (SPEC.md 69.1)
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
    mov byte [tp_selon], 0      ; as Ctrl+V does: the offsets moved under it
    call tp_redraw
    ret
.cut:
    call tp_cut_line
    mov byte [tp_selon], 0
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
    call tp_row_inval
    mov word [tp_cur], 0
    mov word [tp_vscroll], 0
    mov byte [tp_selon], 0      ; the selection is a pair of offsets into the
                                ; document that just went (the same for the
                                ; new document below, a cut and a paste)
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
    call tp_row_inval
    mov word [tp_cur], 0
    mov word [tp_vscroll], 0
    mov byte [tp_selon], 0
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
    call tp_row_inval           ; every other mutation invalidates the row
                                ; cache (tp_ins, tp_del_range2, tp_load,
                                ; tp_seed) - a paste ABOVE the cached offset
                                ; that skipped this left tp_cur_lc walking
                                ; rows from mid-pasted-text: wrong caret row,
                                ; wrong vscroll, until the next edit healed it
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
    ; THE LOGICAL LINE, not the row: this is the Edit menu's 'Cut Line' and
    ; what it takes out has to be a line of the FILE, newline and all.
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    call tp_lstart
    mov bx, si                  ; BX = where it starts
    call tp_lnext               ; SI = where the next one does
    mov cx, si
    sub cx, bx                  ; CX = its length, the newline included
.cut:
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
    call tp_row_inval           ; every row after this one starts somewhere
    push ax                     ; else now
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
    call tp_row_inval
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
    mov byte [tp_edkind], 3
    cmp al, 10
    je .nl
    cmp al, 13
    jne .one
.nl:
    mov byte [tp_edkind], 4
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

; A ROW up and a row down, not a line: on a wrapped line the two are not the
; same move, and the row is the one the reader is looking at.
tp_up:
    push ax
    push bx
    call tp_cur_lc
    or ax, ax
    jz .o
    mov [tp_wantcol], bx
    dec ax
    call tp_goto_lc
.o:
    pop bx
    pop ax
    ret

tp_down:
    push ax
    push bx
    push cx
    push si
    call tp_cur_lc
    mov [tp_wantcol], bx
    inc ax
    push ax
    call tp_row_at              ; is there a row below at all?
    pop ax
    jc .o
    call tp_goto_lc
.o:
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_home:
    push ax
    call tp_cur_lc
    mov word [tp_wantcol], 0
    call tp_goto_lc
    pop ax
    ret

tp_end:
    push ax
    push cx
    push si
    call tp_cur_lc
    push ax
    call tp_row_at
    pop ax
    jc .o
    add si, cx
    mov [tp_cur], si
    mov word [tp_wantcol], 255
.o:
    pop si
    pop cx
    pop ax
    ret

tp_clamp_vs:
    push ax
    push bx
    call tp_count_rows
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

%ifdef OS88UI_SBDRAG
tp_nodrag:  db 0                ; 0xFF = this kernel has no tracking edge
tp_sbd_bar: db 0                ; which bar the live gesture is on: 0 source,
                                ; 1 preview

; -----------------------------------------------------------------------------
; tp_sbd_which - which of the two bars is the live gesture on? (SPEC.md
;                13.10.7.2)
; out: CF = 1 = none of ours; CF = 0, BX = that bar's block and [tp_sbd_bar]
;      set. AX preserved.
;
; THIS APP IS WHY os88ui_sbmine EXISTS AT ALL (13.10.5.10). One gesture record,
; two bars - the source's counting lines and the preview's counting pixels -
; and the record is asked which it belongs to rather than the caller guessing
; from the pointer, because the pointer may be a long way off the bar by then
; (13.10.5.2) and a drop on the wrong bar SPENDS somebody else's gesture.
; -----------------------------------------------------------------------------
tp_sbd_which:
    call os88ui_sbdragging
    jc .no
    push ax
    call tp_src_metrics
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbmine
    jnc .src
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbmine
    jc .nope
    mov byte [tp_sbd_bar], 1
    jmp short .yes
.src:
    mov byte [tp_sbd_bar], 0
.yes:
    pop ax
    clc
    ret
.nope:
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tp_sbd_commit - put the dragged pos into the bar it belongs to, and draw
; in:  AX = the pos, [tp_sbd_bar] = which; gfx lock held
; out: nothing much; both tails redraw their own pane
;
; The two clamps are not interchangeable: tp_clamp_ps forces an EVEN scroll
; because the desk beside the sheet is a 50% dither whose phase is a function
; of absolute y, and tp_clamp_vs bounds a line count. The one-pixel
; disagreement an odd pos leaves between the thumb and the sheet is below
; anything a reader can see at preview scale.
; -----------------------------------------------------------------------------
tp_sbd_commit:
    cmp byte [tp_sbd_bar], 0
    jne .prev
    call tp_caret_hide          ; ...at the scroll they were drawn at, before
    call tp_sel_hide            ; the view moves out from under them
    mov [tp_vscroll], ax
    call tp_clamp_vs
    jmp tp_src_scroll
.prev:
    mov [tp_pscroll], ax
    call tp_clamp_ps
    jmp tp_prev_scroll
%endif

tp_psb_click:
    push ax
    push bx
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbhit
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    cmp al, OS88UI_SBPGUP
    je .pu
    cmp al, OS88UI_SBPGDN
    je .pd
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB      ; SPEC.md 13.10.5: the PREVIEW's thumb, in
    jne .o                      ; pixels - the element does not care (13.10.7.2)
    cmp byte [tp_nodrag], 0
    jne .o
    mov al, TP_SBRATE
    call os88ui_sbgrab
%endif
    jmp short .o
.up:
    sub word [tp_pscroll], TP_SB_STEP * 8
    jnc .cl
    mov word [tp_pscroll], 0
    jmp short .cl
.dn:
    add word [tp_pscroll], TP_SB_STEP * 8
    jmp short .cl
.pu:
    mov ax, [tp_r_psb+10]
    sub [tp_pscroll], ax
    jnc .cl
    mov word [tp_pscroll], 0
    jmp short .cl
.pd:
    mov ax, [tp_r_psb+10]
    add [tp_pscroll], ax
.cl:
    call tp_clamp_ps
.o:
    pop bx
    pop ax
    ret

; Arrow cells step a line, the track pages, the thumb is inert (SPEC.md 13.10.1).
tp_ssb_click:
    push ax
    push bx
    call tp_src_metrics
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbhit
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    cmp al, OS88UI_SBPGUP
    je .pu
    cmp al, OS88UI_SBPGDN
    je .pd
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB      ; ...and the SOURCE's, in lines
    jne .o
    cmp byte [tp_nodrag], 0
    jne .o
    mov al, TP_SBRATE
    call os88ui_sbgrab
    jmp short .o
%endif
    jmp short .o
.up:
    cmp word [tp_vscroll], 0
    je .o
    sub word [tp_vscroll], TP_SB_STEP
    jnc .cl
    mov word [tp_vscroll], 0
    jmp short .cl
.dn:
    add word [tp_vscroll], TP_SB_STEP
    jmp short .cl
.pu:
    mov ax, [tp_erows]
    or ax, ax
    jnz .pu2
    mov ax, 8
.pu2:
    sub [tp_vscroll], ax
    jnc .cl
    mov word [tp_vscroll], 0
    jmp short .cl
.pd:
    mov ax, [tp_erows]
    or ax, ax
    jnz .pd2
    mov ax, 8
.pd2:
    add [tp_vscroll], ax
.cl:
    call tp_clamp_vs
.o:
    pop bx
    pop ax
    ret

; The caret is on screen or the view moves to put it there. It is only ever
; VERTICAL now: a row is the whole width of the pane by construction, so
; there is nothing to scroll sideways and [tp_hscroll] is gone with the
; horizontal arrowing it existed for.
tp_keep_caret:
    push ax
    push bx
    push dx
    call tp_cur_lc              ; AX = row
    cmp ax, [tp_vscroll]
    jae .dn
    mov [tp_vscroll], ax
    jmp short .o
.dn:
    mov dx, [tp_erows]
    or dx, dx
    jnz .er
    mov dx, 8
.er:
    add dx, [tp_vscroll]
    or dx, dx
    jz .o
    dec dx
    cmp ax, dx
    jbe .o
    sub ax, [tp_erows]
    inc ax
    jns .sv
    xor ax, ax
.sv:
    mov [tp_vscroll], ax
.o:
    pop dx
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
tp_s_set:       db 'Typeset', 0
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
tp_s_wrap:      db TP_WRAP_MARK, 0      ; the last column of a continued row
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
tp_s_a12:       db 0
tp_s_a13:       db 'Long lines WRAP; a row that continues is marked >', 0

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

%define OS88UI_SCROLL           ; SPEC.md 13.10: the shared scroll bar
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
                                         ; 28 is FREE: it held tp_hscroll,
                                         ; and there is no horizontal scroll
                                         ; to hold any more (SPEC.md 69.10)
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
tp_r_ssb    equ os88_image_end + 11966   ; 14: x1 y1 x2 y2 total fit pos
tp_onpage   equ os88_image_end + 11980
tp_r_psb    equ os88_image_end + 11982   ; 14
tp_tabtop   equ os88_image_end + 11996
tp_tabbot   equ os88_image_end + 11998
tp_tabw     equ os88_image_end + 12000
tp_tabn     equ os88_image_end + 12002
tp_rowh     equ os88_image_end + 12004
tp_ldfull   equ os88_image_end + 12006   ; a deferred load landed: the next
                                         ; handler owes a FULL repaint, not a
                                         ; source-pane one (tp_deferred_ld)
tp_sel0     equ os88_image_end + 12008
tp_sel1     equ os88_image_end + 12010
tp_anchor   equ os88_image_end + 12012
tp_selon    equ os88_image_end + 12014
tp_dselon   equ os88_image_end + 12015
tp_seldrag  equ os88_image_end + 12016
tp_caret_on equ os88_image_end + 12017
tp_edkind   equ os88_image_end + 12018
                                         ; 12019 is FREE: tp_edn, the cells an
                                         ; insert put in front of the caret -
                                         ; nothing re-letters from the edit's
                                         ; own column (69.8)
tp_dsel0    equ os88_image_end + 12020
tp_dsel1    equ os88_image_end + 12022
tp_ssbpos   equ os88_image_end + 12024
tp_statcnt  equ os88_image_end + 12026   ; where the byte count starts inside
                                         ; tp_nbuf (tp_stat_touch)
tp_oldvs    equ os88_image_end + 12028
                                         ; 12030 is FREE: tp_oldhs, the same
tp_oldps    equ os88_image_end + 12032
tp_ssbtot   equ os88_image_end + 12034   ; total and fit the source bar on the
tp_ssbfit   equ os88_image_end + 12036   ; glass was DRAWN from (tp_ssb_move)
tp_barsig_v equ os88_image_end + 12038   ; ...and what the TOP bar's text was
tp_rgx1     equ os88_image_end + 12040   ; the preview REGION being drawn
tp_rgy1     equ os88_image_end + 12042
tp_rgx2     equ os88_image_end + 12044
tp_rgy2     equ os88_image_end + 12046
tp_psbpos   equ os88_image_end + 12048   ; the preview bar's pos, total and fit
tp_psbtot   equ os88_image_end + 12050   ; as DRAWN (tp_psb_move)
tp_psbfit   equ os88_image_end + 12052
tp_rowcont  equ os88_image_end + 12054   ; the row tp_row_ext just measured is
                                         ; CONTINUED by the next one
tp_rc_row   equ os88_image_end + 12056   ; the row cache: a row index, where it
tp_rc_off   equ os88_image_end + 12058   ; starts, and the width both were
tp_rc_w     equ os88_image_end + 12060   ; measured at
tp_rc_want  equ os88_image_end + 12062
tp_edls     equ os88_image_end + 12064   ; the span an edit reaches (tp_edmark)
tp_edrf     equ os88_image_end + 12066
tp_edtail   equ os88_image_end + 12068
tp_edkb     equ os88_image_end + 12070
tp_edka     equ os88_image_end + 12072
tp_edlen    equ os88_image_end + 12074
tp_nrows    equ os88_image_end + 12076   ; the row COUNT cache: how many rows
tp_nrowsw   equ os88_image_end + 12078   ; the document had, the width it was
tp_nrowsok  equ os88_image_end + 12080   ; counted at, and whether it stands
; remainder to 12288. Every name above is a hand-computed offset and nothing
; checks them against each other, so a new field goes at the END and a field
; that grows moves everything under it - the assertion below is the only
; automatic part, and it catches overflowing the block, not overlapping inside
; it. TP_BSS_LAST is the high-water mark; keep it pointing at the last field.
TP_BSS_LAST equ 12080 + 1
%if TP_BSS_LAST > TP_BSS_TOTAL
    %error "texpad bss map overflows OS88_BSS"
%endif
