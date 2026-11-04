; =============================================================================
; os8088 - apps/vbattle/vbband.inc - THE BAND (SPEC.md 125.5): the rows under
; the canvas, from the header's band row to the screen's bottom. Composed in
; a buffer of its own ([vb_bseg], 80 bytes a row): the band's PICTURE (or
; the plain look: black, two cyan rules, a divider), with the open menu's
; picture over it. Text is drawn over the buffer's bytes, ground and glyph
; in one store a byte (6.1's rule, by hand), so a picture shows through
; around every letter. [vb_dirty]: 1 the menu, 2 the party, 4 the line,
; 8 the buffer rebuilt (and everything drawn again)
; =============================================================================

COL_PARTY   equ 18
ROW_MSG     equ 191
BG_TEXT     equ 0xF0            ; a text colour's ground: the buffer's

; vb_by - AX = the band's first row (the header's)
%macro VB_BY 0
    mov al, [vb_meta+VH_BAND+1]
    xor ah, ah
%endmacro

; vb_band_frame - the buffer composed and put on the screen
vb_band_frame:
    push ds
    pop es
    mov es, [vb_bseg]
    xor di, di
    mov cx, 8000                    ; (16 KB: every row there can be)
    xor ax, ax
    cld
    rep stosw
    mov al, [RL+RH_BANDIMG]
    cmp al, NONE
    je .plain
    call vb_blit_img
    jmp short .menu
.plain:
    VB_BY                           ; the plain look: rules at the band's
    mov bx, ax                      ; second row and above the line
    mov dx, 1
    call .rule
    mov dx, ROW_MSG - 2
    sub dx, bx
    call .rule
    mov dx, 3                       ; the divider, between menu and party
.v:
    mov ax, dx
    add ax, bx
    cmp ax, ROW_MSG - 3
    jae .menu
    push dx
    mov ax, 80
    mul dx
    pop dx
    mov di, ax
    add di, (COL_PARTY - 2) * 2
    mov byte [es:di], 0x40
    inc dx
    jmp .v
.menu:
    cmp byte [vb_state], GS_MENU    ; the open menu's own picture
    je .m
    cmp byte [vb_state], GS_TARGET
    jne .put
.m:
    mov al, [vb_menu]
    call vb_menurec
    mov al, [si+MN_IMG]
    cmp al, NONE
    je .put
    call vb_blit_img
.put:
    ret
.rule:                              ; buffer row DX all cyan (1-1-1-1)
    mov ax, 80
    mul dx
    mov di, ax
    mov cx, 40
    mov ax, 0x5555
    rep stosw
    ret

