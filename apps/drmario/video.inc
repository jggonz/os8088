; Native caches: no per-pixel conversion, multiplication or port selection
; inside the changing-cell copy loops. Source planes are 48 bytes per cell.
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
    push ds
    mov ds, dx
    mov di, dm_glyphs
    mov cx, 768
    rep movsb
    pop ds
    ; Fullscreen text uses ASCII 32..90 plus a/c/o/r in the DrMarco title.
    ; Fold those four glyphs into otherwise unused punctuation slots.
    mov di, dm_glyphs+59*8
%assign letter 'a'
%rep 4
    mov si, dm_glyphs+(letter-32)*8
    mov cx, 4
    rep movsw
%if letter == 'a'
%assign letter 'c'
%elif letter == 'c'
%assign letter 'o'
%else
%assign letter 'r'
%endif
%endrep
    ret

dm_fontbuild:
    push ds
    pop es
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
    mov si, dm_glyphs
    mov di, dm_fontcga
    mov cx, 504
.cgarow:
    lodsb
    xor dx, dx
    mov bp, 8
.bit:
    shl dx, 1
    shl dx, 1
    shl al, 1
    jnc .zero
    or dl, 3
.zero:
    dec bp
    jnz .bit
    mov al, dh
    stosb
    mov al, dl
    stosb
    loop .cgarow
    ret

dm_addresses:
    mov ax, dm_vgaaddr
    cmp byte [dm_cga], 0
    je .vga
    mov ax, dm_cgaaddr
.vga:
    mov [dm_addrtab], ax
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

dm_unpack:
    xor cx, cx
.run:
    lodsb
    or al, al
    jz .done
    mov cl, al
    lodsb
    rep stosb
    jmp .run
.done: ret

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
    mov si, [dm_addrtab]
    mov di, [si+bx]
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
    mov byte [dm_mask], 1
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
    mov cx, 12
.row:
    movsw
    movsw
    add di, 76
    loop .row
    add bx, 4
    cmp bx, [dm_qend]
    jb .tile
    add word [dm_planeoff], 48
    shl byte [dm_mask], 1
    cmp byte [dm_mask], 16
    jb .plane
.done: ret
.cga:
    mov bx, dm_queue
.cgat:
    mov di, [bx]
    mov si, [bx+2]
    mov cx, 5
.cgar:
    movsw
    movsw
    add di, 8192-4
    movsw
    movsw
    sub di, 8192+4-80
    loop .cgar
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
    sub al, 32
    push bx
    mov bx, dm_fontmap
    xlat
    pop bx
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
    ; Only NEXT (row 0) and the state prompt (row 14) own right HUD cells.
    ; Blank rows would erase the CGA preview, character art or footer.
    test si, 8
    jz .hudcell
    cmp si, 24
    jb .hudcell
    cmp si, 240
    jae .same
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

dm_titlecaps: db 'DrMarco',0
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

align 2
dm_addrtab: dw dm_vgaaddr
dm_oldnext: dw 0ffffh
dm_vgaaddr:
%assign i 0
%rep 128
    dw (32+(i/8)*12)*80+24+(i % 8)*4
%assign i i+1
%endrep
dm_cgaaddr:
%assign i 0
%rep 128
    dw (12+(i/8)*5)*80+24+(i % 8)*4
%assign i i+1
%endrep
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
dm_vgaptrs:
%assign i 0
%rep 23
    dw dm_vgatiles+i*192
%assign i i+1
%endrep
dm_cgaptrs:
%assign i 0
%rep 23
    dw dm_cgatiles+i*40
%assign i i+1
%endrep
dm_vgatiles: incbin "dm-vga.bin"
dm_cgatiles: incbin "dm-cga.bin"
dm_screenvga: incbin "dm-screen-vga.bin"
dm_screencga: incbin "dm-screen-cga.bin"

; Packed native glyph indices. Only the title uses lowercase.
dm_fontmap:
%assign ch 32
%rep 96
%if ch == 'a'
    db 59
%elif ch == 'c'
    db 60
%elif ch == 'o'
    db 61
%elif ch == 'r'
    db 62
%elif ch <= 'Z'
    db ch-32
%else
    db 0
%endif
%assign ch ch+1
%endrep
