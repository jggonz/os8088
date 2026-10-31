; =============================================================================
; w16clip.inc - the clipboard, on os8088's own (SPEC.md 124.16.1), and the
; string and cursor calls Calculator brought (SPEC.md 124.16.3)
;
; There is one clipboard on the machine, §55's, and it carries text. CF_TEXT
; and CF_OEMTEXT read and write it, with Windows' CR LF made os8088's CR on
; the way in and back on the way out; any other format is taken and freed.
; =============================================================================

CF_TEXT      equ 1
CF_OEMTEXT   equ 7
W16_CBMAX    equ 0xFFF0             ; a block's bytes: GetClipboardData's cap

; w16_cbtext - AX = a format -> ZF=1 it is one the text answers
; THE CODE IS THE LIBRARY PART'S (SPEC.md 124.19.3): moved out of the image
; in S14 - a call into the image is ICALL, and its data stays in .text
section .w16dlg
w16_cbtext:
    cmp ax, CF_TEXT
    je .out
    cmp ax, CF_OEMTEXT
.out:
    ret

; w16_cbdrop - the block GetClipboardData answered last, freed. Keeps all.
w16_cbdrop:
    push ax
    xor ax, ax
    xchg ax, [w16_clph]
    or ax, ax
    jz .out
    ICALL w16_gfreeh
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_clput - ES:SI = text, CX = its most bytes (it ends at a NUL first): its
; CR LFs made CRs IN PLACE, and it put on os8088's clipboard. CF=1 refused
; (SPEC.md 55.2). Keeps every register.
; -----------------------------------------------------------------------------
w16_clput:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    pop ds                          ; DS = ES = the text's segment: DS:SI
    mov di, si                      ;   reads, ES:DI writes, one block
    push si
    cld
    jcxz .done
.c:
    lodsb
    or al, al
    jz .done
    stosb
    cmp al, 13
    jne .n
    cmp cx, 1
    je .n
    cmp byte [si], 10
    jne .n
    inc si                          ; the LF after a CR is os8088's CR
    dec cx
.n:
    loop .c
.done:
    pop si                          ; SI = the start
    mov cx, di
    sub cx, si                      ; CX = the bytes now
    pop ds
    jcxz .none
    call OSAPI_CLIP_PUT
    jmp short .out
.none:
    stc                             ; nothing to put is not a put
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_clgetb - os8088's clipboard as CF_TEXT: AX = a moveable block holding
; it with CR LF line ends and a NUL, which the clipboard owns ([w16_clph]),
; 0 = empty, or no memory, or over W16_CBMAX. Keeps the rest.
; -----------------------------------------------------------------------------
w16_clgetb:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call w16_cbdrop
    call OSAPI_CLIP_SIZE            ; AX = its length
    jc .zero
    mov si, ax                      ; SI = the length
    inc ax                          ; ...and a NUL
    jz .zero
    xor dx, dx
    mov cl, GMEM_MOVEABLE
    ICALL w16_gnew                   ; AX = the handle, DX = its data
    or ax, ax
    jz .zero
    mov bx, ax                      ; BX = the first block
    mov es, dx
    xor di, di
    mov cx, si
    call OSAPI_CLIP_GET             ; CX = the bytes copied
    jc .free1
    mov si, cx
    mov byte [es:si], 0
    xor dx, dx                      ; DX = its CRs
    xor di, di
.cr:
    cmp di, si
    jae .counted
    cmp byte [es:di], 13
    jne .cr1
    inc dx
.cr1:
    inc di
    jmp short .cr
.counted:
    or dx, dx
    jz .keep                        ; no line ends: the block as it is
    mov ax, si
    add ax, dx
    jc .free1
    inc ax                          ; the length grown by an LF a CR, a NUL
    jz .free1
    cmp ax, W16_CBMAX
    ja .free1
    push dx
    xor dx, dx
    mov cl, GMEM_MOVEABLE
    ICALL w16_gnew                   ; AX = the second, DX = its data
    pop cx                          ; (the CRs: not needed again)
    or ax, ax
    jz .free1
    push ax
    push ds
    mov di, es                      ; DS = the first's data, ES = the second's
    mov ds, di
    mov es, dx
    mov cx, si                      ; CX = the bytes
    xor si, si
    xor di, di
    cld
