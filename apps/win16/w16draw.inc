; =============================================================================
; os8088 - apps/win16/w16draw.inc
;
; WHAT SOLITAIRE CALLS (SPEC.md 124.20.4): the rectangle calls, InvertRect,
; FrameRect and DrawText; RegisterWindowMessage, AdjustWindowRect and
; SetActiveWindow; and GDI's LineDDA, PtVisible, SetBrushOrg,
; CreateCompatibleBitmap, GetDCOrg and GetPixel. All of it is the library
; part's (SPEC.md 124.19.3): a call into the image is ICALL, and the data it
; reads itself stays in .text.
; =============================================================================

DT_CENTER     equ 0x0001
DT_RIGHT      equ 0x0002
DT_VCENTER    equ 0x0004
DT_BOTTOM     equ 0x0008
DT_WORDBREAK  equ 0x0010
DT_SINGLELINE equ 0x0020
DT_EXPANDTABS equ 0x0040
DT_NOCLIP     equ 0x0100
DT_CALCRECT   equ 0x0400
DT_NOPREFIX   equ 0x0800

section .w16dlg

; CopyRect (USER.74) - args: +0 lprcSrc, +4 lprcDst
W16LIB w16u_copyrect
    push si
    push di
    push ds
    ICALL w16_args
    lds si, [es:bx]
    les di, [es:bx + 4]
    cld
    movsw
    movsw
    movsw
    movsw
    pop ds
    pop di
    pop si
    mov ax, 1
    ret

; IntersectRect (USER.79) - args: +0 lprc2, +4 lprc1, +8 lprcDst. An empty
; intersection answers FALSE with the destination emptied, as Windows did
W16LIB w16u_isectrect
    push si
    push di
    ICALL w16_args
    push es
    push bx
    les si, [es:bx + 4]             ; rc1 into the scratch
    mov di, w16_dtr
    mov cx, 4
