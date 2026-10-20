; Native caches: no per-pixel conversion, multiplication or port selection
; inside the changing-cell copy loops. A cell stores its source rows only:
; two 32-byte VGA planes, each written to a pair of planes (0/1, 2/3), or 32
; CGA bytes; the copy loops repeat the rows the vertical scale doubles.
dm_palette:
    cmp byte [dm_cga], 0
    je .vga
    mov dx, 3d9h
    mov al, 10h
    out dx, al
    ret
.vga:
    mov dx, 3c8h
    xor al, al
    out dx, al
    inc dx
    mov si, dm_dac
    mov cx, 24
.loop:
    lodsb
    out dx, al
    loop .loop
    ret
dm_dac: db 0,0,0, 12,24,63, 63,12,16, 63,56,8
        db 5,10,30, 30,4,5, 30,24,2, 63,63,63

; Stage the 63 glyphs fullscreen text uses in dm_glyphs (the render queue,
; idle at mode entry): ASCII 32..90, then a/c/o/r for the DrMarco title in
; four otherwise unused punctuation slots.
dm_fontcache:
    call OSAPI_FONT_GLYPHS
    mov bl, 32
    sub bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add si, bx
    push ds
    pop es
    mov di, dm_glyphs
    push ds
    mov ds, dx
    mov cx, 59*4
    rep movsw
    mov bx, si                  ; ASCII 91
%assign letter 'a'
%rep 4
    lea si, [bx+(letter-91)*8]
    mov cl, 4
    rep movsw
%if letter == 'a'
%assign letter 'c'
%elif letter == 'c'
%assign letter 'o'
%else
%assign letter 'r'
%endif
%endrep
    pop ds
    ret

dm_fontbuild:
    call dm_fontcache
    cmp byte [dm_cga], 0
    jne .cga
    mov bx, dm_glyphs
    mov di, dm_fontvga
    mov bp, 63
.glyph:
    mov dx, 0880h
.plane:
    mov si, bx
    mov cx, 8
.row:
    mov ah, [si]
    inc si
    xor al, al
    test ah, dl
    jz .a
    mov al, 7
.a: stosb
    xor al, al
    test ah, dh
    jz .b
    mov al, 7
.b: stosb
    loop .row
    shr dl, 1
    shr dh, 1
    jnz .plane
    add bx, 8
    dec bp
    jnz .glyph
    ret
.cga:
    ; Each glyph bit becomes a pixel of ink 3: a nibble is one CGA byte.
    mov si, dm_glyphs
    mov di, dm_fontcga
    mov bx, dm_cgaink
    mov cl, 4
    mov dx, 504
.cgarow:
    lodsb
    mov ah, al
    shr al, cl
    xlat
    stosb
    mov al, ah
    and al, 0fh
    xlat
    stosb
    dec dx
    jnz .cgarow
    ret
dm_cgaink:
%assign n 0
%rep 16
    db ((n&8)/8*0c0h)|((n&4)/4*30h)|((n&2)/2*0ch)|(n&1)*3
%assign n n+1
%endrep

; Board cell -> video offset, built for this mode's geometry at mode entry:
; 16 rows of 8 cells, 12 (VGA) or 5 (CGA bank) scanlines a row.
dm_addresses:
    push ds
    pop es
    mov di, dm_celladdr
    mov ax, 32*80+24
    mov dx, 12*80
    cmp byte [dm_cga], 0
    je .fill
    mov ax, 12*80+24
    mov dx, 5*80
.fill:
    mov bx, 16
.row:
    push ax
    mov cx, 8
.cell:
    stosw
    add ax, 4
    loop .cell
    pop ax
    add ax, dx
    dec bx
    jnz .row
    ret

dm_invalidate:
    push ds
    pop es
    mov di, dm_shadow
    mov cx, 128
    mov al, 0ffh
    rep stosb
    mov di, dm_hudshadow
    mov cx, 256
    rep stosb
    mov word [dm_oldnext], 0ffffh
    mov byte [dm_dirty], 2
    mov byte [dm_huddirty], 1
    mov word [dm_actorshadow], 0ffffh
    mov word [dm_actorshadow+2], 0ffffh
    mov byte [dm_animdirty], 1
    ret

