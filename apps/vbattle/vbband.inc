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
; THE BAND'S LAYOUT (125.5.1), [vb_lay]: vbtrules.LAYOUT's bytes, in order -
; columns in cells, rows from the band's top
LY_ECOL     equ 0
LY_EROW     equ 1
LY_ESHOW    equ 2
LY_PROW     equ 3
LY_PPITCH   equ 4
LY_NCOL     equ 5
LY_NSHOW    equ 6
LY_LCOL     equ 7
LY_LSHOW    equ 8
LY_HCOL     equ 9
LY_HSHOW    equ 10
LY_HMAX     equ 11
LY_MCOL     equ 12
LY_MSHOW    equ 13
LY_SCOL     equ 14
LY_SROW     equ 15
LY_SSHOW    equ 16
LY_LINES    equ 17
LY_DIV      equ 18
LY_SZ       equ 20

; vb_lay_init - [vb_lay] from the rules (header 56: its offset), or the
; band as it always was (vb_laydef, the message on screen row 191), and
; the party line's first and last column, [vb_plo] and [vb_phi]
vb_lay_init:
    push ds
    pop es
    mov di, vb_lay
    mov cx, LY_SZ
    mov si, [RL+RH_OLAY]
    cld
    or si, si
    jz .def
    add si, RL
    rep movsb
    jmp short .lohi