.c1:
    mov ax, [es:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .c1
    pop bx
    pop es
    push es
    push bx
    les si, [es:bx]                 ; ...meeting rc2
    mov ax, [es:si]
    cmp ax, [w16_dtr]
    jle .l
    mov [w16_dtr], ax
.l:
    mov ax, [es:si + 2]
    cmp ax, [w16_dtr + 2]
    jle .t
    mov [w16_dtr + 2], ax
.t:
    mov ax, [es:si + 4]
    cmp ax, [w16_dtr + 4]
    jge .r
    mov [w16_dtr + 4], ax
.r:
    mov ax, [es:si + 6]
    cmp ax, [w16_dtr + 6]
    jge .b
    mov [w16_dtr + 6], ax
.b:
    pop bx
    pop es
    les di, [es:bx + 8]
    mov ax, [w16_dtr]
    cmp ax, [w16_dtr + 4]
    jge .empty
    mov ax, [w16_dtr + 2]
    cmp ax, [w16_dtr + 6]
    jge .empty
    mov si, w16_dtr
    cld
    movsw
    movsw
    movsw
    movsw
    mov ax, 1
    jmp short .out
.empty:
    xor ax, ax
    cld
    stosw
    stosw
    stosw
    stosw
.out:
    pop di
    pop si
    ret

; InvertRect (USER.82) - args: +0 lprc, +4 hDC: PatBlt(DSTINVERT) over it
W16LIB w16u_invrect
    ICALL w16_args
    mov cx, [es:bx + 4]
    les bx, [es:bx]
    mov ax, [es:bx]
    mov [w16b_x], ax
    mov dx, [es:bx + 4]
    sub dx, ax
    mov [w16b_cx], dx
    mov ax, [es:bx + 2]
    mov [w16b_y], ax
    mov dx, [es:bx + 6]
    sub dx, ax
    mov [w16b_cy], dx
    push ds
    pop es
    mov bx, w16_rdinv
    xor ax, ax
    call w16_blt
    ret

; FrameRect (USER.83) - args: +0 hBrush, +2 lprc, +6 hDC: the brush along
; each edge, one pixel wide, inside the rectangle
W16LIB w16u_framerect
    push si
    push di
    ICALL w16_args
    cmp word [es:bx + 6], W16_HDC   ; another DC's: nothing drawn
    jne .out
    cmp byte [w16_dcon], 0
    je .out
    mov ax, [es:bx]
    ICALL w16_brcol
    cmp al, W16_NOCOL
    je .out
    mov [w16_gcol], al
    les si, [es:bx + 2]
    mov ax, [es:si]                 ; l, t, r, b
    mov bx, [es:si + 2]
    mov cx, [es:si + 4]
    mov dx, [es:si + 6]
    cmp ax, cx
    jge .out
    cmp bx, dx
    jge .out
    push dx                         ; the top edge
    mov dx, bx
    inc dx
    call .edge
    pop dx
    push bx                         ; the bottom
    mov bx, dx
    dec bx
    call .edge
    pop bx
    push cx                         ; the left
    mov cx, ax
    inc cx
    call .edge
    pop cx
    mov ax, cx                      ; the right
    dec ax
    call .edge
.out:
    pop di
    pop si
    mov ax, 1
    ret
; .edge - fill client AX..CX x BX..DX (right and bottom exclusive). Keeps all
.edge:
    push ax
    push bx
    push cx
    push dx
    push es
    mov [w16_dtr], ax
    mov [w16_dtr + 2], bx
    mov [w16_dtr + 4], cx
    mov [w16_dtr + 6], dx
    push ds
    pop es
    mov bx, w16_dtr
    ICALL w16_rcin
    jc .eout
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    ICALL w16_gfill
.eout:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; DrawText (USER.85) - args: +0 wFormat, +2 lprc, +6 nCount, +8 lpString,
; +12 hDC. Lines at CR/LF and, under DT_WORDBREAK, at the last space that
; fits; each aligned in the rectangle and drawn as TextOut draws, cut to the
; rectangle unless DT_NOCLIP. AX = the height of the text (SPEC.md 124.20.4)
; -----------------------------------------------------------------------------
W16LIB w16u_drawtext
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_dtfmt], ax
    push es
    push bx
    les si, [es:bx + 2]             ; the rectangle, kept
    mov di, w16_dtr
    mov cx, 4