.x:
    jcxz .xend
    lodsb
    stosb
    dec cx
    cmp al, 13
    jne .x
    mov al, 10
    stosb
    jmp short .x
.xend:
    xor al, al
    stosb
    pop ds
    mov ax, bx
    ICALL w16_gfreeh                 ; the first
    pop bx                          ; BX = the second
.keep:
    mov [w16_clph], bx
    mov ax, bx
    jmp short .out
.free1:
    mov ax, bx
    ICALL w16_gfreeh
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

; =============================================================================
; The calls (SPEC.md 124.16.1)
; =============================================================================

; OpenClipboard (USER.137) - args: +0 hWnd
W16LIB w16u_opencb
    ICALL w16_args
    xor ax, ax
    cmp byte [w16_clpopen], 0
    jne .out
    mov byte [w16_clpopen], 1
    mov dx, [es:bx]
    mov [w16_clpown], dx
    inc ax
.out:
    xor dx, dx
    ret

; CloseClipboard (USER.138): the viewer told WM_DRAWCLIPBOARD when it changed
W16LIB w16u_closecb
    xor ax, ax
    xchg al, [w16_clpopen]
    xor ah, ah
    xor dx, dx
    cmp byte [w16_cbchg], 0
    je .out
    mov byte [w16_cbchg], 0
    call w16_cbtell
.out:
    ret

; w16_cbtell - WM_DRAWCLIPBOARD posted to the clipboard's viewer, if there is
; one. Keeps AX and DX
w16_cbtell:
    push ax
    mov ax, [w16_cbview]
    or ax, ax
    jz .out
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_DRAWCLIPBOARD
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    ICALL w16_post
.out:
    pop ax
    ret

; SetClipboardViewer (USER.147) - args: +0 hWnd: the viewer, told
; WM_DRAWCLIPBOARD at once, as Windows told it; the old one is the answer,
; which the new one passes WM_DRAWCLIPBOARD on to. One program here, so the
; chain is as long as it makes it (SPEC.md 124.22.13)
W16LIB w16u_setcbview
    ICALL w16_args
    mov ax, [es:bx]
    xchg ax, [w16_cbview]
    call w16_cbtell
    xor dx, dx
    ret

; ChangeClipboardChain (USER.149) - args: +0 hWndNext, +2 hWnd: hWnd out of
; the chain, hWndNext in its place when it was the viewer
W16LIB w16u_cbchain
    ICALL w16_args
    mov ax, [es:bx + 2]
    cmp ax, [w16_cbview]
    jne .out
    mov ax, [es:bx]
    mov [w16_cbview], ax
.out:
    xor ax, ax
    xor dx, dx
    ret

; RegisterClipboardFormat (USER.145) - args: +0 lpszFormat: C000h + its
; place in WIN16's table, the same for the same name (case aside); 0 when
; the W16_NCBF places are taken. Nothing is ever put on os8088's clipboard
; in one: it carries text
W16LIB w16u_regcbfmt
    push si
    push di
    ICALL w16_args
    les di, [es:bx]
    xor bx, bx                      ; BX = the place
    mov si, w16_cbfn
.f:
    cmp byte [si], 0
    je .new
    push si
    push di
.c:
    mov al, [es:di]
    mov ah, [si]
    call w16_up2
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
    jmp short .have
.ne:
    pop di
    pop si
    add si, 32
    inc bx
    cmp bx, W16_NCBF
    jb .f
    xor ax, ax
    jmp short .out
.new:
    mov cx, 31                      ; the name, cut to 31
.cp:
    mov al, [es:di]
    mov [si], al
    or al, al
    jz .have
    inc si
    inc di
    loop .cp
    mov byte [si], 0
.have:
    mov ax, 0xC000
    add ax, bx
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16_up2 - AL and AH each upper-cased (a-z). Keeps the rest
w16_up2:
    cmp al, 'a'
    jb .h
    cmp al, 'z'
    ja .h
    sub al, 0x20
.h:
    cmp ah, 'a'
    jb .out
    cmp ah, 'z'
    ja .out
    sub ah, 0x20
.out:
    ret

