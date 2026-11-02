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

; =============================================================================
; A program's own libraries, while it runs (SPEC.md 124.23.4-124.23.5)
; =============================================================================

; LoadLibrary (KERNEL.95) - args: +0 lpLibFileName. The file part, its folder
; ignored: WIN16's own module, a library already loaded, or one loaded now
; from the program's folder with its LibMain called. hInstance, or 2 (no
; such file) or 11 (refused); 0 if its LibMain answers 0 (w16k_llback)
W16LIB w16k_loadlib
    push si
    push di
    ICALL w16_args
    les di, [es:bx]
    mov si, di                      ; the file part: past the last \ / or :
.scan:
    mov al, [es:si]
    or al, al
    jz .scanned
    inc si
    cmp al, '\'
    je .cut
    cmp al, '/'
    je .cut
    cmp al, ':'
    jne .scan
.cut:
    mov di, si
    jmp short .scan
.scanned:
    mov si, w16_modnames            ; one of WIN16's own
    xor dx, dx
.mod:
    mov cl, [si]
    or cl, cl
    jz .notours
    inc si
    xor ch, ch
    call w16_nmeq
    jnc .ours
    add si, cx
    inc dl
    jmp short .mod
.ours:
    mov ax, W16_HMOD
    add al, dl
    jmp .out
.notours:
    mov si, w16_lname               ; up to 8 of the stem, then its extension
    xor cx, cx                      ;   or .DLL
.st:
    mov al, [es:di]
    or al, al
    jz .noext
    cmp al, '.'
    je .ext
    cmp cx, 8
    jae .skp
    call w16l_up
    mov [si], al
    inc si
    inc cx
.skp:
    inc di
    jmp short .st
.ext:
    mov byte [si], '.'
    inc si
    inc di
    xor cx, cx
.e2:
    mov al, [es:di]
    or al, al
    jz .term
    cmp cx, 3
    jae .term
    call w16l_up
    mov [si], al
    inc si
    inc di
    inc cx
    jmp short .e2
.noext:
    mov word [si], '.D'
    mov word [si + 2], 'LL'
    add si, 4
.term:
    mov byte [si], 0
    ICALL w16_libfind               ; AL = the record, its count raised
    jnc .found
    ICALL w16_ldlibin               ; AL = the record or 0FFh; CF=1 refused
    jc .refused
    cmp al, 0xFF
    je .nofile
    mov [w16_lltgt], al             ; its LibMain, and those of every library
    call w16k_llrun                 ;   it brought in (w16k_llrun)
    jmp short .out
.found:
    xor ah, ah
    call w16l_recp
    mov ax, [bx + W16L_HINST]
    jmp short .out
.refused:
    mov si, w16_name                ; WIN16's own lines, as they were
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_s_running
    mov di, w16_l2
    ICALL w16_strcpy
    mov ax, 11
    jmp short .out
.nofile:
    mov ax, 2
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16k_llrun - LoadLibrary's LibMains: every library the load brought in
; that owes one, the last loaded first - the libraries [w16_lltgt] imports,
; which Windows initialised before it - and then [w16_lltgt]'s own, whose
; continuation answers; or its hInstance when it owes none. Before, only
; the library asked for was called, and Microsoft's setup engine looped on
; LoadString(NULL): MSCOMSTF's LibMain, which keeps its hInstance, never ran
; (SPEC.md 124.23.5)
w16k_llrun:
    push bx
    mov ax, W16_NLIB
.f:
    dec ax
    js .tgt
    cmp al, [w16_lltgt]
    je .f
    call w16l_recp
    cmp byte [bx + W16L_COUNT], 0
    je .f
    cmp byte [bx + W16L_OWED], 0
    je .f
    mov si, w16k_llnext_c
    jmp short .call
.tgt:
    mov al, [w16_lltgt]
    xor ah, ah
    call w16l_recp
    cmp byte [bx + W16L_OWED], 0
    je .done
    mov si, w16k_llback_c           ; ...then w16k_llback answers
.call:
    mov byte [bx + W16L_OWED], 0
    call w16l_lmcall
    pop bx
    ret
.done:
    mov ax, [bx + W16L_HINST]
    xor dx, dx
    pop bx
    ret

; ...an imported library's LibMain has answered: the next
LCONT w16k_llnext
    jmp w16k_llrun

; LoadLibrary's continuation: AX = LibMain's answer, CX = the record
LCONT w16k_llback
    push ax
    mov ax, cx
    call w16l_recp
    pop ax
    or ax, ax
    jz .fail
    mov ax, [bx + W16L_HINST]
    xor dx, dx
    ret
.fail:
    mov ax, cx
    call w16l_drop
    xor ax, ax
    cwd
    ret

; FreeLibrary (KERNEL.96) - args: +0 hLibModule. The count lowered; at 0
; its WEP(WEP_FREE_DLL) called, then its blocks freed (w16k_flback)
W16LIB w16k_freelib
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx]
    ICALL w16_libof                 ; SI -> it, BX = its record
    jc .out
    dec byte [si + W16L_COUNT]
    jnz .out
    inc byte [si + W16L_COUNT]      ; alive while its WEP runs
    W16CSEG es
    mov di, w16_s_wep               ; XSEC-OK: ES = CS
    mov cl, 3
    ICALL w16_libproc               ; BL = the record -> DX:AX
    jc .nowep
    push dx
    push ax
    mov ax, bx
    mov si, w16k_flback_c
    xor cx, cx
    ICALL w16_cbopen
    xor ax, ax                      ; WEP_FREE_DLL
    ICALL w16_ppush
    pop ax
    pop dx
    ICALL w16_cbcall
    jmp short .out
.nowep:
    mov ax, bx
    call w16l_drop
.out:
    pop di
    pop si
    ret
w16_s_wep: db 'WEP'

LCONT w16k_flback
    mov ax, cx
    jmp w16l_drop

; GetModuleUsage (KERNEL.48) - args: +0 hModule: a library's count, else 1
W16LIB w16k_getmodu
    push si
    ICALL w16_args
    mov ax, [es:bx]
    ICALL w16_libof
    mov ax, 1
    jc .out
    mov al, [si + W16L_COUNT]
    xor ah, ah
.out:
    pop si
    ret

