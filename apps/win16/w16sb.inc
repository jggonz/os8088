; =============================================================================
; w16sb.inc - scroll bars on any window, and ScrollWindow (SPEC.md 124.22.1,
; 124.22.2). The library part's: section .w16dlg
; =============================================================================

section .w16dlg

WM_HSCROLL      equ 0x0114
WM_VSCROLL      equ 0x0115
SB_HORZ         equ 0
SB_VERT         equ 1
SB_CTL          equ 2
SB_BOTH         equ 3
SB_LINEUP       equ 0
SB_LINEDOWN     equ 1
SB_PAGEUP       equ 2
SB_PAGEDOWN     equ 3
SB_THUMBPOSITION equ 4
SB_THUMBTRACK   equ 5
SB_TOP          equ 6
SB_BOTTOM       equ 7
SB_ENDSCROLL    equ 8
SBS_VERT        equ 1               ; a SCROLLBAR control's style: up and down

W16SB_FIRST     equ 6           ; ticks before a held arrow repeats (330
W16SB_EVERY     equ 2           ;   ms: a click is shorter), and between

; [w16_sbg], w16_sbgeom's answer, all on the screen and inclusive
SG_X0   equ 0                   ; the bar
SG_Y0   equ 2
SG_X1   equ 4
SG_Y1   equ 6
SG_T0   equ 8                   ; its track, along it, between the arrows
SG_T1   equ 10
SG_TH   equ 12                  ; the thumb's first pixel along it
SG_FREE equ 14                  ; the pixels the thumb can move; -1 no thumb

; w16_sbfld - BX = a record, AL = SB_HORZ or SB_VERT -> SI = that bar's
; position word in the record (minimum +2, maximum +4). Keeps the rest.
w16_sbfld:
    lea si, [bx + W16W_SBH]
    or al, al
    jz .out
    add si, W16W_SBV - W16W_SBH
.out:
    ret

; w16_sbhas - BX = a record, AL = a bar: CF=0 it has that bar. Keeps every
; register.
w16_sbhas:
    cmp byte [bx + W16W_KIND], W16Q_SCROLL
    je .ctl
    push ax
    mov ah, WS_HSCROLL_HI
    or al, al
    jz .t
    mov ah, WS_VSCROLL_HI
.t:
    test [bx + W16W_STYLE + 2], ah
    pop ax
    jz .no
    clc
    ret
.ctl:
    push ax                         ; a SCROLLBAR control: its one bar
    call w16_sbctlbar
    cmp al, ah
    pop ax
    jne .no
    clc
    ret
.no:
    stc
    ret

; w16_sbctlbar - BX = a SCROLLBAR control -> AH = its bar: SB_VERT with
; SBS_VERT, else SB_HORZ (124.22.8). Keeps the rest.
w16_sbctlbar:
    mov ah, SB_HORZ
    test byte [bx + W16W_STYLE], SBS_VERT
    jz .out
    mov ah, SB_VERT
.out:
    ret

; -----------------------------------------------------------------------------
; w16_sbgeom - BX = a record, AL = a bar it has, DX = the position to show:
; [w16_sbg] = where the bar and its parts are on the screen. CF=1 the window
; is not shown. Keeps every register.
; -----------------------------------------------------------------------------
w16_sbgeom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, dx                      ; BP = the position
    mov [w16d_bar], al
    ICALL w16_origin                ; AX/DX = the client, CX/SI its size
    jc .out
    cmp byte [bx + W16W_KIND], W16Q_SCROLL
    jne .frame
    mov [w16_sbg + SG_X0], ax       ; a SCROLLBAR control is all bar
    mov [w16d_wl], ax
    add ax, cx
    dec ax
    mov [w16_sbg + SG_X1], ax
    mov [w16d_wr], ax
    mov [w16_sbg + SG_Y0], dx
    mov [w16d_wt], dx
    add dx, si
    dec dx
    mov [w16_sbg + SG_Y1], dx
    mov [w16d_wb], dx
    mov cx, [w16_sbg + SG_Y0]       ; CX..DX along it
    cmp byte [w16d_bar], SB_VERT
    je .along
    mov cx, [w16_sbg + SG_X0]
    mov dx, [w16_sbg + SG_X1]
    jmp .along
.frame:
    push ax
    ICALL w16_bord
    mov di, ax                      ; DI = the border
    pop ax
    push ax                         ; the window's right: the client's, the
    add ax, cx                      ;   bar less the border, and the border
    dec ax
    add ax, di
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI
    jz .r
    add ax, W16D_SBW
    sub ax, di
.r:
    mov [w16d_wr], ax
    mov ax, dx                      ; ...its bottom
    add ax, si
    dec ax
    add ax, di
    test word [bx + W16W_STYLE + 2], WS_HSCROLL_HI
    jz .b
    add ax, W16D_SBW
    sub ax, di
