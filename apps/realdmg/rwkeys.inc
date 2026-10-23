; =============================================================================
; os8088 - apps/realdmg/rwkeys.inc
;
; Machine > Controls... (docs/REALDMG-SPEC.md section 14.2): which key is
; which Game Boy button. A window of its own beside the game's - an outline
; of a Game Boy, its eight buttons boxes that name the key each has. Click
; one and the screen says to press a key; the next key typed is that
; button's, and a key another button had changes places with it. Esc, Alt,
; F1-F9, F12 and 1-9 are RealDMG's own (F5, F8 and 1-9 the save states',
; rwstate.inc) and are refused, the screen says so, and it waits on. Default puts the keys back as they shipped. The map is
; rw_keymap, the worker's to read every look (rw_keys), and REALDMG.CFG
; keeps it (rw_cfgsave).
;
; A package's second window is not bound to its instance (SPEC.md 42.1):
; its close box hides it, so it is made once and shown again, and the
; instance's teardown destroys it with the region (wm_destroy_seg). While it
; is the top window the game sees no keys at all - rw_keys asks for its own.
; =============================================================================

RW_KW           equ 232             ; the content: the body 200 wide on a mat
RW_KH           equ 134             ;   (154 tall: a CGA band has 155)
CGRAY_MAT       equ 0FEh            ; rw_krect's colour for the 50% dither
RW_KBOX         equ 8               ; boxes: the eight buttons, then Default
RW_KNONE        equ 0FFh            ; rw_ksel: no button waits for a key

; rw_ctlopen - Machine > Controls..., the lock held: the window, made the
; first time and shown again after
rw_ctlopen:
    mov byte [rw_ksel], RW_KNONE
    mov byte [rw_kmsg], 0
    mov bx, [rw_kwin]
    or bx, bx
    jnz .show
    call OSAPI_VIDEO                ; AX = width, CX = the dock's first row
    sub ax, RW_KW + 2               ; centred on the desktop band
    jns .x
    xor ax, ax
.x:
    shr ax, 1
    mov [rw_ktpl + WT_X], ax
    sub cx, MBAR_H + RW_KH + TITLE_H + 2
    jns .y
    xor cx, cx
.y:
    shr cx, 1
    add cx, MBAR_H
    mov [rw_ktpl + WT_Y], cx
    mov si, rw_ktpl
    call OSAPI_WM_CREATE            ; CF = 1: the window table is full -
    jc .r                           ;   nothing, and the game plays on
    mov [rw_kwin], bx
    mov al, 1                       ; it paints every pixel of its own
    call OSAPI_WM_OWNBG             ;   (its mat), so no white under it
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW              ; (its first paint is all of it)
    ret
.show:
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW
    xor cx, cx                      ; shown again: no button waits now
.all:
    push cx
    call rw_kredraw
    pop cx
    inc cx
    cmp cx, 8
    jb .all
.r:
    ret

; rw_kpaint - its W_PAINT, SI = the window: on the kernel's white, the
; outline, the captions, the boxes and the screen's three lines
rw_kpaint:
    push si
    call rw_korg
    mov si, rw_kbody                ; the body on its grey mat (every pixel:
.r:                                 ;   the window paints its own ground)
    lodsb
    cmp al, 0FFh
    je .cut
    call rw_krect
    jmp short .r
.cut:
    mov si, 1                       ; its bottom right, the corner a Game
