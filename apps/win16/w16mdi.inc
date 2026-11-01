; =============================================================================
; os8088 - apps/win16/w16mdi.inc
;
; FILE MANAGER AND PROGRAM MANAGER (SPEC.md 124.27, docs/plans/WIN16-ANY-PLAN.md
; S20b): windows inside a window - the Multiple Document Interface - and the
; calls around it the two programs import. All of it is PART 9's, the
; system-library part (124.24.1): the image had 1,279 bytes left when it
; came, and a part of its own would have cost a gate of its own there. A
; handler here is W16SYS, near, ending in ret; the image is ICALL from here.
; =============================================================================

section .w16sys

; -----------------------------------------------------------------------------
; The calls around it (SPEC.md 124.27.1)
; -----------------------------------------------------------------------------

; GetDriveType (KERNEL.136) - args: +0 nDrive (0 = A:). Windows' answer for
; os8088's volume of that letter: DRIVE_REMOVABLE 2, DRIVE_FIXED 3, and
; DRIVE_REMOTE 4 for a redirected one (SPEC.md 62.9); 0 when there is none
W16SYS w16i_drvtype
    ICALL w16_args
    mov al, [es:bx]
    call w16i_dtype
    xor dx, dx
    ret

; w16i_dtype - AL = a drive (0 = A:) -> AX = GetDriveType's answer. Keeps
; the rest
w16i_dtype:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_VOL_KIND             ; AL = the medium, AH = the transport
    jc .none
    cmp ah, VT_FILE
    mov ah, 4                       ; DRIVE_REMOTE
    je .have
    mov ah, 3                       ; DRIVE_FIXED
    cmp al, VK_FIXED
    je .have
    dec ah                          ; DRIVE_REMOVABLE
.have:
    mov al, ah
    xor ah, ah
    jmp short .out
.none:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; InquireSystem (SYSTEM.1) - args: +0 wArg, +2 wCode. 0: the timer's period
; in microseconds; 1: the drive wArg's type, in both words, as Wine's
; system.drv16 answers; anything else 0. File Manager lists the drives
; whose type is not 0
W16SYS w16i_inqsys
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov al, [es:bx]
    xor dx, dx
    jcxz .timer
    cmp cx, 1
    jne .zero
    call w16i_dtype
    mov dx, ax
    ret
.timer:
    mov ax, 54925                   ; 65,536 PIT counts of 838 ns
    ret
.zero:
    xor ax, ax
    ret

; GetHeapSpaces (KERNEL.138) - args: +0 hModule. DX = the heap, AX = what is
; free of it, as GetFreeSystemResources counts it: USER's window records or
; GDI's objects, whichever is shorter, scaled to 65,500. Program Manager's
; About box shows the ratio as its "System Resources"
W16SYS w16i_heapsp
    push si
    xor si, si
    call w16u_fsr                   ; AX = the percent free
    mov cx, 655
    mul cx
    mov dx, 65500
    pop si
    ret

; DirectResAlloc (KERNEL.168) - args: +0 wSize, +2 wType, +4 hInstance. A
; moveable block of wSize bytes, as GlobalAlloc gives one: a resource made
; in memory, which Program Manager fills with an icon
W16SYS w16i_dresalloc
    ICALL w16_args
    mov ax, [es:bx]
    xor dx, dx
    mov cl, GMEM_MOVEABLE
    ICALL w16_gnew                  ; AX = the handle, 0 none
    xor dx, dx
    ret

