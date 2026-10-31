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

; MapVirtualKey (KEYBOARD.131) - args: +0 wMapType, +2 wCode. 0: a virtual
; key's scan code, 1: a scan code's virtual key, 2: a virtual key's character
; unshifted - all of the US layout WIN16's keys are read in (w16_vktab). 0
; for what has none (SPEC.md 124.22.12)
W16LIB w16kb_mapvk
    ICALL w16_args
    mov ax, [es:bx + 2]
    mov cx, [es:bx]
    xor bx, bx
    cmp cx, 1
    je .s2v
    ja .v2c
    or ah, ah                       ; 0: the first scan code with that key
    jnz .zero
    or al, al
    jz .zero
.f:
    cmp al, [w16_vktab + bx]
    je .scan
    inc bx
    cmp bx, W16_NSCAN
    jb .f
    jmp short .zero
.scan:
    mov ax, bx
    jmp short .out
.s2v:
    cmp ax, W16_NSCAN
    jae .zero
    mov bx, ax
    mov al, [w16_vktab + bx]
    xor ah, ah
    jmp short .out
.v2c:
    cmp cx, 2
    jne .zero
    or ah, ah
    jnz .zero
    cmp al, '0'                     ; the digits and the letters are their
    jb .ctl                         ;   own characters
    cmp al, '9'
    jbe .out
    cmp al, 'A'
    jb .zero
    cmp al, 'Z'
    jbe .out
    mov bx, w16kb_vkch  ; XSEC-OK: read through cs:
.t:
    mov cx, [cs:bx]
    or cl, cl
    jz .zero
    cmp al, cl
    je .ch
    inc bx
    inc bx
    jmp short .t
.ch:
    mov al, ch
    jmp short .out
.ctl:
    cmp al, 0x08                    ; Backspace, Tab, Enter, Esc and space
    je .out
    cmp al, 0x09
    je .out
    cmp al, 0x0D
    je .out
    cmp al, 0x1B
    je .out
    cmp al, 0x20
    je .out
.zero:
    xor ax, ax
.out:
    xor dx, dx
    ret
w16kb_vkch:                         ; the other keys': virtual key, character
    db 0x6A, '*', 0x6B, '+', 0x6D, '-', 0xBA, ';', 0xBB, '=', 0xBC, ','
    db 0xBD, '-', 0xBE, '.', 0xBF, '/', 0xC0, '`', 0xDB, '[', 0xDC, '\'
    db 0xDD, ']', 0xDE, "'", 0

; GetWindow (USER.262) - args: +0 uCmd, +2 hWnd. GW_HWNDFIRST (0), LAST,
; NEXT, PREV: its siblings in Z order, the order they were made (w16d_sibx)
; - a top-level's are the other top-levels, the desktop's children; GW_OWNER
; (4) a top-level's owner; GW_CHILD (5) its first child, and the desktop's
; is the first top-level (SPEC.md 124.22.12)
W16LIB w16u_getwindow
    push si
    ICALL w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 2]
    cmp cx, 5
    je .child
    cmp ax, W16_HDESK
    je .zero
    ICALL w16_hwnd2rec
    jc .zero
    cmp cx, 4
    je .owner
    ja .zero
    mov dx, W16_HDESK               ; DX = whose children they are
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .k
    mov dx, [bx + W16W_PARENT]
.k:
    mov si, [bx + W16W_SEQ]
    mov al, 1
    cmp cx, 2
    je .sib
    dec ax
    cmp cx, 3
    je .sib
    xor si, si                      ; the first: the next after none
    inc ax
    jcxz .sib
    dec si                          ; the last: the one before all
    dec ax
.sib:
    call w16d_sibx
    jc .zero
    ICALL w16_rec2hwnd
    jmp short .out
.owner:
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .zero
    mov ax, [bx + W16W_PARENT]
    jmp short .out
.child:
    mov dx, ax
    xor si, si
    mov al, 1
    jmp short .sib
.zero:
    xor ax, ax
.out:
    xor dx, dx
    pop si
    ret