.rc:
    mov ax, [es:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .rc
    pop bx
    pop es
    ; --- the string, its prefixes taken out, into w16_dtbuf ---
    mov cx, [es:bx + 6]
    les si, [es:bx + 8]
    mov di, w16_dtbuf
    xor dx, dx                      ; DX = the characters kept
.cp:
    or cx, cx
    jz .copied                      ; nCount characters
    mov al, [es:si]
    cmp cx, 0xFFFF                  ; -1: to the NUL
    jne .have
    or al, al
    jz .copied
    inc cx
.have:
    inc si
    dec cx
    test word [w16_dtfmt], DT_NOPREFIX
    jnz .keep
    cmp al, '&'                     ; '&x' is x; '&&' is one '&'
    jne .keep
    or cx, cx
    jz .copied
    mov al, [es:si]
    cmp cx, 0xFFFF
    jne .p2
    or al, al
    jz .copied
    inc cx
.p2:
    inc si
    dec cx
.keep:
    cmp al, 9                       ; a tab: spaces to the next eighth
    jne .put                        ;   character, under DT_EXPANDTABS
    test word [w16_dtfmt], DT_EXPANDTABS
    jz .put
    mov al, ' '
.tab:
    cmp dx, W16_DTMAX
    jae .copied
    mov [di], al
    inc di
    inc dx
    test dl, 7
    jnz .tab
    jmp short .cp
.put:
    cmp dx, W16_DTMAX
    jae .copied
    mov [di], al
    inc di
    inc dx
    jmp short .cp
.copied:
    mov [w16_dtn], dx
    ; --- the font, the clip ---
    push ds
    pop es
    mov word [w16_dth], 0
    mov word [w16_dtw], 0
    mov es, [w16_face]
    mov al, [es:W16F_ROWS]
    xor ah, ah
    mov [w16_dtfh], ax
    mov byte [w16_dtdraw], 0
    test word [w16_dtfmt], DT_CALCRECT
    jnz .nodraw
    call w16_args2                  ; the DC: the window's, open
    cmp word [es:bx + 12], W16_HDC
    jne .nodraw
    cmp byte [w16_dcon], 0
    je .nodraw
    mov byte [w16_dtdraw], 1
    mov ax, [w16_cl]                ; the DC's clip, banked, then cut to
    mov [w16_dtclip], ax            ;   the rectangle
    mov ax, [w16_ct]
    mov [w16_dtclip + 2], ax
    mov ax, [w16_cr]
    mov [w16_dtclip + 4], ax
    mov ax, [w16_cb]
    mov [w16_dtclip + 6], ax
    test word [w16_dtfmt], DT_NOCLIP
    jnz .nodraw
    mov ax, [w16_dtr]
    add ax, [w16_ox]
    cmp ax, [w16_cl]
    jle .cl
    mov [w16_cl], ax
.cl:
    mov ax, [w16_dtr + 2]
    add ax, [w16_oy]
    cmp ax, [w16_ct]
    jle .ct
    mov [w16_ct], ax
.ct:
    mov ax, [w16_dtr + 4]
    add ax, [w16_ox]
    dec ax
    cmp ax, [w16_cr]
    jge .cr
    mov [w16_cr], ax
.cr:
    mov ax, [w16_dtr + 6]
    add ax, [w16_oy]
    dec ax
    cmp ax, [w16_cb]
    jge .nodraw
    mov [w16_cb], ax
.nodraw:
    ; --- the first line's y ---
    mov ax, [w16_dtr + 2]
    mov [w16_dty], ax
    test word [w16_dtfmt], DT_SINGLELINE
    jz .lines
    mov ax, [w16_dtr + 6]           ; one line: VCENTER and BOTTOM place it
    sub ax, [w16_dtfh]
    test word [w16_dtfmt], DT_BOTTOM
    jnz .y
    test word [w16_dtfmt], DT_VCENTER
    jz .lines
    add ax, [w16_dtr + 2]
    sar ax, 1
.y:
    mov [w16_dty], ax
.lines:
    xor si, si                      ; SI = the next line's first character
.line:
    cmp si, [w16_dtn]
    jb .more
    or si, si                       ; the empty string is still one line
    jnz .done
    cmp word [w16_dth], 0
    jne .done
.more:
    call w16_dtline                 ; DI = its end, BP = where the next starts
    call w16_dtemit
    mov ax, [w16_dtfh]
    add [w16_dth], ax
    add [w16_dty], ax
    mov si, bp
    cmp si, [w16_dtn]
    jb .line
.done:
    cmp byte [w16_dtdraw], 0        ; the DC's own clip again
    je .calc
    mov ax, [w16_dtclip]
    mov [w16_cl], ax
    mov ax, [w16_dtclip + 2]
    mov [w16_ct], ax
    mov ax, [w16_dtclip + 4]
    mov [w16_cr], ax
    mov ax, [w16_dtclip + 6]
    mov [w16_cb], ax
.calc:
    test word [w16_dtfmt], DT_CALCRECT
    jz .ret
    call w16_args2                  ; the rectangle made to fit: as wide as
    les di, [es:bx + 2]             ;   the widest line, as tall as the lines
    mov ax, [es:di]
    add ax, [w16_dtw]
    mov [es:di + 4], ax
    mov ax, [es:di + 2]
    add ax, [w16_dth]
    mov [es:di + 6], ax
.ret:
    mov ax, [w16_dth]
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; w16_args2 - w16_args, as the handler's own code reaches it. ES:BX
w16_args2:
    ICALL w16_args
    ret

; w16_dtw1 - AL = a character -> AX = its advance in the System font. Keeps
; the rest.
w16_dtw1:
    push cx
    push si
    mov [w16_dtc], al
    mov si, w16_dtc
    mov cx, 1
    ICALL w16_textw
    pop si
    pop cx
    ret

; w16_dtline - SI = a line's first character in w16_dtbuf -> DI = its end
; (exclusive), BP = the next line's first. Clobbers AX, BX, CX, DX.
w16_dtline:
    mov di, si
    xor dx, dx                      ; DX = the width so far
    mov bx, 0xFFFF                  ; BX = the last space that fit
    mov cx, [w16_dtr + 4]
    sub cx, [w16_dtr]               ; CX = the rectangle's width
.c:
    cmp di, [w16_dtn]
    jae .end
    mov al, [w16_dtbuf + di]
    test word [w16_dtfmt], DT_SINGLELINE
    jnz .ch
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
.ch:
    test word [w16_dtfmt], DT_WORDBREAK
    jz .add
    cmp al, ' '
    jne .add
    mov bx, di
.add:
    call w16_dtw1
    add dx, ax
    test word [w16_dtfmt], DT_WORDBREAK
    jz .next
    cmp dx, cx
    jle .next
    cmp bx, 0xFFFF                  ; too wide: back to the last space, or on
    je .next                        ;   to the end of a word too long for it
    cmp bx, si
    je .next
    mov di, bx
    mov bp, bx                      ; the spaces after it are not drawn
.sp:
    inc bp
    cmp bp, [w16_dtn]
    jae .ret
    cmp byte [ds:w16_dtbuf + bp], ' '
    je .sp
    ret
.next:
    inc di
    jmp short .c
.cr:
    mov bp, di                      ; CR, CR LF
    inc bp
    cmp bp, [w16_dtn]
    jae .ret
    cmp byte [ds:w16_dtbuf + bp], 10
    jne .ret
    inc bp
    ret
.lf:
    mov bp, di
    inc bp
    ret
.end:
    mov bp, di
.ret:
    ret

; w16_dtemit - characters SI up to DI of w16_dtbuf: one line at [w16_dty],
; aligned, drawn when [w16_dtdraw]; [w16_dtw] = the widest so far. Keeps SI,
; DI, BP.
w16_dtemit:
    push si
    push di
    push bp
    mov cx, di
    sub cx, si
    cmp cx, W16_TXTMAX              ; TextOut's run is the most drawn
    jbe .n
    mov cx, W16_TXTMAX
.n:
    mov [w16_tn], cx
    push cx
    add si, w16_dtbuf
    mov di, w16_txt
    push ds
    pop es
    cld
    rep movsb
    pop cx
    mov si, w16_txt
    ICALL w16_textw                 ; AX = its width
    cmp ax, [w16_dtw]
    jbe .w
    mov [w16_dtw], ax
.w:
    mov dx, [w16_dtr]               ; x: left, centred or right
    test word [w16_dtfmt], DT_CENTER
    jz .notc
    mov dx, [w16_dtr + 4]
    sub dx, [w16_dtr]
    sub dx, ax
    sar dx, 1
    add dx, [w16_dtr]
    jmp short .x
.notc:
    test word [w16_dtfmt], DT_RIGHT
    jz .x
    mov dx, [w16_dtr + 4]
    sub dx, ax
.x:
    cmp byte [w16_dtdraw], 0
    je .out
    add dx, [w16_ox]
    mov [w16_tx], dx
    mov ax, [w16_dty]
    add ax, [w16_oy]
    mov [w16_ty], ax
    ICALL w16_txtdraw
.out:
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; RegisterWindowMessage (USER.118) - args: +0 lpString. C000h and up, from a
; case-blind hash of the name: the same name, the same number, for as long
; as the program runs (SPEC.md 124.20.4)
; -----------------------------------------------------------------------------
W16LIB w16u_regmsg
    push si
    ICALL w16_args
    les si, [es:bx]
    xor dx, dx
.c:
    mov al, [es:si]
    or al, al
    jz .done
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 'a' - 'A'
.up:
    mov cl, 5                       ; DX = rol(DX, 5) xor the character
    rol dx, cl
    xor dl, al
    inc si
    jmp short .c
.done:
    mov ax, dx
    and ax, 0x3FFF
    or ax, 0xC000
    xor dx, dx
    pop si
    ret

; -----------------------------------------------------------------------------
; AdjustWindowRect (USER.102) - args: +0 bMenu, +2 dwStyle, +6 lpRect: grown
; by the frame, the caption and the menu bar, Windows 3.x's NC_AdjustRect
; -----------------------------------------------------------------------------
W16LIB w16u_adjrect
    push si
    push di
    ICALL w16_args
    mov si, [es:bx + 4]             ; SI = the style's high word
    mov di, [es:bx]                 ; DI = bMenu
    les bx, [es:bx + 6]
    xor ax, ax                      ; AX/DX = the frame, x and y
    xor dx, dx
    mov cx, 32                      ; SM_CXFRAME, SM_CYFRAME: WS_THICKFRAME
    test si, 0x0004
    jnz .frame
    mov ax, si
    and ax, 0x00C0
    mov cx, 7                       ; SM_C?DLGFRAME: WS_DLGFRAME alone
    cmp ax, 0x0040
    je .frame
    mov cx, 5                       ; SM_C?BORDER: WS_BORDER
    test si, 0x0080
    jz .grow
.frame:
    push bx
    mov bx, cx
    ICALL w16_sm
    push ax
    mov bx, cx
    inc bx
    ICALL w16_sm
    mov dx, ax
    pop ax
    pop bx
.grow:
    sub [es:bx], ax
    sub [es:bx + 2], dx
    add [es:bx + 4], ax
    add [es:bx + 6], dx
    mov ax, si
    and ax, 0x00C0                  ; WS_CAPTION: the caption, less a border
    cmp ax, 0x00C0
    jne .nocap
    push bx
    mov bx, SM_CYCAPTION
    ICALL w16_sm
    mov cx, ax
    mov bx, 6                       ; SM_CYBORDER
    ICALL w16_sm
    sub cx, ax
    pop bx
    sub [es:bx + 2], cx
.nocap:
    or di, di
    jz .nomenu
    push bx
    mov bx, SM_CYMENU
    ICALL w16_sm
    mov cx, ax
    mov bx, 6
    ICALL w16_sm
    add cx, ax
    pop bx
    sub [es:bx + 2], cx
.nomenu:
    test si, 0x0020                 ; WS_VSCROLL
    jz .novs
    push bx
    mov bx, 2                       ; SM_CXVSCROLL
    ICALL w16_sm
    pop bx
    dec ax
    add [es:bx + 4], ax
.novs:
    test si, 0x0010                 ; WS_HSCROLL
    jz .out
    push bx
    mov bx, 3                       ; SM_CYHSCROLL
    ICALL w16_sm
    pop bx
    dec ax
    add [es:bx + 6], ax
.out:
    mov ax, 1
    pop di
    pop si
    ret

; SetActiveWindow (USER.59) - args: +0 hWnd: fronted as BringWindowToTop
; fronts it; AX = the top-level window that held the focus before
W16LIB w16u_setactive
    push si
    xor si, si                      ; SI = the active one, the focus's
    mov ax, [w16_focus]
    or ax, ax
    jz .front
    ICALL w16_hwnd2rec
    jc .front
    ICALL w16_toplevel
    ICALL w16_rec2hwnd
    mov si, ax
.front:
    ICALL w16_args
    mov ax, [es:bx]
    ICALL w16_hwnd2rec
    jc .out
    ICALL w16_frontwnd
.out:
    mov ax, si
    xor dx, dx
    pop si
    ret

; =============================================================================
; GDI
; =============================================================================

; LineDDA (GDI.100) - args: +0 lpData, +4 lpfn, +8 y2, +10 x2, +12 y1, +14 x1.
; Bresenham's, as Windows' (Wine's LineDDA16): every point from the first up
; to, not including, the last, each a call into the program (SPEC.md 124.2.2)
W16LIB w16g_linedda
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_ddat], ax
    mov ax, [es:bx + 2]
    mov [w16_ddat + 2], ax
    mov ax, [es:bx + 4]
    mov [w16_ddfn], ax
    mov ax, [es:bx + 6]
    mov [w16_ddfn + 2], ax
    mov ax, [es:bx + 14]
    mov [w16_ddx], ax
    mov ax, [es:bx + 12]
    mov [w16_ddy], ax
    mov ax, [es:bx + 10]            ; AX = |dx|, [w16_ddxa] its sign
    sub ax, [w16_ddx]
    mov word [w16_ddxa], 1
    jge .dxp
    neg ax
    neg word [w16_ddxa]
.dxp:
    mov dx, [es:bx + 8]             ; DX = |dy|
    sub dx, [w16_ddy]
    mov word [w16_ddya], 1
    jge .dyp
    neg dx
    neg word [w16_ddya]
.dyp:
    mov byte [w16_ddmaj], 1         ; more horizontal: x the major axis
    cmp ax, dx
    jg .maj
    mov byte [w16_ddmaj], 0
    xchg ax, dx
.maj:
    mov [w16_ddcnt], ax             ; AX = the major delta, DX = the minor
    shl dx, 1
    mov [w16_ddeb], dx              ; 2 minor
    sub dx, ax
    mov [w16_dderr], dx             ; 2 minor - major
    sub dx, ax
    mov [w16_ddea], dx              ; 2 minor - 2 major
    or ax, ax
    jz .none
    jmp short w16_ddcall
.none:
    xor ax, ax
    xor dx, dx
    ret

; w16_ddcall - the callback at the current point; it comes back to w16g_ddnext
w16_ddcall:
    push si
    mov si, w16g_ddnext_c
    xor ax, ax
    xor cx, cx
    ICALL w16_cbopen
    mov ax, [w16_ddx]               ; pascal: x, y, then lpData
    ICALL w16_ppush
    mov ax, [w16_ddy]
    ICALL w16_ppush
    mov ax, [w16_ddat + 2]
    ICALL w16_ppush
    mov ax, [w16_ddat]
    ICALL w16_ppush
    mov ax, [w16_ddfn]
    mov dx, [w16_ddfn + 2]
    ICALL w16_cbcall
    pop si
    ret

; w16g_ddnext - the callback answered: step, and the next point or the end
LCONT w16g_ddnext
    mov ax, [w16_ddya]              ; AX = the minor axis's step, BX/DX the
    mov dx, [w16_ddxa]              ;   major's
    mov bx, w16_ddx
    mov cx, w16_ddy
    cmp byte [w16_ddmaj], 0
    jne .h
    xchg ax, dx
    xchg bx, cx                     ; vertical: y the major
.h:
    cmp word [w16_dderr], 0
    jle .noadj
    push bx
    mov bx, cx
    add [bx], ax                    ; the minor axis moves
    pop bx
    mov ax, [w16_ddea]
    add [w16_dderr], ax
    jmp short .maj
.noadj:
    mov ax, [w16_ddeb]
    add [w16_dderr], ax
.maj:
    add [bx], dx
    dec word [w16_ddcnt]
    jnz w16_ddcall
    xor ax, ax
    xor dx, dx
    ret

; PtVisible (GDI.103) - args: +0 y, +2 x, +4 hDC: inside the DC's clip (a
; memory DC's: its bitmap)
W16LIB w16g_ptvisible
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx]
    mov ax, [es:bx + 4]
    cmp ax, W16_HDC
    jne .mem
    cmp byte [w16_dcon], 0
    je .no
    cmp byte [w16_dchide], 0
    jne .no
    add cx, [w16_ox]
    add dx, [w16_oy]
    cmp cx, [w16_cl]
    jl .no
    cmp cx, [w16_cr]
    jg .no
    cmp dx, [w16_ct]
    jl .no
    cmp dx, [w16_cb]
    jg .no
