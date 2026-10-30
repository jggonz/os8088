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
W16W_EXTRA  equ 20              ; cbWndExtra, at most W16_WEXTRA
W16_WEXTRA  equ 32              ;   (Windows' DLGWINDOWEXTRA is 30, and a
                                ;   dialog-class main window - CALC 3.00's -
                                ;   asks for exactly that)
W16W_MENU   equ 52              ; its HMENU, 0 = none (SPEC.md 124.11); a
                                ;   child's ID
W16W_PARENT equ 54              ; a child's parent; a top-level's owner
W16W_X      equ 56              ; a child's window rectangle, in its parent's
W16W_Y      equ 58              ;   client coordinates (SPEC.md 124.8)
W16W_CX     equ 60
W16W_CY     equ 62
W16W_OSW    equ 64              ; a top-level's os8088 window
W16W_TEXT   equ 66              ; its text, a block in the USER heap; 0 none
W16W_KIND   equ 68              ; W16Q_*: 0 the program's class, else WIN16's
W16W_F2     equ 69              ; W16W2_*
W16W_CS     equ 70              ; 18 bytes: a control's or a dialog's own
                                ;   state (w16dlg.inc)
W16W_SB     equ 88              ; 12: its scroll bars (SPEC.md 124.22.1),
W16W_SBH    equ W16W_SB         ;   SB_HORZ's position, minimum and maximum,
W16W_SBV    equ W16W_SB + 6     ;   then SB_VERT's
W16W_SEQ    equ 100             ; when it was made: siblings are in this
                                ;   order, Windows' Z order (w16dlg.inc)
%if W16W_SEQ + 2 != W16W_SIZE || W16W_EXTRA + W16_WEXTRA != W16W_MENU
  %error "the window record's layout and W16W_SIZE disagree"
%endif

W16WF_USED  equ 1
W16WF_INVAL equ 2
W16WF_ERASE equ 4
W16WF_VIS   equ 8
W16WF_DIS   equ 16              ; disabled (EnableWindow)
W16WF_DYING equ 32              ; DestroyWindow has it

W16W2_TTL   equ 7               ; a second top-level's title buffer, + 1
W16W2_NCDIRTY equ 16            ; its scroll bars are owed (SPEC.md 124.22.1)
W16W2_NSIZE equ 8               ; an overlapped window not yet sized: its
                                ;   first ShowWindow sends WM_SIZE (124.8)

W16Q_PROG   equ 0               ; the kinds: the program's own class ...
W16Q_BUTTON equ 1               ; ...and WIN16's (SPEC.md 124.13), whose
W16Q_EDIT   equ 2               ;   procedures are in the dialog part
W16Q_STATIC equ 3
W16Q_LIST   equ 4
W16Q_DIALOG equ 5               ; DefDlgProc, which calls the program's
W16Q_SCROLL equ 6               ; the SCROLLBAR control (SPEC.md 124.22.8)

; a dialog's own state, in its W16W_CS (w16dlg.inc); the program's DWL_*
; are its extra bytes, as on Windows
WD_RESULT   equ W16W_CS + 0     ; EndDialog's value
WD_FL       equ W16W_CS + 2     ; WDF_*
WD_FOCUS    equ W16W_CS + 4     ; the control that had its focus
WD_DEFID    equ W16W_CS + 6     ; DM_SETDEFID's
WD_WIDX     equ W16W_CS + 8     ; a modal one's waiting DialogBox: its row,
WD_WNARGB   equ W16W_CS + 10    ;   its argument bytes and its frame
WD_WPSP     equ W16W_CS + 12
WD_PREV     equ W16W_CS + 14    ; ...and the modal one it is inside, + 1
WD_OWNER    equ W16W_CS + 16    ; the owner it disabled, 0 none
WDF_ENDED   equ 1
WDF_MODAL   equ 2
DWL_MSGRESULT equ 0
DWL_DLGPROC equ 4
DWL_USER    equ 8

; the style's high word
WS_POPUP_HI    equ 0x8000
WS_VISIBLE_HI  equ 0x1000
WS_DISABLED_HI equ 0x0800
WS_CAPTION_HI  equ 0x00C0
WS_BORDER_HI   equ 0x0080
WS_DLGFRAME_HI equ 0x0040
WS_VSCROLL_HI  equ 0x0020
WS_HSCROLL_HI  equ 0x0010
WS_GROUP_HI    equ 0x0002
WS_TABSTOP_HI  equ 0x0001

