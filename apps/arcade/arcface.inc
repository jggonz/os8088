; =============================================================================
; os8088 - apps/arcade/arcface.inc - THE FRONTEND: one generic cabinet
; (ARCADE-SPEC 11). Included by arcade.asm.
;
; The window is a game browser: a marquee of bulbs, the list of every game
; with its state, the selected game's screen (a picture its own ROM drew,
; 11.4) and its card, and a coin door of buttons. Opening is a LOOK, not a
; load (11.1): each game's .ROM is probed for where a pick would look, and
; read only when the game is played.
;
; Everything is a FIELD drawn in one pass (11.3): a band composed in af_band
; and put down in one OSAPI_GFX_BLIT1, or a fill of ground no field covers.
; The content origin is 8-aligned and every band sits on the byte grid.
; =============================================================================

AF_BANDSZ   equ 3200                ; the widest band: the glass, 17 x 176
AF_PICW     equ 120                 ; a game's picture (11.4)
AF_PICH     equ 144
AF_PICS     equ 15                  ;   its stride
AF_PICSZ    equ 4 + AF_PICS * AF_PICH   ; 2,164 bytes with its header
AF_PICPAR   equ (AF_PICSZ + 15) / 16    ;   a slot, in paragraphs
AF_MAXG     equ 8                   ; games the list holds

; the states (11.1)
AFS_READY   equ 0
AFS_NOROM   equ 1
AFS_NOVGA   equ 2

; the roles: an index into a theme (af_tcol, af_tmono)
R_MARQ      equ 0                   ; the marquee's ground
R_BULB      equ 1                   ;   its lit bulbs
R_TRIM      equ 2                   ;   its rules
R_BODY      equ 3                   ; the cabinet
R_TEXT      equ 4                   ; a row's title, the card
R_SELG      equ 5                   ; the selected row's ground
R_SELT      equ 6                   ;   ...and its ink
R_READY     equ 7                   ; the tags
R_NEED      equ 8
R_BEZEL     equ 9                   ; the screen's bezel, the list's frame
R_SCREEN    equ 10                  ; the glass
R_PHOS      equ 11                  ;   a picture on it
R_BTN       equ 12                  ; a button cap
R_BTNT      equ 13                  ;   its legend
R_BTNOFF    equ 14                  ;   a cap not yet wired (Settings, R4)
R_LED       equ 15                  ; the counter
AF_NROLES   equ 16

af_tcol:    db CBLUE, CYELLOW, CLRED, CBLACK, CWHITE, CYELLOW, CBLACK, CLGREEN
            db CLRED, CDGRAY, CBLACK, CLGREEN, CRED, CWHITE, CDGRAY, CLRED
af_tmono:   db CBLACK, CWHITE, CWHITE, CBLACK, CWHITE, CWHITE, CBLACK, CWHITE
            db CWHITE, CWHITE, CBLACK, CWHITE, CWHITE, CBLACK, CBLACK, CWHITE
%if ($ - af_tmono) != AF_NROLES || (af_tmono - af_tcol) != AF_NROLES
  %error "a theme is not AF_NROLES long"
%endif

; a layout (11.2): every coordinate content-relative, every x a multiple of 8
L_W         equ 0                   ; the content box
L_H         equ 2
L_MQH       equ 4                   ; the marquee's height
L_LOGO      equ 6                   ;   its picture, x, y
L_LOGOX     equ 8
L_LOGOY     equ 10
L_LX        equ 12                  ; the list: x, y of row 0, row height,
L_LY        equ 14
L_LH        equ 16
L_LAW       equ 18                  ;   band A's width (title, maker), band B's
L_LBW       equ 20                  ;   (the tag), both in pixels
L_LTY       equ 22                  ;   the title's row in band A, the maker's
L_LMY       equ 24                  ;   (0FFh: none)
L_SX        equ 26                  ; the screen: the glass (0 = none)
L_SY        equ 28
L_SW        equ 30
L_SH        equ 32
L_CX        equ 34                  ; the card
L_CY        equ 36
L_CW        equ 38
L_CH        equ 40
L_CLH       equ 42                  ;   its line pitch
L_BY        equ 44                  ; the coin door: its buttons' y and height
L_BH        equ 46
L_SIZE      equ 48

af_lfull:
    dw 592, 280, 40, arcart_logo, 224, 7
    dw 8, 48, 24, 184, 88, 4, 14
    dw 312, 56, 136, 176
    dw 464, 48, 120, 192, 11
    dw 252, 24
af_lcomp:
    dw 592, 124, 12, arcart_slogo, 264, 3
    dw 8, 16, 10, 240, 96, 1, 0xFF
    dw 0, 0, 0, 0
    dw 360, 16, 224, 80, 9
    dw 102, 18
%if ($ - af_lcomp) != L_SIZE
  %error "a layout is not L_SIZE long"
%endif

; the coin door: x and width of each cap (the same in both layouts), the
; legend; Settings is R4's and drawn off until then
AF_NBTN     equ 3
af_btnx:    dw 8, 136, 280
af_btnw:    dw 112, 128, 128
af_btns:    dw af_s_bplay, af_s_badd, af_s_bset
af_s_bplay: db 'PLAY', 0
af_s_badd:  db 'ADD ROMS', 0
af_s_bset:  db 'SETTINGS', 0
AF_LEDX     equ 456                 ; the counter: x and width
AF_LEDW     equ 128

; each game's card (11.5's), by its place in ar_boards: a maker line for the
; list, then the card's lines, each NUL-ended - an empty one is a blank
; line - and a 1 to end the card. Small caps: 21 to a FULL line
af_cards:   dw af_c_inv, af_c_pt2, af_c_lr, af_c_gf, af_c_sw, af_c_qb
%ifdef ARC_PACMAN
            dw af_c_pm
%endif
af_c_inv:   db 'TAITO 1978', 0
            db '1 OR 2 PLAYERS', 0, 0
            db 'LEFT RIGHT  MOVE', 0, 'SPACE       FIRE', 0
            db '5 OR C      COIN', 0, '1 2         START', 0, 1
af_c_pt2:   db 'TAITO 1979', 0
            db '1 OR 2 PLAYERS', 0, 0
            db 'LEFT RIGHT  MOVE', 0, 'SPACE       FIRE', 0
            db '5 OR C      COIN', 0, '1 2         START', 0, 1