; WindowFromDC (USER.117) - args: +0 hDC: the window the open DC is on, the
; desktop for the desktop's DC. Windows 3.0's Task Manager calls ordinal 117
; with an HWND and hands the answer to IsWinOldApTask as a task: an HWND
; answers the instance, which stands for the task here (SPEC.md 124.22.12)
W16LIB w16u_wndfromdc
    ICALL w16_args
    mov ax, [es:bx]
    cmp ax, W16_HDC
    jne .desk
    cmp byte [w16_dcon], 0
    je .zero
    mov bx, [w16_dcwnd]
    ICALL w16_rec2hwnd
    jmp short .out
.desk:
    cmp ax, W16_HDESKDC
    jne .wnd
    mov ax, W16_HDESK
    jmp short .out
.wnd:
    ICALL w16_hwnd2rec
    jc .zero
    mov ax, [w16_dgroup]
    jmp short .out
.zero:
    xor ax, ax
.out:
    xor dx, dx
    ret

; SwitchToThisWindow (USER.172) - args: +0 fRestore, +2 hWnd: brought to the
; front, as BringWindowToTop. os8088 does not minimize, so there is nothing
; to restore
W16LIB w16u_switchto
    ICALL w16_args
    mov ax, [es:bx + 2]
    ICALL w16_hwnd2rec
    jc .out
    ICALL w16_frontwnd
.out:
    xor ax, ax
    ret

; =============================================================================
; The mapping calls and the palette (SPEC.md 124.22.13). Drawing is MM_TEXT:
; a mode and the extents are kept in the DC and answered back, and the
; viewport's origin moves logical 0, 0 - which in MM_TEXT is all a viewport
; does, and is what the Clipboard viewer scrolls with
; =============================================================================

MM_TEXT equ 1

; SetMapMode (GDI.3) - args: +0 nMapMode, +2 hDC: the old mode
W16LIB w16g_setmapmode
    ICALL w16_args
    mov ax, MM_TEXT
    call w16g_ours
    jc .out
    mov ax, [es:bx]
    xchg ax, [w16_mm]
.out:
    xor dx, dx
    ret

; w16g_ours - ES:BX = the args, the hDC at +2: CF=0 it is the open DC.
; Keeps every register
w16g_ours:
    cmp word [es:bx + 2], W16_HDC
    jne .no
    cmp byte [w16_dcon], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; SetWindowExt (GDI.12), SetViewportExt (GDI.14) - args: +0 y, +2 x, +4 hDC:
; the old extents, DX:AX = y:x
W16LIB w16g_setwext
    push si
    mov si, w16_wext
    jmp short w16g_ext
W16LIB w16g_setvext
    push si
    mov si, w16_vext
w16g_ext:
    mov cx, 1
    call w16g_pair
    pop si
    ret

; w16g_pair - SI = a pair in the DC's state, CX = its value on any other DC:
; set from the args (+0 y, +2 x, +4 hDC); DX:AX = the old y:x. CF=1 not the
; open DC, nothing set
w16g_pair:
    ICALL w16_args
    add bx, 2
    call w16g_ours
    lahf
    sub bx, 2
    sahf
    jc .other
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    xchg ax, [si]
    xchg dx, [si + 2]
    clc
    ret
.other:
    mov ax, cx
    mov dx, cx
    stc
    ret

; SetViewportOrg (GDI.13) - args: +0 y, +2 x, +4 hDC: logical 0, 0 is at
; that client point, and the old one is the answer. The DC's client origin
; moves by the difference: every call adds it
W16LIB w16g_setvorg
    push si
    mov si, w16_vorg
    xor cx, cx
    call w16g_pair
    jc .out
    mov cx, [w16_vorg]
    sub cx, ax
    add [w16_ox], cx
    mov cx, [w16_vorg + 2]
    sub cx, dx
    add [w16_oy], cx
.out:
    pop si
    ret

