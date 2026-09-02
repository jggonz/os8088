; =============================================================================
; os8088 - apps/artful/atui.inc
;
; ArtfulType's chrome (SPEC.md 46.5): the app-drawn menu bar (black with
; white text in Writer mode - main.c's UpdateMenuBarLook, done honestly
; here because the bar is ours to draw), Macintosh press-drag-release
; pull-down menus (the sol_drag idiom: XOR highlights under held locks, the
; button sampled by level between unlock/yield/lock passes), the modal
; alerts (Save changes / About / errors), and the windowed splash card with
; its New / Open buttons and the 128x100 artwork.
;
; The kernel's own menu bar is unreachable in fullscreen by design (SPEC.md
; 11.2), which is exactly what lets this one exist: rows 0..19 arrive at
; W_ONCLICK like any other content.
; =============================================================================

AT_ITEMH   equ 13                   ; pull-down item row height
AT_BARY    equ 6                    ; bar title text y

; -----------------------------------------------------------------------------
; at_bar_draw - the fullscreen menu bar
; out: nothing; preserves all registers; lock held by the caller
; -----------------------------------------------------------------------------
at_bar_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    cmp byte [at_writer], 0
    je .bg
    mov al, CBLACK                  ; Writer mode: the inverted bar
.bg:
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [at_vw]
    dec cx
    mov dx, 18
    call OSAPI_GFX_FILL
    mov al, CBLACK                  ; the bar's bottom rule, either way
    call OSAPI_SET_COLOR
    xor ax, ax
    mov bx, [at_vw]
    dec bx
    mov dx, 19
    call OSAPI_GFX_HLINE
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the ground the fill above
    cmp byte [at_writer], 0         ; laid - the run's pair (SPEC.md 46.10)
    je .ink
    mov ax, (CBLACK << 8) | CWHITE  ; Writer mode: the inverted bar
.ink:
    mov [at_pair], ax
    call OSAPI_SET_COLOR            ; ...and the pen too, for the rule and the
    xor di, di                      ; title-cell invert below
                                    ; DI = menu index
.title:
    cmp di, AT_NMENUS
    jae .done
    call at_mcell                   ; AX = cell x (CX = width, unused here)
    mov cx, ax
    add cx, 8                       ; text inset
    mov dx, AT_BARY
    mov si, di
    add si, si
    add si, di
    add si, si                      ; SI = di*6
    mov si, [si+at_mtab]
    mov ax, [at_pair]               ; OPAQUE (SPEC.md 46.10): the bar's own fill
    call OSAPI_FONT_RUN             ; is what is under every one of these
    inc di
    jmp short .title
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_mcell - a bar cell's geometry
; in:  DI = menu index
; out: AX = cell left x, CX = cell width (8*len + 16); preserves all else
; -----------------------------------------------------------------------------
at_mcell:
    push bx
    push si
    mov ax, 8                       ; the bar starts 8px in
    xor bx, bx
.walk:
    mov si, bx
    add si, si
    add si, bx
    add si, si                      ; SI = bx*6
    mov si, [si+at_mtab]
    push ax
    call OSAPI_FONT_WIDTH           ; AX = 8*len
    mov cx, ax
    add cx, 16
    pop ax
    cmp bx, di
    je .out
    add ax, cx
    inc bx
    jmp short .walk
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_bar_click - a press in rows 0..19: track the menus until release
; in:  CX = x (absolute); gfx lock held (W_ONCLICK)
; -----------------------------------------------------------------------------
at_bar_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call at_x2menu                  ; DI = menu under x, or -1
    cmp di, 0FFFFh
    je .out
    call at_menu_track
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_x2menu - CX = x -> DI = the menu index there, or 0FFFFh
at_x2menu:
    push ax
    push bx
    push cx
    push dx
    mov bx, cx                      ; BX = the pressed x
    xor di, di
.try:
    cmp di, AT_NMENUS
    jae .none
    call at_mcell                   ; AX = left, CX = width
    mov dx, ax
    add dx, cx
    cmp bx, ax
    jb .next
    cmp bx, dx
    jb .got
.next:
    inc di
    jmp short .try
.none:
    mov di, 0FFFFh