; AX=byte x (x/4), BX=y, CX=byte width, DX=height, BP=solid pixel.
; VGA maps all four planes simultaneously; CGA uses packed repeated ink.
dm_rect:
    SAVE
    mov si, ax
    mov ax, [dm_fsi+FSI_SEG]
    mov es, ax
    cmp byte [dm_cga], 0
    jne .row
    push dx
    mov dx, 3c4h
    mov ax, 0f02h
    out dx, ax
    pop dx
.row:
    push dx
    push cx
    mov ax, bx
    xor di, di
    cmp byte [dm_cga], 0
    je .linear
    shr ax, 1
    jnc .linear
    mov di, 2000h
.linear:
    mov dx, 80
    mul dx
    add di, ax
    add di, si
    mov ax, bp
    rep stosb
    pop cx
    pop dx
    inc bx
    dec dx
    jnz .row
    RESTORE
    ret

; Background is decoded once on mode entry, directly into each native plane
; or bank. No scratch framebuffer and no work on incremental game frames.
dm_background:
    SAVE
    mov ax, [dm_fsi+FSI_SEG]
    mov es, ax
    cmp byte [dm_cga], 0
    jne .cga
    mov si, dm_screenvga
    mov bx, 0102h
.plane:
    mov dx, 3c4h
    mov ax, bx
    out dx, ax
    xor di, di
    call dm_unpack
    shl bh, 1
    cmp bh, 16
    jb .plane
    jmp .done
.cga:
    mov si, dm_screencga
    xor di, di
    call dm_unpack
    mov di, 2000h
    call dm_unpack
.done:
    RESTORE
    ret

; DS:SI one plane/bank stream, ES:DI its video memory. Each row's packets
; are decoded into dm_queue, which still holds the previous row: a copy
; packet, and a row's zero terminator for the rest of it, leave bytes as the
; row above had them. The row is then stored once per repeat. dm_queue is
; idle here (dm_render rebuilds it) and nothing reads video memory.
dm_unpack:
    push bx
    push dx
    push bp
    mov bp, es
    xor ax, ax
    xor cx, cx
.rows:
    lodsb                       ; count of identical native scanlines
    or al, al
    jz .done
    mov bl, al
    mov dx, di
    push ds
    pop es
    mov di, dm_queue
    cmp byte [dm_cga], 0
    je .vgarun
.run:
    lodsb
    or al, al
    jz .rowdone
    js .high
    mov cl, al
    lodsb
    rep stosb
    jmp .run
.high:
    cmp al, 192
    jae .ccopy
    and al, 7fh
    mov cl, al
    inc cx
    rep movsb
    jmp .run
.ccopy:
    sub al, 191
    add di, ax
    jmp .run
.vgarun:
    lodsb
    cmp al, 8
    jb .vcopy
    mov cl, al
    shr cl, 1
    shr cl, 1
    shr cl, 1
    and al, 7
    rep stosb
    jmp .vgarun
.vcopy:
    cmp al, 7
    jb .vskip
    lodsb
.vskip:
    add di, ax
    or al, al
    jnz .vgarun
.rowdone:
    mov di, dx
    mov es, bp
    mov dx, si
.repeat:
    mov si, dm_queue
    mov cl, 40
    rep movsw
    dec bl
    jnz .repeat
    mov si, dx
    jmp .rows
.done:
    pop bp
    pop dx
    pop bx
    ret

dm_framepaint:
    call dm_background
    ; Static bottle is drawn only when changing mode.
    mov ax, 23
    mov bx, 28
    mov cx, 34
    mov dx, 200
    mov bp, 4
    cmp byte [dm_cga], 0
    je .frame
    mov bx, 20
    mov dx, 168
    mov bp, 55h
.frame:
    call dm_rect
    mov ax, 24
    add bx, 4
    mov cx, 32
    sub dx, 8
    xor bp, bp
    call dm_rect
    mov si, dm_titlecaps
    mov ax, 132
    mov dx, 8
    call dm_text
    mov si, dm_footer
    mov ax, 32
    mov dx, 230
    cmp byte [dm_cga], 0
    je .foot
    mov dx, 190
.foot:
    call dm_text
    call dm_render
    ret

dm_render:
    cmp byte [dm_dirty], 0
    jne .compose
    cmp byte [dm_huddirty], 0
    jne .compose
    jmp dm_animpaint             ; unchanged animation: no VRAM writes
.compose:
    mov word [dm_qend], dm_queue
    cmp byte [dm_dirty], 0
    je .preview
    cmp byte [dm_dirty], 1
    jne .whole
    cmp word [dm_olda], 128
    jb .partial
