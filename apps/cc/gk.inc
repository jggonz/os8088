; gk.inc - the os8088 GAME KIT's drawing half (SPEC.md 132.1-132.7): 3D
; boxes, widgets and data-driven scenes for C packages, over the band core
; (apps/breadxt/draw.inc, SPEC.md 130.6.2), which must be included first:
; the kit calls the core's INTERNAL register entries (bx_solid, bx_rect,
; bx_art_at, bx_face, bx_gwid) as well as its C ones. apps/cc/gk.h is the C
; contract. Original.
;
; Near cdecl like the core: arguments at [bp+4].., BP, DS and DF preserved,
; everything else the caller's. Every routine draws only into the band and
; only while a strip is composed; the core clips. Internally a widget works
; on gk_r, the CURRENT RECT (x1, y1, x2, y2 inclusive): gk_boxr draws a
; style's rings round it and leaves it the inside, gk_lab puts a label in it.
; Every colour comes through gk_rc from the role table (132.2).
section .text

GK_ROUND    equ 10h
GK_SHADOW   equ 20h
GK_DROPW    equ 3
GK_CENTRE   equ 1
GK_EMBOSS   equ 10h
GK_FIT      equ 20h
GK_NONE     equ 80h                 ; a ring side, a fill: not drawn
GK_PRESSED  equ 4                   ; button states with a rule of their own
GK_DISABLED equ 3
GK_HILITE   equ 5
; roles (gk.h GK_R_*)
GK_OUT equ 0
GK_FACE equ 1
GK_LT equ 2
GK_SH equ 3
GK_FIELD equ 4
GK_TEXT equ 5
GK_TITLE equ 6
GK_TITLELT equ 7
GK_TITLETX equ 8
GK_FOCUS equ 9
GK_FOCUSTX equ 10
GK_HI equ 11
GK_HITX equ 12
GK_DIS equ 13
GK_DROP equ 14
GK_KEY equ 15

; ---- roles ---------------------------------------------------------------
; void gk_init(int bpp)
_gk_init:
    mov bx,sp
    mov ax,gk_colr
    cmp word [ss:bx+2],4
    jge .c
    mov ax,gk_monr
.c:
    mov [gk_def],ax
    mov [gk_rt],ax
    ret

; void gk_theme(const unsigned char *roles) - 0 = the default gk_init chose
_gk_theme:
    mov bx,sp
    mov ax,[ss:bx+2]
    test ax,ax
    jnz .s
    mov ax,[gk_def]
.s:
    mov [gk_rt],ax
    ret

; int gk_role(int role)
_gk_role:
    mov bx,sp
    mov al,[ss:bx+2]
    call gk_rc
    xor ah,ah
    ret

; gk_rc: AL = a role -> AL = its colour; bit 7 set (none) -> FFh.
gk_rc:
    test al,al
    js .n
    push bx
    and al,15
    mov bx,[gk_rt]
    xlat
    pop bx
    ret
.n:
    mov al,0FFh
    ret

; gk_rc2: AL and AH roles -> colours
gk_rc2:
    call gk_rc
    xchg al,ah
    call gk_rc
    xchg al,ah
    ret

; ---- the current rect ----------------------------------------------------
gk_ld:
    mov ax,[gk_r]
    mov bx,[gk_r+2]
    mov cx,[gk_r+4]
    mov dx,[gk_r+6]
    ret

; gk_setr: gk_r = the caller's x, y, w, h at [bp+4]..[bp+10]
gk_setr:
    mov ax,[bp+4]
    mov bx,[bp+6]
    mov cx,[bp+8]
    mov dx,[bp+10]
; gk_setxywh: AX, BX, CX, DX = x, y, w, h -> gk_r
gk_setxywh:
    mov [gk_r],ax
    mov [gk_r+2],bx
    add cx,ax
    dec cx
    mov [gk_r+4],cx
    add dx,bx
    dec dx
    mov [gk_r+6],dx
    ret
; gk_setsq: gk_r = x, y and a size square from [bp+4]..[bp+8]
gk_setsq:
    mov ax,[bp+4]
    mov bx,[bp+6]
    mov cx,[bp+8]
    mov dx,cx
    jmp gk_setxywh

gk_inset1:
    mov ax,1
gk_inset:                           ; AX = px off every side
    add [gk_r],ax
    add [gk_r+2],ax
    sub [gk_r+4],ax
    sub [gk_r+6],ax
    ret

; ---- fills ---------------------------------------------------------------
; gk_fillro: as gk_fill, DI low = a ROLE
gk_fillro:
    xchg ax,di
    call gk_rc
    xchg ax,di
; gk_fill: AX, BX, CX, DX = x1, y1, x2, y2 (screen, inclusive), DI low = the
; colour (bit 7: nothing). Clobbers everything but BP.
gk_fill:
    xchg ax,di
    test al,al
    js .no
    push di
    push bx
    push cx
    push dx
    call bx_solid
    pop dx
    pop cx
    pop bx
    pop ax
    jmp bx_rect