.b:
    mov [w16d_wb], ax
    pop ax
    sub ax, di
    mov [w16d_wl], ax               ; ...its left and top
    sub dx, di
    mov [w16d_wt], dx
    cmp byte [w16d_bar], SB_VERT
    jne .horz
    mov ax, [w16d_wr]               ; down the right, above a horizontal bar
    mov [w16_sbg + SG_X1], ax
    sub ax, W16D_SBW - 1
    mov [w16_sbg + SG_X0], ax
    mov ax, [w16d_wt]
    mov [w16_sbg + SG_Y0], ax
    mov ax, [w16d_wb]
    test word [bx + W16W_STYLE + 2], WS_HSCROLL_HI
    jz .vb
    sub ax, W16D_SBW
.vb:
    mov [w16_sbg + SG_Y1], ax
    mov cx, [w16_sbg + SG_Y0]       ; CX..DX along it
    mov dx, ax
    jmp short .along
.horz:
    mov ax, [w16d_wb]               ; along the bottom, left of a vertical one
    mov [w16_sbg + SG_Y1], ax
    sub ax, W16D_SBW - 1
    mov [w16_sbg + SG_Y0], ax
    mov ax, [w16d_wl]
    mov [w16_sbg + SG_X0], ax
    mov ax, [w16d_wr]
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI
    jz .hb
    sub ax, W16D_SBW
.hb:
    mov [w16_sbg + SG_X1], ax
    mov cx, [w16_sbg + SG_X0]
    mov dx, ax
.along:
    add cx, W16D_SBW                ; the track between the arrows
    mov [w16_sbg + SG_T0], cx
    sub dx, W16D_SBW
    mov [w16_sbg + SG_T1], dx
    sub dx, cx
    inc dx
    sub dx, W16D_SBW                ; DX = the pixels the thumb can move
    mov [w16_sbg + SG_FREE], dx
    mov [w16_sbg + SG_TH], cx
    js .done                        ; no room for a thumb
    mov al, [w16d_bar]
    call w16_sbfld
    mov cx, [si + 4]                ; CX = the range
    sub cx, [si + 2]
    jle .done
    mov ax, bp                      ; the position, inside the range
    sub ax, [si + 2]
    jge .p0
    xor ax, ax
.p0:
    cmp ax, cx
    jle .p1
    mov ax, cx
.p1:
    mul word [w16_sbg + SG_FREE]    ; free x (pos - min) / range
    div cx
    add [w16_sbg + SG_TH], ax
.done:
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_sbshown - BX = a record, AL = a bar -> DX = the position its thumb is
; drawn at: the dragged one while its thumb is held. Keeps the rest.
w16_sbshown:
    push si
    call w16_sbfld
    mov dx, [si]
    pop si
    cmp byte [w16_sbcode], SB_THUMBTRACK
    jne .out
    cmp al, [w16_sbbar]
    jne .out
    push ax
    ICALL w16_rec2hwnd
    cmp ax, [w16_sbtrk]
    pop ax
    jne .out
    mov dx, [w16_sbdpos]
.out:
    ret

; -----------------------------------------------------------------------------
; w16_sbdraw - BX = a record, AL = a bar it has, CL = 1 only the track and
; the thumb (the position moved) / 0 all of it: drawn, cut to the window's
; frame and its parent's client. Opens and closes a DC of its own. Keeps
; every register.
; -----------------------------------------------------------------------------
w16_sbdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    call w16_sbhas
    jc .ret
    mov [w16d_bar], al
    mov [w16d_sbonly], cl
    call w16_sbshown                ; DX
    call w16_sbgeom
    jc .ret
    mov [w16d_rec], bx
    xor al, al
    ICALL w16_dcopen
    cmp byte [w16_dchide], 0
    jne .close
    call w16_sbclip
    jc .close
    cmp byte [w16d_bar], SB_VERT
    jne .h
    mov ax, [w16_sbg + SG_X0]       ; the list box's bar, drawn here
    mov [w16d_bxx], ax
    mov ax, [w16_sbg + SG_X1]
    mov [w16d_ir], ax
    mov ax, [w16_sbg + SG_Y0]
    mov [w16d_it], ax
    mov ax, [w16_sbg + SG_Y1]
    mov [w16d_ib], ax
    call w16d_vbar
    jmp short .close
.h:
    call w16d_hbar
.close:
    ICALL w16_dcclose
.ret:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_sbclip - the DC's clip made [w16_sbg]'s bar, cut to the window's
; parent's client (a top-level window's frame is its own). CF=1 none of it
; shows. Keeps every register.
w16_sbclip:
    push ax
    push bx
    mov ax, [w16_sbg + SG_X0]
    mov [w16_cl], ax
    mov ax, [w16_sbg + SG_Y0]
    mov [w16_ct], ax
    mov ax, [w16_sbg + SG_X1]
    mov [w16_cr], ax
    mov ax, [w16_sbg + SG_Y1]
    mov [w16_cb], ax
    ICALL w16_parent
    jc .cut
    ICALL w16_wclip                 ; [w16_kl]..[w16_kb]
    jc .none
    mov ax, [w16_kl]
    cmp ax, [w16_cl]
    jle .l
    mov [w16_cl], ax