.yes:
    mov ax, 1
    ret
.mem:
    call w16_mdcbmp                 ; BX = its bitmap's slot * 2
    jc .no
    or cx, cx
    jl .no
    cmp cx, [w16b_w + bx]
    jge .no
    or dx, dx
    jl .no
    cmp dx, [w16b_h + bx]
    jl .yes
.no:
    xor ax, ax
    ret

; w16_mdcbmp - AX = a memory DC -> BX = its bitmap's slot * 2. CF=1: not
; one, or it holds the stock 1 x 1. Keeps all but BX.
w16_mdcbmp:
    push ax
    call w16_mdcslot
    jc .out
    shl bx, 1
    mov ax, [w16m_bmp + bx]
    cmp ax, W16_BMSTK
    stc
    je .out
    call w16_bmslot
.out:
    pop ax
    ret

; SetBrushOrg (GDI.148) - args: +0 y, +2 x, +4 hDC. Kept and answered: brushes
; stand on the screen's own grid (SPEC.md 124.20.4)
W16LIB w16g_setbrorg
    ICALL w16_args
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    xchg ax, [w16_brorg]
    xchg dx, [w16_brorg + 2]
    ret

; CreateCompatibleBitmap (GDI.51) - args: +0 nHeight, +2 nWidth, +4 hDC: a
; black bitmap of the display's format, as CreateBitmap makes one
W16LIB w16g_cmpbmp
    push bx
    ICALL w16_args
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    call w16_bmnew                  ; AX = the handle, 0 none
    pop bx
    ret