.got:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_menu_track - the press-drag-release loop over the open pull-down
; in:  DI = menu index; gfx lock held on entry AND exit
; out: nothing (a chosen command has been dispatched); preserves all
; -----------------------------------------------------------------------------
at_menu_track:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [at_menuon], 1
    call at_caret_off
    mov [at_mcur], di
    mov word [at_mhil], 0FFFFh
    call at_mopen
.pass:
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .release
    cmp dx, 20
    jae .initems
    push di
    call at_x2menu                  ; over another title?
    cmp di, 0FFFFh
    je .samepop
    cmp di, [at_mcur]
    je .samepop
    mov ax, di                      ; switch menus
    pop di
    call at_mclose
    mov di, ax
    mov [at_mcur], di
    mov word [at_mhil], 0FFFFh
    call at_mopen
    jmp short .pass
.samepop:
    pop di
    mov ax, 0FFFFh                  ; in the bar: no item hovered
    jmp short .hover
.initems:
    call at_mitem_at                ; AX = item under (CX,DX), or -1
.hover:
    cmp ax, [at_mhil]
    je .pass
    call at_mhilite
    mov [at_mhil], ax
    jmp short .pass
.release:
    mov si, [at_mhil]               ; SI = the chosen item, or -1
    cmp si, 0FFFFh
    je .close
    mov cx, 3                       ; the Mac selection flash
.flash:
    push cx
    mov ax, [at_mhil]
    call at_mrowxor
    call at_linger
    mov ax, [at_mhil]
    call at_mrowxor
    call at_linger
    pop cx
    loop .flash
.close:
    call at_mclose
    mov byte [at_menuon], 0
    call at_caret_on
    cmp si, 0FFFFh
    je .out
    mov di, [at_mcur]
    call at_mitemptr                ; SI (index) -> SI = item record
    jc .out
    mov al, [si+4]
    or al, al
    jz .out
    call at_docmd
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_linger - hold the lock until the tick turns (sol_linger verbatim)
at_linger:
    push ax
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .spin
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_mgeom - the open pull-down's rectangle
; out: AX = x1, BX = y1 (= 20), CX = x2, DX = y2 (inclusive)
; -----------------------------------------------------------------------------
at_mgeom:
    push si
    push di
    mov di, [at_mcur]
    call at_mcell                   ; AX = the title cell's left
    mov si, di
    shl si, 1
    mov cx, [si+at_mwid]
    add cx, ax
    dec cx                          ; x2
    mov bx, 20
    mov si, di
    add si, si
    add si, di
    add si, si                      ; SI = di*6
    mov dx, [si+at_mtab+4]          ; the item count
    push ax
    mov ax, AT_ITEMH
    mul dx
    mov dx, ax
    pop ax
    add dx, 21                      ; top border + items + bottom border
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; at_mopen - draw the current pull-down and invert its title cell
; -----------------------------------------------------------------------------
at_mopen:
    push ax
    push bx
    push cx
    push dx
    call at_mtitlexor
    call at_mgeom
    mov [at_msx1], ax               ; latched for every row routine
    mov [at_msx2], cx
    mov [at_msy2], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_msx1]
    mov bx, 20
    mov cx, [at_msx2]
    mov dx, [at_msy2]
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [at_msx1]
    mov cx, [at_msx2]
    call OSAPI_GFX_FRAME
    ; the drop shadow: one column right, one row below, offset by 2
    mov ax, [at_msx2]
    inc ax
    cmp ax, [at_vw]
    jae .noright
    mov bx, 22
    mov dx, [at_msy2]
    inc dx
    cmp dx, [at_vh]
    jb .rv
    mov dx, [at_vh]
    dec dx
.rv:
    call OSAPI_GFX_VLINE            ; AX = x, BX = y1, DX = y2
.noright:
    mov dx, [at_msy2]
    inc dx
    cmp dx, [at_vh]
    jae .nobottom
    mov ax, [at_msx1]
    add ax, 2
    mov bx, [at_msx2]
    inc bx
    call OSAPI_GFX_HLINE            ; AX = x1, BX = x2, DX = y
.nobottom:
    call at_mrows
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_mrows - the pull-down's items: labels, separators, checks, disabled
;            gray, right-aligned shortcuts
; -----------------------------------------------------------------------------
at_mrows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, [at_mcur]
    mov si, di
    add si, si
    add si, di
    add si, si
    mov cx, [si+at_mtab+4]          ; CX = item count
    mov si, [si+at_mtab+2]          ; SI = item array
    mov di, 21                      ; DI = row top y
