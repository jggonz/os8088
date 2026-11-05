; =============================================================================
; os8088 - apps/win16/w16w31.inc
;
; WHAT WINDOWS 3.1'S PROGRAMS CALL BESIDES (SPEC.md 124.33.1,
; docs/plans/WIN16-ANY-PLAN.md S24): the calls Write 3.1, Sound Recorder
; and Media Player import that Windows 3.0's did not, and the ones a setup
; program's bootstrap reaches for. All of it is PART 9's, the system-library
; part (124.24.1): a handler here is W16SYS, near, ending in ret; the image
; is ICALL from here, the library part SLCALL.
;
; Several are another call's arguments with a head of their own: ScrollDC's
; last five are ScrollWindow's, GetTextExtentPoint's last three
; GetTextExtent's, MoveToEx's MoveTo's. w16n_tail runs that call on them
; where they stand, so each is its own few bytes and not a second copy.
; =============================================================================

section .w16sys

%ifdef PKG_MEGA
W16N_AHINCR equ 8                   ; __AHINCR: the next selector (124.31.6)
%else
W16N_AHINCR equ 0x1000              ; ...or the next 64 KB's paragraph
%endif

; w16n_tail - AX = a handler in the image, CX = where in this call's
; arguments its own begin: it runs on them, as though it had been called
; with them alone - its arguments are this call's from +CX on. [w16_psp_]
; is moved by CX while it runs and put back, so it must be a handler that
; answers without calling the program (no message sent). Its answer and CF
; kept; CX clobbered
w16n_tail:
    add [w16_psp_], cx
    push cx
    mov [w16_ifn], ax
    call far [w16_ibv]
    pop cx
    pushf
    sub [w16_psp_], cx
    popf
    ret

; -----------------------------------------------------------------------------
; KERNEL
; -----------------------------------------------------------------------------

%ifdef PKG_MEGA
; w16s_mvpre - GlobalReAlloc's move (w16k_grealloc), first, before anything
; is copied: ES = the block's header, SI = its handle's entry or 0FFFFh, DI =
; what the program named. CF = 0: it may move, and a MOVEABLE block the
; program named by its selector is moved as its HANDLE (SI its entry, DI the
; handle), so the entry follows it. CF = 1: a LOCKED block that was ever
; tiled (GH_TN), whose tiles the program may hold and which would not follow.
; Being called is half of it: part 9 is here before the copy, so w16s_gwin
; after it cannot fail. Keeps the rest. (SPEC.md 124.31.3)
w16s_mvpre:
    cmp byte [es:GH_LOCK], 0
    je .ul
    cmp byte [es:GH_TN], 0
    stc
    jne .out
.ul:
    cmp si, 0xFFFF
    jne .ok
    test byte [es:GH_FLAGS], GHF_MOVE
    jz .ok
    mov di, [es:GH_HANDLE]
    lea si, [di - W16_GH0]
.ok:
    clc
.out:
    ret

; w16s_gwin - GlobalReAlloc's move (w16k_grealloc), between the copy and
; the old block's free: [w16_gold] = the old header, ES = the new block's
; data, whose header has the old one's flags already. The old block's handle
; descriptor (w16_pblk) goes to the new header and is re-aimed there with
; the new length, in place, another arena or not: the selector the program
; holds follows the block, as standard mode's did. Keeps every register.
; (SPEC.md 124.31.3)
w16s_gwin:
    push ax
    push cx
    push es
    W16VOF cx, es
    dec cx                          ; CX = the new header
    mov ax, [w16_gold]
    W16SEG es, ax
    test byte [es:GH_FLAGS], GHF_HND
    jz .out
    and byte [es:GH_FLAGS], ~GHF_HND ; the old block's no more...
    mov ax, [es:GH_TSEL]
    W16SEG es, cx
    mov [es:GH_TSEL], ax            ; ...the new one's (GHF_HND came with
    ICALL w16_phfix                 ;   the flags), aimed at it
