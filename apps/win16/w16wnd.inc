; =============================================================================
; os8088 - apps/win16/w16wnd.inc
;
; THE WINDOW TREE (SPEC.md 124.8, S6). A child window is a rectangle of its
; parent's client that os8088 knows nothing about; a top-level window is an
; os8088 window, the program's first one WIN16's own and every other a second
; window of the package. Everything else in USER asks this file where a window
; is: its client's top left and size on the screen (w16_origin), the clip its
; DC gets (w16_wclip), what is under the pointer (w16_hittest), whether it may
; have input (w16_disabled). A window's text, the focus and enabling are here.
; =============================================================================

; w16_rec2hwnd - BX = a record -> AX = its HWND. Keeps the rest.
w16_rec2hwnd:
    push cx
    push dx
    mov ax, bx
    sub ax, w16_wnd
    xor dx, dx
    mov cx, W16W_SIZE
    div cx
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    pop dx
    pop cx
    ret

; w16_parent - BX = a record -> BX = its parent's. CF=1: it is a top-level
; window, and BX is kept. Keeps the rest.
w16_parent:
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .no
    push ax
    push cx
    push bx
    mov ax, [bx + W16W_PARENT]
    call w16_hwnd2rec
    jc .bad
    add sp, 2
    pop cx
    pop ax
    ret
.bad:
    pop bx
    pop cx
    pop ax
.no:
    stc
    ret

; w16_toplevel - BX = a record -> BX = its top-level window's. Keeps the rest.
w16_toplevel:
    call w16_parent
    jnc w16_toplevel
    clc
    ret

; w16_osw2rec - SI = one of our os8088 windows -> BX = the top-level window
; in it, AX = its HWND. CF=1: none (WIN16's own window before the program
; has one). Keeps the rest.
w16_osw2rec:
    push cx
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.r:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .n
    cmp [bx + W16W_OSW], si
    je .hit
.n:
    add bx, W16W_SIZE
    loop .r
    pop cx
    stc
    ret
.hit:
    call w16_rec2hwnd
    pop cx
    clc
    ret

; w16_bord - BX = a record -> AX = its border: 1 for a program's child with
; WS_BORDER, else 0 (a top-level window's frame is os8088's, and WIN16's
; controls draw their own inside their rectangle). Keeps the rest.
w16_bord:
    xor ax, ax
    cmp byte [bx + W16W_KIND], W16Q_PROG    ; WIN16's controls draw their own
    jne .out
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .out
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .out
    inc ax
.out:
    ret

; -----------------------------------------------------------------------------
; w16_origin - BX = a record -> AX/DX = its client's top left on the screen,
; CX/SI = the client's width and height (never below 0). CF=1: its os8088
; window is not visible. Keeps BX and DI.
; -----------------------------------------------------------------------------
w16_origin:
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .child
    push bx
    mov bx, [bx + W16W_OSW]
    call OSAPI_WM_GEOM              ; CX/DX = the content's size
    jc .hid
    mov si, dx
    push cx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    pop cx
    pop bx
    push ax
    mov ax, [bx + W16W_OSW]
    cmp ax, [w16_win]
    jne .nobar
    call w16_mbarh                  ; the client is under the menu bar
    add dx, ax
    sub si, ax
    jge .nobar
    xor si, si
.nobar:
    pop ax
    clc
    ret
.hid:
    pop bx
    stc
    ret
.child:
    push bx
    call w16_parent
    jc .orph
    call w16_origin                 ; the parent's client
    pop bx
    jc .out
    push di
    mov di, ax
    call w16_bord                   ; AX = the border
    add di, ax
    add di, [bx + W16W_X]
    add dx, ax
    add dx, [bx + W16W_Y]
    mov cx, [bx + W16W_CX]
    sub cx, ax
    sub cx, ax
    jge .w
    xor cx, cx
.w:
    mov si, [bx + W16W_CY]
    sub si, ax
    sub si, ax
    jge .h
    xor si, si
.h:
    mov ax, di
    pop di
    clc
.out:
    ret
.orph:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; w16_wclip - BX = a record -> [w16_kl]..[w16_kb] = its client cut by every
; ancestor's, on the screen and inclusive. CF=1: nothing of it shows. Keeps
; every register.
; -----------------------------------------------------------------------------
w16_wclip:
    push ax
    push bx
    push cx
    push dx
    push si
    call w16_origin
    jc .out
    mov [w16_kl], ax
    mov [w16_kt], dx
    add ax, cx
    dec ax
    mov [w16_kr], ax
    add dx, si
    dec dx
    mov [w16_kb], dx
.up:
    call w16_parent
    jc .done
    call w16_origin
    jc .out
    cmp ax, [w16_kl]
    jle .l
    mov [w16_kl], ax
.l:
    cmp dx, [w16_kt]
    jle .t
    mov [w16_kt], dx
.t:
    add ax, cx
    dec ax
    cmp ax, [w16_kr]
    jge .r
    mov [w16_kr], ax
.r:
    add dx, si
    dec dx
    cmp dx, [w16_kb]
    jge .up
    mov [w16_kb], dx
    jmp short .up
.done:
    mov ax, [w16_kl]
    cmp ax, [w16_kr]
    jg .none
    mov ax, [w16_kt]
    cmp ax, [w16_kb]
    jg .none
    clc
    jmp short .out
.none:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_hittest - BX = a top-level record, CX/DX = a screen point -> BX = the
; deepest visible child under it (or the window itself), AX = its HWND. A
; group box is transparent to the pointer, as Windows' answers HTTRANSPARENT,
; and of two siblings that both hold the point the smaller wins.
; Keeps CX, DX, SI, DI.
; -----------------------------------------------------------------------------
w16_hittest:
    push cx
    push dx
    push si
    push di
    mov [w16_hx], cx
    mov [w16_hy], dx