.no:
    ret

; gk_doorr: as gk_door, DI low = a ROLE. gk_dooro: in DROP.
gk_dooro:
    mov di,GK_DROP
gk_doorr:
    xchg ax,di
    call gk_rc
    xchg ax,di
; gk_door: a SCREEN DOOR over AX, BX, CX, DX (inclusive): DI low's colour
; where x + y is even, the band left alone elsewhere (132.3). One bx_rect a
; row inside the strip, through the core's dot mask.
gk_door:
    mov [gk_dx1],ax
    mov [gk_dx2],cx
    xchg ax,di
    mov [gk_dc],al
    mov ax,[bx_y0]
    cmp bx,ax
    jge .t
    mov bx,ax
.t:
    add ax,[bx_h]
    dec ax
    cmp dx,ax
    jle .b
    mov dx,ax
.b:
    mov [gk_dy2],dx
.row:
    cmp bx,[gk_dy2]
    jg .done
    push bx
    mov al,0AAh                     ; even rows: the even x
    test bl,1
    jz .e
    mov al,55h
.e:
    mov [bx_pat],al
    mov al,[gk_dc]
    call bx_solid
    pop bx
    push bx
    mov ax,[gk_dx1]
    mov cx,[gk_dx2]
    mov dx,bx
    call bx_rect
    pop bx
    inc bx
    jmp .row
.done:
    mov byte [bx_pat],0FFh
    ret

; gk_shad: gk_r's drop shadow, SI px right and down, outside it
gk_shad:
    push si
    call gk_ld
    mov ax,cx                       ; the right strip
    inc ax
    add bx,si
    add cx,si
    add dx,si
    call gk_dooro
    pop si
    call gk_ld                      ; the bottom strip
    add ax,si
    mov bx,dx
    inc bx
    add dx,si
    jmp gk_dooro

; ---- rings and boxes -----------------------------------------------------
; gk_ring: AL = top/left colour, AH = bottom/right colour (bit 7: that half
; not drawn), CX = the corner cut (0, or 1 to leave the corners). Frames
; gk_r, then insets it by one.
gk_ring:
    mov [gk_col],ax
    mov [gk_cut],cx
    xor si,si                       ; the side: top, left, bottom, right
.e:
    push si
    call gk_ld
    mov di,[gk_cut]
    test si,1
    jnz .v
    add ax,di                       ; a row
    sub cx,di
    test si,2
    jnz .bot
    mov dx,bx
    jmp short .go
.bot:
    mov bx,dx
    jmp short .go
.v:
    add bx,di                       ; a column
    sub dx,di
    test si,2
    jnz .rt
    mov cx,ax
    jmp short .go
.rt:
    mov ax,cx
.go:
    mov di,[gk_col]
    test si,2
    jz .f
    mov di,[gk_col+1]
.f:
    call gk_fill
    pop si
    inc si
    cmp si,4
    jb .e
    jmp gk_inset1

; gk_boxr: gk_r = the rect, AL = the style, AH = the inside's colour (bit 7:
; left alone). Shadow, rings, fill; leaves gk_r = the inside.
gk_boxr:
    mov [gk_sty],ax
    test al,GK_SHADOW
    jz .ns
    mov si,GK_DROPW
    call gk_shad
.ns:
    mov bl,[gk_sty]
    and bx,7
    mov bl,[gk_ridx+bx]
    add bx,gk_rings
    xor cx,cx
    test byte [gk_sty],GK_ROUND
    jz .r
    inc cx
.r:
    mov ax,[bx]
    cmp al,0FFh
    je .fill
    push bx
    call gk_rc2
    call gk_ring
    pop bx
    inc bx
    inc bx
    xor cx,cx                       ; only the outermost ring is cut
    jmp .r
.fill:
    call gk_ld
    mov di,[gk_sty+1]
    jmp gk_fill

; void gk_box(int x,int y,int w,int h,int style,int fill)
_gk_box:
    push bp
    mov bp,sp
    call gk_setr
    mov al,[bp+12]
    mov ah,[bp+14]
    call gk_boxr
    pop bp
    ret

; void gk_hline(int x,int y,int w,int raised) / gk_vline(x,y,h,raised): one
; ring of a w x 2 (2 x h) rect - SHADE then LIGHT, or LIGHT then SHADE
_gk_hline:
    push bp
    mov bp,sp
    mov cx,[bp+8]
    mov dx,2
    jmp short gk_line
_gk_vline:
    push bp
    mov bp,sp
    mov cx,2
    mov dx,[bp+8]