; GetDCOrg (GDI.79) - args: +0 hDC: the client's origin on the screen
W16LIB w16g_getdcorg
    ICALL w16_args
    xor ax, ax
    xor dx, dx
    cmp word [es:bx], W16_HDC
    jne .out
    cmp byte [w16_dcon], 0
    je .out
    mov ax, [w16_ox]
    mov dx, [w16_oy]
.out:
    ret

; GetPixel (GDI.83) - args: +0 y, +2 x, +4 hDC: the pixel's COLORREF in a
; memory DC's bitmap, or on the screen under a window's DC - read back with
; OSAPI_GFX_SAVE, a byte a plane; CLR_INVALID outside the bitmap or the clip,
; and on a VESA display, whose save is the driver's own (SPEC.md 124.20.4)
W16LIB w16g_getpixel
    push si
    push di
    push es
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx]
    mov ax, [es:bx + 4]
    cmp ax, W16_HDC
    je .screen
    call w16_mdcbmp
    jc .inv
    or cx, cx
    jl .inv
    cmp cx, [w16b_w + bx]
    jge .inv
    or dx, dx
    jl .inv
    cmp dx, [w16b_h + bx]
    jge .inv
    mov ax, [w16b_str + bx]         ; the nibble: row * stride + x / 2
    mul dx
    mov si, cx
    shr si, 1
    add si, ax
    mov es, [w16b_seg + bx]
    mov al, [es:si]
    test cl, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 15