; =============================================================================
; What Recorder and RECORDER.DLL call (SPEC.md 124.23.8)
; =============================================================================

; GetCapture (USER.236): the window SetCapture named, 0 none
W16LIB w16u_getcapture
    mov ax, [w16_capture]
    ret

; GetDoubleClickTime (USER.21): Windows' default, 500 ms
W16LIB w16u_dblclktime
    mov ax, 500
    ret

; GetKeyboardState (USER.222) - args: +0 lpKeyState: 256 bytes, each key's
; state as GetKeyState sees it - Shift, Ctrl and Alt down (80h) off the
; BIOS's flags, and the three locks' toggles (bit 0). Every other key is up
W16LIB w16u_getkbstate
    push di
    ICALL w16_args
    les di, [es:bx]
    push di
    mov cx, 128
    xor ax, ax
    cld
    rep stosw
    pop di
    push ds
    mov ax, 0x40
    mov ds, ax
    mov al, [0x17]
    pop ds
    test al, 3
    jz .s
    mov byte [es:di + 0x10], 0x80   ; VK_SHIFT
.s:
    test al, 4
    jz .c
    mov byte [es:di + 0x11], 0x80   ; VK_CONTROL
.c:
    test al, 8
    jz .a
    mov byte [es:di + 0x12], 0x80   ; VK_MENU
.a:
    test al, 0x40
    jz .cap
    mov byte [es:di + 0x14], 1      ; VK_CAPITAL
.cap:
    test al, 0x20
    jz .num
    mov byte [es:di + 0x90], 1      ; VK_NUMLOCK
.num:
    test al, 0x10
    jz .out
    mov byte [es:di + 0x91], 1      ; VK_SCROLL
.out:
    pop di
    ret

; SetKeyboardState (USER.223): kept by nothing - the keyboard's state is the
; BIOS's, and a program cannot set it here
W16LIB w16u_setkbstate
    ret

; GetClassName (USER.58) - args: +0 nMaxCount, +2 lpClassName, +6 hWnd. A
; class the program registered answers its name as WIN16 keeps it, upper
; case; WIN16's own classes their Windows names. AX = the length
W16LIB w16u_getclsname
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 6]
    les di, [es:bx + 2]
    ICALL w16_hwnd2rec
    jc .zero
    jcxz .zero
    dec cx                          ; room for the NUL
    mov al, [bx + W16W_KIND]
    or al, al
    jnz .ours
    mov al, [bx + W16W_CLS]
    mov ah, W16C_SIZE
    mul ah
    add ax, w16_cls + W16C_NAME
    mov si, ax
    mov dx, 16
    jmp short .copy
.ours:
    xor ah, ah
    dec ax
    shl ax, 1
    mov si, ax
    mov si, [cs:w16u_clsn + si]     ; XSEC-OK: read through cs:
    push ds
    push cs                         ; XSEC-OK: the names are the part's, and
    pop ds                          ;   DS is put back after the copy
    call .copyds
    pop ds
    jmp short .out
.copy:
    call .copyds
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop di
    pop si
    ret
; .copyds - DS:SI, at most DX and CX characters, to ES:DI, NUL-ended:
; AX = the characters
.copyds:
    push di
    mov dx, 16
.c:
    jcxz .e
    mov al, [si]
    or al, al
    jz .e
    mov [es:di], al
    inc si
    inc di
    dec cx
    dec dx
    jnz .c
.e:
    mov byte [es:di], 0
    mov ax, di
    pop di
    sub ax, di
    ret
w16u_clsn:  dw .b, .e, .s, .l, .d, .sc, .co, .md, .pg   ; W16Q_BUTTON.. order
.b:  db 'Button', 0
.e:  db 'Edit', 0
.s:  db 'Static', 0
.l:  db 'ListBox', 0
.d:  db '#32770', 0
.sc: db 'ScrollBar', 0
.co: db 'ComboBox', 0
.md: db 'MDIClient', 0
.pg: db 'Progman', 0                 ; the DDE server's (SPEC.md 124.33.2)

; EnumWindows (USER.54) - args: +0 lParam (long), +4 lpEnumFunc. The
; callback for each of the program's top-level windows, (hwnd, lParam), as a
; call into the program, until one answers FALSE. AX = 1.
; EnumChildWindows (USER.55) - args: the same, +8 hWndParent: the callback
; for each of its descendants instead
W16LIB w16u_enumwnd
    xor cx, cx
    jmp short w16u_enwgo
W16LIB w16u_enumchild
    mov cx, 0x8000
w16u_enwgo:                         ; CX = the slot to try from, bit 15 set
    push si                         ;   for EnumChildWindows
    push di
    mov di, cx
    and di, 0x8000                  ; DI = which
    and cx, 0x7FFF
.slot:
    cmp cx, W16_MAXWND
    jae .done
    ICALL w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .next
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .next
    or di, di
    jnz .child
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .next
    jmp short .take
.child:
    push bx
    ICALL w16_args
    mov ax, [es:bx + 8]             ; hWndParent
    pop bx
    push bx
    ICALL w16_parent
    jc .notin                       ; a top-level window
    ICALL w16_inside                ; CF=1: AX is its parent or above
    cmc
.notin:
    pop bx
    jc .next
.take:
    ICALL w16_rec2hwnd              ; AX = it
    push ax
    ICALL w16_args                  ; (before cbopen moves the frame)
    push word [es:bx + 6]           ; the function
    push word [es:bx + 4]
    push word [es:bx + 2]           ; lParam
    push word [es:bx]
    mov ax, cx                      ; the next slot, the continuation's word
    inc ax
    or ax, di
    mov si, w16u_enwnext_c
    xor cx, cx
    ICALL w16_cbopen
    pop bx                          ; lParam's low word
    pop cx                          ;   ...high
    pop ax                          ; the function: AX offset, DX segment
    pop dx
    pop si                          ; hwnd
    push dx
    push ax
    mov ax, si
    ICALL w16_ppush                 ; pascal: hwnd, then lParam
    mov ax, cx
    ICALL w16_ppush
    mov ax, bx
    ICALL w16_ppush
    pop ax
    pop dx
    ICALL w16_cbcall
    pop di
    pop si
    ret
.next:
    inc cx
    jmp .slot
.done:
    mov ax, 1
    xor dx, dx
    pop di
    pop si
    ret