.c:                                 ;   Boy rounds: a row a pixel in
    mov dx, si
    add dx, 118                     ; DX = the row
    mov al, CWHITE
    mov bx, 214
    sub bx, si
    xor cx, cx                      ; (white from the body's inside)
    call rw_krow
    mov al, CBLACK
    inc bx
    mov cx, bx
    call rw_krow
    mov al, CGRAY_MAT
    inc bx
    mov cx, bx
    mov bx, 215
    call rw_krow
    inc si
    cmp si, 13
    jbe .c
    mov al, CBLACK                  ; the screen
    call OSAPI_SET_COLOR
    mov ax, [rw_kx0]
    add ax, 44
    mov bx, [rw_ky0]
    add bx, 5
    mov cx, ax
    add cx, 143
    mov dx, bx
    add dx, 37
    call OSAPI_GFX_FRAME
    mov si, rw_kcap                 ; the captions: x, y, text
.cap:
    lodsw
    or ax, ax
    jz .boxes
    mov cx, ax
    add cx, [rw_kx0]
    lodsw
    mov dx, ax
    add dx, [rw_ky0]
    push si
    mov si, [si]
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    lodsw
    jmp short .cap
.boxes:
    xor cx, cx
.b:
    call rw_kbox
    inc cx
    cmp cx, RW_KBOX + 1
    jb .b
    call rw_kscreen
    pop si
    ret

; rw_krect - AL = a colour (CGRAY_MAT: the 50% dither), SI = a rect's x1,
; y1, x2, y2 in the content: it, filled. SI advanced past it; all else kept
rw_krect:
    push ax
    push bx
    push cx
    push dx
    call rw_kcol
    pushf
    lodsw
    add ax, [rw_kx0]
    push ax
    lodsw
    add ax, [rw_ky0]
    mov bx, ax
    lodsw
    add ax, [rw_kx0]
    mov cx, ax
    lodsw
    add ax, [rw_ky0]
    mov dx, ax
    pop ax
    popf
    call rw_kfill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_krow - AL = a colour as rw_krect's, CX = x1 (0: 17, the body's inside),
; BX = x2, DX = y, in the content: that run of the row. Kept: all
rw_krow:
    push ax
    push bx
    push cx
    push dx
    or cx, cx
    jnz .x
    mov cx, 17
.x:
    call rw_kcol
    pushf
    mov ax, cx
    add ax, [rw_kx0]
    mov cx, bx
    add cx, [rw_kx0]
    add dx, [rw_ky0]
    mov bx, dx
    popf
    call rw_kfill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_kcol - AL = a colour: the pen set, ZF = 0 - or for CGRAY_MAT, ZF = 1
rw_kcol:
    cmp al, CGRAY_MAT
    je .r
    call OSAPI_SET_COLOR
    or sp, sp
.r:
    ret

; rw_kfill - AX, BX, CX, DX a rect: filled in the pen (ZF = 0) or the
; dither (ZF = 1)
rw_kfill:
    je .g
    call OSAPI_GFX_FILL
    ret
.g:
    call OSAPI_GFX_FILL_GRAY
    ret

; rw_korg - [rw_kx0], [rw_ky0] = the content's corner, the window's now
rw_korg:
    push ax
    push bx
    push dx
    mov bx, [rw_kwin]
    call OSAPI_WM_CONTENT
    mov [rw_kx0], ax
    mov [rw_ky0], dx
    pop dx
    pop bx
    pop ax
    ret

; rw_kbox - box CX: its frame, and its name lettered on its ground - the
; button's key, or "Default" - one opaque run between two rows of the
; ground (black on white; white on black while it waits for a key). Kept: CX
rw_kbox:
    push cx
    mov bx, cx
    shl bx, 1
    shl bx, 1
    mov ax, [rw_kboxes + bx]
    add ax, [rw_kx0]
    mov [rw_kbx], ax
    mov ax, [rw_kboxes + bx + 2]
    add ax, [rw_ky0]
    mov [rw_kby], ax
    mov word [rw_kbw], 42
    mov byte [rw_kbn], 5
    cmp cx, RW_KBOX
    jne .key
    mov word [rw_kbw], 58
    mov byte [rw_kbn], 7
    mov si, rw_s_kdef
    jmp short .fr
.key:
    mov bx, cx
    mov bl, [rw_keymap + bx]
    call rw_kname                   ; SI = the key's name
.fr:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [rw_kbx]
    mov bx, [rw_kby]
    mov cx, ax
    add cx, [rw_kbw]
    dec cx
    mov dx, bx
    add dx, 11
    call OSAPI_GFX_FRAME
    pop cx
    push cx
    mov di, (CWHITE << 8) | CBLACK  ; DI: the ground high, the ink low
    cmp cl, [rw_ksel]
    jne .g
    mov di, (CBLACK << 8) | CWHITE
.g:
    mov ax, di
    mov al, ah
    call OSAPI_SET_COLOR            ; the ground's rows above and below
    mov ax, [rw_kbx]                ;   the letters
    inc ax
    mov bx, ax
    add bx, [rw_kbw]
    sub bx, 3
    mov dx, [rw_kby]
    inc dx
    call OSAPI_GFX_HLINE
    add dx, 9
    call OSAPI_GFX_HLINE
    mov cl, [rw_kbn]
    xor ch, ch
    call rw_kpad                    ; SI = the name, centred in its cells
    mov cx, [rw_kbx]
    inc cx
    mov dx, [rw_kby]
    add dx, 2
    mov ax, di
    call OSAPI_FONT_RUN
    pop cx
    ret