.rgb:
    mov bl, 3                       ; ...as Windows' sixteen
    mul bl
    mov bx, ax
    mov ax, [w16_pal16 + bx]
    mov dl, [w16_pal16 + bx + 2]
    xor dh, dh
    jmp short .out
.inv:
    mov ax, 0xFFFF
    mov dx, ax
.out:
    pop es
    pop di
    pop si
    ret
.screen:                            ; the window's DC: open and not hidden
    cmp byte [w16_dcon], 0
    je .inv
    cmp byte [w16_dchide], 0
    jne .inv
    add cx, [w16_ox]
    add dx, [w16_oy]
    cmp cx, [w16_cl]                ; inside the clip, as Windows asked
    jl .inv
    cmp cx, [w16_cr]
    jg .inv
    cmp dx, [w16_ct]
    jl .inv
    cmp dx, [w16_cb]
    jg .inv
    push cx
    push dx
    mov bx, [w16_win]
    call OSAPI_WM_DISPLAY           ; DL = the kind, DH = the planes
    mov si, dx
    pop dx
    pop cx
    mov ax, si
    cmp al, VID_KIND_VESA
    je .inv
    mov ax, cx                      ; the one pixel's byte, a byte a plane
    mov bx, dx
    push ds
    pop es
    mov di, w16_gpix
    call OSAPI_GFX_SAVE
    jc .inv
    and cl, 7                       ; its bit: 7 is the byte's leftmost
    mov ah, 0x80
    shr ah, cl
    mov cx, si
    mov cl, ch                      ; CL = the planes
    xor ch, ch
    xor al, al
    mov si, w16_gpix
    add si, cx                      ; plane 3 first: its bit is the highest