LCONT w16u_enwnext
    or ax, ax
    jnz w16u_enwgo
    mov ax, 1
    xor dx, dx
    ret

; The W16_LMROW handler (SPEC.md 124.23.4): the program's first "call",
; each library's LibMain in turn, then the program's entry with its start
; registers, which the LibMains' own calls would otherwise have overwritten
W16LIB w16_lmgo
    mov ax, [w16_rbx]
    mov [w16_lmsv], ax
    mov ax, [w16_rcx]
    mov [w16_lmsv + 2], ax
    mov ax, [w16_res]
    mov [w16_lmsv + 4], ax
    mov word [w16_lmi], 0
w16l_lmnext:
    mov ax, [w16_lmi]
    cmp ax, W16_NLIB
    jae .done
    inc word [w16_lmi]
    call w16l_recp
    cmp byte [bx + W16L_COUNT], 0
    je w16l_lmnext
    cmp byte [bx + W16L_OWED], 0
    je w16l_lmnext
    mov byte [bx + W16L_OWED], 0
    mov si, w16l_lmback_c
    jmp w16l_lmcall
.done:
    mov ax, [w16_lmsv]
    mov [w16_rbx], ax
    mov ax, [w16_lmsv + 2]
    mov [w16_rcx], ax
    mov ax, [w16_lmsv + 4]
    mov [w16_res], ax
    xor ax, ax
    cwd
    ret

; ...each LibMain's answer: AX, CX = the record. 0 ends the program
LCONT w16l_lmback
    or ax, ax
    jnz w16l_lmnext
    mov ax, cx
    call w16l_recp
    mov si, w16_s_lmfail
    mov di, w16_l1
    ICALL w16_strcpy
    lea si, [bx + W16L_FILE]
    mov di, w16_l2
    ICALL w16_strcpy
    mov byte [w16_endmsg], 1
    mov byte [w16_state], W16S_ENDED
    xor ax, ax
    cwd
    ret

; w16l_lmcall - call record BX's LibMain (index AX) as a call into the
; program, SI = the continuation: DS = its DGROUP, DI = its hInstance, CX =
; its ne_heap, ES:SI = an empty command line (SPEC.md 124.23.4)
w16l_lmcall:
    push bx
    xor cx, cx
    ICALL w16_cbopen
    pop bx
    mov ax, [bx + W16L_HINST]
    mov [w16_lmdi], ax
    mov [w16_lmds], ax
    mov ax, [bx + W16L_HEAP]
    mov [w16_lmcx], ax
    W16PSP
    mov [w16_lmes], ax
    mov word [w16_lmsi], 0x80       ; the PSP's tail length: a 0, so ""
    push es
    W16SEG es, [bx + W16L_MREC]
    mov ax, [es:W16LM_IP]
    mov [w16_lmfar], ax
    mov bx, [es:W16LM_CS]
    dec bx
    shl bx, 1
    mov ax, [es:W16LM_SEGS + bx]
    mov [w16_lmfar + 2], ax
    pop es
    mov ax, w16_lmtramp
    W16ICS dx
    ICALL w16_cbcall
    ret

; w16l_recp - AX = a record's index -> BX -> it. Keeps the rest.
w16l_recp:
    push cx
    mov bx, ax
    mov cl, 5
    shl bx, cl
    add bx, w16_libs
    pop cx
    ret

; w16l_drop - record AX freed: the libraries it imports lose a count (and go
; at 0, without their WEP), and its three blocks are given back. Keeps all.
w16l_drop:
    push ax
    push bx
    push cx
    push si
    push es
    call w16l_recp
    W16SEG es, [bx + W16L_MREC]
    xor si, si
.dep:
    mov al, [es:W16LM_LIBS + si]
    cmp al, 0xFF
    je .ndep
    xor ah, ah
    push bx
    call w16l_recp
    cmp byte [bx + W16L_COUNT], 0
    je .ok
    dec byte [bx + W16L_COUNT]
    jnz .ok
    call w16l_drop
.ok:
    pop bx
.ndep:
    inc si
    cmp si, 16
    jb .dep
    mov ax, [bx + W16L_CODE]
    or ax, ax
    jz .nc
    ICALL w16_gfreeh
.nc:
    mov ax, [bx + W16L_HINST]