; rw_kscreen - the screen's three lines, each padded to its width so one
; opaque run is the whole of it
rw_kscreen:
    mov bl, [rw_kmsg]
    xor bh, bh
    mov cx, bx
    shl bx, 1
    add bx, cx
    shl bx, 1                       ; BX = the message x 6
    add bx, rw_kmsgs
    mov dx, [rw_ky0]
    add dx, 9
    mov cx, 3
.l:
    push cx
    push bx
    mov si, [bx]
    or si, si
    jnz .t
    mov bl, [rw_ksel]               ; (0: the waiting button's name)
    xor bh, bh
    shl bx, 1
    mov si, [rw_kbname + bx]
.t:
    mov cx, 17
    call rw_kpad
    mov cx, [rw_kx0]
    add cx, 48
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add dx, 11
    pop bx
    add bx, 2
    pop cx
    loop .l
    ret

; rw_kpad - SI = a name, CX = cells: SI = rw_kbuf, the name centred in
; spaces to CX cells. Kept: all but SI
rw_kpad:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    push si
    xor ax, ax                      ; AX = its length
.n:
    cmp byte [si], 0
    je .m
    inc si
    inc ax
    jmp short .n
.m:
    pop si
    cmp ax, cx                      ; (never longer than its cells)
    jbe .fit
    mov ax, cx
.fit:
    mov di, rw_kbuf
    push cx
    sub cx, ax
    shr cx, 1                       ; the spaces before it
    push cx
    mov al, ' '
    rep stosb
    pop ax
    pop cx
    sub cx, ax                      ; ...and what is left for it and after
.c:
    lodsb
    or al, al
    jz .s
    stosb
    loop .c
    jmp short .e
.s:
    mov al, ' '
    rep stosb
.e:
    xor al, al
    stosb
    pop es
    pop di
    pop cx
    pop ax
    mov si, rw_kbuf
    ret

; rw_kname - BL = a scancode: SI = its name (rw_knames, one a code from 0)
rw_kname:
    push ax
    push cx
    mov si, rw_knames
    mov cl, bl
    xor ch, ch
    jcxz .r
.k:
    lodsb
    or al, al
    jnz .k
    loop .k
.r:
    pop cx
    pop ax
    ret

; rw_kredraw - CX = a box (or 0FFFFh, none) and the screen, the lock held,
; outside a paint
rw_kredraw:
    mov bx, [rw_kwin]
    call OSAPI_WM_CLIP_SET          ; CF = 1: nothing of it shows
    jc .r
    call rw_korg
    cmp cx, RW_KBOX
    ja .s
    call rw_kbox
.s:
    call rw_kscreen
.r:
    ret

; rw_konclick - its W_ONCLICK: CX, DX the point, SI the window. A button
; waits for a key (the one that was waiting stops); Default puts the map
; back; anywhere else stops the wait
rw_konclick:
    push si
    call rw_korg
    sub cx, [rw_kx0]
    sub dx, [rw_ky0]
    xor bx, bx
.h:
    mov ax, [rw_kboxes + bx]
    cmp cx, ax
    jb .n
    add ax, 41
    cmp bx, RW_KBOX * 4
    jne .w
    add ax, 16
.w:
    cmp cx, ax
    ja .n
    mov ax, [rw_kboxes + bx + 2]
    cmp dx, ax
    jb .n
    add ax, 11
    cmp dx, ax
    jbe .hit
.n:
    add bx, 4
    cmp bx, (RW_KBOX + 1) * 4
    jb .h
    mov bl, RW_KNONE                ; nothing: the wait, if any, stops
    jmp short .sel
.hit:
    shr bx, 1
    shr bx, 1
    cmp bl, RW_KBOX
    jne .sel
    push ds
    pop es
    mov si, rw_keydef               ; Default
    mov di, rw_keymap
    mov cx, 8
    rep movsb
    mov byte [rw_ksel], RW_KNONE
    mov byte [rw_kmsg], 0
    xor cx, cx
.all:
    push cx
    call rw_kredraw
    pop cx
    inc cx
    cmp cx, 8
    jb .all
    call rw_cfgsave                 ; (drawn first, as a key's is)
    jmp short .r
.sel:
    mov al, bl
    mov ah, 1                       ; the screen: press a key
    cmp al, RW_KNONE
    jne .s
    xor ah, ah
.s:
    mov [rw_kmsg], ah
    xchg al, [rw_ksel]
    cbw
    mov cx, ax                      ; the box that was waiting
    call rw_kredraw
    mov cl, [rw_ksel]
    xor ch, ch
    call rw_kredraw                 ; ...and the one that waits now
.r:
    pop si
    ret

; rw_konkey - its W_ONKEY: AL the character, AH the scancode, SI the
; window. Waiting, the key is the button's (Esc stops the wait); not, Esc
; puts the window away
rw_konkey:
    push si
    mov bl, [rw_ksel]
    cmp bl, RW_KNONE
    jne .wait
    cmp al, 27
    jne .r
    mov bx, [rw_kwin]
    call OSAPI_WM_HIDE
    jmp .r
.wait:
    cmp al, 27
    jne .key
    call .stop                      ; Esc: the wait stops
    jmp .r
.key:
    mov al, ah                      ; AL = the key's make code, as the map
    cmp al, 0E0h                    ;   asks OSAPI_KEY_DOWN for it: the
    jne .f11                        ;   keypad's Enter is Enter there
    mov al, 1Ch
.f11:
    cmp al, 85h                     ; F11, F12: the enhanced ROM's codes
    jne .f12
    mov al, 57h
.f12:
    cmp al, 86h
    jne .chk
    mov al, 58h
.chk:
    cmp al, 2                       ; Esc, and F12 and past it (F12 writes
    jb .no                          ;   the log: rw_logflush)
    cmp al, 0Ah                     ; 1-9: the save states' slots
    jbe .no
    cmp al, 58h
    jae .no
    cmp al, 38h                     ; Alt: Alt+Enter's
    je .no
    cmp al, 54h                     ; (SysRq and a shifted F-key's codes)
    jb .fk
    cmp al, 57h
    jb .no
.fk:
    cmp al, 3Bh                     ; F1-F4: RealDMG's own, F5 and F8 the
    jb .ok                          ;   save states', F6 and F7 the
    cmp al, 43h                     ;   replays' and F9 Sound's
    jbe .no
.ok:
    xor bh, bh                      ; BX = the button
    mov si, rw_keymap               ; a button that has it takes this one's
    mov cx, 8
.d:
    cmp [si], al
    je .sw
    inc si
    loop .d
    jmp short .set
.sw:
    mov ah, [rw_keymap + bx]
    mov [si], ah
    sub si, rw_keymap
    mov cx, si
    push ax
    push bx
    call rw_kredraw                 ; (it is not the one waiting: unlit)
    pop bx
    pop ax
.set:
    mov [rw_keymap + bx], al
    call .stop                      ; drawn first, then the floppy's write
    call rw_cfgsave                 ;   (a box lit through a disk write
    jmp short .r                    ;   reads as a key not taken)
.stop:
    mov byte [rw_kmsg], 0
    mov cl, RW_KNONE
    xchg cl, [rw_ksel]
    xor ch, ch
    jmp rw_kredraw
.no:
    mov byte [rw_kmsg], 2           ; RealDMG's own: say so, and wait on
    mov cx, 0FFFFh
    call rw_kredraw
.r:
    pop si
    ret

rw_ktpl:
    dw 0, 0, RW_KW + 2, RW_KH + TITLE_H + 2     ; x, y set by rw_ctlopen
    dw rw_s_kttl, rw_kpaint, rw_konkey, rw_konclick
rw_s_kttl:      db 'RealDMG Controls', 0
rw_s_kdef:      db 'Default', 0

; the body: a colour (CGRAY_MAT the dither), then x1, y1, x2, y2; FFh ends.
; Its bottom-right corner is rw_kpaint's, a row at a time
rw_kbody:       db CGRAY_MAT
                dw 0, 0, 15, 133
                db CGRAY_MAT
                dw 216, 0, 231, 133
                db CGRAY_MAT
                dw 16, 0, 215, 0
                db CGRAY_MAT
                dw 16, 133, 215, 133
                db CGRAY_MAT
                dw 202, 132, 215, 132
                db CWHITE
                dw 17, 2, 214, 118
                db CBLACK
                dw 16, 1, 215, 1
                db CBLACK
                dw 16, 1, 16, 132
                db CBLACK
                dw 16, 132, 201, 132
                db CBLACK
                dw 215, 1, 215, 118
                db 0FFh
; the boxes' corners in the content, S_JOY's order then Default: 42 x 12,
; Default 58
rw_kboxes:      dw 70, 62,  24, 62,  47, 48,  47, 76    ; Right Left Up Down
                dw 166, 58,  120, 72                    ; A B
                dw 56, 106,  104, 106,  152, 106        ; Select Start Default
; the captions: x, y, text; 0 ends
rw_kcap:        dw 183, 48, rw_kbn_a,  137, 62, rw_kbn_b
                dw 53, 96, rw_kbn_sel,  105, 96, rw_kbn_st, 0
rw_kbname:      dw rw_kbn_r, rw_kbn_l, rw_kbn_u, rw_kbn_d
                dw rw_kbn_a, rw_kbn_b, rw_kbn_sel, rw_kbn_st
rw_kbn_r:       db 'Right', 0
rw_kbn_l:       db 'Left', 0
rw_kbn_u:       db 'Up', 0
rw_kbn_d:       db 'Down', 0
rw_kbn_a:       db 'A', 0
rw_kbn_b:       db 'B', 0
rw_kbn_sel:     db 'Select', 0
rw_kbn_st:      db 'Start', 0
; the screen's three lines a message: 0 the waiting button's name
rw_kmsgs:       dw rw_s_km1, rw_s_km2, rw_s_kem
                dw rw_s_km3, 0, rw_s_km4
                dw rw_s_km5, rw_s_km6, rw_s_km7
rw_s_km1:       db 'Click a button', 0
rw_s_km2:       db 'to give it a key', 0
rw_s_kem:       db 0
rw_s_km3:       db 'Press a key for', 0
rw_s_km4:       db 'Esc: cancel', 0
rw_s_km5:       db 'That key is', 0
rw_s_km6:       db "RealDMG's own:", 0
rw_s_km7:       db 'press another', 0
rw_keydef:      db KSC_RIGHT, KSC_LEFT, KSC_UP, KSC_DOWN, 2Dh, 2Ch, 39h, KSC_ENTER

; rw_knames - set 1 make codes 00-58h, a name each (at most five cells)
rw_knames:
    db 0, 'Esc', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0
    db '8', 0, '9', 0, '0', 0, '-', 0, '=', 0, 'Bksp', 0, 'Tab', 0         ; 0F
    db 'Q', 0, 'W', 0, 'E', 0, 'R', 0, 'T', 0, 'Y', 0, 'U', 0, 'I', 0      ; 17
    db 'O', 0, 'P', 0, '[', 0, ']', 0, 'Enter', 0, 'Ctrl', 0               ; 1D
    db 'A', 0, 'S', 0, 'D', 0, 'F', 0, 'G', 0, 'H', 0, 'J', 0, 'K', 0      ; 25
    db 'L', 0, ';', 0, "'", 0, '`', 0, 'Shift', 0, '\', 0                  ; 2B
    db 'Z', 0, 'X', 0, 'C', 0, 'V', 0, 'B', 0, 'N', 0, 'M', 0              ; 32
    db ',', 0, '.', 0, '/', 0, 'Shift', 0, 'Pad *', 0, 'Alt', 0            ; 38
    db 'Space', 0, 'Caps', 0                                               ; 3A
    db 'F1', 0, 'F2', 0, 'F3', 0, 'F4', 0, 'F5', 0, 'F6', 0, 'F7', 0       ; 41
    db 'F8', 0, 'F9', 0, 'F10', 0, 'Num', 0, 'Scrl', 0                     ; 46
    db 'Home', 0, 'Up', 0, 'PgUp', 0, 'Pad -', 0, 'Left', 0, 'Pad 5', 0    ; 4C
    db 'Right', 0, 'Pad +', 0, 'End', 0, 'Down', 0, 'PgDn', 0, 'Ins', 0    ; 52
    db 'Del', 0, '?', 0, '?', 0, '?', 0, 'F11', 0, 'F12', 0                ; 58