.level:
    call w16_origin                 ; the client's top left
    mov [w16_hox], ax
    mov [w16_hoy], dx
    call w16_rec2hwnd
    mov [w16_hpar], ax
    xor di, di                      ; DI = the best child so far
    mov si, w16_wnd
    mov cx, W16_MAXWND
.r:
    mov al, [si + W16W_FLAGS]
    and al, W16WF_USED | W16WF_VIS
    cmp al, W16WF_USED | W16WF_VIS
    jne .n
    test word [si + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    mov ax, [si + W16W_PARENT]
    cmp ax, [w16_hpar]
    jne .n
    cmp byte [si + W16W_KIND], W16Q_BUTTON
    jne .solid
    mov al, [si + W16W_STYLE]
    and al, 0x0F
    cmp al, 7                       ; BS_GROUPBOX
    je .n
.solid:
    mov ax, [w16_hx]
    sub ax, [w16_hox]
    sub ax, [si + W16W_X]
    jl .n
    cmp ax, [si + W16W_CX]
    jge .n
    mov ax, [w16_hy]
    sub ax, [w16_hoy]
    sub ax, [si + W16W_Y]
    jl .n
    cmp ax, [si + W16W_CY]
    jge .n
    or di, di
    jz .take
    mov ax, [si + W16W_CX]
    add ax, [si + W16W_CY]
    mov dx, [di + W16W_CX]
    add dx, [di + W16W_CY]
    cmp ax, dx
    jae .n
.take:
    mov di, si
.n:
    add si, W16W_SIZE
    loop .r
    or di, di
    jz .done
    mov bx, di
    jmp .level
.done:
    call w16_rec2hwnd
    pop di
    pop si
    pop dx
    pop cx
    ret

; w16_disabled - BX = a record: CF=1 when it, or a window it is inside, is
; disabled or invisible to input. Keeps every register.
w16_disabled:
    push bx
.up:
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jnz .yes
    call w16_parent
    jnc .up
    pop bx
    clc
    ret
.yes:
    pop bx
    stc
    ret

; w16_inside - BX = a record, AX = an HWND: CF=1 when the window AX is BX or
; one of its ancestors. Keeps every register.
w16_inside:
    push bx
    push cx
    push dx
    mov dx, ax
.up:
    call w16_rec2hwnd
    cmp ax, dx
    je .yes
    call w16_parent
    jnc .up
    mov ax, dx
    pop dx
    pop cx
    pop bx
    clc
    ret
.yes:
    mov ax, dx
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; w16_invaltree - BX = a window, AX/CX/DX/SI = left, top, right, bottom in its
; client (exclusive), [w16_erase]: invalidate it and every visible child it
; covers (SPEC.md 124.8: a parent's paint covers its children). Keeps every
; register.
; -----------------------------------------------------------------------------
w16_invaltree:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, sp                      ; [bp+12] left, [bp+8] top, [bp+6] right,
    call w16_inval                  ;   [bp+4] bottom
    call w16_rec2hwnd               ; AX = this window
    mov di, w16_wnd
.c:
    mov cl, [di + W16W_FLAGS]
    and cl, W16WF_USED | W16WF_VIS
    cmp cl, W16WF_USED | W16WF_VIS
    jne .n
    test word [di + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    cmp [di + W16W_PARENT], ax
    jne .n
    push ax
    mov bx, di                      ; the rectangle in the child's client
    call w16_bord
    mov [w16_ivb], ax
    mov ax, [bx + W16W_X]
    add ax, [w16_ivb]
    mov cx, [bx + W16W_Y]
    add cx, [w16_ivb]
    mov dx, [bp + 6]
    sub dx, ax
    mov si, [bp + 4]
    sub si, cx
    neg ax
    add ax, [bp + 12]
    neg cx
    add cx, [bp + 8]
    or ax, ax                       ; ...cut to that client
    jge .l
    xor ax, ax
.l:
    or cx, cx
    jge .t
    xor cx, cx
.t:
    push ax
    mov ax, [w16_ivb]
    shl ax, 1
    neg ax
    add ax, [bx + W16W_CX]
    cmp dx, ax
    jle .r
    mov dx, ax
.r:
    mov ax, [w16_ivb]
    shl ax, 1
    neg ax
    add ax, [bx + W16W_CY]
    cmp si, ax
    jle .b
    mov si, ax
.b:
    pop ax
    cmp ax, dx
    jge .skip
    cmp cx, si
    jge .skip
    call w16_invaltree
.skip:
    pop ax
.n:
    add di, W16W_SIZE
    cmp di, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .c
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; A window's text (SPEC.md 124.8): a NUL-terminated block in the USER heap
; -----------------------------------------------------------------------------

; w16_settext - BX = a record, DX:SI = a far string (DX = 0: none): its text
; becomes a copy of it, at most 255 characters. CF=1 no memory. Keeps every
; register.
w16_settext:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    push dx
    call w16_mheap                  ; ES = the USER heap
    pop dx
    jc .fail
    mov ax, [bx + W16W_TEXT]
    or ax, ax
    jz .none
    call w16_lfree
    mov word [bx + W16W_TEXT], 0
.none:
    or dx, dx
    jz .ok
    push ds                         ; the length, at most 255
    mov ds, dx
    xor cx, cx
.len:
    cmp byte [si], 0
    je .lend
    inc si
    inc cx
    cmp cx, 255
    jb .len
.lend:
    sub si, cx
    pop ds
    push cx
    inc cx
    mov al, 0
    call w16_lnew
    pop cx
    or ax, ax
    jz .fail
    mov [bx + W16W_TEXT], ax
    mov di, ax
    push ds
    mov ds, dx
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0
.ok:
    clc
    jmp short .out
.fail:
    stc
.out:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16_textptr - BX = a record -> ES:SI = its text (an empty string when it has
; none), CX = its length. Keeps the rest.
w16_textptr:
    mov si, [bx + W16W_TEXT]
    or si, si
    jz .empty
    cmp word [w16_mseg], 0
    je .empty
    mov es, [w16_mseg]
    push ax
    push di
    mov di, si
    mov cx, 0xFFFF
    xor al, al
    cld
    repne scasb
    not cx
    dec cx
    pop di
    pop ax
    ret
.empty:
    push ds
    pop es
    mov si, w16_s_empty
    xor cx, cx
    ret

; w16_gettext - BX = a record, DX:DI = a buffer of CX bytes -> AX = the
; characters copied (at most CX - 1, and a NUL). Keeps the rest.
w16_gettext:
    push cx
    push si
    push di
    push es
    push ds
    xor ax, ax
    jcxz .out
    dec cx
    push cx
    call w16_textptr                ; ES:SI, CX = its length
    pop ax
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    mov ax, cx
    push es
    pop ds
    mov es, dx
    cld
    rep movsb
    mov byte [es:di], 0
.out:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    ret

; =============================================================================
; The second top-level window (SPEC.md 124.8): an os8088 window of its own
; =============================================================================

; w16_oframe - the frame os8088 puts round a content box, read off WIN16's own
; window: [w16_fdx]/[w16_fdy] = what the frame adds to the content's size,
; [w16_fox]/[w16_foy] = the content's offset in the frame. Keeps every
; register.
w16_oframe:
    push ax
    push bx
    push cx
    push dx
    push es
    mov bx, [w16_win]
    call OSAPI_WM_GEOM
    jc .guess
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx + W_W]
    sub ax, cx
    mov [w16_fdx], ax
    mov ax, [es:bx + W_H]
    sub ax, dx
    mov [w16_fdy], ax
    call OSAPI_WM_CONTENT
    sub ax, [es:bx + W_X]
    mov [w16_fox], ax
    sub dx, [es:bx + W_Y]
    mov [w16_foy], dx
    jmp short .out
.guess:
    mov word [w16_fdx], 2
    mov word [w16_fdy], 14
    mov word [w16_fox], 1
    mov word [w16_foy], 13
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_newosw - BX = a top-level record whose client is [bx+W16W_CX] by
; [bx+W16W_CY] at screen [bx+W16W_X]/[bx+W16W_Y]: an os8088 window of its
; own for it, HIDDEN (w16_oswshow shows it), titled with its text. CF=1:
; os8088 had no window to give, or every title buffer is in use. Keeps BX.
; -----------------------------------------------------------------------------
w16_newosw:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    xor cx, cx                      ; a free title buffer
    mov di, w16_ttab
.t:
    cmp word [di], 0
    je .ttake
    add di, W16_TTLSZ
    inc cx
    cmp cx, W16_MAXTOP
    jb .t
    jmp .fail
.ttake:
    inc cx
    and byte [bx + W16W_F2], ~W16W2_TTL
    or [bx + W16W_F2], cl
    mov word [di], 1                ; taken, until the window is
    lea di, [di + 2]
    call w16_ttlcopy                ; its text, as the title
    mov [w16_otpl + 8], di
    call w16_oframe
    mov ax, [bx + W16W_X]           ; the frame, kept on the screen
    sub ax, [w16_fox]
    mov cx, [bx + W16W_CX]
    add cx, [w16_fdx]
    mov dx, [w16_scrw]
    sub dx, cx
    cmp ax, dx
    jle .xr
    mov ax, dx
.xr:
    or ax, ax
    jge .xl
    xor ax, ax
.xl:
    mov [w16_otpl], ax
    mov [w16_otpl + 4], cx
    mov ax, [bx + W16W_Y]
    sub ax, [w16_foy]
    mov cx, [bx + W16W_CY]
    add cx, [w16_fdy]
    mov dx, [w16_scrh]
    sub dx, cx
    cmp ax, dx
    jle .yb
    mov ax, dx
.yb:
    cmp ax, 12                      ; under the menu bar
    jge .yt
    mov ax, 12
.yt:
    mov [w16_otpl + 2], ax
    mov [w16_otpl + 6], cx
    mov word [w16_otpl + 10], w16_paint
    mov word [w16_otpl + 12], w16_onkey
    mov word [w16_otpl + 14], w16_onclick
    push bx
    mov si, w16_otpl
    call OSAPI_WM_CREATE            ; BX = the os8088 window, hidden
    mov ax, bx
    pop bx
    jc .ffree
    mov [bx + W16W_OSW], ax
    mov si, [w16_otpl + 8]          ; the title buffer names its window
    mov [si - 2], ax
    push bx
    mov bx, ax
    mov ax, w16_onclose
    call OSAPI_WM_ONCLOSE
    mov ax, w16_onup
    call OSAPI_WM_ONMOUSEUP
    mov ax, w16_ondrag
    call OSAPI_WM_ONDRAG
    mov ax, w16_onrclick
    call OSAPI_WM_ONRCLICK
    mov al, 1                       ; W_PAINT paints every pixel
    call OSAPI_WM_OWNBG
    call OSAPI_WM_NOANIM            ; a dialog does not zoom open (SPEC.md
    pop bx                          ;   11.99.2.1)
    clc
    jmp short .out
.ffree:
    call w16_ttlfree
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16_oswshow - BX = a top-level record with an os8088 window of its own:
; show it in front, its content then fitted to the client it asked for (the
; frame is measured, not assumed). Keeps every register.
w16_oswshow:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [bx + W16W_CX]
    mov dx, [bx + W16W_CY]
    push ax
    push dx
    call w16_lock
    mov bx, [bx + W16W_OSW]
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    pop si                          ; SI = the height asked for
    pop ax                          ; AX = the width
    call OSAPI_WM_GEOM
    jc .shown
    sub ax, cx
    sub si, dx
    mov cx, ax
    or cx, si
    jz .shown
    mov cx, KERNEL_SEG
    mov es, cx
    mov cx, [es:bx + W_W]
    add cx, ax
    mov dx, [es:bx + W_H]
    add dx, si
    call OSAPI_WM_RESIZE
.shown:
    call w16_unlock
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_ttlfree - BX = a record: give back its title buffer. Keeps every
; register.
w16_ttlfree:
    push cx
    push si
    mov cl, [bx + W16W_F2]
    and cx, W16W2_TTL
    jcxz .out
    and byte [bx + W16W_F2], ~W16W2_TTL
    dec cx
    mov si, w16_ttab
    jcxz .z
.n:
    add si, W16_TTLSZ
    loop .n
.z:
    mov word [si], 0
.out:
    pop si
    pop cx
    ret

; w16_ttlcopy - BX = a record, DI = a title buffer in our segment: its text
; there, at most W16_TTLSZ - 3 characters, NUL-terminated. Keeps every
; register.
w16_ttlcopy:
    push ax
    push cx
    push si
    push di
    push es
    call w16_textptr                ; ES:SI, CX = its length
    cmp cx, W16_TTLSZ - 3
    jbe .c
    mov cx, W16_TTLSZ - 3
.c:
    jcxz .z
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
.z:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16_ttlset - BX = a top-level record with a title buffer: copy its text in
; and tell os8088. Keeps every register.
w16_ttlset:
    push ax
    push cx
    push di
    mov cl, [bx + W16W_F2]
    and cx, W16W2_TTL
    jcxz .out
    dec cx
    mov di, w16_ttab
    jcxz .z
.n:
    add di, W16_TTLSZ
    loop .n
.z:
    lea di, [di + 2]
    call w16_ttlcopy
    mov ax, di
    push bx
    mov bx, [bx + W16W_OSW]
    call OSAPI_WM_TITLE
    pop bx
.out:
    pop di
    pop cx
    pop ax
    ret

; w16_killwins - destroy every second os8088 window the program still has,
; under the lock: the program has ended, and these are not bound to the
; instance (SPEC.md 124.8). Keeps every register.
w16_killwins:
    push ax
    push bx
    push cx
    call w16_lock
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    mov ax, [bx + W16W_OSW]
    or ax, ax
    jz .n
    cmp ax, [w16_win]
    je .n
    push bx
    mov word [bx + W16W_OSW], 0
    call w16_ttlfree
    mov bx, ax
    call OSAPI_WM_DESTROY
    pop bx
.n:
    add bx, W16W_SIZE
    loop .w
    call w16_unlock
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Making a window (SPEC.md 124.8): CreateWindow's and the dialog part's
; =============================================================================

; -----------------------------------------------------------------------------
; w16_mkwnd - a window from [w16_cw*]: its kind, class, procedure, brush,
; style, place and size, parent (an owner for a top-level), ID and text. A
; child's place is in its parent's client; a top-level's [w16_cwx]..[cwcy]
; are its CLIENT on the screen, except the first, which becomes WIN16's own
; window and keeps its place. Nothing is sent. Out BX = the record, CX = the
; slot, AX = the HWND. CF=1 refused: no slot, no parent, the dialog part
; not here for one of WIN16's classes, no os8088 window for a second
; top-level. Keeps SI, DI.
; -----------------------------------------------------------------------------
w16_mkwnd:
    push si
    push di
    cmp byte [w16_cwkind], W16Q_PROG
    je .part
    call w16_dlgin                  ; WIN16's classes are the dialog part's
    jc .fail
.part:
    xor cx, cx                      ; a free slot
.slot:
    call w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .take
    inc cx
    cmp cx, W16_MAXWND
    jb .slot
.fail:
    stc
    jmp .out
.take:
    mov si, bx
    push cx
    push es
    push ds
    pop es
    mov di, bx
    mov cx, W16W_SIZE / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop cx
    mov byte [si + W16W_FLAGS], W16WF_USED | W16WF_INVAL | W16WF_ERASE
    mov word [si + W16W_UR], 0x7FFF ; the whole client, erased
    mov word [si + W16W_UB], 0x7FFF
    mov al, [w16_cwkind]
    mov [si + W16W_KIND], al
    mov al, [w16_cwcls]
    mov [si + W16W_CLS], al
    mov ax, [w16_cwproc]
    mov [si + W16W_PROC], ax
    mov ax, [w16_cwproc + 2]
    mov [si + W16W_PROC + 2], ax
    mov ax, [w16_cwhbr]
    mov [si + W16W_HBR], ax
    mov ax, [w16_cwstyle]
    mov [si + W16W_STYLE], ax
    mov ax, [w16_cwstyle + 2]
    mov [si + W16W_STYLE + 2], ax
    mov ax, [w16_cwid]
    mov [si + W16W_MENU], ax
    mov ax, [w16_cwpar]
    mov [si + W16W_PARENT], ax
    mov ax, [w16_cwx]
    mov [si + W16W_X], ax
    mov ax, [w16_cwy]
    mov [si + W16W_Y], ax
    mov ax, [w16_cwcx]
    mov [si + W16W_CX], ax
    mov ax, [w16_cwcy]
    mov [si + W16W_CY], ax
    inc word [w16_seq]
    mov ax, [w16_seq]
    mov [si + W16W_SEQ], ax
    push si                         ; its text
    mov bx, si
    mov si, [w16_cwtext]
    mov dx, [w16_cwtext + 2]
    call w16_settext
    pop si
    test word [si + W16W_STYLE + 2], WS_CHILD_HI
    jz .top
    push cx                         ; --- a child: its parent must be a window
    mov ax, [si + W16W_PARENT]
    call w16_hwnd2rec
    pop cx
    jc .undo
    mov ax, CW_USEDEFAULT           ; CW_USEDEFAULT is nothing for a child
    xor dx, dx
    cmp [si + W16W_X], ax
    jne .x
    mov [si + W16W_X], dx
.x:
    cmp [si + W16W_Y], ax
    jne .y
    mov [si + W16W_Y], dx
.y:
    cmp [si + W16W_CX], ax
    jne .w
    mov [si + W16W_CX], dx
.w:
    cmp [si + W16W_CY], ax
    jne .h
    mov [si + W16W_CY], dx
.h:
    test word [si + W16W_STYLE + 2], WS_VISIBLE_HI
    jz .done
    or byte [si + W16W_FLAGS], W16WF_VIS
    jmp short .done
.top:
    cmp byte [w16_top], 0
    jne .second
    mov ax, [w16_win]               ; --- the first: WIN16's own window
    mov [si + W16W_OSW], ax
    mov al, cl
    inc al
    mov [w16_top], al
    push cx
    mov bx, si
    mov di, w16_wtitle              ; its text is the title
    call w16_ttlcopy
    mov word [w16_titleptr], w16_wtitle
    call w16_lock
    mov bx, [w16_win]
    mov al, 1                       ; W_PAINT paints every pixel (SPEC.md 124.8)
    call OSAPI_WM_OWNBG
    mov ax, w16_wtitle
    call OSAPI_WM_TITLE
    call w16_unlock
    pop cx
    jmp short .done
.second:
    mov bx, si                      ; --- a second: an os8088 window of its own
    call w16_newosw
    jc .undo
.done:
    mov bx, si
    call w16_rec2hwnd
    clc
.out:
    pop di
    pop si
    ret
.undo:
    mov bx, si
    xor dx, dx
    call w16_settext                ; its text goes back
    mov byte [bx + W16W_FLAGS], 0
    stc
    jmp short .out

; w16_builtin - [w16_namebuf] = a class name -> AL = the kind of WIN16's
; class of that name. CF=1 it is not one. Keeps the rest.
w16_builtin:
    push cx
    push si
    push di
    mov si, w16_bitab
    mov al, W16Q_BUTTON
.c:
    push si
    mov di, w16_namebuf
    mov cx, 8
.cmp:
    mov ah, [si]
    cmp ah, [di]
    jne .ne
    inc si
    inc di
    loop .cmp
    pop si
    clc
    jmp short .out
.ne:
    pop si
    add si, 8
    inc al
    cmp al, W16Q_LIST
    jbe .c
    stc
.out:
    pop di
    pop si
    pop cx
    ret

w16_bitab:                      ; in W16Q_BUTTON.. order, NUL-padded to 8
    db 'BUTTON', 0, 0
    db 'EDIT', 0, 0, 0, 0
    db 'STATIC', 0, 0
    db 'LISTBOX', 0

; w16_ctlstub - DX:AX = the window procedure of WIN16's controls: the stub
; of an export row no program can import (w16_ctlrow), so the program can
; read it, keep it and CallWindowProc it like any other. Keeps the rest.
w16_ctlstub:
    mov ax, w16_stubs + 6 * W16_CTLROW
    mov dx, cs
    ret

; w16_ddpstub - DX:AX = DefDlgProc's own stub, a dialog window's procedure
w16_ddpstub:
    mov ax, w16_stubs + 6 * W16_DDPROW
    mov dx, cs
    ret

; w16_ctlrow - a control's procedure called through its stub: args as
; w16_smargs. Its answer is the control's (w16_intproc).
w16_ctlrow:
    call w16_args
    call w16_smargs
    call w16_hwnd2rec
    jc .no
    jmp w16_intproc
.no:
    xor ax, ax
    xor dx, dx
    ret

; w16_csize - BX = a record -> CX/DX = its client's width and height, from
; the record (and WIN16's own window's from os8088), never below 0. Keeps
; the rest.
w16_csize:
    push ax
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .child
    mov ax, [bx + W16W_OSW]
    cmp ax, [w16_win]
    jne .rec
    push bx
    mov bx, ax
    call OSAPI_WM_GEOM
    pop bx
    jnc .bar
    xor cx, cx
    xor dx, dx
.bar:
    call w16_nobar
    jmp short .out
.rec:
    mov cx, [bx + W16W_CX]
    mov dx, [bx + W16W_CY]
    jmp short .pos
.child:
    call w16_bord
    shl ax, 1
    mov cx, [bx + W16W_CX]
    sub cx, ax
    mov dx, [bx + W16W_CY]
    sub dx, ax
.pos:
    or cx, cx
    jge .p1
    xor cx, cx
.p1:
    or dx, dx
    jge .out
    xor dx, dx
.out:
    pop ax
    ret

; w16_uncover - BX = a child: its window rectangle in its parent's client is
; invalid and erased, as when it is hidden, moved or destroyed. Keeps every
; register.
w16_uncover:
    push ax
    push bx
    push cx
    push dx
    push si
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .out
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .out
    mov ax, [bx + W16W_X]
    mov cx, [bx + W16W_Y]
    mov dx, ax
    add dx, [bx + W16W_CX]
    mov si, cx
    add si, [bx + W16W_CY]
    call w16_parent
    jc .out
    mov byte [w16_erase], 1
    call w16_invaltree
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_hidewnd - BX = a window: not visible; a top-level with an os8088
; window of its own takes it down. Keeps every register.
w16_hidewnd:
    call w16_uncover
    and byte [bx + W16W_FLAGS], ~W16WF_VIS
    call w16_ownosw
    jc .out
    push bx
    call w16_lock
    mov bx, [bx + W16W_OSW]
    call OSAPI_WM_HIDE
    call w16_unlock
    pop bx
.out:
    ret

; w16_showwnd - BX = a window: visible, all of it invalid, and a top-level's
; os8088 window shown in front at the client it asked for. Keeps every
; register.
w16_showwnd:
    push ax
    push cx
    push dx
    push si
    or byte [bx + W16W_FLAGS], W16WF_VIS
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    call w16_invaltree
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .out
    cmp word [bx + W16W_OSW], 0
    je .out
    call w16_oswshow
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16_frontwnd - BX = a window: its top-level's os8088 window in front.
; Keeps every register.
w16_frontwnd:
    push bx
    call w16_toplevel
    mov bx, [bx + W16W_OSW]
    or bx, bx
    jz .out
    call w16_lock
    call OSAPI_WM_FRONT
    call w16_unlock
.out:
    pop bx
    ret

; w16_ttlfrom - BX = a top-level window, AX = another's HWND: BX's os8088
; title is that window's text (a dialog without WS_CAPTION shows its
; owner's, SPEC.md 124.13). Keeps every register.
w16_ttlfrom:
    push ax
    push bx
    push cx
    push di
    mov di, bx
    call w16_hwnd2rec               ; BX = the owner
    jc .out
    mov cx, [di + W16W_OSW]
    jcxz .out
    cmp cx, [w16_win]
    jne .own
    push di
    mov di, w16_wtitle
    call w16_ttlcopy
    pop di
    mov ax, w16_wtitle
    jmp short .tell
.own:
    mov cl, [di + W16W_F2]
    and cx, W16W2_TTL
    jcxz .out
    dec cx
    mov ax, W16_TTLSZ
    mul cx
    add ax, w16_ttab + 2
    push di
    mov di, ax
    call w16_ttlcopy
    pop di
.tell:
    call w16_lock
    mov bx, [di + W16W_OSW]
    call OSAPI_WM_TITLE
    call w16_unlock
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; w16_freetree - BX = a window: it and every window inside it or owned by it
; freed with no message to any (a dialog whose template failed half-way).
; WIN16's controls are told WM_DESTROY, as they hold heap blocks. Keeps
; every register.
w16_freetree:
    push ax
    push bx
    push cx
    push dx
    push di
    or byte [bx + W16W_FLAGS], W16WF_DYING
.mark:
    xor dl, dl
    mov di, w16_wnd
.m:
    mov al, [di + W16W_FLAGS]
    and al, W16WF_USED | W16WF_DYING
    cmp al, W16WF_USED
    jne .mn
    mov ax, [di + W16W_PARENT]
    call w16_hwnd2rec
    jc .mn
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jz .mn
    or byte [di + W16W_FLAGS], W16WF_DYING
    inc dl
.mn:
    add di, W16W_SIZE
    cmp di, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .m
    or dl, dl
    jnz .mark
    mov bx, w16_wnd
.f:
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_DYING
    cmp al, W16WF_USED | W16WF_DYING
    jne .fn
    call w16_isctl
    jc .free
    call w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_DESTROY
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_intproc
.free:
    call w16_wndfree
.fn:
    add bx, W16W_SIZE
    cmp bx, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .f
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The calls
; =============================================================================

; GetParent (USER.46) - args: +0 hWnd. A child's parent, or a popup's owner.
w16u_getparent:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    mov ax, 0
    jc .out
    mov ax, [bx + W16W_PARENT]
.out:
    ret

; IsWindow (USER.47) - args: +0 hWnd
w16u_iswindow:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    mov ax, 0
    jc .out
    inc ax
.out:
    ret

; IsChild (USER.48) - args: +0 hWnd, +2 hWndParent: is hWnd a descendant?
w16u_ischild:
    call w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    call w16_hwnd2rec
    jc .no
    call w16_parent
    jc .no
    mov ax, dx
    call w16_inside
    jnc .no
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; IsWindowVisible (USER.49) - args: +0 hWnd: it and every ancestor shown
w16u_isvisible:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    jc .no
.up:
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .no
    call w16_parent
    jnc .up
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; IsWindowEnabled (USER.35) - args: +0 hWnd
w16u_isenabled:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    jc .no
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jnz .no
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; EnableWindow (USER.34) - args: +0 bEnable, +2 hWnd. Answers whether it was
; disabled before. A control repaints (its WM_ENABLE); a window that loses
; the focus it had loses it.
w16u_enable:
    call w16_args
    mov dx, [es:bx]
    mov ax, [es:bx + 2]
    ; fall into w16_enable

; w16_enable - AX = an HWND, DX = 0 disable / else enable -> AX = whether it
; was disabled. Clobbers BX, CX, DX.
w16_enable:
    W16SMKEEP
.go:
    call w16_hwnd2rec
    jc .no
    xor cx, cx
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jz .was
    inc cx
.was:
    or dx, dx
    jz .dis
    and byte [bx + W16W_FLAGS], ~W16WF_DIS
    jmp short .tell
.dis:
    or byte [bx + W16W_FLAGS], W16WF_DIS
    push cx                         ; the focus inside it goes
    push bx
    call w16_rec2hwnd
    mov dx, ax                      ; DX = this window
    mov ax, [w16_focus]
    or ax, ax
    jz .nf
    call w16_hwnd2rec               ; BX = the focus's record
    jc .nf
    mov ax, dx
    call w16_inside                 ; CF=1: this window is it, or holds it
    jnc .nf
    xor ax, ax
    call w16_setfocus
.nf:
    pop bx
    pop cx
.tell:
    cmp byte [bx + W16W_KIND], W16Q_PROG
    je .out
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    je .out
    mov word [w16_sm_msg], WM_ENABLE  ; a control redraws itself greyed or not
    xor ax, ax
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jnz .e
    inc ax
.e:
    mov [w16_sm_wp], ax
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    push cx
    call w16_intproc
    pop cx
.out:
    mov ax, cx
    ret
.no:
    xor ax, ax
    ret

; GetFocus (USER.23)
w16u_getfocus:
    mov ax, [w16_focus]
    ret

; SetFocus (USER.22) - args: +0 hWnd. Answers the window that had it.
w16u_setfocus:
    call w16_args
    mov ax, [es:bx]
    ; fall into w16_setfocus

; -----------------------------------------------------------------------------
; w16_setfocus - AX = an HWND (0 = none) -> AX = the one that had the focus.
; WM_KILLFOCUS and WM_SETFOCUS go to WIN16's own controls (SPEC.md 124.8), at
; once; a program's window is not told yet. A dialog keeps the control that
; had its focus. Clobbers BX, CX, DX.
; -----------------------------------------------------------------------------
w16_setfocus:
    W16SMKEEP                       ; the message being handled survives the
.go:                                ;   two it sends
    push si
    mov dx, ax
    or ax, ax
    jz .ok
    call w16_hwnd2rec
    jc .keep
    call w16_disabled
    jc .keep
.ok:
    mov si, [w16_focus]
    cmp si, dx
    je .same
    mov [w16_focus], dx
    mov ax, si                      ; the old one loses it
    or ax, ax
    jz .new
    call w16_hwnd2rec
    jc .new
    mov word [w16_sm_msg], WM_KILLFOCUS
    mov [w16_sm_wp], dx
    call w16_tellfocus
.new:
    mov ax, dx
    or ax, ax
    jz .done
    call w16_hwnd2rec
    jc .done
    mov word [w16_sm_msg], WM_SETFOCUS
    mov [w16_sm_wp], si
    call w16_tellfocus
    push bx                         ; ...and its dialog keeps it
.dlg:
    call w16_parent
    jc .nodlg
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    jne .dlg
    mov [bx + WD_FOCUS], dx
.nodlg:
    pop bx
.done:
    mov ax, si
    pop si
    ret
.same:
    mov ax, si
    pop si
    ret
.keep:
    mov ax, [w16_focus]
    pop si
    ret

; w16_tellfocus - BX = a window, [w16_sm_msg]/[w16_sm_wp] set: tell it, if it
; is one of WIN16's controls. Keeps every register.
w16_tellfocus:
    cmp byte [bx + W16W_KIND], W16Q_PROG
    je .out
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    je .out
    push ax
    push cx
    push dx
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_intproc
    pop dx
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; w16_intproc - BX = one of WIN16's controls, [w16_sm_*] the message (the
; HWND is filled in here): run its procedure in the dialog part, at once -
; it never calls the program (SPEC.md 124.13). DX:AX = its answer, 0 when
; the part is not here. Keeps BX, SI, DI.
; -----------------------------------------------------------------------------
w16_intproc:
    push bx
    call w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    xor ax, ax
    xor dx, dx
    cmp word [w16_dseg], 0
    je .out
    DCALL w16d_ctlproc
.out:
    pop bx
    ret

; GetWindowText (USER.36) - args: +0 nMaxCount, +2 lpString, +6 hWnd
w16u_getwtext:
    push di
    call w16_args
    mov cx, [es:bx]
    mov di, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov ax, [es:bx + 6]
    call w16_hwnd2rec
    jc .no
    call w16_gettext
    pop di
    ret
.no:
    xor ax, ax
    pop di
    ret

; GetWindowTextLength (USER.38) - args: +0 hWnd
w16u_getwtlen:
    push si
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    mov ax, 0
    jc .out
    call w16_textptr
    mov ax, cx
.out:
    pop si
    ret

; SetWindowText (USER.37) - args: +0 lpString, +4 hWnd. A top-level window's
; is its os8088 title too (WIN16's own window's through w16_wtitle); one of
; WIN16's controls is SENT WM_SETTEXT, as Windows does it, and redraws.
w16u_setwtext:
    push si
    call w16_args
    mov si, [es:bx]
    mov dx, [es:bx + 2]
    mov ax, [es:bx + 4]
    call w16_hwnd2rec
    jc .no
    call w16_wsettext
    mov ax, 1
    pop si
    ret
.no:
    xor ax, ax
    pop si
    ret

; -----------------------------------------------------------------------------
; w16_wsettext - BX = a window, DX:SI = its new text (DX = 0 none): store it
; and show it. Keeps every register.
; -----------------------------------------------------------------------------
w16_wsettext:
    push ax
    push cx
    push dx
    push si
    push di
    cmp byte [bx + W16W_KIND], W16Q_PROG
    je .store
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    je .store
    mov word [w16_sm_msg], WM_SETTEXT   ; a control: its own procedure
    mov word [w16_sm_wp], 0
    mov [w16_sm_lp], si
    mov [w16_sm_lp + 2], dx
    call w16_intproc
    jmp short .out
.store:
    call w16_settext
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .inv
    mov ax, [bx + W16W_OSW]
    or ax, ax
    jz .out
    cmp ax, [w16_win]
    jne .other
    mov di, w16_wtitle              ; WIN16's own window: w16_wtitle
    call w16_ttlcopy
    push bx
    call w16_lock
    mov bx, [w16_win]
    mov ax, w16_wtitle
    call OSAPI_WM_TITLE
    call w16_unlock
    pop bx
    jmp short .out
.other:
    call w16_lock
    call w16_ttlset
    call w16_unlock
    jmp short .out
.inv:
    xor ax, ax                      ; a program's child: invalid
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    call w16_invaltree
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; GetWindowRect (USER.32) - args: +0 lpRect, +4 hWnd: on the screen
w16u_getwrect:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 4]
    call w16_hwnd2rec
    jc .zero
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .child
    push es
    push bx
    mov bx, [bx + W16W_OSW]
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx + W_X]
    mov cx, [es:bx + W_Y]
    mov dx, [es:bx + W_W]
    mov si, [es:bx + W_H]
    pop bx
    pop es
    add dx, ax
    add si, cx
    jmp short .put