.l:
    mov ax, [w16_kt]
    cmp ax, [w16_ct]
    jle .t
    mov [w16_ct], ax
.t:
    mov ax, [w16_kr]
    cmp ax, [w16_cr]
    jge .r
    mov [w16_cr], ax
.r:
    mov ax, [w16_kb]
    cmp ax, [w16_cb]
    jge .cut
    mov [w16_cb], ax
.cut:
    mov ax, [w16_cl]
    cmp ax, [w16_cr]
    jg .none
    mov ax, [w16_ct]
    cmp ax, [w16_cb]
    jg .none
    clc
    jmp short .out
.none:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16d_vbar - the vertical bar down [w16d_bxx] (its left line)..[w16d_ir],
; [w16d_it]..[w16d_ib], its thumb at [w16_sbg + SG_TH] unless SG_FREE is
; negative; with [w16d_sbonly] the track and the thumb alone. The list box's
; bar too (SPEC.md 124.13). Keeps BX.
; -----------------------------------------------------------------------------
w16d_vbar:
    push bx
    cmp byte [w16d_sbonly], 0
    jne .track
    mov si, COLOR_WINDOWFRAME       ; its left edge
    mov ax, [w16d_bxx]
    mov bx, [w16d_it]
    mov cx, ax
    mov dx, [w16d_ib]
    call w16d_fill
    mov ax, [w16d_it]               ; the up box
    mov [w16d_by], ax
    mov si, w16d_bmup  ; XSEC-OK: w16d_sbox reads it through cs:
    call w16d_sbox
    mov ax, [w16d_ib]               ; the down box
    sub ax, W16D_SBW - 1
    mov [w16d_by], ax
    mov si, w16d_bmdown  ; XSEC-OK: w16d_sbox reads it through cs:
    call w16d_sbox
.track:
    mov si, COLOR_SCROLLBAR         ; the track above the thumb and below it
    mov ax, [w16d_bxx]
    inc ax
    mov bx, [w16d_it]
    add bx, W16D_SBW
    mov cx, [w16d_ir]
    mov dx, [w16d_ib]
    sub dx, W16D_SBW
    cmp word [w16_sbg + SG_FREE], 0
    jl .nothumb
    push dx
    mov dx, [w16_sbg + SG_TH]
    dec dx
    call w16d_fill
    pop dx
    mov bx, [w16_sbg + SG_TH]
    add bx, W16D_SBW
    call w16d_fill
    mov ax, [w16_sbg + SG_TH]       ; the thumb, a plain box
    mov [w16d_by], ax
    xor si, si
    call w16d_sbox
    jmp short .out
.nothumb:
    call w16d_fill
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16d_hbar - [w16_sbg]'s horizontal bar: its top line, the left and right
; boxes, the track and the thumb; with [w16d_sbonly] the track and the thumb
; alone. Keeps BX.
; -----------------------------------------------------------------------------
w16d_hbar:
    push bx
    cmp byte [w16d_sbonly], 0
    jne .track
    mov si, COLOR_WINDOWFRAME       ; its top edge
    mov ax, [w16_sbg + SG_X0]
    mov bx, [w16_sbg + SG_Y0]
    mov cx, [w16_sbg + SG_X1]
    mov dx, bx
    call w16d_fill
    mov ax, [w16_sbg + SG_X0]       ; the left box
    mov si, w16d_bmleft  ; XSEC-OK: w16d_hbox reads it through cs:
    call w16d_hbox
    mov ax, [w16_sbg + SG_X1]       ; the right box
    sub ax, W16D_SBW - 1
    mov si, w16d_bmright  ; XSEC-OK: w16d_hbox reads it through cs:
    call w16d_hbox
.track:
    mov si, COLOR_SCROLLBAR         ; the track left of the thumb and right
    mov ax, [w16_sbg + SG_T0]       ;   of it
    mov bx, [w16_sbg + SG_Y0]
    inc bx
    mov cx, [w16_sbg + SG_T1]
    mov dx, [w16_sbg + SG_Y1]
    cmp word [w16_sbg + SG_FREE], 0
    jl .nothumb
    push cx
    mov cx, [w16_sbg + SG_TH]
    dec cx
    call w16d_fill
    pop cx
    mov ax, [w16_sbg + SG_TH]
    add ax, W16D_SBW
    call w16d_fill
    mov ax, [w16_sbg + SG_TH]       ; the thumb, a plain box
    xor si, si
    call w16d_hbox
    jmp short .out
.nothumb:
    call w16d_fill
.out:
    pop bx
    ret