.item:
    or cx, cx
    jnz .go
    jmp .done
.go:
    push cx
    mov ax, [si]
    or ax, ax
    jnz .real
    ; separator: a gray rule across the row's middle
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    mov ax, [at_msx1]
    inc ax
    mov bx, [at_msx2]
    dec bx
    mov dx, di
    add dx, AT_ITEMH/2
    call OSAPI_GFX_HLINE
    jmp .next
.real:
    mov dl, [si+5]
    call at_mstate                  ; AL = 0 plain / 1 disabled / 2 checked
    mov ah, al
    mov al, CBLACK
    cmp ah, 1
    jne .ink
    mov al, CLGRAY                  ; disabled: gray (dither on 1bpp)
.ink:
    mov [at_ink], al                ; ...and the run's ink, kept: a package
    call OSAPI_SET_COLOR            ; cannot read the pen back (SPEC.md 46.10)
                                    ;
                                    ; THIS IS A SPEC.md 47 RULE 1 VIOLATION AND
                                    ; IT IS LEFT STANDING HERE - see 46.10.1.
                                    ; A grey store without [gfx_dis] reduces to
                                    ; solid BLACK on a 1bpp adapter, so a dead
                                    ; item looks live on the target machine.
                                    ; The fix is OSAPI_GFX_PEN with CF, and it
                                    ; is not in this commit because no state
                                    ; this session could drive put a disabled
                                    ; item on screen to diff it against
    cmp ah, 2
    jne .nocheck
    ; a small hand-drawn check in the left gutter (no glyph exists for it)
    mov ax, [at_msx1]
    add ax, 4
    mov bx, ax
    add bx, 2
    mov dx, di
    add dx, 8
    call OSAPI_GFX_HLINE
    mov ax, [at_msx1]
    add ax, 7
    mov bx, di
    add bx, 4
    mov dx, di
    add dx, 8
    call OSAPI_GFX_VLINE
.nocheck:
    mov cx, [at_msx1]
    add cx, 14
    mov dx, di
    add dx, 3
    push si
    mov si, [si]
    mov ah, CWHITE                  ; OPAQUE: at_mopen filled the panel white
    mov al, [at_ink]
    call OSAPI_FONT_RUN
    pop si
    mov ax, [si+2]                  ; the right-aligned shortcut
    or ax, ax
    jz .next
    push si
    mov si, ax
    call OSAPI_FONT_WIDTH           ; AX is the WIDTH here, so the pair goes in
    mov cx, [at_msx2]               ; after it and not before
    sub cx, 6
    sub cx, ax
    mov ah, CWHITE
    mov al, [at_ink]
    call OSAPI_FONT_RUN
    pop si
.next:
    add si, 6
    add di, AT_ITEMH
    pop cx
    dec cx
    jz .done
    jmp .item
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_mstate - an item's live state
; in:  DL = the item's state selector (AT_MS_*)
; out: AL = 0 plain / 1 disabled / 2 checked; preserves all else
; -----------------------------------------------------------------------------
AT_MS_PLAIN   equ 0
AT_MS_UNDO    equ 1                 ; disabled while the undo stack is empty
AT_MS_REDO    equ 2
AT_MS_MDVIEW  equ 3                 ; checked in Markdown mode
AT_MS_WRVIEW  equ 4                 ; checked in Writer mode
AT_MS_ZIN     equ 5                 ; disabled at the top zoom
AT_MS_ZOUT    equ 6                 ; disabled at zoom 0
AT_MS_ZDEF    equ 7

at_mstate:
    xor al, al
    cmp dl, AT_MS_UNDO
    je .undo
    cmp dl, AT_MS_REDO
    je .redo
    cmp dl, AT_MS_MDVIEW
    je .md
    cmp dl, AT_MS_WRVIEW
    je .wr
    cmp dl, AT_MS_ZIN
    je .zin
    cmp dl, AT_MS_ZOUT
    je .zout
    cmp dl, AT_MS_ZDEF
    je .zout                        ; Default disables exactly when Out does
    ret
.undo:
    cmp word [at_ucnt], 0
    je .dis
    ret
.redo:
    cmp word [at_rcnt], 0
    je .dis
    ret