.whole:
    mov byte [dm_dirty], 0
    mov word [dm_olda], 128
    mov word [dm_oldb], 128
    push ds
    pop es
    mov si, dm_board
    mov di, dm_scene
    mov cx, 64
    rep movsw
    cmp byte [dm_state], 0
    jne .flash
    call dm_valid
    jc .diff
    mov [dm_olda], bx
    mov [dm_oldb], si
    call dm_halves
    mov [dm_scene+bx], al
    mov [dm_scene+si], ah
    jmp .diff
.flash:
    cmp byte [dm_state], 1
    jne .diff
    xor bx, bx
.flashcell:
    cmp byte [dm_marks+bx], 0
    je .flashnext
    mov byte [dm_scene+bx], 60h ; special bright checker tile
.flashnext:
    inc bx
    cmp bx, 128
    jb .flashcell
.diff:
    xor bx, bx
.cell:
    call dm_cellcheck
    inc bx
    cmp bx, 128
    jb .cell
    jmp .preview
.partial:
    ; A moving capsule can affect only its two old and two new cells.
    ; Restore the old underlying cells, overlay the new capsule, then check
    ; these four candidates. Shadow updates automatically deduplicate overlap.
    mov byte [dm_dirty], 0
    mov bx, [dm_olda]
    mov [dm_candidates], bx
    mov al, [dm_board+bx]
    mov [dm_scene+bx], al
    mov bx, [dm_oldb]
    mov [dm_candidates+2], bx
    mov al, [dm_board+bx]
    mov [dm_scene+bx], al
    call dm_valid
    mov [dm_olda], bx
    mov [dm_oldb], si
    mov [dm_candidates+4], bx
    mov [dm_candidates+6], si
    call dm_halves
    mov [dm_scene+bx], al
    mov [dm_scene+si], ah
    mov bp, dm_candidates
    mov cx, 4
.candidate:
    mov bx, [ds:bp]
    call dm_cellcheck
    add bp, 2
    loop .candidate
.preview:
    mov ax, [dm_next]
    cmp ax, [dm_oldnext]
    je .flush
    mov [dm_oldnext], ax
    push ax
    or al, 10h
    mov di, 52*80+60
    cmp byte [dm_cga], 0
    je .p1
    mov di, 20*80+60          ; CGA y=40: below NEXT, above the bouncing doctor
.p1:
    push di
    call dm_enqueue
    pop di
    add di, 4
    pop ax
    mov al, ah
    or al, 20h
    call dm_enqueue
.flush:
    call dm_flush
    cmp byte [dm_huddirty], 0
    je .out
    mov byte [dm_huddirty], 0
    call dm_hudcompose
    call dm_hudpaint
.out: jmp dm_animpaint

; BX=cell index. Preserve BX/CX/BP for either dirty traversal.
dm_cellcheck:
    mov al, [dm_scene+bx]
    cmp al, 51h
    jb .compare
    cmp al, 53h
    ja .compare
    push bx
    mov ah, bl
    and ah, 8
    shr ah, 1                   ; odd board rows use the second phase bank
    mov bl, al
    and bx, 3
    or bl, ah
    cmp byte [dm_viruspose+bx], 0
    pop bx
    je .compare
    add al, 20h
.compare:
    cmp al, [dm_shadow+bx]
    je .out
    mov [dm_shadow+bx], al
    push bx
    shl bx, 1
    mov di, [dm_celladdr+bx]
    call dm_enqueue
    pop bx
.out: ret

; AL board code -> pointer, DI video offset. Queue preserves neither.
dm_enqueue:
    xor ah, ah
    mov bx, ax
    mov bl, [dm_tilemap+bx]
    xor bh, bh
    shl bx, 1
    mov si, [dm_vgaptrs+bx]
    cmp byte [dm_cga], 0
    je .put
    mov si, [dm_cgaptrs+bx]
.put:
    mov bx, [dm_qend]
    mov [bx], di
    mov [bx+2], si
    add bx, 4
    mov [dm_qend], bx
    ret

