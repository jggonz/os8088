; =============================================================================
; os8088 - apps/win16/w16sys.inc
;
; WHAT THE CONTROL PANEL CALLS (SPEC.md 124.20.5): the directories, the
; module and library calls and the system settings a Control Panel reads and
; writes. All of it is the library part's (SPEC.md 124.19.3).
; =============================================================================

section .w16dlg

; GetWindowsDirectory (KERNEL.134), GetSystemDirectory (KERNEL.135) - the
; folder WIN.INI is in (SPEC.md 124.14) as a DOS path: "A:\SYSTEM\APPDATA\
; WIN16" on WIN16's own volume. One folder answers both: WIN16 has no SYSTEM
; folder of its own. args: +0 nSize, +2 lpBuffer. Answers the length; a
; buffer too small for it answers the size it needs, NUL included, as Windows
; did, and is left alone
W16LIB w16k_windir
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    les di, [es:bx + 2]
    mov ax, w16s_dirlen + 1
    cmp cx, ax
    jb .out
    mov al, [w16_homev]
    add al, 'A'
    cld
    stosb
    mov si, w16s_dir
    mov cx, w16s_dirlen
    rep movsb                       ; the NUL with it
    mov ax, w16s_dirlen
.out:
    pop di
    pop si
    ret

; OpenFile (KERNEL.74) and _lclose (KERNEL.81) are files' (w16file.inc,
; SPEC.md 124.21.3).

; LoadLibrary (KERNEL.95) - no DLL loads yet (S18): 2, file not found, as a
; library that is not on the path answered. FreeLibrary (KERNEL.96) has
; nothing to free
W16LIB w16k_loadlib
    mov ax, 2
    ret
W16LIB w16k_freelib
    ret

; The network (USER.512-520): there is none. WNetGetCaps answers 0, no
; capabilities, and every other WNet call WN_NOT_SUPPORTED (1), as Windows
; answered without a network driver
W16LIB w16u_wnetcaps
    xor ax, ax
    ret
W16LIB w16u_wnetno
    mov ax, 1
    ret

; OemToAnsi (KEYBOARD.6) - args: +0 lpAnsiStr, +4 lpOemStr. WIN16 does not
; translate between the OEM and ANSI sets (SPEC.md 124.20.5), so the string
; is copied as it is; the same buffer twice is legal
W16LIB w16kb_oem2ansi
    push si
    push di
    push ds
    ICALL w16_args
    les di, [es:bx]
    lds si, [es:bx + 4]
    cld
.c:
    lodsb
    stosb
    or al, al
    jnz .c
    pop ds
    pop di
    pop si
    mov ax, -1
    ret

; EqualRect (USER.244) - args: +0 lprc2, +4 lprc1
W16LIB w16u_eqrect
    push si
    push di
    push ds
    ICALL w16_args
    lds si, [es:bx + 4]
    les di, [es:bx]
    mov cx, 4
    cld
    repe cmpsw
    pop ds
    pop di
    pop si
    mov ax, 0
    jne .out
    inc ax
.out:
    ret

; -----------------------------------------------------------------------------
; Calendar's (SPEC.md 124.22.10)
; -----------------------------------------------------------------------------

; GetUpdateRect (USER.190) - args: +0 bErase, +2 lpRect, +6 hWnd. The
; window's invalid rectangle in its client, cut to the client, and TRUE; an
; empty one and FALSE when nothing is invalid. bErase is not acted on: the
; erase is BeginPaint's, as it was going to be.
W16LIB w16u_getupdrect
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    push es
    push bx
    ICALL w16_hwnd2rec
    pop di
    pop es
    mov si, 0                       ; SI = the answer
    mov ax, 0                       ; AX..DX, CX: the rectangle
    mov cx, ax
    mov dx, ax
    push ax
    jc .put
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jz .put
    push es
    push di
    ICALL w16_csize                 ; CX/DX = the client
    pop di
    pop es
    mov si, cx
    mov ax, [bx + W16W_UR]
    cmp ax, si
    jle .r
    mov ax, si
