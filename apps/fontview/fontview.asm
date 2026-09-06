; =============================================================================
; os8088 - apps/fontview/fontview.asm
;
; FONT VIEWER lists the .F88 families in the system disk's FONTS/ folder and
; renders an editable specimen in the selected face (SPEC.md 90).  It is a
; normal package, carried in APPS/ on the system disk; the F88 declaration in
; its header is all the file manager needs to launch it from a face file.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FONT VIEWER', fv_entry, 3

; A page carrying one large A: a font document rather than a generic app.
    OS88_ICON16
    dw 0x7FF0, 0x7FF8, 0x7FFC, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x0000, 0x3FE0, 0x2030, 0x2028
    dw 0x2184, 0x2284, 0x2444, 0x27C4
    dw 0x2844, 0x3024, 0x3024, 0x2004
    dw 0x2004, 0x2004, 0x3FFC, 0x0000
    OS88_ICON16_END

; The declaration is harvested while the disk is mounted.  No run-time
; registration and no kernel special case are involved (SPEC.md 54.6, 90).
    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'F88'
    OS88_ASSOC16_END

FV_W        equ 620                 ; fits VGA/CGA; leaves room for long samples
FV_H        equ 155                 ; the whole CGA desktop band, dock excluded
FV_CW       equ FV_W - 2
FV_CH       equ FV_H - TITLE_H - 1
FV_DIVX     equ 116
FV_LISTX    equ 5
FV_LISTY    equ 15
FV_ROWH     equ 11
FV_RIGHTX   equ 124
FV_TEXTX    equ 128                 ; 8-aligned from a snapped content origin
FV_BOXY     equ 29
FV_BOXBOT   equ 116
FV_TEXTY    equ 35
FV_SAMPLEW  equ 480
FV_TEXTMAX  equ 127