; GetDOSEnvironment (KERNEL.131) - no args. A program WIN16 starts has no
; environment (its PSP's word 2Ch is 0), so the answer is an empty one: a
; NUL that ends the list at once, in this part, which is kept until the
; program ends
W16SYS w16i_dosenv
    mov ax, w16i_noenv              ; XSEC-OK: a far pointer into this part
    mov dx, cs
    ret
w16i_noenv:
    dw 0

; LongPtrAdd (KERNEL.180) - args: +0 dwAdd, +4 lpPtr. Real mode's answer:
; the pointer moved on by dwAdd bytes, its offset the low four bits. File
; Manager walks its copy buffer with it
W16SYS w16i_lpadd
    ICALL w16_args
    mov ax, [es:bx + 4]             ; DX:AX = the offset + dwAdd
    xor dx, dx
    add ax, [es:bx]
    adc dx, [es:bx + 2]
    mov cx, ax
    and cx, 15
    shr dx, 1                       ; ...in paragraphs
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    add ax, [es:bx + 6]
    mov dx, ax
    mov ax, cx
    ret

; ExitWindows (USER.7) - args: +0 wReserved, +2 dwReturnCode. Windows ends
; there, and with it the program: here the program ends, as a
; PostQuitMessage(0) ends it, and the call answers FALSE - "an application
; refused" - so that a program that goes on to its message loop meets the
; WM_QUIT there
W16SYS w16i_exitwin
    mov word [w16_quitcode], 0
    mov byte [w16_quit], 1
    xor ax, ax
    xor dx, dx
    ret

; GlobalFindAtom (USER.270) - args: +0 lpszName. A MAKEINTATOM is its own;
; a name is the atom GlobalAddAtom gave it (w16u_regcbfmt's table), or 0
W16SYS w16i_findatom
    push si
    push di
    ICALL w16_args
    les di, [es:bx]
    mov ax, es
    or ax, ax
    jnz .name
    mov ax, di
    jmp short .out
.name:
    xor bx, bx                      ; BX = the place
    mov si, w16_cbfn
.f:
    cmp byte [si], 0
    je .none
    push si
    push di
.c:
    mov al, [es:di]
    call w16i_up
    mov ah, al
    mov al, [si]
    call w16i_up
    cmp al, ah
    jne .ne
    or al, al
    jz .eq
    inc si
    inc di
    jmp short .c
.eq:
    pop di
    pop si
    mov ax, 0xC000
    add ax, bx
    jmp short .out
.ne:
    pop di
    pop si
    add si, 32
    inc bx
    cmp bx, W16_NCBF
    jb .f
.none:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16i_up - AL upper-cased, as AnsiUpper does: a-z, and the accented letters
; E0h-FEh but F7h. Keeps the rest
w16i_up:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    jbe .cv
    cmp al, 0xE0
    jb .out
    cmp al, 0xF7
    je .out
    cmp al, 0xFF
    je .out
.cv:
    sub al, 0x20
.out:
    ret

; w16i_alpha - AL: CF=0 it is a letter of the ANSI set - A-Z, a-z, and
; C0h-FFh but D7h and F7h. Keeps every register
w16i_alpha:
    push ax
    cmp al, 0xC0
    jae .hi
    or al, 0x20
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
.yes:
    pop ax
    clc
    ret
.hi:
    cmp al, 0xD7
    je .no
    cmp al, 0xF7
    jne .yes
.no:
    pop ax
    stc
    ret

; IsCharAlpha (USER.433), IsCharAlphaNumeric (USER.434) - args: +0 ch
W16SYS w16i_isalpha
    ICALL w16_args
    mov al, [es:bx]
    call w16i_alpha
    jmp short w16i_bool
W16SYS w16i_isalnum
    ICALL w16_args
    mov al, [es:bx]
    cmp al, '0'
    jb .a
    cmp al, '9'
    ja .a
    clc                             ; a digit
    jmp short w16i_bool
.a:
    call w16i_alpha
w16i_bool:                          ; CF=0 TRUE, CF=1 FALSE
    mov ax, 0
    jc .o
    inc ax
.o:
    xor dx, dx
    ret

; AnsiUpperBuff (USER.437) - args: +0 cbLength (0 is 65,536), +2 lpsz.
; Every byte upper-cased, NULs included. AX = the length
W16SYS w16i_upbuff
    push si
    ICALL w16_args
    mov cx, [es:bx]
    les si, [es:bx + 2]
    mov dx, cx
.c:
    mov al, [es:si]
    call w16i_up
    mov [es:si], al
    inc si
    loop .c
    mov ax, dx
    xor dx, dx
    pop si
    ret

; GetIconID (USER.455) - args: +0 dwResType, +4 hResource: of an
; RT_GROUP_ICON block, the image LoadIcon would choose (w16_icload's rule:
; 32 x 32, this display's colours) - its ordinal, 0 none
W16SYS w16i_iconid
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    ICALL w16_gfind                 ; ES = its block's header
    jc .zero
    cmp byte [w16_gdisc], 0
    jne .zero
    mov ax, es
    inc ax
    mov es, ax
    mov cx, [es:4]                  ; its images, 14 bytes each from +6
    jcxz .zero
    mov si, 6
    mov di, [es:si + 12]
    xor bl, bl
    mov dh, 16
    cmp byte [w16_mono], 0
    je .e
    mov dh, 2
.e:
    xor dl, dl
    cmp word [es:si], 0x2020
    jne .c
    or dl, 4
.c:
    cmp [es:si + 2], dh
    jne .s
    or dl, 2
.s:
    cmp dl, bl
    jbe .n
    mov bl, dl
    mov di, [es:si + 12]
.n:
    add si, 14
    loop .e
    mov ax, di
    jmp short .out
.zero:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; LoadIconHandler (USER.456) - args: +0 fNew, +2 hResource: an RT_ICON
; resource IS an icon here (SPEC.md 124.18.4), so the handle is the icon
W16SYS w16i_ichandler
    ICALL w16_args
    mov ax, [es:bx + 2]
    xor dx, dx
    ret

; =============================================================================
; MDI (SPEC.md 124.27.2-124.27.5)
; =============================================================================

WM_CHILDACTIVATE  equ 0x0022
WM_MDICREATE      equ 0x0220
WM_MDIDESTROY     equ 0x0221
WM_MDIACTIVATE    equ 0x0222
WM_MDIRESTORE     equ 0x0223
WM_MDINEXT        equ 0x0224
WM_MDIMAXIMIZE    equ 0x0225
WM_MDITILE        equ 0x0226
WM_MDICASCADE     equ 0x0227
WM_MDIICONARRANGE equ 0x0228
WM_MDIGETACTIVE   equ 0x0229
WM_MDISETMENU     equ 0x0230
SC_NEXTWINDOW     equ 0xF040
SC_PREVWINDOW     equ 0xF050
VK_F6             equ 0x75
COLOR_ACTIVECAPTION   equ 2
COLOR_INACTIVECAPTION equ 3
COLOR_CAPTIONTEXT     equ 9
COLOR_ACTIVEBORDER    equ 10
COLOR_INACTIVEBORDER  equ 11
COLOR_APPWORKSPACE    equ 12
W16I_CHSTYLE  equ 0x54CF            ; an MDI child's style, high word: WS_CHILD
                                    ;   WS_VISIBLE WS_CLIPSIBLINGS WS_CAPTION
                                    ;   WS_SYSMENU WS_THICKFRAME and both boxes
W16I_FRAME    equ 4                 ; its frame (w16_bord's for WS_THICKFRAME)
W16I_NLIST    equ 9                 ; children the Window menu lists
W16I_ICON     equ 32                ; a minimized child: an icon's side,
W16I_TSIDE    equ 20                ;   the room its title has each side,
W16I_ISTEP    equ 72                ;   and the step between two icons
WS_MAXIMIZE_HI equ 0x0100
WS_MINIMIZEBOX_HI equ 0x0002
WS_MAXIMIZEBOX_HI equ 0x0001
W16I_TRACK    equ 2                 ; w16_mdion: a move or a size is followed
W16I_TMOVE    equ 1                 ; w16i_tm: a move,
W16I_TL       equ 2                 ;   or the edges a size moves,
W16I_TT       equ 4
W16I_TR       equ 8
W16I_TB       equ 16
W16I_TICON    equ 0x40              ;   and an icon's: a click restores it
SC_SIZE       equ 0xF000
SC_MOVE       equ 0xF010
SC_MINIMIZE   equ 0xF020
SC_MAXIMIZE   equ 0xF030
SC_RESTORE    equ 0xF120
SIZE_MINIMIZED equ 1
SIZE_MAXIMIZED equ 2


; an MDICLIENT's own state, in its W16W_CS
MC_WMENU    equ W16W_CS + 0         ; CLIENTCREATESTRUCT's hWindowMenu
MC_FIRST    equ W16W_CS + 2         ; ...and idFirstChild
MC_ACT      equ W16W_CS + 4         ; the active child's HWND, 0 none
MC_N        equ W16W_CS + 6         ; its children: IDs MC_FIRST and on
MC_FL       equ W16W_CS + 8         ; MCF_*
MCF_SEP     equ 1                   ; the Window menu has the list's separator
MC_MAX      equ W16W_CS + 10        ; the maximized child's HWND, 0 none
MC_SYSM     equ W16W_CS + 12        ; ...and the system menu merged into the
                                    ;   frame's bar for it, 0 none
; a child's own, in its W16W_CS: where it goes back to when restored
RS_X        equ W16W_CS + 0
RS_Y        equ W16W_CS + 2
RS_CX       equ W16W_CS + 4
RS_CY       equ W16W_CS + 6

; -----------------------------------------------------------------------------
; w16i_mproc - MDICLIENT's window procedure: BX = the client, [w16_sm_*] the
; message (w16d_ctlproc's dispatch). DX:AX = the answer. Keeps BX, SI, DI.
; A message that runs a program's procedure (WM_MDICREATE's CreateWindow,
; WM_MDIACTIVATE's own messages) arrives in an import - the program's
; SendMessage, DispatchMessage or DefFrameProc - and calls it back through
; SPEC.md 124.2.2, the answer coming from the last continuation
; -----------------------------------------------------------------------------
w16i_mproc:
    push bx
    push si
    push di
    push es
    mov cx, [w16_sm_msg]
    cmp cx, WM_CREATE
    je .create
    cmp cx, WM_PAINT
    je .paint
    cmp cx, WM_SETFOCUS
    je .focus
    cmp cx, WM_MDIGETACTIVE
    je .getact
    mov ax, [w16_sm_wp]
    cmp cx, WM_MDICREATE
    je .mdicreate
    cmp cx, WM_MDIDESTROY
    je .mdidestroy
    cmp cx, WM_MDIACTIVATE
    je .mdiactivate
    cmp cx, WM_MDINEXT
    je .mdinext
    cmp cx, WM_MDISETMENU
    je .mdisetmenu
    cmp cx, WM_MDIMAXIMIZE
    je .mdimax
    cmp cx, WM_MDIRESTORE
    je .mdirest
    cmp cx, WM_MDITILE
    je .mditile
    cmp cx, WM_MDICASCADE
    je .mdicasc
    cmp cx, WM_MDIICONARRANGE
    je .mdiarr
.zero:
    xor ax, ax
    cwd
.out:
    pop es
    pop di
    pop si
    pop bx
    ret
.create:
    les si, [w16_sm_lp]             ; a CREATESTRUCT, whose lpCreateParams
    les si, [es:si]                 ;   is a CLIENTCREATESTRUCT
    mov ax, es
    or ax, si
    jz .cdef
    mov ax, [es:si]
    mov [bx + MC_WMENU], ax
    mov ax, [es:si + 2]
    mov [bx + MC_FIRST], ax
.cdef:
    mov word [bx + W16W_HBR], COLOR_APPWORKSPACE + 1
    and byte [bx + W16W_STYLE + 2], ~(WS_VSCROLL_HI | WS_HSCROLL_HI)
    xor ax, ax                      ; its bars hidden, their range empty,
    mov [bx + W16W_SBH + 4], ax     ;   until its children reach past it
    mov [bx + W16W_SBV + 4], ax
    mov byte [w16_mdion], 1
    jmp .zero
.paint:
    SLCALL w16d_popen
    jc .zero
    ICALL w16_pkidsall              ; its children are drawn after it,
    and byte [bx + W16W_FLAGS], ~(W16WF_INVAL | W16WF_ERASE) ; not it again
    push bx
    mov si, COLOR_APPWORKSPACE
    mov ax, [w16d_l]
    mov bx, [w16d_t]
    mov cx, [w16d_r]
    mov dx, [w16d_b]
    call w16i_box
    pop bx
    SLCALL w16d_pclose
    jmp .zero
.focus:
    mov ax, [bx + MC_ACT]           ; the active child has it
    or ax, ax
    jz .zero
    ICALL w16_setfocus
    jmp .zero
.getact:
    mov ax, [bx + MC_ACT]
    xor dx, dx
    jmp .out
.mdicreate:
    call w16i_mdicreate
    jmp .out
.mdidestroy:
    call w16i_mdidestroy
    jmp .out
.mdiactivate:
    call w16i_activate
    jmp .out
.mdinext:
    mov dx, [w16_sm_lp]
    call w16i_next
    jmp .out
.mdisetmenu:
    call w16i_setmenu
    jmp .out
.mdimax:
    ICALL w16_hwnd2rec
    jc .z2
    call w16i_max
.z2:
    jmp .zero
.mdirest:
    ICALL w16_hwnd2rec
    jc .z2
    call w16i_restore
    jmp .zero
.mditile:
    call w16i_tile
    jmp short .one
.mdicasc:
    call w16i_cascade
    jmp short .one
.mdiarr:
    call w16i_arrange
.one:
    mov ax, 1
    cwd
    jmp .out

; w16i_box - AX/BX/CX/DX (screen, inclusive) filled in the COLOR_* SI, as the
; controls draw it. Keeps every register
w16i_box:
    push ax
    mov ax, si
    SLCALL w16d_syscol
    mov [w16_gcol], al
    pop ax
    ICALL w16_gfill
    ret

; -----------------------------------------------------------------------------
; w16i_press - from w16_mouse: BX = the window a left press is on,
; [w16_mx]/[w16_my] where (SPEC.md 124.27.4). Inside an MDI child that is
; not the active one, WM_MDIACTIVATE is posted to its client ahead of the
; press, as Windows activated a child clicked. A press on the child's frame
; or caption, outside its client, is the child's own and not the program's:
; CF=0, taken. CF=1 the program's. Keeps every register
; -----------------------------------------------------------------------------
w16i_press:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                      ; DI = the window pressed
.up:
    mov cx, bx
    ICALL w16_parent
    jc .no
    cmp byte [bx + W16W_KIND], W16Q_MDI
    jne .up
    push bx                         ; BX = the client, CX = the child
    mov bx, cx
    ICALL w16_rec2hwnd
    pop bx
    cmp ax, [bx + MC_ACT]
    je .frame
    push word [w16_sm_hwnd]         ; the press's own message survives
    push word [w16_sm_msg]
    push word [w16_sm_wp]
    push word [w16_sm_lp]
    push word [w16_sm_lp + 2]
    mov [w16_sm_wp], ax
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_MDIACTIVATE
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    ICALL w16_post
    pop word [w16_sm_lp + 2]
    pop word [w16_sm_lp]
    pop word [w16_sm_wp]
    pop word [w16_sm_msg]
    pop word [w16_sm_hwnd]
.frame:
    cmp di, cx                      ; on the child itself, outside its
    jne .no                         ;   client: its frame
    mov bx, cx
    ICALL w16_origin                ; AX/DX = the client, CX/SI its size
    jc .no
    mov bx, [w16_mx]
    sub bx, ax
    jl .taken
    cmp bx, cx
    jge .taken
    mov bx, [w16_my]
    sub bx, dx
    jl .taken
    cmp bx, si
    jl .no
.taken:
    mov bx, di
    call w16i_ncpress
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16i_ncpaint - BX = a child with a thick frame or a caption, an MDI
; child's: its frame and caption drawn (SPEC.md 124.27.3), MEASURED on
; Windows 3.0's Program Manager on VGA - a black line, two of the border's
; colour, a black line; the caption the frame's width in, a black line under
; it; the system-menu box at its left, a light grey square with a bar in it;
; the maximize and minimize boxes at its right, bevelled, with their arrows;
; the title centred between, in the active or the inactive colours. Keeps
; every register
; -----------------------------------------------------------------------------
w16i_ncpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jnz .gone                       ; maximized: all of it outside the client
    call w16i_outer
    jc .gone
    mov byte [cs:w16i_act], 0       ; active: its client's MC_ACT
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_parent
    jc .np
    cmp byte [bx + W16W_KIND], W16Q_MDI
    jne .foc
    cmp ax, [bx + MC_ACT]
    jne .np
.act:
    mov byte [cs:w16i_act], 1
    jmp short .np
.foc:
    cmp ax, [w16_focus]             ; another child (an icon of Program
    je .act                         ;   Manager's): while it has the focus
.np:
    pop bx
    xor al, al                      ; a DC, its clip the window cut to its
    ICALL w16_dcopen                ;   parent's client
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jz .frame
    call w16i_icon
    jmp short .close
.frame:
    call w16i_ncclip
    jc .close
    push word [w16_bkmode]
    mov byte [w16_bkmode], OPAQUE
    push bx
    mov ax, [cs:w16i_x0]            ; the outer black line
    mov bx, [cs:w16i_y0]
    mov cx, [cs:w16i_x1]
    mov dx, [cs:w16i_y1]
    mov si, COLOR_WINDOWFRAME
    mov di, 1
    call w16i_ring
    cmp word [cs:w16i_bw], W16I_FRAME
    jne .cap
    inc ax                          ; a thick frame: two in the border's
    inc bx                          ;   colour, and a black line
    dec cx
    dec dx
    mov si, COLOR_INACTIVEBORDER
    cmp byte [cs:w16i_act], 0
    je .ib
    mov si, COLOR_ACTIVEBORDER
.ib:
    mov di, 2
    call w16i_ring
    add ax, 2
    add bx, 2
    sub cx, 2
    sub dx, 2
    mov si, COLOR_WINDOWFRAME
    mov di, 1
    call w16i_ring
.cap:
    pop bx
    cmp word [cs:w16i_cap], 0
    je .done
    call w16i_caption
.done:
    pop word [w16_bkmode]
.close:
    ICALL w16_dcclose
.gone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_outer - BX = a child: [cs:w16i_x0]..[cs:w16i_y1] its window on the
; screen, inclusive, [cs:w16i_bw] its frame and [cs:w16i_cap] its caption.
; CF=1 it is not shown. Keeps every register
w16i_outer:
    push ax
    push cx
    push dx
    push si
    ICALL w16_origin                ; AX/DX = its client's top left
    jc .out
    push ax
    ICALL w16_capt
    mov [cs:w16i_cap], ax
    sub dx, ax
    ICALL w16_bord
    mov [cs:w16i_bw], ax
    sub dx, ax
    pop cx
    sub cx, ax
    mov [cs:w16i_x0], cx
    mov [cs:w16i_y0], dx
    add cx, [bx + W16W_CX]
    dec cx
    mov [cs:w16i_x1], cx
    add dx, [bx + W16W_CY]
    dec dx
    mov [cs:w16i_y1], dx
    clc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16i_icon - BX = a minimized child, w16i_outer's rectangle its icon's, a DC
; open: its class's icon, and its title under it in the caption's colours,
; as Windows 3.0 drew an iconic MDI child (SPEC.md 124.27.5). A class with
; no icon gets a frame. Keeps every register
w16i_icon:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push word [cs:w16i_x0]
    push word [cs:w16i_y0]
    mov ax, [cs:w16i_x0]            ; the clip: the icon and the title's
    sub ax, W16I_TSIDE              ;   room under it
    mov [cs:w16i_tl], ax
    mov [cs:w16i_x0], ax
    mov ax, [cs:w16i_x1]
    add ax, W16I_TSIDE
    mov [cs:w16i_tr], ax
    mov [cs:w16i_x1], ax
    mov ax, [cs:w16i_y1]
    add ax, 2
    mov [cs:w16i_ct], ax
    add ax, [w16_capn]
    sub ax, 3
    mov [cs:w16i_cb], ax
    mov [cs:w16i_y1], ax
    sub ax, [cs:w16i_ct]
    inc ax
    mov [cs:w16i_bsz], ax
    call w16i_ncclip
    pop dx                          ; CX/DX = the icon's top left
    pop cx
    jc .out
    mov al, [bx + W16W_CLS]         ; its class's icon
    mov ah, W16C_SIZE
    mul ah
    mov si, ax
    mov ax, [si + w16_cls + W16C_ICON]
    or ax, ax
    jnz .draw
    push ax                         ; none: a program's own child paints
    ICALL w16_bord                  ;   itself, as Program Manager's items
    or ax, ax                       ;   do; an MDI child, whose client is
    pop ax                          ;   empty, gets a frame
    jz .title
    jmp short .box
.draw:
    push bx                         ; CX/DX = the icon's top left
    mov bl, W16_NOCOL
    SLCALL w16_icodraw
    pop bx
    jnc .title
.box:
    push bx                         ; none: a white square, framed
    mov ax, cx
    mov bx, dx
    add cx, W16I_ICON - 1
    add dx, W16I_ICON - 1
    mov si, COLOR_WINDOWFRAME
    mov di, 1
    call w16i_ring
    inc ax
    inc bx
    dec cx
    dec dx
    mov si, COLOR_WINDOW
    call w16i_box
    pop bx
.title:
    mov byte [cs:w16i_tfit], 1
    call w16i_title
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_ncclip - the DC's clip made the window w16i_ncpaint measured, cut to
; its parent's client; BX = the window. CF=1 none of it shows. Keeps every
; register
w16i_ncclip:
    push ax
    push bx
    mov ax, [cs:w16i_x0]
    mov [w16_cl], ax
    mov ax, [cs:w16i_y0]
    mov [w16_ct], ax
    mov ax, [cs:w16i_x1]
    mov [w16_cr], ax
    mov ax, [cs:w16i_y1]
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

; w16i_ring - AX/BX/CX/DX = a rectangle's outer edge (screen, inclusive), DI
; a width, SI a COLOR_*: a ring of it inside the edge. Keeps every register
w16i_ring:
    push ax
    push bx
    push cx
    push dx
    push dx                         ; the top band
    mov dx, bx
    add dx, di
    dec dx
    call w16i_box
    pop dx
    push bx                         ; the bottom band
    mov bx, dx
    sub bx, di
    inc bx
    call w16i_box
    pop bx
    add bx, di                      ; the sides, between them
    sub dx, di
    cmp bx, dx
    jg .out
    push cx                         ; the left
    mov cx, ax
    add cx, di
    dec cx
    call w16i_box
    pop cx
    mov ax, cx                      ; the right
    sub ax, di
    inc ax
    call w16i_box
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_caption - BX = the window: its caption, inside the frame
; w16i_ncpaint measured. The boxes are its rows less one square
w16i_caption:
    mov ax, [cs:w16i_x0]            ; CL..CR, CT..CB: the caption's face
    add ax, [cs:w16i_bw]
    mov [cs:w16i_cl], ax
    mov ax, [cs:w16i_x1]
    sub ax, [cs:w16i_bw]
    mov [cs:w16i_cr], ax
    mov ax, [cs:w16i_y0]
    add ax, [cs:w16i_bw]
    mov [cs:w16i_ct], ax
    add ax, [cs:w16i_cap]
    sub ax, 2
    mov [cs:w16i_cb], ax
    sub ax, [cs:w16i_ct]
    inc ax
    mov [cs:w16i_bsz], ax           ; a box: as wide as the face is high
    push bx
    mov ax, [cs:w16i_cl]            ; the black line under it
    mov bx, [cs:w16i_cb]
    inc bx
    mov cx, [cs:w16i_cr]
    mov dx, bx
    mov si, COLOR_WINDOWFRAME
    call w16i_box
    pop bx
    mov ax, [cs:w16i_cl]            ; TL..TR: the title's, so far
    mov [cs:w16i_tl], ax
    mov ax, [cs:w16i_cr]
    mov [cs:w16i_tr], ax
    test word [bx + W16W_STYLE + 2], 0x0008 ; WS_SYSMENU: its box at the left
    jz .nosys
    mov ax, [cs:w16i_cl]
    call w16i_sysbox
    mov ax, [cs:w16i_cl]
    add ax, [cs:w16i_bsz]
    call w16i_sep
    inc ax
    mov [cs:w16i_tl], ax
.nosys:
    test word [bx + W16W_STYLE + 2], 0x0001 ; WS_MAXIMIZEBOX
    jz .nomax
    mov ax, [cs:w16i_tr]
    sub ax, [cs:w16i_bsz]
    inc ax
    xor dl, dl                      ; its arrow up
    call w16i_button
    dec ax
    call w16i_sep
    dec ax
    mov [cs:w16i_tr], ax
.nomax:
    test word [bx + W16W_STYLE + 2], 0x0002 ; WS_MINIMIZEBOX
    jz .nomin
    mov ax, [cs:w16i_tr]
    sub ax, [cs:w16i_bsz]
    inc ax
    mov dl, 1                       ; its arrow down
    call w16i_button
    dec ax
    call w16i_sep
    dec ax
    mov [cs:w16i_tr], ax
.nomin:
    jmp w16i_title

; w16i_sep - AX = a column: a black line down the caption's face there.
; Keeps every register
w16i_sep:
    push bx
    push cx
    push dx
    push si
    mov bx, [cs:w16i_ct]
    mov cx, ax
    mov dx, [cs:w16i_cb]
    mov si, COLOR_WINDOWFRAME
    call w16i_box
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16i_sysbox - AX = its left: the system-menu box, a light grey square
; with the bar a child's system menu shows, black with a white middle and a
; grey shadow right and under. Keeps every register
w16i_sysbox:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax
    mov bx, [cs:w16i_ct]
    mov cx, ax
    add cx, [cs:w16i_bsz]
    dec cx
    mov dx, [cs:w16i_cb]
    mov si, COLOR_BTNFACE
    call w16i_box
    mov ax, [cs:w16i_bsz]           ; the bar's top left: (box - 6) / 2 in,
    sub ax, 6                       ;   (box - 4) / 2 down
    sar ax, 1
    add di, ax
    mov ax, [cs:w16i_bsz]
    sub ax, 4
    sar ax, 1
    add ax, [cs:w16i_ct]
    mov bx, ax
    mov ax, di                      ; black, 7 by 3
    lea cx, [di + 6]
    lea dx, [bx + 2]
    mov si, COLOR_WINDOWFRAME
    call w16i_box
    inc ax                          ; its middle row white
    inc bx
    dec cx
    mov dx, bx
    mov si, COLOR_WINDOW
    call w16i_box
    lea ax, [di + 7]                ; the shadow: right, then under
    mov cx, ax
    lea dx, [bx + 2]
    mov si, COLOR_BTNSHADOW
    call w16i_box
    lea ax, [di + 1]
    add bx, 2
    mov dx, bx
    call w16i_box
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_button - AX = its left, DL = 0 an arrow up / 1 down: a caption box,
; bevelled - white top and left, two rows and columns of shadow right and
; under - with its arrow, 1, 3, 5 and 7 wide, centred. Keeps every register
w16i_button:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push dx
    mov di, ax                      ; DI = its left, SI its right
    mov si, ax
    add si, [cs:w16i_bsz]
    dec si
    push si
    mov ax, di                      ; the face
    inc ax
    mov bx, [cs:w16i_ct]
    inc bx
    mov cx, si
    sub cx, 2
    mov dx, [cs:w16i_cb]
    sub dx, 2
    mov si, COLOR_BTNFACE
    call w16i_box
    pop si
    push si
    mov ax, di                      ; white: the top row, then the left
    mov bx, [cs:w16i_ct]
    lea cx, [si - 1]
    mov dx, bx
    mov si, COLOR_WINDOW
    call w16i_box
    inc bx
    mov cx, ax
    mov dx, [cs:w16i_cb]
    dec dx
    call w16i_box
    pop si
    push si
    mov ax, si                      ; the shadow: the right two columns,
    dec ax                          ;   the top-right pixel, the two rows
    mov bx, [cs:w16i_ct]            ;   under
    inc bx
    mov cx, si
    mov dx, [cs:w16i_cb]
    mov si, COLOR_BTNSHADOW
    call w16i_box
    pop cx
    push cx
    mov ax, cx
    dec bx
    mov dx, bx
    call w16i_box
    lea ax, [di + 1]
    mov bx, [cs:w16i_cb]
    dec bx
    mov dx, bx
    call w16i_box
    mov ax, di
    inc bx
    mov dx, bx
    call w16i_box
    pop si
    pop dx                          ; the arrow: four rows from the box's
    mov ax, [cs:w16i_bsz]           ;   (box - 8) / 2 + 1, centred on
    shr ax, 1                       ;   box / 2 - 1
    dec ax
    add di, ax                      ; DI = its middle column
    mov ax, [cs:w16i_bsz]
    sub ax, 8
    shr ax, 1
    inc ax
    add ax, [cs:w16i_ct]
    mov bx, ax                      ; BX = its first row
    xor cx, cx                      ; CX = the half-width
    mov si, COLOR_BTNTEXT
    or dl, dl
    jz .row
    inc bx                          ; down: one lower, widest first
    mov cx, 3
.row:
    push cx
    push dx
    mov ax, di
    sub ax, cx
    add cx, di
    mov dx, bx
    call w16i_box
    pop dx
    pop cx
    inc bx
    or dl, dl
    jnz .dn
    inc cx
    cmp cx, 4
    jb .row
    jmp short .out
.dn:
    dec cx
    jns .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_title - BX = the window: its text centred between [w16i_tl] and
; [w16i_tr], cut to fit, the face filled around it: COLOR_CAPTIONTEXT on
; COLOR_ACTIVECAPTION for the active child, the window's text colour on
; COLOR_INACTIVECAPTION for another. Keeps every register
w16i_title:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, COLOR_INACTIVECAPTION
    mov ax, (COLOR_INACTIVECAPTION << 8) | COLOR_WINDOWTEXT
    cmp byte [cs:w16i_act], 0
    je .col
    mov si, COLOR_ACTIVECAPTION
    mov ax, (COLOR_ACTIVECAPTION << 8) | COLOR_CAPTIONTEXT
.col:
    SLCALL w16d_colors              ; [w16_tcol], [w16_tpap]
    mov [cs:w16i_tpc], si
    ICALL w16_textptr               ; ES:SI, CX = the title
    cmp cx, W16_TXTMAX
    jbe .n
    mov cx, W16_TXTMAX
.n:
    mov di, w16_txt
    push cx
    jcxz .cpd
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
.cpd:
    pop cx
    mov di, [cs:w16i_tr]            ; DI = the room
    sub di, [cs:w16i_tl]
    inc di
    xor ax, ax
.fit:
    jcxz .fits
    mov si, w16_txt
    ICALL w16_textw                 ; AX = its width
    cmp ax, di
    jle .fits
    dec cx
    jmp short .fit
.fits:
    mov [w16_tn], cx
    sub di, ax                      ; centred
    sar di, 1
    add di, [cs:w16i_tl]
    mov [w16_tx], di
    cmp byte [cs:w16i_tfit], 0      ; an icon's: only the text's own box,
    je .fill                        ;   a pixel more each side
    jcxz .out
    dec di
    mov [cs:w16i_tl], di
    add di, ax
    inc di
    mov [cs:w16i_tr], di
.fill:
    push bx                         ; the face, then the text over it in
    mov si, [cs:w16i_tpc]           ;   the same paper
    mov ax, [cs:w16i_tl]
    mov bx, [cs:w16i_ct]
    mov cx, [cs:w16i_tr]
    mov dx, [cs:w16i_cb]
    cmp ax, cx
    jg .nofill
    call w16i_box
.nofill:
    pop bx
    cmp word [w16_tn], 0
    je .out
    mov ax, [w16_capn]              ; the System font's rows are the
    sub ax, 3                       ;   caption's less 3: (box - rows) / 2
    neg ax                          ;   down
    add ax, [cs:w16i_bsz]
    sar ax, 1
    add ax, [cs:w16i_ct]
    mov [w16_ty], ax
    push bp
    ICALL w16_txtdraw
    pop bp
.out:
    mov byte [w16_thalf], 0
    mov byte [cs:w16i_tfit], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
w16i_tpc:  dw 0                     ; w16i_title's paper, a COLOR_*
w16i_tfit: db 0                     ; 1: an icon's title, the text's box only

w16i_x0:  dw 0                      ; w16i_ncpaint's: the window on the screen,
w16i_y0:  dw 0                      ;   inclusive,
w16i_x1:  dw 0
w16i_y1:  dw 0
w16i_bw:  dw 0                      ;   its frame, its caption,
w16i_cap: dw 0
w16i_cl:  dw 0                      ;   the caption's face,
w16i_ct:  dw 0
w16i_cr:  dw 0
w16i_cb:  dw 0
w16i_bsz: dw 0                      ;   a box's side,
w16i_tl:  dw 0                      ;   the title's room
w16i_tr:  dw 0
w16i_act: db 0                      ;   and 1 for the active child

; -----------------------------------------------------------------------------
; WM_MDICREATE: BX = the client, [w16_sm_lp] its MDICREATESTRUCT -
; CreateWindow, called back through the program's stack (SPEC.md 124.2.2)
; as the program would call it, its thirty bytes of arguments laid there;
; its HWND answers, after the child is made the active one
; -----------------------------------------------------------------------------
w16i_mdicreate:
    mov si, w16i_mcr_c
    mov ax, [w16_sm_hwnd]           ; the continuation's word: the client
    xor cx, cx
    ICALL w16_cbopen
    sub word [w16_psp_], 30         ; CreateWindow's arguments, the last
    mov di, [w16_psp_]              ;   pushed first
    mov es, [w16_pss]
    push ds
    lds si, [w16_sm_lp]             ; DS:SI = the MDICREATESTRUCT
    mov ax, [si]                    ; szClass
    mov [es:di + 26], ax
    mov ax, [si + 2]
    mov [es:di + 28], ax
    mov ax, [si + 4]                ; szTitle
    mov [es:di + 22], ax
    mov ax, [si + 6]
    mov [es:di + 24], ax
    mov ax, [si + 8]                ; hOwner
    mov [es:di + 4], ax
    mov ax, [si + 10]               ; x, y, cx, cy
    mov [es:di + 16], ax
    mov ax, [si + 12]
    mov [es:di + 14], ax
    mov ax, [si + 14]
    mov [es:di + 12], ax
    mov ax, [si + 16]
    mov [es:di + 10], ax
    mov ax, [si + 18]               ; the style, made an MDI child's
    mov [es:di + 18], ax
    mov ax, [si + 20]
    or ax, W16I_CHSTYLE
    mov [es:di + 20], ax
    mov [es:di], si                 ; lpParam: the MDICREATESTRUCT, as
    mov [es:di + 2], ds             ;   Windows' CREATESTRUCT carried it
    pop ds
    mov ax, [w16_sm_hwnd]
    mov [es:di + 8], ax             ; hWndParent: the client
    mov ax, [bx + MC_FIRST]
    add ax, [bx + MC_N]
    mov [es:di + 6], ax             ; hMenu: its ID
    call w16i_defpos
    mov ax, w16_stubs + 6 * W16_CWROW
    mov dx, ds
    ICALL w16_cbcall
    xor ax, ax
    cwd
    ret

; w16i_defpos - ES:DI = CreateWindow's arguments for a new child of the
; client BX: CW_USEDEFAULT's place and size made Windows' stagger
; (MDI_CalcDefaultChildPos): a step of SM_CYCAPTION + SM_CYFRAME - 1, as many
; places down as a third of the client's height holds, the size what the
; last place leaves. Keeps BX, DI, ES
w16i_defpos:
    mov ax, [bx + MC_N]
    call w16i_stagger
    cmp word [es:di + 12], CW_USEDEFAULT
    jne .pos
    mov [es:di + 12], cx
    mov [es:di + 10], dx
.pos:
    cmp word [es:di + 16], CW_USEDEFAULT
    jne .out
    mov [es:di + 16], ax
    mov [es:di + 14], ax
.out:
    ret

; w16i_stagger - BX = a client, AX = a place's number: AX = its offset, the
; same across and down, and CX/DX the size each place has - Windows' stagger
; (MDI_CalcDefaultChildPos): a step of SM_CYCAPTION + SM_CYFRAME - 1, as many
; places past the first as a third of the client's height holds, the number
; taken modulo them, and the size what the last place leaves. Keeps BX, SI,
; DI, ES
w16i_stagger:
    push si
    push di
    push ax
    ICALL w16_csize                 ; CX/DX = the client's size
    mov si, [w16_capn]
    add si, W16I_FRAME              ; SI = the step
    push cx
    push dx
    mov ax, dx
    mov cx, si
    add cx, si
    add cx, si
    xor dx, dx
    div cx
    mov di, ax                      ; DI = the places past the first
    mul si                          ; AX = the last place's offset
    pop dx
    sub dx, ax
    pop cx
    sub cx, ax
    pop ax
    push cx
    push dx
    lea cx, [di + 1]
    xor dx, dx
    div cx
    mov ax, dx
    mul si
    pop dx
    pop cx
    pop di
    pop si
    ret

; ...CreateWindow answered: AX = the child (0 refused), CX = the client
SCONT w16i_mcr
    or ax, ax
    jz .none
    push ax
    mov ax, cx
    ICALL w16_hwnd2rec              ; BX = the client
    pop ax
    jc .none
    inc word [bx + MC_N]
    call w16i_ncowe
    jmp w16i_activate
.none:
    xor ax, ax
    cwd
    ret

; -----------------------------------------------------------------------------
; w16i_activate - BX = the client, AX = the child to activate (0 none): it is
; brought to the top and drawn whole, both captions are owed, the Window menu
; checks it, and WM_MDIACTIVATE goes to the one leaving (wParam FALSE) and to
; it (TRUE), lParam the new one and the old one, as Windows 3.0 sent them;
; then it has the focus. Answers the active child. One chain at a time
; (w16i_aold, w16i_acli)
; -----------------------------------------------------------------------------
w16i_activate:
    mov dx, [bx + MC_ACT]
    cmp ax, dx
    je .same
    cmp word [bx + MC_MAX], 0       ; one maximized: this one is, instead
    je .nmax
    or ax, ax
    jz .nmax
    push bx
    ICALL w16_hwnd2rec
    jc .nm
    call w16i_max
.nm:
    pop bx
.nmax:
    mov [bx + MC_ACT], ax
    mov [w16i_aold], dx
    push ax
    ICALL w16_rec2hwnd
    mov [w16i_acli], ax
    pop ax
    call w16i_raise
    call w16i_ncowe
    xchg ax, dx
    call w16i_ncowe
    xchg ax, dx
    call w16i_wmenu
    mov ax, [w16i_aold]             ; the one leaving first
    or ax, ax
    jz w16i_act1
    ICALL w16_hwnd2rec
    jc w16i_act1
    xor cx, cx
    mov si, w16i_act1_c
    jmp short w16i_amsg
.same:
    call w16i_raise
    xor dx, dx
    ret

; w16i_amsg - BX = a child, CX = wParam, SI = the continuation: WM_MDIACTIVATE
; sent it, lParam the activation's two
w16i_amsg:
    push bx
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_MDIACTIVATE
    mov [w16_sm_wp], cx
    mov ax, [w16i_acli]
    ICALL w16_hwnd2rec
    mov ax, [bx + MC_ACT]
    mov [w16_sm_lp], ax
    mov ax, [w16i_aold]
    mov [w16_sm_lp + 2], ax
    pop bx
    xor ax, ax
    ICALL w16_sendwnd
    ret

SCONT w16i_act1                     ; ...then the one arriving
    mov ax, [w16i_acli]
    ICALL w16_hwnd2rec
    jc w16i_act2
    mov ax, [bx + MC_ACT]
    or ax, ax
    jz w16i_act2
    ICALL w16_hwnd2rec
    jc w16i_act2
    mov cx, 1
    mov si, w16i_act2_c
    jmp short w16i_amsg

SCONT w16i_act2                     ; ...and it has the focus
    mov ax, [w16i_acli]
    ICALL w16_hwnd2rec
    mov ax, 0
    jc .out
    mov ax, [bx + MC_ACT]
    or ax, ax
    jz .out
    push ax
    ICALL w16_setfocus
    pop ax
.out:
    xor dx, dx
    ret

; w16i_raise - AX = a child (0 none): on top of its siblings (W16W_SEQ, the
; Z order) and, if it was not, all of it invalid. Keeps every register
w16i_raise:
    push ax
    push bx
    push cx
    push dx
    push si
    or ax, ax
    jz .out
    ICALL w16_hwnd2rec
    jc .out
    mov cx, [w16_seq]
    dec cx
    cmp [bx + W16W_SEQ], cx         ; the last made or raised: on top now
    je .out
    inc cx
    inc word [w16_seq]
    mov [bx + W16W_SEQ], cx
    ICALL w16_ncmark
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    ICALL w16_invaltree
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_ncowe - AX = a window (0 none): its frame and caption owed. Keeps
; every register
w16i_ncowe:
    push bx
    or ax, ax
    jz .out
    ICALL w16_hwnd2rec
    jc .out
    or byte [bx + W16W_F2], W16W2_NCDIRTY
.out:
    pop bx
    ret

; w16i_child - BX = the client, AX = an ID -> AX = its child (CF=1 none);
; with AX = 0FFFFh and DX = a SEQ instead: the top child below it. Keeps BX
w16i_child:
    push bx
    push cx
    push si
    push di
    push ax
    ICALL w16_rec2hwnd
    mov di, ax                      ; DI = the client
    pop ax
    mov si, w16_wnd
    mov cx, W16_MAXWND
.r:
    test byte [si + W16W_FLAGS], W16WF_USED
    jz .n
    test word [si + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    cmp [si + W16W_PARENT], di
    jne .n
    cmp [si + W16W_MENU], ax
    je .hit
.n:
    add si, W16W_SIZE
    loop .r
    stc
    jmp short .out
.hit:
    mov bx, si
    ICALL w16_rec2hwnd
    clc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; w16i_kids - BX = the client: CX = its children, DI = the top one's
; record (0 none), SI = the bottom one's. Keeps AX, BX, DX
w16i_kids:
    push ax
    push dx
    push bp
    ICALL w16_rec2hwnd
    mov dx, ax
    xor cx, cx
    xor di, di
    xor si, si
    mov bp, w16_wnd
.r:
    test byte [ds:bp + W16W_FLAGS], W16WF_USED
    jz .n
    test word [ds:bp + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    cmp [ds:bp + W16W_PARENT], dx
    jne .n
    inc cx
    or di, di
    jz .top
    mov ax, [ds:bp + W16W_SEQ]
    cmp ax, [di + W16W_SEQ]
    jb .bot
.top:
    mov di, bp
.bot:
    or si, si
    jz .low
    mov ax, [ds:bp + W16W_SEQ]
    cmp ax, [si + W16W_SEQ]
    jae .n
.low:
    mov si, bp
.n:
    add bp, W16W_SIZE
    cmp bp, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .r
    pop bp
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; WM_MDIDESTROY: BX = the client, AX = the child - DestroyWindow, called back
; through the program's stack as WM_MDICREATE calls CreateWindow; then the
; IDs closed up, the Window menu again, and the top child left made active
; -----------------------------------------------------------------------------
w16i_mdidestroy:
    push bx
    ICALL w16_hwnd2rec
    mov cx, bx
    pop bx
    jc .none
    cmp [bx + MC_ACT], ax           ; the active one is no longer
    jne .na
    mov word [bx + MC_ACT], 0
.na:
    cmp [bx + MC_MAX], ax           ; nor the maximized one, and the bar
    jne .nmx                        ;   is the frame's own again
    mov word [bx + MC_MAX], 0
    push si
    mov si, bx
    call w16i_unmerge
    pop si
.nmx:
    push ax
    mov si, w16i_mds_c
    ICALL w16_rec2hwnd              ; the continuation's word: the client
    xor cx, cx
    ICALL w16_cbopen
    pop ax
    ICALL w16_ppush                 ; DestroyWindow's one argument
    mov ax, w16_stubs + 6 * W16_DWROW
    mov dx, ds
    ICALL w16_cbcall
.none:
    xor ax, ax
    cwd
    ret

SCONT w16i_mds                      ; ...DestroyWindow answered; CX = the client
    mov ax, cx
    ICALL w16_hwnd2rec
    jc .out
    call w16i_renum
    call w16i_wmenu
    cmp word [bx + MC_ACT], 0
    jne .out
    call w16i_kids                  ; DI = the top child left
    xor ax, ax
    or di, di
    jz .act
    push bx
    mov bx, di
    ICALL w16_rec2hwnd
    pop bx
.act:
    jmp w16i_activate
.out:
    xor ax, ax
    cwd
    ret

; w16i_renum - BX = the client: its children's IDs closed up, MC_FIRST on in
; the order they had, and MC_N their count. Keeps BX
w16i_renum:
    push ax
    push cx
    push dx
    push si
    push di
    ICALL w16_rec2hwnd
    mov dx, ax                      ; DX = the client
    xor cx, cx                      ; CX = the children
    mov si, w16_wnd
.r:
    call w16i_kidof
    jc .n
    inc cx
    push cx                         ; its new ID: the first + how many
    mov ax, [bx + MC_FIRST]         ;   siblings have a smaller one
    mov di, w16_wnd
.c:
    push si
    mov si, di
    call w16i_kidof
    pop si
    jc .cn
    mov cx, [di + W16W_MENU]
    cmp cx, [si + W16W_MENU]
    jae .cn
    inc ax
.cn:
    add di, W16W_SIZE
    cmp di, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .c
    pop cx
    mov [si + W16W_MENU], ax
.n:
    add si, W16W_SIZE
    cmp si, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .r
    mov [bx + MC_N], cx
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16i_kidof - SI = a record, DX = a client's HWND: CF=0 it is one of its
; children. Keeps every register
w16i_kidof:
    test byte [si + W16W_FLAGS], W16WF_USED
    jz .no
    test word [si + W16W_STYLE + 2], WS_CHILD_HI
    jz .no
    cmp [si + W16W_PARENT], dx
    jne .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; WM_MDINEXT: BX = the client, AX = a child (0 the active one), DX = 0 the
; next - the top one below it, and it goes to the bottom - or not 0 the
; previous, the bottom one
; -----------------------------------------------------------------------------
w16i_next:
    or ax, ax
    jnz .have
    mov ax, [bx + MC_ACT]
    or ax, ax
    jz .none
.have:
    push bx
    ICALL w16_hwnd2rec
    mov si, bx                      ; SI = it
    pop bx
    jc .none
    or dx, dx
    jnz .prev
    call w16i_below                 ; DI = the top one below it
    or di, di
    jz .none
    push si                         ; it goes to the bottom
    call w16i_kids                  ; (SI = the bottom one)
    mov ax, [si + W16W_SEQ]
    pop si
    or ax, ax
    jz .z
    dec ax
.z:
    mov [si + W16W_SEQ], ax
    jmp short .go
.prev:
    call w16i_kids                  ; the bottom one comes up
    mov di, si
.go:
    push bx
    mov bx, di
    ICALL w16_rec2hwnd
    pop bx
    jmp w16i_activate
.none:
    xor ax, ax
    cwd
    ret

; w16i_below - BX = the client, SI = a child -> DI = the top child below it,
; 0 none. Keeps the rest
w16i_below:
    push ax
    push cx
    push dx
    push si
    mov cx, [si + W16W_SEQ]
    ICALL w16_rec2hwnd
    mov dx, ax
    xor di, di
    mov si, w16_wnd
.f:
    call w16i_kidof
    jc .n
    mov ax, [si + W16W_SEQ]
    cmp ax, cx
    jae .n
    or di, di
    jz .t
    cmp ax, [di + W16W_SEQ]
    jbe .n
.t:
    mov di, si
.n:
    add si, W16W_SIZE
    cmp si, w16_wnd + W16_MAXWND * W16W_SIZE
    jb .f
    pop si
    pop dx
    pop cx
    pop ax
    ret

; WM_MDISETMENU: BX = the client, AX = wParam (fRefresh), [w16_sm_lp] the
; frame's menu and the Window menu (Windows 3.0's MAKELONG): each that is
; not 0 replaces the old, and the list is put in the Window menu again.
; Answers the frame's menu before
w16i_setmenu:
    push ax
    ICALL w16_parent                ; BX = the frame
    mov dx, [bx + W16W_MENU]
    pop ax
    push dx
    or ax, ax
    jnz .list
    mov ax, [w16_sm_lp]
    or ax, ax
    jz .wm
    mov [bx + W16W_MENU], ax
    SLCALL w16_mbardraw
.wm:
    mov ax, [w16_sm_lp + 2]
    or ax, ax
    jz .list
    push bx
    mov bx, [w16_sm_hwnd]
    xchg ax, bx
    ICALL w16_hwnd2rec
    xchg ax, bx                     ; BX = the client, AX = the new menu
    push ax
    xor ax, ax
    xchg ax, [bx + MC_N]            ; the list out of the old one
    push ax
    call w16i_wmenu
    pop word [bx + MC_N]
    pop word [bx + MC_WMENU]
    pop bx
.list:
    mov ax, [w16_sm_hwnd]
    ICALL w16_hwnd2rec
    call w16i_wmenu
    pop ax
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; w16i_wmenu - BX = the client: the Window menu's list made again - a
; separator, then "&1 title" for each child in ID order, the active one
; checked, at most W16I_NLIST, as Windows 3.0's MDI kept it. Keeps every
; register
; -----------------------------------------------------------------------------
w16i_wmenu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [bx + MC_WMENU]
    or ax, ax
    jz .out
    mov cx, [bx + MC_FIRST]         ; the list out: every ID it can use
.del:
    push cx
    xor dx, dx
    SLCALL w16_mloc
    jc .dn
    mov al, 0
    SLCALL w16_mremove
.dn:
    pop cx
    mov ax, [bx + MC_WMENU]
    inc cx
    mov dx, cx
    sub dx, [bx + MC_FIRST]
    cmp dx, W16I_NLIST
    jb .del
    test byte [bx + MC_FL], MCF_SEP ; and its separator, the last item
    jz .sep
    and byte [bx + MC_FL], ~MCF_SEP
    SLCALL w16_mget
    jc .out
    mov cl, [es:si + MH_N]
    xor ch, ch
    jcxz .sep
    dec cx
    SLCALL w16_mitem
    test word [es:di + MI_FLAGS], MF_SEPARATOR
    jz .sep
    mov al, 0
    SLCALL w16_mremove
    mov ax, [bx + MC_WMENU]
.sep:
    cmp word [bx + MC_N], 0
    je .out
    xor dx, dx                      ; the separator in
    mov [w16_mstr], dx
    mov [w16_mstr + 2], dx
    push bx
    xor bx, bx
    mov dx, MF_SEPARATOR
    SLCALL w16_mappend
    pop bx
    jc .out
    or byte [bx + MC_FL], MCF_SEP
    xor cx, cx                      ; CX = the place
.item:
    cmp cx, [bx + MC_N]
    jae .out
    cmp cx, W16I_NLIST
    jae .out
    mov ax, [bx + MC_FIRST]
    add ax, cx
    call w16i_child                 ; AX = the child
    jc .inext
    call w16i_label                 ; "&n title" at cs:w16i_mbuf
    xor dx, dx
    cmp ax, [bx + MC_ACT]
    jne .unch
    mov dx, MF_CHECKED
.unch:
    mov word [w16_mstr], w16i_mbuf  ; XSEC-OK: a far pointer into this part
    mov [w16_mstr + 2], cs
    push bx
    push cx
    mov ax, [bx + MC_WMENU]
    mov si, [bx + MC_FIRST]
    add si, cx
    mov bx, si                      ; the ID
    SLCALL w16_mappend
    pop cx
    pop bx
.inext:
    inc cx
    jmp short .item
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_label - CX = a place, AX = a child: "&<place + 1> <its text>" into
; cs:w16i_mbuf, cut to fit. Keeps every register
w16i_label:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov di, w16i_mbuf               ; XSEC-OK: written through cs:
    mov byte [cs:di], '&'
    add cl, '1'
    mov [cs:di + 1], cl
    mov byte [cs:di + 2], ' '
    add di, 3
    ICALL w16_hwnd2rec
    jc .end
    ICALL w16_textptr               ; ES:SI, CX = its length
    cmp cx, W16I_MBUF - 4
    jbe .cp
    mov cx, W16I_MBUF - 4
.cp:
    jcxz .end
.c:
    mov al, [es:si]
    mov [cs:di], al
    inc si
    inc di
    loop .c
.end:
    mov byte [cs:di], 0
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
W16I_MBUF equ 40
w16i_mbuf:
    times W16I_MBUF db 0

; =============================================================================
; Moving, sizing, maximizing and minimizing a child (SPEC.md 124.27.5)
; =============================================================================

; w16i_ncpress - BX = an MDI child, the left press [w16_mx]/[w16_my] on its
; frame or caption (or anywhere on its icon): the maximize and minimize boxes
; post the child WM_SYSCOMMAND; the caption and an icon start a move, and a
; thick frame a size, followed by w16i_track until the button comes up.
; Keeps every register
w16i_ncpress:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jnz .out
    call w16i_outer
    jc .out
    mov ax, [w16_mx]
    mov dx, [w16_my]
    mov cl, W16I_TMOVE | W16I_TICON
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .start
    mov si, [cs:w16i_bw]            ; on the caption?
    mov di, [cs:w16i_cap]
    or di, di
    jz .frame
    push dx
    sub dx, [cs:w16i_y0]
    sub dx, si
    jl .notcap
    cmp dx, di
    jge .notcap
    pop dx
    push ax
    sub ax, [cs:w16i_x0]
    sub ax, si
    jl .ncap
    mov cx, [cs:w16i_x1]            ; CX = how far in from the right
    sub cx, si
    sub cx, [w16_mx]
    jl .ncap
    dec di                          ; DI = a box's side
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZEBOX_HI
    jz .nmax
    cmp cx, di
    jge .pmax
    pop ax
    mov cx, SC_MAXIMIZE
    jmp short .sys
.pmax:
    sub cx, di
    dec cx
.nmax:
    test word [bx + W16W_STYLE + 2], WS_MINIMIZEBOX_HI
    jz .move
    cmp cx, di
    jge .move
    pop ax
    mov cx, SC_MINIMIZE
.sys:
    call w16i_psys
    jmp .out
.move:
    pop ax
    mov cl, W16I_TMOVE
    jmp short .start
.ncap:
    pop ax
    jmp short .frame
.notcap:
    pop dx
.frame:
    cmp si, W16I_FRAME              ; a thick frame sizes; a thin one not
    jne .out
    mov si, [cs:w16i_cap]           ; SI = how near a corner counts
    add si, W16I_FRAME
    xor cl, cl
    push ax
    sub ax, [cs:w16i_x0]
    cmp ax, si
    pop ax
    jge .r
    or cl, W16I_TL
.r:
    push ax
    neg ax
    add ax, [cs:w16i_x1]
    cmp ax, si
    pop ax
    jge .t
    or cl, W16I_TR
.t:
    push dx
    sub dx, [cs:w16i_y0]
    cmp dx, si
    pop dx
    jge .b
    or cl, W16I_TT
.b:
    push dx
    neg dx
    add dx, [cs:w16i_y1]
    cmp dx, si
    pop dx
    jge .start
    or cl, W16I_TB
.start:
    or cl, cl
    jz .out
    mov [cs:w16i_tm], cl
    mov [cs:w16i_tk], bx
    mov [cs:w16i_ax], ax
    mov [cs:w16i_ay], dx
    push bx                         ; the window as it is, twice: where it
    mov si, w16i_x0                 ; XSEC-OK: read and written through cs:
    mov di, w16i_ol                 ; XSEC-OK: was, and the outline
    mov cx, 4
.cp:
    mov ax, [cs:si]
    mov [cs:di], ax
    mov [cs:di + 8], ax
    inc si
    inc si
    inc di
    inc di
    loop .cp
    ICALL w16_parent                ; the client's, on the screen, kept:
    ICALL w16_wclip                 ;   a paint between two moves reuses
    pop bx                          ;   [w16_kl]
    jc .out
    mov si, w16_kl
    mov di, w16i_kl                 ; XSEC-OK: read and written through cs:
    mov cx, 4
.lim:
    mov ax, [si]
    mov [cs:di], ax
    inc si
    inc si
    inc di
    inc di
    loop .lim
    or byte [w16_mdion], W16I_TRACK
    call w16i_xor
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_psys - BX = a child, CX = an SC_* command: WM_SYSCOMMAND posted it,
; as its system menu would have sent it. Keeps every register
w16i_psys:
    push ax
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_SYSCOMMAND
    mov [w16_sm_wp], cx
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    ICALL w16_post
    pop ax
    ret

; w16i_track - from w16_mouse while W16I_TRACK is set: AX = the message,
; [w16_mx]/[w16_my] where. A move redraws the outline where it now is; the
; button up takes it away and puts the child there - or, for an icon that
; was not dragged, restores it. Keeps every register
w16i_track:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp ax, WM_MOUSEMOVE
    je .move
    cmp ax, WM_LBUTTONUP
    jne .out
    call w16i_xor                   ; the outline away
    and byte [w16_mdion], ~W16I_TRACK
    mov bx, [cs:w16i_tk]
    mov si, w16i_ol                 ; XSEC-OK: read and written through cs:
    mov di, w16i_nl                 ; XSEC-OK: read and written through cs:
    mov cx, 4
.same:
    mov ax, [cs:si]
    cmp ax, [cs:di]
    jne .moved
    inc si
    inc si
    inc di
    inc di
    loop .same
    test byte [cs:w16i_tm], W16I_TICON
    jz .out
    mov cx, SC_RESTORE              ; an icon clicked, not dragged
    call w16i_psys
    jmp .out
.moved:
    push bx                         ; the outline in its parent's client
    ICALL w16_parent
    ICALL w16_origin                ; AX/DX = the client's top left
    pop bx
    jc .out
    mov si, [cs:w16i_nb]
    sub si, [cs:w16i_nt]
    inc si
    mov cx, [cs:w16i_nr]
    sub cx, [cs:w16i_nl]
    inc cx
    neg ax
    add ax, [cs:w16i_nl]
    neg dx
    add dx, [cs:w16i_nt]
    call w16i_place
    test byte [cs:w16i_tm], W16I_TICON
    jz .out
    call w16i_allin_of              ; an icon's title is outside it
    jmp .out
.move:
    mov ax, [w16_mx]                ; AX/DX = how far it has come
    sub ax, [cs:w16i_ax]
    mov dx, [w16_my]
    sub dx, [cs:w16i_ay]
    mov si, w16i_ol                 ; XSEC-OK: read and written through cs:
    mov di, w16i_wl                 ; XSEC-OK: read and written through cs:
    mov cx, 4
.cp:
    mov bx, [cs:si]
    mov [cs:di], bx
    inc si
    inc si
    inc di
    inc di
    loop .cp
    mov cl, [cs:w16i_tm]
    test cl, W16I_TMOVE
    jz .size
    add [cs:w16i_wl], ax
    add [cs:w16i_wr], ax
    add [cs:w16i_wt], dx
    add [cs:w16i_wb], dx
    jmp short .new
.size:
    mov bx, [cs:w16i_cap]           ; BX = the least it may be: three boxes
    add bx, W16I_FRAME              ;   and a frame wide, a caption and a
    test cl, W16I_TL                ;   frame high
    jz .sr
    add [cs:w16i_wl], ax
.sr:
    test cl, W16I_TR
    jz .st
    add [cs:w16i_wr], ax
.st:
    test cl, W16I_TT
    jz .sb
    add [cs:w16i_wt], dx
.sb:
    test cl, W16I_TB
    jz .min
    add [cs:w16i_wb], dx
.min:
    mov ax, bx                      ; width at least 3 * BX
    add ax, bx
    add ax, bx
    mov dx, [cs:w16i_wr]
    sub dx, [cs:w16i_wl]
    inc dx
    sub dx, ax
    jge .mh
    test cl, W16I_TL
    jz .mr
    add [cs:w16i_wl], dx
    jmp short .mh
.mr:
    sub [cs:w16i_wr], dx
.mh:
    mov dx, [cs:w16i_wb]            ; height at least BX + the frame
    sub dx, [cs:w16i_wt]
    inc dx
    sub dx, bx
    sub dx, W16I_FRAME
    jge .new
    test cl, W16I_TT
    jz .mb
    add [cs:w16i_wt], dx
    jmp short .new
.mb:
    sub [cs:w16i_wb], dx
.new:
    mov si, w16i_wl                 ; XSEC-OK: read and written through cs:
    mov di, w16i_nl                 ; XSEC-OK: read and written through cs:
    mov cx, 4
.cmp:
    mov ax, [cs:si]
    cmp ax, [cs:di]
    jne .redraw
    inc si
    inc si
    inc di
    inc di
    loop .cmp
    jmp short .out
.redraw:
    call w16i_xor                   ; the old outline away, the new one in
    mov si, w16i_wl                 ; XSEC-OK: read and written through cs:
    mov di, w16i_nl                 ; XSEC-OK: read and written through cs:
    mov cx, 4
.cpn:
    mov ax, [cs:si]
    mov [cs:di], ax
    inc si
    inc si
    inc di
    inc di
    loop .cpn
    call w16i_xor
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_xor - the outline [cs:w16i_nl]..[cs:w16i_nb], cut to the client's
; [cs:w16i_kl]..[cs:w16i_kb] as w16i_ncpress found it, inverted: drawn once it is
; there, drawn again it is gone. Keeps every register
w16i_xor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [cs:w16i_nl]
    cmp ax, [cs:w16i_kl]
    jge .l
    mov ax, [cs:w16i_kl]
.l:
    mov bx, [cs:w16i_nt]
    cmp bx, [cs:w16i_kt]
    jge .t
    mov bx, [cs:w16i_kt]
.t:
    mov cx, [cs:w16i_nr]
    cmp cx, [cs:w16i_kr]
    jle .r
    mov cx, [cs:w16i_kr]
.r:
    mov dx, [cs:w16i_nb]
    cmp dx, [cs:w16i_kb]
    jle .b
    mov dx, [cs:w16i_kb]
.b:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    call OSAPI_GFX_XOR_RECT
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_place - BX = a child, AX/DX its new place in its parent's client and
; CX/SI its size: where it was and all of it repainted, and WM_SIZE posted
; with its client's size and SIZE_* by its style. Posted, not sent: a move
; from the mouse runs in no call of the program's. Keeps every register
w16i_place:
    push ax
    push cx
    push dx
    push si
    ICALL w16_uncover
    mov [bx + W16W_X], ax
    mov [bx + W16W_Y], dx
    mov [bx + W16W_CX], cx
    mov [bx + W16W_CY], si
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    ICALL w16_invaltree
    ICALL w16_ncmark
    ICALL w16_rec2hwnd
    ICALL w16_sizemsg
    mov ax, SIZE_MAXIMIZED
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jnz .wp
    mov ax, SIZE_MINIMIZED
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .wp
    xor ax, ax
.wp:
    mov [w16_sm_wp], ax
    xor al, al
    ICALL w16_post
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16i_save - BX = a child: where it is, kept as where it is restored to.
; Keeps every register
w16i_save:
    push ax
    mov ax, [bx + W16W_X]
    mov [bx + RS_X], ax
    mov ax, [bx + W16W_Y]
    mov [bx + RS_Y], ax
    mov ax, [bx + W16W_CX]
    mov [bx + RS_CX], ax
    mov ax, [bx + W16W_CY]
    mov [bx + RS_CY], ax
    pop ax
    ret

; w16i_back - BX = a child: put where w16i_save kept. Keeps every register
w16i_back:
    push ax
    push cx
    push dx
    push si
    mov ax, [bx + RS_X]
    mov dx, [bx + RS_Y]
    mov cx, [bx + RS_CX]
    mov si, [bx + RS_CY]
    call w16i_place
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16i_cli - BX = a child -> SI = its MDI client. CF=1 it is not an MDI
; child. Keeps the rest
w16i_cli:
    push bx
    ICALL w16_parent
    mov si, bx
    pop bx
    jc .out
    cmp byte [si + W16W_KIND], W16Q_MDI
    je .out
    stc
.out:
    ret

; w16i_allin - SI = a client: all of it repainted, an icon's title being
; outside its window. Keeps every register
w16i_allin:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    ICALL w16_invaltree
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_max - BX = an MDI child: maximized - its frame and caption outside its
; client, which is the client's whole; the one maximized before restored;
; the system menu and a restore box merged into the frame's bar. Keeps every
; register
w16i_max:
    push ax
    push cx
    push dx
    push si
    call w16i_cli
    jc .out
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jnz .out
    mov ax, [si + MC_MAX]           ; another: back where it was
    or ax, ax
    jz .none
    push bx
    ICALL w16_hwnd2rec
    jc .nr
    and word [bx + W16W_STYLE + 2], ~WS_MAXIMIZE_HI
    call w16i_back
.nr:
    pop bx
.none:
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .wasmin
    call w16i_save
    jmp short .set
.wasmin:
    and word [bx + W16W_STYLE + 2], ~WS_MINIMIZE_HI
    call w16i_allin
.set:
    or word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    ICALL w16_rec2hwnd
    mov [si + MC_MAX], ax
    call w16i_merge
    push bx
    mov bx, si
    ICALL w16_csize                 ; CX/DX = the client's size
    pop bx
    ICALL w16_bord                  ; AX = its frame
    add cx, ax
    add cx, ax
    add dx, ax
    add dx, ax
    mov si, dx                      ; SI = the height, but the caption
    neg ax
    push ax                         ; X = -frame
    ICALL w16_capt
    add si, ax
    pop dx
    push dx
    sub dx, ax                      ; Y = -frame - caption
    pop ax
    call w16i_place
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16i_restore - BX = an MDI child, maximized or minimized: back where it was,
; and a maximized one's bar as it was. Keeps every register
w16i_restore:
    push si
    call w16i_cli
    jc .out
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jz .min
    and word [bx + W16W_STYLE + 2], ~WS_MAXIMIZE_HI
    mov word [si + MC_MAX], 0
    call w16i_unmerge
    jmp short .back
.min:
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jz .out
    and word [bx + W16W_STYLE + 2], ~WS_MINIMIZE_HI
    call w16i_allin
.back:
    call w16i_back
.out:
    pop si
    ret

; w16i_min - BX = an MDI child: minimized, an icon at the bottom of the
; client, after those there already. Keeps every register
w16i_min:
    push ax
    push cx
    push dx
    push si
    push di
    call w16i_cli
    jc .out
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jnz .out
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jz .save
    and word [bx + W16W_STYLE + 2], ~WS_MAXIMIZE_HI
    mov word [si + MC_MAX], 0
    call w16i_unmerge
    jmp short .icon
.save:
    call w16i_save
.icon:
    push bx                         ; DX = the client's HWND
    mov bx, si
    ICALL w16_rec2hwnd
    mov dx, ax
    pop bx
    xor di, di                      ; DI = the icons there already
    push si
    mov si, w16_wnd
    mov cx, W16_MAXWND
.k:
    test byte [si + W16W_FLAGS], W16WF_USED
    jz .kn
    test word [si + W16W_STYLE + 2], WS_MINIMIZE_HI
    jz .kn
    call w16i_kidof
    jc .kn
    cmp si, bx
    je .kn
    inc di
.kn:
    add si, W16W_SIZE
    loop .k
    pop si
    or word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    push bx
    mov bx, si
    ICALL w16_csize                 ; DX = the client's height
    pop bx
    mov ax, W16I_ISTEP
    push dx
    mul di
    pop dx
    add ax, W16I_TSIDE              ; AX = x
    sub dx, W16I_ICON + 4
    sub dx, [w16_capn]              ; DX = y, the title's room under it
    mov cx, W16I_ICON
    mov si, cx
    call w16i_place
    call w16i_allin_of
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16i_allin_of - BX = a child: its client repainted whole. Keeps every
; register
w16i_allin_of:
    push si
    call w16i_cli
    jc .out
    call w16i_allin
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; The frame's bar while a child is maximized: its system menu at the left, a
; restore box at the right (SPEC.md 124.27.5). Both are MF_BITMAP items with
; no text, drawn by w16i_mbglyph; the restore box is a help item, "\b"
; -----------------------------------------------------------------------------

; w16i_merge - SI = a client: the two items in its frame's bar, once. Keeps
; every register
w16i_merge:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [si + MC_SYSM], 0
    jne .out
    push si
    SLCALL w16_mnew                 ; AX = the system menu
    pop si
    or ax, ax
    jz .out
    mov [si + MC_SYSM], ax
    mov di, w16i_systab             ; XSEC-OK: read and written through cs:
.it:
    mov bx, [cs:di]
    mov dx, [cs:di + 2]
    mov cx, [cs:di + 4]
    mov [w16_mstr], cx
    mov [w16_mstr + 2], cs
    or cx, cx
    jnz .s
    mov word [w16_mstr + 2], 0
.s:
    SLCALL w16_mappend
    add di, 6
    cmp di, w16i_systab_end
    jb .it
    mov bx, si                      ; the frame's bar
    ICALL w16_parent
    mov ax, [bx + W16W_MENU]
    or ax, ax
    jz .out
    mov bx, [si + MC_SYSM]
    push ax
    SLCALL w16_mget                 ; ES:SI = the bar's record
    xor cx, cx
    SLCALL w16_minsert              ; DI = its new first item
    mov dx, MF_POPUP
    mov word [w16_mstr], w16i_s_none; XSEC-OK: a far pointer into this part
    mov [w16_mstr + 2], cs
    SLCALL w16_mput
    or word [es:di + MI_FLAGS], MF_BITMAP
    pop ax
    xor dx, dx
    mov bx, SC_RESTORE
    mov word [w16_mstr], w16i_s_help; XSEC-OK: a far pointer into this part
    SLCALL w16_mappend
    SLCALL w16_mget
    mov cl, [es:si + MH_N]
    xor ch, ch
    dec cx
    SLCALL w16_mitem
    or word [es:di + MI_FLAGS], MF_BITMAP
    SLCALL w16_mbardraw
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_unmerge - SI = a client: its two items out of the frame's bar. Keeps
; every register
w16i_unmerge:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, [si + MC_SYSM]
    or dx, dx
    jz .out
    mov word [si + MC_SYSM], 0
    mov bx, si
    ICALL w16_parent
    mov ax, [bx + W16W_MENU]
    or ax, ax
    jz .out
    SLCALL w16_mget
    jc .out
    xor cx, cx                      ; the system menu, its popup with it
.f:
    cmp cl, [es:si + MH_N]
    jae .r
    SLCALL w16_mitem
    test word [es:di + MI_FLAGS], MF_BITMAP
    jz .fn
    cmp [es:di + MI_ID], dx
    jne .fn
    mov al, 1
    SLCALL w16_mremove
    jmp short .r
.fn:
    inc cx
    jmp short .f
.r:
    xor cx, cx                      ; the restore box
.g:
    cmp cl, [es:si + MH_N]
    jae .draw
    SLCALL w16_mitem
    test word [es:di + MI_FLAGS], MF_BITMAP
    jz .gn
    cmp word [es:di + MI_ID], SC_RESTORE
    jne .gn
    xor al, al
    SLCALL w16_mremove
    jmp short .draw
.gn:
    inc cx
    jmp short .g
.draw:
    SLCALL w16_mbardraw
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_mbglyph - from w16_mbaritem: ES:DI = an MF_BITMAP item of the bar,
; laid out, its band open: the system menu's box with its bar (a popup) or
; the restore box with its two arrows, 16 pixels, black on the bar.
; Keeps every register
w16i_mbglyph:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [es:di + MI_W]          ; BX = the glyph's left
    sub ax, 16
    sar ax, 1
    add ax, [es:di + MI_X]
    add ax, [w16_mcx0]
    mov bx, ax
    mov si, [w16_mlh]               ; SI = the band's rows
    lea ax, [bx + 1]                ; the box: top and bottom rows
    lea cx, [bx + 14]
    mov dx, 1
    SLCALL w16_mbhl
    mov dx, si
    sub dx, 2
    SLCALL w16_mbhl
    mov dx, 2                       ; its sides
.side:
    mov ax, bx
    inc ax
    mov cx, ax
    SLCALL w16_mbhl
    add ax, 13
    mov cx, ax
    SLCALL w16_mbhl
    inc dx
    mov ax, si
    sub ax, 2
    cmp dx, ax
    jb .side
    mov dx, si                      ; DX = the middle row
    shr dx, 1
    test word [es:di + MI_FLAGS], MF_POPUP
    jz .rest
    lea ax, [bx + 4]                ; the system menu's bar
    lea cx, [bx + 11]
    dec dx
    SLCALL w16_mbhl
    inc dx
    SLCALL w16_mbhl
    jmp short .out
.rest:
    xor si, si                      ; the restore box's arrows, 2, 4 and 6
    sub dx, 4                       ;   wide: up above the middle, down
.up:                                ;   below it
    lea ax, [bx + 7]
    sub ax, si
    lea cx, [bx + 8]
    add cx, si
    SLCALL w16_mbhl
    inc dx
    inc si
    cmp si, 3
    jb .up
    add dx, 2
.dn:
    dec si
    lea ax, [bx + 7]
    sub ax, si
    lea cx, [bx + 8]
    add cx, si
    SLCALL w16_mbhl
    inc dx
    or si, si
    jnz .dn
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16i_systab:                        ; ID, flags, text: Windows 3.0's MDI child
    dw SC_RESTORE, 0, w16i_s_rest   ;   system menu, maximized
    dw SC_MOVE, MF_GRAYED, w16i_s_move
    dw SC_SIZE, MF_GRAYED, w16i_s_size
    dw SC_MINIMIZE, 0, w16i_s_min
    dw SC_MAXIMIZE, MF_GRAYED, w16i_s_max
    dw 0, MF_SEPARATOR, 0
    dw SC_CLOSE, 0, w16i_s_close
    dw 0, MF_SEPARATOR, 0
    dw SC_NEXTWINDOW, 0, w16i_s_next
w16i_systab_end:
w16i_s_rest:  db '&Restore', 0
w16i_s_move:  db '&Move', 0
w16i_s_size:  db '&Size', 0
w16i_s_min:   db 'Mi&nimize', 0
w16i_s_max:   db 'Ma&ximize', 0
w16i_s_close: db '&Close', 9, 'Ctrl+F4', 0
w16i_s_next:  db 'Nex&t', 9, 'Ctrl+F6', 0
w16i_s_help:  db 8
w16i_s_none:  db 0

w16i_tk:  dw 0                      ; w16i_track's: the child,
w16i_tm:  db 0                      ;   W16I_T*,
w16i_ax:  dw 0                      ;   where the press was,
w16i_ay:  dw 0
w16i_ol:  dw 0, 0, 0, 0             ;   the window when it began,
w16i_nl:  dw 0                      ;   the outline drawn,
w16i_nt:  dw 0
w16i_nr:  dw 0
w16i_nb:  dw 0
w16i_wl:  dw 0                      ;   and the one coming
w16i_wt:  dw 0
w16i_wr:  dw 0
w16i_wb:  dw 0
w16i_kl:  dw 0                      ;   the client's, on the screen
w16i_kt:  dw 0
w16i_kr:  dw 0
w16i_kb:  dw 0

; -----------------------------------------------------------------------------
; w16i_above - BX = a window about to be painted, its frame or its client:
; when it is in an MDI child, every child of that client above it in the Z
; order that it overlaps is owed the overlap - invalid there, erased, its
; frame owed too - so it is painted after and ends on top. WIN16's clip is
; one rectangle and cannot leave a sibling out of it, so this is the
; painter's order Windows' WS_CLIPSIBLINGS made unnecessary (SPEC.md
; 124.27.3). A child above paints nothing below it, so it ends. Keeps every
; register
; -----------------------------------------------------------------------------
w16i_above:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx                         ; the painting window's rectangle on
    call w16i_outer                 ;   the screen
    pop di                          ; DI = it
    jc .out
    mov ax, [cs:w16i_x0]
    mov [cs:w16i_al], ax
    mov ax, [cs:w16i_y0]
    mov [cs:w16i_at], ax
    mov ax, [cs:w16i_x1]
    mov [cs:w16i_ar], ax
    mov ax, [cs:w16i_y1]
    mov [cs:w16i_ab], ax
    mov bx, di                      ; SI = the MDI child it is in, BX its
.up:                                ;   client
    mov si, bx
    ICALL w16_parent
    jc .out
    cmp byte [bx + W16W_KIND], W16Q_MDI
    jne .up
    mov ax, [si + W16W_SEQ]
    mov [cs:w16i_aseq], ax
    ICALL w16_origin                ; the client's origin on the screen
    jc .out
    mov [cs:w16i_aox], ax
    mov [cs:w16i_aoy], dx
    ICALL w16_rec2hwnd
    mov dx, ax                      ; DX = the client's HWND
    mov si, w16_wnd
    mov cx, W16_MAXWND
.k:
    mov al, [si + W16W_FLAGS]
    and al, W16WF_USED | W16WF_VIS | W16WF_DYING
    cmp al, W16WF_USED | W16WF_VIS
    jne .n
    call w16i_kidof
    jc .n
    mov ax, [si + W16W_SEQ]
    cmp ax, [cs:w16i_aseq]
    jbe .n
    call w16i_owe
.n:
    add si, W16W_SIZE
    loop .k
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16i_owe - SI = a child above, [cs:w16i_al].. the rectangle painted, and
; [cs:w16i_aox]/[cs:w16i_aoy] its client's origin: what of SI it covers,
; owed. Keeps every register
w16i_owe:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [si + W16W_X]           ; AX..DX = SI on the screen, inclusive,
    add ax, [cs:w16i_aox]           ;   cut to the painted rectangle
    cmp ax, [cs:w16i_al]
    jge .l
    mov ax, [cs:w16i_al]
.l:
    mov cx, [si + W16W_Y]
    add cx, [cs:w16i_aoy]
    cmp cx, [cs:w16i_at]
    jge .t
    mov cx, [cs:w16i_at]
.t:
    mov dx, [si + W16W_X]
    add dx, [cs:w16i_aox]
    add dx, [si + W16W_CX]
    dec dx
    cmp dx, [cs:w16i_ar]
    jle .r
    mov dx, [cs:w16i_ar]
.r:
    mov bx, [si + W16W_Y]
    add bx, [cs:w16i_aoy]
    add bx, [si + W16W_CY]
    dec bx
    cmp bx, [cs:w16i_ab]
    jle .b
    mov bx, [cs:w16i_ab]
.b:
    cmp ax, dx
    jg .out
    cmp cx, bx
    jg .out
    push bx
    mov bx, si
    ICALL w16_ncmark
    push ax
    push dx
    ICALL w16_origin                ; AX/DX = SI's client on the screen
    mov [cs:w16i_aqx], ax
    mov [cs:w16i_aqy], dx
    pop dx
    pop ax
    pop si                          ; SI = the bottom
    jc .out
    sub ax, [cs:w16i_aqx]           ; ...in its client, right and bottom
    sub cx, [cs:w16i_aqy]           ;   exclusive
    sub dx, [cs:w16i_aqx]
    inc dx
    sub si, [cs:w16i_aqy]
    inc si
    mov byte [w16_erase], 1
    ICALL w16_invaltree
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16i_al:   dw 0                     ; w16i_above's: the painted rectangle,
w16i_at:   dw 0
w16i_ar:   dw 0
w16i_ab:   dw 0
w16i_aseq: dw 0                     ;   its MDI child's place in the Z order,
w16i_aox:  dw 0                     ;   their client's origin,
w16i_aoy:  dw 0
w16i_aqx:  dw 0                     ;   and the child above's
w16i_aqy:  dw 0

; =============================================================================
; Tile, cascade and arrange icons (SPEC.md 124.27.6)
; =============================================================================

MDITILE_HORIZONTAL  equ 1
MDITILE_SKIPDISABLED equ 2
WS_DISABLED_HI      equ 0x0800
W16I_LMAX           equ 32          ; children a tile or a cascade places

; w16i_collect - BX = a client, AL = 0 its children that are not minimized
; (and not disabled, with AH = MDITILE_SKIPDISABLED) / 1 those that are: CX
; of them in [cs:w16i_list], from the bottom of the Z order up. Keeps the
; rest
w16i_collect:
    push ax
    push dx
    push si
    push di
    mov [cs:w16i_cm], ax
    ICALL w16_rec2hwnd
    mov dx, ax                      ; DX = the client's HWND
    xor ax, ax                      ; AX = the SEQ to go above
    xor di, di                      ; DI = the list's next
.next:
    call w16i_zafter
    jc .done
    test byte [si + W16W_FLAGS], W16WF_DYING
    jnz .next
    push ax
    xor al, al
    test word [si + W16W_STYLE + 2], WS_MINIMIZE_HI
    jz .m
    inc al
.m:
    cmp al, [cs:w16i_cm]
    pop ax
    jne .next
    test byte [cs:w16i_cm + 1], MDITILE_SKIPDISABLED
    jz .take
    test word [si + W16W_STYLE + 2], WS_DISABLED_HI
    jnz .next
.take:
    cmp di, W16I_LMAX * 2
    jae .done
    mov [cs:w16i_list + di], si     ; XSEC-OK: read through cs:
    inc di
    inc di
    jmp short .next
.done:
    mov cx, di
    shr cx, 1
    pop di
    pop si
    pop dx
    pop ax
    ret

; w16i_zafter - DX = a client's HWND, AX = a SEQ: SI = the child with the
; least SEQ above it, AX that SEQ. CF=1 none. Keeps BX, CX, DX, DI
w16i_zafter:
    push bx
    push cx
    push di
    mov bx, ax                      ; BX = the floor
    mov di, 0xFFFF                  ; DI = the least above it so far
    xor ax, ax                      ; AX = its record
    mov si, w16_wnd
    mov cx, W16_MAXWND
.k:
    test byte [si + W16W_FLAGS], W16WF_USED
    jz .n
    call w16i_kidof
    jc .n
    cmp [si + W16W_SEQ], bx
    jbe .n
    cmp [si + W16W_SEQ], di
    jae .n
    mov di, [si + W16W_SEQ]
    mov ax, si
.n:
    add si, W16W_SIZE
    loop .k
    mov si, ax
    mov ax, di
    or si, si
    jnz .out
    stc
.out:
    pop di
    pop cx
    pop bx
    ret

; w16i_unmax - BX = a client: its maximized child, if any, restored first,
; as Windows' tile and cascade did. Keeps every register
w16i_unmax:
    push ax
    push bx
    mov ax, [bx + MC_MAX]
    or ax, ax
    jz .out
    ICALL w16_hwnd2rec
    jc .out
    call w16i_restore
.out:
    pop bx
    pop ax
    ret

; w16i_iconh - BX = a client -> AX = the height its icons' row takes from
; its bottom, 0 when none is minimized. Keeps the rest
w16i_iconh:
    push cx
    push ax
    mov al, 1
    call w16i_collect
    pop ax
    xor ax, ax
    jcxz .out
    mov ax, [w16_capn]
    add ax, W16I_ICON + 6
.out:
    pop cx
    ret

; WM_MDITILE: BX = the client, AX = wParam. The children that are not
; minimized side by side in as few columns as hold them squarely (the least
; C with C * C >= N), N / C to a column and the last taking the rest, the
; top of the Z order first; MDITILE_HORIZONTAL lays rows instead. Above the
; icons' row, if there is one (Wine's MDITile)
w16i_tile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call w16i_unmax
    mov ah, al
    xor al, al
    call w16i_collect               ; CX = N
    or cx, cx
    jnz .some
    jmp .out
.some:
    mov [cs:w16i_tn], cx
    mov [cs:w16i_th], ah
    mov di, 1                       ; DI = C
.c:
    mov ax, di
    mul di
    cmp ax, cx
    jae .cdone
    inc di
    jmp short .c
.cdone:
    mov [cs:w16i_tc], di
    push cx
    ICALL w16_csize                 ; CX/DX = the client
    call w16i_iconh
    sub dx, ax
    test byte [cs:w16i_th], MDITILE_HORIZONTAL
    jz .v
    xchg cx, dx                     ; rows: across and down change places
.v:
    mov [cs:w16i_te], cx            ; the extent the columns divide
    mov [cs:w16i_tf], dx            ; ...and the one each column divides
    mov ax, cx
    xor dx, dx
    div di
    mov [cs:w16i_tw], ax            ; a column's breadth
    pop cx
    mov word [cs:w16i_ti], 0        ; the column, and the child (from the
    mov si, cx                      ;   top of the Z order: SI counts down)
    mov word [cs:w16i_tx], 0
.col:
    mov ax, [cs:w16i_ti]
    cmp ax, [cs:w16i_tc]
    jae .out
    mov ax, [cs:w16i_tn]            ; K = N / C, the last column the rest
    xor dx, dx
    div word [cs:w16i_tc]
    mov cx, ax
    mov ax, [cs:w16i_ti]
    inc ax
    cmp ax, [cs:w16i_tc]
    jb .k
    mov cx, si
.k:
    jcxz .nextc
    mov ax, [cs:w16i_tf]
    xor dx, dx
    div cx
    mov [cs:w16i_tk2], ax           ; a child's length down the column
    mov word [cs:w16i_ty], 0
.row:
    dec si
    push si
    shl si, 1
    mov bx, [cs:w16i_list + si]     ; XSEC-OK: read through cs:
    pop si
    push cx
    push si
    mov ax, [cs:w16i_tx]
    mov dx, [cs:w16i_ty]
    mov cx, [cs:w16i_tw]
    mov si, [cs:w16i_tk2]
    test byte [cs:w16i_th], MDITILE_HORIZONTAL
    jz .put
    xchg ax, dx
    xchg cx, si
.put:
    call w16i_place
    pop si
    pop cx
    mov ax, [cs:w16i_tk2]
    add [cs:w16i_ty], ax
    loop .row
.nextc:
    mov ax, [cs:w16i_tw]
    add [cs:w16i_tx], ax
    inc word [cs:w16i_ti]
    jmp .col
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; WM_MDICASCADE: BX = the client. The children that are not minimized each
; at the next place of the stagger new ones take (w16i_stagger), from the
; bottom of the Z order, so the top one is last and lowest
w16i_cascade:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call w16i_unmax
    xor ax, ax
    call w16i_collect
    jcxz .out
    xor di, di
.c:
    push cx
    mov ax, di
    shr ax, 1
    call w16i_stagger               ; AX = the offset, CX/DX the size
    mov si, dx
    mov dx, ax
    push bx
    mov bx, [cs:w16i_list + di]     ; XSEC-OK: read through cs:
    call w16i_place
    pop bx
    pop cx
    inc di
    inc di
    loop .c
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; WM_MDIICONARRANGE: BX = the client. Its icons in a row along its bottom,
; in Z order, as w16i_min places a new one
w16i_arrange:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, 1
    call w16i_collect
    jcxz .out
    push cx
    ICALL w16_csize                 ; DX = the client's height
    pop cx
    sub dx, W16I_ICON + 4
    sub dx, [w16_capn]
    xor di, di
    mov ax, W16I_TSIDE
.i:
    push ax
    push cx
    push bx
    mov bx, [cs:w16i_list + di]     ; XSEC-OK: read through cs:
    mov cx, W16I_ICON
    mov si, cx
    call w16i_place
    pop bx
    pop cx
    pop ax
    add ax, W16I_ISTEP
    inc di
    inc di
    loop .i
    mov si, bx
    call w16i_allin
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16i_cm:  dw 0                      ; w16i_collect's mode,
w16i_list: times W16I_LMAX dw 0     ;   and what it found
w16i_tn:  dw 0                      ; w16i_tile's: N,
w16i_tc:  dw 0                      ;   C,
w16i_th:  db 0                      ;   wParam,
w16i_te:  dw 0                      ;   the two extents,
w16i_tf:  dw 0
w16i_tw:  dw 0                      ;   a column's breadth,
w16i_ti:  dw 0                      ;   which column,
w16i_tx:  dw 0                      ;   where it is,
w16i_ty:  dw 0                      ;   where the next child is in it,
w16i_tk2: dw 0                      ;   and a child's length

; =============================================================================
; The calls Program Manager and File Manager make around it (SPEC.md
; 124.27.7)
; =============================================================================

; GetInternalWindowPos (USER.460) - args: +0 lpptMin, +4 lprcWin, +8 hWnd.
; The show state - SW_SHOWMINIMIZED, SW_SHOWMAXIMIZED or SW_SHOWNORMAL - the
; rectangle it is restored to, in its parent's client, and a minimized one's
; place (-1, -1 for another)
W16SYS w16i_getiwp
    push si
    push di
    ICALL w16_args
    mov di, bx                      ; ES:DI = the arguments
    mov ax, [es:di + 8]
    ICALL w16_hwnd2rec
    jc .no
    mov cx, [bx + W16W_X]           ; AX/CX/DX/SI = the rectangle, and
    mov dx, [bx + W16W_Y]           ;   where it is now
    mov [cs:w16i_px], cx
    mov [cs:w16i_py], dx
    mov ax, 1
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .top
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI | WS_MAXIMIZE_HI
    jz .now
    mov cx, [bx + RS_X]
    mov dx, [bx + RS_Y]
    push word [bx + RS_CX]
    push word [bx + RS_CY]
    jmp short .size
.top:
    ICALL w16_csize                 ; a top-level: its own size, at 0, 0
    push cx
    push dx
    xor cx, cx
    xor dx, dx
    jmp short .size
.now:
    push word [bx + W16W_CX]
    push word [bx + W16W_CY]
.size:
    mov ax, 1
    test word [bx + W16W_STYLE + 2], WS_MAXIMIZE_HI
    jz .nmx
    mov ax, 3
.nmx:
    test word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    jz .nmn
    mov ax, 2
.nmn:
    pop si                          ; SI = the height, BX the width
    pop bx
    push ax
    push es
    push di
    les di, [es:di + 4]
    mov ax, es
    or ax, di
    jz .nr
    mov [es:di], cx
    mov [es:di + 2], dx
    add cx, bx
    mov [es:di + 4], cx
    add dx, si
    mov [es:di + 6], dx
.nr:
    pop di
    pop es
    pop ax
    les di, [es:di]
    mov cx, es
    or cx, di
    jz .out
    mov cx, -1
    mov dx, cx
    cmp ax, 2
    jne .pt
    mov cx, [cs:w16i_px]
    mov dx, [cs:w16i_py]
.pt:
    mov [es:di], cx
    mov [es:di + 2], dx
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; SetInternalWindowPos (USER.461) - args: +0 lpptMin, +4 lprcWin, +8
; showCmd, +10 hWnd. For an MDI child: minimized (SW_SHOWMINIMIZED,
; SW_MINIMIZE, SW_SHOWMINNOACTIVE), maximized (SW_SHOWMAXIMIZED) or normal,
; and the rectangle, when there is one, where it is restored to - or where
; it is, when it is normal. A top-level window is os8088's to place
W16SYS w16i_setiwp
    push si
    push di
    ICALL w16_args
    mov di, bx                      ; ES:DI = the arguments
    mov ax, [es:di + 10]
    ICALL w16_hwnd2rec
    jc .out
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .out
    call w16i_cli
    jnc .mdi
    call w16i_plain
    jmp short .out
.mdi:
    mov ax, [es:di + 8]
    cmp ax, 2
    je .min
    cmp ax, 6
    je .min
    cmp ax, 7
    je .min
    cmp ax, 3
    je .max
    call w16i_restore               ; normal: where the rectangle says
    call w16i_rect
    jc .out
    mov [bx + RS_X], ax
    mov [bx + RS_Y], dx
    mov [bx + RS_CX], cx
    mov [bx + RS_CY], si
    call w16i_place
    jmp short .out
.min:
    call w16i_min
    jmp short .keep
.max:
    call w16i_max
.keep:
    call w16i_rect                  ; ...and where it comes back to
    jc .out
    mov [bx + RS_X], ax
    mov [bx + RS_Y], dx
    mov [bx + RS_CX], cx
    mov [bx + RS_CY], si
.out:
    xor ax, ax
    xor dx, dx
    pop di
    pop si
    ret

; w16i_plain - BX = a child that is not an MDI child, ES:DI =
; SetInternalWindowPos's arguments: put where the rectangle says, when there
; is one, then shown, or hidden by SW_HIDE, as Program Manager 3.0 places
; each of its items. Keeps BX, DI, ES
w16i_plain:
    mov ax, [es:di + 8]
    cmp ax, 2                       ; SW_SHOWMINIMIZED, SW_MINIMIZE and
    je .icon                        ;   SW_SHOWMINNOACTIVE: an icon at the
    cmp ax, 6                       ;   point, its title drawn under it
    je .icon
    cmp ax, 7
    je .icon
    call w16i_rect
    jc .show
    call w16i_place
    jmp short .show
.icon:
    or word [bx + W16W_STYLE + 2], WS_MINIMIZE_HI
    push es
    push di
    les di, [es:di]                 ; lpptMin
    mov ax, es
    or ax, di
    jz .nopt
    mov ax, [es:di]
    mov dx, [es:di + 2]
    cmp ax, -1
    je .nopt
    mov cx, W16I_ICON
    mov si, cx
    call w16i_place
.nopt:
    pop di
    pop es
.show:
    cmp word [es:di + 8], 0         ; SW_HIDE
    jne .vis
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .out
    ICALL w16_uncover
    and byte [bx + W16W_FLAGS], ~W16WF_VIS
    jmp short .out
.vis:
    and byte [bx + W16W_F2], ~W16W2_NSIZE ; its WM_SIZE: w16i_place's, or
    test byte [bx + W16W_FLAGS], W16WF_VIS ;  none needed
    jnz .out
    or byte [bx + W16W_FLAGS], W16WF_VIS
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    ICALL w16_invaltree
.out:
    ret

; w16i_rect - ES:DI = SetInternalWindowPos's arguments: its lprcWin as AX/DX
; a place and CX/SI a size. CF=1 it is NULL. Keeps BX, DI, ES
w16i_rect:
    push es
    push di
    les di, [es:di + 4]
    mov ax, es
    or ax, di
    stc
    jz .out
    mov ax, [es:di]
    mov dx, [es:di + 2]
    mov cx, [es:di + 4]
    sub cx, ax
    mov si, [es:di + 6]
    sub si, dx
    clc
.out:
    pop di
    pop es
    ret

w16i_px:  dw 0                      ; w16i_getiwp's: where the window is
w16i_py:  dw 0

; CalcChildScroll (USER.462) - args: +0 wScroll, +2 hWnd. ScrollChildren
; (USER.463) - args: +0 lParam, +4 wParam, +6 msg, +8 hWnd. The client's
; scroll bars are never shown (SPEC.md 124.27.7): nothing to work out or
; scroll. DumpIcon (USER.459) - args: +0 lplpAND, +4 lplpXOR, +8 lpcb,
; +12 lpIcon: 0, no icon in Windows' own layout to hand out
W16SYS w16i_calcscroll
W16SYS w16i_scrollkids
W16SYS w16i_dumpicon
    xor ax, ax
    cwd
    ret

; CreateCursorIconIndirect (USER.408) - args: +0 lpXORbits, +4 lpANDbits,
; +8 lpCursorInfo, +12 hInst. An icon WIN16 can draw: a global block holding
; it as an RT_ICON image - a 40-byte header, sixteen colours, 4 bits a pixel
; and the mask, rows bottom up - made from Windows' device bits, which are
; 4 planes of 1 bit (VGA's, the planes of each row one after another, as
; Program Manager's groups keep them), 1 plane of 4 bits, or 1 of 1, at most
; 64 square. Its block is marked GHF_ICON, and DestroyIcon frees it. Any
; other shape answers the one cursor LoadCursor hands out (SPEC.md 124.27.7)
W16SYS w16i_cursorind
    push si
    push di
    push es
    ICALL w16_args                  ; ES:BX = the arguments
    push ds
    lds si, [es:bx + 8]             ; the CURSORICONINFO
    mov ax, [si + 4]
    mov [cs:w16i_iw], ax
    mov ax, [si + 6]
    mov [cs:w16i_ih], ax
    mov ax, [si + 8]
    mov [cs:w16i_iwb], ax
    mov ax, [si + 10]               ; AL = planes, AH = bits a pixel
    mov [cs:w16i_ipb], ax
    pop ds
    mov ax, [es:bx]
    mov [cs:w16i_ixo], ax
    mov ax, [es:bx + 2]
    mov [cs:w16i_ixs], ax
    mov ax, [es:bx + 4]
    mov [cs:w16i_iao], ax
    mov ax, [es:bx + 6]
    mov [cs:w16i_ias], ax
    mov ax, [cs:w16i_iw]
    dec ax
    cmp ax, 63
    ja .fall
    mov ax, [cs:w16i_ih]
    dec ax
    cmp ax, 63
    ja .fall
    mov ax, [cs:w16i_ipb]
    cmp ax, 0x0104
    je .shape
    cmp ax, 0x0401
    je .shape
    cmp ax, 0x0101
    jne .fall
.shape:
    mov cl, 5
    mov ax, [cs:w16i_iw]            ; the image's strides, to a dword
    shl ax, 1
    shl ax, 1
    add ax, 31
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [cs:w16i_xs], ax
    mov ax, [cs:w16i_iw]
    add ax, 31
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [cs:w16i_as], ax
    add ax, [cs:w16i_xs]
    mul word [cs:w16i_ih]
    add ax, W16I_IHDR
    adc dx, 0
    mov cl, GMEM_MOVEABLE           ; a handle under W16_HSYSICO: a fixed
    ICALL w16_gnew                  ;   one is its segment, and may be above it
    or ax, ax
    jz .fall
    mov [cs:w16i_ihd], ax
    dec dx                          ; its header: an icon WIN16 made
    mov es, dx
    or byte [es:GH_FLAGS], GHF_ICON
    inc dx
    mov es, dx
    cld
    xor di, di
    xor ax, ax
    mov cx, W16I_IHDR / 2
    rep stosw
    mov word [es:0], 40
    mov ax, [cs:w16i_iw]
    mov [es:4], ax
    mov ax, [cs:w16i_ih]
    shl ax, 1
    mov [es:8], ax
    mov word [es:12], 1
    mov word [es:14], 4
    mov si, w16i_egapal             ; XSEC-OK: read through cs:
    mov di, 40
    mov cx, 32
.pal:
    mov ax, [cs:si]
    stosw
    inc si
    inc si
    loop .pal
    xor bx, bx                      ; BX = the row written, bottom up
.xrow:
    cmp bx, [cs:w16i_ih]
    jae .and
    mov ax, bx
    mul word [cs:w16i_xs]
    add ax, W16I_IHDR
    mov di, ax                      ; ES:DI = it
    mov ax, [cs:w16i_ih]
    dec ax
    sub ax, bx
    mul word [cs:w16i_iwb]
    cmp byte [cs:w16i_ipb], 4
    jne .np
    shl ax, 1
    shl ax, 1
.np:
    add ax, [cs:w16i_ixo]
    mov si, ax                      ; DS:SI = the row it comes from
    push ds
    mov ds, [cs:w16i_ixs]
    xor cx, cx                      ; CX = the column
.xcol:
    cmp cx, [cs:w16i_iw]
    jae .xend
    call w16i_ipx
    mov ah, al
    inc cx
    xor al, al
    cmp cx, [cs:w16i_iw]
    jae .two
    call w16i_ipx
.two:
    inc cx
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or al, ah
    stosb
    jmp short .xcol
.xend:
    pop ds
    inc bx
    jmp short .xrow
.and:
    mov ax, [cs:w16i_iw]            ; the mask's rows come to a word each
    add ax, 15
    mov cl, 4
    shr ax, cl
    shl ax, 1
    mov [cs:w16i_iwb], ax
    xor bx, bx
.arow:
    cmp bx, [cs:w16i_ih]
    jae .done
    mov ax, [cs:w16i_xs]
    mul word [cs:w16i_ih]
    add ax, W16I_IHDR
    push ax
    mov ax, bx
    mul word [cs:w16i_as]
    pop di
    add di, ax                      ; ES:DI = the row written, bottom up
    mov ax, [cs:w16i_ih]
    dec ax
    sub ax, bx
    mul word [cs:w16i_iwb]
    add ax, [cs:w16i_iao]
    mov si, ax
    mov cx, [cs:w16i_as]
    cmp cx, [cs:w16i_iwb]
    jbe .ac
    mov cx, [cs:w16i_iwb]
.ac:
    push ds
    mov ds, [cs:w16i_ias]
    rep movsb
    pop ds
    inc bx
    jmp short .arow
.done:
    mov ax, [cs:w16i_ihd]
    jmp short .out
.fall:
    mov ax, 0x3300
.out:
    xor dx, dx
    pop es
    pop di
    pop si
    ret

; w16i_ipx - DS:SI = a row of w16i_cursorind's device bits, CX = a column
; -> AL = its colour, 0-15 in the EGA's order. Keeps the rest
w16i_ipx:
    push bx
    push cx
    push dx
    mov bx, cx
    cmp word [cs:w16i_ipb], 0x0401
    jne .bits
    shr bx, 1                       ; 4 bits a pixel, the left one high
    mov al, [si + bx]
    test cl, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 0x0F
    jmp short .out
.bits:
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and cl, 7
    mov ch, 0x80
    shr ch, cl
    xor al, al
    cmp byte [cs:w16i_ipb], 4
    je .planes
    test [si + bx], ch              ; 1 bit: white or black
    jz .out
    mov al, 15
    jmp short .out
.planes:
    mov dx, 0x0401                  ; 4 planes: plane N is bit N (DH counts,
.p:                                 ; CH is the column's bit)
    test [si + bx], ch
    jz .z
    or al, dl
.z:
    shl dl, 1
    add bx, [cs:w16i_iwb]
    dec dh
    jnz .p
.out:
    pop dx
    pop cx
    pop bx
    ret

W16I_IHDR equ 40 + 16 * 4           ; an icon's header and its colours
w16i_egapal:                        ; the EGA's sixteen, as RGBQUADs
    db 0x00,0x00,0x00,0, 0x80,0x00,0x00,0, 0x00,0x80,0x00,0, 0x80,0x80,0x00,0
    db 0x00,0x00,0x80,0, 0x80,0x00,0x80,0, 0x00,0x80,0x80,0, 0xC0,0xC0,0xC0,0
    db 0x80,0x80,0x80,0, 0xFF,0x00,0x00,0, 0x00,0xFF,0x00,0, 0xFF,0xFF,0x00,0
    db 0x00,0x00,0xFF,0, 0xFF,0x00,0xFF,0, 0x00,0xFF,0xFF,0, 0xFF,0xFF,0xFF,0
w16i_iw:  dw 0                      ; w16i_cursorind's: the width, the
w16i_ih:  dw 0                      ;   height, the device bits' bytes a
w16i_iwb: dw 0                      ;   row and plane (then the mask's),
w16i_ipb: dw 0                      ;   planes and bits a pixel,
w16i_ixo: dw 0                      ;   where the colours are,
w16i_ixs: dw 0
w16i_iao: dw 0                      ;   and the mask,
w16i_ias: dw 0
w16i_xs:  dw 0                      ;   the image's two strides,
w16i_as:  dw 0
w16i_ihd: dw 0                      ;   and its handle

; =============================================================================
; The frame's and a child's default procedures, and the keys (SPEC.md
; 124.27.4). Each is DefWindowProc but for what an MDI frame or child does
; differently, and hands DefWindowProc the rest with the arguments it reads
; =============================================================================

; DefFrameProc (USER.445) - args: +0 lParam, +4 wParam, +6 msg,
; +8 hWndMDIClient, +10 hWnd. A Window-menu command activates its child;
; WM_SIZE fills the frame's client with the MDI client; WM_SETFOCUS goes to
; the active child
W16SYS w16i_defframe
    ICALL w16_args
    mov ax, [es:bx + 8]
    or ax, ax
    jz .def                         ; no client: DefWindowProc
    push bx
    ICALL w16_hwnd2rec
    mov si, bx                      ; SI = the client's record
    pop bx
    jc .def
    cmp byte [si + W16W_KIND], W16Q_MDI
    jne .def
    mov cx, [es:bx + 6]
    cmp cx, WM_COMMAND
    je .cmd
    cmp cx, WM_SIZE
    je .size
    cmp cx, WM_SETFOCUS
    je .focus
.def:
    mov ax, [es:bx + 10]            ; DefWindowProc's hWnd is at +8
    mov [es:bx + 8], ax
    ICALL w16u_defproc
    ret
.cmd:
    mov ax, [es:bx + 4]
    cmp ax, SC_SIZE                 ; the merged system menu's: the
    jb .list                        ;   maximized child's own, as Windows
    mov ax, [si + MC_MAX]           ;   sent them to it
    or ax, ax
    jz .def
    mov cx, [es:bx + 4]
    ICALL w16_hwnd2rec
    jc .zero
    call w16i_psys
    jmp short .zero
.list:
    mov ax, [es:bx + 4]             ; an ID in the list's: its child
    sub ax, [si + MC_FIRST]
    jb .def
    cmp ax, [si + MC_N]
    jae .def
    mov ax, [es:bx + 4]
    mov bx, si
    call w16i_child
    jc .zero
    call w16i_activate
.zero:
    xor ax, ax
    cwd
    ret
.size:
    mov dx, [es:bx + 2]             ; the frame's new client, LOWORD by HIWORD
    mov cx, [es:bx]
    xor ax, ax
    mov [w16_mvx], ax
    mov [w16_mvy], ax
    mov byte [w16_mvfl], 0
    mov bx, si
    ICALL w16_rec2hwnd
    ICALL w16_moveto
    jmp short .zero
.focus:
    mov ax, [si + MC_ACT]
    or ax, ax
    jnz .setf
    mov bx, si
    ICALL w16_rec2hwnd
.setf:
    ICALL w16_setfocus
    jmp short .zero

; DefMDIChildProc (USER.447) - args: +0 lParam, +4 wParam, +6 msg, +8 hWnd,
; DefWindowProc's own. WM_CLOSE destroys it as WM_MDIDESTROY does; its
; activation and the focus make it the client's active child; Ctrl+F6's
; system commands go to the next one
W16SYS w16i_defchild
    ICALL w16_args
    mov ax, [es:bx + 8]
    push bx
    ICALL w16_hwnd2rec
    jc .defp
    mov di, bx                      ; DI = the child
    ICALL w16_parent
    jc .defp
    cmp byte [bx + W16W_KIND], W16Q_MDI
    jne .defp
    mov si, bx                      ; SI = the client
    pop bx
    mov cx, [es:bx + 6]
    cmp cx, WM_CLOSE
    je .close
    cmp cx, WM_CHILDACTIVATE
    je .act
    cmp cx, WM_SETFOCUS
    jne .nsf
    cmp ax, [w16_focus]             ; posted, so it may be stale: a child
    jne .zero                       ;   no longer the focus is not activated
    jmp short .act                  ;   (SPEC.md 124.27.4)
.nsf:
    cmp cx, WM_SYSCOMMAND
    jne .def
    mov dx, [es:bx + 4]
    and dl, 0xF0
    xor cx, cx
    cmp dx, SC_NEXTWINDOW
    je .next
    inc cx
    cmp dx, SC_PREVWINDOW
    je .next
    mov bx, di
    cmp dx, SC_MAXIMIZE
    je .max
    cmp dx, SC_MINIMIZE
    je .min
    cmp dx, SC_RESTORE
    je .rest
    cmp dx, SC_MOVE                 ; from the keyboard: not followed
    je .zero
    cmp dx, SC_SIZE
    je .zero
    ICALL w16_args
    jmp short .def
.max:
    call w16i_max
    jmp short .zero
.min:
    call w16i_min
    jmp short .zero
.rest:
    call w16i_restore
    jmp short .zero
.defp:
    pop bx
.def:
    ICALL w16u_defproc
    ret
.close:
    mov bx, si
    call w16i_mdidestroy
    jmp short .zero
.act:
    cmp ax, [si + MC_ACT]
    je .def                         ; already: the focus is DefWindowProc's
    mov bx, si
    call w16i_activate
    jmp short .zero
.next:
    mov dx, cx
    mov bx, si
    call w16i_next
.zero:
    xor ax, ax
    cwd
    ret

; TranslateMDISysAccel (USER.451) - args: +0 lpMsg, +4 hWndClient. A key
; down with Ctrl held: F4 sends the active child SC_CLOSE, F6 and Tab
; SC_NEXTWINDOW - SC_PREVWINDOW with Shift - as WM_SYSCOMMAND, and the
; answer is TRUE: the program does not translate or dispatch it
W16SYS w16i_mdiaccel
    ICALL w16_args
    mov ax, [es:bx + 4]
    les di, [es:bx]
    ICALL w16_hwnd2rec
    jc .no
    cmp byte [bx + W16W_KIND], W16Q_MDI
    jne .no
    mov cx, [es:di + 2]             ; MSG: hwnd, message, wParam, lParam
    cmp cx, WM_KEYDOWN
    je .key
    cmp cx, WM_SYSKEYDOWN
    jne .no
.key:
    push bx
    mov bl, [es:di + 8]             ; lParam's high word: the scan code
    xor bh, bh
    mov dl, [w16_kmods + bx]
    pop bx
    test dl, 4
    jz .no
    mov ax, [es:di + 4]
    mov cx, SC_CLOSE
    cmp ax, VK_F4
    je .send
    mov cx, SC_NEXTWINDOW
    cmp ax, VK_F6
    je .dir
    cmp ax, VK_TAB
    jne .no
.dir:
    test dl, 3
    jz .send
    mov cx, SC_PREVWINDOW
.send:
    mov ax, [bx + MC_ACT]
    or ax, ax
    jz .no
    ICALL w16_hwnd2rec
    jc .no
    mov word [w16_sm_msg], WM_SYSCOMMAND
    mov [w16_sm_wp], cx
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    mov si, w16_cont_one
    ICALL w16_sendwnd
    mov ax, 1
    cwd
    ret
.no:
    xor ax, ax
    cwd
    ret

section .text
