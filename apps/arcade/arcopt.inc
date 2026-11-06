; =============================================================================
; os8088 - apps/arcade/arcopt.inc - SETTINGS, SCORES, PAUSE AND HELP
; (ARCADE-SPEC 12). Included by arcade.asm.
;
; - The doors: every entry into a game starts with no key held and nothing
;   typed waiting (12.1).
; - P pauses, F1 shows the game's card over its picture, both on the
;   game's own screen in the system font copied before the bracket, and the
;   board's own setup draws the picture again after (12.2).
; - The DIP switches that matter, in words, in the card when SETTINGS is
;   pressed, kept in ARCADE.DAT and set at every power-on (12.3).
; - High scores: the RAM a game keeps its best in, saved as it is left and
;   put back after its power-on, kept in ARCADE.DAT (12.4).
; =============================================================================

; -----------------------------------------------------------------------------
; 12.1 the doors
; -----------------------------------------------------------------------------

; ar_door - the bracket's door: nothing typed waits, nothing is held, the
; game is not paused
ar_door:
.drain:
    mov ah, 1
    int 0x16
    jz .clear
    xor ah, ah
    int 0x16
    jmp .drain
.clear:
    mov byte [ar_pbits], 0
    mov byte [ar_pleft], 0
    mov byte [ar_hold], 0
    ret

; -----------------------------------------------------------------------------
; 12.2 pause and help
; -----------------------------------------------------------------------------
AR_OVC      equ 24                  ; the box: characters across

; ar_ovinit - on the desktop, before the bracket: the system font's 8x8
; glyphs for 32..127 copied in (desktop calls are off-limits in the game's
; mode: CLAUDE.md, the fast-games rules)
ar_ovinit:
    push ds
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL..AH its range
    mov bl, al
    pop ds
    push ds
    pop es
    mov di, ar_glyph
    mov cx, 96 * 8
    xor al, al
    cld
    rep stosb
    cmp bl, 32                      ; (a table from 32 or before: copied from
    ja .r                           ;   32 on)
    mov al, 32
    sub al, bl
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add si, ax
    mov di, ar_glyph
    mov cx, 96 * 8
    push ds
    mov ds, dx
    rep movsb
    pop ds
.r:
    ret

; ar_holdit - in the bracket, at a frame's end with [ar_hold] set: the board
; stops (its clock and its sound), a box says so - with the game's card if
; F1 asked - and the next key goes on: Esc leaves, anything else plays on
; and the picture is drawn again by the board's own setup
ar_holdit:
    call ar_snd_close
    call ar_ovbox
.w:
    sti
    hlt
    mov ah, 1
    int 0x16
    jz .w
    xor ah, ah
    int 0x16
    cmp al, 27
    jne .go
    mov byte [ar_quit], 1
.go:
    mov byte [ar_hold], 0
    mov bx, [ar_bd]                 ; the picture again: the board's setup
    mov si, [ar_win]                ;   (the mode, its colours, every cell)
    call [bx+BD_SETUP]
    call ar_snd_open
    ret