dm_flush:
    mov ax, [dm_fsi+FSI_SEG]
    mov es, ax
    cmp word [dm_qend], dm_queue
    je .done
    cmp byte [dm_cga], 0
    jne .cga
    ; Cells are horizontally doubled, so planes 0/1 and 2/3 hold the same
    ; bytes: two passes, each writing one 32-byte plane to two planes. A
    ; plane stores the cell's 8 source rows; the unrolled 12 rows step back
    ; over the four the vertical scale repeats (y*8/12: 0, 2, 4 and 6).
    mov byte [dm_mask], 3
    mov word [dm_planeoff], 0
.plane:
    mov dx, 3c4h
    mov al, 2
    mov ah, [dm_mask]
    out dx, ax
    mov bx, dm_queue
.tile:
    mov di, [bx]
    mov si, [bx+2]
    add si, [dm_planeoff]
%assign y 0
%rep 12
    movsw
    movsw
%if y < 11
    add di, 76
%endif
%if y == 0 || y == 3 || y == 6 || y == 9
    sub si, 4
%endif
%assign y y+1
%endrep
    add bx, 4
    cmp bx, [dm_qend]
    jb .tile
    add word [dm_planeoff], 32
    shl byte [dm_mask], 1
    shl byte [dm_mask], 1
    cmp byte [dm_mask], 16
    jb .plane
.done: ret
.cga:
    mov bx, dm_queue
.cgat:
    ; Rows alternate banks. 8 source rows make 10 (y*8/10 repeats 0 and 4).
    mov di, [bx]
    mov si, [bx+2]
%assign y 0
%rep 10
    movsw
    movsw
%if y < 9
%if y % 2 == 0
    add di, 8192-4
%else
    sub di, 8192+4-80
%endif
%endif
%if y == 0 || y == 5
    sub si, 4
%endif
%assign y y+1
%endrep
    add bx, 4
    cmp bx, [dm_qend]
    jb .cgat
    ret

; Static string; AX=x, DX=y. Glyph routine keeps all registers.
dm_text:
    mov bx, ax
.next:
    lodsb
    or al, al
    jz .out
    call dm_glyph
    add bx, 8
    jmp .next
.out: ret

; AL=ASCII, BX=x, DX=y. Native glyphs opaque, preserving all registers.
dm_glyph:
    SAVE
    sub al, 32                  ; ASCII 32..94 is glyph 0..62
    xor ah, ah
    mov si, ax
    mov cl, 6
    shl si, cl
    add si, dm_fontvga
    mov ax, [dm_fsi+FSI_SEG]
    mov es, ax
    mov ax, dx
    cmp byte [dm_cga], 0
    jne .cga
    mov cx, 80
    mul cx
    shr bx, 1
    shr bx, 1
    add ax, bx
    mov bp, ax
    mov bx, 0102h
.plane:
    mov dx, 3c4h
    mov ax, bx
    out dx, ax
    mov di, bp
    mov cx, 8
.row:
    movsw
    add di, 78
    loop .row
    shl bh, 1
    cmp bh, 16
    jb .plane
    jmp .done
.cga:
    ; y is always even for cached HUD and static labels.
    shr ax, 1
    mov cx, 80
    mul cx
    shr bx, 1
    shr bx, 1
    add ax, bx
    mov di, ax
    sub si, dm_fontvga
    shr si, 1
    shr si, 1
    add si, dm_fontcga
    mov cx, 4
.cgarow:
    movsw
    add di, 8192-2
    movsw
    sub di, 8192+2-80
    loop .cgarow
.done:
    RESTORE
    ret

; HUD has sixteen 16-character rows, left 8 then right 8. The bottle
; occupies the middle. Values replace old glyphs, never their whole panel.
dm_hudcompose:
    push ds
    pop es
    mov di, dm_hud
    mov al, ' '
    mov cx, 256
    rep stosb
%macro HUD 2
    mov si, %1
    mov di, dm_hud+%2
    call dm_hudstr
%endmacro
    HUD dm_lscore,0
    HUD dm_llevel,48
    HUD dm_lspeed,96
    HUD dm_lvirus,144
    HUD dm_lnext,8
    xor bx, bx
    mov bl, [dm_music]
    shl bx, 1
    mov si, [dm_musicnames+bx]
    mov di, dm_hud+192
    mov cx, 5
    rep movsb
    mov di, dm_hud+16
    call dm_scoretext
    xor ax, ax
    mov al, [dm_level]
    mov di, dm_hud+64
    call dm_decimal2
    xor ax, ax
    mov al, [dm_viruses]
    mov di, dm_hud+160
    call dm_decimal2
    xor bx, bx
    mov bl, [dm_speed]
    shl bx, 1
    mov si, [dm_speednames+bx]
    mov di, dm_hud+112
    call dm_hudstr
    cmp byte [dm_paused], 0
    je .state
    HUD dm_lpaused,224
    ret