gk_line:
    mov ax,[bp+4]
    mov bx,[bp+6]
    call gk_setxywh
    mov ax,GK_LT << 8 | GK_SH
    cmp word [bp+10],0
    je .g
    xchg al,ah
.g:
    call gk_rc2
    xor cx,cx
    call gk_ring
    pop bp
    ret

; void gk_shadow(int x,int y,int w,int h,int d)
_gk_shadow:
    push bp
    mov bp,sp
    call gk_setr
    mov si,[bp+12]
    call gk_shad
    pop bp
    ret

; ---- text ----------------------------------------------------------------
; int gk_text(int x,int y,int w,const char *s,int colour,int font,int flags)
_gk_text:
    push bp
    mov bp,sp
    mov cx,3
    mov si,4
    mov di,gk_tx
    call gk_cpa                     ; x, y, w
    mov [di],cx                     ; h = 0: y is the top row
    inc di
    inc di
    mov cl,4
    jmp short gk_tgo                ; s, colour, font, flags
; int gk_textbox(int x,int y,int w,int h,const char *s,int colour,int font,
;                int flags)
_gk_textbox:
    push bp
    mov bp,sp
    mov cx,8
    mov si,4
    mov di,gk_tx
gk_tgo:
    call gk_cpa
    call gk_tdo
    pop bp
    ret
; gk_cpa: CX words from [bp+si] to [di]
gk_cpa:
    mov ax,[bp+si]
    mov [di],ax
    inc si
    inc si
    inc di
    inc di
    loop gk_cpa
    ret

; gk_lab: a label in gk_r - AL = colour, AH = flags, SI = string, DX = font
gk_lab:
    mov [gk_tc],al
    mov [gk_tfl],ah
    mov [gk_ts],si
    mov [gk_tf],dx
    call gk_ld
    mov [gk_tx],ax
    mov [gk_ty],bx
    sub cx,ax
    inc cx
    mov [gk_tw],cx
    sub dx,bx
    inc dx
    mov [gk_th],dx
; gk_tdo: draw gk_tx.. (x, y, w, h, s, colour, font, flags; h = 0: y is the
; top row, else the text is centred in h). AX = the drawn width.
gk_tdo:
    mov ax,[gk_tf]
    call bx_face
    mov si,[gk_ts]
    mov di,gk_tb
    xor dx,dx                       ; the width so far
.c:
    mov al,[si]
    test al,al
    jz .e
    cmp di,gk_tb+63
    jae .e
    push si
    push di
    push dx
    call bx_gwid                    ; AX = its advance
    pop dx
    pop di
    pop si
    add ax,dx
    test byte [gk_tfl],GK_FIT
    jz .k
    cmp ax,[gk_tw]
    jg .e                           ; the first glyph that does not fit
.k:
    xchg ax,dx
    mov al,[si]
    mov [di],al
    inc si
    inc di
    jmp .c
.e:
    mov byte [di],0
    push dx
    mov ax,[gk_tw]                  ; x: the slack, by the alignment
    sub ax,dx
    mov cl,[gk_tfl]
    and cl,3
    jz .l
    dec cl
    jnz .x
    sar ax,1
    jmp short .x
.l:
    xor ax,ax
.x:
    add ax,[gk_tx]
    mov bx,[gk_th]                  ; y
    test bx,bx
    jz .y
    sub bx,[bx_fh]
    sar bx,1
.y:
    add bx,[gk_ty]
    test byte [gk_tfl],GK_EMBOSS
    jz .p
    cmp word [bx_np],1
    je .p                           ; (no emboss on 1bpp, 132.4)
    push ax
    push bx
    inc ax
    inc bx
    mov si,[gk_rt]
    mov cl,[si+GK_OUT]
    call gk_bxt
    pop bx
    pop ax
.p:
    mov cl,[gk_tc]
    call gk_bxt
    pop ax
    ret
; gk_bxt: bx_text(AX, BX, gk_tb, CL, gk_tf)
gk_bxt:
    push word [gk_tf]
    xor ch,ch
    push cx
    mov cx,gk_tb
    push cx
    push bx
    push ax
    call _bx_text
    add sp,10
    ret

; gk_tail: a widget's label from its own frame ([bp+12] the string, [bp+14]
; the font) in gk_r, AL = its ROLE, AH = flags; then the widget returns
gk_tail:
    call gk_rc
    mov si,[bp+12]
    mov dx,[bp+14]
    call gk_lab
    pop bp
    ret

; ---- widgets -------------------------------------------------------------
; int gk_panel(int x,int y,int w,int h,int bar,const char *title,int font,
;              int style)
_gk_panel:
    push bp
    mov bp,sp
    call gk_setr
    mov ah,GK_FACE
    call gk_rcah
    mov al,[bp+18]
    call gk_boxr
    mov ax,[gk_r+2]                 ; the inside's top
    mov cx,[bp+12]
    jcxz .done
    push ax
    inc ax                          ; the bar, one pixel inside the rings
    mov [gk_r+2],ax
    add ax,cx
    dec ax
    mov [gk_r+6],ax
    inc word [gk_r]
    dec word [gk_r+4]
    mov si,[bp+14]
    mov dx,[bp+16]
    mov ah,GK_CENTRE
    call gk_tbar
    pop ax
    add ax,[bp+12]
    inc ax
    inc ax