.out:
    pop es
    pop cx
    pop ax
    ret
%endif

; LocalHandleDelta (KERNEL.310) - args: +0 nNewDelta. Handles are not grown
; in blocks here, so the delta is only an answer: the one asked for, or
; Windows' 32. Write 3.1 asks for it by name (GetProcAddress)
W16SYS w16n_lhdelta
    ICALL w16_args
    mov ax, [es:bx]
    or ax, ax
    jnz .out
    mov al, 32
.out:
    xor dx, dx
    ret

; lstrcpyn (KERNEL.353) - args: +0 iMaxLength, +2 lpszSrc, +6 lpszDest. At
; most iMaxLength - 1 characters, and a NUL: the destination answered
W16SYS w16n_strcpyn
    push si
    push di
    push ds
    ICALL w16_args
    mov cx, [es:bx]
    lds si, [es:bx + 2]
    les di, [es:bx + 6]
    mov dx, es
    push di
    jcxz .out
    cld
.c:
    dec cx
    jz .end
    lodsb
    or al, al
    jz .end
    stosb
    jmp short .c
.end:
    mov byte [es:di], 0
.out:
    pop ax
    pop ds
    pop di
    pop si
    ret

; hmemcpy (KERNEL.348) - args: +0 cbCopy (long), +4 lpvSource, +8 lpvDest:
; huge pointers, each moved on by __AHINCR as it passes 64 KB. Forward, at
; most 32 KB a move and none across a segment's end
W16SYS w16n_hmemcpy
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx]                 ; DX:AX = what is left
    mov dx, [es:bx + 2]
    mov si, [es:bx + 4]             ; BP:SI = the source
    mov bp, [es:bx + 6]
    mov di, [es:bx + 8]             ; BX:DI = the destination
    mov bx, [es:bx + 10]
.next:
    call w16n_chunk                 ; CX = this move
    jcxz .done
    push ax
    mov ax, di                      ; ...and the destination's room
    neg ax
    jz .room
    cmp cx, ax
    jbe .room
    mov cx, ax
.room:
    pop ax
    sub ax, cx
    sbb dx, 0
    push ds
    push es
    mov ds, bp
    mov es, bx
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop es
    pop ds
    or si, si
    jnz .s
    add bp, W16N_AHINCR
.s:
    or di, di
    jnz .next
    add bx, W16N_AHINCR
    jmp short .next
.done:
    pop bp
    pop di
    pop si
    ret

; w16n_chunk - DX:AX = bytes left, SI = an offset: CX = the most to move
; now - all of them, 32 KB at most, and none past the offset's segment's
; end. 0 when none are left. Keeps the rest
w16n_chunk:
    mov cx, ax
    or dx, dx
    jz .lo
    mov cx, 0x8000
.lo:
    cmp cx, 0x8000
    jbe .seg
    mov cx, 0x8000
.seg:
    push ax
    mov ax, si
    neg ax
    jz .out                         ; at 0: the whole segment ahead
    cmp cx, ax
    jbe .out
    mov cx, ax
.out:
    pop ax
    ret

; _hread (KERNEL.349) - args: +0 cbRead (long), +4 lpBuffer, +8 hFile.
; _lread's DOS read, a segment's piece at a time, the buffer a huge pointer:
; DX:AX = what was read, short at the file's end; -1 on an error. The
; arguments are moved on in place as it goes
W16SYS w16n_hread
    push si
    push di
    ICALL w16_args
    push word [es:bx + 2]           ; what was asked, for the answer
    push word [es:bx]
.next:
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov si, [es:bx + 4]
    call w16n_chunk
    jcxz .done
    mov di, cx                      ; DI = this read
    mov dx, si
    push word [es:bx + 8]
    mov es, [es:bx + 6]
    pop bx
    mov ah, 0x3F
    ICALL w16_dcall
    jc .err
    push ax
    ICALL w16_args
    pop ax
    sub [es:bx], ax
    sbb word [es:bx + 2], 0
    add [es:bx + 4], ax
    jnc .same
    add word [es:bx + 6], W16N_AHINCR