; GetClipboardFormatName (USER.146) - args: +0 nMaxCount, +2 lpszFormatName,
; +6 wFormat: a registered format's name, cut to the buffer; the answer is
; its length, 0 for any other format
W16LIB w16u_cbfmtname
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 6]
    les di, [es:bx + 2]
    sub ax, 0xC000
    jb .zero
    cmp ax, W16_NCBF
    jae .zero
    mov si, ax
    push cx
    mov cl, 5
    shl si, cl
    pop cx
    add si, w16_cbfn
    cmp byte [si], 0
    je .zero
    jcxz .zero
    dec cx                          ; room for the NUL
    xor ax, ax
.cp:
    jcxz .end
    mov dl, [si]
    or dl, dl
    jz .end
    mov [es:di], dl
    inc si
    inc di
    inc ax
    dec cx
    jmp short .cp
.end:
    mov byte [es:di], 0
    jmp short .out
.zero:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; EmptyClipboard (USER.139)
W16LIB w16u_emptycb
    xor ax, ax
    cmp byte [w16_clpopen], 0
    je .out
    call w16_cbdrop
    xor cx, cx                      ; CX = 0 empties it (SPEC.md 55.4)
    call OSAPI_CLIP_PUT
    mov byte [w16_cbchg], 1
    mov ax, 1
.out:
    xor dx, dx
    ret

; GetClipboardOwner (USER.140)
W16LIB w16u_getcbown
    mov ax, [w16_clpown]
    xor dx, dx
    ret

; SetClipboardData (USER.141) - args: +0 hMem, +2 wFormat
W16LIB w16u_setcbdata
    ICALL w16_args
    mov ax, [es:bx]                 ; AX = hMem
    or ax, ax
    jz .out                         ; delayed rendering: none here
    mov cx, [es:bx + 2]
    push ax
    xchg ax, cx
    call w16_cbtext
    xchg ax, cx
    jne .free                       ; not text: taken, and freed
    call w16_cbdrop
    ICALL w16_gfind                  ; ES = its header
    jc .bad
    cmp byte [w16_gdisc], 0
    jne .bad
    mov cx, [es:GH_SIZE]            ; its paragraphs, the header's among them
    dec cx
    cmp cx, 0x1000
    jb .sz
    mov cx, 0x0FFF
.sz:
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = its bytes
    mov ax, es
    inc ax
    mov es, ax
    xor si, si
    call w16_clput
    jc .bad
    mov byte [w16_cbchg], 1
.free:
    pop ax
    push ax
    ICALL w16_gfreeh
    pop ax
.out:
    xor dx, dx
    ret
.bad:
    pop ax
    ICALL w16_gfreeh
    xor ax, ax
    xor dx, dx
    ret

; GetClipboardData (USER.142) - args: +0 wFormat
W16LIB w16u_getcbdata
    ICALL w16_args
    mov ax, [es:bx]
    call w16_cbtext
    jne .none
    call w16_clgetb
    xor dx, dx
    ret
.none:
    xor ax, ax
    xor dx, dx
    ret

; IsClipboardFormatAvailable (USER.193) - args: +0 wFormat
W16LIB w16u_iscbfmt
    ICALL w16_args
    mov ax, [es:bx]
    call w16_cbtext
    jne w16u_cbno
w16u_cbtrue:
    call OSAPI_CLIP_SIZE
    jc w16u_cbno
    mov ax, 1
    xor dx, dx
    ret
w16u_cbno:
    xor ax, ax
    xor dx, dx
    ret

; CountClipboardFormats (USER.143)
W16LIB w16u_countcbfmt
    call w16u_cbtrue
    shl ax, 1                       ; CF_TEXT and CF_OEMTEXT, or none
    ret

; EnumClipboardFormats (USER.144) - args: +0 wFormat, the last one answered
W16LIB w16u_enumcbfmt
    ICALL w16_args
    mov cx, [es:bx]
    call w16u_cbtrue
    or ax, ax
    jz .out
    mov ax, CF_TEXT
    jcxz .out
    mov ax, CF_OEMTEXT
    cmp cx, CF_TEXT
    je .out
    xor ax, ax
.out:
    xor dx, dx
    ret