.done:
    pop bp
    ret
; gk_rcah: AH = a role -> its colour
gk_rcah:
    xchg al,ah
    call gk_rc
    xchg al,ah
    ret

; void gk_titlebar(int x,int y,int w,int h,const char *s,int font,int flags)
_gk_titlebar:
    push bp
    mov bp,sp
    call gk_setr
    mov si,[bp+12]
    mov dx,[bp+14]
    mov ah,[bp+16]
    call gk_tbar
    pop bp
    ret
; gk_tbar: a title bar over gk_r - SI = string, DX = font, AH = alignment
gk_tbar:
    push si
    push dx
    push ax
    call gk_ld
    mov di,GK_TITLE
    call gk_fillro
    call gk_ld
    mov dx,bx
    mov di,GK_TITLELT
    call gk_fillro
    pop ax
    test ah,3
    jnz .c
    add word [gk_r],6               ; left: a 6 px indent
.c:
    or ah,GK_EMBOSS | GK_FIT
    mov al,GK_TITLETX
    call gk_rc
    pop dx
    pop si
    jmp gk_lab

; void gk_button(int x,int y,int w,int h,const char *label,int font,int state)
_gk_button:
    push bp
    mov bp,sp
    call gk_setr
    mov bx,[bp+16]
    mov dl,0FFh
    call gk_btn
    pop bp
    ret
; gk_btn: gk_r = the rect, BX = the state, DL = a mask over its style
gk_btn:
    cmp bx,5
    jbe .s
    xor bx,bx
.s:
    mov [gk_bst],bl
    mov al,3
    mul bl
    xchg ax,bx
    mov al,[gk_btab+bx]
    and al,dl
    mov ah,[gk_btab+bx+1]
    push word [gk_btab+bx+2]        ; the label's role
    call gk_rcah
    call gk_boxr
    pop ax
    call gk_rc
    cmp byte [gk_bst],GK_PRESSED
    jne .np
    push ax
    call gk_inset1                  ; down-right one: every edge +1
    add word [gk_r+4],2
    add word [gk_r+6],2
    pop ax
.np:
    mov ah,GK_CENTRE | GK_FIT
    mov si,[bp+12]
    mov dx,[bp+14]
    call gk_lab
    cmp byte [gk_bst],GK_DISABLED
    jne .r
    cmp word [bx_np],1
    jne .r
    call gk_ld                      ; 1bpp: the screen door over it
    mov di,GK_FACE
    jmp gk_doorr
.r:
    ret

; void gk_tab(int x,int y,int w,int h,const char *label,int font,int active)
_gk_tab:
    push bp
    mov bp,sp
    call gk_setr
    push word [bx_h]
    mov ax,[gk_r+6]                 ; the strip ends at the tab's bottom row
    inc ax
    sub ax,[bx_y0]
    jge .p
    xor ax,ax
.p:
    cmp ax,[bx_h]
    jge .k
    mov [bx_h],ax
.k:
    add word [gk_r+6],3             ; so the bottom edge falls past it
    xor bx,bx
    cmp word [bp+16],0
    je .n
    mov bl,GK_HILITE
    sub word [gk_r+2],2
.n:
    mov dl,0FFh ^ GK_SHADOW
    call gk_btn
    pop word [bx_h]
    pop bp
    ret

; void gk_keycap(int x,int y,int w,int h,const char *label,int font)
_gk_keycap:
    push bp
    mov bp,sp
    call gk_setr
    mov ax,GK_SH << 8 | GK_ROUND
    call gk_rcah
    call gk_boxr                    ; outline, SHADE inside
    mov ax,[bp+10]
    mov cl,5
    div cl
    xor ah,ah
    cmp al,2
    jae .s
    mov al,2
.s:
    sub [gk_r+6],ax                 ; the front, s rows
    sub word [gk_r+4],2             ; the right side
    call gk_ld
    mov di,GK_FACE
    call gk_fillro
    inc word [gk_r]                 ; the top face
    dec word [gk_r+4]
    dec word [gk_r+6]
    call gk_ld
    mov di,GK_KEY
    call gk_fillro
    mov ax,(GK_CENTRE | GK_FIT) << 8 | GK_TEXT
    jmp gk_tail

