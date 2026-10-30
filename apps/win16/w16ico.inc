; =============================================================================
; os8088 - apps/win16/w16ico.inc
;
; ICONS (SPEC.md 124.18.4): LoadIcon, DrawIcon and DestroyIcon, and the one
; routine that draws an icon - a program's own, an SS_ICON static's, a message
; box's. An icon is a Windows 3.x icon image: a BITMAPINFOHEADER with its
; height doubled, the colour table, the XOR bits (1 or 4 a pixel) and the AND
; mask, rows bottom up. A program's is its RT_ICON resource, chosen out of an
; RT_GROUP_ICON; a system icon (IDI_*) is WIN16's own art in part 6, the same
; shape, so both are drawn by w16_icodraw.
;
; WHERE THE MASK IS 1 NOTHING IS DRAWN, so what the icon was drawn on shows
; through - Windows' AND-then-XOR, less the rare inverting pixel (mask 1 and
; colour not 0), which is see-through here too. A row is drawn as runs of one
; colour through w16_gfill, so the DC's clip and a 1bpp display's reduction
; (w16_ink) are GDI's own.
; =============================================================================

W16_HSYSICO equ 0x3380          ; IDI_APPLICATION .. IDI_ASTERISK, as handles
W16_NSYSICO equ 5
RT_ICON     equ 3
RT_GRPICON  equ 14
IDI_FIRST   equ 32512

; LoadIcon (USER.174) - args: +0 lpIconName, +4 hInst. The program's own by
; name or ordinal, or with hInst NULL one of WIN16's five; NULL for anything
; else.
; THE CODE IS THE LIBRARY PART'S (SPEC.md 124.19.3): moved out of the image
; in S14 - a call into the image is ICALL, and its data stays in .text
section .w16dlg
W16LIB w16u_loadicon
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    cmp word [es:bx + 4], 0
    jne w16_icload                  ; the program's
    or dx, dx
    jnz .none
    sub ax, IDI_FIRST
    cmp ax, W16_NSYSICO
    jae .none
    call w16_icpart                 ; read now, where a disk read is expected,
    jc .none                        ;   not in the WM_PAINT that draws it
    add ax, W16_HSYSICO
    ret
.none:
    xor ax, ax
    ret

; DrawIcon (USER.84) - args: +0 hIcon, +2 y, +4 x, +6 hDC. TRUE. Drawn on the
; window's DC only: an icon onto a memory bitmap is not followed.
W16LIB w16u_drawicon
    ICALL w16_args
    cmp word [es:bx + 6], W16_HDC
    jne .yes
    mov cx, [es:bx + 4]
    add cx, [w16_ox]
    mov dx, [es:bx + 2]
    add dx, [w16_oy]
    mov ax, [es:bx]
    mov bl, W16_NOCOL               ; see-through
    call w16_icodraw
.yes:
    mov ax, 1
    ret

; DestroyIcon (USER.457) - args: +0 hIcon. TRUE. A program's icon is a
; resource block, and it goes back as FreeResource gives one back; WIN16's
; own are never freed.
W16LIB w16u_destroyicon
    ICALL w16_args
    mov ax, [es:bx]
    cmp ax, W16_HSYSICO
    jae .yes
    push si
    ICALL w16_rentry                 ; SI = its entry
    jc .no
    mov es, [w16_rtab]
    cmp word [es:si + RN_USAGE], 0
    je .no
    dec word [es:si + RN_USAGE]
.no:
    pop si
.yes:
    mov ax, 1
    ret