; w16d_hbox - a horizontal bar's box at AX across, W16D_SBW wide, under the
; bar's top line: a black line left and right, white, and the 4 x 7 picture
; at CS:SI in the middle (SI = 0: none). Keeps every register.
w16d_hbox:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, si
    mov si, COLOR_WINDOWFRAME
    mov bx, [w16_sbg + SG_Y0]
    inc bx
    mov dx, [w16_sbg + SG_Y1]
    mov cx, ax
    call w16d_fill                  ; its left
    add ax, W16D_SBW - 1
    mov cx, ax
    call w16d_fill                  ; its right
    sub ax, W16D_SBW - 2            ; AX..CX = inside it
    dec cx
    mov si, COLOR_WINDOW
    or di, di
    jz .plain
    push dx                         ; white round the picture
    mov dx, bx
    add dx, 3
    call w16d_fill                  ; above it
    pop dx
    push bx
    add bx, 11
    call w16d_fill                  ; below it
    pop bx
    add bx, 4
    push dx
    mov dx, bx
    add dx, 6
    push cx
    mov cx, ax
    add cx, 4
    call w16d_fill                  ; left of it
    pop cx
    push ax
    add ax, 9
    call w16d_fill                  ; right of it
    pop ax
    pop dx
    push ax                         ; the picture: seven rows of two bytes
    mov cx, 7
    mov si, w16d_pbuf
.cp:
    mov dx, [cs:di]
    mov [si], dx
    inc di
    inc di
    inc si
    inc si
    loop .cp
    pop ax
    add ax, 5
    mov [w16_tx], ax
    mov [w16_ty], bx
    mov ax, (COLOR_WINDOW << 8) | COLOR_WINDOWTEXT
    call w16d_colors
    mov si, w16d_pbuf
    mov cx, 7
    mov dx, 4
    ICALL w16_bmp
    jmp short .out
.plain:
    call w16d_fill
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16d_bmleft:  db 0x10,0, 0x30,0, 0x70,0, 0xF0,0, 0x70,0, 0x30,0, 0x10,0
w16d_bmright: db 0x80,0, 0xC0,0, 0xE0,0, 0xF0,0, 0xE0,0, 0xC0,0, 0x80,0

; w16_sbctl - the SCROLLBAR control's procedure (124.22.8): BX = it, [w16_sm_*]
; the message -> DX:AX. It paints its bar; a press on it is w16_sbhit's,
; and the program sets and reads it with SB_CTL. Keeps BX, SI, DI.
w16_sbctl:
    cmp word [w16_sm_msg], WM_PAINT
    jne .z
    call w16d_popen
    jc .z
    call w16d_pclose
    push ax
    push cx
    call w16_sbctlbar
    mov al, ah
    xor cl, cl
    call w16_sbdraw
    pop cx
    pop ax
.z:
    xor ax, ax
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; w16_ncpaint - BX = a record whose scroll bars are owed: drawn, and the
; corner between two (SPEC.md 124.22.1). Keeps every register.
; -----------------------------------------------------------------------------
w16_ncpaint:
    and byte [bx + W16W_F2], ~W16W2_NCDIRTY
    push ax
    push cx
    xor cl, cl
    mov al, SB_VERT
    call w16_sbdraw
    mov al, SB_HORZ
    call w16_sbdraw
    mov ax, [bx + W16W_STYLE + 2]
    and ax, WS_VSCROLL_HI | WS_HSCROLL_HI
    cmp ax, WS_VSCROLL_HI | WS_HSCROLL_HI
    jne .out
    push bx                         ; the corner: white
    push dx
    push si
    xor dx, dx
    mov al, SB_VERT
    call w16_sbgeom                 ; [w16d_wr], [w16d_wb]
    jc .c
    xor al, al
    ICALL w16_dcopen
    cmp byte [w16_dchide], 0
    jne .cc
    mov ax, [w16d_wr]
    mov [w16_sbg + SG_X1], ax
    sub ax, W16D_SBW - 1
    mov [w16_sbg + SG_X0], ax
    mov ax, [w16d_wb]
    mov [w16_sbg + SG_Y1], ax
    sub ax, W16D_SBW - 1
    mov [w16_sbg + SG_Y0], ax
    call w16_sbclip
    jc .cc
    mov si, COLOR_WINDOW
    mov ax, [w16_sbg + SG_X0]
    mov bx, [w16_sbg + SG_Y0]
    mov cx, [w16_sbg + SG_X1]
    mov dx, [w16_sbg + SG_Y1]
    call w16d_fill
.cc:
    ICALL w16_dcclose
.c:
    pop si
    pop dx
    pop bx
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_sbhit - BX = a window with a bar, [w16_mx]/[w16_my] where the left
; button went down: CF=0 it was on a bar, which has the mouse now, and the
; window its first WM_VSCROLL or WM_HSCROLL. CF=1 it was not. The gfx lock
; is held. Keeps every register.
; -----------------------------------------------------------------------------
w16_sbhit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, SB_VERT
.bar:
    call w16_sbhas
    jc .next
    push ax
    call w16_sbfld
    mov dx, [si]
    call w16_sbgeom
    pop ax
    jc .next
    mov cx, [w16_mx]
    mov dx, [w16_my]
    cmp cx, [w16_sbg + SG_X0]
    jl .next
    cmp cx, [w16_sbg + SG_X1]
    jg .next
    cmp dx, [w16_sbg + SG_Y0]
    jl .next
    cmp dx, [w16_sbg + SG_Y1]
    jle .on