; void gk_drop(int x,int y,int w,int h,const char *label,int font)
_gk_drop:
    push bp
    mov bp,sp
    call gk_setr
    mov ax,GK_FACE << 8 | 2 | GK_ROUND
    call gk_rcah
    call gk_boxr
    mov ax,[bp+4]
    add ax,[bp+8]
    sub ax,[bp+10]
    push ax                         ; x + w - h
    sub ax,4
    mov [gk_r+4],ax
    inc word [gk_r]
    mov cx,[bp+10]                  ; the triangle: an h/2 square
    shr cx,1
    mov bx,cx
    shr bx,1
    pop ax
    add ax,bx
    add bx,[bp+6]
    mov dx,cx
    mov si,2
    push ax
    mov al,GK_TEXT
    call gk_rc
    xchg ax,di
    pop ax
    call gk_arw
    mov ax,(GK_FIT << 8) | GK_TEXT
    jmp gk_tail

; void gk_banner(int x,int y,int w,int h,const char *s,int font,int fill)
_gk_banner:
    push bp
    mov bp,sp
    call gk_setr
    mov ah,[bp+16]
    mov dl,GK_TEXT
    test ah,ah
    jns .f
    mov ah,GK_HI
    call gk_rcah
    mov dl,GK_HITX
.f:
    push dx
    mov al,1 | GK_ROUND
    call gk_boxr
    pop ax
    mov ah,GK_CENTRE | GK_FIT
    jmp gk_tail

; void gk_check(int x,int y,int size,int on,int tick)
_gk_check:
    push bp
    mov bp,sp
    call gk_setsq
    mov ax,GK_FIELD << 8 | 6
    call gk_rcah
    call gk_boxr
    cmp word [bp+10],0
    je .done
    mov bx,[bp+12]
    test bx,bx
    jle .drawn
    call bx_art_at
    jc .drawn
    xor ax,ax                       ; the art tick, centred on the box
    mov al,[es:si]
    mov cx,[bp+8]
    sub cx,ax
    sar cx,1
    add cx,[bp+4]
    mov al,[es:si+1]
    mov dx,[bp+8]
    sub dx,ax
    sar dx,1
    add dx,[bp+6]
    push word [bp+12]
    push dx
    push cx
    call _bx_spr
    add sp,6
    jmp short .done
.drawn:
    mov ax,[bp+8]                   ; c1 = s/2 - 2 dots down-right
    shr ax,1
    dec ax
    dec ax
    jle .done
    mov [gk_t1],ax
    mov ax,[bp+8]
    mov cl,5
    div cl
    xor ah,ah
    add ax,[bp+4]                   ; from (x + s/5, y + s/2 - 1)
    mov bx,[bp+8]
    shr bx,1
    dec bx
    add bx,[bp+6]
    mov cx,[gk_t1]
    mov si,1
    push ax
    push bx
    call gk_walk
    pop bx
    pop ax
    add ax,[gk_t1]                  ; then up-right to x + s - 3
    add bx,[gk_t1]
    dec bx
    dec bx
    mov cx,[bp+4]
    add cx,[bp+8]
    dec cx
    dec cx
    sub cx,ax
    jle .done
    mov si,-1
    call gk_walk
.done:
    pop bp
    ret
; gk_walk: CX 2x2 green dots from (AX, BX), x +1 and y +SI a step
gk_walk:
    push ax
    push bx
    push cx
    push si
    mov cx,ax
    inc cx
    mov dx,bx
    inc dx
    mov di,2
    call gk_fill
    pop si
    pop cx
    pop bx
    pop ax
    inc ax
    add bx,si
    loop gk_walk
    ret

; void gk_radio(int x,int y,int size,int on)
_gk_radio:
    push bp
    mov bp,sp
    mov ax,GK_OUT << 8
    call gk_rdisc
    mov ax,GK_FIELD << 8 | 1
    call gk_rdisc
    cmp word [bp+10],0
    je .done
    mov ax,GK_TEXT << 8 | 3
    call gk_rdisc
.done:
    pop bp
    ret
; gk_rdisc: a disc AL px inside the radio's square, in AH's role
gk_rdisc:
    xchg al,ah
    call gk_rc
    xchg al,ah
    mov di,ax
    cbw
    mov bx,[bp+6]
    add bx,ax
    mov cx,[bp+8]
    sub cx,ax
    sub cx,ax
    add ax,[bp+4]
; gk_disc: a disc CX across at (AX, BX), in DI high's colour: row i spans
; the columns j with (s-1-2j)^2 + (2i-s+1)^2 <= s^2 (132.5)
gk_disc:
    mov [gk_dx1],ax
    mov [gk_dy2],bx
    mov [gk_ds],cx
    xchg ax,di
    mov [gk_dc],ah
    mov ax,cx
    mul ax
    mov [gk_s2],ax
    xor si,si
.row:
    cmp si,[gk_ds]
    jge .done
    mov ax,si
    shl ax,1
    sub ax,[gk_ds]
    inc ax
    imul ax
    xchg ax,bx                      ; BX = the row's term
    xor cx,cx                       ; CX = the inset