.plane:
    dec si
    shl al, 1
    test [si], ah
    jz .p0
    or al, 1
.p0:
    loop .plane
    cmp byte [w16_mono], 0          ; one plane: lit is white
    je .rgb
    neg al
    and al, 15
    jmp .rgb

; w16_clipkids - BX = the DC's window: with WS_CLIPCHILDREN, each visible
; child that spans [w16_kl]..[w16_kb] edge to edge along one side is cut out
; of it - a status bar, a tool bar (SPEC.md 124.20.4). A child inside the
; rectangle is not: one rectangle cannot have a hole. [w16_ox]/[w16_oy] are
; the window's client origin. Keeps every register.
w16_clipkids:
    test word [bx + W16W_STYLE + 2], WS_CLIPCH_HI
    jz .ret
    push ax
    push cx
    push dx
    push si
    push di
    ICALL w16_rec2hwnd              ; AX = its handle
    mov di, ax
    mov si, w16_wnd
    mov cx, W16_MAXWND
.kid:
    mov al, [si + W16W_FLAGS]
    and al, W16WF_USED | W16WF_VIS | W16WF_DYING
    cmp al, W16WF_USED | W16WF_VIS
    jne .next
    test word [si + W16W_STYLE + 2], WS_CHILD_HI
    jz .next
    cmp [si + W16W_PARENT], di
    jne .next
    call w16_cutkid