.next:
    dec al
    jns .bar
    stc
    jmp .out
.on:
    mov [w16_sbbar], al
    push si
    call w16_sbfld
    mov si, [si]
    mov [w16_sbdpos], si
    pop si
    cmp al, SB_VERT                 ; DX = the point along the bar
    je .v
    mov dx, cx
.v:
    mov ah, SB_LINEUP
    cmp dx, [w16_sbg + SG_T0]
    jl .code
    mov ah, SB_LINEDOWN
    cmp dx, [w16_sbg + SG_T1]
    jg .code
    cmp word [w16_sbg + SG_FREE], 0
    jl .none                        ; no thumb: the track does nothing
    mov ah, SB_PAGEUP
    cmp dx, [w16_sbg + SG_TH]
    jl .code
    mov ah, SB_PAGEDOWN
    mov cx, [w16_sbg + SG_TH]
    add cx, W16D_SBW
    cmp dx, cx
    jge .code
    sub dx, [w16_sbg + SG_TH]       ; the thumb: held where it was pressed
    mov [w16_sbgrab], dx
    call w16_sbfld
    mov dx, [si]
    mov [w16_sbdpos], dx
    mov ah, SB_THUMBTRACK
.code:
    mov [w16_sbcode], ah
    push ax
    ICALL w16_rec2hwnd
    mov [w16_sbtrk], ax
    pop ax
    cmp ah, SB_THUMBTRACK
    je .held
    call w16_sbsend                 ; the first step now, and again while
    call OSAPI_GET_TICKS            ;   it is held
    add ax, W16SB_FIRST
    mov [w16_sbdue], ax
    ICALL w16_tarm
.held:
.none:
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_sbsend - post the held bar's [w16_sbcode] to its window: lParam the
; dragged position. Keeps every register.
w16_sbsend:
    push ax
    mov ah, [w16_sbcode]
    call w16_sbpost
    pop ax
    ret

; w16_sbpost - AH = an SB_* code: WM_VSCROLL or WM_HSCROLL to the held bar's
; window, lParam [w16_sbdpos] and 0 - or, the bar being a SCROLLBAR control,
; to the control's parent with the control in lParam's high word (124.22.8).
; Keeps every register.
w16_sbpost:
    push ax
    push bx
    mov al, ah
    xor ah, ah
    mov [w16_sm_wp], ax
    mov ax, [w16_sbtrk]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_lp + 2], 0
    ICALL w16_hwnd2rec
    jc .win
    cmp byte [bx + W16W_KIND], W16Q_SCROLL
    jne .win
    mov ax, [bx + W16W_PARENT]
    mov [w16_sm_hwnd], ax
    mov ax, [w16_sbtrk]
    mov [w16_sm_lp + 2], ax
.win:
    mov ax, WM_HSCROLL
    cmp byte [w16_sbbar], SB_VERT
    jne .m
    inc ax
.m:
    mov [w16_sm_msg], ax
    mov ax, [w16_sbdpos]
    mov [w16_sm_lp], ax
    xor al, al
    ICALL w16_post
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_sbmouse - the held bar's mouse: [w16_sm_msg] a move or the release,
; [w16_mx]/[w16_my] where. The thumb follows a drag (SB_THUMBTRACK at each new
; position); the release ends it (SB_THUMBPOSITION for the thumb, then
; SB_ENDSCROLL). The gfx lock is held. Keeps every register.
; -----------------------------------------------------------------------------
w16_sbmouse:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [w16_sbtrk]
    ICALL w16_hwnd2rec
    jc .gone                        ; the window has gone
    mov al, [w16_sbbar]
    cmp word [w16_sm_msg], WM_LBUTTONUP
    je .up
    cmp word [w16_sm_msg], WM_MOUSEMOVE
    jne .out
    cmp byte [w16_sbcode], SB_THUMBTRACK
    jne .out
    mov dx, [w16_sbdpos]
    call w16_sbgeom
    jc .out
    cmp word [w16_sbg + SG_FREE], 0
    jle .out
    mov dx, [w16_my]                ; where the thumb's first pixel would be
    cmp al, SB_VERT
    je .v
    mov dx, [w16_mx]
.v:
    sub dx, [w16_sbgrab]
    sub dx, [w16_sbg + SG_T0]
    jge .d0
    xor dx, dx
.d0:
    cmp dx, [w16_sbg + SG_FREE]
    jle .d1
    mov dx, [w16_sbg + SG_FREE]
.d1:
    call w16_sbfld                  ; ...as a position: min + the share of
    mov ax, [si + 4]                ;   the range, rounded
    sub ax, [si + 2]
    mul dx
    mov cx, [w16_sbg + SG_FREE]
    push cx
    shr cx, 1
    add ax, cx
    adc dx, 0
    pop cx
    div cx
    add ax, [si + 2]
    cmp ax, [w16_sbdpos]
    je .out
    mov [w16_sbdpos], ax
    mov al, [w16_sbbar]
    call w16_sblocked               ; the thumb where it is dragged
    mov ah, SB_THUMBTRACK
    call w16_sbpost
    jmp short .out