; W16SMKEEP - at a routine's top: run the rest of it (its `.go`) with
; [w16_sm_*] kept on the stack and put back after, flags and registers as it
; leaves them. A message sent to a control while another is being handled
; (WM_KILLFOCUS from a press's SetFocus) must not take the handled one's
; place.
%macro W16SMKEEP 0
    push word [w16_sm_hwnd]
    push word [w16_sm_msg]
    push word [w16_sm_wp]
    push word [w16_sm_lp]
    push word [w16_sm_lp + 2]
    call .go
    pop word [w16_sm_lp + 2]
    pop word [w16_sm_lp]
    pop word [w16_sm_wp]
    pop word [w16_sm_msg]
    pop word [w16_sm_hwnd]
    ret
%endmacro

; --- a timer -------------------------------------------------------------------
W16T_USED   equ 0
W16T_FIRED  equ 1
W16T_HWND   equ 2
W16T_ID     equ 4
W16T_PER    equ 6               ; ticks
W16T_DUE    equ 8               ; OSAPI_GET_TICKS at which it fires
W16T_PROC   equ 10              ; its TimerProc, 0:0 for none (SPEC.md 124.8)

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
WM_ACTIVATE   equ 0x0006
WM_SETFOCUS   equ 0x0007
WM_KILLFOCUS  equ 0x0008
WM_ENABLE     equ 0x000A
WM_SETTEXT    equ 0x000C
WM_GETTEXT    equ 0x000D
WM_GETTEXTLENGTH equ 0x000E
WM_GETDLGCODE equ 0x0087
WM_INITDIALOG equ 0x0110
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
WS_CLIPCH_HI  equ 0x0200        ; WS_CLIPCHILDREN
WS_THICKFRAME_HI equ 0x0004
WS_MINIMIZE_HI equ 0x2000
SWP_NOSIZE    equ 0x0001
SWP_NOMOVE    equ 0x0002
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
    cmp word [es:si + 8], W16_WEXTRA    ; cbWndExtra
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
    mov ax, [es:bx + 26]
    mov dx, [es:bx + 28]
    call w16_farname
    jc .fail
    call w16_findcls                ; BX = the class
    jc .builtin
    mov byte [w16_cwkind], W16Q_PROG
    mov ax, bx
    sub ax, w16_cls
    mov dl, W16C_SIZE
    div dl
    mov [w16_cwcls], al
    mov ax, [bx + W16C_PROC]
    mov [w16_cwproc], ax
    mov ax, [bx + W16C_PROC + 2]
    mov [w16_cwproc + 2], ax
    mov ax, [bx + W16C_HBR]
    mov [w16_cwhbr], ax
    jmp short .args
.builtin:
    call w16_builtin                ; one of WIN16's own (SPEC.md 124.13)
    jc .fail
    mov [w16_cwkind], al
    mov byte [w16_cwcls], 0xFF
    call w16_ctlstub
    mov [w16_cwproc], ax
    mov [w16_cwproc + 2], dx
    mov word [w16_cwhbr], COLOR_WINDOW + 1
.args:
    mov es, [w16_pss]
    mov bx, [w16_cwargs]
    mov ax, [es:bx + 18]
    mov [w16_cwstyle], ax
    mov ax, [es:bx + 20]
    mov [w16_cwstyle + 2], ax       ; WS_MINIMIZE kept: ShowWindow restores
    mov ax, [es:bx + 22]
    mov [w16_cwtext], ax
    mov ax, [es:bx + 24]
    mov [w16_cwtext + 2], ax
    mov ax, [es:bx + 8]
    mov [w16_cwpar], ax
    mov ax, [es:bx + 6]
    mov [w16_cwid], ax
    mov ax, [es:bx + 16]
    mov [w16_cwx], ax
    mov ax, [es:bx + 14]
    mov [w16_cwy], ax
    mov ax, [es:bx + 12]
    mov [w16_cwcx], ax
    mov ax, [es:bx + 10]
    mov [w16_cwcy], ax
    test word [w16_cwstyle + 2], WS_CHILD_HI
    jnz .mk
    cmp byte [w16_top], 0           ; a second top-level: its client and
    je .mk                          ;   where it goes
    call w16_popclient
.mk:
    call w16_mkwnd                  ; BX = it, CX = its slot
    jc .fail
    cmp byte [bx + W16W_KIND], W16Q_PROG
    je .prog
    mov word [w16_sm_msg], WM_CREATE    ; one of WIN16's: created at once
    mov word [w16_sm_wp], 0
    mov ax, [w16_cwargs]
    mov [w16_sm_lp], ax
    mov ax, [w16_pss]
    mov [w16_sm_lp + 2], ax
    call w16_intproc
    and ax, dx
    inc ax
    jz .refused
    call w16_rec2hwnd
    xor dx, dx
    jmp .out
.refused:
    call w16_wndfree
.fail:
    xor ax, ax
    xor dx, dx
    jmp .out
.prog:
    mov [w16_cwslot], cx
    mov si, bx
    mov es, [w16_pss]
    mov bx, [w16_cwargs]
    test word [si + W16W_STYLE + 2], WS_CHILD_HI
    jnz .create
    mov ax, [es:bx + 6]             ; hMenu, else the class's menu by name
    or ax, ax                       ;   (SPEC.md 124.11)
    jnz .menu
    mov al, [si + W16W_CLS]
    mov ah, W16C_SIZE
    mul ah
    mov di, ax
    add di, w16_cls
    mov ax, [di + W16C_MENU]
    mov dx, [di + W16C_MENU + 2]
    mov cx, ax
    or cx, dx
    jz .menu
    LCALL w16_mload
    mov es, [w16_pss]
    mov bx, [w16_cwargs]
.menu:
    mov [si + W16W_MENU], ax
    mov ax, [si + W16W_OSW]
    cmp ax, [w16_win]
    jne .create
    mov cx, [es:bx + 12]            ; WIN16's window takes the size
    mov dx, [es:bx + 10]
    mov ax, [es:bx + 20]
    call w16_sizeto

.create:
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
    call w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_CREATE
    mov word [w16_sm_wp], 0
    call w16_sendgo
.out:
    pop di
    pop si
    ret

; w16_popclient - [w16_cwx]..[w16_cwcy] = a program's popup as CreateWindow
; gave it (its window, on the screen) -> its client, on the screen: less the
; frame (4 with WS_THICKFRAME, else 1 with a border) and the caption.
; CW_USEDEFAULT puts it below and right of WIN16's window, 320 by 200.
w16_popclient:
    push ax
    push bx
    push dx
    mov ax, CW_USEDEFAULT
    cmp [w16_cwx], ax
    jne .place
    push ax
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    add ax, 24
    mov [w16_cwx], ax
    add dx, 24
    mov [w16_cwy], dx
    pop ax
.place:
    cmp [w16_cwcx], ax
    jne .size
    mov word [w16_cwcx], 320
    mov word [w16_cwcy], 200
.size:
    xor bx, bx                      ; BX = the frame
    test word [w16_cwstyle + 2], WS_BORDER_HI | WS_DLGFRAME_HI | WS_THICKFRAME_HI
    jz .f
    inc bx
    test word [w16_cwstyle + 2], WS_THICKFRAME_HI
    jz .f
    mov bx, 4
.f:
    add [w16_cwx], bx
    add [w16_cwy], bx
    shl bx, 1
    sub [w16_cwcx], bx
    sub [w16_cwcy], bx
    mov ax, [w16_cwstyle + 2]
    and ax, WS_CAPTION_HI
    cmp ax, WS_CAPTION_HI
    jne .out
    mov ax, [w16_capn]
    add [w16_cwy], ax
    sub [w16_cwcy], ax
.out:
    pop dx
    pop bx
    pop ax
    ret

; w16_cw_done - WM_CREATE answered: -1 refuses the window, anything else
; is followed by WM_SIZE, as Windows' CreateWindow follows it
w16_cw_done:
    and ax, dx
    inc ax
    jz .refused
    call w16_slot2rec
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI | WS_POPUP_HI | WS_VISIBLE_HI
    jnz .now                        ; overlapped and hidden: the size waits
    or byte [bx + W16W_F2], W16W2_NSIZE ;   for its first ShowWindow
    jmp w16_cw_sized
.now:
    call w16_rec2hwnd
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

; w16_cw_sized - ...a second top-level with WS_VISIBLE is shown, and
; CreateWindow answers the HWND
w16_cw_sized:
    call w16_slot2rec
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .out
    test word [bx + W16W_STYLE + 2], WS_VISIBLE_HI
    jz .out
    or byte [bx + W16W_FLAGS], W16WF_VIS
    mov ax, [bx + W16W_OSW]
    cmp ax, [w16_win]
    je .out
    call w16_oswshow
.out:
    call w16_rec2hwnd
    xor dx, dx
    ret

; w16_sizeto - give WIN16's window the client a Windows window of CX by DX
; would have (SPEC.md 124.8): cx - 2 frame by cy - caption - 2 frame, the
; frame 4 when AX (the style's high word) has WS_THICKFRAME and 1 otherwise.
; The caption is SM_CYCAPTION less the row it shares with the frame: 19 on
; VGA, MEASURED on Windows 3.0 (PUMP.EXE: 320 x 180 is a 312 x 153 client),
; and the System font's rows + 3 on every display (w16_capn).
; CW_USEDEFAULT is three-quarters of the screen each way, Windows' default
; (SPEC.md 124.8). Clobbers AX, BX, CX, DX, SI, ES.
w16_sizeto:
    cmp cx, CW_USEDEFAULT
    jne .sized
    push ax
    mov ax, [w16_scrw]
    mov cx, ax
    shr ax, 1
    shr ax, 1
    sub cx, ax                      ; CX = 3/4 of the width
    mov ax, [w16_scrh]
    mov dx, ax
    shr ax, 1
    shr ax, 1
    sub dx, ax                      ; DX = 3/4 of the height
    pop ax
.sized:
    mov bx, 2
    test ax, WS_THICKFRAME_HI
    jz .thin
    mov bx, 8
.thin:
    sub cx, bx                      ; the client Windows would have given,
    jle .out
    add cx, 7                       ;   up to os8088's 8-pixel grid, so the
    and cx, 0xFFF8                  ;   snap never takes from it (SPEC.md
                                    ;   124.8)
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

; w16_sizemsg - [w16_sm_*] = WM_SIZE for the window BX (its HWND AX), with
; the client's size now
w16_sizemsg:
    push cx
    push dx
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_SIZE
    mov word [w16_sm_wp], 0         ; SIZE_RESTORED
    call w16_csize
    mov [w16_sm_lp], cx
    mov [w16_sm_lp + 2], dx
    pop dx
    pop cx
    ret

; w16_nobar - DX = the content's height -> the client's: the menu bar's off,
; never below 0 (SPEC.md 124.11). Keeps the rest.
w16_nobar:
    push ax
    LCALL w16_mbarh
    sub dx, ax
    jge .ok
    xor dx, dx
.ok:
    pop ax
    ret

; MoveWindow (USER.56) - args: +0 bRepaint, +2 cy, +4 cx, +6 y, +8 x, +10 hWnd
w16u_movewnd:
    call w16_args
    mov byte [w16_mvfl], 0          ; a move and a size
    mov ax, [es:bx + 8]
    mov [w16_mvx], ax
    mov ax, [es:bx + 6]
    mov [w16_mvy], ax
    mov cx, [es:bx + 4]
    mov dx, [es:bx + 2]
    mov ax, [es:bx + 10]
    jmp short w16_moveto

; SetWindowPos (USER.232) - args: +0 flags, +2 cy, +4 cx, +6 y, +8 x,
; +10 hWndInsertAfter, +12 hWnd. The Z order is os8088's.
w16u_setwndpos:
    call w16_args
    mov al, [es:bx]
    and al, SWP_NOSIZE | SWP_NOMOVE
    mov [w16_mvfl], al
    cmp al, SWP_NOSIZE | SWP_NOMOVE
    je .one
    mov ax, [es:bx + 8]
    mov [w16_mvx], ax
    mov ax, [es:bx + 6]
    mov [w16_mvy], ax
    mov cx, [es:bx + 4]
    mov dx, [es:bx + 2]
    mov ax, [es:bx + 12]
    jmp short w16_moveto
.one:
    mov ax, 1
    ret

; w16_moveto - AX = the HWND, CX by DX its new outer size and [w16_mvx]/
; [w16_mvy] its place, [w16_mvfl] = SWP_NOSIZE/SWP_NOMOVE: a child moves in
; its parent, WIN16's own window takes the size, and either is invalid and
; erased whole and hears WM_SIZE. Answers 1.
w16_moveto:
    push si
    mov [w16_mvcx], cx
    mov [w16_mvcy], dx
    mov [w16_mvhwnd], ax
    call w16_hwnd2rec               ; BX = the record, CX = the slot
    jc .no
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .child
    mov ax, [bx + W16W_OSW]
    cmp ax, [w16_win]
    jne .one                        ; a second top-level stays as it is made
    test byte [w16_mvfl], SWP_NOSIZE
    jnz .one
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .one                        ; ...and a minimized one moves its icon,
    push bx                         ;   keeping the size it restores to
    mov ax, [bx + W16W_STYLE + 2]
    mov cx, [w16_mvcx]
    mov dx, [w16_mvcy]
    call w16_sizeto
    pop bx
    jmp short .inval
.child:
    call w16_uncover                ; where it was
    test byte [w16_mvfl], SWP_NOMOVE
    jnz .cs
    mov ax, [w16_mvx]
    mov [bx + W16W_X], ax
    mov ax, [w16_mvy]
    mov [bx + W16W_Y], ax
.cs:
    test byte [w16_mvfl], SWP_NOSIZE
    jnz .inval
    mov ax, [w16_mvcx]
    mov [bx + W16W_CX], ax
    mov ax, [w16_mvcy]
    mov [bx + W16W_CY], ax
.inval:
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    call w16_invaltree
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

; w16_wndfree - BX = a window record: release it - its text, its menu, a
; second os8088 window it had, the focus, its timers and its capture - and
; the top-level window it was. Nothing is sent. Clobbers CX.
w16_wndfree:
    push ax
    push dx
    push si
    mov byte [bx + W16W_FLAGS], 0
    xor dx, dx
    call w16_settext                ; its text
    xor ax, ax                      ; its menu goes with it, as Windows'
    xchg ax, [bx + W16W_MENU]
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .nomenu                     ;   (a child's is its ID)
    or ax, ax
    jz .nomenu
    LCALL w16_mdestroy
.nomenu:
    xor ax, ax
    xchg ax, [bx + W16W_OSW]
    or ax, ax
    jz .noosw
    cmp ax, [w16_win]
    je .noosw
    call w16_ttlfree                ; a second os8088 window: destroyed
    push bx
    call w16_lock
    mov bx, ax
    call OSAPI_WM_DESTROY
    call w16_unlock
    pop bx
.noosw:
    call w16_rec2hwnd               ; AX = the HWND
    LCALL w16_cargone               ; the caret goes with it (124.22.9)
    cmp [w16_focus], ax
    jne .nofocus
    mov word [w16_focus], 0
.nofocus:
    push ax
    mov ax, bx
    sub ax, w16_wnd
    mov cl, W16W_SIZE
    div cl
    inc al                          ; the slot + 1
    cmp al, [w16_top]
    pop ax
    jne .timers
    mov byte [w16_top], 0           ; WIN16's window is its own again
    mov word [w16_titleptr], w16_name
    push ax
    push bx
    call w16_lock
    mov bx, [w16_win]
    xor al, al
    call OSAPI_WM_OWNBG
    call w16_unlock
    pop bx
    pop ax
.timers:
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
    pop dx
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
    or dx, dx                       ; SW_HIDE
    jnz .show
    call w16_uncover                ; a child: its parent repaints there
    and byte [bx + W16W_FLAGS], ~W16WF_VIS
    call w16_ownosw
    jc .done
    push bx
    call w16_lock
    mov bx, [bx + W16W_OSW]
    call OSAPI_WM_HIDE
    call w16_unlock
    pop bx
    jmp short .done
.show:
    or byte [bx + W16W_FLAGS], W16WF_VIS
    or di, di
    jnz .shown
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI | WS_MINIMIZE_HI
    jnz .noact
    call w16_postact                ; a top-level window: active (124.22.6)
.noact:
    push ax                         ; newly shown: all of it is invalid
    push cx
    push dx
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    call w16_invaltree
    pop dx
    pop cx
    pop ax
    call w16_ownosw                 ; a second top-level: shown at its
    jc .shown                       ;   client. WIN16's own keeps its size: a
    call w16_oswshow                ;   dialog was sized when made (w16d_ownwin)
.shown:
    test byte [bx + W16W_F2], W16W2_NSIZE
    jz .sized                       ; its first show: the WM_SIZE that
    and byte [bx + W16W_F2], ~W16W2_NSIZE ; CreateWindow kept back (124.8)
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .sized
    call w16_sizemsg
    mov si, w16_sw_done
    mov ax, di
    call w16_sendwnd
    jmp short .out
.sized:
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

; w16_ownosw - BX = a record: CF=0 it is a top-level window with an os8088
; window of its own (not WIN16's). Keeps every register.
w16_ownosw:
    push ax
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .no
    mov ax, [bx + W16W_OSW]
    or ax, ax
    jz .no
    cmp ax, [w16_win]
    je .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

w16_sw_done:
    mov ax, cx
    ret

; DestroyWindow (USER.53) - args: +0 hWnd
w16u_destroywnd:
    call w16_args
    mov ax, [es:bx]
    ; fall into w16_destroy

; w16_destroy - AX = the HWND: it, its children and the windows it owns go,
; each told WM_DESTROY first, parents before children (SPEC.md 124.8).
; Serves any import (DefWindowProc's WM_CLOSE reaches it too); answers 1,
; or 0 for no window.
w16_destroy:
    mov si, w16_cont_ret
    mov dx, 1
    ; fall into w16_destroyx

; -----------------------------------------------------------------------------
; w16_destroyx - as w16_destroy, and when the last is gone the continuation
; SI runs with AX = DX (the value it is handed). An import's handler.
; -----------------------------------------------------------------------------
w16_destroyx:
    push dx
    call w16_hwnd2rec
    pop dx
    jc .no
    call w16_uncover                ; a visible child: its parent repaints
    or byte [bx + W16W_FLAGS], W16WF_DYING
.mark:                              ; every window inside it, or owned by
    xor ah, ah                      ;   one that goes, goes too
    mov di, w16_wnd
.m:
    mov al, [di + W16W_FLAGS]
    and al, W16WF_USED | W16WF_DYING
    cmp al, W16WF_USED
    jne .mn
    push ax
    push dx
    mov ax, [di + W16W_PARENT]
    call w16_hwnd2rec
    pop dx
    pop ax
    jc .mn
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jz .mn
    or byte [di + W16W_FLAGS], W16WF_DYING
    inc ah
.mn:
    add di, W16W_SIZE
    cmp di, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .m
    or ah, ah
    jnz .mark
    jmp short w16_dwnext
.no:
    xor ax, ax
    xor dx, dx
    ret

; w16_dwnext - SI = the final continuation, DX = its value: the next window
; marked to go whose parent is not going too. One of WIN16's controls is told
; and freed at once; any other window is SENT WM_DESTROY and freed in its
; continuation (w16_dw_one), which comes back here.
w16_dwnext:
    mov bx, w16_wnd
.f:
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_DYING
    cmp al, W16WF_USED | W16WF_DYING
    jne .fn
    push bx
    push dx
    mov ax, [bx + W16W_PARENT]
    call w16_hwnd2rec
    pop dx
    jc .top
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jz .top
    pop bx
.fn:
    add bx, W16W_SIZE
    cmp bx, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .f
    mov ax, dx                      ; none left: the final answer
    xor dx, dx
    jmp si
.top:
    pop bx
    call w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_DESTROY
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_isctl
    jc .send
    push dx
    call w16_intproc                ; one of WIN16's: at once
    pop dx
    call w16_wndfree
    jmp short w16_dwnext
.send:
    push si
    push dx
    mov ax, bx
    sub ax, w16_wnd
    mov cl, W16W_SIZE
    div cl
    xor ah, ah                      ; AX = the slot
    mov si, w16_dw_one
    mov cx, 4
    call w16_cbopen                 ; ES:DI = the four bytes
    pop dx
    pop si
    mov [es:di], si                 ; the final continuation and its value
    mov [es:di + 2], dx
    jmp w16_sendgo

; w16_dw_one - WM_DESTROY answered: the window goes, and the next is sent
w16_dw_one:
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    mov si, [es:bx - 4]
    mov dx, [es:bx - 2]
    call w16_slot2rec
    call w16_wndfree
    jmp w16_dwnext

; w16_isctl - BX = a record: CF=0 it is one of WIN16's controls and its
; procedure is still WIN16's (it was not subclassed). Keeps every register.
w16_isctl:
    push ax
    push dx
    call w16_ctlstub
    cmp [bx + W16W_PROC], ax
    jne .no
    cmp [bx + W16W_PROC + 2], dx
    jne .no
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; =============================================================================
; Sending: a window procedure called through SPEC.md 124.2.2
; =============================================================================

; w16_sendwnd - BX = the window, SI = the continuation, AX = its word, and
; [w16_sm_*] the message: call the window's procedure
w16_sendwnd:
    call w16_isctl
    jnc .ctl
    push di
    push es
    xor cx, cx
    call w16_cbopen
    pop es
    pop di
    jmp short w16_sendgo
.ctl:
    push es                         ; one of WIN16's controls: its procedure
    push ax                         ;   at once, and the continuation with its
    call w16_intproc                ;   answer (SPEC.md 124.13)
    pop cx
    push ax
    mov ax, KERNEL_SEG
    mov es, ax
    pop ax
    call si
    pop es
    ret

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
    cmp word [w16_sm_msg], WM_TIMER ; a TimerProc's: it is called, with the
    jne .wnd                        ;   time, in the window's place
    mov ax, [w16_sm_lp]
    or ax, [w16_sm_lp + 2]
    jz .wnd
    push si
    mov ax, [w16_sm_lp]             ; the procedure, where w16_sendgo reads
    mov [w16_tproc], ax             ;   a window's
    mov ax, [w16_sm_lp + 2]
    mov [w16_tproc + 2], ax
    mov si, w16_cont_ret
    xor ax, ax
    xor cx, cx
    call w16_cbopen
    call w16_ticksms                ; ...and the time as its lParam
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], dx
    mov bx, w16_tproc - W16W_PROC
    pop si
    jmp w16_sendgo
.wnd:
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
    ; fall into w16_xlatemsg

; w16_xlatemsg - TranslateMessage on the MSG at ES:BX -> AX = 1 if a
; character was posted
w16_xlatemsg:
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
    jmp .gen
.paint:
    xor cx, cx                      ; 3. the scroll bars owed, drawn here
.nc:                                ;   (SPEC.md 124.22.1), then
    call w16_slot2rec
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_DYING
    cmp al, W16WF_USED
    jne .ncn
    test byte [bx + W16W_F2], W16W2_NCDIRTY
    jz .ncn
    call w16_pshown
    jc .ncn
    LCALL w16_ncpaint
.ncn:
    inc cx
    cmp cx, W16_MAXWND
    jb .nc
    xor cx, cx                      ;   WM_PAINT, a window that is invalid
.pw:                                ;   and shown, its parent before it
    call w16_slot2rec
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_INVAL | W16WF_DYING
    cmp al, W16WF_USED | W16WF_INVAL
    jne .pn
    call w16_pshown
    jnc .ispaint
.pn:
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
    mov ax, [si + W16T_PROC]        ; the TimerProc, which DispatchMessage
    mov [bx + MSG_LP], ax           ;   calls instead of the window's
    mov ax, [si + W16T_PROC + 2]
    mov [bx + MSG_LP + 2], ax
    cmp byte [w16_take], 0
    je .gen
    mov byte [si + W16T_FIRED], 0
    jmp short .gen
.ispaint:
    mov si, bx                      ; the highest invalid window it is in
.up:
    call w16_parent
    jc .top
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jz .up
    mov si, bx
    jmp short .up
.top:
    mov bx, si
    call w16_rec2hwnd
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

; w16_pshown - BX = a window: CF=0 it can be painted now - it and every
; window it is in visible, in an os8088 window, and inside W_PAINT the one
; being painted (SPEC.md 124.8). Keeps every register.
w16_pshown:
    push ax
    push bx
.up:
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .no
    call w16_parent
    jnc .up
    mov ax, [bx + W16W_OSW]
    or ax, ax
    jz .no
    cmp byte [w16_inpaint], 0
    je .yes
    cmp ax, [w16_pfosw]
    jne .no
.yes:
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
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
    je .modal
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
.modal:
    cmp byte [w16_inmodal], 0       ; ...or waits in a DialogBox (SPEC.md
    je .out                         ;   124.13 step 3)
    jmp w16_mkick

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
    call w16_invaltree              ; its children with it (SPEC.md 124.8)
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
    LCALL w16_carpre                ; the caret off while it paints (124.22.9)
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
    call w16_pkids
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

; w16_pkids - BX = a window being painted, [w16_prc] its rcPaint: its
; children under it invalid, unless it is WS_CLIPCHILDREN - they are drawn
; after it, so it cannot draw over them (SPEC.md 124.22.6). Keeps every
; register.
w16_pkids:
    test word [bx + W16W_STYLE + 2], WS_CLIPCH_HI
    jnz .ret
    push ax
    push cx
    push dx
    push si
    mov ax, [w16_prc]
    mov cx, [w16_prc + 2]
    mov dx, [w16_prc + 4]
    mov si, [w16_prc + 6]
    mov byte [w16_erase], 1
    call w16_invaltree
    pop si
    pop dx
    pop cx
    pop ax
.ret:
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
    LCALL w16_carpost
    mov ax, 1
    ret

; GetDC (USER.66) - args: +0 hWnd. The whole client.
w16u_getdc:
    call w16_args
    mov ax, [es:bx]
    or ax, ax                       ; NULL is the screen's (Solitaire asks
    jz .desk                        ;   GetDeviceCaps of it, SPEC.md 124.20.4)
    cmp ax, W16_HDESK               ; the desktop's: read, never drawn on
    jne .wnd                        ;   (SPEC.md 124.15.3)
.desk:
    mov ax, W16_HDESKDC
    ret
.wnd:
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
    call w16_args
    cmp word [es:bx], W16_HDC       ; the desktop's DC holds nothing
    jne .out
    call w16_dcclose
.out:
    mov ax, 1
    ret

; GetClientRect (USER.33) - args: +0 lpRect, +4 hWnd
w16u_clientrect:
    push di
    call w16_args
    mov ax, [es:bx + 4]
    call w16_hwnd0
    mov cx, 0
    mov dx, cx
    jc .have
    call w16_csize                  ; CX/DX
.have:
    call w16_args
    les di, [es:bx]
    xor ax, ax
    mov [es:di], ax
    mov [es:di + 2], ax
    mov [es:di + 4], cx
    mov [es:di + 6], dx
    pop di
    ret

; w16_syskey - [w16_sm_*] = WM_SYSKEYDOWN, WM_SYSKEYUP or WM_SYSCHAR: made
; the WM_SYSCOMMAND DefWindowProc makes of it, in [w16_sm_*] - Alt+letter
; SC_KEYMENU with the letter, F10 SC_KEYMENU with none, Alt+F4 SC_CLOSE. CF=1:
; the key makes none. Keeps the rest.
w16_syskey:
    push dx
    mov dx, [w16_sm_msg]
    cmp dx, WM_SYSKEYUP
    jne .notsku
    cmp word [w16_sm_wp], VK_F10    ; F10: SC_KEYMENU, no character
    jne .no
    xor dx, dx
    jmp short .keymenu
.notsku:
    cmp dx, WM_SYSKEYDOWN
    jne .notskd
    cmp word [w16_sm_wp], VK_F4     ; Alt+F4: SC_CLOSE
    jne .no
    test byte [w16_sm_lp + 3], 0x20
    jz .no
    mov dx, SC_CLOSE
    jmp short .syscmd
.notskd:
    cmp dx, WM_SYSCHAR
    jne .no
    mov dx, [w16_sm_wp]             ; Alt+letter: SC_KEYMENU with it
    cmp dl, ' '
    je .no
.keymenu:
    mov [w16_sm_lp], dx
    mov dx, SC_KEYMENU
.syscmd:
    mov word [w16_sm_lp + 2], 0
    mov [w16_sm_wp], dx
    mov word [w16_sm_msg], WM_SYSCOMMAND
    pop dx
    clc
    ret
.no:
    pop dx
    stc
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
    LCALL w16_mtbegin
    ret
.notnc:
    cmp cx, WM_SYSKEYDOWN
    jb .notsc
    cmp cx, WM_SYSCHAR
    ja .notsc
    call w16_syskey
    jc .zero
    call w16_hwnd2rec
    jc .zero
    push si
    mov si, w16_cont_zero
    call w16_sendwnd
    pop si
    ret
.notsc:
    cmp cx, WM_ACTIVATE             ; activated: the focus to it, unless it
    jne .notact                     ;   is in it already (SPEC.md 124.22.6)
    cmp word [w16_sm_wp], 0
    je .actz
    mov dx, ax
    mov ax, [w16_focus]
.inside:
    or ax, ax
    jz .take
    cmp ax, dx
    je .actz
    call w16_hwnd2rec
    jc .take
    mov ax, [bx + W16W_PARENT]
    jmp short .inside
.take:
    mov ax, dx
    call w16_setfocus
.actz:
    jmp .zero
.notact:
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
    push ax
    call w16_rec2hwnd
    LCALL w16_carpre
    pop ax
    mov al, 1
    call w16_dcopen
    call w16_pkids
    call w16_erasebg
    call w16_dcclose
    LCALL w16_carpost
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
    LCALL w16_mtbegin
    ret
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
    mov ax, [es:bx + 8]
    mov dx, [es:bx + 6]
    or ax, ax                       ; no window: an identifier of its own,
    jnz .wnd                        ;   past every slot's (SPEC.md 124.8)
    mov si, w16_tmr
    mov cx, W16_MAXTMR
.nfree:
    cmp byte [si + W16T_USED], 0
    je .nslot
    add si, W16T_SIZE
    loop .nfree
    jmp short .fail
.nslot:
    mov dx, W16_MAXTMR + 0x7F00
    sub dx, cx                      ; 7F00h + the slot
    jmp short .take
.wnd:
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
    mov ax, [es:bx]                 ; the TimerProc, 0:0 for none
    mov [si + W16T_PROC], ax
    mov ax, [es:bx + 2]
    mov [si + W16T_PROC + 2], ax
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
    mov ax, 0xFFFF                  ; the sound queue's next note (124.15.1):
    cmp word [w16_dseg], 0          ;   none unless the library part, which
    je .nosnd                       ;   plays it, is here
    LCALL w16s_wait
.nosnd:
    cmp ax, bx
    jae .snd
    mov bx, ax
.snd:
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
    cmp word [w16_dseg], 0          ; the sound queue (SPEC.md 124.15.1),
    je .nosnd                       ;   the library part's
    LCALL w16s_tick
.nosnd:
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
    push si
    mov [w16_mx], cx
    mov [w16_my], dx
    mov [w16_sm_msg], ax
    mov [w16_sm_wp], di
    cmp word [w16_sbtrk], 0         ; a scroll bar's press: all of it the
    je .nsb                         ;   bar's (SPEC.md 124.22.1)
    LCALL w16_sbmouse
    jmp short .out
.nsb:
    mov ax, [w16_capture]           ; the capture's, while there is one
    or ax, ax
    jz .hit
    call w16_hwnd2rec
    jnc .have
.hit:
    call w16_osw2rec                ; the window under it, in SI's
    jc .out
    mov cx, [w16_mx]
    mov dx, [w16_my]
    call w16_hittest
.dis:
    call w16_disabled               ; a disabled control's goes to the
    jnc .have                       ;   window it is in; a disabled
    call w16_parent                 ;   top-level's nowhere
    jnc .dis
    cmp word [w16_sm_msg], WM_LBUTTONDOWN
    jne .out
    call w16_modfront
    jmp short .out
.have:
    cmp word [w16_sm_msg], WM_LBUTTONDOWN
    jne .nbar
    cmp byte [bx + W16W_KIND], W16Q_SCROLL
    je .sbar
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI | WS_HSCROLL_HI
    jz .nbar
.sbar:
    LCALL w16_sbhit                 ; CF=0: it was on a bar, and is the bar's
    jnc .out
.nbar:
    call w16_origin                 ; AX/DX = its client's top left
    jc .out
    mov cx, [w16_mx]
    sub cx, ax
    mov [w16_sm_lp], cx
    mov cx, [w16_my]
    sub cx, dx
    mov [w16_sm_lp + 2], cx
    call w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    xor al, al
    call w16_post
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_modfront - the innermost modal dialog in front: what a press on the
; window it disabled gets. The gfx lock is held. Keeps every register.
w16_modfront:
    push bx
    push cx
    mov cl, [w16_inmodal]
    xor ch, ch
    jcxz .out
    dec cx
    call w16_slot2rec
    push word [w16_locked]          ; held already, or held by us: as it was
    mov byte [w16_locked], 1
    call w16_frontwnd
    pop word [w16_locked]
.out:
    pop cx
    pop bx
    ret

; W_ONCLICK / ONMOUSEUP / ONDRAG / ONRCLICK: CX, DX = the screen point,
; SI = our window, the gfx lock held. Each gives SI back.
; While the menu loop runs they are its (w16menu.inc); a press on the bar is
; the program's WM_NCLBUTTONDOWN, and that press's drags and release are the
; menu's too (SPEC.md 124.11).
w16_onclick:
    cmp byte [w16_mtrk], 0
    je .nt
    LCALL w16_mclick
    ret
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
    LCALL w16_mup
    ret
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
    LCALL w16_mdrag
    ret
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
    cmp si, [w16_win]               ; the bar is WIN16's own window's
    jne .no
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT           ; DX = the content's top
    mov bx, dx
    pop dx
    push dx
    sub dx, bx
    jl .no
    LCALL w16_mbarh
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
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jnz .front                      ; a modal dialog has it disabled
    mov word [w16_sm_msg], WM_NCLBUTTONDOWN
    mov word [w16_sm_wp], HTMENU
    mov ax, [w16_mptx]
    mov [w16_sm_lp], ax
    mov ax, [w16_mpty]
    mov [w16_sm_lp + 2], ax
    xor al, al
    call w16_post
    jmp short w16_input_kick
.front:
    jmp w16_modfront
.out:
    ret

; w16_input_kick - a callback under the gfx lock has queued something: run the
; program if it was waiting for it, and close up after it if it ended
w16_input_kick:
    push si
    push di
    push word [w16_locked]          ; held already, or held by us: as it was
    mov byte [w16_locked], 1
    call w16_kick
    call w16_after                  ; (still marked held: its clean-up draws)
    pop word [w16_locked]
    pop di
    pop si
    ret

; W_ONKEY - AL = ASCII, AH = the scan code, SI = our window, the lock held.
; The keyboard has no release edge here, so the key-up follows at once.
; While the menu loop runs the key is its (w16menu.inc). Otherwise F10, and a
; key with Alt held, are WM_SYSKEYDOWN / WM_SYSKEYUP with lParam's context bit,
; and an Alt+letter's WM_SYSCHAR is the letter (SPEC.md 124.11).
w16_onkey:
    cmp ah, W16_NSCAN               ; the ROM's own codes for a shifted,
    jb .base                        ;   Ctrl or Alt key folded back to the
    cmp ah, W16_SCANX               ;   key's: the modifier is the shift
    jae .base                       ;   state's (124.22.6)
    push bx
    mov bl, ah
    xor bh, bh
    mov ah, [w16_scanx + bx - W16_NSCAN]
    pop bx
.base:
    cmp byte [w16_mtrk], 0
    je .nt
    LCALL w16_mkey
    ret
.nt:
    push di
    push ax
    call w16_keywnd                 ; who gets it
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

; w16_keywnd - SI = the os8088 window a key came to -> AX = the window it is
; for: the focus when the focus is in that os8088 window, else the
; top-level window there. CF=1 none, or that window is disabled (a modal
; dialog has it), and the dialog is brought to the front. Clobbers BX, CX.
w16_keywnd:
    mov ax, [w16_focus]
    or ax, ax
    jz .top
    call w16_hwnd2rec
    jc .top
    push bx
    call w16_toplevel
    mov ax, [bx + W16W_OSW]
    pop bx
    cmp ax, si
    jne .top
    call w16_disabled
    jc .top
    jmp w16_rec2hwnd                ; (CF=0)
.top:
    call w16_osw2rec                ; BX, AX
    jc .out
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jz .out
    call w16_modfront
    stc
.out:
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

; LoadCursor (USER.173) - a handle that is not NULL: the pointer stays
; os8088's. LoadIcon is w16ico.inc's (SPEC.md 124.18.4)
w16u_loadcursor:
    mov ax, 0x3300
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

; w16_scanx - the scan code the ROM gives a key with Shift, Ctrl or Alt held
; (59h on, to W16_SCANX) -> the key's own: Shift/Ctrl/Alt+F1-F10, Ctrl+the
; cursor keys, Alt+1-=, F11/F12 with each, an enhanced keyboard's Ctrl+Up/
; Down/Ins/Del and Alt+the cursor keys. 0FFh: not a key (dropped)
w16_scanx:
    db 0x40, 0x41, 0x42, 0x43, 0x44, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41
    db 0x42, 0x43, 0x44, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43
    db 0x44, 0xFF, 0x4B, 0x4D, 0x4F, 0x51, 0x47, 0x02, 0x03, 0x04, 0x05, 0x06
    db 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x49, 0x57, 0x58, 0x57, 0x58
    db 0x57, 0x58, 0x57, 0x58, 0x48, 0x4A, 0x4C, 0x4E, 0x50, 0x52, 0x53, 0x0F
    db 0x35, 0x37, 0x47, 0x48, 0x49, 0xFF, 0x4B, 0xFF, 0x4D, 0xFF, 0x4F, 0x50
    db 0x51, 0x52, 0x53
W16_SCANX equ 0xA4
%if ($ - w16_scanx) != W16_SCANX - W16_NSCAN
  %error "w16_scanx: one key per ROM code"
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
    mov [w16_pfosw], si
    mov bx, si
    call OSAPI_WM_DAMAGE            ; AX/BX/CX/DX, absolute and inclusive
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    mov [w16_dmg], ax
    mov [w16_dmg + 2], bx
    mov [w16_dmg + 4], cx
    mov [w16_dmg + 6], dx
    call w16_osw2rec
    jc .out
    push bx                         ; the program's window in it
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    cmp si, [w16_win]
    jne .nobar
    push ax
    LCALL w16_mbarh                  ; the client is under the menu bar
    add dx, ax
    pop ax
.nobar:
    mov [w16_ctop], dx
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
    call w16_ncmark                 ; its scroll bars too (SPEC.md 124.22.1)
    call w16_invaltree              ; its children with it
    cmp byte [w16_mtrk], 0          ; the menu loop's banks are stale now
    je .kick
    mov byte [w16_mstale], 1
.kick:
    mov byte [w16_inpaint], 1
    push word [w16_locked]          ; held already, or held by us: as it was
    mov byte [w16_locked], 1
    call w16_kick
    pop word [w16_locked]
    mov byte [w16_inpaint], 0
    cmp byte [w16_state], W16S_ENDED
    jne .fill
    mov bx, [w16_win]               ; it ended: close up from the wake, not
    call OSAPI_WM_WAKE              ;   from inside a paint
    jmp short .out
.fill:
    mov si, [w16_pfosw]
    call w16_osw2rec
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
    mov si, [w16_pfosw]
    cmp si, [w16_win]
    jne .out
    mov ax, [w16_dmg + 2]           ; the bar, if the damage reaches it, and
    cmp ax, [w16_ctop]              ;   the menu loop's popups over it all
    jge .pops
    LCALL w16_mbardraw
.pops:
    cmp byte [w16_mtrk], 0
    je .out
    LCALL w16_mrepaint
.out:
    mov byte [w16_inwp], 0
    pop si
    ret

; w16_postact - BX = a program's top-level window: WM_ACTIVATE (WA_ACTIVE)
; posted to it, as Windows sent it when the window was activated (SPEC.md
; 124.22.6). Keeps every register.
w16_postact:
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .ret
    push ax
    call w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_ACTIVATE
    mov word [w16_sm_wp], 1
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_post
    pop ax
.ret:
    ret

; w16_close - the close negotiator's half (SPEC.md 124.8). CF=0: let the
; close go ahead (no program window, or it ended on the WM_CLOSE we posted);
; CF=1 refuses, with the WM_CLOSE posted. The gfx lock is held.
w16_close:
    cmp byte [w16_state], W16S_RUN
    jne .go
    cmp si, [w16_win]
    je .main
    push si                         ; a second window: the program is asked,
    call w16_osw2rec                ;   WM_CLOSE (a dialog's is Cancel), and
    jc .sgone                       ;   the window goes when it destroys it
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_CLOSE
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_post
    call w16_kick
    pop si
    cmp byte [w16_state], W16S_ENDED
    jne .sref
    call w16_after                  ; it ended: WIN16's window goes with it
.sref:
    stc
    ret
.sgone:
    pop si
    clc
    ret
.main:
    call w16_toprec
    jc .go
    cmp byte [w16_closeq], 0
    jne .endtask
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jz .ask
    mov byte [w16_closeq], 1        ; a modal dialog has it: the dialog comes
    call w16_modfront               ;   forward, and the next close offers to
    stc                             ;   end the program
    ret
.ask:
    push si
    cmp byte [w16_mtrk], 0          ; a menu open: it closes first, and the
    je .post                        ;   program returns to its loop
    LCALL w16_mhpre
    jc .post
    mov byte [w16_locked], 1        ; (the negotiator holds the lock)
    LCALL w16_mstop
    LCALL w16_mhpost
    cmp byte [w16_state], W16S_RUN
    jne .ended
.post:
    mov byte [w16_closeq], 1
    call w16_closemsg
.ended:
    pop si
    cmp byte [w16_state], W16S_ENDED
    jne .refuse
    mov byte [w16_locked], 1        ; (w16_mhpost cleared it; the kernel's
    call w16_freeprog               ;   lock is still held, w16_onclose)
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
    push word [w16_locked]          ; held already, or held by us: as it was
    mov byte [w16_locked], 1
    call w16_kick
    pop word [w16_locked]
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
    jmp w16_afterl
.again:
    call w16_closemsg
    jmp w16_afterl
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
; w16_sm - BX = an SM_* index -> AX = its metric (AdjustWindowRect's too,
; w16draw.inc). Clobbers BX.
w16_sm:
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
COLOR_WINDOW    equ 5
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