%ifdef PKG_MEGA
    ICALL w16_pin                   ; (the program's selector: 124.31.3)
%endif
    cmp ax, [bx + W16L_MREC]
    je .nd
    ICALL w16_gfreeh
.nd:
    mov ax, [bx + W16L_MREC]
    ICALL w16_gfreeh
    mov byte [bx + W16L_COUNT], 0
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16l_up - AL upper-cased
w16l_up:
    cmp al, 'a'
    jb .o
    cmp al, 'z'
    ja .o
    sub al, 0x20
.o:
    ret

section .text                       ; (read through DS, by w16_strcpy)
w16_s_lmfail: db 'A library it needs did not start.', 0
section .w16dlg

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
    call w16od_last                  ; an own DC's the open one (124.25.11)
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
    call w16od_last                  ; an own DC's the open one (124.25.11)
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
    W16SEG ds, [w16b_seg + bx]
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
    W16SEG ds, [w16b_seg + bx]
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

; The W16_SLROW handler (SPEC.md 124.26.2): a discarded segment's thunk was
; hit. The segment is loaded (w16l_segin, in the kept loader), the gate's
; frame returns to the entry itself instead of the thunk, and the call answers
; the AX and DX it came with. A segment that cannot be loaded ends the
; program, its lines written, as a missing import does.
W16LIB w16_segld
    ICALL w16_args
    sub bx, W16F_ARGS               ; ES:BX -> the frame
    push es
    push bx
    les di, [es:bx + W16F_IP]       ; ES:DI -> the thunk + 5
    mov al, [es:di]                 ; its segment
    xor ah, ah
    mov si, [es:di + 1]             ; its entry's offset
    mov bx, w16l_segin              ; XSEC-OK: part 5's, w16_ldcall calls it there
    ICALL w16_ldcall                ; DX = the segment; CF=1 lines written
    pop bx
    pop es
    jc .fail
%ifdef PKG_MEGA
    push ax                         ; its code selector (SPEC.md 124.31.4)
    push cx
    mov ax, dx
    mov cl, 1
    ICALL w16_ssel
    mov dx, ax
    pop cx
    pop ax
%endif
    mov [es:bx + W16F_IP], si
    mov [es:bx + W16F_CS], dx
    mov ax, [w16_thax]
    mov dx, [w16_thdx]
    ret
.fail:
    mov byte [w16_endmsg], 1
    mov byte [w16_state], W16S_ENDED
    xor ax, ax
    cwd
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

; GetTopWindow (USER.229) - args: +0 hWnd: GetWindow's GW_CHILD. GetNextWindow
; (USER.230) is GetWindow's own row: the same arguments, GW_HWNDNEXT or PREV
W16LIB w16u_topwnd
    push si
    ICALL w16_args
    mov ax, [es:bx]
    jmp short w16u_getwindow.child

; WindowFromDC (USER.117) - args: +0 hDC: the window the open DC is on, the
; desktop for the desktop's DC. Windows 3.0's Task Manager calls ordinal 117
; with an HWND and hands the answer to IsWinOldApTask as a task: an HWND
; answers the instance, which stands for the task here (SPEC.md 124.22.12)
W16LIB w16u_wndfromdc
    call w16od_last                  ; an own DC's the open one (124.25.11)
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
    W16SEG es, si
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

; GetMessageTime (USER.120): when the last message handed over was made,
; in GetTickCount's milliseconds
W16LIB w16u_msgtime
    mov ax, [w16_msg + MSG_TIME]
    mov dx, [w16_msg + MSG_TIME + 2]
    ret

; GetCursorPos (USER.17) - args: +0 lpPoint: where the pointer is now
W16LIB w16u_getcurpos                 ; where the pointer IS, not where the
    ICALL w16_args                      ;   last mouse message said
    les bx, [es:bx]
    push cx
    push dx
    call OSAPI_MOUSE
    mov [es:bx], cx
    mov [es:bx + 2], dx
    pop dx
    pop cx
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

; ChildWindowFromPoint (USER.191) - args: +0 x, +2 y (a POINT by value, in
; hWndParent's client), +4 hWndParent: of its own children, shown or not, the
; one whose rectangle holds the point - of two the first in Z order, which is
; the one made first (GetWindow's, USER.262) - else hWndParent when its
; client holds the point, else 0
W16LIB w16u_chwndpt
    push si
    push di
    ICALL w16_args
    mov di, [es:bx]                 ; DI = x
    mov ax, [es:bx + 4]
    push word [es:bx + 2]
    ICALL w16_hwnd2rec              ; keeps AX = hWndParent
    pop dx                          ; DX = y
    jc .zero
    push ax
    push dx
    ICALL w16_origin                ; CX/SI = its client's size
    pop dx
    pop ax
    jc .zero
    cmp di, cx                      ; outside it (a negative too, unsigned)
    jae .zero
    cmp dx, si
    jae .zero
    push bp
    xor bp, bp                      ; BP = the child found
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    cmp [bx + W16W_PARENT], ax
    jne .n
    mov si, di
    sub si, [bx + W16W_X]
    jl .n
    cmp si, [bx + W16W_CX]
    jge .n
    mov si, dx
    sub si, [bx + W16W_Y]
    jl .n
    cmp si, [bx + W16W_CY]
    jge .n
    or bp, bp
    jz .take
    mov si, [bx + W16W_SEQ]
    cmp si, [ds:bp + W16W_SEQ]
    jae .n
.take:
    mov bp, bx
.n:
    add bx, W16W_SIZE
    loop .w
    or bp, bp
    jz .par                         ; AX = hWndParent
    mov bx, bp
    ICALL w16_rec2hwnd
.par:
    pop bp
    jmp short .out
.zero:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; GrayString (USER.185) - args: +0 nHeight, +2 nWidth, +4 Y, +6 X, +8
; nCount, +10 lpData, +14 lpOutputFunc, +18 hBrush, +20 hDC. With no output
; function the string at lpData, nCount characters (0: to its NUL), drawn at
; X, Y in grey - its ink alone, as under TRANSPARENT, where Windows greys
; the text's cell through hBrush. With one, (hDC, lpData, nCount) as a call
; into the program, which draws in the DC's colours, and its answer is the
; answer (SPEC.md 124.26.8)
W16LIB w16u_graystr
    push si
    push di
    push bp
    cmp byte [w16_dcon], 0
    je .no
    ICALL w16_args
    cmp word [es:bx + 20], W16_HDC
    jne .no
    mov ax, [es:bx + 14]
    or ax, [es:bx + 16]
    jnz .call
    cmp word [es:bx + 8], 0
    jne .n
    push es
    les di, [es:bx + 10]            ; nCount 0: the string's length
    mov cx, W16_TXTMAX
    xor al, al
    cld
    repne scasb
    mov ax, W16_TXTMAX - 1
    sub ax, cx
    pop es
    mov [es:bx + 8], ax             ; (the program's argument, popped by the
.n:                                 ;   gate on the way out)
    mov ax, [es:bx + 6]
    add ax, [w16_ox]
    mov [w16_tx], ax
    mov ax, [es:bx + 4]
    add ax, [w16_oy]
    mov [w16_ty], ax
    mov si, 10
    mov di, 8
    ICALL w16_txtcopy
    mov [w16_tn], cx
    call w16_talign
    mov ax, 0x8080                  ; the grey of COLOR_GRAYTEXT
    mov dl, 0x80
    ICALL w16_rgb2col
    xchg al, [w16_tcol]
    mov ah, TRANSPARENT
    xchg ah, [w16_bkmode]
    push ax
    ICALL w16_txtdraw
    pop ax
    mov [w16_tcol], al
    mov [w16_bkmode], ah
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
.call:
    push word [es:bx + 16]          ; the function
    push word [es:bx + 14]
    push word [es:bx + 8]           ; nCount
    push word [es:bx + 12]          ; lpData
    push word [es:bx + 10]
    mov si, w16u_gsnext_c
    xor cx, cx
    ICALL w16_cbopen
    mov ax, W16_HDC                 ; pascal: hDC, lpData, nCount
    ICALL w16_ppush
    pop ax
    pop dx
    xchg ax, dx
    ICALL w16_ppush                 ; lpData's segment, then its offset
    mov ax, dx
    ICALL w16_ppush
    pop ax
    ICALL w16_ppush
    pop ax
    pop dx
    ICALL w16_cbcall
    jmp short .out

LCONT w16u_gsnext
    xor dx, dx
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
    push dx                         ; w16_gfill cuts it and inverts, on the
    mov dl, W16_XOR                 ;   screen or in a memory DC's bitmap
    xchg dl, [w16_gcol]             ;   (SPEC.md 124.25.3)
    ICALL w16_gfill
    mov [w16_gcol], dl
    pop dx
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
    call w16g_lfill
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

; w16g_lfill - ES:DI -> LF_SIZE bytes: the LOGFONT of the face in [w16_face]
; (EnumFontFamilies', GetObject's and ChooseFont's, SPEC.md 124.24.4). Keeps
; every register.
w16g_lfill:
    push ax
    push cx
    push si
    push di
    push di
    mov cx, LF_SIZE / 2
    xor ax, ax
    cld
    rep stosw
    pop di
    push es
    W16SEG es, [w16_face]              ; its height and width
    mov al, [es:W16F_ROWS]
    mov cl, [es:W16F_AVG]
    pop es
    mov [es:di], ax                 ; lfHeight
    mov [es:di + 2], cx             ; lfWidth
    mov word [es:di + 8], 700       ; lfWeight: bold
    mov word [es:di + 14], 0x0201   ; lfOutPrecision OUT_STRING_PRECIS,
    mov word [es:di + 16], 0x2201   ;   lfClipPrecision CLIP_STROKE_PRECIS,
    add di, 18                      ;   lfQuality DRAFT, VARIABLE_PITCH |
    call w16g_fname                 ;   FF_SWISS; lfFaceName
.nm:
    mov al, [cs:si]
    mov [es:di], al
    inc si
    inc di
    or al, al
    jnz .nm
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16g_fname - SI = the name of the face in [w16_face], read through CS.
; Keeps the rest.
w16g_fname:
    mov si, w16g_facename   ; XSEC-OK: read through cs:
    push ax
    mov ax, [w16_face]
    or ax, ax
    jz .s
    cmp ax, [w16_dface]
    jne .s
    mov si, w16g_dlgname    ; XSEC-OK: read through cs:
.s:
    pop ax
    ret

; GetTextFace (GDI.92) - args: +0 lpFaceName, +4 nCount, +6 hDC: the
; face's name ("System", or the dialog face's, SPEC.md 124.24.4) and its NUL,
; cut to the count; its length
W16LIB w16g_textface
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx + 4]
    les di, [es:bx]
    xor ax, ax
    jcxz .out
    dec cx
    call w16g_fname
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
w16g_dlgname:  db 'MS Sans Serif', 0

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

; Polygon (GDI.36) - args: +0 nCount, +2 lpPoints, +6 hDC: the inside in the
; brush under the DC's polygon-fill mode (w16_pgfill, SPEC.md 124.25.4), then
; the edges in the pen, the last point joined to the first; the current point
; is left where it was
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
    cmp byte [w16_dcon], 0
    je .edges
    mov ax, [w16_brush]
    ICALL w16_brcol
    ICALL w16_ropcol
    cmp al, W16_NOCOL
    je .edges
    mov [w16_gcol], al
    call w16_pgfill
    ICALL w16_args
    mov cx, [es:bx]
.edges:
    push word [w16_curx]
    push word [w16_cury]
    mov si, cx                      ; the last point is the first edge's start
    dec si
    shl si, 1
    shl si, 1
    xor di, di                      ; ...and the first point its end
.start:
    les bx, [es:bx + 2]
    mov ax, [es:bx + si]
    mov [w16_curx], ax
    mov ax, [es:bx + si + 2]
    mov [w16_cury], ax
    mov si, di                      ; SI = the point's offset, CX how many
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

; w16_pgfill - Polygon's inside (SPEC.md 124.25.4): ES:BX -> its arguments,
; +0 the count (2 or more) and +2 the points, filled in [w16_gcol] a row at a
; time. A row y takes each edge that has y in [top, bottom) and crosses it at
; the row's centre, rounded to a column; the columns between crossings are
; inside under ALTERNATE when an odd number lie left of them and under
; WINDING when the edges' directions do not sum to 0, and a run of them is
; one w16_gfill from its first crossing to the column before its last - so a
; polygon's right and bottom edges are not filled, as Windows' are not. Rows
; outside the DC's clip are not looked at; at most W16_PGMAX crossings a row
; are kept. Clobbers AX, BX, CX, DX, SI, DI, BP, ES.
W16_PGMAX   equ 64                  ; crossings in w16_gbuf: their columns,
W16_PGDIR   equ W16_PGMAX * 2       ;   then each one's direction
w16_pgfill:
    mov cx, [es:bx]
    mov [w16_pgn], cx
    les si, [es:bx + 2]
    mov [w16_pgp], si
    mov [w16_pgp + 2], es
    mov ax, [es:si + 2]             ; AX = the top point's y, DX the bottom's
    mov dx, ax
.mm:
    mov di, [es:si + 2]
    cmp di, ax
    jge .mm1
    mov ax, di
.mm1:
    cmp di, dx
    jle .mm2
    mov dx, di
.mm2:
    add si, 4
    loop .mm
    mov bx, [w16_ct]                ; cut to the clip, in the DC's own
    sub bx, [w16_oy]                ;   coordinates
    cmp ax, bx
    jge .t
    mov ax, bx
.t:
    mov bx, [w16_cb]
    sub bx, [w16_oy]
    inc bx
    cmp dx, bx
    jle .b
    mov dx, bx
.b:
    mov [w16_pgy], ax
    mov [w16_pgy1], dx              ; (the row after the last)
.row:
    mov ax, [w16_pgy]
    cmp ax, [w16_pgy1]
    jl .scan
    ret
.scan:
    xor bp, bp                      ; BP = the crossings found
    mov cx, [w16_pgn]
    les bx, [w16_pgp]               ; ES:BX = the points
    mov si, cx                      ; SI = the edge's start, the last point
    dec si                          ;   first; DI = its end
    shl si, 1
    shl si, 1
    xor di, di
.edge:
    mov ax, [es:bx + si]
    mov [w16_pgxa], ax
    mov ax, [es:bx + si + 2]
    mov [w16_pgya], ax
    mov ax, [es:bx + di]
    mov [w16_pgxb], ax
    mov ax, [es:bx + di + 2]
    mov [w16_pgyb], ax
    call w16_pgcross                ; AX = its column, DL its direction
    jc .nx
    cmp bp, W16_PGMAX
    jae .nx
    push si                         ; into the row's list, by column
    push di
    mov di, bp
    shl di, 1
.ins:
    or di, di
    jz .put
    cmp ax, [w16_gbuf + di - 2]
    jge .put
    mov si, [w16_gbuf + di - 2]
    mov [w16_gbuf + di], si
    shr di, 1
    mov dh, [w16_gbuf + W16_PGDIR + di - 1]
    mov [w16_gbuf + W16_PGDIR + di], dh
    shl di, 1
    dec di
    dec di
    jmp short .ins
.put:
    mov [w16_gbuf + di], ax
    shr di, 1
    mov [w16_gbuf + W16_PGDIR + di], dl
    inc bp
    pop di
    pop si
.nx:
    mov si, di
    add di, 4
    loop .edge
    ; --- the runs inside ---
    xor bx, bx                      ; BX = the crossing, DL the winding
    xor dl, dl
.walk:
    cmp bx, bp
    jae .done
    mov dh, dl
    cmp byte [w16_pfmode], WINDING
    je .wind
    xor dl, 1
    jmp short .w1
.wind:
    add dl, [w16_gbuf + W16_PGDIR + bx]
.w1:
    push bx
    shl bx, 1
    mov ax, [w16_gbuf + bx]
    pop bx
    or dh, dh
    jnz .was
    or dl, dl
    jz .wn
    mov [w16_pgs], ax               ; the run starts
    jmp short .wn
.was:
    or dl, dl
    jnz .wn
    dec ax                          ; ...and ends before this crossing
    cmp ax, [w16_pgs]
    jl .wn
    push bx
    push dx
    mov cx, ax
    add cx, [w16_ox]
    mov ax, [w16_pgs]
    add ax, [w16_ox]
    mov bx, [w16_pgy]
    add bx, [w16_oy]
    mov dx, bx
    ICALL w16_gfill
    pop dx
    pop bx
.wn:
    inc bx
    jmp short .walk
.done:
    inc word [w16_pgy]
    jmp .row

; w16_pgcross - [w16_pgxa], [w16_pgya] to [w16_pgxb], [w16_pgyb]: where the
; edge crosses row [w16_pgy]'s centre -> AX = the column, DL = 1 downward or
; -1 upward. CF=1: it does not (level, or the row is not in [top, bottom)),
; or it is longer than 16,383 rows. Keeps all but AX, DX.
w16_pgcross:
    push bx
    push cx
    mov cx, [w16_pgya]
    mov dx, [w16_pgyb]
    mov byte [w16_pgdir], 1
    cmp cx, dx
    je .none
    jl .ord
    xchg cx, dx                     ; upward: from its top end instead
    mov ax, [w16_pgxa]
    xchg ax, [w16_pgxb]
    mov [w16_pgxa], ax
    mov byte [w16_pgdir], -1
.ord:                               ; CX = the top row, DX the bottom
    mov ax, [w16_pgy]
    cmp ax, cx
    jl .none
    cmp ax, dx
    jge .none
    sub dx, cx
    cmp dx, 0x4000
    jae .none
    shl dx, 1
    mov bx, dx                      ; BX = 2 dy
    sub ax, cx
    shl ax, 1
    inc ax                          ; AX = 2 (y - top) + 1
    mov cx, [w16_pgxb]
    sub cx, [w16_pgxa]
    imul cx                         ; DX:AX = that times dx
    mov cx, bx
    shr cx, 1                       ; rounded to the nearest column
    or dx, dx
    js .neg
    add ax, cx
    adc dx, 0
    jmp short .div
.neg:
    sub ax, cx
    sbb dx, 0
.div:
    idiv bx
    add ax, [w16_pgxa]
    mov dl, [w16_pgdir]
    clc
    jmp short .out
.none:
    stc
.out:
    pop cx
    pop bx
    ret

; Polyline (GDI.37) - args: +0 nCount, +2 lpPoints, +4 hDC: Polygon's
; edges without the one closing it, in the pen; the current point is neither
; used nor moved, as Windows'
W16LIB w16g_polyline
    push si
    push di
    push bp
    ICALL w16_args
    cmp word [es:bx + 6], W16_HDC
    jne w16g_polygon.no
    mov cx, [es:bx]
    cmp cx, 2
    jl w16g_polygon.no
    push word [w16_curx]
    push word [w16_cury]
    dec cx                          ; one edge fewer: the first point starts
    xor si, si                      ;   it and the second ends it
    mov di, 4
    jmp w16g_polygon.start

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

; --- Paintbrush's (SPEC.md 124.23.9) ----------------------------------------

; GlobalNotify (KERNEL.154) - args: +0 lpNotifyProc: kept, and asked about
; a GMEM_NOTIFY block before a short heap takes it (w16k_gnask)
W16LIB w16k_gnotify
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_gnproc], ax
    mov ax, [es:bx + 2]
    mov [w16_gnproc + 2], ax
    ret

; w16k_gnask - GlobalAlloc could not find the room (SPEC.md 124.9.1): the
; least recently used GMEM_NOTIFY block nobody has locked is offered to the
; GlobalNotify procedure, as a call into the program, and w16k_gnback is
; what it returns to. AX = 0 with nothing to offer or no procedure
W16LIB w16k_gnask
    mov ax, [w16_gnproc + 2]
    or ax, ax
    jz .none
    mov di, 0xFFFF                  ; DI = the oldest so far, DX its use
    xor si, si
.h:
    test byte [w16_ghf + si], W16_GHNOTE
    jz .n
    mov ax, [w16_ghseg + si]
    or ax, ax
    jz .n                           ; discarded already
    dec ax
    W16SEG es, ax
    cmp byte [es:GH_LOCK], 0
    jne .n
    mov ax, [w16_ghage + si]
    cmp di, 0xFFFF
    je .take
    cmp ax, dx
    jae .n
.take:
    mov di, si
    mov dx, ax
.n:
    inc si
    inc si
    cmp si, 2 * W16_NGH
    jb .h
    cmp di, 0xFFFF
    je .none
    mov ax, di                      ; the continuation's word: the entry
    lea bx, [di + W16_GH0]          ; (cbopen answers in DI)
    mov si, w16k_gnback_c
    xor cx, cx
    ICALL w16_cbopen
    mov ax, bx                      ; pascal: the handle
    ICALL w16_ppush
    mov ax, [w16_gnproc]
    mov dx, [w16_gnproc + 2]
    ICALL w16_cbcall
    ret
.none:
    xor ax, ax
    xor dx, dx
    ret

; the procedure's answer in AX, the entry in CX: nonzero, and the block is
; discarded if nobody locked it meanwhile; 0, and it is never offered again.
; Then the call that ran short once more - GlobalAlloc, or a GDI call making
; a bitmap - its slot and frame w16_cbdone's again (SPEC.md 124.9.1)
LCONT w16k_gnback
    mov si, cx
    or ax, ax
    jz .kept
    mov ax, [w16_ghseg + si]
    or ax, ax
    jz .again                       ; it gave the block up itself
    dec ax
    W16SEG es, ax
    cmp byte [es:GH_LOCK], 0
    jne .kept
    ICALL w16_gfreeblk
    mov word [w16_ghseg + si], 0
    jmp short .again
.kept:
    and byte [w16_ghf + si], ~W16_GHNOTE
.again:
    mov bx, [w16_idx]
    shl bx, 1
    shl bx, 1                       ; * W16B_SIZE
    add bx, w16_bind
    mov ax, [w16_nargb]
    ICALL w16_dispatch
    ret

; GlobalLRUNewest (KERNEL.164), GlobalLRUOldest (KERNEL.163) - args: +0 the
; handle, answered. The block is the last a shortage discards (w16_gdold), or
; the first.
W16LIB w16k_glru
    ICALL w16_args
    mov ax, [es:bx]
    push si
    call w16r_gent
    ICALL w16_gtouch
    pop si
    ret
W16LIB w16k_glruold
    ICALL w16_args
    mov ax, [es:bx]
    push si
    call w16r_gent
    cmp si, 0xFFFF
    je .out
    mov word [w16_ghage + si], 0
.out:
    pop si
    ret
; w16r_gent - AX = a global handle -> SI = its moveable entry, or 0xFFFF
w16r_gent:
    mov si, 0xFFFF
    cmp ax, W16_GH0
    jb .ret
    cmp ax, W16_GH0 + 2 * W16_NGH
    jae .ret
    test al, 1
    jnz .ret
    mov si, ax
    sub si, W16_GH0
    cmp byte [w16_ghf + si], 0
    jne .ret
    mov si, 0xFFFF
.ret:
    ret

; GetTempDrive (KERNEL.92) - args: +0 a byte, ignored. The Windows
; directory's drive (SPEC.md 124.21.3), its letter in AL and ':' in AH, and
; DX = 1, as Wine's GetTempDrive16 answers
W16LIB w16k_tempdrv
    mov al, [w16_homev]
    add al, 'A'
    mov ah, ':'
    mov dx, 1
    ret

; SetRectEmpty (USER.73) - args: +0 lpRect, its four words 0
W16LIB w16u_rectempty
    push di
    ICALL w16_args
    les di, [es:bx]
    xor ax, ax
    cld
    stosw
    stosw
    stosw
    stosw
    pop di
    ret

; IsRectEmpty (USER.75) - args: +0 lpRect: TRUE when it has no width or no
; height
W16LIB w16u_isrempty
    ICALL w16_args
    les bx, [es:bx]
    mov ax, 1
    mov dx, [es:bx + 4]
    cmp dx, [es:bx]
    jle .out
    mov dx, [es:bx + 6]
    cmp dx, [es:bx + 2]
    jle .out
    dec ax
.out:
    xor dx, dx
    ret

; UnionRect (USER.80) - args: +0 lprc2, +4 lprc1, +8 lprcDst. The smallest
; rectangle holding both, an empty one ignored; both empty empties the
; destination and answers FALSE
W16LIB w16u_unionrect
    push si
    push di
    ICALL w16_args
    mov word [w16_dtr], 0x7FFF      ; nothing yet
    mov word [w16_dtr + 2], 0x7FFF
    mov word [w16_dtr + 4], 0x8000
    mov word [w16_dtr + 6], 0x8000
    push bx
    les si, [es:bx + 4]
    call .one
    pop bx
    push bx
    ICALL w16_args
    les si, [es:bx]
    call .one
    pop bx
    ICALL w16_args
    les di, [es:bx + 8]
    mov si, w16_dtr
    mov ax, [si + 4]
    cmp ax, [si]
    jg .some
    xor ax, ax                      ; both empty: the destination emptied
    cld
    stosw
    stosw
    stosw
    stosw
    jmp short .out
.some:
    mov cx, 4
    cld
    rep movsw
    mov ax, 1
.out:
    xor dx, dx
    pop di
    pop si
    ret
; .one - ES:SI = a rectangle into w16_dtr, unless it is empty
.one:
    mov ax, [es:si + 4]
    cmp ax, [es:si]
    jle .skip
    mov ax, [es:si + 6]
    cmp ax, [es:si + 2]
    jle .skip
    mov ax, [es:si]
    cmp ax, [w16_dtr]
    jge .l
    mov [w16_dtr], ax
.l:
    mov ax, [es:si + 2]
    cmp ax, [w16_dtr + 2]
    jge .t
    mov [w16_dtr + 2], ax
.t:
    mov ax, [es:si + 4]
    cmp ax, [w16_dtr + 4]
    jle .r
    mov [w16_dtr + 4], ax
.r:
    mov ax, [es:si + 6]
    cmp ax, [w16_dtr + 6]
    jle .skip
    mov [w16_dtr + 6], ax
.skip:
    ret

; GetPriorityClipboardFormat (USER.402) - args: +0 cFormats, +2 lpList.
; The first of the list the clipboard holds; 0 when it holds nothing, -1
; when it holds none of them
W16LIB w16u_cbprio
    push si
    call w16u_cbtrue                ; AX = 1 when it holds text
    or ax, ax
    jz .out
    ICALL w16_args
    mov cx, [es:bx]
    les si, [es:bx + 2]
    mov ax, -1
    jcxz .out
.f:
    mov dx, [es:si]
    xchg ax, dx
    call w16_cbtext
    je .out
    xchg ax, dx
    inc si
    inc si
    loop .f
.out:
    xor dx, dx
    pop si
    ret

; GetBkColor (GDI.75) - args: +0 hDC: SetBkColor's COLORREF
W16LIB w16g_getbkcol
    mov ax, [w16_brgb]
    mov dx, [w16_brgb + 2]
    ret

; GetTextColor (GDI.90) - args: +0 hDC: SetTextColor's COLORREF
W16LIB w16g_gettextcol
    mov ax, [w16_trgb]
    mov dx, [w16_trgb + 2]
    ret

; GetTextAlign (GDI.345) - args: +0 hDC: SetTextAlign's flags
W16LIB w16g_gettalign
    mov al, [w16_tal]
    xor ah, ah
    cwd
    ret

; GetCurrentPosition (GDI.78) - args: +0 hDC: MoveTo's point, logical, DX:AX
; = y:x
W16LIB w16g_getcurpos
    mov ax, [w16_curx]
    mov dx, [w16_cury]
    ret

; GetCurrentTask (KERNEL.36): the program's task, which its instance stands
; for (WindowFromDC's, USER.117), and in DX the first task of the list - it
; again, the one task there is
W16LIB w16k_curtask
    mov ax, [w16_dgroup]
    mov dx, ax
    ret

; SetSwapAreaSize (KERNEL.106) - args: +0 the paragraphs of code asked for:
; granted, DX = FFFFh the most - the code has no area of its own, and shares
; the global heap with the data (SPEC.md 124.26.6)
W16LIB w16k_swaparea
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, 0xFFFF
    ret

; GetROP2 (GDI.85) - args: +0 hDC: SetROP2's mode
W16LIB w16g_getrop2
    mov al, [w16_rop]
    xor ah, ah
    xor dx, dx
    ret

; GetWindowOrg (GDI.97) - args: +0 hDC: SetWindowOrg's, DX:AX = y:x
W16LIB w16g_getworg
    mov ax, [w16_worg]
    mov dx, [w16_worg + 2]
    ret

; SetPolyFillMode (GDI.6), SetStretchBltMode (GDI.7) - args: +0 the mode,
; +2 hDC: kept, the old one answered. Neither changes a pixel: WIN16 fills no
; polygon (SPEC.md 124.22.16) and StretchBlt drops or repeats whole pixels
W16LIB w16g_setpfmode
    push si
    mov si, w16_pfmode
    call w16_dcbyte
    pop si
    ret
W16LIB w16g_setsbmode
    push si
    mov si, w16_sbmode
    call w16_dcbyte
    pop si
    ret

; LPtoDP (GDI.99), DPtoLP (GDI.67) - args: +0 nCount, +2 lpPoints, +6 hDC.
; Every point moved by the window origin; the extents are kept and never
; scale a drawing here (SPEC.md 124.22.2), so they do not scale these
W16LIB w16g_lptodp
    mov cx, 1
    jmp short w16g_dplp
W16LIB w16g_dptolp
    xor cx, cx
w16g_dplp:
    push si
    push di
    ICALL w16_args
    mov si, [w16_worg]
    mov di, [w16_worg + 2]
    jcxz .back
    neg si
    neg di
.back:
    mov cx, [es:bx]
    les bx, [es:bx + 2]
    jcxz .out
.p:
    add [es:bx], si
    add [es:bx + 2], di
    add bx, 4
    loop .p
.out:
    mov ax, 1
    xor dx, dx
    pop di
    pop si
    ret

; EnumObjects (GDI.71) - args: +0 lpData, +4 lpObjectFunc, +8 nObjectType,
; +10 hDC. OBJ_PEN (1) and OBJ_BRUSH (2): a solid one in each of the
; display's sixteen colours, its LOGPEN or LOGBRUSH in a block on the
; program's stack, (lpLogObject, lpData), as a call into the program, until
; one answers 0. 1 when every one was called, 0 when one stopped it or the
; type is another
W16_EOBLK   equ 10                  ; a LOGPEN, the larger
W16LIB w16g_enumobj
    xor cx, cx
w16g_eogo:                          ; CX = the colour to try from
    push si
    cmp cx, 16
    jae .done
    ICALL w16_args                  ; (before cbopen moves the frame)
    mov dx, [es:bx + 8]
    cmp dx, 1
    je .ok
    cmp dx, 2
    jne .none
.ok:
    push word [es:bx + 6]           ; the function
    push word [es:bx + 4]
    push word [es:bx + 2]           ; lpData
    push word [es:bx]
    push dx                         ; the type
    push cx                         ; the colour
    mov ax, cx                      ; the next colour, the continuation's word
    inc ax
    mov si, w16g_eonext_c
    mov cx, W16_EOBLK
    ICALL w16_cbopen                ; ES:DI = the block, zeroed
    pop bx
    mov ax, bx                      ; BX = the colour * 3
    shl bx, 1
    add bx, ax
    mov ax, [w16_pal16 + bx]
    mov cl, [w16_pal16 + bx + 2]
    xor ch, ch
    pop dx
    cmp dx, 1
    jne .brush
    mov word [es:di + 2], 1         ; a LOGPEN: PS_SOLID, 1 wide
    mov [es:di + 6], ax
    mov [es:di + 8], cx
    jmp short .call
.brush:
    mov [es:di + 2], ax             ; a LOGBRUSH: BS_SOLID
    mov [es:di + 4], cx
.call:
    mov ax, es                      ; pascal: lpLogObject, then lpData
    ICALL w16_ppush
    mov ax, di
    ICALL w16_ppush
    pop ax                          ; lpData's low word
    pop cx                          ;   ...high
    xchg ax, cx
    ICALL w16_ppush
    mov ax, cx
    ICALL w16_ppush
    pop ax                          ; the function: AX offset, DX segment
    pop dx
    ICALL w16_cbcall
    pop si
    ret
.done:
    mov ax, 1
    jmp short .out
.none:
    xor ax, ax
.out:
    xor dx, dx
    pop si
    ret

LCONT w16g_eonext
    or ax, ax
    jnz w16g_eogo
    xor dx, dx
    ret

section .text

w16s_dir:    db ':\SYSTEM\APPDATA\WIN16', 0
w16s_dirlen  equ $ - w16s_dir