.same:
    cmp ax, di
    je .next                        ; short: the file has no more
.done:
    ICALL w16_args
    pop ax
    pop dx
    sub ax, [es:bx]
    sbb dx, [es:bx + 2]
    jmp short .out
.err:
    pop ax
    pop ax
    mov ax, -1
    cwd
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; USER
; -----------------------------------------------------------------------------

; ScrollDC (USER.221) - args: +0 lprcUpdate, +4 hrgnUpdate, +6 lprcClip,
; +10 lprcScroll, +14 dy, +16 dx, +18 hDC. The DC's window's ScrollWindow:
; from +6 on, once hDC is that window, these are ScrollWindow's arguments in
; its order, so it runs on them. What the scroll uncovers is made invalid
; there and WM_PAINT paints it, so the update rectangle answered is empty
; and the region is left as it was. FALSE for a DC that is no window's
W16SYS w16n_scrolldc
    push di
    SLCALL w16od_last               ; an own DC's the open one (124.25.11)
    ICALL w16_args
    les di, [es:bx]
    mov ax, es
    or ax, di
    jz .rgn
    xor ax, ax
    cld
    stosw
    stosw
    stosw
    stosw
.rgn:
    ICALL w16_args
    xor ax, ax
    cmp word [es:bx + 18], W16_HDC
    jne .out
    cmp byte [w16_dcon], 0
    je .out
    mov bx, [w16_dcwnd]
    ICALL w16_rec2hwnd
    push ax
    ICALL w16_args
    pop word [es:bx + 18]           ; hWnd, for ScrollWindow
    add word [w16_psp_], 6
    SLCALL w16u_scrollwnd
    sub word [w16_psp_], 6
    mov ax, 1
.out:
    xor dx, dx
    pop di
    ret

; GetWindowPlacement (USER.370) - args: +0 lpwndpl, +4 hWnd. os8088
; minimizes and maximizes no window of its own, so a top-level window is
; shown normally where it is; an MDI child says what it is (124.27.5).
; Neither has a place minimized or maximized of its own: -1, -1
WPL_NORMAL equ 14                   ; rcNormalPosition
W16SYS w16n_getwpl
    push di
    ICALL w16_args
    les di, [es:bx]
    cld
    mov ax, 22                      ; length
    stosw
    xor ax, ax                      ; flags
    stosw
    mov ax, w16u_isiconic
    mov cx, 4
    call w16n_tail
    mov dx, 2                       ; SW_SHOWMINIMIZED
    or ax, ax
    jnz .show
    mov ax, w16u_iszoomed
    mov cx, 4
    call w16n_tail
    mov dx, 3                       ; SW_SHOWMAXIMIZED
    or ax, ax
    jnz .show
    mov dx, 1                       ; SW_SHOWNORMAL
.show:
    ICALL w16_args
    les di, [es:bx]
    mov [es:di + 4], dx
    mov ax, -1
    mov cx, 4
    add di, 6
    cld
    rep stosw                       ; ptMinPosition, ptMaxPosition
    ICALL w16_args
    add word [es:bx], WPL_NORMAL    ; GetWindowRect's, into rcNormalPosition
    push bx
    push es
    mov ax, w16u_getwrect
    xor cx, cx
    call w16n_tail
    pop es
    pop bx
    sub word [es:bx], WPL_NORMAL
    mov ax, 1
    xor dx, dx
    pop di
    ret