; RectVisibleOld (GDI.104) - args: +0 lpRect, +4 hDC: TRUE when any of the
; rectangle (logical, right and bottom outside it) is inside the clip. Any
; other DC's answers TRUE
W16LIB w16g_rectvis
    ICALL w16_args
    mov ax, 1
    add bx, 2
    call w16g_ours
    lahf
    sub bx, 2
    sahf
    jc .out
    les bx, [es:bx]
    mov cx, [es:bx]
    add cx, [w16_ox]
    cmp cx, [w16_cr]
    jg .no
    mov cx, [es:bx + 4]
    add cx, [w16_ox]
    dec cx
    cmp cx, [w16_cl]
    jl .no
    mov cx, [es:bx + 2]
    add cx, [w16_oy]
    cmp cx, [w16_cb]
    jg .no
    mov cx, [es:bx + 6]
    add cx, [w16_oy]
    dec cx
    cmp cx, [w16_ct]
    jge .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    ret

; CreatePalette (GDI.360), SelectPalette (USER.282): the stock
; DEFAULT_PALETTE. A display here has the colours it has: a palette is
; taken, selected and realized, and changes nothing
W16LIB w16g_crepal
    mov ax, W16_STOCK + 15
    xor dx, dx
    ret

; =============================================================================
; Character Map's calls (SPEC.md 124.22.15)
; =============================================================================

; MulDiv (GDI.128) - args: +0 nDenominator, +2 nNumber, +4 nMultiplier:
; their product over the denominator, rounded half away from 0 in 32 bits;
; 32767 or -32768 by the sign when it does not fit, or the denominator is 0
W16LIB w16g_muldiv
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 4]
    imul word [es:bx + 2]           ; DX:AX = the product
    mov si, dx                      ; SI's sign: the answer's
    xor si, cx
    or dx, dx
    jns .p
    not ax
    not dx
    add ax, 1
    adc dx, 0
.p:
    jcxz .ovf
    or cx, cx
    jns .c
    neg cx
.c:
    mov di, cx                      ; half the denominator, to round
    shr di, 1
    add ax, di
    adc dx, 0
    cmp dx, cx
    jae .ovf
    div cx
    or si, si
    js .neg
    cmp ax, 32767
    ja .ovf
    jmp short .out
.neg:
    cmp ax, 32768
    ja .ovf
    neg ax
    jmp short .out
.ovf:
    mov ax, 32767
    or si, si
    jns .out
    inc ax                          ; -32768
.out:
    xor dx, dx
    pop di
    pop si
    ret

; GetCharWidth (GDI.350) - args: +0 lpBuffer, +4 wLastChar, +6 wFirstChar,
; +8 hDC: each character's advance in the System font, one word each
W16LIB w16g_charwidth
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx + 4]
    mov dx, [es:bx + 6]
    les di, [es:bx]
    sub cx, dx
    jb .no
    inc cx
    cmp cx, 256
    ja .no
    mov si, [w16_face]
    cld
.c:
    push es
    mov es, si
    mov al, dl                      ; w16_glyph's lookup, here
    sub al, [es:W16F_FIRST]
    jb .def
    cmp al, [es:W16F_COUNT]
    jb .ok
.def:
    mov al, W16F_DEFCH
    sub al, [es:W16F_FIRST]
.ok:
    xor ah, ah
    mov bx, ax
    mov al, [es:W16F_ADV + bx]
    pop es
    stosw
    inc dx
    loop .c
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; GetMessagePos (USER.119): where the pointer was for the last message
; handed over, DX:AX = y:x on the screen
W16LIB w16u_msgpos
    mov ax, [w16_msg + MSG_PT]
    mov dx, [w16_msg + MSG_PT + 2]
    ret

; GetCursorPos (USER.17) - args: +0 lpPoint: where the pointer is now
W16LIB w16u_getcurpos
    ICALL w16_args
    les bx, [es:bx]
    mov ax, [w16_mx]
    mov [es:bx], ax
    mov ax, [w16_my]
    mov [es:bx + 2], ax
    mov ax, 1
    xor dx, dx
    ret