.up:
    cmp byte [w16_sbcode], SB_THUMBTRACK
    jne .end
    mov ah, SB_THUMBPOSITION
    call w16_sbpost
.end:
    mov ah, SB_ENDSCROLL
    call w16_sbpost
    mov byte [w16_sbcode], SB_ENDSCROLL
    mov word [w16_sbtrk], 0
    mov al, [w16_sbbar]             ; the thumb back where the window put it
    call w16_sblocked
    jmp short .tarm
.gone:
    mov byte [w16_sbcode], SB_ENDSCROLL
    mov word [w16_sbtrk], 0
.tarm:
    ICALL w16_tarm
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_sblocked - w16_sbdraw's track and thumb, from a callback that holds the
; gfx lock. BX = the record, AL = the bar. Keeps every register.
w16_sblocked:
    push cx
    push word [w16_locked]          ; held already, or held by us: as it was
    mov byte [w16_locked], 1
    mov cl, 1
    call w16_sbdraw
    pop word [w16_locked]
    pop cx
    ret

; w16_sbwait - AX = the ticks to the sound queue's next note (w16s_wait):
; AX = the nearer of that and a held arrow's next repeat. Keeps the rest.
w16_sbwait:
    cmp word [w16_sbtrk], 0
    je .out
    cmp byte [w16_sbcode], SB_PAGEDOWN
    ja .out
    push dx
    push ax
    call OSAPI_GET_TICKS
    mov dx, [w16_sbdue]
    sub dx, ax
    jg .ahead
    mov dx, 1
.ahead:
    pop ax
    cmp dx, ax
    jae .keep
    mov ax, dx
.keep:
    pop dx
.out:
    ret

; w16_sbtick - the os8088 one-shot (w16s_tick): a held arrow or track whose
; repeat is due steps again. Keeps every register.
w16_sbtick:
    cmp word [w16_sbtrk], 0
    je .out
    cmp byte [w16_sbcode], SB_PAGEDOWN
    ja .out
    push ax
    push dx
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [w16_sbdue]
    js .not
    add dx, W16SB_EVERY
    mov [w16_sbdue], dx
    call w16_sbsend
.not:
    pop dx
    pop ax
.out:
    ret

; =============================================================================
; The calls (SPEC.md 124.22.1)
; =============================================================================

; w16_sbargs - ES:BX = the args, the HWND at +DI, the bar at +DI-2 -> BX = the
; record, AL = the bar. CF=1 no window, or SB_CTL. Clobbers CX.
w16_sbargs:
    mov ax, [es:bx + di]
    mov cl, [es:bx + di - 2]
    push cx
    ICALL w16_hwnd2rec
    pop ax
    jc .out
    cmp al, SB_CTL                  ; SB_CTL: a SCROLLBAR control's own bar
    je .ctl
    cmp al, SB_VERT
    ja .no
    clc
.out:
    ret
.ctl:
    cmp byte [bx + W16W_KIND], W16Q_SCROLL
    jne .no
    push cx
    call w16_sbctlbar
    mov al, ah
    pop cx
    clc
    ret
.no:
    stc
    ret

; SetScrollPos (USER.62) - args: +0 bRedraw, +2 nPos, +4 nBar, +6 hWnd. The
; old position
W16LIB w16u_setscrollpos
    push si
    push di
    ICALL w16_args
    push word [es:bx]
    push word [es:bx + 2]
    mov di, 6
    call w16_sbargs
    pop dx                          ; DX = the position
    pop cx                          ; CX = bRedraw
    jc .zero
    call w16_sbfld
    cmp dx, [si + 2]                ; cut to the range
    jge .lo
    mov dx, [si + 2]
.lo:
    cmp dx, [si + 4]
    jle .hi
    mov dx, [si + 4]
.hi:
    mov di, [si]
    mov [si], dx
    cmp dx, di
    je .same
    jcxz .same
    mov cl, 1
    call w16_sbdraw
.same:
    mov ax, di
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; GetScrollPos (USER.63) - args: +0 nBar, +2 hWnd
W16LIB w16u_getscrollpos
    push si
    push di
    ICALL w16_args
    mov di, 2
    call w16_sbargs
    mov ax, 0
    jc .out
    call w16_sbfld
    mov ax, [si]
.out:
    pop di
    pop si
    ret

; GetScrollRange (USER.65) - args: +0 lpMaxPos, +4 lpMinPos, +8 nBar,
; +10 hWnd
W16LIB w16u_getscrollrng
    push si
    push di
    ICALL w16_args
    mov di, 10
    call w16_sbargs
    mov cx, 0
    mov dx, cx
    jc .put
    call w16_sbfld
    mov cx, [si + 2]
    mov dx, [si + 4]
.put:
    ICALL w16_args
    les di, [es:bx + 4]
    mov [es:di], cx
    ICALL w16_args
    les di, [es:bx]
    mov [es:di], dx
    mov ax, 1
    pop di
    pop si
    ret