af_c_lr:    db 'TAITO 1979', 0
            db '1 OR 2 PLAYERS', 0, 0
            db 'LEFT RIGHT  MOVE', 0, 'SPACE     THRUST', 0
            db '5 OR C      COIN', 0, '1 2         START', 0, 1
af_c_gf:    db 'MIDWAY 1975', 0
            db '2 PLAYERS', 0, 0
            db 'LEFT: WASD WALK', 0, '  R F AIM  G FIRE', 0
            db 'RIGHT: ARROWS', 0, '  PGUP PGDN SPACE', 0
            db '5 OR C COIN 1 START', 0, 1
af_c_sw:    db 'MIDWAY 1976', 0
            db '1 PLAYER', 0, 0
            db 'LEFT RIGHT  SCOPE', 0, 'SPACE      TORPEDO', 0
            db '5 OR C      COIN', 0, '1           START', 0, 1
af_c_qb:    db 'GOTTLIEB 1982', 0
            db '1 OR 2 PLAYERS', 0, 0
            db 'KEYPAD 9 3 1 7', 0, '  OR THE ARROWS', 0
            db '5 OR C      COIN', 0, '1 2         START', 0, 1
%ifdef ARC_PACMAN
af_c_pm:    db 'MIDWAY 1980', 0
            db '1 OR 2 PLAYERS', 0, 0
            db 'ARROWS      MOVE', 0
            db '5 OR C      COIN', 0, '1 2         START', 0, 1
%endif

; the boards that need VGA's 640x480 (11.1), by their place in ar_boards
af_vga:     db 0, 0, 0, 0, 0, 1, 1, 1

af_s_ready: db 'READY', 0
af_s_norom: db 'ROM NEEDED', 0
af_s_novga: db 'NEEDS VGA', 0
af_tags:    dw af_s_ready, af_s_norom, af_s_novga
af_s_play:  db 'PRESS ENTER TO PLAY', 0
af_s_add:   db 'ADD ITS ROM SET', 0
af_s_vga:   db 'NEEDS A VGA DISPLAY', 0
af_hints:   dw af_s_play, af_s_add, af_s_vga
af_s_cnt:   db 'READY '
af_s_cntn:  db '0 OF '
af_s_cntt:  db '0', 0
TOASTSTR af_s_tnorom, 'Arcade: add its ROM set'
TOASTSTR af_s_tnovga, 'Arcade: it needs VGA'
af_s_pic:   db 'XXXXXXXX.PIC', 0, 0, 0