; w16s_clsadd - w16_ccb (a class, its W16C_TASK set) goes into the first free
; record of the class block (SPEC.md 124.25.18.1): a block of the program's
; global memory, W16C_STRIDE a record, [w16_clsend] its records' end. None
; yet, or every record used: a block W16_MAXCLS records longer is made, the
; records copied into it at the same indexes (a class's atom is 0C000h + its
; index, so every atom and every W16W_CLS stays good) and the old one freed,
; up to W16_CLSCAP records. AL = the index, CF=0; CF=1 at the cap or with no
; memory: RegisterClass answers FALSE. Keeps the rest
w16s_clsadd:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [w16_clsseg]
    xor bx, bx
.f:
    cmp bx, [w16_clsend]
    jae .grow
    cmp byte [es:bx + W16C_NAME], 0
    je .take
    add bx, W16C_STRIDE
    jmp short .f
.grow:                              ; BX = the end, and the first new record
    mov ax, bx
    add ax, W16_MAXCLS * W16C_STRIDE
    cmp ax, W16_CLSCAP * W16C_STRIDE
    jbe .size
    mov ax, W16_CLSCAP * W16C_STRIDE
    cmp ax, bx
    jbe .no                         ; at the cap
.size:
    push ax
    xor dx, dx
    mov cl, W16G_HIGH | GMEM_ZEROINIT   ; fixed, and zeroed: every new name
    ICALL w16_gnew                  ;   NUL. At the top of the heap, so it is
    pop cx                          ;   not the block a GlobalAlloc's local
    or ax, ax                       ;   heap grows into (WIN16-NEXT lheap).
    jz .no                          ;   CX = the new end
    W16TOSEL dx                     ; DX = its segment (the arm: a selector,
    mov es, dx                      ;   124.31.3)
    mov ax, [w16_clsseg]            ; AX = the old block, 0 none
    mov [w16_clsseg], dx
    mov [w16_clsend], cx
    mov cx, bx                      ; the old records, to the same offsets
    push ds
    mov ds, ax
    xor si, si
    xor di, di
    cld
    rep movsb
    pop ds
    or ax, ax
    jz .take
    push es
    mov es, ax
    W16VOF ax, es                   ; its handle: a fixed block's is its
    pop es                          ;   paragraph
    ICALL w16_gfreeh
.take:
    mov ax, bx                      ; AL = the record's index
    mov cl, W16C_STRIDE
    div cl
    mov di, bx
    mov si, w16_ccb
    mov cx, W16C_STRIDE
    cld
    rep movsb
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_ctlkrow - a CLASS entry of WIN16's controls (slots W16_CTLK + kind,
; SPEC.md 124.25.18), which GetClassInfo hands out as a class's window
; procedure: w16_ctlrow's arguments and answer. A window of the program's
; own class that calls one is a control of that class from this message on:
; Visual Basic's ThunderTextBox forwards to "Edit"'s, and the window keeps the
; program's procedure while its state lives in its record, as a subclassed
; control's does. Windows made that state at WM_NCCREATE, which WIN16 does not
; send, so it is made at the window's WM_CREATE - and, for a procedure that
; answered WM_CREATE itself, before the first message it does forward (a
; control with no state would draw from, and type into, segment 0), and a
; WM_CREATE it forwards after that is answered 0 rather than made twice. A
; window that is a control already keeps its kind. Here rather than in the dialog
; part: part 3 is full, and a program's class never fetched it.
W16SYS w16_ctlkrow
    push si
    mov ax, [w16_idx]               ; the slot the gate is serving
    sub ax, W16_CTLK
    push ax                         ; = the kind
    ICALL w16_args
    ICALL w16_smargs
    ICALL w16_hwnd2rec
    pop ax
    jc .no
    ICALL w16_dlgin                 ; the controls' part
    jc .no
    cmp byte [bx + W16W_KIND], W16Q_PROG
    je .prog
    cmp word [w16_sm_msg], WM_CREATE    ; a control already: made once, so a
    je .no                              ;   WM_CREATE forwarded late is 0
    jmp short .go