; SetScrollRange (USER.64) - args: +0 bRedraw, +2 nMaxPos, +4 nMinPos,
; +6 nBar, +8 hWnd. A range of nothing hides the bar, any other shows it
W16LIB w16u_setscrollrng
    push si
    push di
    ICALL w16_args
    push word [es:bx]
    push word [es:bx + 2]
    push word [es:bx + 4]
    mov di, 8
    call w16_sbargs
    pop cx                          ; CX = min
    pop dx                          ; DX = max
    pop di                          ; DI = bRedraw
    jc .out
    call w16_sbfld
    mov [si + 2], cx
    mov [si + 4], dx
    push dx
    mov dx, [si]                    ; the position, inside it
    cmp dx, cx
    jge .p0
    mov dx, cx
.p0:
    pop cx
    cmp dx, cx
    jle .p1
    mov dx, cx
.p1:
    mov [si], dx
    cmp byte [bx + W16W_KIND], W16Q_SCROLL
    je .drawn                       ; a control's bar is never hidden
    mov dx, [si + 4]                ; shown when min != max
    cmp dx, [si + 2]
    mov ah, 1
    jne .show
    xor ah, ah
.show:
    call w16_sbshow                 ; CF=1: it changed, and all is redrawn
    jc .out
.drawn:
    or di, di
    jz .out
    mov cl, 1
    call w16_sbdraw
.out:
    mov ax, 1
    pop di
    pop si
    ret

; ShowScrollBar (USER.267) - args: +0 bShow, +2 wBar, +4 hWnd
W16LIB w16u_showsb
    push si
    push di
    ICALL w16_args
    mov ah, [es:bx]
    push ax
    mov di, 4
    call w16_sbargs.any
    pop cx
    jc .out
    mov ah, ch
    or ah, ah
    jz .b
    mov ah, 1
.b:
    cmp al, SB_BOTH
    jne .one
    mov al, SB_HORZ
    call w16_sbshow
    mov al, SB_VERT
.one:
    cmp al, SB_VERT
    ja .out
    call w16_sbshow
.out:
    mov ax, 1
    pop di
    pop si
    ret

; w16_sbargs.any - w16_sbargs taking any bar
w16_sbargs.any:
    mov ax, [es:bx + di]
    mov cl, [es:bx + di - 2]
    push cx
    ICALL w16_hwnd2rec
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_sbshow - BX = a record, AL = a bar, AH = 1 shown / 0 hidden: its style
; bit made so. CF=1 that changed the window - its client is another size, so
; it is sent WM_SIZE, posted, and all of it is invalid. Keeps every register.
; -----------------------------------------------------------------------------
w16_sbshow:
    push ax
    push cx
    push dx
    push si
    mov cl, WS_HSCROLL_HI
    or al, al
    jz .m
    mov cl, WS_VSCROLL_HI
.m:
    mov ch, [bx + W16W_STYLE + 2]
    and ch, cl
    or ah, ah
    jz .hide
    or ch, ch
    jnz .same
    or [bx + W16W_STYLE + 2], cl
    jmp short .changed
.hide:
    or ch, ch
    jz .same
    not cl
    and [bx + W16W_STYLE + 2], cl
.changed:
    or byte [bx + W16W_F2], W16W2_NCDIRTY
    xor ax, ax                      ; the whole client, erased
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    ICALL w16_inval
    ICALL w16_rec2hwnd
    ICALL w16_sizemsg
    xor al, al
    ICALL w16_post
    stc
    jmp short .out
.same:
    clc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; ScrollWindow (USER.61, SPEC.md 124.22.2) - args: +0 lpClipRect,
; +4 lpRect, +8 YAmount, +10 XAmount, +12 hWnd
; =============================================================================
W16LIB w16u_scrollwnd
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 12]
    call w16_carpre                 ; the caret off while it scrolls
    push word [es:bx + 8]
    push word [es:bx + 10]
    ICALL w16_hwnd2rec
    pop cx                          ; CX = dx
    pop dx                          ; DX = dy
    jc .out
    push dx
    push cx
    ICALL w16_csize                 ; the client, or lpRect cut by lpClipRect
    mov [w16_srr], cx
    mov [w16_srb], dx
    xor ax, ax
    mov [w16_srl], ax
    mov [w16_srt], ax
    push bx
    ICALL w16_args
    les si, [es:bx + 4]
    call w16_srcut
    ICALL w16_args
    les si, [es:bx]
    call w16_srcut
    pop bx
    pop cx
    pop dx
    or cx, cx                       ; across: the whole rectangle again
    jnz .all
    mov byte [w16_erase], 1
    call w16_scroll
    jmp short .out
.all:
    mov byte [w16_erase], 1
    call w16_srinval
.out:
    call w16_carpost
    mov ax, 1
    pop di
    pop si
    ret