; -----------------------------------------------------------------------------
; fv_entry - scan the machine's faces, make the window and defer the first
; face read until the window is visible.
; -----------------------------------------------------------------------------
fv_entry:
    push si
    push di
    call ty_init
    call fv_take_arg                ; first: OSAPI_ARG_FILE is read-and-clear
    call ty_scan
    call fv_pick_arg

    mov byte [fv_loaded], 0xFF
    mov byte [fv_pending], 1
    mov byte [fv_textlen], FV_INITLEN
    mov si, fv_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [fv_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; makes fv_x + FV_TEXTX byte-aligned
    mov ax, fv_onwake
    call OSAPI_WM_ONWAKE
    call OSAPI_WM_WAKE              ; ordinary launch needs the same first load
    clc
.out:
    pop di
    pop si
    ret

; Record the associated file name while its kernel pointer is still live.
fv_take_arg:
    pushf
    push ax
    push cx
    push si
    push di
    push es
    call OSAPI_ARG_FILE
    jc .out
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, fv_arg
    mov cx, TY_NAMSZ
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .have
    loop .copy
.have:
    mov byte [fv_arg + TY_NAMSZ - 1], 0
    mov byte [fv_arghave], 1
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    popf
    ret

; If an F88 launched us, make that family the initial selection.  A foreign
; face whose name is not installed still opens the catalogue at its first row.
fv_pick_arg:
    cmp byte [fv_arghave], 0
    je .out
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    xor bx, bx
    mov si, ty_fnames
.family:
    cmp bl, [ty_nfam]
    jae .done
    mov di, fv_arg
    mov cx, TY_NAMSZ
    push si
    repe cmpsb
    pop si
    je .found
    add si, TY_NAMSZ
    inc bl
    jmp short .family
.found:
    mov [fv_selected], bl
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; fv_onwake - the only path that turns the floppy for a face change.
; W_ONKEY/W_ONCLICK merely choose and post this callback (SPEC.md 90.1).
; -----------------------------------------------------------------------------
fv_onwake:
    cmp byte [fv_pending], 0
    je .out
    mov byte [fv_pending], 0
    cmp byte [ty_nfam], 0
    je .paint
    mov al, [fv_selected]
    cmp al, [fv_loaded]
    je .paint
    call ty_openfam
    jc .bad
    mov dl, al                      ; new handle survives selecting/caching it
    xor ah, ah
    call ty_use
    call ty_cache                   ; refusal is harmless: ty_put falls back
    mov al, [fv_face]
    call ty_close
    mov [fv_face], dl
    mov al, [fv_selected]
    mov [fv_loaded], al
    mov byte [fv_error], 0
    jmp short .paint
.bad:
    mov [fv_error], al
.paint:
    call OSAPI_GFX_LOCK
    call fv_redraw
    call OSAPI_GFX_UNLOCK
.out:
    ret

; -----------------------------------------------------------------------------
; Keyboard: arrows walk the catalogue; printable ASCII and Backspace edit the
; specimen.  The right pane alone is repainted for ordinary typing.
; -----------------------------------------------------------------------------
fv_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp ah, KSC_UP
    je .prev
    cmp ah, KSC_LEFT
    je .prev
    cmp ah, KSC_DOWN
    je .next
    cmp ah, KSC_RIGHT
    je .next
    cmp al, 8
    je .back
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    mov bl, [fv_textlen]
    cmp bl, FV_TEXTMAX
    jae .out
    xor bh, bh
    mov [fv_text + bx], al
    inc bx
    mov byte [fv_text + bx], 0
    inc byte [fv_textlen]
    call fv_redraw_right
    jmp short .out
.back:
    cmp byte [fv_textlen], 0
    je .out
    dec byte [fv_textlen]
    xor bx, bx
    mov bl, [fv_textlen]
    mov byte [fv_text + bx], 0
    call fv_redraw_right
    jmp short .out
.prev:
    cmp byte [ty_nfam], 0
    je .out
    mov al, [fv_selected]
    or al, al
    jnz .pdec
    mov al, [ty_nfam]
.pdec:
    dec al
    call fv_choose
    jmp short .out
.next:
    cmp byte [ty_nfam], 0
    je .out
    mov al, [fv_selected]
    inc al
    cmp al, [ty_nfam]
    jb .nset
    xor al, al
.nset:
    call fv_choose
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A click in a catalogue row selects it.  CX/DX arrive in screen coordinates.
fv_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov di, cx
    mov bp, dx
    mov bx, si
    call OSAPI_WM_CONTENT
    add ax, FV_DIVX
    cmp di, ax
    jae .out
    add dx, FV_LISTY
    cmp bp, dx
    jb .out
    sub bp, dx
    mov ax, bp
    mov bl, FV_ROWH
    div bl                          ; AL = row
    cmp al, [ty_nfam]
    jae .out
    call fv_choose
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; in: AL = valid family index, SI = window; gfx lock held
fv_choose:
    cmp al, [fv_selected]
    je .out
    mov [fv_selected], al
    mov byte [fv_pending], 1
    mov byte [fv_error], 0
    call fv_redraw                 ; marker + "Loading...", no disk work
    mov bx, si
    call OSAPI_WM_WAKE
.out:
    ret

; -----------------------------------------------------------------------------
; Painting
; -----------------------------------------------------------------------------
fv_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [fv_x], ax
    mov [fv_y], dx
    call fv_draw
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Full self-repaint.  Caller holds the gfx lock.
fv_redraw:
    push ax
    push bx
    push cx
    push dx
    mov bx, [fv_win]
    call OSAPI_WM_CONTENT
    mov [fv_x], ax
    mov [fv_y], dx
    mov bx, dx
    mov cx, ax
    add cx, FV_CW - 1
    add dx, FV_CH - 1
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call fv_draw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fv_draw:
    call fv_draw_list
    call fv_draw_right
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [fv_x]
    add ax, FV_DIVX
    mov bx, [fv_y]
    add bx, 3
    mov dx, [fv_y]
    add dx, FV_CH - 4
    call OSAPI_GFX_VLINE
    ret

fv_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [fv_x]
    add cx, FV_LISTX
    mov dx, [fv_y]
    add dx, 4
    mov si, fv_s_fonts
    call fv_sysline
    cmp byte [ty_nfam], 0
    jne .rows
    add dx, 13
    mov si, fv_s_none
    call fv_sysline
    jmp short .hint
.rows:
    xor bx, bx
.row:
    cmp bl, [ty_nfam]
    jae .hint
    mov byte [fv_line], ' '
    cmp bl, [fv_selected]
    jne .mark
    mov byte [fv_line], '>'
.mark:
    mov byte [fv_line + 1], ' '
    mov al, bl
    call ty_famname
    mov di, fv_line + 2
    call fv_copy
    mov byte [di], 0
    mov cx, [fv_x]
    add cx, FV_LISTX
    mov dx, bx
    and dx, 0x00FF
    mov ax, dx
    mov dl, FV_ROWH
    mul dl
    mov dx, [fv_y]
    add dx, FV_LISTY
    add dx, ax
    mov si, fv_line
    call fv_sysline
    inc bl
    jmp short .row
.hint:
    mov cx, [fv_x]
    add cx, FV_LISTX
    mov dx, [fv_y]
    add dx, FV_CH - 12
    mov si, fv_s_pick
    call fv_sysline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fv_draw_right:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [fv_x]
    add cx, FV_RIGHTX
    mov dx, [fv_y]
    add dx, 4
    mov si, fv_s_spec
    call fv_sysline

    mov di, fv_line
    cmp byte [ty_nfam], 0
    jne .family
    mov si, fv_s_builtin
    call fv_copy
    jmp short .state
.family:
    mov al, [fv_selected]
    call ty_famname
    call fv_copy
.state:
    cmp byte [fv_pending], 0
    je .error
    mov si, fv_s_loading
    call fv_copy
.error:
    cmp byte [fv_error], 0
    je .named
    mov si, fv_s_error
    call fv_copy
.named:
    mov byte [di], 0
    mov cx, [fv_x]
    add cx, FV_RIGHTX
    mov dx, [fv_y]
    add dx, 16
    mov si, fv_line
    call fv_sysline

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [fv_x]
    add ax, FV_RIGHTX - 3
    mov bx, [fv_y]
    add bx, FV_BOXY
    mov cx, [fv_x]
    add cx, FV_CW - 5
    mov dx, [fv_y]
    add dx, FV_BOXBOT
    call OSAPI_GFX_FRAME

    call fv_draw_sample
    mov cx, [fv_x]
    add cx, FV_RIGHTX
    mov dx, [fv_y]
    add dx, FV_CH - 12
    mov si, fv_s_type
    call fv_sysline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Erase and repaint only the pane touched by an ordinary keystroke.
fv_redraw_right:
    push ax
    push bx
    push cx
    push dx
    mov ax, [fv_x]
    add ax, FV_DIVX + 1
    mov bx, [fv_y]
    mov cx, [fv_x]
    add cx, FV_CW - 1
    mov dx, [fv_y]
    add dx, FV_CH - 1
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call fv_draw_right
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Wrap the mutable specimen by the current face's advances and emit one band
; per row.  ES:SI is kept in our segment for all type-library calls.
fv_draw_sample:
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
    mov si, fv_text
    xor cx, cx
    mov cl, [fv_textlen]
    mov di, [fv_y]
    add di, FV_TEXTY
.line:
    jcxz .out
    mov ax, FV_SAMPLEW
    push cx
    call ty_fit                    ; CX = chars, AX = their pixel width
    mov bp, cx
    pop cx
    or bp, bp
    jz .out
    call ty_getrows
    xor ah, ah
    mov dx, ax
    mov ax, di
    add ax, dx
    mov bx, [fv_y]
    add bx, FV_BOXBOT - 3
    cmp ax, bx
    ja .out
    call ty_band
    push cx
    mov cx, bp
    xor ax, ax
    call ty_putn
    pop cx
    mov ax, [fv_x]
    add ax, FV_TEXTX
    mov bx, di
    mov dx, 0
    mov dl, [ty_rows]
    push cx
    mov cx, FV_SAMPLEW
    call ty_flush
    pop cx
    add si, bp
    sub cx, bp
    xor ax, ax
    mov al, [ty_rows]
    add di, ax
    xor ax, ax
    mov al, [ty_lead]
    add di, ax
    jmp short .line
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

; One opaque line in the always-readable system face.
fv_sysline:
    push ax
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop ax
    ret

; SI -> NUL source, DI -> destination; copy without NUL, advance DI.
fv_copy:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc si
    inc di
    jmp short .c
.out:
    pop ax
    ret

%include "os88type.inc"

fv_tpl:
    dw 10, 22, FV_W, FV_H
    dw fv_title, fv_paint, fv_onkey, fv_onclick

fv_title:      db 'Font Viewer', 0
fv_s_fonts:    db 'FONTS', 0
fv_s_none:     db 'No .F88 files', 0
fv_s_pick:     db 'Click/arrows', 0
fv_s_spec:     db 'SPECIMEN', 0
fv_s_builtin:  db 'Built-in', 0
fv_s_loading:  db '  Loading...', 0
fv_s_error:    db '  Open failed', 0
fv_s_type:     db 'Type to edit; Backspace deletes', 0

fv_text:
    db 'The quick brown fox jumps over the lazy dog. ABC abc 0123456789 !?'
FV_INITLEN equ $ - fv_text
    times FV_TEXTMAX + 1 - FV_INITLEN db 0

FV_BSS_OWN equ 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + TY_NAMSZ + 24
    OS88_BSS FV_BSS_OWN + TY_BSS_SIZE
    OS88_IMAGE_END

fv_win       equ os88_image_end + 0     ; word
fv_x         equ os88_image_end + 2     ; content origin
fv_y         equ os88_image_end + 4
fv_selected  equ os88_image_end + 6     ; family row requested
fv_loaded    equ os88_image_end + 7     ; family row actually open
fv_face      equ os88_image_end + 8     ; ty_* handle (0 = built-in)
fv_pending   equ os88_image_end + 9
fv_error     equ os88_image_end + 10
fv_arghave   equ os88_image_end + 11
fv_textlen   equ os88_image_end + 12
fv_arg       equ os88_image_end + 13    ; TY_NAMSZ bytes
fv_line      equ fv_arg + TY_NAMSZ      ; 24-byte composed label

    TY_BSS os88_image_end + FV_BSS_OWN