; ar_ovbox - the box over the picture: PAUSED, or the game's card (F1),
; and how to go on. White on black in the system font, on the game's own
; framebuffer (ar_fsi: VGA's planes, Hercules' and CGA's banks)
ar_ovbox:
    mov word [ar_ovn], 0            ; the lines: into ar_ovl, pointers
    cmp byte [ar_hold], 2
    jne .paused
    mov bl, [af_sel]                ; F1: the game's card, past its maker
    xor bh, bh
    shl bx, 1
    mov si, [af_cards+bx]
.sk:
    lodsb
    or al, al
    jnz .sk
.cl:
    cmp byte [si], 1
    je .tail
    call ar_ovadd
.cn:
    lodsb
    or al, al
    jnz .cn
    jmp .cl
.paused:
    mov si, ar_s_paused
    call ar_ovadd
.tail:
    mov si, ar_s_ovnone
    call ar_ovadd
    mov si, ar_s_ovgo
    call ar_ovadd
    mov si, ar_s_ovesc
    call ar_ovadd
    mov ax, [ar_ovn]                ; the box: lines * 10 + 8 rows, AR_OVC
    mov cl, 10                      ;   + 2 columns, in the middle
    mul cl
    add ax, 8
    mov [ar_ovh], ax
    mov ax, [ar_fsi+FSI_H]
    sub ax, [ar_ovh]
    shr ax, 1
    mov [ar_ovy], ax
    mov ax, [ar_fsi+FSI_W]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    sub ax, AR_OVC + 2
    shr ax, 1
    mov [ar_ovx], ax
    cmp byte [ar_fsi+FSI_BPP], 4    ; VGA's planes: every write to all four,
    jne .rows                       ;   the byte as it is (colour 15 and 0)
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    mov dx, 0x3CE
    mov ax, 0x0005                  ; write mode 0
    out dx, ax
    mov ax, 0x0001                  ; no set/reset
    out dx, ax
    mov ax, 0x0003                  ; no rotate, no function
    out dx, ax
    mov ax, 0xFF08                  ; every bit
    out dx, ax
.rows:
    xor bp, bp                      ; BP = the box's row
.row:
    mov ax, [ar_ovy]
    add ax, bp
    call ar_ovaddr                  ; ES:DI = the row's first byte
    mov ax, bp                      ; which line, and which of its rows
    sub ax, 4
    jb .edge
    mov cl, 10
    div cl                          ; AL = the line, AH = its row
    cmp ah, 8
    jae .blank
    xor ch, ch
    mov cl, al
    cmp cx, [ar_ovn]
    jae .blank
    mov bx, cx
    shl bx, 1
    mov si, [ar_ovl+bx]             ; the line's text, a row of its glyphs
    mov dl, ah
    xor dh, dh
    mov al, 0x80                    ; the left edge
    stosb
    mov cx, AR_OVC
.ch:
    lodsb
    or al, al
    jnz .chok
    dec si                          ; (past its end: spaces)
    mov al, ' '
.chok:
    cmp al, 32
    jb .sp
    cmp al, 127
    jbe .gl
.sp:
    mov al, ' '
.gl:
    sub al, 32
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, dx
    mov bx, ax
    mov al, [ar_glyph+bx]
    stosb
    loop .ch
    mov al, 0x01                    ; the right edge
    stosb
    jmp short .next
.edge:
    cmp bp, 0                       ; the top: a rule, then blank
    jne .blank
.rule:
    mov al, 0xFF
    mov cx, AR_OVC + 2
    rep stosb
    jmp short .next
.blank:
    mov ax, [ar_ovh]
    dec ax
    cmp bp, ax                      ; the bottom: a rule
    je .rule
    mov al, 0x80
    stosb
    xor al, al
    mov cx, AR_OVC
    rep stosb
    mov al, 0x01
    stosb
.next:
    inc bp
    cmp bp, [ar_ovh]
    jb .row
    push ds
    pop es
    ret

; ar_ovadd - SI = a NUL line: another of the box's lines (at most 12)
ar_ovadd:
    mov bx, [ar_ovn]
    cmp bx, 12
    jae .r
    shl bx, 1
    mov [ar_ovl+bx], si
    inc word [ar_ovn]
.r:
    ret

; ar_ovaddr - AX = a screen row: ES:DI = its byte at the box's column
ar_ovaddr:
    xor dx, dx
    mov cl, [ar_fsi+FSI_BANKS]
    xor ch, ch
    or cx, cx
    jnz .b
    inc cx
.b:
    div cx
    push dx
    mul word [ar_fsi+FSI_STRIDE]
    mov di, ax
    pop ax
    mul word [ar_fsi+FSI_BSTEP]
    add di, ax
    add di, [ar_ovx]
    mov es, [ar_fsi+FSI_SEG]
    ret

ar_s_paused: db '        PAUSED', 0
ar_s_ovnone: db 0
ar_s_ovgo:   db ' ANY KEY: PLAY ON', 0
ar_s_ovesc:  db ' ESC: BACK TO THE LIST', 0

; -----------------------------------------------------------------------------
; 12.3 the DIP switches, in words
; -----------------------------------------------------------------------------
; A game's switches (af_dsw, by its place in ar_boards; 0 = none it is safe
; to offer): dw the byte in the view they are, db the bits ARCADE sets
; there, db the defaults, db the settings; each: db its bits, a NUL name,
; db its choices, each a db value and a NUL word. ARCADE.DAT keeps each
; game's byte (af_dsv), 0FFh = the defaults.
DS_AT       equ 0
DS_MASK     equ 2
DS_DEF      equ 3
DS_N        equ 4
DS_SET      equ 5

af_dsw:     dw af_d_inv, 0, 0, 0, 0, 0
%ifdef ARC_PACMAN
            dw af_d_pm