.state:
    cmp byte [dm_state], 3
    jne .win
    HUD dm_lgame,208
    HUD dm_lover,224
    HUD dm_lenter,232
    ret
.win:
    cmp byte [dm_state], 4
    jne .out
    HUD dm_lclear,224
    HUD dm_lenter,232
.out: ret

dm_hudstr:
    lodsb
    or al, al
    jz .out
    stosb
    jmp dm_hudstr
.out: ret

dm_decimal2:
    mov bl, 10
    div bl
    add ax, 3030h
    mov [di], al
    mov [di+1], ah
    ret

dm_scoretext:
    ; Seven digits, saturate for display at 9,999,999 (score stays 32-bit).
    mov ax, [dm_score]
    mov dx, [dm_score+2]
    cmp dx, 152
    jb .start
    ja .cap
    cmp ax, 38527
    jbe .start
.cap:
    mov dx, 152
    mov ax, 38527
.start:
    add di, 6
    mov bp, 7
    mov bx, 10
.digit:
    push ax
    mov ax, dx
    xor dx, dx
    div bx
    mov cx, ax
    pop ax
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    mov dx, cx
    dec bp
    jnz .digit
    ret

dm_hudpaint:
    xor si, si
.next:
    cmp si, 240                 ; unused row 15 must not erase the clipboard
    jae .same
    ; Only NEXT (row 0) and the state prompt (row 14) own right HUD cells.
    ; Blank rows would erase the CGA preview, character art or footer.
    test si, 8
    jz .hudcell
    cmp si, 24
    jb .hudcell
    cmp si, 224
    jb .same
.hudcell:
    mov al, [dm_hud+si]
    cmp al, [dm_hudshadow+si]
    je .same
    mov [dm_hudshadow+si], al
    mov bx, si
    and bx, 7
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, 8
    test si, 8
    jz .left
    add bx, 232
.left:
    push ax
    mov ax, si
    mov cl, 4
    shr ax, cl
    mov dx, 10
    mul dx
    add ax, 32
    ; State prompt sits below the portrait on both display geometries.
    test si, 8
    jz .positioned
    cmp si, 224
    jb .positioned
    add ax, 32
    cmp byte [dm_cga], 0
    je .positioned
    sub ax, 28
.positioned:
    mov dx, ax
    pop ax
    call dm_glyph
.same:
    inc si
    cmp si, 256
    jb .next
    ret

; Fullscreen text is ASCII 32..90; the title's a/c/o/r are drawn from the
; glyphs dm_fontcache folds into slots 91..94, so it spells them that way.
dm_titlecaps: db 'D',94,'M',91,94,92,93,0     ; DrMarco
dm_footer: db 'Z/X ROTATE P PAUSE M MUSIC ESC EXIT',0
dm_lscore: db 'SCORE',0
dm_llevel: db 'LEVEL',0
dm_lspeed: db 'SPEED',0
dm_lvirus: db 'VIRUS',0
dm_lnext: db 'NEXT',0
dm_lpaused: db 'PAUSED',0
dm_lgame: db 'GAME',0
dm_lover: db 'OVER',0
dm_lenter: db 'ENTER',0
dm_lclear: db 'CLEAR!',0

dm_tilemap:
%assign i 0
%rep 116
%if i = 96
    db 19
%elif i >= 113 && i <= 115
    db i-93
%elif (i & 15) >= 1 && (i & 15) <= 3 && (i >> 4) <= 5
    db (i >> 4)*3+(i & 15)
%else
    db 0
%endif
%assign i i+1
%endrep
align 2
dm_vgaptrs:
%assign i 0
%rep 23
    dw dm_vgatiles+i*64
%assign i i+1
%endrep
dm_cgaptrs:
%assign i 0
%rep 23
    dw dm_cgatiles+i*32
%assign i i+1
%endrep
dm_vgatiles: incbin "dm-vga.bin"
dm_cgatiles: incbin "dm-cga.bin"
dm_screenvga: incbin "dm-screen-vga.bin"
dm_screencga: incbin "dm-screen-cga.bin"