; WindowFromPoint (USER.30) - args: +0 x, +2 y (a POINT by value): the
; deepest visible window there among this program's - of two top-levels
; that both hold it, the one made last, which is in front - else 0
W16LIB w16u_wndfrompt
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_odrc], ax              ; (a scratch pair here)
    mov ax, [es:bx + 2]
    mov [w16_odrc + 2], ax
    xor bp, bp                      ; BP = the top-level found, DI its SEQ
    xor di, di
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.w:
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_VIS | W16WF_DYING
    cmp al, W16WF_USED | W16WF_VIS
    jne .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .n
    cmp word [bx + W16W_OSW], 0
    je .n
    push cx
    ICALL w16_origin                ; AX/DX = its client, CX/SI its size
    jc .n0
    neg ax
    add ax, [w16_odrc]
    js .n0
    cmp ax, cx
    jge .n0
    neg dx
    add dx, [w16_odrc + 2]
    js .n0
    cmp dx, si
    jge .n0
    mov ax, [bx + W16W_SEQ]
    cmp ax, di
    jb .n0
    mov di, ax
    mov bp, bx
.n0:
    pop cx
.n:
    add bx, W16W_SIZE
    loop .w
    xor ax, ax
    or bp, bp
    jz .out
    mov bx, bp
    mov cx, [w16_odrc]
    mov dx, [w16_odrc + 2]
    ICALL w16_hittest               ; AX = the HWND
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; DrawFocusRect (USER.466) - args: +0 lprc, +4 hDC: the rectangle's four
; edges inverted, so a second call takes it off - solid, where Windows'
; was dotted: os8088 has no dotted XOR line (SPEC.md 124.22.15)
W16LIB w16u_drawfocus
    push si
    push di
    ICALL w16_args
    cmp word [es:bx + 4], W16_HDC
    jne .out
    cmp byte [w16_dcon], 0
    je .out
    cmp byte [w16_dchide], 0
    jne .out
    les si, [es:bx]
    mov ax, [es:si]                 ; l, t, r, b on the screen, inclusive
    add ax, [w16_ox]
    mov bx, [es:si + 2]
    add bx, [w16_oy]
    mov cx, [es:si + 4]
    add cx, [w16_ox]
    dec cx
    mov dx, [es:si + 6]
    add dx, [w16_oy]
    dec dx
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    push dx                         ; the top
    mov dx, bx
    call w16u_xorcut
    pop dx
    cmp bx, dx
    je .out
    push bx                         ; the bottom
    mov bx, dx
    call w16u_xorcut
    pop bx
    inc bx                          ; the sides, between them
    dec dx
    cmp bx, dx
    jg .out
    push cx
    mov cx, ax
    call w16u_xorcut
    pop cx
    cmp ax, cx
    je .out
    mov ax, cx
    call w16u_xorcut
.out:
    mov ax, 1
    xor dx, dx
    pop di
    pop si
    ret

; w16u_xorcut - AX..CX x BX..DX (screen, inclusive) inverted, cut to the
; DC's clip. Keeps every register.
w16u_xorcut:
    push ax
    push bx
    push cx
    push dx
    cmp ax, [w16_cl]
    jge .l
    mov ax, [w16_cl]
.l:
    cmp bx, [w16_ct]
    jge .t
    mov bx, [w16_ct]
.t:
    cmp cx, [w16_cr]
    jle .r
    mov cx, [w16_cr]
.r:
    cmp dx, [w16_cb]
    jle .b
    mov dx, [w16_cb]
.b:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; VkKeyScan (KEYBOARD.129) - args: +0 cChar: the key that types it, and the
; shift state - AL the virtual key, AH 1 Shift, 2 Ctrl; -1 none (US layout)
W16LIB w16kb_vkscan
    ICALL w16_args
    mov al, [es:bx]
    xor ah, ah
    cmp al, '0'
    jb .ctl
    cmp al, '9'
    jbe .out
    cmp al, 'A'
    jb .punct
    cmp al, 'Z'
    jbe .shift
    cmp al, 'a'
    jb .punct
    cmp al, 'z'
    ja .punct
    sub al, 'a' - 'A'
    jmp short .out
.shift:
    mov ah, 1
    jmp short .out
.ctl:
    cmp al, 0x08                    ; Backspace, Tab, Enter, Esc and space
    je .out
    cmp al, 0x09
    je .out
    cmp al, 0x0D
    je .out
    cmp al, 0x1B
    je .out
    cmp al, 0x20
    je .out
    cmp al, 1                       ; Ctrl and a letter
    jb .none
    cmp al, 26
    ja .punct
    add al, 'A' - 1
    mov ah, 2
    jmp short .out
