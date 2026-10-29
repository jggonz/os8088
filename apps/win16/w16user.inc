; =============================================================================
; os8088 - apps/win16/w16user.inc
;
; USER: CLASSES, WINDOWS, THE QUEUE AND THE PUMP (SPEC.md 124.8). A Windows
; program's main loop is GetMessage / TranslateMessage / DispatchMessage, and
; here it is the coroutine's: GetMessage with nothing to give WAITS
; (SPEC.md 124.2.1), and the os8088 callback that has something - a click, a
; key, a timer, a W_PAINT, the close box - resumes it (w16_kick). Everything
; that calls the program back (WM_CREATE, DispatchMessage, WM_DESTROY,
; WM_ERASEBKGND) is a continuation on the program's stack (SPEC.md 124.2.2),
; so the host stack never holds a call into the program.
;
; Shaped after Wine's user32 message.c/win.c, as winevdm's user.exe16 calls
; them, cut to what a real-mode Windows 3.0 program sees of one task.
; =============================================================================

W16_HWND0   equ 0x2000          ; HWND = W16_HWND0 + 4 * slot

; --- a class -------------------------------------------------------------------
W16C_NAME   equ 0               ; 16: upper case, NUL-padded; 0 = a free slot
W16C_STYLE  equ 16
W16C_PROC   equ 18              ; far
W16C_HBR    equ 22
W16C_WEXTRA equ 24
W16C_MENU   equ 26              ; far: lpszMenuName (SPEC.md 124.11)

; --- a window ------------------------------------------------------------------
W16W_FLAGS  equ 0               ; W16WF_*
W16W_CLS    equ 1
W16W_PROC   equ 2               ; far, the class's
W16W_HBR    equ 6               ; ...its brush
W16W_STYLE  equ 8               ; dword
W16W_UL     equ 12              ; the invalid rectangle, client coordinates,
W16W_UT     equ 14              ;   right and bottom exclusive as Windows'
W16W_UR     equ 16
W16W_UB     equ 18
W16W_EXTRA  equ 20              ; cbWndExtra, at most 16
W16W_MENU   equ 36              ; its HMENU, 0 = none (SPEC.md 124.11)

W16WF_USED  equ 1
W16WF_INVAL equ 2
W16WF_ERASE equ 4
W16WF_VIS   equ 8

; --- a timer -------------------------------------------------------------------
W16T_USED   equ 0
W16T_FIRED  equ 1
W16T_HWND   equ 2
W16T_ID     equ 4
W16T_PER    equ 6               ; ticks
W16T_DUE    equ 8               ; OSAPI_GET_TICKS at which it fires

; --- a queued message is a MSG ---------------------------------------------------
MSG_HWND    equ 0
MSG_MSG     equ 2
MSG_WP      equ 4
MSG_LP      equ 6
MSG_TIME    equ 10
MSG_PT      equ 14

WM_CREATE     equ 0x0001
WM_DESTROY    equ 0x0002
WM_SIZE       equ 0x0005
WM_PAINT      equ 0x000F
WM_CLOSE      equ 0x0010
WM_QUIT       equ 0x0012
WM_ERASEBKGND equ 0x0014
WM_KEYDOWN    equ 0x0100
WM_KEYUP      equ 0x0101
WM_CHAR       equ 0x0102
WM_SYSKEYDOWN equ 0x0104
SC_CLOSE_KEY  equ 0xF060
WM_TIMER      equ 0x0113
WM_SYSCOMMAND equ 0x0112
WM_MOUSEMOVE  equ 0x0200
WM_LBUTTONDOWN equ 0x0201
WM_LBUTTONUP  equ 0x0202
WM_RBUTTONDOWN equ 0x0204
WM_RBUTTONUP  equ 0x0205
MK_LBUTTON    equ 1
MK_RBUTTON    equ 2
SC_CLOSE      equ 0xF060
CW_USEDEFAULT equ 0x8000
WS_CHILD_HI   equ 0x4000        ; the style's high word
WS_THICKFRAME_HI equ 0x0004
WS_MINIMIZE_HI equ 0x2000
SWP_NOSIZE    equ 0x0001
PM_REMOVE     equ 0x0001

; =============================================================================
; Handles and records
; =============================================================================

; w16_hwnd2rec - AX = an HWND -> BX = its record, CX = its slot; CF=1 not one
w16_hwnd2rec:
    mov bx, ax
    sub bx, W16_HWND0
    jb .no
    test bl, 3
    jnz .no
    shr bx, 1
    shr bx, 1
    cmp bx, W16_MAXWND
    jae .no
    mov cx, bx
    call w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .no
    clc
    ret
.no:
    stc
    ret

; w16_slot2rec - CX = a slot -> BX = its record. Keeps all else.
w16_slot2rec:
    push ax
    push dx
    mov ax, W16W_SIZE
    mul cx
    add ax, w16_wnd
    mov bx, ax
    pop dx
    pop ax
    ret

; w16_toprec - the top-level window: BX = its record, AX = its HWND, CX = its
; slot; CF=1 there is none
w16_toprec:
    mov cl, [w16_top]
    xor ch, ch
    jcxz .no
    dec cx
    call w16_slot2rec
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    clc
    ret
.no:
    stc
    ret

; w16_hwnd0 - AX = an HWND, where 0 means the top-level window -> as
; w16_hwnd2rec
w16_hwnd0:
    or ax, ax
    jnz w16_hwnd2rec
    jmp w16_toprec

; w16_farname - DX:AX = a far class name -> [w16_namebuf], upper case and
; NUL-padded to 16. CF=1: NULL, an atom (segment 0), empty or over 15.
w16_farname:
    push cx
    push si
    push di
    push es
    or dx, dx
    jz .no
    mov es, dx
    mov si, ax
    mov di, w16_namebuf
    mov cx, 16
.cp:
    mov al, [es:si]
    or al, al
    jz .end
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 'a' - 'A'
.up:
    mov [di], al
    inc si
    inc di
    loop .cp
    jmp short .no                   ; sixteen and still going: too long
.end:
    cmp di, w16_namebuf
    je .no
.pad:
    mov byte [di], 0
    inc di
    loop .pad
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    ret

; w16_findcls - [w16_namebuf] -> BX = the class; CF=1 none
w16_findcls:
    push cx
    push si
    push di
    mov bx, w16_cls
    mov cx, W16_MAXCLS
.cls:
    cmp byte [bx + W16C_NAME], 0
    je .next
    mov si, w16_namebuf
    lea di, [bx + W16C_NAME]
    push cx
    mov cx, 16
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .ne
    inc si
    inc di
    loop .cmp
    pop cx                          ; all sixteen equal (not ZF: the incs
    jmp short .hit                  ;   after the last compare moved it)
.ne:
    pop cx
.next:
    add bx, W16C_SIZE
    loop .cls
    stc
    jmp short .out
.hit:
    clc
.out:
    pop di
    pop si
    pop cx
    ret

; =============================================================================
; RegisterClass (USER.57) - args: +0 lpWndClass (off, seg)
; =============================================================================
w16u_regclass:
    push si
    push di
    call w16_args
    les si, [es:bx]                 ; ES:SI = the WNDCLASS
    cmp word [es:si + 8], 16        ; cbWndExtra
    ja .fail
    mov ax, [es:si + 22]            ; lpszClassName
    mov dx, [es:si + 24]
    call w16_farname
    jc .fail
    call w16_findcls
    jnc .fail                       ; Windows refuses a second registration
    mov di, w16_cls
    mov cx, W16_MAXCLS
.free:
    cmp byte [di + W16C_NAME], 0
    je .take
    add di, W16C_SIZE
    loop .free
    jmp short .fail
.take:
    push si
    mov si, w16_namebuf
    mov cx, 8