.r:
    mov cx, ax
    mov si, dx
    mov ax, [bx + W16W_UB]
    cmp ax, si
    jle .b
    mov ax, si
.b:
    mov dx, ax
    pop ax
    mov ax, [bx + W16W_UT]          ; the top and left, not above the client
    or ax, ax
    jge .t
    xor ax, ax
.t:
    push ax
    mov ax, [bx + W16W_UL]
    or ax, ax
    jge .l
    xor ax, ax
.l:
    mov si, 1
    cmp ax, cx
    jl .w
    xor si, si
.w:
    pop bx
    push bx
    cmp bx, dx
    jl .put
    xor si, si
.put:
    pop bx                          ; BX = the top
    les di, [es:di + 2]
    mov [es:di], ax
    mov [es:di + 2], bx
    mov [es:di + 4], cx
    mov [es:di + 6], dx
    mov ax, si
    pop di
    pop si
    ret

; GetActiveWindow (USER.60) - the top-level window the focus is in, else the
; program's first window
W16LIB w16u_getactive
    push bx
    mov ax, [w16_focus]
    ICALL w16_hwnd2rec
    jnc .top
    xor ax, ax
    mov bl, [w16_top]
    or bl, bl
    jz .out
    dec bl
    xor bh, bh
    mov cx, bx
    ICALL w16_slot2rec
.top:
    ICALL w16_toplevel
    ICALL w16_rec2hwnd
.out:
    pop bx
    ret

; AnyPopup (USER.52) - TRUE while a visible top-level window has an owner: a
; dialog or a program's popup
W16LIB w16u_anypopup
    push bx
    push cx
    mov bx, w16_wnd
    mov cx, W16_MAXWND
    xor ax, ax
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_VIS | W16WF_DYING
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .n
    cmp word [bx + W16W_PARENT], 0
    je .n
    inc ax
    jmp short .out
.n:
    add bx, W16W_SIZE
    loop .w
.out:
    pop cx
    pop bx
    ret

; FlashWindow (USER.105) - args: +0 bInvert, +2 hWnd. The caption does not
; flash - os8088 draws it - and the answer is that it was not inverted.
; HiliteMenuItem (USER.162) - args: +0 wHilite, +2 wIDHiliteItem, +4 hMenu,
; +6 hWnd. The bar is the menu loop's to highlight; TRUE.
W16LIB w16u_hilite
    mov ax, 1
    ret

; --- Cardfile's (SPEC.md 124.22.11) ---------------------------------------------

; CreateIC (GDI.153) - args as CreateDC's: the display's information context,
; which is the desktop's DC that GetDeviceCaps and GetTextMetrics answer for
W16LIB w16g_creic
    mov ax, W16_HDESKDC
    ret

; IntersectClipRect (GDI.22) - args: +0 bottom, +2 right, +4 top, +6 left,
; +8 hDC. The open DC's clip cut by the rectangle (client, its right and
; bottom outside it), until the DC is released or RestoreDC puts it back.
; SIMPLEREGION (2), or NULLREGION (1) when nothing is left; another DC's is
; kept nowhere and answers SIMPLEREGION.
W16LIB w16g_intclip
    ICALL w16_args
    mov ax, 2
    cmp word [es:bx + 8], W16_HDC
    jne .out
    cmp byte [w16_dcon], 0
    je .out
    mov ax, [es:bx + 6]
    add ax, [w16_ox]
    cmp ax, [w16_cl]
    jle .l
    mov [w16_cl], ax
.l:
    mov ax, [es:bx + 4]
    add ax, [w16_oy]
    cmp ax, [w16_ct]
    jle .t
    mov [w16_ct], ax
.t:
    mov ax, [es:bx + 2]
    add ax, [w16_ox]
    dec ax
    cmp ax, [w16_cr]
    jge .r
    mov [w16_cr], ax