; w16_srcut - ES:SI = a RECT, or 0:0: [w16_srl]..[w16_srb] cut to it.
; Keeps the rest.
w16_srcut:
    push ax
    mov ax, es
    or ax, si
    jz .out
    mov ax, [es:si]
    cmp ax, [w16_srl]
    jle .l
    mov [w16_srl], ax
.l:
    mov ax, [es:si + 2]
    cmp ax, [w16_srt]
    jle .t
    mov [w16_srt], ax
.t:
    mov ax, [es:si + 4]
    cmp ax, [w16_srr]
    jge .r
    mov [w16_srr], ax
.r:
    mov ax, [es:si + 6]
    cmp ax, [w16_srb]
    jge .out
    mov [w16_srb], ax
.out:
    pop ax
    ret

; w16_srinval - BX = a window: [w16_srl]..[w16_srb] invalid. Keeps every
; register.
w16_srinval:
    push ax
    push cx
    push dx
    push si
    mov ax, [w16_srl]
    mov cx, [w16_srt]
    mov dx, [w16_srr]
    mov si, [w16_srb]
    ICALL w16_inval
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_scroll - BX = a window, [w16_srl]..[w16_srb] a rectangle of its client
; (right and bottom exclusive), DX = how far its content moves down (up when
; negative), [w16_erase] whether what it uncovers is erased: the pixels moved
; with gfx_scroll (SPEC.md 5.5) and the rows uncovered made invalid, with the
; columns either side of the byte-aligned part gfx_scroll can move. Anything
; it refuses - a covered window, a move as large as the rectangle - is the
; whole rectangle invalid instead. An update pending in the window moves
; with its pixels. Keeps every register.
; -----------------------------------------------------------------------------
w16_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, dx                      ; DI = dy
    mov ax, [w16_srr]
    sub ax, [w16_srl]
    jle .done
    mov ax, [w16_srb]
    sub ax, [w16_srt]
    jle .done
    or di, di
    jz .done
    mov cx, di                      ; |dy| at least the height: all of it
    or cx, cx
    jns .abs
    neg cx
.abs:
    cmp cx, ax
    jge .whole
    mov [w16d_rec], bx
    xor al, al
    ICALL w16_dcopen
    cmp byte [w16_dchide], 0
    jne .closew
    mov ax, [w16_srl]               ; on the screen, inclusive: and all of it
    add ax, [w16_ox]                ;   inside the DC's clip, or gfx_scroll
    cmp ax, [w16_cl]                ;   would move what is not ours
    jl .closew
    mov [w16d_l], ax
    mov ax, [w16_srt]
    add ax, [w16_oy]
    cmp ax, [w16_ct]
    jl .closew
    mov [w16d_t], ax
    mov ax, [w16_srr]
    add ax, [w16_ox]
    dec ax
    cmp ax, [w16_cr]
    jg .closew
    mov [w16d_r], ax
    mov ax, [w16_srb]
    add ax, [w16_oy]
    dec ax
    cmp ax, [w16_cb]
    jg .closew
    mov [w16d_b], ax
    mov ax, [w16d_l]                ; the byte-aligned part
    add ax, 7
    and al, 0xF8
    mov [w16d_il], ax
    mov cx, [w16d_r]
    inc cx
    and cl, 0xF8
    dec cx
    mov [w16d_ir], cx
    cmp ax, cx
    jg .closew
    mov bx, [w16d_t]
    mov dx, [w16d_b]
    mov si, di
    neg si                          ; gfx_scroll's: positive moves it up
    call OSAPI_GFX_SCROLL
    jc .closew
    ICALL w16_dcclose
    mov bx, [w16d_rec]
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jz .new
    add [bx + W16W_UT], di          ; the update pending moves with it
    add [bx + W16W_UB], di
.new:
    mov ax, [w16_srl]               ; the rows it uncovered
    mov dx, [w16_srr]
    mov cx, [w16_srt]
    mov si, [w16_srb]
    or di, di
    js .up
    add di, cx                      ; down: the top rows
    mov si, di
    jmp short .strip
.up:
    add di, si                      ; up: the bottom rows
    mov cx, di
.strip:
    ICALL w16_inval
    mov cx, [w16_srt]               ; the columns either side of what moved
    mov si, [w16_srb]
    mov dx, [w16d_il]
    sub dx, [w16_ox]
    cmp dx, ax
    jle .rcol
    ICALL w16_inval                 ; [srl]..[il)
.rcol:
    mov ax, [w16d_ir]
    inc ax
    sub ax, [w16_ox]
    mov dx, [w16_srr]
    cmp ax, dx
    jge .done
    ICALL w16_inval
    jmp short .done
.closew:
    ICALL w16_dcclose
    mov bx, [w16d_rec]
.whole:
    call w16_srinval
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; GDI: TabbedTextOut and Escape (SPEC.md 124.22.4)
; =============================================================================

; Escape (GDI.38) - args: +0 lpOutData, +4 lpInData, +8 nCount, +10 nEscape,
; +12 hDC. 0: no escape is supported, there being no printer
W16LIB w16g_escape
    xor ax, ax
    ret

section .text
