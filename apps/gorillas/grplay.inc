; Feature parity helpers. All animation steps run under the caller's graphics
; lock, with no waits. The scene always contains the currently visible frame.

; AX=exclusive upper bound -> AX=random 0..bound-1; preserve other registers.
gr_random:
    push bx
    push dx
    mov bx, ax
    call gr_rand
    ; Scale the full word: low LCG bits alone alternate on two-way choices.
    mul bx
    mov ax, dx
    pop dx
    pop bx
    ret

gr_scoreheader:
    mov si, gr_name1
    cmp byte [gr_turn], 0
    je .name
    mov si, gr_name2
.name:
    mov di, gr_status+1
    call gr_namecopy
    xor ax, ax
    mov al, [gr_scores]
    mov di, gr_status+21
    call gr_number
    mov al, [gr_scores+1]
    mov di, gr_status+27
    call gr_number
    ret

; Proportional arrow in a reserved strip below the buildings. Not terrain.
gr_windarrow:
    mov ax, [gr_wind]
    or ax, ax
    jz .out
    shl ax, 1
    add ax, 128
    mov si, ax
    mov cx, 128
    cmp cx, si
    jbe .line
    xchg cx, si
.line:
    mov dx, 123
    mov di, dx
    mov al, 2
    call gr_rect
    mov cx, [gr_wind]
    shl cx, 1
    add cx, 128
    mov bx, -1
    cmp word [gr_wind], 0
    jg .head
    neg bx
.head:
    add cx, bx
    dec dx
    call gr_put
    add dx, 2
    call gr_put
    add cx, bx
    inc dx
    call gr_put
    sub dx, 4
    call gr_put
.out:
    ret

gr_results:
    call gr_bgmstop
    call gr_musicstop
    mov byte [gr_state], 8
    call gr_clear
    mov si, gr_gameover
    mov dx, 16
    call gr_fronttext
    mov si, gr_name1
    mov di, gr_resultline+4
    call gr_namecopy
    xor ax, ax
    mov al, [gr_scores]
    mov di, gr_resultline+23
    call gr_number
    mov si, gr_resultline
    mov dx, 40
    call gr_fronttext
    mov si, gr_name2
    mov di, gr_resultline+4
    call gr_namecopy
    mov al, [gr_scores+1]
    mov di, gr_resultline+23
    call gr_number
    mov si, gr_resultline
    mov dx, 56
    call gr_fronttext
    mov si, gr_name1
    cmp byte [gr_winner], 0
    je .name
    mov si, gr_name2
.name:
    mov di, gr_winsmsg+4
    call gr_namecopy
    mov si, gr_winsmsg
    mov dx, 80
    call gr_fronttext
    mov si, gr_continue
    mov dx, 112
    call gr_fronttext
    call gr_frontapes
    jmp gr_fullpaint

gr_gameover: db '           GAME OVER!',0
gr_resultline: db '                                ',0
gr_winsmsg: db '               wins!',0

; BP=gorilla word offset, SI=pose; replace only its 16x20 sprite rectangle.
gr_posepaint:
    push si
    mov cx, [ds:gr_gx+bp]
    mov dx, [ds:gr_gy+bp]
    mov si, cx
    add si, 15
    mov di, dx
    add di, 19
    xor al, al
    call gr_rect
    pop si
    call gr_gorillapose
    mov ax, [ds:gr_gx+bp]
    and ax, 0fff8h
    mov bx, [ds:gr_gy+bp]
    mov cx, 24
    mov dx, 20
    jmp gr_blit

gr_throwpose:
    xor bx, bx
    mov bl, [gr_turn]
    shl bx, 1
    mov bp, bx
    mov si, gr_apeleft
    or bx, bx
    jz .paint
    mov si, gr_aperight
.paint:
    mov byte [gr_throwticks], 2
    jmp gr_posepaint

gr_throwtick:
    dec byte [gr_throwticks]
    jnz .out
    xor bx, bx
    mov bl, [gr_turn]
    shl bx, 1
    mov bp, bx
    mov si, gr_ape
    call gr_posepaint
    cmp word [gr_power], 2
    jae .out
    ; The reference treats velocity 0 or 1 as an immediate self-hit.
    mov si, bp
    jmp gr_tick.hitape
.out:
    ret

; CX,DX=blast center, AL=maximum radius. State determines terrain or ape.
gr_blaststart:
    mov [gr_hitx], cx
    mov [gr_hity], dx
    mov [gr_blastmax], al
    mov byte [gr_blast], 1
    ret

gr_blasttick:
    xor bx, bx
    mov bl, [gr_blast]
    xor ax, ax
    mov al, [gr_blastmax]
    cmp bx, ax
    ja .shrink
    mov al, 2
    cmp byte [gr_state], 1
    je .draw
    mov ax, bx
    and al, 1
    add al, 1
.draw:
    call gr_disc
    jmp short .paint