.punct:
    mov bx, w16kb_vkch  ; XSEC-OK: read through cs:
.t:
    mov cx, [cs:bx]
    or cl, cl
    jz .sh
    cmp al, ch
    je .vk
    inc bx
    inc bx
    jmp short .t
.vk:
    mov al, cl
    jmp short .out
.sh:
    mov bx, w16kb_vksh  ; XSEC-OK: read through cs:
.s:
    mov cx, [cs:bx]
    or cl, cl
    jz .none
    cmp al, ch
    je .vks
    inc bx
    inc bx
    jmp short .s
.vks:
    mov al, cl
    mov ah, 1
    jmp short .out
.none:
    mov ax, -1
.out:
    xor dx, dx
    ret
w16kb_vksh:                         ; the shifted ones': virtual key, character
    db '0', ')', '1', '!', '2', '@', '3', '#', '4', '$', '5', '%'
    db '6', '^', '7', '&', '8', '*', '9', '(', 0xBA, ':', 0xBB, '+'
    db 0xBC, '<', 0xBD, '_', 0xBE, '>', 0xBF, '?', 0xC0, '~', 0xDB, '{'
    db 0xDC, '|', 0xDD, '}', 0xDE, '"', 0

; EnumFontFamilies (GDI.330) - args: +0 lParam, +4 lpEnumFontFamProc,
; +8 lpszFamily, +12 hDC: the program's procedure called once for each
; family - WIN16 has one, the display's System font, a raster font - with a
; LOGFONT and a NEWTEXTMETRIC on its stack. A family that is not System has
; no faces: nothing is called, and the answer is 1 (SPEC.md 124.22.15)
LF_SIZE  equ 50
NTM_SIZE equ 42
W16LIB w16g_enumfam
    push si
    push di
    ICALL w16_args
    les si, [es:bx + 8]             ; a family named: System's only
    mov ax, es
    or ax, si
    jz .all
    mov di, w16g_sysname    ; XSEC-OK: read through cs:
.cmp:
    mov al, [es:si]
    mov ah, [cs:di]
    call w16d_fold
    cmp al, ah
    jne .none
    or al, al
    jz .all
    inc si
    inc di
    jmp short .cmp
.none:
    mov ax, 1
    xor dx, dx
    pop di
    pop si
    ret
.all:
    ICALL w16_args
    push word [es:bx + 6]           ; the procedure
    push word [es:bx + 4]
    push word [es:bx + 2]           ; lParam
    push word [es:bx]
    mov si, w16_cont_ret            ; its answer, EnumFontFamilies'
    xor ax, ax
    mov cx, LF_SIZE + NTM_SIZE
    ICALL w16_cbopen                ; ES:DI = the LOGFONT, then the metrics
    push es
    mov es, [w16_face]              ; LOGFONT: its height and width
    mov al, [es:W16F_ROWS]
    xor ah, ah
    mov cl, [es:W16F_AVG]
    xor ch, ch
    pop es
    mov [es:di], ax                 ; lfHeight
    mov [es:di + 2], cx             ; lfWidth
    mov word [es:di + 8], 700       ; lfWeight: bold
    mov word [es:di + 14], 0x0201   ; lfOutPrecision OUT_STRING_PRECIS,
    mov word [es:di + 16], 0x2201   ;   lfClipPrecision CLIP_STROKE_PRECIS,
    push di                         ;   lfQuality DRAFT, VARIABLE_PITCH |
    add di, 18                      ;   FF_SWISS; lfFaceName
    mov si, w16g_facename   ; XSEC-OK: read through cs:
