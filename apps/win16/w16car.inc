; =============================================================================
; w16car.inc - the caret (SPEC.md 124.22.9): CreateCaret, DestroyCaret,
; SetCaretPos, GetCaretPos, HideCaret, ShowCaret and the blink time. The
; library part's: section .w16dlg
;
; Windows had one caret, owned by one window. Here it is an inverted box, the
; caret's size, at its place in its owner's client, drawn while it is shown -
; created hidden, each HideCaret one more to undo - and steady: it does not
; blink, which would be a redraw twice a second for nothing on an XT. A
; paint of its owner takes it off first and puts it back after (BeginPaint
; and EndPaint, DefWindowProc's WM_PAINT, ScrollWindow), as Windows hid it,
; so the inverted pixels and the window's never disagree.
; =============================================================================

section .w16dlg

CAR_HWND  equ 0                 ; [w16_car]: its window, 0 none
CAR_X     equ 2                 ; its place in that window's client
CAR_Y     equ 4
CAR_W     equ 6                 ; its size
CAR_H     equ 8
CAR_HIDE  equ 10                ; HideCaret's count (created at 1)
CAR_ON    equ 11                ; 1: its pixels are inverted now
CAR_PAINT equ 12                ; 1: its window is being painted
CAR_BLINK equ 14                ; SetCaretBlinkTime's, answered back

; w16_carflip - the caret's box inverted, through a DC of its own on its
; window: CAR_ON flipped. Keeps every register.
w16_carflip:
    push ax
    push bx
    push cx
    push dx
    mov ax, [w16_car + CAR_HWND]
    ICALL w16_hwnd2rec
    jc .out
    xor byte [w16_car + CAR_ON], 1
    xor al, al
    ICALL w16_dcopen
    cmp byte [w16_dchide], 0
    jne .close
    mov ax, [w16_car + CAR_X]
    add ax, [w16_ox]
    mov bx, [w16_car + CAR_Y]
    add bx, [w16_oy]
    mov cx, ax
    add cx, [w16_car + CAR_W]
    dec cx
    mov dx, bx
    add dx, [w16_car + CAR_H]
    dec dx
    mov byte [w16_gcol], W16_XOR
    ICALL w16_gfill
.close:
    ICALL w16_dcclose
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_carsync - the caret drawn if it should show and is not, taken off if
; it is and should not. Keeps every register.
w16_carsync:
    push ax
    xor al, al                      ; AL = 1: it should show
    cmp word [w16_car + CAR_HWND], 0
    je .want
    cmp byte [w16_car + CAR_HIDE], 0
    jne .want
    cmp byte [w16_car + CAR_PAINT], 0
    jne .want
    inc al
.want:
    cmp al, [w16_car + CAR_ON]
    je .out
    call w16_carflip
.out:
    pop ax
    ret

; w16_carpre - AX = a window about to be painted or scrolled: the caret taken
; off if it is in it, until w16_carpost. Keeps every register.
w16_carpre:
    cmp ax, [w16_car + CAR_HWND]
    jne .out
    or ax, ax
    jz .out
    mov byte [w16_car + CAR_PAINT], 1
    call w16_carsync
.out:
    ret

; w16_carpost - the paint or the scroll done: the caret back. Keeps every
; register.
w16_carpost:
    cmp byte [w16_car + CAR_PAINT], 0
    je .out
    mov byte [w16_car + CAR_PAINT], 0
    call w16_carsync
.out:
    ret

; w16_cargone - AX = a window being destroyed: the caret goes with it (its
; pixels go with the window). Keeps every register.
w16_cargone:
    cmp ax, [w16_car + CAR_HWND]
    jne .out
    mov word [w16_car + CAR_HWND], 0
    mov byte [w16_car + CAR_ON], 0
    mov byte [w16_car + CAR_PAINT], 0
.out:
    ret

; CreateCaret (USER.163) - args: +0 nHeight, +2 nWidth, +4 hBitmap, +6 hWnd.
; The old caret destroyed; the new one hidden at 0, 0. A width or height of
; 0 is the window border's, 1; a bitmap is drawn as its size's box.
W16LIB w16u_crecaret
    call w16u_descaret
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov [w16_car + CAR_HWND], ax
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    mov cx, [es:bx + 4]
    jcxz .size
    push ax                         ; a bitmap: its size
    push dx
    mov ax, cx
    call w16_bmslot
    pop dx
    pop ax
    jc .size
    mov ax, [w16b_w + bx]
    mov dx, [w16b_h + bx]
.size:
    or ax, ax
    jg .w
    mov ax, 1
.w:
    or dx, dx
    jg .h
    mov dx, 1
.h:
    mov [w16_car + CAR_W], ax
    mov [w16_car + CAR_H], dx
    xor ax, ax
    mov [w16_car + CAR_X], ax
    mov [w16_car + CAR_Y], ax
    mov byte [w16_car + CAR_HIDE], 1
    mov byte [w16_car + CAR_ON], 0
    mov byte [w16_car + CAR_PAINT], 0
    ret

; DestroyCaret (USER.164)
W16LIB w16u_descaret
    mov byte [w16_car + CAR_HIDE], 1
    call w16_carsync
    mov word [w16_car + CAR_HWND], 0
    mov ax, 1
    ret

; SetCaretPos (USER.165) - args: +0 y, +2 x
W16LIB w16u_setcaretpos
    ICALL w16_args
    push word [es:bx]
    push word [es:bx + 2]
    inc byte [w16_car + CAR_HIDE]   ; off at the old place, on at the new
    call w16_carsync
    pop word [w16_car + CAR_X]
    pop word [w16_car + CAR_Y]
    dec byte [w16_car + CAR_HIDE]
    call w16_carsync
    ret

; GetCaretPos (USER.183) - args: +0 lpPoint
W16LIB w16u_getcaretpos
    ICALL w16_args
    les bx, [es:bx]
    mov ax, [w16_car + CAR_X]
    mov [es:bx], ax
    mov ax, [w16_car + CAR_Y]
    mov [es:bx + 2], ax
    ret

; HideCaret (USER.166) - args: +0 hWnd: the caret's own window, or NULL for
; whichever has it
W16LIB w16u_hidecaret
    call w16_carmine
    jc .out
    inc byte [w16_car + CAR_HIDE]
    call w16_carsync
.out:
    ret

; ShowCaret (USER.167) - args: +0 hWnd
W16LIB w16u_showcaret
    call w16_carmine
    jc .out
    cmp byte [w16_car + CAR_HIDE], 0
    je .out
    dec byte [w16_car + CAR_HIDE]
    call w16_carsync
.out:
    ret

; w16_carmine - HideCaret's and ShowCaret's hWnd: CF=0 it is the caret's
; window or NULL, and there is a caret
w16_carmine:
    ICALL w16_args
    mov ax, [es:bx]
    cmp word [w16_car + CAR_HWND], 0
    je .no
    or ax, ax
    jz .yes
    cmp ax, [w16_car + CAR_HWND]
    jne .no
.yes:
    clc
    ret
.no:
    stc
    ret

; SetCaretBlinkTime (USER.168) - args: +0 the milliseconds. Kept and answered
; back; the caret does not blink (above)
W16LIB w16u_setblink
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_car + CAR_BLINK], ax
    ret

; GetCaretBlinkTime (USER.169): Windows' 530 until it is set
W16LIB w16u_getblink
    mov ax, [w16_car + CAR_BLINK]
    or ax, ax
    jnz .out
    mov ax, 530
.out:
    ret

section .text