; -----------------------------------------------------------------------------
; w16_icload - the program's icon: DX:AX = its name (far), or DX = 0 and AX
; its ordinal -> AX = the handle, 0 none. Of the group's images the 32 x 32
; one whose colours are this display's (16, or 2 on a 1bpp one), else any
; 32 x 32, else the first. Keeps the rest. (The dialog part's SS_ICON too.)
; -----------------------------------------------------------------------------
w16_icload:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, w16_rnamb
    ICALL w16_rkey
    mov [w16_rnk], ax
    mov word [w16_rtk], 0x8000 | RT_GRPICON
    ICALL w16_rfind
    ICALL w16_rload                  ; AX = the group's handle, 0 none
    or ax, ax
    jz .out
    ICALL w16_gfind                  ; ES = its block's header
    jc .zero
    mov ax, es
    inc ax
    mov es, ax                      ; ES:0 = the GRPICONDIR
    mov cx, [es:4]                  ; its images, 14 bytes each from +6
    jcxz .zero
    mov si, 6
    mov di, [es:si + 12]            ; DI = the chosen image's ordinal: the
    xor bl, bl                      ;   first, until one scores more (BL)
    mov dh, 16                      ; DH = this display's colours
    cmp byte [w16_mono], 0
    je .e
    mov dh, 2
.e:
    xor dl, dl                      ; DL = this image's score
    cmp word [es:si], 0x2020        ; 32 wide and 32 high
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
    mov ax, RT_ICON
    mov bx, di
    ICALL w16_rfindid
    ICALL w16_rload                  ; AX = the image's handle, 0 none
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16_icpart - part 6, WIN16's icons (SPEC.md 124.18.4), fetched if it is not
; here: ES = it. CF=1 it could not be read. Kept until the program ends
; (w16_icend). Keeps every other register.
; -----------------------------------------------------------------------------
w16_icpart:
    push ax
    mov ax, [w16_icseg]
    or ax, ax
    jnz .have
    mov al, W16P_ICONS
    ICALL w16_pfetch
    jc .out
    mov [w16_icseg], ax
.have:
    mov es, ax
    clc
.out:
    pop ax
    ret

; w16_icend - the icon part, dropped as the program ends. Keeps every register.
w16_icend:
    cmp word [w16_icseg], 0
    je .out
    push ax
    mov al, W16P_ICONS
    ICALL op_drop
    mov word [w16_icseg], 0
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; w16_icdib - AX = an icon's handle -> ES:SI = its image. CF=1 it is not one:
; a 40-byte header, at most 64 wide, one plane, 1 or 4 bits a pixel.
; Keeps all but ES, SI and the flags.
; -----------------------------------------------------------------------------
w16_icdib:
    push ax
    cmp ax, W16_HSYSICO
    jb .prog
    sub ax, W16_HSYSICO
    cmp ax, W16_NSYSICO
    jae .no
    call w16_icpart
    jc .no
    mov si, ax
    shl si, 1
    mov si, [es:si + 2]
    jmp short .chk
.prog:
    ICALL w16_gfind
    jc .no
    cmp byte [w16_gdisc], 0
    jne .no
    mov ax, es
    inc ax
    mov es, ax
    xor si, si
.chk:
    cmp word [es:si], 40            ; biSize
    jne .no
    cmp word [es:si + 4], 64        ; biWidth, a word: at most 64
    ja .no
    cmp word [es:si + 6], 0
    jne .no
    cmp word [es:si + 12], 1        ; biPlanes
    jne .no
    mov al, [es:si + 14]            ; biBitCount
    cmp al, 4
    je .ok
    cmp al, 1
    jne .no
.ok:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; w16_icodraw - AX = an icon's handle, CX = x, DX = y (screen), BL = what a
; see-through pixel is drawn as: W16_NOCOL nothing, or a colour (a static's
; ground, so that its whole square is one pass and nothing is drawn twice).
; Drawn on the DC that is open, cut to its clip. CF=1 it is not an icon.
; Keeps every register.
; -----------------------------------------------------------------------------
w16_icodraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [w16_icx], cx
    mov [w16_icy], dx
    mov [w16_icgnd], bl
    call w16_icdib                  ; ES:SI = the image
    jc .out
    mov ax, [es:si + 4]
    mov [w16_icw], ax
    mov ax, [es:si + 8]             ; the height, doubled for the mask
    shr ax, 1
    mov [w16_ich], ax
    mov al, [es:si + 14]
    mov [w16_icbpp], al
    mov cx, [es:si + 32]            ; biClrUsed, or 2 ^ the bits
    or cx, cx
    jnz .cu
    mov cl, al
    mov ax, 1
    shl ax, cl
    mov cx, ax
.cu:
    cmp cx, 16
    jbe .cn
    mov cx, 16
.cn:
    lea di, [si + 40]               ; the colours: each the nearest of the
    xor bx, bx                      ;   sixteen (SPEC.md 124.8.1)
.pal:
    mov dl, [es:di]                 ; an RGBQUAD: blue, green, red
    mov ah, [es:di + 1]
    mov al, [es:di + 2]
    ICALL w16_rgb2col
    mov [w16_icmap + bx], al
    add di, 4
    inc bx
    loop .pal
    mov [w16_icxp], di              ; the XOR rows start here
    mov ax, [w16_icw]               ; their stride: the bits, to a dword
    cmp byte [w16_icbpp], 4
    jne .s1
    shl ax, 1
    shl ax, 1
.s1:
    add ax, 31
    mov cl, 5
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [w16_icxs], ax
    mul word [w16_ich]
    add ax, di
    mov [w16_icap], ax              ; ...and the mask's rows after them
    mov ax, [w16_icw]
    add ax, 31
    mov cl, 5
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [w16_icas], ax
    xor bp, bp                      ; BP = the row, top down
.row:
    cmp bp, [w16_ich]
    jae .done
    mov ax, [w16_ich]               ; ...which is row h - 1 - BP stored
    dec ax
    sub ax, bp
    push ax
    mul word [w16_icxs]
    add ax, [w16_icxp]
    mov si, ax                      ; SI = its colours
    pop ax
    mul word [w16_icas]
    add ax, [w16_icap]
    mov di, ax                      ; DI = its mask
    mov byte [w16_icrc], W16_NOCOL  ; no run yet
    xor cx, cx                      ; CX = the column
.px:
    cmp cx, [w16_icw]
    jae .eol
    mov bx, cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    push cx
    and cl, 7
    mov ah, 0x80
    shr ah, cl
    mov al, [es:di + bx]            ; the mask's bit
    test al, ah
    jnz .clear
    cmp byte [w16_icbpp], 4
    je .nib
    mov al, [es:si + bx]            ; 1 bit: the same bit of the colours
    and al, ah
    jz .idx
    mov al, 1
    jmp short .idx
.nib:
    pop cx
    push cx
    mov bx, cx
    shr bx, 1
    mov al, [es:si + bx]
    test cl, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 0x0F
.idx:
    pop cx
    mov bl, al
    xor bh, bh
    mov al, [w16_icmap + bx]
.col:
    cmp al, [w16_icrc]
    je .next                        ; the run goes on
    call w16_icrun                  ; the run so far, then this one
    mov [w16_icrc], al
    mov [w16_icrs], cx
    jmp short .next
.clear:
    pop cx
    mov al, [w16_icgnd]
    cmp al, W16_NOCOL
    jne .col                        ; the ground, as a pixel of the icon
    call w16_icrun
    mov byte [w16_icrc], W16_NOCOL
.next:
    inc cx
    jmp short .px
.eol:
    call w16_icrun
    inc bp
    jmp .row
.done:
    clc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_icrun - the run [w16_icrs] .. CX - 1 of row BP in [w16_icrc], if there
; is one. Keeps every register.
w16_icrun:
    cmp byte [w16_icrc], W16_NOCOL
    je .out
    push ax
    push bx
    push cx
    push dx
    mov al, [w16_icrc]
    mov [w16_gcol], al
    mov ax, [w16_icx]
    dec cx
    add cx, ax
    add ax, [w16_icrs]
    mov bx, [w16_icy]
    add bx, bp
    mov dx, bx
    ICALL w16_gfill
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

section .text