.child:
    call w16_origin                 ; its client, then out by its border
    push ax
    call w16_bord
    mov cx, ax
    pop ax
    sub ax, cx
    sub dx, cx
    mov cx, dx                      ; AX/CX = its window's top left
    mov dx, ax
    add dx, [bx + W16W_CX]
    mov si, cx
    add si, [bx + W16W_CY]
.put:
    push ax
    push bx
    call w16_args
    les di, [es:bx]
    pop bx
    pop ax
    mov [es:di], ax
    mov [es:di + 2], cx
    mov [es:di + 4], dx
    mov [es:di + 6], si
    mov ax, 1
    pop di
    pop si
    ret
.zero:
    xor ax, ax
    pop di
    pop si
    ret

; ScreenToClient (USER.29) - args: +0 lpPoint, +4 hWnd
w16u_s2c:
    mov cl, 1
    jmp short w16_ptconv

; ClientToScreen (USER.28) - args: +0 lpPoint, +4 hWnd
w16u_c2s:
    xor cl, cl

; w16_ptconv - CL = 1 screen -> client, 0 client -> screen
w16_ptconv:
    push si
    push di
    mov [w16_hx], cl
    call w16_args
    mov ax, [es:bx + 4]
    les di, [es:bx]
    push es
    call w16_hwnd2rec
    jc .out0
    call w16_origin