.nm:
    mov al, [cs:si]
    mov [es:di], al
    inc si
    inc di
    or al, al
    jnz .nm
    pop di
    push di
    add di, LF_SIZE
    ICALL w16_tmfill                ; the TEXTMETRIC; the rest 0
    pop di
    mov ax, es                      ; the procedure's arguments, pascal:
    ICALL w16_ppush                 ;   lplf,
    mov ax, di
    ICALL w16_ppush
    mov ax, es                      ;   lpntm,
    ICALL w16_ppush
    mov ax, di
    add ax, LF_SIZE
    ICALL w16_ppush
    mov ax, 1                       ;   RASTER_FONTTYPE,
    ICALL w16_ppush
    pop cx                          ;   lParam, its high word first
    pop ax
    ICALL w16_ppush
    mov ax, cx
    ICALL w16_ppush
    pop ax
    pop dx
    ICALL w16_cbcall
    pop di
    pop si
    ret

w16g_sysname: db 'SYSTEM', 0

; GetTextFace (GDI.92) - args: +0 lpFaceName, +4 nCount, +6 hDC: "System"
; and its NUL, cut to the count; its length
W16LIB w16g_textface
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx + 4]
    les di, [es:bx]
    xor ax, ax
    jcxz .out
    dec cx
    mov si, w16g_facename   ; XSEC-OK: read through cs:
.c:
    jcxz .end
    mov dl, [cs:si]
    or dl, dl
    jz .end
    mov [es:di], dl
    inc si
    inc di
    inc ax
    dec cx
    jmp short .c
.end:
    mov byte [es:di], 0
.out:
    xor dx, dx
    pop di
    pop si
    ret

w16g_facename: db 'System', 0

; GetRasterizerCaps (GDI.313) - args: +0 cb, +2 lpraststat: no TrueType -
; nSize 6, wFlags 0, nLanguageID 0
W16LIB w16g_rastcaps
    ICALL w16_args
    mov cx, [es:bx]
    les bx, [es:bx + 2]
    cmp cx, 6
    jb .out
    mov word [es:bx], 6
    mov word [es:bx + 2], 0
    mov word [es:bx + 4], 0
.out:
    mov ax, 1
    xor dx, dx
    ret

; SetWindowOrg (GDI.11) - args: +0 y, +2 x, +4 hDC: logical x, y is at the
; viewport's origin, the old one the answer - in MM_TEXT, the DC's origin
; moved back by the difference (SPEC.md 124.22.16)
W16LIB w16g_setworg
    push si
    mov si, w16_worg
    xor cx, cx
    call w16g_pair
    jc .out
    mov cx, [w16_worg]
    sub cx, ax
    sub [w16_ox], cx
    mov cx, [w16_worg + 2]
    sub cx, dx
    sub [w16_oy], cx
.out:
    pop si
    ret

; Polygon (GDI.36) - args: +0 nCount, +2 lpPoints, +6 hDC: its edges in the
; pen, the last point joined to the first; the current point is left where
; it was. The INSIDE is not filled with the brush, as Windows' is: os8088
; has no polygon fill (SPEC.md 124.22.16)
W16LIB w16g_polygon
    push si
    push di
    push bp
    ICALL w16_args
    cmp word [es:bx + 6], W16_HDC
    jne .no
    mov cx, [es:bx]
    cmp cx, 2
    jl .no
    push word [w16_curx]
    push word [w16_cury]
    mov si, cx                      ; the last point is the first edge's start
    dec si
    shl si, 1
    shl si, 1
    les bx, [es:bx + 2]
    mov ax, [es:bx + si]
    mov [w16_curx], ax
    mov ax, [es:bx + si + 2]
    mov [w16_cury], ax
    xor si, si                      ; SI = the point's offset, CX how many
.e:
    push cx
    push si
    ICALL w16_args
    les bx, [es:bx + 2]
    mov ax, [es:bx + si]
    mov [w16_lx1], ax
    mov ax, [es:bx + si + 2]
    mov [w16_ly1], ax
    ICALL w16_lineline
    pop si
    pop cx
    add si, 4
    loop .e
    pop word [w16_cury]
    pop word [w16_curx]
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; =============================================================================
; Hooks (SPEC.md 124.22.16): SetWindowsHook keeps a chain, a type's last in
; front; WH_MSGFILTER is called by the modal dialog loop with each message
; (MSGF_DIALOGBOX), and every other type is kept and answered, not called
; =============================================================================
W16_HKMAX equ 4                 ; hooks at once: a type, its procedure
WH_MSGFILTER equ -1
%if W16_HKMAX * 6 != 24 || W16_HKMAX * 2 != 8
%error "w16_hooks and w16_hkord are sized for W16_HKMAX in win16.asm"
%endif