.r:
    mov ax, [es:bx]
    add ax, [w16_oy]
    dec ax
    cmp ax, [w16_cb]
    jge .b
    mov [w16_cb], ax
.b:
    mov ax, 2
    mov dx, [w16_cl]
    cmp dx, [w16_cr]
    jg .null
    mov dx, [w16_ct]
    cmp dx, [w16_cb]
    jle .out
.null:
    dec ax
.out:
    xor dx, dx
    ret

; SaveDC (GDI.30) - args: +0 hDC. The open DC's whole state - its clip, its
; objects, its colours and modes - kept: the level to give RestoreDC, 1 for
; the first. W16_SVMAX deep; 0 past it. Another DC's answers 1 and keeps
; nothing.
W16LIB w16g_savedc
    ICALL w16_args
    mov ax, 1
    cmp word [es:bx], W16_HDC
    jne .out
    cmp byte [w16_dcon], 0
    je .out
    xor ax, ax
    mov al, [w16_svn]
    cmp al, W16_SVMAX
    jae .no
    push si
    push di
    push cx
    mov cl, W16_DCSZ
    mul cl
    add ax, w16_svdc
    mov di, ax
    mov si, w16_dchide
    mov cx, W16_DCSZ
    push ds
    pop es
    cld
    rep movsb
    pop cx
    pop di
    pop si
    inc byte [w16_svn]
    mov al, [w16_svn]
    xor ah, ah
.out:
    xor dx, dx
    ret
.no:
    xor ax, ax
    jmp short .out

; RestoreDC (GDI.39) - args: +0 nSavedDC, +2 hDC. The state SaveDC kept at
; that level put back, it and every one after it forgotten; -1 the last,
; -2 the one before. TRUE, FALSE for a level never saved.
W16LIB w16g_restdc
    ICALL w16_args
    mov ax, 1
    cmp word [es:bx + 2], W16_HDC
    jne .out
    mov ax, [es:bx]
    or ax, ax
    jg .lev
    jz .no
    mov cl, [w16_svn]               ; relative: -1 = the last saved
    xor ch, ch
    add ax, cx
    inc ax
.lev:
    cmp byte [w16_dcon], 0
    je .no
    mov cl, [w16_svn]
    xor ch, ch
    cmp ax, cx
    ja .no
    or ax, ax
    jle .no
    dec ax
    mov [w16_svn], al
    push si
    push di
    mov cl, W16_DCSZ
    mul cl
    add ax, w16_svdc
    mov si, ax
    mov di, w16_dchide
    mov cx, W16_DCSZ
    push ds
    pop es
    cld
    rep movsb
    pop di
    pop si
    mov ax, 1
.out:
    xor dx, dx
    ret
.no:
    xor ax, ax
    jmp short .out

; GetBitmapBits (GDI.74) - args: +0 lpBits, +4 dwCount, +8 hBitmap. The
; bitmap's rows, top first, as GetObject describes them: a bitmap made 1
; plane 1 bit a pixel as Windows' monochrome bits (a pixel bright - grey
; and up - is 1, each row a whole number of words), any other as its 4bpp
; rows, each padded to a word. Answers the bytes written, at most dwCount;
; 0 for a handle that is not a bitmap.
W16LIB w16g_getbmbits
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx + 8]
    mov cx, [es:bx + 4]             ; at most 64KB of it: one segment
    cmp word [es:bx + 6], 0
    je .cnt
    mov cx, 0xFFFF
.cnt:
    les di, [es:bx]                 ; ES:DI = the program's buffer
    call w16_bmslot                 ; BX = its slot * 2
    jc .none
    mov [w16_scratch], cx           ; bytes left to give
    mov bp, di                      ; BP = where they began
    mov dx, [w16b_h + bx]
    mov [w16_scratch + 2], dx       ; rows to go
    xor si, si                      ; SI = the row in the bitmap
    cmp byte [w16b_mono + bx], 0
    jne .mrow