.prog:
    mov [bx + W16W_KIND], al
    cmp word [w16_sm_msg], WM_CREATE
    je .go
    mov si, w16_sm_msg              ; its WM_CREATE first, lParam 0 as a
    push word [si]                  ;   dialog's control has it
    push word [si + 2]
    push word [si + 4]
    push word [si + 6]
    mov word [si], WM_CREATE
    xor ax, ax
    mov [si + 2], ax
    mov [si + 4], ax
    mov [si + 6], ax
    ICALL w16_intproc
    pop word [si + 6]
    pop word [si + 4]
    pop word [si + 2]
    pop word [si]
    and ax, dx
    inc ax
    jnz .go
    mov byte [bx + W16W_KIND], W16Q_PROG    ; refused: the program's again
.no:
    xor ax, ax
    xor dx, dx
    pop si
    ret
.go:
    ICALL w16_intproc
    pop si
    ret

; GetClassInfo (USER.404) - args: +0 lpWndClass, +4 lpszClassName,
; +8 hInstance. A class the program registered, by name or by the atom
; RegisterClass answered, as its WNDCLASS, or one of WIN16's control classes
; with its class entry (w16_ctlkrow, SPEC.md 124.25.18); FALSE for any other
W16SYS w16n_clsinfo
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov dx, [es:bx + 6]
    or dx, dx
    jnz .name
    sub ax, 0xC000                  ; the atom: its class's index
    jb .no
    cmp ax, W16_CLSCAP
    jae .no
    push ax
    mov cl, W16C_STRIDE             ; inside the block as it is now
    mul cl
    cmp ax, [w16_clsend]
    pop ax
    jae .no
    ICALL w16_clsget
    mov si, ax
    cmp byte [si + W16C_NAME], 0
    je .no
    jmp .have
.name:
    ICALL w16_farname
    jc .no
    ICALL w16_findcls
    jnc .reg
    ICALL w16_builtin               ; WIN16's own: its class entry
    jc .no                          ;   (SPEC.md 124.25.18); MDICLIENT has
    cmp al, W16Q_COMBO              ;   none
    ja .no
    xor ah, ah
    mov si, ax
    ICALL w16_ctlstub               ; DX:AX = slot 0's stub, the kind's is
    mov cx, si                      ;   W16_CTLK + kind along
    add cx, W16_CTLK
    shl cx, 1
    shl cx, 1
    add ax, cx
    push dx
    push ax
    ICALL w16_args
    push word [es:bx + 4]
    push word [es:bx + 6]
    les di, [es:bx]
    pop word [es:di + 24]           ; lpszClassName
    pop word [es:di + 22]
    pop word [es:di + 2]            ; lpfnWndProc
    pop word [es:di + 4]
    mov ax, 8                       ; CS_DBLCLKS, Windows 3.1's: double clicks, but not
    cmp si, W16Q_STATIC             ;   a static's or a scroll bar's
    je .nodbl
    cmp si, W16Q_SCROLL
    jne .sty
.nodbl:
    xor ax, ax
.sty:
    mov [es:di], ax
    xor ax, ax                      ; no class or window extra words, no icon,
    mov [es:di + 6], ax             ;   cursor or menu: the state is in the
    mov [es:di + 8], ax             ;   window's record...
    mov [es:di + 12], ax
    mov [es:di + 14], ax
    mov [es:di + 18], ax
    mov [es:di + 20], ax
    cmp si, W16Q_EDIT               ; ...but a multi-line EDIT keeps its view
    jne .xw                         ;   and undo in the first 18 extra bytes
    mov byte [es:di + 8], ME_XTRA   ;   (w16edml.inc): a superclass's own
.xw:                                ;   words go after them
    mov ax, [w16_dgroup]
    mov [es:di + 10], ax
    mov word [es:di + 16], COLOR_WINDOW + 1
    mov ax, 1
    jmp .out
.reg:
    mov si, bx