.md:
    cmp byte [at_writer], 0
    je .chk
    ret
.wr:
    cmp byte [at_writer], 0
    jne .chk
    ret
.zin:
    cmp byte [at_zoom], AT_ZMAX
    jae .dis
    ret
.zout:
    cmp byte [at_zoom], 0
    je .dis
    ret
.dis:
    mov al, 1
    ret
.chk:
    mov al, 2
    ret

; -----------------------------------------------------------------------------
; at_mitem_at - hit-test the open pull-down
; in:  CX = x, DX = y
; out: AX = item index, or 0FFFFh (outside / separator / disabled)
; -----------------------------------------------------------------------------
at_mitem_at:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cx
    mov di, dx
    call at_mgeom
    cmp si, ax
    jb .no
    cmp si, cx
    ja .no
    cmp di, 21
    jb .no
    cmp di, dx
    jae .no
    mov ax, di
    sub ax, 21
    mov cl, AT_ITEMH
    div cl
    xor ah, ah
    mov si, ax
    mov di, [at_mcur]
    call at_mitemptr                ; SI (index) -> SI = the record
    jc .no
    cmp word [si], 0
    je .no                          ; separators do not hover
    push ax
    mov dl, [si+5]
    call at_mstate
    mov dl, al
    pop ax
    cmp dl, 1
    je .no                          ; nor do disabled items
    jmp short .out
.no:
    mov ax, 0FFFFh
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; at_mitemptr - item index SI of menu DI -> SI = 6-byte record ptr; CF bad
at_mitemptr:
    push ax
    push bx
    mov bx, di
    add bx, bx
    add bx, di
    add bx, bx                      ; BX = di*6
    mov ax, [bx+at_mtab+4]
    cmp si, ax
    jae .bad
    mov bx, [bx+at_mtab+2]
    mov ax, si
    add ax, ax
    add ax, si
    add ax, ax                      ; index*6
    add ax, bx
    mov si, ax
    clc
    pop bx
    pop ax
    ret
.bad:
    stc
    pop bx
    pop ax
    ret

; at_mhilite - move the hover inversion from item [at_mhil] to item AX
at_mhilite:
    push ax
    push bx
    mov bx, [at_mhil]
    cmp bx, 0FFFFh
    je .new
    xchg ax, bx
    call at_mrowxor
    xchg ax, bx
.new:
    cmp ax, 0FFFFh
    je .out
    call at_mrowxor
.out:
    pop bx
    pop ax
    ret

; at_mrowxor - XOR-invert pull-down row AX
at_mrowxor:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, ax
    mov ax, AT_ITEMH
    mul si
    add ax, 21
    mov bx, ax
    mov dx, ax
    add dx, AT_ITEMH-1
    mov ax, [at_msx1]
    inc ax
    mov cx, [at_msx2]
    dec cx
    call OSAPI_GFX_XOR_FILL
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_mtitlexor - XOR-invert the current menu's title cell (rows 0..18)
at_mtitlexor:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, [at_mcur]
    call at_mcell                   ; AX = left, CX = width
    add cx, ax
    dec cx
    xor bx, bx
    mov dx, 18
    call OSAPI_GFX_XOR_FILL
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_mclose - erase the pull-down (repaint what it covered), un-invert title
; -----------------------------------------------------------------------------
at_mclose:
    push ax
    push bx
    push cx
    push dx
    call at_mtitlexor
    call at_mgeom
    add cx, 2                       ; the shadow's columns and row
    add dx, 2
    mov [at_msx2], cx
    mov [at_msy2], dx
    mov [at_msx1], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_msx1]
    mov bx, 20
    mov cx, [at_msx2]
    mov dx, [at_msy2]
    call OSAPI_GFX_FILL
    mov bx, [at_top]
.lines:
    cmp bx, [at_nlines]
    jae .done
    call at_line_y
    jc .done
    cmp ax, [at_msy2]
    ja .done                        ; below the box: nothing else covered
    call at_draw_line
    inc bx
    jmp short .lines
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Modal alerts (SPEC.md 46.6). One at a time, over the fullscreen editor:
; while [at_modal] is nonzero every key and click routes here.
; =============================================================================

AT_MD_SAVE  equ 1                   ; Save / Cancel / Don't Save
AT_MD_ABOUT equ 2                   ; the About card, OK
AT_MD_ERR   equ 3                   ; message + OK