.k:
    mov ax,[gk_ds]
    dec ax
    sub ax,cx
    sub ax,cx
    imul ax
    add ax,bx
    cmp ax,[gk_s2]
    jle .got
    inc cx
    jmp .k
.got:
    push si
    mov ax,[gk_dx1]
    add ax,cx
    mov bx,[gk_dy2]
    add bx,si
    mov dx,bx
    neg cx
    add cx,[gk_ds]
    dec cx
    add cx,[gk_dx1]
    mov di,[gk_dc]
    call gk_fill
    pop si
    inc si
    jmp .row
.done:
    ret

; void gk_arrow(int x,int y,int w,int h,int dir,int colour)
_gk_arrow:
    push bp
    mov bp,sp
    call gk_setr
    call gk_ld
    mov cx,[bp+8]
    mov dx,[bp+10]
    mov si,[bp+12]
    mov di,[bp+14]
    call gk_arw
    pop bp
    ret
; gk_arw: a solid triangle in AX, BX, CX, DX (x, y, w, h), apex toward SI
; (0 up, 1 right, 2 down, 3 left), DI low = the colour
gk_arw:
    mov [gk_ac],di
    and si,3
    mov [gk_ad],si
    test si,1
    jz .v
    xchg ax,bx                      ; across is y, along is x
    xchg cx,dx
.v:                                 ; AX across from, BX along from, CX across, DX along
    push ax
    mov ax,cx
    inc ax
    shr ax,1
    cmp ax,dx
    jle .n
    mov ax,dx
.n:
    mov [gk_an],ax                  ; n lines
    sub dx,ax
    sar dx,1
    add bx,dx
    mov [gk_as],bx                  ; the first line's along position
    pop ax
    dec cx
    sar cx,1
    add ax,cx
    mov [gk_acn],ax                 ; the across centre
    xor si,si
.k:
    cmp si,[gk_an]
    jge .done
    push si
    mov cx,[gk_an]
    dec cx
    mov bx,cx
    sub cx,si                       ; CX = this line's half length
    sub bx,si                       ; base last (up, left): position n-1-k
    mov al,[gk_ad]
    dec al
    cmp al,1
    ja .bl
    mov bx,si                       ; base first (right, down): k
.bl:
    add bx,[gk_as]
    mov ax,[gk_acn]
    mov dx,ax
    sub ax,cx
    add dx,cx
    mov cx,dx
    mov dx,bx                       ; a row: (c-half, pos, c+half, pos)
    test byte [gk_ad],1
    jz .f
    xchg ax,bx                      ; a column: (pos, c-half, pos, c+half)
    mov dx,cx
    mov cx,ax
.f:
    mov di,[gk_ac]
    call gk_fill
    pop si
    inc si
    jmp .k
.done:
    ret

; int gk_sprshadow(int x,int y,int id,int d)
_gk_sprshadow:
    push bp
    mov bp,sp
    cmp word [bp+10],0
    je .spr
    mov bx,[bp+8]
    call bx_art_at
    jc .spr
    mov [gk_sseg2],es
    xor ax,ax
    mov al,[es:si]
    mov [gk_sw],ax
    mov al,[es:si+1]
    mov [gk_sh],ax
    inc si
    inc si
    mov [gk_sp2],si
    mov ax,[bp+4]
    add ax,[bp+10]
    mov [gk_sxb],ax
    mov word [gk_srr],0
.row:
    mov ax,[gk_srr]
    cmp ax,[gk_sh]
    jae .spr
    add ax,[bp+6]
    add ax,[bp+10]
    mov [gk_sy],ax
    sub ax,[bx_y0]
    cmp ax,[bx_h]
    jae .nrow                       ; outside the strip
    mov ax,[gk_sw]
    inc ax
    shr ax,1
    mul word [gk_srr]
    add ax,[gk_sp2]
    mov si,ax
    mov es,[gk_sseg2]
    xor cx,cx                       ; the column
    mov dx,-1                       ; the open run's first column
.col:
    cmp cx,[gk_sw]
    jae .end
    mov bx,cx
    shr bx,1
    mov al,[es:bx+si]
    mov ah,0D0h
    test cl,1
    jz .hi
    mov ah,0Dh
    and al,0Fh
    jmp short .t
.hi:
    and al,0F0h
.t:
    cmp al,ah
    je .key
    cmp dx,-1
    jne .nx
    mov dx,cx
    jmp short .nx
.key:
    call .emit
.nx:
    inc cx
    jmp .col
.end:
    call .emit
.nrow:
    inc word [gk_srr]
    jmp .row
.spr:
    push word [bp+8]
    push word [bp+6]
    push word [bp+4]
    call _bx_spr
    add sp,6
    pop bp
    ret