.def:
    mov si, vb_laydef
    rep movsb
    mov al, [vb_meta+VH_BAND+1]     ; (the band's first row)
    mov dl, ROW_MSG & 0xFF
    sub dl, al
    mov [vb_lay+LY_SROW], dl
.lohi:
    mov byte [vb_plo], 40
    mov byte [vb_phi], 0
    cmp byte [vb_lay+LY_NSHOW], 0
    je .l1
    mov al, [vb_lay+LY_NCOL]
    mov ah, 6
    call .upd
.l1:
    cmp byte [vb_lay+LY_LSHOW], 0
    je .l2
    mov al, [vb_lay+LY_LCOL]
    mov ah, 1
    call .upd
.l2:
    cmp byte [vb_lay+LY_HSHOW], 0
    je .l4
    mov al, [vb_lay+LY_HCOL]
    mov ah, 4
    call .upd
    cmp byte [vb_lay+LY_HMAX], 0
    je .l4
    mov al, [vb_lay+LY_HCOL]
    add al, 4
    mov ah, 7
    call .upd
.l4:
    cmp byte [vb_lay+LY_MSHOW], 0
    je .l5
    mov al, [vb_lay+LY_MCOL]
    mov ah, 4
    call .upd
.l5:
    cmp byte [vb_phi], 40
    jbe .r
    mov byte [vb_phi], 40
.r:
    ret
.upd:                               ; a field at AL, AH cells wide
    cmp al, [vb_plo]
    jae .u1
    mov [vb_plo], al
.u1:
    add al, ah
    cmp al, [vb_phi]
    jbe .u2
    mov [vb_phi], al
.u2:
    ret

vb_laydef:  db 18, 3, 1, 12, 9, 18, 1, 24, 1, 25, 1, 1, 36, 1, 1, 0, 1, 1, 16, 0

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
    call vb_bwords                  ; (the band's rows alone: 58 of them, a
    xor ax, ax                      ; quarter of the 16 KB once cleared)
    cld
    rep stosw
    mov al, [RL+RH_BANDIMG]
    cmp al, NONE
    je .plain
    call vb_blit_img
    jmp short .menu
.plain:
    cmp byte [vb_lay+LY_LINES], 0   ; THE PLAIN LOOK (its lines may be off,
    je .menu                        ; 125.5.1): rules at the band's second
    mov dx, 1                       ; row and above the line, the divider
    call .rule                      ; between
    mov dl, [vb_lay+LY_SROW]
    xor dh, dh
    sub dx, 2
    jb .menu
    call .rule
    mov dx, 3
.v:
    mov al, [vb_lay+LY_SROW]
    xor ah, ah
    sub ax, 3
    cmp dx, ax
    jge .menu
    push dx
    mov ax, 80
    mul dx
    pop dx
    mov di, ax
    mov al, [vb_lay+LY_DIV]
    xor ah, ah
    shl ax, 1
    add di, ax
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

; vb_bwords - CX = the band buffer's words: 40 a row, its rows from the
; band's first to 200
vb_bwords:
    push ax
    mov al, [vb_meta+VH_BAND+1]
    xor ah, ah
    mov cx, 200
    sub cx, ax
    mov ax, 40
    mul cx
    mov cx, ax
    pop ax
    ret

; --- THE BAND REBUILT ONLY WHEN ITS GROUND CHANGES (the owner, 2026-11-05:
; "any way to draw the menus faster?"). A rebuild - the ground composed,
; every line, the whole band put - was asked for at every turn's menu, a
; sub-menu and an action taken: ~0.75 s of composing under the hook on the
; XT, and on Hercules ~0.75 s more to put. The ground is the band's picture
; and the open menu's own; when that is what the last rebuild drew, the
; old menu's lines no new one covers are erased to the ground and the text
; is drawn as a partial redraw is - on VGA4 and Hercules only its cells put

; vb_gnow - AL = the ground's key now: the open menu's picture (a menu or
; a target being chosen), NONE none
vb_gnow:
    mov al, NONE
    cmp byte [vb_state], GS_MENU
    je .m
    cmp byte [vb_state], GS_TARGET
    jne .r
.m:
    push si
    mov al, [vb_menu]
    call vb_menurec
    mov al, [si+MN_IMG]
    pop si
.r:
    ret

; vb_needrb - [vb_dirty] has 8: CF=1 the band must be rebuilt (its ground
; changes, or it never was); CF=0 it need not - the old menu's lines
; erased and [vb_dirty] the text's (7) instead
vb_needrb:
    cmp byte [vb_gkey], 0xFE
    je .yes
    call vb_gnow
    cmp al, [vb_gkey]
    jne .yes
    call vb_menu_erase
    mov al, [vb_dirty]
    and al, 0xF7
    or al, 7
    mov [vb_dirty], al
    clc
    ret
.yes:
    stc
    ret

; vb_itempos - item BP of menu AL: CX = its column, DX = its row in the
; band (the look's, or its own place). AX, SI spent
vb_itempos:
    mov ah, [vb_menu]
    push ax
    mov [vb_menu], al
    call vb_menurec
    mov al, [si+MN_PITCH]
    mov ah, 0
    mul bp
    add al, [si+MN_ROW]
    adc ah, 0
    mov dx, ax
    mov cl, [si+MN_COL]
    xor ch, ch
    call vb_placed
    pop ax
    mov [vb_menu], ah
    ret

; vb_menu_erase - the lines of the menu last drawn ([vb_lmenu], [vb_lmn])
; that the menu to be drawn now does not cover, to the ground: fifteen
; spaces over the buffer's ground, so no line is drawn twice
vb_menu_erase:
    cmp byte [vb_lmenu], NONE
    je .out
    push bp
    call vb_gnow                    ; (the menu to be drawn: NONE when none)
    mov al, NONE
    cmp byte [vb_state], GS_MENU
    je .sh
    cmp byte [vb_state], GS_TARGET
    jne .ns
.sh:
    mov al, [vb_menu]
.ns:
    mov [vb_qnew], al
    xor bp, bp
.i:
    mov ax, bp
    cmp al, [vb_lmn]
    jae .done
    mov al, [vb_lmenu]
    call vb_itempos
    mov [vb_qecol], cx
    mov [vb_qerow], dx
    cmp byte [vb_qnew], NONE        ; covered by a line of the new menu at
    je .erase                       ; the same place?
    mov al, [vb_qnew]
    push bp
    push ax
    call vb_menurec
    mov bl, [si+MN_N]
    pop ax
    xor bp, bp
.j:
    mov ax, bp
    cmp al, bl
    jae .nj
    push bx
    mov al, [vb_qnew]
    call vb_itempos
    pop bx
    cmp cx, [vb_qecol]
    jne .jn
    cmp dx, [vb_qerow]
    je .cov
.jn:
    inc bp
    jmp .j
.cov:
    pop bp
    jmp short .next
.nj:
    pop bp
.erase:
    VB_BY
    add ax, [vb_qerow]
    mov dx, ax
    mov cx, [vb_qecol]
    mov si, vb_s15
    mov bh, BG_TEXT + 3
    push bp
    call vb_text
    pop bp
.next:
    inc bp
    jmp .i
.done:
    mov byte [vb_lmenu], NONE
    pop bp
.out:
    ret

vb_s15:     db '               ', 0

; vb_mkeep - THE MUSIC FED while the band is drawn (the owner, 2026-11-05):
; a piece read when its ring holds under 3/4 - drawing keeps the reader
; away, and on Hercules' XT a whole band is ~2 s of it. Every register kept
vb_mkeep:
    cmp byte [vb_mres], 0
    jne .r
    push ax
    mov ax, [vb_mfill]
    sub ax, [vb_mtake]
    cmp ax, VB_MLOW
    jae .p
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov byte [vb_pbig], 1           ; (whole: it is running low, vb_reader)
    call vb_mpiece
    mov byte [vb_pbig], 0
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
.p:
    pop ax
.r:
    ret

; vb_band_put - the COMPOSED band ([vb_cseg]) onto the screen, row by row
; (CGA's two banks): every pixel of it written once
vb_band_put:
    cmp byte [vb_kind], VK_CGA4     ; VGA4, Hercules: the buffer is the
    je .cga                         ; band's logical picture, presented
    VB_BY
    mov dx, ax
    mov cx, 200
    sub cx, ax
    xor bx, bx
    mov ax, 40
    jmp vb_bpres
.cga:
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
    call vb_needrb                  ; (the ground the same: no rebuild - the
    jnc .t                          ; old menu's lines erased, text redrawn)
    ; THE BAND REBUILT is composed off the screen and put on it once, its
    ; text and all (6.1's rule for a whole band: no frame shows the ground
    ; without its letters)
    ; ...and THE MUSIC'S RING TOPPED UP FIRST: the rebuild keeps the reader
    ; away for its whole length - ~2 s on Hercules' XT, where the ring
    ; may stand at half (1.5 s) - so the music ran dry under it, or just
    ; after (the owner, 2026-11-05)
    call vb_mtopup
    mov byte [vb_dirty], 0
    call vb_band_frame              ; the ground, in [vb_bseg]
    push ds
    call vb_bwords                  ; (the band's rows alone)
    xor si, si
    xor di, di
    mov es, [vb_cseg]
    mov ds, [vb_bseg]
    cld
    rep movsw
    pop ds
    mov byte [vb_tcomp], 1          ; the text into [vb_cseg]
    mov byte [vb_lmenu], NONE       ; (no menu on this fresh ground yet)
    call vb_gnow
    mov [vb_gkey], al
    call vb_draw_menu
    call vb_draw_party
    call vb_draw_msg
    mov byte [vb_tcomp], 0
    mov byte [vb_bput], 1           ; (the band being put: a test's)
    call vb_band_put
    mov byte [vb_bput], 0
    mov ax, [vb_mfill]              ; (the ring's level as the rebuild ends,
    sub ax, [vb_mtake]              ; the lowest kept: tests/vbtsound.py)
    cmp byte [vb_mres], 0
    jne .lo
    cmp ax, [vb_mlow]
    jae .lo
    mov [vb_mlow], ax
.lo:
    ret
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
    mov [vb_lmenu], al              ; (the menu drawn, for vb_menu_erase)
    call vb_menurec
    mov [vb_mrec], si
    mov al, [si+MN_N]
    mov [vb_lmn], al
    xor bp, bp
.l:
    call vb_mkeep                   ; (the music fed between lines)
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
    cmp byte [vb_lay+LY_MSHOW], 0   ; (no MP in this battle: no cost shown)
    je .afford
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
    mov cl, [si+MN_COL]
    xor ch, ch
    call vb_placed                  ; (an item placed of its own, 125.5.1)
    VB_BY
    add dx, ax
    mov si, vb_lbuf
    push bp
    call vb_text
    pop bp
    inc bp
    jmp .l

; vb_placed - item BP of menu [vb_menu]: CX = its column, DX = its row in
; the band, from its own place when the rules give it one (header 58: how
; many, 60: where). AX, SI spent; BX kept
vb_placed:
    push bx
    mov bl, [RL+RH_NPLACE]
    or bl, bl
    jz .out
    mov si, [RL+RH_OPLACE]
    add si, RL
.s:
    mov al, [si]
    cmp al, [vb_menu]
    jne .n
    mov ax, bp
    cmp al, [si+1]
    jne .n
    mov cl, [si+2]
    xor ch, ch
    mov dl, [si+3]
    xor dh, dh
    jmp short .out
.n:
    add si, 4
    dec bl
    jnz .s
.out:
    pop bx
    ret

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
    cmp byte [vb_lay+LY_ESHOW], 0
    je .ne
    VB_BY
    add al, [vb_lay+LY_EROW]
    adc ah, 0
    mov dx, ax
    mov cl, [vb_lay+LY_ECOL]
    xor ch, ch
    mov si, vb_lbuf
    mov bh, BG_TEXT + 2             ; red
    call vb_text
.ne:
    xor bp, bp
.m:
    call vb_mkeep                   ; (the music fed between lines)
    mov ax, bp
    cmp al, [RL+RH_NCH]
    jb .c
    ret
.c:
    ; A CHARACTER'S LINE, laid out (125.5.1, vbtband.party_line): each
    ; field shown at its column, spaces between, from [vb_plo] to [vb_phi]
    mov al, [vb_phi]
    sub al, [vb_plo]
    jbe .skip                       ; (nothing of the party shown)
    push ds
    pop es
    mov di, vb_lbuf
    mov cl, al
    xor ch, ch
    mov al, ' '
    cld
    rep stosb
    mov bx, bp
    call vb_char
    mov [vb_chrec], si
    cmp byte [vb_lay+LY_NSHOW], 0
    je .f1
    mov al, [vb_lay+LY_NCOL]
    call .at
    mov cx, 6
    call vb_strncpy
    mov byte [di], ' '
.f1:
    cmp byte [vb_lay+LY_LSHOW], 0
    je .f2
    mov bx, bp
    cmp byte [vb_lim+bx], 100
    jb .f2
    mov al, [vb_lay+LY_LCOL]
    call .at
    mov byte [di], '*'
.f2:
    cmp byte [vb_lay+LY_HSHOW], 0
    je .f4
    mov al, [vb_lay+LY_HCOL]
    call .at
    mov bx, bp
    shl bx, 1
    mov ax, [vb_hp+bx]
    call vb_rjust4
    cmp byte [vb_lay+LY_HMAX], 0
    je .f4
    mov al, [vb_lay+LY_HCOL]
    add al, 4
    call .at
    mov byte [di], '/'
    inc di
    mov si, [vb_chrec]
    mov ax, [si+CH_HP]
    call vb_utoa
    mov byte [di], ' '
.f4:
    cmp byte [vb_lay+LY_MSHOW], 0
    je .f5
    mov al, [vb_lay+LY_MCOL]
    call .at
    mov bx, bp
    shl bx, 1
    mov ax, [vb_mp+bx]
    call vb_rjust4
.f5:
    mov bl, [vb_phi]
    sub bl, [vb_plo]
    xor bh, bh
    mov byte [vb_lbuf+bx], 0
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
    mov cl, [vb_lay+LY_PPITCH]
    mul cl
    add al, [vb_lay+LY_PROW]
    adc ah, 0
    mov dx, ax
    VB_BY
    add dx, ax
    pop bx
    mov cl, [vb_plo]
    xor ch, ch
    mov si, vb_lbuf
    push bp
    call vb_text
    pop bp
.skip:
    inc bp
    jmp .m
.at:                                ; DI = the line at column AL
    sub al, [vb_plo]
    xor ah, ah
    mov di, vb_lbuf
    add di, ax
    ret

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
    call vb_mkeep
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
    cmp byte [vb_lay+LY_SSHOW], 0   ; (no message line in this battle)
    jne .sh
    ret
.sh:
    VB_BY
    add al, [vb_lay+LY_SROW]
    adc ah, 0
    mov dx, ax
    mov cl, [vb_lay+LY_SCOL]
    xor ch, ch
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
    mov [vb_tcol0], cx
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
    cmp byte [vb_kind], VK_CGA4     ; (VGA4, Hercules: always the buffer,
    jne .cb                         ; then presented)
    cmp byte [vb_tcomp], 0
    je .ch
.cb:
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
    cmp byte [vb_kind], VK_CGA4
    jne .cmp
    cmp byte [vb_tcomp], 0
    je .scr
.cmp:
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
    cmp byte [vb_kind], VK_CGA4     ; drawn into the buffer: its cells onto
    je .o2                          ; the screen, unless the whole band is
    cmp byte [vb_tcomp], 0          ; about to be
    jne .o2
    mov dx, [vb_trow]
    mov cx, 8
    mov bx, [vb_tcol0]
    mov ax, [vb_tcol]
    call vb_bpres
.o2:
    pop bp
    ret

; vb_bpres - the band's buffer ([vb_cseg], 2 bits a pixel, 80 bytes a row)
; onto a VGA4 or a Hercules screen: CX rows from screen row DX (the band's
; logical rows, as on CGA), cells BX to AX (exclusive), 8 pixels each. VGA4:
; the band's colours are the palette's slots 0..3 (vbtana.battle_palette),
; so planes 0 and 1 carry them and 2 and 3 are cleared; a row's plane at a
; time with interrupts off, since the hook's decode moves the Map Mask.
; Hercules: each pixel two by two (vbtana.HERC22), the canvas's rows above
vb_bpres:
    push si
    push di
    push bp
    cmp ax, bx
    jbe .out
    mov [vb_bpc0], bx
    sub ax, bx
    mov [vb_bpcn], ax
    mov es, [vb_vseg]
.row:
    jcxz .out
    test dl, 7                      ; (every eighth row: the music fed - a
    jnz .rw                         ; whole band put is ~0.6 s on Hercules)
    call vb_mkeep
.rw:
    push cx
    push dx
    mov ax, dx                      ; the buffer's row
    sub al, [vb_meta+VH_BAND+1]
    mov ah, 80
    mul ah
    mov si, [vb_bpc0]
    shl si, 1
    add si, ax
    cmp byte [vb_kind], VK_VGA4
    jne .herc
    mov ax, 40                      ; VGA4: row DX, 40 bytes
    mul dx
    add ax, [vb_bpc0]
    mov di, ax
    mov bp, 0x0C                    ; planes 2 and 3: none of the band's
    call .vclr
    mov bp, 0x0102                  ; plane 0 (the table's low nibbles)
    call .vpl
    mov bp, 0x0202                  ; plane 1 (the high)
    call .vpl
    jmp short .next
.herc:
    mov ax, dx                      ; Hercules: rows 2(y - band) + the
    sub al, [vb_meta+VH_BAND+1]     ; canvas's, and the one under it
    xor ah, ah
    shl ax, 1
    add al, [vb_meta+9]
    adc ah, 0
    xor bp, bp
    call .hrow
    inc ax
    mov bp, 256
    call .hrow
.next:
    pop dx
    pop cx
    inc dx
    dec cx
    jmp .row
.out:
    pop bp
    pop di
    pop si
    ret
.hrow:                              ; Hercules row AX, table half BP
    push ax
    push si
    mov di, ax
    and di, 3
    mov cl, 13
    shl di, cl                      ; its bank
    shr ax, 1
    shr ax, 1
    mov cx, 90
    push dx
    mul cx
    pop dx
    add di, ax
    add di, VB_HORG
    mov ax, [vb_bpc0]
    shl ax, 1
    add di, ax
    mov cx, [vb_bpcn]
    shl cx, 1                       ; two buffer bytes a cell, a screen byte
    push ds                         ; each
    mov ds, [vb_cseg]
.hb:
    lodsb
    xor bh, bh
    mov bl, al
    add bx, bp
    mov al, [cs:vb_hptab+bx]
    stosb
    loop .hb
    pop ds
    pop si
    pop ax
    ret
.vclr:                              ; planes BP's low byte: cleared
    pushf
    cli
    push di
    mov dx, 0x3C4
    mov ax, bp
    mov ah, al
    mov al, 2
    out dx, ax
    mov cx, [vb_bpcn]
    xor al, al
    rep stosb
    pop di
    mov ax, 0x0F02
    out dx, ax
    popf
    ret
.vpl:                               ; plane BP's high byte, its nibbles out
    pushf                           ; of the table by BP's low byte's half
    cli
    push si
    push di
    mov dx, 0x3C4
    mov ax, bp
    mov al, 2
    out dx, ax
    mov cx, [vb_bpcn]
    push ds
    mov ds, [vb_cseg]
.vb:
    lodsb                           ; the cell's left four pixels...
    xor bh, bh
    mov bl, al
    mov ah, [cs:vb_vptab+bx]
    lodsb                           ; ...and its right four
    mov bl, al
    mov al, [cs:vb_vptab+bx]
    test bp, 0x0100
    jnz .lo
    and ah, 0xF0                    ; plane 1: the high nibbles
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    jmp short .c
.lo:
    shl ah, 1                       ; plane 0: the low nibbles
    shl ah, 1
    shl ah, 1
    shl ah, 1
    and al, 0x0F
.c:
    or al, ah
    stosb
    loop .vb
    pop ds
    mov ax, 0x0F02
    out dx, ax
    pop di
    pop si
    popf
    ret

; vb_bptab - the present's tables, for the bundle's adapter: VGA4's, a byte
; of four 2-bit pixels as plane 0's four bits (low nibble) and plane 1's
; (high); Hercules', the same byte as its 8 pixels on an even row and on an
; odd (vbtana.HERC22: 0 none, 1 one, 2 a checker, 3 all)
vb_bptab:
    xor bx, bx
.b:
    xor dx, dx                      ; DL: Hercules' even row, DH: its odd
    xor cx, cx                      ; CL: plane 0's bits, CH: plane 1's
    mov al, bl
    mov si, 4
.p:
    mov ah, al
    rol ah, 1
    rol ah, 1
    and ah, 3                       ; this pixel's value, 0..3
    shl al, 1
    shl al, 1
    push bx
    mov bl, ah
    xor bh, bh
    shl dl, 1
    shl dl, 1
    or dl, [vb_hve+bx]
    shl dh, 1
    shl dh, 1
    or dh, [vb_hvo+bx]
    pop bx
    shl cl, 1
    test ah, 1
    jz .a
    or cl, 1
.a:
    shl ch, 1
    test ah, 2
    jz .c
    or ch, 1
.c:
    dec si
    jnz .p
    shl ch, 1
    shl ch, 1
    shl ch, 1
    shl ch, 1
    or cl, ch
    mov [vb_vptab+bx], cl
    mov [vb_hptab+bx], dl
    mov [vb_hptab+256+bx], dh
    inc bx
    cmp bx, 256
    jb .b
    ret

vb_hve:       db 0, 2, 2, 3         ; a pixel's two on an even row...
vb_hvo:       db 0, 0, 1, 3         ; ...and on an odd

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