%endif
            dw 0
af_d_inv:   dw V_IN2                ; MAME invaders' DSW: bits 0-1 the bases,
            db 0x0B, 0x00, 2        ;   bit 3 the bonus base's score
            db 0x03, 'BASES', 0, 4
            db 0x00, '3', 0, 0x01, '4', 0, 0x02, '5', 0, 0x03, '6', 0
            db 0x08, 'BONUS AT', 0, 2
            db 0x00, '1500', 0, 0x08, '1000', 0
%ifdef ARC_PACMAN
af_d_pm:    dw PM_DSW1              ; MAME pacman's DSW1: coins in 0-1 (left
            db 0xFC, 0xC8, 4        ;   at one a credit), lives, bonus, the
            db 0x0C, 'LIVES', 0, 4  ;   difficulty and the ghosts' names
            db 0x00, '1', 0, 0x04, '2', 0, 0x08, '3', 0, 0x0C, '5', 0
            db 0x30, 'BONUS AT', 0, 4
            db 0x00, '10000', 0, 0x10, '15000', 0, 0x20, '20000', 0, 0x30, 'NONE', 0
            db 0x40, 'DIFFICULTY', 0, 2
            db 0x40, 'NORMAL', 0, 0x00, 'HARD', 0
            db 0x80, 'GHOST NAMES', 0, 2
            db 0x80, 'NORMAL', 0, 0x00, 'OTHER', 0
%endif

; af_dsw_of - BX = a game's index: SI = its switches, ZF=1 none
af_dsw_of:
    push bx
    shl bx, 1
    mov si, [af_dsw+bx]
    pop bx
    or si, si
    ret

; af_dsget - BX = a game: AL = its switches' byte as set (the defaults if
; none kept)
af_dsget:
    mov al, [af_dsv+bx]
    cmp al, 0xFF
    jne .r
    push si
    call af_dsw_of
    jz .no
    mov al, [si+DS_DEF]
.no:
    pop si
.r:
    ret

; ar_dsapply - after a power-on (ar_reset1): the game's switches into its
; byte, the bits ARCADE does not set left as the board's reset made them
ar_dsapply:
    push ax
    push bx
    push si
    push es
    call ar_bdindex
    jc .r
    call af_dsw_of
    jz .r
    call af_dsget
    and al, [si+DS_MASK]
    mov ah, [si+DS_MASK]
    not ah
    mov bx, [si+DS_AT]
    mov es, [ar_vseg]
    and [es:bx], ah
    or [es:bx], al
.r:
    pop es
    pop si
    pop bx
    pop ax
    ret

; ar_bdindex - BX = [ar_bd]'s index in ar_boards, CF=1 none. Kept: all else
ar_bdindex:
    push ax
    push si
    mov ax, [ar_bd]
    mov si, ar_boards
    xor bx, bx
.f:
    cmp word [si], 0
    je .no
    cmp [si], ax
    je .yes
    inc si
    inc si
    inc bx
    jmp .f
.no:
    stc
    jmp short .r
.yes:
    clc
.r:
    pop si
    pop ax
    ret