; =============================================================================
; Strings and the cursor (SPEC.md 124.16.3)
; =============================================================================

; w16_aup / w16_alow - AL an ANSI character -> AL its upper / lower case:
; a-z and E0h-FEh but F7h, and back. Keep the rest.
w16_aup:
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
w16_alow:
    cmp al, 'A'
    jb .out
    cmp al, 'Z'
    jbe .cv
    cmp al, 0xC0
    jb .out
    cmp al, 0xD7
    je .out
    cmp al, 0xDF
    jae .out
.cv:
    add al, 0x20
.out:
    ret

; AnsiUpper (USER.431), AnsiLower (USER.432) - args: +0 lpsz. A segment of 0
; is a character in the offset
W16LIB w16u_ansiup
    mov di, w16_aup
    jmp short w16_ansicv
W16LIB w16u_ansilow
    mov di, w16_alow
w16_ansicv:
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    or dx, dx
    jnz .str
    call di                         ; a character, answered
    xor ah, ah
    ret
.str:
    push ax
    mov si, ax
    mov es, dx
.c:
    mov al, [es:si]
    or al, al
    jz .end
    call di
    mov [es:si], al
    inc si
    jmp short .c
.end:
    pop ax
    ret

; AnsiNext (USER.472) - args: +0 lpsz: past its character, not past a NUL
W16LIB w16u_ansinext
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    push es
    mov es, dx
    mov si, ax
    cmp byte [es:si], 0
    pop es
    je .out
    inc ax
.out:
    ret

; AnsiPrev (USER.473) - args: +0 lpszCurrent, +4 lpszStart: before it, not
; before the start
W16LIB w16u_ansiprev
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    cmp ax, [es:bx + 4]
    jbe .out
    dec ax
.out:
    ret

; lstrcmp (USER.430), lstrcmpi (USER.471) - args: +0 lpsz2, +4 lpsz1. -1, 0
; or 1. Windows 3.x's US language driver: letters case blind first, and when
; that ties, at the first case difference lower case before upper; the rest
; by their bytes. lstrcmpi stops at the first pass.
W16LIB w16u_lstrcmpi
    mov byte [w16_scase], 0
    jmp short w16_lstrcmp
W16LIB w16u_lstrcmp
    mov byte [w16_scase], 1
w16_lstrcmp:
    ICALL w16_args
    mov dl, [w16_scase]
    push ds
    lds si, [es:bx + 4]             ; DS:SI = lpsz1
    les di, [es:bx]                 ; ES:DI = lpsz2
    push si
    push di
.c:
    mov al, [si]
    mov ah, [es:di]
    call w16_aup
    xchg al, ah
    call w16_aup
    xchg al, ah
    cmp al, ah
    jne .diff
    or al, al
    jz .tie
    inc si
    inc di
    jmp short .c
.tie:
    pop di
    pop si
    xor ax, ax
    or dl, dl
    jz .out
.t:                                 ; the first case difference decides
    mov al, [si]
    mov ah, [es:di]
    cmp al, ah
    jne .case
    or al, al
    jz .eq
    inc si
    inc di
    jmp short .t
.eq:
    xor ax, ax
    jmp short .out
.case:
    cmp al, ah                      ; the lower case one is the larger byte,
    mov ax, 1                       ;   and it sorts first
    ja .neg
    jmp short .out
.diff:
    add sp, 4                       ; the two starts
    cmp al, ah
    mov ax, 1
    ja .out
.neg:
    neg ax
.out:
    pop ds
    xor dx, dx
    ret

; SetCursor (USER.69) - args: +0 hCursor. The one set before; os8088's arrow
; stays (SPEC.md 124.10)
W16LIB w16u_setcursor
    ICALL w16_args
    mov ax, [es:bx]
    xchg ax, [w16_cursor]
    xor dx, dx
    ret

; ShowCursor (USER.71) - args: +0 bShow. The count, from 0
W16LIB w16u_showcursor
    ICALL w16_args
    mov ax, [w16_curcnt]
    cmp word [es:bx], 0
    je .hide
    inc ax
    jmp short .set
.hide:
    dec ax
.set:
    mov [w16_curcnt], ax
    xor dx, dx
    ret

section .text