; vb_band_put - the COMPOSED band ([vb_cseg]) onto the screen, row by row
; (CGA's two banks): every pixel of it written once
vb_band_put:
    push ds
    VB_BY
    mov dx, ax
    xor si, si
    mov es, [vb_vseg]
    mov ds, [vb_cseg]
.r:
    call vb_rowaddr
    mov cx, 40
    rep movsw
    inc dx
    cmp dx, 200
    jb .r
    pop ds
    ret

; vb_blit_img - band picture AL into the buffer at its place
vb_blit_img:
    push ds
    pop es
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [RL+RH_OIMG]
    mov si, ax
    add si, RL
    mov al, [si+1]                  ; its row, its column: the buffer's
    mov ah, 80                      ; offset
    mul ah
    mov di, ax
    mov al, [si]
    xor ah, ah
    shl ax, 1
    add di, ax
    mov cl, [si+2]                  ; cells
    xor ch, ch
    mov bl, [si+3]                  ; rows
    xor bh, bh
    mov ax, [si+4]                  ; its paragraph
    add ax, [vb_iseg]
    push ds
    mov es, [vb_bseg]
    mov ds, ax
    xor si, si
.row:
    or bx, bx
    jz .o
    push cx
    push di
    rep movsw                       ; (a cell is a word)
    pop di
    pop cx
    add di, 80
    dec bx
    jmp .row
.o:
    pop ds
    ret

vb_band:
    cmp byte [vb_banddone], 0       ; the openings draw the whole screen
    jne .live
    ret
.live:
    mov al, [vb_dirty]
    or al, al
    jnz .go
    ret
.go:
    test al, 8
    jz .t
    ; THE BAND REBUILT is composed off the screen and put on it once, its
    ; text and all (6.1's rule for a whole band: no frame shows the ground
    ; without its letters)
    mov byte [vb_dirty], 0
    call vb_band_frame              ; the ground, in [vb_bseg]
    push ds
    mov cx, 8000
    xor si, si
    xor di, di
    mov es, [vb_cseg]
    mov ds, [vb_bseg]
    cld
    rep movsw
    pop ds
    mov byte [vb_tcomp], 1          ; the text into [vb_cseg]
    call vb_draw_menu
    call vb_draw_party
    call vb_draw_msg
    mov byte [vb_tcomp], 0
    jmp vb_band_put
.t:
    mov al, [vb_dirty]
    mov byte [vb_dirty], 0
    test al, 1
    jz .p
    push ax
    call vb_draw_menu
    pop ax
.p:
    test al, 2
    jz .m
    push ax
    call vb_draw_party
    pop ax
.m:
    test al, 4
    jz .out
    call vb_draw_msg
.out:
    ret

; vb_draw_menu - the open menu's items at its place, the cursor an arrow or
; inverse as its look says; an action its cost cannot pay for in red
vb_draw_menu:
    cmp byte [vb_state], GS_MENU
    je .go
    cmp byte [vb_state], GS_TARGET
    je .go
    ret
.go:
    mov al, [vb_menu]
    call vb_menurec
    mov [vb_mrec], si
    xor bp, bp
.l:
    mov si, [vb_mrec]
    mov ax, bp
    cmp al, [si+MN_N]
    jb .line
    ret
.line:
    push ds
    pop es
    mov di, vb_lbuf
    mov cx, 15
    mov al, ' '
    cld
    rep stosb
    mov byte [di], 0
    mov di, vb_lbuf
    test byte [si+MN_FLAGS], MF_ARROW
    jz .lab
    mov al, ' '
    mov bx, bp
    cmp bl, [vb_cursor]
    jne .ar
    mov al, '>'
.ar:
    mov [di], al
    inc di
.lab:
    mov bx, bp
    shl bx, 1
    mov ax, [si+MN_ITEMS+bx]        ; AL the index, AH the kind
    mov byte [vb_tc], 0
    or ah, ah
    jz .act
    push di                         ; a sub-menu: its label
    call vb_menurec
    pop di
    mov cx, 8
    call vb_strncpy
    mov byte [di], ' '
    jmp short .col
.act:
    push di
    call vb_act
    pop di
    push si
    mov cx, 8
    call vb_strncpy
    mov byte [di], ' '
    pop si
    cmp byte [si+AC_COST], C_MP
    jne .afford
    mov ax, [si+AC_COSTN]
    mov di, vb_lbuf + 10
    push si
    call vb_utoa
    pop si
    mov word [di], 'MP'
.afford:
    mov bl, [vb_turn]
    push si
    call vb_canpay
    pop si
    jnc .col
    mov byte [vb_tc], 2             ; (it cannot pay: red)
.col:
    mov si, [vb_mrec]
    mov byte [vb_lbuf+15], 0
    mov bh, [si+MN_INK]
    cmp byte [vb_tc], 0
    je .ink
    mov bh, [vb_tc]
.ink:
    or bh, BG_TEXT
    test byte [si+MN_FLAGS], MF_ARROW
    jnz .put
    mov ax, bp
    cmp al, [vb_cursor]
    jne .put
    mov bh, 0x30                    ; the cursor: black on white
.put:
    mov al, [si+MN_PITCH]
    mov ah, 0
    mul bp
    add al, [si+MN_ROW]
    adc ah, 0
    mov dx, ax
    VB_BY
    add dx, ax
    mov cl, [si+MN_COL]
    xor ch, ch
    mov si, vb_lbuf
    push bp
    call vb_text
    pop bp
    inc bp
    jmp .l

; vb_draw_party - the enemy's name (and stance, when it has more than one),
; then each character: name, HP/most, MP; whose turn it is, or who is
; being aimed at, inverse; the fallen red; a full limit gauge starred
vb_draw_party:
    push ds
    pop es
    mov di, vb_lbuf
    mov cx, 22
    mov al, ' '
    cld
    rep stosb
    mov byte [di], 0
    mov di, vb_lbuf
    mov si, RL+RH_ENAME
    mov cx, 10
    call vb_strncpy
    mov byte [di], ' '
    cmp byte [RL+RH_NST], 1
    jbe .en
    inc di
    mov al, [vb_stance]
    call vb_strec
    mov cx, 8
    call vb_strncpy
    mov byte [di], ' '
.en:
    mov byte [vb_lbuf+22], 0
    VB_BY
    add ax, 3
    mov dx, ax
    mov cx, COL_PARTY
    mov si, vb_lbuf
    mov bh, BG_TEXT + 2             ; red
    call vb_text
    xor bp, bp
.m:
    mov ax, bp
    cmp al, [RL+RH_NCH]
    jb .c
    ret
.c:
    push ds
    pop es
    mov di, vb_lbuf                 ; 22 cells: name 6, *, HP 4/4, MP 4
    mov cx, 22
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov bx, bp
    call vb_char
    push si
    mov di, vb_lbuf
    mov cx, 6
    call vb_strncpy
    mov byte [di], ' '
    mov bx, bp
    cmp byte [vb_lim+bx], 100
    jb .nl
    mov byte [vb_lbuf+6], '*'
.nl:
    mov bx, bp
    shl bx, 1
    mov ax, [vb_hp+bx]
    mov di, vb_lbuf+7
    call vb_rjust4
    pop si
    mov byte [vb_lbuf+11], '/'
    mov ax, [si+CH_HP]
    mov di, vb_lbuf+12
    call vb_utoa
    mov byte [di], ' '
    mov bx, bp
    shl bx, 1
    mov ax, [vb_mp+bx]
    mov di, vb_lbuf+18
    call vb_rjust4
    mov byte [vb_lbuf+22], 0
    mov bh, BG_TEXT + 3
    mov bx, bp
    shl bx, 1
    cmp word [vb_hp+bx], 0
    mov bh, BG_TEXT + 3
    jne .up
    mov bh, BG_TEXT + 2             ; down: red
    jmp short .put
.up:
    mov ax, bp
    cmp byte [vb_state], GS_TARGET
    jne .mn
    cmp al, [vb_tcur]
    jne .put
    mov bh, 0x30
    jmp short .put
.mn:
    cmp byte [vb_state], GS_MENU
    jne .put
    cmp al, [vb_turn]
    jne .put
    mov bh, 0x30
.put:
    push bx
    mov ax, bp
    mov cl, 9
    mul cl
    add ax, 12
    mov dx, ax
    VB_BY
    add dx, ax
    pop bx
    mov cx, COL_PARTY
    mov si, vb_lbuf
    push bp
    call vb_text
    pop bp
    inc bp
    jmp .m

; vb_rjust4 - AX right-justified in four cells at DS:DI
vb_rjust4:
    push di
    mov di, vb_nbuf
    call vb_utoa
    mov cx, di
    sub cx, vb_nbuf                 ; its digits
    pop di
    mov ax, 4
    sub ax, cx
    jbe .cp
    add di, ax
.cp:
    mov si, vb_nbuf
.c:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp .c
.out:
    ret

vb_draw_msg:
    push ds
    pop es
    mov di, vb_lbuf
    mov cx, 38
    mov al, ' '
    cld
    rep stosb
    mov byte [di], 0
    mov si, [vb_msg]
    mov di, vb_lbuf
.c:
    lodsb
    or al, al
    jz .d
    cmp di, vb_lbuf+38
    jae .d
    stosb
    jmp .c
.d:
    mov dx, ROW_MSG
    mov cx, 1
    mov si, vb_lbuf
    mov bh, BG_TEXT + 3
    jmp vb_text

; vb_text - DS:SI (NUL) at cell column CX, pixel row DX; BH = colours: the
; low nibble the ink (0..3), the high the ground (0..3), or F: the band's
; buffer under it. Each cell is its 8 rows of 2 bytes, ground and glyph in
; one store (6.1's rule)
vb_text:
    push bp
    mov [vb_tcol], cx
    mov [vb_trow], dx
    mov al, bh
    and al, 3
    mov [vb_tink], al
    mov ah, 0x55                    ; the ink in every pixel of a byte
    mul ah
    mov [vb_tinkp], al
    mov al, bh
    mov cl, 4
    shr al, cl
    mov [vb_tgnd], al
    mov byte [vb_tbg], 0
    cmp al, 0x0F
    jne .solid
    mov byte [vb_tbg], 1
    mov byte [vb_tink], 3           ; the buffer's ground: the expansion is
    mov byte [vb_tgnd], 0           ; a MASK (11 where the glyph is)
.solid:
    and byte [vb_tgnd], 3
    xor bx, bx
.x:
    mov cx, 4
    xor ah, ah
    mov dl, bl
.x1:
    shl ah, 1
    shl ah, 1
    test dl, 8
    jz .g
    or ah, [vb_tink]
    jmp short .x2
.g:
    or ah, [vb_tgnd]
.x2:
    shl dl, 1
    loop .x1
    mov [vb_texp+bx], ah
    inc bx
    cmp bx, 16
    jb .x
    mov es, [vb_vseg]
    cmp byte [vb_tcomp], 0
    je .ch
    mov es, [vb_cseg]
.ch:
    lodsb
    or al, al
    jz .out
    cmp [vb_tcol], word 40
    jae .out
    sub al, 32
    jae .ok
    xor al, al
.ok:
    cmp al, 94
    jbe .ok2
    xor al, al
.ok2:
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, vb_font
    mov bp, ax                      ; the glyph's 8 rows
    mov dx, [vb_trow]
    mov cx, 8
.r:
    push cx
    cmp byte [vb_tcomp], 0
    je .scr
    mov al, dl                      ; composing: the buffer's own row
    sub al, [vb_meta+VH_BAND+1]
    mov ah, 80
    mul ah
    mov di, ax
    jmp short .col
.scr:
    call vb_rowaddr
.col:
    mov ax, [vb_tcol]
    shl ax, 1
    add di, ax
    mov al, [ds:bp]
    mov ah, al
    mov cl, 4
    shr al, cl                      ; the left four pixels' bits
    and ah, 0x0F                    ; ...and the right four's
    xor bh, bh
    mov bl, al
    mov al, [vb_texp+bx]
    mov bl, ah
    mov ah, [vb_texp+bx]
    cmp byte [vb_tbg], 0            ; (the buffer's ground?)
    je .st
    call vb_tground
.st:
    stosw
    inc bp
    inc dx
    pop cx
    loop .r
    inc word [vb_tcol]
    jmp .ch
.out:
    pop bp
    ret

; vb_tground - AX (the glyph's mask, a byte each) as the buffer's bytes
; under row DX, column [vb_tcol], with the ink where the mask is
vb_tground:
    push di
    push es
    push ax
    mov ax, dx
    sub al, [vb_meta+VH_BAND+1]
    mov ah, 80
    mul ah
    mov di, [vb_tcol]
    shl di, 1
    add di, ax
    mov es, [vb_bseg]
    mov bx, [es:di]                 ; the ground there
    pop ax
    mov cx, ax
    not cx
    and bx, cx                      ; the ground where no glyph is...
    mov cl, [vb_tinkp]
    mov ch, cl
    and ax, cx                      ; ...the ink where it is
    or ax, bx
    pop es
    pop di
    ret

; vb_rowaddr - DI = the address of row DX's first byte (CGA's two banks)
vb_rowaddr:
    mov di, dx
    shr di, 1
    mov ax, 80
    push dx
    mul di
    pop dx
    mov di, ax
    test dl, 1
    jz .e
    add di, 0x2000
.e:
    ret