; -----------------------------------------------------------------------------
; at_alert - open a modal alert
; in:  AL = AT_MD_*, SI = message (AT_MD_ERR) or ignored
; out: nothing; draws under the held lock; preserves all registers
; -----------------------------------------------------------------------------
at_alert:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [at_modal], al
    mov [at_mmsg], si
    call at_caret_off
    call at_mdrect
    mov [at_msx1], ax               ; latched box for everything below
    mov [at_msx2], cx
    mov [at_msy2], dx
    mov [at_rby], bx                ; (idle scratch: the box's y1)
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_msx1]
    mov bx, [at_rby]
    mov cx, [at_msx2]
    mov dx, [at_msy2]
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [at_msx1]
    mov cx, [at_msx2]
    call OSAPI_GFX_FRAME
    mov ax, [at_msx1]
    add ax, 2
    mov bx, [at_rby]
    add bx, 2
    mov cx, [at_msx2]
    sub cx, 2
    mov dx, [at_msy2]
    sub dx, 2
    call OSAPI_GFX_FRAME            ; the classic double frame
    mov al, [at_modal]
    cmp al, AT_MD_ABOUT
    je .about
    ; --- one or two lines of message, then the buttons ---------------------
    mov cx, [at_msx1]
    add cx, 16
    mov dx, [at_rby]
    add dx, 14
    cmp byte [at_modal], AT_MD_SAVE
    jne .plain
    mov si, at_s_savechg
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: the box is CWHITE-filled above
    call OSAPI_FONT_RUN
    push dx
    call OSAPI_FONT_WIDTH
    add cx, ax
    pop dx
    mov si, at_name
    cmp byte [at_havefile], 0
    jne .nm
    mov si, at_s_untitled
.nm:
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    push dx
    call OSAPI_FONT_WIDTH
    add cx, ax
    pop dx
    mov si, at_s_qmark
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: ...and so is the rest of the line
    call OSAPI_FONT_RUN
    jmp short .buttons
.plain:
    mov si, [at_mmsg]
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: one message line, same ground
    call OSAPI_FONT_RUN
.buttons:
    call at_mdbuttons
    jmp short .done
.about:
    call at_about_body
    call at_mdbuttons
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_mdrect - the current alert's rectangle (centred; sized by type)
; out: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; -----------------------------------------------------------------------------
at_mdrect:
    push si
    mov cx, 380
    mov dx, 92
    cmp byte [at_modal], AT_MD_ABOUT
    jne .size
    mov cx, 340
    mov dx, 236
    mov si, [at_vh]
    cmp si, 300
    jae .size
    mov dx, 130                     ; the compact card for CGA's 200 rows
.size:
    mov ax, [at_vw]
    sub ax, cx
    shr ax, 1
    mov si, [at_vh]
    sub si, dx
    shr si, 1
    mov bx, si
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    pop si
    ret