.out0:
    pop es
    jc .out
    cmp byte [w16_hx], 0
    je .to
    neg ax
    neg dx
.to:
    add [es:di], ax
    add [es:di + 2], dx
.out:
    pop di
    pop si
    ret

; GetDlgCtrlID (USER.277) - args: +0 hWnd
w16u_ctrlid:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    mov ax, 0
    jc .out
    mov ax, [bx + W16W_MENU]
.out:
    ret

; GetWindowWord (USER.133) - args: +0 nIndex, +2 hWnd. The extra bytes, and
; GWW_ID, GWW_HWNDPARENT and GWW_HINSTANCE.
GWW_HINSTANCE   equ -6
GWW_HWNDPARENT  equ -8
GWW_ID          equ -12
w16u_getwword:
    call w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 2]
    call w16_hwnd2rec
    jc .zero
    call w16_wwaddr
    jc .zero
    mov ax, [bx]
    ret
.zero:
    xor ax, ax
    ret

; SetWindowWord (USER.134) - args: +0 wNewWord, +2 nIndex, +4 hWnd
w16u_setwword:
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    cmp cx, GWW_HINSTANCE           ; read-only here: it is WIN16's own word
    je .zero
    mov ax, [es:bx + 4]
    call w16_hwnd2rec
    jc .zero
    call w16_wwaddr
    jc .zero
    mov ax, [bx]
    mov [bx], dx
    ret