; af_setcard - the card in SETTINGS mode: a line a setting, its name and
; the choice now; the one being changed marked. Into the band af_card made
; (DX = the first line's row); out DX past the last
af_setcard:
    mov bl, [af_sel]
    xor bh, bh
    call af_dsw_of
    jz .r
    call af_dsget
    mov [af_dsb], al
    mov cl, [si+DS_N]
    xor ch, ch
    add si, DS_SET
    xor bx, bx                      ; BX = the setting
.s:
    push cx
    push bx
    mov al, [si]                    ; its bits
    mov [af_dsm], al
    inc si
    push si
    mov si, af_s_mark               ; the mark, on the one being changed
    cmp bl, [af_dsi]
    je .mk
    mov si, af_s_nomark
.mk:
    xor bx, bx
    call af_bcaps
    pop si
    mov bx, 12                      ; the name
    call af_bcaps                   ; (SI past it)
    lodsb                           ; the choices: the one set, by its value
    mov cl, al
    xor ch, ch
    mov al, [af_dsb]
    and al, [af_dsm]
    mov [af_dsc], al
.c:
    lodsb                           ; a choice's value
    push si
    push cx
    cmp al, [af_dsc]
    jne .cnot
    mov bx, 84                      ; its word, at the right
    call af_bcaps
.cnot:
    pop cx
    pop si
.cskip:
    lodsb
    or al, al
    jnz .cskip
    loop .c
    pop bx
    pop cx
    inc bx
    mov di, [af_lay]
    add dx, [di+L_CLH]
    loop .s
.r:
    ret

af_s_mark:   db '>', 0
af_s_nomark: db ' ', 0
af_s_sethd:  db 'SETTINGS', 0
af_s_setkeys: db 'UP DOWN LEFT RIGHT', 0
af_s_noset:  db 'Arcade: no settings', 0

; af_setkey - in SETTINGS mode, W_ONKEY's: AL = ASCII, AH = scan. Up/Down
; the setting, Left/Right/Space/Enter its choice, Esc or Insert leaves.
; CF=0 taken
af_setkey:
    mov bl, [af_sel]
    xor bh, bh
    call af_dsw_of
    jz .leave
    cmp ah, 0x48
    jne .dn
    cmp byte [af_dsi], 0
    je .took
    dec byte [af_dsi]
    jmp short .redraw
.dn:
    cmp ah, 0x50
    jne .ch
    mov al, [af_dsi]
    inc al
    cmp al, [si+DS_N]
    jae .took
    mov [af_dsi], al
    jmp short .redraw
.ch:
    cmp ah, 0x4D                    ; Right, Space, Enter: the next choice
    je .next
    cmp al, ' '
    je .next
    cmp al, 13
    je .next
    cmp ah, 0x4B                    ; Left: the one before
    je .prev
    cmp al, 27
    je .leave
    cmp ah, 0x3C                    ; F2 again: out
    je .leave
    stc
    ret
.prev:
    mov byte [af_dsdir], 0xFF
    jmp short .turn
.next:
    mov byte [af_dsdir], 1
.turn:
    call af_dsturn
.redraw:
    call af_card
.took:
    clc
    ret
.leave:
    call af_setleave
    clc
    ret

; af_dsturn - the setting [af_dsi] of the selected game to its next choice
; (or the one before, [af_dsdir] = FFh), kept in af_dsv
af_dsturn:
    mov bl, [af_sel]
    xor bh, bh
    call af_dsw_of
    jz .r
    call af_dsget
    mov [af_dsb], al
    add si, DS_SET                  ; the setting [af_dsi]
    mov cl, [af_dsi]
    xor ch, ch
.find:
    jcxz .at
    inc si                          ; past its bits, name, choices
.n1:
    lodsb
    or al, al
    jnz .n1
    lodsb
    mov ah, al
.ch:
    inc si
.n2:
    lodsb
    or al, al
    jnz .n2
    dec ah
    jnz .ch
    loop .find
.at:
    lodsb                           ; its bits
    mov [af_dsm], al
.n3:
    lodsb                           ; (its name)
    or al, al
    jnz .n3
    lodsb                           ; its choices: which is set now
    mov cl, al
    xor ch, ch
    mov [af_dsk], cl
    mov al, [af_dsb]
    and al, [af_dsm]
    mov [af_dsc], al
    mov di, af_dsvals
    xor dx, dx                      ; DX = the one set (0 if none matched)
.v:
    lodsb
    mov [di], al
    inc di
    cmp al, [af_dsc]
    jne .vn
    mov dl, ch                      ; (CH counts up below)
.vn:
.n4:
    lodsb
    or al, al
    jnz .n4
    inc ch
    dec cl
    jnz .v
    mov al, dl                      ; the next (or the one before), round
    add al, [af_dsdir]
    cmp al, 0xFF
    jne .nw
    mov al, [af_dsk]
    dec al
.nw:
    cmp al, [af_dsk]
    jb .ok
    xor al, al
.ok:
    xor ah, ah
    mov di, ax
    mov al, [af_dsvals+di]
    mov ah, [af_dsm]
    not ah
    and [af_dsb], ah
    or [af_dsb], al
    mov al, [af_dsb]
    mov [af_dsv+bx], al
    mov byte [af_dsdirty], 1
.r:
    ret

; af_setenter - SETTINGS pressed: the card shows the switches, or a toast
; says the game has none
af_setenter:
    mov bl, [af_sel]
    xor bh, bh
    call af_dsw_of
    jnz .has
    mov si, af_s_noset
    jmp ar_toast
.has:
    mov byte [af_setm], 1
    mov byte [af_dsi], 0
    call af_card
    ret

; af_setleave - out of SETTINGS: the card as it was, and the switches kept
; in ARCADE.DAT if they moved (a game in memory is powered on again with
; them at its next play: ar_select reads them at the reset)
af_setleave:
    cmp byte [af_setm], 0
    je .r
    mov byte [af_setm], 0
    call af_card
    cmp byte [af_dsdirty], 0
    je .r
    mov byte [af_dsdirty], 0
    call ar_datwrite
    mov bl, [af_sel]                ; the game in memory: on again with them
    xor bh, bh
    shl bx, 1
    mov bx, [ar_boards+bx]
    cmp bx, [ar_bd]
    jne .r
    call ar_reset
.r:
    ret

; -----------------------------------------------------------------------------
; 12.4 high scores
; -----------------------------------------------------------------------------
; A game's best (af_hs, by its place in ar_boards; 0 = none known): dw where
; in its RAM (the view), db how many bytes. MAME's hiscore.dat idea, as
; addresses alone. ARCADE.DAT keeps AR_HSB bytes a game (ar_hsv).
AR_HSB      equ 4
AR_HSFROM   equ 300                 ; frames after a power-on: from here...
AR_HSTO     equ 1200                ;   ...to here, put back while it reads 0
af_hs:      dw af_h_inv, 0, 0, 0, 0, 0
%ifdef ARC_PACMAN
            dw af_h_pm
%endif
            dw 0
af_h_inv:   dw 0x20F4
            db 2
%ifdef ARC_PACMAN
af_h_pm:    dw 0x4E88
            db 4
%endif

; ar_hsof - BX = a game: SI = its best's place, ZF=1 none. Kept: BX
ar_hsof:
    push bx
    shl bx, 1
    mov si, [af_hs+bx]
    pop bx
    or si, si
    ret

; ar_hssave - as a game is left: its best, out of its RAM into ar_hsv - if it
; is better than the one kept (a game that never got to put it back has a
; zero there, and must not wipe the kept one)
ar_hssave:
    call ar_bdindex
    jc .r
    call ar_hsof
    jz .r
    mov di, bx
    shl di, 1
    shl di, 1
    add di, ar_hsv
    mov cl, [si+2]
    xor ch, ch
    mov si, [si]
    push ds
    mov es, [ar_vseg]
    xor ax, ax                      ; all zero: nothing to keep
    push si
    push cx
.z:
    or al, [es:si]
    inc si
    loop .z
    pop cx
    pop si
    or al, al
    jz .rds
    push di                         ; a change: kept, and ARCADE.DAT owed
    push si
    push cx
.cmp:
    mov al, [es:si]
    cmp al, [di]
    jne .diff
    inc si
    inc di
    loop .cmp
    pop cx
    pop si
    pop di
    jmp short .rds
.diff:
    pop cx
    pop si
    pop di
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [af_dsdirty], 1
.rds:
    pop ds
.r:
    ret

; ar_hsput - a frame's end in play: between AR_HSFROM and AR_HSTO frames
; after a power-on, the kept best put back while the game's own reads zero
; (its power-on test clears RAM, and may do it after a first put)
ar_hsput:
    mov ax, [ar_hsfr]
    cmp ax, AR_HSTO
    jae .r
    inc word [ar_hsfr]
    cmp ax, AR_HSFROM
    jb .r
    call ar_bdindex
    jc .r
    call ar_hsof
    jz .r
    mov di, bx
    shl di, 1
    shl di, 1
    add di, ar_hsv
    mov cl, [si+2]
    xor ch, ch
    mov si, [si]
    mov es, [ar_vseg]
    xor al, al                      ; a kept one at all?
    push di
    push cx
.k:
    or al, [di]
    inc di
    loop .k
    pop cx
    pop di
    or al, al
    jz .r
    push si                         ; the game's: still zero?
    push cx
    xor al, al
.g:
    or al, [es:si]
    inc si
    loop .g
    pop cx
    pop si
    or al, al
    jnz .r
.put:
    mov al, [di]
    mov [es:si], al
    inc si
    inc di
    loop .put
.r:
    ret

; ar_datwrite - ARCADE.DAT written: the places, the drives, the switches
; and the scores, one record (1.5, 12.3, 12.4)
ar_datwrite:
    push ds
    pop es
    mov si, ar_s_dat
    mov bx, ar_places
    mov cx, AR_DATSZ
    mov di, ar_datst
    call od_write
    ret