.have:
    ICALL w16_args
    push word [es:bx + 4]           ; lpszClassName, as it was asked
    push word [es:bx + 6]
    les di, [es:bx]
    cld
    mov ax, [si + W16C_STYLE]
    stosw
    mov ax, [si + W16C_PROC]
    stosw
    mov ax, [si + W16C_PROC + 2]
    stosw
    xor ax, ax                      ; cbClsExtra
    stosw
    mov ax, [si + W16C_WEXTRA]
    stosw
    mov ax, [w16_dgroup]            ; hInstance
    stosw
    mov ax, [si + W16C_ICON]
    stosw
    xor ax, ax                      ; hCursor
    stosw
    mov ax, [si + W16C_HBR]
    stosw
    mov ax, [si + W16C_MENU]
    stosw
    mov ax, [si + W16C_MENU + 2]
    stosw
    pop dx
    pop ax
    stosw
    mov ax, dx
    stosw
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; SubtractRect (USER.373) - args: +0 lprcSrc2, +4 lprcSrc1, +8 lprcDst.
; Src1 less what of it Src2 covers, when that leaves a rectangle; Src1 when
; it leaves more. FALSE when the answer is empty
W16SYS w16n_subrect
    push si
    push di
    push ds
    ICALL w16_args
    push word [es:bx + 2]           ; Src2, for after the copy
    push word [es:bx]
    lds si, [es:bx + 4]
    les di, [es:bx + 8]
    cld
    movsw                           ; Dst = Src1
    movsw
    movsw
    movsw
    sub di, 8
    pop si
    pop ds                          ; DS:SI = Src2
    mov ax, [es:di]
    cmp ax, [es:di + 4]
    jge .empty
    mov ax, [es:di + 2]
    cmp ax, [es:di + 6]
    jge .empty
    mov ax, [si]                    ; AX, BX, CX, DX = Src1 and Src2 meet
    cmp ax, [es:di]
    jge .l
    mov ax, [es:di]
.l:
    mov bx, [si + 2]
    cmp bx, [es:di + 2]
    jge .t
    mov bx, [es:di + 2]
.t:
    mov cx, [si + 4]
    cmp cx, [es:di + 4]
    jle .r
    mov cx, [es:di + 4]
.r:
    mov dx, [si + 6]
    cmp dx, [es:di + 6]
    jle .b
    mov dx, [es:di + 6]
.b:
    cmp ax, cx
    jge .yes                        ; they do not meet: Src1
    cmp bx, dx
    jge .yes
    cmp bx, [es:di + 2]
    jne .vert
    cmp dx, [es:di + 6]
    jne .vert
    cmp ax, [es:di]                 ; the whole height: a side comes off
    jne .right
    cmp cx, [es:di + 4]
    je .empty                       ; ...or all of it
    mov [es:di], cx
    jmp short .yes
.right:
    cmp cx, [es:di + 4]
    jne .yes
    mov [es:di + 4], ax
    jmp short .yes
.vert:
    cmp ax, [es:di]
    jne .yes
    cmp cx, [es:di + 4]
    jne .yes
    cmp bx, [es:di + 2]             ; the whole width: the top or bottom
    jne .bottom
    mov [es:di + 2], dx
    jmp short .yes
.bottom:
    cmp dx, [es:di + 6]
    jne .yes
    mov [es:di + 6], bx
.yes:
    mov ax, 1
    jmp short .out
.empty:
    xor ax, ax
    stosw
    stosw
    stosw
    stosw
.out:
    xor dx, dx
    pop ds
    pop di
    pop si
    ret

; RedrawWindow (USER.290) - args: +0 fuRedraw, +2 hrgnUpdate,
; +4 lprcUpdate, +8 hWnd. RDW_INVALIDATE is InvalidateRect, RDW_VALIDATE
; ValidateRect - their arguments are these from +2 and +4, with bErase put
; where hrgnUpdate was - and RDW_UPDATENOW or RDW_ERASENOW then
; UpdateWindow's WM_PAINT. A region alone is the whole window; a window's
; children go with it, as InvalidateRect takes them (SPEC.md 124.8)
RDW_INVALIDATE  equ 0x0001
RDW_ERASE       equ 0x0004
RDW_VALIDATE    equ 0x0008
RDW_NOW_HI      equ 0x03            ; RDW_UPDATENOW | RDW_ERASENOW, high byte