.emit:                              ; the run DX .. CX-1, if one is open
    cmp dx,-1
    je .er
    push cx
    push si
    mov ax,[gk_sxb]
    add ax,dx
    add cx,[gk_sxb]
    dec cx
    mov bx,[gk_sy]
    mov dx,bx
    call gk_dooro
    pop si
    pop cx
    mov es,[gk_sseg2]
    mov dx,-1
.er:
    ret

; ---- scenes (132.6) ------------------------------------------------------
; int gk_scene(int id,int x,int y,const char *const *sv,const int *iv)
_gk_scene:
    push bp
    mov bp,sp
    call gk_shdr
    jc gk_no
    mov al,9
    mul cl
    add si,ax
    add si,6
    mov [gk_sp],si
.next:
    mov es,[gk_sseg]
    mov si,[gk_sp]
    xor ax,ax
    mov al,[es:si]
    test al,al
    jz .yes
    add [gk_sp],ax                  ; the next op
    mov al,[es:si+1]
    mov [gk_op],al
    mov ax,[es:si+2]                ; its rows against the strip's
    add ax,[bp+8]
    mov dx,[bx_y0]
    add dx,[bx_h]
    cmp ax,dx
    jge .next
    mov ax,[es:si+4]
    add ax,[bp+8]
    cmp ax,[bx_y0]
    jl .next
    add si,6
    test byte [gk_op],80h
    jz .args
    mov cl,[es:si]                  ; the condition
    mov al,[es:si+1]
    inc si
    inc si
    cbw
    mov bl,cl
    and bx,1Fh
    shl bx,1
    add bx,[bp+12]
    cmp ax,[bx]
    je .eq
    test cl,20h
    jnz .next
    jmp short .args
.eq:
    test cl,20h
    jz .next
.args:
    xor di,di
.a:
    cmp si,[gk_sp]
    jae .call
    cmp di,20
    jae .next                       ; more than ten: refused
    xor ax,ax
    mov al,[es:si]
    inc si
    cmp al,80h
    jb .v
    cmp al,0E0h
    jae .neg
    cmp al,0A0h
    jb .islot
    cmp al,0C0h
    jb .sslot
    je .word
    cmp al,0C2h
    je .role
    ja .yes                         ; C3h-DFh: not a code
    call gk_lit
    jmp short .v
.neg:
    cbw
    jmp short .v
.islot:
    mov bx,[bp+12]
    jmp short .slot
.sslot:
    mov bx,[bp+10]
.slot:
    and al,1Fh
    shl ax,1
    add bx,ax
    mov ax,[bx]
    jmp short .v
.word:
    mov ax,[es:si]
    inc si
    inc si
    jmp short .v
.role:
    mov al,[es:si]
    inc si
    call gk_rc
.v:
    cmp di,8                        ; the origin: x, y (and x2, y2 of 18-20)
    jae .st
    cmp di,4
    jb .org
    mov bl,[gk_op]
    and bl,3Fh
    cmp bl,18
    jb .st
.org:
    test di,2
    jnz .oy
    add ax,[bp+6]
    jmp short .st
.oy:
    add ax,[bp+8]
.st:
    mov [gk_args+di],ax
    inc di
    inc di
    jmp .a
.call:
    mov bl,[gk_op]
    and bx,3Fh
    cmp bl,20
    ja .yes
    shl bx,1
    mov [gk_na],di
.p:
    sub di,2
    jb .c
    push word [gk_args+di]
    jmp .p
.c:
    call [gk_ops+bx]
    add sp,[gk_na]
    jmp .next
.yes:
    mov ax,1
    pop bp
    ret
gk_no:
    xor ax,ax
    pop bp
    ret

; gk_lit: ES:SI = a literal (01h-1Fh insert sv[b-1]) -> gk_tb, AX = gk_tb
gk_lit:
    mov di,gk_tb
.c:
    mov al,[es:si]
    inc si
    test al,al
    jz .e
    cmp al,20h
    jae .ch
    push si
    dec ax
    shl ax,1
    mov bx,[bp+10]
    add bx,ax
    mov si,[bx]
.i:
    lodsb
    test al,al
    jz .ie
    call .put
    jmp .i
.ie:
    pop si
    jmp .c
.ch:
    call .put
    jmp .c
.put:
    cmp di,gk_tb+63
    jae .r
    mov [di],al
    inc di
.r:
    ret
.e:
    mov [di],al
    mov ax,gk_tb
    ret

; gk_shdr: [bp+4] = a scene's id -> ES:SI = its record, CL = its tag count,
; gk_sseg; CF = 1 absent (no record, or not a scene)
gk_shdr:
    mov bx,[bp+4]
    call bx_art_at
    jc .r
    cmp byte [es:si],'S'
    stc
    jne .r
    mov [gk_sseg],es
    mov cl,[es:si+5]
    clc