.zero:
    xor ax, ax
    ret

; GetWindowLong (USER.135) - args: +0 nIndex, +2 hWnd. The extra bytes,
; GWL_WNDPROC and GWL_STYLE.
GWL_WNDPROC equ -4
GWL_STYLE   equ -16
w16u_getwlong:
    call w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 2]
    call w16_hwnd2rec
    jc .zero
    call w16_wladdr
    jc .zero
    mov ax, [bx]
    mov dx, [bx + 2]
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret

; SetWindowLong (USER.136) - args: +0 dwNewLong, +4 nIndex, +6 hWnd
w16u_setwlong:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 6]
    mov cx, [es:bx + 4]
    mov si, [es:bx]
    mov di, [es:bx + 2]
    call w16_hwnd2rec
    jc .zero
    call w16_wladdr
    jc .zero
    mov ax, [bx]
    mov dx, [bx + 2]
    mov [bx], si
    mov [bx + 2], di
    jmp short .out
.zero:
    xor ax, ax
    xor dx, dx
.out:
    pop di
    pop si
    ret

; w16_wwaddr - BX = a record, CX = a window-word index -> BX = the word's
; address in it. CF=1 not one. Keeps the rest.
w16_wwaddr:
    cmp cx, GWW_ID
    jne .np
    add bx, W16W_MENU
    clc
    ret
.np:
    cmp cx, GWW_HWNDPARENT
    jne .ni
    add bx, W16W_PARENT
    clc
    ret
.ni:
    cmp cx, GWW_HINSTANCE
    jne .x
    mov bx, w16_dgroup
    clc
    ret
.x:
    cmp cx, 16 - 2
    ja .no
    add bx, W16W_EXTRA
    add bx, cx
    clc
    ret
.no:
    stc
    ret

; w16_wladdr - as w16_wwaddr for a window LONG
w16_wladdr:
    cmp cx, GWL_WNDPROC
    jne .ns
    add bx, W16W_PROC
    clc
    ret
.ns:
    cmp cx, GWL_STYLE
    jne .x
    add bx, W16W_STYLE
    clc
    ret
.x:
    cmp cx, 16 - 4
    ja .no
    add bx, W16W_EXTRA
    add bx, cx
    clc
    ret
.no:
    stc
    ret

; MessageBeep (USER.104) - args: +0 wType. One short tone on the speaker.
w16u_beep:
    mov ax, 880
    mov cx, 2
    mov dl, 0x40
    call OSAPI_SND_TONE
    xor ax, ax
    ret