; SetWindowsHook (USER.121) - args: +0 lpfn, +4 nFilterType: in front of
; its type's chain; the one it is in front of, the program's to call through
; DefHookProc, is the answer (0 none)
W16LIB w16u_sethook
    push si
    ICALL w16_args
    mov si, w16_hooks
    mov cx, W16_HKMAX
.f:
    cmp word [si + 2], 0            ; a free entry
    jne .n
    cmp word [si + 4], 0
    je .take
.n:
    add si, 6
    loop .f
    xor ax, ax                      ; full: not hooked
    xor dx, dx
    pop si
    ret
.take:
    mov ax, [es:bx + 4]
    mov cx, [es:bx]
    mov dx, [es:bx + 2]
    push ax
    call w16u_hookof                ; DX:AX = the type's front one now
    pop cx
    mov [si], cx
    push ax
    ICALL w16_args
    mov cx, [es:bx]
    mov [si + 2], cx
    mov cx, [es:bx + 2]
    mov [si + 4], cx
    inc word [w16_hkseq]
    mov cx, [w16_hkseq]
    mov [w16_hkord + si - w16_hooks], cx
    pop ax
    pop si
    ret

; w16u_hookof - AX = a type -> DX:AX = the front of its chain (0 none).
; Keeps the rest.
w16u_hookof:
    push bx
    push cx
    push si
    push di
    mov di, ax
    xor ax, ax
    xor dx, dx
    xor cx, cx                      ; CX = the front's order
    mov si, w16_hooks
    mov bx, w16_hkord
.f:
    cmp si, w16_hooks + W16_HKMAX * 6
    jae .out
    cmp [si], di
    jne .n
    mov dx, [si + 2]
    or dx, [si + 4]
    jz .n
    cmp [bx], cx
    jb .n
    mov cx, [bx]
    mov ax, [si + 2]
    mov dx, [si + 4]
.n:
    add si, 6
    add bx, 2
    jmp short .f
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; UnhookWindowsHook (USER.234) - args: +0 lpfnHook, +4 nHook: out of the
; chain. TRUE when it was in it
W16LIB w16u_unhook
    push si
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov cx, [es:bx]
    mov dx, [es:bx + 2]
    mov si, w16_hooks
.f:
    cmp si, w16_hooks + W16_HKMAX * 6
    jae .no
    cmp [si], ax
    jne .n
    cmp [si + 2], cx
    jne .n
    cmp [si + 4], dx
    je .yes
.n:
    add si, 6
    jmp short .f
.yes:
    xor ax, ax
    mov [si + 2], ax
    mov [si + 4], ax
    inc ax
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop si
    ret

; DefHookProc (USER.235) - args: +0 lplpfnNextHook, +4 lParam, +8 wParam,
; +10 nCode: the next hook in the chain called with them, its answer the
; answer; none, 0
W16LIB w16u_defhook
    push si
    ICALL w16_args
    les bx, [es:bx]
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov cx, ax
    or cx, dx
    jz .zero
    push ax
    push dx
    ICALL w16_args
    push word [es:bx + 10]
    push word [es:bx + 8]
    push word [es:bx + 6]
    push word [es:bx + 4]
    mov si, w16_cont_ret
    xor ax, ax
    xor cx, cx
    ICALL w16_cbopen
    pop dx                          ; lParam's low word
    pop cx                          ; ...its high
    pop bx                          ; wParam
    pop ax                          ; nCode
    ICALL w16_ppush
    mov ax, bx
    ICALL w16_ppush
    mov ax, cx
    ICALL w16_ppush
    mov ax, dx
    ICALL w16_ppush
    pop dx
    pop ax
    ICALL w16_cbcall
    pop si
    ret
.zero:
    xor ax, ax
    xor dx, dx
    pop si
    ret

section .text

w16s_dir:    db ':\SYSTEM\APPDATA\WIN16', 0
w16s_dirlen  equ $ - w16s_dir