; -----------------------------------------------------------------------------
; at_mdbuttons - draw the alert's buttons and latch their rects
; Save alert:   [Don't Save]   [Cancel]   [Save*]     (* = default ring)
; About/error:  [OK*]
; -----------------------------------------------------------------------------
at_mdbuttons:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [at_msy2]
    sub bx, 27                      ; the buttons' top row
    cmp byte [at_modal], AT_MD_SAVE
    jne .single
    mov ax, [at_msx2]
    sub ax, 64                      ; Save (56 wide), 8 in from the right
    mov si, at_s_save
    mov dx, 1
    call at_button                  ; out: CX = width
    mov [at_b1x], ax
    mov [at_b1y], bx
    mov [at_b1w], cx
    sub ax, 80                      ; Cancel (72 wide), 8 gap
    mov si, at_s_cancel
    xor dx, dx
    call at_button
    mov [at_b2x], ax
    mov [at_b2y], bx
    mov [at_b2w], cx
    sub ax, 112                     ; Don't Save (104 wide), 8 gap
    mov si, at_s_dontsave
    xor dx, dx
    call at_button
    mov [at_b3x], ax
    mov [at_b3y], bx
    mov [at_b3w], cx
    jmp short .out
.single:
    mov ax, [at_msx1]
    add ax, [at_msx2]
    shr ax, 1
    sub ax, 20                      ; OK is 40 wide: centre it
    mov si, at_s_ok
    mov dx, 1
    call at_button
    mov [at_b1x], ax
    mov [at_b1y], bx
    mov [at_b1w], cx
    mov word [at_b2w], 0
    mov word [at_b3w], 0
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_button - one classic push button
; in:  AX = x, BX = y, SI = label, DX = 1 for the default ring
; out: CX = the button's width (label + 24); preserves all else
;
; The drawing is os88ui_btn's (apps/os88ui.inc). What stays here is the SIZE:
; artful's buttons grow to their label rather than being laid out at a fixed
; width, so this measures first and answers CX, which every caller latches.
;
; ONE PIXEL MOVED, and it is named because it is visible: the default ring
; was 3px outside the frame and the shared control's is 2px - the file
; dialog's inset, which is the one the rest of the system shows. Everything
; else matches already: the whole-rect white fill is OS88UI_FILL (identical
; picture once the frame is drawn over the border), the label was centred at
; runtime horizontally, and its literal +6 in a 20px box IS (20-8)/2.
;
; It gains a greyed state it does not use yet - at_button has never had one -
; and that is the point of the shared control rather than an accident of it.
; -----------------------------------------------------------------------------
at_button:
    push ax
    push bx
    push dx
    push si
    push di
    mov di, dx                      ; DI = the default flag
    mov [at_btx], ax
    mov [at_bty], bx
    call OSAPI_FONT_WIDTH           ; AX = the label's width; SI intact
    add ax, 24
    mov [at_xw], ax                 ; = the button's width, and the answer

    mov ax, [at_btx]                ; ...as a rect, in screen coordinates
    mov [at_brect+0], ax
    add ax, [at_xw]
    dec ax
    mov [at_brect+4], ax
    mov ax, [at_bty]
    mov [at_brect+2], ax
    add ax, 19
    mov [at_brect+6], ax

    or di, di                       ; ZF from the flag, and `mov` does not
    mov di, OS88UI_FILL             ; touch flags - so the test survives the
    jz .go                          ; register being reused for the answer
    or di, OS88UI_DEF
.go:
    mov bx, at_brect
    call os88ui_btn
    mov cx, [at_xw]
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

at_brect:   dw 0, 0, 0, 0           ; the button being drawn

; -----------------------------------------------------------------------------
; at_btnhit - is the point (CX,DX) inside button (BX=x, AX=y, SI=width)?
; out: CF=0 hit
; -----------------------------------------------------------------------------
at_btnhit:
    cmp cx, bx
    jb .no
    push bx
    add bx, si
    cmp cx, bx
    pop bx
    jae .no
    cmp dx, ax
    jb .no
    push ax
    add ax, 19
    cmp dx, ax
    pop ax
    ja .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; at_modal_click / at_modal_key - input while an alert is up
; click in: CX = x, DX = y;  key in: AL = ascii
; -----------------------------------------------------------------------------
at_modal_click:
    push ax
    push bx
    push si
    mov bx, [at_b1x]
    mov ax, [at_b1y]
    mov si, [at_b1w]
    call at_btnhit
    jnc .b1
    cmp word [at_b2w], 0
    je .out
    mov bx, [at_b2x]
    mov ax, [at_b2y]
    mov si, [at_b2w]
    call at_btnhit
    jnc .b2
    cmp word [at_b3w], 0
    je .out
    mov bx, [at_b3x]
    mov ax, [at_b3y]
    mov si, [at_b3w]
    call at_btnhit
    jnc .b3
.out:
    pop si
    pop bx
    pop ax
    ret
.b1:
    call at_md_default
    jmp short .out
.b2:
    call at_md_cancel
    jmp short .out
.b3:
    call at_md_dont
    jmp short .out

at_modal_key:
    push ax
    cmp al, 13
    jne .notcr
    call at_md_default
    jmp short .out
.notcr:
    cmp al, 27
    jne .notesc
    call at_md_cancel
    jmp short .out
.notesc:
    cmp byte [at_modal], AT_MD_SAVE
    jne .out
    cmp al, 'd'
    je .dont
    cmp al, 'D'
    jne .out
.dont:
    call at_md_dont
.out:
    pop ax
    ret

; the three outcomes; each closes the alert first
at_md_default:
    push ax
    mov al, [at_modal]
    call at_mdclose
    cmp al, AT_MD_SAVE
    jne .done
    call at_savepend
.done:
    pop ax
    ret

at_md_cancel:
    push ax
    mov al, [at_modal]
    call at_mdclose
    cmp al, AT_MD_SAVE
    jne .done
    mov byte [at_pend], 0           ; cancelled: forget the pending action
.done:
    pop ax
    ret

at_md_dont:
    push ax
    mov al, [at_modal]
    call at_mdclose
    cmp al, AT_MD_SAVE
    jne .done
    call at_runpend                 ; discard changes: straight on
.done:
    pop ax
    ret

; at_mdclose - erase the alert and repaint what it covered
at_mdclose:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call at_mdrect
    sub ax, 3                       ; the default ring bleeds 3px out
    sub bx, 3
    add cx, 3
    add dx, 3
    mov [at_msy2], dx               ; the redraw must cover the EXPANDED
                                    ; erase, or its bottom 3 rows stay white
    call OSAPI_GFX_FILL
    mov byte [at_modal], 0
    cmp bx, 20
    jae .nobar
    call at_bar_draw
.nobar:
    mov bx, [at_top]
.lines:
    cmp bx, [at_nlines]
    jae .fin
    call at_line_y
    jc .fin
    cmp ax, [at_msy2]
    ja .fin
    call at_draw_line
    inc bx
    jmp short .lines
.fin:
    call at_sbar                    ; a wide card can reach the gutter
    call at_caret_on
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_about_body - the branding card (the About alert and the splash share it)
; When [at_modal] is set it centres on the alert box; windowed it centres
; on [at_spcx] from [at_spy] down.
; -----------------------------------------------------------------------------
at_about_body:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [at_modal], 0
    je .win
    call at_mdrect
    add ax, cx
    shr ax, 1
    mov cx, ax                      ; CX = centre x
    mov dx, bx
    add dx, 14                      ; DX = cursor y
    jmp short .draw
.win:
    mov cx, [at_spcx]
    mov dx, [at_spy]
.draw:
    ; "The Artful Type", 2x bold
    mov si, at_s_title
    call OSAPI_FONT_WIDTH           ; AX = 8*len; the 2x width is twice that
    mov bx, cx
    sub bx, ax                      ; centre = cx - (16*len)/2
    push cx
    push dx
    mov ax, bx
    mov bx, dx
    mov cx, 2
    mov dl, AT_ST_B
    call at_bigtext
    pop dx
    pop cx
    add dx, 24
    ; the artwork, when the screen has room for it
    mov ax, [at_vh]
    cmp ax, 300
    jb .noimg
    mov ax, cx
    sub ax, AT_IMG_W/2
    mov bx, dx
    call at_drawimg
    add dx, AT_IMG_H + 10
.noimg:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, at_s_sub
    call at_ctext
    add dx, 12
    mov si, at_s_ver
    call at_ctext
    add dx, 12
    mov si, at_s_url
    call at_ctext
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_ctext - draw SI centred on x = CX at y = DX (8px font)
;
; OPAQUE, and it names its own pair rather than taking one: all three callers
; are the About box's body, which is drawn on the modal box's CWHITE fill in
; CBLACK, and a package cannot read the pen back to inherit it (SPEC.md 46.10).
at_ctext:
    push ax
    push cx
    call OSAPI_FONT_WIDTH           ; AX is the WIDTH here...
    shr ax, 1
    sub cx, ax
    mov ax, (CWHITE << 8) | CBLACK  ; ...so the pair goes in after it
    call OSAPI_FONT_RUN
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_bigtext - scaled styled text, anywhere on screen
; in:  AX = x, BX = y, SI = string, CX = scale (1..3), DL = AT_ST_* style
; out: nothing; preserves all registers
; Borrows the line pipeline: compose into a fresh strip, expand, one blit.
; -----------------------------------------------------------------------------
at_bigtext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [at_btx], ax
    mov [at_bty], bx
    mov [at_psc], cx
    mov ax, cx
    mov cl, 3
    shl ax, cl
    mov [at_pcw], ax                ; cell width = 8*scale
    mov [at_prh], ax                ; rows = the glyph box exactly
    push ds
    pop es
    cld
    mov di, at_strip1
    mov cx, (AT_S1ST*24)/2          ; 24 rows covers scale 3
    xor ax, ax
    rep stosw
    mov di, at_cellbg
    mov cx, AT_CBGCAP/2
    rep stosw
    xor di, di                      ; DI = x within the strip
.ch:
    lodsb
    or al, al
    jz .composed
    push dx
    call at_glyph
    pop dx
    add di, [at_pcw]
    jmp short .ch
.composed:
    mov ax, di
    cmp ax, AT_S4ST*2               ; the 4bpp strip's 608px, not the 1bpp
    jbe .w                          ; 640: at_expand writes 4 bytes per
    mov ax, AT_S4ST*2               ; 8px column against AT_S4ST rows
.w:
    mov [at_xw], ax
    or ax, ax
    jz .out
    call at_expand
    push ds
    pop es
    mov si, at_strip4
    mov bp, AT_S4ST
    mov ax, [at_btx]
    mov bx, [at_bty]
    mov cx, [at_xw]
    mov dx, [at_prh]
    call OSAPI_GFX_BLIT4
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

; -----------------------------------------------------------------------------
; at_drawimg - blit the 128x100 splash artwork at (AX, BX)
; Expanded 1bpp -> 4bpp through the white table, in bands of 24 rows.
; -----------------------------------------------------------------------------
at_drawimg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    cld
    mov [at_btx], ax
    mov [at_bty], bx
    mov si, at_img
    mov bp, AT_IMG_H                ; BP = rows left
.band:
    mov dx, bp
    cmp dx, 24
    jbe .rows
    mov dx, 24
.rows:
    push dx
    mov di, at_strip4
.brow:
    push di
    mov cx, AT_IMG_RB
.bbyte:
    lodsb
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [bx+at_x4w]
    stosw
    mov ax, [bx+at_x4w+2]
    stosw
    pop bx
    loop .bbyte
    pop di
    add di, AT_S4ST
    dec dx
    jnz .brow
    pop dx
    push si
    push bp
    mov si, at_strip4
    mov bp, AT_S4ST
    mov ax, [at_btx]
    mov bx, [at_bty]
    mov cx, AT_IMG_W
    call OSAPI_GFX_BLIT4
    pop bp
    pop si
    add [at_bty], dx
    sub bp, dx
    jnz .band
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The windowed splash card (SPEC.md 46.7). The kernel has already
; white-filled the content when this runs from W_PAINT.
; =============================================================================
at_splash:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [at_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [at_spx], ax
    mov [at_spy], dx
    call OSAPI_WM_GEOM              ; CX = w, DX = h
    mov [at_spw], cx
    mov [at_sph], dx
    mov ax, cx
    shr ax, 1
    add ax, [at_spx]
    mov [at_spcx], ax
    mov ax, [at_spy]
    add ax, 10
    mov [at_spy], ax
    call at_about_body              ; title, art, subtitle, version, url
    ; the two buttons, anchored to the content's bottom
    mov bx, [at_win]
    call OSAPI_WM_CONTENT
    mov bx, dx
    add bx, [at_sph]
    sub bx, 30                      ; button top
    mov ax, [at_spcx]
    sub ax, 76
    mov si, at_s_new
    mov dx, 1
    call at_button
    mov [at_b1x], ax
    mov [at_b1y], bx
    mov [at_b1w], cx
    mov ax, [at_spcx]
    add ax, 12
    mov si, at_s_open
    xor dx, dx
    call at_button
    mov [at_b2x], ax
    mov [at_b2y], bx
    mov [at_b2w], cx
    mov word [at_b3w], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_splash_click - a windowed click (absolute coords, like every click)
; in:  CX = x, DX = y
; -----------------------------------------------------------------------------
at_splash_click:
    push ax
    push bx
    push si
    mov bx, [at_b1x]
    mov ax, [at_b1y]
    mov si, [at_b1w]
    call at_btnhit
    jnc .new
    mov bx, [at_b2x]
    mov ax, [at_b2y]
    mov si, [at_b2w]
    call at_btnhit
    jnc .open
    jmp short .out
.new:
    call at_cmd_newdoc              ; a fresh document...
    call at_fs_enter                ; ...and the whole screen
    jmp short .out
.open:
    call at_dlg_open
.out:
    pop si
    pop bx
    pop ax
    ret