; =============================================================================
; af_init - at the entry, before the window: the theme and the layout from
; the display, the window's size and place, the pictures' claim
; =============================================================================
af_init:
    call OSAPI_VIDEO
    push cx                         ; (the dock's first row, for the y)
    mov si, af_tmono
    cmp dh, 1
    jbe .mono
    mov si, af_tcol
.mono:
    mov [af_theme], si
    mov si, af_lfull
    cmp bx, 300
    jae .lay
    mov si, af_lcomp
.lay:
    mov [af_lay], si
    mov cx, [si+L_W]                ; the frame: content + 2 wide, + the
    add cx, 2                       ;   title tall, centred in the desktop
    mov [ar_tpl+4], cx
    sub ax, cx
    sar ax, 1
    and ax, 0xFFF8
    mov [ar_tpl], ax
    pop cx
    mov ax, [si+L_H]
    add ax, TITLE_H + 1
    mov [ar_tpl+6], ax
    sub cx, ax                      ; y: centred above the dock, below the
    sar cx, 1                       ;   menu bar
    cmp cx, 14
    jge .yok
    mov cx, 14
.yok:
    mov [ar_tpl+2], cx
    mov word [af_pclaim], 0
%ifndef AF_NOPCLAIM
    mov ax, (AF_PICPAR * AF_MAXG * 16 + 1023) / 1024
    MEMKB_CLAIM
    jc .nopic
    mov [af_pclaim], dx
.nopic:
%endif
    mov byte [af_sel], 0
    ret

; af_pcmove / af_pcpin - the pictures' claim MOVABLE (SPEC.md 66), or pinned
; again across a file call that reads into it or writes out of it (66.3 rule
; 5's ordering: af_look's reads, af_snapsave's write). af_slot derives a
; slot at each use, so the word is the proc's whole job. Kept: all
af_pcmove:
    push ax
    mov ax, af_pcmv
    jmp short af_pcdecl
af_pcpin:
    push ax
    xor ax, ax
af_pcdecl:
    push dx
    mov dx, [af_pclaim]
    or dx, dx
    jz .r
    call OSAPI_MEM_MOVABLE
.r:
    pop dx
    pop ax
    ret
af_pcmv:                            ; BX = was, DX = now
    mov [af_pclaim], dx
    ret

; af_quit - the pictures' claim given back
af_quit:
    xor dx, dx
    xchg dx, [af_pclaim]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
.r:
    ret

; =============================================================================
; af_look - each game's state (11.1) and its picture: the .ROM probed where
; a pick would look, the .PIC beside it read. No game is read. Kept: none
; =============================================================================
af_look:
    call af_pcpin                   ; (read into: pinned until the end)
    mov byte [af_n], 0
    mov byte [af_nready], 0
    mov si, ar_boards
    xor di, di
.g:
    lodsw
    or ax, ax
    jz .done
    cmp di, AF_MAXG
    jae .done
    push si
    push di
    mov [ar_lbd], ax
    mov byte [af_pics+di], 0
    mov byte [af_state+di], AFS_NOROM
    call af_where                   ; CF=0: found, standing there
    jc .next
    mov byte [af_state+di], AFS_READY
    inc byte [af_nready]
    call af_picread
    call ar_gohome
.next:
    pop di
    pop si
    inc di
    inc byte [af_n]
    jmp .g
.done:
    call af_pcmove                  ; (the pictures read: movable again)
    cmp word [ar_win], 0            ; (at the entry: af_vgacheck, after the
    je .r                           ;   window)
    call af_vgacheck
.r:
    ret

; af_vgacheck - the games this display cannot run marked NEEDS VGA (11.1),
; from the window's FSX modes; and the selection put on a READY game
af_vgacheck:
    mov bx, [ar_win]
    mov ch, 0               ; CH = 0: BX's window's display (R12)
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_VGA12
    jnz .pick
    xor bx, bx
.v:
    cmp bl, [af_n]
    jae .pick
    cmp byte [af_vga+bx], 0
    je .vn
    cmp byte [af_state+bx], AFS_READY
    jne .vs
    dec byte [af_nready]
.vs:
    mov byte [af_state+bx], AFS_NOVGA
.vn:
    inc bx
    jmp .v
.pick:
    mov al, [af_sel]                ; the selection: on a READY game if it
    cmp al, [af_n]                  ;   is not on one, and inside the list
    jb .in
    mov byte [af_sel], 0
.in:
    mov bl, [af_sel]
    xor bh, bh
    cmp byte [af_state+bx], AFS_READY
    je .r
    xor bx, bx
.f:
    cmp bl, [af_n]
    jae .r
    cmp byte [af_state+bx], AFS_READY
    je .sel
    inc bx
    jmp .f
.sel:
    mov [af_sel], bl
.r:
    ret

; af_where - [ar_lbd]'s .ROM: CF=0 it is in the folder now stood in (ARCADE's
; own, or a place of ARCADE.DAT's - GOTO back with ar_gohome); CF=1 nowhere
af_where:
    call af_probe
    jnc .r
    mov si, ar_places + 1
    mov cl, [ar_places]
    xor ch, ch
.p:
    jcxz .no
    push cx
    push si
    mov bl, [si]
    mov dx, [si+1]
    call OSAPI_FILE_GOTO_QM
    jc .n
    call af_probe
    jnc .yes
.n:
    pop si
    pop cx
    add si, 3
    loop .p
.no:
    call ar_gohome
    stc
.r:
    ret
.yes:
    pop si
    pop cx
    clc
    ret

; af_probe - is [ar_lbd]'s .ROM here? A read of no bytes: FERR_BIG is a
; file with something in it. CF=0 yes
af_probe:
    mov si, [ar_lbd]
    mov si, [si+BD_ROMF]
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ
    jnc .no                         ; (empty)
    cmp ax, FERR_BIG
    jne .no
    clc
    ret
.no:
    stc
    ret

; af_picname - [ar_lbd]'s <SET>.PIC, in af_s_pic. Out SI = it
af_picname:
    push di
    push ds
    pop es
    mov si, [ar_lbd]
    mov si, [si+BD_ROMF]
    mov di, af_s_pic
.c:
    lodsb
    stosb
    cmp al, '.'
    jne .c
    mov ax, 'PI'
    stosw
    mov ax, 'C'
    stosw
    mov si, af_s_pic
    pop di
    ret

; af_picread - DI = the game: its .PIC, here, into its slot, if it is one
af_picread:
    cmp word [af_pclaim], 0
    je .r
    push di
    call af_picname
    call af_slot                    ; ES = the slot
    xor bx, bx
    mov cx, AF_PICSZ
    xor dx, dx
    call OSAPI_FILE_READ
    pop di
    jc .r
    cmp ax, AF_PICSZ
    jne .r
    cmp word [es:0], 'AP'
    jne .r
    cmp word [es:2], AF_PICW | (AF_PICH << 8)
    jne .r
    mov byte [af_pics+di], 1
.r:
    ret

; af_slot - DI = a game: ES = its picture's slot. Kept: all else
af_slot:
    push ax
    push dx
    mov ax, di
    mov dx, AF_PICPAR
    mul dx
    mov dx, [af_pclaim]             ; (a SUB a slot on kern_mega: ar_sub)
    call ar_sub
    mov es, dx
    pop dx
    pop ax
    ret

; =============================================================================
; drawing: the origin, roles, bands
; =============================================================================

; af_origin - the content origin into af_ox/af_oy (every paint and hit test,
; a moved window does not paint). SI = the window. Kept: all but flags
af_origin:
    push ax
    push bx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [af_ox], ax
    mov [af_oy], dx
    pop dx
    pop bx
    pop ax
    ret

; af_role - AL = a role: AL = its colour in the theme. Kept: all else
af_role:
    push bx
    mov bx, [af_theme]
    xlatb
    pop bx
    ret

; af_fill - AL = a role, AX..: [af_fx1],[af_fy1] to [af_fx2],[af_fy2]
; (content-relative, inclusive). Kept: all
af_fill:
    push ax
    push bx
    push cx
    push dx
    call af_role
    call OSAPI_SET_COLOR
    mov ax, [af_fx1]
    add ax, [af_ox]
    mov bx, [af_fy1]
    add bx, [af_oy]
    mov cx, [af_fx2]
    add cx, [af_ox]
    mov dx, [af_fy2]
    add dx, [af_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; af_rect - AL = role, BX = x, CX = y, DX = width, BP = height: af_fill
af_rect:
    push bx
    push cx
    mov [af_fx1], bx
    mov [af_fy1], cx
    add bx, dx
    dec bx
    mov [af_fx2], bx
    add cx, bp
    dec cx
    mov [af_fy2], cx
    call af_fill
    pop cx
    pop bx
    ret

; af_bnew - a clear band: CX = its width in BYTES, DX = its rows. Kept: all
af_bnew:
    push ax
    push cx
    push di
    mov [af_bst], cx
    mov [af_bh], dx
    mov ax, cx
    mul dx                          ; (DX: rows, under 256 - so DX = 0 after)
    mov cx, ax
    mov dx, [af_bh]
    push ds
    pop es
    mov di, af_band
    xor al, al
    cld
    rep stosb
    pop di
    pop cx
    pop ax
    ret

; af_brow - DX = a band row: DI = its first byte. Kept: all else
af_brow:
    push ax
    push dx
    mov ax, [af_bst]
    mul dx
    add ax, af_band
    mov di, ax
    pop dx
    pop ax
    ret

; af_btext - SI = a NUL string, BX = x in the band (a multiple of 8), DX =
; the row: the system font's 8x8 glyphs set in, clipped at the band's edge.
; Kept: DX
af_btext:
    push dx
    push bx
    push si
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL..AH its range
    mov [af_gseg], dx
    mov [af_goff], si
    mov [af_grng], ax
    pop si
    pop bx
    pop dx
    push dx
    mov cl, 3
    shr bx, cl                      ; BX = the byte column
.ch:
    cmp bx, [af_bst]
    jae .out
    lodsb
    or al, al
    jz .out
    cmp al, [af_grng]
    jb .sp
    cmp al, [af_grng+1]
    ja .sp
    sub al, [af_grng]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [af_goff]
    push si
    push dx
    mov si, ax
    call af_brow
    add di, bx
    mov es, [af_gseg]
    mov cx, 8
.r:
    mov al, [es:si]
    or [di], al
    inc si
    add di, [af_bst]
    loop .r
    pop dx
    pop si
.sp:
    inc bx
    jmp .ch
.out:
    pop dx
    ret

; af_bcaps - SI = a NUL string, BX = x in the band (any), DX = the row: the
; small caps (5 x 7 on a 6-pixel pitch) ORed in, lower case folded up,
; clipped at the band's edge. Out BX = the x after the last. Kept: DX
af_bcaps:
    push dx
    mov [af_cy], dx
.ch:
    lodsb
    or al, al
    jz .out
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 0x20
.up:
    sub al, ARCART_CAPS_FIRST
    jb .next
    cmp al, ARCART_CAPS_LAST - ARCART_CAPS_FIRST
    ja .next
    push bx
    mov bl, al
    xor bh, bh
    mov al, [arcart_caps_ix+bx]
    pop bx
    cmp al, 0xFF
    je .next
    mov ah, 7
    mul ah
    add ax, arcart_caps
    mov [af_cg], ax
    mov ax, bx                      ; the band's byte, and the shift into it
    mov cl, 3
    shr ax, cl
    cmp ax, [af_bst]
    jae .next
    mov dx, [af_cy]
    call af_brow
    add di, ax
    inc ax                          ; CH = FFh: the byte after is the band's too
    cmp ax, [af_bst]
    sbb ch, ch
    mov cl, bl
    and cl, 7
    push si
    mov si, [af_cg]
    mov dh, 7
.r:
    mov ah, [si]                    ; bits 7..3, moved right CL across AH:AL
    xor al, al
    shr ax, cl
    or [di], ah
    or ch, ch
    jz .s2
    or [di+1], al
.s2:
    inc si
    add di, [af_bst]
    dec dh
    jnz .r
    pop si
.next:
    add bx, 6
    jmp .ch
.out:
    pop dx
    ret

; af_bpic - SI = a picture (arcart's: dw w, db h, stride, rows), BX = x in
; the band (a multiple of 8), DX = the row: copied in. Kept: DX
af_bpic:
    push dx
    mov cl, 3
    shr bx, cl
    mov al, [si+2]                  ; rows
    xor ah, ah
    mov [af_pr], ax
    mov al, [si+3]                  ; stride
    mov [af_ps], ax
    add si, 4
    call af_brow
    add di, bx
.row:
    push di
    mov cx, [af_ps]
.b:
    lodsb
    or [di], al
    inc di
    loop .b
    pop di
    add di, [af_bst]
    dec word [af_pr]
    jnz .row
    pop dx
    ret

; af_bout - the band put down: BX = x, CX = y (content-relative, x a multiple
; of 8), DX = width in pixels, AL = the ink's role, AH = the paper's. On a
; 1bpp display the pen is not read and a SET bit is lit (white): black ink
; on white is the band inverted first. Kept: none
af_bout:
    push bx
    push cx
    push dx
    call af_role
    xchg al, ah
    call af_role
    xchg al, ah                     ; AL = ink, AH = paper, colours
    mov [af_pen], ax
    call OSAPI_VIDEO
    cmp dh, 1
    ja .colour
    cmp byte [af_pen], CBLACK       ; black ink: the band inverted
    jne .colour
    mov ax, [af_bst]
    mul word [af_bh]
    mov cx, ax
    mov di, af_band
.inv:
    not byte [di]
    inc di
    loop .inv
.colour:
    mov ax, [af_pen]
    call OSAPI_GFX_BLIT1_PEN
    pop cx                          ; (the width)
    pop bx                          ; (the y)
    pop ax                          ; (the x)
    add ax, [af_ox]
    add bx, [af_oy]
    push ds
    pop es
    mov si, af_band
    mov bp, [af_bst]
    mov dx, [af_bh]
    call OSAPI_GFX_BLIT1
    ret

; =============================================================================
; af_paint - the whole face (W_PAINT's, ar_paint's first half). SI = window
; =============================================================================
af_paint:
    call af_origin
    mov di, [af_lay]
    mov word [af_fx1], 0            ; the marquee, its rules, its bulbs
    mov word [af_fy1], 0
    mov ax, [di+L_W]
    dec ax
    mov [af_fx2], ax
    mov ax, [di+L_MQH]
    dec ax
    mov [af_fy2], ax
    mov al, R_MARQ
    call af_fill
    mov ax, [di+L_MQH]              ; a rule along its foot
    sub ax, 2
    mov [af_fy1], ax
    inc ax
    mov [af_fy2], ax
    mov al, R_TRIM
    call af_fill
    mov si, [di+L_LOGO]             ; the bulbs: one band
    mov cx, [si]
    add cx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov dl, [si+2]
    xor dh, dh
    call af_bnew
    xor bx, bx
    xor dx, dx
    call af_bpic
    mov di, [af_lay]
    mov si, [di+L_LOGO]
    mov bx, [di+L_LOGOX]
    mov cx, [di+L_LOGOY]
    mov dx, [si]
    mov ax, (R_MARQ << 8) | R_BULB
    call af_bout
    mov di, [af_lay]                ; the cabinet below it
    mov ax, [di+L_MQH]
    mov [af_fy1], ax
    mov word [af_fx1], 0
    mov ax, [di+L_W]
    dec ax
    mov [af_fx2], ax
    mov ax, [di+L_H]
    dec ax
    mov [af_fy2], ax
    mov al, R_BODY
    call af_fill
    cmp word [di+L_SW], 0           ; the screen's bezel
    je .nobez
    mov bx, [di+L_SX]
    sub bx, 8
    mov cx, [di+L_SY]
    sub cx, 8
    mov dx, [di+L_SW]
    add dx, 16
    mov bp, [di+L_SH]
    add bp, 16
    mov al, R_BEZEL
    call af_rect
.nobez:
    call af_rows                    ; every row, the screen, the card, the
                                    ;   door
    call af_screen
    call af_card
    call af_door
    ret

; af_rows - every row of the list
af_rows:
    xor cx, cx
.r:
    cmp cl, [af_n]
    jae .x
    push cx
    call af_row
    pop cx
    inc cx
    jmp .r
.x:
    ret

; af_row - CL = a game: its row, two bands (title and maker; the tag)
af_row:
    xor ch, ch
    mov [af_ri], cx
    mov di, [af_lay]
    mov cx, [di+L_LAW]              ; band A
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov dx, [di+L_LH]
    call af_bnew
    mov bx, [af_ri]                 ; the title
    shl bx, 1
    mov si, [ar_boards+bx]
    mov si, [si+BD_TITLE]
    mov bx, 8
    mov di, [af_lay]
    mov dx, [di+L_LTY]
    call af_btext
    mov di, [af_lay]
    mov dx, [di+L_LMY]
    cmp dx, 0xFF
    je .nomk
    mov bx, [af_ri]                 ; the maker and year
    shl bx, 1
    mov si, [af_cards+bx]
    mov bx, 8
    call af_bcaps
.nomk:
    call af_rowpen
    mov di, [af_lay]
    mov bx, [di+L_LX]
    mov cx, [af_ri]
    mov ax, [di+L_LH]
    mul cx
    add ax, [di+L_LY]
    mov cx, ax
    mov dx, [di+L_LAW]
    mov ax, [af_rpen]
    call af_bout
    mov di, [af_lay]                ; band B: the tag
    mov cx, [di+L_LBW]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov dx, [di+L_LH]
    call af_bnew
    mov bx, [af_ri]
    mov al, [af_state+bx]
    mov [af_rst], al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [af_tags+bx]
    push si                         ; right-aligned in its band
    mov di, [af_lay]
    mov bx, [di+L_LBW]
.tl:
    lodsb
    or al, al
    jz .tle
    sub bx, 6
    jmp .tl
.tle:
    pop si
    mov dx, [di+L_LTY]
    call af_bcaps
    call af_rowpen
    mov al, R_READY                 ; the tag's ink: by state, unless selected
    cmp byte [af_rst], AFS_READY
    je .tink
    mov al, R_NEED
.tink:
    mov bx, [af_ri]
    cmp bl, [af_sel]
    jne .tset
    mov al, R_SELT
.tset:
    mov ah, [af_rpen+1]
    push ax
    mov di, [af_lay]
    mov bx, [di+L_LX]
    add bx, [di+L_LAW]
    mov cx, [af_ri]
    mov ax, [di+L_LH]
    mul cx
    add ax, [di+L_LY]
    mov cx, ax
    mov dx, [di+L_LBW]
    pop ax
    call af_bout
    ret

; af_rowpen - [af_ri]'s row: [af_rpen] = ink, paper (roles)
af_rowpen:
    mov word [af_rpen], (R_BODY << 8) | R_TEXT
    mov ax, [af_ri]
    cmp al, [af_sel]
    jne .r
    mov word [af_rpen], (R_SELG << 8) | R_SELT
.r:
    ret

; af_screen - the selected game's picture, or its name on the glass
af_screen:
    mov di, [af_lay]
    cmp word [di+L_SW], 0
    jne .has
    ret
.has:
    mov bl, [af_sel]
    xor bh, bh
    cmp bl, [af_n]
    jae .blank
    cmp byte [af_pics+bx], 0
    jne .pic
.blank:
    mov cx, [di+L_SW]               ; no picture: the glass, the name in the
    shr cx, 1                       ;   middle as one band
    shr cx, 1
    shr cx, 1
    mov dx, [di+L_SH]
    call af_bnew
    mov bl, [af_sel]
    xor bh, bh
    cmp bl, [af_n]
    jae .bout
    shl bx, 1
    mov si, [ar_boards+bx]
    mov si, [si+BD_TITLE]
    push si                         ; centred: its length in small caps
    xor cx, cx
.len:
    lodsb
    or al, al
    jz .lend
    add cx, 6
    jmp .len
.lend:
    pop si
    mov di, [af_lay]
    mov bx, [di+L_SW]
    sub bx, cx
    sar bx, 1
    jns .xok
    xor bx, bx
.xok:
    mov dx, [di+L_SH]
    shr dx, 1
    sub dx, 4
    call af_bcaps
.bout:
    mov di, [af_lay]
    mov bx, [di+L_SX]
    mov cx, [di+L_SY]
    mov dx, [di+L_SW]
    mov ax, (R_SCREEN << 8) | R_PHOS
    call af_bout
    ret
.pic:
    mov dx, [di+L_SH]               ; the glass round the picture: above,
    sub dx, AF_PICH                 ;   below, either side
    shr dx, 1
    mov [af_py], dx
    mov bx, [di+L_SX]
    mov cx, [di+L_SY]
    mov dx, [di+L_SW]
    mov bp, [af_py]
    mov al, R_SCREEN
    call af_rect
    mov cx, [di+L_SY]
    add cx, [af_py]
    add cx, AF_PICH
    mov bp, [di+L_SH]
    sub bp, [af_py]
    sub bp, AF_PICH
    call af_rect
    mov dx, [di+L_SW]
    sub dx, AF_PICW
    shr dx, 1
    mov [af_px], dx
    mov cx, [di+L_SY]
    add cx, [af_py]
    mov bp, AF_PICH
    call af_rect                    ; (left: BX = the glass's x, DX its width)
    mov bx, [di+L_SX]
    add bx, [af_px]
    add bx, AF_PICW
    mov dx, [di+L_SW]
    sub dx, [af_px]
    sub dx, AF_PICW
    call af_rect
    mov al, R_PHOS                  ; the picture, straight from its slot
    call af_role
    mov ah, al
    mov al, R_SCREEN
    call af_role
    xchg al, ah
    call OSAPI_GFX_BLIT1_PEN
    mov bl, [af_sel]
    xor bh, bh
    mov di, bx
    call af_slot
    mov si, 4
    mov bp, AF_PICS
    mov cx, AF_PICW
    mov dx, AF_PICH
    mov di, [af_lay]
    mov ax, [di+L_SX]
    add ax, [af_px]
    add ax, [af_ox]
    mov bx, [di+L_SY]
    add bx, [af_py]
    add bx, [af_oy]
    call OSAPI_GFX_BLIT1
    ret

; af_card - the selected game's card: one band
af_card:
    mov di, [af_lay]
    mov cx, [di+L_CW]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov dx, [di+L_CH]
    call af_bnew
    mov bl, [af_sel]
    xor bh, bh
    cmp bl, [af_n]
    jae .out
    cmp byte [af_setm], 0           ; SETTINGS: the switches instead (12.3)
    je .card
    xor dx, dx
    mov si, af_s_sethd
    xor bx, bx
    call af_bcaps
    mov di, [af_lay]
    add dx, [di+L_CLH]
    add dx, [di+L_CLH]
    call af_setcard
    mov di, [af_lay]
    mov dx, [di+L_CH]
    sub dx, 8
    mov si, af_s_setkeys
    xor bx, bx
    call af_bcaps
    jmp .out
.card:
    mov al, [af_state+bx]
    mov [af_rst], al
    shl bx, 1
    mov si, [af_cards+bx]
.skip:                              ; (past the maker line: the list has it)
    lodsb
    or al, al
    jnz .skip
    xor dx, dx
.line:
    cmp byte [si], 1                ; the card's end
    je .hint
    cmp byte [si], 0                ; a blank line
    jne .text
    inc si
    jmp short .down
.text:
    xor bx, bx
    call af_bcaps
.down:
    mov di, [af_lay]
    add dx, [di+L_CLH]
    mov ax, dx                      ; (the hint's line is the card's last)
    add ax, 8
    cmp ax, [di+L_CH]
    jae .hint
    jmp .line
.hint:
    mov di, [af_lay]                ; what to do: the card's last line
    mov dx, [di+L_CH]
    sub dx, 8
    mov bl, [af_rst]
    xor bh, bh
    shl bx, 1
    mov si, [af_hints+bx]
    xor bx, bx
    call af_bcaps
.out:
    mov di, [af_lay]
    mov bx, [di+L_CX]
    mov cx, [di+L_CY]
    mov dx, [di+L_CW]
    mov ax, (R_BODY << 8) | R_TEXT
    call af_bout
    ret

; af_door - the coin door: three caps and the counter
af_door:
    xor si, si
.b:
    push si
    call af_cap
    pop si
    inc si
    cmp si, AF_NBTN
    jb .b
    mov al, [af_nready]             ; the counter: READY n OF m
    add al, '0'
    mov [af_s_cntn], al
    mov al, [af_n]
    add al, '0'
    mov [af_s_cntt], al
    mov di, [af_lay]
    mov cx, AF_LEDW / 8
    mov dx, [di+L_BH]
    call af_bnew
    mov si, af_s_cnt
    mov bx, 8
    mov dx, [di+L_BH]
    shr dx, 1
    sub dx, 4
    call af_bcaps
    mov di, [af_lay]
    mov bx, AF_LEDX
    mov cx, [di+L_BY]
    mov dx, AF_LEDW
    mov ax, (R_BODY << 8) | R_LED
    call af_bout
    ret

; af_cap - SI = a button: its cap, one band - a frame and the legend
; centred, in the cap's colours
af_cap:
    mov [af_bi], si
    shl si, 1
    mov ax, [af_btnw+si]
    mov [af_bw], ax
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov di, [af_lay]
    mov dx, [di+L_BH]
    call af_bnew
    mov si, [af_bi]                 ; the legend's length, centred
    shl si, 1
    mov si, [af_btns+si]
    push si
    xor cx, cx
.len:
    lodsb
    or al, al
    jz .lend
    add cx, 6
    jmp .len
.lend:
    pop si
    mov bx, [af_bw]
    sub bx, cx
    shr bx, 1
    mov di, [af_lay]
    mov dx, [di+L_BH]
    shr dx, 1
    sub dx, 4
    call af_bcaps
    mov si, [af_bi]
    shl si, 1
    mov bx, [af_btnx+si]
    mov di, [af_lay]
    mov cx, [di+L_BY]
    mov dx, [af_bw]
    mov ax, (R_BTN << 8) | R_BTNT
    cmp word [af_bi], 2             ; SETTINGS: unlit for a game with none
    jne .out
    push bx
    push si
    push ax
    mov bl, [af_sel]
    xor bh, bh
    call af_dsw_of
    pop ax
    pop si
    pop bx
    jnz .out
    mov ax, (R_BTNOFF << 8) | R_TEXT
.out:
    call af_bout
    ret

; =============================================================================
; af_move - AL = the game to select: the two rows, the screen and the card
; redrawn, nothing else. SI = the window
; =============================================================================
af_move:
    cmp al, [af_n]
    jae .r
    cmp al, [af_sel]
    je .r
    push ax
    call af_setleave                ; (a SETTINGS card is the game's own)
    pop ax
    mov ah, [af_sel]
    mov [af_sel], al
    push ax
    call af_origin
    pop ax
    push ax
    mov cl, ah                      ; the row it left
    call af_row
    pop ax
    mov cl, al                      ; the row it came to
    call af_row
    call af_screen
    call af_card
    mov si, 2                       ; SETTINGS: lit for a game that has them
    call af_cap
.r:
    ret

; af_key - W_ONKEY's: AL = ASCII, AH = scan. CF=0 taken
af_key:
    mov bl, [af_sel]
    cmp ah, 0x48                    ; Up
    jne .dn
    or bl, bl
    jz .took
    dec bl
    jmp short .go
.dn:
    cmp ah, 0x50                    ; Down
    jne .home
    inc bl
    jmp short .go
.home:
    cmp ah, 0x47
    jne .end
    xor bl, bl
    jmp short .go
.end:
    cmp ah, 0x4F
    jne .ins
    mov bl, [af_n]
    dec bl
    jmp short .go
.ins:
    cmp ah, 0x3C                    ; F2: SETTINGS (12.3)
    jne .ins2
    call af_setenter
    jmp short .took
.ins2:
    cmp ah, 0x52                    ; Insert: Add ROM sets...
    jne .letter
    call ar_import
    jmp short .took
.letter:
    and al, 0xDF                    ; a letter: the next title it starts
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
    mov [af_ch], al
    mov cl, [af_n]
    xor ch, ch
.t:
    inc bl
    cmp bl, [af_n]
    jb .tin
    xor bl, bl
.tin:
    push bx
    xor bh, bh
    shl bx, 1
    mov bx, [ar_boards+bx]
    mov bx, [bx+BD_TITLE]
    mov al, [bx]
    and al, 0xDF
    pop bx
    cmp al, [af_ch]
    je .go
    loop .t
    jmp short .took
.go:
    mov al, bl
    call af_move
.took:
    clc
    ret
.no:
    stc
    ret

; af_click - W_ONCLICK's: CX, DX = the screen point. A row selects (the
; selected row plays), a cap does its thing. CF=0 taken
af_click:
    call af_origin
    sub cx, [af_ox]
    sub dx, [af_oy]
    mov di, [af_lay]
    cmp dx, [di+L_BY]               ; the coin door?
    jb .list
    mov ax, [di+L_BY]
    add ax, [di+L_BH]
    cmp dx, ax
    jae .no
    xor bx, bx
.cap:
    cmp cx, [af_btnx+bx]
    jb .ncap
    mov ax, [af_btnx+bx]
    add ax, [af_btnw+bx]
    cmp cx, ax
    jae .ncap
    or bx, bx
    jnz .add
    call af_play
    jmp short .took
.add:
    cmp bx, 4                       ; (word index: Settings is the third)
    jne .addr
    cmp byte [af_setm], 0           ; SETTINGS: in, or out again
    je .setin
    call af_setleave
    jmp short .took
.setin:
    call af_setenter
    jmp short .took
.addr:
    call ar_import
    jmp short .took
.ncap:
    inc bx
    inc bx
    cmp bx, AF_NBTN * 2
    jb .cap
    jmp short .no
.list:
    mov ax, dx                      ; a row?
    sub ax, [di+L_LY]
    jb .no
    cmp cx, [di+L_LX]
    jb .no
    mov bx, [di+L_LX]
    add bx, [di+L_LAW]
    add bx, [di+L_LBW]
    cmp cx, bx
    jae .no
    xor dx, dx
    div word [di+L_LH]
    cmp al, [af_n]
    jae .no
    cmp al, [af_sel]
    je .play
    call af_move
    jmp short .took
.play:
    call af_play
.took:
    clc
    ret
.no:
    stc
    ret

; af_play - the selected game: played if READY (loaded first, if another is
; in memory), or its reason toasted
af_play:
    mov bl, [af_sel]
    xor bh, bh
    cmp bl, [af_n]
    jae .r
    mov al, [af_state+bx]
    mov si, af_s_tnorom
    cmp al, AFS_NOROM
    je .say
    mov si, af_s_tnovga
    cmp al, AFS_NOVGA
    je .say
    shl bx, 1
    mov bx, [ar_boards+bx]
    cmp bx, [ar_bd]
    je .loaded
    call ar_select
    jc .r                           ; (its reason toasted)
.loaded:
    jmp ar_play
.say:
    jmp ar_toast
.r:
    ret

; af_selbd - BX = a board: the list's selection put on it (no drawing)
af_selbd:
    push si
    mov si, ar_boards
    xor ax, ax
.f:
    cmp [si], bx
    je .hit
    cmp word [si], 0
    je .r
    inc si
    inc si
    inc ax
    jmp .f
.hit:
    mov [af_sel], al
.r:
    pop si
    ret

; =============================================================================
; af_snap - in the bracket, as a game is left (ARCADE-SPEC 11.4): if it has
; no picture, the screen it was playing on is sampled into its slot - the
; lit columns found, then 120 x 144 points across them and the screen's
; height, any lit plane counting as lit - and [af_snapw] set for
; af_snapsave to write it on the desktop
; =============================================================================
af_snap:
    cmp word [af_pclaim], 0
    je .r
    mov bx, [ar_bd]                 ; the game's index
    mov si, ar_boards
    xor di, di
.f:
    mov ax, [si]
    or ax, ax
    jz .r
    cmp ax, bx
    je .got
    inc si
    inc si
    inc di
    jmp .f
.got:
    cmp di, AF_MAXG
    jae .r
    cmp byte [af_pics+di], 0
    jne .r
    mov [af_sgame], di
    cmp byte [ar_fsi+FSI_BPP], 4    ; a planar mode: READ MODE 0, whatever the
    jne .rm                         ;   game's drawing left (its latch copies
    mov dx, 0x3CE                   ;   compare colours) - the bracket's exit
    mov ax, 0x0005                  ;   sets the mode again anyway
    out dx, ax
    call af_blacks
.rm:
    mov cx, [ar_fsi+FSI_STRIDE]     ; a row, at most 96 bytes
    cmp cx, 96
    ja .r
    push ds                         ; the lit columns: every 4th row ORed
    pop es
    mov di, af_xs                   ;   (af_xs as the accumulator for now)
    xor al, al
    cld
    rep stosb
    xor ax, ax
.acc:
    push ax
    call af_srow
    call af_litrow
    mov cx, [ar_fsi+FSI_STRIDE]
    xor bx, bx
.ab:
    mov al, [af_acc+bx]
    or [af_xs+bx], al
    inc bx
    loop .ab
    pop ax
    add ax, 16                      ; (every 16th row: the columns, not the
    cmp ax, [ar_fsi+FSI_H]          ;   picture - and an 8088's seconds)
    jb .acc
    mov cx, [ar_fsi+FSI_STRIDE]     ; the first and last lit byte
    xor bx, bx
.lo:
    cmp byte [af_xs+bx], 0
    jne .lof
    inc bx
    loop .lo
    jmp .r                          ; (a black screen: no picture)
.lof:
    mov [af_x0], bx
    mov bx, [ar_fsi+FSI_STRIDE]
.hi:
    dec bx
    cmp byte [af_xs+bx], 0
    je .hi
    sub bx, [af_x0]
    inc bx
    mov cl, 3
    shl bx, cl
    mov [af_xn], bx                 ; the span, in pixels
    shl word [af_x0], cl
    xor cx, cx                      ; the columns: x0 + i * span / 120
    xor di, di
.xs:
    mov ax, cx
    mul word [af_xn]
    mov bx, AF_PICW
    div bx
    add ax, [af_x0]
    push cx                         ; as the byte it is in and its bit
    mov cl, al
    and cl, 7
    mov bh, 0x80
    shr bh, cl
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov ah, bh
    pop cx
    mov [af_xs+di], ax
    inc di
    inc di
    inc cx
    cmp cx, AF_PICW
    jb .xs
    mov di, [af_sgame]              ; the picture, row by row
    call af_slot
    mov word [es:0], 'AP'
    mov word [es:2], AF_PICW | (AF_PICH << 8)
    mov word [af_pr], 0
.row:
    mov ax, [af_pr]                 ; y = j * H / 144
    mul word [ar_fsi+FSI_H]
    mov bx, AF_PICH
    div bx
    push es
    call af_srow
    pop es
    mov ax, [af_pr]
    mov bx, AF_PICS
    mul bx
    add ax, 4
    mov di, ax                      ; ES:DI = the picture's row
    xor si, si                      ; SI = the column's index * 2
    mov cx, AF_PICS
.byte:
    push cx
    xor dl, dl
    mov dh, 0x80
.bit:
    mov bx, [af_xs+si]              ; BL = the byte, BH = the bit
    mov al, bh
    xor bh, bh
    cmp byte [ar_fsi+FSI_BPP], 4
    je .idx
    test [af_acc+bx], al            ; mono: the bit itself
    jz .dark
    jmp short .on
.idx:
    xor ah, ah                      ; the colour number, plane by plane
    test [af_pl+288+bx], al
    jz .i3
    or ah, 8
.i3:
    test [af_pl+192+bx], al
    jz .i2
    or ah, 4
.i2:
    test [af_pl+96+bx], al
    jz .i1
    or ah, 2
.i1:
    test [af_pl+bx], al
    jz .i0
    or ah, 1
.i0:
    mov bl, ah
    cmp byte [af_lit+bx], 0
    je .dark
.on:
    or dl, dh
.dark:
    inc si
    inc si
    shr dh, 1
    jnz .bit
    mov [es:di], dl
    inc di
    pop cx
    loop .byte
    inc word [af_pr]
    cmp word [af_pr], AF_PICH
    jb .row
    mov byte [af_snapw], 1
.r:
    ret

; af_blacks - which of the 16 colour numbers are BLACK on the glass: each
; to its DAC register by the standard Attribute Controller map, and that
; register read. A game's overlay or a plane it keeps for itself is a colour number
; that shows as black, and must not count as lit. Out af_blk: the black
; ones, af_nblk of them
af_blacks:
    mov word [af_nblk], 0
    xor bx, bx
.i:
    mov al, [ar_acmap+bx]           ; the DAC register it shows: the standard
    mov dx, 0x3C7                   ;   Attribute Controller map every board
    out dx, al                      ;   here keeps (ar_acmap)
    mov dx, 0x3C9
    in al, dx
    mov ah, al
    in al, dx
    or ah, al
    in al, dx
    or ah, al
    mov byte [af_lit+bx], 1
    test ah, 0x3F
    jnz .lit
    mov byte [af_lit+bx], 0
    mov di, [af_nblk]
    mov [af_blk+di], bl
    inc word [af_nblk]
.lit:
    inc bx
    cmp bx, 16
    jb .i
    ret

; af_srow - AX = a screen row: into af_acc one bit a pixel, LIT - on a
; 16-colour mode a colour number that is not black on the glass (af_blacks),
; on a mono one a set bit. The framebuffer as ar_fsi describes it: banks,
; stride, 4 planes or 1
af_srow:
    xor dx, dx
    mov cl, [ar_fsi+FSI_BANKS]
    xor ch, ch
    or cx, cx
    jnz .b
    inc cx
.b:
    div cx                          ; AX = the row in its bank, DX = the bank
    push dx
    mul word [ar_fsi+FSI_STRIDE]
    mov si, ax
    pop ax
    mul word [ar_fsi+FSI_BSTEP]
    add si, ax
    push ds
    pop es
    mov cx, [ar_fsi+FSI_STRIDE]
    cmp byte [ar_fsi+FSI_BPP], 4
    je .planar
    push ds                         ; mono: the row as it is
    mov ds, [ar_fsi+FSI_SEG]
    mov di, af_acc
    cld
    rep movsb
    pop ds
    ret
.planar:
    mov bl, 0                       ; each plane into af_pl + plane * 96
.p:
    mov dx, 0x3CE
    mov al, 4
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    mov al, 96
    mul bl
    add ax, af_pl
    mov di, ax
    push cx
    push si
    push ds
    mov ds, [ar_fsi+FSI_SEG]
    cld
    rep movsb
    pop ds
    pop si
    pop cx
    inc bl
    cmp bl, 4
    jb .p
    ret

; af_litrow - the row af_srow read, as LIT bytes in af_acc: on a planar mode
; no black colour number there (af_blacks); mono, as it is
af_litrow:
    cmp byte [ar_fsi+FSI_BPP], 4
    jne .r
    mov cx, [ar_fsi+FSI_STRIDE]
    xor bx, bx
.byte:
    xor dl, dl                      ; DL = the black pixels of this byte
    xor si, si
.k:
    cmp si, [af_nblk]
    jae .kd
    mov ah, [af_blk+si]             ; colour number k: the pixels whose four
    mov dh, 0xFF                    ;   plane bits spell it
    mov al, [af_pl+bx]
    test ah, 1
    jnz .p0
    not al
.p0:
    and dh, al
    mov al, [af_pl+96+bx]
    test ah, 2
    jnz .p1
    not al
.p1:
    and dh, al
    mov al, [af_pl+192+bx]
    test ah, 4
    jnz .p2
    not al
.p2:
    and dh, al
    mov al, [af_pl+288+bx]
    test ah, 8
    jnz .p3
    not al
.p3:
    and dh, al
    or dl, dh
    inc si
    jmp .k
.kd:
    not dl
    mov [af_acc+bx], dl
    inc bx
    loop .byte
.r:
    ret

; af_snapsave - on the desktop, after the bracket: a picture af_snap took is
; the game's now, and written as <SET>.PIC where its .ROM was found. A
; refusal (a protected disk) costs nothing but the file
af_snapsave:
    cmp byte [af_snapw], 0
    je .r
    mov byte [af_snapw], 0
    mov di, [af_sgame]
    mov byte [af_pics+di], 1
    mov bx, [ar_bd]
    mov [ar_lbd], bx
    mov bl, [ar_rfdrv]
    mov dx, [ar_rfcl]
    call OSAPI_FILE_GOTO_QM
    jc .home
    call af_picname
    call af_pcpin                   ; (written out of: pinned across it)
    mov di, [af_sgame]
    call af_slot
    xor bx, bx
    mov cx, AF_PICSZ
    xor dx, dx
    call OSAPI_FILE_WRITE
    call af_pcmove
.home:
    call ar_gohome
    mov si, [ar_win]                ; (the desktop came back before it was
    call af_origin                  ;   the game's: the glass drawn again)
    call af_screen
.r:
    ret
