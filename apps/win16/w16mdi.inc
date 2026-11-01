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

; an MDICLIENT's own state, in its W16W_CS
MC_WMENU    equ W16W_CS + 0         ; CLIENTCREATESTRUCT's hWindowMenu
MC_FIRST    equ W16W_CS + 2         ; ...and idFirstChild
MC_ACT      equ W16W_CS + 4         ; the active child's HWND, 0 none
MC_N        equ W16W_CS + 6         ; its children: IDs MC_FIRST and on
MC_FL       equ W16W_CS + 8         ; MCF_*
MCF_SEP     equ 1                   ; the Window menu has the list's separator

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
    ICALL w16_origin                ; AX/DX = its client's top left
    jc .gone
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
    mov byte [cs:w16i_act], 0       ; active: its client's MC_ACT
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_parent
    jc .np
    cmp byte [bx + W16W_KIND], W16Q_MDI
    jne .np
    cmp ax, [bx + MC_ACT]
    jne .np
    mov byte [cs:w16i_act], 1
.np:
    pop bx
    xor al, al                      ; a DC, its clip the window cut to its
    ICALL w16_dcopen                ;   parent's client
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
    push si                         ; the face, then the text over it in
    push bx                         ;   the same paper
    mov ax, [cs:w16i_tl]
    mov bx, [cs:w16i_ct]
    mov cx, [cs:w16i_tr]
    mov dx, [cs:w16i_cb]
    cmp ax, cx
    jg .nofill
    call w16i_box
.nofill:
    pop bx
    pop si
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
.fit:
    jcxz .out
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    div cx                          ; AX = the places past the first
    mov cx, ax
    mul si                          ; AX = the last place's offset
    pop dx
    sub dx, ax                      ; DX/the width below: the default size
    pop bp
    sub bp, ax
    cmp word [es:di + 12], CW_USEDEFAULT
    jne .pos
    mov [es:di + 12], bp
    mov [es:di + 10], dx
.pos:
    cmp word [es:di + 16], CW_USEDEFAULT
    jne .out
    inc cx                          ; the place: N mod (places + 1)
    mov ax, [bx + MC_N]
    xor dx, dx
    div cx
    mov ax, dx
    mul si
    mov [es:di + 16], ax
    mov [es:di + 14], ax
.out:
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
    je .act
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
    jmp short .def
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
