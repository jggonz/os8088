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

section .text