.shrink:
    shl ax, 1
    sub ax, bx
    inc ax
    mov bx, ax
    xor al, al
    call gr_disc
    dec bx
    jz .paint
    mov al, 2
    call gr_disc
.paint:
    mov ax, [gr_hitx]
    sub ax, 24
    and ax, 0fff8h
    jns .x
    xor ax, ax
.x:
    mov bx, [gr_hity]
    sub bx, 20
    cmp bx, 24
    jge .y
    mov bx, 24
.y:
    ; Alignment can move the left edge seven pixels: retain the entire
    ; radius-18 disc at the right edge as well.
    mov cx, 56
    mov dx, 40
    call gr_blit
    inc byte [gr_blast]
    mov al, [gr_blastmax]
    shl al, 1
    cmp [gr_blast], al
    jbe .out
    mov byte [gr_blast], 0
    call gr_sunreset
    cmp byte [gr_state], 1
    je gr_tick.miss
    mov byte [gr_dancing], 9
    jmp gr_hudpaint
.out:
    ret

; Filled circle, BX=radius 1..18, AL=ink. Precomputed half-widths keep the
; hot loop to one packed horizontal span per row, with no per-pixel squares.
gr_disc:
    SAVE
    mov [gr_discink], al
    mov bp, bx
    shl bx, 1
    mov bx, [gr_disctable+bx]
    mov dx, [gr_hity]
    sub dx, bp
    shl bp, 1
    inc bp
.row:
    xor ax, ax
    mov al, [bx]
    inc bx
    mov cx, [gr_hitx]
    mov si, cx
    sub cx, ax
    jns .left
    xor cx, cx
.left:
    add si, ax
    cmp dx, 24
    jl .next
    cmp dx, 116
    jae .next
    mov di, dx
    mov al, [gr_discink]
    call gr_rect
.next:
    inc dx
    dec bp
    jnz .row
    RESTORE
    ret

gr_roundtick:
    cmp byte [gr_roundwait], 0
    je .out
    dec byte [gr_roundwait]
    jnz .out
    cmp byte [gr_state], 3
    je gr_results
    call gr_city
    jmp gr_fullpaint
.out:
    ret

gr_disctable: dw 0,gr_disc1,gr_disc2,gr_disc3,gr_disc4,gr_disc5,gr_disc6,gr_disc7,gr_disc8,gr_disc9,gr_disc10,gr_disc11,gr_disc12,gr_disc13,gr_disc14,gr_disc15,gr_disc16,gr_disc17,gr_disc18
gr_disc1: db 0,1,0
gr_disc2: db 0,1,2,1,0
gr_disc3: db 0,2,2,3,2,2,0
gr_disc4: db 0,2,3,3,4,3,3,2,0
gr_disc5: db 0,3,4,4,4,5,4,4,4,3,0
gr_disc6: db 0,3,4,5,5,5,6,5,5,5,4,3,0
gr_disc7: db 0,3,4,5,6,6,6,7,6,6,6,5,4,3,0
gr_disc8: db 0,3,5,6,6,7,7,7,8,7,7,7,6,6,5,3,0
gr_disc9: db 0,4,5,6,7,8,8,8,8,9,8,8,8,8,7,6,5,4,0
gr_disc10: db 0,4,6,7,8,8,9,9,9,9,10,9,9,9,9,8,8,7,6,4,0
gr_disc11: db 0,4,6,7,8,9,9,10,10,10,10,11,10,10,10,10,9,9,8,7,6,4,0
gr_disc12: db 0,4,6,7,8,9,10,10,11,11,11,11,12,11,11,11,11,10,10,9,8,7,6,4,0
gr_disc13: db 0,5,6,8,9,10,10,11,12,12,12,12,12,13,12,12,12,12,12,11,10,10,9,8,6,5,0
gr_disc14: db 0,5,7,8,9,10,11,12,12,13,13,13,13,13,14,13,13,13,13,13,12,12,11,10,9,8,7,5,0
gr_disc15: db 0,5,7,9,10,11,12,12,13,13,14,14,14,14,14,15,14,14,14,14,14,13,13,12,12,11,10,9,7,5,0
gr_disc16: db 0,5,7,9,10,11,12,13,13,14,14,15,15,15,15,15,16,15,15,15,15,15,14,14,13,13,12,11,10,9,7,5,0
gr_disc17: db 0,5,8,9,10,12,12,13,14,15,15,15,16,16,16,16,16,17,16,16,16,16,16,15,15,15,14,13,12,12,10,9,8,5,0
gr_disc18: db 0,5,8,9,11,12,13,14,14,15,16,16,16,17,17,17,17,17,18,17,17,17,17,17,16,16,16,15,14,14,13,12,11,9,8,5,0

; Seven-row banana masks, clockwise rotation.
gr_bananas:
    db 0,48,96,96,96,48,24
    db 0,56,124,196,128,0,0
    db 48,24,12,12,12,24,0
    db 0,0,2,70,124,56,0