.nm:
    mov ax, [si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .nm
    pop si
    sub di, 16
    mov ax, [es:si]                 ; style
    mov [di + W16C_STYLE], ax
    mov ax, [es:si + 2]             ; lpfnWndProc
    mov [di + W16C_PROC], ax
    mov ax, [es:si + 4]
    mov [di + W16C_PROC + 2], ax
    mov ax, [es:si + 16]            ; hbrBackground
    mov [di + W16C_HBR], ax
    mov ax, [es:si + 8]
    mov [di + W16C_WEXTRA], ax
    mov ax, [es:si + 18]            ; lpszMenuName
    mov [di + W16C_MENU], ax
    mov ax, [es:si + 20]
    mov [di + W16C_MENU + 2], ax
    mov ax, di                      ; an atom-shaped non-zero answer
    sub ax, w16_cls
    or ax, 0xC000
    jmp short .out
.fail:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; =============================================================================
; CreateWindow (USER.41) - args: +0 lpParam, +4 hInstance, +6 hMenu,
; +8 hWndParent, +10 cy, +12 cx, +14 y, +16 x, +18 dwStyle, +22 lpWindowName,
; +26 lpClassName. Their order from +0 to +29 IS a CREATESTRUCT's, which is
; why WM_CREATE's lParam is these thirty bytes copied.
; =============================================================================
W16_CSSIZE  equ 34

w16u_createwnd:
    push si
    push di
    call w16_args
    mov [w16_cwargs], bx
    test word [es:bx + 20], WS_CHILD_HI
    jnz .fail                       ; S3 has one top-level window and no
    cmp byte [w16_top], 0           ;   children (SPEC.md 124.8)
    jne .fail
    mov ax, [es:bx + 26]
    mov dx, [es:bx + 28]
    call w16_farname
    jc .fail
    call w16_findcls                ; BX = the class
    jc .fail
    mov di, bx
    xor cx, cx                      ; a free window slot
.slot:
    call w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .take
    inc cx
    cmp cx, W16_MAXWND
    jb .slot
.fail:
    xor ax, ax
    jmp .out
.take:
    mov si, bx                      ; SI = the window, DI = its class
    push cx
    push di
    mov di, si
    mov cx, W16W_SIZE / 2
    xor ax, ax
    push ds
    pop es
    rep stosw
    pop di
    pop cx
    mov byte [si + W16W_FLAGS], W16WF_USED | W16WF_INVAL | W16WF_ERASE
    mov ax, di
    sub ax, w16_cls
    mov dl, W16C_SIZE
    div dl
    mov [si + W16W_CLS], al
    mov ax, [di + W16C_PROC]
    mov [si + W16W_PROC], ax
    mov ax, [di + W16C_PROC + 2]
    mov [si + W16W_PROC + 2], ax
    mov ax, [di + W16C_HBR]
    mov [si + W16W_HBR], ax
    mov word [si + W16W_UR], 0x7FFF ; the whole client, erased
    mov word [si + W16W_UB], 0x7FFF
    mov es, [w16_pss]
    mov bx, [w16_cwargs]
    mov ax, [es:bx + 18]
    mov [si + W16W_STYLE], ax
    mov ax, [es:bx + 20]
    mov [si + W16W_STYLE + 2], ax   ; WS_MINIMIZE kept: ShowWindow restores
    mov al, cl
    inc al
    mov [w16_top], al
    mov [w16_cwslot], cx
    mov ax, [es:bx + 6]             ; hMenu, else the class's menu by name
    or ax, ax                       ;   (SPEC.md 124.11)
    jnz .menu
    push bx
    mov bx, di
    mov ax, [bx + W16C_MENU]
    mov dx, [bx + W16C_MENU + 2]
    pop bx
    mov cx, ax
    or cx, dx
    jz .menu
    call w16_mload
    mov es, [w16_pss]
.menu:
    mov [si + W16W_MENU], ax

    push si                         ; --- WIN16's window becomes the program's
    mov si, [es:bx + 22]            ; the window text is the title
    mov dx, [es:bx + 24]
    mov di, w16_wtitle
    mov cx, 40
    mov ax, w16_s_empty
    call w16_farline
    mov word [w16_titleptr], w16_wtitle
    call w16_lock
    mov bx, [w16_win]
    mov al, 1                       ; W_PAINT paints every pixel (SPEC.md 124.8)
    call OSAPI_WM_OWNBG
    mov ax, w16_wtitle
    call OSAPI_WM_TITLE
    call w16_unlock
    pop si
    mov es, [w16_pss]
    mov bx, [w16_cwargs]
    mov cx, [es:bx + 12]
    mov dx, [es:bx + 10]
    mov ax, [es:bx + 20]
    call w16_sizeto

    mov si, w16_cw_done             ; --- WM_CREATE, with its CREATESTRUCT
    mov ax, [w16_cwslot]
    mov cx, W16_CSSIZE
    call w16_cbopen                 ; ES:DI = the block, zeroed
    push ds
    mov si, [w16_cwargs]
    push es
    pop ds                          ; both in the program's stack segment
    mov cx, 15
.cs:
    mov ax, [si]
    mov [es:di], ax
    inc si
    inc si
    inc di
    inc di
    loop .cs
    pop ds
    sub di, 30
    mov [w16_sm_lp], di
    mov [w16_sm_lp + 2], es
    mov cx, [w16_cwslot]
    call w16_slot2rec
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_CREATE
    mov word [w16_sm_wp], 0
    call w16_sendgo
.out:
    pop di
    pop si
    ret

; w16_cw_done - WM_CREATE answered: -1 refuses the window, anything else
; is followed by WM_SIZE, as Windows' CreateWindow follows it
w16_cw_done:
    and ax, dx
    inc ax
    jz .refused
    call w16_slot2rec
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    call w16_sizemsg
    push si
    mov si, w16_cw_sized
    mov ax, cx
    call w16_sendwnd
    pop si
    ret
.refused:
    call w16_slot2rec
    call w16_wndfree
    xor ax, ax
    ret

; w16_cw_sized - ...and CreateWindow answers the HWND
w16_cw_sized:
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    xor dx, dx
    ret

; w16_sizeto - give WIN16's window the client a Windows window of CX by DX
; would have (SPEC.md 124.8): cx - 2 frame by cy - caption - 2 frame, the
; frame 4 when AX (the style's high word) has WS_THICKFRAME and 1 otherwise.
; The caption is SM_CYCAPTION less the row it shares with the frame: 19 on
; VGA, MEASURED on Windows 3.0 (PUMP.EXE: 320 x 180 is a 312 x 153 client),
; and the System font's rows + 3 on every display (w16_capn).
; CW_USEDEFAULT keeps the size. Clobbers AX, BX, CX, DX, SI, ES.
w16_sizeto:
    cmp cx, CW_USEDEFAULT
    je .out
    mov bx, 2
    test ax, WS_THICKFRAME_HI
    jz .thin
    mov bx, 8
.thin:
    sub cx, bx                      ; the client Windows would have given
    jle .out
    sub dx, bx
    sub dx, [w16_capn]
    jle .out
    push cx
    push dx
    mov bx, [w16_win]
    call OSAPI_WM_GEOM              ; CX/DX = the content we have now
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx + W_W]
    sub ax, cx                      ; ...and what the frame costs
    mov si, [es:bx + W_H]
    sub si, dx
    pop dx
    pop cx
    add cx, ax
    add dx, si
    call OSAPI_WM_RESIZE
.out:
    ret

; w16_sizemsg - [w16_sm_*] = WM_SIZE for the window AX, with the client's
; size now
w16_sizemsg:
    push bx
    push cx
    push dx
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_SIZE
    mov word [w16_sm_wp], 0         ; SIZE_RESTORED
    mov bx, [w16_win]
    call OSAPI_WM_GEOM
    jnc .have
    xor cx, cx
    xor dx, dx
.have:
    call w16_nobar
    mov [w16_sm_lp], cx
    mov [w16_sm_lp + 2], dx
    pop dx
    pop cx
    pop bx
    ret

; w16_nobar - DX = the content's height -> the client's: the menu bar's off,
; never below 0 (SPEC.md 124.11). Keeps the rest.
w16_nobar:
    push ax
    call w16_mbarh
    sub dx, ax
    jge .ok
    xor dx, dx
.ok:
    pop ax
    ret

; MoveWindow (USER.56) - args: +0 bRepaint, +2 cy, +4 cx, +6 y, +8 x, +10 hWnd
w16u_movewnd:
    call w16_args
    mov cx, [es:bx + 4]
    mov dx, [es:bx + 2]
    mov ax, [es:bx + 10]
    jmp short w16_moveto

; SetWindowPos (USER.232) - args: +0 flags, +2 cy, +4 cx, +6 y, +8 x,
; +10 hWndInsertAfter, +12 hWnd. The place and the Z order are os8088's.
w16u_setwndpos:
    call w16_args
    test word [es:bx], SWP_NOSIZE
    jnz .one
    mov cx, [es:bx + 4]
    mov dx, [es:bx + 2]
    mov ax, [es:bx + 12]
    jmp short w16_moveto
.one:
    mov ax, 1
    ret

; w16_moveto - AX = the HWND, CX by DX its new outer size: the top-level
; window takes it, is invalid and erased whole, and hears WM_SIZE. Answers 1.
w16_moveto:
    push si
    mov [w16_mvcx], cx
    mov [w16_mvcy], dx
    mov [w16_mvhwnd], ax
    call w16_hwnd2rec               ; BX = the record, CX = the slot
    jc .no
    mov al, [w16_top]
    dec al
    cmp al, cl
    jne .one                        ; only the top-level window has a size
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .one                        ; ...and a minimized one moves its icon,
    push bx                         ;   keeping the size it restores to
    mov ax, [bx + W16W_STYLE + 2]
    mov cx, [w16_mvcx]
    mov dx, [w16_mvcy]
    call w16_sizeto
    pop bx
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    call w16_inval
    mov ax, [w16_mvhwnd]
    call w16_sizemsg
    mov si, w16_cont_one
    xor ax, ax
    call w16_sendwnd
    pop si
    ret
.one:
    mov ax, 1
    pop si
    ret
.no:
    xor ax, ax
    pop si
    ret

; w16_wndfree - BX = a window record: release it, its timers, its capture
; and the top-level window it was. Clobbers CX.
w16_wndfree:
    push ax
    push si
    mov ax, bx
    sub ax, w16_wnd
    mov cl, W16W_SIZE
    div cl
    xor ah, ah                      ; AX = the slot
    mov byte [bx + W16W_FLAGS], 0
    push ax
    xor ax, ax                      ; its menu goes with it, as Windows'
    xchg ax, [bx + W16W_MENU]
    or ax, ax
    jz .nomenu
    call w16_mdestroy
.nomenu:
    pop ax
    inc ax
    cmp al, [w16_top]
    jne .timers
    mov byte [w16_top], 0           ; WIN16's window is its own again
    mov word [w16_titleptr], w16_name
    push ax
    push bx
    mov bx, [w16_win]
    xor al, al
    call OSAPI_WM_OWNBG
    pop bx
    pop ax
.timers:
    dec ax
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0               ; AX = the HWND
    mov si, w16_tmr
    mov cx, W16_MAXTMR
.t:
    cmp [si + W16T_HWND], ax
    jne .tn
    mov byte [si + W16T_USED], 0
.tn:
    add si, W16T_SIZE
    loop .t
    cmp [w16_capture], ax
    jne .nocap
    mov word [w16_capture], 0
.nocap:
    call w16_tarm
    pop si
    pop ax
    ret

; ShowWindow (USER.42) - args: +0 nCmdShow, +2 hWnd. The window is already on
; the screen; this answers whether it was visible before. SW_SHOWNORMAL or
; SW_RESTORE on a window created minimized restores it, with WM_SIZE, at the
; size CreateWindow gave it - what Windows 3.0 does (PUMP.EXE).
SW_SHOWNORMAL equ 1
SW_RESTORE  equ 9

w16u_showwnd:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    call w16_hwnd2rec               ; keeps AX = the HWND
    jc .zero
    xor di, di                      ; DI = the answer: visible before?
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .was
    inc di
.was:
    and byte [bx + W16W_FLAGS], ~W16WF_VIS
    or dx, dx                       ; SW_HIDE
    jz .done
    or byte [bx + W16W_FLAGS], W16WF_VIS
    cmp dx, SW_SHOWNORMAL
    je .restore
    cmp dx, SW_RESTORE
    jne .done
.restore:
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jz .done
    and word [bx + W16W_STYLE + 2], ~WS_MINIMIZE_HI
    call w16_sizemsg
    mov si, w16_sw_done
    mov ax, di
    call w16_sendwnd
    jmp short .out
.zero:
    xor di, di
.done:
    mov ax, di
.out:
    pop di
    pop si
    ret

w16_sw_done:
    mov ax, cx
    ret

; DestroyWindow (USER.53) - args: +0 hWnd
w16u_destroywnd:
    call w16_args
    mov ax, [es:bx]
    ; fall into w16_destroy

; w16_destroy - AX = the HWND: WM_DESTROY, then the slot. Serves any import
; (DefWindowProc's WM_CLOSE reaches it too); answers 1, or 0 for no window.
w16_destroy:
    call w16_hwnd2rec
    jc .no
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_DESTROY
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    push si
    mov si, w16_dw_done
    mov ax, cx
    call w16_sendwnd
    pop si
    ret
.no:
    xor ax, ax
    xor dx, dx
    ret

w16_dw_done:
    call w16_slot2rec
    call w16_wndfree
    mov ax, 1
    xor dx, dx
    ret

; =============================================================================
; Sending: a window procedure called through SPEC.md 124.2.2
; =============================================================================

; w16_sendwnd - BX = the window, SI = the continuation, AX = its word, and
; [w16_sm_*] the message: call the window's procedure
w16_sendwnd:
    push di
    push es
    xor cx, cx
    call w16_cbopen
    pop es
    pop di
    ; fall into w16_sendgo

; w16_sendgo - push [w16_sm_*] as the procedure's arguments and enter it,
; BX = the window. The continuation is already open.
w16_sendgo:
    push ax
    push dx
    mov ax, [w16_sm_hwnd]           ; pascal: the first argument first
    call w16_ppush
    mov ax, [w16_sm_msg]
    call w16_ppush
    mov ax, [w16_sm_wp]
    call w16_ppush
    mov ax, [w16_sm_lp + 2]
    call w16_ppush
    mov ax, [w16_sm_lp]
    call w16_ppush
    mov ax, [bx + W16W_PROC]
    mov dx, [bx + W16W_PROC + 2]
    call w16_cbcall
    pop dx
    pop ax
    ret

; w16_cont_ret - a continuation that answers what the procedure answered
w16_cont_ret:
    ret

; w16_cont_one - ...and one that answers 1
w16_cont_one:
    mov ax, 1
    xor dx, dx
    ret

; w16_cont_zero - ...and one that answers 0
w16_cont_zero:
    xor ax, ax
    xor dx, dx
    ret

; w16_smargs - [w16_sm_*] from SendMessage-shaped arguments at ES:BX:
; +0 lParam, +4 wParam, +6 msg, +8 hWnd. Out AX = the HWND.
w16_smargs:
    mov ax, [es:bx]
    mov [w16_sm_lp], ax
    mov ax, [es:bx + 2]
    mov [w16_sm_lp + 2], ax
    mov ax, [es:bx + 4]
    mov [w16_sm_wp], ax
    mov ax, [es:bx + 6]
    mov [w16_sm_msg], ax
    mov ax, [es:bx + 8]
    mov [w16_sm_hwnd], ax
    ret

; SendMessage (USER.111) - args: as w16_smargs
w16u_sendmsg:
    call w16_args
    call w16_smargs
    call w16_hwnd2rec
    jc .no
    push si
    mov si, w16_cont_ret
    call w16_sendwnd
    pop si
    ret
.no:
    xor ax, ax
    xor dx, dx
    ret

; PostMessage (USER.110) - args: as w16_smargs
w16u_postmsg:
    call w16_args
    call w16_smargs
    xor al, al
    call w16_post
    mov ax, 0
    jc .out
    inc ax
.out:
    ret

; PostQuitMessage (USER.6) - args: +0 nExitCode
w16u_postquit:
    call w16_args
    mov ax, [es:bx]
    mov [w16_quitcode], ax
    mov byte [w16_quit], 1
    ret

; DispatchMessage (USER.114) - args: +0 lpMsg
w16u_dispatch:
    call w16_args
    les bx, [es:bx]
    mov ax, [es:bx + MSG_LP]
    mov [w16_sm_lp], ax
    mov ax, [es:bx + MSG_LP + 2]
    mov [w16_sm_lp + 2], ax
    mov ax, [es:bx + MSG_WP]
    mov [w16_sm_wp], ax
    mov ax, [es:bx + MSG_MSG]
    mov [w16_sm_msg], ax
    mov ax, [es:bx + MSG_HWND]
    mov [w16_sm_hwnd], ax
    call w16_hwnd2rec
    jc .no
    push si
    mov si, w16_cont_ret
    call w16_sendwnd
    pop si
    ret
.no:
    xor ax, ax
    xor dx, dx
    ret

; TranslateMessage (USER.113) - args: +0 lpMsg. A key-down whose key had an
; ASCII code posts its WM_CHAR at the FRONT of the queue, so it is the very
; next message, ahead of the key-up already queued (SPEC.md 124.8).
w16u_translate:
    call w16_args
    les bx, [es:bx]
    mov ax, [es:bx + MSG_MSG]
    cmp ax, WM_KEYDOWN
    je .key
    cmp ax, WM_SYSKEYDOWN
    jne .no
.key:
    mov al, [es:bx + MSG_LP + 2]    ; the scan code
    cmp al, W16_NSCAN
    jae .no
    push bx
    mov bl, al
    xor bh, bh
    mov al, [w16_kascii + bx]
    pop bx
    or al, al
    jz .no
    xor ah, ah
    mov [w16_sm_wp], ax
    mov ax, [es:bx + MSG_HWND]
    mov [w16_sm_hwnd], ax
    mov ax, [es:bx + MSG_MSG]
    add ax, WM_CHAR - WM_KEYDOWN    ; WM_CHAR, or WM_SYSCHAR
    mov [w16_sm_msg], ax
    mov ax, [es:bx + MSG_LP]
    mov [w16_sm_lp], ax
    mov ax, [es:bx + MSG_LP + 2]
    mov [w16_sm_lp + 2], ax
    mov al, 1
    call w16_post
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; =============================================================================
; The queue (SPEC.md 124.8)
; =============================================================================

; w16_post - queue [w16_sm_*], AL = 0 at the back / 1 at the front, stamped
; with the time and the last pointer position. CF=1: full, and dropped.
w16_post:
    push bx
    push cx
    push dx
    cmp byte [w16_qcount], W16_QLEN
    jae .full
    mov cl, [w16_qhead]
    or al, al
    jz .back
    dec cl                          ; the front: one before the head
    and cl, W16_QLEN - 1
    mov [w16_qhead], cl
    jmp short .at
.back:
    add cl, [w16_qcount]
    and cl, W16_QLEN - 1
.at:
    inc byte [w16_qcount]
    mov al, MSG_SIZE
    mul cl
    add ax, w16_q
    mov bx, ax
    mov ax, [w16_sm_hwnd]
    mov [bx + MSG_HWND], ax
    mov ax, [w16_sm_msg]
    mov [bx + MSG_MSG], ax
    mov ax, [w16_sm_wp]
    mov [bx + MSG_WP], ax
    mov ax, [w16_sm_lp]
    mov [bx + MSG_LP], ax
    mov ax, [w16_sm_lp + 2]
    mov [bx + MSG_LP + 2], ax
    call w16_stamp
    clc
    jmp short .out
.full:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; w16_stamp - BX -> a MSG: its time and point, now
w16_stamp:
    call w16_ticksms
    mov [bx + MSG_TIME], ax
    mov [bx + MSG_TIME + 2], dx
    mov ax, [w16_mx]
    mov [bx + MSG_PT], ax
    mov ax, [w16_my]
    mov [bx + MSG_PT + 2], ax
    ret

; w16_nextmsg - the next message in Windows' order (SPEC.md 124.8) into
; [w16_msg]; AL = 1 takes it. CF=1: there is none. Keeps SI and DI.
w16_nextmsg:
    push si
    push di
    mov [w16_take], al
    mov bx, w16_msg
    cmp byte [w16_qcount], 0        ; 1. the posted messages
    je .quit
    mov al, MSG_SIZE
    mul byte [w16_qhead]
    add ax, w16_q
    mov si, ax
    mov di, w16_msg
    mov cx, MSG_SIZE / 2
.cp:
    mov ax, [si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .cp
    cmp byte [w16_take], 0
    je .have
    mov al, [w16_qhead]
    inc al
    and al, W16_QLEN - 1
    mov [w16_qhead], al
    dec byte [w16_qcount]
    jmp .have
.quit:
    xor ax, ax
    mov [bx + MSG_LP], ax
    mov [bx + MSG_LP + 2], ax
    cmp byte [w16_quit], 0          ; 2. WM_QUIT
    je .paint
    mov [bx + MSG_HWND], ax
    mov word [bx + MSG_MSG], WM_QUIT
    mov ax, [w16_quitcode]
    mov [bx + MSG_WP], ax
    cmp byte [w16_take], 0
    je .gen
    mov byte [w16_quit], 0
    jmp short .gen
.paint:
    xor cx, cx                      ; 3. WM_PAINT, a window that is invalid
.pw:
    call w16_slot2rec
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_INVAL
    cmp al, W16WF_USED | W16WF_INVAL
    je .ispaint
    inc cx
    cmp cx, W16_MAXWND
    jb .pw
    mov si, w16_tmr                 ; 4. WM_TIMER, one that has fired
    mov cx, W16_MAXTMR
.tm:
    cmp byte [si + W16T_USED], 0
    je .tn
    cmp byte [si + W16T_FIRED], 0
    jne .istimer
.tn:
    add si, W16T_SIZE
    loop .tm
    stc
    jmp short .out
.istimer:
    mov bx, w16_msg
    mov ax, [si + W16T_HWND]
    mov [bx + MSG_HWND], ax
    mov word [bx + MSG_MSG], WM_TIMER
    mov ax, [si + W16T_ID]
    mov [bx + MSG_WP], ax
    cmp byte [w16_take], 0
    je .gen
    mov byte [si + W16T_FIRED], 0
    jmp short .gen
.ispaint:
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    mov bx, w16_msg
    mov [bx + MSG_HWND], ax
    mov word [bx + MSG_MSG], WM_PAINT
    mov word [bx + MSG_WP], 0
.gen:
    mov bx, w16_msg
    call w16_stamp
.have:
    clc
.out:
    pop di
    pop si
    ret

; w16_putmsg - [w16_msg] into the waiting call's MSG, whose far pointer is
; at its arguments + BX. Out AX = 0 for WM_QUIT, else 1 (GetMessage's answer)
w16_putmsg:
    push si
    push di
    push es
    mov si, bx
    call w16_args
    les di, [es:bx + si]
    mov si, w16_msg
    mov cx, MSG_SIZE / 2
.cp:
    mov ax, [si]
    mov [es:di], ax
    inc si
    inc si
    inc di
    inc di
    loop .cp
    xor ax, ax
    cmp word [w16_msg + MSG_MSG], WM_QUIT
    je .out
    inc ax
.out:
    pop es
    pop di
    pop si
    ret

; GetMessage (USER.108) - args: +0 wMsgFilterMax, +2 wMsgFilterMin, +4 hWnd,
; +6 lpMsg. S3 honours only the NULL filter, and treats every other as it.
w16u_getmsg:
    mov al, 1
    call w16_nextmsg
    jc .wait
    mov bx, 6
    jmp w16_putmsg
.wait:
    mov byte [w16_inget], 1         ; nothing to give: the next callback that
    mov byte [w16_pend], 1          ;   has something resumes it (w16_kick)
    ret

; PeekMessage (USER.109) - args: +0 wRemoveMsg, +2 max, +4 min, +6 hWnd,
; +8 lpMsg. NOTHING THERE IS A YIELD: the program waits for one os8088 UI
; pass (OSAPI_WM_WAKE) and is answered then, so a PeekMessage loop gives the
; machine a pass a turn instead of owning it - which is what PeekMessage
; did on Windows 3.x, where it yielded to the other tasks.
w16u_peekmsg:
    call w16_peekmsg
    jnc .out
    mov byte [w16_inpeek], 1
    mov byte [w16_pend], 1
    mov bx, [w16_win]
    call OSAPI_WM_WAKE
.out:
    ret

; w16_peekmsg - PeekMessage's answer, from the waiting frame's arguments.
; CF=1 nothing: AX = 0. CF=0: the MSG is filled and AX = 1.
w16_peekmsg:
    call w16_args
    mov al, [es:bx]
    and al, PM_REMOVE
    call w16_nextmsg
    mov ax, 0
    jc .out
    mov bx, 8
    call w16_putmsg
    mov ax, 1
    clc
.out:
    ret

; w16_peekwake - the wake that a waiting PeekMessage asked for: answer it,
; with a message if there is one now, and run on. No lock is held.
w16_peekwake:
    mov byte [w16_inpeek], 0
    call w16_peekmsg
    xor dx, dx
    mov byte [w16_locked], 0
    call w16_resume
    jmp w16_after

; -----------------------------------------------------------------------------
; w16_kick - resume a program waiting in GetMessage, if there is now a message
; for it. The caller has set [w16_locked]. Clobbers AX, BX, CX, DX.
; -----------------------------------------------------------------------------
w16_kick:
    cmp byte [w16_inget], 0
    je .out
    mov al, 1
    call w16_nextmsg
    jc .out
    mov bx, 6
    call w16_putmsg
    mov byte [w16_inget], 0
    xor dx, dx
    call w16_resume
.out:
    ret

; =============================================================================
; Paint (SPEC.md 124.8)
; =============================================================================

; w16_inval - BX = a window, AX/CX/DX/SI = left, top, right, bottom (client,
; exclusive), [w16_erase] = 1 erases: union it into the invalid rectangle
w16_inval:
    cmp ax, dx
    jge .out
    cmp cx, si
    jge .out
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jnz .union
    mov [bx + W16W_UL], ax
    mov [bx + W16W_UT], cx
    mov [bx + W16W_UR], dx
    mov [bx + W16W_UB], si
    or byte [bx + W16W_FLAGS], W16WF_INVAL
    jmp short .erase
.union:
    cmp ax, [bx + W16W_UL]
    jge .t
    mov [bx + W16W_UL], ax
.t:
    cmp cx, [bx + W16W_UT]
    jge .r
    mov [bx + W16W_UT], cx
.r:
    cmp dx, [bx + W16W_UR]
    jle .b
    mov [bx + W16W_UR], dx
.b:
    cmp si, [bx + W16W_UB]
    jle .erase
    mov [bx + W16W_UB], si
.erase:
    cmp byte [w16_erase], 0
    je .out
    or byte [bx + W16W_FLAGS], W16WF_ERASE
.out:
    ret

; InvalidateRect (USER.125) - args: +0 bErase, +2 lpRect, +6 hWnd
w16u_invalrect:
    push si
    call w16_args
    mov al, [es:bx]
    mov [w16_erase], al
    les si, [es:bx + 2]
    push es
    push si
    call w16_args
    mov ax, [es:bx + 6]
    call w16_hwnd0
    pop si
    pop es
    jc .out
    mov ax, es
    or ax, si
    jz .whole
    mov ax, [es:si]
    mov cx, [es:si + 2]
    mov dx, [es:si + 4]
    mov si, [es:si + 6]
    jmp short .go
.whole:
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
.go:
    call w16_inval
.out:
    mov ax, 1
    pop si
    ret

; ValidateRect (USER.127) - args: +0 lpRect, +4 hWnd. NULL, or a rectangle
; that covers the invalid one, validates; S3 keeps the rest invalid whole.
w16u_validrect:
    push si
    call w16_args
    les si, [es:bx]
    push es
    push si
    call w16_args
    mov ax, [es:bx + 4]
    call w16_hwnd0
    pop si
    pop es
    jc .out
    mov ax, es
    or ax, si
    jz .clear
    mov ax, [es:si]
    cmp ax, [bx + W16W_UL]
    jg .out
    mov ax, [es:si + 2]
    cmp ax, [bx + W16W_UT]
    jg .out
    mov ax, [es:si + 4]
    cmp ax, [bx + W16W_UR]
    jl .out
    mov ax, [es:si + 6]
    cmp ax, [bx + W16W_UB]
    jl .out
.clear:
    and byte [bx + W16W_FLAGS], ~(W16WF_INVAL | W16WF_ERASE)
.out:
    mov ax, 1
    pop si
    ret

; UpdateWindow (USER.124) - args: +0 hWnd. WM_PAINT now, if it is invalid.
w16u_updatewnd:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    jc .one
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jz .one
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_PAINT
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    push si
    mov si, w16_cont_one
    call w16_sendwnd
    pop si
    ret
.one:
    mov ax, 1
    ret

; BeginPaint (USER.39) - args: +0 lpPaint, +4 hWnd
PS_HDC      equ 0
PS_ERASE    equ 2
PS_RC       equ 4
PS_SIZE     equ 32

w16u_beginpaint:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 4]
    call w16_hwnd2rec
    jc .fail
    mov al, 1
    call w16_dcopen                 ; the lock, the clip, the DC
    call w16_args
    les di, [es:bx]
    push di
    mov cx, PS_SIZE / 2
    xor ax, ax
    rep stosw
    pop di
    mov word [es:di + PS_HDC], W16_HDC
    mov ax, [w16_prc]               ; rcPaint: the invalid rectangle cut to
    mov [es:di + PS_RC], ax         ;   the client
    mov ax, [w16_prc + 2]
    mov [es:di + PS_RC + 2], ax
    mov ax, [w16_prc + 4]
    mov [es:di + PS_RC + 4], ax
    mov ax, [w16_prc + 6]
    mov [es:di + PS_RC + 6], ax
    mov bx, [w16_dcwnd]
    mov al, [bx + W16W_FLAGS]
    and byte [bx + W16W_FLAGS], ~(W16WF_INVAL | W16WF_ERASE)
    test al, W16WF_ERASE
    jz .done
    mov ax, bx                      ; WM_ERASEBKGND, through the program
    sub ax, w16_wnd
    mov cl, W16W_SIZE
    div cl
    xor ah, ah
    shl ax, 1
    shl ax, 1
    add ax, W16_HWND0
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_ERASEBKGND
    mov word [w16_sm_wp], W16_HDC
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    mov si, w16_bp_erased
    call w16_sendwnd
    jmp short .out
.done:
    mov ax, W16_HDC
    jmp short .out
.fail:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; w16_bp_erased - WM_ERASEBKGND answered: fErase says whether it did not
w16_bp_erased:
    push di
    push es
    or ax, dx
    mov ax, 0
    jnz .set
    inc ax
.set:
    push ax
    call w16_args
    les di, [es:bx]
    pop ax
    mov [es:di + PS_ERASE], ax
    mov ax, W16_HDC
    pop es
    pop di
    ret

; EndPaint (USER.40) - args: +0 lpPaint, +4 hWnd
w16u_endpaint:
    call w16_dcclose
    mov ax, 1
    ret

; GetDC (USER.66) - args: +0 hWnd. The whole client.
w16u_getdc:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd0
    mov ax, 0
    jc .out
    xor al, al
    call w16_dcopen
    mov ax, W16_HDC
.out:
    ret

; ReleaseDC (USER.68) - args: +0 hDC, +2 hWnd
w16u_releasedc:
    call w16_dcclose
    mov ax, 1
    ret

; GetClientRect (USER.33) - args: +0 lpRect, +4 hWnd
w16u_clientrect:
    push di
    call w16_args
    les di, [es:bx]
    push bx
    mov bx, [w16_win]
    call OSAPI_WM_GEOM
    pop bx
    jnc .have
    xor cx, cx
    xor dx, dx
.have:
    call w16_nobar
    xor ax, ax
    mov [es:di], ax
    mov [es:di + 2], ax
    mov [es:di + 4], cx
    mov [es:di + 6], dx
    pop di
    ret

; =============================================================================
; DefWindowProc (USER.107) - args: as w16_smargs (SPEC.md 124.8's table)
; =============================================================================
w16u_defproc:
    call w16_args
    call w16_smargs
    mov cx, [w16_sm_msg]
    cmp cx, WM_CLOSE
    jne .notclose
    jmp w16_destroy
.notclose:
    cmp cx, WM_NCLBUTTONDOWN        ; the menu loop (SPEC.md 124.11)
    jne .notnc
    cmp word [w16_sm_wp], HTMENU
    jne .zero
    mov cx, [w16_sm_lp]             ; where it was pressed, on the screen
    mov [w16_mptx], cx
    mov cx, [w16_sm_lp + 2]
    mov [w16_mpty], cx
    call w16_hwnd2rec
    jc .zero
    xor dl, dl
    jmp w16_mtbegin
.notnc:
    cmp cx, WM_SYSKEYUP
    jne .notsku
    cmp word [w16_sm_wp], VK_F10    ; F10: SC_KEYMENU, no character
    jne .zero
    xor dx, dx
    jmp short .keymenu
.notsku:
    cmp cx, WM_SYSKEYDOWN
    jne .notskd
    cmp word [w16_sm_wp], VK_F4     ; Alt+F4: SC_CLOSE
    jne .zero
    test byte [w16_sm_lp + 3], 0x20
    jz .zero
    mov dx, SC_CLOSE
    jmp short .syscmd
.notskd:
    cmp cx, WM_SYSCHAR
    jne .notsc
    mov dx, [w16_sm_wp]             ; Alt+letter: SC_KEYMENU with it
    cmp dl, ' '
    je .zero
.keymenu:
    mov [w16_sm_lp], dx
    mov dx, SC_KEYMENU
.syscmd:
    mov word [w16_sm_lp + 2], 0
    mov [w16_sm_wp], dx
    mov word [w16_sm_msg], WM_SYSCOMMAND
    call w16_hwnd2rec
    jc .zero
    push si
    mov si, w16_cont_zero
    call w16_sendwnd
    pop si
    ret
.notsc:
    cmp cx, WM_ERASEBKGND
    jne .noterase
    call w16_hwnd2rec
    jc .zero
    call w16_erasebg
    mov ax, 1
    xor dx, dx
    ret
.noterase:
    cmp cx, WM_PAINT
    jne .notpaint
    call w16_hwnd2rec               ; BeginPaint and EndPaint, with the class
    jc .zero                        ;   brush as the erase
    test byte [bx + W16W_FLAGS], W16WF_ERASE
    jz .valid
    mov al, 1
    call w16_dcopen
    call w16_erasebg
    call w16_dcclose
.valid:
    and byte [bx + W16W_FLAGS], ~(W16WF_INVAL | W16WF_ERASE)
    jmp short .zero
.notpaint:
    cmp cx, WM_SYSCOMMAND
    jne .zero
    mov cx, [w16_sm_wp]
    and cx, 0xFFF0
    cmp cx, SC_KEYMENU
    jne .notkm
    call w16_hwnd2rec
    jc .zero
    mov dl, 1
    mov dh, [w16_sm_lp]
    jmp w16_mtbegin
.notkm:
    cmp cx, SC_CLOSE
    jne .zero
    mov word [w16_sm_msg], WM_CLOSE
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_post
.zero:
    xor ax, ax
    xor dx, dx
    ret

; w16_erasebg - BX = a window: fill the DC's clip with its class brush
w16_erasebg:
    push ax
    push bx
    push cx
    push dx
    mov ax, [bx + W16W_HBR]
    call w16_brcol
    cmp al, W16_NOCOL
    je .out
    mov [w16_gcol], al
    mov ax, [w16_cl]
    mov bx, [w16_ct]
    mov cx, [w16_cr]
    mov dx, [w16_cb]
    call w16_gfill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Timers (SPEC.md 124.8): one os8088 one-shot at the nearest deadline
; =============================================================================

; SetTimer (USER.10) - args: +0 lpTimerFunc, +4 uElapse, +6 nIDEvent, +8 hWnd
w16u_settimer:
    push si
    call w16_args
    mov ax, [es:bx]
    or ax, [es:bx + 2]
    jnz .fail                       ; a TimerProc: not until a program needs it
    mov ax, [es:bx + 8]
    mov dx, [es:bx + 6]
    mov si, w16_tmr                 ; the same timer again, or a free one
    mov cx, W16_MAXTMR
.find:
    cmp byte [si + W16T_USED], 0
    je .next
    cmp [si + W16T_HWND], ax
    jne .next
    cmp [si + W16T_ID], dx
    je .take
.next:
    add si, W16T_SIZE
    loop .find
    mov si, w16_tmr
    mov cx, W16_MAXTMR
.free:
    cmp byte [si + W16T_USED], 0
    je .take
    add si, W16T_SIZE
    loop .free
.fail:
    xor ax, ax
    jmp short .out
.take:
    mov [si + W16T_HWND], ax
    mov [si + W16T_ID], dx
    mov ax, [es:bx + 4]             ; ms * 182 / 10000 ticks, at least one
    mov cx, 182
    mul cx
    mov cx, 10000
    div cx
    or ax, ax
    jnz .per
    inc ax
.per:
    mov [si + W16T_PER], ax
    mov cx, ax
    call OSAPI_GET_TICKS
    add ax, cx
    mov [si + W16T_DUE], ax
    mov byte [si + W16T_USED], 1
    mov byte [si + W16T_FIRED], 0
    call w16_tarm
    mov ax, [si + W16T_ID]
    or ax, ax
    jnz .out
    inc ax
.out:
    pop si
    ret

; KillTimer (USER.12) - args: +0 nIDEvent, +2 hWnd
w16u_killtimer:
    push si
    call w16_args
    mov dx, [es:bx]
    mov ax, [es:bx + 2]
    mov si, w16_tmr
    mov cx, W16_MAXTMR
.find:
    cmp byte [si + W16T_USED], 0
    je .next
    cmp [si + W16T_HWND], ax
    jne .next
    cmp [si + W16T_ID], dx
    je .kill
.next:
    add si, W16T_SIZE
    loop .find
    xor ax, ax
    jmp short .out
.kill:
    mov byte [si + W16T_USED], 0
    call w16_tarm
    mov ax, 1
.out:
    pop si
    ret

; w16_tarm - arm the os8088 one-shot at the nearest deadline, or cancel it
w16_tarm:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_GET_TICKS
    mov dx, ax                      ; DX = now
    mov bx, 0xFFFF                  ; BX = the nearest, in ticks from now
    mov si, w16_tmr
    mov cx, W16_MAXTMR
.t:
    cmp byte [si + W16T_USED], 0
    je .n
    mov ax, [si + W16T_DUE]
    sub ax, dx
    jg .ahead
    mov ax, 1
.ahead:
    cmp ax, bx
    jae .n
    mov bx, ax
.n:
    add si, W16T_SIZE
    loop .t
    mov ax, bx
    inc bx
    jnz .arm
    xor ax, ax                      ; none: cancel
.arm:
    mov bx, [w16_win]
    call OSAPI_WM_TIMER
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_ontimer - OSAPI_WM_ONTIMER: SI = our window, the gfx lock held
w16_ontimer:
    push si
    call OSAPI_GET_TICKS
    mov dx, ax
    mov si, w16_tmr
    mov cx, W16_MAXTMR
.t:
    cmp byte [si + W16T_USED], 0
    je .n
    mov ax, dx
    sub ax, [si + W16T_DUE]
    js .n                           ; not yet
    mov byte [si + W16T_FIRED], 1
    mov ax, [si + W16T_PER]
    add [si + W16T_DUE], ax
    mov ax, dx
    sub ax, [si + W16T_DUE]
    js .n
    mov ax, dx                      ; more than a period behind: from now
    add ax, [si + W16T_PER]
    mov [si + W16T_DUE], ax
.n:
    add si, W16T_SIZE
    loop .t
    call w16_tarm
    pop si
    jmp w16_input_kick

; =============================================================================
; Input (SPEC.md 124.8's table): an os8088 callback becomes messages
; =============================================================================

; w16_mouse - AX = the message, DI = wParam, CX/DX = the screen point:
; post it to the top-level window in client coordinates
w16_mouse:
    push ax
    push bx
    push cx
    push dx
    mov [w16_mx], cx
    mov [w16_my], dx
    mov [w16_sm_msg], ax
    mov [w16_sm_wp], di
    call w16_toprec
    jc .out
    mov [w16_sm_hwnd], ax
    mov cx, [w16_mx]
    mov bx, [w16_win]
    push dx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx
    pop dx
    sub cx, ax
    sub dx, bx
    push ax
    call w16_mbarh                  ; the client is under the menu bar
    sub dx, ax
    pop ax
    mov [w16_sm_lp], cx
    mov [w16_sm_lp + 2], dx
    xor al, al
    call w16_post
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; W_ONCLICK / ONMOUSEUP / ONDRAG / ONRCLICK: CX, DX = the screen point,
; SI = our window, the gfx lock held. Each gives SI back.
; While the menu loop runs they are its (w16menu.inc); a press on the bar is
; the program's WM_NCLBUTTONDOWN, and that press's drags and release are the
; menu's too (SPEC.md 124.11).
w16_onclick:
    cmp byte [w16_mtrk], 0
    je .nt
    jmp w16_mclick
.nt:
    call w16_onbar
    jc w16_barpress
    push di
    mov ax, WM_LBUTTONDOWN
    mov di, MK_LBUTTON
    jmp short w16_mouse1
w16_onup:
    cmp byte [w16_mtrk], 0
    je .nt
    jmp w16_mup
.nt:
    cmp byte [w16_meat], 0
    je .mine
    mov byte [w16_meat], 0
    ret
.mine:
    push di
    mov ax, WM_LBUTTONUP
    xor di, di
    jmp short w16_mouse1
w16_ondrag:
    cmp byte [w16_mtrk], 0
    je .nt
    jmp w16_mdrag
.nt:
    cmp byte [w16_meat], 0
    jne .eat
    push di
    mov ax, WM_MOUSEMOVE
    mov di, MK_LBUTTON
    jmp short w16_mouse1
.eat:
    ret
w16_onrclick:
    cmp byte [w16_mtrk], 0
    jne .eat
    push di
    mov ax, WM_RBUTTONDOWN
    mov di, MK_RBUTTON
    call w16_mouse
    mov ax, WM_RBUTTONUP
    xor di, di
w16_mouse1:
    call w16_mouse
    pop di
    jmp short w16_input_kick
w16_onrclick.eat:
    ret

; w16_onbar - CX, DX = a screen point: CF=1 on the program's menu bar
w16_onbar:
    push ax
    push bx
    push dx
    cmp byte [w16_state], W16S_RUN
    jne .no
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT           ; DX = the content's top
    mov bx, dx
    pop dx
    push dx
    sub dx, bx
    jl .no
    call w16_mbarh
    dec ax
    cmp dx, ax
    jge .no
    pop dx
    pop bx
    pop ax
    stc
    ret
.no:
    pop dx
    pop bx
    pop ax
    clc
    ret

; w16_barpress - a press on the bar: WM_NCLBUTTONDOWN, HTMENU, the point on
; the screen - which the program's DefWindowProc turns into the menu loop
w16_barpress:
    mov byte [w16_meat], 1
    mov [w16_mptx], cx
    mov [w16_mpty], dx
    push ax
    call w16_toprec
    mov [w16_sm_hwnd], ax
    pop ax
    jc .out
    mov word [w16_sm_msg], WM_NCLBUTTONDOWN
    mov word [w16_sm_wp], HTMENU
    mov ax, [w16_mptx]
    mov [w16_sm_lp], ax
    mov ax, [w16_mpty]
    mov [w16_sm_lp + 2], ax
    xor al, al
    call w16_post
    jmp short w16_input_kick
.out:
    ret

; w16_input_kick - a callback under the gfx lock has queued something: run the
; program if it was waiting for it, and close up after it if it ended
w16_input_kick:
    push si
    push di
    mov byte [w16_locked], 1
    call w16_kick
    mov byte [w16_locked], 0
    call w16_after
    pop di
    pop si
    ret

; W_ONKEY - AL = ASCII, AH = the scan code, SI = our window, the lock held.
; The keyboard has no release edge here, so the key-up follows at once.
; While the menu loop runs the key is its (w16menu.inc). Otherwise F10, and a
; key with Alt held, are WM_SYSKEYDOWN / WM_SYSKEYUP with lParam's context bit,
; and an Alt+letter's WM_SYSCHAR is the letter (SPEC.md 124.11).
w16_onkey:
    cmp byte [w16_mtrk], 0
    je .nt
    jmp w16_mkey
.nt:
    push di
    push ax
    call w16_toprec
    mov [w16_sm_hwnd], ax
    pop ax
    jc .out
    mov bl, ah
    xor bh, bh
    cmp bl, W16_NSCAN
    jae .out
    mov [w16_kascii + bx], al       ; TranslateMessage's WM_CHAR, by scan
    push es
    xor cx, cx
    mov es, cx
    mov cl, [es:0x417]              ; the BIOS's shift state, as pressed
    pop es
    and cl, 0x0F
    mov [w16_kmods + bx], cl
    mov al, [w16_vktab + bx]
    or al, al
    jz .out
    xor ah, ah
    mov [w16_sm_wp], ax
    mov word [w16_sm_lp], 1         ; a repeat count of one, the scan code
    mov [w16_sm_lp + 2], bx
    mov dx, WM_KEYDOWN
    test cl, 8
    jz .noalt
    or byte [w16_sm_lp + 3], 0x20   ; the context bit: Alt is down
    mov dx, WM_SYSKEYDOWN
    cmp byte [w16_kascii + bx], 0
    jne .sys
    push ax                         ; Alt+letter: the ROM gave no character,
    cmp al, 'A'                     ;   WM_SYSCHAR's is the letter's own
    jb .nolet
    cmp al, 'Z'
    ja .nolet
    add al, 'a' - 'A'
    jmp short .let
.nolet:
    cmp al, '0'
    jb .nochar
    cmp al, '9'
    ja .nochar
.let:
    mov [w16_kascii + bx], al
.nochar:
    pop ax
    jmp short .sys
.noalt:
    cmp al, VK_F10
    jne .post
    mov dx, WM_SYSKEYDOWN
.sys:
.post:
    mov [w16_sm_msg], dx
    xor al, al
    call w16_post
    or word [w16_sm_lp + 2], 0xC000 ; previous state and transition
    inc word [w16_sm_msg]           ; WM_KEYUP / WM_SYSKEYUP
    xor al, al
    call w16_post
    pop di
    jmp w16_input_kick
.out:
    pop di
    ret

; SetCapture (USER.18) - args: +0 hWnd. Recorded; os8088 already tracks a
; press until its release (SPEC.md 13.7).
w16u_setcapture:
    call w16_args
    mov dx, [es:bx]
    mov ax, [w16_capture]
    mov [w16_capture], dx
    ret

; ReleaseCapture (USER.19)
w16u_relcapture:
    mov word [w16_capture], 0
    ret

; GetTickCount (USER.13), GetCurrentTime (USER.15): DX:AX = milliseconds
w16u_tickcount:
    ; fall into w16_ticksms

; w16_ticksms - DX:AX = the BIOS tick count x 55 (SPEC.md 124.8)
w16_ticksms:
    push bx
    push cx
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [es:0x46C]
    mov bx, [es:0x46E]
    popf
    mov cx, 55
    mul cx                          ; DX:AX = the low word's
    push ax
    push dx
    mov ax, bx
    mul cx
    pop dx
    add dx, ax                      ; ...plus the high word's, modulo 2^32
    pop ax
    pop es
    pop cx
    pop bx
    ret

; LoadCursor (USER.173), LoadIcon (USER.174) - a handle that is not NULL;
; the pointer stays os8088's and the icon is S12's
w16u_loadcursor:
    mov ax, 0x3300
    ret
w16u_loadicon:
    mov ax, 0x3301
    ret

; --- the scan code -> virtual key table (the US layout, scans 00h-58h) --------
w16_vktab:
    db 0x00, 0x1B, '1','2','3','4','5','6','7','8','9','0', 0xBD, 0xBB, 0x08, 0x09
    db 'Q','W','E','R','T','Y','U','I','O','P', 0xDB, 0xDD, 0x0D, 0x11, 'A','S'
    db 'D','F','G','H','J','K','L', 0xBA, 0xDE, 0xC0, 0x10, 0xDC, 'Z','X','C','V'
    db 'B','N','M', 0xBC, 0xBE, 0xBF, 0x10, 0x6A, 0x12, 0x20, 0x14, 0x70, 0x71, 0x72, 0x73, 0x74
    db 0x75, 0x76, 0x77, 0x78, 0x79, 0x90, 0x91, 0x24, 0x26, 0x21, 0x6D, 0x25, 0x0C, 0x27, 0x6B, 0x23
    db 0x28, 0x22, 0x2D, 0x2E, 0x00, 0x00, 0x00, 0x7A, 0x7B
%if ($ - w16_vktab) != W16_NSCAN
  %error "w16_vktab: one virtual key per scan code"
%endif

; =============================================================================
; The os8088 side: W_PAINT, the close box, and the lock
; =============================================================================

; w16_ppaint - W_PAINT once the program has a window (SPEC.md 124.8): the
; kernel's damage joins the invalid rectangle, erased, and a program waiting
; in GetMessage is resumed RIGHT HERE, under the lock and the damage clip, so
; its WM_PAINT lands in this pass. Damage the program has not painted when
; the resume comes back gets the class brush. SI = our window; gives SI back.
w16_ppaint:
    push si
    mov byte [w16_inwp], 1
    mov bx, [w16_win]
    call OSAPI_WM_DAMAGE            ; AX/BX/CX/DX, absolute and inclusive
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    mov [w16_dmg], ax
    mov [w16_dmg + 2], bx
    mov [w16_dmg + 4], cx
    mov [w16_dmg + 6], dx
    call w16_toprec
    jc .out
    push bx                         ; the program's window
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    push ax
    call w16_mbarh                  ; the client is under the menu bar
    add dx, ax
    mov [w16_ctop], dx
    pop ax
    mov bx, dx
    mov si, [w16_dmg + 6]           ; client, right and bottom exclusive
    sub si, bx
    inc si
    mov cx, [w16_dmg + 2]
    sub cx, bx
    mov dx, [w16_dmg + 4]
    sub dx, ax
    inc dx
    neg ax
    add ax, [w16_dmg]
    pop bx
    cmp si, 0
    jle .bar                        ; the damage is all bar
    mov byte [w16_erase], 1
    call w16_inval
    cmp byte [w16_mtrk], 0          ; the menu loop's banks are stale now
    je .kick
    mov byte [w16_mstale], 1
.kick:
    cmp byte [w16_inget], 0
    je .fill
    mov byte [w16_inpaint], 1
    mov byte [w16_locked], 1
    call w16_kick
    mov byte [w16_locked], 0
    mov byte [w16_inpaint], 0
    cmp byte [w16_state], W16S_ENDED
    jne .fill
    mov bx, [w16_win]               ; it ended: close up from the wake, not
    call OSAPI_WM_WAKE              ;   from inside a paint
    jmp short .out
.fill:
    call w16_toprec
    jc .out
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jz .bar
    mov ax, [bx + W16W_HBR]
    call w16_brcol
    cmp al, W16_NOCOL
    je .bar
    call w16_ink
    mov ax, [w16_dmg]
    mov bx, [w16_dmg + 2]
    cmp bx, [w16_ctop]              ; the client's part of the damage
    jge .t
    mov bx, [w16_ctop]
.t:
    mov cx, [w16_dmg + 4]
    mov dx, [w16_dmg + 6]
    cmp bx, dx
    jg .bar
    call OSAPI_GFX_FILL
.bar:
    mov ax, [w16_dmg + 2]           ; the bar, if the damage reaches it, and
    cmp ax, [w16_ctop]              ;   the menu loop's popups over it all
    jge .pops
    call w16_mbardraw
.pops:
    cmp byte [w16_mtrk], 0
    je .out
    call w16_mrepaint
.out:
    mov byte [w16_inwp], 0
    pop si
    ret

; w16_close - the close negotiator's half (SPEC.md 124.8). CF=0: let the
; close go ahead (no program window, or it ended on the WM_CLOSE we posted);
; CF=1 refuses, with the WM_CLOSE posted. The gfx lock is held.
w16_close:
    cmp byte [w16_state], W16S_RUN
    jne .go
    call w16_toprec
    jc .go
    cmp byte [w16_closeq], 0
    jne .endtask
    push si
    cmp byte [w16_mtrk], 0          ; a menu open: it closes first, and the
    je .post                        ;   program returns to its loop
    call w16_mhpre
    jc .post
    mov byte [w16_locked], 1        ; (the negotiator holds the lock)
    call w16_mstop
    call w16_mhpost
    cmp byte [w16_state], W16S_RUN
    jne .ended
.post:
    mov byte [w16_closeq], 1
    call w16_closemsg
.ended:
    pop si
    cmp byte [w16_state], W16S_ENDED
    jne .refuse
    call w16_freeprog
.go:
    clc
    ret
.refuse:
    stc
    ret
.endtask:
    push si                         ; it did not close on the last one: offer
    push di                         ;   to end it (SPEC.md 124.12)
    mov al, OS88UI_ADANGER
    mov bx, [w16_win]
    mov si, w16_s_endtask
    mov di, w16_etdone
    call os88ui_ask                 ; refused: an alert is up (MessageBox's),
    pop di                          ;   raised, to be answered first
    pop si
    stc
    ret

; w16_closemsg - post WM_CLOSE to the top-level window and run the program
; if it waits for it. The gfx lock is held. Clobbers AX, BX, CX, DX.
w16_closemsg:
    call w16_toprec
    jc .out
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_CLOSE
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_post
    mov byte [w16_locked], 1
    call w16_kick
    mov byte [w16_locked], 0
.out:
    ret

; w16_etdone - the End Task alert's answer, the gfx lock held. Proceed (1):
; the program ends where it is - its windows, its memory, WIN16's window.
; Cancel, or the alert dismissed: the program is asked again, WM_CLOSE.
w16_etdone:
    cmp byte [w16_state], W16S_RUN
    jne .no
    cmp al, 1
    jne .again
    mov byte [w16_state], W16S_ENDED
    mov byte [w16_endmsg], 0
    jmp w16_after
.again:
    call w16_closemsg
    jmp w16_after
.no:
    ret

w16_s_endtask: db 'It has not closed. End it?', 0

; w16_lock / w16_unlock - the gfx lock around a call that needs it, unless a
; callback or an open DC already holds it. Preserve every register.
w16_lock:
    cmp byte [w16_locked], 0
    jne .out
    cmp byte [w16_dctook], 0
    jne .out
    call OSAPI_GFX_LOCK
    mov byte [w16_ltook], 1
.out:
    ret

w16_unlock:
    cmp byte [w16_ltook], 0
    je .out
    mov byte [w16_ltook], 0
    call OSAPI_GFX_UNLOCK
.out:
    ret

; =============================================================================
; What the system answered (SPEC.md 124.8.2): GDICARD.EXE on Windows 3.0, VGA
; =============================================================================

; GetSystemMetrics (USER.179) - args: +0 nIndex. Windows 3.0's VGA answers,
; with the screen this window is on and the caption and menu bar the System
; font's (rows + 4 and rows + 2: VGA's 20 and 18).
SM_CXSCREEN      equ 0
SM_CYSCREEN      equ 1
SM_CYCAPTION     equ 4
SM_CYMENU        equ 15
SM_CXFULLSCREEN  equ 16
SM_CYFULLSCREEN  equ 17
W16_NSM          equ 36

w16u_sysmet:
    call w16_args
    mov bx, [es:bx]
    cmp bx, W16_NSM
    jae .zero
    cmp bx, SM_CXSCREEN
    je .w
    cmp bx, SM_CXFULLSCREEN
    je .w
    cmp bx, SM_CYSCREEN
    je .h
    cmp bx, SM_CYCAPTION
    je .cap
    cmp bx, SM_CYMENU
    je .menu
    cmp bx, SM_CYFULLSCREEN
    je .full
    shl bx, 1
    mov ax, [w16_smtab + bx]
    ret
.w:
    mov ax, [w16_scrw]
    ret
.h:
    mov ax, [w16_scrh]
    ret
.cap:
    mov ax, [w16_capn]
    inc ax
    ret
.menu:
    mov ax, [w16_capn]
    dec ax
    ret
.full:
    mov ax, [w16_scrh]
    sub ax, [w16_capn]
    dec ax
    ret
.zero:
    xor ax, ax
    ret

w16_smtab:                      ; SM_* 0-35, Windows 3.0 on VGA
    dw 640, 480, 17, 17, 20, 1, 1, 4, 4, 17, 17, 32
    dw 32, 32, 32, 18, 640, 460, 0, 1, 17, 17, 0, 0
    dw 0, 1, 0, 0, 102, 26, 18, 18, 4, 4, 102, 26

; GetSysColor (USER.180) - args: +0 nIndex. DX:AX = the COLORREF.
COLOR_BTNFACE   equ 15
W16_NSYSCOL     equ 19

w16u_syscolor:
    call w16_args
    mov bx, [es:bx]
    xor ax, ax
    xor dx, dx
    cmp bx, W16_NSYSCOL
    jae .out
    shl bx, 1
    shl bx, 1
    mov ax, [w16_sysrgb + bx]
    mov dx, [w16_sysrgb + bx + 2]
.out:
    ret

; w16_syscol - AL = a COLOR_* index (< W16_NSYSCOL) -> AL = the nearest of
; the sixteen. Keeps all but AL.
w16_syscol:
    push bx
    push dx
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    mov ax, [w16_sysrgb + bx]
    mov dl, [w16_sysrgb + bx + 2]
    call w16_rgb2col
    pop dx
    pop bx
    xor ah, ah
    ret

w16_sysrgb:                     ; COLOR_* 0-18, Windows 3.0's own defaults
    dd 0xE0E0E0, 0xC0C0C0, 0x804000, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x000000
    dd 0x000000, 0x000000, 0xFFFFFF, 0x808080, 0xFFFFFF, 0xE8FFFF, 0x000000
    dd 0xFFFFFF, 0xC0C0C0, 0x808080, 0xC0C0C0, 0x000000

; SetRect (USER.72) - args: +0 bottom, +2 right, +4 top, +6 left, +8 lpRect
w16u_setrect:
    call w16_args
    push ds
    push si
    push di
    mov si, bx
    push es
    pop ds
    les di, [si + 8]
    mov ax, [si + 6]
    stosw
    mov ax, [si + 4]
    stosw
    mov ax, [si + 2]
    stosw
    mov ax, [si]
    stosw
    pop di
    pop si
    pop ds
    ret

; PtInRect (USER.76) - args: +0 the POINT (x, then y), +4 lpRect
w16u_ptinrect:
    call w16_args
    mov cx, [es:bx]
    mov dx, [es:bx + 2]
    les bx, [es:bx + 4]
    xor ax, ax
    cmp cx, [es:bx]
    jl .out
    cmp cx, [es:bx + 4]
    jge .out
    cmp dx, [es:bx + 2]
    jl .out
    cmp dx, [es:bx + 6]
    jge .out
    inc ax
.out:
    ret