.r:
    ret

; int gk_scene_size(int id,int *wh)
_gk_scene_size:
    push bp
    mov bp,sp
    call gk_shdr
    jc gk_no
    inc si
    mov di,[bp+6]
    mov cx,2
gk_out:                             ; CX words ES:SI -> DS:DI, answer 1
    mov ax,[es:si]
    mov [di],ax
    inc si
    inc si
    inc di
    inc di
    loop gk_out
    mov ax,1
    pop bp
    ret

; int gk_scene_rect(int id,int tag,int *r)
_gk_scene_rect:
    push bp
    mov bp,sp
    call gk_shdr
    jc gk_no
    add si,6
    xor ch,ch
    jcxz .no
.t:
    mov al,[es:si]
    inc si
    cmp al,[bp+6]
    je .f
    add si,8
    loop .t
.no:
    jmp gk_no
.f:
    mov di,[bp+8]
    mov cx,4
    jmp gk_out

section .data
gk_rt: dw gk_colr                   ; the role table in force
gk_def: dw gk_colr                  ; ...and gk_theme(0)'s
gk_colr: db 0,7,15,8,15,0,1,9,15,1,15,14,0,8,0,15
gk_monr: db 0,15,15,0,15,0,0,8,15,0,15,0,15,0,0,15
; the ring sets (132.3): top/left, bottom/right role pairs, FFh ends
gk_ridx: db gk_r0-gk_rings, gk_r1-gk_rings, gk_r2-gk_rings, gk_r3-gk_rings
         db gk_r4-gk_rings, gk_r5-gk_rings, gk_r6-gk_rings, gk_rings-gk_rings
gk_rings: db GK_OUT,GK_OUT                    ; 7, the default button
gk_r2: db GK_OUT,GK_OUT, GK_LT,GK_SH, GK_NONE,GK_SH, 0FFh
gk_r1: db GK_OUT,GK_OUT, GK_LT,GK_SH, 0FFh
gk_r0: db GK_OUT,GK_OUT, 0FFh
gk_r6: db GK_OUT,GK_OUT, GK_SH,GK_NONE, 0FFh
gk_r4: db GK_SH,GK_LT, GK_OUT,GK_FACE, 0FFh
gk_r5: db GK_SH,GK_LT, GK_LT,GK_SH, 0FFh
gk_r3: db GK_SH,GK_LT, 0FFh
; button states: style, face role, label role
gk_btab: db 2 | GK_ROUND | GK_SHADOW, GK_FACE, GK_TEXT        ; NORMAL
         db 2 | GK_ROUND | GK_SHADOW, GK_FOCUS, GK_FOCUSTX    ; FOCUS
         db 7 | GK_ROUND | GK_SHADOW, GK_FACE, GK_TEXT        ; DEFAULT
         db 2 | GK_ROUND | GK_SHADOW, GK_FACE, GK_DIS         ; DISABLED
         db 6 | GK_ROUND, GK_FACE, GK_TEXT                    ; PRESSED
         db 2 | GK_ROUND | GK_SHADOW, GK_HI, GK_HITX          ; HILITE
; scene ops 0..20 (132.6)
gk_ops: dw _gk_box, _gk_hline, _gk_vline, _gk_shadow, _gk_text, _gk_textbox
        dw _gk_panel, _gk_titlebar, _gk_button, _gk_keycap, _gk_tab
        dw _gk_check, _gk_radio, _gk_drop, _gk_banner, _gk_arrow
        dw _gk_sprshadow, _bx_spr, _bx_fill, _bx_dither, _bx_dots
section .bss
gk_r: resw 4                        ; the current rect, x1 y1 x2 y2
gk_tx: resw 1                       ; gk_tdo's block: x y w h s colour font flags
gk_ty: resw 1
gk_tw: resw 1
gk_th: resw 1
gk_ts: resw 1
gk_tc: resw 1
gk_tf: resw 1
gk_tfl: resw 1
gk_tb: resb 64                      ; the text drawn: a copy, cut to fit
gk_col: resw 1
gk_cut: resw 1
gk_sty: resw 1
gk_bst: resb 1
gk_op: resb 1
gk_dx1: resw 1
gk_dx2: resw 1
gk_dy2: resw 1
gk_dc: resw 1
gk_ds: resw 1
gk_s2: resw 1
gk_t1: resw 1
gk_ac: resw 1
gk_ad: resw 1
gk_an: resw 1
gk_as: resw 1
gk_acn: resw 1
gk_sw: resw 1
gk_sh: resw 1
gk_sp2: resw 1
gk_sseg2: resw 1
gk_sxb: resw 1
gk_srr: resw 1
gk_sy: resw 1
gk_sseg: resw 1
gk_sp: resw 1
gk_na: resw 1
gk_args: resw 10
section .text