.next:
    add si, W16W_SIZE
    loop .kid
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; w16_cutkid - w16_clipkids' for the child SI: its rectangle on the screen,
; inclusive, cut out of [w16_kl]..[w16_kb] when it spans a side
w16_cutkid:
    push bx
    mov ax, [si + W16W_X]           ; AX..CX x DX..BX: the child, screen
    add ax, [w16_ox]
    mov cx, ax
    add cx, [si + W16W_CX]
    dec cx
    mov dx, [si + W16W_Y]
    add dx, [w16_oy]
    mov bx, dx
    add bx, [si + W16W_CY]
    dec bx
    cmp ax, [w16_kl]                ; across the whole width?
    jg .cols
    cmp cx, [w16_kr]
    jl .cols
    cmp dx, [w16_kt]                ; ...along the top: below it is left
    jg .bot
    cmp bx, [w16_kt]
    jl .out
    inc bx
    mov [w16_kt], bx
    jmp short .out
.bot:
    cmp bx, [w16_kb]                ; ...along the bottom: above it
    jl .out
    cmp dx, [w16_kb]
    jg .out
    dec dx
    mov [w16_kb], dx
    jmp short .out
.cols:
    cmp dx, [w16_kt]                ; down the whole height?
    jg .out
    cmp bx, [w16_kb]
    jl .out
    cmp ax, [w16_kl]                ; ...down the left
    jg .right
    cmp cx, [w16_kl]
    jl .out
    inc cx
    mov [w16_kl], cx
    jmp short .out
.right:
    cmp cx, [w16_kr]                ; ...down the right
    jl .out
    cmp ax, [w16_kr]
    jg .out
    dec ax
    mov [w16_kr], ax
.out:
    pop bx
    ret

section .text
w16_rdinv:  dd 0x00550009           ; DSTINVERT, InvertRect's ROP