.crow:                              ; 4bpp: each row its bytes, then to even
    mov cx, [w16b_str + bx]
    push si
.cb:
    call .room
    jc .cend
    push ds
    mov ds, [w16b_seg + bx]
    lodsb
    pop ds
    stosb
    loop .cb
.cend:
    pop si
    jc .done
    add si, [w16b_str + bx]
    test byte [w16b_str + bx], 1
    jz .cnext
    call .room
    jc .done
    xor al, al
    stosb
.cnext:
    dec word [w16_scratch + 2]
    jnz .crow
    jmp short .done
.mrow:                              ; 1bpp: eight pixels a byte, a word's worth
    mov cx, [w16b_w + bx]           ;   a 16
    add cx, 15
    shr cx, 1
    shr cx, 1
    shr cx, 1
    and cl, 0xFE                    ; CX = the row's bytes
    xor dx, dx                      ; DX = the pixel
.mb:
    call .room
    jc .done
    push cx
    mov cx, 8
    xor ah, ah
.mp:
    shl ah, 1
    cmp dx, [w16b_w + bx]
    jae .mz
    push ds
    push si
    push dx
    shr dx, 1
    add si, dx
    mov ds, [w16b_seg + bx]
    mov al, [si]
    pop dx
    pop si
    pop ds
    test dl, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 0x0F
    cmp al, 7                       ; light grey, or a bright colour: 1
    jb .mz
    or ah, 1
.mz:
    inc dx
    loop .mp
    mov al, ah
    stosb
    pop cx
    loop .mb
    add si, [w16b_str + bx]
    dec word [w16_scratch + 2]
    jnz .mrow
.done:
    mov ax, di
    sub ax, bp
    jmp short .out
.none:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret
; .room - one more byte may be written: CF=1 dwCount is reached
.room:
    cmp word [w16_scratch], 0
    je .full
    dec word [w16_scratch]
    clc
    ret
.full:
    stc
    ret

; Catch (KERNEL.55) - args: +0 lpCatchBuf. Where the gate's frame is on the
; program's stack, and its six words (DI, SI, DS, BP and the return), into
; the buffer's first seven words; 0. Throw comes back here.
W16LIB w16k_catch
    push si
    push di
    ICALL w16_args
    les di, [es:bx]
    mov si, [w16_psp_]
    mov ax, si
    cld
    stosw
    mov cx, 6
    push ds
    mov ds, [w16_pss]
    rep movsw
    pop ds
    pop di
    pop si
    xor ax, ax
    xor dx, dx
    ret

; Throw (KERNEL.56) - args: +0 nThrowBack, +2 lpCatchBuf. The frame Catch
; kept put back where it was, and the call returns from that Catch, with
; its four argument bytes, answering nThrowBack. Everything the program had
; pushed since - its own frames and any call WIN16 was making into it, whose
; continuation is on that stack (SPEC.md 124.2.2) - is left behind with it.
W16LIB w16k_throw
    push si
    push di
    ICALL w16_args
    mov dx, [es:bx]
    mov cx, [w16_pss]
    les si, [es:bx + 2]
    mov di, [es:si]
    mov [w16_psp_], di
    mov word [w16_nargb], 4
    push ds
    push es
    pop ds
    mov es, cx
    add si, 2
    mov cx, 6
    cld
    rep movsw
    pop ds
    pop di
    pop si
    mov ax, dx
    xor dx, dx
    ret

; OpenComm (USER.200) - there is no serial port a Windows program may have:
; IE_HARDWARE (-10). The other Comm calls answer -1, GetCommError 0.
W16LIB w16u_opencomm
    mov ax, -10
    cwd
    ret
W16LIB w16_ansm1
    mov ax, -1
    cwd
    ret

section .text

w16s_dir:    db ':\SYSTEM\APPDATA\WIN16', 0
w16s_dirlen  equ $ - w16s_dir