W16SYS w16n_redraw
    ICALL w16_args
    mov ax, [es:bx]
    test al, RDW_INVALIDATE
    jz .val
    and ax, RDW_ERASE               ; bErase, where hrgnUpdate was
    mov [es:bx + 2], ax
    mov ax, w16u_invalrect
    mov cx, 2
    call w16n_tail
    ICALL w16_args
.val:
    test byte [es:bx], RDW_VALIDATE
    jz .now
    mov ax, w16u_validrect
    mov cx, 4
    call w16n_tail
    ICALL w16_args
.now:
    test byte [es:bx + 1], RDW_NOW_HI
    jz .one
    mov ax, [es:bx + 8]             ; UpdateWindow's, here: it sends
    ICALL w16_hwnd2rec
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
    ICALL w16_sendwnd
    pop si
.one:
    mov ax, 1
    xor dx, dx
    ret

; w16n_enrep - BX = a program's own child window that EnableWindow
; (w16_enable) has just set, CX = 1 it was disabled before: when that
; changed, the window is made invalid whole, erased - what its WM_ENABLE does
; in Windows, where the bitmap buttons of Media Player and Sound Recorder
; repaint themselves greyed or not. The message itself is not sent (SPEC.md
; 124.33.1). Only such a window brings part 9 in. Keeps all but AX and DX
W16SYS w16n_enrep
    push cx
    push si
    mov al, [bx + W16W_FLAGS]       ; disabled now (W16WF_DIS) against
    and al, W16WF_DIS               ;   disabled before (CL)
    cmp al, 1
    cmc                             ; CF = disabled now
    sbb al, al
    and al, 1
    cmp al, cl
    je .out                         ; unchanged: nothing to repaint
    push bx
    mov byte [w16_erase], 1
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    ICALL w16_invaltree
    pop bx
.out:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; GDI
; -----------------------------------------------------------------------------

; GetTextExtentPoint (GDI.471) - args: +0 lpSize, +4 cbString, +6 lpszString,
; +10 hDC. GetTextExtent's, whose arguments are these from +4, into a SIZE
W16SYS w16n_textpt
    mov ax, w16g_textext
    mov cx, 4
    jmp short w16n_pt

; MoveToEx (GDI.483) - args: +0 lpPoint, +4 y, +6 x, +8 hDC. MoveTo's, whose
; arguments are these from +4, the old point into lpPoint when there is one
W16SYS w16n_movetoex
    mov ax, w16g_moveto
    mov cx, 4
w16n_pt:                            ; DX:AX = y:x, into the POINT or SIZE
    push di
    call w16n_tail
    push ax
    ICALL w16_args
    les di, [es:bx]
    pop ax
    mov cx, es
    or cx, di
    jz .out
    mov [es:di], ax
    mov [es:di + 2], dx
.out:
    mov ax, 1
    xor dx, dx
    pop di
    ret

; GetViewportExt (GDI.94), GetViewportOrg (GDI.95), GetWindowExt (GDI.96) -
; args: +0 hDC: what SetViewportExt, SetViewportOrg and SetWindowExt keep,
; DX:AX = y:x
W16SYS w16n_getvext
    mov ax, [w16_vext]
    mov dx, [w16_vext + 2]
    ret
W16SYS w16n_getvorg
    mov ax, [w16_vorg]
    mov dx, [w16_vorg + 2]
    ret
W16SYS w16n_getwext
    mov ax, [w16_wext]
    mov dx, [w16_wext + 2]
    ret

section .text
